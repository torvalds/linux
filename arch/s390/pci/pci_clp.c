/*
 * Copyright IBM Corp. 2012
 *
 * Author(s):
 *   Jan Glauber <jang@linux.vnet.ibm.com>
 */

#define COMPONENT "zPCI"
#define pr_fmt(fmt) COMPONENT ": " fmt

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <asm/pci_debug.h>
#include <asm/pci_clp.h>

/*
 * Call Logical Processor
 * Retry logic is handled by the caller.
 */
static inline u8 clp_instr(void *data)
{
	struct { u8 _[CLP_BLK_SIZE]; } *req = data;
	u64 ignored;
	u8 cc;

	asm volatile (
		"	.insn	rrf,0xb9a00000,%[ign],%[req],0x0,0x2\n"
		"	ipm	%[cc]\n"
		"	srl	%[cc],28\n"
		: [cc] "=d" (cc), [ign] "=d" (ignored), "+m" (*req)
		: [req] "a" (req)
		: "cc");
	return cc;
}

static void *clp_alloc_block(gfp_t gfp_mask)
{
	return (void *) __get_free_pages(gfp_mask, get_order(CLP_BLK_SIZE));
}

static void clp_free_block(void *ptr)
{
	free_pages((unsigned long) ptr, get_order(CLP_BLK_SIZE));
}

static void clp_store_query_pci_fngrp(struct zpci_dev *zdev,
				      struct clp_rsp_query_pci_grp *response)
{
	zdev->tlb_refresh = response->refresh;
	zdev->dma_mask = response->dasm;
	zdev->msi_addr = response->msia;
	zdev->fmb_update = response->mui;

	pr_debug("Supported number of MSI vectors: %u\n", response->noi);
	switch (response->version) {
	case 1:
		zdev->max_bus_speed = PCIE_SPEED_5_0GT;
		break;
	default:
		zdev->max_bus_speed = PCI_SPEED_UNKNOWN;
		break;
	}
}

static int clp_query_pci_fngrp(struct zpci_dev *zdev, u8 pfgid)
{
	struct clp_req_rsp_query_pci_grp *rrb;
	int rc;

	rrb = clp_alloc_block(GFP_KERNEL);
	if (!rrb)
		return -ENOMEM;

	memset(rrb, 0, sizeof(*rrb));
	rrb->request.hdr.len = sizeof(rrb->request);
	rrb->request.hdr.cmd = CLP_QUERY_PCI_FNGRP;
	rrb->response.hdr.len = sizeof(rrb->response);
	rrb->request.pfgid = pfgid;

	rc = clp_instr(rrb);
	if (!rc && rrb->response.hdr.rsp == CLP_RC_OK)
		clp_store_query_pci_fngrp(zdev, &rrb->response);
	else {
		pr_err("Query PCI FNGRP failed with response: %x  cc: %d\n",
			rrb->response.hdr.rsp, rc);
		rc = -EIO;
	}
	clp_free_block(rrb);
	return rc;
}

static int clp_store_query_pci_fn(struct zpci_dev *zdev,
				  struct clp_rsp_query_pci *response)
{
	int i;

	for (i = 0; i < PCI_BAR_COUNT; i++) {
		zdev->bars[i].val = le32_to_cpu(response->bar[i]);
		zdev->bars[i].size = response->bar_size[i];
	}
	zdev->start_dma = response->sdma;
	zdev->end_dma = response->edma;
	zdev->pchid = response->pchid;
	zdev->pfgid = response->pfgid;
	return 0;
}

static int clp_query_pci_fn(struct zpci_dev *zdev, u32 fh)
{
	struct clp_req_rsp_query_pci *rrb;
	int rc;

	rrb = clp_alloc_block(GFP_KERNEL);
	if (!rrb)
		return -ENOMEM;

	memset(rrb, 0, sizeof(*rrb));
	rrb->request.hdr.len = sizeof(rrb->request);
	rrb->request.hdr.cmd = CLP_QUERY_PCI_FN;
	rrb->response.hdr.len = sizeof(rrb->response);
	rrb->request.fh = fh;

	rc = clp_instr(rrb);
	if (!rc && rrb->response.hdr.rsp == CLP_RC_OK) {
		rc = clp_store_query_pci_fn(zdev, &rrb->response);
		if (rc)
			goto out;
		if (rrb->response.pfgid)
			rc = clp_query_pci_fngrp(zdev, rrb->response.pfgid);
	} else {
		pr_err("Query PCI failed with response: %x  cc: %d\n",
			 rrb->response.hdr.rsp, rc);
		rc = -EIO;
	}
out:
	clp_free_block(rrb);
	return rc;
}

int clp_add_pci_device(u32 fid, u32 fh, int configured)
{
	struct zpci_dev *zdev;
	int rc;

	zpci_dbg(3, "add fid:%x, fh:%x, c:%d\n", fid, fh, configured);
	zdev = zpci_alloc_device();
	if (IS_ERR(zdev))
		return PTR_ERR(zdev);

	zdev->fh = fh;
	zdev->fid = fid;

	/* Query function properties and update zdev */
	rc = clp_query_pci_fn(zdev, fh);
	if (rc)
		goto error;

	if (configured)
		zdev->state = ZPCI_FN_STATE_CONFIGURED;
	else
		zdev->state = ZPCI_FN_STATE_STANDBY;

	rc = zpci_create_device(zdev);
	if (rc)
		goto error;
	return 0;

error:
	zpci_free_device(zdev);
	return rc;
}

/*
 * Enable/Disable a given PCI function defined by its function handle.
 */
static int clp_set_pci_fn(u32 *fh, u8 nr_dma_as, u8 command)
{
	struct clp_req_rsp_set_pci *rrb;
	int rc, retries = 100;

	rrb = clp_alloc_block(GFP_KERNEL);
	if (!rrb)
		return -ENOMEM;

	do {
		memset(rrb, 0, sizeof(*rrb));
		rrb->request.hdr.len = sizeof(rrb->request);
		rrb->request.hdr.cmd = CLP_SET_PCI_FN;
		rrb->response.hdr.len = sizeof(rrb->response);
		rrb->request.fh = *fh;
		rrb->request.oc = command;
		rrb->request.ndas = nr_dma_as;

		rc = clp_instr(rrb);
		if (rrb->response.hdr.rsp == CLP_RC_SETPCIFN_BUSY) {
			retries--;
			if (retries < 0)
				break;
			msleep(20);
		}
	} while (rrb->response.hdr.rsp == CLP_RC_SETPCIFN_BUSY);

	if (!rc && rrb->response.hdr.rsp == CLP_RC_OK)
		*fh = rrb->response.fh;
	else {
		zpci_dbg(0, "SPF fh:%x, cc:%d, resp:%x\n", *fh, rc,
			 rrb->response.hdr.rsp);
		rc = -EIO;
	}
	clp_free_block(rrb);
	return rc;
}

int clp_enable_fh(struct zpci_dev *zdev, u8 nr_dma_as)
{
	u32 fh = zdev->fh;
	int rc;

	rc = clp_set_pci_fn(&fh, nr_dma_as, CLP_SET_ENABLE_PCI_FN);
	if (!rc)
		/* Success -> store enabled handle in zdev */
		zdev->fh = fh;

	zpci_dbg(3, "ena fid:%x, fh:%x, rc:%d\n", zdev->fid, zdev->fh, rc);
	return rc;
}

int clp_disable_fh(struct zpci_dev *zdev)
{
	u32 fh = zdev->fh;
	int rc;

	if (!zdev_enabled(zdev))
		return 0;

	rc = clp_set_pci_fn(&fh, 0, CLP_SET_DISABLE_PCI_FN);
	if (!rc)
		/* Success -> store disabled handle in zdev */
		zdev->fh = fh;

	zpci_dbg(3, "dis fid:%x, fh:%x, rc:%d\n", zdev->fid, zdev->fh, rc);
	return rc;
}

static int clp_list_pci(struct clp_req_rsp_list_pci *rrb,
			void (*cb)(struct clp_fh_list_entry *entry))
{
	u64 resume_token = 0;
	int entries, i, rc;

	do {
		memset(rrb, 0, sizeof(*rrb));
		rrb->request.hdr.len = sizeof(rrb->request);
		rrb->request.hdr.cmd = CLP_LIST_PCI;
		/* store as many entries as possible */
		rrb->response.hdr.len = CLP_BLK_SIZE - LIST_PCI_HDR_LEN;
		rrb->request.resume_token = resume_token;

		/* Get PCI function handle list */
		rc = clp_instr(rrb);
		if (rc || rrb->response.hdr.rsp != CLP_RC_OK) {
			pr_err("List PCI failed with response: 0x%x  cc: %d\n",
				rrb->response.hdr.rsp, rc);
			rc = -EIO;
			goto out;
		}

		WARN_ON_ONCE(rrb->response.entry_size !=
			sizeof(struct clp_fh_list_entry));

		entries = (rrb->response.hdr.len - LIST_PCI_HDR_LEN) /
			rrb->response.entry_size;
		pr_info("Detected number of PCI functions: %u\n", entries);

		/* Store the returned resume token as input for the next call */
		resume_token = rrb->response.resume_token;

		for (i = 0; i < entries; i++)
			cb(&rrb->response.fh_list[i]);
	} while (resume_token);

	pr_debug("Maximum number of supported PCI functions: %u\n",
		rrb->response.max_fn);
out:
	return rc;
}

static void __clp_add(struct clp_fh_list_entry *entry)
{
	if (!entry->vendor_id)
		return;

	clp_add_pci_device(entry->fid, entry->fh, entry->config_state);
}

static void __clp_rescan(struct clp_fh_list_entry *entry)
{
	struct zpci_dev *zdev;

	if (!entry->vendor_id)
		return;

	zdev = get_zdev_by_fid(entry->fid);
	if (!zdev) {
		clp_add_pci_device(entry->fid, entry->fh, entry->config_state);
		return;
	}

	if (!entry->config_state) {
		/*
		 * The handle is already disabled, that means no iota/irq freeing via
		 * the firmware interfaces anymore. Need to free resources manually
		 * (DMA memory, debug, sysfs)...
		 */
		zpci_stop_device(zdev);
	}
}

static void __clp_update(struct clp_fh_list_entry *entry)
{
	struct zpci_dev *zdev;

	if (!entry->vendor_id)
		return;

	zdev = get_zdev_by_fid(entry->fid);
	if (!zdev)
		return;

	zdev->fh = entry->fh;
}

int clp_scan_pci_devices(void)
{
	struct clp_req_rsp_list_pci *rrb;
	int rc;

	rrb = clp_alloc_block(GFP_KERNEL);
	if (!rrb)
		return -ENOMEM;

	rc = clp_list_pci(rrb, __clp_add);

	clp_free_block(rrb);
	return rc;
}

int clp_rescan_pci_devices(void)
{
	struct clp_req_rsp_list_pci *rrb;
	int rc;

	rrb = clp_alloc_block(GFP_KERNEL);
	if (!rrb)
		return -ENOMEM;

	rc = clp_list_pci(rrb, __clp_rescan);

	clp_free_block(rrb);
	return rc;
}

int clp_rescan_pci_devices_simple(void)
{
	struct clp_req_rsp_list_pci *rrb;
	int rc;

	rrb = clp_alloc_block(GFP_NOWAIT);
	if (!rrb)
		return -ENOMEM;

	rc = clp_list_pci(rrb, __clp_update);

	clp_free_block(rrb);
	return rc;
}

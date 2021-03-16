// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright IBM Corp. 2012
 *
 * Author(s):
 *   Jan Glauber <jang@linux.vnet.ibm.com>
 */

#define KMSG_COMPONENT "zpci"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/compat.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/uaccess.h>
#include <asm/pci_debug.h>
#include <asm/pci_clp.h>
#include <asm/clp.h>
#include <uapi/asm/clp.h>

bool zpci_unique_uid;

void update_uid_checking(bool new)
{
	if (zpci_unique_uid != new)
		zpci_dbg(1, "uid checking:%d\n", new);

	zpci_unique_uid = new;
}

static inline void zpci_err_clp(unsigned int rsp, int rc)
{
	struct {
		unsigned int rsp;
		int rc;
	} __packed data = {rsp, rc};

	zpci_err_hex(&data, sizeof(data));
}

/*
 * Call Logical Processor with c=1, lps=0 and command 1
 * to get the bit mask of installed logical processors
 */
static inline int clp_get_ilp(unsigned long *ilp)
{
	unsigned long mask;
	int cc = 3;

	asm volatile (
		"	.insn	rrf,0xb9a00000,%[mask],%[cmd],8,0\n"
		"0:	ipm	%[cc]\n"
		"	srl	%[cc],28\n"
		"1:\n"
		EX_TABLE(0b, 1b)
		: [cc] "+d" (cc), [mask] "=d" (mask) : [cmd] "a" (1)
		: "cc");
	*ilp = mask;
	return cc;
}

/*
 * Call Logical Processor with c=0, the give constant lps and an lpcb request.
 */
static __always_inline int clp_req(void *data, unsigned int lps)
{
	struct { u8 _[CLP_BLK_SIZE]; } *req = data;
	u64 ignored;
	int cc = 3;

	asm volatile (
		"	.insn	rrf,0xb9a00000,%[ign],%[req],0,%[lps]\n"
		"0:	ipm	%[cc]\n"
		"	srl	%[cc],28\n"
		"1:\n"
		EX_TABLE(0b, 1b)
		: [cc] "+d" (cc), [ign] "=d" (ignored), "+m" (*req)
		: [req] "a" (req), [lps] "i" (lps)
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
	zdev->max_msi = response->noi;
	zdev->fmb_update = response->mui;
	zdev->version = response->version;

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

	rc = clp_req(rrb, CLP_LPS_PCI);
	if (!rc && rrb->response.hdr.rsp == CLP_RC_OK)
		clp_store_query_pci_fngrp(zdev, &rrb->response);
	else {
		zpci_err("Q PCI FGRP:\n");
		zpci_err_clp(rrb->response.hdr.rsp, rc);
		rc = -EIO;
	}
	clp_free_block(rrb);
	return rc;
}

static int clp_store_query_pci_fn(struct zpci_dev *zdev,
				  struct clp_rsp_query_pci *response)
{
	int i;

	for (i = 0; i < PCI_STD_NUM_BARS; i++) {
		zdev->bars[i].val = le32_to_cpu(response->bar[i]);
		zdev->bars[i].size = response->bar_size[i];
	}
	zdev->start_dma = response->sdma;
	zdev->end_dma = response->edma;
	zdev->pchid = response->pchid;
	zdev->pfgid = response->pfgid;
	zdev->pft = response->pft;
	zdev->vfn = response->vfn;
	zdev->port = response->port;
	zdev->uid = response->uid;
	zdev->fmb_length = sizeof(u32) * response->fmb_len;
	zdev->rid_available = response->rid_avail;
	zdev->is_physfn = response->is_physfn;
	if (!s390_pci_no_rid && zdev->rid_available)
		zdev->devfn = response->rid & ZPCI_RID_MASK_DEVFN;

	memcpy(zdev->pfip, response->pfip, sizeof(zdev->pfip));
	if (response->util_str_avail) {
		memcpy(zdev->util_str, response->util_str,
		       sizeof(zdev->util_str));
		zdev->util_str_avail = 1;
	}
	zdev->mio_capable = response->mio_addr_avail;
	for (i = 0; i < PCI_STD_NUM_BARS; i++) {
		if (!(response->mio.valid & (1 << (PCI_STD_NUM_BARS - i - 1))))
			continue;

		zdev->bars[i].mio_wb = (void __iomem *) response->mio.addr[i].wb;
		zdev->bars[i].mio_wt = (void __iomem *) response->mio.addr[i].wt;
	}
	return 0;
}

int clp_query_pci_fn(struct zpci_dev *zdev)
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
	rrb->request.fh = zdev->fh;

	rc = clp_req(rrb, CLP_LPS_PCI);
	if (!rc && rrb->response.hdr.rsp == CLP_RC_OK) {
		rc = clp_store_query_pci_fn(zdev, &rrb->response);
		if (rc)
			goto out;
		rc = clp_query_pci_fngrp(zdev, rrb->response.pfgid);
	} else {
		zpci_err("Q PCI FN:\n");
		zpci_err_clp(rrb->response.hdr.rsp, rc);
		rc = -EIO;
	}
out:
	clp_free_block(rrb);
	return rc;
}

static int clp_refresh_fh(u32 fid);
/*
 * Enable/Disable a given PCI function and update its function handle if
 * necessary
 */
static int clp_set_pci_fn(struct zpci_dev *zdev, u8 nr_dma_as, u8 command)
{
	struct clp_req_rsp_set_pci *rrb;
	int rc, retries = 100;
	u32 fid = zdev->fid;

	rrb = clp_alloc_block(GFP_KERNEL);
	if (!rrb)
		return -ENOMEM;

	do {
		memset(rrb, 0, sizeof(*rrb));
		rrb->request.hdr.len = sizeof(rrb->request);
		rrb->request.hdr.cmd = CLP_SET_PCI_FN;
		rrb->response.hdr.len = sizeof(rrb->response);
		rrb->request.fh = zdev->fh;
		rrb->request.oc = command;
		rrb->request.ndas = nr_dma_as;

		rc = clp_req(rrb, CLP_LPS_PCI);
		if (rrb->response.hdr.rsp == CLP_RC_SETPCIFN_BUSY) {
			retries--;
			if (retries < 0)
				break;
			msleep(20);
		}
	} while (rrb->response.hdr.rsp == CLP_RC_SETPCIFN_BUSY);

	if (rc || rrb->response.hdr.rsp != CLP_RC_OK) {
		zpci_err("Set PCI FN:\n");
		zpci_err_clp(rrb->response.hdr.rsp, rc);
	}

	if (!rc && rrb->response.hdr.rsp == CLP_RC_OK) {
		zdev->fh = rrb->response.fh;
	} else if (!rc && rrb->response.hdr.rsp == CLP_RC_SETPCIFN_ALRDY &&
			rrb->response.fh == 0) {
		/* Function is already in desired state - update handle */
		rc = clp_refresh_fh(fid);
	}
	clp_free_block(rrb);
	return rc;
}

int clp_setup_writeback_mio(void)
{
	struct clp_req_rsp_slpc_pci *rrb;
	u8  wb_bit_pos;
	int rc;

	rrb = clp_alloc_block(GFP_KERNEL);
	if (!rrb)
		return -ENOMEM;

	memset(rrb, 0, sizeof(*rrb));
	rrb->request.hdr.len = sizeof(rrb->request);
	rrb->request.hdr.cmd = CLP_SLPC;
	rrb->response.hdr.len = sizeof(rrb->response);

	rc = clp_req(rrb, CLP_LPS_PCI);
	if (!rc && rrb->response.hdr.rsp == CLP_RC_OK) {
		if (rrb->response.vwb) {
			wb_bit_pos = rrb->response.mio_wb;
			set_bit_inv(wb_bit_pos, &mio_wb_bit_mask);
			zpci_dbg(3, "wb bit: %d\n", wb_bit_pos);
		} else {
			zpci_dbg(3, "wb bit: n.a.\n");
		}

	} else {
		zpci_err("SLPC PCI:\n");
		zpci_err_clp(rrb->response.hdr.rsp, rc);
		rc = -EIO;
	}
	clp_free_block(rrb);
	return rc;
}

int clp_enable_fh(struct zpci_dev *zdev, u8 nr_dma_as)
{
	int rc;

	rc = clp_set_pci_fn(zdev, nr_dma_as, CLP_SET_ENABLE_PCI_FN);
	zpci_dbg(3, "ena fid:%x, fh:%x, rc:%d\n", zdev->fid, zdev->fh, rc);
	if (rc)
		goto out;

	if (zpci_use_mio(zdev)) {
		rc = clp_set_pci_fn(zdev, nr_dma_as, CLP_SET_ENABLE_MIO);
		zpci_dbg(3, "ena mio fid:%x, fh:%x, rc:%d\n",
				zdev->fid, zdev->fh, rc);
		if (rc)
			clp_disable_fh(zdev);
	}
out:
	return rc;
}

int clp_disable_fh(struct zpci_dev *zdev)
{
	int rc;

	if (!zdev_enabled(zdev))
		return 0;

	rc = clp_set_pci_fn(zdev, 0, CLP_SET_DISABLE_PCI_FN);
	zpci_dbg(3, "dis fid:%x, fh:%x, rc:%d\n", zdev->fid, zdev->fh, rc);
	return rc;
}

static int clp_list_pci(struct clp_req_rsp_list_pci *rrb, void *data,
			void (*cb)(struct clp_fh_list_entry *, void *))
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
		rc = clp_req(rrb, CLP_LPS_PCI);
		if (rc || rrb->response.hdr.rsp != CLP_RC_OK) {
			zpci_err("List PCI FN:\n");
			zpci_err_clp(rrb->response.hdr.rsp, rc);
			rc = -EIO;
			goto out;
		}

		update_uid_checking(rrb->response.uid_checking);
		WARN_ON_ONCE(rrb->response.entry_size !=
			sizeof(struct clp_fh_list_entry));

		entries = (rrb->response.hdr.len - LIST_PCI_HDR_LEN) /
			rrb->response.entry_size;

		resume_token = rrb->response.resume_token;
		for (i = 0; i < entries; i++)
			cb(&rrb->response.fh_list[i], data);
	} while (resume_token);
out:
	return rc;
}

static void __clp_add(struct clp_fh_list_entry *entry, void *data)
{
	struct zpci_dev *zdev;

	if (!entry->vendor_id)
		return;

	zdev = get_zdev_by_fid(entry->fid);
	if (!zdev)
		zpci_create_device(entry->fid, entry->fh, entry->config_state);
}

int clp_scan_pci_devices(void)
{
	struct clp_req_rsp_list_pci *rrb;
	int rc;

	rrb = clp_alloc_block(GFP_KERNEL);
	if (!rrb)
		return -ENOMEM;

	rc = clp_list_pci(rrb, NULL, __clp_add);

	clp_free_block(rrb);
	return rc;
}

static void __clp_refresh_fh(struct clp_fh_list_entry *entry, void *data)
{
	struct zpci_dev *zdev;
	u32 fid = *((u32 *)data);

	if (!entry->vendor_id || fid != entry->fid)
		return;

	zdev = get_zdev_by_fid(fid);
	if (!zdev)
		return;

	zdev->fh = entry->fh;
}

/*
 * Refresh the function handle of the function matching @fid
 */
static int clp_refresh_fh(u32 fid)
{
	struct clp_req_rsp_list_pci *rrb;
	int rc;

	rrb = clp_alloc_block(GFP_NOWAIT);
	if (!rrb)
		return -ENOMEM;

	rc = clp_list_pci(rrb, &fid, __clp_refresh_fh);

	clp_free_block(rrb);
	return rc;
}

struct clp_state_data {
	u32 fid;
	enum zpci_state state;
};

static void __clp_get_state(struct clp_fh_list_entry *entry, void *data)
{
	struct clp_state_data *sd = data;

	if (entry->fid != sd->fid)
		return;

	sd->state = entry->config_state;
}

int clp_get_state(u32 fid, enum zpci_state *state)
{
	struct clp_req_rsp_list_pci *rrb;
	struct clp_state_data sd = {fid, ZPCI_FN_STATE_RESERVED};
	int rc;

	rrb = clp_alloc_block(GFP_ATOMIC);
	if (!rrb)
		return -ENOMEM;

	rc = clp_list_pci(rrb, &sd, __clp_get_state);
	if (!rc)
		*state = sd.state;

	clp_free_block(rrb);
	return rc;
}

static int clp_base_slpc(struct clp_req *req, struct clp_req_rsp_slpc *lpcb)
{
	unsigned long limit = PAGE_SIZE - sizeof(lpcb->request);

	if (lpcb->request.hdr.len != sizeof(lpcb->request) ||
	    lpcb->response.hdr.len > limit)
		return -EINVAL;
	return clp_req(lpcb, CLP_LPS_BASE) ? -EOPNOTSUPP : 0;
}

static int clp_base_command(struct clp_req *req, struct clp_req_hdr *lpcb)
{
	switch (lpcb->cmd) {
	case 0x0001: /* store logical-processor characteristics */
		return clp_base_slpc(req, (void *) lpcb);
	default:
		return -EINVAL;
	}
}

static int clp_pci_slpc(struct clp_req *req, struct clp_req_rsp_slpc_pci *lpcb)
{
	unsigned long limit = PAGE_SIZE - sizeof(lpcb->request);

	if (lpcb->request.hdr.len != sizeof(lpcb->request) ||
	    lpcb->response.hdr.len > limit)
		return -EINVAL;
	return clp_req(lpcb, CLP_LPS_PCI) ? -EOPNOTSUPP : 0;
}

static int clp_pci_list(struct clp_req *req, struct clp_req_rsp_list_pci *lpcb)
{
	unsigned long limit = PAGE_SIZE - sizeof(lpcb->request);

	if (lpcb->request.hdr.len != sizeof(lpcb->request) ||
	    lpcb->response.hdr.len > limit)
		return -EINVAL;
	if (lpcb->request.reserved2 != 0)
		return -EINVAL;
	return clp_req(lpcb, CLP_LPS_PCI) ? -EOPNOTSUPP : 0;
}

static int clp_pci_query(struct clp_req *req,
			 struct clp_req_rsp_query_pci *lpcb)
{
	unsigned long limit = PAGE_SIZE - sizeof(lpcb->request);

	if (lpcb->request.hdr.len != sizeof(lpcb->request) ||
	    lpcb->response.hdr.len > limit)
		return -EINVAL;
	if (lpcb->request.reserved2 != 0 || lpcb->request.reserved3 != 0)
		return -EINVAL;
	return clp_req(lpcb, CLP_LPS_PCI) ? -EOPNOTSUPP : 0;
}

static int clp_pci_query_grp(struct clp_req *req,
			     struct clp_req_rsp_query_pci_grp *lpcb)
{
	unsigned long limit = PAGE_SIZE - sizeof(lpcb->request);

	if (lpcb->request.hdr.len != sizeof(lpcb->request) ||
	    lpcb->response.hdr.len > limit)
		return -EINVAL;
	if (lpcb->request.reserved2 != 0 || lpcb->request.reserved3 != 0 ||
	    lpcb->request.reserved4 != 0)
		return -EINVAL;
	return clp_req(lpcb, CLP_LPS_PCI) ? -EOPNOTSUPP : 0;
}

static int clp_pci_command(struct clp_req *req, struct clp_req_hdr *lpcb)
{
	switch (lpcb->cmd) {
	case 0x0001: /* store logical-processor characteristics */
		return clp_pci_slpc(req, (void *) lpcb);
	case 0x0002: /* list PCI functions */
		return clp_pci_list(req, (void *) lpcb);
	case 0x0003: /* query PCI function */
		return clp_pci_query(req, (void *) lpcb);
	case 0x0004: /* query PCI function group */
		return clp_pci_query_grp(req, (void *) lpcb);
	default:
		return -EINVAL;
	}
}

static int clp_normal_command(struct clp_req *req)
{
	struct clp_req_hdr *lpcb;
	void __user *uptr;
	int rc;

	rc = -EINVAL;
	if (req->lps != 0 && req->lps != 2)
		goto out;

	rc = -ENOMEM;
	lpcb = clp_alloc_block(GFP_KERNEL);
	if (!lpcb)
		goto out;

	rc = -EFAULT;
	uptr = (void __force __user *)(unsigned long) req->data_p;
	if (copy_from_user(lpcb, uptr, PAGE_SIZE) != 0)
		goto out_free;

	rc = -EINVAL;
	if (lpcb->fmt != 0 || lpcb->reserved1 != 0 || lpcb->reserved2 != 0)
		goto out_free;

	switch (req->lps) {
	case 0:
		rc = clp_base_command(req, lpcb);
		break;
	case 2:
		rc = clp_pci_command(req, lpcb);
		break;
	}
	if (rc)
		goto out_free;

	rc = -EFAULT;
	if (copy_to_user(uptr, lpcb, PAGE_SIZE) != 0)
		goto out_free;

	rc = 0;

out_free:
	clp_free_block(lpcb);
out:
	return rc;
}

static int clp_immediate_command(struct clp_req *req)
{
	void __user *uptr;
	unsigned long ilp;
	int exists;

	if (req->cmd > 1 || clp_get_ilp(&ilp) != 0)
		return -EINVAL;

	uptr = (void __force __user *)(unsigned long) req->data_p;
	if (req->cmd == 0) {
		/* Command code 0: test for a specific processor */
		exists = test_bit_inv(req->lps, &ilp);
		return put_user(exists, (int __user *) uptr);
	}
	/* Command code 1: return bit mask of installed processors */
	return put_user(ilp, (unsigned long __user *) uptr);
}

static long clp_misc_ioctl(struct file *filp, unsigned int cmd,
			   unsigned long arg)
{
	struct clp_req req;
	void __user *argp;

	if (cmd != CLP_SYNC)
		return -EINVAL;

	argp = is_compat_task() ? compat_ptr(arg) : (void __user *) arg;
	if (copy_from_user(&req, argp, sizeof(req)))
		return -EFAULT;
	if (req.r != 0)
		return -EINVAL;
	return req.c ? clp_immediate_command(&req) : clp_normal_command(&req);
}

static int clp_misc_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static const struct file_operations clp_misc_fops = {
	.owner = THIS_MODULE,
	.open = nonseekable_open,
	.release = clp_misc_release,
	.unlocked_ioctl = clp_misc_ioctl,
	.compat_ioctl = clp_misc_ioctl,
	.llseek = no_llseek,
};

static struct miscdevice clp_misc_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "clp",
	.fops = &clp_misc_fops,
};

static int __init clp_misc_init(void)
{
	return misc_register(&clp_misc_device);
}

device_initcall(clp_misc_init);

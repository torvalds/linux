// SPDX-License-Identifier: GPL-2.0-only
/*
 * AMD Secure Encrypted Virtualization (SEV) interface
 *
 * Copyright (C) 2016,2019 Advanced Micro Devices, Inc.
 *
 * Author: Brijesh Singh <brijesh.singh@amd.com>
 */

#include <linux/bitfield.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/spinlock_types.h>
#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/hw_random.h>
#include <linux/ccp.h>
#include <linux/firmware.h>
#include <linux/panic_notifier.h>
#include <linux/gfp.h>
#include <linux/cpufeature.h>
#include <linux/fs.h>
#include <linux/fs_struct.h>
#include <linux/psp.h>
#include <linux/amd-iommu.h>

#include <asm/smp.h>
#include <asm/cacheflush.h>
#include <asm/e820/types.h>
#include <asm/sev.h>

#include "psp-dev.h"
#include "sev-dev.h"

#define DEVICE_NAME		"sev"
#define SEV_FW_FILE		"amd/sev.fw"
#define SEV_FW_NAME_SIZE	64

/* Minimum firmware version required for the SEV-SNP support */
#define SNP_MIN_API_MAJOR	1
#define SNP_MIN_API_MINOR	51

/*
 * Maximum number of firmware-writable buffers that might be specified
 * in the parameters of a legacy SEV command buffer.
 */
#define CMD_BUF_FW_WRITABLE_MAX 2

/* Leave room in the descriptor array for an end-of-list indicator. */
#define CMD_BUF_DESC_MAX (CMD_BUF_FW_WRITABLE_MAX + 1)

static DEFINE_MUTEX(sev_cmd_mutex);
static struct sev_misc_dev *misc_dev;

static int psp_cmd_timeout = 100;
module_param(psp_cmd_timeout, int, 0644);
MODULE_PARM_DESC(psp_cmd_timeout, " default timeout value, in seconds, for PSP commands");

static int psp_probe_timeout = 5;
module_param(psp_probe_timeout, int, 0644);
MODULE_PARM_DESC(psp_probe_timeout, " default timeout value, in seconds, during PSP device probe");

static char *init_ex_path;
module_param(init_ex_path, charp, 0444);
MODULE_PARM_DESC(init_ex_path, " Path for INIT_EX data; if set try INIT_EX");

static bool psp_init_on_probe = true;
module_param(psp_init_on_probe, bool, 0444);
MODULE_PARM_DESC(psp_init_on_probe, "  if true, the PSP will be initialized on module init. Else the PSP will be initialized on the first command requiring it");

MODULE_FIRMWARE("amd/amd_sev_fam17h_model0xh.sbin"); /* 1st gen EPYC */
MODULE_FIRMWARE("amd/amd_sev_fam17h_model3xh.sbin"); /* 2nd gen EPYC */
MODULE_FIRMWARE("amd/amd_sev_fam19h_model0xh.sbin"); /* 3rd gen EPYC */
MODULE_FIRMWARE("amd/amd_sev_fam19h_model1xh.sbin"); /* 4th gen EPYC */

static bool psp_dead;
static int psp_timeout;

/* Trusted Memory Region (TMR):
 *   The TMR is a 1MB area that must be 1MB aligned.  Use the page allocator
 *   to allocate the memory, which will return aligned memory for the specified
 *   allocation order.
 *
 * When SEV-SNP is enabled the TMR needs to be 2MB aligned and 2MB sized.
 */
#define SEV_TMR_SIZE		(1024 * 1024)
#define SNP_TMR_SIZE		(2 * 1024 * 1024)

static void *sev_es_tmr;
static size_t sev_es_tmr_size = SEV_TMR_SIZE;

/* INIT_EX NV Storage:
 *   The NV Storage is a 32Kb area and must be 4Kb page aligned.  Use the page
 *   allocator to allocate the memory, which will return aligned memory for the
 *   specified allocation order.
 */
#define NV_LENGTH (32 * 1024)
static void *sev_init_ex_buffer;

/*
 * SEV_DATA_RANGE_LIST:
 *   Array containing range of pages that firmware transitions to HV-fixed
 *   page state.
 */
static struct sev_data_range_list *snp_range_list;

static inline bool sev_version_greater_or_equal(u8 maj, u8 min)
{
	struct sev_device *sev = psp_master->sev_data;

	if (sev->api_major > maj)
		return true;

	if (sev->api_major == maj && sev->api_minor >= min)
		return true;

	return false;
}

static void sev_irq_handler(int irq, void *data, unsigned int status)
{
	struct sev_device *sev = data;
	int reg;

	/* Check if it is command completion: */
	if (!(status & SEV_CMD_COMPLETE))
		return;

	/* Check if it is SEV command completion: */
	reg = ioread32(sev->io_regs + sev->vdata->cmdresp_reg);
	if (FIELD_GET(PSP_CMDRESP_RESP, reg)) {
		sev->int_rcvd = 1;
		wake_up(&sev->int_queue);
	}
}

static int sev_wait_cmd_ioc(struct sev_device *sev,
			    unsigned int *reg, unsigned int timeout)
{
	int ret;

	/*
	 * If invoked during panic handling, local interrupts are disabled,
	 * so the PSP command completion interrupt can't be used. Poll for
	 * PSP command completion instead.
	 */
	if (irqs_disabled()) {
		unsigned long timeout_usecs = (timeout * USEC_PER_SEC) / 10;

		/* Poll for SEV command completion: */
		while (timeout_usecs--) {
			*reg = ioread32(sev->io_regs + sev->vdata->cmdresp_reg);
			if (*reg & PSP_CMDRESP_RESP)
				return 0;

			udelay(10);
		}
		return -ETIMEDOUT;
	}

	ret = wait_event_timeout(sev->int_queue,
			sev->int_rcvd, timeout * HZ);
	if (!ret)
		return -ETIMEDOUT;

	*reg = ioread32(sev->io_regs + sev->vdata->cmdresp_reg);

	return 0;
}

static int sev_cmd_buffer_len(int cmd)
{
	switch (cmd) {
	case SEV_CMD_INIT:			return sizeof(struct sev_data_init);
	case SEV_CMD_INIT_EX:                   return sizeof(struct sev_data_init_ex);
	case SEV_CMD_SNP_SHUTDOWN_EX:		return sizeof(struct sev_data_snp_shutdown_ex);
	case SEV_CMD_SNP_INIT_EX:		return sizeof(struct sev_data_snp_init_ex);
	case SEV_CMD_PLATFORM_STATUS:		return sizeof(struct sev_user_data_status);
	case SEV_CMD_PEK_CSR:			return sizeof(struct sev_data_pek_csr);
	case SEV_CMD_PEK_CERT_IMPORT:		return sizeof(struct sev_data_pek_cert_import);
	case SEV_CMD_PDH_CERT_EXPORT:		return sizeof(struct sev_data_pdh_cert_export);
	case SEV_CMD_LAUNCH_START:		return sizeof(struct sev_data_launch_start);
	case SEV_CMD_LAUNCH_UPDATE_DATA:	return sizeof(struct sev_data_launch_update_data);
	case SEV_CMD_LAUNCH_UPDATE_VMSA:	return sizeof(struct sev_data_launch_update_vmsa);
	case SEV_CMD_LAUNCH_FINISH:		return sizeof(struct sev_data_launch_finish);
	case SEV_CMD_LAUNCH_MEASURE:		return sizeof(struct sev_data_launch_measure);
	case SEV_CMD_ACTIVATE:			return sizeof(struct sev_data_activate);
	case SEV_CMD_DEACTIVATE:		return sizeof(struct sev_data_deactivate);
	case SEV_CMD_DECOMMISSION:		return sizeof(struct sev_data_decommission);
	case SEV_CMD_GUEST_STATUS:		return sizeof(struct sev_data_guest_status);
	case SEV_CMD_DBG_DECRYPT:		return sizeof(struct sev_data_dbg);
	case SEV_CMD_DBG_ENCRYPT:		return sizeof(struct sev_data_dbg);
	case SEV_CMD_SEND_START:		return sizeof(struct sev_data_send_start);
	case SEV_CMD_SEND_UPDATE_DATA:		return sizeof(struct sev_data_send_update_data);
	case SEV_CMD_SEND_UPDATE_VMSA:		return sizeof(struct sev_data_send_update_vmsa);
	case SEV_CMD_SEND_FINISH:		return sizeof(struct sev_data_send_finish);
	case SEV_CMD_RECEIVE_START:		return sizeof(struct sev_data_receive_start);
	case SEV_CMD_RECEIVE_FINISH:		return sizeof(struct sev_data_receive_finish);
	case SEV_CMD_RECEIVE_UPDATE_DATA:	return sizeof(struct sev_data_receive_update_data);
	case SEV_CMD_RECEIVE_UPDATE_VMSA:	return sizeof(struct sev_data_receive_update_vmsa);
	case SEV_CMD_LAUNCH_UPDATE_SECRET:	return sizeof(struct sev_data_launch_secret);
	case SEV_CMD_DOWNLOAD_FIRMWARE:		return sizeof(struct sev_data_download_firmware);
	case SEV_CMD_GET_ID:			return sizeof(struct sev_data_get_id);
	case SEV_CMD_ATTESTATION_REPORT:	return sizeof(struct sev_data_attestation_report);
	case SEV_CMD_SEND_CANCEL:		return sizeof(struct sev_data_send_cancel);
	case SEV_CMD_SNP_GCTX_CREATE:		return sizeof(struct sev_data_snp_addr);
	case SEV_CMD_SNP_LAUNCH_START:		return sizeof(struct sev_data_snp_launch_start);
	case SEV_CMD_SNP_LAUNCH_UPDATE:		return sizeof(struct sev_data_snp_launch_update);
	case SEV_CMD_SNP_ACTIVATE:		return sizeof(struct sev_data_snp_activate);
	case SEV_CMD_SNP_DECOMMISSION:		return sizeof(struct sev_data_snp_addr);
	case SEV_CMD_SNP_PAGE_RECLAIM:		return sizeof(struct sev_data_snp_page_reclaim);
	case SEV_CMD_SNP_GUEST_STATUS:		return sizeof(struct sev_data_snp_guest_status);
	case SEV_CMD_SNP_LAUNCH_FINISH:		return sizeof(struct sev_data_snp_launch_finish);
	case SEV_CMD_SNP_DBG_DECRYPT:		return sizeof(struct sev_data_snp_dbg);
	case SEV_CMD_SNP_DBG_ENCRYPT:		return sizeof(struct sev_data_snp_dbg);
	case SEV_CMD_SNP_PAGE_UNSMASH:		return sizeof(struct sev_data_snp_page_unsmash);
	case SEV_CMD_SNP_PLATFORM_STATUS:	return sizeof(struct sev_data_snp_addr);
	case SEV_CMD_SNP_GUEST_REQUEST:		return sizeof(struct sev_data_snp_guest_request);
	case SEV_CMD_SNP_CONFIG:		return sizeof(struct sev_user_data_snp_config);
	case SEV_CMD_SNP_COMMIT:		return sizeof(struct sev_data_snp_commit);
	default:				return 0;
	}

	return 0;
}

static struct file *open_file_as_root(const char *filename, int flags, umode_t mode)
{
	struct file *fp;
	struct path root;
	struct cred *cred;
	const struct cred *old_cred;

	task_lock(&init_task);
	get_fs_root(init_task.fs, &root);
	task_unlock(&init_task);

	cred = prepare_creds();
	if (!cred)
		return ERR_PTR(-ENOMEM);
	cred->fsuid = GLOBAL_ROOT_UID;
	old_cred = override_creds(cred);

	fp = file_open_root(&root, filename, flags, mode);
	path_put(&root);

	put_cred(revert_creds(old_cred));

	return fp;
}

static int sev_read_init_ex_file(void)
{
	struct sev_device *sev = psp_master->sev_data;
	struct file *fp;
	ssize_t nread;

	lockdep_assert_held(&sev_cmd_mutex);

	if (!sev_init_ex_buffer)
		return -EOPNOTSUPP;

	fp = open_file_as_root(init_ex_path, O_RDONLY, 0);
	if (IS_ERR(fp)) {
		int ret = PTR_ERR(fp);

		if (ret == -ENOENT) {
			dev_info(sev->dev,
				"SEV: %s does not exist and will be created later.\n",
				init_ex_path);
			ret = 0;
		} else {
			dev_err(sev->dev,
				"SEV: could not open %s for read, error %d\n",
				init_ex_path, ret);
		}
		return ret;
	}

	nread = kernel_read(fp, sev_init_ex_buffer, NV_LENGTH, NULL);
	if (nread != NV_LENGTH) {
		dev_info(sev->dev,
			"SEV: could not read %u bytes to non volatile memory area, ret %ld\n",
			NV_LENGTH, nread);
	}

	dev_dbg(sev->dev, "SEV: read %ld bytes from NV file\n", nread);
	filp_close(fp, NULL);

	return 0;
}

static int sev_write_init_ex_file(void)
{
	struct sev_device *sev = psp_master->sev_data;
	struct file *fp;
	loff_t offset = 0;
	ssize_t nwrite;

	lockdep_assert_held(&sev_cmd_mutex);

	if (!sev_init_ex_buffer)
		return 0;

	fp = open_file_as_root(init_ex_path, O_CREAT | O_WRONLY, 0600);
	if (IS_ERR(fp)) {
		int ret = PTR_ERR(fp);

		dev_err(sev->dev,
			"SEV: could not open file for write, error %d\n",
			ret);
		return ret;
	}

	nwrite = kernel_write(fp, sev_init_ex_buffer, NV_LENGTH, &offset);
	vfs_fsync(fp, 0);
	filp_close(fp, NULL);

	if (nwrite != NV_LENGTH) {
		dev_err(sev->dev,
			"SEV: failed to write %u bytes to non volatile memory area, ret %ld\n",
			NV_LENGTH, nwrite);
		return -EIO;
	}

	dev_dbg(sev->dev, "SEV: write successful to NV file\n");

	return 0;
}

static int sev_write_init_ex_file_if_required(int cmd_id)
{
	lockdep_assert_held(&sev_cmd_mutex);

	if (!sev_init_ex_buffer)
		return 0;

	/*
	 * Only a few platform commands modify the SPI/NV area, but none of the
	 * non-platform commands do. Only INIT(_EX), PLATFORM_RESET, PEK_GEN,
	 * PEK_CERT_IMPORT, and PDH_GEN do.
	 */
	switch (cmd_id) {
	case SEV_CMD_FACTORY_RESET:
	case SEV_CMD_INIT_EX:
	case SEV_CMD_PDH_GEN:
	case SEV_CMD_PEK_CERT_IMPORT:
	case SEV_CMD_PEK_GEN:
		break;
	default:
		return 0;
	}

	return sev_write_init_ex_file();
}

/*
 * snp_reclaim_pages() needs __sev_do_cmd_locked(), and __sev_do_cmd_locked()
 * needs snp_reclaim_pages(), so a forward declaration is needed.
 */
static int __sev_do_cmd_locked(int cmd, void *data, int *psp_ret);

static int snp_reclaim_pages(unsigned long paddr, unsigned int npages, bool locked)
{
	int ret, err, i;

	paddr = __sme_clr(ALIGN_DOWN(paddr, PAGE_SIZE));

	for (i = 0; i < npages; i++, paddr += PAGE_SIZE) {
		struct sev_data_snp_page_reclaim data = {0};

		data.paddr = paddr;

		if (locked)
			ret = __sev_do_cmd_locked(SEV_CMD_SNP_PAGE_RECLAIM, &data, &err);
		else
			ret = sev_do_cmd(SEV_CMD_SNP_PAGE_RECLAIM, &data, &err);

		if (ret)
			goto cleanup;

		ret = rmp_make_shared(__phys_to_pfn(paddr), PG_LEVEL_4K);
		if (ret)
			goto cleanup;
	}

	return 0;

cleanup:
	/*
	 * If there was a failure reclaiming the page then it is no longer safe
	 * to release it back to the system; leak it instead.
	 */
	snp_leak_pages(__phys_to_pfn(paddr), npages - i);
	return ret;
}

static int rmp_mark_pages_firmware(unsigned long paddr, unsigned int npages, bool locked)
{
	unsigned long pfn = __sme_clr(paddr) >> PAGE_SHIFT;
	int rc, i;

	for (i = 0; i < npages; i++, pfn++) {
		rc = rmp_make_private(pfn, 0, PG_LEVEL_4K, 0, true);
		if (rc)
			goto cleanup;
	}

	return 0;

cleanup:
	/*
	 * Try unrolling the firmware state changes by
	 * reclaiming the pages which were already changed to the
	 * firmware state.
	 */
	snp_reclaim_pages(paddr, i, locked);

	return rc;
}

static struct page *__snp_alloc_firmware_pages(gfp_t gfp_mask, int order)
{
	unsigned long npages = 1ul << order, paddr;
	struct sev_device *sev;
	struct page *page;

	if (!psp_master || !psp_master->sev_data)
		return NULL;

	page = alloc_pages(gfp_mask, order);
	if (!page)
		return NULL;

	/* If SEV-SNP is initialized then add the page in RMP table. */
	sev = psp_master->sev_data;
	if (!sev->snp_initialized)
		return page;

	paddr = __pa((unsigned long)page_address(page));
	if (rmp_mark_pages_firmware(paddr, npages, false))
		return NULL;

	return page;
}

void *snp_alloc_firmware_page(gfp_t gfp_mask)
{
	struct page *page;

	page = __snp_alloc_firmware_pages(gfp_mask, 0);

	return page ? page_address(page) : NULL;
}
EXPORT_SYMBOL_GPL(snp_alloc_firmware_page);

static void __snp_free_firmware_pages(struct page *page, int order, bool locked)
{
	struct sev_device *sev = psp_master->sev_data;
	unsigned long paddr, npages = 1ul << order;

	if (!page)
		return;

	paddr = __pa((unsigned long)page_address(page));
	if (sev->snp_initialized &&
	    snp_reclaim_pages(paddr, npages, locked))
		return;

	__free_pages(page, order);
}

void snp_free_firmware_page(void *addr)
{
	if (!addr)
		return;

	__snp_free_firmware_pages(virt_to_page(addr), 0, false);
}
EXPORT_SYMBOL_GPL(snp_free_firmware_page);

static void *sev_fw_alloc(unsigned long len)
{
	struct page *page;

	page = __snp_alloc_firmware_pages(GFP_KERNEL, get_order(len));
	if (!page)
		return NULL;

	return page_address(page);
}

/**
 * struct cmd_buf_desc - descriptors for managing legacy SEV command address
 * parameters corresponding to buffers that may be written to by firmware.
 *
 * @paddr_ptr:  pointer to the address parameter in the command buffer which may
 *              need to be saved/restored depending on whether a bounce buffer
 *              is used. In the case of a bounce buffer, the command buffer
 *              needs to be updated with the address of the new bounce buffer
 *              snp_map_cmd_buf_desc() has allocated specifically for it. Must
 *              be NULL if this descriptor is only an end-of-list indicator.
 *
 * @paddr_orig: storage for the original address parameter, which can be used to
 *              restore the original value in @paddr_ptr in cases where it is
 *              replaced with the address of a bounce buffer.
 *
 * @len: length of buffer located at the address originally stored at @paddr_ptr
 *
 * @guest_owned: true if the address corresponds to guest-owned pages, in which
 *               case bounce buffers are not needed.
 */
struct cmd_buf_desc {
	u64 *paddr_ptr;
	u64 paddr_orig;
	u32 len;
	bool guest_owned;
};

/*
 * If a legacy SEV command parameter is a memory address, those pages in
 * turn need to be transitioned to/from firmware-owned before/after
 * executing the firmware command.
 *
 * Additionally, in cases where those pages are not guest-owned, a bounce
 * buffer is needed in place of the original memory address parameter.
 *
 * A set of descriptors are used to keep track of this handling, and
 * initialized here based on the specific commands being executed.
 */
static void snp_populate_cmd_buf_desc_list(int cmd, void *cmd_buf,
					   struct cmd_buf_desc *desc_list)
{
	switch (cmd) {
	case SEV_CMD_PDH_CERT_EXPORT: {
		struct sev_data_pdh_cert_export *data = cmd_buf;

		desc_list[0].paddr_ptr = &data->pdh_cert_address;
		desc_list[0].len = data->pdh_cert_len;
		desc_list[1].paddr_ptr = &data->cert_chain_address;
		desc_list[1].len = data->cert_chain_len;
		break;
	}
	case SEV_CMD_GET_ID: {
		struct sev_data_get_id *data = cmd_buf;

		desc_list[0].paddr_ptr = &data->address;
		desc_list[0].len = data->len;
		break;
	}
	case SEV_CMD_PEK_CSR: {
		struct sev_data_pek_csr *data = cmd_buf;

		desc_list[0].paddr_ptr = &data->address;
		desc_list[0].len = data->len;
		break;
	}
	case SEV_CMD_LAUNCH_UPDATE_DATA: {
		struct sev_data_launch_update_data *data = cmd_buf;

		desc_list[0].paddr_ptr = &data->address;
		desc_list[0].len = data->len;
		desc_list[0].guest_owned = true;
		break;
	}
	case SEV_CMD_LAUNCH_UPDATE_VMSA: {
		struct sev_data_launch_update_vmsa *data = cmd_buf;

		desc_list[0].paddr_ptr = &data->address;
		desc_list[0].len = data->len;
		desc_list[0].guest_owned = true;
		break;
	}
	case SEV_CMD_LAUNCH_MEASURE: {
		struct sev_data_launch_measure *data = cmd_buf;

		desc_list[0].paddr_ptr = &data->address;
		desc_list[0].len = data->len;
		break;
	}
	case SEV_CMD_LAUNCH_UPDATE_SECRET: {
		struct sev_data_launch_secret *data = cmd_buf;

		desc_list[0].paddr_ptr = &data->guest_address;
		desc_list[0].len = data->guest_len;
		desc_list[0].guest_owned = true;
		break;
	}
	case SEV_CMD_DBG_DECRYPT: {
		struct sev_data_dbg *data = cmd_buf;

		desc_list[0].paddr_ptr = &data->dst_addr;
		desc_list[0].len = data->len;
		desc_list[0].guest_owned = true;
		break;
	}
	case SEV_CMD_DBG_ENCRYPT: {
		struct sev_data_dbg *data = cmd_buf;

		desc_list[0].paddr_ptr = &data->dst_addr;
		desc_list[0].len = data->len;
		desc_list[0].guest_owned = true;
		break;
	}
	case SEV_CMD_ATTESTATION_REPORT: {
		struct sev_data_attestation_report *data = cmd_buf;

		desc_list[0].paddr_ptr = &data->address;
		desc_list[0].len = data->len;
		break;
	}
	case SEV_CMD_SEND_START: {
		struct sev_data_send_start *data = cmd_buf;

		desc_list[0].paddr_ptr = &data->session_address;
		desc_list[0].len = data->session_len;
		break;
	}
	case SEV_CMD_SEND_UPDATE_DATA: {
		struct sev_data_send_update_data *data = cmd_buf;

		desc_list[0].paddr_ptr = &data->hdr_address;
		desc_list[0].len = data->hdr_len;
		desc_list[1].paddr_ptr = &data->trans_address;
		desc_list[1].len = data->trans_len;
		break;
	}
	case SEV_CMD_SEND_UPDATE_VMSA: {
		struct sev_data_send_update_vmsa *data = cmd_buf;

		desc_list[0].paddr_ptr = &data->hdr_address;
		desc_list[0].len = data->hdr_len;
		desc_list[1].paddr_ptr = &data->trans_address;
		desc_list[1].len = data->trans_len;
		break;
	}
	case SEV_CMD_RECEIVE_UPDATE_DATA: {
		struct sev_data_receive_update_data *data = cmd_buf;

		desc_list[0].paddr_ptr = &data->guest_address;
		desc_list[0].len = data->guest_len;
		desc_list[0].guest_owned = true;
		break;
	}
	case SEV_CMD_RECEIVE_UPDATE_VMSA: {
		struct sev_data_receive_update_vmsa *data = cmd_buf;

		desc_list[0].paddr_ptr = &data->guest_address;
		desc_list[0].len = data->guest_len;
		desc_list[0].guest_owned = true;
		break;
	}
	default:
		break;
	}
}

static int snp_map_cmd_buf_desc(struct cmd_buf_desc *desc)
{
	unsigned int npages;

	if (!desc->len)
		return 0;

	/* Allocate a bounce buffer if this isn't a guest owned page. */
	if (!desc->guest_owned) {
		struct page *page;

		page = alloc_pages(GFP_KERNEL_ACCOUNT, get_order(desc->len));
		if (!page) {
			pr_warn("Failed to allocate bounce buffer for SEV legacy command.\n");
			return -ENOMEM;
		}

		desc->paddr_orig = *desc->paddr_ptr;
		*desc->paddr_ptr = __psp_pa(page_to_virt(page));
	}

	npages = PAGE_ALIGN(desc->len) >> PAGE_SHIFT;

	/* Transition the buffer to firmware-owned. */
	if (rmp_mark_pages_firmware(*desc->paddr_ptr, npages, true)) {
		pr_warn("Error moving pages to firmware-owned state for SEV legacy command.\n");
		return -EFAULT;
	}

	return 0;
}

static int snp_unmap_cmd_buf_desc(struct cmd_buf_desc *desc)
{
	unsigned int npages;

	if (!desc->len)
		return 0;

	npages = PAGE_ALIGN(desc->len) >> PAGE_SHIFT;

	/* Transition the buffers back to hypervisor-owned. */
	if (snp_reclaim_pages(*desc->paddr_ptr, npages, true)) {
		pr_warn("Failed to reclaim firmware-owned pages while issuing SEV legacy command.\n");
		return -EFAULT;
	}

	/* Copy data from bounce buffer and then free it. */
	if (!desc->guest_owned) {
		void *bounce_buf = __va(__sme_clr(*desc->paddr_ptr));
		void *dst_buf = __va(__sme_clr(desc->paddr_orig));

		memcpy(dst_buf, bounce_buf, desc->len);
		__free_pages(virt_to_page(bounce_buf), get_order(desc->len));

		/* Restore the original address in the command buffer. */
		*desc->paddr_ptr = desc->paddr_orig;
	}

	return 0;
}

static int snp_map_cmd_buf_desc_list(int cmd, void *cmd_buf, struct cmd_buf_desc *desc_list)
{
	int i;

	snp_populate_cmd_buf_desc_list(cmd, cmd_buf, desc_list);

	for (i = 0; i < CMD_BUF_DESC_MAX; i++) {
		struct cmd_buf_desc *desc = &desc_list[i];

		if (!desc->paddr_ptr)
			break;

		if (snp_map_cmd_buf_desc(desc))
			goto err_unmap;
	}

	return 0;

err_unmap:
	for (i--; i >= 0; i--)
		snp_unmap_cmd_buf_desc(&desc_list[i]);

	return -EFAULT;
}

static int snp_unmap_cmd_buf_desc_list(struct cmd_buf_desc *desc_list)
{
	int i, ret = 0;

	for (i = 0; i < CMD_BUF_DESC_MAX; i++) {
		struct cmd_buf_desc *desc = &desc_list[i];

		if (!desc->paddr_ptr)
			break;

		if (snp_unmap_cmd_buf_desc(&desc_list[i]))
			ret = -EFAULT;
	}

	return ret;
}

static bool sev_cmd_buf_writable(int cmd)
{
	switch (cmd) {
	case SEV_CMD_PLATFORM_STATUS:
	case SEV_CMD_GUEST_STATUS:
	case SEV_CMD_LAUNCH_START:
	case SEV_CMD_RECEIVE_START:
	case SEV_CMD_LAUNCH_MEASURE:
	case SEV_CMD_SEND_START:
	case SEV_CMD_SEND_UPDATE_DATA:
	case SEV_CMD_SEND_UPDATE_VMSA:
	case SEV_CMD_PEK_CSR:
	case SEV_CMD_PDH_CERT_EXPORT:
	case SEV_CMD_GET_ID:
	case SEV_CMD_ATTESTATION_REPORT:
		return true;
	default:
		return false;
	}
}

/* After SNP is INIT'ed, the behavior of legacy SEV commands is changed. */
static bool snp_legacy_handling_needed(int cmd)
{
	struct sev_device *sev = psp_master->sev_data;

	return cmd < SEV_CMD_SNP_INIT && sev->snp_initialized;
}

static int snp_prep_cmd_buf(int cmd, void *cmd_buf, struct cmd_buf_desc *desc_list)
{
	if (!snp_legacy_handling_needed(cmd))
		return 0;

	if (snp_map_cmd_buf_desc_list(cmd, cmd_buf, desc_list))
		return -EFAULT;

	/*
	 * Before command execution, the command buffer needs to be put into
	 * the firmware-owned state.
	 */
	if (sev_cmd_buf_writable(cmd)) {
		if (rmp_mark_pages_firmware(__pa(cmd_buf), 1, true))
			return -EFAULT;
	}

	return 0;
}

static int snp_reclaim_cmd_buf(int cmd, void *cmd_buf)
{
	if (!snp_legacy_handling_needed(cmd))
		return 0;

	/*
	 * After command completion, the command buffer needs to be put back
	 * into the hypervisor-owned state.
	 */
	if (sev_cmd_buf_writable(cmd))
		if (snp_reclaim_pages(__pa(cmd_buf), 1, true))
			return -EFAULT;

	return 0;
}

static int __sev_do_cmd_locked(int cmd, void *data, int *psp_ret)
{
	struct cmd_buf_desc desc_list[CMD_BUF_DESC_MAX] = {0};
	struct psp_device *psp = psp_master;
	struct sev_device *sev;
	unsigned int cmdbuff_hi, cmdbuff_lo;
	unsigned int phys_lsb, phys_msb;
	unsigned int reg, ret = 0;
	void *cmd_buf;
	int buf_len;

	if (!psp || !psp->sev_data)
		return -ENODEV;

	if (psp_dead)
		return -EBUSY;

	sev = psp->sev_data;

	buf_len = sev_cmd_buffer_len(cmd);
	if (WARN_ON_ONCE(!data != !buf_len))
		return -EINVAL;

	/*
	 * Copy the incoming data to driver's scratch buffer as __pa() will not
	 * work for some memory, e.g. vmalloc'd addresses, and @data may not be
	 * physically contiguous.
	 */
	if (data) {
		/*
		 * Commands are generally issued one at a time and require the
		 * sev_cmd_mutex, but there could be recursive firmware requests
		 * due to SEV_CMD_SNP_PAGE_RECLAIM needing to be issued while
		 * preparing buffers for another command. This is the only known
		 * case of nesting in the current code, so exactly one
		 * additional command buffer is available for that purpose.
		 */
		if (!sev->cmd_buf_active) {
			cmd_buf = sev->cmd_buf;
			sev->cmd_buf_active = true;
		} else if (!sev->cmd_buf_backup_active) {
			cmd_buf = sev->cmd_buf_backup;
			sev->cmd_buf_backup_active = true;
		} else {
			dev_err(sev->dev,
				"SEV: too many firmware commands in progress, no command buffers available.\n");
			return -EBUSY;
		}

		memcpy(cmd_buf, data, buf_len);

		/*
		 * The behavior of the SEV-legacy commands is altered when the
		 * SNP firmware is in the INIT state.
		 */
		ret = snp_prep_cmd_buf(cmd, cmd_buf, desc_list);
		if (ret) {
			dev_err(sev->dev,
				"SEV: failed to prepare buffer for legacy command 0x%x. Error: %d\n",
				cmd, ret);
			return ret;
		}
	} else {
		cmd_buf = sev->cmd_buf;
	}

	/* Get the physical address of the command buffer */
	phys_lsb = data ? lower_32_bits(__psp_pa(cmd_buf)) : 0;
	phys_msb = data ? upper_32_bits(__psp_pa(cmd_buf)) : 0;

	dev_dbg(sev->dev, "sev command id %#x buffer 0x%08x%08x timeout %us\n",
		cmd, phys_msb, phys_lsb, psp_timeout);

	print_hex_dump_debug("(in):  ", DUMP_PREFIX_OFFSET, 16, 2, data,
			     buf_len, false);

	iowrite32(phys_lsb, sev->io_regs + sev->vdata->cmdbuff_addr_lo_reg);
	iowrite32(phys_msb, sev->io_regs + sev->vdata->cmdbuff_addr_hi_reg);

	sev->int_rcvd = 0;

	reg = FIELD_PREP(SEV_CMDRESP_CMD, cmd);

	/*
	 * If invoked during panic handling, local interrupts are disabled so
	 * the PSP command completion interrupt can't be used.
	 * sev_wait_cmd_ioc() already checks for interrupts disabled and
	 * polls for PSP command completion.  Ensure we do not request an
	 * interrupt from the PSP if irqs disabled.
	 */
	if (!irqs_disabled())
		reg |= SEV_CMDRESP_IOC;

	iowrite32(reg, sev->io_regs + sev->vdata->cmdresp_reg);

	/* wait for command completion */
	ret = sev_wait_cmd_ioc(sev, &reg, psp_timeout);
	if (ret) {
		if (psp_ret)
			*psp_ret = 0;

		dev_err(sev->dev, "sev command %#x timed out, disabling PSP\n", cmd);
		psp_dead = true;

		return ret;
	}

	psp_timeout = psp_cmd_timeout;

	if (psp_ret)
		*psp_ret = FIELD_GET(PSP_CMDRESP_STS, reg);

	if (FIELD_GET(PSP_CMDRESP_STS, reg)) {
		dev_dbg(sev->dev, "sev command %#x failed (%#010lx)\n",
			cmd, FIELD_GET(PSP_CMDRESP_STS, reg));

		/*
		 * PSP firmware may report additional error information in the
		 * command buffer registers on error. Print contents of command
		 * buffer registers if they changed.
		 */
		cmdbuff_hi = ioread32(sev->io_regs + sev->vdata->cmdbuff_addr_hi_reg);
		cmdbuff_lo = ioread32(sev->io_regs + sev->vdata->cmdbuff_addr_lo_reg);
		if (cmdbuff_hi != phys_msb || cmdbuff_lo != phys_lsb) {
			dev_dbg(sev->dev, "Additional error information reported in cmdbuff:");
			dev_dbg(sev->dev, "  cmdbuff hi: %#010x\n", cmdbuff_hi);
			dev_dbg(sev->dev, "  cmdbuff lo: %#010x\n", cmdbuff_lo);
		}
		ret = -EIO;
	} else {
		ret = sev_write_init_ex_file_if_required(cmd);
	}

	/*
	 * Copy potential output from the PSP back to data.  Do this even on
	 * failure in case the caller wants to glean something from the error.
	 */
	if (data) {
		int ret_reclaim;
		/*
		 * Restore the page state after the command completes.
		 */
		ret_reclaim = snp_reclaim_cmd_buf(cmd, cmd_buf);
		if (ret_reclaim) {
			dev_err(sev->dev,
				"SEV: failed to reclaim buffer for legacy command %#x. Error: %d\n",
				cmd, ret_reclaim);
			return ret_reclaim;
		}

		memcpy(data, cmd_buf, buf_len);

		if (sev->cmd_buf_backup_active)
			sev->cmd_buf_backup_active = false;
		else
			sev->cmd_buf_active = false;

		if (snp_unmap_cmd_buf_desc_list(desc_list))
			return -EFAULT;
	}

	print_hex_dump_debug("(out): ", DUMP_PREFIX_OFFSET, 16, 2, data,
			     buf_len, false);

	return ret;
}

int sev_do_cmd(int cmd, void *data, int *psp_ret)
{
	int rc;

	mutex_lock(&sev_cmd_mutex);
	rc = __sev_do_cmd_locked(cmd, data, psp_ret);
	mutex_unlock(&sev_cmd_mutex);

	return rc;
}
EXPORT_SYMBOL_GPL(sev_do_cmd);

static int __sev_init_locked(int *error)
{
	struct sev_data_init data;

	memset(&data, 0, sizeof(data));
	if (sev_es_tmr) {
		/*
		 * Do not include the encryption mask on the physical
		 * address of the TMR (firmware should clear it anyway).
		 */
		data.tmr_address = __pa(sev_es_tmr);

		data.flags |= SEV_INIT_FLAGS_SEV_ES;
		data.tmr_len = sev_es_tmr_size;
	}

	return __sev_do_cmd_locked(SEV_CMD_INIT, &data, error);
}

static int __sev_init_ex_locked(int *error)
{
	struct sev_data_init_ex data;

	memset(&data, 0, sizeof(data));
	data.length = sizeof(data);
	data.nv_address = __psp_pa(sev_init_ex_buffer);
	data.nv_len = NV_LENGTH;

	if (sev_es_tmr) {
		/*
		 * Do not include the encryption mask on the physical
		 * address of the TMR (firmware should clear it anyway).
		 */
		data.tmr_address = __pa(sev_es_tmr);

		data.flags |= SEV_INIT_FLAGS_SEV_ES;
		data.tmr_len = sev_es_tmr_size;
	}

	return __sev_do_cmd_locked(SEV_CMD_INIT_EX, &data, error);
}

static inline int __sev_do_init_locked(int *psp_ret)
{
	if (sev_init_ex_buffer)
		return __sev_init_ex_locked(psp_ret);
	else
		return __sev_init_locked(psp_ret);
}

static void snp_set_hsave_pa(void *arg)
{
	wrmsrl(MSR_VM_HSAVE_PA, 0);
}

static int snp_filter_reserved_mem_regions(struct resource *rs, void *arg)
{
	struct sev_data_range_list *range_list = arg;
	struct sev_data_range *range = &range_list->ranges[range_list->num_elements];
	size_t size;

	/*
	 * Ensure the list of HV_FIXED pages that will be passed to firmware
	 * do not exceed the page-sized argument buffer.
	 */
	if ((range_list->num_elements * sizeof(struct sev_data_range) +
	     sizeof(struct sev_data_range_list)) > PAGE_SIZE)
		return -E2BIG;

	switch (rs->desc) {
	case E820_TYPE_RESERVED:
	case E820_TYPE_PMEM:
	case E820_TYPE_ACPI:
		range->base = rs->start & PAGE_MASK;
		size = PAGE_ALIGN((rs->end + 1) - rs->start);
		range->page_count = size >> PAGE_SHIFT;
		range_list->num_elements++;
		break;
	default:
		break;
	}

	return 0;
}

static int __sev_snp_init_locked(int *error)
{
	struct psp_device *psp = psp_master;
	struct sev_data_snp_init_ex data;
	struct sev_device *sev;
	void *arg = &data;
	int cmd, rc = 0;

	if (!cc_platform_has(CC_ATTR_HOST_SEV_SNP))
		return -ENODEV;

	sev = psp->sev_data;

	if (sev->snp_initialized)
		return 0;

	if (!sev_version_greater_or_equal(SNP_MIN_API_MAJOR, SNP_MIN_API_MINOR)) {
		dev_dbg(sev->dev, "SEV-SNP support requires firmware version >= %d:%d\n",
			SNP_MIN_API_MAJOR, SNP_MIN_API_MINOR);
		return 0;
	}

	/* SNP_INIT requires MSR_VM_HSAVE_PA to be cleared on all CPUs. */
	on_each_cpu(snp_set_hsave_pa, NULL, 1);

	/*
	 * Starting in SNP firmware v1.52, the SNP_INIT_EX command takes a list
	 * of system physical address ranges to convert into HV-fixed page
	 * states during the RMP initialization.  For instance, the memory that
	 * UEFI reserves should be included in the that list. This allows system
	 * components that occasionally write to memory (e.g. logging to UEFI
	 * reserved regions) to not fail due to RMP initialization and SNP
	 * enablement.
	 *
	 */
	if (sev_version_greater_or_equal(SNP_MIN_API_MAJOR, 52)) {
		/*
		 * Firmware checks that the pages containing the ranges enumerated
		 * in the RANGES structure are either in the default page state or in the
		 * firmware page state.
		 */
		snp_range_list = kzalloc(PAGE_SIZE, GFP_KERNEL);
		if (!snp_range_list) {
			dev_err(sev->dev,
				"SEV: SNP_INIT_EX range list memory allocation failed\n");
			return -ENOMEM;
		}

		/*
		 * Retrieve all reserved memory regions from the e820 memory map
		 * to be setup as HV-fixed pages.
		 */
		rc = walk_iomem_res_desc(IORES_DESC_NONE, IORESOURCE_MEM, 0, ~0,
					 snp_range_list, snp_filter_reserved_mem_regions);
		if (rc) {
			dev_err(sev->dev,
				"SEV: SNP_INIT_EX walk_iomem_res_desc failed rc = %d\n", rc);
			return rc;
		}

		memset(&data, 0, sizeof(data));
		data.init_rmp = 1;
		data.list_paddr_en = 1;
		data.list_paddr = __psp_pa(snp_range_list);
		cmd = SEV_CMD_SNP_INIT_EX;
	} else {
		cmd = SEV_CMD_SNP_INIT;
		arg = NULL;
	}

	/*
	 * The following sequence must be issued before launching the first SNP
	 * guest to ensure all dirty cache lines are flushed, including from
	 * updates to the RMP table itself via the RMPUPDATE instruction:
	 *
	 * - WBINVD on all running CPUs
	 * - SEV_CMD_SNP_INIT[_EX] firmware command
	 * - WBINVD on all running CPUs
	 * - SEV_CMD_SNP_DF_FLUSH firmware command
	 */
	wbinvd_on_all_cpus();

	rc = __sev_do_cmd_locked(cmd, arg, error);
	if (rc)
		return rc;

	/* Prepare for first SNP guest launch after INIT. */
	wbinvd_on_all_cpus();
	rc = __sev_do_cmd_locked(SEV_CMD_SNP_DF_FLUSH, NULL, error);
	if (rc)
		return rc;

	sev->snp_initialized = true;
	dev_dbg(sev->dev, "SEV-SNP firmware initialized\n");

	sev_es_tmr_size = SNP_TMR_SIZE;

	return rc;
}

static void __sev_platform_init_handle_tmr(struct sev_device *sev)
{
	if (sev_es_tmr)
		return;

	/* Obtain the TMR memory area for SEV-ES use */
	sev_es_tmr = sev_fw_alloc(sev_es_tmr_size);
	if (sev_es_tmr) {
		/* Must flush the cache before giving it to the firmware */
		if (!sev->snp_initialized)
			clflush_cache_range(sev_es_tmr, sev_es_tmr_size);
	} else {
			dev_warn(sev->dev, "SEV: TMR allocation failed, SEV-ES support unavailable\n");
	}
}

/*
 * If an init_ex_path is provided allocate a buffer for the file and
 * read in the contents. Additionally, if SNP is initialized, convert
 * the buffer pages to firmware pages.
 */
static int __sev_platform_init_handle_init_ex_path(struct sev_device *sev)
{
	struct page *page;
	int rc;

	if (!init_ex_path)
		return 0;

	if (sev_init_ex_buffer)
		return 0;

	page = alloc_pages(GFP_KERNEL, get_order(NV_LENGTH));
	if (!page) {
		dev_err(sev->dev, "SEV: INIT_EX NV memory allocation failed\n");
		return -ENOMEM;
	}

	sev_init_ex_buffer = page_address(page);

	rc = sev_read_init_ex_file();
	if (rc)
		return rc;

	/* If SEV-SNP is initialized, transition to firmware page. */
	if (sev->snp_initialized) {
		unsigned long npages;

		npages = 1UL << get_order(NV_LENGTH);
		if (rmp_mark_pages_firmware(__pa(sev_init_ex_buffer), npages, false)) {
			dev_err(sev->dev, "SEV: INIT_EX NV memory page state change failed.\n");
			return -ENOMEM;
		}
	}

	return 0;
}

static int __sev_platform_init_locked(int *error)
{
	int rc, psp_ret = SEV_RET_NO_FW_CALL;
	struct sev_device *sev;

	if (!psp_master || !psp_master->sev_data)
		return -ENODEV;

	sev = psp_master->sev_data;

	if (sev->state == SEV_STATE_INIT)
		return 0;

	__sev_platform_init_handle_tmr(sev);

	rc = __sev_platform_init_handle_init_ex_path(sev);
	if (rc)
		return rc;

	rc = __sev_do_init_locked(&psp_ret);
	if (rc && psp_ret == SEV_RET_SECURE_DATA_INVALID) {
		/*
		 * Initialization command returned an integrity check failure
		 * status code, meaning that firmware load and validation of SEV
		 * related persistent data has failed. Retrying the
		 * initialization function should succeed by replacing the state
		 * with a reset state.
		 */
		dev_err(sev->dev,
"SEV: retrying INIT command because of SECURE_DATA_INVALID error. Retrying once to reset PSP SEV state.");
		rc = __sev_do_init_locked(&psp_ret);
	}

	if (error)
		*error = psp_ret;

	if (rc)
		return rc;

	sev->state = SEV_STATE_INIT;

	/* Prepare for first SEV guest launch after INIT */
	wbinvd_on_all_cpus();
	rc = __sev_do_cmd_locked(SEV_CMD_DF_FLUSH, NULL, error);
	if (rc)
		return rc;

	dev_dbg(sev->dev, "SEV firmware initialized\n");

	dev_info(sev->dev, "SEV API:%d.%d build:%d\n", sev->api_major,
		 sev->api_minor, sev->build);

	return 0;
}

static int _sev_platform_init_locked(struct sev_platform_init_args *args)
{
	struct sev_device *sev;
	int rc;

	if (!psp_master || !psp_master->sev_data)
		return -ENODEV;

	sev = psp_master->sev_data;

	if (sev->state == SEV_STATE_INIT)
		return 0;

	/*
	 * Legacy guests cannot be running while SNP_INIT(_EX) is executing,
	 * so perform SEV-SNP initialization at probe time.
	 */
	rc = __sev_snp_init_locked(&args->error);
	if (rc && rc != -ENODEV) {
		/*
		 * Don't abort the probe if SNP INIT failed,
		 * continue to initialize the legacy SEV firmware.
		 */
		dev_err(sev->dev, "SEV-SNP: failed to INIT rc %d, error %#x\n",
			rc, args->error);
	}

	/* Defer legacy SEV/SEV-ES support if allowed by caller/module. */
	if (args->probe && !psp_init_on_probe)
		return 0;

	return __sev_platform_init_locked(&args->error);
}

int sev_platform_init(struct sev_platform_init_args *args)
{
	int rc;

	mutex_lock(&sev_cmd_mutex);
	rc = _sev_platform_init_locked(args);
	mutex_unlock(&sev_cmd_mutex);

	return rc;
}
EXPORT_SYMBOL_GPL(sev_platform_init);

static int __sev_platform_shutdown_locked(int *error)
{
	struct psp_device *psp = psp_master;
	struct sev_device *sev;
	int ret;

	if (!psp || !psp->sev_data)
		return 0;

	sev = psp->sev_data;

	if (sev->state == SEV_STATE_UNINIT)
		return 0;

	ret = __sev_do_cmd_locked(SEV_CMD_SHUTDOWN, NULL, error);
	if (ret)
		return ret;

	sev->state = SEV_STATE_UNINIT;
	dev_dbg(sev->dev, "SEV firmware shutdown\n");

	return ret;
}

static int sev_get_platform_state(int *state, int *error)
{
	struct sev_user_data_status data;
	int rc;

	rc = __sev_do_cmd_locked(SEV_CMD_PLATFORM_STATUS, &data, error);
	if (rc)
		return rc;

	*state = data.state;
	return rc;
}

static int sev_ioctl_do_reset(struct sev_issue_cmd *argp, bool writable)
{
	int state, rc;

	if (!writable)
		return -EPERM;

	/*
	 * The SEV spec requires that FACTORY_RESET must be issued in
	 * UNINIT state. Before we go further lets check if any guest is
	 * active.
	 *
	 * If FW is in WORKING state then deny the request otherwise issue
	 * SHUTDOWN command do INIT -> UNINIT before issuing the FACTORY_RESET.
	 *
	 */
	rc = sev_get_platform_state(&state, &argp->error);
	if (rc)
		return rc;

	if (state == SEV_STATE_WORKING)
		return -EBUSY;

	if (state == SEV_STATE_INIT) {
		rc = __sev_platform_shutdown_locked(&argp->error);
		if (rc)
			return rc;
	}

	return __sev_do_cmd_locked(SEV_CMD_FACTORY_RESET, NULL, &argp->error);
}

static int sev_ioctl_do_platform_status(struct sev_issue_cmd *argp)
{
	struct sev_user_data_status data;
	int ret;

	memset(&data, 0, sizeof(data));

	ret = __sev_do_cmd_locked(SEV_CMD_PLATFORM_STATUS, &data, &argp->error);
	if (ret)
		return ret;

	if (copy_to_user((void __user *)argp->data, &data, sizeof(data)))
		ret = -EFAULT;

	return ret;
}

static int sev_ioctl_do_pek_pdh_gen(int cmd, struct sev_issue_cmd *argp, bool writable)
{
	struct sev_device *sev = psp_master->sev_data;
	int rc;

	if (!writable)
		return -EPERM;

	if (sev->state == SEV_STATE_UNINIT) {
		rc = __sev_platform_init_locked(&argp->error);
		if (rc)
			return rc;
	}

	return __sev_do_cmd_locked(cmd, NULL, &argp->error);
}

static int sev_ioctl_do_pek_csr(struct sev_issue_cmd *argp, bool writable)
{
	struct sev_device *sev = psp_master->sev_data;
	struct sev_user_data_pek_csr input;
	struct sev_data_pek_csr data;
	void __user *input_address;
	void *blob = NULL;
	int ret;

	if (!writable)
		return -EPERM;

	if (copy_from_user(&input, (void __user *)argp->data, sizeof(input)))
		return -EFAULT;

	memset(&data, 0, sizeof(data));

	/* userspace wants to query CSR length */
	if (!input.address || !input.length)
		goto cmd;

	/* allocate a physically contiguous buffer to store the CSR blob */
	input_address = (void __user *)input.address;
	if (input.length > SEV_FW_BLOB_MAX_SIZE)
		return -EFAULT;

	blob = kzalloc(input.length, GFP_KERNEL);
	if (!blob)
		return -ENOMEM;

	data.address = __psp_pa(blob);
	data.len = input.length;

cmd:
	if (sev->state == SEV_STATE_UNINIT) {
		ret = __sev_platform_init_locked(&argp->error);
		if (ret)
			goto e_free_blob;
	}

	ret = __sev_do_cmd_locked(SEV_CMD_PEK_CSR, &data, &argp->error);

	 /* If we query the CSR length, FW responded with expected data. */
	input.length = data.len;

	if (copy_to_user((void __user *)argp->data, &input, sizeof(input))) {
		ret = -EFAULT;
		goto e_free_blob;
	}

	if (blob) {
		if (copy_to_user(input_address, blob, input.length))
			ret = -EFAULT;
	}

e_free_blob:
	kfree(blob);
	return ret;
}

void *psp_copy_user_blob(u64 uaddr, u32 len)
{
	if (!uaddr || !len)
		return ERR_PTR(-EINVAL);

	/* verify that blob length does not exceed our limit */
	if (len > SEV_FW_BLOB_MAX_SIZE)
		return ERR_PTR(-EINVAL);

	return memdup_user((void __user *)uaddr, len);
}
EXPORT_SYMBOL_GPL(psp_copy_user_blob);

static int sev_get_api_version(void)
{
	struct sev_device *sev = psp_master->sev_data;
	struct sev_user_data_status status;
	int error = 0, ret;

	ret = sev_platform_status(&status, &error);
	if (ret) {
		dev_err(sev->dev,
			"SEV: failed to get status. Error: %#x\n", error);
		return 1;
	}

	sev->api_major = status.api_major;
	sev->api_minor = status.api_minor;
	sev->build = status.build;
	sev->state = status.state;

	return 0;
}

static int sev_get_firmware(struct device *dev,
			    const struct firmware **firmware)
{
	char fw_name_specific[SEV_FW_NAME_SIZE];
	char fw_name_subset[SEV_FW_NAME_SIZE];

	snprintf(fw_name_specific, sizeof(fw_name_specific),
		 "amd/amd_sev_fam%.2xh_model%.2xh.sbin",
		 boot_cpu_data.x86, boot_cpu_data.x86_model);

	snprintf(fw_name_subset, sizeof(fw_name_subset),
		 "amd/amd_sev_fam%.2xh_model%.1xxh.sbin",
		 boot_cpu_data.x86, (boot_cpu_data.x86_model & 0xf0) >> 4);

	/* Check for SEV FW for a particular model.
	 * Ex. amd_sev_fam17h_model00h.sbin for Family 17h Model 00h
	 *
	 * or
	 *
	 * Check for SEV FW common to a subset of models.
	 * Ex. amd_sev_fam17h_model0xh.sbin for
	 *     Family 17h Model 00h -- Family 17h Model 0Fh
	 *
	 * or
	 *
	 * Fall-back to using generic name: sev.fw
	 */
	if ((firmware_request_nowarn(firmware, fw_name_specific, dev) >= 0) ||
	    (firmware_request_nowarn(firmware, fw_name_subset, dev) >= 0) ||
	    (firmware_request_nowarn(firmware, SEV_FW_FILE, dev) >= 0))
		return 0;

	return -ENOENT;
}

/* Don't fail if SEV FW couldn't be updated. Continue with existing SEV FW */
static int sev_update_firmware(struct device *dev)
{
	struct sev_data_download_firmware *data;
	const struct firmware *firmware;
	int ret, error, order;
	struct page *p;
	u64 data_size;

	if (!sev_version_greater_or_equal(0, 15)) {
		dev_dbg(dev, "DOWNLOAD_FIRMWARE not supported\n");
		return -1;
	}

	if (sev_get_firmware(dev, &firmware) == -ENOENT) {
		dev_dbg(dev, "No SEV firmware file present\n");
		return -1;
	}

	/*
	 * SEV FW expects the physical address given to it to be 32
	 * byte aligned. Memory allocated has structure placed at the
	 * beginning followed by the firmware being passed to the SEV
	 * FW. Allocate enough memory for data structure + alignment
	 * padding + SEV FW.
	 */
	data_size = ALIGN(sizeof(struct sev_data_download_firmware), 32);

	order = get_order(firmware->size + data_size);
	p = alloc_pages(GFP_KERNEL, order);
	if (!p) {
		ret = -1;
		goto fw_err;
	}

	/*
	 * Copy firmware data to a kernel allocated contiguous
	 * memory region.
	 */
	data = page_address(p);
	memcpy(page_address(p) + data_size, firmware->data, firmware->size);

	data->address = __psp_pa(page_address(p) + data_size);
	data->len = firmware->size;

	ret = sev_do_cmd(SEV_CMD_DOWNLOAD_FIRMWARE, data, &error);

	/*
	 * A quirk for fixing the committed TCB version, when upgrading from
	 * earlier firmware version than 1.50.
	 */
	if (!ret && !sev_version_greater_or_equal(1, 50))
		ret = sev_do_cmd(SEV_CMD_DOWNLOAD_FIRMWARE, data, &error);

	if (ret)
		dev_dbg(dev, "Failed to update SEV firmware: %#x\n", error);

	__free_pages(p, order);

fw_err:
	release_firmware(firmware);

	return ret;
}

static int __sev_snp_shutdown_locked(int *error, bool panic)
{
	struct psp_device *psp = psp_master;
	struct sev_device *sev;
	struct sev_data_snp_shutdown_ex data;
	int ret;

	if (!psp || !psp->sev_data)
		return 0;

	sev = psp->sev_data;

	if (!sev->snp_initialized)
		return 0;

	memset(&data, 0, sizeof(data));
	data.len = sizeof(data);
	data.iommu_snp_shutdown = 1;

	/*
	 * If invoked during panic handling, local interrupts are disabled
	 * and all CPUs are stopped, so wbinvd_on_all_cpus() can't be called.
	 * In that case, a wbinvd() is done on remote CPUs via the NMI
	 * callback, so only a local wbinvd() is needed here.
	 */
	if (!panic)
		wbinvd_on_all_cpus();
	else
		wbinvd();

	ret = __sev_do_cmd_locked(SEV_CMD_SNP_SHUTDOWN_EX, &data, error);
	/* SHUTDOWN may require DF_FLUSH */
	if (*error == SEV_RET_DFFLUSH_REQUIRED) {
		ret = __sev_do_cmd_locked(SEV_CMD_SNP_DF_FLUSH, NULL, NULL);
		if (ret) {
			dev_err(sev->dev, "SEV-SNP DF_FLUSH failed\n");
			return ret;
		}
		/* reissue the shutdown command */
		ret = __sev_do_cmd_locked(SEV_CMD_SNP_SHUTDOWN_EX, &data,
					  error);
	}
	if (ret) {
		dev_err(sev->dev, "SEV-SNP firmware shutdown failed\n");
		return ret;
	}

	/*
	 * SNP_SHUTDOWN_EX with IOMMU_SNP_SHUTDOWN set to 1 disables SNP
	 * enforcement by the IOMMU and also transitions all pages
	 * associated with the IOMMU to the Reclaim state.
	 * Firmware was transitioning the IOMMU pages to Hypervisor state
	 * before version 1.53. But, accounting for the number of assigned
	 * 4kB pages in a 2M page was done incorrectly by not transitioning
	 * to the Reclaim state. This resulted in RMP #PF when later accessing
	 * the 2M page containing those pages during kexec boot. Hence, the
	 * firmware now transitions these pages to Reclaim state and hypervisor
	 * needs to transition these pages to shared state. SNP Firmware
	 * version 1.53 and above are needed for kexec boot.
	 */
	ret = amd_iommu_snp_disable();
	if (ret) {
		dev_err(sev->dev, "SNP IOMMU shutdown failed\n");
		return ret;
	}

	sev->snp_initialized = false;
	dev_dbg(sev->dev, "SEV-SNP firmware shutdown\n");

	return ret;
}

static int sev_ioctl_do_pek_import(struct sev_issue_cmd *argp, bool writable)
{
	struct sev_device *sev = psp_master->sev_data;
	struct sev_user_data_pek_cert_import input;
	struct sev_data_pek_cert_import data;
	void *pek_blob, *oca_blob;
	int ret;

	if (!writable)
		return -EPERM;

	if (copy_from_user(&input, (void __user *)argp->data, sizeof(input)))
		return -EFAULT;

	/* copy PEK certificate blobs from userspace */
	pek_blob = psp_copy_user_blob(input.pek_cert_address, input.pek_cert_len);
	if (IS_ERR(pek_blob))
		return PTR_ERR(pek_blob);

	data.reserved = 0;
	data.pek_cert_address = __psp_pa(pek_blob);
	data.pek_cert_len = input.pek_cert_len;

	/* copy PEK certificate blobs from userspace */
	oca_blob = psp_copy_user_blob(input.oca_cert_address, input.oca_cert_len);
	if (IS_ERR(oca_blob)) {
		ret = PTR_ERR(oca_blob);
		goto e_free_pek;
	}

	data.oca_cert_address = __psp_pa(oca_blob);
	data.oca_cert_len = input.oca_cert_len;

	/* If platform is not in INIT state then transition it to INIT */
	if (sev->state != SEV_STATE_INIT) {
		ret = __sev_platform_init_locked(&argp->error);
		if (ret)
			goto e_free_oca;
	}

	ret = __sev_do_cmd_locked(SEV_CMD_PEK_CERT_IMPORT, &data, &argp->error);

e_free_oca:
	kfree(oca_blob);
e_free_pek:
	kfree(pek_blob);
	return ret;
}

static int sev_ioctl_do_get_id2(struct sev_issue_cmd *argp)
{
	struct sev_user_data_get_id2 input;
	struct sev_data_get_id data;
	void __user *input_address;
	void *id_blob = NULL;
	int ret;

	/* SEV GET_ID is available from SEV API v0.16 and up */
	if (!sev_version_greater_or_equal(0, 16))
		return -ENOTSUPP;

	if (copy_from_user(&input, (void __user *)argp->data, sizeof(input)))
		return -EFAULT;

	input_address = (void __user *)input.address;

	if (input.address && input.length) {
		/*
		 * The length of the ID shouldn't be assumed by software since
		 * it may change in the future.  The allocation size is limited
		 * to 1 << (PAGE_SHIFT + MAX_PAGE_ORDER) by the page allocator.
		 * If the allocation fails, simply return ENOMEM rather than
		 * warning in the kernel log.
		 */
		id_blob = kzalloc(input.length, GFP_KERNEL | __GFP_NOWARN);
		if (!id_blob)
			return -ENOMEM;

		data.address = __psp_pa(id_blob);
		data.len = input.length;
	} else {
		data.address = 0;
		data.len = 0;
	}

	ret = __sev_do_cmd_locked(SEV_CMD_GET_ID, &data, &argp->error);

	/*
	 * Firmware will return the length of the ID value (either the minimum
	 * required length or the actual length written), return it to the user.
	 */
	input.length = data.len;

	if (copy_to_user((void __user *)argp->data, &input, sizeof(input))) {
		ret = -EFAULT;
		goto e_free;
	}

	if (id_blob) {
		if (copy_to_user(input_address, id_blob, data.len)) {
			ret = -EFAULT;
			goto e_free;
		}
	}

e_free:
	kfree(id_blob);

	return ret;
}

static int sev_ioctl_do_get_id(struct sev_issue_cmd *argp)
{
	struct sev_data_get_id *data;
	u64 data_size, user_size;
	void *id_blob, *mem;
	int ret;

	/* SEV GET_ID available from SEV API v0.16 and up */
	if (!sev_version_greater_or_equal(0, 16))
		return -ENOTSUPP;

	/* SEV FW expects the buffer it fills with the ID to be
	 * 8-byte aligned. Memory allocated should be enough to
	 * hold data structure + alignment padding + memory
	 * where SEV FW writes the ID.
	 */
	data_size = ALIGN(sizeof(struct sev_data_get_id), 8);
	user_size = sizeof(struct sev_user_data_get_id);

	mem = kzalloc(data_size + user_size, GFP_KERNEL);
	if (!mem)
		return -ENOMEM;

	data = mem;
	id_blob = mem + data_size;

	data->address = __psp_pa(id_blob);
	data->len = user_size;

	ret = __sev_do_cmd_locked(SEV_CMD_GET_ID, data, &argp->error);
	if (!ret) {
		if (copy_to_user((void __user *)argp->data, id_blob, data->len))
			ret = -EFAULT;
	}

	kfree(mem);

	return ret;
}

static int sev_ioctl_do_pdh_export(struct sev_issue_cmd *argp, bool writable)
{
	struct sev_device *sev = psp_master->sev_data;
	struct sev_user_data_pdh_cert_export input;
	void *pdh_blob = NULL, *cert_blob = NULL;
	struct sev_data_pdh_cert_export data;
	void __user *input_cert_chain_address;
	void __user *input_pdh_cert_address;
	int ret;

	/* If platform is not in INIT state then transition it to INIT. */
	if (sev->state != SEV_STATE_INIT) {
		if (!writable)
			return -EPERM;

		ret = __sev_platform_init_locked(&argp->error);
		if (ret)
			return ret;
	}

	if (copy_from_user(&input, (void __user *)argp->data, sizeof(input)))
		return -EFAULT;

	memset(&data, 0, sizeof(data));

	/* Userspace wants to query the certificate length. */
	if (!input.pdh_cert_address ||
	    !input.pdh_cert_len ||
	    !input.cert_chain_address)
		goto cmd;

	input_pdh_cert_address = (void __user *)input.pdh_cert_address;
	input_cert_chain_address = (void __user *)input.cert_chain_address;

	/* Allocate a physically contiguous buffer to store the PDH blob. */
	if (input.pdh_cert_len > SEV_FW_BLOB_MAX_SIZE)
		return -EFAULT;

	/* Allocate a physically contiguous buffer to store the cert chain blob. */
	if (input.cert_chain_len > SEV_FW_BLOB_MAX_SIZE)
		return -EFAULT;

	pdh_blob = kzalloc(input.pdh_cert_len, GFP_KERNEL);
	if (!pdh_blob)
		return -ENOMEM;

	data.pdh_cert_address = __psp_pa(pdh_blob);
	data.pdh_cert_len = input.pdh_cert_len;

	cert_blob = kzalloc(input.cert_chain_len, GFP_KERNEL);
	if (!cert_blob) {
		ret = -ENOMEM;
		goto e_free_pdh;
	}

	data.cert_chain_address = __psp_pa(cert_blob);
	data.cert_chain_len = input.cert_chain_len;

cmd:
	ret = __sev_do_cmd_locked(SEV_CMD_PDH_CERT_EXPORT, &data, &argp->error);

	/* If we query the length, FW responded with expected data. */
	input.cert_chain_len = data.cert_chain_len;
	input.pdh_cert_len = data.pdh_cert_len;

	if (copy_to_user((void __user *)argp->data, &input, sizeof(input))) {
		ret = -EFAULT;
		goto e_free_cert;
	}

	if (pdh_blob) {
		if (copy_to_user(input_pdh_cert_address,
				 pdh_blob, input.pdh_cert_len)) {
			ret = -EFAULT;
			goto e_free_cert;
		}
	}

	if (cert_blob) {
		if (copy_to_user(input_cert_chain_address,
				 cert_blob, input.cert_chain_len))
			ret = -EFAULT;
	}

e_free_cert:
	kfree(cert_blob);
e_free_pdh:
	kfree(pdh_blob);
	return ret;
}

static int sev_ioctl_do_snp_platform_status(struct sev_issue_cmd *argp)
{
	struct sev_device *sev = psp_master->sev_data;
	struct sev_data_snp_addr buf;
	struct page *status_page;
	void *data;
	int ret;

	if (!sev->snp_initialized || !argp->data)
		return -EINVAL;

	status_page = alloc_page(GFP_KERNEL_ACCOUNT);
	if (!status_page)
		return -ENOMEM;

	data = page_address(status_page);

	/*
	 * Firmware expects status page to be in firmware-owned state, otherwise
	 * it will report firmware error code INVALID_PAGE_STATE (0x1A).
	 */
	if (rmp_mark_pages_firmware(__pa(data), 1, true)) {
		ret = -EFAULT;
		goto cleanup;
	}

	buf.address = __psp_pa(data);
	ret = __sev_do_cmd_locked(SEV_CMD_SNP_PLATFORM_STATUS, &buf, &argp->error);

	/*
	 * Status page will be transitioned to Reclaim state upon success, or
	 * left in Firmware state in failure. Use snp_reclaim_pages() to
	 * transition either case back to Hypervisor-owned state.
	 */
	if (snp_reclaim_pages(__pa(data), 1, true))
		return -EFAULT;

	if (ret)
		goto cleanup;

	if (copy_to_user((void __user *)argp->data, data,
			 sizeof(struct sev_user_data_snp_status)))
		ret = -EFAULT;

cleanup:
	__free_pages(status_page, 0);
	return ret;
}

static int sev_ioctl_do_snp_commit(struct sev_issue_cmd *argp)
{
	struct sev_device *sev = psp_master->sev_data;
	struct sev_data_snp_commit buf;

	if (!sev->snp_initialized)
		return -EINVAL;

	buf.len = sizeof(buf);

	return __sev_do_cmd_locked(SEV_CMD_SNP_COMMIT, &buf, &argp->error);
}

static int sev_ioctl_do_snp_set_config(struct sev_issue_cmd *argp, bool writable)
{
	struct sev_device *sev = psp_master->sev_data;
	struct sev_user_data_snp_config config;

	if (!sev->snp_initialized || !argp->data)
		return -EINVAL;

	if (!writable)
		return -EPERM;

	if (copy_from_user(&config, (void __user *)argp->data, sizeof(config)))
		return -EFAULT;

	return __sev_do_cmd_locked(SEV_CMD_SNP_CONFIG, &config, &argp->error);
}

static int sev_ioctl_do_snp_vlek_load(struct sev_issue_cmd *argp, bool writable)
{
	struct sev_device *sev = psp_master->sev_data;
	struct sev_user_data_snp_vlek_load input;
	void *blob;
	int ret;

	if (!sev->snp_initialized || !argp->data)
		return -EINVAL;

	if (!writable)
		return -EPERM;

	if (copy_from_user(&input, u64_to_user_ptr(argp->data), sizeof(input)))
		return -EFAULT;

	if (input.len != sizeof(input) || input.vlek_wrapped_version != 0)
		return -EINVAL;

	blob = psp_copy_user_blob(input.vlek_wrapped_address,
				  sizeof(struct sev_user_data_snp_wrapped_vlek_hashstick));
	if (IS_ERR(blob))
		return PTR_ERR(blob);

	input.vlek_wrapped_address = __psp_pa(blob);

	ret = __sev_do_cmd_locked(SEV_CMD_SNP_VLEK_LOAD, &input, &argp->error);

	kfree(blob);

	return ret;
}

static long sev_ioctl(struct file *file, unsigned int ioctl, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	struct sev_issue_cmd input;
	int ret = -EFAULT;
	bool writable = file->f_mode & FMODE_WRITE;

	if (!psp_master || !psp_master->sev_data)
		return -ENODEV;

	if (ioctl != SEV_ISSUE_CMD)
		return -EINVAL;

	if (copy_from_user(&input, argp, sizeof(struct sev_issue_cmd)))
		return -EFAULT;

	if (input.cmd > SEV_MAX)
		return -EINVAL;

	mutex_lock(&sev_cmd_mutex);

	switch (input.cmd) {

	case SEV_FACTORY_RESET:
		ret = sev_ioctl_do_reset(&input, writable);
		break;
	case SEV_PLATFORM_STATUS:
		ret = sev_ioctl_do_platform_status(&input);
		break;
	case SEV_PEK_GEN:
		ret = sev_ioctl_do_pek_pdh_gen(SEV_CMD_PEK_GEN, &input, writable);
		break;
	case SEV_PDH_GEN:
		ret = sev_ioctl_do_pek_pdh_gen(SEV_CMD_PDH_GEN, &input, writable);
		break;
	case SEV_PEK_CSR:
		ret = sev_ioctl_do_pek_csr(&input, writable);
		break;
	case SEV_PEK_CERT_IMPORT:
		ret = sev_ioctl_do_pek_import(&input, writable);
		break;
	case SEV_PDH_CERT_EXPORT:
		ret = sev_ioctl_do_pdh_export(&input, writable);
		break;
	case SEV_GET_ID:
		pr_warn_once("SEV_GET_ID command is deprecated, use SEV_GET_ID2\n");
		ret = sev_ioctl_do_get_id(&input);
		break;
	case SEV_GET_ID2:
		ret = sev_ioctl_do_get_id2(&input);
		break;
	case SNP_PLATFORM_STATUS:
		ret = sev_ioctl_do_snp_platform_status(&input);
		break;
	case SNP_COMMIT:
		ret = sev_ioctl_do_snp_commit(&input);
		break;
	case SNP_SET_CONFIG:
		ret = sev_ioctl_do_snp_set_config(&input, writable);
		break;
	case SNP_VLEK_LOAD:
		ret = sev_ioctl_do_snp_vlek_load(&input, writable);
		break;
	default:
		ret = -EINVAL;
		goto out;
	}

	if (copy_to_user(argp, &input, sizeof(struct sev_issue_cmd)))
		ret = -EFAULT;
out:
	mutex_unlock(&sev_cmd_mutex);

	return ret;
}

static const struct file_operations sev_fops = {
	.owner	= THIS_MODULE,
	.unlocked_ioctl = sev_ioctl,
};

int sev_platform_status(struct sev_user_data_status *data, int *error)
{
	return sev_do_cmd(SEV_CMD_PLATFORM_STATUS, data, error);
}
EXPORT_SYMBOL_GPL(sev_platform_status);

int sev_guest_deactivate(struct sev_data_deactivate *data, int *error)
{
	return sev_do_cmd(SEV_CMD_DEACTIVATE, data, error);
}
EXPORT_SYMBOL_GPL(sev_guest_deactivate);

int sev_guest_activate(struct sev_data_activate *data, int *error)
{
	return sev_do_cmd(SEV_CMD_ACTIVATE, data, error);
}
EXPORT_SYMBOL_GPL(sev_guest_activate);

int sev_guest_decommission(struct sev_data_decommission *data, int *error)
{
	return sev_do_cmd(SEV_CMD_DECOMMISSION, data, error);
}
EXPORT_SYMBOL_GPL(sev_guest_decommission);

int sev_guest_df_flush(int *error)
{
	return sev_do_cmd(SEV_CMD_DF_FLUSH, NULL, error);
}
EXPORT_SYMBOL_GPL(sev_guest_df_flush);

static void sev_exit(struct kref *ref)
{
	misc_deregister(&misc_dev->misc);
	kfree(misc_dev);
	misc_dev = NULL;
}

static int sev_misc_init(struct sev_device *sev)
{
	struct device *dev = sev->dev;
	int ret;

	/*
	 * SEV feature support can be detected on multiple devices but the SEV
	 * FW commands must be issued on the master. During probe, we do not
	 * know the master hence we create /dev/sev on the first device probe.
	 * sev_do_cmd() finds the right master device to which to issue the
	 * command to the firmware.
	 */
	if (!misc_dev) {
		struct miscdevice *misc;

		misc_dev = kzalloc(sizeof(*misc_dev), GFP_KERNEL);
		if (!misc_dev)
			return -ENOMEM;

		misc = &misc_dev->misc;
		misc->minor = MISC_DYNAMIC_MINOR;
		misc->name = DEVICE_NAME;
		misc->fops = &sev_fops;

		ret = misc_register(misc);
		if (ret)
			return ret;

		kref_init(&misc_dev->refcount);
	} else {
		kref_get(&misc_dev->refcount);
	}

	init_waitqueue_head(&sev->int_queue);
	sev->misc = misc_dev;
	dev_dbg(dev, "registered SEV device\n");

	return 0;
}

int sev_dev_init(struct psp_device *psp)
{
	struct device *dev = psp->dev;
	struct sev_device *sev;
	int ret = -ENOMEM;

	if (!boot_cpu_has(X86_FEATURE_SEV)) {
		dev_info_once(dev, "SEV: memory encryption not enabled by BIOS\n");
		return 0;
	}

	sev = devm_kzalloc(dev, sizeof(*sev), GFP_KERNEL);
	if (!sev)
		goto e_err;

	sev->cmd_buf = (void *)devm_get_free_pages(dev, GFP_KERNEL, 1);
	if (!sev->cmd_buf)
		goto e_sev;

	sev->cmd_buf_backup = (uint8_t *)sev->cmd_buf + PAGE_SIZE;

	psp->sev_data = sev;

	sev->dev = dev;
	sev->psp = psp;

	sev->io_regs = psp->io_regs;

	sev->vdata = (struct sev_vdata *)psp->vdata->sev;
	if (!sev->vdata) {
		ret = -ENODEV;
		dev_err(dev, "sev: missing driver data\n");
		goto e_buf;
	}

	psp_set_sev_irq_handler(psp, sev_irq_handler, sev);

	ret = sev_misc_init(sev);
	if (ret)
		goto e_irq;

	dev_notice(dev, "sev enabled\n");

	return 0;

e_irq:
	psp_clear_sev_irq_handler(psp);
e_buf:
	devm_free_pages(dev, (unsigned long)sev->cmd_buf);
e_sev:
	devm_kfree(dev, sev);
e_err:
	psp->sev_data = NULL;

	dev_notice(dev, "sev initialization failed\n");

	return ret;
}

static void __sev_firmware_shutdown(struct sev_device *sev, bool panic)
{
	int error;

	__sev_platform_shutdown_locked(NULL);

	if (sev_es_tmr) {
		/*
		 * The TMR area was encrypted, flush it from the cache.
		 *
		 * If invoked during panic handling, local interrupts are
		 * disabled and all CPUs are stopped, so wbinvd_on_all_cpus()
		 * can't be used. In that case, wbinvd() is done on remote CPUs
		 * via the NMI callback, and done for this CPU later during
		 * SNP shutdown, so wbinvd_on_all_cpus() can be skipped.
		 */
		if (!panic)
			wbinvd_on_all_cpus();

		__snp_free_firmware_pages(virt_to_page(sev_es_tmr),
					  get_order(sev_es_tmr_size),
					  true);
		sev_es_tmr = NULL;
	}

	if (sev_init_ex_buffer) {
		__snp_free_firmware_pages(virt_to_page(sev_init_ex_buffer),
					  get_order(NV_LENGTH),
					  true);
		sev_init_ex_buffer = NULL;
	}

	if (snp_range_list) {
		kfree(snp_range_list);
		snp_range_list = NULL;
	}

	__sev_snp_shutdown_locked(&error, panic);
}

static void sev_firmware_shutdown(struct sev_device *sev)
{
	mutex_lock(&sev_cmd_mutex);
	__sev_firmware_shutdown(sev, false);
	mutex_unlock(&sev_cmd_mutex);
}

void sev_dev_destroy(struct psp_device *psp)
{
	struct sev_device *sev = psp->sev_data;

	if (!sev)
		return;

	sev_firmware_shutdown(sev);

	if (sev->misc)
		kref_put(&misc_dev->refcount, sev_exit);

	psp_clear_sev_irq_handler(psp);
}

static int snp_shutdown_on_panic(struct notifier_block *nb,
				 unsigned long reason, void *arg)
{
	struct sev_device *sev = psp_master->sev_data;

	/*
	 * If sev_cmd_mutex is already acquired, then it's likely
	 * another PSP command is in flight and issuing a shutdown
	 * would fail in unexpected ways. Rather than create even
	 * more confusion during a panic, just bail out here.
	 */
	if (mutex_is_locked(&sev_cmd_mutex))
		return NOTIFY_DONE;

	__sev_firmware_shutdown(sev, true);

	return NOTIFY_DONE;
}

static struct notifier_block snp_panic_notifier = {
	.notifier_call = snp_shutdown_on_panic,
};

int sev_issue_cmd_external_user(struct file *filep, unsigned int cmd,
				void *data, int *error)
{
	if (!filep || filep->f_op != &sev_fops)
		return -EBADF;

	return sev_do_cmd(cmd, data, error);
}
EXPORT_SYMBOL_GPL(sev_issue_cmd_external_user);

void sev_pci_init(void)
{
	struct sev_device *sev = psp_master->sev_data;
	struct sev_platform_init_args args = {0};
	u8 api_major, api_minor, build;
	int rc;

	if (!sev)
		return;

	psp_timeout = psp_probe_timeout;

	if (sev_get_api_version())
		goto err;

	api_major = sev->api_major;
	api_minor = sev->api_minor;
	build     = sev->build;

	if (sev_update_firmware(sev->dev) == 0)
		sev_get_api_version();

	if (api_major != sev->api_major || api_minor != sev->api_minor ||
	    build != sev->build)
		dev_info(sev->dev, "SEV firmware updated from %d.%d.%d to %d.%d.%d\n",
			 api_major, api_minor, build,
			 sev->api_major, sev->api_minor, sev->build);

	/* Initialize the platform */
	args.probe = true;
	rc = sev_platform_init(&args);
	if (rc)
		dev_err(sev->dev, "SEV: failed to INIT error %#x, rc %d\n",
			args.error, rc);

	dev_info(sev->dev, "SEV%s API:%d.%d build:%d\n", sev->snp_initialized ?
		"-SNP" : "", sev->api_major, sev->api_minor, sev->build);

	atomic_notifier_chain_register(&panic_notifier_list,
				       &snp_panic_notifier);
	return;

err:
	sev_dev_destroy(psp_master);

	psp_master->sev_data = NULL;
}

void sev_pci_exit(void)
{
	struct sev_device *sev = psp_master->sev_data;

	if (!sev)
		return;

	sev_firmware_shutdown(sev);

	atomic_notifier_chain_unregister(&panic_notifier_list,
					 &snp_panic_notifier);
}

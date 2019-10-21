// SPDX-License-Identifier: GPL-2.0-only
/*
 * AMD Platform Security Processor (PSP) interface
 *
 * Copyright (C) 2016,2018 Advanced Micro Devices, Inc.
 *
 * Author: Brijesh Singh <brijesh.singh@amd.com>
 */

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

#include "sp-dev.h"
#include "psp-dev.h"

#define DEVICE_NAME		"sev"
#define SEV_FW_FILE		"amd/sev.fw"
#define SEV_FW_NAME_SIZE	64

static DEFINE_MUTEX(sev_cmd_mutex);
static struct sev_misc_dev *misc_dev;
static struct psp_device *psp_master;

static int psp_cmd_timeout = 100;
module_param(psp_cmd_timeout, int, 0644);
MODULE_PARM_DESC(psp_cmd_timeout, " default timeout value, in seconds, for PSP commands");

static int psp_probe_timeout = 5;
module_param(psp_probe_timeout, int, 0644);
MODULE_PARM_DESC(psp_probe_timeout, " default timeout value, in seconds, during PSP device probe");

static bool psp_dead;
static int psp_timeout;

static inline bool sev_version_greater_or_equal(u8 maj, u8 min)
{
	if (psp_master->api_major > maj)
		return true;
	if (psp_master->api_major == maj && psp_master->api_minor >= min)
		return true;
	return false;
}

static struct psp_device *psp_alloc_struct(struct sp_device *sp)
{
	struct device *dev = sp->dev;
	struct psp_device *psp;

	psp = devm_kzalloc(dev, sizeof(*psp), GFP_KERNEL);
	if (!psp)
		return NULL;

	psp->dev = dev;
	psp->sp = sp;

	snprintf(psp->name, sizeof(psp->name), "psp-%u", sp->ord);

	return psp;
}

static irqreturn_t psp_irq_handler(int irq, void *data)
{
	struct psp_device *psp = data;
	unsigned int status;
	int reg;

	/* Read the interrupt status: */
	status = ioread32(psp->io_regs + psp->vdata->intsts_reg);

	/* Check if it is command completion: */
	if (!(status & PSP_CMD_COMPLETE))
		goto done;

	/* Check if it is SEV command completion: */
	reg = ioread32(psp->io_regs + psp->vdata->cmdresp_reg);
	if (reg & PSP_CMDRESP_RESP) {
		psp->sev_int_rcvd = 1;
		wake_up(&psp->sev_int_queue);
	}

done:
	/* Clear the interrupt status by writing the same value we read. */
	iowrite32(status, psp->io_regs + psp->vdata->intsts_reg);

	return IRQ_HANDLED;
}

static int sev_wait_cmd_ioc(struct psp_device *psp,
			    unsigned int *reg, unsigned int timeout)
{
	int ret;

	ret = wait_event_timeout(psp->sev_int_queue,
			psp->sev_int_rcvd, timeout * HZ);
	if (!ret)
		return -ETIMEDOUT;

	*reg = ioread32(psp->io_regs + psp->vdata->cmdresp_reg);

	return 0;
}

static int sev_cmd_buffer_len(int cmd)
{
	switch (cmd) {
	case SEV_CMD_INIT:			return sizeof(struct sev_data_init);
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
	default:				return 0;
	}

	return 0;
}

static int __sev_do_cmd_locked(int cmd, void *data, int *psp_ret)
{
	struct psp_device *psp = psp_master;
	unsigned int phys_lsb, phys_msb;
	unsigned int reg, ret = 0;

	if (!psp)
		return -ENODEV;

	if (psp_dead)
		return -EBUSY;

	/* Get the physical address of the command buffer */
	phys_lsb = data ? lower_32_bits(__psp_pa(data)) : 0;
	phys_msb = data ? upper_32_bits(__psp_pa(data)) : 0;

	dev_dbg(psp->dev, "sev command id %#x buffer 0x%08x%08x timeout %us\n",
		cmd, phys_msb, phys_lsb, psp_timeout);

	print_hex_dump_debug("(in):  ", DUMP_PREFIX_OFFSET, 16, 2, data,
			     sev_cmd_buffer_len(cmd), false);

	iowrite32(phys_lsb, psp->io_regs + psp->vdata->cmdbuff_addr_lo_reg);
	iowrite32(phys_msb, psp->io_regs + psp->vdata->cmdbuff_addr_hi_reg);

	psp->sev_int_rcvd = 0;

	reg = cmd;
	reg <<= PSP_CMDRESP_CMD_SHIFT;
	reg |= PSP_CMDRESP_IOC;
	iowrite32(reg, psp->io_regs + psp->vdata->cmdresp_reg);

	/* wait for command completion */
	ret = sev_wait_cmd_ioc(psp, &reg, psp_timeout);
	if (ret) {
		if (psp_ret)
			*psp_ret = 0;

		dev_err(psp->dev, "sev command %#x timed out, disabling PSP \n", cmd);
		psp_dead = true;

		return ret;
	}

	psp_timeout = psp_cmd_timeout;

	if (psp_ret)
		*psp_ret = reg & PSP_CMDRESP_ERR_MASK;

	if (reg & PSP_CMDRESP_ERR_MASK) {
		dev_dbg(psp->dev, "sev command %#x failed (%#010x)\n",
			cmd, reg & PSP_CMDRESP_ERR_MASK);
		ret = -EIO;
	}

	print_hex_dump_debug("(out): ", DUMP_PREFIX_OFFSET, 16, 2, data,
			     sev_cmd_buffer_len(cmd), false);

	return ret;
}

static int sev_do_cmd(int cmd, void *data, int *psp_ret)
{
	int rc;

	mutex_lock(&sev_cmd_mutex);
	rc = __sev_do_cmd_locked(cmd, data, psp_ret);
	mutex_unlock(&sev_cmd_mutex);

	return rc;
}

static int __sev_platform_init_locked(int *error)
{
	struct psp_device *psp = psp_master;
	int rc = 0;

	if (!psp)
		return -ENODEV;

	if (psp->sev_state == SEV_STATE_INIT)
		return 0;

	rc = __sev_do_cmd_locked(SEV_CMD_INIT, &psp->init_cmd_buf, error);
	if (rc)
		return rc;

	psp->sev_state = SEV_STATE_INIT;
	dev_dbg(psp->dev, "SEV firmware initialized\n");

	return rc;
}

int sev_platform_init(int *error)
{
	int rc;

	mutex_lock(&sev_cmd_mutex);
	rc = __sev_platform_init_locked(error);
	mutex_unlock(&sev_cmd_mutex);

	return rc;
}
EXPORT_SYMBOL_GPL(sev_platform_init);

static int __sev_platform_shutdown_locked(int *error)
{
	int ret;

	ret = __sev_do_cmd_locked(SEV_CMD_SHUTDOWN, NULL, error);
	if (ret)
		return ret;

	psp_master->sev_state = SEV_STATE_UNINIT;
	dev_dbg(psp_master->dev, "SEV firmware shutdown\n");

	return ret;
}

static int sev_platform_shutdown(int *error)
{
	int rc;

	mutex_lock(&sev_cmd_mutex);
	rc = __sev_platform_shutdown_locked(NULL);
	mutex_unlock(&sev_cmd_mutex);

	return rc;
}

static int sev_get_platform_state(int *state, int *error)
{
	int rc;

	rc = __sev_do_cmd_locked(SEV_CMD_PLATFORM_STATUS,
				 &psp_master->status_cmd_buf, error);
	if (rc)
		return rc;

	*state = psp_master->status_cmd_buf.state;
	return rc;
}

static int sev_ioctl_do_reset(struct sev_issue_cmd *argp)
{
	int state, rc;

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
	struct sev_user_data_status *data = &psp_master->status_cmd_buf;
	int ret;

	ret = __sev_do_cmd_locked(SEV_CMD_PLATFORM_STATUS, data, &argp->error);
	if (ret)
		return ret;

	if (copy_to_user((void __user *)argp->data, data, sizeof(*data)))
		ret = -EFAULT;

	return ret;
}

static int sev_ioctl_do_pek_pdh_gen(int cmd, struct sev_issue_cmd *argp)
{
	int rc;

	if (psp_master->sev_state == SEV_STATE_UNINIT) {
		rc = __sev_platform_init_locked(&argp->error);
		if (rc)
			return rc;
	}

	return __sev_do_cmd_locked(cmd, NULL, &argp->error);
}

static int sev_ioctl_do_pek_csr(struct sev_issue_cmd *argp)
{
	struct sev_user_data_pek_csr input;
	struct sev_data_pek_csr *data;
	void *blob = NULL;
	int ret;

	if (copy_from_user(&input, (void __user *)argp->data, sizeof(input)))
		return -EFAULT;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	/* userspace wants to query CSR length */
	if (!input.address || !input.length)
		goto cmd;

	/* allocate a physically contiguous buffer to store the CSR blob */
	if (!access_ok(input.address, input.length) ||
	    input.length > SEV_FW_BLOB_MAX_SIZE) {
		ret = -EFAULT;
		goto e_free;
	}

	blob = kmalloc(input.length, GFP_KERNEL);
	if (!blob) {
		ret = -ENOMEM;
		goto e_free;
	}

	data->address = __psp_pa(blob);
	data->len = input.length;

cmd:
	if (psp_master->sev_state == SEV_STATE_UNINIT) {
		ret = __sev_platform_init_locked(&argp->error);
		if (ret)
			goto e_free_blob;
	}

	ret = __sev_do_cmd_locked(SEV_CMD_PEK_CSR, data, &argp->error);

	 /* If we query the CSR length, FW responded with expected data. */
	input.length = data->len;

	if (copy_to_user((void __user *)argp->data, &input, sizeof(input))) {
		ret = -EFAULT;
		goto e_free_blob;
	}

	if (blob) {
		if (copy_to_user((void __user *)input.address, blob, input.length))
			ret = -EFAULT;
	}

e_free_blob:
	kfree(blob);
e_free:
	kfree(data);
	return ret;
}

void *psp_copy_user_blob(u64 __user uaddr, u32 len)
{
	if (!uaddr || !len)
		return ERR_PTR(-EINVAL);

	/* verify that blob length does not exceed our limit */
	if (len > SEV_FW_BLOB_MAX_SIZE)
		return ERR_PTR(-EINVAL);

	return memdup_user((void __user *)(uintptr_t)uaddr, len);
}
EXPORT_SYMBOL_GPL(psp_copy_user_blob);

static int sev_get_api_version(void)
{
	struct sev_user_data_status *status;
	int error = 0, ret;

	status = &psp_master->status_cmd_buf;
	ret = sev_platform_status(status, &error);
	if (ret) {
		dev_err(psp_master->dev,
			"SEV: failed to get status. Error: %#x\n", error);
		return 1;
	}

	psp_master->api_major = status->api_major;
	psp_master->api_minor = status->api_minor;
	psp_master->build = status->build;
	psp_master->sev_state = status->state;

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
	if (ret)
		dev_dbg(dev, "Failed to update SEV firmware: %#x\n", error);
	else
		dev_info(dev, "SEV firmware update successful\n");

	__free_pages(p, order);

fw_err:
	release_firmware(firmware);

	return ret;
}

static int sev_ioctl_do_pek_import(struct sev_issue_cmd *argp)
{
	struct sev_user_data_pek_cert_import input;
	struct sev_data_pek_cert_import *data;
	void *pek_blob, *oca_blob;
	int ret;

	if (copy_from_user(&input, (void __user *)argp->data, sizeof(input)))
		return -EFAULT;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	/* copy PEK certificate blobs from userspace */
	pek_blob = psp_copy_user_blob(input.pek_cert_address, input.pek_cert_len);
	if (IS_ERR(pek_blob)) {
		ret = PTR_ERR(pek_blob);
		goto e_free;
	}

	data->pek_cert_address = __psp_pa(pek_blob);
	data->pek_cert_len = input.pek_cert_len;

	/* copy PEK certificate blobs from userspace */
	oca_blob = psp_copy_user_blob(input.oca_cert_address, input.oca_cert_len);
	if (IS_ERR(oca_blob)) {
		ret = PTR_ERR(oca_blob);
		goto e_free_pek;
	}

	data->oca_cert_address = __psp_pa(oca_blob);
	data->oca_cert_len = input.oca_cert_len;

	/* If platform is not in INIT state then transition it to INIT */
	if (psp_master->sev_state != SEV_STATE_INIT) {
		ret = __sev_platform_init_locked(&argp->error);
		if (ret)
			goto e_free_oca;
	}

	ret = __sev_do_cmd_locked(SEV_CMD_PEK_CERT_IMPORT, data, &argp->error);

e_free_oca:
	kfree(oca_blob);
e_free_pek:
	kfree(pek_blob);
e_free:
	kfree(data);
	return ret;
}

static int sev_ioctl_do_get_id2(struct sev_issue_cmd *argp)
{
	struct sev_user_data_get_id2 input;
	struct sev_data_get_id *data;
	void *id_blob = NULL;
	int ret;

	/* SEV GET_ID is available from SEV API v0.16 and up */
	if (!sev_version_greater_or_equal(0, 16))
		return -ENOTSUPP;

	if (copy_from_user(&input, (void __user *)argp->data, sizeof(input)))
		return -EFAULT;

	/* Check if we have write access to the userspace buffer */
	if (input.address &&
	    input.length &&
	    !access_ok(input.address, input.length))
		return -EFAULT;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	if (input.address && input.length) {
		id_blob = kmalloc(input.length, GFP_KERNEL);
		if (!id_blob) {
			kfree(data);
			return -ENOMEM;
		}

		data->address = __psp_pa(id_blob);
		data->len = input.length;
	}

	ret = __sev_do_cmd_locked(SEV_CMD_GET_ID, data, &argp->error);

	/*
	 * Firmware will return the length of the ID value (either the minimum
	 * required length or the actual length written), return it to the user.
	 */
	input.length = data->len;

	if (copy_to_user((void __user *)argp->data, &input, sizeof(input))) {
		ret = -EFAULT;
		goto e_free;
	}

	if (id_blob) {
		if (copy_to_user((void __user *)input.address,
				 id_blob, data->len)) {
			ret = -EFAULT;
			goto e_free;
		}
	}

e_free:
	kfree(id_blob);
	kfree(data);

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

static int sev_ioctl_do_pdh_export(struct sev_issue_cmd *argp)
{
	struct sev_user_data_pdh_cert_export input;
	void *pdh_blob = NULL, *cert_blob = NULL;
	struct sev_data_pdh_cert_export *data;
	int ret;

	if (copy_from_user(&input, (void __user *)argp->data, sizeof(input)))
		return -EFAULT;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	/* Userspace wants to query the certificate length. */
	if (!input.pdh_cert_address ||
	    !input.pdh_cert_len ||
	    !input.cert_chain_address)
		goto cmd;

	/* Allocate a physically contiguous buffer to store the PDH blob. */
	if ((input.pdh_cert_len > SEV_FW_BLOB_MAX_SIZE) ||
	    !access_ok(input.pdh_cert_address, input.pdh_cert_len)) {
		ret = -EFAULT;
		goto e_free;
	}

	/* Allocate a physically contiguous buffer to store the cert chain blob. */
	if ((input.cert_chain_len > SEV_FW_BLOB_MAX_SIZE) ||
	    !access_ok(input.cert_chain_address, input.cert_chain_len)) {
		ret = -EFAULT;
		goto e_free;
	}

	pdh_blob = kmalloc(input.pdh_cert_len, GFP_KERNEL);
	if (!pdh_blob) {
		ret = -ENOMEM;
		goto e_free;
	}

	data->pdh_cert_address = __psp_pa(pdh_blob);
	data->pdh_cert_len = input.pdh_cert_len;

	cert_blob = kmalloc(input.cert_chain_len, GFP_KERNEL);
	if (!cert_blob) {
		ret = -ENOMEM;
		goto e_free_pdh;
	}

	data->cert_chain_address = __psp_pa(cert_blob);
	data->cert_chain_len = input.cert_chain_len;

cmd:
	/* If platform is not in INIT state then transition it to INIT. */
	if (psp_master->sev_state != SEV_STATE_INIT) {
		ret = __sev_platform_init_locked(&argp->error);
		if (ret)
			goto e_free_cert;
	}

	ret = __sev_do_cmd_locked(SEV_CMD_PDH_CERT_EXPORT, data, &argp->error);

	/* If we query the length, FW responded with expected data. */
	input.cert_chain_len = data->cert_chain_len;
	input.pdh_cert_len = data->pdh_cert_len;

	if (copy_to_user((void __user *)argp->data, &input, sizeof(input))) {
		ret = -EFAULT;
		goto e_free_cert;
	}

	if (pdh_blob) {
		if (copy_to_user((void __user *)input.pdh_cert_address,
				 pdh_blob, input.pdh_cert_len)) {
			ret = -EFAULT;
			goto e_free_cert;
		}
	}

	if (cert_blob) {
		if (copy_to_user((void __user *)input.cert_chain_address,
				 cert_blob, input.cert_chain_len))
			ret = -EFAULT;
	}

e_free_cert:
	kfree(cert_blob);
e_free_pdh:
	kfree(pdh_blob);
e_free:
	kfree(data);
	return ret;
}

static long sev_ioctl(struct file *file, unsigned int ioctl, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	struct sev_issue_cmd input;
	int ret = -EFAULT;

	if (!psp_master)
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
		ret = sev_ioctl_do_reset(&input);
		break;
	case SEV_PLATFORM_STATUS:
		ret = sev_ioctl_do_platform_status(&input);
		break;
	case SEV_PEK_GEN:
		ret = sev_ioctl_do_pek_pdh_gen(SEV_CMD_PEK_GEN, &input);
		break;
	case SEV_PDH_GEN:
		ret = sev_ioctl_do_pek_pdh_gen(SEV_CMD_PDH_GEN, &input);
		break;
	case SEV_PEK_CSR:
		ret = sev_ioctl_do_pek_csr(&input);
		break;
	case SEV_PEK_CERT_IMPORT:
		ret = sev_ioctl_do_pek_import(&input);
		break;
	case SEV_PDH_CERT_EXPORT:
		ret = sev_ioctl_do_pdh_export(&input);
		break;
	case SEV_GET_ID:
		pr_warn_once("SEV_GET_ID command is deprecated, use SEV_GET_ID2\n");
		ret = sev_ioctl_do_get_id(&input);
		break;
	case SEV_GET_ID2:
		ret = sev_ioctl_do_get_id2(&input);
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
	struct sev_misc_dev *misc_dev = container_of(ref, struct sev_misc_dev, refcount);

	misc_deregister(&misc_dev->misc);
}

static int sev_misc_init(struct psp_device *psp)
{
	struct device *dev = psp->dev;
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

		misc_dev = devm_kzalloc(dev, sizeof(*misc_dev), GFP_KERNEL);
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

	init_waitqueue_head(&psp->sev_int_queue);
	psp->sev_misc = misc_dev;
	dev_dbg(dev, "registered SEV device\n");

	return 0;
}

static int psp_check_sev_support(struct psp_device *psp)
{
	unsigned int val = ioread32(psp->io_regs + psp->vdata->feature_reg);

	/*
	 * Check for a access to the registers.  If this read returns
	 * 0xffffffff, it's likely that the system is running a broken
	 * BIOS which disallows access to the device. Stop here and
	 * fail the PSP initialization (but not the load, as the CCP
	 * could get properly initialized).
	 */
	if (val == 0xffffffff) {
		dev_notice(psp->dev, "psp: unable to access the device: you might be running a broken BIOS.\n");
		return -ENODEV;
	}

	if (!(val & 1)) {
		/* Device does not support the SEV feature */
		dev_dbg(psp->dev, "psp does not support SEV\n");
		return -ENODEV;
	}

	return 0;
}

int psp_dev_init(struct sp_device *sp)
{
	struct device *dev = sp->dev;
	struct psp_device *psp;
	int ret;

	ret = -ENOMEM;
	psp = psp_alloc_struct(sp);
	if (!psp)
		goto e_err;

	sp->psp_data = psp;

	psp->vdata = (struct psp_vdata *)sp->dev_vdata->psp_vdata;
	if (!psp->vdata) {
		ret = -ENODEV;
		dev_err(dev, "missing driver data\n");
		goto e_err;
	}

	psp->io_regs = sp->io_map;

	ret = psp_check_sev_support(psp);
	if (ret)
		goto e_disable;

	/* Disable and clear interrupts until ready */
	iowrite32(0, psp->io_regs + psp->vdata->inten_reg);
	iowrite32(-1, psp->io_regs + psp->vdata->intsts_reg);

	/* Request an irq */
	ret = sp_request_psp_irq(psp->sp, psp_irq_handler, psp->name, psp);
	if (ret) {
		dev_err(dev, "psp: unable to allocate an IRQ\n");
		goto e_err;
	}

	ret = sev_misc_init(psp);
	if (ret)
		goto e_irq;

	if (sp->set_psp_master_device)
		sp->set_psp_master_device(sp);

	/* Enable interrupt */
	iowrite32(-1, psp->io_regs + psp->vdata->inten_reg);

	dev_notice(dev, "psp enabled\n");

	return 0;

e_irq:
	sp_free_psp_irq(psp->sp, psp);
e_err:
	sp->psp_data = NULL;

	dev_notice(dev, "psp initialization failed\n");

	return ret;

e_disable:
	sp->psp_data = NULL;

	return ret;
}

void psp_dev_destroy(struct sp_device *sp)
{
	struct psp_device *psp = sp->psp_data;

	if (!psp)
		return;

	if (psp->sev_misc)
		kref_put(&misc_dev->refcount, sev_exit);

	sp_free_psp_irq(sp, psp);
}

int sev_issue_cmd_external_user(struct file *filep, unsigned int cmd,
				void *data, int *error)
{
	if (!filep || filep->f_op != &sev_fops)
		return -EBADF;

	return  sev_do_cmd(cmd, data, error);
}
EXPORT_SYMBOL_GPL(sev_issue_cmd_external_user);

void psp_pci_init(void)
{
	struct sp_device *sp;
	int error, rc;

	sp = sp_get_psp_master_device();
	if (!sp)
		return;

	psp_master = sp->psp_data;

	psp_timeout = psp_probe_timeout;

	if (sev_get_api_version())
		goto err;

	/*
	 * If platform is not in UNINIT state then firmware upgrade and/or
	 * platform INIT command will fail. These command require UNINIT state.
	 *
	 * In a normal boot we should never run into case where the firmware
	 * is not in UNINIT state on boot. But in case of kexec boot, a reboot
	 * may not go through a typical shutdown sequence and may leave the
	 * firmware in INIT or WORKING state.
	 */

	if (psp_master->sev_state != SEV_STATE_UNINIT) {
		sev_platform_shutdown(NULL);
		psp_master->sev_state = SEV_STATE_UNINIT;
	}

	if (sev_version_greater_or_equal(0, 15) &&
	    sev_update_firmware(psp_master->dev) == 0)
		sev_get_api_version();

	/* Initialize the platform */
	rc = sev_platform_init(&error);
	if (rc && (error == SEV_RET_SECURE_DATA_INVALID)) {
		/*
		 * INIT command returned an integrity check failure
		 * status code, meaning that firmware load and
		 * validation of SEV related persistent data has
		 * failed and persistent state has been erased.
		 * Retrying INIT command here should succeed.
		 */
		dev_dbg(sp->dev, "SEV: retrying INIT command");
		rc = sev_platform_init(&error);
	}

	if (rc) {
		dev_err(sp->dev, "SEV: failed to INIT error %#x\n", error);
		return;
	}

	dev_info(sp->dev, "SEV API:%d.%d build:%d\n", psp_master->api_major,
		 psp_master->api_minor, psp_master->build);

	return;

err:
	psp_master = NULL;
}

void psp_pci_exit(void)
{
	if (!psp_master)
		return;

	sev_platform_shutdown(NULL);
}

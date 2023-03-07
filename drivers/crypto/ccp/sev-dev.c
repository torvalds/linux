// SPDX-License-Identifier: GPL-2.0-only
/*
 * AMD Secure Encrypted Virtualization (SEV) interface
 *
 * Copyright (C) 2016,2019 Advanced Micro Devices, Inc.
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
#include <linux/gfp.h>
#include <linux/cpufeature.h>
#include <linux/fs.h>
#include <linux/fs_struct.h>

#include <asm/smp.h>
#include <asm/cacheflush.h>

#include "psp-dev.h"
#include "sev-dev.h"

#define DEVICE_NAME		"sev"
#define SEV_FW_FILE		"amd/sev.fw"
#define SEV_FW_NAME_SIZE	64

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
 */
#define SEV_ES_TMR_SIZE		(1024 * 1024)
static void *sev_es_tmr;

/* INIT_EX NV Storage:
 *   The NV Storage is a 32Kb area and must be 4Kb page aligned.  Use the page
 *   allocator to allocate the memory, which will return aligned memory for the
 *   specified allocation order.
 */
#define NV_LENGTH (32 * 1024)
static void *sev_init_ex_buffer;

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
	if (reg & PSP_CMDRESP_RESP) {
		sev->int_rcvd = 1;
		wake_up(&sev->int_queue);
	}
}

static int sev_wait_cmd_ioc(struct sev_device *sev,
			    unsigned int *reg, unsigned int timeout)
{
	int ret;

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
	default:				return 0;
	}

	return 0;
}

static void *sev_fw_alloc(unsigned long len)
{
	struct page *page;

	page = alloc_pages(GFP_KERNEL, get_order(len));
	if (!page)
		return NULL;

	return page_address(page);
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

	revert_creds(old_cred);

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

static int __sev_do_cmd_locked(int cmd, void *data, int *psp_ret)
{
	struct psp_device *psp = psp_master;
	struct sev_device *sev;
	unsigned int phys_lsb, phys_msb;
	unsigned int reg, ret = 0;
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
	if (data)
		memcpy(sev->cmd_buf, data, buf_len);

	/* Get the physical address of the command buffer */
	phys_lsb = data ? lower_32_bits(__psp_pa(sev->cmd_buf)) : 0;
	phys_msb = data ? upper_32_bits(__psp_pa(sev->cmd_buf)) : 0;

	dev_dbg(sev->dev, "sev command id %#x buffer 0x%08x%08x timeout %us\n",
		cmd, phys_msb, phys_lsb, psp_timeout);

	print_hex_dump_debug("(in):  ", DUMP_PREFIX_OFFSET, 16, 2, data,
			     buf_len, false);

	iowrite32(phys_lsb, sev->io_regs + sev->vdata->cmdbuff_addr_lo_reg);
	iowrite32(phys_msb, sev->io_regs + sev->vdata->cmdbuff_addr_hi_reg);

	sev->int_rcvd = 0;

	reg = cmd;
	reg <<= SEV_CMDRESP_CMD_SHIFT;
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
		*psp_ret = reg & PSP_CMDRESP_ERR_MASK;

	if (reg & PSP_CMDRESP_ERR_MASK) {
		dev_dbg(sev->dev, "sev command %#x failed (%#010x)\n",
			cmd, reg & PSP_CMDRESP_ERR_MASK);
		ret = -EIO;
	} else {
		ret = sev_write_init_ex_file_if_required(cmd);
	}

	print_hex_dump_debug("(out): ", DUMP_PREFIX_OFFSET, 16, 2, data,
			     buf_len, false);

	/*
	 * Copy potential output from the PSP back to data.  Do this even on
	 * failure in case the caller wants to glean something from the error.
	 */
	if (data)
		memcpy(data, sev->cmd_buf, buf_len);

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
		data.tmr_len = SEV_ES_TMR_SIZE;
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
		data.tmr_len = SEV_ES_TMR_SIZE;
	}

	return __sev_do_cmd_locked(SEV_CMD_INIT_EX, &data, error);
}

static int __sev_platform_init_locked(int *error)
{
	int rc = 0, psp_ret = SEV_RET_NO_FW_CALL;
	struct psp_device *psp = psp_master;
	int (*init_function)(int *error);
	struct sev_device *sev;

	if (!psp || !psp->sev_data)
		return -ENODEV;

	sev = psp->sev_data;

	if (sev->state == SEV_STATE_INIT)
		return 0;

	if (sev_init_ex_buffer) {
		init_function = __sev_init_ex_locked;
		rc = sev_read_init_ex_file();
		if (rc)
			return rc;
	} else {
		init_function = __sev_init_locked;
	}

	rc = init_function(&psp_ret);
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
		rc = init_function(&psp_ret);
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
	struct sev_device *sev = psp_master->sev_data;
	int ret;

	if (!sev || sev->state == SEV_STATE_UNINIT)
		return 0;

	ret = __sev_do_cmd_locked(SEV_CMD_SHUTDOWN, NULL, error);
	if (ret)
		return ret;

	sev->state = SEV_STATE_UNINIT;
	dev_dbg(sev->dev, "SEV firmware shutdown\n");

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
	else
		dev_info(dev, "SEV firmware update successful\n");

	__free_pages(p, order);

fw_err:
	release_firmware(firmware);

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
		 * to 1 << (PAGE_SHIFT + MAX_ORDER - 1) by the page allocator.
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

	sev->cmd_buf = (void *)devm_get_free_pages(dev, GFP_KERNEL, 0);
	if (!sev->cmd_buf)
		goto e_sev;

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

static void sev_firmware_shutdown(struct sev_device *sev)
{
	sev_platform_shutdown(NULL);

	if (sev_es_tmr) {
		/* The TMR area was encrypted, flush it from the cache */
		wbinvd_on_all_cpus();

		free_pages((unsigned long)sev_es_tmr,
			   get_order(SEV_ES_TMR_SIZE));
		sev_es_tmr = NULL;
	}

	if (sev_init_ex_buffer) {
		free_pages((unsigned long)sev_init_ex_buffer,
			   get_order(NV_LENGTH));
		sev_init_ex_buffer = NULL;
	}
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
	int error, rc;

	if (!sev)
		return;

	psp_timeout = psp_probe_timeout;

	if (sev_get_api_version())
		goto err;

	if (sev_update_firmware(sev->dev) == 0)
		sev_get_api_version();

	/* If an init_ex_path is provided rely on INIT_EX for PSP initialization
	 * instead of INIT.
	 */
	if (init_ex_path) {
		sev_init_ex_buffer = sev_fw_alloc(NV_LENGTH);
		if (!sev_init_ex_buffer) {
			dev_err(sev->dev,
				"SEV: INIT_EX NV memory allocation failed\n");
			goto err;
		}
	}

	/* Obtain the TMR memory area for SEV-ES use */
	sev_es_tmr = sev_fw_alloc(SEV_ES_TMR_SIZE);
	if (sev_es_tmr)
		/* Must flush the cache before giving it to the firmware */
		clflush_cache_range(sev_es_tmr, SEV_ES_TMR_SIZE);
	else
		dev_warn(sev->dev,
			 "SEV: TMR allocation failed, SEV-ES support unavailable\n");

	if (!psp_init_on_probe)
		return;

	/* Initialize the platform */
	rc = sev_platform_init(&error);
	if (rc)
		dev_err(sev->dev, "SEV: failed to INIT error %#x, rc %d\n",
			error, rc);

	return;

err:
	psp_master->sev_data = NULL;
}

void sev_pci_exit(void)
{
	struct sev_device *sev = psp_master->sev_data;

	if (!sev)
		return;

	sev_firmware_shutdown(sev);
}

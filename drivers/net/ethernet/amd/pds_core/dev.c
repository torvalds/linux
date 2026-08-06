// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2023 Advanced Micro Devices, Inc */

#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/utsname.h>

#include "core.h"

int pdsc_err_to_errno(enum pds_core_status_code code)
{
	switch (code) {
	case PDS_RC_SUCCESS:
		return 0;
	case PDS_RC_EVERSION:
	case PDS_RC_EQTYPE:
	case PDS_RC_EQID:
	case PDS_RC_EINVAL:
	case PDS_RC_ENOSUPP:
		return -EINVAL;
	case PDS_RC_EPERM:
		return -EPERM;
	case PDS_RC_ENOENT:
		return -ENOENT;
	case PDS_RC_EAGAIN:
		return -EAGAIN;
	case PDS_RC_ENOMEM:
		return -ENOMEM;
	case PDS_RC_EFAULT:
		return -EFAULT;
	case PDS_RC_EBUSY:
		return -EBUSY;
	case PDS_RC_EEXIST:
		return -EEXIST;
	case PDS_RC_EVFID:
		return -ENODEV;
	case PDS_RC_ECLIENT:
		return -ECHILD;
	case PDS_RC_ENOSPC:
		return -ENOSPC;
	case PDS_RC_ERANGE:
		return -ERANGE;
	case PDS_RC_BAD_ADDR:
		return -EFAULT;
	case PDS_RC_BAD_PCI:
		return -ENXIO;
	case PDS_RC_EOPCODE:
	case PDS_RC_EINTR:
	case PDS_RC_DEV_CMD:
	case PDS_RC_ERROR:
	case PDS_RC_ERDMA:
	case PDS_RC_EIO:
	default:
		return -EIO;
	}
}

bool pdsc_is_fw_running(struct pdsc *pdsc)
{
	if (!pdsc->info_regs)
		return false;

	pdsc->fw_status = ioread8(&pdsc->info_regs->fw_status);
	pdsc->last_fw_time = jiffies;
	pdsc->last_hb = ioread32(&pdsc->info_regs->fw_heartbeat);

	/* Firmware is useful only if the running bit is set and
	 * fw_status != 0xff (bad PCI read)
	 */
	return (pdsc->fw_status != PDS_RC_BAD_PCI) &&
		(pdsc->fw_status & PDS_CORE_FW_STS_F_RUNNING);
}

bool pdsc_is_fw_good(struct pdsc *pdsc)
{
	bool fw_running = pdsc_is_fw_running(pdsc);
	u8 gen;

	/* Make sure to update the cached fw_status by calling
	 * pdsc_is_fw_running() before getting the generation
	 */
	gen = pdsc->fw_status & PDS_CORE_FW_STS_F_GENERATION;

	return fw_running && gen == pdsc->fw_generation;
}

static u8 pdsc_devcmd_status(struct pdsc *pdsc)
{
	return ioread8(&pdsc->cmd_regs->comp.status);
}

static bool pdsc_devcmd_done(struct pdsc *pdsc)
{
	return ioread32(&pdsc->cmd_regs->done) & PDS_CORE_DEV_CMD_DONE;
}

static void pdsc_devcmd_dbell(struct pdsc *pdsc)
{
	iowrite32(0, &pdsc->cmd_regs->done);
	iowrite32(1, &pdsc->cmd_regs->doorbell);
}

static void pdsc_devcmd_clean(struct pdsc *pdsc)
{
	iowrite32(0, &pdsc->cmd_regs->doorbell);
	memset_io(&pdsc->cmd_regs->cmd, 0, sizeof(pdsc->cmd_regs->cmd));
}

static const char *pdsc_devcmd_str(int opcode)
{
	switch (opcode) {
	case PDS_CORE_CMD_NOP:
		return "PDS_CORE_CMD_NOP";
	case PDS_CORE_CMD_IDENTIFY:
		return "PDS_CORE_CMD_IDENTIFY";
	case PDS_CORE_CMD_RESET:
		return "PDS_CORE_CMD_RESET";
	case PDS_CORE_CMD_INIT:
		return "PDS_CORE_CMD_INIT";
	case PDS_CORE_CMD_FW_DOWNLOAD:
		return "PDS_CORE_CMD_FW_DOWNLOAD";
	case PDS_CORE_CMD_FW_CONTROL:
		return "PDS_CORE_CMD_FW_CONTROL";
	default:
		return "PDS_CORE_CMD_UNKNOWN";
	}
}

static int __pdsc_devcmd_wait(struct pdsc *pdsc, u8 opcode, int max_seconds,
			      const bool do_msg)
{
	struct device *dev = pdsc->dev;
	unsigned long start_time;
	unsigned long max_wait;
	unsigned long duration;
	int timeout = 0;
	bool running;
	int done = 0;
	int err = 0;
	int status;

	start_time = jiffies;
	max_wait = start_time + (max_seconds * HZ);

	while (!done && !timeout) {
		running = pdsc_is_fw_running(pdsc);
		if (!running)
			break;

		done = pdsc_devcmd_done(pdsc);
		if (done)
			break;

		timeout = time_after(jiffies, max_wait);
		if (timeout)
			break;

		usleep_range(100, 200);
	}
	duration = jiffies - start_time;

	if (done && duration > HZ)
		dev_dbg(dev, "DEVCMD %d %s after %ld secs\n",
			opcode, pdsc_devcmd_str(opcode), duration / HZ);

	if (!running) {
		dev_err(dev, "DEVCMD %d %s fw not running\n",
			opcode, pdsc_devcmd_str(opcode));
		pdsc_devcmd_clean(pdsc);
		return -ENXIO;
	}

	if (!done || timeout) {
		dev_err(dev, "DEVCMD %d %s timeout, done %d timeout %d max_seconds=%d\n",
			opcode, pdsc_devcmd_str(opcode), done, timeout,
			max_seconds);
		pdsc_devcmd_clean(pdsc);
		return -ETIMEDOUT;
	}

	status = pdsc_devcmd_status(pdsc);
	err = pdsc_err_to_errno(status);
	if (do_msg && err && err != -EAGAIN)
		dev_err(dev, "DEVCMD %d %s failed, status=%d err %d %pe\n",
			opcode, pdsc_devcmd_str(opcode), status, err,
			ERR_PTR(err));

	return err;
}

static int __pdsc_devcmd_locked(struct pdsc *pdsc, union pds_core_dev_cmd *cmd,
				union pds_core_dev_comp *comp, int max_seconds,
				const bool do_msg)
{
	int err;

	if (!pdsc->cmd_regs)
		return -ENXIO;

	memcpy_toio(&pdsc->cmd_regs->cmd, cmd, sizeof(*cmd));
	pdsc_devcmd_dbell(pdsc);
	err = __pdsc_devcmd_wait(pdsc, cmd->opcode, max_seconds, do_msg);

	if ((err == -ENXIO || err == -ETIMEDOUT) && pdsc->wq)
		queue_work(pdsc->wq, &pdsc->health_work);
	else
		memcpy_fromio(comp, &pdsc->cmd_regs->comp, sizeof(*comp));

	if (err != -ETIMEDOUT && err != -EAGAIN)
		pdsc_deferred_dma_free(pdsc);

	return err;
}

void pdsc_deferred_dma_add(struct pdsc *pdsc, struct pdsc_deferred_dma *entry,
			   dma_addr_t dma_addr, void *va, size_t size,
			   enum dma_data_direction dir)
{
	entry->dma_addr = dma_addr;
	entry->va = va;
	entry->size = size;
	entry->dir = dir;

	spin_lock(&pdsc->deferred_dma_lock);
	list_add_tail(&entry->list, &pdsc->deferred_dma_list);
	spin_unlock(&pdsc->deferred_dma_lock);
}

void pdsc_deferred_dma_free(struct pdsc *pdsc)
{
	struct pdsc_deferred_dma *entry, *tmp;
	LIST_HEAD(local_list);

	spin_lock(&pdsc->deferred_dma_lock);
	list_splice_init(&pdsc->deferred_dma_list, &local_list);
	spin_unlock(&pdsc->deferred_dma_lock);

	list_for_each_entry_safe(entry, tmp, &local_list, list) {
		dma_unmap_single(pdsc->dev, entry->dma_addr,
				 entry->size, entry->dir);
		kfree(entry->va);
		list_del(&entry->list);
		kfree(entry);
	}
}

int pdsc_devcmd_locked(struct pdsc *pdsc, union pds_core_dev_cmd *cmd,
		       union pds_core_dev_comp *comp, int max_seconds)
{
	return __pdsc_devcmd_locked(pdsc, cmd, comp, max_seconds, true);
}

int pdsc_devcmd_locked_nomsg(struct pdsc *pdsc, union pds_core_dev_cmd *cmd,
			     union pds_core_dev_comp *comp, int max_seconds)
{
	return __pdsc_devcmd_locked(pdsc, cmd, comp, max_seconds, false);
}

int pdsc_devcmd(struct pdsc *pdsc, union pds_core_dev_cmd *cmd,
		union pds_core_dev_comp *comp, int max_seconds)
{
	int err;

	mutex_lock(&pdsc->devcmd_lock);
	err = pdsc_devcmd_locked(pdsc, cmd, comp, max_seconds);
	mutex_unlock(&pdsc->devcmd_lock);

	return err;
}

static int __pdsc_devcmd_with_data(struct pdsc *pdsc,
				   union pds_core_dev_cmd *cmd,
				   const void *data, size_t data_len,
				   union pds_core_dev_comp *comp,
				   int max_seconds, bool do_msg)
{
	int err;

	mutex_lock(&pdsc->devcmd_lock);
	if (!pdsc->cmd_regs) {
		err = -ENXIO;
		goto unlock;
	}
	if (data_len > sizeof(pdsc->cmd_regs->data)) {
		err = -ENOSPC;
		goto unlock;
	}
	memcpy_toio(&pdsc->cmd_regs->data, data, data_len);
	err = __pdsc_devcmd_locked(pdsc, cmd, comp, max_seconds, do_msg);
unlock:
	mutex_unlock(&pdsc->devcmd_lock);

	return err;
}

int pdsc_devcmd_with_data(struct pdsc *pdsc, union pds_core_dev_cmd *cmd,
			  const void *data, size_t data_len,
			  union pds_core_dev_comp *comp, int max_seconds)
{
	return __pdsc_devcmd_with_data(pdsc, cmd, data, data_len,
				       comp, max_seconds, true);
}

int pdsc_devcmd_with_data_nomsg(struct pdsc *pdsc, union pds_core_dev_cmd *cmd,
				const void *data, size_t data_len,
				union pds_core_dev_comp *comp, int max_seconds)
{
	return __pdsc_devcmd_with_data(pdsc, cmd, data, data_len,
				       comp, max_seconds, false);
}

int pdsc_devcmd_init(struct pdsc *pdsc)
{
	union pds_core_dev_comp comp = {};
	union pds_core_dev_cmd cmd = {
		.opcode = PDS_CORE_CMD_INIT,
	};

	return pdsc_devcmd(pdsc, &cmd, &comp, pdsc->devcmd_timeout);
}

int pdsc_devcmd_reset(struct pdsc *pdsc)
{
	union pds_core_dev_comp comp = {};
	union pds_core_dev_cmd cmd = {
		.reset.opcode = PDS_CORE_CMD_RESET,
	};

	if (!pdsc_is_fw_running(pdsc))
		return 0;

	return pdsc_devcmd(pdsc, &cmd, &comp, pdsc->devcmd_timeout);
}

static int pdsc_devcmd_identify_locked(struct pdsc *pdsc, u8 drv_ident_ver,
				       bool do_msg)
{
	union pds_core_dev_comp comp = {};
	union pds_core_dev_cmd cmd = {
		.identify.opcode = PDS_CORE_CMD_IDENTIFY,
		.identify.ver = drv_ident_ver,
	};

	return __pdsc_devcmd_locked(pdsc, &cmd, &comp, pdsc->devcmd_timeout,
				    do_msg);
}

static void pdsc_init_devinfo(struct pdsc *pdsc)
{
	pdsc->dev_info.asic_type = ioread8(&pdsc->info_regs->asic_type);
	pdsc->dev_info.asic_rev = ioread8(&pdsc->info_regs->asic_rev);
	pdsc->fw_generation = PDS_CORE_FW_STS_F_GENERATION &
			      ioread8(&pdsc->info_regs->fw_status);

	memcpy_fromio(pdsc->dev_info.fw_version,
		      pdsc->info_regs->fw_version,
		      PDS_CORE_DEVINFO_FWVERS_BUFLEN);
	pdsc->dev_info.fw_version[PDS_CORE_DEVINFO_FWVERS_BUFLEN] = 0;

	memcpy_fromio(pdsc->dev_info.serial_num,
		      pdsc->info_regs->serial_num,
		      PDS_CORE_DEVINFO_SERIAL_BUFLEN);
	pdsc->dev_info.serial_num[PDS_CORE_DEVINFO_SERIAL_BUFLEN] = 0;

	dev_dbg(pdsc->dev, "fw_version %s\n", pdsc->dev_info.fw_version);
}

static int pdsc_identify_ver(struct pdsc *pdsc, u8 drv_ident_ver)
{
	bool do_msg = drv_ident_ver == PDS_CORE_IDENTITY_VERSION_1;
	struct pds_core_drv_identity drv = {};
	size_t sz;
	int err;
	int n;

	drv.drv_type = cpu_to_le32(PDS_DRIVER_LINUX);
	/* Catching the return quiets a Wformat-truncation complaint */
	n = snprintf(drv.driver_ver_str, sizeof(drv.driver_ver_str),
		     "%s %s", PDS_CORE_DRV_NAME, utsname()->release);
	if (n > sizeof(drv.driver_ver_str))
		dev_dbg(pdsc->dev, "release name truncated, don't care\n");

	/* Next let's get some info about the device
	 * We use the devcmd_lock at this level in order to
	 * get safe access to the cmd_regs->data before anyone
	 * else can mess it up
	 */
	mutex_lock(&pdsc->devcmd_lock);

	sz = min_t(size_t, sizeof(drv), sizeof(pdsc->cmd_regs->data));
	memcpy_toio(&pdsc->cmd_regs->data, &drv, sz);

	err = pdsc_devcmd_identify_locked(pdsc, drv_ident_ver, do_msg);
	if (!err) {
		sz = min_t(size_t, sizeof(pdsc->dev_ident),
			   sizeof(pdsc->cmd_regs->data));
		memcpy_fromio(&pdsc->dev_ident, &pdsc->cmd_regs->data, sz);

		/* V1 firmware doesn't set capabilities, so the field may
		 * contain garbage from the outgoing driver identity.
		 */
		if (pdsc->dev_ident.version < PDS_CORE_IDENTITY_VERSION_2)
			pdsc->dev_ident.capabilities = 0;
	}
	mutex_unlock(&pdsc->devcmd_lock);

	if (err)
		return err;

	if (isprint(pdsc->dev_info.fw_version[0]) &&
	    isascii(pdsc->dev_info.fw_version[0]))
		dev_info(pdsc->dev, "FW: %.*s\n",
			 (int)(sizeof(pdsc->dev_info.fw_version) - 1),
			 pdsc->dev_info.fw_version);
	else
		dev_info(pdsc->dev, "FW: (invalid string) 0x%02x 0x%02x 0x%02x 0x%02x ...\n",
			 (u8)pdsc->dev_info.fw_version[0],
			 (u8)pdsc->dev_info.fw_version[1],
			 (u8)pdsc->dev_info.fw_version[2],
			 (u8)pdsc->dev_info.fw_version[3]);

	return 0;
}

static int pdsc_identify(struct pdsc *pdsc)
{
	int err;

	/* Older firmware rejects anything but PDS_CORE_IDENTITY_VERSION_1
	 * with PDS_RC_EVERSION (-EINVAL), so retry with V1 on version
	 * rejection. Don't retry on other errors like -ENXIO/-ETIMEDOUT
	 * which indicate firmware is not running or hung.
	 */
	err = pdsc_identify_ver(pdsc, PDS_CORE_IDENTITY_VERSION_2);
	if (err == -EINVAL)
		err = pdsc_identify_ver(pdsc, PDS_CORE_IDENTITY_VERSION_1);

	if (err)
		dev_err(pdsc->dev, "Cannot identify device: %pe\n",
			ERR_PTR(err));

	return err;
}

void pdsc_dev_uninit(struct pdsc *pdsc)
{
	if (pdsc->intr_info) {
		int i;

		for (i = 0; i < pdsc->nintrs; i++)
			pdsc_intr_free(pdsc, i);

		kfree(pdsc->intr_info);
		pdsc->intr_info = NULL;
		pdsc->nintrs = 0;
	}

	pci_free_irq_vectors(pdsc->pdev);
}

int pdsc_dev_init(struct pdsc *pdsc)
{
	unsigned int nintrs;
	int err;

	/* Initial init and reset of device */
	pdsc_init_devinfo(pdsc);
	pdsc->devcmd_timeout = PDS_CORE_DEVCMD_TIMEOUT;

	err = pdsc_devcmd_reset(pdsc);
	if (err)
		return err;

	err = pdsc_identify(pdsc);
	if (err)
		return err;

	pdsc_debugfs_add_ident(pdsc);

	/* Now we can reserve interrupts */
	nintrs = le32_to_cpu(pdsc->dev_ident.nintrs);
	nintrs = min_t(unsigned int, num_online_cpus(), nintrs);

	/* Get intr_info struct array for tracking */
	pdsc->intr_info = kzalloc_objs(*pdsc->intr_info, nintrs);
	if (!pdsc->intr_info)
		return -ENOMEM;

	err = pci_alloc_irq_vectors(pdsc->pdev, nintrs, nintrs, PCI_IRQ_MSIX);
	if (err != nintrs) {
		dev_err(pdsc->dev, "Can't get %d intrs from OS: %pe\n",
			nintrs, ERR_PTR(err));
		err = -ENOSPC;
		goto err_out;
	}
	pdsc->nintrs = nintrs;

	return 0;

err_out:
	kfree(pdsc->intr_info);
	pdsc->intr_info = NULL;

	return err;
}

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
	return (pdsc->fw_status != 0xff) &&
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

static int pdsc_devcmd_wait(struct pdsc *pdsc, u8 opcode, int max_seconds)
{
	struct device *dev = pdsc->dev;
	unsigned long start_time;
	unsigned long max_wait;
	unsigned long duration;
	int timeout = 0;
	int done = 0;
	int err = 0;
	int status;

	start_time = jiffies;
	max_wait = start_time + (max_seconds * HZ);

	while (!done && !timeout) {
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

	if (!done || timeout) {
		dev_err(dev, "DEVCMD %d %s timeout, done %d timeout %d max_seconds=%d\n",
			opcode, pdsc_devcmd_str(opcode), done, timeout,
			max_seconds);
		err = -ETIMEDOUT;
		pdsc_devcmd_clean(pdsc);
	}

	status = pdsc_devcmd_status(pdsc);
	err = pdsc_err_to_errno(status);
	if (err && err != -EAGAIN)
		dev_err(dev, "DEVCMD %d %s failed, status=%d err %d %pe\n",
			opcode, pdsc_devcmd_str(opcode), status, err,
			ERR_PTR(err));

	return err;
}

int pdsc_devcmd_locked(struct pdsc *pdsc, union pds_core_dev_cmd *cmd,
		       union pds_core_dev_comp *comp, int max_seconds)
{
	int err;

	if (!pdsc->cmd_regs)
		return -ENXIO;

	memcpy_toio(&pdsc->cmd_regs->cmd, cmd, sizeof(*cmd));
	pdsc_devcmd_dbell(pdsc);
	err = pdsc_devcmd_wait(pdsc, cmd->opcode, max_seconds);

	if ((err == -ENXIO || err == -ETIMEDOUT) && pdsc->wq)
		queue_work(pdsc->wq, &pdsc->health_work);
	else
		memcpy_fromio(comp, &pdsc->cmd_regs->comp, sizeof(*comp));

	return err;
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

	return pdsc_devcmd(pdsc, &cmd, &comp, pdsc->devcmd_timeout);
}

static int pdsc_devcmd_identify_locked(struct pdsc *pdsc)
{
	union pds_core_dev_comp comp = {};
	union pds_core_dev_cmd cmd = {
		.identify.opcode = PDS_CORE_CMD_IDENTIFY,
		.identify.ver = PDS_CORE_IDENTITY_VERSION_1,
	};

	return pdsc_devcmd_locked(pdsc, &cmd, &comp, pdsc->devcmd_timeout);
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

static int pdsc_identify(struct pdsc *pdsc)
{
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

	err = pdsc_devcmd_identify_locked(pdsc);
	if (!err) {
		sz = min_t(size_t, sizeof(pdsc->dev_ident),
			   sizeof(pdsc->cmd_regs->data));
		memcpy_fromio(&pdsc->dev_ident, &pdsc->cmd_regs->data, sz);
	}
	mutex_unlock(&pdsc->devcmd_lock);

	if (err) {
		dev_err(pdsc->dev, "Cannot identify device: %pe\n",
			ERR_PTR(err));
		return err;
	}

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
	pdsc->intr_info = kcalloc(nintrs, sizeof(*pdsc->intr_info), GFP_KERNEL);
	if (!pdsc->intr_info) {
		err = -ENOMEM;
		goto err_out;
	}

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

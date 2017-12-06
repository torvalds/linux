/*
 * Copyright (C) 2014 Intel Corporation
 *
 * Authors:
 * Jarkko Sakkinen <jarkko.sakkinen@linux.intel.com>
 *
 * Maintained by: <tpmdd-devel@lists.sourceforge.net>
 *
 * This device driver implements the TPM interface as defined in
 * the TCG CRB 2.0 TPM specification.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

#include <linux/acpi.h>
#include <linux/highmem.h>
#include <linux/rculist.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#ifdef CONFIG_ARM64
#include <linux/arm-smccc.h>
#endif
#include "tpm.h"

#define ACPI_SIG_TPM2 "TPM2"

static const guid_t crb_acpi_start_guid =
	GUID_INIT(0x6BBF6CAB, 0x5463, 0x4714,
		  0xB7, 0xCD, 0xF0, 0x20, 0x3C, 0x03, 0x68, 0xD4);

enum crb_defaults {
	CRB_ACPI_START_REVISION_ID = 1,
	CRB_ACPI_START_INDEX = 1,
};

enum crb_loc_ctrl {
	CRB_LOC_CTRL_REQUEST_ACCESS	= BIT(0),
	CRB_LOC_CTRL_RELINQUISH		= BIT(1),
};

enum crb_loc_state {
	CRB_LOC_STATE_LOC_ASSIGNED	= BIT(1),
	CRB_LOC_STATE_TPM_REG_VALID_STS	= BIT(7),
};

enum crb_ctrl_req {
	CRB_CTRL_REQ_CMD_READY	= BIT(0),
	CRB_CTRL_REQ_GO_IDLE	= BIT(1),
};

enum crb_ctrl_sts {
	CRB_CTRL_STS_ERROR	= BIT(0),
	CRB_CTRL_STS_TPM_IDLE	= BIT(1),
};

enum crb_start {
	CRB_START_INVOKE	= BIT(0),
};

enum crb_cancel {
	CRB_CANCEL_INVOKE	= BIT(0),
};

struct crb_regs_head {
	u32 loc_state;
	u32 reserved1;
	u32 loc_ctrl;
	u32 loc_sts;
	u8 reserved2[32];
	u64 intf_id;
	u64 ctrl_ext;
} __packed;

struct crb_regs_tail {
	u32 ctrl_req;
	u32 ctrl_sts;
	u32 ctrl_cancel;
	u32 ctrl_start;
	u32 ctrl_int_enable;
	u32 ctrl_int_sts;
	u32 ctrl_cmd_size;
	u32 ctrl_cmd_pa_low;
	u32 ctrl_cmd_pa_high;
	u32 ctrl_rsp_size;
	u64 ctrl_rsp_pa;
} __packed;

enum crb_status {
	CRB_DRV_STS_COMPLETE	= BIT(0),
};

enum crb_flags {
	CRB_FL_ACPI_START	= BIT(0),
	CRB_FL_CRB_START	= BIT(1),
	CRB_FL_CRB_SMC_START	= BIT(2),
};

struct crb_priv {
	unsigned int flags;
	void __iomem *iobase;
	struct crb_regs_head __iomem *regs_h;
	struct crb_regs_tail __iomem *regs_t;
	u8 __iomem *cmd;
	u8 __iomem *rsp;
	u32 cmd_size;
	u32 smc_func_id;
};

struct tpm2_crb_smc {
	u32 interrupt;
	u8 interrupt_flags;
	u8 op_flags;
	u16 reserved2;
	u32 smc_func_id;
};

/**
 * crb_go_idle - request tpm crb device to go the idle state
 *
 * @dev:  crb device
 * @priv: crb private data
 *
 * Write CRB_CTRL_REQ_GO_IDLE to TPM_CRB_CTRL_REQ
 * The device should respond within TIMEOUT_C by clearing the bit.
 * Anyhow, we do not wait here as a consequent CMD_READY request
 * will be handled correctly even if idle was not completed.
 *
 * The function does nothing for devices with ACPI-start method.
 *
 * Return: 0 always
 */
static int __maybe_unused crb_go_idle(struct device *dev, struct crb_priv *priv)
{
	if ((priv->flags & CRB_FL_ACPI_START) ||
	    (priv->flags & CRB_FL_CRB_SMC_START))
		return 0;

	iowrite32(CRB_CTRL_REQ_GO_IDLE, &priv->regs_t->ctrl_req);
	/* we don't really care when this settles */

	return 0;
}

static bool crb_wait_for_reg_32(u32 __iomem *reg, u32 mask, u32 value,
				unsigned long timeout)
{
	ktime_t start;
	ktime_t stop;

	start = ktime_get();
	stop = ktime_add(start, ms_to_ktime(timeout));

	do {
		if ((ioread32(reg) & mask) == value)
			return true;

		usleep_range(50, 100);
	} while (ktime_before(ktime_get(), stop));

	return false;
}

/**
 * crb_cmd_ready - request tpm crb device to enter ready state
 *
 * @dev:  crb device
 * @priv: crb private data
 *
 * Write CRB_CTRL_REQ_CMD_READY to TPM_CRB_CTRL_REQ
 * and poll till the device acknowledge it by clearing the bit.
 * The device should respond within TIMEOUT_C.
 *
 * The function does nothing for devices with ACPI-start method
 *
 * Return: 0 on success -ETIME on timeout;
 */
static int __maybe_unused crb_cmd_ready(struct device *dev,
					struct crb_priv *priv)
{
	if ((priv->flags & CRB_FL_ACPI_START) ||
	    (priv->flags & CRB_FL_CRB_SMC_START))
		return 0;

	iowrite32(CRB_CTRL_REQ_CMD_READY, &priv->regs_t->ctrl_req);
	if (!crb_wait_for_reg_32(&priv->regs_t->ctrl_req,
				 CRB_CTRL_REQ_CMD_READY /* mask */,
				 0, /* value */
				 TPM2_TIMEOUT_C)) {
		dev_warn(dev, "cmdReady timed out\n");
		return -ETIME;
	}

	return 0;
}

static int crb_request_locality(struct tpm_chip *chip, int loc)
{
	struct crb_priv *priv = dev_get_drvdata(&chip->dev);
	u32 value = CRB_LOC_STATE_LOC_ASSIGNED |
		CRB_LOC_STATE_TPM_REG_VALID_STS;

	if (!priv->regs_h)
		return 0;

	iowrite32(CRB_LOC_CTRL_REQUEST_ACCESS, &priv->regs_h->loc_ctrl);
	if (!crb_wait_for_reg_32(&priv->regs_h->loc_state, value, value,
				 TPM2_TIMEOUT_C)) {
		dev_warn(&chip->dev, "TPM_LOC_STATE_x.requestAccess timed out\n");
		return -ETIME;
	}

	return 0;
}

static void crb_relinquish_locality(struct tpm_chip *chip, int loc)
{
	struct crb_priv *priv = dev_get_drvdata(&chip->dev);

	if (!priv->regs_h)
		return;

	iowrite32(CRB_LOC_CTRL_RELINQUISH, &priv->regs_h->loc_ctrl);
}

static u8 crb_status(struct tpm_chip *chip)
{
	struct crb_priv *priv = dev_get_drvdata(&chip->dev);
	u8 sts = 0;

	if ((ioread32(&priv->regs_t->ctrl_start) & CRB_START_INVOKE) !=
	    CRB_START_INVOKE)
		sts |= CRB_DRV_STS_COMPLETE;

	return sts;
}

static int crb_recv(struct tpm_chip *chip, u8 *buf, size_t count)
{
	struct crb_priv *priv = dev_get_drvdata(&chip->dev);
	unsigned int expected;

	/* sanity check */
	if (count < 6)
		return -EIO;

	if (ioread32(&priv->regs_t->ctrl_sts) & CRB_CTRL_STS_ERROR)
		return -EIO;

	memcpy_fromio(buf, priv->rsp, 6);
	expected = be32_to_cpup((__be32 *) &buf[2]);
	if (expected > count || expected < 6)
		return -EIO;

	memcpy_fromio(&buf[6], &priv->rsp[6], expected - 6);

	return expected;
}

static int crb_do_acpi_start(struct tpm_chip *chip)
{
	union acpi_object *obj;
	int rc;

	obj = acpi_evaluate_dsm(chip->acpi_dev_handle,
				&crb_acpi_start_guid,
				CRB_ACPI_START_REVISION_ID,
				CRB_ACPI_START_INDEX,
				NULL);
	if (!obj)
		return -ENXIO;
	rc = obj->integer.value == 0 ? 0 : -ENXIO;
	ACPI_FREE(obj);
	return rc;
}

#ifdef CONFIG_ARM64
/*
 * This is a TPM Command Response Buffer start method that invokes a
 * Secure Monitor Call to requrest the firmware to execute or cancel
 * a TPM 2.0 command.
 */
static int tpm_crb_smc_start(struct device *dev, unsigned long func_id)
{
	struct arm_smccc_res res;

	arm_smccc_smc(func_id, 0, 0, 0, 0, 0, 0, 0, &res);
	if (res.a0 != 0) {
		dev_err(dev,
			FW_BUG "tpm_crb_smc_start() returns res.a0 = 0x%lx\n",
			res.a0);
		return -EIO;
	}

	return 0;
}
#else
static int tpm_crb_smc_start(struct device *dev, unsigned long func_id)
{
	dev_err(dev, FW_BUG "tpm_crb: incorrect start method\n");
	return -EINVAL;
}
#endif

static int crb_send(struct tpm_chip *chip, u8 *buf, size_t len)
{
	struct crb_priv *priv = dev_get_drvdata(&chip->dev);
	int rc = 0;

	/* Zero the cancel register so that the next command will not get
	 * canceled.
	 */
	iowrite32(0, &priv->regs_t->ctrl_cancel);

	if (len > priv->cmd_size) {
		dev_err(&chip->dev, "invalid command count value %zd %d\n",
			len, priv->cmd_size);
		return -E2BIG;
	}

	memcpy_toio(priv->cmd, buf, len);

	/* Make sure that cmd is populated before issuing start. */
	wmb();

	if (priv->flags & CRB_FL_CRB_START)
		iowrite32(CRB_START_INVOKE, &priv->regs_t->ctrl_start);

	if (priv->flags & CRB_FL_ACPI_START)
		rc = crb_do_acpi_start(chip);

	if (priv->flags & CRB_FL_CRB_SMC_START) {
		iowrite32(CRB_START_INVOKE, &priv->regs_t->ctrl_start);
		rc = tpm_crb_smc_start(&chip->dev, priv->smc_func_id);
	}

	return rc;
}

static void crb_cancel(struct tpm_chip *chip)
{
	struct crb_priv *priv = dev_get_drvdata(&chip->dev);

	iowrite32(CRB_CANCEL_INVOKE, &priv->regs_t->ctrl_cancel);

	if ((priv->flags & CRB_FL_ACPI_START) && crb_do_acpi_start(chip))
		dev_err(&chip->dev, "ACPI Start failed\n");
}

static bool crb_req_canceled(struct tpm_chip *chip, u8 status)
{
	struct crb_priv *priv = dev_get_drvdata(&chip->dev);
	u32 cancel = ioread32(&priv->regs_t->ctrl_cancel);

	return (cancel & CRB_CANCEL_INVOKE) == CRB_CANCEL_INVOKE;
}

static const struct tpm_class_ops tpm_crb = {
	.flags = TPM_OPS_AUTO_STARTUP,
	.status = crb_status,
	.recv = crb_recv,
	.send = crb_send,
	.cancel = crb_cancel,
	.req_canceled = crb_req_canceled,
	.request_locality = crb_request_locality,
	.relinquish_locality = crb_relinquish_locality,
	.req_complete_mask = CRB_DRV_STS_COMPLETE,
	.req_complete_val = CRB_DRV_STS_COMPLETE,
};

static int crb_check_resource(struct acpi_resource *ares, void *data)
{
	struct resource *io_res = data;
	struct resource_win win;
	struct resource *res = &(win.res);

	if (acpi_dev_resource_memory(ares, res) ||
	    acpi_dev_resource_address_space(ares, &win)) {
		*io_res = *res;
		io_res->name = NULL;
	}

	return 1;
}

static void __iomem *crb_map_res(struct device *dev, struct crb_priv *priv,
				 struct resource *io_res, u64 start, u32 size)
{
	struct resource new_res = {
		.start	= start,
		.end	= start + size - 1,
		.flags	= IORESOURCE_MEM,
	};

	/* Detect a 64 bit address on a 32 bit system */
	if (start != new_res.start)
		return (void __iomem *) ERR_PTR(-EINVAL);

	if (!resource_contains(io_res, &new_res))
		return devm_ioremap_resource(dev, &new_res);

	return priv->iobase + (new_res.start - io_res->start);
}

/*
 * Work around broken BIOSs that return inconsistent values from the ACPI
 * region vs the registers. Trust the ACPI region. Such broken systems
 * probably cannot send large TPM commands since the buffer will be truncated.
 */
static u64 crb_fixup_cmd_size(struct device *dev, struct resource *io_res,
			      u64 start, u64 size)
{
	if (io_res->start > start || io_res->end < start)
		return size;

	if (start + size - 1 <= io_res->end)
		return size;

	dev_err(dev,
		FW_BUG "ACPI region does not cover the entire command/response buffer. %pr vs %llx %llx\n",
		io_res, start, size);

	return io_res->end - start + 1;
}

static int crb_map_io(struct acpi_device *device, struct crb_priv *priv,
		      struct acpi_table_tpm2 *buf)
{
	struct list_head resources;
	struct resource io_res;
	struct device *dev = &device->dev;
	u32 pa_high, pa_low;
	u64 cmd_pa;
	u32 cmd_size;
	u64 rsp_pa;
	u32 rsp_size;
	int ret;

	INIT_LIST_HEAD(&resources);
	ret = acpi_dev_get_resources(device, &resources, crb_check_resource,
				     &io_res);
	if (ret < 0)
		return ret;
	acpi_dev_free_resource_list(&resources);

	if (resource_type(&io_res) != IORESOURCE_MEM) {
		dev_err(dev, FW_BUG "TPM2 ACPI table does not define a memory resource\n");
		return -EINVAL;
	}

	priv->iobase = devm_ioremap_resource(dev, &io_res);
	if (IS_ERR(priv->iobase))
		return PTR_ERR(priv->iobase);

	/* The ACPI IO region starts at the head area and continues to include
	 * the control area, as one nice sane region except for some older
	 * stuff that puts the control area outside the ACPI IO region.
	 */
	if (!(priv->flags & CRB_FL_ACPI_START)) {
		if (buf->control_address == io_res.start +
		    sizeof(*priv->regs_h))
			priv->regs_h = priv->iobase;
		else
			dev_warn(dev, FW_BUG "Bad ACPI memory layout");
	}

	priv->regs_t = crb_map_res(dev, priv, &io_res, buf->control_address,
				   sizeof(struct crb_regs_tail));
	if (IS_ERR(priv->regs_t))
		return PTR_ERR(priv->regs_t);

	/*
	 * PTT HW bug w/a: wake up the device to access
	 * possibly not retained registers.
	 */
	ret = crb_cmd_ready(dev, priv);
	if (ret)
		return ret;

	pa_high = ioread32(&priv->regs_t->ctrl_cmd_pa_high);
	pa_low  = ioread32(&priv->regs_t->ctrl_cmd_pa_low);
	cmd_pa = ((u64)pa_high << 32) | pa_low;
	cmd_size = crb_fixup_cmd_size(dev, &io_res, cmd_pa,
				      ioread32(&priv->regs_t->ctrl_cmd_size));

	dev_dbg(dev, "cmd_hi = %X cmd_low = %X cmd_size %X\n",
		pa_high, pa_low, cmd_size);

	priv->cmd = crb_map_res(dev, priv, &io_res, cmd_pa, cmd_size);
	if (IS_ERR(priv->cmd)) {
		ret = PTR_ERR(priv->cmd);
		goto out;
	}

	memcpy_fromio(&rsp_pa, &priv->regs_t->ctrl_rsp_pa, 8);
	rsp_pa = le64_to_cpu(rsp_pa);
	rsp_size = crb_fixup_cmd_size(dev, &io_res, rsp_pa,
				      ioread32(&priv->regs_t->ctrl_rsp_size));

	if (cmd_pa != rsp_pa) {
		priv->rsp = crb_map_res(dev, priv, &io_res, rsp_pa, rsp_size);
		ret = PTR_ERR_OR_ZERO(priv->rsp);
		goto out;
	}

	/* According to the PTP specification, overlapping command and response
	 * buffer sizes must be identical.
	 */
	if (cmd_size != rsp_size) {
		dev_err(dev, FW_BUG "overlapping command and response buffer sizes are not identical");
		ret = -EINVAL;
		goto out;
	}

	priv->rsp = priv->cmd;

out:
	if (!ret)
		priv->cmd_size = cmd_size;

	crb_go_idle(dev, priv);

	return ret;
}

static int crb_acpi_add(struct acpi_device *device)
{
	struct acpi_table_tpm2 *buf;
	struct crb_priv *priv;
	struct tpm_chip *chip;
	struct device *dev = &device->dev;
	struct tpm2_crb_smc *crb_smc;
	acpi_status status;
	u32 sm;
	int rc;

	status = acpi_get_table(ACPI_SIG_TPM2, 1,
				(struct acpi_table_header **) &buf);
	if (ACPI_FAILURE(status) || buf->header.length < sizeof(*buf)) {
		dev_err(dev, FW_BUG "failed to get TPM2 ACPI table\n");
		return -EINVAL;
	}

	/* Should the FIFO driver handle this? */
	sm = buf->start_method;
	if (sm == ACPI_TPM2_MEMORY_MAPPED)
		return -ENODEV;

	priv = devm_kzalloc(dev, sizeof(struct crb_priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	/* The reason for the extra quirk is that the PTT in 4th Gen Core CPUs
	 * report only ACPI start but in practice seems to require both
	 * ACPI start and CRB start.
	 */
	if (sm == ACPI_TPM2_COMMAND_BUFFER || sm == ACPI_TPM2_MEMORY_MAPPED ||
	    !strcmp(acpi_device_hid(device), "MSFT0101"))
		priv->flags |= CRB_FL_CRB_START;

	if (sm == ACPI_TPM2_START_METHOD ||
	    sm == ACPI_TPM2_COMMAND_BUFFER_WITH_START_METHOD)
		priv->flags |= CRB_FL_ACPI_START;

	if (sm == ACPI_TPM2_COMMAND_BUFFER_WITH_ARM_SMC) {
		if (buf->header.length < (sizeof(*buf) + sizeof(*crb_smc))) {
			dev_err(dev,
				FW_BUG "TPM2 ACPI table has wrong size %u for start method type %d\n",
				buf->header.length,
				ACPI_TPM2_COMMAND_BUFFER_WITH_ARM_SMC);
			return -EINVAL;
		}
		crb_smc = ACPI_ADD_PTR(struct tpm2_crb_smc, buf, sizeof(*buf));
		priv->smc_func_id = crb_smc->smc_func_id;
		priv->flags |= CRB_FL_CRB_SMC_START;
	}

	rc = crb_map_io(device, priv, buf);
	if (rc)
		return rc;

	chip = tpmm_chip_alloc(dev, &tpm_crb);
	if (IS_ERR(chip))
		return PTR_ERR(chip);

	dev_set_drvdata(&chip->dev, priv);
	chip->acpi_dev_handle = device->handle;
	chip->flags = TPM_CHIP_FLAG_TPM2;

	rc  = crb_cmd_ready(dev, priv);
	if (rc)
		return rc;

	pm_runtime_get_noresume(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	rc = tpm_chip_register(chip);
	if (rc) {
		crb_go_idle(dev, priv);
		pm_runtime_put_noidle(dev);
		pm_runtime_disable(dev);
		return rc;
	}

	pm_runtime_put(dev);

	return 0;
}

static int crb_acpi_remove(struct acpi_device *device)
{
	struct device *dev = &device->dev;
	struct tpm_chip *chip = dev_get_drvdata(dev);

	tpm_chip_unregister(chip);

	pm_runtime_disable(dev);

	return 0;
}

static int __maybe_unused crb_pm_runtime_suspend(struct device *dev)
{
	struct tpm_chip *chip = dev_get_drvdata(dev);
	struct crb_priv *priv = dev_get_drvdata(&chip->dev);

	return crb_go_idle(dev, priv);
}

static int __maybe_unused crb_pm_runtime_resume(struct device *dev)
{
	struct tpm_chip *chip = dev_get_drvdata(dev);
	struct crb_priv *priv = dev_get_drvdata(&chip->dev);

	return crb_cmd_ready(dev, priv);
}

static int __maybe_unused crb_pm_suspend(struct device *dev)
{
	int ret;

	ret = tpm_pm_suspend(dev);
	if (ret)
		return ret;

	return crb_pm_runtime_suspend(dev);
}

static int __maybe_unused crb_pm_resume(struct device *dev)
{
	int ret;

	ret = crb_pm_runtime_resume(dev);
	if (ret)
		return ret;

	return tpm_pm_resume(dev);
}

static const struct dev_pm_ops crb_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(crb_pm_suspend, crb_pm_resume)
	SET_RUNTIME_PM_OPS(crb_pm_runtime_suspend, crb_pm_runtime_resume, NULL)
};

static const struct acpi_device_id crb_device_ids[] = {
	{"MSFT0101", 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, crb_device_ids);

static struct acpi_driver crb_acpi_driver = {
	.name = "tpm_crb",
	.ids = crb_device_ids,
	.ops = {
		.add = crb_acpi_add,
		.remove = crb_acpi_remove,
	},
	.drv = {
		.pm = &crb_pm,
	},
};

module_acpi_driver(crb_acpi_driver);
MODULE_AUTHOR("Jarkko Sakkinen <jarkko.sakkinen@linux.intel.com>");
MODULE_DESCRIPTION("TPM2 Driver");
MODULE_VERSION("0.1");
MODULE_LICENSE("GPL");

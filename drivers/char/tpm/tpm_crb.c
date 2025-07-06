// SPDX-License-Identifier: GPL-2.0-only
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
 */

#include <linux/acpi.h>
#include <linux/highmem.h>
#include <linux/rculist.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#ifdef CONFIG_ARM64
#include <linux/arm-smccc.h>
#endif
#include "tpm_crb_ffa.h"
#include "tpm.h"

#define ACPI_SIG_TPM2 "TPM2"
#define TPM_CRB_MAX_RESOURCES 3

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

struct crb_priv {
	u32 sm;
	const char *hid;
	struct crb_regs_head __iomem *regs_h;
	struct crb_regs_tail __iomem *regs_t;
	u8 __iomem *cmd;
	u8 __iomem *rsp;
	u32 cmd_size;
	u32 smc_func_id;
	u32 __iomem *pluton_start_addr;
	u32 __iomem *pluton_reply_addr;
	u8 ffa_flags;
	u8 ffa_attributes;
};

struct tpm2_crb_smc {
	u32 interrupt;
	u8 interrupt_flags;
	u8 op_flags;
	u16 reserved2;
	u32 smc_func_id;
};

/* CRB over FFA start method parameters in TCG2 ACPI table */
struct tpm2_crb_ffa {
	u8 flags;
	u8 attributes;
	u16 partition_id;
	u8 reserved[8];
};

struct tpm2_crb_pluton {
	u64 start_addr;
	u64 reply_addr;
};

/*
 * Returns true if the start method supports idle.
 */
static inline bool tpm_crb_has_idle(u32 start_method)
{
	return !(start_method == ACPI_TPM2_START_METHOD ||
	       start_method == ACPI_TPM2_COMMAND_BUFFER_WITH_START_METHOD ||
	       start_method == ACPI_TPM2_COMMAND_BUFFER_WITH_ARM_SMC ||
	       start_method == ACPI_TPM2_CRB_WITH_ARM_FFA);
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

	return ((ioread32(reg) & mask) == value);
}

static int crb_try_pluton_doorbell(struct crb_priv *priv, bool wait_for_complete)
{
	if (priv->sm != ACPI_TPM2_COMMAND_BUFFER_WITH_PLUTON)
		return 0;

	if (!crb_wait_for_reg_32(priv->pluton_reply_addr, ~0, 1, TPM2_TIMEOUT_C))
		return -ETIME;

	iowrite32(1, priv->pluton_start_addr);
	if (wait_for_complete == false)
		return 0;

	if (!crb_wait_for_reg_32(priv->pluton_start_addr,
				 0xffffffff, 0, 200))
		return -ETIME;

	return 0;
}

/**
 * __crb_go_idle - request tpm crb device to go the idle state
 *
 * @dev:  crb device
 * @priv: crb private data
 *
 * Write CRB_CTRL_REQ_GO_IDLE to TPM_CRB_CTRL_REQ
 * The device should respond within TIMEOUT_C by clearing the bit.
 * Anyhow, we do not wait here as a consequent CMD_READY request
 * will be handled correctly even if idle was not completed.
 *
 * The function does nothing for devices with ACPI-start method
 * or SMC-start method.
 *
 * Return: 0 always
 */
static int __crb_go_idle(struct device *dev, struct crb_priv *priv)
{
	int rc;

	if (!tpm_crb_has_idle(priv->sm))
		return 0;

	iowrite32(CRB_CTRL_REQ_GO_IDLE, &priv->regs_t->ctrl_req);

	rc = crb_try_pluton_doorbell(priv, true);
	if (rc)
		return rc;

	if (!crb_wait_for_reg_32(&priv->regs_t->ctrl_req,
				 CRB_CTRL_REQ_GO_IDLE/* mask */,
				 0, /* value */
				 TPM2_TIMEOUT_C)) {
		dev_warn(dev, "goIdle timed out\n");
		return -ETIME;
	}

	return 0;
}

static int crb_go_idle(struct tpm_chip *chip)
{
	struct device *dev = &chip->dev;
	struct crb_priv *priv = dev_get_drvdata(dev);

	return __crb_go_idle(dev, priv);
}

/**
 * __crb_cmd_ready - request tpm crb device to enter ready state
 *
 * @dev:  crb device
 * @priv: crb private data
 *
 * Write CRB_CTRL_REQ_CMD_READY to TPM_CRB_CTRL_REQ
 * and poll till the device acknowledge it by clearing the bit.
 * The device should respond within TIMEOUT_C.
 *
 * The function does nothing for devices with ACPI-start method
 * or SMC-start method.
 *
 * Return: 0 on success -ETIME on timeout;
 */
static int __crb_cmd_ready(struct device *dev, struct crb_priv *priv)
{
	int rc;

	if (!tpm_crb_has_idle(priv->sm))
		return 0;

	iowrite32(CRB_CTRL_REQ_CMD_READY, &priv->regs_t->ctrl_req);

	rc = crb_try_pluton_doorbell(priv, true);
	if (rc)
		return rc;

	if (!crb_wait_for_reg_32(&priv->regs_t->ctrl_req,
				 CRB_CTRL_REQ_CMD_READY /* mask */,
				 0, /* value */
				 TPM2_TIMEOUT_C)) {
		dev_warn(dev, "cmdReady timed out\n");
		return -ETIME;
	}

	return 0;
}

static int crb_cmd_ready(struct tpm_chip *chip)
{
	struct device *dev = &chip->dev;
	struct crb_priv *priv = dev_get_drvdata(dev);

	return __crb_cmd_ready(dev, priv);
}

static int __crb_request_locality(struct device *dev,
				  struct crb_priv *priv, int loc)
{
	u32 value = CRB_LOC_STATE_LOC_ASSIGNED | CRB_LOC_STATE_TPM_REG_VALID_STS;
	int rc;

	if (!priv->regs_h)
		return 0;

	iowrite32(CRB_LOC_CTRL_REQUEST_ACCESS, &priv->regs_h->loc_ctrl);

	if (priv->sm == ACPI_TPM2_CRB_WITH_ARM_FFA) {
		rc = tpm_crb_ffa_start(CRB_FFA_START_TYPE_LOCALITY_REQUEST, loc);
		if (rc)
			return rc;
	}

	if (!crb_wait_for_reg_32(&priv->regs_h->loc_state, value, value,
				 TPM2_TIMEOUT_C)) {
		dev_warn(dev, "TPM_LOC_STATE_x.requestAccess timed out\n");
		return -ETIME;
	}

	return 0;
}

static int crb_request_locality(struct tpm_chip *chip, int loc)
{
	struct crb_priv *priv = dev_get_drvdata(&chip->dev);

	return __crb_request_locality(&chip->dev, priv, loc);
}

static int __crb_relinquish_locality(struct device *dev,
				     struct crb_priv *priv, int loc)
{
	u32 mask = CRB_LOC_STATE_LOC_ASSIGNED | CRB_LOC_STATE_TPM_REG_VALID_STS;
	u32 value = CRB_LOC_STATE_TPM_REG_VALID_STS;
	int rc;

	if (!priv->regs_h)
		return 0;

	iowrite32(CRB_LOC_CTRL_RELINQUISH, &priv->regs_h->loc_ctrl);

	if (priv->sm == ACPI_TPM2_CRB_WITH_ARM_FFA) {
		rc = tpm_crb_ffa_start(CRB_FFA_START_TYPE_LOCALITY_REQUEST, loc);
		if (rc)
			return rc;
	}

	if (!crb_wait_for_reg_32(&priv->regs_h->loc_state, mask, value,
				 TPM2_TIMEOUT_C)) {
		dev_warn(dev, "TPM_LOC_STATE_x.Relinquish timed out\n");
		return -ETIME;
	}

	return 0;
}

static int crb_relinquish_locality(struct tpm_chip *chip, int loc)
{
	struct crb_priv *priv = dev_get_drvdata(&chip->dev);

	return __crb_relinquish_locality(&chip->dev, priv, loc);
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

	/* A sanity check that the upper layer wants to get at least the header
	 * as that is the minimum size for any TPM response.
	 */
	if (count < TPM_HEADER_SIZE)
		return -EIO;

	/* If this bit is set, according to the spec, the TPM is in
	 * unrecoverable condition.
	 */
	if (ioread32(&priv->regs_t->ctrl_sts) & CRB_CTRL_STS_ERROR)
		return -EIO;

	/* Read the first 8 bytes in order to get the length of the response.
	 * We read exactly a quad word in order to make sure that the remaining
	 * reads will be aligned.
	 */
	memcpy_fromio(buf, priv->rsp, 8);

	expected = be32_to_cpup((__be32 *)&buf[2]);
	if (expected > count || expected < TPM_HEADER_SIZE)
		return -EIO;

	memcpy_fromio(&buf[8], &priv->rsp[8], expected - 8);

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

	/* Seems to be necessary for every command */
	if (priv->sm == ACPI_TPM2_COMMAND_BUFFER_WITH_PLUTON)
		__crb_cmd_ready(&chip->dev, priv);

	memcpy_toio(priv->cmd, buf, len);

	/* Make sure that cmd is populated before issuing start. */
	wmb();

	/* The reason for the extra quirk is that the PTT in 4th Gen Core CPUs
	 * report only ACPI start but in practice seems to require both
	 * CRB start, hence invoking CRB start method if hid == MSFT0101.
	 */
	if (priv->sm == ACPI_TPM2_COMMAND_BUFFER ||
	    priv->sm == ACPI_TPM2_MEMORY_MAPPED ||
	    !strcmp(priv->hid, "MSFT0101"))
		iowrite32(CRB_START_INVOKE, &priv->regs_t->ctrl_start);

	if (priv->sm == ACPI_TPM2_START_METHOD ||
	    priv->sm == ACPI_TPM2_COMMAND_BUFFER_WITH_START_METHOD)
		rc = crb_do_acpi_start(chip);

	if (priv->sm == ACPI_TPM2_COMMAND_BUFFER_WITH_ARM_SMC) {
		iowrite32(CRB_START_INVOKE, &priv->regs_t->ctrl_start);
		rc = tpm_crb_smc_start(&chip->dev, priv->smc_func_id);
	}

	if (priv->sm == ACPI_TPM2_CRB_WITH_ARM_FFA) {
		iowrite32(CRB_START_INVOKE, &priv->regs_t->ctrl_start);
		rc = tpm_crb_ffa_start(CRB_FFA_START_TYPE_COMMAND, chip->locality);
	}

	if (rc)
		return rc;

	return crb_try_pluton_doorbell(priv, false);
}

static void crb_cancel(struct tpm_chip *chip)
{
	struct crb_priv *priv = dev_get_drvdata(&chip->dev);
	int rc;

	iowrite32(CRB_CANCEL_INVOKE, &priv->regs_t->ctrl_cancel);

	if ((priv->sm == ACPI_TPM2_START_METHOD ||
	     priv->sm == ACPI_TPM2_COMMAND_BUFFER_WITH_START_METHOD) &&
	     crb_do_acpi_start(chip))
		dev_err(&chip->dev, "ACPI Start failed\n");

	if (priv->sm == ACPI_TPM2_CRB_WITH_ARM_FFA) {
		rc = tpm_crb_ffa_start(CRB_FFA_START_TYPE_COMMAND, chip->locality);
		if (rc)
			dev_err(&chip->dev, "FF-A Start failed\n");
	}
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
	.go_idle  = crb_go_idle,
	.cmd_ready = crb_cmd_ready,
	.request_locality = crb_request_locality,
	.relinquish_locality = crb_relinquish_locality,
	.req_complete_mask = CRB_DRV_STS_COMPLETE,
	.req_complete_val = CRB_DRV_STS_COMPLETE,
};

static int crb_check_resource(struct acpi_resource *ares, void *data)
{
	struct resource *iores_array = data;
	struct resource_win win;
	struct resource *res = &(win.res);
	int i;

	if (acpi_dev_resource_memory(ares, res) ||
	    acpi_dev_resource_address_space(ares, &win)) {
		for (i = 0; i < TPM_CRB_MAX_RESOURCES + 1; ++i) {
			if (resource_type(iores_array + i) != IORESOURCE_MEM) {
				iores_array[i] = *res;
				iores_array[i].name = NULL;
				break;
			}
		}
	}

	return 1;
}

static void __iomem *crb_map_res(struct device *dev, struct resource *iores,
				 void __iomem **iobase_ptr, u64 start, u32 size)
{
	struct resource new_res = {
		.start	= start,
		.end	= start + size - 1,
		.flags	= IORESOURCE_MEM,
	};

	/* Detect a 64 bit address on a 32 bit system */
	if (start != new_res.start)
		return IOMEM_ERR_PTR(-EINVAL);

	if (!iores)
		return devm_ioremap_resource(dev, &new_res);

	if (!*iobase_ptr) {
		*iobase_ptr = devm_ioremap_resource(dev, iores);
		if (IS_ERR(*iobase_ptr))
			return *iobase_ptr;
	}

	return *iobase_ptr + (new_res.start - iores->start);
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
	struct list_head acpi_resource_list;
	struct resource iores_array[TPM_CRB_MAX_RESOURCES + 1] = { {0} };
	void __iomem *iobase_array[TPM_CRB_MAX_RESOURCES] = {NULL};
	struct device *dev = &device->dev;
	struct resource *iores;
	void __iomem **iobase_ptr;
	int i;
	u32 pa_high, pa_low;
	u64 cmd_pa;
	u32 cmd_size;
	__le64 __rsp_pa;
	u64 rsp_pa;
	u32 rsp_size;
	int ret;

	/*
	 * Pluton sometimes does not define ACPI memory regions.
	 * Mapping is then done in crb_map_pluton
	 */
	if (priv->sm != ACPI_TPM2_COMMAND_BUFFER_WITH_PLUTON) {
		INIT_LIST_HEAD(&acpi_resource_list);
		ret = acpi_dev_get_resources(device, &acpi_resource_list,
					     crb_check_resource, iores_array);
		if (ret < 0)
			return ret;
		acpi_dev_free_resource_list(&acpi_resource_list);

		if (resource_type(iores_array) != IORESOURCE_MEM) {
			dev_err(dev, FW_BUG "TPM2 ACPI table does not define a memory resource\n");
			return -EINVAL;
		} else if (resource_type(iores_array + TPM_CRB_MAX_RESOURCES) ==
			   IORESOURCE_MEM) {
			dev_warn(dev, "TPM2 ACPI table defines too many memory resources\n");
			memset(iores_array + TPM_CRB_MAX_RESOURCES,
			       0, sizeof(*iores_array));
			iores_array[TPM_CRB_MAX_RESOURCES].flags = 0;
		}
	}

	iores = NULL;
	iobase_ptr = NULL;
	for (i = 0; resource_type(iores_array + i) == IORESOURCE_MEM; ++i) {
		if (buf->control_address >= iores_array[i].start &&
		    buf->control_address + sizeof(struct crb_regs_tail) - 1 <=
		    iores_array[i].end) {
			iores = iores_array + i;
			iobase_ptr = iobase_array + i;
			break;
		}
	}

	priv->regs_t = crb_map_res(dev, iores, iobase_ptr, buf->control_address,
				   sizeof(struct crb_regs_tail));

	if (IS_ERR(priv->regs_t))
		return PTR_ERR(priv->regs_t);

	/* The ACPI IO region starts at the head area and continues to include
	 * the control area, as one nice sane region except for some older
	 * stuff that puts the control area outside the ACPI IO region.
	 */
	if (priv->sm == ACPI_TPM2_COMMAND_BUFFER ||
	    priv->sm == ACPI_TPM2_CRB_WITH_ARM_FFA ||
	    priv->sm == ACPI_TPM2_MEMORY_MAPPED) {
		if (iores &&
		    buf->control_address == iores->start +
		    sizeof(*priv->regs_h))
			priv->regs_h = *iobase_ptr;
		else
			dev_warn(dev, FW_BUG "Bad ACPI memory layout");
	}

	ret = __crb_request_locality(dev, priv, 0);
	if (ret)
		return ret;

	/*
	 * PTT HW bug w/a: wake up the device to access
	 * possibly not retained registers.
	 */
	ret = __crb_cmd_ready(dev, priv);
	if (ret)
		goto out_relinquish_locality;

	pa_high = ioread32(&priv->regs_t->ctrl_cmd_pa_high);
	pa_low  = ioread32(&priv->regs_t->ctrl_cmd_pa_low);
	cmd_pa = ((u64)pa_high << 32) | pa_low;
	cmd_size = ioread32(&priv->regs_t->ctrl_cmd_size);

	iores = NULL;
	iobase_ptr = NULL;
	for (i = 0; iores_array[i].end; ++i) {
		if (cmd_pa >= iores_array[i].start &&
		    cmd_pa <= iores_array[i].end) {
			iores = iores_array + i;
			iobase_ptr = iobase_array + i;
			break;
		}
	}

	if (iores)
		cmd_size = crb_fixup_cmd_size(dev, iores, cmd_pa, cmd_size);

	dev_dbg(dev, "cmd_hi = %X cmd_low = %X cmd_size %X\n",
		pa_high, pa_low, cmd_size);

	priv->cmd = crb_map_res(dev, iores, iobase_ptr,	cmd_pa, cmd_size);
	if (IS_ERR(priv->cmd)) {
		ret = PTR_ERR(priv->cmd);
		goto out;
	}

	memcpy_fromio(&__rsp_pa, &priv->regs_t->ctrl_rsp_pa, 8);
	rsp_pa = le64_to_cpu(__rsp_pa);
	rsp_size = ioread32(&priv->regs_t->ctrl_rsp_size);

	iores = NULL;
	iobase_ptr = NULL;
	for (i = 0; resource_type(iores_array + i) == IORESOURCE_MEM; ++i) {
		if (rsp_pa >= iores_array[i].start &&
		    rsp_pa <= iores_array[i].end) {
			iores = iores_array + i;
			iobase_ptr = iobase_array + i;
			break;
		}
	}

	if (iores)
		rsp_size = crb_fixup_cmd_size(dev, iores, rsp_pa, rsp_size);

	if (cmd_pa != rsp_pa) {
		priv->rsp = crb_map_res(dev, iores, iobase_ptr,
					rsp_pa, rsp_size);
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

	__crb_go_idle(dev, priv);

out_relinquish_locality:

	__crb_relinquish_locality(dev, priv, 0);

	return ret;
}

static int crb_map_pluton(struct device *dev, struct crb_priv *priv,
	       struct acpi_table_tpm2 *buf, struct tpm2_crb_pluton *crb_pluton)
{
	priv->pluton_start_addr = crb_map_res(dev, NULL, NULL,
					      crb_pluton->start_addr, 4);
	if (IS_ERR(priv->pluton_start_addr))
		return PTR_ERR(priv->pluton_start_addr);

	priv->pluton_reply_addr = crb_map_res(dev, NULL, NULL,
					      crb_pluton->reply_addr, 4);
	if (IS_ERR(priv->pluton_reply_addr))
		return PTR_ERR(priv->pluton_reply_addr);

	return 0;
}

static int crb_acpi_add(struct acpi_device *device)
{
	struct acpi_table_tpm2 *buf;
	struct crb_priv *priv;
	struct tpm_chip *chip;
	struct device *dev = &device->dev;
	struct tpm2_crb_smc *crb_smc;
	struct tpm2_crb_ffa *crb_ffa;
	struct tpm2_crb_pluton *crb_pluton;
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
	if (sm == ACPI_TPM2_MEMORY_MAPPED) {
		rc = -ENODEV;
		goto out;
	}

	priv = devm_kzalloc(dev, sizeof(struct crb_priv), GFP_KERNEL);
	if (!priv) {
		rc = -ENOMEM;
		goto out;
	}

	if (sm == ACPI_TPM2_COMMAND_BUFFER_WITH_ARM_SMC) {
		if (buf->header.length < (sizeof(*buf) + sizeof(*crb_smc))) {
			dev_err(dev,
				FW_BUG "TPM2 ACPI table has wrong size %u for start method type %d\n",
				buf->header.length,
				ACPI_TPM2_COMMAND_BUFFER_WITH_ARM_SMC);
			rc = -EINVAL;
			goto out;
		}
		crb_smc = ACPI_ADD_PTR(struct tpm2_crb_smc, buf, sizeof(*buf));
		priv->smc_func_id = crb_smc->smc_func_id;
	}

	if (sm == ACPI_TPM2_CRB_WITH_ARM_FFA) {
		if (buf->header.length < (sizeof(*buf) + sizeof(*crb_ffa))) {
			dev_err(dev,
				FW_BUG "TPM2 ACPI table has wrong size %u for start method type %d\n",
				buf->header.length,
				ACPI_TPM2_CRB_WITH_ARM_FFA);
			rc = -EINVAL;
			goto out;
		}
		crb_ffa = ACPI_ADD_PTR(struct tpm2_crb_ffa, buf, sizeof(*buf));
		priv->ffa_flags = crb_ffa->flags;
		priv->ffa_attributes = crb_ffa->attributes;
		rc = tpm_crb_ffa_init();
		if (rc) {
			/* If FF-A driver is not available yet, request probe retry */
			if (rc == -ENOENT)
				rc = -EPROBE_DEFER;
			goto out;
		}
	}

	if (sm == ACPI_TPM2_COMMAND_BUFFER_WITH_PLUTON) {
		if (buf->header.length < (sizeof(*buf) + sizeof(*crb_pluton))) {
			dev_err(dev,
				FW_BUG "TPM2 ACPI table has wrong size %u for start method type %d\n",
				buf->header.length,
				ACPI_TPM2_COMMAND_BUFFER_WITH_PLUTON);
			rc = -EINVAL;
			goto out;
		}
		crb_pluton = ACPI_ADD_PTR(struct tpm2_crb_pluton, buf, sizeof(*buf));
		rc = crb_map_pluton(dev, priv, buf, crb_pluton);
		if (rc)
			goto out;
	}

	priv->sm = sm;
	priv->hid = acpi_device_hid(device);

	rc = crb_map_io(device, priv, buf);
	if (rc)
		goto out;

	chip = tpmm_chip_alloc(dev, &tpm_crb);
	if (IS_ERR(chip)) {
		rc = PTR_ERR(chip);
		goto out;
	}

	dev_set_drvdata(&chip->dev, priv);
	chip->acpi_dev_handle = device->handle;
	chip->flags = TPM_CHIP_FLAG_TPM2;

	rc = tpm_chip_bootstrap(chip);
	if (rc)
		goto out;

#ifdef CONFIG_X86
	/* A quirk for https://www.amd.com/en/support/kb/faq/pa-410 */
	if (boot_cpu_data.x86_vendor == X86_VENDOR_AMD &&
	    priv->sm != ACPI_TPM2_COMMAND_BUFFER_WITH_PLUTON) {
		dev_info(dev, "Disabling hwrng\n");
		chip->flags |= TPM_CHIP_FLAG_HWRNG_DISABLED;
	}
#endif /* CONFIG_X86 */

	rc = tpm_chip_register(chip);

out:
	acpi_put_table((struct acpi_table_header *)buf);
	return rc;
}

static void crb_acpi_remove(struct acpi_device *device)
{
	struct device *dev = &device->dev;
	struct tpm_chip *chip = dev_get_drvdata(dev);

	tpm_chip_unregister(chip);
}

static const struct dev_pm_ops crb_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(tpm_pm_suspend, tpm_pm_resume)
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

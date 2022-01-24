// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2020 Intel Corporation. All rights reserved. */
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/moduleparam.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/sizes.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/pci.h>
#include <linux/io.h>
#include "cxlmem.h"
#include "cxlpci.h"
#include "cxl.h"

/**
 * DOC: cxl pci
 *
 * This implements the PCI exclusive functionality for a CXL device as it is
 * defined by the Compute Express Link specification. CXL devices may surface
 * certain functionality even if it isn't CXL enabled. While this driver is
 * focused around the PCI specific aspects of a CXL device, it binds to the
 * specific CXL memory device class code, and therefore the implementation of
 * cxl_pci is focused around CXL memory devices.
 *
 * The driver has several responsibilities, mainly:
 *  - Create the memX device and register on the CXL bus.
 *  - Enumerate device's register interface and map them.
 *  - Registers nvdimm bridge device with cxl_core.
 *  - Registers a CXL mailbox with cxl_core.
 */

#define cxl_doorbell_busy(cxlds)                                                \
	(readl((cxlds)->regs.mbox + CXLDEV_MBOX_CTRL_OFFSET) &                  \
	 CXLDEV_MBOX_CTRL_DOORBELL)

/* CXL 2.0 - 8.2.8.4 */
#define CXL_MAILBOX_TIMEOUT_MS (2 * HZ)

/*
 * CXL 2.0 ECN "Add Mailbox Ready Time" defines a capability field to
 * dictate how long to wait for the mailbox to become ready. The new
 * field allows the device to tell software the amount of time to wait
 * before mailbox ready. This field per the spec theoretically allows
 * for up to 255 seconds. 255 seconds is unreasonably long, its longer
 * than the maximum SATA port link recovery wait. Default to 60 seconds
 * until someone builds a CXL device that needs more time in practice.
 */
static unsigned short mbox_ready_timeout = 60;
module_param(mbox_ready_timeout, ushort, 0644);
MODULE_PARM_DESC(mbox_ready_timeout,
		 "seconds to wait for mailbox ready / memory active status");

static int cxl_pci_mbox_wait_for_doorbell(struct cxl_dev_state *cxlds)
{
	const unsigned long start = jiffies;
	unsigned long end = start;

	while (cxl_doorbell_busy(cxlds)) {
		end = jiffies;

		if (time_after(end, start + CXL_MAILBOX_TIMEOUT_MS)) {
			/* Check again in case preempted before timeout test */
			if (!cxl_doorbell_busy(cxlds))
				break;
			return -ETIMEDOUT;
		}
		cpu_relax();
	}

	dev_dbg(cxlds->dev, "Doorbell wait took %dms",
		jiffies_to_msecs(end) - jiffies_to_msecs(start));
	return 0;
}

#define cxl_err(dev, status, msg)                                        \
	dev_err_ratelimited(dev, msg ", device state %s%s\n",                  \
			    status & CXLMDEV_DEV_FATAL ? " fatal" : "",        \
			    status & CXLMDEV_FW_HALT ? " firmware-halt" : "")

#define cxl_cmd_err(dev, cmd, status, msg)                               \
	dev_err_ratelimited(dev, msg " (opcode: %#x), device state %s%s\n",    \
			    (cmd)->opcode,                                     \
			    status & CXLMDEV_DEV_FATAL ? " fatal" : "",        \
			    status & CXLMDEV_FW_HALT ? " firmware-halt" : "")

/**
 * __cxl_pci_mbox_send_cmd() - Execute a mailbox command
 * @cxlds: The device state to communicate with.
 * @mbox_cmd: Command to send to the memory device.
 *
 * Context: Any context. Expects mbox_mutex to be held.
 * Return: -ETIMEDOUT if timeout occurred waiting for completion. 0 on success.
 *         Caller should check the return code in @mbox_cmd to make sure it
 *         succeeded.
 *
 * This is a generic form of the CXL mailbox send command thus only using the
 * registers defined by the mailbox capability ID - CXL 2.0 8.2.8.4. Memory
 * devices, and perhaps other types of CXL devices may have further information
 * available upon error conditions. Driver facilities wishing to send mailbox
 * commands should use the wrapper command.
 *
 * The CXL spec allows for up to two mailboxes. The intention is for the primary
 * mailbox to be OS controlled and the secondary mailbox to be used by system
 * firmware. This allows the OS and firmware to communicate with the device and
 * not need to coordinate with each other. The driver only uses the primary
 * mailbox.
 */
static int __cxl_pci_mbox_send_cmd(struct cxl_dev_state *cxlds,
				   struct cxl_mbox_cmd *mbox_cmd)
{
	void __iomem *payload = cxlds->regs.mbox + CXLDEV_MBOX_PAYLOAD_OFFSET;
	struct device *dev = cxlds->dev;
	u64 cmd_reg, status_reg;
	size_t out_len;
	int rc;

	lockdep_assert_held(&cxlds->mbox_mutex);

	/*
	 * Here are the steps from 8.2.8.4 of the CXL 2.0 spec.
	 *   1. Caller reads MB Control Register to verify doorbell is clear
	 *   2. Caller writes Command Register
	 *   3. Caller writes Command Payload Registers if input payload is non-empty
	 *   4. Caller writes MB Control Register to set doorbell
	 *   5. Caller either polls for doorbell to be clear or waits for interrupt if configured
	 *   6. Caller reads MB Status Register to fetch Return code
	 *   7. If command successful, Caller reads Command Register to get Payload Length
	 *   8. If output payload is non-empty, host reads Command Payload Registers
	 *
	 * Hardware is free to do whatever it wants before the doorbell is rung,
	 * and isn't allowed to change anything after it clears the doorbell. As
	 * such, steps 2 and 3 can happen in any order, and steps 6, 7, 8 can
	 * also happen in any order (though some orders might not make sense).
	 */

	/* #1 */
	if (cxl_doorbell_busy(cxlds)) {
		u64 md_status =
			readq(cxlds->regs.memdev + CXLMDEV_STATUS_OFFSET);

		cxl_cmd_err(cxlds->dev, mbox_cmd, md_status,
			    "mailbox queue busy");
		return -EBUSY;
	}

	cmd_reg = FIELD_PREP(CXLDEV_MBOX_CMD_COMMAND_OPCODE_MASK,
			     mbox_cmd->opcode);
	if (mbox_cmd->size_in) {
		if (WARN_ON(!mbox_cmd->payload_in))
			return -EINVAL;

		cmd_reg |= FIELD_PREP(CXLDEV_MBOX_CMD_PAYLOAD_LENGTH_MASK,
				      mbox_cmd->size_in);
		memcpy_toio(payload, mbox_cmd->payload_in, mbox_cmd->size_in);
	}

	/* #2, #3 */
	writeq(cmd_reg, cxlds->regs.mbox + CXLDEV_MBOX_CMD_OFFSET);

	/* #4 */
	dev_dbg(dev, "Sending command\n");
	writel(CXLDEV_MBOX_CTRL_DOORBELL,
	       cxlds->regs.mbox + CXLDEV_MBOX_CTRL_OFFSET);

	/* #5 */
	rc = cxl_pci_mbox_wait_for_doorbell(cxlds);
	if (rc == -ETIMEDOUT) {
		u64 md_status = readq(cxlds->regs.memdev + CXLMDEV_STATUS_OFFSET);

		cxl_cmd_err(cxlds->dev, mbox_cmd, md_status, "mailbox timeout");
		return rc;
	}

	/* #6 */
	status_reg = readq(cxlds->regs.mbox + CXLDEV_MBOX_STATUS_OFFSET);
	mbox_cmd->return_code =
		FIELD_GET(CXLDEV_MBOX_STATUS_RET_CODE_MASK, status_reg);

	if (mbox_cmd->return_code != 0) {
		dev_dbg(dev, "Mailbox operation had an error\n");
		return 0;
	}

	/* #7 */
	cmd_reg = readq(cxlds->regs.mbox + CXLDEV_MBOX_CMD_OFFSET);
	out_len = FIELD_GET(CXLDEV_MBOX_CMD_PAYLOAD_LENGTH_MASK, cmd_reg);

	/* #8 */
	if (out_len && mbox_cmd->payload_out) {
		/*
		 * Sanitize the copy. If hardware misbehaves, out_len per the
		 * spec can actually be greater than the max allowed size (21
		 * bits available but spec defined 1M max). The caller also may
		 * have requested less data than the hardware supplied even
		 * within spec.
		 */
		size_t n = min3(mbox_cmd->size_out, cxlds->payload_size, out_len);

		memcpy_fromio(mbox_cmd->payload_out, payload, n);
		mbox_cmd->size_out = n;
	} else {
		mbox_cmd->size_out = 0;
	}

	return 0;
}

static int cxl_pci_mbox_send(struct cxl_dev_state *cxlds, struct cxl_mbox_cmd *cmd)
{
	int rc;

	mutex_lock_io(&cxlds->mbox_mutex);
	rc = __cxl_pci_mbox_send_cmd(cxlds, cmd);
	mutex_unlock(&cxlds->mbox_mutex);

	return rc;
}

static int cxl_pci_setup_mailbox(struct cxl_dev_state *cxlds)
{
	const int cap = readl(cxlds->regs.mbox + CXLDEV_MBOX_CAPS_OFFSET);
	unsigned long timeout;
	u64 md_status;

	timeout = jiffies + mbox_ready_timeout * HZ;
	do {
		md_status = readq(cxlds->regs.memdev + CXLMDEV_STATUS_OFFSET);
		if (md_status & CXLMDEV_MBOX_IF_READY)
			break;
		if (msleep_interruptible(100))
			break;
	} while (!time_after(jiffies, timeout));

	if (!(md_status & CXLMDEV_MBOX_IF_READY)) {
		cxl_err(cxlds->dev, md_status,
			"timeout awaiting mailbox ready");
		return -ETIMEDOUT;
	}

	/*
	 * A command may be in flight from a previous driver instance,
	 * think kexec, do one doorbell wait so that
	 * __cxl_pci_mbox_send_cmd() can assume that it is the only
	 * source for future doorbell busy events.
	 */
	if (cxl_pci_mbox_wait_for_doorbell(cxlds) != 0) {
		cxl_err(cxlds->dev, md_status, "timeout awaiting mailbox idle");
		return -ETIMEDOUT;
	}

	cxlds->mbox_send = cxl_pci_mbox_send;
	cxlds->payload_size =
		1 << FIELD_GET(CXLDEV_MBOX_CAP_PAYLOAD_SIZE_MASK, cap);

	/*
	 * CXL 2.0 8.2.8.4.3 Mailbox Capabilities Register
	 *
	 * If the size is too small, mandatory commands will not work and so
	 * there's no point in going forward. If the size is too large, there's
	 * no harm is soft limiting it.
	 */
	cxlds->payload_size = min_t(size_t, cxlds->payload_size, SZ_1M);
	if (cxlds->payload_size < 256) {
		dev_err(cxlds->dev, "Mailbox is too small (%zub)",
			cxlds->payload_size);
		return -ENXIO;
	}

	dev_dbg(cxlds->dev, "Mailbox payload sized %zu",
		cxlds->payload_size);

	return 0;
}

static int cxl_map_regblock(struct pci_dev *pdev, struct cxl_register_map *map)
{
	void __iomem *addr;
	int bar = map->barno;
	struct device *dev = &pdev->dev;
	resource_size_t offset = map->block_offset;

	/* Basic sanity check that BAR is big enough */
	if (pci_resource_len(pdev, bar) < offset) {
		dev_err(dev, "BAR%d: %pr: too small (offset: %pa)\n", bar,
			&pdev->resource[bar], &offset);
		return -ENXIO;
	}

	addr = pci_iomap(pdev, bar, 0);
	if (!addr) {
		dev_err(dev, "failed to map registers\n");
		return -ENOMEM;
	}

	dev_dbg(dev, "Mapped CXL Memory Device resource bar %u @ %pa\n",
		bar, &offset);

	map->base = addr + map->block_offset;
	return 0;
}

static void cxl_unmap_regblock(struct pci_dev *pdev,
			       struct cxl_register_map *map)
{
	pci_iounmap(pdev, map->base - map->block_offset);
	map->base = NULL;
}

static int cxl_probe_regs(struct pci_dev *pdev, struct cxl_register_map *map)
{
	struct cxl_component_reg_map *comp_map;
	struct cxl_device_reg_map *dev_map;
	struct device *dev = &pdev->dev;
	void __iomem *base = map->base;

	switch (map->reg_type) {
	case CXL_REGLOC_RBI_COMPONENT:
		comp_map = &map->component_map;
		cxl_probe_component_regs(dev, base, comp_map);
		if (!comp_map->hdm_decoder.valid) {
			dev_err(dev, "HDM decoder registers not found\n");
			return -ENXIO;
		}

		dev_dbg(dev, "Set up component registers\n");
		break;
	case CXL_REGLOC_RBI_MEMDEV:
		dev_map = &map->device_map;
		cxl_probe_device_regs(dev, base, dev_map);
		if (!dev_map->status.valid || !dev_map->mbox.valid ||
		    !dev_map->memdev.valid) {
			dev_err(dev, "registers not found: %s%s%s\n",
				!dev_map->status.valid ? "status " : "",
				!dev_map->mbox.valid ? "mbox " : "",
				!dev_map->memdev.valid ? "memdev " : "");
			return -ENXIO;
		}

		dev_dbg(dev, "Probing device registers...\n");
		break;
	default:
		break;
	}

	return 0;
}

static int cxl_map_regs(struct cxl_dev_state *cxlds, struct cxl_register_map *map)
{
	struct device *dev = cxlds->dev;
	struct pci_dev *pdev = to_pci_dev(dev);

	switch (map->reg_type) {
	case CXL_REGLOC_RBI_COMPONENT:
		cxl_map_component_regs(pdev, &cxlds->regs.component, map);
		dev_dbg(dev, "Mapping component registers...\n");
		break;
	case CXL_REGLOC_RBI_MEMDEV:
		cxl_map_device_regs(pdev, &cxlds->regs.device_regs, map);
		dev_dbg(dev, "Probing device registers...\n");
		break;
	default:
		break;
	}

	return 0;
}

static int cxl_setup_regs(struct pci_dev *pdev, enum cxl_regloc_type type,
			  struct cxl_register_map *map)
{
	int rc;

	rc = cxl_find_regblock(pdev, type, map);
	if (rc)
		return rc;

	rc = cxl_map_regblock(pdev, map);
	if (rc)
		return rc;

	rc = cxl_probe_regs(pdev, map);
	cxl_unmap_regblock(pdev, map);

	return rc;
}

static int wait_for_valid(struct cxl_dev_state *cxlds)
{
	struct pci_dev *pdev = to_pci_dev(cxlds->dev);
	int d = cxlds->cxl_dvsec, rc;
	u32 val;

	/*
	 * Memory_Info_Valid: When set, indicates that the CXL Range 1 Size high
	 * and Size Low registers are valid. Must be set within 1 second of
	 * deassertion of reset to CXL device. Likely it is already set by the
	 * time this runs, but otherwise give a 1.5 second timeout in case of
	 * clock skew.
	 */
	rc = pci_read_config_dword(pdev, d + CXL_DVSEC_RANGE_SIZE_LOW(0), &val);
	if (rc)
		return rc;

	if (val & CXL_DVSEC_MEM_INFO_VALID)
		return 0;

	msleep(1500);

	rc = pci_read_config_dword(pdev, d + CXL_DVSEC_RANGE_SIZE_LOW(0), &val);
	if (rc)
		return rc;

	if (val & CXL_DVSEC_MEM_INFO_VALID)
		return 0;

	return -ETIMEDOUT;
}

/*
 * Wait up to @mbox_ready_timeout for the device to report memory
 * active.
 */
static int wait_for_media_ready(struct cxl_dev_state *cxlds)
{
	struct pci_dev *pdev = to_pci_dev(cxlds->dev);
	int d = cxlds->cxl_dvsec;
	bool active = false;
	u64 md_status;
	int rc, i;

	rc = wait_for_valid(cxlds);
	if (rc)
		return rc;

	for (i = mbox_ready_timeout; i; i--) {
		u32 temp;
		int rc;

		rc = pci_read_config_dword(
			pdev, d + CXL_DVSEC_RANGE_SIZE_LOW(0), &temp);
		if (rc)
			return rc;

		active = FIELD_GET(CXL_DVSEC_MEM_ACTIVE, temp);
		if (active)
			break;
		msleep(1000);
	}

	if (!active) {
		dev_err(&pdev->dev,
			"timeout awaiting memory active after %d seconds\n",
			mbox_ready_timeout);
		return -ETIMEDOUT;
	}

	md_status = readq(cxlds->regs.memdev + CXLMDEV_STATUS_OFFSET);
	if (!CXLMDEV_READY(md_status))
		return -EIO;

	return 0;
}

static int cxl_dvsec_ranges(struct cxl_dev_state *cxlds)
{
	struct cxl_endpoint_dvsec_info *info = &cxlds->info;
	struct pci_dev *pdev = to_pci_dev(cxlds->dev);
	int d = cxlds->cxl_dvsec;
	int hdm_count, rc, i;
	u16 cap, ctrl;

	if (!d)
		return -ENXIO;

	rc = pci_read_config_word(pdev, d + CXL_DVSEC_CAP_OFFSET, &cap);
	if (rc)
		return rc;

	rc = pci_read_config_word(pdev, d + CXL_DVSEC_CTRL_OFFSET, &ctrl);
	if (rc)
		return rc;

	if (!(cap & CXL_DVSEC_MEM_CAPABLE))
		return -ENXIO;

	/*
	 * It is not allowed by spec for MEM.capable to be set and have 0 legacy
	 * HDM decoders (values > 2 are also undefined as of CXL 2.0). As this
	 * driver is for a spec defined class code which must be CXL.mem
	 * capable, there is no point in continuing to enable CXL.mem.
	 */
	hdm_count = FIELD_GET(CXL_DVSEC_HDM_COUNT_MASK, cap);
	if (!hdm_count || hdm_count > 2)
		return -EINVAL;

	rc = wait_for_valid(cxlds);
	if (rc)
		return rc;

	info->mem_enabled = FIELD_GET(CXL_DVSEC_MEM_ENABLE, ctrl);

	for (i = 0; i < hdm_count; i++) {
		u64 base, size;
		u32 temp;

		rc = pci_read_config_dword(
			pdev, d + CXL_DVSEC_RANGE_SIZE_HIGH(i), &temp);
		if (rc)
			return rc;

		size = (u64)temp << 32;

		rc = pci_read_config_dword(
			pdev, d + CXL_DVSEC_RANGE_SIZE_LOW(i), &temp);
		if (rc)
			return rc;

		size |= temp & CXL_DVSEC_MEM_SIZE_LOW_MASK;

		rc = pci_read_config_dword(
			pdev, d + CXL_DVSEC_RANGE_BASE_HIGH(i), &temp);
		if (rc)
			return rc;

		base = (u64)temp << 32;

		rc = pci_read_config_dword(
			pdev, d + CXL_DVSEC_RANGE_BASE_LOW(i), &temp);
		if (rc)
			return rc;

		base |= temp & CXL_DVSEC_MEM_BASE_LOW_MASK;

		info->dvsec_range[i] = (struct range) {
			.start = base,
			.end = base + size - 1
		};

		if (size)
			info->ranges++;
	}

	return 0;
}

static int cxl_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct cxl_register_map map;
	struct cxl_memdev *cxlmd;
	struct cxl_dev_state *cxlds;
	int rc;

	/*
	 * Double check the anonymous union trickery in struct cxl_regs
	 * FIXME switch to struct_group()
	 */
	BUILD_BUG_ON(offsetof(struct cxl_regs, memdev) !=
		     offsetof(struct cxl_regs, device_regs.memdev));

	rc = pcim_enable_device(pdev);
	if (rc)
		return rc;

	cxlds = cxl_dev_state_create(&pdev->dev);
	if (IS_ERR(cxlds))
		return PTR_ERR(cxlds);

	cxlds->cxl_dvsec = pci_find_dvsec_capability(
		pdev, PCI_DVSEC_VENDOR_ID_CXL, CXL_DVSEC_PCIE_DEVICE);
	if (!cxlds->cxl_dvsec)
		dev_warn(&pdev->dev,
			 "Device DVSEC not present, skip CXL.mem init\n");

	cxlds->wait_media_ready = wait_for_media_ready;

	rc = cxl_setup_regs(pdev, CXL_REGLOC_RBI_MEMDEV, &map);
	if (rc)
		return rc;

	rc = cxl_map_regs(cxlds, &map);
	if (rc)
		return rc;

	/*
	 * If the component registers can't be found, the cxl_pci driver may
	 * still be useful for management functions so don't return an error.
	 */
	cxlds->component_reg_phys = CXL_RESOURCE_NONE;
	rc = cxl_setup_regs(pdev, CXL_REGLOC_RBI_COMPONENT, &map);
	if (rc)
		dev_warn(&pdev->dev, "No component registers (%d)\n", rc);

	cxlds->component_reg_phys = cxl_regmap_to_base(pdev, &map);

	rc = cxl_pci_setup_mailbox(cxlds);
	if (rc)
		return rc;

	rc = cxl_enumerate_cmds(cxlds);
	if (rc)
		return rc;

	rc = cxl_dev_state_identify(cxlds);
	if (rc)
		return rc;

	rc = cxl_mem_create_range_info(cxlds);
	if (rc)
		return rc;

	rc = cxl_dvsec_ranges(cxlds);
	if (rc)
		dev_warn(&pdev->dev,
			 "Failed to get DVSEC range information (%d)\n", rc);

	cxlmd = devm_cxl_add_memdev(cxlds);
	if (IS_ERR(cxlmd))
		return PTR_ERR(cxlmd);

	if (range_len(&cxlds->pmem_range) && IS_ENABLED(CONFIG_CXL_PMEM))
		rc = devm_cxl_add_nvdimm(&pdev->dev, cxlmd);

	return rc;
}

static const struct pci_device_id cxl_mem_pci_tbl[] = {
	/* PCI class code for CXL.mem Type-3 Devices */
	{ PCI_DEVICE_CLASS((PCI_CLASS_MEMORY_CXL << 8 | CXL_MEMORY_PROGIF), ~0)},
	{ /* terminate list */ },
};
MODULE_DEVICE_TABLE(pci, cxl_mem_pci_tbl);

static struct pci_driver cxl_pci_driver = {
	.name			= KBUILD_MODNAME,
	.id_table		= cxl_mem_pci_tbl,
	.probe			= cxl_pci_probe,
	.driver	= {
		.probe_type	= PROBE_PREFER_ASYNCHRONOUS,
	},
};

MODULE_LICENSE("GPL v2");
module_pci_driver(cxl_pci_driver);
MODULE_IMPORT_NS(CXL);

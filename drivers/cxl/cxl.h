/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2020 Intel Corporation. */

#ifndef __CXL_H__
#define __CXL_H__

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/io.h>

/* CXL 2.0 8.2.8.1 Device Capabilities Array Register */
#define CXLDEV_CAP_ARRAY_OFFSET 0x0
#define   CXLDEV_CAP_ARRAY_CAP_ID 0
#define   CXLDEV_CAP_ARRAY_ID_MASK GENMASK_ULL(15, 0)
#define   CXLDEV_CAP_ARRAY_COUNT_MASK GENMASK_ULL(47, 32)
/* CXL 2.0 8.2.8.2 CXL Device Capability Header Register */
#define CXLDEV_CAP_HDR_CAP_ID_MASK GENMASK(15, 0)
/* CXL 2.0 8.2.8.2.1 CXL Device Capabilities */
#define CXLDEV_CAP_CAP_ID_DEVICE_STATUS 0x1
#define CXLDEV_CAP_CAP_ID_PRIMARY_MAILBOX 0x2
#define CXLDEV_CAP_CAP_ID_SECONDARY_MAILBOX 0x3
#define CXLDEV_CAP_CAP_ID_MEMDEV 0x4000

/* CXL 2.0 8.2.8.4 Mailbox Registers */
#define CXLDEV_MBOX_CAPS_OFFSET 0x00
#define   CXLDEV_MBOX_CAP_PAYLOAD_SIZE_MASK GENMASK(4, 0)
#define CXLDEV_MBOX_CTRL_OFFSET 0x04
#define   CXLDEV_MBOX_CTRL_DOORBELL BIT(0)
#define CXLDEV_MBOX_CMD_OFFSET 0x08
#define   CXLDEV_MBOX_CMD_COMMAND_OPCODE_MASK GENMASK_ULL(15, 0)
#define   CXLDEV_MBOX_CMD_PAYLOAD_LENGTH_MASK GENMASK_ULL(36, 16)
#define CXLDEV_MBOX_STATUS_OFFSET 0x10
#define   CXLDEV_MBOX_STATUS_RET_CODE_MASK GENMASK_ULL(47, 32)
#define CXLDEV_MBOX_BG_CMD_STATUS_OFFSET 0x18
#define CXLDEV_MBOX_PAYLOAD_OFFSET 0x20

/* CXL 2.0 8.2.8.5.1.1 Memory Device Status Register */
#define CXLMDEV_STATUS_OFFSET 0x0
#define   CXLMDEV_DEV_FATAL BIT(0)
#define   CXLMDEV_FW_HALT BIT(1)
#define   CXLMDEV_STATUS_MEDIA_STATUS_MASK GENMASK(3, 2)
#define     CXLMDEV_MS_NOT_READY 0
#define     CXLMDEV_MS_READY 1
#define     CXLMDEV_MS_ERROR 2
#define     CXLMDEV_MS_DISABLED 3
#define CXLMDEV_READY(status)                                                  \
	(FIELD_GET(CXLMDEV_STATUS_MEDIA_STATUS_MASK, status) ==                \
	 CXLMDEV_MS_READY)
#define   CXLMDEV_MBOX_IF_READY BIT(4)
#define   CXLMDEV_RESET_NEEDED_MASK GENMASK(7, 5)
#define     CXLMDEV_RESET_NEEDED_NOT 0
#define     CXLMDEV_RESET_NEEDED_COLD 1
#define     CXLMDEV_RESET_NEEDED_WARM 2
#define     CXLMDEV_RESET_NEEDED_HOT 3
#define     CXLMDEV_RESET_NEEDED_CXL 4
#define CXLMDEV_RESET_NEEDED(status)                                           \
	(FIELD_GET(CXLMDEV_RESET_NEEDED_MASK, status) !=                       \
	 CXLMDEV_RESET_NEEDED_NOT)

struct cxl_memdev;
/**
 * struct cxl_mem - A CXL memory device
 * @pdev: The PCI device associated with this CXL device.
 * @regs: IO mappings to the device's MMIO
 * @status_regs: CXL 2.0 8.2.8.3 Device Status Registers
 * @mbox_regs: CXL 2.0 8.2.8.4 Mailbox Registers
 * @memdev_regs: CXL 2.0 8.2.8.5 Memory Device Registers
 * @payload_size: Size of space for payload
 *                (CXL 2.0 8.2.8.4.3 Mailbox Capabilities Register)
 * @mbox_mutex: Mutex to synchronize mailbox access.
 * @firmware_version: Firmware version for the memory device.
 * @enabled_commands: Hardware commands found enabled in CEL.
 * @pmem_range: Persistent memory capacity information.
 * @ram_range: Volatile memory capacity information.
 */
struct cxl_mem {
	struct pci_dev *pdev;
	void __iomem *regs;
	struct cxl_memdev *cxlmd;

	void __iomem *status_regs;
	void __iomem *mbox_regs;
	void __iomem *memdev_regs;

	size_t payload_size;
	struct mutex mbox_mutex; /* Protects device mailbox and firmware */
	char firmware_version[0x10];
	unsigned long *enabled_cmds;

	struct range pmem_range;
	struct range ram_range;
};

extern struct bus_type cxl_bus_type;
#endif /* __CXL_H__ */

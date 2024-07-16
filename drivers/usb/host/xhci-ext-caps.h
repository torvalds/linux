/* SPDX-License-Identifier: GPL-2.0 */
/*
 * xHCI host controller driver
 *
 * Copyright (C) 2008 Intel Corp.
 *
 * Author: Sarah Sharp
 * Some code borrowed from the Linux EHCI driver.
 */

/* HC should halt within 16 ms, but use 32 ms as some hosts take longer */
#define XHCI_MAX_HALT_USEC	(32 * 1000)
/* HC not running - set to 1 when run/stop bit is cleared. */
#define XHCI_STS_HALT		(1<<0)

/* HCCPARAMS offset from PCI base address */
#define XHCI_HCC_PARAMS_OFFSET	0x10
/* HCCPARAMS contains the first extended capability pointer */
#define XHCI_HCC_EXT_CAPS(p)	(((p)>>16)&0xffff)

/* Command and Status registers offset from the Operational Registers address */
#define XHCI_CMD_OFFSET		0x00
#define XHCI_STS_OFFSET		0x04

#define XHCI_MAX_EXT_CAPS		50

/* Capability Register */
/* bits 7:0 - how long is the Capabilities register */
#define XHCI_HC_LENGTH(p)	(((p)>>00)&0x00ff)

/* Extended capability register fields */
#define XHCI_EXT_CAPS_ID(p)	(((p)>>0)&0xff)
#define XHCI_EXT_CAPS_NEXT(p)	(((p)>>8)&0xff)
#define	XHCI_EXT_CAPS_VAL(p)	((p)>>16)
/* Extended capability IDs - ID 0 reserved */
#define XHCI_EXT_CAPS_LEGACY	1
#define XHCI_EXT_CAPS_PROTOCOL	2
#define XHCI_EXT_CAPS_PM	3
#define XHCI_EXT_CAPS_VIRT	4
#define XHCI_EXT_CAPS_ROUTE	5
/* IDs 6-9 reserved */
#define XHCI_EXT_CAPS_DEBUG	10
/* Vendor caps */
#define XHCI_EXT_CAPS_VENDOR_INTEL	192
/* USB Legacy Support Capability - section 7.1.1 */
#define XHCI_HC_BIOS_OWNED	(1 << 16)
#define XHCI_HC_OS_OWNED	(1 << 24)

/* USB Legacy Support Capability - section 7.1.1 */
/* Add this offset, plus the value of xECP in HCCPARAMS to the base address */
#define XHCI_LEGACY_SUPPORT_OFFSET	(0x00)

/* USB Legacy Support Control and Status Register  - section 7.1.2 */
/* Add this offset, plus the value of xECP in HCCPARAMS to the base address */
#define XHCI_LEGACY_CONTROL_OFFSET	(0x04)
/* bits 1:3, 5:12, and 17:19 need to be preserved; bits 21:28 should be zero */
#define	XHCI_LEGACY_DISABLE_SMI		((0x7 << 1) + (0xff << 5) + (0x7 << 17))
#define XHCI_LEGACY_SMI_EVENTS		(0x7 << 29)

/* USB 2.0 xHCI 0.96 L1C capability - section 7.2.2.1.3.2 */
#define XHCI_L1C               (1 << 16)

/* USB 2.0 xHCI 1.0 hardware LMP capability - section 7.2.2.1.3.2 */
#define XHCI_HLC               (1 << 19)
#define XHCI_BLC               (1 << 20)

/* command register values to disable interrupts and halt the HC */
/* start/stop HC execution - do not write unless HC is halted*/
#define XHCI_CMD_RUN		(1 << 0)
/* Event Interrupt Enable - get irq when EINT bit is set in USBSTS register */
#define XHCI_CMD_EIE		(1 << 2)
/* Host System Error Interrupt Enable - get irq when HSEIE bit set in USBSTS */
#define XHCI_CMD_HSEIE		(1 << 3)
/* Enable Wrap Event - '1' means xHC generates an event when MFINDEX wraps. */
#define XHCI_CMD_EWE		(1 << 10)

#define XHCI_IRQS		(XHCI_CMD_EIE | XHCI_CMD_HSEIE | XHCI_CMD_EWE)

/* true: Controller Not Ready to accept doorbell or op reg writes after reset */
#define XHCI_STS_CNR		(1 << 11)

#include <linux/io.h>

/**
 * Find the offset of the extended capabilities with capability ID id.
 *
 * @base	PCI MMIO registers base address.
 * @start	address at which to start looking, (0 or HCC_PARAMS to start at
 *		beginning of list)
 * @id		Extended capability ID to search for, or 0 for the next
 *		capability
 *
 * Returns the offset of the next matching extended capability structure.
 * Some capabilities can occur several times, e.g., the XHCI_EXT_CAPS_PROTOCOL,
 * and this provides a way to find them all.
 */

static inline int xhci_find_next_ext_cap(void __iomem *base, u32 start, int id)
{
	u32 val;
	u32 next;
	u32 offset;

	offset = start;
	if (!start || start == XHCI_HCC_PARAMS_OFFSET) {
		val = readl(base + XHCI_HCC_PARAMS_OFFSET);
		if (val == ~0)
			return 0;
		offset = XHCI_HCC_EXT_CAPS(val) << 2;
		if (!offset)
			return 0;
	}
	do {
		val = readl(base + offset);
		if (val == ~0)
			return 0;
		if (offset != start && (id == 0 || XHCI_EXT_CAPS_ID(val) == id))
			return offset;

		next = XHCI_EXT_CAPS_NEXT(val);
		offset += next << 2;
	} while (next);

	return 0;
}

/*
 * xHCI host controller driver
 *
 * Copyright (C) 2008 Intel Corp.
 *
 * Author: Sarah Sharp
 * Some code borrowed from the Linux EHCI driver.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
/* Up to 16 ms to halt an HC */
#define XHCI_MAX_HALT_USEC	(16*1000)
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
/* USB Legacy Support Capability - section 7.1.1 */
#define XHCI_HC_BIOS_OWNED	(1 << 16)
#define XHCI_HC_OS_OWNED	(1 << 24)

/* USB Legacy Support Capability - section 7.1.1 */
/* Add this offset, plus the value of xECP in HCCPARAMS to the base address */
#define XHCI_LEGACY_SUPPORT_OFFSET	(0x00)

/* USB Legacy Support Control and Status Register  - section 7.1.2 */
/* Add this offset, plus the value of xECP in HCCPARAMS to the base address */
#define XHCI_LEGACY_CONTROL_OFFSET	(0x04)
/* bits 1:2, 5:12, and 17:19 need to be preserved; bits 21:28 should be zero */
#define	XHCI_LEGACY_DISABLE_SMI		((0x3 << 1) + (0xff << 5) + (0x7 << 17))

/* USB 2.0 xHCI 0.96 L1C capability - section 7.2.2.1.3.2 */
#define XHCI_L1C               (1 << 16)

/* USB 2.0 xHCI 1.0 hardware LMP capability - section 7.2.2.1.3.2 */
#define XHCI_HLC               (1 << 19)

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
 * Return the next extended capability pointer register.
 *
 * @base	PCI register base address.
 *
 * @ext_offset	Offset of the 32-bit register that contains the extended
 * capabilites pointer.  If searching for the first extended capability, pass
 * in XHCI_HCC_PARAMS_OFFSET.  If searching for the next extended capability,
 * pass in the offset of the current extended capability register.
 *
 * Returns 0 if there is no next extended capability register or returns the register offset
 * from the PCI registers base address.
 */
static inline int xhci_find_next_cap_offset(void __iomem *base, int ext_offset)
{
	u32 next;

	next = readl(base + ext_offset);

	if (ext_offset == XHCI_HCC_PARAMS_OFFSET) {
		/* Find the first extended capability */
		next = XHCI_HCC_EXT_CAPS(next);
		ext_offset = 0;
	} else {
		/* Find the next extended capability */
		next = XHCI_EXT_CAPS_NEXT(next);
	}

	if (!next)
		return 0;
	/*
	 * Address calculation from offset of extended capabilities
	 * (or HCCPARAMS) register - see section 5.3.6 and section 7.
	 */
	return ext_offset + (next << 2);
}

/**
 * Find the offset of the extended capabilities with capability ID id.
 *
 * @base PCI MMIO registers base address.
 * @ext_offset Offset from base of the first extended capability to look at,
 * 		or the address of HCCPARAMS.
 * @id Extended capability ID to search for.
 *
 * This uses an arbitrary limit of XHCI_MAX_EXT_CAPS extended capabilities
 * to make sure that the list doesn't contain a loop.
 */
static inline int xhci_find_ext_cap_by_id(void __iomem *base, int ext_offset, int id)
{
	u32 val;
	int limit = XHCI_MAX_EXT_CAPS;

	while (ext_offset && limit > 0) {
		val = readl(base + ext_offset);
		if (XHCI_EXT_CAPS_ID(val) == id)
			break;
		ext_offset = xhci_find_next_cap_offset(base, ext_offset);
		limit--;
	}
	if (limit > 0)
		return ext_offset;
	return 0;
}

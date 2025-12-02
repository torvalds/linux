/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Thunderbolt driver - NHI registers
 *
 * Copyright (c) 2014 Andreas Noever <andreas.noever@gmail.com>
 * Copyright (C) 2018, Intel Corporation
 */

#ifndef NHI_REGS_H_
#define NHI_REGS_H_

#include <linux/types.h>

enum ring_flags {
	RING_FLAG_ISOCH_ENABLE = 1 << 27, /* TX only? */
	RING_FLAG_E2E_FLOW_CONTROL = 1 << 28,
	RING_FLAG_PCI_NO_SNOOP = 1 << 29,
	RING_FLAG_RAW = 1 << 30, /* ignore EOF/SOF mask, include checksum */
	RING_FLAG_ENABLE = 1 << 31,
};

/**
 * struct ring_desc - TX/RX ring entry
 * @phys: DMA mapped address of the frame
 * @length: Size of the ring
 * @eof: End of frame protocol defined field
 * @sof: Start of frame protocol defined field
 * @flags: Ring descriptor flags
 * @time: Fill with zero
 *
 * For TX set length/eof/sof.
 * For RX length/eof/sof are set by the NHI.
 */
struct ring_desc {
	u64 phys;
	u32 length:12;
	u32 eof:4;
	u32 sof:4;
	enum ring_desc_flags flags:12;
	u32 time; /* write zero */
} __packed;

/* NHI registers in bar 0 */

/*
 * 16 bytes per entry, one entry for every hop (REG_CAPS)
 * 00: physical pointer to an array of struct ring_desc
 * 08: ring tail (set by NHI)
 * 10: ring head (index of first non posted descriptor)
 * 12: descriptor count
 */
#define REG_TX_RING_BASE	0x00000

/*
 * 16 bytes per entry, one entry for every hop (REG_CAPS)
 * 00: physical pointer to an array of struct ring_desc
 * 08: ring head (index of first not posted descriptor)
 * 10: ring tail (set by NHI)
 * 12: descriptor count
 * 14: max frame sizes (anything larger than 0x100 has no effect)
 */
#define REG_RX_RING_BASE	0x08000

/*
 * 32 bytes per entry, one entry for every hop (REG_CAPS)
 * 00: enum_ring_flags
 * 04: isoch time stamp ?? (write 0)
 * ..: unknown
 */
#define REG_TX_OPTIONS_BASE	0x19800

/*
 * 32 bytes per entry, one entry for every hop (REG_CAPS)
 * 00: enum ring_flags
 *     If RING_FLAG_E2E_FLOW_CONTROL is set then bits 13-23 must be set to
 *     the corresponding TX hop id.
 * 04: EOF/SOF mask (ignored for RING_FLAG_RAW rings)
 * ..: unknown
 */
#define REG_RX_OPTIONS_BASE	0x29800
#define REG_RX_OPTIONS_E2E_HOP_MASK	GENMASK(22, 12)
#define REG_RX_OPTIONS_E2E_HOP_SHIFT	12

/*
 * three bitfields: tx, rx, rx overflow
 * Every bitfield contains one bit for every hop (REG_CAPS).
 * New interrupts are fired only after ALL registers have been
 * read (even those containing only disabled rings).
 */
#define REG_RING_NOTIFY_BASE	0x37800
#define RING_NOTIFY_REG_COUNT(nhi) ((31 + 3 * nhi->hop_count) / 32)
#define REG_RING_INT_CLEAR	0x37808

/*
 * two bitfields: rx, tx
 * Both bitfields contains one bit for every hop (REG_CAPS). To
 * enable/disable interrupts set/clear the corresponding bits.
 */
#define REG_RING_INTERRUPT_BASE	0x38200
#define RING_INTERRUPT_REG_COUNT(nhi) ((31 + 2 * nhi->hop_count) / 32)

#define REG_RING_INTERRUPT_MASK_CLEAR_BASE	0x38208

#define REG_INT_THROTTLING_RATE	0x38c00

/* Interrupt Vector Allocation */
#define REG_INT_VEC_ALLOC_BASE	0x38c40
#define REG_INT_VEC_ALLOC_BITS	4
#define REG_INT_VEC_ALLOC_MASK	GENMASK(3, 0)
#define REG_INT_VEC_ALLOC_REGS	(32 / REG_INT_VEC_ALLOC_BITS)

/* The last 11 bits contain the number of hops supported by the NHI port. */
#define REG_CAPS			0x39640
#define REG_CAPS_VERSION_MASK		GENMASK(23, 16)
#define REG_CAPS_VERSION_2		0x40

#define REG_DMA_MISC			0x39864
#define REG_DMA_MISC_INT_AUTO_CLEAR     BIT(2)
#define REG_DMA_MISC_DISABLE_AUTO_CLEAR	BIT(17)

#define REG_RESET			0x39898
#define REG_RESET_HRR			BIT(0)

#define REG_INMAIL_DATA			0x39900

#define REG_INMAIL_CMD			0x39904
#define REG_INMAIL_CMD_MASK		GENMASK(7, 0)
#define REG_INMAIL_ERROR		BIT(30)
#define REG_INMAIL_OP_REQUEST		BIT(31)

#define REG_OUTMAIL_CMD			0x3990c
#define REG_OUTMAIL_CMD_OPMODE_SHIFT	8
#define REG_OUTMAIL_CMD_OPMODE_MASK	GENMASK(11, 8)

#define REG_FW_STS			0x39944
#define REG_FW_STS_NVM_AUTH_DONE	BIT(31)
#define REG_FW_STS_CIO_RESET_REQ	BIT(30)
#define REG_FW_STS_ICM_EN_CPU		BIT(2)
#define REG_FW_STS_ICM_EN_INVERT	BIT(1)
#define REG_FW_STS_ICM_EN		BIT(0)

/* ICL NHI VSEC registers */

/* FW ready */
#define VS_CAP_9			0xc8
#define VS_CAP_9_FW_READY		BIT(31)
/* UUID */
#define VS_CAP_10			0xcc
#define VS_CAP_11			0xd0
/* LTR */
#define VS_CAP_15			0xe0
#define VS_CAP_16			0xe4
/* TBT2PCIe */
#define VS_CAP_18			0xec
#define VS_CAP_18_DONE			BIT(0)
/* PCIe2TBT */
#define VS_CAP_19			0xf0
#define VS_CAP_19_VALID			BIT(0)
#define VS_CAP_19_CMD_SHIFT		1
#define VS_CAP_19_CMD_MASK		GENMASK(7, 1)
/* Force power */
#define VS_CAP_22			0xfc
#define VS_CAP_22_FORCE_POWER		BIT(1)
#define VS_CAP_22_DMA_DELAY_MASK	GENMASK(31, 24)
#define VS_CAP_22_DMA_DELAY_SHIFT	24

/**
 * enum icl_lc_mailbox_cmd - ICL specific LC mailbox commands
 * @ICL_LC_GO2SX: Ask LC to enter Sx without wake
 * @ICL_LC_GO2SX_NO_WAKE: Ask LC to enter Sx with wake
 * @ICL_LC_PREPARE_FOR_RESET: Prepare LC for reset
 */
enum icl_lc_mailbox_cmd {
	ICL_LC_GO2SX = 0x02,
	ICL_LC_GO2SX_NO_WAKE = 0x03,
	ICL_LC_PREPARE_FOR_RESET = 0x21,
};

#endif

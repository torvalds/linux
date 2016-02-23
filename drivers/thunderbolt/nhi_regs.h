/*
 * Thunderbolt Cactus Ridge driver - NHI registers
 *
 * Copyright (c) 2014 Andreas Noever <andreas.noever@gmail.com>
 */

#ifndef DSL3510_REGS_H_
#define DSL3510_REGS_H_

#include <linux/types.h>

enum ring_flags {
	RING_FLAG_ISOCH_ENABLE = 1 << 27, /* TX only? */
	RING_FLAG_E2E_FLOW_CONTROL = 1 << 28,
	RING_FLAG_PCI_NO_SNOOP = 1 << 29,
	RING_FLAG_RAW = 1 << 30, /* ignore EOF/SOF mask, include checksum */
	RING_FLAG_ENABLE = 1 << 31,
};

enum ring_desc_flags {
	RING_DESC_ISOCH = 0x1, /* TX only? */
	RING_DESC_COMPLETED = 0x2, /* set by NHI */
	RING_DESC_POSTED = 0x4, /* always set this */
	RING_DESC_INTERRUPT = 0x8, /* request an interrupt on completion */
};

/**
 * struct ring_desc - TX/RX ring entry
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
 * 16 bytes per entry, one entry for every hop (REG_HOP_COUNT)
 * 00: physical pointer to an array of struct ring_desc
 * 08: ring tail (set by NHI)
 * 10: ring head (index of first non posted descriptor)
 * 12: descriptor count
 */
#define REG_TX_RING_BASE	0x00000

/*
 * 16 bytes per entry, one entry for every hop (REG_HOP_COUNT)
 * 00: physical pointer to an array of struct ring_desc
 * 08: ring head (index of first not posted descriptor)
 * 10: ring tail (set by NHI)
 * 12: descriptor count
 * 14: max frame sizes (anything larger than 0x100 has no effect)
 */
#define REG_RX_RING_BASE	0x08000

/*
 * 32 bytes per entry, one entry for every hop (REG_HOP_COUNT)
 * 00: enum_ring_flags
 * 04: isoch time stamp ?? (write 0)
 * ..: unknown
 */
#define REG_TX_OPTIONS_BASE	0x19800

/*
 * 32 bytes per entry, one entry for every hop (REG_HOP_COUNT)
 * 00: enum ring_flags
 *     If RING_FLAG_E2E_FLOW_CONTROL is set then bits 13-23 must be set to
 *     the corresponding TX hop id.
 * 04: EOF/SOF mask (ignored for RING_FLAG_RAW rings)
 * ..: unknown
 */
#define REG_RX_OPTIONS_BASE	0x29800

/*
 * three bitfields: tx, rx, rx overflow
 * Every bitfield contains one bit for every hop (REG_HOP_COUNT). Registers are
 * cleared on read. New interrupts are fired only after ALL registers have been
 * read (even those containing only disabled rings).
 */
#define REG_RING_NOTIFY_BASE	0x37800
#define RING_NOTIFY_REG_COUNT(nhi) ((31 + 3 * nhi->hop_count) / 32)

/*
 * two bitfields: rx, tx
 * Both bitfields contains one bit for every hop (REG_HOP_COUNT). To
 * enable/disable interrupts set/clear the corresponding bits.
 */
#define REG_RING_INTERRUPT_BASE	0x38200
#define RING_INTERRUPT_REG_COUNT(nhi) ((31 + 2 * nhi->hop_count) / 32)

/* The last 11 bits contain the number of hops supported by the NHI port. */
#define REG_HOP_COUNT		0x39640

#endif

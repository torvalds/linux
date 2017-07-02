/*
 * Thunderbolt driver - NHI registers
 *
 * Copyright (c) 2014 Andreas Noever <andreas.noever@gmail.com>
 */

#ifndef NHI_REGS_H_
#define NHI_REGS_H_

#include <linux/types.h>

#define NHI_MMIO_BAR 0

#define TBT_RING_MIN_NUM_BUFFERS	2
#define TBT_RING_MAX_FRAME_SIZE		(4 * 1024)

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

/**
 * struct tbt_buf_desc - TX/RX ring buffer descriptor.
 * This is same as struct ring_desc, but without the use of bitfields and
 * with explicit endianity.
 */
struct tbt_buf_desc {
	__le64 phys;
	__le32 attributes;
	__le32 time;
};

#define DESC_ATTR_LEN_SHIFT		0
#define DESC_ATTR_LEN_MASK		GENMASK(11, DESC_ATTR_LEN_SHIFT)
#define DESC_ATTR_EOF_SHIFT		12
#define DESC_ATTR_EOF_MASK		GENMASK(15, DESC_ATTR_EOF_SHIFT)
#define DESC_ATTR_SOF_SHIFT		16
#define DESC_ATTR_SOF_MASK		GENMASK(19, DESC_ATTR_SOF_SHIFT)
#define DESC_ATTR_TX_ISOCH_DMA_EN	BIT(20)	/* TX */
#define DESC_ATTR_RX_CRC_ERR		BIT(20)	/* RX after use */
#define DESC_ATTR_DESC_DONE		BIT(21)
#define DESC_ATTR_REQ_STS		BIT(22)	/* TX and RX before use */
#define DESC_ATTR_RX_BUF_OVRN_ERR	BIT(22)	/* RX after use */
#define DESC_ATTR_INT_EN		BIT(23)
#define DESC_ATTR_OFFSET_SHIFT		24
#define DESC_ATTR_OFFSET_MASK		GENMASK(31, DESC_ATTR_OFFSET_SHIFT)


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

#define REG_RING_STEP			16
#define REG_RING_PHYS_LO_OFFSET		0
#define REG_RING_PHYS_HI_OFFSET		4
#define REG_RING_CONS_PROD_OFFSET	8	/* cons - RO, prod - RW */
#define REG_RING_CONS_SHIFT		0
#define REG_RING_CONS_MASK		GENMASK(15, REG_RING_CONS_SHIFT)
#define REG_RING_PROD_SHIFT		16
#define REG_RING_PROD_MASK		GENMASK(31, REG_RING_PROD_SHIFT)
#define REG_RING_SIZE_OFFSET		12
#define REG_RING_SIZE_SHIFT		0
#define REG_RING_SIZE_MASK		GENMASK(15, REG_RING_SIZE_SHIFT)
#define REG_RING_BUF_SIZE_SHIFT		16
#define REG_RING_BUF_SIZE_MASK		GENMASK(27, REG_RING_BUF_SIZE_SHIFT)

#define TBT_RING_CONS_PROD_REG(iobase, ringbase, ringnumber) \
			      ((iobase) + (ringbase) + \
			      ((ringnumber) * REG_RING_STEP) + \
			      REG_RING_CONS_PROD_OFFSET)

#define TBT_REG_RING_PROD_EXTRACT(val) (((val) & REG_RING_PROD_MASK) >> \
				       REG_RING_PROD_SHIFT)

#define TBT_REG_RING_CONS_EXTRACT(val) (((val) & REG_RING_CONS_MASK) >> \
				       REG_RING_CONS_SHIFT)
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
#define REG_RX_OPTS_TX_E2E_HOP_ID_SHIFT	12
#define REG_RX_OPTS_TX_E2E_HOP_ID_MASK	\
				GENMASK(22, REG_RX_OPTS_TX_E2E_HOP_ID_SHIFT)
#define REG_RX_OPTS_MASK_OFFSET		4
#define REG_RX_OPTS_MASK_EOF_SHIFT	0
#define REG_RX_OPTS_MASK_EOF_MASK	GENMASK(15, REG_RX_OPTS_MASK_EOF_SHIFT)
#define REG_RX_OPTS_MASK_SOF_SHIFT	16
#define REG_RX_OPTS_MASK_SOF_MASK	GENMASK(31, REG_RX_OPTS_MASK_SOF_SHIFT)

#define REG_OPTS_STEP			32
#define REG_OPTS_E2E_EN			BIT(28)
#define REG_OPTS_RAW			BIT(30)
#define REG_OPTS_VALID			BIT(31)

/*
 * three bitfields: tx, rx, rx overflow
 * Every bitfield contains one bit for every hop (REG_HOP_COUNT). Registers are
 * cleared on read. New interrupts are fired only after ALL registers have been
 * read (even those containing only disabled rings).
 */
#define REG_RING_NOTIFY_BASE	0x37800
#define RING_NOTIFY_REG_COUNT(nhi) ((31 + 3 * nhi->hop_count) / 32)
#define REG_RING_NOTIFY_STEP	4

/*
 * two bitfields: rx, tx
 * Both bitfields contains one bit for every hop (REG_HOP_COUNT). To
 * enable/disable interrupts set/clear the corresponding bits.
 */
#define REG_RING_INTERRUPT_BASE	0x38200
#define RING_INTERRUPT_REG_COUNT(nhi) ((31 + 2 * nhi->hop_count) / 32)
#define REG_RING_INT_TX_PROCESSED(ring_num)		BIT(ring_num)
#define REG_RING_INT_RX_PROCESSED(ring_num, num_paths)	BIT((ring_num) + \
							    (num_paths))
#define RING_INT_DISABLE(base, val) iowrite32( \
			ioread32((base) + REG_RING_INTERRUPT_BASE) & ~(val), \
			(base) + REG_RING_INTERRUPT_BASE)
#define RING_INT_ENABLE(base, val) iowrite32( \
			ioread32((base) + REG_RING_INTERRUPT_BASE) | (val), \
			(base) + REG_RING_INTERRUPT_BASE)
#define RING_INT_DISABLE_TX(base, ring_num) \
	RING_INT_DISABLE(base, REG_RING_INT_TX_PROCESSED(ring_num))
#define RING_INT_DISABLE_RX(base, ring_num, num_paths) \
	RING_INT_DISABLE(base, REG_RING_INT_RX_PROCESSED(ring_num, num_paths))
#define RING_INT_ENABLE_TX(base, ring_num) \
	RING_INT_ENABLE(base, REG_RING_INT_TX_PROCESSED(ring_num))
#define RING_INT_ENABLE_RX(base, ring_num, num_paths) \
	RING_INT_ENABLE(base, REG_RING_INT_RX_PROCESSED(ring_num, num_paths))
#define RING_INT_DISABLE_TX_RX(base, ring_num, num_paths) \
	RING_INT_DISABLE(base, REG_RING_INT_TX_PROCESSED(ring_num) | \
			       REG_RING_INT_RX_PROCESSED(ring_num, num_paths))

#define REG_RING_INTERRUPT_STEP	4

#define REG_INT_THROTTLING_RATE	0x38c00
#define REG_INT_THROTTLING_RATE_STEP	4
#define NUM_INT_VECTORS			16

#define REG_INT_VEC_ALLOC_BASE	0x38c40
#define REG_INT_VEC_ALLOC_STEP		4
#define REG_INT_VEC_ALLOC_FIELD_BITS	4
#define REG_INT_VEC_ALLOC_FIELD_MASK	(BIT(REG_INT_VEC_ALLOC_FIELD_BITS) - 1)
#define REG_INT_VEC_ALLOC_PER_REG	((BITS_PER_BYTE * sizeof(u32)) / \
					 REG_INT_VEC_ALLOC_FIELD_BITS)

/* The last 11 bits contain the number of hops supported by the NHI port. */
#define REG_HOP_COUNT		0x39640
#define REG_HOP_COUNT_TOTAL_PATHS_MASK	GENMASK(10, 0)

#define REG_HOST_INTERFACE_RST	0x39858

#define REG_DMA_MISC		0x39864
#define REG_DMA_MISC_INT_AUTO_CLEAR	BIT(2)

#endif

/* SPDX-License-Identifier: GPL-2.0 */
/* Written 2000 by Andi Kleen */
#ifndef _ASM_X86_DESC_DEFS_H
#define _ASM_X86_DESC_DEFS_H

/*
 * Segment descriptor structure definitions, usable from both x86_64 and i386
 * archs.
 */

/*
 * Low-level interface mapping flags/field names to bits
 */

/* Flags for _DESC_S (non-system) descriptors */
#define _DESC_ACCESSED		0x0001
#define _DESC_DATA_WRITABLE	0x0002
#define _DESC_CODE_READABLE	0x0002
#define _DESC_DATA_EXPAND_DOWN	0x0004
#define _DESC_CODE_CONFORMING	0x0004
#define _DESC_CODE_EXECUTABLE	0x0008

/* Common flags */
#define _DESC_S			0x0010
#define _DESC_DPL(dpl)		((dpl) << 5)
#define _DESC_PRESENT		0x0080

#define _DESC_LONG_CODE		0x2000
#define _DESC_DB		0x4000
#define _DESC_GRANULARITY_4K	0x8000

/* System descriptors have a numeric "type" field instead of flags */
#define _DESC_SYSTEM(code)	(code)

/*
 * High-level interface mapping intended usage to low-level combinations
 * of flags
 */

#define _DESC_DATA		(_DESC_S | _DESC_PRESENT | _DESC_ACCESSED | \
				 _DESC_DATA_WRITABLE)
#define _DESC_CODE		(_DESC_S | _DESC_PRESENT | _DESC_ACCESSED | \
				 _DESC_CODE_READABLE | _DESC_CODE_EXECUTABLE)

#define DESC_DATA16		(_DESC_DATA)
#define DESC_CODE16		(_DESC_CODE)

#define DESC_DATA32		(_DESC_DATA | _DESC_GRANULARITY_4K | _DESC_DB)
#define DESC_DATA32_BIOS	(_DESC_DATA | _DESC_DB)

#define DESC_CODE32		(_DESC_CODE | _DESC_GRANULARITY_4K | _DESC_DB)
#define DESC_CODE32_BIOS	(_DESC_CODE | _DESC_DB)

#define DESC_TSS32		(_DESC_SYSTEM(9) | _DESC_PRESENT)

#define DESC_DATA64		(_DESC_DATA | _DESC_GRANULARITY_4K | _DESC_DB)
#define DESC_CODE64		(_DESC_CODE | _DESC_GRANULARITY_4K | _DESC_LONG_CODE)

#define DESC_USER		(_DESC_DPL(3))

#ifndef __ASSEMBLY__

#include <linux/types.h>

/* 8 byte segment descriptor */
struct desc_struct {
	u16	limit0;
	u16	base0;
	u16	base1: 8, type: 4, s: 1, dpl: 2, p: 1;
	u16	limit1: 4, avl: 1, l: 1, d: 1, g: 1, base2: 8;
} __attribute__((packed));

#define GDT_ENTRY_INIT(flags, base, limit)			\
	{							\
		.limit0		= ((limit) >>  0) & 0xFFFF,	\
		.limit1		= ((limit) >> 16) & 0x000F,	\
		.base0		= ((base)  >>  0) & 0xFFFF,	\
		.base1		= ((base)  >> 16) & 0x00FF,	\
		.base2		= ((base)  >> 24) & 0x00FF,	\
		.type		= ((flags) >>  0) & 0x000F,	\
		.s		= ((flags) >>  4) & 0x0001,	\
		.dpl		= ((flags) >>  5) & 0x0003,	\
		.p		= ((flags) >>  7) & 0x0001,	\
		.avl		= ((flags) >> 12) & 0x0001,	\
		.l		= ((flags) >> 13) & 0x0001,	\
		.d		= ((flags) >> 14) & 0x0001,	\
		.g		= ((flags) >> 15) & 0x0001,	\
	}

enum {
	GATE_INTERRUPT = 0xE,
	GATE_TRAP = 0xF,
	GATE_CALL = 0xC,
	GATE_TASK = 0x5,
};

enum {
	DESC_TSS = 0x9,
	DESC_LDT = 0x2,
	DESCTYPE_S = 0x10,	/* !system */
};

/* LDT or TSS descriptor in the GDT. */
struct ldttss_desc {
	u16	limit0;
	u16	base0;

	u16	base1 : 8, type : 5, dpl : 2, p : 1;
	u16	limit1 : 4, zero0 : 3, g : 1, base2 : 8;
#ifdef CONFIG_X86_64
	u32	base3;
	u32	zero1;
#endif
} __attribute__((packed));

typedef struct ldttss_desc ldt_desc;
typedef struct ldttss_desc tss_desc;

struct idt_bits {
	u16		ist	: 3,
			zero	: 5,
			type	: 5,
			dpl	: 2,
			p	: 1;
} __attribute__((packed));

struct idt_data {
	unsigned int	vector;
	unsigned int	segment;
	struct idt_bits	bits;
	const void	*addr;
};

struct gate_struct {
	u16		offset_low;
	u16		segment;
	struct idt_bits	bits;
	u16		offset_middle;
#ifdef CONFIG_X86_64
	u32		offset_high;
	u32		reserved;
#endif
} __attribute__((packed));

typedef struct gate_struct gate_desc;

#ifndef _SETUP
static inline unsigned long gate_offset(const gate_desc *g)
{
#ifdef CONFIG_X86_64
	return g->offset_low | ((unsigned long)g->offset_middle << 16) |
		((unsigned long) g->offset_high << 32);
#else
	return g->offset_low | ((unsigned long)g->offset_middle << 16);
#endif
}

static inline unsigned long gate_segment(const gate_desc *g)
{
	return g->segment;
}
#endif

struct desc_ptr {
	unsigned short size;
	unsigned long address;
} __attribute__((packed)) ;

#endif /* !__ASSEMBLY__ */

/* Boot IDT definitions */
#define	BOOT_IDT_ENTRIES	32

/* Access rights as returned by LAR */
#define AR_TYPE_RODATA		(0 * (1 << 9))
#define AR_TYPE_RWDATA		(1 * (1 << 9))
#define AR_TYPE_RODATA_EXPDOWN	(2 * (1 << 9))
#define AR_TYPE_RWDATA_EXPDOWN	(3 * (1 << 9))
#define AR_TYPE_XOCODE		(4 * (1 << 9))
#define AR_TYPE_XRCODE		(5 * (1 << 9))
#define AR_TYPE_XOCODE_CONF	(6 * (1 << 9))
#define AR_TYPE_XRCODE_CONF	(7 * (1 << 9))
#define AR_TYPE_MASK		(7 * (1 << 9))

#define AR_DPL0			(0 * (1 << 13))
#define AR_DPL3			(3 * (1 << 13))
#define AR_DPL_MASK		(3 * (1 << 13))

#define AR_A			(1 << 8)   /* "Accessed" */
#define AR_S			(1 << 12)  /* If clear, "System" segment */
#define AR_P			(1 << 15)  /* "Present" */
#define AR_AVL			(1 << 20)  /* "AVaiLable" (no HW effect) */
#define AR_L			(1 << 21)  /* "Long mode" for code segments */
#define AR_DB			(1 << 22)  /* D/B, effect depends on type */
#define AR_G			(1 << 23)  /* "Granularity" (limit in pages) */

#endif /* _ASM_X86_DESC_DEFS_H */

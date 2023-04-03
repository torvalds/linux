/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Copyright (c) 2023 Rockchip Electronics Co., Ltd.
 */

#ifndef RKPM_HELPERS_H
#define RKPM_HELPERS_H

#define REG_MSK_SHIFT	16

#ifndef BIT
#define BIT(nr)			(1U << (nr))
#endif

#ifndef WMSK_BIT
#define WMSK_BIT(nr)		BIT((nr) + REG_MSK_SHIFT)
#endif

/* set one bit with write mask */
#ifndef BIT_WITH_WMSK
#define BIT_WITH_WMSK(nr)	(BIT(nr) | WMSK_BIT(nr))
#endif

#ifndef BITS_SHIFT
#define BITS_SHIFT(bits, shift) ((bits) << (shift))
#endif

#ifndef BITS_WMSK
#define BITS_WMSK(msk, shift) \
		((msk) << ((shift) + REG_MSK_SHIFT))
#endif

#ifndef BITS_WITH_WMASK
#define BITS_WITH_WMASK(bits, msk, shift) \
	(BITS_SHIFT(bits, shift) | BITS_SHIFT(msk, ((shift) + REG_MSK_SHIFT)))
#endif

#ifndef BIT_SET_WMSK
#define BIT_SET_WMSK(nr)		BIT_WITH_WMSK(nr)
#endif

#ifndef BIT_CLR_WMSK
#define BIT_CLR_WMSK(nr)		WMSK_BIT(nr)
#endif

#ifndef WITH_16BITS_WMSK
#define WITH_16BITS_WMSK(bits)		(0xffff0000 | (bits))
#endif

#define REG_REGION(_start, _end, _stride, _base, _wmsk) \
	.start = (_start),				\
	.end = (_end),					\
	.stride = (_stride),				\
	.wmsk = (_wmsk),				\
	.base = (_base),

struct reg_region {
	void __iomem **base;
	u32 start;
	u32 end;
	u32 stride;
	u32 wmsk;
	u32 *buf;
};

void rkpm_alloc_region_mem(struct reg_region *rgns, u32 rgn_num);
void rkpm_region_mem_init(u32 size);
void rkpm_reg_rgn_save(struct reg_region *rgns, u32 rgn_num);
void rkpm_reg_rgn_restore(struct reg_region *rgns, u32 rgn_num);
void rkpm_reg_rgn_restore_reverse(struct reg_region *rgns, u32 rgn_num);
void rkpm_dump_reg_rgns(struct reg_region *rgns, u32 rgn_num);

void rkpm_printch(int c);
void rkpm_printstr(const char *s);
void rkpm_printhex(u32 hex);
void rkpm_printdec(int dec);
void rkpm_regs_dump(void __iomem *base,
		    u32 start_offset,
		    u32 end_offset,
		    u32 stride);

void rkpm_raw_udelay(int us);
#endif

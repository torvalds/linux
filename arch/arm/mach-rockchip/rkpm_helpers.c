// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2023 Rockchip Electronics Co., Ltd.
 */

#include <asm/arch_timer.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/slab.h>

#include "rkpm_helpers.h"

/* REG region */
#define RGN_LEN(_rgn)		(((_rgn)->end - (_rgn)->start) / (_rgn)->stride + 1)

static u32 *region_mem;
static u32 region_mem_size;
static int region_mem_idx;

static int alloc_region_mem(u32 *buf, int max_len,
			    struct reg_region *rgns, u32 rgn_num)
{
	int i;
	int total_len = 0, len = 0;
	struct reg_region *r = rgns;

	if (!buf || !rgns) {
		pr_err("%s invalid parameter\n", __func__);
		return 0;
	}

	for (i = 0; i < rgn_num; i++, r++) {
		if (total_len < max_len)
			r->buf = &buf[total_len];

		len = RGN_LEN(r);
		total_len += len;
	}

	if (len >= max_len) {
		pr_err("%s The buffer remain length:%d is too small for region:0x%x, at least %d\n",
		       __func__, max_len, rgns[0].start, total_len);
		return -ENOMEM;
	}

	return total_len;
}

/**
 * Alloc memory to reg_region->buf from region_mem.
 * @rgns - struct reg_region array.
 * @rgn_num - struct reg_region array length.
 */
void rkpm_alloc_region_mem(struct reg_region *rgns, u32 rgn_num)
{
	int max_len = 0, len;

	max_len = region_mem_size / sizeof(u32) -
		  region_mem_idx;

	len = alloc_region_mem(region_mem + region_mem_idx, max_len,
			       rgns, rgn_num);

	region_mem_idx += len;
}

void rkpm_region_mem_init(u32 size)
{
	if (!size) {
		pr_err("%s invalid param\n", __func__);
		return;
	}

	region_mem = kmalloc(size, GFP_KERNEL);
	if (!region_mem) {
		pr_err("%s malloc region memory (0x%x) err\n", __func__, size);
		return;
	}

	region_mem_size = size;
}

/**
 * Save (reg_region->start ~ reg_region->end) to reg_region->buf.
 * @rgns - struct reg_region array.
 * @rgn_num - struct reg_region array length.
 */
void rkpm_reg_rgn_save(struct reg_region *rgns, u32 rgn_num)
{
	struct reg_region *r;
	u8 *addr;
	u8 *start, *end;
	int i, j;

	for (i = 0; i < rgn_num; i++) {
		r = &rgns[i];
		start = (char *)(*r->base) + r->start;
		end = (char *)(*r->base) + r->end;
		for (j = 0, addr = start; addr <= end; addr += r->stride, j++)
			r->buf[j] = readl_relaxed(addr);
	}
}

/**
 * Restore reg_region->buf to (reg_region->start ~ reg_region->end).
 * @rgns - struct reg_region array.
 * @rgn_num - struct reg_region array length.
 */
void rkpm_reg_rgn_restore(struct reg_region *rgns, u32 rgn_num)
{
	struct reg_region *r;
	u8 *addr;
	u8 *start, *end;
	int i, j;

	for (i = 0; i < rgn_num; i++) {
		r = &rgns[i];
		start = (char *)(*r->base) + r->start;
		end = (char *)(*r->base) + r->end;
		for (j = 0, addr = start; addr <= end; addr += r->stride, j++)
			writel_relaxed(r->buf[j] | r->wmsk, addr);
	}
}

void rkpm_reg_rgn_restore_reverse(struct reg_region *rgns, u32 rgn_num)
{
	struct reg_region *r;
	u8 *addr;
	u8 *start, *end;
	int i, j;

	for (i = rgn_num - 1; i >= 0; i--) {
		r = &rgns[i];
		start = (char *)(*r->base) + r->start;
		end = (char *)(*r->base) + r->end;
		j = RGN_LEN(r) - 1;
		for (addr = end; addr >= start; addr -= r->stride, j--)
			writel_relaxed(r->buf[j] | r->wmsk, addr);
	}
}

/**
 * Dump reg regions
 * @rgns - struct reg_region array.
 * @rgn_num - struct reg_region array length.
 */
void rkpm_dump_reg_rgns(struct reg_region *rgns, u32 rgn_num)
{
	struct reg_region *r;
	int i;

	for (i = 0; i < rgn_num; i++) {
		r = &rgns[i];
		rkpm_regs_dump(*r->base, r->start, r->end, r->stride);
	}
}

#pragma weak rkpm_printch
void rkpm_printch(int c)
{
}

void rkpm_printstr(const char *s)
{
	while (*s) {
		rkpm_printch(*s);
		s++;
	}
}

void rkpm_printhex(u32 hex)
{
	u8 i = 8;
	u8 c;

	rkpm_printch('0');
	rkpm_printch('x');
	while (i--) {
		c = (hex & 0xf0000000) >> 28;
		rkpm_printch(c < 0xa ? c + '0' : c - 0xa + 'a');
		hex <<= 4;
	}
}

void rkpm_printdec(int dec)
{
	int i, tmp = dec;

	if (dec < 0) {
		rkpm_printch('-');
		tmp = -dec;
		dec = -dec;
	}

	for (i = 1; tmp / 10; tmp /= 10, i *= 10)
		;

	for (; i >= 1; i /= 10) {
		rkpm_printch('0' + (char)(dec / i));
		dec %= i;
	}
}

void rkpm_regs_dump(void __iomem *base,
		    u32 start_offset,
		    u32 end_offset,
		    u32 stride)
{
	u32 i;

	for (i = start_offset; i <= end_offset; i += stride) {
		if ((i - start_offset) % 16 == 0) {
			rkpm_printch('\n');
			rkpm_printhex((u32)base + i);
			rkpm_printch(':');
			rkpm_printch(' ');
			rkpm_printch(' ');
			rkpm_printch(' ');
			rkpm_printch(' ');
		}
		rkpm_printhex(readl_relaxed(base + i));
		rkpm_printch(' ');
		rkpm_printch(' ');
		rkpm_printch(' ');
		rkpm_printch(' ');
	}
	rkpm_printch('\n');
}

void rkpm_raw_udelay(int us)
{
	u64 cur_cnt = __arch_counter_get_cntpct();
	u64 del = us * 24;

	while (__arch_counter_get_cntpct() - cur_cnt < del)
		;
}

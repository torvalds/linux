/*
 * Relocate bridge's register base and call board specific routine.
 *
 * Author: Mark A. Greer <source@mvista.com>
 *
 * 2005 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <asm/io.h>
#include <asm/mv64x60_defs.h>

extern struct bi_record *decompress_kernel(unsigned long load_addr,
	int num_words, unsigned long cksum);


u32 size_reg[MV64x60_CPU2MEM_WINDOWS] = {
	MV64x60_CPU2MEM_0_SIZE, MV64x60_CPU2MEM_1_SIZE,
	MV64x60_CPU2MEM_2_SIZE, MV64x60_CPU2MEM_3_SIZE
};

/* Read mem ctlr to get the amount of mem in system */
unsigned long
mv64360_get_mem_size(void)
{
	u32	enables, i, v;
	u32	mem = 0;

	enables = in_le32((void __iomem *)CONFIG_MV64X60_NEW_BASE +
		MV64360_CPU_BAR_ENABLE) & 0xf;

	for (i=0; i<MV64x60_CPU2MEM_WINDOWS; i++)
		if (!(enables & (1<<i))) {
			v = in_le32((void __iomem *)CONFIG_MV64X60_NEW_BASE
				+ size_reg[i]) & 0xffff;
			v = (v + 1) << 16;
			mem += v;
		}

	return mem;
}

void
mv64x60_move_base(void __iomem *old_base, void __iomem *new_base)
{
	u32	bits, mask, b;

	if (old_base != new_base) {
#ifdef CONFIG_GT64260
		bits = 12;
		mask = 0x07000000;
#else /* Must be mv64[34]60 */
		bits = 16;
		mask = 0x03000000;
#endif
		b = in_le32(old_base + MV64x60_INTERNAL_SPACE_DECODE);
		b &= mask;
		b |= ((u32)new_base >> (32 - bits));
		out_le32(old_base + MV64x60_INTERNAL_SPACE_DECODE, b);

		__asm__ __volatile__("sync");

		/* Wait for change to happen (in accordance with the manual) */
		while (in_le32(new_base + MV64x60_INTERNAL_SPACE_DECODE) != b);
	}
}

void __attribute__ ((weak))
mv64x60_board_init(void __iomem *old_base, void __iomem *new_base)
{
}

void *
load_kernel(unsigned long load_addr, int num_words, unsigned long cksum,
		void *ign1, void *ign2)
{
	mv64x60_move_base((void __iomem *)CONFIG_MV64X60_BASE,
		(void __iomem *)CONFIG_MV64X60_NEW_BASE);
	mv64x60_board_init((void __iomem *)CONFIG_MV64X60_BASE,
		(void __iomem *)CONFIG_MV64X60_NEW_BASE);
	return decompress_kernel(load_addr, num_words, cksum);
}

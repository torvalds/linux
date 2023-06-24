/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2013 Imagination Technologies Ltd.
 */

#include <linux/kernel.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <asm/cpu.h>
#include <asm/debug.h>
#include <asm/mipsregs.h>

static void build_segment_config(char *str, unsigned int cfg)
{
	unsigned int am;
	static const char * const am_str[] = {
		"UK", "MK", "MSK", "MUSK", "MUSUK", "USK",
		"RSRVD", "UUSK"};

	/* Segment access mode. */
	am = (cfg & MIPS_SEGCFG_AM) >> MIPS_SEGCFG_AM_SHIFT;
	str += sprintf(str, "%-5s", am_str[am]);

	/*
	 * Access modes MK, MSK and MUSK are mapped segments. Therefore
	 * there is no direct physical address mapping unless it becomes
	 * unmapped uncached at error level due to EU.
	 */
	if ((am == 0) || (am > 3) || (cfg & MIPS_SEGCFG_EU))
		str += sprintf(str, "         %03lx",
			((cfg & MIPS_SEGCFG_PA) >> MIPS_SEGCFG_PA_SHIFT));
	else
		str += sprintf(str, "         UND");

	if ((am == 0) || (am > 3))
		str += sprintf(str, "         %01ld",
			((cfg & MIPS_SEGCFG_C) >> MIPS_SEGCFG_C_SHIFT));
	else
		str += sprintf(str, "         U");

	/* Exception configuration. */
	str += sprintf(str, "       %01ld\n",
		((cfg & MIPS_SEGCFG_EU) >> MIPS_SEGCFG_EU_SHIFT));
}

static int segments_show(struct seq_file *m, void *v)
{
	unsigned int segcfg;
	char str[42];

	seq_puts(m, "Segment   Virtual    Size   Access Mode   Physical   Caching   EU\n");
	seq_puts(m, "-------   -------    ----   -----------   --------   -------   --\n");

	segcfg = read_c0_segctl0();
	build_segment_config(str, segcfg);
	seq_printf(m, "   0      e0000000   512M      %s", str);

	segcfg >>= 16;
	build_segment_config(str, segcfg);
	seq_printf(m, "   1      c0000000   512M      %s", str);

	segcfg = read_c0_segctl1();
	build_segment_config(str, segcfg);
	seq_printf(m, "   2      a0000000   512M      %s", str);

	segcfg >>= 16;
	build_segment_config(str, segcfg);
	seq_printf(m, "   3      80000000   512M      %s", str);

	segcfg = read_c0_segctl2();
	build_segment_config(str, segcfg);
	seq_printf(m, "   4      40000000    1G       %s", str);

	segcfg >>= 16;
	build_segment_config(str, segcfg);
	seq_printf(m, "   5      00000000    1G       %s\n", str);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(segments);

static int __init segments_info(void)
{
	if (cpu_has_segments)
		debugfs_create_file("segments", S_IRUGO, mips_debugfs_dir, NULL,
				    &segments_fops);
	return 0;
}

device_initcall(segments_info);

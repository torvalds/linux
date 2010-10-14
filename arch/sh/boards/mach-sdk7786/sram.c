/*
 * SDK7786 FPGA SRAM Support.
 *
 * Copyright (C) 2010  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/io.h>
#include <linux/string.h>
#include <mach/fpga.h>
#include <asm/sram.h>
#include <asm/sizes.h>

static int __init fpga_sram_init(void)
{
	unsigned long phys;
	unsigned int area;
	void __iomem *vaddr;
	int ret;
	u16 data;

	/* Enable FPGA SRAM */
	data = fpga_read_reg(LCLASR);
	data |= LCLASR_FRAMEN;
	fpga_write_reg(data, LCLASR);

	/*
	 * FPGA_SEL determines the area mapping
	 */
	area = (data & LCLASR_FPGA_SEL_MASK) >> LCLASR_FPGA_SEL_SHIFT;
	if (unlikely(area == LCLASR_AREA_MASK)) {
		pr_err("FPGA memory unmapped.\n");
		return -ENXIO;
	}

	/*
	 * The memory itself occupies a 2KiB range at the top of the area
	 * immediately below the system registers.
	 */
	phys = (area << 26) + SZ_64M - SZ_4K;

	/*
	 * The FPGA SRAM resides in translatable physical space, so set
	 * up a mapping prior to inserting it in to the pool.
	 */
	vaddr = ioremap(phys, SZ_2K);
	if (unlikely(!vaddr)) {
		pr_err("Failed remapping FPGA memory.\n");
		return -ENXIO;
	}

	pr_info("Adding %dKiB of FPGA memory at 0x%08lx-0x%08lx "
		"(area %d) to pool.\n",
		SZ_2K >> 10, phys, phys + SZ_2K - 1, area);

	ret = gen_pool_add(sram_pool, (unsigned long)vaddr, SZ_2K, -1);
	if (unlikely(ret < 0)) {
		pr_err("Failed adding memory\n");
		iounmap(vaddr);
		return ret;
	}

	return 0;
}
postcore_initcall(fpga_sram_init);

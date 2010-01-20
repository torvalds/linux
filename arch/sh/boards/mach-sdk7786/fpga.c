/*
 * SDK7786 FPGA Support.
 *
 * Copyright (C) 2010  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/io.h>
#include <linux/bcd.h>
#include <mach/fpga.h>

#define FPGA_REGS_BASE	0x07fff800
#define FPGA_REGS_SIZE	0x490

void __iomem *sdk7786_fpga_base;

void __init sdk7786_fpga_init(void)
{
	u16 version, date;

	sdk7786_fpga_base = ioremap_nocache(FPGA_REGS_BASE, FPGA_REGS_SIZE);
	if (unlikely(!sdk7786_fpga_base)) {
		panic("FPGA remapping failed.\n");
		return;
	}

	version = fpga_read_reg(FPGAVR);
	date = fpga_read_reg(FPGADR);

	pr_info("\tFPGA version:\t%d.%d (built on %d/%d/%d)\n",
		bcd2bin(version >> 8) & 0xf, bcd2bin(version & 0xf),
		((date >> 12) & 0xf) + 2000,
		(date >> 8) & 0xf, bcd2bin(date & 0xff));
}

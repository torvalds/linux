/*
 * Bock-W board support
 *
 * Copyright (C) 2013  Renesas Solutions Corp.
 * Copyright (C) 2013  Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/of_platform.h>
#include <mach/common.h>
#include <mach/r8a7778.h>
#include <asm/mach/arch.h>

/*
 *	see board-bock.c for checking detail of dip-switch
 */

#define FPGA	0x18200000
#define IRQ0MR	0x30
#define COMCTLR	0x101c
static void __init bockw_init(void)
{
	void __iomem *fpga;

	r8a7778_clock_init();
	r8a7778_init_irq_extpin_dt(1);
	r8a7778_add_dt_devices();

	fpga = ioremap_nocache(FPGA, SZ_1M);
	if (fpga) {
		/*
		 * CAUTION
		 *
		 * IRQ0/1 is cascaded interrupt from FPGA.
		 * it should be cared in the future
		 * Now, it is assuming IRQ0 was used only from SMSC.
		 */
		u16 val = ioread16(fpga + IRQ0MR);
		val &= ~(1 << 4); /* enable SMSC911x */
		iowrite16(val, fpga + IRQ0MR);

		iounmap(fpga);
	}

	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
}

static const char *bockw_boards_compat_dt[] __initdata = {
	"renesas,bockw-reference",
	NULL,
};

DT_MACHINE_START(BOCKW_DT, "bockw")
	.init_early	= r8a7778_init_delay,
	.init_irq	= r8a7778_init_irq_dt,
	.init_machine	= bockw_init,
	.dt_compat	= bockw_boards_compat_dt,
MACHINE_END

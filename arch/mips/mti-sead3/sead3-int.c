/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2012 MIPS Technologies, Inc.  All rights reserved.
 */
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/io.h>

#include <asm/gic.h>
#include <asm/irq_cpu.h>
#include <asm/setup.h>

#include <asm/mips-boards/sead3int.h>

#define SEAD_CONFIG_GIC_PRESENT_SHF	1
#define SEAD_CONFIG_GIC_PRESENT_MSK	(1 << SEAD_CONFIG_GIC_PRESENT_SHF)
#define SEAD_CONFIG_BASE		0x1b100110
#define SEAD_CONFIG_SIZE		4

static unsigned long sead3_config_reg;

/*
 * This table defines the setup for each external GIC interrupt. It is
 * indexed by interrupt number.
 */
#define GIC_CPU_NMI GIC_MAP_TO_NMI_MSK
static struct gic_intr_map gic_intr_map[GIC_NUM_INTRS] = {
	{ 0, GIC_CPU_INT4, GIC_POL_POS, GIC_TRIG_LEVEL, GIC_FLAG_TRANSPARENT },
	{ 0, GIC_CPU_INT3, GIC_POL_POS, GIC_TRIG_LEVEL, GIC_FLAG_TRANSPARENT },
	{ 0, GIC_CPU_INT2, GIC_POL_POS, GIC_TRIG_LEVEL, GIC_FLAG_TRANSPARENT },
	{ 0, GIC_CPU_INT2, GIC_POL_POS, GIC_TRIG_LEVEL, GIC_FLAG_TRANSPARENT },
	{ 0, GIC_CPU_INT1, GIC_POL_POS, GIC_TRIG_LEVEL, GIC_FLAG_TRANSPARENT },
	{ 0, GIC_CPU_INT0, GIC_POL_POS, GIC_TRIG_LEVEL, GIC_FLAG_TRANSPARENT },
	{ 0, GIC_CPU_INT0, GIC_POL_POS, GIC_TRIG_LEVEL, GIC_FLAG_TRANSPARENT },
	{ 0, GIC_CPU_INT0, GIC_POL_POS, GIC_TRIG_LEVEL, GIC_FLAG_TRANSPARENT },
	{ 0, GIC_CPU_INT0, GIC_POL_POS, GIC_TRIG_LEVEL, GIC_FLAG_TRANSPARENT },
	{ GIC_UNUSED, GIC_UNUSED, GIC_UNUSED, GIC_UNUSED, GIC_UNUSED },
	{ GIC_UNUSED, GIC_UNUSED, GIC_UNUSED, GIC_UNUSED, GIC_UNUSED },
	{ GIC_UNUSED, GIC_UNUSED, GIC_UNUSED, GIC_UNUSED, GIC_UNUSED },
	{ GIC_UNUSED, GIC_UNUSED, GIC_UNUSED, GIC_UNUSED, GIC_UNUSED },
	{ GIC_UNUSED, GIC_UNUSED, GIC_UNUSED, GIC_UNUSED, GIC_UNUSED },
	{ GIC_UNUSED, GIC_UNUSED, GIC_UNUSED, GIC_UNUSED, GIC_UNUSED },
	{ GIC_UNUSED, GIC_UNUSED, GIC_UNUSED, GIC_UNUSED, GIC_UNUSED },
};

asmlinkage void plat_irq_dispatch(void)
{
	unsigned int pending = read_c0_cause() & read_c0_status() & ST0_IM;
	int irq;

	irq = (fls(pending) - CAUSEB_IP - 1);
	if (irq >= 0)
		do_IRQ(MIPS_CPU_IRQ_BASE + irq);
	else
		spurious_interrupt();
}

void __init arch_init_irq(void)
{
	int i;

	if (!cpu_has_veic) {
		mips_cpu_irq_init();

		if (cpu_has_vint) {
			/* install generic handler */
			for (i = 0; i < 8; i++)
				set_vi_handler(i, plat_irq_dispatch);
		}
	}

	sead3_config_reg = (unsigned long)ioremap_nocache(SEAD_CONFIG_BASE,
		SEAD_CONFIG_SIZE);
	gic_present = (REG32(sead3_config_reg) & SEAD_CONFIG_GIC_PRESENT_MSK) >>
		SEAD_CONFIG_GIC_PRESENT_SHF;
	pr_info("GIC: %spresent\n", (gic_present) ? "" : "not ");
	pr_info("EIC: %s\n",
		(current_cpu_data.options & MIPS_CPU_VEIC) ?  "on" : "off");

	if (gic_present)
		gic_init(GIC_BASE_ADDR, GIC_ADDRSPACE_SZ, gic_intr_map,
			ARRAY_SIZE(gic_intr_map), MIPS_GIC_IRQ_BASE);
}


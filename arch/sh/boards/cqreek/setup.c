/* $Id: setup.c,v 1.5 2003/08/04 01:51:58 lethal Exp $
 *
 * arch/sh/kernel/setup_cqreek.c
 *
 * Copyright (C) 2000  Niibe Yutaka
 *
 * CqREEK IDE/ISA Bridge Support.
 *
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/irq.h>

#include <asm/mach/cqreek.h>
#include <asm/machvec.h>
#include <asm/io.h>
#include <asm/io_generic.h>
#include <asm/irq.h>
#include <asm/rtc.h>

#define IDE_OFFSET 0xA4000000UL
#define ISA_OFFSET 0xA4A00000UL

const char *get_system_type(void)
{
	return "CqREEK";
}

static unsigned long cqreek_port2addr(unsigned long port)
{
	if (0x0000<=port && port<=0x0040)
		return IDE_OFFSET + port;
	if ((0x01f0<=port && port<=0x01f7) || port == 0x03f6)
		return IDE_OFFSET + port;

	return ISA_OFFSET + port;
}

/*
 * The Machine Vector
 */
struct sh_machine_vector mv_cqreek __initmv = {
#if defined(CONFIG_CPU_SH4)
	.mv_nr_irqs		= 48,
#elif defined(CONFIG_CPU_SUBTYPE_SH7708)
	.mv_nr_irqs		= 32,
#elif defined(CONFIG_CPU_SUBTYPE_SH7709)
	.mv_nr_irqs		= 61,
#endif

	.mv_init_irq		= init_cqreek_IRQ,

	.mv_isa_port2addr	= cqreek_port2addr,
};
ALIAS_MV(cqreek)

/*
 * Initialize the board
 */
void __init platform_setup(void)
{
	int i;
/* udelay is not available at setup time yet... */
#define DELAY() do {for (i=0; i<10000; i++) ctrl_inw(0xa0000000);} while(0)

	if ((inw (BRIDGE_FEATURE) & 1)) { /* We have IDE interface */
		outw_p(0, BRIDGE_IDE_INTR_LVL);
		outw_p(0, BRIDGE_IDE_INTR_MASK);

		outw_p(0, BRIDGE_IDE_CTRL);
		DELAY();

		outw_p(0x8000, BRIDGE_IDE_CTRL);
		DELAY();

		outw_p(0xffff, BRIDGE_IDE_INTR_STAT); /* Clear interrupt status */
		outw_p(0x0f-14, BRIDGE_IDE_INTR_LVL); /* Use 14 IPR */
		outw_p(1, BRIDGE_IDE_INTR_MASK); /* Enable interrupt */
		cqreek_has_ide=1;
	}

	if ((inw (BRIDGE_FEATURE) & 2)) { /* We have ISA interface */
		outw_p(0, BRIDGE_ISA_INTR_LVL);
		outw_p(0, BRIDGE_ISA_INTR_MASK);

		outw_p(0, BRIDGE_ISA_CTRL);
		DELAY();
		outw_p(0x8000, BRIDGE_ISA_CTRL);
		DELAY();

		outw_p(0xffff, BRIDGE_ISA_INTR_STAT); /* Clear interrupt status */
		outw_p(0x0f-10, BRIDGE_ISA_INTR_LVL); /* Use 10 IPR */
		outw_p(0xfff8, BRIDGE_ISA_INTR_MASK); /* Enable interrupt */
		cqreek_has_isa=1;
	}

	printk(KERN_INFO "CqREEK Setup (IDE=%d, ISA=%d)...done\n", cqreek_has_ide, cqreek_has_isa);
}


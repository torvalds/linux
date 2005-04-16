/*
 * linux/arch/sh/kernel/setup_sh2000.c
 *
 * Copyright (C) 2001  SUGIOKA Tochinobu
 *
 * SH-2000 Support.
 *
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/irq.h>

#include <asm/io.h>
#include <asm/machvec.h>
#include <asm/mach/sh2000.h>

#define CF_CIS_BASE	0xb4200000

#define PORT_PECR	0xa4000108
#define PORT_PHCR	0xa400010E
#define	PORT_ICR1	0xa4000010
#define	PORT_IRR0	0xa4000004

#define IDE_OFFSET	0xb6200000
#define NIC_OFFSET	0xb6000000
#define EXTBUS_OFFSET	0xba000000


const char *get_system_type(void)
{
	return "sh2000";
}

static unsigned long sh2000_isa_port2addr(unsigned long offset)
{
	if((offset & ~7) == 0x1f0 || offset == 0x3f6)
		return IDE_OFFSET + offset;
	else if((offset & ~0x1f) == 0x300)
		return NIC_OFFSET + offset;
	return EXTBUS_OFFSET + offset;
}

/*
 * The Machine Vector
 */
struct sh_machine_vector mv_sh2000 __initmv = {
        .mv_nr_irqs		= 80,
        .mv_isa_port2addr	= sh2000_isa_port2addr,
};
ALIAS_MV(sh2000)

/*
 * Initialize the board
 */
int __init platform_setup(void)
{
	/* XXX: RTC setting comes here */

	/* These should be done by BIOS/IPL ... */
	/* Enable nCE2A, nCE2B output */
	ctrl_outw(ctrl_inw(PORT_PECR) & ~0xf00, PORT_PECR);
	/* Enable the Compact Flash card, and set the level interrupt */
	ctrl_outw(0x0042, CF_CIS_BASE+0x0200);
	/* Enable interrupt */
	ctrl_outw(ctrl_inw(PORT_PHCR) & ~0x03f3, PORT_PHCR);
	ctrl_outw(1, PORT_ICR1);
	ctrl_outw(ctrl_inw(PORT_IRR0) & ~0xff3f, PORT_IRR0);
	printk(KERN_INFO "SH-2000 Setup...done\n");
	return 0;
}

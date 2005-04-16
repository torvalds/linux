/*
 * linux/arch/sh/boards/sh03/setup.c
 *
 * Copyright (C) 2004  Interface Co.,Ltd. Saito.K
 *
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/hdreg.h>
#include <linux/ide.h>
#include <asm/io.h>
#include <asm/sh03/io.h>
#include <asm/sh03/sh03.h>
#include <asm/addrspace.h>
#include "../../drivers/pci/pci-sh7751.h"

extern void (*board_time_init)(void);

const char *get_system_type(void)
{
	return "Interface CTP/PCI-SH03)";
}

void init_sh03_IRQ(void)
{
	ctrl_outw(ctrl_inw(INTC_ICR) | INTC_ICR_IRLM, INTC_ICR);

	make_ipr_irq(IRL0_IRQ, IRL0_IPR_ADDR, IRL0_IPR_POS, IRL0_PRIORITY);
	make_ipr_irq(IRL1_IRQ, IRL1_IPR_ADDR, IRL1_IPR_POS, IRL1_PRIORITY);
	make_ipr_irq(IRL2_IRQ, IRL2_IPR_ADDR, IRL2_IPR_POS, IRL2_PRIORITY);
	make_ipr_irq(IRL3_IRQ, IRL3_IPR_ADDR, IRL3_IPR_POS, IRL3_PRIORITY);
}

extern void *cf_io_base;

unsigned long sh03_isa_port2addr(unsigned long port)
{
	if (PXSEG(port))
		return port;
	/* CompactFlash (IDE) */
	if (((port >= 0x1f0) && (port <= 0x1f7)) || (port == 0x3f6)) {
		return (unsigned long)cf_io_base + port;
	}
        return port + SH7751_PCI_IO_BASE;
}

/*
 * The Machine Vector
 */

struct sh_machine_vector mv_sh03 __initmv = {
	.mv_nr_irqs		= 48,
	.mv_isa_port2addr	= sh03_isa_port2addr,
	.mv_init_irq		= init_sh03_IRQ,

#ifdef CONFIG_HEARTBEAT
	.mv_heartbeat		= heartbeat_sh03,
#endif
};

ALIAS_MV(sh03)

/* arch/sh/boards/sh03/rtc.c */
void sh03_time_init(void);

int __init platform_setup(void)
{
	board_time_init = sh03_time_init;
	return 0;
}

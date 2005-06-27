/*
 * arch/ppc/syslib/ppc83xx_setup.c
 *
 * MPC83XX common board code
 *
 * Maintainer: Kumar Gala <kumar.gala@freescale.com>
 *
 * Copyright 2005 Freescale Semiconductor Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/serial.h>
#include <linux/tty.h>	/* for linux/serial_core.h */
#include <linux/serial_core.h>
#include <linux/serial_8250.h>

#include <asm/time.h>
#include <asm/mpc83xx.h>
#include <asm/mmu.h>
#include <asm/ppc_sys.h>
#include <asm/kgdb.h>
#include <asm/delay.h>

#include <syslib/ppc83xx_setup.h>

phys_addr_t immrbar;

/* Return the amount of memory */
unsigned long __init
mpc83xx_find_end_of_memory(void)
{
        bd_t *binfo;

        binfo = (bd_t *) __res;

        return binfo->bi_memsize;
}

long __init
mpc83xx_time_init(void)
{
#define SPCR_OFFS   0x00000110
#define SPCR_TBEN   0x00400000

	bd_t *binfo = (bd_t *)__res;
	u32 *spcr = ioremap(binfo->bi_immr_base + SPCR_OFFS, 4);

	*spcr |= SPCR_TBEN;

	iounmap(spcr);

	return 0;
}

/* The decrementer counts at the system (internal) clock freq divided by 4 */
void __init
mpc83xx_calibrate_decr(void)
{
        bd_t *binfo = (bd_t *) __res;
        unsigned int freq, divisor;

	freq = binfo->bi_busfreq;
	divisor = 4;
	tb_ticks_per_jiffy = freq / HZ / divisor;
	tb_to_us = mulhwu_scale_factor(freq / divisor, 1000000);
}

#ifdef CONFIG_SERIAL_8250
void __init
mpc83xx_early_serial_map(void)
{
#if defined(CONFIG_SERIAL_TEXT_DEBUG) || defined(CONFIG_KGDB)
	struct uart_port serial_req;
#endif
	struct plat_serial8250_port *pdata;
	bd_t *binfo = (bd_t *) __res;
	pdata = (struct plat_serial8250_port *) ppc_sys_get_pdata(MPC83xx_DUART);

	/* Setup serial port access */
	pdata[0].uartclk = binfo->bi_busfreq;
	pdata[0].mapbase += binfo->bi_immr_base;
	pdata[0].membase = ioremap(pdata[0].mapbase, 0x100);

#if defined(CONFIG_SERIAL_TEXT_DEBUG) || defined(CONFIG_KGDB)
	memset(&serial_req, 0, sizeof (serial_req));
	serial_req.iotype = SERIAL_IO_MEM;
	serial_req.mapbase = pdata[0].mapbase;
	serial_req.membase = pdata[0].membase;
	serial_req.regshift = 0;

	gen550_init(0, &serial_req);
#endif

	pdata[1].uartclk = binfo->bi_busfreq;
	pdata[1].mapbase += binfo->bi_immr_base;
	pdata[1].membase = ioremap(pdata[1].mapbase, 0x100);

#if defined(CONFIG_SERIAL_TEXT_DEBUG) || defined(CONFIG_KGDB)
	/* Assume gen550_init() doesn't modify serial_req */
	serial_req.mapbase = pdata[1].mapbase;
	serial_req.membase = pdata[1].membase;

	gen550_init(1, &serial_req);
#endif
}
#endif

void
mpc83xx_restart(char *cmd)
{
	volatile unsigned char __iomem *reg;
	unsigned char tmp;

	reg = ioremap(BCSR_PHYS_ADDR, BCSR_SIZE);

	local_irq_disable();

	/*
	 * Unlock the BCSR bits so a PRST will update the contents.
	 * Otherwise the reset asserts but doesn't clear.
	 */
	tmp = in_8(reg + BCSR_MISC_REG3_OFF);
	tmp |= BCSR_MISC_REG3_CNFLOCK; /* low true, high false */
	out_8(reg + BCSR_MISC_REG3_OFF, tmp);

	/*
	 * Trigger a reset via a low->high transition of the
	 * PORESET bit.
	 */
	tmp = in_8(reg + BCSR_MISC_REG2_OFF);
	tmp &= ~BCSR_MISC_REG2_PORESET;
	out_8(reg + BCSR_MISC_REG2_OFF, tmp);

	udelay(1);

	tmp |= BCSR_MISC_REG2_PORESET;
	out_8(reg + BCSR_MISC_REG2_OFF, tmp);

	for(;;);
}

void
mpc83xx_power_off(void)
{
	local_irq_disable();
	for(;;);
}

void
mpc83xx_halt(void)
{
	local_irq_disable();
	for(;;);
}

/* PCI SUPPORT DOES NOT EXIT, MODEL after ppc85xx_setup.c */

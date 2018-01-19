// SPDX-License-Identifier: GPL-2.0
/*
 * MPC5200 PSC serial console support.
 *
 * Author: Grant Likely <grant.likely@secretlab.ca>
 *
 * Copyright (c) 2007 Secret Lab Technologies Ltd.
 * Copyright (c) 2007 Freescale Semiconductor, Inc.
 *
 * It is assumed that the firmware (or the platform file) has already set
 * up the port.
 */

#include "types.h"
#include "io.h"
#include "ops.h"

/* Programmable Serial Controller (PSC) status register bits */
#define MPC52xx_PSC_SR		0x04
#define MPC52xx_PSC_SR_RXRDY		0x0100
#define MPC52xx_PSC_SR_RXFULL		0x0200
#define MPC52xx_PSC_SR_TXRDY		0x0400
#define MPC52xx_PSC_SR_TXEMP		0x0800

#define MPC52xx_PSC_BUFFER	0x0C

static void *psc;

static int psc_open(void)
{
	/* Assume the firmware has already configured the PSC into
	 * uart mode */
	return 0;
}

static void psc_putc(unsigned char c)
{
	while (!(in_be16(psc + MPC52xx_PSC_SR) & MPC52xx_PSC_SR_TXRDY)) ;
	out_8(psc + MPC52xx_PSC_BUFFER, c);
}

static unsigned char psc_tstc(void)
{
	return (in_be16(psc + MPC52xx_PSC_SR) & MPC52xx_PSC_SR_RXRDY) != 0;
}

static unsigned char psc_getc(void)
{
	while (!(in_be16(psc + MPC52xx_PSC_SR) & MPC52xx_PSC_SR_RXRDY)) ;
	return in_8(psc + MPC52xx_PSC_BUFFER);
}

int mpc5200_psc_console_init(void *devp, struct serial_console_data *scdp)
{
	/* Get the base address of the psc registers */
	if (dt_get_virtual_reg(devp, &psc, 1) < 1)
		return -1;

	scdp->open = psc_open;
	scdp->putc = psc_putc;
	scdp->getc = psc_getc;
	scdp->tstc = psc_tstc;

	return 0;
}

/*
 * kgdb debug routines for SiByte boards.
 *
 * Copyright (C) 2001 MontaVista Software Inc.
 * Author: Jun Sun, jsun@mvista.com or jsun@junsun.net
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

/* -------------------- BEGINNING OF CONFIG --------------------- */

#include <linux/delay.h>
#include <asm/io.h>
#include <asm/sibyte/sb1250.h>
#include <asm/sibyte/sb1250_regs.h>
#include <asm/sibyte/sb1250_uart.h>
#include <asm/sibyte/sb1250_int.h>
#include <asm/addrspace.h>

/*
 * We use the second serial port for kgdb traffic.
 * 	115200, 8, N, 1.
 */

#define	BAUD_RATE		115200
#define	CLK_DIVISOR		V_DUART_BAUD_RATE(BAUD_RATE)
#define	DATA_BITS		V_DUART_BITS_PER_CHAR_8		/* or 7    */
#define	PARITY			V_DUART_PARITY_MODE_NONE	/* or even */
#define	STOP_BITS		M_DUART_STOP_BIT_LEN_1		/* or 2    */

static int duart_initialized = 0;	/* 0: need to be init'ed by kgdb */

/* -------------------- END OF CONFIG --------------------- */
extern int kgdb_port;

#define	duart_out(reg, val)	csr_out32(val, IOADDR(A_DUART_CHANREG(kgdb_port,reg)))
#define duart_in(reg)		csr_in32(IOADDR(A_DUART_CHANREG(kgdb_port,reg)))

void putDebugChar(unsigned char c);
unsigned char getDebugChar(void);
static void
duart_init(int clk_divisor, int data, int parity, int stop)
{
	duart_out(R_DUART_MODE_REG_1, data | parity);
	duart_out(R_DUART_MODE_REG_2, stop);
	duart_out(R_DUART_CLK_SEL, clk_divisor);

	duart_out(R_DUART_CMD, M_DUART_RX_EN | M_DUART_TX_EN);	/* enable rx and tx */
}

void
putDebugChar(unsigned char c)
{
	if (!duart_initialized) {
		duart_initialized = 1;
		duart_init(CLK_DIVISOR, DATA_BITS, PARITY, STOP_BITS);
	}
	while ((duart_in(R_DUART_STATUS) & M_DUART_TX_RDY) == 0);
	duart_out(R_DUART_TX_HOLD, c);
}

unsigned char
getDebugChar(void)
{
	if (!duart_initialized) {
		duart_initialized = 1;
		duart_init(CLK_DIVISOR, DATA_BITS, PARITY, STOP_BITS);
	}
	while ((duart_in(R_DUART_STATUS) & M_DUART_RX_RDY) == 0) ;
	return duart_in(R_DUART_RX_HOLD);
}


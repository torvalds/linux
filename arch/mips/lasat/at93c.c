// SPDX-License-Identifier: GPL-2.0
/*
 * Atmel AT93C46 serial eeprom driver
 *
 * Brian Murphy <brian.murphy@eicon.com>
 *
 */
#include <linux/kernel.h>
#include <linux/delay.h>
#include <asm/lasat/lasat.h>

#include "at93c.h"

#define AT93C_ADDR_SHIFT	7
#define AT93C_ADDR_MAX		((1 << AT93C_ADDR_SHIFT) - 1)
#define AT93C_RCMD		(0x6 << AT93C_ADDR_SHIFT)
#define AT93C_WCMD		(0x5 << AT93C_ADDR_SHIFT)
#define AT93C_WENCMD		0x260
#define AT93C_WDSCMD		0x200

struct at93c_defs *at93c;

static void at93c_reg_write(u32 val)
{
	*at93c->reg = val;
}

static u32 at93c_reg_read(void)
{
	u32 tmp = *at93c->reg;
	return tmp;
}

static u32 at93c_datareg_read(void)
{
	u32 tmp = *at93c->rdata_reg;
	return tmp;
}

static void at93c_cycle_clk(u32 data)
{
	at93c_reg_write(data | at93c->clk);
	lasat_ndelay(250);
	at93c_reg_write(data & ~at93c->clk);
	lasat_ndelay(250);
}

static void at93c_write_databit(u8 bit)
{
	u32 data = at93c_reg_read();
	if (bit)
		data |= 1 << at93c->wdata_shift;
	else
		data &= ~(1 << at93c->wdata_shift);

	at93c_reg_write(data);
	lasat_ndelay(100);
	at93c_cycle_clk(data);
}

static unsigned int at93c_read_databit(void)
{
	u32 data;

	at93c_cycle_clk(at93c_reg_read());
	data = (at93c_datareg_read() >> at93c->rdata_shift) & 1;
	return data;
}

static u8 at93c_read_byte(void)
{
	int i;
	u8 data = 0;

	for (i = 0; i <= 7; i++) {
		data <<= 1;
		data |= at93c_read_databit();
	}
	return data;
}

static void at93c_write_bits(u32 data, int size)
{
	int i;
	int shift = size - 1;
	u32 mask = (1 << shift);

	for (i = 0; i < size; i++) {
		at93c_write_databit((data & mask) >> shift);
		data <<= 1;
	}
}

static void at93c_init_op(void)
{
	at93c_reg_write((at93c_reg_read() | at93c->cs) &
			~at93c->clk & ~(1 << at93c->rdata_shift));
	lasat_ndelay(50);
}

static void at93c_end_op(void)
{
	at93c_reg_write(at93c_reg_read() & ~at93c->cs);
	lasat_ndelay(250);
}

static void at93c_wait(void)
{
	at93c_init_op();
	while (!at93c_read_databit())
		;
	at93c_end_op();
};

static void at93c_disable_wp(void)
{
	at93c_init_op();
	at93c_write_bits(AT93C_WENCMD, 10);
	at93c_end_op();
}

static void at93c_enable_wp(void)
{
	at93c_init_op();
	at93c_write_bits(AT93C_WDSCMD, 10);
	at93c_end_op();
}

u8 at93c_read(u8 addr)
{
	u8 byte;
	at93c_init_op();
	at93c_write_bits((addr & AT93C_ADDR_MAX)|AT93C_RCMD, 10);
	byte = at93c_read_byte();
	at93c_end_op();
	return byte;
}

void at93c_write(u8 addr, u8 data)
{
	at93c_disable_wp();
	at93c_init_op();
	at93c_write_bits((addr & AT93C_ADDR_MAX)|AT93C_WCMD, 10);
	at93c_write_bits(data, 8);
	at93c_end_op();
	at93c_wait();
	at93c_enable_wp();
}

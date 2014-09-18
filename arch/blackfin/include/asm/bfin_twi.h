/*
 * bfin_twi.h - interface to Blackfin TWIs
 *
 * Copyright 2005-2010 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef __ASM_BFIN_TWI_H__
#define __ASM_BFIN_TWI_H__

#include <asm/blackfin.h>

#define DEFINE_TWI_REG(reg_name, reg) \
static inline u16 read_##reg_name(struct bfin_twi_iface *iface) \
	{ return bfin_read16(&iface->regs_base->reg); } \
static inline void write_##reg_name(struct bfin_twi_iface *iface, u16 v) \
	{ bfin_write16(&iface->regs_base->reg, v); }

DEFINE_TWI_REG(CLKDIV, clkdiv)
DEFINE_TWI_REG(SLAVE_CTL, slave_ctl)
DEFINE_TWI_REG(SLAVE_STAT, slave_stat)
DEFINE_TWI_REG(SLAVE_ADDR, slave_addr)
DEFINE_TWI_REG(MASTER_CTL, master_ctl)
DEFINE_TWI_REG(MASTER_STAT, master_stat)
DEFINE_TWI_REG(MASTER_ADDR, master_addr)
DEFINE_TWI_REG(INT_STAT, int_stat)
DEFINE_TWI_REG(INT_MASK, int_mask)
DEFINE_TWI_REG(FIFO_STAT, fifo_stat)
DEFINE_TWI_REG(XMT_DATA8, xmt_data8)
DEFINE_TWI_REG(XMT_DATA16, xmt_data16)
#if !ANOMALY_16000030
DEFINE_TWI_REG(RCV_DATA8, rcv_data8)
DEFINE_TWI_REG(RCV_DATA16, rcv_data16)
#else
static inline u16 read_RCV_DATA8(struct bfin_twi_iface *iface)
{
	u16 ret;
	unsigned long flags;

	flags = hard_local_irq_save();
	ret = bfin_read16(&iface->regs_base->rcv_data8);
	hard_local_irq_restore(flags);

	return ret;
}

static inline u16 read_RCV_DATA16(struct bfin_twi_iface *iface)
{
	u16 ret;
	unsigned long flags;

	flags = hard_local_irq_save();
	ret = bfin_read16(&iface->regs_base->rcv_data16);
	hard_local_irq_restore(flags);

	return ret;
}
#endif

static inline u16 read_FIFO_CTL(struct bfin_twi_iface *iface)
{
	return bfin_read16(&iface->regs_base->fifo_ctl);
}

static inline void write_FIFO_CTL(struct bfin_twi_iface *iface, u16 v)
{
	bfin_write16(&iface->regs_base->fifo_ctl, v);
	SSYNC();
}

static inline u16 read_CONTROL(struct bfin_twi_iface *iface)
{
	return bfin_read16(&iface->regs_base->control);
}

static inline void write_CONTROL(struct bfin_twi_iface *iface, u16 v)
{
	SSYNC();
	bfin_write16(&iface->regs_base->control, v);
}
#endif

/*
 * drivers/usb/sunxi_usb/hcd/include/sw_hcd_regs_i.h
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * javen <javen@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#ifndef  __SW_HCD_REGS_I_H__
#define  __SW_HCD_REGS_I_H__

#include <linux/io.h>
#include "sw_hcd_config.h"

#define sw_hcd_ep_select(usbc_base, epnum)    USBC_Writeb(epnum, USBC_REG_EPIND(usbc_base))


static inline void sw_hcd_readsl(const void __iomem *addr, void *buf, int len)
	{ insl((unsigned long)addr, buf, len); }
static inline void sw_hcd_readsw(const void __iomem *addr, void *buf, int len)
	{ insw((unsigned long)addr, buf, len); }
static inline void sw_hcd_readsb(const void __iomem *addr, void *buf, int len)
	{ insb((unsigned long)addr, buf, len); }

static inline void sw_hcd_writesl(const void __iomem *addr, const void *buf, int len)
	{ outsl((unsigned long)addr, buf, len); }
static inline void sw_hcd_writesw(const void __iomem *addr, const void *buf, int len)
	{ outsw((unsigned long)addr, buf, len); }
static inline void sw_hcd_writesb(const void __iomem *addr, const void *buf, int len)
	{ outsb((unsigned long)addr, buf, len); }



/* "bus control"/target registers, for host side multipoint (external hubs) */
#define USBC_REG_OFFSET_TXFUNCADDR		0x00
#define USBC_REG_OFFSET_TXHUBADDR		0x02
#define USBC_REG_OFFSET_TXHUBPORT		0x03

#define USBC_REG_OFFSET_RXFUNCADDR		0x04
#define USBC_REG_OFFSET_RXHUBADDR		0x06
#define USBC_REG_OFFSET_RXHUBPORT		0x07

static inline void sw_hcd_write_txfifosz(void __iomem *usbc_base, u8 c_size)
{
	USBC_Writeb(c_size, USBC_REG_TXFIFOSZ(usbc_base));
}

static inline void sw_hcd_write_txfifoadd(void __iomem *usbc_base, u16 c_off)
{
	USBC_Writeb(c_off, USBC_REG_TXFIFOAD(usbc_base));
}

static inline void sw_hcd_write_rxfifosz(void __iomem *usbc_base, u8 c_size)
{
	USBC_Writeb(c_size, USBC_REG_RXFIFOSZ(usbc_base));
}

static inline void  sw_hcd_write_rxfifoadd(void __iomem *usbc_base, u16 c_off)
{
	USBC_Writeb(c_off, USBC_REG_RXFIFOAD(usbc_base));
}

static inline u8 sw_hcd_read_configdata(void __iomem *usbc_base)
{
	return USBC_Readb(USBC_REG_CONFIGDATA(usbc_base));
}

static inline void __iomem *sw_hcd_read_target_reg_base(u8 i, void __iomem *usbc_base)
{
	return USBC_REG_TXFADDRx(usbc_base, i);
}

static inline void sw_hcd_write_rxfunaddr(void __iomem *ep_target_regs, u8 qh_addr_reg)
{
	USBC_Writeb(qh_addr_reg, (ep_target_regs + USBC_REG_OFFSET_RXFUNCADDR));
}

static inline void sw_hcd_write_rxhubaddr(void __iomem *ep_target_regs, u8 qh_h_addr_reg)
{
	USBC_Writeb(qh_h_addr_reg, (ep_target_regs + USBC_REG_OFFSET_RXHUBADDR));
}

static inline void sw_hcd_write_rxhubport(void __iomem *ep_target_regs, u8 qh_h_port_reg)
{
	USBC_Writeb(qh_h_port_reg, (ep_target_regs + USBC_REG_OFFSET_RXHUBPORT));
}

static inline void  sw_hcd_write_txfunaddr(void __iomem *usbc_base, u8 epnum, u8 qh_addr_reg)
{
    USBC_Writeb(qh_addr_reg, USBC_REG_TXFADDRx(usbc_base, epnum));
}

static inline void  sw_hcd_write_txhubaddr(void __iomem *usbc_base, u8 epnum, u8 qh_h_addr_reg)
{
	USBC_Writeb(qh_h_addr_reg, USBC_REG_TXHADDRx(usbc_base, epnum));
}

static inline void  sw_hcd_write_txhubport(void __iomem *usbc_base, u8 epnum, u8 qh_h_port_reg)
{
	USBC_Writeb(qh_h_port_reg, USBC_REG_TXHPORTx(usbc_base, epnum));
}

#endif   //__SW_HCD_REGS_I_H__


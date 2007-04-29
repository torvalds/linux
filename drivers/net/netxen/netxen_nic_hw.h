/*
 * Copyright (C) 2003 - 2006 NetXen, Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston,
 * MA  02111-1307, USA.
 *
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.
 *
 * Contact Information:
 *    info@netxen.com
 * NetXen,
 * 3965 Freedom Circle, Fourth floor,
 * Santa Clara, CA 95054
 *
 *
 * Structures, enums, and macros for the MAC
 *
 */

#ifndef __NETXEN_NIC_HW_H_
#define __NETXEN_NIC_HW_H_

#include "netxen_nic_hdr.h"

/* Hardware memory size of 128 meg */
#define NETXEN_MEMADDR_MAX (128 * 1024 * 1024)

#ifndef readq
static inline u64 readq(void __iomem * addr)
{
	return readl(addr) | (((u64) readl(addr + 4)) << 32LL);
}
#endif

#ifndef writeq
static inline void writeq(u64 val, void __iomem * addr)
{
	writel(((u32) (val)), (addr));
	writel(((u32) (val >> 32)), (addr + 4));
}
#endif

static inline void netxen_nic_hw_block_write64(u64 __iomem * data_ptr,
					       u64 __iomem * addr,
					       int num_words)
{
	int num;
	for (num = 0; num < num_words; num++) {
		writeq(readq((void __iomem *)data_ptr), addr);
		addr++;
		data_ptr++;
	}
}

static inline void netxen_nic_hw_block_read64(u64 __iomem * data_ptr,
					      u64 __iomem * addr, int num_words)
{
	int num;
	for (num = 0; num < num_words; num++) {
		writeq(readq((void __iomem *)addr), data_ptr);
		addr++;
		data_ptr++;
	}

}

struct netxen_adapter;

#define NETXEN_PCI_MAPSIZE_BYTES  (NETXEN_PCI_MAPSIZE << 20)

#define NETXEN_NIC_LOCKED_READ_REG(X, Y)	\
	addr = pci_base_offset(adapter, X);	\
	*(u32 *)Y = readl((void __iomem*) addr);

struct netxen_port;
void netxen_nic_set_link_parameters(struct netxen_adapter *adapter);
void netxen_nic_flash_print(struct netxen_adapter *adapter);
int netxen_nic_hw_write_wx(struct netxen_adapter *adapter, u64 off,
			   void *data, int len);
void netxen_crb_writelit_adapter(struct netxen_adapter *adapter,
				 unsigned long off, int data);
int netxen_nic_hw_read_wx(struct netxen_adapter *adapter, u64 off,
			  void *data, int len);

typedef u8 netxen_ethernet_macaddr_t[6];

/* Nibble or Byte mode for phy interface (GbE mode only) */
typedef enum {
	NETXEN_NIU_10_100_MB = 0,
	NETXEN_NIU_1000_MB
} netxen_niu_gbe_ifmode_t;

#define _netxen_crb_get_bit(var, bit)  ((var >> bit) & 0x1)

/*
 * NIU GB MAC Config Register 0 (applies to GB0, GB1, GB2, GB3)
 *
 *	Bit 0 : enable_tx => 1:enable frame xmit, 0:disable
 *	Bit 1 : tx_synced => R/O: xmit enable synched to xmit stream
 *	Bit 2 : enable_rx => 1:enable frame recv, 0:disable
 *	Bit 3 : rx_synced => R/O: recv enable synched to recv stream
 *	Bit 4 : tx_flowctl => 1:enable pause frame generation, 0:disable
 *	Bit 5 : rx_flowctl => 1:act on recv'd pause frames, 0:ignore
 *	Bit 8 : loopback => 1:loop MAC xmits to MAC recvs, 0:normal
 *	Bit 16: tx_reset_pb => 1:reset frame xmit protocol blk, 0:no-op
 *	Bit 17: rx_reset_pb => 1:reset frame recv protocol blk, 0:no-op
 *	Bit 18: tx_reset_mac => 1:reset data/ctl multiplexer blk, 0:no-op
 *	Bit 19: rx_reset_mac => 1:reset ctl frames & timers blk, 0:no-op
 *	Bit 31: soft_reset => 1:reset the MAC and the SERDES, 0:no-op
 */

#define netxen_gb_enable_tx(config_word)	\
	((config_word) |= 1 << 0)
#define netxen_gb_enable_rx(config_word)	\
	((config_word) |= 1 << 2)
#define netxen_gb_tx_flowctl(config_word)	\
	((config_word) |= 1 << 4)
#define netxen_gb_rx_flowctl(config_word)	\
	((config_word) |= 1 << 5)
#define netxen_gb_tx_reset_pb(config_word)	\
	((config_word) |= 1 << 16)
#define netxen_gb_rx_reset_pb(config_word)	\
	((config_word) |= 1 << 17)
#define netxen_gb_tx_reset_mac(config_word)	\
	((config_word) |= 1 << 18)
#define netxen_gb_rx_reset_mac(config_word)	\
	((config_word) |= 1 << 19)
#define netxen_gb_soft_reset(config_word)	\
	((config_word) |= 1 << 31)

#define netxen_gb_unset_tx_flowctl(config_word)	\
	((config_word) &= ~(1 << 4))
#define netxen_gb_unset_rx_flowctl(config_word)	\
	((config_word) &= ~(1 << 5))

#define netxen_gb_get_tx_synced(config_word)	\
		_netxen_crb_get_bit((config_word), 1)
#define netxen_gb_get_rx_synced(config_word)	\
		_netxen_crb_get_bit((config_word), 3)
#define netxen_gb_get_tx_flowctl(config_word)	\
		_netxen_crb_get_bit((config_word), 4)
#define netxen_gb_get_rx_flowctl(config_word)	\
		_netxen_crb_get_bit((config_word), 5)
#define netxen_gb_get_soft_reset(config_word)	\
		_netxen_crb_get_bit((config_word), 31)

/*
 * NIU GB MAC Config Register 1 (applies to GB0, GB1, GB2, GB3)
 *
 *	Bit 0	    : duplex => 1:full duplex mode, 0:half duplex
 *	Bit 1	    : crc_enable => 1:append CRC to xmit frames, 0:dont append
 *	Bit 2	    : padshort => 1:pad short frames and add CRC, 0:dont pad
 *	Bit 4	    : checklength => 1:check framelen with actual,0:dont check
 *	Bit 5	    : hugeframes => 1:allow oversize xmit frames, 0:dont allow
 *	Bits 8-9    : intfmode => 01:nibble (10/100), 10:byte (1000)
 *	Bits 12-15  : preamblelen => preamble field length in bytes, default 7
 */

#define netxen_gb_set_duplex(config_word)	\
		((config_word) |= 1 << 0)
#define netxen_gb_set_crc_enable(config_word)	\
		((config_word) |= 1 << 1)
#define netxen_gb_set_padshort(config_word)	\
		((config_word) |= 1 << 2)
#define netxen_gb_set_checklength(config_word)	\
		((config_word) |= 1 << 4)
#define netxen_gb_set_hugeframes(config_word)	\
		((config_word) |= 1 << 5)
#define netxen_gb_set_preamblelen(config_word, val)	\
		((config_word) |= ((val) << 12) & 0xF000)
#define netxen_gb_set_intfmode(config_word, val)		\
		((config_word) |= ((val) << 8) & 0x300)

#define netxen_gb_get_stationaddress_low(config_word) ((config_word) >> 16)

#define netxen_gb_set_mii_mgmt_clockselect(config_word, val)	\
		((config_word) |= ((val) & 0x07))
#define netxen_gb_mii_mgmt_reset(config_word)	\
		((config_word) |= 1 << 31)
#define netxen_gb_mii_mgmt_unset(config_word)	\
		((config_word) &= ~(1 << 31))

/*
 * NIU GB MII Mgmt Command Register (applies to GB0, GB1, GB2, GB3)
 * Bit 0 : read_cycle => 1:perform single read cycle, 0:no-op
 * Bit 1 : scan_cycle => 1:perform continuous read cycles, 0:no-op
 */

#define netxen_gb_mii_mgmt_set_read_cycle(config_word)	\
		((config_word) |= 1 << 0)
#define netxen_gb_mii_mgmt_reg_addr(config_word, val)	\
		((config_word) |= ((val) & 0x1F))
#define netxen_gb_mii_mgmt_phy_addr(config_word, val)	\
		((config_word) |= (((val) & 0x1F) << 8))

/*
 * NIU GB MII Mgmt Indicators Register (applies to GB0, GB1, GB2, GB3)
 * Read-only register.
 * Bit 0 : busy => 1:performing an MII mgmt cycle, 0:idle
 * Bit 1 : scanning => 1:scan operation in progress, 0:idle
 * Bit 2 : notvalid => :mgmt result data not yet valid, 0:idle
 */
#define netxen_get_gb_mii_mgmt_busy(config_word)	\
		_netxen_crb_get_bit(config_word, 0)
#define netxen_get_gb_mii_mgmt_scanning(config_word)	\
		_netxen_crb_get_bit(config_word, 1)
#define netxen_get_gb_mii_mgmt_notvalid(config_word)	\
		_netxen_crb_get_bit(config_word, 2)
/*
 * NIU XG Pause Ctl Register
 *
 *      Bit 0       : xg0_mask => 1:disable tx pause frames
 *      Bit 1       : xg0_request => 1:request single pause frame
 *      Bit 2       : xg0_on_off => 1:request is pause on, 0:off
 *      Bit 3       : xg1_mask => 1:disable tx pause frames
 *      Bit 4       : xg1_request => 1:request single pause frame
 *      Bit 5       : xg1_on_off => 1:request is pause on, 0:off
 */

#define netxen_xg_set_xg0_mask(config_word)    \
	((config_word) |= 1 << 0)
#define netxen_xg_set_xg1_mask(config_word)    \
	((config_word) |= 1 << 3)
		
#define netxen_xg_get_xg0_mask(config_word)    \
	_netxen_crb_get_bit((config_word), 0)
#define netxen_xg_get_xg1_mask(config_word)    \
	_netxen_crb_get_bit((config_word), 3)

#define netxen_xg_unset_xg0_mask(config_word)  \
	((config_word) &= ~(1 << 0))
#define netxen_xg_unset_xg1_mask(config_word)  \
	((config_word) &= ~(1 << 3))

/*
 * NIU XG Pause Ctl Register
 *
 *      Bit 0       : xg0_mask => 1:disable tx pause frames
 *      Bit 1       : xg0_request => 1:request single pause frame
 *      Bit 2       : xg0_on_off => 1:request is pause on, 0:off
 *      Bit 3       : xg1_mask => 1:disable tx pause frames
 *      Bit 4       : xg1_request => 1:request single pause frame
 *      Bit 5       : xg1_on_off => 1:request is pause on, 0:off
 */
#define netxen_gb_set_gb0_mask(config_word)    \
	((config_word) |= 1 << 0)
#define netxen_gb_set_gb1_mask(config_word)    \
	((config_word) |= 1 << 2)
#define netxen_gb_set_gb2_mask(config_word)    \
	((config_word) |= 1 << 4)
#define netxen_gb_set_gb3_mask(config_word)    \
	((config_word) |= 1 << 6)

#define netxen_gb_get_gb0_mask(config_word)    \
	_netxen_crb_get_bit((config_word), 0)
#define netxen_gb_get_gb1_mask(config_word)    \
	_netxen_crb_get_bit((config_word), 2)
#define netxen_gb_get_gb2_mask(config_word)    \
	_netxen_crb_get_bit((config_word), 4)
#define netxen_gb_get_gb3_mask(config_word)    \
	_netxen_crb_get_bit((config_word), 6)
	
#define netxen_gb_unset_gb0_mask(config_word)  \
	((config_word) &= ~(1 << 0))
#define netxen_gb_unset_gb1_mask(config_word)  \
	((config_word) &= ~(1 << 2))
#define netxen_gb_unset_gb2_mask(config_word)  \
	((config_word) &= ~(1 << 4))
#define netxen_gb_unset_gb3_mask(config_word)  \
	((config_word) &= ~(1 << 6))


/*
 * PHY-Specific MII control/status registers.
 */
typedef enum {
	NETXEN_NIU_GB_MII_MGMT_ADDR_CONTROL = 0,
	NETXEN_NIU_GB_MII_MGMT_ADDR_STATUS = 1,
	NETXEN_NIU_GB_MII_MGMT_ADDR_PHY_ID_0 = 2,
	NETXEN_NIU_GB_MII_MGMT_ADDR_PHY_ID_1 = 3,
	NETXEN_NIU_GB_MII_MGMT_ADDR_AUTONEG = 4,
	NETXEN_NIU_GB_MII_MGMT_ADDR_LNKPART = 5,
	NETXEN_NIU_GB_MII_MGMT_ADDR_AUTONEG_MORE = 6,
	NETXEN_NIU_GB_MII_MGMT_ADDR_NEXTPAGE_XMIT = 7,
	NETXEN_NIU_GB_MII_MGMT_ADDR_LNKPART_NEXTPAGE = 8,
	NETXEN_NIU_GB_MII_MGMT_ADDR_1000BT_CONTROL = 9,
	NETXEN_NIU_GB_MII_MGMT_ADDR_1000BT_STATUS = 10,
	NETXEN_NIU_GB_MII_MGMT_ADDR_EXTENDED_STATUS = 15,
	NETXEN_NIU_GB_MII_MGMT_ADDR_PHY_CONTROL = 16,
	NETXEN_NIU_GB_MII_MGMT_ADDR_PHY_STATUS = 17,
	NETXEN_NIU_GB_MII_MGMT_ADDR_INT_ENABLE = 18,
	NETXEN_NIU_GB_MII_MGMT_ADDR_INT_STATUS = 19,
	NETXEN_NIU_GB_MII_MGMT_ADDR_PHY_CONTROL_MORE = 20,
	NETXEN_NIU_GB_MII_MGMT_ADDR_RECV_ERROR_COUNT = 21,
	NETXEN_NIU_GB_MII_MGMT_ADDR_LED_CONTROL = 24,
	NETXEN_NIU_GB_MII_MGMT_ADDR_LED_OVERRIDE = 25,
	NETXEN_NIU_GB_MII_MGMT_ADDR_PHY_CONTROL_MORE_YET = 26,
	NETXEN_NIU_GB_MII_MGMT_ADDR_PHY_STATUS_MORE = 27
} netxen_niu_phy_register_t;

/*
 * PHY-Specific Status Register (reg 17).
 *
 * Bit 0      : jabber => 1:jabber detected, 0:not
 * Bit 1      : polarity => 1:polarity reversed, 0:normal
 * Bit 2      : recvpause => 1:receive pause enabled, 0:disabled
 * Bit 3      : xmitpause => 1:transmit pause enabled, 0:disabled
 * Bit 4      : energydetect => 1:sleep, 0:active
 * Bit 5      : downshift => 1:downshift, 0:no downshift
 * Bit 6      : crossover => 1:MDIX (crossover), 0:MDI (no crossover)
 * Bits 7-9   : cablelen => not valid in 10Mb/s mode
 *			0:<50m, 1:50-80m, 2:80-110m, 3:110-140m, 4:>140m
 * Bit 10     : link => 1:link up, 0:link down
 * Bit 11     : resolved => 1:speed and duplex resolved, 0:not yet
 * Bit 12     : pagercvd => 1:page received, 0:page not received
 * Bit 13     : duplex => 1:full duplex, 0:half duplex
 * Bits 14-15 : speed => 0:10Mb/s, 1:100Mb/s, 2:1000Mb/s, 3:rsvd
 */

#define netxen_get_phy_cablelen(config_word) (((config_word) >> 7) & 0x07)
#define netxen_get_phy_speed(config_word) (((config_word) >> 14) & 0x03)

#define netxen_set_phy_speed(config_word, val)	\
		((config_word) |= ((val & 0x03) << 14))
#define netxen_set_phy_duplex(config_word)	\
		((config_word) |= 1 << 13)
#define netxen_clear_phy_duplex(config_word)	\
		((config_word) &= ~(1 << 13))

#define netxen_get_phy_jabber(config_word)	\
		_netxen_crb_get_bit(config_word, 0)
#define netxen_get_phy_polarity(config_word)	\
		_netxen_crb_get_bit(config_word, 1)
#define netxen_get_phy_recvpause(config_word)	\
		_netxen_crb_get_bit(config_word, 2)
#define netxen_get_phy_xmitpause(config_word)	\
		_netxen_crb_get_bit(config_word, 3)
#define netxen_get_phy_energydetect(config_word) \
		_netxen_crb_get_bit(config_word, 4)
#define netxen_get_phy_downshift(config_word)	\
		_netxen_crb_get_bit(config_word, 5)
#define netxen_get_phy_crossover(config_word)	\
		_netxen_crb_get_bit(config_word, 6)
#define netxen_get_phy_link(config_word)	\
		_netxen_crb_get_bit(config_word, 10)
#define netxen_get_phy_resolved(config_word)	\
		_netxen_crb_get_bit(config_word, 11)
#define netxen_get_phy_pagercvd(config_word)	\
		_netxen_crb_get_bit(config_word, 12)
#define netxen_get_phy_duplex(config_word)	\
		_netxen_crb_get_bit(config_word, 13)

/*
 * Interrupt Register definition
 * This definition applies to registers 18 and 19 (int enable and int status).
 * Bit 0 : jabber
 * Bit 1 : polarity_changed
 * Bit 4 : energy_detect
 * Bit 5 : downshift
 * Bit 6 : mdi_xover_changed
 * Bit 7 : fifo_over_underflow
 * Bit 8 : false_carrier
 * Bit 9 : symbol_error
 * Bit 10: link_status_changed
 * Bit 11: autoneg_completed
 * Bit 12: page_received
 * Bit 13: duplex_changed
 * Bit 14: speed_changed
 * Bit 15: autoneg_error
 */

#define netxen_get_phy_int_jabber(config_word)	\
		_netxen_crb_get_bit(config_word, 0)
#define netxen_get_phy_int_polarity_changed(config_word)	\
		_netxen_crb_get_bit(config_word, 1)
#define netxen_get_phy_int_energy_detect(config_word)	\
		_netxen_crb_get_bit(config_word, 4)
#define netxen_get_phy_int_downshift(config_word)	\
		_netxen_crb_get_bit(config_word, 5)
#define netxen_get_phy_int_mdi_xover_changed(config_word)	\
		_netxen_crb_get_bit(config_word, 6)
#define netxen_get_phy_int_fifo_over_underflow(config_word)	\
		_netxen_crb_get_bit(config_word, 7)
#define netxen_get_phy_int_false_carrier(config_word)	\
		_netxen_crb_get_bit(config_word, 8)
#define netxen_get_phy_int_symbol_error(config_word)	\
		_netxen_crb_get_bit(config_word, 9)
#define netxen_get_phy_int_link_status_changed(config_word)	\
		_netxen_crb_get_bit(config_word, 10)
#define netxen_get_phy_int_autoneg_completed(config_word)	\
		_netxen_crb_get_bit(config_word, 11)
#define netxen_get_phy_int_page_received(config_word)	\
		_netxen_crb_get_bit(config_word, 12)
#define netxen_get_phy_int_duplex_changed(config_word)	\
		_netxen_crb_get_bit(config_word, 13)
#define netxen_get_phy_int_speed_changed(config_word)	\
		_netxen_crb_get_bit(config_word, 14)
#define netxen_get_phy_int_autoneg_error(config_word)	\
		_netxen_crb_get_bit(config_word, 15)

#define netxen_set_phy_int_link_status_changed(config_word)	\
		((config_word) |= 1 << 10)
#define netxen_set_phy_int_autoneg_completed(config_word)	\
		((config_word) |= 1 << 11)
#define netxen_set_phy_int_speed_changed(config_word)	\
		((config_word) |= 1 << 14)

/*
 * NIU Mode Register.
 * Bit 0 : enable FibreChannel
 * Bit 1 : enable 10/100/1000 Ethernet
 * Bit 2 : enable 10Gb Ethernet
 */

#define netxen_get_niu_enable_ge(config_word)	\
		_netxen_crb_get_bit(config_word, 1)

/* Promiscous mode options (GbE mode only) */
typedef enum {
	NETXEN_NIU_PROMISC_MODE = 0,
	NETXEN_NIU_NON_PROMISC_MODE
} netxen_niu_prom_mode_t;

/*
 * NIU GB Drop CRC Register
 * 
 * Bit 0 : drop_gb0 => 1:drop pkts with bad CRCs, 0:pass them on
 * Bit 1 : drop_gb1 => 1:drop pkts with bad CRCs, 0:pass them on
 * Bit 2 : drop_gb2 => 1:drop pkts with bad CRCs, 0:pass them on
 * Bit 3 : drop_gb3 => 1:drop pkts with bad CRCs, 0:pass them on
 */

#define netxen_set_gb_drop_gb0(config_word)	\
		((config_word) |= 1 << 0)
#define netxen_set_gb_drop_gb1(config_word)	\
		((config_word) |= 1 << 1)
#define netxen_set_gb_drop_gb2(config_word)	\
		((config_word) |= 1 << 2)
#define netxen_set_gb_drop_gb3(config_word)	\
		((config_word) |= 1 << 3)

#define netxen_clear_gb_drop_gb0(config_word)	\
		((config_word) &= ~(1 << 0))
#define netxen_clear_gb_drop_gb1(config_word)	\
		((config_word) &= ~(1 << 1))
#define netxen_clear_gb_drop_gb2(config_word)	\
		((config_word) &= ~(1 << 2))
#define netxen_clear_gb_drop_gb3(config_word)	\
		((config_word) &= ~(1 << 3))

/*
 * NIU XG MAC Config Register
 *
 * Bit 0 : tx_enable => 1:enable frame xmit, 0:disable
 * Bit 2 : rx_enable => 1:enable frame recv, 0:disable
 * Bit 4 : soft_reset => 1:reset the MAC , 0:no-op
 * Bit 27: xaui_framer_reset
 * Bit 28: xaui_rx_reset
 * Bit 29: xaui_tx_reset
 * Bit 30: xg_ingress_afifo_reset
 * Bit 31: xg_egress_afifo_reset
 */

#define netxen_xg_soft_reset(config_word)	\
		((config_word) |= 1 << 4)

/*
 * MAC Control Register
 * 
 * Bit 0-1   : id_pool0
 * Bit 2     : enable_xtnd0
 * Bit 4-5   : id_pool1
 * Bit 6     : enable_xtnd1
 * Bit 8-9   : id_pool2
 * Bit 10    : enable_xtnd2
 * Bit 12-13 : id_pool3
 * Bit 14    : enable_xtnd3
 * Bit 24-25 : mode_select
 * Bit 28-31 : enable_pool
 */

#define netxen_nic_mcr_set_id_pool0(config, val)	\
		((config) |= ((val) &0x03))
#define netxen_nic_mcr_set_enable_xtnd0(config)	\
		((config) |= 1 << 3)
#define netxen_nic_mcr_set_id_pool1(config, val)	\
		((config) |= (((val) & 0x03) << 4))
#define netxen_nic_mcr_set_enable_xtnd1(config)	\
		((config) |= 1 << 6)
#define netxen_nic_mcr_set_id_pool2(config, val)	\
		((config) |= (((val) & 0x03) << 8))
#define netxen_nic_mcr_set_enable_xtnd2(config)	\
		((config) |= 1 << 10)
#define netxen_nic_mcr_set_id_pool3(config, val)	\
		((config) |= (((val) & 0x03) << 12))
#define netxen_nic_mcr_set_enable_xtnd3(config)	\
		((config) |= 1 << 14)
#define netxen_nic_mcr_set_mode_select(config, val)	\
		((config) |= (((val) & 0x03) << 24))
#define netxen_nic_mcr_set_enable_pool(config, val)	\
		((config) |= (((val) & 0x0f) << 28))

/* Set promiscuous mode for a GbE interface */
int netxen_niu_set_promiscuous_mode(struct netxen_adapter *adapter, 
				    netxen_niu_prom_mode_t mode);
int netxen_niu_xg_set_promiscuous_mode(struct netxen_adapter *adapter,
				       netxen_niu_prom_mode_t mode);

/* get/set the MAC address for a given MAC */
int netxen_niu_macaddr_get(struct netxen_adapter *adapter,
			   netxen_ethernet_macaddr_t * addr);
int netxen_niu_macaddr_set(struct netxen_adapter *adapter,
			   netxen_ethernet_macaddr_t addr);

/* XG versons */
int netxen_niu_xg_macaddr_get(struct netxen_adapter *adapter,
			      netxen_ethernet_macaddr_t * addr);
int netxen_niu_xg_macaddr_set(struct netxen_adapter *adapter,
			      netxen_ethernet_macaddr_t addr);

/* Generic enable for GbE ports. Will detect the speed of the link. */
int netxen_niu_gbe_init_port(struct netxen_adapter *adapter, int port);

int netxen_niu_xg_init_port(struct netxen_adapter *adapter, int port);

/* Disable a GbE interface */
int netxen_niu_disable_gbe_port(struct netxen_adapter *adapter);

int netxen_niu_disable_xg_port(struct netxen_adapter *adapter);

#endif				/* __NETXEN_NIC_HW_H_ */

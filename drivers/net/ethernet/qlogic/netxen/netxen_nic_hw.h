/*
 * Copyright (C) 2003 - 2009 NetXen, Inc.
 * Copyright (C) 2009 - QLogic Corporation.
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * The full GNU General Public License is included in this distribution
 * in the file called "COPYING".
 *
 */

#ifndef __NETXEN_NIC_HW_H_
#define __NETXEN_NIC_HW_H_

/* Hardware memory size of 128 meg */
#define NETXEN_MEMADDR_MAX (128 * 1024 * 1024)

struct netxen_adapter;

#define NETXEN_PCI_MAPSIZE_BYTES  (NETXEN_PCI_MAPSIZE << 20)

void netxen_nic_set_link_parameters(struct netxen_adapter *adapter);

/* Nibble or Byte mode for phy interface (GbE mode only) */

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
#define NETXEN_NIU_GB_MII_MGMT_ADDR_CONTROL		0
#define NETXEN_NIU_GB_MII_MGMT_ADDR_STATUS		1
#define NETXEN_NIU_GB_MII_MGMT_ADDR_PHY_ID_0		2
#define NETXEN_NIU_GB_MII_MGMT_ADDR_PHY_ID_1		3
#define NETXEN_NIU_GB_MII_MGMT_ADDR_AUTONEG		4
#define NETXEN_NIU_GB_MII_MGMT_ADDR_LNKPART		5
#define NETXEN_NIU_GB_MII_MGMT_ADDR_AUTONEG_MORE	6
#define NETXEN_NIU_GB_MII_MGMT_ADDR_NEXTPAGE_XMIT	7
#define NETXEN_NIU_GB_MII_MGMT_ADDR_LNKPART_NEXTPAGE	8
#define NETXEN_NIU_GB_MII_MGMT_ADDR_1000BT_CONTROL	9
#define NETXEN_NIU_GB_MII_MGMT_ADDR_1000BT_STATUS	10
#define NETXEN_NIU_GB_MII_MGMT_ADDR_EXTENDED_STATUS	15
#define NETXEN_NIU_GB_MII_MGMT_ADDR_PHY_CONTROL		16
#define NETXEN_NIU_GB_MII_MGMT_ADDR_PHY_STATUS		17
#define NETXEN_NIU_GB_MII_MGMT_ADDR_INT_ENABLE		18
#define NETXEN_NIU_GB_MII_MGMT_ADDR_INT_STATUS		19
#define NETXEN_NIU_GB_MII_MGMT_ADDR_PHY_CONTROL_MORE	20
#define NETXEN_NIU_GB_MII_MGMT_ADDR_RECV_ERROR_COUNT	21
#define NETXEN_NIU_GB_MII_MGMT_ADDR_LED_CONTROL		24
#define NETXEN_NIU_GB_MII_MGMT_ADDR_LED_OVERRIDE	25
#define NETXEN_NIU_GB_MII_MGMT_ADDR_PHY_CONTROL_MORE_YET	26
#define NETXEN_NIU_GB_MII_MGMT_ADDR_PHY_STATUS_MORE	27

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

#define netxen_get_phy_speed(config_word) (((config_word) >> 14) & 0x03)

#define netxen_set_phy_speed(config_word, val)	\
		((config_word) |= ((val & 0x03) << 14))
#define netxen_set_phy_duplex(config_word)	\
		((config_word) |= 1 << 13)
#define netxen_clear_phy_duplex(config_word)	\
		((config_word) &= ~(1 << 13))

#define netxen_get_phy_link(config_word)	\
		_netxen_crb_get_bit(config_word, 10)
#define netxen_get_phy_duplex(config_word)	\
		_netxen_crb_get_bit(config_word, 13)

/*
 * NIU Mode Register.
 * Bit 0 : enable FibreChannel
 * Bit 1 : enable 10/100/1000 Ethernet
 * Bit 2 : enable 10Gb Ethernet
 */

#define netxen_get_niu_enable_ge(config_word)	\
		_netxen_crb_get_bit(config_word, 1)

#define NETXEN_NIU_NON_PROMISC_MODE	0
#define NETXEN_NIU_PROMISC_MODE		1
#define NETXEN_NIU_ALLMULTI_MODE	2

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

typedef struct {
	unsigned valid;
	unsigned start_128M;
	unsigned end_128M;
	unsigned start_2M;
} crb_128M_2M_sub_block_map_t;

typedef struct {
	crb_128M_2M_sub_block_map_t sub_block[16];
} crb_128M_2M_block_map_t;

#endif				/* __NETXEN_NIC_HW_H_ */

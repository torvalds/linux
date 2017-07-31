/*
 * Applied Micro X-Gene SoC Ethernet v2 Driver
 *
 * Copyright (c) 2017, Applied Micro Circuits Corporation
 * Author(s): Iyappan Subramanian <isubramanian@apm.com>
 *	      Keyur Chudgar <kchudgar@apm.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __XGENE_ENET_V2_MAC_H__
#define __XGENE_ENET_V2_MAC_H__

/* Register offsets */
#define MAC_CONFIG_1		0xa000
#define MAC_CONFIG_2		0xa004
#define MII_MGMT_CONFIG		0xa020
#define MII_MGMT_COMMAND	0xa024
#define MII_MGMT_ADDRESS	0xa028
#define MII_MGMT_CONTROL	0xa02c
#define MII_MGMT_STATUS		0xa030
#define MII_MGMT_INDICATORS	0xa034
#define INTERFACE_CONTROL	0xa038
#define STATION_ADDR0		0xa040
#define STATION_ADDR1		0xa044

#define RGMII_REG_0		0x27e0
#define ICM_CONFIG0_REG_0	0x2c00
#define ICM_CONFIG2_REG_0	0x2c08
#define ECM_CONFIG0_REG_0	0x2d00

/* Register fields */
#define SOFT_RESET		BIT(31)
#define TX_EN			BIT(0)
#define RX_EN			BIT(2)
#define PAD_CRC			BIT(2)
#define CRC_EN			BIT(1)
#define FULL_DUPLEX		BIT(0)

#define INTF_MODE_POS		8
#define INTF_MODE_LEN		2
#define HD_MODE_POS		25
#define HD_MODE_LEN		2
#define CFG_MACMODE_POS		18
#define CFG_MACMODE_LEN		2
#define CFG_WAITASYNCRD_POS	0
#define CFG_WAITASYNCRD_LEN	16
#define CFG_SPEED_125_POS	24
#define CFG_WFIFOFULLTHR_POS	0
#define CFG_WFIFOFULLTHR_LEN	7
#define MGMT_CLOCK_SEL_POS	0
#define MGMT_CLOCK_SEL_LEN	3
#define PHY_ADDR_POS		8
#define PHY_ADDR_LEN		5
#define REG_ADDR_POS		0
#define REG_ADDR_LEN		5
#define MII_MGMT_BUSY		BIT(0)
#define MII_READ_CYCLE		BIT(0)
#define CFG_WAITASYNCRD_EN	BIT(16)

static inline void xgene_set_reg_bits(u32 *var, int pos, int len, u32 val)
{
	u32 mask = GENMASK(pos + len, pos);

	*var &= ~mask;
	*var |= ((val << pos) & mask);
}

static inline u32 xgene_get_reg_bits(u32 var, int pos, int len)
{
	u32 mask = GENMASK(pos + len, pos);

	return (var & mask) >> pos;
}

#define SET_REG_BITS(var, field, val)					\
	xgene_set_reg_bits(var, field ## _POS, field ## _LEN, val)

#define SET_REG_BIT(var, field, val)					\
	xgene_set_reg_bits(var, field ## _POS, 1, val)

#define GET_REG_BITS(var, field)					\
	xgene_get_reg_bits(var, field ## _POS, field ## _LEN)

#define GET_REG_BIT(var, field)		((var) & (field))

struct xge_pdata;

void xge_mac_reset(struct xge_pdata *pdata);
void xge_mac_set_speed(struct xge_pdata *pdata);
void xge_mac_enable(struct xge_pdata *pdata);
void xge_mac_disable(struct xge_pdata *pdata);
void xge_mac_init(struct xge_pdata *pdata);
void xge_mac_set_station_addr(struct xge_pdata *pdata);

#endif /* __XGENE_ENET_V2_MAC_H__ */

/* Applied Micro X-Gene SoC Ethernet Driver
 *
 * Copyright (c) 2014, Applied Micro Circuits Corporation
 * Authors: Iyappan Subramanian <isubramanian@apm.com>
 *	    Ravi Patel <rapatel@apm.com>
 *	    Keyur Chudgar <kchudgar@apm.com>
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

#ifndef __XGENE_ENET_HW_H__
#define __XGENE_ENET_HW_H__

#include "xgene_enet_main.h"

struct xgene_enet_pdata;
struct xgene_enet_stats;
struct xgene_enet_desc_ring;

/* clears and then set bits */
static inline void xgene_set_bits(u32 *dst, u32 val, u32 start, u32 len)
{
	u32 end = start + len - 1;
	u32 mask = GENMASK(end, start);

	*dst &= ~mask;
	*dst |= (val << start) & mask;
}

static inline u32 xgene_get_bits(u32 val, u32 start, u32 end)
{
	return (val & GENMASK(end, start)) >> start;
}

enum xgene_enet_rm {
	RM0,
	RM1,
	RM3 = 3
};

#define CSR_RING_ID		0x0008
#define OVERWRITE		BIT(31)
#define IS_BUFFER_POOL		BIT(20)
#define PREFETCH_BUF_EN		BIT(21)
#define CSR_RING_ID_BUF		0x000c
#define CSR_PBM_COAL		0x0014
#define CSR_PBM_CTICK1		0x001c
#define CSR_PBM_CTICK2		0x0020
#define CSR_THRESHOLD0_SET1	0x0030
#define CSR_THRESHOLD1_SET1	0x0034
#define CSR_RING_NE_INT_MODE	0x017c
#define CSR_RING_CONFIG		0x006c
#define CSR_RING_WR_BASE	0x0070
#define NUM_RING_CONFIG		5
#define BUFPOOL_MODE		3
#define INC_DEC_CMD_ADDR	0x002c
#define UDP_HDR_SIZE		2
#define BUF_LEN_CODE_2K		0x5000

#define CREATE_MASK(pos, len)		GENMASK((pos)+(len)-1, (pos))
#define CREATE_MASK_ULL(pos, len)	GENMASK_ULL((pos)+(len)-1, (pos))

/* Empty slot soft signature */
#define EMPTY_SLOT_INDEX	1
#define EMPTY_SLOT		~0ULL

#define WORK_DESC_SIZE		32
#define BUFPOOL_DESC_SIZE	16

#define RING_OWNER_MASK		GENMASK(9, 6)
#define RING_BUFNUM_MASK	GENMASK(5, 0)

#define SELTHRSH_POS		3
#define SELTHRSH_LEN		3
#define RINGADDRL_POS		5
#define RINGADDRL_LEN		27
#define RINGADDRH_POS		0
#define RINGADDRH_LEN		7
#define RINGSIZE_POS		23
#define RINGSIZE_LEN		3
#define RINGTYPE_POS		19
#define RINGTYPE_LEN		2
#define RINGMODE_POS		20
#define RINGMODE_LEN		3
#define RECOMTIMEOUTL_POS	28
#define RECOMTIMEOUTL_LEN	4
#define RECOMTIMEOUTH_POS	0
#define RECOMTIMEOUTH_LEN	3
#define NUMMSGSINQ_POS		1
#define NUMMSGSINQ_LEN		16
#define ACCEPTLERR		BIT(19)
#define QCOHERENT		BIT(4)
#define RECOMBBUF		BIT(27)

#define MAC_OFFSET			0x30
#define OFFSET_4			0x04
#define OFFSET_8			0x08

#define BLOCK_ETH_CSR_OFFSET		0x2000
#define BLOCK_ETH_CLE_CSR_OFFSET	0x6000
#define BLOCK_ETH_RING_IF_OFFSET	0x9000
#define BLOCK_ETH_CLKRST_CSR_OFFSET	0xc000
#define BLOCK_ETH_DIAG_CSR_OFFSET	0xD000
#define BLOCK_ETH_MAC_OFFSET		0x0000
#define BLOCK_ETH_MAC_CSR_OFFSET	0x2800

#define CLKEN_ADDR			0xc208
#define SRST_ADDR			0xc200

#define MAC_ADDR_REG_OFFSET		0x00
#define MAC_COMMAND_REG_OFFSET		0x04
#define MAC_WRITE_REG_OFFSET		0x08
#define MAC_READ_REG_OFFSET		0x0c
#define MAC_COMMAND_DONE_REG_OFFSET	0x10

#define PCS_ADDR_REG_OFFSET		0x00
#define PCS_COMMAND_REG_OFFSET		0x04
#define PCS_WRITE_REG_OFFSET		0x08
#define PCS_READ_REG_OFFSET		0x0c
#define PCS_COMMAND_DONE_REG_OFFSET	0x10

#define MII_MGMT_CONFIG_ADDR		0x20
#define MII_MGMT_COMMAND_ADDR		0x24
#define MII_MGMT_ADDRESS_ADDR		0x28
#define MII_MGMT_CONTROL_ADDR		0x2c
#define MII_MGMT_STATUS_ADDR		0x30
#define MII_MGMT_INDICATORS_ADDR	0x34

#define BUSY_MASK			BIT(0)
#define READ_CYCLE_MASK			BIT(0)
#define PHY_CONTROL_SET(dst, val)	xgene_set_bits(dst, val, 0, 16)

#define ENET_SPARE_CFG_REG_ADDR		0x0750
#define RSIF_CONFIG_REG_ADDR		0x0010
#define RSIF_RAM_DBG_REG0_ADDR		0x0048
#define RGMII_REG_0_ADDR		0x07e0
#define CFG_LINK_AGGR_RESUME_0_ADDR	0x07c8
#define DEBUG_REG_ADDR			0x0700
#define CFG_BYPASS_ADDR			0x0294
#define CLE_BYPASS_REG0_0_ADDR		0x0490
#define CLE_BYPASS_REG1_0_ADDR		0x0494
#define CFG_RSIF_FPBUFF_TIMEOUT_EN	BIT(31)
#define RESUME_TX			BIT(0)
#define CFG_SPEED_1250			BIT(24)
#define TX_PORT0			BIT(0)
#define CFG_BYPASS_UNISEC_TX		BIT(2)
#define CFG_BYPASS_UNISEC_RX		BIT(1)
#define CFG_CLE_BYPASS_EN0		BIT(31)
#define CFG_TXCLK_MUXSEL0_SET(dst, val)	xgene_set_bits(dst, val, 29, 3)
#define CFG_RXCLK_MUXSEL0_SET(dst, val)	xgene_set_bits(dst, val, 26, 3)

#define CFG_CLE_IP_PROTOCOL0_SET(dst, val)	xgene_set_bits(dst, val, 16, 2)
#define CFG_CLE_DSTQID0_SET(dst, val)		xgene_set_bits(dst, val, 0, 12)
#define CFG_CLE_FPSEL0_SET(dst, val)		xgene_set_bits(dst, val, 16, 4)
#define CFG_MACMODE_SET(dst, val)		xgene_set_bits(dst, val, 18, 2)
#define CFG_WAITASYNCRD_SET(dst, val)		xgene_set_bits(dst, val, 0, 16)
#define CFG_CLE_DSTQID0(val)		(val & GENMASK(11, 0))
#define CFG_CLE_FPSEL0(val)		((val << 16) & GENMASK(19, 16))
#define ICM_CONFIG0_REG_0_ADDR		0x0400
#define ICM_CONFIG2_REG_0_ADDR		0x0410
#define RX_DV_GATE_REG_0_ADDR		0x05fc
#define TX_DV_GATE_EN0			BIT(2)
#define RX_DV_GATE_EN0			BIT(1)
#define RESUME_RX0			BIT(0)
#define ENET_CFGSSQMIFPRESET_ADDR		0x14
#define ENET_CFGSSQMIWQRESET_ADDR		0x1c
#define ENET_CFGSSQMIWQASSOC_ADDR		0xe0
#define ENET_CFGSSQMIFPQASSOC_ADDR		0xdc
#define ENET_CFGSSQMIQMLITEFPQASSOC_ADDR	0xf0
#define ENET_CFGSSQMIQMLITEWQASSOC_ADDR		0xf4
#define ENET_CFG_MEM_RAM_SHUTDOWN_ADDR		0x70
#define ENET_BLOCK_MEM_RDY_ADDR			0x74
#define MAC_CONFIG_1_ADDR			0x00
#define MAC_CONFIG_2_ADDR			0x04
#define MAX_FRAME_LEN_ADDR			0x10
#define INTERFACE_CONTROL_ADDR			0x38
#define STATION_ADDR0_ADDR			0x40
#define STATION_ADDR1_ADDR			0x44
#define PHY_ADDR_SET(dst, val)			xgene_set_bits(dst, val, 8, 5)
#define REG_ADDR_SET(dst, val)			xgene_set_bits(dst, val, 0, 5)
#define ENET_INTERFACE_MODE2_SET(dst, val)	xgene_set_bits(dst, val, 8, 2)
#define MGMT_CLOCK_SEL_SET(dst, val)		xgene_set_bits(dst, val, 0, 3)
#define SOFT_RESET1			BIT(31)
#define TX_EN				BIT(0)
#define RX_EN				BIT(2)
#define ENET_LHD_MODE			BIT(25)
#define ENET_GHD_MODE			BIT(26)
#define FULL_DUPLEX2			BIT(0)
#define PAD_CRC				BIT(2)
#define SCAN_AUTO_INCR			BIT(5)
#define TBYT_ADDR			0x38
#define TPKT_ADDR			0x39
#define TDRP_ADDR			0x45
#define TFCS_ADDR			0x47
#define TUND_ADDR			0x4a

#define TSO_IPPROTO_TCP			1

#define USERINFO_POS			0
#define USERINFO_LEN			32
#define FPQNUM_POS			32
#define FPQNUM_LEN			12
#define ELERR_POS                       46
#define ELERR_LEN                       2
#define NV_POS				50
#define NV_LEN				1
#define LL_POS				51
#define LL_LEN				1
#define LERR_POS			60
#define LERR_LEN			3
#define STASH_POS			52
#define STASH_LEN			2
#define BUFDATALEN_POS			48
#define BUFDATALEN_LEN			15
#define DATAADDR_POS			0
#define DATAADDR_LEN			42
#define COHERENT_POS			63
#define HENQNUM_POS			48
#define HENQNUM_LEN			12
#define TYPESEL_POS			44
#define TYPESEL_LEN			4
#define ETHHDR_POS			12
#define ETHHDR_LEN			8
#define IC_POS				35	/* Insert CRC */
#define TCPHDR_POS			0
#define TCPHDR_LEN			6
#define IPHDR_POS			6
#define IPHDR_LEN			6
#define MSS_POS				20
#define MSS_LEN				2
#define EC_POS				22	/* Enable checksum */
#define EC_LEN				1
#define ET_POS				23	/* Enable TSO */
#define IS_POS				24	/* IP protocol select */
#define IS_LEN				1
#define TYPE_ETH_WORK_MESSAGE_POS	44
#define LL_BYTES_MSB_POS		56
#define LL_BYTES_MSB_LEN		8
#define LL_BYTES_LSB_POS		48
#define LL_BYTES_LSB_LEN		12
#define LL_LEN_POS			48
#define LL_LEN_LEN			8
#define DATALEN_MASK			GENMASK(11, 0)

#define LAST_BUFFER			(0x7800ULL << BUFDATALEN_POS)

#define TSO_MSS0_POS			0
#define TSO_MSS0_LEN			14
#define TSO_MSS1_POS			16
#define TSO_MSS1_LEN			14

struct xgene_enet_raw_desc {
	__le64 m0;
	__le64 m1;
	__le64 m2;
	__le64 m3;
};

struct xgene_enet_raw_desc16 {
	__le64 m0;
	__le64 m1;
};

static inline void xgene_enet_mark_desc_slot_empty(void *desc_slot_ptr)
{
	__le64 *desc_slot = desc_slot_ptr;

	desc_slot[EMPTY_SLOT_INDEX] = cpu_to_le64(EMPTY_SLOT);
}

static inline bool xgene_enet_is_desc_slot_empty(void *desc_slot_ptr)
{
	__le64 *desc_slot = desc_slot_ptr;

	return (desc_slot[EMPTY_SLOT_INDEX] == cpu_to_le64(EMPTY_SLOT));
}

enum xgene_enet_ring_cfgsize {
	RING_CFGSIZE_512B,
	RING_CFGSIZE_2KB,
	RING_CFGSIZE_16KB,
	RING_CFGSIZE_64KB,
	RING_CFGSIZE_512KB,
	RING_CFGSIZE_INVALID
};

enum xgene_enet_ring_type {
	RING_DISABLED,
	RING_REGULAR,
	RING_BUFPOOL
};

enum xgene_ring_owner {
	RING_OWNER_ETH0,
	RING_OWNER_ETH1,
	RING_OWNER_CPU = 15,
	RING_OWNER_INVALID
};

enum xgene_enet_ring_bufnum {
	RING_BUFNUM_REGULAR = 0x0,
	RING_BUFNUM_BUFPOOL = 0x20,
	RING_BUFNUM_INVALID
};

enum xgene_enet_err_code {
	HBF_READ_DATA = 3,
	HBF_LL_READ = 4,
	BAD_WORK_MSG = 6,
	BUFPOOL_TIMEOUT = 15,
	INGRESS_CRC = 16,
	INGRESS_CHECKSUM = 17,
	INGRESS_TRUNC_FRAME = 18,
	INGRESS_PKT_LEN = 19,
	INGRESS_PKT_UNDER = 20,
	INGRESS_FIFO_OVERRUN = 21,
	INGRESS_CHECKSUM_COMPUTE = 26,
	ERR_CODE_INVALID
};

static inline enum xgene_ring_owner xgene_enet_ring_owner(u16 id)
{
	return (id & RING_OWNER_MASK) >> 6;
}

static inline u8 xgene_enet_ring_bufnum(u16 id)
{
	return id & RING_BUFNUM_MASK;
}

static inline bool xgene_enet_is_bufpool(u16 id)
{
	return ((id & RING_BUFNUM_MASK) >= 0x20) ? true : false;
}

static inline u16 xgene_enet_get_numslots(u16 id, u32 size)
{
	bool is_bufpool = xgene_enet_is_bufpool(id);

	return (is_bufpool) ? size / BUFPOOL_DESC_SIZE :
		      size / WORK_DESC_SIZE;
}

void xgene_enet_parse_error(struct xgene_enet_desc_ring *ring,
			    struct xgene_enet_pdata *pdata,
			    enum xgene_enet_err_code status);

int xgene_enet_mdio_config(struct xgene_enet_pdata *pdata);
void xgene_enet_mdio_remove(struct xgene_enet_pdata *pdata);
bool xgene_ring_mgr_init(struct xgene_enet_pdata *p);
int xgene_enet_phy_connect(struct net_device *ndev);
void xgene_enet_phy_disconnect(struct xgene_enet_pdata *pdata);

extern const struct xgene_mac_ops xgene_gmac_ops;
extern const struct xgene_port_ops xgene_gport_ops;
extern struct xgene_ring_ops xgene_ring1_ops;

#endif /* __XGENE_ENET_HW_H__ */

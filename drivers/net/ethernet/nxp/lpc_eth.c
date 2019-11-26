// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * drivers/net/ethernet/nxp/lpc_eth.c
 *
 * Author: Kevin Wells <kevin.wells@nxp.com>
 *
 * Copyright (C) 2010 NXP Semiconductors
 * Copyright (C) 2012 Roland Stigge <stigge@antcom.de>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/clk.h>
#include <linux/crc32.h>
#include <linux/etherdevice.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/soc/nxp/lpc32xx-misc.h>

#define MODNAME "lpc-eth"
#define DRV_VERSION "1.00"

#define ENET_MAXF_SIZE 1536
#define ENET_RX_DESC 48
#define ENET_TX_DESC 16

#define NAPI_WEIGHT 16

/*
 * Ethernet MAC controller Register offsets
 */
#define LPC_ENET_MAC1(x)			(x + 0x000)
#define LPC_ENET_MAC2(x)			(x + 0x004)
#define LPC_ENET_IPGT(x)			(x + 0x008)
#define LPC_ENET_IPGR(x)			(x + 0x00C)
#define LPC_ENET_CLRT(x)			(x + 0x010)
#define LPC_ENET_MAXF(x)			(x + 0x014)
#define LPC_ENET_SUPP(x)			(x + 0x018)
#define LPC_ENET_TEST(x)			(x + 0x01C)
#define LPC_ENET_MCFG(x)			(x + 0x020)
#define LPC_ENET_MCMD(x)			(x + 0x024)
#define LPC_ENET_MADR(x)			(x + 0x028)
#define LPC_ENET_MWTD(x)			(x + 0x02C)
#define LPC_ENET_MRDD(x)			(x + 0x030)
#define LPC_ENET_MIND(x)			(x + 0x034)
#define LPC_ENET_SA0(x)				(x + 0x040)
#define LPC_ENET_SA1(x)				(x + 0x044)
#define LPC_ENET_SA2(x)				(x + 0x048)
#define LPC_ENET_COMMAND(x)			(x + 0x100)
#define LPC_ENET_STATUS(x)			(x + 0x104)
#define LPC_ENET_RXDESCRIPTOR(x)		(x + 0x108)
#define LPC_ENET_RXSTATUS(x)			(x + 0x10C)
#define LPC_ENET_RXDESCRIPTORNUMBER(x)		(x + 0x110)
#define LPC_ENET_RXPRODUCEINDEX(x)		(x + 0x114)
#define LPC_ENET_RXCONSUMEINDEX(x)		(x + 0x118)
#define LPC_ENET_TXDESCRIPTOR(x)		(x + 0x11C)
#define LPC_ENET_TXSTATUS(x)			(x + 0x120)
#define LPC_ENET_TXDESCRIPTORNUMBER(x)		(x + 0x124)
#define LPC_ENET_TXPRODUCEINDEX(x)		(x + 0x128)
#define LPC_ENET_TXCONSUMEINDEX(x)		(x + 0x12C)
#define LPC_ENET_TSV0(x)			(x + 0x158)
#define LPC_ENET_TSV1(x)			(x + 0x15C)
#define LPC_ENET_RSV(x)				(x + 0x160)
#define LPC_ENET_FLOWCONTROLCOUNTER(x)		(x + 0x170)
#define LPC_ENET_FLOWCONTROLSTATUS(x)		(x + 0x174)
#define LPC_ENET_RXFILTER_CTRL(x)		(x + 0x200)
#define LPC_ENET_RXFILTERWOLSTATUS(x)		(x + 0x204)
#define LPC_ENET_RXFILTERWOLCLEAR(x)		(x + 0x208)
#define LPC_ENET_HASHFILTERL(x)			(x + 0x210)
#define LPC_ENET_HASHFILTERH(x)			(x + 0x214)
#define LPC_ENET_INTSTATUS(x)			(x + 0xFE0)
#define LPC_ENET_INTENABLE(x)			(x + 0xFE4)
#define LPC_ENET_INTCLEAR(x)			(x + 0xFE8)
#define LPC_ENET_INTSET(x)			(x + 0xFEC)
#define LPC_ENET_POWERDOWN(x)			(x + 0xFF4)

/*
 * mac1 register definitions
 */
#define LPC_MAC1_RECV_ENABLE			(1 << 0)
#define LPC_MAC1_PASS_ALL_RX_FRAMES		(1 << 1)
#define LPC_MAC1_RX_FLOW_CONTROL		(1 << 2)
#define LPC_MAC1_TX_FLOW_CONTROL		(1 << 3)
#define LPC_MAC1_LOOPBACK			(1 << 4)
#define LPC_MAC1_RESET_TX			(1 << 8)
#define LPC_MAC1_RESET_MCS_TX			(1 << 9)
#define LPC_MAC1_RESET_RX			(1 << 10)
#define LPC_MAC1_RESET_MCS_RX			(1 << 11)
#define LPC_MAC1_SIMULATION_RESET		(1 << 14)
#define LPC_MAC1_SOFT_RESET			(1 << 15)

/*
 * mac2 register definitions
 */
#define LPC_MAC2_FULL_DUPLEX			(1 << 0)
#define LPC_MAC2_FRAME_LENGTH_CHECKING		(1 << 1)
#define LPC_MAC2_HUGH_LENGTH_CHECKING		(1 << 2)
#define LPC_MAC2_DELAYED_CRC			(1 << 3)
#define LPC_MAC2_CRC_ENABLE			(1 << 4)
#define LPC_MAC2_PAD_CRC_ENABLE			(1 << 5)
#define LPC_MAC2_VLAN_PAD_ENABLE		(1 << 6)
#define LPC_MAC2_AUTO_DETECT_PAD_ENABLE		(1 << 7)
#define LPC_MAC2_PURE_PREAMBLE_ENFORCEMENT	(1 << 8)
#define LPC_MAC2_LONG_PREAMBLE_ENFORCEMENT	(1 << 9)
#define LPC_MAC2_NO_BACKOFF			(1 << 12)
#define LPC_MAC2_BACK_PRESSURE			(1 << 13)
#define LPC_MAC2_EXCESS_DEFER			(1 << 14)

/*
 * ipgt register definitions
 */
#define LPC_IPGT_LOAD(n)			((n) & 0x7F)

/*
 * ipgr register definitions
 */
#define LPC_IPGR_LOAD_PART2(n)			((n) & 0x7F)
#define LPC_IPGR_LOAD_PART1(n)			(((n) & 0x7F) << 8)

/*
 * clrt register definitions
 */
#define LPC_CLRT_LOAD_RETRY_MAX(n)		((n) & 0xF)
#define LPC_CLRT_LOAD_COLLISION_WINDOW(n)	(((n) & 0x3F) << 8)

/*
 * maxf register definitions
 */
#define LPC_MAXF_LOAD_MAX_FRAME_LEN(n)		((n) & 0xFFFF)

/*
 * supp register definitions
 */
#define LPC_SUPP_SPEED				(1 << 8)
#define LPC_SUPP_RESET_RMII			(1 << 11)

/*
 * test register definitions
 */
#define LPC_TEST_SHORTCUT_PAUSE_QUANTA		(1 << 0)
#define LPC_TEST_PAUSE				(1 << 1)
#define LPC_TEST_BACKPRESSURE			(1 << 2)

/*
 * mcfg register definitions
 */
#define LPC_MCFG_SCAN_INCREMENT			(1 << 0)
#define LPC_MCFG_SUPPRESS_PREAMBLE		(1 << 1)
#define LPC_MCFG_CLOCK_SELECT(n)		(((n) & 0x7) << 2)
#define LPC_MCFG_CLOCK_HOST_DIV_4		0
#define LPC_MCFG_CLOCK_HOST_DIV_6		2
#define LPC_MCFG_CLOCK_HOST_DIV_8		3
#define LPC_MCFG_CLOCK_HOST_DIV_10		4
#define LPC_MCFG_CLOCK_HOST_DIV_14		5
#define LPC_MCFG_CLOCK_HOST_DIV_20		6
#define LPC_MCFG_CLOCK_HOST_DIV_28		7
#define LPC_MCFG_RESET_MII_MGMT			(1 << 15)

/*
 * mcmd register definitions
 */
#define LPC_MCMD_READ				(1 << 0)
#define LPC_MCMD_SCAN				(1 << 1)

/*
 * madr register definitions
 */
#define LPC_MADR_REGISTER_ADDRESS(n)		((n) & 0x1F)
#define LPC_MADR_PHY_0ADDRESS(n)		(((n) & 0x1F) << 8)

/*
 * mwtd register definitions
 */
#define LPC_MWDT_WRITE(n)			((n) & 0xFFFF)

/*
 * mrdd register definitions
 */
#define LPC_MRDD_READ_MASK			0xFFFF

/*
 * mind register definitions
 */
#define LPC_MIND_BUSY				(1 << 0)
#define LPC_MIND_SCANNING			(1 << 1)
#define LPC_MIND_NOT_VALID			(1 << 2)
#define LPC_MIND_MII_LINK_FAIL			(1 << 3)

/*
 * command register definitions
 */
#define LPC_COMMAND_RXENABLE			(1 << 0)
#define LPC_COMMAND_TXENABLE			(1 << 1)
#define LPC_COMMAND_REG_RESET			(1 << 3)
#define LPC_COMMAND_TXRESET			(1 << 4)
#define LPC_COMMAND_RXRESET			(1 << 5)
#define LPC_COMMAND_PASSRUNTFRAME		(1 << 6)
#define LPC_COMMAND_PASSRXFILTER		(1 << 7)
#define LPC_COMMAND_TXFLOWCONTROL		(1 << 8)
#define LPC_COMMAND_RMII			(1 << 9)
#define LPC_COMMAND_FULLDUPLEX			(1 << 10)

/*
 * status register definitions
 */
#define LPC_STATUS_RXACTIVE			(1 << 0)
#define LPC_STATUS_TXACTIVE			(1 << 1)

/*
 * tsv0 register definitions
 */
#define LPC_TSV0_CRC_ERROR			(1 << 0)
#define LPC_TSV0_LENGTH_CHECK_ERROR		(1 << 1)
#define LPC_TSV0_LENGTH_OUT_OF_RANGE		(1 << 2)
#define LPC_TSV0_DONE				(1 << 3)
#define LPC_TSV0_MULTICAST			(1 << 4)
#define LPC_TSV0_BROADCAST			(1 << 5)
#define LPC_TSV0_PACKET_DEFER			(1 << 6)
#define LPC_TSV0_ESCESSIVE_DEFER		(1 << 7)
#define LPC_TSV0_ESCESSIVE_COLLISION		(1 << 8)
#define LPC_TSV0_LATE_COLLISION			(1 << 9)
#define LPC_TSV0_GIANT				(1 << 10)
#define LPC_TSV0_UNDERRUN			(1 << 11)
#define LPC_TSV0_TOTAL_BYTES(n)			(((n) >> 12) & 0xFFFF)
#define LPC_TSV0_CONTROL_FRAME			(1 << 28)
#define LPC_TSV0_PAUSE				(1 << 29)
#define LPC_TSV0_BACKPRESSURE			(1 << 30)
#define LPC_TSV0_VLAN				(1 << 31)

/*
 * tsv1 register definitions
 */
#define LPC_TSV1_TRANSMIT_BYTE_COUNT(n)		((n) & 0xFFFF)
#define LPC_TSV1_COLLISION_COUNT(n)		(((n) >> 16) & 0xF)

/*
 * rsv register definitions
 */
#define LPC_RSV_RECEIVED_BYTE_COUNT(n)		((n) & 0xFFFF)
#define LPC_RSV_RXDV_EVENT_IGNORED		(1 << 16)
#define LPC_RSV_RXDV_EVENT_PREVIOUSLY_SEEN	(1 << 17)
#define LPC_RSV_CARRIER_EVNT_PREVIOUS_SEEN	(1 << 18)
#define LPC_RSV_RECEIVE_CODE_VIOLATION		(1 << 19)
#define LPC_RSV_CRC_ERROR			(1 << 20)
#define LPC_RSV_LENGTH_CHECK_ERROR		(1 << 21)
#define LPC_RSV_LENGTH_OUT_OF_RANGE		(1 << 22)
#define LPC_RSV_RECEIVE_OK			(1 << 23)
#define LPC_RSV_MULTICAST			(1 << 24)
#define LPC_RSV_BROADCAST			(1 << 25)
#define LPC_RSV_DRIBBLE_NIBBLE			(1 << 26)
#define LPC_RSV_CONTROL_FRAME			(1 << 27)
#define LPC_RSV_PAUSE				(1 << 28)
#define LPC_RSV_UNSUPPORTED_OPCODE		(1 << 29)
#define LPC_RSV_VLAN				(1 << 30)

/*
 * flowcontrolcounter register definitions
 */
#define LPC_FCCR_MIRRORCOUNTER(n)		((n) & 0xFFFF)
#define LPC_FCCR_PAUSETIMER(n)			(((n) >> 16) & 0xFFFF)

/*
 * flowcontrolstatus register definitions
 */
#define LPC_FCCR_MIRRORCOUNTERCURRENT(n)	((n) & 0xFFFF)

/*
 * rxfilterctrl, rxfilterwolstatus, and rxfilterwolclear shared
 * register definitions
 */
#define LPC_RXFLTRW_ACCEPTUNICAST		(1 << 0)
#define LPC_RXFLTRW_ACCEPTUBROADCAST		(1 << 1)
#define LPC_RXFLTRW_ACCEPTUMULTICAST		(1 << 2)
#define LPC_RXFLTRW_ACCEPTUNICASTHASH		(1 << 3)
#define LPC_RXFLTRW_ACCEPTUMULTICASTHASH	(1 << 4)
#define LPC_RXFLTRW_ACCEPTPERFECT		(1 << 5)

/*
 * rxfilterctrl register definitions
 */
#define LPC_RXFLTRWSTS_MAGICPACKETENWOL		(1 << 12)
#define LPC_RXFLTRWSTS_RXFILTERENWOL		(1 << 13)

/*
 * rxfilterwolstatus/rxfilterwolclear register definitions
 */
#define LPC_RXFLTRWSTS_RXFILTERWOL		(1 << 7)
#define LPC_RXFLTRWSTS_MAGICPACKETWOL		(1 << 8)

/*
 * intstatus, intenable, intclear, and Intset shared register
 * definitions
 */
#define LPC_MACINT_RXOVERRUNINTEN		(1 << 0)
#define LPC_MACINT_RXERRORONINT			(1 << 1)
#define LPC_MACINT_RXFINISHEDINTEN		(1 << 2)
#define LPC_MACINT_RXDONEINTEN			(1 << 3)
#define LPC_MACINT_TXUNDERRUNINTEN		(1 << 4)
#define LPC_MACINT_TXERRORINTEN			(1 << 5)
#define LPC_MACINT_TXFINISHEDINTEN		(1 << 6)
#define LPC_MACINT_TXDONEINTEN			(1 << 7)
#define LPC_MACINT_SOFTINTEN			(1 << 12)
#define LPC_MACINT_WAKEUPINTEN			(1 << 13)

/*
 * powerdown register definitions
 */
#define LPC_POWERDOWN_MACAHB			(1 << 31)

static phy_interface_t lpc_phy_interface_mode(struct device *dev)
{
	if (dev && dev->of_node) {
		const char *mode = of_get_property(dev->of_node,
						   "phy-mode", NULL);
		if (mode && !strcmp(mode, "mii"))
			return PHY_INTERFACE_MODE_MII;
	}
	return PHY_INTERFACE_MODE_RMII;
}

static bool use_iram_for_net(struct device *dev)
{
	if (dev && dev->of_node)
		return of_property_read_bool(dev->of_node, "use-iram");
	return false;
}

/* Receive Status information word */
#define RXSTATUS_SIZE			0x000007FF
#define RXSTATUS_CONTROL		(1 << 18)
#define RXSTATUS_VLAN			(1 << 19)
#define RXSTATUS_FILTER			(1 << 20)
#define RXSTATUS_MULTICAST		(1 << 21)
#define RXSTATUS_BROADCAST		(1 << 22)
#define RXSTATUS_CRC			(1 << 23)
#define RXSTATUS_SYMBOL			(1 << 24)
#define RXSTATUS_LENGTH			(1 << 25)
#define RXSTATUS_RANGE			(1 << 26)
#define RXSTATUS_ALIGN			(1 << 27)
#define RXSTATUS_OVERRUN		(1 << 28)
#define RXSTATUS_NODESC			(1 << 29)
#define RXSTATUS_LAST			(1 << 30)
#define RXSTATUS_ERROR			(1 << 31)

#define RXSTATUS_STATUS_ERROR \
	(RXSTATUS_NODESC | RXSTATUS_OVERRUN | RXSTATUS_ALIGN | \
	 RXSTATUS_RANGE | RXSTATUS_LENGTH | RXSTATUS_SYMBOL | RXSTATUS_CRC)

/* Receive Descriptor control word */
#define RXDESC_CONTROL_SIZE		0x000007FF
#define RXDESC_CONTROL_INT		(1 << 31)

/* Transmit Status information word */
#define TXSTATUS_COLLISIONS_GET(x)	(((x) >> 21) & 0xF)
#define TXSTATUS_DEFER			(1 << 25)
#define TXSTATUS_EXCESSDEFER		(1 << 26)
#define TXSTATUS_EXCESSCOLL		(1 << 27)
#define TXSTATUS_LATECOLL		(1 << 28)
#define TXSTATUS_UNDERRUN		(1 << 29)
#define TXSTATUS_NODESC			(1 << 30)
#define TXSTATUS_ERROR			(1 << 31)

/* Transmit Descriptor control word */
#define TXDESC_CONTROL_SIZE		0x000007FF
#define TXDESC_CONTROL_OVERRIDE		(1 << 26)
#define TXDESC_CONTROL_HUGE		(1 << 27)
#define TXDESC_CONTROL_PAD		(1 << 28)
#define TXDESC_CONTROL_CRC		(1 << 29)
#define TXDESC_CONTROL_LAST		(1 << 30)
#define TXDESC_CONTROL_INT		(1 << 31)

/*
 * Structure of a TX/RX descriptors and RX status
 */
struct txrx_desc_t {
	__le32 packet;
	__le32 control;
};
struct rx_status_t {
	__le32 statusinfo;
	__le32 statushashcrc;
};

/*
 * Device driver data structure
 */
struct netdata_local {
	struct platform_device	*pdev;
	struct net_device	*ndev;
	struct device_node	*phy_node;
	spinlock_t		lock;
	void __iomem		*net_base;
	u32			msg_enable;
	unsigned int		skblen[ENET_TX_DESC];
	unsigned int		last_tx_idx;
	unsigned int		num_used_tx_buffs;
	struct mii_bus		*mii_bus;
	struct clk		*clk;
	dma_addr_t		dma_buff_base_p;
	void			*dma_buff_base_v;
	size_t			dma_buff_size;
	struct txrx_desc_t	*tx_desc_v;
	u32			*tx_stat_v;
	void			*tx_buff_v;
	struct txrx_desc_t	*rx_desc_v;
	struct rx_status_t	*rx_stat_v;
	void			*rx_buff_v;
	int			link;
	int			speed;
	int			duplex;
	struct napi_struct	napi;
};

/*
 * MAC support functions
 */
static void __lpc_set_mac(struct netdata_local *pldat, u8 *mac)
{
	u32 tmp;

	/* Set station address */
	tmp = mac[0] | ((u32)mac[1] << 8);
	writel(tmp, LPC_ENET_SA2(pldat->net_base));
	tmp = mac[2] | ((u32)mac[3] << 8);
	writel(tmp, LPC_ENET_SA1(pldat->net_base));
	tmp = mac[4] | ((u32)mac[5] << 8);
	writel(tmp, LPC_ENET_SA0(pldat->net_base));

	netdev_dbg(pldat->ndev, "Ethernet MAC address %pM\n", mac);
}

static void __lpc_get_mac(struct netdata_local *pldat, u8 *mac)
{
	u32 tmp;

	/* Get station address */
	tmp = readl(LPC_ENET_SA2(pldat->net_base));
	mac[0] = tmp & 0xFF;
	mac[1] = tmp >> 8;
	tmp = readl(LPC_ENET_SA1(pldat->net_base));
	mac[2] = tmp & 0xFF;
	mac[3] = tmp >> 8;
	tmp = readl(LPC_ENET_SA0(pldat->net_base));
	mac[4] = tmp & 0xFF;
	mac[5] = tmp >> 8;
}

static void __lpc_params_setup(struct netdata_local *pldat)
{
	u32 tmp;

	if (pldat->duplex == DUPLEX_FULL) {
		tmp = readl(LPC_ENET_MAC2(pldat->net_base));
		tmp |= LPC_MAC2_FULL_DUPLEX;
		writel(tmp, LPC_ENET_MAC2(pldat->net_base));
		tmp = readl(LPC_ENET_COMMAND(pldat->net_base));
		tmp |= LPC_COMMAND_FULLDUPLEX;
		writel(tmp, LPC_ENET_COMMAND(pldat->net_base));
		writel(LPC_IPGT_LOAD(0x15), LPC_ENET_IPGT(pldat->net_base));
	} else {
		tmp = readl(LPC_ENET_MAC2(pldat->net_base));
		tmp &= ~LPC_MAC2_FULL_DUPLEX;
		writel(tmp, LPC_ENET_MAC2(pldat->net_base));
		tmp = readl(LPC_ENET_COMMAND(pldat->net_base));
		tmp &= ~LPC_COMMAND_FULLDUPLEX;
		writel(tmp, LPC_ENET_COMMAND(pldat->net_base));
		writel(LPC_IPGT_LOAD(0x12), LPC_ENET_IPGT(pldat->net_base));
	}

	if (pldat->speed == SPEED_100)
		writel(LPC_SUPP_SPEED, LPC_ENET_SUPP(pldat->net_base));
	else
		writel(0, LPC_ENET_SUPP(pldat->net_base));
}

static void __lpc_eth_reset(struct netdata_local *pldat)
{
	/* Reset all MAC logic */
	writel((LPC_MAC1_RESET_TX | LPC_MAC1_RESET_MCS_TX | LPC_MAC1_RESET_RX |
		LPC_MAC1_RESET_MCS_RX | LPC_MAC1_SIMULATION_RESET |
		LPC_MAC1_SOFT_RESET), LPC_ENET_MAC1(pldat->net_base));
	writel((LPC_COMMAND_REG_RESET | LPC_COMMAND_TXRESET |
		LPC_COMMAND_RXRESET), LPC_ENET_COMMAND(pldat->net_base));
}

static int __lpc_mii_mngt_reset(struct netdata_local *pldat)
{
	/* Reset MII management hardware */
	writel(LPC_MCFG_RESET_MII_MGMT, LPC_ENET_MCFG(pldat->net_base));

	/* Setup MII clock to slowest rate with a /28 divider */
	writel(LPC_MCFG_CLOCK_SELECT(LPC_MCFG_CLOCK_HOST_DIV_28),
	       LPC_ENET_MCFG(pldat->net_base));

	return 0;
}

static inline phys_addr_t __va_to_pa(void *addr, struct netdata_local *pldat)
{
	phys_addr_t phaddr;

	phaddr = addr - pldat->dma_buff_base_v;
	phaddr += pldat->dma_buff_base_p;

	return phaddr;
}

static void lpc_eth_enable_int(void __iomem *regbase)
{
	writel((LPC_MACINT_RXDONEINTEN | LPC_MACINT_TXDONEINTEN),
	       LPC_ENET_INTENABLE(regbase));
}

static void lpc_eth_disable_int(void __iomem *regbase)
{
	writel(0, LPC_ENET_INTENABLE(regbase));
}

/* Setup TX/RX descriptors */
static void __lpc_txrx_desc_setup(struct netdata_local *pldat)
{
	u32 *ptxstat;
	void *tbuff;
	int i;
	struct txrx_desc_t *ptxrxdesc;
	struct rx_status_t *prxstat;

	tbuff = PTR_ALIGN(pldat->dma_buff_base_v, 16);

	/* Setup TX descriptors, status, and buffers */
	pldat->tx_desc_v = tbuff;
	tbuff += sizeof(struct txrx_desc_t) * ENET_TX_DESC;

	pldat->tx_stat_v = tbuff;
	tbuff += sizeof(u32) * ENET_TX_DESC;

	tbuff = PTR_ALIGN(tbuff, 16);
	pldat->tx_buff_v = tbuff;
	tbuff += ENET_MAXF_SIZE * ENET_TX_DESC;

	/* Setup RX descriptors, status, and buffers */
	pldat->rx_desc_v = tbuff;
	tbuff += sizeof(struct txrx_desc_t) * ENET_RX_DESC;

	tbuff = PTR_ALIGN(tbuff, 16);
	pldat->rx_stat_v = tbuff;
	tbuff += sizeof(struct rx_status_t) * ENET_RX_DESC;

	tbuff = PTR_ALIGN(tbuff, 16);
	pldat->rx_buff_v = tbuff;
	tbuff += ENET_MAXF_SIZE * ENET_RX_DESC;

	/* Map the TX descriptors to the TX buffers in hardware */
	for (i = 0; i < ENET_TX_DESC; i++) {
		ptxstat = &pldat->tx_stat_v[i];
		ptxrxdesc = &pldat->tx_desc_v[i];

		ptxrxdesc->packet = __va_to_pa(
				pldat->tx_buff_v + i * ENET_MAXF_SIZE, pldat);
		ptxrxdesc->control = 0;
		*ptxstat = 0;
	}

	/* Map the RX descriptors to the RX buffers in hardware */
	for (i = 0; i < ENET_RX_DESC; i++) {
		prxstat = &pldat->rx_stat_v[i];
		ptxrxdesc = &pldat->rx_desc_v[i];

		ptxrxdesc->packet = __va_to_pa(
				pldat->rx_buff_v + i * ENET_MAXF_SIZE, pldat);
		ptxrxdesc->control = RXDESC_CONTROL_INT | (ENET_MAXF_SIZE - 1);
		prxstat->statusinfo = 0;
		prxstat->statushashcrc = 0;
	}

	/* Setup base addresses in hardware to point to buffers and
	 * descriptors
	 */
	writel((ENET_TX_DESC - 1),
	       LPC_ENET_TXDESCRIPTORNUMBER(pldat->net_base));
	writel(__va_to_pa(pldat->tx_desc_v, pldat),
	       LPC_ENET_TXDESCRIPTOR(pldat->net_base));
	writel(__va_to_pa(pldat->tx_stat_v, pldat),
	       LPC_ENET_TXSTATUS(pldat->net_base));
	writel((ENET_RX_DESC - 1),
	       LPC_ENET_RXDESCRIPTORNUMBER(pldat->net_base));
	writel(__va_to_pa(pldat->rx_desc_v, pldat),
	       LPC_ENET_RXDESCRIPTOR(pldat->net_base));
	writel(__va_to_pa(pldat->rx_stat_v, pldat),
	       LPC_ENET_RXSTATUS(pldat->net_base));
}

static void __lpc_eth_init(struct netdata_local *pldat)
{
	u32 tmp;

	/* Disable controller and reset */
	tmp = readl(LPC_ENET_COMMAND(pldat->net_base));
	tmp &= ~LPC_COMMAND_RXENABLE | LPC_COMMAND_TXENABLE;
	writel(tmp, LPC_ENET_COMMAND(pldat->net_base));
	tmp = readl(LPC_ENET_MAC1(pldat->net_base));
	tmp &= ~LPC_MAC1_RECV_ENABLE;
	writel(tmp, LPC_ENET_MAC1(pldat->net_base));

	/* Initial MAC setup */
	writel(LPC_MAC1_PASS_ALL_RX_FRAMES, LPC_ENET_MAC1(pldat->net_base));
	writel((LPC_MAC2_PAD_CRC_ENABLE | LPC_MAC2_CRC_ENABLE),
	       LPC_ENET_MAC2(pldat->net_base));
	writel(ENET_MAXF_SIZE, LPC_ENET_MAXF(pldat->net_base));

	/* Collision window, gap */
	writel((LPC_CLRT_LOAD_RETRY_MAX(0xF) |
		LPC_CLRT_LOAD_COLLISION_WINDOW(0x37)),
	       LPC_ENET_CLRT(pldat->net_base));
	writel(LPC_IPGR_LOAD_PART2(0x12), LPC_ENET_IPGR(pldat->net_base));

	if (lpc_phy_interface_mode(&pldat->pdev->dev) == PHY_INTERFACE_MODE_MII)
		writel(LPC_COMMAND_PASSRUNTFRAME,
		       LPC_ENET_COMMAND(pldat->net_base));
	else {
		writel((LPC_COMMAND_PASSRUNTFRAME | LPC_COMMAND_RMII),
		       LPC_ENET_COMMAND(pldat->net_base));
		writel(LPC_SUPP_RESET_RMII, LPC_ENET_SUPP(pldat->net_base));
	}

	__lpc_params_setup(pldat);

	/* Setup TX and RX descriptors */
	__lpc_txrx_desc_setup(pldat);

	/* Setup packet filtering */
	writel((LPC_RXFLTRW_ACCEPTUBROADCAST | LPC_RXFLTRW_ACCEPTPERFECT),
	       LPC_ENET_RXFILTER_CTRL(pldat->net_base));

	/* Get the next TX buffer output index */
	pldat->num_used_tx_buffs = 0;
	pldat->last_tx_idx =
		readl(LPC_ENET_TXCONSUMEINDEX(pldat->net_base));

	/* Clear and enable interrupts */
	writel(0xFFFF, LPC_ENET_INTCLEAR(pldat->net_base));
	smp_wmb();
	lpc_eth_enable_int(pldat->net_base);

	/* Enable controller */
	tmp = readl(LPC_ENET_COMMAND(pldat->net_base));
	tmp |= LPC_COMMAND_RXENABLE | LPC_COMMAND_TXENABLE;
	writel(tmp, LPC_ENET_COMMAND(pldat->net_base));
	tmp = readl(LPC_ENET_MAC1(pldat->net_base));
	tmp |= LPC_MAC1_RECV_ENABLE;
	writel(tmp, LPC_ENET_MAC1(pldat->net_base));
}

static void __lpc_eth_shutdown(struct netdata_local *pldat)
{
	/* Reset ethernet and power down PHY */
	__lpc_eth_reset(pldat);
	writel(0, LPC_ENET_MAC1(pldat->net_base));
	writel(0, LPC_ENET_MAC2(pldat->net_base));
}

/*
 * MAC<--->PHY support functions
 */
static int lpc_mdio_read(struct mii_bus *bus, int phy_id, int phyreg)
{
	struct netdata_local *pldat = bus->priv;
	unsigned long timeout = jiffies + msecs_to_jiffies(100);
	int lps;

	writel(((phy_id << 8) | phyreg), LPC_ENET_MADR(pldat->net_base));
	writel(LPC_MCMD_READ, LPC_ENET_MCMD(pldat->net_base));

	/* Wait for unbusy status */
	while (readl(LPC_ENET_MIND(pldat->net_base)) & LPC_MIND_BUSY) {
		if (time_after(jiffies, timeout))
			return -EIO;
		cpu_relax();
	}

	lps = readl(LPC_ENET_MRDD(pldat->net_base));
	writel(0, LPC_ENET_MCMD(pldat->net_base));

	return lps;
}

static int lpc_mdio_write(struct mii_bus *bus, int phy_id, int phyreg,
			u16 phydata)
{
	struct netdata_local *pldat = bus->priv;
	unsigned long timeout = jiffies + msecs_to_jiffies(100);

	writel(((phy_id << 8) | phyreg), LPC_ENET_MADR(pldat->net_base));
	writel(phydata, LPC_ENET_MWTD(pldat->net_base));

	/* Wait for completion */
	while (readl(LPC_ENET_MIND(pldat->net_base)) & LPC_MIND_BUSY) {
		if (time_after(jiffies, timeout))
			return -EIO;
		cpu_relax();
	}

	return 0;
}

static int lpc_mdio_reset(struct mii_bus *bus)
{
	return __lpc_mii_mngt_reset((struct netdata_local *)bus->priv);
}

static void lpc_handle_link_change(struct net_device *ndev)
{
	struct netdata_local *pldat = netdev_priv(ndev);
	struct phy_device *phydev = ndev->phydev;
	unsigned long flags;

	bool status_change = false;

	spin_lock_irqsave(&pldat->lock, flags);

	if (phydev->link) {
		if ((pldat->speed != phydev->speed) ||
		    (pldat->duplex != phydev->duplex)) {
			pldat->speed = phydev->speed;
			pldat->duplex = phydev->duplex;
			status_change = true;
		}
	}

	if (phydev->link != pldat->link) {
		if (!phydev->link) {
			pldat->speed = 0;
			pldat->duplex = -1;
		}
		pldat->link = phydev->link;

		status_change = true;
	}

	spin_unlock_irqrestore(&pldat->lock, flags);

	if (status_change)
		__lpc_params_setup(pldat);
}

static int lpc_mii_probe(struct net_device *ndev)
{
	struct netdata_local *pldat = netdev_priv(ndev);
	struct phy_device *phydev;

	/* Attach to the PHY */
	if (lpc_phy_interface_mode(&pldat->pdev->dev) == PHY_INTERFACE_MODE_MII)
		netdev_info(ndev, "using MII interface\n");
	else
		netdev_info(ndev, "using RMII interface\n");

	if (pldat->phy_node)
		phydev =  of_phy_find_device(pldat->phy_node);
	else
		phydev = phy_find_first(pldat->mii_bus);
	if (!phydev) {
		netdev_err(ndev, "no PHY found\n");
		return -ENODEV;
	}

	phydev = phy_connect(ndev, phydev_name(phydev),
			     &lpc_handle_link_change,
			     lpc_phy_interface_mode(&pldat->pdev->dev));
	if (IS_ERR(phydev)) {
		netdev_err(ndev, "Could not attach to PHY\n");
		return PTR_ERR(phydev);
	}

	phy_set_max_speed(phydev, SPEED_100);

	pldat->link = 0;
	pldat->speed = 0;
	pldat->duplex = -1;

	phy_attached_info(phydev);

	return 0;
}

static int lpc_mii_init(struct netdata_local *pldat)
{
	struct device_node *node;
	int err = -ENXIO;

	pldat->mii_bus = mdiobus_alloc();
	if (!pldat->mii_bus) {
		err = -ENOMEM;
		goto err_out;
	}

	/* Setup MII mode */
	if (lpc_phy_interface_mode(&pldat->pdev->dev) == PHY_INTERFACE_MODE_MII)
		writel(LPC_COMMAND_PASSRUNTFRAME,
		       LPC_ENET_COMMAND(pldat->net_base));
	else {
		writel((LPC_COMMAND_PASSRUNTFRAME | LPC_COMMAND_RMII),
		       LPC_ENET_COMMAND(pldat->net_base));
		writel(LPC_SUPP_RESET_RMII, LPC_ENET_SUPP(pldat->net_base));
	}

	pldat->mii_bus->name = "lpc_mii_bus";
	pldat->mii_bus->read = &lpc_mdio_read;
	pldat->mii_bus->write = &lpc_mdio_write;
	pldat->mii_bus->reset = &lpc_mdio_reset;
	snprintf(pldat->mii_bus->id, MII_BUS_ID_SIZE, "%s-%x",
		 pldat->pdev->name, pldat->pdev->id);
	pldat->mii_bus->priv = pldat;
	pldat->mii_bus->parent = &pldat->pdev->dev;

	platform_set_drvdata(pldat->pdev, pldat->mii_bus);

	node = of_get_child_by_name(pldat->pdev->dev.of_node, "mdio");
	err = of_mdiobus_register(pldat->mii_bus, node);
	of_node_put(node);
	if (err)
		goto err_out_unregister_bus;

	if (lpc_mii_probe(pldat->ndev) != 0)
		goto err_out_unregister_bus;

	return 0;

err_out_unregister_bus:
	mdiobus_unregister(pldat->mii_bus);
	mdiobus_free(pldat->mii_bus);
err_out:
	return err;
}

static void __lpc_handle_xmit(struct net_device *ndev)
{
	struct netdata_local *pldat = netdev_priv(ndev);
	u32 txcidx, *ptxstat, txstat;

	txcidx = readl(LPC_ENET_TXCONSUMEINDEX(pldat->net_base));
	while (pldat->last_tx_idx != txcidx) {
		unsigned int skblen = pldat->skblen[pldat->last_tx_idx];

		/* A buffer is available, get buffer status */
		ptxstat = &pldat->tx_stat_v[pldat->last_tx_idx];
		txstat = *ptxstat;

		/* Next buffer and decrement used buffer counter */
		pldat->num_used_tx_buffs--;
		pldat->last_tx_idx++;
		if (pldat->last_tx_idx >= ENET_TX_DESC)
			pldat->last_tx_idx = 0;

		/* Update collision counter */
		ndev->stats.collisions += TXSTATUS_COLLISIONS_GET(txstat);

		/* Any errors occurred? */
		if (txstat & TXSTATUS_ERROR) {
			if (txstat & TXSTATUS_UNDERRUN) {
				/* FIFO underrun */
				ndev->stats.tx_fifo_errors++;
			}
			if (txstat & TXSTATUS_LATECOLL) {
				/* Late collision */
				ndev->stats.tx_aborted_errors++;
			}
			if (txstat & TXSTATUS_EXCESSCOLL) {
				/* Excessive collision */
				ndev->stats.tx_aborted_errors++;
			}
			if (txstat & TXSTATUS_EXCESSDEFER) {
				/* Defer limit */
				ndev->stats.tx_aborted_errors++;
			}
			ndev->stats.tx_errors++;
		} else {
			/* Update stats */
			ndev->stats.tx_packets++;
			ndev->stats.tx_bytes += skblen;
		}

		txcidx = readl(LPC_ENET_TXCONSUMEINDEX(pldat->net_base));
	}

	if (pldat->num_used_tx_buffs <= ENET_TX_DESC/2) {
		if (netif_queue_stopped(ndev))
			netif_wake_queue(ndev);
	}
}

static int __lpc_handle_recv(struct net_device *ndev, int budget)
{
	struct netdata_local *pldat = netdev_priv(ndev);
	struct sk_buff *skb;
	u32 rxconsidx, len, ethst;
	struct rx_status_t *prxstat;
	int rx_done = 0;

	/* Get the current RX buffer indexes */
	rxconsidx = readl(LPC_ENET_RXCONSUMEINDEX(pldat->net_base));
	while (rx_done < budget && rxconsidx !=
			readl(LPC_ENET_RXPRODUCEINDEX(pldat->net_base))) {
		/* Get pointer to receive status */
		prxstat = &pldat->rx_stat_v[rxconsidx];
		len = (prxstat->statusinfo & RXSTATUS_SIZE) + 1;

		/* Status error? */
		ethst = prxstat->statusinfo;
		if ((ethst & (RXSTATUS_ERROR | RXSTATUS_STATUS_ERROR)) ==
		    (RXSTATUS_ERROR | RXSTATUS_RANGE))
			ethst &= ~RXSTATUS_ERROR;

		if (ethst & RXSTATUS_ERROR) {
			int si = prxstat->statusinfo;
			/* Check statuses */
			if (si & RXSTATUS_OVERRUN) {
				/* Overrun error */
				ndev->stats.rx_fifo_errors++;
			} else if (si & RXSTATUS_CRC) {
				/* CRC error */
				ndev->stats.rx_crc_errors++;
			} else if (si & RXSTATUS_LENGTH) {
				/* Length error */
				ndev->stats.rx_length_errors++;
			} else if (si & RXSTATUS_ERROR) {
				/* Other error */
				ndev->stats.rx_length_errors++;
			}
			ndev->stats.rx_errors++;
		} else {
			/* Packet is good */
			skb = dev_alloc_skb(len);
			if (!skb) {
				ndev->stats.rx_dropped++;
			} else {
				/* Copy packet from buffer */
				skb_put_data(skb,
					     pldat->rx_buff_v + rxconsidx * ENET_MAXF_SIZE,
					     len);

				/* Pass to upper layer */
				skb->protocol = eth_type_trans(skb, ndev);
				netif_receive_skb(skb);
				ndev->stats.rx_packets++;
				ndev->stats.rx_bytes += len;
			}
		}

		/* Increment consume index */
		rxconsidx = rxconsidx + 1;
		if (rxconsidx >= ENET_RX_DESC)
			rxconsidx = 0;
		writel(rxconsidx,
		       LPC_ENET_RXCONSUMEINDEX(pldat->net_base));
		rx_done++;
	}

	return rx_done;
}

static int lpc_eth_poll(struct napi_struct *napi, int budget)
{
	struct netdata_local *pldat = container_of(napi,
			struct netdata_local, napi);
	struct net_device *ndev = pldat->ndev;
	int rx_done = 0;
	struct netdev_queue *txq = netdev_get_tx_queue(ndev, 0);

	__netif_tx_lock(txq, smp_processor_id());
	__lpc_handle_xmit(ndev);
	__netif_tx_unlock(txq);
	rx_done = __lpc_handle_recv(ndev, budget);

	if (rx_done < budget) {
		napi_complete_done(napi, rx_done);
		lpc_eth_enable_int(pldat->net_base);
	}

	return rx_done;
}

static irqreturn_t __lpc_eth_interrupt(int irq, void *dev_id)
{
	struct net_device *ndev = dev_id;
	struct netdata_local *pldat = netdev_priv(ndev);
	u32 tmp;

	spin_lock(&pldat->lock);

	tmp = readl(LPC_ENET_INTSTATUS(pldat->net_base));
	/* Clear interrupts */
	writel(tmp, LPC_ENET_INTCLEAR(pldat->net_base));

	lpc_eth_disable_int(pldat->net_base);
	if (likely(napi_schedule_prep(&pldat->napi)))
		__napi_schedule(&pldat->napi);

	spin_unlock(&pldat->lock);

	return IRQ_HANDLED;
}

static int lpc_eth_close(struct net_device *ndev)
{
	unsigned long flags;
	struct netdata_local *pldat = netdev_priv(ndev);

	if (netif_msg_ifdown(pldat))
		dev_dbg(&pldat->pdev->dev, "shutting down %s\n", ndev->name);

	napi_disable(&pldat->napi);
	netif_stop_queue(ndev);

	if (ndev->phydev)
		phy_stop(ndev->phydev);

	spin_lock_irqsave(&pldat->lock, flags);
	__lpc_eth_reset(pldat);
	netif_carrier_off(ndev);
	writel(0, LPC_ENET_MAC1(pldat->net_base));
	writel(0, LPC_ENET_MAC2(pldat->net_base));
	spin_unlock_irqrestore(&pldat->lock, flags);

	clk_disable_unprepare(pldat->clk);

	return 0;
}

static int lpc_eth_hard_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct netdata_local *pldat = netdev_priv(ndev);
	u32 len, txidx;
	u32 *ptxstat;
	struct txrx_desc_t *ptxrxdesc;

	len = skb->len;

	spin_lock_irq(&pldat->lock);

	if (pldat->num_used_tx_buffs >= (ENET_TX_DESC - 1)) {
		/* This function should never be called when there are no
		   buffers */
		netif_stop_queue(ndev);
		spin_unlock_irq(&pldat->lock);
		WARN(1, "BUG! TX request when no free TX buffers!\n");
		return NETDEV_TX_BUSY;
	}

	/* Get the next TX descriptor index */
	txidx = readl(LPC_ENET_TXPRODUCEINDEX(pldat->net_base));

	/* Setup control for the transfer */
	ptxstat = &pldat->tx_stat_v[txidx];
	*ptxstat = 0;
	ptxrxdesc = &pldat->tx_desc_v[txidx];
	ptxrxdesc->control =
		(len - 1) | TXDESC_CONTROL_LAST | TXDESC_CONTROL_INT;

	/* Copy data to the DMA buffer */
	memcpy(pldat->tx_buff_v + txidx * ENET_MAXF_SIZE, skb->data, len);

	/* Save the buffer and increment the buffer counter */
	pldat->skblen[txidx] = len;
	pldat->num_used_tx_buffs++;

	/* Start transmit */
	txidx++;
	if (txidx >= ENET_TX_DESC)
		txidx = 0;
	writel(txidx, LPC_ENET_TXPRODUCEINDEX(pldat->net_base));

	/* Stop queue if no more TX buffers */
	if (pldat->num_used_tx_buffs >= (ENET_TX_DESC - 1))
		netif_stop_queue(ndev);

	spin_unlock_irq(&pldat->lock);

	dev_kfree_skb(skb);
	return NETDEV_TX_OK;
}

static int lpc_set_mac_address(struct net_device *ndev, void *p)
{
	struct sockaddr *addr = p;
	struct netdata_local *pldat = netdev_priv(ndev);
	unsigned long flags;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;
	memcpy(ndev->dev_addr, addr->sa_data, ETH_ALEN);

	spin_lock_irqsave(&pldat->lock, flags);

	/* Set station address */
	__lpc_set_mac(pldat, ndev->dev_addr);

	spin_unlock_irqrestore(&pldat->lock, flags);

	return 0;
}

static void lpc_eth_set_multicast_list(struct net_device *ndev)
{
	struct netdata_local *pldat = netdev_priv(ndev);
	struct netdev_hw_addr_list *mcptr = &ndev->mc;
	struct netdev_hw_addr *ha;
	u32 tmp32, hash_val, hashlo, hashhi;
	unsigned long flags;

	spin_lock_irqsave(&pldat->lock, flags);

	/* Set station address */
	__lpc_set_mac(pldat, ndev->dev_addr);

	tmp32 =  LPC_RXFLTRW_ACCEPTUBROADCAST | LPC_RXFLTRW_ACCEPTPERFECT;

	if (ndev->flags & IFF_PROMISC)
		tmp32 |= LPC_RXFLTRW_ACCEPTUNICAST |
			LPC_RXFLTRW_ACCEPTUMULTICAST;
	if (ndev->flags & IFF_ALLMULTI)
		tmp32 |= LPC_RXFLTRW_ACCEPTUMULTICAST;

	if (netdev_hw_addr_list_count(mcptr))
		tmp32 |= LPC_RXFLTRW_ACCEPTUMULTICASTHASH;

	writel(tmp32, LPC_ENET_RXFILTER_CTRL(pldat->net_base));


	/* Set initial hash table */
	hashlo = 0x0;
	hashhi = 0x0;

	/* 64 bits : multicast address in hash table */
	netdev_hw_addr_list_for_each(ha, mcptr) {
		hash_val = (ether_crc(6, ha->addr) >> 23) & 0x3F;

		if (hash_val >= 32)
			hashhi |= 1 << (hash_val - 32);
		else
			hashlo |= 1 << hash_val;
	}

	writel(hashlo, LPC_ENET_HASHFILTERL(pldat->net_base));
	writel(hashhi, LPC_ENET_HASHFILTERH(pldat->net_base));

	spin_unlock_irqrestore(&pldat->lock, flags);
}

static int lpc_eth_ioctl(struct net_device *ndev, struct ifreq *req, int cmd)
{
	struct phy_device *phydev = ndev->phydev;

	if (!netif_running(ndev))
		return -EINVAL;

	if (!phydev)
		return -ENODEV;

	return phy_mii_ioctl(phydev, req, cmd);
}

static int lpc_eth_open(struct net_device *ndev)
{
	struct netdata_local *pldat = netdev_priv(ndev);
	int ret;

	if (netif_msg_ifup(pldat))
		dev_dbg(&pldat->pdev->dev, "enabling %s\n", ndev->name);

	ret = clk_prepare_enable(pldat->clk);
	if (ret)
		return ret;

	/* Suspended PHY makes LPC ethernet core block, so resume now */
	phy_resume(ndev->phydev);

	/* Reset and initialize */
	__lpc_eth_reset(pldat);
	__lpc_eth_init(pldat);

	/* schedule a link state check */
	phy_start(ndev->phydev);
	netif_start_queue(ndev);
	napi_enable(&pldat->napi);

	return 0;
}

/*
 * Ethtool ops
 */
static void lpc_eth_ethtool_getdrvinfo(struct net_device *ndev,
	struct ethtool_drvinfo *info)
{
	strlcpy(info->driver, MODNAME, sizeof(info->driver));
	strlcpy(info->version, DRV_VERSION, sizeof(info->version));
	strlcpy(info->bus_info, dev_name(ndev->dev.parent),
		sizeof(info->bus_info));
}

static u32 lpc_eth_ethtool_getmsglevel(struct net_device *ndev)
{
	struct netdata_local *pldat = netdev_priv(ndev);

	return pldat->msg_enable;
}

static void lpc_eth_ethtool_setmsglevel(struct net_device *ndev, u32 level)
{
	struct netdata_local *pldat = netdev_priv(ndev);

	pldat->msg_enable = level;
}

static const struct ethtool_ops lpc_eth_ethtool_ops = {
	.get_drvinfo	= lpc_eth_ethtool_getdrvinfo,
	.get_msglevel	= lpc_eth_ethtool_getmsglevel,
	.set_msglevel	= lpc_eth_ethtool_setmsglevel,
	.get_link	= ethtool_op_get_link,
	.get_link_ksettings = phy_ethtool_get_link_ksettings,
	.set_link_ksettings = phy_ethtool_set_link_ksettings,
};

static const struct net_device_ops lpc_netdev_ops = {
	.ndo_open		= lpc_eth_open,
	.ndo_stop		= lpc_eth_close,
	.ndo_start_xmit		= lpc_eth_hard_start_xmit,
	.ndo_set_rx_mode	= lpc_eth_set_multicast_list,
	.ndo_do_ioctl		= lpc_eth_ioctl,
	.ndo_set_mac_address	= lpc_set_mac_address,
	.ndo_validate_addr	= eth_validate_addr,
};

static int lpc_eth_drv_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct netdata_local *pldat;
	struct net_device *ndev;
	dma_addr_t dma_handle;
	struct resource *res;
	int irq, ret;

	/* Setup network interface for RMII or MII mode */
	lpc32xx_set_phy_interface_mode(lpc_phy_interface_mode(dev));

	/* Get platform resources */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	irq = platform_get_irq(pdev, 0);
	if (!res || irq < 0) {
		dev_err(dev, "error getting resources.\n");
		ret = -ENXIO;
		goto err_exit;
	}

	/* Allocate net driver data structure */
	ndev = alloc_etherdev(sizeof(struct netdata_local));
	if (!ndev) {
		dev_err(dev, "could not allocate device.\n");
		ret = -ENOMEM;
		goto err_exit;
	}

	SET_NETDEV_DEV(ndev, dev);

	pldat = netdev_priv(ndev);
	pldat->pdev = pdev;
	pldat->ndev = ndev;

	spin_lock_init(&pldat->lock);

	/* Save resources */
	ndev->irq = irq;

	/* Get clock for the device */
	pldat->clk = clk_get(dev, NULL);
	if (IS_ERR(pldat->clk)) {
		dev_err(dev, "error getting clock.\n");
		ret = PTR_ERR(pldat->clk);
		goto err_out_free_dev;
	}

	/* Enable network clock */
	ret = clk_prepare_enable(pldat->clk);
	if (ret)
		goto err_out_clk_put;

	/* Map IO space */
	pldat->net_base = ioremap(res->start, resource_size(res));
	if (!pldat->net_base) {
		dev_err(dev, "failed to map registers\n");
		ret = -ENOMEM;
		goto err_out_disable_clocks;
	}
	ret = request_irq(ndev->irq, __lpc_eth_interrupt, 0,
			  ndev->name, ndev);
	if (ret) {
		dev_err(dev, "error requesting interrupt.\n");
		goto err_out_iounmap;
	}

	/* Setup driver functions */
	ndev->netdev_ops = &lpc_netdev_ops;
	ndev->ethtool_ops = &lpc_eth_ethtool_ops;
	ndev->watchdog_timeo = msecs_to_jiffies(2500);

	/* Get size of DMA buffers/descriptors region */
	pldat->dma_buff_size = (ENET_TX_DESC + ENET_RX_DESC) * (ENET_MAXF_SIZE +
		sizeof(struct txrx_desc_t) + sizeof(struct rx_status_t));

	if (use_iram_for_net(dev)) {
		if (pldat->dma_buff_size >
		    lpc32xx_return_iram(&pldat->dma_buff_base_v, &dma_handle)) {
			pldat->dma_buff_base_v = NULL;
			pldat->dma_buff_size = 0;
			netdev_err(ndev,
				"IRAM not big enough for net buffers, using SDRAM instead.\n");
		}
	}

	if (pldat->dma_buff_base_v == NULL) {
		ret = dma_coerce_mask_and_coherent(dev, DMA_BIT_MASK(32));
		if (ret)
			goto err_out_free_irq;

		pldat->dma_buff_size = PAGE_ALIGN(pldat->dma_buff_size);

		/* Allocate a chunk of memory for the DMA ethernet buffers
		   and descriptors */
		pldat->dma_buff_base_v =
			dma_alloc_coherent(dev,
					   pldat->dma_buff_size, &dma_handle,
					   GFP_KERNEL);
		if (pldat->dma_buff_base_v == NULL) {
			ret = -ENOMEM;
			goto err_out_free_irq;
		}
	}
	pldat->dma_buff_base_p = dma_handle;

	netdev_dbg(ndev, "IO address space     :%pR\n", res);
	netdev_dbg(ndev, "IO address size      :%zd\n",
			(size_t)resource_size(res));
	netdev_dbg(ndev, "IO address (mapped)  :0x%p\n",
			pldat->net_base);
	netdev_dbg(ndev, "IRQ number           :%d\n", ndev->irq);
	netdev_dbg(ndev, "DMA buffer size      :%zd\n", pldat->dma_buff_size);
	netdev_dbg(ndev, "DMA buffer P address :%pad\n",
			&pldat->dma_buff_base_p);
	netdev_dbg(ndev, "DMA buffer V address :0x%p\n",
			pldat->dma_buff_base_v);

	pldat->phy_node = of_parse_phandle(np, "phy-handle", 0);

	/* Get MAC address from current HW setting (POR state is all zeros) */
	__lpc_get_mac(pldat, ndev->dev_addr);

	if (!is_valid_ether_addr(ndev->dev_addr)) {
		const char *macaddr = of_get_mac_address(np);
		if (!IS_ERR(macaddr))
			ether_addr_copy(ndev->dev_addr, macaddr);
	}
	if (!is_valid_ether_addr(ndev->dev_addr))
		eth_hw_addr_random(ndev);

	/* then shut everything down to save power */
	__lpc_eth_shutdown(pldat);

	/* Set default parameters */
	pldat->msg_enable = NETIF_MSG_LINK;

	/* Force an MII interface reset and clock setup */
	__lpc_mii_mngt_reset(pldat);

	/* Force default PHY interface setup in chip, this will probably be
	   changed by the PHY driver */
	pldat->link = 0;
	pldat->speed = 100;
	pldat->duplex = DUPLEX_FULL;
	__lpc_params_setup(pldat);

	netif_napi_add(ndev, &pldat->napi, lpc_eth_poll, NAPI_WEIGHT);

	ret = register_netdev(ndev);
	if (ret) {
		dev_err(dev, "Cannot register net device, aborting.\n");
		goto err_out_dma_unmap;
	}
	platform_set_drvdata(pdev, ndev);

	ret = lpc_mii_init(pldat);
	if (ret)
		goto err_out_unregister_netdev;

	netdev_info(ndev, "LPC mac at 0x%08lx irq %d\n",
	       (unsigned long)res->start, ndev->irq);

	device_init_wakeup(dev, 1);
	device_set_wakeup_enable(dev, 0);

	return 0;

err_out_unregister_netdev:
	unregister_netdev(ndev);
err_out_dma_unmap:
	if (!use_iram_for_net(dev) ||
	    pldat->dma_buff_size > lpc32xx_return_iram(NULL, NULL))
		dma_free_coherent(dev, pldat->dma_buff_size,
				  pldat->dma_buff_base_v,
				  pldat->dma_buff_base_p);
err_out_free_irq:
	free_irq(ndev->irq, ndev);
err_out_iounmap:
	iounmap(pldat->net_base);
err_out_disable_clocks:
	clk_disable_unprepare(pldat->clk);
err_out_clk_put:
	clk_put(pldat->clk);
err_out_free_dev:
	free_netdev(ndev);
err_exit:
	pr_err("%s: not found (%d).\n", MODNAME, ret);
	return ret;
}

static int lpc_eth_drv_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct netdata_local *pldat = netdev_priv(ndev);

	unregister_netdev(ndev);

	if (!use_iram_for_net(&pldat->pdev->dev) ||
	    pldat->dma_buff_size > lpc32xx_return_iram(NULL, NULL))
		dma_free_coherent(&pldat->pdev->dev, pldat->dma_buff_size,
				  pldat->dma_buff_base_v,
				  pldat->dma_buff_base_p);
	free_irq(ndev->irq, ndev);
	iounmap(pldat->net_base);
	mdiobus_unregister(pldat->mii_bus);
	mdiobus_free(pldat->mii_bus);
	clk_disable_unprepare(pldat->clk);
	clk_put(pldat->clk);
	free_netdev(ndev);

	return 0;
}

#ifdef CONFIG_PM
static int lpc_eth_drv_suspend(struct platform_device *pdev,
	pm_message_t state)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct netdata_local *pldat = netdev_priv(ndev);

	if (device_may_wakeup(&pdev->dev))
		enable_irq_wake(ndev->irq);

	if (ndev) {
		if (netif_running(ndev)) {
			netif_device_detach(ndev);
			__lpc_eth_shutdown(pldat);
			clk_disable_unprepare(pldat->clk);

			/*
			 * Reset again now clock is disable to be sure
			 * EMC_MDC is down
			 */
			__lpc_eth_reset(pldat);
		}
	}

	return 0;
}

static int lpc_eth_drv_resume(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct netdata_local *pldat;

	if (device_may_wakeup(&pdev->dev))
		disable_irq_wake(ndev->irq);

	if (ndev) {
		if (netif_running(ndev)) {
			pldat = netdev_priv(ndev);

			/* Enable interface clock */
			clk_enable(pldat->clk);

			/* Reset and initialize */
			__lpc_eth_reset(pldat);
			__lpc_eth_init(pldat);

			netif_device_attach(ndev);
		}
	}

	return 0;
}
#endif

static const struct of_device_id lpc_eth_match[] = {
	{ .compatible = "nxp,lpc-eth" },
	{ }
};
MODULE_DEVICE_TABLE(of, lpc_eth_match);

static struct platform_driver lpc_eth_driver = {
	.probe		= lpc_eth_drv_probe,
	.remove		= lpc_eth_drv_remove,
#ifdef CONFIG_PM
	.suspend	= lpc_eth_drv_suspend,
	.resume		= lpc_eth_drv_resume,
#endif
	.driver		= {
		.name	= MODNAME,
		.of_match_table = lpc_eth_match,
	},
};

module_platform_driver(lpc_eth_driver);

MODULE_AUTHOR("Kevin Wells <kevin.wells@nxp.com>");
MODULE_AUTHOR("Roland Stigge <stigge@antcom.de>");
MODULE_DESCRIPTION("LPC Ethernet Driver");
MODULE_LICENSE("GPL");

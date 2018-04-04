// SPDX-License-Identifier: GPL-2.0+

#include <linux/types.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/acpi.h>
#include <linux/of_mdio.h>
#include <linux/etherdevice.h>
#include <linux/interrupt.h>
#include <linux/io.h>

#include <net/tcp.h>
#include <net/ip6_checksum.h>

#define NETSEC_REG_SOFT_RST			0x104
#define NETSEC_REG_COM_INIT			0x120

#define NETSEC_REG_TOP_STATUS			0x200
#define NETSEC_IRQ_RX				BIT(1)
#define NETSEC_IRQ_TX				BIT(0)

#define NETSEC_REG_TOP_INTEN			0x204
#define NETSEC_REG_INTEN_SET			0x234
#define NETSEC_REG_INTEN_CLR			0x238

#define NETSEC_REG_NRM_TX_STATUS		0x400
#define NETSEC_REG_NRM_TX_INTEN			0x404
#define NETSEC_REG_NRM_TX_INTEN_SET		0x428
#define NETSEC_REG_NRM_TX_INTEN_CLR		0x42c
#define NRM_TX_ST_NTOWNR	BIT(17)
#define NRM_TX_ST_TR_ERR	BIT(16)
#define NRM_TX_ST_TXDONE	BIT(15)
#define NRM_TX_ST_TMREXP	BIT(14)

#define NETSEC_REG_NRM_RX_STATUS		0x440
#define NETSEC_REG_NRM_RX_INTEN			0x444
#define NETSEC_REG_NRM_RX_INTEN_SET		0x468
#define NETSEC_REG_NRM_RX_INTEN_CLR		0x46c
#define NRM_RX_ST_RC_ERR	BIT(16)
#define NRM_RX_ST_PKTCNT	BIT(15)
#define NRM_RX_ST_TMREXP	BIT(14)

#define NETSEC_REG_PKT_CMD_BUF			0xd0

#define NETSEC_REG_CLK_EN			0x100

#define NETSEC_REG_PKT_CTRL			0x140

#define NETSEC_REG_DMA_TMR_CTRL			0x20c
#define NETSEC_REG_F_TAIKI_MC_VER		0x22c
#define NETSEC_REG_F_TAIKI_VER			0x230
#define NETSEC_REG_DMA_HM_CTRL			0x214
#define NETSEC_REG_DMA_MH_CTRL			0x220
#define NETSEC_REG_ADDR_DIS_CORE		0x218
#define NETSEC_REG_DMAC_HM_CMD_BUF		0x210
#define NETSEC_REG_DMAC_MH_CMD_BUF		0x21c

#define NETSEC_REG_NRM_TX_PKTCNT		0x410

#define NETSEC_REG_NRM_TX_DONE_PKTCNT		0x414
#define NETSEC_REG_NRM_TX_DONE_TXINT_PKTCNT	0x418

#define NETSEC_REG_NRM_TX_TMR			0x41c

#define NETSEC_REG_NRM_RX_PKTCNT		0x454
#define NETSEC_REG_NRM_RX_RXINT_PKTCNT		0x458
#define NETSEC_REG_NRM_TX_TXINT_TMR		0x420
#define NETSEC_REG_NRM_RX_RXINT_TMR		0x460

#define NETSEC_REG_NRM_RX_TMR			0x45c

#define NETSEC_REG_NRM_TX_DESC_START_UP		0x434
#define NETSEC_REG_NRM_TX_DESC_START_LW		0x408
#define NETSEC_REG_NRM_RX_DESC_START_UP		0x474
#define NETSEC_REG_NRM_RX_DESC_START_LW		0x448

#define NETSEC_REG_NRM_TX_CONFIG		0x430
#define NETSEC_REG_NRM_RX_CONFIG		0x470

#define MAC_REG_STATUS				0x1024
#define MAC_REG_DATA				0x11c0
#define MAC_REG_CMD				0x11c4
#define MAC_REG_FLOW_TH				0x11cc
#define MAC_REG_INTF_SEL			0x11d4
#define MAC_REG_DESC_INIT			0x11fc
#define MAC_REG_DESC_SOFT_RST			0x1204
#define NETSEC_REG_MODE_TRANS_COMP_STATUS	0x500

#define GMAC_REG_MCR				0x0000
#define GMAC_REG_MFFR				0x0004
#define GMAC_REG_GAR				0x0010
#define GMAC_REG_GDR				0x0014
#define GMAC_REG_FCR				0x0018
#define GMAC_REG_BMR				0x1000
#define GMAC_REG_RDLAR				0x100c
#define GMAC_REG_TDLAR				0x1010
#define GMAC_REG_OMR				0x1018

#define MHZ(n)		((n) * 1000 * 1000)

#define NETSEC_TX_SHIFT_OWN_FIELD		31
#define NETSEC_TX_SHIFT_LD_FIELD		30
#define NETSEC_TX_SHIFT_DRID_FIELD		24
#define NETSEC_TX_SHIFT_PT_FIELD		21
#define NETSEC_TX_SHIFT_TDRID_FIELD		16
#define NETSEC_TX_SHIFT_CC_FIELD		15
#define NETSEC_TX_SHIFT_FS_FIELD		9
#define NETSEC_TX_LAST				8
#define NETSEC_TX_SHIFT_CO			7
#define NETSEC_TX_SHIFT_SO			6
#define NETSEC_TX_SHIFT_TRS_FIELD		4

#define NETSEC_RX_PKT_OWN_FIELD			31
#define NETSEC_RX_PKT_LD_FIELD			30
#define NETSEC_RX_PKT_SDRID_FIELD		24
#define NETSEC_RX_PKT_FR_FIELD			23
#define NETSEC_RX_PKT_ER_FIELD			21
#define NETSEC_RX_PKT_ERR_FIELD			16
#define NETSEC_RX_PKT_TDRID_FIELD		12
#define NETSEC_RX_PKT_FS_FIELD			9
#define NETSEC_RX_PKT_LS_FIELD			8
#define NETSEC_RX_PKT_CO_FIELD			6

#define NETSEC_RX_PKT_ERR_MASK			3

#define NETSEC_MAX_TX_PKT_LEN			1518
#define NETSEC_MAX_TX_JUMBO_PKT_LEN		9018

#define NETSEC_RING_GMAC			15
#define NETSEC_RING_MAX				2

#define NETSEC_TCP_SEG_LEN_MAX			1460
#define NETSEC_TCP_JUMBO_SEG_LEN_MAX		8960

#define NETSEC_RX_CKSUM_NOTAVAIL		0
#define NETSEC_RX_CKSUM_OK			1
#define NETSEC_RX_CKSUM_NG			2

#define NETSEC_TOP_IRQ_REG_CODE_LOAD_END	BIT(20)
#define NETSEC_IRQ_TRANSITION_COMPLETE		BIT(4)

#define NETSEC_MODE_TRANS_COMP_IRQ_N2T		BIT(20)
#define NETSEC_MODE_TRANS_COMP_IRQ_T2N		BIT(19)

#define NETSEC_INT_PKTCNT_MAX			2047

#define NETSEC_FLOW_START_TH_MAX		95
#define NETSEC_FLOW_STOP_TH_MAX			95
#define NETSEC_FLOW_PAUSE_TIME_MIN		5

#define NETSEC_CLK_EN_REG_DOM_ALL		0x3f

#define NETSEC_PKT_CTRL_REG_MODE_NRM		BIT(28)
#define NETSEC_PKT_CTRL_REG_EN_JUMBO		BIT(27)
#define NETSEC_PKT_CTRL_REG_LOG_CHKSUM_ER	BIT(3)
#define NETSEC_PKT_CTRL_REG_LOG_HD_INCOMPLETE	BIT(2)
#define NETSEC_PKT_CTRL_REG_LOG_HD_ER		BIT(1)
#define NETSEC_PKT_CTRL_REG_DRP_NO_MATCH	BIT(0)

#define NETSEC_CLK_EN_REG_DOM_G			BIT(5)
#define NETSEC_CLK_EN_REG_DOM_C			BIT(1)
#define NETSEC_CLK_EN_REG_DOM_D			BIT(0)

#define NETSEC_COM_INIT_REG_DB			BIT(2)
#define NETSEC_COM_INIT_REG_CLS			BIT(1)
#define NETSEC_COM_INIT_REG_ALL			(NETSEC_COM_INIT_REG_CLS | \
						 NETSEC_COM_INIT_REG_DB)

#define NETSEC_SOFT_RST_REG_RESET		0
#define NETSEC_SOFT_RST_REG_RUN			BIT(31)

#define NETSEC_DMA_CTRL_REG_STOP		1
#define MH_CTRL__MODE_TRANS			BIT(20)

#define NETSEC_GMAC_CMD_ST_READ			0
#define NETSEC_GMAC_CMD_ST_WRITE		BIT(28)
#define NETSEC_GMAC_CMD_ST_BUSY			BIT(31)

#define NETSEC_GMAC_BMR_REG_COMMON		0x00412080
#define NETSEC_GMAC_BMR_REG_RESET		0x00020181
#define NETSEC_GMAC_BMR_REG_SWR			0x00000001

#define NETSEC_GMAC_OMR_REG_ST			BIT(13)
#define NETSEC_GMAC_OMR_REG_SR			BIT(1)

#define NETSEC_GMAC_MCR_REG_IBN			BIT(30)
#define NETSEC_GMAC_MCR_REG_CST			BIT(25)
#define NETSEC_GMAC_MCR_REG_JE			BIT(20)
#define NETSEC_MCR_PS				BIT(15)
#define NETSEC_GMAC_MCR_REG_FES			BIT(14)
#define NETSEC_GMAC_MCR_REG_FULL_DUPLEX_COMMON	0x0000280c
#define NETSEC_GMAC_MCR_REG_HALF_DUPLEX_COMMON	0x0001a00c

#define NETSEC_FCR_RFE				BIT(2)
#define NETSEC_FCR_TFE				BIT(1)

#define NETSEC_GMAC_GAR_REG_GW			BIT(1)
#define NETSEC_GMAC_GAR_REG_GB			BIT(0)

#define NETSEC_GMAC_GAR_REG_SHIFT_PA		11
#define NETSEC_GMAC_GAR_REG_SHIFT_GR		6
#define GMAC_REG_SHIFT_CR_GAR			2

#define NETSEC_GMAC_GAR_REG_CR_25_35_MHZ	2
#define NETSEC_GMAC_GAR_REG_CR_35_60_MHZ	3
#define NETSEC_GMAC_GAR_REG_CR_60_100_MHZ	0
#define NETSEC_GMAC_GAR_REG_CR_100_150_MHZ	1
#define NETSEC_GMAC_GAR_REG_CR_150_250_MHZ	4
#define NETSEC_GMAC_GAR_REG_CR_250_300_MHZ	5

#define NETSEC_GMAC_RDLAR_REG_COMMON		0x18000
#define NETSEC_GMAC_TDLAR_REG_COMMON		0x1c000

#define NETSEC_REG_NETSEC_VER_F_TAIKI		0x50000

#define NETSEC_REG_DESC_RING_CONFIG_CFG_UP	BIT(31)
#define NETSEC_REG_DESC_RING_CONFIG_CH_RST	BIT(30)
#define NETSEC_REG_DESC_TMR_MODE		4
#define NETSEC_REG_DESC_ENDIAN			0

#define NETSEC_MAC_DESC_SOFT_RST_SOFT_RST	1
#define NETSEC_MAC_DESC_INIT_REG_INIT		1

#define NETSEC_EEPROM_MAC_ADDRESS		0x00
#define NETSEC_EEPROM_HM_ME_ADDRESS_H		0x08
#define NETSEC_EEPROM_HM_ME_ADDRESS_L		0x0C
#define NETSEC_EEPROM_HM_ME_SIZE		0x10
#define NETSEC_EEPROM_MH_ME_ADDRESS_H		0x14
#define NETSEC_EEPROM_MH_ME_ADDRESS_L		0x18
#define NETSEC_EEPROM_MH_ME_SIZE		0x1C
#define NETSEC_EEPROM_PKT_ME_ADDRESS		0x20
#define NETSEC_EEPROM_PKT_ME_SIZE		0x24

#define DESC_NUM	128
#define NAPI_BUDGET	(DESC_NUM / 2)

#define DESC_SZ	sizeof(struct netsec_de)

#define NETSEC_F_NETSEC_VER_MAJOR_NUM(x)	((x) & 0xffff0000)

enum ring_id {
	NETSEC_RING_TX = 0,
	NETSEC_RING_RX
};

struct netsec_desc {
	struct sk_buff *skb;
	dma_addr_t dma_addr;
	void *addr;
	u16 len;
};

struct netsec_desc_ring {
	dma_addr_t desc_dma;
	struct netsec_desc *desc;
	void *vaddr;
	u16 pkt_cnt;
	u16 head, tail;
};

struct netsec_priv {
	struct netsec_desc_ring desc_ring[NETSEC_RING_MAX];
	struct ethtool_coalesce et_coalesce;
	spinlock_t reglock; /* protect reg access */
	struct napi_struct napi;
	phy_interface_t phy_interface;
	struct net_device *ndev;
	struct device_node *phy_np;
	struct phy_device *phydev;
	struct mii_bus *mii_bus;
	void __iomem *ioaddr;
	void __iomem *eeprom_base;
	struct device *dev;
	struct clk *clk;
	u32 msg_enable;
	u32 freq;
	bool rx_cksum_offload_flag;
};

struct netsec_de { /* Netsec Descriptor layout */
	u32 attr;
	u32 data_buf_addr_up;
	u32 data_buf_addr_lw;
	u32 buf_len_info;
};

struct netsec_tx_pkt_ctrl {
	u16 tcp_seg_len;
	bool tcp_seg_offload_flag;
	bool cksum_offload_flag;
};

struct netsec_rx_pkt_info {
	int rx_cksum_result;
	int err_code;
	bool err_flag;
};

static void netsec_write(struct netsec_priv *priv, u32 reg_addr, u32 val)
{
	writel(val, priv->ioaddr + reg_addr);
}

static u32 netsec_read(struct netsec_priv *priv, u32 reg_addr)
{
	return readl(priv->ioaddr + reg_addr);
}

/************* MDIO BUS OPS FOLLOW *************/

#define TIMEOUT_SPINS_MAC		1000
#define TIMEOUT_SECONDARY_MS_MAC	100

static u32 netsec_clk_type(u32 freq)
{
	if (freq < MHZ(35))
		return NETSEC_GMAC_GAR_REG_CR_25_35_MHZ;
	if (freq < MHZ(60))
		return NETSEC_GMAC_GAR_REG_CR_35_60_MHZ;
	if (freq < MHZ(100))
		return NETSEC_GMAC_GAR_REG_CR_60_100_MHZ;
	if (freq < MHZ(150))
		return NETSEC_GMAC_GAR_REG_CR_100_150_MHZ;
	if (freq < MHZ(250))
		return NETSEC_GMAC_GAR_REG_CR_150_250_MHZ;

	return NETSEC_GMAC_GAR_REG_CR_250_300_MHZ;
}

static int netsec_wait_while_busy(struct netsec_priv *priv, u32 addr, u32 mask)
{
	u32 timeout = TIMEOUT_SPINS_MAC;

	while (--timeout && netsec_read(priv, addr) & mask)
		cpu_relax();
	if (timeout)
		return 0;

	timeout = TIMEOUT_SECONDARY_MS_MAC;
	while (--timeout && netsec_read(priv, addr) & mask)
		usleep_range(1000, 2000);

	if (timeout)
		return 0;

	netdev_WARN(priv->ndev, "%s: timeout\n", __func__);

	return -ETIMEDOUT;
}

static int netsec_mac_write(struct netsec_priv *priv, u32 addr, u32 value)
{
	netsec_write(priv, MAC_REG_DATA, value);
	netsec_write(priv, MAC_REG_CMD, addr | NETSEC_GMAC_CMD_ST_WRITE);
	return netsec_wait_while_busy(priv,
				      MAC_REG_CMD, NETSEC_GMAC_CMD_ST_BUSY);
}

static int netsec_mac_read(struct netsec_priv *priv, u32 addr, u32 *read)
{
	int ret;

	netsec_write(priv, MAC_REG_CMD, addr | NETSEC_GMAC_CMD_ST_READ);
	ret = netsec_wait_while_busy(priv,
				     MAC_REG_CMD, NETSEC_GMAC_CMD_ST_BUSY);
	if (ret)
		return ret;

	*read = netsec_read(priv, MAC_REG_DATA);

	return 0;
}

static int netsec_mac_wait_while_busy(struct netsec_priv *priv,
				      u32 addr, u32 mask)
{
	u32 timeout = TIMEOUT_SPINS_MAC;
	int ret, data;

	do {
		ret = netsec_mac_read(priv, addr, &data);
		if (ret)
			break;
		cpu_relax();
	} while (--timeout && (data & mask));

	if (timeout)
		return 0;

	timeout = TIMEOUT_SECONDARY_MS_MAC;
	do {
		usleep_range(1000, 2000);

		ret = netsec_mac_read(priv, addr, &data);
		if (ret)
			break;
		cpu_relax();
	} while (--timeout && (data & mask));

	if (timeout && !ret)
		return 0;

	netdev_WARN(priv->ndev, "%s: timeout\n", __func__);

	return -ETIMEDOUT;
}

static int netsec_mac_update_to_phy_state(struct netsec_priv *priv)
{
	struct phy_device *phydev = priv->ndev->phydev;
	u32 value = 0;

	value = phydev->duplex ? NETSEC_GMAC_MCR_REG_FULL_DUPLEX_COMMON :
				 NETSEC_GMAC_MCR_REG_HALF_DUPLEX_COMMON;

	if (phydev->speed != SPEED_1000)
		value |= NETSEC_MCR_PS;

	if (priv->phy_interface != PHY_INTERFACE_MODE_GMII &&
	    phydev->speed == SPEED_100)
		value |= NETSEC_GMAC_MCR_REG_FES;

	value |= NETSEC_GMAC_MCR_REG_CST | NETSEC_GMAC_MCR_REG_JE;

	if (phy_interface_mode_is_rgmii(priv->phy_interface))
		value |= NETSEC_GMAC_MCR_REG_IBN;

	if (netsec_mac_write(priv, GMAC_REG_MCR, value))
		return -ETIMEDOUT;

	return 0;
}

static int netsec_phy_write(struct mii_bus *bus,
			    int phy_addr, int reg, u16 val)
{
	struct netsec_priv *priv = bus->priv;

	if (netsec_mac_write(priv, GMAC_REG_GDR, val))
		return -ETIMEDOUT;
	if (netsec_mac_write(priv, GMAC_REG_GAR,
			     phy_addr << NETSEC_GMAC_GAR_REG_SHIFT_PA |
			     reg << NETSEC_GMAC_GAR_REG_SHIFT_GR |
			     NETSEC_GMAC_GAR_REG_GW | NETSEC_GMAC_GAR_REG_GB |
			     (netsec_clk_type(priv->freq) <<
			      GMAC_REG_SHIFT_CR_GAR)))
		return -ETIMEDOUT;

	return netsec_mac_wait_while_busy(priv, GMAC_REG_GAR,
					  NETSEC_GMAC_GAR_REG_GB);
}

static int netsec_phy_read(struct mii_bus *bus, int phy_addr, int reg_addr)
{
	struct netsec_priv *priv = bus->priv;
	u32 data;
	int ret;

	if (netsec_mac_write(priv, GMAC_REG_GAR, NETSEC_GMAC_GAR_REG_GB |
			     phy_addr << NETSEC_GMAC_GAR_REG_SHIFT_PA |
			     reg_addr << NETSEC_GMAC_GAR_REG_SHIFT_GR |
			     (netsec_clk_type(priv->freq) <<
			      GMAC_REG_SHIFT_CR_GAR)))
		return -ETIMEDOUT;

	ret = netsec_mac_wait_while_busy(priv, GMAC_REG_GAR,
					 NETSEC_GMAC_GAR_REG_GB);
	if (ret)
		return ret;

	ret = netsec_mac_read(priv, GMAC_REG_GDR, &data);
	if (ret)
		return ret;

	return data;
}

/************* ETHTOOL_OPS FOLLOW *************/

static void netsec_et_get_drvinfo(struct net_device *net_device,
				  struct ethtool_drvinfo *info)
{
	strlcpy(info->driver, "netsec", sizeof(info->driver));
	strlcpy(info->bus_info, dev_name(net_device->dev.parent),
		sizeof(info->bus_info));
}

static int netsec_et_get_coalesce(struct net_device *net_device,
				  struct ethtool_coalesce *et_coalesce)
{
	struct netsec_priv *priv = netdev_priv(net_device);

	*et_coalesce = priv->et_coalesce;

	return 0;
}

static int netsec_et_set_coalesce(struct net_device *net_device,
				  struct ethtool_coalesce *et_coalesce)
{
	struct netsec_priv *priv = netdev_priv(net_device);

	priv->et_coalesce = *et_coalesce;

	if (priv->et_coalesce.tx_coalesce_usecs < 50)
		priv->et_coalesce.tx_coalesce_usecs = 50;
	if (priv->et_coalesce.tx_max_coalesced_frames < 1)
		priv->et_coalesce.tx_max_coalesced_frames = 1;

	netsec_write(priv, NETSEC_REG_NRM_TX_DONE_TXINT_PKTCNT,
		     priv->et_coalesce.tx_max_coalesced_frames);
	netsec_write(priv, NETSEC_REG_NRM_TX_TXINT_TMR,
		     priv->et_coalesce.tx_coalesce_usecs);
	netsec_write(priv, NETSEC_REG_NRM_TX_INTEN_SET, NRM_TX_ST_TXDONE);
	netsec_write(priv, NETSEC_REG_NRM_TX_INTEN_SET, NRM_TX_ST_TMREXP);

	if (priv->et_coalesce.rx_coalesce_usecs < 50)
		priv->et_coalesce.rx_coalesce_usecs = 50;
	if (priv->et_coalesce.rx_max_coalesced_frames < 1)
		priv->et_coalesce.rx_max_coalesced_frames = 1;

	netsec_write(priv, NETSEC_REG_NRM_RX_RXINT_PKTCNT,
		     priv->et_coalesce.rx_max_coalesced_frames);
	netsec_write(priv, NETSEC_REG_NRM_RX_RXINT_TMR,
		     priv->et_coalesce.rx_coalesce_usecs);
	netsec_write(priv, NETSEC_REG_NRM_RX_INTEN_SET, NRM_RX_ST_PKTCNT);
	netsec_write(priv, NETSEC_REG_NRM_RX_INTEN_SET, NRM_RX_ST_TMREXP);

	return 0;
}

static u32 netsec_et_get_msglevel(struct net_device *dev)
{
	struct netsec_priv *priv = netdev_priv(dev);

	return priv->msg_enable;
}

static void netsec_et_set_msglevel(struct net_device *dev, u32 datum)
{
	struct netsec_priv *priv = netdev_priv(dev);

	priv->msg_enable = datum;
}

static const struct ethtool_ops netsec_ethtool_ops = {
	.get_drvinfo		= netsec_et_get_drvinfo,
	.get_link_ksettings	= phy_ethtool_get_link_ksettings,
	.set_link_ksettings	= phy_ethtool_set_link_ksettings,
	.get_link		= ethtool_op_get_link,
	.get_coalesce		= netsec_et_get_coalesce,
	.set_coalesce		= netsec_et_set_coalesce,
	.get_msglevel		= netsec_et_get_msglevel,
	.set_msglevel		= netsec_et_set_msglevel,
};

/************* NETDEV_OPS FOLLOW *************/

static struct sk_buff *netsec_alloc_skb(struct netsec_priv *priv,
					struct netsec_desc *desc)
{
	struct sk_buff *skb;

	if (device_get_dma_attr(priv->dev) == DEV_DMA_COHERENT) {
		skb = netdev_alloc_skb_ip_align(priv->ndev, desc->len);
	} else {
		desc->len = L1_CACHE_ALIGN(desc->len);
		skb = netdev_alloc_skb(priv->ndev, desc->len);
	}
	if (!skb)
		return NULL;

	desc->addr = skb->data;
	desc->dma_addr = dma_map_single(priv->dev, desc->addr, desc->len,
					DMA_FROM_DEVICE);
	if (dma_mapping_error(priv->dev, desc->dma_addr)) {
		dev_kfree_skb_any(skb);
		return NULL;
	}
	return skb;
}

static void netsec_set_rx_de(struct netsec_priv *priv,
			     struct netsec_desc_ring *dring, u16 idx,
			     const struct netsec_desc *desc,
			     struct sk_buff *skb)
{
	struct netsec_de *de = dring->vaddr + DESC_SZ * idx;
	u32 attr = (1 << NETSEC_RX_PKT_OWN_FIELD) |
		   (1 << NETSEC_RX_PKT_FS_FIELD) |
		   (1 << NETSEC_RX_PKT_LS_FIELD);

	if (idx == DESC_NUM - 1)
		attr |= (1 << NETSEC_RX_PKT_LD_FIELD);

	de->data_buf_addr_up = upper_32_bits(desc->dma_addr);
	de->data_buf_addr_lw = lower_32_bits(desc->dma_addr);
	de->buf_len_info = desc->len;
	de->attr = attr;
	dma_wmb();

	dring->desc[idx].dma_addr = desc->dma_addr;
	dring->desc[idx].addr = desc->addr;
	dring->desc[idx].len = desc->len;
	dring->desc[idx].skb = skb;
}

static struct sk_buff *netsec_get_rx_de(struct netsec_priv *priv,
					struct netsec_desc_ring *dring,
					u16 idx,
					struct netsec_rx_pkt_info *rxpi,
					struct netsec_desc *desc, u16 *len)
{
	struct netsec_de de = {};

	memcpy(&de, dring->vaddr + DESC_SZ * idx, DESC_SZ);

	*len = de.buf_len_info >> 16;

	rxpi->err_flag = (de.attr >> NETSEC_RX_PKT_ER_FIELD) & 1;
	rxpi->rx_cksum_result = (de.attr >> NETSEC_RX_PKT_CO_FIELD) & 3;
	rxpi->err_code = (de.attr >> NETSEC_RX_PKT_ERR_FIELD) &
							NETSEC_RX_PKT_ERR_MASK;
	*desc = dring->desc[idx];
	return desc->skb;
}

static struct sk_buff *netsec_get_rx_pkt_data(struct netsec_priv *priv,
					      struct netsec_rx_pkt_info *rxpi,
					      struct netsec_desc *desc,
					      u16 *len)
{
	struct netsec_desc_ring *dring = &priv->desc_ring[NETSEC_RING_RX];
	struct sk_buff *tmp_skb, *skb = NULL;
	struct netsec_desc td;
	int tail;

	*rxpi = (struct netsec_rx_pkt_info){};

	td.len = priv->ndev->mtu + 22;

	tmp_skb = netsec_alloc_skb(priv, &td);

	dma_rmb();

	tail = dring->tail;

	if (!tmp_skb) {
		netsec_set_rx_de(priv, dring, tail, &dring->desc[tail],
				 dring->desc[tail].skb);
	} else {
		skb = netsec_get_rx_de(priv, dring, tail, rxpi, desc, len);
		netsec_set_rx_de(priv, dring, tail, &td, tmp_skb);
	}

	/* move tail ahead */
	dring->tail = (dring->tail + 1) % DESC_NUM;

	dring->pkt_cnt--;

	return skb;
}

static int netsec_clean_tx_dring(struct netsec_priv *priv, int budget)
{
	struct netsec_desc_ring *dring = &priv->desc_ring[NETSEC_RING_TX];
	unsigned int pkts, bytes;

	dring->pkt_cnt += netsec_read(priv, NETSEC_REG_NRM_TX_DONE_PKTCNT);

	if (dring->pkt_cnt < budget)
		budget = dring->pkt_cnt;

	pkts = 0;
	bytes = 0;

	while (pkts < budget) {
		struct netsec_desc *desc;
		struct netsec_de *entry;
		int tail, eop;

		tail = dring->tail;

		/* move tail ahead */
		dring->tail = (tail + 1) % DESC_NUM;

		desc = &dring->desc[tail];
		entry = dring->vaddr + DESC_SZ * tail;

		eop = (entry->attr >> NETSEC_TX_LAST) & 1;

		dma_unmap_single(priv->dev, desc->dma_addr, desc->len,
				 DMA_TO_DEVICE);
		if (eop) {
			pkts++;
			bytes += desc->skb->len;
			dev_kfree_skb(desc->skb);
		}
		*desc = (struct netsec_desc){};
	}
	dring->pkt_cnt -= budget;

	priv->ndev->stats.tx_packets += budget;
	priv->ndev->stats.tx_bytes += bytes;

	netdev_completed_queue(priv->ndev, budget, bytes);

	return budget;
}

static int netsec_process_tx(struct netsec_priv *priv, int budget)
{
	struct net_device *ndev = priv->ndev;
	int new, done = 0;

	do {
		new = netsec_clean_tx_dring(priv, budget);
		done += new;
		budget -= new;
	} while (new);

	if (done && netif_queue_stopped(ndev))
		netif_wake_queue(ndev);

	return done;
}

static int netsec_process_rx(struct netsec_priv *priv, int budget)
{
	struct netsec_desc_ring *dring = &priv->desc_ring[NETSEC_RING_RX];
	struct net_device *ndev = priv->ndev;
	struct netsec_rx_pkt_info rx_info;
	int done = 0, rx_num = 0;
	struct netsec_desc desc;
	struct sk_buff *skb;
	u16 len;

	while (done < budget) {
		if (!rx_num) {
			rx_num = netsec_read(priv, NETSEC_REG_NRM_RX_PKTCNT);
			dring->pkt_cnt += rx_num;

			/* move head 'rx_num' */
			dring->head = (dring->head + rx_num) % DESC_NUM;

			rx_num = dring->pkt_cnt;
			if (!rx_num)
				break;
		}
		done++;
		rx_num--;
		skb = netsec_get_rx_pkt_data(priv, &rx_info, &desc, &len);
		if (unlikely(!skb) || rx_info.err_flag) {
			netif_err(priv, drv, priv->ndev,
				  "%s: rx fail err(%d)\n",
				  __func__, rx_info.err_code);
			ndev->stats.rx_dropped++;
			continue;
		}

		dma_unmap_single(priv->dev, desc.dma_addr, desc.len,
				 DMA_FROM_DEVICE);
		skb_put(skb, len);
		skb->protocol = eth_type_trans(skb, priv->ndev);

		if (priv->rx_cksum_offload_flag &&
		    rx_info.rx_cksum_result == NETSEC_RX_CKSUM_OK)
			skb->ip_summed = CHECKSUM_UNNECESSARY;

		if (napi_gro_receive(&priv->napi, skb) != GRO_DROP) {
			ndev->stats.rx_packets++;
			ndev->stats.rx_bytes += len;
		}
	}

	return done;
}

static int netsec_napi_poll(struct napi_struct *napi, int budget)
{
	struct netsec_priv *priv;
	struct net_device *ndev;
	int tx, rx, done, todo;

	priv = container_of(napi, struct netsec_priv, napi);
	ndev = priv->ndev;

	todo = budget;
	do {
		if (!todo)
			break;

		tx = netsec_process_tx(priv, todo);
		todo -= tx;

		if (!todo)
			break;

		rx = netsec_process_rx(priv, todo);
		todo -= rx;
	} while (rx || tx);

	done = budget - todo;

	if (done < budget && napi_complete_done(napi, done)) {
		unsigned long flags;

		spin_lock_irqsave(&priv->reglock, flags);
		netsec_write(priv, NETSEC_REG_INTEN_SET,
			     NETSEC_IRQ_RX | NETSEC_IRQ_TX);
		spin_unlock_irqrestore(&priv->reglock, flags);
	}

	return done;
}

static void netsec_set_tx_de(struct netsec_priv *priv,
			     struct netsec_desc_ring *dring,
			     const struct netsec_tx_pkt_ctrl *tx_ctrl,
			     const struct netsec_desc *desc,
			     struct sk_buff *skb)
{
	int idx = dring->head;
	struct netsec_de *de;
	u32 attr;

	de = dring->vaddr + (DESC_SZ * idx);

	attr = (1 << NETSEC_TX_SHIFT_OWN_FIELD) |
	       (1 << NETSEC_TX_SHIFT_PT_FIELD) |
	       (NETSEC_RING_GMAC << NETSEC_TX_SHIFT_TDRID_FIELD) |
	       (1 << NETSEC_TX_SHIFT_FS_FIELD) |
	       (1 << NETSEC_TX_LAST) |
	       (tx_ctrl->cksum_offload_flag << NETSEC_TX_SHIFT_CO) |
	       (tx_ctrl->tcp_seg_offload_flag << NETSEC_TX_SHIFT_SO) |
	       (1 << NETSEC_TX_SHIFT_TRS_FIELD);
	if (idx == DESC_NUM - 1)
		attr |= (1 << NETSEC_TX_SHIFT_LD_FIELD);

	de->data_buf_addr_up = upper_32_bits(desc->dma_addr);
	de->data_buf_addr_lw = lower_32_bits(desc->dma_addr);
	de->buf_len_info = (tx_ctrl->tcp_seg_len << 16) | desc->len;
	de->attr = attr;
	dma_wmb();

	dring->desc[idx] = *desc;
	dring->desc[idx].skb = skb;

	/* move head ahead */
	dring->head = (dring->head + 1) % DESC_NUM;
}

static netdev_tx_t netsec_netdev_start_xmit(struct sk_buff *skb,
					    struct net_device *ndev)
{
	struct netsec_priv *priv = netdev_priv(ndev);
	struct netsec_desc_ring *dring = &priv->desc_ring[NETSEC_RING_TX];
	struct netsec_tx_pkt_ctrl tx_ctrl = {};
	struct netsec_desc tx_desc;
	u16 tso_seg_len = 0;
	int filled;

	/* differentiate between full/emtpy ring */
	if (dring->head >= dring->tail)
		filled = dring->head - dring->tail;
	else
		filled = dring->head + DESC_NUM - dring->tail;

	if (DESC_NUM - filled < 2) { /* if less than 2 available */
		netif_err(priv, drv, priv->ndev, "%s: TxQFull!\n", __func__);
		netif_stop_queue(priv->ndev);
		dma_wmb();
		return NETDEV_TX_BUSY;
	}

	if (skb->ip_summed == CHECKSUM_PARTIAL)
		tx_ctrl.cksum_offload_flag = true;

	if (skb_is_gso(skb))
		tso_seg_len = skb_shinfo(skb)->gso_size;

	if (tso_seg_len > 0) {
		if (skb->protocol == htons(ETH_P_IP)) {
			ip_hdr(skb)->tot_len = 0;
			tcp_hdr(skb)->check =
				~tcp_v4_check(0, ip_hdr(skb)->saddr,
					      ip_hdr(skb)->daddr, 0);
		} else {
			ipv6_hdr(skb)->payload_len = 0;
			tcp_hdr(skb)->check =
				~csum_ipv6_magic(&ipv6_hdr(skb)->saddr,
						 &ipv6_hdr(skb)->daddr,
						 0, IPPROTO_TCP, 0);
		}

		tx_ctrl.tcp_seg_offload_flag = true;
		tx_ctrl.tcp_seg_len = tso_seg_len;
	}

	tx_desc.dma_addr = dma_map_single(priv->dev, skb->data,
					  skb_headlen(skb), DMA_TO_DEVICE);
	if (dma_mapping_error(priv->dev, tx_desc.dma_addr)) {
		netif_err(priv, drv, priv->ndev,
			  "%s: DMA mapping failed\n", __func__);
		ndev->stats.tx_dropped++;
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}
	tx_desc.addr = skb->data;
	tx_desc.len = skb_headlen(skb);

	skb_tx_timestamp(skb);
	netdev_sent_queue(priv->ndev, skb->len);

	netsec_set_tx_de(priv, dring, &tx_ctrl, &tx_desc, skb);
	netsec_write(priv, NETSEC_REG_NRM_TX_PKTCNT, 1); /* submit another tx */

	return NETDEV_TX_OK;
}

static void netsec_uninit_pkt_dring(struct netsec_priv *priv, int id)
{
	struct netsec_desc_ring *dring = &priv->desc_ring[id];
	struct netsec_desc *desc;
	u16 idx;

	if (!dring->vaddr || !dring->desc)
		return;

	for (idx = 0; idx < DESC_NUM; idx++) {
		desc = &dring->desc[idx];
		if (!desc->addr)
			continue;

		dma_unmap_single(priv->dev, desc->dma_addr, desc->len,
				 id == NETSEC_RING_RX ? DMA_FROM_DEVICE :
							      DMA_TO_DEVICE);
		dev_kfree_skb(desc->skb);
	}

	memset(dring->desc, 0, sizeof(struct netsec_desc) * DESC_NUM);
	memset(dring->vaddr, 0, DESC_SZ * DESC_NUM);

	dring->head = 0;
	dring->tail = 0;
	dring->pkt_cnt = 0;
}

static void netsec_free_dring(struct netsec_priv *priv, int id)
{
	struct netsec_desc_ring *dring = &priv->desc_ring[id];

	if (dring->vaddr) {
		dma_free_coherent(priv->dev, DESC_SZ * DESC_NUM,
				  dring->vaddr, dring->desc_dma);
		dring->vaddr = NULL;
	}

	kfree(dring->desc);
	dring->desc = NULL;
}

static int netsec_alloc_dring(struct netsec_priv *priv, enum ring_id id)
{
	struct netsec_desc_ring *dring = &priv->desc_ring[id];
	int ret = 0;

	dring->vaddr = dma_zalloc_coherent(priv->dev, DESC_SZ * DESC_NUM,
					   &dring->desc_dma, GFP_KERNEL);
	if (!dring->vaddr) {
		ret = -ENOMEM;
		goto err;
	}

	dring->desc = kzalloc(DESC_NUM * sizeof(*dring->desc), GFP_KERNEL);
	if (!dring->desc) {
		ret = -ENOMEM;
		goto err;
	}

	return 0;
err:
	netsec_free_dring(priv, id);

	return ret;
}

static int netsec_setup_rx_dring(struct netsec_priv *priv)
{
	struct netsec_desc_ring *dring = &priv->desc_ring[NETSEC_RING_RX];
	struct netsec_desc desc;
	struct sk_buff *skb;
	int n;

	desc.len = priv->ndev->mtu + 22;

	for (n = 0; n < DESC_NUM; n++) {
		skb = netsec_alloc_skb(priv, &desc);
		if (!skb) {
			netsec_uninit_pkt_dring(priv, NETSEC_RING_RX);
			return -ENOMEM;
		}
		netsec_set_rx_de(priv, dring, n, &desc, skb);
	}

	return 0;
}

static int netsec_netdev_load_ucode_region(struct netsec_priv *priv, u32 reg,
					   u32 addr_h, u32 addr_l, u32 size)
{
	u64 base = (u64)addr_h << 32 | addr_l;
	void __iomem *ucode;
	u32 i;

	ucode = ioremap(base, size * sizeof(u32));
	if (!ucode)
		return -ENOMEM;

	for (i = 0; i < size; i++)
		netsec_write(priv, reg, readl(ucode + i * 4));

	iounmap(ucode);
	return 0;
}

static int netsec_netdev_load_microcode(struct netsec_priv *priv)
{
	u32 addr_h, addr_l, size;
	int err;

	addr_h = readl(priv->eeprom_base + NETSEC_EEPROM_HM_ME_ADDRESS_H);
	addr_l = readl(priv->eeprom_base + NETSEC_EEPROM_HM_ME_ADDRESS_L);
	size = readl(priv->eeprom_base + NETSEC_EEPROM_HM_ME_SIZE);
	err = netsec_netdev_load_ucode_region(priv, NETSEC_REG_DMAC_HM_CMD_BUF,
					      addr_h, addr_l, size);
	if (err)
		return err;

	addr_h = readl(priv->eeprom_base + NETSEC_EEPROM_MH_ME_ADDRESS_H);
	addr_l = readl(priv->eeprom_base + NETSEC_EEPROM_MH_ME_ADDRESS_L);
	size = readl(priv->eeprom_base + NETSEC_EEPROM_MH_ME_SIZE);
	err = netsec_netdev_load_ucode_region(priv, NETSEC_REG_DMAC_MH_CMD_BUF,
					      addr_h, addr_l, size);
	if (err)
		return err;

	addr_h = 0;
	addr_l = readl(priv->eeprom_base + NETSEC_EEPROM_PKT_ME_ADDRESS);
	size = readl(priv->eeprom_base + NETSEC_EEPROM_PKT_ME_SIZE);
	err = netsec_netdev_load_ucode_region(priv, NETSEC_REG_PKT_CMD_BUF,
					      addr_h, addr_l, size);
	if (err)
		return err;

	return 0;
}

static int netsec_reset_hardware(struct netsec_priv *priv)
{
	u32 value;
	int err;

	/* stop DMA engines */
	if (!netsec_read(priv, NETSEC_REG_ADDR_DIS_CORE)) {
		netsec_write(priv, NETSEC_REG_DMA_HM_CTRL,
			     NETSEC_DMA_CTRL_REG_STOP);
		netsec_write(priv, NETSEC_REG_DMA_MH_CTRL,
			     NETSEC_DMA_CTRL_REG_STOP);

		while (netsec_read(priv, NETSEC_REG_DMA_HM_CTRL) &
		       NETSEC_DMA_CTRL_REG_STOP)
			cpu_relax();

		while (netsec_read(priv, NETSEC_REG_DMA_MH_CTRL) &
		       NETSEC_DMA_CTRL_REG_STOP)
			cpu_relax();
	}

	netsec_write(priv, NETSEC_REG_SOFT_RST, NETSEC_SOFT_RST_REG_RESET);
	netsec_write(priv, NETSEC_REG_SOFT_RST, NETSEC_SOFT_RST_REG_RUN);
	netsec_write(priv, NETSEC_REG_COM_INIT, NETSEC_COM_INIT_REG_ALL);

	while (netsec_read(priv, NETSEC_REG_COM_INIT) != 0)
		cpu_relax();

	/* set desc_start addr */
	netsec_write(priv, NETSEC_REG_NRM_RX_DESC_START_UP,
		     upper_32_bits(priv->desc_ring[NETSEC_RING_RX].desc_dma));
	netsec_write(priv, NETSEC_REG_NRM_RX_DESC_START_LW,
		     lower_32_bits(priv->desc_ring[NETSEC_RING_RX].desc_dma));

	netsec_write(priv, NETSEC_REG_NRM_TX_DESC_START_UP,
		     upper_32_bits(priv->desc_ring[NETSEC_RING_TX].desc_dma));
	netsec_write(priv, NETSEC_REG_NRM_TX_DESC_START_LW,
		     lower_32_bits(priv->desc_ring[NETSEC_RING_TX].desc_dma));

	/* set normal tx dring ring config */
	netsec_write(priv, NETSEC_REG_NRM_TX_CONFIG,
		     1 << NETSEC_REG_DESC_ENDIAN);
	netsec_write(priv, NETSEC_REG_NRM_RX_CONFIG,
		     1 << NETSEC_REG_DESC_ENDIAN);

	err = netsec_netdev_load_microcode(priv);
	if (err) {
		netif_err(priv, probe, priv->ndev,
			  "%s: failed to load microcode (%d)\n", __func__, err);
		return err;
	}

	/* start DMA engines */
	netsec_write(priv, NETSEC_REG_DMA_TMR_CTRL, priv->freq / 1000000 - 1);
	netsec_write(priv, NETSEC_REG_ADDR_DIS_CORE, 0);

	usleep_range(1000, 2000);

	if (!(netsec_read(priv, NETSEC_REG_TOP_STATUS) &
	      NETSEC_TOP_IRQ_REG_CODE_LOAD_END)) {
		netif_err(priv, probe, priv->ndev,
			  "microengine start failed\n");
		return -ENXIO;
	}
	netsec_write(priv, NETSEC_REG_TOP_STATUS,
		     NETSEC_TOP_IRQ_REG_CODE_LOAD_END);

	value = NETSEC_PKT_CTRL_REG_MODE_NRM;
	if (priv->ndev->mtu > ETH_DATA_LEN)
		value |= NETSEC_PKT_CTRL_REG_EN_JUMBO;

	/* change to normal mode */
	netsec_write(priv, NETSEC_REG_DMA_MH_CTRL, MH_CTRL__MODE_TRANS);
	netsec_write(priv, NETSEC_REG_PKT_CTRL, value);

	while ((netsec_read(priv, NETSEC_REG_MODE_TRANS_COMP_STATUS) &
		NETSEC_MODE_TRANS_COMP_IRQ_T2N) == 0)
		cpu_relax();

	/* clear any pending EMPTY/ERR irq status */
	netsec_write(priv, NETSEC_REG_NRM_TX_STATUS, ~0);

	/* Disable TX & RX intr */
	netsec_write(priv, NETSEC_REG_INTEN_CLR, ~0);

	return 0;
}

static int netsec_start_gmac(struct netsec_priv *priv)
{
	struct phy_device *phydev = priv->ndev->phydev;
	u32 value = 0;
	int ret;

	if (phydev->speed != SPEED_1000)
		value = (NETSEC_GMAC_MCR_REG_CST |
			 NETSEC_GMAC_MCR_REG_HALF_DUPLEX_COMMON);

	if (netsec_mac_write(priv, GMAC_REG_MCR, value))
		return -ETIMEDOUT;
	if (netsec_mac_write(priv, GMAC_REG_BMR,
			     NETSEC_GMAC_BMR_REG_RESET))
		return -ETIMEDOUT;

	/* Wait soft reset */
	usleep_range(1000, 5000);

	ret = netsec_mac_read(priv, GMAC_REG_BMR, &value);
	if (ret)
		return ret;
	if (value & NETSEC_GMAC_BMR_REG_SWR)
		return -EAGAIN;

	netsec_write(priv, MAC_REG_DESC_SOFT_RST, 1);
	if (netsec_wait_while_busy(priv, MAC_REG_DESC_SOFT_RST, 1))
		return -ETIMEDOUT;

	netsec_write(priv, MAC_REG_DESC_INIT, 1);
	if (netsec_wait_while_busy(priv, MAC_REG_DESC_INIT, 1))
		return -ETIMEDOUT;

	if (netsec_mac_write(priv, GMAC_REG_BMR,
			     NETSEC_GMAC_BMR_REG_COMMON))
		return -ETIMEDOUT;
	if (netsec_mac_write(priv, GMAC_REG_RDLAR,
			     NETSEC_GMAC_RDLAR_REG_COMMON))
		return -ETIMEDOUT;
	if (netsec_mac_write(priv, GMAC_REG_TDLAR,
			     NETSEC_GMAC_TDLAR_REG_COMMON))
		return -ETIMEDOUT;
	if (netsec_mac_write(priv, GMAC_REG_MFFR, 0x80000001))
		return -ETIMEDOUT;

	ret = netsec_mac_update_to_phy_state(priv);
	if (ret)
		return ret;

	ret = netsec_mac_read(priv, GMAC_REG_OMR, &value);
	if (ret)
		return ret;

	value |= NETSEC_GMAC_OMR_REG_SR;
	value |= NETSEC_GMAC_OMR_REG_ST;

	netsec_write(priv, NETSEC_REG_NRM_RX_INTEN_CLR, ~0);
	netsec_write(priv, NETSEC_REG_NRM_TX_INTEN_CLR, ~0);

	netsec_et_set_coalesce(priv->ndev, &priv->et_coalesce);

	if (netsec_mac_write(priv, GMAC_REG_OMR, value))
		return -ETIMEDOUT;

	return 0;
}

static int netsec_stop_gmac(struct netsec_priv *priv)
{
	u32 value;
	int ret;

	ret = netsec_mac_read(priv, GMAC_REG_OMR, &value);
	if (ret)
		return ret;
	value &= ~NETSEC_GMAC_OMR_REG_SR;
	value &= ~NETSEC_GMAC_OMR_REG_ST;

	/* disable all interrupts */
	netsec_write(priv, NETSEC_REG_NRM_RX_INTEN_CLR, ~0);
	netsec_write(priv, NETSEC_REG_NRM_TX_INTEN_CLR, ~0);

	return netsec_mac_write(priv, GMAC_REG_OMR, value);
}

static void netsec_phy_adjust_link(struct net_device *ndev)
{
	struct netsec_priv *priv = netdev_priv(ndev);

	if (ndev->phydev->link)
		netsec_start_gmac(priv);
	else
		netsec_stop_gmac(priv);

	phy_print_status(ndev->phydev);
}

static irqreturn_t netsec_irq_handler(int irq, void *dev_id)
{
	struct netsec_priv *priv = dev_id;
	u32 val, status = netsec_read(priv, NETSEC_REG_TOP_STATUS);
	unsigned long flags;

	/* Disable interrupts */
	if (status & NETSEC_IRQ_TX) {
		val = netsec_read(priv, NETSEC_REG_NRM_TX_STATUS);
		netsec_write(priv, NETSEC_REG_NRM_TX_STATUS, val);
	}
	if (status & NETSEC_IRQ_RX) {
		val = netsec_read(priv, NETSEC_REG_NRM_RX_STATUS);
		netsec_write(priv, NETSEC_REG_NRM_RX_STATUS, val);
	}

	spin_lock_irqsave(&priv->reglock, flags);
	netsec_write(priv, NETSEC_REG_INTEN_CLR, NETSEC_IRQ_RX | NETSEC_IRQ_TX);
	spin_unlock_irqrestore(&priv->reglock, flags);

	napi_schedule(&priv->napi);

	return IRQ_HANDLED;
}

static int netsec_netdev_open(struct net_device *ndev)
{
	struct netsec_priv *priv = netdev_priv(ndev);
	int ret;

	pm_runtime_get_sync(priv->dev);

	ret = netsec_setup_rx_dring(priv);
	if (ret) {
		netif_err(priv, probe, priv->ndev,
			  "%s: fail setup ring\n", __func__);
		goto err1;
	}

	ret = request_irq(priv->ndev->irq, netsec_irq_handler,
			  IRQF_SHARED, "netsec", priv);
	if (ret) {
		netif_err(priv, drv, priv->ndev, "request_irq failed\n");
		goto err2;
	}

	if (dev_of_node(priv->dev)) {
		if (!of_phy_connect(priv->ndev, priv->phy_np,
				    netsec_phy_adjust_link, 0,
				    priv->phy_interface)) {
			netif_err(priv, link, priv->ndev, "missing PHY\n");
			ret = -ENODEV;
			goto err3;
		}
	} else {
		ret = phy_connect_direct(priv->ndev, priv->phydev,
					 netsec_phy_adjust_link,
					 priv->phy_interface);
		if (ret) {
			netif_err(priv, link, priv->ndev,
				  "phy_connect_direct() failed (%d)\n", ret);
			goto err3;
		}
	}

	phy_start(ndev->phydev);

	netsec_start_gmac(priv);
	napi_enable(&priv->napi);
	netif_start_queue(ndev);

	/* Enable RX intr. */
	netsec_write(priv, NETSEC_REG_INTEN_SET, NETSEC_IRQ_RX);

	return 0;
err3:
	free_irq(priv->ndev->irq, priv);
err2:
	netsec_uninit_pkt_dring(priv, NETSEC_RING_RX);
err1:
	pm_runtime_put_sync(priv->dev);
	return ret;
}

static int netsec_netdev_stop(struct net_device *ndev)
{
	struct netsec_priv *priv = netdev_priv(ndev);

	netif_stop_queue(priv->ndev);
	dma_wmb();

	napi_disable(&priv->napi);

	netsec_write(priv, NETSEC_REG_INTEN_CLR, ~0);
	netsec_stop_gmac(priv);

	free_irq(priv->ndev->irq, priv);

	netsec_uninit_pkt_dring(priv, NETSEC_RING_TX);
	netsec_uninit_pkt_dring(priv, NETSEC_RING_RX);

	phy_stop(ndev->phydev);
	phy_disconnect(ndev->phydev);

	pm_runtime_put_sync(priv->dev);

	return 0;
}

static int netsec_netdev_init(struct net_device *ndev)
{
	struct netsec_priv *priv = netdev_priv(ndev);
	int ret;

	ret = netsec_alloc_dring(priv, NETSEC_RING_TX);
	if (ret)
		return ret;

	ret = netsec_alloc_dring(priv, NETSEC_RING_RX);
	if (ret)
		goto err1;

	ret = netsec_reset_hardware(priv);
	if (ret)
		goto err2;

	return 0;
err2:
	netsec_free_dring(priv, NETSEC_RING_RX);
err1:
	netsec_free_dring(priv, NETSEC_RING_TX);
	return ret;
}

static void netsec_netdev_uninit(struct net_device *ndev)
{
	struct netsec_priv *priv = netdev_priv(ndev);

	netsec_free_dring(priv, NETSEC_RING_RX);
	netsec_free_dring(priv, NETSEC_RING_TX);
}

static int netsec_netdev_set_features(struct net_device *ndev,
				      netdev_features_t features)
{
	struct netsec_priv *priv = netdev_priv(ndev);

	priv->rx_cksum_offload_flag = !!(features & NETIF_F_RXCSUM);

	return 0;
}

static int netsec_netdev_ioctl(struct net_device *ndev, struct ifreq *ifr,
			       int cmd)
{
	return phy_mii_ioctl(ndev->phydev, ifr, cmd);
}

static const struct net_device_ops netsec_netdev_ops = {
	.ndo_init		= netsec_netdev_init,
	.ndo_uninit		= netsec_netdev_uninit,
	.ndo_open		= netsec_netdev_open,
	.ndo_stop		= netsec_netdev_stop,
	.ndo_start_xmit		= netsec_netdev_start_xmit,
	.ndo_set_features	= netsec_netdev_set_features,
	.ndo_set_mac_address    = eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_do_ioctl		= netsec_netdev_ioctl,
};

static int netsec_of_probe(struct platform_device *pdev,
			   struct netsec_priv *priv)
{
	priv->phy_np = of_parse_phandle(pdev->dev.of_node, "phy-handle", 0);
	if (!priv->phy_np) {
		dev_err(&pdev->dev, "missing required property 'phy-handle'\n");
		return -EINVAL;
	}

	priv->clk = devm_clk_get(&pdev->dev, NULL); /* get by 'phy_ref_clk' */
	if (IS_ERR(priv->clk)) {
		dev_err(&pdev->dev, "phy_ref_clk not found\n");
		return PTR_ERR(priv->clk);
	}
	priv->freq = clk_get_rate(priv->clk);

	return 0;
}

static int netsec_acpi_probe(struct platform_device *pdev,
			     struct netsec_priv *priv, u32 *phy_addr)
{
	int ret;

	if (!IS_ENABLED(CONFIG_ACPI))
		return -ENODEV;

	ret = device_property_read_u32(&pdev->dev, "phy-channel", phy_addr);
	if (ret) {
		dev_err(&pdev->dev,
			"missing required property 'phy-channel'\n");
		return ret;
	}

	ret = device_property_read_u32(&pdev->dev,
				       "socionext,phy-clock-frequency",
				       &priv->freq);
	if (ret)
		dev_err(&pdev->dev,
			"missing required property 'socionext,phy-clock-frequency'\n");
	return ret;
}

static void netsec_unregister_mdio(struct netsec_priv *priv)
{
	struct phy_device *phydev = priv->phydev;

	if (!dev_of_node(priv->dev) && phydev) {
		phy_device_remove(phydev);
		phy_device_free(phydev);
	}

	mdiobus_unregister(priv->mii_bus);
}

static int netsec_register_mdio(struct netsec_priv *priv, u32 phy_addr)
{
	struct mii_bus *bus;
	int ret;

	bus = devm_mdiobus_alloc(priv->dev);
	if (!bus)
		return -ENOMEM;

	snprintf(bus->id, MII_BUS_ID_SIZE, "%s", dev_name(priv->dev));
	bus->priv = priv;
	bus->name = "SNI NETSEC MDIO";
	bus->read = netsec_phy_read;
	bus->write = netsec_phy_write;
	bus->parent = priv->dev;
	priv->mii_bus = bus;

	if (dev_of_node(priv->dev)) {
		struct device_node *mdio_node, *parent = dev_of_node(priv->dev);

		mdio_node = of_get_child_by_name(parent, "mdio");
		if (mdio_node) {
			parent = mdio_node;
		} else {
			/* older f/w doesn't populate the mdio subnode,
			 * allow relaxed upgrade of f/w in due time.
			 */
			dev_info(priv->dev, "Upgrade f/w for mdio subnode!\n");
		}

		ret = of_mdiobus_register(bus, parent);
		of_node_put(mdio_node);

		if (ret) {
			dev_err(priv->dev, "mdiobus register err(%d)\n", ret);
			return ret;
		}
	} else {
		/* Mask out all PHYs from auto probing. */
		bus->phy_mask = ~0;
		ret = mdiobus_register(bus);
		if (ret) {
			dev_err(priv->dev, "mdiobus register err(%d)\n", ret);
			return ret;
		}

		priv->phydev = get_phy_device(bus, phy_addr, false);
		if (IS_ERR(priv->phydev)) {
			ret = PTR_ERR(priv->phydev);
			dev_err(priv->dev, "get_phy_device err(%d)\n", ret);
			priv->phydev = NULL;
			return -ENODEV;
		}

		ret = phy_device_register(priv->phydev);
		if (ret) {
			mdiobus_unregister(bus);
			dev_err(priv->dev,
				"phy_device_register err(%d)\n", ret);
		}
	}

	return ret;
}

static int netsec_probe(struct platform_device *pdev)
{
	struct resource *mmio_res, *eeprom_res, *irq_res;
	u8 *mac, macbuf[ETH_ALEN];
	struct netsec_priv *priv;
	u32 hw_ver, phy_addr = 0;
	struct net_device *ndev;
	int ret;

	mmio_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mmio_res) {
		dev_err(&pdev->dev, "No MMIO resource found.\n");
		return -ENODEV;
	}

	eeprom_res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!eeprom_res) {
		dev_info(&pdev->dev, "No EEPROM resource found.\n");
		return -ENODEV;
	}

	irq_res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!irq_res) {
		dev_err(&pdev->dev, "No IRQ resource found.\n");
		return -ENODEV;
	}

	ndev = alloc_etherdev(sizeof(*priv));
	if (!ndev)
		return -ENOMEM;

	priv = netdev_priv(ndev);

	spin_lock_init(&priv->reglock);
	SET_NETDEV_DEV(ndev, &pdev->dev);
	platform_set_drvdata(pdev, priv);
	ndev->irq = irq_res->start;
	priv->dev = &pdev->dev;
	priv->ndev = ndev;

	priv->msg_enable = NETIF_MSG_TX_ERR | NETIF_MSG_HW | NETIF_MSG_DRV |
			   NETIF_MSG_LINK | NETIF_MSG_PROBE;

	priv->phy_interface = device_get_phy_mode(&pdev->dev);
	if (priv->phy_interface < 0) {
		dev_err(&pdev->dev, "missing required property 'phy-mode'\n");
		ret = -ENODEV;
		goto free_ndev;
	}

	priv->ioaddr = devm_ioremap(&pdev->dev, mmio_res->start,
				    resource_size(mmio_res));
	if (!priv->ioaddr) {
		dev_err(&pdev->dev, "devm_ioremap() failed\n");
		ret = -ENXIO;
		goto free_ndev;
	}

	priv->eeprom_base = devm_ioremap(&pdev->dev, eeprom_res->start,
					 resource_size(eeprom_res));
	if (!priv->eeprom_base) {
		dev_err(&pdev->dev, "devm_ioremap() failed for EEPROM\n");
		ret = -ENXIO;
		goto free_ndev;
	}

	mac = device_get_mac_address(&pdev->dev, macbuf, sizeof(macbuf));
	if (mac)
		ether_addr_copy(ndev->dev_addr, mac);

	if (priv->eeprom_base &&
	    (!mac || !is_valid_ether_addr(ndev->dev_addr))) {
		void __iomem *macp = priv->eeprom_base +
					NETSEC_EEPROM_MAC_ADDRESS;

		ndev->dev_addr[0] = readb(macp + 3);
		ndev->dev_addr[1] = readb(macp + 2);
		ndev->dev_addr[2] = readb(macp + 1);
		ndev->dev_addr[3] = readb(macp + 0);
		ndev->dev_addr[4] = readb(macp + 7);
		ndev->dev_addr[5] = readb(macp + 6);
	}

	if (!is_valid_ether_addr(ndev->dev_addr)) {
		dev_warn(&pdev->dev, "No MAC address found, using random\n");
		eth_hw_addr_random(ndev);
	}

	if (dev_of_node(&pdev->dev))
		ret = netsec_of_probe(pdev, priv);
	else
		ret = netsec_acpi_probe(pdev, priv, &phy_addr);
	if (ret)
		goto free_ndev;

	if (!priv->freq) {
		dev_err(&pdev->dev, "missing PHY reference clock frequency\n");
		ret = -ENODEV;
		goto free_ndev;
	}

	/* default for throughput */
	priv->et_coalesce.rx_coalesce_usecs = 500;
	priv->et_coalesce.rx_max_coalesced_frames = 8;
	priv->et_coalesce.tx_coalesce_usecs = 500;
	priv->et_coalesce.tx_max_coalesced_frames = 8;

	ret = device_property_read_u32(&pdev->dev, "max-frame-size",
				       &ndev->max_mtu);
	if (ret < 0)
		ndev->max_mtu = ETH_DATA_LEN;

	/* runtime_pm coverage just for probe, open/close also cover it */
	pm_runtime_enable(&pdev->dev);
	pm_runtime_get_sync(&pdev->dev);

	hw_ver = netsec_read(priv, NETSEC_REG_F_TAIKI_VER);
	/* this driver only supports F_TAIKI style NETSEC */
	if (NETSEC_F_NETSEC_VER_MAJOR_NUM(hw_ver) !=
	    NETSEC_F_NETSEC_VER_MAJOR_NUM(NETSEC_REG_NETSEC_VER_F_TAIKI)) {
		ret = -ENODEV;
		goto pm_disable;
	}

	dev_info(&pdev->dev, "hardware revision %d.%d\n",
		 hw_ver >> 16, hw_ver & 0xffff);

	netif_napi_add(ndev, &priv->napi, netsec_napi_poll, NAPI_BUDGET);

	ndev->netdev_ops = &netsec_netdev_ops;
	ndev->ethtool_ops = &netsec_ethtool_ops;

	ndev->features |= NETIF_F_HIGHDMA | NETIF_F_RXCSUM | NETIF_F_GSO |
				NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM;
	ndev->hw_features = ndev->features;

	priv->rx_cksum_offload_flag = true;

	ret = netsec_register_mdio(priv, phy_addr);
	if (ret)
		goto unreg_napi;

	if (dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64)))
		dev_warn(&pdev->dev, "Failed to enable 64-bit DMA\n");

	ret = register_netdev(ndev);
	if (ret) {
		netif_err(priv, probe, ndev, "register_netdev() failed\n");
		goto unreg_mii;
	}

	pm_runtime_put_sync(&pdev->dev);
	return 0;

unreg_mii:
	netsec_unregister_mdio(priv);
unreg_napi:
	netif_napi_del(&priv->napi);
pm_disable:
	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
free_ndev:
	free_netdev(ndev);
	dev_err(&pdev->dev, "init failed\n");

	return ret;
}

static int netsec_remove(struct platform_device *pdev)
{
	struct netsec_priv *priv = platform_get_drvdata(pdev);

	unregister_netdev(priv->ndev);

	netsec_unregister_mdio(priv);

	netif_napi_del(&priv->napi);

	pm_runtime_disable(&pdev->dev);
	free_netdev(priv->ndev);

	return 0;
}

#ifdef CONFIG_PM
static int netsec_runtime_suspend(struct device *dev)
{
	struct netsec_priv *priv = dev_get_drvdata(dev);

	netsec_write(priv, NETSEC_REG_CLK_EN, 0);

	clk_disable_unprepare(priv->clk);

	return 0;
}

static int netsec_runtime_resume(struct device *dev)
{
	struct netsec_priv *priv = dev_get_drvdata(dev);

	clk_prepare_enable(priv->clk);

	netsec_write(priv, NETSEC_REG_CLK_EN, NETSEC_CLK_EN_REG_DOM_D |
					       NETSEC_CLK_EN_REG_DOM_C |
					       NETSEC_CLK_EN_REG_DOM_G);
	return 0;
}
#endif

static const struct dev_pm_ops netsec_pm_ops = {
	SET_RUNTIME_PM_OPS(netsec_runtime_suspend, netsec_runtime_resume, NULL)
};

static const struct of_device_id netsec_dt_ids[] = {
	{ .compatible = "socionext,synquacer-netsec" },
	{ }
};
MODULE_DEVICE_TABLE(of, netsec_dt_ids);

#ifdef CONFIG_ACPI
static const struct acpi_device_id netsec_acpi_ids[] = {
	{ "SCX0001" },
	{ }
};
MODULE_DEVICE_TABLE(acpi, netsec_acpi_ids);
#endif

static struct platform_driver netsec_driver = {
	.probe	= netsec_probe,
	.remove	= netsec_remove,
	.driver = {
		.name = "netsec",
		.pm = &netsec_pm_ops,
		.of_match_table = netsec_dt_ids,
		.acpi_match_table = ACPI_PTR(netsec_acpi_ids),
	},
};
module_platform_driver(netsec_driver);

MODULE_AUTHOR("Jassi Brar <jaswinder.singh@linaro.org>");
MODULE_AUTHOR("Ard Biesheuvel <ard.biesheuvel@linaro.org>");
MODULE_DESCRIPTION("NETSEC Ethernet driver");
MODULE_LICENSE("GPL");

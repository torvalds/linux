/*   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; version 2 of the License
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   Copyright (C) 2009-2016 John Crispin <blogic@openwrt.org>
 *   Copyright (C) 2009-2016 Felix Fietkau <nbd@openwrt.org>
 *   Copyright (C) 2013-2016 Michael Lee <igvtee@gmail.com>
 */

#ifndef MTK_ETH_H
#define MTK_ETH_H

#define MTK_QDMA_PAGE_SIZE	2048
#define	MTK_MAX_RX_LENGTH	1536
#define MTK_TX_DMA_BUF_LEN	0x3fff
#define MTK_DMA_SIZE		256
#define MTK_NAPI_WEIGHT		64
#define MTK_MAC_COUNT		2
#define MTK_RX_ETH_HLEN		(VLAN_ETH_HLEN + VLAN_HLEN + ETH_FCS_LEN)
#define MTK_RX_HLEN		(NET_SKB_PAD + MTK_RX_ETH_HLEN + NET_IP_ALIGN)
#define MTK_DMA_DUMMY_DESC	0xffffffff
#define MTK_DEFAULT_MSG_ENABLE	(NETIF_MSG_DRV | \
				 NETIF_MSG_PROBE | \
				 NETIF_MSG_LINK | \
				 NETIF_MSG_TIMER | \
				 NETIF_MSG_IFDOWN | \
				 NETIF_MSG_IFUP | \
				 NETIF_MSG_RX_ERR | \
				 NETIF_MSG_TX_ERR)
#define MTK_HW_FEATURES		(NETIF_F_IP_CSUM | \
				 NETIF_F_RXCSUM | \
				 NETIF_F_HW_VLAN_CTAG_TX | \
				 NETIF_F_HW_VLAN_CTAG_RX | \
				 NETIF_F_SG | NETIF_F_TSO | \
				 NETIF_F_TSO6 | \
				 NETIF_F_IPV6_CSUM)
#define NEXT_RX_DESP_IDX(X)	(((X) + 1) & (MTK_DMA_SIZE - 1))

/* Frame Engine Global Reset Register */
#define MTK_RST_GL		0x04
#define RST_GL_PSE		BIT(0)

/* Frame Engine Interrupt Status Register */
#define MTK_INT_STATUS2		0x08
#define MTK_GDM1_AF		BIT(28)
#define MTK_GDM2_AF		BIT(29)

/* Frame Engine Interrupt Grouping Register */
#define MTK_FE_INT_GRP		0x20

/* CDMP Exgress Control Register */
#define MTK_CDMP_EG_CTRL	0x404

/* GDM Exgress Control Register */
#define MTK_GDMA_FWD_CFG(x)	(0x500 + (x * 0x1000))
#define MTK_GDMA_ICS_EN		BIT(22)
#define MTK_GDMA_TCS_EN		BIT(21)
#define MTK_GDMA_UCS_EN		BIT(20)

/* Unicast Filter MAC Address Register - Low */
#define MTK_GDMA_MAC_ADRL(x)	(0x508 + (x * 0x1000))

/* Unicast Filter MAC Address Register - High */
#define MTK_GDMA_MAC_ADRH(x)	(0x50C + (x * 0x1000))

/* PDMA RX Base Pointer Register */
#define MTK_PRX_BASE_PTR0	0x900

/* PDMA RX Maximum Count Register */
#define MTK_PRX_MAX_CNT0	0x904

/* PDMA RX CPU Pointer Register */
#define MTK_PRX_CRX_IDX0	0x908

/* PDMA Global Configuration Register */
#define MTK_PDMA_GLO_CFG	0xa04
#define MTK_MULTI_EN		BIT(10)

/* PDMA Reset Index Register */
#define MTK_PDMA_RST_IDX	0xa08
#define MTK_PST_DRX_IDX0	BIT(16)

/* PDMA Delay Interrupt Register */
#define MTK_PDMA_DELAY_INT	0xa0c

/* PDMA Interrupt Status Register */
#define MTK_PDMA_INT_STATUS	0xa20

/* PDMA Interrupt Mask Register */
#define MTK_PDMA_INT_MASK	0xa28

/* PDMA Interrupt grouping registers */
#define MTK_PDMA_INT_GRP1	0xa50
#define MTK_PDMA_INT_GRP2	0xa54

/* QDMA TX Queue Configuration Registers */
#define MTK_QTX_CFG(x)		(0x1800 + (x * 0x10))
#define QDMA_RES_THRES		4

/* QDMA TX Queue Scheduler Registers */
#define MTK_QTX_SCH(x)		(0x1804 + (x * 0x10))

/* QDMA RX Base Pointer Register */
#define MTK_QRX_BASE_PTR0	0x1900

/* QDMA RX Maximum Count Register */
#define MTK_QRX_MAX_CNT0	0x1904

/* QDMA RX CPU Pointer Register */
#define MTK_QRX_CRX_IDX0	0x1908

/* QDMA RX DMA Pointer Register */
#define MTK_QRX_DRX_IDX0	0x190C

/* QDMA Global Configuration Register */
#define MTK_QDMA_GLO_CFG	0x1A04
#define MTK_RX_2B_OFFSET	BIT(31)
#define MTK_RX_BT_32DWORDS	(3 << 11)
#define MTK_NDP_CO_PRO		BIT(10)
#define MTK_TX_WB_DDONE		BIT(6)
#define MTK_DMA_SIZE_16DWORDS	(2 << 4)
#define MTK_RX_DMA_BUSY		BIT(3)
#define MTK_TX_DMA_BUSY		BIT(1)
#define MTK_RX_DMA_EN		BIT(2)
#define MTK_TX_DMA_EN		BIT(0)
#define MTK_DMA_BUSY_TIMEOUT	HZ

/* QDMA Reset Index Register */
#define MTK_QDMA_RST_IDX	0x1A08
#define MTK_PST_DRX_IDX0	BIT(16)

/* QDMA Delay Interrupt Register */
#define MTK_QDMA_DELAY_INT	0x1A0C

/* QDMA Flow Control Register */
#define MTK_QDMA_FC_THRES	0x1A10
#define FC_THRES_DROP_MODE	BIT(20)
#define FC_THRES_DROP_EN	(7 << 16)
#define FC_THRES_MIN		0x4444

/* QDMA Interrupt Status Register */
#define MTK_QMTK_INT_STATUS	0x1A18
#define MTK_RX_DONE_INT3	BIT(19)
#define MTK_RX_DONE_INT2	BIT(18)
#define MTK_RX_DONE_INT1	BIT(17)
#define MTK_RX_DONE_INT0	BIT(16)
#define MTK_TX_DONE_INT3	BIT(3)
#define MTK_TX_DONE_INT2	BIT(2)
#define MTK_TX_DONE_INT1	BIT(1)
#define MTK_TX_DONE_INT0	BIT(0)
#define MTK_RX_DONE_INT		(MTK_RX_DONE_INT0 | MTK_RX_DONE_INT1 | \
				 MTK_RX_DONE_INT2 | MTK_RX_DONE_INT3)
#define MTK_TX_DONE_INT		(MTK_TX_DONE_INT0 | MTK_TX_DONE_INT1 | \
				 MTK_TX_DONE_INT2 | MTK_TX_DONE_INT3)

/* QDMA Interrupt grouping registers */
#define MTK_QDMA_INT_GRP1	0x1a20
#define MTK_QDMA_INT_GRP2	0x1a24
#define MTK_RLS_DONE_INT	BIT(0)

/* QDMA Interrupt Status Register */
#define MTK_QDMA_INT_MASK	0x1A1C

/* QDMA Interrupt Mask Register */
#define MTK_QDMA_HRED2		0x1A44

/* QDMA TX Forward CPU Pointer Register */
#define MTK_QTX_CTX_PTR		0x1B00

/* QDMA TX Forward DMA Pointer Register */
#define MTK_QTX_DTX_PTR		0x1B04

/* QDMA TX Release CPU Pointer Register */
#define MTK_QTX_CRX_PTR		0x1B10

/* QDMA TX Release DMA Pointer Register */
#define MTK_QTX_DRX_PTR		0x1B14

/* QDMA FQ Head Pointer Register */
#define MTK_QDMA_FQ_HEAD	0x1B20

/* QDMA FQ Head Pointer Register */
#define MTK_QDMA_FQ_TAIL	0x1B24

/* QDMA FQ Free Page Counter Register */
#define MTK_QDMA_FQ_CNT		0x1B28

/* QDMA FQ Free Page Buffer Length Register */
#define MTK_QDMA_FQ_BLEN	0x1B2C

/* GMA1 Received Good Byte Count Register */
#define MTK_GDM1_TX_GBCNT	0x2400
#define MTK_STAT_OFFSET		0x40

/* QDMA descriptor txd4 */
#define TX_DMA_CHKSUM		(0x7 << 29)
#define TX_DMA_TSO		BIT(28)
#define TX_DMA_FPORT_SHIFT	25
#define TX_DMA_FPORT_MASK	0x7
#define TX_DMA_INS_VLAN		BIT(16)

/* QDMA descriptor txd3 */
#define TX_DMA_OWNER_CPU	BIT(31)
#define TX_DMA_LS0		BIT(30)
#define TX_DMA_PLEN0(_x)	(((_x) & MTK_TX_DMA_BUF_LEN) << 16)
#define TX_DMA_SWC		BIT(14)
#define TX_DMA_SDL(_x)		(((_x) & 0x3fff) << 16)

/* QDMA descriptor rxd2 */
#define RX_DMA_DONE		BIT(31)
#define RX_DMA_PLEN0(_x)	(((_x) & 0x3fff) << 16)
#define RX_DMA_GET_PLEN0(_x)	(((_x) >> 16) & 0x3fff)

/* QDMA descriptor rxd3 */
#define RX_DMA_VID(_x)		((_x) & 0xfff)

/* QDMA descriptor rxd4 */
#define RX_DMA_L4_VALID		BIT(24)
#define RX_DMA_FPORT_SHIFT	19
#define RX_DMA_FPORT_MASK	0x7

/* PHY Indirect Access Control registers */
#define MTK_PHY_IAC		0x10004
#define PHY_IAC_ACCESS		BIT(31)
#define PHY_IAC_READ		BIT(19)
#define PHY_IAC_WRITE		BIT(18)
#define PHY_IAC_START		BIT(16)
#define PHY_IAC_ADDR_SHIFT	20
#define PHY_IAC_REG_SHIFT	25
#define PHY_IAC_TIMEOUT		HZ

/* Mac control registers */
#define MTK_MAC_MCR(x)		(0x10100 + (x * 0x100))
#define MAC_MCR_MAX_RX_1536	BIT(24)
#define MAC_MCR_IPG_CFG		(BIT(18) | BIT(16))
#define MAC_MCR_FORCE_MODE	BIT(15)
#define MAC_MCR_TX_EN		BIT(14)
#define MAC_MCR_RX_EN		BIT(13)
#define MAC_MCR_BACKOFF_EN	BIT(9)
#define MAC_MCR_BACKPR_EN	BIT(8)
#define MAC_MCR_FORCE_RX_FC	BIT(5)
#define MAC_MCR_FORCE_TX_FC	BIT(4)
#define MAC_MCR_SPEED_1000	BIT(3)
#define MAC_MCR_SPEED_100	BIT(2)
#define MAC_MCR_FORCE_DPX	BIT(1)
#define MAC_MCR_FORCE_LINK	BIT(0)
#define MAC_MCR_FIXED_LINK	(MAC_MCR_MAX_RX_1536 | MAC_MCR_IPG_CFG | \
				 MAC_MCR_FORCE_MODE | MAC_MCR_TX_EN | \
				 MAC_MCR_RX_EN | MAC_MCR_BACKOFF_EN | \
				 MAC_MCR_BACKPR_EN | MAC_MCR_FORCE_RX_FC | \
				 MAC_MCR_FORCE_TX_FC | MAC_MCR_SPEED_1000 | \
				 MAC_MCR_FORCE_DPX | MAC_MCR_FORCE_LINK)

/* GPIO port control registers for GMAC 2*/
#define GPIO_OD33_CTRL8		0x4c0
#define GPIO_BIAS_CTRL		0xed0
#define GPIO_DRV_SEL10		0xf00

/* ethernet subsystem config register */
#define ETHSYS_SYSCFG0		0x14
#define SYSCFG0_GE_MASK		0x3
#define SYSCFG0_GE_MODE(x, y)	(x << (12 + (y * 2)))

struct mtk_rx_dma {
	unsigned int rxd1;
	unsigned int rxd2;
	unsigned int rxd3;
	unsigned int rxd4;
} __packed __aligned(4);

struct mtk_tx_dma {
	unsigned int txd1;
	unsigned int txd2;
	unsigned int txd3;
	unsigned int txd4;
} __packed __aligned(4);

struct mtk_eth;
struct mtk_mac;

/* struct mtk_hw_stats - the structure that holds the traffic statistics.
 * @stats_lock:		make sure that stats operations are atomic
 * @reg_offset:		the status register offset of the SoC
 * @syncp:		the refcount
 *
 * All of the supported SoCs have hardware counters for traffic statistics.
 * Whenever the status IRQ triggers we can read the latest stats from these
 * counters and store them in this struct.
 */
struct mtk_hw_stats {
	u64 tx_bytes;
	u64 tx_packets;
	u64 tx_skip;
	u64 tx_collisions;
	u64 rx_bytes;
	u64 rx_packets;
	u64 rx_overflow;
	u64 rx_fcs_errors;
	u64 rx_short_errors;
	u64 rx_long_errors;
	u64 rx_checksum_errors;
	u64 rx_flow_control_packets;

	spinlock_t		stats_lock;
	u32			reg_offset;
	struct u64_stats_sync	syncp;
};

/* PDMA descriptor can point at 1-2 segments. This enum allows us to track how
 * memory was allocated so that it can be freed properly
 */
enum mtk_tx_flags {
	MTK_TX_FLAGS_SINGLE0	= 0x01,
	MTK_TX_FLAGS_PAGE0	= 0x02,
};

/* struct mtk_tx_buf -	This struct holds the pointers to the memory pointed at
 *			by the TX descriptor	s
 * @skb:		The SKB pointer of the packet being sent
 * @dma_addr0:		The base addr of the first segment
 * @dma_len0:		The length of the first segment
 * @dma_addr1:		The base addr of the second segment
 * @dma_len1:		The length of the second segment
 */
struct mtk_tx_buf {
	struct sk_buff *skb;
	u32 flags;
	DEFINE_DMA_UNMAP_ADDR(dma_addr0);
	DEFINE_DMA_UNMAP_LEN(dma_len0);
	DEFINE_DMA_UNMAP_ADDR(dma_addr1);
	DEFINE_DMA_UNMAP_LEN(dma_len1);
};

/* struct mtk_tx_ring -	This struct holds info describing a TX ring
 * @dma:		The descriptor ring
 * @buf:		The memory pointed at by the ring
 * @phys:		The physical addr of tx_buf
 * @next_free:		Pointer to the next free descriptor
 * @last_free:		Pointer to the last free descriptor
 * @thresh:		The threshold of minimum amount of free descriptors
 * @free_count:		QDMA uses a linked list. Track how many free descriptors
 *			are present
 */
struct mtk_tx_ring {
	struct mtk_tx_dma *dma;
	struct mtk_tx_buf *buf;
	dma_addr_t phys;
	struct mtk_tx_dma *next_free;
	struct mtk_tx_dma *last_free;
	u16 thresh;
	atomic_t free_count;
};

/* struct mtk_rx_ring -	This struct holds info describing a RX ring
 * @dma:		The descriptor ring
 * @data:		The memory pointed at by the ring
 * @phys:		The physical addr of rx_buf
 * @frag_size:		How big can each fragment be
 * @buf_size:		The size of each packet buffer
 * @calc_idx:		The current head of ring
 */
struct mtk_rx_ring {
	struct mtk_rx_dma *dma;
	u8 **data;
	dma_addr_t phys;
	u16 frag_size;
	u16 buf_size;
	u16 calc_idx;
};

/* currently no SoC has more than 2 macs */
#define MTK_MAX_DEVS			2

/* struct mtk_eth -	This is the main datasructure for holding the state
 *			of the driver
 * @dev:		The device pointer
 * @base:		The mapped register i/o base
 * @page_lock:		Make sure that register operations are atomic
 * @dummy_dev:		we run 2 netdevs on 1 physical DMA ring and need a
 *			dummy for NAPI to work
 * @netdev:		The netdev instances
 * @mac:		Each netdev is linked to a physical MAC
 * @irq:		The IRQ that we are using
 * @msg_enable:		Ethtool msg level
 * @ethsys:		The register map pointing at the range used to setup
 *			MII modes
 * @pctl:		The register map pointing at the range used to setup
 *			GMAC port drive/slew values
 * @dma_refcnt:		track how many netdevs are using the DMA engine
 * @tx_ring:		Pointer to the memore holding info about the TX ring
 * @rx_ring:		Pointer to the memore holding info about the RX ring
 * @tx_napi:		The TX NAPI struct
 * @rx_napi:		The RX NAPI struct
 * @scratch_ring:	Newer SoCs need memory for a second HW managed TX ring
 * @phy_scratch_ring:	physical address of scratch_ring
 * @scratch_head:	The scratch memory that scratch_ring points to.
 * @clk_ethif:		The ethif clock
 * @clk_esw:		The switch clock
 * @clk_gp1:		The gmac1 clock
 * @clk_gp2:		The gmac2 clock
 * @mii_bus:		If there is a bus we need to create an instance for it
 * @pending_work:	The workqueue used to reset the dma ring
 */

struct mtk_eth {
	struct device			*dev;
	void __iomem			*base;
	struct reset_control		*rstc;
	spinlock_t			page_lock;
	spinlock_t			irq_lock;
	struct net_device		dummy_dev;
	struct net_device		*netdev[MTK_MAX_DEVS];
	struct mtk_mac			*mac[MTK_MAX_DEVS];
	int				irq[3];
	u32				msg_enable;
	unsigned long			sysclk;
	struct regmap			*ethsys;
	struct regmap			*pctl;
	atomic_t			dma_refcnt;
	struct mtk_tx_ring		tx_ring;
	struct mtk_rx_ring		rx_ring;
	struct napi_struct		tx_napi;
	struct napi_struct		rx_napi;
	struct mtk_tx_dma		*scratch_ring;
	dma_addr_t			phy_scratch_ring;
	void				*scratch_head;
	struct clk			*clk_ethif;
	struct clk			*clk_esw;
	struct clk			*clk_gp1;
	struct clk			*clk_gp2;
	struct mii_bus			*mii_bus;
	struct work_struct		pending_work;
};

/* struct mtk_mac -	the structure that holds the info about the MACs of the
 *			SoC
 * @id:			The number of the MAC
 * @of_node:		Our devicetree node
 * @hw:			Backpointer to our main datastruture
 * @hw_stats:		Packet statistics counter
 * @phy_dev:		The attached PHY if available
 */
struct mtk_mac {
	int				id;
	struct device_node		*of_node;
	struct mtk_eth			*hw;
	struct mtk_hw_stats		*hw_stats;
	struct phy_device		*phy_dev;
};

/* the struct describing the SoC. these are declared in the soc_xyz.c files */
extern const struct of_device_id of_mtk_match[];

/* read the hardware status register */
void mtk_stats_update_mac(struct mtk_mac *mac);

void mtk_w32(struct mtk_eth *eth, u32 val, unsigned reg);
u32 mtk_r32(struct mtk_eth *eth, unsigned reg);

#endif /* MTK_ETH_H */

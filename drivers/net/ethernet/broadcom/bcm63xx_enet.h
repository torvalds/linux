#ifndef BCM63XX_ENET_H_
#define BCM63XX_ENET_H_

#include <linux/types.h>
#include <linux/mii.h>
#include <linux/mutex.h>
#include <linux/phy.h>
#include <linux/platform_device.h>

#include <bcm63xx_regs.h>
#include <bcm63xx_irq.h>
#include <bcm63xx_io.h>
#include <bcm63xx_iudma.h>

/* default number of descriptor */
#define BCMENET_DEF_RX_DESC	64
#define BCMENET_DEF_TX_DESC	32

/* maximum burst len for dma (4 bytes unit) */
#define BCMENET_DMA_MAXBURST	16
#define BCMENETSW_DMA_MAXBURST	8

/* tx transmit threshold (4 bytes unit), fifo is 256 bytes, the value
 * must be low enough so that a DMA transfer of above burst length can
 * not overflow the fifo  */
#define BCMENET_TX_FIFO_TRESH	32

/*
 * hardware maximum rx/tx packet size including FCS, max mtu is
 * actually 2047, but if we set max rx size register to 2047 we won't
 * get overflow information if packet size is 2048 or above
 */
#define BCMENET_MAX_MTU		2046

/*
 * MIB Counters register definitions
*/
#define ETH_MIB_TX_GD_OCTETS			0
#define ETH_MIB_TX_GD_PKTS			1
#define ETH_MIB_TX_ALL_OCTETS			2
#define ETH_MIB_TX_ALL_PKTS			3
#define ETH_MIB_TX_BRDCAST			4
#define ETH_MIB_TX_MULT				5
#define ETH_MIB_TX_64				6
#define ETH_MIB_TX_65_127			7
#define ETH_MIB_TX_128_255			8
#define ETH_MIB_TX_256_511			9
#define ETH_MIB_TX_512_1023			10
#define ETH_MIB_TX_1024_MAX			11
#define ETH_MIB_TX_JAB				12
#define ETH_MIB_TX_OVR				13
#define ETH_MIB_TX_FRAG				14
#define ETH_MIB_TX_UNDERRUN			15
#define ETH_MIB_TX_COL				16
#define ETH_MIB_TX_1_COL			17
#define ETH_MIB_TX_M_COL			18
#define ETH_MIB_TX_EX_COL			19
#define ETH_MIB_TX_LATE				20
#define ETH_MIB_TX_DEF				21
#define ETH_MIB_TX_CRS				22
#define ETH_MIB_TX_PAUSE			23

#define ETH_MIB_RX_GD_OCTETS			32
#define ETH_MIB_RX_GD_PKTS			33
#define ETH_MIB_RX_ALL_OCTETS			34
#define ETH_MIB_RX_ALL_PKTS			35
#define ETH_MIB_RX_BRDCAST			36
#define ETH_MIB_RX_MULT				37
#define ETH_MIB_RX_64				38
#define ETH_MIB_RX_65_127			39
#define ETH_MIB_RX_128_255			40
#define ETH_MIB_RX_256_511			41
#define ETH_MIB_RX_512_1023			42
#define ETH_MIB_RX_1024_MAX			43
#define ETH_MIB_RX_JAB				44
#define ETH_MIB_RX_OVR				45
#define ETH_MIB_RX_FRAG				46
#define ETH_MIB_RX_DROP				47
#define ETH_MIB_RX_CRC_ALIGN			48
#define ETH_MIB_RX_UND				49
#define ETH_MIB_RX_CRC				50
#define ETH_MIB_RX_ALIGN			51
#define ETH_MIB_RX_SYM				52
#define ETH_MIB_RX_PAUSE			53
#define ETH_MIB_RX_CNTRL			54


/*
 * SW MIB Counters register definitions
*/
#define ETHSW_MIB_TX_ALL_OCT			0
#define ETHSW_MIB_TX_DROP_PKTS			2
#define ETHSW_MIB_TX_QOS_PKTS			3
#define ETHSW_MIB_TX_BRDCAST			4
#define ETHSW_MIB_TX_MULT			5
#define ETHSW_MIB_TX_UNI			6
#define ETHSW_MIB_TX_COL			7
#define ETHSW_MIB_TX_1_COL			8
#define ETHSW_MIB_TX_M_COL			9
#define ETHSW_MIB_TX_DEF			10
#define ETHSW_MIB_TX_LATE			11
#define ETHSW_MIB_TX_EX_COL			12
#define ETHSW_MIB_TX_PAUSE			14
#define ETHSW_MIB_TX_QOS_OCT			15

#define ETHSW_MIB_RX_ALL_OCT			17
#define ETHSW_MIB_RX_UND			19
#define ETHSW_MIB_RX_PAUSE			20
#define ETHSW_MIB_RX_64				21
#define ETHSW_MIB_RX_65_127			22
#define ETHSW_MIB_RX_128_255			23
#define ETHSW_MIB_RX_256_511			24
#define ETHSW_MIB_RX_512_1023			25
#define ETHSW_MIB_RX_1024_1522			26
#define ETHSW_MIB_RX_OVR			27
#define ETHSW_MIB_RX_JAB			28
#define ETHSW_MIB_RX_ALIGN			29
#define ETHSW_MIB_RX_CRC			30
#define ETHSW_MIB_RX_GD_OCT			31
#define ETHSW_MIB_RX_DROP			33
#define ETHSW_MIB_RX_UNI			34
#define ETHSW_MIB_RX_MULT			35
#define ETHSW_MIB_RX_BRDCAST			36
#define ETHSW_MIB_RX_SA_CHANGE			37
#define ETHSW_MIB_RX_FRAG			38
#define ETHSW_MIB_RX_OVR_DISC			39
#define ETHSW_MIB_RX_SYM			40
#define ETHSW_MIB_RX_QOS_PKTS			41
#define ETHSW_MIB_RX_QOS_OCT			42
#define ETHSW_MIB_RX_1523_2047			44
#define ETHSW_MIB_RX_2048_4095			45
#define ETHSW_MIB_RX_4096_8191			46
#define ETHSW_MIB_RX_8192_9728			47


struct bcm_enet_mib_counters {
	u64 tx_gd_octets;
	u32 tx_gd_pkts;
	u32 tx_all_octets;
	u32 tx_all_pkts;
	u32 tx_unicast;
	u32 tx_brdcast;
	u32 tx_mult;
	u32 tx_64;
	u32 tx_65_127;
	u32 tx_128_255;
	u32 tx_256_511;
	u32 tx_512_1023;
	u32 tx_1024_max;
	u32 tx_1523_2047;
	u32 tx_2048_4095;
	u32 tx_4096_8191;
	u32 tx_8192_9728;
	u32 tx_jab;
	u32 tx_drop;
	u32 tx_ovr;
	u32 tx_frag;
	u32 tx_underrun;
	u32 tx_col;
	u32 tx_1_col;
	u32 tx_m_col;
	u32 tx_ex_col;
	u32 tx_late;
	u32 tx_def;
	u32 tx_crs;
	u32 tx_pause;
	u64 rx_gd_octets;
	u32 rx_gd_pkts;
	u32 rx_all_octets;
	u32 rx_all_pkts;
	u32 rx_brdcast;
	u32 rx_unicast;
	u32 rx_mult;
	u32 rx_64;
	u32 rx_65_127;
	u32 rx_128_255;
	u32 rx_256_511;
	u32 rx_512_1023;
	u32 rx_1024_max;
	u32 rx_jab;
	u32 rx_ovr;
	u32 rx_frag;
	u32 rx_drop;
	u32 rx_crc_align;
	u32 rx_und;
	u32 rx_crc;
	u32 rx_align;
	u32 rx_sym;
	u32 rx_pause;
	u32 rx_cntrl;
};


struct bcm_enet_priv {

	/* mac id (from platform device id) */
	int mac_id;

	/* base remapped address of device */
	void __iomem *base;

	/* mac irq, rx_dma irq, tx_dma irq */
	int irq;
	int irq_rx;
	int irq_tx;

	/* hw view of rx & tx dma ring */
	dma_addr_t rx_desc_dma;
	dma_addr_t tx_desc_dma;

	/* allocated size (in bytes) for rx & tx dma ring */
	unsigned int rx_desc_alloc_size;
	unsigned int tx_desc_alloc_size;


	struct napi_struct napi;

	/* dma channel id for rx */
	int rx_chan;

	/* number of dma desc in rx ring */
	int rx_ring_size;

	/* cpu view of rx dma ring */
	struct bcm_enet_desc *rx_desc_cpu;

	/* current number of armed descriptor given to hardware for rx */
	int rx_desc_count;

	/* next rx descriptor to fetch from hardware */
	int rx_curr_desc;

	/* next dirty rx descriptor to refill */
	int rx_dirty_desc;

	/* size of allocated rx skbs */
	unsigned int rx_skb_size;

	/* list of skb given to hw for rx */
	struct sk_buff **rx_skb;

	/* used when rx skb allocation failed, so we defer rx queue
	 * refill */
	struct timer_list rx_timeout;

	/* lock rx_timeout against rx normal operation */
	spinlock_t rx_lock;


	/* dma channel id for tx */
	int tx_chan;

	/* number of dma desc in tx ring */
	int tx_ring_size;

	/* maximum dma burst size */
	int dma_maxburst;

	/* cpu view of rx dma ring */
	struct bcm_enet_desc *tx_desc_cpu;

	/* number of available descriptor for tx */
	int tx_desc_count;

	/* next tx descriptor avaiable */
	int tx_curr_desc;

	/* next dirty tx descriptor to reclaim */
	int tx_dirty_desc;

	/* list of skb given to hw for tx */
	struct sk_buff **tx_skb;

	/* lock used by tx reclaim and xmit */
	spinlock_t tx_lock;


	/* set if internal phy is ignored and external mii interface
	 * is selected */
	int use_external_mii;

	/* set if a phy is connected, phy address must be known,
	 * probing is not possible */
	int has_phy;
	int phy_id;

	/* set if connected phy has an associated irq */
	int has_phy_interrupt;
	int phy_interrupt;

	/* used when a phy is connected (phylib used) */
	struct mii_bus *mii_bus;
	struct phy_device *phydev;
	int old_link;
	int old_duplex;
	int old_pause;

	/* used when no phy is connected */
	int force_speed_100;
	int force_duplex_full;

	/* pause parameters */
	int pause_auto;
	int pause_rx;
	int pause_tx;

	/* stats */
	struct bcm_enet_mib_counters mib;

	/* after mib interrupt, mib registers update is done in this
	 * work queue */
	struct work_struct mib_update_task;

	/* lock mib update between userspace request and workqueue */
	struct mutex mib_update_lock;

	/* mac clock */
	struct clk *mac_clk;

	/* phy clock if internal phy is used */
	struct clk *phy_clk;

	/* network device reference */
	struct net_device *net_dev;

	/* platform device reference */
	struct platform_device *pdev;

	/* maximum hardware transmit/receive size */
	unsigned int hw_mtu;

	bool enet_is_sw;

	/* port mapping for switch devices */
	int num_ports;
	struct bcm63xx_enetsw_port used_ports[ENETSW_MAX_PORT];
	int sw_port_link[ENETSW_MAX_PORT];

	/* used to poll switch port state */
	struct timer_list swphy_poll;
	spinlock_t enetsw_mdio_lock;

	/* dma channel enable mask */
	u32 dma_chan_en_mask;

	/* dma channel interrupt mask */
	u32 dma_chan_int_mask;

	/* DMA engine has internal SRAM */
	bool dma_has_sram;

	/* dma channel width */
	unsigned int dma_chan_width;

	/* dma descriptor shift value */
	unsigned int dma_desc_shift;
};


#endif /* ! BCM63XX_ENET_H_ */

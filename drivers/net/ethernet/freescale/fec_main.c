// SPDX-License-Identifier: GPL-2.0+
/*
 * Fast Ethernet Controller (FEC) driver for Motorola MPC8xx.
 * Copyright (c) 1997 Dan Malek (dmalek@jlc.net)
 *
 * Right now, I am very wasteful with the buffers.  I allocate memory
 * pages and then divide them into 2K frame buffers.  This way I know I
 * have buffers large enough to hold one frame within one buffer descriptor.
 * Once I get this working, I will use 64 or 128 byte CPM buffers, which
 * will be much more memory efficient and will easily handle lots of
 * small packets.
 *
 * Much better multiple PHY support by Magnus Damm.
 * Copyright (c) 2000 Ericsson Radio Systems AB.
 *
 * Support for FEC controller of ColdFire processors.
 * Copyright (c) 2001-2005 Greg Ungerer (gerg@snapgear.com)
 *
 * Bug fixes and cleanup by Philippe De Muyter (phdm@macqel.be)
 * Copyright (c) 2004-2006 Macq Electronique SA.
 *
 * Copyright (C) 2010-2011 Freescale Semiconductor, Inc.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/pm_runtime.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <net/ip.h>
#include <net/selftests.h>
#include <net/tso.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/icmp.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/bitops.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/clk.h>
#include <linux/crc32.h>
#include <linux/platform_device.h>
#include <linux/mdio.h>
#include <linux/phy.h>
#include <linux/fec.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/regulator/consumer.h>
#include <linux/if_vlan.h>
#include <linux/pinctrl/consumer.h>
#include <linux/prefetch.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <soc/imx/cpuidle.h>

#include <asm/cacheflush.h>

#include "fec.h"

static void set_multicast_list(struct net_device *ndev);
static void fec_enet_itr_coal_init(struct net_device *ndev);

#define DRIVER_NAME	"fec"

static const u16 fec_enet_vlan_pri_to_queue[8] = {0, 0, 1, 1, 1, 2, 2, 2};

/* Pause frame feild and FIFO threshold */
#define FEC_ENET_FCE	(1 << 5)
#define FEC_ENET_RSEM_V	0x84
#define FEC_ENET_RSFL_V	16
#define FEC_ENET_RAEM_V	0x8
#define FEC_ENET_RAFL_V	0x8
#define FEC_ENET_OPD_V	0xFFF0
#define FEC_MDIO_PM_TIMEOUT  100 /* ms */

struct fec_devinfo {
	u32 quirks;
};

static const struct fec_devinfo fec_imx25_info = {
	.quirks = FEC_QUIRK_USE_GASKET | FEC_QUIRK_MIB_CLEAR |
		  FEC_QUIRK_HAS_FRREG,
};

static const struct fec_devinfo fec_imx27_info = {
	.quirks = FEC_QUIRK_MIB_CLEAR | FEC_QUIRK_HAS_FRREG,
};

static const struct fec_devinfo fec_imx28_info = {
	.quirks = FEC_QUIRK_ENET_MAC | FEC_QUIRK_SWAP_FRAME |
		  FEC_QUIRK_SINGLE_MDIO | FEC_QUIRK_HAS_RACC |
		  FEC_QUIRK_HAS_FRREG | FEC_QUIRK_CLEAR_SETUP_MII |
		  FEC_QUIRK_NO_HARD_RESET,
};

static const struct fec_devinfo fec_imx6q_info = {
	.quirks = FEC_QUIRK_ENET_MAC | FEC_QUIRK_HAS_GBIT |
		  FEC_QUIRK_HAS_BUFDESC_EX | FEC_QUIRK_HAS_CSUM |
		  FEC_QUIRK_HAS_VLAN | FEC_QUIRK_ERR006358 |
		  FEC_QUIRK_HAS_RACC | FEC_QUIRK_CLEAR_SETUP_MII,
};

static const struct fec_devinfo fec_mvf600_info = {
	.quirks = FEC_QUIRK_ENET_MAC | FEC_QUIRK_HAS_RACC,
};

static const struct fec_devinfo fec_imx6x_info = {
	.quirks = FEC_QUIRK_ENET_MAC | FEC_QUIRK_HAS_GBIT |
		  FEC_QUIRK_HAS_BUFDESC_EX | FEC_QUIRK_HAS_CSUM |
		  FEC_QUIRK_HAS_VLAN | FEC_QUIRK_HAS_AVB |
		  FEC_QUIRK_ERR007885 | FEC_QUIRK_BUG_CAPTURE |
		  FEC_QUIRK_HAS_RACC | FEC_QUIRK_HAS_COALESCE |
		  FEC_QUIRK_CLEAR_SETUP_MII | FEC_QUIRK_HAS_MULTI_QUEUES,
};

static const struct fec_devinfo fec_imx6ul_info = {
	.quirks = FEC_QUIRK_ENET_MAC | FEC_QUIRK_HAS_GBIT |
		  FEC_QUIRK_HAS_BUFDESC_EX | FEC_QUIRK_HAS_CSUM |
		  FEC_QUIRK_HAS_VLAN | FEC_QUIRK_ERR007885 |
		  FEC_QUIRK_BUG_CAPTURE | FEC_QUIRK_HAS_RACC |
		  FEC_QUIRK_HAS_COALESCE | FEC_QUIRK_CLEAR_SETUP_MII,
};

static const struct fec_devinfo fec_imx8mq_info = {
	.quirks = FEC_QUIRK_ENET_MAC | FEC_QUIRK_HAS_GBIT |
		  FEC_QUIRK_HAS_BUFDESC_EX | FEC_QUIRK_HAS_CSUM |
		  FEC_QUIRK_HAS_VLAN | FEC_QUIRK_HAS_AVB |
		  FEC_QUIRK_ERR007885 | FEC_QUIRK_BUG_CAPTURE |
		  FEC_QUIRK_HAS_RACC | FEC_QUIRK_HAS_COALESCE |
		  FEC_QUIRK_CLEAR_SETUP_MII | FEC_QUIRK_HAS_MULTI_QUEUES |
		  FEC_QUIRK_HAS_EEE | FEC_QUIRK_WAKEUP_FROM_INT2,
};

static const struct fec_devinfo fec_imx8qm_info = {
	.quirks = FEC_QUIRK_ENET_MAC | FEC_QUIRK_HAS_GBIT |
		  FEC_QUIRK_HAS_BUFDESC_EX | FEC_QUIRK_HAS_CSUM |
		  FEC_QUIRK_HAS_VLAN | FEC_QUIRK_HAS_AVB |
		  FEC_QUIRK_ERR007885 | FEC_QUIRK_BUG_CAPTURE |
		  FEC_QUIRK_HAS_RACC | FEC_QUIRK_HAS_COALESCE |
		  FEC_QUIRK_CLEAR_SETUP_MII | FEC_QUIRK_HAS_MULTI_QUEUES |
		  FEC_QUIRK_DELAYED_CLKS_SUPPORT,
};

static struct platform_device_id fec_devtype[] = {
	{
		/* keep it for coldfire */
		.name = DRIVER_NAME,
		.driver_data = 0,
	}, {
		.name = "imx25-fec",
		.driver_data = (kernel_ulong_t)&fec_imx25_info,
	}, {
		.name = "imx27-fec",
		.driver_data = (kernel_ulong_t)&fec_imx27_info,
	}, {
		.name = "imx28-fec",
		.driver_data = (kernel_ulong_t)&fec_imx28_info,
	}, {
		.name = "imx6q-fec",
		.driver_data = (kernel_ulong_t)&fec_imx6q_info,
	}, {
		.name = "mvf600-fec",
		.driver_data = (kernel_ulong_t)&fec_mvf600_info,
	}, {
		.name = "imx6sx-fec",
		.driver_data = (kernel_ulong_t)&fec_imx6x_info,
	}, {
		.name = "imx6ul-fec",
		.driver_data = (kernel_ulong_t)&fec_imx6ul_info,
	}, {
		.name = "imx8mq-fec",
		.driver_data = (kernel_ulong_t)&fec_imx8mq_info,
	}, {
		.name = "imx8qm-fec",
		.driver_data = (kernel_ulong_t)&fec_imx8qm_info,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(platform, fec_devtype);

enum imx_fec_type {
	IMX25_FEC = 1,	/* runs on i.mx25/50/53 */
	IMX27_FEC,	/* runs on i.mx27/35/51 */
	IMX28_FEC,
	IMX6Q_FEC,
	MVF600_FEC,
	IMX6SX_FEC,
	IMX6UL_FEC,
	IMX8MQ_FEC,
	IMX8QM_FEC,
};

static const struct of_device_id fec_dt_ids[] = {
	{ .compatible = "fsl,imx25-fec", .data = &fec_devtype[IMX25_FEC], },
	{ .compatible = "fsl,imx27-fec", .data = &fec_devtype[IMX27_FEC], },
	{ .compatible = "fsl,imx28-fec", .data = &fec_devtype[IMX28_FEC], },
	{ .compatible = "fsl,imx6q-fec", .data = &fec_devtype[IMX6Q_FEC], },
	{ .compatible = "fsl,mvf600-fec", .data = &fec_devtype[MVF600_FEC], },
	{ .compatible = "fsl,imx6sx-fec", .data = &fec_devtype[IMX6SX_FEC], },
	{ .compatible = "fsl,imx6ul-fec", .data = &fec_devtype[IMX6UL_FEC], },
	{ .compatible = "fsl,imx8mq-fec", .data = &fec_devtype[IMX8MQ_FEC], },
	{ .compatible = "fsl,imx8qm-fec", .data = &fec_devtype[IMX8QM_FEC], },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, fec_dt_ids);

static unsigned char macaddr[ETH_ALEN];
module_param_array(macaddr, byte, NULL, 0);
MODULE_PARM_DESC(macaddr, "FEC Ethernet MAC address");

#if defined(CONFIG_M5272)
/*
 * Some hardware gets it MAC address out of local flash memory.
 * if this is non-zero then assume it is the address to get MAC from.
 */
#if defined(CONFIG_NETtel)
#define	FEC_FLASHMAC	0xf0006006
#elif defined(CONFIG_GILBARCONAP) || defined(CONFIG_SCALES)
#define	FEC_FLASHMAC	0xf0006000
#elif defined(CONFIG_CANCam)
#define	FEC_FLASHMAC	0xf0020000
#elif defined (CONFIG_M5272C3)
#define	FEC_FLASHMAC	(0xffe04000 + 4)
#elif defined(CONFIG_MOD5272)
#define FEC_FLASHMAC	0xffc0406b
#else
#define	FEC_FLASHMAC	0
#endif
#endif /* CONFIG_M5272 */

/* The FEC stores dest/src/type/vlan, data, and checksum for receive packets.
 *
 * 2048 byte skbufs are allocated. However, alignment requirements
 * varies between FEC variants. Worst case is 64, so round down by 64.
 */
#define PKT_MAXBUF_SIZE		(round_down(2048 - 64, 64))
#define PKT_MINBUF_SIZE		64

/* FEC receive acceleration */
#define FEC_RACC_IPDIS		(1 << 1)
#define FEC_RACC_PRODIS		(1 << 2)
#define FEC_RACC_SHIFT16	BIT(7)
#define FEC_RACC_OPTIONS	(FEC_RACC_IPDIS | FEC_RACC_PRODIS)

/* MIB Control Register */
#define FEC_MIB_CTRLSTAT_DISABLE	BIT(31)

/*
 * The 5270/5271/5280/5282/532x RX control register also contains maximum frame
 * size bits. Other FEC hardware does not, so we need to take that into
 * account when setting it.
 */
#if defined(CONFIG_M523x) || defined(CONFIG_M527x) || defined(CONFIG_M528x) || \
    defined(CONFIG_M520x) || defined(CONFIG_M532x) || defined(CONFIG_ARM) || \
    defined(CONFIG_ARM64)
#define	OPT_FRAME_SIZE	(PKT_MAXBUF_SIZE << 16)
#else
#define	OPT_FRAME_SIZE	0
#endif

/* FEC MII MMFR bits definition */
#define FEC_MMFR_ST		(1 << 30)
#define FEC_MMFR_ST_C45		(0)
#define FEC_MMFR_OP_READ	(2 << 28)
#define FEC_MMFR_OP_READ_C45	(3 << 28)
#define FEC_MMFR_OP_WRITE	(1 << 28)
#define FEC_MMFR_OP_ADDR_WRITE	(0)
#define FEC_MMFR_PA(v)		((v & 0x1f) << 23)
#define FEC_MMFR_RA(v)		((v & 0x1f) << 18)
#define FEC_MMFR_TA		(2 << 16)
#define FEC_MMFR_DATA(v)	(v & 0xffff)
/* FEC ECR bits definition */
#define FEC_ECR_RESET   BIT(0)
#define FEC_ECR_ETHEREN BIT(1)
#define FEC_ECR_MAGICEN BIT(2)
#define FEC_ECR_SLEEP   BIT(3)
#define FEC_ECR_EN1588  BIT(4)

#define FEC_MII_TIMEOUT		30000 /* us */

/* Transmitter timeout */
#define TX_TIMEOUT (2 * HZ)

#define FEC_PAUSE_FLAG_AUTONEG	0x1
#define FEC_PAUSE_FLAG_ENABLE	0x2
#define FEC_WOL_HAS_MAGIC_PACKET	(0x1 << 0)
#define FEC_WOL_FLAG_ENABLE		(0x1 << 1)
#define FEC_WOL_FLAG_SLEEP_ON		(0x1 << 2)

#define COPYBREAK_DEFAULT	256

/* Max number of allowed TCP segments for software TSO */
#define FEC_MAX_TSO_SEGS	100
#define FEC_MAX_SKB_DESCS	(FEC_MAX_TSO_SEGS * 2 + MAX_SKB_FRAGS)

#define IS_TSO_HEADER(txq, addr) \
	((addr >= txq->tso_hdrs_dma) && \
	(addr < txq->tso_hdrs_dma + txq->bd.ring_size * TSO_HEADER_SIZE))

static int mii_cnt;

static struct bufdesc *fec_enet_get_nextdesc(struct bufdesc *bdp,
					     struct bufdesc_prop *bd)
{
	return (bdp >= bd->last) ? bd->base
			: (struct bufdesc *)(((void *)bdp) + bd->dsize);
}

static struct bufdesc *fec_enet_get_prevdesc(struct bufdesc *bdp,
					     struct bufdesc_prop *bd)
{
	return (bdp <= bd->base) ? bd->last
			: (struct bufdesc *)(((void *)bdp) - bd->dsize);
}

static int fec_enet_get_bd_index(struct bufdesc *bdp,
				 struct bufdesc_prop *bd)
{
	return ((const char *)bdp - (const char *)bd->base) >> bd->dsize_log2;
}

static int fec_enet_get_free_txdesc_num(struct fec_enet_priv_tx_q *txq)
{
	int entries;

	entries = (((const char *)txq->dirty_tx -
			(const char *)txq->bd.cur) >> txq->bd.dsize_log2) - 1;

	return entries >= 0 ? entries : entries + txq->bd.ring_size;
}

static void swap_buffer(void *bufaddr, int len)
{
	int i;
	unsigned int *buf = bufaddr;

	for (i = 0; i < len; i += 4, buf++)
		swab32s(buf);
}

static void swap_buffer2(void *dst_buf, void *src_buf, int len)
{
	int i;
	unsigned int *src = src_buf;
	unsigned int *dst = dst_buf;

	for (i = 0; i < len; i += 4, src++, dst++)
		*dst = swab32p(src);
}

static void fec_dump(struct net_device *ndev)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	struct bufdesc *bdp;
	struct fec_enet_priv_tx_q *txq;
	int index = 0;

	netdev_info(ndev, "TX ring dump\n");
	pr_info("Nr     SC     addr       len  SKB\n");

	txq = fep->tx_queue[0];
	bdp = txq->bd.base;

	do {
		pr_info("%3u %c%c 0x%04x 0x%08x %4u %p\n",
			index,
			bdp == txq->bd.cur ? 'S' : ' ',
			bdp == txq->dirty_tx ? 'H' : ' ',
			fec16_to_cpu(bdp->cbd_sc),
			fec32_to_cpu(bdp->cbd_bufaddr),
			fec16_to_cpu(bdp->cbd_datlen),
			txq->tx_skbuff[index]);
		bdp = fec_enet_get_nextdesc(bdp, &txq->bd);
		index++;
	} while (bdp != txq->bd.base);
}

static inline bool is_ipv4_pkt(struct sk_buff *skb)
{
	return skb->protocol == htons(ETH_P_IP) && ip_hdr(skb)->version == 4;
}

static int
fec_enet_clear_csum(struct sk_buff *skb, struct net_device *ndev)
{
	/* Only run for packets requiring a checksum. */
	if (skb->ip_summed != CHECKSUM_PARTIAL)
		return 0;

	if (unlikely(skb_cow_head(skb, 0)))
		return -1;

	if (is_ipv4_pkt(skb))
		ip_hdr(skb)->check = 0;
	*(__sum16 *)(skb->head + skb->csum_start + skb->csum_offset) = 0;

	return 0;
}

static struct bufdesc *
fec_enet_txq_submit_frag_skb(struct fec_enet_priv_tx_q *txq,
			     struct sk_buff *skb,
			     struct net_device *ndev)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	struct bufdesc *bdp = txq->bd.cur;
	struct bufdesc_ex *ebdp;
	int nr_frags = skb_shinfo(skb)->nr_frags;
	int frag, frag_len;
	unsigned short status;
	unsigned int estatus = 0;
	skb_frag_t *this_frag;
	unsigned int index;
	void *bufaddr;
	dma_addr_t addr;
	int i;

	for (frag = 0; frag < nr_frags; frag++) {
		this_frag = &skb_shinfo(skb)->frags[frag];
		bdp = fec_enet_get_nextdesc(bdp, &txq->bd);
		ebdp = (struct bufdesc_ex *)bdp;

		status = fec16_to_cpu(bdp->cbd_sc);
		status &= ~BD_ENET_TX_STATS;
		status |= (BD_ENET_TX_TC | BD_ENET_TX_READY);
		frag_len = skb_frag_size(&skb_shinfo(skb)->frags[frag]);

		/* Handle the last BD specially */
		if (frag == nr_frags - 1) {
			status |= (BD_ENET_TX_INTR | BD_ENET_TX_LAST);
			if (fep->bufdesc_ex) {
				estatus |= BD_ENET_TX_INT;
				if (unlikely(skb_shinfo(skb)->tx_flags &
					SKBTX_HW_TSTAMP && fep->hwts_tx_en))
					estatus |= BD_ENET_TX_TS;
			}
		}

		if (fep->bufdesc_ex) {
			if (fep->quirks & FEC_QUIRK_HAS_AVB)
				estatus |= FEC_TX_BD_FTYPE(txq->bd.qid);
			if (skb->ip_summed == CHECKSUM_PARTIAL)
				estatus |= BD_ENET_TX_PINS | BD_ENET_TX_IINS;

			ebdp->cbd_bdu = 0;
			ebdp->cbd_esc = cpu_to_fec32(estatus);
		}

		bufaddr = skb_frag_address(this_frag);

		index = fec_enet_get_bd_index(bdp, &txq->bd);
		if (((unsigned long) bufaddr) & fep->tx_align ||
			fep->quirks & FEC_QUIRK_SWAP_FRAME) {
			memcpy(txq->tx_bounce[index], bufaddr, frag_len);
			bufaddr = txq->tx_bounce[index];

			if (fep->quirks & FEC_QUIRK_SWAP_FRAME)
				swap_buffer(bufaddr, frag_len);
		}

		addr = dma_map_single(&fep->pdev->dev, bufaddr, frag_len,
				      DMA_TO_DEVICE);
		if (dma_mapping_error(&fep->pdev->dev, addr)) {
			if (net_ratelimit())
				netdev_err(ndev, "Tx DMA memory map failed\n");
			goto dma_mapping_error;
		}

		bdp->cbd_bufaddr = cpu_to_fec32(addr);
		bdp->cbd_datlen = cpu_to_fec16(frag_len);
		/* Make sure the updates to rest of the descriptor are
		 * performed before transferring ownership.
		 */
		wmb();
		bdp->cbd_sc = cpu_to_fec16(status);
	}

	return bdp;
dma_mapping_error:
	bdp = txq->bd.cur;
	for (i = 0; i < frag; i++) {
		bdp = fec_enet_get_nextdesc(bdp, &txq->bd);
		dma_unmap_single(&fep->pdev->dev, fec32_to_cpu(bdp->cbd_bufaddr),
				 fec16_to_cpu(bdp->cbd_datlen), DMA_TO_DEVICE);
	}
	return ERR_PTR(-ENOMEM);
}

static int fec_enet_txq_submit_skb(struct fec_enet_priv_tx_q *txq,
				   struct sk_buff *skb, struct net_device *ndev)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	int nr_frags = skb_shinfo(skb)->nr_frags;
	struct bufdesc *bdp, *last_bdp;
	void *bufaddr;
	dma_addr_t addr;
	unsigned short status;
	unsigned short buflen;
	unsigned int estatus = 0;
	unsigned int index;
	int entries_free;

	entries_free = fec_enet_get_free_txdesc_num(txq);
	if (entries_free < MAX_SKB_FRAGS + 1) {
		dev_kfree_skb_any(skb);
		if (net_ratelimit())
			netdev_err(ndev, "NOT enough BD for SG!\n");
		return NETDEV_TX_OK;
	}

	/* Protocol checksum off-load for TCP and UDP. */
	if (fec_enet_clear_csum(skb, ndev)) {
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	/* Fill in a Tx ring entry */
	bdp = txq->bd.cur;
	last_bdp = bdp;
	status = fec16_to_cpu(bdp->cbd_sc);
	status &= ~BD_ENET_TX_STATS;

	/* Set buffer length and buffer pointer */
	bufaddr = skb->data;
	buflen = skb_headlen(skb);

	index = fec_enet_get_bd_index(bdp, &txq->bd);
	if (((unsigned long) bufaddr) & fep->tx_align ||
		fep->quirks & FEC_QUIRK_SWAP_FRAME) {
		memcpy(txq->tx_bounce[index], skb->data, buflen);
		bufaddr = txq->tx_bounce[index];

		if (fep->quirks & FEC_QUIRK_SWAP_FRAME)
			swap_buffer(bufaddr, buflen);
	}

	/* Push the data cache so the CPM does not get stale memory data. */
	addr = dma_map_single(&fep->pdev->dev, bufaddr, buflen, DMA_TO_DEVICE);
	if (dma_mapping_error(&fep->pdev->dev, addr)) {
		dev_kfree_skb_any(skb);
		if (net_ratelimit())
			netdev_err(ndev, "Tx DMA memory map failed\n");
		return NETDEV_TX_OK;
	}

	if (nr_frags) {
		last_bdp = fec_enet_txq_submit_frag_skb(txq, skb, ndev);
		if (IS_ERR(last_bdp)) {
			dma_unmap_single(&fep->pdev->dev, addr,
					 buflen, DMA_TO_DEVICE);
			dev_kfree_skb_any(skb);
			return NETDEV_TX_OK;
		}
	} else {
		status |= (BD_ENET_TX_INTR | BD_ENET_TX_LAST);
		if (fep->bufdesc_ex) {
			estatus = BD_ENET_TX_INT;
			if (unlikely(skb_shinfo(skb)->tx_flags &
				SKBTX_HW_TSTAMP && fep->hwts_tx_en))
				estatus |= BD_ENET_TX_TS;
		}
	}
	bdp->cbd_bufaddr = cpu_to_fec32(addr);
	bdp->cbd_datlen = cpu_to_fec16(buflen);

	if (fep->bufdesc_ex) {

		struct bufdesc_ex *ebdp = (struct bufdesc_ex *)bdp;

		if (unlikely(skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP &&
			fep->hwts_tx_en))
			skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;

		if (fep->quirks & FEC_QUIRK_HAS_AVB)
			estatus |= FEC_TX_BD_FTYPE(txq->bd.qid);

		if (skb->ip_summed == CHECKSUM_PARTIAL)
			estatus |= BD_ENET_TX_PINS | BD_ENET_TX_IINS;

		ebdp->cbd_bdu = 0;
		ebdp->cbd_esc = cpu_to_fec32(estatus);
	}

	index = fec_enet_get_bd_index(last_bdp, &txq->bd);
	/* Save skb pointer */
	txq->tx_skbuff[index] = skb;

	/* Make sure the updates to rest of the descriptor are performed before
	 * transferring ownership.
	 */
	wmb();

	/* Send it on its way.  Tell FEC it's ready, interrupt when done,
	 * it's the last BD of the frame, and to put the CRC on the end.
	 */
	status |= (BD_ENET_TX_READY | BD_ENET_TX_TC);
	bdp->cbd_sc = cpu_to_fec16(status);

	/* If this was the last BD in the ring, start at the beginning again. */
	bdp = fec_enet_get_nextdesc(last_bdp, &txq->bd);

	skb_tx_timestamp(skb);

	/* Make sure the update to bdp and tx_skbuff are performed before
	 * txq->bd.cur.
	 */
	wmb();
	txq->bd.cur = bdp;

	/* Trigger transmission start */
	writel(0, txq->bd.reg_desc_active);

	return 0;
}

static int
fec_enet_txq_put_data_tso(struct fec_enet_priv_tx_q *txq, struct sk_buff *skb,
			  struct net_device *ndev,
			  struct bufdesc *bdp, int index, char *data,
			  int size, bool last_tcp, bool is_last)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	struct bufdesc_ex *ebdp = container_of(bdp, struct bufdesc_ex, desc);
	unsigned short status;
	unsigned int estatus = 0;
	dma_addr_t addr;

	status = fec16_to_cpu(bdp->cbd_sc);
	status &= ~BD_ENET_TX_STATS;

	status |= (BD_ENET_TX_TC | BD_ENET_TX_READY);

	if (((unsigned long) data) & fep->tx_align ||
		fep->quirks & FEC_QUIRK_SWAP_FRAME) {
		memcpy(txq->tx_bounce[index], data, size);
		data = txq->tx_bounce[index];

		if (fep->quirks & FEC_QUIRK_SWAP_FRAME)
			swap_buffer(data, size);
	}

	addr = dma_map_single(&fep->pdev->dev, data, size, DMA_TO_DEVICE);
	if (dma_mapping_error(&fep->pdev->dev, addr)) {
		dev_kfree_skb_any(skb);
		if (net_ratelimit())
			netdev_err(ndev, "Tx DMA memory map failed\n");
		return NETDEV_TX_BUSY;
	}

	bdp->cbd_datlen = cpu_to_fec16(size);
	bdp->cbd_bufaddr = cpu_to_fec32(addr);

	if (fep->bufdesc_ex) {
		if (fep->quirks & FEC_QUIRK_HAS_AVB)
			estatus |= FEC_TX_BD_FTYPE(txq->bd.qid);
		if (skb->ip_summed == CHECKSUM_PARTIAL)
			estatus |= BD_ENET_TX_PINS | BD_ENET_TX_IINS;
		ebdp->cbd_bdu = 0;
		ebdp->cbd_esc = cpu_to_fec32(estatus);
	}

	/* Handle the last BD specially */
	if (last_tcp)
		status |= (BD_ENET_TX_LAST | BD_ENET_TX_TC);
	if (is_last) {
		status |= BD_ENET_TX_INTR;
		if (fep->bufdesc_ex)
			ebdp->cbd_esc |= cpu_to_fec32(BD_ENET_TX_INT);
	}

	bdp->cbd_sc = cpu_to_fec16(status);

	return 0;
}

static int
fec_enet_txq_put_hdr_tso(struct fec_enet_priv_tx_q *txq,
			 struct sk_buff *skb, struct net_device *ndev,
			 struct bufdesc *bdp, int index)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	int hdr_len = skb_tcp_all_headers(skb);
	struct bufdesc_ex *ebdp = container_of(bdp, struct bufdesc_ex, desc);
	void *bufaddr;
	unsigned long dmabuf;
	unsigned short status;
	unsigned int estatus = 0;

	status = fec16_to_cpu(bdp->cbd_sc);
	status &= ~BD_ENET_TX_STATS;
	status |= (BD_ENET_TX_TC | BD_ENET_TX_READY);

	bufaddr = txq->tso_hdrs + index * TSO_HEADER_SIZE;
	dmabuf = txq->tso_hdrs_dma + index * TSO_HEADER_SIZE;
	if (((unsigned long)bufaddr) & fep->tx_align ||
		fep->quirks & FEC_QUIRK_SWAP_FRAME) {
		memcpy(txq->tx_bounce[index], skb->data, hdr_len);
		bufaddr = txq->tx_bounce[index];

		if (fep->quirks & FEC_QUIRK_SWAP_FRAME)
			swap_buffer(bufaddr, hdr_len);

		dmabuf = dma_map_single(&fep->pdev->dev, bufaddr,
					hdr_len, DMA_TO_DEVICE);
		if (dma_mapping_error(&fep->pdev->dev, dmabuf)) {
			dev_kfree_skb_any(skb);
			if (net_ratelimit())
				netdev_err(ndev, "Tx DMA memory map failed\n");
			return NETDEV_TX_BUSY;
		}
	}

	bdp->cbd_bufaddr = cpu_to_fec32(dmabuf);
	bdp->cbd_datlen = cpu_to_fec16(hdr_len);

	if (fep->bufdesc_ex) {
		if (fep->quirks & FEC_QUIRK_HAS_AVB)
			estatus |= FEC_TX_BD_FTYPE(txq->bd.qid);
		if (skb->ip_summed == CHECKSUM_PARTIAL)
			estatus |= BD_ENET_TX_PINS | BD_ENET_TX_IINS;
		ebdp->cbd_bdu = 0;
		ebdp->cbd_esc = cpu_to_fec32(estatus);
	}

	bdp->cbd_sc = cpu_to_fec16(status);

	return 0;
}

static int fec_enet_txq_submit_tso(struct fec_enet_priv_tx_q *txq,
				   struct sk_buff *skb,
				   struct net_device *ndev)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	int hdr_len, total_len, data_left;
	struct bufdesc *bdp = txq->bd.cur;
	struct tso_t tso;
	unsigned int index = 0;
	int ret;

	if (tso_count_descs(skb) >= fec_enet_get_free_txdesc_num(txq)) {
		dev_kfree_skb_any(skb);
		if (net_ratelimit())
			netdev_err(ndev, "NOT enough BD for TSO!\n");
		return NETDEV_TX_OK;
	}

	/* Protocol checksum off-load for TCP and UDP. */
	if (fec_enet_clear_csum(skb, ndev)) {
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	/* Initialize the TSO handler, and prepare the first payload */
	hdr_len = tso_start(skb, &tso);

	total_len = skb->len - hdr_len;
	while (total_len > 0) {
		char *hdr;

		index = fec_enet_get_bd_index(bdp, &txq->bd);
		data_left = min_t(int, skb_shinfo(skb)->gso_size, total_len);
		total_len -= data_left;

		/* prepare packet headers: MAC + IP + TCP */
		hdr = txq->tso_hdrs + index * TSO_HEADER_SIZE;
		tso_build_hdr(skb, hdr, &tso, data_left, total_len == 0);
		ret = fec_enet_txq_put_hdr_tso(txq, skb, ndev, bdp, index);
		if (ret)
			goto err_release;

		while (data_left > 0) {
			int size;

			size = min_t(int, tso.size, data_left);
			bdp = fec_enet_get_nextdesc(bdp, &txq->bd);
			index = fec_enet_get_bd_index(bdp, &txq->bd);
			ret = fec_enet_txq_put_data_tso(txq, skb, ndev,
							bdp, index,
							tso.data, size,
							size == data_left,
							total_len == 0);
			if (ret)
				goto err_release;

			data_left -= size;
			tso_build_data(skb, &tso, size);
		}

		bdp = fec_enet_get_nextdesc(bdp, &txq->bd);
	}

	/* Save skb pointer */
	txq->tx_skbuff[index] = skb;

	skb_tx_timestamp(skb);
	txq->bd.cur = bdp;

	/* Trigger transmission start */
	if (!(fep->quirks & FEC_QUIRK_ERR007885) ||
	    !readl(txq->bd.reg_desc_active) ||
	    !readl(txq->bd.reg_desc_active) ||
	    !readl(txq->bd.reg_desc_active) ||
	    !readl(txq->bd.reg_desc_active))
		writel(0, txq->bd.reg_desc_active);

	return 0;

err_release:
	/* TODO: Release all used data descriptors for TSO */
	return ret;
}

static netdev_tx_t
fec_enet_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	int entries_free;
	unsigned short queue;
	struct fec_enet_priv_tx_q *txq;
	struct netdev_queue *nq;
	int ret;

	queue = skb_get_queue_mapping(skb);
	txq = fep->tx_queue[queue];
	nq = netdev_get_tx_queue(ndev, queue);

	if (skb_is_gso(skb))
		ret = fec_enet_txq_submit_tso(txq, skb, ndev);
	else
		ret = fec_enet_txq_submit_skb(txq, skb, ndev);
	if (ret)
		return ret;

	entries_free = fec_enet_get_free_txdesc_num(txq);
	if (entries_free <= txq->tx_stop_threshold)
		netif_tx_stop_queue(nq);

	return NETDEV_TX_OK;
}

/* Init RX & TX buffer descriptors
 */
static void fec_enet_bd_init(struct net_device *dev)
{
	struct fec_enet_private *fep = netdev_priv(dev);
	struct fec_enet_priv_tx_q *txq;
	struct fec_enet_priv_rx_q *rxq;
	struct bufdesc *bdp;
	unsigned int i;
	unsigned int q;

	for (q = 0; q < fep->num_rx_queues; q++) {
		/* Initialize the receive buffer descriptors. */
		rxq = fep->rx_queue[q];
		bdp = rxq->bd.base;

		for (i = 0; i < rxq->bd.ring_size; i++) {

			/* Initialize the BD for every fragment in the page. */
			if (bdp->cbd_bufaddr)
				bdp->cbd_sc = cpu_to_fec16(BD_ENET_RX_EMPTY);
			else
				bdp->cbd_sc = cpu_to_fec16(0);
			bdp = fec_enet_get_nextdesc(bdp, &rxq->bd);
		}

		/* Set the last buffer to wrap */
		bdp = fec_enet_get_prevdesc(bdp, &rxq->bd);
		bdp->cbd_sc |= cpu_to_fec16(BD_SC_WRAP);

		rxq->bd.cur = rxq->bd.base;
	}

	for (q = 0; q < fep->num_tx_queues; q++) {
		/* ...and the same for transmit */
		txq = fep->tx_queue[q];
		bdp = txq->bd.base;
		txq->bd.cur = bdp;

		for (i = 0; i < txq->bd.ring_size; i++) {
			/* Initialize the BD for every fragment in the page. */
			bdp->cbd_sc = cpu_to_fec16(0);
			if (bdp->cbd_bufaddr &&
			    !IS_TSO_HEADER(txq, fec32_to_cpu(bdp->cbd_bufaddr)))
				dma_unmap_single(&fep->pdev->dev,
						 fec32_to_cpu(bdp->cbd_bufaddr),
						 fec16_to_cpu(bdp->cbd_datlen),
						 DMA_TO_DEVICE);
			if (txq->tx_skbuff[i]) {
				dev_kfree_skb_any(txq->tx_skbuff[i]);
				txq->tx_skbuff[i] = NULL;
			}
			bdp->cbd_bufaddr = cpu_to_fec32(0);
			bdp = fec_enet_get_nextdesc(bdp, &txq->bd);
		}

		/* Set the last buffer to wrap */
		bdp = fec_enet_get_prevdesc(bdp, &txq->bd);
		bdp->cbd_sc |= cpu_to_fec16(BD_SC_WRAP);
		txq->dirty_tx = bdp;
	}
}

static void fec_enet_active_rxring(struct net_device *ndev)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	int i;

	for (i = 0; i < fep->num_rx_queues; i++)
		writel(0, fep->rx_queue[i]->bd.reg_desc_active);
}

static void fec_enet_enable_ring(struct net_device *ndev)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	struct fec_enet_priv_tx_q *txq;
	struct fec_enet_priv_rx_q *rxq;
	int i;

	for (i = 0; i < fep->num_rx_queues; i++) {
		rxq = fep->rx_queue[i];
		writel(rxq->bd.dma, fep->hwp + FEC_R_DES_START(i));
		writel(PKT_MAXBUF_SIZE, fep->hwp + FEC_R_BUFF_SIZE(i));

		/* enable DMA1/2 */
		if (i)
			writel(RCMR_MATCHEN | RCMR_CMP(i),
			       fep->hwp + FEC_RCMR(i));
	}

	for (i = 0; i < fep->num_tx_queues; i++) {
		txq = fep->tx_queue[i];
		writel(txq->bd.dma, fep->hwp + FEC_X_DES_START(i));

		/* enable DMA1/2 */
		if (i)
			writel(DMA_CLASS_EN | IDLE_SLOPE(i),
			       fep->hwp + FEC_DMA_CFG(i));
	}
}

static void fec_enet_reset_skb(struct net_device *ndev)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	struct fec_enet_priv_tx_q *txq;
	int i, j;

	for (i = 0; i < fep->num_tx_queues; i++) {
		txq = fep->tx_queue[i];

		for (j = 0; j < txq->bd.ring_size; j++) {
			if (txq->tx_skbuff[j]) {
				dev_kfree_skb_any(txq->tx_skbuff[j]);
				txq->tx_skbuff[j] = NULL;
			}
		}
	}
}

/*
 * This function is called to start or restart the FEC during a link
 * change, transmit timeout, or to reconfigure the FEC.  The network
 * packet processing for this device must be stopped before this call.
 */
static void
fec_restart(struct net_device *ndev)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	u32 temp_mac[2];
	u32 rcntl = OPT_FRAME_SIZE | 0x04;
	u32 ecntl = 0x2; /* ETHEREN */
	struct ptp_clock_request ptp_rq = { .type = PTP_CLK_REQ_PPS };

	fec_ptp_save_state(fep);

	/* Whack a reset.  We should wait for this.
	 * For i.MX6SX SOC, enet use AXI bus, we use disable MAC
	 * instead of reset MAC itself.
	 */
	if (fep->quirks & FEC_QUIRK_HAS_MULTI_QUEUES ||
	    ((fep->quirks & FEC_QUIRK_NO_HARD_RESET) && fep->link)) {
		writel(0, fep->hwp + FEC_ECNTRL);
	} else {
		writel(1, fep->hwp + FEC_ECNTRL);
		udelay(10);
	}

	/*
	 * enet-mac reset will reset mac address registers too,
	 * so need to reconfigure it.
	 */
	memcpy(&temp_mac, ndev->dev_addr, ETH_ALEN);
	writel((__force u32)cpu_to_be32(temp_mac[0]),
	       fep->hwp + FEC_ADDR_LOW);
	writel((__force u32)cpu_to_be32(temp_mac[1]),
	       fep->hwp + FEC_ADDR_HIGH);

	/* Clear any outstanding interrupt, except MDIO. */
	writel((0xffffffff & ~FEC_ENET_MII), fep->hwp + FEC_IEVENT);

	fec_enet_bd_init(ndev);

	fec_enet_enable_ring(ndev);

	/* Reset tx SKB buffers. */
	fec_enet_reset_skb(ndev);

	/* Enable MII mode */
	if (fep->full_duplex == DUPLEX_FULL) {
		/* FD enable */
		writel(0x04, fep->hwp + FEC_X_CNTRL);
	} else {
		/* No Rcv on Xmit */
		rcntl |= 0x02;
		writel(0x0, fep->hwp + FEC_X_CNTRL);
	}

	/* Set MII speed */
	writel(fep->phy_speed, fep->hwp + FEC_MII_SPEED);

#if !defined(CONFIG_M5272)
	if (fep->quirks & FEC_QUIRK_HAS_RACC) {
		u32 val = readl(fep->hwp + FEC_RACC);

		/* align IP header */
		val |= FEC_RACC_SHIFT16;
		if (fep->csum_flags & FLAG_RX_CSUM_ENABLED)
			/* set RX checksum */
			val |= FEC_RACC_OPTIONS;
		else
			val &= ~FEC_RACC_OPTIONS;
		writel(val, fep->hwp + FEC_RACC);
		writel(PKT_MAXBUF_SIZE, fep->hwp + FEC_FTRL);
	}
#endif

	/*
	 * The phy interface and speed need to get configured
	 * differently on enet-mac.
	 */
	if (fep->quirks & FEC_QUIRK_ENET_MAC) {
		/* Enable flow control and length check */
		rcntl |= 0x40000000 | 0x00000020;

		/* RGMII, RMII or MII */
		if (fep->phy_interface == PHY_INTERFACE_MODE_RGMII ||
		    fep->phy_interface == PHY_INTERFACE_MODE_RGMII_ID ||
		    fep->phy_interface == PHY_INTERFACE_MODE_RGMII_RXID ||
		    fep->phy_interface == PHY_INTERFACE_MODE_RGMII_TXID)
			rcntl |= (1 << 6);
		else if (fep->phy_interface == PHY_INTERFACE_MODE_RMII)
			rcntl |= (1 << 8);
		else
			rcntl &= ~(1 << 8);

		/* 1G, 100M or 10M */
		if (ndev->phydev) {
			if (ndev->phydev->speed == SPEED_1000)
				ecntl |= (1 << 5);
			else if (ndev->phydev->speed == SPEED_100)
				rcntl &= ~(1 << 9);
			else
				rcntl |= (1 << 9);
		}
	} else {
#ifdef FEC_MIIGSK_ENR
		if (fep->quirks & FEC_QUIRK_USE_GASKET) {
			u32 cfgr;
			/* disable the gasket and wait */
			writel(0, fep->hwp + FEC_MIIGSK_ENR);
			while (readl(fep->hwp + FEC_MIIGSK_ENR) & 4)
				udelay(1);

			/*
			 * configure the gasket:
			 *   RMII, 50 MHz, no loopback, no echo
			 *   MII, 25 MHz, no loopback, no echo
			 */
			cfgr = (fep->phy_interface == PHY_INTERFACE_MODE_RMII)
				? BM_MIIGSK_CFGR_RMII : BM_MIIGSK_CFGR_MII;
			if (ndev->phydev && ndev->phydev->speed == SPEED_10)
				cfgr |= BM_MIIGSK_CFGR_FRCONT_10M;
			writel(cfgr, fep->hwp + FEC_MIIGSK_CFGR);

			/* re-enable the gasket */
			writel(2, fep->hwp + FEC_MIIGSK_ENR);
		}
#endif
	}

#if !defined(CONFIG_M5272)
	/* enable pause frame*/
	if ((fep->pause_flag & FEC_PAUSE_FLAG_ENABLE) ||
	    ((fep->pause_flag & FEC_PAUSE_FLAG_AUTONEG) &&
	     ndev->phydev && ndev->phydev->pause)) {
		rcntl |= FEC_ENET_FCE;

		/* set FIFO threshold parameter to reduce overrun */
		writel(FEC_ENET_RSEM_V, fep->hwp + FEC_R_FIFO_RSEM);
		writel(FEC_ENET_RSFL_V, fep->hwp + FEC_R_FIFO_RSFL);
		writel(FEC_ENET_RAEM_V, fep->hwp + FEC_R_FIFO_RAEM);
		writel(FEC_ENET_RAFL_V, fep->hwp + FEC_R_FIFO_RAFL);

		/* OPD */
		writel(FEC_ENET_OPD_V, fep->hwp + FEC_OPD);
	} else {
		rcntl &= ~FEC_ENET_FCE;
	}
#endif /* !defined(CONFIG_M5272) */

	writel(rcntl, fep->hwp + FEC_R_CNTRL);

	/* Setup multicast filter. */
	set_multicast_list(ndev);
#ifndef CONFIG_M5272
	writel(0, fep->hwp + FEC_HASH_TABLE_HIGH);
	writel(0, fep->hwp + FEC_HASH_TABLE_LOW);
#endif

	if (fep->quirks & FEC_QUIRK_ENET_MAC) {
		/* enable ENET endian swap */
		ecntl |= (1 << 8);
		/* enable ENET store and forward mode */
		writel(1 << 8, fep->hwp + FEC_X_WMRK);
	}

	if (fep->bufdesc_ex)
		ecntl |= FEC_ECR_EN1588;

	if (fep->quirks & FEC_QUIRK_DELAYED_CLKS_SUPPORT &&
	    fep->rgmii_txc_dly)
		ecntl |= FEC_ENET_TXC_DLY;
	if (fep->quirks & FEC_QUIRK_DELAYED_CLKS_SUPPORT &&
	    fep->rgmii_rxc_dly)
		ecntl |= FEC_ENET_RXC_DLY;

#ifndef CONFIG_M5272
	/* Enable the MIB statistic event counters */
	writel(0 << 31, fep->hwp + FEC_MIB_CTRLSTAT);
#endif

	/* And last, enable the transmit and receive processing */
	writel(ecntl, fep->hwp + FEC_ECNTRL);
	fec_enet_active_rxring(ndev);

	if (fep->bufdesc_ex)
		fec_ptp_start_cyclecounter(ndev);

	/* Restart PPS if needed */
	if (fep->pps_enable) {
		/* Clear flag so fec_ptp_enable_pps() doesn't return immediately */
		fep->pps_enable = 0;
		fec_ptp_restore_state(fep);
		fep->ptp_caps.enable(&fep->ptp_caps, &ptp_rq, 1);
	}

	/* Enable interrupts we wish to service */
	if (fep->link)
		writel(FEC_DEFAULT_IMASK, fep->hwp + FEC_IMASK);
	else
		writel(0, fep->hwp + FEC_IMASK);

	/* Init the interrupt coalescing */
	fec_enet_itr_coal_init(ndev);

}

static void fec_enet_stop_mode(struct fec_enet_private *fep, bool enabled)
{
	struct fec_platform_data *pdata = fep->pdev->dev.platform_data;
	struct fec_stop_mode_gpr *stop_gpr = &fep->stop_gpr;

	if (stop_gpr->gpr) {
		if (enabled)
			regmap_update_bits(stop_gpr->gpr, stop_gpr->reg,
					   BIT(stop_gpr->bit),
					   BIT(stop_gpr->bit));
		else
			regmap_update_bits(stop_gpr->gpr, stop_gpr->reg,
					   BIT(stop_gpr->bit), 0);
	} else if (pdata && pdata->sleep_mode_enable) {
		pdata->sleep_mode_enable(enabled);
	}
}

static void fec_irqs_disable(struct net_device *ndev)
{
	struct fec_enet_private *fep = netdev_priv(ndev);

	writel(0, fep->hwp + FEC_IMASK);
}

static void fec_irqs_disable_except_wakeup(struct net_device *ndev)
{
	struct fec_enet_private *fep = netdev_priv(ndev);

	writel(0, fep->hwp + FEC_IMASK);
	writel(FEC_ENET_WAKEUP, fep->hwp + FEC_IMASK);
}

static void
fec_stop(struct net_device *ndev)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	u32 rmii_mode = readl(fep->hwp + FEC_R_CNTRL) & (1 << 8);
	u32 val;
	struct ptp_clock_request ptp_rq = { .type = PTP_CLK_REQ_PPS };
	u32 ecntl = 0;

	/* We cannot expect a graceful transmit stop without link !!! */
	if (fep->link) {
		writel(1, fep->hwp + FEC_X_CNTRL); /* Graceful transmit stop */
		udelay(10);
		if (!(readl(fep->hwp + FEC_IEVENT) & FEC_ENET_GRA))
			netdev_err(ndev, "Graceful transmit stop did not complete!\n");
	}

	fec_ptp_save_state(fep);

	/* Whack a reset.  We should wait for this.
	 * For i.MX6SX SOC, enet use AXI bus, we use disable MAC
	 * instead of reset MAC itself.
	 */
	if (!(fep->wol_flag & FEC_WOL_FLAG_SLEEP_ON)) {
		if (fep->quirks & FEC_QUIRK_HAS_MULTI_QUEUES) {
			writel(0, fep->hwp + FEC_ECNTRL);
		} else {
			writel(1, fep->hwp + FEC_ECNTRL);
			udelay(10);
		}
	} else {
		val = readl(fep->hwp + FEC_ECNTRL);
		val |= (FEC_ECR_MAGICEN | FEC_ECR_SLEEP);
		writel(val, fep->hwp + FEC_ECNTRL);
	}
	writel(fep->phy_speed, fep->hwp + FEC_MII_SPEED);
	writel(FEC_DEFAULT_IMASK, fep->hwp + FEC_IMASK);

	if (fep->bufdesc_ex)
		ecntl |= FEC_ECR_EN1588;

	/* We have to keep ENET enabled to have MII interrupt stay working */
	if (fep->quirks & FEC_QUIRK_ENET_MAC &&
		!(fep->wol_flag & FEC_WOL_FLAG_SLEEP_ON)) {
		ecntl |= FEC_ECR_ETHEREN;
		writel(rmii_mode, fep->hwp + FEC_R_CNTRL);
	}

	writel(ecntl, fep->hwp + FEC_ECNTRL);

	if (fep->bufdesc_ex)
		fec_ptp_start_cyclecounter(ndev);

	/* Restart PPS if needed */
	if (fep->pps_enable) {
		/* Clear flag so fec_ptp_enable_pps() doesn't return immediately */
		fep->pps_enable = 0;
		fec_ptp_restore_state(fep);
		fep->ptp_caps.enable(&fep->ptp_caps, &ptp_rq, 1);
	}
}


static void
fec_timeout(struct net_device *ndev, unsigned int txqueue)
{
	struct fec_enet_private *fep = netdev_priv(ndev);

	fec_dump(ndev);

	ndev->stats.tx_errors++;

	schedule_work(&fep->tx_timeout_work);
}

static void fec_enet_timeout_work(struct work_struct *work)
{
	struct fec_enet_private *fep =
		container_of(work, struct fec_enet_private, tx_timeout_work);
	struct net_device *ndev = fep->netdev;

	rtnl_lock();
	if (netif_device_present(ndev) || netif_running(ndev)) {
		napi_disable(&fep->napi);
		netif_tx_lock_bh(ndev);
		fec_restart(ndev);
		netif_tx_wake_all_queues(ndev);
		netif_tx_unlock_bh(ndev);
		napi_enable(&fep->napi);
	}
	rtnl_unlock();
}

static void
fec_enet_hwtstamp(struct fec_enet_private *fep, unsigned ts,
	struct skb_shared_hwtstamps *hwtstamps)
{
	unsigned long flags;
	u64 ns;

	spin_lock_irqsave(&fep->tmreg_lock, flags);
	ns = timecounter_cyc2time(&fep->tc, ts);
	spin_unlock_irqrestore(&fep->tmreg_lock, flags);

	memset(hwtstamps, 0, sizeof(*hwtstamps));
	hwtstamps->hwtstamp = ns_to_ktime(ns);
}

static void
fec_enet_tx_queue(struct net_device *ndev, u16 queue_id)
{
	struct	fec_enet_private *fep;
	struct bufdesc *bdp;
	unsigned short status;
	struct	sk_buff	*skb;
	struct fec_enet_priv_tx_q *txq;
	struct netdev_queue *nq;
	int	index = 0;
	int	entries_free;

	fep = netdev_priv(ndev);

	txq = fep->tx_queue[queue_id];
	/* get next bdp of dirty_tx */
	nq = netdev_get_tx_queue(ndev, queue_id);
	bdp = txq->dirty_tx;

	/* get next bdp of dirty_tx */
	bdp = fec_enet_get_nextdesc(bdp, &txq->bd);

	while (bdp != READ_ONCE(txq->bd.cur)) {
		/* Order the load of bd.cur and cbd_sc */
		rmb();
		status = fec16_to_cpu(READ_ONCE(bdp->cbd_sc));
		if (status & BD_ENET_TX_READY)
			break;

		index = fec_enet_get_bd_index(bdp, &txq->bd);

		skb = txq->tx_skbuff[index];
		txq->tx_skbuff[index] = NULL;
		if (!IS_TSO_HEADER(txq, fec32_to_cpu(bdp->cbd_bufaddr)))
			dma_unmap_single(&fep->pdev->dev,
					 fec32_to_cpu(bdp->cbd_bufaddr),
					 fec16_to_cpu(bdp->cbd_datlen),
					 DMA_TO_DEVICE);
		bdp->cbd_bufaddr = cpu_to_fec32(0);
		if (!skb)
			goto skb_done;

		/* Check for errors. */
		if (status & (BD_ENET_TX_HB | BD_ENET_TX_LC |
				   BD_ENET_TX_RL | BD_ENET_TX_UN |
				   BD_ENET_TX_CSL)) {
			ndev->stats.tx_errors++;
			if (status & BD_ENET_TX_HB)  /* No heartbeat */
				ndev->stats.tx_heartbeat_errors++;
			if (status & BD_ENET_TX_LC)  /* Late collision */
				ndev->stats.tx_window_errors++;
			if (status & BD_ENET_TX_RL)  /* Retrans limit */
				ndev->stats.tx_aborted_errors++;
			if (status & BD_ENET_TX_UN)  /* Underrun */
				ndev->stats.tx_fifo_errors++;
			if (status & BD_ENET_TX_CSL) /* Carrier lost */
				ndev->stats.tx_carrier_errors++;
		} else {
			ndev->stats.tx_packets++;
			ndev->stats.tx_bytes += skb->len;
		}

		/* NOTE: SKBTX_IN_PROGRESS being set does not imply it's we who
		 * are to time stamp the packet, so we still need to check time
		 * stamping enabled flag.
		 */
		if (unlikely(skb_shinfo(skb)->tx_flags & SKBTX_IN_PROGRESS &&
			     fep->hwts_tx_en) &&
		    fep->bufdesc_ex) {
			struct skb_shared_hwtstamps shhwtstamps;
			struct bufdesc_ex *ebdp = (struct bufdesc_ex *)bdp;

			fec_enet_hwtstamp(fep, fec32_to_cpu(ebdp->ts), &shhwtstamps);
			skb_tstamp_tx(skb, &shhwtstamps);
		}

		/* Deferred means some collisions occurred during transmit,
		 * but we eventually sent the packet OK.
		 */
		if (status & BD_ENET_TX_DEF)
			ndev->stats.collisions++;

		/* Free the sk buffer associated with this last transmit */
		dev_kfree_skb_any(skb);
skb_done:
		/* Make sure the update to bdp and tx_skbuff are performed
		 * before dirty_tx
		 */
		wmb();
		txq->dirty_tx = bdp;

		/* Update pointer to next buffer descriptor to be transmitted */
		bdp = fec_enet_get_nextdesc(bdp, &txq->bd);

		/* Since we have freed up a buffer, the ring is no longer full
		 */
		if (netif_tx_queue_stopped(nq)) {
			entries_free = fec_enet_get_free_txdesc_num(txq);
			if (entries_free >= txq->tx_wake_threshold)
				netif_tx_wake_queue(nq);
		}
	}

	/* ERR006358: Keep the transmitter going */
	if (bdp != txq->bd.cur &&
	    readl(txq->bd.reg_desc_active) == 0)
		writel(0, txq->bd.reg_desc_active);
}

static void fec_enet_tx(struct net_device *ndev)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	int i;

	/* Make sure that AVB queues are processed first. */
	for (i = fep->num_tx_queues - 1; i >= 0; i--)
		fec_enet_tx_queue(ndev, i);
}

static int
fec_enet_new_rxbdp(struct net_device *ndev, struct bufdesc *bdp, struct sk_buff *skb)
{
	struct  fec_enet_private *fep = netdev_priv(ndev);
	int off;

	off = ((unsigned long)skb->data) & fep->rx_align;
	if (off)
		skb_reserve(skb, fep->rx_align + 1 - off);

	bdp->cbd_bufaddr = cpu_to_fec32(dma_map_single(&fep->pdev->dev, skb->data, FEC_ENET_RX_FRSIZE - fep->rx_align, DMA_FROM_DEVICE));
	if (dma_mapping_error(&fep->pdev->dev, fec32_to_cpu(bdp->cbd_bufaddr))) {
		if (net_ratelimit())
			netdev_err(ndev, "Rx DMA memory map failed\n");
		return -ENOMEM;
	}

	return 0;
}

static bool fec_enet_copybreak(struct net_device *ndev, struct sk_buff **skb,
			       struct bufdesc *bdp, u32 length, bool swap)
{
	struct  fec_enet_private *fep = netdev_priv(ndev);
	struct sk_buff *new_skb;

	if (length > fep->rx_copybreak)
		return false;

	new_skb = netdev_alloc_skb(ndev, length);
	if (!new_skb)
		return false;

	dma_sync_single_for_cpu(&fep->pdev->dev,
				fec32_to_cpu(bdp->cbd_bufaddr),
				FEC_ENET_RX_FRSIZE - fep->rx_align,
				DMA_FROM_DEVICE);
	if (!swap)
		memcpy(new_skb->data, (*skb)->data, length);
	else
		swap_buffer2(new_skb->data, (*skb)->data, length);
	*skb = new_skb;

	return true;
}

/* During a receive, the bd_rx.cur points to the current incoming buffer.
 * When we update through the ring, if the next incoming buffer has
 * not been given to the system, we just set the empty indicator,
 * effectively tossing the packet.
 */
static int
fec_enet_rx_queue(struct net_device *ndev, int budget, u16 queue_id)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	struct fec_enet_priv_rx_q *rxq;
	struct bufdesc *bdp;
	unsigned short status;
	struct  sk_buff *skb_new = NULL;
	struct  sk_buff *skb;
	ushort	pkt_len;
	__u8 *data;
	int	pkt_received = 0;
	struct	bufdesc_ex *ebdp = NULL;
	bool	vlan_packet_rcvd = false;
	u16	vlan_tag;
	int	index = 0;
	bool	is_copybreak;
	bool	need_swap = fep->quirks & FEC_QUIRK_SWAP_FRAME;

#ifdef CONFIG_M532x
	flush_cache_all();
#endif
	rxq = fep->rx_queue[queue_id];

	/* First, grab all of the stats for the incoming packet.
	 * These get messed up if we get called due to a busy condition.
	 */
	bdp = rxq->bd.cur;

	while (!((status = fec16_to_cpu(bdp->cbd_sc)) & BD_ENET_RX_EMPTY)) {

		if (pkt_received >= budget)
			break;
		pkt_received++;

		writel(FEC_ENET_RXF_GET(queue_id), fep->hwp + FEC_IEVENT);

		/* Check for errors. */
		status ^= BD_ENET_RX_LAST;
		if (status & (BD_ENET_RX_LG | BD_ENET_RX_SH | BD_ENET_RX_NO |
			   BD_ENET_RX_CR | BD_ENET_RX_OV | BD_ENET_RX_LAST |
			   BD_ENET_RX_CL)) {
			ndev->stats.rx_errors++;
			if (status & BD_ENET_RX_OV) {
				/* FIFO overrun */
				ndev->stats.rx_fifo_errors++;
				goto rx_processing_done;
			}
			if (status & (BD_ENET_RX_LG | BD_ENET_RX_SH
						| BD_ENET_RX_LAST)) {
				/* Frame too long or too short. */
				ndev->stats.rx_length_errors++;
				if (status & BD_ENET_RX_LAST)
					netdev_err(ndev, "rcv is not +last\n");
			}
			if (status & BD_ENET_RX_CR)	/* CRC Error */
				ndev->stats.rx_crc_errors++;
			/* Report late collisions as a frame error. */
			if (status & (BD_ENET_RX_NO | BD_ENET_RX_CL))
				ndev->stats.rx_frame_errors++;
			goto rx_processing_done;
		}

		/* Process the incoming frame. */
		ndev->stats.rx_packets++;
		pkt_len = fec16_to_cpu(bdp->cbd_datlen);
		ndev->stats.rx_bytes += pkt_len;

		index = fec_enet_get_bd_index(bdp, &rxq->bd);
		skb = rxq->rx_skbuff[index];

		/* The packet length includes FCS, but we don't want to
		 * include that when passing upstream as it messes up
		 * bridging applications.
		 */
		is_copybreak = fec_enet_copybreak(ndev, &skb, bdp, pkt_len - 4,
						  need_swap);
		if (!is_copybreak) {
			skb_new = netdev_alloc_skb(ndev, FEC_ENET_RX_FRSIZE);
			if (unlikely(!skb_new)) {
				ndev->stats.rx_dropped++;
				goto rx_processing_done;
			}
			dma_unmap_single(&fep->pdev->dev,
					 fec32_to_cpu(bdp->cbd_bufaddr),
					 FEC_ENET_RX_FRSIZE - fep->rx_align,
					 DMA_FROM_DEVICE);
		}

		prefetch(skb->data - NET_IP_ALIGN);
		skb_put(skb, pkt_len - 4);
		data = skb->data;

		if (!is_copybreak && need_swap)
			swap_buffer(data, pkt_len);

#if !defined(CONFIG_M5272)
		if (fep->quirks & FEC_QUIRK_HAS_RACC)
			data = skb_pull_inline(skb, 2);
#endif

		/* Extract the enhanced buffer descriptor */
		ebdp = NULL;
		if (fep->bufdesc_ex)
			ebdp = (struct bufdesc_ex *)bdp;

		/* If this is a VLAN packet remove the VLAN Tag */
		vlan_packet_rcvd = false;
		if ((ndev->features & NETIF_F_HW_VLAN_CTAG_RX) &&
		    fep->bufdesc_ex &&
		    (ebdp->cbd_esc & cpu_to_fec32(BD_ENET_RX_VLAN))) {
			/* Push and remove the vlan tag */
			struct vlan_hdr *vlan_header =
					(struct vlan_hdr *) (data + ETH_HLEN);
			vlan_tag = ntohs(vlan_header->h_vlan_TCI);

			vlan_packet_rcvd = true;

			memmove(skb->data + VLAN_HLEN, data, ETH_ALEN * 2);
			skb_pull(skb, VLAN_HLEN);
		}

		skb->protocol = eth_type_trans(skb, ndev);

		/* Get receive timestamp from the skb */
		if (fep->hwts_rx_en && fep->bufdesc_ex)
			fec_enet_hwtstamp(fep, fec32_to_cpu(ebdp->ts),
					  skb_hwtstamps(skb));

		if (fep->bufdesc_ex &&
		    (fep->csum_flags & FLAG_RX_CSUM_ENABLED)) {
			if (!(ebdp->cbd_esc & cpu_to_fec32(FLAG_RX_CSUM_ERROR))) {
				/* don't check it */
				skb->ip_summed = CHECKSUM_UNNECESSARY;
			} else {
				skb_checksum_none_assert(skb);
			}
		}

		/* Handle received VLAN packets */
		if (vlan_packet_rcvd)
			__vlan_hwaccel_put_tag(skb,
					       htons(ETH_P_8021Q),
					       vlan_tag);

		skb_record_rx_queue(skb, queue_id);
		napi_gro_receive(&fep->napi, skb);

		if (is_copybreak) {
			dma_sync_single_for_device(&fep->pdev->dev,
						   fec32_to_cpu(bdp->cbd_bufaddr),
						   FEC_ENET_RX_FRSIZE - fep->rx_align,
						   DMA_FROM_DEVICE);
		} else {
			rxq->rx_skbuff[index] = skb_new;
			fec_enet_new_rxbdp(ndev, bdp, skb_new);
		}

rx_processing_done:
		/* Clear the status flags for this buffer */
		status &= ~BD_ENET_RX_STATS;

		/* Mark the buffer empty */
		status |= BD_ENET_RX_EMPTY;

		if (fep->bufdesc_ex) {
			struct bufdesc_ex *ebdp = (struct bufdesc_ex *)bdp;

			ebdp->cbd_esc = cpu_to_fec32(BD_ENET_RX_INT);
			ebdp->cbd_prot = 0;
			ebdp->cbd_bdu = 0;
		}
		/* Make sure the updates to rest of the descriptor are
		 * performed before transferring ownership.
		 */
		wmb();
		bdp->cbd_sc = cpu_to_fec16(status);

		/* Update BD pointer to next entry */
		bdp = fec_enet_get_nextdesc(bdp, &rxq->bd);

		/* Doing this here will keep the FEC running while we process
		 * incoming frames.  On a heavily loaded network, we should be
		 * able to keep up at the expense of system resources.
		 */
		writel(0, rxq->bd.reg_desc_active);
	}
	rxq->bd.cur = bdp;
	return pkt_received;
}

static int fec_enet_rx(struct net_device *ndev, int budget)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	int i, done = 0;

	/* Make sure that AVB queues are processed first. */
	for (i = fep->num_rx_queues - 1; i >= 0; i--)
		done += fec_enet_rx_queue(ndev, budget - done, i);

	return done;
}

static bool fec_enet_collect_events(struct fec_enet_private *fep)
{
	uint int_events;

	int_events = readl(fep->hwp + FEC_IEVENT);

	/* Don't clear MDIO events, we poll for those */
	int_events &= ~FEC_ENET_MII;

	writel(int_events, fep->hwp + FEC_IEVENT);

	return int_events != 0;
}

static irqreturn_t
fec_enet_interrupt(int irq, void *dev_id)
{
	struct net_device *ndev = dev_id;
	struct fec_enet_private *fep = netdev_priv(ndev);
	irqreturn_t ret = IRQ_NONE;

	if (fec_enet_collect_events(fep) && fep->link) {
		ret = IRQ_HANDLED;

		if (napi_schedule_prep(&fep->napi)) {
			/* Disable interrupts */
			writel(0, fep->hwp + FEC_IMASK);
			__napi_schedule(&fep->napi);
		}
	}

	return ret;
}

static int fec_enet_rx_napi(struct napi_struct *napi, int budget)
{
	struct net_device *ndev = napi->dev;
	struct fec_enet_private *fep = netdev_priv(ndev);
	int done = 0;

	do {
		done += fec_enet_rx(ndev, budget - done);
		fec_enet_tx(ndev);
	} while ((done < budget) && fec_enet_collect_events(fep));

	if (done < budget) {
		napi_complete_done(napi, done);
		writel(FEC_DEFAULT_IMASK, fep->hwp + FEC_IMASK);
	}

	return done;
}

/* ------------------------------------------------------------------------- */
static int fec_get_mac(struct net_device *ndev)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	unsigned char *iap, tmpaddr[ETH_ALEN];
	int ret;

	/*
	 * try to get mac address in following order:
	 *
	 * 1) module parameter via kernel command line in form
	 *    fec.macaddr=0x00,0x04,0x9f,0x01,0x30,0xe0
	 */
	iap = macaddr;

	/*
	 * 2) from device tree data
	 */
	if (!is_valid_ether_addr(iap)) {
		struct device_node *np = fep->pdev->dev.of_node;
		if (np) {
			ret = of_get_mac_address(np, tmpaddr);
			if (!ret)
				iap = tmpaddr;
			else if (ret == -EPROBE_DEFER)
				return ret;
		}
	}

	/*
	 * 3) from flash or fuse (via platform data)
	 */
	if (!is_valid_ether_addr(iap)) {
#ifdef CONFIG_M5272
		if (FEC_FLASHMAC)
			iap = (unsigned char *)FEC_FLASHMAC;
#else
		struct fec_platform_data *pdata = dev_get_platdata(&fep->pdev->dev);

		if (pdata)
			iap = (unsigned char *)&pdata->mac;
#endif
	}

	/*
	 * 4) FEC mac registers set by bootloader
	 */
	if (!is_valid_ether_addr(iap)) {
		*((__be32 *) &tmpaddr[0]) =
			cpu_to_be32(readl(fep->hwp + FEC_ADDR_LOW));
		*((__be16 *) &tmpaddr[4]) =
			cpu_to_be16(readl(fep->hwp + FEC_ADDR_HIGH) >> 16);
		iap = &tmpaddr[0];
	}

	/*
	 * 5) random mac address
	 */
	if (!is_valid_ether_addr(iap)) {
		/* Report it and use a random ethernet address instead */
		dev_err(&fep->pdev->dev, "Invalid MAC address: %pM\n", iap);
		eth_hw_addr_random(ndev);
		dev_info(&fep->pdev->dev, "Using random MAC address: %pM\n",
			 ndev->dev_addr);
		return 0;
	}

	/* Adjust MAC if using macaddr */
	eth_hw_addr_gen(ndev, iap, iap == macaddr ? fep->dev_id : 0);

	return 0;
}

/* ------------------------------------------------------------------------- */

/*
 * Phy section
 */
static void fec_enet_adjust_link(struct net_device *ndev)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	struct phy_device *phy_dev = ndev->phydev;
	int status_change = 0;

	/*
	 * If the netdev is down, or is going down, we're not interested
	 * in link state events, so just mark our idea of the link as down
	 * and ignore the event.
	 */
	if (!netif_running(ndev) || !netif_device_present(ndev)) {
		fep->link = 0;
	} else if (phy_dev->link) {
		if (!fep->link) {
			fep->link = phy_dev->link;
			status_change = 1;
		}

		if (fep->full_duplex != phy_dev->duplex) {
			fep->full_duplex = phy_dev->duplex;
			status_change = 1;
		}

		if (phy_dev->speed != fep->speed) {
			fep->speed = phy_dev->speed;
			status_change = 1;
		}

		/* if any of the above changed restart the FEC */
		if (status_change) {
			napi_disable(&fep->napi);
			netif_tx_lock_bh(ndev);
			fec_restart(ndev);
			netif_tx_wake_all_queues(ndev);
			netif_tx_unlock_bh(ndev);
			napi_enable(&fep->napi);
		}
	} else {
		if (fep->link) {
			napi_disable(&fep->napi);
			netif_tx_lock_bh(ndev);
			fec_stop(ndev);
			netif_tx_unlock_bh(ndev);
			napi_enable(&fep->napi);
			fep->link = phy_dev->link;
			status_change = 1;
		}
	}

	if (status_change)
		phy_print_status(phy_dev);
}

static int fec_enet_mdio_wait(struct fec_enet_private *fep)
{
	uint ievent;
	int ret;

	ret = readl_poll_timeout_atomic(fep->hwp + FEC_IEVENT, ievent,
					ievent & FEC_ENET_MII, 2, 30000);

	if (!ret)
		writel(FEC_ENET_MII, fep->hwp + FEC_IEVENT);

	return ret;
}

static int fec_enet_mdio_read(struct mii_bus *bus, int mii_id, int regnum)
{
	struct fec_enet_private *fep = bus->priv;
	struct device *dev = &fep->pdev->dev;
	int ret = 0, frame_start, frame_addr, frame_op;
	bool is_c45 = !!(regnum & MII_ADDR_C45);

	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0)
		return ret;

	if (is_c45) {
		frame_start = FEC_MMFR_ST_C45;

		/* write address */
		frame_addr = (regnum >> 16);
		writel(frame_start | FEC_MMFR_OP_ADDR_WRITE |
		       FEC_MMFR_PA(mii_id) | FEC_MMFR_RA(frame_addr) |
		       FEC_MMFR_TA | (regnum & 0xFFFF),
		       fep->hwp + FEC_MII_DATA);

		/* wait for end of transfer */
		ret = fec_enet_mdio_wait(fep);
		if (ret) {
			netdev_err(fep->netdev, "MDIO address write timeout\n");
			goto out;
		}

		frame_op = FEC_MMFR_OP_READ_C45;

	} else {
		/* C22 read */
		frame_op = FEC_MMFR_OP_READ;
		frame_start = FEC_MMFR_ST;
		frame_addr = regnum;
	}

	/* start a read op */
	writel(frame_start | frame_op |
		FEC_MMFR_PA(mii_id) | FEC_MMFR_RA(frame_addr) |
		FEC_MMFR_TA, fep->hwp + FEC_MII_DATA);

	/* wait for end of transfer */
	ret = fec_enet_mdio_wait(fep);
	if (ret) {
		netdev_err(fep->netdev, "MDIO read timeout\n");
		goto out;
	}

	ret = FEC_MMFR_DATA(readl(fep->hwp + FEC_MII_DATA));

out:
	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	return ret;
}

static int fec_enet_mdio_write(struct mii_bus *bus, int mii_id, int regnum,
			   u16 value)
{
	struct fec_enet_private *fep = bus->priv;
	struct device *dev = &fep->pdev->dev;
	int ret, frame_start, frame_addr;
	bool is_c45 = !!(regnum & MII_ADDR_C45);

	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0)
		return ret;

	if (is_c45) {
		frame_start = FEC_MMFR_ST_C45;

		/* write address */
		frame_addr = (regnum >> 16);
		writel(frame_start | FEC_MMFR_OP_ADDR_WRITE |
		       FEC_MMFR_PA(mii_id) | FEC_MMFR_RA(frame_addr) |
		       FEC_MMFR_TA | (regnum & 0xFFFF),
		       fep->hwp + FEC_MII_DATA);

		/* wait for end of transfer */
		ret = fec_enet_mdio_wait(fep);
		if (ret) {
			netdev_err(fep->netdev, "MDIO address write timeout\n");
			goto out;
		}
	} else {
		/* C22 write */
		frame_start = FEC_MMFR_ST;
		frame_addr = regnum;
	}

	/* start a write op */
	writel(frame_start | FEC_MMFR_OP_WRITE |
		FEC_MMFR_PA(mii_id) | FEC_MMFR_RA(frame_addr) |
		FEC_MMFR_TA | FEC_MMFR_DATA(value),
		fep->hwp + FEC_MII_DATA);

	/* wait for end of transfer */
	ret = fec_enet_mdio_wait(fep);
	if (ret)
		netdev_err(fep->netdev, "MDIO write timeout\n");

out:
	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	return ret;
}

static void fec_enet_phy_reset_after_clk_enable(struct net_device *ndev)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	struct phy_device *phy_dev = ndev->phydev;

	if (phy_dev) {
		phy_reset_after_clk_enable(phy_dev);
	} else if (fep->phy_node) {
		/*
		 * If the PHY still is not bound to the MAC, but there is
		 * OF PHY node and a matching PHY device instance already,
		 * use the OF PHY node to obtain the PHY device instance,
		 * and then use that PHY device instance when triggering
		 * the PHY reset.
		 */
		phy_dev = of_phy_find_device(fep->phy_node);
		phy_reset_after_clk_enable(phy_dev);
		put_device(&phy_dev->mdio.dev);
	}
}

static int fec_enet_clk_enable(struct net_device *ndev, bool enable)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	int ret;

	if (enable) {
		ret = clk_prepare_enable(fep->clk_enet_out);
		if (ret)
			return ret;

		if (fep->clk_ptp) {
			mutex_lock(&fep->ptp_clk_mutex);
			ret = clk_prepare_enable(fep->clk_ptp);
			if (ret) {
				mutex_unlock(&fep->ptp_clk_mutex);
				goto failed_clk_ptp;
			} else {
				fep->ptp_clk_on = true;
			}
			mutex_unlock(&fep->ptp_clk_mutex);
		}

		ret = clk_prepare_enable(fep->clk_ref);
		if (ret)
			goto failed_clk_ref;

		ret = clk_prepare_enable(fep->clk_2x_txclk);
		if (ret)
			goto failed_clk_2x_txclk;

		fec_enet_phy_reset_after_clk_enable(ndev);
	} else {
		clk_disable_unprepare(fep->clk_enet_out);
		if (fep->clk_ptp) {
			mutex_lock(&fep->ptp_clk_mutex);
			clk_disable_unprepare(fep->clk_ptp);
			fep->ptp_clk_on = false;
			mutex_unlock(&fep->ptp_clk_mutex);
		}
		clk_disable_unprepare(fep->clk_ref);
		clk_disable_unprepare(fep->clk_2x_txclk);
	}

	return 0;

failed_clk_2x_txclk:
	if (fep->clk_ref)
		clk_disable_unprepare(fep->clk_ref);
failed_clk_ref:
	if (fep->clk_ptp) {
		mutex_lock(&fep->ptp_clk_mutex);
		clk_disable_unprepare(fep->clk_ptp);
		fep->ptp_clk_on = false;
		mutex_unlock(&fep->ptp_clk_mutex);
	}
failed_clk_ptp:
	clk_disable_unprepare(fep->clk_enet_out);

	return ret;
}

static int fec_enet_parse_rgmii_delay(struct fec_enet_private *fep,
				      struct device_node *np)
{
	u32 rgmii_tx_delay, rgmii_rx_delay;

	/* For rgmii tx internal delay, valid values are 0ps and 2000ps */
	if (!of_property_read_u32(np, "tx-internal-delay-ps", &rgmii_tx_delay)) {
		if (rgmii_tx_delay != 0 && rgmii_tx_delay != 2000) {
			dev_err(&fep->pdev->dev, "The only allowed RGMII TX delay values are: 0ps, 2000ps");
			return -EINVAL;
		} else if (rgmii_tx_delay == 2000) {
			fep->rgmii_txc_dly = true;
		}
	}

	/* For rgmii rx internal delay, valid values are 0ps and 2000ps */
	if (!of_property_read_u32(np, "rx-internal-delay-ps", &rgmii_rx_delay)) {
		if (rgmii_rx_delay != 0 && rgmii_rx_delay != 2000) {
			dev_err(&fep->pdev->dev, "The only allowed RGMII RX delay values are: 0ps, 2000ps");
			return -EINVAL;
		} else if (rgmii_rx_delay == 2000) {
			fep->rgmii_rxc_dly = true;
		}
	}

	return 0;
}

static int fec_enet_mii_probe(struct net_device *ndev)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	struct phy_device *phy_dev = NULL;
	char mdio_bus_id[MII_BUS_ID_SIZE];
	char phy_name[MII_BUS_ID_SIZE + 3];
	int phy_id;
	int dev_id = fep->dev_id;

	if (fep->phy_node) {
		phy_dev = of_phy_connect(ndev, fep->phy_node,
					 &fec_enet_adjust_link, 0,
					 fep->phy_interface);
		if (!phy_dev) {
			netdev_err(ndev, "Unable to connect to phy\n");
			return -ENODEV;
		}
	} else {
		/* check for attached phy */
		for (phy_id = 0; (phy_id < PHY_MAX_ADDR); phy_id++) {
			if (!mdiobus_is_registered_device(fep->mii_bus, phy_id))
				continue;
			if (dev_id--)
				continue;
			strlcpy(mdio_bus_id, fep->mii_bus->id, MII_BUS_ID_SIZE);
			break;
		}

		if (phy_id >= PHY_MAX_ADDR) {
			netdev_info(ndev, "no PHY, assuming direct connection to switch\n");
			strlcpy(mdio_bus_id, "fixed-0", MII_BUS_ID_SIZE);
			phy_id = 0;
		}

		snprintf(phy_name, sizeof(phy_name),
			 PHY_ID_FMT, mdio_bus_id, phy_id);
		phy_dev = phy_connect(ndev, phy_name, &fec_enet_adjust_link,
				      fep->phy_interface);
	}

	if (IS_ERR(phy_dev)) {
		netdev_err(ndev, "could not attach to PHY\n");
		return PTR_ERR(phy_dev);
	}

	/* mask with MAC supported features */
	if (fep->quirks & FEC_QUIRK_HAS_GBIT) {
		phy_set_max_speed(phy_dev, 1000);
		phy_remove_link_mode(phy_dev,
				     ETHTOOL_LINK_MODE_1000baseT_Half_BIT);
#if !defined(CONFIG_M5272)
		phy_support_sym_pause(phy_dev);
#endif
	}
	else
		phy_set_max_speed(phy_dev, 100);

	fep->link = 0;
	fep->full_duplex = 0;

	phy_dev->mac_managed_pm = 1;

	phy_attached_info(phy_dev);

	return 0;
}

static int fec_enet_mii_init(struct platform_device *pdev)
{
	static struct mii_bus *fec0_mii_bus;
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct fec_enet_private *fep = netdev_priv(ndev);
	bool suppress_preamble = false;
	struct device_node *node;
	int err = -ENXIO;
	u32 mii_speed, holdtime;
	u32 bus_freq;

	/*
	 * The i.MX28 dual fec interfaces are not equal.
	 * Here are the differences:
	 *
	 *  - fec0 supports MII & RMII modes while fec1 only supports RMII
	 *  - fec0 acts as the 1588 time master while fec1 is slave
	 *  - external phys can only be configured by fec0
	 *
	 * That is to say fec1 can not work independently. It only works
	 * when fec0 is working. The reason behind this design is that the
	 * second interface is added primarily for Switch mode.
	 *
	 * Because of the last point above, both phys are attached on fec0
	 * mdio interface in board design, and need to be configured by
	 * fec0 mii_bus.
	 */
	if ((fep->quirks & FEC_QUIRK_SINGLE_MDIO) && fep->dev_id > 0) {
		/* fec1 uses fec0 mii_bus */
		if (mii_cnt && fec0_mii_bus) {
			fep->mii_bus = fec0_mii_bus;
			mii_cnt++;
			return 0;
		}
		return -ENOENT;
	}

	bus_freq = 2500000; /* 2.5MHz by default */
	node = of_get_child_by_name(pdev->dev.of_node, "mdio");
	if (node) {
		of_property_read_u32(node, "clock-frequency", &bus_freq);
		suppress_preamble = of_property_read_bool(node,
							  "suppress-preamble");
	}

	/*
	 * Set MII speed (= clk_get_rate() / 2 * phy_speed)
	 *
	 * The formula for FEC MDC is 'ref_freq / (MII_SPEED x 2)' while
	 * for ENET-MAC is 'ref_freq / ((MII_SPEED + 1) x 2)'.  The i.MX28
	 * Reference Manual has an error on this, and gets fixed on i.MX6Q
	 * document.
	 */
	mii_speed = DIV_ROUND_UP(clk_get_rate(fep->clk_ipg), bus_freq * 2);
	if (fep->quirks & FEC_QUIRK_ENET_MAC)
		mii_speed--;
	if (mii_speed > 63) {
		dev_err(&pdev->dev,
			"fec clock (%lu) too fast to get right mii speed\n",
			clk_get_rate(fep->clk_ipg));
		err = -EINVAL;
		goto err_out;
	}

	/*
	 * The i.MX28 and i.MX6 types have another filed in the MSCR (aka
	 * MII_SPEED) register that defines the MDIO output hold time. Earlier
	 * versions are RAZ there, so just ignore the difference and write the
	 * register always.
	 * The minimal hold time according to IEE802.3 (clause 22) is 10 ns.
	 * HOLDTIME + 1 is the number of clk cycles the fec is holding the
	 * output.
	 * The HOLDTIME bitfield takes values between 0 and 7 (inclusive).
	 * Given that ceil(clkrate / 5000000) <= 64, the calculation for
	 * holdtime cannot result in a value greater than 3.
	 */
	holdtime = DIV_ROUND_UP(clk_get_rate(fep->clk_ipg), 100000000) - 1;

	fep->phy_speed = mii_speed << 1 | holdtime << 8;

	if (suppress_preamble)
		fep->phy_speed |= BIT(7);

	if (fep->quirks & FEC_QUIRK_CLEAR_SETUP_MII) {
		/* Clear MMFR to avoid to generate MII event by writing MSCR.
		 * MII event generation condition:
		 * - writing MSCR:
		 *	- mmfr[31:0]_not_zero & mscr[7:0]_is_zero &
		 *	  mscr_reg_data_in[7:0] != 0
		 * - writing MMFR:
		 *	- mscr[7:0]_not_zero
		 */
		writel(0, fep->hwp + FEC_MII_DATA);
	}

	writel(fep->phy_speed, fep->hwp + FEC_MII_SPEED);

	/* Clear any pending transaction complete indication */
	writel(FEC_ENET_MII, fep->hwp + FEC_IEVENT);

	fep->mii_bus = mdiobus_alloc();
	if (fep->mii_bus == NULL) {
		err = -ENOMEM;
		goto err_out;
	}

	fep->mii_bus->name = "fec_enet_mii_bus";
	fep->mii_bus->read = fec_enet_mdio_read;
	fep->mii_bus->write = fec_enet_mdio_write;
	snprintf(fep->mii_bus->id, MII_BUS_ID_SIZE, "%s-%x",
		pdev->name, fep->dev_id + 1);
	fep->mii_bus->priv = fep;
	fep->mii_bus->parent = &pdev->dev;

	err = of_mdiobus_register(fep->mii_bus, node);
	if (err)
		goto err_out_free_mdiobus;
	of_node_put(node);

	mii_cnt++;

	/* save fec0 mii_bus */
	if (fep->quirks & FEC_QUIRK_SINGLE_MDIO)
		fec0_mii_bus = fep->mii_bus;

	return 0;

err_out_free_mdiobus:
	mdiobus_free(fep->mii_bus);
err_out:
	of_node_put(node);
	return err;
}

static void fec_enet_mii_remove(struct fec_enet_private *fep)
{
	if (--mii_cnt == 0) {
		mdiobus_unregister(fep->mii_bus);
		mdiobus_free(fep->mii_bus);
	}
}

static void fec_enet_get_drvinfo(struct net_device *ndev,
				 struct ethtool_drvinfo *info)
{
	struct fec_enet_private *fep = netdev_priv(ndev);

	strlcpy(info->driver, fep->pdev->dev.driver->name,
		sizeof(info->driver));
	strlcpy(info->bus_info, dev_name(&ndev->dev), sizeof(info->bus_info));
}

static int fec_enet_get_regs_len(struct net_device *ndev)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	struct resource *r;
	int s = 0;

	r = platform_get_resource(fep->pdev, IORESOURCE_MEM, 0);
	if (r)
		s = resource_size(r);

	return s;
}

/* List of registers that can be safety be read to dump them with ethtool */
#if defined(CONFIG_M523x) || defined(CONFIG_M527x) || defined(CONFIG_M528x) || \
	defined(CONFIG_M520x) || defined(CONFIG_M532x) || defined(CONFIG_ARM) || \
	defined(CONFIG_ARM64) || defined(CONFIG_COMPILE_TEST)
static __u32 fec_enet_register_version = 2;
static u32 fec_enet_register_offset[] = {
	FEC_IEVENT, FEC_IMASK, FEC_R_DES_ACTIVE_0, FEC_X_DES_ACTIVE_0,
	FEC_ECNTRL, FEC_MII_DATA, FEC_MII_SPEED, FEC_MIB_CTRLSTAT, FEC_R_CNTRL,
	FEC_X_CNTRL, FEC_ADDR_LOW, FEC_ADDR_HIGH, FEC_OPD, FEC_TXIC0, FEC_TXIC1,
	FEC_TXIC2, FEC_RXIC0, FEC_RXIC1, FEC_RXIC2, FEC_HASH_TABLE_HIGH,
	FEC_HASH_TABLE_LOW, FEC_GRP_HASH_TABLE_HIGH, FEC_GRP_HASH_TABLE_LOW,
	FEC_X_WMRK, FEC_R_BOUND, FEC_R_FSTART, FEC_R_DES_START_1,
	FEC_X_DES_START_1, FEC_R_BUFF_SIZE_1, FEC_R_DES_START_2,
	FEC_X_DES_START_2, FEC_R_BUFF_SIZE_2, FEC_R_DES_START_0,
	FEC_X_DES_START_0, FEC_R_BUFF_SIZE_0, FEC_R_FIFO_RSFL, FEC_R_FIFO_RSEM,
	FEC_R_FIFO_RAEM, FEC_R_FIFO_RAFL, FEC_RACC, FEC_RCMR_1, FEC_RCMR_2,
	FEC_DMA_CFG_1, FEC_DMA_CFG_2, FEC_R_DES_ACTIVE_1, FEC_X_DES_ACTIVE_1,
	FEC_R_DES_ACTIVE_2, FEC_X_DES_ACTIVE_2, FEC_QOS_SCHEME,
	RMON_T_DROP, RMON_T_PACKETS, RMON_T_BC_PKT, RMON_T_MC_PKT,
	RMON_T_CRC_ALIGN, RMON_T_UNDERSIZE, RMON_T_OVERSIZE, RMON_T_FRAG,
	RMON_T_JAB, RMON_T_COL, RMON_T_P64, RMON_T_P65TO127, RMON_T_P128TO255,
	RMON_T_P256TO511, RMON_T_P512TO1023, RMON_T_P1024TO2047,
	RMON_T_P_GTE2048, RMON_T_OCTETS,
	IEEE_T_DROP, IEEE_T_FRAME_OK, IEEE_T_1COL, IEEE_T_MCOL, IEEE_T_DEF,
	IEEE_T_LCOL, IEEE_T_EXCOL, IEEE_T_MACERR, IEEE_T_CSERR, IEEE_T_SQE,
	IEEE_T_FDXFC, IEEE_T_OCTETS_OK,
	RMON_R_PACKETS, RMON_R_BC_PKT, RMON_R_MC_PKT, RMON_R_CRC_ALIGN,
	RMON_R_UNDERSIZE, RMON_R_OVERSIZE, RMON_R_FRAG, RMON_R_JAB,
	RMON_R_RESVD_O, RMON_R_P64, RMON_R_P65TO127, RMON_R_P128TO255,
	RMON_R_P256TO511, RMON_R_P512TO1023, RMON_R_P1024TO2047,
	RMON_R_P_GTE2048, RMON_R_OCTETS,
	IEEE_R_DROP, IEEE_R_FRAME_OK, IEEE_R_CRC, IEEE_R_ALIGN, IEEE_R_MACERR,
	IEEE_R_FDXFC, IEEE_R_OCTETS_OK
};
#else
static __u32 fec_enet_register_version = 1;
static u32 fec_enet_register_offset[] = {
	FEC_ECNTRL, FEC_IEVENT, FEC_IMASK, FEC_IVEC, FEC_R_DES_ACTIVE_0,
	FEC_R_DES_ACTIVE_1, FEC_R_DES_ACTIVE_2, FEC_X_DES_ACTIVE_0,
	FEC_X_DES_ACTIVE_1, FEC_X_DES_ACTIVE_2, FEC_MII_DATA, FEC_MII_SPEED,
	FEC_R_BOUND, FEC_R_FSTART, FEC_X_WMRK, FEC_X_FSTART, FEC_R_CNTRL,
	FEC_MAX_FRM_LEN, FEC_X_CNTRL, FEC_ADDR_LOW, FEC_ADDR_HIGH,
	FEC_GRP_HASH_TABLE_HIGH, FEC_GRP_HASH_TABLE_LOW, FEC_R_DES_START_0,
	FEC_R_DES_START_1, FEC_R_DES_START_2, FEC_X_DES_START_0,
	FEC_X_DES_START_1, FEC_X_DES_START_2, FEC_R_BUFF_SIZE_0,
	FEC_R_BUFF_SIZE_1, FEC_R_BUFF_SIZE_2
};
#endif

static void fec_enet_get_regs(struct net_device *ndev,
			      struct ethtool_regs *regs, void *regbuf)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	u32 __iomem *theregs = (u32 __iomem *)fep->hwp;
	struct device *dev = &fep->pdev->dev;
	u32 *buf = (u32 *)regbuf;
	u32 i, off;
	int ret;

	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0)
		return;

	regs->version = fec_enet_register_version;

	memset(buf, 0, regs->len);

	for (i = 0; i < ARRAY_SIZE(fec_enet_register_offset); i++) {
		off = fec_enet_register_offset[i];

		if ((off == FEC_R_BOUND || off == FEC_R_FSTART) &&
		    !(fep->quirks & FEC_QUIRK_HAS_FRREG))
			continue;

		off >>= 2;
		buf[off] = readl(&theregs[off]);
	}

	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);
}

static int fec_enet_get_ts_info(struct net_device *ndev,
				struct ethtool_ts_info *info)
{
	struct fec_enet_private *fep = netdev_priv(ndev);

	if (fep->bufdesc_ex) {

		info->so_timestamping = SOF_TIMESTAMPING_TX_SOFTWARE |
					SOF_TIMESTAMPING_RX_SOFTWARE |
					SOF_TIMESTAMPING_SOFTWARE |
					SOF_TIMESTAMPING_TX_HARDWARE |
					SOF_TIMESTAMPING_RX_HARDWARE |
					SOF_TIMESTAMPING_RAW_HARDWARE;
		if (fep->ptp_clock)
			info->phc_index = ptp_clock_index(fep->ptp_clock);
		else
			info->phc_index = -1;

		info->tx_types = (1 << HWTSTAMP_TX_OFF) |
				 (1 << HWTSTAMP_TX_ON);

		info->rx_filters = (1 << HWTSTAMP_FILTER_NONE) |
				   (1 << HWTSTAMP_FILTER_ALL);
		return 0;
	} else {
		return ethtool_op_get_ts_info(ndev, info);
	}
}

#if !defined(CONFIG_M5272)

static void fec_enet_get_pauseparam(struct net_device *ndev,
				    struct ethtool_pauseparam *pause)
{
	struct fec_enet_private *fep = netdev_priv(ndev);

	pause->autoneg = (fep->pause_flag & FEC_PAUSE_FLAG_AUTONEG) != 0;
	pause->tx_pause = (fep->pause_flag & FEC_PAUSE_FLAG_ENABLE) != 0;
	pause->rx_pause = pause->tx_pause;
}

static int fec_enet_set_pauseparam(struct net_device *ndev,
				   struct ethtool_pauseparam *pause)
{
	struct fec_enet_private *fep = netdev_priv(ndev);

	if (!ndev->phydev)
		return -ENODEV;

	if (pause->tx_pause != pause->rx_pause) {
		netdev_info(ndev,
			"hardware only support enable/disable both tx and rx");
		return -EINVAL;
	}

	fep->pause_flag = 0;

	/* tx pause must be same as rx pause */
	fep->pause_flag |= pause->rx_pause ? FEC_PAUSE_FLAG_ENABLE : 0;
	fep->pause_flag |= pause->autoneg ? FEC_PAUSE_FLAG_AUTONEG : 0;

	phy_set_sym_pause(ndev->phydev, pause->rx_pause, pause->tx_pause,
			  pause->autoneg);

	if (pause->autoneg) {
		if (netif_running(ndev))
			fec_stop(ndev);
		phy_start_aneg(ndev->phydev);
	}
	if (netif_running(ndev)) {
		napi_disable(&fep->napi);
		netif_tx_lock_bh(ndev);
		fec_restart(ndev);
		netif_tx_wake_all_queues(ndev);
		netif_tx_unlock_bh(ndev);
		napi_enable(&fep->napi);
	}

	return 0;
}

static const struct fec_stat {
	char name[ETH_GSTRING_LEN];
	u16 offset;
} fec_stats[] = {
	/* RMON TX */
	{ "tx_dropped", RMON_T_DROP },
	{ "tx_packets", RMON_T_PACKETS },
	{ "tx_broadcast", RMON_T_BC_PKT },
	{ "tx_multicast", RMON_T_MC_PKT },
	{ "tx_crc_errors", RMON_T_CRC_ALIGN },
	{ "tx_undersize", RMON_T_UNDERSIZE },
	{ "tx_oversize", RMON_T_OVERSIZE },
	{ "tx_fragment", RMON_T_FRAG },
	{ "tx_jabber", RMON_T_JAB },
	{ "tx_collision", RMON_T_COL },
	{ "tx_64byte", RMON_T_P64 },
	{ "tx_65to127byte", RMON_T_P65TO127 },
	{ "tx_128to255byte", RMON_T_P128TO255 },
	{ "tx_256to511byte", RMON_T_P256TO511 },
	{ "tx_512to1023byte", RMON_T_P512TO1023 },
	{ "tx_1024to2047byte", RMON_T_P1024TO2047 },
	{ "tx_GTE2048byte", RMON_T_P_GTE2048 },
	{ "tx_octets", RMON_T_OCTETS },

	/* IEEE TX */
	{ "IEEE_tx_drop", IEEE_T_DROP },
	{ "IEEE_tx_frame_ok", IEEE_T_FRAME_OK },
	{ "IEEE_tx_1col", IEEE_T_1COL },
	{ "IEEE_tx_mcol", IEEE_T_MCOL },
	{ "IEEE_tx_def", IEEE_T_DEF },
	{ "IEEE_tx_lcol", IEEE_T_LCOL },
	{ "IEEE_tx_excol", IEEE_T_EXCOL },
	{ "IEEE_tx_macerr", IEEE_T_MACERR },
	{ "IEEE_tx_cserr", IEEE_T_CSERR },
	{ "IEEE_tx_sqe", IEEE_T_SQE },
	{ "IEEE_tx_fdxfc", IEEE_T_FDXFC },
	{ "IEEE_tx_octets_ok", IEEE_T_OCTETS_OK },

	/* RMON RX */
	{ "rx_packets", RMON_R_PACKETS },
	{ "rx_broadcast", RMON_R_BC_PKT },
	{ "rx_multicast", RMON_R_MC_PKT },
	{ "rx_crc_errors", RMON_R_CRC_ALIGN },
	{ "rx_undersize", RMON_R_UNDERSIZE },
	{ "rx_oversize", RMON_R_OVERSIZE },
	{ "rx_fragment", RMON_R_FRAG },
	{ "rx_jabber", RMON_R_JAB },
	{ "rx_64byte", RMON_R_P64 },
	{ "rx_65to127byte", RMON_R_P65TO127 },
	{ "rx_128to255byte", RMON_R_P128TO255 },
	{ "rx_256to511byte", RMON_R_P256TO511 },
	{ "rx_512to1023byte", RMON_R_P512TO1023 },
	{ "rx_1024to2047byte", RMON_R_P1024TO2047 },
	{ "rx_GTE2048byte", RMON_R_P_GTE2048 },
	{ "rx_octets", RMON_R_OCTETS },

	/* IEEE RX */
	{ "IEEE_rx_drop", IEEE_R_DROP },
	{ "IEEE_rx_frame_ok", IEEE_R_FRAME_OK },
	{ "IEEE_rx_crc", IEEE_R_CRC },
	{ "IEEE_rx_align", IEEE_R_ALIGN },
	{ "IEEE_rx_macerr", IEEE_R_MACERR },
	{ "IEEE_rx_fdxfc", IEEE_R_FDXFC },
	{ "IEEE_rx_octets_ok", IEEE_R_OCTETS_OK },
};

#define FEC_STATS_SIZE		(ARRAY_SIZE(fec_stats) * sizeof(u64))

static void fec_enet_update_ethtool_stats(struct net_device *dev)
{
	struct fec_enet_private *fep = netdev_priv(dev);
	int i;

	for (i = 0; i < ARRAY_SIZE(fec_stats); i++)
		fep->ethtool_stats[i] = readl(fep->hwp + fec_stats[i].offset);
}

static void fec_enet_get_ethtool_stats(struct net_device *dev,
				       struct ethtool_stats *stats, u64 *data)
{
	struct fec_enet_private *fep = netdev_priv(dev);

	if (netif_running(dev))
		fec_enet_update_ethtool_stats(dev);

	memcpy(data, fep->ethtool_stats, FEC_STATS_SIZE);
}

static void fec_enet_get_strings(struct net_device *netdev,
	u32 stringset, u8 *data)
{
	int i;
	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < ARRAY_SIZE(fec_stats); i++)
			memcpy(data + i * ETH_GSTRING_LEN,
				fec_stats[i].name, ETH_GSTRING_LEN);
		break;
	case ETH_SS_TEST:
		net_selftest_get_strings(data);
		break;
	}
}

static int fec_enet_get_sset_count(struct net_device *dev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return ARRAY_SIZE(fec_stats);
	case ETH_SS_TEST:
		return net_selftest_get_count();
	default:
		return -EOPNOTSUPP;
	}
}

static void fec_enet_clear_ethtool_stats(struct net_device *dev)
{
	struct fec_enet_private *fep = netdev_priv(dev);
	int i;

	/* Disable MIB statistics counters */
	writel(FEC_MIB_CTRLSTAT_DISABLE, fep->hwp + FEC_MIB_CTRLSTAT);

	for (i = 0; i < ARRAY_SIZE(fec_stats); i++)
		writel(0, fep->hwp + fec_stats[i].offset);

	/* Don't disable MIB statistics counters */
	writel(0, fep->hwp + FEC_MIB_CTRLSTAT);
}

#else	/* !defined(CONFIG_M5272) */
#define FEC_STATS_SIZE	0
static inline void fec_enet_update_ethtool_stats(struct net_device *dev)
{
}

static inline void fec_enet_clear_ethtool_stats(struct net_device *dev)
{
}
#endif /* !defined(CONFIG_M5272) */

/* ITR clock source is enet system clock (clk_ahb).
 * TCTT unit is cycle_ns * 64 cycle
 * So, the ICTT value = X us / (cycle_ns * 64)
 */
static int fec_enet_us_to_itr_clock(struct net_device *ndev, int us)
{
	struct fec_enet_private *fep = netdev_priv(ndev);

	return us * (fep->itr_clk_rate / 64000) / 1000;
}

/* Set threshold for interrupt coalescing */
static void fec_enet_itr_coal_set(struct net_device *ndev)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	int rx_itr, tx_itr;

	/* Must be greater than zero to avoid unpredictable behavior */
	if (!fep->rx_time_itr || !fep->rx_pkts_itr ||
	    !fep->tx_time_itr || !fep->tx_pkts_itr)
		return;

	/* Select enet system clock as Interrupt Coalescing
	 * timer Clock Source
	 */
	rx_itr = FEC_ITR_CLK_SEL;
	tx_itr = FEC_ITR_CLK_SEL;

	/* set ICFT and ICTT */
	rx_itr |= FEC_ITR_ICFT(fep->rx_pkts_itr);
	rx_itr |= FEC_ITR_ICTT(fec_enet_us_to_itr_clock(ndev, fep->rx_time_itr));
	tx_itr |= FEC_ITR_ICFT(fep->tx_pkts_itr);
	tx_itr |= FEC_ITR_ICTT(fec_enet_us_to_itr_clock(ndev, fep->tx_time_itr));

	rx_itr |= FEC_ITR_EN;
	tx_itr |= FEC_ITR_EN;

	writel(tx_itr, fep->hwp + FEC_TXIC0);
	writel(rx_itr, fep->hwp + FEC_RXIC0);
	if (fep->quirks & FEC_QUIRK_HAS_MULTI_QUEUES) {
		writel(tx_itr, fep->hwp + FEC_TXIC1);
		writel(rx_itr, fep->hwp + FEC_RXIC1);
		writel(tx_itr, fep->hwp + FEC_TXIC2);
		writel(rx_itr, fep->hwp + FEC_RXIC2);
	}
}

static int fec_enet_get_coalesce(struct net_device *ndev,
				 struct ethtool_coalesce *ec,
				 struct kernel_ethtool_coalesce *kernel_coal,
				 struct netlink_ext_ack *extack)
{
	struct fec_enet_private *fep = netdev_priv(ndev);

	if (!(fep->quirks & FEC_QUIRK_HAS_COALESCE))
		return -EOPNOTSUPP;

	ec->rx_coalesce_usecs = fep->rx_time_itr;
	ec->rx_max_coalesced_frames = fep->rx_pkts_itr;

	ec->tx_coalesce_usecs = fep->tx_time_itr;
	ec->tx_max_coalesced_frames = fep->tx_pkts_itr;

	return 0;
}

static int fec_enet_set_coalesce(struct net_device *ndev,
				 struct ethtool_coalesce *ec,
				 struct kernel_ethtool_coalesce *kernel_coal,
				 struct netlink_ext_ack *extack)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	struct device *dev = &fep->pdev->dev;
	unsigned int cycle;

	if (!(fep->quirks & FEC_QUIRK_HAS_COALESCE))
		return -EOPNOTSUPP;

	if (ec->rx_max_coalesced_frames > 255) {
		dev_err(dev, "Rx coalesced frames exceed hardware limitation\n");
		return -EINVAL;
	}

	if (ec->tx_max_coalesced_frames > 255) {
		dev_err(dev, "Tx coalesced frame exceed hardware limitation\n");
		return -EINVAL;
	}

	cycle = fec_enet_us_to_itr_clock(ndev, ec->rx_coalesce_usecs);
	if (cycle > 0xFFFF) {
		dev_err(dev, "Rx coalesced usec exceed hardware limitation\n");
		return -EINVAL;
	}

	cycle = fec_enet_us_to_itr_clock(ndev, ec->tx_coalesce_usecs);
	if (cycle > 0xFFFF) {
		dev_err(dev, "Tx coalesced usec exceed hardware limitation\n");
		return -EINVAL;
	}

	fep->rx_time_itr = ec->rx_coalesce_usecs;
	fep->rx_pkts_itr = ec->rx_max_coalesced_frames;

	fep->tx_time_itr = ec->tx_coalesce_usecs;
	fep->tx_pkts_itr = ec->tx_max_coalesced_frames;

	fec_enet_itr_coal_set(ndev);

	return 0;
}

static void fec_enet_itr_coal_init(struct net_device *ndev)
{
	struct ethtool_coalesce ec;

	ec.rx_coalesce_usecs = FEC_ITR_ICTT_DEFAULT;
	ec.rx_max_coalesced_frames = FEC_ITR_ICFT_DEFAULT;

	ec.tx_coalesce_usecs = FEC_ITR_ICTT_DEFAULT;
	ec.tx_max_coalesced_frames = FEC_ITR_ICFT_DEFAULT;

	fec_enet_set_coalesce(ndev, &ec, NULL, NULL);
}

static int fec_enet_get_tunable(struct net_device *netdev,
				const struct ethtool_tunable *tuna,
				void *data)
{
	struct fec_enet_private *fep = netdev_priv(netdev);
	int ret = 0;

	switch (tuna->id) {
	case ETHTOOL_RX_COPYBREAK:
		*(u32 *)data = fep->rx_copybreak;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int fec_enet_set_tunable(struct net_device *netdev,
				const struct ethtool_tunable *tuna,
				const void *data)
{
	struct fec_enet_private *fep = netdev_priv(netdev);
	int ret = 0;

	switch (tuna->id) {
	case ETHTOOL_RX_COPYBREAK:
		fep->rx_copybreak = *(u32 *)data;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

/* LPI Sleep Ts count base on tx clk (clk_ref).
 * The lpi sleep cnt value = X us / (cycle_ns).
 */
static int fec_enet_us_to_tx_cycle(struct net_device *ndev, int us)
{
	struct fec_enet_private *fep = netdev_priv(ndev);

	return us * (fep->clk_ref_rate / 1000) / 1000;
}

static int fec_enet_eee_mode_set(struct net_device *ndev, bool enable)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	struct ethtool_eee *p = &fep->eee;
	unsigned int sleep_cycle, wake_cycle;
	int ret = 0;

	if (enable) {
		ret = phy_init_eee(ndev->phydev, false);
		if (ret)
			return ret;

		sleep_cycle = fec_enet_us_to_tx_cycle(ndev, p->tx_lpi_timer);
		wake_cycle = sleep_cycle;
	} else {
		sleep_cycle = 0;
		wake_cycle = 0;
	}

	p->tx_lpi_enabled = enable;
	p->eee_enabled = enable;
	p->eee_active = enable;

	writel(sleep_cycle, fep->hwp + FEC_LPI_SLEEP);
	writel(wake_cycle, fep->hwp + FEC_LPI_WAKE);

	return 0;
}

static int
fec_enet_get_eee(struct net_device *ndev, struct ethtool_eee *edata)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	struct ethtool_eee *p = &fep->eee;

	if (!(fep->quirks & FEC_QUIRK_HAS_EEE))
		return -EOPNOTSUPP;

	if (!netif_running(ndev))
		return -ENETDOWN;

	edata->eee_enabled = p->eee_enabled;
	edata->eee_active = p->eee_active;
	edata->tx_lpi_timer = p->tx_lpi_timer;
	edata->tx_lpi_enabled = p->tx_lpi_enabled;

	return phy_ethtool_get_eee(ndev->phydev, edata);
}

static int
fec_enet_set_eee(struct net_device *ndev, struct ethtool_eee *edata)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	struct ethtool_eee *p = &fep->eee;
	int ret = 0;

	if (!(fep->quirks & FEC_QUIRK_HAS_EEE))
		return -EOPNOTSUPP;

	if (!netif_running(ndev))
		return -ENETDOWN;

	p->tx_lpi_timer = edata->tx_lpi_timer;

	if (!edata->eee_enabled || !edata->tx_lpi_enabled ||
	    !edata->tx_lpi_timer)
		ret = fec_enet_eee_mode_set(ndev, false);
	else
		ret = fec_enet_eee_mode_set(ndev, true);

	if (ret)
		return ret;

	return phy_ethtool_set_eee(ndev->phydev, edata);
}

static void
fec_enet_get_wol(struct net_device *ndev, struct ethtool_wolinfo *wol)
{
	struct fec_enet_private *fep = netdev_priv(ndev);

	if (fep->wol_flag & FEC_WOL_HAS_MAGIC_PACKET) {
		wol->supported = WAKE_MAGIC;
		wol->wolopts = fep->wol_flag & FEC_WOL_FLAG_ENABLE ? WAKE_MAGIC : 0;
	} else {
		wol->supported = wol->wolopts = 0;
	}
}

static int
fec_enet_set_wol(struct net_device *ndev, struct ethtool_wolinfo *wol)
{
	struct fec_enet_private *fep = netdev_priv(ndev);

	if (!(fep->wol_flag & FEC_WOL_HAS_MAGIC_PACKET))
		return -EINVAL;

	if (wol->wolopts & ~WAKE_MAGIC)
		return -EINVAL;

	device_set_wakeup_enable(&ndev->dev, wol->wolopts & WAKE_MAGIC);
	if (device_may_wakeup(&ndev->dev))
		fep->wol_flag |= FEC_WOL_FLAG_ENABLE;
	else
		fep->wol_flag &= (~FEC_WOL_FLAG_ENABLE);

	return 0;
}

static const struct ethtool_ops fec_enet_ethtool_ops = {
	.supported_coalesce_params = ETHTOOL_COALESCE_USECS |
				     ETHTOOL_COALESCE_MAX_FRAMES,
	.get_drvinfo		= fec_enet_get_drvinfo,
	.get_regs_len		= fec_enet_get_regs_len,
	.get_regs		= fec_enet_get_regs,
	.nway_reset		= phy_ethtool_nway_reset,
	.get_link		= ethtool_op_get_link,
	.get_coalesce		= fec_enet_get_coalesce,
	.set_coalesce		= fec_enet_set_coalesce,
#ifndef CONFIG_M5272
	.get_pauseparam		= fec_enet_get_pauseparam,
	.set_pauseparam		= fec_enet_set_pauseparam,
	.get_strings		= fec_enet_get_strings,
	.get_ethtool_stats	= fec_enet_get_ethtool_stats,
	.get_sset_count		= fec_enet_get_sset_count,
#endif
	.get_ts_info		= fec_enet_get_ts_info,
	.get_tunable		= fec_enet_get_tunable,
	.set_tunable		= fec_enet_set_tunable,
	.get_wol		= fec_enet_get_wol,
	.set_wol		= fec_enet_set_wol,
	.get_eee		= fec_enet_get_eee,
	.set_eee		= fec_enet_set_eee,
	.get_link_ksettings	= phy_ethtool_get_link_ksettings,
	.set_link_ksettings	= phy_ethtool_set_link_ksettings,
	.self_test		= net_selftest,
};

static int fec_enet_ioctl(struct net_device *ndev, struct ifreq *rq, int cmd)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	struct phy_device *phydev = ndev->phydev;

	if (!netif_running(ndev))
		return -EINVAL;

	if (!phydev)
		return -ENODEV;

	if (fep->bufdesc_ex) {
		bool use_fec_hwts = !phy_has_hwtstamp(phydev);

		if (cmd == SIOCSHWTSTAMP) {
			if (use_fec_hwts)
				return fec_ptp_set(ndev, rq);
			fec_ptp_disable_hwts(ndev);
		} else if (cmd == SIOCGHWTSTAMP) {
			if (use_fec_hwts)
				return fec_ptp_get(ndev, rq);
		}
	}

	return phy_mii_ioctl(phydev, rq, cmd);
}

static void fec_enet_free_buffers(struct net_device *ndev)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	unsigned int i;
	struct sk_buff *skb;
	struct bufdesc	*bdp;
	struct fec_enet_priv_tx_q *txq;
	struct fec_enet_priv_rx_q *rxq;
	unsigned int q;

	for (q = 0; q < fep->num_rx_queues; q++) {
		rxq = fep->rx_queue[q];
		bdp = rxq->bd.base;
		for (i = 0; i < rxq->bd.ring_size; i++) {
			skb = rxq->rx_skbuff[i];
			rxq->rx_skbuff[i] = NULL;
			if (skb) {
				dma_unmap_single(&fep->pdev->dev,
						 fec32_to_cpu(bdp->cbd_bufaddr),
						 FEC_ENET_RX_FRSIZE - fep->rx_align,
						 DMA_FROM_DEVICE);
				dev_kfree_skb(skb);
			}
			bdp = fec_enet_get_nextdesc(bdp, &rxq->bd);
		}
	}

	for (q = 0; q < fep->num_tx_queues; q++) {
		txq = fep->tx_queue[q];
		for (i = 0; i < txq->bd.ring_size; i++) {
			kfree(txq->tx_bounce[i]);
			txq->tx_bounce[i] = NULL;
			skb = txq->tx_skbuff[i];
			txq->tx_skbuff[i] = NULL;
			dev_kfree_skb(skb);
		}
	}
}

static void fec_enet_free_queue(struct net_device *ndev)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	int i;
	struct fec_enet_priv_tx_q *txq;

	for (i = 0; i < fep->num_tx_queues; i++)
		if (fep->tx_queue[i] && fep->tx_queue[i]->tso_hdrs) {
			txq = fep->tx_queue[i];
			dma_free_coherent(&fep->pdev->dev,
					  txq->bd.ring_size * TSO_HEADER_SIZE,
					  txq->tso_hdrs,
					  txq->tso_hdrs_dma);
		}

	for (i = 0; i < fep->num_rx_queues; i++)
		kfree(fep->rx_queue[i]);
	for (i = 0; i < fep->num_tx_queues; i++)
		kfree(fep->tx_queue[i]);
}

static int fec_enet_alloc_queue(struct net_device *ndev)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	int i;
	int ret = 0;
	struct fec_enet_priv_tx_q *txq;

	for (i = 0; i < fep->num_tx_queues; i++) {
		txq = kzalloc(sizeof(*txq), GFP_KERNEL);
		if (!txq) {
			ret = -ENOMEM;
			goto alloc_failed;
		}

		fep->tx_queue[i] = txq;
		txq->bd.ring_size = TX_RING_SIZE;
		fep->total_tx_ring_size += fep->tx_queue[i]->bd.ring_size;

		txq->tx_stop_threshold = FEC_MAX_SKB_DESCS;
		txq->tx_wake_threshold =
			(txq->bd.ring_size - txq->tx_stop_threshold) / 2;

		txq->tso_hdrs = dma_alloc_coherent(&fep->pdev->dev,
					txq->bd.ring_size * TSO_HEADER_SIZE,
					&txq->tso_hdrs_dma,
					GFP_KERNEL);
		if (!txq->tso_hdrs) {
			ret = -ENOMEM;
			goto alloc_failed;
		}
	}

	for (i = 0; i < fep->num_rx_queues; i++) {
		fep->rx_queue[i] = kzalloc(sizeof(*fep->rx_queue[i]),
					   GFP_KERNEL);
		if (!fep->rx_queue[i]) {
			ret = -ENOMEM;
			goto alloc_failed;
		}

		fep->rx_queue[i]->bd.ring_size = RX_RING_SIZE;
		fep->total_rx_ring_size += fep->rx_queue[i]->bd.ring_size;
	}
	return ret;

alloc_failed:
	fec_enet_free_queue(ndev);
	return ret;
}

static int
fec_enet_alloc_rxq_buffers(struct net_device *ndev, unsigned int queue)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	unsigned int i;
	struct sk_buff *skb;
	struct bufdesc	*bdp;
	struct fec_enet_priv_rx_q *rxq;

	rxq = fep->rx_queue[queue];
	bdp = rxq->bd.base;
	for (i = 0; i < rxq->bd.ring_size; i++) {
		skb = __netdev_alloc_skb(ndev, FEC_ENET_RX_FRSIZE, GFP_KERNEL);
		if (!skb)
			goto err_alloc;

		if (fec_enet_new_rxbdp(ndev, bdp, skb)) {
			dev_kfree_skb(skb);
			goto err_alloc;
		}

		rxq->rx_skbuff[i] = skb;
		bdp->cbd_sc = cpu_to_fec16(BD_ENET_RX_EMPTY);

		if (fep->bufdesc_ex) {
			struct bufdesc_ex *ebdp = (struct bufdesc_ex *)bdp;
			ebdp->cbd_esc = cpu_to_fec32(BD_ENET_RX_INT);
		}

		bdp = fec_enet_get_nextdesc(bdp, &rxq->bd);
	}

	/* Set the last buffer to wrap. */
	bdp = fec_enet_get_prevdesc(bdp, &rxq->bd);
	bdp->cbd_sc |= cpu_to_fec16(BD_SC_WRAP);
	return 0;

 err_alloc:
	fec_enet_free_buffers(ndev);
	return -ENOMEM;
}

static int
fec_enet_alloc_txq_buffers(struct net_device *ndev, unsigned int queue)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	unsigned int i;
	struct bufdesc  *bdp;
	struct fec_enet_priv_tx_q *txq;

	txq = fep->tx_queue[queue];
	bdp = txq->bd.base;
	for (i = 0; i < txq->bd.ring_size; i++) {
		txq->tx_bounce[i] = kmalloc(FEC_ENET_TX_FRSIZE, GFP_KERNEL);
		if (!txq->tx_bounce[i])
			goto err_alloc;

		bdp->cbd_sc = cpu_to_fec16(0);
		bdp->cbd_bufaddr = cpu_to_fec32(0);

		if (fep->bufdesc_ex) {
			struct bufdesc_ex *ebdp = (struct bufdesc_ex *)bdp;
			ebdp->cbd_esc = cpu_to_fec32(BD_ENET_TX_INT);
		}

		bdp = fec_enet_get_nextdesc(bdp, &txq->bd);
	}

	/* Set the last buffer to wrap. */
	bdp = fec_enet_get_prevdesc(bdp, &txq->bd);
	bdp->cbd_sc |= cpu_to_fec16(BD_SC_WRAP);

	return 0;

 err_alloc:
	fec_enet_free_buffers(ndev);
	return -ENOMEM;
}

static int fec_enet_alloc_buffers(struct net_device *ndev)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	unsigned int i;

	for (i = 0; i < fep->num_rx_queues; i++)
		if (fec_enet_alloc_rxq_buffers(ndev, i))
			return -ENOMEM;

	for (i = 0; i < fep->num_tx_queues; i++)
		if (fec_enet_alloc_txq_buffers(ndev, i))
			return -ENOMEM;
	return 0;
}

static int
fec_enet_open(struct net_device *ndev)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	int ret;
	bool reset_again;

	ret = pm_runtime_resume_and_get(&fep->pdev->dev);
	if (ret < 0)
		return ret;

	pinctrl_pm_select_default_state(&fep->pdev->dev);
	ret = fec_enet_clk_enable(ndev, true);
	if (ret)
		goto clk_enable;

	/* During the first fec_enet_open call the PHY isn't probed at this
	 * point. Therefore the phy_reset_after_clk_enable() call within
	 * fec_enet_clk_enable() fails. As we need this reset in order to be
	 * sure the PHY is working correctly we check if we need to reset again
	 * later when the PHY is probed
	 */
	if (ndev->phydev && ndev->phydev->drv)
		reset_again = false;
	else
		reset_again = true;

	/* I should reset the ring buffers here, but I don't yet know
	 * a simple way to do that.
	 */

	ret = fec_enet_alloc_buffers(ndev);
	if (ret)
		goto err_enet_alloc;

	/* Init MAC prior to mii bus probe */
	fec_restart(ndev);

	/* Call phy_reset_after_clk_enable() again if it failed during
	 * phy_reset_after_clk_enable() before because the PHY wasn't probed.
	 */
	if (reset_again)
		fec_enet_phy_reset_after_clk_enable(ndev);

	/* Probe and connect to PHY when open the interface */
	ret = fec_enet_mii_probe(ndev);
	if (ret)
		goto err_enet_mii_probe;

	if (fep->quirks & FEC_QUIRK_ERR006687)
		imx6q_cpuidle_fec_irqs_used();

	napi_enable(&fep->napi);
	phy_start(ndev->phydev);
	netif_tx_start_all_queues(ndev);

	device_set_wakeup_enable(&ndev->dev, fep->wol_flag &
				 FEC_WOL_FLAG_ENABLE);

	return 0;

err_enet_mii_probe:
	fec_enet_free_buffers(ndev);
err_enet_alloc:
	fec_enet_clk_enable(ndev, false);
clk_enable:
	pm_runtime_mark_last_busy(&fep->pdev->dev);
	pm_runtime_put_autosuspend(&fep->pdev->dev);
	pinctrl_pm_select_sleep_state(&fep->pdev->dev);
	return ret;
}

static int
fec_enet_close(struct net_device *ndev)
{
	struct fec_enet_private *fep = netdev_priv(ndev);

	phy_stop(ndev->phydev);

	if (netif_device_present(ndev)) {
		napi_disable(&fep->napi);
		netif_tx_disable(ndev);
		fec_stop(ndev);
	}

	phy_disconnect(ndev->phydev);

	if (fep->quirks & FEC_QUIRK_ERR006687)
		imx6q_cpuidle_fec_irqs_unused();

	fec_enet_update_ethtool_stats(ndev);

	fec_enet_clk_enable(ndev, false);
	pinctrl_pm_select_sleep_state(&fep->pdev->dev);
	pm_runtime_mark_last_busy(&fep->pdev->dev);
	pm_runtime_put_autosuspend(&fep->pdev->dev);

	fec_enet_free_buffers(ndev);

	return 0;
}

/* Set or clear the multicast filter for this adaptor.
 * Skeleton taken from sunlance driver.
 * The CPM Ethernet implementation allows Multicast as well as individual
 * MAC address filtering.  Some of the drivers check to make sure it is
 * a group multicast address, and discard those that are not.  I guess I
 * will do the same for now, but just remove the test if you want
 * individual filtering as well (do the upper net layers want or support
 * this kind of feature?).
 */

#define FEC_HASH_BITS	6		/* #bits in hash */

static void set_multicast_list(struct net_device *ndev)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	struct netdev_hw_addr *ha;
	unsigned int crc, tmp;
	unsigned char hash;
	unsigned int hash_high = 0, hash_low = 0;

	if (ndev->flags & IFF_PROMISC) {
		tmp = readl(fep->hwp + FEC_R_CNTRL);
		tmp |= 0x8;
		writel(tmp, fep->hwp + FEC_R_CNTRL);
		return;
	}

	tmp = readl(fep->hwp + FEC_R_CNTRL);
	tmp &= ~0x8;
	writel(tmp, fep->hwp + FEC_R_CNTRL);

	if (ndev->flags & IFF_ALLMULTI) {
		/* Catch all multicast addresses, so set the
		 * filter to all 1's
		 */
		writel(0xffffffff, fep->hwp + FEC_GRP_HASH_TABLE_HIGH);
		writel(0xffffffff, fep->hwp + FEC_GRP_HASH_TABLE_LOW);

		return;
	}

	/* Add the addresses in hash register */
	netdev_for_each_mc_addr(ha, ndev) {
		/* calculate crc32 value of mac address */
		crc = ether_crc_le(ndev->addr_len, ha->addr);

		/* only upper 6 bits (FEC_HASH_BITS) are used
		 * which point to specific bit in the hash registers
		 */
		hash = (crc >> (32 - FEC_HASH_BITS)) & 0x3f;

		if (hash > 31)
			hash_high |= 1 << (hash - 32);
		else
			hash_low |= 1 << hash;
	}

	writel(hash_high, fep->hwp + FEC_GRP_HASH_TABLE_HIGH);
	writel(hash_low, fep->hwp + FEC_GRP_HASH_TABLE_LOW);
}

/* Set a MAC change in hardware. */
static int
fec_set_mac_address(struct net_device *ndev, void *p)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	struct sockaddr *addr = p;

	if (addr) {
		if (!is_valid_ether_addr(addr->sa_data))
			return -EADDRNOTAVAIL;
		eth_hw_addr_set(ndev, addr->sa_data);
	}

	/* Add netif status check here to avoid system hang in below case:
	 * ifconfig ethx down; ifconfig ethx hw ether xx:xx:xx:xx:xx:xx;
	 * After ethx down, fec all clocks are gated off and then register
	 * access causes system hang.
	 */
	if (!netif_running(ndev))
		return 0;

	writel(ndev->dev_addr[3] | (ndev->dev_addr[2] << 8) |
		(ndev->dev_addr[1] << 16) | (ndev->dev_addr[0] << 24),
		fep->hwp + FEC_ADDR_LOW);
	writel((ndev->dev_addr[5] << 16) | (ndev->dev_addr[4] << 24),
		fep->hwp + FEC_ADDR_HIGH);
	return 0;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
/**
 * fec_poll_controller - FEC Poll controller function
 * @dev: The FEC network adapter
 *
 * Polled functionality used by netconsole and others in non interrupt mode
 *
 */
static void fec_poll_controller(struct net_device *dev)
{
	int i;
	struct fec_enet_private *fep = netdev_priv(dev);

	for (i = 0; i < FEC_IRQ_NUM; i++) {
		if (fep->irq[i] > 0) {
			disable_irq(fep->irq[i]);
			fec_enet_interrupt(fep->irq[i], dev);
			enable_irq(fep->irq[i]);
		}
	}
}
#endif

static inline void fec_enet_set_netdev_features(struct net_device *netdev,
	netdev_features_t features)
{
	struct fec_enet_private *fep = netdev_priv(netdev);
	netdev_features_t changed = features ^ netdev->features;

	netdev->features = features;

	/* Receive checksum has been changed */
	if (changed & NETIF_F_RXCSUM) {
		if (features & NETIF_F_RXCSUM)
			fep->csum_flags |= FLAG_RX_CSUM_ENABLED;
		else
			fep->csum_flags &= ~FLAG_RX_CSUM_ENABLED;
	}
}

static int fec_set_features(struct net_device *netdev,
	netdev_features_t features)
{
	struct fec_enet_private *fep = netdev_priv(netdev);
	netdev_features_t changed = features ^ netdev->features;

	if (netif_running(netdev) && changed & NETIF_F_RXCSUM) {
		napi_disable(&fep->napi);
		netif_tx_lock_bh(netdev);
		fec_stop(netdev);
		fec_enet_set_netdev_features(netdev, features);
		fec_restart(netdev);
		netif_tx_wake_all_queues(netdev);
		netif_tx_unlock_bh(netdev);
		napi_enable(&fep->napi);
	} else {
		fec_enet_set_netdev_features(netdev, features);
	}

	return 0;
}

static u16 fec_enet_get_raw_vlan_tci(struct sk_buff *skb)
{
	struct vlan_ethhdr *vhdr;
	unsigned short vlan_TCI = 0;

	if (skb->protocol == htons(ETH_P_ALL)) {
		vhdr = (struct vlan_ethhdr *)(skb->data);
		vlan_TCI = ntohs(vhdr->h_vlan_TCI);
	}

	return vlan_TCI;
}

static u16 fec_enet_select_queue(struct net_device *ndev, struct sk_buff *skb,
				 struct net_device *sb_dev)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	u16 vlan_tag;

	if (!(fep->quirks & FEC_QUIRK_HAS_AVB))
		return netdev_pick_tx(ndev, skb, NULL);

	vlan_tag = fec_enet_get_raw_vlan_tci(skb);
	if (!vlan_tag)
		return vlan_tag;

	return fec_enet_vlan_pri_to_queue[vlan_tag >> 13];
}

static const struct net_device_ops fec_netdev_ops = {
	.ndo_open		= fec_enet_open,
	.ndo_stop		= fec_enet_close,
	.ndo_start_xmit		= fec_enet_start_xmit,
	.ndo_select_queue       = fec_enet_select_queue,
	.ndo_set_rx_mode	= set_multicast_list,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_tx_timeout		= fec_timeout,
	.ndo_set_mac_address	= fec_set_mac_address,
	.ndo_eth_ioctl		= fec_enet_ioctl,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= fec_poll_controller,
#endif
	.ndo_set_features	= fec_set_features,
};

static const unsigned short offset_des_active_rxq[] = {
	FEC_R_DES_ACTIVE_0, FEC_R_DES_ACTIVE_1, FEC_R_DES_ACTIVE_2
};

static const unsigned short offset_des_active_txq[] = {
	FEC_X_DES_ACTIVE_0, FEC_X_DES_ACTIVE_1, FEC_X_DES_ACTIVE_2
};

 /*
  * XXX:  We need to clean up on failure exits here.
  *
  */
static int fec_enet_init(struct net_device *ndev)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	struct bufdesc *cbd_base;
	dma_addr_t bd_dma;
	int bd_size;
	unsigned int i;
	unsigned dsize = fep->bufdesc_ex ? sizeof(struct bufdesc_ex) :
			sizeof(struct bufdesc);
	unsigned dsize_log2 = __fls(dsize);
	int ret;

	WARN_ON(dsize != (1 << dsize_log2));
#if defined(CONFIG_ARM) || defined(CONFIG_ARM64)
	fep->rx_align = 0xf;
	fep->tx_align = 0xf;
#else
	fep->rx_align = 0x3;
	fep->tx_align = 0x3;
#endif

	/* Check mask of the streaming and coherent API */
	ret = dma_set_mask_and_coherent(&fep->pdev->dev, DMA_BIT_MASK(32));
	if (ret < 0) {
		dev_warn(&fep->pdev->dev, "No suitable DMA available\n");
		return ret;
	}

	ret = fec_enet_alloc_queue(ndev);
	if (ret)
		return ret;

	bd_size = (fep->total_tx_ring_size + fep->total_rx_ring_size) * dsize;

	/* Allocate memory for buffer descriptors. */
	cbd_base = dmam_alloc_coherent(&fep->pdev->dev, bd_size, &bd_dma,
				       GFP_KERNEL);
	if (!cbd_base) {
		ret = -ENOMEM;
		goto free_queue_mem;
	}

	/* Get the Ethernet address */
	ret = fec_get_mac(ndev);
	if (ret)
		goto free_queue_mem;

	/* make sure MAC we just acquired is programmed into the hw */
	fec_set_mac_address(ndev, NULL);

	/* Set receive and transmit descriptor base. */
	for (i = 0; i < fep->num_rx_queues; i++) {
		struct fec_enet_priv_rx_q *rxq = fep->rx_queue[i];
		unsigned size = dsize * rxq->bd.ring_size;

		rxq->bd.qid = i;
		rxq->bd.base = cbd_base;
		rxq->bd.cur = cbd_base;
		rxq->bd.dma = bd_dma;
		rxq->bd.dsize = dsize;
		rxq->bd.dsize_log2 = dsize_log2;
		rxq->bd.reg_desc_active = fep->hwp + offset_des_active_rxq[i];
		bd_dma += size;
		cbd_base = (struct bufdesc *)(((void *)cbd_base) + size);
		rxq->bd.last = (struct bufdesc *)(((void *)cbd_base) - dsize);
	}

	for (i = 0; i < fep->num_tx_queues; i++) {
		struct fec_enet_priv_tx_q *txq = fep->tx_queue[i];
		unsigned size = dsize * txq->bd.ring_size;

		txq->bd.qid = i;
		txq->bd.base = cbd_base;
		txq->bd.cur = cbd_base;
		txq->bd.dma = bd_dma;
		txq->bd.dsize = dsize;
		txq->bd.dsize_log2 = dsize_log2;
		txq->bd.reg_desc_active = fep->hwp + offset_des_active_txq[i];
		bd_dma += size;
		cbd_base = (struct bufdesc *)(((void *)cbd_base) + size);
		txq->bd.last = (struct bufdesc *)(((void *)cbd_base) - dsize);
	}


	/* The FEC Ethernet specific entries in the device structure */
	ndev->watchdog_timeo = TX_TIMEOUT;
	ndev->netdev_ops = &fec_netdev_ops;
	ndev->ethtool_ops = &fec_enet_ethtool_ops;

	writel(FEC_RX_DISABLED_IMASK, fep->hwp + FEC_IMASK);
	netif_napi_add(ndev, &fep->napi, fec_enet_rx_napi, NAPI_POLL_WEIGHT);

	if (fep->quirks & FEC_QUIRK_HAS_VLAN)
		/* enable hw VLAN support */
		ndev->features |= NETIF_F_HW_VLAN_CTAG_RX;

	if (fep->quirks & FEC_QUIRK_HAS_CSUM) {
		netif_set_tso_max_segs(ndev, FEC_MAX_TSO_SEGS);

		/* enable hw accelerator */
		ndev->features |= (NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM
				| NETIF_F_RXCSUM | NETIF_F_SG | NETIF_F_TSO);
		fep->csum_flags |= FLAG_RX_CSUM_ENABLED;
	}

	if (fep->quirks & FEC_QUIRK_HAS_MULTI_QUEUES) {
		fep->tx_align = 0;
		fep->rx_align = 0x3f;
	}

	ndev->hw_features = ndev->features;

	fec_restart(ndev);

	if (fep->quirks & FEC_QUIRK_MIB_CLEAR)
		fec_enet_clear_ethtool_stats(ndev);
	else
		fec_enet_update_ethtool_stats(ndev);

	return 0;

free_queue_mem:
	fec_enet_free_queue(ndev);
	return ret;
}

#ifdef CONFIG_OF
static int fec_reset_phy(struct platform_device *pdev)
{
	int err, phy_reset;
	bool active_high = false;
	int msec = 1, phy_post_delay = 0;
	struct device_node *np = pdev->dev.of_node;

	if (!np)
		return 0;

	err = of_property_read_u32(np, "phy-reset-duration", &msec);
	/* A sane reset duration should not be longer than 1s */
	if (!err && msec > 1000)
		msec = 1;

	phy_reset = of_get_named_gpio(np, "phy-reset-gpios", 0);
	if (phy_reset == -EPROBE_DEFER)
		return phy_reset;
	else if (!gpio_is_valid(phy_reset))
		return 0;

	err = of_property_read_u32(np, "phy-reset-post-delay", &phy_post_delay);
	/* valid reset duration should be less than 1s */
	if (!err && phy_post_delay > 1000)
		return -EINVAL;

	active_high = of_property_read_bool(np, "phy-reset-active-high");

	err = devm_gpio_request_one(&pdev->dev, phy_reset,
			active_high ? GPIOF_OUT_INIT_HIGH : GPIOF_OUT_INIT_LOW,
			"phy-reset");
	if (err) {
		dev_err(&pdev->dev, "failed to get phy-reset-gpios: %d\n", err);
		return err;
	}

	if (msec > 20)
		msleep(msec);
	else
		usleep_range(msec * 1000, msec * 1000 + 1000);

	gpio_set_value_cansleep(phy_reset, !active_high);

	if (!phy_post_delay)
		return 0;

	if (phy_post_delay > 20)
		msleep(phy_post_delay);
	else
		usleep_range(phy_post_delay * 1000,
			     phy_post_delay * 1000 + 1000);

	return 0;
}
#else /* CONFIG_OF */
static int fec_reset_phy(struct platform_device *pdev)
{
	/*
	 * In case of platform probe, the reset has been done
	 * by machine code.
	 */
	return 0;
}
#endif /* CONFIG_OF */

static void
fec_enet_get_queue_num(struct platform_device *pdev, int *num_tx, int *num_rx)
{
	struct device_node *np = pdev->dev.of_node;

	*num_tx = *num_rx = 1;

	if (!np || !of_device_is_available(np))
		return;

	/* parse the num of tx and rx queues */
	of_property_read_u32(np, "fsl,num-tx-queues", num_tx);

	of_property_read_u32(np, "fsl,num-rx-queues", num_rx);

	if (*num_tx < 1 || *num_tx > FEC_ENET_MAX_TX_QS) {
		dev_warn(&pdev->dev, "Invalid num_tx(=%d), fall back to 1\n",
			 *num_tx);
		*num_tx = 1;
		return;
	}

	if (*num_rx < 1 || *num_rx > FEC_ENET_MAX_RX_QS) {
		dev_warn(&pdev->dev, "Invalid num_rx(=%d), fall back to 1\n",
			 *num_rx);
		*num_rx = 1;
		return;
	}

}

static int fec_enet_get_irq_cnt(struct platform_device *pdev)
{
	int irq_cnt = platform_irq_count(pdev);

	if (irq_cnt > FEC_IRQ_NUM)
		irq_cnt = FEC_IRQ_NUM;	/* last for pps */
	else if (irq_cnt == 2)
		irq_cnt = 1;	/* last for pps */
	else if (irq_cnt <= 0)
		irq_cnt = 1;	/* At least 1 irq is needed */
	return irq_cnt;
}

static void fec_enet_get_wakeup_irq(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct fec_enet_private *fep = netdev_priv(ndev);

	if (fep->quirks & FEC_QUIRK_WAKEUP_FROM_INT2)
		fep->wake_irq = fep->irq[2];
	else
		fep->wake_irq = fep->irq[0];
}

static int fec_enet_init_stop_mode(struct fec_enet_private *fep,
				   struct device_node *np)
{
	struct device_node *gpr_np;
	u32 out_val[3];
	int ret = 0;

	gpr_np = of_parse_phandle(np, "fsl,stop-mode", 0);
	if (!gpr_np)
		return 0;

	ret = of_property_read_u32_array(np, "fsl,stop-mode", out_val,
					 ARRAY_SIZE(out_val));
	if (ret) {
		dev_dbg(&fep->pdev->dev, "no stop mode property\n");
		goto out;
	}

	fep->stop_gpr.gpr = syscon_node_to_regmap(gpr_np);
	if (IS_ERR(fep->stop_gpr.gpr)) {
		dev_err(&fep->pdev->dev, "could not find gpr regmap\n");
		ret = PTR_ERR(fep->stop_gpr.gpr);
		fep->stop_gpr.gpr = NULL;
		goto out;
	}

	fep->stop_gpr.reg = out_val[1];
	fep->stop_gpr.bit = out_val[2];

out:
	of_node_put(gpr_np);

	return ret;
}

static int
fec_probe(struct platform_device *pdev)
{
	struct fec_enet_private *fep;
	struct fec_platform_data *pdata;
	phy_interface_t interface;
	struct net_device *ndev;
	int i, irq, ret = 0;
	const struct of_device_id *of_id;
	static int dev_id;
	struct device_node *np = pdev->dev.of_node, *phy_node;
	int num_tx_qs;
	int num_rx_qs;
	char irq_name[8];
	int irq_cnt;
	struct fec_devinfo *dev_info;

	fec_enet_get_queue_num(pdev, &num_tx_qs, &num_rx_qs);

	/* Init network device */
	ndev = alloc_etherdev_mqs(sizeof(struct fec_enet_private) +
				  FEC_STATS_SIZE, num_tx_qs, num_rx_qs);
	if (!ndev)
		return -ENOMEM;

	SET_NETDEV_DEV(ndev, &pdev->dev);

	/* setup board info structure */
	fep = netdev_priv(ndev);

	of_id = of_match_device(fec_dt_ids, &pdev->dev);
	if (of_id)
		pdev->id_entry = of_id->data;
	dev_info = (struct fec_devinfo *)pdev->id_entry->driver_data;
	if (dev_info)
		fep->quirks = dev_info->quirks;

	fep->netdev = ndev;
	fep->num_rx_queues = num_rx_qs;
	fep->num_tx_queues = num_tx_qs;

#if !defined(CONFIG_M5272)
	/* default enable pause frame auto negotiation */
	if (fep->quirks & FEC_QUIRK_HAS_GBIT)
		fep->pause_flag |= FEC_PAUSE_FLAG_AUTONEG;
#endif

	/* Select default pin state */
	pinctrl_pm_select_default_state(&pdev->dev);

	fep->hwp = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(fep->hwp)) {
		ret = PTR_ERR(fep->hwp);
		goto failed_ioremap;
	}

	fep->pdev = pdev;
	fep->dev_id = dev_id++;

	platform_set_drvdata(pdev, ndev);

	if ((of_machine_is_compatible("fsl,imx6q") ||
	     of_machine_is_compatible("fsl,imx6dl")) &&
	    !of_property_read_bool(np, "fsl,err006687-workaround-present"))
		fep->quirks |= FEC_QUIRK_ERR006687;

	if (of_get_property(np, "fsl,magic-packet", NULL))
		fep->wol_flag |= FEC_WOL_HAS_MAGIC_PACKET;

	ret = fec_enet_init_stop_mode(fep, np);
	if (ret)
		goto failed_stop_mode;

	phy_node = of_parse_phandle(np, "phy-handle", 0);
	if (!phy_node && of_phy_is_fixed_link(np)) {
		ret = of_phy_register_fixed_link(np);
		if (ret < 0) {
			dev_err(&pdev->dev,
				"broken fixed-link specification\n");
			goto failed_phy;
		}
		phy_node = of_node_get(np);
	}
	fep->phy_node = phy_node;

	ret = of_get_phy_mode(pdev->dev.of_node, &interface);
	if (ret) {
		pdata = dev_get_platdata(&pdev->dev);
		if (pdata)
			fep->phy_interface = pdata->phy;
		else
			fep->phy_interface = PHY_INTERFACE_MODE_MII;
	} else {
		fep->phy_interface = interface;
	}

	ret = fec_enet_parse_rgmii_delay(fep, np);
	if (ret)
		goto failed_rgmii_delay;

	fep->clk_ipg = devm_clk_get(&pdev->dev, "ipg");
	if (IS_ERR(fep->clk_ipg)) {
		ret = PTR_ERR(fep->clk_ipg);
		goto failed_clk;
	}

	fep->clk_ahb = devm_clk_get(&pdev->dev, "ahb");
	if (IS_ERR(fep->clk_ahb)) {
		ret = PTR_ERR(fep->clk_ahb);
		goto failed_clk;
	}

	fep->itr_clk_rate = clk_get_rate(fep->clk_ahb);

	/* enet_out is optional, depends on board */
	fep->clk_enet_out = devm_clk_get_optional(&pdev->dev, "enet_out");
	if (IS_ERR(fep->clk_enet_out)) {
		ret = PTR_ERR(fep->clk_enet_out);
		goto failed_clk;
	}

	fep->ptp_clk_on = false;
	mutex_init(&fep->ptp_clk_mutex);

	/* clk_ref is optional, depends on board */
	fep->clk_ref = devm_clk_get_optional(&pdev->dev, "enet_clk_ref");
	if (IS_ERR(fep->clk_ref)) {
		ret = PTR_ERR(fep->clk_ref);
		goto failed_clk;
	}
	fep->clk_ref_rate = clk_get_rate(fep->clk_ref);

	/* clk_2x_txclk is optional, depends on board */
	if (fep->rgmii_txc_dly || fep->rgmii_rxc_dly) {
		fep->clk_2x_txclk = devm_clk_get(&pdev->dev, "enet_2x_txclk");
		if (IS_ERR(fep->clk_2x_txclk))
			fep->clk_2x_txclk = NULL;
	}

	fep->bufdesc_ex = fep->quirks & FEC_QUIRK_HAS_BUFDESC_EX;
	fep->clk_ptp = devm_clk_get(&pdev->dev, "ptp");
	if (IS_ERR(fep->clk_ptp)) {
		fep->clk_ptp = NULL;
		fep->bufdesc_ex = false;
	}

	ret = fec_enet_clk_enable(ndev, true);
	if (ret)
		goto failed_clk;

	ret = clk_prepare_enable(fep->clk_ipg);
	if (ret)
		goto failed_clk_ipg;
	ret = clk_prepare_enable(fep->clk_ahb);
	if (ret)
		goto failed_clk_ahb;

	fep->reg_phy = devm_regulator_get_optional(&pdev->dev, "phy");
	if (!IS_ERR(fep->reg_phy)) {
		ret = regulator_enable(fep->reg_phy);
		if (ret) {
			dev_err(&pdev->dev,
				"Failed to enable phy regulator: %d\n", ret);
			goto failed_regulator;
		}
	} else {
		if (PTR_ERR(fep->reg_phy) == -EPROBE_DEFER) {
			ret = -EPROBE_DEFER;
			goto failed_regulator;
		}
		fep->reg_phy = NULL;
	}

	pm_runtime_set_autosuspend_delay(&pdev->dev, FEC_MDIO_PM_TIMEOUT);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_get_noresume(&pdev->dev);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	ret = fec_reset_phy(pdev);
	if (ret)
		goto failed_reset;

	irq_cnt = fec_enet_get_irq_cnt(pdev);
	if (fep->bufdesc_ex)
		fec_ptp_init(pdev, irq_cnt);

	ret = fec_enet_init(ndev);
	if (ret)
		goto failed_init;

	for (i = 0; i < irq_cnt; i++) {
		snprintf(irq_name, sizeof(irq_name), "int%d", i);
		irq = platform_get_irq_byname_optional(pdev, irq_name);
		if (irq < 0)
			irq = platform_get_irq(pdev, i);
		if (irq < 0) {
			ret = irq;
			goto failed_irq;
		}
		ret = devm_request_irq(&pdev->dev, irq, fec_enet_interrupt,
				       0, pdev->name, ndev);
		if (ret)
			goto failed_irq;

		fep->irq[i] = irq;
	}

	/* Decide which interrupt line is wakeup capable */
	fec_enet_get_wakeup_irq(pdev);

	ret = fec_enet_mii_init(pdev);
	if (ret)
		goto failed_mii_init;

	/* Carrier starts down, phylib will bring it up */
	netif_carrier_off(ndev);
	fec_enet_clk_enable(ndev, false);
	pinctrl_pm_select_sleep_state(&pdev->dev);

	ndev->max_mtu = PKT_MAXBUF_SIZE - ETH_HLEN - ETH_FCS_LEN;

	ret = register_netdev(ndev);
	if (ret)
		goto failed_register;

	device_init_wakeup(&ndev->dev, fep->wol_flag &
			   FEC_WOL_HAS_MAGIC_PACKET);

	if (fep->bufdesc_ex && fep->ptp_clock)
		netdev_info(ndev, "registered PHC device %d\n", fep->dev_id);

	fep->rx_copybreak = COPYBREAK_DEFAULT;
	INIT_WORK(&fep->tx_timeout_work, fec_enet_timeout_work);

	pm_runtime_mark_last_busy(&pdev->dev);
	pm_runtime_put_autosuspend(&pdev->dev);

	return 0;

failed_register:
	fec_enet_mii_remove(fep);
failed_mii_init:
failed_irq:
failed_init:
	fec_ptp_stop(pdev);
failed_reset:
	pm_runtime_put_noidle(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	if (fep->reg_phy)
		regulator_disable(fep->reg_phy);
failed_regulator:
	clk_disable_unprepare(fep->clk_ahb);
failed_clk_ahb:
	clk_disable_unprepare(fep->clk_ipg);
failed_clk_ipg:
	fec_enet_clk_enable(ndev, false);
failed_clk:
failed_rgmii_delay:
	if (of_phy_is_fixed_link(np))
		of_phy_deregister_fixed_link(np);
	of_node_put(phy_node);
failed_stop_mode:
failed_phy:
	dev_id--;
failed_ioremap:
	free_netdev(ndev);

	return ret;
}

static int
fec_drv_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct fec_enet_private *fep = netdev_priv(ndev);
	struct device_node *np = pdev->dev.of_node;
	int ret;

	ret = pm_runtime_resume_and_get(&pdev->dev);
	if (ret < 0)
		return ret;

	cancel_work_sync(&fep->tx_timeout_work);
	fec_ptp_stop(pdev);
	unregister_netdev(ndev);
	fec_enet_mii_remove(fep);
	if (fep->reg_phy)
		regulator_disable(fep->reg_phy);

	if (of_phy_is_fixed_link(np))
		of_phy_deregister_fixed_link(np);
	of_node_put(fep->phy_node);

	clk_disable_unprepare(fep->clk_ahb);
	clk_disable_unprepare(fep->clk_ipg);
	pm_runtime_put_noidle(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	free_netdev(ndev);
	return 0;
}

static int __maybe_unused fec_suspend(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct fec_enet_private *fep = netdev_priv(ndev);

	rtnl_lock();
	if (netif_running(ndev)) {
		if (fep->wol_flag & FEC_WOL_FLAG_ENABLE)
			fep->wol_flag |= FEC_WOL_FLAG_SLEEP_ON;
		phy_stop(ndev->phydev);
		napi_disable(&fep->napi);
		netif_tx_lock_bh(ndev);
		netif_device_detach(ndev);
		netif_tx_unlock_bh(ndev);
		fec_stop(ndev);
		if (!(fep->wol_flag & FEC_WOL_FLAG_ENABLE)) {
			fec_irqs_disable(ndev);
			pinctrl_pm_select_sleep_state(&fep->pdev->dev);
		} else {
			fec_irqs_disable_except_wakeup(ndev);
			if (fep->wake_irq > 0) {
				disable_irq(fep->wake_irq);
				enable_irq_wake(fep->wake_irq);
			}
			fec_enet_stop_mode(fep, true);
		}
		/* It's safe to disable clocks since interrupts are masked */
		fec_enet_clk_enable(ndev, false);
	}
	rtnl_unlock();

	if (fep->reg_phy && !(fep->wol_flag & FEC_WOL_FLAG_ENABLE))
		regulator_disable(fep->reg_phy);

	/* SOC supply clock to phy, when clock is disabled, phy link down
	 * SOC control phy regulator, when regulator is disabled, phy link down
	 */
	if (fep->clk_enet_out || fep->reg_phy)
		fep->link = 0;

	return 0;
}

static int __maybe_unused fec_resume(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct fec_enet_private *fep = netdev_priv(ndev);
	int ret;
	int val;

	if (fep->reg_phy && !(fep->wol_flag & FEC_WOL_FLAG_ENABLE)) {
		ret = regulator_enable(fep->reg_phy);
		if (ret)
			return ret;
	}

	rtnl_lock();
	if (netif_running(ndev)) {
		ret = fec_enet_clk_enable(ndev, true);
		if (ret) {
			rtnl_unlock();
			goto failed_clk;
		}
		if (fep->wol_flag & FEC_WOL_FLAG_ENABLE) {
			fec_enet_stop_mode(fep, false);
			if (fep->wake_irq) {
				disable_irq_wake(fep->wake_irq);
				enable_irq(fep->wake_irq);
			}

			val = readl(fep->hwp + FEC_ECNTRL);
			val &= ~(FEC_ECR_MAGICEN | FEC_ECR_SLEEP);
			writel(val, fep->hwp + FEC_ECNTRL);
			fep->wol_flag &= ~FEC_WOL_FLAG_SLEEP_ON;
		} else {
			pinctrl_pm_select_default_state(&fep->pdev->dev);
		}
		fec_restart(ndev);
		netif_tx_lock_bh(ndev);
		netif_device_attach(ndev);
		netif_tx_unlock_bh(ndev);
		napi_enable(&fep->napi);
		phy_init_hw(ndev->phydev);
		phy_start(ndev->phydev);
	}
	rtnl_unlock();

	return 0;

failed_clk:
	if (fep->reg_phy)
		regulator_disable(fep->reg_phy);
	return ret;
}

static int __maybe_unused fec_runtime_suspend(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct fec_enet_private *fep = netdev_priv(ndev);

	clk_disable_unprepare(fep->clk_ahb);
	clk_disable_unprepare(fep->clk_ipg);

	return 0;
}

static int __maybe_unused fec_runtime_resume(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct fec_enet_private *fep = netdev_priv(ndev);
	int ret;

	ret = clk_prepare_enable(fep->clk_ahb);
	if (ret)
		return ret;
	ret = clk_prepare_enable(fep->clk_ipg);
	if (ret)
		goto failed_clk_ipg;

	return 0;

failed_clk_ipg:
	clk_disable_unprepare(fep->clk_ahb);
	return ret;
}

static const struct dev_pm_ops fec_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(fec_suspend, fec_resume)
	SET_RUNTIME_PM_OPS(fec_runtime_suspend, fec_runtime_resume, NULL)
};

static struct platform_driver fec_driver = {
	.driver	= {
		.name	= DRIVER_NAME,
		.pm	= &fec_pm_ops,
		.of_match_table = fec_dt_ids,
		.suppress_bind_attrs = true,
	},
	.id_table = fec_devtype,
	.probe	= fec_probe,
	.remove	= fec_drv_remove,
};

module_platform_driver(fec_driver);

MODULE_LICENSE("GPL");

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
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <net/ip.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/icmp.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/bitops.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/phy.h>
#include <linux/fec.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/of_net.h>
#include <linux/regulator/consumer.h>

#include <asm/cacheflush.h>

#include "fec.h"

#if defined(CONFIG_ARM)
#define FEC_ALIGNMENT	0xf
#else
#define FEC_ALIGNMENT	0x3
#endif

#define DRIVER_NAME	"fec"
#define FEC_NAPI_WEIGHT	64

/* Pause frame feild and FIFO threshold */
#define FEC_ENET_FCE	(1 << 5)
#define FEC_ENET_RSEM_V	0x84
#define FEC_ENET_RSFL_V	16
#define FEC_ENET_RAEM_V	0x8
#define FEC_ENET_RAFL_V	0x8
#define FEC_ENET_OPD_V	0xFFF0

/* Controller is ENET-MAC */
#define FEC_QUIRK_ENET_MAC		(1 << 0)
/* Controller needs driver to swap frame */
#define FEC_QUIRK_SWAP_FRAME		(1 << 1)
/* Controller uses gasket */
#define FEC_QUIRK_USE_GASKET		(1 << 2)
/* Controller has GBIT support */
#define FEC_QUIRK_HAS_GBIT		(1 << 3)
/* Controller has extend desc buffer */
#define FEC_QUIRK_HAS_BUFDESC_EX	(1 << 4)
/* Controller has hardware checksum support */
#define FEC_QUIRK_HAS_CSUM		(1 << 5)

static struct platform_device_id fec_devtype[] = {
	{
		/* keep it for coldfire */
		.name = DRIVER_NAME,
		.driver_data = 0,
	}, {
		.name = "imx25-fec",
		.driver_data = FEC_QUIRK_USE_GASKET,
	}, {
		.name = "imx27-fec",
		.driver_data = 0,
	}, {
		.name = "imx28-fec",
		.driver_data = FEC_QUIRK_ENET_MAC | FEC_QUIRK_SWAP_FRAME,
	}, {
		.name = "imx6q-fec",
		.driver_data = FEC_QUIRK_ENET_MAC | FEC_QUIRK_HAS_GBIT |
				FEC_QUIRK_HAS_BUFDESC_EX | FEC_QUIRK_HAS_CSUM,
	}, {
		.name = "mvf600-fec",
		.driver_data = FEC_QUIRK_ENET_MAC,
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
};

static const struct of_device_id fec_dt_ids[] = {
	{ .compatible = "fsl,imx25-fec", .data = &fec_devtype[IMX25_FEC], },
	{ .compatible = "fsl,imx27-fec", .data = &fec_devtype[IMX27_FEC], },
	{ .compatible = "fsl,imx28-fec", .data = &fec_devtype[IMX28_FEC], },
	{ .compatible = "fsl,imx6q-fec", .data = &fec_devtype[IMX6Q_FEC], },
	{ .compatible = "fsl,mvf600-fec", .data = &fec_devtype[MVF600_FEC], },
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

#if (((RX_RING_SIZE + TX_RING_SIZE) * 32) > PAGE_SIZE)
#error "FEC: descriptor ring size constants too large"
#endif

/* Interrupt events/masks. */
#define FEC_ENET_HBERR	((uint)0x80000000)	/* Heartbeat error */
#define FEC_ENET_BABR	((uint)0x40000000)	/* Babbling receiver */
#define FEC_ENET_BABT	((uint)0x20000000)	/* Babbling transmitter */
#define FEC_ENET_GRA	((uint)0x10000000)	/* Graceful stop complete */
#define FEC_ENET_TXF	((uint)0x08000000)	/* Full frame transmitted */
#define FEC_ENET_TXB	((uint)0x04000000)	/* A buffer was transmitted */
#define FEC_ENET_RXF	((uint)0x02000000)	/* Full frame received */
#define FEC_ENET_RXB	((uint)0x01000000)	/* A buffer was received */
#define FEC_ENET_MII	((uint)0x00800000)	/* MII interrupt */
#define FEC_ENET_EBERR	((uint)0x00400000)	/* SDMA bus error */

#define FEC_DEFAULT_IMASK (FEC_ENET_TXF | FEC_ENET_RXF | FEC_ENET_MII)
#define FEC_RX_DISABLED_IMASK (FEC_DEFAULT_IMASK & (~FEC_ENET_RXF))

/* The FEC stores dest/src/type, data, and checksum for receive packets.
 */
#define PKT_MAXBUF_SIZE		1518
#define PKT_MINBUF_SIZE		64
#define PKT_MAXBLR_SIZE		1520

/* FEC receive acceleration */
#define FEC_RACC_IPDIS		(1 << 1)
#define FEC_RACC_PRODIS		(1 << 2)
#define FEC_RACC_OPTIONS	(FEC_RACC_IPDIS | FEC_RACC_PRODIS)

/*
 * The 5270/5271/5280/5282/532x RX control register also contains maximum frame
 * size bits. Other FEC hardware does not, so we need to take that into
 * account when setting it.
 */
#if defined(CONFIG_M523x) || defined(CONFIG_M527x) || defined(CONFIG_M528x) || \
    defined(CONFIG_M520x) || defined(CONFIG_M532x) || defined(CONFIG_ARM)
#define	OPT_FRAME_SIZE	(PKT_MAXBUF_SIZE << 16)
#else
#define	OPT_FRAME_SIZE	0
#endif

/* FEC MII MMFR bits definition */
#define FEC_MMFR_ST		(1 << 30)
#define FEC_MMFR_OP_READ	(2 << 28)
#define FEC_MMFR_OP_WRITE	(1 << 28)
#define FEC_MMFR_PA(v)		((v & 0x1f) << 23)
#define FEC_MMFR_RA(v)		((v & 0x1f) << 18)
#define FEC_MMFR_TA		(2 << 16)
#define FEC_MMFR_DATA(v)	(v & 0xffff)

#define FEC_MII_TIMEOUT		30000 /* us */

/* Transmitter timeout */
#define TX_TIMEOUT (2 * HZ)

#define FEC_PAUSE_FLAG_AUTONEG	0x1
#define FEC_PAUSE_FLAG_ENABLE	0x2

static int mii_cnt;

static struct bufdesc *fec_enet_get_nextdesc(struct bufdesc *bdp, int is_ex)
{
	struct bufdesc_ex *ex = (struct bufdesc_ex *)bdp;
	if (is_ex)
		return (struct bufdesc *)(ex + 1);
	else
		return bdp + 1;
}

static struct bufdesc *fec_enet_get_prevdesc(struct bufdesc *bdp, int is_ex)
{
	struct bufdesc_ex *ex = (struct bufdesc_ex *)bdp;
	if (is_ex)
		return (struct bufdesc *)(ex - 1);
	else
		return bdp - 1;
}

static void *swap_buffer(void *bufaddr, int len)
{
	int i;
	unsigned int *buf = bufaddr;

	for (i = 0; i < DIV_ROUND_UP(len, 4); i++, buf++)
		*buf = cpu_to_be32(*buf);

	return bufaddr;
}

static int
fec_enet_clear_csum(struct sk_buff *skb, struct net_device *ndev)
{
	/* Only run for packets requiring a checksum. */
	if (skb->ip_summed != CHECKSUM_PARTIAL)
		return 0;

	if (unlikely(skb_cow_head(skb, 0)))
		return -1;

	*(__sum16 *)(skb->head + skb->csum_start + skb->csum_offset) = 0;

	return 0;
}

static netdev_tx_t
fec_enet_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	const struct platform_device_id *id_entry =
				platform_get_device_id(fep->pdev);
	struct bufdesc *bdp;
	void *bufaddr;
	unsigned short	status;
	unsigned int index;

	if (!fep->link) {
		/* Link is down or auto-negotiation is in progress. */
		return NETDEV_TX_BUSY;
	}

	/* Fill in a Tx ring entry */
	bdp = fep->cur_tx;

	status = bdp->cbd_sc;

	if (status & BD_ENET_TX_READY) {
		/* Ooops.  All transmit buffers are full.  Bail out.
		 * This should not happen, since ndev->tbusy should be set.
		 */
		netdev_err(ndev, "tx queue full!\n");
		return NETDEV_TX_BUSY;
	}

	/* Protocol checksum off-load for TCP and UDP. */
	if (fec_enet_clear_csum(skb, ndev)) {
		kfree_skb(skb);
		return NETDEV_TX_OK;
	}

	/* Clear all of the status flags */
	status &= ~BD_ENET_TX_STATS;

	/* Set buffer length and buffer pointer */
	bufaddr = skb->data;
	bdp->cbd_datlen = skb->len;

	/*
	 * On some FEC implementations data must be aligned on
	 * 4-byte boundaries. Use bounce buffers to copy data
	 * and get it aligned. Ugh.
	 */
	if (fep->bufdesc_ex)
		index = (struct bufdesc_ex *)bdp -
			(struct bufdesc_ex *)fep->tx_bd_base;
	else
		index = bdp - fep->tx_bd_base;

	if (((unsigned long) bufaddr) & FEC_ALIGNMENT) {
		memcpy(fep->tx_bounce[index], skb->data, skb->len);
		bufaddr = fep->tx_bounce[index];
	}

	/*
	 * Some design made an incorrect assumption on endian mode of
	 * the system that it's running on. As the result, driver has to
	 * swap every frame going to and coming from the controller.
	 */
	if (id_entry->driver_data & FEC_QUIRK_SWAP_FRAME)
		swap_buffer(bufaddr, skb->len);

	/* Save skb pointer */
	fep->tx_skbuff[index] = skb;

	/* Push the data cache so the CPM does not get stale memory
	 * data.
	 */
	bdp->cbd_bufaddr = dma_map_single(&fep->pdev->dev, bufaddr,
			FEC_ENET_TX_FRSIZE, DMA_TO_DEVICE);

	/* Send it on its way.  Tell FEC it's ready, interrupt when done,
	 * it's the last BD of the frame, and to put the CRC on the end.
	 */
	status |= (BD_ENET_TX_READY | BD_ENET_TX_INTR
			| BD_ENET_TX_LAST | BD_ENET_TX_TC);
	bdp->cbd_sc = status;

	if (fep->bufdesc_ex) {

		struct bufdesc_ex *ebdp = (struct bufdesc_ex *)bdp;
		ebdp->cbd_bdu = 0;
		if (unlikely(skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP &&
			fep->hwts_tx_en)) {
			ebdp->cbd_esc = (BD_ENET_TX_TS | BD_ENET_TX_INT);
			skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
		} else {
			ebdp->cbd_esc = BD_ENET_TX_INT;

			/* Enable protocol checksum flags
			 * We do not bother with the IP Checksum bits as they
			 * are done by the kernel
			 */
			if (skb->ip_summed == CHECKSUM_PARTIAL)
				ebdp->cbd_esc |= BD_ENET_TX_PINS;
		}
	}
	/* If this was the last BD in the ring, start at the beginning again. */
	if (status & BD_ENET_TX_WRAP)
		bdp = fep->tx_bd_base;
	else
		bdp = fec_enet_get_nextdesc(bdp, fep->bufdesc_ex);

	fep->cur_tx = bdp;

	if (fep->cur_tx == fep->dirty_tx)
		netif_stop_queue(ndev);

	/* Trigger transmission start */
	writel(0, fep->hwp + FEC_X_DES_ACTIVE);

	skb_tx_timestamp(skb);

	return NETDEV_TX_OK;
}

/* Init RX & TX buffer descriptors
 */
static void fec_enet_bd_init(struct net_device *dev)
{
	struct fec_enet_private *fep = netdev_priv(dev);
	struct bufdesc *bdp;
	unsigned int i;

	/* Initialize the receive buffer descriptors. */
	bdp = fep->rx_bd_base;
	for (i = 0; i < RX_RING_SIZE; i++) {

		/* Initialize the BD for every fragment in the page. */
		if (bdp->cbd_bufaddr)
			bdp->cbd_sc = BD_ENET_RX_EMPTY;
		else
			bdp->cbd_sc = 0;
		bdp = fec_enet_get_nextdesc(bdp, fep->bufdesc_ex);
	}

	/* Set the last buffer to wrap */
	bdp = fec_enet_get_prevdesc(bdp, fep->bufdesc_ex);
	bdp->cbd_sc |= BD_SC_WRAP;

	fep->cur_rx = fep->rx_bd_base;

	/* ...and the same for transmit */
	bdp = fep->tx_bd_base;
	fep->cur_tx = bdp;
	for (i = 0; i < TX_RING_SIZE; i++) {

		/* Initialize the BD for every fragment in the page. */
		bdp->cbd_sc = 0;
		if (bdp->cbd_bufaddr && fep->tx_skbuff[i]) {
			dev_kfree_skb_any(fep->tx_skbuff[i]);
			fep->tx_skbuff[i] = NULL;
		}
		bdp->cbd_bufaddr = 0;
		bdp = fec_enet_get_nextdesc(bdp, fep->bufdesc_ex);
	}

	/* Set the last buffer to wrap */
	bdp = fec_enet_get_prevdesc(bdp, fep->bufdesc_ex);
	bdp->cbd_sc |= BD_SC_WRAP;
	fep->dirty_tx = bdp;
}

/* This function is called to start or restart the FEC during a link
 * change.  This only happens when switching between half and full
 * duplex.
 */
static void
fec_restart(struct net_device *ndev, int duplex)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	const struct platform_device_id *id_entry =
				platform_get_device_id(fep->pdev);
	int i;
	u32 val;
	u32 temp_mac[2];
	u32 rcntl = OPT_FRAME_SIZE | 0x04;
	u32 ecntl = 0x2; /* ETHEREN */

	if (netif_running(ndev)) {
		netif_device_detach(ndev);
		napi_disable(&fep->napi);
		netif_stop_queue(ndev);
		netif_tx_lock_bh(ndev);
	}

	/* Whack a reset.  We should wait for this. */
	writel(1, fep->hwp + FEC_ECNTRL);
	udelay(10);

	/*
	 * enet-mac reset will reset mac address registers too,
	 * so need to reconfigure it.
	 */
	if (id_entry->driver_data & FEC_QUIRK_ENET_MAC) {
		memcpy(&temp_mac, ndev->dev_addr, ETH_ALEN);
		writel(cpu_to_be32(temp_mac[0]), fep->hwp + FEC_ADDR_LOW);
		writel(cpu_to_be32(temp_mac[1]), fep->hwp + FEC_ADDR_HIGH);
	}

	/* Clear any outstanding interrupt. */
	writel(0xffc00000, fep->hwp + FEC_IEVENT);

	/* Reset all multicast.	*/
	writel(0, fep->hwp + FEC_GRP_HASH_TABLE_HIGH);
	writel(0, fep->hwp + FEC_GRP_HASH_TABLE_LOW);
#ifndef CONFIG_M5272
	writel(0, fep->hwp + FEC_HASH_TABLE_HIGH);
	writel(0, fep->hwp + FEC_HASH_TABLE_LOW);
#endif

	/* Set maximum receive buffer size. */
	writel(PKT_MAXBLR_SIZE, fep->hwp + FEC_R_BUFF_SIZE);

	fec_enet_bd_init(ndev);

	/* Set receive and transmit descriptor base. */
	writel(fep->bd_dma, fep->hwp + FEC_R_DES_START);
	if (fep->bufdesc_ex)
		writel((unsigned long)fep->bd_dma + sizeof(struct bufdesc_ex)
			* RX_RING_SIZE, fep->hwp + FEC_X_DES_START);
	else
		writel((unsigned long)fep->bd_dma + sizeof(struct bufdesc)
			* RX_RING_SIZE,	fep->hwp + FEC_X_DES_START);


	for (i = 0; i <= TX_RING_MOD_MASK; i++) {
		if (fep->tx_skbuff[i]) {
			dev_kfree_skb_any(fep->tx_skbuff[i]);
			fep->tx_skbuff[i] = NULL;
		}
	}

	/* Enable MII mode */
	if (duplex) {
		/* FD enable */
		writel(0x04, fep->hwp + FEC_X_CNTRL);
	} else {
		/* No Rcv on Xmit */
		rcntl |= 0x02;
		writel(0x0, fep->hwp + FEC_X_CNTRL);
	}

	fep->full_duplex = duplex;

	/* Set MII speed */
	writel(fep->phy_speed, fep->hwp + FEC_MII_SPEED);

	/* set RX checksum */
	val = readl(fep->hwp + FEC_RACC);
	if (fep->csum_flags & FLAG_RX_CSUM_ENABLED)
		val |= FEC_RACC_OPTIONS;
	else
		val &= ~FEC_RACC_OPTIONS;
	writel(val, fep->hwp + FEC_RACC);

	/*
	 * The phy interface and speed need to get configured
	 * differently on enet-mac.
	 */
	if (id_entry->driver_data & FEC_QUIRK_ENET_MAC) {
		/* Enable flow control and length check */
		rcntl |= 0x40000000 | 0x00000020;

		/* RGMII, RMII or MII */
		if (fep->phy_interface == PHY_INTERFACE_MODE_RGMII)
			rcntl |= (1 << 6);
		else if (fep->phy_interface == PHY_INTERFACE_MODE_RMII)
			rcntl |= (1 << 8);
		else
			rcntl &= ~(1 << 8);

		/* 1G, 100M or 10M */
		if (fep->phy_dev) {
			if (fep->phy_dev->speed == SPEED_1000)
				ecntl |= (1 << 5);
			else if (fep->phy_dev->speed == SPEED_100)
				rcntl &= ~(1 << 9);
			else
				rcntl |= (1 << 9);
		}
	} else {
#ifdef FEC_MIIGSK_ENR
		if (id_entry->driver_data & FEC_QUIRK_USE_GASKET) {
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
			if (fep->phy_dev && fep->phy_dev->speed == SPEED_10)
				cfgr |= BM_MIIGSK_CFGR_FRCONT_10M;
			writel(cfgr, fep->hwp + FEC_MIIGSK_CFGR);

			/* re-enable the gasket */
			writel(2, fep->hwp + FEC_MIIGSK_ENR);
		}
#endif
	}

	/* enable pause frame*/
	if ((fep->pause_flag & FEC_PAUSE_FLAG_ENABLE) ||
	    ((fep->pause_flag & FEC_PAUSE_FLAG_AUTONEG) &&
	     fep->phy_dev && fep->phy_dev->pause)) {
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

	writel(rcntl, fep->hwp + FEC_R_CNTRL);

	if (id_entry->driver_data & FEC_QUIRK_ENET_MAC) {
		/* enable ENET endian swap */
		ecntl |= (1 << 8);
		/* enable ENET store and forward mode */
		writel(1 << 8, fep->hwp + FEC_X_WMRK);
	}

	if (fep->bufdesc_ex)
		ecntl |= (1 << 4);

#ifndef CONFIG_M5272
	/* Disable, clear, and enable the MIB */
	writel(1 << 31, fep->hwp + FEC_MIB_CTRLSTAT);
	for (i = RMON_T_DROP; i < IEEE_R_OCTETS_OK; i++)
		writel(0, fep->hwp + i);
	writel(0, fep->hwp + FEC_MIB_CTRLSTAT);
#endif

	/* And last, enable the transmit and receive processing */
	writel(ecntl, fep->hwp + FEC_ECNTRL);
	writel(0, fep->hwp + FEC_R_DES_ACTIVE);

	if (fep->bufdesc_ex)
		fec_ptp_start_cyclecounter(ndev);

	/* Enable interrupts we wish to service */
	writel(FEC_DEFAULT_IMASK, fep->hwp + FEC_IMASK);

	if (netif_running(ndev)) {
		netif_tx_unlock_bh(ndev);
		netif_wake_queue(ndev);
		napi_enable(&fep->napi);
		netif_device_attach(ndev);
	}
}

static void
fec_stop(struct net_device *ndev)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	const struct platform_device_id *id_entry =
				platform_get_device_id(fep->pdev);
	u32 rmii_mode = readl(fep->hwp + FEC_R_CNTRL) & (1 << 8);

	/* We cannot expect a graceful transmit stop without link !!! */
	if (fep->link) {
		writel(1, fep->hwp + FEC_X_CNTRL); /* Graceful transmit stop */
		udelay(10);
		if (!(readl(fep->hwp + FEC_IEVENT) & FEC_ENET_GRA))
			netdev_err(ndev, "Graceful transmit stop did not complete!\n");
	}

	/* Whack a reset.  We should wait for this. */
	writel(1, fep->hwp + FEC_ECNTRL);
	udelay(10);
	writel(fep->phy_speed, fep->hwp + FEC_MII_SPEED);
	writel(FEC_DEFAULT_IMASK, fep->hwp + FEC_IMASK);

	/* We have to keep ENET enabled to have MII interrupt stay working */
	if (id_entry->driver_data & FEC_QUIRK_ENET_MAC) {
		writel(2, fep->hwp + FEC_ECNTRL);
		writel(rmii_mode, fep->hwp + FEC_R_CNTRL);
	}
}


static void
fec_timeout(struct net_device *ndev)
{
	struct fec_enet_private *fep = netdev_priv(ndev);

	ndev->stats.tx_errors++;

	fep->delay_work.timeout = true;
	schedule_delayed_work(&(fep->delay_work.delay_work), 0);
}

static void fec_enet_work(struct work_struct *work)
{
	struct fec_enet_private *fep =
		container_of(work,
			     struct fec_enet_private,
			     delay_work.delay_work.work);

	if (fep->delay_work.timeout) {
		fep->delay_work.timeout = false;
		fec_restart(fep->netdev, fep->full_duplex);
		netif_wake_queue(fep->netdev);
	}
}

static void
fec_enet_tx(struct net_device *ndev)
{
	struct	fec_enet_private *fep;
	struct bufdesc *bdp;
	unsigned short status;
	struct	sk_buff	*skb;
	int	index = 0;

	fep = netdev_priv(ndev);
	bdp = fep->dirty_tx;

	/* get next bdp of dirty_tx */
	if (bdp->cbd_sc & BD_ENET_TX_WRAP)
		bdp = fep->tx_bd_base;
	else
		bdp = fec_enet_get_nextdesc(bdp, fep->bufdesc_ex);

	while (((status = bdp->cbd_sc) & BD_ENET_TX_READY) == 0) {

		/* current queue is empty */
		if (bdp == fep->cur_tx)
			break;

		if (fep->bufdesc_ex)
			index = (struct bufdesc_ex *)bdp -
				(struct bufdesc_ex *)fep->tx_bd_base;
		else
			index = bdp - fep->tx_bd_base;

		dma_unmap_single(&fep->pdev->dev, bdp->cbd_bufaddr,
				FEC_ENET_TX_FRSIZE, DMA_TO_DEVICE);
		bdp->cbd_bufaddr = 0;

		skb = fep->tx_skbuff[index];

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
		}

		if (unlikely(skb_shinfo(skb)->tx_flags & SKBTX_IN_PROGRESS) &&
			fep->bufdesc_ex) {
			struct skb_shared_hwtstamps shhwtstamps;
			unsigned long flags;
			struct bufdesc_ex *ebdp = (struct bufdesc_ex *)bdp;

			memset(&shhwtstamps, 0, sizeof(shhwtstamps));
			spin_lock_irqsave(&fep->tmreg_lock, flags);
			shhwtstamps.hwtstamp = ns_to_ktime(
				timecounter_cyc2time(&fep->tc, ebdp->ts));
			spin_unlock_irqrestore(&fep->tmreg_lock, flags);
			skb_tstamp_tx(skb, &shhwtstamps);
		}

		if (status & BD_ENET_TX_READY)
			netdev_err(ndev, "HEY! Enet xmit interrupt and TX_READY\n");

		/* Deferred means some collisions occurred during transmit,
		 * but we eventually sent the packet OK.
		 */
		if (status & BD_ENET_TX_DEF)
			ndev->stats.collisions++;

		/* Free the sk buffer associated with this last transmit */
		dev_kfree_skb_any(skb);
		fep->tx_skbuff[index] = NULL;

		fep->dirty_tx = bdp;

		/* Update pointer to next buffer descriptor to be transmitted */
		if (status & BD_ENET_TX_WRAP)
			bdp = fep->tx_bd_base;
		else
			bdp = fec_enet_get_nextdesc(bdp, fep->bufdesc_ex);

		/* Since we have freed up a buffer, the ring is no longer full
		 */
		if (fep->dirty_tx != fep->cur_tx) {
			if (netif_queue_stopped(ndev))
				netif_wake_queue(ndev);
		}
	}
	return;
}


/* During a receive, the cur_rx points to the current incoming buffer.
 * When we update through the ring, if the next incoming buffer has
 * not been given to the system, we just set the empty indicator,
 * effectively tossing the packet.
 */
static int
fec_enet_rx(struct net_device *ndev, int budget)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	const struct platform_device_id *id_entry =
				platform_get_device_id(fep->pdev);
	struct bufdesc *bdp;
	unsigned short status;
	struct	sk_buff	*skb;
	ushort	pkt_len;
	__u8 *data;
	int	pkt_received = 0;

#ifdef CONFIG_M532x
	flush_cache_all();
#endif

	/* First, grab all of the stats for the incoming packet.
	 * These get messed up if we get called due to a busy condition.
	 */
	bdp = fep->cur_rx;

	while (!((status = bdp->cbd_sc) & BD_ENET_RX_EMPTY)) {

		if (pkt_received >= budget)
			break;
		pkt_received++;

		/* Since we have allocated space to hold a complete frame,
		 * the last indicator should be set.
		 */
		if ((status & BD_ENET_RX_LAST) == 0)
			netdev_err(ndev, "rcv is not +last\n");

		if (!fep->opened)
			goto rx_processing_done;

		/* Check for errors. */
		if (status & (BD_ENET_RX_LG | BD_ENET_RX_SH | BD_ENET_RX_NO |
			   BD_ENET_RX_CR | BD_ENET_RX_OV)) {
			ndev->stats.rx_errors++;
			if (status & (BD_ENET_RX_LG | BD_ENET_RX_SH)) {
				/* Frame too long or too short. */
				ndev->stats.rx_length_errors++;
			}
			if (status & BD_ENET_RX_NO)	/* Frame alignment */
				ndev->stats.rx_frame_errors++;
			if (status & BD_ENET_RX_CR)	/* CRC Error */
				ndev->stats.rx_crc_errors++;
			if (status & BD_ENET_RX_OV)	/* FIFO overrun */
				ndev->stats.rx_fifo_errors++;
		}

		/* Report late collisions as a frame error.
		 * On this error, the BD is closed, but we don't know what we
		 * have in the buffer.  So, just drop this frame on the floor.
		 */
		if (status & BD_ENET_RX_CL) {
			ndev->stats.rx_errors++;
			ndev->stats.rx_frame_errors++;
			goto rx_processing_done;
		}

		/* Process the incoming frame. */
		ndev->stats.rx_packets++;
		pkt_len = bdp->cbd_datlen;
		ndev->stats.rx_bytes += pkt_len;
		data = (__u8*)__va(bdp->cbd_bufaddr);

		dma_unmap_single(&fep->pdev->dev, bdp->cbd_bufaddr,
				FEC_ENET_TX_FRSIZE, DMA_FROM_DEVICE);

		if (id_entry->driver_data & FEC_QUIRK_SWAP_FRAME)
			swap_buffer(data, pkt_len);

		/* This does 16 byte alignment, exactly what we need.
		 * The packet length includes FCS, but we don't want to
		 * include that when passing upstream as it messes up
		 * bridging applications.
		 */
		skb = netdev_alloc_skb(ndev, pkt_len - 4 + NET_IP_ALIGN);

		if (unlikely(!skb)) {
			ndev->stats.rx_dropped++;
		} else {
			skb_reserve(skb, NET_IP_ALIGN);
			skb_put(skb, pkt_len - 4);	/* Make room */
			skb_copy_to_linear_data(skb, data, pkt_len - 4);
			skb->protocol = eth_type_trans(skb, ndev);

			/* Get receive timestamp from the skb */
			if (fep->hwts_rx_en && fep->bufdesc_ex) {
				struct skb_shared_hwtstamps *shhwtstamps =
							    skb_hwtstamps(skb);
				unsigned long flags;
				struct bufdesc_ex *ebdp =
					(struct bufdesc_ex *)bdp;

				memset(shhwtstamps, 0, sizeof(*shhwtstamps));

				spin_lock_irqsave(&fep->tmreg_lock, flags);
				shhwtstamps->hwtstamp = ns_to_ktime(
				    timecounter_cyc2time(&fep->tc, ebdp->ts));
				spin_unlock_irqrestore(&fep->tmreg_lock, flags);
			}

			if (fep->bufdesc_ex &&
				(fep->csum_flags & FLAG_RX_CSUM_ENABLED)) {
				struct bufdesc_ex *ebdp =
					(struct bufdesc_ex *)bdp;
				if (!(ebdp->cbd_esc & FLAG_RX_CSUM_ERROR)) {
					/* don't check it */
					skb->ip_summed = CHECKSUM_UNNECESSARY;
				} else {
					skb_checksum_none_assert(skb);
				}
			}

			if (!skb_defer_rx_timestamp(skb))
				napi_gro_receive(&fep->napi, skb);
		}

		bdp->cbd_bufaddr = dma_map_single(&fep->pdev->dev, data,
				FEC_ENET_TX_FRSIZE, DMA_FROM_DEVICE);
rx_processing_done:
		/* Clear the status flags for this buffer */
		status &= ~BD_ENET_RX_STATS;

		/* Mark the buffer empty */
		status |= BD_ENET_RX_EMPTY;
		bdp->cbd_sc = status;

		if (fep->bufdesc_ex) {
			struct bufdesc_ex *ebdp = (struct bufdesc_ex *)bdp;

			ebdp->cbd_esc = BD_ENET_RX_INT;
			ebdp->cbd_prot = 0;
			ebdp->cbd_bdu = 0;
		}

		/* Update BD pointer to next entry */
		if (status & BD_ENET_RX_WRAP)
			bdp = fep->rx_bd_base;
		else
			bdp = fec_enet_get_nextdesc(bdp, fep->bufdesc_ex);
		/* Doing this here will keep the FEC running while we process
		 * incoming frames.  On a heavily loaded network, we should be
		 * able to keep up at the expense of system resources.
		 */
		writel(0, fep->hwp + FEC_R_DES_ACTIVE);
	}
	fep->cur_rx = bdp;

	return pkt_received;
}

static irqreturn_t
fec_enet_interrupt(int irq, void *dev_id)
{
	struct net_device *ndev = dev_id;
	struct fec_enet_private *fep = netdev_priv(ndev);
	uint int_events;
	irqreturn_t ret = IRQ_NONE;

	do {
		int_events = readl(fep->hwp + FEC_IEVENT);
		writel(int_events, fep->hwp + FEC_IEVENT);

		if (int_events & (FEC_ENET_RXF | FEC_ENET_TXF)) {
			ret = IRQ_HANDLED;

			/* Disable the RX interrupt */
			if (napi_schedule_prep(&fep->napi)) {
				writel(FEC_RX_DISABLED_IMASK,
					fep->hwp + FEC_IMASK);
				__napi_schedule(&fep->napi);
			}
		}

		if (int_events & FEC_ENET_MII) {
			ret = IRQ_HANDLED;
			complete(&fep->mdio_done);
		}
	} while (int_events);

	return ret;
}

static int fec_enet_rx_napi(struct napi_struct *napi, int budget)
{
	struct net_device *ndev = napi->dev;
	int pkts = fec_enet_rx(ndev, budget);
	struct fec_enet_private *fep = netdev_priv(ndev);

	fec_enet_tx(ndev);

	if (pkts < budget) {
		napi_complete(napi);
		writel(FEC_DEFAULT_IMASK, fep->hwp + FEC_IMASK);
	}
	return pkts;
}

/* ------------------------------------------------------------------------- */
static void fec_get_mac(struct net_device *ndev)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	struct fec_platform_data *pdata = fep->pdev->dev.platform_data;
	unsigned char *iap, tmpaddr[ETH_ALEN];

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
			const char *mac = of_get_mac_address(np);
			if (mac)
				iap = (unsigned char *) mac;
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
		if (pdata)
			iap = (unsigned char *)&pdata->mac;
#endif
	}

	/*
	 * 4) FEC mac registers set by bootloader
	 */
	if (!is_valid_ether_addr(iap)) {
		*((unsigned long *) &tmpaddr[0]) =
			be32_to_cpu(readl(fep->hwp + FEC_ADDR_LOW));
		*((unsigned short *) &tmpaddr[4]) =
			be16_to_cpu(readl(fep->hwp + FEC_ADDR_HIGH) >> 16);
		iap = &tmpaddr[0];
	}

	/*
	 * 5) random mac address
	 */
	if (!is_valid_ether_addr(iap)) {
		/* Report it and use a random ethernet address instead */
		netdev_err(ndev, "Invalid MAC address: %pM\n", iap);
		eth_hw_addr_random(ndev);
		netdev_info(ndev, "Using random MAC address: %pM\n",
			    ndev->dev_addr);
		return;
	}

	memcpy(ndev->dev_addr, iap, ETH_ALEN);

	/* Adjust MAC if using macaddr */
	if (iap == macaddr)
		 ndev->dev_addr[ETH_ALEN-1] = macaddr[ETH_ALEN-1] + fep->dev_id;
}

/* ------------------------------------------------------------------------- */

/*
 * Phy section
 */
static void fec_enet_adjust_link(struct net_device *ndev)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	struct phy_device *phy_dev = fep->phy_dev;
	int status_change = 0;

	/* Prevent a state halted on mii error */
	if (fep->mii_timeout && phy_dev->state == PHY_HALTED) {
		phy_dev->state = PHY_RESUMING;
		return;
	}

	if (phy_dev->link) {
		if (!fep->link) {
			fep->link = phy_dev->link;
			status_change = 1;
		}

		if (fep->full_duplex != phy_dev->duplex)
			status_change = 1;

		if (phy_dev->speed != fep->speed) {
			fep->speed = phy_dev->speed;
			status_change = 1;
		}

		/* if any of the above changed restart the FEC */
		if (status_change)
			fec_restart(ndev, phy_dev->duplex);
	} else {
		if (fep->link) {
			fec_stop(ndev);
			fep->link = phy_dev->link;
			status_change = 1;
		}
	}

	if (status_change)
		phy_print_status(phy_dev);
}

static int fec_enet_mdio_read(struct mii_bus *bus, int mii_id, int regnum)
{
	struct fec_enet_private *fep = bus->priv;
	unsigned long time_left;

	fep->mii_timeout = 0;
	init_completion(&fep->mdio_done);

	/* start a read op */
	writel(FEC_MMFR_ST | FEC_MMFR_OP_READ |
		FEC_MMFR_PA(mii_id) | FEC_MMFR_RA(regnum) |
		FEC_MMFR_TA, fep->hwp + FEC_MII_DATA);

	/* wait for end of transfer */
	time_left = wait_for_completion_timeout(&fep->mdio_done,
			usecs_to_jiffies(FEC_MII_TIMEOUT));
	if (time_left == 0) {
		fep->mii_timeout = 1;
		netdev_err(fep->netdev, "MDIO read timeout\n");
		return -ETIMEDOUT;
	}

	/* return value */
	return FEC_MMFR_DATA(readl(fep->hwp + FEC_MII_DATA));
}

static int fec_enet_mdio_write(struct mii_bus *bus, int mii_id, int regnum,
			   u16 value)
{
	struct fec_enet_private *fep = bus->priv;
	unsigned long time_left;

	fep->mii_timeout = 0;
	init_completion(&fep->mdio_done);

	/* start a write op */
	writel(FEC_MMFR_ST | FEC_MMFR_OP_WRITE |
		FEC_MMFR_PA(mii_id) | FEC_MMFR_RA(regnum) |
		FEC_MMFR_TA | FEC_MMFR_DATA(value),
		fep->hwp + FEC_MII_DATA);

	/* wait for end of transfer */
	time_left = wait_for_completion_timeout(&fep->mdio_done,
			usecs_to_jiffies(FEC_MII_TIMEOUT));
	if (time_left == 0) {
		fep->mii_timeout = 1;
		netdev_err(fep->netdev, "MDIO write timeout\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static int fec_enet_mdio_reset(struct mii_bus *bus)
{
	return 0;
}

static int fec_enet_mii_probe(struct net_device *ndev)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	const struct platform_device_id *id_entry =
				platform_get_device_id(fep->pdev);
	struct phy_device *phy_dev = NULL;
	char mdio_bus_id[MII_BUS_ID_SIZE];
	char phy_name[MII_BUS_ID_SIZE + 3];
	int phy_id;
	int dev_id = fep->dev_id;

	fep->phy_dev = NULL;

	/* check for attached phy */
	for (phy_id = 0; (phy_id < PHY_MAX_ADDR); phy_id++) {
		if ((fep->mii_bus->phy_mask & (1 << phy_id)))
			continue;
		if (fep->mii_bus->phy_map[phy_id] == NULL)
			continue;
		if (fep->mii_bus->phy_map[phy_id]->phy_id == 0)
			continue;
		if (dev_id--)
			continue;
		strncpy(mdio_bus_id, fep->mii_bus->id, MII_BUS_ID_SIZE);
		break;
	}

	if (phy_id >= PHY_MAX_ADDR) {
		netdev_info(ndev, "no PHY, assuming direct connection to switch\n");
		strncpy(mdio_bus_id, "fixed-0", MII_BUS_ID_SIZE);
		phy_id = 0;
	}

	snprintf(phy_name, sizeof(phy_name), PHY_ID_FMT, mdio_bus_id, phy_id);
	phy_dev = phy_connect(ndev, phy_name, &fec_enet_adjust_link,
			      fep->phy_interface);
	if (IS_ERR(phy_dev)) {
		netdev_err(ndev, "could not attach to PHY\n");
		return PTR_ERR(phy_dev);
	}

	/* mask with MAC supported features */
	if (id_entry->driver_data & FEC_QUIRK_HAS_GBIT) {
		phy_dev->supported &= PHY_GBIT_FEATURES;
		phy_dev->supported |= SUPPORTED_Pause;
	}
	else
		phy_dev->supported &= PHY_BASIC_FEATURES;

	phy_dev->advertising = phy_dev->supported;

	fep->phy_dev = phy_dev;
	fep->link = 0;
	fep->full_duplex = 0;

	netdev_info(ndev, "Freescale FEC PHY driver [%s] (mii_bus:phy_addr=%s, irq=%d)\n",
		    fep->phy_dev->drv->name, dev_name(&fep->phy_dev->dev),
		    fep->phy_dev->irq);

	return 0;
}

static int fec_enet_mii_init(struct platform_device *pdev)
{
	static struct mii_bus *fec0_mii_bus;
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct fec_enet_private *fep = netdev_priv(ndev);
	const struct platform_device_id *id_entry =
				platform_get_device_id(fep->pdev);
	int err = -ENXIO, i;

	/*
	 * The dual fec interfaces are not equivalent with enet-mac.
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
	if ((id_entry->driver_data & FEC_QUIRK_ENET_MAC) && fep->dev_id > 0) {
		/* fec1 uses fec0 mii_bus */
		if (mii_cnt && fec0_mii_bus) {
			fep->mii_bus = fec0_mii_bus;
			mii_cnt++;
			return 0;
		}
		return -ENOENT;
	}

	fep->mii_timeout = 0;

	/*
	 * Set MII speed to 2.5 MHz (= clk_get_rate() / 2 * phy_speed)
	 *
	 * The formula for FEC MDC is 'ref_freq / (MII_SPEED x 2)' while
	 * for ENET-MAC is 'ref_freq / ((MII_SPEED + 1) x 2)'.  The i.MX28
	 * Reference Manual has an error on this, and gets fixed on i.MX6Q
	 * document.
	 */
	fep->phy_speed = DIV_ROUND_UP(clk_get_rate(fep->clk_ahb), 5000000);
	if (id_entry->driver_data & FEC_QUIRK_ENET_MAC)
		fep->phy_speed--;
	fep->phy_speed <<= 1;
	writel(fep->phy_speed, fep->hwp + FEC_MII_SPEED);

	fep->mii_bus = mdiobus_alloc();
	if (fep->mii_bus == NULL) {
		err = -ENOMEM;
		goto err_out;
	}

	fep->mii_bus->name = "fec_enet_mii_bus";
	fep->mii_bus->read = fec_enet_mdio_read;
	fep->mii_bus->write = fec_enet_mdio_write;
	fep->mii_bus->reset = fec_enet_mdio_reset;
	snprintf(fep->mii_bus->id, MII_BUS_ID_SIZE, "%s-%x",
		pdev->name, fep->dev_id + 1);
	fep->mii_bus->priv = fep;
	fep->mii_bus->parent = &pdev->dev;

	fep->mii_bus->irq = kmalloc(sizeof(int) * PHY_MAX_ADDR, GFP_KERNEL);
	if (!fep->mii_bus->irq) {
		err = -ENOMEM;
		goto err_out_free_mdiobus;
	}

	for (i = 0; i < PHY_MAX_ADDR; i++)
		fep->mii_bus->irq[i] = PHY_POLL;

	if (mdiobus_register(fep->mii_bus))
		goto err_out_free_mdio_irq;

	mii_cnt++;

	/* save fec0 mii_bus */
	if (id_entry->driver_data & FEC_QUIRK_ENET_MAC)
		fec0_mii_bus = fep->mii_bus;

	return 0;

err_out_free_mdio_irq:
	kfree(fep->mii_bus->irq);
err_out_free_mdiobus:
	mdiobus_free(fep->mii_bus);
err_out:
	return err;
}

static void fec_enet_mii_remove(struct fec_enet_private *fep)
{
	if (--mii_cnt == 0) {
		mdiobus_unregister(fep->mii_bus);
		kfree(fep->mii_bus->irq);
		mdiobus_free(fep->mii_bus);
	}
}

static int fec_enet_get_settings(struct net_device *ndev,
				  struct ethtool_cmd *cmd)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	struct phy_device *phydev = fep->phy_dev;

	if (!phydev)
		return -ENODEV;

	return phy_ethtool_gset(phydev, cmd);
}

static int fec_enet_set_settings(struct net_device *ndev,
				 struct ethtool_cmd *cmd)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	struct phy_device *phydev = fep->phy_dev;

	if (!phydev)
		return -ENODEV;

	return phy_ethtool_sset(phydev, cmd);
}

static void fec_enet_get_drvinfo(struct net_device *ndev,
				 struct ethtool_drvinfo *info)
{
	struct fec_enet_private *fep = netdev_priv(ndev);

	strlcpy(info->driver, fep->pdev->dev.driver->name,
		sizeof(info->driver));
	strlcpy(info->version, "Revision: 1.0", sizeof(info->version));
	strlcpy(info->bus_info, dev_name(&ndev->dev), sizeof(info->bus_info));
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

	if (pause->tx_pause != pause->rx_pause) {
		netdev_info(ndev,
			"hardware only support enable/disable both tx and rx");
		return -EINVAL;
	}

	fep->pause_flag = 0;

	/* tx pause must be same as rx pause */
	fep->pause_flag |= pause->rx_pause ? FEC_PAUSE_FLAG_ENABLE : 0;
	fep->pause_flag |= pause->autoneg ? FEC_PAUSE_FLAG_AUTONEG : 0;

	if (pause->rx_pause || pause->autoneg) {
		fep->phy_dev->supported |= ADVERTISED_Pause;
		fep->phy_dev->advertising |= ADVERTISED_Pause;
	} else {
		fep->phy_dev->supported &= ~ADVERTISED_Pause;
		fep->phy_dev->advertising &= ~ADVERTISED_Pause;
	}

	if (pause->autoneg) {
		if (netif_running(ndev))
			fec_stop(ndev);
		phy_start_aneg(fep->phy_dev);
	}
	if (netif_running(ndev))
		fec_restart(ndev, 0);

	return 0;
}

#ifndef CONFIG_M5272
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

static void fec_enet_get_ethtool_stats(struct net_device *dev,
	struct ethtool_stats *stats, u64 *data)
{
	struct fec_enet_private *fep = netdev_priv(dev);
	int i;

	for (i = 0; i < ARRAY_SIZE(fec_stats); i++)
		data[i] = readl(fep->hwp + fec_stats[i].offset);
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
	}
}

static int fec_enet_get_sset_count(struct net_device *dev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return ARRAY_SIZE(fec_stats);
	default:
		return -EOPNOTSUPP;
	}
}
#endif

static int fec_enet_nway_reset(struct net_device *dev)
{
	struct fec_enet_private *fep = netdev_priv(dev);
	struct phy_device *phydev = fep->phy_dev;

	if (!phydev)
		return -ENODEV;

	return genphy_restart_aneg(phydev);
}

static const struct ethtool_ops fec_enet_ethtool_ops = {
	.get_pauseparam		= fec_enet_get_pauseparam,
	.set_pauseparam		= fec_enet_set_pauseparam,
	.get_settings		= fec_enet_get_settings,
	.set_settings		= fec_enet_set_settings,
	.get_drvinfo		= fec_enet_get_drvinfo,
	.get_link		= ethtool_op_get_link,
	.get_ts_info		= fec_enet_get_ts_info,
	.nway_reset		= fec_enet_nway_reset,
#ifndef CONFIG_M5272
	.get_ethtool_stats	= fec_enet_get_ethtool_stats,
	.get_strings		= fec_enet_get_strings,
	.get_sset_count		= fec_enet_get_sset_count,
#endif
};

static int fec_enet_ioctl(struct net_device *ndev, struct ifreq *rq, int cmd)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	struct phy_device *phydev = fep->phy_dev;

	if (!netif_running(ndev))
		return -EINVAL;

	if (!phydev)
		return -ENODEV;

	if (cmd == SIOCSHWTSTAMP && fep->bufdesc_ex)
		return fec_ptp_ioctl(ndev, rq, cmd);

	return phy_mii_ioctl(phydev, rq, cmd);
}

static void fec_enet_free_buffers(struct net_device *ndev)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	unsigned int i;
	struct sk_buff *skb;
	struct bufdesc	*bdp;

	bdp = fep->rx_bd_base;
	for (i = 0; i < RX_RING_SIZE; i++) {
		skb = fep->rx_skbuff[i];

		if (bdp->cbd_bufaddr)
			dma_unmap_single(&fep->pdev->dev, bdp->cbd_bufaddr,
					FEC_ENET_RX_FRSIZE, DMA_FROM_DEVICE);
		if (skb)
			dev_kfree_skb(skb);
		bdp = fec_enet_get_nextdesc(bdp, fep->bufdesc_ex);
	}

	bdp = fep->tx_bd_base;
	for (i = 0; i < TX_RING_SIZE; i++)
		kfree(fep->tx_bounce[i]);
}

static int fec_enet_alloc_buffers(struct net_device *ndev)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	unsigned int i;
	struct sk_buff *skb;
	struct bufdesc	*bdp;

	bdp = fep->rx_bd_base;
	for (i = 0; i < RX_RING_SIZE; i++) {
		skb = netdev_alloc_skb(ndev, FEC_ENET_RX_FRSIZE);
		if (!skb) {
			fec_enet_free_buffers(ndev);
			return -ENOMEM;
		}
		fep->rx_skbuff[i] = skb;

		bdp->cbd_bufaddr = dma_map_single(&fep->pdev->dev, skb->data,
				FEC_ENET_RX_FRSIZE, DMA_FROM_DEVICE);
		bdp->cbd_sc = BD_ENET_RX_EMPTY;

		if (fep->bufdesc_ex) {
			struct bufdesc_ex *ebdp = (struct bufdesc_ex *)bdp;
			ebdp->cbd_esc = BD_ENET_RX_INT;
		}

		bdp = fec_enet_get_nextdesc(bdp, fep->bufdesc_ex);
	}

	/* Set the last buffer to wrap. */
	bdp = fec_enet_get_prevdesc(bdp, fep->bufdesc_ex);
	bdp->cbd_sc |= BD_SC_WRAP;

	bdp = fep->tx_bd_base;
	for (i = 0; i < TX_RING_SIZE; i++) {
		fep->tx_bounce[i] = kmalloc(FEC_ENET_TX_FRSIZE, GFP_KERNEL);

		bdp->cbd_sc = 0;
		bdp->cbd_bufaddr = 0;

		if (fep->bufdesc_ex) {
			struct bufdesc_ex *ebdp = (struct bufdesc_ex *)bdp;
			ebdp->cbd_esc = BD_ENET_TX_INT;
		}

		bdp = fec_enet_get_nextdesc(bdp, fep->bufdesc_ex);
	}

	/* Set the last buffer to wrap. */
	bdp = fec_enet_get_prevdesc(bdp, fep->bufdesc_ex);
	bdp->cbd_sc |= BD_SC_WRAP;

	return 0;
}

static int
fec_enet_open(struct net_device *ndev)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	int ret;

	napi_enable(&fep->napi);

	/* I should reset the ring buffers here, but I don't yet know
	 * a simple way to do that.
	 */

	ret = fec_enet_alloc_buffers(ndev);
	if (ret)
		return ret;

	/* Probe and connect to PHY when open the interface */
	ret = fec_enet_mii_probe(ndev);
	if (ret) {
		fec_enet_free_buffers(ndev);
		return ret;
	}
	phy_start(fep->phy_dev);
	netif_start_queue(ndev);
	fep->opened = 1;
	return 0;
}

static int
fec_enet_close(struct net_device *ndev)
{
	struct fec_enet_private *fep = netdev_priv(ndev);

	/* Don't know what to do yet. */
	napi_disable(&fep->napi);
	fep->opened = 0;
	netif_stop_queue(ndev);
	fec_stop(ndev);

	if (fep->phy_dev) {
		phy_stop(fep->phy_dev);
		phy_disconnect(fep->phy_dev);
	}

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

#define HASH_BITS	6		/* #bits in hash */
#define CRC32_POLY	0xEDB88320

static void set_multicast_list(struct net_device *ndev)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	struct netdev_hw_addr *ha;
	unsigned int i, bit, data, crc, tmp;
	unsigned char hash;

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

	/* Clear filter and add the addresses in hash register
	 */
	writel(0, fep->hwp + FEC_GRP_HASH_TABLE_HIGH);
	writel(0, fep->hwp + FEC_GRP_HASH_TABLE_LOW);

	netdev_for_each_mc_addr(ha, ndev) {
		/* calculate crc32 value of mac address */
		crc = 0xffffffff;

		for (i = 0; i < ndev->addr_len; i++) {
			data = ha->addr[i];
			for (bit = 0; bit < 8; bit++, data >>= 1) {
				crc = (crc >> 1) ^
				(((crc ^ data) & 1) ? CRC32_POLY : 0);
			}
		}

		/* only upper 6 bits (HASH_BITS) are used
		 * which point to specific bit in he hash registers
		 */
		hash = (crc >> (32 - HASH_BITS)) & 0x3f;

		if (hash > 31) {
			tmp = readl(fep->hwp + FEC_GRP_HASH_TABLE_HIGH);
			tmp |= 1 << (hash - 32);
			writel(tmp, fep->hwp + FEC_GRP_HASH_TABLE_HIGH);
		} else {
			tmp = readl(fep->hwp + FEC_GRP_HASH_TABLE_LOW);
			tmp |= 1 << hash;
			writel(tmp, fep->hwp + FEC_GRP_HASH_TABLE_LOW);
		}
	}
}

/* Set a MAC change in hardware. */
static int
fec_set_mac_address(struct net_device *ndev, void *p)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	struct sockaddr *addr = p;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	memcpy(ndev->dev_addr, addr->sa_data, ndev->addr_len);

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

static int fec_set_features(struct net_device *netdev,
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

		if (netif_running(netdev)) {
			fec_stop(netdev);
			fec_restart(netdev, fep->phy_dev->duplex);
			netif_wake_queue(netdev);
		} else {
			fec_restart(netdev, fep->phy_dev->duplex);
		}
	}

	return 0;
}

static const struct net_device_ops fec_netdev_ops = {
	.ndo_open		= fec_enet_open,
	.ndo_stop		= fec_enet_close,
	.ndo_start_xmit		= fec_enet_start_xmit,
	.ndo_set_rx_mode	= set_multicast_list,
	.ndo_change_mtu		= eth_change_mtu,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_tx_timeout		= fec_timeout,
	.ndo_set_mac_address	= fec_set_mac_address,
	.ndo_do_ioctl		= fec_enet_ioctl,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= fec_poll_controller,
#endif
	.ndo_set_features	= fec_set_features,
};

 /*
  * XXX:  We need to clean up on failure exits here.
  *
  */
static int fec_enet_init(struct net_device *ndev)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	const struct platform_device_id *id_entry =
				platform_get_device_id(fep->pdev);
	struct bufdesc *cbd_base;

	/* Allocate memory for buffer descriptors. */
	cbd_base = dma_alloc_coherent(NULL, PAGE_SIZE, &fep->bd_dma,
				      GFP_KERNEL);
	if (!cbd_base)
		return -ENOMEM;

	memset(cbd_base, 0, PAGE_SIZE);

	fep->netdev = ndev;

	/* Get the Ethernet address */
	fec_get_mac(ndev);

	/* Set receive and transmit descriptor base. */
	fep->rx_bd_base = cbd_base;
	if (fep->bufdesc_ex)
		fep->tx_bd_base = (struct bufdesc *)
			(((struct bufdesc_ex *)cbd_base) + RX_RING_SIZE);
	else
		fep->tx_bd_base = cbd_base + RX_RING_SIZE;

	/* The FEC Ethernet specific entries in the device structure */
	ndev->watchdog_timeo = TX_TIMEOUT;
	ndev->netdev_ops = &fec_netdev_ops;
	ndev->ethtool_ops = &fec_enet_ethtool_ops;

	writel(FEC_RX_DISABLED_IMASK, fep->hwp + FEC_IMASK);
	netif_napi_add(ndev, &fep->napi, fec_enet_rx_napi, FEC_NAPI_WEIGHT);

	if (id_entry->driver_data & FEC_QUIRK_HAS_CSUM) {
		/* enable hw accelerator */
		ndev->features |= (NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM
				| NETIF_F_RXCSUM);
		ndev->hw_features |= (NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM
				| NETIF_F_RXCSUM);
		fep->csum_flags |= FLAG_RX_CSUM_ENABLED;
	}

	fec_restart(ndev, 0);

	return 0;
}

#ifdef CONFIG_OF
static void fec_reset_phy(struct platform_device *pdev)
{
	int err, phy_reset;
	int msec = 1;
	struct device_node *np = pdev->dev.of_node;

	if (!np)
		return;

	of_property_read_u32(np, "phy-reset-duration", &msec);
	/* A sane reset duration should not be longer than 1s */
	if (msec > 1000)
		msec = 1;

	phy_reset = of_get_named_gpio(np, "phy-reset-gpios", 0);
	if (!gpio_is_valid(phy_reset))
		return;

	err = devm_gpio_request_one(&pdev->dev, phy_reset,
				    GPIOF_OUT_INIT_LOW, "phy-reset");
	if (err) {
		dev_err(&pdev->dev, "failed to get phy-reset-gpios: %d\n", err);
		return;
	}
	msleep(msec);
	gpio_set_value(phy_reset, 1);
}
#else /* CONFIG_OF */
static void fec_reset_phy(struct platform_device *pdev)
{
	/*
	 * In case of platform probe, the reset has been done
	 * by machine code.
	 */
}
#endif /* CONFIG_OF */

static int
fec_probe(struct platform_device *pdev)
{
	struct fec_enet_private *fep;
	struct fec_platform_data *pdata;
	struct net_device *ndev;
	int i, irq, ret = 0;
	struct resource *r;
	const struct of_device_id *of_id;
	static int dev_id;

	of_id = of_match_device(fec_dt_ids, &pdev->dev);
	if (of_id)
		pdev->id_entry = of_id->data;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r)
		return -ENXIO;

	/* Init network device */
	ndev = alloc_etherdev(sizeof(struct fec_enet_private));
	if (!ndev)
		return -ENOMEM;

	SET_NETDEV_DEV(ndev, &pdev->dev);

	/* setup board info structure */
	fep = netdev_priv(ndev);

	/* default enable pause frame auto negotiation */
	if (pdev->id_entry &&
	    (pdev->id_entry->driver_data & FEC_QUIRK_HAS_GBIT))
		fep->pause_flag |= FEC_PAUSE_FLAG_AUTONEG;

	fep->hwp = devm_ioremap_resource(&pdev->dev, r);
	if (IS_ERR(fep->hwp)) {
		ret = PTR_ERR(fep->hwp);
		goto failed_ioremap;
	}

	fep->pdev = pdev;
	fep->dev_id = dev_id++;

	fep->bufdesc_ex = 0;

	platform_set_drvdata(pdev, ndev);

	ret = of_get_phy_mode(pdev->dev.of_node);
	if (ret < 0) {
		pdata = pdev->dev.platform_data;
		if (pdata)
			fep->phy_interface = pdata->phy;
		else
			fep->phy_interface = PHY_INTERFACE_MODE_MII;
	} else {
		fep->phy_interface = ret;
	}

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

	/* enet_out is optional, depends on board */
	fep->clk_enet_out = devm_clk_get(&pdev->dev, "enet_out");
	if (IS_ERR(fep->clk_enet_out))
		fep->clk_enet_out = NULL;

	fep->clk_ptp = devm_clk_get(&pdev->dev, "ptp");
	fep->bufdesc_ex =
		pdev->id_entry->driver_data & FEC_QUIRK_HAS_BUFDESC_EX;
	if (IS_ERR(fep->clk_ptp)) {
		fep->clk_ptp = NULL;
		fep->bufdesc_ex = 0;
	}

	clk_prepare_enable(fep->clk_ahb);
	clk_prepare_enable(fep->clk_ipg);
	clk_prepare_enable(fep->clk_enet_out);
	clk_prepare_enable(fep->clk_ptp);

	fep->reg_phy = devm_regulator_get(&pdev->dev, "phy");
	if (!IS_ERR(fep->reg_phy)) {
		ret = regulator_enable(fep->reg_phy);
		if (ret) {
			dev_err(&pdev->dev,
				"Failed to enable phy regulator: %d\n", ret);
			goto failed_regulator;
		}
	} else {
		fep->reg_phy = NULL;
	}

	fec_reset_phy(pdev);

	if (fep->bufdesc_ex)
		fec_ptp_init(pdev);

	ret = fec_enet_init(ndev);
	if (ret)
		goto failed_init;

	for (i = 0; i < FEC_IRQ_NUM; i++) {
		irq = platform_get_irq(pdev, i);
		if (irq < 0) {
			if (i)
				break;
			ret = irq;
			goto failed_irq;
		}
		ret = request_irq(irq, fec_enet_interrupt, IRQF_DISABLED, pdev->name, ndev);
		if (ret) {
			while (--i >= 0) {
				irq = platform_get_irq(pdev, i);
				free_irq(irq, ndev);
			}
			goto failed_irq;
		}
	}

	ret = fec_enet_mii_init(pdev);
	if (ret)
		goto failed_mii_init;

	/* Carrier starts down, phylib will bring it up */
	netif_carrier_off(ndev);

	ret = register_netdev(ndev);
	if (ret)
		goto failed_register;

	if (fep->bufdesc_ex && fep->ptp_clock)
		netdev_info(ndev, "registered PHC device %d\n", fep->dev_id);

	INIT_DELAYED_WORK(&(fep->delay_work.delay_work), fec_enet_work);
	return 0;

failed_register:
	fec_enet_mii_remove(fep);
failed_mii_init:
failed_irq:
	for (i = 0; i < FEC_IRQ_NUM; i++) {
		irq = platform_get_irq(pdev, i);
		if (irq > 0)
			free_irq(irq, ndev);
	}
failed_init:
	if (fep->reg_phy)
		regulator_disable(fep->reg_phy);
failed_regulator:
	clk_disable_unprepare(fep->clk_ahb);
	clk_disable_unprepare(fep->clk_ipg);
	clk_disable_unprepare(fep->clk_enet_out);
	clk_disable_unprepare(fep->clk_ptp);
failed_clk:
failed_ioremap:
	free_netdev(ndev);

	return ret;
}

static int
fec_drv_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct fec_enet_private *fep = netdev_priv(ndev);
	int i;

	cancel_delayed_work_sync(&(fep->delay_work.delay_work));
	unregister_netdev(ndev);
	fec_enet_mii_remove(fep);
	del_timer_sync(&fep->time_keep);
	for (i = 0; i < FEC_IRQ_NUM; i++) {
		int irq = platform_get_irq(pdev, i);
		if (irq > 0)
			free_irq(irq, ndev);
	}
	if (fep->reg_phy)
		regulator_disable(fep->reg_phy);
	clk_disable_unprepare(fep->clk_ptp);
	if (fep->ptp_clock)
		ptp_clock_unregister(fep->ptp_clock);
	clk_disable_unprepare(fep->clk_enet_out);
	clk_disable_unprepare(fep->clk_ahb);
	clk_disable_unprepare(fep->clk_ipg);
	free_netdev(ndev);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int
fec_suspend(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct fec_enet_private *fep = netdev_priv(ndev);

	if (netif_running(ndev)) {
		fec_stop(ndev);
		netif_device_detach(ndev);
	}
	clk_disable_unprepare(fep->clk_enet_out);
	clk_disable_unprepare(fep->clk_ahb);
	clk_disable_unprepare(fep->clk_ipg);

	if (fep->reg_phy)
		regulator_disable(fep->reg_phy);

	return 0;
}

static int
fec_resume(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct fec_enet_private *fep = netdev_priv(ndev);
	int ret;

	if (fep->reg_phy) {
		ret = regulator_enable(fep->reg_phy);
		if (ret)
			return ret;
	}

	clk_prepare_enable(fep->clk_enet_out);
	clk_prepare_enable(fep->clk_ahb);
	clk_prepare_enable(fep->clk_ipg);
	if (netif_running(ndev)) {
		fec_restart(ndev, fep->full_duplex);
		netif_device_attach(ndev);
	}

	return 0;
}
#endif /* CONFIG_PM_SLEEP */

static SIMPLE_DEV_PM_OPS(fec_pm_ops, fec_suspend, fec_resume);

static struct platform_driver fec_driver = {
	.driver	= {
		.name	= DRIVER_NAME,
		.owner	= THIS_MODULE,
		.pm	= &fec_pm_ops,
		.of_match_table = fec_dt_ids,
	},
	.id_table = fec_devtype,
	.probe	= fec_probe,
	.remove	= fec_drv_remove,
};

module_platform_driver(fec_driver);

MODULE_LICENSE("GPL");

// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2014-2019 Nuvoton Technology corporation.

#ifdef CONFIG_NPCM7XX_EMC_ETH_DEBUG
#define DEBUG
#endif

#include <linux/module.h>
#include <linux/init.h>
#include <linux/mii.h>
#include <linux/phy.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/ethtool.h>
#include <linux/platform_device.h>
#include <linux/gfp.h>
#include <linux/kthread.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/ctype.h>
#include <linux/debugfs.h>

#include <linux/clk.h>

#include <linux/of.h>
#include <linux/of_net.h>
#include <linux/of_device.h>
#include <linux/of_mdio.h>
#include <linux/dma-mapping.h>

#include <linux/regmap.h>
#include <linux/mfd/syscon.h>

#include <linux/if_ether.h>

#include <net/ip.h>
#include <net/ncsi.h>

#ifdef CONFIG_DEBUG_FS
static struct dentry *npcm7xx_fs_dir;
#endif

#define  MFSEL1_OFFSET 0x00C
#define  MFSEL3_OFFSET 0x064
#define  INTCR_OFFSET  0x03C

#define  IPSRST1_OFFSET 0x020

#define DRV_MODULE_NAME		"npcm7xx-emc"
#define DRV_MODULE_VERSION	"3.90"

/* Ethernet MAC Registers */
#define REG_CAMCMR	0x00
#define REG_CAMEN	0x04
#define REG_CAMM_BASE	0x08
#define REG_CAML_BASE	0x0c
#define REG_TXDLSA	0x88
#define REG_RXDLSA	0x8C
#define REG_MCMDR	0x90
#define REG_MIID	0x94
#define REG_MIIDA	0x98
#define REG_FFTCR	0x9C
#define REG_TSDR	0xa0
#define REG_RSDR	0xa4
#define REG_DMARFC	0xa8
#define REG_MIEN	0xac
#define REG_MISTA	0xb0
#define REG_MGSTA	0xb4
#define REG_MPCNT	0xb8
#define REG_MRPC	0xbc
#define REG_MRPCC	0xc0
#define REG_MREPC	0xc4
#define REG_DMARFS	0xc8
#define REG_CTXDSA	0xcc
#define REG_CTXBSA	0xd0
#define REG_CRXDSA	0xd4
#define REG_CRXBSA	0xd8

/* EMC Diagnostic Registers */
#define REG_RXFSM      0x200
#define REG_TXFSM      0x204
#define REG_FSM0       0x208
#define REG_FSM1       0x20c
#define REG_DCR        0x210
#define REG_DMMIR      0x214
#define REG_BISTR      0x300

/* mac controller bit */
#define MCMDR_RXON		BIT(0)
#define MCMDR_ALP		BIT(1)
#define MCMDR_ACP		BIT(3)
#define MCMDR_SPCRC		BIT(5)
#define MCMDR_TXON		BIT(8)
#define MCMDR_NDEF		BIT(9)
#define MCMDR_FDUP		BIT(18)
#define MCMDR_ENMDC		BIT(19)
#define MCMDR_OPMOD		BIT(20)
#define SWR			BIT(24)

/* cam command regiser */
#define CAMCMR_AUP		BIT(0)
#define CAMCMR_AMP		BIT(1)
#define CAMCMR_ABP		BIT(2)
#define CAMCMR_CCAM		BIT(3)
#define CAMCMR_ECMP		BIT(4)

/* cam enable regiser */
#define CAM0EN			BIT(0)

/* mac mii controller bit */
#define PHYAD			BIT(8)
#define PHYWR			BIT(16)
#define PHYBUSY			BIT(17)
#define PHYPRESP		BIT(18)
#define MDCON			BIT(19)
#define CAM_ENTRY_SIZE	 0x08

/* rx and tx status */
#define TXDS_TXCP		BIT(19)
#define RXDS_CRCE		BIT(17)
#define RXDS_PTLE		BIT(19)
#define RXDS_RXGD		BIT(20)
#define RXDS_ALIE		BIT(21)
#define RXDS_RP			BIT(22)

/* mac interrupt status*/
#define MISTA_RXINTR		BIT(0)
#define MISTA_CRCE		BIT(1)
#define MISTA_RXOV		BIT(2)
#define MISTA_PTLE		BIT(3)
#define MISTA_RXGD		BIT(4)
#define MISTA_ALIE		BIT(5)
#define MISTA_RP		BIT(6)
#define MISTA_MMP		BIT(7)
#define MISTA_DFOI		BIT(8)
#define MISTA_DENI		BIT(9)
#define MISTA_RDU		BIT(10)
#define MISTA_RXBERR		BIT(11)
#define MISTA_CFR		BIT(14)
#define MISTA_TXINTR		BIT(16)
#define MISTA_TXEMP		BIT(17)
#define MISTA_TXCP		BIT(18)
#define MISTA_EXDEF		BIT(19)
#define MISTA_NCS		BIT(20)
#define MISTA_TXABT		BIT(21)
#define MISTA_LC		BIT(22)
#define MISTA_TDU		BIT(23)
#define MISTA_TXBERR		BIT(24)

/* Transmit/Receive Start Demand Register */
#define ENSTART			BIT(0)

#define ENRXINTR		BIT(0)
#define ENCRCE			BIT(1)
#define EMRXOV			BIT(2)
#define ENPTLE			BIT(3)
#define ENRXGD			BIT(4)
#define ENALIE			BIT(5)
#define ENRP			BIT(6)
#define ENMMP			BIT(7)
#define ENDFO			BIT(8)
#define ENDENI			BIT(9)
#define ENRDU			BIT(10)
#define ENRXBERR		BIT(11)
#define ENCFR			BIT(14)
#define ENTXINTR		BIT(16)
#define ENTXEMP			BIT(17)
#define ENTXCP			BIT(18)
#define ENTXDEF			BIT(19)
#define ENNCS			BIT(20)
#define ENTXABT			BIT(21)
#define ENLC			BIT(22)
#define ENTDU			BIT(23)
#define ENTXBERR		BIT(24)

/* rx and tx owner bit */
#define RX_OWN_DMA		BIT(31)
#define TX_OWN_DMA		BIT(31)

/* tx frame desc controller bit */
#define MACTXINTEN		BIT(2)
#define CRCMODE			BIT(1)
#define PADDINGMODE		BIT(0)

/* fftcr controller bit */
#define RXTHD			(0x03 << 0)
#define TXTHD			(0x02 << 8)
#define BLENGTH			(0x02 << 20)

/* global setting for driver */
#define RX_QUEUE_LEN	128
#define TX_QUEUE_LEN	64
#define MAX_RBUFF_SZ	0x600
#define MAX_TBUFF_SZ	0x600
#define TX_TIMEOUT	50
#define DELAY		1000
#define CAM0		0x0
#define RX_POLL_SIZE    16

#ifdef CONFIG_VLAN_8021Q
#define IS_VLAN 1
#else
#define IS_VLAN 0
#endif

#define MAX_PACKET_SIZE           (1514 + (IS_VLAN * 4))
#define MAX_PACKET_SIZE_W_CRC     (MAX_PACKET_SIZE + 4) /* 1518 */

#define MHZ (1000 * 1000)
#define MII_TIMEOUT	100

struct plat_npcm7xx_emc_data {
	char *phy_bus_name;
	int phy_addr;
	unsigned char mac_addr[ETH_ALEN];
};

struct npcm7xx_rxbd {
	__le32 sl;
	__le32 buffer;
	__le32 reserved;
	__le32 next;
};

struct npcm7xx_txbd {
	__le32 mode;   /* Ownership bit and some other bits	*/
	__le32 buffer; /* Transmit Buffer Starting Address	*/
	__le32 sl;     /* Transmit Byte Count and status bits	*/
	__le32 next;   /* Next Tx Descriptor Starting Address	*/
};

struct  npcm7xx_ether {
	struct sk_buff *rx_skb[RX_QUEUE_LEN];
	struct sk_buff *tx_skb[TX_QUEUE_LEN];
	spinlock_t lock;	/* lock sk */
	struct npcm7xx_rxbd *rdesc;
	struct npcm7xx_txbd *tdesc;
	dma_addr_t rdesc_phys;
	dma_addr_t tdesc_phys;
	struct net_device_stats stats;
	struct platform_device *pdev;
	struct net_device *ndev;
	struct resource *res;
	unsigned int msg_enable;
	struct device_node *phy_dn;
	struct mii_bus *mii_bus;
	struct phy_device *phy_dev;
	struct napi_struct napi;
	struct ncsi_dev *ncsidev;
	bool use_ncsi;
	void __iomem *reg;
	int rxirq;
	int txirq;
	unsigned int cur_tx;
	unsigned int cur_rx;
	unsigned int finish_tx;
	unsigned int pending_tx;
	__le32 start_tx_ptr;
	__le32 start_rx_ptr;
	unsigned int rx_berr;
	unsigned int rx_err;
	unsigned int rdu;
	unsigned int rxov;
	__le32 camcmr;
	unsigned int rx_stuck;
	int link;
	int speed;
	int duplex;
	int need_reset;
	char *dump_buf;
	struct regmap *rst_regmap;

	/* debug counters */
	unsigned int max_waiting_rx;
	unsigned int rx_count_pool;
	unsigned int count_xmit;
	unsigned int rx_int_count;
	unsigned int rx_err_count;
	unsigned int tx_int_count;
	unsigned int tx_tdu;
	unsigned int tx_tdu_i;
	unsigned int tx_cp_i;
	unsigned int count_finish;

#ifdef CONFIG_DEBUG_FS
	struct dentry *dbgfs_dir;
	struct dentry *dbgfs_status;
	struct dentry *dbgfs_dma_cap;
#endif
};

#if defined CONFIG_NPCM7XX_EMC_ETH_DEBUG || defined CONFIG_DEBUG_FS
#define REG_PRINT(reg_name) {t = scnprintf(next, size, "%-10s = %08X\n", \
	#reg_name, readl(ether->reg + reg_name)); size -= t;	next += t; }
#define DUMP_PRINT(f, x...) {t = scnprintf(next, size, f, ## x); size -= t; \
	next += t; }

static int npcm7xx_info_dump(char *buf, int count, struct net_device *dev)
{
	struct npcm7xx_ether *ether = netdev_priv(dev);
	struct npcm7xx_txbd *txbd;
	struct npcm7xx_rxbd *rxbd;
	unsigned long flags;
	unsigned int i, cur, txd_offset, rxd_offset;
	char *next = buf;
	unsigned int size = count;
	int t;
	int is_locked = spin_is_locked(&ether->lock);

	if (!is_locked)
		spin_lock_irqsave(&ether->lock, flags);

	/* ------basic driver information ---- */
	DUMP_PRINT("NPCM7XX EMC %s driver version: %s\n", dev->name,
		   DRV_MODULE_VERSION);

	REG_PRINT(REG_CAMCMR);
	REG_PRINT(REG_CAMEN);
	REG_PRINT(REG_CAMM_BASE);
	REG_PRINT(REG_CAML_BASE);
	REG_PRINT(REG_TXDLSA);
	REG_PRINT(REG_RXDLSA);
	REG_PRINT(REG_MCMDR);
	REG_PRINT(REG_MIID);
	REG_PRINT(REG_MIIDA);
	REG_PRINT(REG_FFTCR);
	REG_PRINT(REG_TSDR);
	REG_PRINT(REG_RSDR);
	REG_PRINT(REG_DMARFC);
	REG_PRINT(REG_MIEN);
	REG_PRINT(REG_MISTA);
	REG_PRINT(REG_MGSTA);
	REG_PRINT(REG_MPCNT);
	writel(0x7FFF, (ether->reg + REG_MPCNT));
	REG_PRINT(REG_MRPC);
	REG_PRINT(REG_MRPCC);
	REG_PRINT(REG_MREPC);
	REG_PRINT(REG_DMARFS);
	REG_PRINT(REG_CTXDSA);
	REG_PRINT(REG_CTXBSA);
	REG_PRINT(REG_CRXDSA);
	REG_PRINT(REG_CRXBSA);
	REG_PRINT(REG_RXFSM);
	REG_PRINT(REG_TXFSM);
	REG_PRINT(REG_FSM0);
	REG_PRINT(REG_FSM1);
	REG_PRINT(REG_DCR);
	REG_PRINT(REG_DMMIR);
	REG_PRINT(REG_BISTR);
	DUMP_PRINT("\n");

	DUMP_PRINT("netif_queue %s\n\n", netif_queue_stopped(dev) ?
					"Stopped" : "Running");
	if (ether->rdesc)
		DUMP_PRINT("napi is %s\n\n", test_bit(NAPI_STATE_SCHED,
						      &ether->napi.state) ?
							"scheduled" :
							"not scheduled");

	txd_offset = (readl((ether->reg + REG_CTXDSA)) -
		      readl((ether->reg + REG_TXDLSA))) /
		sizeof(struct npcm7xx_txbd);
	DUMP_PRINT("TXD offset    %6d\n", txd_offset);
	DUMP_PRINT("cur_tx        %6d\n", ether->cur_tx);
	DUMP_PRINT("finish_tx     %6d\n", ether->finish_tx);
	DUMP_PRINT("pending_tx    %6d\n", ether->pending_tx);
	/* debug counters */
	DUMP_PRINT("tx_tdu        %6d\n", ether->tx_tdu);
	ether->tx_tdu = 0;
	DUMP_PRINT("tx_tdu_i      %6d\n", ether->tx_tdu_i);
	ether->tx_tdu_i = 0;
	DUMP_PRINT("tx_cp_i       %6d\n", ether->tx_cp_i);
	 ether->tx_cp_i = 0;
	DUMP_PRINT("tx_int_count  %6d\n", ether->tx_int_count);
	ether->tx_int_count = 0;
	DUMP_PRINT("count_xmit tx %6d\n", ether->count_xmit);
	ether->count_xmit = 0;
	DUMP_PRINT("count_finish  %6d\n", ether->count_finish);
	ether->count_finish = 0;
	DUMP_PRINT("\n");

	rxd_offset = (readl((ether->reg + REG_CRXDSA)) -
		      readl((ether->reg + REG_RXDLSA)))
		/ sizeof(struct npcm7xx_txbd);
	DUMP_PRINT("RXD offset    %6d\n", rxd_offset);
	DUMP_PRINT("cur_rx        %6d\n", ether->cur_rx);
	DUMP_PRINT("rx_err        %6d\n", ether->rx_err);
	ether->rx_err = 0;
	DUMP_PRINT("rx_berr       %6d\n", ether->rx_berr);
	ether->rx_berr = 0;
	DUMP_PRINT("rx_stuck      %6d\n", ether->rx_stuck);
	ether->rx_stuck = 0;
	DUMP_PRINT("rdu           %6d\n", ether->rdu);
	ether->rdu = 0;
	DUMP_PRINT("rxov rx       %6d\n", ether->rxov);
	ether->rxov = 0;
	/* debug counters */
	DUMP_PRINT("rx_int_count  %6d\n", ether->rx_int_count);
	ether->rx_int_count = 0;
	DUMP_PRINT("rx_err_count  %6d\n", ether->rx_err_count);
	ether->rx_err_count = 0;
	DUMP_PRINT("rx_count_pool %6d\n", ether->rx_count_pool);
	ether->rx_count_pool = 0;
	DUMP_PRINT("max_waiting_rx %5d\n", ether->max_waiting_rx);
	ether->max_waiting_rx = 0;
	DUMP_PRINT("\n");
	DUMP_PRINT("need_reset    %5d\n", ether->need_reset);

	if (ether->tdesc && ether->rdesc) {
		cur = ether->finish_tx - 2;
		for (i = 0; i < 3; i++) {
			cur = (cur + 1) % TX_QUEUE_LEN;
			txbd = (ether->tdesc + cur);
			DUMP_PRINT("finish %3d txbd mode %08X buffer %08X sl %08X next %08X tx_skb %p\n",
				   cur, txbd->mode, txbd->buffer,
				   txbd->sl, txbd->next, ether->tx_skb[cur]);
		}
		DUMP_PRINT("\n");

		cur = txd_offset - 2;
		for (i = 0; i < 3; i++) {
			cur = (cur + 1) % TX_QUEUE_LEN;
			txbd = (ether->tdesc + cur);
			DUMP_PRINT("txd_of %3d txbd mode %08X buffer %08X sl %08X next %08X\n",
				   cur, txbd->mode, txbd->buffer,
				   txbd->sl, txbd->next);
		}
		DUMP_PRINT("\n");

		cur = ether->cur_tx - 63;
		for (i = 0; i < 64; i++) {
			cur = (cur + 1) % TX_QUEUE_LEN;
			txbd = (ether->tdesc + cur);
			DUMP_PRINT("cur_tx %3d txbd mode %08X buffer %08X sl %08X next %08X\n",
				   cur, txbd->mode, txbd->buffer,
				   txbd->sl, txbd->next);
		}
		DUMP_PRINT("\n");

		cur = ether->cur_rx - 63;
		for (i = 0; i < 64; i++) {
			cur = (cur + 1) % RX_QUEUE_LEN;
			rxbd = (ether->rdesc + cur);
			DUMP_PRINT("cur_rx %3d rxbd sl   %08X buffer %08X sl %08X next %08X\n",
				   cur, rxbd->sl, rxbd->buffer,
				   rxbd->reserved, rxbd->next);
		}
		DUMP_PRINT("\n");

		cur = rxd_offset - 2;
		for (i = 0; i < 3; i++) {
			cur = (cur + 1) % RX_QUEUE_LEN;
			rxbd = (ether->rdesc + cur);
			DUMP_PRINT("rxd_of %3d rxbd sl %08X buffer %08X sl %08X next %08X\n",
				   cur, rxbd->sl, rxbd->buffer,
				   rxbd->reserved, rxbd->next);
		}
		DUMP_PRINT("\n");
	}

	if (!is_locked)
		spin_unlock_irqrestore(&ether->lock, flags);

	return count - size;
}
#endif

#ifdef CONFIG_NPCM7XX_EMC_ETH_DEBUG
static void npcm7xx_info_print(struct net_device *dev)
{
	char *emc_dump_buf;
	int count;
	struct npcm7xx_ether *ether;
	struct platform_device *pdev;
	const size_t print_size = 5 * PAGE_SIZE;

	ether = netdev_priv(dev);
	pdev = ether->pdev;

	emc_dump_buf = kmalloc(print_size, GFP_KERNEL);
	if (!emc_dump_buf) {
		dev_err(&pdev->dev, "kmalloc failed\n");
	} else {
		char c;
		char *tmp_buf = emc_dump_buf;

		count = npcm7xx_info_dump(emc_dump_buf, print_size, dev);
		while (count > 512) {
			c = tmp_buf[512];
			tmp_buf[512] = 0;
			dev_info(&pdev->dev, "%s", tmp_buf);
			tmp_buf += 512;
			tmp_buf[0] = c;
			count -= 512;
		}
		dev_info(&pdev->dev, "%s", tmp_buf);
		kfree(emc_dump_buf);
	}
}
#endif

#ifdef CONFIG_DEBUG_FS
#include <linux/seq_file.h>

static int npcm7xx_debug_show(struct seq_file *sf, void *v)
{
	struct net_device *dev = (struct net_device *)sf->private;
	struct npcm7xx_ether *ether = netdev_priv(dev);
	const size_t print_size = 5 * PAGE_SIZE;

	if (!ether->dump_buf) {
		ether->dump_buf = kmalloc(print_size, GFP_KERNEL);
		if (!ether->dump_buf)
			return -1;
		npcm7xx_info_dump(ether->dump_buf, print_size, dev);
	}

	seq_printf(sf, "%s", ether->dump_buf);
	if (sf->count < sf->size) {
		kfree(ether->dump_buf);
		ether->dump_buf = NULL;
	}

	return 0;
}

static int npcm7xx_debug_show_open(struct inode *inode, struct file *file)
{
	return single_open(file, npcm7xx_debug_show, inode->i_private);
}

static const struct file_operations npcm7xx_debug_show_fops = {
	.open           = npcm7xx_debug_show_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release        = single_release,
};

static int npcm7xx_debug_reset(struct seq_file *sf, void *v)
{
	struct net_device *dev = (struct net_device *)sf->private;
	struct npcm7xx_ether *ether = netdev_priv(dev);
	unsigned long flags;

	seq_puts(sf, "Ask to reset the module\n");
	spin_lock_irqsave(&ether->lock, flags);
	writel(0,  (ether->reg + REG_MIEN));
	spin_unlock_irqrestore(&ether->lock, flags);
	ether->need_reset = 1;
	napi_schedule(&ether->napi);

	return 0;
}

static int npcm7xx_debug_reset_open(struct inode *inode, struct file *file)
{
	return single_open(file, npcm7xx_debug_reset, inode->i_private);
}

static const struct file_operations npcm7xx_debug_reset_fops = {
	.owner		= THIS_MODULE,
	.open           = npcm7xx_debug_reset_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release        = single_release,
};

static int npcm7xx_debug_fs(struct npcm7xx_ether *ether)
{
	/* Create debugfs main directory if it doesn't exist yet */
	if (!npcm7xx_fs_dir) {
		npcm7xx_fs_dir = debugfs_create_dir(DRV_MODULE_NAME, NULL);

		if (!npcm7xx_fs_dir || IS_ERR(npcm7xx_fs_dir)) {
			dev_err(&ether->pdev->dev, "ERROR %s, debugfs create directory failed\n",
				DRV_MODULE_NAME);
			return -ENOMEM;
		}
	}

	/* Create per netdev entries */
	ether->dbgfs_dir = debugfs_create_dir(ether->ndev->name,
					      npcm7xx_fs_dir);
	if (!ether->dbgfs_dir || IS_ERR(ether->dbgfs_dir)) {
		dev_err(&ether->pdev->dev, "ERROR failed to create %s directory\n", ether->ndev->name);
		return -ENOMEM;
	}

	/* Entry to report DMA RX/TX rings */
	ether->dbgfs_status =
		debugfs_create_file("status", 0444,
				    ether->dbgfs_dir, ether->ndev,
				    &npcm7xx_debug_show_fops);

	if (!ether->dbgfs_status || IS_ERR(ether->dbgfs_status)) {
		dev_err(&ether->pdev->dev, "ERROR creating \'status\' debugfs file\n");
		debugfs_remove_recursive(ether->dbgfs_dir);

		return -ENOMEM;
	}

	/* Entry to report the DMA HW features */
	ether->dbgfs_dma_cap = debugfs_create_file("do_reset", 0444,
						   ether->dbgfs_dir,
						   ether->ndev,
						   &npcm7xx_debug_reset_fops);

	if (!ether->dbgfs_dma_cap || IS_ERR(ether->dbgfs_dma_cap)) {
		dev_err(&ether->pdev->dev, "ERROR creating stmmac \'do_reset\' debugfs file\n");
		debugfs_remove_recursive(ether->dbgfs_dir);

		return -ENOMEM;
	}

	return 0;
}
#endif

static void npcm7xx_opmode(struct net_device *dev, int speed, int duplex)
{
	__le32 val;
	struct npcm7xx_ether *ether = netdev_priv(dev);

	val = readl((ether->reg + REG_MCMDR));
	if (speed == 100)
		val |= MCMDR_OPMOD;
	else
		val &= ~MCMDR_OPMOD;

	if (duplex == DUPLEX_FULL)
		val |= MCMDR_FDUP;
	else
		val &= ~MCMDR_FDUP;

	writel(val, (ether->reg + REG_MCMDR));
}

static void adjust_link(struct net_device *dev)
{
	struct npcm7xx_ether *ether = netdev_priv(dev);
	struct phy_device *phydev = ether->phy_dev;
	bool status_change = false;
	unsigned long flags;

	/* clear GPIO interrupt status whihc indicates PHY statu change? */
	spin_lock_irqsave(&ether->lock, flags);

	if (phydev->link) {
		if (ether->speed != phydev->speed ||
		    ether->duplex != phydev->duplex) {
			ether->speed = phydev->speed;
			ether->duplex = phydev->duplex;
			status_change = true;
		}
	} else {
		ether->speed = 0;
		ether->duplex = -1;
	}

	if (phydev->link != ether->link) {
		ether->link = phydev->link;
		status_change = true;
	}

	spin_unlock_irqrestore(&ether->lock, flags);

	if (status_change)
		npcm7xx_opmode(dev, ether->speed, ether->duplex);
}

static void npcm7xx_write_cam(struct net_device *dev,
			      unsigned int x, unsigned char *pval)
{
	struct npcm7xx_ether *ether = netdev_priv(dev);
	__le32 msw, lsw;

	msw = (pval[0] << 24) | (pval[1] << 16) | (pval[2] << 8) | pval[3];

	lsw = (pval[4] << 24) | (pval[5] << 16);

	writel(lsw, (ether->reg + REG_CAML_BASE) + x * CAM_ENTRY_SIZE);
	writel(msw, (ether->reg + REG_CAMM_BASE) + x * CAM_ENTRY_SIZE);
	dev_dbg(&ether->pdev->dev, "REG_CAML_BASE = 0x%08X REG_CAMM_BASE = 0x%08X", lsw, msw);
}

static struct sk_buff *get_new_skb(struct net_device *dev, u32 i)
{
	struct npcm7xx_ether *ether = netdev_priv(dev);
	struct sk_buff *skb = dev_alloc_skb(roundup(MAX_PACKET_SIZE_W_CRC, 4));

	if (!skb)
		return NULL;

	/* Do not unmark the following skb_reserve() Receive Buffer Starting
	 * Address must be aligned to 4 bytes and the following line
	 * if unmarked will make it align to 2 and this likely will
	 * hult the RX and crash the linux skb_reserve(skb, NET_IP_ALIGN);
	 */
	skb->dev = dev;
	(ether->rdesc + i)->buffer =
		dma_map_single(&dev->dev, skb->data,
			       roundup(MAX_PACKET_SIZE_W_CRC, 4),
			       DMA_FROM_DEVICE);
	ether->rx_skb[i] = skb;

	return skb;
}

static int npcm7xx_init_desc(struct net_device *dev)
{
	struct npcm7xx_ether *ether;
	struct npcm7xx_txbd  *tdesc;
	struct npcm7xx_rxbd  *rdesc;
	struct platform_device *pdev;
	unsigned int i;

	ether = netdev_priv(dev);
	pdev = ether->pdev;

	if (!ether->tdesc) {
		ether->tdesc = (struct npcm7xx_txbd *)
				dma_alloc_coherent(&pdev->dev,
						   sizeof(struct npcm7xx_txbd) *
						   TX_QUEUE_LEN,
						   &ether->tdesc_phys,
						   GFP_KERNEL);

		if (!ether->tdesc) {
			dev_err(&pdev->dev, "Failed to allocate memory for tx desc\n");
			return -ENOMEM;
		}
	}

	if (!ether->rdesc) {
		ether->rdesc = (struct npcm7xx_rxbd *)
				dma_alloc_coherent(&pdev->dev,
						   sizeof(struct npcm7xx_rxbd) *
						   RX_QUEUE_LEN,
						   &ether->rdesc_phys,
						   GFP_KERNEL);

		if (!ether->rdesc) {
			dev_err(&pdev->dev, "Failed to allocate memory for rx desc\n");
			dma_free_coherent(&pdev->dev,
					  sizeof(struct npcm7xx_txbd) *
					  TX_QUEUE_LEN, ether->tdesc,
					  ether->tdesc_phys);
			ether->tdesc = NULL;
			return -ENOMEM;
		}
	}

	for (i = 0; i < TX_QUEUE_LEN; i++) {
		unsigned int offset;

		tdesc = (ether->tdesc + i);

		if (i == TX_QUEUE_LEN - 1)
			offset = 0;
		else
			offset = sizeof(struct npcm7xx_txbd) * (i + 1);

		tdesc->next = ether->tdesc_phys + offset;
		tdesc->buffer = (__le32)NULL;
		tdesc->sl = 0;
		tdesc->mode = 0;
	}

	ether->start_tx_ptr = ether->tdesc_phys;

	for (i = 0; i < RX_QUEUE_LEN; i++) {
		unsigned int offset;

		rdesc = (ether->rdesc + i);

		if (i == RX_QUEUE_LEN - 1)
			offset = 0;
		else
			offset = sizeof(struct npcm7xx_rxbd) * (i + 1);

		rdesc->next = ether->rdesc_phys + offset;
		rdesc->sl = RX_OWN_DMA;

		if (!get_new_skb(dev, i)) {
			dev_err(&pdev->dev, "get_new_skb() failed\n");

			for (; i != 0; i--) {
				dma_unmap_single(&dev->dev, (dma_addr_t)
						 ((ether->rdesc + i)->buffer),
						 roundup(MAX_PACKET_SIZE_W_CRC,
							 4), DMA_FROM_DEVICE);
				dev_kfree_skb_any(ether->rx_skb[i]);
				ether->rx_skb[i] = NULL;
			}

			dma_free_coherent(&pdev->dev,
					  sizeof(struct npcm7xx_txbd) *
					  TX_QUEUE_LEN,
					  ether->tdesc, ether->tdesc_phys);
			dma_free_coherent(&pdev->dev,
					  sizeof(struct npcm7xx_rxbd) *
					  RX_QUEUE_LEN,
					  ether->rdesc, ether->rdesc_phys);

			return -ENOMEM;
		}
	}

	ether->start_rx_ptr = ether->rdesc_phys;
	wmb();
	for (i = 0; i < TX_QUEUE_LEN; i++)
		ether->tx_skb[i] = NULL;

	return 0;
}

/* This API must call with Tx/Rx stopped */
static void npcm7xx_free_desc(struct net_device *dev,
			      bool free_also_descriptors)
{
	struct sk_buff *skb;
	u32 i;
	struct npcm7xx_ether *ether = netdev_priv(dev);
	struct platform_device *pdev = ether->pdev;

	for (i = 0; i < TX_QUEUE_LEN; i++) {
		skb = ether->tx_skb[i];
		if (skb) {
			dma_unmap_single(&dev->dev, (dma_addr_t)((ether->tdesc +
								  i)->buffer),
					 skb->len, DMA_TO_DEVICE);
			dev_kfree_skb_any(skb);
			ether->tx_skb[i] = NULL;
		}
	}

	for (i = 0; i < RX_QUEUE_LEN; i++) {
		skb = ether->rx_skb[i];
		if (skb) {
			dma_unmap_single(&dev->dev, (dma_addr_t)((ether->rdesc +
								   i)->buffer),
					 roundup(MAX_PACKET_SIZE_W_CRC, 4),
					 DMA_FROM_DEVICE);
			dev_kfree_skb_any(skb);
			ether->rx_skb[i] = NULL;
		}
	}

	if (free_also_descriptors) {
		if (ether->tdesc)
			dma_free_coherent(&pdev->dev,
					  sizeof(struct npcm7xx_txbd) *
					  TX_QUEUE_LEN,
					  ether->tdesc, ether->tdesc_phys);
		ether->tdesc = NULL;

		if (ether->rdesc)
			dma_free_coherent(&pdev->dev,
					  sizeof(struct npcm7xx_rxbd) *
					  RX_QUEUE_LEN,
					  ether->rdesc, ether->rdesc_phys);
		ether->rdesc = NULL;
	}
}

static void npcm7xx_set_fifo_threshold(struct net_device *dev)
{
	struct npcm7xx_ether *ether = netdev_priv(dev);
	__le32 val;

	val = RXTHD | TXTHD | BLENGTH;
	writel(val, (ether->reg + REG_FFTCR));
}

static void npcm7xx_return_default_idle(struct net_device *dev)
{
	struct npcm7xx_ether *ether = netdev_priv(dev);
	__le32 val;
	__le32 saved_bits;

	val = readl((ether->reg + REG_MCMDR));
	saved_bits = val & (MCMDR_FDUP | MCMDR_OPMOD);
	val |= SWR;
	writel(val, (ether->reg + REG_MCMDR));

	/* During the EMC reset the AHB will read 0 from all registers,
	 * so in order to see if the reset finished we can't count on
	 * (ether->reg + REG_MCMDR).SWR to become 0, instead we read another
	 * register that its reset value is not 0,
	 * we choose (ether->reg + REG_FFTCR).
	 */
	do {
		val = readl((ether->reg + REG_FFTCR));
	} while (val == 0);

	/*
	 * Now we can verify if (ether->reg + REG_MCMDR).SWR became
	 * 0 (probably it will be 0 on the first read).
	 */
	do {
		val = readl((ether->reg + REG_MCMDR));
	} while (val & SWR);

	/* restore values */
	writel(saved_bits, (ether->reg + REG_MCMDR));
}

static void npcm7xx_enable_mac_interrupt(struct net_device *dev)
{
	struct npcm7xx_ether *ether = netdev_priv(dev);
	__le32 val;

	val = ENRXINTR |  /* Start of RX interrupts */
		ENCRCE   |
		EMRXOV   |
		(ENPTLE * (!IS_VLAN)) | /* If we don't support VLAN we want interrupt on long packets */
		ENRXGD   |
		ENALIE   |
		ENRP     |
		ENMMP    |
		ENDFO    |
		/*   ENDENI   |  */  /* We don't need interrupt on DMA Early Notification */
		ENRDU    |    /* We don't need interrupt on Receive Descriptor Unavailable Interrupt */
		ENRXBERR |
		/*   ENCFR    |  */
		ENTXINTR |  /* Start of TX interrupts */
		ENTXEMP  |
		ENTXCP   |
		ENTXDEF  |
		ENNCS    |
		ENTXABT  |
		ENLC     |
		/* ENTDU    |  */  /* We don't need interrupt on Transmit Descriptor Unavailable at start of operation */
		ENTXBERR;
	writel(val, (ether->reg + REG_MIEN));
}

static void npcm7xx_get_and_clear_int(struct net_device *dev,
				      __le32 *val, __le32 mask)
{
	struct npcm7xx_ether *ether = netdev_priv(dev);

	*val = readl((ether->reg + REG_MISTA)) & mask;
	writel(*val, (ether->reg + REG_MISTA));
}

static void npcm7xx_set_global_maccmd(struct net_device *dev)
{
	struct npcm7xx_ether *ether = netdev_priv(dev);
	__le32 val;

	val = readl((ether->reg + REG_MCMDR));

	val |= MCMDR_SPCRC | MCMDR_ENMDC | MCMDR_ACP | MCMDR_NDEF;
	if (IS_VLAN) {
		/*
		 * we set ALP accept long packets since VLAN packets
		 * are 4 bytes longer than 1518
		 */
		val |= MCMDR_ALP;
		/* limit receive length to 1522 bytes due to VLAN */
		writel(MAX_PACKET_SIZE_W_CRC, (ether->reg + REG_DMARFC));
	}
	writel(val, (ether->reg + REG_MCMDR));
}

static void npcm7xx_enable_cam(struct net_device *dev)
{
	struct npcm7xx_ether *ether = netdev_priv(dev);
	__le32 val;

	npcm7xx_write_cam(dev, CAM0, dev->dev_addr);

	val = readl((ether->reg + REG_CAMEN));
	val |= CAM0EN;
	writel(val, (ether->reg + REG_CAMEN));
}

static void npcm7xx_set_curdest(struct net_device *dev)
{
	struct npcm7xx_ether *ether = netdev_priv(dev);

	writel(ether->start_rx_ptr, (ether->reg + REG_RXDLSA));
	writel(ether->start_tx_ptr, (ether->reg + REG_TXDLSA));
}

static void npcm7xx_ether_set_rx_mode(struct net_device *dev)
{
	struct npcm7xx_ether *ether;
	__le32 rx_mode;

	ether = netdev_priv(dev);

	dev_dbg(&ether->pdev->dev, "%s CAMCMR_AUP\n",
		(dev->flags & IFF_PROMISC) ? "Set" : "Clear");
	if (dev->flags & IFF_PROMISC)
		rx_mode = CAMCMR_AUP | CAMCMR_AMP | CAMCMR_ABP | CAMCMR_ECMP;
	else if ((dev->flags & IFF_ALLMULTI) || !netdev_mc_empty(dev))
		rx_mode = CAMCMR_AMP | CAMCMR_ABP | CAMCMR_ECMP;
	else
		rx_mode = CAMCMR_ECMP | CAMCMR_ABP;
	writel(rx_mode, (ether->reg + REG_CAMCMR));
	ether->camcmr = rx_mode;
}

static void npcm7xx_reset_mac(struct net_device *dev, int need_free)
{
	struct npcm7xx_ether *ether = netdev_priv(dev);

	netif_tx_lock(dev);

	/* disable RX and TX */
	writel(readl((ether->reg + REG_MCMDR)) & ~(MCMDR_TXON | MCMDR_RXON),
	       (ether->reg + REG_MCMDR));

	npcm7xx_return_default_idle(dev);
	npcm7xx_set_fifo_threshold(dev);

	if (need_free)
		npcm7xx_free_desc(dev, false);

	npcm7xx_init_desc(dev);

	ether->cur_tx = 0x0;
	ether->finish_tx = 0x0;
	ether->pending_tx = 0x0;
	ether->cur_rx = 0x0;
	ether->tx_tdu = 0;
	ether->tx_tdu_i = 0;
	ether->tx_cp_i = 0;

	npcm7xx_set_curdest(dev);
	npcm7xx_enable_cam(dev);
	npcm7xx_ether_set_rx_mode(dev);
	npcm7xx_enable_mac_interrupt(dev);
	npcm7xx_set_global_maccmd(dev);

	/* enable RX and TX */
	writel(readl((ether->reg + REG_MCMDR)) | MCMDR_TXON | MCMDR_RXON,
	       (ether->reg + REG_MCMDR));

	/* trigger RX */
	writel(ENSTART, (ether->reg + REG_RSDR));

	ether->need_reset = 0;

	netif_wake_queue(dev);
	netif_tx_unlock(dev);
}

static int npcm7xx_mdio_write(struct mii_bus *bus, int phy_id, int regnum,
			      u16 value)
{
	struct npcm7xx_ether *ether = bus->priv;
	unsigned long timeout = jiffies + msecs_to_jiffies(MII_TIMEOUT * 100);

	writel(value,  (ether->reg + REG_MIID));
	writel((phy_id << 0x08) | regnum | PHYBUSY | PHYWR,
	       (ether->reg + REG_MIIDA));

	/* Wait for completion */
	while (readl((ether->reg + REG_MIIDA)) & PHYBUSY) {
		if (time_after(jiffies, timeout)) {
			dev_dbg(&ether->pdev->dev, "mdio read timed out\n ether->reg = 0x%x phy_id=0x%x REG_MIIDA=0x%x\n",
				(unsigned int)ether->reg, phy_id
				, readl((ether->reg + REG_MIIDA)));
			return -ETIMEDOUT;
		}
		cpu_relax();
	}

	return 0;
}

static int npcm7xx_mdio_read(struct mii_bus *bus, int phy_id, int regnum)
{
	struct npcm7xx_ether *ether = bus->priv;
	unsigned long timeout = jiffies + msecs_to_jiffies(MII_TIMEOUT * 100);

	writel((phy_id << 0x08) | regnum | PHYBUSY,  (ether->reg + REG_MIIDA));

	/* Wait for completion */
	while (readl((ether->reg + REG_MIIDA)) & PHYBUSY) {
		if (time_after(jiffies, timeout)) {
			dev_dbg(&ether->pdev->dev, "mdio read timed out\n ether->reg = 0x%x phy_id=0x%x REG_MIIDA=0x%x\n",
				(unsigned int)ether->reg, phy_id
				, readl((ether->reg + REG_MIIDA)));
			return -ETIMEDOUT;
		}
		cpu_relax();
	}

	return readl((ether->reg + REG_MIID));
}

static int npcm7xx_mdio_reset(struct mii_bus *bus)
{
	/* reset EMAC engine?? */
	return 0;
}

static int npcm7xx_set_mac_address(struct net_device *dev, void *addr)
{
	struct sockaddr *address = addr;

	if (!is_valid_ether_addr((u8 *)address->sa_data))
		return -EADDRNOTAVAIL;

	memcpy(dev->dev_addr, address->sa_data, dev->addr_len);
	npcm7xx_write_cam(dev, CAM0, dev->dev_addr);

	return 0;
}

static int npcm7xx_ether_close(struct net_device *dev)
{
	struct npcm7xx_ether *ether = netdev_priv(dev);

	npcm7xx_return_default_idle(dev);

	if (ether->phy_dev)
		phy_stop(ether->phy_dev);
	else if (ether->use_ncsi)
		ncsi_stop_dev(ether->ncsidev);

	msleep(20);

	free_irq(ether->txirq, dev);
	free_irq(ether->rxirq, dev);

	netif_stop_queue(dev);
	napi_disable(&ether->napi);

	npcm7xx_free_desc(dev, true);

	kfree(ether->dump_buf);
	ether->dump_buf = NULL;

	return 0;
}

static struct net_device_stats *npcm7xx_ether_stats(struct net_device *dev)
{
	struct npcm7xx_ether *ether;

	ether = netdev_priv(dev);
	return &ether->stats;
}

static int npcm7xx_clean_tx(struct net_device *dev, bool from_xmit)
{
	struct npcm7xx_ether *ether = netdev_priv(dev);
	struct npcm7xx_txbd *txbd;
	struct sk_buff *s;
	dma_addr_t cur_entry, entry;
	__le32 sl;

	if (ether->pending_tx == 0)
		return (0);

	cur_entry = readl((ether->reg + REG_CTXDSA));

	/* Release old used buffers */
	entry = ether->tdesc_phys + sizeof(struct npcm7xx_txbd) *
		(ether->finish_tx);

	while (entry != cur_entry) {
		txbd = (ether->tdesc + ether->finish_tx);
		s = ether->tx_skb[ether->finish_tx];
		if (!s)
			break;

		ether->count_finish++;

		dma_unmap_single(&dev->dev, txbd->buffer, s->len,
				 DMA_TO_DEVICE);
		consume_skb(s);
		ether->tx_skb[ether->finish_tx] = NULL;

		if (++ether->finish_tx >= TX_QUEUE_LEN)
			ether->finish_tx = 0;
		ether->pending_tx--;

		sl = txbd->sl;
		if (sl & TXDS_TXCP) {
			ether->stats.tx_packets++;
			ether->stats.tx_bytes += (sl & 0xFFFF);
		} else {
			ether->stats.tx_errors++;
		}

		entry = ether->tdesc_phys + sizeof(struct npcm7xx_txbd) *
			(ether->finish_tx);
	}

	if (!from_xmit && unlikely(netif_queue_stopped(dev) &&
				   (TX_QUEUE_LEN - ether->pending_tx) > 1)) {
		netif_tx_lock(dev);
		if (netif_queue_stopped(dev) &&
		    (TX_QUEUE_LEN - ether->pending_tx) > 1) {
			netif_wake_queue(dev);
		}
		netif_tx_unlock(dev);
	}

	return(0);
}

static int npcm7xx_ether_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct npcm7xx_ether *ether = netdev_priv(dev);
	struct npcm7xx_txbd *txbd;
	unsigned long flags;

	ether->count_xmit++;

	/* Insert new buffer */
	txbd = (ether->tdesc + ether->cur_tx);
	txbd->buffer = dma_map_single(&dev->dev, skb->data, skb->len,
				      DMA_TO_DEVICE);
	ether->tx_skb[ether->cur_tx]  = skb;
	if (skb->len > MAX_PACKET_SIZE)
		dev_err(&ether->pdev->dev, "skb->len (= %d) > MAX_PACKET_SIZE (= %d)\n",
			skb->len, MAX_PACKET_SIZE);

	txbd->sl = skb->len > MAX_PACKET_SIZE ? MAX_PACKET_SIZE : skb->len;
	dma_wmb();

	txbd->mode = TX_OWN_DMA | PADDINGMODE | CRCMODE;
	wmb();

	/* trigger TX */
	writel(ENSTART, (ether->reg + REG_TSDR));

	if (++ether->cur_tx >= TX_QUEUE_LEN)
		ether->cur_tx = 0;

	spin_lock_irqsave(&ether->lock, flags);
	ether->pending_tx++;

	npcm7xx_clean_tx(dev, true);

	if (ether->pending_tx >= TX_QUEUE_LEN - 1) {
		__le32 reg_mien;
		unsigned int index_to_wake = ether->cur_tx +
			((TX_QUEUE_LEN * 3) / 4);

		if (index_to_wake >= TX_QUEUE_LEN)
			index_to_wake -= TX_QUEUE_LEN;

		txbd = (ether->tdesc + index_to_wake);
		txbd->mode = TX_OWN_DMA | PADDINGMODE | CRCMODE | MACTXINTEN;
		wmb();

		writel(MISTA_TDU, (ether->reg + REG_MISTA));
		/* Clear TDU interrupt */
		reg_mien = readl((ether->reg + REG_MIEN));

		if (reg_mien != 0)
			/* Enable TDU interrupt */
			writel(reg_mien | ENTDU, (ether->reg + REG_MIEN));

		ether->tx_tdu++;
		netif_stop_queue(dev);
	}

	spin_unlock_irqrestore(&ether->lock, flags);

	return 0;
}

static irqreturn_t npcm7xx_tx_interrupt(int irq, void *dev_id)
{
	struct npcm7xx_ether *ether;
	struct platform_device *pdev;
	struct net_device *dev;
	__le32 status;
	unsigned long flags;

	dev = dev_id;
	ether = netdev_priv(dev);
	pdev = ether->pdev;

	npcm7xx_get_and_clear_int(dev, &status, 0xFFFF0000);

	ether->tx_int_count++;

	if (status & MISTA_EXDEF)
		dev_err(&pdev->dev, "emc defer exceed interrupt status=0x%08X\n"
			, status);
	else if (status & MISTA_TXBERR) {
		dev_err(&pdev->dev, "emc bus error interrupt status=0x%08X\n",
			status);
#ifdef CONFIG_NPCM7XX_EMC_ETH_DEBUG
		npcm7xx_info_print(dev);
#endif
		spin_lock_irqsave(&ether->lock, flags);
		writel(0, (ether->reg + REG_MIEN)); /* disable any interrupt */
		spin_unlock_irqrestore(&ether->lock, flags);
		ether->need_reset = 1;
	} else if (status & ~(MISTA_TXINTR | MISTA_TXCP | MISTA_TDU))
		dev_err(&pdev->dev, "emc other error interrupt status=0x%08X\n",
			status);

    /* if we got MISTA_TXCP | MISTA_TDU remove those interrupt and call napi */
	if (status & (MISTA_TXCP | MISTA_TDU) &
	    readl((ether->reg + REG_MIEN))) {
		__le32 reg_mien;

		spin_lock_irqsave(&ether->lock, flags);
		reg_mien = readl((ether->reg + REG_MIEN));
		if (reg_mien & ENTDU)
			/* Disable TDU interrupt */
			writel(reg_mien & (~ENTDU), (ether->reg + REG_MIEN));

		spin_unlock_irqrestore(&ether->lock, flags);

		if (status & MISTA_TXCP)
			ether->tx_cp_i++;
		if (status & MISTA_TDU)
			ether->tx_tdu_i++;
	} else {
		dev_dbg(&pdev->dev, "status=0x%08X\n", status);
	}

	napi_schedule(&ether->napi);

	return IRQ_HANDLED;
}

static irqreturn_t npcm7xx_rx_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = (struct net_device *)dev_id;
	struct npcm7xx_ether *ether = netdev_priv(dev);
	struct platform_device *pdev = ether->pdev;
	__le32 status;
	unsigned long flags;
	unsigned int any_err = 0;
	__le32 rxfsm;

	npcm7xx_get_and_clear_int(dev, &status, 0xFFFF);
	ether->rx_int_count++;

	if (unlikely(status & MISTA_RXBERR)) {
		ether->rx_berr++;
		dev_err(&pdev->dev, "emc rx bus error status=0x%08X\n", status);
#ifdef CONFIG_NPCM7XX_EMC_ETH_DEBUG
		npcm7xx_info_print(dev);
#endif
		spin_lock_irqsave(&ether->lock, flags);
		writel(0, (ether->reg + REG_MIEN)); /* disable any interrupt */
		spin_unlock_irqrestore(&ether->lock, flags);
		ether->need_reset = 1;
		napi_schedule(&ether->napi);
		return IRQ_HANDLED;
	}

	if (unlikely(status & (MISTA_RXOV | MISTA_RDU))) {
		/*
		 * filter out all received packets until we have
		 * enough available buffer descriptors
		 */
		writel(0, (ether->reg + REG_CAMCMR));
		any_err = 1;
		if (status & (MISTA_RXOV))
			ether->rxov++;
		if (status & (MISTA_RDU))
			ether->rdu++;

		/*
		 * workaround Errata 1.36: EMC Hangs on receiving 253-256
		 * byte packet
		 */
		rxfsm = readl((ether->reg + REG_RXFSM));

		if ((rxfsm & 0xFFFFF000) == 0x08044000) {
			int i;

			for (i = 0; i < 32; i++) {
				rxfsm = readl((ether->reg + REG_RXFSM));
				if ((rxfsm & 0xFFFFF000) != 0x08044000)
					break;
			}
			if (i == 32) {
				ether->rx_stuck++;
				spin_lock_irqsave(&ether->lock, flags);
#ifdef CONFIG_NPCM7XX_EMC_ETH_DEBUG
				npcm7xx_info_print(dev);
#endif
				writel(0,  (ether->reg + REG_MIEN));
				spin_unlock_irqrestore(&ether->lock, flags);
				ether->need_reset = 1;
				    napi_schedule(&ether->napi);
				dev_err(&pdev->dev, "stuck on REG_RXFSM = 0x%08X status=%08X doing reset!\n", rxfsm, status);
				return IRQ_HANDLED;
			}
		}
	}

	/* echo MISTA status on unexpected flags although we don't do anithing with them */
	if (unlikely(status &
		     (/* MISTA_RXINTR | */ /* Receive - all RX interrupt set this */
			       MISTA_CRCE   |    /* CRC Error */
			    /* MISTA_RXOV   | */ /* Receive FIFO Overflow - we alread handled it */
			       (MISTA_PTLE * !IS_VLAN) | /* Packet Too Long is needed if VLAN is not supported */
			    /* MISTA_RXGD   | */ /* Receive Good - this is the common good case */
			       MISTA_ALIE   |    /* Alignment Error */
			       MISTA_RP     |    /* Runt Packet */
			       MISTA_MMP    |    /* More Missed Packet */
			       MISTA_DFOI   |    /* Maximum Frame Length */
			    /* MISTA_DENI   | */ /* DMA Early Notification - every packet get this */
			    /* MISTA_RDU    | */ /* Receive Descriptor Unavailable */
			    /* MISTA_RXBERR | */ /* Receive Bus Error Interrupt - we alread handled it */
			    /* MISTA_CFR    | */ /* Control Frame Receive - not an error */
				0))) {
		dev_dbg(&pdev->dev, "emc rx MISTA status=0x%08X\n", status);
		any_err = 1;
		ether->rx_err++;
	}

	if (!any_err && ((status & MISTA_RXGD) == 0))
		dev_err(&pdev->dev, "emc rx MISTA status=0x%08X\n", status);

	spin_lock_irqsave(&ether->lock, flags);
	writel(readl((ether->reg + REG_MIEN)) & ~ENRXGD,
	       (ether->reg + REG_MIEN));
	spin_unlock_irqrestore(&ether->lock, flags);
	napi_schedule(&ether->napi);

	return IRQ_HANDLED;
}

static int npcm7xx_poll(struct napi_struct *napi, int budget)
{
	struct npcm7xx_ether *ether =
		container_of(napi, struct npcm7xx_ether, napi);
	struct npcm7xx_rxbd *rxbd;
	struct net_device *dev = ether->ndev;
	struct platform_device *pdev = ether->pdev;
	struct sk_buff *skb, *s;
	unsigned int length;
	__le32 status;
	unsigned long flags;
	int rx_cnt = 0;
	int complete = 0;
	unsigned int rx_offset = (readl((ether->reg + REG_CRXDSA)) -
				  ether->start_rx_ptr) /
				sizeof(struct npcm7xx_txbd);
	unsigned int local_count = (rx_offset >= ether->cur_rx) ?
		rx_offset - ether->cur_rx : rx_offset +
		RX_QUEUE_LEN - ether->cur_rx;

	if (local_count > ether->max_waiting_rx)
		ether->max_waiting_rx = local_count;

	if (local_count > (4 * RX_POLL_SIZE))
		/*
		 * we are porbably in a storm of short packets and we don't
		 * want to get into RDU since short packets in RDU cause
		 * many RXOV which may cause EMC halt, so we filter out all
		 * coming packets
		 */
		writel(0, (ether->reg + REG_CAMCMR));

	if (local_count <= budget)
		/* we can restore accepting of packets */
		writel(ether->camcmr, (ether->reg + REG_CAMCMR));

	spin_lock_irqsave(&ether->lock, flags);
	npcm7xx_clean_tx(dev, false);
	spin_unlock_irqrestore(&ether->lock, flags);

	rxbd = (ether->rdesc + ether->cur_rx);

	while (rx_cnt < budget) {
		status = rxbd->sl;
		if ((status & RX_OWN_DMA) == RX_OWN_DMA) {
			complete = 1;
			break;
		}
		/* for debug puposes we save the previous value */
		rxbd->reserved = status;
		s = ether->rx_skb[ether->cur_rx];
		length = status & 0xFFFF;

		/*
		 * If VLAN is not supporte RXDS_PTLE (packet too long) is also
		 * an error
		 */
		if (likely((status & (RXDS_RXGD | RXDS_CRCE | RXDS_ALIE |
				      RXDS_RP | (IS_VLAN ? 0 : RXDS_PTLE))) ==
			   RXDS_RXGD) && likely(length <= MAX_PACKET_SIZE)) {
			dma_unmap_single(&dev->dev, (dma_addr_t)rxbd->buffer,
					 roundup(MAX_PACKET_SIZE_W_CRC, 4),
					 DMA_FROM_DEVICE);

			skb_put(s, length);
			s->protocol = eth_type_trans(s, dev);
			netif_receive_skb(s);
			ether->stats.rx_packets++;
			ether->stats.rx_bytes += length;
			rx_cnt++;
			ether->rx_count_pool++;

			/* now we allocate new skb instead if the used one. */
			skb = dev_alloc_skb(roundup(MAX_PACKET_SIZE_W_CRC, 4));
			if (!skb) {
				dev_err(&pdev->dev, "get skb buffer error\n");
				ether->stats.rx_dropped++;
				goto rx_out;
			}

			/* Do not unmark the following skb_reserve() Receive
			 * Buffer Starting Address must be aligned
			 * to 4 bytes and the following line if unmarked
			 * will make it align to 2 and this likely
			 * will hult the RX and crash the linux
			 * skb_reserve(skb, NET_IP_ALIGN);
			 */
			skb->dev = dev;

			rxbd->buffer = dma_map_single(&dev->dev, skb->data,
						      roundup(MAX_PACKET_SIZE_W_CRC, 4),
						      DMA_FROM_DEVICE);
			ether->rx_skb[ether->cur_rx] = skb;
		} else {
			ether->rx_err_count++;
			ether->stats.rx_errors++;
			dev_dbg(&pdev->dev, "rx_errors = %lu status = 0x%08X\n",
				ether->stats.rx_errors, status);

			if (status & RXDS_RP) {
				ether->stats.rx_length_errors++;
				dev_dbg(&pdev->dev, "rx_length_errors = %lu\n",
					ether->stats.rx_length_errors);
			} else if (status & RXDS_CRCE) {
				ether->stats.rx_crc_errors++;
				dev_dbg(&pdev->dev, "rx_crc_errors = %lu\n",
					ether->stats.rx_crc_errors);
			} else if (status & RXDS_ALIE) {
				ether->stats.rx_frame_errors++;
				dev_dbg(&pdev->dev, "rx_frame_errors = %lu\n",
					ether->stats.rx_frame_errors);
			} else if (((!IS_VLAN) && (status & RXDS_PTLE)) ||
				   length > MAX_PACKET_SIZE) {
				ether->stats.rx_length_errors++;
				dev_dbg(&pdev->dev, "rx_length_errors = %lu\n",
					ether->stats.rx_length_errors);
			}
		}

		wmb();
		rxbd->sl = RX_OWN_DMA;
		wmb();

		if (++ether->cur_rx >= RX_QUEUE_LEN)
			ether->cur_rx = 0;

		rxbd = (ether->rdesc + ether->cur_rx);
	}

	if (complete) {
		napi_complete(napi);

		if (ether->need_reset) {
			dev_dbg(&pdev->dev, "Reset\n");
			npcm7xx_reset_mac(dev, 1);
		}

		spin_lock_irqsave(&ether->lock, flags);
		writel(readl((ether->reg + REG_MIEN)) | ENRXGD,  (ether->reg +
								  REG_MIEN));
		spin_unlock_irqrestore(&ether->lock, flags);
	} else {
		rx_offset = (readl((ether->reg + REG_CRXDSA)) -
			     ether->start_rx_ptr) / sizeof(struct npcm7xx_txbd);
		local_count = (rx_offset >= ether->cur_rx) ? rx_offset -
			ether->cur_rx : rx_offset + RX_QUEUE_LEN -
			ether->cur_rx;

		if (local_count > ether->max_waiting_rx)
			ether->max_waiting_rx = local_count;

		if (local_count > (3 * RX_POLL_SIZE))
			/*
			 * we are porbably in a storm of short packets and
			 * we don't want to get into RDU since short packets in
			 * RDU cause many RXOV which may cause
			 * EMC halt, so we filter out all coming packets
			 */
			writel(0, (ether->reg + REG_CAMCMR));
		if (local_count <= RX_POLL_SIZE)
			/* we can restore accepting of packets */
			writel(ether->camcmr, (ether->reg + REG_CAMCMR));
	}
rx_out:

	/* trigger RX */
	writel(ENSTART, (ether->reg + REG_RSDR));
	return rx_cnt;
}

static int npcm7xx_ether_open(struct net_device *dev)
{
	struct npcm7xx_ether *ether;
	struct platform_device *pdev;

	ether = netdev_priv(dev);
	pdev = ether->pdev;

	if (ether->use_ncsi) {
		ether->speed = 100;
		ether->duplex = DUPLEX_FULL;
		npcm7xx_opmode(dev, 100, DUPLEX_FULL);
	}
	npcm7xx_reset_mac(dev, 0);

	if (request_irq(ether->txirq, npcm7xx_tx_interrupt, 0x0, pdev->name,
			dev)) {
		dev_err(&pdev->dev, "register irq tx failed\n");
		npcm7xx_ether_close(dev);
		return -EAGAIN;
	}

	if (request_irq(ether->rxirq, npcm7xx_rx_interrupt, 0x0, pdev->name,
			dev)) {
		dev_err(&pdev->dev, "register irq rx failed\n");
		npcm7xx_ether_close(dev);
		return -EAGAIN;
	}

	if (ether->phy_dev)
		phy_start(ether->phy_dev);
	else if (ether->use_ncsi)
		netif_carrier_on(dev);

	netif_start_queue(dev);
	napi_enable(&ether->napi);

	/* trigger RX */
	writel(ENSTART, (ether->reg + REG_RSDR));

	/* Start the NCSI device */
	if (ether->use_ncsi) {
		int err = ncsi_start_dev(ether->ncsidev);

		if (err) {
			npcm7xx_ether_close(dev);
			return err;
		}
	}

	dev_info(&pdev->dev, "%s is OPENED\n", dev->name);

	return 0;
}

static int npcm7xx_ether_ioctl(struct net_device *dev,
			       struct ifreq *ifr, int cmd)
{
	struct npcm7xx_ether *ether = netdev_priv(dev);
	struct phy_device *phydev = ether->phy_dev;

	if (!netif_running(dev))
		return -EINVAL;

	if (!phydev)
		return -ENODEV;

	return phy_mii_ioctl(phydev, ifr, cmd);
}

static void npcm7xx_get_drvinfo(struct net_device *dev,
				struct ethtool_drvinfo *info)
{
	strlcpy(info->driver, DRV_MODULE_NAME, sizeof(info->driver));
	strlcpy(info->version, DRV_MODULE_VERSION, sizeof(info->version));
	strlcpy(info->fw_version, "N/A", sizeof(info->fw_version));
	strlcpy(info->bus_info, "N/A", sizeof(info->bus_info));
}

static int npcm7xx_get_settings(struct net_device *dev,
				struct ethtool_link_ksettings *cmd)
{
	struct npcm7xx_ether *ether = netdev_priv(dev);
	struct phy_device *phydev = ether->phy_dev;

	if (!phydev)
		return -ENODEV;

	phy_ethtool_ksettings_get(phydev, cmd);

	return 0;
}

static int npcm7xx_set_settings(struct net_device *dev,
				const struct ethtool_link_ksettings *cmd)
{
	struct npcm7xx_ether *ether = netdev_priv(dev);
	struct phy_device *phydev = ether->phy_dev;
	int ret;

	if (!phydev)
		return -ENODEV;

	ret =  phy_ethtool_ksettings_set(phydev, cmd);

	return ret;
}

static u32 npcm7xx_get_msglevel(struct net_device *dev)
{
	struct npcm7xx_ether *ether = netdev_priv(dev);

	return ether->msg_enable;
}

static void npcm7xx_set_msglevel(struct net_device *dev, u32 level)
{
	struct npcm7xx_ether *ether = netdev_priv(dev);

	ether->msg_enable = level;
}

static const struct ethtool_ops npcm7xx_ether_ethtool_ops = {
	.get_link_ksettings     = npcm7xx_get_settings,
	.set_link_ksettings     = npcm7xx_set_settings,
	.get_drvinfo	= npcm7xx_get_drvinfo,
	.get_msglevel	= npcm7xx_get_msglevel,
	.set_msglevel	= npcm7xx_set_msglevel,
	.get_link	= ethtool_op_get_link,
};

static const struct net_device_ops npcm7xx_ether_netdev_ops = {
	.ndo_open		= npcm7xx_ether_open,
	.ndo_stop		= npcm7xx_ether_close,
	.ndo_start_xmit		= npcm7xx_ether_start_xmit,
	.ndo_get_stats		= npcm7xx_ether_stats,
	.ndo_set_rx_mode	= npcm7xx_ether_set_rx_mode,
	.ndo_set_mac_address	= npcm7xx_set_mac_address,
	.ndo_do_ioctl		= npcm7xx_ether_ioctl,
	.ndo_validate_addr	= eth_validate_addr,
};

static void get_mac_address(struct net_device *dev)
{
	struct npcm7xx_ether *ether = netdev_priv(dev);
	struct platform_device *pdev = ether->pdev;
	struct device_node *np = ether->pdev->dev.of_node;

	of_get_mac_address(np, dev->dev_addr);

	if (is_valid_ether_addr(dev->dev_addr)) {
		dev_info(&pdev->dev, "%s: device MAC address : %pM\n",
			 pdev->name, dev->dev_addr);
	} else {
		eth_hw_addr_random(dev);
		dev_info(&pdev->dev, "%s: device MAC address (random generator) %pM\n",
			 dev->name, dev->dev_addr);
	}
}

static int npcm7xx_mii_setup(struct net_device *dev)
{
	struct npcm7xx_ether *ether = netdev_priv(dev);
	struct platform_device *pdev;
	struct phy_device *phydev = NULL;
	int i, err = 0;

	pdev = ether->pdev;

	if (ether->phy_dn) {
		ether->phy_dev = of_phy_connect(dev, ether->phy_dn,
					&adjust_link, 0, 0);
		if (!ether->phy_dn) {
			dev_err(&dev->dev, "could not connect to phy %pOF\n",
				ether->phy_dn);
			return -ENODEV;
		}
		return 0;
	}

	ether->mii_bus = mdiobus_alloc();
	if (!ether->mii_bus) {
		err = -ENOMEM;
		dev_err(&pdev->dev, "mdiobus_alloc() failed\n");
		goto out0;
	}

	ether->mii_bus->name = "npcm7xx_rmii";
	ether->mii_bus->read = &npcm7xx_mdio_read;
	ether->mii_bus->write = &npcm7xx_mdio_write;
	ether->mii_bus->reset = &npcm7xx_mdio_reset;
	snprintf(ether->mii_bus->id, MII_BUS_ID_SIZE, "%s-%x",
		 ether->pdev->name, ether->pdev->id);
	dev_dbg(&pdev->dev, "%s ether->mii_bus->id=%s\n", __func__,
		ether->mii_bus->id);
	ether->mii_bus->priv = ether;
	ether->mii_bus->parent = &ether->pdev->dev;

	for (i = 0; i < PHY_MAX_ADDR; i++)
		ether->mii_bus->irq[i] = PHY_POLL;

	platform_set_drvdata(ether->pdev, ether->mii_bus);

	/* Enable MDIO Clock */
	writel(readl((ether->reg + REG_MCMDR)) | MCMDR_ENMDC,
	       (ether->reg + REG_MCMDR));

	if (mdiobus_register(ether->mii_bus)) {
		dev_err(&pdev->dev, "mdiobus_register() failed\n");
		goto out2;
	}

	phydev = phy_find_first(ether->mii_bus);
	if (!phydev) {
		dev_err(&pdev->dev, "phy_find_first() failed\n");
		goto out3;
	}

	dev_info(&pdev->dev, " name = %s ETH-Phy-Id = 0x%x\n",
		 phydev_name(phydev), phydev->phy_id);

	phydev = phy_connect(dev, phydev_name(phydev),
			     &adjust_link,
			     PHY_INTERFACE_MODE_RMII);

	dev_info(&pdev->dev, " ETH-Phy-Id = 0x%x name = %s\n",
		 phydev->phy_id, phydev->drv->name);

	if (IS_ERR(phydev)) {
		err = PTR_ERR(phydev);
		dev_err(&pdev->dev, "phy_connect() failed - %d\n", err);
		goto out3;
	}

	linkmode_and(phydev->supported, phydev->supported, PHY_BASIC_FEATURES);
	linkmode_copy(phydev->advertising, phydev->supported);
	ether->phy_dev = phydev;

	return 0;

out3:
	mdiobus_unregister(ether->mii_bus);
out2:
	kfree(ether->mii_bus->irq);
	mdiobus_free(ether->mii_bus);
out0:

	return err;
}

static const struct of_device_id emc_dt_id[] = {
	{ .compatible = "nuvoton,npcm750-emc",  },
	{},
};
MODULE_DEVICE_TABLE(of, emc_dt_id);

static void npcm7xx_ncsi_handler(struct ncsi_dev *nd)
{
	if (unlikely(nd->state != ncsi_dev_state_functional))
		return;

	netdev_info(nd->dev, "NCSI interface %s\n",
		    nd->link_up ? "up" : "down");
}

static int npcm7xx_ether_probe(struct platform_device *pdev)
{
	struct npcm7xx_ether *ether;
	struct net_device *dev;
	int error;

	struct clk *emc_clk = NULL;
	struct device_node *np = pdev->dev.of_node;

	pdev->id = of_alias_get_id(np, "ethernet");
	if (pdev->id < 0)
		pdev->id = 0;

	emc_clk = devm_clk_get(&pdev->dev, NULL);

	if (IS_ERR(emc_clk))
		return PTR_ERR(emc_clk);

	/* Enable Clock */
	clk_prepare_enable(emc_clk);

	error = dma_coerce_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (error)
		return -ENODEV;

	dev = alloc_etherdev(sizeof(struct npcm7xx_ether));
	if (!dev)
		return -ENOMEM;

	ether = netdev_priv(dev);

	ether->rst_regmap =
		syscon_regmap_lookup_by_compatible("nuvoton,npcm750-rst");
	if (IS_ERR(ether->rst_regmap)) {
		dev_err(&pdev->dev, "%s: failed to find nuvoton,npcm750-rst\n", __func__);
		return IS_ERR(ether->rst_regmap);
	}

	/* Reset EMC module */
	if (pdev->id == 0) {
		regmap_update_bits(ether->rst_regmap, IPSRST1_OFFSET,
				   (0x1 << 6), (0x1 << 6));
		regmap_update_bits(ether->rst_regmap, IPSRST1_OFFSET,
				   (0x1 << 6), 0);
	}
	if (pdev->id == 1) {
		regmap_update_bits(ether->rst_regmap, IPSRST1_OFFSET,
				   (0x1 << 21), (0x1 << 21));
		regmap_update_bits(ether->rst_regmap, IPSRST1_OFFSET,
				   (0x1 << 21), 0);
	}

	ether->res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!ether->res) {
		dev_err(&pdev->dev, "failed to get I/O memory\n");
		error = -ENXIO;
		goto failed_free;
	}

	if (!request_mem_region(ether->res->start,
				resource_size(ether->res), pdev->name)) {
		dev_err(&pdev->dev, "failed to request I/O memory\n");
		error = -EBUSY;
		goto failed_free;
	}

	ether->reg = ioremap(ether->res->start, resource_size(ether->res));
	dev_dbg(&pdev->dev, "%s ether->reg = 0x%x\n", __func__,
		(unsigned int)ether->reg);

	if (!ether->reg) {
		dev_err(&pdev->dev, "failed to remap I/O memory\n");
		error = -ENXIO;
		goto failed_free_mem;
	}

	ether->txirq = platform_get_irq(pdev, 0);
	if (ether->txirq < 0) {
		dev_err(&pdev->dev, "failed to get ether tx irq\n");
		error = -ENXIO;
		goto failed_free_io;
	}

	ether->rxirq = platform_get_irq(pdev, 1);
	if (ether->rxirq < 0) {
		dev_err(&pdev->dev, "failed to get ether rx irq\n");
		error = -ENXIO;
		goto failed_free_io;
	}

	SET_NETDEV_DEV(dev, &pdev->dev);
	platform_set_drvdata(pdev, dev);
	ether->ndev = dev;

	ether->pdev = pdev;
	ether->msg_enable = NETIF_MSG_LINK;

	dev->netdev_ops = &npcm7xx_ether_netdev_ops;
	dev->ethtool_ops = &npcm7xx_ether_ethtool_ops;

	dev->tx_queue_len = TX_QUEUE_LEN;
	dev->dma = 0x0;
	dev->watchdog_timeo = TX_TIMEOUT;

	get_mac_address(dev);

	ether->cur_tx = 0x0;
	ether->cur_rx = 0x0;
	ether->finish_tx = 0x0;
	ether->pending_tx = 0x0;
	ether->link = 0;
	ether->speed = 100;
	ether->duplex = DUPLEX_FULL;
	ether->need_reset = 0;
	ether->dump_buf = NULL;
	ether->rx_berr = 0;
	ether->rx_err = 0;
	ether->rdu = 0;
	ether->rxov = 0;
	ether->rx_stuck = 0;
	/* debug counters */
	ether->max_waiting_rx = 0;
	ether->rx_count_pool = 0;
	ether->count_xmit = 0;
	ether->rx_int_count = 0;
	ether->rx_err_count = 0;
	ether->tx_int_count = 0;
	ether->count_finish = 0;
	ether->tx_tdu = 0;
	ether->tx_tdu_i = 0;
	ether->tx_cp_i = 0;

	spin_lock_init(&ether->lock);

	netif_napi_add(dev, &ether->napi, npcm7xx_poll, RX_POLL_SIZE);

	if (pdev->dev.of_node &&
	    of_get_property(pdev->dev.of_node, "use-ncsi", NULL)) {
		if (!IS_ENABLED(CONFIG_NET_NCSI)) {
			dev_err(&pdev->dev, "CONFIG_NET_NCSI not enabled\n");
			error = -ENODEV;
			goto failed_free_napi;
		}
		dev_info(&pdev->dev, "Using NCSI interface\n");
		ether->use_ncsi = true;
		ether->ncsidev = ncsi_register_dev(dev, npcm7xx_ncsi_handler);
		if (!ether->ncsidev) {
			error = -ENODEV;
			goto failed_free_napi;
		}
	} else {
		ether->use_ncsi = false;

		ether->phy_dn = of_parse_phandle(np, "phy-handle", 0);
		if (!ether->phy_dn && of_phy_is_fixed_link(np)) {
			error = of_phy_register_fixed_link(np);
			if (error < 0)
				goto failed_free_napi;
			ether->phy_dn = of_node_get(np);
		}

	error = npcm7xx_mii_setup(dev);
	if (error < 0) {
		dev_err(&pdev->dev, "npcm7xx_mii_setup err\n");
		goto failed_free_napi;
		}
	}

	error = register_netdev(dev);
	if (error != 0) {
		dev_err(&pdev->dev, "register_netdev() failed\n");
		error = -ENODEV;
		goto failed_free_napi;
	}

#ifdef CONFIG_DEBUG_FS
	npcm7xx_debug_fs(ether);
#endif

	return 0;

failed_free_napi:
	of_node_put(ether->phy_dn);
	if (of_phy_is_fixed_link(np))
		of_phy_deregister_fixed_link(np);
	netif_napi_del(&ether->napi);
	platform_set_drvdata(pdev, NULL);
failed_free_io:
	iounmap(ether->reg);
failed_free_mem:
	release_mem_region(ether->res->start, resource_size(ether->res));
failed_free:
	free_netdev(dev);

	return error;
}

static int npcm7xx_ether_remove(struct platform_device *pdev)
{
	struct net_device *dev = platform_get_drvdata(pdev);
	struct npcm7xx_ether *ether = netdev_priv(dev);
	struct device_node *np = pdev->dev.of_node;

#ifdef CONFIG_DEBUG_FS
	debugfs_remove_recursive(ether->dbgfs_dir);
#endif
	unregister_netdev(dev);

	of_node_put(ether->phy_dn);
	if (of_phy_is_fixed_link(np))
		of_phy_deregister_fixed_link(np);

	free_irq(ether->txirq, dev);
	free_irq(ether->rxirq, dev);

	if (ether->phy_dev)
		phy_disconnect(ether->phy_dev);

	mdiobus_unregister(ether->mii_bus);
	kfree(ether->mii_bus->irq);
	mdiobus_free(ether->mii_bus);

	platform_set_drvdata(pdev, NULL);

	free_netdev(dev);
	return 0;
}

static struct platform_driver npcm7xx_ether_driver = {
	.probe		= npcm7xx_ether_probe,
	.remove		= npcm7xx_ether_remove,
	.driver		= {
		.name	= DRV_MODULE_NAME,
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(emc_dt_id),
	},
};

module_platform_driver(npcm7xx_ether_driver);

MODULE_AUTHOR("Nuvoton Technology Corp.");
MODULE_DESCRIPTION("NPCM750 EMC driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:npcm750-emc");
MODULE_VERSION(DRV_MODULE_VERSION);

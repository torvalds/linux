// SPDX-License-Identifier: GPL-2.0-or-later
/*


*/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#define DRV_NAME	"uli526x"
#define DRV_VERSION	"0.9.3"
#define DRV_RELDATE	"2005-7-29"

#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/skbuff.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/dma-mapping.h>
#include <linux/bitops.h>

#include <asm/processor.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <linux/uaccess.h>

#define uw32(reg, val)	iowrite32(val, ioaddr + (reg))
#define ur32(reg)	ioread32(ioaddr + (reg))

/* Board/System/Debug information/definition ---------------- */
#define PCI_ULI5261_ID  0x526110B9	/* ULi M5261 ID*/
#define PCI_ULI5263_ID  0x526310B9	/* ULi M5263 ID*/

#define ULI526X_IO_SIZE 0x100
#define TX_DESC_CNT     0x20            /* Allocated Tx descriptors */
#define RX_DESC_CNT     0x30            /* Allocated Rx descriptors */
#define TX_FREE_DESC_CNT (TX_DESC_CNT - 2)	/* Max TX packet count */
#define TX_WAKE_DESC_CNT (TX_DESC_CNT - 3)	/* TX wakeup count */
#define DESC_ALL_CNT    (TX_DESC_CNT + RX_DESC_CNT)
#define TX_BUF_ALLOC    0x600
#define RX_ALLOC_SIZE   0x620
#define ULI526X_RESET    1
#define CR0_DEFAULT     0
#define CR6_DEFAULT     0x22200000
#define CR7_DEFAULT     0x180c1
#define CR15_DEFAULT    0x06            /* TxJabber RxWatchdog */
#define TDES0_ERR_MASK  0x4302          /* TXJT, LC, EC, FUE */
#define MAX_PACKET_SIZE 1514
#define ULI5261_MAX_MULTICAST 14
#define RX_COPY_SIZE	100
#define MAX_CHECK_PACKET 0x8000

#define ULI526X_10MHF      0
#define ULI526X_100MHF     1
#define ULI526X_10MFD      4
#define ULI526X_100MFD     5
#define ULI526X_AUTO       8

#define ULI526X_TXTH_72	0x400000	/* TX TH 72 byte */
#define ULI526X_TXTH_96	0x404000	/* TX TH 96 byte */
#define ULI526X_TXTH_128	0x0000		/* TX TH 128 byte */
#define ULI526X_TXTH_256	0x4000		/* TX TH 256 byte */
#define ULI526X_TXTH_512	0x8000		/* TX TH 512 byte */
#define ULI526X_TXTH_1K	0xC000		/* TX TH 1K  byte */

#define ULI526X_TIMER_WUT  (jiffies + HZ * 1)/* timer wakeup time : 1 second */
#define ULI526X_TX_TIMEOUT ((16*HZ)/2)	/* tx packet time-out time 8 s" */
#define ULI526X_TX_KICK 	(4*HZ/2)	/* tx packet Kick-out time 2 s" */

#define ULI526X_DBUG(dbug_now, msg, value)			\
do {								\
	if (uli526x_debug || (dbug_now))			\
		pr_err("%s %lx\n", (msg), (long) (value));	\
} while (0)

#define SHOW_MEDIA_TYPE(mode)					\
	pr_err("Change Speed to %sMhz %s duplex\n",		\
	       mode & 1 ? "100" : "10",				\
	       mode & 4 ? "full" : "half");


/* CR9 definition: SROM/MII */
#define CR9_SROM_READ   0x4800
#define CR9_SRCS        0x1
#define CR9_SRCLK       0x2
#define CR9_CRDOUT      0x8
#define SROM_DATA_0     0x0
#define SROM_DATA_1     0x4
#define PHY_DATA_1      0x20000
#define PHY_DATA_0      0x00000
#define MDCLKH          0x10000

#define PHY_POWER_DOWN	0x800

#define SROM_V41_CODE   0x14

/* Structure/enum declaration ------------------------------- */
struct tx_desc {
        __le32 tdes0, tdes1, tdes2, tdes3; /* Data for the card */
        char *tx_buf_ptr;               /* Data for us */
        struct tx_desc *next_tx_desc;
} __attribute__(( aligned(32) ));

struct rx_desc {
	__le32 rdes0, rdes1, rdes2, rdes3; /* Data for the card */
	struct sk_buff *rx_skb_ptr;	/* Data for us */
	struct rx_desc *next_rx_desc;
} __attribute__(( aligned(32) ));

struct uli526x_board_info {
	struct uli_phy_ops {
		void (*write)(struct uli526x_board_info *, u8, u8, u16);
		u16 (*read)(struct uli526x_board_info *, u8, u8);
	} phy;
	struct net_device *next_dev;	/* next device */
	struct pci_dev *pdev;		/* PCI device */
	spinlock_t lock;

	void __iomem *ioaddr;		/* I/O base address */
	u32 cr0_data;
	u32 cr5_data;
	u32 cr6_data;
	u32 cr7_data;
	u32 cr15_data;

	/* pointer for memory physical address */
	dma_addr_t buf_pool_dma_ptr;	/* Tx buffer pool memory */
	dma_addr_t buf_pool_dma_start;	/* Tx buffer pool align dword */
	dma_addr_t desc_pool_dma_ptr;	/* descriptor pool memory */
	dma_addr_t first_tx_desc_dma;
	dma_addr_t first_rx_desc_dma;

	/* descriptor pointer */
	unsigned char *buf_pool_ptr;	/* Tx buffer pool memory */
	unsigned char *buf_pool_start;	/* Tx buffer pool align dword */
	unsigned char *desc_pool_ptr;	/* descriptor pool memory */
	struct tx_desc *first_tx_desc;
	struct tx_desc *tx_insert_ptr;
	struct tx_desc *tx_remove_ptr;
	struct rx_desc *first_rx_desc;
	struct rx_desc *rx_insert_ptr;
	struct rx_desc *rx_ready_ptr;	/* packet come pointer */
	unsigned long tx_packet_cnt;	/* transmitted packet count */
	unsigned long rx_avail_cnt;	/* available rx descriptor count */
	unsigned long interval_rx_cnt;	/* rx packet count a callback time */

	u16 dbug_cnt;
	u16 NIC_capability;		/* NIC media capability */
	u16 PHY_reg4;			/* Saved Phyxcer register 4 value */

	u8 media_mode;			/* user specify media mode */
	u8 op_mode;			/* real work media mode */
	u8 phy_addr;
	u8 link_failed;			/* Ever link failed */
	u8 wait_reset;			/* Hardware failed, need to reset */
	struct timer_list timer;

	/* Driver defined statistic counter */
	unsigned long tx_fifo_underrun;
	unsigned long tx_loss_carrier;
	unsigned long tx_no_carrier;
	unsigned long tx_late_collision;
	unsigned long tx_excessive_collision;
	unsigned long tx_jabber_timeout;
	unsigned long reset_count;
	unsigned long reset_cr8;
	unsigned long reset_fatal;
	unsigned long reset_TXtimeout;

	/* NIC SROM data */
	unsigned char srom[128];
	u8 init;
};

enum uli526x_offsets {
	DCR0 = 0x00, DCR1 = 0x08, DCR2 = 0x10, DCR3 = 0x18, DCR4 = 0x20,
	DCR5 = 0x28, DCR6 = 0x30, DCR7 = 0x38, DCR8 = 0x40, DCR9 = 0x48,
	DCR10 = 0x50, DCR11 = 0x58, DCR12 = 0x60, DCR13 = 0x68, DCR14 = 0x70,
	DCR15 = 0x78
};

enum uli526x_CR6_bits {
	CR6_RXSC = 0x2, CR6_PBF = 0x8, CR6_PM = 0x40, CR6_PAM = 0x80,
	CR6_FDM = 0x200, CR6_TXSC = 0x2000, CR6_STI = 0x100000,
	CR6_SFT = 0x200000, CR6_RXA = 0x40000000, CR6_NO_PURGE = 0x20000000
};

/* Global variable declaration ----------------------------- */
static int printed_version;
static const char version[] =
	"ULi M5261/M5263 net driver, version " DRV_VERSION " (" DRV_RELDATE ")";

static int uli526x_debug;
static unsigned char uli526x_media_mode = ULI526X_AUTO;
static u32 uli526x_cr6_user_set;

/* For module input parameter */
static int debug;
static u32 cr6set;
static int mode = 8;

/* function declaration ------------------------------------- */
static int uli526x_open(struct net_device *);
static netdev_tx_t uli526x_start_xmit(struct sk_buff *,
					    struct net_device *);
static int uli526x_stop(struct net_device *);
static void uli526x_set_filter_mode(struct net_device *);
static const struct ethtool_ops netdev_ethtool_ops;
static u16 read_srom_word(struct uli526x_board_info *, int);
static irqreturn_t uli526x_interrupt(int, void *);
#ifdef CONFIG_NET_POLL_CONTROLLER
static void uli526x_poll(struct net_device *dev);
#endif
static void uli526x_descriptor_init(struct net_device *, void __iomem *);
static void allocate_rx_buffer(struct net_device *);
static void update_cr6(u32, void __iomem *);
static void send_filter_frame(struct net_device *, int);
static u16 phy_readby_cr9(struct uli526x_board_info *, u8, u8);
static u16 phy_readby_cr10(struct uli526x_board_info *, u8, u8);
static void phy_writeby_cr9(struct uli526x_board_info *, u8, u8, u16);
static void phy_writeby_cr10(struct uli526x_board_info *, u8, u8, u16);
static void phy_write_1bit(struct uli526x_board_info *db, u32);
static u16 phy_read_1bit(struct uli526x_board_info *db);
static u8 uli526x_sense_speed(struct uli526x_board_info *);
static void uli526x_process_mode(struct uli526x_board_info *);
static void uli526x_timer(struct timer_list *t);
static void uli526x_rx_packet(struct net_device *, struct uli526x_board_info *);
static void uli526x_free_tx_pkt(struct net_device *, struct uli526x_board_info *);
static void uli526x_reuse_skb(struct uli526x_board_info *, struct sk_buff *);
static void uli526x_dynamic_reset(struct net_device *);
static void uli526x_free_rxbuffer(struct uli526x_board_info *);
static void uli526x_init(struct net_device *);
static void uli526x_set_phyxcer(struct uli526x_board_info *);

static void srom_clk_write(struct uli526x_board_info *db, u32 data)
{
	void __iomem *ioaddr = db->ioaddr;

	uw32(DCR9, data | CR9_SROM_READ | CR9_SRCS);
	udelay(5);
	uw32(DCR9, data | CR9_SROM_READ | CR9_SRCS | CR9_SRCLK);
	udelay(5);
	uw32(DCR9, data | CR9_SROM_READ | CR9_SRCS);
	udelay(5);
}

/* ULI526X network board routine ---------------------------- */

static const struct net_device_ops netdev_ops = {
	.ndo_open		= uli526x_open,
	.ndo_stop		= uli526x_stop,
	.ndo_start_xmit		= uli526x_start_xmit,
	.ndo_set_rx_mode	= uli526x_set_filter_mode,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller 	= uli526x_poll,
#endif
};

/*
 *	Search ULI526X board, allocate space and register it
 */

static int uli526x_init_one(struct pci_dev *pdev,
			    const struct pci_device_id *ent)
{
	struct uli526x_board_info *db;	/* board information structure */
	struct net_device *dev;
	void __iomem *ioaddr;
	int i, err;

	ULI526X_DBUG(0, "uli526x_init_one()", 0);

	if (!printed_version++)
		pr_info("%s\n", version);

	/* Init network device */
	dev = alloc_etherdev(sizeof(*db));
	if (dev == NULL)
		return -ENOMEM;
	SET_NETDEV_DEV(dev, &pdev->dev);

	if (pci_set_dma_mask(pdev, DMA_BIT_MASK(32))) {
		pr_warn("32-bit PCI DMA not available\n");
		err = -ENODEV;
		goto err_out_free;
	}

	/* Enable Master/IO access, Disable memory access */
	err = pci_enable_device(pdev);
	if (err)
		goto err_out_free;

	if (!pci_resource_start(pdev, 0)) {
		pr_err("I/O base is zero\n");
		err = -ENODEV;
		goto err_out_disable;
	}

	if (pci_resource_len(pdev, 0) < (ULI526X_IO_SIZE) ) {
		pr_err("Allocated I/O size too small\n");
		err = -ENODEV;
		goto err_out_disable;
	}

	err = pci_request_regions(pdev, DRV_NAME);
	if (err < 0) {
		pr_err("Failed to request PCI regions\n");
		goto err_out_disable;
	}

	/* Init system & device */
	db = netdev_priv(dev);

	/* Allocate Tx/Rx descriptor memory */
	err = -ENOMEM;

	db->desc_pool_ptr = pci_alloc_consistent(pdev, sizeof(struct tx_desc) * DESC_ALL_CNT + 0x20, &db->desc_pool_dma_ptr);
	if (!db->desc_pool_ptr)
		goto err_out_release;

	db->buf_pool_ptr = pci_alloc_consistent(pdev, TX_BUF_ALLOC * TX_DESC_CNT + 4, &db->buf_pool_dma_ptr);
	if (!db->buf_pool_ptr)
		goto err_out_free_tx_desc;

	db->first_tx_desc = (struct tx_desc *) db->desc_pool_ptr;
	db->first_tx_desc_dma = db->desc_pool_dma_ptr;
	db->buf_pool_start = db->buf_pool_ptr;
	db->buf_pool_dma_start = db->buf_pool_dma_ptr;

	switch (ent->driver_data) {
	case PCI_ULI5263_ID:
		db->phy.write	= phy_writeby_cr10;
		db->phy.read	= phy_readby_cr10;
		break;
	default:
		db->phy.write	= phy_writeby_cr9;
		db->phy.read	= phy_readby_cr9;
		break;
	}

	/* IO region. */
	ioaddr = pci_iomap(pdev, 0, 0);
	if (!ioaddr)
		goto err_out_free_tx_buf;

	db->ioaddr = ioaddr;
	db->pdev = pdev;
	db->init = 1;

	pci_set_drvdata(pdev, dev);

	/* Register some necessary functions */
	dev->netdev_ops = &netdev_ops;
	dev->ethtool_ops = &netdev_ethtool_ops;

	spin_lock_init(&db->lock);


	/* read 64 word srom data */
	for (i = 0; i < 64; i++)
		((__le16 *) db->srom)[i] = cpu_to_le16(read_srom_word(db, i));

	/* Set Node address */
	if(((u16 *) db->srom)[0] == 0xffff || ((u16 *) db->srom)[0] == 0)		/* SROM absent, so read MAC address from ID Table */
	{
		uw32(DCR0, 0x10000);	//Diagnosis mode
		uw32(DCR13, 0x1c0);	//Reset dianostic pointer port
		uw32(DCR14, 0);		//Clear reset port
		uw32(DCR14, 0x10);	//Reset ID Table pointer
		uw32(DCR14, 0);		//Clear reset port
		uw32(DCR13, 0);		//Clear CR13
		uw32(DCR13, 0x1b0);	//Select ID Table access port
		//Read MAC address from CR14
		for (i = 0; i < 6; i++)
			dev->dev_addr[i] = ur32(DCR14);
		//Read end
		uw32(DCR13, 0);		//Clear CR13
		uw32(DCR0, 0);		//Clear CR0
		udelay(10);
	}
	else		/*Exist SROM*/
	{
		for (i = 0; i < 6; i++)
			dev->dev_addr[i] = db->srom[20 + i];
	}
	err = register_netdev (dev);
	if (err)
		goto err_out_unmap;

	netdev_info(dev, "ULi M%04lx at pci%s, %pM, irq %d\n",
		    ent->driver_data >> 16, pci_name(pdev),
		    dev->dev_addr, pdev->irq);

	pci_set_master(pdev);

	return 0;

err_out_unmap:
	pci_iounmap(pdev, db->ioaddr);
err_out_free_tx_buf:
	pci_free_consistent(pdev, TX_BUF_ALLOC * TX_DESC_CNT + 4,
			    db->buf_pool_ptr, db->buf_pool_dma_ptr);
err_out_free_tx_desc:
	pci_free_consistent(pdev, sizeof(struct tx_desc) * DESC_ALL_CNT + 0x20,
			    db->desc_pool_ptr, db->desc_pool_dma_ptr);
err_out_release:
	pci_release_regions(pdev);
err_out_disable:
	pci_disable_device(pdev);
err_out_free:
	free_netdev(dev);

	return err;
}


static void uli526x_remove_one(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct uli526x_board_info *db = netdev_priv(dev);

	unregister_netdev(dev);
	pci_iounmap(pdev, db->ioaddr);
	pci_free_consistent(db->pdev, sizeof(struct tx_desc) *
				DESC_ALL_CNT + 0x20, db->desc_pool_ptr,
 				db->desc_pool_dma_ptr);
	pci_free_consistent(db->pdev, TX_BUF_ALLOC * TX_DESC_CNT + 4,
				db->buf_pool_ptr, db->buf_pool_dma_ptr);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	free_netdev(dev);
}


/*
 *	Open the interface.
 *	The interface is opened whenever "ifconfig" activates it.
 */

static int uli526x_open(struct net_device *dev)
{
	int ret;
	struct uli526x_board_info *db = netdev_priv(dev);

	ULI526X_DBUG(0, "uli526x_open", 0);

	/* system variable init */
	db->cr6_data = CR6_DEFAULT | uli526x_cr6_user_set;
	db->tx_packet_cnt = 0;
	db->rx_avail_cnt = 0;
	db->link_failed = 1;
	netif_carrier_off(dev);
	db->wait_reset = 0;

	db->NIC_capability = 0xf;	/* All capability*/
	db->PHY_reg4 = 0x1e0;

	/* CR6 operation mode decision */
	db->cr6_data |= ULI526X_TXTH_256;
	db->cr0_data = CR0_DEFAULT;

	/* Initialize ULI526X board */
	uli526x_init(dev);

	ret = request_irq(db->pdev->irq, uli526x_interrupt, IRQF_SHARED,
			  dev->name, dev);
	if (ret)
		return ret;

	/* Active System Interface */
	netif_wake_queue(dev);

	/* set and active a timer process */
	timer_setup(&db->timer, uli526x_timer, 0);
	db->timer.expires = ULI526X_TIMER_WUT + HZ * 2;
	add_timer(&db->timer);

	return 0;
}


/*	Initialize ULI526X board
 *	Reset ULI526X board
 *	Initialize TX/Rx descriptor chain structure
 *	Send the set-up frame
 *	Enable Tx/Rx machine
 */

static void uli526x_init(struct net_device *dev)
{
	struct uli526x_board_info *db = netdev_priv(dev);
	struct uli_phy_ops *phy = &db->phy;
	void __iomem *ioaddr = db->ioaddr;
	u8	phy_tmp;
	u8	timeout;
	u16 phy_reg_reset;


	ULI526X_DBUG(0, "uli526x_init()", 0);

	/* Reset M526x MAC controller */
	uw32(DCR0, ULI526X_RESET);	/* RESET MAC */
	udelay(100);
	uw32(DCR0, db->cr0_data);
	udelay(5);

	/* Phy addr : In some boards,M5261/M5263 phy address != 1 */
	db->phy_addr = 1;
	for (phy_tmp = 0; phy_tmp < 32; phy_tmp++) {
		u16 phy_value;

		phy_value = phy->read(db, phy_tmp, 3);	//peer add
		if (phy_value != 0xffff && phy_value != 0) {
			db->phy_addr = phy_tmp;
			break;
		}
	}

	if (phy_tmp == 32)
		pr_warn("Can not find the phy address!!!\n");
	/* Parser SROM and media mode */
	db->media_mode = uli526x_media_mode;

	/* phyxcer capability setting */
	phy_reg_reset = phy->read(db, db->phy_addr, 0);
	phy_reg_reset = (phy_reg_reset | 0x8000);
	phy->write(db, db->phy_addr, 0, phy_reg_reset);

	/* See IEEE 802.3-2002.pdf (Section 2, Chapter "22.2.4 Management
	 * functions") or phy data sheet for details on phy reset
	 */
	udelay(500);
	timeout = 10;
	while (timeout-- && phy->read(db, db->phy_addr, 0) & 0x8000)
		udelay(100);

	/* Process Phyxcer Media Mode */
	uli526x_set_phyxcer(db);

	/* Media Mode Process */
	if ( !(db->media_mode & ULI526X_AUTO) )
		db->op_mode = db->media_mode;		/* Force Mode */

	/* Initialize Transmit/Receive descriptor and CR3/4 */
	uli526x_descriptor_init(dev, ioaddr);

	/* Init CR6 to program M526X operation */
	update_cr6(db->cr6_data, ioaddr);

	/* Send setup frame */
	send_filter_frame(dev, netdev_mc_count(dev));	/* M5261/M5263 */

	/* Init CR7, interrupt active bit */
	db->cr7_data = CR7_DEFAULT;
	uw32(DCR7, db->cr7_data);

	/* Init CR15, Tx jabber and Rx watchdog timer */
	uw32(DCR15, db->cr15_data);

	/* Enable ULI526X Tx/Rx function */
	db->cr6_data |= CR6_RXSC | CR6_TXSC;
	update_cr6(db->cr6_data, ioaddr);
}


/*
 *	Hardware start transmission.
 *	Send a packet to media from the upper layer.
 */

static netdev_tx_t uli526x_start_xmit(struct sk_buff *skb,
					    struct net_device *dev)
{
	struct uli526x_board_info *db = netdev_priv(dev);
	void __iomem *ioaddr = db->ioaddr;
	struct tx_desc *txptr;
	unsigned long flags;

	ULI526X_DBUG(0, "uli526x_start_xmit", 0);

	/* Resource flag check */
	netif_stop_queue(dev);

	/* Too large packet check */
	if (skb->len > MAX_PACKET_SIZE) {
		netdev_err(dev, "big packet = %d\n", (u16)skb->len);
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	spin_lock_irqsave(&db->lock, flags);

	/* No Tx resource check, it never happen nromally */
	if (db->tx_packet_cnt >= TX_FREE_DESC_CNT) {
		spin_unlock_irqrestore(&db->lock, flags);
		netdev_err(dev, "No Tx resource %ld\n", db->tx_packet_cnt);
		return NETDEV_TX_BUSY;
	}

	/* Disable NIC interrupt */
	uw32(DCR7, 0);

	/* transmit this packet */
	txptr = db->tx_insert_ptr;
	skb_copy_from_linear_data(skb, txptr->tx_buf_ptr, skb->len);
	txptr->tdes1 = cpu_to_le32(0xe1000000 | skb->len);

	/* Point to next transmit free descriptor */
	db->tx_insert_ptr = txptr->next_tx_desc;

	/* Transmit Packet Process */
	if (db->tx_packet_cnt < TX_DESC_CNT) {
		txptr->tdes0 = cpu_to_le32(0x80000000);	/* Set owner bit */
		db->tx_packet_cnt++;			/* Ready to send */
		uw32(DCR1, 0x1);			/* Issue Tx polling */
		netif_trans_update(dev);		/* saved time stamp */
	}

	/* Tx resource check */
	if ( db->tx_packet_cnt < TX_FREE_DESC_CNT )
		netif_wake_queue(dev);

	/* Restore CR7 to enable interrupt */
	spin_unlock_irqrestore(&db->lock, flags);
	uw32(DCR7, db->cr7_data);

	/* free this SKB */
	dev_consume_skb_any(skb);

	return NETDEV_TX_OK;
}


/*
 *	Stop the interface.
 *	The interface is stopped when it is brought.
 */

static int uli526x_stop(struct net_device *dev)
{
	struct uli526x_board_info *db = netdev_priv(dev);
	void __iomem *ioaddr = db->ioaddr;

	/* disable system */
	netif_stop_queue(dev);

	/* deleted timer */
	del_timer_sync(&db->timer);

	/* Reset & stop ULI526X board */
	uw32(DCR0, ULI526X_RESET);
	udelay(5);
	db->phy.write(db, db->phy_addr, 0, 0x8000);

	/* free interrupt */
	free_irq(db->pdev->irq, dev);

	/* free allocated rx buffer */
	uli526x_free_rxbuffer(db);

	return 0;
}


/*
 *	M5261/M5263 insterrupt handler
 *	receive the packet to upper layer, free the transmitted packet
 */

static irqreturn_t uli526x_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct uli526x_board_info *db = netdev_priv(dev);
	void __iomem *ioaddr = db->ioaddr;
	unsigned long flags;

	spin_lock_irqsave(&db->lock, flags);
	uw32(DCR7, 0);

	/* Got ULI526X status */
	db->cr5_data = ur32(DCR5);
	uw32(DCR5, db->cr5_data);
	if ( !(db->cr5_data & 0x180c1) ) {
		/* Restore CR7 to enable interrupt mask */
		uw32(DCR7, db->cr7_data);
		spin_unlock_irqrestore(&db->lock, flags);
		return IRQ_HANDLED;
	}

	/* Check system status */
	if (db->cr5_data & 0x2000) {
		/* system bus error happen */
		ULI526X_DBUG(1, "System bus error happen. CR5=", db->cr5_data);
		db->reset_fatal++;
		db->wait_reset = 1;	/* Need to RESET */
		spin_unlock_irqrestore(&db->lock, flags);
		return IRQ_HANDLED;
	}

	 /* Received the coming packet */
	if ( (db->cr5_data & 0x40) && db->rx_avail_cnt )
		uli526x_rx_packet(dev, db);

	/* reallocate rx descriptor buffer */
	if (db->rx_avail_cnt<RX_DESC_CNT)
		allocate_rx_buffer(dev);

	/* Free the transmitted descriptor */
	if ( db->cr5_data & 0x01)
		uli526x_free_tx_pkt(dev, db);

	/* Restore CR7 to enable interrupt mask */
	uw32(DCR7, db->cr7_data);

	spin_unlock_irqrestore(&db->lock, flags);
	return IRQ_HANDLED;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void uli526x_poll(struct net_device *dev)
{
	struct uli526x_board_info *db = netdev_priv(dev);

	/* ISR grabs the irqsave lock, so this should be safe */
	uli526x_interrupt(db->pdev->irq, dev);
}
#endif

/*
 *	Free TX resource after TX complete
 */

static void uli526x_free_tx_pkt(struct net_device *dev,
				struct uli526x_board_info * db)
{
	struct tx_desc *txptr;
	u32 tdes0;

	txptr = db->tx_remove_ptr;
	while(db->tx_packet_cnt) {
		tdes0 = le32_to_cpu(txptr->tdes0);
		if (tdes0 & 0x80000000)
			break;

		/* A packet sent completed */
		db->tx_packet_cnt--;
		dev->stats.tx_packets++;

		/* Transmit statistic counter */
		if ( tdes0 != 0x7fffffff ) {
			dev->stats.collisions += (tdes0 >> 3) & 0xf;
			dev->stats.tx_bytes += le32_to_cpu(txptr->tdes1) & 0x7ff;
			if (tdes0 & TDES0_ERR_MASK) {
				dev->stats.tx_errors++;
				if (tdes0 & 0x0002) {	/* UnderRun */
					db->tx_fifo_underrun++;
					if ( !(db->cr6_data & CR6_SFT) ) {
						db->cr6_data = db->cr6_data | CR6_SFT;
						update_cr6(db->cr6_data, db->ioaddr);
					}
				}
				if (tdes0 & 0x0100)
					db->tx_excessive_collision++;
				if (tdes0 & 0x0200)
					db->tx_late_collision++;
				if (tdes0 & 0x0400)
					db->tx_no_carrier++;
				if (tdes0 & 0x0800)
					db->tx_loss_carrier++;
				if (tdes0 & 0x4000)
					db->tx_jabber_timeout++;
			}
		}

    		txptr = txptr->next_tx_desc;
	}/* End of while */

	/* Update TX remove pointer to next */
	db->tx_remove_ptr = txptr;

	/* Resource available check */
	if ( db->tx_packet_cnt < TX_WAKE_DESC_CNT )
		netif_wake_queue(dev);	/* Active upper layer, send again */
}


/*
 *	Receive the come packet and pass to upper layer
 */

static void uli526x_rx_packet(struct net_device *dev, struct uli526x_board_info * db)
{
	struct rx_desc *rxptr;
	struct sk_buff *skb;
	int rxlen;
	u32 rdes0;

	rxptr = db->rx_ready_ptr;

	while(db->rx_avail_cnt) {
		rdes0 = le32_to_cpu(rxptr->rdes0);
		if (rdes0 & 0x80000000)	/* packet owner check */
		{
			break;
		}

		db->rx_avail_cnt--;
		db->interval_rx_cnt++;

		pci_unmap_single(db->pdev, le32_to_cpu(rxptr->rdes2), RX_ALLOC_SIZE, PCI_DMA_FROMDEVICE);
		if ( (rdes0 & 0x300) != 0x300) {
			/* A packet without First/Last flag */
			/* reuse this SKB */
			ULI526X_DBUG(0, "Reuse SK buffer, rdes0", rdes0);
			uli526x_reuse_skb(db, rxptr->rx_skb_ptr);
		} else {
			/* A packet with First/Last flag */
			rxlen = ( (rdes0 >> 16) & 0x3fff) - 4;

			/* error summary bit check */
			if (rdes0 & 0x8000) {
				/* This is a error packet */
				dev->stats.rx_errors++;
				if (rdes0 & 1)
					dev->stats.rx_fifo_errors++;
				if (rdes0 & 2)
					dev->stats.rx_crc_errors++;
				if (rdes0 & 0x80)
					dev->stats.rx_length_errors++;
			}

			if ( !(rdes0 & 0x8000) ||
				((db->cr6_data & CR6_PM) && (rxlen>6)) ) {
				struct sk_buff *new_skb = NULL;

				skb = rxptr->rx_skb_ptr;

				/* Good packet, send to upper layer */
				/* Shorst packet used new SKB */
				if ((rxlen < RX_COPY_SIZE) &&
				    (((new_skb = netdev_alloc_skb(dev, rxlen + 2)) != NULL))) {
					skb = new_skb;
					/* size less than COPY_SIZE, allocate a rxlen SKB */
					skb_reserve(skb, 2); /* 16byte align */
					skb_put_data(skb,
						     skb_tail_pointer(rxptr->rx_skb_ptr),
						     rxlen);
					uli526x_reuse_skb(db, rxptr->rx_skb_ptr);
				} else
					skb_put(skb, rxlen);

				skb->protocol = eth_type_trans(skb, dev);
				netif_rx(skb);
				dev->stats.rx_packets++;
				dev->stats.rx_bytes += rxlen;

			} else {
				/* Reuse SKB buffer when the packet is error */
				ULI526X_DBUG(0, "Reuse SK buffer, rdes0", rdes0);
				uli526x_reuse_skb(db, rxptr->rx_skb_ptr);
			}
		}

		rxptr = rxptr->next_rx_desc;
	}

	db->rx_ready_ptr = rxptr;
}


/*
 * Set ULI526X multicast address
 */

static void uli526x_set_filter_mode(struct net_device * dev)
{
	struct uli526x_board_info *db = netdev_priv(dev);
	unsigned long flags;

	ULI526X_DBUG(0, "uli526x_set_filter_mode()", 0);
	spin_lock_irqsave(&db->lock, flags);

	if (dev->flags & IFF_PROMISC) {
		ULI526X_DBUG(0, "Enable PROM Mode", 0);
		db->cr6_data |= CR6_PM | CR6_PBF;
		update_cr6(db->cr6_data, db->ioaddr);
		spin_unlock_irqrestore(&db->lock, flags);
		return;
	}

	if (dev->flags & IFF_ALLMULTI ||
	    netdev_mc_count(dev) > ULI5261_MAX_MULTICAST) {
		ULI526X_DBUG(0, "Pass all multicast address",
			     netdev_mc_count(dev));
		db->cr6_data &= ~(CR6_PM | CR6_PBF);
		db->cr6_data |= CR6_PAM;
		spin_unlock_irqrestore(&db->lock, flags);
		return;
	}

	ULI526X_DBUG(0, "Set multicast address", netdev_mc_count(dev));
	send_filter_frame(dev, netdev_mc_count(dev)); 	/* M5261/M5263 */
	spin_unlock_irqrestore(&db->lock, flags);
}

static void
ULi_ethtool_get_link_ksettings(struct uli526x_board_info *db,
			       struct ethtool_link_ksettings *cmd)
{
	u32 supported, advertising;

	supported = (SUPPORTED_10baseT_Half |
	                   SUPPORTED_10baseT_Full |
	                   SUPPORTED_100baseT_Half |
	                   SUPPORTED_100baseT_Full |
	                   SUPPORTED_Autoneg |
	                   SUPPORTED_MII);

	advertising = (ADVERTISED_10baseT_Half |
	                   ADVERTISED_10baseT_Full |
	                   ADVERTISED_100baseT_Half |
	                   ADVERTISED_100baseT_Full |
	                   ADVERTISED_Autoneg |
	                   ADVERTISED_MII);

	ethtool_convert_legacy_u32_to_link_mode(cmd->link_modes.supported,
						supported);
	ethtool_convert_legacy_u32_to_link_mode(cmd->link_modes.advertising,
						advertising);

	cmd->base.port = PORT_MII;
	cmd->base.phy_address = db->phy_addr;

	cmd->base.speed = SPEED_10;
	cmd->base.duplex = DUPLEX_HALF;

	if(db->op_mode==ULI526X_100MHF || db->op_mode==ULI526X_100MFD)
	{
		cmd->base.speed = SPEED_100;
	}
	if(db->op_mode==ULI526X_10MFD || db->op_mode==ULI526X_100MFD)
	{
		cmd->base.duplex = DUPLEX_FULL;
	}
	if(db->link_failed)
	{
		cmd->base.speed = SPEED_UNKNOWN;
		cmd->base.duplex = DUPLEX_UNKNOWN;
	}

	if (db->media_mode & ULI526X_AUTO)
	{
		cmd->base.autoneg = AUTONEG_ENABLE;
	}
}

static void netdev_get_drvinfo(struct net_device *dev,
			       struct ethtool_drvinfo *info)
{
	struct uli526x_board_info *np = netdev_priv(dev);

	strlcpy(info->driver, DRV_NAME, sizeof(info->driver));
	strlcpy(info->version, DRV_VERSION, sizeof(info->version));
	strlcpy(info->bus_info, pci_name(np->pdev), sizeof(info->bus_info));
}

static int netdev_get_link_ksettings(struct net_device *dev,
				     struct ethtool_link_ksettings *cmd)
{
	struct uli526x_board_info *np = netdev_priv(dev);

	ULi_ethtool_get_link_ksettings(np, cmd);

	return 0;
}

static u32 netdev_get_link(struct net_device *dev) {
	struct uli526x_board_info *np = netdev_priv(dev);

	if(np->link_failed)
		return 0;
	else
		return 1;
}

static void uli526x_get_wol(struct net_device *dev, struct ethtool_wolinfo *wol)
{
	wol->supported = WAKE_PHY | WAKE_MAGIC;
	wol->wolopts = 0;
}

static const struct ethtool_ops netdev_ethtool_ops = {
	.get_drvinfo		= netdev_get_drvinfo,
	.get_link		= netdev_get_link,
	.get_wol		= uli526x_get_wol,
	.get_link_ksettings	= netdev_get_link_ksettings,
};

/*
 *	A periodic timer routine
 *	Dynamic media sense, allocate Rx buffer...
 */

static void uli526x_timer(struct timer_list *t)
{
	struct uli526x_board_info *db = from_timer(db, t, timer);
	struct net_device *dev = pci_get_drvdata(db->pdev);
	struct uli_phy_ops *phy = &db->phy;
	void __iomem *ioaddr = db->ioaddr;
 	unsigned long flags;
	u8 tmp_cr12 = 0;
	u32 tmp_cr8;

	//ULI526X_DBUG(0, "uli526x_timer()", 0);
	spin_lock_irqsave(&db->lock, flags);


	/* Dynamic reset ULI526X : system error or transmit time-out */
	tmp_cr8 = ur32(DCR8);
	if ( (db->interval_rx_cnt==0) && (tmp_cr8) ) {
		db->reset_cr8++;
		db->wait_reset = 1;
	}
	db->interval_rx_cnt = 0;

	/* TX polling kick monitor */
	if ( db->tx_packet_cnt &&
	     time_after(jiffies, dev_trans_start(dev) + ULI526X_TX_KICK) ) {
		uw32(DCR1, 0x1);   // Tx polling again

		// TX Timeout
		if ( time_after(jiffies, dev_trans_start(dev) + ULI526X_TX_TIMEOUT) ) {
			db->reset_TXtimeout++;
			db->wait_reset = 1;
			netdev_err(dev, " Tx timeout - resetting\n");
		}
	}

	if (db->wait_reset) {
		ULI526X_DBUG(0, "Dynamic Reset device", db->tx_packet_cnt);
		db->reset_count++;
		uli526x_dynamic_reset(dev);
		db->timer.expires = ULI526X_TIMER_WUT;
		add_timer(&db->timer);
		spin_unlock_irqrestore(&db->lock, flags);
		return;
	}

	/* Link status check, Dynamic media type change */
	if ((phy->read(db, db->phy_addr, 5) & 0x01e0)!=0)
		tmp_cr12 = 3;

	if ( !(tmp_cr12 & 0x3) && !db->link_failed ) {
		/* Link Failed */
		ULI526X_DBUG(0, "Link Failed", tmp_cr12);
		netif_carrier_off(dev);
		netdev_info(dev, "NIC Link is Down\n");
		db->link_failed = 1;

		/* For Force 10/100M Half/Full mode: Enable Auto-Nego mode */
		/* AUTO don't need */
		if ( !(db->media_mode & 0x8) )
			phy->write(db, db->phy_addr, 0, 0x1000);

		/* AUTO mode, if INT phyxcer link failed, select EXT device */
		if (db->media_mode & ULI526X_AUTO) {
			db->cr6_data&=~0x00000200;	/* bit9=0, HD mode */
			update_cr6(db->cr6_data, db->ioaddr);
		}
	} else
		if ((tmp_cr12 & 0x3) && db->link_failed) {
			ULI526X_DBUG(0, "Link link OK", tmp_cr12);
			db->link_failed = 0;

			/* Auto Sense Speed */
			if ( (db->media_mode & ULI526X_AUTO) &&
				uli526x_sense_speed(db) )
				db->link_failed = 1;
			uli526x_process_mode(db);

			if(db->link_failed==0)
			{
				netdev_info(dev, "NIC Link is Up %d Mbps %s duplex\n",
					    (db->op_mode == ULI526X_100MHF ||
					     db->op_mode == ULI526X_100MFD)
					    ? 100 : 10,
					    (db->op_mode == ULI526X_10MFD ||
					     db->op_mode == ULI526X_100MFD)
					    ? "Full" : "Half");
				netif_carrier_on(dev);
			}
			/* SHOW_MEDIA_TYPE(db->op_mode); */
		}
		else if(!(tmp_cr12 & 0x3) && db->link_failed)
		{
			if(db->init==1)
			{
				netdev_info(dev, "NIC Link is Down\n");
				netif_carrier_off(dev);
			}
		}
	db->init = 0;

	/* Timer active again */
	db->timer.expires = ULI526X_TIMER_WUT;
	add_timer(&db->timer);
	spin_unlock_irqrestore(&db->lock, flags);
}


/*
 *	Stop ULI526X board
 *	Free Tx/Rx allocated memory
 *	Init system variable
 */

static void uli526x_reset_prepare(struct net_device *dev)
{
	struct uli526x_board_info *db = netdev_priv(dev);
	void __iomem *ioaddr = db->ioaddr;

	/* Sopt MAC controller */
	db->cr6_data &= ~(CR6_RXSC | CR6_TXSC);	/* Disable Tx/Rx */
	update_cr6(db->cr6_data, ioaddr);
	uw32(DCR7, 0);				/* Disable Interrupt */
	uw32(DCR5, ur32(DCR5));

	/* Disable upper layer interface */
	netif_stop_queue(dev);

	/* Free Rx Allocate buffer */
	uli526x_free_rxbuffer(db);

	/* system variable init */
	db->tx_packet_cnt = 0;
	db->rx_avail_cnt = 0;
	db->link_failed = 1;
	db->init=1;
	db->wait_reset = 0;
}


/*
 *	Dynamic reset the ULI526X board
 *	Stop ULI526X board
 *	Free Tx/Rx allocated memory
 *	Reset ULI526X board
 *	Re-initialize ULI526X board
 */

static void uli526x_dynamic_reset(struct net_device *dev)
{
	ULI526X_DBUG(0, "uli526x_dynamic_reset()", 0);

	uli526x_reset_prepare(dev);

	/* Re-initialize ULI526X board */
	uli526x_init(dev);

	/* Restart upper layer interface */
	netif_wake_queue(dev);
}


#ifdef CONFIG_PM

/*
 *	Suspend the interface.
 */

static int uli526x_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	pci_power_t power_state;
	int err;

	ULI526X_DBUG(0, "uli526x_suspend", 0);

	pci_save_state(pdev);

	if (!netif_running(dev))
		return 0;

	netif_device_detach(dev);
	uli526x_reset_prepare(dev);

	power_state = pci_choose_state(pdev, state);
	pci_enable_wake(pdev, power_state, 0);
	err = pci_set_power_state(pdev, power_state);
	if (err) {
		netif_device_attach(dev);
		/* Re-initialize ULI526X board */
		uli526x_init(dev);
		/* Restart upper layer interface */
		netif_wake_queue(dev);
	}

	return err;
}

/*
 *	Resume the interface.
 */

static int uli526x_resume(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	int err;

	ULI526X_DBUG(0, "uli526x_resume", 0);

	pci_restore_state(pdev);

	if (!netif_running(dev))
		return 0;

	err = pci_set_power_state(pdev, PCI_D0);
	if (err) {
		netdev_warn(dev, "Could not put device into D0\n");
		return err;
	}

	netif_device_attach(dev);
	/* Re-initialize ULI526X board */
	uli526x_init(dev);
	/* Restart upper layer interface */
	netif_wake_queue(dev);

	return 0;
}

#else /* !CONFIG_PM */

#define uli526x_suspend	NULL
#define uli526x_resume	NULL

#endif /* !CONFIG_PM */


/*
 *	free all allocated rx buffer
 */

static void uli526x_free_rxbuffer(struct uli526x_board_info * db)
{
	ULI526X_DBUG(0, "uli526x_free_rxbuffer()", 0);

	/* free allocated rx buffer */
	while (db->rx_avail_cnt) {
		dev_kfree_skb(db->rx_ready_ptr->rx_skb_ptr);
		db->rx_ready_ptr = db->rx_ready_ptr->next_rx_desc;
		db->rx_avail_cnt--;
	}
}


/*
 *	Reuse the SK buffer
 */

static void uli526x_reuse_skb(struct uli526x_board_info *db, struct sk_buff * skb)
{
	struct rx_desc *rxptr = db->rx_insert_ptr;

	if (!(rxptr->rdes0 & cpu_to_le32(0x80000000))) {
		rxptr->rx_skb_ptr = skb;
		rxptr->rdes2 = cpu_to_le32(pci_map_single(db->pdev,
							  skb_tail_pointer(skb),
							  RX_ALLOC_SIZE,
							  PCI_DMA_FROMDEVICE));
		wmb();
		rxptr->rdes0 = cpu_to_le32(0x80000000);
		db->rx_avail_cnt++;
		db->rx_insert_ptr = rxptr->next_rx_desc;
	} else
		ULI526X_DBUG(0, "SK Buffer reuse method error", db->rx_avail_cnt);
}


/*
 *	Initialize transmit/Receive descriptor
 *	Using Chain structure, and allocate Tx/Rx buffer
 */

static void uli526x_descriptor_init(struct net_device *dev, void __iomem *ioaddr)
{
	struct uli526x_board_info *db = netdev_priv(dev);
	struct tx_desc *tmp_tx;
	struct rx_desc *tmp_rx;
	unsigned char *tmp_buf;
	dma_addr_t tmp_tx_dma, tmp_rx_dma;
	dma_addr_t tmp_buf_dma;
	int i;

	ULI526X_DBUG(0, "uli526x_descriptor_init()", 0);

	/* tx descriptor start pointer */
	db->tx_insert_ptr = db->first_tx_desc;
	db->tx_remove_ptr = db->first_tx_desc;
	uw32(DCR4, db->first_tx_desc_dma);	/* TX DESC address */

	/* rx descriptor start pointer */
	db->first_rx_desc = (void *)db->first_tx_desc + sizeof(struct tx_desc) * TX_DESC_CNT;
	db->first_rx_desc_dma =  db->first_tx_desc_dma + sizeof(struct tx_desc) * TX_DESC_CNT;
	db->rx_insert_ptr = db->first_rx_desc;
	db->rx_ready_ptr = db->first_rx_desc;
	uw32(DCR3, db->first_rx_desc_dma);	/* RX DESC address */

	/* Init Transmit chain */
	tmp_buf = db->buf_pool_start;
	tmp_buf_dma = db->buf_pool_dma_start;
	tmp_tx_dma = db->first_tx_desc_dma;
	for (tmp_tx = db->first_tx_desc, i = 0; i < TX_DESC_CNT; i++, tmp_tx++) {
		tmp_tx->tx_buf_ptr = tmp_buf;
		tmp_tx->tdes0 = cpu_to_le32(0);
		tmp_tx->tdes1 = cpu_to_le32(0x81000000);	/* IC, chain */
		tmp_tx->tdes2 = cpu_to_le32(tmp_buf_dma);
		tmp_tx_dma += sizeof(struct tx_desc);
		tmp_tx->tdes3 = cpu_to_le32(tmp_tx_dma);
		tmp_tx->next_tx_desc = tmp_tx + 1;
		tmp_buf = tmp_buf + TX_BUF_ALLOC;
		tmp_buf_dma = tmp_buf_dma + TX_BUF_ALLOC;
	}
	(--tmp_tx)->tdes3 = cpu_to_le32(db->first_tx_desc_dma);
	tmp_tx->next_tx_desc = db->first_tx_desc;

	 /* Init Receive descriptor chain */
	tmp_rx_dma=db->first_rx_desc_dma;
	for (tmp_rx = db->first_rx_desc, i = 0; i < RX_DESC_CNT; i++, tmp_rx++) {
		tmp_rx->rdes0 = cpu_to_le32(0);
		tmp_rx->rdes1 = cpu_to_le32(0x01000600);
		tmp_rx_dma += sizeof(struct rx_desc);
		tmp_rx->rdes3 = cpu_to_le32(tmp_rx_dma);
		tmp_rx->next_rx_desc = tmp_rx + 1;
	}
	(--tmp_rx)->rdes3 = cpu_to_le32(db->first_rx_desc_dma);
	tmp_rx->next_rx_desc = db->first_rx_desc;

	/* pre-allocate Rx buffer */
	allocate_rx_buffer(dev);
}


/*
 *	Update CR6 value
 *	Firstly stop ULI526X, then written value and start
 */
static void update_cr6(u32 cr6_data, void __iomem *ioaddr)
{
	uw32(DCR6, cr6_data);
	udelay(5);
}


/*
 *	Send a setup frame for M5261/M5263
 *	This setup frame initialize ULI526X address filter mode
 */

#ifdef __BIG_ENDIAN
#define FLT_SHIFT 16
#else
#define FLT_SHIFT 0
#endif

static void send_filter_frame(struct net_device *dev, int mc_cnt)
{
	struct uli526x_board_info *db = netdev_priv(dev);
	void __iomem *ioaddr = db->ioaddr;
	struct netdev_hw_addr *ha;
	struct tx_desc *txptr;
	u16 * addrptr;
	u32 * suptr;
	int i;

	ULI526X_DBUG(0, "send_filter_frame()", 0);

	txptr = db->tx_insert_ptr;
	suptr = (u32 *) txptr->tx_buf_ptr;

	/* Node address */
	addrptr = (u16 *) dev->dev_addr;
	*suptr++ = addrptr[0] << FLT_SHIFT;
	*suptr++ = addrptr[1] << FLT_SHIFT;
	*suptr++ = addrptr[2] << FLT_SHIFT;

	/* broadcast address */
	*suptr++ = 0xffff << FLT_SHIFT;
	*suptr++ = 0xffff << FLT_SHIFT;
	*suptr++ = 0xffff << FLT_SHIFT;

	/* fit the multicast address */
	netdev_for_each_mc_addr(ha, dev) {
		addrptr = (u16 *) ha->addr;
		*suptr++ = addrptr[0] << FLT_SHIFT;
		*suptr++ = addrptr[1] << FLT_SHIFT;
		*suptr++ = addrptr[2] << FLT_SHIFT;
	}

	for (i = netdev_mc_count(dev); i < 14; i++) {
		*suptr++ = 0xffff << FLT_SHIFT;
		*suptr++ = 0xffff << FLT_SHIFT;
		*suptr++ = 0xffff << FLT_SHIFT;
	}

	/* prepare the setup frame */
	db->tx_insert_ptr = txptr->next_tx_desc;
	txptr->tdes1 = cpu_to_le32(0x890000c0);

	/* Resource Check and Send the setup packet */
	if (db->tx_packet_cnt < TX_DESC_CNT) {
		/* Resource Empty */
		db->tx_packet_cnt++;
		txptr->tdes0 = cpu_to_le32(0x80000000);
		update_cr6(db->cr6_data | 0x2000, ioaddr);
		uw32(DCR1, 0x1);	/* Issue Tx polling */
		update_cr6(db->cr6_data, ioaddr);
		netif_trans_update(dev);
	} else
		netdev_err(dev, "No Tx resource - Send_filter_frame!\n");
}


/*
 *	Allocate rx buffer,
 *	As possible as allocate maxiumn Rx buffer
 */

static void allocate_rx_buffer(struct net_device *dev)
{
	struct uli526x_board_info *db = netdev_priv(dev);
	struct rx_desc *rxptr;
	struct sk_buff *skb;

	rxptr = db->rx_insert_ptr;

	while(db->rx_avail_cnt < RX_DESC_CNT) {
		skb = netdev_alloc_skb(dev, RX_ALLOC_SIZE);
		if (skb == NULL)
			break;
		rxptr->rx_skb_ptr = skb; /* FIXME (?) */
		rxptr->rdes2 = cpu_to_le32(pci_map_single(db->pdev,
							  skb_tail_pointer(skb),
							  RX_ALLOC_SIZE,
							  PCI_DMA_FROMDEVICE));
		wmb();
		rxptr->rdes0 = cpu_to_le32(0x80000000);
		rxptr = rxptr->next_rx_desc;
		db->rx_avail_cnt++;
	}

	db->rx_insert_ptr = rxptr;
}


/*
 *	Read one word data from the serial ROM
 */

static u16 read_srom_word(struct uli526x_board_info *db, int offset)
{
	void __iomem *ioaddr = db->ioaddr;
	u16 srom_data = 0;
	int i;

	uw32(DCR9, CR9_SROM_READ);
	uw32(DCR9, CR9_SROM_READ | CR9_SRCS);

	/* Send the Read Command 110b */
	srom_clk_write(db, SROM_DATA_1);
	srom_clk_write(db, SROM_DATA_1);
	srom_clk_write(db, SROM_DATA_0);

	/* Send the offset */
	for (i = 5; i >= 0; i--) {
		srom_data = (offset & (1 << i)) ? SROM_DATA_1 : SROM_DATA_0;
		srom_clk_write(db, srom_data);
	}

	uw32(DCR9, CR9_SROM_READ | CR9_SRCS);

	for (i = 16; i > 0; i--) {
		uw32(DCR9, CR9_SROM_READ | CR9_SRCS | CR9_SRCLK);
		udelay(5);
		srom_data = (srom_data << 1) |
			    ((ur32(DCR9) & CR9_CRDOUT) ? 1 : 0);
		uw32(DCR9, CR9_SROM_READ | CR9_SRCS);
		udelay(5);
	}

	uw32(DCR9, CR9_SROM_READ);
	return srom_data;
}


/*
 *	Auto sense the media mode
 */

static u8 uli526x_sense_speed(struct uli526x_board_info * db)
{
	struct uli_phy_ops *phy = &db->phy;
	u8 ErrFlag = 0;
	u16 phy_mode;

	phy_mode = phy->read(db, db->phy_addr, 1);
	phy_mode = phy->read(db, db->phy_addr, 1);

	if ( (phy_mode & 0x24) == 0x24 ) {

		phy_mode = ((phy->read(db, db->phy_addr, 5) & 0x01e0)<<7);
		if(phy_mode&0x8000)
			phy_mode = 0x8000;
		else if(phy_mode&0x4000)
			phy_mode = 0x4000;
		else if(phy_mode&0x2000)
			phy_mode = 0x2000;
		else
			phy_mode = 0x1000;

		switch (phy_mode) {
		case 0x1000: db->op_mode = ULI526X_10MHF; break;
		case 0x2000: db->op_mode = ULI526X_10MFD; break;
		case 0x4000: db->op_mode = ULI526X_100MHF; break;
		case 0x8000: db->op_mode = ULI526X_100MFD; break;
		default: db->op_mode = ULI526X_10MHF; ErrFlag = 1; break;
		}
	} else {
		db->op_mode = ULI526X_10MHF;
		ULI526X_DBUG(0, "Link Failed :", phy_mode);
		ErrFlag = 1;
	}

	return ErrFlag;
}


/*
 *	Set 10/100 phyxcer capability
 *	AUTO mode : phyxcer register4 is NIC capability
 *	Force mode: phyxcer register4 is the force media
 */

static void uli526x_set_phyxcer(struct uli526x_board_info *db)
{
	struct uli_phy_ops *phy = &db->phy;
	u16 phy_reg;

	/* Phyxcer capability setting */
	phy_reg = phy->read(db, db->phy_addr, 4) & ~0x01e0;

	if (db->media_mode & ULI526X_AUTO) {
		/* AUTO Mode */
		phy_reg |= db->PHY_reg4;
	} else {
		/* Force Mode */
		switch(db->media_mode) {
		case ULI526X_10MHF: phy_reg |= 0x20; break;
		case ULI526X_10MFD: phy_reg |= 0x40; break;
		case ULI526X_100MHF: phy_reg |= 0x80; break;
		case ULI526X_100MFD: phy_reg |= 0x100; break;
		}

	}

  	/* Write new capability to Phyxcer Reg4 */
	if ( !(phy_reg & 0x01e0)) {
		phy_reg|=db->PHY_reg4;
		db->media_mode|=ULI526X_AUTO;
	}
	phy->write(db, db->phy_addr, 4, phy_reg);

 	/* Restart Auto-Negotiation */
	phy->write(db, db->phy_addr, 0, 0x1200);
	udelay(50);
}


/*
 *	Process op-mode
 	AUTO mode : PHY controller in Auto-negotiation Mode
 *	Force mode: PHY controller in force mode with HUB
 *			N-way force capability with SWITCH
 */

static void uli526x_process_mode(struct uli526x_board_info *db)
{
	struct uli_phy_ops *phy = &db->phy;
	u16 phy_reg;

	/* Full Duplex Mode Check */
	if (db->op_mode & 0x4)
		db->cr6_data |= CR6_FDM;	/* Set Full Duplex Bit */
	else
		db->cr6_data &= ~CR6_FDM;	/* Clear Full Duplex Bit */

	update_cr6(db->cr6_data, db->ioaddr);

	/* 10/100M phyxcer force mode need */
	if (!(db->media_mode & 0x8)) {
		/* Forece Mode */
		phy_reg = phy->read(db, db->phy_addr, 6);
		if (!(phy_reg & 0x1)) {
			/* parter without N-Way capability */
			phy_reg = 0x0;
			switch(db->op_mode) {
			case ULI526X_10MHF: phy_reg = 0x0; break;
			case ULI526X_10MFD: phy_reg = 0x100; break;
			case ULI526X_100MHF: phy_reg = 0x2000; break;
			case ULI526X_100MFD: phy_reg = 0x2100; break;
			}
			phy->write(db, db->phy_addr, 0, phy_reg);
		}
	}
}


/* M5261/M5263 Chip */
static void phy_writeby_cr9(struct uli526x_board_info *db, u8 phy_addr,
			    u8 offset, u16 phy_data)
{
	u16 i;

	/* Send 33 synchronization clock to Phy controller */
	for (i = 0; i < 35; i++)
		phy_write_1bit(db, PHY_DATA_1);

	/* Send start command(01) to Phy */
	phy_write_1bit(db, PHY_DATA_0);
	phy_write_1bit(db, PHY_DATA_1);

	/* Send write command(01) to Phy */
	phy_write_1bit(db, PHY_DATA_0);
	phy_write_1bit(db, PHY_DATA_1);

	/* Send Phy address */
	for (i = 0x10; i > 0; i = i >> 1)
		phy_write_1bit(db, phy_addr & i ? PHY_DATA_1 : PHY_DATA_0);

	/* Send register address */
	for (i = 0x10; i > 0; i = i >> 1)
		phy_write_1bit(db, offset & i ? PHY_DATA_1 : PHY_DATA_0);

	/* written trasnition */
	phy_write_1bit(db, PHY_DATA_1);
	phy_write_1bit(db, PHY_DATA_0);

	/* Write a word data to PHY controller */
	for (i = 0x8000; i > 0; i >>= 1)
		phy_write_1bit(db, phy_data & i ? PHY_DATA_1 : PHY_DATA_0);
}

static u16 phy_readby_cr9(struct uli526x_board_info *db, u8 phy_addr, u8 offset)
{
	u16 phy_data;
	int i;

	/* Send 33 synchronization clock to Phy controller */
	for (i = 0; i < 35; i++)
		phy_write_1bit(db, PHY_DATA_1);

	/* Send start command(01) to Phy */
	phy_write_1bit(db, PHY_DATA_0);
	phy_write_1bit(db, PHY_DATA_1);

	/* Send read command(10) to Phy */
	phy_write_1bit(db, PHY_DATA_1);
	phy_write_1bit(db, PHY_DATA_0);

	/* Send Phy address */
	for (i = 0x10; i > 0; i = i >> 1)
		phy_write_1bit(db, phy_addr & i ? PHY_DATA_1 : PHY_DATA_0);

	/* Send register address */
	for (i = 0x10; i > 0; i = i >> 1)
		phy_write_1bit(db, offset & i ? PHY_DATA_1 : PHY_DATA_0);

	/* Skip transition state */
	phy_read_1bit(db);

	/* read 16bit data */
	for (phy_data = 0, i = 0; i < 16; i++) {
		phy_data <<= 1;
		phy_data |= phy_read_1bit(db);
	}

	return phy_data;
}

static u16 phy_readby_cr10(struct uli526x_board_info *db, u8 phy_addr,
			   u8 offset)
{
	void __iomem *ioaddr = db->ioaddr;
	u32 cr10_value = phy_addr;

	cr10_value = (cr10_value <<  5) + offset;
	cr10_value = (cr10_value << 16) + 0x08000000;
	uw32(DCR10, cr10_value);
	udelay(1);
	while (1) {
		cr10_value = ur32(DCR10);
		if (cr10_value & 0x10000000)
			break;
	}
	return cr10_value & 0x0ffff;
}

static void phy_writeby_cr10(struct uli526x_board_info *db, u8 phy_addr,
			     u8 offset, u16 phy_data)
{
	void __iomem *ioaddr = db->ioaddr;
	u32 cr10_value = phy_addr;

	cr10_value = (cr10_value <<  5) + offset;
	cr10_value = (cr10_value << 16) + 0x04000000 + phy_data;
	uw32(DCR10, cr10_value);
	udelay(1);
}
/*
 *	Write one bit data to Phy Controller
 */

static void phy_write_1bit(struct uli526x_board_info *db, u32 data)
{
	void __iomem *ioaddr = db->ioaddr;

	uw32(DCR9, data);		/* MII Clock Low */
	udelay(1);
	uw32(DCR9, data | MDCLKH);	/* MII Clock High */
	udelay(1);
	uw32(DCR9, data);		/* MII Clock Low */
	udelay(1);
}


/*
 *	Read one bit phy data from PHY controller
 */

static u16 phy_read_1bit(struct uli526x_board_info *db)
{
	void __iomem *ioaddr = db->ioaddr;
	u16 phy_data;

	uw32(DCR9, 0x50000);
	udelay(1);
	phy_data = (ur32(DCR9) >> 19) & 0x1;
	uw32(DCR9, 0x40000);
	udelay(1);

	return phy_data;
}


static const struct pci_device_id uli526x_pci_tbl[] = {
	{ 0x10B9, 0x5261, PCI_ANY_ID, PCI_ANY_ID, 0, 0, PCI_ULI5261_ID },
	{ 0x10B9, 0x5263, PCI_ANY_ID, PCI_ANY_ID, 0, 0, PCI_ULI5263_ID },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, uli526x_pci_tbl);


static struct pci_driver uli526x_driver = {
	.name		= "uli526x",
	.id_table	= uli526x_pci_tbl,
	.probe		= uli526x_init_one,
	.remove		= uli526x_remove_one,
	.suspend	= uli526x_suspend,
	.resume		= uli526x_resume,
};

MODULE_AUTHOR("Peer Chen, peer.chen@uli.com.tw");
MODULE_DESCRIPTION("ULi M5261/M5263 fast ethernet driver");
MODULE_LICENSE("GPL");

module_param(debug, int, 0644);
module_param(mode, int, 0);
module_param(cr6set, int, 0);
MODULE_PARM_DESC(debug, "ULi M5261/M5263 enable debugging (0-1)");
MODULE_PARM_DESC(mode, "ULi M5261/M5263: Bit 0: 10/100Mbps, bit 2: duplex, bit 8: HomePNA");

/*	Description:
 *	when user used insmod to add module, system invoked init_module()
 *	to register the services.
 */

static int __init uli526x_init_module(void)
{

	pr_info("%s\n", version);
	printed_version = 1;

	ULI526X_DBUG(0, "init_module() ", debug);

	if (debug)
		uli526x_debug = debug;	/* set debug flag */
	if (cr6set)
		uli526x_cr6_user_set = cr6set;

 	switch (mode) {
   	case ULI526X_10MHF:
	case ULI526X_100MHF:
	case ULI526X_10MFD:
	case ULI526X_100MFD:
		uli526x_media_mode = mode;
		break;
	default:
		uli526x_media_mode = ULI526X_AUTO;
		break;
	}

	return pci_register_driver(&uli526x_driver);
}


/*
 *	Description:
 *	when user used rmmod to delete module, system invoked clean_module()
 *	to un-register all registered services.
 */

static void __exit uli526x_cleanup_module(void)
{
	ULI526X_DBUG(0, "uli526x_cleanup_module() ", debug);
	pci_unregister_driver(&uli526x_driver);
}

module_init(uli526x_init_module);
module_exit(uli526x_cleanup_module);

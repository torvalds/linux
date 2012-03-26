/*
    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2
    of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.


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
#include <asm/uaccess.h>


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

#define SROM_CLK_WRITE(data, ioaddr)					\
		outl(data|CR9_SROM_READ|CR9_SRCS,ioaddr);		\
		udelay(5);						\
		outl(data|CR9_SROM_READ|CR9_SRCS|CR9_SRCLK,ioaddr);	\
		udelay(5);						\
		outl(data|CR9_SROM_READ|CR9_SRCS,ioaddr);		\
		udelay(5);

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
	u32 chip_id;			/* Chip vendor/Device ID */
	struct net_device *next_dev;	/* next device */
	struct pci_dev *pdev;		/* PCI device */
	spinlock_t lock;

	long ioaddr;			/* I/O base address */
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
static int __devinitdata printed_version;
static const char version[] __devinitconst =
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
static u16 read_srom_word(long, int);
static irqreturn_t uli526x_interrupt(int, void *);
#ifdef CONFIG_NET_POLL_CONTROLLER
static void uli526x_poll(struct net_device *dev);
#endif
static void uli526x_descriptor_init(struct net_device *, unsigned long);
static void allocate_rx_buffer(struct net_device *);
static void update_cr6(u32, unsigned long);
static void send_filter_frame(struct net_device *, int);
static u16 phy_read(unsigned long, u8, u8, u32);
static u16 phy_readby_cr10(unsigned long, u8, u8);
static void phy_write(unsigned long, u8, u8, u16, u32);
static void phy_writeby_cr10(unsigned long, u8, u8, u16);
static void phy_write_1bit(unsigned long, u32, u32);
static u16 phy_read_1bit(unsigned long, u32);
static u8 uli526x_sense_speed(struct uli526x_board_info *);
static void uli526x_process_mode(struct uli526x_board_info *);
static void uli526x_timer(unsigned long);
static void uli526x_rx_packet(struct net_device *, struct uli526x_board_info *);
static void uli526x_free_tx_pkt(struct net_device *, struct uli526x_board_info *);
static void uli526x_reuse_skb(struct uli526x_board_info *, struct sk_buff *);
static void uli526x_dynamic_reset(struct net_device *);
static void uli526x_free_rxbuffer(struct uli526x_board_info *);
static void uli526x_init(struct net_device *);
static void uli526x_set_phyxcer(struct uli526x_board_info *);

/* ULI526X network board routine ---------------------------- */

static const struct net_device_ops netdev_ops = {
	.ndo_open		= uli526x_open,
	.ndo_stop		= uli526x_stop,
	.ndo_start_xmit		= uli526x_start_xmit,
	.ndo_set_rx_mode	= uli526x_set_filter_mode,
	.ndo_change_mtu		= eth_change_mtu,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller 	= uli526x_poll,
#endif
};

/*
 *	Search ULI526X board, allocate space and register it
 */

static int __devinit uli526x_init_one (struct pci_dev *pdev,
				    const struct pci_device_id *ent)
{
	struct uli526x_board_info *db;	/* board information structure */
	struct net_device *dev;
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

	if (pci_request_regions(pdev, DRV_NAME)) {
		pr_err("Failed to request PCI regions\n");
		err = -ENODEV;
		goto err_out_disable;
	}

	/* Init system & device */
	db = netdev_priv(dev);

	/* Allocate Tx/Rx descriptor memory */
	db->desc_pool_ptr = pci_alloc_consistent(pdev, sizeof(struct tx_desc) * DESC_ALL_CNT + 0x20, &db->desc_pool_dma_ptr);
	if(db->desc_pool_ptr == NULL)
	{
		err = -ENOMEM;
		goto err_out_nomem;
	}
	db->buf_pool_ptr = pci_alloc_consistent(pdev, TX_BUF_ALLOC * TX_DESC_CNT + 4, &db->buf_pool_dma_ptr);
	if(db->buf_pool_ptr == NULL)
	{
		err = -ENOMEM;
		goto err_out_nomem;
	}

	db->first_tx_desc = (struct tx_desc *) db->desc_pool_ptr;
	db->first_tx_desc_dma = db->desc_pool_dma_ptr;
	db->buf_pool_start = db->buf_pool_ptr;
	db->buf_pool_dma_start = db->buf_pool_dma_ptr;

	db->chip_id = ent->driver_data;
	db->ioaddr = pci_resource_start(pdev, 0);

	db->pdev = pdev;
	db->init = 1;

	dev->base_addr = db->ioaddr;
	dev->irq = pdev->irq;
	pci_set_drvdata(pdev, dev);

	/* Register some necessary functions */
	dev->netdev_ops = &netdev_ops;
	dev->ethtool_ops = &netdev_ethtool_ops;

	spin_lock_init(&db->lock);


	/* read 64 word srom data */
	for (i = 0; i < 64; i++)
		((__le16 *) db->srom)[i] = cpu_to_le16(read_srom_word(db->ioaddr, i));

	/* Set Node address */
	if(((u16 *) db->srom)[0] == 0xffff || ((u16 *) db->srom)[0] == 0)		/* SROM absent, so read MAC address from ID Table */
	{
		outl(0x10000, db->ioaddr + DCR0);	//Diagnosis mode
		outl(0x1c0, db->ioaddr + DCR13);	//Reset dianostic pointer port
		outl(0, db->ioaddr + DCR14);		//Clear reset port
		outl(0x10, db->ioaddr + DCR14);		//Reset ID Table pointer
		outl(0, db->ioaddr + DCR14);		//Clear reset port
		outl(0, db->ioaddr + DCR13);		//Clear CR13
		outl(0x1b0, db->ioaddr + DCR13);	//Select ID Table access port
		//Read MAC address from CR14
		for (i = 0; i < 6; i++)
			dev->dev_addr[i] = inl(db->ioaddr + DCR14);
		//Read end
		outl(0, db->ioaddr + DCR13);	//Clear CR13
		outl(0, db->ioaddr + DCR0);		//Clear CR0
		udelay(10);
	}
	else		/*Exist SROM*/
	{
		for (i = 0; i < 6; i++)
			dev->dev_addr[i] = db->srom[20 + i];
	}
	err = register_netdev (dev);
	if (err)
		goto err_out_res;

	netdev_info(dev, "ULi M%04lx at pci%s, %pM, irq %d\n",
		    ent->driver_data >> 16, pci_name(pdev),
		    dev->dev_addr, dev->irq);

	pci_set_master(pdev);

	return 0;

err_out_res:
	pci_release_regions(pdev);
err_out_nomem:
	if(db->desc_pool_ptr)
		pci_free_consistent(pdev, sizeof(struct tx_desc) * DESC_ALL_CNT + 0x20,
			db->desc_pool_ptr, db->desc_pool_dma_ptr);

	if(db->buf_pool_ptr != NULL)
		pci_free_consistent(pdev, TX_BUF_ALLOC * TX_DESC_CNT + 4,
			db->buf_pool_ptr, db->buf_pool_dma_ptr);
err_out_disable:
	pci_disable_device(pdev);
err_out_free:
	pci_set_drvdata(pdev, NULL);
	free_netdev(dev);

	return err;
}


static void __devexit uli526x_remove_one (struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct uli526x_board_info *db = netdev_priv(dev);

	ULI526X_DBUG(0, "uli526x_remove_one()", 0);

	pci_free_consistent(db->pdev, sizeof(struct tx_desc) *
				DESC_ALL_CNT + 0x20, db->desc_pool_ptr,
 				db->desc_pool_dma_ptr);
	pci_free_consistent(db->pdev, TX_BUF_ALLOC * TX_DESC_CNT + 4,
				db->buf_pool_ptr, db->buf_pool_dma_ptr);
	unregister_netdev(dev);
	pci_release_regions(pdev);
	free_netdev(dev);	/* free board information */
	pci_set_drvdata(pdev, NULL);
	pci_disable_device(pdev);
	ULI526X_DBUG(0, "uli526x_remove_one() exit", 0);
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

	ret = request_irq(dev->irq, uli526x_interrupt, IRQF_SHARED, dev->name, dev);
	if (ret)
		return ret;

	/* Active System Interface */
	netif_wake_queue(dev);

	/* set and active a timer process */
	init_timer(&db->timer);
	db->timer.expires = ULI526X_TIMER_WUT + HZ * 2;
	db->timer.data = (unsigned long)dev;
	db->timer.function = uli526x_timer;
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
	unsigned long ioaddr = db->ioaddr;
	u8	phy_tmp;
	u8	timeout;
	u16	phy_value;
	u16 phy_reg_reset;


	ULI526X_DBUG(0, "uli526x_init()", 0);

	/* Reset M526x MAC controller */
	outl(ULI526X_RESET, ioaddr + DCR0);	/* RESET MAC */
	udelay(100);
	outl(db->cr0_data, ioaddr + DCR0);
	udelay(5);

	/* Phy addr : In some boards,M5261/M5263 phy address != 1 */
	db->phy_addr = 1;
	for(phy_tmp=0;phy_tmp<32;phy_tmp++)
	{
		phy_value=phy_read(db->ioaddr,phy_tmp,3,db->chip_id);//peer add
		if(phy_value != 0xffff&&phy_value!=0)
		{
			db->phy_addr = phy_tmp;
			break;
		}
	}
	if(phy_tmp == 32)
		pr_warn("Can not find the phy address!!!\n");
	/* Parser SROM and media mode */
	db->media_mode = uli526x_media_mode;

	/* phyxcer capability setting */
	phy_reg_reset = phy_read(db->ioaddr, db->phy_addr, 0, db->chip_id);
	phy_reg_reset = (phy_reg_reset | 0x8000);
	phy_write(db->ioaddr, db->phy_addr, 0, phy_reg_reset, db->chip_id);

	/* See IEEE 802.3-2002.pdf (Section 2, Chapter "22.2.4 Management
	 * functions") or phy data sheet for details on phy reset
	 */
	udelay(500);
	timeout = 10;
	while (timeout-- &&
		phy_read(db->ioaddr, db->phy_addr, 0, db->chip_id) & 0x8000)
			udelay(100);

	/* Process Phyxcer Media Mode */
	uli526x_set_phyxcer(db);

	/* Media Mode Process */
	if ( !(db->media_mode & ULI526X_AUTO) )
		db->op_mode = db->media_mode; 	/* Force Mode */

	/* Initialize Transmit/Receive decriptor and CR3/4 */
	uli526x_descriptor_init(dev, ioaddr);

	/* Init CR6 to program M526X operation */
	update_cr6(db->cr6_data, ioaddr);

	/* Send setup frame */
	send_filter_frame(dev, netdev_mc_count(dev));	/* M5261/M5263 */

	/* Init CR7, interrupt active bit */
	db->cr7_data = CR7_DEFAULT;
	outl(db->cr7_data, ioaddr + DCR7);

	/* Init CR15, Tx jabber and Rx watchdog timer */
	outl(db->cr15_data, ioaddr + DCR15);

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
	struct tx_desc *txptr;
	unsigned long flags;

	ULI526X_DBUG(0, "uli526x_start_xmit", 0);

	/* Resource flag check */
	netif_stop_queue(dev);

	/* Too large packet check */
	if (skb->len > MAX_PACKET_SIZE) {
		netdev_err(dev, "big packet = %d\n", (u16)skb->len);
		dev_kfree_skb(skb);
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
	outl(0, dev->base_addr + DCR7);

	/* transmit this packet */
	txptr = db->tx_insert_ptr;
	skb_copy_from_linear_data(skb, txptr->tx_buf_ptr, skb->len);
	txptr->tdes1 = cpu_to_le32(0xe1000000 | skb->len);

	/* Point to next transmit free descriptor */
	db->tx_insert_ptr = txptr->next_tx_desc;

	/* Transmit Packet Process */
	if ( (db->tx_packet_cnt < TX_DESC_CNT) ) {
		txptr->tdes0 = cpu_to_le32(0x80000000);	/* Set owner bit */
		db->tx_packet_cnt++;			/* Ready to send */
		outl(0x1, dev->base_addr + DCR1);	/* Issue Tx polling */
		dev->trans_start = jiffies;		/* saved time stamp */
	}

	/* Tx resource check */
	if ( db->tx_packet_cnt < TX_FREE_DESC_CNT )
		netif_wake_queue(dev);

	/* Restore CR7 to enable interrupt */
	spin_unlock_irqrestore(&db->lock, flags);
	outl(db->cr7_data, dev->base_addr + DCR7);

	/* free this SKB */
	dev_kfree_skb(skb);

	return NETDEV_TX_OK;
}


/*
 *	Stop the interface.
 *	The interface is stopped when it is brought.
 */

static int uli526x_stop(struct net_device *dev)
{
	struct uli526x_board_info *db = netdev_priv(dev);
	unsigned long ioaddr = dev->base_addr;

	ULI526X_DBUG(0, "uli526x_stop", 0);

	/* disable system */
	netif_stop_queue(dev);

	/* deleted timer */
	del_timer_sync(&db->timer);

	/* Reset & stop ULI526X board */
	outl(ULI526X_RESET, ioaddr + DCR0);
	udelay(5);
	phy_write(db->ioaddr, db->phy_addr, 0, 0x8000, db->chip_id);

	/* free interrupt */
	free_irq(dev->irq, dev);

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
	unsigned long ioaddr = dev->base_addr;
	unsigned long flags;

	spin_lock_irqsave(&db->lock, flags);
	outl(0, ioaddr + DCR7);

	/* Got ULI526X status */
	db->cr5_data = inl(ioaddr + DCR5);
	outl(db->cr5_data, ioaddr + DCR5);
	if ( !(db->cr5_data & 0x180c1) ) {
		/* Restore CR7 to enable interrupt mask */
		outl(db->cr7_data, ioaddr + DCR7);
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
	outl(db->cr7_data, ioaddr + DCR7);

	spin_unlock_irqrestore(&db->lock, flags);
	return IRQ_HANDLED;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void uli526x_poll(struct net_device *dev)
{
	/* ISR grabs the irqsave lock, so this should be safe */
	uli526x_interrupt(dev->irq, dev);
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
					memcpy(skb_put(skb, rxlen),
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
ULi_ethtool_gset(struct uli526x_board_info *db, struct ethtool_cmd *ecmd)
{
	ecmd->supported = (SUPPORTED_10baseT_Half |
	                   SUPPORTED_10baseT_Full |
	                   SUPPORTED_100baseT_Half |
	                   SUPPORTED_100baseT_Full |
	                   SUPPORTED_Autoneg |
	                   SUPPORTED_MII);

	ecmd->advertising = (ADVERTISED_10baseT_Half |
	                   ADVERTISED_10baseT_Full |
	                   ADVERTISED_100baseT_Half |
	                   ADVERTISED_100baseT_Full |
	                   ADVERTISED_Autoneg |
	                   ADVERTISED_MII);


	ecmd->port = PORT_MII;
	ecmd->phy_address = db->phy_addr;

	ecmd->transceiver = XCVR_EXTERNAL;

	ethtool_cmd_speed_set(ecmd, SPEED_10);
	ecmd->duplex = DUPLEX_HALF;

	if(db->op_mode==ULI526X_100MHF || db->op_mode==ULI526X_100MFD)
	{
		ethtool_cmd_speed_set(ecmd, SPEED_100);
	}
	if(db->op_mode==ULI526X_10MFD || db->op_mode==ULI526X_100MFD)
	{
		ecmd->duplex = DUPLEX_FULL;
	}
	if(db->link_failed)
	{
		ethtool_cmd_speed_set(ecmd, -1);
		ecmd->duplex = -1;
	}

	if (db->media_mode & ULI526X_AUTO)
	{
		ecmd->autoneg = AUTONEG_ENABLE;
	}
}

static void netdev_get_drvinfo(struct net_device *dev,
			       struct ethtool_drvinfo *info)
{
	struct uli526x_board_info *np = netdev_priv(dev);

	strlcpy(info->driver, DRV_NAME, sizeof(info->driver));
	strlcpy(info->version, DRV_VERSION, sizeof(info->version));
	if (np->pdev)
		strlcpy(info->bus_info, pci_name(np->pdev),
			sizeof(info->bus_info));
	else
		sprintf(info->bus_info, "EISA 0x%lx %d",
			dev->base_addr, dev->irq);
}

static int netdev_get_settings(struct net_device *dev, struct ethtool_cmd *cmd) {
	struct uli526x_board_info *np = netdev_priv(dev);

	ULi_ethtool_gset(np, cmd);

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
	.get_settings		= netdev_get_settings,
	.get_link		= netdev_get_link,
	.get_wol		= uli526x_get_wol,
};

/*
 *	A periodic timer routine
 *	Dynamic media sense, allocate Rx buffer...
 */

static void uli526x_timer(unsigned long data)
{
	u32 tmp_cr8;
	unsigned char tmp_cr12=0;
	struct net_device *dev = (struct net_device *) data;
	struct uli526x_board_info *db = netdev_priv(dev);
 	unsigned long flags;

	//ULI526X_DBUG(0, "uli526x_timer()", 0);
	spin_lock_irqsave(&db->lock, flags);


	/* Dynamic reset ULI526X : system error or transmit time-out */
	tmp_cr8 = inl(db->ioaddr + DCR8);
	if ( (db->interval_rx_cnt==0) && (tmp_cr8) ) {
		db->reset_cr8++;
		db->wait_reset = 1;
	}
	db->interval_rx_cnt = 0;

	/* TX polling kick monitor */
	if ( db->tx_packet_cnt &&
	     time_after(jiffies, dev_trans_start(dev) + ULI526X_TX_KICK) ) {
		outl(0x1, dev->base_addr + DCR1);   // Tx polling again

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
	if((phy_read(db->ioaddr, db->phy_addr, 5, db->chip_id) & 0x01e0)!=0)
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
			phy_write(db->ioaddr, db->phy_addr, 0, 0x1000, db->chip_id);

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
		db->init=0;

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

	/* Sopt MAC controller */
	db->cr6_data &= ~(CR6_RXSC | CR6_TXSC);	/* Disable Tx/Rx */
	update_cr6(db->cr6_data, dev->base_addr);
	outl(0, dev->base_addr + DCR7);		/* Disable Interrupt */
	outl(inl(dev->base_addr + DCR5), dev->base_addr + DCR5);

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

	if (!netdev_priv(dev))
		return 0;

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

	if (!netdev_priv(dev))
		return 0;

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

static void uli526x_descriptor_init(struct net_device *dev, unsigned long ioaddr)
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
	outl(db->first_tx_desc_dma, ioaddr + DCR4);     /* TX DESC address */

	/* rx descriptor start pointer */
	db->first_rx_desc = (void *)db->first_tx_desc + sizeof(struct tx_desc) * TX_DESC_CNT;
	db->first_rx_desc_dma =  db->first_tx_desc_dma + sizeof(struct tx_desc) * TX_DESC_CNT;
	db->rx_insert_ptr = db->first_rx_desc;
	db->rx_ready_ptr = db->first_rx_desc;
	outl(db->first_rx_desc_dma, ioaddr + DCR3);	/* RX DESC address */

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

static void update_cr6(u32 cr6_data, unsigned long ioaddr)
{

	outl(cr6_data, ioaddr + DCR6);
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
		update_cr6(db->cr6_data | 0x2000, dev->base_addr);
		outl(0x1, dev->base_addr + DCR1);	/* Issue Tx polling */
		update_cr6(db->cr6_data, dev->base_addr);
		dev->trans_start = jiffies;
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

static u16 read_srom_word(long ioaddr, int offset)
{
	int i;
	u16 srom_data = 0;
	long cr9_ioaddr = ioaddr + DCR9;

	outl(CR9_SROM_READ, cr9_ioaddr);
	outl(CR9_SROM_READ | CR9_SRCS, cr9_ioaddr);

	/* Send the Read Command 110b */
	SROM_CLK_WRITE(SROM_DATA_1, cr9_ioaddr);
	SROM_CLK_WRITE(SROM_DATA_1, cr9_ioaddr);
	SROM_CLK_WRITE(SROM_DATA_0, cr9_ioaddr);

	/* Send the offset */
	for (i = 5; i >= 0; i--) {
		srom_data = (offset & (1 << i)) ? SROM_DATA_1 : SROM_DATA_0;
		SROM_CLK_WRITE(srom_data, cr9_ioaddr);
	}

	outl(CR9_SROM_READ | CR9_SRCS, cr9_ioaddr);

	for (i = 16; i > 0; i--) {
		outl(CR9_SROM_READ | CR9_SRCS | CR9_SRCLK, cr9_ioaddr);
		udelay(5);
		srom_data = (srom_data << 1) | ((inl(cr9_ioaddr) & CR9_CRDOUT) ? 1 : 0);
		outl(CR9_SROM_READ | CR9_SRCS, cr9_ioaddr);
		udelay(5);
	}

	outl(CR9_SROM_READ, cr9_ioaddr);
	return srom_data;
}


/*
 *	Auto sense the media mode
 */

static u8 uli526x_sense_speed(struct uli526x_board_info * db)
{
	u8 ErrFlag = 0;
	u16 phy_mode;

	phy_mode = phy_read(db->ioaddr, db->phy_addr, 1, db->chip_id);
	phy_mode = phy_read(db->ioaddr, db->phy_addr, 1, db->chip_id);

	if ( (phy_mode & 0x24) == 0x24 ) {

		phy_mode = ((phy_read(db->ioaddr, db->phy_addr, 5, db->chip_id) & 0x01e0)<<7);
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
	u16 phy_reg;

	/* Phyxcer capability setting */
	phy_reg = phy_read(db->ioaddr, db->phy_addr, 4, db->chip_id) & ~0x01e0;

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
	phy_write(db->ioaddr, db->phy_addr, 4, phy_reg, db->chip_id);

 	/* Restart Auto-Negotiation */
	phy_write(db->ioaddr, db->phy_addr, 0, 0x1200, db->chip_id);
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
	u16 phy_reg;

	/* Full Duplex Mode Check */
	if (db->op_mode & 0x4)
		db->cr6_data |= CR6_FDM;	/* Set Full Duplex Bit */
	else
		db->cr6_data &= ~CR6_FDM;	/* Clear Full Duplex Bit */

	update_cr6(db->cr6_data, db->ioaddr);

	/* 10/100M phyxcer force mode need */
	if ( !(db->media_mode & 0x8)) {
		/* Forece Mode */
		phy_reg = phy_read(db->ioaddr, db->phy_addr, 6, db->chip_id);
		if ( !(phy_reg & 0x1) ) {
			/* parter without N-Way capability */
			phy_reg = 0x0;
			switch(db->op_mode) {
			case ULI526X_10MHF: phy_reg = 0x0; break;
			case ULI526X_10MFD: phy_reg = 0x100; break;
			case ULI526X_100MHF: phy_reg = 0x2000; break;
			case ULI526X_100MFD: phy_reg = 0x2100; break;
			}
			phy_write(db->ioaddr, db->phy_addr, 0, phy_reg, db->chip_id);
		}
	}
}


/*
 *	Write a word to Phy register
 */

static void phy_write(unsigned long iobase, u8 phy_addr, u8 offset, u16 phy_data, u32 chip_id)
{
	u16 i;
	unsigned long ioaddr;

	if(chip_id == PCI_ULI5263_ID)
	{
		phy_writeby_cr10(iobase, phy_addr, offset, phy_data);
		return;
	}
	/* M5261/M5263 Chip */
	ioaddr = iobase + DCR9;

	/* Send 33 synchronization clock to Phy controller */
	for (i = 0; i < 35; i++)
		phy_write_1bit(ioaddr, PHY_DATA_1, chip_id);

	/* Send start command(01) to Phy */
	phy_write_1bit(ioaddr, PHY_DATA_0, chip_id);
	phy_write_1bit(ioaddr, PHY_DATA_1, chip_id);

	/* Send write command(01) to Phy */
	phy_write_1bit(ioaddr, PHY_DATA_0, chip_id);
	phy_write_1bit(ioaddr, PHY_DATA_1, chip_id);

	/* Send Phy address */
	for (i = 0x10; i > 0; i = i >> 1)
		phy_write_1bit(ioaddr, phy_addr & i ? PHY_DATA_1 : PHY_DATA_0, chip_id);

	/* Send register address */
	for (i = 0x10; i > 0; i = i >> 1)
		phy_write_1bit(ioaddr, offset & i ? PHY_DATA_1 : PHY_DATA_0, chip_id);

	/* written trasnition */
	phy_write_1bit(ioaddr, PHY_DATA_1, chip_id);
	phy_write_1bit(ioaddr, PHY_DATA_0, chip_id);

	/* Write a word data to PHY controller */
	for ( i = 0x8000; i > 0; i >>= 1)
		phy_write_1bit(ioaddr, phy_data & i ? PHY_DATA_1 : PHY_DATA_0, chip_id);

}


/*
 *	Read a word data from phy register
 */

static u16 phy_read(unsigned long iobase, u8 phy_addr, u8 offset, u32 chip_id)
{
	int i;
	u16 phy_data;
	unsigned long ioaddr;

	if(chip_id == PCI_ULI5263_ID)
		return phy_readby_cr10(iobase, phy_addr, offset);
	/* M5261/M5263 Chip */
	ioaddr = iobase + DCR9;

	/* Send 33 synchronization clock to Phy controller */
	for (i = 0; i < 35; i++)
		phy_write_1bit(ioaddr, PHY_DATA_1, chip_id);

	/* Send start command(01) to Phy */
	phy_write_1bit(ioaddr, PHY_DATA_0, chip_id);
	phy_write_1bit(ioaddr, PHY_DATA_1, chip_id);

	/* Send read command(10) to Phy */
	phy_write_1bit(ioaddr, PHY_DATA_1, chip_id);
	phy_write_1bit(ioaddr, PHY_DATA_0, chip_id);

	/* Send Phy address */
	for (i = 0x10; i > 0; i = i >> 1)
		phy_write_1bit(ioaddr, phy_addr & i ? PHY_DATA_1 : PHY_DATA_0, chip_id);

	/* Send register address */
	for (i = 0x10; i > 0; i = i >> 1)
		phy_write_1bit(ioaddr, offset & i ? PHY_DATA_1 : PHY_DATA_0, chip_id);

	/* Skip transition state */
	phy_read_1bit(ioaddr, chip_id);

	/* read 16bit data */
	for (phy_data = 0, i = 0; i < 16; i++) {
		phy_data <<= 1;
		phy_data |= phy_read_1bit(ioaddr, chip_id);
	}

	return phy_data;
}

static u16 phy_readby_cr10(unsigned long iobase, u8 phy_addr, u8 offset)
{
	unsigned long ioaddr,cr10_value;

	ioaddr = iobase + DCR10;
	cr10_value = phy_addr;
	cr10_value = (cr10_value<<5) + offset;
	cr10_value = (cr10_value<<16) + 0x08000000;
	outl(cr10_value,ioaddr);
	udelay(1);
	while(1)
	{
		cr10_value = inl(ioaddr);
		if(cr10_value&0x10000000)
			break;
	}
	return cr10_value & 0x0ffff;
}

static void phy_writeby_cr10(unsigned long iobase, u8 phy_addr, u8 offset, u16 phy_data)
{
	unsigned long ioaddr,cr10_value;

	ioaddr = iobase + DCR10;
	cr10_value = phy_addr;
	cr10_value = (cr10_value<<5) + offset;
	cr10_value = (cr10_value<<16) + 0x04000000 + phy_data;
	outl(cr10_value,ioaddr);
	udelay(1);
}
/*
 *	Write one bit data to Phy Controller
 */

static void phy_write_1bit(unsigned long ioaddr, u32 phy_data, u32 chip_id)
{
	outl(phy_data , ioaddr);			/* MII Clock Low */
	udelay(1);
	outl(phy_data  | MDCLKH, ioaddr);	/* MII Clock High */
	udelay(1);
	outl(phy_data , ioaddr);			/* MII Clock Low */
	udelay(1);
}


/*
 *	Read one bit phy data from PHY controller
 */

static u16 phy_read_1bit(unsigned long ioaddr, u32 chip_id)
{
	u16 phy_data;

	outl(0x50000 , ioaddr);
	udelay(1);
	phy_data = ( inl(ioaddr) >> 19 ) & 0x1;
	outl(0x40000 , ioaddr);
	udelay(1);

	return phy_data;
}


static DEFINE_PCI_DEVICE_TABLE(uli526x_pci_tbl) = {
	{ 0x10B9, 0x5261, PCI_ANY_ID, PCI_ANY_ID, 0, 0, PCI_ULI5261_ID },
	{ 0x10B9, 0x5263, PCI_ANY_ID, PCI_ANY_ID, 0, 0, PCI_ULI5263_ID },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, uli526x_pci_tbl);


static struct pci_driver uli526x_driver = {
	.name		= "uli526x",
	.id_table	= uli526x_pci_tbl,
	.probe		= uli526x_init_one,
	.remove		= __devexit_p(uli526x_remove_one),
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
	ULI526X_DBUG(0, "uli526x_clean_module() ", debug);
	pci_unregister_driver(&uli526x_driver);
}

module_init(uli526x_init_module);
module_exit(uli526x_cleanup_module);

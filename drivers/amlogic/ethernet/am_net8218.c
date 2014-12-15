/*
 * Amlogic Ethernet Driver
 * h
 * Copyright (C) 2012 Amlogic, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the named License,
 * or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA
 *
 * Author: Platform-BJ@amlogic.com
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/io.h>
#include <linux/mii.h>
#include <linux/sched.h>
#include <linux/crc32.h>
#include <linux/platform_device.h>
#include <linux/of.h>

#include <plat/eth.h>
#include <plat/regops.h>
#include <mach/am_regs.h>
#include <mach/pinmux.h>
#include <mach/gpio.h>
#include <asm/delay.h>
#include <linux/delay.h>
#include <linux/pinctrl/consumer.h>
#include <linux/amlogic/aml_gpio_consumer.h>
#include <linux/of_platform.h>
#include <linux/kthread.h>
#include "am_net8218.h"
#include <mach/mod_gate.h>

#define MODULE_NAME "ethernet"
#define DRIVER_NAME "ethernet"

#define DRV_NAME	DRIVER_NAME
#define DRV_VERSION	"v2.0.0"

#undef CONFIG_HAS_EARLYSUSPEND
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
static struct early_suspend early_suspend;
#endif

MODULE_DESCRIPTION("Amlogic Ethernet Driver");
MODULE_AUTHOR("Platform-BJ@amlogic.com>");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

// >0 basic init and remove info;
// >1 further setup info;
// >2 rx data dump
// >3 tx data dump
#ifdef CONFIG_AM_ETHERNET_DEBUG_LEVEL
static int g_debug = CONFIG_AM_ETHERNET_DEBUG_LEVEL;
#else
static int g_debug = 1;
#endif
static unsigned int g_tx_cnt = 0;
static unsigned int g_rx_cnt = 0;
static int g_mdcclk = 2;
static int g_rxnum = 64;
static int g_txnum = 64;
static int new_maclogic = 0;
static unsigned int ethbaseaddr = ETHBASE;
static unsigned int savepowermode = 0;
static int interruptnum = ETH_INTERRUPT;
static int phy_interface = 1;
static int reset_delay = 0;
static int reset_pin_num = 0;
static int reset_pin_enable = 0;
static const char *reset_pin;
static unsigned int MDCCLK = ETH_MAC_4_GMII_Addr_CR_100_150;

module_param_named(amlog_level, g_debug, int, 0664);
MODULE_PARM_DESC(amlog_level, "ethernet debug level\n");

#include "am_mdio.c"

//#define LOOP_BACK_TEST
//#define MAC_LOOPBACK_TEST
//#define PHY_LOOPBACK_TEST

static int running = 0;
static struct net_device *my_ndev = NULL;
static struct aml_eth_platdata *eth_pdata = NULL;
static unsigned int g_ethernet_registered = 0;
static unsigned int g_mac_addr_setup = 0;
static unsigned int g_mac_pmt_enable = 0;
static char DEFMAC[] = "\x00\x01\x23\xcd\xee\xaf";

#define PERIPHS_SET_BITS(reg, mask)	\
	aml_set_reg32_mask(reg, mask)
#define PERIPHS_CLEAR_BITS(reg, mask)	\
	aml_clr_reg32_mask(reg, mask)

void start_test(struct net_device *dev);
static void write_mac_addr(struct net_device *dev, char *macaddr);
static int ethernet_reset(struct net_device *dev);
static int reset_mac(struct net_device *dev);
static void am_net_dump_macreg(void);
static void read_macreg(void);

/* --------------------------------------------------------------------------*/
/**
 * @brief  data_dump
 *
 * @param  p
 * @param  len
 */
/* --------------------------------------------------------------------------*/
static void data_dump(unsigned char *p, int len)
{
	int i, j;
	char s[20];
	for (i = 0; i < len; i += 16) {
		printk("%08x:", (unsigned int)p);
		for (j = 0; j < 16 && j < len - 0 * 16; j++) {
			s[j] = (p[j] > 15 && p[j] < 128) ? p[j] : '.';
			printk(" %02x", p[j]);
		}
		s[j] = 0;
		printk(" |%s|\n", s);
		p = p + 16;
	}
}

/* --------------------------------------------------------------------------*/
/**
 * @brief  tx_data_dump
 *
 * @param  p
 * @param  len
 */
/* --------------------------------------------------------------------------*/
static void tx_data_dump(unsigned char *p, int len)
{
	if ((g_debug == 3) || (g_debug == 5)) {
		printk("---------->\n");
		data_dump(p, len);
	}
	g_tx_cnt++;

	return;
}

/* --------------------------------------------------------------------------*/
/**
 * @brief  rx_data_dump
 *
 * @param  p
 * @param  len
 */
/* --------------------------------------------------------------------------*/
static void rx_data_dump(unsigned char *p, int len)
{
	if ((g_debug == 4) || (g_debug == 5)) {
		printk("<----------\n");
		data_dump(p, len);
	}
	g_rx_cnt++;

	return;
}

/* --------------------------------------------------------------------------*/
/**
 * @brief  netdev_ioctl
 *
 * @param  dev
 * @param  rq
 * @param  cmd
 *
 * @return
 */
/* --------------------------------------------------------------------------*/
static int netdev_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct am_net_private *priv = netdev_priv(dev);
        int ret;

        if (!netif_running(dev))
                return -EINVAL;

        if (!priv->phydev)
                return -EINVAL;

        spin_lock(&priv->lock);
        ret = phy_mii_ioctl(priv->phydev, rq, cmd);
        spin_unlock(&priv->lock);

        return ret;
}

/* --------------------------------------------------------------------------*/
/**
 * @brief  init_rxtx_rings
 *
 * @param  dev
 *
 * @return
 */
/* --------------------------------------------------------------------------*/
int init_rxtx_rings(struct net_device *dev)
{
	struct am_net_private *np = netdev_priv(dev);
	int i;
#ifndef DMA_USE_SKB_BUF
	unsigned long tx = 0, rx = 0;
#endif
#ifdef DMA_USE_MALLOC_ADDR
	rx = (unsigned long)kmalloc((RX_RING_SIZE) * np->rx_buf_sz, GFP_KERNEL | GFP_DMA);
	if (rx == 0) {
		printk("error to alloc Rx  ring buf\n");
		return -1;
	}
	tx = (unsigned long)kmalloc((TX_RING_SIZE) * np->rx_buf_sz, GFP_KERNEL | GFP_DMA);
	if (tx == 0) {
		kfree((void *)rx);
		printk("error to alloc Tx  ring buf\n");
		return -1;
	}
#elif defined(DMA_USE_SKB_BUF)
	//not needed
#else
	tx = TX_BUF_ADDR;
	rx = RX_BUF_ADDR;
#endif

	/* Fill in the Rx buffers.  Handle allocation failure gracefully. */
	for (i = 0; i < RX_RING_SIZE; i++) {
#ifdef DMA_USE_SKB_BUF
		struct sk_buff *skb = dev_alloc_skb(np->rx_buf_sz);
		np->rx_ring[i].skb = skb;
		if (skb == NULL) {
			break;
		}
		skb_reserve(skb, 2);	/* 16 byte alignd for ip */
		skb->dev = dev;	/* Mark as being used by this device. */
		np->rx_ring[i].buf = (unsigned long)skb->data;
#else
		np->rx_ring[i].skb = NULL;
		np->rx_ring[i].buf = (rx + i * np->rx_buf_sz);	//(unsigned long )skb->data;
#endif
		np->rx_ring[i].buf_dma = dma_map_single(&dev->dev, (void *)np->rx_ring[i].buf, np->rx_buf_sz, DMA_FROM_DEVICE);
		np->rx_ring[i].count = (DescChain) | (np->rx_buf_sz & DescSize1Mask);
		np->rx_ring[i].status = (DescOwnByDma);
		np->rx_ring[i].next_dma = &np->rx_ring_dma[i + 1];
		np->rx_ring[i].next = &np->rx_ring[i + 1];

	}

	np->rx_ring[RX_RING_SIZE - 1].next_dma = &np->rx_ring_dma[0];
	np->rx_ring[RX_RING_SIZE - 1].next = &np->rx_ring[0];
	/* Initialize the Tx descriptors */
	for (i = 0; i < TX_RING_SIZE; i++) {
#ifdef DMA_USE_SKB_BUF
		np->tx_ring[i].buf = 0;
#else
		np->tx_ring[i].buf = (tx + i * np->rx_buf_sz);
#endif
		np->tx_ring[i].status = 0;
		np->tx_ring[i].count =
		    (DescChain) | (np->rx_buf_sz & DescSize1Mask);
		np->tx_ring[i].next_dma = &np->tx_ring_dma[i + 1];
		np->tx_ring[i].next = &np->tx_ring[i + 1];
		np->tx_ring[i].skb = NULL;
	}
	np->tx_ring[TX_RING_SIZE - 1].next_dma = &np->tx_ring_dma[0];
	np->tx_ring[TX_RING_SIZE - 1].next = &np->tx_ring[0];
	np->start_tx = &np->tx_ring[0];
	np->last_tx = NULL;
	np->last_rx = &np->rx_ring[RX_RING_SIZE - 1];
	CACHE_WSYNC(np->tx_ring, sizeof(struct _tx_desc)*TX_RING_SIZE);
	CACHE_WSYNC(np->rx_ring, sizeof(struct _rx_desc)*RX_RING_SIZE);


	return 0;
}

/* --------------------------------------------------------------------------*/
/**
 * @brief  alloc_ringdesc
 *
 * @param  dev
 *
 * @return
 */
/* --------------------------------------------------------------------------*/
static int alloc_ringdesc(struct net_device *dev)
{
	struct am_net_private *np = netdev_priv(dev);

	np->rx_buf_sz = (dev->mtu <= 1500 ? PKT_BUF_SZ : dev->mtu + 32);
#ifdef USE_COHERENT_MEMORY
	np->rx_ring = dma_alloc_coherent(&dev->dev,
	                                 sizeof(struct _rx_desc) * RX_RING_SIZE,
	                                 (dma_addr_t *)&np->rx_ring_dma, GFP_KERNEL);
#else
	np->rx_ring = kmalloc(sizeof(struct _rx_desc) * RX_RING_SIZE, GFP_KERNEL | GFP_DMA);
	np->rx_ring_dma = (void*)virt_to_phys(np->rx_ring);
#endif
	if (!np->rx_ring) {
		return -ENOMEM;
	}

	if (!IS_CACHE_ALIGNED(np->rx_ring)) {
		printk("Error the alloc mem is not cache aligned(%p)\n", np->rx_ring);
	}
	printk("NET MDA descpter start addr=%p\n", np->rx_ring);
#ifdef USE_COHERENT_MEMORY
	np->tx_ring = dma_alloc_coherent(&dev->dev,
	                                 sizeof(struct _tx_desc) * TX_RING_SIZE ,
	                                 (dma_addr_t *)&np->tx_ring_dma, GFP_KERNEL);
#else
	np->tx_ring = kmalloc(sizeof(struct _tx_desc) * TX_RING_SIZE, GFP_KERNEL | GFP_DMA);
	np->tx_ring_dma = (void*)virt_to_phys(np->tx_ring);
#endif
	if (init_rxtx_rings(dev)) {
		printk("init rx tx ring failed!!\n");
		return -1;
	}

	return 0;
}

/* --------------------------------------------------------------------------*/
/**
 * @brief  free_ringdesc
 *
 * @param  dev
 *
 * @return
 */
/* --------------------------------------------------------------------------*/
static int free_ringdesc(struct net_device *dev)
{
	struct am_net_private *np = netdev_priv(dev);
	int i;
	for (i = 0; i < RX_RING_SIZE; i++) {
		if (np->rx_ring[i].skb) {
			if (np->rx_ring[i].buf_dma != 0) {
				dma_unmap_single(&dev->dev, np->rx_ring[i].buf_dma, np->rx_buf_sz, DMA_FROM_DEVICE);
			}
			dev_kfree_skb_any(np->rx_ring[i].skb);
		}
		np->rx_ring[i].skb = NULL;
	}
	for (i = 0; i < TX_RING_SIZE; i++) {
		if (np->tx_ring[i].skb != NULL) {
			if (np->tx_ring[i].buf_dma != 0) {
				dma_unmap_single(&dev->dev, np->tx_ring[i].buf_dma, np->rx_buf_sz, DMA_TO_DEVICE);
			}
			dev_kfree_skb_any(np->tx_ring[i].skb);
		}
		np->tx_ring[i].skb = NULL;
	}
	if (np->rx_ring) {
#ifdef USE_COHERENT_MEMORY
		dma_free_coherent(&dev->dev,
		                  sizeof(struct _rx_desc) * RX_RING_SIZE ,
		                  np->rx_ring, (dma_addr_t)np->rx_ring_dma);	// for apollo
#else
		kfree(np->rx_ring);
#endif
	}

	np->rx_ring = NULL;
	if (np->tx_ring) {
#ifdef USE_COHERENT_MEMORY
		dma_free_coherent(&dev->dev,
		                  sizeof(struct _tx_desc) * TX_RING_SIZE ,
		                  np->tx_ring, (dma_addr_t)np->tx_ring_dma);	// for apollo
#else
		kfree(np->tx_ring);
#endif
	}
	np->tx_ring = NULL;
	return 0;
}

/* --------------------------------------------------------------------------*/
/**
 * @brief  update_status
 *
 * @param  dev
 * @param  status
 * @param  mask
 *
 * @return
 */
/* --------------------------------------------------------------------------*/
static inline int update_status(struct net_device *dev, unsigned long status,
                                unsigned long mask)
{
	struct am_net_private *np = netdev_priv(dev);
	int need_reset = 0;
	int need_rx_restart = 0;
	int res = 0;
	if(status & GMAC_MMC_Interrupt){
			printk("ETH_MMC_ipc_intr_rx = %x\n",readl((void*)(np->base_addr + ETH_MMC_ipc_intr_rx)));
			printk("ETH_MMC_intr_rx = %x\n",readl((void*)(np->base_addr + ETH_MMC_intr_rx)));
	}

	if (status & NOR_INTR_EN) {	//Normal Interrupts Process
		if (status & TX_INTR_EN) {	//Transmit Interrupt Process
			writel(1,(void*)(np->base_addr + ETH_DMA_1_Tr_Poll_Demand));
			netif_wake_queue(dev);
			writel((1 << 0 | 1 << 16),(void*)(np->base_addr + ETH_DMA_5_Status));
			res |= 1;
		}
		if (status & RX_INTR_EN) {	//Receive Interrupt Process
			writel((1 << 6 | 1 << 16), (void*)(np->base_addr + ETH_DMA_5_Status));
			res |= 2;
		}
		if (status & EARLY_RX_INTR_EN) {
			writel((EARLY_RX_INTR_EN | NOR_INTR_EN),(void*) (np->base_addr + ETH_DMA_5_Status));
		}
		if (status & TX_BUF_UN_EN) {
			writel((1 << 2 | 1 << 16), (void*)(np->base_addr + ETH_DMA_5_Status));
			res |= 1;
			//this error will cleard in start tx...
			if (g_debug > 1) {
				printk(KERN_WARNING "[" DRV_NAME "]" "Tx bufer unenable\n");
			}
		}
	} else if (status & ANOR_INTR_EN) {	//Abnormal Interrupts Process
		if (status & RX_BUF_UN) {
			writel((RX_BUF_UN | ANOR_INTR_EN),(void*) (np->base_addr + ETH_DMA_5_Status));
			np->stats.rx_over_errors++;
			need_rx_restart++;
			res |= 2;
			//printk(KERN_WARNING DRV_NAME "Receive Buffer Unavailable\n");
			if (g_debug > 1) {
				printk(KERN_WARNING "[" DRV_NAME "]" "Rx bufer unenable\n");
			}
		}
		if (status & RX_STOP_EN) {
			writel((RX_STOP_EN | ANOR_INTR_EN),
			           (void*)(np->base_addr + ETH_DMA_5_Status));
			need_rx_restart++;
			res |= 2;
		}
		if (status & RX_WATCH_TIMEOUT) {
			writel((RX_WATCH_TIMEOUT | ANOR_INTR_EN),
			          (void*)( np->base_addr + ETH_DMA_5_Status));
			need_rx_restart++;
		}
		if (status & FATAL_BUS_ERROR) {
			writel((FATAL_BUS_ERROR | ANOR_INTR_EN),
			           (void*)(np->base_addr + ETH_DMA_5_Status));
			need_reset++;
			printk(KERN_WARNING "[" DRV_NAME "]" "fatal bus error\n");
		}
		if (status & EARLY_TX_INTR_EN) {
			writel((EARLY_TX_INTR_EN | ANOR_INTR_EN),
			          (void*) (np->base_addr + ETH_DMA_5_Status));
			writel(1,(void*)(np->base_addr + ETH_DMA_1_Tr_Poll_Demand));
			netif_wake_queue(dev);
		}
		if (status & TX_STOP_EN) {
			writel((TX_STOP_EN | ANOR_INTR_EN),
			           (void*)(np->base_addr + ETH_DMA_5_Status));
			writel(1,(void*)(np->base_addr + ETH_DMA_1_Tr_Poll_Demand));
			netif_wake_queue(dev);
			res |= 1;
		}
		if (status & TX_JABBER_TIMEOUT) {
			writel((TX_JABBER_TIMEOUT | ANOR_INTR_EN),
			          (void*) (np->base_addr + ETH_DMA_5_Status));
			printk(KERN_WARNING "[" DRV_NAME "]" "tx jabber timeout\n");
			writel(1,(void*)(np->base_addr + ETH_DMA_1_Tr_Poll_Demand));
			netif_wake_queue(dev);
			np->first_tx = 1;
		}
		if (status & RX_FIFO_OVER) {
			writel((RX_FIFO_OVER | ANOR_INTR_EN),
			          (void*)( np->base_addr + ETH_DMA_5_Status));
			np->stats.rx_fifo_errors++;
			need_rx_restart++;
			res |= 2;
			printk(KERN_WARNING "[" DRV_NAME "]" "Rx fifo over\n");
		}
		if (status & TX_UNDERFLOW) {
			writel((TX_UNDERFLOW | ANOR_INTR_EN),
			           (void*)(np->base_addr + ETH_DMA_5_Status));
			printk(KERN_WARNING "[" DRV_NAME "]" "Tx underflow\n");
			writel(1,(void*)(np->base_addr + ETH_DMA_1_Tr_Poll_Demand));
			netif_wake_queue(dev);
			np->first_tx = 1;
			res |= 1;
		}
	}

	if (need_reset) {
		printk(KERN_WARNING DRV_NAME "system reset\n");
		free_ringdesc(dev);
		writel(0, (void*)(np->base_addr + ETH_DMA_6_Operation_Mode));
		writel(0, (void*)(np->base_addr + ETH_DMA_7_Interrupt_Enable));
		reset_mac(dev);
	} else if (need_rx_restart) {
		writel(1, (void*)(np->base_addr + ETH_DMA_2_Re_Poll_Demand));
	}
	return res;
}

/* --------------------------------------------------------------------------*/
/**
 * @brief  print_rx_error_log
 *
 * @param  status
 */
/* --------------------------------------------------------------------------*/
static void inline print_rx_error_log(unsigned long status)
{

	if (status & DescRxTruncated) {
		printk(KERN_WARNING "Descriptor Error desc-mask[%d]\n",
		       DescRxTruncated);
	}
	if (status & DescSAFilterFail) {
		printk(KERN_WARNING
		       "Source Address Filter Fail rx desc-mask[%d]\n",
		       DescSAFilterFail);
	}
	if (status & DescRxLengthError) {
		printk(KERN_WARNING "Length Error rx desc-mask[%d]\n",
		       DescRxLengthError);
	}
	if (status & DescRxIPChecksumErr) {
		printk(KERN_WARNING "TCP checksum Error rx desc-mask[%d]\n",
		       DescRxLengthError);
	}
	if (status & DescRxTCPChecksumErr) {
		printk(KERN_WARNING "TCP checksum Error rx desc-mask[%d]\n",
		       DescRxLengthError);
	}
	if (status & DescRxDamaged) {
		printk(KERN_WARNING "Overflow Error rx desc-mask[%d]\n",
		       DescRxDamaged);
	}
	if (status & DescRxMiiError) {
		printk(KERN_WARNING "Receive Error rx desc-mask[%d]\n",
		       DescRxMiiError);
	}
	if (status & DescRxDribbling) {
		printk(KERN_WARNING "Dribble Bit Error rx desc-mask[%d]\n",
		       DescRxDribbling);
	}
	if (status & DescRxCrc) {
		printk(KERN_WARNING "CE: CRC Errorrx desc-mask[%d]\n",
		       DescRxCrc);
	}
}

/* --------------------------------------------------------------------------*/
/**
 * @brief  net_tasklet
 *
 * @param  dev_instance
 */
/* --------------------------------------------------------------------------*/
void net_tasklet(unsigned long dev_instance)
{
	struct net_device *dev = (struct net_device *)dev_instance;
	struct am_net_private *np = netdev_priv(dev);
	int len;
	int result;
	unsigned long flags;

#ifndef DMA_USE_SKB_BUF
	struct sk_buff *skb = NULL;
#endif
	if (!running) {
		goto release;
	}

	/* handle pmt event */
	result = np->pmt;
	np->pmt = 0;
	if (result & (1 << 5)) {
		printk("*******************************\n");
		printk("******** Magic Packet Received!\n");
		printk("*******************************\n");
	}
	if (result & (1 << 6)) {
		printk("*******************************\n");
		printk("******** Wake-Up Frame Received!\n");
		printk("*******************************\n");
	}

	/* handle normal tx-rx */
	result = np->int_rx_tx;
	np->int_rx_tx = 0;

	if (result & 1) {
		struct _tx_desc *c_tx, *tx = NULL;
		int rx_count = 0;
		c_tx = (void *)readl((void*)(np->base_addr + ETH_DMA_18_Curr_Host_Tr_Descriptor));
		c_tx = np->tx_ring + (c_tx - np->tx_ring_dma);
		tx = np->start_tx;
		CACHE_RSYNC(tx, sizeof(struct _tx_desc));
		while (tx != NULL && tx != c_tx && !(tx->status & DescOwnByDma)) {
#ifdef DMA_USE_SKB_BUF
			rx_count++;

			if(unlikely(!spin_trylock_irqsave(&np->lock,flags)))
                        {
                            break;
                        }
			if (tx->skb != NULL) {
				//clear to next send;
				if (np->tx_full) {
					netif_wake_queue(dev);
					np->tx_full = 0;
				}
				if (g_debug > 2) {
					printk("send data ok len=%d\n", tx->skb->len);
				}
				tx_data_dump((unsigned char *)tx->buf, tx->skb->len);
				if (tx->buf_dma != 0) {
					dma_unmap_single(&dev->dev, tx->buf_dma, np->rx_buf_sz, DMA_TO_DEVICE);
				}
				dev_kfree_skb_any(tx->skb);
				tx->skb = NULL;
				tx->buf = 0;
				tx->buf_dma = 0;
				tx->status = 0;
			} else {
				spin_unlock_irqrestore(&np->lock, flags);
				break;
			}
			spin_unlock_irqrestore(&np->lock, flags);
#else
			tx->status = 0;
			CACHE_WSYNC(tx, sizeof(struct _tx_desc));
			if (np->tx_full) {
				netif_wake_queue(dev);
				np->tx_full = 0;
			}
#endif
			tx = tx->next;
			CACHE_RSYNC(tx, sizeof(struct _tx_desc));
		if(rx_count == g_txnum)
			break;
		}
		np->start_tx = tx;
	}
	if (result & 2) {
		struct _rx_desc *c_rx, *rx = NULL;
		int rx_cnt = 0;
		c_rx = (void *)readl((void*)(np->base_addr + ETH_DMA_19_Curr_Host_Re_Descriptor));
		c_rx = np->rx_ring + (c_rx - np->rx_ring_dma);
		rx = np->last_rx->next;
		while (rx != NULL) {
			CACHE_RSYNC(rx, sizeof(struct _rx_desc));
			if (!(rx->status & (DescOwnByDma))) {
				int ip_summed = CHECKSUM_UNNECESSARY;
				rx_cnt++;
				len = (rx->status & DescFrameLengthMask) >> DescFrameLengthShift;
				if (unlikely(len < 18 || len > np->rx_buf_sz)) {	//here is fatal error we drop it ;
					np->stats.rx_dropped++;
					np->stats.rx_errors++;
					goto to_next;
				}
				if (unlikely(rx->status & (DescError))) {	//here is not often occur
					print_rx_error_log(rx->status);
					if ((rx->status & DescRxIPChecksumErr) || (rx->status & DescRxTCPChecksumErr)) {	//maybe checksum engine's problem;
						//we set the NONE for ip/tcp need check it again
						ip_summed = CHECKSUM_NONE;
					} else {
						np->stats.rx_dropped++;
						np->stats.rx_errors++;
						goto to_next;
					}
				}
				len = len - 4;	//clear the crc
#ifdef DMA_USE_SKB_BUF
				if (rx->skb == NULL) {
					printk("NET skb pointer error!!!\n");
					break;
				}

				if (rx->buf_dma != 0) {
					dma_unmap_single(&dev->dev, rx->buf_dma,/* np->rx_buf_sz*/len, DMA_FROM_DEVICE);
				}
				if (rx->skb->len > 0) {
                                        printk("skb have data before,skb=%p,len=%d\n", rx->skb, rx->skb->len);
                                        rx->skb = NULL;
                                        goto to_next;
                                }
                                skb_put(rx->skb, len);
                                rx->skb->dev = dev;
                                rx->skb->protocol =
                                    eth_type_trans(rx->skb, dev);
                                /*we have checked in hardware;
                                   we not need check again */
                                rx->skb->ip_summed = ip_summed;
				rx->buf_dma = 0;
				netif_rx(rx->skb);
				if (g_debug > 3) {
					printk("receive skb=%p\n", rx->skb);
				}
				rx->skb = NULL;
#else
				skb = dev_alloc_skb(len);
				if (skb == NULL) {
					np->stats.rx_dropped++;
					printk("error to alloc skb\n");
					break;
				}
				skb_reserve(skb, 2);
				skb_put(skb, len);
				if (rx->buf_dma != NULL) {
					dma_unmap_single(&dev->dev, (void *)rx->buf_dma, np->rx_buf_sz, DMA_FROM_DEVICE);
				}
				memcpy(skb->data, (void *)rx->buf, len);
				skb->dev = dev;
				skb->protocol = eth_type_trans(skb, dev);
				skb->ip_summed = ip_summed;
				netif_rx(skb);
#endif
				dev->last_rx = jiffies;
				np->stats.rx_packets++;
				np->stats.rx_bytes += len;

				if (g_debug > 3) {
					printk("receive data len=%d\n", len);
				}
				rx_data_dump((unsigned char *)rx->buf,len);
to_next:
#ifdef DMA_USE_SKB_BUF
				if (rx->skb) {
					dev_kfree_skb_any(rx->skb);
				}
				rx->skb = dev_alloc_skb(np->rx_buf_sz );
				if (rx->skb == NULL) {
					printk(KERN_ERR "error to alloc the skb\n");
					rx->buf = 0;
					rx->buf_dma = 0;
					rx->status = 0;
					rx->count = 0;
					np->last_rx = rx;
					CACHE_WSYNC(rx, sizeof(struct _rx_desc));
					break;
				}
				if (g_debug > 3) {
					printk("new malloc skb=%p\n", rx->skb);
				}
				skb_reserve(rx->skb, 2);
				rx->buf = (unsigned long)rx->skb->data;
#endif
				rx->buf_dma = dma_map_single(&dev->dev, (void *)rx->buf, (unsigned long)np->rx_buf_sz, DMA_FROM_DEVICE);	//invalidate for next dma in;
				rx->count = (DescChain) | (np->rx_buf_sz & DescSize1Mask);
				rx->status = DescOwnByDma;
				CACHE_WSYNC(rx, sizeof(struct _rx_desc));
				np->last_rx = rx;
				rx = rx->next;
			} else {
				break;
			}
			if(rx_cnt == g_rxnum)
				break;

		}
	}
release:
	writel(np->irq_mask, (void*)(np->base_addr + ETH_DMA_7_Interrupt_Enable));
}

/* --------------------------------------------------------------------------*/
/**
 * @brief  intr_handler
 *
 * @param  irq
 * @param  dev_instance
 *
 * @return
 */
/* --------------------------------------------------------------------------*/
static irqreturn_t intr_handler(int irq, void *dev_instance)
{
	struct net_device *dev = (struct net_device *)dev_instance;
	struct am_net_private *np = netdev_priv(dev);
	unsigned long status = 0;
	unsigned long mask = 0;
	writel(0, (void*)(np->base_addr + ETH_DMA_7_Interrupt_Enable));//disable irq
	np->pmt = readl((void*)(np->base_addr + ETH_MAC_PMT_Control_and_Status));
	status = readl((void*)(np->base_addr + ETH_DMA_5_Status));
	mask = readl((void*)(np->base_addr + ETH_MAC_Interrupt_Mask));
	np->int_rx_tx |= update_status(dev, status, mask);
	tasklet_schedule(&np->rx_tasklet);
	return IRQ_HANDLED;
}

static int mac_pmt_enable(unsigned int enable)
{
	struct am_net_private *np = netdev_priv(my_ndev);
	unsigned long val;
	int i;

	if (enable >= 1) {
		switch (enable) {
		case 1:
			/* setup pmt mode */
			val = 0 << 0  //Power Down
				| 1 << 1  //Magic Packet Enable
				| 0;
			writel(val, (void*)(np->base_addr + ETH_MAC_PMT_Control_and_Status));
			break;
		case 2:
			val = 0 << 0  //Power Down
				| 1 << 2  //Wake-Up Frame Enable
				| 1 << 31 //Wake-Up Frame Filter Register Pointer Reset
				| 0;
			writel(val, (void*)(np->base_addr + ETH_MAC_PMT_Control_and_Status));

			/* setup Wake-Up Frame Filter */
			/* Filter 0 */
			val = 0x7f;
			writel(val, (void*)(np->base_addr + ETH_MAC_Remote_Wake_Up_Frame_Filter));
			val = 0;
			/* Filter 1,2,3 */
			for (i = 0; i < 3; i++) {
				writel(val, (void*)(np->base_addr + ETH_MAC_Remote_Wake_Up_Frame_Filter));
			}
			val = 1 << 0 //Enable Filter 0
				| 1 << 3 //Multicast
				| 0;
			writel(val, (void*)(np->base_addr + ETH_MAC_Remote_Wake_Up_Frame_Filter));
			val = 42;
			writel(val, (void*)(np->base_addr + ETH_MAC_Remote_Wake_Up_Frame_Filter));
			val = 0x5b3e;
			writel(val, (void*)(np->base_addr + ETH_MAC_Remote_Wake_Up_Frame_Filter));
			val = 0;
			writel(val, (void*)(np->base_addr + ETH_MAC_Remote_Wake_Up_Frame_Filter));

			for (i = 0; i < 8; i++) {
				val = readl((void*)(np->base_addr + ETH_MAC_Remote_Wake_Up_Frame_Filter));
				printk("ETH_MAC_Remote_Wake_Up_Frame_Filter=%d : 0x%lx\n", i, val);
			}
			break;
		case 3:
			val = 0 << 0  //Power Down
				| 1 << 2  //Wake-Up Frame Enable
				| 1 << 9  //Global Unicast
				| 0;
			writel(val,(void*)(np->base_addr + ETH_MAC_PMT_Control_and_Status));
			break;
		default:
			break;
		}

	} else {
		/* setup pmt mode */
		val = 0;
		writel(val, (void*)(np->base_addr + ETH_MAC_PMT_Control_and_Status));

		/* setup Wake-Up Frame Filter */
	}

	return 0;
}

/* --------------------------------------------------------------------------*/
/**
 * @brief  phy_reset
 *
 * @param  ndev
 *
 * @return
 */
/* --------------------------------------------------------------------------*/
//#undef CONFIG_AML_NAND_KEY
#ifdef CONFIG_AML_NAND_KEY
extern int get_aml_key_kernel(const char* key_name, unsigned char* data, int ascii_flag);
extern int extenal_api_key_set_version(char *devvesion);
static char print_buff[1025];
void read_mac_from_nand(struct net_device *ndev)
{
	int ret;
	u8 mac[ETH_ALEN];
	char *endp;
	int j;
	ret = get_aml_key_kernel("mac", print_buff, 0);
	extenal_api_key_set_version("nand3");
	printk("ret = %d\nprint_buff=%s\n", ret, print_buff);
	if (ret >= 0) {
		strcpy(ndev->dev_addr, print_buff);
	for(j=0; j < ETH_ALEN; j++)
	{
		mac[j] = simple_strtol(&ndev->dev_addr[3 * j], &endp, 16);
		printk("%d : %d\n", j, mac[j]);
	}
	memcpy(ndev->dev_addr, mac, ETH_ALEN);
	}

}
#endif
static int aml_mac_init(struct net_device *ndev)
{
	struct am_net_private *np = netdev_priv(ndev);
	unsigned long val;

	writel(1, (void*)(np->base_addr + ETH_DMA_0_Bus_Mode));
	writel(0x00100800,(void*)(np->base_addr + ETH_DMA_0_Bus_Mode));
	printk("--1--write mac add to:");

	data_dump(ndev->dev_addr, 6);
#ifdef CONFIG_AML_NAND_KEY
	read_mac_from_nand(ndev);
#endif
	printk("--2--write mac add to:");
	data_dump(ndev->dev_addr, 6);
	write_mac_addr(ndev, ndev->dev_addr);

	val = 0xc80c |		//8<<8 | 8<<17; //tx and rx all 8bit mode;
	      1 << 10 | 1 << 24;		//checksum offload enabled
#ifdef MAC_LOOPBACK_TEST
	val |= 1 << 12; //mac loop back
#endif

	writel(val, (void*)(np->base_addr + ETH_MAC_0_Configuration));

	val = 1 << 4;/*receive all muticast*/
	//| 1 << 31;	//receive all the data
	writel(val,(void*)( np->base_addr + ETH_MAC_1_Frame_Filter));

	writel((unsigned long)&np->rx_ring_dma[0], (void*)(np->base_addr + ETH_DMA_3_Re_Descriptor_List_Addr));
	writel((unsigned long)&np->tx_ring_dma[0], (void*)(np->base_addr + ETH_DMA_4_Tr_Descriptor_List_Addr));
	writel(np->irq_mask, (void*)(np->base_addr + ETH_DMA_7_Interrupt_Enable));
	writel((0), (void*)(np->base_addr + ETH_MAC_Interrupt_Mask));
	val = 7 << 14 //TTC
		| 1 << 8  //EFC
		| 1 << 21 //TSF
		| 1 << 25 //RSF
		| 1 << 26;//DT
	/*don't start receive here */
	printk("Current DMA mode=%x, set mode=%lx\n", readl((void*)(np->base_addr + ETH_DMA_6_Operation_Mode)), val);
	writel(val, (void*)(np->base_addr + ETH_DMA_6_Operation_Mode));

	/* enable mac mpt mode */
	//mac_pmt_enable(1);
	return 0;
}

static void aml_adjust_link(struct net_device *dev)
{
	struct am_net_private *priv = netdev_priv(dev);
	struct phy_device *phydev = priv->phydev;
	unsigned long flags;
	int new_state = 0;
	int val;

	if (phydev == NULL)
		return;

	spin_lock_irqsave(&priv->lock, flags);
	if(phydev->phy_id == INTERNALPHY_ID){
		val = (8<<27)|(7 << 24)|(1<<16)|(1<<15)|(1 << 13)|(1 << 12)|(4 << 4)|(0 << 1);
		PERIPHS_SET_BITS(P_PREG_ETHERNET_ADDR0, val);
	}
	if (phydev->link) {
		u32 ctrl = readl((void*)(priv->base_addr + ETH_MAC_0_Configuration));

		/* Now we make sure that we can be in full duplex mode.
		 * If not, we operate in half-duplex mode. */
		if (phydev->duplex != priv->oldduplex) {
			new_state = 1;
			if (!(phydev->duplex)) {
				ctrl &= ~((1 << 11)|(7<< 17)|(3<<5));
				if(new_maclogic != 0)
					ctrl |= (4 << 17);
				ctrl |= (3 << 5);
				g_rxnum = 128;
				g_txnum = 128;
			}
			else {
				ctrl &= ~((7 << 17)|(3 << 5));
				ctrl |= (1 << 11);
				if(new_maclogic != 0)
					ctrl |= (2 << 17);
				g_rxnum = 128;
				g_txnum = 128;
			}

			priv->oldduplex = phydev->duplex;
		}

		if (phydev->speed != priv->speed) {
			new_state = 1;
			if(new_maclogic != 0)
				PERIPHS_CLEAR_BITS(P_PREG_ETHERNET_ADDR0, 1);
			switch (phydev->speed) {
				case 1000:
					ctrl &= ~((1 << 14)|(1 << 15));//1000m 
					ctrl |= (1 << 13);//1000m 
					break;
				case 100:
					ctrl |= (1 << 14)|(1 << 15);
					if(new_maclogic !=0)
						PERIPHS_SET_BITS(P_PREG_ETHERNET_ADDR0, (1 << 1));
					break;
				case 10:
					ctrl &= ~((1 << 14)|(3 << 5));//10m half backoff = 00
					if(new_maclogic !=0)
						PERIPHS_CLEAR_BITS(P_PREG_ETHERNET_ADDR0, (1 << 1));
					if(phydev->phy_id == INTERNALPHY_ID){
						val =0x4100b040;
						WRITE_CBUS_REG(P_PREG_ETHERNET_ADDR0, val);
					}
					break;
				default:
					printk("%s: Speed (%d) is not 10"
								" or 100!\n", dev->name, phydev->speed);
					break;
			}
			if(new_maclogic !=0)
				PERIPHS_SET_BITS(P_PREG_ETHERNET_ADDR0, 1);
			priv->speed = phydev->speed;
		}

		writel(ctrl, (void*)(priv->base_addr + ETH_MAC_0_Configuration));

		if (!priv->oldlink) {
			new_state = 1;
			priv->oldlink = 1;
		}
	} else if (priv->oldlink) {
		new_state = 1;
		priv->oldlink = 0;
		priv->speed = 0;
		priv->oldduplex = -1;

	}

	if (new_state){
		if(new_maclogic == 1)
			read_macreg();
		phy_print_status(phydev);
	}

	spin_unlock_irqrestore(&priv->lock, flags);

#ifdef LOOP_BACK_TEST
#ifdef PHY_LOOPBACK_TEST
	mdio_write(priv->mii, priv->phy_addr, MII_BMCR, BMCR_LOOPBACK | BMCR_SPEED100 | BMCR_FULLDPLX);
#endif
	start_test(priv->dev);
#endif
}


static int aml_phy_init(struct net_device *dev)
{
        struct am_net_private *priv = netdev_priv(dev);
        struct phy_device *phydev;
        char phy_id[MII_BUS_ID_SIZE + 3];
        char bus_id[MII_BUS_ID_SIZE];

        priv->oldlink = 0;
        priv->speed = 0;
        priv->oldduplex = -1;
		printk("phy_interface = %d\n",phy_interface);
		if(phy_interface == 1)
			priv->phy_interface = PHY_INTERFACE_MODE_RMII;
		else
			priv->phy_interface = PHY_INTERFACE_MODE_RGMII;

        if (priv->phy_addr == -1) {
                /* We don't have a PHY, so do nothing */
                pr_err("%s: have no attached PHY\n", dev->name);
                return -1;
        }

        snprintf(bus_id, MII_BUS_ID_SIZE, "%x", 0);
        snprintf(phy_id, MII_BUS_ID_SIZE + 3, PHY_ID_FMT, bus_id,
                 priv->phy_addr);
        printk("aml_phy_init:  trying to attach to %s\n", phy_id);
        if(priv->phydev && savepowermode)
            priv->phydev->drv->resume(priv->phydev);
        phydev = phy_connect(dev, phy_id, &aml_adjust_link, priv->phy_interface);

        if (IS_ERR(phydev)) {
                pr_err("%s: Could not attach to PHY\n", dev->name);
                return PTR_ERR(phydev);
        }

        /*
         * Broken HW is sometimes missing the pull-up resistor on the
         * MDIO line, which results in reads to non-existent devices returning
         * 0 rather than 0xffff. Catch this here and treat 0 as a non-existent
         * device as well.
         * Note: phydev->phy_id is the result of reading the UID PHY registers.
         */
        if (phydev->phy_id == 0) {
                phy_disconnect(phydev);
                return -ENODEV;
        }
        pr_debug("aml_phy_init:  %s: attached to PHY (UID 0x%x)"
               " Link = %d\n", dev->name, phydev->phy_id, phydev->link);

        priv->phydev = phydev;
	if (priv->phydev)
		phy_start(priv->phydev);

        return 0;
}
static void read_macreg(void)
{
	int reg = 0;
	int val = 0;
	struct am_net_private *np = netdev_priv(my_ndev);

	if ((np == NULL) || (np->dev == NULL))
		return;

	for (reg = ETH_MAC_0_Configuration; reg <= ETH_MAC_54_SGMII_RGMII_Status; reg += 0x4) {
		val = readl((void*)(np->base_addr + reg));
	}

	for (reg = ETH_DMA_0_Bus_Mode; reg <= ETH_DMA_21_Curr_Host_Re_Buffer_Addr; reg += 0x4) {
		val = readl((void*)(np->base_addr + reg));
	}
}

static int reset_mac(struct net_device *dev)
{
	struct am_net_private *np = netdev_priv(dev);
	int res;
	unsigned long flags;
	int tmp;

	spin_lock_irqsave(&np->lock, flags);
	res = alloc_ringdesc(dev);
	spin_unlock_irqrestore(&np->lock, flags);
	if (res != 0) {
		printk(KERN_INFO "can't alloc ring desc!err=%d\n", res);
		goto out_err;
	}
	aml_mac_init(dev);
	np->first_tx = 1;
	tmp = readl((void*)(np->base_addr + ETH_DMA_6_Operation_Mode));//tx enable
	tmp |= (7 << 14) | (1 << 13);
	writel(tmp, (void*)(np->base_addr + ETH_DMA_6_Operation_Mode));
	tmp = readl((void*)(np->base_addr + ETH_MAC_6_Flow_Control));
	tmp |= (1 << 1) | (1 << 0);
	writel(tmp, (void*)(np->base_addr + ETH_MAC_6_Flow_Control));

	tmp = readl((void*)(np->base_addr + ETH_DMA_6_Operation_Mode));
	tmp |= (1 << 1); /*start receive*/
	writel(tmp, (void*)(np->base_addr + ETH_DMA_6_Operation_Mode));
out_err:
	return res;
}

/* --------------------------------------------------------------------------*/
/**
 * @brief  ethernet_reset
 *
 * @param  dev
 *
 * @return
 */
/* --------------------------------------------------------------------------*/
static int ethernet_reset(struct net_device *dev)
{
	struct am_net_private *np = netdev_priv(dev);
	int res;
	unsigned long flags;
	int tmp;
	printk(KERN_INFO "Ethernet reset\n");

	spin_lock_irqsave(&np->lock, flags);
	res = alloc_ringdesc(dev);
	spin_unlock_irqrestore(&np->lock, flags);
	if (res != 0) {
		printk(KERN_INFO "can't alloc ring desc!err=%d\n", res);
		goto out_err;
	}

	res = aml_phy_init(dev);
	if (res != 0) {
		printk(KERN_INFO "init phy failed! err=%d\n", res);
		goto out_err;
	}

	aml_mac_init(dev);

	np->first_tx = 1;
	tmp = readl((void*)(np->base_addr + ETH_DMA_6_Operation_Mode));//tx enable
	tmp |= (7 << 14) | (1 << 13);
	writel(tmp, (void*)(np->base_addr + ETH_DMA_6_Operation_Mode));
	tmp = readl((void*)(np->base_addr + ETH_MAC_6_Flow_Control));
	tmp |= (1 << 1) | (1 << 0);
	writel(tmp, (void*)(np->base_addr + ETH_MAC_6_Flow_Control));
out_err:
	return res;
}

/* --------------------------------------------------------------------------*/
/**
 * @brief  netdev_open
 *
 * @param  dev
 *
 * @return
 */
/* --------------------------------------------------------------------------*/
static int netdev_open(struct net_device *dev)
{
	struct am_net_private *np = netdev_priv(dev);
	unsigned long val;
	int res;
	np->refcnt++;
	switch_mod_gate_by_name("ethernet",1);
	if (running) {
		return 0;
	}
	printk(KERN_INFO "netdev_open\n");
	res = ethernet_reset(dev);

	if (res != 0) {
		printk(KERN_INFO "ethernet_reset err=%d\n", res);
		goto out_err;
	}

	res = request_irq(dev->irq, &intr_handler, IRQF_SHARED, dev->name, dev);
	if (res) {
		printk(KERN_ERR "%s: request_irq error %d.,err=%d\n",
		       dev->name, dev->irq, res);
		goto out_err;
	}

	if (g_debug > 0)
		printk(KERN_DEBUG "%s: opened (irq %d).\n",
		       dev->name, dev->irq);

	val = readl((void*)(np->base_addr + ETH_DMA_6_Operation_Mode));
	val |= (1 << 1); /*start receive*/
	writel(val, (void*)(np->base_addr + ETH_DMA_6_Operation_Mode));
	running = 1;
	if(new_maclogic == 1){	
		writel(0xffffffff,(void*)(np->base_addr + ETH_MMC_ipc_intr_mask_rx));
		writel(0xffffffff,(void*)(np->base_addr + ETH_MMC_intr_mask_rx));
	}
	netif_start_queue(dev);

	return 0;
out_err:
	running = 0;
	return -EIO;
}

/* --------------------------------------------------------------------------*/
/**
 * @brief  netdev_close
 *
 * @param  dev
 *
 * @return
 */
/* --------------------------------------------------------------------------*/
static int netdev_close(struct net_device *dev)
{
	struct am_net_private *np = netdev_priv(dev);
	unsigned long val;

	if (!running) {
		return 0;
	}

	if (np->phydev && savepowermode) {
		np->phydev->drv->suspend(np->phydev);
	}
	if (np->phydev) {
		phy_stop(np->phydev);
		phy_disconnect(np->phydev);
	}

	running = 0;

	writel(0, (void*)(np->base_addr + ETH_DMA_6_Operation_Mode));
	writel(0, (void*)(np->base_addr + ETH_DMA_7_Interrupt_Enable));
	val = readl((void*)(np->base_addr + ETH_DMA_5_Status));
	while ((val & (7 << 17)) || (val & (7 << 20))) { /*DMA not finished?*/
		printk(KERN_ERR "ERROR! DMA is not stoped, val=%lx!\n", val);
		msleep(1);//waiting all dma is finished!!
		val = readl((void*)(np->base_addr + ETH_DMA_5_Status));
	}
	if (g_debug > 0) {
		printk(KERN_INFO "NET DMA is stoped, ETH_DMA_Status=%lx!\n", val);
	}
	disable_irq(dev->irq);
	netif_carrier_off(dev);
	netif_stop_queue(dev);
	free_ringdesc(dev);
	free_irq(dev->irq, dev);

	if (g_debug > 0) {
		printk(KERN_DEBUG "%s: closed\n", dev->name);
	}
	return 0;
}

/* --------------------------------------------------------------------------*/
/**
 * @brief  start_tx
 *
 * @param  skb
 * @param  dev
 *
 * @return
 */
/* --------------------------------------------------------------------------*/
static int start_tx(struct sk_buff *skb, struct net_device *dev)
{
	struct am_net_private *np = netdev_priv(dev);
	int tmp;
	struct _tx_desc *tx;
	unsigned long flags;
	dev->trans_start = jiffies;
	if (np->first_tx) {
		if(new_maclogic == 1)
			read_macreg();
	}
	if (!running) {
		return -1;
	}
	if (g_debug > 2) {
		printk(KERN_DEBUG "%s: Transmit frame queued\n", dev->name);
	}
	tasklet_disable(&np->rx_tasklet);
	spin_lock_irqsave(&np->lock, flags);
	writel(0,(void*)(np->base_addr + ETH_DMA_7_Interrupt_Enable));

	if (np->last_tx != NULL) {
		tx = np->last_tx->next;
	} else {
		tx = &np->tx_ring[0];
	}
	CACHE_RSYNC(tx, sizeof(*tx));
	if (tx->status & DescOwnByDma) {
		//spin_unlock_irqrestore(&np->lock, flags);
		if (g_debug > 2) {
			printk("tx queue is full \n");
		}
		goto err;
	}
#ifdef DMA_USE_SKB_BUF
	if (tx->skb != NULL) {
		if (tx->buf_dma != 0) {
			dma_unmap_single(&dev->dev, tx->buf_dma, np->rx_buf_sz, DMA_TO_DEVICE);
		}
		dev_kfree_skb_any(tx->skb);
	}
	tx->skb = skb;
	tx->buf = (unsigned long)skb->data;
#else
	memcpy((void *)tx->buf, skb->data, skb->len);
#endif
	tx->buf_dma = dma_map_single(&dev->dev, (void *)tx->buf, (unsigned long)(skb->len), DMA_TO_DEVICE);
	tx->count = ((skb->len << DescSize1Shift) & DescSize1Mask) | DescTxFirst | DescTxLast | DescTxIntEnable | DescChain;	//|2<<27; (1<<25, ring end)
	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		tx->count |= 0x3 << 27;	//add hw check sum;
	}
	tx->status = DescOwnByDma;
	np->last_tx = tx;
	np->stats.tx_packets++;
	np->stats.tx_bytes += skb->len;
	CACHE_WSYNC(tx, sizeof(*tx));
#ifndef DMA_USE_SKB_BUF
	dev_kfree_skb_any(skb);
#endif
	if (np->first_tx) {
		np->first_tx = 0;
		tmp = readl((void*)(np->base_addr + ETH_DMA_6_Operation_Mode));
		tmp |= (7 << 14) | (1 << 13);
		writel(tmp, (void*)(np->base_addr + ETH_DMA_6_Operation_Mode));
	} else {
		//ETH_DMA_1_Tr_Poll_Demand
	//	writel(1,(void*)(np->base_addr + ETH_DMA_1_Tr_Poll_Demand));
	}

	writel(1,(void*)(np->base_addr + ETH_DMA_1_Tr_Poll_Demand));
	writel(np->irq_mask, (void*)(np->base_addr + ETH_DMA_7_Interrupt_Enable));	
	spin_unlock_irqrestore(&np->lock, flags);
	tasklet_enable(&np->rx_tasklet);
	return NETDEV_TX_OK;
err:
	np->tx_full = 1;
	np->stats.tx_dropped++;
	netif_stop_queue(dev);
	writel(np->irq_mask,(void*) (np->base_addr + ETH_DMA_7_Interrupt_Enable));
	spin_unlock_irqrestore(&np->lock, flags);
	tasklet_enable(&np->rx_tasklet);
	return NETDEV_TX_BUSY;
}

#ifdef LOOP_BACK_TEST
/* --------------------------------------------------------------------------*/
/**
 * @brief  test_loop_back
 *
 * @param  dev
 */
/* --------------------------------------------------------------------------*/
void test_loop_back(struct net_device *dev)
{
	//static int start_tx(struct sk_buff *skb, struct net_device *dev)
	//struct am_net_private *np = netdev_priv(dev);
	int i = 0;
	char header[64] = "";
	struct am_net_private *np = netdev_priv(dev);

	printk("start testing!!\n");
	memcpy(header, dev->dev_addr, 6);
	memcpy(header + 8, dev->dev_addr, 6);
	header[12] = 0x80;
	header[13] = 0;
	while (1) {
		struct sk_buff *skb = dev_alloc_skb(1600);
		while (!running) {
			i = 0;
			msleep(10);
		}

		skb_put(skb, 1400);
		memset(skb->data, 0x55, skb->len);
		memcpy(skb->data, header, 16);
		if (start_tx(skb, dev) != 0) {
			/*tx list is full*/
			msleep(1);
			dev_kfree_skb(skb);
		} else {
			i++;
		}
		if (i % 2000 == 0) {
			msleep(1);
			printk("send pkts=%ld, receive pkts=%ld\n", np->stats.tx_packets, np->stats.rx_packets);
		}

	}
}

static void force_speed100_duplex_set(struct am_net_private *np)
{
	int val;

	val = readl((void*)(np->base_addr + ETH_MAC_0_Configuration));
	val |= (1 << 11) | (1 << 14);
	writel(val, (void*)(np->base_addr + ETH_MAC_0_Configuration));

	PERIPHS_CLEAR_BITS(P_PREG_ETHERNET_ADDR0, 1);
	PERIPHS_SET_BITS(P_PREG_ETHERNET_ADDR0, (1 << 1));
	PERIPHS_SET_BITS(P_PREG_ETHERNET_ADDR0, 1);

	return;
}
/* --------------------------------------------------------------------------*/
/**
 * @brief  start_test
 *
 * @param  dev
 */
/* --------------------------------------------------------------------------*/
void start_test(struct net_device *dev)
{
	static int test_running = 0;
	struct am_net_private *np = netdev_priv(dev);

	force_speed100_duplex_set(np);

	if (test_running) {
		return ;
	}

	kernel_thread((void *)test_loop_back, (void *)dev, CLONE_FS | CLONE_SIGHAND);
	test_running++;

}
#endif
static struct net_device_stats *get_stats(struct net_device *dev) {
	struct am_net_private *np = netdev_priv(dev);

	return &np->stats;
}

/* --------------------------------------------------------------------------*/
/**
 * @brief  tx_timeout
 *
 * @param  dev
 */
/* --------------------------------------------------------------------------*/
static void tx_timeout(struct net_device *dev)
{
	struct am_net_private *np = netdev_priv(dev);
	int val;
	spin_lock_irq(&np->lock);
	val = mdio_read(np->mii, np->phy_addr, MII_BMSR);
	spin_unlock_irq(&np->lock);
	if (!(val & (BMSR_LSTATUS))) {	//unlink .....
		netif_stop_queue(dev);
		netif_carrier_off(dev);
	} else {
		netif_carrier_on(dev);
		netif_wake_queue(dev);
		dev->trans_start = jiffies;
		np->stats.tx_errors++;
	}
}

/* --------------------------------------------------------------------------*/
/**
 * @brief  write_mac_addr
 *
 * @param  dev
 * @param  macaddr
 */
/* --------------------------------------------------------------------------*/
/*static void get_mac_from_nand(struct net_device *dev, char *macaddr)
{
	int ret;
	int use_nand_mac=0;
	u8 mac[ETH_ALEN];

	extenal_api_key_set_version("nand3");
	ret = get_aml_key_kernel("mac_wifi", print_buff, 0);
	printk("ret = %d\nprint_buff=%s\n", ret, print_buff);
	if (ret >= 0) {
		strcpy(mac_addr, print_buff);
	}
	for(; j < ETH_ALEN; j++)
		{
		mac[j] = simple_strtol(&mac_addr[3 * j], &endp, 16);
		printk("%d : %d\n", j, mac[j]);
	}
	memcpy(macaddr, mac, ETH_ALEN);
}
static void print_mac(char *macaddr)
{
	printk("write mac add to:");
	data_dump(macaddr, 6);
}*/



static void write_mac_addr(struct net_device *dev, char *macaddr)
{
	struct am_net_private *np = netdev_priv(dev);
	unsigned int val;
	val = *((unsigned short *)&macaddr[4]);
	writel(val, (void*)(np->base_addr + ETH_MAC_Addr0_High));
	val = *((unsigned long *)macaddr);
	writel(val, (void*)(np->base_addr + ETH_MAC_Addr0_Low));
	printk("write mac add to:");
	data_dump(macaddr, 6);
}

/* --------------------------------------------------------------------------*/
/**
 * @brief  chartonum
 *
 * @param  c
 *
 * @return
 */
/* --------------------------------------------------------------------------*/
static unsigned char inline chartonum(char c)
{
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	if (c >= 'A' && c <= 'F') {
		return (c - 'A') + 10;
	}
	if (c >= 'a' && c <= 'f') {
		return (c - 'a') + 10;
	}
	return 0;

}

/* --------------------------------------------------------------------------*/
/**
 * @brief  config_mac_addr
 *
 * @param  dev
 * @param  mac
 */
/* --------------------------------------------------------------------------*/
static void config_mac_addr(struct net_device *dev, void *mac)
{
	if(g_mac_addr_setup)
		memcpy(dev->dev_addr, mac, 6);
	else
		random_ether_addr(dev->dev_addr);

	write_mac_addr(dev, dev->dev_addr);
}

/* --------------------------------------------------------------------------*/
/**
 * @brief  mac_addr_set
 *
 * @param  line
 *
 * @return
 */
/* --------------------------------------------------------------------------*/
static int __init mac_addr_set(char *line)
{
	unsigned char mac[6];
	int i = 0;
	for (i = 0; i < 6 && line[0] != '\0' && line[1] != '\0'; i++) {
		mac[i] = chartonum(line[0]) << 4 | chartonum(line[1]);
		line += 3;
	}
	memcpy(DEFMAC, mac, 6);
	printk("******** uboot setup mac-addr: %x:%x:%x:%x:%x:%x\n",
			DEFMAC[0], DEFMAC[1], DEFMAC[2], DEFMAC[3], DEFMAC[4], DEFMAC[5]);
	g_mac_addr_setup++;

	return 1;
}

__setup("mac=", mac_addr_set);


/* --------------------------------------------------------------------------*/
/**
 * @brief  phy_mc_hash
 *
 * @param  addr
 *
 * @return
 */
/* --------------------------------------------------------------------------*/
static inline int phy_mc_hash(__u8 *addr)
{
	return (bitrev32(~crc32_le(~0, addr, ETH_ALEN)) >> 26);
}

/* --------------------------------------------------------------------------*/
/**
 * @brief  set_multicast_list
 *
 * @param  dev
 */
/* --------------------------------------------------------------------------*/
static void set_multicast_list(struct net_device *dev)
{
	struct am_net_private *np = netdev_priv(dev);
	u32  tmp;
	static  u32  dev_flags=0xefefefef;
	static  u32  dev_hash[2]={0,0};

	if(dev->flags != dev_flags)//not always change
	{
		if ((dev->flags & IFF_PROMISC)) {
			tmp = readl((void*)(np->base_addr + ETH_MAC_1_Frame_Filter));
			tmp |= 1;
			writel(tmp, (void*)(np->base_addr + ETH_MAC_1_Frame_Filter));
			printk("ether enter promiscuous mode\n");
		} else {
			tmp = readl((void*)(np->base_addr + ETH_MAC_1_Frame_Filter));
			tmp &= ~1;
			writel(tmp, (void*)(np->base_addr + ETH_MAC_1_Frame_Filter));
			printk("ether leave promiscuous mode\n");
		}
		if ((dev->flags & IFF_ALLMULTI)) {
			tmp = readl((void*)(np->base_addr + ETH_MAC_1_Frame_Filter));
			tmp |= (1 << 4);
			writel(tmp, (void*)(np->base_addr + ETH_MAC_1_Frame_Filter));
			printk("ether enter all multicast mode\n");
		} else {
			tmp = readl((void*)(np->base_addr + ETH_MAC_1_Frame_Filter));
			tmp &= ~(1 << 4);
			writel(tmp, (void*)(np->base_addr + ETH_MAC_1_Frame_Filter));
			printk("ether leave all muticast mode\n");
		}
		dev_flags=dev->flags;
	}
	if (netdev_mc_count(dev) > 0) {
		u32 hash[2];
		struct netdev_hw_addr *ha;
		u32 hash_id;
		char * addr;
		hash[0] = 0;
		hash[1] = 0;
		printk("changed the Multicast,mcount=%d\n", netdev_mc_count(dev));
		netdev_for_each_mc_addr(ha, dev) {
			addr = ha->addr;
			hash_id = phy_mc_hash(addr);
			printk("add mac address:%02x:%02x:%02x:%02x:%02x:%02x,bit=%d\n",
			       addr[0], addr[1], addr[2], addr[3], addr[4], addr[5],
			       hash_id);

			if (hash_id > 31) {
				hash[1] |= 1 << (hash_id - 32);
			} else {
				hash[0] |= 1 << hash_id;
			}
		}
		if((dev_hash[0]==hash[0]) && (dev_hash[1]==hash[1])) return;
		dev_hash[0]=hash[0] ;
		dev_hash[1]=hash[1];
		printk("set hash low=%x,high=%x\n", hash[0], hash[1]);
		writel(hash[1],(void*)(np->base_addr + ETH_MAC_2_Hash_Table_High));
		writel(hash[0], (void*)(np->base_addr + ETH_MAC_3_Hash_Table_Low));
		tmp = readl((void*)(np->base_addr + ETH_MAC_1_Frame_Filter));
		tmp |= (1 << 2) | 	//hash filter
		       0;
		printk("changed the filter setting to :%x\n", tmp);
		writel(tmp, (void*)(np->base_addr + ETH_MAC_1_Frame_Filter));//hash muticast
	}
}
static int set_mac_addr_n(struct net_device *dev, void *addr){
	struct sockaddr *sa = addr;
	printk("mac addr come in\n");

	if (!is_valid_ether_addr(sa->sa_data))
		return -EADDRNOTAVAIL;

	memcpy(dev->dev_addr, sa->sa_data, ETH_ALEN);

	write_mac_addr(dev, dev->dev_addr);
	return 0;
}

static const struct net_device_ops am_netdev_ops = {
	.ndo_open               = netdev_open,
	.ndo_stop               = netdev_close,
	.ndo_start_xmit         = start_tx,
	.ndo_tx_timeout         = tx_timeout,
	.ndo_set_rx_mode = set_multicast_list,
	.ndo_do_ioctl			= netdev_ioctl,
	.ndo_get_stats          = get_stats,
	.ndo_change_mtu         = eth_change_mtu,
	.ndo_set_mac_address    = set_mac_addr_n,
	.ndo_validate_addr      = eth_validate_addr,
};

static int aml_ethtool_get_settings(struct net_device *dev,
					 struct ethtool_cmd *cmd)
{
	struct am_net_private *np = netdev_priv(dev);

	if (!np->phydev)
		return -ENODEV;

	cmd->maxtxpkt = 1;
	cmd->maxrxpkt = 1;
	return phy_ethtool_gset(np->phydev, cmd);
}

static int aml_ethtool_set_settings(struct net_device *dev,
					 struct ethtool_cmd *cmd)
{
	struct am_net_private *np = netdev_priv(dev);

	if (!np->phydev)
		return -ENODEV;

	return phy_ethtool_sset(np->phydev, cmd);
}

static int aml_ethtool_nway_reset(struct net_device *netdev)
{
	struct am_net_private *np = netdev_priv(netdev);

	if (!np->phydev)
		return -ENODEV;

	return phy_start_aneg(np->phydev);
}
static void aml_eth_get_wol(struct net_device *dev, struct ethtool_wolinfo *wol)
{
	struct am_net_private *np = netdev_priv(dev);
	wol->supported = 0;
	wol->wolopts = 0;
	if (np->phydev)
		phy_ethtool_get_wol(np->phydev, wol);
}

static int aml_eth_set_wol(struct net_device *dev, struct ethtool_wolinfo *wol)
{
	struct am_net_private *np = netdev_priv(dev);
	int err;

	if (np->phydev == NULL)
		return -EOPNOTSUPP;

	err = phy_ethtool_set_wol(np->phydev, wol);
	/* Given that amlogic mac works without the micrel PHY driver,
	 * this debugging hint is useful to have.
	 */
	if (err == -EOPNOTSUPP)
		printk( "The PHY does not support set_wol, was CONFIG_MICREL_PHY enabled?\n");
	return err;
}
static int aml_ethtool_op_get_eee(struct net_device *dev,
				     struct ethtool_eee *edata)
{
	struct am_net_private *np = netdev_priv(dev);
	if (np->phydev == NULL)
		return -EOPNOTSUPP;
	return phy_ethtool_get_eee(np->phydev, edata);
}

static int aml_ethtool_op_set_eee(struct net_device *dev,
				     struct ethtool_eee *edata)
{
	struct am_net_private *np = netdev_priv(dev);
	if (np->phydev == NULL)
		return -EOPNOTSUPP;
	return phy_ethtool_set_eee(np->phydev, edata);
}

static const struct ethtool_ops aml_ethtool_ops = {
	.get_settings = aml_ethtool_get_settings,
	.set_settings = aml_ethtool_set_settings,
	.nway_reset = aml_ethtool_nway_reset,
	.get_link = ethtool_op_get_link,
	.get_wol  = aml_eth_get_wol,
	.set_wol  = aml_eth_set_wol,
	.get_eee = aml_ethtool_op_get_eee,
	.set_eee = aml_ethtool_op_set_eee,
};
/* --------------------------------------------------------------------------*/
/**
 * @brief  setup_net_device
 *
 * @param  dev
 *
 * @return
 */
/* --------------------------------------------------------------------------*/
static int setup_net_device(struct net_device *dev)
{
	struct am_net_private *np = netdev_priv(dev);
	int res = 0;
	dev->dev.coherent_dma_mask = DMA_BIT_MASK(32);
	dev->features = NETIF_F_GEN_CSUM;
	dev->netdev_ops = &am_netdev_ops;
	dev->ethtool_ops = &aml_ethtool_ops;	// &netdev_ethtool_ops;
	dev->watchdog_timeo = TX_TIMEOUT;
	np->irq_mask = (1 << 16) |          //NIE: Normal Interrupt Summary Enable
	               (1 << 15) |          //abnormal int summary
	               (1 << 6) |           //Receive Interrupt Enable
	               (1 << 2) |           //Transmit Buffer Unavailable Enable
	               (1 << 3) |           //TJT: Transmit Jabber Timeout
	               (1 << 5) |           //UNF: Transmit Underflow
	               (1 << 8) |           //RPS: Receive Process Stopped
	               (1 << 13) |          //FBI: Fatal Bus Error Interrupt
	               (1) | 		        //tx interrupt
	               0;
	config_mac_addr(dev, DEFMAC);
	dev_alloc_name(dev, "eth%d");
	memset(&np->stats, 0, sizeof(np->stats));
	return res;
}

/*
M6TV
 23
M6TVlite
 24
M8
 25
M6TVd
 26
M8baby
 27
G9TV
 28
*/
#if 0
static unsigned int get_cpuid(){
	return READ_CBUS_REG(0x1f53)&0xff;
}
#endif
/* --------------------------------------------------------------------------*/
/**
 * @brief  probe_init
 *
 * @param  ndev
 *
 * @return
 */
/* --------------------------------------------------------------------------*/
static int probe_init(struct net_device *ndev)
{
	int res = 0;

	struct am_net_private *priv = netdev_priv(ndev);
	priv->dev = ndev;
	ndev->base_addr = (unsigned long)(ETHBASE);
	ndev->irq = ETH_INTERRUPT;
	spin_lock_init(&priv->lock);
	priv->base_addr = ndev->base_addr;
	priv->refcnt = 0;
	if (g_debug > 0) {
		printk("ethernet base addr is %x\n", (unsigned int)ndev->base_addr);
	}
	res = setup_net_device(ndev);
	if (res != 0) {
		printk("setup net device error !\n");
		res = -EIO;
		goto error0;
	}

	netif_carrier_off(ndev);

	res = register_netdev(ndev);
	if (res != 0) {
		printk("can't register net  device !\n");
		res = -EBUSY;
		goto error0;
	}
	tasklet_init(&priv->rx_tasklet, net_tasklet, (unsigned long)ndev);

	res = aml_mdio_register(ndev);
	if (res < 0) {
		goto out_unregister;
	}
	return 0;

out_unregister:
	unregister_netdev(ndev);
error0:
	return res;
}

/*
 * Ethernet debug
 */

static void initTSTMODE(void)
{
	struct am_net_private *np = netdev_priv(my_ndev);
	mdio_write(np->mii, np->phy_addr, 20, 0x0400);
	mdio_write(np->mii, np->phy_addr, 20, 0x0000);
	mdio_write(np->mii, np->phy_addr, 20, 0x0400);

}

static void closeTSTMODE(void)
{
	struct am_net_private *np = netdev_priv(my_ndev);
	mdio_write(np->mii, np->phy_addr, 20, 0x0000);
}

static void tstcntl_dump_phyreg(void)
{
	int rd_addr = 0;
	int rd_data = 0;
	int rd_data_hi =0;
	struct am_net_private *np = netdev_priv(my_ndev);

	if ((np == NULL) || (np->dev == NULL))
		return;

	printk("========== ETH TST PHY regs ==========\n");
	for (rd_addr = 0; rd_addr < 32; rd_addr++) {
		mdio_write(np->mii, np->phy_addr,20,((1 << 15) | (1 << 10) | ((rd_addr & 0x1f) << 5)));
		rd_data = mdio_read(np->mii, np->phy_addr, 21);
		rd_data_hi = mdio_read(np->mii, np->phy_addr, 22);
		rd_data = ((rd_data_hi & 0xffff) << 16) | rd_data;
		printk("tstcntl phy [reg_%d] 0x%x\n", rd_addr, rd_data);
	}
}


/* --------------------------------------------------------------------------*/
/**
 * @brief  am_net_dump_phyreg
 */
/* --------------------------------------------------------------------------*/
static void am_net_dump_phyreg(void)
{
	int reg = 0;
	int val = 0;
	struct am_net_private *np = netdev_priv(my_ndev);

	if ((np == NULL) || (np->dev == NULL))
		return;

	printk("========== ETH PHY regs ==========\n");
	for (reg = 0; reg < 32; reg++) {
		val = mdio_read(np->mii, np->phy_addr, reg);
		printk("[reg_%d] 0x%x\n", reg, val);
	}
}

/* --------------------------------------------------------------------------*/
/**
 * @brief  am_net_read_phyreg
 *
 * @param  argc
 * @param  argv
 *
 * @return
 */
/* --------------------------------------------------------------------------*/
static int am_net_read_phyreg(int argc, char **argv)
{
	int reg = 0;
	int val = 0;
	struct am_net_private *np = netdev_priv(my_ndev);

	if ((np == NULL) || (np->dev == NULL))
		return -1;

	if (argc < 2 || (argv == NULL) || (argv[0] == NULL) || (argv[1] == NULL)) {
		printk("Invalid syntax\n");
		return -1;
	}
	reg = simple_strtol(argv[1], NULL, 16);
	if (reg >= 0 && reg <= 31) {
		val = mdio_read(np->mii, np->phy_addr, reg);
		printk("read phy [reg_%d] 0x%x\n", reg, val);
	} else {
		printk("Invalid parameter\n");
	}

	return 0;
}

/* --------------------------------------------------------------------------*/
/**
 * @brief  am_net_write_phyreg
 *
 * @param  argc
 * @param  argv
 *
 * @return
 */
/* --------------------------------------------------------------------------*/
static int am_net_write_phyreg(int argc, char **argv)
{
	int reg = 0;
	int val = 0;
	struct am_net_private *np = netdev_priv(my_ndev);

	if ((np == NULL) || (np->dev == NULL))
		return -1;

	if (argc < 3 || (argv == NULL) || (argv[0] == NULL)
			|| (argv[1] == NULL) || (argv[2] == NULL)) {
		printk("Invalid syntax\n");
		return -1;
	}
	reg = simple_strtol(argv[1], NULL, 16);
	val = simple_strtol(argv[2], NULL, 16);
	if (reg >=0 && reg <=31) {
		mdio_write(np->mii, np->phy_addr, reg, val);
		printk("write phy [reg_%d] 0x%x, 0x%x\n", reg, val, mdio_read(np->mii, np->phy_addr, reg));
	} else {
		printk("Invalid parameter\n");
	}

	return 0;
}


static int readTSTCNTLRegister( int argc, char **argv){
	int rd_data =0;
	int rd_data_hi = 0;
	int rd_addr;
	struct am_net_private *np = netdev_priv(my_ndev);

	if ((np == NULL) || (np->dev == NULL))
		return -1;

	if (argc < 2 || (argv == NULL) || (argv[0] == NULL) || (argv[1] == NULL)) {
		printk("Invalid syntax\n");
		return -1;
	}
	rd_addr = simple_strtol(argv[1], NULL, 16);
	if (rd_addr >= 0 && rd_addr <= 31) {
		mdio_write(np->mii, np->phy_addr,20,((1 << 15) | (1 << 10) | ((rd_addr & 0x1f) << 5)));
		rd_data = mdio_read(np->mii, np->phy_addr, 21);
		rd_data_hi = mdio_read(np->mii, np->phy_addr, 22);
		rd_data = ((rd_data_hi & 0xffff) << 16) | rd_data;
		printk("read tstcntl phy [reg_%d] 0x%x\n", rd_addr, rd_data);
	} else {
		printk("Invalid parameter\n");
	}
	return rd_data;
}
int returnwriteval(int rd_addr)
{
	int rd_data =0;
	int rd_data_hi = 0;
	struct am_net_private *np = netdev_priv(my_ndev);
	mdio_write(np->mii, np->phy_addr,20,((1 << 15) | (1 << 10) | ((rd_addr & 0x1f) << 5)));
	rd_data = mdio_read(np->mii, np->phy_addr, 21);
	rd_data_hi = mdio_read(np->mii, np->phy_addr, 22);
	rd_data = ((rd_data_hi & 0xffff) << 16) | rd_data;
	return rd_data;
}

int writeTSTCNTLRegister( int argc, char **argv) {
	int wr_addr = 0;
	int wr_data = 0;
	struct am_net_private *np = netdev_priv(my_ndev);
	if ((np == NULL) || (np->dev == NULL))
		return -1;

	if (argc < 3 || (argv == NULL) || (argv[0] == NULL)
			|| (argv[1] == NULL) || (argv[2] == NULL)) {
		printk("Invalid syntax\n");
		return -1;
	}
	wr_addr = simple_strtol(argv[1], NULL, 16);
	wr_data = simple_strtol(argv[2], NULL, 16);
	if (wr_addr >=0 && wr_addr <=31) {
		mdio_write(np->mii, np->phy_addr, 23, (wr_data & 0xffff));
		mdio_write(np->mii, np->phy_addr, 20, ((1 << 14) | (1 << 10) | ((wr_addr << 0) & 0x1f)));
		printk("write phy tstcntl [reg_%d] 0x%x, 0x%x\n", wr_addr, wr_data, returnwriteval( wr_addr));
	} else {
		printk("Invalid parameter\n");
	}

	return 0;
}




static const char *g_phyreg_help = {
	"Usage:\n"
	"    echo d > phyreg;            //dump ethernet phy reg\n"
	"    echo r reg > phyreg;        //read ethernet phy reg\n"
	"    echo w reg val > phyreg;    //read ethernet phy reg\n"
};

/* --------------------------------------------------------------------------*/
/**
 * @brief  eth_phyreg_help
 *
 * @param  class
 * @param  attr
 * @param  buf
 *
 * @return
 */
/* --------------------------------------------------------------------------*/
static ssize_t eth_phyreg_help(struct class *class, struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", g_phyreg_help);
}

unsigned char adc_data[32*32];
unsigned char adc_freq[64];

static void adc_show(void)
{
	int rd_data =0;
	int rd_data_hi = 0;
	int i,j;
	struct am_net_private *np = netdev_priv(my_ndev);
	initTSTMODE();
	for (i=0;i<32*32;i++){
		mdio_write(np->mii, np->phy_addr,20,((1 << 15) | (1 << 10) | ((16 & 0x1f) << 5)));
		rd_data = mdio_read(np->mii, np->phy_addr, 21);
		rd_data_hi = mdio_read(np->mii, np->phy_addr, 22);
		adc_data[i] = rd_data & 0x3f;
	}
	closeTSTMODE();
	for (i=0;i<32;i++){
		for (j=0;j<32;j++){
			printk("%02x ", adc_data[i*32+j]);
		}
		printk("\n");
	}
	for (i=0;i<64;i++)
		adc_freq[i]=0;
	for (i=0;i<32;i++){
		for (j=0;j<32;j++){
			if (adc_data[i*32+j]>31) adc_freq[adc_data[i*32+j]-32]++;
			else adc_freq[32+adc_data[i*32+j]]++;
			printk("%02x ", adc_data[i*32+j]);
		}
		printk("\n");
	}
	for (i=0;i<64;i++){
		printk("%d:\t",i-32);
		if (adc_freq[i]>128) adc_freq[i]=128;
		for (j=0;j<adc_freq[i];j++)
			printk("#");
		printk("\n");
	}
}


/* --------------------------------------------------------------------------*/
/**
 * @brief  eth_phyreg_func
 *
 * @param  class
 * @param  attr
 * @param  buf
 * @param  count
 *
 * @return
 */
/* --------------------------------------------------------------------------*/
static ssize_t eth_phyreg_func(struct class *class, struct class_attribute *attr,
		const char *buf, size_t count)
{
	int argc;
	char *buff, *p, *para;
	char *argv[4];
	char cmd;

	buff = kstrdup(buf, GFP_KERNEL);
	p = buff;
	for (argc = 0; argc < 4; argc++) {
		para = strsep(&p, " ");
		if (para == NULL)
			break;
		argv[argc] = para;
	}
	if (argc < 1 || argc > 4)
		goto end;

	cmd = argv[0][0];
	switch (cmd) {
	case 'r':
	case 'R':
		am_net_read_phyreg(argc, argv);
		break;
	case 'w':
	case 'W':
		am_net_write_phyreg(argc, argv);
		break;
	case 'd':
	case 'D':
		am_net_dump_phyreg();
		break;
		case 't':
		case 'T':
			initTSTMODE();
			if((argv[0][1] == 'w')||(argv[0][1] == 'W'))
			{
				writeTSTCNTLRegister(argc, argv);
			}
			if((argv[0][1] == 'r')||(argv[0][1] == 'R'))
			{
				readTSTCNTLRegister(argc, argv);
			}
			if((argv[0][1] == 'd')||(argv[0][1] == 'D'))
				tstcntl_dump_phyreg();
			closeTSTMODE();
			break;
		case 'c':
		case 'C':
			adc_show();
			break;
	default:
		goto end;
	}

	return count;

end:
	kfree(buff);
	return 0;
}

/* --------------------------------------------------------------------------*/
/**
 * @brief  am_net_dump_macreg
 */
/* --------------------------------------------------------------------------*/
static void am_net_dump_macreg(void)
{
	int reg = 0;
	int val = 0;
	struct am_net_private *np = netdev_priv(my_ndev);

	if ((np == NULL) || (np->dev == NULL))
		return;

	printk("========== ETH_MAC regs ==========\n");
	for (reg = ETH_MAC_0_Configuration; reg <= ETH_MAC_54_SGMII_RGMII_Status; reg += 0x4) {
		val = readl((void*)(np->base_addr + reg));
		printk("[0x%04x] 0x%x\n", reg, val);
	}

	printk("========== ETH_DMA regs ==========\n");
	for (reg = ETH_DMA_0_Bus_Mode; reg <= ETH_DMA_21_Curr_Host_Re_Buffer_Addr; reg += 0x4) {
		val = readl((void*)(np->base_addr + reg));
		printk("[0x%04x] 0x%x\n", reg, val);
	}
}

/* --------------------------------------------------------------------------*/
/**
 * @brief  am_net_read_macreg
 *
 * @param  argc
 * @param  argv
 *
 * @return
 */
/* --------------------------------------------------------------------------*/
static int am_net_read_macreg(int argc, char **argv)
{
	int reg = 0;
	int val = 0;
	struct am_net_private *np = netdev_priv(my_ndev);

	if ((np == NULL) || (np->dev == NULL))
		return -1;

	if (argc < 2 || (argv == NULL) || (argv[0] == NULL) || (argv[1] == NULL)) {
		printk("Invalid syntax\n");
		return -1;
	}
	reg = simple_strtol(argv[1], NULL, 16);
	if (reg >= 0 && reg <= ETH_DMA_21_Curr_Host_Re_Buffer_Addr) {
		val = readl((void*)(np->base_addr + reg));
		printk("read mac [0x04%x] 0x%x\n", reg, val);
	} else {
		printk("Invalid parameter\n");
	}

	return 0;
}

/* --------------------------------------------------------------------------*/
/**
 * @brief  am_net_write_macreg
 *
 * @param  argc
 * @param  argv
 *
 * @return
 */
/* --------------------------------------------------------------------------*/
static int am_net_write_macreg(int argc, char **argv)
{
	int reg = 0;
	int val = 0;
	struct am_net_private *np = netdev_priv(my_ndev);

	if ((np == NULL) || (np->dev == NULL))
		return -1;

	if ((argc < 3) || (argv == NULL) || (argv[0] == NULL)
			|| (argv[1] == NULL) || (argv[2] == NULL)) {
		printk("Invalid syntax\n");
		return -1;
	}
	reg = simple_strtol(argv[1], NULL, 16);
	val = simple_strtol(argv[2], NULL, 16);
	if (reg >= 0 && reg <= ETH_DMA_21_Curr_Host_Re_Buffer_Addr) {
		writel(val, (void*)(np->base_addr + reg));
		printk("write mac [0x%x] 0x%x, 0x%x\n", reg, val, readl((void*)(np->base_addr + reg)));
	} else {
		printk("Invalid parameter\n");
	}

	return 0;
}

static const char *g_macreg_help = {
	"Usage:\n"
	"    echo d > macreg;            //dump ethernet mac reg\n"
	"    echo r reg > macreg;        //read ethernet mac reg\n"
	"    echo w reg val > macreg;    //read ethernet mac reg\n"
};

/* --------------------------------------------------------------------------*/
/**
 * @brief  eth_macreg_help
 *
 * @param  class
 * @param  attr
 * @param  buf
 *
 * @return
 */
/* --------------------------------------------------------------------------*/
static ssize_t eth_macreg_help(struct class *class, struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", g_macreg_help);
}

/* --------------------------------------------------------------------------*/
/**
 * @brief  eth_macreg_func
 *
 * @param  class
 * @param  attr
 * @param  buf
 * @param  count
 *
 * @return
 */
/* --------------------------------------------------------------------------*/
static ssize_t eth_macreg_func(struct class *class, struct class_attribute *attr,
		const char *buf, size_t count)
{
	int argc;
	char *buff, *p, *para;
	char *argv[4];
	char cmd;

	buff = kstrdup(buf, GFP_KERNEL);
	p = buff;
	for (argc = 0; argc < 4; argc++) {
		para = strsep(&p, " ");
		if (para == NULL)
			break;
		argv[argc] = para;
	}
	if (argc < 1 || argc > 4)
		goto end;

	cmd = argv[0][0];
	switch (cmd) {
	case 'r':
	case 'R':
		am_net_read_macreg(argc, argv);
		break;
	case 'w':
	case 'W':
		am_net_write_macreg(argc, argv);
		break;
	case 'd':
	case 'D':
		am_net_dump_macreg();
		break;
	default:
		goto end;
	}

	return count;

end:
	kfree(buff);
	return 0;
}
static const char *g_mdcclk_help = {
	"Ethernet mdcclk:\n"
	"    1. ETH_MAC_4_GMII_Addr_CR_60_100.\n"
	"    2. ETH_MAC_4_GMII_Addr_CR_100_150.\n"
	"    3. ETH_MAC_4_GMII_Addr_CR_20_35.\n"
	"    4. ETH_MAC_4_GMII_Addr_CR_35_60.\n"
	"    5. ETH_MAC_4_GMII_Addr_CR_150_250.\n"
	"    6. ETH_MAC_4_GMII_Addr_CR_250_300.\n"
};
/* --------------------------------------------------------------------------*/
/**
 * @brief  eth_mdcclk_show
 *
 * @param  class
 * @param  attr
 * @param  buf
 *
 * @return
 */
/* --------------------------------------------------------------------------*/
static ssize_t eth_mdcclk_show(struct class *class, struct class_attribute *attr, char *buf)
{
	int ret = 0;
	ret = sprintf(buf, "%s\n", g_mdcclk_help);
	printk("current ethernet mdcclk: %d\n", g_mdcclk);

	return ret;
}
/* --------------------------------------------------------------------------*/
/**
 * @brief  eth_mdcclk_store
 *
 * @param  class
 * @param  attr
 * @param  buf
 * @param  count
 *
 * @return
 */
/* --------------------------------------------------------------------------*/
static ssize_t eth_mdcclk_store(struct class *class, struct class_attribute *attr, const char *buf, size_t count)
{
	unsigned int mdcclk = 0;

	mdcclk = simple_strtoul(buf, NULL, 0);
	if (mdcclk >= 0 && mdcclk <= 6) {
		printk("ethernet mdcclk: %d -> %d\n", g_mdcclk, mdcclk);
		g_mdcclk = mdcclk;
	} else {
		printk("set ethernet debug error\n");
	}
	switch(g_mdcclk)
	{
		case 1:
		MDCCLK = ETH_MAC_4_GMII_Addr_CR_60_100;
		break;
		case 2:
		MDCCLK = ETH_MAC_4_GMII_Addr_CR_100_150;
		break;
		case 3:
		MDCCLK = ETH_MAC_4_GMII_Addr_CR_20_35;
		break;
		case 4:
		MDCCLK = ETH_MAC_4_GMII_Addr_CR_35_60;
		break;
		case 5:
		MDCCLK = ETH_MAC_4_GMII_Addr_CR_150_250;
		break;
		case 6:
		MDCCLK = ETH_MAC_4_GMII_Addr_CR_250_300;
		break;
		default:
		break;
	}

	return count;
}

/* --------------------------------------------------------------------------*/
static const char *g_debug_help = {
	"Ethernet Debug:\n"
	"    1. basic module init and remove info.\n"
	"    2. further setup info.\n"
	"    3. tx data dump.\n"
	"    4. rx data dump.\n"
	"    5. tx/rx data dump.\n"
};

/* --------------------------------------------------------------------------*/
/**
 * @brief  eth_debug_show
 *
 * @param  class
 * @param  attr
 * @param  buf
 *
 * @return
 */
/* --------------------------------------------------------------------------*/
static ssize_t eth_debug_show(struct class *class, struct class_attribute *attr, char *buf)
{
	int ret = 0;
	ret = sprintf(buf, "%s\n", g_debug_help);
	printk("current ethernet debug: %d\n", g_debug);

	return ret;
}

/* --------------------------------------------------------------------------*/
/**
 * @brief  eth_debug_store
 *
 * @param  class
 * @param  attr
 * @param  buf
 * @param  count
 *
 * @return
 */
/* --------------------------------------------------------------------------*/
static ssize_t eth_debug_store(struct class *class, struct class_attribute *attr, const char *buf, size_t count)
{
	unsigned int debug = 0;

	debug = simple_strtoul(buf, NULL, 0);
	if (debug >= 0 && debug < 6) {
		printk("ethernet debug: %d -> %d\n", g_debug, debug);
		g_debug = debug;
	} else {
		printk("set ethernet debug error\n");
	}

	return count;
}

/* --------------------------------------------------------------------------*/
/**
 * @brief  eth_count_show
 *
 * @param  class
 * @param  attr
 * @param  buf
 *
 * @return
 */
/* --------------------------------------------------------------------------*/
static ssize_t eth_count_show(struct class *class, struct class_attribute *attr, char *buf)
{
	printk("Ethernet TX count: %08d\n", g_tx_cnt);
	printk("Ethernet RX count: %08d\n", g_rx_cnt);

	return 0;
}

/* --------------------------------------------------------------------------*/
/**
 * @brief  eth_count_store
 *
 * @param  class
 * @param  attr
 * @param  buf
 * @param  count
 *
 * @return
 */
/* --------------------------------------------------------------------------*/
static ssize_t eth_count_store(struct class *class, struct class_attribute *attr, const char *buf, size_t count)
{
	unsigned int cnt = 0;

	cnt = simple_strtoul(buf, NULL, 0);
	if (cnt == 0) {
		printk("reset ethernet tx/rx count.\n");
		g_tx_cnt = 0;
		g_rx_cnt = 0;
	} else {
		printk("reset ethernet count error\n");
	}

	return count;
}

static const char *g_wol_help = {
	"Ethernet WOL:\n"
	"    0. disable WOL.\n"
	"    1. enable WOL, Magic Packet.\n"
	"    2. enable WOL, Wake-Up Frame.\n"
	"    3. enable WOL, Global Unicast.\n"
	"    4. enable WOL, support all.\n"
};
static ssize_t eth_wol_show(struct class *class, struct class_attribute *attr, char *buf)
{
	int ret;
	ret = sprintf(buf, "%s\n", g_wol_help);
	printk("Current WOL: %d\n", g_mac_pmt_enable);

	return ret;
}

static ssize_t eth_wol_store(struct class *class, struct class_attribute *attr, const char *buf, size_t count)
{
	unsigned int enable = 0;

	enable = simple_strtoul(buf, NULL, 0);
	if (enable >= 0 && enable <= 4) {
		g_mac_pmt_enable = enable;
		mac_pmt_enable(g_mac_pmt_enable);
		printk("Set Ethernet WOL: %d\n", g_mac_pmt_enable);
	} else {
		printk("Set Ethernet WOL Error\n");
	}

	return count;
}
static ssize_t eth_linkspeed_show(struct class *class, struct class_attribute *attr, char *buf)
{
	struct am_net_private *np = netdev_priv(my_ndev);
	struct phy_device *phydev = np->phydev;
	int ret;
	char buff[100];

	if(np->phydev) {
		phy_print_status(np->phydev);

		genphy_update_link(phydev);
		if (phydev->link)
			strcpy(buff,"link status: link\n");
		else
			strcpy(buff,"link status: unlink\n");
	} else
		strcpy(buff,"link status: unlink\n");

	ret = sprintf(buf, "%s\n", buff);

	return ret;
}
static const char *g_pwol_help = {
	"Ethernet WOL:\n"
	"    0. disable PHY WOL.\n"
	"    1. enable PHY  WOL, Magic Packet.\n"

};
static ssize_t eth_pwol_show(struct class *class, struct class_attribute *attr, char *buf)
{
	struct am_net_private *np = netdev_priv(my_ndev);
	int ret;
	struct ethtool_wolinfo syswol;
	syswol.supported = 0;
	syswol.wolopts = 0;
	if (np->phydev)
		phy_ethtool_get_wol(np->phydev, &syswol);
	ret = sprintf(buf, "%s\n", g_pwol_help);
	printk("Current PHY WOL: %d\n", syswol.wolopts);
	return ret;
}
static ssize_t eth_pwol_store(struct class *class, struct class_attribute *attr, const char *buf, size_t count)
{
	struct am_net_private *np = netdev_priv(my_ndev);
	int err;
	unsigned int enable = -1;
	struct ethtool_wolinfo syswol;
	if (np->phydev == NULL)
		return -EOPNOTSUPP;
	enable = simple_strtoul(buf, NULL, 0);
	if((enable >=0) && (enable <2)){
	switch(enable){
		case 0:
			// close wol funtion
			 syswol.wolopts = WAKE_PHY;
			 err = phy_ethtool_set_wol(np->phydev, &syswol);
				return count;
		case 1:
			//open wol funtion
			syswol.wolopts = WAKE_MAGIC;
			printk("Set PHY i11WOL: %d\n,WAKE_MAGIC = %d\n",syswol.wolopts,WAKE_MAGIC);
			 phy_ethtool_set_wol(np->phydev, &syswol);
			printk("Set PHY WOL: %d\n", enable);
				return count;
		default:
			return count;
	}
	}
	else{
			printk("Set Ethernet WOL Error\n");
			return count;
	}
	return count;
}
#if(MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8)

static int am_net_cali(int argc, char **argv,int gate)
{
	int cali_rise = 0;
	int cali_sel = 0;
	int cali_start = 0;
	int cali_time = 0;
	int ii=0;
	unsigned int value;
	cali_start = gate;
	if ((argc < 4) || (argv == NULL) || (argv[0] == NULL)
			|| (argv[1] == NULL) || (argv[2] == NULL)|| (argv[3] == NULL)) {
		printk("Invalid syntax\n");
		return -1;
	}
	cali_rise = simple_strtol(argv[1], NULL, 0);
	cali_sel = simple_strtol(argv[2], NULL, 0);
	cali_time = simple_strtol(argv[3], NULL, 0);
	aml_write_reg32(P_PREG_ETH_REG0,aml_read_reg32(P_PREG_ETH_REG0)&(~(0x1f << 25)));
	aml_write_reg32(P_PREG_ETH_REG0,aml_read_reg32(P_PREG_ETH_REG0)|(cali_start << 25)|(cali_rise << 26)|(cali_sel << 27));
	printk("rise :%d   sel: %d  time: %d   start:%d  cbus2050 = %x\n",cali_rise,cali_sel,cali_time,cali_start,aml_read_reg32(P_PREG_ETH_REG0));
	for(ii=0;ii < cali_time;ii++){
		value = aml_read_reg32(P_PREG_ETH_REG1);
		if((value>>15) & 0x1){
 			printk("value == %x,  cali_len == %d, cali_idx == %d,  cali_sel =%d,  cali_rise = %d\n",value,(value>>5)&0x1f,(value&0x1f),(value>>11)&0x7,(value>>14)&0x1);
		}
	}
	return 0;
}
static ssize_t eth_cali_store(struct class *class, struct class_attribute *attr,
		const char *buf, size_t count)
{
	int argc;
	char *buff, *p, *para;
	char *argv[5];
	char cmd;

	buff = kstrdup(buf, GFP_KERNEL);
	p = buff;
	if(IS_MESON_M8_CPU){
		printk("Sorry ,this cpu is not support cali!\n");
		goto end;
	}
	for (argc = 0; argc < 6; argc++) {
		para = strsep(&p, " ");
		if (para == NULL)
			break;
		argv[argc] = para;
	}
	if (argc < 1 || argc > 4)
		goto end;

	cmd = argv[0][0];
		switch (cmd) {
		case 'e':
		case 'E':
			am_net_cali(argc, argv,1);
			break;
		case 'd':
		case 'D':
			am_net_cali(argc, argv,0);
			break;

		default:
			goto end;
		}
	
		return count;
	
	end:
		kfree(buff);
		return 0;

}
#endif

/* --------------------------------------------------------------------------*/
static struct class *eth_sys_class;
static CLASS_ATTR(mdcclk, S_IWUSR | S_IRUGO, eth_mdcclk_show, eth_mdcclk_store);
static CLASS_ATTR(debug, S_IWUSR | S_IRUGO, eth_debug_show, eth_debug_store);
static CLASS_ATTR(count, S_IWUSR | S_IRUGO, eth_count_show, eth_count_store);
static CLASS_ATTR(phyreg, S_IWUSR | S_IRUGO, eth_phyreg_help, eth_phyreg_func);
static CLASS_ATTR(macreg, S_IWUSR | S_IRUGO, eth_macreg_help, eth_macreg_func);
static CLASS_ATTR(wol, S_IWUSR | S_IRUGO, eth_wol_show, eth_wol_store);
static CLASS_ATTR(pwol, S_IWUSR | S_IRUGO, eth_pwol_show, eth_pwol_store);
static CLASS_ATTR(linkspeed, S_IWUSR | S_IRUGO, eth_linkspeed_show, NULL);
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
static CLASS_ATTR(cali, S_IWUSR | S_IRUGO, NULL,eth_cali_store);
#endif

/* --------------------------------------------------------------------------*/
/**
 * @brief  am_eth_class_init
 *
 * @return
 */
/* --------------------------------------------------------------------------*/
static int __init am_eth_class_init(void)
{
	int ret = 0;

	eth_sys_class = class_create(THIS_MODULE, DRIVER_NAME);
	ret = class_create_file(eth_sys_class, &class_attr_mdcclk);
	ret = class_create_file(eth_sys_class, &class_attr_debug);
	ret = class_create_file(eth_sys_class, &class_attr_count);
	ret = class_create_file(eth_sys_class, &class_attr_phyreg);
	ret = class_create_file(eth_sys_class, &class_attr_macreg);
	ret = class_create_file(eth_sys_class, &class_attr_wol);
	ret = class_create_file(eth_sys_class, &class_attr_pwol);
	ret = class_create_file(eth_sys_class, &class_attr_linkspeed);
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
	ret = class_create_file(eth_sys_class, &class_attr_cali);
#endif

	return ret;
}
#define OWNER_NAME "meson-eth"
void hardware_reset_phy(void){
	if(reset_pin_enable){
		amlogic_gpio_direction_output(reset_pin_num, 0, OWNER_NAME);
		mdelay(reset_delay);
		amlogic_gpio_direction_output(reset_pin_num, 1, OWNER_NAME);
	}
}
#ifdef CONFIG_HAS_EARLYSUSPEND
static int ethernet_early_suspend(struct early_suspend *dev)
{
	printk("ethernet_early_suspend!\n");
	netdev_close(my_ndev);
	return 0;
}
static int ethernet_late_resume(struct early_suspend *dev)
{
	int res = 0;
	printk("ethernet_late_resume()\n");
	hardware_reset_phy();
	res = netdev_open(my_ndev);
	if (res != 0) {
		printk("nono, it can not be true!\n");
	}

	return 0;
}
#endif
/* --------------------------------------------------------------------------*/
/**
 * @brief ethernet_probe
 *
 * @param pdev
 *
 * @return
 */
/* --------------------------------------------------------------------------*/
static int ethernet_probe(struct platform_device *pdev)
{
        int ret;
	int res;
	struct am_net_private *np=NULL;
	printk("ethernet_driver probe!\n");
#ifdef CONFIG_OF
	if (!pdev->dev.of_node) {
		printk("eth: pdev->dev.of_node == NULL!\n");
		return -1;
	}
	ret = of_property_read_u32(pdev->dev.of_node,"ethbaseaddr",&ethbaseaddr);
	if (ret) {
		printk("Please config ethbaseaddr.\n");
		return -1;
	}
	ret = of_property_read_u32(pdev->dev.of_node,"interruptnum",&interruptnum);
	if (ret) {
		printk("Please config interruptnum.\n");
		return -1;
	}
	ret = of_property_read_u32(pdev->dev.of_node,"phy_interface",&phy_interface); // 0 rgmii 1: RMII
	if (ret) {
		printk("Please config phy  interface.\n");
	}
	ret = of_property_read_u32(pdev->dev.of_node,"savepowermode",&savepowermode);
	if (ret) {
		printk("Please config savepowermode.\n");
	}
	ret = of_property_read_u32(pdev->dev.of_node,"reset_pin_enable",&reset_pin_enable);
	if (ret) {
		printk("Please config reset_pin_enable.\n");
	}
	ret = of_property_read_u32(pdev->dev.of_node,"reset_delay",&reset_delay);
	if (ret) {
		printk("Please config reset_delay.\n");
	}
	ret = of_property_read_string(pdev->dev.of_node,"reset_pin",&reset_pin);
	if (ret) {
		printk("Please config reset_pin.\n");
	}
	ret = of_property_read_u32(pdev->dev.of_node,"new_maclogic",&new_maclogic);
	if (ret) {
		printk("Please config new_maclogic.\n");
	}
	if(reset_pin_enable){
		reset_pin_num = amlogic_gpio_name_map_num(reset_pin);
		amlogic_gpio_request(reset_pin_num, OWNER_NAME);
	}

#endif
	printk(DRV_NAME "init(dbg[%p]=%d)\n", (&g_debug), g_debug);
	switch_mod_gate_by_name("ethernet",1);
	my_ndev = alloc_etherdev(sizeof(struct am_net_private));
	if (my_ndev == NULL) {
		printk(DRV_NAME "ndev alloc failed!!\n");
		return -ENOMEM;
	}
#ifdef CONFIG_HAS_EARLYSUSPEND
       early_suspend.level = EARLY_SUSPEND_LEVEL_STOP_DRAWING;
       early_suspend.suspend = ethernet_early_suspend;
       early_suspend.resume = ethernet_late_resume;
       register_early_suspend(&early_suspend);
#endif
	SET_NETDEV_DEV(my_ndev, &pdev->dev);
	res = probe_init(my_ndev);
	if (res != 0)
		free_netdev(my_ndev);
	else
		res = am_eth_class_init();

	eth_pdata = (struct aml_eth_platdata *)pdev->dev.platform_data;
	np = netdev_priv(my_ndev);
	if(np->phydev && savepowermode)
		np->phydev->drv->suspend(np->phydev);
	//switch_mod_gate_by_name("ethernet",0);

	//if (!eth_pdata) {
	//	printk("\nethernet pm ops resource undefined.\n");
	//	return -EFAULT;
	//}

	return 0;
}

/* --------------------------------------------------------------------------*/
/**
 * @brief ethernet_remove
 *
 * @param pdev
 *
 * @return
 */
/* --------------------------------------------------------------------------*/
static int ethernet_remove(struct platform_device *pdev)
{
	printk("ethernet_driver remove!\n");
#ifdef CONFIG_HAS_EARLYSUSPEND
	 unregister_early_suspend(&early_suspend);
#endif
	switch_mod_gate_by_name("ethernet",0);
	return 0;
}

/* --------------------------------------------------------------------------*/
/**
 * @brief ethernet_suspend
 *
 * @param dev
 * @param event
 *
 * @return
 */
/* --------------------------------------------------------------------------*/
#ifndef  CONFIG_PM
static int ethernet_suspend(struct platform_device *dev, pm_message_t event)
{
	printk("ethernet_suspend!\n");
	netdev_close(my_ndev);	
	return 0;
}
#endif

/* --------------------------------------------------------------------------*/
/**
 * @brief ethernet_resume
 *
 * @param dev
 *
 * @return
 */
/* --------------------------------------------------------------------------*/
#if 0
static int ethernet_resume(struct platform_device *dev)
{
	int res = 0;
	printk("ethernet_resume()\n");
	hardware_reset_phy();
	res = netdev_open(my_ndev);
	if (res != 0) {
		printk("nono, it can not be true!\n");
	}

	return 0;
}
#endif
#ifdef CONFIG_OF
static const struct of_device_id eth_dt_match[]={
	{	.compatible 	= "amlogic,meson-eth",
	},
	{},
};
#else
#define eth_dt_match NULL
#endif

static struct platform_driver ethernet_driver = {
	.probe   = ethernet_probe,
	.remove  = ethernet_remove,
#ifndef  CONFIG_PM
	.suspend = ethernet_suspend,
	.resume  = ethernet_resume,
#endif
	.driver  = {
		.name = "meson-eth",
		.of_match_table = eth_dt_match,
	}
};




/* --------------------------------------------------------------------------*/
/**
 * @brief  am_net_init
 *
 * @return
 */
/* --------------------------------------------------------------------------*/
static int __init am_net_init(void)
{
	if (platform_driver_register(&ethernet_driver)) {
		printk("failed to register ethernet_pm driver\n");
		g_ethernet_registered = 0;
	} else {
		g_ethernet_registered = 1;
	}

	return 0;
}

/* --------------------------------------------------------------------------*/
/**
 * @brief  am_net_free
 *
 * @param  ndev
 */
/* --------------------------------------------------------------------------*/
static void am_net_free(struct net_device *ndev)
{
	netdev_close(ndev);
	unregister_netdev(ndev);
}

/* --------------------------------------------------------------------------*/
/**
 * @brief  am_net_exit
 *
 * @return
 */
/* --------------------------------------------------------------------------*/
static void __exit am_net_exit(void)
{
	printk(DRV_NAME "exit\n");

	am_net_free(my_ndev);
	free_netdev(my_ndev);
	aml_mdio_unregister(my_ndev);
	class_destroy(eth_sys_class);
	if (g_ethernet_registered == 1) {
		printk("ethernet_pm driver remove.\n");
		platform_driver_unregister(&ethernet_driver);
		g_ethernet_registered = 0;
	}
	return;
}

module_init(am_net_init);
module_exit(am_net_exit);



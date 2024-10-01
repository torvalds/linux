// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PXA168 ethernet driver.
 * Most of the code is derived from mv643xx ethernet driver.
 *
 * Copyright (C) 2010 Marvell International Ltd.
 *		Sachin Sanap <ssanap@marvell.com>
 *		Zhangfei Gao <zgao6@marvell.com>
 *		Philip Rakity <prakity@marvell.com>
 *		Mark Brown <markb@marvell.com>
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/in.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ip.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_net.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/pxa168_eth.h>
#include <linux/tcp.h>
#include <linux/types.h>
#include <linux/udp.h>
#include <linux/workqueue.h>
#include <linux/pgtable.h>

#include <asm/cacheflush.h>

#define DRIVER_NAME	"pxa168-eth"
#define DRIVER_VERSION	"0.3"

/*
 * Registers
 */

#define PHY_ADDRESS		0x0000
#define SMI			0x0010
#define PORT_CONFIG		0x0400
#define PORT_CONFIG_EXT		0x0408
#define PORT_COMMAND		0x0410
#define PORT_STATUS		0x0418
#define HTPR			0x0428
#define MAC_ADDR_LOW		0x0430
#define MAC_ADDR_HIGH		0x0438
#define SDMA_CONFIG		0x0440
#define SDMA_CMD		0x0448
#define INT_CAUSE		0x0450
#define INT_W_CLEAR		0x0454
#define INT_MASK		0x0458
#define ETH_F_RX_DESC_0		0x0480
#define ETH_C_RX_DESC_0		0x04A0
#define ETH_C_TX_DESC_1		0x04E4

/* smi register */
#define SMI_BUSY		(1 << 28)	/* 0 - Write, 1 - Read  */
#define SMI_R_VALID		(1 << 27)	/* 0 - Write, 1 - Read  */
#define SMI_OP_W		(0 << 26)	/* Write operation      */
#define SMI_OP_R		(1 << 26)	/* Read operation */

#define PHY_WAIT_ITERATIONS	10

#define PXA168_ETH_PHY_ADDR_DEFAULT	0
/* RX & TX descriptor command */
#define BUF_OWNED_BY_DMA	(1 << 31)

/* RX descriptor status */
#define RX_EN_INT		(1 << 23)
#define RX_FIRST_DESC		(1 << 17)
#define RX_LAST_DESC		(1 << 16)
#define RX_ERROR		(1 << 15)

/* TX descriptor command */
#define TX_EN_INT		(1 << 23)
#define TX_GEN_CRC		(1 << 22)
#define TX_ZERO_PADDING		(1 << 18)
#define TX_FIRST_DESC		(1 << 17)
#define TX_LAST_DESC		(1 << 16)
#define TX_ERROR		(1 << 15)

/* SDMA_CMD */
#define SDMA_CMD_AT		(1 << 31)
#define SDMA_CMD_TXDL		(1 << 24)
#define SDMA_CMD_TXDH		(1 << 23)
#define SDMA_CMD_AR		(1 << 15)
#define SDMA_CMD_ERD		(1 << 7)

/* Bit definitions of the Port Config Reg */
#define PCR_DUPLEX_FULL		(1 << 15)
#define PCR_HS			(1 << 12)
#define PCR_EN			(1 << 7)
#define PCR_PM			(1 << 0)

/* Bit definitions of the Port Config Extend Reg */
#define PCXR_2BSM		(1 << 28)
#define PCXR_DSCP_EN		(1 << 21)
#define PCXR_RMII_EN		(1 << 20)
#define PCXR_AN_SPEED_DIS	(1 << 19)
#define PCXR_SPEED_100		(1 << 18)
#define PCXR_MFL_1518		(0 << 14)
#define PCXR_MFL_1536		(1 << 14)
#define PCXR_MFL_2048		(2 << 14)
#define PCXR_MFL_64K		(3 << 14)
#define PCXR_FLOWCTL_DIS	(1 << 12)
#define PCXR_FLP		(1 << 11)
#define PCXR_AN_FLOWCTL_DIS	(1 << 10)
#define PCXR_AN_DUPLEX_DIS	(1 << 9)
#define PCXR_PRIO_TX_OFF	3
#define PCXR_TX_HIGH_PRI	(7 << PCXR_PRIO_TX_OFF)

/* Bit definitions of the SDMA Config Reg */
#define SDCR_BSZ_OFF		12
#define SDCR_BSZ8		(3 << SDCR_BSZ_OFF)
#define SDCR_BSZ4		(2 << SDCR_BSZ_OFF)
#define SDCR_BSZ2		(1 << SDCR_BSZ_OFF)
#define SDCR_BSZ1		(0 << SDCR_BSZ_OFF)
#define SDCR_BLMR		(1 << 6)
#define SDCR_BLMT		(1 << 7)
#define SDCR_RIFB		(1 << 9)
#define SDCR_RC_OFF		2
#define SDCR_RC_MAX_RETRANS	(0xf << SDCR_RC_OFF)

/*
 * Bit definitions of the Interrupt Cause Reg
 * and Interrupt MASK Reg is the same
 */
#define ICR_RXBUF		(1 << 0)
#define ICR_TXBUF_H		(1 << 2)
#define ICR_TXBUF_L		(1 << 3)
#define ICR_TXEND_H		(1 << 6)
#define ICR_TXEND_L		(1 << 7)
#define ICR_RXERR		(1 << 8)
#define ICR_TXERR_H		(1 << 10)
#define ICR_TXERR_L		(1 << 11)
#define ICR_TX_UDR		(1 << 13)
#define ICR_MII_CH		(1 << 28)

#define ALL_INTS (ICR_TXBUF_H  | ICR_TXBUF_L  | ICR_TX_UDR |\
				ICR_TXERR_H  | ICR_TXERR_L |\
				ICR_TXEND_H  | ICR_TXEND_L |\
				ICR_RXBUF | ICR_RXERR  | ICR_MII_CH)

#define ETH_HW_IP_ALIGN		2	/* hw aligns IP header */

#define NUM_RX_DESCS		64
#define NUM_TX_DESCS		64

#define HASH_ADD		0
#define HASH_DELETE		1
#define HASH_ADDR_TABLE_SIZE	0x4000	/* 16K (1/2K address - PCR_HS == 1) */
#define HOP_NUMBER		12

/* Bit definitions for Port status */
#define PORT_SPEED_100		(1 << 0)
#define FULL_DUPLEX		(1 << 1)
#define FLOW_CONTROL_DISABLED	(1 << 2)
#define LINK_UP			(1 << 3)

/* Bit definitions for work to be done */
#define WORK_TX_DONE		(1 << 1)

/*
 * Misc definitions.
 */
#define SKB_DMA_REALIGN		((PAGE_SIZE - NET_SKB_PAD) % SMP_CACHE_BYTES)

struct rx_desc {
	u32 cmd_sts;		/* Descriptor command status            */
	u16 byte_cnt;		/* Descriptor buffer byte count         */
	u16 buf_size;		/* Buffer size                          */
	u32 buf_ptr;		/* Descriptor buffer pointer            */
	u32 next_desc_ptr;	/* Next descriptor pointer              */
};

struct tx_desc {
	u32 cmd_sts;		/* Command/status field                 */
	u16 reserved;
	u16 byte_cnt;		/* buffer byte count                    */
	u32 buf_ptr;		/* pointer to buffer for this descriptor */
	u32 next_desc_ptr;	/* Pointer to next descriptor           */
};

struct pxa168_eth_private {
	struct platform_device *pdev;
	int port_num;		/* User Ethernet port number    */
	int phy_addr;
	int phy_speed;
	int phy_duplex;
	phy_interface_t phy_intf;

	int rx_resource_err;	/* Rx ring resource error flag */

	/* Next available and first returning Rx resource */
	int rx_curr_desc_q, rx_used_desc_q;

	/* Next available and first returning Tx resource */
	int tx_curr_desc_q, tx_used_desc_q;

	struct rx_desc *p_rx_desc_area;
	dma_addr_t rx_desc_dma;
	int rx_desc_area_size;
	struct sk_buff **rx_skb;

	struct tx_desc *p_tx_desc_area;
	dma_addr_t tx_desc_dma;
	int tx_desc_area_size;
	struct sk_buff **tx_skb;

	struct work_struct tx_timeout_task;

	struct net_device *dev;
	struct napi_struct napi;
	u8 work_todo;
	int skb_size;

	/* Size of Tx Ring per queue */
	int tx_ring_size;
	/* Number of tx descriptors in use */
	int tx_desc_count;
	/* Size of Rx Ring per queue */
	int rx_ring_size;
	/* Number of rx descriptors in use */
	int rx_desc_count;

	/*
	 * Used in case RX Ring is empty, which can occur when
	 * system does not have resources (skb's)
	 */
	struct timer_list timeout;
	struct mii_bus *smi_bus;

	/* clock */
	struct clk *clk;
	struct pxa168_eth_platform_data *pd;
	/*
	 * Ethernet controller base address.
	 */
	void __iomem *base;

	/* Pointer to the hardware address filter table */
	void *htpr;
	dma_addr_t htpr_dma;
};

struct addr_table_entry {
	__le32 lo;
	__le32 hi;
};

/* Bit fields of a Hash Table Entry */
enum hash_table_entry {
	HASH_ENTRY_VALID = 1,
	SKIP = 2,
	HASH_ENTRY_RECEIVE_DISCARD = 4,
	HASH_ENTRY_RECEIVE_DISCARD_BIT = 2
};

static int pxa168_init_hw(struct pxa168_eth_private *pep);
static int pxa168_init_phy(struct net_device *dev);
static void eth_port_reset(struct net_device *dev);
static void eth_port_start(struct net_device *dev);
static int pxa168_eth_open(struct net_device *dev);
static int pxa168_eth_stop(struct net_device *dev);

static inline u32 rdl(struct pxa168_eth_private *pep, int offset)
{
	return readl_relaxed(pep->base + offset);
}

static inline void wrl(struct pxa168_eth_private *pep, int offset, u32 data)
{
	writel_relaxed(data, pep->base + offset);
}

static void abort_dma(struct pxa168_eth_private *pep)
{
	int delay;
	int max_retries = 40;

	do {
		wrl(pep, SDMA_CMD, SDMA_CMD_AR | SDMA_CMD_AT);
		udelay(100);

		delay = 10;
		while ((rdl(pep, SDMA_CMD) & (SDMA_CMD_AR | SDMA_CMD_AT))
		       && delay-- > 0) {
			udelay(10);
		}
	} while (max_retries-- > 0 && delay <= 0);

	if (max_retries <= 0)
		netdev_err(pep->dev, "%s : DMA Stuck\n", __func__);
}

static void rxq_refill(struct net_device *dev)
{
	struct pxa168_eth_private *pep = netdev_priv(dev);
	struct sk_buff *skb;
	struct rx_desc *p_used_rx_desc;
	int used_rx_desc;

	while (pep->rx_desc_count < pep->rx_ring_size) {
		int size;

		skb = netdev_alloc_skb(dev, pep->skb_size);
		if (!skb)
			break;
		if (SKB_DMA_REALIGN)
			skb_reserve(skb, SKB_DMA_REALIGN);
		pep->rx_desc_count++;
		/* Get 'used' Rx descriptor */
		used_rx_desc = pep->rx_used_desc_q;
		p_used_rx_desc = &pep->p_rx_desc_area[used_rx_desc];
		size = skb_end_pointer(skb) - skb->data;
		p_used_rx_desc->buf_ptr = dma_map_single(&pep->pdev->dev,
							 skb->data,
							 size,
							 DMA_FROM_DEVICE);
		p_used_rx_desc->buf_size = size;
		pep->rx_skb[used_rx_desc] = skb;

		/* Return the descriptor to DMA ownership */
		dma_wmb();
		p_used_rx_desc->cmd_sts = BUF_OWNED_BY_DMA | RX_EN_INT;
		dma_wmb();

		/* Move the used descriptor pointer to the next descriptor */
		pep->rx_used_desc_q = (used_rx_desc + 1) % pep->rx_ring_size;

		/* Any Rx return cancels the Rx resource error status */
		pep->rx_resource_err = 0;

		skb_reserve(skb, ETH_HW_IP_ALIGN);
	}

	/*
	 * If RX ring is empty of SKB, set a timer to try allocating
	 * again at a later time.
	 */
	if (pep->rx_desc_count == 0) {
		pep->timeout.expires = jiffies + (HZ / 10);
		add_timer(&pep->timeout);
	}
}

static inline void rxq_refill_timer_wrapper(struct timer_list *t)
{
	struct pxa168_eth_private *pep = from_timer(pep, t, timeout);
	napi_schedule(&pep->napi);
}

static inline u8 flip_8_bits(u8 x)
{
	return (((x) & 0x01) << 3) | (((x) & 0x02) << 1)
	    | (((x) & 0x04) >> 1) | (((x) & 0x08) >> 3)
	    | (((x) & 0x10) << 3) | (((x) & 0x20) << 1)
	    | (((x) & 0x40) >> 1) | (((x) & 0x80) >> 3);
}

static void nibble_swap_every_byte(unsigned char *mac_addr)
{
	int i;
	for (i = 0; i < ETH_ALEN; i++) {
		mac_addr[i] = ((mac_addr[i] & 0x0f) << 4) |
				((mac_addr[i] & 0xf0) >> 4);
	}
}

static void inverse_every_nibble(unsigned char *mac_addr)
{
	int i;
	for (i = 0; i < ETH_ALEN; i++)
		mac_addr[i] = flip_8_bits(mac_addr[i]);
}

/*
 * ----------------------------------------------------------------------------
 * This function will calculate the hash function of the address.
 * Inputs
 * mac_addr_orig    - MAC address.
 * Outputs
 * return the calculated entry.
 */
static u32 hash_function(const unsigned char *mac_addr_orig)
{
	u32 hash_result;
	u32 addr0;
	u32 addr1;
	u32 addr2;
	u32 addr3;
	unsigned char mac_addr[ETH_ALEN];

	/* Make a copy of MAC address since we are going to performe bit
	 * operations on it
	 */
	memcpy(mac_addr, mac_addr_orig, ETH_ALEN);

	nibble_swap_every_byte(mac_addr);
	inverse_every_nibble(mac_addr);

	addr0 = (mac_addr[5] >> 2) & 0x3f;
	addr1 = (mac_addr[5] & 0x03) | (((mac_addr[4] & 0x7f)) << 2);
	addr2 = ((mac_addr[4] & 0x80) >> 7) | mac_addr[3] << 1;
	addr3 = (mac_addr[2] & 0xff) | ((mac_addr[1] & 1) << 8);

	hash_result = (addr0 << 9) | (addr1 ^ addr2 ^ addr3);
	hash_result = hash_result & 0x07ff;
	return hash_result;
}

/*
 * ----------------------------------------------------------------------------
 * This function will add/del an entry to the address table.
 * Inputs
 * pep - ETHERNET .
 * mac_addr - MAC address.
 * skip - if 1, skip this address.Used in case of deleting an entry which is a
 *	  part of chain in the hash table.We can't just delete the entry since
 *	  that will break the chain.We need to defragment the tables time to
 *	  time.
 * rd   - 0 Discard packet upon match.
 *	- 1 Receive packet upon match.
 * Outputs
 * address table entry is added/deleted.
 * 0 if success.
 * -ENOSPC if table full
 */
static int add_del_hash_entry(struct pxa168_eth_private *pep,
			      const unsigned char *mac_addr,
			      u32 rd, u32 skip, int del)
{
	struct addr_table_entry *entry, *start;
	u32 new_high;
	u32 new_low;
	u32 i;

	new_low = (((mac_addr[1] >> 4) & 0xf) << 15)
	    | (((mac_addr[1] >> 0) & 0xf) << 11)
	    | (((mac_addr[0] >> 4) & 0xf) << 7)
	    | (((mac_addr[0] >> 0) & 0xf) << 3)
	    | (((mac_addr[3] >> 4) & 0x1) << 31)
	    | (((mac_addr[3] >> 0) & 0xf) << 27)
	    | (((mac_addr[2] >> 4) & 0xf) << 23)
	    | (((mac_addr[2] >> 0) & 0xf) << 19)
	    | (skip << SKIP) | (rd << HASH_ENTRY_RECEIVE_DISCARD_BIT)
	    | HASH_ENTRY_VALID;

	new_high = (((mac_addr[5] >> 4) & 0xf) << 15)
	    | (((mac_addr[5] >> 0) & 0xf) << 11)
	    | (((mac_addr[4] >> 4) & 0xf) << 7)
	    | (((mac_addr[4] >> 0) & 0xf) << 3)
	    | (((mac_addr[3] >> 5) & 0x7) << 0);

	/*
	 * Pick the appropriate table, start scanning for free/reusable
	 * entries at the index obtained by hashing the specified MAC address
	 */
	start = pep->htpr;
	entry = start + hash_function(mac_addr);
	for (i = 0; i < HOP_NUMBER; i++) {
		if (!(le32_to_cpu(entry->lo) & HASH_ENTRY_VALID)) {
			break;
		} else {
			/* if same address put in same position */
			if (((le32_to_cpu(entry->lo) & 0xfffffff8) ==
				(new_low & 0xfffffff8)) &&
				(le32_to_cpu(entry->hi) == new_high)) {
				break;
			}
		}
		if (entry == start + 0x7ff)
			entry = start;
		else
			entry++;
	}

	if (((le32_to_cpu(entry->lo) & 0xfffffff8) != (new_low & 0xfffffff8)) &&
	    (le32_to_cpu(entry->hi) != new_high) && del)
		return 0;

	if (i == HOP_NUMBER) {
		if (!del) {
			netdev_info(pep->dev,
				    "%s: table section is full, need to "
				    "move to 16kB implementation?\n",
				    __FILE__);
			return -ENOSPC;
		} else
			return 0;
	}

	/*
	 * Update the selected entry
	 */
	if (del) {
		entry->hi = 0;
		entry->lo = 0;
	} else {
		entry->hi = cpu_to_le32(new_high);
		entry->lo = cpu_to_le32(new_low);
	}

	return 0;
}

/*
 * ----------------------------------------------------------------------------
 *  Create an addressTable entry from MAC address info
 *  found in the specifed net_device struct
 *
 *  Input : pointer to ethernet interface network device structure
 *  Output : N/A
 */
static void update_hash_table_mac_address(struct pxa168_eth_private *pep,
					  unsigned char *oaddr,
					  const unsigned char *addr)
{
	/* Delete old entry */
	if (oaddr)
		add_del_hash_entry(pep, oaddr, 1, 0, HASH_DELETE);
	/* Add new entry */
	add_del_hash_entry(pep, addr, 1, 0, HASH_ADD);
}

static int init_hash_table(struct pxa168_eth_private *pep)
{
	/*
	 * Hardware expects CPU to build a hash table based on a predefined
	 * hash function and populate it based on hardware address. The
	 * location of the hash table is identified by 32-bit pointer stored
	 * in HTPR internal register. Two possible sizes exists for the hash
	 * table 8kB (256kB of DRAM required (4 x 64 kB banks)) and 1/2kB
	 * (16kB of DRAM required (4 x 4 kB banks)).We currently only support
	 * 1/2kB.
	 */
	/* TODO: Add support for 8kB hash table and alternative hash
	 * function.Driver can dynamically switch to them if the 1/2kB hash
	 * table is full.
	 */
	if (!pep->htpr) {
		pep->htpr = dma_alloc_coherent(pep->dev->dev.parent,
					       HASH_ADDR_TABLE_SIZE,
					       &pep->htpr_dma, GFP_KERNEL);
		if (!pep->htpr)
			return -ENOMEM;
	} else {
		memset(pep->htpr, 0, HASH_ADDR_TABLE_SIZE);
	}
	wrl(pep, HTPR, pep->htpr_dma);
	return 0;
}

static void pxa168_eth_set_rx_mode(struct net_device *dev)
{
	struct pxa168_eth_private *pep = netdev_priv(dev);
	struct netdev_hw_addr *ha;
	u32 val;

	val = rdl(pep, PORT_CONFIG);
	if (dev->flags & IFF_PROMISC)
		val |= PCR_PM;
	else
		val &= ~PCR_PM;
	wrl(pep, PORT_CONFIG, val);

	/*
	 * Remove the old list of MAC address and add dev->addr
	 * and multicast address.
	 */
	memset(pep->htpr, 0, HASH_ADDR_TABLE_SIZE);
	update_hash_table_mac_address(pep, NULL, dev->dev_addr);

	netdev_for_each_mc_addr(ha, dev)
		update_hash_table_mac_address(pep, NULL, ha->addr);
}

static void pxa168_eth_get_mac_address(struct net_device *dev,
				       unsigned char *addr)
{
	struct pxa168_eth_private *pep = netdev_priv(dev);
	unsigned int mac_h = rdl(pep, MAC_ADDR_HIGH);
	unsigned int mac_l = rdl(pep, MAC_ADDR_LOW);

	addr[0] = (mac_h >> 24) & 0xff;
	addr[1] = (mac_h >> 16) & 0xff;
	addr[2] = (mac_h >> 8) & 0xff;
	addr[3] = mac_h & 0xff;
	addr[4] = (mac_l >> 8) & 0xff;
	addr[5] = mac_l & 0xff;
}

static int pxa168_eth_set_mac_address(struct net_device *dev, void *addr)
{
	struct sockaddr *sa = addr;
	struct pxa168_eth_private *pep = netdev_priv(dev);
	unsigned char oldMac[ETH_ALEN];
	u32 mac_h, mac_l;

	if (!is_valid_ether_addr(sa->sa_data))
		return -EADDRNOTAVAIL;
	memcpy(oldMac, dev->dev_addr, ETH_ALEN);
	eth_hw_addr_set(dev, sa->sa_data);

	mac_h = dev->dev_addr[0] << 24;
	mac_h |= dev->dev_addr[1] << 16;
	mac_h |= dev->dev_addr[2] << 8;
	mac_h |= dev->dev_addr[3];
	mac_l = dev->dev_addr[4] << 8;
	mac_l |= dev->dev_addr[5];
	wrl(pep, MAC_ADDR_HIGH, mac_h);
	wrl(pep, MAC_ADDR_LOW, mac_l);

	netif_addr_lock_bh(dev);
	update_hash_table_mac_address(pep, oldMac, dev->dev_addr);
	netif_addr_unlock_bh(dev);
	return 0;
}

static void eth_port_start(struct net_device *dev)
{
	unsigned int val = 0;
	struct pxa168_eth_private *pep = netdev_priv(dev);
	int tx_curr_desc, rx_curr_desc;

	phy_start(dev->phydev);

	/* Assignment of Tx CTRP of given queue */
	tx_curr_desc = pep->tx_curr_desc_q;
	wrl(pep, ETH_C_TX_DESC_1,
	    (u32) (pep->tx_desc_dma + tx_curr_desc * sizeof(struct tx_desc)));

	/* Assignment of Rx CRDP of given queue */
	rx_curr_desc = pep->rx_curr_desc_q;
	wrl(pep, ETH_C_RX_DESC_0,
	    (u32) (pep->rx_desc_dma + rx_curr_desc * sizeof(struct rx_desc)));

	wrl(pep, ETH_F_RX_DESC_0,
	    (u32) (pep->rx_desc_dma + rx_curr_desc * sizeof(struct rx_desc)));

	/* Clear all interrupts */
	wrl(pep, INT_CAUSE, 0);

	/* Enable all interrupts for receive, transmit and error. */
	wrl(pep, INT_MASK, ALL_INTS);

	val = rdl(pep, PORT_CONFIG);
	val |= PCR_EN;
	wrl(pep, PORT_CONFIG, val);

	/* Start RX DMA engine */
	val = rdl(pep, SDMA_CMD);
	val |= SDMA_CMD_ERD;
	wrl(pep, SDMA_CMD, val);
}

static void eth_port_reset(struct net_device *dev)
{
	struct pxa168_eth_private *pep = netdev_priv(dev);
	unsigned int val = 0;

	/* Stop all interrupts for receive, transmit and error. */
	wrl(pep, INT_MASK, 0);

	/* Clear all interrupts */
	wrl(pep, INT_CAUSE, 0);

	/* Stop RX DMA */
	val = rdl(pep, SDMA_CMD);
	val &= ~SDMA_CMD_ERD;	/* abort dma command */

	/* Abort any transmit and receive operations and put DMA
	 * in idle state.
	 */
	abort_dma(pep);

	/* Disable port */
	val = rdl(pep, PORT_CONFIG);
	val &= ~PCR_EN;
	wrl(pep, PORT_CONFIG, val);

	phy_stop(dev->phydev);
}

/*
 * txq_reclaim - Free the tx desc data for completed descriptors
 * If force is non-zero, frees uncompleted descriptors as well
 */
static int txq_reclaim(struct net_device *dev, int force)
{
	struct pxa168_eth_private *pep = netdev_priv(dev);
	struct tx_desc *desc;
	u32 cmd_sts;
	struct sk_buff *skb;
	int tx_index;
	dma_addr_t addr;
	int count;
	int released = 0;

	netif_tx_lock(dev);

	pep->work_todo &= ~WORK_TX_DONE;
	while (pep->tx_desc_count > 0) {
		tx_index = pep->tx_used_desc_q;
		desc = &pep->p_tx_desc_area[tx_index];
		cmd_sts = desc->cmd_sts;
		if (!force && (cmd_sts & BUF_OWNED_BY_DMA)) {
			if (released > 0) {
				goto txq_reclaim_end;
			} else {
				released = -1;
				goto txq_reclaim_end;
			}
		}
		pep->tx_used_desc_q = (tx_index + 1) % pep->tx_ring_size;
		pep->tx_desc_count--;
		addr = desc->buf_ptr;
		count = desc->byte_cnt;
		skb = pep->tx_skb[tx_index];
		if (skb)
			pep->tx_skb[tx_index] = NULL;

		if (cmd_sts & TX_ERROR) {
			if (net_ratelimit())
				netdev_err(dev, "Error in TX\n");
			dev->stats.tx_errors++;
		}
		dma_unmap_single(&pep->pdev->dev, addr, count, DMA_TO_DEVICE);
		if (skb)
			dev_kfree_skb_irq(skb);
		released++;
	}
txq_reclaim_end:
	netif_tx_unlock(dev);
	return released;
}

static void pxa168_eth_tx_timeout(struct net_device *dev, unsigned int txqueue)
{
	struct pxa168_eth_private *pep = netdev_priv(dev);

	netdev_info(dev, "TX timeout  desc_count %d\n", pep->tx_desc_count);

	schedule_work(&pep->tx_timeout_task);
}

static void pxa168_eth_tx_timeout_task(struct work_struct *work)
{
	struct pxa168_eth_private *pep = container_of(work,
						 struct pxa168_eth_private,
						 tx_timeout_task);
	struct net_device *dev = pep->dev;
	pxa168_eth_stop(dev);
	pxa168_eth_open(dev);
}

static int rxq_process(struct net_device *dev, int budget)
{
	struct pxa168_eth_private *pep = netdev_priv(dev);
	struct net_device_stats *stats = &dev->stats;
	unsigned int received_packets = 0;
	struct sk_buff *skb;

	while (budget-- > 0) {
		int rx_next_curr_desc, rx_curr_desc, rx_used_desc;
		struct rx_desc *rx_desc;
		unsigned int cmd_sts;

		/* Do not process Rx ring in case of Rx ring resource error */
		if (pep->rx_resource_err)
			break;
		rx_curr_desc = pep->rx_curr_desc_q;
		rx_used_desc = pep->rx_used_desc_q;
		rx_desc = &pep->p_rx_desc_area[rx_curr_desc];
		cmd_sts = rx_desc->cmd_sts;
		dma_rmb();
		if (cmd_sts & (BUF_OWNED_BY_DMA))
			break;
		skb = pep->rx_skb[rx_curr_desc];
		pep->rx_skb[rx_curr_desc] = NULL;

		rx_next_curr_desc = (rx_curr_desc + 1) % pep->rx_ring_size;
		pep->rx_curr_desc_q = rx_next_curr_desc;

		/* Rx descriptors exhausted. */
		/* Set the Rx ring resource error flag */
		if (rx_next_curr_desc == rx_used_desc)
			pep->rx_resource_err = 1;
		pep->rx_desc_count--;
		dma_unmap_single(&pep->pdev->dev, rx_desc->buf_ptr,
				 rx_desc->buf_size,
				 DMA_FROM_DEVICE);
		received_packets++;
		/*
		 * Update statistics.
		 * Note byte count includes 4 byte CRC count
		 */
		stats->rx_packets++;
		stats->rx_bytes += rx_desc->byte_cnt;
		/*
		 * In case received a packet without first / last bits on OR
		 * the error summary bit is on, the packets needs to be droped.
		 */
		if (((cmd_sts & (RX_FIRST_DESC | RX_LAST_DESC)) !=
		     (RX_FIRST_DESC | RX_LAST_DESC))
		    || (cmd_sts & RX_ERROR)) {

			stats->rx_dropped++;
			if ((cmd_sts & (RX_FIRST_DESC | RX_LAST_DESC)) !=
			    (RX_FIRST_DESC | RX_LAST_DESC)) {
				if (net_ratelimit())
					netdev_err(dev,
						   "Rx pkt on multiple desc\n");
			}
			if (cmd_sts & RX_ERROR)
				stats->rx_errors++;
			dev_kfree_skb_irq(skb);
		} else {
			/*
			 * The -4 is for the CRC in the trailer of the
			 * received packet
			 */
			skb_put(skb, rx_desc->byte_cnt - 4);
			skb->protocol = eth_type_trans(skb, dev);
			netif_receive_skb(skb);
		}
	}
	/* Fill RX ring with skb's */
	rxq_refill(dev);
	return received_packets;
}

static int pxa168_eth_collect_events(struct pxa168_eth_private *pep,
				     struct net_device *dev)
{
	u32 icr;
	int ret = 0;

	icr = rdl(pep, INT_CAUSE);
	if (icr == 0)
		return IRQ_NONE;

	wrl(pep, INT_CAUSE, ~icr);
	if (icr & (ICR_TXBUF_H | ICR_TXBUF_L)) {
		pep->work_todo |= WORK_TX_DONE;
		ret = 1;
	}
	if (icr & ICR_RXBUF)
		ret = 1;
	return ret;
}

static irqreturn_t pxa168_eth_int_handler(int irq, void *dev_id)
{
	struct net_device *dev = (struct net_device *)dev_id;
	struct pxa168_eth_private *pep = netdev_priv(dev);

	if (unlikely(!pxa168_eth_collect_events(pep, dev)))
		return IRQ_NONE;
	/* Disable interrupts */
	wrl(pep, INT_MASK, 0);
	napi_schedule(&pep->napi);
	return IRQ_HANDLED;
}

static void pxa168_eth_recalc_skb_size(struct pxa168_eth_private *pep)
{
	int skb_size;

	/*
	 * Reserve 2+14 bytes for an ethernet header (the hardware
	 * automatically prepends 2 bytes of dummy data to each
	 * received packet), 16 bytes for up to four VLAN tags, and
	 * 4 bytes for the trailing FCS -- 36 bytes total.
	 */
	skb_size = pep->dev->mtu + 36;

	/*
	 * Make sure that the skb size is a multiple of 8 bytes, as
	 * the lower three bits of the receive descriptor's buffer
	 * size field are ignored by the hardware.
	 */
	pep->skb_size = (skb_size + 7) & ~7;

	/*
	 * If NET_SKB_PAD is smaller than a cache line,
	 * netdev_alloc_skb() will cause skb->data to be misaligned
	 * to a cache line boundary.  If this is the case, include
	 * some extra space to allow re-aligning the data area.
	 */
	pep->skb_size += SKB_DMA_REALIGN;

}

static int set_port_config_ext(struct pxa168_eth_private *pep)
{
	int skb_size;

	pxa168_eth_recalc_skb_size(pep);
	if  (pep->skb_size <= 1518)
		skb_size = PCXR_MFL_1518;
	else if (pep->skb_size <= 1536)
		skb_size = PCXR_MFL_1536;
	else if (pep->skb_size <= 2048)
		skb_size = PCXR_MFL_2048;
	else
		skb_size = PCXR_MFL_64K;

	/* Extended Port Configuration */
	wrl(pep, PORT_CONFIG_EXT,
	    PCXR_AN_SPEED_DIS |		 /* Disable HW AN */
	    PCXR_AN_DUPLEX_DIS |
	    PCXR_AN_FLOWCTL_DIS |
	    PCXR_2BSM |			 /* Two byte prefix aligns IP hdr */
	    PCXR_DSCP_EN |		 /* Enable DSCP in IP */
	    skb_size | PCXR_FLP |	 /* do not force link pass */
	    PCXR_TX_HIGH_PRI);		 /* Transmit - high priority queue */

	return 0;
}

static void pxa168_eth_adjust_link(struct net_device *dev)
{
	struct pxa168_eth_private *pep = netdev_priv(dev);
	struct phy_device *phy = dev->phydev;
	u32 cfg, cfg_o = rdl(pep, PORT_CONFIG);
	u32 cfgext, cfgext_o = rdl(pep, PORT_CONFIG_EXT);

	cfg = cfg_o & ~PCR_DUPLEX_FULL;
	cfgext = cfgext_o & ~(PCXR_SPEED_100 | PCXR_FLOWCTL_DIS | PCXR_RMII_EN);

	if (phy->interface == PHY_INTERFACE_MODE_RMII)
		cfgext |= PCXR_RMII_EN;
	if (phy->speed == SPEED_100)
		cfgext |= PCXR_SPEED_100;
	if (phy->duplex)
		cfg |= PCR_DUPLEX_FULL;
	if (!phy->pause)
		cfgext |= PCXR_FLOWCTL_DIS;

	/* Bail out if there has nothing changed */
	if (cfg == cfg_o && cfgext == cfgext_o)
		return;

	wrl(pep, PORT_CONFIG, cfg);
	wrl(pep, PORT_CONFIG_EXT, cfgext);

	phy_print_status(phy);
}

static int pxa168_init_phy(struct net_device *dev)
{
	struct pxa168_eth_private *pep = netdev_priv(dev);
	struct ethtool_link_ksettings cmd;
	struct phy_device *phy = NULL;
	int err;

	if (dev->phydev)
		return 0;

	phy = mdiobus_scan_c22(pep->smi_bus, pep->phy_addr);
	if (IS_ERR(phy))
		return PTR_ERR(phy);

	err = phy_connect_direct(dev, phy, pxa168_eth_adjust_link,
				 pep->phy_intf);
	if (err)
		return err;

	cmd.base.phy_address = pep->phy_addr;
	cmd.base.speed = pep->phy_speed;
	cmd.base.duplex = pep->phy_duplex;
	linkmode_copy(cmd.link_modes.advertising, PHY_BASIC_FEATURES);
	cmd.base.autoneg = AUTONEG_ENABLE;

	if (cmd.base.speed != 0)
		cmd.base.autoneg = AUTONEG_DISABLE;

	return phy_ethtool_set_link_ksettings(dev, &cmd);
}

static int pxa168_init_hw(struct pxa168_eth_private *pep)
{
	int err = 0;

	/* Disable interrupts */
	wrl(pep, INT_MASK, 0);
	wrl(pep, INT_CAUSE, 0);
	/* Write to ICR to clear interrupts. */
	wrl(pep, INT_W_CLEAR, 0);
	/* Abort any transmit and receive operations and put DMA
	 * in idle state.
	 */
	abort_dma(pep);
	/* Initialize address hash table */
	err = init_hash_table(pep);
	if (err)
		return err;
	/* SDMA configuration */
	wrl(pep, SDMA_CONFIG, SDCR_BSZ8 |	/* Burst size = 32 bytes */
	    SDCR_RIFB |				/* Rx interrupt on frame */
	    SDCR_BLMT |				/* Little endian transmit */
	    SDCR_BLMR |				/* Little endian receive */
	    SDCR_RC_MAX_RETRANS);		/* Max retransmit count */
	/* Port Configuration */
	wrl(pep, PORT_CONFIG, PCR_HS);		/* Hash size is 1/2kb */
	set_port_config_ext(pep);

	return err;
}

static int rxq_init(struct net_device *dev)
{
	struct pxa168_eth_private *pep = netdev_priv(dev);
	struct rx_desc *p_rx_desc;
	int size = 0, i = 0;
	int rx_desc_num = pep->rx_ring_size;

	/* Allocate RX skb rings */
	pep->rx_skb = kcalloc(rx_desc_num, sizeof(*pep->rx_skb), GFP_KERNEL);
	if (!pep->rx_skb)
		return -ENOMEM;

	/* Allocate RX ring */
	pep->rx_desc_count = 0;
	size = pep->rx_ring_size * sizeof(struct rx_desc);
	pep->rx_desc_area_size = size;
	pep->p_rx_desc_area = dma_alloc_coherent(pep->dev->dev.parent, size,
						 &pep->rx_desc_dma,
						 GFP_KERNEL);
	if (!pep->p_rx_desc_area)
		goto out;

	/* initialize the next_desc_ptr links in the Rx descriptors ring */
	p_rx_desc = pep->p_rx_desc_area;
	for (i = 0; i < rx_desc_num; i++) {
		p_rx_desc[i].next_desc_ptr = pep->rx_desc_dma +
		    ((i + 1) % rx_desc_num) * sizeof(struct rx_desc);
	}
	/* Save Rx desc pointer to driver struct. */
	pep->rx_curr_desc_q = 0;
	pep->rx_used_desc_q = 0;
	pep->rx_desc_area_size = rx_desc_num * sizeof(struct rx_desc);
	return 0;
out:
	kfree(pep->rx_skb);
	return -ENOMEM;
}

static void rxq_deinit(struct net_device *dev)
{
	struct pxa168_eth_private *pep = netdev_priv(dev);
	int curr;

	/* Free preallocated skb's on RX rings */
	for (curr = 0; pep->rx_desc_count && curr < pep->rx_ring_size; curr++) {
		if (pep->rx_skb[curr]) {
			dev_kfree_skb(pep->rx_skb[curr]);
			pep->rx_desc_count--;
		}
	}
	if (pep->rx_desc_count)
		netdev_err(dev, "Error in freeing Rx Ring. %d skb's still\n",
			   pep->rx_desc_count);
	/* Free RX ring */
	if (pep->p_rx_desc_area)
		dma_free_coherent(pep->dev->dev.parent, pep->rx_desc_area_size,
				  pep->p_rx_desc_area, pep->rx_desc_dma);
	kfree(pep->rx_skb);
}

static int txq_init(struct net_device *dev)
{
	struct pxa168_eth_private *pep = netdev_priv(dev);
	struct tx_desc *p_tx_desc;
	int size = 0, i = 0;
	int tx_desc_num = pep->tx_ring_size;

	pep->tx_skb = kcalloc(tx_desc_num, sizeof(*pep->tx_skb), GFP_KERNEL);
	if (!pep->tx_skb)
		return -ENOMEM;

	/* Allocate TX ring */
	pep->tx_desc_count = 0;
	size = pep->tx_ring_size * sizeof(struct tx_desc);
	pep->tx_desc_area_size = size;
	pep->p_tx_desc_area = dma_alloc_coherent(pep->dev->dev.parent, size,
						 &pep->tx_desc_dma,
						 GFP_KERNEL);
	if (!pep->p_tx_desc_area)
		goto out;
	/* Initialize the next_desc_ptr links in the Tx descriptors ring */
	p_tx_desc = pep->p_tx_desc_area;
	for (i = 0; i < tx_desc_num; i++) {
		p_tx_desc[i].next_desc_ptr = pep->tx_desc_dma +
		    ((i + 1) % tx_desc_num) * sizeof(struct tx_desc);
	}
	pep->tx_curr_desc_q = 0;
	pep->tx_used_desc_q = 0;
	pep->tx_desc_area_size = tx_desc_num * sizeof(struct tx_desc);
	return 0;
out:
	kfree(pep->tx_skb);
	return -ENOMEM;
}

static void txq_deinit(struct net_device *dev)
{
	struct pxa168_eth_private *pep = netdev_priv(dev);

	/* Free outstanding skb's on TX ring */
	txq_reclaim(dev, 1);
	BUG_ON(pep->tx_used_desc_q != pep->tx_curr_desc_q);
	/* Free TX ring */
	if (pep->p_tx_desc_area)
		dma_free_coherent(pep->dev->dev.parent, pep->tx_desc_area_size,
				  pep->p_tx_desc_area, pep->tx_desc_dma);
	kfree(pep->tx_skb);
}

static int pxa168_eth_open(struct net_device *dev)
{
	struct pxa168_eth_private *pep = netdev_priv(dev);
	int err;

	err = pxa168_init_phy(dev);
	if (err)
		return err;

	err = request_irq(dev->irq, pxa168_eth_int_handler, 0, dev->name, dev);
	if (err) {
		dev_err(&dev->dev, "can't assign irq\n");
		return -EAGAIN;
	}
	pep->rx_resource_err = 0;
	err = rxq_init(dev);
	if (err != 0)
		goto out_free_irq;
	err = txq_init(dev);
	if (err != 0)
		goto out_free_rx_skb;
	pep->rx_used_desc_q = 0;
	pep->rx_curr_desc_q = 0;

	/* Fill RX ring with skb's */
	rxq_refill(dev);
	pep->rx_used_desc_q = 0;
	pep->rx_curr_desc_q = 0;
	netif_carrier_off(dev);
	napi_enable(&pep->napi);
	eth_port_start(dev);
	return 0;
out_free_rx_skb:
	rxq_deinit(dev);
out_free_irq:
	free_irq(dev->irq, dev);
	return err;
}

static int pxa168_eth_stop(struct net_device *dev)
{
	struct pxa168_eth_private *pep = netdev_priv(dev);
	eth_port_reset(dev);

	/* Disable interrupts */
	wrl(pep, INT_MASK, 0);
	wrl(pep, INT_CAUSE, 0);
	/* Write to ICR to clear interrupts. */
	wrl(pep, INT_W_CLEAR, 0);
	napi_disable(&pep->napi);
	del_timer_sync(&pep->timeout);
	netif_carrier_off(dev);
	free_irq(dev->irq, dev);
	rxq_deinit(dev);
	txq_deinit(dev);

	return 0;
}

static int pxa168_eth_change_mtu(struct net_device *dev, int mtu)
{
	struct pxa168_eth_private *pep = netdev_priv(dev);

	WRITE_ONCE(dev->mtu, mtu);
	set_port_config_ext(pep);

	if (!netif_running(dev))
		return 0;

	/*
	 * Stop and then re-open the interface. This will allocate RX
	 * skbs of the new MTU.
	 * There is a possible danger that the open will not succeed,
	 * due to memory being full.
	 */
	pxa168_eth_stop(dev);
	if (pxa168_eth_open(dev)) {
		dev_err(&dev->dev,
			"fatal error on re-opening device after MTU change\n");
	}

	return 0;
}

static int eth_alloc_tx_desc_index(struct pxa168_eth_private *pep)
{
	int tx_desc_curr;

	tx_desc_curr = pep->tx_curr_desc_q;
	pep->tx_curr_desc_q = (tx_desc_curr + 1) % pep->tx_ring_size;
	BUG_ON(pep->tx_curr_desc_q == pep->tx_used_desc_q);
	pep->tx_desc_count++;

	return tx_desc_curr;
}

static int pxa168_rx_poll(struct napi_struct *napi, int budget)
{
	struct pxa168_eth_private *pep =
	    container_of(napi, struct pxa168_eth_private, napi);
	struct net_device *dev = pep->dev;
	int work_done = 0;

	/*
	 * We call txq_reclaim every time since in NAPI interupts are disabled
	 * and due to this we miss the TX_DONE interrupt,which is not updated in
	 * interrupt status register.
	 */
	txq_reclaim(dev, 0);
	if (netif_queue_stopped(dev)
	    && pep->tx_ring_size - pep->tx_desc_count > 1) {
		netif_wake_queue(dev);
	}
	work_done = rxq_process(dev, budget);
	if (work_done < budget) {
		napi_complete_done(napi, work_done);
		wrl(pep, INT_MASK, ALL_INTS);
	}

	return work_done;
}

static netdev_tx_t
pxa168_eth_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct pxa168_eth_private *pep = netdev_priv(dev);
	struct net_device_stats *stats = &dev->stats;
	struct tx_desc *desc;
	int tx_index;
	int length;

	tx_index = eth_alloc_tx_desc_index(pep);
	desc = &pep->p_tx_desc_area[tx_index];
	length = skb->len;
	pep->tx_skb[tx_index] = skb;
	desc->byte_cnt = length;
	desc->buf_ptr = dma_map_single(&pep->pdev->dev, skb->data, length,
					DMA_TO_DEVICE);

	skb_tx_timestamp(skb);

	dma_wmb();
	desc->cmd_sts = BUF_OWNED_BY_DMA | TX_GEN_CRC | TX_FIRST_DESC |
			TX_ZERO_PADDING | TX_LAST_DESC | TX_EN_INT;
	wmb();
	wrl(pep, SDMA_CMD, SDMA_CMD_TXDH | SDMA_CMD_ERD);

	stats->tx_bytes += length;
	stats->tx_packets++;
	netif_trans_update(dev);
	if (pep->tx_ring_size - pep->tx_desc_count <= 1) {
		/* We handled the current skb, but now we are out of space.*/
		netif_stop_queue(dev);
	}

	return NETDEV_TX_OK;
}

static int smi_wait_ready(struct pxa168_eth_private *pep)
{
	int i = 0;

	/* wait for the SMI register to become available */
	for (i = 0; rdl(pep, SMI) & SMI_BUSY; i++) {
		if (i == PHY_WAIT_ITERATIONS)
			return -ETIMEDOUT;
		msleep(10);
	}

	return 0;
}

static int pxa168_smi_read(struct mii_bus *bus, int phy_addr, int regnum)
{
	struct pxa168_eth_private *pep = bus->priv;
	int i = 0;
	int val;

	if (smi_wait_ready(pep)) {
		netdev_warn(pep->dev, "pxa168_eth: SMI bus busy timeout\n");
		return -ETIMEDOUT;
	}
	wrl(pep, SMI, (phy_addr << 16) | (regnum << 21) | SMI_OP_R);
	/* now wait for the data to be valid */
	for (i = 0; !((val = rdl(pep, SMI)) & SMI_R_VALID); i++) {
		if (i == PHY_WAIT_ITERATIONS) {
			netdev_warn(pep->dev,
				    "pxa168_eth: SMI bus read not valid\n");
			return -ENODEV;
		}
		msleep(10);
	}

	return val & 0xffff;
}

static int pxa168_smi_write(struct mii_bus *bus, int phy_addr, int regnum,
			    u16 value)
{
	struct pxa168_eth_private *pep = bus->priv;

	if (smi_wait_ready(pep)) {
		netdev_warn(pep->dev, "pxa168_eth: SMI bus busy timeout\n");
		return -ETIMEDOUT;
	}

	wrl(pep, SMI, (phy_addr << 16) | (regnum << 21) |
	    SMI_OP_W | (value & 0xffff));

	if (smi_wait_ready(pep)) {
		netdev_err(pep->dev, "pxa168_eth: SMI bus busy timeout\n");
		return -ETIMEDOUT;
	}

	return 0;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void pxa168_eth_netpoll(struct net_device *dev)
{
	disable_irq(dev->irq);
	pxa168_eth_int_handler(dev->irq, dev);
	enable_irq(dev->irq);
}
#endif

static void pxa168_get_drvinfo(struct net_device *dev,
			       struct ethtool_drvinfo *info)
{
	strscpy(info->driver, DRIVER_NAME, sizeof(info->driver));
	strscpy(info->version, DRIVER_VERSION, sizeof(info->version));
	strscpy(info->fw_version, "N/A", sizeof(info->fw_version));
	strscpy(info->bus_info, "N/A", sizeof(info->bus_info));
}

static const struct ethtool_ops pxa168_ethtool_ops = {
	.get_drvinfo	= pxa168_get_drvinfo,
	.nway_reset	= phy_ethtool_nway_reset,
	.get_link	= ethtool_op_get_link,
	.get_ts_info	= ethtool_op_get_ts_info,
	.get_link_ksettings = phy_ethtool_get_link_ksettings,
	.set_link_ksettings = phy_ethtool_set_link_ksettings,
};

static const struct net_device_ops pxa168_eth_netdev_ops = {
	.ndo_open		= pxa168_eth_open,
	.ndo_stop		= pxa168_eth_stop,
	.ndo_start_xmit		= pxa168_eth_start_xmit,
	.ndo_set_rx_mode	= pxa168_eth_set_rx_mode,
	.ndo_set_mac_address	= pxa168_eth_set_mac_address,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_eth_ioctl		= phy_do_ioctl,
	.ndo_change_mtu		= pxa168_eth_change_mtu,
	.ndo_tx_timeout		= pxa168_eth_tx_timeout,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller    = pxa168_eth_netpoll,
#endif
};

static int pxa168_eth_probe(struct platform_device *pdev)
{
	struct pxa168_eth_private *pep = NULL;
	struct net_device *dev = NULL;
	struct clk *clk;
	struct device_node *np;
	int err;

	printk(KERN_NOTICE "PXA168 10/100 Ethernet Driver\n");

	clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "Fast Ethernet failed to get clock\n");
		return -ENODEV;
	}
	clk_prepare_enable(clk);

	dev = alloc_etherdev(sizeof(struct pxa168_eth_private));
	if (!dev) {
		err = -ENOMEM;
		goto err_clk;
	}

	platform_set_drvdata(pdev, dev);
	pep = netdev_priv(dev);
	pep->dev = dev;
	pep->clk = clk;

	pep->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pep->base)) {
		err = PTR_ERR(pep->base);
		goto err_netdev;
	}

	err = platform_get_irq(pdev, 0);
	if (err == -EPROBE_DEFER)
		goto err_netdev;
	BUG_ON(dev->irq < 0);
	dev->irq = err;
	dev->netdev_ops = &pxa168_eth_netdev_ops;
	dev->watchdog_timeo = 2 * HZ;
	dev->base_addr = 0;
	dev->ethtool_ops = &pxa168_ethtool_ops;

	/* MTU range: 68 - 9500 */
	dev->min_mtu = ETH_MIN_MTU;
	dev->max_mtu = 9500;

	INIT_WORK(&pep->tx_timeout_task, pxa168_eth_tx_timeout_task);

	err = of_get_ethdev_address(pdev->dev.of_node, dev);
	if (err) {
		u8 addr[ETH_ALEN];

		/* try reading the mac address, if set by the bootloader */
		pxa168_eth_get_mac_address(dev, addr);
		if (is_valid_ether_addr(addr)) {
			eth_hw_addr_set(dev, addr);
		} else {
			dev_info(&pdev->dev, "Using random mac address\n");
			eth_hw_addr_random(dev);
		}
	}

	pep->rx_ring_size = NUM_RX_DESCS;
	pep->tx_ring_size = NUM_TX_DESCS;

	pep->pd = dev_get_platdata(&pdev->dev);
	if (pep->pd) {
		if (pep->pd->rx_queue_size)
			pep->rx_ring_size = pep->pd->rx_queue_size;

		if (pep->pd->tx_queue_size)
			pep->tx_ring_size = pep->pd->tx_queue_size;

		pep->port_num = pep->pd->port_number;
		pep->phy_addr = pep->pd->phy_addr;
		pep->phy_speed = pep->pd->speed;
		pep->phy_duplex = pep->pd->duplex;
		pep->phy_intf = pep->pd->intf;

		if (pep->pd->init)
			pep->pd->init();
	} else if (pdev->dev.of_node) {
		of_property_read_u32(pdev->dev.of_node, "port-id",
				     &pep->port_num);

		np = of_parse_phandle(pdev->dev.of_node, "phy-handle", 0);
		if (!np) {
			dev_err(&pdev->dev, "missing phy-handle\n");
			err = -EINVAL;
			goto err_netdev;
		}
		of_property_read_u32(np, "reg", &pep->phy_addr);
		of_node_put(np);
		err = of_get_phy_mode(pdev->dev.of_node, &pep->phy_intf);
		if (err && err != -ENODEV)
			goto err_netdev;
	}

	/* Hardware supports only 3 ports */
	BUG_ON(pep->port_num > 2);
	netif_napi_add_weight(dev, &pep->napi, pxa168_rx_poll,
			      pep->rx_ring_size);

	memset(&pep->timeout, 0, sizeof(struct timer_list));
	timer_setup(&pep->timeout, rxq_refill_timer_wrapper, 0);

	pep->smi_bus = mdiobus_alloc();
	if (!pep->smi_bus) {
		err = -ENOMEM;
		goto err_netdev;
	}
	pep->smi_bus->priv = pep;
	pep->smi_bus->name = "pxa168_eth smi";
	pep->smi_bus->read = pxa168_smi_read;
	pep->smi_bus->write = pxa168_smi_write;
	snprintf(pep->smi_bus->id, MII_BUS_ID_SIZE, "%s-%d",
		pdev->name, pdev->id);
	pep->smi_bus->parent = &pdev->dev;
	pep->smi_bus->phy_mask = 0xffffffff;
	err = mdiobus_register(pep->smi_bus);
	if (err)
		goto err_free_mdio;

	pep->pdev = pdev;
	SET_NETDEV_DEV(dev, &pdev->dev);
	pxa168_init_hw(pep);
	err = register_netdev(dev);
	if (err)
		goto err_mdiobus;
	return 0;

err_mdiobus:
	mdiobus_unregister(pep->smi_bus);
err_free_mdio:
	mdiobus_free(pep->smi_bus);
err_netdev:
	free_netdev(dev);
err_clk:
	clk_disable_unprepare(clk);
	return err;
}

static void pxa168_eth_remove(struct platform_device *pdev)
{
	struct net_device *dev = platform_get_drvdata(pdev);
	struct pxa168_eth_private *pep = netdev_priv(dev);

	cancel_work_sync(&pep->tx_timeout_task);
	if (pep->htpr) {
		dma_free_coherent(pep->dev->dev.parent, HASH_ADDR_TABLE_SIZE,
				  pep->htpr, pep->htpr_dma);
		pep->htpr = NULL;
	}
	if (dev->phydev)
		phy_disconnect(dev->phydev);

	clk_disable_unprepare(pep->clk);
	mdiobus_unregister(pep->smi_bus);
	mdiobus_free(pep->smi_bus);
	unregister_netdev(dev);
	free_netdev(dev);
}

static void pxa168_eth_shutdown(struct platform_device *pdev)
{
	struct net_device *dev = platform_get_drvdata(pdev);
	eth_port_reset(dev);
}

#ifdef CONFIG_PM
static int pxa168_eth_resume(struct platform_device *pdev)
{
	return -ENOSYS;
}

static int pxa168_eth_suspend(struct platform_device *pdev, pm_message_t state)
{
	return -ENOSYS;
}

#else
#define pxa168_eth_resume NULL
#define pxa168_eth_suspend NULL
#endif

static const struct of_device_id pxa168_eth_of_match[] = {
	{ .compatible = "marvell,pxa168-eth" },
	{ },
};
MODULE_DEVICE_TABLE(of, pxa168_eth_of_match);

static struct platform_driver pxa168_eth_driver = {
	.probe = pxa168_eth_probe,
	.remove_new = pxa168_eth_remove,
	.shutdown = pxa168_eth_shutdown,
	.resume = pxa168_eth_resume,
	.suspend = pxa168_eth_suspend,
	.driver = {
		.name		= DRIVER_NAME,
		.of_match_table	= pxa168_eth_of_match,
	},
};

module_platform_driver(pxa168_eth_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Ethernet driver for Marvell PXA168");
MODULE_ALIAS("platform:pxa168_eth");

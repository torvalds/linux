/*
 * Blackfin On-Chip MAC Driver
 *
 * Copyright 2004-2007 Analog Devices Inc.
 *
 * Enter bugs at http://blackfin.uclinux.org/
 *
 * Licensed under the GPL-2 or later.
 */

#define BFIN_MAC_CSUM_OFFLOAD

struct dma_descriptor {
	struct dma_descriptor *next_dma_desc;
	unsigned long start_addr;
	unsigned short config;
	unsigned short x_count;
};

struct status_area_rx {
#if defined(BFIN_MAC_CSUM_OFFLOAD)
	unsigned short ip_hdr_csum;	/* ip header checksum */
	/* ip payload(udp or tcp or others) checksum */
	unsigned short ip_payload_csum;
#endif
	unsigned long status_word;	/* the frame status word */
};

struct status_area_tx {
	unsigned long status_word;	/* the frame status word */
};

/* use two descriptors for a packet */
struct net_dma_desc_rx {
	struct net_dma_desc_rx *next;
	struct sk_buff *skb;
	struct dma_descriptor desc_a;
	struct dma_descriptor desc_b;
	struct status_area_rx status;
};

/* use two descriptors for a packet */
struct net_dma_desc_tx {
	struct net_dma_desc_tx *next;
	struct sk_buff *skb;
	struct dma_descriptor desc_a;
	struct dma_descriptor desc_b;
	unsigned char packet[1560];
	struct status_area_tx status;
};

struct bfin_mac_local {
	/*
	 * these are things that the kernel wants me to keep, so users
	 * can find out semi-useless statistics of how well the card is
	 * performing
	 */
	struct net_device_stats stats;

	unsigned char Mac[6];	/* MAC address of the board */
	spinlock_t lock;

	/* MII and PHY stuffs */
	int old_link;          /* used by bf537_adjust_link */
	int old_speed;
	int old_duplex;

	struct phy_device *phydev;
	struct mii_bus mii_bus;
};

extern void bfin_get_ether_addr(char *addr);

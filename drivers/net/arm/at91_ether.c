/*
 * Ethernet driver for the Atmel AT91RM9200 (Thunder)
 *
 *  Copyright (C) 2003 SAN People (Pty) Ltd
 *
 * Based on an earlier Atmel EMAC macrocell driver by Atmel and Lineo Inc.
 * Initial version by Rick Bronson 01/11/2003
 *
 * Intel LXT971A PHY support by Christopher Bahns & David Knickerbocker
 *   (Polaroid Corporation)
 *
 * Realtek RTL8201(B)L PHY support by Roman Avramenko <roman@imsystems.ru>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/mii.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/dma-mapping.h>
#include <linux/ethtool.h>
#include <linux/platform_device.h>
#include <linux/clk.h>

#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/mach-types.h>

#include <asm/arch/at91rm9200_emac.h>
#include <asm/arch/gpio.h>
#include <asm/arch/board.h>

#include "at91_ether.h"

#define DRV_NAME	"at91_ether"
#define DRV_VERSION	"1.0"

#define LINK_POLL_INTERVAL	(HZ)

/* ..................................................................... */

/*
 * Read from a EMAC register.
 */
static inline unsigned long at91_emac_read(unsigned int reg)
{
	void __iomem *emac_base = (void __iomem *)AT91_VA_BASE_EMAC;

	return __raw_readl(emac_base + reg);
}

/*
 * Write to a EMAC register.
 */
static inline void at91_emac_write(unsigned int reg, unsigned long value)
{
	void __iomem *emac_base = (void __iomem *)AT91_VA_BASE_EMAC;

	__raw_writel(value, emac_base + reg);
}

/* ........................... PHY INTERFACE ........................... */

/*
 * Enable the MDIO bit in MAC control register
 * When not called from an interrupt-handler, access to the PHY must be
 *  protected by a spinlock.
 */
static void enable_mdi(void)
{
	unsigned long ctl;

	ctl = at91_emac_read(AT91_EMAC_CTL);
	at91_emac_write(AT91_EMAC_CTL, ctl | AT91_EMAC_MPE);	/* enable management port */
}

/*
 * Disable the MDIO bit in the MAC control register
 */
static void disable_mdi(void)
{
	unsigned long ctl;

	ctl = at91_emac_read(AT91_EMAC_CTL);
	at91_emac_write(AT91_EMAC_CTL, ctl & ~AT91_EMAC_MPE);	/* disable management port */
}

/*
 * Wait until the PHY operation is complete.
 */
static inline void at91_phy_wait(void) {
	unsigned long timeout = jiffies + 2;

	while (!(at91_emac_read(AT91_EMAC_SR) & AT91_EMAC_SR_IDLE)) {
		if (time_after(jiffies, timeout)) {
			printk("at91_ether: MIO timeout\n");
			break;
		}
		cpu_relax();
	}
}

/*
 * Write value to the a PHY register
 * Note: MDI interface is assumed to already have been enabled.
 */
static void write_phy(unsigned char phy_addr, unsigned char address, unsigned int value)
{
	at91_emac_write(AT91_EMAC_MAN, AT91_EMAC_MAN_802_3 | AT91_EMAC_RW_W
		| ((phy_addr & 0x1f) << 23) | (address << 18) | (value & AT91_EMAC_DATA));

	/* Wait until IDLE bit in Network Status register is cleared */
	at91_phy_wait();
}

/*
 * Read value stored in a PHY register.
 * Note: MDI interface is assumed to already have been enabled.
 */
static void read_phy(unsigned char phy_addr, unsigned char address, unsigned int *value)
{
	at91_emac_write(AT91_EMAC_MAN, AT91_EMAC_MAN_802_3 | AT91_EMAC_RW_R
		| ((phy_addr & 0x1f) << 23) | (address << 18));

	/* Wait until IDLE bit in Network Status register is cleared */
	at91_phy_wait();

	*value = at91_emac_read(AT91_EMAC_MAN) & AT91_EMAC_DATA;
}

/* ........................... PHY MANAGEMENT .......................... */

/*
 * Access the PHY to determine the current link speed and mode, and update the
 * MAC accordingly.
 * If no link or auto-negotiation is busy, then no changes are made.
 */
static void update_linkspeed(struct net_device *dev, int silent)
{
	struct at91_private *lp = netdev_priv(dev);
	unsigned int bmsr, bmcr, lpa, mac_cfg;
	unsigned int speed, duplex;

	if (!mii_link_ok(&lp->mii)) {		/* no link */
		netif_carrier_off(dev);
		if (!silent)
			printk(KERN_INFO "%s: Link down.\n", dev->name);
		return;
	}

	/* Link up, or auto-negotiation still in progress */
	read_phy(lp->phy_address, MII_BMSR, &bmsr);
	read_phy(lp->phy_address, MII_BMCR, &bmcr);
	if (bmcr & BMCR_ANENABLE) {				/* AutoNegotiation is enabled */
		if (!(bmsr & BMSR_ANEGCOMPLETE))
			return;			/* Do nothing - another interrupt generated when negotiation complete */

		read_phy(lp->phy_address, MII_LPA, &lpa);
		if ((lpa & LPA_100FULL) || (lpa & LPA_100HALF)) speed = SPEED_100;
		else speed = SPEED_10;
		if ((lpa & LPA_100FULL) || (lpa & LPA_10FULL)) duplex = DUPLEX_FULL;
		else duplex = DUPLEX_HALF;
	} else {
		speed = (bmcr & BMCR_SPEED100) ? SPEED_100 : SPEED_10;
		duplex = (bmcr & BMCR_FULLDPLX) ? DUPLEX_FULL : DUPLEX_HALF;
	}

	/* Update the MAC */
	mac_cfg = at91_emac_read(AT91_EMAC_CFG) & ~(AT91_EMAC_SPD | AT91_EMAC_FD);
	if (speed == SPEED_100) {
		if (duplex == DUPLEX_FULL)		/* 100 Full Duplex */
			mac_cfg |= AT91_EMAC_SPD | AT91_EMAC_FD;
		else					/* 100 Half Duplex */
			mac_cfg |= AT91_EMAC_SPD;
	} else {
		if (duplex == DUPLEX_FULL)		/* 10 Full Duplex */
			mac_cfg |= AT91_EMAC_FD;
		else {}					/* 10 Half Duplex */
	}
	at91_emac_write(AT91_EMAC_CFG, mac_cfg);

	if (!silent)
		printk(KERN_INFO "%s: Link now %i-%s\n", dev->name, speed, (duplex == DUPLEX_FULL) ? "FullDuplex" : "HalfDuplex");
	netif_carrier_on(dev);
}

/*
 * Handle interrupts from the PHY
 */
static irqreturn_t at91ether_phy_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = (struct net_device *) dev_id;
	struct at91_private *lp = netdev_priv(dev);
	unsigned int phy;

	/*
	 * This hander is triggered on both edges, but the PHY chips expect
	 * level-triggering.  We therefore have to check if the PHY actually has
	 * an IRQ pending.
	 */
	enable_mdi();
	if ((lp->phy_type == MII_DM9161_ID) || (lp->phy_type == MII_DM9161A_ID)) {
		read_phy(lp->phy_address, MII_DSINTR_REG, &phy);	/* ack interrupt in Davicom PHY */
		if (!(phy & (1 << 0)))
			goto done;
	}
	else if (lp->phy_type == MII_LXT971A_ID) {
		read_phy(lp->phy_address, MII_ISINTS_REG, &phy);	/* ack interrupt in Intel PHY */
		if (!(phy & (1 << 2)))
			goto done;
	}
	else if (lp->phy_type == MII_BCM5221_ID) {
		read_phy(lp->phy_address, MII_BCMINTR_REG, &phy);	/* ack interrupt in Broadcom PHY */
		if (!(phy & (1 << 0)))
			goto done;
	}
	else if (lp->phy_type == MII_KS8721_ID) {
		read_phy(lp->phy_address, MII_TPISTATUS, &phy);		/* ack interrupt in Micrel PHY */
		if (!(phy & ((1 << 2) | 1)))
			goto done;
	}
	else if (lp->phy_type == MII_T78Q21x3_ID) {			/* ack interrupt in Teridian PHY */
		read_phy(lp->phy_address, MII_T78Q21INT_REG, &phy);
		if (!(phy & ((1 << 2) | 1)))
			goto done;
	}
	else if (lp->phy_type == MII_DP83848_ID) {
		read_phy(lp->phy_address, MII_DPPHYSTS_REG, &phy);	/* ack interrupt in DP83848 PHY */
		if (!(phy & (1 << 7)))
			goto done;
	}

	update_linkspeed(dev, 0);

done:
	disable_mdi();

	return IRQ_HANDLED;
}

/*
 * Initialize and enable the PHY interrupt for link-state changes
 */
static void enable_phyirq(struct net_device *dev)
{
	struct at91_private *lp = netdev_priv(dev);
	unsigned int dsintr, irq_number;
	int status;

	irq_number = lp->board_data.phy_irq_pin;
	if (!irq_number) {
		/*
		 * PHY doesn't have an IRQ pin (RTL8201, DP83847, AC101L),
		 * or board does not have it connected.
		 */
		mod_timer(&lp->check_timer, jiffies + LINK_POLL_INTERVAL);
		return;
	}

	status = request_irq(irq_number, at91ether_phy_interrupt, 0, dev->name, dev);
	if (status) {
		printk(KERN_ERR "at91_ether: PHY IRQ %d request failed - status %d!\n", irq_number, status);
		return;
	}

	spin_lock_irq(&lp->lock);
	enable_mdi();

	if ((lp->phy_type == MII_DM9161_ID) || (lp->phy_type == MII_DM9161A_ID)) {	/* for Davicom PHY */
		read_phy(lp->phy_address, MII_DSINTR_REG, &dsintr);
		dsintr = dsintr & ~0xf00;		/* clear bits 8..11 */
		write_phy(lp->phy_address, MII_DSINTR_REG, dsintr);
	}
	else if (lp->phy_type == MII_LXT971A_ID) {	/* for Intel PHY */
		read_phy(lp->phy_address, MII_ISINTE_REG, &dsintr);
		dsintr = dsintr | 0xf2;			/* set bits 1, 4..7 */
		write_phy(lp->phy_address, MII_ISINTE_REG, dsintr);
	}
	else if (lp->phy_type == MII_BCM5221_ID) {	/* for Broadcom PHY */
		dsintr = (1 << 15) | ( 1 << 14);
		write_phy(lp->phy_address, MII_BCMINTR_REG, dsintr);
	}
	else if (lp->phy_type == MII_KS8721_ID) {	/* for Micrel PHY */
		dsintr = (1 << 10) | ( 1 << 8);
		write_phy(lp->phy_address, MII_TPISTATUS, dsintr);
	}
	else if (lp->phy_type == MII_T78Q21x3_ID) {	/* for Teridian PHY */
		read_phy(lp->phy_address, MII_T78Q21INT_REG, &dsintr);
		dsintr = dsintr | 0x500;		/* set bits 8, 10 */
		write_phy(lp->phy_address, MII_T78Q21INT_REG, dsintr);
	}
	else if (lp->phy_type == MII_DP83848_ID) {	/* National Semiconductor DP83848 PHY */
		read_phy(lp->phy_address, MII_DPMISR_REG, &dsintr);
		dsintr = dsintr | 0x3c;			/* set bits 2..5 */
		write_phy(lp->phy_address, MII_DPMISR_REG, dsintr);
		read_phy(lp->phy_address, MII_DPMICR_REG, &dsintr);
		dsintr = dsintr | 0x3;			/* set bits 0,1 */
		write_phy(lp->phy_address, MII_DPMICR_REG, dsintr);
	}

	disable_mdi();
	spin_unlock_irq(&lp->lock);
}

/*
 * Disable the PHY interrupt
 */
static void disable_phyirq(struct net_device *dev)
{
	struct at91_private *lp = netdev_priv(dev);
	unsigned int dsintr;
	unsigned int irq_number;

	irq_number = lp->board_data.phy_irq_pin;
	if (!irq_number) {
		del_timer_sync(&lp->check_timer);
		return;
	}

	spin_lock_irq(&lp->lock);
	enable_mdi();

	if ((lp->phy_type == MII_DM9161_ID) || (lp->phy_type == MII_DM9161A_ID)) {	/* for Davicom PHY */
		read_phy(lp->phy_address, MII_DSINTR_REG, &dsintr);
		dsintr = dsintr | 0xf00;			/* set bits 8..11 */
		write_phy(lp->phy_address, MII_DSINTR_REG, dsintr);
	}
	else if (lp->phy_type == MII_LXT971A_ID) {	/* for Intel PHY */
		read_phy(lp->phy_address, MII_ISINTE_REG, &dsintr);
		dsintr = dsintr & ~0xf2;			/* clear bits 1, 4..7 */
		write_phy(lp->phy_address, MII_ISINTE_REG, dsintr);
	}
	else if (lp->phy_type == MII_BCM5221_ID) {	/* for Broadcom PHY */
		read_phy(lp->phy_address, MII_BCMINTR_REG, &dsintr);
		dsintr = ~(1 << 14);
		write_phy(lp->phy_address, MII_BCMINTR_REG, dsintr);
	}
	else if (lp->phy_type == MII_KS8721_ID) {	/* for Micrel PHY */
		read_phy(lp->phy_address, MII_TPISTATUS, &dsintr);
		dsintr = ~((1 << 10) | (1 << 8));
		write_phy(lp->phy_address, MII_TPISTATUS, dsintr);
	}
	else if (lp->phy_type == MII_T78Q21x3_ID) {	/* for Teridian PHY */
		read_phy(lp->phy_address, MII_T78Q21INT_REG, &dsintr);
		dsintr = dsintr & ~0x500;			/* clear bits 8, 10 */
		write_phy(lp->phy_address, MII_T78Q21INT_REG, dsintr);
	}
	else if (lp->phy_type == MII_DP83848_ID) {	/* National Semiconductor DP83848 PHY */
		read_phy(lp->phy_address, MII_DPMICR_REG, &dsintr);
		dsintr = dsintr & ~0x3;				/* clear bits 0, 1 */
		write_phy(lp->phy_address, MII_DPMICR_REG, dsintr);
		read_phy(lp->phy_address, MII_DPMISR_REG, &dsintr);
		dsintr = dsintr & ~0x3c;			/* clear bits 2..5 */
		write_phy(lp->phy_address, MII_DPMISR_REG, dsintr);
	}

	disable_mdi();
	spin_unlock_irq(&lp->lock);

	free_irq(irq_number, dev);			/* Free interrupt handler */
}

/*
 * Perform a software reset of the PHY.
 */
#if 0
static void reset_phy(struct net_device *dev)
{
	struct at91_private *lp = netdev_priv(dev);
	unsigned int bmcr;

	spin_lock_irq(&lp->lock);
	enable_mdi();

	/* Perform PHY reset */
	write_phy(lp->phy_address, MII_BMCR, BMCR_RESET);

	/* Wait until PHY reset is complete */
	do {
		read_phy(lp->phy_address, MII_BMCR, &bmcr);
	} while (!(bmcr && BMCR_RESET));

	disable_mdi();
	spin_unlock_irq(&lp->lock);
}
#endif

static void at91ether_check_link(unsigned long dev_id)
{
	struct net_device *dev = (struct net_device *) dev_id;
	struct at91_private *lp = netdev_priv(dev);

	enable_mdi();
	update_linkspeed(dev, 1);
	disable_mdi();

	mod_timer(&lp->check_timer, jiffies + LINK_POLL_INTERVAL);
}

/* ......................... ADDRESS MANAGEMENT ........................ */

/*
 * NOTE: Your bootloader must always set the MAC address correctly before
 * booting into Linux.
 *
 * - It must always set the MAC address after reset, even if it doesn't
 *   happen to access the Ethernet while it's booting.  Some versions of
 *   U-Boot on the AT91RM9200-DK do not do this.
 *
 * - Likewise it must store the addresses in the correct byte order.
 *   MicroMonitor (uMon) on the CSB337 does this incorrectly (and
 *   continues to do so, for bug-compatibility).
 */

static short __init unpack_mac_address(struct net_device *dev, unsigned int hi, unsigned int lo)
{
	char addr[6];

	if (machine_is_csb337()) {
		addr[5] = (lo & 0xff);			/* The CSB337 bootloader stores the MAC the wrong-way around */
		addr[4] = (lo & 0xff00) >> 8;
		addr[3] = (lo & 0xff0000) >> 16;
		addr[2] = (lo & 0xff000000) >> 24;
		addr[1] = (hi & 0xff);
		addr[0] = (hi & 0xff00) >> 8;
	}
	else {
		addr[0] = (lo & 0xff);
		addr[1] = (lo & 0xff00) >> 8;
		addr[2] = (lo & 0xff0000) >> 16;
		addr[3] = (lo & 0xff000000) >> 24;
		addr[4] = (hi & 0xff);
		addr[5] = (hi & 0xff00) >> 8;
	}

	if (is_valid_ether_addr(addr)) {
		memcpy(dev->dev_addr, &addr, 6);
		return 1;
	}
	return 0;
}

/*
 * Set the ethernet MAC address in dev->dev_addr
 */
static void __init get_mac_address(struct net_device *dev)
{
	/* Check Specific-Address 1 */
	if (unpack_mac_address(dev, at91_emac_read(AT91_EMAC_SA1H), at91_emac_read(AT91_EMAC_SA1L)))
		return;
	/* Check Specific-Address 2 */
	if (unpack_mac_address(dev, at91_emac_read(AT91_EMAC_SA2H), at91_emac_read(AT91_EMAC_SA2L)))
		return;
	/* Check Specific-Address 3 */
	if (unpack_mac_address(dev, at91_emac_read(AT91_EMAC_SA3H), at91_emac_read(AT91_EMAC_SA3L)))
		return;
	/* Check Specific-Address 4 */
	if (unpack_mac_address(dev, at91_emac_read(AT91_EMAC_SA4H), at91_emac_read(AT91_EMAC_SA4L)))
		return;

	printk(KERN_ERR "at91_ether: Your bootloader did not configure a MAC address.\n");
}

/*
 * Program the hardware MAC address from dev->dev_addr.
 */
static void update_mac_address(struct net_device *dev)
{
	at91_emac_write(AT91_EMAC_SA1L, (dev->dev_addr[3] << 24) | (dev->dev_addr[2] << 16) | (dev->dev_addr[1] << 8) | (dev->dev_addr[0]));
	at91_emac_write(AT91_EMAC_SA1H, (dev->dev_addr[5] << 8) | (dev->dev_addr[4]));

	at91_emac_write(AT91_EMAC_SA2L, 0);
	at91_emac_write(AT91_EMAC_SA2H, 0);
}

/*
 * Store the new hardware address in dev->dev_addr, and update the MAC.
 */
static int set_mac_address(struct net_device *dev, void* addr)
{
	struct sockaddr *address = addr;
	DECLARE_MAC_BUF(mac);

	if (!is_valid_ether_addr(address->sa_data))
		return -EADDRNOTAVAIL;

	memcpy(dev->dev_addr, address->sa_data, dev->addr_len);
	update_mac_address(dev);

	printk("%s: Setting MAC address to %s\n", dev->name,
	       print_mac(mac, dev->dev_addr));

	return 0;
}

static int inline hash_bit_value(int bitnr, __u8 *addr)
{
	if (addr[bitnr / 8] & (1 << (bitnr % 8)))
		return 1;
	return 0;
}

/*
 * The hash address register is 64 bits long and takes up two locations in the memory map.
 * The least significant bits are stored in EMAC_HSL and the most significant
 * bits in EMAC_HSH.
 *
 * The unicast hash enable and the multicast hash enable bits in the network configuration
 *  register enable the reception of hash matched frames. The destination address is
 *  reduced to a 6 bit index into the 64 bit hash register using the following hash function.
 * The hash function is an exclusive or of every sixth bit of the destination address.
 *   hash_index[5] = da[5] ^ da[11] ^ da[17] ^ da[23] ^ da[29] ^ da[35] ^ da[41] ^ da[47]
 *   hash_index[4] = da[4] ^ da[10] ^ da[16] ^ da[22] ^ da[28] ^ da[34] ^ da[40] ^ da[46]
 *   hash_index[3] = da[3] ^ da[09] ^ da[15] ^ da[21] ^ da[27] ^ da[33] ^ da[39] ^ da[45]
 *   hash_index[2] = da[2] ^ da[08] ^ da[14] ^ da[20] ^ da[26] ^ da[32] ^ da[38] ^ da[44]
 *   hash_index[1] = da[1] ^ da[07] ^ da[13] ^ da[19] ^ da[25] ^ da[31] ^ da[37] ^ da[43]
 *   hash_index[0] = da[0] ^ da[06] ^ da[12] ^ da[18] ^ da[24] ^ da[30] ^ da[36] ^ da[42]
 * da[0] represents the least significant bit of the first byte received, that is, the multicast/
 *  unicast indicator, and da[47] represents the most significant bit of the last byte
 *  received.
 * If the hash index points to a bit that is set in the hash register then the frame will be
 *  matched according to whether the frame is multicast or unicast.
 * A multicast match will be signalled if the multicast hash enable bit is set, da[0] is 1 and
 *  the hash index points to a bit set in the hash register.
 * A unicast match will be signalled if the unicast hash enable bit is set, da[0] is 0 and the
 *  hash index points to a bit set in the hash register.
 * To receive all multicast frames, the hash register should be set with all ones and the
 *  multicast hash enable bit should be set in the network configuration register.
 */

/*
 * Return the hash index value for the specified address.
 */
static int hash_get_index(__u8 *addr)
{
	int i, j, bitval;
	int hash_index = 0;

	for (j = 0; j < 6; j++) {
		for (i = 0, bitval = 0; i < 8; i++)
			bitval ^= hash_bit_value(i*6 + j, addr);

		hash_index |= (bitval << j);
	}

	return hash_index;
}

/*
 * Add multicast addresses to the internal multicast-hash table.
 */
static void at91ether_sethashtable(struct net_device *dev)
{
	struct dev_mc_list *curr;
	unsigned long mc_filter[2];
	unsigned int i, bitnr;

	mc_filter[0] = mc_filter[1] = 0;

	curr = dev->mc_list;
	for (i = 0; i < dev->mc_count; i++, curr = curr->next) {
		if (!curr) break;	/* unexpected end of list */

		bitnr = hash_get_index(curr->dmi_addr);
		mc_filter[bitnr >> 5] |= 1 << (bitnr & 31);
	}

	at91_emac_write(AT91_EMAC_HSL, mc_filter[0]);
	at91_emac_write(AT91_EMAC_HSH, mc_filter[1]);
}

/*
 * Enable/Disable promiscuous and multicast modes.
 */
static void at91ether_set_rx_mode(struct net_device *dev)
{
	unsigned long cfg;

	cfg = at91_emac_read(AT91_EMAC_CFG);

	if (dev->flags & IFF_PROMISC)			/* Enable promiscuous mode */
		cfg |= AT91_EMAC_CAF;
	else if (dev->flags & (~IFF_PROMISC))		/* Disable promiscuous mode */
		cfg &= ~AT91_EMAC_CAF;

	if (dev->flags & IFF_ALLMULTI) {		/* Enable all multicast mode */
		at91_emac_write(AT91_EMAC_HSH, -1);
		at91_emac_write(AT91_EMAC_HSL, -1);
		cfg |= AT91_EMAC_MTI;
	} else if (dev->mc_count > 0) {			/* Enable specific multicasts */
		at91ether_sethashtable(dev);
		cfg |= AT91_EMAC_MTI;
	} else if (dev->flags & (~IFF_ALLMULTI)) {	/* Disable all multicast mode */
		at91_emac_write(AT91_EMAC_HSH, 0);
		at91_emac_write(AT91_EMAC_HSL, 0);
		cfg &= ~AT91_EMAC_MTI;
	}

	at91_emac_write(AT91_EMAC_CFG, cfg);
}

/* ......................... ETHTOOL SUPPORT ........................... */

static int mdio_read(struct net_device *dev, int phy_id, int location)
{
	unsigned int value;

	read_phy(phy_id, location, &value);
	return value;
}

static void mdio_write(struct net_device *dev, int phy_id, int location, int value)
{
	write_phy(phy_id, location, value);
}

static int at91ether_get_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct at91_private *lp = netdev_priv(dev);
	int ret;

	spin_lock_irq(&lp->lock);
	enable_mdi();

	ret = mii_ethtool_gset(&lp->mii, cmd);

	disable_mdi();
	spin_unlock_irq(&lp->lock);

	if (lp->phy_media == PORT_FIBRE) {		/* override media type since mii.c doesn't know */
		cmd->supported = SUPPORTED_FIBRE;
		cmd->port = PORT_FIBRE;
	}

	return ret;
}

static int at91ether_set_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct at91_private *lp = netdev_priv(dev);
	int ret;

	spin_lock_irq(&lp->lock);
	enable_mdi();

	ret = mii_ethtool_sset(&lp->mii, cmd);

	disable_mdi();
	spin_unlock_irq(&lp->lock);

	return ret;
}

static int at91ether_nwayreset(struct net_device *dev)
{
	struct at91_private *lp = netdev_priv(dev);
	int ret;

	spin_lock_irq(&lp->lock);
	enable_mdi();

	ret = mii_nway_restart(&lp->mii);

	disable_mdi();
	spin_unlock_irq(&lp->lock);

	return ret;
}

static void at91ether_get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *info)
{
	strlcpy(info->driver, DRV_NAME, sizeof(info->driver));
	strlcpy(info->version, DRV_VERSION, sizeof(info->version));
	strlcpy(info->bus_info, dev->dev.parent->bus_id, sizeof(info->bus_info));
}

static const struct ethtool_ops at91ether_ethtool_ops = {
	.get_settings	= at91ether_get_settings,
	.set_settings	= at91ether_set_settings,
	.get_drvinfo	= at91ether_get_drvinfo,
	.nway_reset	= at91ether_nwayreset,
	.get_link	= ethtool_op_get_link,
};

static int at91ether_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct at91_private *lp = netdev_priv(dev);
	int res;

	if (!netif_running(dev))
		return -EINVAL;

	spin_lock_irq(&lp->lock);
	enable_mdi();
	res = generic_mii_ioctl(&lp->mii, if_mii(rq), cmd, NULL);
	disable_mdi();
	spin_unlock_irq(&lp->lock);

	return res;
}

/* ................................ MAC ................................ */

/*
 * Initialize and start the Receiver and Transmit subsystems
 */
static void at91ether_start(struct net_device *dev)
{
	struct at91_private *lp = netdev_priv(dev);
	struct recv_desc_bufs *dlist, *dlist_phys;
	int i;
	unsigned long ctl;

	dlist = lp->dlist;
	dlist_phys = lp->dlist_phys;

	for (i = 0; i < MAX_RX_DESCR; i++) {
		dlist->descriptors[i].addr = (unsigned int) &dlist_phys->recv_buf[i][0];
		dlist->descriptors[i].size = 0;
	}

	/* Set the Wrap bit on the last descriptor */
	dlist->descriptors[i-1].addr |= EMAC_DESC_WRAP;

	/* Reset buffer index */
	lp->rxBuffIndex = 0;

	/* Program address of descriptor list in Rx Buffer Queue register */
	at91_emac_write(AT91_EMAC_RBQP, (unsigned long) dlist_phys);

	/* Enable Receive and Transmit */
	ctl = at91_emac_read(AT91_EMAC_CTL);
	at91_emac_write(AT91_EMAC_CTL, ctl | AT91_EMAC_RE | AT91_EMAC_TE);
}

/*
 * Open the ethernet interface
 */
static int at91ether_open(struct net_device *dev)
{
	struct at91_private *lp = netdev_priv(dev);
	unsigned long ctl;

	if (!is_valid_ether_addr(dev->dev_addr))
		return -EADDRNOTAVAIL;

	clk_enable(lp->ether_clk);		/* Re-enable Peripheral clock */

	/* Clear internal statistics */
	ctl = at91_emac_read(AT91_EMAC_CTL);
	at91_emac_write(AT91_EMAC_CTL, ctl | AT91_EMAC_CSR);

	/* Update the MAC address (incase user has changed it) */
	update_mac_address(dev);

	/* Enable PHY interrupt */
	enable_phyirq(dev);

	/* Enable MAC interrupts */
	at91_emac_write(AT91_EMAC_IER, AT91_EMAC_RCOM | AT91_EMAC_RBNA
				| AT91_EMAC_TUND | AT91_EMAC_RTRY | AT91_EMAC_TCOM
				| AT91_EMAC_ROVR | AT91_EMAC_ABT);

	/* Determine current link speed */
	spin_lock_irq(&lp->lock);
	enable_mdi();
	update_linkspeed(dev, 0);
	disable_mdi();
	spin_unlock_irq(&lp->lock);

	at91ether_start(dev);
	netif_start_queue(dev);
	return 0;
}

/*
 * Close the interface
 */
static int at91ether_close(struct net_device *dev)
{
	struct at91_private *lp = netdev_priv(dev);
	unsigned long ctl;

	/* Disable Receiver and Transmitter */
	ctl = at91_emac_read(AT91_EMAC_CTL);
	at91_emac_write(AT91_EMAC_CTL, ctl & ~(AT91_EMAC_TE | AT91_EMAC_RE));

	/* Disable PHY interrupt */
	disable_phyirq(dev);

	/* Disable MAC interrupts */
	at91_emac_write(AT91_EMAC_IDR, AT91_EMAC_RCOM | AT91_EMAC_RBNA
				| AT91_EMAC_TUND | AT91_EMAC_RTRY | AT91_EMAC_TCOM
				| AT91_EMAC_ROVR | AT91_EMAC_ABT);

	netif_stop_queue(dev);

	clk_disable(lp->ether_clk);		/* Disable Peripheral clock */

	return 0;
}

/*
 * Transmit packet.
 */
static int at91ether_tx(struct sk_buff *skb, struct net_device *dev)
{
	struct at91_private *lp = netdev_priv(dev);

	if (at91_emac_read(AT91_EMAC_TSR) & AT91_EMAC_TSR_BNQ) {
		netif_stop_queue(dev);

		/* Store packet information (to free when Tx completed) */
		lp->skb = skb;
		lp->skb_length = skb->len;
		lp->skb_physaddr = dma_map_single(NULL, skb->data, skb->len, DMA_TO_DEVICE);
		lp->stats.tx_bytes += skb->len;

		/* Set address of the data in the Transmit Address register */
		at91_emac_write(AT91_EMAC_TAR, lp->skb_physaddr);
		/* Set length of the packet in the Transmit Control register */
		at91_emac_write(AT91_EMAC_TCR, skb->len);

		dev->trans_start = jiffies;
	} else {
		printk(KERN_ERR "at91_ether.c: at91ether_tx() called, but device is busy!\n");
		return 1;	/* if we return anything but zero, dev.c:1055 calls kfree_skb(skb)
				on this skb, he also reports -ENETDOWN and printk's, so either
				we free and return(0) or don't free and return 1 */
	}

	return 0;
}

/*
 * Update the current statistics from the internal statistics registers.
 */
static struct net_device_stats *at91ether_stats(struct net_device *dev)
{
	struct at91_private *lp = netdev_priv(dev);
	int ale, lenerr, seqe, lcol, ecol;

	if (netif_running(dev)) {
		lp->stats.rx_packets += at91_emac_read(AT91_EMAC_OK);		/* Good frames received */
		ale = at91_emac_read(AT91_EMAC_ALE);
		lp->stats.rx_frame_errors += ale;				/* Alignment errors */
		lenerr = at91_emac_read(AT91_EMAC_ELR) + at91_emac_read(AT91_EMAC_USF);
		lp->stats.rx_length_errors += lenerr;				/* Excessive Length or Undersize Frame error */
		seqe = at91_emac_read(AT91_EMAC_SEQE);
		lp->stats.rx_crc_errors += seqe;				/* CRC error */
		lp->stats.rx_fifo_errors += at91_emac_read(AT91_EMAC_DRFC);	/* Receive buffer not available */
		lp->stats.rx_errors += (ale + lenerr + seqe
			+ at91_emac_read(AT91_EMAC_CDE) + at91_emac_read(AT91_EMAC_RJB));

		lp->stats.tx_packets += at91_emac_read(AT91_EMAC_FRA);		/* Frames successfully transmitted */
		lp->stats.tx_fifo_errors += at91_emac_read(AT91_EMAC_TUE);	/* Transmit FIFO underruns */
		lp->stats.tx_carrier_errors += at91_emac_read(AT91_EMAC_CSE);	/* Carrier Sense errors */
		lp->stats.tx_heartbeat_errors += at91_emac_read(AT91_EMAC_SQEE);/* Heartbeat error */

		lcol = at91_emac_read(AT91_EMAC_LCOL);
		ecol = at91_emac_read(AT91_EMAC_ECOL);
		lp->stats.tx_window_errors += lcol;			/* Late collisions */
		lp->stats.tx_aborted_errors += ecol;			/* 16 collisions */

		lp->stats.collisions += (at91_emac_read(AT91_EMAC_SCOL) + at91_emac_read(AT91_EMAC_MCOL) + lcol + ecol);
	}
	return &lp->stats;
}

/*
 * Extract received frame from buffer descriptors and sent to upper layers.
 * (Called from interrupt context)
 */
static void at91ether_rx(struct net_device *dev)
{
	struct at91_private *lp = netdev_priv(dev);
	struct recv_desc_bufs *dlist;
	unsigned char *p_recv;
	struct sk_buff *skb;
	unsigned int pktlen;

	dlist = lp->dlist;
	while (dlist->descriptors[lp->rxBuffIndex].addr & EMAC_DESC_DONE) {
		p_recv = dlist->recv_buf[lp->rxBuffIndex];
		pktlen = dlist->descriptors[lp->rxBuffIndex].size & 0x7ff;	/* Length of frame including FCS */
		skb = dev_alloc_skb(pktlen + 2);
		if (skb != NULL) {
			skb_reserve(skb, 2);
			memcpy(skb_put(skb, pktlen), p_recv, pktlen);

			skb->protocol = eth_type_trans(skb, dev);
			dev->last_rx = jiffies;
			lp->stats.rx_bytes += pktlen;
			netif_rx(skb);
		}
		else {
			lp->stats.rx_dropped += 1;
			printk(KERN_NOTICE "%s: Memory squeeze, dropping packet.\n", dev->name);
		}

		if (dlist->descriptors[lp->rxBuffIndex].size & EMAC_MULTICAST)
			lp->stats.multicast++;

		dlist->descriptors[lp->rxBuffIndex].addr &= ~EMAC_DESC_DONE;	/* reset ownership bit */
		if (lp->rxBuffIndex == MAX_RX_DESCR-1)				/* wrap after last buffer */
			lp->rxBuffIndex = 0;
		else
			lp->rxBuffIndex++;
	}
}

/*
 * MAC interrupt handler
 */
static irqreturn_t at91ether_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = (struct net_device *) dev_id;
	struct at91_private *lp = netdev_priv(dev);
	unsigned long intstatus, ctl;

	/* MAC Interrupt Status register indicates what interrupts are pending.
	   It is automatically cleared once read. */
	intstatus = at91_emac_read(AT91_EMAC_ISR);

	if (intstatus & AT91_EMAC_RCOM)		/* Receive complete */
		at91ether_rx(dev);

	if (intstatus & AT91_EMAC_TCOM) {	/* Transmit complete */
		/* The TCOM bit is set even if the transmission failed. */
		if (intstatus & (AT91_EMAC_TUND | AT91_EMAC_RTRY))
			lp->stats.tx_errors += 1;

		if (lp->skb) {
			dev_kfree_skb_irq(lp->skb);
			lp->skb = NULL;
			dma_unmap_single(NULL, lp->skb_physaddr, lp->skb_length, DMA_TO_DEVICE);
		}
		netif_wake_queue(dev);
	}

	/* Work-around for Errata #11 */
	if (intstatus & AT91_EMAC_RBNA) {
		ctl = at91_emac_read(AT91_EMAC_CTL);
		at91_emac_write(AT91_EMAC_CTL, ctl & ~AT91_EMAC_RE);
		at91_emac_write(AT91_EMAC_CTL, ctl | AT91_EMAC_RE);
	}

	if (intstatus & AT91_EMAC_ROVR)
		printk("%s: ROVR error\n", dev->name);

	return IRQ_HANDLED;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void at91ether_poll_controller(struct net_device *dev)
{
	unsigned long flags;

	local_irq_save(flags);
	at91ether_interrupt(dev->irq, dev);
	local_irq_restore(flags);
}
#endif

/*
 * Initialize the ethernet interface
 */
static int __init at91ether_setup(unsigned long phy_type, unsigned short phy_address,
			struct platform_device *pdev, struct clk *ether_clk)
{
	struct at91_eth_data *board_data = pdev->dev.platform_data;
	struct net_device *dev;
	struct at91_private *lp;
	unsigned int val;
	int res;
	DECLARE_MAC_BUF(mac);

	dev = alloc_etherdev(sizeof(struct at91_private));
	if (!dev)
		return -ENOMEM;

	dev->base_addr = AT91_VA_BASE_EMAC;
	dev->irq = AT91RM9200_ID_EMAC;

	/* Install the interrupt handler */
	if (request_irq(dev->irq, at91ether_interrupt, 0, dev->name, dev)) {
		free_netdev(dev);
		return -EBUSY;
	}

	/* Allocate memory for DMA Receive descriptors */
	lp = netdev_priv(dev);
	lp->dlist = (struct recv_desc_bufs *) dma_alloc_coherent(NULL, sizeof(struct recv_desc_bufs), (dma_addr_t *) &lp->dlist_phys, GFP_KERNEL);
	if (lp->dlist == NULL) {
		free_irq(dev->irq, dev);
		free_netdev(dev);
		return -ENOMEM;
	}
	lp->board_data = *board_data;
	lp->ether_clk = ether_clk;
	platform_set_drvdata(pdev, dev);

	spin_lock_init(&lp->lock);

	ether_setup(dev);
	dev->open = at91ether_open;
	dev->stop = at91ether_close;
	dev->hard_start_xmit = at91ether_tx;
	dev->get_stats = at91ether_stats;
	dev->set_multicast_list = at91ether_set_rx_mode;
	dev->set_mac_address = set_mac_address;
	dev->ethtool_ops = &at91ether_ethtool_ops;
	dev->do_ioctl = at91ether_ioctl;
#ifdef CONFIG_NET_POLL_CONTROLLER
	dev->poll_controller = at91ether_poll_controller;
#endif

	SET_NETDEV_DEV(dev, &pdev->dev);

	get_mac_address(dev);		/* Get ethernet address and store it in dev->dev_addr */
	update_mac_address(dev);	/* Program ethernet address into MAC */

	at91_emac_write(AT91_EMAC_CTL, 0);

	if (lp->board_data.is_rmii)
		at91_emac_write(AT91_EMAC_CFG, AT91_EMAC_CLK_DIV32 | AT91_EMAC_BIG | AT91_EMAC_RMII);
	else
		at91_emac_write(AT91_EMAC_CFG, AT91_EMAC_CLK_DIV32 | AT91_EMAC_BIG);

	/* Perform PHY-specific initialization */
	spin_lock_irq(&lp->lock);
	enable_mdi();
	if ((phy_type == MII_DM9161_ID) || (lp->phy_type == MII_DM9161A_ID)) {
		read_phy(phy_address, MII_DSCR_REG, &val);
		if ((val & (1 << 10)) == 0)			/* DSCR bit 10 is 0 -- fiber mode */
			lp->phy_media = PORT_FIBRE;
	} else if (machine_is_csb337()) {
		/* mix link activity status into LED2 link state */
		write_phy(phy_address, MII_LEDCTRL_REG, 0x0d22);
	}
	disable_mdi();
	spin_unlock_irq(&lp->lock);

	lp->mii.dev = dev;		/* Support for ethtool */
	lp->mii.mdio_read = mdio_read;
	lp->mii.mdio_write = mdio_write;
	lp->mii.phy_id = phy_address;
	lp->mii.phy_id_mask = 0x1f;
	lp->mii.reg_num_mask = 0x1f;

	lp->phy_type = phy_type;	/* Type of PHY connected */
	lp->phy_address = phy_address;	/* MDI address of PHY */

	/* Register the network interface */
	res = register_netdev(dev);
	if (res) {
		free_irq(dev->irq, dev);
		free_netdev(dev);
		dma_free_coherent(NULL, sizeof(struct recv_desc_bufs), lp->dlist, (dma_addr_t)lp->dlist_phys);
		return res;
	}

	/* Determine current link speed */
	spin_lock_irq(&lp->lock);
	enable_mdi();
	update_linkspeed(dev, 0);
	disable_mdi();
	spin_unlock_irq(&lp->lock);
	netif_carrier_off(dev);		/* will be enabled in open() */

	/* If board has no PHY IRQ, use a timer to poll the PHY */
	if (!lp->board_data.phy_irq_pin) {
		init_timer(&lp->check_timer);
		lp->check_timer.data = (unsigned long)dev;
		lp->check_timer.function = at91ether_check_link;
	}

	/* Display ethernet banner */
	printk(KERN_INFO "%s: AT91 ethernet at 0x%08x int=%d %s%s (%s)\n",
	       dev->name, (uint) dev->base_addr, dev->irq,
	       at91_emac_read(AT91_EMAC_CFG) & AT91_EMAC_SPD ? "100-" : "10-",
	       at91_emac_read(AT91_EMAC_CFG) & AT91_EMAC_FD ? "FullDuplex" : "HalfDuplex",
	       print_mac(mac, dev->dev_addr));
	if ((phy_type == MII_DM9161_ID) || (lp->phy_type == MII_DM9161A_ID))
		printk(KERN_INFO "%s: Davicom 9161 PHY %s\n", dev->name, (lp->phy_media == PORT_FIBRE) ? "(Fiber)" : "(Copper)");
	else if (phy_type == MII_LXT971A_ID)
		printk(KERN_INFO "%s: Intel LXT971A PHY\n", dev->name);
	else if (phy_type == MII_RTL8201_ID)
		printk(KERN_INFO "%s: Realtek RTL8201(B)L PHY\n", dev->name);
	else if (phy_type == MII_BCM5221_ID)
		printk(KERN_INFO "%s: Broadcom BCM5221 PHY\n", dev->name);
	else if (phy_type == MII_DP83847_ID)
		printk(KERN_INFO "%s: National Semiconductor DP83847 PHY\n", dev->name);
	else if (phy_type == MII_DP83848_ID)
		printk(KERN_INFO "%s: National Semiconductor DP83848 PHY\n", dev->name);
	else if (phy_type == MII_AC101L_ID)
		printk(KERN_INFO "%s: Altima AC101L PHY\n", dev->name);
	else if (phy_type == MII_KS8721_ID)
		printk(KERN_INFO "%s: Micrel KS8721 PHY\n", dev->name);
	else if (phy_type == MII_T78Q21x3_ID)
		printk(KERN_INFO "%s: Teridian 78Q21x3 PHY\n", dev->name);
	else if (phy_type == MII_LAN83C185_ID)
		printk(KERN_INFO "%s: SMSC LAN83C185 PHY\n", dev->name);

	return 0;
}

/*
 * Detect MAC and PHY and perform initialization
 */
static int __init at91ether_probe(struct platform_device *pdev)
{
	unsigned int phyid1, phyid2;
	int detected = -1;
	unsigned long phy_id;
	unsigned short phy_address = 0;
	struct clk *ether_clk;

	ether_clk = clk_get(&pdev->dev, "ether_clk");
	if (IS_ERR(ether_clk)) {
		printk(KERN_ERR "at91_ether: no clock defined\n");
		return -ENODEV;
	}
	clk_enable(ether_clk);					/* Enable Peripheral clock */

	while ((detected != 0) && (phy_address < 32)) {
		/* Read the PHY ID registers */
		enable_mdi();
		read_phy(phy_address, MII_PHYSID1, &phyid1);
		read_phy(phy_address, MII_PHYSID2, &phyid2);
		disable_mdi();

		phy_id = (phyid1 << 16) | (phyid2 & 0xfff0);
		switch (phy_id) {
			case MII_DM9161_ID:		/* Davicom 9161: PHY_ID1 = 0x181, PHY_ID2 = B881 */
			case MII_DM9161A_ID:		/* Davicom 9161A: PHY_ID1 = 0x181, PHY_ID2 = B8A0 */
			case MII_LXT971A_ID:		/* Intel LXT971A: PHY_ID1 = 0x13, PHY_ID2 = 78E0 */
			case MII_RTL8201_ID:		/* Realtek RTL8201: PHY_ID1 = 0, PHY_ID2 = 0x8201 */
			case MII_BCM5221_ID:		/* Broadcom BCM5221: PHY_ID1 = 0x40, PHY_ID2 = 0x61e0 */
			case MII_DP83847_ID:		/* National Semiconductor DP83847:  */
			case MII_DP83848_ID:		/* National Semiconductor DP83848:  */
			case MII_AC101L_ID:		/* Altima AC101L: PHY_ID1 = 0x22, PHY_ID2 = 0x5520 */
			case MII_KS8721_ID:		/* Micrel KS8721: PHY_ID1 = 0x22, PHY_ID2 = 0x1610 */
			case MII_T78Q21x3_ID:		/* Teridian 78Q21x3: PHY_ID1 = 0x0E, PHY_ID2 = 7237 */
			case MII_LAN83C185_ID:		/* SMSC LAN83C185: PHY_ID1 = 0x0007, PHY_ID2 = 0xC0A1 */
				detected = at91ether_setup(phy_id, phy_address, pdev, ether_clk);
				break;
		}

		phy_address++;
	}

	clk_disable(ether_clk);					/* Disable Peripheral clock */

	return detected;
}

static int __devexit at91ether_remove(struct platform_device *pdev)
{
	struct net_device *dev = platform_get_drvdata(pdev);
	struct at91_private *lp = netdev_priv(dev);

	unregister_netdev(dev);
	free_irq(dev->irq, dev);
	dma_free_coherent(NULL, sizeof(struct recv_desc_bufs), lp->dlist, (dma_addr_t)lp->dlist_phys);
	clk_put(lp->ether_clk);

	platform_set_drvdata(pdev, NULL);
	free_netdev(dev);
	return 0;
}

#ifdef CONFIG_PM

static int at91ether_suspend(struct platform_device *pdev, pm_message_t mesg)
{
	struct net_device *net_dev = platform_get_drvdata(pdev);
	struct at91_private *lp = netdev_priv(net_dev);
	int phy_irq = lp->board_data.phy_irq_pin;

	if (netif_running(net_dev)) {
		if (phy_irq)
			disable_irq(phy_irq);

		netif_stop_queue(net_dev);
		netif_device_detach(net_dev);

		clk_disable(lp->ether_clk);
	}
	return 0;
}

static int at91ether_resume(struct platform_device *pdev)
{
	struct net_device *net_dev = platform_get_drvdata(pdev);
	struct at91_private *lp = netdev_priv(net_dev);
	int phy_irq = lp->board_data.phy_irq_pin;

	if (netif_running(net_dev)) {
		clk_enable(lp->ether_clk);

		netif_device_attach(net_dev);
		netif_start_queue(net_dev);

		if (phy_irq)
			enable_irq(phy_irq);
	}
	return 0;
}

#else
#define at91ether_suspend	NULL
#define at91ether_resume	NULL
#endif

static struct platform_driver at91ether_driver = {
	.probe		= at91ether_probe,
	.remove		= __devexit_p(at91ether_remove),
	.suspend	= at91ether_suspend,
	.resume		= at91ether_resume,
	.driver		= {
		.name	= DRV_NAME,
		.owner	= THIS_MODULE,
	},
};

static int __init at91ether_init(void)
{
	return platform_driver_register(&at91ether_driver);
}

static void __exit at91ether_exit(void)
{
	platform_driver_unregister(&at91ether_driver);
}

module_init(at91ether_init)
module_exit(at91ether_exit)

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("AT91RM9200 EMAC Ethernet driver");
MODULE_AUTHOR("Andrew Victor");

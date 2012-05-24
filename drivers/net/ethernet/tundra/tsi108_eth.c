/*******************************************************************************

  Copyright(c) 2006 Tundra Semiconductor Corporation.

  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the Free
  Software Foundation; either version 2 of the License, or (at your option)
  any later version.

  This program is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc., 59
  Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*******************************************************************************/

/* This driver is based on the driver code originally developed
 * for the Intel IOC80314 (ForestLake) Gigabit Ethernet by
 * scott.wood@timesys.com  * Copyright (C) 2003 TimeSys Corporation
 *
 * Currently changes from original version are:
 * - porting to Tsi108-based platform and kernel 2.6 (kong.lai@tundra.com)
 * - modifications to handle two ports independently and support for
 *   additional PHY devices (alexandre.bounine@tundra.com)
 * - Get hardware information from platform device. (tie-fei.zang@freescale.com)
 *
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/net.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/crc32.h>
#include <linux/mii.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <linux/rtnetlink.h>
#include <linux/timer.h>
#include <linux/platform_device.h>
#include <linux/gfp.h>

#include <asm/io.h>
#include <asm/tsi108.h>

#include "tsi108_eth.h"

#define MII_READ_DELAY 10000	/* max link wait time in msec */

#define TSI108_RXRING_LEN     256

/* NOTE: The driver currently does not support receiving packets
 * larger than the buffer size, so don't decrease this (unless you
 * want to add such support).
 */
#define TSI108_RXBUF_SIZE     1536

#define TSI108_TXRING_LEN     256

#define TSI108_TX_INT_FREQ    64

/* Check the phy status every half a second. */
#define CHECK_PHY_INTERVAL (HZ/2)

static int tsi108_init_one(struct platform_device *pdev);
static int tsi108_ether_remove(struct platform_device *pdev);

struct tsi108_prv_data {
	void  __iomem *regs;	/* Base of normal regs */
	void  __iomem *phyregs;	/* Base of register bank used for PHY access */

	struct net_device *dev;
	struct napi_struct napi;

	unsigned int phy;		/* Index of PHY for this interface */
	unsigned int irq_num;
	unsigned int id;
	unsigned int phy_type;

	struct timer_list timer;/* Timer that triggers the check phy function */
	unsigned int rxtail;	/* Next entry in rxring to read */
	unsigned int rxhead;	/* Next entry in rxring to give a new buffer */
	unsigned int rxfree;	/* Number of free, allocated RX buffers */

	unsigned int rxpending;	/* Non-zero if there are still descriptors
				 * to be processed from a previous descriptor
				 * interrupt condition that has been cleared */

	unsigned int txtail;	/* Next TX descriptor to check status on */
	unsigned int txhead;	/* Next TX descriptor to use */

	/* Number of free TX descriptors.  This could be calculated from
	 * rxhead and rxtail if one descriptor were left unused to disambiguate
	 * full and empty conditions, but it's simpler to just keep track
	 * explicitly. */

	unsigned int txfree;

	unsigned int phy_ok;		/* The PHY is currently powered on. */

	/* PHY status (duplex is 1 for half, 2 for full,
	 * so that the default 0 indicates that neither has
	 * yet been configured). */

	unsigned int link_up;
	unsigned int speed;
	unsigned int duplex;

	tx_desc *txring;
	rx_desc *rxring;
	struct sk_buff *txskbs[TSI108_TXRING_LEN];
	struct sk_buff *rxskbs[TSI108_RXRING_LEN];

	dma_addr_t txdma, rxdma;

	/* txlock nests in misclock and phy_lock */

	spinlock_t txlock, misclock;

	/* stats is used to hold the upper bits of each hardware counter,
	 * and tmpstats is used to hold the full values for returning
	 * to the caller of get_stats().  They must be separate in case
	 * an overflow interrupt occurs before the stats are consumed.
	 */

	struct net_device_stats stats;
	struct net_device_stats tmpstats;

	/* These stats are kept separate in hardware, thus require individual
	 * fields for handling carry.  They are combined in get_stats.
	 */

	unsigned long rx_fcs;	/* Add to rx_frame_errors */
	unsigned long rx_short_fcs;	/* Add to rx_frame_errors */
	unsigned long rx_long_fcs;	/* Add to rx_frame_errors */
	unsigned long rx_underruns;	/* Add to rx_length_errors */
	unsigned long rx_overruns;	/* Add to rx_length_errors */

	unsigned long tx_coll_abort;	/* Add to tx_aborted_errors/collisions */
	unsigned long tx_pause_drop;	/* Add to tx_aborted_errors */

	unsigned long mc_hash[16];
	u32 msg_enable;			/* debug message level */
	struct mii_if_info mii_if;
	unsigned int init_media;
};

/* Structure for a device driver */

static struct platform_driver tsi_eth_driver = {
	.probe = tsi108_init_one,
	.remove = tsi108_ether_remove,
	.driver	= {
		.name = "tsi-ethernet",
		.owner = THIS_MODULE,
	},
};

static void tsi108_timed_checker(unsigned long dev_ptr);

static void dump_eth_one(struct net_device *dev)
{
	struct tsi108_prv_data *data = netdev_priv(dev);

	printk("Dumping %s...\n", dev->name);
	printk("intstat %x intmask %x phy_ok %d"
	       " link %d speed %d duplex %d\n",
	       TSI_READ(TSI108_EC_INTSTAT),
	       TSI_READ(TSI108_EC_INTMASK), data->phy_ok,
	       data->link_up, data->speed, data->duplex);

	printk("TX: head %d, tail %d, free %d, stat %x, estat %x, err %x\n",
	       data->txhead, data->txtail, data->txfree,
	       TSI_READ(TSI108_EC_TXSTAT),
	       TSI_READ(TSI108_EC_TXESTAT),
	       TSI_READ(TSI108_EC_TXERR));

	printk("RX: head %d, tail %d, free %d, stat %x,"
	       " estat %x, err %x, pending %d\n\n",
	       data->rxhead, data->rxtail, data->rxfree,
	       TSI_READ(TSI108_EC_RXSTAT),
	       TSI_READ(TSI108_EC_RXESTAT),
	       TSI_READ(TSI108_EC_RXERR), data->rxpending);
}

/* Synchronization is needed between the thread and up/down events.
 * Note that the PHY is accessed through the same registers for both
 * interfaces, so this can't be made interface-specific.
 */

static DEFINE_SPINLOCK(phy_lock);

static int tsi108_read_mii(struct tsi108_prv_data *data, int reg)
{
	unsigned i;

	TSI_WRITE_PHY(TSI108_MAC_MII_ADDR,
				(data->phy << TSI108_MAC_MII_ADDR_PHY) |
				(reg << TSI108_MAC_MII_ADDR_REG));
	TSI_WRITE_PHY(TSI108_MAC_MII_CMD, 0);
	TSI_WRITE_PHY(TSI108_MAC_MII_CMD, TSI108_MAC_MII_CMD_READ);
	for (i = 0; i < 100; i++) {
		if (!(TSI_READ_PHY(TSI108_MAC_MII_IND) &
		      (TSI108_MAC_MII_IND_NOTVALID | TSI108_MAC_MII_IND_BUSY)))
			break;
		udelay(10);
	}

	if (i == 100)
		return 0xffff;
	else
		return TSI_READ_PHY(TSI108_MAC_MII_DATAIN);
}

static void tsi108_write_mii(struct tsi108_prv_data *data,
				int reg, u16 val)
{
	unsigned i = 100;
	TSI_WRITE_PHY(TSI108_MAC_MII_ADDR,
				(data->phy << TSI108_MAC_MII_ADDR_PHY) |
				(reg << TSI108_MAC_MII_ADDR_REG));
	TSI_WRITE_PHY(TSI108_MAC_MII_DATAOUT, val);
	while (i--) {
		if(!(TSI_READ_PHY(TSI108_MAC_MII_IND) &
			TSI108_MAC_MII_IND_BUSY))
			break;
		udelay(10);
	}
}

static int tsi108_mdio_read(struct net_device *dev, int addr, int reg)
{
	struct tsi108_prv_data *data = netdev_priv(dev);
	return tsi108_read_mii(data, reg);
}

static void tsi108_mdio_write(struct net_device *dev, int addr, int reg, int val)
{
	struct tsi108_prv_data *data = netdev_priv(dev);
	tsi108_write_mii(data, reg, val);
}

static inline void tsi108_write_tbi(struct tsi108_prv_data *data,
					int reg, u16 val)
{
	unsigned i = 1000;
	TSI_WRITE(TSI108_MAC_MII_ADDR,
			     (0x1e << TSI108_MAC_MII_ADDR_PHY)
			     | (reg << TSI108_MAC_MII_ADDR_REG));
	TSI_WRITE(TSI108_MAC_MII_DATAOUT, val);
	while(i--) {
		if(!(TSI_READ(TSI108_MAC_MII_IND) & TSI108_MAC_MII_IND_BUSY))
			return;
		udelay(10);
	}
	printk(KERN_ERR "%s function time out\n", __func__);
}

static int mii_speed(struct mii_if_info *mii)
{
	int advert, lpa, val, media;
	int lpa2 = 0;
	int speed;

	if (!mii_link_ok(mii))
		return 0;

	val = (*mii->mdio_read) (mii->dev, mii->phy_id, MII_BMSR);
	if ((val & BMSR_ANEGCOMPLETE) == 0)
		return 0;

	advert = (*mii->mdio_read) (mii->dev, mii->phy_id, MII_ADVERTISE);
	lpa = (*mii->mdio_read) (mii->dev, mii->phy_id, MII_LPA);
	media = mii_nway_result(advert & lpa);

	if (mii->supports_gmii)
		lpa2 = mii->mdio_read(mii->dev, mii->phy_id, MII_STAT1000);

	speed = lpa2 & (LPA_1000FULL | LPA_1000HALF) ? 1000 :
			(media & (ADVERTISE_100FULL | ADVERTISE_100HALF) ? 100 : 10);
	return speed;
}

static void tsi108_check_phy(struct net_device *dev)
{
	struct tsi108_prv_data *data = netdev_priv(dev);
	u32 mac_cfg2_reg, portctrl_reg;
	u32 duplex;
	u32 speed;
	unsigned long flags;

	spin_lock_irqsave(&phy_lock, flags);

	if (!data->phy_ok)
		goto out;

	duplex = mii_check_media(&data->mii_if, netif_msg_link(data), data->init_media);
	data->init_media = 0;

	if (netif_carrier_ok(dev)) {

		speed = mii_speed(&data->mii_if);

		if ((speed != data->speed) || duplex) {

			mac_cfg2_reg = TSI_READ(TSI108_MAC_CFG2);
			portctrl_reg = TSI_READ(TSI108_EC_PORTCTRL);

			mac_cfg2_reg &= ~TSI108_MAC_CFG2_IFACE_MASK;

			if (speed == 1000) {
				mac_cfg2_reg |= TSI108_MAC_CFG2_GIG;
				portctrl_reg &= ~TSI108_EC_PORTCTRL_NOGIG;
			} else {
				mac_cfg2_reg |= TSI108_MAC_CFG2_NOGIG;
				portctrl_reg |= TSI108_EC_PORTCTRL_NOGIG;
			}

			data->speed = speed;

			if (data->mii_if.full_duplex) {
				mac_cfg2_reg |= TSI108_MAC_CFG2_FULLDUPLEX;
				portctrl_reg &= ~TSI108_EC_PORTCTRL_HALFDUPLEX;
				data->duplex = 2;
			} else {
				mac_cfg2_reg &= ~TSI108_MAC_CFG2_FULLDUPLEX;
				portctrl_reg |= TSI108_EC_PORTCTRL_HALFDUPLEX;
				data->duplex = 1;
			}

			TSI_WRITE(TSI108_MAC_CFG2, mac_cfg2_reg);
			TSI_WRITE(TSI108_EC_PORTCTRL, portctrl_reg);
		}

		if (data->link_up == 0) {
			/* The manual says it can take 3-4 usecs for the speed change
			 * to take effect.
			 */
			udelay(5);

			spin_lock(&data->txlock);
			if (is_valid_ether_addr(dev->dev_addr) && data->txfree)
				netif_wake_queue(dev);

			data->link_up = 1;
			spin_unlock(&data->txlock);
		}
	} else {
		if (data->link_up == 1) {
			netif_stop_queue(dev);
			data->link_up = 0;
			printk(KERN_NOTICE "%s : link is down\n", dev->name);
		}

		goto out;
	}


out:
	spin_unlock_irqrestore(&phy_lock, flags);
}

static inline void
tsi108_stat_carry_one(int carry, int carry_bit, int carry_shift,
		      unsigned long *upper)
{
	if (carry & carry_bit)
		*upper += carry_shift;
}

static void tsi108_stat_carry(struct net_device *dev)
{
	struct tsi108_prv_data *data = netdev_priv(dev);
	u32 carry1, carry2;

	spin_lock_irq(&data->misclock);

	carry1 = TSI_READ(TSI108_STAT_CARRY1);
	carry2 = TSI_READ(TSI108_STAT_CARRY2);

	TSI_WRITE(TSI108_STAT_CARRY1, carry1);
	TSI_WRITE(TSI108_STAT_CARRY2, carry2);

	tsi108_stat_carry_one(carry1, TSI108_STAT_CARRY1_RXBYTES,
			      TSI108_STAT_RXBYTES_CARRY, &data->stats.rx_bytes);

	tsi108_stat_carry_one(carry1, TSI108_STAT_CARRY1_RXPKTS,
			      TSI108_STAT_RXPKTS_CARRY,
			      &data->stats.rx_packets);

	tsi108_stat_carry_one(carry1, TSI108_STAT_CARRY1_RXFCS,
			      TSI108_STAT_RXFCS_CARRY, &data->rx_fcs);

	tsi108_stat_carry_one(carry1, TSI108_STAT_CARRY1_RXMCAST,
			      TSI108_STAT_RXMCAST_CARRY,
			      &data->stats.multicast);

	tsi108_stat_carry_one(carry1, TSI108_STAT_CARRY1_RXALIGN,
			      TSI108_STAT_RXALIGN_CARRY,
			      &data->stats.rx_frame_errors);

	tsi108_stat_carry_one(carry1, TSI108_STAT_CARRY1_RXLENGTH,
			      TSI108_STAT_RXLENGTH_CARRY,
			      &data->stats.rx_length_errors);

	tsi108_stat_carry_one(carry1, TSI108_STAT_CARRY1_RXRUNT,
			      TSI108_STAT_RXRUNT_CARRY, &data->rx_underruns);

	tsi108_stat_carry_one(carry1, TSI108_STAT_CARRY1_RXJUMBO,
			      TSI108_STAT_RXJUMBO_CARRY, &data->rx_overruns);

	tsi108_stat_carry_one(carry1, TSI108_STAT_CARRY1_RXFRAG,
			      TSI108_STAT_RXFRAG_CARRY, &data->rx_short_fcs);

	tsi108_stat_carry_one(carry1, TSI108_STAT_CARRY1_RXJABBER,
			      TSI108_STAT_RXJABBER_CARRY, &data->rx_long_fcs);

	tsi108_stat_carry_one(carry1, TSI108_STAT_CARRY1_RXDROP,
			      TSI108_STAT_RXDROP_CARRY,
			      &data->stats.rx_missed_errors);

	tsi108_stat_carry_one(carry2, TSI108_STAT_CARRY2_TXBYTES,
			      TSI108_STAT_TXBYTES_CARRY, &data->stats.tx_bytes);

	tsi108_stat_carry_one(carry2, TSI108_STAT_CARRY2_TXPKTS,
			      TSI108_STAT_TXPKTS_CARRY,
			      &data->stats.tx_packets);

	tsi108_stat_carry_one(carry2, TSI108_STAT_CARRY2_TXEXDEF,
			      TSI108_STAT_TXEXDEF_CARRY,
			      &data->stats.tx_aborted_errors);

	tsi108_stat_carry_one(carry2, TSI108_STAT_CARRY2_TXEXCOL,
			      TSI108_STAT_TXEXCOL_CARRY, &data->tx_coll_abort);

	tsi108_stat_carry_one(carry2, TSI108_STAT_CARRY2_TXTCOL,
			      TSI108_STAT_TXTCOL_CARRY,
			      &data->stats.collisions);

	tsi108_stat_carry_one(carry2, TSI108_STAT_CARRY2_TXPAUSE,
			      TSI108_STAT_TXPAUSEDROP_CARRY,
			      &data->tx_pause_drop);

	spin_unlock_irq(&data->misclock);
}

/* Read a stat counter atomically with respect to carries.
 * data->misclock must be held.
 */
static inline unsigned long
tsi108_read_stat(struct tsi108_prv_data * data, int reg, int carry_bit,
		 int carry_shift, unsigned long *upper)
{
	int carryreg;
	unsigned long val;

	if (reg < 0xb0)
		carryreg = TSI108_STAT_CARRY1;
	else
		carryreg = TSI108_STAT_CARRY2;

      again:
	val = TSI_READ(reg) | *upper;

	/* Check to see if it overflowed, but the interrupt hasn't
	 * been serviced yet.  If so, handle the carry here, and
	 * try again.
	 */

	if (unlikely(TSI_READ(carryreg) & carry_bit)) {
		*upper += carry_shift;
		TSI_WRITE(carryreg, carry_bit);
		goto again;
	}

	return val;
}

static struct net_device_stats *tsi108_get_stats(struct net_device *dev)
{
	unsigned long excol;

	struct tsi108_prv_data *data = netdev_priv(dev);
	spin_lock_irq(&data->misclock);

	data->tmpstats.rx_packets =
	    tsi108_read_stat(data, TSI108_STAT_RXPKTS,
			     TSI108_STAT_CARRY1_RXPKTS,
			     TSI108_STAT_RXPKTS_CARRY, &data->stats.rx_packets);

	data->tmpstats.tx_packets =
	    tsi108_read_stat(data, TSI108_STAT_TXPKTS,
			     TSI108_STAT_CARRY2_TXPKTS,
			     TSI108_STAT_TXPKTS_CARRY, &data->stats.tx_packets);

	data->tmpstats.rx_bytes =
	    tsi108_read_stat(data, TSI108_STAT_RXBYTES,
			     TSI108_STAT_CARRY1_RXBYTES,
			     TSI108_STAT_RXBYTES_CARRY, &data->stats.rx_bytes);

	data->tmpstats.tx_bytes =
	    tsi108_read_stat(data, TSI108_STAT_TXBYTES,
			     TSI108_STAT_CARRY2_TXBYTES,
			     TSI108_STAT_TXBYTES_CARRY, &data->stats.tx_bytes);

	data->tmpstats.multicast =
	    tsi108_read_stat(data, TSI108_STAT_RXMCAST,
			     TSI108_STAT_CARRY1_RXMCAST,
			     TSI108_STAT_RXMCAST_CARRY, &data->stats.multicast);

	excol = tsi108_read_stat(data, TSI108_STAT_TXEXCOL,
				 TSI108_STAT_CARRY2_TXEXCOL,
				 TSI108_STAT_TXEXCOL_CARRY,
				 &data->tx_coll_abort);

	data->tmpstats.collisions =
	    tsi108_read_stat(data, TSI108_STAT_TXTCOL,
			     TSI108_STAT_CARRY2_TXTCOL,
			     TSI108_STAT_TXTCOL_CARRY, &data->stats.collisions);

	data->tmpstats.collisions += excol;

	data->tmpstats.rx_length_errors =
	    tsi108_read_stat(data, TSI108_STAT_RXLENGTH,
			     TSI108_STAT_CARRY1_RXLENGTH,
			     TSI108_STAT_RXLENGTH_CARRY,
			     &data->stats.rx_length_errors);

	data->tmpstats.rx_length_errors +=
	    tsi108_read_stat(data, TSI108_STAT_RXRUNT,
			     TSI108_STAT_CARRY1_RXRUNT,
			     TSI108_STAT_RXRUNT_CARRY, &data->rx_underruns);

	data->tmpstats.rx_length_errors +=
	    tsi108_read_stat(data, TSI108_STAT_RXJUMBO,
			     TSI108_STAT_CARRY1_RXJUMBO,
			     TSI108_STAT_RXJUMBO_CARRY, &data->rx_overruns);

	data->tmpstats.rx_frame_errors =
	    tsi108_read_stat(data, TSI108_STAT_RXALIGN,
			     TSI108_STAT_CARRY1_RXALIGN,
			     TSI108_STAT_RXALIGN_CARRY,
			     &data->stats.rx_frame_errors);

	data->tmpstats.rx_frame_errors +=
	    tsi108_read_stat(data, TSI108_STAT_RXFCS,
			     TSI108_STAT_CARRY1_RXFCS, TSI108_STAT_RXFCS_CARRY,
			     &data->rx_fcs);

	data->tmpstats.rx_frame_errors +=
	    tsi108_read_stat(data, TSI108_STAT_RXFRAG,
			     TSI108_STAT_CARRY1_RXFRAG,
			     TSI108_STAT_RXFRAG_CARRY, &data->rx_short_fcs);

	data->tmpstats.rx_missed_errors =
	    tsi108_read_stat(data, TSI108_STAT_RXDROP,
			     TSI108_STAT_CARRY1_RXDROP,
			     TSI108_STAT_RXDROP_CARRY,
			     &data->stats.rx_missed_errors);

	/* These three are maintained by software. */
	data->tmpstats.rx_fifo_errors = data->stats.rx_fifo_errors;
	data->tmpstats.rx_crc_errors = data->stats.rx_crc_errors;

	data->tmpstats.tx_aborted_errors =
	    tsi108_read_stat(data, TSI108_STAT_TXEXDEF,
			     TSI108_STAT_CARRY2_TXEXDEF,
			     TSI108_STAT_TXEXDEF_CARRY,
			     &data->stats.tx_aborted_errors);

	data->tmpstats.tx_aborted_errors +=
	    tsi108_read_stat(data, TSI108_STAT_TXPAUSEDROP,
			     TSI108_STAT_CARRY2_TXPAUSE,
			     TSI108_STAT_TXPAUSEDROP_CARRY,
			     &data->tx_pause_drop);

	data->tmpstats.tx_aborted_errors += excol;

	data->tmpstats.tx_errors = data->tmpstats.tx_aborted_errors;
	data->tmpstats.rx_errors = data->tmpstats.rx_length_errors +
	    data->tmpstats.rx_crc_errors +
	    data->tmpstats.rx_frame_errors +
	    data->tmpstats.rx_fifo_errors + data->tmpstats.rx_missed_errors;

	spin_unlock_irq(&data->misclock);
	return &data->tmpstats;
}

static void tsi108_restart_rx(struct tsi108_prv_data * data, struct net_device *dev)
{
	TSI_WRITE(TSI108_EC_RXQ_PTRHIGH,
			     TSI108_EC_RXQ_PTRHIGH_VALID);

	TSI_WRITE(TSI108_EC_RXCTRL, TSI108_EC_RXCTRL_GO
			     | TSI108_EC_RXCTRL_QUEUE0);
}

static void tsi108_restart_tx(struct tsi108_prv_data * data)
{
	TSI_WRITE(TSI108_EC_TXQ_PTRHIGH,
			     TSI108_EC_TXQ_PTRHIGH_VALID);

	TSI_WRITE(TSI108_EC_TXCTRL, TSI108_EC_TXCTRL_IDLEINT |
			     TSI108_EC_TXCTRL_GO | TSI108_EC_TXCTRL_QUEUE0);
}

/* txlock must be held by caller, with IRQs disabled, and
 * with permission to re-enable them when the lock is dropped.
 */
static void tsi108_complete_tx(struct net_device *dev)
{
	struct tsi108_prv_data *data = netdev_priv(dev);
	int tx;
	struct sk_buff *skb;
	int release = 0;

	while (!data->txfree || data->txhead != data->txtail) {
		tx = data->txtail;

		if (data->txring[tx].misc & TSI108_TX_OWN)
			break;

		skb = data->txskbs[tx];

		if (!(data->txring[tx].misc & TSI108_TX_OK))
			printk("%s: bad tx packet, misc %x\n",
			       dev->name, data->txring[tx].misc);

		data->txtail = (data->txtail + 1) % TSI108_TXRING_LEN;
		data->txfree++;

		if (data->txring[tx].misc & TSI108_TX_EOF) {
			dev_kfree_skb_any(skb);
			release++;
		}
	}

	if (release) {
		if (is_valid_ether_addr(dev->dev_addr) && data->link_up)
			netif_wake_queue(dev);
	}
}

static int tsi108_send_packet(struct sk_buff * skb, struct net_device *dev)
{
	struct tsi108_prv_data *data = netdev_priv(dev);
	int frags = skb_shinfo(skb)->nr_frags + 1;
	int i;

	if (!data->phy_ok && net_ratelimit())
		printk(KERN_ERR "%s: Transmit while PHY is down!\n", dev->name);

	if (!data->link_up) {
		printk(KERN_ERR "%s: Transmit while link is down!\n",
		       dev->name);
		netif_stop_queue(dev);
		return NETDEV_TX_BUSY;
	}

	if (data->txfree < MAX_SKB_FRAGS + 1) {
		netif_stop_queue(dev);

		if (net_ratelimit())
			printk(KERN_ERR "%s: Transmit with full tx ring!\n",
			       dev->name);
		return NETDEV_TX_BUSY;
	}

	if (data->txfree - frags < MAX_SKB_FRAGS + 1) {
		netif_stop_queue(dev);
	}

	spin_lock_irq(&data->txlock);

	for (i = 0; i < frags; i++) {
		int misc = 0;
		int tx = data->txhead;

		/* This is done to mark every TSI108_TX_INT_FREQ tx buffers with
		 * the interrupt bit.  TX descriptor-complete interrupts are
		 * enabled when the queue fills up, and masked when there is
		 * still free space.  This way, when saturating the outbound
		 * link, the tx interrupts are kept to a reasonable level.
		 * When the queue is not full, reclamation of skbs still occurs
		 * as new packets are transmitted, or on a queue-empty
		 * interrupt.
		 */

		if ((tx % TSI108_TX_INT_FREQ == 0) &&
		    ((TSI108_TXRING_LEN - data->txfree) >= TSI108_TX_INT_FREQ))
			misc = TSI108_TX_INT;

		data->txskbs[tx] = skb;

		if (i == 0) {
			data->txring[tx].buf0 = dma_map_single(NULL, skb->data,
					skb_headlen(skb), DMA_TO_DEVICE);
			data->txring[tx].len = skb_headlen(skb);
			misc |= TSI108_TX_SOF;
		} else {
			const skb_frag_t *frag = &skb_shinfo(skb)->frags[i - 1];

			data->txring[tx].buf0 = skb_frag_dma_map(NULL, frag,
								 0,
								 skb_frag_size(frag),
								 DMA_TO_DEVICE);
			data->txring[tx].len = skb_frag_size(frag);
		}

		if (i == frags - 1)
			misc |= TSI108_TX_EOF;

		if (netif_msg_pktdata(data)) {
			int i;
			printk("%s: Tx Frame contents (%d)\n", dev->name,
			       skb->len);
			for (i = 0; i < skb->len; i++)
				printk(" %2.2x", skb->data[i]);
			printk(".\n");
		}
		data->txring[tx].misc = misc | TSI108_TX_OWN;

		data->txhead = (data->txhead + 1) % TSI108_TXRING_LEN;
		data->txfree--;
	}

	tsi108_complete_tx(dev);

	/* This must be done after the check for completed tx descriptors,
	 * so that the tail pointer is correct.
	 */

	if (!(TSI_READ(TSI108_EC_TXSTAT) & TSI108_EC_TXSTAT_QUEUE0))
		tsi108_restart_tx(data);

	spin_unlock_irq(&data->txlock);
	return NETDEV_TX_OK;
}

static int tsi108_complete_rx(struct net_device *dev, int budget)
{
	struct tsi108_prv_data *data = netdev_priv(dev);
	int done = 0;

	while (data->rxfree && done != budget) {
		int rx = data->rxtail;
		struct sk_buff *skb;

		if (data->rxring[rx].misc & TSI108_RX_OWN)
			break;

		skb = data->rxskbs[rx];
		data->rxtail = (data->rxtail + 1) % TSI108_RXRING_LEN;
		data->rxfree--;
		done++;

		if (data->rxring[rx].misc & TSI108_RX_BAD) {
			spin_lock_irq(&data->misclock);

			if (data->rxring[rx].misc & TSI108_RX_CRC)
				data->stats.rx_crc_errors++;
			if (data->rxring[rx].misc & TSI108_RX_OVER)
				data->stats.rx_fifo_errors++;

			spin_unlock_irq(&data->misclock);

			dev_kfree_skb_any(skb);
			continue;
		}
		if (netif_msg_pktdata(data)) {
			int i;
			printk("%s: Rx Frame contents (%d)\n",
			       dev->name, data->rxring[rx].len);
			for (i = 0; i < data->rxring[rx].len; i++)
				printk(" %2.2x", skb->data[i]);
			printk(".\n");
		}

		skb_put(skb, data->rxring[rx].len);
		skb->protocol = eth_type_trans(skb, dev);
		netif_receive_skb(skb);
	}

	return done;
}

static int tsi108_refill_rx(struct net_device *dev, int budget)
{
	struct tsi108_prv_data *data = netdev_priv(dev);
	int done = 0;

	while (data->rxfree != TSI108_RXRING_LEN && done != budget) {
		int rx = data->rxhead;
		struct sk_buff *skb;

		skb = netdev_alloc_skb_ip_align(dev, TSI108_RXBUF_SIZE);
		data->rxskbs[rx] = skb;
		if (!skb)
			break;

		data->rxring[rx].buf0 = dma_map_single(NULL, skb->data,
							TSI108_RX_SKB_SIZE,
							DMA_FROM_DEVICE);

		/* Sometimes the hardware sets blen to zero after packet
		 * reception, even though the manual says that it's only ever
		 * modified by the driver.
		 */

		data->rxring[rx].blen = TSI108_RX_SKB_SIZE;
		data->rxring[rx].misc = TSI108_RX_OWN | TSI108_RX_INT;

		data->rxhead = (data->rxhead + 1) % TSI108_RXRING_LEN;
		data->rxfree++;
		done++;
	}

	if (done != 0 && !(TSI_READ(TSI108_EC_RXSTAT) &
			   TSI108_EC_RXSTAT_QUEUE0))
		tsi108_restart_rx(data, dev);

	return done;
}

static int tsi108_poll(struct napi_struct *napi, int budget)
{
	struct tsi108_prv_data *data = container_of(napi, struct tsi108_prv_data, napi);
	struct net_device *dev = data->dev;
	u32 estat = TSI_READ(TSI108_EC_RXESTAT);
	u32 intstat = TSI_READ(TSI108_EC_INTSTAT);
	int num_received = 0, num_filled = 0;

	intstat &= TSI108_INT_RXQUEUE0 | TSI108_INT_RXTHRESH |
	    TSI108_INT_RXOVERRUN | TSI108_INT_RXERROR | TSI108_INT_RXWAIT;

	TSI_WRITE(TSI108_EC_RXESTAT, estat);
	TSI_WRITE(TSI108_EC_INTSTAT, intstat);

	if (data->rxpending || (estat & TSI108_EC_RXESTAT_Q0_DESCINT))
		num_received = tsi108_complete_rx(dev, budget);

	/* This should normally fill no more slots than the number of
	 * packets received in tsi108_complete_rx().  The exception
	 * is when we previously ran out of memory for RX SKBs.  In that
	 * case, it's helpful to obey the budget, not only so that the
	 * CPU isn't hogged, but so that memory (which may still be low)
	 * is not hogged by one device.
	 *
	 * A work unit is considered to be two SKBs to allow us to catch
	 * up when the ring has shrunk due to out-of-memory but we're
	 * still removing the full budget's worth of packets each time.
	 */

	if (data->rxfree < TSI108_RXRING_LEN)
		num_filled = tsi108_refill_rx(dev, budget * 2);

	if (intstat & TSI108_INT_RXERROR) {
		u32 err = TSI_READ(TSI108_EC_RXERR);
		TSI_WRITE(TSI108_EC_RXERR, err);

		if (err) {
			if (net_ratelimit())
				printk(KERN_DEBUG "%s: RX error %x\n",
				       dev->name, err);

			if (!(TSI_READ(TSI108_EC_RXSTAT) &
			      TSI108_EC_RXSTAT_QUEUE0))
				tsi108_restart_rx(data, dev);
		}
	}

	if (intstat & TSI108_INT_RXOVERRUN) {
		spin_lock_irq(&data->misclock);
		data->stats.rx_fifo_errors++;
		spin_unlock_irq(&data->misclock);
	}

	if (num_received < budget) {
		data->rxpending = 0;
		napi_complete(napi);

		TSI_WRITE(TSI108_EC_INTMASK,
				     TSI_READ(TSI108_EC_INTMASK)
				     & ~(TSI108_INT_RXQUEUE0
					 | TSI108_INT_RXTHRESH |
					 TSI108_INT_RXOVERRUN |
					 TSI108_INT_RXERROR |
					 TSI108_INT_RXWAIT));
	} else {
		data->rxpending = 1;
	}

	return num_received;
}

static void tsi108_rx_int(struct net_device *dev)
{
	struct tsi108_prv_data *data = netdev_priv(dev);

	/* A race could cause dev to already be scheduled, so it's not an
	 * error if that happens (and interrupts shouldn't be re-masked,
	 * because that can cause harmful races, if poll has already
	 * unmasked them but not cleared LINK_STATE_SCHED).
	 *
	 * This can happen if this code races with tsi108_poll(), which masks
	 * the interrupts after tsi108_irq_one() read the mask, but before
	 * napi_schedule is called.  It could also happen due to calls
	 * from tsi108_check_rxring().
	 */

	if (napi_schedule_prep(&data->napi)) {
		/* Mask, rather than ack, the receive interrupts.  The ack
		 * will happen in tsi108_poll().
		 */

		TSI_WRITE(TSI108_EC_INTMASK,
				     TSI_READ(TSI108_EC_INTMASK) |
				     TSI108_INT_RXQUEUE0
				     | TSI108_INT_RXTHRESH |
				     TSI108_INT_RXOVERRUN | TSI108_INT_RXERROR |
				     TSI108_INT_RXWAIT);
		__napi_schedule(&data->napi);
	} else {
		if (!netif_running(dev)) {
			/* This can happen if an interrupt occurs while the
			 * interface is being brought down, as the START
			 * bit is cleared before the stop function is called.
			 *
			 * In this case, the interrupts must be masked, or
			 * they will continue indefinitely.
			 *
			 * There's a race here if the interface is brought down
			 * and then up in rapid succession, as the device could
			 * be made running after the above check and before
			 * the masking below.  This will only happen if the IRQ
			 * thread has a lower priority than the task brining
			 * up the interface.  Fixing this race would likely
			 * require changes in generic code.
			 */

			TSI_WRITE(TSI108_EC_INTMASK,
					     TSI_READ
					     (TSI108_EC_INTMASK) |
					     TSI108_INT_RXQUEUE0 |
					     TSI108_INT_RXTHRESH |
					     TSI108_INT_RXOVERRUN |
					     TSI108_INT_RXERROR |
					     TSI108_INT_RXWAIT);
		}
	}
}

/* If the RX ring has run out of memory, try periodically
 * to allocate some more, as otherwise poll would never
 * get called (apart from the initial end-of-queue condition).
 *
 * This is called once per second (by default) from the thread.
 */

static void tsi108_check_rxring(struct net_device *dev)
{
	struct tsi108_prv_data *data = netdev_priv(dev);

	/* A poll is scheduled, as opposed to caling tsi108_refill_rx
	 * directly, so as to keep the receive path single-threaded
	 * (and thus not needing a lock).
	 */

	if (netif_running(dev) && data->rxfree < TSI108_RXRING_LEN / 4)
		tsi108_rx_int(dev);
}

static void tsi108_tx_int(struct net_device *dev)
{
	struct tsi108_prv_data *data = netdev_priv(dev);
	u32 estat = TSI_READ(TSI108_EC_TXESTAT);

	TSI_WRITE(TSI108_EC_TXESTAT, estat);
	TSI_WRITE(TSI108_EC_INTSTAT, TSI108_INT_TXQUEUE0 |
			     TSI108_INT_TXIDLE | TSI108_INT_TXERROR);
	if (estat & TSI108_EC_TXESTAT_Q0_ERR) {
		u32 err = TSI_READ(TSI108_EC_TXERR);
		TSI_WRITE(TSI108_EC_TXERR, err);

		if (err && net_ratelimit())
			printk(KERN_ERR "%s: TX error %x\n", dev->name, err);
	}

	if (estat & (TSI108_EC_TXESTAT_Q0_DESCINT | TSI108_EC_TXESTAT_Q0_EOQ)) {
		spin_lock(&data->txlock);
		tsi108_complete_tx(dev);
		spin_unlock(&data->txlock);
	}
}


static irqreturn_t tsi108_irq(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct tsi108_prv_data *data = netdev_priv(dev);
	u32 stat = TSI_READ(TSI108_EC_INTSTAT);

	if (!(stat & TSI108_INT_ANY))
		return IRQ_NONE;	/* Not our interrupt */

	stat &= ~TSI_READ(TSI108_EC_INTMASK);

	if (stat & (TSI108_INT_TXQUEUE0 | TSI108_INT_TXIDLE |
		    TSI108_INT_TXERROR))
		tsi108_tx_int(dev);
	if (stat & (TSI108_INT_RXQUEUE0 | TSI108_INT_RXTHRESH |
		    TSI108_INT_RXWAIT | TSI108_INT_RXOVERRUN |
		    TSI108_INT_RXERROR))
		tsi108_rx_int(dev);

	if (stat & TSI108_INT_SFN) {
		if (net_ratelimit())
			printk(KERN_DEBUG "%s: SFN error\n", dev->name);
		TSI_WRITE(TSI108_EC_INTSTAT, TSI108_INT_SFN);
	}

	if (stat & TSI108_INT_STATCARRY) {
		tsi108_stat_carry(dev);
		TSI_WRITE(TSI108_EC_INTSTAT, TSI108_INT_STATCARRY);
	}

	return IRQ_HANDLED;
}

static void tsi108_stop_ethernet(struct net_device *dev)
{
	struct tsi108_prv_data *data = netdev_priv(dev);
	int i = 1000;
	/* Disable all TX and RX queues ... */
	TSI_WRITE(TSI108_EC_TXCTRL, 0);
	TSI_WRITE(TSI108_EC_RXCTRL, 0);

	/* ...and wait for them to become idle */
	while(i--) {
		if(!(TSI_READ(TSI108_EC_TXSTAT) & TSI108_EC_TXSTAT_ACTIVE))
			break;
		udelay(10);
	}
	i = 1000;
	while(i--){
		if(!(TSI_READ(TSI108_EC_RXSTAT) & TSI108_EC_RXSTAT_ACTIVE))
			return;
		udelay(10);
	}
	printk(KERN_ERR "%s function time out\n", __func__);
}

static void tsi108_reset_ether(struct tsi108_prv_data * data)
{
	TSI_WRITE(TSI108_MAC_CFG1, TSI108_MAC_CFG1_SOFTRST);
	udelay(100);
	TSI_WRITE(TSI108_MAC_CFG1, 0);

	TSI_WRITE(TSI108_EC_PORTCTRL, TSI108_EC_PORTCTRL_STATRST);
	udelay(100);
	TSI_WRITE(TSI108_EC_PORTCTRL,
			     TSI_READ(TSI108_EC_PORTCTRL) &
			     ~TSI108_EC_PORTCTRL_STATRST);

	TSI_WRITE(TSI108_EC_TXCFG, TSI108_EC_TXCFG_RST);
	udelay(100);
	TSI_WRITE(TSI108_EC_TXCFG,
			     TSI_READ(TSI108_EC_TXCFG) &
			     ~TSI108_EC_TXCFG_RST);

	TSI_WRITE(TSI108_EC_RXCFG, TSI108_EC_RXCFG_RST);
	udelay(100);
	TSI_WRITE(TSI108_EC_RXCFG,
			     TSI_READ(TSI108_EC_RXCFG) &
			     ~TSI108_EC_RXCFG_RST);

	TSI_WRITE(TSI108_MAC_MII_MGMT_CFG,
			     TSI_READ(TSI108_MAC_MII_MGMT_CFG) |
			     TSI108_MAC_MII_MGMT_RST);
	udelay(100);
	TSI_WRITE(TSI108_MAC_MII_MGMT_CFG,
			     (TSI_READ(TSI108_MAC_MII_MGMT_CFG) &
			     ~(TSI108_MAC_MII_MGMT_RST |
			       TSI108_MAC_MII_MGMT_CLK)) | 0x07);
}

static int tsi108_get_mac(struct net_device *dev)
{
	struct tsi108_prv_data *data = netdev_priv(dev);
	u32 word1 = TSI_READ(TSI108_MAC_ADDR1);
	u32 word2 = TSI_READ(TSI108_MAC_ADDR2);

	/* Note that the octets are reversed from what the manual says,
	 * producing an even weirder ordering...
	 */
	if (word2 == 0 && word1 == 0) {
		dev->dev_addr[0] = 0x00;
		dev->dev_addr[1] = 0x06;
		dev->dev_addr[2] = 0xd2;
		dev->dev_addr[3] = 0x00;
		dev->dev_addr[4] = 0x00;
		if (0x8 == data->phy)
			dev->dev_addr[5] = 0x01;
		else
			dev->dev_addr[5] = 0x02;

		word2 = (dev->dev_addr[0] << 16) | (dev->dev_addr[1] << 24);

		word1 = (dev->dev_addr[2] << 0) | (dev->dev_addr[3] << 8) |
		    (dev->dev_addr[4] << 16) | (dev->dev_addr[5] << 24);

		TSI_WRITE(TSI108_MAC_ADDR1, word1);
		TSI_WRITE(TSI108_MAC_ADDR2, word2);
	} else {
		dev->dev_addr[0] = (word2 >> 16) & 0xff;
		dev->dev_addr[1] = (word2 >> 24) & 0xff;
		dev->dev_addr[2] = (word1 >> 0) & 0xff;
		dev->dev_addr[3] = (word1 >> 8) & 0xff;
		dev->dev_addr[4] = (word1 >> 16) & 0xff;
		dev->dev_addr[5] = (word1 >> 24) & 0xff;
	}

	if (!is_valid_ether_addr(dev->dev_addr)) {
		printk(KERN_ERR
		       "%s: Invalid MAC address. word1: %08x, word2: %08x\n",
		       dev->name, word1, word2);
		return -EINVAL;
	}

	return 0;
}

static int tsi108_set_mac(struct net_device *dev, void *addr)
{
	struct tsi108_prv_data *data = netdev_priv(dev);
	u32 word1, word2;
	int i;

	if (!is_valid_ether_addr(addr))
		return -EADDRNOTAVAIL;

	for (i = 0; i < 6; i++)
		/* +2 is for the offset of the HW addr type */
		dev->dev_addr[i] = ((unsigned char *)addr)[i + 2];

	word2 = (dev->dev_addr[0] << 16) | (dev->dev_addr[1] << 24);

	word1 = (dev->dev_addr[2] << 0) | (dev->dev_addr[3] << 8) |
	    (dev->dev_addr[4] << 16) | (dev->dev_addr[5] << 24);

	spin_lock_irq(&data->misclock);
	TSI_WRITE(TSI108_MAC_ADDR1, word1);
	TSI_WRITE(TSI108_MAC_ADDR2, word2);
	spin_lock(&data->txlock);

	if (data->txfree && data->link_up)
		netif_wake_queue(dev);

	spin_unlock(&data->txlock);
	spin_unlock_irq(&data->misclock);
	return 0;
}

/* Protected by dev->xmit_lock. */
static void tsi108_set_rx_mode(struct net_device *dev)
{
	struct tsi108_prv_data *data = netdev_priv(dev);
	u32 rxcfg = TSI_READ(TSI108_EC_RXCFG);

	if (dev->flags & IFF_PROMISC) {
		rxcfg &= ~(TSI108_EC_RXCFG_UC_HASH | TSI108_EC_RXCFG_MC_HASH);
		rxcfg |= TSI108_EC_RXCFG_UFE | TSI108_EC_RXCFG_MFE;
		goto out;
	}

	rxcfg &= ~(TSI108_EC_RXCFG_UFE | TSI108_EC_RXCFG_MFE);

	if (dev->flags & IFF_ALLMULTI || !netdev_mc_empty(dev)) {
		int i;
		struct netdev_hw_addr *ha;
		rxcfg |= TSI108_EC_RXCFG_MFE | TSI108_EC_RXCFG_MC_HASH;

		memset(data->mc_hash, 0, sizeof(data->mc_hash));

		netdev_for_each_mc_addr(ha, dev) {
			u32 hash, crc;

			crc = ether_crc(6, ha->addr);
			hash = crc >> 23;
			__set_bit(hash, &data->mc_hash[0]);
		}

		TSI_WRITE(TSI108_EC_HASHADDR,
				     TSI108_EC_HASHADDR_AUTOINC |
				     TSI108_EC_HASHADDR_MCAST);

		for (i = 0; i < 16; i++) {
			/* The manual says that the hardware may drop
			 * back-to-back writes to the data register.
			 */
			udelay(1);
			TSI_WRITE(TSI108_EC_HASHDATA,
					     data->mc_hash[i]);
		}
	}

      out:
	TSI_WRITE(TSI108_EC_RXCFG, rxcfg);
}

static void tsi108_init_phy(struct net_device *dev)
{
	struct tsi108_prv_data *data = netdev_priv(dev);
	u32 i = 0;
	u16 phyval = 0;
	unsigned long flags;

	spin_lock_irqsave(&phy_lock, flags);

	tsi108_write_mii(data, MII_BMCR, BMCR_RESET);
	while (--i) {
		if(!(tsi108_read_mii(data, MII_BMCR) & BMCR_RESET))
			break;
		udelay(10);
	}
	if (i == 0)
		printk(KERN_ERR "%s function time out\n", __func__);

	if (data->phy_type == TSI108_PHY_BCM54XX) {
		tsi108_write_mii(data, 0x09, 0x0300);
		tsi108_write_mii(data, 0x10, 0x1020);
		tsi108_write_mii(data, 0x1c, 0x8c00);
	}

	tsi108_write_mii(data,
			 MII_BMCR,
			 BMCR_ANENABLE | BMCR_ANRESTART);
	while (tsi108_read_mii(data, MII_BMCR) & BMCR_ANRESTART)
		cpu_relax();

	/* Set G/MII mode and receive clock select in TBI control #2.  The
	 * second port won't work if this isn't done, even though we don't
	 * use TBI mode.
	 */

	tsi108_write_tbi(data, 0x11, 0x30);

	/* FIXME: It seems to take more than 2 back-to-back reads to the
	 * PHY_STAT register before the link up status bit is set.
	 */

	data->link_up = 0;

	while (!((phyval = tsi108_read_mii(data, MII_BMSR)) &
		 BMSR_LSTATUS)) {
		if (i++ > (MII_READ_DELAY / 10)) {
			break;
		}
		spin_unlock_irqrestore(&phy_lock, flags);
		msleep(10);
		spin_lock_irqsave(&phy_lock, flags);
	}

	data->mii_if.supports_gmii = mii_check_gmii_support(&data->mii_if);
	printk(KERN_DEBUG "PHY_STAT reg contains %08x\n", phyval);
	data->phy_ok = 1;
	data->init_media = 1;
	spin_unlock_irqrestore(&phy_lock, flags);
}

static void tsi108_kill_phy(struct net_device *dev)
{
	struct tsi108_prv_data *data = netdev_priv(dev);
	unsigned long flags;

	spin_lock_irqsave(&phy_lock, flags);
	tsi108_write_mii(data, MII_BMCR, BMCR_PDOWN);
	data->phy_ok = 0;
	spin_unlock_irqrestore(&phy_lock, flags);
}

static int tsi108_open(struct net_device *dev)
{
	int i;
	struct tsi108_prv_data *data = netdev_priv(dev);
	unsigned int rxring_size = TSI108_RXRING_LEN * sizeof(rx_desc);
	unsigned int txring_size = TSI108_TXRING_LEN * sizeof(tx_desc);

	i = request_irq(data->irq_num, tsi108_irq, 0, dev->name, dev);
	if (i != 0) {
		printk(KERN_ERR "tsi108_eth%d: Could not allocate IRQ%d.\n",
		       data->id, data->irq_num);
		return i;
	} else {
		dev->irq = data->irq_num;
		printk(KERN_NOTICE
		       "tsi108_open : Port %d Assigned IRQ %d to %s\n",
		       data->id, dev->irq, dev->name);
	}

	data->rxring = dma_alloc_coherent(NULL, rxring_size,
			&data->rxdma, GFP_KERNEL);

	if (!data->rxring) {
		printk(KERN_DEBUG
		       "TSI108_ETH: failed to allocate memory for rxring!\n");
		return -ENOMEM;
	} else {
		memset(data->rxring, 0, rxring_size);
	}

	data->txring = dma_alloc_coherent(NULL, txring_size,
			&data->txdma, GFP_KERNEL);

	if (!data->txring) {
		printk(KERN_DEBUG
		       "TSI108_ETH: failed to allocate memory for txring!\n");
		pci_free_consistent(0, rxring_size, data->rxring, data->rxdma);
		return -ENOMEM;
	} else {
		memset(data->txring, 0, txring_size);
	}

	for (i = 0; i < TSI108_RXRING_LEN; i++) {
		data->rxring[i].next0 = data->rxdma + (i + 1) * sizeof(rx_desc);
		data->rxring[i].blen = TSI108_RXBUF_SIZE;
		data->rxring[i].vlan = 0;
	}

	data->rxring[TSI108_RXRING_LEN - 1].next0 = data->rxdma;

	data->rxtail = 0;
	data->rxhead = 0;

	for (i = 0; i < TSI108_RXRING_LEN; i++) {
		struct sk_buff *skb;

		skb = netdev_alloc_skb_ip_align(dev, TSI108_RXBUF_SIZE);
		if (!skb) {
			/* Bah.  No memory for now, but maybe we'll get
			 * some more later.
			 * For now, we'll live with the smaller ring.
			 */
			printk(KERN_WARNING
			       "%s: Could only allocate %d receive skb(s).\n",
			       dev->name, i);
			data->rxhead = i;
			break;
		}

		data->rxskbs[i] = skb;
		data->rxskbs[i] = skb;
		data->rxring[i].buf0 = virt_to_phys(data->rxskbs[i]->data);
		data->rxring[i].misc = TSI108_RX_OWN | TSI108_RX_INT;
	}

	data->rxfree = i;
	TSI_WRITE(TSI108_EC_RXQ_PTRLOW, data->rxdma);

	for (i = 0; i < TSI108_TXRING_LEN; i++) {
		data->txring[i].next0 = data->txdma + (i + 1) * sizeof(tx_desc);
		data->txring[i].misc = 0;
	}

	data->txring[TSI108_TXRING_LEN - 1].next0 = data->txdma;
	data->txtail = 0;
	data->txhead = 0;
	data->txfree = TSI108_TXRING_LEN;
	TSI_WRITE(TSI108_EC_TXQ_PTRLOW, data->txdma);
	tsi108_init_phy(dev);

	napi_enable(&data->napi);

	setup_timer(&data->timer, tsi108_timed_checker, (unsigned long)dev);
	mod_timer(&data->timer, jiffies + 1);

	tsi108_restart_rx(data, dev);

	TSI_WRITE(TSI108_EC_INTSTAT, ~0);

	TSI_WRITE(TSI108_EC_INTMASK,
			     ~(TSI108_INT_TXQUEUE0 | TSI108_INT_RXERROR |
			       TSI108_INT_RXTHRESH | TSI108_INT_RXQUEUE0 |
			       TSI108_INT_RXOVERRUN | TSI108_INT_RXWAIT |
			       TSI108_INT_SFN | TSI108_INT_STATCARRY));

	TSI_WRITE(TSI108_MAC_CFG1,
			     TSI108_MAC_CFG1_RXEN | TSI108_MAC_CFG1_TXEN);
	netif_start_queue(dev);
	return 0;
}

static int tsi108_close(struct net_device *dev)
{
	struct tsi108_prv_data *data = netdev_priv(dev);

	netif_stop_queue(dev);
	napi_disable(&data->napi);

	del_timer_sync(&data->timer);

	tsi108_stop_ethernet(dev);
	tsi108_kill_phy(dev);
	TSI_WRITE(TSI108_EC_INTMASK, ~0);
	TSI_WRITE(TSI108_MAC_CFG1, 0);

	/* Check for any pending TX packets, and drop them. */

	while (!data->txfree || data->txhead != data->txtail) {
		int tx = data->txtail;
		struct sk_buff *skb;
		skb = data->txskbs[tx];
		data->txtail = (data->txtail + 1) % TSI108_TXRING_LEN;
		data->txfree++;
		dev_kfree_skb(skb);
	}

	free_irq(data->irq_num, dev);

	/* Discard the RX ring. */

	while (data->rxfree) {
		int rx = data->rxtail;
		struct sk_buff *skb;

		skb = data->rxskbs[rx];
		data->rxtail = (data->rxtail + 1) % TSI108_RXRING_LEN;
		data->rxfree--;
		dev_kfree_skb(skb);
	}

	dma_free_coherent(0,
			    TSI108_RXRING_LEN * sizeof(rx_desc),
			    data->rxring, data->rxdma);
	dma_free_coherent(0,
			    TSI108_TXRING_LEN * sizeof(tx_desc),
			    data->txring, data->txdma);

	return 0;
}

static void tsi108_init_mac(struct net_device *dev)
{
	struct tsi108_prv_data *data = netdev_priv(dev);

	TSI_WRITE(TSI108_MAC_CFG2, TSI108_MAC_CFG2_DFLT_PREAMBLE |
			     TSI108_MAC_CFG2_PADCRC);

	TSI_WRITE(TSI108_EC_TXTHRESH,
			     (192 << TSI108_EC_TXTHRESH_STARTFILL) |
			     (192 << TSI108_EC_TXTHRESH_STOPFILL));

	TSI_WRITE(TSI108_STAT_CARRYMASK1,
			     ~(TSI108_STAT_CARRY1_RXBYTES |
			       TSI108_STAT_CARRY1_RXPKTS |
			       TSI108_STAT_CARRY1_RXFCS |
			       TSI108_STAT_CARRY1_RXMCAST |
			       TSI108_STAT_CARRY1_RXALIGN |
			       TSI108_STAT_CARRY1_RXLENGTH |
			       TSI108_STAT_CARRY1_RXRUNT |
			       TSI108_STAT_CARRY1_RXJUMBO |
			       TSI108_STAT_CARRY1_RXFRAG |
			       TSI108_STAT_CARRY1_RXJABBER |
			       TSI108_STAT_CARRY1_RXDROP));

	TSI_WRITE(TSI108_STAT_CARRYMASK2,
			     ~(TSI108_STAT_CARRY2_TXBYTES |
			       TSI108_STAT_CARRY2_TXPKTS |
			       TSI108_STAT_CARRY2_TXEXDEF |
			       TSI108_STAT_CARRY2_TXEXCOL |
			       TSI108_STAT_CARRY2_TXTCOL |
			       TSI108_STAT_CARRY2_TXPAUSE));

	TSI_WRITE(TSI108_EC_PORTCTRL, TSI108_EC_PORTCTRL_STATEN);
	TSI_WRITE(TSI108_MAC_CFG1, 0);

	TSI_WRITE(TSI108_EC_RXCFG,
			     TSI108_EC_RXCFG_SE | TSI108_EC_RXCFG_BFE);

	TSI_WRITE(TSI108_EC_TXQ_CFG, TSI108_EC_TXQ_CFG_DESC_INT |
			     TSI108_EC_TXQ_CFG_EOQ_OWN_INT |
			     TSI108_EC_TXQ_CFG_WSWP | (TSI108_PBM_PORT <<
						TSI108_EC_TXQ_CFG_SFNPORT));

	TSI_WRITE(TSI108_EC_RXQ_CFG, TSI108_EC_RXQ_CFG_DESC_INT |
			     TSI108_EC_RXQ_CFG_EOQ_OWN_INT |
			     TSI108_EC_RXQ_CFG_WSWP | (TSI108_PBM_PORT <<
						TSI108_EC_RXQ_CFG_SFNPORT));

	TSI_WRITE(TSI108_EC_TXQ_BUFCFG,
			     TSI108_EC_TXQ_BUFCFG_BURST256 |
			     TSI108_EC_TXQ_BUFCFG_BSWP | (TSI108_PBM_PORT <<
						TSI108_EC_TXQ_BUFCFG_SFNPORT));

	TSI_WRITE(TSI108_EC_RXQ_BUFCFG,
			     TSI108_EC_RXQ_BUFCFG_BURST256 |
			     TSI108_EC_RXQ_BUFCFG_BSWP | (TSI108_PBM_PORT <<
						TSI108_EC_RXQ_BUFCFG_SFNPORT));

	TSI_WRITE(TSI108_EC_INTMASK, ~0);
}

static int tsi108_get_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct tsi108_prv_data *data = netdev_priv(dev);
	unsigned long flags;
	int rc;

	spin_lock_irqsave(&data->txlock, flags);
	rc = mii_ethtool_gset(&data->mii_if, cmd);
	spin_unlock_irqrestore(&data->txlock, flags);

	return rc;
}

static int tsi108_set_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct tsi108_prv_data *data = netdev_priv(dev);
	unsigned long flags;
	int rc;

	spin_lock_irqsave(&data->txlock, flags);
	rc = mii_ethtool_sset(&data->mii_if, cmd);
	spin_unlock_irqrestore(&data->txlock, flags);

	return rc;
}

static int tsi108_do_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct tsi108_prv_data *data = netdev_priv(dev);
	if (!netif_running(dev))
		return -EINVAL;
	return generic_mii_ioctl(&data->mii_if, if_mii(rq), cmd, NULL);
}

static const struct ethtool_ops tsi108_ethtool_ops = {
	.get_link 	= ethtool_op_get_link,
	.get_settings	= tsi108_get_settings,
	.set_settings	= tsi108_set_settings,
};

static const struct net_device_ops tsi108_netdev_ops = {
	.ndo_open		= tsi108_open,
	.ndo_stop		= tsi108_close,
	.ndo_start_xmit		= tsi108_send_packet,
	.ndo_set_rx_mode	= tsi108_set_rx_mode,
	.ndo_get_stats		= tsi108_get_stats,
	.ndo_do_ioctl		= tsi108_do_ioctl,
	.ndo_set_mac_address	= tsi108_set_mac,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_change_mtu		= eth_change_mtu,
};

static int
tsi108_init_one(struct platform_device *pdev)
{
	struct net_device *dev = NULL;
	struct tsi108_prv_data *data = NULL;
	hw_info *einfo;
	int err = 0;

	einfo = pdev->dev.platform_data;

	if (NULL == einfo) {
		printk(KERN_ERR "tsi-eth %d: Missing additional data!\n",
		       pdev->id);
		return -ENODEV;
	}

	/* Create an ethernet device instance */

	dev = alloc_etherdev(sizeof(struct tsi108_prv_data));
	if (!dev)
		return -ENOMEM;

	printk("tsi108_eth%d: probe...\n", pdev->id);
	data = netdev_priv(dev);
	data->dev = dev;

	pr_debug("tsi108_eth%d:regs:phyresgs:phy:irq_num=0x%x:0x%x:0x%x:0x%x\n",
			pdev->id, einfo->regs, einfo->phyregs,
			einfo->phy, einfo->irq_num);

	data->regs = ioremap(einfo->regs, 0x400);
	if (NULL == data->regs) {
		err = -ENOMEM;
		goto regs_fail;
	}

	data->phyregs = ioremap(einfo->phyregs, 0x400);
	if (NULL == data->phyregs) {
		err = -ENOMEM;
		goto phyregs_fail;
	}
/* MII setup */
	data->mii_if.dev = dev;
	data->mii_if.mdio_read = tsi108_mdio_read;
	data->mii_if.mdio_write = tsi108_mdio_write;
	data->mii_if.phy_id = einfo->phy;
	data->mii_if.phy_id_mask = 0x1f;
	data->mii_if.reg_num_mask = 0x1f;

	data->phy = einfo->phy;
	data->phy_type = einfo->phy_type;
	data->irq_num = einfo->irq_num;
	data->id = pdev->id;
	netif_napi_add(dev, &data->napi, tsi108_poll, 64);
	dev->netdev_ops = &tsi108_netdev_ops;
	dev->ethtool_ops = &tsi108_ethtool_ops;

	/* Apparently, the Linux networking code won't use scatter-gather
	 * if the hardware doesn't do checksums.  However, it's faster
	 * to checksum in place and use SG, as (among other reasons)
	 * the cache won't be dirtied (which then has to be flushed
	 * before DMA).  The checksumming is done by the driver (via
	 * a new function skb_csum_dev() in net/core/skbuff.c).
	 */

	dev->features = NETIF_F_HIGHDMA;

	spin_lock_init(&data->txlock);
	spin_lock_init(&data->misclock);

	tsi108_reset_ether(data);
	tsi108_kill_phy(dev);

	if ((err = tsi108_get_mac(dev)) != 0) {
		printk(KERN_ERR "%s: Invalid MAC address.  Please correct.\n",
		       dev->name);
		goto register_fail;
	}

	tsi108_init_mac(dev);
	err = register_netdev(dev);
	if (err) {
		printk(KERN_ERR "%s: Cannot register net device, aborting.\n",
				dev->name);
		goto register_fail;
	}

	platform_set_drvdata(pdev, dev);
	printk(KERN_INFO "%s: Tsi108 Gigabit Ethernet, MAC: %pM\n",
	       dev->name, dev->dev_addr);
#ifdef DEBUG
	data->msg_enable = DEBUG;
	dump_eth_one(dev);
#endif

	return 0;

register_fail:
	iounmap(data->phyregs);

phyregs_fail:
	iounmap(data->regs);

regs_fail:
	free_netdev(dev);
	return err;
}

/* There's no way to either get interrupts from the PHY when
 * something changes, or to have the Tsi108 automatically communicate
 * with the PHY to reconfigure itself.
 *
 * Thus, we have to do it using a timer.
 */

static void tsi108_timed_checker(unsigned long dev_ptr)
{
	struct net_device *dev = (struct net_device *)dev_ptr;
	struct tsi108_prv_data *data = netdev_priv(dev);

	tsi108_check_phy(dev);
	tsi108_check_rxring(dev);
	mod_timer(&data->timer, jiffies + CHECK_PHY_INTERVAL);
}

static int tsi108_ether_remove(struct platform_device *pdev)
{
	struct net_device *dev = platform_get_drvdata(pdev);
	struct tsi108_prv_data *priv = netdev_priv(dev);

	unregister_netdev(dev);
	tsi108_stop_ethernet(dev);
	platform_set_drvdata(pdev, NULL);
	iounmap(priv->regs);
	iounmap(priv->phyregs);
	free_netdev(dev);

	return 0;
}
module_platform_driver(tsi_eth_driver);

MODULE_AUTHOR("Tundra Semiconductor Corporation");
MODULE_DESCRIPTION("Tsi108 Gigabit Ethernet driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:tsi-ethernet");

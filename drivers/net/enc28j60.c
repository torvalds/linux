/*
 * Microchip ENC28J60 ethernet driver (MAC + PHY)
 *
 * Copyright (C) 2007 Eurek srl
 * Author: Claudio Lanconelli <lanconelli.claudio@eptar.com>
 * based on enc28j60.c written by David Anders for 2.4 kernel version
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * $Id: enc28j60.c,v 1.22 2007/12/20 10:47:01 claudio Exp $
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/tcp.h>
#include <linux/skbuff.h>
#include <linux/delay.h>
#include <linux/spi/spi.h>

#include "enc28j60_hw.h"

#define DRV_NAME	"enc28j60"
#define DRV_VERSION	"1.01"

#define SPI_OPLEN	1

#define ENC28J60_MSG_DEFAULT	\
	(NETIF_MSG_PROBE | NETIF_MSG_IFUP | NETIF_MSG_IFDOWN | NETIF_MSG_LINK)

/* Buffer size required for the largest SPI transfer (i.e., reading a
 * frame). */
#define SPI_TRANSFER_BUF_LEN	(4 + MAX_FRAMELEN)

#define TX_TIMEOUT	(4 * HZ)

/* Max TX retries in case of collision as suggested by errata datasheet */
#define MAX_TX_RETRYCOUNT	16

enum {
	RXFILTER_NORMAL,
	RXFILTER_MULTI,
	RXFILTER_PROMISC
};

/* Driver local data */
struct enc28j60_net {
	struct net_device *netdev;
	struct spi_device *spi;
	struct mutex lock;
	struct sk_buff *tx_skb;
	struct work_struct tx_work;
	struct work_struct irq_work;
	struct work_struct setrx_work;
	struct work_struct restart_work;
	u8 bank;		/* current register bank selected */
	u16 next_pk_ptr;	/* next packet pointer within FIFO */
	u16 max_pk_counter;	/* statistics: max packet counter */
	u16 tx_retry_count;
	bool hw_enable;
	bool full_duplex;
	int rxfilter;
	u32 msg_enable;
	u8 spi_transfer_buf[SPI_TRANSFER_BUF_LEN];
};

/* use ethtool to change the level for any given device */
static struct {
	u32 msg_enable;
} debug = { -1 };

/*
 * SPI read buffer
 * wait for the SPI transfer and copy received data to destination
 */
static int
spi_read_buf(struct enc28j60_net *priv, int len, u8 *data)
{
	u8 *rx_buf = priv->spi_transfer_buf + 4;
	u8 *tx_buf = priv->spi_transfer_buf;
	struct spi_transfer t = {
		.tx_buf = tx_buf,
		.rx_buf = rx_buf,
		.len = SPI_OPLEN + len,
	};
	struct spi_message msg;
	int ret;

	tx_buf[0] = ENC28J60_READ_BUF_MEM;
	tx_buf[1] = tx_buf[2] = tx_buf[3] = 0;	/* don't care */

	spi_message_init(&msg);
	spi_message_add_tail(&t, &msg);
	ret = spi_sync(priv->spi, &msg);
	if (ret == 0) {
		memcpy(data, &rx_buf[SPI_OPLEN], len);
		ret = msg.status;
	}
	if (ret && netif_msg_drv(priv))
		printk(KERN_DEBUG DRV_NAME ": %s() failed: ret = %d\n",
			__func__, ret);

	return ret;
}

/*
 * SPI write buffer
 */
static int spi_write_buf(struct enc28j60_net *priv, int len,
			 const u8 *data)
{
	int ret;

	if (len > SPI_TRANSFER_BUF_LEN - 1 || len <= 0)
		ret = -EINVAL;
	else {
		priv->spi_transfer_buf[0] = ENC28J60_WRITE_BUF_MEM;
		memcpy(&priv->spi_transfer_buf[1], data, len);
		ret = spi_write(priv->spi, priv->spi_transfer_buf, len + 1);
		if (ret && netif_msg_drv(priv))
			printk(KERN_DEBUG DRV_NAME ": %s() failed: ret = %d\n",
				__func__, ret);
	}
	return ret;
}

/*
 * basic SPI read operation
 */
static u8 spi_read_op(struct enc28j60_net *priv, u8 op,
			   u8 addr)
{
	u8 tx_buf[2];
	u8 rx_buf[4];
	u8 val = 0;
	int ret;
	int slen = SPI_OPLEN;

	/* do dummy read if needed */
	if (addr & SPRD_MASK)
		slen++;

	tx_buf[0] = op | (addr & ADDR_MASK);
	ret = spi_write_then_read(priv->spi, tx_buf, 1, rx_buf, slen);
	if (ret)
		printk(KERN_DEBUG DRV_NAME ": %s() failed: ret = %d\n",
			__func__, ret);
	else
		val = rx_buf[slen - 1];

	return val;
}

/*
 * basic SPI write operation
 */
static int spi_write_op(struct enc28j60_net *priv, u8 op,
			u8 addr, u8 val)
{
	int ret;

	priv->spi_transfer_buf[0] = op | (addr & ADDR_MASK);
	priv->spi_transfer_buf[1] = val;
	ret = spi_write(priv->spi, priv->spi_transfer_buf, 2);
	if (ret && netif_msg_drv(priv))
		printk(KERN_DEBUG DRV_NAME ": %s() failed: ret = %d\n",
			__func__, ret);
	return ret;
}

static void enc28j60_soft_reset(struct enc28j60_net *priv)
{
	if (netif_msg_hw(priv))
		printk(KERN_DEBUG DRV_NAME ": %s() enter\n", __func__);

	spi_write_op(priv, ENC28J60_SOFT_RESET, 0, ENC28J60_SOFT_RESET);
	/* Errata workaround #1, CLKRDY check is unreliable,
	 * delay at least 1 mS instead */
	udelay(2000);
}

/*
 * select the current register bank if necessary
 */
static void enc28j60_set_bank(struct enc28j60_net *priv, u8 addr)
{
	u8 b = (addr & BANK_MASK) >> 5;

	/* These registers (EIE, EIR, ESTAT, ECON2, ECON1)
	 * are present in all banks, no need to switch bank
	 */
	if (addr >= EIE && addr <= ECON1)
		return;

	/* Clear or set each bank selection bit as needed */
	if ((b & ECON1_BSEL0) != (priv->bank & ECON1_BSEL0)) {
		if (b & ECON1_BSEL0)
			spi_write_op(priv, ENC28J60_BIT_FIELD_SET, ECON1,
					ECON1_BSEL0);
		else
			spi_write_op(priv, ENC28J60_BIT_FIELD_CLR, ECON1,
					ECON1_BSEL0);
	}
	if ((b & ECON1_BSEL1) != (priv->bank & ECON1_BSEL1)) {
		if (b & ECON1_BSEL1)
			spi_write_op(priv, ENC28J60_BIT_FIELD_SET, ECON1,
					ECON1_BSEL1);
		else
			spi_write_op(priv, ENC28J60_BIT_FIELD_CLR, ECON1,
					ECON1_BSEL1);
	}
	priv->bank = b;
}

/*
 * Register access routines through the SPI bus.
 * Every register access comes in two flavours:
 * - nolock_xxx: caller needs to invoke mutex_lock, usually to access
 *   atomically more than one register
 * - locked_xxx: caller doesn't need to invoke mutex_lock, single access
 *
 * Some registers can be accessed through the bit field clear and
 * bit field set to avoid a read modify write cycle.
 */

/*
 * Register bit field Set
 */
static void nolock_reg_bfset(struct enc28j60_net *priv,
				      u8 addr, u8 mask)
{
	enc28j60_set_bank(priv, addr);
	spi_write_op(priv, ENC28J60_BIT_FIELD_SET, addr, mask);
}

static void locked_reg_bfset(struct enc28j60_net *priv,
				      u8 addr, u8 mask)
{
	mutex_lock(&priv->lock);
	nolock_reg_bfset(priv, addr, mask);
	mutex_unlock(&priv->lock);
}

/*
 * Register bit field Clear
 */
static void nolock_reg_bfclr(struct enc28j60_net *priv,
				      u8 addr, u8 mask)
{
	enc28j60_set_bank(priv, addr);
	spi_write_op(priv, ENC28J60_BIT_FIELD_CLR, addr, mask);
}

static void locked_reg_bfclr(struct enc28j60_net *priv,
				      u8 addr, u8 mask)
{
	mutex_lock(&priv->lock);
	nolock_reg_bfclr(priv, addr, mask);
	mutex_unlock(&priv->lock);
}

/*
 * Register byte read
 */
static int nolock_regb_read(struct enc28j60_net *priv,
				     u8 address)
{
	enc28j60_set_bank(priv, address);
	return spi_read_op(priv, ENC28J60_READ_CTRL_REG, address);
}

static int locked_regb_read(struct enc28j60_net *priv,
				     u8 address)
{
	int ret;

	mutex_lock(&priv->lock);
	ret = nolock_regb_read(priv, address);
	mutex_unlock(&priv->lock);

	return ret;
}

/*
 * Register word read
 */
static int nolock_regw_read(struct enc28j60_net *priv,
				     u8 address)
{
	int rl, rh;

	enc28j60_set_bank(priv, address);
	rl = spi_read_op(priv, ENC28J60_READ_CTRL_REG, address);
	rh = spi_read_op(priv, ENC28J60_READ_CTRL_REG, address + 1);

	return (rh << 8) | rl;
}

static int locked_regw_read(struct enc28j60_net *priv,
				     u8 address)
{
	int ret;

	mutex_lock(&priv->lock);
	ret = nolock_regw_read(priv, address);
	mutex_unlock(&priv->lock);

	return ret;
}

/*
 * Register byte write
 */
static void nolock_regb_write(struct enc28j60_net *priv,
				       u8 address, u8 data)
{
	enc28j60_set_bank(priv, address);
	spi_write_op(priv, ENC28J60_WRITE_CTRL_REG, address, data);
}

static void locked_regb_write(struct enc28j60_net *priv,
				       u8 address, u8 data)
{
	mutex_lock(&priv->lock);
	nolock_regb_write(priv, address, data);
	mutex_unlock(&priv->lock);
}

/*
 * Register word write
 */
static void nolock_regw_write(struct enc28j60_net *priv,
				       u8 address, u16 data)
{
	enc28j60_set_bank(priv, address);
	spi_write_op(priv, ENC28J60_WRITE_CTRL_REG, address, (u8) data);
	spi_write_op(priv, ENC28J60_WRITE_CTRL_REG, address + 1,
		     (u8) (data >> 8));
}

static void locked_regw_write(struct enc28j60_net *priv,
				       u8 address, u16 data)
{
	mutex_lock(&priv->lock);
	nolock_regw_write(priv, address, data);
	mutex_unlock(&priv->lock);
}

/*
 * Buffer memory read
 * Select the starting address and execute a SPI buffer read
 */
static void enc28j60_mem_read(struct enc28j60_net *priv,
				     u16 addr, int len, u8 *data)
{
	mutex_lock(&priv->lock);
	nolock_regw_write(priv, ERDPTL, addr);
#ifdef CONFIG_ENC28J60_WRITEVERIFY
	if (netif_msg_drv(priv)) {
		u16 reg;
		reg = nolock_regw_read(priv, ERDPTL);
		if (reg != addr)
			printk(KERN_DEBUG DRV_NAME ": %s() error writing ERDPT "
				"(0x%04x - 0x%04x)\n", __func__, reg, addr);
	}
#endif
	spi_read_buf(priv, len, data);
	mutex_unlock(&priv->lock);
}

/*
 * Write packet to enc28j60 TX buffer memory
 */
static void
enc28j60_packet_write(struct enc28j60_net *priv, int len, const u8 *data)
{
	mutex_lock(&priv->lock);
	/* Set the write pointer to start of transmit buffer area */
	nolock_regw_write(priv, EWRPTL, TXSTART_INIT);
#ifdef CONFIG_ENC28J60_WRITEVERIFY
	if (netif_msg_drv(priv)) {
		u16 reg;
		reg = nolock_regw_read(priv, EWRPTL);
		if (reg != TXSTART_INIT)
			printk(KERN_DEBUG DRV_NAME
				": %s() ERWPT:0x%04x != 0x%04x\n",
				__func__, reg, TXSTART_INIT);
	}
#endif
	/* Set the TXND pointer to correspond to the packet size given */
	nolock_regw_write(priv, ETXNDL, TXSTART_INIT + len);
	/* write per-packet control byte */
	spi_write_op(priv, ENC28J60_WRITE_BUF_MEM, 0, 0x00);
	if (netif_msg_hw(priv))
		printk(KERN_DEBUG DRV_NAME
			": %s() after control byte ERWPT:0x%04x\n",
			__func__, nolock_regw_read(priv, EWRPTL));
	/* copy the packet into the transmit buffer */
	spi_write_buf(priv, len, data);
	if (netif_msg_hw(priv))
		printk(KERN_DEBUG DRV_NAME
			 ": %s() after write packet ERWPT:0x%04x, len=%d\n",
			 __func__, nolock_regw_read(priv, EWRPTL), len);
	mutex_unlock(&priv->lock);
}

static unsigned long msec20_to_jiffies;

static int poll_ready(struct enc28j60_net *priv, u8 reg, u8 mask, u8 val)
{
	unsigned long timeout = jiffies + msec20_to_jiffies;

	/* 20 msec timeout read */
	while ((nolock_regb_read(priv, reg) & mask) != val) {
		if (time_after(jiffies, timeout)) {
			if (netif_msg_drv(priv))
				dev_dbg(&priv->spi->dev,
					"reg %02x ready timeout!\n", reg);
			return -ETIMEDOUT;
		}
		cpu_relax();
	}
	return 0;
}

/*
 * Wait until the PHY operation is complete.
 */
static int wait_phy_ready(struct enc28j60_net *priv)
{
	return poll_ready(priv, MISTAT, MISTAT_BUSY, 0) ? 0 : 1;
}

/*
 * PHY register read
 * PHY registers are not accessed directly, but through the MII
 */
static u16 enc28j60_phy_read(struct enc28j60_net *priv, u8 address)
{
	u16 ret;

	mutex_lock(&priv->lock);
	/* set the PHY register address */
	nolock_regb_write(priv, MIREGADR, address);
	/* start the register read operation */
	nolock_regb_write(priv, MICMD, MICMD_MIIRD);
	/* wait until the PHY read completes */
	wait_phy_ready(priv);
	/* quit reading */
	nolock_regb_write(priv, MICMD, 0x00);
	/* return the data */
	ret  = nolock_regw_read(priv, MIRDL);
	mutex_unlock(&priv->lock);

	return ret;
}

static int enc28j60_phy_write(struct enc28j60_net *priv, u8 address, u16 data)
{
	int ret;

	mutex_lock(&priv->lock);
	/* set the PHY register address */
	nolock_regb_write(priv, MIREGADR, address);
	/* write the PHY data */
	nolock_regw_write(priv, MIWRL, data);
	/* wait until the PHY write completes and return */
	ret = wait_phy_ready(priv);
	mutex_unlock(&priv->lock);

	return ret;
}

/*
 * Program the hardware MAC address from dev->dev_addr.
 */
static int enc28j60_set_hw_macaddr(struct net_device *ndev)
{
	int ret;
	struct enc28j60_net *priv = netdev_priv(ndev);

	mutex_lock(&priv->lock);
	if (!priv->hw_enable) {
		if (netif_msg_drv(priv))
			printk(KERN_INFO DRV_NAME
				": %s: Setting MAC address to %pM\n",
				ndev->name, ndev->dev_addr);
		/* NOTE: MAC address in ENC28J60 is byte-backward */
		nolock_regb_write(priv, MAADR5, ndev->dev_addr[0]);
		nolock_regb_write(priv, MAADR4, ndev->dev_addr[1]);
		nolock_regb_write(priv, MAADR3, ndev->dev_addr[2]);
		nolock_regb_write(priv, MAADR2, ndev->dev_addr[3]);
		nolock_regb_write(priv, MAADR1, ndev->dev_addr[4]);
		nolock_regb_write(priv, MAADR0, ndev->dev_addr[5]);
		ret = 0;
	} else {
		if (netif_msg_drv(priv))
			printk(KERN_DEBUG DRV_NAME
				": %s() Hardware must be disabled to set "
				"Mac address\n", __func__);
		ret = -EBUSY;
	}
	mutex_unlock(&priv->lock);
	return ret;
}

/*
 * Store the new hardware address in dev->dev_addr, and update the MAC.
 */
static int enc28j60_set_mac_address(struct net_device *dev, void *addr)
{
	struct sockaddr *address = addr;

	if (netif_running(dev))
		return -EBUSY;
	if (!is_valid_ether_addr(address->sa_data))
		return -EADDRNOTAVAIL;

	memcpy(dev->dev_addr, address->sa_data, dev->addr_len);
	return enc28j60_set_hw_macaddr(dev);
}

/*
 * Debug routine to dump useful register contents
 */
static void enc28j60_dump_regs(struct enc28j60_net *priv, const char *msg)
{
	mutex_lock(&priv->lock);
	printk(KERN_DEBUG DRV_NAME " %s\n"
		"HwRevID: 0x%02x\n"
		"Cntrl: ECON1 ECON2 ESTAT  EIR  EIE\n"
		"       0x%02x  0x%02x  0x%02x  0x%02x  0x%02x\n"
		"MAC  : MACON1 MACON3 MACON4\n"
		"       0x%02x   0x%02x   0x%02x\n"
		"Rx   : ERXST  ERXND  ERXWRPT ERXRDPT ERXFCON EPKTCNT MAMXFL\n"
		"       0x%04x 0x%04x 0x%04x  0x%04x  "
		"0x%02x    0x%02x    0x%04x\n"
		"Tx   : ETXST  ETXND  MACLCON1 MACLCON2 MAPHSUP\n"
		"       0x%04x 0x%04x 0x%02x     0x%02x     0x%02x\n",
		msg, nolock_regb_read(priv, EREVID),
		nolock_regb_read(priv, ECON1), nolock_regb_read(priv, ECON2),
		nolock_regb_read(priv, ESTAT), nolock_regb_read(priv, EIR),
		nolock_regb_read(priv, EIE), nolock_regb_read(priv, MACON1),
		nolock_regb_read(priv, MACON3), nolock_regb_read(priv, MACON4),
		nolock_regw_read(priv, ERXSTL), nolock_regw_read(priv, ERXNDL),
		nolock_regw_read(priv, ERXWRPTL),
		nolock_regw_read(priv, ERXRDPTL),
		nolock_regb_read(priv, ERXFCON),
		nolock_regb_read(priv, EPKTCNT),
		nolock_regw_read(priv, MAMXFLL), nolock_regw_read(priv, ETXSTL),
		nolock_regw_read(priv, ETXNDL),
		nolock_regb_read(priv, MACLCON1),
		nolock_regb_read(priv, MACLCON2),
		nolock_regb_read(priv, MAPHSUP));
	mutex_unlock(&priv->lock);
}

/*
 * ERXRDPT need to be set always at odd addresses, refer to errata datasheet
 */
static u16 erxrdpt_workaround(u16 next_packet_ptr, u16 start, u16 end)
{
	u16 erxrdpt;

	if ((next_packet_ptr - 1 < start) || (next_packet_ptr - 1 > end))
		erxrdpt = end;
	else
		erxrdpt = next_packet_ptr - 1;

	return erxrdpt;
}

/*
 * Calculate wrap around when reading beyond the end of the RX buffer
 */
static u16 rx_packet_start(u16 ptr)
{
	if (ptr + RSV_SIZE > RXEND_INIT)
		return (ptr + RSV_SIZE) - (RXEND_INIT - RXSTART_INIT + 1);
	else
		return ptr + RSV_SIZE;
}

static void nolock_rxfifo_init(struct enc28j60_net *priv, u16 start, u16 end)
{
	u16 erxrdpt;

	if (start > 0x1FFF || end > 0x1FFF || start > end) {
		if (netif_msg_drv(priv))
			printk(KERN_ERR DRV_NAME ": %s(%d, %d) RXFIFO "
				"bad parameters!\n", __func__, start, end);
		return;
	}
	/* set receive buffer start + end */
	priv->next_pk_ptr = start;
	nolock_regw_write(priv, ERXSTL, start);
	erxrdpt = erxrdpt_workaround(priv->next_pk_ptr, start, end);
	nolock_regw_write(priv, ERXRDPTL, erxrdpt);
	nolock_regw_write(priv, ERXNDL, end);
}

static void nolock_txfifo_init(struct enc28j60_net *priv, u16 start, u16 end)
{
	if (start > 0x1FFF || end > 0x1FFF || start > end) {
		if (netif_msg_drv(priv))
			printk(KERN_ERR DRV_NAME ": %s(%d, %d) TXFIFO "
				"bad parameters!\n", __func__, start, end);
		return;
	}
	/* set transmit buffer start + end */
	nolock_regw_write(priv, ETXSTL, start);
	nolock_regw_write(priv, ETXNDL, end);
}

/*
 * Low power mode shrinks power consumption about 100x, so we'd like
 * the chip to be in that mode whenever it's inactive.  (However, we
 * can't stay in lowpower mode during suspend with WOL active.)
 */
static void enc28j60_lowpower(struct enc28j60_net *priv, bool is_low)
{
	if (netif_msg_drv(priv))
		dev_dbg(&priv->spi->dev, "%s power...\n",
				is_low ? "low" : "high");

	mutex_lock(&priv->lock);
	if (is_low) {
		nolock_reg_bfclr(priv, ECON1, ECON1_RXEN);
		poll_ready(priv, ESTAT, ESTAT_RXBUSY, 0);
		poll_ready(priv, ECON1, ECON1_TXRTS, 0);
		/* ECON2_VRPS was set during initialization */
		nolock_reg_bfset(priv, ECON2, ECON2_PWRSV);
	} else {
		nolock_reg_bfclr(priv, ECON2, ECON2_PWRSV);
		poll_ready(priv, ESTAT, ESTAT_CLKRDY, ESTAT_CLKRDY);
		/* caller sets ECON1_RXEN */
	}
	mutex_unlock(&priv->lock);
}

static int enc28j60_hw_init(struct enc28j60_net *priv)
{
	u8 reg;

	if (netif_msg_drv(priv))
		printk(KERN_DEBUG DRV_NAME ": %s() - %s\n", __func__,
			priv->full_duplex ? "FullDuplex" : "HalfDuplex");

	mutex_lock(&priv->lock);
	/* first reset the chip */
	enc28j60_soft_reset(priv);
	/* Clear ECON1 */
	spi_write_op(priv, ENC28J60_WRITE_CTRL_REG, ECON1, 0x00);
	priv->bank = 0;
	priv->hw_enable = false;
	priv->tx_retry_count = 0;
	priv->max_pk_counter = 0;
	priv->rxfilter = RXFILTER_NORMAL;
	/* enable address auto increment and voltage regulator powersave */
	nolock_regb_write(priv, ECON2, ECON2_AUTOINC | ECON2_VRPS);

	nolock_rxfifo_init(priv, RXSTART_INIT, RXEND_INIT);
	nolock_txfifo_init(priv, TXSTART_INIT, TXEND_INIT);
	mutex_unlock(&priv->lock);

	/*
	 * Check the RevID.
	 * If it's 0x00 or 0xFF probably the enc28j60 is not mounted or
	 * damaged
	 */
	reg = locked_regb_read(priv, EREVID);
	if (netif_msg_drv(priv))
		printk(KERN_INFO DRV_NAME ": chip RevID: 0x%02x\n", reg);
	if (reg == 0x00 || reg == 0xff) {
		if (netif_msg_drv(priv))
			printk(KERN_DEBUG DRV_NAME ": %s() Invalid RevId %d\n",
				__func__, reg);
		return 0;
	}

	/* default filter mode: (unicast OR broadcast) AND crc valid */
	locked_regb_write(priv, ERXFCON,
			    ERXFCON_UCEN | ERXFCON_CRCEN | ERXFCON_BCEN);

	/* enable MAC receive */
	locked_regb_write(priv, MACON1,
			    MACON1_MARXEN | MACON1_TXPAUS | MACON1_RXPAUS);
	/* enable automatic padding and CRC operations */
	if (priv->full_duplex) {
		locked_regb_write(priv, MACON3,
				    MACON3_PADCFG0 | MACON3_TXCRCEN |
				    MACON3_FRMLNEN | MACON3_FULDPX);
		/* set inter-frame gap (non-back-to-back) */
		locked_regb_write(priv, MAIPGL, 0x12);
		/* set inter-frame gap (back-to-back) */
		locked_regb_write(priv, MABBIPG, 0x15);
	} else {
		locked_regb_write(priv, MACON3,
				    MACON3_PADCFG0 | MACON3_TXCRCEN |
				    MACON3_FRMLNEN);
		locked_regb_write(priv, MACON4, 1 << 6);	/* DEFER bit */
		/* set inter-frame gap (non-back-to-back) */
		locked_regw_write(priv, MAIPGL, 0x0C12);
		/* set inter-frame gap (back-to-back) */
		locked_regb_write(priv, MABBIPG, 0x12);
	}
	/*
	 * MACLCON1 (default)
	 * MACLCON2 (default)
	 * Set the maximum packet size which the controller will accept
	 */
	locked_regw_write(priv, MAMXFLL, MAX_FRAMELEN);

	/* Configure LEDs */
	if (!enc28j60_phy_write(priv, PHLCON, ENC28J60_LAMPS_MODE))
		return 0;

	if (priv->full_duplex) {
		if (!enc28j60_phy_write(priv, PHCON1, PHCON1_PDPXMD))
			return 0;
		if (!enc28j60_phy_write(priv, PHCON2, 0x00))
			return 0;
	} else {
		if (!enc28j60_phy_write(priv, PHCON1, 0x00))
			return 0;
		if (!enc28j60_phy_write(priv, PHCON2, PHCON2_HDLDIS))
			return 0;
	}
	if (netif_msg_hw(priv))
		enc28j60_dump_regs(priv, "Hw initialized.");

	return 1;
}

static void enc28j60_hw_enable(struct enc28j60_net *priv)
{
	/* enable interrupts */
	if (netif_msg_hw(priv))
		printk(KERN_DEBUG DRV_NAME ": %s() enabling interrupts.\n",
			__func__);

	enc28j60_phy_write(priv, PHIE, PHIE_PGEIE | PHIE_PLNKIE);

	mutex_lock(&priv->lock);
	nolock_reg_bfclr(priv, EIR, EIR_DMAIF | EIR_LINKIF |
			 EIR_TXIF | EIR_TXERIF | EIR_RXERIF | EIR_PKTIF);
	nolock_regb_write(priv, EIE, EIE_INTIE | EIE_PKTIE | EIE_LINKIE |
			  EIE_TXIE | EIE_TXERIE | EIE_RXERIE);

	/* enable receive logic */
	nolock_reg_bfset(priv, ECON1, ECON1_RXEN);
	priv->hw_enable = true;
	mutex_unlock(&priv->lock);
}

static void enc28j60_hw_disable(struct enc28j60_net *priv)
{
	mutex_lock(&priv->lock);
	/* disable interrutps and packet reception */
	nolock_regb_write(priv, EIE, 0x00);
	nolock_reg_bfclr(priv, ECON1, ECON1_RXEN);
	priv->hw_enable = false;
	mutex_unlock(&priv->lock);
}

static int
enc28j60_setlink(struct net_device *ndev, u8 autoneg, u16 speed, u8 duplex)
{
	struct enc28j60_net *priv = netdev_priv(ndev);
	int ret = 0;

	if (!priv->hw_enable) {
		/* link is in low power mode now; duplex setting
		 * will take effect on next enc28j60_hw_init().
		 */
		if (autoneg == AUTONEG_DISABLE && speed == SPEED_10)
			priv->full_duplex = (duplex == DUPLEX_FULL);
		else {
			if (netif_msg_link(priv))
				dev_warn(&ndev->dev,
					"unsupported link setting\n");
			ret = -EOPNOTSUPP;
		}
	} else {
		if (netif_msg_link(priv))
			dev_warn(&ndev->dev, "Warning: hw must be disabled "
				"to set link mode\n");
		ret = -EBUSY;
	}
	return ret;
}

/*
 * Read the Transmit Status Vector
 */
static void enc28j60_read_tsv(struct enc28j60_net *priv, u8 tsv[TSV_SIZE])
{
	int endptr;

	endptr = locked_regw_read(priv, ETXNDL);
	if (netif_msg_hw(priv))
		printk(KERN_DEBUG DRV_NAME ": reading TSV at addr:0x%04x\n",
			 endptr + 1);
	enc28j60_mem_read(priv, endptr + 1, sizeof(tsv), tsv);
}

static void enc28j60_dump_tsv(struct enc28j60_net *priv, const char *msg,
				u8 tsv[TSV_SIZE])
{
	u16 tmp1, tmp2;

	printk(KERN_DEBUG DRV_NAME ": %s - TSV:\n", msg);
	tmp1 = tsv[1];
	tmp1 <<= 8;
	tmp1 |= tsv[0];

	tmp2 = tsv[5];
	tmp2 <<= 8;
	tmp2 |= tsv[4];

	printk(KERN_DEBUG DRV_NAME ": ByteCount: %d, CollisionCount: %d,"
		" TotByteOnWire: %d\n", tmp1, tsv[2] & 0x0f, tmp2);
	printk(KERN_DEBUG DRV_NAME ": TxDone: %d, CRCErr:%d, LenChkErr: %d,"
		" LenOutOfRange: %d\n", TSV_GETBIT(tsv, TSV_TXDONE),
		TSV_GETBIT(tsv, TSV_TXCRCERROR),
		TSV_GETBIT(tsv, TSV_TXLENCHKERROR),
		TSV_GETBIT(tsv, TSV_TXLENOUTOFRANGE));
	printk(KERN_DEBUG DRV_NAME ": Multicast: %d, Broadcast: %d, "
		"PacketDefer: %d, ExDefer: %d\n",
		TSV_GETBIT(tsv, TSV_TXMULTICAST),
		TSV_GETBIT(tsv, TSV_TXBROADCAST),
		TSV_GETBIT(tsv, TSV_TXPACKETDEFER),
		TSV_GETBIT(tsv, TSV_TXEXDEFER));
	printk(KERN_DEBUG DRV_NAME ": ExCollision: %d, LateCollision: %d, "
		 "Giant: %d, Underrun: %d\n",
		 TSV_GETBIT(tsv, TSV_TXEXCOLLISION),
		 TSV_GETBIT(tsv, TSV_TXLATECOLLISION),
		 TSV_GETBIT(tsv, TSV_TXGIANT), TSV_GETBIT(tsv, TSV_TXUNDERRUN));
	printk(KERN_DEBUG DRV_NAME ": ControlFrame: %d, PauseFrame: %d, "
		 "BackPressApp: %d, VLanTagFrame: %d\n",
		 TSV_GETBIT(tsv, TSV_TXCONTROLFRAME),
		 TSV_GETBIT(tsv, TSV_TXPAUSEFRAME),
		 TSV_GETBIT(tsv, TSV_BACKPRESSUREAPP),
		 TSV_GETBIT(tsv, TSV_TXVLANTAGFRAME));
}

/*
 * Receive Status vector
 */
static void enc28j60_dump_rsv(struct enc28j60_net *priv, const char *msg,
			      u16 pk_ptr, int len, u16 sts)
{
	printk(KERN_DEBUG DRV_NAME ": %s - NextPk: 0x%04x - RSV:\n",
		msg, pk_ptr);
	printk(KERN_DEBUG DRV_NAME ": ByteCount: %d, DribbleNibble: %d\n", len,
		 RSV_GETBIT(sts, RSV_DRIBBLENIBBLE));
	printk(KERN_DEBUG DRV_NAME ": RxOK: %d, CRCErr:%d, LenChkErr: %d,"
		 " LenOutOfRange: %d\n", RSV_GETBIT(sts, RSV_RXOK),
		 RSV_GETBIT(sts, RSV_CRCERROR),
		 RSV_GETBIT(sts, RSV_LENCHECKERR),
		 RSV_GETBIT(sts, RSV_LENOUTOFRANGE));
	printk(KERN_DEBUG DRV_NAME ": Multicast: %d, Broadcast: %d, "
		 "LongDropEvent: %d, CarrierEvent: %d\n",
		 RSV_GETBIT(sts, RSV_RXMULTICAST),
		 RSV_GETBIT(sts, RSV_RXBROADCAST),
		 RSV_GETBIT(sts, RSV_RXLONGEVDROPEV),
		 RSV_GETBIT(sts, RSV_CARRIEREV));
	printk(KERN_DEBUG DRV_NAME ": ControlFrame: %d, PauseFrame: %d,"
		 " UnknownOp: %d, VLanTagFrame: %d\n",
		 RSV_GETBIT(sts, RSV_RXCONTROLFRAME),
		 RSV_GETBIT(sts, RSV_RXPAUSEFRAME),
		 RSV_GETBIT(sts, RSV_RXUNKNOWNOPCODE),
		 RSV_GETBIT(sts, RSV_RXTYPEVLAN));
}

static void dump_packet(const char *msg, int len, const char *data)
{
	printk(KERN_DEBUG DRV_NAME ": %s - packet len:%d\n", msg, len);
	print_hex_dump(KERN_DEBUG, "pk data: ", DUMP_PREFIX_OFFSET, 16, 1,
			data, len, true);
}

/*
 * Hardware receive function.
 * Read the buffer memory, update the FIFO pointer to free the buffer,
 * check the status vector and decrement the packet counter.
 */
static void enc28j60_hw_rx(struct net_device *ndev)
{
	struct enc28j60_net *priv = netdev_priv(ndev);
	struct sk_buff *skb = NULL;
	u16 erxrdpt, next_packet, rxstat;
	u8 rsv[RSV_SIZE];
	int len;

	if (netif_msg_rx_status(priv))
		printk(KERN_DEBUG DRV_NAME ": RX pk_addr:0x%04x\n",
			priv->next_pk_ptr);

	if (unlikely(priv->next_pk_ptr > RXEND_INIT)) {
		if (netif_msg_rx_err(priv))
			dev_err(&ndev->dev,
				"%s() Invalid packet address!! 0x%04x\n",
				__func__, priv->next_pk_ptr);
		/* packet address corrupted: reset RX logic */
		mutex_lock(&priv->lock);
		nolock_reg_bfclr(priv, ECON1, ECON1_RXEN);
		nolock_reg_bfset(priv, ECON1, ECON1_RXRST);
		nolock_reg_bfclr(priv, ECON1, ECON1_RXRST);
		nolock_rxfifo_init(priv, RXSTART_INIT, RXEND_INIT);
		nolock_reg_bfclr(priv, EIR, EIR_RXERIF);
		nolock_reg_bfset(priv, ECON1, ECON1_RXEN);
		mutex_unlock(&priv->lock);
		ndev->stats.rx_errors++;
		return;
	}
	/* Read next packet pointer and rx status vector */
	enc28j60_mem_read(priv, priv->next_pk_ptr, sizeof(rsv), rsv);

	next_packet = rsv[1];
	next_packet <<= 8;
	next_packet |= rsv[0];

	len = rsv[3];
	len <<= 8;
	len |= rsv[2];

	rxstat = rsv[5];
	rxstat <<= 8;
	rxstat |= rsv[4];

	if (netif_msg_rx_status(priv))
		enc28j60_dump_rsv(priv, __func__, next_packet, len, rxstat);

	if (!RSV_GETBIT(rxstat, RSV_RXOK) || len > MAX_FRAMELEN) {
		if (netif_msg_rx_err(priv))
			dev_err(&ndev->dev, "Rx Error (%04x)\n", rxstat);
		ndev->stats.rx_errors++;
		if (RSV_GETBIT(rxstat, RSV_CRCERROR))
			ndev->stats.rx_crc_errors++;
		if (RSV_GETBIT(rxstat, RSV_LENCHECKERR))
			ndev->stats.rx_frame_errors++;
		if (len > MAX_FRAMELEN)
			ndev->stats.rx_over_errors++;
	} else {
		skb = dev_alloc_skb(len + NET_IP_ALIGN);
		if (!skb) {
			if (netif_msg_rx_err(priv))
				dev_err(&ndev->dev,
					"out of memory for Rx'd frame\n");
			ndev->stats.rx_dropped++;
		} else {
			skb->dev = ndev;
			skb_reserve(skb, NET_IP_ALIGN);
			/* copy the packet from the receive buffer */
			enc28j60_mem_read(priv,
				rx_packet_start(priv->next_pk_ptr),
				len, skb_put(skb, len));
			if (netif_msg_pktdata(priv))
				dump_packet(__func__, skb->len, skb->data);
			skb->protocol = eth_type_trans(skb, ndev);
			/* update statistics */
			ndev->stats.rx_packets++;
			ndev->stats.rx_bytes += len;
			netif_rx_ni(skb);
		}
	}
	/*
	 * Move the RX read pointer to the start of the next
	 * received packet.
	 * This frees the memory we just read out
	 */
	erxrdpt = erxrdpt_workaround(next_packet, RXSTART_INIT, RXEND_INIT);
	if (netif_msg_hw(priv))
		printk(KERN_DEBUG DRV_NAME ": %s() ERXRDPT:0x%04x\n",
			__func__, erxrdpt);

	mutex_lock(&priv->lock);
	nolock_regw_write(priv, ERXRDPTL, erxrdpt);
#ifdef CONFIG_ENC28J60_WRITEVERIFY
	if (netif_msg_drv(priv)) {
		u16 reg;
		reg = nolock_regw_read(priv, ERXRDPTL);
		if (reg != erxrdpt)
			printk(KERN_DEBUG DRV_NAME ": %s() ERXRDPT verify "
				"error (0x%04x - 0x%04x)\n", __func__,
				reg, erxrdpt);
	}
#endif
	priv->next_pk_ptr = next_packet;
	/* we are done with this packet, decrement the packet counter */
	nolock_reg_bfset(priv, ECON2, ECON2_PKTDEC);
	mutex_unlock(&priv->lock);
}

/*
 * Calculate free space in RxFIFO
 */
static int enc28j60_get_free_rxfifo(struct enc28j60_net *priv)
{
	int epkcnt, erxst, erxnd, erxwr, erxrd;
	int free_space;

	mutex_lock(&priv->lock);
	epkcnt = nolock_regb_read(priv, EPKTCNT);
	if (epkcnt >= 255)
		free_space = -1;
	else {
		erxst = nolock_regw_read(priv, ERXSTL);
		erxnd = nolock_regw_read(priv, ERXNDL);
		erxwr = nolock_regw_read(priv, ERXWRPTL);
		erxrd = nolock_regw_read(priv, ERXRDPTL);

		if (erxwr > erxrd)
			free_space = (erxnd - erxst) - (erxwr - erxrd);
		else if (erxwr == erxrd)
			free_space = (erxnd - erxst);
		else
			free_space = erxrd - erxwr - 1;
	}
	mutex_unlock(&priv->lock);
	if (netif_msg_rx_status(priv))
		printk(KERN_DEBUG DRV_NAME ": %s() free_space = %d\n",
			__func__, free_space);
	return free_space;
}

/*
 * Access the PHY to determine link status
 */
static void enc28j60_check_link_status(struct net_device *ndev)
{
	struct enc28j60_net *priv = netdev_priv(ndev);
	u16 reg;
	int duplex;

	reg = enc28j60_phy_read(priv, PHSTAT2);
	if (netif_msg_hw(priv))
		printk(KERN_DEBUG DRV_NAME ": %s() PHSTAT1: %04x, "
			"PHSTAT2: %04x\n", __func__,
			enc28j60_phy_read(priv, PHSTAT1), reg);
	duplex = reg & PHSTAT2_DPXSTAT;

	if (reg & PHSTAT2_LSTAT) {
		netif_carrier_on(ndev);
		if (netif_msg_ifup(priv))
			dev_info(&ndev->dev, "link up - %s\n",
				duplex ? "Full duplex" : "Half duplex");
	} else {
		if (netif_msg_ifdown(priv))
			dev_info(&ndev->dev, "link down\n");
		netif_carrier_off(ndev);
	}
}

static void enc28j60_tx_clear(struct net_device *ndev, bool err)
{
	struct enc28j60_net *priv = netdev_priv(ndev);

	if (err)
		ndev->stats.tx_errors++;
	else
		ndev->stats.tx_packets++;

	if (priv->tx_skb) {
		if (!err)
			ndev->stats.tx_bytes += priv->tx_skb->len;
		dev_kfree_skb(priv->tx_skb);
		priv->tx_skb = NULL;
	}
	locked_reg_bfclr(priv, ECON1, ECON1_TXRTS);
	netif_wake_queue(ndev);
}

/*
 * RX handler
 * ignore PKTIF because is unreliable! (look at the errata datasheet)
 * check EPKTCNT is the suggested workaround.
 * We don't need to clear interrupt flag, automatically done when
 * enc28j60_hw_rx() decrements the packet counter.
 * Returns how many packet processed.
 */
static int enc28j60_rx_interrupt(struct net_device *ndev)
{
	struct enc28j60_net *priv = netdev_priv(ndev);
	int pk_counter, ret;

	pk_counter = locked_regb_read(priv, EPKTCNT);
	if (pk_counter && netif_msg_intr(priv))
		printk(KERN_DEBUG DRV_NAME ": intRX, pk_cnt: %d\n", pk_counter);
	if (pk_counter > priv->max_pk_counter) {
		/* update statistics */
		priv->max_pk_counter = pk_counter;
		if (netif_msg_rx_status(priv) && priv->max_pk_counter > 1)
			printk(KERN_DEBUG DRV_NAME ": RX max_pk_cnt: %d\n",
				priv->max_pk_counter);
	}
	ret = pk_counter;
	while (pk_counter-- > 0)
		enc28j60_hw_rx(ndev);

	return ret;
}

static void enc28j60_irq_work_handler(struct work_struct *work)
{
	struct enc28j60_net *priv =
		container_of(work, struct enc28j60_net, irq_work);
	struct net_device *ndev = priv->netdev;
	int intflags, loop;

	if (netif_msg_intr(priv))
		printk(KERN_DEBUG DRV_NAME ": %s() enter\n", __func__);
	/* disable further interrupts */
	locked_reg_bfclr(priv, EIE, EIE_INTIE);

	do {
		loop = 0;
		intflags = locked_regb_read(priv, EIR);
		/* DMA interrupt handler (not currently used) */
		if ((intflags & EIR_DMAIF) != 0) {
			loop++;
			if (netif_msg_intr(priv))
				printk(KERN_DEBUG DRV_NAME
					": intDMA(%d)\n", loop);
			locked_reg_bfclr(priv, EIR, EIR_DMAIF);
		}
		/* LINK changed handler */
		if ((intflags & EIR_LINKIF) != 0) {
			loop++;
			if (netif_msg_intr(priv))
				printk(KERN_DEBUG DRV_NAME
					": intLINK(%d)\n", loop);
			enc28j60_check_link_status(ndev);
			/* read PHIR to clear the flag */
			enc28j60_phy_read(priv, PHIR);
		}
		/* TX complete handler */
		if ((intflags & EIR_TXIF) != 0) {
			bool err = false;
			loop++;
			if (netif_msg_intr(priv))
				printk(KERN_DEBUG DRV_NAME
					": intTX(%d)\n", loop);
			priv->tx_retry_count = 0;
			if (locked_regb_read(priv, ESTAT) & ESTAT_TXABRT) {
				if (netif_msg_tx_err(priv))
					dev_err(&ndev->dev,
						"Tx Error (aborted)\n");
				err = true;
			}
			if (netif_msg_tx_done(priv)) {
				u8 tsv[TSV_SIZE];
				enc28j60_read_tsv(priv, tsv);
				enc28j60_dump_tsv(priv, "Tx Done", tsv);
			}
			enc28j60_tx_clear(ndev, err);
			locked_reg_bfclr(priv, EIR, EIR_TXIF);
		}
		/* TX Error handler */
		if ((intflags & EIR_TXERIF) != 0) {
			u8 tsv[TSV_SIZE];

			loop++;
			if (netif_msg_intr(priv))
				printk(KERN_DEBUG DRV_NAME
					": intTXErr(%d)\n", loop);
			locked_reg_bfclr(priv, ECON1, ECON1_TXRTS);
			enc28j60_read_tsv(priv, tsv);
			if (netif_msg_tx_err(priv))
				enc28j60_dump_tsv(priv, "Tx Error", tsv);
			/* Reset TX logic */
			mutex_lock(&priv->lock);
			nolock_reg_bfset(priv, ECON1, ECON1_TXRST);
			nolock_reg_bfclr(priv, ECON1, ECON1_TXRST);
			nolock_txfifo_init(priv, TXSTART_INIT, TXEND_INIT);
			mutex_unlock(&priv->lock);
			/* Transmit Late collision check for retransmit */
			if (TSV_GETBIT(tsv, TSV_TXLATECOLLISION)) {
				if (netif_msg_tx_err(priv))
					printk(KERN_DEBUG DRV_NAME
						": LateCollision TXErr (%d)\n",
						priv->tx_retry_count);
				if (priv->tx_retry_count++ < MAX_TX_RETRYCOUNT)
					locked_reg_bfset(priv, ECON1,
							   ECON1_TXRTS);
				else
					enc28j60_tx_clear(ndev, true);
			} else
				enc28j60_tx_clear(ndev, true);
			locked_reg_bfclr(priv, EIR, EIR_TXERIF);
		}
		/* RX Error handler */
		if ((intflags & EIR_RXERIF) != 0) {
			loop++;
			if (netif_msg_intr(priv))
				printk(KERN_DEBUG DRV_NAME
					": intRXErr(%d)\n", loop);
			/* Check free FIFO space to flag RX overrun */
			if (enc28j60_get_free_rxfifo(priv) <= 0) {
				if (netif_msg_rx_err(priv))
					printk(KERN_DEBUG DRV_NAME
						": RX Overrun\n");
				ndev->stats.rx_dropped++;
			}
			locked_reg_bfclr(priv, EIR, EIR_RXERIF);
		}
		/* RX handler */
		if (enc28j60_rx_interrupt(ndev))
			loop++;
	} while (loop);

	/* re-enable interrupts */
	locked_reg_bfset(priv, EIE, EIE_INTIE);
	if (netif_msg_intr(priv))
		printk(KERN_DEBUG DRV_NAME ": %s() exit\n", __func__);
}

/*
 * Hardware transmit function.
 * Fill the buffer memory and send the contents of the transmit buffer
 * onto the network
 */
static void enc28j60_hw_tx(struct enc28j60_net *priv)
{
	if (netif_msg_tx_queued(priv))
		printk(KERN_DEBUG DRV_NAME
			": Tx Packet Len:%d\n", priv->tx_skb->len);

	if (netif_msg_pktdata(priv))
		dump_packet(__func__,
			    priv->tx_skb->len, priv->tx_skb->data);
	enc28j60_packet_write(priv, priv->tx_skb->len, priv->tx_skb->data);

#ifdef CONFIG_ENC28J60_WRITEVERIFY
	/* readback and verify written data */
	if (netif_msg_drv(priv)) {
		int test_len, k;
		u8 test_buf[64]; /* limit the test to the first 64 bytes */
		int okflag;

		test_len = priv->tx_skb->len;
		if (test_len > sizeof(test_buf))
			test_len = sizeof(test_buf);

		/* + 1 to skip control byte */
		enc28j60_mem_read(priv, TXSTART_INIT + 1, test_len, test_buf);
		okflag = 1;
		for (k = 0; k < test_len; k++) {
			if (priv->tx_skb->data[k] != test_buf[k]) {
				printk(KERN_DEBUG DRV_NAME
					 ": Error, %d location differ: "
					 "0x%02x-0x%02x\n", k,
					 priv->tx_skb->data[k], test_buf[k]);
				okflag = 0;
			}
		}
		if (!okflag)
			printk(KERN_DEBUG DRV_NAME ": Tx write buffer, "
				"verify ERROR!\n");
	}
#endif
	/* set TX request flag */
	locked_reg_bfset(priv, ECON1, ECON1_TXRTS);
}

static netdev_tx_t enc28j60_send_packet(struct sk_buff *skb,
					struct net_device *dev)
{
	struct enc28j60_net *priv = netdev_priv(dev);

	if (netif_msg_tx_queued(priv))
		printk(KERN_DEBUG DRV_NAME ": %s() enter\n", __func__);

	/* If some error occurs while trying to transmit this
	 * packet, you should return '1' from this function.
	 * In such a case you _may not_ do anything to the
	 * SKB, it is still owned by the network queueing
	 * layer when an error is returned.  This means you
	 * may not modify any SKB fields, you may not free
	 * the SKB, etc.
	 */
	netif_stop_queue(dev);

	/* Remember the skb for deferred processing */
	priv->tx_skb = skb;
	schedule_work(&priv->tx_work);

	return NETDEV_TX_OK;
}

static void enc28j60_tx_work_handler(struct work_struct *work)
{
	struct enc28j60_net *priv =
		container_of(work, struct enc28j60_net, tx_work);

	/* actual delivery of data */
	enc28j60_hw_tx(priv);
}

static irqreturn_t enc28j60_irq(int irq, void *dev_id)
{
	struct enc28j60_net *priv = dev_id;

	/*
	 * Can't do anything in interrupt context because we need to
	 * block (spi_sync() is blocking) so fire of the interrupt
	 * handling workqueue.
	 * Remember that we access enc28j60 registers through SPI bus
	 * via spi_sync() call.
	 */
	schedule_work(&priv->irq_work);

	return IRQ_HANDLED;
}

static void enc28j60_tx_timeout(struct net_device *ndev)
{
	struct enc28j60_net *priv = netdev_priv(ndev);

	if (netif_msg_timer(priv))
		dev_err(&ndev->dev, DRV_NAME " tx timeout\n");

	ndev->stats.tx_errors++;
	/* can't restart safely under softirq */
	schedule_work(&priv->restart_work);
}

/*
 * Open/initialize the board. This is called (in the current kernel)
 * sometime after booting when the 'ifconfig' program is run.
 *
 * This routine should set everything up anew at each open, even
 * registers that "should" only need to be set once at boot, so that
 * there is non-reboot way to recover if something goes wrong.
 */
static int enc28j60_net_open(struct net_device *dev)
{
	struct enc28j60_net *priv = netdev_priv(dev);

	if (netif_msg_drv(priv))
		printk(KERN_DEBUG DRV_NAME ": %s() enter\n", __func__);

	if (!is_valid_ether_addr(dev->dev_addr)) {
		if (netif_msg_ifup(priv))
			dev_err(&dev->dev, "invalid MAC address %pM\n",
				dev->dev_addr);
		return -EADDRNOTAVAIL;
	}
	/* Reset the hardware here (and take it out of low power mode) */
	enc28j60_lowpower(priv, false);
	enc28j60_hw_disable(priv);
	if (!enc28j60_hw_init(priv)) {
		if (netif_msg_ifup(priv))
			dev_err(&dev->dev, "hw_reset() failed\n");
		return -EINVAL;
	}
	/* Update the MAC address (in case user has changed it) */
	enc28j60_set_hw_macaddr(dev);
	/* Enable interrupts */
	enc28j60_hw_enable(priv);
	/* check link status */
	enc28j60_check_link_status(dev);
	/* We are now ready to accept transmit requests from
	 * the queueing layer of the networking.
	 */
	netif_start_queue(dev);

	return 0;
}

/* The inverse routine to net_open(). */
static int enc28j60_net_close(struct net_device *dev)
{
	struct enc28j60_net *priv = netdev_priv(dev);

	if (netif_msg_drv(priv))
		printk(KERN_DEBUG DRV_NAME ": %s() enter\n", __func__);

	enc28j60_hw_disable(priv);
	enc28j60_lowpower(priv, true);
	netif_stop_queue(dev);

	return 0;
}

/*
 * Set or clear the multicast filter for this adapter
 * num_addrs == -1	Promiscuous mode, receive all packets
 * num_addrs == 0	Normal mode, filter out multicast packets
 * num_addrs > 0	Multicast mode, receive normal and MC packets
 */
static void enc28j60_set_multicast_list(struct net_device *dev)
{
	struct enc28j60_net *priv = netdev_priv(dev);
	int oldfilter = priv->rxfilter;

	if (dev->flags & IFF_PROMISC) {
		if (netif_msg_link(priv))
			dev_info(&dev->dev, "promiscuous mode\n");
		priv->rxfilter = RXFILTER_PROMISC;
	} else if ((dev->flags & IFF_ALLMULTI) || !netdev_mc_empty(dev)) {
		if (netif_msg_link(priv))
			dev_info(&dev->dev, "%smulticast mode\n",
				(dev->flags & IFF_ALLMULTI) ? "all-" : "");
		priv->rxfilter = RXFILTER_MULTI;
	} else {
		if (netif_msg_link(priv))
			dev_info(&dev->dev, "normal mode\n");
		priv->rxfilter = RXFILTER_NORMAL;
	}

	if (oldfilter != priv->rxfilter)
		schedule_work(&priv->setrx_work);
}

static void enc28j60_setrx_work_handler(struct work_struct *work)
{
	struct enc28j60_net *priv =
		container_of(work, struct enc28j60_net, setrx_work);

	if (priv->rxfilter == RXFILTER_PROMISC) {
		if (netif_msg_drv(priv))
			printk(KERN_DEBUG DRV_NAME ": promiscuous mode\n");
		locked_regb_write(priv, ERXFCON, 0x00);
	} else if (priv->rxfilter == RXFILTER_MULTI) {
		if (netif_msg_drv(priv))
			printk(KERN_DEBUG DRV_NAME ": multicast mode\n");
		locked_regb_write(priv, ERXFCON,
					ERXFCON_UCEN | ERXFCON_CRCEN |
					ERXFCON_BCEN | ERXFCON_MCEN);
	} else {
		if (netif_msg_drv(priv))
			printk(KERN_DEBUG DRV_NAME ": normal mode\n");
		locked_regb_write(priv, ERXFCON,
					ERXFCON_UCEN | ERXFCON_CRCEN |
					ERXFCON_BCEN);
	}
}

static void enc28j60_restart_work_handler(struct work_struct *work)
{
	struct enc28j60_net *priv =
			container_of(work, struct enc28j60_net, restart_work);
	struct net_device *ndev = priv->netdev;
	int ret;

	rtnl_lock();
	if (netif_running(ndev)) {
		enc28j60_net_close(ndev);
		ret = enc28j60_net_open(ndev);
		if (unlikely(ret)) {
			dev_info(&ndev->dev, " could not restart %d\n", ret);
			dev_close(ndev);
		}
	}
	rtnl_unlock();
}

/* ......................... ETHTOOL SUPPORT ........................... */

static void
enc28j60_get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *info)
{
	strlcpy(info->driver, DRV_NAME, sizeof(info->driver));
	strlcpy(info->version, DRV_VERSION, sizeof(info->version));
	strlcpy(info->bus_info,
		dev_name(dev->dev.parent), sizeof(info->bus_info));
}

static int
enc28j60_get_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct enc28j60_net *priv = netdev_priv(dev);

	cmd->transceiver = XCVR_INTERNAL;
	cmd->supported	= SUPPORTED_10baseT_Half
			| SUPPORTED_10baseT_Full
			| SUPPORTED_TP;
	cmd->speed	= SPEED_10;
	cmd->duplex	= priv->full_duplex ? DUPLEX_FULL : DUPLEX_HALF;
	cmd->port	= PORT_TP;
	cmd->autoneg	= AUTONEG_DISABLE;

	return 0;
}

static int
enc28j60_set_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	return enc28j60_setlink(dev, cmd->autoneg, cmd->speed, cmd->duplex);
}

static u32 enc28j60_get_msglevel(struct net_device *dev)
{
	struct enc28j60_net *priv = netdev_priv(dev);
	return priv->msg_enable;
}

static void enc28j60_set_msglevel(struct net_device *dev, u32 val)
{
	struct enc28j60_net *priv = netdev_priv(dev);
	priv->msg_enable = val;
}

static const struct ethtool_ops enc28j60_ethtool_ops = {
	.get_settings	= enc28j60_get_settings,
	.set_settings	= enc28j60_set_settings,
	.get_drvinfo	= enc28j60_get_drvinfo,
	.get_msglevel	= enc28j60_get_msglevel,
	.set_msglevel	= enc28j60_set_msglevel,
};

static int enc28j60_chipset_init(struct net_device *dev)
{
	struct enc28j60_net *priv = netdev_priv(dev);

	return enc28j60_hw_init(priv);
}

static const struct net_device_ops enc28j60_netdev_ops = {
	.ndo_open		= enc28j60_net_open,
	.ndo_stop		= enc28j60_net_close,
	.ndo_start_xmit		= enc28j60_send_packet,
	.ndo_set_multicast_list = enc28j60_set_multicast_list,
	.ndo_set_mac_address	= enc28j60_set_mac_address,
	.ndo_tx_timeout		= enc28j60_tx_timeout,
	.ndo_change_mtu		= eth_change_mtu,
	.ndo_validate_addr	= eth_validate_addr,
};

static int __devinit enc28j60_probe(struct spi_device *spi)
{
	struct net_device *dev;
	struct enc28j60_net *priv;
	int ret = 0;

	if (netif_msg_drv(&debug))
		dev_info(&spi->dev, DRV_NAME " Ethernet driver %s loaded\n",
			DRV_VERSION);

	dev = alloc_etherdev(sizeof(struct enc28j60_net));
	if (!dev) {
		if (netif_msg_drv(&debug))
			dev_err(&spi->dev, DRV_NAME
				": unable to alloc new ethernet\n");
		ret = -ENOMEM;
		goto error_alloc;
	}
	priv = netdev_priv(dev);

	priv->netdev = dev;	/* priv to netdev reference */
	priv->spi = spi;	/* priv to spi reference */
	priv->msg_enable = netif_msg_init(debug.msg_enable,
						ENC28J60_MSG_DEFAULT);
	mutex_init(&priv->lock);
	INIT_WORK(&priv->tx_work, enc28j60_tx_work_handler);
	INIT_WORK(&priv->setrx_work, enc28j60_setrx_work_handler);
	INIT_WORK(&priv->irq_work, enc28j60_irq_work_handler);
	INIT_WORK(&priv->restart_work, enc28j60_restart_work_handler);
	dev_set_drvdata(&spi->dev, priv);	/* spi to priv reference */
	SET_NETDEV_DEV(dev, &spi->dev);

	if (!enc28j60_chipset_init(dev)) {
		if (netif_msg_probe(priv))
			dev_info(&spi->dev, DRV_NAME " chip not found\n");
		ret = -EIO;
		goto error_irq;
	}
	random_ether_addr(dev->dev_addr);
	enc28j60_set_hw_macaddr(dev);

	/* Board setup must set the relevant edge trigger type;
	 * level triggers won't currently work.
	 */
	ret = request_irq(spi->irq, enc28j60_irq, 0, DRV_NAME, priv);
	if (ret < 0) {
		if (netif_msg_probe(priv))
			dev_err(&spi->dev, DRV_NAME ": request irq %d failed "
				"(ret = %d)\n", spi->irq, ret);
		goto error_irq;
	}

	dev->if_port = IF_PORT_10BASET;
	dev->irq = spi->irq;
	dev->netdev_ops = &enc28j60_netdev_ops;
	dev->watchdog_timeo = TX_TIMEOUT;
	SET_ETHTOOL_OPS(dev, &enc28j60_ethtool_ops);

	enc28j60_lowpower(priv, true);

	ret = register_netdev(dev);
	if (ret) {
		if (netif_msg_probe(priv))
			dev_err(&spi->dev, "register netdev " DRV_NAME
				" failed (ret = %d)\n", ret);
		goto error_register;
	}
	dev_info(&dev->dev, DRV_NAME " driver registered\n");

	return 0;

error_register:
	free_irq(spi->irq, priv);
error_irq:
	free_netdev(dev);
error_alloc:
	return ret;
}

static int __devexit enc28j60_remove(struct spi_device *spi)
{
	struct enc28j60_net *priv = dev_get_drvdata(&spi->dev);

	if (netif_msg_drv(priv))
		printk(KERN_DEBUG DRV_NAME ": remove\n");

	unregister_netdev(priv->netdev);
	free_irq(spi->irq, priv);
	free_netdev(priv->netdev);

	return 0;
}

static struct spi_driver enc28j60_driver = {
	.driver = {
		   .name = DRV_NAME,
		   .owner = THIS_MODULE,
	 },
	.probe = enc28j60_probe,
	.remove = __devexit_p(enc28j60_remove),
};

static int __init enc28j60_init(void)
{
	msec20_to_jiffies = msecs_to_jiffies(20);

	return spi_register_driver(&enc28j60_driver);
}

module_init(enc28j60_init);

static void __exit enc28j60_exit(void)
{
	spi_unregister_driver(&enc28j60_driver);
}

module_exit(enc28j60_exit);

MODULE_DESCRIPTION(DRV_NAME " ethernet driver");
MODULE_AUTHOR("Claudio Lanconelli <lanconelli.claudio@eptar.com>");
MODULE_LICENSE("GPL");
module_param_named(debug, debug.msg_enable, int, 0);
MODULE_PARM_DESC(debug, "Debug verbosity level (0=none, ..., ffff=all)");
MODULE_ALIAS("spi:" DRV_NAME);

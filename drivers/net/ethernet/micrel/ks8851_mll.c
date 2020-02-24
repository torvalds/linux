// SPDX-License-Identifier: GPL-2.0-only
/**
 * drivers/net/ethernet/micrel/ks8851_mll.c
 * Copyright (c) 2009 Micrel Inc.
 */

/* Supports:
 * KS8851 16bit MLL chip from Micrel Inc.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/cache.h>
#include <linux/crc32.h>
#include <linux/crc32poly.h>
#include <linux/mii.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/ks8851_mll.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_net.h>

#include "ks8851.h"

#define	DRV_NAME	"ks8851_mll"

static u8 KS_DEFAULT_MAC_ADDRESS[] = { 0x00, 0x10, 0xA1, 0x86, 0x95, 0x11 };
#define MAX_RECV_FRAMES			255
#define MAX_BUF_SIZE			2048
#define TX_BUF_SIZE			2000
#define RX_BUF_SIZE			2000

#define RXCR1_FILTER_MASK    		(RXCR1_RXINVF | RXCR1_RXAE | \
					 RXCR1_RXMAFMA | RXCR1_RXPAFMA)
#define RXQCR_CMD_CNTL                	(RXQCR_RXFCTE|RXQCR_ADRFE)

#define	ENUM_BUS_NONE			0
#define	ENUM_BUS_8BIT			1
#define	ENUM_BUS_16BIT			2
#define	ENUM_BUS_32BIT			3

#define MAX_MCAST_LST			32
#define HW_MCAST_SIZE			8

/**
 * union ks_tx_hdr - tx header data
 * @txb: The header as bytes
 * @txw: The header as 16bit, little-endian words
 *
 * A dual representation of the tx header data to allow
 * access to individual bytes, and to allow 16bit accesses
 * with 16bit alignment.
 */
union ks_tx_hdr {
	u8      txb[4];
	__le16  txw[2];
};

/**
 * struct ks_net - KS8851 driver private data
 * @net_device 	: The network device we're bound to
 * @hw_addr	: start address of data register.
 * @hw_addr_cmd	: start address of command register.
 * @txh    	: temporaly buffer to save status/length.
 * @lock	: Lock to ensure that the device is not accessed when busy.
 * @pdev	: Pointer to platform device.
 * @mii		: The MII state information for the mii calls.
 * @frame_head_info   	: frame header information for multi-pkt rx.
 * @statelock	: Lock on this structure for tx list.
 * @msg_enable	: The message flags controlling driver output (see ethtool).
 * @frame_cnt  	: number of frames received.
 * @bus_width  	: i/o bus width.
 * @rc_rxqcr	: Cached copy of KS_RXQCR.
 * @rc_txcr	: Cached copy of KS_TXCR.
 * @rc_ier	: Cached copy of KS_IER.
 * @sharedbus  	: Multipex(addr and data bus) mode indicator.
 * @cmd_reg_cache	: command register cached.
 * @cmd_reg_cache_int	: command register cached. Used in the irq handler.
 * @promiscuous	: promiscuous mode indicator.
 * @all_mcast  	: mutlicast indicator.
 * @mcast_lst_size   	: size of multicast list.
 * @mcast_lst    	: multicast list.
 * @mcast_bits    	: multicast enabed.
 * @mac_addr   		: MAC address assigned to this device.
 * @fid    		: frame id.
 * @extra_byte    	: number of extra byte prepended rx pkt.
 * @enabled    		: indicator this device works.
 *
 * The @lock ensures that the chip is protected when certain operations are
 * in progress. When the read or write packet transfer is in progress, most
 * of the chip registers are not accessible until the transfer is finished and
 * the DMA has been de-asserted.
 *
 * The @statelock is used to protect information in the structure which may
 * need to be accessed via several sources, such as the network driver layer
 * or one of the work queues.
 *
 */

/* Receive multiplex framer header info */
struct type_frame_head {
	u16	sts;         /* Frame status */
	u16	len;         /* Byte count */
};

struct ks_net {
	struct net_device	*netdev;
	void __iomem    	*hw_addr;
	void __iomem    	*hw_addr_cmd;
	union ks_tx_hdr		txh ____cacheline_aligned;
	struct mutex      	lock; /* spinlock to be interrupt safe */
	struct platform_device *pdev;
	struct mii_if_info	mii;
	struct type_frame_head	*frame_head_info;
	spinlock_t		statelock;
	u32			msg_enable;
	u32			frame_cnt;
	int			bus_width;

	u16			rc_rxqcr;
	u16			rc_txcr;
	u16			rc_ier;
	u16			sharedbus;
	u16			cmd_reg_cache;
	u16			cmd_reg_cache_int;
	u16			promiscuous;
	u16			all_mcast;
	u16			mcast_lst_size;
	u8			mcast_lst[MAX_MCAST_LST][ETH_ALEN];
	u8			mcast_bits[HW_MCAST_SIZE];
	u8			mac_addr[6];
	u8                      fid;
	u8			extra_byte;
	u8			enabled;
};

static int msg_enable;

#define BE3             0x8000      /* Byte Enable 3 */
#define BE2             0x4000      /* Byte Enable 2 */
#define BE1             0x2000      /* Byte Enable 1 */
#define BE0             0x1000      /* Byte Enable 0 */

/* register read/write calls.
 *
 * All these calls issue transactions to access the chip's registers. They
 * all require that the necessary lock is held to prevent accesses when the
 * chip is busy transferring packet data (RX/TX FIFO accesses).
 */

/**
 * ks_rdreg16 - read 16 bit register from device
 * @ks	  : The chip information
 * @offset: The register address
 *
 * Read a 16bit register from the chip, returning the result
 */

static u16 ks_rdreg16(struct ks_net *ks, int offset)
{
	ks->cmd_reg_cache = (u16)offset | ((BE3 | BE2) >> (offset & 0x02));
	iowrite16(ks->cmd_reg_cache, ks->hw_addr_cmd);
	return ioread16(ks->hw_addr);
}

/**
 * ks_wrreg16 - write 16bit register value to chip
 * @ks: The chip information
 * @offset: The register address
 * @value: The value to write
 *
 */

static void ks_wrreg16(struct ks_net *ks, int offset, u16 value)
{
	ks->cmd_reg_cache = (u16)offset | ((BE3 | BE2) >> (offset & 0x02));
	iowrite16(ks->cmd_reg_cache, ks->hw_addr_cmd);
	iowrite16(value, ks->hw_addr);
}

/**
 * ks_inblk - read a block of data from QMU. This is called after sudo DMA mode enabled.
 * @ks: The chip state
 * @wptr: buffer address to save data
 * @len: length in byte to read
 *
 */
static inline void ks_inblk(struct ks_net *ks, u16 *wptr, u32 len)
{
	len >>= 1;
	while (len--)
		*wptr++ = be16_to_cpu(ioread16(ks->hw_addr));
}

/**
 * ks_outblk - write data to QMU. This is called after sudo DMA mode enabled.
 * @ks: The chip information
 * @wptr: buffer address
 * @len: length in byte to write
 *
 */
static inline void ks_outblk(struct ks_net *ks, u16 *wptr, u32 len)
{
	len >>= 1;
	while (len--)
		iowrite16(cpu_to_be16(*wptr++), ks->hw_addr);
}

static void ks_disable_int(struct ks_net *ks)
{
	ks_wrreg16(ks, KS_IER, 0x0000);
}  /* ks_disable_int */

static void ks_enable_int(struct ks_net *ks)
{
	ks_wrreg16(ks, KS_IER, ks->rc_ier);
}  /* ks_enable_int */

/**
 * ks_tx_fifo_space - return the available hardware buffer size.
 * @ks: The chip information
 *
 */
static inline u16 ks_tx_fifo_space(struct ks_net *ks)
{
	return ks_rdreg16(ks, KS_TXMIR) & 0x1fff;
}

/**
 * ks_save_cmd_reg - save the command register from the cache.
 * @ks: The chip information
 *
 */
static inline void ks_save_cmd_reg(struct ks_net *ks)
{
	/*ks8851 MLL has a bug to read back the command register.
	* So rely on software to save the content of command register.
	*/
	ks->cmd_reg_cache_int = ks->cmd_reg_cache;
}

/**
 * ks_restore_cmd_reg - restore the command register from the cache and
 * 	write to hardware register.
 * @ks: The chip information
 *
 */
static inline void ks_restore_cmd_reg(struct ks_net *ks)
{
	ks->cmd_reg_cache = ks->cmd_reg_cache_int;
	iowrite16(ks->cmd_reg_cache, ks->hw_addr_cmd);
}

/**
 * ks_set_powermode - set power mode of the device
 * @ks: The chip information
 * @pwrmode: The power mode value to write to KS_PMECR.
 *
 * Change the power mode of the chip.
 */
static void ks_set_powermode(struct ks_net *ks, unsigned pwrmode)
{
	unsigned pmecr;

	netif_dbg(ks, hw, ks->netdev, "setting power mode %d\n", pwrmode);

	ks_rdreg16(ks, KS_GRR);
	pmecr = ks_rdreg16(ks, KS_PMECR);
	pmecr &= ~PMECR_PM_MASK;
	pmecr |= pwrmode;

	ks_wrreg16(ks, KS_PMECR, pmecr);
}

/**
 * ks_read_config - read chip configuration of bus width.
 * @ks: The chip information
 *
 */
static void ks_read_config(struct ks_net *ks)
{
	u16 reg_data = 0;

	/* Regardless of bus width, 8 bit read should always work.*/
	reg_data = ks_rdreg16(ks, KS_CCR);

	/* addr/data bus are multiplexed */
	ks->sharedbus = (reg_data & CCR_SHARED) == CCR_SHARED;

	/* There are garbage data when reading data from QMU,
	depending on bus-width.
	*/

	if (reg_data & CCR_8BIT) {
		ks->bus_width = ENUM_BUS_8BIT;
		ks->extra_byte = 1;
	} else if (reg_data & CCR_16BIT) {
		ks->bus_width = ENUM_BUS_16BIT;
		ks->extra_byte = 2;
	} else {
		ks->bus_width = ENUM_BUS_32BIT;
		ks->extra_byte = 4;
	}
}

/**
 * ks_soft_reset - issue one of the soft reset to the device
 * @ks: The device state.
 * @op: The bit(s) to set in the GRR
 *
 * Issue the relevant soft-reset command to the device's GRR register
 * specified by @op.
 *
 * Note, the delays are in there as a caution to ensure that the reset
 * has time to take effect and then complete. Since the datasheet does
 * not currently specify the exact sequence, we have chosen something
 * that seems to work with our device.
 */
static void ks_soft_reset(struct ks_net *ks, unsigned op)
{
	/* Disable interrupt first */
	ks_wrreg16(ks, KS_IER, 0x0000);
	ks_wrreg16(ks, KS_GRR, op);
	mdelay(10);	/* wait a short time to effect reset */
	ks_wrreg16(ks, KS_GRR, 0);
	mdelay(1);	/* wait for condition to clear */
}


static void ks_enable_qmu(struct ks_net *ks)
{
	u16 w;

	w = ks_rdreg16(ks, KS_TXCR);
	/* Enables QMU Transmit (TXCR). */
	ks_wrreg16(ks, KS_TXCR, w | TXCR_TXE);

	/*
	 * RX Frame Count Threshold Enable and Auto-Dequeue RXQ Frame
	 * Enable
	 */

	w = ks_rdreg16(ks, KS_RXQCR);
	ks_wrreg16(ks, KS_RXQCR, w | RXQCR_RXFCTE);

	/* Enables QMU Receive (RXCR1). */
	w = ks_rdreg16(ks, KS_RXCR1);
	ks_wrreg16(ks, KS_RXCR1, w | RXCR1_RXE);
	ks->enabled = true;
}  /* ks_enable_qmu */

static void ks_disable_qmu(struct ks_net *ks)
{
	u16	w;

	w = ks_rdreg16(ks, KS_TXCR);

	/* Disables QMU Transmit (TXCR). */
	w  &= ~TXCR_TXE;
	ks_wrreg16(ks, KS_TXCR, w);

	/* Disables QMU Receive (RXCR1). */
	w = ks_rdreg16(ks, KS_RXCR1);
	w &= ~RXCR1_RXE ;
	ks_wrreg16(ks, KS_RXCR1, w);

	ks->enabled = false;

}  /* ks_disable_qmu */

/**
 * ks_read_qmu - read 1 pkt data from the QMU.
 * @ks: The chip information
 * @buf: buffer address to save 1 pkt
 * @len: Pkt length
 * Here is the sequence to read 1 pkt:
 *	1. set sudo DMA mode
 *	2. read prepend data
 *	3. read pkt data
 *	4. reset sudo DMA Mode
 */
static inline void ks_read_qmu(struct ks_net *ks, u16 *buf, u32 len)
{
	u32 r =  ks->extra_byte & 0x1 ;
	u32 w = ks->extra_byte - r;

	/* 1. set sudo DMA mode */
	ks_wrreg16(ks, KS_RXFDPR, RXFDPR_RXFPAI);
	ks_wrreg16(ks, KS_RXQCR, ks->rc_rxqcr | RXQCR_SDA);

	/* 2. read prepend data */
	/**
	 * read 4 + extra bytes and discard them.
	 * extra bytes for dummy, 2 for status, 2 for len
	 */

	/* use likely(r) for 8 bit access for performance */
	if (unlikely(r))
		ioread8(ks->hw_addr);
	ks_inblk(ks, buf, w + 2 + 2);

	/* 3. read pkt data */
	ks_inblk(ks, buf, ALIGN(len, 4));

	/* 4. reset sudo DMA Mode */
	ks_wrreg16(ks, KS_RXQCR, ks->rc_rxqcr);
}

/**
 * ks_rcv - read multiple pkts data from the QMU.
 * @ks: The chip information
 * @netdev: The network device being opened.
 *
 * Read all of header information before reading pkt content.
 * It is not allowed only port of pkts in QMU after issuing
 * interrupt ack.
 */
static void ks_rcv(struct ks_net *ks, struct net_device *netdev)
{
	u32	i;
	struct type_frame_head *frame_hdr = ks->frame_head_info;
	struct sk_buff *skb;

	ks->frame_cnt = ks_rdreg16(ks, KS_RXFCTR) >> 8;

	/* read all header information */
	for (i = 0; i < ks->frame_cnt; i++) {
		/* Checking Received packet status */
		frame_hdr->sts = ks_rdreg16(ks, KS_RXFHSR);
		/* Get packet len from hardware */
		frame_hdr->len = ks_rdreg16(ks, KS_RXFHBCR);
		frame_hdr++;
	}

	frame_hdr = ks->frame_head_info;
	while (ks->frame_cnt--) {
		if (unlikely(!(frame_hdr->sts & RXFSHR_RXFV) ||
			     frame_hdr->len >= RX_BUF_SIZE ||
			     frame_hdr->len <= 0)) {

			/* discard an invalid packet */
			ks_wrreg16(ks, KS_RXQCR, (ks->rc_rxqcr | RXQCR_RRXEF));
			netdev->stats.rx_dropped++;
			if (!(frame_hdr->sts & RXFSHR_RXFV))
				netdev->stats.rx_frame_errors++;
			else
				netdev->stats.rx_length_errors++;
			frame_hdr++;
			continue;
		}

		skb = netdev_alloc_skb(netdev, frame_hdr->len + 16);
		if (likely(skb)) {
			skb_reserve(skb, 2);
			/* read data block including CRC 4 bytes */
			ks_read_qmu(ks, (u16 *)skb->data, frame_hdr->len);
			skb_put(skb, frame_hdr->len - 4);
			skb->protocol = eth_type_trans(skb, netdev);
			netif_rx(skb);
			/* exclude CRC size */
			netdev->stats.rx_bytes += frame_hdr->len - 4;
			netdev->stats.rx_packets++;
		} else {
			ks_wrreg16(ks, KS_RXQCR, (ks->rc_rxqcr | RXQCR_RRXEF));
			netdev->stats.rx_dropped++;
		}
		frame_hdr++;
	}
}

/**
 * ks_update_link_status - link status update.
 * @netdev: The network device being opened.
 * @ks: The chip information
 *
 */

static void ks_update_link_status(struct net_device *netdev, struct ks_net *ks)
{
	/* check the status of the link */
	u32 link_up_status;
	if (ks_rdreg16(ks, KS_P1SR) & P1SR_LINK_GOOD) {
		netif_carrier_on(netdev);
		link_up_status = true;
	} else {
		netif_carrier_off(netdev);
		link_up_status = false;
	}
	netif_dbg(ks, link, ks->netdev,
		  "%s: %s\n", __func__, link_up_status ? "UP" : "DOWN");
}

/**
 * ks_irq - device interrupt handler
 * @irq: Interrupt number passed from the IRQ handler.
 * @pw: The private word passed to register_irq(), our struct ks_net.
 *
 * This is the handler invoked to find out what happened
 *
 * Read the interrupt status, work out what needs to be done and then clear
 * any of the interrupts that are not needed.
 */

static irqreturn_t ks_irq(int irq, void *pw)
{
	struct net_device *netdev = pw;
	struct ks_net *ks = netdev_priv(netdev);
	u16 status;

	/*this should be the first in IRQ handler */
	ks_save_cmd_reg(ks);

	status = ks_rdreg16(ks, KS_ISR);
	if (unlikely(!status)) {
		ks_restore_cmd_reg(ks);
		return IRQ_NONE;
	}

	ks_wrreg16(ks, KS_ISR, status);

	if (likely(status & IRQ_RXI))
		ks_rcv(ks, netdev);

	if (unlikely(status & IRQ_LCI))
		ks_update_link_status(netdev, ks);

	if (unlikely(status & IRQ_TXI))
		netif_wake_queue(netdev);

	if (unlikely(status & IRQ_LDI)) {

		u16 pmecr = ks_rdreg16(ks, KS_PMECR);
		pmecr &= ~PMECR_WKEVT_MASK;
		ks_wrreg16(ks, KS_PMECR, pmecr | PMECR_WKEVT_LINK);
	}

	if (unlikely(status & IRQ_RXOI))
		ks->netdev->stats.rx_over_errors++;
	/* this should be the last in IRQ handler*/
	ks_restore_cmd_reg(ks);
	return IRQ_HANDLED;
}


/**
 * ks_net_open - open network device
 * @netdev: The network device being opened.
 *
 * Called when the network device is marked active, such as a user executing
 * 'ifconfig up' on the device.
 */
static int ks_net_open(struct net_device *netdev)
{
	struct ks_net *ks = netdev_priv(netdev);
	int err;

#define	KS_INT_FLAGS	IRQF_TRIGGER_LOW
	/* lock the card, even if we may not actually do anything
	 * else at the moment.
	 */

	netif_dbg(ks, ifup, ks->netdev, "%s - entry\n", __func__);

	/* reset the HW */
	err = request_irq(netdev->irq, ks_irq, KS_INT_FLAGS, DRV_NAME, netdev);

	if (err) {
		pr_err("Failed to request IRQ: %d: %d\n", netdev->irq, err);
		return err;
	}

	/* wake up powermode to normal mode */
	ks_set_powermode(ks, PMECR_PM_NORMAL);
	mdelay(1);	/* wait for normal mode to take effect */

	ks_wrreg16(ks, KS_ISR, 0xffff);
	ks_enable_int(ks);
	ks_enable_qmu(ks);
	netif_start_queue(ks->netdev);

	netif_dbg(ks, ifup, ks->netdev, "network device up\n");

	return 0;
}

/**
 * ks_net_stop - close network device
 * @netdev: The device being closed.
 *
 * Called to close down a network device which has been active. Cancell any
 * work, shutdown the RX and TX process and then place the chip into a low
 * power state whilst it is not being used.
 */
static int ks_net_stop(struct net_device *netdev)
{
	struct ks_net *ks = netdev_priv(netdev);

	netif_info(ks, ifdown, netdev, "shutting down\n");

	netif_stop_queue(netdev);

	mutex_lock(&ks->lock);

	/* turn off the IRQs and ack any outstanding */
	ks_wrreg16(ks, KS_IER, 0x0000);
	ks_wrreg16(ks, KS_ISR, 0xffff);

	/* shutdown RX/TX QMU */
	ks_disable_qmu(ks);

	/* set powermode to soft power down to save power */
	ks_set_powermode(ks, PMECR_PM_SOFTDOWN);
	free_irq(netdev->irq, netdev);
	mutex_unlock(&ks->lock);
	return 0;
}


/**
 * ks_write_qmu - write 1 pkt data to the QMU.
 * @ks: The chip information
 * @pdata: buffer address to save 1 pkt
 * @len: Pkt length in byte
 * Here is the sequence to write 1 pkt:
 *	1. set sudo DMA mode
 *	2. write status/length
 *	3. write pkt data
 *	4. reset sudo DMA Mode
 *	5. reset sudo DMA mode
 *	6. Wait until pkt is out
 */
static void ks_write_qmu(struct ks_net *ks, u8 *pdata, u16 len)
{
	/* start header at txb[0] to align txw entries */
	ks->txh.txw[0] = 0;
	ks->txh.txw[1] = cpu_to_le16(len);

	/* 1. set sudo-DMA mode */
	ks_wrreg16(ks, KS_RXQCR, ks->rc_rxqcr | RXQCR_SDA);
	/* 2. write status/lenth info */
	ks_outblk(ks, ks->txh.txw, 4);
	/* 3. write pkt data */
	ks_outblk(ks, (u16 *)pdata, ALIGN(len, 4));
	/* 4. reset sudo-DMA mode */
	ks_wrreg16(ks, KS_RXQCR, ks->rc_rxqcr);
	/* 5. Enqueue Tx(move the pkt from TX buffer into TXQ) */
	ks_wrreg16(ks, KS_TXQCR, TXQCR_METFE);
	/* 6. wait until TXQCR_METFE is auto-cleared */
	while (ks_rdreg16(ks, KS_TXQCR) & TXQCR_METFE)
		;
}

/**
 * ks_start_xmit - transmit packet
 * @skb		: The buffer to transmit
 * @netdev	: The device used to transmit the packet.
 *
 * Called by the network layer to transmit the @skb.
 * spin_lock_irqsave is required because tx and rx should be mutual exclusive.
 * So while tx is in-progress, prevent IRQ interrupt from happenning.
 */
static netdev_tx_t ks_start_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	netdev_tx_t retv = NETDEV_TX_OK;
	struct ks_net *ks = netdev_priv(netdev);

	disable_irq(netdev->irq);
	ks_disable_int(ks);
	spin_lock(&ks->statelock);

	/* Extra space are required:
	*  4 byte for alignment, 4 for status/length, 4 for CRC
	*/

	if (likely(ks_tx_fifo_space(ks) >= skb->len + 12)) {
		ks_write_qmu(ks, skb->data, skb->len);
		/* add tx statistics */
		netdev->stats.tx_bytes += skb->len;
		netdev->stats.tx_packets++;
		dev_kfree_skb(skb);
	} else
		retv = NETDEV_TX_BUSY;
	spin_unlock(&ks->statelock);
	ks_enable_int(ks);
	enable_irq(netdev->irq);
	return retv;
}

/**
 * ks_start_rx - ready to serve pkts
 * @ks		: The chip information
 *
 */
static void ks_start_rx(struct ks_net *ks)
{
	u16 cntl;

	/* Enables QMU Receive (RXCR1). */
	cntl = ks_rdreg16(ks, KS_RXCR1);
	cntl |= RXCR1_RXE ;
	ks_wrreg16(ks, KS_RXCR1, cntl);
}  /* ks_start_rx */

/**
 * ks_stop_rx - stop to serve pkts
 * @ks		: The chip information
 *
 */
static void ks_stop_rx(struct ks_net *ks)
{
	u16 cntl;

	/* Disables QMU Receive (RXCR1). */
	cntl = ks_rdreg16(ks, KS_RXCR1);
	cntl &= ~RXCR1_RXE ;
	ks_wrreg16(ks, KS_RXCR1, cntl);

}  /* ks_stop_rx */

static unsigned long const ethernet_polynomial = CRC32_POLY_BE;

static unsigned long ether_gen_crc(int length, u8 *data)
{
	long crc = -1;
	while (--length >= 0) {
		u8 current_octet = *data++;
		int bit;

		for (bit = 0; bit < 8; bit++, current_octet >>= 1) {
			crc = (crc << 1) ^
				((crc < 0) ^ (current_octet & 1) ?
			ethernet_polynomial : 0);
		}
	}
	return (unsigned long)crc;
}  /* ether_gen_crc */

/**
* ks_set_grpaddr - set multicast information
* @ks : The chip information
*/

static void ks_set_grpaddr(struct ks_net *ks)
{
	u8	i;
	u32	index, position, value;

	memset(ks->mcast_bits, 0, sizeof(u8) * HW_MCAST_SIZE);

	for (i = 0; i < ks->mcast_lst_size; i++) {
		position = (ether_gen_crc(6, ks->mcast_lst[i]) >> 26) & 0x3f;
		index = position >> 3;
		value = 1 << (position & 7);
		ks->mcast_bits[index] |= (u8)value;
	}

	for (i  = 0; i < HW_MCAST_SIZE; i++) {
		if (i & 1) {
			ks_wrreg16(ks, (u16)((KS_MAHTR0 + i) & ~1),
				(ks->mcast_bits[i] << 8) |
				ks->mcast_bits[i - 1]);
		}
	}
}  /* ks_set_grpaddr */

/**
* ks_clear_mcast - clear multicast information
*
* @ks : The chip information
* This routine removes all mcast addresses set in the hardware.
*/

static void ks_clear_mcast(struct ks_net *ks)
{
	u16	i, mcast_size;
	for (i = 0; i < HW_MCAST_SIZE; i++)
		ks->mcast_bits[i] = 0;

	mcast_size = HW_MCAST_SIZE >> 2;
	for (i = 0; i < mcast_size; i++)
		ks_wrreg16(ks, KS_MAHTR0 + (2*i), 0);
}

static void ks_set_promis(struct ks_net *ks, u16 promiscuous_mode)
{
	u16		cntl;
	ks->promiscuous = promiscuous_mode;
	ks_stop_rx(ks);  /* Stop receiving for reconfiguration */
	cntl = ks_rdreg16(ks, KS_RXCR1);

	cntl &= ~RXCR1_FILTER_MASK;
	if (promiscuous_mode)
		/* Enable Promiscuous mode */
		cntl |= RXCR1_RXAE | RXCR1_RXINVF;
	else
		/* Disable Promiscuous mode (default normal mode) */
		cntl |= RXCR1_RXPAFMA;

	ks_wrreg16(ks, KS_RXCR1, cntl);

	if (ks->enabled)
		ks_start_rx(ks);

}  /* ks_set_promis */

static void ks_set_mcast(struct ks_net *ks, u16 mcast)
{
	u16	cntl;

	ks->all_mcast = mcast;
	ks_stop_rx(ks);  /* Stop receiving for reconfiguration */
	cntl = ks_rdreg16(ks, KS_RXCR1);
	cntl &= ~RXCR1_FILTER_MASK;
	if (mcast)
		/* Enable "Perfect with Multicast address passed mode" */
		cntl |= (RXCR1_RXAE | RXCR1_RXMAFMA | RXCR1_RXPAFMA);
	else
		/**
		 * Disable "Perfect with Multicast address passed
		 * mode" (normal mode).
		 */
		cntl |= RXCR1_RXPAFMA;

	ks_wrreg16(ks, KS_RXCR1, cntl);

	if (ks->enabled)
		ks_start_rx(ks);
}  /* ks_set_mcast */

static void ks_set_rx_mode(struct net_device *netdev)
{
	struct ks_net *ks = netdev_priv(netdev);
	struct netdev_hw_addr *ha;

	/* Turn on/off promiscuous mode. */
	if ((netdev->flags & IFF_PROMISC) == IFF_PROMISC)
		ks_set_promis(ks,
			(u16)((netdev->flags & IFF_PROMISC) == IFF_PROMISC));
	/* Turn on/off all mcast mode. */
	else if ((netdev->flags & IFF_ALLMULTI) == IFF_ALLMULTI)
		ks_set_mcast(ks,
			(u16)((netdev->flags & IFF_ALLMULTI) == IFF_ALLMULTI));
	else
		ks_set_promis(ks, false);

	if ((netdev->flags & IFF_MULTICAST) && netdev_mc_count(netdev)) {
		if (netdev_mc_count(netdev) <= MAX_MCAST_LST) {
			int i = 0;

			netdev_for_each_mc_addr(ha, netdev) {
				if (i >= MAX_MCAST_LST)
					break;
				memcpy(ks->mcast_lst[i++], ha->addr, ETH_ALEN);
			}
			ks->mcast_lst_size = (u8)i;
			ks_set_grpaddr(ks);
		} else {
			/**
			 * List too big to support so
			 * turn on all mcast mode.
			 */
			ks->mcast_lst_size = MAX_MCAST_LST;
			ks_set_mcast(ks, true);
		}
	} else {
		ks->mcast_lst_size = 0;
		ks_clear_mcast(ks);
	}
} /* ks_set_rx_mode */

static void ks_set_mac(struct ks_net *ks, u8 *data)
{
	u16 *pw = (u16 *)data;
	u16 w, u;

	ks_stop_rx(ks);  /* Stop receiving for reconfiguration */

	u = *pw++;
	w = ((u & 0xFF) << 8) | ((u >> 8) & 0xFF);
	ks_wrreg16(ks, KS_MARH, w);

	u = *pw++;
	w = ((u & 0xFF) << 8) | ((u >> 8) & 0xFF);
	ks_wrreg16(ks, KS_MARM, w);

	u = *pw;
	w = ((u & 0xFF) << 8) | ((u >> 8) & 0xFF);
	ks_wrreg16(ks, KS_MARL, w);

	memcpy(ks->mac_addr, data, ETH_ALEN);

	if (ks->enabled)
		ks_start_rx(ks);
}

static int ks_set_mac_address(struct net_device *netdev, void *paddr)
{
	struct ks_net *ks = netdev_priv(netdev);
	struct sockaddr *addr = paddr;
	u8 *da;

	memcpy(netdev->dev_addr, addr->sa_data, netdev->addr_len);

	da = (u8 *)netdev->dev_addr;

	ks_set_mac(ks, da);
	return 0;
}

static int ks_net_ioctl(struct net_device *netdev, struct ifreq *req, int cmd)
{
	struct ks_net *ks = netdev_priv(netdev);

	if (!netif_running(netdev))
		return -EINVAL;

	return generic_mii_ioctl(&ks->mii, if_mii(req), cmd, NULL);
}

static const struct net_device_ops ks_netdev_ops = {
	.ndo_open		= ks_net_open,
	.ndo_stop		= ks_net_stop,
	.ndo_do_ioctl		= ks_net_ioctl,
	.ndo_start_xmit		= ks_start_xmit,
	.ndo_set_mac_address	= ks_set_mac_address,
	.ndo_set_rx_mode	= ks_set_rx_mode,
	.ndo_validate_addr	= eth_validate_addr,
};

/* ethtool support */

static void ks_get_drvinfo(struct net_device *netdev,
			       struct ethtool_drvinfo *di)
{
	strlcpy(di->driver, DRV_NAME, sizeof(di->driver));
	strlcpy(di->version, "1.00", sizeof(di->version));
	strlcpy(di->bus_info, dev_name(netdev->dev.parent),
		sizeof(di->bus_info));
}

static u32 ks_get_msglevel(struct net_device *netdev)
{
	struct ks_net *ks = netdev_priv(netdev);
	return ks->msg_enable;
}

static void ks_set_msglevel(struct net_device *netdev, u32 to)
{
	struct ks_net *ks = netdev_priv(netdev);
	ks->msg_enable = to;
}

static int ks_get_link_ksettings(struct net_device *netdev,
				 struct ethtool_link_ksettings *cmd)
{
	struct ks_net *ks = netdev_priv(netdev);

	mii_ethtool_get_link_ksettings(&ks->mii, cmd);

	return 0;
}

static int ks_set_link_ksettings(struct net_device *netdev,
				 const struct ethtool_link_ksettings *cmd)
{
	struct ks_net *ks = netdev_priv(netdev);
	return mii_ethtool_set_link_ksettings(&ks->mii, cmd);
}

static u32 ks_get_link(struct net_device *netdev)
{
	struct ks_net *ks = netdev_priv(netdev);
	return mii_link_ok(&ks->mii);
}

static int ks_nway_reset(struct net_device *netdev)
{
	struct ks_net *ks = netdev_priv(netdev);
	return mii_nway_restart(&ks->mii);
}

static const struct ethtool_ops ks_ethtool_ops = {
	.get_drvinfo	= ks_get_drvinfo,
	.get_msglevel	= ks_get_msglevel,
	.set_msglevel	= ks_set_msglevel,
	.get_link	= ks_get_link,
	.nway_reset	= ks_nway_reset,
	.get_link_ksettings = ks_get_link_ksettings,
	.set_link_ksettings = ks_set_link_ksettings,
};

/* MII interface controls */

/**
 * ks_phy_reg - convert MII register into a KS8851 register
 * @reg: MII register number.
 *
 * Return the KS8851 register number for the corresponding MII PHY register
 * if possible. Return zero if the MII register has no direct mapping to the
 * KS8851 register set.
 */
static int ks_phy_reg(int reg)
{
	switch (reg) {
	case MII_BMCR:
		return KS_P1MBCR;
	case MII_BMSR:
		return KS_P1MBSR;
	case MII_PHYSID1:
		return KS_PHY1ILR;
	case MII_PHYSID2:
		return KS_PHY1IHR;
	case MII_ADVERTISE:
		return KS_P1ANAR;
	case MII_LPA:
		return KS_P1ANLPR;
	}

	return 0x0;
}

/**
 * ks_phy_read - MII interface PHY register read.
 * @netdev: The network device the PHY is on.
 * @phy_addr: Address of PHY (ignored as we only have one)
 * @reg: The register to read.
 *
 * This call reads data from the PHY register specified in @reg. Since the
 * device does not support all the MII registers, the non-existent values
 * are always returned as zero.
 *
 * We return zero for unsupported registers as the MII code does not check
 * the value returned for any error status, and simply returns it to the
 * caller. The mii-tool that the driver was tested with takes any -ve error
 * as real PHY capabilities, thus displaying incorrect data to the user.
 */
static int ks_phy_read(struct net_device *netdev, int phy_addr, int reg)
{
	struct ks_net *ks = netdev_priv(netdev);
	int ksreg;
	int result;

	ksreg = ks_phy_reg(reg);
	if (!ksreg)
		return 0x0;	/* no error return allowed, so use zero */

	mutex_lock(&ks->lock);
	result = ks_rdreg16(ks, ksreg);
	mutex_unlock(&ks->lock);

	return result;
}

static void ks_phy_write(struct net_device *netdev,
			     int phy, int reg, int value)
{
	struct ks_net *ks = netdev_priv(netdev);
	int ksreg;

	ksreg = ks_phy_reg(reg);
	if (ksreg) {
		mutex_lock(&ks->lock);
		ks_wrreg16(ks, ksreg, value);
		mutex_unlock(&ks->lock);
	}
}

/**
 * ks_read_selftest - read the selftest memory info.
 * @ks: The device state
 *
 * Read and check the TX/RX memory selftest information.
 */
static int ks_read_selftest(struct ks_net *ks)
{
	unsigned both_done = MBIR_TXMBF | MBIR_RXMBF;
	int ret = 0;
	unsigned rd;

	rd = ks_rdreg16(ks, KS_MBIR);

	if ((rd & both_done) != both_done) {
		netdev_warn(ks->netdev, "Memory selftest not finished\n");
		return 0;
	}

	if (rd & MBIR_TXMBFA) {
		netdev_err(ks->netdev, "TX memory selftest fails\n");
		ret |= 1;
	}

	if (rd & MBIR_RXMBFA) {
		netdev_err(ks->netdev, "RX memory selftest fails\n");
		ret |= 2;
	}

	netdev_info(ks->netdev, "the selftest passes\n");
	return ret;
}

static void ks_setup(struct ks_net *ks)
{
	u16	w;

	/**
	 * Configure QMU Transmit
	 */

	/* Setup Transmit Frame Data Pointer Auto-Increment (TXFDPR) */
	ks_wrreg16(ks, KS_TXFDPR, TXFDPR_TXFPAI);

	/* Setup Receive Frame Data Pointer Auto-Increment */
	ks_wrreg16(ks, KS_RXFDPR, RXFDPR_RXFPAI);

	/* Setup Receive Frame Threshold - 1 frame (RXFCTFC) */
	ks_wrreg16(ks, KS_RXFCTR, 1 & RXFCTR_RXFCT_MASK);

	/* Setup RxQ Command Control (RXQCR) */
	ks->rc_rxqcr = RXQCR_CMD_CNTL;
	ks_wrreg16(ks, KS_RXQCR, ks->rc_rxqcr);

	/**
	 * set the force mode to half duplex, default is full duplex
	 *  because if the auto-negotiation fails, most switch uses
	 *  half-duplex.
	 */

	w = ks_rdreg16(ks, KS_P1MBCR);
	w &= ~BMCR_FULLDPLX;
	ks_wrreg16(ks, KS_P1MBCR, w);

	w = TXCR_TXFCE | TXCR_TXPE | TXCR_TXCRC | TXCR_TCGIP;
	ks_wrreg16(ks, KS_TXCR, w);

	w = RXCR1_RXFCE | RXCR1_RXBE | RXCR1_RXUE | RXCR1_RXME | RXCR1_RXIPFCC;

	if (ks->promiscuous)         /* bPromiscuous */
		w |= (RXCR1_RXAE | RXCR1_RXINVF);
	else if (ks->all_mcast) /* Multicast address passed mode */
		w |= (RXCR1_RXAE | RXCR1_RXMAFMA | RXCR1_RXPAFMA);
	else                                   /* Normal mode */
		w |= RXCR1_RXPAFMA;

	ks_wrreg16(ks, KS_RXCR1, w);
}  /*ks_setup */


static void ks_setup_int(struct ks_net *ks)
{
	ks->rc_ier = 0x00;
	/* Clear the interrupts status of the hardware. */
	ks_wrreg16(ks, KS_ISR, 0xffff);

	/* Enables the interrupts of the hardware. */
	ks->rc_ier = (IRQ_LCI | IRQ_TXI | IRQ_RXI);
}  /* ks_setup_int */

static int ks_hw_init(struct ks_net *ks)
{
#define	MHEADER_SIZE	(sizeof(struct type_frame_head) * MAX_RECV_FRAMES)
	ks->promiscuous = 0;
	ks->all_mcast = 0;
	ks->mcast_lst_size = 0;

	ks->frame_head_info = devm_kmalloc(&ks->pdev->dev, MHEADER_SIZE,
					   GFP_KERNEL);
	if (!ks->frame_head_info)
		return false;

	ks_set_mac(ks, KS_DEFAULT_MAC_ADDRESS);
	return true;
}

#if defined(CONFIG_OF)
static const struct of_device_id ks8851_ml_dt_ids[] = {
	{ .compatible = "micrel,ks8851-mll" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ks8851_ml_dt_ids);
#endif

static int ks8851_probe(struct platform_device *pdev)
{
	int err;
	struct net_device *netdev;
	struct ks_net *ks;
	u16 id, data;
	const char *mac;

	netdev = alloc_etherdev(sizeof(struct ks_net));
	if (!netdev)
		return -ENOMEM;

	SET_NETDEV_DEV(netdev, &pdev->dev);

	ks = netdev_priv(netdev);
	ks->netdev = netdev;

	ks->hw_addr = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(ks->hw_addr)) {
		err = PTR_ERR(ks->hw_addr);
		goto err_free;
	}

	ks->hw_addr_cmd = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(ks->hw_addr_cmd)) {
		err = PTR_ERR(ks->hw_addr_cmd);
		goto err_free;
	}

	netdev->irq = platform_get_irq(pdev, 0);

	if ((int)netdev->irq < 0) {
		err = netdev->irq;
		goto err_free;
	}

	ks->pdev = pdev;

	mutex_init(&ks->lock);
	spin_lock_init(&ks->statelock);

	netdev->netdev_ops = &ks_netdev_ops;
	netdev->ethtool_ops = &ks_ethtool_ops;

	/* setup mii state */
	ks->mii.dev             = netdev;
	ks->mii.phy_id          = 1,
	ks->mii.phy_id_mask     = 1;
	ks->mii.reg_num_mask    = 0xf;
	ks->mii.mdio_read       = ks_phy_read;
	ks->mii.mdio_write      = ks_phy_write;

	netdev_info(netdev, "message enable is %d\n", msg_enable);
	/* set the default message enable */
	ks->msg_enable = netif_msg_init(msg_enable, (NETIF_MSG_DRV |
						     NETIF_MSG_PROBE |
						     NETIF_MSG_LINK));
	ks_read_config(ks);

	/* simple check for a valid chip being connected to the bus */
	if ((ks_rdreg16(ks, KS_CIDER) & ~CIDER_REV_MASK) != CIDER_ID) {
		netdev_err(netdev, "failed to read device ID\n");
		err = -ENODEV;
		goto err_free;
	}

	if (ks_read_selftest(ks)) {
		netdev_err(netdev, "failed to read device ID\n");
		err = -ENODEV;
		goto err_free;
	}

	err = register_netdev(netdev);
	if (err)
		goto err_free;

	platform_set_drvdata(pdev, netdev);

	ks_soft_reset(ks, GRR_GSR);
	ks_hw_init(ks);
	ks_disable_qmu(ks);
	ks_setup(ks);
	ks_setup_int(ks);

	data = ks_rdreg16(ks, KS_OBCR);
	ks_wrreg16(ks, KS_OBCR, data | OBCR_ODS_16mA);

	/* overwriting the default MAC address */
	if (pdev->dev.of_node) {
		mac = of_get_mac_address(pdev->dev.of_node);
		if (!IS_ERR(mac))
			ether_addr_copy(ks->mac_addr, mac);
	} else {
		struct ks8851_mll_platform_data *pdata;

		pdata = dev_get_platdata(&pdev->dev);
		if (!pdata) {
			netdev_err(netdev, "No platform data\n");
			err = -ENODEV;
			goto err_pdata;
		}
		memcpy(ks->mac_addr, pdata->mac_addr, ETH_ALEN);
	}
	if (!is_valid_ether_addr(ks->mac_addr)) {
		/* Use random MAC address if none passed */
		eth_random_addr(ks->mac_addr);
		netdev_info(netdev, "Using random mac address\n");
	}
	netdev_info(netdev, "Mac address is: %pM\n", ks->mac_addr);

	memcpy(netdev->dev_addr, ks->mac_addr, ETH_ALEN);

	ks_set_mac(ks, netdev->dev_addr);

	id = ks_rdreg16(ks, KS_CIDER);

	netdev_info(netdev, "Found chip, family: 0x%x, id: 0x%x, rev: 0x%x\n",
		    (id >> 8) & 0xff, (id >> 4) & 0xf, (id >> 1) & 0x7);
	return 0;

err_pdata:
	unregister_netdev(netdev);
err_free:
	free_netdev(netdev);
	return err;
}

static int ks8851_remove(struct platform_device *pdev)
{
	struct net_device *netdev = platform_get_drvdata(pdev);

	unregister_netdev(netdev);
	free_netdev(netdev);
	return 0;

}

static struct platform_driver ks8851_platform_driver = {
	.driver = {
		.name = DRV_NAME,
		.of_match_table	= of_match_ptr(ks8851_ml_dt_ids),
	},
	.probe = ks8851_probe,
	.remove = ks8851_remove,
};

module_platform_driver(ks8851_platform_driver);

MODULE_DESCRIPTION("KS8851 MLL Network driver");
MODULE_AUTHOR("David Choi <david.choi@micrel.com>");
MODULE_LICENSE("GPL");
module_param_named(message, msg_enable, int, 0);
MODULE_PARM_DESC(message, "Message verbosity level (0=none, 31=all)");


/*****************************************************************************
*
* Filename:      mcs7780.c
* Version:       0.4-alpha
* Description:   Irda MosChip USB Dongle Driver
* Authors:       Lukasz Stelmach <stlman@poczta.fm>
* 		 Brian Pugh <bpugh@cs.pdx.edu>
*		 Judy Fischbach <jfisch@cs.pdx.edu>
*
*       Based on stir4200 driver, but some things done differently.
*       Based on earlier driver by Paul Stewart <stewart@parc.com>
*
*       Copyright (C) 2000, Roman Weissgaerber <weissg@vienna.at>
*       Copyright (C) 2001, Dag Brattli <dag@brattli.net>
*       Copyright (C) 2001, Jean Tourrilhes <jt@hpl.hp.com>
*       Copyright (C) 2004, Stephen Hemminger <shemminger@osdl.org>
*       Copyright (C) 2005, Lukasz Stelmach <stlman@poczta.fm>
*       Copyright (C) 2005, Brian Pugh <bpugh@cs.pdx.edu>
*       Copyright (C) 2005, Judy Fischbach <jfisch@cs.pdx.edu>
*
*       This program is free software; you can redistribute it and/or modify
*       it under the terms of the GNU General Public License as published by
*       the Free Software Foundation; either version 2 of the License, or
*       (at your option) any later version.
*
*       This program is distributed in the hope that it will be useful,
*       but WITHOUT ANY WARRANTY; without even the implied warranty of
*       MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*       GNU General Public License for more details.
*
*       You should have received a copy of the GNU General Public License
*       along with this program; if not, write to the Free Software
*       Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*
*****************************************************************************/

/*
 * MCS7780 is a simple USB to IrDA bridge by MosChip. It is neither
 * compatibile with irda-usb nor with stir4200. Although it is quite
 * similar to the later as far as general idea of operation is concerned.
 * That is it requires the software to do all the framing job at SIR speeds.
 * The hardware does take care of the framing at MIR and FIR speeds.
 * It supports all speeds from 2400 through 4Mbps
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/device.h>
#include <linux/crc32.h>

#include <asm/unaligned.h>
#include <asm/byteorder.h>
#include <asm/uaccess.h>

#include <net/irda/irda.h>
#include <net/irda/wrapper.h>
#include <net/irda/crc.h>

#include "mcs7780.h"

#define MCS_VENDOR_ID 0x9710
#define MCS_PRODUCT_ID 0x7780

static struct usb_device_id mcs_table[] = {
	/* MosChip Corp.,  MCS7780 FIR-USB Adapter */
	{USB_DEVICE(MCS_VENDOR_ID, MCS_PRODUCT_ID)},
	{},
};

MODULE_AUTHOR("Brian Pugh <bpugh@cs.pdx.edu>");
MODULE_DESCRIPTION("IrDA-USB Dongle Driver for MosChip MCS7780");
MODULE_VERSION("0.3alpha");
MODULE_LICENSE("GPL");

MODULE_DEVICE_TABLE(usb, mcs_table);

static int qos_mtt_bits = 0x07 /* > 1ms */ ;
module_param(qos_mtt_bits, int, 0);
MODULE_PARM_DESC(qos_mtt_bits, "Minimum Turn Time");

static int receive_mode = 0x1;
module_param(receive_mode, int, 0);
MODULE_PARM_DESC(receive_mode,
		 "Receive mode of the device (1:fast, 0:slow, default:1)");

static int sir_tweak = 1;
module_param(sir_tweak, int, 0444);
MODULE_PARM_DESC(sir_tweak,
		 "Default pulse width (1:1.6us, 0:3/16 bit, default:1).");

static int transceiver_type = MCS_TSC_VISHAY;
module_param(transceiver_type, int, 0444);
MODULE_PARM_DESC(transceiver_type, "IR transceiver type, see mcs7780.h.");

static struct usb_driver mcs_driver = {
	.name = "mcs7780",
	.probe = mcs_probe,
	.disconnect = mcs_disconnect,
	.id_table = mcs_table,
};

/* speed flag selection by direct addressing.
addr = (speed >> 8) & 0x0f

0x1   57600	 0x2  115200	 0x4 1152000	 0x5    9600
0x6   38400	 0x9    2400	 0xa  576000	 0xb   19200

4Mbps (or 2400) must be checked separately. Since it also has
to be programmed in a different manner that is not a big problem.
*/
static __u16 mcs_speed_set[16] = { 0,
	MCS_SPEED_57600,
	MCS_SPEED_115200,
	0,
	MCS_SPEED_1152000,
	MCS_SPEED_9600,
	MCS_SPEED_38400,
	0, 0,
	MCS_SPEED_2400,
	MCS_SPEED_576000,
	MCS_SPEED_19200,
	0, 0, 0,
};

/* Set given 16 bit register with a 16 bit value. Send control message
 * to set dongle register. */
static int mcs_set_reg(struct mcs_cb *mcs, __u16 reg, __u16 val)
{
	struct usb_device *dev = mcs->usbdev;
	return usb_control_msg(dev, usb_sndctrlpipe(dev, 0), MCS_WRREQ,
			       MCS_WR_RTYPE, val, reg, NULL, 0,
			       msecs_to_jiffies(MCS_CTRL_TIMEOUT));
}

/* Get 16 bit register value. Send contol message to read dongle register. */
static int mcs_get_reg(struct mcs_cb *mcs, __u16 reg, __u16 * val)
{
	struct usb_device *dev = mcs->usbdev;
	int ret = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0), MCS_RDREQ,
				  MCS_RD_RTYPE, 0, reg, val, 2,
				  msecs_to_jiffies(MCS_CTRL_TIMEOUT));

	return ret;
}

/* Setup a communication between mcs7780 and TFDU chips.  It is described
 * in more detail in the data sheet.  The setup sequence puts the the
 * vishay tranceiver into high speed mode.  It will also receive SIR speed
 * packets but at reduced sensitivity.
 */

/* 0: OK 1:ERROR */
static inline int mcs_setup_transceiver_vishay(struct mcs_cb *mcs)
{
	int ret = 0;
	__u16 rval;

	/* mcs_get_reg should read exactly two bytes from the dongle */
	ret = mcs_get_reg(mcs, MCS_XCVR_REG, &rval);
	if (unlikely(ret != 2)) {
		ret = -EIO;
		goto error;
	}

	/* The MCS_XCVR_CONF bit puts the transceiver into configuration
	 * mode.  The MCS_MODE0 bit must start out high (1) and then
	 * transition to low and the MCS_STFIR and MCS_MODE1 bits must
	 * be low.
	 */
	rval |= (MCS_MODE0 | MCS_XCVR_CONF);
	rval &= ~MCS_STFIR;
	rval &= ~MCS_MODE1;
	ret = mcs_set_reg(mcs, MCS_XCVR_REG, rval);
	if (unlikely(ret))
		goto error;

	rval &= ~MCS_MODE0;
	ret = mcs_set_reg(mcs, MCS_XCVR_REG, rval);
	if (unlikely(ret))
		goto error;

	rval &= ~MCS_XCVR_CONF;
	ret = mcs_set_reg(mcs, MCS_XCVR_REG, rval);
	if (unlikely(ret))
		goto error;

	ret = 0;
	error:
		return ret;
}

/* Setup a communication between mcs7780 and agilent chip. */
static inline int mcs_setup_transceiver_agilent(struct mcs_cb *mcs)
{
	IRDA_WARNING("This transceiver type is not supported yet.\n");
	return 1;
}

/* Setup a communication between mcs7780 and sharp chip. */
static inline int mcs_setup_transceiver_sharp(struct mcs_cb *mcs)
{
	IRDA_WARNING("This transceiver type is not supported yet.\n");
	return 1;
}

/* Common setup for all transceivers */
static inline int mcs_setup_transceiver(struct mcs_cb *mcs)
{
	int ret = 0;
	__u16 rval;
	char *msg;

	msg = "Basic transceiver setup error.";

	/* read value of MODE Register, set the DRIVER and RESET bits
	* and write value back out to MODE Register
	*/
	ret = mcs_get_reg(mcs, MCS_MODE_REG, &rval);
	if(unlikely(ret != 2))
		goto error;
	rval |= MCS_DRIVER;	/* put the mcs7780 into configuration mode. */
	ret = mcs_set_reg(mcs, MCS_MODE_REG, rval);
	if(unlikely(ret))
		goto error;

	rval = 0;		/* set min pulse width to 0 initially. */
	ret = mcs_set_reg(mcs, MCS_MINRXPW_REG, rval);
	if(unlikely(ret))
		goto error;

	ret = mcs_get_reg(mcs, MCS_MODE_REG, &rval);
	if(unlikely(ret != 2))
		goto error;

	rval &= ~MCS_FIR;	/* turn off fir mode. */
	if(mcs->sir_tweak)
		rval |= MCS_SIR16US;	/* 1.6us pulse width */
	else
		rval &= ~MCS_SIR16US;	/* 3/16 bit time pulse width */

	/* make sure ask mode and back to back packets are off. */
	rval &= ~(MCS_BBTG | MCS_ASK);

	rval &= ~MCS_SPEED_MASK;
	rval |= MCS_SPEED_9600;		/* make sure initial speed is 9600. */
	mcs->speed = 9600;
	mcs->new_speed = 0;		/* new_speed is set to 0 */
	rval &= ~MCS_PLLPWDN;		/* disable power down. */

	/* make sure device determines direction and that the auto send sip
	 * pulse are on.
	 */
	rval |= MCS_DTD | MCS_SIPEN;

	ret = mcs_set_reg(mcs, MCS_MODE_REG, rval);
	if(unlikely(ret))
		goto error;

	msg = "transceiver model specific setup error.";
	switch (mcs->transceiver_type) {
	case MCS_TSC_VISHAY:
		ret = mcs_setup_transceiver_vishay(mcs);
		break;

	case MCS_TSC_SHARP:
		ret = mcs_setup_transceiver_sharp(mcs);
		break;

	case MCS_TSC_AGILENT:
		ret = mcs_setup_transceiver_agilent(mcs);
		break;

	default:
		IRDA_WARNING("Unknown transceiver type: %d\n",
			     mcs->transceiver_type);
		ret = 1;
	}
	if (unlikely(ret))
		goto error;

	/* If transceiver is not SHARP, then if receive mode set
	* on the RXFAST bit in the XCVR Register otherwise unset it
	*/
	if (mcs->transceiver_type != MCS_TSC_SHARP) {

		ret = mcs_get_reg(mcs, MCS_XCVR_REG, &rval);
		if (unlikely(ret != 2))
			goto error;
		if (mcs->receive_mode)
			rval |= MCS_RXFAST;
		else
			rval &= ~MCS_RXFAST;
		ret = mcs_set_reg(mcs, MCS_XCVR_REG, rval);
		if (unlikely(ret))
			goto error;
	}

	msg = "transceiver reset.";

	ret = mcs_get_reg(mcs, MCS_MODE_REG, &rval);
	if (unlikely(ret != 2))
		goto error;

	/* reset the mcs7780 so all changes take effect. */
	rval &= ~MCS_RESET;
	ret = mcs_set_reg(mcs, MCS_MODE_REG, rval);
	if (unlikely(ret))
		goto error;
	else
		return ret;

error:
	IRDA_ERROR("%s\n", msg);
	return ret;
}

/* Wraps the data in format for SIR */
static inline int mcs_wrap_sir_skb(struct sk_buff *skb, __u8 * buf)
{
	int wraplen;

	/* 2: full frame length, including "the length" */
	wraplen = async_wrap_skb(skb, buf + 2, 4094);

	wraplen += 2;
	buf[0] = wraplen & 0xff;
	buf[1] = (wraplen >> 8) & 0xff;

	return wraplen;
}

/* Wraps the data in format for FIR */
static unsigned mcs_wrap_fir_skb(const struct sk_buff *skb, __u8 *buf)
{
	unsigned int len = 0;
	__u32 fcs = ~(crc32_le(~0, skb->data, skb->len));

	/* add 2 bytes for length value and 4 bytes for fcs. */
	len = skb->len + 6;

	/* The mcs7780 requires that the first two bytes are the packet
	 * length in little endian order.  Note: the length value includes
	 * the two bytes for the length value itself.
	 */
	buf[0] = len & 0xff;
	buf[1] = (len >> 8) & 0xff;
	/* copy the data into the tx buffer. */
	skb_copy_from_linear_data(skb, buf + 2, skb->len);
	/* put the fcs in the last four bytes in little endian order. */
	buf[len - 4] = fcs & 0xff;
	buf[len - 3] = (fcs >> 8) & 0xff;
	buf[len - 2] = (fcs >> 16) & 0xff;
	buf[len - 1] = (fcs >> 24) & 0xff;

	return len;
}

/* Wraps the data in format for MIR */
static unsigned mcs_wrap_mir_skb(const struct sk_buff *skb, __u8 *buf)
{
	__u16 fcs = 0;
	int len = skb->len + 4;

	fcs = ~(irda_calc_crc16(~fcs, skb->data, skb->len));
	/* put the total packet length in first.  Note: packet length
	 * value includes the two bytes that hold the packet length
	 * itself.
	 */
	buf[0] = len & 0xff;
	buf[1] = (len >> 8) & 0xff;
	/* copy the data */
	skb_copy_from_linear_data(skb, buf + 2, skb->len);
	/* put the fcs in last two bytes in little endian order. */
	buf[len - 2] = fcs & 0xff;
	buf[len - 1] = (fcs >> 8) & 0xff;

	return len;
}

/* Unwrap received packets at MIR speed.  A 16 bit crc_ccitt checksum is
 * used for the fcs.  When performed over the entire packet the result
 * should be GOOD_FCS = 0xf0b8.  Hands the unwrapped data off to the IrDA
 * layer via a sk_buff.
 */
static void mcs_unwrap_mir(struct mcs_cb *mcs, __u8 *buf, int len)
{
	__u16 fcs;
	int new_len;
	struct sk_buff *skb;

	/* Assume that the frames are going to fill a single packet
	 * rather than span multiple packets.
	 */

	new_len = len - 2;
	if(unlikely(new_len <= 0)) {
		IRDA_ERROR("%s short frame length %d\n",
			     mcs->netdev->name, new_len);
		++mcs->netdev->stats.rx_errors;
		++mcs->netdev->stats.rx_length_errors;
		return;
	}
	fcs = 0;
	fcs = irda_calc_crc16(~fcs, buf, len);

	if(fcs != GOOD_FCS) {
		IRDA_ERROR("crc error calc 0x%x len %d\n",
			   fcs, new_len);
		mcs->netdev->stats.rx_errors++;
		mcs->netdev->stats.rx_crc_errors++;
		return;
	}

	skb = dev_alloc_skb(new_len + 1);
	if(unlikely(!skb)) {
		++mcs->netdev->stats.rx_dropped;
		return;
	}

	skb_reserve(skb, 1);
	skb_copy_to_linear_data(skb, buf, new_len);
	skb_put(skb, new_len);
	skb_reset_mac_header(skb);
	skb->protocol = htons(ETH_P_IRDA);
	skb->dev = mcs->netdev;

	netif_rx(skb);

	mcs->netdev->stats.rx_packets++;
	mcs->netdev->stats.rx_bytes += new_len;

	return;
}

/* Unwrap received packets at FIR speed.  A 32 bit crc_ccitt checksum is
 * used for the fcs.  Hands the unwrapped data off to the IrDA
 * layer via a sk_buff.
 */
static void mcs_unwrap_fir(struct mcs_cb *mcs, __u8 *buf, int len)
{
	__u32 fcs;
	int new_len;
	struct sk_buff *skb;

	/* Assume that the frames are going to fill a single packet
	 * rather than span multiple packets.  This is most likely a false
	 * assumption.
	 */

	new_len = len - 4;
	if(unlikely(new_len <= 0)) {
		IRDA_ERROR("%s short frame length %d\n",
			   mcs->netdev->name, new_len);
		++mcs->netdev->stats.rx_errors;
		++mcs->netdev->stats.rx_length_errors;
		return;
	}

	fcs = ~(crc32_le(~0, buf, new_len));
	if(fcs != get_unaligned_le32(buf + new_len)) {
		IRDA_ERROR("crc error calc 0x%x len %d\n", fcs, new_len);
		mcs->netdev->stats.rx_errors++;
		mcs->netdev->stats.rx_crc_errors++;
		return;
	}

	skb = dev_alloc_skb(new_len + 1);
	if(unlikely(!skb)) {
		++mcs->netdev->stats.rx_dropped;
		return;
	}

	skb_reserve(skb, 1);
	skb_copy_to_linear_data(skb, buf, new_len);
	skb_put(skb, new_len);
	skb_reset_mac_header(skb);
	skb->protocol = htons(ETH_P_IRDA);
	skb->dev = mcs->netdev;

	netif_rx(skb);

	mcs->netdev->stats.rx_packets++;
	mcs->netdev->stats.rx_bytes += new_len;

	return;
}


/* Allocates urbs for both receive and transmit.
 * If alloc fails return error code 0 (fail) otherwise
 * return error code 1 (success).
 */
static inline int mcs_setup_urbs(struct mcs_cb *mcs)
{
	mcs->rx_urb = NULL;

	mcs->tx_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!mcs->tx_urb)
		return 0;

	mcs->rx_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!mcs->rx_urb)
		return 0;

	return 1;
}

/* Sets up state to be initially outside frame, gets receive urb,
 * sets status to successful and then submits the urb to start
 * receiving the data.
 */
static inline int mcs_receive_start(struct mcs_cb *mcs)
{
	mcs->rx_buff.in_frame = FALSE;
	mcs->rx_buff.state = OUTSIDE_FRAME;

	usb_fill_bulk_urb(mcs->rx_urb, mcs->usbdev,
			  usb_rcvbulkpipe(mcs->usbdev, mcs->ep_in),
			  mcs->in_buf, 4096, mcs_receive_irq, mcs);

	mcs->rx_urb->status = 0;
	return usb_submit_urb(mcs->rx_urb, GFP_KERNEL);
}

/* Finds the in and out endpoints for the mcs control block */
static inline int mcs_find_endpoints(struct mcs_cb *mcs,
				     struct usb_host_endpoint *ep, int epnum)
{
	int i;
	int ret = 0;

	/* If no place to store the endpoints just return */
	if (!ep)
		return ret;

	/* cycle through all endpoints, find the first two that are DIR_IN */
	for (i = 0; i < epnum; i++) {
		if (ep[i].desc.bEndpointAddress & USB_DIR_IN)
			mcs->ep_in = ep[i].desc.bEndpointAddress;
		else
			mcs->ep_out = ep[i].desc.bEndpointAddress;

		/* MosChip says that the chip has only two bulk
		 * endpoints. Find one for each direction and move on.
		 */
		if ((mcs->ep_in != 0) && (mcs->ep_out != 0)) {
			ret = 1;
			break;
		}
	}

	return ret;
}

static void mcs_speed_work(struct work_struct *work)
{
	struct mcs_cb *mcs = container_of(work, struct mcs_cb, work);
	struct net_device *netdev = mcs->netdev;

	mcs_speed_change(mcs);
	netif_wake_queue(netdev);
}

/* Function to change the speed of the mcs7780.  Fully supports SIR,
 * MIR, and FIR speeds.
 */
static int mcs_speed_change(struct mcs_cb *mcs)
{
	int ret = 0;
	int rst = 0;
	int cnt = 0;
	__u16 nspeed;
	__u16 rval;

	nspeed = mcs_speed_set[(mcs->new_speed >> 8) & 0x0f];

	do {
		mcs_get_reg(mcs, MCS_RESV_REG, &rval);
	} while(cnt++ < 100 && (rval & MCS_IRINTX));

	if (cnt > 100) {
		IRDA_ERROR("unable to change speed\n");
		ret = -EIO;
		goto error;
	}

	mcs_get_reg(mcs, MCS_MODE_REG, &rval);

	/* MINRXPW values recomended by MosChip */
	if (mcs->new_speed <= 115200) {
		rval &= ~MCS_FIR;

		if ((rst = (mcs->speed > 115200)))
			mcs_set_reg(mcs, MCS_MINRXPW_REG, 0);

	} else if (mcs->new_speed <= 1152000) {
		rval &= ~MCS_FIR;

		if ((rst = !(mcs->speed == 576000 || mcs->speed == 1152000)))
			mcs_set_reg(mcs, MCS_MINRXPW_REG, 5);

	} else {
		rval |= MCS_FIR;

		if ((rst = (mcs->speed != 4000000)))
			mcs_set_reg(mcs, MCS_MINRXPW_REG, 5);

	}

	rval &= ~MCS_SPEED_MASK;
	rval |= nspeed;

	ret = mcs_set_reg(mcs, MCS_MODE_REG, rval);
	if (unlikely(ret))
		goto error;

	if (rst)
		switch (mcs->transceiver_type) {
		case MCS_TSC_VISHAY:
			ret = mcs_setup_transceiver_vishay(mcs);
			break;

		case MCS_TSC_SHARP:
			ret = mcs_setup_transceiver_sharp(mcs);
			break;

		case MCS_TSC_AGILENT:
			ret = mcs_setup_transceiver_agilent(mcs);
			break;

		default:
			ret = 1;
			IRDA_WARNING("Unknown transceiver type: %d\n",
				     mcs->transceiver_type);
		}
	if (unlikely(ret))
		goto error;

	mcs_get_reg(mcs, MCS_MODE_REG, &rval);
	rval &= ~MCS_RESET;
	ret = mcs_set_reg(mcs, MCS_MODE_REG, rval);

	mcs->speed = mcs->new_speed;
	error:
		mcs->new_speed = 0;
		return ret;
}

/* Ioctl calls not supported at this time.  Can be an area of future work. */
static int mcs_net_ioctl(struct net_device *netdev, struct ifreq *rq, int cmd)
{
	/* struct if_irda_req *irq = (struct if_irda_req *)rq; */
	/* struct mcs_cb *mcs = netdev_priv(netdev); */
	int ret = 0;

	switch (cmd) {
	default:
		ret = -EOPNOTSUPP;
	}

	return ret;
}

/* Network device is taken down, done by "ifconfig irda0 down" */
static int mcs_net_close(struct net_device *netdev)
{
	int ret = 0;
	struct mcs_cb *mcs = netdev_priv(netdev);

	/* Stop transmit processing */
	netif_stop_queue(netdev);

	kfree_skb(mcs->rx_buff.skb);

	/* kill and free the receive and transmit URBs */
	usb_kill_urb(mcs->rx_urb);
	usb_free_urb(mcs->rx_urb);
	usb_kill_urb(mcs->tx_urb);
	usb_free_urb(mcs->tx_urb);

	/* Stop and remove instance of IrLAP */
	if (mcs->irlap)
		irlap_close(mcs->irlap);

	mcs->irlap = NULL;
	return ret;
}

/* Network device is taken up, done by "ifconfig irda0 up" */
static int mcs_net_open(struct net_device *netdev)
{
	struct mcs_cb *mcs = netdev_priv(netdev);
	char hwname[16];
	int ret = 0;

	ret = usb_clear_halt(mcs->usbdev,
			     usb_sndbulkpipe(mcs->usbdev, mcs->ep_in));
	if (ret)
		goto error1;
	ret = usb_clear_halt(mcs->usbdev,
			     usb_rcvbulkpipe(mcs->usbdev, mcs->ep_out));
	if (ret)
		goto error1;

	ret = mcs_setup_transceiver(mcs);
	if (ret)
		goto error1;

	ret = -ENOMEM;

	/* Initialize for SIR/FIR to copy data directly into skb.  */
	mcs->receiving = 0;
	mcs->rx_buff.truesize = IRDA_SKB_MAX_MTU;
	mcs->rx_buff.skb = dev_alloc_skb(IRDA_SKB_MAX_MTU);
	if (!mcs->rx_buff.skb)
		goto error1;

	skb_reserve(mcs->rx_buff.skb, 1);
	mcs->rx_buff.head = mcs->rx_buff.skb->data;
	do_gettimeofday(&mcs->rx_time);

	/*
	 * Now that everything should be initialized properly,
	 * Open new IrLAP layer instance to take care of us...
	 * Note : will send immediately a speed change...
	 */
	sprintf(hwname, "usb#%d", mcs->usbdev->devnum);
	mcs->irlap = irlap_open(netdev, &mcs->qos, hwname);
	if (!mcs->irlap) {
		IRDA_ERROR("mcs7780: irlap_open failed\n");
		goto error2;
	}

	if (!mcs_setup_urbs(mcs))
	goto error3;

	ret = mcs_receive_start(mcs);
	if (ret)
		goto error3;

	netif_start_queue(netdev);
	return 0;

	error3:
		irlap_close(mcs->irlap);
	error2:
		kfree_skb(mcs->rx_buff.skb);
	error1:
		return ret;
}

/* Receive callback function.  */
static void mcs_receive_irq(struct urb *urb)
{
	__u8 *bytes;
	struct mcs_cb *mcs = urb->context;
	int i;
	int ret;

	if (!netif_running(mcs->netdev))
		return;

	if (urb->status)
		return;

	if (urb->actual_length > 0) {
		bytes = urb->transfer_buffer;

		/* MCS returns frames without BOF and EOF
		 * I assume it returns whole frames.
		 */
		/* SIR speed */
		if(mcs->speed < 576000) {
			async_unwrap_char(mcs->netdev, &mcs->netdev->stats,
				  &mcs->rx_buff, 0xc0);

			for (i = 0; i < urb->actual_length; i++)
				async_unwrap_char(mcs->netdev, &mcs->netdev->stats,
					  &mcs->rx_buff, bytes[i]);

			async_unwrap_char(mcs->netdev, &mcs->netdev->stats,
				  &mcs->rx_buff, 0xc1);
		}
		/* MIR speed */
		else if(mcs->speed == 576000 || mcs->speed == 1152000) {
			mcs_unwrap_mir(mcs, urb->transfer_buffer,
				urb->actual_length);
		}
		/* FIR speed */
		else {
			mcs_unwrap_fir(mcs, urb->transfer_buffer,
				urb->actual_length);
		}
		do_gettimeofday(&mcs->rx_time);
	}

	ret = usb_submit_urb(urb, GFP_ATOMIC);
}

/* Transmit callback funtion.  */
static void mcs_send_irq(struct urb *urb)
{
	struct mcs_cb *mcs = urb->context;
	struct net_device *ndev = mcs->netdev;

	if (unlikely(mcs->new_speed))
		schedule_work(&mcs->work);
	else
		netif_wake_queue(ndev);
}

/* Transmit callback funtion.  */
static netdev_tx_t mcs_hard_xmit(struct sk_buff *skb,
				       struct net_device *ndev)
{
	unsigned long flags;
	struct mcs_cb *mcs;
	int wraplen;
	int ret = 0;

	netif_stop_queue(ndev);
	mcs = netdev_priv(ndev);

	spin_lock_irqsave(&mcs->lock, flags);

	mcs->new_speed = irda_get_next_speed(skb);
	if (likely(mcs->new_speed == mcs->speed))
		mcs->new_speed = 0;

	/* SIR speed */
	if(mcs->speed < 576000) {
		wraplen = mcs_wrap_sir_skb(skb, mcs->out_buf);
	}
	/* MIR speed */
	else if(mcs->speed == 576000 || mcs->speed == 1152000) {
		wraplen = mcs_wrap_mir_skb(skb, mcs->out_buf);
	}
	/* FIR speed */
	else {
		wraplen = mcs_wrap_fir_skb(skb, mcs->out_buf);
	}
	usb_fill_bulk_urb(mcs->tx_urb, mcs->usbdev,
			  usb_sndbulkpipe(mcs->usbdev, mcs->ep_out),
			  mcs->out_buf, wraplen, mcs_send_irq, mcs);

	if ((ret = usb_submit_urb(mcs->tx_urb, GFP_ATOMIC))) {
		IRDA_ERROR("failed tx_urb: %d\n", ret);
		switch (ret) {
		case -ENODEV:
		case -EPIPE:
			break;
		default:
			mcs->netdev->stats.tx_errors++;
			netif_start_queue(ndev);
		}
	} else {
		mcs->netdev->stats.tx_packets++;
		mcs->netdev->stats.tx_bytes += skb->len;
	}

	dev_kfree_skb(skb);
	spin_unlock_irqrestore(&mcs->lock, flags);
	return NETDEV_TX_OK;
}

static const struct net_device_ops mcs_netdev_ops = {
	.ndo_open = mcs_net_open,
	.ndo_stop = mcs_net_close,
	.ndo_start_xmit = mcs_hard_xmit,
	.ndo_do_ioctl = mcs_net_ioctl,
};

/*
 * This function is called by the USB subsystem for each new device in the
 * system.  Need to verify the device and if it is, then start handling it.
 */
static int mcs_probe(struct usb_interface *intf,
		     const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct net_device *ndev = NULL;
	struct mcs_cb *mcs;
	int ret = -ENOMEM;

	ndev = alloc_irdadev(sizeof(*mcs));
	if (!ndev)
		goto error1;

	IRDA_DEBUG(1, "MCS7780 USB-IrDA bridge found at %d.\n", udev->devnum);

	SET_NETDEV_DEV(ndev, &intf->dev);

	ret = usb_reset_configuration(udev);
	if (ret != 0) {
		IRDA_ERROR("mcs7780: usb reset configuration failed\n");
		goto error2;
	}

	mcs = netdev_priv(ndev);
	mcs->usbdev = udev;
	mcs->netdev = ndev;
	spin_lock_init(&mcs->lock);

	/* Initialize QoS for this device */
	irda_init_max_qos_capabilies(&mcs->qos);

	/* That's the Rx capability. */
	mcs->qos.baud_rate.bits &=
	    IR_2400 | IR_9600 | IR_19200 | IR_38400 | IR_57600 | IR_115200
		| IR_576000 | IR_1152000 | (IR_4000000 << 8);


	mcs->qos.min_turn_time.bits &= qos_mtt_bits;
	irda_qos_bits_to_value(&mcs->qos);

	/* Speed change work initialisation*/
	INIT_WORK(&mcs->work, mcs_speed_work);

	ndev->netdev_ops = &mcs_netdev_ops;

	if (!intf->cur_altsetting)
		goto error2;

	ret = mcs_find_endpoints(mcs, intf->cur_altsetting->endpoint,
				 intf->cur_altsetting->desc.bNumEndpoints);
	if (!ret) {
		ret = -ENODEV;
		goto error2;
	}

	ret = register_netdev(ndev);
	if (ret != 0)
		goto error2;

	IRDA_DEBUG(1, "IrDA: Registered MosChip MCS7780 device as %s\n",
		   ndev->name);

	mcs->transceiver_type = transceiver_type;
	mcs->sir_tweak = sir_tweak;
	mcs->receive_mode = receive_mode;

	usb_set_intfdata(intf, mcs);
	return 0;

	error2:
		free_netdev(ndev);

	error1:
		return ret;
}

/* The current device is removed, the USB layer tells us to shut down. */
static void mcs_disconnect(struct usb_interface *intf)
{
	struct mcs_cb *mcs = usb_get_intfdata(intf);

	if (!mcs)
		return;

	flush_scheduled_work();

	unregister_netdev(mcs->netdev);
	free_netdev(mcs->netdev);

	usb_set_intfdata(intf, NULL);
	IRDA_DEBUG(0, "MCS7780 now disconnected.\n");
}

/* Module insertion */
static int __init mcs_init(void)
{
	int result;

	/* register this driver with the USB subsystem */
	result = usb_register(&mcs_driver);
	if (result)
		IRDA_ERROR("usb_register failed. Error number %d\n", result);

	return result;
}
module_init(mcs_init);

/* Module removal */
static void __exit mcs_exit(void)
{
	/* deregister this driver with the USB subsystem */
	usb_deregister(&mcs_driver);
}
module_exit(mcs_exit);


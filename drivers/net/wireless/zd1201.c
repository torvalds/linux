/*
 *	Driver for ZyDAS zd1201 based wireless USB devices.
 *
 *	Copyright (c) 2004, 2005 Jeroen Vreeken (pe1rxq@amsat.org)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	version 2 as published by the Free Software Foundation.
 *
 *	Parts of this driver have been derived from a wlan-ng version
 *	modified by ZyDAS. They also made documentation available, thanks!
 *	Copyright (C) 1999 AbsoluteValue Systems, Inc.  All Rights Reserved.
 */

#include <linux/module.h>
#include <linux/usb.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/wireless.h>
#include <net/iw_handler.h>
#include <linux/string.h>
#include <linux/if_arp.h>
#include <linux/firmware.h>
#include <net/ieee80211.h>
#include "zd1201.h"

static struct usb_device_id zd1201_table[] = {
	{USB_DEVICE(0x0586, 0x3400)}, /* Peabird Wireless USB Adapter */
	{USB_DEVICE(0x0ace, 0x1201)}, /* ZyDAS ZD1201 Wireless USB Adapter */
	{USB_DEVICE(0x050d, 0x6051)}, /* Belkin F5D6051 usb  adapter */
	{USB_DEVICE(0x0db0, 0x6823)}, /* MSI UB11B usb  adapter */
	{USB_DEVICE(0x1044, 0x8005)}, /* GIGABYTE GN-WLBZ201 usb adapter */
	{}
};

static int ap;	/* Are we an AP or a normal station? */

#define ZD1201_VERSION	"0.15"

MODULE_AUTHOR("Jeroen Vreeken <pe1rxq@amsat.org>");
MODULE_DESCRIPTION("Driver for ZyDAS ZD1201 based USB Wireless adapters");
MODULE_VERSION(ZD1201_VERSION);
MODULE_LICENSE("GPL");
module_param(ap, int, 0);
MODULE_PARM_DESC(ap, "If non-zero Access Point firmware will be loaded");
MODULE_DEVICE_TABLE(usb, zd1201_table);


static int zd1201_fw_upload(struct usb_device *dev, int apfw)
{
	const struct firmware *fw_entry;
	char *data;
	unsigned long len;
	int err;
	unsigned char ret;
	char *buf;
	char *fwfile;

	if (apfw)
		fwfile = "zd1201-ap.fw";
	else
		fwfile = "zd1201.fw";

	err = request_firmware(&fw_entry, fwfile, &dev->dev);
	if (err) {
		dev_err(&dev->dev, "Failed to load %s firmware file!\n", fwfile);
		dev_err(&dev->dev, "Make sure the hotplug firmware loader is installed.\n");
		dev_err(&dev->dev, "Goto http://linux-lc100020.sourceforge.net for more info.\n");
		return err;
	}

	data = fw_entry->data;
        len = fw_entry->size;

	buf = kmalloc(1024, GFP_ATOMIC);
	if (!buf)
		goto exit;
	
	while (len > 0) {
		int translen = (len > 1024) ? 1024 : len;
		memcpy(buf, data, translen);

		err = usb_control_msg(dev, usb_sndctrlpipe(dev, 0), 0,
		    USB_DIR_OUT | 0x40, 0, 0, buf, translen,
		    ZD1201_FW_TIMEOUT);
		if (err < 0)
			goto exit;

		len -= translen;
		data += translen;
	}
                                        
	err = usb_control_msg(dev, usb_sndctrlpipe(dev, 0), 0x2,
	    USB_DIR_OUT | 0x40, 0, 0, NULL, 0, ZD1201_FW_TIMEOUT);
	if (err < 0)
		goto exit;

	err = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0), 0x4,
	    USB_DIR_IN | 0x40, 0,0, &ret, sizeof(ret), ZD1201_FW_TIMEOUT);
	if (err < 0)
		goto exit;

	if (ret & 0x80) {
		err = -EIO;
		goto exit;
	}

	err = 0;
exit:
	kfree(buf);
	release_firmware(fw_entry);
	return err;
}

static void zd1201_usbfree(struct urb *urb)
{
	struct zd1201 *zd = urb->context;

	switch(urb->status) {
		case -EILSEQ:
		case -ENODEV:
		case -ETIME:
		case -ENOENT:
		case -EPIPE:
		case -EOVERFLOW:
		case -ESHUTDOWN:
			dev_warn(&zd->usb->dev, "%s: urb failed: %d\n", 
			    zd->dev->name, urb->status);
	}

	kfree(urb->transfer_buffer);
	usb_free_urb(urb);
	return;
}

/* cmdreq message: 
	u32 type
	u16 cmd
	u16 parm0
	u16 parm1
	u16 parm2
	u8  pad[4]

	total: 4 + 2 + 2 + 2 + 2 + 4 = 16
*/
static int zd1201_docmd(struct zd1201 *zd, int cmd, int parm0,
			int parm1, int parm2)
{
	unsigned char *command;
	int ret;
	struct urb *urb;

	command = kmalloc(16, GFP_ATOMIC);
	if (!command)
		return -ENOMEM;

	*((__le32*)command) = cpu_to_le32(ZD1201_USB_CMDREQ);
	*((__le16*)&command[4]) = cpu_to_le16(cmd);
	*((__le16*)&command[6]) = cpu_to_le16(parm0);
	*((__le16*)&command[8]) = cpu_to_le16(parm1);
	*((__le16*)&command[10])= cpu_to_le16(parm2);

	urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!urb) {
		kfree(command);
		return -ENOMEM;
	}
	usb_fill_bulk_urb(urb, zd->usb, usb_sndbulkpipe(zd->usb, zd->endp_out2),
			  command, 16, zd1201_usbfree, zd);
	ret = usb_submit_urb(urb, GFP_ATOMIC);
	if (ret) {
		kfree(command);
		usb_free_urb(urb);
	}

	return ret;
}

/* Callback after sending out a packet */
static void zd1201_usbtx(struct urb *urb)
{
	struct zd1201 *zd = urb->context;
	netif_wake_queue(zd->dev);
	return;
}

/* Incoming data */
static void zd1201_usbrx(struct urb *urb)
{
	struct zd1201 *zd = urb->context;
	int free = 0;
	unsigned char *data = urb->transfer_buffer;
	struct sk_buff *skb;
	unsigned char type;

	if (!zd)
		return;

	switch(urb->status) {
		case -EILSEQ:
		case -ENODEV:
		case -ETIME:
		case -ENOENT:
		case -EPIPE:
		case -EOVERFLOW:
		case -ESHUTDOWN:
			dev_warn(&zd->usb->dev, "%s: rx urb failed: %d\n",
			    zd->dev->name, urb->status);
			free = 1;
			goto exit;
	}
	
	if (urb->status != 0 || urb->actual_length == 0)
		goto resubmit;

	type = data[0];
	if (type == ZD1201_PACKET_EVENTSTAT || type == ZD1201_PACKET_RESOURCE) {
		memcpy(zd->rxdata, data, urb->actual_length);
		zd->rxlen = urb->actual_length;
		zd->rxdatas = 1;
		wake_up(&zd->rxdataq);
	}
	/* Info frame */
	if (type == ZD1201_PACKET_INQUIRE) {
		int i = 0;
		unsigned short infotype, framelen, copylen;
		framelen = le16_to_cpu(*(__le16*)&data[4]);
		infotype = le16_to_cpu(*(__le16*)&data[6]);

		if (infotype == ZD1201_INF_LINKSTATUS) {
			short linkstatus;

			linkstatus = le16_to_cpu(*(__le16*)&data[8]);
			switch(linkstatus) {
				case 1:
					netif_carrier_on(zd->dev);
					break;
				case 2:
					netif_carrier_off(zd->dev);
					break;
				case 3:
					netif_carrier_off(zd->dev);
					break;
				case 4:
					netif_carrier_on(zd->dev);
					break;
				default:
					netif_carrier_off(zd->dev);
			}
			goto resubmit;
		}
		if (infotype == ZD1201_INF_ASSOCSTATUS) {
			short status = le16_to_cpu(*(__le16*)(data+8));
			int event;
			union iwreq_data wrqu;

			switch (status) {
				case ZD1201_ASSOCSTATUS_STAASSOC:
				case ZD1201_ASSOCSTATUS_REASSOC:
					event = IWEVREGISTERED;
					break;
				case ZD1201_ASSOCSTATUS_DISASSOC:
				case ZD1201_ASSOCSTATUS_ASSOCFAIL:
				case ZD1201_ASSOCSTATUS_AUTHFAIL:
				default:
					event = IWEVEXPIRED;
			}
			memcpy(wrqu.addr.sa_data, data+10, ETH_ALEN);
			wrqu.addr.sa_family = ARPHRD_ETHER;

			/* Send event to user space */
			wireless_send_event(zd->dev, event, &wrqu, NULL);

			goto resubmit;
		}
		if (infotype == ZD1201_INF_AUTHREQ) {
			union iwreq_data wrqu;

			memcpy(wrqu.addr.sa_data, data+8, ETH_ALEN);
			wrqu.addr.sa_family = ARPHRD_ETHER;
			/* There isn't a event that trully fits this request.
			   We assume that userspace will be smart enough to
			   see a new station being expired and sends back a
			   authstation ioctl to authorize it. */
			wireless_send_event(zd->dev, IWEVEXPIRED, &wrqu, NULL);
			goto resubmit;
		}
		/* Other infotypes are handled outside this handler */
		zd->rxlen = 0;
		while (i < urb->actual_length) {
			copylen = le16_to_cpu(*(__le16*)&data[i+2]);
			/* Sanity check, sometimes we get junk */
			if (copylen+zd->rxlen > sizeof(zd->rxdata))
				break;
			memcpy(zd->rxdata+zd->rxlen, data+i+4, copylen);
			zd->rxlen += copylen;
			i += 64;
		}
		if (i >= urb->actual_length) {
			zd->rxdatas = 1;
			wake_up(&zd->rxdataq);
		}
		goto  resubmit;
	}
	/* Actual data */
	if (data[urb->actual_length-1] == ZD1201_PACKET_RXDATA) {
		int datalen = urb->actual_length-1;
		unsigned short len, fc, seq;
		struct hlist_node *node;

		len = ntohs(*(__be16 *)&data[datalen-2]);
		if (len>datalen)
			len=datalen;
		fc = le16_to_cpu(*(__le16 *)&data[datalen-16]);
		seq = le16_to_cpu(*(__le16 *)&data[datalen-24]);

		if (zd->monitor) {
			if (datalen < 24)
				goto resubmit;
			if (!(skb = dev_alloc_skb(datalen+24)))
				goto resubmit;
			
			memcpy(skb_put(skb, 2), &data[datalen-16], 2);
			memcpy(skb_put(skb, 2), &data[datalen-2], 2);
			memcpy(skb_put(skb, 6), &data[datalen-14], 6);
			memcpy(skb_put(skb, 6), &data[datalen-22], 6);
			memcpy(skb_put(skb, 6), &data[datalen-8], 6);
			memcpy(skb_put(skb, 2), &data[datalen-24], 2);
			memcpy(skb_put(skb, len), data, len);
			skb->protocol = eth_type_trans(skb, zd->dev);
			skb->dev->last_rx = jiffies;
			zd->stats.rx_packets++;
			zd->stats.rx_bytes += skb->len;
			netif_rx(skb);
			goto resubmit;
		}
			
		if ((seq & IEEE80211_SCTL_FRAG) ||
		    (fc & IEEE80211_FCTL_MOREFRAGS)) {
			struct zd1201_frag *frag = NULL;
			char *ptr;

			if (datalen<14)
				goto resubmit;
			if ((seq & IEEE80211_SCTL_FRAG) == 0) {
				frag = kmalloc(sizeof(*frag), GFP_ATOMIC);
				if (!frag)
					goto resubmit;
				skb = dev_alloc_skb(IEEE80211_DATA_LEN +14+2);
				if (!skb) {
					kfree(frag);
					goto resubmit;
				}
				frag->skb = skb;
				frag->seq = seq & IEEE80211_SCTL_SEQ;
				skb_reserve(skb, 2);
				memcpy(skb_put(skb, 12), &data[datalen-14], 12);
				memcpy(skb_put(skb, 2), &data[6], 2);
				memcpy(skb_put(skb, len), data+8, len);
				hlist_add_head(&frag->fnode, &zd->fraglist);
				goto resubmit;
			}
			hlist_for_each_entry(frag, node, &zd->fraglist, fnode)
				if (frag->seq == (seq&IEEE80211_SCTL_SEQ))
					break;
			if (!frag)
				goto resubmit;
			skb = frag->skb;
			ptr = skb_put(skb, len);
			if (ptr)
				memcpy(ptr, data+8, len);
			if (fc & IEEE80211_FCTL_MOREFRAGS)
				goto resubmit;
			hlist_del_init(&frag->fnode);
			kfree(frag);
		} else {
			if (datalen<14)
				goto resubmit;
			skb = dev_alloc_skb(len + 14 + 2);
			if (!skb)
				goto resubmit;
			skb_reserve(skb, 2);
			memcpy(skb_put(skb, 12), &data[datalen-14], 12);
			memcpy(skb_put(skb, 2), &data[6], 2);
			memcpy(skb_put(skb, len), data+8, len);
		}
		skb->protocol = eth_type_trans(skb, zd->dev);
		skb->dev->last_rx = jiffies;
		zd->stats.rx_packets++;
		zd->stats.rx_bytes += skb->len;
		netif_rx(skb);
	}
resubmit:
	memset(data, 0, ZD1201_RXSIZE);

	urb->status = 0;
	urb->dev = zd->usb;
	if(usb_submit_urb(urb, GFP_ATOMIC))
		free = 1;

exit:
	if (free) {
		zd->rxlen = 0;
		zd->rxdatas = 1;
		wake_up(&zd->rxdataq);
		kfree(urb->transfer_buffer);
	}
	return;
}

static int zd1201_getconfig(struct zd1201 *zd, int rid, void *riddata,
	unsigned int riddatalen)
{
	int err;
	int i = 0;
	int code;
	int rid_fid;
	int length;
	unsigned char *pdata;

	zd->rxdatas = 0;
	err = zd1201_docmd(zd, ZD1201_CMDCODE_ACCESS, rid, 0, 0);
	if (err)
		return err;

	wait_event_interruptible(zd->rxdataq, zd->rxdatas);
	if (!zd->rxlen)
		return -EIO;

	code = le16_to_cpu(*(__le16*)(&zd->rxdata[4]));
	rid_fid = le16_to_cpu(*(__le16*)(&zd->rxdata[6]));
	length = le16_to_cpu(*(__le16*)(&zd->rxdata[8]));
	if (length > zd->rxlen)
		length = zd->rxlen-6;

	/* If access bit is not on, then error */
	if ((code & ZD1201_ACCESSBIT) != ZD1201_ACCESSBIT || rid_fid != rid )
		return -EINVAL;

	/* Not enough buffer for allocating data */
	if (riddatalen != (length - 4)) {
		dev_dbg(&zd->usb->dev, "riddatalen mismatches, expected=%u, (packet=%u) length=%u, rid=0x%04X, rid_fid=0x%04X\n",
		    riddatalen, zd->rxlen, length, rid, rid_fid);
		return -ENODATA;
	}

	zd->rxdatas = 0;
	/* Issue SetRxRid commnd */			
	err = zd1201_docmd(zd, ZD1201_CMDCODE_SETRXRID, rid, 0, length);
	if (err)
		return err;

	/* Receive RID record from resource packets */
	wait_event_interruptible(zd->rxdataq, zd->rxdatas);
	if (!zd->rxlen)
		return -EIO;

	if (zd->rxdata[zd->rxlen - 1] != ZD1201_PACKET_RESOURCE) {
		dev_dbg(&zd->usb->dev, "Packet type mismatch: 0x%x not 0x3\n",
		    zd->rxdata[zd->rxlen-1]);
		return -EINVAL;
	}

	/* Set the data pointer and received data length */
	pdata = zd->rxdata;
	length = zd->rxlen;

	do {
		int actual_length;

		actual_length = (length > 64) ? 64 : length;

		if (pdata[0] != 0x3) {
			dev_dbg(&zd->usb->dev, "Rx Resource packet type error: %02X\n",
			    pdata[0]);
			return -EINVAL;
		}

		if (actual_length != 64) {
			/* Trim the last packet type byte */
			actual_length--;
		}

		/* Skip the 4 bytes header (RID length and RID) */
		if (i == 0) {
			pdata += 8;
			actual_length -= 8;
		} else {
			pdata += 4;
			actual_length -= 4;
		}
		
		memcpy(riddata, pdata, actual_length);
		riddata += actual_length;
		pdata += actual_length;
		length -= 64;
		i++;
	} while (length > 0);

	return 0;
}

/*
 *	resreq:
 *		byte	type
 *		byte	sequence
 *		u16	reserved
 *		byte	data[12]
 *	total: 16
 */
static int zd1201_setconfig(struct zd1201 *zd, int rid, void *buf, int len, int wait)
{
	int err;
	unsigned char *request;
	int reqlen;
	char seq=0;
	struct urb *urb;
	gfp_t gfp_mask = wait ? GFP_NOIO : GFP_ATOMIC;

	len += 4;			/* first 4 are for header */

	zd->rxdatas = 0;
	zd->rxlen = 0;
	for (seq=0; len > 0; seq++) {
		request = kmalloc(16, gfp_mask);
		if (!request)
			return -ENOMEM;
		urb = usb_alloc_urb(0, gfp_mask);
		if (!urb) {
			kfree(request);
			return -ENOMEM;
		}
		memset(request, 0, 16);
		reqlen = len>12 ? 12 : len;
		request[0] = ZD1201_USB_RESREQ;
		request[1] = seq;
		request[2] = 0;
		request[3] = 0;
		if (request[1] == 0) {
			/* add header */
			*(__le16*)&request[4] = cpu_to_le16((len-2+1)/2);
			*(__le16*)&request[6] = cpu_to_le16(rid);
			memcpy(request+8, buf, reqlen-4);
			buf += reqlen-4;
		} else {
			memcpy(request+4, buf, reqlen);
			buf += reqlen;
		}

		len -= reqlen;

		usb_fill_bulk_urb(urb, zd->usb, usb_sndbulkpipe(zd->usb,
		    zd->endp_out2), request, 16, zd1201_usbfree, zd);
		err = usb_submit_urb(urb, gfp_mask);
		if (err)
			goto err;
	}

	request = kmalloc(16, gfp_mask);
	if (!request)
		return -ENOMEM;
	urb = usb_alloc_urb(0, gfp_mask);
	if (!urb) {
		kfree(request);
		return -ENOMEM;
	}
	*((__le32*)request) = cpu_to_le32(ZD1201_USB_CMDREQ);
	*((__le16*)&request[4]) = 
	    cpu_to_le16(ZD1201_CMDCODE_ACCESS|ZD1201_ACCESSBIT);
	*((__le16*)&request[6]) = cpu_to_le16(rid);
	*((__le16*)&request[8]) = cpu_to_le16(0);
	*((__le16*)&request[10]) = cpu_to_le16(0);
	usb_fill_bulk_urb(urb, zd->usb, usb_sndbulkpipe(zd->usb, zd->endp_out2),
	     request, 16, zd1201_usbfree, zd);
	err = usb_submit_urb(urb, gfp_mask);
	if (err)
		goto err;
	
	if (wait) {
		wait_event_interruptible(zd->rxdataq, zd->rxdatas);
		if (!zd->rxlen || le16_to_cpu(*(__le16*)&zd->rxdata[6]) != rid) {
			dev_dbg(&zd->usb->dev, "wrong or no RID received\n");
		}
	}

	return 0;
err:
	kfree(request);
	usb_free_urb(urb);
	return err;
}

static inline int zd1201_getconfig16(struct zd1201 *zd, int rid, short *val)
{
	int err;
	__le16 zdval;

	err = zd1201_getconfig(zd, rid, &zdval, sizeof(__le16));
	if (err)
		return err;
	*val = le16_to_cpu(zdval);
	return 0;
}

static inline int zd1201_setconfig16(struct zd1201 *zd, int rid, short val)
{
	__le16 zdval = cpu_to_le16(val);
	return (zd1201_setconfig(zd, rid, &zdval, sizeof(__le16), 1));
}

static int zd1201_drvr_start(struct zd1201 *zd)
{
	int err, i;
	short max;
	__le16 zdmax;
	unsigned char *buffer;

	buffer = kzalloc(ZD1201_RXSIZE, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	usb_fill_bulk_urb(zd->rx_urb, zd->usb, 
	    usb_rcvbulkpipe(zd->usb, zd->endp_in), buffer, ZD1201_RXSIZE,
	    zd1201_usbrx, zd);

	err = usb_submit_urb(zd->rx_urb, GFP_KERNEL);
	if (err)
		goto err_buffer;

	err = zd1201_docmd(zd, ZD1201_CMDCODE_INIT, 0, 0, 0);
	if (err)
		goto err_urb;

	err = zd1201_getconfig(zd, ZD1201_RID_CNFMAXTXBUFFERNUMBER, &zdmax,
	    sizeof(__le16));
	if (err)
		goto err_urb;

	max = le16_to_cpu(zdmax);
	for (i=0; i<max; i++) {
		err = zd1201_docmd(zd, ZD1201_CMDCODE_ALLOC, 1514, 0, 0);
		if (err)
			goto err_urb;
	}

	return 0;

err_urb:
	usb_kill_urb(zd->rx_urb);
	return err;
err_buffer:
	kfree(buffer);
	return err;
}

/*	Magic alert: The firmware doesn't seem to like the MAC state being
 *	toggled in promisc (aka monitor) mode.
 *	(It works a number of times, but will halt eventually)
 *	So we turn it of before disabling and on after enabling if needed.
 */
static int zd1201_enable(struct zd1201 *zd)
{
	int err;

	if (zd->mac_enabled)
		return 0;

	err = zd1201_docmd(zd, ZD1201_CMDCODE_ENABLE, 0, 0, 0);
	if (!err)
		zd->mac_enabled = 1;

	if (zd->monitor)
		err = zd1201_setconfig16(zd, ZD1201_RID_PROMISCUOUSMODE, 1);

	return err;
}

static int zd1201_disable(struct zd1201 *zd)
{
	int err;

	if (!zd->mac_enabled)
		return 0;
	if (zd->monitor) {
		err = zd1201_setconfig16(zd, ZD1201_RID_PROMISCUOUSMODE, 0);
		if (err)
			return err;
	}

	err = zd1201_docmd(zd, ZD1201_CMDCODE_DISABLE, 0, 0, 0);
	if (!err)
		zd->mac_enabled = 0;
	return err;
}

static int zd1201_mac_reset(struct zd1201 *zd)
{
	if (!zd->mac_enabled)
		return 0;
	zd1201_disable(zd);
	return zd1201_enable(zd);
}

static int zd1201_join(struct zd1201 *zd, char *essid, int essidlen)
{
	int err, val;
	char buf[IW_ESSID_MAX_SIZE+2];

	err = zd1201_disable(zd);
	if (err)
		return err;

	val = ZD1201_CNFAUTHENTICATION_OPENSYSTEM;
	val |= ZD1201_CNFAUTHENTICATION_SHAREDKEY;
	err = zd1201_setconfig16(zd, ZD1201_RID_CNFAUTHENTICATION, val);
	if (err)
		return err;

	*(__le16 *)buf = cpu_to_le16(essidlen);
	memcpy(buf+2, essid, essidlen);
	if (!zd->ap) {	/* Normal station */
		err = zd1201_setconfig(zd, ZD1201_RID_CNFDESIREDSSID, buf,
		    IW_ESSID_MAX_SIZE+2, 1);
		if (err)
			return err;
	} else {	/* AP */
		err = zd1201_setconfig(zd, ZD1201_RID_CNFOWNSSID, buf,
		    IW_ESSID_MAX_SIZE+2, 1);
		if (err)
			return err;
	}

	err = zd1201_setconfig(zd, ZD1201_RID_CNFOWNMACADDR, 
	    zd->dev->dev_addr, zd->dev->addr_len, 1);
	if (err)
		return err;

	err = zd1201_enable(zd);
	if (err)
		return err;

	msleep(100);
	return 0;
}

static int zd1201_net_open(struct net_device *dev)
{
	struct zd1201 *zd = (struct zd1201 *)dev->priv;

	/* Start MAC with wildcard if no essid set */
	if (!zd->mac_enabled)
		zd1201_join(zd, zd->essid, zd->essidlen);
	netif_start_queue(dev);

	return 0;
}

static int zd1201_net_stop(struct net_device *dev)
{
	netif_stop_queue(dev);
	return 0;
}

/*
	RFC 1042 encapsulates Ethernet frames in 802.11 frames
	by prefixing them with 0xaa, 0xaa, 0x03) followed by a SNAP OID of 0
	(0x00, 0x00, 0x00). Zd requires an additional padding, copy
	of ethernet addresses, length of the standard RFC 1042 packet
	and a command byte (which is nul for tx).
	
	tx frame (from Wlan NG):
	RFC 1042:
		llc		0xAA 0xAA 0x03 (802.2 LLC)
		snap		0x00 0x00 0x00 (Ethernet encapsulated)
		type		2 bytes, Ethernet type field
		payload		(minus eth header)
	Zydas specific:
		padding		1B if (skb->len+8+1)%64==0
		Eth MAC addr	12 bytes, Ethernet MAC addresses
		length		2 bytes, RFC 1042 packet length 
				(llc+snap+type+payload)
		zd		1 null byte, zd1201 packet type
 */
static int zd1201_hard_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct zd1201 *zd = (struct zd1201 *)dev->priv;
	unsigned char *txbuf = zd->txdata;
	int txbuflen, pad = 0, err;
	struct urb *urb = zd->tx_urb;

	if (!zd->mac_enabled || zd->monitor) {
		zd->stats.tx_dropped++;
		kfree_skb(skb);
		return 0;
	}
	netif_stop_queue(dev);

	txbuflen = skb->len + 8 + 1;
	if (txbuflen%64 == 0) {
		pad = 1;
		txbuflen++;
	}
	txbuf[0] = 0xAA;
	txbuf[1] = 0xAA;
	txbuf[2] = 0x03;
	txbuf[3] = 0x00;	/* rfc1042 */
	txbuf[4] = 0x00;
	txbuf[5] = 0x00;

	skb_copy_from_linear_data_offset(skb, 12, txbuf + 6, skb->len - 12);
	if (pad)
		txbuf[skb->len-12+6]=0;
	skb_copy_from_linear_data(skb, txbuf + skb->len - 12 + 6 + pad, 12);
	*(__be16*)&txbuf[skb->len+6+pad] = htons(skb->len-12+6);
	txbuf[txbuflen-1] = 0;

	usb_fill_bulk_urb(urb, zd->usb, usb_sndbulkpipe(zd->usb, zd->endp_out),
	    txbuf, txbuflen, zd1201_usbtx, zd);

	err = usb_submit_urb(zd->tx_urb, GFP_ATOMIC);
	if (err) {
		zd->stats.tx_errors++;
		netif_start_queue(dev);
		return err;
	}
	zd->stats.tx_packets++;
	zd->stats.tx_bytes += skb->len;
	dev->trans_start = jiffies;
	kfree_skb(skb);

	return 0;
}

static void zd1201_tx_timeout(struct net_device *dev)
{
	struct zd1201 *zd = (struct zd1201 *)dev->priv;

	if (!zd)
		return;
	dev_warn(&zd->usb->dev, "%s: TX timeout, shooting down urb\n",
	    dev->name);
	usb_unlink_urb(zd->tx_urb);
	zd->stats.tx_errors++;
	/* Restart the timeout to quiet the watchdog: */
	dev->trans_start = jiffies;
}

static int zd1201_set_mac_address(struct net_device *dev, void *p)
{
	struct sockaddr *addr = p;
	struct zd1201 *zd = (struct zd1201 *)dev->priv;
	int err;

	if (!zd)
		return -ENODEV;

	err = zd1201_setconfig(zd, ZD1201_RID_CNFOWNMACADDR, 
	    addr->sa_data, dev->addr_len, 1);
	if (err)
		return err;
	memcpy(dev->dev_addr, addr->sa_data, dev->addr_len);

	return zd1201_mac_reset(zd);
}

static struct net_device_stats *zd1201_get_stats(struct net_device *dev)
{
	struct zd1201 *zd = (struct zd1201 *)dev->priv;

	return &zd->stats;
}

static struct iw_statistics *zd1201_get_wireless_stats(struct net_device *dev)
{
	struct zd1201 *zd = (struct zd1201 *)dev->priv;

	return &zd->iwstats;
}

static void zd1201_set_multicast(struct net_device *dev)
{
	struct zd1201 *zd = (struct zd1201 *)dev->priv;
	struct dev_mc_list *mc = dev->mc_list;
	unsigned char reqbuf[ETH_ALEN*ZD1201_MAXMULTI];
	int i;

	if (dev->mc_count > ZD1201_MAXMULTI)
		return;

	for (i=0; i<dev->mc_count; i++) {
		memcpy(reqbuf+i*ETH_ALEN, mc->dmi_addr, ETH_ALEN);
		mc = mc->next;
	}
	zd1201_setconfig(zd, ZD1201_RID_CNFGROUPADDRESS, reqbuf,
	    dev->mc_count*ETH_ALEN, 0);
	
}

static int zd1201_config_commit(struct net_device *dev, 
    struct iw_request_info *info, struct iw_point *data, char *essid)
{
	struct zd1201 *zd = (struct zd1201 *)dev->priv;

	return zd1201_mac_reset(zd);
}

static int zd1201_get_name(struct net_device *dev,
    struct iw_request_info *info, char *name, char *extra)
{
	strcpy(name, "IEEE 802.11b");
	return 0;
}

static int zd1201_set_freq(struct net_device *dev,
    struct iw_request_info *info, struct iw_freq *freq, char *extra)
{
	struct zd1201 *zd = (struct zd1201 *)dev->priv;
	short channel = 0;
	int err;

	if (freq->e == 0)
		channel = freq->m;
	else {
		if (freq->m >= 2482)
			channel = 14;
		if (freq->m >= 2407)
			channel = (freq->m-2407)/5;
	}

	err = zd1201_setconfig16(zd, ZD1201_RID_CNFOWNCHANNEL, channel);
	if (err)
		return err;

	zd1201_mac_reset(zd);

	return 0;
}

static int zd1201_get_freq(struct net_device *dev,
    struct iw_request_info *info, struct iw_freq *freq, char *extra)
{
	struct zd1201 *zd = (struct zd1201 *)dev->priv;
	short channel;
	int err;

	err = zd1201_getconfig16(zd, ZD1201_RID_CNFOWNCHANNEL, &channel);
	if (err)
		return err;
	freq->e = 0;
	freq->m = channel;

	return 0;
}

static int zd1201_set_mode(struct net_device *dev,
    struct iw_request_info *info, __u32 *mode, char *extra)
{
	struct zd1201 *zd = (struct zd1201 *)dev->priv;
	short porttype, monitor = 0;
	unsigned char buffer[IW_ESSID_MAX_SIZE+2];
	int err;

	if (zd->ap) {
		if (*mode != IW_MODE_MASTER)
			return -EINVAL;
		return 0;
	}

	err = zd1201_setconfig16(zd, ZD1201_RID_PROMISCUOUSMODE, 0);
	if (err)
		return err;
	zd->dev->type = ARPHRD_ETHER;
	switch(*mode) {
		case IW_MODE_MONITOR:
			monitor = 1;
			zd->dev->type = ARPHRD_IEEE80211;
			/* Make sure we are no longer associated with by
			   setting an 'impossible' essid.
			   (otherwise we mess up firmware)
			 */
			zd1201_join(zd, "\0-*#\0", 5);
			/* Put port in pIBSS */
		case 8: /* No pseudo-IBSS in wireless extensions (yet) */
			porttype = ZD1201_PORTTYPE_PSEUDOIBSS;
			break;
		case IW_MODE_ADHOC:
			porttype = ZD1201_PORTTYPE_IBSS;
			break;
		case IW_MODE_INFRA:
			porttype = ZD1201_PORTTYPE_BSS;
			break;
		default:
			return -EINVAL;
	}

	err = zd1201_setconfig16(zd, ZD1201_RID_CNFPORTTYPE, porttype);
	if (err)
		return err;
	if (zd->monitor && !monitor) {
			zd1201_disable(zd);
			*(__le16 *)buffer = cpu_to_le16(zd->essidlen);
			memcpy(buffer+2, zd->essid, zd->essidlen);
			err = zd1201_setconfig(zd, ZD1201_RID_CNFDESIREDSSID,
			    buffer, IW_ESSID_MAX_SIZE+2, 1);
			if (err)
				return err;
	}
	zd->monitor = monitor;
	/* If monitor mode is set we don't actually turn it on here since it
	 * is done during mac reset anyway (see zd1201_mac_enable).
	 */
	zd1201_mac_reset(zd);

	return 0;
}

static int zd1201_get_mode(struct net_device *dev,
    struct iw_request_info *info, __u32 *mode, char *extra)
{
	struct zd1201 *zd = (struct zd1201 *)dev->priv;
	short porttype;
	int err;

	err = zd1201_getconfig16(zd, ZD1201_RID_CNFPORTTYPE, &porttype);
	if (err)
		return err;
	switch(porttype) {
		case ZD1201_PORTTYPE_IBSS:
			*mode = IW_MODE_ADHOC;
			break;
		case ZD1201_PORTTYPE_BSS:
			*mode = IW_MODE_INFRA;
			break;
		case ZD1201_PORTTYPE_WDS:
			*mode = IW_MODE_REPEAT;
			break;
		case ZD1201_PORTTYPE_PSEUDOIBSS:
			*mode = 8;/* No Pseudo-IBSS... */
			break;
		case ZD1201_PORTTYPE_AP:
			*mode = IW_MODE_MASTER;
			break;
		default:
			dev_dbg(&zd->usb->dev, "Unknown porttype: %d\n",
			    porttype);
			*mode = IW_MODE_AUTO;
	}
	if (zd->monitor)
		*mode = IW_MODE_MONITOR;

	return 0;
}

static int zd1201_get_range(struct net_device *dev,
    struct iw_request_info *info, struct iw_point *wrq, char *extra)
{
	struct iw_range *range = (struct iw_range *)extra;

	wrq->length = sizeof(struct iw_range);
	memset(range, 0, sizeof(struct iw_range));
	range->we_version_compiled = WIRELESS_EXT;
	range->we_version_source = WIRELESS_EXT;

	range->max_qual.qual = 128;
	range->max_qual.level = 128;
	range->max_qual.noise = 128;
	range->max_qual.updated = 7;

	range->encoding_size[0] = 5;
	range->encoding_size[1] = 13;
	range->num_encoding_sizes = 2;
	range->max_encoding_tokens = ZD1201_NUMKEYS;

	range->num_bitrates = 4;
	range->bitrate[0] = 1000000;
	range->bitrate[1] = 2000000;
	range->bitrate[2] = 5500000;
	range->bitrate[3] = 11000000;

	range->min_rts = 0;
	range->min_frag = ZD1201_FRAGMIN;
	range->max_rts = ZD1201_RTSMAX;
	range->min_frag = ZD1201_FRAGMAX;

	return 0;
}

/*	Little bit of magic here: we only get the quality if we poll
 *	for it, and we never get an actual request to trigger such
 *	a poll. Therefore we 'assume' that the user will soon ask for
 *	the stats after asking the bssid.
 */
static int zd1201_get_wap(struct net_device *dev,
    struct iw_request_info *info, struct sockaddr *ap_addr, char *extra)
{
	struct zd1201 *zd = (struct zd1201 *)dev->priv;
	unsigned char buffer[6];

	if (!zd1201_getconfig(zd, ZD1201_RID_COMMSQUALITY, buffer, 6)) {
		/* Unfortunately the quality and noise reported is useless.
		   they seem to be accumulators that increase until you
		   read them, unless we poll on a fixed interval we can't
		   use them
		 */
		/*zd->iwstats.qual.qual = le16_to_cpu(((__le16 *)buffer)[0]);*/
		zd->iwstats.qual.level = le16_to_cpu(((__le16 *)buffer)[1]);
		/*zd->iwstats.qual.noise = le16_to_cpu(((__le16 *)buffer)[2]);*/
		zd->iwstats.qual.updated = 2;
	}

	return zd1201_getconfig(zd, ZD1201_RID_CURRENTBSSID, ap_addr->sa_data, 6);
}

static int zd1201_set_scan(struct net_device *dev,
    struct iw_request_info *info, struct iw_point *srq, char *extra)
{
	/* We do everything in get_scan */
	return 0;
}

static int zd1201_get_scan(struct net_device *dev,
    struct iw_request_info *info, struct iw_point *srq, char *extra)
{
	struct zd1201 *zd = (struct zd1201 *)dev->priv;
	int err, i, j, enabled_save;
	struct iw_event iwe;
	char *cev = extra;
	char *end_buf = extra + IW_SCAN_MAX_DATA;

	/* No scanning in AP mode */
	if (zd->ap)
		return -EOPNOTSUPP;

	/* Scan doesn't seem to work if disabled */
	enabled_save = zd->mac_enabled;
	zd1201_enable(zd);

	zd->rxdatas = 0;
	err = zd1201_docmd(zd, ZD1201_CMDCODE_INQUIRE, 
	     ZD1201_INQ_SCANRESULTS, 0, 0);
	if (err)
		return err;

	wait_event_interruptible(zd->rxdataq, zd->rxdatas);
	if (!zd->rxlen)
		return -EIO;

	if (le16_to_cpu(*(__le16*)&zd->rxdata[2]) != ZD1201_INQ_SCANRESULTS)
		return -EIO;

	for(i=8; i<zd->rxlen; i+=62) {
		iwe.cmd = SIOCGIWAP;
		iwe.u.ap_addr.sa_family = ARPHRD_ETHER;
		memcpy(iwe.u.ap_addr.sa_data, zd->rxdata+i+6, 6);
		cev = iwe_stream_add_event(cev, end_buf, &iwe, IW_EV_ADDR_LEN);

		iwe.cmd = SIOCGIWESSID;
		iwe.u.data.length = zd->rxdata[i+16];
		iwe.u.data.flags = 1;
		cev = iwe_stream_add_point(cev, end_buf, &iwe, zd->rxdata+i+18);

		iwe.cmd = SIOCGIWMODE;
		if (zd->rxdata[i+14]&0x01)
			iwe.u.mode = IW_MODE_MASTER;
		else
			iwe.u.mode = IW_MODE_ADHOC;
		cev = iwe_stream_add_event(cev, end_buf, &iwe, IW_EV_UINT_LEN);
		
		iwe.cmd = SIOCGIWFREQ;
		iwe.u.freq.m = zd->rxdata[i+0];
		iwe.u.freq.e = 0;
		cev = iwe_stream_add_event(cev, end_buf, &iwe, IW_EV_FREQ_LEN);
		
		iwe.cmd = SIOCGIWRATE;
		iwe.u.bitrate.fixed = 0;
		iwe.u.bitrate.disabled = 0;
		for (j=0; j<10; j++) if (zd->rxdata[i+50+j]) {
			iwe.u.bitrate.value = (zd->rxdata[i+50+j]&0x7f)*500000;
			cev=iwe_stream_add_event(cev, end_buf, &iwe,
			    IW_EV_PARAM_LEN);
		}
		
		iwe.cmd = SIOCGIWENCODE;
		iwe.u.data.length = 0;
		if (zd->rxdata[i+14]&0x10)
			iwe.u.data.flags = IW_ENCODE_ENABLED;
		else
			iwe.u.data.flags = IW_ENCODE_DISABLED;
		cev = iwe_stream_add_point(cev, end_buf, &iwe, NULL);
		
		iwe.cmd = IWEVQUAL;
		iwe.u.qual.qual = zd->rxdata[i+4];
		iwe.u.qual.noise= zd->rxdata[i+2]/10-100;
		iwe.u.qual.level = (256+zd->rxdata[i+4]*100)/255-100;
		iwe.u.qual.updated = 7;
		cev = iwe_stream_add_event(cev, end_buf, &iwe, IW_EV_QUAL_LEN);
	}

	if (!enabled_save)
		zd1201_disable(zd);

	srq->length = cev - extra;
	srq->flags = 0;

	return 0;
}

static int zd1201_set_essid(struct net_device *dev,
    struct iw_request_info *info, struct iw_point *data, char *essid)
{
	struct zd1201 *zd = (struct zd1201 *)dev->priv;

	if (data->length > IW_ESSID_MAX_SIZE)
		return -EINVAL;
	if (data->length < 1)
		data->length = 1;
	zd->essidlen = data->length;
	memset(zd->essid, 0, IW_ESSID_MAX_SIZE+1);
	memcpy(zd->essid, essid, data->length);
	return zd1201_join(zd, zd->essid, zd->essidlen);
}

static int zd1201_get_essid(struct net_device *dev,
    struct iw_request_info *info, struct iw_point *data, char *essid)
{
	struct zd1201 *zd = (struct zd1201 *)dev->priv;

	memcpy(essid, zd->essid, zd->essidlen);
	data->flags = 1;
	data->length = zd->essidlen;

	return 0;
}

static int zd1201_get_nick(struct net_device *dev, struct iw_request_info *info,
    struct iw_point *data, char *nick)
{
	strcpy(nick, "zd1201");
	data->flags = 1;
	data->length = strlen(nick);
	return 0;
}

static int zd1201_set_rate(struct net_device *dev,
    struct iw_request_info *info, struct iw_param *rrq, char *extra)
{
	struct zd1201 *zd = (struct zd1201 *)dev->priv;
	short rate;
	int err;

	switch (rrq->value) {
		case 1000000:
			rate = ZD1201_RATEB1;
			break;
		case 2000000:
			rate = ZD1201_RATEB2;
			break;
		case 5500000:
			rate = ZD1201_RATEB5;
			break;
		case 11000000:
		default:
			rate = ZD1201_RATEB11;
			break;
	}
	if (!rrq->fixed) { /* Also enable all lower bitrates */
		rate |= rate-1;
	}

	err = zd1201_setconfig16(zd, ZD1201_RID_TXRATECNTL, rate);
	if (err)
		return err;

	return zd1201_mac_reset(zd);
}

static int zd1201_get_rate(struct net_device *dev,
    struct iw_request_info *info, struct iw_param *rrq, char *extra)
{
	struct zd1201 *zd = (struct zd1201 *)dev->priv;
	short rate;
	int err;

	err = zd1201_getconfig16(zd, ZD1201_RID_CURRENTTXRATE, &rate);
	if (err)
		return err;

	switch(rate) {
		case 1:
			rrq->value = 1000000;
			break;
		case 2:
			rrq->value = 2000000;
			break;
		case 5:
			rrq->value = 5500000;
			break;
		case 11:
			rrq->value = 11000000;
			break;
		default:
			rrq->value = 0;
	}
	rrq->fixed = 0;
	rrq->disabled = 0;

	return 0;
}

static int zd1201_set_rts(struct net_device *dev, struct iw_request_info *info,
    struct iw_param *rts, char *extra)
{
	struct zd1201 *zd = (struct zd1201 *)dev->priv;
	int err;
	short val = rts->value;

	if (rts->disabled || !rts->fixed)
		val = ZD1201_RTSMAX;
	if (val > ZD1201_RTSMAX)
		return -EINVAL;
	if (val < 0)
		return -EINVAL;

	err = zd1201_setconfig16(zd, ZD1201_RID_CNFRTSTHRESHOLD, val);
	if (err)
		return err;
	return zd1201_mac_reset(zd);
}

static int zd1201_get_rts(struct net_device *dev, struct iw_request_info *info,
    struct iw_param *rts, char *extra)
{
	struct zd1201 *zd = (struct zd1201 *)dev->priv;
	short rtst;
	int err;

	err = zd1201_getconfig16(zd, ZD1201_RID_CNFRTSTHRESHOLD, &rtst);
	if (err)
		return err;
	rts->value = rtst;
	rts->disabled = (rts->value == ZD1201_RTSMAX);
	rts->fixed = 1;

	return 0;
}

static int zd1201_set_frag(struct net_device *dev, struct iw_request_info *info,
    struct iw_param *frag, char *extra)
{
	struct zd1201 *zd = (struct zd1201 *)dev->priv;
	int err;
	short val = frag->value;

	if (frag->disabled || !frag->fixed)
		val = ZD1201_FRAGMAX;
	if (val > ZD1201_FRAGMAX)
		return -EINVAL;
	if (val < ZD1201_FRAGMIN)
		return -EINVAL;
	if (val & 1)
		return -EINVAL;
	err = zd1201_setconfig16(zd, ZD1201_RID_CNFFRAGTHRESHOLD, val);
	if (err)
		return err;
	return zd1201_mac_reset(zd);
}

static int zd1201_get_frag(struct net_device *dev, struct iw_request_info *info,
    struct iw_param *frag, char *extra)
{
	struct zd1201 *zd = (struct zd1201 *)dev->priv;
	short fragt;
	int err;

	err = zd1201_getconfig16(zd, ZD1201_RID_CNFFRAGTHRESHOLD, &fragt);
	if (err)
		return err;
	frag->value = fragt;
	frag->disabled = (frag->value == ZD1201_FRAGMAX);
	frag->fixed = 1;

	return 0;
}

static int zd1201_set_retry(struct net_device *dev,
    struct iw_request_info *info, struct iw_param *rrq, char *extra)
{
	return 0;
}

static int zd1201_get_retry(struct net_device *dev,
    struct iw_request_info *info, struct iw_param *rrq, char *extra)
{
	return 0;
}

static int zd1201_set_encode(struct net_device *dev,
    struct iw_request_info *info, struct iw_point *erq, char *key)
{
	struct zd1201 *zd = (struct zd1201 *)dev->priv;
	short i;
	int err, rid;

	if (erq->length > ZD1201_MAXKEYLEN)
		return -EINVAL;

	i = (erq->flags & IW_ENCODE_INDEX)-1;
	if (i == -1) {
		err = zd1201_getconfig16(zd,ZD1201_RID_CNFDEFAULTKEYID,&i);
		if (err)
			return err;
	} else {
		err = zd1201_setconfig16(zd, ZD1201_RID_CNFDEFAULTKEYID, i);
		if (err)
			return err;
	}

	if (i < 0 || i >= ZD1201_NUMKEYS)
		return -EINVAL;

	rid = ZD1201_RID_CNFDEFAULTKEY0 + i;
	err = zd1201_setconfig(zd, rid, key, erq->length, 1);
	if (err)
		return err;
	zd->encode_keylen[i] = erq->length;
	memcpy(zd->encode_keys[i], key, erq->length);

	i=0;
	if (!(erq->flags & IW_ENCODE_DISABLED & IW_ENCODE_MODE)) {
		i |= 0x01;
		zd->encode_enabled = 1;
	} else
		zd->encode_enabled = 0;
	if (erq->flags & IW_ENCODE_RESTRICTED & IW_ENCODE_MODE) {
		i |= 0x02;
		zd->encode_restricted = 1;
	} else
		zd->encode_restricted = 0;
	err = zd1201_setconfig16(zd, ZD1201_RID_CNFWEBFLAGS, i);
	if (err)
		return err;

	if (zd->encode_enabled)
		i = ZD1201_CNFAUTHENTICATION_SHAREDKEY;
	else
		i = ZD1201_CNFAUTHENTICATION_OPENSYSTEM;
	err = zd1201_setconfig16(zd, ZD1201_RID_CNFAUTHENTICATION, i);
	if (err)
		return err;

	return zd1201_mac_reset(zd);
}

static int zd1201_get_encode(struct net_device *dev,
    struct iw_request_info *info, struct iw_point *erq, char *key)
{
	struct zd1201 *zd = (struct zd1201 *)dev->priv;
	short i;
	int err;

	if (zd->encode_enabled)
		erq->flags = IW_ENCODE_ENABLED;
	else
		erq->flags = IW_ENCODE_DISABLED;
	if (zd->encode_restricted)
		erq->flags |= IW_ENCODE_RESTRICTED;
	else
		erq->flags |= IW_ENCODE_OPEN;

	i = (erq->flags & IW_ENCODE_INDEX) -1;
	if (i == -1) {
		err = zd1201_getconfig16(zd, ZD1201_RID_CNFDEFAULTKEYID, &i);
		if (err)
			return err;
	}
	if (i<0 || i>= ZD1201_NUMKEYS)
		return -EINVAL;

	erq->flags |= i+1;

	erq->length = zd->encode_keylen[i];
	memcpy(key, zd->encode_keys[i], erq->length);

	return 0;
}

static int zd1201_set_power(struct net_device *dev, 
    struct iw_request_info *info, struct iw_param *vwrq, char *extra)
{
	struct zd1201 *zd = (struct zd1201 *)dev->priv;
	short enabled, duration, level;
	int err;

	enabled = vwrq->disabled ? 0 : 1;
	if (enabled) {
		if (vwrq->flags & IW_POWER_PERIOD) {
			duration = vwrq->value;
			err = zd1201_setconfig16(zd, 
			    ZD1201_RID_CNFMAXSLEEPDURATION, duration);
			if (err)
				return err;
			goto out;
		}
		if (vwrq->flags & IW_POWER_TIMEOUT) {
			err = zd1201_getconfig16(zd, 
			    ZD1201_RID_CNFMAXSLEEPDURATION, &duration);
			if (err)
				return err;
			level = vwrq->value * 4 / duration;
			if (level > 4)
				level = 4;
			if (level < 0)
				level = 0;
			err = zd1201_setconfig16(zd, ZD1201_RID_CNFPMEPS,
			    level);
			if (err)
				return err;
			goto out;
		}
		return -EINVAL;
	}
out:
	return zd1201_setconfig16(zd, ZD1201_RID_CNFPMENABLED, enabled);
}

static int zd1201_get_power(struct net_device *dev,
    struct iw_request_info *info, struct iw_param *vwrq, char *extra)
{
	struct zd1201 *zd = (struct zd1201 *)dev->priv;
	short enabled, level, duration;
	int err;

	err = zd1201_getconfig16(zd, ZD1201_RID_CNFPMENABLED, &enabled);
	if (err)
		return err;
	err = zd1201_getconfig16(zd, ZD1201_RID_CNFPMEPS, &level);
	if (err)
		return err;
	err = zd1201_getconfig16(zd, ZD1201_RID_CNFMAXSLEEPDURATION, &duration);
	if (err)
		return err;
	vwrq->disabled = enabled ? 0 : 1;
	if (vwrq->flags & IW_POWER_TYPE) {
		if (vwrq->flags & IW_POWER_PERIOD) {
			vwrq->value = duration;
			vwrq->flags = IW_POWER_PERIOD;
		} else {
			vwrq->value = duration * level / 4;
			vwrq->flags = IW_POWER_TIMEOUT;
		}
	}
	if (vwrq->flags & IW_POWER_MODE) {
		if (enabled && level)
			vwrq->flags = IW_POWER_UNICAST_R;
		else
			vwrq->flags = IW_POWER_ALL_R;
	}

	return 0;
}


static const iw_handler zd1201_iw_handler[] =
{
	(iw_handler) zd1201_config_commit,	/* SIOCSIWCOMMIT */
	(iw_handler) zd1201_get_name,    	/* SIOCGIWNAME */
	(iw_handler) NULL,			/* SIOCSIWNWID */
	(iw_handler) NULL,			/* SIOCGIWNWID */
	(iw_handler) zd1201_set_freq,		/* SIOCSIWFREQ */
	(iw_handler) zd1201_get_freq,		/* SIOCGIWFREQ */
	(iw_handler) zd1201_set_mode,		/* SIOCSIWMODE */
	(iw_handler) zd1201_get_mode,		/* SIOCGIWMODE */
	(iw_handler) NULL,                  	/* SIOCSIWSENS */
	(iw_handler) NULL,           		/* SIOCGIWSENS */
	(iw_handler) NULL,			/* SIOCSIWRANGE */
	(iw_handler) zd1201_get_range,           /* SIOCGIWRANGE */
	(iw_handler) NULL,			/* SIOCSIWPRIV */
	(iw_handler) NULL,			/* SIOCGIWPRIV */
	(iw_handler) NULL,			/* SIOCSIWSTATS */
	(iw_handler) NULL,			/* SIOCGIWSTATS */
	(iw_handler) NULL,			/* SIOCSIWSPY */
	(iw_handler) NULL,			/* SIOCGIWSPY */
	(iw_handler) NULL,			/* -- hole -- */
	(iw_handler) NULL,			/* -- hole -- */
	(iw_handler) NULL/*zd1201_set_wap*/,		/* SIOCSIWAP */
	(iw_handler) zd1201_get_wap,		/* SIOCGIWAP */
	(iw_handler) NULL,			/* -- hole -- */
	(iw_handler) NULL,       		/* SIOCGIWAPLIST */
	(iw_handler) zd1201_set_scan,		/* SIOCSIWSCAN */
	(iw_handler) zd1201_get_scan,		/* SIOCGIWSCAN */
	(iw_handler) zd1201_set_essid,		/* SIOCSIWESSID */
	(iw_handler) zd1201_get_essid,		/* SIOCGIWESSID */
	(iw_handler) NULL,         		/* SIOCSIWNICKN */
	(iw_handler) zd1201_get_nick, 		/* SIOCGIWNICKN */
	(iw_handler) NULL,			/* -- hole -- */
	(iw_handler) NULL,			/* -- hole -- */
	(iw_handler) zd1201_set_rate,		/* SIOCSIWRATE */
	(iw_handler) zd1201_get_rate,		/* SIOCGIWRATE */
	(iw_handler) zd1201_set_rts,		/* SIOCSIWRTS */
	(iw_handler) zd1201_get_rts,		/* SIOCGIWRTS */
	(iw_handler) zd1201_set_frag,		/* SIOCSIWFRAG */
	(iw_handler) zd1201_get_frag,		/* SIOCGIWFRAG */
	(iw_handler) NULL,         		/* SIOCSIWTXPOW */
	(iw_handler) NULL,          		/* SIOCGIWTXPOW */
	(iw_handler) zd1201_set_retry,		/* SIOCSIWRETRY */
	(iw_handler) zd1201_get_retry,		/* SIOCGIWRETRY */
	(iw_handler) zd1201_set_encode,		/* SIOCSIWENCODE */
	(iw_handler) zd1201_get_encode,		/* SIOCGIWENCODE */
	(iw_handler) zd1201_set_power,		/* SIOCSIWPOWER */
	(iw_handler) zd1201_get_power,		/* SIOCGIWPOWER */
};

static int zd1201_set_hostauth(struct net_device *dev,
    struct iw_request_info *info, struct iw_param *rrq, char *extra)
{
	struct zd1201 *zd = (struct zd1201 *)dev->priv;

	if (!zd->ap)
		return -EOPNOTSUPP;

	return zd1201_setconfig16(zd, ZD1201_RID_CNFHOSTAUTH, rrq->value);
}

static int zd1201_get_hostauth(struct net_device *dev,
    struct iw_request_info *info, struct iw_param *rrq, char *extra)
{
	struct zd1201 *zd = (struct zd1201 *)dev->priv;
	short hostauth;
	int err;

	if (!zd->ap)
		return -EOPNOTSUPP;

	err = zd1201_getconfig16(zd, ZD1201_RID_CNFHOSTAUTH, &hostauth);
	if (err)
		return err;
	rrq->value = hostauth;
	rrq->fixed = 1;

	return 0;
}

static int zd1201_auth_sta(struct net_device *dev,
    struct iw_request_info *info, struct sockaddr *sta, char *extra)
{
	struct zd1201 *zd = (struct zd1201 *)dev->priv;
	unsigned char buffer[10];

	if (!zd->ap)
		return -EOPNOTSUPP;

	memcpy(buffer, sta->sa_data, ETH_ALEN);
	*(short*)(buffer+6) = 0;	/* 0==success, 1==failure */
	*(short*)(buffer+8) = 0;

	return zd1201_setconfig(zd, ZD1201_RID_AUTHENTICATESTA, buffer, 10, 1);
}

static int zd1201_set_maxassoc(struct net_device *dev,
    struct iw_request_info *info, struct iw_param *rrq, char *extra)
{
	struct zd1201 *zd = (struct zd1201 *)dev->priv;
	int err;

	if (!zd->ap)
		return -EOPNOTSUPP;

	err = zd1201_setconfig16(zd, ZD1201_RID_CNFMAXASSOCSTATIONS, rrq->value);
	if (err)
		return err;
	return 0;
}

static int zd1201_get_maxassoc(struct net_device *dev,
    struct iw_request_info *info, struct iw_param *rrq, char *extra)
{
	struct zd1201 *zd = (struct zd1201 *)dev->priv;
	short maxassoc;
	int err;

	if (!zd->ap)
		return -EOPNOTSUPP;

	err = zd1201_getconfig16(zd, ZD1201_RID_CNFMAXASSOCSTATIONS, &maxassoc);
	if (err)
		return err;
	rrq->value = maxassoc;
	rrq->fixed = 1;

	return 0;
}

static const iw_handler zd1201_private_handler[] = {
	(iw_handler) zd1201_set_hostauth,	/* ZD1201SIWHOSTAUTH */
	(iw_handler) zd1201_get_hostauth,	/* ZD1201GIWHOSTAUTH */
	(iw_handler) zd1201_auth_sta,		/* ZD1201SIWAUTHSTA */
	(iw_handler) NULL,			/* nothing to get */
	(iw_handler) zd1201_set_maxassoc,	/* ZD1201SIMAXASSOC */
	(iw_handler) zd1201_get_maxassoc,	/* ZD1201GIMAXASSOC */
};

static const struct iw_priv_args zd1201_private_args[] = {
	{ ZD1201SIWHOSTAUTH, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,
	    IW_PRIV_TYPE_NONE, "sethostauth" },
	{ ZD1201GIWHOSTAUTH, IW_PRIV_TYPE_NONE,
	    IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, "gethostauth" },
	{ ZD1201SIWAUTHSTA, IW_PRIV_TYPE_ADDR | IW_PRIV_SIZE_FIXED | 1,
	    IW_PRIV_TYPE_NONE, "authstation" },
	{ ZD1201SIWMAXASSOC, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,
	    IW_PRIV_TYPE_NONE, "setmaxassoc" },
	{ ZD1201GIWMAXASSOC, IW_PRIV_TYPE_NONE,
	    IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, "getmaxassoc" },
};

static const struct iw_handler_def zd1201_iw_handlers = {
	.num_standard 		= ARRAY_SIZE(zd1201_iw_handler),
	.num_private 		= ARRAY_SIZE(zd1201_private_handler),
	.num_private_args 	= ARRAY_SIZE(zd1201_private_args),
	.standard 		= (iw_handler *)zd1201_iw_handler,
	.private 		= (iw_handler *)zd1201_private_handler,
	.private_args 		= (struct iw_priv_args *) zd1201_private_args,
	.get_wireless_stats	= zd1201_get_wireless_stats,
};

static int zd1201_probe(struct usb_interface *interface,
			const struct usb_device_id *id)
{
	struct zd1201 *zd;
	struct usb_device *usb;
	int err;
	short porttype;
	char buf[IW_ESSID_MAX_SIZE+2];

	usb = interface_to_usbdev(interface);

	zd = kzalloc(sizeof(struct zd1201), GFP_KERNEL);
	if (!zd)
		return -ENOMEM;
	zd->ap = ap;
	zd->usb = usb;
	zd->removed = 0;
	init_waitqueue_head(&zd->rxdataq);
	INIT_HLIST_HEAD(&zd->fraglist);
	
	err = zd1201_fw_upload(usb, zd->ap);
	if (err) {
		dev_err(&usb->dev, "zd1201 firmware upload failed: %d\n", err);
		goto err_zd;
	}
	
	zd->endp_in = 1;
	zd->endp_out = 1;
	zd->endp_out2 = 2;
	zd->rx_urb = usb_alloc_urb(0, GFP_KERNEL);
	zd->tx_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!zd->rx_urb || !zd->tx_urb)
		goto err_zd;

	mdelay(100);
	err = zd1201_drvr_start(zd);
	if (err)
		goto err_zd;

	err = zd1201_setconfig16(zd, ZD1201_RID_CNFMAXDATALEN, 2312);
	if (err)
		goto err_start;

	err = zd1201_setconfig16(zd, ZD1201_RID_TXRATECNTL,
	    ZD1201_RATEB1 | ZD1201_RATEB2 | ZD1201_RATEB5 | ZD1201_RATEB11);
	if (err)
		goto err_start;

	zd->dev = alloc_etherdev(0);
	if (!zd->dev)
		goto err_start;

	zd->dev->priv = zd;
	zd->dev->open = zd1201_net_open;
	zd->dev->stop = zd1201_net_stop;
	zd->dev->get_stats = zd1201_get_stats;
	zd->dev->wireless_handlers =
	    (struct iw_handler_def *)&zd1201_iw_handlers;
	zd->dev->hard_start_xmit = zd1201_hard_start_xmit;
	zd->dev->watchdog_timeo = ZD1201_TX_TIMEOUT;
	zd->dev->tx_timeout = zd1201_tx_timeout;
	zd->dev->set_multicast_list = zd1201_set_multicast;
	zd->dev->set_mac_address = zd1201_set_mac_address;
	strcpy(zd->dev->name, "wlan%d");

	err = zd1201_getconfig(zd, ZD1201_RID_CNFOWNMACADDR, 
	    zd->dev->dev_addr, zd->dev->addr_len);
	if (err)
		goto err_net;

	/* Set wildcard essid to match zd->essid */
	*(__le16 *)buf = cpu_to_le16(0);
	err = zd1201_setconfig(zd, ZD1201_RID_CNFDESIREDSSID, buf,
	    IW_ESSID_MAX_SIZE+2, 1);
	if (err)
		goto err_net;

	if (zd->ap)
		porttype = ZD1201_PORTTYPE_AP;
	else
		porttype = ZD1201_PORTTYPE_BSS;
	err = zd1201_setconfig16(zd, ZD1201_RID_CNFPORTTYPE, porttype);
	if (err)
		goto err_net;

	SET_NETDEV_DEV(zd->dev, &usb->dev);

	err = register_netdev(zd->dev);
	if (err)
		goto err_net;
	dev_info(&usb->dev, "%s: ZD1201 USB Wireless interface\n",
	    zd->dev->name);

	usb_set_intfdata(interface, zd);
	zd1201_enable(zd);	/* zd1201 likes to startup enabled, */
	zd1201_disable(zd);	/* interfering with all the wifis in range */
	return 0;

err_net:
	free_netdev(zd->dev);
err_start:
	/* Leave the device in reset state */
	zd1201_docmd(zd, ZD1201_CMDCODE_INIT, 0, 0, 0);
err_zd:
	usb_free_urb(zd->tx_urb);
	usb_free_urb(zd->rx_urb);
	kfree(zd);
	return err;
}

static void zd1201_disconnect(struct usb_interface *interface)
{
	struct zd1201 *zd=(struct zd1201 *)usb_get_intfdata(interface);
	struct hlist_node *node, *node2;
	struct zd1201_frag *frag;

	if (!zd)
		return;
	usb_set_intfdata(interface, NULL);
	if (zd->dev) {
		unregister_netdev(zd->dev);
		free_netdev(zd->dev);
	}

	hlist_for_each_entry_safe(frag, node, node2, &zd->fraglist, fnode) {
		hlist_del_init(&frag->fnode);
		kfree_skb(frag->skb);
		kfree(frag);
	}

	if (zd->tx_urb) {
		usb_kill_urb(zd->tx_urb);
		usb_free_urb(zd->tx_urb);
	}
	if (zd->rx_urb) {
		usb_kill_urb(zd->rx_urb);
		usb_free_urb(zd->rx_urb);
	}
	kfree(zd);
}

#ifdef CONFIG_PM

static int zd1201_suspend(struct usb_interface *interface,
			   pm_message_t message)
{
	struct zd1201 *zd = usb_get_intfdata(interface);

	netif_device_detach(zd->dev);

	zd->was_enabled = zd->mac_enabled;

	if (zd->was_enabled)
		return zd1201_disable(zd);
	else
		return 0;
}

static int zd1201_resume(struct usb_interface *interface)
{
	struct zd1201 *zd = usb_get_intfdata(interface);

	if (!zd || !zd->dev)
		return -ENODEV;

	netif_device_attach(zd->dev);

	if (zd->was_enabled)
		return zd1201_enable(zd);
	else
		return 0;
}

#else

#define zd1201_suspend NULL
#define zd1201_resume  NULL

#endif

static struct usb_driver zd1201_usb = {
	.name = "zd1201",
	.probe = zd1201_probe,
	.disconnect = zd1201_disconnect,
	.id_table = zd1201_table,
	.suspend = zd1201_suspend,
	.resume = zd1201_resume,
};

static int __init zd1201_init(void)
{
	return usb_register(&zd1201_usb);
}

static void __exit zd1201_cleanup(void)
{
	usb_deregister(&zd1201_usb);
}

module_init(zd1201_init);
module_exit(zd1201_cleanup);

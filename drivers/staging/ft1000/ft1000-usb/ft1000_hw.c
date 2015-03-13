/* CopyRight (C) 2007 Qualcomm Inc. All Rights Reserved.
 *
 *
 * This file is part of Express Card USB Driver
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/usb.h>
#include "ft1000_usb.h"
#include <linux/types.h>

#define HARLEY_READ_REGISTER     0x0
#define HARLEY_WRITE_REGISTER    0x01
#define HARLEY_READ_DPRAM_32     0x02
#define HARLEY_READ_DPRAM_LOW    0x03
#define HARLEY_READ_DPRAM_HIGH   0x04
#define HARLEY_WRITE_DPRAM_32    0x05
#define HARLEY_WRITE_DPRAM_LOW   0x06
#define HARLEY_WRITE_DPRAM_HIGH  0x07

#define HARLEY_READ_OPERATION    0xc1
#define HARLEY_WRITE_OPERATION   0x41

#if 0
#define JDEBUG
#endif

static int ft1000_submit_rx_urb(struct ft1000_info *info);

static u8 tempbuffer[1600];

#define MAX_RCV_LOOP   100

/* send a control message via USB interface synchronously
 *  Parameters:  ft1000_usb  - device structure
 *               pipe - usb control message pipe
 *               request - control request
 *               requesttype - control message request type
 *               value - value to be written or 0
 *               index - register index
 *               data - data buffer to hold the read/write values
 *               size - data size
 *               timeout - control message time out value
 */
static int ft1000_control(struct ft1000_usb *ft1000dev, unsigned int pipe,
			  u8 request, u8 requesttype, u16 value, u16 index,
			  void *data, u16 size, int timeout)
{
	int ret;

	if ((ft1000dev == NULL) || (ft1000dev->dev == NULL)) {
		pr_debug("ft1000dev or ft1000dev->dev == NULL, failure\n");
		return -ENODEV;
	}

	ret = usb_control_msg(ft1000dev->dev, pipe, request, requesttype,
			      value, index, data, size, timeout);

	if (ret > 0)
		ret = 0;

	return ret;
}

/* returns the value in a register */
int ft1000_read_register(struct ft1000_usb *ft1000dev, u16 *Data,
			 u16 nRegIndx)
{
	int ret = 0;

	ret = ft1000_control(ft1000dev,
			     usb_rcvctrlpipe(ft1000dev->dev, 0),
			     HARLEY_READ_REGISTER,
			     HARLEY_READ_OPERATION,
			     0,
			     nRegIndx,
			     Data,
			     2,
			     USB_CTRL_GET_TIMEOUT);

	return ret;
}

/* writes the value in a register */
int ft1000_write_register(struct ft1000_usb *ft1000dev, u16 value,
			  u16 nRegIndx)
{
	int ret = 0;

	ret = ft1000_control(ft1000dev,
			     usb_sndctrlpipe(ft1000dev->dev, 0),
			     HARLEY_WRITE_REGISTER,
			     HARLEY_WRITE_OPERATION,
			     value,
			     nRegIndx,
			     NULL,
			     0,
			     USB_CTRL_SET_TIMEOUT);

	return ret;
}

/* read a number of bytes from DPRAM */
int ft1000_read_dpram32(struct ft1000_usb *ft1000dev, u16 indx, u8 *buffer,
			u16 cnt)
{
	int ret = 0;

	ret = ft1000_control(ft1000dev,
			     usb_rcvctrlpipe(ft1000dev->dev, 0),
			     HARLEY_READ_DPRAM_32,
			     HARLEY_READ_OPERATION,
			     0,
			     indx,
			     buffer,
			     cnt,
			     USB_CTRL_GET_TIMEOUT);

	return ret;
}

/* writes into DPRAM a number of bytes */
int ft1000_write_dpram32(struct ft1000_usb *ft1000dev, u16 indx, u8 *buffer,
			 u16 cnt)
{
	int ret = 0;

	if (cnt % 4)
		cnt += cnt - (cnt % 4);

	ret = ft1000_control(ft1000dev,
			     usb_sndctrlpipe(ft1000dev->dev, 0),
			     HARLEY_WRITE_DPRAM_32,
			     HARLEY_WRITE_OPERATION,
			     0,
			     indx,
			     buffer,
			     cnt,
			     USB_CTRL_SET_TIMEOUT);

	return ret;
}

/* read 16 bits from DPRAM */
int ft1000_read_dpram16(struct ft1000_usb *ft1000dev, u16 indx, u8 *buffer,
			u8 highlow)
{
	int ret = 0;
	u8 request;

	if (highlow == 0)
		request = HARLEY_READ_DPRAM_LOW;
	else
		request = HARLEY_READ_DPRAM_HIGH;

	ret = ft1000_control(ft1000dev,
			     usb_rcvctrlpipe(ft1000dev->dev, 0),
			     request,
			     HARLEY_READ_OPERATION,
			     0,
			     indx,
			     buffer,
			     2,
			     USB_CTRL_GET_TIMEOUT);

	return ret;
}

/* write into DPRAM a number of bytes */
int ft1000_write_dpram16(struct ft1000_usb *ft1000dev, u16 indx, u16 value,
			 u8 highlow)
{
	int ret = 0;
	u8 request;

	if (highlow == 0)
		request = HARLEY_WRITE_DPRAM_LOW;
	else
		request = HARLEY_WRITE_DPRAM_HIGH;

	ret = ft1000_control(ft1000dev,
			     usb_sndctrlpipe(ft1000dev->dev, 0),
			     request,
			     HARLEY_WRITE_OPERATION,
			     value,
			     indx,
			     NULL,
			     0,
			     USB_CTRL_SET_TIMEOUT);

	return ret;
}

/* read DPRAM 4 words at a time */
int fix_ft1000_read_dpram32(struct ft1000_usb *ft1000dev, u16 indx,
			    u8 *buffer)
{
	u8 buf[16];
	u16 pos;
	int ret = 0;

	pos = (indx / 4) * 4;
	ret = ft1000_read_dpram32(ft1000dev, pos, buf, 16);

	if (ret == 0) {
		pos = (indx % 4) * 4;
		*buffer++ = buf[pos++];
		*buffer++ = buf[pos++];
		*buffer++ = buf[pos++];
		*buffer++ = buf[pos++];
	} else {
		pr_debug("DPRAM32 Read failed\n");
		*buffer++ = 0;
		*buffer++ = 0;
		*buffer++ = 0;
		*buffer++ = 0;
	}

	return ret;
}


/* Description: This function write to DPRAM 4 words at a time */
int fix_ft1000_write_dpram32(struct ft1000_usb *ft1000dev, u16 indx, u8 *buffer)
{
	u16 pos1;
	u16 pos2;
	u16 i;
	u8 buf[32];
	u8 resultbuffer[32];
	u8 *pdata;
	int ret  = 0;

	pos1 = (indx / 4) * 4;
	pdata = buffer;
	ret = ft1000_read_dpram32(ft1000dev, pos1, buf, 16);

	if (ret == 0) {
		pos2 = (indx % 4)*4;
		buf[pos2++] = *buffer++;
		buf[pos2++] = *buffer++;
		buf[pos2++] = *buffer++;
		buf[pos2++] = *buffer++;
		ret = ft1000_write_dpram32(ft1000dev, pos1, buf, 16);
	} else {
		pr_debug("DPRAM32 Read failed\n");
		return ret;
	}

	ret = ft1000_read_dpram32(ft1000dev, pos1, (u8 *)&resultbuffer[0], 16);

	if (ret == 0) {
		buffer = pdata;
		for (i = 0; i < 16; i++) {
			if (buf[i] != resultbuffer[i])
				ret = -1;
		}
	}

	if (ret == -1) {
		ret = ft1000_write_dpram32(ft1000dev, pos1,
					   (u8 *)&tempbuffer[0], 16);
		ret = ft1000_read_dpram32(ft1000dev, pos1,
					  (u8 *)&resultbuffer[0], 16);
		if (ret == 0) {
			buffer = pdata;
			for (i = 0; i < 16; i++) {
				if (tempbuffer[i] != resultbuffer[i]) {
					ret = -1;
					pr_debug("Failed to write\n");
				}
			}
		}
	}

	return ret;
}

/* reset or activate the DSP */
static void card_reset_dsp(struct ft1000_usb *ft1000dev, bool value)
{
	int status = 0;
	u16 tempword;

	status = ft1000_write_register(ft1000dev, HOST_INTF_BE,
				       FT1000_REG_SUP_CTRL);
	status = ft1000_read_register(ft1000dev, &tempword,
				      FT1000_REG_SUP_CTRL);

	if (value) {
		pr_debug("Reset DSP\n");
		status = ft1000_read_register(ft1000dev, &tempword,
					      FT1000_REG_RESET);
		tempword |= DSP_RESET_BIT;
		status = ft1000_write_register(ft1000dev, tempword,
					       FT1000_REG_RESET);
	} else {
		pr_debug("Activate DSP\n");
		status = ft1000_read_register(ft1000dev, &tempword,
					      FT1000_REG_RESET);
		tempword |= DSP_ENCRYPTED;
		tempword &= ~DSP_UNENCRYPTED;
		status = ft1000_write_register(ft1000dev, tempword,
					       FT1000_REG_RESET);
		status = ft1000_read_register(ft1000dev, &tempword,
					      FT1000_REG_RESET);
		tempword &= ~EFUSE_MEM_DISABLE;
		tempword &= ~DSP_RESET_BIT;
		status = ft1000_write_register(ft1000dev, tempword,
					       FT1000_REG_RESET);
		status = ft1000_read_register(ft1000dev, &tempword,
					      FT1000_REG_RESET);
	}
}

/* send a command to ASIC
 *  Parameters:  ft1000_usb  - device structure
 *               ptempbuffer - command buffer
 *               size - command buffer size
 */
int card_send_command(struct ft1000_usb *ft1000dev, void *ptempbuffer,
		      int size)
{
	int ret;
	unsigned short temp;
	unsigned char *commandbuf;

	pr_debug("enter card_send_command... size=%d\n", size);

	ret = ft1000_read_register(ft1000dev, &temp, FT1000_REG_DOORBELL);
	if (ret)
		return ret;

	commandbuf = kmalloc(size + 2, GFP_KERNEL);
	if (!commandbuf)
		return -ENOMEM;
	memcpy((void *)commandbuf + 2, ptempbuffer, size);

	if (temp & 0x0100)
		usleep_range(900, 1100);

	/* check for odd word */
	size = size + 2;

	/* Must force to be 32 bit aligned */
	if (size % 4)
		size += 4 - (size % 4);

	ret = ft1000_write_dpram32(ft1000dev, 0, commandbuf, size);
	if (ret)
		return ret;
	usleep_range(900, 1100);
	ret = ft1000_write_register(ft1000dev, FT1000_DB_DPRAM_TX,
				    FT1000_REG_DOORBELL);
	if (ret)
		return ret;
	usleep_range(900, 1100);

	ret = ft1000_read_register(ft1000dev, &temp, FT1000_REG_DOORBELL);

#if 0
	if ((temp & 0x0100) == 0)
		pr_debug("Message sent\n");
#endif
	return ret;
}

/* load or reload the DSP */
int dsp_reload(struct ft1000_usb *ft1000dev)
{
	int status;
	u16 tempword;
	u32 templong;

	struct ft1000_info *pft1000info;

	pft1000info = netdev_priv(ft1000dev->net);

	pft1000info->CardReady = 0;

	/* Program Interrupt Mask register */
	status = ft1000_write_register(ft1000dev, 0xffff, FT1000_REG_SUP_IMASK);

	status = ft1000_read_register(ft1000dev, &tempword, FT1000_REG_RESET);
	tempword |= ASIC_RESET_BIT;
	status = ft1000_write_register(ft1000dev, tempword, FT1000_REG_RESET);
	msleep(1000);
	status = ft1000_read_register(ft1000dev, &tempword, FT1000_REG_RESET);
	pr_debug("Reset Register = 0x%x\n", tempword);

	/* Toggle DSP reset */
	card_reset_dsp(ft1000dev, 1);
	msleep(1000);
	card_reset_dsp(ft1000dev, 0);
	msleep(1000);

	status =
		ft1000_write_register(ft1000dev, HOST_INTF_BE, FT1000_REG_SUP_CTRL);

	/* Let's check for FEFE */
	status =
		ft1000_read_dpram32(ft1000dev, FT1000_MAG_DPRAM_FEFE_INDX,
				    (u8 *)&templong, 4);
	pr_debug("templong (fefe) = 0x%8x\n", templong);

	/* call codeloader */
	status = scram_dnldr(ft1000dev, pFileStart, FileLength);

	if (status != 0)
		return -EIO;

	msleep(1000);

	return 0;
}

/* call the Card Service function to reset the ASIC. */
static void ft1000_reset_asic(struct net_device *dev)
{
	struct ft1000_info *info = netdev_priv(dev);
	struct ft1000_usb *ft1000dev = info->priv;
	u16 tempword;

	/* Let's use the register provided by the Magnemite ASIC to reset the
	 * ASIC and DSP.
	 */
	ft1000_write_register(ft1000dev, DSP_RESET_BIT | ASIC_RESET_BIT,
			      FT1000_REG_RESET);

	mdelay(1);

	/* set watermark to -1 in order to not generate an interrupt */
	ft1000_write_register(ft1000dev, 0xffff, FT1000_REG_MAG_WATERMARK);

	/* clear interrupts */
	ft1000_read_register(ft1000dev, &tempword, FT1000_REG_SUP_ISR);
	pr_debug("interrupt status register = 0x%x\n", tempword);
	ft1000_write_register(ft1000dev, tempword, FT1000_REG_SUP_ISR);
	ft1000_read_register(ft1000dev, &tempword, FT1000_REG_SUP_ISR);
	pr_debug("interrupt status register = 0x%x\n", tempword);
}

static int ft1000_reset_card(struct net_device *dev)
{
	struct ft1000_info *info = netdev_priv(dev);
	struct ft1000_usb *ft1000dev = info->priv;
	u16 tempword;
	struct prov_record *ptr;
	struct prov_record *tmp;

	ft1000dev->fCondResetPend = true;
	info->CardReady = 0;
	ft1000dev->fProvComplete = false;

	/* Make sure we free any memory reserve for provisioning */
	list_for_each_entry_safe(ptr, tmp, &info->prov_list, list) {
		pr_debug("deleting provisioning record\n");
		list_del(&ptr->list);
		kfree(ptr->pprov_data);
		kfree(ptr);
	}

	pr_debug("reset asic\n");
	ft1000_reset_asic(dev);

	pr_debug("call dsp_reload\n");
	dsp_reload(ft1000dev);

	pr_debug("dsp reload successful\n");

	mdelay(10);

	/* Initialize DSP heartbeat area */
	ft1000_write_dpram16(ft1000dev, FT1000_MAG_HI_HO, ho_mag,
			     FT1000_MAG_HI_HO_INDX);
	ft1000_read_dpram16(ft1000dev, FT1000_MAG_HI_HO, (u8 *)&tempword,
			    FT1000_MAG_HI_HO_INDX);
	pr_debug("hi_ho value = 0x%x\n", tempword);

	info->CardReady = 1;

	ft1000dev->fCondResetPend = false;

	return TRUE;
}

/* callback function when a urb is transmitted */
static void ft1000_usb_transmit_complete(struct urb *urb)
{

	struct ft1000_usb *ft1000dev = urb->context;

	if (urb->status)
		pr_err("%s: TX status %d\n", ft1000dev->net->name, urb->status);

	netif_wake_queue(ft1000dev->net);
}

/* take an ethernet packet and convert it to a Flarion
 *  packet prior to sending it to the ASIC Downlink FIFO.
 */
static int ft1000_copy_down_pkt(struct net_device *netdev, u8 *packet, u16 len)
{
	struct ft1000_info *pInfo = netdev_priv(netdev);
	struct ft1000_usb *pFt1000Dev = pInfo->priv;

	int count, ret;
	u8 *t;
	struct pseudo_hdr hdr;

	if (!pInfo->CardReady) {
		pr_debug("Card Not Ready\n");
		return -ENODEV;
	}

	count = sizeof(struct pseudo_hdr) + len;
	if (count > MAX_BUF_SIZE) {
		pr_debug("Message Size Overflow! size = %d\n", count);
		return -EINVAL;
	}

	if (count % 4)
		count = count + (4 - (count % 4));

	memset(&hdr, 0, sizeof(struct pseudo_hdr));

	hdr.length = ntohs(count);
	hdr.source = 0x10;
	hdr.destination = 0x20;
	hdr.portdest = 0x20;
	hdr.portsrc = 0x10;
	hdr.sh_str_id = 0x91;
	hdr.control = 0x00;

	hdr.checksum = hdr.length ^ hdr.source ^ hdr.destination ^
		hdr.portdest ^ hdr.portsrc ^ hdr.sh_str_id ^ hdr.control;

	memcpy(&pFt1000Dev->tx_buf[0], &hdr, sizeof(hdr));
	memcpy(&pFt1000Dev->tx_buf[sizeof(struct pseudo_hdr)], packet, len);

	netif_stop_queue(netdev);

	usb_fill_bulk_urb(pFt1000Dev->tx_urb,
			  pFt1000Dev->dev,
			  usb_sndbulkpipe(pFt1000Dev->dev,
					  pFt1000Dev->bulk_out_endpointAddr),
			  pFt1000Dev->tx_buf, count,
			  ft1000_usb_transmit_complete, pFt1000Dev);

	t = (u8 *)pFt1000Dev->tx_urb->transfer_buffer;

	ret = usb_submit_urb(pFt1000Dev->tx_urb, GFP_ATOMIC);

	if (ret) {
		pr_debug("failed tx_urb %d\n", ret);
		return ret;
	}
	pInfo->stats.tx_packets++;
	pInfo->stats.tx_bytes += (len + 14);

	return 0;
}

/* transmit an ethernet packet
 *  Parameters:  skb - socket buffer to be sent
 *               dev - network device
 */
static int ft1000_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct ft1000_info *pInfo = netdev_priv(dev);
	struct ft1000_usb *pFt1000Dev = pInfo->priv;
	u8 *pdata;
	int maxlen, pipe;

	if (skb == NULL) {
		pr_debug("skb == NULL!!!\n");
		return NETDEV_TX_OK;
	}

	if (pFt1000Dev->status & FT1000_STATUS_CLOSING) {
		pr_debug("network driver is closed, return\n");
		goto err;
	}

	pipe =
		usb_sndbulkpipe(pFt1000Dev->dev, pFt1000Dev->bulk_out_endpointAddr);
	maxlen = usb_maxpacket(pFt1000Dev->dev, pipe, usb_pipeout(pipe));

	pdata = (u8 *)skb->data;

	if (pInfo->mediastate == 0) {
		/* Drop packet is mediastate is down */
		pr_debug("mediastate is down\n");
		goto err;
	}

	if ((skb->len < ENET_HEADER_SIZE) || (skb->len > ENET_MAX_SIZE)) {
		/* Drop packet which has invalid size */
		pr_debug("invalid ethernet length\n");
		goto err;
	}

	ft1000_copy_down_pkt(dev, pdata + ENET_HEADER_SIZE - 2,
			     skb->len - ENET_HEADER_SIZE + 2);

err:
	dev_kfree_skb(skb);

	return NETDEV_TX_OK;
}

/* open the network driver */
static int ft1000_open(struct net_device *dev)
{
	struct ft1000_info *pInfo = netdev_priv(dev);
	struct ft1000_usb *pFt1000Dev = pInfo->priv;
	struct timeval tv;

	pr_debug("ft1000_open is called for card %d\n", pFt1000Dev->CardNumber);

	pInfo->stats.rx_bytes = 0;
	pInfo->stats.tx_bytes = 0;
	pInfo->stats.rx_packets = 0;
	pInfo->stats.tx_packets = 0;
	do_gettimeofday(&tv);
	pInfo->ConTm = tv.tv_sec;
	pInfo->ProgConStat = 0;

	netif_start_queue(dev);

	netif_carrier_on(dev);

	return ft1000_submit_rx_urb(pInfo);
}

static struct net_device_stats *ft1000_netdev_stats(struct net_device *dev)
{
	struct ft1000_info *info = netdev_priv(dev);

	return &(info->stats);
}

static const struct net_device_ops ftnet_ops = {
	.ndo_open = &ft1000_open,
	.ndo_stop = &ft1000_close,
	.ndo_start_xmit = &ft1000_start_xmit,
	.ndo_get_stats = &ft1000_netdev_stats,
};

/* initialize the network device */
static int ft1000_reset(void *dev)
{
	ft1000_reset_card(dev);
	return 0;
}

int init_ft1000_netdev(struct ft1000_usb *ft1000dev)
{
	struct net_device *netdev;
	struct ft1000_info *pInfo = NULL;
	struct dpram_blk *pdpram_blk;
	int i, ret_val;
	struct list_head *cur, *tmp;
	char card_nr[2];
	u8 gCardIndex = 0;

	netdev = alloc_etherdev(sizeof(struct ft1000_info));
	if (!netdev) {
		pr_debug("can not allocate network device\n");
		return -ENOMEM;
	}

	pInfo = netdev_priv(netdev);

	memset(pInfo, 0, sizeof(struct ft1000_info));

	dev_alloc_name(netdev, netdev->name);

	pr_debug("network device name is %s\n", netdev->name);

	if (strncmp(netdev->name, "eth", 3) == 0) {
		card_nr[0] = netdev->name[3];
		card_nr[1] = '\0';
		ret_val = kstrtou8(card_nr, 10, &gCardIndex);
		if (ret_val) {
			netdev_err(ft1000dev->net, "Can't parse netdev\n");
			goto err_net;
		}

		ft1000dev->CardNumber = gCardIndex;
		pr_debug("card number = %d\n", ft1000dev->CardNumber);
	} else {
		netdev_err(ft1000dev->net, "ft1000: Invalid device name\n");
		ret_val = -ENXIO;
		goto err_net;
	}

	memset(&pInfo->stats, 0, sizeof(struct net_device_stats));

	spin_lock_init(&pInfo->dpram_lock);
	pInfo->priv = ft1000dev;
	pInfo->DrvErrNum = 0;
	pInfo->registered = 1;
	pInfo->ft1000_reset = ft1000_reset;
	pInfo->mediastate = 0;
	pInfo->fifo_cnt = 0;
	ft1000dev->DeviceCreated = FALSE;
	pInfo->CardReady = 0;
	pInfo->DSP_TIME[0] = 0;
	pInfo->DSP_TIME[1] = 0;
	pInfo->DSP_TIME[2] = 0;
	pInfo->DSP_TIME[3] = 0;
	ft1000dev->fAppMsgPend = false;
	ft1000dev->fCondResetPend = false;
	ft1000dev->usbboot = 0;
	ft1000dev->dspalive = 0;
	memset(&ft1000dev->tempbuf[0], 0, sizeof(ft1000dev->tempbuf));

	INIT_LIST_HEAD(&pInfo->prov_list);

	INIT_LIST_HEAD(&ft1000dev->nodes.list);

	netdev->netdev_ops = &ftnet_ops;

	ft1000dev->net = netdev;

	pr_debug("Initialize free_buff_lock and freercvpool\n");
	spin_lock_init(&free_buff_lock);

	/* initialize a list of buffers to be use for queuing
	 * up receive command data
	 */
	INIT_LIST_HEAD(&freercvpool);

	/* create list of free buffers */
	for (i = 0; i < NUM_OF_FREE_BUFFERS; i++) {
		/* Get memory for DPRAM_DATA link list */
		pdpram_blk = kmalloc(sizeof(struct dpram_blk), GFP_KERNEL);
		if (pdpram_blk == NULL) {
			ret_val = -ENOMEM;
			goto err_free;
		}
		/* Get a block of memory to store command data */
		pdpram_blk->pbuffer = kmalloc(MAX_CMD_SQSIZE, GFP_KERNEL);
		if (pdpram_blk->pbuffer == NULL) {
			ret_val = -ENOMEM;
			kfree(pdpram_blk);
			goto err_free;
		}
		/* link provisioning data */
		list_add_tail(&pdpram_blk->list, &freercvpool);
	}
	numofmsgbuf = NUM_OF_FREE_BUFFERS;

	return 0;

err_free:
	list_for_each_safe(cur, tmp, &freercvpool) {
		pdpram_blk = list_entry(cur, struct dpram_blk, list);
		list_del(&pdpram_blk->list);
		kfree(pdpram_blk->pbuffer);
		kfree(pdpram_blk);
	}
err_net:
	free_netdev(netdev);
	return ret_val;
}

/* register the network driver */
int reg_ft1000_netdev(struct ft1000_usb *ft1000dev,
		      struct usb_interface *intf)
{
	struct net_device *netdev;
	struct ft1000_info *pInfo;
	int rc;

	netdev = ft1000dev->net;
	pInfo = netdev_priv(ft1000dev->net);

	ft1000_read_register(ft1000dev, &pInfo->AsicID, FT1000_REG_ASIC_ID);

	usb_set_intfdata(intf, pInfo);
	SET_NETDEV_DEV(netdev, &intf->dev);

	rc = register_netdev(netdev);
	if (rc) {
		pr_debug("could not register network device\n");
		free_netdev(netdev);
		return rc;
	}

	ft1000_create_dev(ft1000dev);

	pInfo->CardReady = 1;

	return 0;
}

/* take a packet from the FIFO up link and
 *  convert it into an ethernet packet and deliver it to the IP stack
 */
static int ft1000_copy_up_pkt(struct urb *urb)
{
	struct ft1000_info *info = urb->context;
	struct ft1000_usb *ft1000dev = info->priv;
	struct net_device *net = ft1000dev->net;

	u16 tempword;
	u16 len;
	u16 lena;
	struct sk_buff *skb;
	u16 i;
	u8 *pbuffer = NULL;
	u8 *ptemp = NULL;
	u16 *chksum;

	if (ft1000dev->status & FT1000_STATUS_CLOSING) {
		pr_debug("network driver is closed, return\n");
		return 0;
	}
	/* Read length */
	len = urb->transfer_buffer_length;
	lena = urb->actual_length;

	chksum = (u16 *)ft1000dev->rx_buf;

	tempword = *chksum++;
	for (i = 1; i < 7; i++)
		tempword ^= *chksum++;

	if (tempword != *chksum) {
		info->stats.rx_errors++;
		ft1000_submit_rx_urb(info);
		return -1;
	}

	skb = dev_alloc_skb(len + 12 + 2);

	if (skb == NULL) {
		pr_debug("No Network buffers available\n");
		info->stats.rx_errors++;
		ft1000_submit_rx_urb(info);
		return -1;
	}

	pbuffer = (u8 *)skb_put(skb, len + 12);

	/* subtract the number of bytes read already */
	ptemp = pbuffer;

	/* fake MAC address */
	*pbuffer++ = net->dev_addr[0];
	*pbuffer++ = net->dev_addr[1];
	*pbuffer++ = net->dev_addr[2];
	*pbuffer++ = net->dev_addr[3];
	*pbuffer++ = net->dev_addr[4];
	*pbuffer++ = net->dev_addr[5];
	*pbuffer++ = 0x00;
	*pbuffer++ = 0x07;
	*pbuffer++ = 0x35;
	*pbuffer++ = 0xff;
	*pbuffer++ = 0xff;
	*pbuffer++ = 0xfe;

	memcpy(pbuffer, ft1000dev->rx_buf + sizeof(struct pseudo_hdr),
	       len - sizeof(struct pseudo_hdr));

	skb->dev = net;

	skb->protocol = eth_type_trans(skb, net);
	skb->ip_summed = CHECKSUM_UNNECESSARY;
	netif_rx(skb);

	info->stats.rx_packets++;
	/* Add on 12 bytes for MAC address which was removed */
	info->stats.rx_bytes += (lena + 12);

	ft1000_submit_rx_urb(info);

	return 0;
}


/* the receiving function of the network driver */
static int ft1000_submit_rx_urb(struct ft1000_info *info)
{
	int result;
	struct ft1000_usb *pFt1000Dev = info->priv;

	if (pFt1000Dev->status & FT1000_STATUS_CLOSING) {
		pr_debug("network driver is closed, return\n");
		return -ENODEV;
	}

	usb_fill_bulk_urb(pFt1000Dev->rx_urb,
			  pFt1000Dev->dev,
			  usb_rcvbulkpipe(pFt1000Dev->dev,
					  pFt1000Dev->bulk_in_endpointAddr),
			  pFt1000Dev->rx_buf, MAX_BUF_SIZE,
			  (usb_complete_t)ft1000_copy_up_pkt, info);

	result = usb_submit_urb(pFt1000Dev->rx_urb, GFP_ATOMIC);

	if (result) {
		pr_err("submitting rx_urb %d failed\n", result);
		return result;
	}

	return 0;
}

/* close the network driver */
int ft1000_close(struct net_device *net)
{
	struct ft1000_info *pInfo = netdev_priv(net);
	struct ft1000_usb *ft1000dev = pInfo->priv;

	ft1000dev->status |= FT1000_STATUS_CLOSING;

	pr_debug("pInfo=%p, ft1000dev=%p\n", pInfo, ft1000dev);
	netif_carrier_off(net);
	netif_stop_queue(net);
	ft1000dev->status &= ~FT1000_STATUS_CLOSING;

	pInfo->ProgConStat = 0xff;

	return 0;
}

/* check if the device is presently available on the system. */
static int ft1000_chkcard(struct ft1000_usb *dev)
{
	u16 tempword;
	int status;

	if (dev->fCondResetPend) {
		pr_debug("Card is being reset, return FALSE\n");
		return TRUE;
	}
	/* Mask register is used to check for device presence since it is never
	 * set to zero.
	 */
	status = ft1000_read_register(dev, &tempword, FT1000_REG_SUP_IMASK);
	if (tempword == 0) {
		pr_debug("IMASK = 0 Card not detected\n");
		return FALSE;
	}
	/* The system will return the value of 0xffff for the version register
	 * if the device is not present.
	 */
	status = ft1000_read_register(dev, &tempword, FT1000_REG_ASIC_ID);
	if (tempword != 0x1b01) {
		dev->status |= FT1000_STATUS_CLOSING;
		pr_debug("Version = 0xffff Card not detected\n");
		return FALSE;
	}
	return TRUE;
}

/* read a message from the dpram area.
 *  Input:
 *    dev - network device structure
 *    pbuffer - caller supply address to buffer
 */
static bool ft1000_receive_cmd(struct ft1000_usb *dev, u16 *pbuffer,
			       int maxsz)
{
	u16 size;
	int ret;
	u16 *ppseudohdr;
	int i;
	u16 tempword;

	ret =
		ft1000_read_dpram16(dev, FT1000_MAG_PH_LEN, (u8 *)&size,
				    FT1000_MAG_PH_LEN_INDX);
	size = ntohs(size) + PSEUDOSZ;
	if (size > maxsz) {
		pr_debug("Invalid command length = %d\n", size);
		return FALSE;
	}
	ppseudohdr = (u16 *)pbuffer;
	ft1000_write_register(dev, FT1000_DPRAM_MAG_RX_BASE,
			      FT1000_REG_DPRAM_ADDR);
	ret =
		ft1000_read_register(dev, pbuffer, FT1000_REG_MAG_DPDATAH);
	pbuffer++;
	ft1000_write_register(dev, FT1000_DPRAM_MAG_RX_BASE + 1,
			      FT1000_REG_DPRAM_ADDR);
	for (i = 0; i <= (size >> 2); i++) {
		ret =
			ft1000_read_register(dev, pbuffer,
					     FT1000_REG_MAG_DPDATAL);
		pbuffer++;
		ret =
			ft1000_read_register(dev, pbuffer,
					     FT1000_REG_MAG_DPDATAH);
		pbuffer++;
	}
	/* copy odd aligned word */
	ret =
		ft1000_read_register(dev, pbuffer, FT1000_REG_MAG_DPDATAL);

	pbuffer++;
	ret =
		ft1000_read_register(dev, pbuffer, FT1000_REG_MAG_DPDATAH);

	pbuffer++;
	if (size & 0x0001) {
		/* copy odd byte from fifo */
		ret =
			ft1000_read_register(dev, &tempword,
					     FT1000_REG_DPRAM_DATA);
		*pbuffer = ntohs(tempword);
	}
	/* Check if pseudo header checksum is good
	 * Calculate pseudo header checksum
	 */
	tempword = *ppseudohdr++;
	for (i = 1; i < 7; i++)
		tempword ^= *ppseudohdr++;

	if (tempword != *ppseudohdr)
		return FALSE;

	return TRUE;
}

static int ft1000_dsp_prov(void *arg)
{
	struct ft1000_usb *dev = (struct ft1000_usb *)arg;
	struct ft1000_info *info = netdev_priv(dev->net);
	u16 tempword;
	u16 len;
	u16 i = 0;
	struct prov_record *ptr;
	struct pseudo_hdr *ppseudo_hdr;
	u16 *pmsg;
	int status;
	u16 TempShortBuf[256];

	while (list_empty(&info->prov_list) == 0) {
		pr_debug("DSP Provisioning List Entry\n");

		/* Check if doorbell is available */
		pr_debug("check if doorbell is cleared\n");
		status =
			ft1000_read_register(dev, &tempword, FT1000_REG_DOORBELL);
		if (status) {
			pr_debug("ft1000_read_register error\n");
			break;
		}

		while (tempword & FT1000_DB_DPRAM_TX) {
			mdelay(10);
			i++;
			if (i == 10) {
				pr_debug("message drop\n");
				return -1;
			}
			ft1000_read_register(dev, &tempword,
					     FT1000_REG_DOORBELL);
		}

		if (!(tempword & FT1000_DB_DPRAM_TX)) {
			pr_debug("*** Provision Data Sent to DSP\n");

			/* Send provisioning data */
			ptr =
				list_entry(info->prov_list.next, struct prov_record,
					   list);
			len = *(u16 *)ptr->pprov_data;
			len = htons(len);
			len += PSEUDOSZ;

			pmsg = (u16 *)ptr->pprov_data;
			ppseudo_hdr = (struct pseudo_hdr *)pmsg;
			/* Insert slow queue sequence number */
			ppseudo_hdr->seq_num = info->squeseqnum++;
			ppseudo_hdr->portsrc = 0;
			/* Calculate new checksum */
			ppseudo_hdr->checksum = *pmsg++;
			for (i = 1; i < 7; i++)
				ppseudo_hdr->checksum ^= *pmsg++;

			TempShortBuf[0] = 0;
			TempShortBuf[1] = htons(len);
			memcpy(&TempShortBuf[2], ppseudo_hdr, len);

			status =
				ft1000_write_dpram32(dev, 0,
						     (u8 *)&TempShortBuf[0],
						     (unsigned short)(len + 2));
			status =
				ft1000_write_register(dev, FT1000_DB_DPRAM_TX,
						      FT1000_REG_DOORBELL);

			list_del(&ptr->list);
			kfree(ptr->pprov_data);
			kfree(ptr);
		}
		usleep_range(9000, 11000);
	}

	pr_debug("DSP Provisioning List Entry finished\n");

	msleep(100);

	dev->fProvComplete = true;
	info->CardReady = 1;

	return 0;
}

static int ft1000_proc_drvmsg(struct ft1000_usb *dev, u16 size)
{
	struct ft1000_info *info = netdev_priv(dev->net);
	u16 msgtype;
	u16 tempword;
	struct media_msg *pmediamsg;
	struct dsp_init_msg *pdspinitmsg;
	struct drv_msg *pdrvmsg;
	u16 i;
	struct pseudo_hdr *ppseudo_hdr;
	u16 *pmsg;
	int status;
	union {
		u8 byte[2];
		u16 wrd;
	} convert;

	char *cmdbuffer = kmalloc(1600, GFP_KERNEL);

	if (!cmdbuffer)
		return -ENOMEM;

	status = ft1000_read_dpram32(dev, 0x200, cmdbuffer, size);

#ifdef JDEBUG
	print_hex_dump_debug("cmdbuffer: ", HEX_DUMP_OFFSET, 16, 1,
			     cmdbuffer, size, true);
#endif
	pdrvmsg = (struct drv_msg *)&cmdbuffer[2];
	msgtype = ntohs(pdrvmsg->type);
	pr_debug("Command message type = 0x%x\n", msgtype);
	switch (msgtype) {
	case MEDIA_STATE:{
		pr_debug("Command message type = MEDIA_STATE\n");
		pmediamsg = (struct media_msg *)&cmdbuffer[0];
		if (info->ProgConStat != 0xFF) {
			if (pmediamsg->state) {
				pr_debug("Media is up\n");
				if (info->mediastate == 0) {
					if (dev->NetDevRegDone)
						netif_wake_queue(dev->net);
					info->mediastate = 1;
				}
			} else {
				pr_debug("Media is down\n");
				if (info->mediastate == 1) {
					info->mediastate = 0;
					if (dev->NetDevRegDone)
						info->ConTm = 0;
				}
			}
		} else {
			pr_debug("Media is down\n");
			if (info->mediastate == 1) {
				info->mediastate = 0;
				info->ConTm = 0;
			}
		}
		break;
	}
	case DSP_INIT_MSG:{
		pr_debug("Command message type = DSP_INIT_MSG\n");
		pdspinitmsg = (struct dsp_init_msg *)&cmdbuffer[2];
		memcpy(info->DspVer, pdspinitmsg->DspVer, DSPVERSZ);
		pr_debug("DSPVER = 0x%2x 0x%2x 0x%2x 0x%2x\n",
			 info->DspVer[0], info->DspVer[1], info->DspVer[2],
			 info->DspVer[3]);
		memcpy(info->HwSerNum, pdspinitmsg->HwSerNum,
		       HWSERNUMSZ);
		memcpy(info->Sku, pdspinitmsg->Sku, SKUSZ);
		memcpy(info->eui64, pdspinitmsg->eui64, EUISZ);
		pr_debug("EUI64=%2x.%2x.%2x.%2x.%2x.%2x.%2x.%2x\n",
			 info->eui64[0], info->eui64[1], info->eui64[2],
			 info->eui64[3], info->eui64[4], info->eui64[5],
			 info->eui64[6], info->eui64[7]);
		dev->net->dev_addr[0] = info->eui64[0];
		dev->net->dev_addr[1] = info->eui64[1];
		dev->net->dev_addr[2] = info->eui64[2];
		dev->net->dev_addr[3] = info->eui64[5];
		dev->net->dev_addr[4] = info->eui64[6];
		dev->net->dev_addr[5] = info->eui64[7];

		if (ntohs(pdspinitmsg->length) ==
		    (sizeof(struct dsp_init_msg) - 20)) {
			memcpy(info->ProductMode, pdspinitmsg->ProductMode,
			       MODESZ);
			memcpy(info->RfCalVer, pdspinitmsg->RfCalVer, CALVERSZ);
			memcpy(info->RfCalDate, pdspinitmsg->RfCalDate,
			       CALDATESZ);
			pr_debug("RFCalVer = 0x%2x 0x%2x\n",
				 info->RfCalVer[0], info->RfCalVer[1]);
		}
		break;
	}
	case DSP_PROVISION:{
		pr_debug("Command message type = DSP_PROVISION\n");

		/* kick off dspprov routine to start provisioning
		 * Send provisioning data to DSP
		 */
		if (list_empty(&info->prov_list) == 0) {
			dev->fProvComplete = false;
			status = ft1000_dsp_prov(dev);
			if (status != 0)
				goto out;
		} else {
			dev->fProvComplete = true;
			status = ft1000_write_register(dev, FT1000_DB_HB,
						       FT1000_REG_DOORBELL);
			pr_debug("No more DSP provisioning data in dsp image\n");
		}
		pr_debug("DSP PROVISION is done\n");
		break;
	}
	case DSP_STORE_INFO:{
		pr_debug("Command message type = DSP_STORE_INFO");
		tempword = ntohs(pdrvmsg->length);
		info->DSPInfoBlklen = tempword;
		if (tempword < (MAX_DSP_SESS_REC - 4)) {
			pmsg = (u16 *)&pdrvmsg->data[0];
			for (i = 0; i < ((tempword + 1) / 2); i++) {
				pr_debug("dsp info data = 0x%x\n", *pmsg);
				info->DSPInfoBlk[i + 10] = *pmsg++;
			}
		} else {
			info->DSPInfoBlklen = 0;
		}
		break;
	}
	case DSP_GET_INFO:{
		pr_debug("Got DSP_GET_INFO\n");
		/* copy dsp info block to dsp */
		dev->DrvMsgPend = 1;
		/* allow any outstanding ioctl to finish */
		mdelay(10);
		status = ft1000_read_register(dev, &tempword,
					      FT1000_REG_DOORBELL);
		if (tempword & FT1000_DB_DPRAM_TX) {
			mdelay(10);
			status = ft1000_read_register(dev, &tempword,
						      FT1000_REG_DOORBELL);
			if (tempword & FT1000_DB_DPRAM_TX) {
				mdelay(10);
				status = ft1000_read_register(dev, &tempword,
							      FT1000_REG_DOORBELL);
				if (tempword & FT1000_DB_DPRAM_TX)
					break;
			}
		}
		/* Put message into Slow Queue Form Pseudo header */
		pmsg = (u16 *)info->DSPInfoBlk;
		*pmsg++ = 0;
		*pmsg++ = htons(info->DSPInfoBlklen + 20 + info->DSPInfoBlklen);
		ppseudo_hdr =
			(struct pseudo_hdr *)(u16 *)&info->DSPInfoBlk[2];
		ppseudo_hdr->length = htons(info->DSPInfoBlklen + 4
					    + info->DSPInfoBlklen);
		ppseudo_hdr->source = 0x10;
		ppseudo_hdr->destination = 0x20;
		ppseudo_hdr->portdest = 0;
		ppseudo_hdr->portsrc = 0;
		ppseudo_hdr->sh_str_id = 0;
		ppseudo_hdr->control = 0;
		ppseudo_hdr->rsvd1 = 0;
		ppseudo_hdr->rsvd2 = 0;
		ppseudo_hdr->qos_class = 0;
		/* Insert slow queue sequence number */
		ppseudo_hdr->seq_num = info->squeseqnum++;
		/* Insert application id */
		ppseudo_hdr->portsrc = 0;
		/* Calculate new checksum */
		ppseudo_hdr->checksum = *pmsg++;
		for (i = 1; i < 7; i++)
			ppseudo_hdr->checksum ^= *pmsg++;

		info->DSPInfoBlk[10] = 0x7200;
		info->DSPInfoBlk[11] = htons(info->DSPInfoBlklen);
		status = ft1000_write_dpram32(dev, 0,
					      (u8 *)&info->DSPInfoBlk[0],
					      (unsigned short)(info->DSPInfoBlklen + 22));
		status = ft1000_write_register(dev, FT1000_DB_DPRAM_TX,
					       FT1000_REG_DOORBELL);
		dev->DrvMsgPend = 0;
		break;
	}
	case GET_DRV_ERR_RPT_MSG:{
		pr_debug("Got GET_DRV_ERR_RPT_MSG\n");
		/* copy driver error message to dsp */
		dev->DrvMsgPend = 1;
		/* allow any outstanding ioctl to finish */
		mdelay(10);
		status = ft1000_read_register(dev, &tempword,
					      FT1000_REG_DOORBELL);
		if (tempword & FT1000_DB_DPRAM_TX) {
			mdelay(10);
			status = ft1000_read_register(dev, &tempword,
						      FT1000_REG_DOORBELL);
			if (tempword & FT1000_DB_DPRAM_TX)
				mdelay(10);
		}
		if ((tempword & FT1000_DB_DPRAM_TX) == 0) {
			/* Put message into Slow Queue Form Pseudo header */
			pmsg = (u16 *)&tempbuffer[0];
			ppseudo_hdr = (struct pseudo_hdr *)pmsg;
			ppseudo_hdr->length = htons(0x0012);
			ppseudo_hdr->source = 0x10;
			ppseudo_hdr->destination = 0x20;
			ppseudo_hdr->portdest = 0;
			ppseudo_hdr->portsrc = 0;
			ppseudo_hdr->sh_str_id = 0;
			ppseudo_hdr->control = 0;
			ppseudo_hdr->rsvd1 = 0;
			ppseudo_hdr->rsvd2 = 0;
			ppseudo_hdr->qos_class = 0;
			/* Insert slow queue sequence number */
			ppseudo_hdr->seq_num = info->squeseqnum++;
			/* Insert application id */
			ppseudo_hdr->portsrc = 0;
			/* Calculate new checksum */
			ppseudo_hdr->checksum = *pmsg++;
			for (i = 1; i < 7; i++)
				ppseudo_hdr->checksum ^= *pmsg++;

			pmsg = (u16 *)&tempbuffer[16];
			*pmsg++ = htons(RSP_DRV_ERR_RPT_MSG);
			*pmsg++ = htons(0x000e);
			*pmsg++ = htons(info->DSP_TIME[0]);
			*pmsg++ = htons(info->DSP_TIME[1]);
			*pmsg++ = htons(info->DSP_TIME[2]);
			*pmsg++ = htons(info->DSP_TIME[3]);
			convert.byte[0] = info->DspVer[0];
			convert.byte[1] = info->DspVer[1];
			*pmsg++ = convert.wrd;
			convert.byte[0] = info->DspVer[2];
			convert.byte[1] = info->DspVer[3];
			*pmsg++ = convert.wrd;
			*pmsg++ = htons(info->DrvErrNum);

			status = card_send_command(dev, (unsigned char *)&tempbuffer[0],
						   (u16)(0x0012 + PSEUDOSZ));
			if (status)
				goto out;
			info->DrvErrNum = 0;
		}
		dev->DrvMsgPend = 0;
		break;
	}
	default:
		break;
	}

	status = 0;
out:
	kfree(cmdbuffer);
	return status;
}

/* Check which application has registered for dsp broadcast messages */
static int dsp_broadcast_msg_id(struct ft1000_usb *dev)
{
	struct dpram_blk *pdpram_blk;
	unsigned long flags;
	int i;

	for (i = 0; i < MAX_NUM_APP; i++) {
		if ((dev->app_info[i].DspBCMsgFlag)
		    && (dev->app_info[i].fileobject)
		    && (dev->app_info[i].NumOfMsg
			< MAX_MSG_LIMIT)) {
			pdpram_blk = ft1000_get_buffer(&freercvpool);
			if (pdpram_blk == NULL) {
				pr_debug("Out of memory in free receive command pool\n");
				dev->app_info[i].nRxMsgMiss++;
				return -1;
			}
			if (ft1000_receive_cmd(dev, pdpram_blk->pbuffer,
					       MAX_CMD_SQSIZE)) {
				/* Put message into the
				 * appropriate application block
				 */
				dev->app_info[i].nRxMsg++;
				spin_lock_irqsave(&free_buff_lock, flags);
				list_add_tail(&pdpram_blk->list,
					      &dev->app_info[i] .app_sqlist);
				dev->app_info[i].NumOfMsg++;
				spin_unlock_irqrestore(&free_buff_lock, flags);
				wake_up_interruptible(&dev->app_info[i]
						      .wait_dpram_msg);
			} else {
				dev->app_info[i].nRxMsgMiss++;
				ft1000_free_buffer(pdpram_blk, &freercvpool);
				pr_debug("ft1000_get_buffer NULL\n");
				return -1;
			}
		}
	}
	return 0;
}

static int handle_misc_portid(struct ft1000_usb *dev)
{
	struct dpram_blk *pdpram_blk;
	int i;

	pdpram_blk = ft1000_get_buffer(&freercvpool);
	if (pdpram_blk == NULL) {
		pr_debug("Out of memory in free receive command pool\n");
		return -1;
	}
	if (!ft1000_receive_cmd(dev, pdpram_blk->pbuffer, MAX_CMD_SQSIZE))
		goto exit_failure;

	/* Search for correct application block */
	for (i = 0; i < MAX_NUM_APP; i++) {
		if (dev->app_info[i].app_id == ((struct pseudo_hdr *)
						pdpram_blk->pbuffer)->portdest)
			break;
	}
	if (i == MAX_NUM_APP) {
		pr_debug("No application matching id = %d\n",
			 ((struct pseudo_hdr *)pdpram_blk->pbuffer)->portdest);
		goto exit_failure;
	} else if (dev->app_info[i].NumOfMsg > MAX_MSG_LIMIT) {
		goto exit_failure;
	} else {
		dev->app_info[i].nRxMsg++;
		/* Put message into the appropriate application block */
		list_add_tail(&pdpram_blk->list, &dev->app_info[i].app_sqlist);
		dev->app_info[i].NumOfMsg++;
	}
	return 0;

exit_failure:
	ft1000_free_buffer(pdpram_blk, &freercvpool);
	return -1;
}

int ft1000_poll(void *dev_id)
{
	struct ft1000_usb *dev = (struct ft1000_usb *)dev_id;
	struct ft1000_info *info = netdev_priv(dev->net);
	u16 tempword;
	int status;
	u16 size;
	int i;
	u16 data;
	u16 modulo;
	u16 portid;

	if (ft1000_chkcard(dev) == FALSE) {
		pr_debug("failed\n");
		return -1;
	}
	status = ft1000_read_register(dev, &tempword, FT1000_REG_DOORBELL);
	if (!status) {
		if (tempword & FT1000_DB_DPRAM_RX) {
			status = ft1000_read_dpram16(dev,
						     0x200, (u8 *)&data, 0);
			size = ntohs(data) + 16 + 2;
			if (size % 4) {
				modulo = 4 - (size % 4);
				size = size + modulo;
			}
			status = ft1000_read_dpram16(dev, 0x201,
						     (u8 *)&portid, 1);
			portid &= 0xff;
			if (size < MAX_CMD_SQSIZE) {
				switch (portid) {
				case DRIVERID:
					pr_debug("FT1000_REG_DOORBELL message type: FT1000_DB_DPRAM_RX : portid DRIVERID\n");
					status = ft1000_proc_drvmsg(dev, size);
					if (status != 0)
						return status;
					break;
				case DSPBCMSGID:
					status = dsp_broadcast_msg_id(dev);
					break;
				default:
					status = handle_misc_portid(dev);
					break;
				}
			} else
				pr_debug("Invalid total length for SlowQ = %d\n",
					 size);
			status = ft1000_write_register(dev,
						       FT1000_DB_DPRAM_RX,
						       FT1000_REG_DOORBELL);
		} else if (tempword & FT1000_DSP_ASIC_RESET) {
			/* Let's reset the ASIC from the Host side as well */
			status = ft1000_write_register(dev, ASIC_RESET_BIT,
						       FT1000_REG_RESET);
			status = ft1000_read_register(dev, &tempword,
						      FT1000_REG_RESET);
			i = 0;
			while (tempword & ASIC_RESET_BIT) {
				status = ft1000_read_register(dev, &tempword,
							      FT1000_REG_RESET);
				usleep_range(9000, 11000);
				i++;
				if (i == 100)
					break;
			}
			if (i == 100) {
				pr_debug("Unable to reset ASIC\n");
				return 0;
			}
			usleep_range(9000, 11000);
			/* Program WMARK register */
			status = ft1000_write_register(dev, 0x600,
						       FT1000_REG_MAG_WATERMARK);
			/* clear ASIC reset doorbell */
			status = ft1000_write_register(dev,
						       FT1000_DSP_ASIC_RESET,
						       FT1000_REG_DOORBELL);
			usleep_range(9000, 11000);
		} else if (tempword & FT1000_ASIC_RESET_REQ) {
			pr_debug("FT1000_REG_DOORBELL message type: FT1000_ASIC_RESET_REQ\n");
			/* clear ASIC reset request from DSP */
			status = ft1000_write_register(dev,
						       FT1000_ASIC_RESET_REQ,
						       FT1000_REG_DOORBELL);
			status = ft1000_write_register(dev, HOST_INTF_BE,
						       FT1000_REG_SUP_CTRL);
			/* copy dsp session record from Adapter block */
			status = ft1000_write_dpram32(dev, 0,
						      (u8 *)&info->DSPSess.Rec[0], 1024);
			status = ft1000_write_register(dev, 0x600,
						       FT1000_REG_MAG_WATERMARK);
			/* ring doorbell to tell DSP that
			 * ASIC is out of reset
			 * */
			status = ft1000_write_register(dev,
						       FT1000_ASIC_RESET_DSP,
						       FT1000_REG_DOORBELL);
		} else if (tempword & FT1000_DB_COND_RESET) {
			pr_debug("FT1000_REG_DOORBELL message type: FT1000_DB_COND_RESET\n");
			if (!dev->fAppMsgPend) {
				/* Reset ASIC and DSP */
				status = ft1000_read_dpram16(dev,
							     FT1000_MAG_DSP_TIMER0,
							     (u8 *)&info->DSP_TIME[0],
							     FT1000_MAG_DSP_TIMER0_INDX);
				status = ft1000_read_dpram16(dev,
							     FT1000_MAG_DSP_TIMER1,
							     (u8 *)&info->DSP_TIME[1],
							     FT1000_MAG_DSP_TIMER1_INDX);
				status = ft1000_read_dpram16(dev,
							     FT1000_MAG_DSP_TIMER2,
							     (u8 *)&info->DSP_TIME[2],
							     FT1000_MAG_DSP_TIMER2_INDX);
				status = ft1000_read_dpram16(dev,
							     FT1000_MAG_DSP_TIMER3,
							     (u8 *)&info->DSP_TIME[3],
							     FT1000_MAG_DSP_TIMER3_INDX);
				info->CardReady = 0;
				info->DrvErrNum = DSP_CONDRESET_INFO;
				pr_debug("DSP conditional reset requested\n");
				info->ft1000_reset(dev->net);
			} else {
				dev->fProvComplete = false;
				dev->fCondResetPend = true;
			}
			ft1000_write_register(dev, FT1000_DB_COND_RESET,
					      FT1000_REG_DOORBELL);
		}
	}
	return 0;
}

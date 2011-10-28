//=====================================================
// CopyRight (C) 2007 Qualcomm Inc. All Rights Reserved.
//
//
// This file is part of Express Card USB Driver
//
// $Id:
//====================================================
#include <linux/init.h>
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

//#define JDEBUG

static int ft1000_reset(struct net_device *ft1000dev);
static int ft1000_submit_rx_urb(struct ft1000_info *info);
static int ft1000_start_xmit(struct sk_buff *skb, struct net_device *dev);
static int ft1000_open (struct net_device *dev);
static struct net_device_stats *ft1000_netdev_stats(struct net_device *dev);
static int ft1000_chkcard (struct ft1000_device *dev);

static u8 tempbuffer[1600];

#define MAX_RCV_LOOP   100

//---------------------------------------------------------------------------
// Function:    ft1000_control
//
// Parameters:  ft1000_device  - device structure
//              pipe - usb control message pipe
//              request - control request
//              requesttype - control message request type
//              value - value to be written or 0
//              index - register index
//              data - data buffer to hold the read/write values
//              size - data size
//              timeout - control message time out value
//
// Returns:     STATUS_SUCCESS - success
//              STATUS_FAILURE - failure
//
// Description: This function sends a control message via USB interface synchronously
//
// Notes:
//
//---------------------------------------------------------------------------
static int ft1000_control(struct ft1000_device *ft1000dev, unsigned int pipe,
			  u8 request, u8 requesttype, u16 value, u16 index,
			  void *data, u16 size, int timeout)
{
	u16 ret;

	if ((ft1000dev == NULL) || (ft1000dev->dev == NULL)) {
		DEBUG("ft1000dev or ft1000dev->dev == NULL, failure\n");
		return -ENODEV;
	}

	ret = usb_control_msg(ft1000dev->dev, pipe, request, requesttype,
			      value, index, data, size, LARGE_TIMEOUT);

	if (ret > 0)
		ret = 0;

	return ret;
}

//---------------------------------------------------------------------------
// Function:    ft1000_read_register
//
// Parameters:  ft1000_device  - device structure
//              Data - data buffer to hold the value read
//              nRegIndex - register index
//
// Returns:     STATUS_SUCCESS - success
//              STATUS_FAILURE - failure
//
// Description: This function returns the value in a register
//
// Notes:
//
//---------------------------------------------------------------------------

int ft1000_read_register(struct ft1000_device *ft1000dev, u16* Data,
			 u16 nRegIndx)
{
	int ret = STATUS_SUCCESS;

	ret = ft1000_control(ft1000dev,
			     usb_rcvctrlpipe(ft1000dev->dev, 0),
			     HARLEY_READ_REGISTER,
			     HARLEY_READ_OPERATION,
			     0,
			     nRegIndx,
			     Data,
			     2,
			     LARGE_TIMEOUT);

	return ret;
}

//---------------------------------------------------------------------------
// Function:    ft1000_write_register
//
// Parameters:  ft1000_device  - device structure
//              value - value to write into a register
//              nRegIndex - register index
//
// Returns:     STATUS_SUCCESS - success
//              STATUS_FAILURE - failure
//
// Description: This function writes the value in a register
//
// Notes:
//
//---------------------------------------------------------------------------
int ft1000_write_register(struct ft1000_device *ft1000dev, u16 value,
			  u16 nRegIndx)
{
	int ret = STATUS_SUCCESS;

	ret = ft1000_control(ft1000dev,
			     usb_sndctrlpipe(ft1000dev->dev, 0),
			     HARLEY_WRITE_REGISTER,
			     HARLEY_WRITE_OPERATION,
			     value,
			     nRegIndx,
			     NULL,
			     0,
			     LARGE_TIMEOUT);

	return ret;
}

//---------------------------------------------------------------------------
// Function:    ft1000_read_dpram32
//
// Parameters:  ft1000_device  - device structure
//              indx - starting address to read
//              buffer - data buffer to hold the data read
//              cnt - number of byte read from DPRAM
//
// Returns:     STATUS_SUCCESS - success
//              STATUS_FAILURE - failure
//
// Description: This function read a number of bytes from DPRAM
//
// Notes:
//
//---------------------------------------------------------------------------

int ft1000_read_dpram32(struct ft1000_device *ft1000dev, u16 indx, u8 *buffer,
			u16 cnt)
{
	int ret = STATUS_SUCCESS;

	ret = ft1000_control(ft1000dev,
			     usb_rcvctrlpipe(ft1000dev->dev, 0),
			     HARLEY_READ_DPRAM_32,
			     HARLEY_READ_OPERATION,
			     0,
			     indx,
			     buffer,
			     cnt,
			     LARGE_TIMEOUT);

	return ret;
}

//---------------------------------------------------------------------------
// Function:    ft1000_write_dpram32
//
// Parameters:  ft1000_device  - device structure
//              indx - starting address to write the data
//              buffer - data buffer to write into DPRAM
//              cnt - number of bytes to write
//
// Returns:     STATUS_SUCCESS - success
//              STATUS_FAILURE - failure
//
// Description: This function writes into DPRAM a number of bytes
//
// Notes:
//
//---------------------------------------------------------------------------
int ft1000_write_dpram32(struct ft1000_device *ft1000dev, u16 indx, u8 *buffer,
			 u16 cnt)
{
	int ret = STATUS_SUCCESS;

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
			     LARGE_TIMEOUT);

	return ret;
}

//---------------------------------------------------------------------------
// Function:    ft1000_read_dpram16
//
// Parameters:  ft1000_device  - device structure
//              indx - starting address to read
//              buffer - data buffer to hold the data read
//              hightlow - high or low 16 bit word
//
// Returns:     STATUS_SUCCESS - success
//              STATUS_FAILURE - failure
//
// Description: This function read 16 bits from DPRAM
//
// Notes:
//
//---------------------------------------------------------------------------
int ft1000_read_dpram16(struct ft1000_device *ft1000dev, u16 indx, u8 *buffer,
			u8 highlow)
{
	int ret = STATUS_SUCCESS;
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
			     LARGE_TIMEOUT);

	return ret;
}

//---------------------------------------------------------------------------
// Function:    ft1000_write_dpram16
//
// Parameters:  ft1000_device  - device structure
//              indx - starting address to write the data
//              value - 16bits value to write
//              hightlow - high or low 16 bit word
//
// Returns:     STATUS_SUCCESS - success
//              STATUS_FAILURE - failure
//
// Description: This function writes into DPRAM a number of bytes
//
// Notes:
//
//---------------------------------------------------------------------------
int ft1000_write_dpram16(struct ft1000_device *ft1000dev, u16 indx, u16 value, u8 highlow)
{
	int ret = STATUS_SUCCESS;
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
			     LARGE_TIMEOUT);

	return ret;
}

//---------------------------------------------------------------------------
// Function:    fix_ft1000_read_dpram32
//
// Parameters:  ft1000_device  - device structure
//              indx - starting address to read
//              buffer - data buffer to hold the data read
//
//
// Returns:     STATUS_SUCCESS - success
//              STATUS_FAILURE - failure
//
// Description: This function read DPRAM 4 words at a time
//
// Notes:
//
//---------------------------------------------------------------------------
int fix_ft1000_read_dpram32(struct ft1000_device *ft1000dev, u16 indx,
			    u8 *buffer)
{
	u8 buf[16];
	u16 pos;
	int ret = STATUS_SUCCESS;

	pos = (indx / 4) * 4;
	ret = ft1000_read_dpram32(ft1000dev, pos, buf, 16);

	if (ret == STATUS_SUCCESS) {
		pos = (indx % 4) * 4;
		*buffer++ = buf[pos++];
		*buffer++ = buf[pos++];
		*buffer++ = buf[pos++];
		*buffer++ = buf[pos++];
	} else {
		DEBUG("fix_ft1000_read_dpram32: DPRAM32 Read failed\n");
		*buffer++ = 0;
		*buffer++ = 0;
		*buffer++ = 0;
		*buffer++ = 0;
	}

	return ret;
}


//---------------------------------------------------------------------------
// Function:    fix_ft1000_write_dpram32
//
// Parameters:  ft1000_device  - device structure
//              indx - starting address to write
//              buffer - data buffer to write
//
//
// Returns:     STATUS_SUCCESS - success
//              STATUS_FAILURE - failure
//
// Description: This function write to DPRAM 4 words at a time
//
// Notes:
//
//---------------------------------------------------------------------------
int fix_ft1000_write_dpram32(struct ft1000_device *ft1000dev, u16 indx, u8 *buffer)
{
	u16 pos1;
	u16 pos2;
	u16 i;
	u8 buf[32];
	u8 resultbuffer[32];
	u8 *pdata;
	int ret  = STATUS_SUCCESS;

	pos1 = (indx / 4) * 4;
	pdata = buffer;
	ret = ft1000_read_dpram32(ft1000dev, pos1, buf, 16);

	if (ret == STATUS_SUCCESS) {
		pos2 = (indx % 4)*4;
		buf[pos2++] = *buffer++;
		buf[pos2++] = *buffer++;
		buf[pos2++] = *buffer++;
		buf[pos2++] = *buffer++;
		ret = ft1000_write_dpram32(ft1000dev, pos1, buf, 16);
	} else {
		DEBUG("fix_ft1000_write_dpram32: DPRAM32 Read failed\n");
		return ret;
	}

	ret = ft1000_read_dpram32(ft1000dev, pos1, (u8 *)&resultbuffer[0], 16);

	if (ret == STATUS_SUCCESS) {
		buffer = pdata;
		for (i = 0; i < 16; i++) {
			if (buf[i] != resultbuffer[i])
				ret = STATUS_FAILURE;
		}
	}

	if (ret == STATUS_FAILURE) {
		ret = ft1000_write_dpram32(ft1000dev, pos1,
					   (u8 *)&tempbuffer[0], 16);
		ret = ft1000_read_dpram32(ft1000dev, pos1,
					  (u8 *)&resultbuffer[0], 16);
		if (ret == STATUS_SUCCESS) {
			buffer = pdata;
			for (i = 0; i < 16; i++) {
				if (tempbuffer[i] != resultbuffer[i]) {
					ret = STATUS_FAILURE;
					DEBUG("%s Failed to write\n",
					      __func__);
				}
			}
		}
	}

	return ret;
}


//------------------------------------------------------------------------
//
//  Function:   card_reset_dsp
//
//  Synopsis:   This function is called to reset or activate the DSP
//
//  Arguments:  value                  - reset or activate
//
//  Returns:    None
//-----------------------------------------------------------------------
static void card_reset_dsp(struct ft1000_device *ft1000dev, bool value)
{
	u16 status = STATUS_SUCCESS;
	u16 tempword;

	status = ft1000_write_register(ft1000dev, HOST_INTF_BE,
					FT1000_REG_SUP_CTRL);
	status = ft1000_read_register(ft1000dev, &tempword,
				      FT1000_REG_SUP_CTRL);

	if (value) {
		DEBUG("Reset DSP\n");
		status = ft1000_read_register(ft1000dev, &tempword,
					      FT1000_REG_RESET);
		tempword |= DSP_RESET_BIT;
		status = ft1000_write_register(ft1000dev, tempword,
					       FT1000_REG_RESET);
	} else {
		DEBUG("Activate DSP\n");
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

//---------------------------------------------------------------------------
// Function:    card_send_command
//
// Parameters:  ft1000_device  - device structure
//              ptempbuffer - command buffer
//              size - command buffer size
//
// Returns:     STATUS_SUCCESS - success
//              STATUS_FAILURE - failure
//
// Description: This function sends a command to ASIC
//
// Notes:
//
//---------------------------------------------------------------------------
void card_send_command(struct ft1000_device *ft1000dev, void *ptempbuffer,
		       int size)
{
	unsigned short temp;
	unsigned char *commandbuf;

	DEBUG("card_send_command: enter card_send_command... size=%d\n", size);

	commandbuf = (unsigned char *)kmalloc(size + 2, GFP_KERNEL);
	memcpy((void *)commandbuf + 2, (void *)ptempbuffer, size);

	ft1000_read_register(ft1000dev, &temp, FT1000_REG_DOORBELL);

	if (temp & 0x0100)
		msleep(10);

	/* check for odd word */
	size = size + 2;

	/* Must force to be 32 bit aligned */
	if (size % 4)
		size += 4 - (size % 4);

	ft1000_write_dpram32(ft1000dev, 0, commandbuf, size);
	msleep(1);
	ft1000_write_register(ft1000dev, FT1000_DB_DPRAM_TX,
			      FT1000_REG_DOORBELL);
	msleep(1);

	ft1000_read_register(ft1000dev, &temp, FT1000_REG_DOORBELL);

	if ((temp & 0x0100) == 0) {
		//DEBUG("card_send_command: Message sent\n");
	}

}

//--------------------------------------------------------------------------
//
//  Function:   dsp_reload
//
//  Synopsis:   This function is called to load or reload the DSP
//
//  Arguments:  ft1000dev - device structure
//
//  Returns:    None
//-----------------------------------------------------------------------
int dsp_reload(struct ft1000_device *ft1000dev)
{
	u16 status;
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
	DEBUG("Reset Register = 0x%x\n", tempword);

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
				(u8 *) &templong, 4);
	DEBUG("templong (fefe) = 0x%8x\n", templong);

	/* call codeloader */
	status = scram_dnldr(ft1000dev, pFileStart, FileLength);

	if (status != STATUS_SUCCESS)
		return -EIO;

	msleep(1000);

	DEBUG("dsp_reload returned\n");

	return 0;
}

//---------------------------------------------------------------------------
//
// Function:   ft1000_reset_asic
// Description: This function will call the Card Service function to reset the
//             ASIC.
// Input:
//     dev    - device structure
// Output:
//     none
//
//---------------------------------------------------------------------------
static void ft1000_reset_asic(struct net_device *dev)
{
	struct ft1000_info *info = netdev_priv(dev);
	struct ft1000_device *ft1000dev = info->pFt1000Dev;
	u16 tempword;

	DEBUG("ft1000_hw:ft1000_reset_asic called\n");

	/* Let's use the register provided by the Magnemite ASIC to reset the
	 * ASIC and DSP.
	 */
	ft1000_write_register(ft1000dev, (DSP_RESET_BIT | ASIC_RESET_BIT),
			      FT1000_REG_RESET);

	mdelay(1);

	/* set watermark to -1 in order to not generate an interrrupt */
	ft1000_write_register(ft1000dev, 0xffff, FT1000_REG_MAG_WATERMARK);

	/* clear interrupts */
	ft1000_read_register(ft1000dev, &tempword, FT1000_REG_SUP_ISR);
	DEBUG("ft1000_hw: interrupt status register = 0x%x\n", tempword);
	ft1000_write_register(ft1000dev, tempword, FT1000_REG_SUP_ISR);
	ft1000_read_register(ft1000dev, &tempword, FT1000_REG_SUP_ISR);
	DEBUG("ft1000_hw: interrupt status register = 0x%x\n", tempword);
}


//---------------------------------------------------------------------------
//
// Function:   ft1000_reset_card
// Description: This function will reset the card
// Input:
//     dev    - device structure
// Output:
//     status - FALSE (card reset fail)
//              TRUE  (card reset successful)
//
//---------------------------------------------------------------------------
static int ft1000_reset_card(struct net_device *dev)
{
	struct ft1000_info *info = netdev_priv(dev);
	struct ft1000_device *ft1000dev = info->pFt1000Dev;
	u16 tempword;
	struct prov_record *ptr;

	DEBUG("ft1000_hw:ft1000_reset_card called.....\n");

	info->fCondResetPend = 1;
	info->CardReady = 0;
	info->fProvComplete = 0;

	/* Make sure we free any memory reserve for provisioning */
	while (list_empty(&info->prov_list) == 0) {
		DEBUG("ft1000_reset_card:deleting provisioning record\n");
		ptr =
		    list_entry(info->prov_list.next, struct prov_record, list);
		list_del(&ptr->list);
		kfree(ptr->pprov_data);
		kfree(ptr);
	}

	DEBUG("ft1000_hw:ft1000_reset_card: reset asic\n");
	ft1000_reset_asic(dev);

	DEBUG("ft1000_hw:ft1000_reset_card: call dsp_reload\n");
	dsp_reload(ft1000dev);

	DEBUG("dsp reload successful\n");

	mdelay(10);

	/* Initialize DSP heartbeat area */
	ft1000_write_dpram16(ft1000dev, FT1000_MAG_HI_HO, ho_mag,
			     FT1000_MAG_HI_HO_INDX);
	ft1000_read_dpram16(ft1000dev, FT1000_MAG_HI_HO, (u8 *) &tempword,
			    FT1000_MAG_HI_HO_INDX);
	DEBUG("ft1000_hw:ft1000_reset_card:hi_ho value = 0x%x\n", tempword);

	info->CardReady = 1;

	info->fCondResetPend = 0;

	return TRUE;
}

#ifdef HAVE_NET_DEVICE_OPS
static const struct net_device_ops ftnet_ops =
{
	.ndo_open = &ft1000_open,
	.ndo_stop = &ft1000_close,
	.ndo_start_xmit = &ft1000_start_xmit,
	.ndo_get_stats = &ft1000_netdev_stats,
};
#endif


//---------------------------------------------------------------------------
// Function:    init_ft1000_netdev
//
// Parameters:  ft1000dev  - device structure
//
//
// Returns:     STATUS_SUCCESS - success
//              STATUS_FAILURE - failure
//
// Description: This function initialize the network device
//
// Notes:
//
//---------------------------------------------------------------------------
int init_ft1000_netdev(struct ft1000_device *ft1000dev)
{
	struct net_device *netdev;
	struct ft1000_info *pInfo = NULL;
	struct dpram_blk *pdpram_blk;
	int i, ret_val;
	struct list_head *cur, *tmp;
	char card_nr[2];
	unsigned long gCardIndex = 0;

	DEBUG("Enter init_ft1000_netdev...\n");

	netdev = alloc_etherdev(sizeof(struct ft1000_info));
	if (!netdev) {
		DEBUG("init_ft1000_netdev: can not allocate network device\n");
		return -ENOMEM;
	}

	pInfo = netdev_priv(netdev);

	memset(pInfo, 0, sizeof(struct ft1000_info));

	dev_alloc_name(netdev, netdev->name);

	DEBUG("init_ft1000_netdev: network device name is %s\n", netdev->name);

	if (strncmp(netdev->name, "eth", 3) == 0) {
		card_nr[0] = netdev->name[3];
		card_nr[1] = '\0';
		ret_val = strict_strtoul(card_nr, 10, &gCardIndex);
		if (ret_val) {
			printk(KERN_ERR "Can't parse netdev\n");
			goto err_net;
		}

		pInfo->CardNumber = gCardIndex;
		DEBUG("card number = %d\n", pInfo->CardNumber);
	} else {
		printk(KERN_ERR "ft1000: Invalid device name\n");
		ret_val = -ENXIO;
		goto err_net;
	}

	memset(&pInfo->stats, 0, sizeof(struct net_device_stats));

	spin_lock_init(&pInfo->dpram_lock);
	pInfo->pFt1000Dev = ft1000dev;
	pInfo->DrvErrNum = 0;
	pInfo->registered = 1;
	pInfo->ft1000_reset = ft1000_reset;
	pInfo->mediastate = 0;
	pInfo->fifo_cnt = 0;
	pInfo->DeviceCreated = FALSE;
	pInfo->CardReady = 0;
	pInfo->DSP_TIME[0] = 0;
	pInfo->DSP_TIME[1] = 0;
	pInfo->DSP_TIME[2] = 0;
	pInfo->DSP_TIME[3] = 0;
	pInfo->fAppMsgPend = 0;
	pInfo->fCondResetPend = 0;
	pInfo->usbboot = 0;
	pInfo->dspalive = 0;
	memset(&pInfo->tempbuf[0], 0, sizeof(pInfo->tempbuf));

	INIT_LIST_HEAD(&pInfo->prov_list);

	INIT_LIST_HEAD(&pInfo->nodes.list);

#ifdef HAVE_NET_DEVICE_OPS
	netdev->netdev_ops = &ftnet_ops;
#else
	netdev->hard_start_xmit = &ft1000_start_xmit;
	netdev->get_stats = &ft1000_netdev_stats;
	netdev->open = &ft1000_open;
	netdev->stop = &ft1000_close;
#endif

	ft1000dev->net = netdev;

	DEBUG("Initialize free_buff_lock and freercvpool\n");
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

//---------------------------------------------------------------------------
// Function:    reg_ft1000_netdev
//
// Parameters:  ft1000dev  - device structure
//
//
// Returns:     STATUS_SUCCESS - success
//              STATUS_FAILURE - failure
//
// Description: This function register the network driver
//
// Notes:
//
//---------------------------------------------------------------------------
int reg_ft1000_netdev(struct ft1000_device *ft1000dev,
		      struct usb_interface *intf)
{
	struct net_device *netdev;
	struct ft1000_info *pInfo;
	int rc;

	netdev = ft1000dev->net;
	pInfo = netdev_priv(ft1000dev->net);
	DEBUG("Enter reg_ft1000_netdev...\n");

	ft1000_read_register(ft1000dev, &pInfo->AsicID, FT1000_REG_ASIC_ID);

	usb_set_intfdata(intf, pInfo);
	SET_NETDEV_DEV(netdev, &intf->dev);

	rc = register_netdev(netdev);
	if (rc) {
		DEBUG("reg_ft1000_netdev: could not register network device\n");
		free_netdev(netdev);
		return rc;
	}

	ft1000_create_dev(ft1000dev);

	DEBUG("reg_ft1000_netdev returned\n");

	pInfo->CardReady = 1;

	return 0;
}

static int ft1000_reset(struct net_device *dev)
{
	ft1000_reset_card(dev);
	return 0;
}

//---------------------------------------------------------------------------
// Function:    ft1000_usb_transmit_complete
//
// Parameters:  urb  - transmitted usb urb
//
//
// Returns:     none
//
// Description: This is the callback function when a urb is transmitted
//
// Notes:
//
//---------------------------------------------------------------------------
static void ft1000_usb_transmit_complete(struct urb *urb)
{

	struct ft1000_device *ft1000dev = urb->context;

	if (urb->status)
		pr_err("%s: TX status %d\n", ft1000dev->net->name, urb->status);

	netif_wake_queue(ft1000dev->net);
}

//---------------------------------------------------------------------------
//
// Function:   ft1000_copy_down_pkt
// Description: This function will take an ethernet packet and convert it to
//             a Flarion packet prior to sending it to the ASIC Downlink
//             FIFO.
// Input:
//     dev    - device structure
//     packet - address of ethernet packet
//     len    - length of IP packet
// Output:
//     status - FAILURE
//              SUCCESS
//
//---------------------------------------------------------------------------
static int ft1000_copy_down_pkt(struct net_device *netdev, u8 * packet, u16 len)
{
	struct ft1000_info *pInfo = netdev_priv(netdev);
	struct ft1000_device *pFt1000Dev = pInfo->pFt1000Dev;

	int count, ret;
	u8 *t;
	struct pseudo_hdr hdr;

	if (!pInfo->CardReady) {
		DEBUG("ft1000_copy_down_pkt::Card Not Ready\n");
		return -ENODEV;
	}

	count = sizeof(struct pseudo_hdr) + len;
	if (count > MAX_BUF_SIZE) {
		DEBUG("Error:ft1000_copy_down_pkt:Message Size Overflow!\n");
		DEBUG("size = %d\n", count);
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
	memcpy(&(pFt1000Dev->tx_buf[sizeof(struct pseudo_hdr)]), packet, len);

	netif_stop_queue(netdev);

	usb_fill_bulk_urb(pFt1000Dev->tx_urb,
			  pFt1000Dev->dev,
			  usb_sndbulkpipe(pFt1000Dev->dev,
					  pFt1000Dev->bulk_out_endpointAddr),
			  pFt1000Dev->tx_buf, count,
			  ft1000_usb_transmit_complete, (void *)pFt1000Dev);

	t = (u8 *) pFt1000Dev->tx_urb->transfer_buffer;

	ret = usb_submit_urb(pFt1000Dev->tx_urb, GFP_ATOMIC);

	if (ret) {
		DEBUG("ft1000 failed tx_urb %d\n", ret);
		return ret;
	} else {
		pInfo->stats.tx_packets++;
		pInfo->stats.tx_bytes += (len + 14);
	}

	return 0;
}


//---------------------------------------------------------------------------
// Function:    ft1000_start_xmit
//
// Parameters:  skb - socket buffer to be sent
//              dev - network device
//
//
// Returns:     none
//
// Description: transmit a ethernet packet
//
// Notes:
//
//---------------------------------------------------------------------------
static int ft1000_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct ft1000_info *pInfo = netdev_priv(dev);
	struct ft1000_device *pFt1000Dev = pInfo->pFt1000Dev;
	u8 *pdata;
	int maxlen, pipe;

	if (skb == NULL) {
		DEBUG("ft1000_hw: ft1000_start_xmit:skb == NULL!!!\n");
		return NETDEV_TX_OK;
	}

	if (pFt1000Dev->status & FT1000_STATUS_CLOSING) {
		DEBUG("network driver is closed, return\n");
		goto err;
	}

	pipe =
	    usb_sndbulkpipe(pFt1000Dev->dev, pFt1000Dev->bulk_out_endpointAddr);
	maxlen = usb_maxpacket(pFt1000Dev->dev, pipe, usb_pipeout(pipe));

	pdata = (u8 *) skb->data;

	if (pInfo->mediastate == 0) {
		/* Drop packet is mediastate is down */
		DEBUG("ft1000_hw:ft1000_start_xmit:mediastate is down\n");
		goto err;
	}

	if ((skb->len < ENET_HEADER_SIZE) || (skb->len > ENET_MAX_SIZE)) {
		/* Drop packet which has invalid size */
		DEBUG("ft1000_hw:ft1000_start_xmit:invalid ethernet length\n");
		goto err;
	}

	ft1000_copy_down_pkt(dev, (pdata + ENET_HEADER_SIZE - 2),
			     skb->len - ENET_HEADER_SIZE + 2);

err:
	dev_kfree_skb(skb);

	return NETDEV_TX_OK;
}


//---------------------------------------------------------------------------
//
// Function:   ft1000_copy_up_pkt
// Description: This function will take a packet from the FIFO up link and
//             convert it into an ethernet packet and deliver it to the IP stack
// Input:
//     urb - the receiving usb urb
//
// Output:
//     status - FAILURE
//              SUCCESS
//
//---------------------------------------------------------------------------
static int ft1000_copy_up_pkt(struct urb *urb)
{
	struct ft1000_info *info = urb->context;
	struct ft1000_device *ft1000dev = info->pFt1000Dev;
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
		DEBUG("network driver is closed, return\n");
		return STATUS_SUCCESS;
	}
	// Read length
	len = urb->transfer_buffer_length;
	lena = urb->actual_length;

	chksum = (u16 *) ft1000dev->rx_buf;

	tempword = *chksum++;
	for (i = 1; i < 7; i++)
		tempword ^= *chksum++;

	if (tempword != *chksum) {
		info->stats.rx_errors++;
		ft1000_submit_rx_urb(info);
		return STATUS_FAILURE;
	}

	skb = dev_alloc_skb(len + 12 + 2);

	if (skb == NULL) {
		DEBUG("ft1000_copy_up_pkt: No Network buffers available\n");
		info->stats.rx_errors++;
		ft1000_submit_rx_urb(info);
		return STATUS_FAILURE;
	}

	pbuffer = (u8 *) skb_put(skb, len + 12);

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

	return SUCCESS;
}


//---------------------------------------------------------------------------
//
// Function:   ft1000_submit_rx_urb
// Description: the receiving function of the network driver
//
// Input:
//     info - a private structure contains the device information
//
// Output:
//     status - FAILURE
//              SUCCESS
//
//---------------------------------------------------------------------------
static int ft1000_submit_rx_urb(struct ft1000_info *info)
{
	int result;
	struct ft1000_device *pFt1000Dev = info->pFt1000Dev;

	if (pFt1000Dev->status & FT1000_STATUS_CLOSING) {
		DEBUG("network driver is closed, return\n");
		return -ENODEV;
	}

	usb_fill_bulk_urb(pFt1000Dev->rx_urb,
			  pFt1000Dev->dev,
			  usb_rcvbulkpipe(pFt1000Dev->dev,
					  pFt1000Dev->bulk_in_endpointAddr),
			  pFt1000Dev->rx_buf, MAX_BUF_SIZE,
			  (usb_complete_t) ft1000_copy_up_pkt, info);

	result = usb_submit_urb(pFt1000Dev->rx_urb, GFP_ATOMIC);

	if (result) {
		pr_err("ft1000_submit_rx_urb: submitting rx_urb %d failed\n",
		       result);
		return result;
	}

	return 0;
}


//---------------------------------------------------------------------------
// Function:    ft1000_open
//
// Parameters:
//              dev - network device
//
//
// Returns:     none
//
// Description: open the network driver
//
// Notes:
//
//---------------------------------------------------------------------------
static int ft1000_open(struct net_device *dev)
{
	struct ft1000_info *pInfo = netdev_priv(dev);
	struct timeval tv;
	int ret;

	DEBUG("ft1000_open is called for card %d\n", pInfo->CardNumber);

	pInfo->stats.rx_bytes = 0;
	pInfo->stats.tx_bytes = 0;
	pInfo->stats.rx_packets = 0;
	pInfo->stats.tx_packets = 0;
	do_gettimeofday(&tv);
	pInfo->ConTm = tv.tv_sec;
	pInfo->ProgConStat = 0;

	netif_start_queue(dev);

	netif_carrier_on(dev);

	ret = ft1000_submit_rx_urb(pInfo);

	return ret;
}

//---------------------------------------------------------------------------
// Function:    ft1000_close
//
// Parameters:
//              net - network device
//
//
// Returns:     none
//
// Description: close the network driver
//
// Notes:
//
//---------------------------------------------------------------------------
int ft1000_close(struct net_device *net)
{
	struct ft1000_info *pInfo = netdev_priv(net);
	struct ft1000_device *ft1000dev = pInfo->pFt1000Dev;

	ft1000dev->status |= FT1000_STATUS_CLOSING;

	DEBUG("ft1000_close: pInfo=%p, ft1000dev=%p\n", pInfo, ft1000dev);
	netif_carrier_off(net);
	netif_stop_queue(net);
	ft1000dev->status &= ~FT1000_STATUS_CLOSING;

	pInfo->ProgConStat = 0xff;

	return 0;
}

static struct net_device_stats *ft1000_netdev_stats(struct net_device *dev)
{
	struct ft1000_info *info = netdev_priv(dev);

	return &(info->stats);
}


//---------------------------------------------------------------------------
//
// Function:   ft1000_chkcard
// Description: This function will check if the device is presently available on
//             the system.
// Input:
//     dev    - device structure
// Output:
//     status - FALSE (device is not present)
//              TRUE  (device is present)
//
//---------------------------------------------------------------------------
static int ft1000_chkcard(struct ft1000_device *dev)
{
	u16 tempword;
	u16 status;
	struct ft1000_info *info = netdev_priv(dev->net);

	if (info->fCondResetPend) {
		DEBUG
		    ("ft1000_hw:ft1000_chkcard:Card is being reset, return FALSE\n");
		return TRUE;
	}
	/* Mask register is used to check for device presence since it is never
	 * set to zero.
	 */
	status = ft1000_read_register(dev, &tempword, FT1000_REG_SUP_IMASK);
	if (tempword == 0) {
		DEBUG
		    ("ft1000_hw:ft1000_chkcard: IMASK = 0 Card not detected\n");
		return FALSE;
	}
	/* The system will return the value of 0xffff for the version register
	 * if the device is not present.
	 */
	status = ft1000_read_register(dev, &tempword, FT1000_REG_ASIC_ID);
	if (tempword != 0x1b01) {
		dev->status |= FT1000_STATUS_CLOSING;
		DEBUG
		    ("ft1000_hw:ft1000_chkcard: Version = 0xffff Card not detected\n");
		return FALSE;
	}
	return TRUE;
}

//---------------------------------------------------------------------------
//
// Function:   ft1000_receive_cmd
// Description: This function will read a message from the dpram area.
// Input:
//    dev - network device structure
//    pbuffer - caller supply address to buffer
//    pnxtph - pointer to next pseudo header
// Output:
//   Status = 0 (unsuccessful)
//          = 1 (successful)
//
//---------------------------------------------------------------------------
static bool ft1000_receive_cmd(struct ft1000_device *dev, u16 *pbuffer,
			       int maxsz, u16 *pnxtph)
{
	u16 size, ret;
	u16 *ppseudohdr;
	int i;
	u16 tempword;

	ret =
	    ft1000_read_dpram16(dev, FT1000_MAG_PH_LEN, (u8 *) &size,
				FT1000_MAG_PH_LEN_INDX);
	size = ntohs(size) + PSEUDOSZ;
	if (size > maxsz) {
		DEBUG("FT1000:ft1000_receive_cmd:Invalid command length = %d\n",
		      size);
		return FALSE;
	} else {
		ppseudohdr = (u16 *) pbuffer;
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

		if ((tempword != *ppseudohdr))
			return FALSE;

		return TRUE;
	}
}

static int ft1000_dsp_prov(void *arg)
{
	struct ft1000_device *dev = (struct ft1000_device *)arg;
	struct ft1000_info *info = netdev_priv(dev->net);
	u16 tempword;
	u16 len;
	u16 i = 0;
	struct prov_record *ptr;
	struct pseudo_hdr *ppseudo_hdr;
	u16 *pmsg;
	u16 status;
	u16 TempShortBuf[256];

	DEBUG("*** DspProv Entered\n");

	while (list_empty(&info->prov_list) == 0) {
		DEBUG("DSP Provisioning List Entry\n");

		/* Check if doorbell is available */
		DEBUG("check if doorbell is cleared\n");
		status =
		    ft1000_read_register(dev, &tempword, FT1000_REG_DOORBELL);
		if (status) {
			DEBUG("ft1000_dsp_prov::ft1000_read_register error\n");
			break;
		}

		while (tempword & FT1000_DB_DPRAM_TX) {
			mdelay(10);
			i++;
			if (i == 10) {
				DEBUG("FT1000:ft1000_dsp_prov:message drop\n");
				return STATUS_FAILURE;
			}
			ft1000_read_register(dev, &tempword,
					     FT1000_REG_DOORBELL);
		}

		if (!(tempword & FT1000_DB_DPRAM_TX)) {
			DEBUG("*** Provision Data Sent to DSP\n");

			/* Send provisioning data */
			ptr =
			    list_entry(info->prov_list.next, struct prov_record,
				       list);
			len = *(u16 *) ptr->pprov_data;
			len = htons(len);
			len += PSEUDOSZ;

			pmsg = (u16 *) ptr->pprov_data;
			ppseudo_hdr = (struct pseudo_hdr *)pmsg;
			/* Insert slow queue sequence number */
			ppseudo_hdr->seq_num = info->squeseqnum++;
			ppseudo_hdr->portsrc = 0;
			/* Calculate new checksum */
			ppseudo_hdr->checksum = *pmsg++;
			for (i = 1; i < 7; i++) {
				ppseudo_hdr->checksum ^= *pmsg++;
			}

			TempShortBuf[0] = 0;
			TempShortBuf[1] = htons(len);
			memcpy(&TempShortBuf[2], ppseudo_hdr, len);

			status =
			    ft1000_write_dpram32(dev, 0,
						 (u8 *) &TempShortBuf[0],
						 (unsigned short)(len + 2));
			status =
			    ft1000_write_register(dev, FT1000_DB_DPRAM_TX,
						  FT1000_REG_DOORBELL);

			list_del(&ptr->list);
			kfree(ptr->pprov_data);
			kfree(ptr);
		}
		msleep(10);
	}

	DEBUG("DSP Provisioning List Entry finished\n");

	msleep(100);

	info->fProvComplete = 1;
	info->CardReady = 1;

	return STATUS_SUCCESS;
}

static int ft1000_proc_drvmsg(struct ft1000_device *dev, u16 size)
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
	u16 status;
	union {
		u8 byte[2];
		u16 wrd;
	} convert;

	char *cmdbuffer = kmalloc(1600, GFP_KERNEL);
	if (!cmdbuffer)
		return STATUS_FAILURE;

	status = ft1000_read_dpram32(dev, 0x200, cmdbuffer, size);

#ifdef JDEBUG
	DEBUG("ft1000_proc_drvmsg:cmdbuffer\n");
	for (i = 0; i < size; i += 5) {
		if ((i + 5) < size)
			DEBUG("0x%x, 0x%x, 0x%x, 0x%x, 0x%x\n", cmdbuffer[i],
			      cmdbuffer[i + 1], cmdbuffer[i + 2],
			      cmdbuffer[i + 3], cmdbuffer[i + 4]);
		else {
			for (j = i; j < size; j++)
				DEBUG("0x%x ", cmdbuffer[j]);
			DEBUG("\n");
			break;
		}
	}
#endif
	pdrvmsg = (struct drv_msg *)&cmdbuffer[2];
	msgtype = ntohs(pdrvmsg->type);
	DEBUG("ft1000_proc_drvmsg:Command message type = 0x%x\n", msgtype);
	switch (msgtype) {
	case MEDIA_STATE:{
			DEBUG
			    ("ft1000_proc_drvmsg:Command message type = MEDIA_STATE");

			pmediamsg = (struct media_msg *)&cmdbuffer[0];
			if (info->ProgConStat != 0xFF) {
				if (pmediamsg->state) {
					DEBUG("Media is up\n");
					if (info->mediastate == 0) {
						if (info->NetDevRegDone) {
							netif_wake_queue(dev->
									 net);
						}
						info->mediastate = 1;
					}
				} else {
					DEBUG("Media is down\n");
					if (info->mediastate == 1) {
						info->mediastate = 0;
						if (info->NetDevRegDone) {
						}
						info->ConTm = 0;
					}
				}
			} else {
				DEBUG("Media is down\n");
				if (info->mediastate == 1) {
					info->mediastate = 0;
					info->ConTm = 0;
				}
			}
			break;
		}
	case DSP_INIT_MSG:{
			DEBUG
			    ("ft1000_proc_drvmsg:Command message type = DSP_INIT_MSG");

			pdspinitmsg = (struct dsp_init_msg *)&cmdbuffer[2];
			memcpy(info->DspVer, pdspinitmsg->DspVer, DSPVERSZ);
			DEBUG("DSPVER = 0x%2x 0x%2x 0x%2x 0x%2x\n",
			      info->DspVer[0], info->DspVer[1], info->DspVer[2],
			      info->DspVer[3]);
			memcpy(info->HwSerNum, pdspinitmsg->HwSerNum,
			       HWSERNUMSZ);
			memcpy(info->Sku, pdspinitmsg->Sku, SKUSZ);
			memcpy(info->eui64, pdspinitmsg->eui64, EUISZ);
			DEBUG("EUI64=%2x.%2x.%2x.%2x.%2x.%2x.%2x.%2x\n",
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
				memcpy(info->ProductMode,
				       pdspinitmsg->ProductMode, MODESZ);
				memcpy(info->RfCalVer, pdspinitmsg->RfCalVer,
				       CALVERSZ);
				memcpy(info->RfCalDate, pdspinitmsg->RfCalDate,
				       CALDATESZ);
				DEBUG("RFCalVer = 0x%2x 0x%2x\n",
				      info->RfCalVer[0], info->RfCalVer[1]);
			}
			break;
		}
	case DSP_PROVISION:{
			DEBUG
			    ("ft1000_proc_drvmsg:Command message type = DSP_PROVISION\n");

			/* kick off dspprov routine to start provisioning
			 * Send provisioning data to DSP
			 */
			if (list_empty(&info->prov_list) == 0) {
				info->fProvComplete = 0;
				status = ft1000_dsp_prov(dev);
				if (status != STATUS_SUCCESS)
					goto out;
			} else {
				info->fProvComplete = 1;
				status =
				    ft1000_write_register(dev, FT1000_DB_HB,
							  FT1000_REG_DOORBELL);
				DEBUG
				    ("FT1000:drivermsg:No more DSP provisioning data in dsp image\n");
			}
			DEBUG("ft1000_proc_drvmsg:DSP PROVISION is done\n");
			break;
		}
	case DSP_STORE_INFO:{
			DEBUG
			    ("ft1000_proc_drvmsg:Command message type = DSP_STORE_INFO");

			DEBUG("FT1000:drivermsg:Got DSP_STORE_INFO\n");
			tempword = ntohs(pdrvmsg->length);
			info->DSPInfoBlklen = tempword;
			if (tempword < (MAX_DSP_SESS_REC - 4)) {
				pmsg = (u16 *) &pdrvmsg->data[0];
				for (i = 0; i < ((tempword + 1) / 2); i++) {
					DEBUG
					    ("FT1000:drivermsg:dsp info data = 0x%x\n",
					     *pmsg);
					info->DSPInfoBlk[i + 10] = *pmsg++;
				}
			} else {
				info->DSPInfoBlklen = 0;
			}
			break;
		}
	case DSP_GET_INFO:{
			DEBUG("FT1000:drivermsg:Got DSP_GET_INFO\n");
			/* copy dsp info block to dsp */
			info->DrvMsgPend = 1;
			/* allow any outstanding ioctl to finish */
			mdelay(10);
			status =
			    ft1000_read_register(dev, &tempword,
						 FT1000_REG_DOORBELL);
			if (tempword & FT1000_DB_DPRAM_TX) {
				mdelay(10);
				status =
				    ft1000_read_register(dev, &tempword,
							 FT1000_REG_DOORBELL);
				if (tempword & FT1000_DB_DPRAM_TX) {
					mdelay(10);
					status =
					    ft1000_read_register(dev, &tempword,
								 FT1000_REG_DOORBELL);
					if (tempword & FT1000_DB_DPRAM_TX)
						break;
				}
			}
			/* Put message into Slow Queue
			 * Form Pseudo header
			 */
			pmsg = (u16 *) info->DSPInfoBlk;
			*pmsg++ = 0;
			*pmsg++ =
			    htons(info->DSPInfoBlklen + 20 +
				  info->DSPInfoBlklen);
			ppseudo_hdr =
			    (struct pseudo_hdr *)(u16 *) &info->DSPInfoBlk[2];
			ppseudo_hdr->length =
			    htons(info->DSPInfoBlklen + 4 +
				  info->DSPInfoBlklen);
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
			status =
			    ft1000_write_dpram32(dev, 0,
						 (u8 *) &info->DSPInfoBlk[0],
						 (unsigned short)(info->
								  DSPInfoBlklen
								  + 22));
			status =
			    ft1000_write_register(dev, FT1000_DB_DPRAM_TX,
						  FT1000_REG_DOORBELL);
			info->DrvMsgPend = 0;

			break;
		}

	case GET_DRV_ERR_RPT_MSG:{
			DEBUG("FT1000:drivermsg:Got GET_DRV_ERR_RPT_MSG\n");
			/* copy driver error message to dsp */
			info->DrvMsgPend = 1;
			/* allow any outstanding ioctl to finish */
			mdelay(10);
			status =
			    ft1000_read_register(dev, &tempword,
						 FT1000_REG_DOORBELL);
			if (tempword & FT1000_DB_DPRAM_TX) {
				mdelay(10);
				status =
				    ft1000_read_register(dev, &tempword,
							 FT1000_REG_DOORBELL);
				if (tempword & FT1000_DB_DPRAM_TX)
					mdelay(10);
			}

			if ((tempword & FT1000_DB_DPRAM_TX) == 0) {
				/* Put message into Slow Queue
				 * Form Pseudo header
				 */
				pmsg = (u16 *) &tempbuffer[0];
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

				pmsg = (u16 *) &tempbuffer[16];
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

				card_send_command(dev,
						 (unsigned char *)&tempbuffer[0],
						 (u16) (0x0012 + PSEUDOSZ));
				info->DrvErrNum = 0;
			}
			info->DrvMsgPend = 0;

			break;
		}

	default:
		break;
	}

	status = STATUS_SUCCESS;
out:
	kfree(cmdbuffer);
	DEBUG("return from ft1000_proc_drvmsg\n");
	return status;
}

int ft1000_poll(void* dev_id) {

    struct ft1000_device *dev = (struct ft1000_device *)dev_id;
	struct ft1000_info *info = netdev_priv(dev->net);

    u16 tempword;
    u16 status;
    u16 size;
    int i;
    u16 data;
    u16 modulo;
    u16 portid;
    u16 nxtph;
	struct dpram_blk *pdpram_blk;
	struct pseudo_hdr *ppseudo_hdr;
    unsigned long flags;

    if (ft1000_chkcard(dev) == FALSE) {
        DEBUG("ft1000_poll::ft1000_chkcard: failed\n");
        return STATUS_FAILURE;
    }

    status = ft1000_read_register (dev, &tempword, FT1000_REG_DOORBELL);

    if ( !status )
    {

        if (tempword & FT1000_DB_DPRAM_RX) {

            status = ft1000_read_dpram16(dev, 0x200, (u8 *)&data, 0);
            size = ntohs(data) + 16 + 2;
            if (size % 4) {
                modulo = 4 - (size % 4);
                size = size + modulo;
            }
            status = ft1000_read_dpram16(dev, 0x201, (u8 *)&portid, 1);
            portid &= 0xff;

            if (size < MAX_CMD_SQSIZE) {
                switch (portid)
                {
                    case DRIVERID:
                        DEBUG("ft1000_poll: FT1000_REG_DOORBELL message type: FT1000_DB_DPRAM_RX : portid DRIVERID\n");

                        status = ft1000_proc_drvmsg (dev, size);
                        if (status != STATUS_SUCCESS )
                            return status;
                        break;
                    case DSPBCMSGID:
                        // This is a dsp broadcast message
                        // Check which application has registered for dsp broadcast messages

    	    	        for (i=0; i<MAX_NUM_APP; i++) {
        	           if ( (info->app_info[i].DspBCMsgFlag) && (info->app_info[i].fileobject) &&
                                         (info->app_info[i].NumOfMsg < MAX_MSG_LIMIT)  )
			   {
			       nxtph = FT1000_DPRAM_RX_BASE + 2;
			       pdpram_blk = ft1000_get_buffer (&freercvpool);
			       if (pdpram_blk != NULL) {
			           if ( ft1000_receive_cmd(dev, pdpram_blk->pbuffer, MAX_CMD_SQSIZE, &nxtph) ) {
					ppseudo_hdr = (struct pseudo_hdr *)pdpram_blk->pbuffer;
				       // Put message into the appropriate application block
				       info->app_info[i].nRxMsg++;
				       spin_lock_irqsave(&free_buff_lock, flags);
				       list_add_tail(&pdpram_blk->list, &info->app_info[i].app_sqlist);
				       info->app_info[i].NumOfMsg++;
				       spin_unlock_irqrestore(&free_buff_lock, flags);
				       wake_up_interruptible(&info->app_info[i].wait_dpram_msg);
                                   }
                                   else {
				       info->app_info[i].nRxMsgMiss++;
				       // Put memory back to free pool
				       ft1000_free_buffer(pdpram_blk, &freercvpool);
				       DEBUG("pdpram_blk::ft1000_get_buffer NULL\n");
                                   }
                               }
                               else {
                                   DEBUG("Out of memory in free receive command pool\n");
                                   info->app_info[i].nRxMsgMiss++;
                               }
                           }
	                }
                        break;
                    default:
                        pdpram_blk = ft1000_get_buffer (&freercvpool);

                        if (pdpram_blk != NULL) {
                           if ( ft1000_receive_cmd(dev, pdpram_blk->pbuffer, MAX_CMD_SQSIZE, &nxtph) ) {
				ppseudo_hdr = (struct pseudo_hdr *)pdpram_blk->pbuffer;
                               // Search for correct application block
                               for (i=0; i<MAX_NUM_APP; i++) {
                                   if (info->app_info[i].app_id == ppseudo_hdr->portdest) {
                                       break;
                                   }
                               }

                               if (i == MAX_NUM_APP) {
                                   DEBUG("FT1000:ft1000_parse_dpram_msg: No application matching id = %d\n", ppseudo_hdr->portdest);
                                   // Put memory back to free pool
                                   ft1000_free_buffer(pdpram_blk, &freercvpool);
                               }
                               else {
                                   if (info->app_info[i].NumOfMsg > MAX_MSG_LIMIT) {
	                               // Put memory back to free pool
	                               ft1000_free_buffer(pdpram_blk, &freercvpool);
                                   }
                                   else {
                                       info->app_info[i].nRxMsg++;
                                       // Put message into the appropriate application block
                                       list_add_tail(&pdpram_blk->list, &info->app_info[i].app_sqlist);
            			       info->app_info[i].NumOfMsg++;
                                   }
                               }
                           }
                           else {
                               // Put memory back to free pool
                               ft1000_free_buffer(pdpram_blk, &freercvpool);
                           }
                        }
                        else {
                            DEBUG("Out of memory in free receive command pool\n");
                        }
                        break;
                }
            }
            else {
                DEBUG("FT1000:dpc:Invalid total length for SlowQ = %d\n", size);
            }
            status = ft1000_write_register (dev, FT1000_DB_DPRAM_RX, FT1000_REG_DOORBELL);
        }
        else if (tempword & FT1000_DSP_ASIC_RESET) {

            // Let's reset the ASIC from the Host side as well
            status = ft1000_write_register (dev, ASIC_RESET_BIT, FT1000_REG_RESET);
            status = ft1000_read_register (dev, &tempword, FT1000_REG_RESET);
            i = 0;
            while (tempword & ASIC_RESET_BIT) {
                status = ft1000_read_register (dev, &tempword, FT1000_REG_RESET);
                msleep(10);
                i++;
                if (i==100)
                    break;
            }
            if (i==100) {
                DEBUG("Unable to reset ASIC\n");
                return STATUS_SUCCESS;
            }
            msleep(10);
            // Program WMARK register
            status = ft1000_write_register (dev, 0x600, FT1000_REG_MAG_WATERMARK);
            // clear ASIC reset doorbell
            status = ft1000_write_register (dev, FT1000_DSP_ASIC_RESET, FT1000_REG_DOORBELL);
            msleep(10);
        }
        else if (tempword & FT1000_ASIC_RESET_REQ) {
            DEBUG("ft1000_poll: FT1000_REG_DOORBELL message type:  FT1000_ASIC_RESET_REQ\n");

            // clear ASIC reset request from DSP
            status = ft1000_write_register (dev, FT1000_ASIC_RESET_REQ, FT1000_REG_DOORBELL);
            status = ft1000_write_register (dev, HOST_INTF_BE, FT1000_REG_SUP_CTRL);
            // copy dsp session record from Adapter block
            status = ft1000_write_dpram32 (dev, 0, (u8 *)&info->DSPSess.Rec[0], 1024);
            // Program WMARK register
            status = ft1000_write_register (dev, 0x600, FT1000_REG_MAG_WATERMARK);
            // ring doorbell to tell DSP that ASIC is out of reset
            status = ft1000_write_register (dev, FT1000_ASIC_RESET_DSP, FT1000_REG_DOORBELL);
        }
        else if (tempword & FT1000_DB_COND_RESET) {
            DEBUG("ft1000_poll: FT1000_REG_DOORBELL message type:  FT1000_DB_COND_RESET\n");

	    if (info->fAppMsgPend == 0) {
               // Reset ASIC and DSP

                status    = ft1000_read_dpram16(dev, FT1000_MAG_DSP_TIMER0, (u8 *)&(info->DSP_TIME[0]), FT1000_MAG_DSP_TIMER0_INDX);
                status    = ft1000_read_dpram16(dev, FT1000_MAG_DSP_TIMER1, (u8 *)&(info->DSP_TIME[1]), FT1000_MAG_DSP_TIMER1_INDX);
                status    = ft1000_read_dpram16(dev, FT1000_MAG_DSP_TIMER2, (u8 *)&(info->DSP_TIME[2]), FT1000_MAG_DSP_TIMER2_INDX);
                status    = ft1000_read_dpram16(dev, FT1000_MAG_DSP_TIMER3, (u8 *)&(info->DSP_TIME[3]), FT1000_MAG_DSP_TIMER3_INDX);
                info->CardReady = 0;
                info->DrvErrNum = DSP_CONDRESET_INFO;
                DEBUG("ft1000_hw:DSP conditional reset requested\n");
                info->ft1000_reset(dev->net);
            }
            else {
                info->fProvComplete = 0;
                info->fCondResetPend = 1;
            }

            ft1000_write_register(dev, FT1000_DB_COND_RESET, FT1000_REG_DOORBELL);
        }

    }

    return STATUS_SUCCESS;

}

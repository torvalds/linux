#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/slab.h>

#include <scsi/scsi.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_device.h>

#include "usb.h"
#include "scsiglue.h"
#include "transport.h"
#include "init.h"

/*
 * ENE_InitMedia():
 */
int ENE_InitMedia(struct us_data *us)
{
	int	result;
	u8	MiscReg03 = 0;

	dev_info(&us->pusb_dev->dev, "--- Init Media ---\n");
	result = ene_read_byte(us, REG_CARD_STATUS, &MiscReg03);
	if (result != USB_STOR_XFER_GOOD) {
		dev_err(&us->pusb_dev->dev, "Failed to read register\n");
		return USB_STOR_TRANSPORT_ERROR;
	}
	dev_info(&us->pusb_dev->dev, "MiscReg03 = %x\n", MiscReg03);

	if (MiscReg03 & 0x02) {
		if (!us->SM_Status.Ready && !us->MS_Status.Ready) {
			result = ENE_SMInit(us);
			if (result != USB_STOR_XFER_GOOD)
				return USB_STOR_TRANSPORT_ERROR;
		}

	}
	return result;
}

/*
 * ene_read_byte() :
 */
int ene_read_byte(struct us_data *us, u16 index, void *buf)
{
	struct bulk_cb_wrap *bcb = (struct bulk_cb_wrap *) us->iobuf;
	int result;

	memset(bcb, 0, sizeof(struct bulk_cb_wrap));
	bcb->Signature = cpu_to_le32(US_BULK_CB_SIGN);
	bcb->DataTransferLength	= 0x01;
	bcb->Flags			= 0x80;
	bcb->CDB[0]			= 0xED;
	bcb->CDB[2]			= (u8)(index>>8);
	bcb->CDB[3]			= (u8)index;

	result = ENE_SendScsiCmd(us, FDIR_READ, buf, 0);
	return result;
}

/*
 *ENE_SMInit()
 */
int ENE_SMInit(struct us_data *us)
{
	struct bulk_cb_wrap *bcb = (struct bulk_cb_wrap *) us->iobuf;
	int	result;
	u8	buf[0x200];

	dev_dbg(&us->pusb_dev->dev, "transport --- ENE_SMInit\n");

	result = ENE_LoadBinCode(us, SM_INIT_PATTERN);
	if (result != USB_STOR_XFER_GOOD) {
		dev_info(&us->pusb_dev->dev,
			 "Failed to load SmartMedia init code\n: result= %x\n",
			 result);
		return USB_STOR_TRANSPORT_ERROR;
	}

	memset(bcb, 0, sizeof(struct bulk_cb_wrap));
	bcb->Signature = cpu_to_le32(US_BULK_CB_SIGN);
	bcb->DataTransferLength	= 0x200;
	bcb->Flags			= 0x80;
	bcb->CDB[0]			= 0xF1;
	bcb->CDB[1]			= 0x01;

	result = ENE_SendScsiCmd(us, FDIR_READ, &buf, 0);
	if (result != USB_STOR_XFER_GOOD) {
		dev_err(&us->pusb_dev->dev,
			"Failed to load SmartMedia init code: result = %x\n",
			result);
		return USB_STOR_TRANSPORT_ERROR;
	}

	us->SM_Status = *(struct keucr_sm_status *)&buf[0];

	us->SM_DeviceID = buf[1];
	us->SM_CardID   = buf[2];

	if (us->SM_Status.Insert && us->SM_Status.Ready) {
		dev_info(&us->pusb_dev->dev, "Insert     = %x\n",
					     us->SM_Status.Insert);
		dev_info(&us->pusb_dev->dev, "Ready      = %x\n",
					     us->SM_Status.Ready);
		dev_info(&us->pusb_dev->dev, "WtP        = %x\n",
					     us->SM_Status.WtP);
		dev_info(&us->pusb_dev->dev, "DeviceID   = %x\n",
					     us->SM_DeviceID);
		dev_info(&us->pusb_dev->dev, "CardID     = %x\n",
					     us->SM_CardID);
		MediaChange = 1;
		Check_D_MediaFmt(us);
	} else {
		dev_err(&us->pusb_dev->dev,
			"SmartMedia Card Not Ready --- %x\n", buf[0]);
		return USB_STOR_TRANSPORT_ERROR;
	}

	return USB_STOR_TRANSPORT_GOOD;
}

/*
 * ENE_LoadBinCode()
 */
int ENE_LoadBinCode(struct us_data *us, u8 flag)
{
	struct bulk_cb_wrap *bcb = (struct bulk_cb_wrap *) us->iobuf;
	int result;
	/* void *buf; */
	u8 *buf;

	/* dev_info(&us->pusb_dev->dev, "transport --- ENE_LoadBinCode\n"); */
	if (us->BIN_FLAG == flag)
		return USB_STOR_TRANSPORT_GOOD;

	buf = kmalloc(0x800, GFP_KERNEL);
	if (buf == NULL)
		return USB_STOR_TRANSPORT_ERROR;
	switch (flag) {
	/* For SS */
	case SM_INIT_PATTERN:
		dev_dbg(&us->pusb_dev->dev, "SM_INIT_PATTERN\n");
		memcpy(buf, SM_Init, 0x800);
		break;
	case SM_RW_PATTERN:
		dev_dbg(&us->pusb_dev->dev, "SM_RW_PATTERN\n");
		memcpy(buf, SM_Rdwr, 0x800);
		break;
	}

	memset(bcb, 0, sizeof(struct bulk_cb_wrap));
	bcb->Signature = cpu_to_le32(US_BULK_CB_SIGN);
	bcb->DataTransferLength = 0x800;
	bcb->Flags = 0x00;
	bcb->CDB[0] = 0xEF;

	result = ENE_SendScsiCmd(us, FDIR_WRITE, buf, 0);

	kfree(buf);
	us->BIN_FLAG = flag;
	return result;
}

/*
 * ENE_SendScsiCmd():
 */
int ENE_SendScsiCmd(struct us_data *us, u8 fDir, void *buf, int use_sg)
{
	struct bulk_cb_wrap *bcb = (struct bulk_cb_wrap *) us->iobuf;
	struct bulk_cs_wrap *bcs = (struct bulk_cs_wrap *) us->iobuf;

	int result;
	unsigned int transfer_length = bcb->DataTransferLength,
		     cswlen = 0, partial = 0;
	unsigned int residue;

	/* dev_dbg(&us->pusb_dev->dev, "transport --- ENE_SendScsiCmd\n"); */
	/* send cmd to out endpoint */
	result = usb_stor_bulk_transfer_buf(us, us->send_bulk_pipe,
					    bcb, US_BULK_CB_WRAP_LEN, NULL);
	if (result != USB_STOR_XFER_GOOD) {
		dev_err(&us->pusb_dev->dev,
				"send cmd to out endpoint fail ---\n");
		return USB_STOR_TRANSPORT_ERROR;
	}

	if (buf) {
		unsigned int pipe = fDir;

		if (fDir == FDIR_READ)
			pipe = us->recv_bulk_pipe;
		else
			pipe = us->send_bulk_pipe;

		/* Bulk */
		if (use_sg)
			result = usb_stor_bulk_srb(us, pipe, us->srb);
		else
			result = usb_stor_bulk_transfer_sg(us, pipe, buf,
						transfer_length, 0, &partial);
		if (result != USB_STOR_XFER_GOOD) {
			dev_err(&us->pusb_dev->dev, "data transfer fail ---\n");
			return USB_STOR_TRANSPORT_ERROR;
		}
	}

	/* Get CSW for device status */
	result = usb_stor_bulk_transfer_buf(us, us->recv_bulk_pipe, bcs,
						US_BULK_CS_WRAP_LEN, &cswlen);

	if (result == USB_STOR_XFER_SHORT && cswlen == 0) {
		dev_warn(&us->pusb_dev->dev,
				"Received 0-length CSW; retrying...\n");
		result = usb_stor_bulk_transfer_buf(us, us->recv_bulk_pipe,
					bcs, US_BULK_CS_WRAP_LEN, &cswlen);
	}

	if (result == USB_STOR_XFER_STALLED) {
		/* get the status again */
		dev_warn(&us->pusb_dev->dev,
				"Attempting to get CSW (2nd try)...\n");
		result = usb_stor_bulk_transfer_buf(us, us->recv_bulk_pipe,
						bcs, US_BULK_CS_WRAP_LEN, NULL);
	}

	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	/* check bulk status */
	residue = le32_to_cpu(bcs->Residue);

	/*
	 * try to compute the actual residue, based on how much data
	 * was really transferred and what the device tells us
	 */
	if (residue && !(us->fflags & US_FL_IGNORE_RESIDUE)) {
		residue = min(residue, transfer_length);
		if (us->srb)
			scsi_set_resid(us->srb, max(scsi_get_resid(us->srb),
					(int) residue));
	}

	if (bcs->Status != US_BULK_STAT_OK)
		return USB_STOR_TRANSPORT_ERROR;

	return USB_STOR_TRANSPORT_GOOD;
}

/*
 * ENE_Read_Data()
 */
int ENE_Read_Data(struct us_data *us, void *buf, unsigned int length)
{
	struct bulk_cb_wrap *bcb = (struct bulk_cb_wrap *) us->iobuf;
	struct bulk_cs_wrap *bcs = (struct bulk_cs_wrap *) us->iobuf;
	int result;

	/* dev_dbg(&us->pusb_dev->dev, "transport --- ENE_Read_Data\n"); */
	/* set up the command wrapper */
	memset(bcb, 0, sizeof(struct bulk_cb_wrap));
	bcb->Signature = cpu_to_le32(US_BULK_CB_SIGN);
	bcb->DataTransferLength = length;
	bcb->Flags = 0x80;
	bcb->CDB[0] = 0xED;
	bcb->CDB[2] = 0xFF;
	bcb->CDB[3] = 0x81;

	/* send cmd to out endpoint */
	result = usb_stor_bulk_transfer_buf(us, us->send_bulk_pipe, bcb,
						US_BULK_CB_WRAP_LEN, NULL);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	/* R/W data */
	result = usb_stor_bulk_transfer_buf(us, us->recv_bulk_pipe,
						buf, length, NULL);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	/* Get CSW for device status */
	result = usb_stor_bulk_transfer_buf(us, us->recv_bulk_pipe, bcs,
						US_BULK_CS_WRAP_LEN, NULL);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;
	if (bcs->Status != US_BULK_STAT_OK)
		return USB_STOR_TRANSPORT_ERROR;

	return USB_STOR_TRANSPORT_GOOD;
}

/*
 * ENE_Write_Data():
 */
int ENE_Write_Data(struct us_data *us, void *buf, unsigned int length)
{
	struct bulk_cb_wrap *bcb = (struct bulk_cb_wrap *) us->iobuf;
	struct bulk_cs_wrap *bcs = (struct bulk_cs_wrap *) us->iobuf;
	int result;

	/* printk("transport --- ENE_Write_Data\n"); */
	/* set up the command wrapper */
	memset(bcb, 0, sizeof(struct bulk_cb_wrap));
	bcb->Signature = cpu_to_le32(US_BULK_CB_SIGN);
	bcb->DataTransferLength = length;
	bcb->Flags = 0x00;
	bcb->CDB[0] = 0xEE;
	bcb->CDB[2] = 0xFF;
	bcb->CDB[3] = 0x81;

	/* send cmd to out endpoint */
	result = usb_stor_bulk_transfer_buf(us, us->send_bulk_pipe, bcb,
						US_BULK_CB_WRAP_LEN, NULL);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	/* R/W data */
	result = usb_stor_bulk_transfer_buf(us, us->send_bulk_pipe,
						buf, length, NULL);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	/* Get CSW for device status */
	result = usb_stor_bulk_transfer_buf(us, us->recv_bulk_pipe, bcs,
						US_BULK_CS_WRAP_LEN, NULL);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;
	if (bcs->Status != US_BULK_STAT_OK)
		return USB_STOR_TRANSPORT_ERROR;

	return USB_STOR_TRANSPORT_GOOD;
}


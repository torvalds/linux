#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/slab.h>

#include <scsi/scsi.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_device.h>

#include "usb.h"
#include "scsiglue.h"
#include "transport.h"

int SD_SCSI_Test_Unit_Ready (struct us_data *us, struct scsi_cmnd *srb);
int SD_SCSI_Inquiry         (struct us_data *us, struct scsi_cmnd *srb);
int SD_SCSI_Mode_Sense      (struct us_data *us, struct scsi_cmnd *srb);
int SD_SCSI_Start_Stop      (struct us_data *us, struct scsi_cmnd *srb);
int SD_SCSI_Read_Capacity   (struct us_data *us, struct scsi_cmnd *srb);
int SD_SCSI_Read            (struct us_data *us, struct scsi_cmnd *srb);
int SD_SCSI_Write           (struct us_data *us, struct scsi_cmnd *srb);

//----- SD_SCSIIrp() --------------------------------------------------
int SD_SCSIIrp(struct us_data *us, struct scsi_cmnd *srb)
{
	int    result;

	us->SrbStatus = SS_SUCCESS;
	switch (srb->cmnd[0])
	{
		case TEST_UNIT_READY :  result = SD_SCSI_Test_Unit_Ready (us, srb);  break; //0x00
		case INQUIRY         :  result = SD_SCSI_Inquiry         (us, srb);  break; //0x12
		case MODE_SENSE      :  result = SD_SCSI_Mode_Sense      (us, srb);  break; //0x1A
//		case START_STOP      :  result = SD_SCSI_Start_Stop      (us, srb);  break; //0x1B
		case READ_CAPACITY   :  result = SD_SCSI_Read_Capacity   (us, srb);  break; //0x25
		case READ_10         :  result = SD_SCSI_Read            (us, srb);  break; //0x28
		case WRITE_10        :  result = SD_SCSI_Write           (us, srb);  break; //0x2A

		default:
			us->SrbStatus = SS_ILLEGAL_REQUEST;
			result = USB_STOR_TRANSPORT_FAILED;
			break;
	}
	return result;
}

//----- SD_SCSI_Test_Unit_Ready() --------------------------------------------------
int SD_SCSI_Test_Unit_Ready(struct us_data *us, struct scsi_cmnd *srb)
{
	//printk("SD_SCSI_Test_Unit_Ready\n");
	if (us->SD_Status.Insert && us->SD_Status.Ready)
		return USB_STOR_TRANSPORT_GOOD;
	else
	{
		ENE_SDInit(us);
		return USB_STOR_TRANSPORT_GOOD;
	}
		
	return USB_STOR_TRANSPORT_GOOD;
}

//----- SD_SCSI_Inquiry() --------------------------------------------------
int SD_SCSI_Inquiry(struct us_data *us, struct scsi_cmnd *srb)
{
	//printk("SD_SCSI_Inquiry\n");
	BYTE data_ptr[36] = {0x00, 0x80, 0x02, 0x00, 0x1F, 0x00, 0x00, 0x00, 0x55, 0x53, 0x42, 0x32, 0x2E, 0x30, 0x20, 0x20, 0x43, 0x61, 0x72, 0x64, 0x52, 0x65, 0x61, 0x64, 0x65, 0x72, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x30, 0x31, 0x30, 0x30};

	usb_stor_set_xfer_buf(us, data_ptr, 36, srb, TO_XFER_BUF);
	return USB_STOR_TRANSPORT_GOOD;
}


//----- SD_SCSI_Mode_Sense() --------------------------------------------------
int SD_SCSI_Mode_Sense(struct us_data *us, struct scsi_cmnd *srb)
{
	BYTE	mediaNoWP[12] = {0x0b,0x00,0x00,0x08,0x00,0x00,0x71,0xc0,0x00,0x00,0x02,0x00};
	BYTE	mediaWP[12]   = {0x0b,0x00,0x80,0x08,0x00,0x00,0x71,0xc0,0x00,0x00,0x02,0x00};

	if (us->SD_Status.WtP)
		usb_stor_set_xfer_buf(us, mediaWP, 12, srb, TO_XFER_BUF);
	else
		usb_stor_set_xfer_buf(us, mediaNoWP, 12, srb, TO_XFER_BUF);


	return USB_STOR_TRANSPORT_GOOD;
}

//----- SD_SCSI_Read_Capacity() --------------------------------------------------
int SD_SCSI_Read_Capacity(struct us_data *us, struct scsi_cmnd *srb)
{
	unsigned int offset = 0;
	struct scatterlist *sg = NULL;
	DWORD   bl_num;
	WORD    bl_len;
	BYTE    buf[8];

	printk("SD_SCSI_Read_Capacity\n");
	if ( us->SD_Status.HiCapacity )
	{
		bl_len = 0x200;
		if (us->SD_Status.IsMMC)
			bl_num = us->HC_C_SIZE-1;
		else
			bl_num = (us->HC_C_SIZE + 1) * 1024 - 1;
	}
	else
	{
		bl_len = 1<<(us->SD_READ_BL_LEN);
		bl_num = us->SD_Block_Mult*(us->SD_C_SIZE+1)*(1<<(us->SD_C_SIZE_MULT+2)) - 1;
	}
	us->bl_num = bl_num;
	printk("bl_len = %x\n", bl_len);
	printk("bl_num = %x\n", bl_num);

	//srb->request_bufflen = 8;
	buf[0] = (bl_num>>24) & 0xff;
	buf[1] = (bl_num>>16) & 0xff;
	buf[2] = (bl_num>> 8) & 0xff;
	buf[3] = (bl_num>> 0) & 0xff;
	buf[4] = (bl_len>>24) & 0xff;
	buf[5] = (bl_len>>16) & 0xff;
	buf[6] = (bl_len>> 8) & 0xff;
	buf[7] = (bl_len>> 0) & 0xff;
	
	usb_stor_access_xfer_buf(us, buf, 8, srb, &sg, &offset, TO_XFER_BUF);
	//usb_stor_set_xfer_buf(us, buf, srb->request_bufflen, srb, TO_XFER_BUF);

	return USB_STOR_TRANSPORT_GOOD;
}

//----- SD_SCSI_Read() --------------------------------------------------
int SD_SCSI_Read(struct us_data *us, struct scsi_cmnd *srb)
{
	struct bulk_cb_wrap *bcb = (struct bulk_cb_wrap *) us->iobuf;
	int result;
	PBYTE	Cdb = srb->cmnd;
	DWORD bn  =  ((Cdb[2]<<24) & 0xff000000) | ((Cdb[3]<<16) & 0x00ff0000) |
                   ((Cdb[4]<< 8) & 0x0000ff00) | ((Cdb[5]<< 0) & 0x000000ff);
	WORD  blen = ((Cdb[7]<< 8) & 0xff00)     | ((Cdb[8]<< 0) & 0x00ff);
	DWORD bnByte = bn * 0x200;
	DWORD	blenByte = blen * 0x200;

	if (bn > us->bl_num)
		return USB_STOR_TRANSPORT_ERROR;

	result = ENE_LoadBinCode(us, SD_RW_PATTERN);
	if (result != USB_STOR_XFER_GOOD)
	{
		printk("Load SD RW pattern Fail !!\n");
		return USB_STOR_TRANSPORT_ERROR;
	}

	if ( us->SD_Status.HiCapacity )
		bnByte = bn;
		
	// set up the command wrapper
	memset(bcb, 0, sizeof(struct bulk_cb_wrap));
	bcb->Signature = cpu_to_le32(US_BULK_CB_SIGN);
	bcb->DataTransferLength = blenByte;
	bcb->Flags  = 0x80;
	bcb->CDB[0] = 0xF1;
	bcb->CDB[5] = (BYTE)(bnByte);
	bcb->CDB[4] = (BYTE)(bnByte>>8);
	bcb->CDB[3] = (BYTE)(bnByte>>16);
	bcb->CDB[2] = (BYTE)(bnByte>>24);

	result = ENE_SendScsiCmd(us, FDIR_READ, scsi_sglist(srb), 1);
	return result;
}

//----- SD_SCSI_Write() --------------------------------------------------
int SD_SCSI_Write(struct us_data *us, struct scsi_cmnd *srb)
{
	struct bulk_cb_wrap *bcb = (struct bulk_cb_wrap *) us->iobuf;
	int result;
	PBYTE	Cdb = srb->cmnd;
	DWORD bn  =  ((Cdb[2]<<24) & 0xff000000) | ((Cdb[3]<<16) & 0x00ff0000) |
                   ((Cdb[4]<< 8) & 0x0000ff00) | ((Cdb[5]<< 0) & 0x000000ff);
	WORD  blen = ((Cdb[7]<< 8) & 0xff00)     | ((Cdb[8]<< 0) & 0x00ff);
	DWORD bnByte = bn * 0x200;
	DWORD	blenByte = blen * 0x200;

	if (bn > us->bl_num)
		return USB_STOR_TRANSPORT_ERROR;

	result = ENE_LoadBinCode(us, SD_RW_PATTERN);
	if (result != USB_STOR_XFER_GOOD)
	{
		printk("Load SD RW pattern Fail !!\n");
		return USB_STOR_TRANSPORT_ERROR;
	}

	if ( us->SD_Status.HiCapacity )
		bnByte = bn;

	// set up the command wrapper
	memset(bcb, 0, sizeof(struct bulk_cb_wrap));
	bcb->Signature = cpu_to_le32(US_BULK_CB_SIGN);
	bcb->DataTransferLength = blenByte;
	bcb->Flags  = 0x00;
	bcb->CDB[0] = 0xF0;
	bcb->CDB[5] = (BYTE)(bnByte);
	bcb->CDB[4] = (BYTE)(bnByte>>8);
	bcb->CDB[3] = (BYTE)(bnByte>>16);
	bcb->CDB[2] = (BYTE)(bnByte>>24);

	result = ENE_SendScsiCmd(us, FDIR_WRITE, scsi_sglist(srb), 1);
	return result;
}




#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/slab.h>

#include <scsi/scsi.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_device.h>

#include "usb.h"
#include "scsiglue.h"
#include "transport.h"

int MS_SCSI_Test_Unit_Ready (struct us_data *us, struct scsi_cmnd *srb);
int MS_SCSI_Inquiry         (struct us_data *us, struct scsi_cmnd *srb);
int MS_SCSI_Mode_Sense      (struct us_data *us, struct scsi_cmnd *srb);
int MS_SCSI_Start_Stop      (struct us_data *us, struct scsi_cmnd *srb);
int MS_SCSI_Read_Capacity   (struct us_data *us, struct scsi_cmnd *srb);
int MS_SCSI_Read            (struct us_data *us, struct scsi_cmnd *srb);
int MS_SCSI_Write           (struct us_data *us, struct scsi_cmnd *srb);

//----- MS_SCSIIrp() --------------------------------------------------
int MS_SCSIIrp(struct us_data *us, struct scsi_cmnd *srb)
{
	int    result;

	us->SrbStatus = SS_SUCCESS;
	switch (srb->cmnd[0])
	{
		case TEST_UNIT_READY :  result = MS_SCSI_Test_Unit_Ready (us, srb);  break; //0x00
		case INQUIRY         :  result = MS_SCSI_Inquiry         (us, srb);  break; //0x12
		case MODE_SENSE      :  result = MS_SCSI_Mode_Sense      (us, srb);  break; //0x1A
		case READ_CAPACITY   :  result = MS_SCSI_Read_Capacity   (us, srb);  break; //0x25
		case READ_10         :  result = MS_SCSI_Read            (us, srb);  break; //0x28
		case WRITE_10        :  result = MS_SCSI_Write           (us, srb);  break; //0x2A

		default:
			us->SrbStatus = SS_ILLEGAL_REQUEST;
			result = USB_STOR_TRANSPORT_FAILED;
			break;
	}
	return result;
}

//----- MS_SCSI_Test_Unit_Ready() --------------------------------------------------
int MS_SCSI_Test_Unit_Ready(struct us_data *us, struct scsi_cmnd *srb)
{
	//printk("MS_SCSI_Test_Unit_Ready\n");
	if (us->MS_Status.Insert && us->MS_Status.Ready)
		return USB_STOR_TRANSPORT_GOOD;
	else
	{
		ENE_MSInit(us);
		return USB_STOR_TRANSPORT_GOOD;
	}
		
	return USB_STOR_TRANSPORT_GOOD;
}

//----- MS_SCSI_Inquiry() --------------------------------------------------
int MS_SCSI_Inquiry(struct us_data *us, struct scsi_cmnd *srb)
{
	//printk("MS_SCSI_Inquiry\n");
	BYTE data_ptr[36] = {0x00, 0x80, 0x02, 0x00, 0x1F, 0x00, 0x00, 0x00, 0x55, 0x53, 0x42, 0x32, 0x2E, 0x30, 0x20, 0x20, 0x43, 0x61, 0x72, 0x64, 0x52, 0x65, 0x61, 0x64, 0x65, 0x72, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x30, 0x31, 0x30, 0x30};

	usb_stor_set_xfer_buf(us, data_ptr, 36, srb, TO_XFER_BUF);
	return USB_STOR_TRANSPORT_GOOD;
}


//----- MS_SCSI_Mode_Sense() --------------------------------------------------
int MS_SCSI_Mode_Sense(struct us_data *us, struct scsi_cmnd *srb)
{
	BYTE	mediaNoWP[12] = {0x0b,0x00,0x00,0x08,0x00,0x00,0x71,0xc0,0x00,0x00,0x02,0x00};
	BYTE	mediaWP[12]   = {0x0b,0x00,0x80,0x08,0x00,0x00,0x71,0xc0,0x00,0x00,0x02,0x00};

	if (us->MS_Status.WtP)
		usb_stor_set_xfer_buf(us, mediaWP, 12, srb, TO_XFER_BUF);
	else
		usb_stor_set_xfer_buf(us, mediaNoWP, 12, srb, TO_XFER_BUF);


	return USB_STOR_TRANSPORT_GOOD;
}

//----- MS_SCSI_Read_Capacity() --------------------------------------------------
int MS_SCSI_Read_Capacity(struct us_data *us, struct scsi_cmnd *srb)
{
	unsigned int offset = 0;
	struct scatterlist *sg = NULL;
	DWORD   bl_num;
	WORD    bl_len;
	BYTE    buf[8];

	printk("MS_SCSI_Read_Capacity\n");

	bl_len = 0x200;
	if ( us->MS_Status.IsMSPro )
		bl_num = us->MSP_TotalBlock - 1;
	else
		bl_num = us->MS_Lib.NumberOfLogBlock * us->MS_Lib.blockSize * 2 - 1;

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

//----- MS_SCSI_Read() --------------------------------------------------
int MS_SCSI_Read(struct us_data *us, struct scsi_cmnd *srb)
{
	struct bulk_cb_wrap *bcb = (struct bulk_cb_wrap *) us->iobuf;
	int result=0;
	PBYTE	Cdb = srb->cmnd;
	DWORD bn  =  ((Cdb[2]<<24) & 0xff000000) | ((Cdb[3]<<16) & 0x00ff0000) |
                   ((Cdb[4]<< 8) & 0x0000ff00) | ((Cdb[5]<< 0) & 0x000000ff);
	WORD  blen = ((Cdb[7]<< 8) & 0xff00)     | ((Cdb[8]<< 0) & 0x00ff);
	DWORD	blenByte = blen * 0x200;

	//printk("SCSIOP_READ --- bn = %X, blen = %X, srb->use_sg = %X\n", bn, blen, srb->use_sg);
	
	if (bn > us->bl_num)
		return USB_STOR_TRANSPORT_ERROR;

	if (us->MS_Status.IsMSPro)
	{
		result = ENE_LoadBinCode(us, MSP_RW_PATTERN);
		if (result != USB_STOR_XFER_GOOD)
		{
			printk("Load MSP RW pattern Fail !!\n");
			return USB_STOR_TRANSPORT_ERROR;
		}

		// set up the command wrapper
		memset(bcb, 0, sizeof(struct bulk_cb_wrap));
		bcb->Signature = cpu_to_le32(US_BULK_CB_SIGN);
		bcb->DataTransferLength = blenByte;
		bcb->Flags  = 0x80;
		bcb->CDB[0] = 0xF1;
		bcb->CDB[1] = 0x02;
		bcb->CDB[5] = (BYTE)(bn);
		bcb->CDB[4] = (BYTE)(bn>>8);
		bcb->CDB[3] = (BYTE)(bn>>16);
		bcb->CDB[2] = (BYTE)(bn>>24);

		result = ENE_SendScsiCmd(us, FDIR_READ, scsi_sglist(srb), 1);
	}
	else
	{
		void	*buf;
		int	offset=0;
		WORD	phyblk, logblk;
		BYTE	PageNum;
		WORD	len;
		DWORD	blkno;

		buf = kmalloc(blenByte, GFP_KERNEL);
		if (buf == NULL)
			return USB_STOR_TRANSPORT_ERROR;

		result = ENE_LoadBinCode(us, MS_RW_PATTERN);
		if (result != USB_STOR_XFER_GOOD)
		{
			printk("Load MS RW pattern Fail !!\n");
			result = USB_STOR_TRANSPORT_ERROR;
			goto exit;
		}

		logblk  = (WORD)(bn / us->MS_Lib.PagesPerBlock);
		PageNum = (BYTE)(bn % us->MS_Lib.PagesPerBlock);

		while(1)
		{
			if (blen > (us->MS_Lib.PagesPerBlock-PageNum) )
				len = us->MS_Lib.PagesPerBlock-PageNum;
			else
				len = blen;

			phyblk = MS_LibConv2Physical(us, logblk);
			blkno  = phyblk * 0x20 + PageNum;

			// set up the command wrapper
			memset(bcb, 0, sizeof(struct bulk_cb_wrap));
			bcb->Signature = cpu_to_le32(US_BULK_CB_SIGN);
			bcb->DataTransferLength = 0x200 * len;
			bcb->Flags  = 0x80;
			bcb->CDB[0] = 0xF1;
			bcb->CDB[1] = 0x02;
			bcb->CDB[5] = (BYTE)(blkno);
			bcb->CDB[4] = (BYTE)(blkno>>8);
			bcb->CDB[3] = (BYTE)(blkno>>16);
			bcb->CDB[2] = (BYTE)(blkno>>24);

			result = ENE_SendScsiCmd(us, FDIR_READ, buf+offset, 0);
			if (result != USB_STOR_XFER_GOOD)
			{
				printk("MS_SCSI_Read --- result = %x\n", result);
				result =  USB_STOR_TRANSPORT_ERROR;
				goto exit;
			}

			blen -= len;
			if (blen<=0)
				break;
			logblk++;
			PageNum = 0;
			offset += MS_BYTES_PER_PAGE*len;
		}
		usb_stor_set_xfer_buf(us, buf, blenByte, srb, TO_XFER_BUF);
exit:
		kfree(buf);
	}
	return result;
}

//----- MS_SCSI_Write() --------------------------------------------------
int MS_SCSI_Write(struct us_data *us, struct scsi_cmnd *srb)
{
	struct bulk_cb_wrap *bcb = (struct bulk_cb_wrap *) us->iobuf;
	int result=0;
	PBYTE	Cdb = srb->cmnd;
	DWORD bn  =  ((Cdb[2]<<24) & 0xff000000) | ((Cdb[3]<<16) & 0x00ff0000) |
                   ((Cdb[4]<< 8) & 0x0000ff00) | ((Cdb[5]<< 0) & 0x000000ff);
	WORD  blen = ((Cdb[7]<< 8) & 0xff00)     | ((Cdb[8]<< 0) & 0x00ff);
	DWORD	blenByte = blen * 0x200;

	if (bn > us->bl_num)
		return USB_STOR_TRANSPORT_ERROR;

	if (us->MS_Status.IsMSPro)
	{
		result = ENE_LoadBinCode(us, MSP_RW_PATTERN);
		if (result != USB_STOR_XFER_GOOD)
		{
			printk("Load MSP RW pattern Fail !!\n");
			return USB_STOR_TRANSPORT_ERROR;
		}

		// set up the command wrapper
		memset(bcb, 0, sizeof(struct bulk_cb_wrap));
		bcb->Signature = cpu_to_le32(US_BULK_CB_SIGN);
		bcb->DataTransferLength = blenByte;
		bcb->Flags  = 0x00;
		bcb->CDB[0] = 0xF0;
		bcb->CDB[1] = 0x04;
		bcb->CDB[5] = (BYTE)(bn);
		bcb->CDB[4] = (BYTE)(bn>>8);
		bcb->CDB[3] = (BYTE)(bn>>16);
		bcb->CDB[2] = (BYTE)(bn>>24);

		result = ENE_SendScsiCmd(us, FDIR_WRITE, scsi_sglist(srb), 1);
	}
	else
	{
		void	*buf;
		int	offset=0;
		WORD	PhyBlockAddr;
		BYTE	PageNum;
		DWORD	result;
		WORD	len, oldphy, newphy;

		buf = kmalloc(blenByte, GFP_KERNEL);
		if (buf == NULL)
			return USB_STOR_TRANSPORT_ERROR;
		usb_stor_set_xfer_buf(us, buf, blenByte, srb, FROM_XFER_BUF);

		result = ENE_LoadBinCode(us, MS_RW_PATTERN);
		if (result != USB_STOR_XFER_GOOD)
		{
			printk("Load MS RW pattern Fail !!\n");
			result = USB_STOR_TRANSPORT_ERROR;
			goto exit;
		}

		PhyBlockAddr = (WORD)(bn / us->MS_Lib.PagesPerBlock);
		PageNum      = (BYTE)(bn % us->MS_Lib.PagesPerBlock);

		while(1)
		{
			if (blen > (us->MS_Lib.PagesPerBlock-PageNum) )
				len = us->MS_Lib.PagesPerBlock-PageNum;
			else
				len = blen;

			oldphy = MS_LibConv2Physical(us, PhyBlockAddr);
			newphy = MS_LibSearchBlockFromLogical(us, PhyBlockAddr);

			result = MS_ReaderCopyBlock(us, oldphy, newphy, PhyBlockAddr, PageNum, buf+offset, len);
			if (result != USB_STOR_XFER_GOOD)
			{
				printk("MS_SCSI_Write --- result = %x\n", result);
				result =  USB_STOR_TRANSPORT_ERROR;
				goto exit;
			}

			us->MS_Lib.Phy2LogMap[oldphy] = MS_LB_NOT_USED_ERASED;
			MS_LibForceSetLogicalPair(us, PhyBlockAddr, newphy);

			blen -= len;
			if (blen<=0)
				break;
			PhyBlockAddr++;
			PageNum = 0;
			offset += MS_BYTES_PER_PAGE*len;
		}
exit:
		kfree(buf);
	}
	return result;
}


#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/slab.h>

#include <scsi/scsi.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_device.h>

#include "usb.h"
#include "scsiglue.h"
#include "transport.h"

/***********************************************************************
 * Data transfer routines
 ***********************************************************************/
//----- usb_stor_blocking_completion() ---------------------
static void usb_stor_blocking_completion(struct urb *urb)
{
	struct completion *urb_done_ptr = urb->context;

	//printk("transport --- usb_stor_blocking_completion\n");
	complete(urb_done_ptr);
}

//----- usb_stor_msg_common() ---------------------
static int usb_stor_msg_common(struct us_data *us, int timeout)
{
	struct completion urb_done;
	long timeleft;
	int status;

	//printk("transport --- usb_stor_msg_common\n");
	if (test_bit(US_FLIDX_ABORTING, &us->dflags))
		return -EIO;

	init_completion(&urb_done);

	us->current_urb->context = &urb_done;
	us->current_urb->actual_length = 0;
	us->current_urb->error_count = 0;
	us->current_urb->status = 0;

//	us->current_urb->transfer_flags = URB_NO_SETUP_DMA_MAP;
	if (us->current_urb->transfer_buffer == us->iobuf)
		us->current_urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	us->current_urb->transfer_dma = us->iobuf_dma;
	us->current_urb->setup_dma = us->cr_dma;

	status = usb_submit_urb(us->current_urb, GFP_NOIO);
	if (status)
		return status;

	set_bit(US_FLIDX_URB_ACTIVE, &us->dflags);

	if (test_bit(US_FLIDX_ABORTING, &us->dflags))
	{
		if (test_and_clear_bit(US_FLIDX_URB_ACTIVE, &us->dflags))
		{
			//printk("-- cancelling URB\n");
			usb_unlink_urb(us->current_urb);
		}
	}

	timeleft = wait_for_completion_interruptible_timeout(&urb_done, timeout ? : MAX_SCHEDULE_TIMEOUT);
	clear_bit(US_FLIDX_URB_ACTIVE, &us->dflags);

	if (timeleft <= 0)
	{
		//printk("%s -- cancelling URB\n", timeleft == 0 ? "Timeout" : "Signal");
		usb_kill_urb(us->current_urb);
	}

	return us->current_urb->status;
}

//----- usb_stor_control_msg() ---------------------
int usb_stor_control_msg(struct us_data *us, unsigned int pipe,
		 u8 request, u8 requesttype, u16 value, u16 index,
		 void *data, u16 size, int timeout)
{
	int status;

	//printk("transport --- usb_stor_control_msg\n");

	/* fill in the devrequest structure */
	us->cr->bRequestType = requesttype;
	us->cr->bRequest = request;
	us->cr->wValue = cpu_to_le16(value);
	us->cr->wIndex = cpu_to_le16(index);
	us->cr->wLength = cpu_to_le16(size);

	/* fill and submit the URB */
	usb_fill_control_urb(us->current_urb, us->pusb_dev, pipe,
			 (unsigned char*) us->cr, data, size,
			 usb_stor_blocking_completion, NULL);
	status = usb_stor_msg_common(us, timeout);

	/* return the actual length of the data transferred if no error */
	if (status == 0)
		status = us->current_urb->actual_length;
	return status;
}

//----- usb_stor_clear_halt() ---------------------
int usb_stor_clear_halt(struct us_data *us, unsigned int pipe)
{
	int result;
	int endp = usb_pipeendpoint(pipe);

	//printk("transport --- usb_stor_clear_halt\n");
	if (usb_pipein (pipe))
		endp |= USB_DIR_IN;

	result = usb_stor_control_msg(us, us->send_ctrl_pipe,
		USB_REQ_CLEAR_FEATURE, USB_RECIP_ENDPOINT,
		USB_ENDPOINT_HALT, endp,
		NULL, 0, 3*HZ);

	/* reset the endpoint toggle */
	if (result >= 0)
		//usb_settoggle(us->pusb_dev, usb_pipeendpoint(pipe), usb_pipeout(pipe), 0);
                usb_reset_endpoint(us->pusb_dev, endp);

	return result;
}

//----- interpret_urb_result() ---------------------
static int interpret_urb_result(struct us_data *us, unsigned int pipe,
		unsigned int length, int result, unsigned int partial)
{
	//printk("transport --- interpret_urb_result\n");
	switch (result) {
	/* no error code; did we send all the data? */
	case 0:
		if (partial != length)
		{
			//printk("-- short transfer\n");
			return USB_STOR_XFER_SHORT;
		}
		//printk("-- transfer complete\n");
		return USB_STOR_XFER_GOOD;
	case -EPIPE:
		if (usb_pipecontrol(pipe))
		{
			//printk("-- stall on control pipe\n");
			return USB_STOR_XFER_STALLED;
		}
		//printk("clearing endpoint halt for pipe 0x%x\n", pipe);
		if (usb_stor_clear_halt(us, pipe) < 0)
			return USB_STOR_XFER_ERROR;
		return USB_STOR_XFER_STALLED;
	case -EOVERFLOW:
		//printk("-- babble\n");
		return USB_STOR_XFER_LONG;
	case -ECONNRESET:
		//printk("-- transfer cancelled\n");
		return USB_STOR_XFER_ERROR;
	case -EREMOTEIO:
		//printk("-- short read transfer\n");
		return USB_STOR_XFER_SHORT;
	case -EIO:
		//printk("-- abort or disconnect in progress\n");
		return USB_STOR_XFER_ERROR;
	default:
		//printk("-- unknown error\n");
		return USB_STOR_XFER_ERROR;
	}
}

//----- usb_stor_bulk_transfer_buf() ---------------------
int usb_stor_bulk_transfer_buf(struct us_data *us, unsigned int pipe,
	void *buf, unsigned int length, unsigned int *act_len)
{
	int result;

	//printk("transport --- usb_stor_bulk_transfer_buf\n");

	/* fill and submit the URB */
	usb_fill_bulk_urb(us->current_urb, us->pusb_dev, pipe, buf, length, usb_stor_blocking_completion, NULL);
	result = usb_stor_msg_common(us, 0);

	/* store the actual length of the data transferred */
	if (act_len)
		*act_len = us->current_urb->actual_length;

	return interpret_urb_result(us, pipe, length, result, us->current_urb->actual_length);
}

//----- usb_stor_bulk_transfer_sglist() ---------------------
static int usb_stor_bulk_transfer_sglist(struct us_data *us, unsigned int pipe,
		struct scatterlist *sg, int num_sg, unsigned int length,
		unsigned int *act_len)
{
	int result;

	//printk("transport --- usb_stor_bulk_transfer_sglist\n");
	if (test_bit(US_FLIDX_ABORTING, &us->dflags))
		return USB_STOR_XFER_ERROR;

	/* initialize the scatter-gather request block */
	result = usb_sg_init(&us->current_sg, us->pusb_dev, pipe, 0, sg, num_sg, length, GFP_NOIO);
	if (result)
	{
		//printk("usb_sg_init returned %d\n", result);
		return USB_STOR_XFER_ERROR;
	}

	/* since the block has been initialized successfully, it's now okay to cancel it */
	set_bit(US_FLIDX_SG_ACTIVE, &us->dflags);

	/* did an abort/disconnect occur during the submission? */
	if (test_bit(US_FLIDX_ABORTING, &us->dflags))
	{
		/* cancel the request, if it hasn't been cancelled already */
		if (test_and_clear_bit(US_FLIDX_SG_ACTIVE, &us->dflags))
		{
			//printk("-- cancelling sg request\n");
			usb_sg_cancel(&us->current_sg);
		}
	}

	/* wait for the completion of the transfer */
	usb_sg_wait(&us->current_sg);
	clear_bit(US_FLIDX_SG_ACTIVE, &us->dflags);

	result = us->current_sg.status;
	if (act_len)
		*act_len = us->current_sg.bytes;

	return interpret_urb_result(us, pipe, length, result, us->current_sg.bytes);
}

//----- usb_stor_bulk_srb() ---------------------
int usb_stor_bulk_srb(struct us_data* us, unsigned int pipe, struct scsi_cmnd* srb)
{
	unsigned int partial;
	int result = usb_stor_bulk_transfer_sglist(us, pipe, scsi_sglist(srb),
				      scsi_sg_count(srb), scsi_bufflen(srb),
				      &partial);

	scsi_set_resid(srb, scsi_bufflen(srb) - partial);
	return result;
}

//----- usb_stor_bulk_transfer_sg() ---------------------
int usb_stor_bulk_transfer_sg(struct us_data* us, unsigned int pipe,
		void *buf, unsigned int length_left, int use_sg, int *residual)
{
	int result;
	unsigned int partial;

	//printk("transport --- usb_stor_bulk_transfer_sg\n");
	/* are we scatter-gathering? */
	if (use_sg)
	{
		/* use the usb core scatter-gather primitives */
		result = usb_stor_bulk_transfer_sglist(us, pipe,
				(struct scatterlist *) buf, use_sg,
				length_left, &partial);
		length_left -= partial;
	}
	else
	{
		/* no scatter-gather, just make the request */
		result = usb_stor_bulk_transfer_buf(us, pipe, buf, length_left, &partial);
		length_left -= partial;
	}

	/* store the residual and return the error code */
	if (residual)
		*residual = length_left;
	return result;
}

/***********************************************************************
 * Transport routines
 ***********************************************************************/
//----- usb_stor_invoke_transport() ---------------------
void usb_stor_invoke_transport(struct scsi_cmnd *srb, struct us_data *us)
{
	int need_auto_sense;
	int result;

	//printk("transport --- usb_stor_invoke_transport\n");
	usb_stor_print_cmd(srb);
	/* send the command to the transport layer */
	scsi_set_resid(srb, 0);
	result = us->transport(srb, us); //usb_stor_Bulk_transport;
	
	/* if the command gets aborted by the higher layers, we need to short-circuit all other processing */
	if (test_bit(US_FLIDX_TIMED_OUT, &us->dflags))
	{
		//printk("-- command was aborted\n");
		srb->result = DID_ABORT << 16;
		goto Handle_Errors;
	}

	/* if there is a transport error, reset and don't auto-sense */
	if (result == USB_STOR_TRANSPORT_ERROR)
	{
		//printk("-- transport indicates error, resetting\n");
		srb->result = DID_ERROR << 16;
		goto Handle_Errors;
	}

	/* if the transport provided its own sense data, don't auto-sense */
	if (result == USB_STOR_TRANSPORT_NO_SENSE)
	{
		srb->result = SAM_STAT_CHECK_CONDITION;
		return;
	}

	srb->result = SAM_STAT_GOOD;

	/* Determine if we need to auto-sense */
	need_auto_sense = 0;

	if ((us->protocol == US_PR_CB || us->protocol == US_PR_DPCM_USB) && srb->sc_data_direction != DMA_FROM_DEVICE)
	{
		//printk("-- CB transport device requiring auto-sense\n");
		need_auto_sense = 1;
	}

	if (result == USB_STOR_TRANSPORT_FAILED)
	{
		//printk("-- transport indicates command failure\n");
		need_auto_sense = 1;
	}

	/* Now, if we need to do the auto-sense, let's do it */
	if (need_auto_sense)
	{
		int temp_result;
		struct scsi_eh_save ses;

		printk("Issuing auto-REQUEST_SENSE\n");

		scsi_eh_prep_cmnd(srb, &ses, NULL, 0, US_SENSE_SIZE);

		/* we must do the protocol translation here */
		if (us->subclass == US_SC_RBC || us->subclass == US_SC_SCSI || us->subclass == US_SC_CYP_ATACB)
			srb->cmd_len = 6;
		else
			srb->cmd_len = 12;

		/* issue the auto-sense command */
		scsi_set_resid(srb, 0);
		temp_result = us->transport(us->srb, us);

		/* let's clean up right away */
		scsi_eh_restore_cmnd(srb, &ses);

		if (test_bit(US_FLIDX_TIMED_OUT, &us->dflags))
		{
			//printk("-- auto-sense aborted\n");
			srb->result = DID_ABORT << 16;
			goto Handle_Errors;
		}
		if (temp_result != USB_STOR_TRANSPORT_GOOD)
		{
			//printk("-- auto-sense failure\n");
			srb->result = DID_ERROR << 16;
			if (!(us->fflags & US_FL_SCM_MULT_TARG))
				goto Handle_Errors;
			return;
		}

		/* set the result so the higher layers expect this data */
		srb->result = SAM_STAT_CHECK_CONDITION;

		if (result == USB_STOR_TRANSPORT_GOOD &&
			(srb->sense_buffer[2] & 0xaf) == 0 &&
			srb->sense_buffer[12] == 0 &&
			srb->sense_buffer[13] == 0)
		{
			srb->result = SAM_STAT_GOOD;
			srb->sense_buffer[0] = 0x0;
		}
	}

	/* Did we transfer less than the minimum amount required? */
	if (srb->result == SAM_STAT_GOOD &&	scsi_bufflen(srb) - scsi_get_resid(srb) < srb->underflow)
		srb->result = (DID_ERROR << 16);//v02 | (SUGGEST_RETRY << 24);

	return;

Handle_Errors:
	scsi_lock(us_to_host(us));
	set_bit(US_FLIDX_RESETTING, &us->dflags);
	clear_bit(US_FLIDX_ABORTING, &us->dflags);
	scsi_unlock(us_to_host(us));

	mutex_unlock(&us->dev_mutex);
	result = usb_stor_port_reset(us);
	mutex_lock(&us->dev_mutex);

	if (result < 0)
	{
		scsi_lock(us_to_host(us));
		usb_stor_report_device_reset(us);
		scsi_unlock(us_to_host(us));
		us->transport_reset(us);
	}
	clear_bit(US_FLIDX_RESETTING, &us->dflags);
}

//----- ENE_stor_invoke_transport() ---------------------
void ENE_stor_invoke_transport(struct scsi_cmnd *srb, struct us_data *us)
{
	int result=0;

	//printk("transport --- ENE_stor_invoke_transport\n");
	usb_stor_print_cmd(srb);
	/* send the command to the transport layer */
	scsi_set_resid(srb, 0);
	if ( !(us->SD_Status.Ready || us->MS_Status.Ready || us->SM_Status.Ready) )
		result = ENE_InitMedia(us);
	
	if (us->Power_IsResum == true) {
		result = ENE_InitMedia(us);
		us->Power_IsResum = false;		
	}	
	
	if (us->SD_Status.Ready)	result = SD_SCSIIrp(us, srb);
	if (us->MS_Status.Ready)	result = MS_SCSIIrp(us, srb);
	if (us->SM_Status.Ready)	result = SM_SCSIIrp(us, srb);

	/* if the command gets aborted by the higher layers, we need to short-circuit all other processing */
	if (test_bit(US_FLIDX_TIMED_OUT, &us->dflags))
	{
		//printk("-- command was aborted\n");
		srb->result = DID_ABORT << 16;
		goto Handle_Errors;
	}

	/* if there is a transport error, reset and don't auto-sense */
	if (result == USB_STOR_TRANSPORT_ERROR)
	{
		//printk("-- transport indicates error, resetting\n");
		srb->result = DID_ERROR << 16;
		goto Handle_Errors;
	}

	/* if the transport provided its own sense data, don't auto-sense */
	if (result == USB_STOR_TRANSPORT_NO_SENSE)
	{
		srb->result = SAM_STAT_CHECK_CONDITION;
		return;
	}

	srb->result = SAM_STAT_GOOD;
	if (result == USB_STOR_TRANSPORT_FAILED)
	{
		//printk("-- transport indicates command failure\n");
		//need_auto_sense = 1;
		BuildSenseBuffer(srb, us->SrbStatus);
		srb->result = SAM_STAT_CHECK_CONDITION;
	}

	/* Did we transfer less than the minimum amount required? */
	if (srb->result == SAM_STAT_GOOD && scsi_bufflen(srb) - scsi_get_resid(srb) < srb->underflow)
		srb->result = (DID_ERROR << 16);//v02 | (SUGGEST_RETRY << 24);

	return;

Handle_Errors:
	scsi_lock(us_to_host(us));
	set_bit(US_FLIDX_RESETTING, &us->dflags);
	clear_bit(US_FLIDX_ABORTING, &us->dflags);
	scsi_unlock(us_to_host(us));

	mutex_unlock(&us->dev_mutex);
	result = usb_stor_port_reset(us);
	mutex_lock(&us->dev_mutex);

	if (result < 0)
	{
		scsi_lock(us_to_host(us));
		usb_stor_report_device_reset(us);
		scsi_unlock(us_to_host(us));
		us->transport_reset(us);
	}
	clear_bit(US_FLIDX_RESETTING, &us->dflags);
}

//----- BuildSenseBuffer() -------------------------------------------
void BuildSenseBuffer(struct scsi_cmnd *srb, int SrbStatus)
{
    BYTE    *buf = srb->sense_buffer;
    BYTE    asc;

    printk("transport --- BuildSenseBuffer\n");
    switch (SrbStatus)
    {
        case SS_NOT_READY:        asc = 0x3a;    break;  // sense key = 0x02
        case SS_MEDIUM_ERR:       asc = 0x0c;    break;  // sense key = 0x03
        case SS_ILLEGAL_REQUEST:  asc = 0x20;    break;  // sense key = 0x05
        default:                  asc = 0x00;    break;  // ??
    }

    memset(buf, 0, 18);
    buf[0x00] = 0xf0;
    buf[0x02] = SrbStatus;
    buf[0x07] = 0x0b;
    buf[0x0c] = asc;
}

//----- usb_stor_stop_transport() ---------------------
void usb_stor_stop_transport(struct us_data *us)
{
	//printk("transport --- usb_stor_stop_transport\n");

	if (test_and_clear_bit(US_FLIDX_URB_ACTIVE, &us->dflags))
	{
		//printk("-- cancelling URB\n");
		usb_unlink_urb(us->current_urb);
	}

	if (test_and_clear_bit(US_FLIDX_SG_ACTIVE, &us->dflags))
	{
		//printk("-- cancelling sg request\n");
		usb_sg_cancel(&us->current_sg);
	}
}

//----- usb_stor_Bulk_max_lun() ---------------------
int usb_stor_Bulk_max_lun(struct us_data *us)
{
	int result;

	//printk("transport --- usb_stor_Bulk_max_lun\n");
	/* issue the command */
	us->iobuf[0] = 0;
	result = usb_stor_control_msg(us, us->recv_ctrl_pipe,
				 US_BULK_GET_MAX_LUN,
				 USB_DIR_IN | USB_TYPE_CLASS |
				 USB_RECIP_INTERFACE,
				 0, us->ifnum, us->iobuf, 1, HZ);

	//printk("GetMaxLUN command result is %d, data is %d\n", result, us->iobuf[0]);

	/* if we have a successful request, return the result */
	if (result > 0)
		return us->iobuf[0];

	return 0;
}

//----- usb_stor_Bulk_transport() ---------------------
int usb_stor_Bulk_transport(struct scsi_cmnd *srb, struct us_data *us)
{
	struct bulk_cb_wrap *bcb = (struct bulk_cb_wrap *) us->iobuf;
	struct bulk_cs_wrap *bcs = (struct bulk_cs_wrap *) us->iobuf;
	unsigned int transfer_length = scsi_bufflen(srb);
	unsigned int residue;
	int result;
	int fake_sense = 0;
	unsigned int cswlen;
	unsigned int cbwlen = US_BULK_CB_WRAP_LEN;

	//printk("transport --- usb_stor_Bulk_transport\n");
	/* Take care of BULK32 devices; set extra byte to 0 */
	if (unlikely(us->fflags & US_FL_BULK32))
	{
		cbwlen = 32;
		us->iobuf[31] = 0;
	}

	/* set up the command wrapper */
	bcb->Signature = cpu_to_le32(US_BULK_CB_SIGN);
	bcb->DataTransferLength = cpu_to_le32(transfer_length);
	bcb->Flags = srb->sc_data_direction == DMA_FROM_DEVICE ? 1 << 7 : 0;
	bcb->Tag = ++us->tag;
	bcb->Lun = srb->device->lun;
	if (us->fflags & US_FL_SCM_MULT_TARG)
		bcb->Lun |= srb->device->id << 4;
	bcb->Length = srb->cmd_len;

	/* copy the command payload */
	memset(bcb->CDB, 0, sizeof(bcb->CDB));
	memcpy(bcb->CDB, srb->cmnd, bcb->Length);

	// send command
	/* send it to out endpoint */
	/*printk("Bulk Command S 0x%x T 0x%x L %d F %d Trg %d LUN %d CL %d\n",
			le32_to_cpu(bcb->Signature), bcb->Tag,
			le32_to_cpu(bcb->DataTransferLength), bcb->Flags,
			(bcb->Lun >> 4), (bcb->Lun & 0x0F),
			bcb->Length);*/
	result = usb_stor_bulk_transfer_buf(us, us->send_bulk_pipe, bcb, cbwlen, NULL);
	//printk("Bulk command transfer result=%d\n", result);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	if (unlikely(us->fflags & US_FL_GO_SLOW))
		udelay(125);

	// R/W data
	if (transfer_length)
	{
		unsigned int pipe = srb->sc_data_direction == DMA_FROM_DEVICE ? us->recv_bulk_pipe : us->send_bulk_pipe;
		result = usb_stor_bulk_srb(us, pipe, srb);
		//printk("Bulk data transfer result 0x%x\n", result);
		if (result == USB_STOR_XFER_ERROR)
			return USB_STOR_TRANSPORT_ERROR;

		if (result == USB_STOR_XFER_LONG)
			fake_sense = 1;
	}

	/* get CSW for device status */
	//printk("Attempting to get CSW...\n");
	result = usb_stor_bulk_transfer_buf(us, us->recv_bulk_pipe, bcs, US_BULK_CS_WRAP_LEN, &cswlen);

	if (result == USB_STOR_XFER_SHORT && cswlen == 0)
	{
		//printk("Received 0-length CSW; retrying...\n");
		result = usb_stor_bulk_transfer_buf(us, us->recv_bulk_pipe, bcs, US_BULK_CS_WRAP_LEN, &cswlen);
	}

	/* did the attempt to read the CSW fail? */
	if (result == USB_STOR_XFER_STALLED)
	{
		/* get the status again */
		//printk("Attempting to get CSW (2nd try)...\n");
		result = usb_stor_bulk_transfer_buf(us, us->recv_bulk_pipe, bcs, US_BULK_CS_WRAP_LEN, NULL);
	}

	/* if we still have a failure at this point, we're in trouble */
	//printk("Bulk status result = %d\n", result);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	/* check bulk status */
	residue = le32_to_cpu(bcs->Residue);
	//printk("Bulk Status S 0x%x T 0x%x R %u Stat 0x%x\n", le32_to_cpu(bcs->Signature), bcs->Tag, residue, bcs->Status);
	if (!(bcs->Tag == us->tag || (us->fflags & US_FL_BULK_IGNORE_TAG)) || bcs->Status > US_BULK_STAT_PHASE)
	{
		//printk("Bulk logical error\n");
		return USB_STOR_TRANSPORT_ERROR;
	}

	if (!us->bcs_signature)
	{
		us->bcs_signature = bcs->Signature;
		//if (us->bcs_signature != cpu_to_le32(US_BULK_CS_SIGN))
		//	printk("Learnt BCS signature 0x%08X\n", le32_to_cpu(us->bcs_signature));
	}
	else if (bcs->Signature != us->bcs_signature)
	{
		/*printk("Signature mismatch: got %08X, expecting %08X\n",
			  le32_to_cpu(bcs->Signature),
			  le32_to_cpu(us->bcs_signature));*/
		return USB_STOR_TRANSPORT_ERROR;
	}

	/* try to compute the actual residue, based on how much data
	 * was really transferred and what the device tells us */
	if (residue && !(us->fflags & US_FL_IGNORE_RESIDUE))
	{

		/* Heuristically detect devices that generate bogus residues
		 * by seeing what happens with INQUIRY and READ CAPACITY
		 * commands.
		 */
		if (bcs->Status == US_BULK_STAT_OK &&
				scsi_get_resid(srb) == 0 &&
					((srb->cmnd[0] == INQUIRY &&
						transfer_length == 36) ||
					(srb->cmnd[0] == READ_CAPACITY &&
						transfer_length == 8)))
		{
			us->fflags |= US_FL_IGNORE_RESIDUE;

		}
		else
		{
			residue = min(residue, transfer_length);
			scsi_set_resid(srb, max(scsi_get_resid(srb), (int) residue));
		}
	}

	/* based on the status code, we report good or bad */
	switch (bcs->Status)
	{
		case US_BULK_STAT_OK:
			if (fake_sense)
			{
				memcpy(srb->sense_buffer, usb_stor_sense_invalidCDB, sizeof(usb_stor_sense_invalidCDB));
				return USB_STOR_TRANSPORT_NO_SENSE;
			}
			return USB_STOR_TRANSPORT_GOOD;

		case US_BULK_STAT_FAIL:
			return USB_STOR_TRANSPORT_FAILED;

		case US_BULK_STAT_PHASE:
			return USB_STOR_TRANSPORT_ERROR;
	}
	return USB_STOR_TRANSPORT_ERROR;
}

/***********************************************************************
 * Reset routines
 ***********************************************************************/
//----- usb_stor_reset_common() ---------------------
static int usb_stor_reset_common(struct us_data *us,
		u8 request, u8 requesttype,
		u16 value, u16 index, void *data, u16 size)
{
	int result;
	int result2;

	//printk("transport --- usb_stor_reset_common\n");
	if (test_bit(US_FLIDX_DISCONNECTING, &us->dflags))
	{
		//printk("No reset during disconnect\n");
		return -EIO;
	}

	result = usb_stor_control_msg(us, us->send_ctrl_pipe, request, requesttype, value, index, data, size,	5*HZ);
	if (result < 0)
	{
		//printk("Soft reset failed: %d\n", result);
		return result;
	}

	wait_event_interruptible_timeout(us->delay_wait, test_bit(US_FLIDX_DISCONNECTING, &us->dflags),	HZ*6);
	if (test_bit(US_FLIDX_DISCONNECTING, &us->dflags))
	{
		//printk("Reset interrupted by disconnect\n");
		return -EIO;
	}

	//printk("Soft reset: clearing bulk-in endpoint halt\n");
	result = usb_stor_clear_halt(us, us->recv_bulk_pipe);

	//printk("Soft reset: clearing bulk-out endpoint halt\n");
	result2 = usb_stor_clear_halt(us, us->send_bulk_pipe);

	/* return a result code based on the result of the clear-halts */
	if (result >= 0)
		result = result2;
	//if (result < 0)
	//	printk("Soft reset failed\n");
	//else
	//	printk("Soft reset done\n");
	return result;
}

//----- usb_stor_Bulk_reset() ---------------------
int usb_stor_Bulk_reset(struct us_data *us)
{
	//printk("transport --- usb_stor_Bulk_reset\n");
	return usb_stor_reset_common(us, US_BULK_RESET_REQUEST,
				 USB_TYPE_CLASS | USB_RECIP_INTERFACE,
				 0, us->ifnum, NULL, 0);
}

//----- usb_stor_port_reset() ---------------------
int usb_stor_port_reset(struct us_data *us)
{
	int result, rc_lock;

	//printk("transport --- usb_stor_port_reset\n");
	result = usb_lock_device_for_reset(us->pusb_dev, us->pusb_intf);
	if (result < 0)
		printk("unable to lock device for reset: %d\n", result);
	else {
		/* Were we disconnected while waiting for the lock? */
		if (test_bit(US_FLIDX_DISCONNECTING, &us->dflags)) {
			result = -EIO;
			//printk("No reset during disconnect\n");
		} else {
			result = usb_reset_device(us->pusb_dev);
			//printk("usb_reset_composite_device returns %d\n", result);
		}
		usb_unlock_device(us->pusb_dev);
	}
	return result;
}



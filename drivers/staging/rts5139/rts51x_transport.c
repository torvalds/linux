/* Driver for Realtek RTS51xx USB card reader
 *
 * Copyright(c) 2009 Realtek Semiconductor Corp. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author:
 *   wwang (wei_wang@realsil.com.cn)
 *   No. 450, Shenhu Road, Suzhou Industry Park, Suzhou, China
 * Maintainer:
 *   Edwin Rong (edwin_rong@realsil.com.cn)
 *   No. 450, Shenhu Road, Suzhou Industry Park, Suzhou, China
 */

#include <linux/blkdev.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include <scsi/scsi.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_device.h>

#include "debug.h"
#include "rts51x.h"
#include "rts51x_chip.h"
#include "rts51x_card.h"
#include "rts51x_scsi.h"
#include "rts51x_transport.h"
#include "trace.h"

/***********************************************************************
 * Scatter-gather transfer buffer access routines
 ***********************************************************************/

/* Copy a buffer of length buflen to/from the srb's transfer buffer.
 * Update the **sgptr and *offset variables so that the next copy will
 * pick up from where this one left off.
 */

unsigned int rts51x_access_sglist(unsigned char *buffer,
				  unsigned int buflen, void *sglist,
				  void **sgptr, unsigned int *offset,
				  enum xfer_buf_dir dir)
{
	unsigned int cnt;
	struct scatterlist *sg = (struct scatterlist *)*sgptr;

	/* We have to go through the list one entry
	 * at a time.  Each s-g entry contains some number of pages, and
	 * each page has to be kmap()'ed separately.  If the page is already
	 * in kernel-addressable memory then kmap() will return its address.
	 * If the page is not directly accessible -- such as a user buffer
	 * located in high memory -- then kmap() will map it to a temporary
	 * position in the kernel's virtual address space.
	 */

	if (!sg)
		sg = (struct scatterlist *)sglist;

	/* This loop handles a single s-g list entry, which may
	 * include multiple pages.  Find the initial page structure
	 * and the starting offset within the page, and update
	 * the *offset and **sgptr values for the next loop.
	 */
	cnt = 0;
	while (cnt < buflen && sg) {
		struct page *page = sg_page(sg) +
		    ((sg->offset + *offset) >> PAGE_SHIFT);
		unsigned int poff = (sg->offset + *offset) & (PAGE_SIZE - 1);
		unsigned int sglen = sg->length - *offset;

		if (sglen > buflen - cnt) {

			/* Transfer ends within this s-g entry */
			sglen = buflen - cnt;
			*offset += sglen;
		} else {

			/* Transfer continues to next s-g entry */
			*offset = 0;
			sg = sg_next(sg);
		}

		/* Transfer the data for all the pages in this
		 * s-g entry.  For each page: call kmap(), do the
		 * transfer, and call kunmap() immediately after. */
		while (sglen > 0) {
			unsigned int plen = min(sglen, (unsigned int)
						PAGE_SIZE - poff);
			unsigned char *ptr = kmap(page);

			if (dir == TO_XFER_BUF)
				memcpy(ptr + poff, buffer + cnt, plen);
			else
				memcpy(buffer + cnt, ptr + poff, plen);
			kunmap(page);

			/* Start at the beginning of the next page */
			poff = 0;
			++page;
			cnt += plen;
			sglen -= plen;
		}
	}
	*sgptr = sg;

	/* Return the amount actually transferred */
	return cnt;
}

unsigned int rts51x_access_xfer_buf(unsigned char *buffer,
				    unsigned int buflen, struct scsi_cmnd *srb,
				    struct scatterlist **sgptr,
				    unsigned int *offset, enum xfer_buf_dir dir)
{
	return rts51x_access_sglist(buffer, buflen, (void *)scsi_sglist(srb),
				    (void **)sgptr, offset, dir);
}

/* Store the contents of buffer into srb's transfer buffer and set the
 * SCSI residue.
 */
void rts51x_set_xfer_buf(unsigned char *buffer,
			 unsigned int buflen, struct scsi_cmnd *srb)
{
	unsigned int offset = 0;
	struct scatterlist *sg = NULL;

	buflen = min(buflen, scsi_bufflen(srb));
	buflen = rts51x_access_xfer_buf(buffer, buflen, srb, &sg, &offset,
					TO_XFER_BUF);
	if (buflen < scsi_bufflen(srb))
		scsi_set_resid(srb, scsi_bufflen(srb) - buflen);
}

void rts51x_get_xfer_buf(unsigned char *buffer,
			 unsigned int buflen, struct scsi_cmnd *srb)
{
	unsigned int offset = 0;
	struct scatterlist *sg = NULL;

	buflen = min(buflen, scsi_bufflen(srb));
	buflen = rts51x_access_xfer_buf(buffer, buflen, srb, &sg, &offset,
					FROM_XFER_BUF);
	if (buflen < scsi_bufflen(srb))
		scsi_set_resid(srb, scsi_bufflen(srb) - buflen);
}

/* This is the completion handler which will wake us up when an URB
 * completes.
 */
static void urb_done_completion(struct urb *urb)
{
	struct completion *urb_done_ptr = urb->context;

	if (urb_done_ptr)
		complete(urb_done_ptr);
}

/* This is the common part of the URB message submission code
 *
 * All URBs from the driver involved in handling a queued scsi
 * command _must_ pass through this function (or something like it) for the
 * abort mechanisms to work properly.
 */
static int rts51x_msg_common(struct rts51x_chip *chip, struct urb *urb,
			     int timeout)
{
	struct rts51x_usb *rts51x = chip->usb;
	struct completion urb_done;
	long timeleft;
	int status;

	/* don't submit URBs during abort processing */
	if (test_bit(FLIDX_ABORTING, &rts51x->dflags))
		TRACE_RET(chip, -EIO);

	/* set up data structures for the wakeup system */
	init_completion(&urb_done);

	/* fill the common fields in the URB */
	urb->context = &urb_done;
	urb->actual_length = 0;
	urb->error_count = 0;
	urb->status = 0;

	/* we assume that if transfer_buffer isn't us->iobuf then it
	 * hasn't been mapped for DMA.  Yes, this is clunky, but it's
	 * easier than always having the caller tell us whether the
	 * transfer buffer has already been mapped. */
	urb->transfer_flags = URB_NO_SETUP_DMA_MAP;
	if (urb->transfer_buffer == rts51x->iobuf) {
		urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
		urb->transfer_dma = rts51x->iobuf_dma;
	}
	urb->setup_dma = rts51x->cr_dma;

	/* submit the URB */
	status = usb_submit_urb(urb, GFP_NOIO);
	if (status) {
		/* something went wrong */
		TRACE_RET(chip, status);
	}

	/* since the URB has been submitted successfully, it's now okay
	 * to cancel it */
	set_bit(FLIDX_URB_ACTIVE, &rts51x->dflags);

	/* did an abort occur during the submission? */
	if (test_bit(FLIDX_ABORTING, &rts51x->dflags)) {

		/* cancel the URB, if it hasn't been cancelled already */
		if (test_and_clear_bit(FLIDX_URB_ACTIVE, &rts51x->dflags)) {
			RTS51X_DEBUGP("-- cancelling URB\n");
			usb_unlink_urb(urb);
		}
	}

	/* wait for the completion of the URB */
	timeleft =
	    wait_for_completion_interruptible_timeout(&urb_done,
						      (timeout * HZ /
						       1000) ? :
						      MAX_SCHEDULE_TIMEOUT);

	clear_bit(FLIDX_URB_ACTIVE, &rts51x->dflags);

	if (timeleft <= 0) {
		RTS51X_DEBUGP("%s -- cancelling URB\n",
			       timeleft == 0 ? "Timeout" : "Signal");
		usb_kill_urb(urb);
		if (timeleft == 0)
			status = -ETIMEDOUT;
		else
			status = -EINTR;
	} else {
		status = urb->status;
	}

	return status;
}

/*
 * Interpret the results of a URB transfer
 */
static int interpret_urb_result(struct rts51x_chip *chip, unsigned int pipe,
				unsigned int length, int result,
				unsigned int partial)
{
	int retval = STATUS_SUCCESS;

	/* RTS51X_DEBUGP("Status code %d; transferred %u/%u\n",
				result, partial, length); */
	switch (result) {
		/* no error code; did we send all the data? */
	case 0:
		if (partial != length) {
			RTS51X_DEBUGP("-- short transfer\n");
			TRACE_RET(chip, STATUS_TRANS_SHORT);
		}
		/* RTS51X_DEBUGP("-- transfer complete\n"); */
		return STATUS_SUCCESS;
		/* stalled */
	case -EPIPE:
		/* for control endpoints, (used by CB[I]) a stall indicates
		 * a failed command */
		if (usb_pipecontrol(pipe)) {
			RTS51X_DEBUGP("-- stall on control pipe\n");
			TRACE_RET(chip, STATUS_STALLED);
		}
		/* for other sorts of endpoint, clear the stall */
		RTS51X_DEBUGP("clearing endpoint halt for pipe 0x%x\n", pipe);
		if (rts51x_clear_halt(chip, pipe) < 0)
			TRACE_RET(chip, STATUS_ERROR);
		retval = STATUS_STALLED;
		TRACE_GOTO(chip, Exit);

		/* babble - the device tried to send more than
		 * we wanted to read */
	case -EOVERFLOW:
		RTS51X_DEBUGP("-- babble\n");
		retval = STATUS_TRANS_LONG;
		TRACE_GOTO(chip, Exit);

		/* the transfer was cancelled by abort,
		 * disconnect, or timeout */
	case -ECONNRESET:
		RTS51X_DEBUGP("-- transfer cancelled\n");
		retval = STATUS_ERROR;
		TRACE_GOTO(chip, Exit);

		/* short scatter-gather read transfer */
	case -EREMOTEIO:
		RTS51X_DEBUGP("-- short read transfer\n");
		retval = STATUS_TRANS_SHORT;
		TRACE_GOTO(chip, Exit);

		/* abort or disconnect in progress */
	case -EIO:
		RTS51X_DEBUGP("-- abort or disconnect in progress\n");
		retval = STATUS_ERROR;
		TRACE_GOTO(chip, Exit);

	case -ETIMEDOUT:
		RTS51X_DEBUGP("-- time out\n");
		retval = STATUS_TIMEDOUT;
		TRACE_GOTO(chip, Exit);

		/* the catch-all error case */
	default:
		RTS51X_DEBUGP("-- unknown error\n");
		retval = STATUS_ERROR;
		TRACE_GOTO(chip, Exit);
	}

Exit:
	if ((retval != STATUS_SUCCESS) && !usb_pipecontrol(pipe))
		rts51x_clear_hw_error(chip);

	return retval;
}

int rts51x_ctrl_transfer(struct rts51x_chip *chip, unsigned int pipe,
			 u8 request, u8 requesttype, u16 value, u16 index,
			 void *data, u16 size, int timeout)
{
	struct rts51x_usb *rts51x = chip->usb;
	int result;

	RTS51X_DEBUGP("%s: rq=%02x rqtype=%02x value=%04x index=%02x len=%u\n",
		       __func__, request, requesttype, value, index, size);

	/* fill in the devrequest structure */
	rts51x->cr->bRequestType = requesttype;
	rts51x->cr->bRequest = request;
	rts51x->cr->wValue = cpu_to_le16(value);
	rts51x->cr->wIndex = cpu_to_le16(index);
	rts51x->cr->wLength = cpu_to_le16(size);

	/* fill and submit the URB */
	usb_fill_control_urb(rts51x->current_urb, rts51x->pusb_dev, pipe,
			     (unsigned char *)rts51x->cr, data, size,
			     urb_done_completion, NULL);
	result = rts51x_msg_common(chip, rts51x->current_urb, timeout);

	return interpret_urb_result(chip, pipe, size, result,
				    rts51x->current_urb->actual_length);
}

int rts51x_clear_halt(struct rts51x_chip *chip, unsigned int pipe)
{
	int result;
	int endp = usb_pipeendpoint(pipe);

	if (usb_pipein(pipe))
		endp |= USB_DIR_IN;

	result = rts51x_ctrl_transfer(chip, SND_CTRL_PIPE(chip),
				      USB_REQ_CLEAR_FEATURE, USB_RECIP_ENDPOINT,
				      USB_ENDPOINT_HALT, endp, NULL, 0, 3000);
	if (result != STATUS_SUCCESS)
		TRACE_RET(chip, STATUS_FAIL);

	usb_reset_endpoint(chip->usb->pusb_dev, endp);

	return STATUS_SUCCESS;
}

int rts51x_reset_pipe(struct rts51x_chip *chip, char pipe)
{
	return rts51x_clear_halt(chip, pipe);
}

static void rts51x_sg_clean(struct usb_sg_request *io)
{
	if (io->urbs) {
		while (io->entries--)
			usb_free_urb(io->urbs[io->entries]);
		kfree(io->urbs);
		io->urbs = NULL;
	}
	io->dev = NULL;
}

int rts51x_sg_init(struct usb_sg_request *io, struct usb_device *dev,
		   unsigned pipe, unsigned period, struct scatterlist *sg,
		   int nents, size_t length, gfp_t mem_flags)
{
	return usb_sg_init(io, dev, pipe, period, sg, nents, length, mem_flags);
}

int rts51x_sg_wait(struct usb_sg_request *io, int timeout)
{
	long timeleft;
	int i;
	int entries = io->entries;

	/* queue the urbs.  */
	spin_lock_irq(&io->lock);
	i = 0;
	while (i < entries && !io->status) {
		int retval;

		io->urbs[i]->dev = io->dev;
		retval = usb_submit_urb(io->urbs[i], GFP_ATOMIC);

		/* after we submit, let completions or cancelations fire;
		 * we handshake using io->status.
		 */
		spin_unlock_irq(&io->lock);
		switch (retval) {
			/* maybe we retrying will recover */
		case -ENXIO:	/* hc didn't queue this one */
		case -EAGAIN:
		case -ENOMEM:
			io->urbs[i]->dev = NULL;
			retval = 0;
			yield();
			break;

			/* no error? continue immediately.
			 *
			 * NOTE: to work better with UHCI (4K I/O buffer may
			 * need 3K of TDs) it may be good to limit how many
			 * URBs are queued at once; N milliseconds?
			 */
		case 0:
			++i;
			cpu_relax();
			break;

			/* fail any uncompleted urbs */
		default:
			io->urbs[i]->dev = NULL;
			io->urbs[i]->status = retval;
			dev_dbg(&io->dev->dev, "%s, submit --> %d\n",
				__func__, retval);
			usb_sg_cancel(io);
		}
		spin_lock_irq(&io->lock);
		if (retval && (io->status == 0 || io->status == -ECONNRESET))
			io->status = retval;
	}
	io->count -= entries - i;
	if (io->count == 0)
		complete(&io->complete);
	spin_unlock_irq(&io->lock);

	timeleft =
	    wait_for_completion_interruptible_timeout(&io->complete,
						      (timeout * HZ /
						       1000) ? :
						      MAX_SCHEDULE_TIMEOUT);
	if (timeleft <= 0) {
		RTS51X_DEBUGP("%s -- cancelling SG request\n",
			       timeleft == 0 ? "Timeout" : "Signal");
		usb_sg_cancel(io);
		if (timeleft == 0)
			io->status = -ETIMEDOUT;
		else
			io->status = -EINTR;
	}

	rts51x_sg_clean(io);
	return io->status;
}

/*
 * Transfer a scatter-gather list via bulk transfer
 *
 * This function does basically the same thing as usb_stor_bulk_transfer_buf()
 * above, but it uses the usbcore scatter-gather library.
 */
static int rts51x_bulk_transfer_sglist(struct rts51x_chip *chip,
				       unsigned int pipe,
				       struct scatterlist *sg, int num_sg,
				       unsigned int length,
				       unsigned int *act_len, int timeout)
{
	int result;

	/* don't submit s-g requests during abort processing */
	if (test_bit(FLIDX_ABORTING, &chip->usb->dflags))
		TRACE_RET(chip, STATUS_ERROR);

	/* initialize the scatter-gather request block */
	RTS51X_DEBUGP("%s: xfer %u bytes, %d entries\n", __func__,
		       length, num_sg);
	result =
	    rts51x_sg_init(&chip->usb->current_sg, chip->usb->pusb_dev, pipe, 0,
			   sg, num_sg, length, GFP_NOIO);
	if (result) {
		RTS51X_DEBUGP("rts51x_sg_init returned %d\n", result);
		TRACE_RET(chip, STATUS_ERROR);
	}

	/* since the block has been initialized successfully, it's now
	 * okay to cancel it */
	set_bit(FLIDX_SG_ACTIVE, &chip->usb->dflags);

	/* did an abort occur during the submission? */
	if (test_bit(FLIDX_ABORTING, &chip->usb->dflags)) {

		/* cancel the request, if it hasn't been cancelled already */
		if (test_and_clear_bit(FLIDX_SG_ACTIVE, &chip->usb->dflags)) {
			RTS51X_DEBUGP("-- cancelling sg request\n");
			usb_sg_cancel(&chip->usb->current_sg);
		}
	}

	/* wait for the completion of the transfer */
	result = rts51x_sg_wait(&chip->usb->current_sg, timeout);

	clear_bit(FLIDX_SG_ACTIVE, &chip->usb->dflags);

	/* result = us->current_sg.status; */
	if (act_len)
		*act_len = chip->usb->current_sg.bytes;
	return interpret_urb_result(chip, pipe, length, result,
				    chip->usb->current_sg.bytes);
}

int rts51x_bulk_transfer_buf(struct rts51x_chip *chip, unsigned int pipe,
			     void *buf, unsigned int length,
			     unsigned int *act_len, int timeout)
{
	int result;

	/* fill and submit the URB */
	usb_fill_bulk_urb(chip->usb->current_urb, chip->usb->pusb_dev, pipe,
			  buf, length, urb_done_completion, NULL);
	result = rts51x_msg_common(chip, chip->usb->current_urb, timeout);

	/* store the actual length of the data transferred */
	if (act_len)
		*act_len = chip->usb->current_urb->actual_length;
	return interpret_urb_result(chip, pipe, length, result,
				    chip->usb->current_urb->actual_length);
}

int rts51x_transfer_data(struct rts51x_chip *chip, unsigned int pipe,
			 void *buf, unsigned int len, int use_sg,
			 unsigned int *act_len, int timeout)
{
	int result;

	if (timeout < 600)
		timeout = 600;

	if (use_sg) {
		result =
		    rts51x_bulk_transfer_sglist(chip, pipe,
						(struct scatterlist *)buf,
						use_sg, len, act_len, timeout);
	} else {
		result =
		    rts51x_bulk_transfer_buf(chip, pipe, buf, len, act_len,
					     timeout);
	}

	return result;
}

int rts51x_transfer_data_partial(struct rts51x_chip *chip, unsigned int pipe,
				 void *buf, void **ptr, unsigned int *offset,
				 unsigned int len, int use_sg,
				 unsigned int *act_len, int timeout)
{
	int result;

	if (timeout < 600)
		timeout = 600;

	if (use_sg) {
		void *tmp_buf = kmalloc(len, GFP_KERNEL);
		if (!tmp_buf)
			TRACE_RET(chip, STATUS_NOMEM);

		if (usb_pipeout(pipe)) {
			rts51x_access_sglist(tmp_buf, len, buf, ptr, offset,
					     FROM_XFER_BUF);
		}
		result =
		    rts51x_bulk_transfer_buf(chip, pipe, tmp_buf, len, act_len,
					     timeout);
		if (result == STATUS_SUCCESS) {
			if (usb_pipein(pipe)) {
				rts51x_access_sglist(tmp_buf, len, buf, ptr,
						     offset, TO_XFER_BUF);
			}
		}

		kfree(tmp_buf);
	} else {
		unsigned int step = 0;
		if (offset)
			step = *offset;
		result =
		    rts51x_bulk_transfer_buf(chip, pipe, buf + step, len,
					     act_len, timeout);
		if (act_len)
			step += *act_len;
		else
			step += len;
		if (offset)
			*offset = step;
	}

	return result;
}

int rts51x_get_epc_status(struct rts51x_chip *chip, u16 *status)
{
	unsigned int pipe = RCV_INTR_PIPE(chip);
	struct usb_host_endpoint *ep;
	struct completion urb_done;
	int result;

	if (!status)
		TRACE_RET(chip, STATUS_ERROR);

	/* set up data structures for the wakeup system */
	init_completion(&urb_done);

	ep = chip->usb->pusb_dev->ep_in[usb_pipeendpoint(pipe)];

	/* fill and submit the URB */
	/* We set interval to 1 here, so the polling interval is controlled
	 * by our polling thread */
	usb_fill_int_urb(chip->usb->intr_urb, chip->usb->pusb_dev, pipe,
			 status, 2, urb_done_completion, &urb_done, 1);

	result = rts51x_msg_common(chip, chip->usb->intr_urb, 50);

	return interpret_urb_result(chip, pipe, 2, result,
				    chip->usb->intr_urb->actual_length);
}

u8 media_not_present[] = {
	0x70, 0, 0x02, 0, 0, 0, 0, 10, 0, 0, 0, 0, 0x3A, 0, 0, 0, 0, 0 };
u8 invalid_cmd_field[] = {
	0x70, 0, 0x05, 0, 0, 0, 0, 10, 0, 0, 0, 0, 0x24, 0, 0, 0, 0, 0 };

void rts51x_invoke_transport(struct scsi_cmnd *srb, struct rts51x_chip *chip)
{
	int result;

#ifdef CONFIG_PM
	if (chip->option.ss_en) {
		if (srb->cmnd[0] == TEST_UNIT_READY) {
			if (RTS51X_CHK_STAT(chip, STAT_SS)) {
				if (check_fake_card_ready(chip,
							SCSI_LUN(srb))) {
					srb->result = SAM_STAT_GOOD;
				} else {
					srb->result = SAM_STAT_CHECK_CONDITION;
					memcpy(srb->sense_buffer,
					       media_not_present, SENSE_SIZE);
				}
				return;
			}
		} else if (srb->cmnd[0] == ALLOW_MEDIUM_REMOVAL) {
			if (RTS51X_CHK_STAT(chip, STAT_SS)) {
				int prevent = srb->cmnd[4] & 0x1;

				if (prevent) {
					srb->result = SAM_STAT_CHECK_CONDITION;
					memcpy(srb->sense_buffer,
					       invalid_cmd_field, SENSE_SIZE);
				} else {
					srb->result = SAM_STAT_GOOD;
				}
				return;
			}
		} else {
			if (RTS51X_CHK_STAT(chip, STAT_SS)
			    || RTS51X_CHK_STAT(chip, STAT_SS_PRE)) {
				/* Wake up device */
				RTS51X_DEBUGP("Try to wake up device\n");
				chip->resume_from_scsi = 1;

				rts51x_try_to_exit_ss(chip);

				if (RTS51X_CHK_STAT(chip, STAT_SS)) {
					wait_timeout(3000);

					rts51x_init_chip(chip);
					rts51x_init_cards(chip);
				}
			}
		}
	}
#endif

	result = rts51x_scsi_handler(srb, chip);

	/* if there is a transport error, reset and don't auto-sense */
	if (result == TRANSPORT_ERROR) {
		RTS51X_DEBUGP("-- transport indicates error, resetting\n");
		srb->result = DID_ERROR << 16;
		goto Handle_Errors;
	}

	srb->result = SAM_STAT_GOOD;

	/*
	 * If we have a failure, we're going to do a REQUEST_SENSE
	 * automatically.  Note that we differentiate between a command
	 * "failure" and an "error" in the transport mechanism.
	 */
	if (result == TRANSPORT_FAILED) {
		/* set the result so the higher layers expect this data */
		srb->result = SAM_STAT_CHECK_CONDITION;
		memcpy(srb->sense_buffer,
		       (unsigned char *)&(chip->sense_buffer[SCSI_LUN(srb)]),
		       sizeof(struct sense_data_t));
	}

	return;

	/* Error and abort processing: try to resynchronize with the device
	 * by issuing a port reset.  If that fails, try a class-specific
	 * device reset. */
Handle_Errors:
	return;
}

/* Driver for Microtek Scanmaker X6 USB scanner, and possibly others.
 *
 * (C) Copyright 2000 John Fremlin <vii@penguinpowered.com>
 * (C) Copyright 2000 Oliver Neukum <Oliver.Neukum@lrz.uni-muenchen.de>
 *
 * Parts shamelessly stolen from usb-storage and copyright by their
 * authors. Thanks to Matt Dharm for giving us permission!
 *
 * This driver implements a SCSI host controller driver and a USB
 * device driver. To avoid confusion, all the USB related stuff is
 * prefixed by mts_usb_ and all the SCSI stuff by mts_scsi_.
 *
 * Microtek (www.microtek.com) did not release the specifications for
 * their USB protocol to us, so we had to reverse engineer them. We
 * don't know for which models they are valid.
 *
 * The X6 USB has three bulk endpoints, one output (0x1) down which
 * commands and outgoing data are sent, and two input: 0x82 from which
 * normal data is read from the scanner (in packets of maximum 32
 * bytes) and from which the status byte is read, and 0x83 from which
 * the results of a scan (or preview) are read in up to 64 * 1024 byte
 * chunks by the Windows driver. We don't know how much it is possible
 * to read at a time from 0x83.
 *
 * It seems possible to read (with URB transfers) everything from 0x82
 * in one go, without bothering to read in 32 byte chunks.
 *
 * There seems to be an optimisation of a further READ implicit if
 * you simply read from 0x83.
 *
 * Guessed protocol:
 *
 *	Send raw SCSI command to EP 0x1
 *
 *	If there is data to receive:
 *		If the command was READ datatype=image:
 *			Read a lot of data from EP 0x83
 *		Else:
 *			Read data from EP 0x82
 *	Else:
 *		If there is data to transmit:
 *			Write it to EP 0x1
 *
 *	Read status byte from EP 0x82
 *
 * References:
 *
 * The SCSI command set for the scanner is available from
 *	ftp://ftp.microtek.com/microtek/devpack/
 *
 * Microtek NV sent us a more up to date version of the document. If
 * you want it, just send mail.
 *
 * Status:
 *
 *	Untested with multiple scanners.
 *	Untested on SMP.
 *	Untested on a bigendian machine.
 *
 * History:
 *
 *	20000417 starting history
 *	20000417 fixed load oops
 *	20000417 fixed unload oops
 *	20000419 fixed READ IMAGE detection
 *	20000424 started conversion to use URBs
 *	20000502 handled short transfers as errors
 *	20000513 rename and organisation of functions (john)
 *	20000513 added IDs for all products supported by Windows driver (john)
 *	20000514 Rewrote mts_scsi_queuecommand to use URBs (john)
 *	20000514 Version 0.0.8j
 *      20000514 Fix reporting of non-existent devices to SCSI layer (john)
 *	20000514 Added MTS_DEBUG_INT (john)
 *	20000514 Changed "usb-microtek" to "microtek" for consistency (john)
 *	20000514 Stupid bug fixes (john)
 *	20000514 Version 0.0.9j
 *	20000515 Put transfer context and URB in mts_desc (john)
 *	20000515 Added prelim turn off debugging support (john)
 *	20000515 Version 0.0.10j
 *      20000515 Fixed up URB allocation (clear URB on alloc) (john)
 *      20000515 Version 0.0.11j
 *	20000516 Removed unnecessary spinlock in mts_transfer_context (john)
 *	20000516 Removed unnecessary up on instance lock in mts_remove_nolock (john)
 *	20000516 Implemented (badly) scsi_abort (john)
 *	20000516 Version 0.0.12j
 *      20000517 Hopefully removed mts_remove_nolock quasideadlock (john)
 *      20000517 Added mts_debug_dump to print ll USB info (john)
 *	20000518 Tweaks and documentation updates (john)
 *	20000518 Version 0.0.13j
 *	20000518 Cleaned up abort handling (john)
 *	20000523 Removed scsi_command and various scsi_..._resets (john)
 *	20000523 Added unlink URB on scsi_abort, now OHCI supports it (john)
 *	20000523 Fixed last tiresome compile warning (john)
 *	20000523 Version 0.0.14j (though version 0.1 has come out?)
 *	20000602 Added primitive reset
 *	20000602 Version 0.2.0
 *	20000603 various cosmetic changes
 *	20000603 Version 0.2.1
 *	20000620 minor cosmetic changes
 *	20000620 Version 0.2.2
 *	20000822 Hopefully fixed deadlock in mts_remove_nolock()
 *	20000822 Fixed minor race in mts_transfer_cleanup()
 *	20000822 Fixed deadlock on submission error in queuecommand
 *	20000822 Version 0.2.3
 *	20000913 Reduced module size if debugging is off
 *	20000913 Version 0.2.4
 *      20010210 New abort logic
 *      20010210 Version 0.3.0
 *	20010217 Merged scatter/gather
 *	20010218 Version 0.4.0
 *	20010218 Cosmetic fixes
 *	20010218 Version 0.4.1
 *      20010306 Abort while using scatter/gather
 *      20010306 Version 0.4.2
 *      20010311 Remove all timeouts and tidy up generally (john)
 *	20010320 check return value of scsi_register()
 *	20010320 Version 0.4.3
 *	20010408 Identify version on module load.
 *	20011003 Fix multiple requests
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/random.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/usb.h>
#include <linux/proc_fs.h>

#include <linux/atomic.h>
#include <linux/blkdev.h>
#include "../../scsi/scsi.h"
#include <scsi/scsi_host.h>

#include "microtek.h"

#define DRIVER_AUTHOR "John Fremlin <vii@penguinpowered.com>, Oliver Neukum <Oliver.Neukum@lrz.uni-muenchen.de>"
#define DRIVER_DESC "Microtek Scanmaker X6 USB scanner driver"

/* Should we do debugging? */

//#define MTS_DO_DEBUG

/* USB layer driver interface */

static int mts_usb_probe(struct usb_interface *intf,
			 const struct usb_device_id *id);
static void mts_usb_disconnect(struct usb_interface *intf);

static const struct usb_device_id mts_usb_ids[];

static struct usb_driver mts_usb_driver = {
	.name =		"microtekX6",
	.probe =	mts_usb_probe,
	.disconnect =	mts_usb_disconnect,
	.id_table =	mts_usb_ids,
};


/* Internal driver stuff */

#define MTS_VERSION	"0.4.3"
#define MTS_NAME	"microtek usb (rev " MTS_VERSION "): "

#define MTS_WARNING(x...) \
	printk( KERN_WARNING MTS_NAME x )
#define MTS_ERROR(x...) \
	printk( KERN_ERR MTS_NAME x )
#define MTS_INT_ERROR(x...) \
	MTS_ERROR(x)
#define MTS_MESSAGE(x...) \
	printk( KERN_INFO MTS_NAME x )

#if defined MTS_DO_DEBUG

#define MTS_DEBUG(x...) \
	printk( KERN_DEBUG MTS_NAME x )

#define MTS_DEBUG_GOT_HERE() \
	MTS_DEBUG("got to %s:%d (%s)\n", __FILE__, (int)__LINE__, __func__ )
#define MTS_DEBUG_INT() \
	do { MTS_DEBUG_GOT_HERE(); \
	     MTS_DEBUG("transfer = 0x%x context = 0x%x\n",(int)transfer,(int)context ); \
	     MTS_DEBUG("status = 0x%x data-length = 0x%x sent = 0x%x\n",transfer->status,(int)context->data_length, (int)transfer->actual_length ); \
             mts_debug_dump(context->instance);\
	   } while(0)
#else

#define MTS_NUL_STATEMENT do { } while(0)

#define MTS_DEBUG(x...)	MTS_NUL_STATEMENT
#define MTS_DEBUG_GOT_HERE() MTS_NUL_STATEMENT
#define MTS_DEBUG_INT() MTS_NUL_STATEMENT

#endif



#define MTS_INT_INIT()\
	struct mts_transfer_context* context = (struct mts_transfer_context*)transfer->context; \
	MTS_DEBUG_INT();\

#ifdef MTS_DO_DEBUG

static inline void mts_debug_dump(struct mts_desc* desc) {
	MTS_DEBUG("desc at 0x%x: toggle = %02x%02x\n",
		  (int)desc,
		  (int)desc->usb_dev->toggle[1],(int)desc->usb_dev->toggle[0]
		);
	MTS_DEBUG("ep_out=%x ep_response=%x ep_image=%x\n",
		  usb_sndbulkpipe(desc->usb_dev,desc->ep_out),
		  usb_rcvbulkpipe(desc->usb_dev,desc->ep_response),
		  usb_rcvbulkpipe(desc->usb_dev,desc->ep_image)
		);
}


static inline void mts_show_command(struct scsi_cmnd *srb)
{
	char *what = NULL;

	switch (srb->cmnd[0]) {
	case TEST_UNIT_READY: what = "TEST_UNIT_READY"; break;
	case REZERO_UNIT: what = "REZERO_UNIT"; break;
	case REQUEST_SENSE: what = "REQUEST_SENSE"; break;
	case FORMAT_UNIT: what = "FORMAT_UNIT"; break;
	case READ_BLOCK_LIMITS: what = "READ_BLOCK_LIMITS"; break;
	case REASSIGN_BLOCKS: what = "REASSIGN_BLOCKS"; break;
	case READ_6: what = "READ_6"; break;
	case WRITE_6: what = "WRITE_6"; break;
	case SEEK_6: what = "SEEK_6"; break;
	case READ_REVERSE: what = "READ_REVERSE"; break;
	case WRITE_FILEMARKS: what = "WRITE_FILEMARKS"; break;
	case SPACE: what = "SPACE"; break;
	case INQUIRY: what = "INQUIRY"; break;
	case RECOVER_BUFFERED_DATA: what = "RECOVER_BUFFERED_DATA"; break;
	case MODE_SELECT: what = "MODE_SELECT"; break;
	case RESERVE: what = "RESERVE"; break;
	case RELEASE: what = "RELEASE"; break;
	case COPY: what = "COPY"; break;
	case ERASE: what = "ERASE"; break;
	case MODE_SENSE: what = "MODE_SENSE"; break;
	case START_STOP: what = "START_STOP"; break;
	case RECEIVE_DIAGNOSTIC: what = "RECEIVE_DIAGNOSTIC"; break;
	case SEND_DIAGNOSTIC: what = "SEND_DIAGNOSTIC"; break;
	case ALLOW_MEDIUM_REMOVAL: what = "ALLOW_MEDIUM_REMOVAL"; break;
	case SET_WINDOW: what = "SET_WINDOW"; break;
	case READ_CAPACITY: what = "READ_CAPACITY"; break;
	case READ_10: what = "READ_10"; break;
	case WRITE_10: what = "WRITE_10"; break;
	case SEEK_10: what = "SEEK_10"; break;
	case WRITE_VERIFY: what = "WRITE_VERIFY"; break;
	case VERIFY: what = "VERIFY"; break;
	case SEARCH_HIGH: what = "SEARCH_HIGH"; break;
	case SEARCH_EQUAL: what = "SEARCH_EQUAL"; break;
	case SEARCH_LOW: what = "SEARCH_LOW"; break;
	case SET_LIMITS: what = "SET_LIMITS"; break;
	case READ_POSITION: what = "READ_POSITION"; break;
	case SYNCHRONIZE_CACHE: what = "SYNCHRONIZE_CACHE"; break;
	case LOCK_UNLOCK_CACHE: what = "LOCK_UNLOCK_CACHE"; break;
	case READ_DEFECT_DATA: what = "READ_DEFECT_DATA"; break;
	case MEDIUM_SCAN: what = "MEDIUM_SCAN"; break;
	case COMPARE: what = "COMPARE"; break;
	case COPY_VERIFY: what = "COPY_VERIFY"; break;
	case WRITE_BUFFER: what = "WRITE_BUFFER"; break;
	case READ_BUFFER: what = "READ_BUFFER"; break;
	case UPDATE_BLOCK: what = "UPDATE_BLOCK"; break;
	case READ_LONG: what = "READ_LONG"; break;
	case WRITE_LONG: what = "WRITE_LONG"; break;
	case CHANGE_DEFINITION: what = "CHANGE_DEFINITION"; break;
	case WRITE_SAME: what = "WRITE_SAME"; break;
	case READ_TOC: what = "READ_TOC"; break;
	case LOG_SELECT: what = "LOG_SELECT"; break;
	case LOG_SENSE: what = "LOG_SENSE"; break;
	case MODE_SELECT_10: what = "MODE_SELECT_10"; break;
	case MODE_SENSE_10: what = "MODE_SENSE_10"; break;
	case MOVE_MEDIUM: what = "MOVE_MEDIUM"; break;
	case READ_12: what = "READ_12"; break;
	case WRITE_12: what = "WRITE_12"; break;
	case WRITE_VERIFY_12: what = "WRITE_VERIFY_12"; break;
	case SEARCH_HIGH_12: what = "SEARCH_HIGH_12"; break;
	case SEARCH_EQUAL_12: what = "SEARCH_EQUAL_12"; break;
	case SEARCH_LOW_12: what = "SEARCH_LOW_12"; break;
	case READ_ELEMENT_STATUS: what = "READ_ELEMENT_STATUS"; break;
	case SEND_VOLUME_TAG: what = "SEND_VOLUME_TAG"; break;
	case WRITE_LONG_2: what = "WRITE_LONG_2"; break;
	default:
		MTS_DEBUG("can't decode command\n");
		goto out;
		break;
	}
	MTS_DEBUG( "Command %s (%d bytes)\n", what, srb->cmd_len);

 out:
	MTS_DEBUG( "  %10ph\n", srb->cmnd);
}

#else

static inline void mts_show_command(struct scsi_cmnd * dummy)
{
}

static inline void mts_debug_dump(struct mts_desc* dummy)
{
}

#endif

static inline void mts_urb_abort(struct mts_desc* desc) {
	MTS_DEBUG_GOT_HERE();
	mts_debug_dump(desc);

	usb_kill_urb( desc->urb );
}

static int mts_slave_alloc (struct scsi_device *s)
{
	s->inquiry_len = 0x24;
	return 0;
}

static int mts_slave_configure (struct scsi_device *s)
{
	blk_queue_dma_alignment(s->request_queue, (512 - 1));
	return 0;
}

static int mts_scsi_abort(struct scsi_cmnd *srb)
{
	struct mts_desc* desc = (struct mts_desc*)(srb->device->host->hostdata[0]);

	MTS_DEBUG_GOT_HERE();

	mts_urb_abort(desc);

	return FAILED;
}

static int mts_scsi_host_reset(struct scsi_cmnd *srb)
{
	struct mts_desc* desc = (struct mts_desc*)(srb->device->host->hostdata[0]);
	int result;

	MTS_DEBUG_GOT_HERE();
	mts_debug_dump(desc);

	result = usb_lock_device_for_reset(desc->usb_dev, desc->usb_intf);
	if (result == 0) {
		result = usb_reset_device(desc->usb_dev);
		usb_unlock_device(desc->usb_dev);
	}
	return result ? FAILED : SUCCESS;
}

static int
mts_scsi_queuecommand(struct Scsi_Host *shost, struct scsi_cmnd *srb);

static void mts_transfer_cleanup( struct urb *transfer );
static void mts_do_sg(struct urb * transfer);

static inline
void mts_int_submit_urb (struct urb* transfer,
			int pipe,
			void* data,
			unsigned length,
			usb_complete_t callback )
/* Interrupt context! */

/* Holding transfer->context->lock! */
{
	int res;

	MTS_INT_INIT();

	usb_fill_bulk_urb(transfer,
		      context->instance->usb_dev,
		      pipe,
		      data,
		      length,
		      callback,
		      context
		);

	res = usb_submit_urb( transfer, GFP_ATOMIC );
	if ( unlikely(res) ) {
		MTS_INT_ERROR( "could not submit URB! Error was %d\n",(int)res );
		context->srb->result = DID_ERROR << 16;
		mts_transfer_cleanup(transfer);
	}
}


static void mts_transfer_cleanup( struct urb *transfer )
/* Interrupt context! */
{
	MTS_INT_INIT();

	if ( likely(context->final_callback != NULL) )
		context->final_callback(context->srb);
}

static void mts_transfer_done( struct urb *transfer )
{
	MTS_INT_INIT();

	context->srb->result &= MTS_SCSI_ERR_MASK;
	context->srb->result |= (unsigned)(*context->scsi_status)<<1;

	mts_transfer_cleanup(transfer);
}


static void mts_get_status( struct urb *transfer )
/* Interrupt context! */
{
	MTS_INT_INIT();

	mts_int_submit_urb(transfer,
			   usb_rcvbulkpipe(context->instance->usb_dev,
					   context->instance->ep_response),
			   context->scsi_status,
			   1,
			   mts_transfer_done );
}

static void mts_data_done( struct urb* transfer )
/* Interrupt context! */
{
	int status = transfer->status;
	MTS_INT_INIT();

	if ( context->data_length != transfer->actual_length ) {
		scsi_set_resid(context->srb, context->data_length -
			       transfer->actual_length);
	} else if ( unlikely(status) ) {
		context->srb->result = (status == -ENOENT ? DID_ABORT : DID_ERROR)<<16;
	}

	mts_get_status(transfer);
}


static void mts_command_done( struct urb *transfer )
/* Interrupt context! */
{
	int status = transfer->status;
	MTS_INT_INIT();

	if ( unlikely(status) ) {
	        if (status == -ENOENT) {
		        /* We are being killed */
			MTS_DEBUG_GOT_HERE();
			context->srb->result = DID_ABORT<<16;
                } else {
		        /* A genuine error has occurred */
			MTS_DEBUG_GOT_HERE();

		        context->srb->result = DID_ERROR<<16;
                }
		mts_transfer_cleanup(transfer);

		return;
	}

	if (context->srb->cmnd[0] == REQUEST_SENSE) {
		mts_int_submit_urb(transfer,
				   context->data_pipe,
				   context->srb->sense_buffer,
				   context->data_length,
				   mts_data_done);
	} else { if ( context->data ) {
			mts_int_submit_urb(transfer,
					   context->data_pipe,
					   context->data,
					   context->data_length,
					   scsi_sg_count(context->srb) > 1 ?
					           mts_do_sg : mts_data_done);
		} else {
			mts_get_status(transfer);
		}
	}
}

static void mts_do_sg (struct urb* transfer)
{
	struct scatterlist * sg;
	int status = transfer->status;
	MTS_INT_INIT();

	MTS_DEBUG("Processing fragment %d of %d\n", context->fragment,
	                                          scsi_sg_count(context->srb));

	if (unlikely(status)) {
                context->srb->result = (status == -ENOENT ? DID_ABORT : DID_ERROR)<<16;
		mts_transfer_cleanup(transfer);
        }

	sg = scsi_sglist(context->srb);
	context->fragment++;
	mts_int_submit_urb(transfer,
			   context->data_pipe,
			   sg_virt(&sg[context->fragment]),
			   sg[context->fragment].length,
			   context->fragment + 1 == scsi_sg_count(context->srb) ?
			   mts_data_done : mts_do_sg);
}

static const u8 mts_read_image_sig[] = { 0x28, 00, 00, 00 };
static const u8 mts_read_image_sig_len = 4;
static const unsigned char mts_direction[256/8] = {
	0x28, 0x81, 0x14, 0x14, 0x20, 0x01, 0x90, 0x77,
	0x0C, 0x20, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};


#define MTS_DIRECTION_IS_IN(x) ((mts_direction[x>>3] >> (x & 7)) & 1)

static void
mts_build_transfer_context(struct scsi_cmnd *srb, struct mts_desc* desc)
{
	int pipe;
	struct scatterlist * sg;
	
	MTS_DEBUG_GOT_HERE();

	desc->context.instance = desc;
	desc->context.srb = srb;
	desc->context.fragment = 0;

	if (!scsi_bufflen(srb)) {
		desc->context.data = NULL;
		desc->context.data_length = 0;
		return;
	} else {
		sg = scsi_sglist(srb);
		desc->context.data = sg_virt(&sg[0]);
		desc->context.data_length = sg[0].length;
	}


	/* can't rely on srb->sc_data_direction */

	/* Brutally ripped from usb-storage */

	if ( !memcmp( srb->cmnd, mts_read_image_sig, mts_read_image_sig_len )
) { 		pipe = usb_rcvbulkpipe(desc->usb_dev,desc->ep_image);
		MTS_DEBUG( "transferring from desc->ep_image == %d\n",
			   (int)desc->ep_image );
	} else if ( MTS_DIRECTION_IS_IN(srb->cmnd[0]) ) {
			pipe = usb_rcvbulkpipe(desc->usb_dev,desc->ep_response);
			MTS_DEBUG( "transferring from desc->ep_response == %d\n",
				   (int)desc->ep_response);
	} else {
		MTS_DEBUG("transferring to desc->ep_out == %d\n",
			  (int)desc->ep_out);
		pipe = usb_sndbulkpipe(desc->usb_dev,desc->ep_out);
	}
	desc->context.data_pipe = pipe;
}


static int
mts_scsi_queuecommand_lck(struct scsi_cmnd *srb, mts_scsi_cmnd_callback callback)
{
	struct mts_desc* desc = (struct mts_desc*)(srb->device->host->hostdata[0]);
	int err = 0;
	int res;

	MTS_DEBUG_GOT_HERE();
	mts_show_command(srb);
	mts_debug_dump(desc);

	if ( srb->device->lun || srb->device->id || srb->device->channel ) {

		MTS_DEBUG("Command to LUN=%d ID=%d CHANNEL=%d from SCSI layer\n",(int)srb->device->lun,(int)srb->device->id, (int)srb->device->channel );

		MTS_DEBUG("this device doesn't exist\n");

		srb->result = DID_BAD_TARGET << 16;

		if(likely(callback != NULL))
			callback(srb);

		goto out;
	}

	
	usb_fill_bulk_urb(desc->urb,
		      desc->usb_dev,
		      usb_sndbulkpipe(desc->usb_dev,desc->ep_out),
		      srb->cmnd,
		      srb->cmd_len,
		      mts_command_done,
		      &desc->context
		      );


	mts_build_transfer_context( srb, desc );
	desc->context.final_callback = callback;
	
	/* here we need ATOMIC as we are called with the iolock */
	res=usb_submit_urb(desc->urb, GFP_ATOMIC);

	if(unlikely(res)){
		MTS_ERROR("error %d submitting URB\n",(int)res);
		srb->result = DID_ERROR << 16;

		if(likely(callback != NULL))
			callback(srb);

	}
out:
	return err;
}

static DEF_SCSI_QCMD(mts_scsi_queuecommand)

static struct scsi_host_template mts_scsi_host_template = {
	.module			= THIS_MODULE,
	.name			= "microtekX6",
	.proc_name		= "microtekX6",
	.queuecommand		= mts_scsi_queuecommand,
	.eh_abort_handler	= mts_scsi_abort,
	.eh_host_reset_handler	= mts_scsi_host_reset,
	.sg_tablesize =		SG_ALL,
	.can_queue =		1,
	.this_id =		-1,
	.use_clustering =	1,
	.emulated =		1,
	.slave_alloc =		mts_slave_alloc,
	.slave_configure =	mts_slave_configure,
	.max_sectors=		256, /* 128 K */
};

/* The entries of microtek_table must correspond, line-by-line to
   the entries of mts_supported_products[]. */

static const struct usb_device_id mts_usb_ids[] =
{
	{ USB_DEVICE(0x4ce, 0x0300) },
	{ USB_DEVICE(0x5da, 0x0094) },
	{ USB_DEVICE(0x5da, 0x0099) },
	{ USB_DEVICE(0x5da, 0x009a) },
	{ USB_DEVICE(0x5da, 0x00a0) },
	{ USB_DEVICE(0x5da, 0x00a3) },
	{ USB_DEVICE(0x5da, 0x80a3) },
	{ USB_DEVICE(0x5da, 0x80ac) },
	{ USB_DEVICE(0x5da, 0x00b6) },
	{ }						/* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, mts_usb_ids);


static int mts_usb_probe(struct usb_interface *intf,
			 const struct usb_device_id *id)
{
	int i;
	int ep_out = -1;
	int ep_in_set[3]; /* this will break if we have more than three endpoints
			   which is why we check */
	int *ep_in_current = ep_in_set;
	int err_retval = -ENOMEM;

	struct mts_desc * new_desc;
	struct usb_device *dev = interface_to_usbdev (intf);

	/* the current altsetting on the interface we're probing */
	struct usb_host_interface *altsetting;

	MTS_DEBUG_GOT_HERE();
	MTS_DEBUG( "usb-device descriptor at %x\n", (int)dev );

	MTS_DEBUG( "product id = 0x%x, vendor id = 0x%x\n",
		   le16_to_cpu(dev->descriptor.idProduct),
		   le16_to_cpu(dev->descriptor.idVendor) );

	MTS_DEBUG_GOT_HERE();

	/* the current altsetting on the interface we're probing */
	altsetting = intf->cur_altsetting;


	/* Check if the config is sane */

	if ( altsetting->desc.bNumEndpoints != MTS_EP_TOTAL ) {
		MTS_WARNING( "expecting %d got %d endpoints! Bailing out.\n",
			     (int)MTS_EP_TOTAL, (int)altsetting->desc.bNumEndpoints );
		return -ENODEV;
	}

	for( i = 0; i < altsetting->desc.bNumEndpoints; i++ ) {
		if ((altsetting->endpoint[i].desc.bmAttributes &
		     USB_ENDPOINT_XFERTYPE_MASK) != USB_ENDPOINT_XFER_BULK) {

			MTS_WARNING( "can only deal with bulk endpoints; endpoint %d is not bulk.\n",
			     (int)altsetting->endpoint[i].desc.bEndpointAddress );
		} else {
			if (altsetting->endpoint[i].desc.bEndpointAddress &
			    USB_DIR_IN)
				*ep_in_current++
					= altsetting->endpoint[i].desc.bEndpointAddress &
					USB_ENDPOINT_NUMBER_MASK;
			else {
				if ( ep_out != -1 ) {
					MTS_WARNING( "can only deal with one output endpoints. Bailing out." );
					return -ENODEV;
				}

				ep_out = altsetting->endpoint[i].desc.bEndpointAddress &
					USB_ENDPOINT_NUMBER_MASK;
			}
		}

	}


	if ( ep_out == -1 ) {
		MTS_WARNING( "couldn't find an output bulk endpoint. Bailing out.\n" );
		return -ENODEV;
	}


	new_desc = kzalloc(sizeof(struct mts_desc), GFP_KERNEL);
	if (!new_desc)
		goto out;

	new_desc->urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!new_desc->urb)
		goto out_kfree;

	new_desc->context.scsi_status = kmalloc(1, GFP_KERNEL);
	if (!new_desc->context.scsi_status)
		goto out_free_urb;

	new_desc->usb_dev = dev;
	new_desc->usb_intf = intf;

	/* endpoints */
	new_desc->ep_out = ep_out;
	new_desc->ep_response = ep_in_set[0];
	new_desc->ep_image = ep_in_set[1];

	if ( new_desc->ep_out != MTS_EP_OUT )
		MTS_WARNING( "will this work? Command EP is not usually %d\n",
			     (int)new_desc->ep_out );

	if ( new_desc->ep_response != MTS_EP_RESPONSE )
		MTS_WARNING( "will this work? Response EP is not usually %d\n",
			     (int)new_desc->ep_response );

	if ( new_desc->ep_image != MTS_EP_IMAGE )
		MTS_WARNING( "will this work? Image data EP is not usually %d\n",
			     (int)new_desc->ep_image );

	new_desc->host = scsi_host_alloc(&mts_scsi_host_template,
			sizeof(new_desc));
	if (!new_desc->host)
		goto out_kfree2;

	new_desc->host->hostdata[0] = (unsigned long)new_desc;
	if (scsi_add_host(new_desc->host, &dev->dev)) {
		err_retval = -EIO;
		goto out_host_put;
	}
	scsi_scan_host(new_desc->host);

	usb_set_intfdata(intf, new_desc);
	return 0;

 out_host_put:
	scsi_host_put(new_desc->host);
 out_kfree2:
	kfree(new_desc->context.scsi_status);
 out_free_urb:
	usb_free_urb(new_desc->urb);
 out_kfree:
	kfree(new_desc);
 out:
	return err_retval;
}

static void mts_usb_disconnect (struct usb_interface *intf)
{
	struct mts_desc *desc = usb_get_intfdata(intf);

	usb_set_intfdata(intf, NULL);

	usb_kill_urb(desc->urb);
	scsi_remove_host(desc->host);

	scsi_host_put(desc->host);
	usb_free_urb(desc->urb);
	kfree(desc->context.scsi_status);
	kfree(desc);
}

module_usb_driver(mts_usb_driver);

MODULE_AUTHOR( DRIVER_AUTHOR );
MODULE_DESCRIPTION( DRIVER_DESC );
MODULE_LICENSE("GPL");

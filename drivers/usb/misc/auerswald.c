/*****************************************************************************/
/*
 *      auerswald.c  --  Auerswald PBX/System Telephone usb driver.
 *
 *      Copyright (C) 2001  Wolfgang Mües (wolfgang@iksw-muees.de)
 *
 *      Very much code of this driver is borrowed from dabusb.c (Deti Fliegl)
 *      and from the USB Skeleton driver (Greg Kroah-Hartman). Thank you.
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
 /*****************************************************************************/

/* Standard Linux module include files */
#include <asm/uaccess.h>
#include <asm/byteorder.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/wait.h>
#include <linux/usb.h>

/*-------------------------------------------------------------------*/
/* Debug support 						     */
#ifdef DEBUG
#define dump( adr, len) \
do {			\
	unsigned int u;	\
	printk (KERN_DEBUG); \
	for (u = 0; u < len; u++) \
		printk (" %02X", adr[u] & 0xFF); \
	printk ("\n"); \
} while (0)
#else
#define dump( adr, len)
#endif

/*-------------------------------------------------------------------*/
/* Version Information */
#define DRIVER_VERSION "0.9.11"
#define DRIVER_AUTHOR  "Wolfgang Mües <wolfgang@iksw-muees.de>"
#define DRIVER_DESC    "Auerswald PBX/System Telephone usb driver"

/*-------------------------------------------------------------------*/
/* Private declarations for Auerswald USB driver                     */

/* Auerswald Vendor ID */
#define ID_AUERSWALD  	0x09BF

#define AUER_MINOR_BASE	112	/* auerswald driver minor number */

/* we can have up to this number of device plugged in at once */
#define AUER_MAX_DEVICES 16


/* Number of read buffers for each device */
#define AU_RBUFFERS     10

/* Number of chain elements for each control chain */
#define AUCH_ELEMENTS   20

/* Number of retries in communication */
#define AU_RETRIES	10

/*-------------------------------------------------------------------*/
/* vendor specific protocol                                          */
/* Header Byte */
#define AUH_INDIRMASK   0x80    /* mask for direct/indirect bit */
#define AUH_DIRECT      0x00    /* data is for USB device */
#define AUH_INDIRECT    0x80    /* USB device is relay */

#define AUH_SPLITMASK   0x40    /* mask for split bit */
#define AUH_UNSPLIT     0x00    /* data block is full-size */
#define AUH_SPLIT       0x40    /* data block is part of a larger one,
                                   split-byte follows */

#define AUH_TYPEMASK    0x3F    /* mask for type of data transfer */
#define AUH_TYPESIZE    0x40    /* different types */
#define AUH_DCHANNEL    0x00    /* D channel data */
#define AUH_B1CHANNEL   0x01    /* B1 channel transparent */
#define AUH_B2CHANNEL   0x02    /* B2 channel transparent */
/*                0x03..0x0F       reserved for driver internal use */
#define AUH_COMMAND     0x10    /* Command channel */
#define AUH_BPROT       0x11    /* Configuration block protocol */
#define AUH_DPROTANA    0x12    /* D channel protocol analyzer */
#define AUH_TAPI        0x13    /* telephone api data (ATD) */
/*                0x14..0x3F       reserved for other protocols */
#define AUH_UNASSIGNED  0xFF    /* if char device has no assigned service */
#define AUH_FIRSTUSERCH 0x11    /* first channel which is available for driver users */

#define AUH_SIZE	1 	/* Size of Header Byte */

/* Split Byte. Only present if split bit in header byte set.*/
#define AUS_STARTMASK   0x80    /* mask for first block of splitted frame */
#define AUS_FIRST       0x80    /* first block */
#define AUS_FOLLOW      0x00    /* following block */

#define AUS_ENDMASK     0x40    /* mask for last block of splitted frame */
#define AUS_END         0x40    /* last block */
#define AUS_NOEND       0x00    /* not the last block */

#define AUS_LENMASK     0x3F    /* mask for block length information */

/* Request types */
#define AUT_RREQ        (USB_DIR_IN  | USB_TYPE_VENDOR | USB_RECIP_OTHER)   /* Read Request */
#define AUT_WREQ        (USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_OTHER)   /* Write Request */

/* Vendor Requests */
#define AUV_GETINFO     0x00    /* GetDeviceInfo */
#define AUV_WBLOCK      0x01    /* Write Block */
#define AUV_RBLOCK      0x02    /* Read Block */
#define AUV_CHANNELCTL  0x03    /* Channel Control */
#define AUV_DUMMY	0x04	/* Dummy Out for retry */

/* Device Info Types */
#define AUDI_NUMBCH     0x0000  /* Number of supported B channels */
#define AUDI_OUTFSIZE   0x0001  /* Size of OUT B channel fifos */
#define AUDI_MBCTRANS   0x0002  /* max. Blocklength of control transfer */

/* Interrupt endpoint definitions */
#define AU_IRQENDP      1       /* Endpoint number */
#define AU_IRQCMDID     16      /* Command-block ID */
#define AU_BLOCKRDY     0       /* Command: Block data ready on ctl endpoint */
#define AU_IRQMINSIZE	5	/* Nr. of bytes decoded in this driver */

/* Device String Descriptors */
#define AUSI_VENDOR   	1	/* "Auerswald GmbH & Co. KG" */
#define AUSI_DEVICE   	2	/* Name of the Device */
#define AUSI_SERIALNR 	3	/* Serial Number */
#define AUSI_MSN      	4	/* "MSN ..." (first) Multiple Subscriber Number */

#define AUSI_DLEN	100	/* Max. Length of Device Description */

#define AUV_RETRY	0x101	/* First Firmware version which can do control retries */

/*-------------------------------------------------------------------*/
/* External data structures / Interface                              */
typedef struct
{
	char __user *buf;	/* return buffer for string contents */
	unsigned int bsize;	/* size of return buffer */
} audevinfo_t,*paudevinfo_t;

/* IO controls */
#define IOCTL_AU_SLEN	  _IOR( 'U', 0xF0, int)         /* return the max. string descriptor length */
#define IOCTL_AU_DEVINFO  _IOWR('U', 0xF1, audevinfo_t) /* get name of a specific device */
#define IOCTL_AU_SERVREQ  _IOW( 'U', 0xF2, int) 	/* request a service channel */
#define IOCTL_AU_BUFLEN	  _IOR( 'U', 0xF3, int)         /* return the max. buffer length for the device */
#define IOCTL_AU_RXAVAIL  _IOR( 'U', 0xF4, int)         /* return != 0 if Receive Data available */
#define IOCTL_AU_CONNECT  _IOR( 'U', 0xF5, int)         /* return != 0 if connected to a service channel */
#define IOCTL_AU_TXREADY  _IOR( 'U', 0xF6, int)         /* return != 0 if Transmitt channel ready to send */
/*                              'U'  0xF7..0xFF reseved */

/*-------------------------------------------------------------------*/
/* Internal data structures                                          */

/* ..................................................................*/
/* urb chain element */
struct  auerchain;                      /* forward for circular reference */
typedef struct
{
        struct auerchain *chain;        /* pointer to the chain to which this element belongs */
        struct urb * urbp;                   /* pointer to attached urb */
        void *context;                  /* saved URB context */
        usb_complete_t complete;        /* saved URB completion function */
        struct list_head list;          /* to include element into a list */
} auerchainelement_t,*pauerchainelement_t;

/* urb chain */
typedef struct auerchain
{
        pauerchainelement_t active;     /* element which is submitted to urb */
	spinlock_t lock;                /* protection agains interrupts */
        struct list_head waiting_list;  /* list of waiting elements */
        struct list_head free_list;     /* list of available elements */
} auerchain_t,*pauerchain_t;

/* urb blocking completion helper struct */
typedef struct
{
	wait_queue_head_t wqh;    	/* wait for completion */
	unsigned int done;		/* completion flag */
} auerchain_chs_t,*pauerchain_chs_t;

/* ...................................................................*/
/* buffer element */
struct  auerbufctl;                     /* forward */
typedef struct
{
        char *bufp;                     /* reference to allocated data buffer */
        unsigned int len;               /* number of characters in data buffer */
	unsigned int retries;		/* for urb retries */
        struct usb_ctrlrequest *dr;	/* for setup data in control messages */
        struct urb * urbp;                   /* USB urb */
        struct auerbufctl *list;        /* pointer to list */
        struct list_head buff_list;     /* reference to next buffer in list */
} auerbuf_t,*pauerbuf_t;

/* buffer list control block */
typedef struct auerbufctl
{
        spinlock_t lock;                /* protection in interrupt */
        struct list_head free_buff_list;/* free buffers */
        struct list_head rec_buff_list; /* buffers with receive data */
} auerbufctl_t,*pauerbufctl_t;

/* ...................................................................*/
/* service context */
struct  auerscon;                       /* forward */
typedef void (*auer_dispatch_t)(struct auerscon*, pauerbuf_t);
typedef void (*auer_disconn_t) (struct auerscon*);
typedef struct auerscon
{
        unsigned int id;                /* protocol service id AUH_xxxx */
        auer_dispatch_t dispatch;       /* dispatch read buffer */
	auer_disconn_t disconnect;	/* disconnect from device, wake up all char readers */
} auerscon_t,*pauerscon_t;

/* ...................................................................*/
/* USB device context */
typedef struct
{
	struct semaphore 	mutex;         	    /* protection in user context */
	char 			name[20];	    /* name of the /dev/usb entry */
	unsigned int		dtindex;	    /* index in the device table */
	struct usb_device *	usbdev;      	    /* USB device handle */
	int			open_count;	    /* count the number of open character channels */
        char 			dev_desc[AUSI_DLEN];/* for storing a textual description */
        unsigned int 		maxControlLength;   /* max. Length of control paket (without header) */
        struct urb * 		inturbp;            /* interrupt urb */
        char *			intbufp;            /* data buffer for interrupt urb */
	unsigned int 		irqsize;	    /* size of interrupt endpoint 1 */
        struct auerchain 	controlchain;  	    /* for chaining of control messages */
	auerbufctl_t 		bufctl;             /* Buffer control for control transfers */
        pauerscon_t 	     	services[AUH_TYPESIZE];/* context pointers for each service */
	unsigned int		version;	    /* Version of the device */
	wait_queue_head_t 	bufferwait;         /* wait for a control buffer */
} auerswald_t,*pauerswald_t;

/* ................................................................... */
/* character device context */
typedef struct
{
	struct semaphore mutex;         /* protection in user context */
	pauerswald_t auerdev;           /* context pointer of assigned device */
        auerbufctl_t bufctl;            /* controls the buffer chain */
        auerscon_t scontext;            /* service context */
	wait_queue_head_t readwait;     /* for synchronous reading */
	struct semaphore readmutex;     /* protection against multiple reads */
	pauerbuf_t readbuf;		/* buffer held for partial reading */
	unsigned int readoffset;	/* current offset in readbuf */
	unsigned int removed;		/* is != 0 if device is removed */
} auerchar_t,*pauerchar_t;


/*-------------------------------------------------------------------*/
/* Forwards */
static void auerswald_ctrlread_complete (struct urb * urb);
static void auerswald_removeservice (pauerswald_t cp, pauerscon_t scp);
static struct usb_driver auerswald_driver;


/*-------------------------------------------------------------------*/
/* USB chain helper functions                                        */
/* --------------------------                                        */

/* completion function for chained urbs */
static void auerchain_complete (struct urb * urb)
{
	unsigned long flags;
        int result;

        /* get pointer to element and to chain */
        pauerchainelement_t acep = (pauerchainelement_t) urb->context;
        pauerchain_t         acp = acep->chain;

        /* restore original entries in urb */
        urb->context  = acep->context;
        urb->complete = acep->complete;

        dbg ("auerchain_complete called");

        /* call original completion function
           NOTE: this function may lead to more urbs submitted into the chain.
                 (no chain lock at calling complete()!)
                 acp->active != NULL is protecting us against recursion.*/
        urb->complete (urb);

        /* detach element from chain data structure */
	spin_lock_irqsave (&acp->lock, flags);
        if (acp->active != acep) /* paranoia debug check */
	        dbg ("auerchain_complete: completion on non-active element called!");
        else
                acp->active = NULL;

        /* add the used chain element to the list of free elements */
	list_add_tail (&acep->list, &acp->free_list);
        acep = NULL;

        /* is there a new element waiting in the chain? */
        if (!acp->active && !list_empty (&acp->waiting_list)) {
                /* yes: get the entry */
                struct list_head *tmp = acp->waiting_list.next;
                list_del (tmp);
                acep = list_entry (tmp, auerchainelement_t, list);
                acp->active = acep;
        }
        spin_unlock_irqrestore (&acp->lock, flags);

        /* submit the new urb */
        if (acep) {
                urb    = acep->urbp;
                dbg ("auerchain_complete: submitting next urb from chain");
		urb->status = 0;	/* needed! */
		result = usb_submit_urb(urb, GFP_ATOMIC);

                /* check for submit errors */
                if (result) {
                        urb->status = result;
                        dbg("auerchain_complete: usb_submit_urb with error code %d", result);
                        /* and do error handling via *this* completion function (recursive) */
                        auerchain_complete( urb);
                }
        } else {
                /* simple return without submitting a new urb.
                   The empty chain is detected with acp->active == NULL. */
        };
}


/* submit function for chained urbs
   this function may be called from completion context or from user space!
   early = 1 -> submit in front of chain
*/
static int auerchain_submit_urb_list (pauerchain_t acp, struct urb * urb, int early)
{
        int result;
        unsigned long flags;
        pauerchainelement_t acep = NULL;

        dbg ("auerchain_submit_urb called");

        /* try to get a chain element */
        spin_lock_irqsave (&acp->lock, flags);
        if (!list_empty (&acp->free_list)) {
                /* yes: get the entry */
                struct list_head *tmp = acp->free_list.next;
                list_del (tmp);
                acep = list_entry (tmp, auerchainelement_t, list);
        }
        spin_unlock_irqrestore (&acp->lock, flags);

        /* if no chain element available: return with error */
        if (!acep) {
                return -ENOMEM;
        }

        /* fill in the new chain element values */
        acep->chain    = acp;
        acep->context  = urb->context;
        acep->complete = urb->complete;
        acep->urbp     = urb;
        INIT_LIST_HEAD (&acep->list);

        /* modify urb */
        urb->context   = acep;
        urb->complete  = auerchain_complete;
        urb->status    = -EINPROGRESS;    /* usb_submit_urb does this, too */

        /* add element to chain - or start it immediately */
        spin_lock_irqsave (&acp->lock, flags);
        if (acp->active) {
                /* there is traffic in the chain, simple add element to chain */
		if (early) {
			dbg ("adding new urb to head of chain");
			list_add (&acep->list, &acp->waiting_list);
		} else {
			dbg ("adding new urb to end of chain");
			list_add_tail (&acep->list, &acp->waiting_list);
		}
		acep = NULL;
        } else {
                /* the chain is empty. Prepare restart */
                acp->active = acep;
        }
        /* Spin has to be removed before usb_submit_urb! */
        spin_unlock_irqrestore (&acp->lock, flags);

        /* Submit urb if immediate restart */
        if (acep) {
                dbg("submitting urb immediate");
		urb->status = 0;	/* needed! */
                result = usb_submit_urb(urb, GFP_ATOMIC);
                /* check for submit errors */
                if (result) {
                        urb->status = result;
                        dbg("auerchain_submit_urb: usb_submit_urb with error code %d", result);
                        /* and do error handling via completion function */
                        auerchain_complete( urb);
                }
        }

        return 0;
}

/* submit function for chained urbs
   this function may be called from completion context or from user space!
*/
static int auerchain_submit_urb (pauerchain_t acp, struct urb * urb)
{
	return auerchain_submit_urb_list (acp, urb, 0);
}

/* cancel an urb which is submitted to the chain
   the result is 0 if the urb is cancelled, or -EINPROGRESS if
   the function is successfully started.
*/
static int auerchain_unlink_urb (pauerchain_t acp, struct urb * urb)
{
	unsigned long flags;
        struct urb * urbp;
        pauerchainelement_t acep;
        struct list_head *tmp;

        dbg ("auerchain_unlink_urb called");

        /* search the chain of waiting elements */
        spin_lock_irqsave (&acp->lock, flags);
        list_for_each (tmp, &acp->waiting_list) {
                acep = list_entry (tmp, auerchainelement_t, list);
                if (acep->urbp == urb) {
                        list_del (tmp);
                        urb->context = acep->context;
                        urb->complete = acep->complete;
                        list_add_tail (&acep->list, &acp->free_list);
                        spin_unlock_irqrestore (&acp->lock, flags);
                        dbg ("unlink waiting urb");
                        urb->status = -ENOENT;
                        urb->complete (urb);
                        return 0;
                }
        }
        /* not found. */
        spin_unlock_irqrestore (&acp->lock, flags);

        /* get the active urb */
        acep = acp->active;
        if (acep) {
                urbp = acep->urbp;

                /* check if we have to cancel the active urb */
                if (urbp == urb) {
                        /* note that there is a race condition between the check above
                           and the unlink() call because of no lock. This race is harmless,
                           because the usb module will detect the unlink() after completion.
                           We can't use the acp->lock here because the completion function
                           wants to grab it.
			*/
                        dbg ("unlink active urb");
                        return usb_unlink_urb (urbp);
                }
        }

        /* not found anyway
           ... is some kind of success
	*/
        dbg ("urb to unlink not found in chain");
        return 0;
}

/* cancel all urbs which are in the chain.
   this function must not be called from interrupt or completion handler.
*/
static void auerchain_unlink_all (pauerchain_t acp)
{
	unsigned long flags;
        struct urb * urbp;
        pauerchainelement_t acep;

        dbg ("auerchain_unlink_all called");

        /* clear the chain of waiting elements */
        spin_lock_irqsave (&acp->lock, flags);
        while (!list_empty (&acp->waiting_list)) {
                /* get the next entry */
                struct list_head *tmp = acp->waiting_list.next;
                list_del (tmp);
                acep = list_entry (tmp, auerchainelement_t, list);
                urbp = acep->urbp;
                urbp->context = acep->context;
                urbp->complete = acep->complete;
                list_add_tail (&acep->list, &acp->free_list);
                spin_unlock_irqrestore (&acp->lock, flags);
                dbg ("unlink waiting urb");
                urbp->status = -ENOENT;
                urbp->complete (urbp);
                spin_lock_irqsave (&acp->lock, flags);
        }
        spin_unlock_irqrestore (&acp->lock, flags);

        /* clear the active urb */
        acep = acp->active;
        if (acep) {
                urbp = acep->urbp;
                dbg ("unlink active urb");
                usb_kill_urb (urbp);
        }
}


/* free the chain.
   this function must not be called from interrupt or completion handler.
*/
static void auerchain_free (pauerchain_t acp)
{
	unsigned long flags;
        pauerchainelement_t acep;

        dbg ("auerchain_free called");

        /* first, cancel all pending urbs */
        auerchain_unlink_all (acp);

        /* free the elements */
        spin_lock_irqsave (&acp->lock, flags);
        while (!list_empty (&acp->free_list)) {
                /* get the next entry */
                struct list_head *tmp = acp->free_list.next;
                list_del (tmp);
                spin_unlock_irqrestore (&acp->lock, flags);
		acep = list_entry (tmp, auerchainelement_t, list);
                kfree (acep);
	        spin_lock_irqsave (&acp->lock, flags);
	}
        spin_unlock_irqrestore (&acp->lock, flags);
}


/* Init the chain control structure */
static void auerchain_init (pauerchain_t acp)
{
        /* init the chain data structure */
        acp->active = NULL;
	spin_lock_init (&acp->lock);
        INIT_LIST_HEAD (&acp->waiting_list);
        INIT_LIST_HEAD (&acp->free_list);
}

/* setup a chain.
   It is assumed that there is no concurrency while setting up the chain
   requirement: auerchain_init()
*/
static int auerchain_setup (pauerchain_t acp, unsigned int numElements)
{
        pauerchainelement_t acep;

        dbg ("auerchain_setup called with %d elements", numElements);

        /* fill the list of free elements */
        for (;numElements; numElements--) {
                acep = kzalloc(sizeof(auerchainelement_t), GFP_KERNEL);
                if (!acep)
			goto ac_fail;
                INIT_LIST_HEAD (&acep->list);
                list_add_tail (&acep->list, &acp->free_list);
        }
        return 0;

ac_fail:/* free the elements */
        while (!list_empty (&acp->free_list)) {
                /* get the next entry */
                struct list_head *tmp = acp->free_list.next;
                list_del (tmp);
                acep = list_entry (tmp, auerchainelement_t, list);
                kfree (acep);
        }
        return -ENOMEM;
}


/* completion handler for synchronous chained URBs */
static void auerchain_blocking_completion (struct urb *urb)
{
	pauerchain_chs_t pchs = (pauerchain_chs_t)urb->context;
	pchs->done = 1;
	wmb();
	wake_up (&pchs->wqh);
}


/* Starts chained urb and waits for completion or timeout */
static int auerchain_start_wait_urb (pauerchain_t acp, struct urb *urb, int timeout, int* actual_length)
{
	auerchain_chs_t chs;
	int status;

	dbg ("auerchain_start_wait_urb called");
	init_waitqueue_head (&chs.wqh);
	chs.done = 0;

	urb->context = &chs;
	status = auerchain_submit_urb (acp, urb);
	if (status)
		/* something went wrong */
		return status;

	timeout = wait_event_timeout(chs.wqh, chs.done, timeout);

	if (!timeout && !chs.done) {
		if (urb->status != -EINPROGRESS) {	/* No callback?!! */
			dbg ("auerchain_start_wait_urb: raced timeout");
			status = urb->status;
		} else {
			dbg ("auerchain_start_wait_urb: timeout");
			auerchain_unlink_urb (acp, urb);  /* remove urb safely */
			status = -ETIMEDOUT;
		}
	} else
		status = urb->status;

	if (actual_length)
		*actual_length = urb->actual_length;

  	return status;
}


/* auerchain_control_msg - Builds a control urb, sends it off and waits for completion
   acp: pointer to the auerchain
   dev: pointer to the usb device to send the message to
   pipe: endpoint "pipe" to send the message to
   request: USB message request value
   requesttype: USB message request type value
   value: USB message value
   index: USB message index value
   data: pointer to the data to send
   size: length in bytes of the data to send
   timeout: time to wait for the message to complete before timing out (if 0 the wait is forever)

   This function sends a simple control message to a specified endpoint
   and waits for the message to complete, or timeout.

   If successful, it returns the transferred length, otherwise a negative error number.

   Don't use this function from within an interrupt context, like a
   bottom half handler.  If you need an asynchronous message, or need to send
   a message from within interrupt context, use auerchain_submit_urb()
*/
static int auerchain_control_msg (pauerchain_t acp, struct usb_device *dev, unsigned int pipe, __u8 request, __u8 requesttype,
			          __u16 value, __u16 index, void *data, __u16 size, int timeout)
{
	int ret;
	struct usb_ctrlrequest *dr;
	struct urb *urb;
        int length;

        dbg ("auerchain_control_msg");
        dr = kmalloc (sizeof (struct usb_ctrlrequest), GFP_KERNEL);
	if (!dr)
		return -ENOMEM;
	urb = usb_alloc_urb (0, GFP_KERNEL);
	if (!urb) {
        	kfree (dr);
		return -ENOMEM;
        }

	dr->bRequestType = requesttype;
	dr->bRequest = request;
	dr->wValue  = cpu_to_le16 (value);
	dr->wIndex  = cpu_to_le16 (index);
	dr->wLength = cpu_to_le16 (size);

	usb_fill_control_urb (urb, dev, pipe, (unsigned char*)dr, data, size,    /* build urb */
		          auerchain_blocking_completion, NULL);
	ret = auerchain_start_wait_urb (acp, urb, timeout, &length);

	usb_free_urb (urb);
	kfree (dr);

        if (ret < 0)
		return ret;
	else
		return length;
}


/*-------------------------------------------------------------------*/
/* Buffer List helper functions                                      */

/* free a single auerbuf */
static void auerbuf_free (pauerbuf_t bp)
{
	kfree(bp->bufp);
	kfree(bp->dr);
	usb_free_urb(bp->urbp);
	kfree(bp);
}

/* free the buffers from an auerbuf list */
static void auerbuf_free_list (struct list_head *q)
{
        struct list_head *tmp;
	struct list_head *p;
	pauerbuf_t bp;

	dbg ("auerbuf_free_list");
	for (p = q->next; p != q;) {
		bp = list_entry (p, auerbuf_t, buff_list);
		tmp = p->next;
		list_del (p);
		p = tmp;
		auerbuf_free (bp);
	}
}

/* init the members of a list control block */
static void auerbuf_init (pauerbufctl_t bcp)
{
	dbg ("auerbuf_init");
	spin_lock_init (&bcp->lock);
        INIT_LIST_HEAD (&bcp->free_buff_list);
        INIT_LIST_HEAD (&bcp->rec_buff_list);
}

/* free all buffers from an auerbuf chain */
static void auerbuf_free_buffers (pauerbufctl_t bcp)
{
	unsigned long flags;
	dbg ("auerbuf_free_buffers");

        spin_lock_irqsave (&bcp->lock, flags);

	auerbuf_free_list (&bcp->free_buff_list);
	auerbuf_free_list (&bcp->rec_buff_list);

        spin_unlock_irqrestore (&bcp->lock, flags);
}

/* setup a list of buffers */
/* requirement: auerbuf_init() */
static int auerbuf_setup (pauerbufctl_t bcp, unsigned int numElements, unsigned int bufsize)
{
        pauerbuf_t bep = NULL;

        dbg ("auerbuf_setup called with %d elements of %d bytes", numElements, bufsize);

        /* fill the list of free elements */
        for (;numElements; numElements--) {
                bep = kzalloc(sizeof(auerbuf_t), GFP_KERNEL);
                if (!bep)
			goto bl_fail;
                bep->list = bcp;
                INIT_LIST_HEAD (&bep->buff_list);
                bep->bufp = kmalloc (bufsize, GFP_KERNEL);
                if (!bep->bufp)
			goto bl_fail;
                bep->dr = kmalloc(sizeof (struct usb_ctrlrequest), GFP_KERNEL);
                if (!bep->dr)
			goto bl_fail;
                bep->urbp = usb_alloc_urb (0, GFP_KERNEL);
                if (!bep->urbp)
			goto bl_fail;
                list_add_tail (&bep->buff_list, &bcp->free_buff_list);
        }
        return 0;

bl_fail:/* not enough memory. Free allocated elements */
        dbg ("auerbuf_setup: no more memory");
	auerbuf_free(bep);
        auerbuf_free_buffers (bcp);
        return -ENOMEM;
}

/* insert a used buffer into the free list */
static void auerbuf_releasebuf( pauerbuf_t bp)
{
        unsigned long flags;
        pauerbufctl_t bcp = bp->list;
	bp->retries = 0;

        dbg ("auerbuf_releasebuf called");
        spin_lock_irqsave (&bcp->lock, flags);
	list_add_tail (&bp->buff_list, &bcp->free_buff_list);
        spin_unlock_irqrestore (&bcp->lock, flags);
}


/*-------------------------------------------------------------------*/
/* Completion handlers */

/* Values of urb->status or results of usb_submit_urb():
0		Initial, OK
-EINPROGRESS	during submission until end
-ENOENT		if urb is unlinked
-ETIME		Device did not respond
-ENOMEM		Memory Overflow
-ENODEV		Specified USB-device or bus doesn't exist
-ENXIO		URB already queued
-EINVAL		a) Invalid transfer type specified (or not supported)
		b) Invalid interrupt interval (0n256)
-EAGAIN		a) Specified ISO start frame too early
		b) (using ISO-ASAP) Too much scheduled for the future wait some time and try again.
-EFBIG		Too much ISO frames requested (currently uhci900)
-EPIPE		Specified pipe-handle/Endpoint is already stalled
-EMSGSIZE	Endpoint message size is zero, do interface/alternate setting
-EPROTO		a) Bitstuff error
		b) Unknown USB error
-EILSEQ		CRC mismatch
-ENOSR		Buffer error
-EREMOTEIO	Short packet detected
-EXDEV		ISO transfer only partially completed look at individual frame status for details
-EINVAL		ISO madness, if this happens: Log off and go home
-EOVERFLOW	babble
*/

/* check if a status code allows a retry */
static int auerswald_status_retry (int status)
{
	switch (status) {
	case 0:
	case -ETIME:
	case -EOVERFLOW:
	case -EAGAIN:
	case -EPIPE:
	case -EPROTO:
	case -EILSEQ:
	case -ENOSR:
	case -EREMOTEIO:
		return 1; /* do a retry */
	}
	return 0;	/* no retry possible */
}

/* Completion of asynchronous write block */
static void auerchar_ctrlwrite_complete (struct urb * urb)
{
	pauerbuf_t bp = (pauerbuf_t) urb->context;
	pauerswald_t cp = ((pauerswald_t)((char *)(bp->list)-(unsigned long)(&((pauerswald_t)0)->bufctl)));
	dbg ("auerchar_ctrlwrite_complete called");

	/* reuse the buffer */
	auerbuf_releasebuf (bp);
	/* Wake up all processes waiting for a buffer */
	wake_up (&cp->bufferwait);
}

/* Completion handler for dummy retry packet */
static void auerswald_ctrlread_wretcomplete (struct urb * urb)
{
        pauerbuf_t bp = (pauerbuf_t) urb->context;
        pauerswald_t cp;
	int ret;
        dbg ("auerswald_ctrlread_wretcomplete called");
        dbg ("complete with status: %d", urb->status);
	cp = ((pauerswald_t)((char *)(bp->list)-(unsigned long)(&((pauerswald_t)0)->bufctl)));

	/* check if it is possible to advance */
	if (!auerswald_status_retry (urb->status) || !cp->usbdev) {
		/* reuse the buffer */
		err ("control dummy: transmission error %d, can not retry", urb->status);
		auerbuf_releasebuf (bp);
		/* Wake up all processes waiting for a buffer */
		wake_up (&cp->bufferwait);
		return;
	}

	/* fill the control message */
	bp->dr->bRequestType = AUT_RREQ;
	bp->dr->bRequest     = AUV_RBLOCK;
	bp->dr->wLength      = bp->dr->wValue;	/* temporary stored */
	bp->dr->wValue       = cpu_to_le16 (1);	/* Retry Flag */
	/* bp->dr->index    = channel id;          remains */
	usb_fill_control_urb (bp->urbp, cp->usbdev, usb_rcvctrlpipe (cp->usbdev, 0),
                          (unsigned char*)bp->dr, bp->bufp, le16_to_cpu (bp->dr->wLength),
		          auerswald_ctrlread_complete,bp);

	/* submit the control msg as next paket */
	ret = auerchain_submit_urb_list (&cp->controlchain, bp->urbp, 1);
        if (ret) {
        	dbg ("auerswald_ctrlread_complete: nonzero result of auerchain_submit_urb_list %d", ret);
        	bp->urbp->status = ret;
        	auerswald_ctrlread_complete (bp->urbp);
    	}
}

/* completion handler for receiving of control messages */
static void auerswald_ctrlread_complete (struct urb * urb)
{
        unsigned int  serviceid;
        pauerswald_t  cp;
        pauerscon_t   scp;
        pauerbuf_t    bp  = (pauerbuf_t) urb->context;
	int ret;
        dbg ("auerswald_ctrlread_complete called");

	cp = ((pauerswald_t)((char *)(bp->list)-(unsigned long)(&((pauerswald_t)0)->bufctl)));

	/* check if there is valid data in this urb */
        if (urb->status) {
		dbg ("complete with non-zero status: %d", urb->status);
		/* should we do a retry? */
		if (!auerswald_status_retry (urb->status)
		 || !cp->usbdev
		 || (cp->version < AUV_RETRY)
                 || (bp->retries >= AU_RETRIES)) {
			/* reuse the buffer */
			err ("control read: transmission error %d, can not retry", urb->status);
			auerbuf_releasebuf (bp);
			/* Wake up all processes waiting for a buffer */
			wake_up (&cp->bufferwait);
			return;
		}
		bp->retries++;
		dbg ("Retry count = %d", bp->retries);
		/* send a long dummy control-write-message to allow device firmware to react */
		bp->dr->bRequestType = AUT_WREQ;
		bp->dr->bRequest     = AUV_DUMMY;
		bp->dr->wValue       = bp->dr->wLength; /* temporary storage */
		// bp->dr->wIndex    channel ID remains
		bp->dr->wLength      = cpu_to_le16 (32); /* >= 8 bytes */
		usb_fill_control_urb (bp->urbp, cp->usbdev, usb_sndctrlpipe (cp->usbdev, 0),
  			(unsigned char*)bp->dr, bp->bufp, 32,
	   		auerswald_ctrlread_wretcomplete,bp);

		/* submit the control msg as next paket */
       		ret = auerchain_submit_urb_list (&cp->controlchain, bp->urbp, 1);
       		if (ret) {
               		dbg ("auerswald_ctrlread_complete: nonzero result of auerchain_submit_urb_list %d", ret);
               		bp->urbp->status = ret;
               		auerswald_ctrlread_wretcomplete (bp->urbp);
		}
                return;
        }

        /* get the actual bytecount (incl. headerbyte) */
        bp->len = urb->actual_length;
        serviceid = bp->bufp[0] & AUH_TYPEMASK;
        dbg ("Paket with serviceid %d and %d bytes received", serviceid, bp->len);

        /* dispatch the paket */
        scp = cp->services[serviceid];
        if (scp) {
                /* look, Ma, a listener! */
                scp->dispatch (scp, bp);
        }

        /* release the paket */
        auerbuf_releasebuf (bp);
	/* Wake up all processes waiting for a buffer */
	wake_up (&cp->bufferwait);
}

/*-------------------------------------------------------------------*/
/* Handling of Interrupt Endpoint                                    */
/* This interrupt Endpoint is used to inform the host about waiting
   messages from the USB device.
*/
/* int completion handler. */
static void auerswald_int_complete (struct urb * urb)
{
        unsigned long flags;
        unsigned  int channelid;
        unsigned  int bytecount;
        int ret;
        pauerbuf_t   bp = NULL;
        pauerswald_t cp = (pauerswald_t) urb->context;

        dbg ("%s called", __FUNCTION__);

	switch (urb->status) {
	case 0:
		/* success */
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* this urb is terminated, clean up */
		dbg("%s - urb shutting down with status: %d", __FUNCTION__, urb->status);
		return;
	default:
		dbg("%s - nonzero urb status received: %d", __FUNCTION__, urb->status);
		goto exit;
	}

        /* check if all needed data was received */
	if (urb->actual_length < AU_IRQMINSIZE) {
                dbg ("invalid data length received: %d bytes", urb->actual_length);
		goto exit;
        }

        /* check the command code */
        if (cp->intbufp[0] != AU_IRQCMDID) {
                dbg ("invalid command received: %d", cp->intbufp[0]);
		goto exit;
        }

        /* check the command type */
        if (cp->intbufp[1] != AU_BLOCKRDY) {
                dbg ("invalid command type received: %d", cp->intbufp[1]);
		goto exit;
        }

        /* now extract the information */
        channelid = cp->intbufp[2];
        bytecount = (unsigned char)cp->intbufp[3];
        bytecount |= (unsigned char)cp->intbufp[4] << 8;

        /* check the channel id */
        if (channelid >= AUH_TYPESIZE) {
                dbg ("invalid channel id received: %d", channelid);
		goto exit;
        }

        /* check the byte count */
        if (bytecount > (cp->maxControlLength+AUH_SIZE)) {
                dbg ("invalid byte count received: %d", bytecount);
		goto exit;
        }
        dbg ("Service Channel = %d", channelid);
        dbg ("Byte Count = %d", bytecount);

        /* get a buffer for the next data paket */
        spin_lock_irqsave (&cp->bufctl.lock, flags);
        if (!list_empty (&cp->bufctl.free_buff_list)) {
                /* yes: get the entry */
                struct list_head *tmp = cp->bufctl.free_buff_list.next;
                list_del (tmp);
                bp = list_entry (tmp, auerbuf_t, buff_list);
        }
        spin_unlock_irqrestore (&cp->bufctl.lock, flags);

        /* if no buffer available: skip it */
        if (!bp) {
                dbg ("auerswald_int_complete: no data buffer available");
                /* can we do something more?
		   This is a big problem: if this int packet is ignored, the
		   device will wait forever and not signal any more data.
		   The only real solution is: having enough buffers!
		   Or perhaps temporary disabling the int endpoint?
		*/
		goto exit;
        }

	/* fill the control message */
        bp->dr->bRequestType = AUT_RREQ;
	bp->dr->bRequest     = AUV_RBLOCK;
	bp->dr->wValue       = cpu_to_le16 (0);
	bp->dr->wIndex       = cpu_to_le16 (channelid | AUH_DIRECT | AUH_UNSPLIT);
	bp->dr->wLength      = cpu_to_le16 (bytecount);
	usb_fill_control_urb (bp->urbp, cp->usbdev, usb_rcvctrlpipe (cp->usbdev, 0),
                          (unsigned char*)bp->dr, bp->bufp, bytecount,
		          auerswald_ctrlread_complete,bp);

        /* submit the control msg */
        ret = auerchain_submit_urb (&cp->controlchain, bp->urbp);
        if (ret) {
                dbg ("auerswald_int_complete: nonzero result of auerchain_submit_urb %d", ret);
                bp->urbp->status = ret;
                auerswald_ctrlread_complete( bp->urbp);
		/* here applies the same problem as above: device locking! */
        }
exit:
	ret = usb_submit_urb (urb, GFP_ATOMIC);
	if (ret)
		err ("%s - usb_submit_urb failed with result %d",
		     __FUNCTION__, ret);
}

/* int memory deallocation
   NOTE: no mutex please!
*/
static void auerswald_int_free (pauerswald_t cp)
{
	if (cp->inturbp) {
		usb_free_urb(cp->inturbp);
		cp->inturbp = NULL;
	}
	kfree(cp->intbufp);
	cp->intbufp = NULL;
}

/* This function is called to activate the interrupt
   endpoint. This function returns 0 if successful or an error code.
   NOTE: no mutex please!
*/
static int auerswald_int_open (pauerswald_t cp)
{
        int ret;
	struct usb_host_endpoint *ep;
	int irqsize;
	dbg ("auerswald_int_open");

	ep = cp->usbdev->ep_in[AU_IRQENDP];
	if (!ep) {
		ret = -EFAULT;
  		goto intoend;
    	}
	irqsize = le16_to_cpu(ep->desc.wMaxPacketSize);
	cp->irqsize = irqsize;

	/* allocate the urb and data buffer */
        if (!cp->inturbp) {
                cp->inturbp = usb_alloc_urb (0, GFP_KERNEL);
                if (!cp->inturbp) {
                        ret = -ENOMEM;
                        goto intoend;
                }
        }
        if (!cp->intbufp) {
                cp->intbufp = kmalloc (irqsize, GFP_KERNEL);
                if (!cp->intbufp) {
                        ret = -ENOMEM;
                        goto intoend;
                }
        }
        /* setup urb */
        usb_fill_int_urb (cp->inturbp, cp->usbdev,
			usb_rcvintpipe (cp->usbdev,AU_IRQENDP), cp->intbufp,
			irqsize, auerswald_int_complete, cp, ep->desc.bInterval);
        /* start the urb */
	cp->inturbp->status = 0;	/* needed! */
	ret = usb_submit_urb (cp->inturbp, GFP_KERNEL);

intoend:
        if (ret < 0) {
                /* activation of interrupt endpoint has failed. Now clean up. */
                dbg ("auerswald_int_open: activation of int endpoint failed");

                /* deallocate memory */
                auerswald_int_free (cp);
        }
        return ret;
}

/* This function is called to deactivate the interrupt
   endpoint. This function returns 0 if successful or an error code.
   NOTE: no mutex please!
*/
static void auerswald_int_release (pauerswald_t cp)
{
        dbg ("auerswald_int_release");

        /* stop the int endpoint */
	usb_kill_urb (cp->inturbp);

        /* deallocate memory */
        auerswald_int_free (cp);
}

/* --------------------------------------------------------------------- */
/* Helper functions                                                      */

/* wake up waiting readers */
static void auerchar_disconnect (pauerscon_t scp)
{
        pauerchar_t ccp = ((pauerchar_t)((char *)(scp)-(unsigned long)(&((pauerchar_t)0)->scontext)));
	dbg ("auerchar_disconnect called");
	ccp->removed = 1;
	wake_up (&ccp->readwait);
}


/* dispatch a read paket to a waiting character device */
static void auerchar_ctrlread_dispatch (pauerscon_t scp, pauerbuf_t bp)
{
	unsigned long flags;
        pauerchar_t ccp;
        pauerbuf_t newbp = NULL;
        char * charp;
        dbg ("auerchar_ctrlread_dispatch called");
        ccp = ((pauerchar_t)((char *)(scp)-(unsigned long)(&((pauerchar_t)0)->scontext)));

        /* get a read buffer from character device context */
        spin_lock_irqsave (&ccp->bufctl.lock, flags);
        if (!list_empty (&ccp->bufctl.free_buff_list)) {
                /* yes: get the entry */
                struct list_head *tmp = ccp->bufctl.free_buff_list.next;
                list_del (tmp);
                newbp = list_entry (tmp, auerbuf_t, buff_list);
        }
        spin_unlock_irqrestore (&ccp->bufctl.lock, flags);

        if (!newbp) {
                dbg ("No read buffer available, discard paket!");
                return;     /* no buffer, no dispatch */
        }

        /* copy information to new buffer element
           (all buffers have the same length) */
        charp = newbp->bufp;
        newbp->bufp = bp->bufp;
        bp->bufp = charp;
        newbp->len = bp->len;

        /* insert new buffer in read list */
        spin_lock_irqsave (&ccp->bufctl.lock, flags);
	list_add_tail (&newbp->buff_list, &ccp->bufctl.rec_buff_list);
        spin_unlock_irqrestore (&ccp->bufctl.lock, flags);
        dbg ("read buffer appended to rec_list");

        /* wake up pending synchronous reads */
	wake_up (&ccp->readwait);
}


/* Delete an auerswald driver context */
static void auerswald_delete( pauerswald_t cp)
{
	dbg( "auerswald_delete");
	if (cp == NULL)
		return;

	/* Wake up all processes waiting for a buffer */
	wake_up (&cp->bufferwait);

	/* Cleaning up */
	auerswald_int_release (cp);
	auerchain_free (&cp->controlchain);
	auerbuf_free_buffers (&cp->bufctl);

	/* release the memory */
	kfree( cp);
}


/* Delete an auerswald character context */
static void auerchar_delete( pauerchar_t ccp)
{
	dbg ("auerchar_delete");
	if (ccp == NULL)
		return;

        /* wake up pending synchronous reads */
	ccp->removed = 1;
	wake_up (&ccp->readwait);

	/* remove the read buffer */
	if (ccp->readbuf) {
		auerbuf_releasebuf (ccp->readbuf);
		ccp->readbuf = NULL;
	}

	/* remove the character buffers */
	auerbuf_free_buffers (&ccp->bufctl);

	/* release the memory */
	kfree( ccp);
}


/* add a new service to the device
   scp->id must be set!
   return: 0 if OK, else error code
*/
static int auerswald_addservice (pauerswald_t cp, pauerscon_t scp)
{
	int ret;

	/* is the device available? */
	if (!cp->usbdev) {
		dbg ("usbdev == NULL");
		return -EIO;	/*no: can not add a service, sorry*/
	}

	/* is the service available? */
	if (cp->services[scp->id]) {
		dbg ("service is busy");
                return -EBUSY;
	}

	/* device is available, service is free */
	cp->services[scp->id] = scp;

	/* register service in device */
	ret = auerchain_control_msg(
		&cp->controlchain,                      /* pointer to control chain */
		cp->usbdev,                             /* pointer to device */
		usb_sndctrlpipe (cp->usbdev, 0),        /* pipe to control endpoint */
		AUV_CHANNELCTL,                         /* USB message request value */
		AUT_WREQ,                               /* USB message request type value */
		0x01,              /* open                 USB message value */
		scp->id,            		        /* USB message index value */
		NULL,                                   /* pointer to the data to send */
		0,                                      /* length in bytes of the data to send */
		HZ * 2);                                /* time to wait for the message to complete before timing out */
	if (ret < 0) {
		dbg ("auerswald_addservice: auerchain_control_msg returned error code %d", ret);
		/* undo above actions */
		cp->services[scp->id] = NULL;
		return ret;
	}

	dbg ("auerswald_addservice: channel open OK");
	return 0;
}


/* remove a service from the the device
   scp->id must be set! */
static void auerswald_removeservice (pauerswald_t cp, pauerscon_t scp)
{
	dbg ("auerswald_removeservice called");

	/* check if we have a service allocated */
	if (scp->id == AUH_UNASSIGNED)
		return;

	/* If there is a device: close the channel */
	if (cp->usbdev) {
		/* Close the service channel inside the device */
		int ret = auerchain_control_msg(
		&cp->controlchain,            		/* pointer to control chain */
		cp->usbdev,         		        /* pointer to device */
		usb_sndctrlpipe (cp->usbdev, 0),	/* pipe to control endpoint */
		AUV_CHANNELCTL,                         /* USB message request value */
		AUT_WREQ,                               /* USB message request type value */
		0x00,              // close             /* USB message value */
		scp->id,            		        /* USB message index value */
		NULL,                                   /* pointer to the data to send */
		0,                                      /* length in bytes of the data to send */
		HZ * 2);                                /* time to wait for the message to complete before timing out */
		if (ret < 0) {
			dbg ("auerswald_removeservice: auerchain_control_msg returned error code %d", ret);
		}
		else {
			dbg ("auerswald_removeservice: channel close OK");
		}
	}

	/* remove the service from the device */
	cp->services[scp->id] = NULL;
	scp->id = AUH_UNASSIGNED;
}


/* --------------------------------------------------------------------- */
/* Char device functions                                                 */

/* Open a new character device */
static int auerchar_open (struct inode *inode, struct file *file)
{
	int dtindex = iminor(inode);
	pauerswald_t cp = NULL;
	pauerchar_t ccp = NULL;
	struct usb_interface *intf;
        int ret;

        /* minor number in range? */
	if (dtindex < 0) {
		return -ENODEV;
        }
	intf = usb_find_interface(&auerswald_driver, dtindex);
	if (!intf) {
		return -ENODEV;
	}

	/* usb device available? */
	cp = usb_get_intfdata (intf);
	if (cp == NULL) {
		return -ENODEV;
	}
	if (down_interruptible (&cp->mutex)) {
		return -ERESTARTSYS;
	}

	/* we have access to the device. Now lets allocate memory */
	ccp = kzalloc(sizeof(auerchar_t), GFP_KERNEL);
	if (ccp == NULL) {
		err ("out of memory");
		ret = -ENOMEM;
		goto ofail;
	}

	/* Initialize device descriptor */
	init_MUTEX( &ccp->mutex);
	init_MUTEX( &ccp->readmutex);
        auerbuf_init (&ccp->bufctl);
        ccp->scontext.id = AUH_UNASSIGNED;
        ccp->scontext.dispatch = auerchar_ctrlread_dispatch;
	ccp->scontext.disconnect = auerchar_disconnect;
	init_waitqueue_head (&ccp->readwait);

	ret = auerbuf_setup (&ccp->bufctl, AU_RBUFFERS, cp->maxControlLength+AUH_SIZE);
       	if (ret) {
		goto ofail;
	}

	cp->open_count++;
	ccp->auerdev = cp;
	dbg("open %s as /dev/%s", cp->dev_desc, cp->name);
	up (&cp->mutex);

	/* file IO stuff */
	file->f_pos = 0;
	file->private_data = ccp;
	return nonseekable_open(inode, file);

	/* Error exit */
ofail:	up (&cp->mutex);
	auerchar_delete (ccp);
	return ret;
}


/* IOCTL functions */
static int auerchar_ioctl (struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	pauerchar_t ccp = (pauerchar_t) file->private_data;
	int ret = 0;
        audevinfo_t devinfo;
        pauerswald_t cp = NULL;
	unsigned int u;
	unsigned int __user *user_arg = (unsigned int __user *)arg;

        dbg ("ioctl");

	/* get the mutexes */
	if (down_interruptible (&ccp->mutex)) {
		return -ERESTARTSYS;
	}
	cp = ccp->auerdev;
	if (!cp) {
		up (&ccp->mutex);
                return -ENODEV;
	}
	if (down_interruptible (&cp->mutex)) {
		up(&ccp->mutex);
		return -ERESTARTSYS;
	}

	/* Check for removal */
	if (!cp->usbdev) {
		up(&cp->mutex);
		up(&ccp->mutex);
                return -ENODEV;
	}

	switch (cmd) {

	/* return != 0 if Transmitt channel ready to send */
	case IOCTL_AU_TXREADY:
		dbg ("IOCTL_AU_TXREADY");
		u   = ccp->auerdev
		   && (ccp->scontext.id != AUH_UNASSIGNED)
		   && !list_empty (&cp->bufctl.free_buff_list);
	        ret = put_user (u, user_arg);
		break;

	/* return != 0 if connected to a service channel */
	case IOCTL_AU_CONNECT:
		dbg ("IOCTL_AU_CONNECT");
		u = (ccp->scontext.id != AUH_UNASSIGNED);
	        ret = put_user (u, user_arg);
		break;

	/* return != 0 if Receive Data available */
	case IOCTL_AU_RXAVAIL:
		dbg ("IOCTL_AU_RXAVAIL");
		if (ccp->scontext.id == AUH_UNASSIGNED) {
                        ret = -EIO;
                        break;
                }
		u = 0;	/* no data */
		if (ccp->readbuf) {
			int restlen = ccp->readbuf->len - ccp->readoffset;
			if (restlen > 0)
				u = 1;
		}
		if (!u) {
        		if (!list_empty (&ccp->bufctl.rec_buff_list)) {
				u = 1;
			}
		}
	        ret = put_user (u, user_arg);
		break;

	/* return the max. buffer length for the device */
	case IOCTL_AU_BUFLEN:
		dbg ("IOCTL_AU_BUFLEN");
		u = cp->maxControlLength;
	        ret = put_user (u, user_arg);
		break;

	/* requesting a service channel */
        case IOCTL_AU_SERVREQ:
		dbg ("IOCTL_AU_SERVREQ");
                /* requesting a service means: release the previous one first */
		auerswald_removeservice (cp, &ccp->scontext);
		/* get the channel number */
		ret = get_user (u, user_arg);
		if (ret) {
			break;
		}
		if ((u < AUH_FIRSTUSERCH) || (u >= AUH_TYPESIZE)) {
                        ret = -EIO;
                        break;
                }
                dbg ("auerchar service request parameters are ok");
		ccp->scontext.id = u;

		/* request the service now */
		ret = auerswald_addservice (cp, &ccp->scontext);
		if (ret) {
			/* no: revert service entry */
                	ccp->scontext.id = AUH_UNASSIGNED;
		}
		break;

	/* get a string descriptor for the device */
	case IOCTL_AU_DEVINFO:
		dbg ("IOCTL_AU_DEVINFO");
                if (copy_from_user (&devinfo, (void __user *) arg, sizeof (audevinfo_t))) {
        		ret = -EFAULT;
	        	break;
                }
		u = strlen(cp->dev_desc)+1;
		if (u > devinfo.bsize) {
			u = devinfo.bsize;
		}
		ret = copy_to_user(devinfo.buf, cp->dev_desc, u) ? -EFAULT : 0;
		break;

	/* get the max. string descriptor length */
        case IOCTL_AU_SLEN:
		dbg ("IOCTL_AU_SLEN");
		u = AUSI_DLEN;
	        ret = put_user (u, user_arg);
		break;

	default:
		dbg ("IOCTL_AU_UNKNOWN");
		ret = -ENOIOCTLCMD;
		break;
        }
	/* release the mutexes */
	up(&cp->mutex);
	up(&ccp->mutex);
	return ret;
}

/* Read data from the device */
static ssize_t auerchar_read (struct file *file, char __user *buf, size_t count, loff_t * ppos)
{
        unsigned long flags;
	pauerchar_t ccp = (pauerchar_t) file->private_data;
        pauerbuf_t   bp = NULL;
	wait_queue_t wait;

        dbg ("auerchar_read");

	/* Error checking */
	if (!ccp)
		return -EIO;
	if (*ppos)
 		return -ESPIPE;
        if (count == 0)
		return 0;

	/* get the mutex */
	if (down_interruptible (&ccp->mutex))
		return -ERESTARTSYS;

	/* Can we expect to read something? */
	if (ccp->scontext.id == AUH_UNASSIGNED) {
		up (&ccp->mutex);
                return -EIO;
	}

	/* only one reader per device allowed */
	if (down_interruptible (&ccp->readmutex)) {
		up (&ccp->mutex);
		return -ERESTARTSYS;
	}

	/* read data from readbuf, if available */
doreadbuf:
	bp = ccp->readbuf;
	if (bp) {
		/* read the maximum bytes */
		int restlen = bp->len - ccp->readoffset;
		if (restlen < 0)
			restlen = 0;
		if (count > restlen)
			count = restlen;
		if (count) {
			if (copy_to_user (buf, bp->bufp+ccp->readoffset, count)) {
				dbg ("auerswald_read: copy_to_user failed");
				up (&ccp->readmutex);
				up (&ccp->mutex);
				return -EFAULT;
			}
		}
		/* advance the read offset */
		ccp->readoffset += count;
		restlen -= count;
		// reuse the read buffer
		if (restlen <= 0) {
			auerbuf_releasebuf (bp);
			ccp->readbuf = NULL;
		}
		/* return with number of bytes read */
		if (count) {
			up (&ccp->readmutex);
			up (&ccp->mutex);
			return count;
		}
	}

	/* a read buffer is not available. Try to get the next data block. */
doreadlist:
	/* Preparing for sleep */
	init_waitqueue_entry (&wait, current);
	set_current_state (TASK_INTERRUPTIBLE);
	add_wait_queue (&ccp->readwait, &wait);

	bp = NULL;
	spin_lock_irqsave (&ccp->bufctl.lock, flags);
        if (!list_empty (&ccp->bufctl.rec_buff_list)) {
                /* yes: get the entry */
                struct list_head *tmp = ccp->bufctl.rec_buff_list.next;
                list_del (tmp);
                bp = list_entry (tmp, auerbuf_t, buff_list);
        }
        spin_unlock_irqrestore (&ccp->bufctl.lock, flags);

	/* have we got data? */
	if (bp) {
		ccp->readbuf = bp;
		ccp->readoffset = AUH_SIZE; /* for headerbyte */
		set_current_state (TASK_RUNNING);
		remove_wait_queue (&ccp->readwait, &wait);
		goto doreadbuf;		  /* now we can read! */
	}

	/* no data available. Should we wait? */
	if (file->f_flags & O_NONBLOCK) {
                dbg ("No read buffer available, returning -EAGAIN");
		set_current_state (TASK_RUNNING);
		remove_wait_queue (&ccp->readwait, &wait);
		up (&ccp->readmutex);
		up (&ccp->mutex);
		return -EAGAIN;  /* nonblocking, no data available */
        }

	/* yes, we should wait! */
	up (&ccp->mutex); /* allow other operations while we wait */
	schedule();
	remove_wait_queue (&ccp->readwait, &wait);
	if (signal_pending (current)) {
		/* waked up by a signal */
		up (&ccp->readmutex);
		return -ERESTARTSYS;
	}

	/* Anything left to read? */
	if ((ccp->scontext.id == AUH_UNASSIGNED) || ccp->removed) {
		up (&ccp->readmutex);
		return -EIO;
	}

	if (down_interruptible (&ccp->mutex)) {
		up (&ccp->readmutex);
		return -ERESTARTSYS;
	}

	/* try to read the incoming data again */
	goto doreadlist;
}


/* Write a data block into the right service channel of the device */
static ssize_t auerchar_write (struct file *file, const char __user *buf, size_t len, loff_t *ppos)
{
	pauerchar_t ccp = (pauerchar_t) file->private_data;
        pauerswald_t cp = NULL;
        pauerbuf_t bp;
        unsigned long flags;
	int ret;
	wait_queue_t wait;

        dbg ("auerchar_write %zd bytes", len);

	/* Error checking */
	if (!ccp)
		return -EIO;
        if (*ppos)
		return -ESPIPE;
        if (len == 0)
                return 0;

write_again:
	/* get the mutex */
	if (down_interruptible (&ccp->mutex))
		return -ERESTARTSYS;

	/* Can we expect to write something? */
	if (ccp->scontext.id == AUH_UNASSIGNED) {
		up (&ccp->mutex);
                return -EIO;
	}

	cp = ccp->auerdev;
	if (!cp) {
		up (&ccp->mutex);
		return -ERESTARTSYS;
	}
	if (down_interruptible (&cp->mutex)) {
		up (&ccp->mutex);
		return -ERESTARTSYS;
	}
	if (!cp->usbdev) {
		up (&cp->mutex);
		up (&ccp->mutex);
		return -EIO;
	}
	/* Prepare for sleep */
	init_waitqueue_entry (&wait, current);
	set_current_state (TASK_INTERRUPTIBLE);
	add_wait_queue (&cp->bufferwait, &wait);

	/* Try to get a buffer from the device pool.
	   We can't use a buffer from ccp->bufctl because the write
	   command will last beond a release() */
	bp = NULL;
	spin_lock_irqsave (&cp->bufctl.lock, flags);
        if (!list_empty (&cp->bufctl.free_buff_list)) {
                /* yes: get the entry */
                struct list_head *tmp = cp->bufctl.free_buff_list.next;
                list_del (tmp);
                bp = list_entry (tmp, auerbuf_t, buff_list);
        }
        spin_unlock_irqrestore (&cp->bufctl.lock, flags);

	/* are there any buffers left? */
	if (!bp) {
		up (&cp->mutex);
		up (&ccp->mutex);

		/* NONBLOCK: don't wait */
		if (file->f_flags & O_NONBLOCK) {
			set_current_state (TASK_RUNNING);
			remove_wait_queue (&cp->bufferwait, &wait);
			return -EAGAIN;
		}

		/* BLOCKING: wait */
		schedule();
		remove_wait_queue (&cp->bufferwait, &wait);
		if (signal_pending (current)) {
			/* waked up by a signal */
			return -ERESTARTSYS;
		}
		goto write_again;
	} else {
		set_current_state (TASK_RUNNING);
		remove_wait_queue (&cp->bufferwait, &wait);
	}

	/* protect against too big write requests */
	if (len > cp->maxControlLength)
		len = cp->maxControlLength;

	/* Fill the buffer */
	if (copy_from_user ( bp->bufp+AUH_SIZE, buf, len)) {
		dbg ("copy_from_user failed");
		auerbuf_releasebuf (bp);
		/* Wake up all processes waiting for a buffer */
		wake_up (&cp->bufferwait);
		up (&cp->mutex);
		up (&ccp->mutex);
		return -EFAULT;
	}

	/* set the header byte */
        *(bp->bufp) = ccp->scontext.id | AUH_DIRECT | AUH_UNSPLIT;

	/* Set the transfer Parameters */
	bp->len = len+AUH_SIZE;
        bp->dr->bRequestType = AUT_WREQ;
	bp->dr->bRequest     = AUV_WBLOCK;
	bp->dr->wValue       = cpu_to_le16 (0);
	bp->dr->wIndex       = cpu_to_le16 (ccp->scontext.id | AUH_DIRECT | AUH_UNSPLIT);
	bp->dr->wLength      = cpu_to_le16 (len+AUH_SIZE);
	usb_fill_control_urb (bp->urbp, cp->usbdev, usb_sndctrlpipe (cp->usbdev, 0),
                   (unsigned char*)bp->dr, bp->bufp, len+AUH_SIZE,
		    auerchar_ctrlwrite_complete, bp);
	/* up we go */
	ret = auerchain_submit_urb (&cp->controlchain, bp->urbp);
	up (&cp->mutex);
	if (ret) {
		dbg ("auerchar_write: nonzero result of auerchain_submit_urb %d", ret);
		auerbuf_releasebuf (bp);
		/* Wake up all processes waiting for a buffer */
		wake_up (&cp->bufferwait);
		up (&ccp->mutex);
		return -EIO;
	}
	else {
		dbg ("auerchar_write: Write OK");
		up (&ccp->mutex);
		return len;
	}
}


/* Close a character device */
static int auerchar_release (struct inode *inode, struct file *file)
{
	pauerchar_t ccp = (pauerchar_t) file->private_data;
	pauerswald_t cp;
	dbg("release");

	/* get the mutexes */
	if (down_interruptible (&ccp->mutex)) {
		return -ERESTARTSYS;
	}
	cp = ccp->auerdev;
	if (cp) {
		if (down_interruptible (&cp->mutex)) {
			up (&ccp->mutex);
			return -ERESTARTSYS;
		}
		/* remove an open service */
		auerswald_removeservice (cp, &ccp->scontext);
		/* detach from device */
		if ((--cp->open_count <= 0) && (cp->usbdev == NULL)) {
			/* usb device waits for removal */
			up (&cp->mutex);
			auerswald_delete (cp);
		} else {
			up (&cp->mutex);
		}
		cp = NULL;
		ccp->auerdev = NULL;
	}
	up (&ccp->mutex);
	auerchar_delete (ccp);

	return 0;
}


/*----------------------------------------------------------------------*/
/* File operation structure                                             */
static const struct file_operations auerswald_fops =
{
	.owner =	THIS_MODULE,
	.llseek =	no_llseek,
	.read =		auerchar_read,
	.write =        auerchar_write,
	.ioctl =	auerchar_ioctl,
	.open =		auerchar_open,
	.release =	auerchar_release,
};

static struct usb_class_driver auerswald_class = {
	.name =		"auer%d",
	.fops =		&auerswald_fops,
	.minor_base =	AUER_MINOR_BASE,
};


/* --------------------------------------------------------------------- */
/* Special USB driver functions                                          */

/* Probe if this driver wants to serve an USB device

   This entry point is called whenever a new device is attached to the bus.
   Then the device driver has to create a new instance of its internal data
   structures for the new device.

   The  dev argument specifies the device context, which contains pointers
   to all USB descriptors. The  interface argument specifies the interface
   number. If a USB driver wants to bind itself to a particular device and
   interface it has to return a pointer. This pointer normally references
   the device driver's context structure.

   Probing normally is done by checking the vendor and product identifications
   or the class and subclass definitions. If they match the interface number
   is compared with the ones supported by the driver. When probing is done
   class based it might be necessary to parse some more USB descriptors because
   the device properties can differ in a wide range.
*/
static int auerswald_probe (struct usb_interface *intf,
			    const struct usb_device_id *id)
{
	struct usb_device *usbdev = interface_to_usbdev(intf);
	pauerswald_t cp = NULL;
	unsigned int u = 0;
	__le16 *pbuf;
	int ret;

	dbg ("probe: vendor id 0x%x, device id 0x%x",
	     le16_to_cpu(usbdev->descriptor.idVendor),
	     le16_to_cpu(usbdev->descriptor.idProduct));

        /* we use only the first -and only- interface */
        if (intf->altsetting->desc.bInterfaceNumber != 0)
		return -ENODEV;

	/* allocate memory for our device and initialize it */
	cp = kzalloc (sizeof(auerswald_t), GFP_KERNEL);
	if (cp == NULL) {
		err ("out of memory");
		goto pfail;
	}

	/* Initialize device descriptor */
	init_MUTEX (&cp->mutex);
	cp->usbdev = usbdev;
	auerchain_init (&cp->controlchain);
        auerbuf_init (&cp->bufctl);
	init_waitqueue_head (&cp->bufferwait);

	ret = usb_register_dev(intf, &auerswald_class);
	if (ret) {
		err ("Not able to get a minor for this device.");
		goto pfail;
	}

	/* Give the device a name */
	sprintf (cp->name, "usb/auer%d", intf->minor);

	/* Store the index */
	cp->dtindex = intf->minor;

	/* Get the usb version of the device */
	cp->version = le16_to_cpu(cp->usbdev->descriptor.bcdDevice);
	dbg ("Version is %X", cp->version);

	/* allow some time to settle the device */
	msleep(334);

	/* Try to get a suitable textual description of the device */
	/* Device name:*/
	ret = usb_string( cp->usbdev, AUSI_DEVICE, cp->dev_desc, AUSI_DLEN-1);
	if (ret >= 0) {
		u += ret;
		/* Append Serial Number */
		memcpy(&cp->dev_desc[u], ",Ser# ", 6);
		u += 6;
		ret = usb_string( cp->usbdev, AUSI_SERIALNR, &cp->dev_desc[u], AUSI_DLEN-u-1);
		if (ret >= 0) {
			u += ret;
			/* Append subscriber number */
			memcpy(&cp->dev_desc[u], ", ", 2);
			u += 2;
			ret = usb_string( cp->usbdev, AUSI_MSN, &cp->dev_desc[u], AUSI_DLEN-u-1);
			if (ret >= 0) {
				u += ret;
			}
		}
	}
	cp->dev_desc[u] = '\0';
	info("device is a %s", cp->dev_desc);

        /* get the maximum allowed control transfer length */
        pbuf = kmalloc(2, GFP_KERNEL);    /* use an allocated buffer because of urb target */
        if (!pbuf) {
		err( "out of memory");
		goto pfail;
	}
        ret = usb_control_msg(cp->usbdev,           /* pointer to device */
                usb_rcvctrlpipe( cp->usbdev, 0 ),   /* pipe to control endpoint */
                AUV_GETINFO,                        /* USB message request value */
                AUT_RREQ,                           /* USB message request type value */
                0,                                  /* USB message value */
                AUDI_MBCTRANS,                      /* USB message index value */
                pbuf,                               /* pointer to the receive buffer */
                2,                                  /* length of the buffer */
                2000);                            /* time to wait for the message to complete before timing out */
        if (ret == 2) {
	        cp->maxControlLength = le16_to_cpup(pbuf);
                kfree(pbuf);
                dbg("setup: max. allowed control transfersize is %d bytes", cp->maxControlLength);
        } else {
                kfree(pbuf);
                err("setup: getting max. allowed control transfer length failed with error %d", ret);
		goto pfail;
        }

	/* allocate a chain for the control messages */
        if (auerchain_setup (&cp->controlchain, AUCH_ELEMENTS)) {
		err ("out of memory");
		goto pfail;
	}

        /* allocate buffers for control messages */
	if (auerbuf_setup (&cp->bufctl, AU_RBUFFERS, cp->maxControlLength+AUH_SIZE)) {
		err ("out of memory");
		goto pfail;
	}

	/* start the interrupt endpoint */
	if (auerswald_int_open (cp)) {
		err ("int endpoint failed");
		goto pfail;
	}

	/* all OK */
	usb_set_intfdata (intf, cp);
	return 0;

	/* Error exit: clean up the memory */
pfail:	auerswald_delete (cp);
	return -EIO;
}


/* Disconnect driver from a served device

   This function is called whenever a device which was served by this driver
   is disconnected.

   The argument  dev specifies the device context and the  driver_context
   returns a pointer to the previously registered  driver_context of the
   probe function. After returning from the disconnect function the USB
   framework completely deallocates all data structures associated with
   this device. So especially the usb_device structure must not be used
   any longer by the usb driver.
*/
static void auerswald_disconnect (struct usb_interface *intf)
{
	pauerswald_t cp = usb_get_intfdata (intf);
	unsigned int u;

	usb_set_intfdata (intf, NULL);
	if (!cp)
		return;

	down (&cp->mutex);
	info ("device /dev/%s now disconnecting", cp->name);

	/* give back our USB minor number */
	usb_deregister_dev(intf, &auerswald_class);

	/* Stop the interrupt endpoint */
	auerswald_int_release (cp);

	/* remove the control chain allocated in auerswald_probe
	   This has the benefit of
	   a) all pending (a)synchronous urbs are unlinked
	   b) all buffers dealing with urbs are reclaimed
	*/
	auerchain_free (&cp->controlchain);

	if (cp->open_count == 0) {
		/* nobody is using this device. So we can clean up now */
		up (&cp->mutex);/* up() is possible here because no other task
				   can open the device (see above). I don't want
				   to kfree() a locked mutex. */
		auerswald_delete (cp);
	} else {
		/* device is used. Remove the pointer to the
		   usb device (it's not valid any more). The last
		   release() will do the clean up */
		cp->usbdev = NULL;
		up (&cp->mutex);
		/* Terminate waiting writers */
		wake_up (&cp->bufferwait);
		/* Inform all waiting readers */
		for ( u = 0; u < AUH_TYPESIZE; u++) {
			pauerscon_t scp = cp->services[u];
			if (scp)
				scp->disconnect( scp);
		}
	}
}

/* Descriptor for the devices which are served by this driver.
   NOTE: this struct is parsed by the usbmanager install scripts.
         Don't change without caution!
*/
static struct usb_device_id auerswald_ids [] = {
	{ USB_DEVICE (ID_AUERSWALD, 0x00C0) },          /* COMpact 2104 USB */
	{ USB_DEVICE (ID_AUERSWALD, 0x00DB) },          /* COMpact 4410/2206 USB */
	{ USB_DEVICE (ID_AUERSWALD, 0x00DC) }, /* COMpact 4406 DSL */
	{ USB_DEVICE (ID_AUERSWALD, 0x00DD) }, /* COMpact 2204 USB */
	{ USB_DEVICE (ID_AUERSWALD, 0x00F1) },          /* Comfort 2000 System Telephone */
	{ USB_DEVICE (ID_AUERSWALD, 0x00F2) },          /* Comfort 1200 System Telephone */
        { }			                        /* Terminating entry */
};

/* Standard module device table */
MODULE_DEVICE_TABLE (usb, auerswald_ids);

/* Standard usb driver struct */
static struct usb_driver auerswald_driver = {
	.name =		"auerswald",
	.probe =	auerswald_probe,
	.disconnect =	auerswald_disconnect,
	.id_table =	auerswald_ids,
};


/* --------------------------------------------------------------------- */
/* Module loading/unloading                                              */

/* Driver initialisation. Called after module loading.
   NOTE: there is no concurrency at _init
*/
static int __init auerswald_init (void)
{
	int result;
	dbg ("init");

	/* register driver at the USB subsystem */
	result = usb_register (&auerswald_driver);
	if (result < 0) {
		err ("driver could not be registered");
		return -1;
	}
	return 0;
}

/* Driver deinit. Called before module removal.
   NOTE: there is no concurrency at _cleanup
*/
static void __exit auerswald_cleanup (void)
{
	dbg ("cleanup");
	usb_deregister (&auerswald_driver);
}

/* --------------------------------------------------------------------- */
/* Linux device driver module description                                */

MODULE_AUTHOR (DRIVER_AUTHOR);
MODULE_DESCRIPTION (DRIVER_DESC);
MODULE_LICENSE ("GPL");

module_init (auerswald_init);
module_exit (auerswald_cleanup);

/* --------------------------------------------------------------------- */


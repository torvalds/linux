/*
 * BRIEF MODULE DESCRIPTION
 *	Au1000 USB Device-Side (device layer)
 *
 * Copyright 2001-2002 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *		stevel@mvista.com or source@mvista.com
 *
 *  This program is free software; you can redistribute	 it and/or modify it
 *  under  the terms of	 the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the	License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED	  ``AS	IS'' AND   ANY	EXPRESS OR IMPLIED
 *  WARRANTIES,	  INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO	EVENT  SHALL   THE AUTHOR  BE	 LIABLE FOR ANY	  DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED	  TO, PROCUREMENT OF  SUBSTITUTE GOODS	OR SERVICES; LOSS OF
 *  USE, DATA,	OR PROFITS; OR	BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN	 CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/fcntl.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/smp_lock.h>
#define DEBUG
#include <linux/usb.h>

#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/irq.h>
#include <asm/mipsregs.h>
#include <asm/au1000.h>
#include <asm/au1000_dma.h>
#include <asm/au1000_usbdev.h>

#ifdef DEBUG
#undef VDEBUG
#ifdef VDEBUG
#define vdbg(fmt, arg...) printk(KERN_DEBUG __FILE__ ": " fmt "\n" , ## arg)
#else
#define vdbg(fmt, arg...) do {} while (0)
#endif
#else
#define vdbg(fmt, arg...) do {} while (0)
#endif

#define ALLOC_FLAGS (in_interrupt () ? GFP_ATOMIC : GFP_KERNEL)

#define EP_FIFO_DEPTH 8

typedef enum {
	SETUP_STAGE = 0,
	DATA_STAGE,
	STATUS_STAGE
} ep0_stage_t;

typedef struct {
	int read_fifo;
	int write_fifo;
	int ctrl_stat;
	int read_fifo_status;
	int write_fifo_status;
} endpoint_reg_t;

typedef struct {
	usbdev_pkt_t *head;
	usbdev_pkt_t *tail;
	int count;
} pkt_list_t;

typedef struct {
	int active;
	struct usb_endpoint_descriptor *desc;
	endpoint_reg_t *reg;
	/* Only one of these are used, unless this is the control ep */
	pkt_list_t inlist;
	pkt_list_t outlist;
	unsigned int indma, outdma; /* DMA channel numbers for IN, OUT */
	/* following are extracted from endpoint descriptor for easy access */
	int max_pkt_size;
	int type;
	int direction;
	/* WE assign endpoint addresses! */
	int address;
	spinlock_t lock;
} endpoint_t;


static struct usb_dev {
	endpoint_t ep[6];
	ep0_stage_t ep0_stage;

	struct usb_device_descriptor *   dev_desc;
	struct usb_interface_descriptor* if_desc;
	struct usb_config_descriptor *   conf_desc;
	u8 *                             full_conf_desc;
	struct usb_string_descriptor *   str_desc[6];

	/* callback to function layer */
	void (*func_cb)(usbdev_cb_type_t type, unsigned long arg,
			void *cb_data);
	void* cb_data;

	usbdev_state_t state;	// device state
	int suspended;		// suspended flag
	int address;		// device address
	int interface;
	int num_ep;
	u8 alternate_setting;
	u8 configuration;	// configuration value
	int remote_wakeup_en;
} usbdev;


static endpoint_reg_t ep_reg[] = {
	// FIFO's 0 and 1 are EP0 default control
	{USBD_EP0RD, USBD_EP0WR, USBD_EP0CS, USBD_EP0RDSTAT, USBD_EP0WRSTAT },
	{0},
	// FIFO 2 is EP2, IN
	{ -1, USBD_EP2WR, USBD_EP2CS, -1, USBD_EP2WRSTAT },
	// FIFO 3 is EP3, IN
	{    -1,     USBD_EP3WR, USBD_EP3CS,     -1,         USBD_EP3WRSTAT },
	// FIFO 4 is EP4, OUT
	{USBD_EP4RD,     -1,     USBD_EP4CS, USBD_EP4RDSTAT,     -1         },
	// FIFO 5 is EP5, OUT
	{USBD_EP5RD,     -1,     USBD_EP5CS, USBD_EP5RDSTAT,     -1         }
};

static struct {
	unsigned int id;
	const char *str;
} ep_dma_id[] = {
	{ DMA_ID_USBDEV_EP0_TX, "USBDev EP0 IN" },
	{ DMA_ID_USBDEV_EP0_RX, "USBDev EP0 OUT" },
	{ DMA_ID_USBDEV_EP2_TX, "USBDev EP2 IN" },
	{ DMA_ID_USBDEV_EP3_TX, "USBDev EP3 IN" },
	{ DMA_ID_USBDEV_EP4_RX, "USBDev EP4 OUT" },
	{ DMA_ID_USBDEV_EP5_RX, "USBDev EP5 OUT" }
};

#define DIR_OUT 0
#define DIR_IN  (1<<3)

#define CONTROL_EP USB_ENDPOINT_XFER_CONTROL
#define BULK_EP    USB_ENDPOINT_XFER_BULK

static inline endpoint_t *
epaddr_to_ep(struct usb_dev* dev, int ep_addr)
{
	if (ep_addr >= 0 && ep_addr < 2)
		return &dev->ep[0];
	if (ep_addr < 6)
		return &dev->ep[ep_addr];
	return NULL;
}

static const char* std_req_name[] = {
	"GET_STATUS",
	"CLEAR_FEATURE",
	"RESERVED",
	"SET_FEATURE",
	"RESERVED",
	"SET_ADDRESS",
	"GET_DESCRIPTOR",
	"SET_DESCRIPTOR",
	"GET_CONFIGURATION",
	"SET_CONFIGURATION",
	"GET_INTERFACE",
	"SET_INTERFACE",
	"SYNCH_FRAME"
};

static inline const char*
get_std_req_name(int req)
{
	return (req >= 0 && req <= 12) ? std_req_name[req] : "UNKNOWN";
}

#if 0
static void
dump_setup(struct usb_ctrlrequest* s)
{
	dbg("%s: requesttype=%d", __FUNCTION__, s->requesttype);
	dbg("%s: request=%d %s", __FUNCTION__, s->request,
	    get_std_req_name(s->request));
	dbg("%s: value=0x%04x", __FUNCTION__, s->wValue);
	dbg("%s: index=%d", __FUNCTION__, s->index);
	dbg("%s: length=%d", __FUNCTION__, s->length);
}
#endif

static inline usbdev_pkt_t *
alloc_packet(endpoint_t * ep, int data_size, void* data)
{
	usbdev_pkt_t* pkt = kmalloc(sizeof(usbdev_pkt_t) + data_size,
				    ALLOC_FLAGS);
	if (!pkt)
		return NULL;
	pkt->ep_addr = ep->address;
	pkt->size = data_size;
	pkt->status = 0;
	pkt->next = NULL;
	if (data)
		memcpy(pkt->payload, data, data_size);

	return pkt;
}


/*
 * Link a packet to the tail of the enpoint's packet list.
 * EP spinlock must be held when calling.
 */
static void
link_tail(endpoint_t * ep, pkt_list_t * list, usbdev_pkt_t * pkt)
{
	if (!list->tail) {
		list->head = list->tail = pkt;
		list->count = 1;
	} else {
		list->tail->next = pkt;
		list->tail = pkt;
		list->count++;
	}
}

/*
 * Unlink and return a packet from the head of the given packet
 * list. It is the responsibility of the caller to free the packet.
 * EP spinlock must be held when calling.
 */
static usbdev_pkt_t *
unlink_head(pkt_list_t * list)
{
	usbdev_pkt_t *pkt;

	pkt = list->head;
	if (!pkt || !list->count) {
		return NULL;
	}

	list->head = pkt->next;
	if (!list->head) {
		list->head = list->tail = NULL;
		list->count = 0;
	} else
		list->count--;

	return pkt;
}

/*
 * Create and attach a new packet to the tail of the enpoint's
 * packet list. EP spinlock must be held when calling.
 */
static usbdev_pkt_t *
add_packet(endpoint_t * ep, pkt_list_t * list, int size)
{
	usbdev_pkt_t *pkt = alloc_packet(ep, size, NULL);
	if (!pkt)
		return NULL;

	link_tail(ep, list, pkt);
	return pkt;
}


/*
 * Unlink and free a packet from the head of the enpoint's
 * packet list. EP spinlock must be held when calling.
 */
static inline void
free_packet(pkt_list_t * list)
{
	kfree(unlink_head(list));
}

/* EP spinlock must be held when calling. */
static inline void
flush_pkt_list(pkt_list_t * list)
{
	while (list->count)
		free_packet(list);
}

/* EP spinlock must be held when calling */
static inline void
flush_write_fifo(endpoint_t * ep)
{
	if (ep->reg->write_fifo_status >= 0) {
		au_writel(USBDEV_FSTAT_FLUSH | USBDEV_FSTAT_UF |
			  USBDEV_FSTAT_OF,
			  ep->reg->write_fifo_status);
		//udelay(100);
		//au_writel(USBDEV_FSTAT_UF | USBDEV_FSTAT_OF,
		//	  ep->reg->write_fifo_status);
	}
}

/* EP spinlock must be held when calling */
static inline void
flush_read_fifo(endpoint_t * ep)
{
	if (ep->reg->read_fifo_status >= 0) {
		au_writel(USBDEV_FSTAT_FLUSH | USBDEV_FSTAT_UF |
			  USBDEV_FSTAT_OF,
			  ep->reg->read_fifo_status);
		//udelay(100);
		//au_writel(USBDEV_FSTAT_UF | USBDEV_FSTAT_OF,
		//	  ep->reg->read_fifo_status);
	}
}


/* EP spinlock must be held when calling. */
static void
endpoint_flush(endpoint_t * ep)
{
	// First, flush all packets
	flush_pkt_list(&ep->inlist);
	flush_pkt_list(&ep->outlist);

	// Now flush the endpoint's h/w FIFO(s)
	flush_write_fifo(ep);
	flush_read_fifo(ep);
}

/* EP spinlock must be held when calling. */
static void
endpoint_stall(endpoint_t * ep)
{
	u32 cs;

	warn(__FUNCTION__);

	cs = au_readl(ep->reg->ctrl_stat) | USBDEV_CS_STALL;
	au_writel(cs, ep->reg->ctrl_stat);
}

/* EP spinlock must be held when calling. */
static void
endpoint_unstall(endpoint_t * ep)
{
	u32 cs;

	warn(__FUNCTION__);

	cs = au_readl(ep->reg->ctrl_stat) & ~USBDEV_CS_STALL;
	au_writel(cs, ep->reg->ctrl_stat);
}

static void
endpoint_reset_datatoggle(endpoint_t * ep)
{
	// FIXME: is this possible?
}


/* EP spinlock must be held when calling. */
static int
endpoint_fifo_read(endpoint_t * ep)
{
	int read_count = 0;
	u8 *bufptr;
	usbdev_pkt_t *pkt = ep->outlist.tail;

	if (!pkt)
		return -EINVAL;

	bufptr = &pkt->payload[pkt->size];
	while (au_readl(ep->reg->read_fifo_status) & USBDEV_FSTAT_FCNT_MASK) {
		*bufptr++ = au_readl(ep->reg->read_fifo) & 0xff;
		read_count++;
		pkt->size++;
	}

	return read_count;
}

#if 0
/* EP spinlock must be held when calling. */
static int
endpoint_fifo_write(endpoint_t * ep, int index)
{
	int write_count = 0;
	u8 *bufptr;
	usbdev_pkt_t *pkt = ep->inlist.head;

	if (!pkt)
		return -EINVAL;

	bufptr = &pkt->payload[index];
	while ((au_readl(ep->reg->write_fifo_status) &
		USBDEV_FSTAT_FCNT_MASK) < EP_FIFO_DEPTH) {
		if (bufptr < pkt->payload + pkt->size) {
			au_writel(*bufptr++, ep->reg->write_fifo);
			write_count++;
		} else {
			break;
		}
	}

	return write_count;
}
#endif

/*
 * This routine is called to restart transmission of a packet.
 * The endpoint's TSIZE must be set to the new packet's size,
 * and DMA to the write FIFO needs to be restarted.
 * EP spinlock must be held when calling.
 */
static void
kickstart_send_packet(endpoint_t * ep)
{
	u32 cs;
	usbdev_pkt_t *pkt = ep->inlist.head;

	vdbg("%s: ep%d, pkt=%p", __FUNCTION__, ep->address, pkt);

	if (!pkt) {
		err("%s: head=NULL! list->count=%d", __FUNCTION__,
		    ep->inlist.count);
		return;
	}

	dma_cache_wback_inv((unsigned long)pkt->payload, pkt->size);

	/*
	 * make sure FIFO is empty
	 */
	flush_write_fifo(ep);

	cs = au_readl(ep->reg->ctrl_stat) & USBDEV_CS_STALL;
	cs |= (pkt->size << USBDEV_CS_TSIZE_BIT);
	au_writel(cs, ep->reg->ctrl_stat);

	if (get_dma_active_buffer(ep->indma) == 1) {
		set_dma_count1(ep->indma, pkt->size);
		set_dma_addr1(ep->indma, virt_to_phys(pkt->payload));
		enable_dma_buffer1(ep->indma);	// reenable
	} else {
		set_dma_count0(ep->indma, pkt->size);
		set_dma_addr0(ep->indma, virt_to_phys(pkt->payload));
		enable_dma_buffer0(ep->indma);	// reenable
	}
	if (dma_halted(ep->indma))
		start_dma(ep->indma);
}


/*
 * This routine is called when a packet in the inlist has been
 * completed. Frees the completed packet and starts sending the
 * next. EP spinlock must be held when calling.
 */
static usbdev_pkt_t *
send_packet_complete(endpoint_t * ep)
{
	usbdev_pkt_t *pkt = unlink_head(&ep->inlist);

	if (pkt) {
		pkt->status =
			(au_readl(ep->reg->ctrl_stat) & USBDEV_CS_NAK) ?
			PKT_STATUS_NAK : PKT_STATUS_ACK;

		vdbg("%s: ep%d, %s pkt=%p, list count=%d", __FUNCTION__,
		     ep->address, (pkt->status & PKT_STATUS_NAK) ?
		     "NAK" : "ACK", pkt, ep->inlist.count);
	}

	/*
	 * The write fifo should already be drained if things are
	 * working right, but flush it anyway just in case.
	 */
	flush_write_fifo(ep);

	// begin transmitting next packet in the inlist
	if (ep->inlist.count) {
		kickstart_send_packet(ep);
	}

	return pkt;
}

/*
 * Add a new packet to the tail of the given ep's packet
 * inlist. The transmit complete interrupt frees packets from
 * the head of this list. EP spinlock must be held when calling.
 */
static int
send_packet(struct usb_dev* dev, usbdev_pkt_t *pkt, int async)
{
	pkt_list_t *list;
	endpoint_t* ep;

	if (!pkt || !(ep = epaddr_to_ep(dev, pkt->ep_addr)))
		return -EINVAL;

	if (!pkt->size)
		return 0;

	list = &ep->inlist;

	if (!async && list->count) {
		halt_dma(ep->indma);
		flush_pkt_list(list);
	}

	link_tail(ep, list, pkt);

	vdbg("%s: ep%d, pkt=%p, size=%d, list count=%d", __FUNCTION__,
	     ep->address, pkt, pkt->size, list->count);

	if (list->count == 1) {
		/*
		 * if the packet count is one, it means the list was empty,
		 * and no more data will go out this ep until we kick-start
		 * it again.
		 */
		kickstart_send_packet(ep);
	}

	return pkt->size;
}

/*
 * This routine is called to restart reception of a packet.
 * EP spinlock must be held when calling.
 */
static void
kickstart_receive_packet(endpoint_t * ep)
{
	usbdev_pkt_t *pkt;

	// get and link a new packet for next reception
	if (!(pkt = add_packet(ep, &ep->outlist, ep->max_pkt_size))) {
		err("%s: could not alloc new packet", __FUNCTION__);
		return;
	}

	if (get_dma_active_buffer(ep->outdma) == 1) {
		clear_dma_done1(ep->outdma);
		set_dma_count1(ep->outdma, ep->max_pkt_size);
		set_dma_count0(ep->outdma, 0);
		set_dma_addr1(ep->outdma, virt_to_phys(pkt->payload));
		enable_dma_buffer1(ep->outdma);	// reenable
	} else {
		clear_dma_done0(ep->outdma);
		set_dma_count0(ep->outdma, ep->max_pkt_size);
		set_dma_count1(ep->outdma, 0);
		set_dma_addr0(ep->outdma, virt_to_phys(pkt->payload));
		enable_dma_buffer0(ep->outdma);	// reenable
	}
	if (dma_halted(ep->outdma))
		start_dma(ep->outdma);
}


/*
 * This routine is called when a packet in the outlist has been
 * completed (received) and we need to prepare for a new packet
 * to be received. Halts DMA and computes the packet size from the
 * remaining DMA counter. Then prepares a new packet for reception
 * and restarts DMA. FIXME: what if another packet comes in
 * on top of the completed packet? Counter would be wrong.
 * EP spinlock must be held when calling.
 */
static usbdev_pkt_t *
receive_packet_complete(endpoint_t * ep)
{
	usbdev_pkt_t *pkt = ep->outlist.tail;
	u32 cs;

	halt_dma(ep->outdma);

	cs = au_readl(ep->reg->ctrl_stat);

	if (!pkt)
		return NULL;

	pkt->size = ep->max_pkt_size - get_dma_residue(ep->outdma);
	if (pkt->size)
		dma_cache_inv((unsigned long)pkt->payload, pkt->size);
	/*
	 * need to pull out any remaining bytes in the FIFO.
	 */
	endpoint_fifo_read(ep);
	/*
	 * should be drained now, but flush anyway just in case.
	 */
	flush_read_fifo(ep);

	pkt->status = (cs & USBDEV_CS_NAK) ? PKT_STATUS_NAK : PKT_STATUS_ACK;
	if (ep->address == 0 && (cs & USBDEV_CS_SU))
		pkt->status |= PKT_STATUS_SU;

	vdbg("%s: ep%d, %s pkt=%p, size=%d", __FUNCTION__,
	     ep->address, (pkt->status & PKT_STATUS_NAK) ?
	     "NAK" : "ACK", pkt, pkt->size);

	kickstart_receive_packet(ep);

	return pkt;
}


/*
 ****************************************************************************
 * Here starts the standard device request handlers. They are
 * all called by do_setup() via a table of function pointers.
 ****************************************************************************
 */

static ep0_stage_t
do_get_status(struct usb_dev* dev, struct usb_ctrlrequest* setup)
{
	switch (setup->bRequestType) {
	case 0x80:	// Device
		// FIXME: send device status
		break;
	case 0x81:	// Interface
		// FIXME: send interface status
		break;
	case 0x82:	// End Point
		// FIXME: send endpoint status
		break;
	default:
		// Invalid Command
		endpoint_stall(&dev->ep[0]); // Stall End Point 0
		break;
	}

	return STATUS_STAGE;
}

static ep0_stage_t
do_clear_feature(struct usb_dev* dev, struct usb_ctrlrequest* setup)
{
	switch (setup->bRequestType) {
	case 0x00:	// Device
		if ((le16_to_cpu(setup->wValue) & 0xff) == 1)
			dev->remote_wakeup_en = 0;
	else
			endpoint_stall(&dev->ep[0]);
		break;
	case 0x02:	// End Point
		if ((le16_to_cpu(setup->wValue) & 0xff) == 0) {
			endpoint_t *ep =
				epaddr_to_ep(dev,
					     le16_to_cpu(setup->wIndex) & 0xff);

			endpoint_unstall(ep);
			endpoint_reset_datatoggle(ep);
		} else
			endpoint_stall(&dev->ep[0]);
		break;
	}

	return SETUP_STAGE;
}

static ep0_stage_t
do_reserved(struct usb_dev* dev, struct usb_ctrlrequest* setup)
{
	// Invalid request, stall End Point 0
	endpoint_stall(&dev->ep[0]);
	return SETUP_STAGE;
}

static ep0_stage_t
do_set_feature(struct usb_dev* dev, struct usb_ctrlrequest* setup)
{
	switch (setup->bRequestType) {
	case 0x00:	// Device
		if ((le16_to_cpu(setup->wValue) & 0xff) == 1)
			dev->remote_wakeup_en = 1;
		else
			endpoint_stall(&dev->ep[0]);
		break;
	case 0x02:	// End Point
		if ((le16_to_cpu(setup->wValue) & 0xff) == 0) {
			endpoint_t *ep =
				epaddr_to_ep(dev,
					     le16_to_cpu(setup->wIndex) & 0xff);

			endpoint_stall(ep);
		} else
			endpoint_stall(&dev->ep[0]);
		break;
	}

	return SETUP_STAGE;
}

static ep0_stage_t
do_set_address(struct usb_dev* dev, struct usb_ctrlrequest* setup)
{
	int new_state = dev->state;
	int new_addr = le16_to_cpu(setup->wValue);

	dbg("%s: our address=%d", __FUNCTION__, new_addr);

	if (new_addr > 127) {
			// usb spec doesn't tell us what to do, so just go to
			// default state
		new_state = DEFAULT;
		dev->address = 0;
	} else if (dev->address != new_addr) {
		dev->address = new_addr;
		new_state = ADDRESS;
	}

	if (dev->state != new_state) {
		dev->state = new_state;
		/* inform function layer of usbdev state change */
		dev->func_cb(CB_NEW_STATE, dev->state, dev->cb_data);
	}

	return SETUP_STAGE;
}

static ep0_stage_t
do_get_descriptor(struct usb_dev* dev, struct usb_ctrlrequest* setup)
{
	int strnum, desc_len = le16_to_cpu(setup->wLength);

		switch (le16_to_cpu(setup->wValue) >> 8) {
		case USB_DT_DEVICE:
			// send device descriptor!
		desc_len = desc_len > dev->dev_desc->bLength ?
			dev->dev_desc->bLength : desc_len;
			dbg("sending device desc, size=%d", desc_len);
		send_packet(dev, alloc_packet(&dev->ep[0], desc_len,
					      dev->dev_desc), 0);
			break;
		case USB_DT_CONFIG:
			// If the config descr index in low-byte of
			// setup->wValue	is valid, send config descr,
			// otherwise stall ep0.
			if ((le16_to_cpu(setup->wValue) & 0xff) == 0) {
				// send config descriptor!
				if (desc_len <= USB_DT_CONFIG_SIZE) {
					dbg("sending partial config desc, size=%d",
					     desc_len);
				send_packet(dev,
					    alloc_packet(&dev->ep[0],
							 desc_len,
							 dev->conf_desc),
					    0);
				} else {
				int len = le16_to_cpu(dev->conf_desc->wTotalLength);
				dbg("sending whole config desc,"
				    " size=%d, our size=%d", desc_len, len);
				desc_len = desc_len > len ? len : desc_len;
				send_packet(dev,
					    alloc_packet(&dev->ep[0],
							 desc_len,
							 dev->full_conf_desc),
					    0);
				}
			} else
			endpoint_stall(&dev->ep[0]);
			break;
		case USB_DT_STRING:
			// If the string descr index in low-byte of setup->wValue
			// is valid, send string descr, otherwise stall ep0.
			strnum = le16_to_cpu(setup->wValue) & 0xff;
			if (strnum >= 0 && strnum < 6) {
				struct usb_string_descriptor *desc =
				dev->str_desc[strnum];
				desc_len = desc_len > desc->bLength ?
					desc->bLength : desc_len;
				dbg("sending string desc %d", strnum);
			send_packet(dev,
				    alloc_packet(&dev->ep[0], desc_len,
						 desc), 0);
			} else
			endpoint_stall(&dev->ep[0]);
			break;
	default:
		// Invalid request
		err("invalid get desc=%d, stalled",
			    le16_to_cpu(setup->wValue) >> 8);
		endpoint_stall(&dev->ep[0]);	// Stall endpoint 0
			break;
		}

	return STATUS_STAGE;
}

static ep0_stage_t
do_set_descriptor(struct usb_dev* dev, struct usb_ctrlrequest* setup)
{
	// TODO: implement
	// there will be an OUT data stage (the descriptor to set)
	return DATA_STAGE;
}

static ep0_stage_t
do_get_configuration(struct usb_dev* dev, struct usb_ctrlrequest* setup)
{
	// send dev->configuration
	dbg("sending config");
	send_packet(dev, alloc_packet(&dev->ep[0], 1, &dev->configuration),
		    0);
	return STATUS_STAGE;
}

static ep0_stage_t
do_set_configuration(struct usb_dev* dev, struct usb_ctrlrequest* setup)
{
	// set active config to low-byte of setup->wValue
	dev->configuration = le16_to_cpu(setup->wValue) & 0xff;
	dbg("set config, config=%d", dev->configuration);
	if (!dev->configuration && dev->state > DEFAULT) {
		dev->state = ADDRESS;
		/* inform function layer of usbdev state change */
		dev->func_cb(CB_NEW_STATE, dev->state, dev->cb_data);
	} else if (dev->configuration == 1) {
		dev->state = CONFIGURED;
		/* inform function layer of usbdev state change */
		dev->func_cb(CB_NEW_STATE, dev->state, dev->cb_data);
	} else {
		// FIXME: "respond with request error" - how?
	}

	return SETUP_STAGE;
}

static ep0_stage_t
do_get_interface(struct usb_dev* dev, struct usb_ctrlrequest* setup)
{
		// interface must be zero.
	if ((le16_to_cpu(setup->wIndex) & 0xff) || dev->state == ADDRESS) {
			// FIXME: respond with "request error". how?
	} else if (dev->state == CONFIGURED) {
		// send dev->alternate_setting
			dbg("sending alt setting");
		send_packet(dev, alloc_packet(&dev->ep[0], 1,
					      &dev->alternate_setting), 0);
		}

	return STATUS_STAGE;

}

static ep0_stage_t
do_set_interface(struct usb_dev* dev, struct usb_ctrlrequest* setup)
{
	if (dev->state == ADDRESS) {
			// FIXME: respond with "request error". how?
	} else if (dev->state == CONFIGURED) {
		dev->interface = le16_to_cpu(setup->wIndex) & 0xff;
		dev->alternate_setting =
			    le16_to_cpu(setup->wValue) & 0xff;
			// interface and alternate_setting must be zero
		if (dev->interface || dev->alternate_setting) {
				// FIXME: respond with "request error". how?
			}
		}

	return SETUP_STAGE;
}

static ep0_stage_t
do_synch_frame(struct usb_dev* dev, struct usb_ctrlrequest* setup)
{
	// TODO
	return SETUP_STAGE;
}

typedef ep0_stage_t (*req_method_t)(struct usb_dev* dev,
				    struct usb_ctrlrequest* setup);


/* Table of the standard device request handlers */
static const req_method_t req_method[] = {
	do_get_status,
	do_clear_feature,
	do_reserved,
	do_set_feature,
	do_reserved,
	do_set_address,
	do_get_descriptor,
	do_set_descriptor,
	do_get_configuration,
	do_set_configuration,
	do_get_interface,
	do_set_interface,
	do_synch_frame
};


// SETUP packet request dispatcher
static void
do_setup (struct usb_dev* dev, struct usb_ctrlrequest* setup)
{
	req_method_t m;

	dbg("%s: req %d %s", __FUNCTION__, setup->bRequestType,
	    get_std_req_name(setup->bRequestType));

	if ((setup->bRequestType & USB_TYPE_MASK) != USB_TYPE_STANDARD ||
	    (setup->bRequestType & USB_RECIP_MASK) != USB_RECIP_DEVICE) {
		err("%s: invalid requesttype 0x%02x", __FUNCTION__,
		    setup->bRequestType);
		return;
		}

	if ((setup->bRequestType & 0x80) == USB_DIR_OUT && setup->wLength)
		dbg("%s: OUT phase! length=%d", __FUNCTION__, setup->wLength);

	if (setup->bRequestType < sizeof(req_method)/sizeof(req_method_t))
		m = req_method[setup->bRequestType];
			else
		m = do_reserved;

	dev->ep0_stage = (*m)(dev, setup);
}

/*
 * A SETUP, DATA0, or DATA1 packet has been received
 * on the default control endpoint's fifo.
 */
static void
process_ep0_receive (struct usb_dev* dev)
{
	endpoint_t *ep0 = &dev->ep[0];
	usbdev_pkt_t *pkt;

	spin_lock(&ep0->lock);

		// complete packet and prepare a new packet
	pkt = receive_packet_complete(ep0);
	if (!pkt) {
		// FIXME: should  put a warn/err here.
		spin_unlock(&ep0->lock);
			return;
		}

	// unlink immediately from endpoint.
	unlink_head(&ep0->outlist);

	// override current stage if h/w says it's a setup packet
	if (pkt->status & PKT_STATUS_SU)
		dev->ep0_stage = SETUP_STAGE;

	switch (dev->ep0_stage) {
	case SETUP_STAGE:
		vdbg("SU bit is %s in setup stage",
		     (pkt->status & PKT_STATUS_SU) ? "set" : "not set");

			if (pkt->size == sizeof(struct usb_ctrlrequest)) {
#ifdef VDEBUG
			if (pkt->status & PKT_STATUS_ACK)
				vdbg("received SETUP");
				else
				vdbg("received NAK SETUP");
#endif
			do_setup(dev, (struct usb_ctrlrequest*)pkt->payload);
		} else
			err("%s: wrong size SETUP received", __FUNCTION__);
		break;
	case DATA_STAGE:
		/*
		 * this setup has an OUT data stage. Of the standard
		 * device requests, only set_descriptor has this stage,
		 * so this packet is that descriptor. TODO: drop it for
		 * now, set_descriptor not implemented.
		 *
		 * Need to place a byte in the write FIFO here, to prepare
		 * to send a zero-length DATA ack packet to the host in the
		 * STATUS stage.
		 */
		au_writel(0, ep0->reg->write_fifo);
		dbg("received OUT stage DATAx on EP0, size=%d", pkt->size);
		dev->ep0_stage = SETUP_STAGE;
		break;
	case STATUS_STAGE:
		// this setup had an IN data stage, and host is ACK'ing
		// the packet we sent during that stage.
		if (pkt->size != 0)
			warn("received non-zero ACK on EP0??");
#ifdef VDEBUG
		else
			vdbg("received ACK on EP0");
#endif
		dev->ep0_stage = SETUP_STAGE;
		break;
		}

	spin_unlock(&ep0->lock);
		// we're done processing the packet, free it
		kfree(pkt);
}


/*
 * A DATA0/1 packet has been received on one of the OUT endpoints (4 or 5)
 */
static void
process_ep_receive (struct usb_dev* dev, endpoint_t *ep)
{
	usbdev_pkt_t *pkt;

		spin_lock(&ep->lock);
	pkt = receive_packet_complete(ep);
		spin_unlock(&ep->lock);

	dev->func_cb(CB_PKT_COMPLETE, (unsigned long)pkt, dev->cb_data);
}



/* This ISR handles the receive complete and suspend events */
static void
req_sus_intr (int irq, void *dev_id, struct pt_regs *regs)
{
	struct usb_dev *dev = (struct usb_dev *) dev_id;
	u32 status;

	status = au_readl(USBD_INTSTAT);
	au_writel(status, USBD_INTSTAT);	// ack'em

	if (status & (1<<0))
		process_ep0_receive(dev);
	if (status & (1<<4))
		process_ep_receive(dev, &dev->ep[4]);
	if (status & (1<<5))
		process_ep_receive(dev, &dev->ep[5]);
}


/* This ISR handles the DMA done events on EP0 */
static void
dma_done_ep0_intr(int irq, void *dev_id, struct pt_regs *regs)
{
	struct usb_dev *dev = (struct usb_dev *) dev_id;
	usbdev_pkt_t* pkt;
	endpoint_t *ep0 = &dev->ep[0];
	u32 cs0, buff_done;

	spin_lock(&ep0->lock);
	cs0 = au_readl(ep0->reg->ctrl_stat);

	// first check packet transmit done
	if ((buff_done = get_dma_buffer_done(ep0->indma)) != 0) {
		// transmitted a DATAx packet during DATA stage
		// on control endpoint 0
		// clear DMA done bit
		if (buff_done & DMA_D0)
			clear_dma_done0(ep0->indma);
		if (buff_done & DMA_D1)
			clear_dma_done1(ep0->indma);

		pkt = send_packet_complete(ep0);
		if (pkt)
			kfree(pkt);
	}

	/*
	 * Now check packet receive done. Shouldn't get these,
	 * the receive packet complete intr should happen
	 * before the DMA done intr occurs.
	 */
	if ((buff_done = get_dma_buffer_done(ep0->outdma)) != 0) {
		// clear DMA done bit
		if (buff_done & DMA_D0)
			clear_dma_done0(ep0->outdma);
		if (buff_done & DMA_D1)
			clear_dma_done1(ep0->outdma);

		//process_ep0_receive(dev);
	}

	spin_unlock(&ep0->lock);
}

/* This ISR handles the DMA done events on endpoints 2,3,4,5 */
static void
dma_done_ep_intr(int irq, void *dev_id, struct pt_regs *regs)
{
	struct usb_dev *dev = (struct usb_dev *) dev_id;
	int i;

	for (i = 2; i < 6; i++) {
	u32 buff_done;
		usbdev_pkt_t* pkt;
		endpoint_t *ep = &dev->ep[i];

		if (!ep->active) continue;

	spin_lock(&ep->lock);

		if (ep->direction == USB_DIR_IN) {
			buff_done = get_dma_buffer_done(ep->indma);
			if (buff_done != 0) {
				// transmitted a DATAx pkt on the IN ep
		// clear DMA done bit
		if (buff_done & DMA_D0)
			clear_dma_done0(ep->indma);
		if (buff_done & DMA_D1)
			clear_dma_done1(ep->indma);

				pkt = send_packet_complete(ep);

				spin_unlock(&ep->lock);
				dev->func_cb(CB_PKT_COMPLETE,
					     (unsigned long)pkt,
					     dev->cb_data);
				spin_lock(&ep->lock);
			}
		} else {
	/*
			 * Check packet receive done (OUT ep). Shouldn't get
			 * these, the rx packet complete intr should happen
	 * before the DMA done intr occurs.
	 */
			buff_done = get_dma_buffer_done(ep->outdma);
			if (buff_done != 0) {
				// received a DATAx pkt on the OUT ep
		// clear DMA done bit
		if (buff_done & DMA_D0)
			clear_dma_done0(ep->outdma);
		if (buff_done & DMA_D1)
			clear_dma_done1(ep->outdma);

				//process_ep_receive(dev, ep);
	}
	}

		spin_unlock(&ep->lock);
	}
}


/***************************************************************************
 * Here begins the external interface functions
 ***************************************************************************
 */

/*
 * allocate a new packet
 */
int
usbdev_alloc_packet(int ep_addr, int data_size, usbdev_pkt_t** pkt)
{
	endpoint_t * ep = epaddr_to_ep(&usbdev, ep_addr);
	usbdev_pkt_t* lpkt = NULL;

	if (!ep || !ep->active || ep->address < 2)
		return -ENODEV;
	if (data_size > ep->max_pkt_size)
		return -EINVAL;

	lpkt = *pkt = alloc_packet(ep, data_size, NULL);
	if (!lpkt)
		return -ENOMEM;
	return 0;
}


/*
 * packet send
 */
int
usbdev_send_packet(int ep_addr, usbdev_pkt_t * pkt)
{
	unsigned long flags;
	int count;
	endpoint_t * ep;

	if (!pkt || !(ep = epaddr_to_ep(&usbdev, pkt->ep_addr)) ||
	    !ep->active || ep->address < 2)
		return -ENODEV;
	if (ep->direction != USB_DIR_IN)
		return -EINVAL;

	spin_lock_irqsave(&ep->lock, flags);
	count = send_packet(&usbdev, pkt, 1);
	spin_unlock_irqrestore(&ep->lock, flags);

	return count;
}

/*
 * packet receive
 */
int
usbdev_receive_packet(int ep_addr, usbdev_pkt_t** pkt)
{
	unsigned long flags;
	usbdev_pkt_t* lpkt = NULL;
	endpoint_t *ep = epaddr_to_ep(&usbdev, ep_addr);

	if (!ep || !ep->active || ep->address < 2)
		return -ENODEV;
	if (ep->direction != USB_DIR_OUT)
		return -EINVAL;

	spin_lock_irqsave(&ep->lock, flags);
	if (ep->outlist.count > 1)
		lpkt = unlink_head(&ep->outlist);
	spin_unlock_irqrestore(&ep->lock, flags);

	if (!lpkt) {
		/* no packet available */
		*pkt = NULL;
		return -ENODATA;
	}

	*pkt = lpkt;

	return lpkt->size;
}


/*
 * return total queued byte count on the endpoint.
 */
int
usbdev_get_byte_count(int ep_addr)
{
        unsigned long flags;
        pkt_list_t *list;
        usbdev_pkt_t *scan;
        int count = 0;
	endpoint_t * ep = epaddr_to_ep(&usbdev, ep_addr);

	if (!ep || !ep->active || ep->address < 2)
		return -ENODEV;

	if (ep->direction == USB_DIR_IN) {
		list = &ep->inlist;

		spin_lock_irqsave(&ep->lock, flags);
		for (scan = list->head; scan; scan = scan->next)
			count += scan->size;
		spin_unlock_irqrestore(&ep->lock, flags);
	} else {
		list = &ep->outlist;

		spin_lock_irqsave(&ep->lock, flags);
		if (list->count > 1) {
			for (scan = list->head; scan != list->tail;
			     scan = scan->next)
				count += scan->size;
	}
		spin_unlock_irqrestore(&ep->lock, flags);
	}

	return count;
}


void
usbdev_exit(void)
{
	endpoint_t *ep;
	int i;

	au_writel(0, USBD_INTEN);	// disable usb dev ints
	au_writel(0, USBD_ENABLE);	// disable usb dev

	free_irq(AU1000_USB_DEV_REQ_INT, &usbdev);
	free_irq(AU1000_USB_DEV_SUS_INT, &usbdev);

	// free all control endpoint resources
	ep = &usbdev.ep[0];
	free_au1000_dma(ep->indma);
	free_au1000_dma(ep->outdma);
	endpoint_flush(ep);

	// free ep resources
	for (i = 2; i < 6; i++) {
		ep = &usbdev.ep[i];
		if (!ep->active) continue;

		if (ep->direction == USB_DIR_IN) {
			free_au1000_dma(ep->indma);
		} else {
		free_au1000_dma(ep->outdma);
		}
		endpoint_flush(ep);
	}

	if (usbdev.full_conf_desc)
		kfree(usbdev.full_conf_desc);
}

int
usbdev_init(struct usb_device_descriptor* dev_desc,
	    struct usb_config_descriptor* config_desc,
	    struct usb_interface_descriptor* if_desc,
	    struct usb_endpoint_descriptor* ep_desc,
	    struct usb_string_descriptor* str_desc[],
	    void (*cb)(usbdev_cb_type_t, unsigned long, void *),
	    void* cb_data)
{
	endpoint_t *ep0;
	int i, ret=0;
	u8* fcd;

	if (dev_desc->bNumConfigurations > 1 ||
	    config_desc->bNumInterfaces > 1 ||
	    if_desc->bNumEndpoints > 4) {
		err("Only one config, one i/f, and no more "
		    "than 4 ep's allowed");
		ret = -EINVAL;
		goto out;
	}

	if (!cb) {
		err("Function-layer callback required");
		ret = -EINVAL;
		goto out;
	}

	if (dev_desc->bMaxPacketSize0 != USBDEV_EP0_MAX_PACKET_SIZE) {
		warn("EP0 Max Packet size must be %d",
		     USBDEV_EP0_MAX_PACKET_SIZE);
		dev_desc->bMaxPacketSize0 = USBDEV_EP0_MAX_PACKET_SIZE;
	}

	memset(&usbdev, 0, sizeof(struct usb_dev));

	usbdev.state = DEFAULT;
	usbdev.dev_desc = dev_desc;
	usbdev.if_desc = if_desc;
	usbdev.conf_desc = config_desc;
	for (i=0; i<6; i++)
		usbdev.str_desc[i] = str_desc[i];
	usbdev.func_cb = cb;
	usbdev.cb_data = cb_data;

	/* Initialize default control endpoint */
	ep0 = &usbdev.ep[0];
	ep0->active = 1;
	ep0->type = CONTROL_EP;
	ep0->max_pkt_size = USBDEV_EP0_MAX_PACKET_SIZE;
	spin_lock_init(&ep0->lock);
	ep0->desc = NULL;	// ep0 has no descriptor
	ep0->address = 0;
	ep0->direction = 0;
	ep0->reg = &ep_reg[0];

	/* Initialize the other requested endpoints */
	for (i = 0; i < if_desc->bNumEndpoints; i++) {
		struct usb_endpoint_descriptor* epd = &ep_desc[i];
	endpoint_t *ep;

		if ((epd->bEndpointAddress & 0x80) == USB_DIR_IN) {
			ep = &usbdev.ep[2];
			ep->address = 2;
			if (ep->active) {
				ep = &usbdev.ep[3];
				ep->address = 3;
				if (ep->active) {
					err("too many IN ep's requested");
					ret = -ENODEV;
					goto out;
	}
	}
		} else {
			ep = &usbdev.ep[4];
			ep->address = 4;
			if (ep->active) {
				ep = &usbdev.ep[5];
				ep->address = 5;
				if (ep->active) {
					err("too many OUT ep's requested");
					ret = -ENODEV;
					goto out;
	}
	}
		}

		ep->active = 1;
		epd->bEndpointAddress &= ~0x0f;
		epd->bEndpointAddress |= (u8)ep->address;
		ep->direction = epd->bEndpointAddress & 0x80;
		ep->type = epd->bmAttributes & 0x03;
		ep->max_pkt_size = le16_to_cpu(epd->wMaxPacketSize);
		spin_lock_init(&ep->lock);
		ep->desc = epd;
		ep->reg = &ep_reg[ep->address];
		}

	/*
	 * initialize the full config descriptor
	 */
	usbdev.full_conf_desc = fcd = kmalloc(le16_to_cpu(config_desc->wTotalLength),
					      ALLOC_FLAGS);
	if (!fcd) {
		err("failed to alloc full config descriptor");
		ret = -ENOMEM;
		goto out;
	}

	memcpy(fcd, config_desc, USB_DT_CONFIG_SIZE);
	fcd += USB_DT_CONFIG_SIZE;
	memcpy(fcd, if_desc, USB_DT_INTERFACE_SIZE);
	fcd += USB_DT_INTERFACE_SIZE;
	for (i = 0; i < if_desc->bNumEndpoints; i++) {
		memcpy(fcd, &ep_desc[i], USB_DT_ENDPOINT_SIZE);
		fcd += USB_DT_ENDPOINT_SIZE;
	}

	/* Now we're ready to enable the controller */
	au_writel(0x0002, USBD_ENABLE);
	udelay(100);
	au_writel(0x0003, USBD_ENABLE);
	udelay(100);

	/* build and send config table based on ep descriptors */
	for (i = 0; i < 6; i++) {
		endpoint_t *ep;
		if (i == 1)
			continue; // skip dummy ep
		ep = &usbdev.ep[i];
		if (ep->active) {
			au_writel((ep->address << 4) | 0x04, USBD_CONFIG);
			au_writel(((ep->max_pkt_size & 0x380) >> 7) |
				  (ep->direction >> 4) | (ep->type << 4),
				  USBD_CONFIG);
			au_writel((ep->max_pkt_size & 0x7f) << 1, USBD_CONFIG);
			au_writel(0x00, USBD_CONFIG);
			au_writel(ep->address, USBD_CONFIG);
		} else {
			u8 dir = (i==2 || i==3) ? DIR_IN : DIR_OUT;
			au_writel((i << 4) | 0x04, USBD_CONFIG);
			au_writel(((16 & 0x380) >> 7) | dir |
				  (BULK_EP << 4), USBD_CONFIG);
			au_writel((16 & 0x7f) << 1, USBD_CONFIG);
			au_writel(0x00, USBD_CONFIG);
			au_writel(i, USBD_CONFIG);
		}
	}

	/*
	 * Enable Receive FIFO Complete interrupts only. Transmit
	 * complete is being handled by the DMA done interrupts.
	 */
	au_writel(0x31, USBD_INTEN);

	/*
	 * Controller is now enabled, request DMA and IRQ
	 * resources.
	 */

	/* request the USB device transfer complete interrupt */
	if (request_irq(AU1000_USB_DEV_REQ_INT, req_sus_intr, SA_INTERRUPT,
			"USBdev req", &usbdev)) {
		err("Can't get device request intr");
		ret = -ENXIO;
		goto out;
	}
	/* request the USB device suspend interrupt */
	if (request_irq(AU1000_USB_DEV_SUS_INT, req_sus_intr, SA_INTERRUPT,
			"USBdev sus", &usbdev)) {
		err("Can't get device suspend intr");
		ret = -ENXIO;
		goto out;
	}

	/* Request EP0 DMA and IRQ */
	if ((ep0->indma = request_au1000_dma(ep_dma_id[0].id,
					     ep_dma_id[0].str,
					     dma_done_ep0_intr,
					     SA_INTERRUPT,
					     &usbdev)) < 0) {
		err("Can't get %s DMA", ep_dma_id[0].str);
		ret = -ENXIO;
		goto out;
	}
	if ((ep0->outdma = request_au1000_dma(ep_dma_id[1].id,
					      ep_dma_id[1].str,
					      NULL, 0, NULL)) < 0) {
		err("Can't get %s DMA", ep_dma_id[1].str);
		ret = -ENXIO;
		goto out;
	}

	// Flush the ep0 buffers and FIFOs
	endpoint_flush(ep0);
	// start packet reception on ep0
	kickstart_receive_packet(ep0);

	/* Request DMA and IRQ for the other endpoints */
	for (i = 2; i < 6; i++) {
		endpoint_t *ep = &usbdev.ep[i];
		if (!ep->active)
			continue;

		// Flush the endpoint buffers and FIFOs
		endpoint_flush(ep);

		if (ep->direction == USB_DIR_IN) {
			ep->indma =
				request_au1000_dma(ep_dma_id[ep->address].id,
						   ep_dma_id[ep->address].str,
						   dma_done_ep_intr,
						   SA_INTERRUPT,
						   &usbdev);
			if (ep->indma < 0) {
				err("Can't get %s DMA",
				    ep_dma_id[ep->address].str);
				ret = -ENXIO;
				goto out;
			}
		} else {
			ep->outdma =
				request_au1000_dma(ep_dma_id[ep->address].id,
						   ep_dma_id[ep->address].str,
						   NULL, 0, NULL);
			if (ep->outdma < 0) {
				err("Can't get %s DMA",
				    ep_dma_id[ep->address].str);
				ret = -ENXIO;
				goto out;
			}

			// start packet reception on OUT endpoint
			kickstart_receive_packet(ep);
		}
	}

 out:
	if (ret)
		usbdev_exit();
	return ret;
}

EXPORT_SYMBOL(usbdev_init);
EXPORT_SYMBOL(usbdev_exit);
EXPORT_SYMBOL(usbdev_alloc_packet);
EXPORT_SYMBOL(usbdev_receive_packet);
EXPORT_SYMBOL(usbdev_send_packet);
EXPORT_SYMBOL(usbdev_get_byte_count);

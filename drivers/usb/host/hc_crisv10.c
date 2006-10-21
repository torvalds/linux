/*
 * usb-host.c: ETRAX 100LX USB Host Controller Driver (HCD)
 *
 * Copyright (c) 2002, 2003 Axis Communications AB.
 */

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/unistd.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/spinlock.h>

#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/dma.h>
#include <asm/system.h>
#include <asm/arch/svinto.h>

#include <linux/usb.h>
/* Ugly include because we don't live with the other host drivers. */
#include <../drivers/usb/core/hcd.h>
#include <../drivers/usb/core/usb.h>

#include "hc_crisv10.h"

#define ETRAX_USB_HC_IRQ USB_HC_IRQ_NBR
#define ETRAX_USB_RX_IRQ USB_DMA_RX_IRQ_NBR
#define ETRAX_USB_TX_IRQ USB_DMA_TX_IRQ_NBR

static const char *usb_hcd_version = "$Revision: 1.2 $";

#undef KERN_DEBUG
#define KERN_DEBUG ""


#undef USB_DEBUG_RH
#undef USB_DEBUG_EPID
#undef USB_DEBUG_SB
#undef USB_DEBUG_DESC
#undef USB_DEBUG_URB
#undef USB_DEBUG_TRACE
#undef USB_DEBUG_BULK
#undef USB_DEBUG_CTRL
#undef USB_DEBUG_INTR
#undef USB_DEBUG_ISOC

#ifdef USB_DEBUG_RH
#define dbg_rh(format, arg...) printk(KERN_DEBUG __FILE__ ": (RH) " format "\n" , ## arg)
#else
#define dbg_rh(format, arg...) do {} while (0)
#endif

#ifdef USB_DEBUG_EPID
#define dbg_epid(format, arg...) printk(KERN_DEBUG __FILE__ ": (EPID) " format "\n" , ## arg)
#else
#define dbg_epid(format, arg...) do {} while (0)
#endif

#ifdef USB_DEBUG_SB
#define dbg_sb(format, arg...) printk(KERN_DEBUG __FILE__ ": (SB) " format "\n" , ## arg)
#else
#define dbg_sb(format, arg...) do {} while (0)
#endif

#ifdef USB_DEBUG_CTRL
#define dbg_ctrl(format, arg...) printk(KERN_DEBUG __FILE__ ": (CTRL) " format "\n" , ## arg)
#else
#define dbg_ctrl(format, arg...) do {} while (0)
#endif

#ifdef USB_DEBUG_BULK
#define dbg_bulk(format, arg...) printk(KERN_DEBUG __FILE__ ": (BULK) " format "\n" , ## arg)
#else
#define dbg_bulk(format, arg...) do {} while (0)
#endif

#ifdef USB_DEBUG_INTR
#define dbg_intr(format, arg...) printk(KERN_DEBUG __FILE__ ": (INTR) " format "\n" , ## arg)
#else
#define dbg_intr(format, arg...) do {} while (0)
#endif

#ifdef USB_DEBUG_ISOC
#define dbg_isoc(format, arg...) printk(KERN_DEBUG __FILE__ ": (ISOC) " format "\n" , ## arg)
#else
#define dbg_isoc(format, arg...) do {} while (0)
#endif

#ifdef USB_DEBUG_TRACE
#define DBFENTER (printk(": Entering: %s\n", __FUNCTION__))
#define DBFEXIT  (printk(": Exiting:  %s\n", __FUNCTION__))
#else
#define DBFENTER do {} while (0)
#define DBFEXIT  do {} while (0)
#endif

#define usb_pipeslow(pipe)	(((pipe) >> 26) & 1)

/*-------------------------------------------------------------------
 Virtual Root Hub
 -------------------------------------------------------------------*/

static __u8 root_hub_dev_des[] =
{
	0x12,  /*  __u8  bLength; */
	0x01,  /*  __u8  bDescriptorType; Device */
	0x00,  /*  __le16 bcdUSB; v1.0 */
	0x01,
	0x09,  /*  __u8  bDeviceClass; HUB_CLASSCODE */
	0x00,  /*  __u8  bDeviceSubClass; */
	0x00,  /*  __u8  bDeviceProtocol; */
	0x08,  /*  __u8  bMaxPacketSize0; 8 Bytes */
	0x00,  /*  __le16 idVendor; */
	0x00,
	0x00,  /*  __le16 idProduct; */
	0x00,
	0x00,  /*  __le16 bcdDevice; */
	0x00,
	0x00,  /*  __u8  iManufacturer; */
	0x02,  /*  __u8  iProduct; */
	0x01,  /*  __u8  iSerialNumber; */
	0x01   /*  __u8  bNumConfigurations; */
};

/* Configuration descriptor */
static __u8 root_hub_config_des[] =
{
	0x09,  /*  __u8  bLength; */
	0x02,  /*  __u8  bDescriptorType; Configuration */
	0x19,  /*  __le16 wTotalLength; */
	0x00,
	0x01,  /*  __u8  bNumInterfaces; */
	0x01,  /*  __u8  bConfigurationValue; */
	0x00,  /*  __u8  iConfiguration; */
	0x40,  /*  __u8  bmAttributes; Bit 7: Bus-powered */
	0x00,  /*  __u8  MaxPower; */

     /* interface */
	0x09,  /*  __u8  if_bLength; */
	0x04,  /*  __u8  if_bDescriptorType; Interface */
	0x00,  /*  __u8  if_bInterfaceNumber; */
	0x00,  /*  __u8  if_bAlternateSetting; */
	0x01,  /*  __u8  if_bNumEndpoints; */
	0x09,  /*  __u8  if_bInterfaceClass; HUB_CLASSCODE */
	0x00,  /*  __u8  if_bInterfaceSubClass; */
	0x00,  /*  __u8  if_bInterfaceProtocol; */
	0x00,  /*  __u8  if_iInterface; */

     /* endpoint */
	0x07,  /*  __u8  ep_bLength; */
	0x05,  /*  __u8  ep_bDescriptorType; Endpoint */
	0x81,  /*  __u8  ep_bEndpointAddress; IN Endpoint 1 */
	0x03,  /*  __u8  ep_bmAttributes; Interrupt */
	0x08,  /*  __le16 ep_wMaxPacketSize; 8 Bytes */
	0x00,
	0xff   /*  __u8  ep_bInterval; 255 ms */
};

static __u8 root_hub_hub_des[] =
{
	0x09,  /*  __u8  bLength; */
	0x29,  /*  __u8  bDescriptorType; Hub-descriptor */
	0x02,  /*  __u8  bNbrPorts; */
	0x00,  /* __u16  wHubCharacteristics; */
	0x00,
	0x01,  /*  __u8  bPwrOn2pwrGood; 2ms */
	0x00,  /*  __u8  bHubContrCurrent; 0 mA */
	0x00,  /*  __u8  DeviceRemovable; *** 7 Ports max *** */
	0xff   /*  __u8  PortPwrCtrlMask; *** 7 ports max *** */
};

static DEFINE_TIMER(bulk_start_timer, NULL, 0, 0);
static DEFINE_TIMER(bulk_eot_timer, NULL, 0, 0);

/* We want the start timer to expire before the eot timer, because the former might start
   traffic, thus making it unnecessary for the latter to time out. */
#define BULK_START_TIMER_INTERVAL (HZ/10) /* 100 ms */
#define BULK_EOT_TIMER_INTERVAL (HZ/10+2) /* 120 ms */

#define OK(x) len = (x); dbg_rh("OK(%d): line: %d", x, __LINE__); break
#define CHECK_ALIGN(x) if (((__u32)(x)) & 0x00000003) \
{panic("Alignment check (DWORD) failed at %s:%s:%d\n", __FILE__, __FUNCTION__, __LINE__);}

#define SLAB_FLAG     (in_interrupt() ? SLAB_ATOMIC : SLAB_KERNEL)
#define KMALLOC_FLAG  (in_interrupt() ? GFP_ATOMIC : GFP_KERNEL)

/* Most helpful debugging aid */
#define assert(expr) ((void) ((expr) ? 0 : (err("assert failed at line %d",__LINE__))))

/* Alternative assert define which stops after a failed assert. */
/*
#define assert(expr)                                      \
{                                                         \
        if (!(expr)) {                                    \
                err("assert failed at line %d",__LINE__); \
                while (1);                                \
        }                                                 \
}
*/


/* FIXME: Should RX_BUF_SIZE be a config option, or maybe we should adjust it dynamically?
   To adjust it dynamically we would have to get an interrupt when we reach the end
   of the rx descriptor list, or when we get close to the end, and then allocate more
   descriptors. */

#define NBR_OF_RX_DESC     512
#define RX_DESC_BUF_SIZE   1024
#define RX_BUF_SIZE        (NBR_OF_RX_DESC * RX_DESC_BUF_SIZE)

/* The number of epids is, among other things, used for pre-allocating
   ctrl, bulk and isoc EP descriptors (one for each epid).
   Assumed to be > 1 when initiating the DMA lists. */
#define NBR_OF_EPIDS       32

/* Support interrupt traffic intervals up to 128 ms. */
#define MAX_INTR_INTERVAL 128

/* If periodic traffic (intr or isoc) is to be used, then one entry in the EP table
   must be "invalid". By this we mean that we shouldn't care about epid attentions
   for this epid, or at least handle them differently from epid attentions for "valid"
   epids. This define determines which one to use (don't change it). */
#define INVALID_EPID     31
/* A special epid for the bulk dummys. */
#define DUMMY_EPID       30

/* This is just a software cache for the valid entries in R_USB_EPT_DATA. */
static __u32 epid_usage_bitmask;

/* A bitfield to keep information on in/out traffic is needed to uniquely identify
   an endpoint on a device, since the most significant bit which indicates traffic
   direction is lacking in the ep_id field (ETRAX epids can handle both in and
   out traffic on endpoints that are otherwise identical). The USB framework, however,
   relies on them to be handled separately.  For example, bulk IN and OUT urbs cannot
   be queued in the same list, since they would block each other. */
static __u32 epid_out_traffic;

/* DMA IN cache bug. Align the DMA IN buffers to 32 bytes, i.e. a cache line.
   Since RX_DESC_BUF_SIZE is 1024 is a multiple of 32, all rx buffers will be cache aligned. */
static volatile unsigned char RxBuf[RX_BUF_SIZE] __attribute__ ((aligned (32)));
static volatile USB_IN_Desc_t RxDescList[NBR_OF_RX_DESC] __attribute__ ((aligned (4)));

/* Pointers into RxDescList. */
static volatile USB_IN_Desc_t *myNextRxDesc;
static volatile USB_IN_Desc_t *myLastRxDesc;
static volatile USB_IN_Desc_t *myPrevRxDesc;

/* EP descriptors must be 32-bit aligned. */
static volatile USB_EP_Desc_t TxCtrlEPList[NBR_OF_EPIDS] __attribute__ ((aligned (4)));
static volatile USB_EP_Desc_t TxBulkEPList[NBR_OF_EPIDS] __attribute__ ((aligned (4)));
/* After each enabled bulk EP (IN or OUT) we put two disabled EP descriptors with the eol flag set,
   causing the DMA to stop the DMA channel. The first of these two has the intr flag set, which
   gives us a dma8_sub0_descr interrupt. When we receive this, we advance the DMA one step in the
   EP list and then restart the bulk channel, thus forcing a switch between bulk EP descriptors
   in each frame. */
static volatile USB_EP_Desc_t TxBulkDummyEPList[NBR_OF_EPIDS][2] __attribute__ ((aligned (4)));

static volatile USB_EP_Desc_t TxIsocEPList[NBR_OF_EPIDS] __attribute__ ((aligned (4)));
static volatile USB_SB_Desc_t TxIsocSB_zout __attribute__ ((aligned (4)));

static volatile USB_EP_Desc_t TxIntrEPList[MAX_INTR_INTERVAL] __attribute__ ((aligned (4)));
static volatile USB_SB_Desc_t TxIntrSB_zout __attribute__ ((aligned (4)));

/* A zout transfer makes a memory access at the address of its buf pointer, which means that setting
   this buf pointer to 0 will cause an access to the flash. In addition to this, setting sw_len to 0
   results in a 16/32 bytes (depending on DMA burst size) transfer. Instead, we set it to 1, and point
   it to this buffer. */
static int zout_buffer[4] __attribute__ ((aligned (4)));

/* Cache for allocating new EP and SB descriptors. */
static kmem_cache_t *usb_desc_cache;

/* Cache for the registers allocated in the top half. */
static kmem_cache_t *top_half_reg_cache;

/* Cache for the data allocated in the isoc descr top half. */
static kmem_cache_t *isoc_compl_cache;

static struct usb_bus *etrax_usb_bus;

/* This is a circular (double-linked) list of the active urbs for each epid.
   The head is never removed, and new urbs are linked onto the list as
   urb_entry_t elements. Don't reference urb_list directly; use the wrapper
   functions instead. Note that working with these lists might require spinlock
   protection. */
static struct list_head urb_list[NBR_OF_EPIDS];

/* Read about the need and usage of this lock in submit_ctrl_urb. */
static spinlock_t urb_list_lock;

/* Used when unlinking asynchronously. */
static struct list_head urb_unlink_list;

/* for returning string descriptors in UTF-16LE */
static int ascii2utf (char *ascii, __u8 *utf, int utfmax)
{
	int retval;

	for (retval = 0; *ascii && utfmax > 1; utfmax -= 2, retval += 2) {
		*utf++ = *ascii++ & 0x7f;
		*utf++ = 0;
	}
	return retval;
}

static int usb_root_hub_string (int id, int serial, char *type, __u8 *data, int len)
{
	char buf [30];

	// assert (len > (2 * (sizeof (buf) + 1)));
	// assert (strlen (type) <= 8);

	// language ids
	if (id == 0) {
		*data++ = 4; *data++ = 3;	/* 4 bytes data */
		*data++ = 0; *data++ = 0;	/* some language id */
		return 4;

	// serial number
	} else if (id == 1) {
		sprintf (buf, "%x", serial);

	// product description
	} else if (id == 2) {
		sprintf (buf, "USB %s Root Hub", type);

	// id 3 == vendor description

	// unsupported IDs --> "stall"
	} else
	    return 0;

	data [0] = 2 + ascii2utf (buf, data + 2, len - 2);
	data [1] = 3;
	return data [0];
}

/* Wrappers around the list functions (include/linux/list.h). */

static inline int urb_list_empty(int epid)
{
	return list_empty(&urb_list[epid]);
}

/* Returns first urb for this epid, or NULL if list is empty. */
static inline struct urb *urb_list_first(int epid)
{
	struct urb *first_urb = 0;

	if (!urb_list_empty(epid)) {
		/* Get the first urb (i.e. head->next). */
		urb_entry_t *urb_entry = list_entry((&urb_list[epid])->next, urb_entry_t, list);
		first_urb = urb_entry->urb;
	}
	return first_urb;
}

/* Adds an urb_entry last in the list for this epid. */
static inline void urb_list_add(struct urb *urb, int epid)
{
	urb_entry_t *urb_entry = (urb_entry_t *)kmalloc(sizeof(urb_entry_t), KMALLOC_FLAG);
	assert(urb_entry);

	urb_entry->urb = urb;
	list_add_tail(&urb_entry->list, &urb_list[epid]);
}

/* Search through the list for an element that contains this urb. (The list
   is expected to be short and the one we are about to delete will often be
   the first in the list.) */
static inline urb_entry_t *__urb_list_entry(struct urb *urb, int epid)
{
	struct list_head *entry;
	struct list_head *tmp;
	urb_entry_t *urb_entry;

	list_for_each_safe(entry, tmp, &urb_list[epid]) {
		urb_entry = list_entry(entry, urb_entry_t, list);
		assert(urb_entry);
		assert(urb_entry->urb);

		if (urb_entry->urb == urb) {
			return urb_entry;
		}
	}
	return 0;
}

/* Delete an urb from the list. */
static inline void urb_list_del(struct urb *urb, int epid)
{
	urb_entry_t *urb_entry = __urb_list_entry(urb, epid);
	assert(urb_entry);

	/* Delete entry and free. */
	list_del(&urb_entry->list);
	kfree(urb_entry);
}

/* Move an urb to the end of the list. */
static inline void urb_list_move_last(struct urb *urb, int epid)
{
	urb_entry_t *urb_entry = __urb_list_entry(urb, epid);
	assert(urb_entry);

	list_move_tail(&urb_entry->list, &urb_list[epid]);
}

/* Get the next urb in the list. */
static inline struct urb *urb_list_next(struct urb *urb, int epid)
{
	urb_entry_t *urb_entry = __urb_list_entry(urb, epid);

	assert(urb_entry);

	if (urb_entry->list.next != &urb_list[epid]) {
		struct list_head *elem = urb_entry->list.next;
		urb_entry = list_entry(elem, urb_entry_t, list);
		return urb_entry->urb;
	} else {
		return NULL;
	}
}



/* For debug purposes only. */
static inline void urb_list_dump(int epid)
{
	struct list_head *entry;
	struct list_head *tmp;
	urb_entry_t *urb_entry;
	int i = 0;

	info("Dumping urb list for epid %d", epid);

	list_for_each_safe(entry, tmp, &urb_list[epid]) {
		urb_entry = list_entry(entry, urb_entry_t, list);
		info("   entry %d, urb = 0x%lx", i, (unsigned long)urb_entry->urb);
	}
}

static void init_rx_buffers(void);
static int etrax_rh_unlink_urb(struct urb *urb);
static void etrax_rh_send_irq(struct urb *urb);
static void etrax_rh_init_int_timer(struct urb *urb);
static void etrax_rh_int_timer_do(unsigned long ptr);

static int etrax_usb_setup_epid(struct urb *urb);
static int etrax_usb_lookup_epid(struct urb *urb);
static int etrax_usb_allocate_epid(void);
static void etrax_usb_free_epid(int epid);

static int etrax_remove_from_sb_list(struct urb *urb);

static void* etrax_usb_buffer_alloc(struct usb_bus* bus, size_t size,
	unsigned mem_flags, dma_addr_t *dma);
static void etrax_usb_buffer_free(struct usb_bus *bus, size_t size, void *addr, dma_addr_t dma);

static void etrax_usb_add_to_bulk_sb_list(struct urb *urb, int epid);
static void etrax_usb_add_to_ctrl_sb_list(struct urb *urb, int epid);
static void etrax_usb_add_to_intr_sb_list(struct urb *urb, int epid);
static void etrax_usb_add_to_isoc_sb_list(struct urb *urb, int epid);

static int etrax_usb_submit_bulk_urb(struct urb *urb);
static int etrax_usb_submit_ctrl_urb(struct urb *urb);
static int etrax_usb_submit_intr_urb(struct urb *urb);
static int etrax_usb_submit_isoc_urb(struct urb *urb);

static int etrax_usb_submit_urb(struct urb *urb, unsigned mem_flags);
static int etrax_usb_unlink_urb(struct urb *urb, int status);
static int etrax_usb_get_frame_number(struct usb_device *usb_dev);

static irqreturn_t etrax_usb_tx_interrupt(int irq, void *vhc);
static irqreturn_t etrax_usb_rx_interrupt(int irq, void *vhc);
static irqreturn_t etrax_usb_hc_interrupt_top_half(int irq, void *vhc);
static void etrax_usb_hc_interrupt_bottom_half(void *data);

static void etrax_usb_isoc_descr_interrupt_bottom_half(void *data);


/* The following is a list of interrupt handlers for the host controller interrupts we use.
   They are called from etrax_usb_hc_interrupt_bottom_half. */
static void etrax_usb_hc_isoc_eof_interrupt(void);
static void etrax_usb_hc_bulk_eot_interrupt(int timer_induced);
static void etrax_usb_hc_epid_attn_interrupt(usb_interrupt_registers_t *reg);
static void etrax_usb_hc_port_status_interrupt(usb_interrupt_registers_t *reg);
static void etrax_usb_hc_ctl_status_interrupt(usb_interrupt_registers_t *reg);

static int etrax_rh_submit_urb (struct urb *urb);

/* Forward declaration needed because they are used in the rx interrupt routine. */
static void etrax_usb_complete_urb(struct urb *urb, int status);
static void etrax_usb_complete_bulk_urb(struct urb *urb, int status);
static void etrax_usb_complete_ctrl_urb(struct urb *urb, int status);
static void etrax_usb_complete_intr_urb(struct urb *urb, int status);
static void etrax_usb_complete_isoc_urb(struct urb *urb, int status);

static int etrax_usb_hc_init(void);
static void etrax_usb_hc_cleanup(void);

static struct usb_operations etrax_usb_device_operations =
{
	.get_frame_number = etrax_usb_get_frame_number,
	.submit_urb = etrax_usb_submit_urb,
	.unlink_urb = etrax_usb_unlink_urb,
        .buffer_alloc = etrax_usb_buffer_alloc,
        .buffer_free = etrax_usb_buffer_free
};

/* Note that these functions are always available in their "__" variants, for use in
   error situations. The "__" missing variants are controlled by the USB_DEBUG_DESC/
   USB_DEBUG_URB macros. */
static void __dump_urb(struct urb* purb)
{
	printk("\nurb                  :0x%08lx\n", (unsigned long)purb);
	printk("dev                   :0x%08lx\n", (unsigned long)purb->dev);
	printk("pipe                  :0x%08x\n", purb->pipe);
	printk("status                :%d\n", purb->status);
	printk("transfer_flags        :0x%08x\n", purb->transfer_flags);
	printk("transfer_buffer       :0x%08lx\n", (unsigned long)purb->transfer_buffer);
	printk("transfer_buffer_length:%d\n", purb->transfer_buffer_length);
	printk("actual_length         :%d\n", purb->actual_length);
	printk("setup_packet          :0x%08lx\n", (unsigned long)purb->setup_packet);
	printk("start_frame           :%d\n", purb->start_frame);
	printk("number_of_packets     :%d\n", purb->number_of_packets);
	printk("interval              :%d\n", purb->interval);
	printk("error_count           :%d\n", purb->error_count);
	printk("context               :0x%08lx\n", (unsigned long)purb->context);
	printk("complete              :0x%08lx\n\n", (unsigned long)purb->complete);
}

static void __dump_in_desc(volatile USB_IN_Desc_t *in)
{
	printk("\nUSB_IN_Desc at 0x%08lx\n", (unsigned long)in);
	printk("  sw_len  : 0x%04x (%d)\n", in->sw_len, in->sw_len);
	printk("  command : 0x%04x\n", in->command);
	printk("  next    : 0x%08lx\n", in->next);
	printk("  buf     : 0x%08lx\n", in->buf);
	printk("  hw_len  : 0x%04x (%d)\n", in->hw_len, in->hw_len);
	printk("  status  : 0x%04x\n\n", in->status);
}

static void __dump_sb_desc(volatile USB_SB_Desc_t *sb)
{
	char tt = (sb->command & 0x30) >> 4;
	char *tt_string;

	switch (tt) {
	case 0:
		tt_string = "zout";
		break;
	case 1:
		tt_string = "in";
		break;
	case 2:
		tt_string = "out";
		break;
	case 3:
		tt_string = "setup";
		break;
	default:
		tt_string = "unknown (weird)";
	}

	printk("\n   USB_SB_Desc at 0x%08lx\n", (unsigned long)sb);
	printk("     command : 0x%04x\n", sb->command);
	printk("        rem     : %d\n", (sb->command & 0x3f00) >> 8);
	printk("        full    : %d\n", (sb->command & 0x40) >> 6);
	printk("        tt      : %d (%s)\n", tt, tt_string);
	printk("        intr    : %d\n", (sb->command & 0x8) >> 3);
	printk("        eot     : %d\n", (sb->command & 0x2) >> 1);
	printk("        eol     : %d\n", sb->command & 0x1);
	printk("     sw_len  : 0x%04x (%d)\n", sb->sw_len, sb->sw_len);
	printk("     next    : 0x%08lx\n", sb->next);
	printk("     buf     : 0x%08lx\n\n", sb->buf);
}


static void __dump_ep_desc(volatile USB_EP_Desc_t *ep)
{
	printk("\nUSB_EP_Desc at 0x%08lx\n", (unsigned long)ep);
	printk("  command : 0x%04x\n", ep->command);
	printk("     ep_id   : %d\n", (ep->command & 0x1f00) >> 8);
	printk("     enable  : %d\n", (ep->command & 0x10) >> 4);
	printk("     intr    : %d\n", (ep->command & 0x8) >> 3);
	printk("     eof     : %d\n", (ep->command & 0x2) >> 1);
	printk("     eol     : %d\n", ep->command & 0x1);
	printk("  hw_len  : 0x%04x (%d)\n", ep->hw_len, ep->hw_len);
	printk("  next    : 0x%08lx\n", ep->next);
	printk("  sub     : 0x%08lx\n\n", ep->sub);
}

static inline void __dump_ep_list(int pipe_type)
{
	volatile USB_EP_Desc_t *ep;
	volatile USB_EP_Desc_t *first_ep;
	volatile USB_SB_Desc_t *sb;

	switch (pipe_type)
	{
	case PIPE_BULK:
		first_ep = &TxBulkEPList[0];
		break;
	case PIPE_CONTROL:
		first_ep = &TxCtrlEPList[0];
		break;
	case PIPE_INTERRUPT:
		first_ep = &TxIntrEPList[0];
		break;
	case PIPE_ISOCHRONOUS:
		first_ep = &TxIsocEPList[0];
		break;
	default:
		warn("Cannot dump unknown traffic type");
		return;
	}
	ep = first_ep;

	printk("\n\nDumping EP list...\n\n");

	do {
		__dump_ep_desc(ep);
		/* Cannot phys_to_virt on 0 as it turns into 80000000, which is != 0. */
		sb = ep->sub ? phys_to_virt(ep->sub) : 0;
		while (sb) {
			__dump_sb_desc(sb);
			sb = sb->next ? phys_to_virt(sb->next) : 0;
		}
		ep = (volatile USB_EP_Desc_t *)(phys_to_virt(ep->next));

	} while (ep != first_ep);
}

static inline void __dump_ept_data(int epid)
{
	unsigned long flags;
	__u32 r_usb_ept_data;

	if (epid < 0 || epid > 31) {
		printk("Cannot dump ept data for invalid epid %d\n", epid);
		return;
	}

	save_flags(flags);
	cli();
	*R_USB_EPT_INDEX = IO_FIELD(R_USB_EPT_INDEX, value, epid);
	nop();
	r_usb_ept_data = *R_USB_EPT_DATA;
	restore_flags(flags);

	printk("\nR_USB_EPT_DATA = 0x%x for epid %d :\n", r_usb_ept_data, epid);
	if (r_usb_ept_data == 0) {
		/* No need for more detailed printing. */
		return;
	}
	printk("  valid           : %d\n", (r_usb_ept_data & 0x80000000) >> 31);
	printk("  hold            : %d\n", (r_usb_ept_data & 0x40000000) >> 30);
	printk("  error_count_in  : %d\n", (r_usb_ept_data & 0x30000000) >> 28);
	printk("  t_in            : %d\n", (r_usb_ept_data & 0x08000000) >> 27);
	printk("  low_speed       : %d\n", (r_usb_ept_data & 0x04000000) >> 26);
	printk("  port            : %d\n", (r_usb_ept_data & 0x03000000) >> 24);
	printk("  error_code      : %d\n", (r_usb_ept_data & 0x00c00000) >> 22);
	printk("  t_out           : %d\n", (r_usb_ept_data & 0x00200000) >> 21);
	printk("  error_count_out : %d\n", (r_usb_ept_data & 0x00180000) >> 19);
	printk("  max_len         : %d\n", (r_usb_ept_data & 0x0003f800) >> 11);
	printk("  ep              : %d\n", (r_usb_ept_data & 0x00000780) >> 7);
	printk("  dev             : %d\n", (r_usb_ept_data & 0x0000003f));
}

static inline void __dump_ept_data_list(void)
{
	int i;

	printk("Dumping the whole R_USB_EPT_DATA list\n");

	for (i = 0; i < 32; i++) {
		__dump_ept_data(i);
	}
}
#ifdef USB_DEBUG_DESC
#define dump_in_desc(...) __dump_in_desc(...)
#define dump_sb_desc(...) __dump_sb_desc(...)
#define dump_ep_desc(...) __dump_ep_desc(...)
#else
#define dump_in_desc(...) do {} while (0)
#define dump_sb_desc(...) do {} while (0)
#define dump_ep_desc(...) do {} while (0)
#endif

#ifdef USB_DEBUG_URB
#define dump_urb(x)     __dump_urb(x)
#else
#define dump_urb(x)     do {} while (0)
#endif

static void init_rx_buffers(void)
{
	int i;

	DBFENTER;

	for (i = 0; i < (NBR_OF_RX_DESC - 1); i++) {
		RxDescList[i].sw_len = RX_DESC_BUF_SIZE;
		RxDescList[i].command = 0;
		RxDescList[i].next = virt_to_phys(&RxDescList[i + 1]);
		RxDescList[i].buf = virt_to_phys(RxBuf + (i * RX_DESC_BUF_SIZE));
		RxDescList[i].hw_len = 0;
		RxDescList[i].status = 0;

		/* DMA IN cache bug. (struct etrax_dma_descr has the same layout as USB_IN_Desc
		   for the relevant fields.) */
		prepare_rx_descriptor((struct etrax_dma_descr*)&RxDescList[i]);

	}

	RxDescList[i].sw_len = RX_DESC_BUF_SIZE;
	RxDescList[i].command = IO_STATE(USB_IN_command, eol, yes);
	RxDescList[i].next = virt_to_phys(&RxDescList[0]);
	RxDescList[i].buf = virt_to_phys(RxBuf + (i * RX_DESC_BUF_SIZE));
	RxDescList[i].hw_len = 0;
	RxDescList[i].status = 0;

	myNextRxDesc = &RxDescList[0];
	myLastRxDesc = &RxDescList[NBR_OF_RX_DESC - 1];
	myPrevRxDesc = &RxDescList[NBR_OF_RX_DESC - 1];

	*R_DMA_CH9_FIRST = virt_to_phys(myNextRxDesc);
	*R_DMA_CH9_CMD = IO_STATE(R_DMA_CH9_CMD, cmd, start);

	DBFEXIT;
}

static void init_tx_bulk_ep(void)
{
	int i;

	DBFENTER;

	for (i = 0; i < (NBR_OF_EPIDS - 1); i++) {
		CHECK_ALIGN(&TxBulkEPList[i]);
		TxBulkEPList[i].hw_len = 0;
		TxBulkEPList[i].command = IO_FIELD(USB_EP_command, epid, i);
		TxBulkEPList[i].sub = 0;
		TxBulkEPList[i].next = virt_to_phys(&TxBulkEPList[i + 1]);

		/* Initiate two EPs, disabled and with the eol flag set. No need for any
		   preserved epid. */

		/* The first one has the intr flag set so we get an interrupt when the DMA
		   channel is about to become disabled. */
		CHECK_ALIGN(&TxBulkDummyEPList[i][0]);
		TxBulkDummyEPList[i][0].hw_len = 0;
		TxBulkDummyEPList[i][0].command = (IO_FIELD(USB_EP_command, epid, DUMMY_EPID) |
						   IO_STATE(USB_EP_command, eol, yes) |
						   IO_STATE(USB_EP_command, intr, yes));
		TxBulkDummyEPList[i][0].sub = 0;
		TxBulkDummyEPList[i][0].next = virt_to_phys(&TxBulkDummyEPList[i][1]);

		/* The second one. */
		CHECK_ALIGN(&TxBulkDummyEPList[i][1]);
		TxBulkDummyEPList[i][1].hw_len = 0;
		TxBulkDummyEPList[i][1].command = (IO_FIELD(USB_EP_command, epid, DUMMY_EPID) |
						   IO_STATE(USB_EP_command, eol, yes));
		TxBulkDummyEPList[i][1].sub = 0;
		/* The last dummy's next pointer is the same as the current EP's next pointer. */
		TxBulkDummyEPList[i][1].next = virt_to_phys(&TxBulkEPList[i + 1]);
	}

	/* Configure the last one. */
	CHECK_ALIGN(&TxBulkEPList[i]);
	TxBulkEPList[i].hw_len = 0;
	TxBulkEPList[i].command = (IO_STATE(USB_EP_command, eol, yes) |
				   IO_FIELD(USB_EP_command, epid, i));
	TxBulkEPList[i].sub = 0;
	TxBulkEPList[i].next = virt_to_phys(&TxBulkEPList[0]);

	/* No need configuring dummy EPs for the last one as it will never be used for
	   bulk traffic (i == INVALD_EPID at this point). */

	/* Set up to start on the last EP so we will enable it when inserting traffic
	   for the first time (imitating the situation where the DMA has stopped
	   because there was no more traffic). */
	*R_DMA_CH8_SUB0_EP = virt_to_phys(&TxBulkEPList[i]);
	/* No point in starting the bulk channel yet.
	 *R_DMA_CH8_SUB0_CMD = IO_STATE(R_DMA_CH8_SUB0_CMD, cmd, start); */
	DBFEXIT;
}

static void init_tx_ctrl_ep(void)
{
	int i;

	DBFENTER;

	for (i = 0; i < (NBR_OF_EPIDS - 1); i++) {
		CHECK_ALIGN(&TxCtrlEPList[i]);
		TxCtrlEPList[i].hw_len = 0;
		TxCtrlEPList[i].command = IO_FIELD(USB_EP_command, epid, i);
		TxCtrlEPList[i].sub = 0;
		TxCtrlEPList[i].next = virt_to_phys(&TxCtrlEPList[i + 1]);
	}

	CHECK_ALIGN(&TxCtrlEPList[i]);
	TxCtrlEPList[i].hw_len = 0;
	TxCtrlEPList[i].command = (IO_STATE(USB_EP_command, eol, yes) |
				   IO_FIELD(USB_EP_command, epid, i));

	TxCtrlEPList[i].sub = 0;
	TxCtrlEPList[i].next = virt_to_phys(&TxCtrlEPList[0]);

	*R_DMA_CH8_SUB1_EP = virt_to_phys(&TxCtrlEPList[0]);
	*R_DMA_CH8_SUB1_CMD = IO_STATE(R_DMA_CH8_SUB1_CMD, cmd, start);

	DBFEXIT;
}


static void init_tx_intr_ep(void)
{
	int i;

	DBFENTER;

	/* Read comment at zout_buffer declaration for an explanation to this. */
	TxIntrSB_zout.sw_len = 1;
	TxIntrSB_zout.next = 0;
	TxIntrSB_zout.buf = virt_to_phys(&zout_buffer[0]);
	TxIntrSB_zout.command = (IO_FIELD(USB_SB_command, rem, 0) |
				 IO_STATE(USB_SB_command, tt, zout) |
				 IO_STATE(USB_SB_command, full, yes) |
				 IO_STATE(USB_SB_command, eot, yes) |
				 IO_STATE(USB_SB_command, eol, yes));

	for (i = 0; i < (MAX_INTR_INTERVAL - 1); i++) {
		CHECK_ALIGN(&TxIntrEPList[i]);
		TxIntrEPList[i].hw_len = 0;
		TxIntrEPList[i].command =
			(IO_STATE(USB_EP_command, eof, yes) |
			 IO_STATE(USB_EP_command, enable, yes) |
			 IO_FIELD(USB_EP_command, epid, INVALID_EPID));
		TxIntrEPList[i].sub = virt_to_phys(&TxIntrSB_zout);
		TxIntrEPList[i].next = virt_to_phys(&TxIntrEPList[i + 1]);
	}

	CHECK_ALIGN(&TxIntrEPList[i]);
	TxIntrEPList[i].hw_len = 0;
	TxIntrEPList[i].command =
		(IO_STATE(USB_EP_command, eof, yes) |
		 IO_STATE(USB_EP_command, eol, yes) |
		 IO_STATE(USB_EP_command, enable, yes) |
		 IO_FIELD(USB_EP_command, epid, INVALID_EPID));
	TxIntrEPList[i].sub = virt_to_phys(&TxIntrSB_zout);
	TxIntrEPList[i].next = virt_to_phys(&TxIntrEPList[0]);

	*R_DMA_CH8_SUB2_EP = virt_to_phys(&TxIntrEPList[0]);
	*R_DMA_CH8_SUB2_CMD = IO_STATE(R_DMA_CH8_SUB2_CMD, cmd, start);
	DBFEXIT;
}

static void init_tx_isoc_ep(void)
{
	int i;

	DBFENTER;

	/* Read comment at zout_buffer declaration for an explanation to this. */
	TxIsocSB_zout.sw_len = 1;
	TxIsocSB_zout.next = 0;
	TxIsocSB_zout.buf = virt_to_phys(&zout_buffer[0]);
	TxIsocSB_zout.command = (IO_FIELD(USB_SB_command, rem, 0) |
				 IO_STATE(USB_SB_command, tt, zout) |
				 IO_STATE(USB_SB_command, full, yes) |
				 IO_STATE(USB_SB_command, eot, yes) |
				 IO_STATE(USB_SB_command, eol, yes));

	/* The last isochronous EP descriptor is a dummy. */

	for (i = 0; i < (NBR_OF_EPIDS - 1); i++) {
		CHECK_ALIGN(&TxIsocEPList[i]);
		TxIsocEPList[i].hw_len = 0;
		TxIsocEPList[i].command = IO_FIELD(USB_EP_command, epid, i);
		TxIsocEPList[i].sub = 0;
		TxIsocEPList[i].next = virt_to_phys(&TxIsocEPList[i + 1]);
	}

	CHECK_ALIGN(&TxIsocEPList[i]);
	TxIsocEPList[i].hw_len = 0;

	/* Must enable the last EP descr to get eof interrupt. */
	TxIsocEPList[i].command = (IO_STATE(USB_EP_command, enable, yes) |
				   IO_STATE(USB_EP_command, eof, yes) |
				   IO_STATE(USB_EP_command, eol, yes) |
				   IO_FIELD(USB_EP_command, epid, INVALID_EPID));
	TxIsocEPList[i].sub = virt_to_phys(&TxIsocSB_zout);
	TxIsocEPList[i].next = virt_to_phys(&TxIsocEPList[0]);

	*R_DMA_CH8_SUB3_EP = virt_to_phys(&TxIsocEPList[0]);
	*R_DMA_CH8_SUB3_CMD = IO_STATE(R_DMA_CH8_SUB3_CMD, cmd, start);

	DBFEXIT;
}

static void etrax_usb_unlink_intr_urb(struct urb *urb)
{
	volatile USB_EP_Desc_t *first_ep;  /* First EP in the list. */
	volatile USB_EP_Desc_t *curr_ep;   /* Current EP, the iterator. */
	volatile USB_EP_Desc_t *next_ep;   /* The EP after current. */
	volatile USB_EP_Desc_t *unlink_ep; /* The one we should remove from the list. */

	int epid;

	/* Read 8.8.4 in Designer's Reference, "Removing an EP Descriptor from the List". */

	DBFENTER;

	epid = ((etrax_urb_priv_t *)urb->hcpriv)->epid;

	first_ep = &TxIntrEPList[0];
	curr_ep = first_ep;


	/* Note that this loop removes all EP descriptors with this epid. This assumes
	   that all EP descriptors belong to the one and only urb for this epid. */

	do {
		next_ep = (USB_EP_Desc_t *)phys_to_virt(curr_ep->next);

		if (IO_EXTRACT(USB_EP_command, epid, next_ep->command) == epid) {

			dbg_intr("Found EP to unlink for epid %d", epid);

			/* This is the one we should unlink. */
			unlink_ep = next_ep;

			/* Actually unlink the EP from the DMA list. */
			curr_ep->next = unlink_ep->next;

			/* Wait until the DMA is no longer at this descriptor. */
			while (*R_DMA_CH8_SUB2_EP == virt_to_phys(unlink_ep));

			/* Now we are free to remove it and its SB descriptor.
			   Note that it is assumed here that there is only one sb in the
			   sb list for this ep. */
			kmem_cache_free(usb_desc_cache, phys_to_virt(unlink_ep->sub));
			kmem_cache_free(usb_desc_cache, (USB_EP_Desc_t *)unlink_ep);
		}

		curr_ep = phys_to_virt(curr_ep->next);

	} while (curr_ep != first_ep);
        urb->hcpriv = NULL;
}

void etrax_usb_do_intr_recover(int epid)
{
	USB_EP_Desc_t *first_ep, *tmp_ep;

	DBFENTER;

	first_ep = (USB_EP_Desc_t *)phys_to_virt(*R_DMA_CH8_SUB2_EP);
	tmp_ep = first_ep;

	/* What this does is simply to walk the list of interrupt
	   ep descriptors and enable those that are disabled. */

	do {
		if (IO_EXTRACT(USB_EP_command, epid, tmp_ep->command) == epid &&
		    !(tmp_ep->command & IO_MASK(USB_EP_command, enable))) {
			tmp_ep->command |= IO_STATE(USB_EP_command, enable, yes);
		}

		tmp_ep = (USB_EP_Desc_t *)phys_to_virt(tmp_ep->next);

	} while (tmp_ep != first_ep);


	DBFEXIT;
}

static int etrax_rh_unlink_urb (struct urb *urb)
{
	etrax_hc_t *hc;

	DBFENTER;

	hc = urb->dev->bus->hcpriv;

	if (hc->rh.urb == urb) {
		hc->rh.send = 0;
		del_timer(&hc->rh.rh_int_timer);
	}

	DBFEXIT;
	return 0;
}

static void etrax_rh_send_irq(struct urb *urb)
{
	__u16 data = 0;
	etrax_hc_t *hc = urb->dev->bus->hcpriv;
	DBFENTER;

/*
  dbg_rh("R_USB_FM_NUMBER   : 0x%08X", *R_USB_FM_NUMBER);
  dbg_rh("R_USB_FM_REMAINING: 0x%08X", *R_USB_FM_REMAINING);
*/

	data |= (hc->rh.wPortChange_1) ? (1 << 1) : 0;
	data |= (hc->rh.wPortChange_2) ? (1 << 2) : 0;

	*((__u16 *)urb->transfer_buffer) = cpu_to_le16(data);
	/* FIXME: Why is actual_length set to 1 when data is 2 bytes?
	   Since only 1 byte is used, why not declare data as __u8? */
	urb->actual_length = 1;
	urb->status = 0;

	if (hc->rh.send && urb->complete) {
		dbg_rh("wPortChange_1: 0x%04X", hc->rh.wPortChange_1);
		dbg_rh("wPortChange_2: 0x%04X", hc->rh.wPortChange_2);

		urb->complete(urb, NULL);
	}

	DBFEXIT;
}

static void etrax_rh_init_int_timer(struct urb *urb)
{
	etrax_hc_t *hc;

	DBFENTER;

	hc = urb->dev->bus->hcpriv;
	hc->rh.interval = urb->interval;
	init_timer(&hc->rh.rh_int_timer);
	hc->rh.rh_int_timer.function = etrax_rh_int_timer_do;
	hc->rh.rh_int_timer.data = (unsigned long)urb;
	/* FIXME: Is the jiffies resolution enough? All intervals < 10 ms will be mapped
	   to 0, and the rest to the nearest lower 10 ms. */
	hc->rh.rh_int_timer.expires = jiffies + ((HZ * hc->rh.interval) / 1000);
	add_timer(&hc->rh.rh_int_timer);

	DBFEXIT;
}

static void etrax_rh_int_timer_do(unsigned long ptr)
{
	struct urb *urb;
	etrax_hc_t *hc;

	DBFENTER;

	urb = (struct urb*)ptr;
	hc = urb->dev->bus->hcpriv;

	if (hc->rh.send) {
		etrax_rh_send_irq(urb);
	}

	DBFEXIT;
}

static int etrax_usb_setup_epid(struct urb *urb)
{
	int epid;
	char devnum, endpoint, out_traffic, slow;
	int maxlen;
	unsigned long flags;

	DBFENTER;

	epid = etrax_usb_lookup_epid(urb);
	if ((epid != -1)){
		/* An epid that fits this urb has been found. */
		DBFEXIT;
		return epid;
	}

	/* We must find and initiate a new epid for this urb. */
	epid = etrax_usb_allocate_epid();

	if (epid == -1) {
		/* Failed to allocate a new epid. */
		DBFEXIT;
		return epid;
	}

	/* We now have a new epid to use. Initiate it. */
	set_bit(epid, (void *)&epid_usage_bitmask);

	devnum = usb_pipedevice(urb->pipe);
	endpoint = usb_pipeendpoint(urb->pipe);
	slow = usb_pipeslow(urb->pipe);
	maxlen = usb_maxpacket(urb->dev, urb->pipe, usb_pipeout(urb->pipe));
	if (usb_pipetype(urb->pipe) == PIPE_CONTROL) {
		/* We want both IN and OUT control traffic to be put on the same EP/SB list. */
		out_traffic = 1;
	} else {
		out_traffic = usb_pipeout(urb->pipe);
	}

	save_flags(flags);
	cli();

	*R_USB_EPT_INDEX = IO_FIELD(R_USB_EPT_INDEX, value, epid);
	nop();

	if (usb_pipetype(urb->pipe) == PIPE_ISOCHRONOUS) {
		*R_USB_EPT_DATA_ISO = IO_STATE(R_USB_EPT_DATA_ISO, valid, yes) |
			/* FIXME: Change any to the actual port? */
			IO_STATE(R_USB_EPT_DATA_ISO, port, any) |
			IO_FIELD(R_USB_EPT_DATA_ISO, max_len, maxlen) |
			IO_FIELD(R_USB_EPT_DATA_ISO, ep, endpoint) |
			IO_FIELD(R_USB_EPT_DATA_ISO, dev, devnum);
	} else {
		*R_USB_EPT_DATA = IO_STATE(R_USB_EPT_DATA, valid, yes) |
			IO_FIELD(R_USB_EPT_DATA, low_speed, slow) |
			/* FIXME: Change any to the actual port? */
			IO_STATE(R_USB_EPT_DATA, port, any) |
			IO_FIELD(R_USB_EPT_DATA, max_len, maxlen) |
			IO_FIELD(R_USB_EPT_DATA, ep, endpoint) |
			IO_FIELD(R_USB_EPT_DATA, dev, devnum);
	}

	restore_flags(flags);

	if (out_traffic) {
		set_bit(epid, (void *)&epid_out_traffic);
	} else {
		clear_bit(epid, (void *)&epid_out_traffic);
	}

	dbg_epid("Setting up epid %d with devnum %d, endpoint %d and max_len %d (%s)",
		 epid, devnum, endpoint, maxlen, out_traffic ? "OUT" : "IN");

	DBFEXIT;
	return epid;
}

static void etrax_usb_free_epid(int epid)
{
	unsigned long flags;

	DBFENTER;

	if (!test_bit(epid, (void *)&epid_usage_bitmask)) {
		warn("Trying to free unused epid %d", epid);
		DBFEXIT;
		return;
	}

	save_flags(flags);
	cli();

	*R_USB_EPT_INDEX = IO_FIELD(R_USB_EPT_INDEX, value, epid);
	nop();
	while (*R_USB_EPT_DATA & IO_MASK(R_USB_EPT_DATA, hold));
	/* This will, among other things, set the valid field to 0. */
	*R_USB_EPT_DATA = 0;
	restore_flags(flags);

	clear_bit(epid, (void *)&epid_usage_bitmask);


	dbg_epid("Freed epid %d", epid);

	DBFEXIT;
}

static int etrax_usb_lookup_epid(struct urb *urb)
{
	int i;
	__u32 data;
	char devnum, endpoint, slow, out_traffic;
	int maxlen;
	unsigned long flags;

	DBFENTER;

	devnum = usb_pipedevice(urb->pipe);
	endpoint = usb_pipeendpoint(urb->pipe);
	slow = usb_pipeslow(urb->pipe);
	maxlen = usb_maxpacket(urb->dev, urb->pipe, usb_pipeout(urb->pipe));
	if (usb_pipetype(urb->pipe) == PIPE_CONTROL) {
		/* We want both IN and OUT control traffic to be put on the same EP/SB list. */
		out_traffic = 1;
	} else {
		out_traffic = usb_pipeout(urb->pipe);
	}

	/* Step through att epids. */
	for (i = 0; i < NBR_OF_EPIDS; i++) {
		if (test_bit(i, (void *)&epid_usage_bitmask) &&
		    test_bit(i, (void *)&epid_out_traffic) == out_traffic) {

			save_flags(flags);
			cli();
			*R_USB_EPT_INDEX = IO_FIELD(R_USB_EPT_INDEX, value, i);
			nop();

			if (usb_pipetype(urb->pipe) == PIPE_ISOCHRONOUS) {
				data = *R_USB_EPT_DATA_ISO;
				restore_flags(flags);

				if ((IO_MASK(R_USB_EPT_DATA_ISO, valid) & data) &&
				    (IO_EXTRACT(R_USB_EPT_DATA_ISO, dev, data) == devnum) &&
				    (IO_EXTRACT(R_USB_EPT_DATA_ISO, ep, data) == endpoint) &&
				    (IO_EXTRACT(R_USB_EPT_DATA_ISO, max_len, data) == maxlen)) {
					dbg_epid("Found epid %d for devnum %d, endpoint %d (%s)",
						 i, devnum, endpoint, out_traffic ? "OUT" : "IN");
					DBFEXIT;
					return i;
				}
			} else {
				data = *R_USB_EPT_DATA;
				restore_flags(flags);

				if ((IO_MASK(R_USB_EPT_DATA, valid) & data) &&
				    (IO_EXTRACT(R_USB_EPT_DATA, dev, data) == devnum) &&
				    (IO_EXTRACT(R_USB_EPT_DATA, ep, data) == endpoint) &&
				    (IO_EXTRACT(R_USB_EPT_DATA, low_speed, data) == slow) &&
				    (IO_EXTRACT(R_USB_EPT_DATA, max_len, data) == maxlen)) {
					dbg_epid("Found epid %d for devnum %d, endpoint %d (%s)",
						 i, devnum, endpoint, out_traffic ? "OUT" : "IN");
					DBFEXIT;
					return i;
				}
			}
		}
	}

	DBFEXIT;
	return -1;
}

static int etrax_usb_allocate_epid(void)
{
	int i;

	DBFENTER;

	for (i = 0; i < NBR_OF_EPIDS; i++) {
		if (!test_bit(i, (void *)&epid_usage_bitmask)) {
			dbg_epid("Found free epid %d", i);
			DBFEXIT;
			return i;
		}
	}

	dbg_epid("Found no free epids");
	DBFEXIT;
	return -1;
}

static int etrax_usb_submit_urb(struct urb *urb, unsigned mem_flags)
{
	etrax_hc_t *hc;
	int ret = -EINVAL;

	DBFENTER;

	if (!urb->dev || !urb->dev->bus) {
		return -ENODEV;
	}
	if (usb_maxpacket(urb->dev, urb->pipe, usb_pipeout(urb->pipe)) <= 0) {
		info("Submit urb to pipe with maxpacketlen 0, pipe 0x%X\n", urb->pipe);
		return -EMSGSIZE;
	}

	if (urb->timeout) {
		/* FIXME. */
		warn("urb->timeout specified, ignoring.");
	}

	hc = (etrax_hc_t*)urb->dev->bus->hcpriv;

	if (usb_pipedevice(urb->pipe) == hc->rh.devnum) {
		/* This request is for the Virtual Root Hub. */
		ret = etrax_rh_submit_urb(urb);

	} else if (usb_pipetype(urb->pipe) == PIPE_BULK) {

		ret = etrax_usb_submit_bulk_urb(urb);

	} else if (usb_pipetype(urb->pipe) == PIPE_CONTROL) {

		ret = etrax_usb_submit_ctrl_urb(urb);

	} else if (usb_pipetype(urb->pipe) == PIPE_INTERRUPT) {
		int bustime;

		if (urb->bandwidth == 0) {
			bustime = usb_check_bandwidth(urb->dev, urb);
			if (bustime < 0) {
				ret = bustime;
			} else {
				ret = etrax_usb_submit_intr_urb(urb);
				if (ret == 0)
					usb_claim_bandwidth(urb->dev, urb, bustime, 0);
			}
		} else {
			/* Bandwidth already set. */
			ret = etrax_usb_submit_intr_urb(urb);
		}

	} else if (usb_pipetype(urb->pipe) == PIPE_ISOCHRONOUS) {
		int bustime;

		if (urb->bandwidth == 0) {
			bustime = usb_check_bandwidth(urb->dev, urb);
			if (bustime < 0) {
				ret = bustime;
			} else {
				ret = etrax_usb_submit_isoc_urb(urb);
				if (ret == 0)
					usb_claim_bandwidth(urb->dev, urb, bustime, 0);
			}
		} else {
			/* Bandwidth already set. */
			ret = etrax_usb_submit_isoc_urb(urb);
		}
	}

	DBFEXIT;

        if (ret != 0)
          printk("Submit URB error %d\n", ret);

	return ret;
}

static int etrax_usb_unlink_urb(struct urb *urb, int status)
{
	etrax_hc_t *hc;
	etrax_urb_priv_t *urb_priv;
	int epid;
	unsigned int flags;

	DBFENTER;

	if (!urb) {
		return -EINVAL;
	}

	/* Disable interrupts here since a descriptor interrupt for the isoc epid
	   will modify the sb list.  This could possibly be done more granular, but
	   unlink_urb should not be used frequently anyway.
	*/

	save_flags(flags);
	cli();

	if (!urb->dev || !urb->dev->bus) {
		restore_flags(flags);
		return -ENODEV;
	}
	if (!urb->hcpriv) {
		/* This happens if a device driver calls unlink on an urb that
		   was never submitted (lazy driver) or if the urb was completed
		   while unlink was being called. */
		restore_flags(flags);
		return 0;
	}
	if (urb->transfer_flags & URB_ASYNC_UNLINK) {
		/* FIXME. */
		/* If URB_ASYNC_UNLINK is set:
		   unlink
		   move to a separate urb list
		   call complete at next sof with ECONNRESET

		   If not:
		   wait 1 ms
		   unlink
		   call complete with ENOENT
		*/
		warn("URB_ASYNC_UNLINK set, ignoring.");
	}

	/* One might think that urb->status = -EINPROGRESS would be a requirement for unlinking,
	   but that doesn't work for interrupt and isochronous traffic since they are completed
	   repeatedly, and urb->status is set then. That may in itself be a bug though. */

	hc = urb->dev->bus->hcpriv;
	urb_priv = (etrax_urb_priv_t *)urb->hcpriv;
	epid = urb_priv->epid;

	/* Set the urb status (synchronous unlink). */
	urb->status = -ENOENT;
	urb_priv->urb_state = UNLINK;

	if (usb_pipedevice(urb->pipe) == hc->rh.devnum) {
		int ret;
		ret = etrax_rh_unlink_urb(urb);
		DBFEXIT;
		restore_flags(flags);
		return ret;

	} else if (usb_pipetype(urb->pipe) == PIPE_BULK) {

		dbg_bulk("Unlink of bulk urb (0x%lx)", (unsigned long)urb);

		if (TxBulkEPList[epid].command & IO_MASK(USB_EP_command, enable)) {
			/* The EP was enabled, disable it and wait. */
			TxBulkEPList[epid].command &= ~IO_MASK(USB_EP_command, enable);

			/* Ah, the luxury of busy-wait. */
			while (*R_DMA_CH8_SUB0_EP == virt_to_phys(&TxBulkEPList[epid]));
		}
		/* Kicking dummy list out of the party. */
		TxBulkEPList[epid].next = virt_to_phys(&TxBulkEPList[(epid + 1) % NBR_OF_EPIDS]);

	} else if (usb_pipetype(urb->pipe) == PIPE_CONTROL) {

		dbg_ctrl("Unlink of ctrl urb (0x%lx)", (unsigned long)urb);

		if (TxCtrlEPList[epid].command & IO_MASK(USB_EP_command, enable)) {
			/* The EP was enabled, disable it and wait. */
			TxCtrlEPList[epid].command &= ~IO_MASK(USB_EP_command, enable);

			/* Ah, the luxury of busy-wait. */
			while (*R_DMA_CH8_SUB1_EP == virt_to_phys(&TxCtrlEPList[epid]));
		}

	} else if (usb_pipetype(urb->pipe) == PIPE_INTERRUPT) {

		dbg_intr("Unlink of intr urb (0x%lx)", (unsigned long)urb);

		/* Separate function because it's a tad more complicated. */
		etrax_usb_unlink_intr_urb(urb);

	} else if (usb_pipetype(urb->pipe) == PIPE_ISOCHRONOUS) {

		dbg_isoc("Unlink of isoc urb (0x%lx)", (unsigned long)urb);

		if (TxIsocEPList[epid].command & IO_MASK(USB_EP_command, enable)) {
			/* The EP was enabled, disable it and wait. */
			TxIsocEPList[epid].command &= ~IO_MASK(USB_EP_command, enable);

			/* Ah, the luxury of busy-wait. */
			while (*R_DMA_CH8_SUB3_EP == virt_to_phys(&TxIsocEPList[epid]));
		}
	}

	/* Note that we need to remove the urb from the urb list *before* removing its SB
	   descriptors. (This means that the isoc eof handler might get a null urb when we
	   are unlinking the last urb.) */

	if (usb_pipetype(urb->pipe) == PIPE_BULK) {

		urb_list_del(urb, epid);
		TxBulkEPList[epid].sub = 0;
		etrax_remove_from_sb_list(urb);

	} else if (usb_pipetype(urb->pipe) == PIPE_CONTROL) {

		urb_list_del(urb, epid);
		TxCtrlEPList[epid].sub = 0;
		etrax_remove_from_sb_list(urb);

	} else if (usb_pipetype(urb->pipe) == PIPE_INTERRUPT) {

		urb_list_del(urb, epid);
		/* Sanity check (should never happen). */
		assert(urb_list_empty(epid));

		/* Release allocated bandwidth. */
		usb_release_bandwidth(urb->dev, urb, 0);

	} else if (usb_pipetype(urb->pipe) == PIPE_ISOCHRONOUS) {

		if (usb_pipeout(urb->pipe)) {

			USB_SB_Desc_t *iter_sb, *prev_sb, *next_sb;

			if (__urb_list_entry(urb, epid)) {

				urb_list_del(urb, epid);
				iter_sb = TxIsocEPList[epid].sub ? phys_to_virt(TxIsocEPList[epid].sub) : 0;
				prev_sb = 0;
				while (iter_sb && (iter_sb != urb_priv->first_sb)) {
					prev_sb = iter_sb;
					iter_sb = iter_sb->next ? phys_to_virt(iter_sb->next) : 0;
				}

				if (iter_sb == 0) {
					/* Unlink of the URB currently being transmitted. */
					prev_sb = 0;
					iter_sb = TxIsocEPList[epid].sub ? phys_to_virt(TxIsocEPList[epid].sub) : 0;
				}

				while (iter_sb && (iter_sb != urb_priv->last_sb)) {
					iter_sb = iter_sb->next ? phys_to_virt(iter_sb->next) : 0;
				}
				if (iter_sb) {
					next_sb = iter_sb->next ? phys_to_virt(iter_sb->next) : 0;
				} else {
					/* This should only happen if the DMA has completed
					   processing the SB list for this EP while interrupts
					   are disabled. */
					dbg_isoc("Isoc urb not found, already sent?");
					next_sb = 0;
				}
				if (prev_sb) {
					prev_sb->next = next_sb ? virt_to_phys(next_sb) : 0;
				} else {
					TxIsocEPList[epid].sub = next_sb ? virt_to_phys(next_sb) : 0;
				}

				etrax_remove_from_sb_list(urb);
				if (urb_list_empty(epid)) {
					TxIsocEPList[epid].sub = 0;
					dbg_isoc("Last isoc out urb epid %d", epid);
				} else if (next_sb || prev_sb) {
					dbg_isoc("Re-enable isoc out epid %d", epid);

					TxIsocEPList[epid].hw_len = 0;
					TxIsocEPList[epid].command |= IO_STATE(USB_EP_command, enable, yes);
				} else {
					TxIsocEPList[epid].sub = 0;
					dbg_isoc("URB list non-empty and no SB list, EP disabled");
				}
			} else {
				dbg_isoc("Urb 0x%p not found, completed already?", urb);
			}
		} else {

			urb_list_del(urb, epid);

			/* For in traffic there is only one SB descriptor for each EP even
			   though there may be several urbs (all urbs point at the same SB). */
			if (urb_list_empty(epid)) {
				/* No more urbs, remove the SB. */
				TxIsocEPList[epid].sub = 0;
				etrax_remove_from_sb_list(urb);
			} else {
				TxIsocEPList[epid].hw_len = 0;
				TxIsocEPList[epid].command |= IO_STATE(USB_EP_command, enable, yes);
			}
		}
		/* Release allocated bandwidth. */
		usb_release_bandwidth(urb->dev, urb, 1);
	}
	/* Free the epid if urb list is empty. */
	if (urb_list_empty(epid)) {
		etrax_usb_free_epid(epid);
	}
	restore_flags(flags);

	/* Must be done before calling completion handler. */
	kfree(urb_priv);
	urb->hcpriv = 0;

	if (urb->complete) {
		urb->complete(urb, NULL);
	}

	DBFEXIT;
	return 0;
}

static int etrax_usb_get_frame_number(struct usb_device *usb_dev)
{
	DBFENTER;
	DBFEXIT;
	return (*R_USB_FM_NUMBER & 0x7ff);
}

static irqreturn_t etrax_usb_tx_interrupt(int irq, void *vhc)
{
	DBFENTER;

	/* This interrupt handler could be used when unlinking EP descriptors. */

	if (*R_IRQ_READ2 & IO_MASK(R_IRQ_READ2, dma8_sub0_descr)) {
		USB_EP_Desc_t *ep;

		//dbg_bulk("dma8_sub0_descr (BULK) intr.");

		/* It should be safe clearing the interrupt here, since we don't expect to get a new
		   one until we restart the bulk channel. */
		*R_DMA_CH8_SUB0_CLR_INTR = IO_STATE(R_DMA_CH8_SUB0_CLR_INTR, clr_descr, do);

		/* Wait while the DMA is running (though we don't expect it to be). */
		while (*R_DMA_CH8_SUB0_CMD & IO_MASK(R_DMA_CH8_SUB0_CMD, cmd));

		/* Advance the DMA to the next EP descriptor. */
		ep = (USB_EP_Desc_t *)phys_to_virt(*R_DMA_CH8_SUB0_EP);

		//dbg_bulk("descr intr: DMA is at 0x%lx", (unsigned long)ep);

		/* ep->next is already a physical address; no need for a virt_to_phys. */
		*R_DMA_CH8_SUB0_EP = ep->next;

		/* Start the DMA bulk channel again. */
		*R_DMA_CH8_SUB0_CMD = IO_STATE(R_DMA_CH8_SUB0_CMD, cmd, start);
	}
	if (*R_IRQ_READ2 & IO_MASK(R_IRQ_READ2, dma8_sub1_descr)) {
		struct urb *urb;
		int epid;
		etrax_urb_priv_t *urb_priv;
		unsigned long int flags;

		dbg_ctrl("dma8_sub1_descr (CTRL) intr.");
		*R_DMA_CH8_SUB1_CLR_INTR = IO_STATE(R_DMA_CH8_SUB1_CLR_INTR, clr_descr, do);

		/* The complete callback gets called so we cli. */
		save_flags(flags);
		cli();

		for (epid = 0; epid < NBR_OF_EPIDS - 1; epid++) {
			if ((TxCtrlEPList[epid].sub == 0) ||
			    (epid == DUMMY_EPID) ||
			    (epid == INVALID_EPID)) {
				/* Nothing here to see. */
				continue;
			}

			/* Get the first urb (if any). */
			urb = urb_list_first(epid);

			if (urb) {

				/* Sanity check. */
				assert(usb_pipetype(urb->pipe) == PIPE_CONTROL);

				urb_priv = (etrax_urb_priv_t *)urb->hcpriv;
				assert(urb_priv);

				if (urb_priv->urb_state == WAITING_FOR_DESCR_INTR) {
					assert(!(TxCtrlEPList[urb_priv->epid].command & IO_MASK(USB_EP_command, enable)));

					etrax_usb_complete_urb(urb, 0);
				}
			}
		}
		restore_flags(flags);
	}
	if (*R_IRQ_READ2 & IO_MASK(R_IRQ_READ2, dma8_sub2_descr)) {
		dbg_intr("dma8_sub2_descr (INTR) intr.");
		*R_DMA_CH8_SUB2_CLR_INTR = IO_STATE(R_DMA_CH8_SUB2_CLR_INTR, clr_descr, do);
	}
	if (*R_IRQ_READ2 & IO_MASK(R_IRQ_READ2, dma8_sub3_descr)) {
		struct urb *urb;
		int epid;
		int epid_done;
		etrax_urb_priv_t *urb_priv;
		USB_SB_Desc_t *sb_desc;

		usb_isoc_complete_data_t *comp_data = NULL;

		/* One or more isoc out transfers are done. */
		dbg_isoc("dma8_sub3_descr (ISOC) intr.");

		/* For each isoc out EP search for the first sb_desc with the intr flag
		   set.  This descriptor must be the last packet from an URB.  Then
		   traverse the URB list for the EP until the URB with urb_priv->last_sb
		   matching the intr-marked sb_desc is found.  All URBs before this have
		   been sent.
		*/

		for (epid = 0; epid < NBR_OF_EPIDS - 1; epid++) {
			/* Skip past epids with no SB lists, epids used for in traffic,
			   and special (dummy, invalid) epids. */
			if ((TxIsocEPList[epid].sub == 0) ||
			    (test_bit(epid, (void *)&epid_out_traffic) == 0) ||
			    (epid == DUMMY_EPID) ||
			    (epid == INVALID_EPID)) {
				/* Nothing here to see. */
				continue;
			}
			sb_desc = phys_to_virt(TxIsocEPList[epid].sub);

			/* Find the last descriptor of the currently active URB for this ep.
			   This is the first descriptor in the sub list marked for a descriptor
			   interrupt. */
			while (sb_desc && !IO_EXTRACT(USB_SB_command, intr, sb_desc->command)) {
				sb_desc = sb_desc->next ? phys_to_virt(sb_desc->next) : 0;
			}
			assert(sb_desc);

			dbg_isoc("Check epid %d, sub 0x%p, SB 0x%p",
				 epid,
				 phys_to_virt(TxIsocEPList[epid].sub),
				 sb_desc);

			epid_done = 0;

			/* Get the first urb (if any). */
			urb = urb_list_first(epid);
			assert(urb);

			while (urb && !epid_done) {

				/* Sanity check. */
				assert(usb_pipetype(urb->pipe) == PIPE_ISOCHRONOUS);

				if (!usb_pipeout(urb->pipe)) {
					/* descr interrupts are generated only for out pipes. */
					epid_done = 1;
					continue;
				}

				urb_priv = (etrax_urb_priv_t *)urb->hcpriv;
				assert(urb_priv);

				if (sb_desc != urb_priv->last_sb) {

					/* This urb has been sent. */
					dbg_isoc("out URB 0x%p sent", urb);

					urb_priv->urb_state = TRANSFER_DONE;

				} else if ((sb_desc == urb_priv->last_sb) &&
					   !(TxIsocEPList[epid].command & IO_MASK(USB_EP_command, enable))) {

					assert((sb_desc->command & IO_MASK(USB_SB_command, eol)) == IO_STATE(USB_SB_command, eol, yes));
					assert(sb_desc->next == 0);

					dbg_isoc("out URB 0x%p last in list, epid disabled", urb);
					TxIsocEPList[epid].sub = 0;
					TxIsocEPList[epid].hw_len = 0;
					urb_priv->urb_state = TRANSFER_DONE;

					epid_done = 1;

				} else {
					epid_done = 1;
				}
				if (!epid_done) {
					urb = urb_list_next(urb, epid);
				}
			}

		}

		*R_DMA_CH8_SUB3_CLR_INTR = IO_STATE(R_DMA_CH8_SUB3_CLR_INTR, clr_descr, do);

		comp_data = (usb_isoc_complete_data_t*)kmem_cache_alloc(isoc_compl_cache, SLAB_ATOMIC);
		assert(comp_data != NULL);

                INIT_WORK(&comp_data->usb_bh, etrax_usb_isoc_descr_interrupt_bottom_half, comp_data);
                schedule_work(&comp_data->usb_bh);
	}

	DBFEXIT;
        return IRQ_HANDLED;
}

static void etrax_usb_isoc_descr_interrupt_bottom_half(void *data)
{
	usb_isoc_complete_data_t *comp_data = (usb_isoc_complete_data_t*)data;

	struct urb *urb;
	int epid;
	int epid_done;
	etrax_urb_priv_t *urb_priv;

	DBFENTER;

	dbg_isoc("dma8_sub3_descr (ISOC) bottom half.");

	for (epid = 0; epid < NBR_OF_EPIDS - 1; epid++) {
		unsigned long flags;

		save_flags(flags);
		cli();

		epid_done = 0;

		/* The descriptor interrupt handler has marked all transmitted isoch. out
		   URBs with TRANSFER_DONE.  Now we traverse all epids and for all that
 		   have isoch. out traffic traverse its URB list and complete the
		   transmitted URB.
		*/

		while (!epid_done) {

			/* Get the first urb (if any). */
			urb = urb_list_first(epid);
			if (urb == 0) {
				epid_done = 1;
				continue;
			}

			if (usb_pipetype(urb->pipe) != PIPE_ISOCHRONOUS) {
					epid_done = 1;
					continue;
			}

			if (!usb_pipeout(urb->pipe)) {
				/* descr interrupts are generated only for out pipes. */
				epid_done = 1;
				continue;
			}

			dbg_isoc("Check epid %d, SB 0x%p", epid, (char*)TxIsocEPList[epid].sub);

			urb_priv = (etrax_urb_priv_t *)urb->hcpriv;
			assert(urb_priv);

			if (urb_priv->urb_state == TRANSFER_DONE) {
				int i;
				struct usb_iso_packet_descriptor *packet;

				/* This urb has been sent. */
				dbg_isoc("Completing isoc out URB 0x%p", urb);

				for (i = 0; i < urb->number_of_packets; i++) {
					packet = &urb->iso_frame_desc[i];
					packet->status = 0;
					packet->actual_length = packet->length;
				}

				etrax_usb_complete_isoc_urb(urb, 0);

				if (urb_list_empty(epid)) {
					etrax_usb_free_epid(epid);
					epid_done = 1;
				}
			} else {
				epid_done = 1;
			}
		}
		restore_flags(flags);

	}
	kmem_cache_free(isoc_compl_cache, comp_data);

	DBFEXIT;
}



static irqreturn_t etrax_usb_rx_interrupt(int irq, void *vhc)
{
	struct urb *urb;
	etrax_urb_priv_t *urb_priv;
	int epid = 0;
	unsigned long flags;

	/* Isoc diagnostics. */
	static int curr_fm = 0;
	static int prev_fm = 0;

	DBFENTER;

	/* Clear this interrupt. */
	*R_DMA_CH9_CLR_INTR = IO_STATE(R_DMA_CH9_CLR_INTR, clr_eop, do);

	/* Note that this while loop assumes that all packets span only
	   one rx descriptor. */

	/* The reason we cli here is that we call the driver's callback functions. */
	save_flags(flags);
	cli();

	while (myNextRxDesc->status & IO_MASK(USB_IN_status, eop)) {

		epid = IO_EXTRACT(USB_IN_status, epid, myNextRxDesc->status);
		urb = urb_list_first(epid);

		//printk("eop for epid %d, first urb 0x%lx\n", epid, (unsigned long)urb);

		if (!urb) {
			err("No urb for epid %d in rx interrupt", epid);
			__dump_ept_data(epid);
			goto skip_out;
		}

		/* Note that we cannot indescriminately assert(usb_pipein(urb->pipe)) since
		   ctrl pipes are not. */

		if (myNextRxDesc->status & IO_MASK(USB_IN_status, error)) {
			__u32 r_usb_ept_data;
			int no_error = 0;

			assert(test_bit(epid, (void *)&epid_usage_bitmask));

			*R_USB_EPT_INDEX = IO_FIELD(R_USB_EPT_INDEX, value, epid);
			nop();
			if (usb_pipetype(urb->pipe) == PIPE_ISOCHRONOUS) {
				r_usb_ept_data = *R_USB_EPT_DATA_ISO;

				if ((r_usb_ept_data & IO_MASK(R_USB_EPT_DATA_ISO, valid)) &&
				    (IO_EXTRACT(R_USB_EPT_DATA_ISO, error_code, r_usb_ept_data) == 0) &&
				    (myNextRxDesc->status & IO_MASK(USB_IN_status, nodata))) {
					/* Not an error, just a failure to receive an expected iso
					   in packet in this frame.  This is not documented
					   in the designers reference.
					*/
					no_error++;
				} else {
					warn("R_USB_EPT_DATA_ISO for epid %d = 0x%x", epid, r_usb_ept_data);
				}
			} else {
				r_usb_ept_data = *R_USB_EPT_DATA;
				warn("R_USB_EPT_DATA for epid %d = 0x%x", epid, r_usb_ept_data);
			}

			if (!no_error){
				warn("error in rx desc->status, epid %d, first urb = 0x%lx",
				     epid, (unsigned long)urb);
				__dump_in_desc(myNextRxDesc);

				warn("R_USB_STATUS = 0x%x", *R_USB_STATUS);

				/* Check that ept was disabled when error occurred. */
				switch (usb_pipetype(urb->pipe)) {
				case PIPE_BULK:
					assert(!(TxBulkEPList[epid].command & IO_MASK(USB_EP_command, enable)));
					break;
				case PIPE_CONTROL:
					assert(!(TxCtrlEPList[epid].command & IO_MASK(USB_EP_command, enable)));
					break;
				case PIPE_INTERRUPT:
					assert(!(TxIntrEPList[epid].command & IO_MASK(USB_EP_command, enable)));
					break;
				case PIPE_ISOCHRONOUS:
					assert(!(TxIsocEPList[epid].command & IO_MASK(USB_EP_command, enable)));
					break;
				default:
					warn("etrax_usb_rx_interrupt: bad pipetype %d in urb 0x%p",
					     usb_pipetype(urb->pipe),
					     urb);
				}
				etrax_usb_complete_urb(urb, -EPROTO);
				goto skip_out;
			}
		}

		urb_priv = (etrax_urb_priv_t *)urb->hcpriv;
		assert(urb_priv);

		if ((usb_pipetype(urb->pipe) == PIPE_BULK) ||
		    (usb_pipetype(urb->pipe) == PIPE_CONTROL) ||
		    (usb_pipetype(urb->pipe) == PIPE_INTERRUPT)) {

			if (myNextRxDesc->status & IO_MASK(USB_IN_status, nodata)) {
				/* We get nodata for empty data transactions, and the rx descriptor's
				   hw_len field is not valid in that case. No data to copy in other
				   words. */
			} else {
				/* Make sure the data fits in the buffer. */
				assert(urb_priv->rx_offset + myNextRxDesc->hw_len
				       <= urb->transfer_buffer_length);

				memcpy(urb->transfer_buffer + urb_priv->rx_offset,
				       phys_to_virt(myNextRxDesc->buf), myNextRxDesc->hw_len);
				urb_priv->rx_offset += myNextRxDesc->hw_len;
			}

			if (myNextRxDesc->status & IO_MASK(USB_IN_status, eot)) {
				if ((usb_pipetype(urb->pipe) == PIPE_CONTROL) &&
				    ((TxCtrlEPList[urb_priv->epid].command & IO_MASK(USB_EP_command, enable)) ==
				     IO_STATE(USB_EP_command, enable, yes))) {
					/* The EP is still enabled, so the OUT packet used to ack
					   the in data is probably not processed yet.  If the EP
					   sub pointer has not moved beyond urb_priv->last_sb mark
					   it for a descriptor interrupt and complete the urb in
					   the descriptor interrupt handler.
					*/
					USB_SB_Desc_t *sub = TxCtrlEPList[urb_priv->epid].sub ? phys_to_virt(TxCtrlEPList[urb_priv->epid].sub) : 0;

					while ((sub != NULL) && (sub != urb_priv->last_sb)) {
						sub = sub->next ? phys_to_virt(sub->next) : 0;
					}
					if (sub != NULL) {
						/* The urb has not been fully processed. */
						urb_priv->urb_state = WAITING_FOR_DESCR_INTR;
					} else {
						warn("(CTRL) epid enabled and urb (0x%p) processed, ep->sub=0x%p", urb, (char*)TxCtrlEPList[urb_priv->epid].sub);
						etrax_usb_complete_urb(urb, 0);
					}
				} else {
					etrax_usb_complete_urb(urb, 0);
				}
			}

		} else if (usb_pipetype(urb->pipe) == PIPE_ISOCHRONOUS) {

			struct usb_iso_packet_descriptor *packet;

			if (urb_priv->urb_state == UNLINK) {
				info("Ignoring rx data for urb being unlinked.");
				goto skip_out;
			} else if (urb_priv->urb_state == NOT_STARTED) {
				info("What? Got rx data for urb that isn't started?");
				goto skip_out;
			}

			packet = &urb->iso_frame_desc[urb_priv->isoc_packet_counter];
			packet->status = 0;

			if (myNextRxDesc->status & IO_MASK(USB_IN_status, nodata)) {
				/* We get nodata for empty data transactions, and the rx descriptor's
				   hw_len field is not valid in that case. We copy 0 bytes however to
				   stay in synch. */
				packet->actual_length = 0;
			} else {
				packet->actual_length = myNextRxDesc->hw_len;
				/* Make sure the data fits in the buffer. */
				assert(packet->actual_length <= packet->length);
				memcpy(urb->transfer_buffer + packet->offset,
				       phys_to_virt(myNextRxDesc->buf), packet->actual_length);
			}

			/* Increment the packet counter. */
			urb_priv->isoc_packet_counter++;

			/* Note that we don't care about the eot field in the rx descriptor's status.
			   It will always be set for isoc traffic. */
			if (urb->number_of_packets == urb_priv->isoc_packet_counter) {

				/* Out-of-synch diagnostics. */
				curr_fm = (*R_USB_FM_NUMBER & 0x7ff);
				if (((prev_fm + urb_priv->isoc_packet_counter) % (0x7ff + 1)) != curr_fm) {
					/* This test is wrong, if there is more than one isoc
					   in endpoint active it will always calculate wrong
					   since prev_fm is shared by all endpoints.

					   FIXME Make this check per URB using urb->start_frame.
					*/
					dbg_isoc("Out of synch? Previous frame = %d, current frame = %d",
						 prev_fm, curr_fm);

				}
				prev_fm = curr_fm;

				/* Complete the urb with status OK. */
				etrax_usb_complete_isoc_urb(urb, 0);
			}
		}

	skip_out:

		/* DMA IN cache bug. Flush the DMA IN buffer from the cache. (struct etrax_dma_descr
		   has the same layout as USB_IN_Desc for the relevant fields.) */
		prepare_rx_descriptor((struct etrax_dma_descr*)myNextRxDesc);

		myPrevRxDesc = myNextRxDesc;
		myPrevRxDesc->command |= IO_MASK(USB_IN_command, eol);
		myLastRxDesc->command &= ~IO_MASK(USB_IN_command, eol);
		myLastRxDesc = myPrevRxDesc;

		myNextRxDesc->status = 0;
		myNextRxDesc = phys_to_virt(myNextRxDesc->next);
	}

	restore_flags(flags);

	DBFEXIT;

        return IRQ_HANDLED;
}


/* This function will unlink the SB descriptors associated with this urb. */
static int etrax_remove_from_sb_list(struct urb *urb)
{
	USB_SB_Desc_t *next_sb, *first_sb, *last_sb;
	etrax_urb_priv_t *urb_priv;
	int i = 0;

	DBFENTER;

	urb_priv = (etrax_urb_priv_t *)urb->hcpriv;
	assert(urb_priv);

	/* Just a sanity check. Since we don't fiddle with the DMA list the EP descriptor
	   doesn't really need to be disabled, it's just that we expect it to be. */
	if (usb_pipetype(urb->pipe) == PIPE_BULK) {
		assert(!(TxBulkEPList[urb_priv->epid].command & IO_MASK(USB_EP_command, enable)));
	} else if (usb_pipetype(urb->pipe) == PIPE_CONTROL) {
		assert(!(TxCtrlEPList[urb_priv->epid].command & IO_MASK(USB_EP_command, enable)));
	}

	first_sb = urb_priv->first_sb;
	last_sb = urb_priv->last_sb;

	assert(first_sb);
	assert(last_sb);

	while (first_sb != last_sb) {
		next_sb = (USB_SB_Desc_t *)phys_to_virt(first_sb->next);
		kmem_cache_free(usb_desc_cache, first_sb);
		first_sb = next_sb;
		i++;
	}
	kmem_cache_free(usb_desc_cache, last_sb);
	i++;
	dbg_sb("%d SB descriptors freed", i);
	/* Compare i with urb->number_of_packets for Isoc traffic.
	   Should be same when calling unlink_urb */

	DBFEXIT;

	return i;
}

static int etrax_usb_submit_bulk_urb(struct urb *urb)
{
	int epid;
	int empty;
	unsigned long flags;
	etrax_urb_priv_t *urb_priv;

	DBFENTER;

	/* Epid allocation, empty check and list add must be protected.
	   Read about this in etrax_usb_submit_ctrl_urb. */

	spin_lock_irqsave(&urb_list_lock, flags);
	epid = etrax_usb_setup_epid(urb);
	if (epid == -1) {
		DBFEXIT;
		spin_unlock_irqrestore(&urb_list_lock, flags);
		return -ENOMEM;
	}
	empty = urb_list_empty(epid);
	urb_list_add(urb, epid);
	spin_unlock_irqrestore(&urb_list_lock, flags);

	dbg_bulk("Adding bulk %s urb 0x%lx to %s list, epid %d",
		 usb_pipein(urb->pipe) ? "IN" : "OUT", (unsigned long)urb, empty ? "empty" : "", epid);

	/* Mark the urb as being in progress. */
	urb->status = -EINPROGRESS;

	/* Setup the hcpriv data. */
	urb_priv = kzalloc(sizeof(etrax_urb_priv_t), KMALLOC_FLAG);
	assert(urb_priv != NULL);
	/* This sets rx_offset to 0. */
	urb_priv->urb_state = NOT_STARTED;
	urb->hcpriv = urb_priv;

	if (empty) {
		etrax_usb_add_to_bulk_sb_list(urb, epid);
	}

	DBFEXIT;

	return 0;
}

static void etrax_usb_add_to_bulk_sb_list(struct urb *urb, int epid)
{
	USB_SB_Desc_t *sb_desc;
	etrax_urb_priv_t *urb_priv = (etrax_urb_priv_t *)urb->hcpriv;
	unsigned long flags;
	char maxlen;

	DBFENTER;

	dbg_bulk("etrax_usb_add_to_bulk_sb_list, urb 0x%lx", (unsigned long)urb);

	maxlen = usb_maxpacket(urb->dev, urb->pipe, usb_pipeout(urb->pipe));

	sb_desc = (USB_SB_Desc_t*)kmem_cache_alloc(usb_desc_cache, SLAB_FLAG);
	assert(sb_desc != NULL);
	memset(sb_desc, 0, sizeof(USB_SB_Desc_t));


	if (usb_pipeout(urb->pipe)) {

		dbg_bulk("Grabbing bulk OUT, urb 0x%lx, epid %d", (unsigned long)urb, epid);

		/* This is probably a sanity check of the bulk transaction length
		   not being larger than 64 kB. */
		if (urb->transfer_buffer_length > 0xffff) {
			panic("urb->transfer_buffer_length > 0xffff");
		}

		sb_desc->sw_len = urb->transfer_buffer_length;

		/* The rem field is don't care if it's not a full-length transfer, so setting
		   it shouldn't hurt. Also, rem isn't used for OUT traffic. */
		sb_desc->command = (IO_FIELD(USB_SB_command, rem, 0) |
				    IO_STATE(USB_SB_command, tt, out) |
				    IO_STATE(USB_SB_command, eot, yes) |
				    IO_STATE(USB_SB_command, eol, yes));

		/* The full field is set to yes, even if we don't actually check that this is
		   a full-length transfer (i.e., that transfer_buffer_length % maxlen = 0).
		   Setting full prevents the USB controller from sending an empty packet in
		   that case.  However, if URB_ZERO_PACKET was set we want that. */
		if (!(urb->transfer_flags & URB_ZERO_PACKET)) {
			sb_desc->command |= IO_STATE(USB_SB_command, full, yes);
		}

		sb_desc->buf = virt_to_phys(urb->transfer_buffer);
		sb_desc->next = 0;

	} else if (usb_pipein(urb->pipe)) {

		dbg_bulk("Grabbing bulk IN, urb 0x%lx, epid %d", (unsigned long)urb, epid);

		sb_desc->sw_len = urb->transfer_buffer_length ?
			(urb->transfer_buffer_length - 1) / maxlen + 1 : 0;

		/* The rem field is don't care if it's not a full-length transfer, so setting
		   it shouldn't hurt. */
		sb_desc->command =
			(IO_FIELD(USB_SB_command, rem,
				  urb->transfer_buffer_length % maxlen) |
			 IO_STATE(USB_SB_command, tt, in) |
			 IO_STATE(USB_SB_command, eot, yes) |
			 IO_STATE(USB_SB_command, eol, yes));

		sb_desc->buf = 0;
		sb_desc->next = 0;
	}

	urb_priv->first_sb = sb_desc;
	urb_priv->last_sb = sb_desc;
	urb_priv->epid = epid;

	urb->hcpriv = urb_priv;

	/* Reset toggle bits and reset error count. */
	save_flags(flags);
	cli();

	*R_USB_EPT_INDEX = IO_FIELD(R_USB_EPT_INDEX, value, epid);
	nop();

	/* FIXME: Is this a special case since the hold field is checked,
	   or should we check hold in a lot of other cases as well? */
	if (*R_USB_EPT_DATA & IO_MASK(R_USB_EPT_DATA, hold)) {
		panic("Hold was set in %s", __FUNCTION__);
	}

	/* Reset error counters (regardless of which direction this traffic is). */
	*R_USB_EPT_DATA &=
		~(IO_MASK(R_USB_EPT_DATA, error_count_in) |
		  IO_MASK(R_USB_EPT_DATA, error_count_out));

	/* Software must preset the toggle bits. */
	if (usb_pipeout(urb->pipe)) {
		char toggle =
			usb_gettoggle(urb->dev, usb_pipeendpoint(urb->pipe), usb_pipeout(urb->pipe));
		*R_USB_EPT_DATA &= ~IO_MASK(R_USB_EPT_DATA, t_out);
		*R_USB_EPT_DATA |= IO_FIELD(R_USB_EPT_DATA, t_out, toggle);
	} else {
		char toggle =
			usb_gettoggle(urb->dev, usb_pipeendpoint(urb->pipe), usb_pipeout(urb->pipe));
		*R_USB_EPT_DATA &= ~IO_MASK(R_USB_EPT_DATA, t_in);
		*R_USB_EPT_DATA |= IO_FIELD(R_USB_EPT_DATA, t_in, toggle);
	}

	/* Assert that the EP descriptor is disabled. */
	assert(!(TxBulkEPList[epid].command & IO_MASK(USB_EP_command, enable)));

	/* The reason we set the EP's sub pointer directly instead of
	   walking the SB list and linking it last in the list is that we only
	   have one active urb at a time (the rest are queued). */

	/* Note that we cannot have interrupts running when we have set the SB descriptor
	   but the EP is not yet enabled.  If a bulk eot happens for another EP, we will
	   find this EP disabled and with a SB != 0, which will make us think that it's done. */
	TxBulkEPList[epid].sub = virt_to_phys(sb_desc);
	TxBulkEPList[epid].hw_len = 0;
	/* Note that we don't have to fill in the ep_id field since this
	   was done when we allocated the EP descriptors in init_tx_bulk_ep. */

	/* Check if the dummy list is already with us (if several urbs were queued). */
	if (TxBulkEPList[epid].next != virt_to_phys(&TxBulkDummyEPList[epid][0])) {

		dbg_bulk("Inviting dummy list to the party for urb 0x%lx, epid %d",
			 (unsigned long)urb, epid);

		/* The last EP in the dummy list already has its next pointer set to
		   TxBulkEPList[epid].next. */

		/* We don't need to check if the DMA is at this EP or not before changing the
		   next pointer, since we will do it in one 32-bit write (EP descriptors are
		   32-bit aligned). */
		TxBulkEPList[epid].next = virt_to_phys(&TxBulkDummyEPList[epid][0]);
	}
	/* Enable the EP descr. */
	dbg_bulk("Enabling bulk EP for urb 0x%lx, epid %d", (unsigned long)urb, epid);
	TxBulkEPList[epid].command |= IO_STATE(USB_EP_command, enable, yes);

	/* Everything is set up, safe to enable interrupts again. */
	restore_flags(flags);

	/* If the DMA bulk channel isn't running, we need to restart it if it
	   has stopped at the last EP descriptor (DMA stopped because there was
	   no more traffic) or if it has stopped at a dummy EP with the intr flag
	   set (DMA stopped because we were too slow in inserting new traffic). */
	if (!(*R_DMA_CH8_SUB0_CMD & IO_MASK(R_DMA_CH8_SUB0_CMD, cmd))) {

		USB_EP_Desc_t *ep;
		ep = (USB_EP_Desc_t *)phys_to_virt(*R_DMA_CH8_SUB0_EP);
		dbg_bulk("DMA channel not running in add");
		dbg_bulk("DMA is at 0x%lx", (unsigned long)ep);

		if (*R_DMA_CH8_SUB0_EP == virt_to_phys(&TxBulkEPList[NBR_OF_EPIDS - 1]) ||
		    (ep->command & 0x8) >> 3) {
			*R_DMA_CH8_SUB0_CMD = IO_STATE(R_DMA_CH8_SUB0_CMD, cmd, start);
			/* Update/restart the bulk start timer since we just started the channel. */
			mod_timer(&bulk_start_timer, jiffies + BULK_START_TIMER_INTERVAL);
			/* Update/restart the bulk eot timer since we just inserted traffic. */
			mod_timer(&bulk_eot_timer, jiffies + BULK_EOT_TIMER_INTERVAL);
		}
	}

	DBFEXIT;
}

static void etrax_usb_complete_bulk_urb(struct urb *urb, int status)
{
	etrax_urb_priv_t *urb_priv = (etrax_urb_priv_t *)urb->hcpriv;
	int epid = urb_priv->epid;
	unsigned long flags;

	DBFENTER;

	if (status)
		warn("Completing bulk urb with status %d.", status);

	dbg_bulk("Completing bulk urb 0x%lx for epid %d", (unsigned long)urb, epid);

	/* Update the urb list. */
	urb_list_del(urb, epid);

	/* For an IN pipe, we always set the actual length, regardless of whether there was
	   an error or not (which means the device driver can use the data if it wants to). */
	if (usb_pipein(urb->pipe)) {
		urb->actual_length = urb_priv->rx_offset;
	} else {
		/* Set actual_length for OUT urbs also; the USB mass storage driver seems
		   to want that. We wouldn't know of any partial writes if there was an error. */
		if (status == 0) {
			urb->actual_length = urb->transfer_buffer_length;
		} else {
			urb->actual_length = 0;
		}
	}

	/* FIXME: Is there something of the things below we shouldn't do if there was an error?
	   Like, maybe we shouldn't toggle the toggle bits, or maybe we shouldn't insert more traffic. */

	save_flags(flags);
	cli();

	*R_USB_EPT_INDEX = IO_FIELD(R_USB_EPT_INDEX, value, epid);
	nop();

	/* We need to fiddle with the toggle bits because the hardware doesn't do it for us. */
	if (usb_pipeout(urb->pipe)) {
		char toggle =
			IO_EXTRACT(R_USB_EPT_DATA, t_out, *R_USB_EPT_DATA);
		usb_settoggle(urb->dev, usb_pipeendpoint(urb->pipe),
			      usb_pipeout(urb->pipe), toggle);
	} else {
		char toggle =
			IO_EXTRACT(R_USB_EPT_DATA, t_in, *R_USB_EPT_DATA);
		usb_settoggle(urb->dev, usb_pipeendpoint(urb->pipe),
			      usb_pipeout(urb->pipe), toggle);
	}
	restore_flags(flags);

	/* Remember to free the SBs. */
	etrax_remove_from_sb_list(urb);
	kfree(urb_priv);
	urb->hcpriv = 0;

	/* If there are any more urb's in the list we'd better start sending */
	if (!urb_list_empty(epid)) {

		struct urb *new_urb;

		/* Get the first urb. */
		new_urb = urb_list_first(epid);
		assert(new_urb);

		dbg_bulk("More bulk for epid %d", epid);

		etrax_usb_add_to_bulk_sb_list(new_urb, epid);
	}

	urb->status = status;

	/* We let any non-zero status from the layer above have precedence. */
	if (status == 0) {
		/* URB_SHORT_NOT_OK means that short reads (shorter than the endpoint's max length)
		   is to be treated as an error. */
		if (urb->transfer_flags & URB_SHORT_NOT_OK) {
			if (usb_pipein(urb->pipe) &&
			    (urb->actual_length !=
			     usb_maxpacket(urb->dev, urb->pipe, usb_pipeout(urb->pipe)))) {
				urb->status = -EREMOTEIO;
			}
		}
	}

	if (urb->complete) {
		urb->complete(urb, NULL);
	}

	if (urb_list_empty(epid)) {
		/* This means that this EP is now free, deconfigure it. */
		etrax_usb_free_epid(epid);

		/* No more traffic; time to clean up.
		   Must set sub pointer to 0, since we look at the sub pointer when handling
		   the bulk eot interrupt. */

		dbg_bulk("No bulk for epid %d", epid);

		TxBulkEPList[epid].sub = 0;

		/* Unlink the dummy list. */

		dbg_bulk("Kicking dummy list out of party for urb 0x%lx, epid %d",
			 (unsigned long)urb, epid);

		/* No need to wait for the DMA before changing the next pointer.
		   The modulo NBR_OF_EPIDS isn't actually necessary, since we will never use
		   the last one (INVALID_EPID) for actual traffic. */
		TxBulkEPList[epid].next =
			virt_to_phys(&TxBulkEPList[(epid + 1) % NBR_OF_EPIDS]);
	}

	DBFEXIT;
}

static int etrax_usb_submit_ctrl_urb(struct urb *urb)
{
	int epid;
	int empty;
	unsigned long flags;
	etrax_urb_priv_t *urb_priv;

	DBFENTER;

	/* FIXME: Return -ENXIO if there is already a queued urb for this endpoint? */

	/* Epid allocation, empty check and list add must be protected.

	   Epid allocation because if we find an existing epid for this endpoint an urb might be
	   completed (emptying the list) before we add the new urb to the list, causing the epid
	   to be de-allocated. We would then start the transfer with an invalid epid -> epid attn.

	   Empty check and add because otherwise we might conclude that the list is not empty,
	   after which it becomes empty before we add the new urb to the list, causing us not to
	   insert the new traffic into the SB list. */

	spin_lock_irqsave(&urb_list_lock, flags);
	epid = etrax_usb_setup_epid(urb);
	if (epid == -1) {
		spin_unlock_irqrestore(&urb_list_lock, flags);
		DBFEXIT;
		return -ENOMEM;
	}
	empty = urb_list_empty(epid);
	urb_list_add(urb, epid);
	spin_unlock_irqrestore(&urb_list_lock, flags);

	dbg_ctrl("Adding ctrl urb 0x%lx to %s list, epid %d",
		 (unsigned long)urb, empty ? "empty" : "", epid);

	/* Mark the urb as being in progress. */
	urb->status = -EINPROGRESS;

	/* Setup the hcpriv data. */
	urb_priv = kzalloc(sizeof(etrax_urb_priv_t), KMALLOC_FLAG);
	assert(urb_priv != NULL);
	/* This sets rx_offset to 0. */
	urb_priv->urb_state = NOT_STARTED;
	urb->hcpriv = urb_priv;

	if (empty) {
		etrax_usb_add_to_ctrl_sb_list(urb, epid);
	}

	DBFEXIT;

	return 0;
}

static void etrax_usb_add_to_ctrl_sb_list(struct urb *urb, int epid)
{
	USB_SB_Desc_t *sb_desc_setup;
	USB_SB_Desc_t *sb_desc_data;
	USB_SB_Desc_t *sb_desc_status;

	etrax_urb_priv_t *urb_priv = (etrax_urb_priv_t *)urb->hcpriv;

	unsigned long flags;
	char maxlen;

	DBFENTER;

	maxlen = usb_maxpacket(urb->dev, urb->pipe, usb_pipeout(urb->pipe));

	sb_desc_setup = (USB_SB_Desc_t*)kmem_cache_alloc(usb_desc_cache, SLAB_FLAG);
	assert(sb_desc_setup != NULL);
	sb_desc_status = (USB_SB_Desc_t*)kmem_cache_alloc(usb_desc_cache, SLAB_FLAG);
	assert(sb_desc_status != NULL);

	/* Initialize the mandatory setup SB descriptor (used only in control transfers) */
	sb_desc_setup->sw_len = 8;
	sb_desc_setup->command = (IO_FIELD(USB_SB_command, rem, 0) |
				  IO_STATE(USB_SB_command, tt, setup) |
				  IO_STATE(USB_SB_command, full, yes) |
				  IO_STATE(USB_SB_command, eot, yes));

	sb_desc_setup->buf = virt_to_phys(urb->setup_packet);

	if (usb_pipeout(urb->pipe)) {
		dbg_ctrl("Transfer for epid %d is OUT", epid);

		/* If this Control OUT transfer has an optional data stage we add an OUT token
		   before the mandatory IN (status) token, hence the reordered SB list */

		sb_desc_setup->next = virt_to_phys(sb_desc_status);
		if (urb->transfer_buffer) {

			dbg_ctrl("This OUT transfer has an extra data stage");

			sb_desc_data = (USB_SB_Desc_t*)kmem_cache_alloc(usb_desc_cache, SLAB_FLAG);
			assert(sb_desc_data != NULL);

			sb_desc_setup->next = virt_to_phys(sb_desc_data);

			sb_desc_data->sw_len = urb->transfer_buffer_length;
			sb_desc_data->command = (IO_STATE(USB_SB_command, tt, out) |
						 IO_STATE(USB_SB_command, full, yes) |
						 IO_STATE(USB_SB_command, eot, yes));
			sb_desc_data->buf = virt_to_phys(urb->transfer_buffer);
			sb_desc_data->next = virt_to_phys(sb_desc_status);
		}

		sb_desc_status->sw_len = 1;
		sb_desc_status->command = (IO_FIELD(USB_SB_command, rem, 0) |
					   IO_STATE(USB_SB_command, tt, in) |
					   IO_STATE(USB_SB_command, eot, yes) |
					   IO_STATE(USB_SB_command, intr, yes) |
					   IO_STATE(USB_SB_command, eol, yes));

		sb_desc_status->buf = 0;
		sb_desc_status->next = 0;

	} else if (usb_pipein(urb->pipe)) {

		dbg_ctrl("Transfer for epid %d is IN", epid);
		dbg_ctrl("transfer_buffer_length = %d", urb->transfer_buffer_length);
		dbg_ctrl("rem is calculated to %d", urb->transfer_buffer_length % maxlen);

		sb_desc_data = (USB_SB_Desc_t*)kmem_cache_alloc(usb_desc_cache, SLAB_FLAG);
		assert(sb_desc_data != NULL);

		sb_desc_setup->next = virt_to_phys(sb_desc_data);

		sb_desc_data->sw_len = urb->transfer_buffer_length ?
			(urb->transfer_buffer_length - 1) / maxlen + 1 : 0;
		dbg_ctrl("sw_len got %d", sb_desc_data->sw_len);

		sb_desc_data->command =
			(IO_FIELD(USB_SB_command, rem,
				  urb->transfer_buffer_length % maxlen) |
			 IO_STATE(USB_SB_command, tt, in) |
			 IO_STATE(USB_SB_command, eot, yes));

		sb_desc_data->buf = 0;
		sb_desc_data->next = virt_to_phys(sb_desc_status);

		/* Read comment at zout_buffer declaration for an explanation to this. */
		sb_desc_status->sw_len = 1;
		sb_desc_status->command = (IO_FIELD(USB_SB_command, rem, 0) |
					   IO_STATE(USB_SB_command, tt, zout) |
					   IO_STATE(USB_SB_command, full, yes) |
					   IO_STATE(USB_SB_command, eot, yes) |
					   IO_STATE(USB_SB_command, intr, yes) |
					   IO_STATE(USB_SB_command, eol, yes));

		sb_desc_status->buf = virt_to_phys(&zout_buffer[0]);
		sb_desc_status->next = 0;
	}

	urb_priv->first_sb = sb_desc_setup;
	urb_priv->last_sb = sb_desc_status;
	urb_priv->epid = epid;

	urb_priv->urb_state = STARTED;

	/* Reset toggle bits and reset error count, remember to di and ei */
	/* Warning: it is possible that this locking doesn't work with bottom-halves */

	save_flags(flags);
	cli();

	*R_USB_EPT_INDEX = IO_FIELD(R_USB_EPT_INDEX, value, epid);
	nop();
	if (*R_USB_EPT_DATA & IO_MASK(R_USB_EPT_DATA, hold)) {
		panic("Hold was set in %s", __FUNCTION__);
	}


	/* FIXME: Compare with etrax_usb_add_to_bulk_sb_list where the toggle bits
	   are set to a specific value. Why the difference? Read "Transfer and Toggle Bits
	   in Designer's Reference, p. 8 - 11. */
	*R_USB_EPT_DATA &=
		~(IO_MASK(R_USB_EPT_DATA, error_count_in) |
		  IO_MASK(R_USB_EPT_DATA, error_count_out) |
		  IO_MASK(R_USB_EPT_DATA, t_in) |
		  IO_MASK(R_USB_EPT_DATA, t_out));

	/* Since we use the rx interrupt to complete ctrl urbs, we can enable interrupts now
	   (i.e. we don't check the sub pointer on an eot interrupt like we do for bulk traffic). */
	restore_flags(flags);

	/* Assert that the EP descriptor is disabled. */
	assert(!(TxCtrlEPList[epid].command & IO_MASK(USB_EP_command, enable)));

	/* Set up and enable the EP descriptor. */
	TxCtrlEPList[epid].sub = virt_to_phys(sb_desc_setup);
	TxCtrlEPList[epid].hw_len = 0;
	TxCtrlEPList[epid].command |= IO_STATE(USB_EP_command, enable, yes);

	/* We start the DMA sub channel without checking if it's running or not, because:
	   1) If it's already running, issuing the start command is a nop.
	   2) We avoid a test-and-set race condition. */
	*R_DMA_CH8_SUB1_CMD = IO_STATE(R_DMA_CH8_SUB1_CMD, cmd, start);

	DBFEXIT;
}

static void etrax_usb_complete_ctrl_urb(struct urb *urb, int status)
{
	etrax_urb_priv_t *urb_priv = (etrax_urb_priv_t *)urb->hcpriv;
	int epid = urb_priv->epid;

	DBFENTER;

	if (status)
		warn("Completing ctrl urb with status %d.", status);

	dbg_ctrl("Completing ctrl epid %d, urb 0x%lx", epid, (unsigned long)urb);

	/* Remove this urb from the list. */
	urb_list_del(urb, epid);

	/* For an IN pipe, we always set the actual length, regardless of whether there was
	   an error or not (which means the device driver can use the data if it wants to). */
	if (usb_pipein(urb->pipe)) {
		urb->actual_length = urb_priv->rx_offset;
	}

	/* FIXME: Is there something of the things below we shouldn't do if there was an error?
	   Like, maybe we shouldn't insert more traffic. */

	/* Remember to free the SBs. */
	etrax_remove_from_sb_list(urb);
	kfree(urb_priv);
	urb->hcpriv = 0;

	/* If there are any more urbs in the list we'd better start sending. */
	if (!urb_list_empty(epid)) {
		struct urb *new_urb;

		/* Get the first urb. */
		new_urb = urb_list_first(epid);
		assert(new_urb);

		dbg_ctrl("More ctrl for epid %d, first urb = 0x%lx", epid, (unsigned long)new_urb);

		etrax_usb_add_to_ctrl_sb_list(new_urb, epid);
	}

	urb->status = status;

	/* We let any non-zero status from the layer above have precedence. */
	if (status == 0) {
		/* URB_SHORT_NOT_OK means that short reads (shorter than the endpoint's max length)
		   is to be treated as an error. */
		if (urb->transfer_flags & URB_SHORT_NOT_OK) {
			if (usb_pipein(urb->pipe) &&
			    (urb->actual_length !=
			     usb_maxpacket(urb->dev, urb->pipe, usb_pipeout(urb->pipe)))) {
				urb->status = -EREMOTEIO;
			}
		}
	}

	if (urb->complete) {
		urb->complete(urb, NULL);
	}

	if (urb_list_empty(epid)) {
		/* No more traffic. Time to clean up. */
		etrax_usb_free_epid(epid);
		/* Must set sub pointer to 0. */
		dbg_ctrl("No ctrl for epid %d", epid);
		TxCtrlEPList[epid].sub = 0;
	}

	DBFEXIT;
}

static int etrax_usb_submit_intr_urb(struct urb *urb)
{

	int epid;

	DBFENTER;

	if (usb_pipeout(urb->pipe)) {
		/* Unsupported transfer type.
		   We don't support interrupt out traffic. (If we do, we can't support
		   intervals for neither in or out traffic, but are forced to schedule all
		   interrupt traffic in one frame.) */
		return -EINVAL;
	}

	epid = etrax_usb_setup_epid(urb);
	if (epid == -1) {
		DBFEXIT;
		return -ENOMEM;
	}

	if (!urb_list_empty(epid)) {
		/* There is already a queued urb for this endpoint. */
		etrax_usb_free_epid(epid);
		return -ENXIO;
	}

	urb->status = -EINPROGRESS;

	dbg_intr("Add intr urb 0x%lx, to list, epid %d", (unsigned long)urb, epid);

	urb_list_add(urb, epid);
	etrax_usb_add_to_intr_sb_list(urb, epid);

	return 0;

	DBFEXIT;
}

static void etrax_usb_add_to_intr_sb_list(struct urb *urb, int epid)
{

	volatile USB_EP_Desc_t *tmp_ep;
	volatile USB_EP_Desc_t *first_ep;

	char maxlen;
	int interval;
	int i;

	etrax_urb_priv_t *urb_priv;

	DBFENTER;

	maxlen = usb_maxpacket(urb->dev, urb->pipe, usb_pipeout(urb->pipe));
	interval = urb->interval;

	urb_priv = kzalloc(sizeof(etrax_urb_priv_t), KMALLOC_FLAG);
	assert(urb_priv != NULL);
	urb->hcpriv = urb_priv;

	first_ep = &TxIntrEPList[0];

	/* Round of the interval to 2^n, it is obvious that this code favours
	   smaller numbers, but that is actually a good thing */
	/* FIXME: The "rounding error" for larger intervals will be quite
	   large. For in traffic this shouldn't be a problem since it will only
	   mean that we "poll" more often. */
	for (i = 0; interval; i++) {
		interval = interval >> 1;
	}
	interval = 1 << (i - 1);

	dbg_intr("Interval rounded to %d", interval);

	tmp_ep = first_ep;
	i = 0;
	do {
		if (tmp_ep->command & IO_MASK(USB_EP_command, eof)) {
			if ((i % interval) == 0) {
				/* Insert the traffic ep after tmp_ep */
				USB_EP_Desc_t *ep_desc;
				USB_SB_Desc_t *sb_desc;

				dbg_intr("Inserting EP for epid %d", epid);

				ep_desc = (USB_EP_Desc_t *)
					kmem_cache_alloc(usb_desc_cache, SLAB_FLAG);
				sb_desc = (USB_SB_Desc_t *)
					kmem_cache_alloc(usb_desc_cache, SLAB_FLAG);
				assert(ep_desc != NULL);
				CHECK_ALIGN(ep_desc);
				assert(sb_desc != NULL);

				ep_desc->sub = virt_to_phys(sb_desc);
				ep_desc->hw_len = 0;
				ep_desc->command = (IO_FIELD(USB_EP_command, epid, epid) |
						    IO_STATE(USB_EP_command, enable, yes));


				/* Round upwards the number of packets of size maxlen
				   that this SB descriptor should receive. */
				sb_desc->sw_len = urb->transfer_buffer_length ?
					(urb->transfer_buffer_length - 1) / maxlen + 1 : 0;
				sb_desc->next = 0;
				sb_desc->buf = 0;
				sb_desc->command =
					(IO_FIELD(USB_SB_command, rem, urb->transfer_buffer_length % maxlen) |
					 IO_STATE(USB_SB_command, tt, in) |
					 IO_STATE(USB_SB_command, eot, yes) |
					 IO_STATE(USB_SB_command, eol, yes));

				ep_desc->next = tmp_ep->next;
				tmp_ep->next = virt_to_phys(ep_desc);
			}
			i++;
		}
		tmp_ep = (USB_EP_Desc_t *)phys_to_virt(tmp_ep->next);
	} while (tmp_ep != first_ep);


	/* Note that first_sb/last_sb doesn't apply to interrupt traffic. */
	urb_priv->epid = epid;

	/* We start the DMA sub channel without checking if it's running or not, because:
	   1) If it's already running, issuing the start command is a nop.
	   2) We avoid a test-and-set race condition. */
	*R_DMA_CH8_SUB2_CMD = IO_STATE(R_DMA_CH8_SUB2_CMD, cmd, start);

	DBFEXIT;
}



static void etrax_usb_complete_intr_urb(struct urb *urb, int status)
{
	etrax_urb_priv_t *urb_priv = (etrax_urb_priv_t *)urb->hcpriv;
	int epid = urb_priv->epid;

	DBFENTER;

	if (status)
		warn("Completing intr urb with status %d.", status);

	dbg_intr("Completing intr epid %d, urb 0x%lx", epid, (unsigned long)urb);

	urb->status = status;
	urb->actual_length = urb_priv->rx_offset;

	dbg_intr("interrupt urb->actual_length = %d", urb->actual_length);

	/* We let any non-zero status from the layer above have precedence. */
	if (status == 0) {
		/* URB_SHORT_NOT_OK means that short reads (shorter than the endpoint's max length)
		   is to be treated as an error. */
		if (urb->transfer_flags & URB_SHORT_NOT_OK) {
			if (urb->actual_length !=
			    usb_maxpacket(urb->dev, urb->pipe, usb_pipeout(urb->pipe))) {
				urb->status = -EREMOTEIO;
			}
		}
	}

	/* The driver will resubmit the URB so we need to remove it first */
        etrax_usb_unlink_urb(urb, 0);
	if (urb->complete) {
		urb->complete(urb, NULL);
	}

	DBFEXIT;
}


static int etrax_usb_submit_isoc_urb(struct urb *urb)
{
	int epid;
	unsigned long flags;

	DBFENTER;

	dbg_isoc("Submitting isoc urb = 0x%lx", (unsigned long)urb);

	/* Epid allocation, empty check and list add must be protected.
	   Read about this in etrax_usb_submit_ctrl_urb. */

	spin_lock_irqsave(&urb_list_lock, flags);
	/* Is there an active epid for this urb ? */
	epid = etrax_usb_setup_epid(urb);
	if (epid == -1) {
		DBFEXIT;
		spin_unlock_irqrestore(&urb_list_lock, flags);
		return -ENOMEM;
	}

	/* Ok, now we got valid endpoint, lets insert some traffic */

	urb->status = -EINPROGRESS;

	/* Find the last urb in the URB_List and add this urb after that one.
	   Also add the traffic, that is do an etrax_usb_add_to_isoc_sb_list.  This
	   is important to make this in "real time" since isochronous traffic is
	   time sensitive. */

	dbg_isoc("Adding isoc urb to (possibly empty) list");
	urb_list_add(urb, epid);
	etrax_usb_add_to_isoc_sb_list(urb, epid);
	spin_unlock_irqrestore(&urb_list_lock, flags);

	DBFEXIT;

	return 0;
}

static void etrax_usb_check_error_isoc_ep(const int epid)
{
	unsigned long int flags;
	int error_code;
	__u32 r_usb_ept_data;

	/* We can't read R_USB_EPID_ATTN here since it would clear the iso_eof,
	   bulk_eot and epid_attn interrupts.  So we just check the status of
	   the epid without testing if for it in R_USB_EPID_ATTN. */


	save_flags(flags);
	cli();
	*R_USB_EPT_INDEX = IO_FIELD(R_USB_EPT_INDEX, value, epid);
	nop();
	/* Note that although there are separate R_USB_EPT_DATA and R_USB_EPT_DATA_ISO
	   registers, they are located at the same address and are of the same size.
	   In other words, this read should be ok for isoc also. */
	r_usb_ept_data = *R_USB_EPT_DATA;
	restore_flags(flags);

	error_code = IO_EXTRACT(R_USB_EPT_DATA_ISO, error_code, r_usb_ept_data);

	if (r_usb_ept_data & IO_MASK(R_USB_EPT_DATA, hold)) {
		warn("Hold was set for epid %d.", epid);
		return;
	}

	if (error_code == IO_STATE_VALUE(R_USB_EPT_DATA_ISO, error_code, no_error)) {

		/* This indicates that the SB list of the ept was completed before
		   new data was appended to it.  This is not an error, but indicates
		   large system or USB load and could possibly cause trouble for
		   very timing sensitive USB device drivers so we log it.
		*/
		info("Isoc. epid %d disabled with no error", epid);
		return;

	} else if (error_code == IO_STATE_VALUE(R_USB_EPT_DATA_ISO, error_code, stall)) {
		/* Not really a protocol error, just says that the endpoint gave
		   a stall response. Note that error_code cannot be stall for isoc. */
		panic("Isoc traffic cannot stall");

	} else if (error_code == IO_STATE_VALUE(R_USB_EPT_DATA_ISO, error_code, bus_error)) {
		/* Two devices responded to a transaction request. Must be resolved
		   by software. FIXME: Reset ports? */
		panic("Bus error for epid %d."
		      " Two devices responded to transaction request",
		      epid);

	} else if (error_code == IO_STATE_VALUE(R_USB_EPT_DATA, error_code, buffer_error)) {
		/* DMA overrun or underrun. */
		warn("Buffer overrun/underrun for epid %d. DMA too busy?", epid);

		/* It seems that error_code = buffer_error in
		   R_USB_EPT_DATA/R_USB_EPT_DATA_ISO and ourun = yes in R_USB_STATUS
		   are the same error. */
	}
}


static void etrax_usb_add_to_isoc_sb_list(struct urb *urb, int epid)
{

	int i = 0;

	etrax_urb_priv_t *urb_priv;
	USB_SB_Desc_t *prev_sb_desc,  *next_sb_desc, *temp_sb_desc;

	DBFENTER;

	prev_sb_desc = next_sb_desc = temp_sb_desc = NULL;

	urb_priv = kzalloc(sizeof(etrax_urb_priv_t), GFP_ATOMIC);
	assert(urb_priv != NULL);

	urb->hcpriv = urb_priv;
	urb_priv->epid = epid;

	if (usb_pipeout(urb->pipe)) {

		if (urb->number_of_packets == 0) panic("etrax_usb_add_to_isoc_sb_list 0 packets\n");

		dbg_isoc("Transfer for epid %d is OUT", epid);
		dbg_isoc("%d packets in URB", urb->number_of_packets);

		/* Create one SB descriptor for each packet and link them together. */
		for (i = 0; i < urb->number_of_packets; i++) {
			if (!urb->iso_frame_desc[i].length)
				continue;

			next_sb_desc = (USB_SB_Desc_t*)kmem_cache_alloc(usb_desc_cache, SLAB_ATOMIC);
			assert(next_sb_desc != NULL);

			if (urb->iso_frame_desc[i].length > 0) {

				next_sb_desc->command = (IO_STATE(USB_SB_command, tt, out) |
							 IO_STATE(USB_SB_command, eot, yes));

				next_sb_desc->sw_len = urb->iso_frame_desc[i].length;
				next_sb_desc->buf = virt_to_phys((char*)urb->transfer_buffer + urb->iso_frame_desc[i].offset);

				/* Check if full length transfer. */
				if (urb->iso_frame_desc[i].length ==
				    usb_maxpacket(urb->dev, urb->pipe, usb_pipeout(urb->pipe))) {
					next_sb_desc->command |= IO_STATE(USB_SB_command, full, yes);
				}
			} else {
				dbg_isoc("zero len packet");
				next_sb_desc->command = (IO_FIELD(USB_SB_command, rem, 0) |
							 IO_STATE(USB_SB_command, tt, zout) |
							 IO_STATE(USB_SB_command, eot, yes) |
							 IO_STATE(USB_SB_command, full, yes));

				next_sb_desc->sw_len = 1;
				next_sb_desc->buf = virt_to_phys(&zout_buffer[0]);
			}

			/* First SB descriptor that belongs to this urb */
			if (i == 0)
				urb_priv->first_sb = next_sb_desc;
			else
				prev_sb_desc->next = virt_to_phys(next_sb_desc);

			prev_sb_desc = next_sb_desc;
		}

		next_sb_desc->command |= (IO_STATE(USB_SB_command, intr, yes) |
					  IO_STATE(USB_SB_command, eol, yes));
		next_sb_desc->next = 0;
		urb_priv->last_sb = next_sb_desc;

	} else if (usb_pipein(urb->pipe)) {

		dbg_isoc("Transfer for epid %d is IN", epid);
		dbg_isoc("transfer_buffer_length = %d", urb->transfer_buffer_length);
		dbg_isoc("rem is calculated to %d", urb->iso_frame_desc[urb->number_of_packets - 1].length);

		/* Note that in descriptors for periodic traffic are not consumed. This means that
		   the USB controller never propagates in the SB list. In other words, if there already
		   is an SB descriptor in the list for this EP we don't have to do anything. */
		if (TxIsocEPList[epid].sub == 0) {
			dbg_isoc("Isoc traffic not already running, allocating SB");

			next_sb_desc = (USB_SB_Desc_t*)kmem_cache_alloc(usb_desc_cache, SLAB_ATOMIC);
			assert(next_sb_desc != NULL);

			next_sb_desc->command = (IO_STATE(USB_SB_command, tt, in) |
						 IO_STATE(USB_SB_command, eot, yes) |
						 IO_STATE(USB_SB_command, eol, yes));

			next_sb_desc->next = 0;
			next_sb_desc->sw_len = 1; /* Actual number of packets is not relevant
						     for periodic in traffic as long as it is more
						     than zero.  Set to 1 always. */
			next_sb_desc->buf = 0;

			/* The rem field is don't care for isoc traffic, so we don't set it. */

			/* Only one SB descriptor that belongs to this urb. */
			urb_priv->first_sb = next_sb_desc;
			urb_priv->last_sb = next_sb_desc;

		} else {

			dbg_isoc("Isoc traffic already running, just setting first/last_sb");

			/* Each EP for isoc in will have only one SB descriptor, setup when submitting the
			   already active urb. Note that even though we may have several first_sb/last_sb
			   pointing at the same SB descriptor, they are freed only once (when the list has
			   become empty). */
			urb_priv->first_sb = phys_to_virt(TxIsocEPList[epid].sub);
			urb_priv->last_sb = phys_to_virt(TxIsocEPList[epid].sub);
			return;
		}

	}

	/* Find the spot to insert this urb and add it. */
	if (TxIsocEPList[epid].sub == 0) {
		/* First SB descriptor inserted in this list (in or out). */
		dbg_isoc("Inserting SB desc first in list");
		TxIsocEPList[epid].hw_len = 0;
		TxIsocEPList[epid].sub = virt_to_phys(urb_priv->first_sb);

	} else {
		/* Isochronous traffic is already running, insert new traffic last (only out). */
		dbg_isoc("Inserting SB desc last in list");
		temp_sb_desc = phys_to_virt(TxIsocEPList[epid].sub);
		while ((temp_sb_desc->command & IO_MASK(USB_SB_command, eol)) !=
		       IO_STATE(USB_SB_command, eol, yes)) {
			assert(temp_sb_desc->next);
			temp_sb_desc = phys_to_virt(temp_sb_desc->next);
		}
		dbg_isoc("Appending list on desc 0x%p", temp_sb_desc);

		/* Next pointer must be set before eol is removed. */
		temp_sb_desc->next = virt_to_phys(urb_priv->first_sb);
		/* Clear the previous end of list flag since there is a new in the
		   added SB descriptor list. */
		temp_sb_desc->command &= ~IO_MASK(USB_SB_command, eol);

		if (!(TxIsocEPList[epid].command & IO_MASK(USB_EP_command, enable))) {
			/* 8.8.5 in Designer's Reference says we should check for and correct
			   any errors in the EP here.  That should not be necessary if epid_attn
			   is handled correctly, so we assume all is ok. */
			dbg_isoc("EP disabled");
			etrax_usb_check_error_isoc_ep(epid);

			/* The SB list was exhausted. */
			if (virt_to_phys(urb_priv->last_sb) != TxIsocEPList[epid].sub) {
				/* The new sublist did not get processed before the EP was
				   disabled.  Setup the EP again. */
				dbg_isoc("Set EP sub to new list");
				TxIsocEPList[epid].hw_len = 0;
				TxIsocEPList[epid].sub = virt_to_phys(urb_priv->first_sb);
			}
		}
	}

	if (urb->transfer_flags & URB_ISO_ASAP) {
		/* The isoc transfer should be started as soon as possible. The start_frame
		   field is a return value if URB_ISO_ASAP was set. Comparing R_USB_FM_NUMBER
		   with a USB Chief trace shows that the first isoc IN token is sent 2 frames
		   later. I'm not sure how this affects usage of the start_frame field by the
		   device driver, or how it affects things when USB_ISO_ASAP is not set, so
		   therefore there's no compensation for the 2 frame "lag" here. */
		urb->start_frame = (*R_USB_FM_NUMBER & 0x7ff);
		TxIsocEPList[epid].command |= IO_STATE(USB_EP_command, enable, yes);
		urb_priv->urb_state = STARTED;
		dbg_isoc("URB_ISO_ASAP set, urb->start_frame set to %d", urb->start_frame);
	} else {
		/* Not started yet. */
		urb_priv->urb_state = NOT_STARTED;
		dbg_isoc("urb_priv->urb_state set to NOT_STARTED");
	}

       /* We start the DMA sub channel without checking if it's running or not, because:
	  1) If it's already running, issuing the start command is a nop.
	  2) We avoid a test-and-set race condition. */
	*R_DMA_CH8_SUB3_CMD = IO_STATE(R_DMA_CH8_SUB3_CMD, cmd, start);

	DBFEXIT;
}

static void etrax_usb_complete_isoc_urb(struct urb *urb, int status)
{
	etrax_urb_priv_t *urb_priv = (etrax_urb_priv_t *)urb->hcpriv;
	int epid = urb_priv->epid;
	int auto_resubmit = 0;

	DBFENTER;
	dbg_isoc("complete urb 0x%p, status %d", urb, status);

	if (status)
		warn("Completing isoc urb with status %d.", status);

	if (usb_pipein(urb->pipe)) {
		int i;

		/* Make that all isoc packets have status and length set before
		   completing the urb. */
		for (i = urb_priv->isoc_packet_counter; i < urb->number_of_packets; i++) {
			urb->iso_frame_desc[i].actual_length = 0;
			urb->iso_frame_desc[i].status = -EPROTO;
		}

		urb_list_del(urb, epid);

		if (!list_empty(&urb_list[epid])) {
			((etrax_urb_priv_t *)(urb_list_first(epid)->hcpriv))->urb_state = STARTED;
		} else {
			unsigned long int flags;
			if (TxIsocEPList[epid].command & IO_MASK(USB_EP_command, enable)) {
				/* The EP was enabled, disable it and wait. */
				TxIsocEPList[epid].command &= ~IO_MASK(USB_EP_command, enable);

				/* Ah, the luxury of busy-wait. */
				while (*R_DMA_CH8_SUB3_EP == virt_to_phys(&TxIsocEPList[epid]));
			}

			etrax_remove_from_sb_list(urb);
			TxIsocEPList[epid].sub = 0;
			TxIsocEPList[epid].hw_len = 0;

			save_flags(flags);
			cli();
			etrax_usb_free_epid(epid);
			restore_flags(flags);
		}

		urb->hcpriv = 0;
		kfree(urb_priv);

		/* Release allocated bandwidth. */
		usb_release_bandwidth(urb->dev, urb, 0);
	} else if (usb_pipeout(urb->pipe)) {
		int freed_descr;

		dbg_isoc("Isoc out urb complete 0x%p", urb);

		/* Update the urb list. */
		urb_list_del(urb, epid);

		freed_descr = etrax_remove_from_sb_list(urb);
		dbg_isoc("freed %d descriptors of %d packets", freed_descr, urb->number_of_packets);
		assert(freed_descr == urb->number_of_packets);
		urb->hcpriv = 0;
		kfree(urb_priv);

		/* Release allocated bandwidth. */
		usb_release_bandwidth(urb->dev, urb, 0);
	}

	urb->status = status;
	if (urb->complete) {
		urb->complete(urb, NULL);
	}

	if (auto_resubmit) {
		/* Check that urb was not unlinked by the complete callback. */
		if (__urb_list_entry(urb, epid)) {
			/* Move this one down the list. */
			urb_list_move_last(urb, epid);

			/* Mark the now first urb as started (may already be). */
			((etrax_urb_priv_t *)(urb_list_first(epid)->hcpriv))->urb_state = STARTED;

			/* Must set this to 0 since this urb is still active after
			   completion. */
			urb_priv->isoc_packet_counter = 0;
		} else {
			warn("(ISOC) automatic resubmit urb 0x%p removed by complete.", urb);
		}
	}

	DBFEXIT;
}

static void etrax_usb_complete_urb(struct urb *urb, int status)
{
	switch (usb_pipetype(urb->pipe)) {
	case PIPE_BULK:
		etrax_usb_complete_bulk_urb(urb, status);
		break;
	case PIPE_CONTROL:
		etrax_usb_complete_ctrl_urb(urb, status);
		break;
	case PIPE_INTERRUPT:
		etrax_usb_complete_intr_urb(urb, status);
		break;
	case PIPE_ISOCHRONOUS:
		etrax_usb_complete_isoc_urb(urb, status);
		break;
	default:
		err("Unknown pipetype");
	}
}



static irqreturn_t etrax_usb_hc_interrupt_top_half(int irq, void *vhc)
{
	usb_interrupt_registers_t *reg;
	unsigned long flags;
	__u32 irq_mask;
	__u8 status;
	__u32 epid_attn;
	__u16 port_status_1;
	__u16 port_status_2;
	__u32 fm_number;

	DBFENTER;

	/* Read critical registers into local variables, do kmalloc afterwards. */
	save_flags(flags);
	cli();

	irq_mask = *R_USB_IRQ_MASK_READ;
	/* Reading R_USB_STATUS clears the ctl_status interrupt. Note that R_USB_STATUS
	   must be read before R_USB_EPID_ATTN since reading the latter clears the
	   ourun and perror fields of R_USB_STATUS. */
	status = *R_USB_STATUS;

	/* Reading R_USB_EPID_ATTN clears the iso_eof, bulk_eot and epid_attn interrupts. */
	epid_attn = *R_USB_EPID_ATTN;

	/* Reading R_USB_RH_PORT_STATUS_1 and R_USB_RH_PORT_STATUS_2 clears the
	   port_status interrupt. */
	port_status_1 = *R_USB_RH_PORT_STATUS_1;
	port_status_2 = *R_USB_RH_PORT_STATUS_2;

	/* Reading R_USB_FM_NUMBER clears the sof interrupt. */
	/* Note: the lower 11 bits contain the actual frame number, sent with each sof. */
	fm_number = *R_USB_FM_NUMBER;

	restore_flags(flags);

	reg = (usb_interrupt_registers_t *)kmem_cache_alloc(top_half_reg_cache, SLAB_ATOMIC);

	assert(reg != NULL);

	reg->hc = (etrax_hc_t *)vhc;

	/* Now put register values into kmalloc'd area. */
	reg->r_usb_irq_mask_read = irq_mask;
	reg->r_usb_status = status;
	reg->r_usb_epid_attn = epid_attn;
	reg->r_usb_rh_port_status_1 = port_status_1;
	reg->r_usb_rh_port_status_2 = port_status_2;
	reg->r_usb_fm_number = fm_number;

        INIT_WORK(&reg->usb_bh, etrax_usb_hc_interrupt_bottom_half, reg);
        schedule_work(&reg->usb_bh);

	DBFEXIT;

        return IRQ_HANDLED;
}

static void etrax_usb_hc_interrupt_bottom_half(void *data)
{
	usb_interrupt_registers_t *reg = (usb_interrupt_registers_t *)data;
	__u32 irq_mask = reg->r_usb_irq_mask_read;

	DBFENTER;

	/* Interrupts are handled in order of priority. */
	if (irq_mask & IO_MASK(R_USB_IRQ_MASK_READ, epid_attn)) {
		etrax_usb_hc_epid_attn_interrupt(reg);
	}
	if (irq_mask & IO_MASK(R_USB_IRQ_MASK_READ, port_status)) {
		etrax_usb_hc_port_status_interrupt(reg);
	}
	if (irq_mask & IO_MASK(R_USB_IRQ_MASK_READ, ctl_status)) {
		etrax_usb_hc_ctl_status_interrupt(reg);
	}
	if (irq_mask & IO_MASK(R_USB_IRQ_MASK_READ, iso_eof)) {
		etrax_usb_hc_isoc_eof_interrupt();
	}
	if (irq_mask & IO_MASK(R_USB_IRQ_MASK_READ, bulk_eot)) {
		/* Update/restart the bulk start timer since obviously the channel is running. */
		mod_timer(&bulk_start_timer, jiffies + BULK_START_TIMER_INTERVAL);
		/* Update/restart the bulk eot timer since we just received an bulk eot interrupt. */
		mod_timer(&bulk_eot_timer, jiffies + BULK_EOT_TIMER_INTERVAL);

		etrax_usb_hc_bulk_eot_interrupt(0);
	}

	kmem_cache_free(top_half_reg_cache, reg);

	DBFEXIT;
}


void etrax_usb_hc_isoc_eof_interrupt(void)
{
	struct urb *urb;
	etrax_urb_priv_t *urb_priv;
	int epid;
	unsigned long flags;

	DBFENTER;

	/* Do not check the invalid epid (it has a valid sub pointer). */
	for (epid = 0; epid < NBR_OF_EPIDS - 1; epid++) {

		/* Do not check the invalid epid (it has a valid sub pointer). */
		if ((epid == DUMMY_EPID) || (epid == INVALID_EPID))
			continue;

		/* Disable interrupts to block the isoc out descriptor interrupt handler
		   from being called while the isoc EPID list is being checked.
		*/
		save_flags(flags);
		cli();

		if (TxIsocEPList[epid].sub == 0) {
			/* Nothing here to see. */
			restore_flags(flags);
			continue;
		}

		/* Get the first urb (if any). */
		urb = urb_list_first(epid);
		if (urb == 0) {
			warn("Ignoring NULL urb");
			restore_flags(flags);
			continue;
		}
		if (usb_pipein(urb->pipe)) {

			/* Sanity check. */
			assert(usb_pipetype(urb->pipe) == PIPE_ISOCHRONOUS);

			urb_priv = (etrax_urb_priv_t *)urb->hcpriv;
			assert(urb_priv);

			if (urb_priv->urb_state == NOT_STARTED) {

				/* If ASAP is not set and urb->start_frame is the current frame,
				   start the transfer. */
				if (!(urb->transfer_flags & URB_ISO_ASAP) &&
				    (urb->start_frame == (*R_USB_FM_NUMBER & 0x7ff))) {

					dbg_isoc("Enabling isoc IN EP descr for epid %d", epid);
					TxIsocEPList[epid].command |= IO_STATE(USB_EP_command, enable, yes);

					/* This urb is now active. */
					urb_priv->urb_state = STARTED;
					continue;
				}
			}
		}
		restore_flags(flags);
	}

	DBFEXIT;

}

void etrax_usb_hc_bulk_eot_interrupt(int timer_induced)
{
 	int epid;

	/* The technique is to run one urb at a time, wait for the eot interrupt at which
	   point the EP descriptor has been disabled. */

	DBFENTER;
	dbg_bulk("bulk eot%s", timer_induced ? ", called by timer" : "");

	for (epid = 0; epid < NBR_OF_EPIDS; epid++) {

		if (!(TxBulkEPList[epid].command & IO_MASK(USB_EP_command, enable)) &&
		    (TxBulkEPList[epid].sub != 0)) {

			struct urb *urb;
			etrax_urb_priv_t *urb_priv;
			unsigned long flags;
			__u32 r_usb_ept_data;

			/* Found a disabled EP descriptor which has a non-null sub pointer.
			   Verify that this ctrl EP descriptor got disabled no errors.
			   FIXME: Necessary to check error_code? */
			dbg_bulk("for epid %d?", epid);

			/* Get the first urb. */
			urb = urb_list_first(epid);

			/* FIXME: Could this happen for valid reasons? Why did it disappear? Because of
			   wrong unlinking? */
			if (!urb) {
				warn("NULL urb for epid %d", epid);
				continue;
			}

			assert(urb);
			urb_priv = (etrax_urb_priv_t *)urb->hcpriv;
			assert(urb_priv);

			/* Sanity checks. */
			assert(usb_pipetype(urb->pipe) == PIPE_BULK);
			if (phys_to_virt(TxBulkEPList[epid].sub) != urb_priv->last_sb) {
				err("bulk endpoint got disabled before reaching last sb");
			}

			/* For bulk IN traffic, there seems to be a race condition between
			   between the bulk eot and eop interrupts, or rather an uncertainty regarding
			   the order in which they happen. Normally we expect the eop interrupt from
			   DMA channel 9 to happen before the eot interrupt.

			   Therefore, we complete the bulk IN urb in the rx interrupt handler instead. */

			if (usb_pipein(urb->pipe)) {
				dbg_bulk("in urb, continuing");
				continue;
			}

			save_flags(flags);
			cli();
			*R_USB_EPT_INDEX = IO_FIELD(R_USB_EPT_INDEX, value, epid);
			nop();
			r_usb_ept_data = *R_USB_EPT_DATA;
			restore_flags(flags);

			if (IO_EXTRACT(R_USB_EPT_DATA, error_code, r_usb_ept_data) ==
			    IO_STATE_VALUE(R_USB_EPT_DATA, error_code, no_error)) {
				/* This means that the endpoint has no error, is disabled
				   and had inserted traffic, i.e. transfer successfully completed. */
				etrax_usb_complete_bulk_urb(urb, 0);
			} else {
				/* Shouldn't happen. We expect errors to be caught by epid attention. */
				err("Found disabled bulk EP desc, error_code != no_error");
			}
		}
	}

	/* Normally, we should find (at least) one disabled EP descriptor with a valid sub pointer.
	   However, because of the uncertainty in the deliverance of the eop/eot interrupts, we may
	   not.  Also, we might find two disabled EPs when handling an eot interrupt, and then find
	   none the next time. */

	DBFEXIT;

}

void etrax_usb_hc_epid_attn_interrupt(usb_interrupt_registers_t *reg)
{
	/* This function handles the epid attention interrupt.  There are a variety of reasons
	   for this interrupt to happen (Designer's Reference, p. 8 - 22 for the details):

	   invalid ep_id  - Invalid epid in an EP (EP disabled).
	   stall	  - Not strictly an error condition (EP disabled).
	   3rd error      - Three successive transaction errors  (EP disabled).
	   buffer ourun   - Buffer overrun or underrun (EP disabled).
	   past eof1      - Intr or isoc transaction proceeds past EOF1.
	   near eof       - Intr or isoc transaction would not fit inside the frame.
	   zout transfer  - If zout transfer for a bulk endpoint (EP disabled).
	   setup transfer - If setup transfer for a non-ctrl endpoint (EP disabled). */

	int epid;


	DBFENTER;

	assert(reg != NULL);

	/* Note that we loop through all epids. We still want to catch errors for
	   the invalid one, even though we might handle them differently. */
	for (epid = 0; epid < NBR_OF_EPIDS; epid++) {

		if (test_bit(epid, (void *)&reg->r_usb_epid_attn)) {

			struct urb *urb;
			__u32 r_usb_ept_data;
			unsigned long flags;
			int error_code;

			save_flags(flags);
			cli();
			*R_USB_EPT_INDEX = IO_FIELD(R_USB_EPT_INDEX, value, epid);
			nop();
			/* Note that although there are separate R_USB_EPT_DATA and R_USB_EPT_DATA_ISO
			   registers, they are located at the same address and are of the same size.
			   In other words, this read should be ok for isoc also. */
			r_usb_ept_data = *R_USB_EPT_DATA;
			restore_flags(flags);

			/* First some sanity checks. */
			if (epid == INVALID_EPID) {
				/* FIXME: What if it became disabled? Could seriously hurt interrupt
				   traffic. (Use do_intr_recover.) */
				warn("Got epid_attn for INVALID_EPID (%d).", epid);
				err("R_USB_EPT_DATA = 0x%x", r_usb_ept_data);
				err("R_USB_STATUS = 0x%x", reg->r_usb_status);
				continue;
			} else 	if (epid == DUMMY_EPID) {
				/* We definitely don't care about these ones. Besides, they are
				   always disabled, so any possible disabling caused by the
				   epid attention interrupt is irrelevant. */
				warn("Got epid_attn for DUMMY_EPID (%d).", epid);
				continue;
			}

			/* Get the first urb in the urb list for this epid. We blatantly assume
			   that only the first urb could have caused the epid attention.
			   (For bulk and ctrl, only one urb is active at any one time. For intr
			   and isoc we remove them once they are completed.) */
			urb = urb_list_first(epid);

			if (urb == NULL) {
				err("Got epid_attn for epid %i with no urb.", epid);
				err("R_USB_EPT_DATA = 0x%x", r_usb_ept_data);
				err("R_USB_STATUS = 0x%x", reg->r_usb_status);
				continue;
			}

			switch (usb_pipetype(urb->pipe)) {
			case PIPE_BULK:
				warn("Got epid attn for bulk endpoint, epid %d", epid);
				break;
			case PIPE_CONTROL:
				warn("Got epid attn for control endpoint, epid %d", epid);
				break;
			case PIPE_INTERRUPT:
				warn("Got epid attn for interrupt endpoint, epid %d", epid);
				break;
			case PIPE_ISOCHRONOUS:
				warn("Got epid attn for isochronous endpoint, epid %d", epid);
				break;
			}

			if (usb_pipetype(urb->pipe) != PIPE_ISOCHRONOUS) {
				if (r_usb_ept_data & IO_MASK(R_USB_EPT_DATA, hold)) {
					warn("Hold was set for epid %d.", epid);
					continue;
				}
			}

			/* Even though error_code occupies bits 22 - 23 in both R_USB_EPT_DATA and
			   R_USB_EPT_DATA_ISOC, we separate them here so we don't forget in other places. */
			if (usb_pipetype(urb->pipe) == PIPE_ISOCHRONOUS) {
				error_code = IO_EXTRACT(R_USB_EPT_DATA_ISO, error_code, r_usb_ept_data);
			} else {
				error_code = IO_EXTRACT(R_USB_EPT_DATA, error_code, r_usb_ept_data);
			}

			/* Using IO_STATE_VALUE on R_USB_EPT_DATA should be ok for isoc also. */
			if (error_code == IO_STATE_VALUE(R_USB_EPT_DATA, error_code, no_error)) {

				/* Isoc traffic doesn't have error_count_in/error_count_out. */
				if ((usb_pipetype(urb->pipe) != PIPE_ISOCHRONOUS) &&
				    (IO_EXTRACT(R_USB_EPT_DATA, error_count_in, r_usb_ept_data) == 3 ||
				     IO_EXTRACT(R_USB_EPT_DATA, error_count_out, r_usb_ept_data) == 3)) {
					/* 3rd error. */
					warn("3rd error for epid %i", epid);
					etrax_usb_complete_urb(urb, -EPROTO);

				} else if (reg->r_usb_status & IO_MASK(R_USB_STATUS, perror)) {

					warn("Perror for epid %d", epid);

					if (!(r_usb_ept_data & IO_MASK(R_USB_EPT_DATA, valid))) {
						/* invalid ep_id */
						panic("Perror because of invalid epid."
						      " Deconfigured too early?");
					} else {
						/* past eof1, near eof, zout transfer, setup transfer */

						/* Dump the urb and the relevant EP descriptor list. */

						__dump_urb(urb);
						__dump_ept_data(epid);
						__dump_ep_list(usb_pipetype(urb->pipe));

						panic("Something wrong with DMA descriptor contents."
						      " Too much traffic inserted?");
					}
				} else if (reg->r_usb_status & IO_MASK(R_USB_STATUS, ourun)) {
					/* buffer ourun */
					panic("Buffer overrun/underrun for epid %d. DMA too busy?", epid);
				}

			} else if (error_code == IO_STATE_VALUE(R_USB_EPT_DATA, error_code, stall)) {
				/* Not really a protocol error, just says that the endpoint gave
				   a stall response. Note that error_code cannot be stall for isoc. */
				if (usb_pipetype(urb->pipe) == PIPE_ISOCHRONOUS) {
					panic("Isoc traffic cannot stall");
				}

				warn("Stall for epid %d", epid);
				etrax_usb_complete_urb(urb, -EPIPE);

			} else if (error_code == IO_STATE_VALUE(R_USB_EPT_DATA, error_code, bus_error)) {
				/* Two devices responded to a transaction request. Must be resolved
				   by software. FIXME: Reset ports? */
				panic("Bus error for epid %d."
				      " Two devices responded to transaction request",
				      epid);

			} else if (error_code == IO_STATE_VALUE(R_USB_EPT_DATA, error_code, buffer_error)) {
				/* DMA overrun or underrun. */
				warn("Buffer overrun/underrun for epid %d. DMA too busy?", epid);

				/* It seems that error_code = buffer_error in
				   R_USB_EPT_DATA/R_USB_EPT_DATA_ISO and ourun = yes in R_USB_STATUS
				   are the same error. */
				etrax_usb_complete_urb(urb, -EPROTO);
			}
		}
	}

	DBFEXIT;

}

void etrax_usb_bulk_start_timer_func(unsigned long dummy)
{

	/* We might enable an EP descriptor behind the current DMA position when it's about
	   to decide that there are no more bulk traffic and it should stop the bulk channel.
	   Therefore we periodically check if the bulk channel is stopped and there is an
	   enabled bulk EP descriptor, in which case we start the bulk channel. */
	dbg_bulk("bulk_start_timer timed out.");

	if (!(*R_DMA_CH8_SUB0_CMD & IO_MASK(R_DMA_CH8_SUB0_CMD, cmd))) {
		int epid;

		dbg_bulk("Bulk DMA channel not running.");

		for (epid = 0; epid < NBR_OF_EPIDS; epid++) {
			if (TxBulkEPList[epid].command & IO_MASK(USB_EP_command, enable)) {
				dbg_bulk("Found enabled EP for epid %d, starting bulk channel.\n",
					 epid);
				*R_DMA_CH8_SUB0_CMD = IO_STATE(R_DMA_CH8_SUB0_CMD, cmd, start);

				/* Restart the bulk eot timer since we just started the bulk channel. */
				mod_timer(&bulk_eot_timer, jiffies + BULK_EOT_TIMER_INTERVAL);

				/* No need to search any further. */
				break;
			}
		}
	} else {
		dbg_bulk("Bulk DMA channel running.");
	}
}

void etrax_usb_hc_port_status_interrupt(usb_interrupt_registers_t *reg)
{
	etrax_hc_t *hc = reg->hc;
	__u16 r_usb_rh_port_status_1 = reg->r_usb_rh_port_status_1;
	__u16 r_usb_rh_port_status_2 = reg->r_usb_rh_port_status_2;

	DBFENTER;

	/* The Etrax RH does not include a wPortChange register, so this has to be handled in software
	   (by saving the old port status value for comparison when the port status interrupt happens).
	   See section 11.16.2.6.2 in the USB 1.1 spec for details. */

	dbg_rh("hc->rh.prev_wPortStatus_1 = 0x%x", hc->rh.prev_wPortStatus_1);
	dbg_rh("hc->rh.prev_wPortStatus_2 = 0x%x", hc->rh.prev_wPortStatus_2);
	dbg_rh("r_usb_rh_port_status_1 = 0x%x", r_usb_rh_port_status_1);
	dbg_rh("r_usb_rh_port_status_2 = 0x%x", r_usb_rh_port_status_2);

	/* C_PORT_CONNECTION is set on any transition. */
	hc->rh.wPortChange_1 |=
		((r_usb_rh_port_status_1 & (1 << RH_PORT_CONNECTION)) !=
		 (hc->rh.prev_wPortStatus_1 & (1 << RH_PORT_CONNECTION))) ?
		(1 << RH_PORT_CONNECTION) : 0;

	hc->rh.wPortChange_2 |=
		((r_usb_rh_port_status_2 & (1 << RH_PORT_CONNECTION)) !=
		 (hc->rh.prev_wPortStatus_2 & (1 << RH_PORT_CONNECTION))) ?
		(1 << RH_PORT_CONNECTION) : 0;

	/* C_PORT_ENABLE is _only_ set on a one to zero transition, i.e. when
	   the port is disabled, not when it's enabled. */
	hc->rh.wPortChange_1 |=
		((hc->rh.prev_wPortStatus_1 & (1 << RH_PORT_ENABLE))
		 && !(r_usb_rh_port_status_1 & (1 << RH_PORT_ENABLE))) ?
		(1 << RH_PORT_ENABLE) : 0;

	hc->rh.wPortChange_2 |=
		((hc->rh.prev_wPortStatus_2 & (1 << RH_PORT_ENABLE))
		 && !(r_usb_rh_port_status_2 & (1 << RH_PORT_ENABLE))) ?
		(1 << RH_PORT_ENABLE) : 0;

	/* C_PORT_SUSPEND is set to one when the device has transitioned out
	   of the suspended state, i.e. when suspend goes from one to zero. */
	hc->rh.wPortChange_1 |=
		((hc->rh.prev_wPortStatus_1 & (1 << RH_PORT_SUSPEND))
		 && !(r_usb_rh_port_status_1 & (1 << RH_PORT_SUSPEND))) ?
		(1 << RH_PORT_SUSPEND) : 0;

	hc->rh.wPortChange_2 |=
		((hc->rh.prev_wPortStatus_2 & (1 << RH_PORT_SUSPEND))
		 && !(r_usb_rh_port_status_2 & (1 << RH_PORT_SUSPEND))) ?
		(1 << RH_PORT_SUSPEND) : 0;


	/* C_PORT_RESET is set when reset processing on this port is complete. */
	hc->rh.wPortChange_1 |=
		((hc->rh.prev_wPortStatus_1 & (1 << RH_PORT_RESET))
		 && !(r_usb_rh_port_status_1 & (1 << RH_PORT_RESET))) ?
		(1 << RH_PORT_RESET) : 0;

	hc->rh.wPortChange_2 |=
		((hc->rh.prev_wPortStatus_2 & (1 << RH_PORT_RESET))
		 && !(r_usb_rh_port_status_2 & (1 << RH_PORT_RESET))) ?
		(1 << RH_PORT_RESET) : 0;

	/* Save the new values for next port status change. */
	hc->rh.prev_wPortStatus_1 = r_usb_rh_port_status_1;
	hc->rh.prev_wPortStatus_2 = r_usb_rh_port_status_2;

	dbg_rh("hc->rh.wPortChange_1 set to 0x%x", hc->rh.wPortChange_1);
	dbg_rh("hc->rh.wPortChange_2 set to 0x%x", hc->rh.wPortChange_2);

	DBFEXIT;

}

void etrax_usb_hc_ctl_status_interrupt(usb_interrupt_registers_t *reg)
{
	DBFENTER;

	/* FIXME: What should we do if we get ourun or perror? Dump the EP and SB
	   list for the corresponding epid? */
	if (reg->r_usb_status & IO_MASK(R_USB_STATUS, ourun)) {
		panic("USB controller got ourun.");
	}
	if (reg->r_usb_status & IO_MASK(R_USB_STATUS, perror)) {

		/* Before, etrax_usb_do_intr_recover was called on this epid if it was
		   an interrupt pipe. I don't see how re-enabling all EP descriptors
		   will help if there was a programming error. */
		panic("USB controller got perror.");
	}

	if (reg->r_usb_status & IO_MASK(R_USB_STATUS, device_mode)) {
		/* We should never operate in device mode. */
		panic("USB controller in device mode.");
	}

	/* These if-statements could probably be nested. */
	if (reg->r_usb_status & IO_MASK(R_USB_STATUS, host_mode)) {
		info("USB controller in host mode.");
	}
	if (reg->r_usb_status & IO_MASK(R_USB_STATUS, started)) {
		info("USB controller started.");
	}
	if (reg->r_usb_status & IO_MASK(R_USB_STATUS, running)) {
		info("USB controller running.");
	}

	DBFEXIT;

}


static int etrax_rh_submit_urb(struct urb *urb)
{
	struct usb_device *usb_dev = urb->dev;
	etrax_hc_t *hc = usb_dev->bus->hcpriv;
	unsigned int pipe = urb->pipe;
	struct usb_ctrlrequest *cmd = (struct usb_ctrlrequest *) urb->setup_packet;
	void *data = urb->transfer_buffer;
	int leni = urb->transfer_buffer_length;
	int len = 0;
	int stat = 0;

	__u16 bmRType_bReq;
	__u16 wValue;
	__u16 wIndex;
	__u16 wLength;

	DBFENTER;

	/* FIXME: What is this interrupt urb that is sent to the root hub? */
	if (usb_pipetype (pipe) == PIPE_INTERRUPT) {
		dbg_rh("Root-Hub submit IRQ: every %d ms", urb->interval);
		hc->rh.urb = urb;
		hc->rh.send = 1;
		/* FIXME: We could probably remove this line since it's done
		   in etrax_rh_init_int_timer. (Don't remove it from
		   etrax_rh_init_int_timer though.) */
		hc->rh.interval = urb->interval;
		etrax_rh_init_int_timer(urb);
		DBFEXIT;

		return 0;
	}

	bmRType_bReq = cmd->bRequestType | (cmd->bRequest << 8);
	wValue = le16_to_cpu(cmd->wValue);
	wIndex = le16_to_cpu(cmd->wIndex);
	wLength = le16_to_cpu(cmd->wLength);

	dbg_rh("bmRType_bReq : 0x%04x (%d)", bmRType_bReq, bmRType_bReq);
	dbg_rh("wValue       : 0x%04x (%d)", wValue, wValue);
	dbg_rh("wIndex       : 0x%04x (%d)", wIndex, wIndex);
	dbg_rh("wLength      : 0x%04x (%d)", wLength, wLength);

	switch (bmRType_bReq) {

		/* Request Destination:
		   without flags: Device,
		   RH_INTERFACE: interface,
		   RH_ENDPOINT: endpoint,
		   RH_CLASS means HUB here,
		   RH_OTHER | RH_CLASS  almost ever means HUB_PORT here
		 */

	case RH_GET_STATUS:
		*(__u16 *) data = cpu_to_le16 (1);
		OK (2);

	case RH_GET_STATUS | RH_INTERFACE:
		*(__u16 *) data = cpu_to_le16 (0);
		OK (2);

	case RH_GET_STATUS | RH_ENDPOINT:
		*(__u16 *) data = cpu_to_le16 (0);
		OK (2);

	case RH_GET_STATUS | RH_CLASS:
		*(__u32 *) data = cpu_to_le32 (0);
		OK (4);		/* hub power ** */

	case RH_GET_STATUS | RH_OTHER | RH_CLASS:
		if (wIndex == 1) {
			*((__u16*)data) = cpu_to_le16(hc->rh.prev_wPortStatus_1);
			*((__u16*)data + 1) = cpu_to_le16(hc->rh.wPortChange_1);
		} else if (wIndex == 2) {
			*((__u16*)data) = cpu_to_le16(hc->rh.prev_wPortStatus_2);
			*((__u16*)data + 1) = cpu_to_le16(hc->rh.wPortChange_2);
		} else {
			dbg_rh("RH_GET_STATUS whith invalid wIndex!");
			OK(0);
		}

		OK(4);

	case RH_CLEAR_FEATURE | RH_ENDPOINT:
		switch (wValue) {
		case (RH_ENDPOINT_STALL):
			OK (0);
		}
		break;

	case RH_CLEAR_FEATURE | RH_CLASS:
		switch (wValue) {
		case (RH_C_HUB_OVER_CURRENT):
			OK (0);	/* hub power over current ** */
		}
		break;

	case RH_CLEAR_FEATURE | RH_OTHER | RH_CLASS:
		switch (wValue) {
		case (RH_PORT_ENABLE):
			if (wIndex == 1) {

				dbg_rh("trying to do disable port 1");

				*R_USB_PORT1_DISABLE = IO_STATE(R_USB_PORT1_DISABLE, disable, yes);

				while (hc->rh.prev_wPortStatus_1 &
				       IO_STATE(R_USB_RH_PORT_STATUS_1, enabled, yes));
				*R_USB_PORT1_DISABLE = IO_STATE(R_USB_PORT1_DISABLE, disable, no);
				dbg_rh("Port 1 is disabled");

			} else if (wIndex == 2) {

				dbg_rh("trying to do disable port 2");

				*R_USB_PORT2_DISABLE = IO_STATE(R_USB_PORT2_DISABLE, disable, yes);

				while (hc->rh.prev_wPortStatus_2 &
				       IO_STATE(R_USB_RH_PORT_STATUS_2, enabled, yes));
				*R_USB_PORT2_DISABLE = IO_STATE(R_USB_PORT2_DISABLE, disable, no);
				dbg_rh("Port 2 is disabled");

			} else {
				dbg_rh("RH_CLEAR_FEATURE->RH_PORT_ENABLE "
				       "with invalid wIndex == %d!", wIndex);
			}

			OK (0);
		case (RH_PORT_SUSPEND):
			/* Opposite to suspend should be resume, so we'll do a resume. */
			/* FIXME: USB 1.1, 11.16.2.2 says:
			   "Clearing the PORT_SUSPEND feature causes a host-initiated resume
			   on the specified port. If the port is not in the Suspended state,
			   the hub should treat this request as a functional no-operation."
			   Shouldn't we check if the port is in a suspended state before
			   resuming? */

			/* Make sure the controller isn't busy. */
			while (*R_USB_COMMAND & IO_MASK(R_USB_COMMAND, busy));

			if (wIndex == 1) {
				*R_USB_COMMAND =
					IO_STATE(R_USB_COMMAND, port_sel, port1) |
					IO_STATE(R_USB_COMMAND, port_cmd, resume) |
					IO_STATE(R_USB_COMMAND, ctrl_cmd, nop);
			} else if (wIndex == 2) {
				*R_USB_COMMAND =
					IO_STATE(R_USB_COMMAND, port_sel, port2) |
					IO_STATE(R_USB_COMMAND, port_cmd, resume) |
					IO_STATE(R_USB_COMMAND, ctrl_cmd, nop);
			} else {
				dbg_rh("RH_CLEAR_FEATURE->RH_PORT_SUSPEND "
				       "with invalid wIndex == %d!", wIndex);
			}

			OK (0);
		case (RH_PORT_POWER):
			OK (0);	/* port power ** */
		case (RH_C_PORT_CONNECTION):
			if (wIndex == 1) {
				hc->rh.wPortChange_1 &= ~(1 << RH_PORT_CONNECTION);
			} else if (wIndex == 2) {
				hc->rh.wPortChange_2 &= ~(1 << RH_PORT_CONNECTION);
			} else {
				dbg_rh("RH_CLEAR_FEATURE->RH_C_PORT_CONNECTION "
				       "with invalid wIndex == %d!", wIndex);
			}

			OK (0);
		case (RH_C_PORT_ENABLE):
			if (wIndex == 1) {
				hc->rh.wPortChange_1 &= ~(1 << RH_PORT_ENABLE);
			} else if (wIndex == 2) {
				hc->rh.wPortChange_2 &= ~(1 << RH_PORT_ENABLE);
			} else {
				dbg_rh("RH_CLEAR_FEATURE->RH_C_PORT_ENABLE "
				       "with invalid wIndex == %d!", wIndex);
			}
			OK (0);
		case (RH_C_PORT_SUSPEND):
/*** WR_RH_PORTSTAT(RH_PS_PSSC); */
			OK (0);
		case (RH_C_PORT_OVER_CURRENT):
			OK (0);	/* port power over current ** */
		case (RH_C_PORT_RESET):
			if (wIndex == 1) {
				hc->rh.wPortChange_1 &= ~(1 << RH_PORT_RESET);
			} else if (wIndex == 2) {
				hc->rh.wPortChange_2 &= ~(1 << RH_PORT_RESET);
			} else {
				dbg_rh("RH_CLEAR_FEATURE->RH_C_PORT_RESET "
				       "with invalid index == %d!", wIndex);
			}

			OK (0);

		}
		break;

	case RH_SET_FEATURE | RH_OTHER | RH_CLASS:
		switch (wValue) {
		case (RH_PORT_SUSPEND):

			/* Make sure the controller isn't busy. */
			while (*R_USB_COMMAND & IO_MASK(R_USB_COMMAND, busy));

			if (wIndex == 1) {
				*R_USB_COMMAND =
					IO_STATE(R_USB_COMMAND, port_sel, port1) |
					IO_STATE(R_USB_COMMAND, port_cmd, suspend) |
					IO_STATE(R_USB_COMMAND, ctrl_cmd, nop);
			} else if (wIndex == 2) {
				*R_USB_COMMAND =
					IO_STATE(R_USB_COMMAND, port_sel, port2) |
					IO_STATE(R_USB_COMMAND, port_cmd, suspend) |
					IO_STATE(R_USB_COMMAND, ctrl_cmd, nop);
			} else {
				dbg_rh("RH_SET_FEATURE->RH_PORT_SUSPEND "
				       "with invalid wIndex == %d!", wIndex);
			}

			OK (0);
		case (RH_PORT_RESET):
			if (wIndex == 1) {

			port_1_reset:
				dbg_rh("Doing reset of port 1");

				/* Make sure the controller isn't busy. */
				while (*R_USB_COMMAND & IO_MASK(R_USB_COMMAND, busy));

				*R_USB_COMMAND =
					IO_STATE(R_USB_COMMAND, port_sel, port1) |
					IO_STATE(R_USB_COMMAND, port_cmd, reset) |
					IO_STATE(R_USB_COMMAND, ctrl_cmd, nop);

				/* We must wait at least 10 ms for the device to recover.
				   15 ms should be enough. */
				udelay(15000);

				/* Wait for reset bit to go low (should be done by now). */
				while (hc->rh.prev_wPortStatus_1 &
				       IO_STATE(R_USB_RH_PORT_STATUS_1, reset, yes));

				/* If the port status is
				   1) connected and enabled then there is a device and everything is fine
				   2) neither connected nor enabled then there is no device, also fine
				   3) connected and not enabled then we try again
				   (Yes, there are other port status combinations besides these.) */

				if ((hc->rh.prev_wPortStatus_1 &
				     IO_STATE(R_USB_RH_PORT_STATUS_1, connected, yes)) &&
				    (hc->rh.prev_wPortStatus_1 &
				     IO_STATE(R_USB_RH_PORT_STATUS_1, enabled, no))) {
					dbg_rh("Connected device on port 1, but port not enabled?"
					       " Trying reset again.");
					goto port_2_reset;
				}

				/* Diagnostic printouts. */
				if ((hc->rh.prev_wPortStatus_1 &
				     IO_STATE(R_USB_RH_PORT_STATUS_1, connected, no)) &&
				    (hc->rh.prev_wPortStatus_1 &
				     IO_STATE(R_USB_RH_PORT_STATUS_1, enabled, no))) {
					dbg_rh("No connected device on port 1");
				} else if ((hc->rh.prev_wPortStatus_1 &
					    IO_STATE(R_USB_RH_PORT_STATUS_1, connected, yes)) &&
					   (hc->rh.prev_wPortStatus_1 &
					    IO_STATE(R_USB_RH_PORT_STATUS_1, enabled, yes))) {
					dbg_rh("Connected device on port 1, port 1 enabled");
				}

			} else if (wIndex == 2) {

			port_2_reset:
				dbg_rh("Doing reset of port 2");

				/* Make sure the controller isn't busy. */
				while (*R_USB_COMMAND & IO_MASK(R_USB_COMMAND, busy));

				/* Issue the reset command. */
				*R_USB_COMMAND =
					IO_STATE(R_USB_COMMAND, port_sel, port2) |
					IO_STATE(R_USB_COMMAND, port_cmd, reset) |
					IO_STATE(R_USB_COMMAND, ctrl_cmd, nop);

				/* We must wait at least 10 ms for the device to recover.
				   15 ms should be enough. */
				udelay(15000);

				/* Wait for reset bit to go low (should be done by now). */
				while (hc->rh.prev_wPortStatus_2 &
				       IO_STATE(R_USB_RH_PORT_STATUS_2, reset, yes));

				/* If the port status is
				   1) connected and enabled then there is a device and everything is fine
				   2) neither connected nor enabled then there is no device, also fine
				   3) connected and not enabled then we try again
				   (Yes, there are other port status combinations besides these.) */

				if ((hc->rh.prev_wPortStatus_2 &
				     IO_STATE(R_USB_RH_PORT_STATUS_2, connected, yes)) &&
				    (hc->rh.prev_wPortStatus_2 &
				     IO_STATE(R_USB_RH_PORT_STATUS_2, enabled, no))) {
					dbg_rh("Connected device on port 2, but port not enabled?"
					       " Trying reset again.");
					goto port_2_reset;
				}

				/* Diagnostic printouts. */
				if ((hc->rh.prev_wPortStatus_2 &
				     IO_STATE(R_USB_RH_PORT_STATUS_2, connected, no)) &&
				    (hc->rh.prev_wPortStatus_2 &
				     IO_STATE(R_USB_RH_PORT_STATUS_2, enabled, no))) {
					dbg_rh("No connected device on port 2");
				} else if ((hc->rh.prev_wPortStatus_2 &
					    IO_STATE(R_USB_RH_PORT_STATUS_2, connected, yes)) &&
					   (hc->rh.prev_wPortStatus_2 &
					    IO_STATE(R_USB_RH_PORT_STATUS_2, enabled, yes))) {
					dbg_rh("Connected device on port 2, port 2 enabled");
				}

			} else {
				dbg_rh("RH_SET_FEATURE->RH_PORT_RESET with invalid wIndex = %d", wIndex);
			}

			/* Make sure the controller isn't busy. */
			while (*R_USB_COMMAND & IO_MASK(R_USB_COMMAND, busy));

			/* If all enabled ports were disabled the host controller goes down into
			   started mode, so we need to bring it back into the running state.
			   (This is safe even if it's already in the running state.) */
			*R_USB_COMMAND =
				IO_STATE(R_USB_COMMAND, port_sel, nop) |
				IO_STATE(R_USB_COMMAND, port_cmd, reset) |
				IO_STATE(R_USB_COMMAND, ctrl_cmd, host_run);

			dbg_rh("...Done");
			OK(0);

		case (RH_PORT_POWER):
			OK (0);	/* port power ** */
		case (RH_PORT_ENABLE):
			/* There is no port enable command in the host controller, so if the
			   port is already enabled, we do nothing. If not, we reset the port
			   (with an ugly goto). */

			if (wIndex == 1) {
				if (hc->rh.prev_wPortStatus_1 &
				    IO_STATE(R_USB_RH_PORT_STATUS_1, enabled, no)) {
					goto port_1_reset;
				}
			} else if (wIndex == 2) {
				if (hc->rh.prev_wPortStatus_2 &
				    IO_STATE(R_USB_RH_PORT_STATUS_2, enabled, no)) {
					goto port_2_reset;
				}
			} else {
				dbg_rh("RH_SET_FEATURE->RH_GET_STATUS with invalid wIndex = %d", wIndex);
			}
			OK (0);
		}
		break;

	case RH_SET_ADDRESS:
		hc->rh.devnum = wValue;
		dbg_rh("RH address set to: %d", hc->rh.devnum);
		OK (0);

	case RH_GET_DESCRIPTOR:
		switch ((wValue & 0xff00) >> 8) {
		case (0x01):	/* device descriptor */
			len = min_t(unsigned int, leni, min_t(unsigned int, sizeof (root_hub_dev_des), wLength));
			memcpy (data, root_hub_dev_des, len);
			OK (len);
		case (0x02):	/* configuration descriptor */
			len = min_t(unsigned int, leni, min_t(unsigned int, sizeof (root_hub_config_des), wLength));
			memcpy (data, root_hub_config_des, len);
			OK (len);
		case (0x03):	/* string descriptors */
			len = usb_root_hub_string (wValue & 0xff,
						   0xff, "ETRAX 100LX",
						   data, wLength);
			if (len > 0) {
				OK(min(leni, len));
			} else {
				stat = -EPIPE;
			}

		}
		break;

	case RH_GET_DESCRIPTOR | RH_CLASS:
		root_hub_hub_des[2] = hc->rh.numports;
		len = min_t(unsigned int, leni, min_t(unsigned int, sizeof (root_hub_hub_des), wLength));
		memcpy (data, root_hub_hub_des, len);
		OK (len);

	case RH_GET_CONFIGURATION:
		*(__u8 *) data = 0x01;
		OK (1);

	case RH_SET_CONFIGURATION:
		OK (0);

	default:
		stat = -EPIPE;
	}

	urb->actual_length = len;
	urb->status = stat;
	urb->dev = NULL;
	if (urb->complete) {
		urb->complete(urb, NULL);
	}
	DBFEXIT;

	return 0;
}

static void
etrax_usb_bulk_eot_timer_func(unsigned long dummy)
{
	/* Because of a race condition in the top half, we might miss a bulk eot.
	   This timer "simulates" a bulk eot if we don't get one for a while, hopefully
	   correcting the situation. */
	dbg_bulk("bulk_eot_timer timed out.");
	etrax_usb_hc_bulk_eot_interrupt(1);
}

static void*
etrax_usb_buffer_alloc(struct usb_bus* bus, size_t size,
	unsigned mem_flags, dma_addr_t *dma)
{
  return kmalloc(size, mem_flags);
}

static void
etrax_usb_buffer_free(struct usb_bus *bus, size_t size, void *addr, dma_addr_t dma)
{
  kfree(addr);
}


static struct device fake_device;

static int __init etrax_usb_hc_init(void)
{
	static etrax_hc_t *hc;
	struct usb_bus *bus;
	struct usb_device *usb_rh;
	int i;

	DBFENTER;

	info("ETRAX 100LX USB-HCD %s (c) 2001-2003 Axis Communications AB\n", usb_hcd_version);

 	hc = kmalloc(sizeof(etrax_hc_t), GFP_KERNEL);
	assert(hc != NULL);

	/* We use kmem_cache_* to make sure that all DMA desc. are dword aligned */
	/* Note that we specify sizeof(USB_EP_Desc_t) as the size, but also allocate
	   SB descriptors from this cache. This is ok since sizeof(USB_EP_Desc_t) ==
	   sizeof(USB_SB_Desc_t). */

	usb_desc_cache = kmem_cache_create("usb_desc_cache", sizeof(USB_EP_Desc_t), 0,
					   SLAB_HWCACHE_ALIGN, 0, 0);
	assert(usb_desc_cache != NULL);

	top_half_reg_cache = kmem_cache_create("top_half_reg_cache",
					       sizeof(usb_interrupt_registers_t),
					       0, SLAB_HWCACHE_ALIGN, 0, 0);
	assert(top_half_reg_cache != NULL);

	isoc_compl_cache = kmem_cache_create("isoc_compl_cache",
						sizeof(usb_isoc_complete_data_t),
						0, SLAB_HWCACHE_ALIGN, 0, 0);
	assert(isoc_compl_cache != NULL);

	etrax_usb_bus = bus = usb_alloc_bus(&etrax_usb_device_operations);
	hc->bus = bus;
	bus->bus_name="ETRAX 100LX";
	bus->hcpriv = hc;

	/* Initialize RH to the default address.
	   And make sure that we have no status change indication */
	hc->rh.numports = 2;  /* The RH has two ports */
	hc->rh.devnum = 1;
	hc->rh.wPortChange_1 = 0;
	hc->rh.wPortChange_2 = 0;

	/* Also initate the previous values to zero */
	hc->rh.prev_wPortStatus_1 = 0;
	hc->rh.prev_wPortStatus_2 = 0;

	/* Initialize the intr-traffic flags */
	/* FIXME: This isn't used. (Besides, the error field isn't initialized.) */
	hc->intr.sleeping = 0;
	hc->intr.wq = NULL;

	epid_usage_bitmask = 0;
	epid_out_traffic = 0;

	/* Mark the invalid epid as being used. */
	set_bit(INVALID_EPID, (void *)&epid_usage_bitmask);
	*R_USB_EPT_INDEX = IO_FIELD(R_USB_EPT_INDEX, value, INVALID_EPID);
	nop();
	/* The valid bit should still be set ('invalid' is in our world; not the hardware's). */
	*R_USB_EPT_DATA = (IO_STATE(R_USB_EPT_DATA, valid, yes) |
			   IO_FIELD(R_USB_EPT_DATA, max_len, 1));

	/* Mark the dummy epid as being used. */
	set_bit(DUMMY_EPID, (void *)&epid_usage_bitmask);
	*R_USB_EPT_INDEX = IO_FIELD(R_USB_EPT_INDEX, value, DUMMY_EPID);
	nop();
	*R_USB_EPT_DATA = (IO_STATE(R_USB_EPT_DATA, valid, no) |
			   IO_FIELD(R_USB_EPT_DATA, max_len, 1));

	/* Initialize the urb list by initiating a head for each list. */
	for (i = 0; i < NBR_OF_EPIDS; i++) {
		INIT_LIST_HEAD(&urb_list[i]);
	}
	spin_lock_init(&urb_list_lock);

	INIT_LIST_HEAD(&urb_unlink_list);


	/* Initiate the bulk start timer. */
	init_timer(&bulk_start_timer);
	bulk_start_timer.expires = jiffies + BULK_START_TIMER_INTERVAL;
	bulk_start_timer.function = etrax_usb_bulk_start_timer_func;
	add_timer(&bulk_start_timer);


	/* Initiate the bulk eot timer. */
	init_timer(&bulk_eot_timer);
	bulk_eot_timer.expires = jiffies + BULK_EOT_TIMER_INTERVAL;
	bulk_eot_timer.function = etrax_usb_bulk_eot_timer_func;
	add_timer(&bulk_eot_timer);

	/* Set up the data structures for USB traffic. Note that this must be done before
	   any interrupt that relies on sane DMA list occurrs. */
	init_rx_buffers();
	init_tx_bulk_ep();
	init_tx_ctrl_ep();
	init_tx_intr_ep();
	init_tx_isoc_ep();

        device_initialize(&fake_device);
        kobject_set_name(&fake_device.kobj, "etrax_usb");
        kobject_add(&fake_device.kobj);
	kobject_uevent(&fake_device.kobj, KOBJ_ADD);
        hc->bus->controller = &fake_device;
	usb_register_bus(hc->bus);

	*R_IRQ_MASK2_SET =
		/* Note that these interrupts are not used. */
		IO_STATE(R_IRQ_MASK2_SET, dma8_sub0_descr, set) |
		/* Sub channel 1 (ctrl) descr. interrupts are used. */
		IO_STATE(R_IRQ_MASK2_SET, dma8_sub1_descr, set) |
		IO_STATE(R_IRQ_MASK2_SET, dma8_sub2_descr, set) |
		/* Sub channel 3 (isoc) descr. interrupts are used. */
		IO_STATE(R_IRQ_MASK2_SET, dma8_sub3_descr, set);

	/* Note that the dma9_descr interrupt is not used. */
	*R_IRQ_MASK2_SET =
		IO_STATE(R_IRQ_MASK2_SET, dma9_eop, set) |
		IO_STATE(R_IRQ_MASK2_SET, dma9_descr, set);

	/* FIXME: Enable iso_eof only when isoc traffic is running. */
	*R_USB_IRQ_MASK_SET =
		IO_STATE(R_USB_IRQ_MASK_SET, iso_eof, set) |
		IO_STATE(R_USB_IRQ_MASK_SET, bulk_eot, set) |
		IO_STATE(R_USB_IRQ_MASK_SET, epid_attn, set) |
		IO_STATE(R_USB_IRQ_MASK_SET, port_status, set) |
		IO_STATE(R_USB_IRQ_MASK_SET, ctl_status, set);


	if (request_irq(ETRAX_USB_HC_IRQ, etrax_usb_hc_interrupt_top_half, 0,
			"ETRAX 100LX built-in USB (HC)", hc)) {
		err("Could not allocate IRQ %d for USB", ETRAX_USB_HC_IRQ);
		etrax_usb_hc_cleanup();
		DBFEXIT;
		return -1;
	}

	if (request_irq(ETRAX_USB_RX_IRQ, etrax_usb_rx_interrupt, 0,
			"ETRAX 100LX built-in USB (Rx)", hc)) {
		err("Could not allocate IRQ %d for USB", ETRAX_USB_RX_IRQ);
		etrax_usb_hc_cleanup();
		DBFEXIT;
		return -1;
	}

	if (request_irq(ETRAX_USB_TX_IRQ, etrax_usb_tx_interrupt, 0,
			"ETRAX 100LX built-in USB (Tx)", hc)) {
		err("Could not allocate IRQ %d for USB", ETRAX_USB_TX_IRQ);
		etrax_usb_hc_cleanup();
		DBFEXIT;
		return -1;
	}

	/* R_USB_COMMAND:
	   USB commands in host mode. The fields in this register should all be
	   written to in one write. Do not read-modify-write one field at a time. A
	   write to this register will trigger events in the USB controller and an
	   incomplete command may lead to unpredictable results, and in worst case
	   even to a deadlock in the controller.
	   (Note however that the busy field is read-only, so no need to write to it.) */

	/* Check the busy bit before writing to R_USB_COMMAND. */

	while (*R_USB_COMMAND & IO_MASK(R_USB_COMMAND, busy));

	/* Reset the USB interface. */
	*R_USB_COMMAND =
		IO_STATE(R_USB_COMMAND, port_sel, nop) |
		IO_STATE(R_USB_COMMAND, port_cmd, reset) |
		IO_STATE(R_USB_COMMAND, ctrl_cmd, reset);

	/* Designer's Reference, p. 8 - 10 says we should Initate R_USB_FM_PSTART to 0x2A30 (10800),
	   to guarantee that control traffic gets 10% of the bandwidth, and periodic transfer may
	   allocate the rest (90%). This doesn't work though. Read on for a lenghty explanation.

	   While there is a difference between rev. 2 and rev. 3 of the ETRAX 100LX regarding the NAK
	   behaviour, it doesn't solve this problem. What happens is that a control transfer will not
	   be interrupted in its data stage when PSTART happens (the point at which periodic traffic
	   is started). Thus, if PSTART is set to 10800 and its IN or OUT token is NAKed until just before
	   PSTART happens, it will continue the IN/OUT transfer as long as it's ACKed. After it's done,
	   there may be too little time left for an isochronous transfer, causing an epid attention
	   interrupt due to perror. The work-around for this is to let the control transfers run at the
	   end of the frame instead of at the beginning, and will be interrupted just fine if it doesn't
	   fit into the frame. However, since there will *always* be a control transfer at the beginning
	   of the frame, regardless of what we set PSTART to, that transfer might be a 64-byte transfer
	   which consumes up to 15% of the frame, leaving only 85% for periodic traffic. The solution to
	   this would be to 'dummy allocate' 5% of the frame with the usb_claim_bandwidth function to make
	   sure that the periodic transfers that are inserted will always fit in the frame.

	   The idea was suggested that a control transfer could be split up into several 8 byte transfers,
	   so that it would be interrupted by PSTART, but since this can't be done for an IN transfer this
	   hasn't been implemented.

	   The value 11960 is chosen to be just after the SOF token, with a couple of bit times extra
	   for possible bit stuffing. */

	*R_USB_FM_PSTART = IO_FIELD(R_USB_FM_PSTART, value, 11960);

#ifdef CONFIG_ETRAX_USB_HOST_PORT1
	*R_USB_PORT1_DISABLE = IO_STATE(R_USB_PORT1_DISABLE, disable, no);
#endif

#ifdef CONFIG_ETRAX_USB_HOST_PORT2
	*R_USB_PORT2_DISABLE = IO_STATE(R_USB_PORT2_DISABLE, disable, no);
#endif

	while (*R_USB_COMMAND & IO_MASK(R_USB_COMMAND, busy));

	/* Configure the USB interface as a host controller. */
	*R_USB_COMMAND =
		IO_STATE(R_USB_COMMAND, port_sel, nop) |
		IO_STATE(R_USB_COMMAND, port_cmd, reset) |
		IO_STATE(R_USB_COMMAND, ctrl_cmd, host_config);

	/* Note: Do not reset any ports here. Await the port status interrupts, to have a controlled
	   sequence of resetting the ports. If we reset both ports now, and there are devices
	   on both ports, we will get a bus error because both devices will answer the set address
	   request. */

	while (*R_USB_COMMAND & IO_MASK(R_USB_COMMAND, busy));

	/* Start processing of USB traffic. */
	*R_USB_COMMAND =
		IO_STATE(R_USB_COMMAND, port_sel, nop) |
		IO_STATE(R_USB_COMMAND, port_cmd, reset) |
		IO_STATE(R_USB_COMMAND, ctrl_cmd, host_run);

	while (*R_USB_COMMAND & IO_MASK(R_USB_COMMAND, busy));

	usb_rh = usb_alloc_dev(NULL, hc->bus, 0);
	hc->bus->root_hub = usb_rh;
        usb_rh->state = USB_STATE_ADDRESS;
        usb_rh->speed = USB_SPEED_FULL;
        usb_rh->devnum = 1;
        hc->bus->devnum_next = 2;
        usb_rh->ep0.desc.wMaxPacketSize = __const_cpu_to_le16(64);
        usb_get_device_descriptor(usb_rh, USB_DT_DEVICE_SIZE);
	usb_new_device(usb_rh);

	DBFEXIT;

	return 0;
}

static void etrax_usb_hc_cleanup(void)
{
	DBFENTER;

	free_irq(ETRAX_USB_HC_IRQ, NULL);
	free_irq(ETRAX_USB_RX_IRQ, NULL);
	free_irq(ETRAX_USB_TX_IRQ, NULL);

	usb_deregister_bus(etrax_usb_bus);

	/* FIXME: call kmem_cache_destroy here? */

	DBFEXIT;
}

module_init(etrax_usb_hc_init);
module_exit(etrax_usb_hc_cleanup);

#ifndef __LINUX_ETRAX_USB_H
#define __LINUX_ETRAX_USB_H

#include <linux/types.h>
#include <linux/list.h>

typedef struct USB_IN_Desc {
	volatile __u16 sw_len;
	volatile __u16 command;
	volatile unsigned long next;
	volatile unsigned long buf;
	volatile __u16 hw_len;
	volatile __u16 status;
} USB_IN_Desc_t;

typedef struct USB_SB_Desc {
	volatile __u16 sw_len;
	volatile __u16 command;
	volatile unsigned long next;
	volatile unsigned long buf;
	__u32 dummy;
} USB_SB_Desc_t;

typedef struct USB_EP_Desc {
	volatile __u16 hw_len;
	volatile __u16 command;
	volatile unsigned long sub;
	volatile unsigned long next;
	__u32 dummy;
} USB_EP_Desc_t;

struct virt_root_hub {
	int devnum;
	void *urb;
	void *int_addr;
	int send;
	int interval;
	int numports;
	struct timer_list rh_int_timer;
	volatile __u16 wPortChange_1;
	volatile __u16 wPortChange_2;
	volatile __u16 prev_wPortStatus_1;
	volatile __u16 prev_wPortStatus_2;
};

struct etrax_usb_intr_traffic {
	int sleeping;
	int error;
	struct wait_queue *wq;
};

typedef struct etrax_usb_hc {
	struct usb_bus *bus;
	struct virt_root_hub rh;
	struct etrax_usb_intr_traffic intr;
} etrax_hc_t;

typedef enum {
	STARTED,
	NOT_STARTED,
	UNLINK,
	TRANSFER_DONE,
	WAITING_FOR_DESCR_INTR
} etrax_usb_urb_state_t;



typedef struct etrax_usb_urb_priv {
	/* The first_sb field is used for freeing all SB descriptors belonging
	   to an urb. The corresponding ep descriptor's sub pointer cannot be
	   used for this since the DMA advances the sub pointer as it processes
	   the sb list. */
	USB_SB_Desc_t *first_sb;
	/* The last_sb field referes to the last SB descriptor that belongs to
	   this urb. This is important to know so we can free the SB descriptors
	   that ranges between first_sb and last_sb. */
	USB_SB_Desc_t *last_sb;

	/* The rx_offset field is used in ctrl and bulk traffic to keep track
	   of the offset in the urb's transfer_buffer where incoming data should be
	   copied to. */
	__u32 rx_offset;

	/* Counter used in isochronous transfers to keep track of the
	   number of packets received/transmitted.  */
	__u32 isoc_packet_counter;

	/* This field is used to pass information about the urb's current state between
	   the various interrupt handlers (thus marked volatile). */
	volatile etrax_usb_urb_state_t urb_state;

	/* Connection between the submitted urb and ETRAX epid number */
	__u8 epid;

	/* The rx_data_list field is used for periodic traffic, to hold
	   received data for later processing in the the complete_urb functions,
	   where the data us copied to the urb's transfer_buffer. Basically, we
	   use this intermediate storage because we don't know when it's safe to
	   reuse the transfer_buffer (FIXME?). */
	struct list_head rx_data_list;
} etrax_urb_priv_t;

/* This struct is for passing data from the top half to the bottom half. */
typedef struct usb_interrupt_registers
{
	etrax_hc_t *hc;
	__u32 r_usb_epid_attn;
	__u8 r_usb_status;
	__u16 r_usb_rh_port_status_1;
	__u16 r_usb_rh_port_status_2;
	__u32 r_usb_irq_mask_read;
	__u32 r_usb_fm_number;
	struct work_struct usb_bh;
} usb_interrupt_registers_t;

/* This struct is for passing data from the isoc top half to the isoc bottom half. */
typedef struct usb_isoc_complete_data
{
	struct urb *urb;
	struct work_struct usb_bh;
} usb_isoc_complete_data_t;

/* This struct holds data we get from the rx descriptors for DMA channel 9
   for periodic traffic (intr and isoc). */
typedef struct rx_data
{
	void *data;
	int length;
	struct list_head list;
} rx_data_t;

typedef struct urb_entry
{
	struct urb *urb;
	struct list_head list;
} urb_entry_t;

/* ---------------------------------------------------------------------------
   Virtual Root HUB
   ------------------------------------------------------------------------- */
/* destination of request */
#define RH_INTERFACE               0x01
#define RH_ENDPOINT                0x02
#define RH_OTHER                   0x03

#define RH_CLASS                   0x20
#define RH_VENDOR                  0x40

/* Requests: bRequest << 8 | bmRequestType */
#define RH_GET_STATUS           0x0080
#define RH_CLEAR_FEATURE        0x0100
#define RH_SET_FEATURE          0x0300
#define RH_SET_ADDRESS		0x0500
#define RH_GET_DESCRIPTOR	0x0680
#define RH_SET_DESCRIPTOR       0x0700
#define RH_GET_CONFIGURATION	0x0880
#define RH_SET_CONFIGURATION	0x0900
#define RH_GET_STATE            0x0280
#define RH_GET_INTERFACE        0x0A80
#define RH_SET_INTERFACE        0x0B00
#define RH_SYNC_FRAME           0x0C80
/* Our Vendor Specific Request */
#define RH_SET_EP               0x2000


/* Hub port features */
#define RH_PORT_CONNECTION         0x00
#define RH_PORT_ENABLE             0x01
#define RH_PORT_SUSPEND            0x02
#define RH_PORT_OVER_CURRENT       0x03
#define RH_PORT_RESET              0x04
#define RH_PORT_POWER              0x08
#define RH_PORT_LOW_SPEED          0x09
#define RH_C_PORT_CONNECTION       0x10
#define RH_C_PORT_ENABLE           0x11
#define RH_C_PORT_SUSPEND          0x12
#define RH_C_PORT_OVER_CURRENT     0x13
#define RH_C_PORT_RESET            0x14

/* Hub features */
#define RH_C_HUB_LOCAL_POWER       0x00
#define RH_C_HUB_OVER_CURRENT      0x01

#define RH_DEVICE_REMOTE_WAKEUP    0x00
#define RH_ENDPOINT_STALL          0x01

/* Our Vendor Specific feature */
#define RH_REMOVE_EP               0x00


#define RH_ACK                     0x01
#define RH_REQ_ERR                 -1
#define RH_NACK                    0x00

/* Field definitions for */

#define USB_IN_command__eol__BITNR      0 /* command macros */
#define USB_IN_command__eol__WIDTH      1
#define USB_IN_command__eol__no         0
#define USB_IN_command__eol__yes        1

#define USB_IN_command__intr__BITNR     3
#define USB_IN_command__intr__WIDTH     1
#define USB_IN_command__intr__no        0
#define USB_IN_command__intr__yes       1

#define USB_IN_status__eop__BITNR       1 /* status macros. */
#define USB_IN_status__eop__WIDTH       1
#define USB_IN_status__eop__no          0
#define USB_IN_status__eop__yes         1

#define USB_IN_status__eot__BITNR       5
#define USB_IN_status__eot__WIDTH       1
#define USB_IN_status__eot__no          0
#define USB_IN_status__eot__yes         1

#define USB_IN_status__error__BITNR     6
#define USB_IN_status__error__WIDTH     1
#define USB_IN_status__error__no        0
#define USB_IN_status__error__yes       1

#define USB_IN_status__nodata__BITNR    7
#define USB_IN_status__nodata__WIDTH    1
#define USB_IN_status__nodata__no       0
#define USB_IN_status__nodata__yes      1

#define USB_IN_status__epid__BITNR      8
#define USB_IN_status__epid__WIDTH      5

#define USB_EP_command__eol__BITNR      0
#define USB_EP_command__eol__WIDTH      1
#define USB_EP_command__eol__no         0
#define USB_EP_command__eol__yes        1

#define USB_EP_command__eof__BITNR      1
#define USB_EP_command__eof__WIDTH      1
#define USB_EP_command__eof__no         0
#define USB_EP_command__eof__yes        1

#define USB_EP_command__intr__BITNR     3
#define USB_EP_command__intr__WIDTH     1
#define USB_EP_command__intr__no        0
#define USB_EP_command__intr__yes       1

#define USB_EP_command__enable__BITNR   4
#define USB_EP_command__enable__WIDTH   1
#define USB_EP_command__enable__no      0
#define USB_EP_command__enable__yes     1

#define USB_EP_command__hw_valid__BITNR 5
#define USB_EP_command__hw_valid__WIDTH 1
#define USB_EP_command__hw_valid__no    0
#define USB_EP_command__hw_valid__yes   1

#define USB_EP_command__epid__BITNR     8
#define USB_EP_command__epid__WIDTH     5

#define USB_SB_command__eol__BITNR      0 /* command macros. */
#define USB_SB_command__eol__WIDTH      1
#define USB_SB_command__eol__no         0
#define USB_SB_command__eol__yes        1

#define USB_SB_command__eot__BITNR      1
#define USB_SB_command__eot__WIDTH      1
#define USB_SB_command__eot__no         0
#define USB_SB_command__eot__yes        1

#define USB_SB_command__intr__BITNR     3
#define USB_SB_command__intr__WIDTH     1
#define USB_SB_command__intr__no        0
#define USB_SB_command__intr__yes       1

#define USB_SB_command__tt__BITNR       4
#define USB_SB_command__tt__WIDTH       2
#define USB_SB_command__tt__zout        0
#define USB_SB_command__tt__in          1
#define USB_SB_command__tt__out         2
#define USB_SB_command__tt__setup       3


#define USB_SB_command__rem__BITNR      8
#define USB_SB_command__rem__WIDTH      6

#define USB_SB_command__full__BITNR     6
#define USB_SB_command__full__WIDTH     1
#define USB_SB_command__full__no        0
#define USB_SB_command__full__yes       1

#endif

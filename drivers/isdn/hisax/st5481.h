/*
 * Driver for ST5481 USB ISDN modem
 *
 * Author       Frode Isaksen
 * Copyright    2001 by Frode Isaksen      <fisaksen@bewan.com>
 *              2001 by Kai Germaschewski  <kai.germaschewski@gmx.de>
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#ifndef _ST5481_H_
#define _ST5481_H_


// USB IDs, the Product Id is in the range 0x4810-0x481F

#define ST_VENDOR_ID 0x0483
#define ST5481_PRODUCT_ID 0x4810
#define ST5481_PRODUCT_ID_MASK 0xFFF0

// ST5481 endpoints when using alternative setting 3 (2B+D).
// To get the endpoint address, OR with 0x80 for IN endpoints.

#define EP_CTRL   0x00U /* Control endpoint */
#define EP_INT    0x01U /* Interrupt endpoint */
#define EP_B1_OUT 0x02U /* B1 channel out */
#define EP_B1_IN  0x03U /* B1 channel in */
#define EP_B2_OUT 0x04U /* B2 channel out */
#define EP_B2_IN  0x05U /* B2 channel in */
#define EP_D_OUT  0x06U /* D channel out */
#define EP_D_IN   0x07U /* D channel in */
  
// Number of isochronous packets. With 20 packets we get
// 50 interrupts/sec for each endpoint.

#define NUM_ISO_PACKETS_D      20
#define NUM_ISO_PACKETS_B      20

// Size of each isochronous packet.
// In outgoing direction we need to match ISDN data rates:
// D:  2 bytes / msec -> 16 kbit / s
// B: 16 bytes / msec -> 64 kbit / s
#define SIZE_ISO_PACKETS_D_IN  16
#define SIZE_ISO_PACKETS_D_OUT 2
#define SIZE_ISO_PACKETS_B_IN  32
#define SIZE_ISO_PACKETS_B_OUT 8

// If we overrun/underrun, we send one packet with +/- 2 bytes
#define B_FLOW_ADJUST 2

// Registers that are written using vendor specific device request
// on endpoint 0. 

#define LBA			0x02 /* S loopback */
#define SET_DEFAULT		0x06 /* Soft reset */
#define LBB			0x1D /* S maintenance loopback */
#define STT			0x1e /* S force transmission signals */
#define SDA_MIN			0x20 /* SDA-sin minimal value */
#define SDA_MAX			0x21 /* SDA-sin maximal value */
#define SDELAY_VALUE		0x22 /* Delay between Tx and Rx clock */
#define IN_D_COUNTER		0x36 /* D receive channel fifo counter */
#define OUT_D_COUNTER		0x37 /* D transmit channel fifo counter */
#define IN_B1_COUNTER		0x38 /* B1 receive channel fifo counter */
#define OUT_B1_COUNTER		0x39 /* B1 transmit channel fifo counter */
#define IN_B2_COUNTER		0x3a /* B2 receive channel fifo counter */
#define OUT_B2_COUNTER		0x3b /* B2 transmit channel fifo counter */
#define FFCTRL_IN_D		0x3C /* D receive channel fifo threshold low */
#define FFCTRH_IN_D		0x3D /* D receive channel fifo threshold high */
#define FFCTRL_OUT_D		0x3E /* D transmit channel fifo threshold low */
#define FFCTRH_OUT_D		0x3F /* D transmit channel fifo threshold high */
#define FFCTRL_IN_B1		0x40 /* B1 receive channel fifo threshold low */
#define FFCTRH_IN_B1		0x41 /* B1 receive channel fifo threshold high */
#define FFCTRL_OUT_B1		0x42 /* B1 transmit channel fifo threshold low */
#define FFCTRH_OUT_B1		0x43 /* B1 transmit channel fifo threshold high */
#define FFCTRL_IN_B2		0x44 /* B2 receive channel fifo threshold low */
#define FFCTRH_IN_B2		0x45 /* B2 receive channel fifo threshold high */
#define FFCTRL_OUT_B2		0x46 /* B2 transmit channel fifo threshold low */
#define FFCTRH_OUT_B2		0x47 /* B2 transmit channel fifo threshold high */
#define MPMSK			0x4A /* Multi purpose interrupt MASK register */
#define	FFMSK_D			0x4c /* D fifo interrupt MASK register */
#define	FFMSK_B1		0x4e /* B1 fifo interrupt MASK register */
#define	FFMSK_B2		0x50 /* B2 fifo interrupt MASK register */
#define GPIO_DIR		0x52 /* GPIO pins direction registers */
#define GPIO_OUT		0x53 /* GPIO pins output register */
#define GPIO_IN			0x54 /* GPIO pins input register */ 
#define TXCI			0x56 /* CI command to be transmitted */


// Format of the interrupt packet received on endpoint 1:
//
// +--------+--------+--------+--------+--------+--------+
// !MPINT   !FFINT_D !FFINT_B1!FFINT_B2!CCIST   !GPIO_INT!
// +--------+--------+--------+--------+--------+--------+

// Offsets in the interrupt packet

#define MPINT			0
#define FFINT_D			1
#define FFINT_B1		2
#define FFINT_B2		3
#define CCIST			4
#define GPIO_INT		5
#define INT_PKT_SIZE            6

// MPINT
#define LSD_INT                 0x80 /* S line activity detected */
#define RXCI_INT		0x40 /* Indicate primitive arrived */
#define	DEN_INT			0x20 /* Signal enabling data out of D Tx fifo */
#define DCOLL_INT		0x10 /* D channel collision */
#define AMIVN_INT		0x04 /* AMI violation number reached 2 */
#define INFOI_INT		0x04 /* INFOi changed */
#define DRXON_INT               0x02 /* Reception channel active */
#define GPCHG_INT               0x01 /* GPIO pin value changed */

// FFINT_x
#define IN_OVERRUN		0x80 /* In fifo overrun */
#define OUT_UNDERRUN		0x40 /* Out fifo underrun */
#define IN_UP			0x20 /* In fifo thresholdh up-crossed */
#define IN_DOWN			0x10 /* In fifo thresholdl down-crossed */
#define OUT_UP			0x08 /* Out fifo thresholdh up-crossed */
#define OUT_DOWN		0x04 /* Out fifo thresholdl down-crossed */
#define IN_COUNTER_ZEROED	0x02 /* In down-counter reached 0 */
#define OUT_COUNTER_ZEROED	0x01 /* Out down-counter reached 0 */

#define ANY_REC_INT	(IN_OVERRUN+IN_UP+IN_DOWN+IN_COUNTER_ZEROED)
#define ANY_XMIT_INT	(OUT_UNDERRUN+OUT_UP+OUT_DOWN+OUT_COUNTER_ZEROED)


// Level 1 commands that are sent using the TXCI device request
#define ST5481_CMD_DR		 0x0 /* Deactivation Request */
#define ST5481_CMD_RES		 0x1 /* state machine RESet */
#define ST5481_CMD_TM1		 0x2 /* Test Mode 1 */
#define ST5481_CMD_TM2		 0x3 /* Test Mode 2 */
#define ST5481_CMD_PUP		 0x7 /* Power UP */
#define ST5481_CMD_AR8		 0x8 /* Activation Request class 1 */
#define ST5481_CMD_AR10		 0x9 /* Activation Request class 2 */
#define ST5481_CMD_ARL		 0xA /* Activation Request Loopback */
#define ST5481_CMD_PDN		 0xF /* Power DoWn */

// Turn on/off the LEDs using the GPIO device request.
// To use the B LEDs, number_of_leds must be set to 4
#define B1_LED		0x10U
#define B2_LED		0x20U
#define GREEN_LED	0x40U
#define RED_LED	        0x80U

// D channel out states
enum {
	ST_DOUT_NONE,

	ST_DOUT_SHORT_INIT,
	ST_DOUT_SHORT_WAIT_DEN,

	ST_DOUT_LONG_INIT,
	ST_DOUT_LONG_WAIT_DEN,
	ST_DOUT_NORMAL,

	ST_DOUT_WAIT_FOR_UNDERRUN,
        ST_DOUT_WAIT_FOR_NOT_BUSY,
	ST_DOUT_WAIT_FOR_STOP,
	ST_DOUT_WAIT_FOR_RESET,
};

#define DOUT_STATE_COUNT (ST_DOUT_WAIT_FOR_RESET + 1)

// D channel out events
enum {
	EV_DOUT_START_XMIT,
	EV_DOUT_COMPLETE,
	EV_DOUT_DEN,
	EV_DOUT_RESETED,
	EV_DOUT_STOPPED,
	EV_DOUT_COLL,
	EV_DOUT_UNDERRUN,
};

#define DOUT_EVENT_COUNT (EV_DOUT_UNDERRUN + 1)

// ----------------------------------------------------------------------

enum {
	ST_L1_F3,
	ST_L1_F4,
	ST_L1_F6,
	ST_L1_F7,
	ST_L1_F8,
};

#define L1_STATE_COUNT (ST_L1_F8+1)

// The first 16 entries match the Level 1 indications that 
// are found at offset 4 (CCIST) in the interrupt packet

enum {
	EV_IND_DP,  // 0000 Deactivation Pending
	EV_IND_1,   // 0001
	EV_IND_2,   // 0010
	EV_IND_3,   // 0011
	EV_IND_RSY, // 0100 ReSYnchronizing
	EV_IND_5,   // 0101
	EV_IND_6,   // 0110
	EV_IND_7,   // 0111
	EV_IND_AP,  // 1000 Activation Pending
	EV_IND_9,   // 1001
	EV_IND_10,  // 1010
	EV_IND_11,  // 1011
	EV_IND_AI8, // 1100 Activation Indication class 8
	EV_IND_AI10,// 1101 Activation Indication class 10
	EV_IND_AIL, // 1110 Activation Indication Loopback
	EV_IND_DI,  // 1111 Deactivation Indication
	EV_PH_ACTIVATE_REQ,
	EV_PH_DEACTIVATE_REQ,
	EV_TIMER3,
};

#define L1_EVENT_COUNT (EV_TIMER3 + 1)

#define ERR(format, arg...) \
printk(KERN_ERR "%s:%s: " format "\n" , __FILE__,  __func__ , ## arg)

#define WARNING(format, arg...) \
printk(KERN_WARNING "%s:%s: " format "\n" , __FILE__,  __func__ , ## arg)

#define INFO(format, arg...) \
printk(KERN_INFO "%s:%s: " format "\n" , __FILE__,  __func__ , ## arg)

#include "isdnhdlc.h"
#include "fsm.h"
#include "hisax_if.h"
#include <linux/skbuff.h>

/* ======================================================================
 * FIFO handling
 */

/* Generic FIFO structure */
struct fifo {
	u_char r,w,count,size;
	spinlock_t lock;
};

/*
 * Init an FIFO
 */
static inline void fifo_init(struct fifo *fifo, int size)
{
	fifo->r = fifo->w = fifo->count = 0;
	fifo->size = size;
	spin_lock_init(&fifo->lock);
}

/*
 * Add an entry to the FIFO
 */
static inline int fifo_add(struct fifo *fifo)
{
	unsigned long flags;
	int index;

	if (!fifo) {
		return -1;
	}

	spin_lock_irqsave(&fifo->lock, flags);
	if (fifo->count == fifo->size) {
		// FIFO full
		index = -1;
	} else {
		// Return index where to get the next data to add to the FIFO
		index = fifo->w++ & (fifo->size-1);
		fifo->count++;
	}
	spin_unlock_irqrestore(&fifo->lock, flags);
	return index;
}

/*
 * Remove an entry from the FIFO with the index returned.
 */
static inline int fifo_remove(struct fifo *fifo)
{
	unsigned long flags;
	int index;

	if (!fifo) {
		return -1;
	}

	spin_lock_irqsave(&fifo->lock, flags);
	if (!fifo->count) {
		// FIFO empty
		index = -1;
	} else {
		// Return index where to get the next data from the FIFO
		index = fifo->r++ & (fifo->size-1);
		fifo->count--;
	}
	spin_unlock_irqrestore(&fifo->lock, flags);

	return index;
}

/* ======================================================================
 * control pipe
 */
typedef void (*ctrl_complete_t)(void *);

typedef struct ctrl_msg {
	struct usb_ctrlrequest dr;
	ctrl_complete_t complete;
	void *context;
} ctrl_msg; 

/* FIFO of ctrl messages waiting to be sent */
#define MAX_EP0_MSG 16
struct ctrl_msg_fifo {
	struct fifo f;
	struct ctrl_msg data[MAX_EP0_MSG];
};	

#define MAX_DFRAME_LEN_L1	300
#define HSCX_BUFMAX	4096

struct st5481_ctrl {
	struct ctrl_msg_fifo msg_fifo;
	unsigned long busy;
	struct urb *urb;
};

struct st5481_intr {
  //	struct evt_fifo evt_fifo;
	struct urb *urb;
};

struct st5481_d_out {
	struct isdnhdlc_vars hdlc_state;
	struct urb *urb[2]; /* double buffering */
	unsigned long busy;
	struct sk_buff *tx_skb;
	struct FsmInst fsm;
};

struct st5481_b_out {
	struct isdnhdlc_vars hdlc_state;
	struct urb *urb[2]; /* double buffering */
	u_char flow_event;
	u_long busy;
	struct sk_buff *tx_skb;
};

struct st5481_in {
	struct isdnhdlc_vars hdlc_state;
	struct urb *urb[2]; /* double buffering */
	int mode;
	int bufsize;
	unsigned int num_packets;
	unsigned int packet_size;
	unsigned char ep, counter;
	unsigned char *rcvbuf;
	struct st5481_adapter *adapter;
	struct hisax_if *hisax_if;
};

int st5481_setup_in(struct st5481_in *in);
void st5481_release_in(struct st5481_in *in);
void st5481_in_mode(struct st5481_in *in, int mode);

struct st5481_bcs {
	struct hisax_b_if b_if;
	struct st5481_adapter *adapter;
	struct st5481_in b_in;
	struct st5481_b_out b_out;
	int channel;
	int mode;
};

struct st5481_adapter {
	struct list_head list;
	int number_of_leds;
	struct usb_device *usb_dev;
	struct hisax_d_if hisax_d_if;

	struct st5481_ctrl ctrl;
	struct st5481_intr intr;
	struct st5481_in d_in;
	struct st5481_d_out d_out;

	unsigned char leds;
	unsigned int led_counter;

	unsigned long event;

	struct FsmInst l1m;
	struct FsmTimer timer;

	struct st5481_bcs bcs[2];
};

#define TIMER3_VALUE 7000

/* ======================================================================
 *
 */

/*
 * Submit an URB with error reporting. This is a macro so
 * the __func__ returns the caller function name.
 */
#define SUBMIT_URB(urb, mem_flags) \
({ \
	int status; \
	if ((status = usb_submit_urb(urb, mem_flags)) < 0) { \
		WARNING("usb_submit_urb failed,status=%d", status); \
	} \
        status; \
})

/*
 * USB double buffering, return the URB index (0 or 1).
 */
static inline int get_buf_nr(struct urb *urbs[], struct urb *urb)
{
        return (urbs[0]==urb ? 0 : 1); 
}

/* ---------------------------------------------------------------------- */

/* B Channel */

int  st5481_setup_b(struct st5481_bcs *bcs);
void st5481_release_b(struct st5481_bcs *bcs);
void st5481_d_l2l1(struct hisax_if *hisax_d_if, int pr, void *arg);

/* D Channel */

int  st5481_setup_d(struct st5481_adapter *adapter);
void st5481_release_d(struct st5481_adapter *adapter);
void st5481_b_l2l1(struct hisax_if *b_if, int pr, void *arg);
int  st5481_d_init(void);
void st5481_d_exit(void);

/* USB */
void st5481_ph_command(struct st5481_adapter *adapter, unsigned int command);
int st5481_setup_isocpipes(struct urb* urb[2], struct usb_device *dev, 
			   unsigned int pipe, int num_packets,
			   int packet_size, int buf_size,
			   usb_complete_t complete, void *context);
void st5481_release_isocpipes(struct urb* urb[2]);

void st5481_usb_pipe_reset(struct st5481_adapter *adapter,
		    u_char pipe, ctrl_complete_t complete, void *context);
void st5481_usb_device_ctrl_msg(struct st5481_adapter *adapter,
			 u8 request, u16 value,
			 ctrl_complete_t complete, void *context);
int  st5481_setup_usb(struct st5481_adapter *adapter);
void st5481_release_usb(struct st5481_adapter *adapter);
void st5481_start(struct st5481_adapter *adapter);
void st5481_stop(struct st5481_adapter *adapter);

// ----------------------------------------------------------------------
// debugging macros

#define __debug_variable st5481_debug
#include "hisax_debug.h"

extern int st5481_debug;

#ifdef CONFIG_HISAX_DEBUG

#define DBG_ISO_PACKET(level,urb) \
  if (level & __debug_variable) dump_iso_packet(__func__,urb)

static void __attribute__((unused))
dump_iso_packet(const char *name, struct urb *urb)
{
	int i,j;
	int len,ofs;
	u_char *data;

	printk(KERN_DEBUG "%s: packets=%d,errors=%d\n",
	       name,urb->number_of_packets,urb->error_count);
	for (i = 0; i  < urb->number_of_packets; ++i) {
		if (urb->pipe & USB_DIR_IN) {
			len = urb->iso_frame_desc[i].actual_length;
		} else {
			len = urb->iso_frame_desc[i].length;
		}
		ofs = urb->iso_frame_desc[i].offset;
		printk(KERN_DEBUG "len=%.2d,ofs=%.3d ",len,ofs);
		if (len) {
			data = urb->transfer_buffer+ofs;
			for (j=0; j < len; j++) {
				printk ("%.2x", data[j]);
			}
		}
		printk("\n");
	}
}

static inline const char *ST5481_CMD_string(int evt)
{
	static char s[16];

	switch (evt) {
	case ST5481_CMD_DR: return "DR";
	case ST5481_CMD_RES: return "RES";
	case ST5481_CMD_TM1: return "TM1";
	case ST5481_CMD_TM2: return "TM2";
	case ST5481_CMD_PUP: return "PUP";
	case ST5481_CMD_AR8: return "AR8";
	case ST5481_CMD_AR10: return "AR10";
	case ST5481_CMD_ARL: return "ARL";
	case ST5481_CMD_PDN: return "PDN";
	};
	
	sprintf(s,"0x%x",evt);
	return s;
}	

#else

#define DBG_ISO_PACKET(level,urb) do {} while (0)

#endif



#endif 

/*
* hfc_usb.h
*
* $Id: hfc_usb.h,v 4.2 2005/04/07 15:27:17 martinb1 Exp $
*/

#ifndef __HFC_USB_H__
#define __HFC_USB_H__

#define DRIVER_AUTHOR   "Peter Sprenger (sprenger@moving-byters.de)"
#define DRIVER_DESC     "HFC-S USB based HiSAX ISDN driver"

#define VERBOSE_USB_DEBUG

#define TRUE  1
#define FALSE 0


/***********/
/* defines */
/***********/
#define HFC_CTRL_TIMEOUT 20	/* 5ms timeout writing/reading regs */
#define HFC_TIMER_T3 8000	/* timeout for l1 activation timer */
#define HFC_TIMER_T4 500	/* time for state change interval */

#define HFCUSB_L1_STATECHANGE 0	/* L1 state changed */
#define HFCUSB_L1_DRX 1		/* D-frame received */
#define HFCUSB_L1_ERX 2		/* E-frame received */
#define HFCUSB_L1_DTX 4		/* D-frames completed */

#define MAX_BCH_SIZE 2048	/* allowed B-channel packet size */

#define HFCUSB_RX_THRESHOLD 64	/* threshold for fifo report bit rx */
#define HFCUSB_TX_THRESHOLD 64	/* threshold for fifo report bit tx */

#define HFCUSB_CHIP_ID		0x16	/* Chip ID register index */
#define HFCUSB_CIRM		0x00	/* cirm register index */
#define HFCUSB_USB_SIZE		0x07	/* int length register */
#define HFCUSB_USB_SIZE_I	0x06	/* iso length register */
#define HFCUSB_F_CROSS		0x0b	/* bit order register */
#define HFCUSB_CLKDEL		0x37	/* bit delay register */
#define HFCUSB_CON_HDLC		0xfa	/* channel connect register */
#define HFCUSB_HDLC_PAR		0xfb
#define HFCUSB_SCTRL		0x31	/* S-bus control register (tx) */
#define HFCUSB_SCTRL_E		0x32	/* same for E and special funcs */
#define HFCUSB_SCTRL_R		0x33	/* S-bus control register (rx) */
#define HFCUSB_F_THRES		0x0c	/* threshold register */
#define HFCUSB_FIFO		0x0f	/* fifo select register */
#define HFCUSB_F_USAGE		0x1a	/* fifo usage register */
#define HFCUSB_MST_MODE0	0x14
#define HFCUSB_MST_MODE1	0x15
#define HFCUSB_P_DATA		0x1f
#define HFCUSB_INC_RES_F	0x0e
#define HFCUSB_STATES		0x30

#define HFCUSB_CHIPID		0x40	/* ID value of HFC-S USB */

/******************/
/* fifo registers */
/******************/
#define HFCUSB_NUM_FIFOS	8	/* maximum number of fifos */
#define HFCUSB_B1_TX		0	/* index for B1 transmit bulk/int */
#define HFCUSB_B1_RX		1	/* index for B1 receive bulk/int */
#define HFCUSB_B2_TX		2
#define HFCUSB_B2_RX		3
#define HFCUSB_D_TX		4
#define HFCUSB_D_RX		5
#define HFCUSB_PCM_TX		6
#define HFCUSB_PCM_RX		7

/*
* used to switch snd_transfer_mode for different TA modes e.g. the Billion USB TA just
* supports ISO out, while the Cologne Chip EVAL TA just supports BULK out
*/
#define USB_INT		0
#define USB_BULK	1
#define USB_ISOC	2

#define ISOC_PACKETS_D	8
#define ISOC_PACKETS_B	8
#define ISO_BUFFER_SIZE	128

// ISO send definitions
#define SINK_MAX	68
#define SINK_MIN	48
#define SINK_DMIN	12
#define SINK_DMAX	18
#define BITLINE_INF	(-64*8)


/**********/
/* macros */
/**********/
#define write_usb(a,b,c)usb_control_msg((a)->dev,(a)->ctrl_out_pipe,0,0x40,(c),(b),NULL,0,HFC_CTRL_TIMEOUT)
#define read_usb(a,b,c) usb_control_msg((a)->dev,(a)->ctrl_in_pipe,1,0xC0,0,(b),(c),1,HFC_CTRL_TIMEOUT)


/*******************/
/* Debugging Flags */
/*******************/
#define USB_DBG   1
#define ISDN_DBG  2


/* *********************/
/* USB related defines */
/***********************/
#define HFC_CTRL_BUFSIZE 32



/*************************************************/
/* entry and size of output/input control buffer */
/*************************************************/
typedef struct {
	__u8 hfc_reg;		/* register number */
	__u8 reg_val;		/* value to be written (or read) */
	int action;		/* data for action handler */
} ctrl_buft;


/********************/
/* URB error codes: */
/********************/
/* Used to represent a list of values and their respective symbolic names */
struct hfcusb_symbolic_list {
	const int num;
	const char *name;
};

static struct hfcusb_symbolic_list urb_errlist[] = {
	{-ENOMEM, "No memory for allocation of internal structures"},
	{-ENOSPC, "The host controller's bandwidth is already consumed"},
	{-ENOENT, "URB was canceled by unlink_urb"},
	{-EXDEV, "ISO transfer only partially completed"},
	{-EAGAIN, "Too match scheduled for the future"},
	{-ENXIO, "URB already queued"},
	{-EFBIG, "Too much ISO frames requested"},
	{-ENOSR, "Buffer error (overrun)"},
	{-EPIPE, "Specified endpoint is stalled"},
	{-EOVERFLOW, "Babble (bad cable?)"},
	{-EPROTO, "Bit-stuff error (bad cable?)"},
	{-EILSEQ, "CRC or missing token"},
	{-ETIME, "Device did not respond"},
	{-ESHUTDOWN, "Device unplugged"},
	{-1, NULL}
};


/*****************************************************/
/* device dependant information to support different */
/* ISDN Ta's using the HFC-S USB chip                */
/*****************************************************/

/* USB descriptor need to contain one of the following EndPoint combination: */
#define CNF_4INT3ISO	1	// 4 INT IN, 3 ISO OUT
#define CNF_3INT3ISO	2	// 3 INT IN, 3 ISO OUT
#define CNF_4ISO3ISO	3	// 4 ISO IN, 3 ISO OUT
#define CNF_3ISO3ISO	4	// 3 ISO IN, 3 ISO OUT

#define EP_NUL 1		// Endpoint at this position not allowed
#define EP_NOP 2		// all type of endpoints allowed at this position
#define EP_ISO 3		// Isochron endpoint mandatory at this position
#define EP_BLK 4		// Bulk endpoint mandatory at this position
#define EP_INT 5		// Interrupt endpoint mandatory at this position

/* this array represents all endpoints possible in the HCF-USB the last
* 3 entries are the configuration number, the minimum interval for
* Interrupt endpoints & boolean if E-channel logging possible
*/
static int validconf[][19] = {
	// INT in, ISO out config
	{EP_NUL, EP_INT, EP_NUL, EP_INT, EP_NUL, EP_INT, EP_NOP, EP_INT,
	 EP_ISO, EP_NUL, EP_ISO, EP_NUL, EP_ISO, EP_NUL, EP_NUL, EP_NUL,
	 CNF_4INT3ISO, 2, 1},
	{EP_NUL, EP_INT, EP_NUL, EP_INT, EP_NUL, EP_INT, EP_NUL, EP_NUL,
	 EP_ISO, EP_NUL, EP_ISO, EP_NUL, EP_ISO, EP_NUL, EP_NUL, EP_NUL,
	 CNF_3INT3ISO, 2, 0},
	// ISO in, ISO out config
	{EP_NUL, EP_NUL, EP_NUL, EP_NUL, EP_NUL, EP_NUL, EP_NUL, EP_NUL,
	 EP_ISO, EP_ISO, EP_ISO, EP_ISO, EP_ISO, EP_ISO, EP_NOP, EP_ISO,
	 CNF_4ISO3ISO, 2, 1},
	{EP_NUL, EP_NUL, EP_NUL, EP_NUL, EP_NUL, EP_NUL, EP_NUL, EP_NUL,
	 EP_ISO, EP_ISO, EP_ISO, EP_ISO, EP_ISO, EP_ISO, EP_NUL, EP_NUL,
	 CNF_3ISO3ISO, 2, 0},
	{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}	// EOL element
};

#ifdef CONFIG_HISAX_DEBUG
// string description of chosen config
static char *conf_str[] = {
	"4 Interrupt IN + 3 Isochron OUT",
	"3 Interrupt IN + 3 Isochron OUT",
	"4 Isochron IN + 3 Isochron OUT",
	"3 Isochron IN + 3 Isochron OUT"
};
#endif


typedef struct {
	int vendor;		// vendor id
	int prod_id;		// product id
	char *vend_name;	// vendor string
	__u8 led_scheme;	// led display scheme
	signed short led_bits[8];	// array of 8 possible LED bitmask settings
} vendor_data;

#define LED_OFF      0		// no LED support
#define LED_SCHEME1  1		// LED standard scheme
#define LED_SCHEME2  2		// not used yet...

#define LED_POWER_ON	1
#define LED_POWER_OFF	2
#define LED_S0_ON	3
#define LED_S0_OFF	4
#define LED_B1_ON	5
#define LED_B1_OFF	6
#define LED_B1_DATA	7
#define LED_B2_ON	8
#define LED_B2_OFF	9
#define LED_B2_DATA	10

#define LED_NORMAL   0		// LEDs are normal
#define LED_INVERTED 1		// LEDs are inverted

/* time in ms to perform a Flashing LED when B-Channel has traffic */
#define LED_TIME      250


#endif				// __HFC_USB_H__

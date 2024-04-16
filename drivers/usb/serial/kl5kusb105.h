/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Definitions for the KLSI KL5KUSB105 serial port adapter
 */

/* vendor/product pairs that are known to contain this chipset */
#define PALMCONNECT_VID		0x0830
#define PALMCONNECT_PID		0x0080

/* Vendor commands: */


/* port table -- the chip supports up to 4 channels */

/* baud rates */

enum {
	kl5kusb105a_sio_b115200 = 0,
	kl5kusb105a_sio_b57600  = 1,
	kl5kusb105a_sio_b38400  = 2,
	kl5kusb105a_sio_b19200  = 4,
	kl5kusb105a_sio_b14400  = 5,
	kl5kusb105a_sio_b9600   = 6,
	kl5kusb105a_sio_b4800   = 8,	/* unchecked */
	kl5kusb105a_sio_b2400   = 9,	/* unchecked */
	kl5kusb105a_sio_b1200   = 0xa,	/* unchecked */
	kl5kusb105a_sio_b600    = 0xb	/* unchecked */
};

/* data bits */
#define kl5kusb105a_dtb_7   7
#define kl5kusb105a_dtb_8   8



/* requests: */
#define KL5KUSB105A_SIO_SET_DATA  1
#define KL5KUSB105A_SIO_POLL      2
#define KL5KUSB105A_SIO_CONFIGURE      3
/* values used for request KL5KUSB105A_SIO_CONFIGURE */
#define KL5KUSB105A_SIO_CONFIGURE_READ_ON      3
#define KL5KUSB105A_SIO_CONFIGURE_READ_OFF     2

/* Interpretation of modem status lines */
/* These need sorting out by individually connecting pins and checking
 * results. FIXME!
 * When data is being sent we see 0x30 in the lower byte; this must
 * contain DSR and CTS ...
 */
#define KL5KUSB105A_DSR			((1<<4) | (1<<5))
#define KL5KUSB105A_CTS			((1<<5) | (1<<4))

#define KL5KUSB105A_WANTS_TO_SEND	0x30
#if 0
#define KL5KUSB105A_DTR			/* Data Terminal Ready */
#define KL5KUSB105A_CTS			/* Clear To Send */
#define KL5KUSB105A_CD			/* Carrier Detect */
#define KL5KUSB105A_DSR			/* Data Set Ready */
#define KL5KUSB105A_RxD			/* Receive pin */

#define KL5KUSB105A_LE
#define KL5KUSB105A_RTS
#define KL5KUSB105A_ST
#define KL5KUSB105A_SR
#define KL5KUSB105A_RI			/* Ring Indicator */
#endif

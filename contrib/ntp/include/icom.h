/*
 * Header file for ICOM radios
 */
#include "ntp_types.h"

/*
 * Common definitions
 */
#define P_ERMSG	0x1		/* trace bus error messages */
#define P_TRACE 0x2		/* trace CI-V messges */
#define RETRY	3		/* max packet retries */
#define IBAUD	B1200		/* autotune port speed */

/*
 * Radio identifier codes
 */
#define IC1271	0x24
#define IC1275	0x18
#define IC271	0x20
#define IC275	0x10
#define IC375	0x12
#define IC471	0x22
#define IC475	0x14
#define IC575	0x16
#define IC725	0x28
#define IC726	0x30
#define IC735	0x04
#define IC751	0x1c
#define IC761	0x1e
#define IC765	0x2c
#define IC775	0x46
#define IC781	0x26
#define IC970	0x2e
#define R7000	0x08
#define R71	0x1a
#define R7100	0x34
#define R72	0x32
#define R8500	0x4a
#define R9000	0x2a

/*
 * CI-V frame codes
 */
#define PR	0xfe		/* preamble */
#define TX	0xe0		/* controller address */
#define FI	0xfd		/* end of message */
#define ACK	0xfb		/* controller normal reply */
#define NAK	0xfa		/* controller error reply */
#define PAD	0xff		/* transmit padding */

/*
 * CI-V controller commands
 */
#define V_FREQT	0x00		/* freq set (transceive) */
#define V_MODET	0x01		/* set mode (transceive) */
#define V_RBAND	0x02		/* read band edge */
#define V_RFREQ	0x03		/* read frequency */
#define V_RMODE	0x04		/* read mode */
#define V_SFREQ	0x05		/* set frequency */
#define V_SMODE	0x06		/* set mode */
#define V_SVFO	0x07		/* select vfo */
#define V_SMEM	0x08		/* select channel/bank */
#define V_WRITE	0x09		/* write channel */
#define V_VFOM	0x0a		/* memory -> vfo */
#define V_CLEAR	0x0b		/* clear channel */
#define V_ROFFS	0x0c		/* read tx offset */
#define V_SOFFS	0x0d		/* write tx offset */
#define V_SCAN	0x0e		/* scan control */
#define V_SPLIT	0x0f		/* split control */
#define V_DIAL	0x10		/* set dial tuning step */
#define V_ATTEN	0x11		/* set attenuator */
#define V_SANT	0x12		/* select antenna */
#define V_ANNC	0x13		/* announce control */
#define V_WRCTL	0x14		/* write controls */
#define V_RDCTL	0x15		/* read controls */
#define V_TOGL	0x16		/* set switches */
#define V_ASCII	0x17		/* send CW message */
#define V_POWER	0x18		/* power control */
#define V_RDID	0x19		/* read model ID */
#define V_SETW	0x1a		/* read/write channel/bank data */
#define V_CTRL	0x7f		/* miscellaneous control */

/*
 * Function prototypes
 */
int	icom_init		(const char *, int, int);
int	icom_freq		(int, int, double);

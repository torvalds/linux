/* Header for CPC-USB Driver ********************
 * Copyright 1999, 2000, 2001
 *
 * Company:  EMS Dr. Thomas Wuensche
 *           Sonnenhang 3
 *           85304 Ilmmuenster
 *           Phone: +49-8441-490260
 *           Fax:   +49-8441-81860
 *           email: support@ems-wuensche.com
 *           WWW:   www.ems-wuensche.com
 */

#ifndef CPCUSB_H
#define CPCUSB_H

#undef err
#undef dbg
#undef info

/* Use our own dbg macro */
#define dbg(format, arg...) do { if (debug) printk(KERN_INFO "CPC-USB: " format "\n" , ## arg); } while (0)
#define info(format, arg...) do { printk(KERN_INFO "CPC-USB: " format "\n" , ## arg); } while (0)
#define err(format, arg...) do { printk(KERN_INFO "CPC-USB(ERROR): " format "\n" , ## arg); } while (0)

#define CPC_USB_CARD_CNT      4

typedef struct CPC_USB_READ_URB {
	unsigned char *buffer;	/* the buffer to send data */
	size_t size;		/* the size of the send buffer */
	struct urb *urb;	/* the urb used to send data */
} CPC_USB_READ_URB_T;

typedef struct CPC_USB_WRITE_URB {
	unsigned char *buffer;	/* the buffer to send data */
	size_t size;		/* the size of the send buffer */
	struct urb *urb;	/* the urb used to send data */
	atomic_t busy;		/* true if write urb is busy */
	struct completion finished;	/* wait for the write to finish */
} CPC_USB_WRITE_URB_T;

#define CPC_USB_URB_CNT  10

typedef struct CPC_USB {
	struct usb_device *udev;	/* save off the usb device pointer */
	struct usb_interface *interface;	/* the interface for this device */
	unsigned char minor;	/* the starting minor number for this device */
	unsigned char num_ports;	/* the number of ports this device has */
	int num_intr_in;	/* number of interrupt in endpoints we have */
	int num_bulk_in;	/* number of bulk in endpoints we have */
	int num_bulk_out;	/* number of bulk out endpoints we have */

	CPC_USB_READ_URB_T urbs[CPC_USB_URB_CNT];

	unsigned char intr_in_buffer[4];	/* interrupt transfer buffer */
	struct urb *intr_in_urb;	/* interrupt transfer urb */

	CPC_USB_WRITE_URB_T wrUrbs[CPC_USB_URB_CNT];

	int open;		/* if the port is open or not */
	int present;		/* if the device is not disconnected */
	struct semaphore sem;	/* locks this structure */

	int free_slots;		/* free send slots of CPC-USB */
	int idx;

	spinlock_t slock;

	char serialNumber[128];	/* serial number */
	int productId;		/* product id to differ between M16C and LPC2119 */
	CPC_CHAN_T *chan;
} CPC_USB_T;

#define CPCTable               CPCUSB_Table

#define CPC_DRIVER_VERSION "0.724"
#define CPC_DRIVER_SERIAL  "not applicable"

#define OBUF_SIZE 255		// 4096

/* read timeouts -- RD_NAK_TIMEOUT * RD_EXPIRE = Number of seconds */
#define RD_NAK_TIMEOUT (10*HZ)	/* Default number of X seconds to wait */
#define RD_EXPIRE 12		/* Number of attempts to wait X seconds */

#define CPC_USB_BASE_MNR 0	/* CPC-USB start at minor 0  */

#endif

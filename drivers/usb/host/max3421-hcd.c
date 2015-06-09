/*
 * MAX3421 Host Controller driver for USB.
 *
 * Author: David Mosberger-Tang <davidm@egauge.net>
 *
 * (C) Copyright 2014 David Mosberger-Tang <davidm@egauge.net>
 *
 * MAX3421 is a chip implementing a USB 2.0 Full-/Low-Speed host
 * controller on a SPI bus.
 *
 * Based on:
 *	o MAX3421E datasheet
 *		http://datasheets.maximintegrated.com/en/ds/MAX3421E.pdf
 *	o MAX3421E Programming Guide
 *		http://www.hdl.co.jp/ftpdata/utl-001/AN3785.pdf
 *	o gadget/dummy_hcd.c
 *		For USB HCD implementation.
 *	o Arduino MAX3421 driver
 *	     https://github.com/felis/USB_Host_Shield_2.0/blob/master/Usb.cpp
 *
 * This file is licenced under the GPL v2.
 *
 * Important note on worst-case (full-speed) packet size constraints
 * (See USB 2.0 Section 5.6.3 and following):
 *
 *	- control:	  64 bytes
 *	- isochronous:	1023 bytes
 *	- interrupt:	  64 bytes
 *	- bulk:		  64 bytes
 *
 * Since the MAX3421 FIFO size is 64 bytes, we do not have to work about
 * multi-FIFO writes/reads for a single USB packet *except* for isochronous
 * transfers.  We don't support isochronous transfers at this time, so we
 * just assume that a USB packet always fits into a single FIFO buffer.
 *
 * NOTE: The June 2006 version of "MAX3421E Programming Guide"
 * (AN3785) has conflicting info for the RCVDAVIRQ bit:
 *
 *	The description of RCVDAVIRQ says "The CPU *must* clear
 *	this IRQ bit (by writing a 1 to it) before reading the
 *	RCVFIFO data.
 *
 * However, the earlier section on "Programming BULK-IN
 * Transfers" says * that:
 *
 *	After the CPU retrieves the data, it clears the
 *	RCVDAVIRQ bit.
 *
 * The December 2006 version has been corrected and it consistently
 * states the second behavior is the correct one.
 *
 * Synchronous SPI transactions sleep so we can't perform any such
 * transactions while holding a spin-lock (and/or while interrupts are
 * masked).  To achieve this, all SPI transactions are issued from a
 * single thread (max3421_spi_thread).
 */

#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>

#include <linux/platform_data/max3421-hcd.h>

#define DRIVER_DESC	"MAX3421 USB Host-Controller Driver"
#define DRIVER_VERSION	"1.0"

/* 11-bit counter that wraps around (USB 2.0 Section 8.3.3): */
#define USB_MAX_FRAME_NUMBER	0x7ff
#define USB_MAX_RETRIES		3 /* # of retries before error is reported */

/*
 * Max. # of times we're willing to retransmit a request immediately in
 * resposne to a NAK.  Afterwards, we fall back on trying once a frame.
 */
#define NAK_MAX_FAST_RETRANSMITS	2

#define POWER_BUDGET	500	/* in mA; use 8 for low-power port testing */

/* Port-change mask: */
#define PORT_C_MASK	((USB_PORT_STAT_C_CONNECTION |	\
			  USB_PORT_STAT_C_ENABLE |	\
			  USB_PORT_STAT_C_SUSPEND |	\
			  USB_PORT_STAT_C_OVERCURRENT | \
			  USB_PORT_STAT_C_RESET) << 16)

enum max3421_rh_state {
	MAX3421_RH_RESET,
	MAX3421_RH_SUSPENDED,
	MAX3421_RH_RUNNING
};

enum pkt_state {
	PKT_STATE_SETUP,	/* waiting to send setup packet to ctrl pipe */
	PKT_STATE_TRANSFER,	/* waiting to xfer transfer_buffer */
	PKT_STATE_TERMINATE	/* waiting to terminate control transfer */
};

enum scheduling_pass {
	SCHED_PASS_PERIODIC,
	SCHED_PASS_NON_PERIODIC,
	SCHED_PASS_DONE
};

/* Bit numbers for max3421_hcd->todo: */
enum {
	ENABLE_IRQ = 0,
	RESET_HCD,
	RESET_PORT,
	CHECK_UNLINK,
	IOPIN_UPDATE
};

struct max3421_dma_buf {
	u8 data[2];
};

struct max3421_hcd {
	spinlock_t lock;

	struct task_struct *spi_thread;

	struct max3421_hcd *next;

	enum max3421_rh_state rh_state;
	/* lower 16 bits contain port status, upper 16 bits the change mask: */
	u32 port_status;

	unsigned active:1;

	struct list_head ep_list;	/* list of EP's with work */

	/*
	 * The following are owned by spi_thread (may be accessed by
	 * SPI-thread without acquiring the HCD lock:
	 */
	u8 rev;				/* chip revision */
	u16 frame_number;
	/*
	 * kmalloc'd buffers guaranteed to be in separate (DMA)
	 * cache-lines:
	 */
	struct max3421_dma_buf *tx;
	struct max3421_dma_buf *rx;
	/*
	 * URB we're currently processing.  Must not be reset to NULL
	 * unless MAX3421E chip is idle:
	 */
	struct urb *curr_urb;
	enum scheduling_pass sched_pass;
	struct usb_device *loaded_dev;	/* dev that's loaded into the chip */
	int loaded_epnum;		/* epnum whose toggles are loaded */
	int urb_done;			/* > 0 -> no errors, < 0: errno */
	size_t curr_len;
	u8 hien;
	u8 mode;
	u8 iopins[2];
	unsigned long todo;
#ifdef DEBUG
	unsigned long err_stat[16];
#endif
};

struct max3421_ep {
	struct usb_host_endpoint *ep;
	struct list_head ep_list;
	u32 naks;
	u16 last_active;		/* frame # this ep was last active */
	enum pkt_state pkt_state;
	u8 retries;
	u8 retransmit;			/* packet needs retransmission */
};

static struct max3421_hcd *max3421_hcd_list;

#define MAX3421_FIFO_SIZE	64

#define MAX3421_SPI_DIR_RD	0	/* read register from MAX3421 */
#define MAX3421_SPI_DIR_WR	1	/* write register to MAX3421 */

/* SPI commands: */
#define MAX3421_SPI_DIR_SHIFT	1
#define MAX3421_SPI_REG_SHIFT	3

#define MAX3421_REG_RCVFIFO	1
#define MAX3421_REG_SNDFIFO	2
#define MAX3421_REG_SUDFIFO	4
#define MAX3421_REG_RCVBC	6
#define MAX3421_REG_SNDBC	7
#define MAX3421_REG_USBIRQ	13
#define MAX3421_REG_USBIEN	14
#define MAX3421_REG_USBCTL	15
#define MAX3421_REG_CPUCTL	16
#define MAX3421_REG_PINCTL	17
#define MAX3421_REG_REVISION	18
#define MAX3421_REG_IOPINS1	20
#define MAX3421_REG_IOPINS2	21
#define MAX3421_REG_GPINIRQ	22
#define MAX3421_REG_GPINIEN	23
#define MAX3421_REG_GPINPOL	24
#define MAX3421_REG_HIRQ	25
#define MAX3421_REG_HIEN	26
#define MAX3421_REG_MODE	27
#define MAX3421_REG_PERADDR	28
#define MAX3421_REG_HCTL	29
#define MAX3421_REG_HXFR	30
#define MAX3421_REG_HRSL	31

enum {
	MAX3421_USBIRQ_OSCOKIRQ_BIT = 0,
	MAX3421_USBIRQ_NOVBUSIRQ_BIT = 5,
	MAX3421_USBIRQ_VBUSIRQ_BIT
};

enum {
	MAX3421_CPUCTL_IE_BIT = 0,
	MAX3421_CPUCTL_PULSEWID0_BIT = 6,
	MAX3421_CPUCTL_PULSEWID1_BIT
};

enum {
	MAX3421_USBCTL_PWRDOWN_BIT = 4,
	MAX3421_USBCTL_CHIPRES_BIT
};

enum {
	MAX3421_PINCTL_GPXA_BIT	= 0,
	MAX3421_PINCTL_GPXB_BIT,
	MAX3421_PINCTL_POSINT_BIT,
	MAX3421_PINCTL_INTLEVEL_BIT,
	MAX3421_PINCTL_FDUPSPI_BIT,
	MAX3421_PINCTL_EP0INAK_BIT,
	MAX3421_PINCTL_EP2INAK_BIT,
	MAX3421_PINCTL_EP3INAK_BIT,
};

enum {
	MAX3421_HI_BUSEVENT_BIT = 0,	/* bus-reset/-resume */
	MAX3421_HI_RWU_BIT,		/* remote wakeup */
	MAX3421_HI_RCVDAV_BIT,		/* receive FIFO data available */
	MAX3421_HI_SNDBAV_BIT,		/* send buffer available */
	MAX3421_HI_SUSDN_BIT,		/* suspend operation done */
	MAX3421_HI_CONDET_BIT,		/* peripheral connect/disconnect */
	MAX3421_HI_FRAME_BIT,		/* frame generator */
	MAX3421_HI_HXFRDN_BIT,		/* host transfer done */
};

enum {
	MAX3421_HCTL_BUSRST_BIT = 0,
	MAX3421_HCTL_FRMRST_BIT,
	MAX3421_HCTL_SAMPLEBUS_BIT,
	MAX3421_HCTL_SIGRSM_BIT,
	MAX3421_HCTL_RCVTOG0_BIT,
	MAX3421_HCTL_RCVTOG1_BIT,
	MAX3421_HCTL_SNDTOG0_BIT,
	MAX3421_HCTL_SNDTOG1_BIT
};

enum {
	MAX3421_MODE_HOST_BIT = 0,
	MAX3421_MODE_LOWSPEED_BIT,
	MAX3421_MODE_HUBPRE_BIT,
	MAX3421_MODE_SOFKAENAB_BIT,
	MAX3421_MODE_SEPIRQ_BIT,
	MAX3421_MODE_DELAYISO_BIT,
	MAX3421_MODE_DMPULLDN_BIT,
	MAX3421_MODE_DPPULLDN_BIT
};

enum {
	MAX3421_HRSL_OK = 0,
	MAX3421_HRSL_BUSY,
	MAX3421_HRSL_BADREQ,
	MAX3421_HRSL_UNDEF,
	MAX3421_HRSL_NAK,
	MAX3421_HRSL_STALL,
	MAX3421_HRSL_TOGERR,
	MAX3421_HRSL_WRONGPID,
	MAX3421_HRSL_BADBC,
	MAX3421_HRSL_PIDERR,
	MAX3421_HRSL_PKTERR,
	MAX3421_HRSL_CRCERR,
	MAX3421_HRSL_KERR,
	MAX3421_HRSL_JERR,
	MAX3421_HRSL_TIMEOUT,
	MAX3421_HRSL_BABBLE,
	MAX3421_HRSL_RESULT_MASK = 0xf,
	MAX3421_HRSL_RCVTOGRD_BIT = 4,
	MAX3421_HRSL_SNDTOGRD_BIT,
	MAX3421_HRSL_KSTATUS_BIT,
	MAX3421_HRSL_JSTATUS_BIT
};

/* Return same error-codes as ohci.h:cc_to_error: */
static const int hrsl_to_error[] = {
	[MAX3421_HRSL_OK] =		0,
	[MAX3421_HRSL_BUSY] =		-EINVAL,
	[MAX3421_HRSL_BADREQ] =		-EINVAL,
	[MAX3421_HRSL_UNDEF] =		-EINVAL,
	[MAX3421_HRSL_NAK] =		-EAGAIN,
	[MAX3421_HRSL_STALL] =		-EPIPE,
	[MAX3421_HRSL_TOGERR] =		-EILSEQ,
	[MAX3421_HRSL_WRONGPID] =	-EPROTO,
	[MAX3421_HRSL_BADBC] =		-EREMOTEIO,
	[MAX3421_HRSL_PIDERR] =		-EPROTO,
	[MAX3421_HRSL_PKTERR] =		-EPROTO,
	[MAX3421_HRSL_CRCERR] =		-EILSEQ,
	[MAX3421_HRSL_KERR] =		-EIO,
	[MAX3421_HRSL_JERR] =		-EIO,
	[MAX3421_HRSL_TIMEOUT] =	-ETIME,
	[MAX3421_HRSL_BABBLE] =		-EOVERFLOW
};

/*
 * See http://www.beyondlogic.org/usbnutshell/usb4.shtml#Control for a
 * reasonable overview of how control transfers use the the IN/OUT
 * tokens.
 */
#define MAX3421_HXFR_BULK_IN(ep)	(0x00 | (ep))	/* bulk or interrupt */
#define MAX3421_HXFR_SETUP		 0x10
#define MAX3421_HXFR_BULK_OUT(ep)	(0x20 | (ep))	/* bulk or interrupt */
#define MAX3421_HXFR_ISO_IN(ep)		(0x40 | (ep))
#define MAX3421_HXFR_ISO_OUT(ep)	(0x60 | (ep))
#define MAX3421_HXFR_HS_IN		 0x80		/* handshake in */
#define MAX3421_HXFR_HS_OUT		 0xa0		/* handshake out */

#define field(val, bit)	((val) << (bit))

static inline s16
frame_diff(u16 left, u16 right)
{
	return ((unsigned) (left - right)) % (USB_MAX_FRAME_NUMBER + 1);
}

static inline struct max3421_hcd *
hcd_to_max3421(struct usb_hcd *hcd)
{
	return (struct max3421_hcd *) hcd->hcd_priv;
}

static inline struct usb_hcd *
max3421_to_hcd(struct max3421_hcd *max3421_hcd)
{
	return container_of((void *) max3421_hcd, struct usb_hcd, hcd_priv);
}

static u8
spi_rd8(struct usb_hcd *hcd, unsigned int reg)
{
	struct max3421_hcd *max3421_hcd = hcd_to_max3421(hcd);
	struct spi_device *spi = to_spi_device(hcd->self.controller);
	struct spi_transfer transfer;
	struct spi_message msg;

	memset(&transfer, 0, sizeof(transfer));

	spi_message_init(&msg);

	max3421_hcd->tx->data[0] =
		(field(reg, MAX3421_SPI_REG_SHIFT) |
		 field(MAX3421_SPI_DIR_RD, MAX3421_SPI_DIR_SHIFT));

	transfer.tx_buf = max3421_hcd->tx->data;
	transfer.rx_buf = max3421_hcd->rx->data;
	transfer.len = 2;

	spi_message_add_tail(&transfer, &msg);
	spi_sync(spi, &msg);

	return max3421_hcd->rx->data[1];
}

static void
spi_wr8(struct usb_hcd *hcd, unsigned int reg, u8 val)
{
	struct spi_device *spi = to_spi_device(hcd->self.controller);
	struct max3421_hcd *max3421_hcd = hcd_to_max3421(hcd);
	struct spi_transfer transfer;
	struct spi_message msg;

	memset(&transfer, 0, sizeof(transfer));

	spi_message_init(&msg);

	max3421_hcd->tx->data[0] =
		(field(reg, MAX3421_SPI_REG_SHIFT) |
		 field(MAX3421_SPI_DIR_WR, MAX3421_SPI_DIR_SHIFT));
	max3421_hcd->tx->data[1] = val;

	transfer.tx_buf = max3421_hcd->tx->data;
	transfer.len = 2;

	spi_message_add_tail(&transfer, &msg);
	spi_sync(spi, &msg);
}

static void
spi_rd_buf(struct usb_hcd *hcd, unsigned int reg, void *buf, size_t len)
{
	struct spi_device *spi = to_spi_device(hcd->self.controller);
	struct max3421_hcd *max3421_hcd = hcd_to_max3421(hcd);
	struct spi_transfer transfer[2];
	struct spi_message msg;

	memset(transfer, 0, sizeof(transfer));

	spi_message_init(&msg);

	max3421_hcd->tx->data[0] =
		(field(reg, MAX3421_SPI_REG_SHIFT) |
		 field(MAX3421_SPI_DIR_RD, MAX3421_SPI_DIR_SHIFT));
	transfer[0].tx_buf = max3421_hcd->tx->data;
	transfer[0].len = 1;

	transfer[1].rx_buf = buf;
	transfer[1].len = len;

	spi_message_add_tail(&transfer[0], &msg);
	spi_message_add_tail(&transfer[1], &msg);
	spi_sync(spi, &msg);
}

static void
spi_wr_buf(struct usb_hcd *hcd, unsigned int reg, void *buf, size_t len)
{
	struct spi_device *spi = to_spi_device(hcd->self.controller);
	struct max3421_hcd *max3421_hcd = hcd_to_max3421(hcd);
	struct spi_transfer transfer[2];
	struct spi_message msg;

	memset(transfer, 0, sizeof(transfer));

	spi_message_init(&msg);

	max3421_hcd->tx->data[0] =
		(field(reg, MAX3421_SPI_REG_SHIFT) |
		 field(MAX3421_SPI_DIR_WR, MAX3421_SPI_DIR_SHIFT));

	transfer[0].tx_buf = max3421_hcd->tx->data;
	transfer[0].len = 1;

	transfer[1].tx_buf = buf;
	transfer[1].len = len;

	spi_message_add_tail(&transfer[0], &msg);
	spi_message_add_tail(&transfer[1], &msg);
	spi_sync(spi, &msg);
}

/*
 * Figure out the correct setting for the LOWSPEED and HUBPRE mode
 * bits.  The HUBPRE bit needs to be set when MAX3421E operates at
 * full speed, but it's talking to a low-speed device (i.e., through a
 * hub).  Setting that bit ensures that every low-speed packet is
 * preceded by a full-speed PRE PID.  Possible configurations:
 *
 * Hub speed:	Device speed:	=>	LOWSPEED bit:	HUBPRE bit:
 *	FULL	FULL		=>	0		0
 *	FULL	LOW		=>	1		1
 *	LOW	LOW		=>	1		0
 *	LOW	FULL		=>	1		0
 */
static void
max3421_set_speed(struct usb_hcd *hcd, struct usb_device *dev)
{
	struct max3421_hcd *max3421_hcd = hcd_to_max3421(hcd);
	u8 mode_lowspeed, mode_hubpre, mode = max3421_hcd->mode;

	mode_lowspeed = BIT(MAX3421_MODE_LOWSPEED_BIT);
	mode_hubpre   = BIT(MAX3421_MODE_HUBPRE_BIT);
	if (max3421_hcd->port_status & USB_PORT_STAT_LOW_SPEED) {
		mode |=  mode_lowspeed;
		mode &= ~mode_hubpre;
	} else if (dev->speed == USB_SPEED_LOW) {
		mode |= mode_lowspeed | mode_hubpre;
	} else {
		mode &= ~(mode_lowspeed | mode_hubpre);
	}
	if (mode != max3421_hcd->mode) {
		max3421_hcd->mode = mode;
		spi_wr8(hcd, MAX3421_REG_MODE, max3421_hcd->mode);
	}

}

/*
 * Caller must NOT hold HCD spinlock.
 */
static void
max3421_set_address(struct usb_hcd *hcd, struct usb_device *dev, int epnum,
		    int force_toggles)
{
	struct max3421_hcd *max3421_hcd = hcd_to_max3421(hcd);
	int old_epnum, same_ep, rcvtog, sndtog;
	struct usb_device *old_dev;
	u8 hctl;

	old_dev = max3421_hcd->loaded_dev;
	old_epnum = max3421_hcd->loaded_epnum;

	same_ep = (dev == old_dev && epnum == old_epnum);
	if (same_ep && !force_toggles)
		return;

	if (old_dev && !same_ep) {
		/* save the old end-points toggles: */
		u8 hrsl = spi_rd8(hcd, MAX3421_REG_HRSL);

		rcvtog = (hrsl >> MAX3421_HRSL_RCVTOGRD_BIT) & 1;
		sndtog = (hrsl >> MAX3421_HRSL_SNDTOGRD_BIT) & 1;

		/* no locking: HCD (i.e., we) own toggles, don't we? */
		usb_settoggle(old_dev, old_epnum, 0, rcvtog);
		usb_settoggle(old_dev, old_epnum, 1, sndtog);
	}
	/* setup new endpoint's toggle bits: */
	rcvtog = usb_gettoggle(dev, epnum, 0);
	sndtog = usb_gettoggle(dev, epnum, 1);
	hctl = (BIT(rcvtog + MAX3421_HCTL_RCVTOG0_BIT) |
		BIT(sndtog + MAX3421_HCTL_SNDTOG0_BIT));

	max3421_hcd->loaded_epnum = epnum;
	spi_wr8(hcd, MAX3421_REG_HCTL, hctl);

	/*
	 * Note: devnum for one and the same device can change during
	 * address-assignment so it's best to just always load the
	 * address whenever the end-point changed/was forced.
	 */
	max3421_hcd->loaded_dev = dev;
	spi_wr8(hcd, MAX3421_REG_PERADDR, dev->devnum);
}

static int
max3421_ctrl_setup(struct usb_hcd *hcd, struct urb *urb)
{
	spi_wr_buf(hcd, MAX3421_REG_SUDFIFO, urb->setup_packet, 8);
	return MAX3421_HXFR_SETUP;
}

static int
max3421_transfer_in(struct usb_hcd *hcd, struct urb *urb)
{
	struct max3421_hcd *max3421_hcd = hcd_to_max3421(hcd);
	int epnum = usb_pipeendpoint(urb->pipe);

	max3421_hcd->curr_len = 0;
	max3421_hcd->hien |= BIT(MAX3421_HI_RCVDAV_BIT);
	return MAX3421_HXFR_BULK_IN(epnum);
}

static int
max3421_transfer_out(struct usb_hcd *hcd, struct urb *urb, int fast_retransmit)
{
	struct spi_device *spi = to_spi_device(hcd->self.controller);
	struct max3421_hcd *max3421_hcd = hcd_to_max3421(hcd);
	int epnum = usb_pipeendpoint(urb->pipe);
	u32 max_packet;
	void *src;

	src = urb->transfer_buffer + urb->actual_length;

	if (fast_retransmit) {
		if (max3421_hcd->rev == 0x12) {
			/* work around rev 0x12 bug: */
			spi_wr8(hcd, MAX3421_REG_SNDBC, 0);
			spi_wr8(hcd, MAX3421_REG_SNDFIFO, ((u8 *) src)[0]);
			spi_wr8(hcd, MAX3421_REG_SNDBC, max3421_hcd->curr_len);
		}
		return MAX3421_HXFR_BULK_OUT(epnum);
	}

	max_packet = usb_maxpacket(urb->dev, urb->pipe, 1);

	if (max_packet > MAX3421_FIFO_SIZE) {
		/*
		 * We do not support isochronous transfers at this
		 * time.
		 */
		dev_err(&spi->dev,
			"%s: packet-size of %u too big (limit is %u bytes)",
			__func__, max_packet, MAX3421_FIFO_SIZE);
		max3421_hcd->urb_done = -EMSGSIZE;
		return -EMSGSIZE;
	}
	max3421_hcd->curr_len = min((urb->transfer_buffer_length -
				     urb->actual_length), max_packet);

	spi_wr_buf(hcd, MAX3421_REG_SNDFIFO, src, max3421_hcd->curr_len);
	spi_wr8(hcd, MAX3421_REG_SNDBC, max3421_hcd->curr_len);
	return MAX3421_HXFR_BULK_OUT(epnum);
}

/*
 * Issue the next host-transfer command.
 * Caller must NOT hold HCD spinlock.
 */
static void
max3421_next_transfer(struct usb_hcd *hcd, int fast_retransmit)
{
	struct max3421_hcd *max3421_hcd = hcd_to_max3421(hcd);
	struct urb *urb = max3421_hcd->curr_urb;
	struct max3421_ep *max3421_ep;
	int cmd = -EINVAL;

	if (!urb)
		return;	/* nothing to do */

	max3421_ep = urb->ep->hcpriv;

	switch (max3421_ep->pkt_state) {
	case PKT_STATE_SETUP:
		cmd = max3421_ctrl_setup(hcd, urb);
		break;

	case PKT_STATE_TRANSFER:
		if (usb_urb_dir_in(urb))
			cmd = max3421_transfer_in(hcd, urb);
		else
			cmd = max3421_transfer_out(hcd, urb, fast_retransmit);
		break;

	case PKT_STATE_TERMINATE:
		/*
		 * IN transfers are terminated with HS_OUT token,
		 * OUT transfers with HS_IN:
		 */
		if (usb_urb_dir_in(urb))
			cmd = MAX3421_HXFR_HS_OUT;
		else
			cmd = MAX3421_HXFR_HS_IN;
		break;
	}

	if (cmd < 0)
		return;

	/* issue the command and wait for host-xfer-done interrupt: */

	spi_wr8(hcd, MAX3421_REG_HXFR, cmd);
	max3421_hcd->hien |= BIT(MAX3421_HI_HXFRDN_BIT);
}

/*
 * Find the next URB to process and start its execution.
 *
 * At this time, we do not anticipate ever connecting a USB hub to the
 * MAX3421 chip, so at most USB device can be connected and we can use
 * a simplistic scheduler: at the start of a frame, schedule all
 * periodic transfers.  Once that is done, use the remainder of the
 * frame to process non-periodic (bulk & control) transfers.
 *
 * Preconditions:
 * o Caller must NOT hold HCD spinlock.
 * o max3421_hcd->curr_urb MUST BE NULL.
 * o MAX3421E chip must be idle.
 */
static int
max3421_select_and_start_urb(struct usb_hcd *hcd)
{
	struct spi_device *spi = to_spi_device(hcd->self.controller);
	struct max3421_hcd *max3421_hcd = hcd_to_max3421(hcd);
	struct urb *urb, *curr_urb = NULL;
	struct max3421_ep *max3421_ep;
	int epnum, force_toggles = 0;
	struct usb_host_endpoint *ep;
	struct list_head *pos;
	unsigned long flags;

	spin_lock_irqsave(&max3421_hcd->lock, flags);

	for (;
	     max3421_hcd->sched_pass < SCHED_PASS_DONE;
	     ++max3421_hcd->sched_pass)
		list_for_each(pos, &max3421_hcd->ep_list) {
			urb = NULL;
			max3421_ep = container_of(pos, struct max3421_ep,
						  ep_list);
			ep = max3421_ep->ep;

			switch (usb_endpoint_type(&ep->desc)) {
			case USB_ENDPOINT_XFER_ISOC:
			case USB_ENDPOINT_XFER_INT:
				if (max3421_hcd->sched_pass !=
				    SCHED_PASS_PERIODIC)
					continue;
				break;

			case USB_ENDPOINT_XFER_CONTROL:
			case USB_ENDPOINT_XFER_BULK:
				if (max3421_hcd->sched_pass !=
				    SCHED_PASS_NON_PERIODIC)
					continue;
				break;
			}

			if (list_empty(&ep->urb_list))
				continue;	/* nothing to do */
			urb = list_first_entry(&ep->urb_list, struct urb,
					       urb_list);
			if (urb->unlinked) {
				dev_dbg(&spi->dev, "%s: URB %p unlinked=%d",
					__func__, urb, urb->unlinked);
				max3421_hcd->curr_urb = urb;
				max3421_hcd->urb_done = 1;
				spin_unlock_irqrestore(&max3421_hcd->lock,
						       flags);
				return 1;
			}

			switch (usb_endpoint_type(&ep->desc)) {
			case USB_ENDPOINT_XFER_CONTROL:
				/*
				 * Allow one control transaction per
				 * frame per endpoint:
				 */
				if (frame_diff(max3421_ep->last_active,
					       max3421_hcd->frame_number) == 0)
					continue;
				break;

			case USB_ENDPOINT_XFER_BULK:
				if (max3421_ep->retransmit
				    && (frame_diff(max3421_ep->last_active,
						   max3421_hcd->frame_number)
					== 0))
					/*
					 * We already tried this EP
					 * during this frame and got a
					 * NAK or error; wait for next frame
					 */
					continue;
				break;

			case USB_ENDPOINT_XFER_ISOC:
			case USB_ENDPOINT_XFER_INT:
				if (frame_diff(max3421_hcd->frame_number,
					       max3421_ep->last_active)
				    < urb->interval)
					/*
					 * We already processed this
					 * end-point in the current
					 * frame
					 */
					continue;
				break;
			}

			/* move current ep to tail: */
			list_move_tail(pos, &max3421_hcd->ep_list);
			curr_urb = urb;
			goto done;
		}
done:
	if (!curr_urb) {
		spin_unlock_irqrestore(&max3421_hcd->lock, flags);
		return 0;
	}

	urb = max3421_hcd->curr_urb = curr_urb;
	epnum = usb_endpoint_num(&urb->ep->desc);
	if (max3421_ep->retransmit)
		/* restart (part of) a USB transaction: */
		max3421_ep->retransmit = 0;
	else {
		/* start USB transaction: */
		if (usb_endpoint_xfer_control(&ep->desc)) {
			/*
			 * See USB 2.0 spec section 8.6.1
			 * Initialization via SETUP Token:
			 */
			usb_settoggle(urb->dev, epnum, 0, 1);
			usb_settoggle(urb->dev, epnum, 1, 1);
			max3421_ep->pkt_state = PKT_STATE_SETUP;
			force_toggles = 1;
		} else
			max3421_ep->pkt_state = PKT_STATE_TRANSFER;
	}

	spin_unlock_irqrestore(&max3421_hcd->lock, flags);

	max3421_ep->last_active = max3421_hcd->frame_number;
	max3421_set_address(hcd, urb->dev, epnum, force_toggles);
	max3421_set_speed(hcd, urb->dev);
	max3421_next_transfer(hcd, 0);
	return 1;
}

/*
 * Check all endpoints for URBs that got unlinked.
 *
 * Caller must NOT hold HCD spinlock.
 */
static int
max3421_check_unlink(struct usb_hcd *hcd)
{
	struct spi_device *spi = to_spi_device(hcd->self.controller);
	struct max3421_hcd *max3421_hcd = hcd_to_max3421(hcd);
	struct list_head *pos, *upos, *next_upos;
	struct max3421_ep *max3421_ep;
	struct usb_host_endpoint *ep;
	struct urb *urb;
	unsigned long flags;
	int retval = 0;

	spin_lock_irqsave(&max3421_hcd->lock, flags);
	list_for_each(pos, &max3421_hcd->ep_list) {
		max3421_ep = container_of(pos, struct max3421_ep, ep_list);
		ep = max3421_ep->ep;
		list_for_each_safe(upos, next_upos, &ep->urb_list) {
			urb = container_of(upos, struct urb, urb_list);
			if (urb->unlinked) {
				retval = 1;
				dev_dbg(&spi->dev, "%s: URB %p unlinked=%d",
					__func__, urb, urb->unlinked);
				usb_hcd_unlink_urb_from_ep(hcd, urb);
				spin_unlock_irqrestore(&max3421_hcd->lock,
						       flags);
				usb_hcd_giveback_urb(hcd, urb, 0);
				spin_lock_irqsave(&max3421_hcd->lock, flags);
			}
		}
	}
	spin_unlock_irqrestore(&max3421_hcd->lock, flags);
	return retval;
}

/*
 * Caller must NOT hold HCD spinlock.
 */
static void
max3421_slow_retransmit(struct usb_hcd *hcd)
{
	struct max3421_hcd *max3421_hcd = hcd_to_max3421(hcd);
	struct urb *urb = max3421_hcd->curr_urb;
	struct max3421_ep *max3421_ep;

	max3421_ep = urb->ep->hcpriv;
	max3421_ep->retransmit = 1;
	max3421_hcd->curr_urb = NULL;
}

/*
 * Caller must NOT hold HCD spinlock.
 */
static void
max3421_recv_data_available(struct usb_hcd *hcd)
{
	struct max3421_hcd *max3421_hcd = hcd_to_max3421(hcd);
	struct urb *urb = max3421_hcd->curr_urb;
	size_t remaining, transfer_size;
	u8 rcvbc;

	rcvbc = spi_rd8(hcd, MAX3421_REG_RCVBC);

	if (rcvbc > MAX3421_FIFO_SIZE)
		rcvbc = MAX3421_FIFO_SIZE;
	if (urb->actual_length >= urb->transfer_buffer_length)
		remaining = 0;
	else
		remaining = urb->transfer_buffer_length - urb->actual_length;
	transfer_size = rcvbc;
	if (transfer_size > remaining)
		transfer_size = remaining;
	if (transfer_size > 0) {
		void *dst = urb->transfer_buffer + urb->actual_length;

		spi_rd_buf(hcd, MAX3421_REG_RCVFIFO, dst, transfer_size);
		urb->actual_length += transfer_size;
		max3421_hcd->curr_len = transfer_size;
	}

	/* ack the RCVDAV irq now that the FIFO has been read: */
	spi_wr8(hcd, MAX3421_REG_HIRQ, BIT(MAX3421_HI_RCVDAV_BIT));
}

static void
max3421_handle_error(struct usb_hcd *hcd, u8 hrsl)
{
	struct spi_device *spi = to_spi_device(hcd->self.controller);
	struct max3421_hcd *max3421_hcd = hcd_to_max3421(hcd);
	u8 result_code = hrsl & MAX3421_HRSL_RESULT_MASK;
	struct urb *urb = max3421_hcd->curr_urb;
	struct max3421_ep *max3421_ep = urb->ep->hcpriv;
	int switch_sndfifo;

	/*
	 * If an OUT command results in any response other than OK
	 * (i.e., error or NAK), we have to perform a dummy-write to
	 * SNDBC so the FIFO gets switched back to us.  Otherwise, we
	 * get out of sync with the SNDFIFO double buffer.
	 */
	switch_sndfifo = (max3421_ep->pkt_state == PKT_STATE_TRANSFER &&
			  usb_urb_dir_out(urb));

	switch (result_code) {
	case MAX3421_HRSL_OK:
		return;			/* this shouldn't happen */

	case MAX3421_HRSL_WRONGPID:	/* received wrong PID */
	case MAX3421_HRSL_BUSY:		/* SIE busy */
	case MAX3421_HRSL_BADREQ:	/* bad val in HXFR */
	case MAX3421_HRSL_UNDEF:	/* reserved */
	case MAX3421_HRSL_KERR:		/* K-state instead of response */
	case MAX3421_HRSL_JERR:		/* J-state instead of response */
		/*
		 * packet experienced an error that we cannot recover
		 * from; report error
		 */
		max3421_hcd->urb_done = hrsl_to_error[result_code];
		dev_dbg(&spi->dev, "%s: unexpected error HRSL=0x%02x",
			__func__, hrsl);
		break;

	case MAX3421_HRSL_TOGERR:
		if (usb_urb_dir_in(urb))
			; /* don't do anything (device will switch toggle) */
		else {
			/* flip the send toggle bit: */
			int sndtog = (hrsl >> MAX3421_HRSL_SNDTOGRD_BIT) & 1;

			sndtog ^= 1;
			spi_wr8(hcd, MAX3421_REG_HCTL,
				BIT(sndtog + MAX3421_HCTL_SNDTOG0_BIT));
		}
		/* FALL THROUGH */
	case MAX3421_HRSL_BADBC:	/* bad byte count */
	case MAX3421_HRSL_PIDERR:	/* received PID is corrupted */
	case MAX3421_HRSL_PKTERR:	/* packet error (stuff, EOP) */
	case MAX3421_HRSL_CRCERR:	/* CRC error */
	case MAX3421_HRSL_BABBLE:	/* device talked too long */
	case MAX3421_HRSL_TIMEOUT:
		if (max3421_ep->retries++ < USB_MAX_RETRIES)
			/* retry the packet again in the next frame */
			max3421_slow_retransmit(hcd);
		else {
			/* Based on ohci.h cc_to_err[]: */
			max3421_hcd->urb_done = hrsl_to_error[result_code];
			dev_dbg(&spi->dev, "%s: unexpected error HRSL=0x%02x",
				__func__, hrsl);
		}
		break;

	case MAX3421_HRSL_STALL:
		dev_dbg(&spi->dev, "%s: unexpected error HRSL=0x%02x",
			__func__, hrsl);
		max3421_hcd->urb_done = hrsl_to_error[result_code];
		break;

	case MAX3421_HRSL_NAK:
		/*
		 * Device wasn't ready for data or has no data
		 * available: retry the packet again.
		 */
		if (max3421_ep->naks++ < NAK_MAX_FAST_RETRANSMITS) {
			max3421_next_transfer(hcd, 1);
			switch_sndfifo = 0;
		} else
			max3421_slow_retransmit(hcd);
		break;
	}
	if (switch_sndfifo)
		spi_wr8(hcd, MAX3421_REG_SNDBC, 0);
}

/*
 * Caller must NOT hold HCD spinlock.
 */
static int
max3421_transfer_in_done(struct usb_hcd *hcd, struct urb *urb)
{
	struct spi_device *spi = to_spi_device(hcd->self.controller);
	struct max3421_hcd *max3421_hcd = hcd_to_max3421(hcd);
	u32 max_packet;

	if (urb->actual_length >= urb->transfer_buffer_length)
		return 1;	/* read is complete, so we're done */

	/*
	 * USB 2.0 Section 5.3.2 Pipes: packets must be full size
	 * except for last one.
	 */
	max_packet = usb_maxpacket(urb->dev, urb->pipe, 0);
	if (max_packet > MAX3421_FIFO_SIZE) {
		/*
		 * We do not support isochronous transfers at this
		 * time...
		 */
		dev_err(&spi->dev,
			"%s: packet-size of %u too big (limit is %u bytes)",
			__func__, max_packet, MAX3421_FIFO_SIZE);
		return -EINVAL;
	}

	if (max3421_hcd->curr_len < max_packet) {
		if (urb->transfer_flags & URB_SHORT_NOT_OK) {
			/*
			 * remaining > 0 and received an
			 * unexpected partial packet ->
			 * error
			 */
			return -EREMOTEIO;
		} else
			/* short read, but it's OK */
			return 1;
	}
	return 0;	/* not done */
}

/*
 * Caller must NOT hold HCD spinlock.
 */
static int
max3421_transfer_out_done(struct usb_hcd *hcd, struct urb *urb)
{
	struct max3421_hcd *max3421_hcd = hcd_to_max3421(hcd);

	urb->actual_length += max3421_hcd->curr_len;
	if (urb->actual_length < urb->transfer_buffer_length)
		return 0;
	if (urb->transfer_flags & URB_ZERO_PACKET) {
		/*
		 * Some hardware needs a zero-size packet at the end
		 * of a bulk-out transfer if the last transfer was a
		 * full-sized packet (i.e., such hardware use <
		 * max_packet as an indicator that the end of the
		 * packet has been reached).
		 */
		u32 max_packet = usb_maxpacket(urb->dev, urb->pipe, 1);

		if (max3421_hcd->curr_len == max_packet)
			return 0;
	}
	return 1;
}

/*
 * Caller must NOT hold HCD spinlock.
 */
static void
max3421_host_transfer_done(struct usb_hcd *hcd)
{
	struct max3421_hcd *max3421_hcd = hcd_to_max3421(hcd);
	struct urb *urb = max3421_hcd->curr_urb;
	struct max3421_ep *max3421_ep;
	u8 result_code, hrsl;
	int urb_done = 0;

	max3421_hcd->hien &= ~(BIT(MAX3421_HI_HXFRDN_BIT) |
			       BIT(MAX3421_HI_RCVDAV_BIT));

	hrsl = spi_rd8(hcd, MAX3421_REG_HRSL);
	result_code = hrsl & MAX3421_HRSL_RESULT_MASK;

#ifdef DEBUG
	++max3421_hcd->err_stat[result_code];
#endif

	max3421_ep = urb->ep->hcpriv;

	if (unlikely(result_code != MAX3421_HRSL_OK)) {
		max3421_handle_error(hcd, hrsl);
		return;
	}

	max3421_ep->naks = 0;
	max3421_ep->retries = 0;
	switch (max3421_ep->pkt_state) {

	case PKT_STATE_SETUP:
		if (urb->transfer_buffer_length > 0)
			max3421_ep->pkt_state = PKT_STATE_TRANSFER;
		else
			max3421_ep->pkt_state = PKT_STATE_TERMINATE;
		break;

	case PKT_STATE_TRANSFER:
		if (usb_urb_dir_in(urb))
			urb_done = max3421_transfer_in_done(hcd, urb);
		else
			urb_done = max3421_transfer_out_done(hcd, urb);
		if (urb_done > 0 && usb_pipetype(urb->pipe) == PIPE_CONTROL) {
			/*
			 * We aren't really done - we still need to
			 * terminate the control transfer:
			 */
			max3421_hcd->urb_done = urb_done = 0;
			max3421_ep->pkt_state = PKT_STATE_TERMINATE;
		}
		break;

	case PKT_STATE_TERMINATE:
		urb_done = 1;
		break;
	}

	if (urb_done)
		max3421_hcd->urb_done = urb_done;
	else
		max3421_next_transfer(hcd, 0);
}

/*
 * Caller must NOT hold HCD spinlock.
 */
static void
max3421_detect_conn(struct usb_hcd *hcd)
{
	struct max3421_hcd *max3421_hcd = hcd_to_max3421(hcd);
	unsigned int jk, have_conn = 0;
	u32 old_port_status, chg;
	unsigned long flags;
	u8 hrsl, mode;

	hrsl = spi_rd8(hcd, MAX3421_REG_HRSL);

	jk = ((((hrsl >> MAX3421_HRSL_JSTATUS_BIT) & 1) << 0) |
	      (((hrsl >> MAX3421_HRSL_KSTATUS_BIT) & 1) << 1));

	mode = max3421_hcd->mode;

	switch (jk) {
	case 0x0: /* SE0: disconnect */
		/*
		 * Turn off SOFKAENAB bit to avoid getting interrupt
		 * every milli-second:
		 */
		mode &= ~BIT(MAX3421_MODE_SOFKAENAB_BIT);
		break;

	case 0x1: /* J=0,K=1: low-speed (in full-speed or vice versa) */
	case 0x2: /* J=1,K=0: full-speed (in full-speed or vice versa) */
		if (jk == 0x2)
			/* need to switch to the other speed: */
			mode ^= BIT(MAX3421_MODE_LOWSPEED_BIT);
		/* turn on SOFKAENAB bit: */
		mode |= BIT(MAX3421_MODE_SOFKAENAB_BIT);
		have_conn = 1;
		break;

	case 0x3: /* illegal */
		break;
	}

	max3421_hcd->mode = mode;
	spi_wr8(hcd, MAX3421_REG_MODE, max3421_hcd->mode);

	spin_lock_irqsave(&max3421_hcd->lock, flags);
	old_port_status = max3421_hcd->port_status;
	if (have_conn)
		max3421_hcd->port_status |=  USB_PORT_STAT_CONNECTION;
	else
		max3421_hcd->port_status &= ~USB_PORT_STAT_CONNECTION;
	if (mode & BIT(MAX3421_MODE_LOWSPEED_BIT))
		max3421_hcd->port_status |=  USB_PORT_STAT_LOW_SPEED;
	else
		max3421_hcd->port_status &= ~USB_PORT_STAT_LOW_SPEED;
	chg = (old_port_status ^ max3421_hcd->port_status);
	max3421_hcd->port_status |= chg << 16;
	spin_unlock_irqrestore(&max3421_hcd->lock, flags);
}

static irqreturn_t
max3421_irq_handler(int irq, void *dev_id)
{
	struct usb_hcd *hcd = dev_id;
	struct spi_device *spi = to_spi_device(hcd->self.controller);
	struct max3421_hcd *max3421_hcd = hcd_to_max3421(hcd);

	if (max3421_hcd->spi_thread &&
	    max3421_hcd->spi_thread->state != TASK_RUNNING)
		wake_up_process(max3421_hcd->spi_thread);
	if (!test_and_set_bit(ENABLE_IRQ, &max3421_hcd->todo))
		disable_irq_nosync(spi->irq);
	return IRQ_HANDLED;
}

#ifdef DEBUG

static void
dump_eps(struct usb_hcd *hcd)
{
	struct max3421_hcd *max3421_hcd = hcd_to_max3421(hcd);
	struct max3421_ep *max3421_ep;
	struct usb_host_endpoint *ep;
	struct list_head *pos, *upos;
	char ubuf[512], *dp, *end;
	unsigned long flags;
	struct urb *urb;
	int epnum, ret;

	spin_lock_irqsave(&max3421_hcd->lock, flags);
	list_for_each(pos, &max3421_hcd->ep_list) {
		max3421_ep = container_of(pos, struct max3421_ep, ep_list);
		ep = max3421_ep->ep;

		dp = ubuf;
		end = dp + sizeof(ubuf);
		*dp = '\0';
		list_for_each(upos, &ep->urb_list) {
			urb = container_of(upos, struct urb, urb_list);
			ret = snprintf(dp, end - dp, " %p(%d.%s %d/%d)", urb,
				       usb_pipetype(urb->pipe),
				       usb_urb_dir_in(urb) ? "IN" : "OUT",
				       urb->actual_length,
				       urb->transfer_buffer_length);
			if (ret < 0 || ret >= end - dp)
				break;	/* error or buffer full */
			dp += ret;
		}

		epnum = usb_endpoint_num(&ep->desc);
		pr_info("EP%0u %u lst %04u rtr %u nak %6u rxmt %u: %s\n",
			epnum, max3421_ep->pkt_state, max3421_ep->last_active,
			max3421_ep->retries, max3421_ep->naks,
			max3421_ep->retransmit, ubuf);
	}
	spin_unlock_irqrestore(&max3421_hcd->lock, flags);
}

#endif /* DEBUG */

/* Return zero if no work was performed, 1 otherwise.  */
static int
max3421_handle_irqs(struct usb_hcd *hcd)
{
	struct max3421_hcd *max3421_hcd = hcd_to_max3421(hcd);
	u32 chg, old_port_status;
	unsigned long flags;
	u8 hirq;

	/*
	 * Read and ack pending interrupts (CPU must never
	 * clear SNDBAV directly and RCVDAV must be cleared by
	 * max3421_recv_data_available()!):
	 */
	hirq = spi_rd8(hcd, MAX3421_REG_HIRQ);
	hirq &= max3421_hcd->hien;
	if (!hirq)
		return 0;

	spi_wr8(hcd, MAX3421_REG_HIRQ,
		hirq & ~(BIT(MAX3421_HI_SNDBAV_BIT) |
			 BIT(MAX3421_HI_RCVDAV_BIT)));

	if (hirq & BIT(MAX3421_HI_FRAME_BIT)) {
		max3421_hcd->frame_number = ((max3421_hcd->frame_number + 1)
					     & USB_MAX_FRAME_NUMBER);
		max3421_hcd->sched_pass = SCHED_PASS_PERIODIC;
	}

	if (hirq & BIT(MAX3421_HI_RCVDAV_BIT))
		max3421_recv_data_available(hcd);

	if (hirq & BIT(MAX3421_HI_HXFRDN_BIT))
		max3421_host_transfer_done(hcd);

	if (hirq & BIT(MAX3421_HI_CONDET_BIT))
		max3421_detect_conn(hcd);

	/*
	 * Now process interrupts that may affect HCD state
	 * other than the end-points:
	 */
	spin_lock_irqsave(&max3421_hcd->lock, flags);

	old_port_status = max3421_hcd->port_status;
	if (hirq & BIT(MAX3421_HI_BUSEVENT_BIT)) {
		if (max3421_hcd->port_status & USB_PORT_STAT_RESET) {
			/* BUSEVENT due to completion of Bus Reset */
			max3421_hcd->port_status &= ~USB_PORT_STAT_RESET;
			max3421_hcd->port_status |=  USB_PORT_STAT_ENABLE;
		} else {
			/* BUSEVENT due to completion of Bus Resume */
			pr_info("%s: BUSEVENT Bus Resume Done\n", __func__);
		}
	}
	if (hirq & BIT(MAX3421_HI_RWU_BIT))
		pr_info("%s: RWU\n", __func__);
	if (hirq & BIT(MAX3421_HI_SUSDN_BIT))
		pr_info("%s: SUSDN\n", __func__);

	chg = (old_port_status ^ max3421_hcd->port_status);
	max3421_hcd->port_status |= chg << 16;

	spin_unlock_irqrestore(&max3421_hcd->lock, flags);

#ifdef DEBUG
	{
		static unsigned long last_time;
		char sbuf[16 * 16], *dp, *end;
		int i;

		if (time_after(jiffies, last_time + 5*HZ)) {
			dp = sbuf;
			end = sbuf + sizeof(sbuf);
			*dp = '\0';
			for (i = 0; i < 16; ++i) {
				int ret = snprintf(dp, end - dp, " %lu",
						   max3421_hcd->err_stat[i]);
				if (ret < 0 || ret >= end - dp)
					break;	/* error or buffer full */
				dp += ret;
			}
			pr_info("%s: hrsl_stats %s\n", __func__, sbuf);
			memset(max3421_hcd->err_stat, 0,
			       sizeof(max3421_hcd->err_stat));
			last_time = jiffies;

			dump_eps(hcd);
		}
	}
#endif
	return 1;
}

static int
max3421_reset_hcd(struct usb_hcd *hcd)
{
	struct spi_device *spi = to_spi_device(hcd->self.controller);
	struct max3421_hcd *max3421_hcd = hcd_to_max3421(hcd);
	int timeout;

	/* perform a chip reset and wait for OSCIRQ signal to appear: */
	spi_wr8(hcd, MAX3421_REG_USBCTL, BIT(MAX3421_USBCTL_CHIPRES_BIT));
	/* clear reset: */
	spi_wr8(hcd, MAX3421_REG_USBCTL, 0);
	timeout = 1000;
	while (1) {
		if (spi_rd8(hcd, MAX3421_REG_USBIRQ)
		    & BIT(MAX3421_USBIRQ_OSCOKIRQ_BIT))
			break;
		if (--timeout < 0) {
			dev_err(&spi->dev,
				"timed out waiting for oscillator OK signal");
			return 1;
		}
		cond_resched();
	}

	/*
	 * Turn on host mode, automatic generation of SOF packets, and
	 * enable pull-down registers on DM/DP:
	 */
	max3421_hcd->mode = (BIT(MAX3421_MODE_HOST_BIT) |
			     BIT(MAX3421_MODE_SOFKAENAB_BIT) |
			     BIT(MAX3421_MODE_DMPULLDN_BIT) |
			     BIT(MAX3421_MODE_DPPULLDN_BIT));
	spi_wr8(hcd, MAX3421_REG_MODE, max3421_hcd->mode);

	/* reset frame-number: */
	max3421_hcd->frame_number = USB_MAX_FRAME_NUMBER;
	spi_wr8(hcd, MAX3421_REG_HCTL, BIT(MAX3421_HCTL_FRMRST_BIT));

	/* sample the state of the D+ and D- lines */
	spi_wr8(hcd, MAX3421_REG_HCTL, BIT(MAX3421_HCTL_SAMPLEBUS_BIT));
	max3421_detect_conn(hcd);

	/* enable frame, connection-detected, and bus-event interrupts: */
	max3421_hcd->hien = (BIT(MAX3421_HI_FRAME_BIT) |
			     BIT(MAX3421_HI_CONDET_BIT) |
			     BIT(MAX3421_HI_BUSEVENT_BIT));
	spi_wr8(hcd, MAX3421_REG_HIEN, max3421_hcd->hien);

	/* enable interrupts: */
	spi_wr8(hcd, MAX3421_REG_CPUCTL, BIT(MAX3421_CPUCTL_IE_BIT));
	return 1;
}

static int
max3421_urb_done(struct usb_hcd *hcd)
{
	struct max3421_hcd *max3421_hcd = hcd_to_max3421(hcd);
	unsigned long flags;
	struct urb *urb;
	int status;

	status = max3421_hcd->urb_done;
	max3421_hcd->urb_done = 0;
	if (status > 0)
		status = 0;
	urb = max3421_hcd->curr_urb;
	if (urb) {
		max3421_hcd->curr_urb = NULL;
		spin_lock_irqsave(&max3421_hcd->lock, flags);
		usb_hcd_unlink_urb_from_ep(hcd, urb);
		spin_unlock_irqrestore(&max3421_hcd->lock, flags);

		/* must be called without the HCD spinlock: */
		usb_hcd_giveback_urb(hcd, urb, status);
	}
	return 1;
}

static int
max3421_spi_thread(void *dev_id)
{
	struct usb_hcd *hcd = dev_id;
	struct spi_device *spi = to_spi_device(hcd->self.controller);
	struct max3421_hcd *max3421_hcd = hcd_to_max3421(hcd);
	int i, i_worked = 1;

	/* set full-duplex SPI mode, low-active interrupt pin: */
	spi_wr8(hcd, MAX3421_REG_PINCTL,
		(BIT(MAX3421_PINCTL_FDUPSPI_BIT) |	/* full-duplex */
		 BIT(MAX3421_PINCTL_INTLEVEL_BIT)));	/* low-active irq */

	while (!kthread_should_stop()) {
		max3421_hcd->rev = spi_rd8(hcd, MAX3421_REG_REVISION);
		if (max3421_hcd->rev == 0x12 || max3421_hcd->rev == 0x13)
			break;
		dev_err(&spi->dev, "bad rev 0x%02x", max3421_hcd->rev);
		msleep(10000);
	}
	dev_info(&spi->dev, "rev 0x%x, SPI clk %dHz, bpw %u, irq %d\n",
		 max3421_hcd->rev, spi->max_speed_hz, spi->bits_per_word,
		 spi->irq);

	while (!kthread_should_stop()) {
		if (!i_worked) {
			/*
			 * We'll be waiting for wakeups from the hard
			 * interrupt handler, so now is a good time to
			 * sync our hien with the chip:
			 */
			spi_wr8(hcd, MAX3421_REG_HIEN, max3421_hcd->hien);

			set_current_state(TASK_INTERRUPTIBLE);
			if (test_and_clear_bit(ENABLE_IRQ, &max3421_hcd->todo))
				enable_irq(spi->irq);
			schedule();
			__set_current_state(TASK_RUNNING);
		}

		i_worked = 0;

		if (max3421_hcd->urb_done)
			i_worked |= max3421_urb_done(hcd);
		else if (max3421_handle_irqs(hcd))
			i_worked = 1;
		else if (!max3421_hcd->curr_urb)
			i_worked |= max3421_select_and_start_urb(hcd);

		if (test_and_clear_bit(RESET_HCD, &max3421_hcd->todo))
			/* reset the HCD: */
			i_worked |= max3421_reset_hcd(hcd);
		if (test_and_clear_bit(RESET_PORT, &max3421_hcd->todo)) {
			/* perform a USB bus reset: */
			spi_wr8(hcd, MAX3421_REG_HCTL,
				BIT(MAX3421_HCTL_BUSRST_BIT));
			i_worked = 1;
		}
		if (test_and_clear_bit(CHECK_UNLINK, &max3421_hcd->todo))
			i_worked |= max3421_check_unlink(hcd);
		if (test_and_clear_bit(IOPIN_UPDATE, &max3421_hcd->todo)) {
			/*
			 * IOPINS1/IOPINS2 do not auto-increment, so we can't
			 * use spi_wr_buf().
			 */
			for (i = 0; i < ARRAY_SIZE(max3421_hcd->iopins); ++i) {
				u8 val = spi_rd8(hcd, MAX3421_REG_IOPINS1);

				val = ((val & 0xf0) |
				       (max3421_hcd->iopins[i] & 0x0f));
				spi_wr8(hcd, MAX3421_REG_IOPINS1 + i, val);
				max3421_hcd->iopins[i] = val;
			}
			i_worked = 1;
		}
	}
	set_current_state(TASK_RUNNING);
	dev_info(&spi->dev, "SPI thread exiting");
	return 0;
}

static int
max3421_reset_port(struct usb_hcd *hcd)
{
	struct max3421_hcd *max3421_hcd = hcd_to_max3421(hcd);

	max3421_hcd->port_status &= ~(USB_PORT_STAT_ENABLE |
				      USB_PORT_STAT_LOW_SPEED);
	max3421_hcd->port_status |= USB_PORT_STAT_RESET;
	set_bit(RESET_PORT, &max3421_hcd->todo);
	wake_up_process(max3421_hcd->spi_thread);
	return 0;
}

static int
max3421_reset(struct usb_hcd *hcd)
{
	struct max3421_hcd *max3421_hcd = hcd_to_max3421(hcd);

	hcd->self.sg_tablesize = 0;
	hcd->speed = HCD_USB2;
	hcd->self.root_hub->speed = USB_SPEED_FULL;
	set_bit(RESET_HCD, &max3421_hcd->todo);
	wake_up_process(max3421_hcd->spi_thread);
	return 0;
}

static int
max3421_start(struct usb_hcd *hcd)
{
	struct max3421_hcd *max3421_hcd = hcd_to_max3421(hcd);

	spin_lock_init(&max3421_hcd->lock);
	max3421_hcd->rh_state = MAX3421_RH_RUNNING;

	INIT_LIST_HEAD(&max3421_hcd->ep_list);

	hcd->power_budget = POWER_BUDGET;
	hcd->state = HC_STATE_RUNNING;
	hcd->uses_new_polling = 1;
	return 0;
}

static void
max3421_stop(struct usb_hcd *hcd)
{
}

static int
max3421_urb_enqueue(struct usb_hcd *hcd, struct urb *urb, gfp_t mem_flags)
{
	struct spi_device *spi = to_spi_device(hcd->self.controller);
	struct max3421_hcd *max3421_hcd = hcd_to_max3421(hcd);
	struct max3421_ep *max3421_ep;
	unsigned long flags;
	int retval;

	switch (usb_pipetype(urb->pipe)) {
	case PIPE_INTERRUPT:
	case PIPE_ISOCHRONOUS:
		if (urb->interval < 0) {
			dev_err(&spi->dev,
			  "%s: interval=%d for intr-/iso-pipe; expected > 0\n",
				__func__, urb->interval);
			return -EINVAL;
		}
	default:
		break;
	}

	spin_lock_irqsave(&max3421_hcd->lock, flags);

	max3421_ep = urb->ep->hcpriv;
	if (!max3421_ep) {
		/* gets freed in max3421_endpoint_disable: */
		max3421_ep = kzalloc(sizeof(struct max3421_ep), GFP_ATOMIC);
		if (!max3421_ep) {
			retval = -ENOMEM;
			goto out;
		}
		max3421_ep->ep = urb->ep;
		max3421_ep->last_active = max3421_hcd->frame_number;
		urb->ep->hcpriv = max3421_ep;

		list_add_tail(&max3421_ep->ep_list, &max3421_hcd->ep_list);
	}

	retval = usb_hcd_link_urb_to_ep(hcd, urb);
	if (retval == 0) {
		/* Since we added to the queue, restart scheduling: */
		max3421_hcd->sched_pass = SCHED_PASS_PERIODIC;
		wake_up_process(max3421_hcd->spi_thread);
	}

out:
	spin_unlock_irqrestore(&max3421_hcd->lock, flags);
	return retval;
}

static int
max3421_urb_dequeue(struct usb_hcd *hcd, struct urb *urb, int status)
{
	struct max3421_hcd *max3421_hcd = hcd_to_max3421(hcd);
	unsigned long flags;
	int retval;

	spin_lock_irqsave(&max3421_hcd->lock, flags);

	/*
	 * This will set urb->unlinked which in turn causes the entry
	 * to be dropped at the next opportunity.
	 */
	retval = usb_hcd_check_unlink_urb(hcd, urb, status);
	if (retval == 0) {
		set_bit(CHECK_UNLINK, &max3421_hcd->todo);
		wake_up_process(max3421_hcd->spi_thread);
	}
	spin_unlock_irqrestore(&max3421_hcd->lock, flags);
	return retval;
}

static void
max3421_endpoint_disable(struct usb_hcd *hcd, struct usb_host_endpoint *ep)
{
	struct max3421_hcd *max3421_hcd = hcd_to_max3421(hcd);
	unsigned long flags;

	spin_lock_irqsave(&max3421_hcd->lock, flags);

	if (ep->hcpriv) {
		struct max3421_ep *max3421_ep = ep->hcpriv;

		/* remove myself from the ep_list: */
		if (!list_empty(&max3421_ep->ep_list))
			list_del(&max3421_ep->ep_list);
		kfree(max3421_ep);
		ep->hcpriv = NULL;
	}

	spin_unlock_irqrestore(&max3421_hcd->lock, flags);
}

static int
max3421_get_frame_number(struct usb_hcd *hcd)
{
	struct max3421_hcd *max3421_hcd = hcd_to_max3421(hcd);
	return max3421_hcd->frame_number;
}

/*
 * Should return a non-zero value when any port is undergoing a resume
 * transition while the root hub is suspended.
 */
static int
max3421_hub_status_data(struct usb_hcd *hcd, char *buf)
{
	struct max3421_hcd *max3421_hcd = hcd_to_max3421(hcd);
	unsigned long flags;
	int retval = 0;

	spin_lock_irqsave(&max3421_hcd->lock, flags);
	if (!HCD_HW_ACCESSIBLE(hcd))
		goto done;

	*buf = 0;
	if ((max3421_hcd->port_status & PORT_C_MASK) != 0) {
		*buf = (1 << 1); /* a hub over-current condition exists */
		dev_dbg(hcd->self.controller,
			"port status 0x%08x has changes\n",
			max3421_hcd->port_status);
		retval = 1;
		if (max3421_hcd->rh_state == MAX3421_RH_SUSPENDED)
			usb_hcd_resume_root_hub(hcd);
	}
done:
	spin_unlock_irqrestore(&max3421_hcd->lock, flags);
	return retval;
}

static inline void
hub_descriptor(struct usb_hub_descriptor *desc)
{
	memset(desc, 0, sizeof(*desc));
	/*
	 * See Table 11-13: Hub Descriptor in USB 2.0 spec.
	 */
	desc->bDescriptorType = USB_DT_HUB; /* hub descriptor */
	desc->bDescLength = 9;
	desc->wHubCharacteristics = cpu_to_le16(HUB_CHAR_INDV_PORT_LPSM |
						HUB_CHAR_COMMON_OCPM);
	desc->bNbrPorts = 1;
}

/*
 * Set the MAX3421E general-purpose output with number PIN_NUMBER to
 * VALUE (0 or 1).  PIN_NUMBER may be in the range from 1-8.  For
 * any other value, this function acts as a no-op.
 */
static void
max3421_gpout_set_value(struct usb_hcd *hcd, u8 pin_number, u8 value)
{
	struct max3421_hcd *max3421_hcd = hcd_to_max3421(hcd);
	u8 mask, idx;

	--pin_number;
	if (pin_number > 7)
		return;

	mask = 1u << pin_number;
	idx = pin_number / 4;

	if (value)
		max3421_hcd->iopins[idx] |=  mask;
	else
		max3421_hcd->iopins[idx] &= ~mask;
	set_bit(IOPIN_UPDATE, &max3421_hcd->todo);
	wake_up_process(max3421_hcd->spi_thread);
}

static int
max3421_hub_control(struct usb_hcd *hcd, u16 type_req, u16 value, u16 index,
		    char *buf, u16 length)
{
	struct spi_device *spi = to_spi_device(hcd->self.controller);
	struct max3421_hcd *max3421_hcd = hcd_to_max3421(hcd);
	struct max3421_hcd_platform_data *pdata;
	unsigned long flags;
	int retval = 0;

	spin_lock_irqsave(&max3421_hcd->lock, flags);

	pdata = spi->dev.platform_data;

	switch (type_req) {
	case ClearHubFeature:
		break;
	case ClearPortFeature:
		switch (value) {
		case USB_PORT_FEAT_SUSPEND:
			break;
		case USB_PORT_FEAT_POWER:
			dev_dbg(hcd->self.controller, "power-off\n");
			max3421_gpout_set_value(hcd, pdata->vbus_gpout,
						!pdata->vbus_active_level);
			/* FALLS THROUGH */
		default:
			max3421_hcd->port_status &= ~(1 << value);
		}
		break;
	case GetHubDescriptor:
		hub_descriptor((struct usb_hub_descriptor *) buf);
		break;

	case DeviceRequest | USB_REQ_GET_DESCRIPTOR:
	case GetPortErrorCount:
	case SetHubDepth:
		/* USB3 only */
		goto error;

	case GetHubStatus:
		*(__le32 *) buf = cpu_to_le32(0);
		break;

	case GetPortStatus:
		if (index != 1) {
			retval = -EPIPE;
			goto error;
		}
		((__le16 *) buf)[0] = cpu_to_le16(max3421_hcd->port_status);
		((__le16 *) buf)[1] =
			cpu_to_le16(max3421_hcd->port_status >> 16);
		break;

	case SetHubFeature:
		retval = -EPIPE;
		break;

	case SetPortFeature:
		switch (value) {
		case USB_PORT_FEAT_LINK_STATE:
		case USB_PORT_FEAT_U1_TIMEOUT:
		case USB_PORT_FEAT_U2_TIMEOUT:
		case USB_PORT_FEAT_BH_PORT_RESET:
			goto error;
		case USB_PORT_FEAT_SUSPEND:
			if (max3421_hcd->active)
				max3421_hcd->port_status |=
					USB_PORT_STAT_SUSPEND;
			break;
		case USB_PORT_FEAT_POWER:
			dev_dbg(hcd->self.controller, "power-on\n");
			max3421_hcd->port_status |= USB_PORT_STAT_POWER;
			max3421_gpout_set_value(hcd, pdata->vbus_gpout,
						pdata->vbus_active_level);
			break;
		case USB_PORT_FEAT_RESET:
			max3421_reset_port(hcd);
			/* FALLS THROUGH */
		default:
			if ((max3421_hcd->port_status & USB_PORT_STAT_POWER)
			    != 0)
				max3421_hcd->port_status |= (1 << value);
		}
		break;

	default:
		dev_dbg(hcd->self.controller,
			"hub control req%04x v%04x i%04x l%d\n",
			type_req, value, index, length);
error:		/* "protocol stall" on error */
		retval = -EPIPE;
	}

	spin_unlock_irqrestore(&max3421_hcd->lock, flags);
	return retval;
}

static int
max3421_bus_suspend(struct usb_hcd *hcd)
{
	return -1;
}

static int
max3421_bus_resume(struct usb_hcd *hcd)
{
	return -1;
}

/*
 * The SPI driver already takes care of DMA-mapping/unmapping, so no
 * reason to do it twice.
 */
static int
max3421_map_urb_for_dma(struct usb_hcd *hcd, struct urb *urb, gfp_t mem_flags)
{
	return 0;
}

static void
max3421_unmap_urb_for_dma(struct usb_hcd *hcd, struct urb *urb)
{
}

static struct hc_driver max3421_hcd_desc = {
	.description =		"max3421",
	.product_desc =		DRIVER_DESC,
	.hcd_priv_size =	sizeof(struct max3421_hcd),
	.flags =		HCD_USB11,
	.reset =		max3421_reset,
	.start =		max3421_start,
	.stop =			max3421_stop,
	.get_frame_number =	max3421_get_frame_number,
	.urb_enqueue =		max3421_urb_enqueue,
	.urb_dequeue =		max3421_urb_dequeue,
	.map_urb_for_dma =	max3421_map_urb_for_dma,
	.unmap_urb_for_dma =	max3421_unmap_urb_for_dma,
	.endpoint_disable =	max3421_endpoint_disable,
	.hub_status_data =	max3421_hub_status_data,
	.hub_control =		max3421_hub_control,
	.bus_suspend =		max3421_bus_suspend,
	.bus_resume =		max3421_bus_resume,
};

static int
max3421_probe(struct spi_device *spi)
{
	struct max3421_hcd *max3421_hcd;
	struct usb_hcd *hcd = NULL;
	int retval = -ENOMEM;

	if (spi_setup(spi) < 0) {
		dev_err(&spi->dev, "Unable to setup SPI bus");
		return -EFAULT;
	}

	hcd = usb_create_hcd(&max3421_hcd_desc, &spi->dev,
			     dev_name(&spi->dev));
	if (!hcd) {
		dev_err(&spi->dev, "failed to create HCD structure\n");
		goto error;
	}
	set_bit(HCD_FLAG_POLL_RH, &hcd->flags);
	max3421_hcd = hcd_to_max3421(hcd);
	max3421_hcd->next = max3421_hcd_list;
	max3421_hcd_list = max3421_hcd;
	INIT_LIST_HEAD(&max3421_hcd->ep_list);

	max3421_hcd->tx = kmalloc(sizeof(*max3421_hcd->tx), GFP_KERNEL);
	if (!max3421_hcd->tx) {
		dev_err(&spi->dev, "failed to kmalloc tx buffer\n");
		goto error;
	}
	max3421_hcd->rx = kmalloc(sizeof(*max3421_hcd->rx), GFP_KERNEL);
	if (!max3421_hcd->rx) {
		dev_err(&spi->dev, "failed to kmalloc rx buffer\n");
		goto error;
	}

	max3421_hcd->spi_thread = kthread_run(max3421_spi_thread, hcd,
					      "max3421_spi_thread");
	if (max3421_hcd->spi_thread == ERR_PTR(-ENOMEM)) {
		dev_err(&spi->dev,
			"failed to create SPI thread (out of memory)\n");
		goto error;
	}

	retval = usb_add_hcd(hcd, 0, 0);
	if (retval) {
		dev_err(&spi->dev, "failed to add HCD\n");
		goto error;
	}

	retval = request_irq(spi->irq, max3421_irq_handler,
			     IRQF_TRIGGER_LOW, "max3421", hcd);
	if (retval < 0) {
		dev_err(&spi->dev, "failed to request irq %d\n", spi->irq);
		goto error;
	}
	return 0;

error:
	if (hcd) {
		kfree(max3421_hcd->tx);
		kfree(max3421_hcd->rx);
		if (max3421_hcd->spi_thread)
			kthread_stop(max3421_hcd->spi_thread);
		usb_put_hcd(hcd);
	}
	return retval;
}

static int
max3421_remove(struct spi_device *spi)
{
	struct max3421_hcd *max3421_hcd = NULL, **prev;
	struct usb_hcd *hcd = NULL;
	unsigned long flags;

	for (prev = &max3421_hcd_list; *prev; prev = &(*prev)->next) {
		max3421_hcd = *prev;
		hcd = max3421_to_hcd(max3421_hcd);
		if (hcd->self.controller == &spi->dev)
			break;
	}
	if (!max3421_hcd) {
		dev_err(&spi->dev, "no MAX3421 HCD found for SPI device %p\n",
			spi);
		return -ENODEV;
	}

	usb_remove_hcd(hcd);

	spin_lock_irqsave(&max3421_hcd->lock, flags);

	kthread_stop(max3421_hcd->spi_thread);
	*prev = max3421_hcd->next;

	spin_unlock_irqrestore(&max3421_hcd->lock, flags);

	free_irq(spi->irq, hcd);

	usb_put_hcd(hcd);
	return 0;
}

static struct spi_driver max3421_driver = {
	.probe		= max3421_probe,
	.remove		= max3421_remove,
	.driver		= {
		.name	= "max3421-hcd",
		.owner	= THIS_MODULE,
	},
};

module_spi_driver(max3421_driver);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR("David Mosberger <davidm@egauge.net>");
MODULE_LICENSE("GPL");

// SPDX-License-Identifier: GPL-2.0
/*
 * Freescale QUICC Engine UART device driver
 *
 * Author: Timur Tabi <timur@freescale.com>
 *
 * Copyright 2007 Freescale Semiconductor, Inc.
 *
 * This driver adds support for UART devices via Freescale's QUICC Engine
 * found on some Freescale SOCs.
 *
 * If Soft-UART support is needed but not already present, then this driver
 * will request and upload the "Soft-UART" microcode upon probe.  The
 * filename of the microcode should be fsl_qe_ucode_uart_X_YZ.bin, where "X"
 * is the name of the SOC (e.g. 8323), and YZ is the revision of the SOC,
 * (e.g. "11" for 1.1).
 */

#include <linux/module.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/io.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/dma-mapping.h>

#include <linux/fs_uart_pd.h>
#include <soc/fsl/qe/ucc_slow.h>

#include <linux/firmware.h>
#include <soc/fsl/cpm.h>

#ifdef CONFIG_PPC32
#include <asm/reg.h> /* mfspr, SPRN_SVR */
#endif

/*
 * The GUMR flag for Soft UART.  This would normally be defined in qe.h,
 * but Soft-UART is a hack and we want to keep everything related to it in
 * this file.
 */
#define UCC_SLOW_GUMR_H_SUART   	0x00004000      /* Soft-UART */

/*
 * soft_uart is 1 if we need to use Soft-UART mode
 */
static int soft_uart;
/*
 * firmware_loaded is 1 if the firmware has been loaded, 0 otherwise.
 */
static int firmware_loaded;

/* Enable this macro to configure all serial ports in internal loopback
   mode */
/* #define LOOPBACK */

/* The major and minor device numbers are defined in
 * http://www.lanana.org/docs/device-list/devices-2.6+.txt.  For the QE
 * UART, we have major number 204 and minor numbers 46 - 49, which are the
 * same as for the CPM2.  This decision was made because no Freescale part
 * has both a CPM and a QE.
 */
#define SERIAL_QE_MAJOR 204
#define SERIAL_QE_MINOR 46

/* Since we only have minor numbers 46 - 49, there is a hard limit of 4 ports */
#define UCC_MAX_UART    4

/* The number of buffer descriptors for receiving characters. */
#define RX_NUM_FIFO     4

/* The number of buffer descriptors for transmitting characters. */
#define TX_NUM_FIFO     4

/* The maximum size of the character buffer for a single RX BD. */
#define RX_BUF_SIZE     32

/* The maximum size of the character buffer for a single TX BD. */
#define TX_BUF_SIZE     32

/*
 * The number of jiffies to wait after receiving a close command before the
 * device is actually closed.  This allows the last few characters to be
 * sent over the wire.
 */
#define UCC_WAIT_CLOSING 100

struct ucc_uart_pram {
	struct ucc_slow_pram common;
	u8 res1[8];     	/* reserved */
	__be16 maxidl;  	/* Maximum idle chars */
	__be16 idlc;    	/* temp idle counter */
	__be16 brkcr;   	/* Break count register */
	__be16 parec;   	/* receive parity error counter */
	__be16 frmec;   	/* receive framing error counter */
	__be16 nosec;   	/* receive noise counter */
	__be16 brkec;   	/* receive break condition counter */
	__be16 brkln;   	/* last received break length */
	__be16 uaddr[2];	/* UART address character 1 & 2 */
	__be16 rtemp;   	/* Temp storage */
	__be16 toseq;   	/* Transmit out of sequence char */
	__be16 cchars[8];       /* control characters 1-8 */
	__be16 rccm;    	/* receive control character mask */
	__be16 rccr;    	/* receive control character register */
	__be16 rlbc;    	/* receive last break character */
	__be16 res2;    	/* reserved */
	__be32 res3;    	/* reserved, should be cleared */
	u8 res4;		/* reserved, should be cleared */
	u8 res5[3];     	/* reserved, should be cleared */
	__be32 res6;    	/* reserved, should be cleared */
	__be32 res7;    	/* reserved, should be cleared */
	__be32 res8;    	/* reserved, should be cleared */
	__be32 res9;    	/* reserved, should be cleared */
	__be32 res10;   	/* reserved, should be cleared */
	__be32 res11;   	/* reserved, should be cleared */
	__be32 res12;   	/* reserved, should be cleared */
	__be32 res13;   	/* reserved, should be cleared */
/* The rest is for Soft-UART only */
	__be16 supsmr;  	/* 0x90, Shadow UPSMR */
	__be16 res92;   	/* 0x92, reserved, initialize to 0 */
	__be32 rx_state;	/* 0x94, RX state, initialize to 0 */
	__be32 rx_cnt;  	/* 0x98, RX count, initialize to 0 */
	u8 rx_length;   	/* 0x9C, Char length, set to 1+CL+PEN+1+SL */
	u8 rx_bitmark;  	/* 0x9D, reserved, initialize to 0 */
	u8 rx_temp_dlst_qe;     /* 0x9E, reserved, initialize to 0 */
	u8 res14[0xBC - 0x9F];  /* reserved */
	__be32 dump_ptr;	/* 0xBC, Dump pointer */
	__be32 rx_frame_rem;    /* 0xC0, reserved, initialize to 0 */
	u8 rx_frame_rem_size;   /* 0xC4, reserved, initialize to 0 */
	u8 tx_mode;     	/* 0xC5, mode, 0=AHDLC, 1=UART */
	__be16 tx_state;	/* 0xC6, TX state */
	u8 res15[0xD0 - 0xC8];  /* reserved */
	__be32 resD0;   	/* 0xD0, reserved, initialize to 0 */
	u8 resD4;       	/* 0xD4, reserved, initialize to 0 */
	__be16 resD5;   	/* 0xD5, reserved, initialize to 0 */
} __attribute__ ((packed));

/* SUPSMR definitions, for Soft-UART only */
#define UCC_UART_SUPSMR_SL      	0x8000
#define UCC_UART_SUPSMR_RPM_MASK	0x6000
#define UCC_UART_SUPSMR_RPM_ODD 	0x0000
#define UCC_UART_SUPSMR_RPM_LOW 	0x2000
#define UCC_UART_SUPSMR_RPM_EVEN	0x4000
#define UCC_UART_SUPSMR_RPM_HIGH	0x6000
#define UCC_UART_SUPSMR_PEN     	0x1000
#define UCC_UART_SUPSMR_TPM_MASK	0x0C00
#define UCC_UART_SUPSMR_TPM_ODD 	0x0000
#define UCC_UART_SUPSMR_TPM_LOW 	0x0400
#define UCC_UART_SUPSMR_TPM_EVEN	0x0800
#define UCC_UART_SUPSMR_TPM_HIGH	0x0C00
#define UCC_UART_SUPSMR_FRZ     	0x0100
#define UCC_UART_SUPSMR_UM_MASK 	0x00c0
#define UCC_UART_SUPSMR_UM_NORMAL       0x0000
#define UCC_UART_SUPSMR_UM_MAN_MULTI    0x0040
#define UCC_UART_SUPSMR_UM_AUTO_MULTI   0x00c0
#define UCC_UART_SUPSMR_CL_MASK 	0x0030
#define UCC_UART_SUPSMR_CL_8    	0x0030
#define UCC_UART_SUPSMR_CL_7    	0x0020
#define UCC_UART_SUPSMR_CL_6    	0x0010
#define UCC_UART_SUPSMR_CL_5    	0x0000

#define UCC_UART_TX_STATE_AHDLC 	0x00
#define UCC_UART_TX_STATE_UART  	0x01
#define UCC_UART_TX_STATE_X1    	0x00
#define UCC_UART_TX_STATE_X16   	0x80

#define UCC_UART_PRAM_ALIGNMENT 0x100

#define UCC_UART_SIZE_OF_BD     UCC_SLOW_SIZE_OF_BD
#define NUM_CONTROL_CHARS       8

/* Private per-port data structure */
struct uart_qe_port {
	struct uart_port port;
	struct ucc_slow __iomem *uccp;
	struct ucc_uart_pram __iomem *uccup;
	struct ucc_slow_info us_info;
	struct ucc_slow_private *us_private;
	struct device_node *np;
	unsigned int ucc_num;   /* First ucc is 0, not 1 */

	u16 rx_nrfifos;
	u16 rx_fifosize;
	u16 tx_nrfifos;
	u16 tx_fifosize;
	int wait_closing;
	u32 flags;
	struct qe_bd *rx_bd_base;
	struct qe_bd *rx_cur;
	struct qe_bd *tx_bd_base;
	struct qe_bd *tx_cur;
	unsigned char *tx_buf;
	unsigned char *rx_buf;
	void *bd_virt;  	/* virtual address of the BD buffers */
	dma_addr_t bd_dma_addr; /* bus address of the BD buffers */
	unsigned int bd_size;   /* size of BD buffer space */
};

static struct uart_driver ucc_uart_driver = {
	.owner  	= THIS_MODULE,
	.driver_name    = "ucc_uart",
	.dev_name       = "ttyQE",
	.major  	= SERIAL_QE_MAJOR,
	.minor  	= SERIAL_QE_MINOR,
	.nr     	= UCC_MAX_UART,
};

/*
 * Virtual to physical address translation.
 *
 * Given the virtual address for a character buffer, this function returns
 * the physical (DMA) equivalent.
 */
static inline dma_addr_t cpu2qe_addr(void *addr, struct uart_qe_port *qe_port)
{
	if (likely((addr >= qe_port->bd_virt)) &&
	    (addr < (qe_port->bd_virt + qe_port->bd_size)))
		return qe_port->bd_dma_addr + (addr - qe_port->bd_virt);

	/* something nasty happened */
	printk(KERN_ERR "%s: addr=%p\n", __func__, addr);
	BUG();
	return 0;
}

/*
 * Physical to virtual address translation.
 *
 * Given the physical (DMA) address for a character buffer, this function
 * returns the virtual equivalent.
 */
static inline void *qe2cpu_addr(dma_addr_t addr, struct uart_qe_port *qe_port)
{
	/* sanity check */
	if (likely((addr >= qe_port->bd_dma_addr) &&
		   (addr < (qe_port->bd_dma_addr + qe_port->bd_size))))
		return qe_port->bd_virt + (addr - qe_port->bd_dma_addr);

	/* something nasty happened */
	printk(KERN_ERR "%s: addr=%llx\n", __func__, (u64)addr);
	BUG();
	return NULL;
}

/*
 * Return 1 if the QE is done transmitting all buffers for this port
 *
 * This function scans each BD in sequence.  If we find a BD that is not
 * ready (READY=1), then we return 0 indicating that the QE is still sending
 * data.  If we reach the last BD (WRAP=1), then we know we've scanned
 * the entire list, and all BDs are done.
 */
static unsigned int qe_uart_tx_empty(struct uart_port *port)
{
	struct uart_qe_port *qe_port =
		container_of(port, struct uart_qe_port, port);
	struct qe_bd *bdp = qe_port->tx_bd_base;

	while (1) {
		if (qe_ioread16be(&bdp->status) & BD_SC_READY)
			/* This BD is not done, so return "not done" */
			return 0;

		if (qe_ioread16be(&bdp->status) & BD_SC_WRAP)
			/*
			 * This BD is done and it's the last one, so return
			 * "done"
			 */
			return 1;

		bdp++;
	}
}

/*
 * Set the modem control lines
 *
 * Although the QE can control the modem control lines (e.g. CTS), we
 * don't need that support. This function must exist, however, otherwise
 * the kernel will panic.
 */
void qe_uart_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
}

/*
 * Get the current modem control line status
 *
 * Although the QE can control the modem control lines (e.g. CTS), this
 * driver currently doesn't support that, so we always return Carrier
 * Detect, Data Set Ready, and Clear To Send.
 */
static unsigned int qe_uart_get_mctrl(struct uart_port *port)
{
	return TIOCM_CAR | TIOCM_DSR | TIOCM_CTS;
}

/*
 * Disable the transmit interrupt.
 *
 * Although this function is called "stop_tx", it does not actually stop
 * transmission of data.  Instead, it tells the QE to not generate an
 * interrupt when the UCC is finished sending characters.
 */
static void qe_uart_stop_tx(struct uart_port *port)
{
	struct uart_qe_port *qe_port =
		container_of(port, struct uart_qe_port, port);

	qe_clrbits_be16(&qe_port->uccp->uccm, UCC_UART_UCCE_TX);
}

/*
 * Transmit as many characters to the HW as possible.
 *
 * This function will attempt to stuff of all the characters from the
 * kernel's transmit buffer into TX BDs.
 *
 * A return value of non-zero indicates that it successfully stuffed all
 * characters from the kernel buffer.
 *
 * A return value of zero indicates that there are still characters in the
 * kernel's buffer that have not been transmitted, but there are no more BDs
 * available.  This function should be called again after a BD has been made
 * available.
 */
static int qe_uart_tx_pump(struct uart_qe_port *qe_port)
{
	struct qe_bd *bdp;
	unsigned char *p;
	unsigned int count;
	struct uart_port *port = &qe_port->port;
	struct circ_buf *xmit = &port->state->xmit;

	/* Handle xon/xoff */
	if (port->x_char) {
		/* Pick next descriptor and fill from buffer */
		bdp = qe_port->tx_cur;

		p = qe2cpu_addr(be32_to_cpu(bdp->buf), qe_port);

		*p++ = port->x_char;
		qe_iowrite16be(1, &bdp->length);
		qe_setbits_be16(&bdp->status, BD_SC_READY);
		/* Get next BD. */
		if (qe_ioread16be(&bdp->status) & BD_SC_WRAP)
			bdp = qe_port->tx_bd_base;
		else
			bdp++;
		qe_port->tx_cur = bdp;

		port->icount.tx++;
		port->x_char = 0;
		return 1;
	}

	if (uart_circ_empty(xmit) || uart_tx_stopped(port)) {
		qe_uart_stop_tx(port);
		return 0;
	}

	/* Pick next descriptor and fill from buffer */
	bdp = qe_port->tx_cur;

	while (!(qe_ioread16be(&bdp->status) & BD_SC_READY) &&
	       (xmit->tail != xmit->head)) {
		count = 0;
		p = qe2cpu_addr(be32_to_cpu(bdp->buf), qe_port);
		while (count < qe_port->tx_fifosize) {
			*p++ = xmit->buf[xmit->tail];
			xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
			port->icount.tx++;
			count++;
			if (xmit->head == xmit->tail)
				break;
		}

		qe_iowrite16be(count, &bdp->length);
		qe_setbits_be16(&bdp->status, BD_SC_READY);

		/* Get next BD. */
		if (qe_ioread16be(&bdp->status) & BD_SC_WRAP)
			bdp = qe_port->tx_bd_base;
		else
			bdp++;
	}
	qe_port->tx_cur = bdp;

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);

	if (uart_circ_empty(xmit)) {
		/* The kernel buffer is empty, so turn off TX interrupts.  We
		   don't need to be told when the QE is finished transmitting
		   the data. */
		qe_uart_stop_tx(port);
		return 0;
	}

	return 1;
}

/*
 * Start transmitting data
 *
 * This function will start transmitting any available data, if the port
 * isn't already transmitting data.
 */
static void qe_uart_start_tx(struct uart_port *port)
{
	struct uart_qe_port *qe_port =
		container_of(port, struct uart_qe_port, port);

	/* If we currently are transmitting, then just return */
	if (qe_ioread16be(&qe_port->uccp->uccm) & UCC_UART_UCCE_TX)
		return;

	/* Otherwise, pump the port and start transmission */
	if (qe_uart_tx_pump(qe_port))
		qe_setbits_be16(&qe_port->uccp->uccm, UCC_UART_UCCE_TX);
}

/*
 * Stop transmitting data
 */
static void qe_uart_stop_rx(struct uart_port *port)
{
	struct uart_qe_port *qe_port =
		container_of(port, struct uart_qe_port, port);

	qe_clrbits_be16(&qe_port->uccp->uccm, UCC_UART_UCCE_RX);
}

/* Start or stop sending  break signal
 *
 * This function controls the sending of a break signal.  If break_state=1,
 * then we start sending a break signal.  If break_state=0, then we stop
 * sending the break signal.
 */
static void qe_uart_break_ctl(struct uart_port *port, int break_state)
{
	struct uart_qe_port *qe_port =
		container_of(port, struct uart_qe_port, port);

	if (break_state)
		ucc_slow_stop_tx(qe_port->us_private);
	else
		ucc_slow_restart_tx(qe_port->us_private);
}

/* ISR helper function for receiving character.
 *
 * This function is called by the ISR to handling receiving characters
 */
static void qe_uart_int_rx(struct uart_qe_port *qe_port)
{
	int i;
	unsigned char ch, *cp;
	struct uart_port *port = &qe_port->port;
	struct tty_port *tport = &port->state->port;
	struct qe_bd *bdp;
	u16 status;
	unsigned int flg;

	/* Just loop through the closed BDs and copy the characters into
	 * the buffer.
	 */
	bdp = qe_port->rx_cur;
	while (1) {
		status = qe_ioread16be(&bdp->status);

		/* If this one is empty, then we assume we've read them all */
		if (status & BD_SC_EMPTY)
			break;

		/* get number of characters, and check space in RX buffer */
		i = qe_ioread16be(&bdp->length);

		/* If we don't have enough room in RX buffer for the entire BD,
		 * then we try later, which will be the next RX interrupt.
		 */
		if (tty_buffer_request_room(tport, i) < i) {
			dev_dbg(port->dev, "ucc-uart: no room in RX buffer\n");
			return;
		}

		/* get pointer */
		cp = qe2cpu_addr(be32_to_cpu(bdp->buf), qe_port);

		/* loop through the buffer */
		while (i-- > 0) {
			ch = *cp++;
			port->icount.rx++;
			flg = TTY_NORMAL;

			if (!i && status &
			    (BD_SC_BR | BD_SC_FR | BD_SC_PR | BD_SC_OV))
				goto handle_error;
			if (uart_handle_sysrq_char(port, ch))
				continue;

error_return:
			tty_insert_flip_char(tport, ch, flg);

		}

		/* This BD is ready to be used again. Clear status. get next */
		qe_clrsetbits_be16(&bdp->status,
				   BD_SC_BR | BD_SC_FR | BD_SC_PR | BD_SC_OV | BD_SC_ID,
				   BD_SC_EMPTY);
		if (qe_ioread16be(&bdp->status) & BD_SC_WRAP)
			bdp = qe_port->rx_bd_base;
		else
			bdp++;

	}

	/* Write back buffer pointer */
	qe_port->rx_cur = bdp;

	/* Activate BH processing */
	tty_flip_buffer_push(tport);

	return;

	/* Error processing */

handle_error:
	/* Statistics */
	if (status & BD_SC_BR)
		port->icount.brk++;
	if (status & BD_SC_PR)
		port->icount.parity++;
	if (status & BD_SC_FR)
		port->icount.frame++;
	if (status & BD_SC_OV)
		port->icount.overrun++;

	/* Mask out ignored conditions */
	status &= port->read_status_mask;

	/* Handle the remaining ones */
	if (status & BD_SC_BR)
		flg = TTY_BREAK;
	else if (status & BD_SC_PR)
		flg = TTY_PARITY;
	else if (status & BD_SC_FR)
		flg = TTY_FRAME;

	/* Overrun does not affect the current character ! */
	if (status & BD_SC_OV)
		tty_insert_flip_char(tport, 0, TTY_OVERRUN);
	port->sysrq = 0;
	goto error_return;
}

/* Interrupt handler
 *
 * This interrupt handler is called after a BD is processed.
 */
static irqreturn_t qe_uart_int(int irq, void *data)
{
	struct uart_qe_port *qe_port = (struct uart_qe_port *) data;
	struct ucc_slow __iomem *uccp = qe_port->uccp;
	u16 events;

	/* Clear the interrupts */
	events = qe_ioread16be(&uccp->ucce);
	qe_iowrite16be(events, &uccp->ucce);

	if (events & UCC_UART_UCCE_BRKE)
		uart_handle_break(&qe_port->port);

	if (events & UCC_UART_UCCE_RX)
		qe_uart_int_rx(qe_port);

	if (events & UCC_UART_UCCE_TX)
		qe_uart_tx_pump(qe_port);

	return events ? IRQ_HANDLED : IRQ_NONE;
}

/* Initialize buffer descriptors
 *
 * This function initializes all of the RX and TX buffer descriptors.
 */
static void qe_uart_initbd(struct uart_qe_port *qe_port)
{
	int i;
	void *bd_virt;
	struct qe_bd *bdp;

	/* Set the physical address of the host memory buffers in the buffer
	 * descriptors, and the virtual address for us to work with.
	 */
	bd_virt = qe_port->bd_virt;
	bdp = qe_port->rx_bd_base;
	qe_port->rx_cur = qe_port->rx_bd_base;
	for (i = 0; i < (qe_port->rx_nrfifos - 1); i++) {
		qe_iowrite16be(BD_SC_EMPTY | BD_SC_INTRPT, &bdp->status);
		qe_iowrite32be(cpu2qe_addr(bd_virt, qe_port), &bdp->buf);
		qe_iowrite16be(0, &bdp->length);
		bd_virt += qe_port->rx_fifosize;
		bdp++;
	}

	/* */
	qe_iowrite16be(BD_SC_WRAP | BD_SC_EMPTY | BD_SC_INTRPT, &bdp->status);
	qe_iowrite32be(cpu2qe_addr(bd_virt, qe_port), &bdp->buf);
	qe_iowrite16be(0, &bdp->length);

	/* Set the physical address of the host memory
	 * buffers in the buffer descriptors, and the
	 * virtual address for us to work with.
	 */
	bd_virt = qe_port->bd_virt +
		L1_CACHE_ALIGN(qe_port->rx_nrfifos * qe_port->rx_fifosize);
	qe_port->tx_cur = qe_port->tx_bd_base;
	bdp = qe_port->tx_bd_base;
	for (i = 0; i < (qe_port->tx_nrfifos - 1); i++) {
		qe_iowrite16be(BD_SC_INTRPT, &bdp->status);
		qe_iowrite32be(cpu2qe_addr(bd_virt, qe_port), &bdp->buf);
		qe_iowrite16be(0, &bdp->length);
		bd_virt += qe_port->tx_fifosize;
		bdp++;
	}

	/* Loopback requires the preamble bit to be set on the first TX BD */
#ifdef LOOPBACK
	qe_setbits_be16(&qe_port->tx_cur->status, BD_SC_P);
#endif

	qe_iowrite16be(BD_SC_WRAP | BD_SC_INTRPT, &bdp->status);
	qe_iowrite32be(cpu2qe_addr(bd_virt, qe_port), &bdp->buf);
	qe_iowrite16be(0, &bdp->length);
}

/*
 * Initialize a UCC for UART.
 *
 * This function configures a given UCC to be used as a UART device. Basic
 * UCC initialization is handled in qe_uart_request_port().  This function
 * does all the UART-specific stuff.
 */
static void qe_uart_init_ucc(struct uart_qe_port *qe_port)
{
	u32 cecr_subblock;
	struct ucc_slow __iomem *uccp = qe_port->uccp;
	struct ucc_uart_pram *uccup = qe_port->uccup;

	unsigned int i;

	/* First, disable TX and RX in the UCC */
	ucc_slow_disable(qe_port->us_private, COMM_DIR_RX_AND_TX);

	/* Program the UCC UART parameter RAM */
	qe_iowrite8(UCC_BMR_GBL | UCC_BMR_BO_BE, &uccup->common.rbmr);
	qe_iowrite8(UCC_BMR_GBL | UCC_BMR_BO_BE, &uccup->common.tbmr);
	qe_iowrite16be(qe_port->rx_fifosize, &uccup->common.mrblr);
	qe_iowrite16be(0x10, &uccup->maxidl);
	qe_iowrite16be(1, &uccup->brkcr);
	qe_iowrite16be(0, &uccup->parec);
	qe_iowrite16be(0, &uccup->frmec);
	qe_iowrite16be(0, &uccup->nosec);
	qe_iowrite16be(0, &uccup->brkec);
	qe_iowrite16be(0, &uccup->uaddr[0]);
	qe_iowrite16be(0, &uccup->uaddr[1]);
	qe_iowrite16be(0, &uccup->toseq);
	for (i = 0; i < 8; i++)
		qe_iowrite16be(0xC000, &uccup->cchars[i]);
	qe_iowrite16be(0xc0ff, &uccup->rccm);

	/* Configure the GUMR registers for UART */
	if (soft_uart) {
		/* Soft-UART requires a 1X multiplier for TX */
		qe_clrsetbits_be32(&uccp->gumr_l,
				   UCC_SLOW_GUMR_L_MODE_MASK | UCC_SLOW_GUMR_L_TDCR_MASK | UCC_SLOW_GUMR_L_RDCR_MASK,
				   UCC_SLOW_GUMR_L_MODE_UART | UCC_SLOW_GUMR_L_TDCR_1 | UCC_SLOW_GUMR_L_RDCR_16);

		qe_clrsetbits_be32(&uccp->gumr_h, UCC_SLOW_GUMR_H_RFW,
				   UCC_SLOW_GUMR_H_TRX | UCC_SLOW_GUMR_H_TTX);
	} else {
		qe_clrsetbits_be32(&uccp->gumr_l,
				   UCC_SLOW_GUMR_L_MODE_MASK | UCC_SLOW_GUMR_L_TDCR_MASK | UCC_SLOW_GUMR_L_RDCR_MASK,
				   UCC_SLOW_GUMR_L_MODE_UART | UCC_SLOW_GUMR_L_TDCR_16 | UCC_SLOW_GUMR_L_RDCR_16);

		qe_clrsetbits_be32(&uccp->gumr_h,
				   UCC_SLOW_GUMR_H_TRX | UCC_SLOW_GUMR_H_TTX,
				   UCC_SLOW_GUMR_H_RFW);
	}

#ifdef LOOPBACK
	qe_clrsetbits_be32(&uccp->gumr_l, UCC_SLOW_GUMR_L_DIAG_MASK,
			   UCC_SLOW_GUMR_L_DIAG_LOOP);
	qe_clrsetbits_be32(&uccp->gumr_h,
			   UCC_SLOW_GUMR_H_CTSP | UCC_SLOW_GUMR_H_RSYN,
			   UCC_SLOW_GUMR_H_CDS);
#endif

	/* Disable rx interrupts  and clear all pending events.  */
	qe_iowrite16be(0, &uccp->uccm);
	qe_iowrite16be(0xffff, &uccp->ucce);
	qe_iowrite16be(0x7e7e, &uccp->udsr);

	/* Initialize UPSMR */
	qe_iowrite16be(0, &uccp->upsmr);

	if (soft_uart) {
		qe_iowrite16be(0x30, &uccup->supsmr);
		qe_iowrite16be(0, &uccup->res92);
		qe_iowrite32be(0, &uccup->rx_state);
		qe_iowrite32be(0, &uccup->rx_cnt);
		qe_iowrite8(0, &uccup->rx_bitmark);
		qe_iowrite8(10, &uccup->rx_length);
		qe_iowrite32be(0x4000, &uccup->dump_ptr);
		qe_iowrite8(0, &uccup->rx_temp_dlst_qe);
		qe_iowrite32be(0, &uccup->rx_frame_rem);
		qe_iowrite8(0, &uccup->rx_frame_rem_size);
		/* Soft-UART requires TX to be 1X */
		qe_iowrite8(UCC_UART_TX_STATE_UART | UCC_UART_TX_STATE_X1,
			    &uccup->tx_mode);
		qe_iowrite16be(0, &uccup->tx_state);
		qe_iowrite8(0, &uccup->resD4);
		qe_iowrite16be(0, &uccup->resD5);

		/* Set UART mode.
		 * Enable receive and transmit.
		 */

		/* From the microcode errata:
		 * 1.GUMR_L register, set mode=0010 (QMC).
		 * 2.Set GUMR_H[17] bit. (UART/AHDLC mode).
		 * 3.Set GUMR_H[19:20] (Transparent mode)
		 * 4.Clear GUMR_H[26] (RFW)
		 * ...
		 * 6.Receiver must use 16x over sampling
		 */
		qe_clrsetbits_be32(&uccp->gumr_l,
				   UCC_SLOW_GUMR_L_MODE_MASK | UCC_SLOW_GUMR_L_TDCR_MASK | UCC_SLOW_GUMR_L_RDCR_MASK,
				   UCC_SLOW_GUMR_L_MODE_QMC | UCC_SLOW_GUMR_L_TDCR_16 | UCC_SLOW_GUMR_L_RDCR_16);

		qe_clrsetbits_be32(&uccp->gumr_h,
				   UCC_SLOW_GUMR_H_RFW | UCC_SLOW_GUMR_H_RSYN,
				   UCC_SLOW_GUMR_H_SUART | UCC_SLOW_GUMR_H_TRX | UCC_SLOW_GUMR_H_TTX | UCC_SLOW_GUMR_H_TFL);

#ifdef LOOPBACK
		qe_clrsetbits_be32(&uccp->gumr_l, UCC_SLOW_GUMR_L_DIAG_MASK,
				   UCC_SLOW_GUMR_L_DIAG_LOOP);
		qe_clrbits_be32(&uccp->gumr_h,
				UCC_SLOW_GUMR_H_CTSP | UCC_SLOW_GUMR_H_CDS);
#endif

		cecr_subblock = ucc_slow_get_qe_cr_subblock(qe_port->ucc_num);
		qe_issue_cmd(QE_INIT_TX_RX, cecr_subblock,
			QE_CR_PROTOCOL_UNSPECIFIED, 0);
	} else {
		cecr_subblock = ucc_slow_get_qe_cr_subblock(qe_port->ucc_num);
		qe_issue_cmd(QE_INIT_TX_RX, cecr_subblock,
			QE_CR_PROTOCOL_UART, 0);
	}
}

/*
 * Initialize the port.
 */
static int qe_uart_startup(struct uart_port *port)
{
	struct uart_qe_port *qe_port =
		container_of(port, struct uart_qe_port, port);
	int ret;

	/*
	 * If we're using Soft-UART mode, then we need to make sure the
	 * firmware has been uploaded first.
	 */
	if (soft_uart && !firmware_loaded) {
		dev_err(port->dev, "Soft-UART firmware not uploaded\n");
		return -ENODEV;
	}

	qe_uart_initbd(qe_port);
	qe_uart_init_ucc(qe_port);

	/* Install interrupt handler. */
	ret = request_irq(port->irq, qe_uart_int, IRQF_SHARED, "ucc-uart",
		qe_port);
	if (ret) {
		dev_err(port->dev, "could not claim IRQ %u\n", port->irq);
		return ret;
	}

	/* Startup rx-int */
	qe_setbits_be16(&qe_port->uccp->uccm, UCC_UART_UCCE_RX);
	ucc_slow_enable(qe_port->us_private, COMM_DIR_RX_AND_TX);

	return 0;
}

/*
 * Shutdown the port.
 */
static void qe_uart_shutdown(struct uart_port *port)
{
	struct uart_qe_port *qe_port =
		container_of(port, struct uart_qe_port, port);
	struct ucc_slow __iomem *uccp = qe_port->uccp;
	unsigned int timeout = 20;

	/* Disable RX and TX */

	/* Wait for all the BDs marked sent */
	while (!qe_uart_tx_empty(port)) {
		if (!--timeout) {
			dev_warn(port->dev, "shutdown timeout\n");
			break;
		}
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(2);
	}

	if (qe_port->wait_closing) {
		/* Wait a bit longer */
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(qe_port->wait_closing);
	}

	/* Stop uarts */
	ucc_slow_disable(qe_port->us_private, COMM_DIR_RX_AND_TX);
	qe_clrbits_be16(&uccp->uccm, UCC_UART_UCCE_TX | UCC_UART_UCCE_RX);

	/* Shut them really down and reinit buffer descriptors */
	ucc_slow_graceful_stop_tx(qe_port->us_private);
	qe_uart_initbd(qe_port);

	free_irq(port->irq, qe_port);
}

/*
 * Set the serial port parameters.
 */
static void qe_uart_set_termios(struct uart_port *port,
				struct ktermios *termios, struct ktermios *old)
{
	struct uart_qe_port *qe_port =
		container_of(port, struct uart_qe_port, port);
	struct ucc_slow __iomem *uccp = qe_port->uccp;
	unsigned int baud;
	unsigned long flags;
	u16 upsmr = qe_ioread16be(&uccp->upsmr);
	struct ucc_uart_pram __iomem *uccup = qe_port->uccup;
	u16 supsmr = qe_ioread16be(&uccup->supsmr);
	u8 char_length = 2; /* 1 + CL + PEN + 1 + SL */

	/* Character length programmed into the mode register is the
	 * sum of: 1 start bit, number of data bits, 0 or 1 parity bit,
	 * 1 or 2 stop bits, minus 1.
	 * The value 'bits' counts this for us.
	 */

	/* byte size */
	upsmr &= UCC_UART_UPSMR_CL_MASK;
	supsmr &= UCC_UART_SUPSMR_CL_MASK;

	switch (termios->c_cflag & CSIZE) {
	case CS5:
		upsmr |= UCC_UART_UPSMR_CL_5;
		supsmr |= UCC_UART_SUPSMR_CL_5;
		char_length += 5;
		break;
	case CS6:
		upsmr |= UCC_UART_UPSMR_CL_6;
		supsmr |= UCC_UART_SUPSMR_CL_6;
		char_length += 6;
		break;
	case CS7:
		upsmr |= UCC_UART_UPSMR_CL_7;
		supsmr |= UCC_UART_SUPSMR_CL_7;
		char_length += 7;
		break;
	default:	/* case CS8 */
		upsmr |= UCC_UART_UPSMR_CL_8;
		supsmr |= UCC_UART_SUPSMR_CL_8;
		char_length += 8;
		break;
	}

	/* If CSTOPB is set, we want two stop bits */
	if (termios->c_cflag & CSTOPB) {
		upsmr |= UCC_UART_UPSMR_SL;
		supsmr |= UCC_UART_SUPSMR_SL;
		char_length++;  /* + SL */
	}

	if (termios->c_cflag & PARENB) {
		upsmr |= UCC_UART_UPSMR_PEN;
		supsmr |= UCC_UART_SUPSMR_PEN;
		char_length++;  /* + PEN */

		if (!(termios->c_cflag & PARODD)) {
			upsmr &= ~(UCC_UART_UPSMR_RPM_MASK |
				   UCC_UART_UPSMR_TPM_MASK);
			upsmr |= UCC_UART_UPSMR_RPM_EVEN |
				UCC_UART_UPSMR_TPM_EVEN;
			supsmr &= ~(UCC_UART_SUPSMR_RPM_MASK |
				    UCC_UART_SUPSMR_TPM_MASK);
			supsmr |= UCC_UART_SUPSMR_RPM_EVEN |
				UCC_UART_SUPSMR_TPM_EVEN;
		}
	}

	/*
	 * Set up parity check flag
	 */
	port->read_status_mask = BD_SC_EMPTY | BD_SC_OV;
	if (termios->c_iflag & INPCK)
		port->read_status_mask |= BD_SC_FR | BD_SC_PR;
	if (termios->c_iflag & (IGNBRK | BRKINT | PARMRK))
		port->read_status_mask |= BD_SC_BR;

	/*
	 * Characters to ignore
	 */
	port->ignore_status_mask = 0;
	if (termios->c_iflag & IGNPAR)
		port->ignore_status_mask |= BD_SC_PR | BD_SC_FR;
	if (termios->c_iflag & IGNBRK) {
		port->ignore_status_mask |= BD_SC_BR;
		/*
		 * If we're ignore parity and break indicators, ignore
		 * overruns too.  (For real raw support).
		 */
		if (termios->c_iflag & IGNPAR)
			port->ignore_status_mask |= BD_SC_OV;
	}
	/*
	 * !!! ignore all characters if CREAD is not set
	 */
	if ((termios->c_cflag & CREAD) == 0)
		port->read_status_mask &= ~BD_SC_EMPTY;

	baud = uart_get_baud_rate(port, termios, old, 0, port->uartclk / 16);

	/* Do we really need a spinlock here? */
	spin_lock_irqsave(&port->lock, flags);

	/* Update the per-port timeout. */
	uart_update_timeout(port, termios->c_cflag, baud);

	qe_iowrite16be(upsmr, &uccp->upsmr);
	if (soft_uart) {
		qe_iowrite16be(supsmr, &uccup->supsmr);
		qe_iowrite8(char_length, &uccup->rx_length);

		/* Soft-UART requires a 1X multiplier for TX */
		qe_setbrg(qe_port->us_info.rx_clock, baud, 16);
		qe_setbrg(qe_port->us_info.tx_clock, baud, 1);
	} else {
		qe_setbrg(qe_port->us_info.rx_clock, baud, 16);
		qe_setbrg(qe_port->us_info.tx_clock, baud, 16);
	}

	spin_unlock_irqrestore(&port->lock, flags);
}

/*
 * Return a pointer to a string that describes what kind of port this is.
 */
static const char *qe_uart_type(struct uart_port *port)
{
	return "QE";
}

/*
 * Allocate any memory and I/O resources required by the port.
 */
static int qe_uart_request_port(struct uart_port *port)
{
	int ret;
	struct uart_qe_port *qe_port =
		container_of(port, struct uart_qe_port, port);
	struct ucc_slow_info *us_info = &qe_port->us_info;
	struct ucc_slow_private *uccs;
	unsigned int rx_size, tx_size;
	void *bd_virt;
	dma_addr_t bd_dma_addr = 0;

	ret = ucc_slow_init(us_info, &uccs);
	if (ret) {
		dev_err(port->dev, "could not initialize UCC%u\n",
		       qe_port->ucc_num);
		return ret;
	}

	qe_port->us_private = uccs;
	qe_port->uccp = uccs->us_regs;
	qe_port->uccup = (struct ucc_uart_pram *) uccs->us_pram;
	qe_port->rx_bd_base = uccs->rx_bd;
	qe_port->tx_bd_base = uccs->tx_bd;

	/*
	 * Allocate the transmit and receive data buffers.
	 */

	rx_size = L1_CACHE_ALIGN(qe_port->rx_nrfifos * qe_port->rx_fifosize);
	tx_size = L1_CACHE_ALIGN(qe_port->tx_nrfifos * qe_port->tx_fifosize);

	bd_virt = dma_alloc_coherent(port->dev, rx_size + tx_size, &bd_dma_addr,
		GFP_KERNEL);
	if (!bd_virt) {
		dev_err(port->dev, "could not allocate buffer descriptors\n");
		return -ENOMEM;
	}

	qe_port->bd_virt = bd_virt;
	qe_port->bd_dma_addr = bd_dma_addr;
	qe_port->bd_size = rx_size + tx_size;

	qe_port->rx_buf = bd_virt;
	qe_port->tx_buf = qe_port->rx_buf + rx_size;

	return 0;
}

/*
 * Configure the port.
 *
 * We say we're a CPM-type port because that's mostly true.  Once the device
 * is configured, this driver operates almost identically to the CPM serial
 * driver.
 */
static void qe_uart_config_port(struct uart_port *port, int flags)
{
	if (flags & UART_CONFIG_TYPE) {
		port->type = PORT_CPM;
		qe_uart_request_port(port);
	}
}

/*
 * Release any memory and I/O resources that were allocated in
 * qe_uart_request_port().
 */
static void qe_uart_release_port(struct uart_port *port)
{
	struct uart_qe_port *qe_port =
		container_of(port, struct uart_qe_port, port);
	struct ucc_slow_private *uccs = qe_port->us_private;

	dma_free_coherent(port->dev, qe_port->bd_size, qe_port->bd_virt,
			  qe_port->bd_dma_addr);

	ucc_slow_free(uccs);
}

/*
 * Verify that the data in serial_struct is suitable for this device.
 */
static int qe_uart_verify_port(struct uart_port *port,
			       struct serial_struct *ser)
{
	if (ser->type != PORT_UNKNOWN && ser->type != PORT_CPM)
		return -EINVAL;

	if (ser->irq < 0 || ser->irq >= nr_irqs)
		return -EINVAL;

	if (ser->baud_base < 9600)
		return -EINVAL;

	return 0;
}
/* UART operations
 *
 * Details on these functions can be found in Documentation/driver-api/serial/driver.rst
 */
static const struct uart_ops qe_uart_pops = {
	.tx_empty       = qe_uart_tx_empty,
	.set_mctrl      = qe_uart_set_mctrl,
	.get_mctrl      = qe_uart_get_mctrl,
	.stop_tx	= qe_uart_stop_tx,
	.start_tx       = qe_uart_start_tx,
	.stop_rx	= qe_uart_stop_rx,
	.break_ctl      = qe_uart_break_ctl,
	.startup	= qe_uart_startup,
	.shutdown       = qe_uart_shutdown,
	.set_termios    = qe_uart_set_termios,
	.type   	= qe_uart_type,
	.release_port   = qe_uart_release_port,
	.request_port   = qe_uart_request_port,
	.config_port    = qe_uart_config_port,
	.verify_port    = qe_uart_verify_port,
};


#ifdef CONFIG_PPC32
/*
 * Obtain the SOC model number and revision level
 *
 * This function parses the device tree to obtain the SOC model.  It then
 * reads the SVR register to the revision.
 *
 * The device tree stores the SOC model two different ways.
 *
 * The new way is:
 *
 *      	cpu@0 {
 *      		compatible = "PowerPC,8323";
 *      		device_type = "cpu";
 *      		...
 *
 *
 * The old way is:
 *      	 PowerPC,8323@0 {
 *      		device_type = "cpu";
 *      		...
 *
 * This code first checks the new way, and then the old way.
 */
static unsigned int soc_info(unsigned int *rev_h, unsigned int *rev_l)
{
	struct device_node *np;
	const char *soc_string;
	unsigned int svr;
	unsigned int soc;

	/* Find the CPU node */
	np = of_find_node_by_type(NULL, "cpu");
	if (!np)
		return 0;
	/* Find the compatible property */
	soc_string = of_get_property(np, "compatible", NULL);
	if (!soc_string)
		/* No compatible property, so try the name. */
		soc_string = np->name;

	/* Extract the SOC number from the "PowerPC," string */
	if ((sscanf(soc_string, "PowerPC,%u", &soc) != 1) || !soc)
		return 0;

	/* Get the revision from the SVR */
	svr = mfspr(SPRN_SVR);
	*rev_h = (svr >> 4) & 0xf;
	*rev_l = svr & 0xf;

	return soc;
}

/*
 * requst_firmware_nowait() callback function
 *
 * This function is called by the kernel when a firmware is made available,
 * or if it times out waiting for the firmware.
 */
static void uart_firmware_cont(const struct firmware *fw, void *context)
{
	struct qe_firmware *firmware;
	struct device *dev = context;
	int ret;

	if (!fw) {
		dev_err(dev, "firmware not found\n");
		return;
	}

	firmware = (struct qe_firmware *) fw->data;

	if (firmware->header.length != fw->size) {
		dev_err(dev, "invalid firmware\n");
		goto out;
	}

	ret = qe_upload_firmware(firmware);
	if (ret) {
		dev_err(dev, "could not load firmware\n");
		goto out;
	}

	firmware_loaded = 1;
 out:
	release_firmware(fw);
}

static int soft_uart_init(struct platform_device *ofdev)
{
	struct device_node *np = ofdev->dev.of_node;
	struct qe_firmware_info *qe_fw_info;
	int ret;

	if (of_find_property(np, "soft-uart", NULL)) {
		dev_dbg(&ofdev->dev, "using Soft-UART mode\n");
		soft_uart = 1;
	} else {
		return 0;
	}

	qe_fw_info = qe_get_firmware_info();

	/* Check if the firmware has been uploaded. */
	if (qe_fw_info && strstr(qe_fw_info->id, "Soft-UART")) {
		firmware_loaded = 1;
	} else {
		char filename[32];
		unsigned int soc;
		unsigned int rev_h;
		unsigned int rev_l;

		soc = soc_info(&rev_h, &rev_l);
		if (!soc) {
			dev_err(&ofdev->dev, "unknown CPU model\n");
			return -ENXIO;
		}
		sprintf(filename, "fsl_qe_ucode_uart_%u_%u%u.bin",
			soc, rev_h, rev_l);

		dev_info(&ofdev->dev, "waiting for firmware %s\n",
			 filename);

		/*
		 * We call request_firmware_nowait instead of
		 * request_firmware so that the driver can load and
		 * initialize the ports without holding up the rest of
		 * the kernel.  If hotplug support is enabled in the
		 * kernel, then we use it.
		 */
		ret = request_firmware_nowait(THIS_MODULE,
					      FW_ACTION_HOTPLUG, filename, &ofdev->dev,
					      GFP_KERNEL, &ofdev->dev, uart_firmware_cont);
		if (ret) {
			dev_err(&ofdev->dev,
				"could not load firmware %s\n",
				filename);
			return ret;
		}
	}
	return 0;
}

#else /* !CONFIG_PPC32 */

static int soft_uart_init(struct platform_device *ofdev)
{
	return 0;
}

#endif


static int ucc_uart_probe(struct platform_device *ofdev)
{
	struct device_node *np = ofdev->dev.of_node;
	const char *sprop;      /* String OF properties */
	struct uart_qe_port *qe_port = NULL;
	struct resource res;
	u32 val;
	int ret;

	/*
	 * Determine if we need Soft-UART mode
	 */
	ret = soft_uart_init(ofdev);
	if (ret)
		return ret;

	qe_port = kzalloc(sizeof(struct uart_qe_port), GFP_KERNEL);
	if (!qe_port) {
		dev_err(&ofdev->dev, "can't allocate QE port structure\n");
		return -ENOMEM;
	}

	/* Search for IRQ and mapbase */
	ret = of_address_to_resource(np, 0, &res);
	if (ret) {
		dev_err(&ofdev->dev, "missing 'reg' property in device tree\n");
		goto out_free;
	}
	if (!res.start) {
		dev_err(&ofdev->dev, "invalid 'reg' property in device tree\n");
		ret = -EINVAL;
		goto out_free;
	}
	qe_port->port.mapbase = res.start;

	/* Get the UCC number (device ID) */
	/* UCCs are numbered 1-7 */
	if (of_property_read_u32(np, "cell-index", &val)) {
		if (of_property_read_u32(np, "device-id", &val)) {
			dev_err(&ofdev->dev, "UCC is unspecified in device tree\n");
			ret = -EINVAL;
			goto out_free;
		}
	}

	if (val < 1 || val > UCC_MAX_NUM) {
		dev_err(&ofdev->dev, "no support for UCC%u\n", val);
		ret = -ENODEV;
		goto out_free;
	}
	qe_port->ucc_num = val - 1;

	/*
	 * In the future, we should not require the BRG to be specified in the
	 * device tree.  If no clock-source is specified, then just pick a BRG
	 * to use.  This requires a new QE library function that manages BRG
	 * assignments.
	 */

	sprop = of_get_property(np, "rx-clock-name", NULL);
	if (!sprop) {
		dev_err(&ofdev->dev, "missing rx-clock-name in device tree\n");
		ret = -ENODEV;
		goto out_free;
	}

	qe_port->us_info.rx_clock = qe_clock_source(sprop);
	if ((qe_port->us_info.rx_clock < QE_BRG1) ||
	    (qe_port->us_info.rx_clock > QE_BRG16)) {
		dev_err(&ofdev->dev, "rx-clock-name must be a BRG for UART\n");
		ret = -ENODEV;
		goto out_free;
	}

#ifdef LOOPBACK
	/* In internal loopback mode, TX and RX must use the same clock */
	qe_port->us_info.tx_clock = qe_port->us_info.rx_clock;
#else
	sprop = of_get_property(np, "tx-clock-name", NULL);
	if (!sprop) {
		dev_err(&ofdev->dev, "missing tx-clock-name in device tree\n");
		ret = -ENODEV;
		goto out_free;
	}
	qe_port->us_info.tx_clock = qe_clock_source(sprop);
#endif
	if ((qe_port->us_info.tx_clock < QE_BRG1) ||
	    (qe_port->us_info.tx_clock > QE_BRG16)) {
		dev_err(&ofdev->dev, "tx-clock-name must be a BRG for UART\n");
		ret = -ENODEV;
		goto out_free;
	}

	/* Get the port number, numbered 0-3 */
	if (of_property_read_u32(np, "port-number", &val)) {
		dev_err(&ofdev->dev, "missing port-number in device tree\n");
		ret = -EINVAL;
		goto out_free;
	}
	qe_port->port.line = val;
	if (qe_port->port.line >= UCC_MAX_UART) {
		dev_err(&ofdev->dev, "port-number must be 0-%u\n",
			UCC_MAX_UART - 1);
		ret = -EINVAL;
		goto out_free;
	}

	qe_port->port.irq = irq_of_parse_and_map(np, 0);
	if (qe_port->port.irq == 0) {
		dev_err(&ofdev->dev, "could not map IRQ for UCC%u\n",
		       qe_port->ucc_num + 1);
		ret = -EINVAL;
		goto out_free;
	}

	/*
	 * Newer device trees have an "fsl,qe" compatible property for the QE
	 * node, but we still need to support older device trees.
	 */
	np = of_find_compatible_node(NULL, NULL, "fsl,qe");
	if (!np) {
		np = of_find_node_by_type(NULL, "qe");
		if (!np) {
			dev_err(&ofdev->dev, "could not find 'qe' node\n");
			ret = -EINVAL;
			goto out_free;
		}
	}

	if (of_property_read_u32(np, "brg-frequency", &val)) {
		dev_err(&ofdev->dev,
		       "missing brg-frequency in device tree\n");
		ret = -EINVAL;
		goto out_np;
	}

	if (val)
		qe_port->port.uartclk = val;
	else {
		if (!IS_ENABLED(CONFIG_PPC32)) {
			dev_err(&ofdev->dev,
				"invalid brg-frequency in device tree\n");
			ret = -EINVAL;
			goto out_np;
		}

		/*
		 * Older versions of U-Boot do not initialize the brg-frequency
		 * property, so in this case we assume the BRG frequency is
		 * half the QE bus frequency.
		 */
		if (of_property_read_u32(np, "bus-frequency", &val)) {
			dev_err(&ofdev->dev,
				"missing QE bus-frequency in device tree\n");
			ret = -EINVAL;
			goto out_np;
		}
		if (val)
			qe_port->port.uartclk = val / 2;
		else {
			dev_err(&ofdev->dev,
				"invalid QE bus-frequency in device tree\n");
			ret = -EINVAL;
			goto out_np;
		}
	}

	spin_lock_init(&qe_port->port.lock);
	qe_port->np = np;
	qe_port->port.dev = &ofdev->dev;
	qe_port->port.ops = &qe_uart_pops;
	qe_port->port.iotype = UPIO_MEM;

	qe_port->tx_nrfifos = TX_NUM_FIFO;
	qe_port->tx_fifosize = TX_BUF_SIZE;
	qe_port->rx_nrfifos = RX_NUM_FIFO;
	qe_port->rx_fifosize = RX_BUF_SIZE;

	qe_port->wait_closing = UCC_WAIT_CLOSING;
	qe_port->port.fifosize = 512;
	qe_port->port.flags = UPF_BOOT_AUTOCONF | UPF_IOREMAP;

	qe_port->us_info.ucc_num = qe_port->ucc_num;
	qe_port->us_info.regs = (phys_addr_t) res.start;
	qe_port->us_info.irq = qe_port->port.irq;

	qe_port->us_info.rx_bd_ring_len = qe_port->rx_nrfifos;
	qe_port->us_info.tx_bd_ring_len = qe_port->tx_nrfifos;

	/* Make sure ucc_slow_init() initializes both TX and RX */
	qe_port->us_info.init_tx = 1;
	qe_port->us_info.init_rx = 1;

	/* Add the port to the uart sub-system.  This will cause
	 * qe_uart_config_port() to be called, so the us_info structure must
	 * be initialized.
	 */
	ret = uart_add_one_port(&ucc_uart_driver, &qe_port->port);
	if (ret) {
		dev_err(&ofdev->dev, "could not add /dev/ttyQE%u\n",
		       qe_port->port.line);
		goto out_np;
	}

	platform_set_drvdata(ofdev, qe_port);

	dev_info(&ofdev->dev, "UCC%u assigned to /dev/ttyQE%u\n",
		qe_port->ucc_num + 1, qe_port->port.line);

	/* Display the mknod command for this device */
	dev_dbg(&ofdev->dev, "mknod command is 'mknod /dev/ttyQE%u c %u %u'\n",
	       qe_port->port.line, SERIAL_QE_MAJOR,
	       SERIAL_QE_MINOR + qe_port->port.line);

	return 0;
out_np:
	of_node_put(np);
out_free:
	kfree(qe_port);
	return ret;
}

static int ucc_uart_remove(struct platform_device *ofdev)
{
	struct uart_qe_port *qe_port = platform_get_drvdata(ofdev);

	dev_info(&ofdev->dev, "removing /dev/ttyQE%u\n", qe_port->port.line);

	uart_remove_one_port(&ucc_uart_driver, &qe_port->port);

	kfree(qe_port);

	return 0;
}

static const struct of_device_id ucc_uart_match[] = {
	{
		.type = "serial",
		.compatible = "ucc_uart",
	},
	{
		.compatible = "fsl,t1040-ucc-uart",
	},
	{},
};
MODULE_DEVICE_TABLE(of, ucc_uart_match);

static struct platform_driver ucc_uart_of_driver = {
	.driver = {
		.name = "ucc_uart",
		.of_match_table    = ucc_uart_match,
	},
	.probe  	= ucc_uart_probe,
	.remove 	= ucc_uart_remove,
};

static int __init ucc_uart_init(void)
{
	int ret;

	printk(KERN_INFO "Freescale QUICC Engine UART device driver\n");
#ifdef LOOPBACK
	printk(KERN_INFO "ucc-uart: Using loopback mode\n");
#endif

	ret = uart_register_driver(&ucc_uart_driver);
	if (ret) {
		printk(KERN_ERR "ucc-uart: could not register UART driver\n");
		return ret;
	}

	ret = platform_driver_register(&ucc_uart_of_driver);
	if (ret) {
		printk(KERN_ERR
		       "ucc-uart: could not register platform driver\n");
		uart_unregister_driver(&ucc_uart_driver);
	}

	return ret;
}

static void __exit ucc_uart_exit(void)
{
	printk(KERN_INFO
	       "Freescale QUICC Engine UART device driver unloading\n");

	platform_driver_unregister(&ucc_uart_of_driver);
	uart_unregister_driver(&ucc_uart_driver);
}

module_init(ucc_uart_init);
module_exit(ucc_uart_exit);

MODULE_DESCRIPTION("Freescale QUICC Engine (QE) UART");
MODULE_AUTHOR("Timur Tabi <timur@freescale.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS_CHARDEV_MAJOR(SERIAL_QE_MAJOR);


/*
 *  winbond-cir.c - Driver for the Consumer IR functionality of Winbond
 *                  SuperI/O chips.
 *
 *  Currently supports the Winbond WPCD376i chip (PNP id WEC1022), but
 *  could probably support others (Winbond WEC102X, NatSemi, etc)
 *  with minor modifications.
 *
 *  Original Author: David Härdeman <david@hardeman.nu>
 *     Copyright (C) 2012 Sean Young <sean@mess.org>
 *     Copyright (C) 2009 - 2011 David Härdeman <david@hardeman.nu>
 *
 *  Dedicated to my daughter Matilda, without whose loving attention this
 *  driver would have been finished in half the time and with a fraction
 *  of the bugs.
 *
 *  Written using:
 *    o Winbond WPCD376I datasheet helpfully provided by Jesse Barnes at Intel
 *    o NatSemi PC87338/PC97338 datasheet (for the serial port stuff)
 *    o DSDT dumps
 *
 *  Supported features:
 *    o IR Receive
 *    o IR Transmit
 *    o Wake-On-CIR functionality
 *    o Carrier detection
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/pnp.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/leds.h>
#include <linux/spinlock.h>
#include <linux/pci_ids.h>
#include <linux/io.h>
#include <linux/bitrev.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <media/rc-core.h>

#define DRVNAME "winbond-cir"

/* CEIR Wake-Up Registers, relative to data->wbase                      */
#define WBCIR_REG_WCEIR_CTL	0x03 /* CEIR Receiver Control		*/
#define WBCIR_REG_WCEIR_STS	0x04 /* CEIR Receiver Status		*/
#define WBCIR_REG_WCEIR_EV_EN	0x05 /* CEIR Receiver Event Enable	*/
#define WBCIR_REG_WCEIR_CNTL	0x06 /* CEIR Receiver Counter Low	*/
#define WBCIR_REG_WCEIR_CNTH	0x07 /* CEIR Receiver Counter High	*/
#define WBCIR_REG_WCEIR_INDEX	0x08 /* CEIR Receiver Index		*/
#define WBCIR_REG_WCEIR_DATA	0x09 /* CEIR Receiver Data		*/
#define WBCIR_REG_WCEIR_CSL	0x0A /* CEIR Re. Compare Strlen		*/
#define WBCIR_REG_WCEIR_CFG1	0x0B /* CEIR Re. Configuration 1	*/
#define WBCIR_REG_WCEIR_CFG2	0x0C /* CEIR Re. Configuration 2	*/

/* CEIR Enhanced Functionality Registers, relative to data->ebase       */
#define WBCIR_REG_ECEIR_CTS	0x00 /* Enhanced IR Control Status	*/
#define WBCIR_REG_ECEIR_CCTL	0x01 /* Infrared Counter Control	*/
#define WBCIR_REG_ECEIR_CNT_LO	0x02 /* Infrared Counter LSB		*/
#define WBCIR_REG_ECEIR_CNT_HI	0x03 /* Infrared Counter MSB		*/
#define WBCIR_REG_ECEIR_IREM	0x04 /* Infrared Emitter Status		*/

/* SP3 Banked Registers, relative to data->sbase                        */
#define WBCIR_REG_SP3_BSR	0x03 /* Bank Select, all banks		*/
				      /* Bank 0				*/
#define WBCIR_REG_SP3_RXDATA	0x00 /* FIFO RX data (r)		*/
#define WBCIR_REG_SP3_TXDATA	0x00 /* FIFO TX data (w)		*/
#define WBCIR_REG_SP3_IER	0x01 /* Interrupt Enable		*/
#define WBCIR_REG_SP3_EIR	0x02 /* Event Identification (r)	*/
#define WBCIR_REG_SP3_FCR	0x02 /* FIFO Control (w)		*/
#define WBCIR_REG_SP3_MCR	0x04 /* Mode Control			*/
#define WBCIR_REG_SP3_LSR	0x05 /* Link Status			*/
#define WBCIR_REG_SP3_MSR	0x06 /* Modem Status			*/
#define WBCIR_REG_SP3_ASCR	0x07 /* Aux Status and Control		*/
				      /* Bank 2				*/
#define WBCIR_REG_SP3_BGDL	0x00 /* Baud Divisor LSB		*/
#define WBCIR_REG_SP3_BGDH	0x01 /* Baud Divisor MSB		*/
#define WBCIR_REG_SP3_EXCR1	0x02 /* Extended Control 1		*/
#define WBCIR_REG_SP3_EXCR2	0x04 /* Extended Control 2		*/
#define WBCIR_REG_SP3_TXFLV	0x06 /* TX FIFO Level			*/
#define WBCIR_REG_SP3_RXFLV	0x07 /* RX FIFO Level			*/
				      /* Bank 3				*/
#define WBCIR_REG_SP3_MRID	0x00 /* Module Identification		*/
#define WBCIR_REG_SP3_SH_LCR	0x01 /* LCR Shadow			*/
#define WBCIR_REG_SP3_SH_FCR	0x02 /* FCR Shadow			*/
				      /* Bank 4				*/
#define WBCIR_REG_SP3_IRCR1	0x02 /* Infrared Control 1		*/
				      /* Bank 5				*/
#define WBCIR_REG_SP3_IRCR2	0x04 /* Infrared Control 2		*/
				      /* Bank 6				*/
#define WBCIR_REG_SP3_IRCR3	0x00 /* Infrared Control 3		*/
#define WBCIR_REG_SP3_SIR_PW	0x02 /* SIR Pulse Width			*/
				      /* Bank 7				*/
#define WBCIR_REG_SP3_IRRXDC	0x00 /* IR RX Demod Control		*/
#define WBCIR_REG_SP3_IRTXMC	0x01 /* IR TX Mod Control		*/
#define WBCIR_REG_SP3_RCCFG	0x02 /* CEIR Config			*/
#define WBCIR_REG_SP3_IRCFG1	0x04 /* Infrared Config 1		*/
#define WBCIR_REG_SP3_IRCFG4	0x07 /* Infrared Config 4		*/

/*
 * Magic values follow
 */

/* No interrupts for WBCIR_REG_SP3_IER and WBCIR_REG_SP3_EIR */
#define WBCIR_IRQ_NONE		0x00
/* RX data bit for WBCIR_REG_SP3_IER and WBCIR_REG_SP3_EIR */
#define WBCIR_IRQ_RX		0x01
/* TX data low bit for WBCIR_REG_SP3_IER and WBCIR_REG_SP3_EIR */
#define WBCIR_IRQ_TX_LOW	0x02
/* Over/Under-flow bit for WBCIR_REG_SP3_IER and WBCIR_REG_SP3_EIR */
#define WBCIR_IRQ_ERR		0x04
/* TX data empty bit for WBCEIR_REG_SP3_IER and WBCIR_REG_SP3_EIR */
#define WBCIR_IRQ_TX_EMPTY	0x20
/* Led enable/disable bit for WBCIR_REG_ECEIR_CTS */
#define WBCIR_LED_ENABLE	0x80
/* RX data available bit for WBCIR_REG_SP3_LSR */
#define WBCIR_RX_AVAIL		0x01
/* RX data overrun error bit for WBCIR_REG_SP3_LSR */
#define WBCIR_RX_OVERRUN	0x02
/* TX End-Of-Transmission bit for WBCIR_REG_SP3_ASCR */
#define WBCIR_TX_EOT		0x04
/* RX disable bit for WBCIR_REG_SP3_ASCR */
#define WBCIR_RX_DISABLE	0x20
/* TX data underrun error bit for WBCIR_REG_SP3_ASCR */
#define WBCIR_TX_UNDERRUN	0x40
/* Extended mode enable bit for WBCIR_REG_SP3_EXCR1 */
#define WBCIR_EXT_ENABLE	0x01
/* Select compare register in WBCIR_REG_WCEIR_INDEX (bits 5 & 6) */
#define WBCIR_REGSEL_COMPARE	0x10
/* Select mask register in WBCIR_REG_WCEIR_INDEX (bits 5 & 6) */
#define WBCIR_REGSEL_MASK	0x20
/* Starting address of selected register in WBCIR_REG_WCEIR_INDEX */
#define WBCIR_REG_ADDR0		0x00
/* Enable carrier counter */
#define WBCIR_CNTR_EN		0x01
/* Reset carrier counter */
#define WBCIR_CNTR_R		0x02
/* Invert TX */
#define WBCIR_IRTX_INV		0x04
/* Receiver oversampling */
#define WBCIR_RX_T_OV		0x40

/* Valid banks for the SP3 UART */
enum wbcir_bank {
	WBCIR_BANK_0          = 0x00,
	WBCIR_BANK_1          = 0x80,
	WBCIR_BANK_2          = 0xE0,
	WBCIR_BANK_3          = 0xE4,
	WBCIR_BANK_4          = 0xE8,
	WBCIR_BANK_5          = 0xEC,
	WBCIR_BANK_6          = 0xF0,
	WBCIR_BANK_7          = 0xF4,
};

/* Supported power-on IR Protocols */
enum wbcir_protocol {
	IR_PROTOCOL_RC5          = 0x0,
	IR_PROTOCOL_NEC          = 0x1,
	IR_PROTOCOL_RC6          = 0x2,
};

/* Possible states for IR reception */
enum wbcir_rxstate {
	WBCIR_RXSTATE_INACTIVE = 0,
	WBCIR_RXSTATE_ACTIVE,
	WBCIR_RXSTATE_ERROR
};

/* Possible states for IR transmission */
enum wbcir_txstate {
	WBCIR_TXSTATE_INACTIVE = 0,
	WBCIR_TXSTATE_ACTIVE,
	WBCIR_TXSTATE_ERROR
};

/* Misc */
#define WBCIR_NAME	"Winbond CIR"
#define WBCIR_ID_FAMILY          0xF1 /* Family ID for the WPCD376I	*/
#define	WBCIR_ID_CHIP            0x04 /* Chip ID for the WPCD376I	*/
#define INVALID_SCANCODE   0x7FFFFFFF /* Invalid with all protos	*/
#define WAKEUP_IOMEM_LEN         0x10 /* Wake-Up I/O Reg Len		*/
#define EHFUNC_IOMEM_LEN         0x10 /* Enhanced Func I/O Reg Len	*/
#define SP_IOMEM_LEN             0x08 /* Serial Port 3 (IR) Reg Len	*/

/* Per-device data */
struct wbcir_data {
	spinlock_t spinlock;
	struct rc_dev *dev;
	struct led_classdev led;

	unsigned long wbase;        /* Wake-Up Baseaddr		*/
	unsigned long ebase;        /* Enhanced Func. Baseaddr	*/
	unsigned long sbase;        /* Serial Port Baseaddr	*/
	unsigned int  irq;          /* Serial Port IRQ		*/
	u8 irqmask;

	/* RX state */
	enum wbcir_rxstate rxstate;
	int carrier_report_enabled;
	u32 pulse_duration;

	/* TX state */
	enum wbcir_txstate txstate;
	u32 txlen;
	u32 txoff;
	u32 *txbuf;
	u8 txmask;
	u32 txcarrier;
};

static enum wbcir_protocol protocol = IR_PROTOCOL_RC6;
module_param(protocol, uint, 0444);
MODULE_PARM_DESC(protocol, "IR protocol to use for the power-on command "
		 "(0 = RC5, 1 = NEC, 2 = RC6A, default)");

static bool invert; /* default = 0 */
module_param(invert, bool, 0444);
MODULE_PARM_DESC(invert, "Invert the signal from the IR receiver");

static bool txandrx; /* default = 0 */
module_param(txandrx, bool, 0444);
MODULE_PARM_DESC(txandrx, "Allow simultaneous TX and RX");

static unsigned int wake_sc = 0x800F040C;
module_param(wake_sc, uint, 0644);
MODULE_PARM_DESC(wake_sc, "Scancode of the power-on IR command");

static unsigned int wake_rc6mode = 6;
module_param(wake_rc6mode, uint, 0644);
MODULE_PARM_DESC(wake_rc6mode, "RC6 mode for the power-on command "
		 "(0 = 0, 6 = 6A, default)");



/*****************************************************************************
 *
 * UTILITY FUNCTIONS
 *
 *****************************************************************************/

/* Caller needs to hold wbcir_lock */
static void
wbcir_set_bits(unsigned long addr, u8 bits, u8 mask)
{
	u8 val;

	val = inb(addr);
	val = ((val & ~mask) | (bits & mask));
	outb(val, addr);
}

/* Selects the register bank for the serial port */
static inline void
wbcir_select_bank(struct wbcir_data *data, enum wbcir_bank bank)
{
	outb(bank, data->sbase + WBCIR_REG_SP3_BSR);
}

static inline void
wbcir_set_irqmask(struct wbcir_data *data, u8 irqmask)
{
	if (data->irqmask == irqmask)
		return;

	wbcir_select_bank(data, WBCIR_BANK_0);
	outb(irqmask, data->sbase + WBCIR_REG_SP3_IER);
	data->irqmask = irqmask;
}

static enum led_brightness
wbcir_led_brightness_get(struct led_classdev *led_cdev)
{
	struct wbcir_data *data = container_of(led_cdev,
					       struct wbcir_data,
					       led);

	if (inb(data->ebase + WBCIR_REG_ECEIR_CTS) & WBCIR_LED_ENABLE)
		return LED_FULL;
	else
		return LED_OFF;
}

static void
wbcir_led_brightness_set(struct led_classdev *led_cdev,
			 enum led_brightness brightness)
{
	struct wbcir_data *data = container_of(led_cdev,
					       struct wbcir_data,
					       led);

	wbcir_set_bits(data->ebase + WBCIR_REG_ECEIR_CTS,
		       brightness == LED_OFF ? 0x00 : WBCIR_LED_ENABLE,
		       WBCIR_LED_ENABLE);
}

/* Manchester encodes bits to RC6 message cells (see wbcir_shutdown) */
static u8
wbcir_to_rc6cells(u8 val)
{
	u8 coded = 0x00;
	int i;

	val &= 0x0F;
	for (i = 0; i < 4; i++) {
		if (val & 0x01)
			coded |= 0x02 << (i * 2);
		else
			coded |= 0x01 << (i * 2);
		val >>= 1;
	}

	return coded;
}

/*****************************************************************************
 *
 * INTERRUPT FUNCTIONS
 *
 *****************************************************************************/

static void
wbcir_carrier_report(struct wbcir_data *data)
{
	unsigned counter = inb(data->ebase + WBCIR_REG_ECEIR_CNT_LO) |
			inb(data->ebase + WBCIR_REG_ECEIR_CNT_HI) << 8;

	if (counter > 0 && counter < 0xffff) {
		DEFINE_IR_RAW_EVENT(ev);

		ev.carrier_report = 1;
		ev.carrier = DIV_ROUND_CLOSEST(counter * 1000000u,
						data->pulse_duration);

		ir_raw_event_store(data->dev, &ev);
	}

	/* reset and restart the counter */
	data->pulse_duration = 0;
	wbcir_set_bits(data->ebase + WBCIR_REG_ECEIR_CCTL, WBCIR_CNTR_R,
						WBCIR_CNTR_EN | WBCIR_CNTR_R);
	wbcir_set_bits(data->ebase + WBCIR_REG_ECEIR_CCTL, WBCIR_CNTR_EN,
						WBCIR_CNTR_EN | WBCIR_CNTR_R);
}

static void
wbcir_idle_rx(struct rc_dev *dev, bool idle)
{
	struct wbcir_data *data = dev->priv;

	if (!idle && data->rxstate == WBCIR_RXSTATE_INACTIVE)
		data->rxstate = WBCIR_RXSTATE_ACTIVE;

	if (idle && data->rxstate != WBCIR_RXSTATE_INACTIVE) {
		data->rxstate = WBCIR_RXSTATE_INACTIVE;

		if (data->carrier_report_enabled)
			wbcir_carrier_report(data);

		/* Tell hardware to go idle by setting RXINACTIVE */
		outb(WBCIR_RX_DISABLE, data->sbase + WBCIR_REG_SP3_ASCR);
	}
}

static void
wbcir_irq_rx(struct wbcir_data *data, struct pnp_dev *device)
{
	u8 irdata;
	DEFINE_IR_RAW_EVENT(rawir);
	unsigned duration;

	/* Since RXHDLEV is set, at least 8 bytes are in the FIFO */
	while (inb(data->sbase + WBCIR_REG_SP3_LSR) & WBCIR_RX_AVAIL) {
		irdata = inb(data->sbase + WBCIR_REG_SP3_RXDATA);
		if (data->rxstate == WBCIR_RXSTATE_ERROR)
			continue;

		duration = ((irdata & 0x7F) + 1) *
			(data->carrier_report_enabled ? 2 : 10);
		rawir.pulse = irdata & 0x80 ? false : true;
		rawir.duration = US_TO_NS(duration);

		if (rawir.pulse)
			data->pulse_duration += duration;

		ir_raw_event_store_with_filter(data->dev, &rawir);
	}

	ir_raw_event_handle(data->dev);
}

static void
wbcir_irq_tx(struct wbcir_data *data)
{
	unsigned int space;
	unsigned int used;
	u8 bytes[16];
	u8 byte;

	if (!data->txbuf)
		return;

	switch (data->txstate) {
	case WBCIR_TXSTATE_INACTIVE:
		/* TX FIFO empty */
		space = 16;
		break;
	case WBCIR_TXSTATE_ACTIVE:
		/* TX FIFO low (3 bytes or less) */
		space = 13;
		break;
	case WBCIR_TXSTATE_ERROR:
		space = 0;
		break;
	default:
		return;
	}

	/*
	 * TX data is run-length coded in bytes: YXXXXXXX
	 * Y = space (1) or pulse (0)
	 * X = duration, encoded as (X + 1) * 10us (i.e 10 to 1280 us)
	 */
	for (used = 0; used < space && data->txoff != data->txlen; used++) {
		if (data->txbuf[data->txoff] == 0) {
			data->txoff++;
			continue;
		}
		byte = min((u32)0x80, data->txbuf[data->txoff]);
		data->txbuf[data->txoff] -= byte;
		byte--;
		byte |= (data->txoff % 2 ? 0x80 : 0x00); /* pulse/space */
		bytes[used] = byte;
	}

	while (data->txbuf[data->txoff] == 0 && data->txoff != data->txlen)
		data->txoff++;

	if (used == 0) {
		/* Finished */
		if (data->txstate == WBCIR_TXSTATE_ERROR)
			/* Clear TX underrun bit */
			outb(WBCIR_TX_UNDERRUN, data->sbase + WBCIR_REG_SP3_ASCR);
		wbcir_set_irqmask(data, WBCIR_IRQ_RX | WBCIR_IRQ_ERR);
		kfree(data->txbuf);
		data->txbuf = NULL;
		data->txstate = WBCIR_TXSTATE_INACTIVE;
	} else if (data->txoff == data->txlen) {
		/* At the end of transmission, tell the hw before last byte */
		outsb(data->sbase + WBCIR_REG_SP3_TXDATA, bytes, used - 1);
		outb(WBCIR_TX_EOT, data->sbase + WBCIR_REG_SP3_ASCR);
		outb(bytes[used - 1], data->sbase + WBCIR_REG_SP3_TXDATA);
		wbcir_set_irqmask(data, WBCIR_IRQ_RX | WBCIR_IRQ_ERR |
				  WBCIR_IRQ_TX_EMPTY);
	} else {
		/* More data to follow... */
		outsb(data->sbase + WBCIR_REG_SP3_RXDATA, bytes, used);
		if (data->txstate == WBCIR_TXSTATE_INACTIVE) {
			wbcir_set_irqmask(data, WBCIR_IRQ_RX | WBCIR_IRQ_ERR |
					  WBCIR_IRQ_TX_LOW);
			data->txstate = WBCIR_TXSTATE_ACTIVE;
		}
	}
}

static irqreturn_t
wbcir_irq_handler(int irqno, void *cookie)
{
	struct pnp_dev *device = cookie;
	struct wbcir_data *data = pnp_get_drvdata(device);
	unsigned long flags;
	u8 status;

	spin_lock_irqsave(&data->spinlock, flags);
	wbcir_select_bank(data, WBCIR_BANK_0);
	status = inb(data->sbase + WBCIR_REG_SP3_EIR);
	status &= data->irqmask;

	if (!status) {
		spin_unlock_irqrestore(&data->spinlock, flags);
		return IRQ_NONE;
	}

	if (status & WBCIR_IRQ_ERR) {
		/* RX overflow? (read clears bit) */
		if (inb(data->sbase + WBCIR_REG_SP3_LSR) & WBCIR_RX_OVERRUN) {
			data->rxstate = WBCIR_RXSTATE_ERROR;
			ir_raw_event_reset(data->dev);
		}

		/* TX underflow? */
		if (inb(data->sbase + WBCIR_REG_SP3_ASCR) & WBCIR_TX_UNDERRUN)
			data->txstate = WBCIR_TXSTATE_ERROR;
	}

	if (status & WBCIR_IRQ_RX)
		wbcir_irq_rx(data, device);

	if (status & (WBCIR_IRQ_TX_LOW | WBCIR_IRQ_TX_EMPTY))
		wbcir_irq_tx(data);

	spin_unlock_irqrestore(&data->spinlock, flags);
	return IRQ_HANDLED;
}

/*****************************************************************************
 *
 * RC-CORE INTERFACE FUNCTIONS
 *
 *****************************************************************************/

static int
wbcir_set_carrier_report(struct rc_dev *dev, int enable)
{
	struct wbcir_data *data = dev->priv;
	unsigned long flags;

	spin_lock_irqsave(&data->spinlock, flags);

	if (data->carrier_report_enabled == enable) {
		spin_unlock_irqrestore(&data->spinlock, flags);
		return 0;
	}

	data->pulse_duration = 0;
	wbcir_set_bits(data->ebase + WBCIR_REG_ECEIR_CCTL, WBCIR_CNTR_R,
						WBCIR_CNTR_EN | WBCIR_CNTR_R);

	if (enable && data->dev->idle)
		wbcir_set_bits(data->ebase + WBCIR_REG_ECEIR_CCTL,
				WBCIR_CNTR_EN, WBCIR_CNTR_EN | WBCIR_CNTR_R);

	/* Set a higher sampling resolution if carrier reports are enabled */
	wbcir_select_bank(data, WBCIR_BANK_2);
	data->dev->rx_resolution = US_TO_NS(enable ? 2 : 10);
	outb(enable ? 0x03 : 0x0f, data->sbase + WBCIR_REG_SP3_BGDL);
	outb(0x00, data->sbase + WBCIR_REG_SP3_BGDH);

	/* Enable oversampling if carrier reports are enabled */
	wbcir_select_bank(data, WBCIR_BANK_7);
	wbcir_set_bits(data->sbase + WBCIR_REG_SP3_RCCFG,
				enable ? WBCIR_RX_T_OV : 0, WBCIR_RX_T_OV);

	data->carrier_report_enabled = enable;
	spin_unlock_irqrestore(&data->spinlock, flags);

	return 0;
}

static int
wbcir_txcarrier(struct rc_dev *dev, u32 carrier)
{
	struct wbcir_data *data = dev->priv;
	unsigned long flags;
	u8 val;
	u32 freq;

	freq = DIV_ROUND_CLOSEST(carrier, 1000);
	if (freq < 30 || freq > 60)
		return -EINVAL;

	switch (freq) {
	case 58:
	case 59:
	case 60:
		val = freq - 58;
		freq *= 1000;
		break;
	case 57:
		val = freq - 27;
		freq = 56900;
		break;
	default:
		val = freq - 27;
		freq *= 1000;
		break;
	}

	spin_lock_irqsave(&data->spinlock, flags);
	if (data->txstate != WBCIR_TXSTATE_INACTIVE) {
		spin_unlock_irqrestore(&data->spinlock, flags);
		return -EBUSY;
	}

	if (data->txcarrier != freq) {
		wbcir_select_bank(data, WBCIR_BANK_7);
		wbcir_set_bits(data->sbase + WBCIR_REG_SP3_IRTXMC, val, 0x1F);
		data->txcarrier = freq;
	}

	spin_unlock_irqrestore(&data->spinlock, flags);
	return 0;
}

static int
wbcir_txmask(struct rc_dev *dev, u32 mask)
{
	struct wbcir_data *data = dev->priv;
	unsigned long flags;
	u8 val;

	/* return the number of transmitters */
	if (mask > 15)
		return 4;

	/* Four outputs, only one output can be enabled at a time */
	switch (mask) {
	case 0x1:
		val = 0x0;
		break;
	case 0x2:
		val = 0x1;
		break;
	case 0x4:
		val = 0x2;
		break;
	case 0x8:
		val = 0x3;
		break;
	default:
		return -EINVAL;
	}

	spin_lock_irqsave(&data->spinlock, flags);
	if (data->txstate != WBCIR_TXSTATE_INACTIVE) {
		spin_unlock_irqrestore(&data->spinlock, flags);
		return -EBUSY;
	}

	if (data->txmask != mask) {
		wbcir_set_bits(data->ebase + WBCIR_REG_ECEIR_CTS, val, 0x0c);
		data->txmask = mask;
	}

	spin_unlock_irqrestore(&data->spinlock, flags);
	return 0;
}

static int
wbcir_tx(struct rc_dev *dev, unsigned *b, unsigned count)
{
	struct wbcir_data *data = dev->priv;
	unsigned *buf;
	unsigned i;
	unsigned long flags;

	buf = kmalloc(count * sizeof(*b), GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	/* Convert values to multiples of 10us */
	for (i = 0; i < count; i++)
		buf[i] = DIV_ROUND_CLOSEST(b[i], 10);

	/* Not sure if this is possible, but better safe than sorry */
	spin_lock_irqsave(&data->spinlock, flags);
	if (data->txstate != WBCIR_TXSTATE_INACTIVE) {
		spin_unlock_irqrestore(&data->spinlock, flags);
		kfree(buf);
		return -EBUSY;
	}

	/* Fill the TX fifo once, the irq handler will do the rest */
	data->txbuf = buf;
	data->txlen = count;
	data->txoff = 0;
	wbcir_irq_tx(data);

	/* We're done */
	spin_unlock_irqrestore(&data->spinlock, flags);
	return count;
}

/*****************************************************************************
 *
 * SETUP/INIT/SUSPEND/RESUME FUNCTIONS
 *
 *****************************************************************************/

static void
wbcir_shutdown(struct pnp_dev *device)
{
	struct device *dev = &device->dev;
	struct wbcir_data *data = pnp_get_drvdata(device);
	bool do_wake = true;
	u8 match[11];
	u8 mask[11];
	u8 rc6_csl = 0;
	int i;

	memset(match, 0, sizeof(match));
	memset(mask, 0, sizeof(mask));

	if (wake_sc == INVALID_SCANCODE || !device_may_wakeup(dev)) {
		do_wake = false;
		goto finish;
	}

	switch (protocol) {
	case IR_PROTOCOL_RC5:
		if (wake_sc > 0xFFF) {
			do_wake = false;
			dev_err(dev, "RC5 - Invalid wake scancode\n");
			break;
		}

		/* Mask = 13 bits, ex toggle */
		mask[0] = 0xFF;
		mask[1] = 0x17;

		match[0]  = (wake_sc & 0x003F);      /* 6 command bits */
		match[0] |= (wake_sc & 0x0180) >> 1; /* 2 address bits */
		match[1]  = (wake_sc & 0x0E00) >> 9; /* 3 address bits */
		if (!(wake_sc & 0x0040))             /* 2nd start bit  */
			match[1] |= 0x10;

		break;

	case IR_PROTOCOL_NEC:
		if (wake_sc > 0xFFFFFF) {
			do_wake = false;
			dev_err(dev, "NEC - Invalid wake scancode\n");
			break;
		}

		mask[0] = mask[1] = mask[2] = mask[3] = 0xFF;

		match[1] = bitrev8((wake_sc & 0xFF));
		match[0] = ~match[1];

		match[3] = bitrev8((wake_sc & 0xFF00) >> 8);
		if (wake_sc > 0xFFFF)
			match[2] = bitrev8((wake_sc & 0xFF0000) >> 16);
		else
			match[2] = ~match[3];

		break;

	case IR_PROTOCOL_RC6:

		if (wake_rc6mode == 0) {
			if (wake_sc > 0xFFFF) {
				do_wake = false;
				dev_err(dev, "RC6 - Invalid wake scancode\n");
				break;
			}

			/* Command */
			match[0] = wbcir_to_rc6cells(wake_sc >>  0);
			mask[0]  = 0xFF;
			match[1] = wbcir_to_rc6cells(wake_sc >>  4);
			mask[1]  = 0xFF;

			/* Address */
			match[2] = wbcir_to_rc6cells(wake_sc >>  8);
			mask[2]  = 0xFF;
			match[3] = wbcir_to_rc6cells(wake_sc >> 12);
			mask[3]  = 0xFF;

			/* Header */
			match[4] = 0x50; /* mode1 = mode0 = 0, ignore toggle */
			mask[4]  = 0xF0;
			match[5] = 0x09; /* start bit = 1, mode2 = 0 */
			mask[5]  = 0x0F;

			rc6_csl = 44;

		} else if (wake_rc6mode == 6) {
			i = 0;

			/* Command */
			match[i]  = wbcir_to_rc6cells(wake_sc >>  0);
			mask[i++] = 0xFF;
			match[i]  = wbcir_to_rc6cells(wake_sc >>  4);
			mask[i++] = 0xFF;

			/* Address + Toggle */
			match[i]  = wbcir_to_rc6cells(wake_sc >>  8);
			mask[i++] = 0xFF;
			match[i]  = wbcir_to_rc6cells(wake_sc >> 12);
			mask[i++] = 0x3F;

			/* Customer bits 7 - 0 */
			match[i]  = wbcir_to_rc6cells(wake_sc >> 16);
			mask[i++] = 0xFF;
			match[i]  = wbcir_to_rc6cells(wake_sc >> 20);
			mask[i++] = 0xFF;

			if (wake_sc & 0x80000000) {
				/* Customer range bit and bits 15 - 8 */
				match[i]  = wbcir_to_rc6cells(wake_sc >> 24);
				mask[i++] = 0xFF;
				match[i]  = wbcir_to_rc6cells(wake_sc >> 28);
				mask[i++] = 0xFF;
				rc6_csl = 76;
			} else if (wake_sc <= 0x007FFFFF) {
				rc6_csl = 60;
			} else {
				do_wake = false;
				dev_err(dev, "RC6 - Invalid wake scancode\n");
				break;
			}

			/* Header */
			match[i]  = 0x93; /* mode1 = mode0 = 1, submode = 0 */
			mask[i++] = 0xFF;
			match[i]  = 0x0A; /* start bit = 1, mode2 = 1 */
			mask[i++] = 0x0F;

		} else {
			do_wake = false;
			dev_err(dev, "RC6 - Invalid wake mode\n");
		}

		break;

	default:
		do_wake = false;
		break;
	}

finish:
	if (do_wake) {
		/* Set compare and compare mask */
		wbcir_set_bits(data->wbase + WBCIR_REG_WCEIR_INDEX,
			       WBCIR_REGSEL_COMPARE | WBCIR_REG_ADDR0,
			       0x3F);
		outsb(data->wbase + WBCIR_REG_WCEIR_DATA, match, 11);
		wbcir_set_bits(data->wbase + WBCIR_REG_WCEIR_INDEX,
			       WBCIR_REGSEL_MASK | WBCIR_REG_ADDR0,
			       0x3F);
		outsb(data->wbase + WBCIR_REG_WCEIR_DATA, mask, 11);

		/* RC6 Compare String Len */
		outb(rc6_csl, data->wbase + WBCIR_REG_WCEIR_CSL);

		/* Clear status bits NEC_REP, BUFF, MSG_END, MATCH */
		wbcir_set_bits(data->wbase + WBCIR_REG_WCEIR_STS, 0x17, 0x17);

		/* Clear BUFF_EN, Clear END_EN, Set MATCH_EN */
		wbcir_set_bits(data->wbase + WBCIR_REG_WCEIR_EV_EN, 0x01, 0x07);

		/* Set CEIR_EN */
		wbcir_set_bits(data->wbase + WBCIR_REG_WCEIR_CTL, 0x01, 0x01);

	} else {
		/* Clear BUFF_EN, Clear END_EN, Clear MATCH_EN */
		wbcir_set_bits(data->wbase + WBCIR_REG_WCEIR_EV_EN, 0x00, 0x07);

		/* Clear CEIR_EN */
		wbcir_set_bits(data->wbase + WBCIR_REG_WCEIR_CTL, 0x00, 0x01);
	}

	/*
	 * ACPI will set the HW disable bit for SP3 which means that the
	 * output signals are left in an undefined state which may cause
	 * spurious interrupts which we need to ignore until the hardware
	 * is reinitialized.
	 */
	wbcir_set_irqmask(data, WBCIR_IRQ_NONE);
	disable_irq(data->irq);
}

static int
wbcir_suspend(struct pnp_dev *device, pm_message_t state)
{
	struct wbcir_data *data = pnp_get_drvdata(device);
	led_classdev_suspend(&data->led);
	wbcir_shutdown(device);
	return 0;
}

static void
wbcir_init_hw(struct wbcir_data *data)
{
	u8 tmp;

	/* Disable interrupts */
	wbcir_set_irqmask(data, WBCIR_IRQ_NONE);

	/* Set PROT_SEL, RX_INV, Clear CEIR_EN (needed for the led) */
	tmp = protocol << 4;
	if (invert)
		tmp |= 0x08;
	outb(tmp, data->wbase + WBCIR_REG_WCEIR_CTL);

	/* Clear status bits NEC_REP, BUFF, MSG_END, MATCH */
	wbcir_set_bits(data->wbase + WBCIR_REG_WCEIR_STS, 0x17, 0x17);

	/* Clear BUFF_EN, Clear END_EN, Clear MATCH_EN */
	wbcir_set_bits(data->wbase + WBCIR_REG_WCEIR_EV_EN, 0x00, 0x07);

	/* Set RC5 cell time to correspond to 36 kHz */
	wbcir_set_bits(data->wbase + WBCIR_REG_WCEIR_CFG1, 0x4A, 0x7F);

	/* Set IRTX_INV */
	if (invert)
		outb(WBCIR_IRTX_INV, data->ebase + WBCIR_REG_ECEIR_CCTL);
	else
		outb(0x00, data->ebase + WBCIR_REG_ECEIR_CCTL);

	/*
	 * Clear IR LED, set SP3 clock to 24Mhz, set TX mask to IRTX1,
	 * set SP3_IRRX_SW to binary 01, helpfully not documented
	 */
	outb(0x10, data->ebase + WBCIR_REG_ECEIR_CTS);
	data->txmask = 0x1;

	/* Enable extended mode */
	wbcir_select_bank(data, WBCIR_BANK_2);
	outb(WBCIR_EXT_ENABLE, data->sbase + WBCIR_REG_SP3_EXCR1);

	/*
	 * Configure baud generator, IR data will be sampled at
	 * a bitrate of: (24Mhz * prescaler) / (divisor * 16).
	 *
	 * The ECIR registers include a flag to change the
	 * 24Mhz clock freq to 48Mhz.
	 *
	 * It's not documented in the specs, but fifo levels
	 * other than 16 seems to be unsupported.
	 */

	/* prescaler 1.0, tx/rx fifo lvl 16 */
	outb(0x30, data->sbase + WBCIR_REG_SP3_EXCR2);

	/* Set baud divisor to sample every 10 us */
	outb(0x0f, data->sbase + WBCIR_REG_SP3_BGDL);
	outb(0x00, data->sbase + WBCIR_REG_SP3_BGDH);

	/* Set CEIR mode */
	wbcir_select_bank(data, WBCIR_BANK_0);
	outb(0xC0, data->sbase + WBCIR_REG_SP3_MCR);
	inb(data->sbase + WBCIR_REG_SP3_LSR); /* Clear LSR */
	inb(data->sbase + WBCIR_REG_SP3_MSR); /* Clear MSR */

	/* Disable RX demod, enable run-length enc/dec, set freq span */
	wbcir_select_bank(data, WBCIR_BANK_7);
	outb(0x90, data->sbase + WBCIR_REG_SP3_RCCFG);

	/* Disable timer */
	wbcir_select_bank(data, WBCIR_BANK_4);
	outb(0x00, data->sbase + WBCIR_REG_SP3_IRCR1);

	/* Disable MSR interrupt, clear AUX_IRX, mask RX during TX? */
	wbcir_select_bank(data, WBCIR_BANK_5);
	outb(txandrx ? 0x03 : 0x02, data->sbase + WBCIR_REG_SP3_IRCR2);

	/* Disable CRC */
	wbcir_select_bank(data, WBCIR_BANK_6);
	outb(0x20, data->sbase + WBCIR_REG_SP3_IRCR3);

	/* Set RX demodulation freq, not really used */
	wbcir_select_bank(data, WBCIR_BANK_7);
	outb(0xF2, data->sbase + WBCIR_REG_SP3_IRRXDC);

	/* Set TX modulation, 36kHz, 7us pulse width */
	outb(0x69, data->sbase + WBCIR_REG_SP3_IRTXMC);
	data->txcarrier = 36000;

	/* Set invert and pin direction */
	if (invert)
		outb(0x10, data->sbase + WBCIR_REG_SP3_IRCFG4);
	else
		outb(0x00, data->sbase + WBCIR_REG_SP3_IRCFG4);

	/* Set FIFO thresholds (RX = 8, TX = 3), reset RX/TX */
	wbcir_select_bank(data, WBCIR_BANK_0);
	outb(0x97, data->sbase + WBCIR_REG_SP3_FCR);

	/* Clear AUX status bits */
	outb(0xE0, data->sbase + WBCIR_REG_SP3_ASCR);

	/* Clear RX state */
	data->rxstate = WBCIR_RXSTATE_INACTIVE;
	ir_raw_event_reset(data->dev);
	ir_raw_event_set_idle(data->dev, true);

	/* Clear TX state */
	if (data->txstate == WBCIR_TXSTATE_ACTIVE) {
		kfree(data->txbuf);
		data->txbuf = NULL;
		data->txstate = WBCIR_TXSTATE_INACTIVE;
	}

	/* Enable interrupts */
	wbcir_set_irqmask(data, WBCIR_IRQ_RX | WBCIR_IRQ_ERR);
}

static int
wbcir_resume(struct pnp_dev *device)
{
	struct wbcir_data *data = pnp_get_drvdata(device);

	wbcir_init_hw(data);
	enable_irq(data->irq);
	led_classdev_resume(&data->led);

	return 0;
}

static int
wbcir_probe(struct pnp_dev *device, const struct pnp_device_id *dev_id)
{
	struct device *dev = &device->dev;
	struct wbcir_data *data;
	int err;

	if (!(pnp_port_len(device, 0) == EHFUNC_IOMEM_LEN &&
	      pnp_port_len(device, 1) == WAKEUP_IOMEM_LEN &&
	      pnp_port_len(device, 2) == SP_IOMEM_LEN)) {
		dev_err(dev, "Invalid resources\n");
		return -ENODEV;
	}

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data) {
		err = -ENOMEM;
		goto exit;
	}

	pnp_set_drvdata(device, data);

	spin_lock_init(&data->spinlock);
	data->ebase = pnp_port_start(device, 0);
	data->wbase = pnp_port_start(device, 1);
	data->sbase = pnp_port_start(device, 2);
	data->irq = pnp_irq(device, 0);

	if (data->wbase == 0 || data->ebase == 0 ||
	    data->sbase == 0 || data->irq == 0) {
		err = -ENODEV;
		dev_err(dev, "Invalid resources\n");
		goto exit_free_data;
	}

	dev_dbg(&device->dev, "Found device "
		"(w: 0x%lX, e: 0x%lX, s: 0x%lX, i: %u)\n",
		data->wbase, data->ebase, data->sbase, data->irq);

	data->led.name = "cir::activity";
	data->led.default_trigger = "rc-feedback";
	data->led.brightness_set = wbcir_led_brightness_set;
	data->led.brightness_get = wbcir_led_brightness_get;
	err = led_classdev_register(&device->dev, &data->led);
	if (err)
		goto exit_free_data;

	data->dev = rc_allocate_device();
	if (!data->dev) {
		err = -ENOMEM;
		goto exit_unregister_led;
	}

	data->dev->driver_type = RC_DRIVER_IR_RAW;
	data->dev->driver_name = DRVNAME;
	data->dev->input_name = WBCIR_NAME;
	data->dev->input_phys = "wbcir/cir0";
	data->dev->input_id.bustype = BUS_HOST;
	data->dev->input_id.vendor = PCI_VENDOR_ID_WINBOND;
	data->dev->input_id.product = WBCIR_ID_FAMILY;
	data->dev->input_id.version = WBCIR_ID_CHIP;
	data->dev->map_name = RC_MAP_RC6_MCE;
	data->dev->s_idle = wbcir_idle_rx;
	data->dev->s_carrier_report = wbcir_set_carrier_report;
	data->dev->s_tx_mask = wbcir_txmask;
	data->dev->s_tx_carrier = wbcir_txcarrier;
	data->dev->tx_ir = wbcir_tx;
	data->dev->priv = data;
	data->dev->dev.parent = &device->dev;
	data->dev->timeout = MS_TO_NS(100);
	data->dev->rx_resolution = US_TO_NS(2);
	data->dev->allowed_protocols = RC_BIT_ALL;

	err = rc_register_device(data->dev);
	if (err)
		goto exit_free_rc;

	if (!request_region(data->wbase, WAKEUP_IOMEM_LEN, DRVNAME)) {
		dev_err(dev, "Region 0x%lx-0x%lx already in use!\n",
			data->wbase, data->wbase + WAKEUP_IOMEM_LEN - 1);
		err = -EBUSY;
		goto exit_unregister_device;
	}

	if (!request_region(data->ebase, EHFUNC_IOMEM_LEN, DRVNAME)) {
		dev_err(dev, "Region 0x%lx-0x%lx already in use!\n",
			data->ebase, data->ebase + EHFUNC_IOMEM_LEN - 1);
		err = -EBUSY;
		goto exit_release_wbase;
	}

	if (!request_region(data->sbase, SP_IOMEM_LEN, DRVNAME)) {
		dev_err(dev, "Region 0x%lx-0x%lx already in use!\n",
			data->sbase, data->sbase + SP_IOMEM_LEN - 1);
		err = -EBUSY;
		goto exit_release_ebase;
	}

	err = request_irq(data->irq, wbcir_irq_handler,
			  0, DRVNAME, device);
	if (err) {
		dev_err(dev, "Failed to claim IRQ %u\n", data->irq);
		err = -EBUSY;
		goto exit_release_sbase;
	}

	device_init_wakeup(&device->dev, 1);

	wbcir_init_hw(data);

	return 0;

exit_release_sbase:
	release_region(data->sbase, SP_IOMEM_LEN);
exit_release_ebase:
	release_region(data->ebase, EHFUNC_IOMEM_LEN);
exit_release_wbase:
	release_region(data->wbase, WAKEUP_IOMEM_LEN);
exit_unregister_device:
	rc_unregister_device(data->dev);
	data->dev = NULL;
exit_free_rc:
	rc_free_device(data->dev);
exit_unregister_led:
	led_classdev_unregister(&data->led);
exit_free_data:
	kfree(data);
	pnp_set_drvdata(device, NULL);
exit:
	return err;
}

static void
wbcir_remove(struct pnp_dev *device)
{
	struct wbcir_data *data = pnp_get_drvdata(device);

	/* Disable interrupts */
	wbcir_set_irqmask(data, WBCIR_IRQ_NONE);
	free_irq(data->irq, device);

	/* Clear status bits NEC_REP, BUFF, MSG_END, MATCH */
	wbcir_set_bits(data->wbase + WBCIR_REG_WCEIR_STS, 0x17, 0x17);

	/* Clear CEIR_EN */
	wbcir_set_bits(data->wbase + WBCIR_REG_WCEIR_CTL, 0x00, 0x01);

	/* Clear BUFF_EN, END_EN, MATCH_EN */
	wbcir_set_bits(data->wbase + WBCIR_REG_WCEIR_EV_EN, 0x00, 0x07);

	rc_unregister_device(data->dev);

	led_classdev_unregister(&data->led);

	/* This is ok since &data->led isn't actually used */
	wbcir_led_brightness_set(&data->led, LED_OFF);

	release_region(data->wbase, WAKEUP_IOMEM_LEN);
	release_region(data->ebase, EHFUNC_IOMEM_LEN);
	release_region(data->sbase, SP_IOMEM_LEN);

	kfree(data);

	pnp_set_drvdata(device, NULL);
}

static const struct pnp_device_id wbcir_ids[] = {
	{ "WEC1022", 0 },
	{ "", 0 }
};
MODULE_DEVICE_TABLE(pnp, wbcir_ids);

static struct pnp_driver wbcir_driver = {
	.name     = WBCIR_NAME,
	.id_table = wbcir_ids,
	.probe    = wbcir_probe,
	.remove   = wbcir_remove,
	.suspend  = wbcir_suspend,
	.resume   = wbcir_resume,
	.shutdown = wbcir_shutdown
};

static int __init
wbcir_init(void)
{
	int ret;

	switch (protocol) {
	case IR_PROTOCOL_RC5:
	case IR_PROTOCOL_NEC:
	case IR_PROTOCOL_RC6:
		break;
	default:
		pr_err("Invalid power-on protocol\n");
	}

	ret = pnp_register_driver(&wbcir_driver);
	if (ret)
		pr_err("Unable to register driver\n");

	return ret;
}

static void __exit
wbcir_exit(void)
{
	pnp_unregister_driver(&wbcir_driver);
}

module_init(wbcir_init);
module_exit(wbcir_exit);

MODULE_AUTHOR("David Härdeman <david@hardeman.nu>");
MODULE_DESCRIPTION("Winbond SuperI/O Consumer IR Driver");
MODULE_LICENSE("GPL");

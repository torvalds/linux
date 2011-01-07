/*
 *  winbond-cir.c - Driver for the Consumer IR functionality of Winbond
 *                  SuperI/O chips.
 *
 *  Currently supports the Winbond WPCD376i chip (PNP id WEC1022), but
 *  could probably support others (Winbond WEC102X, NatSemi, etc)
 *  with minor modifications.
 *
 *  Original Author: David Härdeman <david@hardeman.nu>
 *     Copyright (C) 2009 - 2010 David Härdeman <david@hardeman.nu>
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
 *    o Wake-On-CIR functionality
 *
 *  To do:
 *    o Learning
 *    o IR Transmit
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
/* Over/Under-flow bit for WBCIR_REG_SP3_IER and WBCIR_REG_SP3_EIR */
#define WBCIR_IRQ_ERR		0x04
/* Led enable/disable bit for WBCIR_REG_ECEIR_CTS */
#define WBCIR_LED_ENABLE	0x80
/* RX data available bit for WBCIR_REG_SP3_LSR */
#define WBCIR_RX_AVAIL		0x01
/* RX disable bit for WBCIR_REG_SP3_ASCR */
#define WBCIR_RX_DISABLE	0x20
/* Extended mode enable bit for WBCIR_REG_SP3_EXCR1 */
#define WBCIR_EXT_ENABLE	0x01
/* Select compare register in WBCIR_REG_WCEIR_INDEX (bits 5 & 6) */
#define WBCIR_REGSEL_COMPARE	0x10
/* Select mask register in WBCIR_REG_WCEIR_INDEX (bits 5 & 6) */
#define WBCIR_REGSEL_MASK	0x20
/* Starting address of selected register in WBCIR_REG_WCEIR_INDEX */
#define WBCIR_REG_ADDR0		0x00

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

	unsigned long wbase;        /* Wake-Up Baseaddr		*/
	unsigned long ebase;        /* Enhanced Func. Baseaddr	*/
	unsigned long sbase;        /* Serial Port Baseaddr	*/
	unsigned int  irq;          /* Serial Port IRQ		*/

	struct rc_dev *dev;

	struct led_trigger *rxtrigger;
	struct led_trigger *txtrigger;
	struct led_classdev led;

	/* RX irdata state */
	bool irdata_active;
	bool irdata_error;
	struct ir_raw_event ev;
};

static enum wbcir_protocol protocol = IR_PROTOCOL_RC6;
module_param(protocol, uint, 0444);
MODULE_PARM_DESC(protocol, "IR protocol to use for the power-on command "
		 "(0 = RC5, 1 = NEC, 2 = RC6A, default)");

static int invert; /* default = 0 */
module_param(invert, bool, 0444);
MODULE_PARM_DESC(invert, "Invert the signal from the IR receiver");

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

static irqreturn_t
wbcir_irq_handler(int irqno, void *cookie)
{
	struct pnp_dev *device = cookie;
	struct wbcir_data *data = pnp_get_drvdata(device);
	unsigned long flags;
	u8 irdata[8];
	u8 disable = true;
	u8 status;
	int i;

	spin_lock_irqsave(&data->spinlock, flags);

	wbcir_select_bank(data, WBCIR_BANK_0);

	status = inb(data->sbase + WBCIR_REG_SP3_EIR);

	if (!(status & (WBCIR_IRQ_RX | WBCIR_IRQ_ERR))) {
		spin_unlock_irqrestore(&data->spinlock, flags);
		return IRQ_NONE;
	}

	/* Check for e.g. buffer overflow */
	if (status & WBCIR_IRQ_ERR) {
		data->irdata_error = true;
		ir_raw_event_reset(data->dev);
	}

	if (!(status & WBCIR_IRQ_RX))
		goto out;

	if (!data->irdata_active) {
		data->irdata_active = true;
		led_trigger_event(data->rxtrigger, LED_FULL);
	}

	/* Since RXHDLEV is set, at least 8 bytes are in the FIFO */
	insb(data->sbase + WBCIR_REG_SP3_RXDATA, &irdata[0], 8);

	for (i = 0; i < 8; i++) {
		u8 pulse;
		u32 duration;

		if (irdata[i] != 0xFF && irdata[i] != 0x00)
			disable = false;

		if (data->irdata_error)
			continue;

		pulse = irdata[i] & 0x80 ? false : true;
		duration = (irdata[i] & 0x7F) * 10000; /* ns */

		if (data->ev.pulse != pulse) {
			if (data->ev.duration != 0) {
				ir_raw_event_store(data->dev, &data->ev);
				data->ev.duration = 0;
			}

			data->ev.pulse = pulse;
		}

		data->ev.duration += duration;
	}

	if (disable) {
		if (data->ev.duration != 0 && !data->irdata_error) {
			ir_raw_event_store(data->dev, &data->ev);
			data->ev.duration = 0;
		}

		/* Set RXINACTIVE */
		outb(WBCIR_RX_DISABLE, data->sbase + WBCIR_REG_SP3_ASCR);

		/* Drain the FIFO */
		while (inb(data->sbase + WBCIR_REG_SP3_LSR) & WBCIR_RX_AVAIL)
			inb(data->sbase + WBCIR_REG_SP3_RXDATA);

		ir_raw_event_reset(data->dev);
		data->irdata_error = false;
		data->irdata_active = false;
		led_trigger_event(data->rxtrigger, LED_OFF);
	}

	ir_raw_event_handle(data->dev);

out:
	spin_unlock_irqrestore(&data->spinlock, flags);
	return IRQ_HANDLED;
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
	int do_wake = 1;
	u8 match[11];
	u8 mask[11];
	u8 rc6_csl = 0;
	int i;

	memset(match, 0, sizeof(match));
	memset(mask, 0, sizeof(mask));

	if (wake_sc == INVALID_SCANCODE || !device_may_wakeup(dev)) {
		do_wake = 0;
		goto finish;
	}

	switch (protocol) {
	case IR_PROTOCOL_RC5:
		if (wake_sc > 0xFFF) {
			do_wake = 0;
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
			do_wake = 0;
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
				do_wake = 0;
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
				do_wake = 0;
				dev_err(dev, "RC6 - Invalid wake scancode\n");
				break;
			}

			/* Header */
			match[i]  = 0x93; /* mode1 = mode0 = 1, submode = 0 */
			mask[i++] = 0xFF;
			match[i]  = 0x0A; /* start bit = 1, mode2 = 1 */
			mask[i++] = 0x0F;

		} else {
			do_wake = 0;
			dev_err(dev, "RC6 - Invalid wake mode\n");
		}

		break;

	default:
		do_wake = 0;
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

	/* Disable interrupts */
	wbcir_select_bank(data, WBCIR_BANK_0);
	outb(WBCIR_IRQ_NONE, data->sbase + WBCIR_REG_SP3_IER);

	/* Disable LED */
	data->irdata_active = false;
	led_trigger_event(data->rxtrigger, LED_OFF);

	/*
	 * ACPI will set the HW disable bit for SP3 which means that the
	 * output signals are left in an undefined state which may cause
	 * spurious interrupts which we need to ignore until the hardware
	 * is reinitialized.
	 */
	disable_irq(data->irq);
}

static int
wbcir_suspend(struct pnp_dev *device, pm_message_t state)
{
	wbcir_shutdown(device);
	return 0;
}

static void
wbcir_init_hw(struct wbcir_data *data)
{
	u8 tmp;

	/* Disable interrupts */
	wbcir_select_bank(data, WBCIR_BANK_0);
	outb(WBCIR_IRQ_NONE, data->sbase + WBCIR_REG_SP3_IER);

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
		outb(0x04, data->ebase + WBCIR_REG_ECEIR_CCTL);
	else
		outb(0x00, data->ebase + WBCIR_REG_ECEIR_CCTL);

	/*
	 * Clear IR LED, set SP3 clock to 24Mhz
	 * set SP3_IRRX_SW to binary 01, helpfully not documented
	 */
	outb(0x10, data->ebase + WBCIR_REG_ECEIR_CTS);

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

	/* Set baud divisor to generate one byte per bit/cell */
	switch (protocol) {
	case IR_PROTOCOL_RC5:
		outb(0xA7, data->sbase + WBCIR_REG_SP3_BGDL);
		break;
	case IR_PROTOCOL_RC6:
		outb(0x53, data->sbase + WBCIR_REG_SP3_BGDL);
		break;
	case IR_PROTOCOL_NEC:
		outb(0x69, data->sbase + WBCIR_REG_SP3_BGDL);
		break;
	}
	outb(0x00, data->sbase + WBCIR_REG_SP3_BGDH);

	/* Set CEIR mode */
	wbcir_select_bank(data, WBCIR_BANK_0);
	outb(0xC0, data->sbase + WBCIR_REG_SP3_MCR);
	inb(data->sbase + WBCIR_REG_SP3_LSR); /* Clear LSR */
	inb(data->sbase + WBCIR_REG_SP3_MSR); /* Clear MSR */

	/* Disable RX demod, run-length encoding/decoding, set freq span */
	wbcir_select_bank(data, WBCIR_BANK_7);
	outb(0x10, data->sbase + WBCIR_REG_SP3_RCCFG);

	/* Disable timer */
	wbcir_select_bank(data, WBCIR_BANK_4);
	outb(0x00, data->sbase + WBCIR_REG_SP3_IRCR1);

	/* Enable MSR interrupt, Clear AUX_IRX */
	wbcir_select_bank(data, WBCIR_BANK_5);
	outb(0x00, data->sbase + WBCIR_REG_SP3_IRCR2);

	/* Disable CRC */
	wbcir_select_bank(data, WBCIR_BANK_6);
	outb(0x20, data->sbase + WBCIR_REG_SP3_IRCR3);

	/* Set RX/TX (de)modulation freq, not really used */
	wbcir_select_bank(data, WBCIR_BANK_7);
	outb(0xF2, data->sbase + WBCIR_REG_SP3_IRRXDC);
	outb(0x69, data->sbase + WBCIR_REG_SP3_IRTXMC);

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

	/* Clear IR decoding state */
	data->irdata_active = false;
	led_trigger_event(data->rxtrigger, LED_OFF);
	data->irdata_error = false;
	data->ev.duration = 0;
	ir_raw_event_reset(data->dev);
	ir_raw_event_handle(data->dev);

	/* Enable interrupts */
	outb(WBCIR_IRQ_RX | WBCIR_IRQ_ERR, data->sbase + WBCIR_REG_SP3_IER);
}

static int
wbcir_resume(struct pnp_dev *device)
{
	struct wbcir_data *data = pnp_get_drvdata(device);

	wbcir_init_hw(data);
	enable_irq(data->irq);

	return 0;
}

static int __devinit
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

	if (!request_region(data->wbase, WAKEUP_IOMEM_LEN, DRVNAME)) {
		dev_err(dev, "Region 0x%lx-0x%lx already in use!\n",
			data->wbase, data->wbase + WAKEUP_IOMEM_LEN - 1);
		err = -EBUSY;
		goto exit_free_data;
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
			  IRQF_DISABLED, DRVNAME, device);
	if (err) {
		dev_err(dev, "Failed to claim IRQ %u\n", data->irq);
		err = -EBUSY;
		goto exit_release_sbase;
	}

	led_trigger_register_simple("cir-tx", &data->txtrigger);
	if (!data->txtrigger) {
		err = -ENOMEM;
		goto exit_free_irq;
	}

	led_trigger_register_simple("cir-rx", &data->rxtrigger);
	if (!data->rxtrigger) {
		err = -ENOMEM;
		goto exit_unregister_txtrigger;
	}

	data->led.name = "cir::activity";
	data->led.default_trigger = "cir-rx";
	data->led.brightness_set = wbcir_led_brightness_set;
	data->led.brightness_get = wbcir_led_brightness_get;
	err = led_classdev_register(&device->dev, &data->led);
	if (err)
		goto exit_unregister_rxtrigger;

	data->dev = rc_allocate_device();
	if (!data->dev) {
		err = -ENOMEM;
		goto exit_unregister_led;
	}

	data->dev->driver_name = WBCIR_NAME;
	data->dev->input_name = WBCIR_NAME;
	data->dev->input_phys = "wbcir/cir0";
	data->dev->input_id.bustype = BUS_HOST;
	data->dev->input_id.vendor = PCI_VENDOR_ID_WINBOND;
	data->dev->input_id.product = WBCIR_ID_FAMILY;
	data->dev->input_id.version = WBCIR_ID_CHIP;
	data->dev->priv = data;
	data->dev->dev.parent = &device->dev;

	err = rc_register_device(data->dev);
	if (err)
		goto exit_free_rc;

	device_init_wakeup(&device->dev, 1);

	wbcir_init_hw(data);

	return 0;

exit_free_rc:
	rc_free_device(data->dev);
exit_unregister_led:
	led_classdev_unregister(&data->led);
exit_unregister_rxtrigger:
	led_trigger_unregister_simple(data->rxtrigger);
exit_unregister_txtrigger:
	led_trigger_unregister_simple(data->txtrigger);
exit_free_irq:
	free_irq(data->irq, device);
exit_release_sbase:
	release_region(data->sbase, SP_IOMEM_LEN);
exit_release_ebase:
	release_region(data->ebase, EHFUNC_IOMEM_LEN);
exit_release_wbase:
	release_region(data->wbase, WAKEUP_IOMEM_LEN);
exit_free_data:
	kfree(data);
	pnp_set_drvdata(device, NULL);
exit:
	return err;
}

static void __devexit
wbcir_remove(struct pnp_dev *device)
{
	struct wbcir_data *data = pnp_get_drvdata(device);

	/* Disable interrupts */
	wbcir_select_bank(data, WBCIR_BANK_0);
	outb(WBCIR_IRQ_NONE, data->sbase + WBCIR_REG_SP3_IER);

	free_irq(data->irq, device);

	/* Clear status bits NEC_REP, BUFF, MSG_END, MATCH */
	wbcir_set_bits(data->wbase + WBCIR_REG_WCEIR_STS, 0x17, 0x17);

	/* Clear CEIR_EN */
	wbcir_set_bits(data->wbase + WBCIR_REG_WCEIR_CTL, 0x00, 0x01);

	/* Clear BUFF_EN, END_EN, MATCH_EN */
	wbcir_set_bits(data->wbase + WBCIR_REG_WCEIR_EV_EN, 0x00, 0x07);

	rc_unregister_device(data->dev);

	led_trigger_unregister_simple(data->rxtrigger);
	led_trigger_unregister_simple(data->txtrigger);
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
	.remove   = __devexit_p(wbcir_remove),
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
		printk(KERN_ERR DRVNAME ": Invalid power-on protocol\n");
	}

	ret = pnp_register_driver(&wbcir_driver);
	if (ret)
		printk(KERN_ERR DRVNAME ": Unable to register driver\n");

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

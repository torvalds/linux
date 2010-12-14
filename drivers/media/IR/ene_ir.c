/*
 * driver for ENE KB3926 B/C/D/E/F CIR (pnp id: ENE0XXX)
 *
 * Copyright (C) 2010 Maxim Levitsky <maximlevitsky@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 *
 * Special thanks to:
 *   Sami R. <maesesami@gmail.com> for lot of help in debugging and therefore
 *    bringing to life support for transmission & learning mode.
 *
 *   Charlie Andrews <charliethepilot@googlemail.com> for lots of help in
 *   bringing up the support of new firmware buffer that is popular
 *   on latest notebooks
 *
 *   ENE for partial device documentation
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pnp.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <media/ir-core.h>
#include <media/ir-common.h>
#include "ene_ir.h"

static int sample_period;
static bool learning_mode_force;
static int debug;
static bool txsim;

static void ene_set_reg_addr(struct ene_device *dev, u16 reg)
{
	outb(reg >> 8, dev->hw_io + ENE_ADDR_HI);
	outb(reg & 0xFF, dev->hw_io + ENE_ADDR_LO);
}

/* read a hardware register */
static u8 ene_read_reg(struct ene_device *dev, u16 reg)
{
	u8 retval;
	ene_set_reg_addr(dev, reg);
	retval = inb(dev->hw_io + ENE_IO);
	dbg_regs("reg %04x == %02x", reg, retval);
	return retval;
}

/* write a hardware register */
static void ene_write_reg(struct ene_device *dev, u16 reg, u8 value)
{
	dbg_regs("reg %04x <- %02x", reg, value);
	ene_set_reg_addr(dev, reg);
	outb(value, dev->hw_io + ENE_IO);
}

/* Set bits in hardware register */
static void ene_set_reg_mask(struct ene_device *dev, u16 reg, u8 mask)
{
	dbg_regs("reg %04x |= %02x", reg, mask);
	ene_set_reg_addr(dev, reg);
	outb(inb(dev->hw_io + ENE_IO) | mask, dev->hw_io + ENE_IO);
}

/* Clear bits in hardware register */
static void ene_clear_reg_mask(struct ene_device *dev, u16 reg, u8 mask)
{
	dbg_regs("reg %04x &= ~%02x ", reg, mask);
	ene_set_reg_addr(dev, reg);
	outb(inb(dev->hw_io + ENE_IO) & ~mask, dev->hw_io + ENE_IO);
}

/* A helper to set/clear a bit in register according to boolean variable */
static void ene_set_clear_reg_mask(struct ene_device *dev, u16 reg, u8 mask,
								bool set)
{
	if (set)
		ene_set_reg_mask(dev, reg, mask);
	else
		ene_clear_reg_mask(dev, reg, mask);
}

/* detect hardware features */
static int ene_hw_detect(struct ene_device *dev)
{
	u8 chip_major, chip_minor;
	u8 hw_revision, old_ver;
	u8 fw_reg2, fw_reg1;

	ene_clear_reg_mask(dev, ENE_ECSTS, ENE_ECSTS_RSRVD);
	chip_major = ene_read_reg(dev, ENE_ECVER_MAJOR);
	chip_minor = ene_read_reg(dev, ENE_ECVER_MINOR);
	ene_set_reg_mask(dev, ENE_ECSTS, ENE_ECSTS_RSRVD);

	hw_revision = ene_read_reg(dev, ENE_ECHV);
	old_ver = ene_read_reg(dev, ENE_HW_VER_OLD);

	dev->pll_freq = (ene_read_reg(dev, ENE_PLLFRH) << 4) +
		(ene_read_reg(dev, ENE_PLLFRL) >> 4);

	if (sample_period != ENE_DEFAULT_SAMPLE_PERIOD)
		dev->rx_period_adjust =
			dev->pll_freq == ENE_DEFAULT_PLL_FREQ ? 2 : 4;

	if (hw_revision == 0xFF) {
		ene_warn("device seems to be disabled");
		ene_warn("send a mail to lirc-list@lists.sourceforge.net");
		ene_warn("please attach output of acpidump and dmidecode");
		return -ENODEV;
	}

	ene_notice("chip is 0x%02x%02x - kbver = 0x%02x, rev = 0x%02x",
		chip_major, chip_minor, old_ver, hw_revision);

	ene_notice("PLL freq = %d", dev->pll_freq);

	if (chip_major == 0x33) {
		ene_warn("chips 0x33xx aren't supported");
		return -ENODEV;
	}

	if (chip_major == 0x39 && chip_minor == 0x26 && hw_revision == 0xC0) {
		dev->hw_revision = ENE_HW_C;
		ene_notice("KB3926C detected");
	} else if (old_ver == 0x24 && hw_revision == 0xC0) {
		dev->hw_revision = ENE_HW_B;
		ene_notice("KB3926B detected");
	} else {
		dev->hw_revision = ENE_HW_D;
		ene_notice("KB3926D or higher detected");
	}

	/* detect features hardware supports */
	if (dev->hw_revision < ENE_HW_C)
		return 0;

	fw_reg1 = ene_read_reg(dev, ENE_FW1);
	fw_reg2 = ene_read_reg(dev, ENE_FW2);

	ene_notice("Firmware regs: %02x %02x", fw_reg1, fw_reg2);

	dev->hw_use_gpio_0a = !!(fw_reg2 & ENE_FW2_GP0A);
	dev->hw_learning_and_tx_capable = !!(fw_reg2 & ENE_FW2_LEARNING);
	dev->hw_extra_buffer = !!(fw_reg1 & ENE_FW1_HAS_EXTRA_BUF);

	if (dev->hw_learning_and_tx_capable)
		dev->hw_fan_input = !!(fw_reg2 & ENE_FW2_FAN_INPUT);

	ene_notice("Hardware features:");

	if (dev->hw_learning_and_tx_capable) {
		ene_notice("* Supports transmitting & learning mode");
		ene_notice("   This feature is rare and therefore,");
		ene_notice("   you are welcome to test it,");
		ene_notice("   and/or contact the author via:");
		ene_notice("   lirc-list@lists.sourceforge.net");
		ene_notice("   or maximlevitsky@gmail.com");

		ene_notice("* Uses GPIO %s for IR raw input",
			dev->hw_use_gpio_0a ? "40" : "0A");

		if (dev->hw_fan_input)
			ene_notice("* Uses unused fan feedback input as source"
					" of demodulated IR data");
	}

	if (!dev->hw_fan_input)
		ene_notice("* Uses GPIO %s for IR demodulated input",
			dev->hw_use_gpio_0a ? "0A" : "40");

	if (dev->hw_extra_buffer)
		ene_notice("* Uses new style input buffer");
	return 0;
}

/* Read properities of hw sample buffer */
static void ene_rx_setup_hw_buffer(struct ene_device *dev)
{
	u16 tmp;

	ene_rx_read_hw_pointer(dev);
	dev->r_pointer = dev->w_pointer;

	if (!dev->hw_extra_buffer) {
		dev->buffer_len = ENE_FW_PACKET_SIZE * 2;
		return;
	}

	tmp = ene_read_reg(dev, ENE_FW_SAMPLE_BUFFER);
	tmp |= ene_read_reg(dev, ENE_FW_SAMPLE_BUFFER+1) << 8;
	dev->extra_buf1_address = tmp;

	dev->extra_buf1_len = ene_read_reg(dev, ENE_FW_SAMPLE_BUFFER + 2);

	tmp = ene_read_reg(dev, ENE_FW_SAMPLE_BUFFER + 3);
	tmp |= ene_read_reg(dev, ENE_FW_SAMPLE_BUFFER + 4) << 8;
	dev->extra_buf2_address = tmp;

	dev->extra_buf2_len = ene_read_reg(dev, ENE_FW_SAMPLE_BUFFER + 5);

	dev->buffer_len = dev->extra_buf1_len + dev->extra_buf2_len + 8;

	ene_notice("Hardware uses 2 extended buffers:");
	ene_notice("  0x%04x - len : %d", dev->extra_buf1_address,
						dev->extra_buf1_len);
	ene_notice("  0x%04x - len : %d", dev->extra_buf2_address,
						dev->extra_buf2_len);

	ene_notice("Total buffer len = %d", dev->buffer_len);

	if (dev->buffer_len > 64 || dev->buffer_len < 16)
		goto error;

	if (dev->extra_buf1_address > 0xFBFC ||
					dev->extra_buf1_address < 0xEC00)
		goto error;

	if (dev->extra_buf2_address > 0xFBFC ||
					dev->extra_buf2_address < 0xEC00)
		goto error;

	if (dev->r_pointer > dev->buffer_len)
		goto error;

	ene_set_reg_mask(dev, ENE_FW1, ENE_FW1_EXTRA_BUF_HND);
	return;
error:
	ene_warn("Error validating extra buffers, device probably won't work");
	dev->hw_extra_buffer = false;
	ene_clear_reg_mask(dev, ENE_FW1, ENE_FW1_EXTRA_BUF_HND);
}


/* Restore the pointers to extra buffers - to make module reload work*/
static void ene_rx_restore_hw_buffer(struct ene_device *dev)
{
	if (!dev->hw_extra_buffer)
		return;

	ene_write_reg(dev, ENE_FW_SAMPLE_BUFFER + 0,
				dev->extra_buf1_address & 0xFF);
	ene_write_reg(dev, ENE_FW_SAMPLE_BUFFER + 1,
				dev->extra_buf1_address >> 8);
	ene_write_reg(dev, ENE_FW_SAMPLE_BUFFER + 2, dev->extra_buf1_len);

	ene_write_reg(dev, ENE_FW_SAMPLE_BUFFER + 3,
				dev->extra_buf2_address & 0xFF);
	ene_write_reg(dev, ENE_FW_SAMPLE_BUFFER + 4,
				dev->extra_buf2_address >> 8);
	ene_write_reg(dev, ENE_FW_SAMPLE_BUFFER + 5,
				dev->extra_buf2_len);
	ene_clear_reg_mask(dev, ENE_FW1, ENE_FW1_EXTRA_BUF_HND);
}

/* Read hardware write pointer */
static void ene_rx_read_hw_pointer(struct ene_device *dev)
{
	if (dev->hw_extra_buffer)
		dev->w_pointer = ene_read_reg(dev, ENE_FW_RX_POINTER);
	else
		dev->w_pointer = ene_read_reg(dev, ENE_FW2)
			& ENE_FW2_BUF_WPTR ? 0 : ENE_FW_PACKET_SIZE;

	dbg_verbose("RB: HW write pointer: %02x, driver read pointer: %02x",
		dev->w_pointer, dev->r_pointer);
}

/* Gets address of next sample from HW ring buffer */
static int ene_rx_get_sample_reg(struct ene_device *dev)
{
	int r_pointer;

	if (dev->r_pointer == dev->w_pointer) {
		dbg_verbose("RB: hit end, try update w_pointer");
		ene_rx_read_hw_pointer(dev);
	}

	if (dev->r_pointer == dev->w_pointer) {
		dbg_verbose("RB: end of data at %d", dev->r_pointer);
		return 0;
	}

	dbg_verbose("RB: reading at offset %d", dev->r_pointer);
	r_pointer = dev->r_pointer;

	dev->r_pointer++;
	if (dev->r_pointer == dev->buffer_len)
		dev->r_pointer = 0;

	dbg_verbose("RB: next read will be from offset %d", dev->r_pointer);

	if (r_pointer < 8) {
		dbg_verbose("RB: read at main buffer at %d", r_pointer);
		return ENE_FW_SAMPLE_BUFFER + r_pointer;
	}

	r_pointer -= 8;

	if (r_pointer < dev->extra_buf1_len) {
		dbg_verbose("RB: read at 1st extra buffer at %d", r_pointer);
		return dev->extra_buf1_address + r_pointer;
	}

	r_pointer -= dev->extra_buf1_len;

	if (r_pointer < dev->extra_buf2_len) {
		dbg_verbose("RB: read at 2nd extra buffer at %d", r_pointer);
		return dev->extra_buf2_address + r_pointer;
	}

	dbg("attempt to read beyong ring bufer end");
	return 0;
}

/* Sense current received carrier */
void ene_rx_sense_carrier(struct ene_device *dev)
{
	DEFINE_IR_RAW_EVENT(ev);

	int carrier, duty_cycle;
	int period = ene_read_reg(dev, ENE_CIRCAR_PRD);
	int hperiod = ene_read_reg(dev, ENE_CIRCAR_HPRD);

	if (!(period & ENE_CIRCAR_PRD_VALID))
		return;

	period &= ~ENE_CIRCAR_PRD_VALID;

	if (!period)
		return;

	dbg("RX: hardware carrier period = %02x", period);
	dbg("RX: hardware carrier pulse period = %02x", hperiod);

	carrier = 2000000 / period;
	duty_cycle = (hperiod * 100) / period;
	dbg("RX: sensed carrier = %d Hz, duty cycle %d%%",
						carrier, duty_cycle);
	if (dev->carrier_detect_enabled) {
		ev.carrier_report = true;
		ev.carrier = carrier;
		ev.duty_cycle = duty_cycle;
		ir_raw_event_store(dev->idev, &ev);
	}
}

/* this enables/disables the CIR RX engine */
static void ene_rx_enable_cir_engine(struct ene_device *dev, bool enable)
{
	ene_set_clear_reg_mask(dev, ENE_CIRCFG,
			ENE_CIRCFG_RX_EN | ENE_CIRCFG_RX_IRQ, enable);
}

/* this selects input for CIR engine. Ether GPIO 0A or GPIO40*/
static void ene_rx_select_input(struct ene_device *dev, bool gpio_0a)
{
	ene_set_clear_reg_mask(dev, ENE_CIRCFG2, ENE_CIRCFG2_GPIO0A, gpio_0a);
}

/*
 * this enables alternative input via fan tachometer sensor and bypasses
 * the hw CIR engine
 */
static void ene_rx_enable_fan_input(struct ene_device *dev, bool enable)
{
	if (!dev->hw_fan_input)
		return;

	if (!enable)
		ene_write_reg(dev, ENE_FAN_AS_IN1, 0);
	else {
		ene_write_reg(dev, ENE_FAN_AS_IN1, ENE_FAN_AS_IN1_EN);
		ene_write_reg(dev, ENE_FAN_AS_IN2, ENE_FAN_AS_IN2_EN);
	}
}

/* setup the receiver for RX*/
static void ene_rx_setup(struct ene_device *dev)
{
	bool learning_mode = dev->learning_mode_enabled ||
					dev->carrier_detect_enabled;
	int sample_period_adjust = 0;

	dbg("RX: setup receiver, learning mode = %d", learning_mode);


	/* This selects RLC input and clears CFG2 settings */
	ene_write_reg(dev, ENE_CIRCFG2, 0x00);

	/* set sample period*/
	if (sample_period == ENE_DEFAULT_SAMPLE_PERIOD)
		sample_period_adjust =
			dev->pll_freq == ENE_DEFAULT_PLL_FREQ ? 1 : 2;

	ene_write_reg(dev, ENE_CIRRLC_CFG,
			(sample_period + sample_period_adjust) |
						ENE_CIRRLC_CFG_OVERFLOW);
	/* revB doesn't support inputs */
	if (dev->hw_revision < ENE_HW_C)
		goto select_timeout;

	if (learning_mode) {

		WARN_ON(!dev->hw_learning_and_tx_capable);

		/* Enable the opposite of the normal input
		That means that if GPIO40 is normally used, use GPIO0A
		and vice versa.
		This input will carry non demodulated
		signal, and we will tell the hw to demodulate it itself */
		ene_rx_select_input(dev, !dev->hw_use_gpio_0a);
		dev->rx_fan_input_inuse = false;

		/* Enable carrier demodulation */
		ene_set_reg_mask(dev, ENE_CIRCFG, ENE_CIRCFG_CARR_DEMOD);

		/* Enable carrier detection */
		ene_write_reg(dev, ENE_CIRCAR_PULS, 0x63);
		ene_set_clear_reg_mask(dev, ENE_CIRCFG2, ENE_CIRCFG2_CARR_DETECT,
			dev->carrier_detect_enabled || debug);
	} else {
		if (dev->hw_fan_input)
			dev->rx_fan_input_inuse = true;
		else
			ene_rx_select_input(dev, dev->hw_use_gpio_0a);

		/* Disable carrier detection & demodulation */
		ene_clear_reg_mask(dev, ENE_CIRCFG, ENE_CIRCFG_CARR_DEMOD);
		ene_clear_reg_mask(dev, ENE_CIRCFG2, ENE_CIRCFG2_CARR_DETECT);
	}

select_timeout:
	if (dev->rx_fan_input_inuse) {
		dev->props->rx_resolution = MS_TO_NS(ENE_FW_SAMPLE_PERIOD_FAN);

		/* Fan input doesn't support timeouts, it just ends the
			input with a maximum sample */
		dev->props->min_timeout = dev->props->max_timeout =
			MS_TO_NS(ENE_FW_SMPL_BUF_FAN_MSK *
				ENE_FW_SAMPLE_PERIOD_FAN);
	} else {
		dev->props->rx_resolution = MS_TO_NS(sample_period);

		/* Theoreticly timeout is unlimited, but we cap it
		 * because it was seen that on one device, it
		 * would stop sending spaces after around 250 msec.
		 * Besides, this is close to 2^32 anyway and timeout is u32.
		 */
		dev->props->min_timeout = MS_TO_NS(127 * sample_period);
		dev->props->max_timeout = MS_TO_NS(200000);
	}

	if (dev->hw_learning_and_tx_capable)
		dev->props->tx_resolution = MS_TO_NS(sample_period);

	if (dev->props->timeout > dev->props->max_timeout)
		dev->props->timeout = dev->props->max_timeout;
	if (dev->props->timeout < dev->props->min_timeout)
		dev->props->timeout = dev->props->min_timeout;
}

/* Enable the device for receive */
static void ene_rx_enable(struct ene_device *dev)
{
	u8 reg_value;

	/* Enable system interrupt */
	if (dev->hw_revision < ENE_HW_C) {
		ene_write_reg(dev, ENEB_IRQ, dev->irq << 1);
		ene_write_reg(dev, ENEB_IRQ_UNK1, 0x01);
	} else {
		reg_value = ene_read_reg(dev, ENE_IRQ) & 0xF0;
		reg_value |= ENE_IRQ_UNK_EN;
		reg_value &= ~ENE_IRQ_STATUS;
		reg_value |= (dev->irq & ENE_IRQ_MASK);
		ene_write_reg(dev, ENE_IRQ, reg_value);
	}

	/* Enable inputs */
	ene_rx_enable_fan_input(dev, dev->rx_fan_input_inuse);
	ene_rx_enable_cir_engine(dev, !dev->rx_fan_input_inuse);

	/* ack any pending irqs - just in case */
	ene_irq_status(dev);

	/* enable firmware bits */
	ene_set_reg_mask(dev, ENE_FW1, ENE_FW1_ENABLE | ENE_FW1_IRQ);

	/* enter idle mode */
	ir_raw_event_set_idle(dev->idev, true);
	dev->rx_enabled = true;
}

/* Disable the device receiver */
static void ene_rx_disable(struct ene_device *dev)
{
	/* disable inputs */
	ene_rx_enable_cir_engine(dev, false);
	ene_rx_enable_fan_input(dev, false);

	/* disable hardware IRQ and firmware flag */
	ene_clear_reg_mask(dev, ENE_FW1, ENE_FW1_ENABLE | ENE_FW1_IRQ);

	ir_raw_event_set_idle(dev->idev, true);
	dev->rx_enabled = false;
}

/* This resets the receiver. Usefull to stop stream of spaces at end of
 * transmission
 */
static void ene_rx_reset(struct ene_device *dev)
{
	ene_clear_reg_mask(dev, ENE_CIRCFG, ENE_CIRCFG_RX_EN);
	ene_set_reg_mask(dev, ENE_CIRCFG, ENE_CIRCFG_RX_EN);
}

/* Set up the TX carrier frequency and duty cycle */
static void ene_tx_set_carrier(struct ene_device *dev)
{
	u8 tx_puls_width;
	unsigned long flags;

	spin_lock_irqsave(&dev->hw_lock, flags);

	ene_set_clear_reg_mask(dev, ENE_CIRCFG,
		ENE_CIRCFG_TX_CARR, dev->tx_period > 0);

	if (!dev->tx_period)
		goto unlock;

	BUG_ON(dev->tx_duty_cycle >= 100 || dev->tx_duty_cycle <= 0);

	tx_puls_width = dev->tx_period / (100 / dev->tx_duty_cycle);

	if (!tx_puls_width)
		tx_puls_width = 1;

	dbg("TX: pulse distance = %d * 500 ns", dev->tx_period);
	dbg("TX: pulse width = %d * 500 ns", tx_puls_width);

	ene_write_reg(dev, ENE_CIRMOD_PRD, dev->tx_period | ENE_CIRMOD_PRD_POL);
	ene_write_reg(dev, ENE_CIRMOD_HPRD, tx_puls_width);
unlock:
	spin_unlock_irqrestore(&dev->hw_lock, flags);
}

/* Enable/disable transmitters */
static void ene_tx_set_transmitters(struct ene_device *dev)
{
	unsigned long flags;

	spin_lock_irqsave(&dev->hw_lock, flags);
	ene_set_clear_reg_mask(dev, ENE_GPIOFS8, ENE_GPIOFS8_GPIO41,
					!!(dev->transmitter_mask & 0x01));
	ene_set_clear_reg_mask(dev, ENE_GPIOFS1, ENE_GPIOFS1_GPIO0D,
					!!(dev->transmitter_mask & 0x02));
	spin_unlock_irqrestore(&dev->hw_lock, flags);
}

/* prepare transmission */
static void ene_tx_enable(struct ene_device *dev)
{
	u8 conf1 = ene_read_reg(dev, ENE_CIRCFG);
	u8 fwreg2 = ene_read_reg(dev, ENE_FW2);

	dev->saved_conf1 = conf1;

	/* Show information about currently connected transmitter jacks */
	if (fwreg2 & ENE_FW2_EMMITER1_CONN)
		dbg("TX: Transmitter #1 is connected");

	if (fwreg2 & ENE_FW2_EMMITER2_CONN)
		dbg("TX: Transmitter #2 is connected");

	if (!(fwreg2 & (ENE_FW2_EMMITER1_CONN | ENE_FW2_EMMITER2_CONN)))
		ene_warn("TX: transmitter cable isn't connected!");

	/* disable receive on revc */
	if (dev->hw_revision == ENE_HW_C)
		conf1 &= ~ENE_CIRCFG_RX_EN;

	/* Enable TX engine */
	conf1 |= ENE_CIRCFG_TX_EN | ENE_CIRCFG_TX_IRQ;
	ene_write_reg(dev, ENE_CIRCFG, conf1);
}

/* end transmission */
static void ene_tx_disable(struct ene_device *dev)
{
	ene_write_reg(dev, ENE_CIRCFG, dev->saved_conf1);
	dev->tx_buffer = NULL;
}


/* TX one sample - must be called with dev->hw_lock*/
static void ene_tx_sample(struct ene_device *dev)
{
	u8 raw_tx;
	u32 sample;
	bool pulse = dev->tx_sample_pulse;

	if (!dev->tx_buffer) {
		ene_warn("TX: BUG: attempt to transmit NULL buffer");
		return;
	}

	/* Grab next TX sample */
	if (!dev->tx_sample) {

		if (dev->tx_pos == dev->tx_len) {
			if (!dev->tx_done) {
				dbg("TX: no more data to send");
				dev->tx_done = true;
				goto exit;
			} else {
				dbg("TX: last sample sent by hardware");
				ene_tx_disable(dev);
				complete(&dev->tx_complete);
				return;
			}
		}

		sample = dev->tx_buffer[dev->tx_pos++];
		dev->tx_sample_pulse = !dev->tx_sample_pulse;

		dev->tx_sample = DIV_ROUND_CLOSEST(sample, sample_period);

		if (!dev->tx_sample)
			dev->tx_sample = 1;
	}

	raw_tx = min(dev->tx_sample , (unsigned int)ENE_CIRRLC_OUT_MASK);
	dev->tx_sample -= raw_tx;

	dbg("TX: sample %8d (%s)", raw_tx * sample_period,
						pulse ? "pulse" : "space");
	if (pulse)
		raw_tx |= ENE_CIRRLC_OUT_PULSE;

	ene_write_reg(dev,
		dev->tx_reg ? ENE_CIRRLC_OUT1 : ENE_CIRRLC_OUT0, raw_tx);

	dev->tx_reg = !dev->tx_reg;
exit:
	/* simulate TX done interrupt */
	if (txsim)
		mod_timer(&dev->tx_sim_timer, jiffies + HZ / 500);
}

/* timer to simulate tx done interrupt */
static void ene_tx_irqsim(unsigned long data)
{
	struct ene_device *dev = (struct ene_device *)data;
	unsigned long flags;

	spin_lock_irqsave(&dev->hw_lock, flags);
	ene_tx_sample(dev);
	spin_unlock_irqrestore(&dev->hw_lock, flags);
}


/* read irq status and ack it */
static int ene_irq_status(struct ene_device *dev)
{
	u8 irq_status;
	u8 fw_flags1, fw_flags2;
	int retval = 0;

	fw_flags2 = ene_read_reg(dev, ENE_FW2);

	if (dev->hw_revision < ENE_HW_C) {
		irq_status = ene_read_reg(dev, ENEB_IRQ_STATUS);

		if (!(irq_status & ENEB_IRQ_STATUS_IR))
			return 0;

		ene_clear_reg_mask(dev, ENEB_IRQ_STATUS, ENEB_IRQ_STATUS_IR);
		return ENE_IRQ_RX;
	}

	irq_status = ene_read_reg(dev, ENE_IRQ);
	if (!(irq_status & ENE_IRQ_STATUS))
		return 0;

	/* original driver does that twice - a workaround ? */
	ene_write_reg(dev, ENE_IRQ, irq_status & ~ENE_IRQ_STATUS);
	ene_write_reg(dev, ENE_IRQ, irq_status & ~ENE_IRQ_STATUS);

	/* check RX interrupt */
	if (fw_flags2 & ENE_FW2_RXIRQ) {
		retval |= ENE_IRQ_RX;
		ene_write_reg(dev, ENE_FW2, fw_flags2 & ~ENE_FW2_RXIRQ);
	}

	/* check TX interrupt */
	fw_flags1 = ene_read_reg(dev, ENE_FW1);
	if (fw_flags1 & ENE_FW1_TXIRQ) {
		ene_write_reg(dev, ENE_FW1, fw_flags1 & ~ENE_FW1_TXIRQ);
		retval |= ENE_IRQ_TX;
	}

	return retval;
}

/* interrupt handler */
static irqreturn_t ene_isr(int irq, void *data)
{
	u16 hw_value, reg;
	int hw_sample, irq_status;
	bool pulse;
	unsigned long flags;
	irqreturn_t retval = IRQ_NONE;
	struct ene_device *dev = (struct ene_device *)data;
	DEFINE_IR_RAW_EVENT(ev);

	spin_lock_irqsave(&dev->hw_lock, flags);

	dbg_verbose("ISR called");
	ene_rx_read_hw_pointer(dev);
	irq_status = ene_irq_status(dev);

	if (!irq_status)
		goto unlock;

	retval = IRQ_HANDLED;

	if (irq_status & ENE_IRQ_TX) {
		dbg_verbose("TX interrupt");
		if (!dev->hw_learning_and_tx_capable) {
			dbg("TX interrupt on unsupported device!");
			goto unlock;
		}
		ene_tx_sample(dev);
	}

	if (!(irq_status & ENE_IRQ_RX))
		goto unlock;

	dbg_verbose("RX interrupt");

	if (dev->hw_learning_and_tx_capable)
		ene_rx_sense_carrier(dev);

	/* On hardware that don't support extra buffer we need to trust
		the interrupt and not track the read pointer */
	if (!dev->hw_extra_buffer)
		dev->r_pointer = dev->w_pointer == 0 ? ENE_FW_PACKET_SIZE : 0;

	while (1) {

		reg = ene_rx_get_sample_reg(dev);

		dbg_verbose("next sample to read at: %04x", reg);
		if (!reg)
			break;

		hw_value = ene_read_reg(dev, reg);

		if (dev->rx_fan_input_inuse) {

			int offset = ENE_FW_SMPL_BUF_FAN - ENE_FW_SAMPLE_BUFFER;

			/* read high part of the sample */
			hw_value |= ene_read_reg(dev, reg + offset) << 8;
			pulse = hw_value & ENE_FW_SMPL_BUF_FAN_PLS;

			/* clear space bit, and other unused bits */
			hw_value &= ENE_FW_SMPL_BUF_FAN_MSK;
			hw_sample = hw_value * ENE_FW_SAMPLE_PERIOD_FAN;

		} else {
			pulse = !(hw_value & ENE_FW_SAMPLE_SPACE);
			hw_value &= ~ENE_FW_SAMPLE_SPACE;
			hw_sample = hw_value * sample_period;

			if (dev->rx_period_adjust) {
				hw_sample *= 100;
				hw_sample /= (100 + dev->rx_period_adjust);
			}
		}

		if (!dev->hw_extra_buffer && !hw_sample) {
			dev->r_pointer = dev->w_pointer;
			continue;
		}

		dbg("RX: %d (%s)", hw_sample, pulse ? "pulse" : "space");

		ev.duration = MS_TO_NS(hw_sample);
		ev.pulse = pulse;
		ir_raw_event_store_with_filter(dev->idev, &ev);
	}

	ir_raw_event_handle(dev->idev);
unlock:
	spin_unlock_irqrestore(&dev->hw_lock, flags);
	return retval;
}

/* Initialize default settings */
static void ene_setup_default_settings(struct ene_device *dev)
{
	dev->tx_period = 32;
	dev->tx_duty_cycle = 50; /*%*/
	dev->transmitter_mask = 0x03;
	dev->learning_mode_enabled = learning_mode_force;

	/* Set reasonable default timeout */
	dev->props->timeout = MS_TO_NS(150000);
}

/* Upload all hardware settings at once. Used at load and resume time */
static void ene_setup_hw_settings(struct ene_device *dev)
{
	if (dev->hw_learning_and_tx_capable) {
		ene_tx_set_carrier(dev);
		ene_tx_set_transmitters(dev);
	}

	ene_rx_setup(dev);
}

/* outside interface: called on first open*/
static int ene_open(void *data)
{
	struct ene_device *dev = (struct ene_device *)data;
	unsigned long flags;

	spin_lock_irqsave(&dev->hw_lock, flags);
	ene_rx_enable(dev);
	spin_unlock_irqrestore(&dev->hw_lock, flags);
	return 0;
}

/* outside interface: called on device close*/
static void ene_close(void *data)
{
	struct ene_device *dev = (struct ene_device *)data;
	unsigned long flags;
	spin_lock_irqsave(&dev->hw_lock, flags);

	ene_rx_disable(dev);
	spin_unlock_irqrestore(&dev->hw_lock, flags);
}

/* outside interface: set transmitter mask */
static int ene_set_tx_mask(void *data, u32 tx_mask)
{
	struct ene_device *dev = (struct ene_device *)data;
	dbg("TX: attempt to set transmitter mask %02x", tx_mask);

	/* invalid txmask */
	if (!tx_mask || tx_mask & ~0x03) {
		dbg("TX: invalid mask");
		/* return count of transmitters */
		return 2;
	}

	dev->transmitter_mask = tx_mask;
	ene_tx_set_transmitters(dev);
	return 0;
}

/* outside interface : set tx carrier */
static int ene_set_tx_carrier(void *data, u32 carrier)
{
	struct ene_device *dev = (struct ene_device *)data;
	u32 period = 2000000 / carrier;

	dbg("TX: attempt to set tx carrier to %d kHz", carrier);

	if (period && (period > ENE_CIRMOD_PRD_MAX ||
			period < ENE_CIRMOD_PRD_MIN)) {

		dbg("TX: out of range %d-%d kHz carrier",
			2000 / ENE_CIRMOD_PRD_MIN, 2000 / ENE_CIRMOD_PRD_MAX);
		return -1;
	}

	dev->tx_period = period;
	ene_tx_set_carrier(dev);
	return 0;
}

/*outside interface : set tx duty cycle */
static int ene_set_tx_duty_cycle(void *data, u32 duty_cycle)
{
	struct ene_device *dev = (struct ene_device *)data;
	dbg("TX: setting duty cycle to %d%%", duty_cycle);
	dev->tx_duty_cycle = duty_cycle;
	ene_tx_set_carrier(dev);
	return 0;
}

/* outside interface: enable learning mode */
static int ene_set_learning_mode(void *data, int enable)
{
	struct ene_device *dev = (struct ene_device *)data;
	unsigned long flags;
	if (enable == dev->learning_mode_enabled)
		return 0;

	spin_lock_irqsave(&dev->hw_lock, flags);
	dev->learning_mode_enabled = enable;
	ene_rx_disable(dev);
	ene_rx_setup(dev);
	ene_rx_enable(dev);
	spin_unlock_irqrestore(&dev->hw_lock, flags);
	return 0;
}

static int ene_set_carrier_report(void *data, int enable)
{
	struct ene_device *dev = (struct ene_device *)data;
	unsigned long flags;

	if (enable == dev->carrier_detect_enabled)
		return 0;

	spin_lock_irqsave(&dev->hw_lock, flags);
	dev->carrier_detect_enabled = enable;
	ene_rx_disable(dev);
	ene_rx_setup(dev);
	ene_rx_enable(dev);
	spin_unlock_irqrestore(&dev->hw_lock, flags);
	return 0;
}

/* outside interface: enable or disable idle mode */
static void ene_set_idle(void *data, bool idle)
{
	if (idle) {
		ene_rx_reset((struct ene_device *)data);
		dbg("RX: end of data");
	}
}

/* outside interface: transmit */
static int ene_transmit(void *data, int *buf, u32 n)
{
	struct ene_device *dev = (struct ene_device *)data;
	unsigned long flags;

	dev->tx_buffer = buf;
	dev->tx_len = n / sizeof(int);
	dev->tx_pos = 0;
	dev->tx_reg = 0;
	dev->tx_done = 0;
	dev->tx_sample = 0;
	dev->tx_sample_pulse = 0;

	dbg("TX: %d samples", dev->tx_len);

	spin_lock_irqsave(&dev->hw_lock, flags);

	ene_tx_enable(dev);

	/* Transmit first two samples */
	ene_tx_sample(dev);
	ene_tx_sample(dev);

	spin_unlock_irqrestore(&dev->hw_lock, flags);

	if (wait_for_completion_timeout(&dev->tx_complete, 2 * HZ) == 0) {
		dbg("TX: timeout");
		spin_lock_irqsave(&dev->hw_lock, flags);
		ene_tx_disable(dev);
		spin_unlock_irqrestore(&dev->hw_lock, flags);
	} else
		dbg("TX: done");
	return n;
}

/* probe entry */
static int ene_probe(struct pnp_dev *pnp_dev, const struct pnp_device_id *id)
{
	int error = -ENOMEM;
	struct ir_dev_props *ir_props;
	struct input_dev *input_dev;
	struct ene_device *dev;

	/* allocate memory */
	input_dev = input_allocate_device();
	ir_props = kzalloc(sizeof(struct ir_dev_props), GFP_KERNEL);
	dev = kzalloc(sizeof(struct ene_device), GFP_KERNEL);

	if (!input_dev || !ir_props || !dev)
		goto error1;

	/* validate resources */
	error = -ENODEV;

	if (!pnp_port_valid(pnp_dev, 0) ||
	    pnp_port_len(pnp_dev, 0) < ENE_IO_SIZE)
		goto error;

	if (!pnp_irq_valid(pnp_dev, 0))
		goto error;

	spin_lock_init(&dev->hw_lock);

	/* claim the resources */
	error = -EBUSY;
	dev->hw_io = pnp_port_start(pnp_dev, 0);
	if (!request_region(dev->hw_io, ENE_IO_SIZE, ENE_DRIVER_NAME)) {
		dev->hw_io = -1;
		dev->irq = -1;
		goto error;
	}

	dev->irq = pnp_irq(pnp_dev, 0);
	if (request_irq(dev->irq, ene_isr,
			IRQF_SHARED, ENE_DRIVER_NAME, (void *)dev)) {
		dev->irq = -1;
		goto error;
	}

	pnp_set_drvdata(pnp_dev, dev);
	dev->pnp_dev = pnp_dev;

	/* don't allow too short/long sample periods */
	if (sample_period < 5 || sample_period > 0x7F)
		sample_period = ENE_DEFAULT_SAMPLE_PERIOD;

	/* detect hardware version and features */
	error = ene_hw_detect(dev);
	if (error)
		goto error;

	if (!dev->hw_learning_and_tx_capable && txsim) {
		dev->hw_learning_and_tx_capable = true;
		setup_timer(&dev->tx_sim_timer, ene_tx_irqsim,
						(long unsigned int)dev);
		ene_warn("Simulation of TX activated");
	}

	if (!dev->hw_learning_and_tx_capable)
		learning_mode_force = false;

	ir_props->driver_type = RC_DRIVER_IR_RAW;
	ir_props->allowed_protos = IR_TYPE_ALL;
	ir_props->priv = dev;
	ir_props->open = ene_open;
	ir_props->close = ene_close;
	ir_props->s_idle = ene_set_idle;

	dev->props = ir_props;
	dev->idev = input_dev;

	if (dev->hw_learning_and_tx_capable) {
		ir_props->s_learning_mode = ene_set_learning_mode;
		init_completion(&dev->tx_complete);
		ir_props->tx_ir = ene_transmit;
		ir_props->s_tx_mask = ene_set_tx_mask;
		ir_props->s_tx_carrier = ene_set_tx_carrier;
		ir_props->s_tx_duty_cycle = ene_set_tx_duty_cycle;
		ir_props->s_carrier_report = ene_set_carrier_report;
	}

	ene_rx_setup_hw_buffer(dev);
	ene_setup_default_settings(dev);
	ene_setup_hw_settings(dev);

	device_set_wakeup_capable(&pnp_dev->dev, true);
	device_set_wakeup_enable(&pnp_dev->dev, true);

	if (dev->hw_learning_and_tx_capable)
		input_dev->name = "ENE eHome Infrared Remote Transceiver";
	else
		input_dev->name = "ENE eHome Infrared Remote Receiver";

	error = -ENODEV;
	if (ir_input_register(input_dev, RC_MAP_RC6_MCE, ir_props,
							ENE_DRIVER_NAME))
		goto error;

	ene_notice("driver has been succesfully loaded");
	return 0;
error:
	if (dev && dev->irq >= 0)
		free_irq(dev->irq, dev);
	if (dev && dev->hw_io >= 0)
		release_region(dev->hw_io, ENE_IO_SIZE);
error1:
	input_free_device(input_dev);
	kfree(ir_props);
	kfree(dev);
	return error;
}

/* main unload function */
static void ene_remove(struct pnp_dev *pnp_dev)
{
	struct ene_device *dev = pnp_get_drvdata(pnp_dev);
	unsigned long flags;

	spin_lock_irqsave(&dev->hw_lock, flags);
	ene_rx_disable(dev);
	ene_rx_restore_hw_buffer(dev);
	spin_unlock_irqrestore(&dev->hw_lock, flags);

	free_irq(dev->irq, dev);
	release_region(dev->hw_io, ENE_IO_SIZE);
	ir_input_unregister(dev->idev);
	kfree(dev->props);
	kfree(dev);
}

/* enable wake on IR (wakes on specific button on original remote) */
static void ene_enable_wake(struct ene_device *dev, int enable)
{
	enable = enable && device_may_wakeup(&dev->pnp_dev->dev);
	dbg("wake on IR %s", enable ? "enabled" : "disabled");
	ene_set_clear_reg_mask(dev, ENE_FW1, ENE_FW1_WAKE, enable);
}

#ifdef CONFIG_PM
static int ene_suspend(struct pnp_dev *pnp_dev, pm_message_t state)
{
	struct ene_device *dev = pnp_get_drvdata(pnp_dev);
	ene_enable_wake(dev, true);

	/* TODO: add support for wake pattern */
	return 0;
}

static int ene_resume(struct pnp_dev *pnp_dev)
{
	struct ene_device *dev = pnp_get_drvdata(pnp_dev);
	ene_setup_hw_settings(dev);

	if (dev->rx_enabled)
		ene_rx_enable(dev);

	ene_enable_wake(dev, false);
	return 0;
}
#endif

static void ene_shutdown(struct pnp_dev *pnp_dev)
{
	struct ene_device *dev = pnp_get_drvdata(pnp_dev);
	ene_enable_wake(dev, true);
}

static const struct pnp_device_id ene_ids[] = {
	{.id = "ENE0100",},
	{.id = "ENE0200",},
	{.id = "ENE0201",},
	{.id = "ENE0202",},
	{},
};

static struct pnp_driver ene_driver = {
	.name = ENE_DRIVER_NAME,
	.id_table = ene_ids,
	.flags = PNP_DRIVER_RES_DO_NOT_CHANGE,

	.probe = ene_probe,
	.remove = __devexit_p(ene_remove),
#ifdef CONFIG_PM
	.suspend = ene_suspend,
	.resume = ene_resume,
#endif
	.shutdown = ene_shutdown,
};

static int __init ene_init(void)
{
	return pnp_register_driver(&ene_driver);
}

static void ene_exit(void)
{
	pnp_unregister_driver(&ene_driver);
}

module_param(sample_period, int, S_IRUGO);
MODULE_PARM_DESC(sample_period, "Hardware sample period (50 us default)");

module_param(learning_mode_force, bool, S_IRUGO);
MODULE_PARM_DESC(learning_mode_force, "Enable learning mode by default");

module_param(debug, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Debug level");

module_param(txsim, bool, S_IRUGO);
MODULE_PARM_DESC(txsim,
	"Simulate TX features on unsupported hardware (dangerous)");

MODULE_DEVICE_TABLE(pnp, ene_ids);
MODULE_DESCRIPTION
	("Infrared input driver for KB3926B/C/D/E/F "
	"(aka ENE0100/ENE0200/ENE0201/ENE0202) CIR port");

MODULE_AUTHOR("Maxim Levitsky");
MODULE_LICENSE("GPL");

module_init(ene_init);
module_exit(ene_exit);

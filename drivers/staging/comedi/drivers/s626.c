/*
 * comedi/drivers/s626.c
 * Sensoray s626 Comedi driver
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 2000 David A. Schleef <ds@schleef.org>
 *
 * Based on Sensoray Model 626 Linux driver Version 0.2
 * Copyright (C) 2002-2004 Sensoray Co., Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/*
 * Driver: s626
 * Description: Sensoray 626 driver
 * Devices: [Sensoray] 626 (s626)
 * Authors: Gianluca Palli <gpalli@deis.unibo.it>,
 * Updated: Fri, 15 Feb 2008 10:28:42 +0000
 * Status: experimental

 * Configuration options: not applicable, uses PCI auto config

 * INSN_CONFIG instructions:
 *   analog input:
 *    none
 *
 *   analog output:
 *    none
 *
 *   digital channel:
 *    s626 has 3 dio subdevices (2,3 and 4) each with 16 i/o channels
 *    supported configuration options:
 *    INSN_CONFIG_DIO_QUERY
 *    COMEDI_INPUT
 *    COMEDI_OUTPUT
 *
 *   encoder:
 *    Every channel must be configured before reading.
 *
 *   Example code
 *
 *    insn.insn=INSN_CONFIG;   //configuration instruction
 *    insn.n=1;                //number of operation (must be 1)
 *    insn.data=&initialvalue; //initial value loaded into encoder
 *                             //during configuration
 *    insn.subdev=5;           //encoder subdevice
 *    insn.chanspec=CR_PACK(encoder_channel,0,AREF_OTHER); //encoder_channel
 *                                                         //to configure
 *
 *    comedi_do_insn(cf,&insn); //executing configuration
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/types.h>

#include "../comedi_pci.h"

#include "s626.h"

struct s626_buffer_dma {
	dma_addr_t physical_base;
	void *logical_base;
};

struct s626_private {
	u8 ai_cmd_running;		/* ai_cmd is running */
	unsigned int ai_sample_timer;	/* time between samples in
					 * units of the timer */
	int ai_convert_count;		/* conversion counter */
	unsigned int ai_convert_timer;	/* time between conversion in
					 * units of the timer */
	u16 counter_int_enabs;	        /* counter interrupt enable mask
					 * for MISC2 register */
	u8 adc_items;		        /* number of items in ADC poll list */
	struct s626_buffer_dma rps_buf;	/* DMA buffer used to hold ADC (RPS1)
					 * program */
	struct s626_buffer_dma ana_buf;	/* DMA buffer used to receive ADC data
					 * and hold DAC data */
	u32 *dac_wbuf;		        /* pointer to logical adrs of DMA buffer
					 * used to hold DAC data */
	u16 dacpol;		        /* image of DAC polarity register */
	u8 trim_setpoint[12];	        /* images of TrimDAC setpoints */
	u32 i2c_adrs;		        /* I2C device address for onboard EEPROM
					 * (board rev dependent) */
};

/* Counter overflow/index event flag masks for RDMISC2. */
#define S626_INDXMASK(C) (1 << (((C) > 2) ? ((C) * 2 - 1) : ((C) * 2 +  4)))
#define S626_OVERMASK(C) (1 << (((C) > 2) ? ((C) * 2 + 5) : ((C) * 2 + 10)))

/*
 * Enable/disable a function or test status bit(s) that are accessed
 * through Main Control Registers 1 or 2.
 */
static void s626_mc_enable(struct comedi_device *dev,
			   unsigned int cmd, unsigned int reg)
{
	unsigned int val = (cmd << 16) | cmd;

	mmiowb();
	writel(val, dev->mmio + reg);
}

static void s626_mc_disable(struct comedi_device *dev,
			    unsigned int cmd, unsigned int reg)
{
	writel(cmd << 16, dev->mmio + reg);
	mmiowb();
}

static bool s626_mc_test(struct comedi_device *dev,
			 unsigned int cmd, unsigned int reg)
{
	unsigned int val;

	val = readl(dev->mmio + reg);

	return (val & cmd) ? true : false;
}

#define S626_BUGFIX_STREG(REGADRS)   ((REGADRS) - 4)

/* Write a time slot control record to TSL2. */
#define S626_VECTPORT(VECTNUM)		(S626_P_TSL2 + ((VECTNUM) << 2))

static const struct comedi_lrange s626_range_table = {
	2, {
		BIP_RANGE(5),
		BIP_RANGE(10)
	}
};

/*
 * Execute a DEBI transfer.  This must be called from within a critical section.
 */
static void s626_debi_transfer(struct comedi_device *dev)
{
	static const int timeout = 10000;
	int i;

	/* Initiate upload of shadow RAM to DEBI control register */
	s626_mc_enable(dev, S626_MC2_UPLD_DEBI, S626_P_MC2);

	/*
	 * Wait for completion of upload from shadow RAM to
	 * DEBI control register.
	 */
	for (i = 0; i < timeout; i++) {
		if (s626_mc_test(dev, S626_MC2_UPLD_DEBI, S626_P_MC2))
			break;
		udelay(1);
	}
	if (i == timeout)
		dev_err(dev->class_dev,
			"Timeout while uploading to DEBI control register\n");

	/* Wait until DEBI transfer is done */
	for (i = 0; i < timeout; i++) {
		if (!(readl(dev->mmio + S626_P_PSR) & S626_PSR_DEBI_S))
			break;
		udelay(1);
	}
	if (i == timeout)
		dev_err(dev->class_dev, "DEBI transfer timeout\n");
}

/*
 * Read a value from a gate array register.
 */
static u16 s626_debi_read(struct comedi_device *dev, u16 addr)
{
	/* Set up DEBI control register value in shadow RAM */
	writel(S626_DEBI_CMD_RDWORD | addr, dev->mmio + S626_P_DEBICMD);

	/*  Execute the DEBI transfer. */
	s626_debi_transfer(dev);

	return readl(dev->mmio + S626_P_DEBIAD);
}

/*
 * Write a value to a gate array register.
 */
static void s626_debi_write(struct comedi_device *dev, u16 addr,
			    u16 wdata)
{
	/* Set up DEBI control register value in shadow RAM */
	writel(S626_DEBI_CMD_WRWORD | addr, dev->mmio + S626_P_DEBICMD);
	writel(wdata, dev->mmio + S626_P_DEBIAD);

	/*  Execute the DEBI transfer. */
	s626_debi_transfer(dev);
}

/*
 * Replace the specified bits in a gate array register.  Imports: mask
 * specifies bits that are to be preserved, wdata is new value to be
 * or'd with the masked original.
 */
static void s626_debi_replace(struct comedi_device *dev, unsigned int addr,
			      unsigned int mask, unsigned int wdata)
{
	unsigned int val;

	addr &= 0xffff;
	writel(S626_DEBI_CMD_RDWORD | addr, dev->mmio + S626_P_DEBICMD);
	s626_debi_transfer(dev);

	writel(S626_DEBI_CMD_WRWORD | addr, dev->mmio + S626_P_DEBICMD);
	val = readl(dev->mmio + S626_P_DEBIAD);
	val &= mask;
	val |= wdata;
	writel(val & 0xffff, dev->mmio + S626_P_DEBIAD);
	s626_debi_transfer(dev);
}

/* **************  EEPROM ACCESS FUNCTIONS  ************** */

static int s626_i2c_handshake_eoc(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn,
				  unsigned long context)
{
	bool status;

	status = s626_mc_test(dev, S626_MC2_UPLD_IIC, S626_P_MC2);
	if (status)
		return 0;
	return -EBUSY;
}

static int s626_i2c_handshake(struct comedi_device *dev, u32 val)
{
	unsigned int ctrl;
	int ret;

	/* Write I2C command to I2C Transfer Control shadow register */
	writel(val, dev->mmio + S626_P_I2CCTRL);

	/*
	 * Upload I2C shadow registers into working registers and
	 * wait for upload confirmation.
	 */
	s626_mc_enable(dev, S626_MC2_UPLD_IIC, S626_P_MC2);
	ret = comedi_timeout(dev, NULL, NULL, s626_i2c_handshake_eoc, 0);
	if (ret)
		return ret;

	/* Wait until I2C bus transfer is finished or an error occurs */
	do {
		ctrl = readl(dev->mmio + S626_P_I2CCTRL);
	} while ((ctrl & (S626_I2C_BUSY | S626_I2C_ERR)) == S626_I2C_BUSY);

	/* Return non-zero if I2C error occurred */
	return ctrl & S626_I2C_ERR;
}

/* Read u8 from EEPROM. */
static u8 s626_i2c_read(struct comedi_device *dev, u8 addr)
{
	struct s626_private *devpriv = dev->private;

	/*
	 * Send EEPROM target address:
	 *  Byte2 = I2C command: write to I2C EEPROM device.
	 *  Byte1 = EEPROM internal target address.
	 *  Byte0 = Not sent.
	 */
	if (s626_i2c_handshake(dev, S626_I2C_B2(S626_I2C_ATTRSTART,
						devpriv->i2c_adrs) |
				    S626_I2C_B1(S626_I2C_ATTRSTOP, addr) |
				    S626_I2C_B0(S626_I2C_ATTRNOP, 0)))
		/* Abort function and declare error if handshake failed. */
		return 0;

	/*
	 * Execute EEPROM read:
	 *  Byte2 = I2C command: read from I2C EEPROM device.
	 *  Byte1 receives uint8_t from EEPROM.
	 *  Byte0 = Not sent.
	 */
	if (s626_i2c_handshake(dev, S626_I2C_B2(S626_I2C_ATTRSTART,
						(devpriv->i2c_adrs | 1)) |
				    S626_I2C_B1(S626_I2C_ATTRSTOP, 0) |
				    S626_I2C_B0(S626_I2C_ATTRNOP, 0)))
		/* Abort function and declare error if handshake failed. */
		return 0;

	return (readl(dev->mmio + S626_P_I2CCTRL) >> 16) & 0xff;
}

/* ***********  DAC FUNCTIONS *********** */

/* TrimDac LogicalChan-to-PhysicalChan mapping table. */
static const u8 s626_trimchan[] = { 10, 9, 8, 3, 2, 7, 6, 1, 0, 5, 4 };

/* TrimDac LogicalChan-to-EepromAdrs mapping table. */
static const u8 s626_trimadrs[] = {
	0x40, 0x41, 0x42, 0x50, 0x51, 0x52, 0x53, 0x60, 0x61, 0x62, 0x63
};

enum {
	s626_send_dac_wait_not_mc1_a2out,
	s626_send_dac_wait_ssr_af2_out,
	s626_send_dac_wait_fb_buffer2_msb_00,
	s626_send_dac_wait_fb_buffer2_msb_ff
};

static int s626_send_dac_eoc(struct comedi_device *dev,
			     struct comedi_subdevice *s,
			     struct comedi_insn *insn,
			     unsigned long context)
{
	unsigned int status;

	switch (context) {
	case s626_send_dac_wait_not_mc1_a2out:
		status = readl(dev->mmio + S626_P_MC1);
		if (!(status & S626_MC1_A2OUT))
			return 0;
		break;
	case s626_send_dac_wait_ssr_af2_out:
		status = readl(dev->mmio + S626_P_SSR);
		if (status & S626_SSR_AF2_OUT)
			return 0;
		break;
	case s626_send_dac_wait_fb_buffer2_msb_00:
		status = readl(dev->mmio + S626_P_FB_BUFFER2);
		if (!(status & 0xff000000))
			return 0;
		break;
	case s626_send_dac_wait_fb_buffer2_msb_ff:
		status = readl(dev->mmio + S626_P_FB_BUFFER2);
		if (status & 0xff000000)
			return 0;
		break;
	default:
		return -EINVAL;
	}
	return -EBUSY;
}

/*
 * Private helper function: Transmit serial data to DAC via Audio
 * channel 2.  Assumes: (1) TSL2 slot records initialized, and (2)
 * dacpol contains valid target image.
 */
static int s626_send_dac(struct comedi_device *dev, u32 val)
{
	struct s626_private *devpriv = dev->private;
	int ret;

	/* START THE SERIAL CLOCK RUNNING ------------- */

	/*
	 * Assert DAC polarity control and enable gating of DAC serial clock
	 * and audio bit stream signals.  At this point in time we must be
	 * assured of being in time slot 0.  If we are not in slot 0, the
	 * serial clock and audio stream signals will be disabled; this is
	 * because the following s626_debi_write statement (which enables
	 * signals to be passed through the gate array) would execute before
	 * the trailing edge of WS1/WS3 (which turns off the signals), thus
	 * causing the signals to be inactive during the DAC write.
	 */
	s626_debi_write(dev, S626_LP_DACPOL, devpriv->dacpol);

	/* TRANSFER OUTPUT DWORD VALUE INTO A2'S OUTPUT FIFO ---------------- */

	/* Copy DAC setpoint value to DAC's output DMA buffer. */
	/* writel(val, dev->mmio + (uint32_t)devpriv->dac_wbuf); */
	*devpriv->dac_wbuf = val;

	/*
	 * Enable the output DMA transfer. This will cause the DMAC to copy
	 * the DAC's data value to A2's output FIFO. The DMA transfer will
	 * then immediately terminate because the protection address is
	 * reached upon transfer of the first DWORD value.
	 */
	s626_mc_enable(dev, S626_MC1_A2OUT, S626_P_MC1);

	/* While the DMA transfer is executing ... */

	/*
	 * Reset Audio2 output FIFO's underflow flag (along with any
	 * other FIFO underflow/overflow flags). When set, this flag
	 * will indicate that we have emerged from slot 0.
	 */
	writel(S626_ISR_AFOU, dev->mmio + S626_P_ISR);

	/*
	 * Wait for the DMA transfer to finish so that there will be data
	 * available in the FIFO when time slot 1 tries to transfer a DWORD
	 * from the FIFO to the output buffer register.  We test for DMA
	 * Done by polling the DMAC enable flag; this flag is automatically
	 * cleared when the transfer has finished.
	 */
	ret = comedi_timeout(dev, NULL, NULL, s626_send_dac_eoc,
			     s626_send_dac_wait_not_mc1_a2out);
	if (ret) {
		dev_err(dev->class_dev, "DMA transfer timeout\n");
		return ret;
	}

	/* START THE OUTPUT STREAM TO THE TARGET DAC -------------------- */

	/*
	 * FIFO data is now available, so we enable execution of time slots
	 * 1 and higher by clearing the EOS flag in slot 0.  Note that SD3
	 * will be shifted in and stored in FB_BUFFER2 for end-of-slot-list
	 * detection.
	 */
	writel(S626_XSD2 | S626_RSD3 | S626_SIB_A2,
	       dev->mmio + S626_VECTPORT(0));

	/*
	 * Wait for slot 1 to execute to ensure that the Packet will be
	 * transmitted.  This is detected by polling the Audio2 output FIFO
	 * underflow flag, which will be set when slot 1 execution has
	 * finished transferring the DAC's data DWORD from the output FIFO
	 * to the output buffer register.
	 */
	ret = comedi_timeout(dev, NULL, NULL, s626_send_dac_eoc,
			     s626_send_dac_wait_ssr_af2_out);
	if (ret) {
		dev_err(dev->class_dev,
			"TSL timeout waiting for slot 1 to execute\n");
		return ret;
	}

	/*
	 * Set up to trap execution at slot 0 when the TSL sequencer cycles
	 * back to slot 0 after executing the EOS in slot 5.  Also,
	 * simultaneously shift out and in the 0x00 that is ALWAYS the value
	 * stored in the last byte to be shifted out of the FIFO's DWORD
	 * buffer register.
	 */
	writel(S626_XSD2 | S626_XFIFO_2 | S626_RSD2 | S626_SIB_A2 | S626_EOS,
	       dev->mmio + S626_VECTPORT(0));

	/* WAIT FOR THE TRANSACTION TO FINISH ----------------------- */

	/*
	 * Wait for the TSL to finish executing all time slots before
	 * exiting this function.  We must do this so that the next DAC
	 * write doesn't start, thereby enabling clock/chip select signals:
	 *
	 * 1. Before the TSL sequence cycles back to slot 0, which disables
	 *    the clock/cs signal gating and traps slot // list execution.
	 *    we have not yet finished slot 5 then the clock/cs signals are
	 *    still gated and we have not finished transmitting the stream.
	 *
	 * 2. While slots 2-5 are executing due to a late slot 0 trap.  In
	 *    this case, the slot sequence is currently repeating, but with
	 *    clock/cs signals disabled.  We must wait for slot 0 to trap
	 *    execution before setting up the next DAC setpoint DMA transfer
	 *    and enabling the clock/cs signals.  To detect the end of slot 5,
	 *    we test for the FB_BUFFER2 MSB contents to be equal to 0xFF.  If
	 *    the TSL has not yet finished executing slot 5 ...
	 */
	if (readl(dev->mmio + S626_P_FB_BUFFER2) & 0xff000000) {
		/*
		 * The trap was set on time and we are still executing somewhere
		 * in slots 2-5, so we now wait for slot 0 to execute and trap
		 * TSL execution.  This is detected when FB_BUFFER2 MSB changes
		 * from 0xFF to 0x00, which slot 0 causes to happen by shifting
		 * out/in on SD2 the 0x00 that is always referenced by slot 5.
		 */
		ret = comedi_timeout(dev, NULL, NULL, s626_send_dac_eoc,
				     s626_send_dac_wait_fb_buffer2_msb_00);
		if (ret) {
			dev_err(dev->class_dev,
				"TSL timeout waiting for slot 0 to execute\n");
			return ret;
		}
	}
	/*
	 * Either (1) we were too late setting the slot 0 trap; the TSL
	 * sequencer restarted slot 0 before we could set the EOS trap flag,
	 * or (2) we were not late and execution is now trapped at slot 0.
	 * In either case, we must now change slot 0 so that it will store
	 * value 0xFF (instead of 0x00) to FB_BUFFER2 next time it executes.
	 * In order to do this, we reprogram slot 0 so that it will shift in
	 * SD3, which is driven only by a pull-up resistor.
	 */
	writel(S626_RSD3 | S626_SIB_A2 | S626_EOS,
	       dev->mmio + S626_VECTPORT(0));

	/*
	 * Wait for slot 0 to execute, at which time the TSL is setup for
	 * the next DAC write.  This is detected when FB_BUFFER2 MSB changes
	 * from 0x00 to 0xFF.
	 */
	ret = comedi_timeout(dev, NULL, NULL, s626_send_dac_eoc,
			     s626_send_dac_wait_fb_buffer2_msb_ff);
	if (ret) {
		dev_err(dev->class_dev,
			"TSL timeout waiting for slot 0 to execute\n");
		return ret;
	}
	return 0;
}

/*
 * Private helper function: Write setpoint to an application DAC channel.
 */
static int s626_set_dac(struct comedi_device *dev,
			u16 chan, int16_t dacdata)
{
	struct s626_private *devpriv = dev->private;
	u16 signmask;
	u32 ws_image;
	u32 val;

	/*
	 * Adjust DAC data polarity and set up Polarity Control Register image.
	 */
	signmask = 1 << chan;
	if (dacdata < 0) {
		dacdata = -dacdata;
		devpriv->dacpol |= signmask;
	} else {
		devpriv->dacpol &= ~signmask;
	}

	/* Limit DAC setpoint value to valid range. */
	if ((u16)dacdata > 0x1FFF)
		dacdata = 0x1FFF;

	/*
	 * Set up TSL2 records (aka "vectors") for DAC update.  Vectors V2
	 * and V3 transmit the setpoint to the target DAC.  V4 and V5 send
	 * data to a non-existent TrimDac channel just to keep the clock
	 * running after sending data to the target DAC.  This is necessary
	 * to eliminate the clock glitch that would otherwise occur at the
	 * end of the target DAC's serial data stream.  When the sequence
	 * restarts at V0 (after executing V5), the gate array automatically
	 * disables gating for the DAC clock and all DAC chip selects.
	 */

	/* Choose DAC chip select to be asserted */
	ws_image = (chan & 2) ? S626_WS1 : S626_WS2;
	/* Slot 2: Transmit high data byte to target DAC */
	writel(S626_XSD2 | S626_XFIFO_1 | ws_image,
	       dev->mmio + S626_VECTPORT(2));
	/* Slot 3: Transmit low data byte to target DAC */
	writel(S626_XSD2 | S626_XFIFO_0 | ws_image,
	       dev->mmio + S626_VECTPORT(3));
	/* Slot 4: Transmit to non-existent TrimDac channel to keep clock */
	writel(S626_XSD2 | S626_XFIFO_3 | S626_WS3,
	       dev->mmio + S626_VECTPORT(4));
	/* Slot 5: running after writing target DAC's low data byte */
	writel(S626_XSD2 | S626_XFIFO_2 | S626_WS3 | S626_EOS,
	       dev->mmio + S626_VECTPORT(5));

	/*
	 * Construct and transmit target DAC's serial packet:
	 * (A10D DDDD), (DDDD DDDD), (0x0F), (0x00) where A is chan<0>,
	 * and D<12:0> is the DAC setpoint.  Append a WORD value (that writes
	 * to a  non-existent TrimDac channel) that serves to keep the clock
	 * running after the packet has been sent to the target DAC.
	 */
	val = 0x0F000000;	/* Continue clock after target DAC data
				 * (write to non-existent trimdac). */
	val |= 0x00004000;	/* Address the two main dual-DAC devices
				 * (TSL's chip select enables target device). */
	val |= ((u32)(chan & 1) << 15);	/* Address the DAC channel
						 * within the device. */
	val |= (u32)dacdata;	/* Include DAC setpoint data. */
	return s626_send_dac(dev, val);
}

static int s626_write_trim_dac(struct comedi_device *dev,
			       u8 logical_chan, u8 dac_data)
{
	struct s626_private *devpriv = dev->private;
	u32 chan;

	/*
	 * Save the new setpoint in case the application needs to read it back
	 * later.
	 */
	devpriv->trim_setpoint[logical_chan] = (u8)dac_data;

	/* Map logical channel number to physical channel number. */
	chan = s626_trimchan[logical_chan];

	/*
	 * Set up TSL2 records for TrimDac write operation.  All slots shift
	 * 0xFF in from pulled-up SD3 so that the end of the slot sequence
	 * can be detected.
	 */

	/* Slot 2: Send high uint8_t to target TrimDac */
	writel(S626_XSD2 | S626_XFIFO_1 | S626_WS3,
	       dev->mmio + S626_VECTPORT(2));
	/* Slot 3: Send low uint8_t to target TrimDac */
	writel(S626_XSD2 | S626_XFIFO_0 | S626_WS3,
	       dev->mmio + S626_VECTPORT(3));
	/* Slot 4: Send NOP high uint8_t to DAC0 to keep clock running */
	writel(S626_XSD2 | S626_XFIFO_3 | S626_WS1,
	       dev->mmio + S626_VECTPORT(4));
	/* Slot 5: Send NOP low  uint8_t to DAC0 */
	writel(S626_XSD2 | S626_XFIFO_2 | S626_WS1 | S626_EOS,
	       dev->mmio + S626_VECTPORT(5));

	/*
	 * Construct and transmit target DAC's serial packet:
	 * (0000 AAAA), (DDDD DDDD), (0x00), (0x00) where A<3:0> is the
	 * DAC channel's address, and D<7:0> is the DAC setpoint.  Append a
	 * WORD value (that writes a channel 0 NOP command to a non-existent
	 * main DAC channel) that serves to keep the clock running after the
	 * packet has been sent to the target DAC.
	 */

	/*
	 * Address the DAC channel within the trimdac device.
	 * Include DAC setpoint data.
	 */
	return s626_send_dac(dev, (chan << 8) | dac_data);
}

static int s626_load_trim_dacs(struct comedi_device *dev)
{
	u8 i;
	int ret;

	/* Copy TrimDac setpoint values from EEPROM to TrimDacs. */
	for (i = 0; i < ARRAY_SIZE(s626_trimchan); i++) {
		ret = s626_write_trim_dac(dev, i,
					  s626_i2c_read(dev, s626_trimadrs[i]));
		if (ret)
			return ret;
	}
	return 0;
}

/* ******  COUNTER FUNCTIONS  ******* */

/*
 * All counter functions address a specific counter by means of the
 * "Counter" argument, which is a logical counter number.  The Counter
 * argument may have any of the following legal values: 0=0A, 1=1A,
 * 2=2A, 3=0B, 4=1B, 5=2B.
 */

/*
 * Return/set a counter pair's latch trigger source.  0: On read
 * access, 1: A index latches A, 2: B index latches B, 3: A overflow
 * latches B.
 */
static void s626_set_latch_source(struct comedi_device *dev,
				  unsigned int chan, u16 value)
{
	s626_debi_replace(dev, S626_LP_CRB(chan),
			  ~(S626_CRBMSK_INTCTRL | S626_CRBMSK_LATCHSRC),
			  S626_SET_CRB_LATCHSRC(value));
}

/*
 * Write value into counter preload register.
 */
static void s626_preload(struct comedi_device *dev,
			 unsigned int chan, u32 value)
{
	s626_debi_write(dev, S626_LP_CNTR(chan), value);
	s626_debi_write(dev, S626_LP_CNTR(chan) + 2, value >> 16);
}

/* ******  PRIVATE COUNTER FUNCTIONS ****** */

/*
 * Reset a counter's index and overflow event capture flags.
 */
static void s626_reset_cap_flags(struct comedi_device *dev,
				 unsigned int chan)
{
	u16 set;

	set = S626_SET_CRB_INTRESETCMD(1);
	if (chan < 3)
		set |= S626_SET_CRB_INTRESET_A(1);
	else
		set |= S626_SET_CRB_INTRESET_B(1);

	s626_debi_replace(dev, S626_LP_CRB(chan), ~S626_CRBMSK_INTCTRL, set);
}

/*
 * Set the operating mode for the specified counter.  The setup
 * parameter is treated as a COUNTER_SETUP data type.  The following
 * parameters are programmable (all other parms are ignored): ClkMult,
 * ClkPol, ClkEnab, IndexSrc, IndexPol, LoadSrc.
 */
static void s626_set_mode_a(struct comedi_device *dev,
			    unsigned int chan, u16 setup,
			    u16 disable_int_src)
{
	struct s626_private *devpriv = dev->private;
	u16 cra;
	u16 crb;
	unsigned int cntsrc, clkmult, clkpol;

	/* Initialize CRA and CRB images. */
	/* Preload trigger is passed through. */
	cra = S626_SET_CRA_LOADSRC_A(S626_GET_STD_LOADSRC(setup));
	/* IndexSrc is passed through. */
	cra |= S626_SET_CRA_INDXSRC_A(S626_GET_STD_INDXSRC(setup));

	/* Reset any pending CounterA event captures. */
	crb = S626_SET_CRB_INTRESETCMD(1) | S626_SET_CRB_INTRESET_A(1);
	/* Clock enable is passed through. */
	crb |= S626_SET_CRB_CLKENAB_A(S626_GET_STD_CLKENAB(setup));

	/* Force IntSrc to Disabled if disable_int_src is asserted. */
	if (!disable_int_src)
		cra |= S626_SET_CRA_INTSRC_A(S626_GET_STD_INTSRC(setup));

	/* Populate all mode-dependent attributes of CRA & CRB images. */
	clkpol = S626_GET_STD_CLKPOL(setup);
	switch (S626_GET_STD_ENCMODE(setup)) {
	case S626_ENCMODE_EXTENDER: /* Extender Mode: */
		/* Force to Timer mode (Extender valid only for B counters). */
		/* Fall through to case S626_ENCMODE_TIMER: */
	case S626_ENCMODE_TIMER:	/* Timer Mode: */
		/* CntSrcA<1> selects system clock */
		cntsrc = S626_CNTSRC_SYSCLK;
		/* Count direction (CntSrcA<0>) obtained from ClkPol. */
		cntsrc |= clkpol;
		/* ClkPolA behaves as always-on clock enable. */
		clkpol = 1;
		/* ClkMult must be 1x. */
		clkmult = S626_CLKMULT_1X;
		break;
	default:		/* Counter Mode: */
		/* Select ENC_C and ENC_D as clock/direction inputs. */
		cntsrc = S626_CNTSRC_ENCODER;
		/* Clock polarity is passed through. */
		/* Force multiplier to x1 if not legal, else pass through. */
		clkmult = S626_GET_STD_CLKMULT(setup);
		if (clkmult == S626_CLKMULT_SPECIAL)
			clkmult = S626_CLKMULT_1X;
		break;
	}
	cra |= S626_SET_CRA_CNTSRC_A(cntsrc) | S626_SET_CRA_CLKPOL_A(clkpol) |
	       S626_SET_CRA_CLKMULT_A(clkmult);

	/*
	 * Force positive index polarity if IndxSrc is software-driven only,
	 * otherwise pass it through.
	 */
	if (S626_GET_STD_INDXSRC(setup) != S626_INDXSRC_SOFT)
		cra |= S626_SET_CRA_INDXPOL_A(S626_GET_STD_INDXPOL(setup));

	/*
	 * If IntSrc has been forced to Disabled, update the MISC2 interrupt
	 * enable mask to indicate the counter interrupt is disabled.
	 */
	if (disable_int_src)
		devpriv->counter_int_enabs &= ~(S626_OVERMASK(chan) |
						S626_INDXMASK(chan));

	/*
	 * While retaining CounterB and LatchSrc configurations, program the
	 * new counter operating mode.
	 */
	s626_debi_replace(dev, S626_LP_CRA(chan),
			  S626_CRAMSK_INDXSRC_B | S626_CRAMSK_CNTSRC_B, cra);
	s626_debi_replace(dev, S626_LP_CRB(chan),
			  ~(S626_CRBMSK_INTCTRL | S626_CRBMSK_CLKENAB_A), crb);
}

static void s626_set_mode_b(struct comedi_device *dev,
			    unsigned int chan, u16 setup,
			    u16 disable_int_src)
{
	struct s626_private *devpriv = dev->private;
	u16 cra;
	u16 crb;
	unsigned int cntsrc, clkmult, clkpol;

	/* Initialize CRA and CRB images. */
	/* IndexSrc is passed through. */
	cra = S626_SET_CRA_INDXSRC_B(S626_GET_STD_INDXSRC(setup));

	/* Reset event captures and disable interrupts. */
	crb = S626_SET_CRB_INTRESETCMD(1) | S626_SET_CRB_INTRESET_B(1);
	/* Clock enable is passed through. */
	crb |= S626_SET_CRB_CLKENAB_B(S626_GET_STD_CLKENAB(setup));
	/* Preload trigger source is passed through. */
	crb |= S626_SET_CRB_LOADSRC_B(S626_GET_STD_LOADSRC(setup));

	/* Force IntSrc to Disabled if disable_int_src is asserted. */
	if (!disable_int_src)
		crb |= S626_SET_CRB_INTSRC_B(S626_GET_STD_INTSRC(setup));

	/* Populate all mode-dependent attributes of CRA & CRB images. */
	clkpol = S626_GET_STD_CLKPOL(setup);
	switch (S626_GET_STD_ENCMODE(setup)) {
	case S626_ENCMODE_TIMER:	/* Timer Mode: */
		/* CntSrcB<1> selects system clock */
		cntsrc = S626_CNTSRC_SYSCLK;
		/* with direction (CntSrcB<0>) obtained from ClkPol. */
		cntsrc |= clkpol;
		/* ClkPolB behaves as always-on clock enable. */
		clkpol = 1;
		/* ClkMultB must be 1x. */
		clkmult = S626_CLKMULT_1X;
		break;
	case S626_ENCMODE_EXTENDER:	/* Extender Mode: */
		/* CntSrcB source is OverflowA (same as "timer") */
		cntsrc = S626_CNTSRC_SYSCLK;
		/* with direction obtained from ClkPol. */
		cntsrc |= clkpol;
		/* ClkPolB controls IndexB -- always set to active. */
		clkpol = 1;
		/* ClkMultB selects OverflowA as the clock source. */
		clkmult = S626_CLKMULT_SPECIAL;
		break;
	default:		/* Counter Mode: */
		/* Select ENC_C and ENC_D as clock/direction inputs. */
		cntsrc = S626_CNTSRC_ENCODER;
		/* ClkPol is passed through. */
		/* Force ClkMult to x1 if not legal, otherwise pass through. */
		clkmult = S626_GET_STD_CLKMULT(setup);
		if (clkmult == S626_CLKMULT_SPECIAL)
			clkmult = S626_CLKMULT_1X;
		break;
	}
	cra |= S626_SET_CRA_CNTSRC_B(cntsrc);
	crb |= S626_SET_CRB_CLKPOL_B(clkpol) | S626_SET_CRB_CLKMULT_B(clkmult);

	/*
	 * Force positive index polarity if IndxSrc is software-driven only,
	 * otherwise pass it through.
	 */
	if (S626_GET_STD_INDXSRC(setup) != S626_INDXSRC_SOFT)
		crb |= S626_SET_CRB_INDXPOL_B(S626_GET_STD_INDXPOL(setup));

	/*
	 * If IntSrc has been forced to Disabled, update the MISC2 interrupt
	 * enable mask to indicate the counter interrupt is disabled.
	 */
	if (disable_int_src)
		devpriv->counter_int_enabs &= ~(S626_OVERMASK(chan) |
						S626_INDXMASK(chan));

	/*
	 * While retaining CounterA and LatchSrc configurations, program the
	 * new counter operating mode.
	 */
	s626_debi_replace(dev, S626_LP_CRA(chan),
			  ~(S626_CRAMSK_INDXSRC_B | S626_CRAMSK_CNTSRC_B), cra);
	s626_debi_replace(dev, S626_LP_CRB(chan),
			  S626_CRBMSK_CLKENAB_A | S626_CRBMSK_LATCHSRC, crb);
}

static void s626_set_mode(struct comedi_device *dev,
			  unsigned int chan,
			  u16 setup, u16 disable_int_src)
{
	if (chan < 3)
		s626_set_mode_a(dev, chan, setup, disable_int_src);
	else
		s626_set_mode_b(dev, chan, setup, disable_int_src);
}

/*
 * Return/set a counter's enable.  enab: 0=always enabled, 1=enabled by index.
 */
static void s626_set_enable(struct comedi_device *dev,
			    unsigned int chan, u16 enab)
{
	unsigned int mask = S626_CRBMSK_INTCTRL;
	unsigned int set;

	if (chan < 3) {
		mask |= S626_CRBMSK_CLKENAB_A;
		set = S626_SET_CRB_CLKENAB_A(enab);
	} else {
		mask |= S626_CRBMSK_CLKENAB_B;
		set = S626_SET_CRB_CLKENAB_B(enab);
	}
	s626_debi_replace(dev, S626_LP_CRB(chan), ~mask, set);
}

/*
 * Return/set the event that will trigger transfer of the preload
 * register into the counter.  0=ThisCntr_Index, 1=ThisCntr_Overflow,
 * 2=OverflowA (B counters only), 3=disabled.
 */
static void s626_set_load_trig(struct comedi_device *dev,
			       unsigned int chan, u16 trig)
{
	u16 reg;
	u16 mask;
	u16 set;

	if (chan < 3) {
		reg = S626_LP_CRA(chan);
		mask = S626_CRAMSK_LOADSRC_A;
		set = S626_SET_CRA_LOADSRC_A(trig);
	} else {
		reg = S626_LP_CRB(chan);
		mask = S626_CRBMSK_LOADSRC_B | S626_CRBMSK_INTCTRL;
		set = S626_SET_CRB_LOADSRC_B(trig);
	}
	s626_debi_replace(dev, reg, ~mask, set);
}

/*
 * Return/set counter interrupt source and clear any captured
 * index/overflow events.  int_source: 0=Disabled, 1=OverflowOnly,
 * 2=IndexOnly, 3=IndexAndOverflow.
 */
static void s626_set_int_src(struct comedi_device *dev,
			     unsigned int chan, u16 int_source)
{
	struct s626_private *devpriv = dev->private;
	u16 cra_reg = S626_LP_CRA(chan);
	u16 crb_reg = S626_LP_CRB(chan);

	if (chan < 3) {
		/* Reset any pending counter overflow or index captures */
		s626_debi_replace(dev, crb_reg, ~S626_CRBMSK_INTCTRL,
				  S626_SET_CRB_INTRESETCMD(1) |
				  S626_SET_CRB_INTRESET_A(1));

		/* Program counter interrupt source */
		s626_debi_replace(dev, cra_reg, ~S626_CRAMSK_INTSRC_A,
				  S626_SET_CRA_INTSRC_A(int_source));
	} else {
		u16 crb;

		/* Cache writeable CRB register image */
		crb = s626_debi_read(dev, crb_reg);
		crb &= ~S626_CRBMSK_INTCTRL;

		/* Reset any pending counter overflow or index captures */
		s626_debi_write(dev, crb_reg,
				crb | S626_SET_CRB_INTRESETCMD(1) |
				S626_SET_CRB_INTRESET_B(1));

		/* Program counter interrupt source */
		s626_debi_write(dev, crb_reg,
				(crb & ~S626_CRBMSK_INTSRC_B) |
				S626_SET_CRB_INTSRC_B(int_source));
	}

	/* Update MISC2 interrupt enable mask. */
	devpriv->counter_int_enabs &= ~(S626_OVERMASK(chan) |
					S626_INDXMASK(chan));
	switch (int_source) {
	case 0:
	default:
		break;
	case 1:
		devpriv->counter_int_enabs |= S626_OVERMASK(chan);
		break;
	case 2:
		devpriv->counter_int_enabs |= S626_INDXMASK(chan);
		break;
	case 3:
		devpriv->counter_int_enabs |= (S626_OVERMASK(chan) |
					       S626_INDXMASK(chan));
		break;
	}
}

/*
 * Generate an index pulse.
 */
static void s626_pulse_index(struct comedi_device *dev,
			     unsigned int chan)
{
	if (chan < 3) {
		u16 cra;

		cra = s626_debi_read(dev, S626_LP_CRA(chan));

		/* Pulse index */
		s626_debi_write(dev, S626_LP_CRA(chan),
				(cra ^ S626_CRAMSK_INDXPOL_A));
		s626_debi_write(dev, S626_LP_CRA(chan), cra);
	} else {
		u16 crb;

		crb = s626_debi_read(dev, S626_LP_CRB(chan));
		crb &= ~S626_CRBMSK_INTCTRL;

		/* Pulse index */
		s626_debi_write(dev, S626_LP_CRB(chan),
				(crb ^ S626_CRBMSK_INDXPOL_B));
		s626_debi_write(dev, S626_LP_CRB(chan), crb);
	}
}

static unsigned int s626_ai_reg_to_uint(unsigned int data)
{
	return ((data >> 18) & 0x3fff) ^ 0x2000;
}

static int s626_dio_set_irq(struct comedi_device *dev, unsigned int chan)
{
	unsigned int group = chan / 16;
	unsigned int mask = 1 << (chan - (16 * group));
	unsigned int status;

	/* set channel to capture positive edge */
	status = s626_debi_read(dev, S626_LP_RDEDGSEL(group));
	s626_debi_write(dev, S626_LP_WREDGSEL(group), mask | status);

	/* enable interrupt on selected channel */
	status = s626_debi_read(dev, S626_LP_RDINTSEL(group));
	s626_debi_write(dev, S626_LP_WRINTSEL(group), mask | status);

	/* enable edge capture write command */
	s626_debi_write(dev, S626_LP_MISC1, S626_MISC1_EDCAP);

	/* enable edge capture on selected channel */
	status = s626_debi_read(dev, S626_LP_RDCAPSEL(group));
	s626_debi_write(dev, S626_LP_WRCAPSEL(group), mask | status);

	return 0;
}

static int s626_dio_reset_irq(struct comedi_device *dev, unsigned int group,
			      unsigned int mask)
{
	/* disable edge capture write command */
	s626_debi_write(dev, S626_LP_MISC1, S626_MISC1_NOEDCAP);

	/* enable edge capture on selected channel */
	s626_debi_write(dev, S626_LP_WRCAPSEL(group), mask);

	return 0;
}

static int s626_dio_clear_irq(struct comedi_device *dev)
{
	unsigned int group;

	/* disable edge capture write command */
	s626_debi_write(dev, S626_LP_MISC1, S626_MISC1_NOEDCAP);

	/* clear all dio pending events and interrupt */
	for (group = 0; group < S626_DIO_BANKS; group++)
		s626_debi_write(dev, S626_LP_WRCAPSEL(group), 0xffff);

	return 0;
}

static void s626_handle_dio_interrupt(struct comedi_device *dev,
				      u16 irqbit, u8 group)
{
	struct s626_private *devpriv = dev->private;
	struct comedi_subdevice *s = dev->read_subdev;
	struct comedi_cmd *cmd = &s->async->cmd;

	s626_dio_reset_irq(dev, group, irqbit);

	if (devpriv->ai_cmd_running) {
		/* check if interrupt is an ai acquisition start trigger */
		if ((irqbit >> (cmd->start_arg - (16 * group))) == 1 &&
		    cmd->start_src == TRIG_EXT) {
			/* Start executing the RPS program */
			s626_mc_enable(dev, S626_MC1_ERPS1, S626_P_MC1);

			if (cmd->scan_begin_src == TRIG_EXT)
				s626_dio_set_irq(dev, cmd->scan_begin_arg);
		}
		if ((irqbit >> (cmd->scan_begin_arg - (16 * group))) == 1 &&
		    cmd->scan_begin_src == TRIG_EXT) {
			/* Trigger ADC scan loop start */
			s626_mc_enable(dev, S626_MC2_ADC_RPS, S626_P_MC2);

			if (cmd->convert_src == TRIG_EXT) {
				devpriv->ai_convert_count = cmd->chanlist_len;

				s626_dio_set_irq(dev, cmd->convert_arg);
			}

			if (cmd->convert_src == TRIG_TIMER) {
				devpriv->ai_convert_count = cmd->chanlist_len;
				s626_set_enable(dev, 5, S626_CLKENAB_ALWAYS);
			}
		}
		if ((irqbit >> (cmd->convert_arg - (16 * group))) == 1 &&
		    cmd->convert_src == TRIG_EXT) {
			/* Trigger ADC scan loop start */
			s626_mc_enable(dev, S626_MC2_ADC_RPS, S626_P_MC2);

			devpriv->ai_convert_count--;
			if (devpriv->ai_convert_count > 0)
				s626_dio_set_irq(dev, cmd->convert_arg);
		}
	}
}

static void s626_check_dio_interrupts(struct comedi_device *dev)
{
	u16 irqbit;
	u8 group;

	for (group = 0; group < S626_DIO_BANKS; group++) {
		/* read interrupt type */
		irqbit = s626_debi_read(dev, S626_LP_RDCAPFLG(group));

		/* check if interrupt is generated from dio channels */
		if (irqbit) {
			s626_handle_dio_interrupt(dev, irqbit, group);
			return;
		}
	}
}

static void s626_check_counter_interrupts(struct comedi_device *dev)
{
	struct s626_private *devpriv = dev->private;
	struct comedi_subdevice *s = dev->read_subdev;
	struct comedi_async *async = s->async;
	struct comedi_cmd *cmd = &async->cmd;
	u16 irqbit;

	/* read interrupt type */
	irqbit = s626_debi_read(dev, S626_LP_RDMISC2);

	/* check interrupt on counters */
	if (irqbit & S626_IRQ_COINT1A) {
		/* clear interrupt capture flag */
		s626_reset_cap_flags(dev, 0);
	}
	if (irqbit & S626_IRQ_COINT2A) {
		/* clear interrupt capture flag */
		s626_reset_cap_flags(dev, 1);
	}
	if (irqbit & S626_IRQ_COINT3A) {
		/* clear interrupt capture flag */
		s626_reset_cap_flags(dev, 2);
	}
	if (irqbit & S626_IRQ_COINT1B) {
		/* clear interrupt capture flag */
		s626_reset_cap_flags(dev, 3);
	}
	if (irqbit & S626_IRQ_COINT2B) {
		/* clear interrupt capture flag */
		s626_reset_cap_flags(dev, 4);

		if (devpriv->ai_convert_count > 0) {
			devpriv->ai_convert_count--;
			if (devpriv->ai_convert_count == 0)
				s626_set_enable(dev, 4, S626_CLKENAB_INDEX);

			if (cmd->convert_src == TRIG_TIMER) {
				/* Trigger ADC scan loop start */
				s626_mc_enable(dev, S626_MC2_ADC_RPS,
					       S626_P_MC2);
			}
		}
	}
	if (irqbit & S626_IRQ_COINT3B) {
		/* clear interrupt capture flag */
		s626_reset_cap_flags(dev, 5);

		if (cmd->scan_begin_src == TRIG_TIMER) {
			/* Trigger ADC scan loop start */
			s626_mc_enable(dev, S626_MC2_ADC_RPS, S626_P_MC2);
		}

		if (cmd->convert_src == TRIG_TIMER) {
			devpriv->ai_convert_count = cmd->chanlist_len;
			s626_set_enable(dev, 4, S626_CLKENAB_ALWAYS);
		}
	}
}

static bool s626_handle_eos_interrupt(struct comedi_device *dev)
{
	struct s626_private *devpriv = dev->private;
	struct comedi_subdevice *s = dev->read_subdev;
	struct comedi_async *async = s->async;
	struct comedi_cmd *cmd = &async->cmd;
	/*
	 * Init ptr to DMA buffer that holds new ADC data.  We skip the
	 * first uint16_t in the buffer because it contains junk data
	 * from the final ADC of the previous poll list scan.
	 */
	u32 *readaddr = (u32 *)devpriv->ana_buf.logical_base + 1;
	int i;

	/* get the data and hand it over to comedi */
	for (i = 0; i < cmd->chanlist_len; i++) {
		unsigned short tempdata;

		/*
		 * Convert ADC data to 16-bit integer values and copy
		 * to application buffer.
		 */
		tempdata = s626_ai_reg_to_uint(*readaddr);
		readaddr++;

		comedi_buf_write_samples(s, &tempdata, 1);
	}

	if (cmd->stop_src == TRIG_COUNT && async->scans_done >= cmd->stop_arg)
		async->events |= COMEDI_CB_EOA;

	if (async->events & COMEDI_CB_CANCEL_MASK)
		devpriv->ai_cmd_running = 0;

	if (devpriv->ai_cmd_running && cmd->scan_begin_src == TRIG_EXT)
		s626_dio_set_irq(dev, cmd->scan_begin_arg);

	comedi_handle_events(dev, s);

	return !devpriv->ai_cmd_running;
}

static irqreturn_t s626_irq_handler(int irq, void *d)
{
	struct comedi_device *dev = d;
	unsigned long flags;
	u32 irqtype, irqstatus;

	if (!dev->attached)
		return IRQ_NONE;
	/* lock to avoid race with comedi_poll */
	spin_lock_irqsave(&dev->spinlock, flags);

	/* save interrupt enable register state */
	irqstatus = readl(dev->mmio + S626_P_IER);

	/* read interrupt type */
	irqtype = readl(dev->mmio + S626_P_ISR);

	/* disable master interrupt */
	writel(0, dev->mmio + S626_P_IER);

	/* clear interrupt */
	writel(irqtype, dev->mmio + S626_P_ISR);

	switch (irqtype) {
	case S626_IRQ_RPS1:	/* end_of_scan occurs */
		if (s626_handle_eos_interrupt(dev))
			irqstatus = 0;
		break;
	case S626_IRQ_GPIO3:	/* check dio and counter interrupt */
		/* s626_dio_clear_irq(dev); */
		s626_check_dio_interrupts(dev);
		s626_check_counter_interrupts(dev);
		break;
	}

	/* enable interrupt */
	writel(irqstatus, dev->mmio + S626_P_IER);

	spin_unlock_irqrestore(&dev->spinlock, flags);
	return IRQ_HANDLED;
}

/*
 * This function builds the RPS program for hardware driven acquisition.
 */
static void s626_reset_adc(struct comedi_device *dev, u8 *ppl)
{
	struct s626_private *devpriv = dev->private;
	struct comedi_subdevice *s = dev->read_subdev;
	struct comedi_cmd *cmd = &s->async->cmd;
	u32 *rps;
	u32 jmp_adrs;
	u16 i;
	u16 n;
	u32 local_ppl;

	/* Stop RPS program in case it is currently running */
	s626_mc_disable(dev, S626_MC1_ERPS1, S626_P_MC1);

	/* Set starting logical address to write RPS commands. */
	rps = (u32 *)devpriv->rps_buf.logical_base;

	/* Initialize RPS instruction pointer */
	writel((u32)devpriv->rps_buf.physical_base,
	       dev->mmio + S626_P_RPSADDR1);

	/* Construct RPS program in rps_buf DMA buffer */
	if (cmd->scan_begin_src != TRIG_FOLLOW) {
		/* Wait for Start trigger. */
		*rps++ = S626_RPS_PAUSE | S626_RPS_SIGADC;
		*rps++ = S626_RPS_CLRSIGNAL | S626_RPS_SIGADC;
	}

	/*
	 * SAA7146 BUG WORKAROUND Do a dummy DEBI Write.  This is necessary
	 * because the first RPS DEBI Write following a non-RPS DEBI write
	 * seems to always fail.  If we don't do this dummy write, the ADC
	 * gain might not be set to the value required for the first slot in
	 * the poll list; the ADC gain would instead remain unchanged from
	 * the previously programmed value.
	 */
	/* Write DEBI Write command and address to shadow RAM. */
	*rps++ = S626_RPS_LDREG | (S626_P_DEBICMD >> 2);
	*rps++ = S626_DEBI_CMD_WRWORD | S626_LP_GSEL;
	*rps++ = S626_RPS_LDREG | (S626_P_DEBIAD >> 2);
	/* Write DEBI immediate data  to shadow RAM: */
	*rps++ = S626_GSEL_BIPOLAR5V;	/* arbitrary immediate data  value. */
	*rps++ = S626_RPS_CLRSIGNAL | S626_RPS_DEBI;
	/* Reset "shadow RAM  uploaded" flag. */
	/* Invoke shadow RAM upload. */
	*rps++ = S626_RPS_UPLOAD | S626_RPS_DEBI;
	/* Wait for shadow upload to finish. */
	*rps++ = S626_RPS_PAUSE | S626_RPS_DEBI;

	/*
	 * Digitize all slots in the poll list. This is implemented as a
	 * for loop to limit the slot count to 16 in case the application
	 * forgot to set the S626_EOPL flag in the final slot.
	 */
	for (devpriv->adc_items = 0; devpriv->adc_items < 16;
	     devpriv->adc_items++) {
		/*
		 * Convert application's poll list item to private board class
		 * format.  Each app poll list item is an uint8_t with form
		 * (EOPL,x,x,RANGE,CHAN<3:0>), where RANGE code indicates 0 =
		 * +-10V, 1 = +-5V, and EOPL = End of Poll List marker.
		 */
		local_ppl = (*ppl << 8) | (*ppl & 0x10 ? S626_GSEL_BIPOLAR5V :
					   S626_GSEL_BIPOLAR10V);

		/* Switch ADC analog gain. */
		/* Write DEBI command and address to shadow RAM. */
		*rps++ = S626_RPS_LDREG | (S626_P_DEBICMD >> 2);
		*rps++ = S626_DEBI_CMD_WRWORD | S626_LP_GSEL;
		/* Write DEBI immediate data to shadow RAM. */
		*rps++ = S626_RPS_LDREG | (S626_P_DEBIAD >> 2);
		*rps++ = local_ppl;
		/* Reset "shadow RAM uploaded" flag. */
		*rps++ = S626_RPS_CLRSIGNAL | S626_RPS_DEBI;
		/* Invoke shadow RAM upload. */
		*rps++ = S626_RPS_UPLOAD | S626_RPS_DEBI;
		/* Wait for shadow upload to finish. */
		*rps++ = S626_RPS_PAUSE | S626_RPS_DEBI;
		/* Select ADC analog input channel. */
		*rps++ = S626_RPS_LDREG | (S626_P_DEBICMD >> 2);
		/* Write DEBI command and address to shadow RAM. */
		*rps++ = S626_DEBI_CMD_WRWORD | S626_LP_ISEL;
		*rps++ = S626_RPS_LDREG | (S626_P_DEBIAD >> 2);
		/* Write DEBI immediate data to shadow RAM. */
		*rps++ = local_ppl;
		/* Reset "shadow RAM uploaded" flag. */
		*rps++ = S626_RPS_CLRSIGNAL | S626_RPS_DEBI;
		/* Invoke shadow RAM upload. */
		*rps++ = S626_RPS_UPLOAD | S626_RPS_DEBI;
		/* Wait for shadow upload to finish. */
		*rps++ = S626_RPS_PAUSE | S626_RPS_DEBI;

		/*
		 * Delay at least 10 microseconds for analog input settling.
		 * Instead of padding with NOPs, we use S626_RPS_JUMP
		 * instructions here; this allows us to produce a longer delay
		 * than is possible with NOPs because each S626_RPS_JUMP
		 * flushes the RPS' instruction prefetch pipeline.
		 */
		jmp_adrs =
			(u32)devpriv->rps_buf.physical_base +
			(u32)((unsigned long)rps -
				   (unsigned long)devpriv->
						  rps_buf.logical_base);
		for (i = 0; i < (10 * S626_RPSCLK_PER_US / 2); i++) {
			jmp_adrs += 8;	/* Repeat to implement time delay: */
			/* Jump to next RPS instruction. */
			*rps++ = S626_RPS_JUMP;
			*rps++ = jmp_adrs;
		}

		if (cmd->convert_src != TRIG_NOW) {
			/* Wait for Start trigger. */
			*rps++ = S626_RPS_PAUSE | S626_RPS_SIGADC;
			*rps++ = S626_RPS_CLRSIGNAL | S626_RPS_SIGADC;
		}
		/* Start ADC by pulsing GPIO1. */
		/* Begin ADC Start pulse. */
		*rps++ = S626_RPS_LDREG | (S626_P_GPIO >> 2);
		*rps++ = S626_GPIO_BASE | S626_GPIO1_LO;
		*rps++ = S626_RPS_NOP;
		/* VERSION 2.03 CHANGE: STRETCH OUT ADC START PULSE. */
		/* End ADC Start pulse. */
		*rps++ = S626_RPS_LDREG | (S626_P_GPIO >> 2);
		*rps++ = S626_GPIO_BASE | S626_GPIO1_HI;
		/*
		 * Wait for ADC to complete (GPIO2 is asserted high when ADC not
		 * busy) and for data from previous conversion to shift into FB
		 * BUFFER 1 register.
		 */
		/* Wait for ADC done. */
		*rps++ = S626_RPS_PAUSE | S626_RPS_GPIO2;

		/* Transfer ADC data from FB BUFFER 1 register to DMA buffer. */
		*rps++ = S626_RPS_STREG |
			 (S626_BUGFIX_STREG(S626_P_FB_BUFFER1) >> 2);
		*rps++ = (u32)devpriv->ana_buf.physical_base +
			 (devpriv->adc_items << 2);

		/*
		 * If this slot's EndOfPollList flag is set, all channels have
		 * now been processed.
		 */
		if (*ppl++ & S626_EOPL) {
			devpriv->adc_items++; /* Adjust poll list item count. */
			break;	/* Exit poll list processing loop. */
		}
	}

	/*
	 * VERSION 2.01 CHANGE: DELAY CHANGED FROM 250NS to 2US.  Allow the
	 * ADC to stabilize for 2 microseconds before starting the final
	 * (dummy) conversion.  This delay is necessary to allow sufficient
	 * time between last conversion finished and the start of the dummy
	 * conversion.  Without this delay, the last conversion's data value
	 * is sometimes set to the previous conversion's data value.
	 */
	for (n = 0; n < (2 * S626_RPSCLK_PER_US); n++)
		*rps++ = S626_RPS_NOP;

	/*
	 * Start a dummy conversion to cause the data from the last
	 * conversion of interest to be shifted in.
	 */
	/* Begin ADC Start pulse. */
	*rps++ = S626_RPS_LDREG | (S626_P_GPIO >> 2);
	*rps++ = S626_GPIO_BASE | S626_GPIO1_LO;
	*rps++ = S626_RPS_NOP;
	/* VERSION 2.03 CHANGE: STRETCH OUT ADC START PULSE. */
	*rps++ = S626_RPS_LDREG | (S626_P_GPIO >> 2); /* End ADC Start pulse. */
	*rps++ = S626_GPIO_BASE | S626_GPIO1_HI;

	/*
	 * Wait for the data from the last conversion of interest to arrive
	 * in FB BUFFER 1 register.
	 */
	*rps++ = S626_RPS_PAUSE | S626_RPS_GPIO2;	/* Wait for ADC done. */

	/* Transfer final ADC data from FB BUFFER 1 register to DMA buffer. */
	*rps++ = S626_RPS_STREG | (S626_BUGFIX_STREG(S626_P_FB_BUFFER1) >> 2);
	*rps++ = (u32)devpriv->ana_buf.physical_base +
		 (devpriv->adc_items << 2);

	/* Indicate ADC scan loop is finished. */
	/* Signal ReadADC() that scan is done. */
	/* *rps++= S626_RPS_CLRSIGNAL | S626_RPS_SIGADC; */

	/* invoke interrupt */
	if (devpriv->ai_cmd_running == 1)
		*rps++ = S626_RPS_IRQ;

	/* Restart RPS program at its beginning. */
	*rps++ = S626_RPS_JUMP;	/* Branch to start of RPS program. */
	*rps++ = (u32)devpriv->rps_buf.physical_base;

	/* End of RPS program build */
}

static int s626_ai_eoc(struct comedi_device *dev,
		       struct comedi_subdevice *s,
		       struct comedi_insn *insn,
		       unsigned long context)
{
	unsigned int status;

	status = readl(dev->mmio + S626_P_PSR);
	if (status & S626_PSR_GPIO2)
		return 0;
	return -EBUSY;
}

static int s626_ai_insn_read(struct comedi_device *dev,
			     struct comedi_subdevice *s,
			     struct comedi_insn *insn,
			     unsigned int *data)
{
	u16 chan = CR_CHAN(insn->chanspec);
	u16 range = CR_RANGE(insn->chanspec);
	u16 adc_spec = 0;
	u32 gpio_image;
	u32 tmp;
	int ret;
	int n;

	/*
	 * Convert application's ADC specification into form
	 *  appropriate for register programming.
	 */
	if (range == 0)
		adc_spec = (chan << 8) | (S626_GSEL_BIPOLAR5V);
	else
		adc_spec = (chan << 8) | (S626_GSEL_BIPOLAR10V);

	/* Switch ADC analog gain. */
	s626_debi_write(dev, S626_LP_GSEL, adc_spec);	/* Set gain. */

	/* Select ADC analog input channel. */
	s626_debi_write(dev, S626_LP_ISEL, adc_spec);	/* Select channel. */

	for (n = 0; n < insn->n; n++) {
		/* Delay 10 microseconds for analog input settling. */
		usleep_range(10, 20);

		/* Start ADC by pulsing GPIO1 low */
		gpio_image = readl(dev->mmio + S626_P_GPIO);
		/* Assert ADC Start command */
		writel(gpio_image & ~S626_GPIO1_HI, dev->mmio + S626_P_GPIO);
		/* and stretch it out */
		writel(gpio_image & ~S626_GPIO1_HI, dev->mmio + S626_P_GPIO);
		writel(gpio_image & ~S626_GPIO1_HI, dev->mmio + S626_P_GPIO);
		/* Negate ADC Start command */
		writel(gpio_image | S626_GPIO1_HI, dev->mmio + S626_P_GPIO);

		/*
		 * Wait for ADC to complete (GPIO2 is asserted high when
		 * ADC not busy) and for data from previous conversion to
		 * shift into FB BUFFER 1 register.
		 */

		/* Wait for ADC done */
		ret = comedi_timeout(dev, s, insn, s626_ai_eoc, 0);
		if (ret)
			return ret;

		/* Fetch ADC data */
		if (n != 0) {
			tmp = readl(dev->mmio + S626_P_FB_BUFFER1);
			data[n - 1] = s626_ai_reg_to_uint(tmp);
		}

		/*
		 * Allow the ADC to stabilize for 4 microseconds before
		 * starting the next (final) conversion.  This delay is
		 * necessary to allow sufficient time between last
		 * conversion finished and the start of the next
		 * conversion.  Without this delay, the last conversion's
		 * data value is sometimes set to the previous
		 * conversion's data value.
		 */
		udelay(4);
	}

	/*
	 * Start a dummy conversion to cause the data from the
	 * previous conversion to be shifted in.
	 */
	gpio_image = readl(dev->mmio + S626_P_GPIO);
	/* Assert ADC Start command */
	writel(gpio_image & ~S626_GPIO1_HI, dev->mmio + S626_P_GPIO);
	/* and stretch it out */
	writel(gpio_image & ~S626_GPIO1_HI, dev->mmio + S626_P_GPIO);
	writel(gpio_image & ~S626_GPIO1_HI, dev->mmio + S626_P_GPIO);
	/* Negate ADC Start command */
	writel(gpio_image | S626_GPIO1_HI, dev->mmio + S626_P_GPIO);

	/* Wait for the data to arrive in FB BUFFER 1 register. */

	/* Wait for ADC done */
	ret = comedi_timeout(dev, s, insn, s626_ai_eoc, 0);
	if (ret)
		return ret;

	/* Fetch ADC data from audio interface's input shift register. */

	/* Fetch ADC data */
	if (n != 0) {
		tmp = readl(dev->mmio + S626_P_FB_BUFFER1);
		data[n - 1] = s626_ai_reg_to_uint(tmp);
	}

	return n;
}

static int s626_ai_load_polllist(u8 *ppl, struct comedi_cmd *cmd)
{
	int n;

	for (n = 0; n < cmd->chanlist_len; n++) {
		if (CR_RANGE(cmd->chanlist[n]) == 0)
			ppl[n] = CR_CHAN(cmd->chanlist[n]) | S626_RANGE_5V;
		else
			ppl[n] = CR_CHAN(cmd->chanlist[n]) | S626_RANGE_10V;
	}
	if (n != 0)
		ppl[n - 1] |= S626_EOPL;

	return n;
}

static int s626_ai_inttrig(struct comedi_device *dev,
			   struct comedi_subdevice *s,
			   unsigned int trig_num)
{
	struct comedi_cmd *cmd = &s->async->cmd;

	if (trig_num != cmd->start_arg)
		return -EINVAL;

	/* Start executing the RPS program */
	s626_mc_enable(dev, S626_MC1_ERPS1, S626_P_MC1);

	s->async->inttrig = NULL;

	return 1;
}

/*
 * This function doesn't require a particular form, this is just what
 * happens to be used in some of the drivers.  It should convert ns
 * nanoseconds to a counter value suitable for programming the device.
 * Also, it should adjust ns so that it cooresponds to the actual time
 * that the device will use.
 */
static int s626_ns_to_timer(unsigned int *nanosec, unsigned int flags)
{
	int divider, base;

	base = 500;		/* 2MHz internal clock */

	switch (flags & CMDF_ROUND_MASK) {
	case CMDF_ROUND_NEAREST:
	default:
		divider = DIV_ROUND_CLOSEST(*nanosec, base);
		break;
	case CMDF_ROUND_DOWN:
		divider = (*nanosec) / base;
		break;
	case CMDF_ROUND_UP:
		divider = DIV_ROUND_UP(*nanosec, base);
		break;
	}

	*nanosec = base * divider;
	return divider - 1;
}

static void s626_timer_load(struct comedi_device *dev,
			    unsigned int chan, int tick)
{
	u16 setup =
		/* Preload upon index. */
		S626_SET_STD_LOADSRC(S626_LOADSRC_INDX) |
		/* Disable hardware index. */
		S626_SET_STD_INDXSRC(S626_INDXSRC_SOFT) |
		/* Operating mode is Timer. */
		S626_SET_STD_ENCMODE(S626_ENCMODE_TIMER) |
		/* Count direction is Down. */
		S626_SET_STD_CLKPOL(S626_CNTDIR_DOWN) |
		/* Clock multiplier is 1x. */
		S626_SET_STD_CLKMULT(S626_CLKMULT_1X) |
		/* Enabled by index */
		S626_SET_STD_CLKENAB(S626_CLKENAB_INDEX);
	u16 value_latchsrc = S626_LATCHSRC_A_INDXA;
	/* uint16_t enab = S626_CLKENAB_ALWAYS; */

	s626_set_mode(dev, chan, setup, false);

	/* Set the preload register */
	s626_preload(dev, chan, tick);

	/*
	 * Software index pulse forces the preload register to load
	 * into the counter
	 */
	s626_set_load_trig(dev, chan, 0);
	s626_pulse_index(dev, chan);

	/* set reload on counter overflow */
	s626_set_load_trig(dev, chan, 1);

	/* set interrupt on overflow */
	s626_set_int_src(dev, chan, S626_INTSRC_OVER);

	s626_set_latch_source(dev, chan, value_latchsrc);
	/* s626_set_enable(dev, chan, (uint16_t)(enab != 0)); */
}

/* TO COMPLETE  */
static int s626_ai_cmd(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct s626_private *devpriv = dev->private;
	u8 ppl[16];
	struct comedi_cmd *cmd = &s->async->cmd;
	int tick;

	if (devpriv->ai_cmd_running) {
		dev_err(dev->class_dev,
			"s626_ai_cmd: Another ai_cmd is running\n");
		return -EBUSY;
	}
	/* disable interrupt */
	writel(0, dev->mmio + S626_P_IER);

	/* clear interrupt request */
	writel(S626_IRQ_RPS1 | S626_IRQ_GPIO3, dev->mmio + S626_P_ISR);

	/* clear any pending interrupt */
	s626_dio_clear_irq(dev);
	/* s626_enc_clear_irq(dev); */

	/* reset ai_cmd_running flag */
	devpriv->ai_cmd_running = 0;

	s626_ai_load_polllist(ppl, cmd);
	devpriv->ai_cmd_running = 1;
	devpriv->ai_convert_count = 0;

	switch (cmd->scan_begin_src) {
	case TRIG_FOLLOW:
		break;
	case TRIG_TIMER:
		/*
		 * set a counter to generate adc trigger at scan_begin_arg
		 * interval
		 */
		tick = s626_ns_to_timer(&cmd->scan_begin_arg, cmd->flags);

		/* load timer value and enable interrupt */
		s626_timer_load(dev, 5, tick);
		s626_set_enable(dev, 5, S626_CLKENAB_ALWAYS);
		break;
	case TRIG_EXT:
		/* set the digital line and interrupt for scan trigger */
		if (cmd->start_src != TRIG_EXT)
			s626_dio_set_irq(dev, cmd->scan_begin_arg);
		break;
	}

	switch (cmd->convert_src) {
	case TRIG_NOW:
		break;
	case TRIG_TIMER:
		/*
		 * set a counter to generate adc trigger at convert_arg
		 * interval
		 */
		tick = s626_ns_to_timer(&cmd->convert_arg, cmd->flags);

		/* load timer value and enable interrupt */
		s626_timer_load(dev, 4, tick);
		s626_set_enable(dev, 4, S626_CLKENAB_INDEX);
		break;
	case TRIG_EXT:
		/* set the digital line and interrupt for convert trigger */
		if (cmd->scan_begin_src != TRIG_EXT &&
		    cmd->start_src == TRIG_EXT)
			s626_dio_set_irq(dev, cmd->convert_arg);
		break;
	}

	s626_reset_adc(dev, ppl);

	switch (cmd->start_src) {
	case TRIG_NOW:
		/* Trigger ADC scan loop start */
		/* s626_mc_enable(dev, S626_MC2_ADC_RPS, S626_P_MC2); */

		/* Start executing the RPS program */
		s626_mc_enable(dev, S626_MC1_ERPS1, S626_P_MC1);
		s->async->inttrig = NULL;
		break;
	case TRIG_EXT:
		/* configure DIO channel for acquisition trigger */
		s626_dio_set_irq(dev, cmd->start_arg);
		s->async->inttrig = NULL;
		break;
	case TRIG_INT:
		s->async->inttrig = s626_ai_inttrig;
		break;
	}

	/* enable interrupt */
	writel(S626_IRQ_GPIO3 | S626_IRQ_RPS1, dev->mmio + S626_P_IER);

	return 0;
}

static int s626_ai_cmdtest(struct comedi_device *dev,
			   struct comedi_subdevice *s, struct comedi_cmd *cmd)
{
	int err = 0;
	unsigned int arg;

	/* Step 1 : check if triggers are trivially valid */

	err |= comedi_check_trigger_src(&cmd->start_src,
					TRIG_NOW | TRIG_INT | TRIG_EXT);
	err |= comedi_check_trigger_src(&cmd->scan_begin_src,
					TRIG_TIMER | TRIG_EXT | TRIG_FOLLOW);
	err |= comedi_check_trigger_src(&cmd->convert_src,
					TRIG_TIMER | TRIG_EXT | TRIG_NOW);
	err |= comedi_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	err |= comedi_check_trigger_src(&cmd->stop_src, TRIG_COUNT | TRIG_NONE);

	if (err)
		return 1;

	/* Step 2a : make sure trigger sources are unique */

	err |= comedi_check_trigger_is_unique(cmd->start_src);
	err |= comedi_check_trigger_is_unique(cmd->scan_begin_src);
	err |= comedi_check_trigger_is_unique(cmd->convert_src);
	err |= comedi_check_trigger_is_unique(cmd->stop_src);

	/* Step 2b : and mutually compatible */

	if (err)
		return 2;

	/* Step 3: check if arguments are trivially valid */

	switch (cmd->start_src) {
	case TRIG_NOW:
	case TRIG_INT:
		err |= comedi_check_trigger_arg_is(&cmd->start_arg, 0);
		break;
	case TRIG_EXT:
		err |= comedi_check_trigger_arg_max(&cmd->start_arg, 39);
		break;
	}

	if (cmd->scan_begin_src == TRIG_EXT)
		err |= comedi_check_trigger_arg_max(&cmd->scan_begin_arg, 39);
	if (cmd->convert_src == TRIG_EXT)
		err |= comedi_check_trigger_arg_max(&cmd->convert_arg, 39);

#define S626_MAX_SPEED	200000	/* in nanoseconds */
#define S626_MIN_SPEED	2000000000	/* in nanoseconds */

	if (cmd->scan_begin_src == TRIG_TIMER) {
		err |= comedi_check_trigger_arg_min(&cmd->scan_begin_arg,
						    S626_MAX_SPEED);
		err |= comedi_check_trigger_arg_max(&cmd->scan_begin_arg,
						    S626_MIN_SPEED);
	} else {
		/*
		 * external trigger
		 * should be level/edge, hi/lo specification here
		 * should specify multiple external triggers
		 * err |= comedi_check_trigger_arg_max(&cmd->scan_begin_arg, 9);
		 */
	}
	if (cmd->convert_src == TRIG_TIMER) {
		err |= comedi_check_trigger_arg_min(&cmd->convert_arg,
						    S626_MAX_SPEED);
		err |= comedi_check_trigger_arg_max(&cmd->convert_arg,
						    S626_MIN_SPEED);
	} else {
		/*
		 * external trigger - see above
		 * err |= comedi_check_trigger_arg_max(&cmd->scan_begin_arg, 9);
		 */
	}

	err |= comedi_check_trigger_arg_is(&cmd->scan_end_arg,
					   cmd->chanlist_len);

	if (cmd->stop_src == TRIG_COUNT)
		err |= comedi_check_trigger_arg_min(&cmd->stop_arg, 1);
	else	/* TRIG_NONE */
		err |= comedi_check_trigger_arg_is(&cmd->stop_arg, 0);

	if (err)
		return 3;

	/* step 4: fix up any arguments */

	if (cmd->scan_begin_src == TRIG_TIMER) {
		arg = cmd->scan_begin_arg;
		s626_ns_to_timer(&arg, cmd->flags);
		err |= comedi_check_trigger_arg_is(&cmd->scan_begin_arg, arg);
	}

	if (cmd->convert_src == TRIG_TIMER) {
		arg = cmd->convert_arg;
		s626_ns_to_timer(&arg, cmd->flags);
		err |= comedi_check_trigger_arg_is(&cmd->convert_arg, arg);

		if (cmd->scan_begin_src == TRIG_TIMER) {
			arg = cmd->convert_arg * cmd->scan_end_arg;
			err |= comedi_check_trigger_arg_min(&cmd->
							    scan_begin_arg,
							    arg);
		}
	}

	if (err)
		return 4;

	return 0;
}

static int s626_ai_cancel(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct s626_private *devpriv = dev->private;

	/* Stop RPS program in case it is currently running */
	s626_mc_disable(dev, S626_MC1_ERPS1, S626_P_MC1);

	/* disable master interrupt */
	writel(0, dev->mmio + S626_P_IER);

	devpriv->ai_cmd_running = 0;

	return 0;
}

static int s626_ao_insn_write(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn,
			      unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	int i;

	for (i = 0; i < insn->n; i++) {
		int16_t dacdata = (int16_t)data[i];
		int ret;

		dacdata -= (0x1fff);

		ret = s626_set_dac(dev, chan, dacdata);
		if (ret)
			return ret;

		s->readback[chan] = data[i];
	}

	return insn->n;
}

/* *************** DIGITAL I/O FUNCTIONS *************** */

/*
 * All DIO functions address a group of DIO channels by means of
 * "group" argument.  group may be 0, 1 or 2, which correspond to DIO
 * ports A, B and C, respectively.
 */

static void s626_dio_init(struct comedi_device *dev)
{
	u16 group;

	/* Prepare to treat writes to WRCapSel as capture disables. */
	s626_debi_write(dev, S626_LP_MISC1, S626_MISC1_NOEDCAP);

	/* For each group of sixteen channels ... */
	for (group = 0; group < S626_DIO_BANKS; group++) {
		/* Disable all interrupts */
		s626_debi_write(dev, S626_LP_WRINTSEL(group), 0);
		/* Disable all event captures */
		s626_debi_write(dev, S626_LP_WRCAPSEL(group), 0xffff);
		/* Init all DIOs to default edge polarity */
		s626_debi_write(dev, S626_LP_WREDGSEL(group), 0);
		/* Program all outputs to inactive state */
		s626_debi_write(dev, S626_LP_WRDOUT(group), 0);
	}
}

static int s626_dio_insn_bits(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn,
			      unsigned int *data)
{
	unsigned long group = (unsigned long)s->private;

	if (comedi_dio_update_state(s, data))
		s626_debi_write(dev, S626_LP_WRDOUT(group), s->state);

	data[1] = s626_debi_read(dev, S626_LP_RDDIN(group));

	return insn->n;
}

static int s626_dio_insn_config(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	unsigned long group = (unsigned long)s->private;
	int ret;

	ret = comedi_dio_insn_config(dev, s, insn, data, 0);
	if (ret)
		return ret;

	s626_debi_write(dev, S626_LP_WRDOUT(group), s->io_bits);

	return insn->n;
}

/*
 * Now this function initializes the value of the counter (data[0])
 * and set the subdevice. To complete with trigger and interrupt
 * configuration.
 *
 * FIXME: data[0] is supposed to be an INSN_CONFIG_xxx constant indicating
 * what is being configured, but this function appears to be using data[0]
 * as a variable.
 */
static int s626_enc_insn_config(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn, unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	u16 setup =
		/* Preload upon index. */
		S626_SET_STD_LOADSRC(S626_LOADSRC_INDX) |
		/* Disable hardware index. */
		S626_SET_STD_INDXSRC(S626_INDXSRC_SOFT) |
		/* Operating mode is Counter. */
		S626_SET_STD_ENCMODE(S626_ENCMODE_COUNTER) |
		/* Active high clock. */
		S626_SET_STD_CLKPOL(S626_CLKPOL_POS) |
		/* Clock multiplier is 1x. */
		S626_SET_STD_CLKMULT(S626_CLKMULT_1X) |
		/* Enabled by index */
		S626_SET_STD_CLKENAB(S626_CLKENAB_INDEX);
	/* uint16_t disable_int_src = true; */
	/* uint32_t Preloadvalue;              //Counter initial value */
	u16 value_latchsrc = S626_LATCHSRC_AB_READ;
	u16 enab = S626_CLKENAB_ALWAYS;

	/* (data==NULL) ? (Preloadvalue=0) : (Preloadvalue=data[0]); */

	s626_set_mode(dev, chan, setup, true);
	s626_preload(dev, chan, data[0]);
	s626_pulse_index(dev, chan);
	s626_set_latch_source(dev, chan, value_latchsrc);
	s626_set_enable(dev, chan, (enab != 0));

	return insn->n;
}

static int s626_enc_insn_read(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn,
			      unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	u16 cntr_latch_reg = S626_LP_CNTR(chan);
	int i;

	for (i = 0; i < insn->n; i++) {
		unsigned int val;

		/*
		 * Read the counter's output latch LSW/MSW.
		 * Latches on LSW read.
		 */
		val = s626_debi_read(dev, cntr_latch_reg);
		val |= (s626_debi_read(dev, cntr_latch_reg + 2) << 16);
		data[i] = val;
	}

	return insn->n;
}

static int s626_enc_insn_write(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn, unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);

	/* Set the preload register */
	s626_preload(dev, chan, data[0]);

	/*
	 * Software index pulse forces the preload register to load
	 * into the counter
	 */
	s626_set_load_trig(dev, chan, 0);
	s626_pulse_index(dev, chan);
	s626_set_load_trig(dev, chan, 2);

	return 1;
}

static void s626_write_misc2(struct comedi_device *dev, u16 new_image)
{
	s626_debi_write(dev, S626_LP_MISC1, S626_MISC1_WENABLE);
	s626_debi_write(dev, S626_LP_WRMISC2, new_image);
	s626_debi_write(dev, S626_LP_MISC1, S626_MISC1_WDISABLE);
}

static void s626_counters_init(struct comedi_device *dev)
{
	int chan;
	u16 setup =
		/* Preload upon index. */
		S626_SET_STD_LOADSRC(S626_LOADSRC_INDX) |
		/* Disable hardware index. */
		S626_SET_STD_INDXSRC(S626_INDXSRC_SOFT) |
		/* Operating mode is counter. */
		S626_SET_STD_ENCMODE(S626_ENCMODE_COUNTER) |
		/* Active high clock. */
		S626_SET_STD_CLKPOL(S626_CLKPOL_POS) |
		/* Clock multiplier is 1x. */
		S626_SET_STD_CLKMULT(S626_CLKMULT_1X) |
		/* Enabled by index */
		S626_SET_STD_CLKENAB(S626_CLKENAB_INDEX);

	/*
	 * Disable all counter interrupts and clear any captured counter events.
	 */
	for (chan = 0; chan < S626_ENCODER_CHANNELS; chan++) {
		s626_set_mode(dev, chan, setup, true);
		s626_set_int_src(dev, chan, 0);
		s626_reset_cap_flags(dev, chan);
		s626_set_enable(dev, chan, S626_CLKENAB_ALWAYS);
	}
}

static int s626_allocate_dma_buffers(struct comedi_device *dev)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	struct s626_private *devpriv = dev->private;
	void *addr;
	dma_addr_t appdma;

	addr = pci_alloc_consistent(pcidev, S626_DMABUF_SIZE, &appdma);
	if (!addr)
		return -ENOMEM;
	devpriv->ana_buf.logical_base = addr;
	devpriv->ana_buf.physical_base = appdma;

	addr = pci_alloc_consistent(pcidev, S626_DMABUF_SIZE, &appdma);
	if (!addr)
		return -ENOMEM;
	devpriv->rps_buf.logical_base = addr;
	devpriv->rps_buf.physical_base = appdma;

	return 0;
}

static void s626_free_dma_buffers(struct comedi_device *dev)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	struct s626_private *devpriv = dev->private;

	if (!devpriv)
		return;

	if (devpriv->rps_buf.logical_base)
		pci_free_consistent(pcidev, S626_DMABUF_SIZE,
				    devpriv->rps_buf.logical_base,
				    devpriv->rps_buf.physical_base);
	if (devpriv->ana_buf.logical_base)
		pci_free_consistent(pcidev, S626_DMABUF_SIZE,
				    devpriv->ana_buf.logical_base,
				    devpriv->ana_buf.physical_base);
}

static int s626_initialize(struct comedi_device *dev)
{
	struct s626_private *devpriv = dev->private;
	dma_addr_t phys_buf;
	u16 chan;
	int i;
	int ret;

	/* Enable DEBI and audio pins, enable I2C interface */
	s626_mc_enable(dev, S626_MC1_DEBI | S626_MC1_AUDIO | S626_MC1_I2C,
		       S626_P_MC1);

	/*
	 * Configure DEBI operating mode
	 *
	 *  Local bus is 16 bits wide
	 *  Declare DEBI transfer timeout interval
	 *  Set up byte lane steering
	 *  Intel-compatible local bus (DEBI never times out)
	 */
	writel(S626_DEBI_CFG_SLAVE16 |
	       (S626_DEBI_TOUT << S626_DEBI_CFG_TOUT_BIT) | S626_DEBI_SWAP |
	       S626_DEBI_CFG_INTEL, dev->mmio + S626_P_DEBICFG);

	/* Disable MMU paging */
	writel(S626_DEBI_PAGE_DISABLE, dev->mmio + S626_P_DEBIPAGE);

	/* Init GPIO so that ADC Start* is negated */
	writel(S626_GPIO_BASE | S626_GPIO1_HI, dev->mmio + S626_P_GPIO);

	/* I2C device address for onboard eeprom (revb) */
	devpriv->i2c_adrs = 0xA0;

	/*
	 * Issue an I2C ABORT command to halt any I2C
	 * operation in progress and reset BUSY flag.
	 */
	writel(S626_I2C_CLKSEL | S626_I2C_ABORT,
	       dev->mmio + S626_P_I2CSTAT);
	s626_mc_enable(dev, S626_MC2_UPLD_IIC, S626_P_MC2);
	ret = comedi_timeout(dev, NULL, NULL, s626_i2c_handshake_eoc, 0);
	if (ret)
		return ret;

	/*
	 * Per SAA7146 data sheet, write to STATUS
	 * reg twice to reset all  I2C error flags.
	 */
	for (i = 0; i < 2; i++) {
		writel(S626_I2C_CLKSEL, dev->mmio + S626_P_I2CSTAT);
		s626_mc_enable(dev, S626_MC2_UPLD_IIC, S626_P_MC2);
		ret = comedi_timeout(dev, NULL,
				     NULL, s626_i2c_handshake_eoc, 0);
		if (ret)
			return ret;
	}

	/*
	 * Init audio interface functional attributes: set DAC/ADC
	 * serial clock rates, invert DAC serial clock so that
	 * DAC data setup times are satisfied, enable DAC serial
	 * clock out.
	 */
	writel(S626_ACON2_INIT, dev->mmio + S626_P_ACON2);

	/*
	 * Set up TSL1 slot list, which is used to control the
	 * accumulation of ADC data: S626_RSD1 = shift data in on SD1.
	 * S626_SIB_A1  = store data uint8_t at next available location
	 * in FB BUFFER1 register.
	 */
	writel(S626_RSD1 | S626_SIB_A1, dev->mmio + S626_P_TSL1);
	writel(S626_RSD1 | S626_SIB_A1 | S626_EOS,
	       dev->mmio + S626_P_TSL1 + 4);

	/* Enable TSL1 slot list so that it executes all the time */
	writel(S626_ACON1_ADCSTART, dev->mmio + S626_P_ACON1);

	/*
	 * Initialize RPS registers used for ADC
	 */

	/* Physical start of RPS program */
	writel((u32)devpriv->rps_buf.physical_base,
	       dev->mmio + S626_P_RPSADDR1);
	/* RPS program performs no explicit mem writes */
	writel(0, dev->mmio + S626_P_RPSPAGE1);
	/* Disable RPS timeouts */
	writel(0, dev->mmio + S626_P_RPS1_TOUT);

#if 0
	/*
	 * SAA7146 BUG WORKAROUND
	 *
	 * Initialize SAA7146 ADC interface to a known state by
	 * invoking ADCs until FB BUFFER 1 register shows that it
	 * is correctly receiving ADC data. This is necessary
	 * because the SAA7146 ADC interface does not start up in
	 * a defined state after a PCI reset.
	 */
	{
		struct comedi_subdevice *s = dev->read_subdev;
		uint8_t poll_list;
		uint16_t adc_data;
		uint16_t start_val;
		uint16_t index;
		unsigned int data[16];

		/* Create a simple polling list for analog input channel 0 */
		poll_list = S626_EOPL;
		s626_reset_adc(dev, &poll_list);

		/* Get initial ADC value */
		s626_ai_rinsn(dev, s, NULL, data);
		start_val = data[0];

		/*
		 * VERSION 2.01 CHANGE: TIMEOUT ADDED TO PREVENT HANGED
		 * EXECUTION.
		 *
		 * Invoke ADCs until the new ADC value differs from the initial
		 * value or a timeout occurs.  The timeout protects against the
		 * possibility that the driver is restarting and the ADC data is
		 * a fixed value resulting from the applied ADC analog input
		 * being unusually quiet or at the rail.
		 */
		for (index = 0; index < 500; index++) {
			s626_ai_rinsn(dev, s, NULL, data);
			adc_data = data[0];
			if (adc_data != start_val)
				break;
		}
	}
#endif	/* SAA7146 BUG WORKAROUND */

	/*
	 * Initialize the DAC interface
	 */

	/*
	 * Init Audio2's output DMAC attributes:
	 *   burst length = 1 DWORD
	 *   threshold = 1 DWORD.
	 */
	writel(0, dev->mmio + S626_P_PCI_BT_A);

	/*
	 * Init Audio2's output DMA physical addresses.  The protection
	 * address is set to 1 DWORD past the base address so that a
	 * single DWORD will be transferred each time a DMA transfer is
	 * enabled.
	 */
	phys_buf = devpriv->ana_buf.physical_base +
		   (S626_DAC_WDMABUF_OS * sizeof(u32));
	writel((u32)phys_buf, dev->mmio + S626_P_BASEA2_OUT);
	writel((u32)(phys_buf + sizeof(u32)),
	       dev->mmio + S626_P_PROTA2_OUT);

	/*
	 * Cache Audio2's output DMA buffer logical address.  This is
	 * where DAC data is buffered for A2 output DMA transfers.
	 */
	devpriv->dac_wbuf = (u32 *)devpriv->ana_buf.logical_base +
			    S626_DAC_WDMABUF_OS;

	/*
	 * Audio2's output channels does not use paging.  The
	 * protection violation handling bit is set so that the
	 * DMAC will automatically halt and its PCI address pointer
	 * will be reset when the protection address is reached.
	 */
	writel(8, dev->mmio + S626_P_PAGEA2_OUT);

	/*
	 * Initialize time slot list 2 (TSL2), which is used to control
	 * the clock generation for and serialization of data to be sent
	 * to the DAC devices.  Slot 0 is a NOP that is used to trap TSL
	 * execution; this permits other slots to be safely modified
	 * without first turning off the TSL sequencer (which is
	 * apparently impossible to do).  Also, SD3 (which is driven by a
	 * pull-up resistor) is shifted in and stored to the MSB of
	 * FB_BUFFER2 to be used as evidence that the slot sequence has
	 * not yet finished executing.
	 */

	/* Slot 0: Trap TSL execution, shift 0xFF into FB_BUFFER2 */
	writel(S626_XSD2 | S626_RSD3 | S626_SIB_A2 | S626_EOS,
	       dev->mmio + S626_VECTPORT(0));

	/*
	 * Initialize slot 1, which is constant.  Slot 1 causes a
	 * DWORD to be transferred from audio channel 2's output FIFO
	 * to the FIFO's output buffer so that it can be serialized
	 * and sent to the DAC during subsequent slots.  All remaining
	 * slots are dynamically populated as required by the target
	 * DAC device.
	 */

	/* Slot 1: Fetch DWORD from Audio2's output FIFO */
	writel(S626_LF_A2, dev->mmio + S626_VECTPORT(1));

	/* Start DAC's audio interface (TSL2) running */
	writel(S626_ACON1_DACSTART, dev->mmio + S626_P_ACON1);

	/*
	 * Init Trim DACs to calibrated values.  Do it twice because the
	 * SAA7146 audio channel does not always reset properly and
	 * sometimes causes the first few TrimDAC writes to malfunction.
	 */
	s626_load_trim_dacs(dev);
	ret = s626_load_trim_dacs(dev);
	if (ret)
		return ret;

	/*
	 * Manually init all gate array hardware in case this is a soft
	 * reset (we have no way of determining whether this is a warm
	 * or cold start).  This is necessary because the gate array will
	 * reset only in response to a PCI hard reset; there is no soft
	 * reset function.
	 */

	/*
	 * Init all DAC outputs to 0V and init all DAC setpoint and
	 * polarity images.
	 */
	for (chan = 0; chan < S626_DAC_CHANNELS; chan++) {
		ret = s626_set_dac(dev, chan, 0);
		if (ret)
			return ret;
	}

	/* Init counters */
	s626_counters_init(dev);

	/*
	 * Without modifying the state of the Battery Backup enab, disable
	 * the watchdog timer, set DIO channels 0-5 to operate in the
	 * standard DIO (vs. counter overflow) mode, disable the battery
	 * charger, and reset the watchdog interval selector to zero.
	 */
	s626_write_misc2(dev, (s626_debi_read(dev, S626_LP_RDMISC2) &
			       S626_MISC2_BATT_ENABLE));

	/* Initialize the digital I/O subsystem */
	s626_dio_init(dev);

	return 0;
}

static int s626_auto_attach(struct comedi_device *dev,
			    unsigned long context_unused)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	struct s626_private *devpriv;
	struct comedi_subdevice *s;
	int ret;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	ret = comedi_pci_enable(dev);
	if (ret)
		return ret;

	dev->mmio = pci_ioremap_bar(pcidev, 0);
	if (!dev->mmio)
		return -ENOMEM;

	/* disable master interrupt */
	writel(0, dev->mmio + S626_P_IER);

	/* soft reset */
	writel(S626_MC1_SOFT_RESET, dev->mmio + S626_P_MC1);

	/* DMA FIXME DMA// */

	ret = s626_allocate_dma_buffers(dev);
	if (ret)
		return ret;

	if (pcidev->irq) {
		ret = request_irq(pcidev->irq, s626_irq_handler, IRQF_SHARED,
				  dev->board_name, dev);

		if (ret == 0)
			dev->irq = pcidev->irq;
	}

	ret = comedi_alloc_subdevices(dev, 6);
	if (ret)
		return ret;

	s = &dev->subdevices[0];
	/* analog input subdevice */
	s->type		= COMEDI_SUBD_AI;
	s->subdev_flags	= SDF_READABLE | SDF_DIFF;
	s->n_chan	= S626_ADC_CHANNELS;
	s->maxdata	= 0x3fff;
	s->range_table	= &s626_range_table;
	s->len_chanlist	= S626_ADC_CHANNELS;
	s->insn_read	= s626_ai_insn_read;
	if (dev->irq) {
		dev->read_subdev = s;
		s->subdev_flags	|= SDF_CMD_READ;
		s->do_cmd	= s626_ai_cmd;
		s->do_cmdtest	= s626_ai_cmdtest;
		s->cancel	= s626_ai_cancel;
	}

	s = &dev->subdevices[1];
	/* analog output subdevice */
	s->type		= COMEDI_SUBD_AO;
	s->subdev_flags	= SDF_WRITABLE | SDF_READABLE;
	s->n_chan	= S626_DAC_CHANNELS;
	s->maxdata	= 0x3fff;
	s->range_table	= &range_bipolar10;
	s->insn_write	= s626_ao_insn_write;

	ret = comedi_alloc_subdev_readback(s);
	if (ret)
		return ret;

	s = &dev->subdevices[2];
	/* digital I/O subdevice */
	s->type		= COMEDI_SUBD_DIO;
	s->subdev_flags	= SDF_WRITABLE | SDF_READABLE;
	s->n_chan	= 16;
	s->maxdata	= 1;
	s->io_bits	= 0xffff;
	s->private	= (void *)0;	/* DIO group 0 */
	s->range_table	= &range_digital;
	s->insn_config	= s626_dio_insn_config;
	s->insn_bits	= s626_dio_insn_bits;

	s = &dev->subdevices[3];
	/* digital I/O subdevice */
	s->type		= COMEDI_SUBD_DIO;
	s->subdev_flags	= SDF_WRITABLE | SDF_READABLE;
	s->n_chan	= 16;
	s->maxdata	= 1;
	s->io_bits	= 0xffff;
	s->private	= (void *)1;	/* DIO group 1 */
	s->range_table	= &range_digital;
	s->insn_config	= s626_dio_insn_config;
	s->insn_bits	= s626_dio_insn_bits;

	s = &dev->subdevices[4];
	/* digital I/O subdevice */
	s->type		= COMEDI_SUBD_DIO;
	s->subdev_flags	= SDF_WRITABLE | SDF_READABLE;
	s->n_chan	= 16;
	s->maxdata	= 1;
	s->io_bits	= 0xffff;
	s->private	= (void *)2;	/* DIO group 2 */
	s->range_table	= &range_digital;
	s->insn_config	= s626_dio_insn_config;
	s->insn_bits	= s626_dio_insn_bits;

	s = &dev->subdevices[5];
	/* encoder (counter) subdevice */
	s->type		= COMEDI_SUBD_COUNTER;
	s->subdev_flags	= SDF_WRITABLE | SDF_READABLE | SDF_LSAMPL;
	s->n_chan	= S626_ENCODER_CHANNELS;
	s->maxdata	= 0xffffff;
	s->range_table	= &range_unknown;
	s->insn_config	= s626_enc_insn_config;
	s->insn_read	= s626_enc_insn_read;
	s->insn_write	= s626_enc_insn_write;

	return s626_initialize(dev);
}

static void s626_detach(struct comedi_device *dev)
{
	struct s626_private *devpriv = dev->private;

	if (devpriv) {
		/* stop ai_command */
		devpriv->ai_cmd_running = 0;

		if (dev->mmio) {
			/* interrupt mask */
			/* Disable master interrupt */
			writel(0, dev->mmio + S626_P_IER);
			/* Clear board's IRQ status flag */
			writel(S626_IRQ_GPIO3 | S626_IRQ_RPS1,
			       dev->mmio + S626_P_ISR);

			/* Disable the watchdog timer and battery charger. */
			s626_write_misc2(dev, 0);

			/* Close all interfaces on 7146 device */
			writel(S626_MC1_SHUTDOWN, dev->mmio + S626_P_MC1);
			writel(S626_ACON1_BASE, dev->mmio + S626_P_ACON1);
		}
	}
	comedi_pci_detach(dev);
	s626_free_dma_buffers(dev);
}

static struct comedi_driver s626_driver = {
	.driver_name	= "s626",
	.module		= THIS_MODULE,
	.auto_attach	= s626_auto_attach,
	.detach		= s626_detach,
};

static int s626_pci_probe(struct pci_dev *dev,
			  const struct pci_device_id *id)
{
	return comedi_pci_auto_config(dev, &s626_driver, id->driver_data);
}

/*
 * For devices with vendor:device id == 0x1131:0x7146 you must specify
 * also subvendor:subdevice ids, because otherwise it will conflict with
 * Philips SAA7146 media/dvb based cards.
 */
static const struct pci_device_id s626_pci_table[] = {
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_PHILIPS, PCI_DEVICE_ID_PHILIPS_SAA7146,
			 0x6000, 0x0272) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, s626_pci_table);

static struct pci_driver s626_pci_driver = {
	.name		= "s626",
	.id_table	= s626_pci_table,
	.probe		= s626_pci_probe,
	.remove		= comedi_pci_auto_unconfig,
};
module_comedi_pci_driver(s626_driver, s626_pci_driver);

MODULE_AUTHOR("Gianluca Palli <gpalli@deis.unibo.it>");
MODULE_DESCRIPTION("Sensoray 626 Comedi driver module");
MODULE_LICENSE("GPL");

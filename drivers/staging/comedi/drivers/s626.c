/*
  comedi/drivers/s626.c
  Sensoray s626 Comedi driver

  COMEDI - Linux Control and Measurement Device Interface
  Copyright (C) 2000 David A. Schleef <ds@schleef.org>

  Based on Sensoray Model 626 Linux driver Version 0.2
  Copyright (C) 2002-2004 Sensoray Co., Inc.

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

/*
Driver: s626
Description: Sensoray 626 driver
Devices: [Sensoray] 626 (s626)
Authors: Gianluca Palli <gpalli@deis.unibo.it>,
Updated: Fri, 15 Feb 2008 10:28:42 +0000
Status: experimental

Configuration options: not applicable, uses PCI auto config

INSN_CONFIG instructions:
  analog input:
   none

  analog output:
   none

  digital channel:
   s626 has 3 dio subdevices (2,3 and 4) each with 16 i/o channels
   supported configuration options:
   INSN_CONFIG_DIO_QUERY
   COMEDI_INPUT
   COMEDI_OUTPUT

  encoder:
   Every channel must be configured before reading.

   Example code

   insn.insn=INSN_CONFIG;   //configuration instruction
   insn.n=1;                //number of operation (must be 1)
   insn.data=&initialvalue; //initial value loaded into encoder
				//during configuration
   insn.subdev=5;           //encoder subdevice
   insn.chanspec=CR_PACK(encoder_channel,0,AREF_OTHER); //encoder_channel
							//to configure

   comedi_do_insn(cf,&insn); //executing configuration
*/

#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/types.h>

#include "../comedidev.h"

#include "comedi_fc.h"
#include "s626.h"

#define PCI_VENDOR_ID_S626 0x1131
#define PCI_DEVICE_ID_S626 0x7146
#define PCI_SUBVENDOR_ID_S626 0x6000
#define PCI_SUBDEVICE_ID_S626 0x0272

struct s626_private {
	void __iomem *base_addr;
	uint8_t ai_cmd_running;	/*  ai_cmd is running */
	uint8_t ai_continous;	/*  continous acquisition */
	int ai_sample_count;	/*  number of samples to acquire */
	unsigned int ai_sample_timer;
	/*  time between samples in  units of the timer */
	int ai_convert_count;	/*  conversion counter */
	unsigned int ai_convert_timer;
	/*  time between conversion in  units of the timer */
	uint16_t CounterIntEnabs;
	/* Counter interrupt enable  mask for MISC2 register. */
	uint8_t AdcItems;	/* Number of items in ADC poll  list. */
	struct bufferDMA RPSBuf;	/* DMA buffer used to hold ADC (RPS1) program. */
	struct bufferDMA ANABuf;
	/* DMA buffer used to receive ADC data and hold DAC data. */
	uint32_t *pDacWBuf;
	/* Pointer to logical adrs of DMA buffer used to hold DAC  data. */
	uint16_t Dacpol;	/* Image of DAC polarity register. */
	uint8_t TrimSetpoint[12];	/* Images of TrimDAC setpoints */
	/* Charge Enabled (0 or WRMISC2_CHARGE_ENABLE). */
	uint32_t I2CAdrs;
	/* I2C device address for onboard EEPROM (board rev dependent). */
	/*   short         I2Cards; */
	unsigned int ao_readback[S626_DAC_CHANNELS];
};

struct dio_private {
	uint16_t RDDIn;
	uint16_t WRDOut;
	uint16_t RDEdgSel;
	uint16_t WREdgSel;
	uint16_t RDCapSel;
	uint16_t WRCapSel;
	uint16_t RDCapFlg;
	uint16_t RDIntSel;
	uint16_t WRIntSel;
};

static struct dio_private dio_private_A = {
	.RDDIn = LP_RDDINA,
	.WRDOut = LP_WRDOUTA,
	.RDEdgSel = LP_RDEDGSELA,
	.WREdgSel = LP_WREDGSELA,
	.RDCapSel = LP_RDCAPSELA,
	.WRCapSel = LP_WRCAPSELA,
	.RDCapFlg = LP_RDCAPFLGA,
	.RDIntSel = LP_RDINTSELA,
	.WRIntSel = LP_WRINTSELA,
};

static struct dio_private dio_private_B = {
	.RDDIn = LP_RDDINB,
	.WRDOut = LP_WRDOUTB,
	.RDEdgSel = LP_RDEDGSELB,
	.WREdgSel = LP_WREDGSELB,
	.RDCapSel = LP_RDCAPSELB,
	.WRCapSel = LP_WRCAPSELB,
	.RDCapFlg = LP_RDCAPFLGB,
	.RDIntSel = LP_RDINTSELB,
	.WRIntSel = LP_WRINTSELB,
};

static struct dio_private dio_private_C = {
	.RDDIn = LP_RDDINC,
	.WRDOut = LP_WRDOUTC,
	.RDEdgSel = LP_RDEDGSELC,
	.WREdgSel = LP_WREDGSELC,
	.RDCapSel = LP_RDCAPSELC,
	.WRCapSel = LP_WRCAPSELC,
	.RDCapFlg = LP_RDCAPFLGC,
	.RDIntSel = LP_RDINTSELC,
	.WRIntSel = LP_WRINTSELC,
};

/* to group dio devices (48 bits mask and data are not allowed ???)
static struct dio_private *dio_private_word[]={
  &dio_private_A,
  &dio_private_B,
  &dio_private_C,
};
*/

#define diopriv ((struct dio_private *)s->private)

/*  COUNTER OBJECT ------------------------------------------------ */
struct enc_private {
	/*  Pointers to functions that differ for A and B counters: */
	uint16_t(*GetEnable) (struct comedi_device *dev, struct enc_private *);	/* Return clock enable. */
	uint16_t(*GetIntSrc) (struct comedi_device *dev, struct enc_private *);	/* Return interrupt source. */
	uint16_t(*GetLoadTrig) (struct comedi_device *dev, struct enc_private *);	/* Return preload trigger source. */
	uint16_t(*GetMode) (struct comedi_device *dev, struct enc_private *);	/* Return standardized operating mode. */
	void (*PulseIndex) (struct comedi_device *dev, struct enc_private *);	/* Generate soft index strobe. */
	void (*SetEnable) (struct comedi_device *dev, struct enc_private *, uint16_t enab);	/* Program clock enable. */
	void (*SetIntSrc) (struct comedi_device *dev, struct enc_private *, uint16_t IntSource);	/* Program interrupt source. */
	void (*SetLoadTrig) (struct comedi_device *dev, struct enc_private *, uint16_t Trig);	/* Program preload trigger source. */
	void (*SetMode) (struct comedi_device *dev, struct enc_private *, uint16_t Setup, uint16_t DisableIntSrc);	/* Program standardized operating mode. */
	void (*ResetCapFlags) (struct comedi_device *dev, struct enc_private *);	/* Reset event capture flags. */

	uint16_t MyCRA;		/*    Address of CRA register. */
	uint16_t MyCRB;		/*    Address of CRB register. */
	uint16_t MyLatchLsw;	/*    Address of Latch least-significant-word */
	/*    register. */
	uint16_t MyEventBits[4];	/*    Bit translations for IntSrc -->RDMISC2. */
};

#define encpriv ((struct enc_private *)(dev->subdevices+5)->private)

/*  Counter overflow/index event flag masks for RDMISC2. */
#define INDXMASK(C)		(1 << (((C) > 2) ? ((C) * 2 - 1) : ((C) * 2 +  4)))
#define OVERMASK(C)		(1 << (((C) > 2) ? ((C) * 2 + 5) : ((C) * 2 + 10)))
#define EVBITS(C)		{ 0, OVERMASK(C), INDXMASK(C), OVERMASK(C) | INDXMASK(C) }

/*  Translation table to map IntSrc into equivalent RDMISC2 event flag  bits. */
/* static const uint16_t EventBits[][4] = { EVBITS(0), EVBITS(1), EVBITS(2), EVBITS(3), EVBITS(4), EVBITS(5) }; */

/*  enab/disable a function or test status bit(s) that are accessed */
/*  through Main Control Registers 1 or 2. */
#define MC_ENABLE(REGADRS, CTRLWORD)	writel(((uint32_t)(CTRLWORD) << 16) | (uint32_t)(CTRLWORD), devpriv->base_addr+(REGADRS))

#define MC_DISABLE(REGADRS, CTRLWORD)	writel((uint32_t)(CTRLWORD) << 16 , devpriv->base_addr+(REGADRS))

#define MC_TEST(REGADRS, CTRLWORD)	((readl(devpriv->base_addr+(REGADRS)) & CTRLWORD) != 0)

/* #define WR7146(REGARDS,CTRLWORD)
    writel(CTRLWORD,(uint32_t)(devpriv->base_addr+(REGARDS))) */
#define WR7146(REGARDS, CTRLWORD) writel(CTRLWORD, devpriv->base_addr+(REGARDS))

/* #define RR7146(REGARDS)
    readl((uint32_t)(devpriv->base_addr+(REGARDS))) */
#define RR7146(REGARDS)		readl(devpriv->base_addr+(REGARDS))

#define BUGFIX_STREG(REGADRS)   (REGADRS - 4)

/*  Write a time slot control record to TSL2. */
#define VECTPORT(VECTNUM)		(P_TSL2 + ((VECTNUM) << 2))
#define SETVECT(VECTNUM, VECTVAL)	WR7146(VECTPORT(VECTNUM), (VECTVAL))

/*  Code macros used for constructing I2C command bytes. */
#define I2C_B2(ATTR, VAL)	(((ATTR) << 6) | ((VAL) << 24))
#define I2C_B1(ATTR, VAL)	(((ATTR) << 4) | ((VAL) << 16))
#define I2C_B0(ATTR, VAL)	(((ATTR) << 2) | ((VAL) <<  8))

static const struct comedi_lrange s626_range_table = { 2, {
							   RANGE(-5, 5),
							   RANGE(-10, 10),
							   }
};

/*  Execute a DEBI transfer.  This must be called from within a */
/*  critical section. */
static void DEBItransfer(struct comedi_device *dev)
{
	struct s626_private *devpriv = dev->private;

	/*  Initiate upload of shadow RAM to DEBI control register. */
	MC_ENABLE(P_MC2, MC2_UPLD_DEBI);

	/*  Wait for completion of upload from shadow RAM to DEBI control */
	/*  register. */
	while (!MC_TEST(P_MC2, MC2_UPLD_DEBI))
		;

	/*  Wait until DEBI transfer is done. */
	while (RR7146(P_PSR) & PSR_DEBI_S)
		;
}

/*  Initialize the DEBI interface for all transfers. */

static uint16_t DEBIread(struct comedi_device *dev, uint16_t addr)
{
	struct s626_private *devpriv = dev->private;
	uint16_t retval;

	/*  Set up DEBI control register value in shadow RAM. */
	WR7146(P_DEBICMD, DEBI_CMD_RDWORD | addr);

	/*  Execute the DEBI transfer. */
	DEBItransfer(dev);

	/*  Fetch target register value. */
	retval = (uint16_t) RR7146(P_DEBIAD);

	/*  Return register value. */
	return retval;
}

/*  Write a value to a gate array register. */
static void DEBIwrite(struct comedi_device *dev, uint16_t addr, uint16_t wdata)
{
	struct s626_private *devpriv = dev->private;

	/*  Set up DEBI control register value in shadow RAM. */
	WR7146(P_DEBICMD, DEBI_CMD_WRWORD | addr);
	WR7146(P_DEBIAD, wdata);

	/*  Execute the DEBI transfer. */
	DEBItransfer(dev);
}

/* Replace the specified bits in a gate array register.  Imports: mask
 * specifies bits that are to be preserved, wdata is new value to be
 * or'd with the masked original.
 */
static void DEBIreplace(struct comedi_device *dev, uint16_t addr, uint16_t mask,
			uint16_t wdata)
{
	struct s626_private *devpriv = dev->private;

	/*  Copy target gate array register into P_DEBIAD register. */
	WR7146(P_DEBICMD, DEBI_CMD_RDWORD | addr);
	/* Set up DEBI control reg value in shadow RAM. */
	DEBItransfer(dev);	/*  Execute the DEBI Read transfer. */

	/*  Write back the modified image. */
	WR7146(P_DEBICMD, DEBI_CMD_WRWORD | addr);
	/* Set up DEBI control reg value in shadow  RAM. */

	WR7146(P_DEBIAD, wdata | ((uint16_t) RR7146(P_DEBIAD) & mask));
	/* Modify the register image. */
	DEBItransfer(dev);	/*  Execute the DEBI Write transfer. */
}

/* **************  EEPROM ACCESS FUNCTIONS  ************** */

static uint32_t I2Chandshake(struct comedi_device *dev, uint32_t val)
{
	struct s626_private *devpriv = dev->private;

	/*  Write I2C command to I2C Transfer Control shadow register. */
	WR7146(P_I2CCTRL, val);

	/*  Upload I2C shadow registers into working registers and wait for */
	/*  upload confirmation. */

	MC_ENABLE(P_MC2, MC2_UPLD_IIC);
	while (!MC_TEST(P_MC2, MC2_UPLD_IIC))
		;

	/*  Wait until I2C bus transfer is finished or an error occurs. */
	while ((RR7146(P_I2CCTRL) & (I2C_BUSY | I2C_ERR)) == I2C_BUSY)
		;

	/*  Return non-zero if I2C error occurred. */
	return RR7146(P_I2CCTRL) & I2C_ERR;

}

/*  Read uint8_t from EEPROM. */
static uint8_t I2Cread(struct comedi_device *dev, uint8_t addr)
{
	struct s626_private *devpriv = dev->private;
	uint8_t rtnval;

	/*  Send EEPROM target address. */
	if (I2Chandshake(dev, I2C_B2(I2C_ATTRSTART, I2CW)
			 /* Byte2 = I2C command: write to I2C EEPROM  device. */
			 | I2C_B1(I2C_ATTRSTOP, addr)
			 /* Byte1 = EEPROM internal target address. */
			 | I2C_B0(I2C_ATTRNOP, 0))) {	/*  Byte0 = Not sent. */
		/*  Abort function and declare error if handshake failed. */
		return 0;
	}
	/*  Execute EEPROM read. */
	if (I2Chandshake(dev, I2C_B2(I2C_ATTRSTART, I2CR)

			 /*  Byte2 = I2C */
			 /*  command: read */
			 /*  from I2C EEPROM */
			 /*  device. */
			 |I2C_B1(I2C_ATTRSTOP, 0)

			 /*  Byte1 receives */
			 /*  uint8_t from */
			 /*  EEPROM. */
			 |I2C_B0(I2C_ATTRNOP, 0))) {	/*  Byte0 = Not  sent. */

		/*  Abort function and declare error if handshake failed. */
		return 0;
	}
	/*  Return copy of EEPROM value. */
	rtnval = (uint8_t) (RR7146(P_I2CCTRL) >> 16);
	return rtnval;
}

/* ***********  DAC FUNCTIONS *********** */

/*  Slot 0 base settings. */
#define VECT0	(XSD2 | RSD3 | SIB_A2)
/*  Slot 0 always shifts in  0xFF and store it to  FB_BUFFER2. */

/*  TrimDac LogicalChan-to-PhysicalChan mapping table. */
static uint8_t trimchan[] = { 10, 9, 8, 3, 2, 7, 6, 1, 0, 5, 4 };

/*  TrimDac LogicalChan-to-EepromAdrs mapping table. */
static uint8_t trimadrs[] = { 0x40, 0x41, 0x42, 0x50, 0x51, 0x52, 0x53, 0x60, 0x61, 0x62, 0x63 };

/* Private helper function: Transmit serial data to DAC via Audio
 * channel 2.  Assumes: (1) TSL2 slot records initialized, and (2)
 * Dacpol contains valid target image.
 */
static void SendDAC(struct comedi_device *dev, uint32_t val)
{
	struct s626_private *devpriv = dev->private;

	/* START THE SERIAL CLOCK RUNNING ------------- */

	/* Assert DAC polarity control and enable gating of DAC serial clock
	 * and audio bit stream signals.  At this point in time we must be
	 * assured of being in time slot 0.  If we are not in slot 0, the
	 * serial clock and audio stream signals will be disabled; this is
	 * because the following DEBIwrite statement (which enables signals
	 * to be passed through the gate array) would execute before the
	 * trailing edge of WS1/WS3 (which turns off the signals), thus
	 * causing the signals to be inactive during the DAC write.
	 */
	DEBIwrite(dev, LP_DACPOL, devpriv->Dacpol);

	/* TRANSFER OUTPUT DWORD VALUE INTO A2'S OUTPUT FIFO ---------------- */

	/* Copy DAC setpoint value to DAC's output DMA buffer. */

	/* WR7146( (uint32_t)devpriv->pDacWBuf, val ); */
	*devpriv->pDacWBuf = val;

	/* enab the output DMA transfer.  This will cause the DMAC to copy
	 * the DAC's data value to A2's output FIFO.  The DMA transfer will
	 * then immediately terminate because the protection address is
	 * reached upon transfer of the first DWORD value.
	 */
	MC_ENABLE(P_MC1, MC1_A2OUT);

	/*  While the DMA transfer is executing ... */

	/* Reset Audio2 output FIFO's underflow flag (along with any other
	 * FIFO underflow/overflow flags).  When set, this flag will
	 * indicate that we have emerged from slot 0.
	 */
	WR7146(P_ISR, ISR_AFOU);

	/* Wait for the DMA transfer to finish so that there will be data
	 * available in the FIFO when time slot 1 tries to transfer a DWORD
	 * from the FIFO to the output buffer register.  We test for DMA
	 * Done by polling the DMAC enable flag; this flag is automatically
	 * cleared when the transfer has finished.
	 */
	while ((RR7146(P_MC1) & MC1_A2OUT) != 0)
		;

	/* START THE OUTPUT STREAM TO THE TARGET DAC -------------------- */

	/* FIFO data is now available, so we enable execution of time slots
	 * 1 and higher by clearing the EOS flag in slot 0.  Note that SD3
	 * will be shifted in and stored in FB_BUFFER2 for end-of-slot-list
	 * detection.
	 */
	SETVECT(0, XSD2 | RSD3 | SIB_A2);

	/* Wait for slot 1 to execute to ensure that the Packet will be
	 * transmitted.  This is detected by polling the Audio2 output FIFO
	 * underflow flag, which will be set when slot 1 execution has
	 * finished transferring the DAC's data DWORD from the output FIFO
	 * to the output buffer register.
	 */
	while ((RR7146(P_SSR) & SSR_AF2_OUT) == 0)
		;

	/* Set up to trap execution at slot 0 when the TSL sequencer cycles
	 * back to slot 0 after executing the EOS in slot 5.  Also,
	 * simultaneously shift out and in the 0x00 that is ALWAYS the value
	 * stored in the last byte to be shifted out of the FIFO's DWORD
	 * buffer register.
	 */
	SETVECT(0, XSD2 | XFIFO_2 | RSD2 | SIB_A2 | EOS);

	/* WAIT FOR THE TRANSACTION TO FINISH ----------------------- */

	/* Wait for the TSL to finish executing all time slots before
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
	if ((RR7146(P_FB_BUFFER2) & 0xFF000000) != 0) {
		/* The trap was set on time and we are still executing somewhere
		 * in slots 2-5, so we now wait for slot 0 to execute and trap
		 * TSL execution.  This is detected when FB_BUFFER2 MSB changes
		 * from 0xFF to 0x00, which slot 0 causes to happen by shifting
		 * out/in on SD2 the 0x00 that is always referenced by slot 5.
		 */
		while ((RR7146(P_FB_BUFFER2) & 0xFF000000) != 0)
			;
	}
	/* Either (1) we were too late setting the slot 0 trap; the TSL
	 * sequencer restarted slot 0 before we could set the EOS trap flag,
	 * or (2) we were not late and execution is now trapped at slot 0.
	 * In either case, we must now change slot 0 so that it will store
	 * value 0xFF (instead of 0x00) to FB_BUFFER2 next time it executes.
	 * In order to do this, we reprogram slot 0 so that it will shift in
	 * SD3, which is driven only by a pull-up resistor.
	 */
	SETVECT(0, RSD3 | SIB_A2 | EOS);

	/* Wait for slot 0 to execute, at which time the TSL is setup for
	 * the next DAC write.  This is detected when FB_BUFFER2 MSB changes
	 * from 0x00 to 0xFF.
	 */
	while ((RR7146(P_FB_BUFFER2) & 0xFF000000) == 0)
		;
}

/*  Private helper function: Write setpoint to an application DAC channel. */
static void SetDAC(struct comedi_device *dev, uint16_t chan, short dacdata)
{
	struct s626_private *devpriv = dev->private;
	register uint16_t signmask;
	register uint32_t WSImage;

	/*  Adjust DAC data polarity and set up Polarity Control Register */
	/*  image. */
	signmask = 1 << chan;
	if (dacdata < 0) {
		dacdata = -dacdata;
		devpriv->Dacpol |= signmask;
	} else
		devpriv->Dacpol &= ~signmask;

	/*  Limit DAC setpoint value to valid range. */
	if ((uint16_t) dacdata > 0x1FFF)
		dacdata = 0x1FFF;

	/* Set up TSL2 records (aka "vectors") for DAC update.  Vectors V2
	 * and V3 transmit the setpoint to the target DAC.  V4 and V5 send
	 * data to a non-existent TrimDac channel just to keep the clock
	 * running after sending data to the target DAC.  This is necessary
	 * to eliminate the clock glitch that would otherwise occur at the
	 * end of the target DAC's serial data stream.  When the sequence
	 * restarts at V0 (after executing V5), the gate array automatically
	 * disables gating for the DAC clock and all DAC chip selects.
	 */

	WSImage = (chan & 2) ? WS1 : WS2;
	/* Choose DAC chip select to be asserted. */
	SETVECT(2, XSD2 | XFIFO_1 | WSImage);
	/* Slot 2: Transmit high data byte to target DAC. */
	SETVECT(3, XSD2 | XFIFO_0 | WSImage);
	/* Slot 3: Transmit low data byte to target DAC. */
	SETVECT(4, XSD2 | XFIFO_3 | WS3);
	/* Slot 4: Transmit to non-existent TrimDac channel to keep clock */
	SETVECT(5, XSD2 | XFIFO_2 | WS3 | EOS);
	/* Slot 5: running after writing target DAC's low data byte. */

	/*  Construct and transmit target DAC's serial packet:
	 * ( A10D DDDD ),( DDDD DDDD ),( 0x0F ),( 0x00 ) where A is chan<0>,
	 * and D<12:0> is the DAC setpoint.  Append a WORD value (that writes
	 * to a  non-existent TrimDac channel) that serves to keep the clock
	 * running after the packet has been sent to the target DAC.
	 */
	SendDAC(dev, 0x0F000000
		/* Continue clock after target DAC data (write to non-existent trimdac). */
		| 0x00004000
		/* Address the two main dual-DAC devices (TSL's chip select enables
		 * target device). */
		| ((uint32_t) (chan & 1) << 15)
		/*  Address the DAC channel within the  device. */
		| (uint32_t) dacdata);	/*  Include DAC setpoint data. */

}

static void WriteTrimDAC(struct comedi_device *dev, uint8_t LogicalChan,
			 uint8_t DacData)
{
	struct s626_private *devpriv = dev->private;
	uint32_t chan;

	/*  Save the new setpoint in case the application needs to read it back later. */
	devpriv->TrimSetpoint[LogicalChan] = (uint8_t) DacData;

	/*  Map logical channel number to physical channel number. */
	chan = (uint32_t) trimchan[LogicalChan];

	/* Set up TSL2 records for TrimDac write operation.  All slots shift
	 * 0xFF in from pulled-up SD3 so that the end of the slot sequence
	 * can be detected.
	 */

	SETVECT(2, XSD2 | XFIFO_1 | WS3);
	/* Slot 2: Send high uint8_t to target TrimDac. */
	SETVECT(3, XSD2 | XFIFO_0 | WS3);
	/* Slot 3: Send low uint8_t to target TrimDac. */
	SETVECT(4, XSD2 | XFIFO_3 | WS1);
	/* Slot 4: Send NOP high uint8_t to DAC0 to keep clock running. */
	SETVECT(5, XSD2 | XFIFO_2 | WS1 | EOS);
	/* Slot 5: Send NOP low  uint8_t to DAC0. */

	/* Construct and transmit target DAC's serial packet:
	 * ( 0000 AAAA ), ( DDDD DDDD ),( 0x00 ),( 0x00 ) where A<3:0> is the
	 * DAC channel's address, and D<7:0> is the DAC setpoint.  Append a
	 * WORD value (that writes a channel 0 NOP command to a non-existent
	 * main DAC channel) that serves to keep the clock running after the
	 * packet has been sent to the target DAC.
	 */

	/*  Address the DAC channel within the trimdac device. */
	SendDAC(dev, ((uint32_t) chan << 8)
		| (uint32_t) DacData);	/*  Include DAC setpoint data. */
}

static void LoadTrimDACs(struct comedi_device *dev)
{
	register uint8_t i;

	/*  Copy TrimDac setpoint values from EEPROM to TrimDacs. */
	for (i = 0; i < ARRAY_SIZE(trimchan); i++)
		WriteTrimDAC(dev, i, I2Cread(dev, trimadrs[i]));
}

/* ******  COUNTER FUNCTIONS  ******* */
/* All counter functions address a specific counter by means of the
 * "Counter" argument, which is a logical counter number.  The Counter
 * argument may have any of the following legal values: 0=0A, 1=1A,
 * 2=2A, 3=0B, 4=1B, 5=2B.
 */

/*  Read a counter's output latch. */
static uint32_t ReadLatch(struct comedi_device *dev, struct enc_private *k)
{
	register uint32_t value;

	/*  Latch counts and fetch LSW of latched counts value. */
	value = (uint32_t) DEBIread(dev, k->MyLatchLsw);

	/*  Fetch MSW of latched counts and combine with LSW. */
	value |= ((uint32_t) DEBIread(dev, k->MyLatchLsw + 2) << 16);

	/*  Return latched counts. */
	return value;
}

/* Return/set a counter pair's latch trigger source.  0: On read
 * access, 1: A index latches A, 2: B index latches B, 3: A overflow
 * latches B.
 */
static void SetLatchSource(struct comedi_device *dev, struct enc_private *k,
			   uint16_t value)
{
	DEBIreplace(dev, k->MyCRB,
		    (uint16_t) (~(CRBMSK_INTCTRL | CRBMSK_LATCHSRC)),
		    (uint16_t) (value << CRBBIT_LATCHSRC));
}

/*  Write value into counter preload register. */
static void Preload(struct comedi_device *dev, struct enc_private *k,
		    uint32_t value)
{
	DEBIwrite(dev, (uint16_t) (k->MyLatchLsw), (uint16_t) value);
	DEBIwrite(dev, (uint16_t) (k->MyLatchLsw + 2),
		  (uint16_t) (value >> 16));
}

static unsigned int s626_ai_reg_to_uint(int data)
{
	unsigned int tempdata;

	tempdata = (data >> 18);
	if (tempdata & 0x2000)
		tempdata &= 0x1fff;
	else
		tempdata += (1 << 13);

	return tempdata;
}

/* static unsigned int s626_uint_to_reg(struct comedi_subdevice *s, int data){ */
/*   return 0; */
/* } */

static int s626_dio_set_irq(struct comedi_device *dev, unsigned int chan)
{
	unsigned int group;
	unsigned int bitmask;
	unsigned int status;

	/* select dio bank */
	group = chan / 16;
	bitmask = 1 << (chan - (16 * group));

	/* set channel to capture positive edge */
	status = DEBIread(dev,
			  ((struct dio_private *)(dev->subdevices + 2 +
						  group)->private)->RDEdgSel);
	DEBIwrite(dev,
		  ((struct dio_private *)(dev->subdevices + 2 +
					  group)->private)->WREdgSel,
		  bitmask | status);

	/* enable interrupt on selected channel */
	status = DEBIread(dev,
			  ((struct dio_private *)(dev->subdevices + 2 +
						  group)->private)->RDIntSel);
	DEBIwrite(dev,
		  ((struct dio_private *)(dev->subdevices + 2 +
					  group)->private)->WRIntSel,
		  bitmask | status);

	/* enable edge capture write command */
	DEBIwrite(dev, LP_MISC1, MISC1_EDCAP);

	/* enable edge capture on selected channel */
	status = DEBIread(dev,
			  ((struct dio_private *)(dev->subdevices + 2 +
						  group)->private)->RDCapSel);
	DEBIwrite(dev,
		  ((struct dio_private *)(dev->subdevices + 2 +
					  group)->private)->WRCapSel,
		  bitmask | status);

	return 0;
}

static int s626_dio_reset_irq(struct comedi_device *dev, unsigned int group,
			      unsigned int mask)
{
	/* disable edge capture write command */
	DEBIwrite(dev, LP_MISC1, MISC1_NOEDCAP);

	/* enable edge capture on selected channel */
	DEBIwrite(dev,
		  ((struct dio_private *)(dev->subdevices + 2 +
					  group)->private)->WRCapSel, mask);

	return 0;
}

static int s626_dio_clear_irq(struct comedi_device *dev)
{
	unsigned int group;

	/* disable edge capture write command */
	DEBIwrite(dev, LP_MISC1, MISC1_NOEDCAP);

	for (group = 0; group < S626_DIO_BANKS; group++) {
		/* clear pending events and interrupt */
		DEBIwrite(dev,
			  ((struct dio_private *)(dev->subdevices + 2 +
						  group)->private)->WRCapSel,
			  0xffff);
	}

	return 0;
}

static irqreturn_t s626_irq_handler(int irq, void *d)
{
	struct comedi_device *dev = d;
	struct s626_private *devpriv = dev->private;
	struct comedi_subdevice *s;
	struct comedi_cmd *cmd;
	struct enc_private *k;
	unsigned long flags;
	int32_t *readaddr;
	uint32_t irqtype, irqstatus;
	int i = 0;
	short tempdata;
	uint8_t group;
	uint16_t irqbit;

	if (dev->attached == 0)
		return IRQ_NONE;
	/*  lock to avoid race with comedi_poll */
	spin_lock_irqsave(&dev->spinlock, flags);

	/* save interrupt enable register state */
	irqstatus = readl(devpriv->base_addr + P_IER);

	/* read interrupt type */
	irqtype = readl(devpriv->base_addr + P_ISR);

	/* disable master interrupt */
	writel(0, devpriv->base_addr + P_IER);

	/* clear interrupt */
	writel(irqtype, devpriv->base_addr + P_ISR);

	switch (irqtype) {
	case IRQ_RPS1:		/*  end_of_scan occurs */
		/*  manage ai subdevice */
		s = dev->subdevices;
		cmd = &(s->async->cmd);

		/* Init ptr to DMA buffer that holds new ADC data.  We skip the
		 * first uint16_t in the buffer because it contains junk data from
		 * the final ADC of the previous poll list scan.
		 */
		readaddr = (int32_t *) devpriv->ANABuf.LogicalBase + 1;

		/*  get the data and hand it over to comedi */
		for (i = 0; i < (s->async->cmd.chanlist_len); i++) {
			/*  Convert ADC data to 16-bit integer values and copy to application */
			/*  buffer. */
			tempdata = s626_ai_reg_to_uint((int)*readaddr);
			readaddr++;

			/* put data into read buffer */
			/*  comedi_buf_put(s->async, tempdata); */
			if (cfc_write_to_buffer(s, tempdata) == 0)
				printk
				    ("s626_irq_handler: cfc_write_to_buffer error!\n");
		}

		/* end of scan occurs */
		s->async->events |= COMEDI_CB_EOS;

		if (!(devpriv->ai_continous))
			devpriv->ai_sample_count--;
		if (devpriv->ai_sample_count <= 0) {
			devpriv->ai_cmd_running = 0;

			/*  Stop RPS program. */
			MC_DISABLE(P_MC1, MC1_ERPS1);

			/* send end of acquisition */
			s->async->events |= COMEDI_CB_EOA;

			/* disable master interrupt */
			irqstatus = 0;
		}

		if (devpriv->ai_cmd_running && cmd->scan_begin_src == TRIG_EXT)
			s626_dio_set_irq(dev, cmd->scan_begin_arg);
		/*  tell comedi that data is there */
		comedi_event(dev, s);
		break;
	case IRQ_GPIO3:	/* check dio and conter interrupt */
		/*  manage ai subdevice */
		s = dev->subdevices;
		cmd = &(s->async->cmd);

		/* s626_dio_clear_irq(dev); */

		for (group = 0; group < S626_DIO_BANKS; group++) {
			irqbit = 0;
			/* read interrupt type */
			irqbit = DEBIread(dev,
					  ((struct dio_private *)(dev->
								  subdevices +
								  2 +
								  group)->
					   private)->RDCapFlg);

			/* check if interrupt is generated from dio channels */
			if (irqbit) {
				s626_dio_reset_irq(dev, group, irqbit);
				if (devpriv->ai_cmd_running) {
					/* check if interrupt is an ai acquisition start trigger */
					if ((irqbit >> (cmd->start_arg -
							(16 * group)))
					    == 1 && cmd->start_src == TRIG_EXT) {
						/*  Start executing the RPS program. */
						MC_ENABLE(P_MC1, MC1_ERPS1);

						if (cmd->scan_begin_src ==
						    TRIG_EXT) {
							s626_dio_set_irq(dev,
									 cmd->scan_begin_arg);
						}
					}
					if ((irqbit >> (cmd->scan_begin_arg -
							(16 * group)))
					    == 1
					    && cmd->scan_begin_src ==
					    TRIG_EXT) {
						/*  Trigger ADC scan loop start by setting RPS Signal 0. */
						MC_ENABLE(P_MC2, MC2_ADC_RPS);

						if (cmd->convert_src ==
						    TRIG_EXT) {
							devpriv->ai_convert_count
							    = cmd->chanlist_len;

							s626_dio_set_irq(dev,
									 cmd->convert_arg);
						}

						if (cmd->convert_src ==
						    TRIG_TIMER) {
							k = &encpriv[5];
							devpriv->ai_convert_count
							    = cmd->chanlist_len;
							k->SetEnable(dev, k,
								     CLKENAB_ALWAYS);
						}
					}
					if ((irqbit >> (cmd->convert_arg -
							(16 * group)))
					    == 1
					    && cmd->convert_src == TRIG_EXT) {
						/*  Trigger ADC scan loop start by setting RPS Signal 0. */
						MC_ENABLE(P_MC2, MC2_ADC_RPS);

						devpriv->ai_convert_count--;

						if (devpriv->ai_convert_count >
						    0) {
							s626_dio_set_irq(dev,
									 cmd->convert_arg);
						}
					}
				}
				break;
			}
		}

		/* read interrupt type */
		irqbit = DEBIread(dev, LP_RDMISC2);

		/* check interrupt on counters */
		if (irqbit & IRQ_COINT1A) {
			k = &encpriv[0];

			/* clear interrupt capture flag */
			k->ResetCapFlags(dev, k);
		}
		if (irqbit & IRQ_COINT2A) {
			k = &encpriv[1];

			/* clear interrupt capture flag */
			k->ResetCapFlags(dev, k);
		}
		if (irqbit & IRQ_COINT3A) {
			k = &encpriv[2];

			/* clear interrupt capture flag */
			k->ResetCapFlags(dev, k);
		}
		if (irqbit & IRQ_COINT1B) {
			k = &encpriv[3];

			/* clear interrupt capture flag */
			k->ResetCapFlags(dev, k);
		}
		if (irqbit & IRQ_COINT2B) {
			k = &encpriv[4];

			/* clear interrupt capture flag */
			k->ResetCapFlags(dev, k);

			if (devpriv->ai_convert_count > 0) {
				devpriv->ai_convert_count--;
				if (devpriv->ai_convert_count == 0)
					k->SetEnable(dev, k, CLKENAB_INDEX);

				if (cmd->convert_src == TRIG_TIMER) {
					/*  Trigger ADC scan loop start by setting RPS Signal 0. */
					MC_ENABLE(P_MC2, MC2_ADC_RPS);
				}
			}
		}
		if (irqbit & IRQ_COINT3B) {
			k = &encpriv[5];

			/* clear interrupt capture flag */
			k->ResetCapFlags(dev, k);

			if (cmd->scan_begin_src == TRIG_TIMER) {
				/*  Trigger ADC scan loop start by setting RPS Signal 0. */
				MC_ENABLE(P_MC2, MC2_ADC_RPS);
			}

			if (cmd->convert_src == TRIG_TIMER) {
				k = &encpriv[4];
				devpriv->ai_convert_count = cmd->chanlist_len;
				k->SetEnable(dev, k, CLKENAB_ALWAYS);
			}
		}
	}

	/* enable interrupt */
	writel(irqstatus, devpriv->base_addr + P_IER);

	spin_unlock_irqrestore(&dev->spinlock, flags);
	return IRQ_HANDLED;
}

/*
 * this functions build the RPS program for hardware driven acquistion
 */
static void ResetADC(struct comedi_device *dev, uint8_t *ppl)
{
	struct s626_private *devpriv = dev->private;
	register uint32_t *pRPS;
	uint32_t JmpAdrs;
	uint16_t i;
	uint16_t n;
	uint32_t LocalPPL;
	struct comedi_cmd *cmd = &(dev->subdevices->async->cmd);

	/*  Stop RPS program in case it is currently running. */
	MC_DISABLE(P_MC1, MC1_ERPS1);

	/*  Set starting logical address to write RPS commands. */
	pRPS = (uint32_t *) devpriv->RPSBuf.LogicalBase;

	/*  Initialize RPS instruction pointer. */
	WR7146(P_RPSADDR1, (uint32_t) devpriv->RPSBuf.PhysicalBase);

	/*  Construct RPS program in RPSBuf DMA buffer */

	if (cmd != NULL && cmd->scan_begin_src != TRIG_FOLLOW) {
		/*  Wait for Start trigger. */
		*pRPS++ = RPS_PAUSE | RPS_SIGADC;
		*pRPS++ = RPS_CLRSIGNAL | RPS_SIGADC;
	}

	/* SAA7146 BUG WORKAROUND Do a dummy DEBI Write.  This is necessary
	 * because the first RPS DEBI Write following a non-RPS DEBI write
	 * seems to always fail.  If we don't do this dummy write, the ADC
	 * gain might not be set to the value required for the first slot in
	 * the poll list; the ADC gain would instead remain unchanged from
	 * the previously programmed value.
	 */
	*pRPS++ = RPS_LDREG | (P_DEBICMD >> 2);
	/* Write DEBI Write command and address to shadow RAM. */

	*pRPS++ = DEBI_CMD_WRWORD | LP_GSEL;
	*pRPS++ = RPS_LDREG | (P_DEBIAD >> 2);
	/*  Write DEBI immediate data  to shadow RAM: */

	*pRPS++ = GSEL_BIPOLAR5V;
	/*  arbitrary immediate data  value. */

	*pRPS++ = RPS_CLRSIGNAL | RPS_DEBI;
	/*  Reset "shadow RAM  uploaded" flag. */
	*pRPS++ = RPS_UPLOAD | RPS_DEBI;	/*  Invoke shadow RAM upload. */
	*pRPS++ = RPS_PAUSE | RPS_DEBI;	/*  Wait for shadow upload to finish. */

	/* Digitize all slots in the poll list. This is implemented as a
	 * for loop to limit the slot count to 16 in case the application
	 * forgot to set the EOPL flag in the final slot.
	 */
	for (devpriv->AdcItems = 0; devpriv->AdcItems < 16; devpriv->AdcItems++) {
		/* Convert application's poll list item to private board class
		 * format.  Each app poll list item is an uint8_t with form
		 * (EOPL,x,x,RANGE,CHAN<3:0>), where RANGE code indicates 0 =
		 * +-10V, 1 = +-5V, and EOPL = End of Poll List marker.
		 */
		LocalPPL =
		    (*ppl << 8) | (*ppl & 0x10 ? GSEL_BIPOLAR5V :
				   GSEL_BIPOLAR10V);

		/*  Switch ADC analog gain. */
		*pRPS++ = RPS_LDREG | (P_DEBICMD >> 2);	/*  Write DEBI command */
		/*  and address to */
		/*  shadow RAM. */
		*pRPS++ = DEBI_CMD_WRWORD | LP_GSEL;
		*pRPS++ = RPS_LDREG | (P_DEBIAD >> 2);	/*  Write DEBI */
		/*  immediate data to */
		/*  shadow RAM. */
		*pRPS++ = LocalPPL;
		*pRPS++ = RPS_CLRSIGNAL | RPS_DEBI;	/*  Reset "shadow RAM uploaded" */
		/*  flag. */
		*pRPS++ = RPS_UPLOAD | RPS_DEBI;	/*  Invoke shadow RAM upload. */
		*pRPS++ = RPS_PAUSE | RPS_DEBI;	/*  Wait for shadow upload to */
		/*  finish. */

		/*  Select ADC analog input channel. */
		*pRPS++ = RPS_LDREG | (P_DEBICMD >> 2);
		/*  Write DEBI command and address to  shadow RAM. */
		*pRPS++ = DEBI_CMD_WRWORD | LP_ISEL;
		*pRPS++ = RPS_LDREG | (P_DEBIAD >> 2);
		/*  Write DEBI immediate data to shadow RAM. */
		*pRPS++ = LocalPPL;
		*pRPS++ = RPS_CLRSIGNAL | RPS_DEBI;
		/*  Reset "shadow RAM uploaded"  flag. */

		*pRPS++ = RPS_UPLOAD | RPS_DEBI;
		/*  Invoke shadow RAM upload. */

		*pRPS++ = RPS_PAUSE | RPS_DEBI;
		/*  Wait for shadow upload to finish. */

		/* Delay at least 10 microseconds for analog input settling.
		 * Instead of padding with NOPs, we use RPS_JUMP instructions
		 * here; this allows us to produce a longer delay than is
		 * possible with NOPs because each RPS_JUMP flushes the RPS'
		 * instruction prefetch pipeline.
		 */
		JmpAdrs =
		    (uint32_t) devpriv->RPSBuf.PhysicalBase +
		    (uint32_t) ((unsigned long)pRPS -
				(unsigned long)devpriv->RPSBuf.LogicalBase);
		for (i = 0; i < (10 * RPSCLK_PER_US / 2); i++) {
			JmpAdrs += 8;	/*  Repeat to implement time delay: */
			*pRPS++ = RPS_JUMP;	/*  Jump to next RPS instruction. */
			*pRPS++ = JmpAdrs;
		}

		if (cmd != NULL && cmd->convert_src != TRIG_NOW) {
			/*  Wait for Start trigger. */
			*pRPS++ = RPS_PAUSE | RPS_SIGADC;
			*pRPS++ = RPS_CLRSIGNAL | RPS_SIGADC;
		}
		/*  Start ADC by pulsing GPIO1. */
		*pRPS++ = RPS_LDREG | (P_GPIO >> 2);	/*  Begin ADC Start pulse. */
		*pRPS++ = GPIO_BASE | GPIO1_LO;
		*pRPS++ = RPS_NOP;
		/*  VERSION 2.03 CHANGE: STRETCH OUT ADC START PULSE. */
		*pRPS++ = RPS_LDREG | (P_GPIO >> 2);	/*  End ADC Start pulse. */
		*pRPS++ = GPIO_BASE | GPIO1_HI;

		/* Wait for ADC to complete (GPIO2 is asserted high when ADC not
		 * busy) and for data from previous conversion to shift into FB
		 * BUFFER 1 register.
		 */
		*pRPS++ = RPS_PAUSE | RPS_GPIO2;	/*  Wait for ADC done. */

		/*  Transfer ADC data from FB BUFFER 1 register to DMA buffer. */
		*pRPS++ = RPS_STREG | (BUGFIX_STREG(P_FB_BUFFER1) >> 2);
		*pRPS++ =
		    (uint32_t) devpriv->ANABuf.PhysicalBase +
		    (devpriv->AdcItems << 2);

		/*  If this slot's EndOfPollList flag is set, all channels have */
		/*  now been processed. */
		if (*ppl++ & EOPL) {
			devpriv->AdcItems++;	/*  Adjust poll list item count. */
			break;	/*  Exit poll list processing loop. */
		}
	}

	/* VERSION 2.01 CHANGE: DELAY CHANGED FROM 250NS to 2US.  Allow the
	 * ADC to stabilize for 2 microseconds before starting the final
	 * (dummy) conversion.  This delay is necessary to allow sufficient
	 * time between last conversion finished and the start of the dummy
	 * conversion.  Without this delay, the last conversion's data value
	 * is sometimes set to the previous conversion's data value.
	 */
	for (n = 0; n < (2 * RPSCLK_PER_US); n++)
		*pRPS++ = RPS_NOP;

	/* Start a dummy conversion to cause the data from the last
	 * conversion of interest to be shifted in.
	 */
	*pRPS++ = RPS_LDREG | (P_GPIO >> 2);	/*  Begin ADC Start pulse. */
	*pRPS++ = GPIO_BASE | GPIO1_LO;
	*pRPS++ = RPS_NOP;
	/* VERSION 2.03 CHANGE: STRETCH OUT ADC START PULSE. */
	*pRPS++ = RPS_LDREG | (P_GPIO >> 2);	/*  End ADC Start pulse. */
	*pRPS++ = GPIO_BASE | GPIO1_HI;

	/* Wait for the data from the last conversion of interest to arrive
	 * in FB BUFFER 1 register.
	 */
	*pRPS++ = RPS_PAUSE | RPS_GPIO2;	/*  Wait for ADC done. */

	/*  Transfer final ADC data from FB BUFFER 1 register to DMA buffer. */
	*pRPS++ = RPS_STREG | (BUGFIX_STREG(P_FB_BUFFER1) >> 2);	/*  */
	*pRPS++ =
	    (uint32_t) devpriv->ANABuf.PhysicalBase + (devpriv->AdcItems << 2);

	/*  Indicate ADC scan loop is finished. */
	/*  *pRPS++= RPS_CLRSIGNAL | RPS_SIGADC ;  // Signal ReadADC() that scan is done. */

	/* invoke interrupt */
	if (devpriv->ai_cmd_running == 1) {
		*pRPS++ = RPS_IRQ;
	}
	/*  Restart RPS program at its beginning. */
	*pRPS++ = RPS_JUMP;	/*  Branch to start of RPS program. */
	*pRPS++ = (uint32_t) devpriv->RPSBuf.PhysicalBase;

	/*  End of RPS program build */
}

/* TO COMPLETE, IF NECESSARY */
static int s626_ai_insn_config(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn, unsigned int *data)
{

	return -EINVAL;
}

/* static int s626_ai_rinsn(struct comedi_device *dev,struct comedi_subdevice *s,struct comedi_insn *insn,unsigned int *data) */
/* { */
/*   struct s626_private *devpriv = dev->private; */
/*   register uint8_t	i; */
/*   register int32_t	*readaddr; */

/*   Trigger ADC scan loop start by setting RPS Signal 0. */
/*   MC_ENABLE( P_MC2, MC2_ADC_RPS ); */

/*   Wait until ADC scan loop is finished (RPS Signal 0 reset). */
/*   while ( MC_TEST( P_MC2, MC2_ADC_RPS ) ); */

/* Init ptr to DMA buffer that holds new ADC data.  We skip the
 * first uint16_t in the buffer because it contains junk data from
 * the final ADC of the previous poll list scan.
 */
/*   readaddr = (uint32_t *)devpriv->ANABuf.LogicalBase + 1; */

/*  Convert ADC data to 16-bit integer values and copy to application buffer. */
/*   for ( i = 0; i < devpriv->AdcItems; i++ ) { */
/*     *data = s626_ai_reg_to_uint( *readaddr++ ); */
/*     data++; */
/*   } */

/*   return i; */
/* } */

static int s626_ai_insn_read(struct comedi_device *dev,
			     struct comedi_subdevice *s,
			     struct comedi_insn *insn, unsigned int *data)
{
	struct s626_private *devpriv = dev->private;
	uint16_t chan = CR_CHAN(insn->chanspec);
	uint16_t range = CR_RANGE(insn->chanspec);
	uint16_t AdcSpec = 0;
	uint32_t GpioImage;
	int n;

	/* interrupt call test  */
/*   writel(IRQ_GPIO3,devpriv->base_addr+P_PSR); */
	/* Writing a logical 1 into any of the RPS_PSR bits causes the
	 * corresponding interrupt to be generated if enabled
	 */

	/* Convert application's ADC specification into form
	 *  appropriate for register programming.
	 */
	if (range == 0)
		AdcSpec = (chan << 8) | (GSEL_BIPOLAR5V);
	else
		AdcSpec = (chan << 8) | (GSEL_BIPOLAR10V);

	/*  Switch ADC analog gain. */
	DEBIwrite(dev, LP_GSEL, AdcSpec);	/*  Set gain. */

	/*  Select ADC analog input channel. */
	DEBIwrite(dev, LP_ISEL, AdcSpec);	/*  Select channel. */

	for (n = 0; n < insn->n; n++) {

		/*  Delay 10 microseconds for analog input settling. */
		udelay(10);

		/*  Start ADC by pulsing GPIO1 low. */
		GpioImage = RR7146(P_GPIO);
		/*  Assert ADC Start command */
		WR7146(P_GPIO, GpioImage & ~GPIO1_HI);
		/*    and stretch it out. */
		WR7146(P_GPIO, GpioImage & ~GPIO1_HI);
		WR7146(P_GPIO, GpioImage & ~GPIO1_HI);
		/*  Negate ADC Start command. */
		WR7146(P_GPIO, GpioImage | GPIO1_HI);

		/*  Wait for ADC to complete (GPIO2 is asserted high when */
		/*  ADC not busy) and for data from previous conversion to */
		/*  shift into FB BUFFER 1 register. */

		/*  Wait for ADC done. */
		while (!(RR7146(P_PSR) & PSR_GPIO2))
			;

		/*  Fetch ADC data. */
		if (n != 0)
			data[n - 1] = s626_ai_reg_to_uint(RR7146(P_FB_BUFFER1));

		/* Allow the ADC to stabilize for 4 microseconds before
		 * starting the next (final) conversion.  This delay is
		 * necessary to allow sufficient time between last
		 * conversion finished and the start of the next
		 * conversion.  Without this delay, the last conversion's
		 * data value is sometimes set to the previous
		 * conversion's data value.
		 */
		udelay(4);
	}

	/* Start a dummy conversion to cause the data from the
	 * previous conversion to be shifted in. */
	GpioImage = RR7146(P_GPIO);

	/* Assert ADC Start command */
	WR7146(P_GPIO, GpioImage & ~GPIO1_HI);
	/*    and stretch it out. */
	WR7146(P_GPIO, GpioImage & ~GPIO1_HI);
	WR7146(P_GPIO, GpioImage & ~GPIO1_HI);
	/*  Negate ADC Start command. */
	WR7146(P_GPIO, GpioImage | GPIO1_HI);

	/*  Wait for the data to arrive in FB BUFFER 1 register. */

	/*  Wait for ADC done. */
	while (!(RR7146(P_PSR) & PSR_GPIO2))
		;

	/*  Fetch ADC data from audio interface's input shift register. */

	/*  Fetch ADC data. */
	if (n != 0)
		data[n - 1] = s626_ai_reg_to_uint(RR7146(P_FB_BUFFER1));

	return n;
}

static int s626_ai_load_polllist(uint8_t *ppl, struct comedi_cmd *cmd)
{

	int n;

	for (n = 0; n < cmd->chanlist_len; n++) {
		if (CR_RANGE((cmd->chanlist)[n]) == 0)
			ppl[n] = (CR_CHAN((cmd->chanlist)[n])) | (RANGE_5V);
		else
			ppl[n] = (CR_CHAN((cmd->chanlist)[n])) | (RANGE_10V);
	}
	if (n != 0)
		ppl[n - 1] |= EOPL;

	return n;
}

static int s626_ai_inttrig(struct comedi_device *dev,
			   struct comedi_subdevice *s, unsigned int trignum)
{
	struct s626_private *devpriv = dev->private;

	if (trignum != 0)
		return -EINVAL;

	/*  Start executing the RPS program. */
	MC_ENABLE(P_MC1, MC1_ERPS1);

	s->async->inttrig = NULL;

	return 1;
}

/* This function doesn't require a particular form, this is just what
 * happens to be used in some of the drivers.  It should convert ns
 * nanoseconds to a counter value suitable for programming the device.
 * Also, it should adjust ns so that it cooresponds to the actual time
 * that the device will use. */
static int s626_ns_to_timer(int *nanosec, int round_mode)
{
	int divider, base;

	base = 500;		/* 2MHz internal clock */

	switch (round_mode) {
	case TRIG_ROUND_NEAREST:
	default:
		divider = (*nanosec + base / 2) / base;
		break;
	case TRIG_ROUND_DOWN:
		divider = (*nanosec) / base;
		break;
	case TRIG_ROUND_UP:
		divider = (*nanosec + base - 1) / base;
		break;
	}

	*nanosec = base * divider;
	return divider - 1;
}

static void s626_timer_load(struct comedi_device *dev, struct enc_private *k,
			    int tick)
{
	uint16_t Setup = (LOADSRC_INDX << BF_LOADSRC) |	/*  Preload upon */
	    /*  index. */
	    (INDXSRC_SOFT << BF_INDXSRC) |	/*  Disable hardware index. */
	    (CLKSRC_TIMER << BF_CLKSRC) |	/*  Operating mode is Timer. */
	    (CLKPOL_POS << BF_CLKPOL) |	/*  Active high clock. */
	    (CNTDIR_DOWN << BF_CLKPOL) |	/*  Count direction is Down. */
	    (CLKMULT_1X << BF_CLKMULT) |	/*  Clock multiplier is 1x. */
	    (CLKENAB_INDEX << BF_CLKENAB);
	uint16_t valueSrclatch = LATCHSRC_A_INDXA;
	/*   uint16_t enab=CLKENAB_ALWAYS; */

	k->SetMode(dev, k, Setup, FALSE);

	/*  Set the preload register */
	Preload(dev, k, tick);

	/*  Software index pulse forces the preload register to load */
	/*  into the counter */
	k->SetLoadTrig(dev, k, 0);
	k->PulseIndex(dev, k);

	/* set reload on counter overflow */
	k->SetLoadTrig(dev, k, 1);

	/* set interrupt on overflow */
	k->SetIntSrc(dev, k, INTSRC_OVER);

	SetLatchSource(dev, k, valueSrclatch);
	/*   k->SetEnable(dev,k,(uint16_t)(enab != 0)); */
}

/*  TO COMPLETE  */
static int s626_ai_cmd(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct s626_private *devpriv = dev->private;
	uint8_t ppl[16];
	struct comedi_cmd *cmd = &s->async->cmd;
	struct enc_private *k;
	int tick;

	if (devpriv->ai_cmd_running) {
		printk(KERN_ERR "s626_ai_cmd: Another ai_cmd is running %d\n",
		       dev->minor);
		return -EBUSY;
	}
	/* disable interrupt */
	writel(0, devpriv->base_addr + P_IER);

	/* clear interrupt request */
	writel(IRQ_RPS1 | IRQ_GPIO3, devpriv->base_addr + P_ISR);

	/* clear any pending interrupt */
	s626_dio_clear_irq(dev);
	/*   s626_enc_clear_irq(dev); */

	/* reset ai_cmd_running flag */
	devpriv->ai_cmd_running = 0;

	/*  test if cmd is valid */
	if (cmd == NULL)
		return -EINVAL;

	if (dev->irq == 0) {
		comedi_error(dev,
			     "s626_ai_cmd: cannot run command without an irq");
		return -EIO;
	}

	s626_ai_load_polllist(ppl, cmd);
	devpriv->ai_cmd_running = 1;
	devpriv->ai_convert_count = 0;

	switch (cmd->scan_begin_src) {
	case TRIG_FOLLOW:
		break;
	case TRIG_TIMER:
		/*  set a conter to generate adc trigger at scan_begin_arg interval */
		k = &encpriv[5];
		tick = s626_ns_to_timer((int *)&cmd->scan_begin_arg,
					cmd->flags & TRIG_ROUND_MASK);

		/* load timer value and enable interrupt */
		s626_timer_load(dev, k, tick);
		k->SetEnable(dev, k, CLKENAB_ALWAYS);
		break;
	case TRIG_EXT:
		/*  set the digital line and interrupt for scan trigger */
		if (cmd->start_src != TRIG_EXT)
			s626_dio_set_irq(dev, cmd->scan_begin_arg);
		break;
	}

	switch (cmd->convert_src) {
	case TRIG_NOW:
		break;
	case TRIG_TIMER:
		/*  set a conter to generate adc trigger at convert_arg interval */
		k = &encpriv[4];
		tick = s626_ns_to_timer((int *)&cmd->convert_arg,
					cmd->flags & TRIG_ROUND_MASK);

		/* load timer value and enable interrupt */
		s626_timer_load(dev, k, tick);
		k->SetEnable(dev, k, CLKENAB_INDEX);
		break;
	case TRIG_EXT:
		/*  set the digital line and interrupt for convert trigger */
		if (cmd->scan_begin_src != TRIG_EXT
		    && cmd->start_src == TRIG_EXT)
			s626_dio_set_irq(dev, cmd->convert_arg);
		break;
	}

	switch (cmd->stop_src) {
	case TRIG_COUNT:
		/*  data arrives as one packet */
		devpriv->ai_sample_count = cmd->stop_arg;
		devpriv->ai_continous = 0;
		break;
	case TRIG_NONE:
		/*  continous acquisition */
		devpriv->ai_continous = 1;
		devpriv->ai_sample_count = 0;
		break;
	}

	ResetADC(dev, ppl);

	switch (cmd->start_src) {
	case TRIG_NOW:
		/*  Trigger ADC scan loop start by setting RPS Signal 0. */
		/*  MC_ENABLE( P_MC2, MC2_ADC_RPS ); */

		/*  Start executing the RPS program. */
		MC_ENABLE(P_MC1, MC1_ERPS1);

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
	writel(IRQ_GPIO3 | IRQ_RPS1, devpriv->base_addr + P_IER);

	return 0;
}

static int s626_ai_cmdtest(struct comedi_device *dev,
			   struct comedi_subdevice *s, struct comedi_cmd *cmd)
{
	int err = 0;
	int tmp;

	/* Step 1 : check if triggers are trivially valid */

	err |= cfc_check_trigger_src(&cmd->start_src,
					TRIG_NOW | TRIG_INT | TRIG_EXT);
	err |= cfc_check_trigger_src(&cmd->scan_begin_src,
					TRIG_TIMER | TRIG_EXT | TRIG_FOLLOW);
	err |= cfc_check_trigger_src(&cmd->convert_src,
					TRIG_TIMER | TRIG_EXT | TRIG_NOW);
	err |= cfc_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	err |= cfc_check_trigger_src(&cmd->stop_src, TRIG_COUNT | TRIG_NONE);

	if (err)
		return 1;

	/* Step 2a : make sure trigger sources are unique */

	err |= cfc_check_trigger_is_unique(cmd->start_src);
	err |= cfc_check_trigger_is_unique(cmd->scan_begin_src);
	err |= cfc_check_trigger_is_unique(cmd->convert_src);
	err |= cfc_check_trigger_is_unique(cmd->stop_src);

	/* Step 2b : and mutually compatible */

	if (err)
		return 2;

	/* step 3: make sure arguments are trivially compatible */

	if (cmd->start_src != TRIG_EXT)
		err |= cfc_check_trigger_arg_is(&cmd->start_arg, 0);
	if (cmd->start_src == TRIG_EXT)
		err |= cfc_check_trigger_arg_max(&cmd->start_arg, 39);

	if (cmd->scan_begin_src == TRIG_EXT)
		err |= cfc_check_trigger_arg_max(&cmd->scan_begin_arg, 39);

	if (cmd->convert_src == TRIG_EXT)
		err |= cfc_check_trigger_arg_max(&cmd->convert_arg, 39);

#define MAX_SPEED	200000	/* in nanoseconds */
#define MIN_SPEED	2000000000	/* in nanoseconds */

	if (cmd->scan_begin_src == TRIG_TIMER) {
		err |= cfc_check_trigger_arg_min(&cmd->scan_begin_arg,
						 MAX_SPEED);
		err |= cfc_check_trigger_arg_max(&cmd->scan_begin_arg,
						 MIN_SPEED);
	} else {
		/* external trigger */
		/* should be level/edge, hi/lo specification here */
		/* should specify multiple external triggers */
/*		err |= cfc_check_trigger_arg_max(&cmd->scan_begin_arg, 9); */
	}
	if (cmd->convert_src == TRIG_TIMER) {
		err |= cfc_check_trigger_arg_min(&cmd->convert_arg, MAX_SPEED);
		err |= cfc_check_trigger_arg_max(&cmd->convert_arg, MIN_SPEED);
	} else {
		/* external trigger */
		/* see above */
/*		err |= cfc_check_trigger_arg_max(&cmd->scan_begin_arg, 9); */
	}

	err |= cfc_check_trigger_arg_is(&cmd->scan_end_arg, cmd->chanlist_len);

	if (cmd->stop_src == TRIG_COUNT)
		err |= cfc_check_trigger_arg_max(&cmd->stop_arg, 0x00ffffff);
	else	/* TRIG_NONE */
		err |= cfc_check_trigger_arg_is(&cmd->stop_arg, 0);

	if (err)
		return 3;

	/* step 4: fix up any arguments */

	if (cmd->scan_begin_src == TRIG_TIMER) {
		tmp = cmd->scan_begin_arg;
		s626_ns_to_timer((int *)&cmd->scan_begin_arg,
				 cmd->flags & TRIG_ROUND_MASK);
		if (tmp != cmd->scan_begin_arg)
			err++;
	}
	if (cmd->convert_src == TRIG_TIMER) {
		tmp = cmd->convert_arg;
		s626_ns_to_timer((int *)&cmd->convert_arg,
				 cmd->flags & TRIG_ROUND_MASK);
		if (tmp != cmd->convert_arg)
			err++;
		if (cmd->scan_begin_src == TRIG_TIMER &&
		    cmd->scan_begin_arg <
		    cmd->convert_arg * cmd->scan_end_arg) {
			cmd->scan_begin_arg =
			    cmd->convert_arg * cmd->scan_end_arg;
			err++;
		}
	}

	if (err)
		return 4;

	return 0;
}

static int s626_ai_cancel(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct s626_private *devpriv = dev->private;

	/*  Stop RPS program in case it is currently running. */
	MC_DISABLE(P_MC1, MC1_ERPS1);

	/* disable master interrupt */
	writel(0, devpriv->base_addr + P_IER);

	devpriv->ai_cmd_running = 0;

	return 0;
}

static int s626_ao_winsn(struct comedi_device *dev, struct comedi_subdevice *s,
			 struct comedi_insn *insn, unsigned int *data)
{
	struct s626_private *devpriv = dev->private;
	int i;
	uint16_t chan = CR_CHAN(insn->chanspec);
	int16_t dacdata;

	for (i = 0; i < insn->n; i++) {
		dacdata = (int16_t) data[i];
		devpriv->ao_readback[CR_CHAN(insn->chanspec)] = data[i];
		dacdata -= (0x1fff);

		SetDAC(dev, chan, dacdata);
	}

	return i;
}

static int s626_ao_rinsn(struct comedi_device *dev, struct comedi_subdevice *s,
			 struct comedi_insn *insn, unsigned int *data)
{
	struct s626_private *devpriv = dev->private;
	int i;

	for (i = 0; i < insn->n; i++)
		data[i] = devpriv->ao_readback[CR_CHAN(insn->chanspec)];

	return i;
}

/* *************** DIGITAL I/O FUNCTIONS ***************
 * All DIO functions address a group of DIO channels by means of
 * "group" argument.  group may be 0, 1 or 2, which correspond to DIO
 * ports A, B and C, respectively.
 */

static void s626_dio_init(struct comedi_device *dev)
{
	uint16_t group;
	struct comedi_subdevice *s;

	/*  Prepare to treat writes to WRCapSel as capture disables. */
	DEBIwrite(dev, LP_MISC1, MISC1_NOEDCAP);

	/*  For each group of sixteen channels ... */
	for (group = 0; group < S626_DIO_BANKS; group++) {
		s = dev->subdevices + 2 + group;
		DEBIwrite(dev, diopriv->WRIntSel, 0);	/*  Disable all interrupts. */
		DEBIwrite(dev, diopriv->WRCapSel, 0xFFFF);	/*  Disable all event */
		/*  captures. */
		DEBIwrite(dev, diopriv->WREdgSel, 0);	/*  Init all DIOs to */
		/*  default edge */
		/*  polarity. */
		DEBIwrite(dev, diopriv->WRDOut, 0);	/*  Program all outputs */
		/*  to inactive state. */
	}
}

/* DIO devices are slightly special.  Although it is possible to
 * implement the insn_read/insn_write interface, it is much more
 * useful to applications if you implement the insn_bits interface.
 * This allows packed reading/writing of the DIO channels.  The comedi
 * core can convert between insn_bits and insn_read/write */

static int s626_dio_insn_bits(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn, unsigned int *data)
{
	/*
	 * The insn data consists of a mask in data[0] and the new data in
	 * data[1]. The mask defines which bits we are concerning about.
	 * The new data must be anded with the mask.  Each channel
	 * corresponds to a bit.
	 */
	if (data[0]) {
		/* Check if requested ports are configured for output */
		if ((s->io_bits & data[0]) != data[0])
			return -EIO;

		s->state &= ~data[0];
		s->state |= data[0] & data[1];

		/* Write out the new digital output lines */

		DEBIwrite(dev, diopriv->WRDOut, s->state);
	}
	data[1] = DEBIread(dev, diopriv->RDDIn);

	return insn->n;
}

static int s626_dio_insn_config(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn, unsigned int *data)
{

	switch (data[0]) {
	case INSN_CONFIG_DIO_QUERY:
		data[1] =
		    (s->
		     io_bits & (1 << CR_CHAN(insn->chanspec))) ? COMEDI_OUTPUT :
		    COMEDI_INPUT;
		return insn->n;
		break;
	case COMEDI_INPUT:
		s->io_bits &= ~(1 << CR_CHAN(insn->chanspec));
		break;
	case COMEDI_OUTPUT:
		s->io_bits |= 1 << CR_CHAN(insn->chanspec);
		break;
	default:
		return -EINVAL;
		break;
	}
	DEBIwrite(dev, diopriv->WRDOut, s->io_bits);

	return 1;
}

/* Now this function initializes the value of the counter (data[0])
   and set the subdevice. To complete with trigger and interrupt
   configuration */
/* FIXME: data[0] is supposed to be an INSN_CONFIG_xxx constant indicating
 * what is being configured, but this function appears to be using data[0]
 * as a variable. */
static int s626_enc_insn_config(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn, unsigned int *data)
{
	uint16_t Setup = (LOADSRC_INDX << BF_LOADSRC) |	/*  Preload upon */
	    /*  index. */
	    (INDXSRC_SOFT << BF_INDXSRC) |	/*  Disable hardware index. */
	    (CLKSRC_COUNTER << BF_CLKSRC) |	/*  Operating mode is Counter. */
	    (CLKPOL_POS << BF_CLKPOL) |	/*  Active high clock. */
	    /* ( CNTDIR_UP << BF_CLKPOL ) |      // Count direction is Down. */
	    (CLKMULT_1X << BF_CLKMULT) |	/*  Clock multiplier is 1x. */
	    (CLKENAB_INDEX << BF_CLKENAB);
	/*   uint16_t DisableIntSrc=TRUE; */
	/*  uint32_t Preloadvalue;              //Counter initial value */
	uint16_t valueSrclatch = LATCHSRC_AB_READ;
	uint16_t enab = CLKENAB_ALWAYS;
	struct enc_private *k = &encpriv[CR_CHAN(insn->chanspec)];

	/*   (data==NULL) ? (Preloadvalue=0) : (Preloadvalue=data[0]); */

	k->SetMode(dev, k, Setup, TRUE);
	Preload(dev, k, data[0]);
	k->PulseIndex(dev, k);
	SetLatchSource(dev, k, valueSrclatch);
	k->SetEnable(dev, k, (uint16_t) (enab != 0));

	return insn->n;
}

static int s626_enc_insn_read(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn, unsigned int *data)
{

	int n;
	struct enc_private *k = &encpriv[CR_CHAN(insn->chanspec)];

	for (n = 0; n < insn->n; n++)
		data[n] = ReadLatch(dev, k);

	return n;
}

static int s626_enc_insn_write(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn, unsigned int *data)
{

	struct enc_private *k = &encpriv[CR_CHAN(insn->chanspec)];

	/*  Set the preload register */
	Preload(dev, k, data[0]);

	/*  Software index pulse forces the preload register to load */
	/*  into the counter */
	k->SetLoadTrig(dev, k, 0);
	k->PulseIndex(dev, k);
	k->SetLoadTrig(dev, k, 2);

	return 1;
}

static void WriteMISC2(struct comedi_device *dev, uint16_t NewImage)
{
	DEBIwrite(dev, LP_MISC1, MISC1_WENABLE);	/*  enab writes to */
	/*  MISC2 register. */
	DEBIwrite(dev, LP_WRMISC2, NewImage);	/*  Write new image to MISC2. */
	DEBIwrite(dev, LP_MISC1, MISC1_WDISABLE);	/*  Disable writes to MISC2. */
}

static void CloseDMAB(struct comedi_device *dev, struct bufferDMA *pdma,
		      size_t bsize)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	void *vbptr;
	dma_addr_t vpptr;

	if (pdma == NULL)
		return;
	/* find the matching allocation from the board struct */

	vbptr = pdma->LogicalBase;
	vpptr = pdma->PhysicalBase;
	if (vbptr) {
		pci_free_consistent(pcidev, bsize, vbptr, vpptr);
		pdma->LogicalBase = NULL;
		pdma->PhysicalBase = 0;
	}
}

/* ******  PRIVATE COUNTER FUNCTIONS ****** */

/*  Reset a counter's index and overflow event capture flags. */

static void ResetCapFlags_A(struct comedi_device *dev, struct enc_private *k)
{
	DEBIreplace(dev, k->MyCRB, (uint16_t) (~CRBMSK_INTCTRL),
		    CRBMSK_INTRESETCMD | CRBMSK_INTRESET_A);
}

static void ResetCapFlags_B(struct comedi_device *dev, struct enc_private *k)
{
	DEBIreplace(dev, k->MyCRB, (uint16_t) (~CRBMSK_INTCTRL),
		    CRBMSK_INTRESETCMD | CRBMSK_INTRESET_B);
}

/*  Return counter setup in a format (COUNTER_SETUP) that is consistent */
/*  for both A and B counters. */

static uint16_t GetMode_A(struct comedi_device *dev, struct enc_private *k)
{
	register uint16_t cra;
	register uint16_t crb;
	register uint16_t setup;

	/*  Fetch CRA and CRB register images. */
	cra = DEBIread(dev, k->MyCRA);
	crb = DEBIread(dev, k->MyCRB);

	/*  Populate the standardized counter setup bit fields.  Note: */
	/*  IndexSrc is restricted to ENC_X or IndxPol. */
	setup = ((cra & STDMSK_LOADSRC)	/*  LoadSrc  = LoadSrcA. */
		 |((crb << (STDBIT_LATCHSRC - CRBBIT_LATCHSRC)) & STDMSK_LATCHSRC)	/*  LatchSrc = LatchSrcA. */
		 |((cra << (STDBIT_INTSRC - CRABIT_INTSRC_A)) & STDMSK_INTSRC)	/*  IntSrc   = IntSrcA. */
		 |((cra << (STDBIT_INDXSRC - (CRABIT_INDXSRC_A + 1))) & STDMSK_INDXSRC)	/*  IndxSrc  = IndxSrcA<1>. */
		 |((cra >> (CRABIT_INDXPOL_A - STDBIT_INDXPOL)) & STDMSK_INDXPOL)	/*  IndxPol  = IndxPolA. */
		 |((crb >> (CRBBIT_CLKENAB_A - STDBIT_CLKENAB)) & STDMSK_CLKENAB));	/*  ClkEnab  = ClkEnabA. */

	/*  Adjust mode-dependent parameters. */
	if (cra & (2 << CRABIT_CLKSRC_A))	/*  If Timer mode (ClkSrcA<1> == 1): */
		setup |= ((CLKSRC_TIMER << STDBIT_CLKSRC)	/*    Indicate Timer mode. */
			  |((cra << (STDBIT_CLKPOL - CRABIT_CLKSRC_A)) & STDMSK_CLKPOL)	/*    Set ClkPol to indicate count direction (ClkSrcA<0>). */
			  |(MULT_X1 << STDBIT_CLKMULT));	/*    ClkMult must be 1x in Timer mode. */

	else			/*  If Counter mode (ClkSrcA<1> == 0): */
		setup |= ((CLKSRC_COUNTER << STDBIT_CLKSRC)	/*    Indicate Counter mode. */
			  |((cra >> (CRABIT_CLKPOL_A - STDBIT_CLKPOL)) & STDMSK_CLKPOL)	/*    Pass through ClkPol. */
			  |(((cra & CRAMSK_CLKMULT_A) == (MULT_X0 << CRABIT_CLKMULT_A)) ?	/*    Force ClkMult to 1x if not legal, else pass through. */
			    (MULT_X1 << STDBIT_CLKMULT) :
			    ((cra >> (CRABIT_CLKMULT_A -
				      STDBIT_CLKMULT)) & STDMSK_CLKMULT)));

	/*  Return adjusted counter setup. */
	return setup;
}

static uint16_t GetMode_B(struct comedi_device *dev, struct enc_private *k)
{
	register uint16_t cra;
	register uint16_t crb;
	register uint16_t setup;

	/*  Fetch CRA and CRB register images. */
	cra = DEBIread(dev, k->MyCRA);
	crb = DEBIread(dev, k->MyCRB);

	/*  Populate the standardized counter setup bit fields.  Note: */
	/*  IndexSrc is restricted to ENC_X or IndxPol. */
	setup = (((crb << (STDBIT_INTSRC - CRBBIT_INTSRC_B)) & STDMSK_INTSRC)	/*  IntSrc   = IntSrcB. */
		 |((crb << (STDBIT_LATCHSRC - CRBBIT_LATCHSRC)) & STDMSK_LATCHSRC)	/*  LatchSrc = LatchSrcB. */
		 |((crb << (STDBIT_LOADSRC - CRBBIT_LOADSRC_B)) & STDMSK_LOADSRC)	/*  LoadSrc  = LoadSrcB. */
		 |((crb << (STDBIT_INDXPOL - CRBBIT_INDXPOL_B)) & STDMSK_INDXPOL)	/*  IndxPol  = IndxPolB. */
		 |((crb >> (CRBBIT_CLKENAB_B - STDBIT_CLKENAB)) & STDMSK_CLKENAB)	/*  ClkEnab  = ClkEnabB. */
		 |((cra >> ((CRABIT_INDXSRC_B + 1) - STDBIT_INDXSRC)) & STDMSK_INDXSRC));	/*  IndxSrc  = IndxSrcB<1>. */

	/*  Adjust mode-dependent parameters. */
	if ((crb & CRBMSK_CLKMULT_B) == (MULT_X0 << CRBBIT_CLKMULT_B))	/*  If Extender mode (ClkMultB == MULT_X0): */
		setup |= ((CLKSRC_EXTENDER << STDBIT_CLKSRC)	/*    Indicate Extender mode. */
			  |(MULT_X1 << STDBIT_CLKMULT)	/*    Indicate multiplier is 1x. */
			  |((cra >> (CRABIT_CLKSRC_B - STDBIT_CLKPOL)) & STDMSK_CLKPOL));	/*    Set ClkPol equal to Timer count direction (ClkSrcB<0>). */

	else if (cra & (2 << CRABIT_CLKSRC_B))	/*  If Timer mode (ClkSrcB<1> == 1): */
		setup |= ((CLKSRC_TIMER << STDBIT_CLKSRC)	/*    Indicate Timer mode. */
			  |(MULT_X1 << STDBIT_CLKMULT)	/*    Indicate multiplier is 1x. */
			  |((cra >> (CRABIT_CLKSRC_B - STDBIT_CLKPOL)) & STDMSK_CLKPOL));	/*    Set ClkPol equal to Timer count direction (ClkSrcB<0>). */

	else			/*  If Counter mode (ClkSrcB<1> == 0): */
		setup |= ((CLKSRC_COUNTER << STDBIT_CLKSRC)	/*    Indicate Timer mode. */
			  |((crb >> (CRBBIT_CLKMULT_B - STDBIT_CLKMULT)) & STDMSK_CLKMULT)	/*    Clock multiplier is passed through. */
			  |((crb << (STDBIT_CLKPOL - CRBBIT_CLKPOL_B)) & STDMSK_CLKPOL));	/*    Clock polarity is passed through. */

	/*  Return adjusted counter setup. */
	return setup;
}

/*
 * Set the operating mode for the specified counter.  The setup
 * parameter is treated as a COUNTER_SETUP data type.  The following
 * parameters are programmable (all other parms are ignored): ClkMult,
 * ClkPol, ClkEnab, IndexSrc, IndexPol, LoadSrc.
 */

static void SetMode_A(struct comedi_device *dev, struct enc_private *k,
		      uint16_t Setup, uint16_t DisableIntSrc)
{
	struct s626_private *devpriv = dev->private;
	register uint16_t cra;
	register uint16_t crb;
	register uint16_t setup = Setup;	/*  Cache the Standard Setup. */

	/*  Initialize CRA and CRB images. */
	cra = ((setup & CRAMSK_LOADSRC_A)	/*  Preload trigger is passed through. */
	       |((setup & STDMSK_INDXSRC) >> (STDBIT_INDXSRC - (CRABIT_INDXSRC_A + 1))));	/*  IndexSrc is restricted to ENC_X or IndxPol. */

	crb = (CRBMSK_INTRESETCMD | CRBMSK_INTRESET_A	/*  Reset any pending CounterA event captures. */
	       | ((setup & STDMSK_CLKENAB) << (CRBBIT_CLKENAB_A - STDBIT_CLKENAB)));	/*  Clock enable is passed through. */

	/*  Force IntSrc to Disabled if DisableIntSrc is asserted. */
	if (!DisableIntSrc)
		cra |= ((setup & STDMSK_INTSRC) >> (STDBIT_INTSRC -
						    CRABIT_INTSRC_A));

	/*  Populate all mode-dependent attributes of CRA & CRB images. */
	switch ((setup & STDMSK_CLKSRC) >> STDBIT_CLKSRC) {
	case CLKSRC_EXTENDER:	/*  Extender Mode: Force to Timer mode */
		/*  (Extender valid only for B counters). */

	case CLKSRC_TIMER:	/*  Timer Mode: */
		cra |= ((2 << CRABIT_CLKSRC_A)	/*    ClkSrcA<1> selects system clock */
			|((setup & STDMSK_CLKPOL) >> (STDBIT_CLKPOL - CRABIT_CLKSRC_A))	/*      with count direction (ClkSrcA<0>) obtained from ClkPol. */
			|(1 << CRABIT_CLKPOL_A)	/*    ClkPolA behaves as always-on clock enable. */
			|(MULT_X1 << CRABIT_CLKMULT_A));	/*    ClkMult must be 1x. */
		break;

	default:		/*  Counter Mode: */
		cra |= (CLKSRC_COUNTER	/*    Select ENC_C and ENC_D as clock/direction inputs. */
			| ((setup & STDMSK_CLKPOL) << (CRABIT_CLKPOL_A - STDBIT_CLKPOL))	/*    Clock polarity is passed through. */
			|(((setup & STDMSK_CLKMULT) == (MULT_X0 << STDBIT_CLKMULT)) ?	/*    Force multiplier to x1 if not legal, otherwise pass through. */
			  (MULT_X1 << CRABIT_CLKMULT_A) :
			  ((setup & STDMSK_CLKMULT) << (CRABIT_CLKMULT_A -
							STDBIT_CLKMULT))));
	}

	/*  Force positive index polarity if IndxSrc is software-driven only, */
	/*  otherwise pass it through. */
	if (~setup & STDMSK_INDXSRC)
		cra |= ((setup & STDMSK_INDXPOL) << (CRABIT_INDXPOL_A -
						     STDBIT_INDXPOL));

	/*  If IntSrc has been forced to Disabled, update the MISC2 interrupt */
	/*  enable mask to indicate the counter interrupt is disabled. */
	if (DisableIntSrc)
		devpriv->CounterIntEnabs &= ~k->MyEventBits[3];

	/*  While retaining CounterB and LatchSrc configurations, program the */
	/*  new counter operating mode. */
	DEBIreplace(dev, k->MyCRA, CRAMSK_INDXSRC_B | CRAMSK_CLKSRC_B, cra);
	DEBIreplace(dev, k->MyCRB,
		    (uint16_t) (~(CRBMSK_INTCTRL | CRBMSK_CLKENAB_A)), crb);
}

static void SetMode_B(struct comedi_device *dev, struct enc_private *k,
		      uint16_t Setup, uint16_t DisableIntSrc)
{
	struct s626_private *devpriv = dev->private;
	register uint16_t cra;
	register uint16_t crb;
	register uint16_t setup = Setup;	/*  Cache the Standard Setup. */

	/*  Initialize CRA and CRB images. */
	cra = ((setup & STDMSK_INDXSRC) << ((CRABIT_INDXSRC_B + 1) - STDBIT_INDXSRC));	/*  IndexSrc field is restricted to ENC_X or IndxPol. */

	crb = (CRBMSK_INTRESETCMD | CRBMSK_INTRESET_B	/*  Reset event captures and disable interrupts. */
	       | ((setup & STDMSK_CLKENAB) << (CRBBIT_CLKENAB_B - STDBIT_CLKENAB))	/*  Clock enable is passed through. */
	       |((setup & STDMSK_LOADSRC) >> (STDBIT_LOADSRC - CRBBIT_LOADSRC_B)));	/*  Preload trigger source is passed through. */

	/*  Force IntSrc to Disabled if DisableIntSrc is asserted. */
	if (!DisableIntSrc)
		crb |= ((setup & STDMSK_INTSRC) >> (STDBIT_INTSRC -
						    CRBBIT_INTSRC_B));

	/*  Populate all mode-dependent attributes of CRA & CRB images. */
	switch ((setup & STDMSK_CLKSRC) >> STDBIT_CLKSRC) {
	case CLKSRC_TIMER:	/*  Timer Mode: */
		cra |= ((2 << CRABIT_CLKSRC_B)	/*    ClkSrcB<1> selects system clock */
			|((setup & STDMSK_CLKPOL) << (CRABIT_CLKSRC_B - STDBIT_CLKPOL)));	/*      with direction (ClkSrcB<0>) obtained from ClkPol. */
		crb |= ((1 << CRBBIT_CLKPOL_B)	/*    ClkPolB behaves as always-on clock enable. */
			|(MULT_X1 << CRBBIT_CLKMULT_B));	/*    ClkMultB must be 1x. */
		break;

	case CLKSRC_EXTENDER:	/*  Extender Mode: */
		cra |= ((2 << CRABIT_CLKSRC_B)	/*    ClkSrcB source is OverflowA (same as "timer") */
			|((setup & STDMSK_CLKPOL) << (CRABIT_CLKSRC_B - STDBIT_CLKPOL)));	/*      with direction obtained from ClkPol. */
		crb |= ((1 << CRBBIT_CLKPOL_B)	/*    ClkPolB controls IndexB -- always set to active. */
			|(MULT_X0 << CRBBIT_CLKMULT_B));	/*    ClkMultB selects OverflowA as the clock source. */
		break;

	default:		/*  Counter Mode: */
		cra |= (CLKSRC_COUNTER << CRABIT_CLKSRC_B);	/*    Select ENC_C and ENC_D as clock/direction inputs. */
		crb |= (((setup & STDMSK_CLKPOL) >> (STDBIT_CLKPOL - CRBBIT_CLKPOL_B))	/*    ClkPol is passed through. */
			|(((setup & STDMSK_CLKMULT) == (MULT_X0 << STDBIT_CLKMULT)) ?	/*    Force ClkMult to x1 if not legal, otherwise pass through. */
			  (MULT_X1 << CRBBIT_CLKMULT_B) :
			  ((setup & STDMSK_CLKMULT) << (CRBBIT_CLKMULT_B -
							STDBIT_CLKMULT))));
	}

	/*  Force positive index polarity if IndxSrc is software-driven only, */
	/*  otherwise pass it through. */
	if (~setup & STDMSK_INDXSRC)
		crb |= ((setup & STDMSK_INDXPOL) >> (STDBIT_INDXPOL -
						     CRBBIT_INDXPOL_B));

	/*  If IntSrc has been forced to Disabled, update the MISC2 interrupt */
	/*  enable mask to indicate the counter interrupt is disabled. */
	if (DisableIntSrc)
		devpriv->CounterIntEnabs &= ~k->MyEventBits[3];

	/*  While retaining CounterA and LatchSrc configurations, program the */
	/*  new counter operating mode. */
	DEBIreplace(dev, k->MyCRA,
		    (uint16_t) (~(CRAMSK_INDXSRC_B | CRAMSK_CLKSRC_B)), cra);
	DEBIreplace(dev, k->MyCRB, CRBMSK_CLKENAB_A | CRBMSK_LATCHSRC, crb);
}

/*  Return/set a counter's enable.  enab: 0=always enabled, 1=enabled by index. */

static void SetEnable_A(struct comedi_device *dev, struct enc_private *k,
			uint16_t enab)
{
	DEBIreplace(dev, k->MyCRB,
		    (uint16_t) (~(CRBMSK_INTCTRL | CRBMSK_CLKENAB_A)),
		    (uint16_t) (enab << CRBBIT_CLKENAB_A));
}

static void SetEnable_B(struct comedi_device *dev, struct enc_private *k,
			uint16_t enab)
{
	DEBIreplace(dev, k->MyCRB,
		    (uint16_t) (~(CRBMSK_INTCTRL | CRBMSK_CLKENAB_B)),
		    (uint16_t) (enab << CRBBIT_CLKENAB_B));
}

static uint16_t GetEnable_A(struct comedi_device *dev, struct enc_private *k)
{
	return (DEBIread(dev, k->MyCRB) >> CRBBIT_CLKENAB_A) & 1;
}

static uint16_t GetEnable_B(struct comedi_device *dev, struct enc_private *k)
{
	return (DEBIread(dev, k->MyCRB) >> CRBBIT_CLKENAB_B) & 1;
}

/*
 * static uint16_t GetLatchSource(struct comedi_device *dev, struct enc_private *k )
 * {
 *	return ( DEBIread( dev, k->MyCRB) >> CRBBIT_LATCHSRC ) & 3;
 * }
 */

/*
 * Return/set the event that will trigger transfer of the preload
 * register into the counter.  0=ThisCntr_Index, 1=ThisCntr_Overflow,
 * 2=OverflowA (B counters only), 3=disabled.
 */

static void SetLoadTrig_A(struct comedi_device *dev, struct enc_private *k,
			  uint16_t Trig)
{
	DEBIreplace(dev, k->MyCRA, (uint16_t) (~CRAMSK_LOADSRC_A),
		    (uint16_t) (Trig << CRABIT_LOADSRC_A));
}

static void SetLoadTrig_B(struct comedi_device *dev, struct enc_private *k,
			  uint16_t Trig)
{
	DEBIreplace(dev, k->MyCRB,
		    (uint16_t) (~(CRBMSK_LOADSRC_B | CRBMSK_INTCTRL)),
		    (uint16_t) (Trig << CRBBIT_LOADSRC_B));
}

static uint16_t GetLoadTrig_A(struct comedi_device *dev, struct enc_private *k)
{
	return (DEBIread(dev, k->MyCRA) >> CRABIT_LOADSRC_A) & 3;
}

static uint16_t GetLoadTrig_B(struct comedi_device *dev, struct enc_private *k)
{
	return (DEBIread(dev, k->MyCRB) >> CRBBIT_LOADSRC_B) & 3;
}

/* Return/set counter interrupt source and clear any captured
 * index/overflow events.  IntSource: 0=Disabled, 1=OverflowOnly,
 * 2=IndexOnly, 3=IndexAndOverflow.
 */

static void SetIntSrc_A(struct comedi_device *dev, struct enc_private *k,
			uint16_t IntSource)
{
	struct s626_private *devpriv = dev->private;

	/*  Reset any pending counter overflow or index captures. */
	DEBIreplace(dev, k->MyCRB, (uint16_t) (~CRBMSK_INTCTRL),
		    CRBMSK_INTRESETCMD | CRBMSK_INTRESET_A);

	/*  Program counter interrupt source. */
	DEBIreplace(dev, k->MyCRA, ~CRAMSK_INTSRC_A,
		    (uint16_t) (IntSource << CRABIT_INTSRC_A));

	/*  Update MISC2 interrupt enable mask. */
	devpriv->CounterIntEnabs =
	    (devpriv->CounterIntEnabs & ~k->
	     MyEventBits[3]) | k->MyEventBits[IntSource];
}

static void SetIntSrc_B(struct comedi_device *dev, struct enc_private *k,
			uint16_t IntSource)
{
	struct s626_private *devpriv = dev->private;
	uint16_t crb;

	/*  Cache writeable CRB register image. */
	crb = DEBIread(dev, k->MyCRB) & ~CRBMSK_INTCTRL;

	/*  Reset any pending counter overflow or index captures. */
	DEBIwrite(dev, k->MyCRB,
		  (uint16_t) (crb | CRBMSK_INTRESETCMD | CRBMSK_INTRESET_B));

	/*  Program counter interrupt source. */
	DEBIwrite(dev, k->MyCRB,
		  (uint16_t) ((crb & ~CRBMSK_INTSRC_B) | (IntSource <<
							  CRBBIT_INTSRC_B)));

	/*  Update MISC2 interrupt enable mask. */
	devpriv->CounterIntEnabs =
	    (devpriv->CounterIntEnabs & ~k->
	     MyEventBits[3]) | k->MyEventBits[IntSource];
}

static uint16_t GetIntSrc_A(struct comedi_device *dev, struct enc_private *k)
{
	return (DEBIread(dev, k->MyCRA) >> CRABIT_INTSRC_A) & 3;
}

static uint16_t GetIntSrc_B(struct comedi_device *dev, struct enc_private *k)
{
	return (DEBIread(dev, k->MyCRB) >> CRBBIT_INTSRC_B) & 3;
}

/*  Return/set the clock multiplier. */

/* static void SetClkMult(struct comedi_device *dev, struct enc_private *k, uint16_t value )  */
/* { */
/*   k->SetMode(dev, k, (uint16_t)( ( k->GetMode(dev, k ) & ~STDMSK_CLKMULT ) | ( value << STDBIT_CLKMULT ) ), FALSE ); */
/* } */

/* static uint16_t GetClkMult(struct comedi_device *dev, struct enc_private *k )  */
/* { */
/*   return ( k->GetMode(dev, k ) >> STDBIT_CLKMULT ) & 3; */
/* } */

/* Return/set the clock polarity. */

/* static void SetClkPol( struct comedi_device *dev,struct enc_private *k, uint16_t value )  */
/* { */
/*   k->SetMode(dev, k, (uint16_t)( ( k->GetMode(dev, k ) & ~STDMSK_CLKPOL ) | ( value << STDBIT_CLKPOL ) ), FALSE ); */
/* } */

/* static uint16_t GetClkPol(struct comedi_device *dev, struct enc_private *k )  */
/* { */
/*   return ( k->GetMode(dev, k ) >> STDBIT_CLKPOL ) & 1; */
/* } */

/* Return/set the clock source.  */

/* static void SetClkSrc( struct comedi_device *dev,struct enc_private *k, uint16_t value )  */
/* { */
/*   k->SetMode(dev, k, (uint16_t)( ( k->GetMode(dev, k ) & ~STDMSK_CLKSRC ) | ( value << STDBIT_CLKSRC ) ), FALSE ); */
/* } */

/* static uint16_t GetClkSrc( struct comedi_device *dev,struct enc_private *k )  */
/* { */
/*   return ( k->GetMode(dev, k ) >> STDBIT_CLKSRC ) & 3; */
/* } */

/* Return/set the index polarity. */

/* static void SetIndexPol(struct comedi_device *dev, struct enc_private *k, uint16_t value )  */
/* { */
/*   k->SetMode(dev, k, (uint16_t)( ( k->GetMode(dev, k ) & ~STDMSK_INDXPOL ) | ( (value != 0) << STDBIT_INDXPOL ) ), FALSE ); */
/* } */

/* static uint16_t GetIndexPol(struct comedi_device *dev, struct enc_private *k )  */
/* { */
/*   return ( k->GetMode(dev, k ) >> STDBIT_INDXPOL ) & 1; */
/* } */

/*  Return/set the index source. */

/* static void SetIndexSrc(struct comedi_device *dev, struct enc_private *k, uint16_t value )  */
/* { */
/*   k->SetMode(dev, k, (uint16_t)( ( k->GetMode(dev, k ) & ~STDMSK_INDXSRC ) | ( (value != 0) << STDBIT_INDXSRC ) ), FALSE ); */
/* } */

/* static uint16_t GetIndexSrc(struct comedi_device *dev, struct enc_private *k )  */
/* { */
/*   return ( k->GetMode(dev, k ) >> STDBIT_INDXSRC ) & 1; */
/* } */

/*  Generate an index pulse. */

static void PulseIndex_A(struct comedi_device *dev, struct enc_private *k)
{
	register uint16_t cra;

	cra = DEBIread(dev, k->MyCRA);	/*  Pulse index. */
	DEBIwrite(dev, k->MyCRA, (uint16_t) (cra ^ CRAMSK_INDXPOL_A));
	DEBIwrite(dev, k->MyCRA, cra);
}

static void PulseIndex_B(struct comedi_device *dev, struct enc_private *k)
{
	register uint16_t crb;

	crb = DEBIread(dev, k->MyCRB) & ~CRBMSK_INTCTRL;	/*  Pulse index. */
	DEBIwrite(dev, k->MyCRB, (uint16_t) (crb ^ CRBMSK_INDXPOL_B));
	DEBIwrite(dev, k->MyCRB, crb);
}

static struct enc_private enc_private_data[] = {
	{
		.GetEnable	= GetEnable_A,
		.GetIntSrc	= GetIntSrc_A,
		.GetLoadTrig	= GetLoadTrig_A,
		.GetMode	= GetMode_A,
		.PulseIndex	= PulseIndex_A,
		.SetEnable	= SetEnable_A,
		.SetIntSrc	= SetIntSrc_A,
		.SetLoadTrig	= SetLoadTrig_A,
		.SetMode	= SetMode_A,
		.ResetCapFlags	= ResetCapFlags_A,
		.MyCRA		= LP_CR0A,
		.MyCRB		= LP_CR0B,
		.MyLatchLsw	= LP_CNTR0ALSW,
		.MyEventBits	= EVBITS(0),
	}, {
		.GetEnable	= GetEnable_A,
		.GetIntSrc	= GetIntSrc_A,
		.GetLoadTrig	= GetLoadTrig_A,
		.GetMode	= GetMode_A,
		.PulseIndex	= PulseIndex_A,
		.SetEnable	= SetEnable_A,
		.SetIntSrc	= SetIntSrc_A,
		.SetLoadTrig	= SetLoadTrig_A,
		.SetMode	= SetMode_A,
		.ResetCapFlags	= ResetCapFlags_A,
		.MyCRA		= LP_CR1A,
		.MyCRB		= LP_CR1B,
		.MyLatchLsw	= LP_CNTR1ALSW,
		.MyEventBits	= EVBITS(1),
	}, {
		.GetEnable	= GetEnable_A,
		.GetIntSrc	= GetIntSrc_A,
		.GetLoadTrig	= GetLoadTrig_A,
		.GetMode	= GetMode_A,
		.PulseIndex	= PulseIndex_A,
		.SetEnable	= SetEnable_A,
		.SetIntSrc	= SetIntSrc_A,
		.SetLoadTrig	= SetLoadTrig_A,
		.SetMode	= SetMode_A,
		.ResetCapFlags	= ResetCapFlags_A,
		.MyCRA		= LP_CR2A,
		.MyCRB		= LP_CR2B,
		.MyLatchLsw	= LP_CNTR2ALSW,
		.MyEventBits	= EVBITS(2),
	}, {
		.GetEnable	= GetEnable_B,
		.GetIntSrc	= GetIntSrc_B,
		.GetLoadTrig	= GetLoadTrig_B,
		.GetMode	= GetMode_B,
		.PulseIndex	= PulseIndex_B,
		.SetEnable	= SetEnable_B,
		.SetIntSrc	= SetIntSrc_B,
		.SetLoadTrig	= SetLoadTrig_B,
		.SetMode	= SetMode_B,
		.ResetCapFlags	= ResetCapFlags_B,
		.MyCRA		= LP_CR0A,
		.MyCRB		= LP_CR0B,
		.MyLatchLsw	= LP_CNTR0BLSW,
		.MyEventBits	= EVBITS(3),
	}, {
		.GetEnable	= GetEnable_B,
		.GetIntSrc	= GetIntSrc_B,
		.GetLoadTrig	= GetLoadTrig_B,
		.GetMode	= GetMode_B,
		.PulseIndex	= PulseIndex_B,
		.SetEnable	= SetEnable_B,
		.SetIntSrc	= SetIntSrc_B,
		.SetLoadTrig	= SetLoadTrig_B,
		.SetMode	= SetMode_B,
		.ResetCapFlags	= ResetCapFlags_B,
		.MyCRA		= LP_CR1A,
		.MyCRB		= LP_CR1B,
		.MyLatchLsw	= LP_CNTR1BLSW,
		.MyEventBits	= EVBITS(4),
	}, {
		.GetEnable	= GetEnable_B,
		.GetIntSrc	= GetIntSrc_B,
		.GetLoadTrig	= GetLoadTrig_B,
		.GetMode	= GetMode_B,
		.PulseIndex	= PulseIndex_B,
		.SetEnable	= SetEnable_B,
		.SetIntSrc	= SetIntSrc_B,
		.SetLoadTrig	= SetLoadTrig_B,
		.SetMode	= SetMode_B,
		.ResetCapFlags	= ResetCapFlags_B,
		.MyCRA		= LP_CR2A,
		.MyCRB		= LP_CR2B,
		.MyLatchLsw	= LP_CNTR2BLSW,
		.MyEventBits	= EVBITS(5),
	},
};

static void CountersInit(struct comedi_device *dev)
{
	int chan;
	struct enc_private *k;
	uint16_t Setup = (LOADSRC_INDX << BF_LOADSRC) |	/*  Preload upon */
	    /*  index. */
	    (INDXSRC_SOFT << BF_INDXSRC) |	/*  Disable hardware index. */
	    (CLKSRC_COUNTER << BF_CLKSRC) |	/*  Operating mode is counter. */
	    (CLKPOL_POS << BF_CLKPOL) |	/*  Active high clock. */
	    (CNTDIR_UP << BF_CLKPOL) |	/*  Count direction is up. */
	    (CLKMULT_1X << BF_CLKMULT) |	/*  Clock multiplier is 1x. */
	    (CLKENAB_INDEX << BF_CLKENAB);	/*  Enabled by index */

	/*  Disable all counter interrupts and clear any captured counter events. */
	for (chan = 0; chan < S626_ENCODER_CHANNELS; chan++) {
		k = &encpriv[chan];
		k->SetMode(dev, k, Setup, TRUE);
		k->SetIntSrc(dev, k, 0);
		k->ResetCapFlags(dev, k);
		k->SetEnable(dev, k, CLKENAB_ALWAYS);
	}
}

static int s626_allocate_dma_buffers(struct comedi_device *dev)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	struct s626_private *devpriv = dev->private;
	void *addr;
	dma_addr_t appdma;

	addr = pci_alloc_consistent(pcidev, DMABUF_SIZE, &appdma);
	if (!addr)
		return -ENOMEM;
	devpriv->ANABuf.LogicalBase = addr;
	devpriv->ANABuf.PhysicalBase = appdma;

	addr = pci_alloc_consistent(pcidev, DMABUF_SIZE, &appdma);
	if (!addr)
		return -ENOMEM;
	devpriv->RPSBuf.LogicalBase = addr;
	devpriv->RPSBuf.PhysicalBase = appdma;

	return 0;
}

static void s626_initialize(struct comedi_device *dev)
{
	struct s626_private *devpriv = dev->private;
	dma_addr_t pPhysBuf;
	uint16_t chan;
	int i;

	/* Enable DEBI and audio pins, enable I2C interface */
	MC_ENABLE(P_MC1, MC1_DEBI | MC1_AUDIO | MC1_I2C);

	/*
	 *  Configure DEBI operating mode
	 *
	 *   Local bus is 16 bits wide
	 *   Declare DEBI transfer timeout interval
	 *   Set up byte lane steering
	 *   Intel-compatible local bus (DEBI never times out)
	 */
	WR7146(P_DEBICFG, DEBI_CFG_SLAVE16 |
			  (DEBI_TOUT << DEBI_CFG_TOUT_BIT) |
			  DEBI_SWAP | DEBI_CFG_INTEL);

	/* Disable MMU paging */
	WR7146(P_DEBIPAGE, DEBI_PAGE_DISABLE);

	/* Init GPIO so that ADC Start* is negated */
	WR7146(P_GPIO, GPIO_BASE | GPIO1_HI);

	/* I2C device address for onboard eeprom (revb) */
	devpriv->I2CAdrs = 0xA0;

	/*
	 * Issue an I2C ABORT command to halt any I2C
	 * operation in progress and reset BUSY flag.
	 */
	WR7146(P_I2CSTAT, I2C_CLKSEL | I2C_ABORT);
	MC_ENABLE(P_MC2, MC2_UPLD_IIC);
	while ((RR7146(P_MC2) & MC2_UPLD_IIC) == 0)
		;

	/*
	 * Per SAA7146 data sheet, write to STATUS
	 * reg twice to reset all  I2C error flags.
	 */
	for (i = 0; i < 2; i++) {
		WR7146(P_I2CSTAT, I2C_CLKSEL);
		MC_ENABLE(P_MC2, MC2_UPLD_IIC);
		while (!MC_TEST(P_MC2, MC2_UPLD_IIC))
			;
	}

	/*
	 * Init audio interface functional attributes: set DAC/ADC
	 * serial clock rates, invert DAC serial clock so that
	 * DAC data setup times are satisfied, enable DAC serial
	 * clock out.
	 */
	WR7146(P_ACON2, ACON2_INIT);

	/*
	 * Set up TSL1 slot list, which is used to control the
	 * accumulation of ADC data: RSD1 = shift data in on SD1.
	 * SIB_A1  = store data uint8_t at next available location
	 * in FB BUFFER1 register.
	 */
	WR7146(P_TSL1, RSD1 | SIB_A1);
	WR7146(P_TSL1 + 4, RSD1 | SIB_A1 | EOS);

	/* Enable TSL1 slot list so that it executes all the time */
	WR7146(P_ACON1, ACON1_ADCSTART);

	/*
	 * Initialize RPS registers used for ADC
	 */

	/* Physical start of RPS program */
	WR7146(P_RPSADDR1, (uint32_t)devpriv->RPSBuf.PhysicalBase);
	/* RPS program performs no explicit mem writes */
	WR7146(P_RPSPAGE1, 0);
	/* Disable RPS timeouts */
	WR7146(P_RPS1_TOUT, 0);

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
	uint8_t PollList;
	uint16_t AdcData;
	uint16_t StartVal;
	uint16_t index;
	unsigned int data[16];

	/* Create a simple polling list for analog input channel 0 */
	PollList = EOPL;
	ResetADC(dev, &PollList);

	/* Get initial ADC value */
	s626_ai_rinsn(dev, dev->subdevices, NULL, data);
	StartVal = data[0];

	/*
	 * VERSION 2.01 CHANGE: TIMEOUT ADDED TO PREVENT HANGED EXECUTION.
	 *
	 * Invoke ADCs until the new ADC value differs from the initial
	 * value or a timeout occurs.  The timeout protects against the
	 * possibility that the driver is restarting and the ADC data is a
	 * fixed value resulting from the applied ADC analog input being
	 * unusually quiet or at the rail.
	 */
	for (index = 0; index < 500; index++) {
		s626_ai_rinsn(dev, dev->subdevices, NULL, data);
		AdcData = data[0];
		if (AdcData != StartVal)
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
	WR7146(P_PCI_BT_A, 0);

	/*
	 * Init Audio2's output DMA physical addresses.  The protection
	 * address is set to 1 DWORD past the base address so that a
	 * single DWORD will be transferred each time a DMA transfer is
	 * enabled.
	 */
	pPhysBuf = devpriv->ANABuf.PhysicalBase +
		   (DAC_WDMABUF_OS * sizeof(uint32_t));
	WR7146(P_BASEA2_OUT, (uint32_t) pPhysBuf);
	WR7146(P_PROTA2_OUT, (uint32_t) (pPhysBuf + sizeof(uint32_t)));

	/*
	 * Cache Audio2's output DMA buffer logical address.  This is
	 * where DAC data is buffered for A2 output DMA transfers.
	 */
	devpriv->pDacWBuf = (uint32_t *)devpriv->ANABuf.LogicalBase +
			    DAC_WDMABUF_OS;

	/*
	 * Audio2's output channels does not use paging.  The
	 * protection violation handling bit is set so that the
	 * DMAC will automatically halt and its PCI address pointer
	 * will be reset when the protection address is reached.
	 */
	WR7146(P_PAGEA2_OUT, 8);

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
	SETVECT(0, XSD2 | RSD3 | SIB_A2 | EOS);

	/*
	 * Initialize slot 1, which is constant.  Slot 1 causes a
	 * DWORD to be transferred from audio channel 2's output FIFO
	 * to the FIFO's output buffer so that it can be serialized
	 * and sent to the DAC during subsequent slots.  All remaining
	 * slots are dynamically populated as required by the target
	 * DAC device.
	 */

	/* Slot 1: Fetch DWORD from Audio2's output FIFO */
	SETVECT(1, LF_A2);

	/* Start DAC's audio interface (TSL2) running */
	WR7146(P_ACON1, ACON1_DACSTART);

	/*
	 * Init Trim DACs to calibrated values.  Do it twice because the
	 * SAA7146 audio channel does not always reset properly and
	 * sometimes causes the first few TrimDAC writes to malfunction.
	 */
	LoadTrimDACs(dev);
	LoadTrimDACs(dev);

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
	for (chan = 0; chan < S626_DAC_CHANNELS; chan++)
		SetDAC(dev, chan, 0);

	/* Init counters */
	CountersInit(dev);

	/*
	 * Without modifying the state of the Battery Backup enab, disable
	 * the watchdog timer, set DIO channels 0-5 to operate in the
	 * standard DIO (vs. counter overflow) mode, disable the battery
	 * charger, and reset the watchdog interval selector to zero.
	 */
	WriteMISC2(dev, (uint16_t)(DEBIread(dev, LP_RDMISC2) &
				   MISC2_BATT_ENABLE));

	/* Initialize the digital I/O subsystem */
	s626_dio_init(dev);

	/* enable interrupt test */
	/* writel(IRQ_GPIO3 | IRQ_RPS1, devpriv->base_addr + P_IER); */
}

static int s626_auto_attach(struct comedi_device *dev,
				      unsigned long context_unused)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	struct s626_private *devpriv;
	struct comedi_subdevice *s;
	int ret;

	dev->board_name = dev->driver->driver_name;

	devpriv = kzalloc(sizeof(*devpriv), GFP_KERNEL);
	if (!devpriv)
		return -ENOMEM;
	dev->private = devpriv;

	ret = comedi_pci_enable(pcidev, dev->board_name);
	if (ret)
		return ret;
	dev->iobase = 1;	/* detach needs this */

	devpriv->base_addr = ioremap(pci_resource_start(pcidev, 0),
				     pci_resource_len(pcidev, 0));
	if (!devpriv->base_addr)
		return -ENOMEM;

	/* disable master interrupt */
	writel(0, devpriv->base_addr + P_IER);

	/* soft reset */
	writel(MC1_SOFT_RESET, devpriv->base_addr + P_MC1);

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

	s = dev->subdevices + 0;
	/* analog input subdevice */
	dev->read_subdev = s;
	/* we support single-ended (ground) and differential */
	s->type = COMEDI_SUBD_AI;
	s->subdev_flags = SDF_READABLE | SDF_DIFF | SDF_CMD_READ;
	s->n_chan = S626_ADC_CHANNELS;
	s->maxdata = (0xffff >> 2);
	s->range_table = &s626_range_table;
	s->len_chanlist = S626_ADC_CHANNELS;
	s->insn_config = s626_ai_insn_config;
	s->insn_read = s626_ai_insn_read;
	s->do_cmd = s626_ai_cmd;
	s->do_cmdtest = s626_ai_cmdtest;
	s->cancel = s626_ai_cancel;

	s = dev->subdevices + 1;
	/* analog output subdevice */
	s->type = COMEDI_SUBD_AO;
	s->subdev_flags = SDF_WRITABLE | SDF_READABLE;
	s->n_chan = S626_DAC_CHANNELS;
	s->maxdata = (0x3fff);
	s->range_table = &range_bipolar10;
	s->insn_write = s626_ao_winsn;
	s->insn_read = s626_ao_rinsn;

	s = dev->subdevices + 2;
	/* digital I/O subdevice */
	s->type = COMEDI_SUBD_DIO;
	s->subdev_flags = SDF_WRITABLE | SDF_READABLE;
	s->n_chan = 16;
	s->maxdata = 1;
	s->io_bits = 0xffff;
	s->private = &dio_private_A;
	s->range_table = &range_digital;
	s->insn_config = s626_dio_insn_config;
	s->insn_bits = s626_dio_insn_bits;

	s = dev->subdevices + 3;
	/* digital I/O subdevice */
	s->type = COMEDI_SUBD_DIO;
	s->subdev_flags = SDF_WRITABLE | SDF_READABLE;
	s->n_chan = 16;
	s->maxdata = 1;
	s->io_bits = 0xffff;
	s->private = &dio_private_B;
	s->range_table = &range_digital;
	s->insn_config = s626_dio_insn_config;
	s->insn_bits = s626_dio_insn_bits;

	s = dev->subdevices + 4;
	/* digital I/O subdevice */
	s->type = COMEDI_SUBD_DIO;
	s->subdev_flags = SDF_WRITABLE | SDF_READABLE;
	s->n_chan = 16;
	s->maxdata = 1;
	s->io_bits = 0xffff;
	s->private = &dio_private_C;
	s->range_table = &range_digital;
	s->insn_config = s626_dio_insn_config;
	s->insn_bits = s626_dio_insn_bits;

	s = dev->subdevices + 5;
	/* encoder (counter) subdevice */
	s->type = COMEDI_SUBD_COUNTER;
	s->subdev_flags = SDF_WRITABLE | SDF_READABLE | SDF_LSAMPL;
	s->n_chan = S626_ENCODER_CHANNELS;
	s->private = enc_private_data;
	s->insn_config = s626_enc_insn_config;
	s->insn_read = s626_enc_insn_read;
	s->insn_write = s626_enc_insn_write;
	s->maxdata = 0xffffff;
	s->range_table = &range_unknown;

	s626_initialize(dev);

	dev_info(dev->class_dev, "%s attached\n", dev->board_name);

	return 0;
}

static void s626_detach(struct comedi_device *dev)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	struct s626_private *devpriv = dev->private;

	if (devpriv) {
		/* stop ai_command */
		devpriv->ai_cmd_running = 0;

		if (devpriv->base_addr) {
			/* interrupt mask */
			WR7146(P_IER, 0);	/*  Disable master interrupt. */
			WR7146(P_ISR, IRQ_GPIO3 | IRQ_RPS1);	/*  Clear board's IRQ status flag. */

			/*  Disable the watchdog timer and battery charger. */
			WriteMISC2(dev, 0);

			/*  Close all interfaces on 7146 device. */
			WR7146(P_MC1, MC1_SHUTDOWN);
			WR7146(P_ACON1, ACON1_BASE);

			CloseDMAB(dev, &devpriv->RPSBuf, DMABUF_SIZE);
			CloseDMAB(dev, &devpriv->ANABuf, DMABUF_SIZE);
		}

		if (dev->irq)
			free_irq(dev->irq, dev);
		if (devpriv->base_addr)
			iounmap(devpriv->base_addr);
	}
	if (pcidev) {
		if (dev->iobase)
			comedi_pci_disable(pcidev);
	}
}

static struct comedi_driver s626_driver = {
	.driver_name	= "s626",
	.module		= THIS_MODULE,
	.auto_attach	= s626_auto_attach,
	.detach		= s626_detach,
};

static int s626_pci_probe(struct pci_dev *dev,
				    const struct pci_device_id *ent)
{
	return comedi_pci_auto_config(dev, &s626_driver);
}

static void __devexit s626_pci_remove(struct pci_dev *dev)
{
	comedi_pci_auto_unconfig(dev);
}

/*
 * For devices with vendor:device id == 0x1131:0x7146 you must specify
 * also subvendor:subdevice ids, because otherwise it will conflict with
 * Philips SAA7146 media/dvb based cards.
 */
static DEFINE_PCI_DEVICE_TABLE(s626_pci_table) = {
	{ PCI_VENDOR_ID_S626, PCI_DEVICE_ID_S626,
		PCI_SUBVENDOR_ID_S626, PCI_SUBDEVICE_ID_S626, 0, 0, 0 },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, s626_pci_table);

static struct pci_driver s626_pci_driver = {
	.name		= "s626",
	.id_table	= s626_pci_table,
	.probe		= s626_pci_probe,
	.remove		= s626_pci_remove,
};
module_comedi_pci_driver(s626_driver, s626_pci_driver);

MODULE_AUTHOR("Gianluca Palli <gpalli@deis.unibo.it>");
MODULE_DESCRIPTION("Sensoray 626 Comedi driver module");
MODULE_LICENSE("GPL");

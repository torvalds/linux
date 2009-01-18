/*
  comedi/drivers/s626.h
  Sensoray s626 Comedi driver, header file

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
  Driver: s626.o (s626.ko)
  Description: Sensoray 626 driver
  Devices: Sensoray s626
  Authors: Gianluca Palli <gpalli@deis.unibo.it>,
  Updated: Thu, 12 Jul 2005
  Status: experimental

  Configuration Options:
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

#ifdef _DEBUG_
#define DEBUG(...);        rt_printk(__VA_ARGS__);
#else
#define DEBUG(...)
#endif

#if !defined(TRUE)
#define TRUE    (1)
#endif

#if !defined(FALSE)
#define FALSE   (0)
#endif

#if !defined(EXTERN)
#if defined(__cplusplus)
#define EXTERN extern "C"
#else
#define EXTERN extern
#endif
#endif

#if !defined(INLINE)
#define INLINE static __inline
#endif

/////////////////////////////////////////////////////
#include<linux/slab.h>

#define S626_SIZE 0x0200
#define SIZEOF_ADDRESS_SPACE		0x0200
#define DMABUF_SIZE			4096	// 4k pages

#define S626_ADC_CHANNELS       16
#define S626_DAC_CHANNELS       4
#define S626_ENCODER_CHANNELS   6
#define S626_DIO_CHANNELS       48
#define S626_DIO_BANKS		3	// Number of DIO groups.
#define S626_DIO_EXTCHANS	40	// Number of
					// extended-capability
					// DIO channels.

#define NUM_TRIMDACS	12	// Number of valid TrimDAC channels.

// PCI bus interface types.
#define INTEL				1	// Intel bus type.
#define MOTOROLA			2	// Motorola bus type.

//////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////
#define PLATFORM		INTEL	// *** SELECT PLATFORM TYPE ***
//////////////////////////////////////////////////////////

#define RANGE_5V                0x10	// +/-5V range
#define RANGE_10V               0x00	// +/-10V range

#define EOPL			0x80	// End of ADC poll list marker.
#define GSEL_BIPOLAR5V		0x00F0	// LP_GSEL setting for 5V bipolar range.
#define GSEL_BIPOLAR10V		0x00A0	// LP_GSEL setting for 10V bipolar range.

// Error codes that must be visible to this base class.
#define ERR_ILLEGAL_PARM	0x00010000	// Illegal function parameter value was specified.
#define ERR_I2C			0x00020000	// I2C error.
#define ERR_COUNTERSETUP	0x00200000	// Illegal setup specified for counter channel.
#define ERR_DEBI_TIMEOUT	0x00400000	// DEBI transfer timed out.

// Organization (physical order) and size (in DWORDs) of logical DMA buffers contained by ANA_DMABUF.
#define ADC_DMABUF_DWORDS	40	// ADC DMA buffer must hold 16 samples, plus pre/post garbage samples.
#define DAC_WDMABUF_DWORDS	1	// DAC output DMA buffer holds a single sample.

// All remaining space in 4KB DMA buffer is available for the RPS1 program.

// Address offsets, in DWORDS, from base of DMA buffer.
#define DAC_WDMABUF_OS		ADC_DMABUF_DWORDS

// Interrupt enab bit in ISR and IER.
#define IRQ_GPIO3		0x00000040	// IRQ enable for GPIO3.
#define IRQ_RPS1                0x10000000
#define ISR_AFOU		0x00000800	// Audio fifo
						// under/overflow
						// detected.
#define IRQ_COINT1A             0x0400	// conter 1A overflow
						// interrupt mask
#define IRQ_COINT1B             0x0800	// conter 1B overflow
						// interrupt mask
#define IRQ_COINT2A             0x1000	// conter 2A overflow
						// interrupt mask
#define IRQ_COINT2B             0x2000	// conter 2B overflow
						// interrupt mask
#define IRQ_COINT3A             0x4000	// conter 3A overflow
						// interrupt mask
#define IRQ_COINT3B             0x8000	// conter 3B overflow
						// interrupt mask

// RPS command codes.
#define RPS_CLRSIGNAL		0x00000000	// CLEAR SIGNAL
#define RPS_SETSIGNAL		0x10000000	// SET SIGNAL
#define RPS_NOP			0x00000000	// NOP
#define RPS_PAUSE		0x20000000	// PAUSE
#define RPS_UPLOAD		0x40000000	// UPLOAD
#define RPS_JUMP		0x80000000	// JUMP
#define RPS_LDREG		0x90000100	// LDREG (1 uint32_t only)
#define RPS_STREG		0xA0000100	// STREG (1 uint32_t only)
#define RPS_STOP		0x50000000	// STOP
#define RPS_IRQ                 0x60000000	// IRQ

#define RPS_LOGICAL_OR		0x08000000	// Logical OR conditionals.
#define RPS_INVERT		0x04000000	// Test for negated semaphores.
#define RPS_DEBI		0x00000002	// DEBI done

#define RPS_SIG0		0x00200000	// RPS semaphore 0 (used by ADC).
#define RPS_SIG1		0x00400000	// RPS semaphore 1 (used by DAC).
#define RPS_SIG2		0x00800000	// RPS semaphore 2 (not used).
#define RPS_GPIO2		0x00080000	// RPS GPIO2
#define RPS_GPIO3		0x00100000	// RPS GPIO3

#define RPS_SIGADC		RPS_SIG0	// Trigger/status for ADC's RPS program.
#define RPS_SIGDAC		RPS_SIG1	// Trigger/status for DAC's RPS program.

// RPS clock parameters.
#define RPSCLK_SCALAR		8	// This is apparent ratio of PCI/RPS clks (undocumented!!).
#define RPSCLK_PER_US		( 33 / RPSCLK_SCALAR )	// Number of RPS clocks in one microsecond.

// Event counter source addresses.
#define SBA_RPS_A0		0x27	// Time of RPS0 busy, in PCI clocks.

// GPIO constants.
#define GPIO_BASE		0x10004000	// GPIO 0,2,3 = inputs, GPIO3 = IRQ; GPIO1 = out.
#define GPIO1_LO		0x00000000	// GPIO1 set to LOW.
#define GPIO1_HI		0x00001000	// GPIO1 set to HIGH.

// Primary Status Register (PSR) constants.
#define PSR_DEBI_E		0x00040000	// DEBI event flag.
#define PSR_DEBI_S		0x00080000	// DEBI status flag.
#define PSR_A2_IN		0x00008000	// Audio output DMA2 protection address reached.
#define PSR_AFOU		0x00000800	// Audio FIFO under/overflow detected.
#define PSR_GPIO2		0x00000020	// GPIO2 input pin: 0=AdcBusy, 1=AdcIdle.
#define PSR_EC0S		0x00000001	// Event counter 0 threshold reached.

// Secondary Status Register (SSR) constants.
#define SSR_AF2_OUT		0x00000200	// Audio 2 output FIFO under/overflow detected.

// Master Control Register 1 (MC1) constants.
#define MC1_SOFT_RESET		0x80000000	// Invoke 7146 soft reset.
#define MC1_SHUTDOWN		0x3FFF0000	// Shut down all MC1-controlled enables.

#define MC1_ERPS1		0x2000	// enab/disable RPS task 1.
#define MC1_ERPS0		0x1000	// enab/disable RPS task 0.
#define MC1_DEBI		0x0800	// enab/disable DEBI pins.
#define MC1_AUDIO		0x0200	// enab/disable audio port pins.
#define MC1_I2C			0x0100	// enab/disable I2C interface.
#define MC1_A2OUT		0x0008	// enab/disable transfer on A2 out.
#define MC1_A2IN		0x0004	// enab/disable transfer on A2 in.
#define MC1_A1IN		0x0001	// enab/disable transfer on A1 in.

// Master Control Register 2 (MC2) constants.
#define MC2_UPLD_DEBIq		0x00020002	// Upload DEBI registers.
#define MC2_UPLD_IICq		0x00010001	// Upload I2C registers.
#define MC2_RPSSIG2_ONq		0x20002000	// Assert RPS_SIG2.
#define MC2_RPSSIG1_ONq		0x10001000	// Assert RPS_SIG1.
#define MC2_RPSSIG0_ONq		0x08000800	// Assert RPS_SIG0.
#define MC2_UPLD_DEBI_MASKq	0x00000002	// Upload DEBI mask.
#define MC2_UPLD_IIC_MASKq	0x00000001	// Upload I2C mask.
#define MC2_RPSSIG2_MASKq	0x00002000	// RPS_SIG2 bit mask.
#define MC2_RPSSIG1_MASKq	0x00001000	// RPS_SIG1 bit mask.
#define MC2_RPSSIG0_MASKq	0x00000800	// RPS_SIG0 bit mask.

#define MC2_DELAYTRIG_4USq	MC2_RPSSIG1_ON
#define MC2_DELAYBUSY_4USq	MC2_RPSSIG1_MASK

#define	MC2_DELAYTRIG_6USq	MC2_RPSSIG2_ON
#define MC2_DELAYBUSY_6USq	MC2_RPSSIG2_MASK

#define MC2_UPLD_DEBI		0x0002	// Upload DEBI.
#define MC2_UPLD_IIC		0x0001	// Upload I2C.
#define MC2_RPSSIG2		0x2000	// RPS signal 2 (not used).
#define MC2_RPSSIG1		0x1000	// RPS signal 1 (DAC RPS busy).
#define MC2_RPSSIG0		0x0800	// RPS signal 0 (ADC RPS busy).

#define MC2_ADC_RPS		MC2_RPSSIG0	// ADC RPS busy.
#define MC2_DAC_RPS		MC2_RPSSIG1	// DAC RPS busy.

///////////////////oldies///////////
#define MC2_UPLD_DEBIQ		0x00020002	// Upload DEBI registers.
#define MC2_UPLD_IICQ		0x00010001	// Upload I2C registers.
////////////////////////////////////////

// PCI BUS (SAA7146) REGISTER ADDRESS OFFSETS ////////////////////////
#define P_PCI_BT_A		0x004C	// Audio DMA
						// burst/threshold
						// control.
#define P_DEBICFG               0x007C	// DEBI configuration.
#define P_DEBICMD               0x0080	// DEBI command.
#define P_DEBIPAGE              0x0084	// DEBI page.
#define P_DEBIAD                0x0088	// DEBI target address.
#define P_I2CCTRL               0x008C	// I2C control.
#define P_I2CSTAT               0x0090	// I2C status.
#define P_BASEA2_IN		0x00AC	// Audio input 2 base
						// physical DMAbuf
						// address.
#define P_PROTA2_IN		0x00B0	// Audio input 2
						// physical DMAbuf
						// protection address.
#define P_PAGEA2_IN		0x00B4	// Audio input 2
						// paging attributes.
#define P_BASEA2_OUT		0x00B8	// Audio output 2 base
						// physical DMAbuf
						// address.
#define P_PROTA2_OUT		0x00BC	// Audio output 2
						// physical DMAbuf
						// protection address.
#define P_PAGEA2_OUT		0x00C0	// Audio output 2
						// paging attributes.
#define P_RPSPAGE0              0x00C4	// RPS0 page.
#define P_RPSPAGE1              0x00C8	// RPS1 page.
#define P_RPS0_TOUT		0x00D4	// RPS0 time-out.
#define P_RPS1_TOUT		0x00D8	// RPS1 time-out.
#define P_IER                   0x00DC	// Interrupt enable.
#define P_GPIO                  0x00E0	// General-purpose I/O.
#define P_EC1SSR		0x00E4	// Event counter set 1
						// source select.
#define P_ECT1R			0x00EC	// Event counter
						// threshold set 1.
#define P_ACON1                 0x00F4	// Audio control 1.
#define P_ACON2                 0x00F8	// Audio control 2.
#define P_MC1                   0x00FC	// Master control 1.
#define P_MC2                   0x0100	// Master control 2.
#define P_RPSADDR0              0x0104	// RPS0 instruction pointer.
#define P_RPSADDR1              0x0108	// RPS1 instruction pointer.
#define P_ISR                   0x010C	// Interrupt status.
#define P_PSR                   0x0110	// Primary status.
#define P_SSR                   0x0114	// Secondary status.
#define P_EC1R			0x0118	// Event counter set 1.
#define P_ADP4			0x0138	// Logical audio DMA
						// pointer of audio
						// input FIFO A2_IN.
#define P_FB_BUFFER1            0x0144	// Audio feedback buffer 1.
#define P_FB_BUFFER2            0x0148	// Audio feedback buffer 2.
#define P_TSL1                  0x0180	// Audio time slot list 1.
#define P_TSL2                  0x01C0	// Audio time slot list 2.

// LOCAL BUS (GATE ARRAY) REGISTER ADDRESS OFFSETS /////////////////
// Analog I/O registers:
#define LP_DACPOL		0x0082	//  Write DAC polarity.
#define LP_GSEL			0x0084	//  Write ADC gain.
#define LP_ISEL			0x0086	//  Write ADC channel select.
// Digital I/O (write only):
#define LP_WRINTSELA		0x0042	//  Write A interrupt enable.
#define LP_WREDGSELA		0x0044	//  Write A edge selection.
#define LP_WRCAPSELA		0x0046	//  Write A capture enable.
#define LP_WRDOUTA		0x0048	//  Write A digital output.
#define LP_WRINTSELB		0x0052	//  Write B interrupt enable.
#define LP_WREDGSELB		0x0054	//  Write B edge selection.
#define LP_WRCAPSELB		0x0056	//  Write B capture enable.
#define LP_WRDOUTB		0x0058	//  Write B digital output.
#define LP_WRINTSELC		0x0062	//  Write C interrupt enable.
#define LP_WREDGSELC		0x0064	//  Write C edge selection.
#define LP_WRCAPSELC		0x0066	//  Write C capture enable.
#define LP_WRDOUTC		0x0068	//  Write C digital output.

// Digital I/O (read only):
#define LP_RDDINA		0x0040	//  Read digital input.
#define LP_RDCAPFLGA		0x0048	//  Read edges captured.
#define LP_RDINTSELA		0x004A	//  Read interrupt
						//  enable register.
#define LP_RDEDGSELA		0x004C	//  Read edge
						//  selection
						//  register.
#define LP_RDCAPSELA		0x004E	//  Read capture
						//  enable register.
#define LP_RDDINB		0x0050	//  Read digital input.
#define LP_RDCAPFLGB		0x0058	//  Read edges captured.
#define LP_RDINTSELB		0x005A	//  Read interrupt
						//  enable register.
#define LP_RDEDGSELB		0x005C	//  Read edge
						//  selection
						//  register.
#define LP_RDCAPSELB		0x005E	//  Read capture
						//  enable register.
#define LP_RDDINC		0x0060	//  Read digital input.
#define LP_RDCAPFLGC		0x0068	//  Read edges captured.
#define LP_RDINTSELC		0x006A	//  Read interrupt
						//  enable register.
#define LP_RDEDGSELC		0x006C	//  Read edge
						//  selection
						//  register.
#define LP_RDCAPSELC		0x006E	//  Read capture
						//  enable register.
// Counter Registers (read/write):
#define LP_CR0A			0x0000	//  0A setup register.
#define LP_CR0B			0x0002	//  0B setup register.
#define LP_CR1A			0x0004	//  1A setup register.
#define LP_CR1B			0x0006	//  1B setup register.
#define LP_CR2A			0x0008	//  2A setup register.
#define LP_CR2B			0x000A	//  2B setup register.
// Counter PreLoad (write) and Latch (read) Registers:
#define	LP_CNTR0ALSW		0x000C	//  0A lsw.
#define	LP_CNTR0AMSW		0x000E	//  0A msw.
#define	LP_CNTR0BLSW		0x0010	//  0B lsw.
#define	LP_CNTR0BMSW		0x0012	//  0B msw.
#define	LP_CNTR1ALSW		0x0014	//  1A lsw.
#define	LP_CNTR1AMSW		0x0016	//  1A msw.
#define	LP_CNTR1BLSW		0x0018	//  1B lsw.
#define	LP_CNTR1BMSW		0x001A	//  1B msw.
#define	LP_CNTR2ALSW		0x001C	//  2A lsw.
#define	LP_CNTR2AMSW		0x001E	//  2A msw.
#define	LP_CNTR2BLSW		0x0020	//  2B lsw.
#define	LP_CNTR2BMSW		0x0022	//  2B msw.
// Miscellaneous Registers (read/write):
#define LP_MISC1		0x0088	//  Read/write Misc1.
#define LP_WRMISC2		0x0090	//  Write Misc2.
#define LP_RDMISC2		0x0082	//  Read Misc2.

// Bit masks for MISC1 register that are the same for reads and writes.
#define MISC1_WENABLE		0x8000	// enab writes to
						// MISC2 (except Clear
						// Watchdog bit).
#define MISC1_WDISABLE		0x0000	// Disable writes to MISC2.
#define MISC1_EDCAP		0x1000	// enab edge capture
						// on DIO chans
						// specified by
						// LP_WRCAPSELx.
#define MISC1_NOEDCAP		0x0000	// Disable edge
						// capture on
						// specified DIO
						// chans.

// Bit masks for MISC1 register reads.
#define RDMISC1_WDTIMEOUT	0x4000	// Watchdog timer timed out.

// Bit masks for MISC2 register writes.
#define WRMISC2_WDCLEAR		0x8000	// Reset watchdog
						// timer to zero.
#define WRMISC2_CHARGE_ENABLE	0x4000	// enab battery
						// trickle charging.

// Bit masks for MISC2 register that are the same for reads and writes.
#define MISC2_BATT_ENABLE	0x0008	// Backup battery enable.
#define MISC2_WDENABLE		0x0004	// Watchdog timer enable.
#define MISC2_WDPERIOD_MASK	0x0003	// Watchdog interval
						// select mask.

// Bit masks for ACON1 register.
#define A2_RUN			0x40000000	// Run A2 based on TSL2.
#define A1_RUN			0x20000000	// Run A1 based on TSL1.
#define A1_SWAP			0x00200000	// Use big-endian for A1.
#define A2_SWAP			0x00100000	// Use big-endian for A2.
#define WS_MODES		0x00019999	// WS0 = TSL1 trigger
						// input, WS1-WS4 =
						// CS* outputs.

#if PLATFORM == INTEL		// Base ACON1 config:
						// always run A1 based
						// on TSL1.
#define ACON1_BASE		( WS_MODES | A1_RUN )
#elif PLATFORM == MOTOROLA
#define ACON1_BASE		( WS_MODES | A1_RUN | A1_SWAP | A2_SWAP )
#endif

#define ACON1_ADCSTART		ACON1_BASE	// Start ADC: run A1
						// based on TSL1.
#define ACON1_DACSTART		( ACON1_BASE | A2_RUN )	// Start
							// transmit to
							// DAC: run A2
							// based on
							// TSL2.
#define ACON1_DACSTOP		ACON1_BASE	// Halt A2.

// Bit masks for ACON2 register.
#define A1_CLKSRC_BCLK1		0x00000000	// A1 bit rate = BCLK1 (ADC).
#define A2_CLKSRC_X1		0x00800000	// A2 bit rate = ACLK/1 (DACs).
#define A2_CLKSRC_X2		0x00C00000	// A2 bit rate = ACLK/2 (DACs).
#define A2_CLKSRC_X4		0x01400000	// A2 bit rate = ACLK/4 (DACs).
#define INVERT_BCLK2		0x00100000	// Invert BCLK2 (DACs).
#define BCLK2_OE		0x00040000	// enab BCLK2 (DACs).
#define ACON2_XORMASK		0x000C0000	// XOR mask for ACON2
						// active-low bits.

#define ACON2_INIT		( ACON2_XORMASK ^ ( A1_CLKSRC_BCLK1 | A2_CLKSRC_X2 | INVERT_BCLK2 | BCLK2_OE ) )

// Bit masks for timeslot records.
#define WS1		     	0x40000000	// WS output to assert.
#define WS2		     	0x20000000
#define WS3		     	0x10000000
#define WS4		     	0x08000000
#define RSD1			0x01000000	// Shift A1 data in on SD1.
#define SDW_A1			0x00800000	// Store rcv'd char at
						// next char slot of
						// DWORD1 buffer.
#define SIB_A1			0x00400000	// Store rcv'd char at
						// next char slot of
						// FB1 buffer.
#define SF_A1			0x00200000	// Write unsigned long
						// buffer to input
						// FIFO.

//Select parallel-to-serial converter's data source:
#define XFIFO_0			0x00000000	//   Data fifo byte 0.
#define XFIFO_1			0x00000010	//   Data fifo byte 1.
#define XFIFO_2			0x00000020	//   Data fifo byte 2.
#define XFIFO_3			0x00000030	//   Data fifo byte 3.
#define XFB0			0x00000040	//   FB_BUFFER byte 0.
#define XFB1			0x00000050	//   FB_BUFFER byte 1.
#define XFB2			0x00000060	//   FB_BUFFER byte 2.
#define XFB3			0x00000070	//   FB_BUFFER byte 3.
#define SIB_A2			0x00000200	// Store next dword
						// from A2's input
						// shifter to FB2
						// buffer.
#define SF_A2			0x00000100	// Store next dword
						// from A2's input
						// shifter to its
						// input fifo.
#define LF_A2			0x00000080	// Load next dword
						// from A2's output
						// fifo into its
						// output dword
						// buffer.
#define XSD2			0x00000008	// Shift data out on SD2.
#define RSD3			0x00001800	// Shift data in on SD3.
#define RSD2			0x00001000	// Shift data in on SD2.
#define LOW_A2			0x00000002	// Drive last SD low
						// for 7 clks, then
						// tri-state.
#define EOS		     	0x00000001	// End of superframe.

//////////////////////

// I2C configuration constants.
#define I2C_CLKSEL		0x0400	// I2C bit rate =
						// PCIclk/480 = 68.75
						// KHz.
#define I2C_BITRATE		68.75	// I2C bus data bit
						// rate (determined by
						// I2C_CLKSEL) in KHz.
#define I2C_WRTIME		15.0	// Worst case time,in
						// msec, for EEPROM
						// internal write op.

// I2C manifest constants.

// Max retries to wait for EEPROM write.
#define I2C_RETRIES		( I2C_WRTIME * I2C_BITRATE / 9.0 )
#define I2C_ERR			0x0002	// I2C control/status
						// flag ERROR.
#define I2C_BUSY		0x0001	// I2C control/status
						// flag BUSY.
#define I2C_ABORT		0x0080	// I2C status flag ABORT.
#define I2C_ATTRSTART		0x3	// I2C attribute START.
#define I2C_ATTRCONT		0x2	// I2C attribute CONT.
#define I2C_ATTRSTOP		0x1	// I2C attribute STOP.
#define I2C_ATTRNOP		0x0	// I2C attribute NOP.

// I2C read command  | EEPROM address.
#define I2CR			( devpriv->I2CAdrs | 1 )

// I2C write command | EEPROM address.
#define I2CW			( devpriv->I2CAdrs )

// Code macros used for constructing I2C command bytes.
#define I2C_B2(ATTR,VAL)	( ( (ATTR) << 6 ) | ( (VAL) << 24 ) )
#define I2C_B1(ATTR,VAL)	( ( (ATTR) << 4 ) | ( (VAL) << 16 ) )
#define I2C_B0(ATTR,VAL)	( ( (ATTR) << 2 ) | ( (VAL) <<  8 ) )

////////////////////////////////////////////////////////
//oldest
#define P_DEBICFGq              0x007C	// DEBI configuration.
#define P_DEBICMDq              0x0080	// DEBI command.
#define P_DEBIPAGEq             0x0084	// DEBI page.
#define P_DEBIADq               0x0088	// DEBI target address.

#define DEBI_CFG_TOQ		0x03C00000	// timeout (15 PCI cycles)
#define DEBI_CFG_FASTQ		0x10000000	// fast mode enable
#define DEBI_CFG_16Q		0x00080000	// 16-bit access enable
#define DEBI_CFG_INCQ		0x00040000	// enable address increment
#define DEBI_CFG_TIMEROFFQ	0x00010000	// disable timer
#define DEBI_CMD_RDQ		0x00050000	// read immediate 2 bytes
#define DEBI_CMD_WRQ		0x00040000	// write immediate 2 bytes
#define DEBI_PAGE_DISABLEQ	0x00000000	// paging disable

///////////////////////////////////////////
// DEBI command constants.
#define DEBI_CMD_SIZE16		( 2 << 17 )	// Transfer size is
						// always 2 bytes.
#define DEBI_CMD_READ		0x00010000	// Read operation.
#define DEBI_CMD_WRITE		0x00000000	// Write operation.

// Read immediate 2 bytes.
#define DEBI_CMD_RDWORD		( DEBI_CMD_READ  | DEBI_CMD_SIZE16 )

// Write immediate 2 bytes.
#define DEBI_CMD_WRWORD		( DEBI_CMD_WRITE | DEBI_CMD_SIZE16 )

// DEBI configuration constants.
#define DEBI_CFG_XIRQ_EN	0x80000000	// enab external
						// interrupt on GPIO3.
#define DEBI_CFG_XRESUME	0x40000000	// Resume block
						// transfer when XIRQ
						// deasserted.
#define DEBI_CFG_FAST		0x10000000	// Fast mode enable.

// 4-bit field that specifies DEBI timeout value in PCI clock cycles:
#define DEBI_CFG_TOUT_BIT	22	//   Finish DEBI cycle after
					//   this many clocks.

// 2-bit field that specifies Endian byte lane steering:
#define DEBI_CFG_SWAP_NONE	0x00000000	//   Straight - don't
						//   swap any bytes
						//   (Intel).
#define DEBI_CFG_SWAP_2		0x00100000	//   2-byte swap (Motorola).
#define DEBI_CFG_SWAP_4		0x00200000	//   4-byte swap.
#define DEBI_CFG_16		0x00080000	// Slave is able to
						// serve 16-bit
						// cycles.

#define DEBI_CFG_SLAVE16	0x00080000	// Slave is able to
						// serve 16-bit
						// cycles.
#define DEBI_CFG_INC		0x00040000	// enab address
						// increment for block
						// transfers.
#define DEBI_CFG_INTEL		0x00020000	// Intel style local bus.
#define DEBI_CFG_TIMEROFF	0x00010000	// Disable timer.

#if PLATFORM == INTEL

#define DEBI_TOUT		7	// Wait 7 PCI clocks
						// (212 ns) before
						// polling RDY.

// Intel byte lane steering (pass through all byte lanes).
#define DEBI_SWAP		DEBI_CFG_SWAP_NONE

#elif PLATFORM == MOTOROLA

#define DEBI_TOUT		15	// Wait 15 PCI clocks (454 ns)
					// maximum before timing out.
#define DEBI_SWAP		DEBI_CFG_SWAP_2	// Motorola byte lane steering.

#endif

// DEBI page table constants.
#define DEBI_PAGE_DISABLE	0x00000000	// Paging disable.

///////////////////EXTRA FROM OTHER SANSORAY  * .h////////

// LoadSrc values:
#define LOADSRC_INDX		0	// Preload core in response to
					// Index.
#define LOADSRC_OVER		1	// Preload core in response to
					// Overflow.
#define LOADSRCB_OVERA		2	// Preload B core in response
					// to A Overflow.
#define LOADSRC_NONE		3	// Never preload core.

// IntSrc values:
#define INTSRC_NONE 		0	// Interrupts disabled.
#define INTSRC_OVER 		1	// Interrupt on Overflow.
#define INTSRC_INDX 		2	// Interrupt on Index.
#define INTSRC_BOTH 		3	// Interrupt on Index or Overflow.

// LatchSrc values:
#define LATCHSRC_AB_READ	0	// Latch on read.
#define LATCHSRC_A_INDXA	1	// Latch A on A Index.
#define LATCHSRC_B_INDXB	2	// Latch B on B Index.
#define LATCHSRC_B_OVERA	3	// Latch B on A Overflow.

// IndxSrc values:
#define INDXSRC_HARD		0	// Hardware or software index.
#define INDXSRC_SOFT		1	// Software index only.

// IndxPol values:
#define INDXPOL_POS 		0	// Index input is active high.
#define INDXPOL_NEG 		1	// Index input is active low.

// ClkSrc values:
#define CLKSRC_COUNTER		0	// Counter mode.
#define CLKSRC_TIMER		2	// Timer mode.
#define CLKSRC_EXTENDER		3	// Extender mode.

// ClkPol values:
#define CLKPOL_POS		0	// Counter/Extender clock is
					// active high.
#define CLKPOL_NEG		1	// Counter/Extender clock is
					// active low.
#define CNTDIR_UP		0	// Timer counts up.
#define CNTDIR_DOWN 		1	// Timer counts down.

// ClkEnab values:
#define CLKENAB_ALWAYS		0	// Clock always enabled.
#define CLKENAB_INDEX		1	// Clock is enabled by index.

// ClkMult values:
#define CLKMULT_4X 		0	// 4x clock multiplier.
#define CLKMULT_2X 		1	// 2x clock multiplier.
#define CLKMULT_1X 		2	// 1x clock multiplier.

// Bit Field positions in COUNTER_SETUP structure:
#define BF_LOADSRC		9	// Preload trigger.
#define BF_INDXSRC		7	// Index source.
#define BF_INDXPOL		6	// Index polarity.
#define BF_CLKSRC		4	// Clock source.
#define BF_CLKPOL		3	// Clock polarity/count direction.
#define BF_CLKMULT		1	// Clock multiplier.
#define BF_CLKENAB		0	// Clock enable.

// Enumerated counter operating modes specified by ClkSrc bit field in
// a COUNTER_SETUP.

#define CLKSRC_COUNTER		0	// Counter: ENC_C clock, ENC_D
					// direction.
#define CLKSRC_TIMER		2	// Timer: SYS_C clock,
					// direction specified by
					// ClkPol.
#define CLKSRC_EXTENDER		3	// Extender: OVR_A clock,
					// ENC_D direction.

// Enumerated counter clock multipliers.

#define MULT_X0			0x0003	// Supports no multipliers;
					// fixed physical multiplier =
					// 3.
#define MULT_X1			0x0002	// Supports multiplier x1;
					// fixed physical multiplier =
					// 2.
#define MULT_X2			0x0001	// Supports multipliers x1,
					// x2; physical multipliers =
					// 1 or 2.
#define MULT_X4			0x0000	// Supports multipliers x1,
					// x2, x4; physical
					// multipliers = 0, 1 or 2.

// Sanity-check limits for parameters.

#define NUM_COUNTERS		6	// Maximum valid counter
					// logical channel number.
#define NUM_INTSOURCES		4
#define NUM_LATCHSOURCES	4
#define NUM_CLKMULTS		4
#define NUM_CLKSOURCES		4
#define NUM_CLKPOLS		2
#define NUM_INDEXPOLS		2
#define NUM_INDEXSOURCES	2
#define NUM_LOADTRIGS		4

// Bit field positions in CRA and CRB counter control registers.

// Bit field positions in CRA:
#define CRABIT_INDXSRC_B	14	//   B index source.
#define CRABIT_CLKSRC_B		12	//   B clock source.
#define CRABIT_INDXPOL_A	11	//   A index polarity.
#define CRABIT_LOADSRC_A	 9	//   A preload trigger.
#define CRABIT_CLKMULT_A	 7	//   A clock multiplier.
#define CRABIT_INTSRC_A		 5	//   A interrupt source.
#define CRABIT_CLKPOL_A		 4	//   A clock polarity.
#define CRABIT_INDXSRC_A	 2	//   A index source.
#define CRABIT_CLKSRC_A		 0	//   A clock source.

// Bit field positions in CRB:
#define CRBBIT_INTRESETCMD	15	//   Interrupt reset command.
#define CRBBIT_INTRESET_B	14	//   B interrupt reset enable.
#define CRBBIT_INTRESET_A	13	//   A interrupt reset enable.
#define CRBBIT_CLKENAB_A	12	//   A clock enable.
#define CRBBIT_INTSRC_B		10	//   B interrupt source.
#define CRBBIT_LATCHSRC		 8	//   A/B latch source.
#define CRBBIT_LOADSRC_B	 6	//   B preload trigger.
#define CRBBIT_CLKMULT_B	 3	//   B clock multiplier.
#define CRBBIT_CLKENAB_B	 2	//   B clock enable.
#define CRBBIT_INDXPOL_B	 1	//   B index polarity.
#define CRBBIT_CLKPOL_B		 0	//   B clock polarity.

// Bit field masks for CRA and CRB.

#define CRAMSK_INDXSRC_B	( (uint16_t)( 3 << CRABIT_INDXSRC_B) )
#define CRAMSK_CLKSRC_B		( (uint16_t)( 3 << CRABIT_CLKSRC_B) )
#define CRAMSK_INDXPOL_A	( (uint16_t)( 1 << CRABIT_INDXPOL_A) )
#define CRAMSK_LOADSRC_A	( (uint16_t)( 3 << CRABIT_LOADSRC_A) )
#define CRAMSK_CLKMULT_A	( (uint16_t)( 3 << CRABIT_CLKMULT_A) )
#define CRAMSK_INTSRC_A		( (uint16_t)( 3 << CRABIT_INTSRC_A) )
#define CRAMSK_CLKPOL_A		( (uint16_t)( 3 << CRABIT_CLKPOL_A) )
#define CRAMSK_INDXSRC_A	( (uint16_t)( 3 << CRABIT_INDXSRC_A) )
#define CRAMSK_CLKSRC_A		( (uint16_t)( 3 << CRABIT_CLKSRC_A) )

#define CRBMSK_INTRESETCMD	( (uint16_t)( 1 << CRBBIT_INTRESETCMD) )
#define CRBMSK_INTRESET_B	( (uint16_t)( 1 << CRBBIT_INTRESET_B) )
#define CRBMSK_INTRESET_A	( (uint16_t)( 1 << CRBBIT_INTRESET_A) )
#define CRBMSK_CLKENAB_A	( (uint16_t)( 1 << CRBBIT_CLKENAB_A) )
#define CRBMSK_INTSRC_B		( (uint16_t)( 3 << CRBBIT_INTSRC_B) )
#define CRBMSK_LATCHSRC		( (uint16_t)( 3 << CRBBIT_LATCHSRC) )
#define CRBMSK_LOADSRC_B	( (uint16_t)( 3 << CRBBIT_LOADSRC_B) )
#define CRBMSK_CLKMULT_B	( (uint16_t)( 3 << CRBBIT_CLKMULT_B) )
#define CRBMSK_CLKENAB_B	( (uint16_t)( 1 << CRBBIT_CLKENAB_B) )
#define CRBMSK_INDXPOL_B	( (uint16_t)( 1 << CRBBIT_INDXPOL_B) )
#define CRBMSK_CLKPOL_B		( (uint16_t)( 1 << CRBBIT_CLKPOL_B) )

#define CRBMSK_INTCTRL		( CRBMSK_INTRESETCMD | CRBMSK_INTRESET_A | CRBMSK_INTRESET_B )	// Interrupt reset control bits.

// Bit field positions for standardized SETUP structure.

#define STDBIT_INTSRC		13
#define STDBIT_LATCHSRC		11
#define STDBIT_LOADSRC		 9
#define STDBIT_INDXSRC		 7
#define STDBIT_INDXPOL		 6
#define STDBIT_CLKSRC		 4
#define STDBIT_CLKPOL		 3
#define STDBIT_CLKMULT		 1
#define STDBIT_CLKENAB		 0

// Bit field masks for standardized SETUP structure.

#define STDMSK_INTSRC		( (uint16_t)( 3 << STDBIT_INTSRC   ) )
#define STDMSK_LATCHSRC		( (uint16_t)( 3 << STDBIT_LATCHSRC ) )
#define STDMSK_LOADSRC		( (uint16_t)( 3 << STDBIT_LOADSRC  ) )
#define STDMSK_INDXSRC		( (uint16_t)( 1 << STDBIT_INDXSRC  ) )
#define STDMSK_INDXPOL		( (uint16_t)( 1 << STDBIT_INDXPOL  ) )
#define STDMSK_CLKSRC		( (uint16_t)( 3 << STDBIT_CLKSRC   ) )
#define STDMSK_CLKPOL		( (uint16_t)( 1 << STDBIT_CLKPOL   ) )
#define STDMSK_CLKMULT		( (uint16_t)( 3 << STDBIT_CLKMULT  ) )
#define STDMSK_CLKENAB		( (uint16_t)( 1 << STDBIT_CLKENAB  ) )

//////////////////////////////////////////////////////////

/* typedef struct indexCounter */
/* { */
/*   unsigned int ao; */
/*   unsigned int ai; */
/*   unsigned int digout; */
/*   unsigned int digin; */
/*   unsigned int enc; */
/* }CallCounter; */

typedef struct bufferDMA {
	dma_addr_t PhysicalBase;
	void *LogicalBase;
	uint32_t DMAHandle;
} DMABUF;

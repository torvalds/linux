/*
 * comedi/drivers/s626.h
 * Sensoray s626 Comedi driver, header file
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

#ifndef S626_H_INCLUDED
#define S626_H_INCLUDED

#define S626_DMABUF_SIZE	4096	/* 4k pages */

#define S626_ADC_CHANNELS       16
#define S626_DAC_CHANNELS       4
#define S626_ENCODER_CHANNELS   6
#define S626_DIO_CHANNELS       48
#define S626_DIO_BANKS		3	/* Number of DIO groups. */
#define S626_DIO_EXTCHANS	40	/* Number of extended-capability
					 * DIO channels. */

#define S626_NUM_TRIMDACS	12	/* Number of valid TrimDAC channels. */

/* PCI bus interface types. */
#define S626_INTEL		1	/* Intel bus type. */
#define S626_MOTOROLA		2	/* Motorola bus type. */

#define S626_PLATFORM		S626_INTEL /* *** SELECT PLATFORM TYPE *** */

#define S626_RANGE_5V		0x10	/* +/-5V range */
#define S626_RANGE_10V		0x00	/* +/-10V range */

#define S626_EOPL		0x80	/* End of ADC poll list marker. */
#define S626_GSEL_BIPOLAR5V	0x00F0	/* S626_LP_GSEL setting 5V bipolar. */
#define S626_GSEL_BIPOLAR10V	0x00A0	/* S626_LP_GSEL setting 10V bipolar. */

/* Error codes that must be visible to this base class. */
#define S626_ERR_ILLEGAL_PARM	0x00010000	/* Illegal function parameter
						 * value was specified. */
#define S626_ERR_I2C		0x00020000	/* I2C error. */
#define S626_ERR_COUNTERSETUP	0x00200000	/* Illegal setup specified for
						 * counter channel. */
#define S626_ERR_DEBI_TIMEOUT	0x00400000	/* DEBI transfer timed out. */

/*
 * Organization (physical order) and size (in DWORDs) of logical DMA buffers
 * contained by ANA_DMABUF.
 */
#define S626_ADC_DMABUF_DWORDS	40	/* ADC DMA buffer must hold 16 samples,
					 * plus pre/post garbage samples. */
#define S626_DAC_WDMABUF_DWORDS	1	/* DAC output DMA buffer holds a single
					 * sample. */

/* All remaining space in 4KB DMA buffer is available for the RPS1 program. */

/* Address offsets, in DWORDS, from base of DMA buffer. */
#define S626_DAC_WDMABUF_OS	S626_ADC_DMABUF_DWORDS

/*  Interrupt enable bit in ISR and IER. */
#define S626_IRQ_GPIO3		0x00000040	/* IRQ enable for GPIO3. */
#define S626_IRQ_RPS1		0x10000000
#define S626_ISR_AFOU		0x00000800
/* Audio fifo under/overflow  detected. */

#define S626_IRQ_COINT1A	0x0400	/* counter 1A overflow interrupt mask */
#define S626_IRQ_COINT1B	0x0800	/* counter 1B overflow interrupt mask */
#define S626_IRQ_COINT2A	0x1000	/* counter 2A overflow interrupt mask */
#define S626_IRQ_COINT2B	0x2000	/* counter 2B overflow interrupt mask */
#define S626_IRQ_COINT3A	0x4000	/* counter 3A overflow interrupt mask */
#define S626_IRQ_COINT3B	0x8000	/* counter 3B overflow interrupt mask */

/* RPS command codes. */
#define S626_RPS_CLRSIGNAL	0x00000000	/* CLEAR SIGNAL */
#define S626_RPS_SETSIGNAL	0x10000000	/* SET SIGNAL */
#define S626_RPS_NOP		0x00000000	/* NOP */
#define S626_RPS_PAUSE		0x20000000	/* PAUSE */
#define S626_RPS_UPLOAD		0x40000000	/* UPLOAD */
#define S626_RPS_JUMP		0x80000000	/* JUMP */
#define S626_RPS_LDREG		0x90000100	/* LDREG (1 uint32_t only) */
#define S626_RPS_STREG		0xA0000100	/* STREG (1 uint32_t only) */
#define S626_RPS_STOP		0x50000000	/* STOP */
#define S626_RPS_IRQ		0x60000000	/* IRQ */

#define S626_RPS_LOGICAL_OR	0x08000000	/* Logical OR conditionals. */
#define S626_RPS_INVERT		0x04000000	/* Test for negated
						 * semaphores. */
#define S626_RPS_DEBI		0x00000002	/* DEBI done */

#define S626_RPS_SIG0		0x00200000	/* RPS semaphore 0
						 * (used by ADC). */
#define S626_RPS_SIG1		0x00400000	/* RPS semaphore 1
						 * (used by DAC). */
#define S626_RPS_SIG2		0x00800000	/* RPS semaphore 2
						 * (not used). */
#define S626_RPS_GPIO2		0x00080000	/* RPS GPIO2 */
#define S626_RPS_GPIO3		0x00100000	/* RPS GPIO3 */

#define S626_RPS_SIGADC		S626_RPS_SIG0	/* Trigger/status for
						 * ADC's RPS program. */
#define S626_RPS_SIGDAC		S626_RPS_SIG1	/* Trigger/status for
						 * DAC's RPS program. */

/* RPS clock parameters. */
#define S626_RPSCLK_SCALAR	8	/* This is apparent ratio of
					 * PCI/RPS clks (undocumented!!). */
#define S626_RPSCLK_PER_US	(33 / S626_RPSCLK_SCALAR)
					/* Number of RPS clocks in one
					 * microsecond. */

/* Event counter source addresses. */
#define S626_SBA_RPS_A0		0x27	/* Time of RPS0 busy, in PCI clocks. */

/* GPIO constants. */
#define S626_GPIO_BASE		0x10004000	/* GPIO 0,2,3 = inputs,
						 * GPIO3 = IRQ; GPIO1 = out. */
#define S626_GPIO1_LO		0x00000000	/* GPIO1 set to LOW. */
#define S626_GPIO1_HI		0x00001000	/* GPIO1 set to HIGH. */

/* Primary Status Register (PSR) constants. */
#define S626_PSR_DEBI_E		0x00040000	/* DEBI event flag. */
#define S626_PSR_DEBI_S		0x00080000	/* DEBI status flag. */
#define S626_PSR_A2_IN		0x00008000	/* Audio output DMA2 protection
						 * address reached. */
#define S626_PSR_AFOU		0x00000800	/* Audio FIFO under/overflow
						 * detected. */
#define S626_PSR_GPIO2		0x00000020	/* GPIO2 input pin: 0=AdcBusy,
						 * 1=AdcIdle. */
#define S626_PSR_EC0S		0x00000001	/* Event counter 0 threshold
						 * reached. */

/* Secondary Status Register (SSR) constants. */
#define S626_SSR_AF2_OUT	0x00000200	/* Audio 2 output FIFO
						 * under/overflow detected. */

/* Master Control Register 1 (MC1) constants. */
#define S626_MC1_SOFT_RESET	0x80000000	/* Invoke 7146 soft reset. */
#define S626_MC1_SHUTDOWN	0x3FFF0000	/* Shut down all MC1-controlled
						 * enables. */

#define S626_MC1_ERPS1		0x2000	/* Enab/disable RPS task 1. */
#define S626_MC1_ERPS0		0x1000	/* Enab/disable RPS task 0. */
#define S626_MC1_DEBI		0x0800	/* Enab/disable DEBI pins. */
#define S626_MC1_AUDIO		0x0200	/* Enab/disable audio port pins. */
#define S626_MC1_I2C		0x0100	/* Enab/disable I2C interface. */
#define S626_MC1_A2OUT		0x0008	/* Enab/disable transfer on A2 out. */
#define S626_MC1_A2IN		0x0004	/* Enab/disable transfer on A2 in. */
#define S626_MC1_A1IN		0x0001	/* Enab/disable transfer on A1 in. */

/* Master Control Register 2 (MC2) constants. */
#define S626_MC2_UPLD_DEBI	0x0002	/* Upload DEBI. */
#define S626_MC2_UPLD_IIC	0x0001	/* Upload I2C. */
#define S626_MC2_RPSSIG2	0x2000	/* RPS signal 2 (not used). */
#define S626_MC2_RPSSIG1	0x1000	/* RPS signal 1 (DAC RPS busy). */
#define S626_MC2_RPSSIG0	0x0800	/* RPS signal 0 (ADC RPS busy). */

#define S626_MC2_ADC_RPS	S626_MC2_RPSSIG0	/* ADC RPS busy. */
#define S626_MC2_DAC_RPS	S626_MC2_RPSSIG1	/* DAC RPS busy. */

/* PCI BUS (SAA7146) REGISTER ADDRESS OFFSETS */
#define S626_P_PCI_BT_A		0x004C	/* Audio DMA burst/threshold control. */
#define S626_P_DEBICFG		0x007C	/* DEBI configuration. */
#define S626_P_DEBICMD		0x0080	/* DEBI command. */
#define S626_P_DEBIPAGE		0x0084	/* DEBI page. */
#define S626_P_DEBIAD		0x0088	/* DEBI target address. */
#define S626_P_I2CCTRL		0x008C	/* I2C control. */
#define S626_P_I2CSTAT		0x0090	/* I2C status. */
#define S626_P_BASEA2_IN	0x00AC	/* Audio input 2 base physical DMAbuf
					 * address. */
#define S626_P_PROTA2_IN	0x00B0	/* Audio input 2 physical DMAbuf
					 * protection address. */
#define S626_P_PAGEA2_IN	0x00B4	/* Audio input 2 paging attributes. */
#define S626_P_BASEA2_OUT	0x00B8	/* Audio output 2 base physical DMAbuf
					 * address. */
#define S626_P_PROTA2_OUT	0x00BC	/* Audio output 2 physical DMAbuf
					 * protection address. */
#define S626_P_PAGEA2_OUT	0x00C0	/* Audio output 2 paging attributes. */
#define S626_P_RPSPAGE0		0x00C4	/* RPS0 page. */
#define S626_P_RPSPAGE1		0x00C8	/* RPS1 page. */
#define S626_P_RPS0_TOUT	0x00D4	/* RPS0 time-out. */
#define S626_P_RPS1_TOUT	0x00D8	/* RPS1 time-out. */
#define S626_P_IER		0x00DC	/* Interrupt enable. */
#define S626_P_GPIO		0x00E0	/* General-purpose I/O. */
#define S626_P_EC1SSR		0x00E4	/* Event counter set 1 source select. */
#define S626_P_ECT1R		0x00EC	/* Event counter threshold set 1. */
#define S626_P_ACON1		0x00F4	/* Audio control 1. */
#define S626_P_ACON2		0x00F8	/* Audio control 2. */
#define S626_P_MC1		0x00FC	/* Master control 1. */
#define S626_P_MC2		0x0100	/* Master control 2. */
#define S626_P_RPSADDR0		0x0104	/* RPS0 instruction pointer. */
#define S626_P_RPSADDR1		0x0108	/* RPS1 instruction pointer. */
#define S626_P_ISR		0x010C	/* Interrupt status. */
#define S626_P_PSR		0x0110	/* Primary status. */
#define S626_P_SSR		0x0114	/* Secondary status. */
#define S626_P_EC1R		0x0118	/* Event counter set 1. */
#define S626_P_ADP4		0x0138	/* Logical audio DMA pointer of audio
					 * input FIFO A2_IN. */
#define S626_P_FB_BUFFER1	0x0144	/* Audio feedback buffer 1. */
#define S626_P_FB_BUFFER2	0x0148	/* Audio feedback buffer 2. */
#define S626_P_TSL1		0x0180	/* Audio time slot list 1. */
#define S626_P_TSL2		0x01C0	/* Audio time slot list 2. */

/* LOCAL BUS (GATE ARRAY) REGISTER ADDRESS OFFSETS */
/* Analog I/O registers: */
#define S626_LP_DACPOL		0x0082	/* Write DAC polarity. */
#define S626_LP_GSEL		0x0084	/* Write ADC gain. */
#define S626_LP_ISEL		0x0086	/* Write ADC channel select. */

/* Digital I/O registers */
#define S626_LP_RDDIN(x)	(0x0040 + (x) * 0x10)	/* R: digital input */
#define S626_LP_WRINTSEL(x)	(0x0042 + (x) * 0x10)	/* W: int enable */
#define S626_LP_WREDGSEL(x)	(0x0044 + (x) * 0x10)	/* W: edge selection */
#define S626_LP_WRCAPSEL(x)	(0x0046 + (x) * 0x10)	/* W: capture enable */
#define S626_LP_RDCAPFLG(x)	(0x0048 + (x) * 0x10)	/* R: edges captured */
#define S626_LP_WRDOUT(x)	(0x0048 + (x) * 0x10)	/* W: digital output */
#define S626_LP_RDINTSEL(x)	(0x004a + (x) * 0x10)	/* R: int enable */
#define S626_LP_RDEDGSEL(x)	(0x004c + (x) * 0x10)	/* R: edge selection */
#define S626_LP_RDCAPSEL(x)	(0x004e + (x) * 0x10)	/* R: capture enable */

/* Counter Registers (read/write): */
#define S626_LP_CR0A		0x0000	/* 0A setup register. */
#define S626_LP_CR0B		0x0002	/* 0B setup register. */
#define S626_LP_CR1A		0x0004	/* 1A setup register. */
#define S626_LP_CR1B		0x0006	/* 1B setup register. */
#define S626_LP_CR2A		0x0008	/* 2A setup register. */
#define S626_LP_CR2B		0x000A	/* 2B setup register. */

/* Counter PreLoad (write) and Latch (read) Registers: */
#define	S626_LP_CNTR0ALSW	0x000C	/* 0A lsw. */
#define	S626_LP_CNTR0AMSW	0x000E	/* 0A msw. */
#define	S626_LP_CNTR0BLSW	0x0010	/* 0B lsw. */
#define	S626_LP_CNTR0BMSW	0x0012	/* 0B msw. */
#define	S626_LP_CNTR1ALSW	0x0014	/* 1A lsw. */
#define	S626_LP_CNTR1AMSW	0x0016	/* 1A msw. */
#define	S626_LP_CNTR1BLSW	0x0018	/* 1B lsw. */
#define	S626_LP_CNTR1BMSW	0x001A	/* 1B msw. */
#define	S626_LP_CNTR2ALSW	0x001C	/* 2A lsw. */
#define	S626_LP_CNTR2AMSW	0x001E	/* 2A msw. */
#define	S626_LP_CNTR2BLSW	0x0020	/* 2B lsw. */
#define	S626_LP_CNTR2BMSW	0x0022	/* 2B msw. */

/* Miscellaneous Registers (read/write): */
#define S626_LP_MISC1		0x0088	/* Read/write Misc1. */
#define S626_LP_WRMISC2		0x0090	/* Write Misc2. */
#define S626_LP_RDMISC2		0x0082	/* Read Misc2. */

/* Bit masks for MISC1 register that are the same for reads and writes. */
#define S626_MISC1_WENABLE	0x8000	/* enab writes to MISC2 (except Clear
					 * Watchdog bit). */
#define S626_MISC1_WDISABLE	0x0000	/* Disable writes to MISC2. */
#define S626_MISC1_EDCAP	0x1000	/* Enable edge capture on DIO chans
					 * specified by S626_LP_WRCAPSELx. */
#define S626_MISC1_NOEDCAP	0x0000	/* Disable edge capture on specified
					 * DIO chans. */

/* Bit masks for MISC1 register reads. */
#define S626_RDMISC1_WDTIMEOUT	0x4000	/* Watchdog timer timed out. */

/* Bit masks for MISC2 register writes. */
#define S626_WRMISC2_WDCLEAR	0x8000	/* Reset watchdog timer to zero. */
#define S626_WRMISC2_CHARGE_ENABLE 0x4000 /* Enable battery trickle charging. */

/* Bit masks for MISC2 register that are the same for reads and writes. */
#define S626_MISC2_BATT_ENABLE	0x0008	/* Backup battery enable. */
#define S626_MISC2_WDENABLE	0x0004	/* Watchdog timer enable. */
#define S626_MISC2_WDPERIOD_MASK 0x0003	/* Watchdog interval select mask. */

/* Bit masks for ACON1 register. */
#define S626_A2_RUN		0x40000000	/* Run A2 based on TSL2. */
#define S626_A1_RUN		0x20000000	/* Run A1 based on TSL1. */
#define S626_A1_SWAP		0x00200000	/* Use big-endian for A1. */
#define S626_A2_SWAP		0x00100000	/* Use big-endian for A2. */
#define S626_WS_MODES		0x00019999	/* WS0 = TSL1 trigger input,
						 * WS1-WS4 = CS* outputs. */

#if S626_PLATFORM == S626_INTEL		/* Base ACON1 config: always run
					 * A1 based on TSL1. */
#define S626_ACON1_BASE		(S626_WS_MODES | S626_A1_RUN)
#elif S626_PLATFORM == S626_MOTOROLA
#define S626_ACON1_BASE		\
	(S626_WS_MODES | S626_A1_RUN | S626_A1_SWAP | S626_A2_SWAP)
#endif

#define S626_ACON1_ADCSTART	S626_ACON1_BASE	/* Start ADC: run A1
						 * based on TSL1. */
#define S626_ACON1_DACSTART	(S626_ACON1_BASE | S626_A2_RUN)
/* Start transmit to DAC: run A2 based on TSL2. */
#define S626_ACON1_DACSTOP	S626_ACON1_BASE	/* Halt A2. */

/* Bit masks for ACON2 register. */
#define S626_A1_CLKSRC_BCLK1	0x00000000	/* A1 bit rate = BCLK1 (ADC). */
#define S626_A2_CLKSRC_X1	0x00800000	/* A2 bit rate = ACLK/1
						 * (DACs). */
#define S626_A2_CLKSRC_X2	0x00C00000	/* A2 bit rate = ACLK/2
						 * (DACs). */
#define S626_A2_CLKSRC_X4	0x01400000	/* A2 bit rate = ACLK/4
						 * (DACs). */
#define S626_INVERT_BCLK2	0x00100000	/* Invert BCLK2 (DACs). */
#define S626_BCLK2_OE		0x00040000	/* Enable BCLK2 (DACs). */
#define S626_ACON2_XORMASK	0x000C0000	/* XOR mask for ACON2
						 * active-low bits. */

#define S626_ACON2_INIT		(S626_ACON2_XORMASK ^ \
				 (S626_A1_CLKSRC_BCLK1 | S626_A2_CLKSRC_X2 | \
				  S626_INVERT_BCLK2 | S626_BCLK2_OE))

/* Bit masks for timeslot records. */
#define S626_WS1		0x40000000	/* WS output to assert. */
#define S626_WS2		0x20000000
#define S626_WS3		0x10000000
#define S626_WS4		0x08000000
#define S626_RSD1		0x01000000	/* Shift A1 data in on SD1. */
#define S626_SDW_A1		0x00800000	/* Store rcv'd char at next char
						 * slot of DWORD1 buffer. */
#define S626_SIB_A1		0x00400000	/* Store rcv'd char at next
						 * char slot of FB1 buffer. */
#define S626_SF_A1		0x00200000	/* Write unsigned long
						 * buffer to input FIFO. */

/* Select parallel-to-serial converter's data source: */
#define S626_XFIFO_0		0x00000000	/* Data fifo byte 0. */
#define S626_XFIFO_1		0x00000010	/* Data fifo byte 1. */
#define S626_XFIFO_2		0x00000020	/* Data fifo byte 2. */
#define S626_XFIFO_3		0x00000030	/* Data fifo byte 3. */
#define S626_XFB0		0x00000040	/* FB_BUFFER byte 0. */
#define S626_XFB1		0x00000050	/* FB_BUFFER byte 1. */
#define S626_XFB2		0x00000060	/* FB_BUFFER byte 2. */
#define S626_XFB3		0x00000070	/* FB_BUFFER byte 3. */
#define S626_SIB_A2		0x00000200	/* Store next dword from A2's
						 * input shifter to FB2
						 * buffer. */
#define S626_SF_A2		0x00000100	/* Store next dword from A2's
						 * input shifter to its input
						 * fifo. */
#define S626_LF_A2		0x00000080	/* Load next dword from A2's
						 * output fifo into its
						 * output dword buffer. */
#define S626_XSD2		0x00000008	/* Shift data out on SD2. */
#define S626_RSD3		0x00001800	/* Shift data in on SD3. */
#define S626_RSD2		0x00001000	/* Shift data in on SD2. */
#define S626_LOW_A2		0x00000002	/* Drive last SD low for 7 clks,
						 * then tri-state. */
#define S626_EOS		0x00000001	/* End of superframe. */

/* I2C configuration constants. */
#define S626_I2C_CLKSEL		0x0400		/* I2C bit rate =
						 * PCIclk/480 = 68.75 KHz. */
#define S626_I2C_BITRATE	68.75		/* I2C bus data bit rate
						 * (determined by
						 * S626_I2C_CLKSEL) in KHz. */
#define S626_I2C_WRTIME		15.0		/* Worst case time, in msec,
						 * for EEPROM internal write
						 * op. */

/* I2C manifest constants. */

/* Max retries to wait for EEPROM write. */
#define S626_I2C_RETRIES	(S626_I2C_WRTIME * S626_I2C_BITRATE / 9.0)
#define S626_I2C_ERR		0x0002	/* I2C control/status flag ERROR. */
#define S626_I2C_BUSY		0x0001	/* I2C control/status flag BUSY. */
#define S626_I2C_ABORT		0x0080	/* I2C status flag ABORT. */
#define S626_I2C_ATTRSTART	0x3	/* I2C attribute START. */
#define S626_I2C_ATTRCONT	0x2	/* I2C attribute CONT. */
#define S626_I2C_ATTRSTOP	0x1	/* I2C attribute STOP. */
#define S626_I2C_ATTRNOP	0x0	/* I2C attribute NOP. */

/* Code macros used for constructing I2C command bytes. */
#define S626_I2C_B2(ATTR, VAL)	(((ATTR) << 6) | ((VAL) << 24))
#define S626_I2C_B1(ATTR, VAL)	(((ATTR) << 4) | ((VAL) << 16))
#define S626_I2C_B0(ATTR, VAL)	(((ATTR) << 2) | ((VAL) <<  8))

/* DEBI command constants. */
#define S626_DEBI_CMD_SIZE16	(2 << 17)	/* Transfer size is always
						 * 2 bytes. */
#define S626_DEBI_CMD_READ	0x00010000	/* Read operation. */
#define S626_DEBI_CMD_WRITE	0x00000000	/* Write operation. */

/* Read immediate 2 bytes. */
#define S626_DEBI_CMD_RDWORD	(S626_DEBI_CMD_READ | S626_DEBI_CMD_SIZE16)

/* Write immediate 2 bytes. */
#define S626_DEBI_CMD_WRWORD	(S626_DEBI_CMD_WRITE | S626_DEBI_CMD_SIZE16)

/* DEBI configuration constants. */
#define S626_DEBI_CFG_XIRQ_EN	0x80000000	/* Enable external interrupt
						 * on GPIO3. */
#define S626_DEBI_CFG_XRESUME	0x40000000	/* Resume block */
						/* Transfer when XIRQ
						 * deasserted. */
#define S626_DEBI_CFG_TOQ	0x03C00000	/* Timeout (15 PCI cycles). */
#define S626_DEBI_CFG_FAST	0x10000000	/* Fast mode enable. */

/* 4-bit field that specifies DEBI timeout value in PCI clock cycles: */
#define S626_DEBI_CFG_TOUT_BIT	22	/* Finish DEBI cycle after this many
					 * clocks. */

/* 2-bit field that specifies Endian byte lane steering: */
#define S626_DEBI_CFG_SWAP_NONE	0x00000000	/* Straight - don't swap any
						 * bytes (Intel). */
#define S626_DEBI_CFG_SWAP_2	0x00100000	/* 2-byte swap (Motorola). */
#define S626_DEBI_CFG_SWAP_4	0x00200000	/* 4-byte swap. */
#define S626_DEBI_CFG_SLAVE16	0x00080000	/* Slave is able to serve
						 * 16-bit cycles. */
#define S626_DEBI_CFG_INC	0x00040000	/* Enable address increment
						 * for block transfers. */
#define S626_DEBI_CFG_INTEL	0x00020000	/* Intel style local bus. */
#define S626_DEBI_CFG_TIMEROFF	0x00010000	/* Disable timer. */

#if S626_PLATFORM == S626_INTEL

#define S626_DEBI_TOUT		7	/* Wait 7 PCI clocks (212 ns) before
					 * polling RDY. */

/* Intel byte lane steering (pass through all byte lanes). */
#define S626_DEBI_SWAP		S626_DEBI_CFG_SWAP_NONE

#elif S626_PLATFORM == S626_MOTOROLA

#define S626_DEBI_TOUT		15	/* Wait 15 PCI clocks (454 ns) maximum
					 * before timing out. */

/* Motorola byte lane steering. */
#define S626_DEBI_SWAP		S626_DEBI_CFG_SWAP_2

#endif

/* DEBI page table constants. */
#define S626_DEBI_PAGE_DISABLE	0x00000000	/* Paging disable. */

/* ******* EXTRA FROM OTHER SENSORAY  * .h  ******* */

/* LoadSrc values: */
#define S626_LOADSRC_INDX	0	/* Preload core in response to Index. */
#define S626_LOADSRC_OVER	1	/* Preload core in response to
					 * Overflow. */
#define S626_LOADSRCB_OVERA	2	/* Preload B core in response to
					 * A Overflow. */
#define S626_LOADSRC_NONE	3	/* Never preload core. */

/* IntSrc values: */
#define S626_INTSRC_NONE	0	/* Interrupts disabled. */
#define S626_INTSRC_OVER	1	/* Interrupt on Overflow. */
#define S626_INTSRC_INDX	2	/* Interrupt on Index. */
#define S626_INTSRC_BOTH	3	/* Interrupt on Index or Overflow. */

/* LatchSrc values: */
#define S626_LATCHSRC_AB_READ	0	/* Latch on read. */
#define S626_LATCHSRC_A_INDXA	1	/* Latch A on A Index. */
#define S626_LATCHSRC_B_INDXB	2	/* Latch B on B Index. */
#define S626_LATCHSRC_B_OVERA	3	/* Latch B on A Overflow. */

/* IndxSrc values: */
#define S626_INDXSRC_HARD	0	/* Hardware or software index. */
#define S626_INDXSRC_SOFT	1	/* Software index only. */

/* IndxPol values: */
#define S626_INDXPOL_POS	0	/* Index input is active high. */
#define S626_INDXPOL_NEG	1	/* Index input is active low. */

/* ClkSrc values: */
#define S626_CLKSRC_COUNTER	0	/* Counter mode. */
#define S626_CLKSRC_TIMER	2	/* Timer mode. */
#define S626_CLKSRC_EXTENDER	3	/* Extender mode. */

/* ClkPol values: */
#define S626_CLKPOL_POS		0	/* Counter/Extender clock is
					 * active high. */
#define S626_CLKPOL_NEG		1	/* Counter/Extender clock is
					 * active low. */
#define S626_CNTDIR_UP		0	/* Timer counts up. */
#define S626_CNTDIR_DOWN	1	/* Timer counts down. */

/* ClkEnab values: */
#define S626_CLKENAB_ALWAYS	0	/* Clock always enabled. */
#define S626_CLKENAB_INDEX	1	/* Clock is enabled by index. */

/* ClkMult values: */
#define S626_CLKMULT_4X		0	/* 4x clock multiplier. */
#define S626_CLKMULT_2X		1	/* 2x clock multiplier. */
#define S626_CLKMULT_1X		2	/* 1x clock multiplier. */

/* Bit Field positions in COUNTER_SETUP structure: */
#define S626_BF_LOADSRC		9	/* Preload trigger. */
#define S626_BF_INDXSRC		7	/* Index source. */
#define S626_BF_INDXPOL		6	/* Index polarity. */
#define S626_BF_CLKSRC		4	/* Clock source. */
#define S626_BF_CLKPOL		3	/* Clock polarity/count direction. */
#define S626_BF_CLKMULT		1	/* Clock multiplier. */
#define S626_BF_CLKENAB		0	/* Clock enable. */

/* Enumerated counter clock multipliers. */

#define S626_MULT_X0		0x0003	/* Supports no multipliers;
					 * fixed physical multiplier = 3. */
#define S626_MULT_X1		0x0002	/* Supports multiplier x1;
					 * fixed physical multiplier = 2. */
#define S626_MULT_X2		0x0001	/* Supports multipliers x1, x2;
					 * physical multipliers = 1 or 2. */
#define S626_MULT_X4		0x0000	/* Supports multipliers x1, x2, x4;
					 * physical multipliers = 0, 1 or 2. */

/* Sanity-check limits for parameters. */

#define S626_NUM_COUNTERS	6	/* Maximum valid counter
					 * logical channel number. */
#define S626_NUM_INTSOURCES	4
#define S626_NUM_LATCHSOURCES	4
#define S626_NUM_CLKMULTS	4
#define S626_NUM_CLKSOURCES	4
#define S626_NUM_CLKPOLS	2
#define S626_NUM_INDEXPOLS	2
#define S626_NUM_INDEXSOURCES	2
#define S626_NUM_LOADTRIGS	4

/* Bit field positions in CRA and CRB counter control registers. */

/* Bit field positions in CRA: */
#define S626_CRABIT_INDXSRC_B	14	/* B index source. */
#define S626_CRABIT_CLKSRC_B	12	/* B clock source. */
#define S626_CRABIT_INDXPOL_A	11	/* A index polarity. */
#define S626_CRABIT_LOADSRC_A	 9	/* A preload trigger. */
#define S626_CRABIT_CLKMULT_A	 7	/* A clock multiplier. */
#define S626_CRABIT_INTSRC_A	 5	/* A interrupt source. */
#define S626_CRABIT_CLKPOL_A	 4	/* A clock polarity. */
#define S626_CRABIT_INDXSRC_A	 2	/* A index source. */
#define S626_CRABIT_CLKSRC_A	 0	/* A clock source. */

/* Bit field positions in CRB: */
#define S626_CRBBIT_INTRESETCMD	15	/* Interrupt reset command. */
#define S626_CRBBIT_INTRESET_B	14	/* B interrupt reset enable. */
#define S626_CRBBIT_INTRESET_A	13	/* A interrupt reset enable. */
#define S626_CRBBIT_CLKENAB_A	12	/* A clock enable. */
#define S626_CRBBIT_INTSRC_B	10	/* B interrupt source. */
#define S626_CRBBIT_LATCHSRC	 8	/* A/B latch source. */
#define S626_CRBBIT_LOADSRC_B	 6	/* B preload trigger. */
#define S626_CRBBIT_CLKMULT_B	 3	/* B clock multiplier. */
#define S626_CRBBIT_CLKENAB_B	 2	/* B clock enable. */
#define S626_CRBBIT_INDXPOL_B	 1	/* B index polarity. */
#define S626_CRBBIT_CLKPOL_B	 0	/* B clock polarity. */

/* Bit field masks for CRA and CRB. */

#define S626_CRAMSK_INDXSRC_B	(3 << S626_CRABIT_INDXSRC_B)
#define S626_CRAMSK_CLKSRC_B	(3 << S626_CRABIT_CLKSRC_B)
#define S626_CRAMSK_INDXPOL_A	(1 << S626_CRABIT_INDXPOL_A)
#define S626_CRAMSK_LOADSRC_A	(3 << S626_CRABIT_LOADSRC_A)
#define S626_CRAMSK_CLKMULT_A	(3 << S626_CRABIT_CLKMULT_A)
#define S626_CRAMSK_INTSRC_A	(3 << S626_CRABIT_INTSRC_A)
#define S626_CRAMSK_CLKPOL_A	(3 << S626_CRABIT_CLKPOL_A)
#define S626_CRAMSK_INDXSRC_A	(3 << S626_CRABIT_INDXSRC_A)
#define S626_CRAMSK_CLKSRC_A	(3 << S626_CRABIT_CLKSRC_A)

#define S626_CRBMSK_INTRESETCMD	(1 << S626_CRBBIT_INTRESETCMD)
#define S626_CRBMSK_INTRESET_B	(1 << S626_CRBBIT_INTRESET_B)
#define S626_CRBMSK_INTRESET_A	(1 << S626_CRBBIT_INTRESET_A)
#define S626_CRBMSK_CLKENAB_A	(1 << S626_CRBBIT_CLKENAB_A)
#define S626_CRBMSK_INTSRC_B	(3 << S626_CRBBIT_INTSRC_B)
#define S626_CRBMSK_LATCHSRC	(3 << S626_CRBBIT_LATCHSRC)
#define S626_CRBMSK_LOADSRC_B	(3 << S626_CRBBIT_LOADSRC_B)
#define S626_CRBMSK_CLKMULT_B	(3 << S626_CRBBIT_CLKMULT_B)
#define S626_CRBMSK_CLKENAB_B	(1 << S626_CRBBIT_CLKENAB_B)
#define S626_CRBMSK_INDXPOL_B	(1 << S626_CRBBIT_INDXPOL_B)
#define S626_CRBMSK_CLKPOL_B	(1 << S626_CRBBIT_CLKPOL_B)

/* Interrupt reset control bits. */
#define S626_CRBMSK_INTCTRL	(S626_CRBMSK_INTRESETCMD | \
				 S626_CRBMSK_INTRESET_A | \
				 S626_CRBMSK_INTRESET_B)

/* Bit field positions for standardized SETUP structure. */

#define S626_STDBIT_INTSRC	13
#define S626_STDBIT_LATCHSRC	11
#define S626_STDBIT_LOADSRC	 9
#define S626_STDBIT_INDXSRC	 7
#define S626_STDBIT_INDXPOL	 6
#define S626_STDBIT_CLKSRC	 4
#define S626_STDBIT_CLKPOL	 3
#define S626_STDBIT_CLKMULT	 1
#define S626_STDBIT_CLKENAB	 0

/* Bit field masks for standardized SETUP structure. */

#define S626_STDMSK_INTSRC	(3 << S626_STDBIT_INTSRC)
#define S626_STDMSK_LATCHSRC	(3 << S626_STDBIT_LATCHSRC)
#define S626_STDMSK_LOADSRC	(3 << S626_STDBIT_LOADSRC)
#define S626_STDMSK_INDXSRC	(1 << S626_STDBIT_INDXSRC)
#define S626_STDMSK_INDXPOL	(1 << S626_STDBIT_INDXPOL)
#define S626_STDMSK_CLKSRC	(3 << S626_STDBIT_CLKSRC)
#define S626_STDMSK_CLKPOL	(1 << S626_STDBIT_CLKPOL)
#define S626_STDMSK_CLKMULT	(3 << S626_STDBIT_CLKMULT)
#define S626_STDMSK_CLKENAB	(1 << S626_STDBIT_CLKENAB)

#endif

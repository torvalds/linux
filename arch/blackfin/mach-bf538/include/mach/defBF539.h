/*
 * Copyright 2008-2009 Analog Devices Inc.
 *
 * Licensed under the ADI BSD license or the GPL-2 (or later)
 */

/* SYSTEM & MM REGISTER BIT & ADDRESS DEFINITIONS FOR ADSP-BF538/9 */

#ifndef _DEF_BF539_H
#define _DEF_BF539_H

/* include all Core registers and bit definitions */
#include <asm/def_LPBlackfin.h>


/*********************************************************************************** */
/* System MMR Register Map */
/*********************************************************************************** */
/* Clock/Regulator Control (0xFFC00000 - 0xFFC000FF) */
#define	PLL_CTL			0xFFC00000	/* PLL Control register (16-bit) */
#define	PLL_DIV			0xFFC00004	/* PLL Divide Register (16-bit) */
#define	VR_CTL			0xFFC00008	/* Voltage Regulator Control Register (16-bit) */
#define	PLL_STAT		0xFFC0000C	/* PLL Status register (16-bit) */
#define	PLL_LOCKCNT		0xFFC00010	/* PLL Lock	Count register (16-bit) */
#define	CHIPID			0xFFC00014	/* Chip	ID Register */

/* CHIPID Masks */
#define CHIPID_VERSION         0xF0000000
#define CHIPID_FAMILY          0x0FFFF000
#define CHIPID_MANUFACTURE     0x00000FFE

/* System Interrupt Controller (0xFFC00100 - 0xFFC001FF) */
#define	SWRST			0xFFC00100  /* Software	Reset Register (16-bit) */
#define	SYSCR			0xFFC00104  /* System Configuration registe */
#define	SIC_RVECT		0xFFC00108
#define	SIC_IMASK0		0xFFC0010C  /* Interrupt Mask Register */
#define	SIC_IAR0		0xFFC00110  /* Interrupt Assignment Register 0 */
#define	SIC_IAR1		0xFFC00114  /* Interrupt Assignment Register 1 */
#define	SIC_IAR2		0xFFC00118  /* Interrupt Assignment Register 2 */
#define	SIC_IAR3			0xFFC0011C	/* Interrupt Assignment	Register 3 */
#define	SIC_ISR0			0xFFC00120  /* Interrupt Status	Register */
#define	SIC_IWR0			0xFFC00124  /* Interrupt Wakeup	Register */
#define	SIC_IMASK1			0xFFC00128	/* Interrupt Mask Register 1 */
#define	SIC_ISR1			0xFFC0012C	/* Interrupt Status Register 1 */
#define	SIC_IWR1			0xFFC00130	/* Interrupt Wakeup Register 1 */
#define	SIC_IAR4			0xFFC00134	/* Interrupt Assignment	Register 4 */
#define	SIC_IAR5			0xFFC00138	/* Interrupt Assignment	Register 5 */
#define	SIC_IAR6			0xFFC0013C	/* Interrupt Assignment	Register 6 */


/* Watchdog Timer (0xFFC00200 -	0xFFC002FF) */
#define	WDOG_CTL	0xFFC00200  /* Watchdog	Control	Register */
#define	WDOG_CNT	0xFFC00204  /* Watchdog	Count Register */
#define	WDOG_STAT	0xFFC00208  /* Watchdog	Status Register */


/* Real	Time Clock (0xFFC00300 - 0xFFC003FF) */
#define	RTC_STAT	0xFFC00300  /* RTC Status Register */
#define	RTC_ICTL	0xFFC00304  /* RTC Interrupt Control Register */
#define	RTC_ISTAT	0xFFC00308  /* RTC Interrupt Status Register */
#define	RTC_SWCNT	0xFFC0030C  /* RTC Stopwatch Count Register */
#define	RTC_ALARM	0xFFC00310  /* RTC Alarm Time Register */
#define	RTC_FAST	0xFFC00314  /* RTC Prescaler Enable Register */
#define	RTC_PREN		0xFFC00314  /* RTC Prescaler Enable Register (alternate	macro) */


/* UART0 Controller (0xFFC00400	- 0xFFC004FF) */
#define	UART0_THR	      0xFFC00400  /* Transmit Holding register */
#define	UART0_RBR	      0xFFC00400  /* Receive Buffer register */
#define	UART0_DLL	      0xFFC00400  /* Divisor Latch (Low-Byte) */
#define	UART0_IER	      0xFFC00404  /* Interrupt Enable Register */
#define	UART0_DLH	      0xFFC00404  /* Divisor Latch (High-Byte) */
#define	UART0_IIR	      0xFFC00408  /* Interrupt Identification Register */
#define	UART0_LCR	      0xFFC0040C  /* Line Control Register */
#define	UART0_MCR			 0xFFC00410  /*	Modem Control Register */
#define	UART0_LSR	      0xFFC00414  /* Line Status Register */
#define	UART0_SCR	      0xFFC0041C  /* SCR Scratch Register */
#define	UART0_GCTL		     0xFFC00424	 /* Global Control Register */


/* SPI0	Controller (0xFFC00500 - 0xFFC005FF) */

#define	SPI0_CTL			0xFFC00500  /* SPI0 Control Register */
#define	SPI0_FLG			0xFFC00504  /* SPI0 Flag register */
#define	SPI0_STAT			0xFFC00508  /* SPI0 Status register */
#define	SPI0_TDBR			0xFFC0050C  /* SPI0 Transmit Data Buffer Register */
#define	SPI0_RDBR			0xFFC00510  /* SPI0 Receive Data Buffer	Register */
#define	SPI0_BAUD			0xFFC00514  /* SPI0 Baud rate Register */
#define	SPI0_SHADOW			0xFFC00518  /* SPI0_RDBR Shadow	Register */
#define SPI0_REGBASE			SPI0_CTL


/* TIMER 0, 1, 2 Registers (0xFFC00600 - 0xFFC006FF) */
#define	TIMER0_CONFIG			0xFFC00600     /* Timer	0 Configuration	Register */
#define	TIMER0_COUNTER				0xFFC00604     /* Timer	0 Counter Register */
#define	TIMER0_PERIOD			0xFFC00608     /* Timer	0 Period Register */
#define	TIMER0_WIDTH			0xFFC0060C     /* Timer	0 Width	Register */

#define	TIMER1_CONFIG			0xFFC00610	/*  Timer 1 Configuration Register   */
#define	TIMER1_COUNTER			0xFFC00614	/*  Timer 1 Counter Register	     */
#define	TIMER1_PERIOD			0xFFC00618	/*  Timer 1 Period Register	     */
#define	TIMER1_WIDTH			0xFFC0061C	/*  Timer 1 Width Register	     */

#define	TIMER2_CONFIG			0xFFC00620	/* Timer 2 Configuration Register   */
#define	TIMER2_COUNTER			0xFFC00624	/* Timer 2 Counter Register	    */
#define	TIMER2_PERIOD			0xFFC00628	/* Timer 2 Period Register	    */
#define	TIMER2_WIDTH			0xFFC0062C	/* Timer 2 Width Register	    */

#define	TIMER_ENABLE				0xFFC00640	/* Timer Enable	Register */
#define	TIMER_DISABLE				0xFFC00644	/* Timer Disable Register */
#define	TIMER_STATUS				0xFFC00648	/* Timer Status	Register */


/* Programmable	Flags (0xFFC00700 - 0xFFC007FF) */
#define	FIO_FLAG_D				0xFFC00700  /* Flag Mask to directly specify state of pins */
#define	FIO_FLAG_C			0xFFC00704  /* Peripheral Interrupt Flag Register (clear) */
#define	FIO_FLAG_S			0xFFC00708  /* Peripheral Interrupt Flag Register (set) */
#define	FIO_FLAG_T					0xFFC0070C  /* Flag Mask to directly toggle state of pins */
#define	FIO_MASKA_D			0xFFC00710  /* Flag Mask Interrupt A Register (set directly) */
#define	FIO_MASKA_C			0xFFC00714  /* Flag Mask Interrupt A Register (clear) */
#define	FIO_MASKA_S			0xFFC00718  /* Flag Mask Interrupt A Register (set) */
#define	FIO_MASKA_T			0xFFC0071C  /* Flag Mask Interrupt A Register (toggle) */
#define	FIO_MASKB_D			0xFFC00720  /* Flag Mask Interrupt B Register (set directly) */
#define	FIO_MASKB_C			0xFFC00724  /* Flag Mask Interrupt B Register (clear) */
#define	FIO_MASKB_S			0xFFC00728  /* Flag Mask Interrupt B Register (set) */
#define	FIO_MASKB_T			0xFFC0072C  /* Flag Mask Interrupt B Register (toggle) */
#define	FIO_DIR				0xFFC00730  /* Peripheral Flag Direction Register */
#define	FIO_POLAR			0xFFC00734  /* Flag Source Polarity Register */
#define	FIO_EDGE			0xFFC00738  /* Flag Source Sensitivity Register */
#define	FIO_BOTH			0xFFC0073C  /* Flag Set	on BOTH	Edges Register */
#define	FIO_INEN					0xFFC00740  /* Flag Input Enable Register  */


/* SPORT0 Controller (0xFFC00800 - 0xFFC008FF) */
#define	SPORT0_TCR1				0xFFC00800  /* SPORT0 Transmit Configuration 1 Register */
#define	SPORT0_TCR2				0xFFC00804  /* SPORT0 Transmit Configuration 2 Register */
#define	SPORT0_TCLKDIV			0xFFC00808  /* SPORT0 Transmit Clock Divider */
#define	SPORT0_TFSDIV			0xFFC0080C  /* SPORT0 Transmit Frame Sync Divider */
#define	SPORT0_TX			0xFFC00810  /* SPORT0 TX Data Register */
#define	SPORT0_RX			0xFFC00818  /* SPORT0 RX Data Register */
#define	SPORT0_RCR1				0xFFC00820  /* SPORT0 Transmit Configuration 1 Register */
#define	SPORT0_RCR2				0xFFC00824  /* SPORT0 Transmit Configuration 2 Register */
#define	SPORT0_RCLKDIV			0xFFC00828  /* SPORT0 Receive Clock Divider */
#define	SPORT0_RFSDIV			0xFFC0082C  /* SPORT0 Receive Frame Sync Divider */
#define	SPORT0_STAT			0xFFC00830  /* SPORT0 Status Register */
#define	SPORT0_CHNL			0xFFC00834  /* SPORT0 Current Channel Register */
#define	SPORT0_MCMC1			0xFFC00838  /* SPORT0 Multi-Channel Configuration Register 1 */
#define	SPORT0_MCMC2			0xFFC0083C  /* SPORT0 Multi-Channel Configuration Register 2 */
#define	SPORT0_MTCS0			0xFFC00840  /* SPORT0 Multi-Channel Transmit Select Register 0 */
#define	SPORT0_MTCS1			0xFFC00844  /* SPORT0 Multi-Channel Transmit Select Register 1 */
#define	SPORT0_MTCS2			0xFFC00848  /* SPORT0 Multi-Channel Transmit Select Register 2 */
#define	SPORT0_MTCS3			0xFFC0084C  /* SPORT0 Multi-Channel Transmit Select Register 3 */
#define	SPORT0_MRCS0			0xFFC00850  /* SPORT0 Multi-Channel Receive Select Register 0 */
#define	SPORT0_MRCS1			0xFFC00854  /* SPORT0 Multi-Channel Receive Select Register 1 */
#define	SPORT0_MRCS2			0xFFC00858  /* SPORT0 Multi-Channel Receive Select Register 2 */
#define	SPORT0_MRCS3			0xFFC0085C  /* SPORT0 Multi-Channel Receive Select Register 3 */


/* SPORT1 Controller (0xFFC00900 - 0xFFC009FF) */
#define	SPORT1_TCR1				0xFFC00900  /* SPORT1 Transmit Configuration 1 Register */
#define	SPORT1_TCR2				0xFFC00904  /* SPORT1 Transmit Configuration 2 Register */
#define	SPORT1_TCLKDIV			0xFFC00908  /* SPORT1 Transmit Clock Divider */
#define	SPORT1_TFSDIV			0xFFC0090C  /* SPORT1 Transmit Frame Sync Divider */
#define	SPORT1_TX			0xFFC00910  /* SPORT1 TX Data Register */
#define	SPORT1_RX			0xFFC00918  /* SPORT1 RX Data Register */
#define	SPORT1_RCR1				0xFFC00920  /* SPORT1 Transmit Configuration 1 Register */
#define	SPORT1_RCR2				0xFFC00924  /* SPORT1 Transmit Configuration 2 Register */
#define	SPORT1_RCLKDIV			0xFFC00928  /* SPORT1 Receive Clock Divider */
#define	SPORT1_RFSDIV			0xFFC0092C  /* SPORT1 Receive Frame Sync Divider */
#define	SPORT1_STAT			0xFFC00930  /* SPORT1 Status Register */
#define	SPORT1_CHNL			0xFFC00934  /* SPORT1 Current Channel Register */
#define	SPORT1_MCMC1			0xFFC00938  /* SPORT1 Multi-Channel Configuration Register 1 */
#define	SPORT1_MCMC2			0xFFC0093C  /* SPORT1 Multi-Channel Configuration Register 2 */
#define	SPORT1_MTCS0			0xFFC00940  /* SPORT1 Multi-Channel Transmit Select Register 0 */
#define	SPORT1_MTCS1			0xFFC00944  /* SPORT1 Multi-Channel Transmit Select Register 1 */
#define	SPORT1_MTCS2			0xFFC00948  /* SPORT1 Multi-Channel Transmit Select Register 2 */
#define	SPORT1_MTCS3			0xFFC0094C  /* SPORT1 Multi-Channel Transmit Select Register 3 */
#define	SPORT1_MRCS0			0xFFC00950  /* SPORT1 Multi-Channel Receive Select Register 0 */
#define	SPORT1_MRCS1			0xFFC00954  /* SPORT1 Multi-Channel Receive Select Register 1 */
#define	SPORT1_MRCS2			0xFFC00958  /* SPORT1 Multi-Channel Receive Select Register 2 */
#define	SPORT1_MRCS3			0xFFC0095C  /* SPORT1 Multi-Channel Receive Select Register 3 */


/* External Bus	Interface Unit (0xFFC00A00 - 0xFFC00AFF) */
/* Asynchronous	Memory Controller  */
#define	EBIU_AMGCTL			0xFFC00A00  /* Asynchronous Memory Global Control Register */
#define	EBIU_AMBCTL0		0xFFC00A04  /* Asynchronous Memory Bank	Control	Register 0 */
#define	EBIU_AMBCTL1		0xFFC00A08  /* Asynchronous Memory Bank	Control	Register 1 */

/* SDRAM Controller */
#define	EBIU_SDGCTL			0xFFC00A10  /* SDRAM Global Control Register */
#define	EBIU_SDBCTL			0xFFC00A14  /* SDRAM Bank Control Register */
#define	EBIU_SDRRC			0xFFC00A18  /* SDRAM Refresh Rate Control Register */
#define	EBIU_SDSTAT			0xFFC00A1C  /* SDRAM Status Register */



/* DMA Controller 0 Traffic Control Registers (0xFFC00B00 - 0xFFC00BFF) */

#define	DMAC0_TC_PER			0xFFC00B0C	/* DMA Controller 0 Traffic Control Periods Register */
#define	DMAC0_TC_CNT			0xFFC00B10	/* DMA Controller 0 Traffic Control Current Counts Register */

/* Alternate deprecated	register names (below) provided	for backwards code compatibility */
#define	DMA0_TCPER			DMAC0_TC_PER
#define	DMA0_TCCNT			DMAC0_TC_CNT


/* DMA Controller 0 (0xFFC00C00	- 0xFFC00FFF)							 */

#define	DMA0_NEXT_DESC_PTR		0xFFC00C00	/* DMA Channel 0 Next Descriptor Pointer Register */
#define	DMA0_START_ADDR			0xFFC00C04	/* DMA Channel 0 Start Address Register */
#define	DMA0_CONFIG				0xFFC00C08	/* DMA Channel 0 Configuration Register */
#define	DMA0_X_COUNT			0xFFC00C10	/* DMA Channel 0 X Count Register */
#define	DMA0_X_MODIFY			0xFFC00C14	/* DMA Channel 0 X Modify Register */
#define	DMA0_Y_COUNT			0xFFC00C18	/* DMA Channel 0 Y Count Register */
#define	DMA0_Y_MODIFY			0xFFC00C1C	/* DMA Channel 0 Y Modify Register */
#define	DMA0_CURR_DESC_PTR		0xFFC00C20	/* DMA Channel 0 Current Descriptor Pointer Register */
#define	DMA0_CURR_ADDR			0xFFC00C24	/* DMA Channel 0 Current Address Register */
#define	DMA0_IRQ_STATUS			0xFFC00C28	/* DMA Channel 0 Interrupt/Status Register */
#define	DMA0_PERIPHERAL_MAP		0xFFC00C2C	/* DMA Channel 0 Peripheral Map	Register */
#define	DMA0_CURR_X_COUNT		0xFFC00C30	/* DMA Channel 0 Current X Count Register */
#define	DMA0_CURR_Y_COUNT		0xFFC00C38	/* DMA Channel 0 Current Y Count Register */

#define	DMA1_NEXT_DESC_PTR		0xFFC00C40	/* DMA Channel 1 Next Descriptor Pointer Register */
#define	DMA1_START_ADDR			0xFFC00C44	/* DMA Channel 1 Start Address Register */
#define	DMA1_CONFIG				0xFFC00C48	/* DMA Channel 1 Configuration Register */
#define	DMA1_X_COUNT			0xFFC00C50	/* DMA Channel 1 X Count Register */
#define	DMA1_X_MODIFY			0xFFC00C54	/* DMA Channel 1 X Modify Register */
#define	DMA1_Y_COUNT			0xFFC00C58	/* DMA Channel 1 Y Count Register */
#define	DMA1_Y_MODIFY			0xFFC00C5C	/* DMA Channel 1 Y Modify Register */
#define	DMA1_CURR_DESC_PTR		0xFFC00C60	/* DMA Channel 1 Current Descriptor Pointer Register */
#define	DMA1_CURR_ADDR			0xFFC00C64	/* DMA Channel 1 Current Address Register */
#define	DMA1_IRQ_STATUS			0xFFC00C68	/* DMA Channel 1 Interrupt/Status Register */
#define	DMA1_PERIPHERAL_MAP		0xFFC00C6C	/* DMA Channel 1 Peripheral Map	Register */
#define	DMA1_CURR_X_COUNT		0xFFC00C70	/* DMA Channel 1 Current X Count Register */
#define	DMA1_CURR_Y_COUNT		0xFFC00C78	/* DMA Channel 1 Current Y Count Register */

#define	DMA2_NEXT_DESC_PTR		0xFFC00C80	/* DMA Channel 2 Next Descriptor Pointer Register */
#define	DMA2_START_ADDR			0xFFC00C84	/* DMA Channel 2 Start Address Register */
#define	DMA2_CONFIG				0xFFC00C88	/* DMA Channel 2 Configuration Register */
#define	DMA2_X_COUNT			0xFFC00C90	/* DMA Channel 2 X Count Register */
#define	DMA2_X_MODIFY			0xFFC00C94	/* DMA Channel 2 X Modify Register */
#define	DMA2_Y_COUNT			0xFFC00C98	/* DMA Channel 2 Y Count Register */
#define	DMA2_Y_MODIFY			0xFFC00C9C	/* DMA Channel 2 Y Modify Register */
#define	DMA2_CURR_DESC_PTR		0xFFC00CA0	/* DMA Channel 2 Current Descriptor Pointer Register */
#define	DMA2_CURR_ADDR			0xFFC00CA4	/* DMA Channel 2 Current Address Register */
#define	DMA2_IRQ_STATUS			0xFFC00CA8	/* DMA Channel 2 Interrupt/Status Register */
#define	DMA2_PERIPHERAL_MAP		0xFFC00CAC	/* DMA Channel 2 Peripheral Map	Register */
#define	DMA2_CURR_X_COUNT		0xFFC00CB0	/* DMA Channel 2 Current X Count Register */
#define	DMA2_CURR_Y_COUNT		0xFFC00CB8	/* DMA Channel 2 Current Y Count Register */

#define	DMA3_NEXT_DESC_PTR		0xFFC00CC0	/* DMA Channel 3 Next Descriptor Pointer Register */
#define	DMA3_START_ADDR			0xFFC00CC4	/* DMA Channel 3 Start Address Register */
#define	DMA3_CONFIG				0xFFC00CC8	/* DMA Channel 3 Configuration Register */
#define	DMA3_X_COUNT			0xFFC00CD0	/* DMA Channel 3 X Count Register */
#define	DMA3_X_MODIFY			0xFFC00CD4	/* DMA Channel 3 X Modify Register */
#define	DMA3_Y_COUNT			0xFFC00CD8	/* DMA Channel 3 Y Count Register */
#define	DMA3_Y_MODIFY			0xFFC00CDC	/* DMA Channel 3 Y Modify Register */
#define	DMA3_CURR_DESC_PTR		0xFFC00CE0	/* DMA Channel 3 Current Descriptor Pointer Register */
#define	DMA3_CURR_ADDR			0xFFC00CE4	/* DMA Channel 3 Current Address Register */
#define	DMA3_IRQ_STATUS			0xFFC00CE8	/* DMA Channel 3 Interrupt/Status Register */
#define	DMA3_PERIPHERAL_MAP		0xFFC00CEC	/* DMA Channel 3 Peripheral Map	Register */
#define	DMA3_CURR_X_COUNT		0xFFC00CF0	/* DMA Channel 3 Current X Count Register */
#define	DMA3_CURR_Y_COUNT		0xFFC00CF8	/* DMA Channel 3 Current Y Count Register */

#define	DMA4_NEXT_DESC_PTR		0xFFC00D00	/* DMA Channel 4 Next Descriptor Pointer Register */
#define	DMA4_START_ADDR			0xFFC00D04	/* DMA Channel 4 Start Address Register */
#define	DMA4_CONFIG				0xFFC00D08	/* DMA Channel 4 Configuration Register */
#define	DMA4_X_COUNT			0xFFC00D10	/* DMA Channel 4 X Count Register */
#define	DMA4_X_MODIFY			0xFFC00D14	/* DMA Channel 4 X Modify Register */
#define	DMA4_Y_COUNT			0xFFC00D18	/* DMA Channel 4 Y Count Register */
#define	DMA4_Y_MODIFY			0xFFC00D1C	/* DMA Channel 4 Y Modify Register */
#define	DMA4_CURR_DESC_PTR		0xFFC00D20	/* DMA Channel 4 Current Descriptor Pointer Register */
#define	DMA4_CURR_ADDR			0xFFC00D24	/* DMA Channel 4 Current Address Register */
#define	DMA4_IRQ_STATUS			0xFFC00D28	/* DMA Channel 4 Interrupt/Status Register */
#define	DMA4_PERIPHERAL_MAP		0xFFC00D2C	/* DMA Channel 4 Peripheral Map	Register */
#define	DMA4_CURR_X_COUNT		0xFFC00D30	/* DMA Channel 4 Current X Count Register */
#define	DMA4_CURR_Y_COUNT		0xFFC00D38	/* DMA Channel 4 Current Y Count Register */

#define	DMA5_NEXT_DESC_PTR		0xFFC00D40	/* DMA Channel 5 Next Descriptor Pointer Register */
#define	DMA5_START_ADDR			0xFFC00D44	/* DMA Channel 5 Start Address Register */
#define	DMA5_CONFIG				0xFFC00D48	/* DMA Channel 5 Configuration Register */
#define	DMA5_X_COUNT			0xFFC00D50	/* DMA Channel 5 X Count Register */
#define	DMA5_X_MODIFY			0xFFC00D54	/* DMA Channel 5 X Modify Register */
#define	DMA5_Y_COUNT			0xFFC00D58	/* DMA Channel 5 Y Count Register */
#define	DMA5_Y_MODIFY			0xFFC00D5C	/* DMA Channel 5 Y Modify Register */
#define	DMA5_CURR_DESC_PTR		0xFFC00D60	/* DMA Channel 5 Current Descriptor Pointer Register */
#define	DMA5_CURR_ADDR			0xFFC00D64	/* DMA Channel 5 Current Address Register */
#define	DMA5_IRQ_STATUS			0xFFC00D68	/* DMA Channel 5 Interrupt/Status Register */
#define	DMA5_PERIPHERAL_MAP		0xFFC00D6C	/* DMA Channel 5 Peripheral Map	Register */
#define	DMA5_CURR_X_COUNT		0xFFC00D70	/* DMA Channel 5 Current X Count Register */
#define	DMA5_CURR_Y_COUNT		0xFFC00D78	/* DMA Channel 5 Current Y Count Register */

#define	DMA6_NEXT_DESC_PTR		0xFFC00D80	/* DMA Channel 6 Next Descriptor Pointer Register */
#define	DMA6_START_ADDR			0xFFC00D84	/* DMA Channel 6 Start Address Register */
#define	DMA6_CONFIG				0xFFC00D88	/* DMA Channel 6 Configuration Register */
#define	DMA6_X_COUNT			0xFFC00D90	/* DMA Channel 6 X Count Register */
#define	DMA6_X_MODIFY			0xFFC00D94	/* DMA Channel 6 X Modify Register */
#define	DMA6_Y_COUNT			0xFFC00D98	/* DMA Channel 6 Y Count Register */
#define	DMA6_Y_MODIFY			0xFFC00D9C	/* DMA Channel 6 Y Modify Register */
#define	DMA6_CURR_DESC_PTR		0xFFC00DA0	/* DMA Channel 6 Current Descriptor Pointer Register */
#define	DMA6_CURR_ADDR			0xFFC00DA4	/* DMA Channel 6 Current Address Register */
#define	DMA6_IRQ_STATUS			0xFFC00DA8	/* DMA Channel 6 Interrupt/Status Register */
#define	DMA6_PERIPHERAL_MAP		0xFFC00DAC	/* DMA Channel 6 Peripheral Map	Register */
#define	DMA6_CURR_X_COUNT		0xFFC00DB0	/* DMA Channel 6 Current X Count Register */
#define	DMA6_CURR_Y_COUNT		0xFFC00DB8	/* DMA Channel 6 Current Y Count Register */

#define	DMA7_NEXT_DESC_PTR		0xFFC00DC0	/* DMA Channel 7 Next Descriptor Pointer Register */
#define	DMA7_START_ADDR			0xFFC00DC4	/* DMA Channel 7 Start Address Register */
#define	DMA7_CONFIG				0xFFC00DC8	/* DMA Channel 7 Configuration Register */
#define	DMA7_X_COUNT			0xFFC00DD0	/* DMA Channel 7 X Count Register */
#define	DMA7_X_MODIFY			0xFFC00DD4	/* DMA Channel 7 X Modify Register */
#define	DMA7_Y_COUNT			0xFFC00DD8	/* DMA Channel 7 Y Count Register */
#define	DMA7_Y_MODIFY			0xFFC00DDC	/* DMA Channel 7 Y Modify Register */
#define	DMA7_CURR_DESC_PTR		0xFFC00DE0	/* DMA Channel 7 Current Descriptor Pointer Register */
#define	DMA7_CURR_ADDR			0xFFC00DE4	/* DMA Channel 7 Current Address Register */
#define	DMA7_IRQ_STATUS			0xFFC00DE8	/* DMA Channel 7 Interrupt/Status Register */
#define	DMA7_PERIPHERAL_MAP		0xFFC00DEC	/* DMA Channel 7 Peripheral Map	Register */
#define	DMA7_CURR_X_COUNT		0xFFC00DF0	/* DMA Channel 7 Current X Count Register */
#define	DMA7_CURR_Y_COUNT		0xFFC00DF8	/* DMA Channel 7 Current Y Count Register */

#define	MDMA0_D0_NEXT_DESC_PTR	0xFFC00E00	/* MemDMA0 Stream 0 Destination	Next Descriptor	Pointer	Register */
#define	MDMA0_D0_START_ADDR		0xFFC00E04	/* MemDMA0 Stream 0 Destination	Start Address Register */
#define	MDMA0_D0_CONFIG			0xFFC00E08	/* MemDMA0 Stream 0 Destination	Configuration Register */
#define	MDMA0_D0_X_COUNT		0xFFC00E10	/* MemDMA0 Stream 0 Destination	X Count	Register */
#define	MDMA0_D0_X_MODIFY		0xFFC00E14	/* MemDMA0 Stream 0 Destination	X Modify Register */
#define	MDMA0_D0_Y_COUNT		0xFFC00E18	/* MemDMA0 Stream 0 Destination	Y Count	Register */
#define	MDMA0_D0_Y_MODIFY		0xFFC00E1C	/* MemDMA0 Stream 0 Destination	Y Modify Register */
#define	MDMA0_D0_CURR_DESC_PTR	0xFFC00E20	/* MemDMA0 Stream 0 Destination	Current	Descriptor Pointer Register */
#define	MDMA0_D0_CURR_ADDR		0xFFC00E24	/* MemDMA0 Stream 0 Destination	Current	Address	Register */
#define	MDMA0_D0_IRQ_STATUS		0xFFC00E28	/* MemDMA0 Stream 0 Destination	Interrupt/Status Register */
#define	MDMA0_D0_PERIPHERAL_MAP	0xFFC00E2C	/* MemDMA0 Stream 0 Destination	Peripheral Map Register */
#define	MDMA0_D0_CURR_X_COUNT	0xFFC00E30	/* MemDMA0 Stream 0 Destination	Current	X Count	Register */
#define	MDMA0_D0_CURR_Y_COUNT	0xFFC00E38	/* MemDMA0 Stream 0 Destination	Current	Y Count	Register */

#define	MDMA0_S0_NEXT_DESC_PTR	0xFFC00E40	/* MemDMA0 Stream 0 Source Next	Descriptor Pointer Register */
#define	MDMA0_S0_START_ADDR		0xFFC00E44	/* MemDMA0 Stream 0 Source Start Address Register */
#define	MDMA0_S0_CONFIG			0xFFC00E48	/* MemDMA0 Stream 0 Source Configuration Register */
#define	MDMA0_S0_X_COUNT		0xFFC00E50	/* MemDMA0 Stream 0 Source X Count Register */
#define	MDMA0_S0_X_MODIFY		0xFFC00E54	/* MemDMA0 Stream 0 Source X Modify Register */
#define	MDMA0_S0_Y_COUNT		0xFFC00E58	/* MemDMA0 Stream 0 Source Y Count Register */
#define	MDMA0_S0_Y_MODIFY		0xFFC00E5C	/* MemDMA0 Stream 0 Source Y Modify Register */
#define	MDMA0_S0_CURR_DESC_PTR	0xFFC00E60	/* MemDMA0 Stream 0 Source Current Descriptor Pointer Register */
#define	MDMA0_S0_CURR_ADDR		0xFFC00E64	/* MemDMA0 Stream 0 Source Current Address Register */
#define	MDMA0_S0_IRQ_STATUS		0xFFC00E68	/* MemDMA0 Stream 0 Source Interrupt/Status Register */
#define	MDMA0_S0_PERIPHERAL_MAP	0xFFC00E6C	/* MemDMA0 Stream 0 Source Peripheral Map Register */
#define	MDMA0_S0_CURR_X_COUNT	0xFFC00E70	/* MemDMA0 Stream 0 Source Current X Count Register */
#define	MDMA0_S0_CURR_Y_COUNT	0xFFC00E78	/* MemDMA0 Stream 0 Source Current Y Count Register */

#define	MDMA0_D1_NEXT_DESC_PTR	0xFFC00E80	/* MemDMA0 Stream 1 Destination	Next Descriptor	Pointer	Register */
#define	MDMA0_D1_START_ADDR		0xFFC00E84	/* MemDMA0 Stream 1 Destination	Start Address Register */
#define	MDMA0_D1_CONFIG			0xFFC00E88	/* MemDMA0 Stream 1 Destination	Configuration Register */
#define	MDMA0_D1_X_COUNT		0xFFC00E90	/* MemDMA0 Stream 1 Destination	X Count	Register */
#define	MDMA0_D1_X_MODIFY		0xFFC00E94	/* MemDMA0 Stream 1 Destination	X Modify Register */
#define	MDMA0_D1_Y_COUNT		0xFFC00E98	/* MemDMA0 Stream 1 Destination	Y Count	Register */
#define	MDMA0_D1_Y_MODIFY		0xFFC00E9C	/* MemDMA0 Stream 1 Destination	Y Modify Register */
#define	MDMA0_D1_CURR_DESC_PTR	0xFFC00EA0	/* MemDMA0 Stream 1 Destination	Current	Descriptor Pointer Register */
#define	MDMA0_D1_CURR_ADDR		0xFFC00EA4	/* MemDMA0 Stream 1 Destination	Current	Address	Register */
#define	MDMA0_D1_IRQ_STATUS		0xFFC00EA8	/* MemDMA0 Stream 1 Destination	Interrupt/Status Register */
#define	MDMA0_D1_PERIPHERAL_MAP	0xFFC00EAC	/* MemDMA0 Stream 1 Destination	Peripheral Map Register */
#define	MDMA0_D1_CURR_X_COUNT	0xFFC00EB0	/* MemDMA0 Stream 1 Destination	Current	X Count	Register */
#define	MDMA0_D1_CURR_Y_COUNT	0xFFC00EB8	/* MemDMA0 Stream 1 Destination	Current	Y Count	Register */

#define	MDMA0_S1_NEXT_DESC_PTR	0xFFC00EC0	/* MemDMA0 Stream 1 Source Next	Descriptor Pointer Register */
#define	MDMA0_S1_START_ADDR		0xFFC00EC4	/* MemDMA0 Stream 1 Source Start Address Register */
#define	MDMA0_S1_CONFIG			0xFFC00EC8	/* MemDMA0 Stream 1 Source Configuration Register */
#define	MDMA0_S1_X_COUNT		0xFFC00ED0	/* MemDMA0 Stream 1 Source X Count Register */
#define	MDMA0_S1_X_MODIFY		0xFFC00ED4	/* MemDMA0 Stream 1 Source X Modify Register */
#define	MDMA0_S1_Y_COUNT		0xFFC00ED8	/* MemDMA0 Stream 1 Source Y Count Register */
#define	MDMA0_S1_Y_MODIFY		0xFFC00EDC	/* MemDMA0 Stream 1 Source Y Modify Register */
#define	MDMA0_S1_CURR_DESC_PTR	0xFFC00EE0	/* MemDMA0 Stream 1 Source Current Descriptor Pointer Register */
#define	MDMA0_S1_CURR_ADDR		0xFFC00EE4	/* MemDMA0 Stream 1 Source Current Address Register */
#define	MDMA0_S1_IRQ_STATUS		0xFFC00EE8	/* MemDMA0 Stream 1 Source Interrupt/Status Register */
#define	MDMA0_S1_PERIPHERAL_MAP	0xFFC00EEC	/* MemDMA0 Stream 1 Source Peripheral Map Register */
#define	MDMA0_S1_CURR_X_COUNT	0xFFC00EF0	/* MemDMA0 Stream 1 Source Current X Count Register */
#define	MDMA0_S1_CURR_Y_COUNT	0xFFC00EF8	/* MemDMA0 Stream 1 Source Current Y Count Register */

#define MDMA_D0_NEXT_DESC_PTR MDMA0_D0_NEXT_DESC_PTR
#define MDMA_D0_START_ADDR MDMA0_D0_START_ADDR
#define MDMA_D0_CONFIG MDMA0_D0_CONFIG
#define MDMA_D0_X_COUNT MDMA0_D0_X_COUNT
#define MDMA_D0_X_MODIFY MDMA0_D0_X_MODIFY
#define MDMA_D0_Y_COUNT MDMA0_D0_Y_COUNT
#define MDMA_D0_Y_MODIFY MDMA0_D0_Y_MODIFY
#define MDMA_D0_CURR_DESC_PTR MDMA0_D0_CURR_DESC_PTR
#define MDMA_D0_CURR_ADDR MDMA0_D0_CURR_ADDR
#define MDMA_D0_IRQ_STATUS MDMA0_D0_IRQ_STATUS
#define MDMA_D0_PERIPHERAL_MAP MDMA0_D0_PERIPHERAL_MAP
#define MDMA_D0_CURR_X_COUNT MDMA0_D0_CURR_X_COUNT
#define MDMA_D0_CURR_Y_COUNT MDMA0_D0_CURR_Y_COUNT

#define MDMA_S0_NEXT_DESC_PTR MDMA0_S0_NEXT_DESC_PTR
#define MDMA_S0_START_ADDR MDMA0_S0_START_ADDR
#define MDMA_S0_CONFIG MDMA0_S0_CONFIG
#define MDMA_S0_X_COUNT MDMA0_S0_X_COUNT
#define MDMA_S0_X_MODIFY MDMA0_S0_X_MODIFY
#define MDMA_S0_Y_COUNT MDMA0_S0_Y_COUNT
#define MDMA_S0_Y_MODIFY MDMA0_S0_Y_MODIFY
#define MDMA_S0_CURR_DESC_PTR MDMA0_S0_CURR_DESC_PTR
#define MDMA_S0_CURR_ADDR MDMA0_S0_CURR_ADDR
#define MDMA_S0_IRQ_STATUS MDMA0_S0_IRQ_STATUS
#define MDMA_S0_PERIPHERAL_MAP MDMA0_S0_PERIPHERAL_MAP
#define MDMA_S0_CURR_X_COUNT MDMA0_S0_CURR_X_COUNT
#define MDMA_S0_CURR_Y_COUNT MDMA0_S0_CURR_Y_COUNT

#define MDMA_D1_NEXT_DESC_PTR MDMA0_D1_NEXT_DESC_PTR
#define MDMA_D1_START_ADDR MDMA0_D1_START_ADDR
#define MDMA_D1_CONFIG MDMA0_D1_CONFIG
#define MDMA_D1_X_COUNT MDMA0_D1_X_COUNT
#define MDMA_D1_X_MODIFY MDMA0_D1_X_MODIFY
#define MDMA_D1_Y_COUNT MDMA0_D1_Y_COUNT
#define MDMA_D1_Y_MODIFY MDMA0_D1_Y_MODIFY
#define MDMA_D1_CURR_DESC_PTR MDMA0_D1_CURR_DESC_PTR
#define MDMA_D1_CURR_ADDR MDMA0_D1_CURR_ADDR
#define MDMA_D1_IRQ_STATUS MDMA0_D1_IRQ_STATUS
#define MDMA_D1_PERIPHERAL_MAP MDMA0_D1_PERIPHERAL_MAP
#define MDMA_D1_CURR_X_COUNT MDMA0_D1_CURR_X_COUNT
#define MDMA_D1_CURR_Y_COUNT MDMA0_D1_CURR_Y_COUNT

#define MDMA_S1_NEXT_DESC_PTR MDMA0_S1_NEXT_DESC_PTR
#define MDMA_S1_START_ADDR MDMA0_S1_START_ADDR
#define MDMA_S1_CONFIG MDMA0_S1_CONFIG
#define MDMA_S1_X_COUNT MDMA0_S1_X_COUNT
#define MDMA_S1_X_MODIFY MDMA0_S1_X_MODIFY
#define MDMA_S1_Y_COUNT MDMA0_S1_Y_COUNT
#define MDMA_S1_Y_MODIFY MDMA0_S1_Y_MODIFY
#define MDMA_S1_CURR_DESC_PTR MDMA0_S1_CURR_DESC_PTR
#define MDMA_S1_CURR_ADDR MDMA0_S1_CURR_ADDR
#define MDMA_S1_IRQ_STATUS MDMA0_S1_IRQ_STATUS
#define MDMA_S1_PERIPHERAL_MAP MDMA0_S1_PERIPHERAL_MAP
#define MDMA_S1_CURR_X_COUNT MDMA0_S1_CURR_X_COUNT
#define MDMA_S1_CURR_Y_COUNT MDMA0_S1_CURR_Y_COUNT


/* Parallel Peripheral Interface (PPI) (0xFFC01000 - 0xFFC010FF) */
#define	PPI_CONTROL			0xFFC01000	/* PPI Control Register */
#define	PPI_STATUS			0xFFC01004	/* PPI Status Register */
#define	PPI_COUNT			0xFFC01008	/* PPI Transfer	Count Register */
#define	PPI_DELAY			0xFFC0100C	/* PPI Delay Count Register */
#define	PPI_FRAME			0xFFC01010	/* PPI Frame Length Register */


/* Two-Wire Interface 0	(0xFFC01400 - 0xFFC014FF)			 */
#define	TWI0_CLKDIV			0xFFC01400	/* Serial Clock	Divider	Register */
#define	TWI0_CONTROL		0xFFC01404	/* TWI0	Master Internal	Time Reference Register */
#define	TWI0_SLAVE_CTL		0xFFC01408	/* Slave Mode Control Register */
#define	TWI0_SLAVE_STAT		0xFFC0140C	/* Slave Mode Status Register */
#define	TWI0_SLAVE_ADDR		0xFFC01410	/* Slave Mode Address Register */
#define	TWI0_MASTER_CTL	0xFFC01414	/* Master Mode Control Register */
#define	TWI0_MASTER_STAT	0xFFC01418	/* Master Mode Status Register */
#define	TWI0_MASTER_ADDR	0xFFC0141C	/* Master Mode Address Register */
#define	TWI0_INT_STAT		0xFFC01420	/* TWI0	Master Interrupt Register */
#define	TWI0_INT_MASK		0xFFC01424	/* TWI0	Master Interrupt Mask Register */
#define	TWI0_FIFO_CTL		0xFFC01428	/* FIFO	Control	Register */
#define	TWI0_FIFO_STAT		0xFFC0142C	/* FIFO	Status Register */
#define	TWI0_XMT_DATA8		0xFFC01480	/* FIFO	Transmit Data Single Byte Register */
#define	TWI0_XMT_DATA16		0xFFC01484	/* FIFO	Transmit Data Double Byte Register */
#define	TWI0_RCV_DATA8		0xFFC01488	/* FIFO	Receive	Data Single Byte Register */
#define	TWI0_RCV_DATA16		0xFFC0148C	/* FIFO	Receive	Data Double Byte Register */

#define TWI0_REGBASE		TWI0_CLKDIV

/* the following are for backwards compatibility */
#define	TWI0_PRESCALE	 TWI0_CONTROL
#define	TWI0_INT_SRC	 TWI0_INT_STAT
#define	TWI0_INT_ENABLE	 TWI0_INT_MASK


/* General-Purpose Ports  (0xFFC01500 -	0xFFC015FF)	 */

/* GPIO	Port C Register	Names */
#define PORTCIO_FER			0xFFC01500	/* GPIO	Pin Port C Configuration Register */
#define PORTCIO				0xFFC01510	/* GPIO	Pin Port C Data	Register */
#define PORTCIO_CLEAR			0xFFC01520	/* Clear GPIO Pin Port C Register */
#define PORTCIO_SET			0xFFC01530	/* Set GPIO Pin	Port C Register */
#define PORTCIO_TOGGLE			0xFFC01540	/* Toggle GPIO Pin Port	C Register */
#define PORTCIO_DIR			0xFFC01550	/* GPIO	Pin Port C Direction Register */
#define PORTCIO_INEN			0xFFC01560	/* GPIO	Pin Port C Input Enable	Register */

/* GPIO	Port D Register	Names */
#define PORTDIO_FER			0xFFC01504	/* GPIO	Pin Port D Configuration Register */
#define PORTDIO				0xFFC01514	/* GPIO	Pin Port D Data	Register */
#define PORTDIO_CLEAR			0xFFC01524	/* Clear GPIO Pin Port D Register */
#define PORTDIO_SET			0xFFC01534	/* Set GPIO Pin	Port D Register */
#define PORTDIO_TOGGLE			0xFFC01544	/* Toggle GPIO Pin Port	D Register */
#define PORTDIO_DIR			0xFFC01554	/* GPIO	Pin Port D Direction Register */
#define PORTDIO_INEN			0xFFC01564	/* GPIO	Pin Port D Input Enable	Register */

/* GPIO	Port E Register	Names */
#define PORTEIO_FER			0xFFC01508	/* GPIO	Pin Port E Configuration Register */
#define PORTEIO				0xFFC01518	/* GPIO	Pin Port E Data	Register */
#define PORTEIO_CLEAR			0xFFC01528	/* Clear GPIO Pin Port E Register */
#define PORTEIO_SET			0xFFC01538	/* Set GPIO Pin	Port E Register */
#define PORTEIO_TOGGLE			0xFFC01548	/* Toggle GPIO Pin Port	E Register */
#define PORTEIO_DIR			0xFFC01558	/* GPIO	Pin Port E Direction Register */
#define PORTEIO_INEN			0xFFC01568	/* GPIO	Pin Port E Input Enable	Register */

/* DMA Controller 1 Traffic Control Registers (0xFFC01B00 - 0xFFC01BFF) */

#define	DMAC1_TC_PER			0xFFC01B0C	/* DMA Controller 1 Traffic Control Periods Register */
#define	DMAC1_TC_CNT			0xFFC01B10	/* DMA Controller 1 Traffic Control Current Counts Register */

/* Alternate deprecated	register names (below) provided	for backwards code compatibility */
#define	DMA1_TCPER			DMAC1_TC_PER
#define	DMA1_TCCNT			DMAC1_TC_CNT


/* DMA Controller 1 (0xFFC01C00	- 0xFFC01FFF)							 */
#define	DMA8_NEXT_DESC_PTR		0xFFC01C00	/* DMA Channel 8 Next Descriptor Pointer Register */
#define	DMA8_START_ADDR			0xFFC01C04	/* DMA Channel 8 Start Address Register */
#define	DMA8_CONFIG				0xFFC01C08	/* DMA Channel 8 Configuration Register */
#define	DMA8_X_COUNT			0xFFC01C10	/* DMA Channel 8 X Count Register */
#define	DMA8_X_MODIFY			0xFFC01C14	/* DMA Channel 8 X Modify Register */
#define	DMA8_Y_COUNT			0xFFC01C18	/* DMA Channel 8 Y Count Register */
#define	DMA8_Y_MODIFY			0xFFC01C1C	/* DMA Channel 8 Y Modify Register */
#define	DMA8_CURR_DESC_PTR		0xFFC01C20	/* DMA Channel 8 Current Descriptor Pointer Register */
#define	DMA8_CURR_ADDR			0xFFC01C24	/* DMA Channel 8 Current Address Register */
#define	DMA8_IRQ_STATUS			0xFFC01C28	/* DMA Channel 8 Interrupt/Status Register */
#define	DMA8_PERIPHERAL_MAP		0xFFC01C2C	/* DMA Channel 8 Peripheral Map	Register */
#define	DMA8_CURR_X_COUNT		0xFFC01C30	/* DMA Channel 8 Current X Count Register */
#define	DMA8_CURR_Y_COUNT		0xFFC01C38	/* DMA Channel 8 Current Y Count Register */

#define	DMA9_NEXT_DESC_PTR		0xFFC01C40	/* DMA Channel 9 Next Descriptor Pointer Register */
#define	DMA9_START_ADDR			0xFFC01C44	/* DMA Channel 9 Start Address Register */
#define	DMA9_CONFIG				0xFFC01C48	/* DMA Channel 9 Configuration Register */
#define	DMA9_X_COUNT			0xFFC01C50	/* DMA Channel 9 X Count Register */
#define	DMA9_X_MODIFY			0xFFC01C54	/* DMA Channel 9 X Modify Register */
#define	DMA9_Y_COUNT			0xFFC01C58	/* DMA Channel 9 Y Count Register */
#define	DMA9_Y_MODIFY			0xFFC01C5C	/* DMA Channel 9 Y Modify Register */
#define	DMA9_CURR_DESC_PTR		0xFFC01C60	/* DMA Channel 9 Current Descriptor Pointer Register */
#define	DMA9_CURR_ADDR			0xFFC01C64	/* DMA Channel 9 Current Address Register */
#define	DMA9_IRQ_STATUS			0xFFC01C68	/* DMA Channel 9 Interrupt/Status Register */
#define	DMA9_PERIPHERAL_MAP		0xFFC01C6C	/* DMA Channel 9 Peripheral Map	Register */
#define	DMA9_CURR_X_COUNT		0xFFC01C70	/* DMA Channel 9 Current X Count Register */
#define	DMA9_CURR_Y_COUNT		0xFFC01C78	/* DMA Channel 9 Current Y Count Register */

#define	DMA10_NEXT_DESC_PTR		0xFFC01C80	/* DMA Channel 10 Next Descriptor Pointer Register */
#define	DMA10_START_ADDR		0xFFC01C84	/* DMA Channel 10 Start	Address	Register */
#define	DMA10_CONFIG			0xFFC01C88	/* DMA Channel 10 Configuration	Register */
#define	DMA10_X_COUNT			0xFFC01C90	/* DMA Channel 10 X Count Register */
#define	DMA10_X_MODIFY			0xFFC01C94	/* DMA Channel 10 X Modify Register */
#define	DMA10_Y_COUNT			0xFFC01C98	/* DMA Channel 10 Y Count Register */
#define	DMA10_Y_MODIFY			0xFFC01C9C	/* DMA Channel 10 Y Modify Register */
#define	DMA10_CURR_DESC_PTR		0xFFC01CA0	/* DMA Channel 10 Current Descriptor Pointer Register */
#define	DMA10_CURR_ADDR			0xFFC01CA4	/* DMA Channel 10 Current Address Register */
#define	DMA10_IRQ_STATUS		0xFFC01CA8	/* DMA Channel 10 Interrupt/Status Register */
#define	DMA10_PERIPHERAL_MAP	0xFFC01CAC	/* DMA Channel 10 Peripheral Map Register */
#define	DMA10_CURR_X_COUNT		0xFFC01CB0	/* DMA Channel 10 Current X Count Register */
#define	DMA10_CURR_Y_COUNT		0xFFC01CB8	/* DMA Channel 10 Current Y Count Register */

#define	DMA11_NEXT_DESC_PTR		0xFFC01CC0	/* DMA Channel 11 Next Descriptor Pointer Register */
#define	DMA11_START_ADDR		0xFFC01CC4	/* DMA Channel 11 Start	Address	Register */
#define	DMA11_CONFIG			0xFFC01CC8	/* DMA Channel 11 Configuration	Register */
#define	DMA11_X_COUNT			0xFFC01CD0	/* DMA Channel 11 X Count Register */
#define	DMA11_X_MODIFY			0xFFC01CD4	/* DMA Channel 11 X Modify Register */
#define	DMA11_Y_COUNT			0xFFC01CD8	/* DMA Channel 11 Y Count Register */
#define	DMA11_Y_MODIFY			0xFFC01CDC	/* DMA Channel 11 Y Modify Register */
#define	DMA11_CURR_DESC_PTR		0xFFC01CE0	/* DMA Channel 11 Current Descriptor Pointer Register */
#define	DMA11_CURR_ADDR			0xFFC01CE4	/* DMA Channel 11 Current Address Register */
#define	DMA11_IRQ_STATUS		0xFFC01CE8	/* DMA Channel 11 Interrupt/Status Register */
#define	DMA11_PERIPHERAL_MAP	0xFFC01CEC	/* DMA Channel 11 Peripheral Map Register */
#define	DMA11_CURR_X_COUNT		0xFFC01CF0	/* DMA Channel 11 Current X Count Register */
#define	DMA11_CURR_Y_COUNT		0xFFC01CF8	/* DMA Channel 11 Current Y Count Register */

#define	DMA12_NEXT_DESC_PTR		0xFFC01D00	/* DMA Channel 12 Next Descriptor Pointer Register */
#define	DMA12_START_ADDR		0xFFC01D04	/* DMA Channel 12 Start	Address	Register */
#define	DMA12_CONFIG			0xFFC01D08	/* DMA Channel 12 Configuration	Register */
#define	DMA12_X_COUNT			0xFFC01D10	/* DMA Channel 12 X Count Register */
#define	DMA12_X_MODIFY			0xFFC01D14	/* DMA Channel 12 X Modify Register */
#define	DMA12_Y_COUNT			0xFFC01D18	/* DMA Channel 12 Y Count Register */
#define	DMA12_Y_MODIFY			0xFFC01D1C	/* DMA Channel 12 Y Modify Register */
#define	DMA12_CURR_DESC_PTR		0xFFC01D20	/* DMA Channel 12 Current Descriptor Pointer Register */
#define	DMA12_CURR_ADDR			0xFFC01D24	/* DMA Channel 12 Current Address Register */
#define	DMA12_IRQ_STATUS		0xFFC01D28	/* DMA Channel 12 Interrupt/Status Register */
#define	DMA12_PERIPHERAL_MAP	0xFFC01D2C	/* DMA Channel 12 Peripheral Map Register */
#define	DMA12_CURR_X_COUNT		0xFFC01D30	/* DMA Channel 12 Current X Count Register */
#define	DMA12_CURR_Y_COUNT		0xFFC01D38	/* DMA Channel 12 Current Y Count Register */

#define	DMA13_NEXT_DESC_PTR		0xFFC01D40	/* DMA Channel 13 Next Descriptor Pointer Register */
#define	DMA13_START_ADDR		0xFFC01D44	/* DMA Channel 13 Start	Address	Register */
#define	DMA13_CONFIG			0xFFC01D48	/* DMA Channel 13 Configuration	Register */
#define	DMA13_X_COUNT			0xFFC01D50	/* DMA Channel 13 X Count Register */
#define	DMA13_X_MODIFY			0xFFC01D54	/* DMA Channel 13 X Modify Register */
#define	DMA13_Y_COUNT			0xFFC01D58	/* DMA Channel 13 Y Count Register */
#define	DMA13_Y_MODIFY			0xFFC01D5C	/* DMA Channel 13 Y Modify Register */
#define	DMA13_CURR_DESC_PTR		0xFFC01D60	/* DMA Channel 13 Current Descriptor Pointer Register */
#define	DMA13_CURR_ADDR			0xFFC01D64	/* DMA Channel 13 Current Address Register */
#define	DMA13_IRQ_STATUS		0xFFC01D68	/* DMA Channel 13 Interrupt/Status Register */
#define	DMA13_PERIPHERAL_MAP	0xFFC01D6C	/* DMA Channel 13 Peripheral Map Register */
#define	DMA13_CURR_X_COUNT		0xFFC01D70	/* DMA Channel 13 Current X Count Register */
#define	DMA13_CURR_Y_COUNT		0xFFC01D78	/* DMA Channel 13 Current Y Count Register */

#define	DMA14_NEXT_DESC_PTR		0xFFC01D80	/* DMA Channel 14 Next Descriptor Pointer Register */
#define	DMA14_START_ADDR		0xFFC01D84	/* DMA Channel 14 Start	Address	Register */
#define	DMA14_CONFIG			0xFFC01D88	/* DMA Channel 14 Configuration	Register */
#define	DMA14_X_COUNT			0xFFC01D90	/* DMA Channel 14 X Count Register */
#define	DMA14_X_MODIFY			0xFFC01D94	/* DMA Channel 14 X Modify Register */
#define	DMA14_Y_COUNT			0xFFC01D98	/* DMA Channel 14 Y Count Register */
#define	DMA14_Y_MODIFY			0xFFC01D9C	/* DMA Channel 14 Y Modify Register */
#define	DMA14_CURR_DESC_PTR		0xFFC01DA0	/* DMA Channel 14 Current Descriptor Pointer Register */
#define	DMA14_CURR_ADDR			0xFFC01DA4	/* DMA Channel 14 Current Address Register */
#define	DMA14_IRQ_STATUS		0xFFC01DA8	/* DMA Channel 14 Interrupt/Status Register */
#define	DMA14_PERIPHERAL_MAP	0xFFC01DAC	/* DMA Channel 14 Peripheral Map Register */
#define	DMA14_CURR_X_COUNT		0xFFC01DB0	/* DMA Channel 14 Current X Count Register */
#define	DMA14_CURR_Y_COUNT		0xFFC01DB8	/* DMA Channel 14 Current Y Count Register */

#define	DMA15_NEXT_DESC_PTR		0xFFC01DC0	/* DMA Channel 15 Next Descriptor Pointer Register */
#define	DMA15_START_ADDR		0xFFC01DC4	/* DMA Channel 15 Start	Address	Register */
#define	DMA15_CONFIG			0xFFC01DC8	/* DMA Channel 15 Configuration	Register */
#define	DMA15_X_COUNT			0xFFC01DD0	/* DMA Channel 15 X Count Register */
#define	DMA15_X_MODIFY			0xFFC01DD4	/* DMA Channel 15 X Modify Register */
#define	DMA15_Y_COUNT			0xFFC01DD8	/* DMA Channel 15 Y Count Register */
#define	DMA15_Y_MODIFY			0xFFC01DDC	/* DMA Channel 15 Y Modify Register */
#define	DMA15_CURR_DESC_PTR		0xFFC01DE0	/* DMA Channel 15 Current Descriptor Pointer Register */
#define	DMA15_CURR_ADDR			0xFFC01DE4	/* DMA Channel 15 Current Address Register */
#define	DMA15_IRQ_STATUS		0xFFC01DE8	/* DMA Channel 15 Interrupt/Status Register */
#define	DMA15_PERIPHERAL_MAP	0xFFC01DEC	/* DMA Channel 15 Peripheral Map Register */
#define	DMA15_CURR_X_COUNT		0xFFC01DF0	/* DMA Channel 15 Current X Count Register */
#define	DMA15_CURR_Y_COUNT		0xFFC01DF8	/* DMA Channel 15 Current Y Count Register */

#define	DMA16_NEXT_DESC_PTR		0xFFC01E00	/* DMA Channel 16 Next Descriptor Pointer Register */
#define	DMA16_START_ADDR		0xFFC01E04	/* DMA Channel 16 Start	Address	Register */
#define	DMA16_CONFIG			0xFFC01E08	/* DMA Channel 16 Configuration	Register */
#define	DMA16_X_COUNT			0xFFC01E10	/* DMA Channel 16 X Count Register */
#define	DMA16_X_MODIFY			0xFFC01E14	/* DMA Channel 16 X Modify Register */
#define	DMA16_Y_COUNT			0xFFC01E18	/* DMA Channel 16 Y Count Register */
#define	DMA16_Y_MODIFY			0xFFC01E1C	/* DMA Channel 16 Y Modify Register */
#define	DMA16_CURR_DESC_PTR		0xFFC01E20	/* DMA Channel 16 Current Descriptor Pointer Register */
#define	DMA16_CURR_ADDR			0xFFC01E24	/* DMA Channel 16 Current Address Register */
#define	DMA16_IRQ_STATUS		0xFFC01E28	/* DMA Channel 16 Interrupt/Status Register */
#define	DMA16_PERIPHERAL_MAP	0xFFC01E2C	/* DMA Channel 16 Peripheral Map Register */
#define	DMA16_CURR_X_COUNT		0xFFC01E30	/* DMA Channel 16 Current X Count Register */
#define	DMA16_CURR_Y_COUNT		0xFFC01E38	/* DMA Channel 16 Current Y Count Register */

#define	DMA17_NEXT_DESC_PTR		0xFFC01E40	/* DMA Channel 17 Next Descriptor Pointer Register */
#define	DMA17_START_ADDR		0xFFC01E44	/* DMA Channel 17 Start	Address	Register */
#define	DMA17_CONFIG			0xFFC01E48	/* DMA Channel 17 Configuration	Register */
#define	DMA17_X_COUNT			0xFFC01E50	/* DMA Channel 17 X Count Register */
#define	DMA17_X_MODIFY			0xFFC01E54	/* DMA Channel 17 X Modify Register */
#define	DMA17_Y_COUNT			0xFFC01E58	/* DMA Channel 17 Y Count Register */
#define	DMA17_Y_MODIFY			0xFFC01E5C	/* DMA Channel 17 Y Modify Register */
#define	DMA17_CURR_DESC_PTR		0xFFC01E60	/* DMA Channel 17 Current Descriptor Pointer Register */
#define	DMA17_CURR_ADDR			0xFFC01E64	/* DMA Channel 17 Current Address Register */
#define	DMA17_IRQ_STATUS		0xFFC01E68	/* DMA Channel 17 Interrupt/Status Register */
#define	DMA17_PERIPHERAL_MAP	0xFFC01E6C	/* DMA Channel 17 Peripheral Map Register */
#define	DMA17_CURR_X_COUNT		0xFFC01E70	/* DMA Channel 17 Current X Count Register */
#define	DMA17_CURR_Y_COUNT		0xFFC01E78	/* DMA Channel 17 Current Y Count Register */

#define	DMA18_NEXT_DESC_PTR		0xFFC01E80	/* DMA Channel 18 Next Descriptor Pointer Register */
#define	DMA18_START_ADDR		0xFFC01E84	/* DMA Channel 18 Start	Address	Register */
#define	DMA18_CONFIG			0xFFC01E88	/* DMA Channel 18 Configuration	Register */
#define	DMA18_X_COUNT			0xFFC01E90	/* DMA Channel 18 X Count Register */
#define	DMA18_X_MODIFY			0xFFC01E94	/* DMA Channel 18 X Modify Register */
#define	DMA18_Y_COUNT			0xFFC01E98	/* DMA Channel 18 Y Count Register */
#define	DMA18_Y_MODIFY			0xFFC01E9C	/* DMA Channel 18 Y Modify Register */
#define	DMA18_CURR_DESC_PTR		0xFFC01EA0	/* DMA Channel 18 Current Descriptor Pointer Register */
#define	DMA18_CURR_ADDR			0xFFC01EA4	/* DMA Channel 18 Current Address Register */
#define	DMA18_IRQ_STATUS		0xFFC01EA8	/* DMA Channel 18 Interrupt/Status Register */
#define	DMA18_PERIPHERAL_MAP	0xFFC01EAC	/* DMA Channel 18 Peripheral Map Register */
#define	DMA18_CURR_X_COUNT		0xFFC01EB0	/* DMA Channel 18 Current X Count Register */
#define	DMA18_CURR_Y_COUNT		0xFFC01EB8	/* DMA Channel 18 Current Y Count Register */

#define	DMA19_NEXT_DESC_PTR		0xFFC01EC0	/* DMA Channel 19 Next Descriptor Pointer Register */
#define	DMA19_START_ADDR		0xFFC01EC4	/* DMA Channel 19 Start	Address	Register */
#define	DMA19_CONFIG			0xFFC01EC8	/* DMA Channel 19 Configuration	Register */
#define	DMA19_X_COUNT			0xFFC01ED0	/* DMA Channel 19 X Count Register */
#define	DMA19_X_MODIFY			0xFFC01ED4	/* DMA Channel 19 X Modify Register */
#define	DMA19_Y_COUNT			0xFFC01ED8	/* DMA Channel 19 Y Count Register */
#define	DMA19_Y_MODIFY			0xFFC01EDC	/* DMA Channel 19 Y Modify Register */
#define	DMA19_CURR_DESC_PTR		0xFFC01EE0	/* DMA Channel 19 Current Descriptor Pointer Register */
#define	DMA19_CURR_ADDR			0xFFC01EE4	/* DMA Channel 19 Current Address Register */
#define	DMA19_IRQ_STATUS		0xFFC01EE8	/* DMA Channel 19 Interrupt/Status Register */
#define	DMA19_PERIPHERAL_MAP	0xFFC01EEC	/* DMA Channel 19 Peripheral Map Register */
#define	DMA19_CURR_X_COUNT		0xFFC01EF0	/* DMA Channel 19 Current X Count Register */
#define	DMA19_CURR_Y_COUNT		0xFFC01EF8	/* DMA Channel 19 Current Y Count Register */

#define	MDMA1_D0_NEXT_DESC_PTR	0xFFC01F00	/* MemDMA1 Stream 0 Destination	Next Descriptor	Pointer	Register */
#define	MDMA1_D0_START_ADDR		0xFFC01F04	/* MemDMA1 Stream 0 Destination	Start Address Register */
#define	MDMA1_D0_CONFIG			0xFFC01F08	/* MemDMA1 Stream 0 Destination	Configuration Register */
#define	MDMA1_D0_X_COUNT		0xFFC01F10	/* MemDMA1 Stream 0 Destination	X Count	Register */
#define	MDMA1_D0_X_MODIFY		0xFFC01F14	/* MemDMA1 Stream 0 Destination	X Modify Register */
#define	MDMA1_D0_Y_COUNT		0xFFC01F18	/* MemDMA1 Stream 0 Destination	Y Count	Register */
#define	MDMA1_D0_Y_MODIFY		0xFFC01F1C	/* MemDMA1 Stream 0 Destination	Y Modify Register */
#define	MDMA1_D0_CURR_DESC_PTR	0xFFC01F20	/* MemDMA1 Stream 0 Destination	Current	Descriptor Pointer Register */
#define	MDMA1_D0_CURR_ADDR		0xFFC01F24	/* MemDMA1 Stream 0 Destination	Current	Address	Register */
#define	MDMA1_D0_IRQ_STATUS		0xFFC01F28	/* MemDMA1 Stream 0 Destination	Interrupt/Status Register */
#define	MDMA1_D0_PERIPHERAL_MAP	0xFFC01F2C	/* MemDMA1 Stream 0 Destination	Peripheral Map Register */
#define	MDMA1_D0_CURR_X_COUNT	0xFFC01F30	/* MemDMA1 Stream 0 Destination	Current	X Count	Register */
#define	MDMA1_D0_CURR_Y_COUNT	0xFFC01F38	/* MemDMA1 Stream 0 Destination	Current	Y Count	Register */

#define	MDMA1_S0_NEXT_DESC_PTR	0xFFC01F40	/* MemDMA1 Stream 0 Source Next	Descriptor Pointer Register */
#define	MDMA1_S0_START_ADDR		0xFFC01F44	/* MemDMA1 Stream 0 Source Start Address Register */
#define	MDMA1_S0_CONFIG			0xFFC01F48	/* MemDMA1 Stream 0 Source Configuration Register */
#define	MDMA1_S0_X_COUNT		0xFFC01F50	/* MemDMA1 Stream 0 Source X Count Register */
#define	MDMA1_S0_X_MODIFY		0xFFC01F54	/* MemDMA1 Stream 0 Source X Modify Register */
#define	MDMA1_S0_Y_COUNT		0xFFC01F58	/* MemDMA1 Stream 0 Source Y Count Register */
#define	MDMA1_S0_Y_MODIFY		0xFFC01F5C	/* MemDMA1 Stream 0 Source Y Modify Register */
#define	MDMA1_S0_CURR_DESC_PTR	0xFFC01F60	/* MemDMA1 Stream 0 Source Current Descriptor Pointer Register */
#define	MDMA1_S0_CURR_ADDR		0xFFC01F64	/* MemDMA1 Stream 0 Source Current Address Register */
#define	MDMA1_S0_IRQ_STATUS		0xFFC01F68	/* MemDMA1 Stream 0 Source Interrupt/Status Register */
#define	MDMA1_S0_PERIPHERAL_MAP	0xFFC01F6C	/* MemDMA1 Stream 0 Source Peripheral Map Register */
#define	MDMA1_S0_CURR_X_COUNT	0xFFC01F70	/* MemDMA1 Stream 0 Source Current X Count Register */
#define	MDMA1_S0_CURR_Y_COUNT	0xFFC01F78	/* MemDMA1 Stream 0 Source Current Y Count Register */

#define	MDMA1_D1_NEXT_DESC_PTR	0xFFC01F80	/* MemDMA1 Stream 1 Destination	Next Descriptor	Pointer	Register */
#define	MDMA1_D1_START_ADDR		0xFFC01F84	/* MemDMA1 Stream 1 Destination	Start Address Register */
#define	MDMA1_D1_CONFIG			0xFFC01F88	/* MemDMA1 Stream 1 Destination	Configuration Register */
#define	MDMA1_D1_X_COUNT		0xFFC01F90	/* MemDMA1 Stream 1 Destination	X Count	Register */
#define	MDMA1_D1_X_MODIFY		0xFFC01F94	/* MemDMA1 Stream 1 Destination	X Modify Register */
#define	MDMA1_D1_Y_COUNT		0xFFC01F98	/* MemDMA1 Stream 1 Destination	Y Count	Register */
#define	MDMA1_D1_Y_MODIFY		0xFFC01F9C	/* MemDMA1 Stream 1 Destination	Y Modify Register */
#define	MDMA1_D1_CURR_DESC_PTR	0xFFC01FA0	/* MemDMA1 Stream 1 Destination	Current	Descriptor Pointer Register */
#define	MDMA1_D1_CURR_ADDR		0xFFC01FA4	/* MemDMA1 Stream 1 Destination	Current	Address	Register */
#define	MDMA1_D1_IRQ_STATUS		0xFFC01FA8	/* MemDMA1 Stream 1 Destination	Interrupt/Status Register */
#define	MDMA1_D1_PERIPHERAL_MAP	0xFFC01FAC	/* MemDMA1 Stream 1 Destination	Peripheral Map Register */
#define	MDMA1_D1_CURR_X_COUNT	0xFFC01FB0	/* MemDMA1 Stream 1 Destination	Current	X Count	Register */
#define	MDMA1_D1_CURR_Y_COUNT	0xFFC01FB8	/* MemDMA1 Stream 1 Destination	Current	Y Count	Register */

#define	MDMA1_S1_NEXT_DESC_PTR	0xFFC01FC0	/* MemDMA1 Stream 1 Source Next	Descriptor Pointer Register */
#define	MDMA1_S1_START_ADDR		0xFFC01FC4	/* MemDMA1 Stream 1 Source Start Address Register */
#define	MDMA1_S1_CONFIG			0xFFC01FC8	/* MemDMA1 Stream 1 Source Configuration Register */
#define	MDMA1_S1_X_COUNT		0xFFC01FD0	/* MemDMA1 Stream 1 Source X Count Register */
#define	MDMA1_S1_X_MODIFY		0xFFC01FD4	/* MemDMA1 Stream 1 Source X Modify Register */
#define	MDMA1_S1_Y_COUNT		0xFFC01FD8	/* MemDMA1 Stream 1 Source Y Count Register */
#define	MDMA1_S1_Y_MODIFY		0xFFC01FDC	/* MemDMA1 Stream 1 Source Y Modify Register */
#define	MDMA1_S1_CURR_DESC_PTR	0xFFC01FE0	/* MemDMA1 Stream 1 Source Current Descriptor Pointer Register */
#define	MDMA1_S1_CURR_ADDR		0xFFC01FE4	/* MemDMA1 Stream 1 Source Current Address Register */
#define	MDMA1_S1_IRQ_STATUS		0xFFC01FE8	/* MemDMA1 Stream 1 Source Interrupt/Status Register */
#define	MDMA1_S1_PERIPHERAL_MAP	0xFFC01FEC	/* MemDMA1 Stream 1 Source Peripheral Map Register */
#define	MDMA1_S1_CURR_X_COUNT	0xFFC01FF0	/* MemDMA1 Stream 1 Source Current X Count Register */
#define	MDMA1_S1_CURR_Y_COUNT	0xFFC01FF8	/* MemDMA1 Stream 1 Source Current Y Count Register */


/* UART1 Controller		(0xFFC02000 - 0xFFC020FF)	 */
#define	UART1_THR			0xFFC02000	/* Transmit Holding register */
#define	UART1_RBR			0xFFC02000	/* Receive Buffer register */
#define	UART1_DLL			0xFFC02000	/* Divisor Latch (Low-Byte) */
#define	UART1_IER			0xFFC02004	/* Interrupt Enable Register */
#define	UART1_DLH			0xFFC02004	/* Divisor Latch (High-Byte) */
#define	UART1_IIR			0xFFC02008	/* Interrupt Identification Register */
#define	UART1_LCR			0xFFC0200C	/* Line	Control	Register */
#define	UART1_MCR			0xFFC02010	/* Modem Control Register */
#define	UART1_LSR			0xFFC02014	/* Line	Status Register */
#define	UART1_SCR			0xFFC0201C	/* SCR Scratch Register */
#define	UART1_GCTL			0xFFC02024	/* Global Control Register */


/* UART2 Controller		(0xFFC02100 - 0xFFC021FF)	 */
#define	UART2_THR			0xFFC02100	/* Transmit Holding register */
#define	UART2_RBR			0xFFC02100	/* Receive Buffer register */
#define	UART2_DLL			0xFFC02100	/* Divisor Latch (Low-Byte) */
#define	UART2_IER			0xFFC02104	/* Interrupt Enable Register */
#define	UART2_DLH			0xFFC02104	/* Divisor Latch (High-Byte) */
#define	UART2_IIR			0xFFC02108	/* Interrupt Identification Register */
#define	UART2_LCR			0xFFC0210C	/* Line	Control	Register */
#define	UART2_MCR			0xFFC02110	/* Modem Control Register */
#define	UART2_LSR			0xFFC02114	/* Line	Status Register */
#define	UART2_SCR			0xFFC0211C	/* SCR Scratch Register */
#define	UART2_GCTL			0xFFC02124	/* Global Control Register */


/* Two-Wire Interface 1	(0xFFC02200 - 0xFFC022FF)			 */
#define	TWI1_CLKDIV			0xFFC02200	/* Serial Clock	Divider	Register */
#define	TWI1_CONTROL		0xFFC02204	/* TWI1	Master Internal	Time Reference Register */
#define	TWI1_SLAVE_CTL		0xFFC02208	/* Slave Mode Control Register */
#define	TWI1_SLAVE_STAT		0xFFC0220C	/* Slave Mode Status Register */
#define	TWI1_SLAVE_ADDR		0xFFC02210	/* Slave Mode Address Register */
#define	TWI1_MASTER_CTL	0xFFC02214	/* Master Mode Control Register */
#define	TWI1_MASTER_STAT	0xFFC02218	/* Master Mode Status Register */
#define	TWI1_MASTER_ADDR	0xFFC0221C	/* Master Mode Address Register */
#define	TWI1_INT_STAT		0xFFC02220	/* TWI1	Master Interrupt Register */
#define	TWI1_INT_MASK		0xFFC02224	/* TWI1	Master Interrupt Mask Register */
#define	TWI1_FIFO_CTL		0xFFC02228	/* FIFO	Control	Register */
#define	TWI1_FIFO_STAT		0xFFC0222C	/* FIFO	Status Register */
#define	TWI1_XMT_DATA8		0xFFC02280	/* FIFO	Transmit Data Single Byte Register */
#define	TWI1_XMT_DATA16		0xFFC02284	/* FIFO	Transmit Data Double Byte Register */
#define	TWI1_RCV_DATA8		0xFFC02288	/* FIFO	Receive	Data Single Byte Register */
#define	TWI1_RCV_DATA16		0xFFC0228C	/* FIFO	Receive	Data Double Byte Register */
#define TWI1_REGBASE		TWI1_CLKDIV


/* the following are for backwards compatibility */
#define	TWI1_PRESCALE	  TWI1_CONTROL
#define	TWI1_INT_SRC	  TWI1_INT_STAT
#define	TWI1_INT_ENABLE	  TWI1_INT_MASK


/* SPI1	Controller		(0xFFC02300 - 0xFFC023FF)	 */
#define	SPI1_CTL			0xFFC02300  /* SPI1 Control Register */
#define	SPI1_FLG			0xFFC02304  /* SPI1 Flag register */
#define	SPI1_STAT			0xFFC02308  /* SPI1 Status register */
#define	SPI1_TDBR			0xFFC0230C  /* SPI1 Transmit Data Buffer Register */
#define	SPI1_RDBR			0xFFC02310  /* SPI1 Receive Data Buffer	Register */
#define	SPI1_BAUD			0xFFC02314  /* SPI1 Baud rate Register */
#define	SPI1_SHADOW			0xFFC02318  /* SPI1_RDBR Shadow	Register */
#define SPI1_REGBASE			SPI1_CTL

/* SPI2	Controller		(0xFFC02400 - 0xFFC024FF)	 */
#define	SPI2_CTL			0xFFC02400  /* SPI2 Control Register */
#define	SPI2_FLG			0xFFC02404  /* SPI2 Flag register */
#define	SPI2_STAT			0xFFC02408  /* SPI2 Status register */
#define	SPI2_TDBR			0xFFC0240C  /* SPI2 Transmit Data Buffer Register */
#define	SPI2_RDBR			0xFFC02410  /* SPI2 Receive Data Buffer	Register */
#define	SPI2_BAUD			0xFFC02414  /* SPI2 Baud rate Register */
#define	SPI2_SHADOW			0xFFC02418  /* SPI2_RDBR Shadow	Register */
#define SPI2_REGBASE			SPI2_CTL

/* SPORT2 Controller		(0xFFC02500 - 0xFFC025FF)			 */
#define	SPORT2_TCR1			0xFFC02500	/* SPORT2 Transmit Configuration 1 Register */
#define	SPORT2_TCR2			0xFFC02504	/* SPORT2 Transmit Configuration 2 Register */
#define	SPORT2_TCLKDIV		0xFFC02508	/* SPORT2 Transmit Clock Divider */
#define	SPORT2_TFSDIV		0xFFC0250C	/* SPORT2 Transmit Frame Sync Divider */
#define	SPORT2_TX			0xFFC02510	/* SPORT2 TX Data Register */
#define	SPORT2_RX			0xFFC02518	/* SPORT2 RX Data Register */
#define	SPORT2_RCR1			0xFFC02520	/* SPORT2 Transmit Configuration 1 Register */
#define	SPORT2_RCR2			0xFFC02524	/* SPORT2 Transmit Configuration 2 Register */
#define	SPORT2_RCLKDIV		0xFFC02528	/* SPORT2 Receive Clock	Divider */
#define	SPORT2_RFSDIV		0xFFC0252C	/* SPORT2 Receive Frame	Sync Divider */
#define	SPORT2_STAT			0xFFC02530	/* SPORT2 Status Register */
#define	SPORT2_CHNL			0xFFC02534	/* SPORT2 Current Channel Register */
#define	SPORT2_MCMC1		0xFFC02538	/* SPORT2 Multi-Channel	Configuration Register 1 */
#define	SPORT2_MCMC2		0xFFC0253C	/* SPORT2 Multi-Channel	Configuration Register 2 */
#define	SPORT2_MTCS0		0xFFC02540	/* SPORT2 Multi-Channel	Transmit Select	Register 0 */
#define	SPORT2_MTCS1		0xFFC02544	/* SPORT2 Multi-Channel	Transmit Select	Register 1 */
#define	SPORT2_MTCS2		0xFFC02548	/* SPORT2 Multi-Channel	Transmit Select	Register 2 */
#define	SPORT2_MTCS3		0xFFC0254C	/* SPORT2 Multi-Channel	Transmit Select	Register 3 */
#define	SPORT2_MRCS0		0xFFC02550	/* SPORT2 Multi-Channel	Receive	Select Register	0 */
#define	SPORT2_MRCS1		0xFFC02554	/* SPORT2 Multi-Channel	Receive	Select Register	1 */
#define	SPORT2_MRCS2		0xFFC02558	/* SPORT2 Multi-Channel	Receive	Select Register	2 */
#define	SPORT2_MRCS3		0xFFC0255C	/* SPORT2 Multi-Channel	Receive	Select Register	3 */


/* SPORT3 Controller		(0xFFC02600 - 0xFFC026FF)			 */
#define	SPORT3_TCR1			0xFFC02600	/* SPORT3 Transmit Configuration 1 Register */
#define	SPORT3_TCR2			0xFFC02604	/* SPORT3 Transmit Configuration 2 Register */
#define	SPORT3_TCLKDIV		0xFFC02608	/* SPORT3 Transmit Clock Divider */
#define	SPORT3_TFSDIV		0xFFC0260C	/* SPORT3 Transmit Frame Sync Divider */
#define	SPORT3_TX			0xFFC02610	/* SPORT3 TX Data Register */
#define	SPORT3_RX			0xFFC02618	/* SPORT3 RX Data Register */
#define	SPORT3_RCR1			0xFFC02620	/* SPORT3 Transmit Configuration 1 Register */
#define	SPORT3_RCR2			0xFFC02624	/* SPORT3 Transmit Configuration 2 Register */
#define	SPORT3_RCLKDIV		0xFFC02628	/* SPORT3 Receive Clock	Divider */
#define	SPORT3_RFSDIV		0xFFC0262C	/* SPORT3 Receive Frame	Sync Divider */
#define	SPORT3_STAT			0xFFC02630	/* SPORT3 Status Register */
#define	SPORT3_CHNL			0xFFC02634	/* SPORT3 Current Channel Register */
#define	SPORT3_MCMC1		0xFFC02638	/* SPORT3 Multi-Channel	Configuration Register 1 */
#define	SPORT3_MCMC2		0xFFC0263C	/* SPORT3 Multi-Channel	Configuration Register 2 */
#define	SPORT3_MTCS0		0xFFC02640	/* SPORT3 Multi-Channel	Transmit Select	Register 0 */
#define	SPORT3_MTCS1		0xFFC02644	/* SPORT3 Multi-Channel	Transmit Select	Register 1 */
#define	SPORT3_MTCS2		0xFFC02648	/* SPORT3 Multi-Channel	Transmit Select	Register 2 */
#define	SPORT3_MTCS3		0xFFC0264C	/* SPORT3 Multi-Channel	Transmit Select	Register 3 */
#define	SPORT3_MRCS0		0xFFC02650	/* SPORT3 Multi-Channel	Receive	Select Register	0 */
#define	SPORT3_MRCS1		0xFFC02654	/* SPORT3 Multi-Channel	Receive	Select Register	1 */
#define	SPORT3_MRCS2		0xFFC02658	/* SPORT3 Multi-Channel	Receive	Select Register	2 */
#define	SPORT3_MRCS3		0xFFC0265C	/* SPORT3 Multi-Channel	Receive	Select Register	3 */


/* Media Transceiver (MXVR)   (0xFFC02700 - 0xFFC028FF) */

#define	MXVR_CONFIG	      0xFFC02700  /* MXVR Configuration	Register */
#define	MXVR_PLL_CTL_0	      0xFFC02704  /* MXVR Phase	Lock Loop Control Register 0 */

#define	MXVR_STATE_0	      0xFFC02708  /* MXVR State	Register 0 */
#define	MXVR_STATE_1	      0xFFC0270C  /* MXVR State	Register 1 */

#define	MXVR_INT_STAT_0	      0xFFC02710  /* MXVR Interrupt Status Register 0 */
#define	MXVR_INT_STAT_1	      0xFFC02714  /* MXVR Interrupt Status Register 1 */

#define	MXVR_INT_EN_0	      0xFFC02718  /* MXVR Interrupt Enable Register 0 */
#define	MXVR_INT_EN_1	      0xFFC0271C  /* MXVR Interrupt Enable Register 1 */

#define	MXVR_POSITION	      0xFFC02720  /* MXVR Node Position	Register */
#define	MXVR_MAX_POSITION     0xFFC02724  /* MXVR Maximum Node Position	Register */

#define	MXVR_DELAY	      0xFFC02728  /* MXVR Node Frame Delay Register */
#define	MXVR_MAX_DELAY	      0xFFC0272C  /* MXVR Maximum Node Frame Delay Register */

#define	MXVR_LADDR	      0xFFC02730  /* MXVR Logical Address Register */
#define	MXVR_GADDR	      0xFFC02734  /* MXVR Group	Address	Register */
#define	MXVR_AADDR	      0xFFC02738  /* MXVR Alternate Address Register */

#define	MXVR_ALLOC_0	      0xFFC0273C  /* MXVR Allocation Table Register 0 */
#define	MXVR_ALLOC_1	      0xFFC02740  /* MXVR Allocation Table Register 1 */
#define	MXVR_ALLOC_2	      0xFFC02744  /* MXVR Allocation Table Register 2 */
#define	MXVR_ALLOC_3	      0xFFC02748  /* MXVR Allocation Table Register 3 */
#define	MXVR_ALLOC_4	      0xFFC0274C  /* MXVR Allocation Table Register 4 */
#define	MXVR_ALLOC_5	      0xFFC02750  /* MXVR Allocation Table Register 5 */
#define	MXVR_ALLOC_6	      0xFFC02754  /* MXVR Allocation Table Register 6 */
#define	MXVR_ALLOC_7	      0xFFC02758  /* MXVR Allocation Table Register 7 */
#define	MXVR_ALLOC_8	      0xFFC0275C  /* MXVR Allocation Table Register 8 */
#define	MXVR_ALLOC_9	      0xFFC02760  /* MXVR Allocation Table Register 9 */
#define	MXVR_ALLOC_10	      0xFFC02764  /* MXVR Allocation Table Register 10 */
#define	MXVR_ALLOC_11	      0xFFC02768  /* MXVR Allocation Table Register 11 */
#define	MXVR_ALLOC_12	      0xFFC0276C  /* MXVR Allocation Table Register 12 */
#define	MXVR_ALLOC_13	      0xFFC02770  /* MXVR Allocation Table Register 13 */
#define	MXVR_ALLOC_14	      0xFFC02774  /* MXVR Allocation Table Register 14 */

#define	MXVR_SYNC_LCHAN_0     0xFFC02778  /* MXVR Sync Data Logical Channel Assign Register 0 */
#define	MXVR_SYNC_LCHAN_1     0xFFC0277C  /* MXVR Sync Data Logical Channel Assign Register 1 */
#define	MXVR_SYNC_LCHAN_2     0xFFC02780  /* MXVR Sync Data Logical Channel Assign Register 2 */
#define	MXVR_SYNC_LCHAN_3     0xFFC02784  /* MXVR Sync Data Logical Channel Assign Register 3 */
#define	MXVR_SYNC_LCHAN_4     0xFFC02788  /* MXVR Sync Data Logical Channel Assign Register 4 */
#define	MXVR_SYNC_LCHAN_5     0xFFC0278C  /* MXVR Sync Data Logical Channel Assign Register 5 */
#define	MXVR_SYNC_LCHAN_6     0xFFC02790  /* MXVR Sync Data Logical Channel Assign Register 6 */
#define	MXVR_SYNC_LCHAN_7     0xFFC02794  /* MXVR Sync Data Logical Channel Assign Register 7 */

#define	MXVR_DMA0_CONFIG      0xFFC02798  /* MXVR Sync Data DMA0 Config	Register */
#define	MXVR_DMA0_START_ADDR  0xFFC0279C  /* MXVR Sync Data DMA0 Start Address Register */
#define	MXVR_DMA0_COUNT	      0xFFC027A0  /* MXVR Sync Data DMA0 Loop Count Register */
#define	MXVR_DMA0_CURR_ADDR   0xFFC027A4  /* MXVR Sync Data DMA0 Current Address Register */
#define	MXVR_DMA0_CURR_COUNT  0xFFC027A8  /* MXVR Sync Data DMA0 Current Loop Count Register */

#define	MXVR_DMA1_CONFIG      0xFFC027AC  /* MXVR Sync Data DMA1 Config	Register */
#define	MXVR_DMA1_START_ADDR  0xFFC027B0  /* MXVR Sync Data DMA1 Start Address Register */
#define	MXVR_DMA1_COUNT	      0xFFC027B4  /* MXVR Sync Data DMA1 Loop Count Register */
#define	MXVR_DMA1_CURR_ADDR   0xFFC027B8  /* MXVR Sync Data DMA1 Current Address Register */
#define	MXVR_DMA1_CURR_COUNT  0xFFC027BC  /* MXVR Sync Data DMA1 Current Loop Count Register */

#define	MXVR_DMA2_CONFIG      0xFFC027C0  /* MXVR Sync Data DMA2 Config	Register */
#define	MXVR_DMA2_START_ADDR  0xFFC027C4  /* MXVR Sync Data DMA2 Start Address Register */
#define	MXVR_DMA2_COUNT	      0xFFC027C8  /* MXVR Sync Data DMA2 Loop Count Register */
#define	MXVR_DMA2_CURR_ADDR   0xFFC027CC  /* MXVR Sync Data DMA2 Current Address Register */
#define	MXVR_DMA2_CURR_COUNT  0xFFC027D0  /* MXVR Sync Data DMA2 Current Loop Count Register */

#define	MXVR_DMA3_CONFIG      0xFFC027D4  /* MXVR Sync Data DMA3 Config	Register */
#define	MXVR_DMA3_START_ADDR  0xFFC027D8  /* MXVR Sync Data DMA3 Start Address Register */
#define	MXVR_DMA3_COUNT	      0xFFC027DC  /* MXVR Sync Data DMA3 Loop Count Register */
#define	MXVR_DMA3_CURR_ADDR   0xFFC027E0  /* MXVR Sync Data DMA3 Current Address Register */
#define	MXVR_DMA3_CURR_COUNT  0xFFC027E4  /* MXVR Sync Data DMA3 Current Loop Count Register */

#define	MXVR_DMA4_CONFIG      0xFFC027E8  /* MXVR Sync Data DMA4 Config	Register */
#define	MXVR_DMA4_START_ADDR  0xFFC027EC  /* MXVR Sync Data DMA4 Start Address Register */
#define	MXVR_DMA4_COUNT	      0xFFC027F0  /* MXVR Sync Data DMA4 Loop Count Register */
#define	MXVR_DMA4_CURR_ADDR   0xFFC027F4  /* MXVR Sync Data DMA4 Current Address Register */
#define	MXVR_DMA4_CURR_COUNT  0xFFC027F8  /* MXVR Sync Data DMA4 Current Loop Count Register */

#define	MXVR_DMA5_CONFIG      0xFFC027FC  /* MXVR Sync Data DMA5 Config	Register */
#define	MXVR_DMA5_START_ADDR  0xFFC02800  /* MXVR Sync Data DMA5 Start Address Register */
#define	MXVR_DMA5_COUNT	      0xFFC02804  /* MXVR Sync Data DMA5 Loop Count Register */
#define	MXVR_DMA5_CURR_ADDR   0xFFC02808  /* MXVR Sync Data DMA5 Current Address Register */
#define	MXVR_DMA5_CURR_COUNT  0xFFC0280C  /* MXVR Sync Data DMA5 Current Loop Count Register */

#define	MXVR_DMA6_CONFIG      0xFFC02810  /* MXVR Sync Data DMA6 Config	Register */
#define	MXVR_DMA6_START_ADDR  0xFFC02814  /* MXVR Sync Data DMA6 Start Address Register */
#define	MXVR_DMA6_COUNT	      0xFFC02818  /* MXVR Sync Data DMA6 Loop Count Register */
#define	MXVR_DMA6_CURR_ADDR   0xFFC0281C  /* MXVR Sync Data DMA6 Current Address Register */
#define	MXVR_DMA6_CURR_COUNT  0xFFC02820  /* MXVR Sync Data DMA6 Current Loop Count Register */

#define	MXVR_DMA7_CONFIG      0xFFC02824  /* MXVR Sync Data DMA7 Config	Register */
#define	MXVR_DMA7_START_ADDR  0xFFC02828  /* MXVR Sync Data DMA7 Start Address Register */
#define	MXVR_DMA7_COUNT	      0xFFC0282C  /* MXVR Sync Data DMA7 Loop Count Register */
#define	MXVR_DMA7_CURR_ADDR   0xFFC02830  /* MXVR Sync Data DMA7 Current Address Register */
#define	MXVR_DMA7_CURR_COUNT  0xFFC02834  /* MXVR Sync Data DMA7 Current Loop Count Register */

#define	MXVR_AP_CTL	      0xFFC02838  /* MXVR Async	Packet Control Register */
#define	MXVR_APRB_START_ADDR  0xFFC0283C  /* MXVR Async	Packet RX Buffer Start Addr Register */
#define	MXVR_APRB_CURR_ADDR   0xFFC02840  /* MXVR Async	Packet RX Buffer Current Addr Register */
#define	MXVR_APTB_START_ADDR  0xFFC02844  /* MXVR Async	Packet TX Buffer Start Addr Register */
#define	MXVR_APTB_CURR_ADDR   0xFFC02848  /* MXVR Async	Packet TX Buffer Current Addr Register */

#define	MXVR_CM_CTL	      0xFFC0284C  /* MXVR Control Message Control Register */
#define	MXVR_CMRB_START_ADDR  0xFFC02850  /* MXVR Control Message RX Buffer Start Addr Register */
#define	MXVR_CMRB_CURR_ADDR   0xFFC02854  /* MXVR Control Message RX Buffer Current Address */
#define	MXVR_CMTB_START_ADDR  0xFFC02858  /* MXVR Control Message TX Buffer Start Addr Register */
#define	MXVR_CMTB_CURR_ADDR   0xFFC0285C  /* MXVR Control Message TX Buffer Current Address */

#define	MXVR_RRDB_START_ADDR  0xFFC02860  /* MXVR Remote Read Buffer Start Addr	Register */
#define	MXVR_RRDB_CURR_ADDR   0xFFC02864  /* MXVR Remote Read Buffer Current Addr Register */

#define	MXVR_PAT_DATA_0	      0xFFC02868  /* MXVR Pattern Data Register	0 */
#define	MXVR_PAT_EN_0	      0xFFC0286C  /* MXVR Pattern Enable Register 0 */
#define	MXVR_PAT_DATA_1	      0xFFC02870  /* MXVR Pattern Data Register	1 */
#define	MXVR_PAT_EN_1	      0xFFC02874  /* MXVR Pattern Enable Register 1 */

#define	MXVR_FRAME_CNT_0      0xFFC02878  /* MXVR Frame	Counter	0 */
#define	MXVR_FRAME_CNT_1      0xFFC0287C  /* MXVR Frame	Counter	1 */

#define	MXVR_ROUTING_0	      0xFFC02880  /* MXVR Routing Table	Register 0 */
#define	MXVR_ROUTING_1	      0xFFC02884  /* MXVR Routing Table	Register 1 */
#define	MXVR_ROUTING_2	      0xFFC02888  /* MXVR Routing Table	Register 2 */
#define	MXVR_ROUTING_3	      0xFFC0288C  /* MXVR Routing Table	Register 3 */
#define	MXVR_ROUTING_4	      0xFFC02890  /* MXVR Routing Table	Register 4 */
#define	MXVR_ROUTING_5	      0xFFC02894  /* MXVR Routing Table	Register 5 */
#define	MXVR_ROUTING_6	      0xFFC02898  /* MXVR Routing Table	Register 6 */
#define	MXVR_ROUTING_7	      0xFFC0289C  /* MXVR Routing Table	Register 7 */
#define	MXVR_ROUTING_8	      0xFFC028A0  /* MXVR Routing Table	Register 8 */
#define	MXVR_ROUTING_9	      0xFFC028A4  /* MXVR Routing Table	Register 9 */
#define	MXVR_ROUTING_10	      0xFFC028A8  /* MXVR Routing Table	Register 10 */
#define	MXVR_ROUTING_11	      0xFFC028AC  /* MXVR Routing Table	Register 11 */
#define	MXVR_ROUTING_12	      0xFFC028B0  /* MXVR Routing Table	Register 12 */
#define	MXVR_ROUTING_13	      0xFFC028B4  /* MXVR Routing Table	Register 13 */
#define	MXVR_ROUTING_14	      0xFFC028B8  /* MXVR Routing Table	Register 14 */

#define	MXVR_PLL_CTL_1	      0xFFC028BC  /* MXVR Phase	Lock Loop Control Register 1 */
#define	MXVR_BLOCK_CNT	      0xFFC028C0  /* MXVR Block	Counter */
#define	MXVR_PLL_CTL_2	      0xFFC028C4  /* MXVR Phase	Lock Loop Control Register 2 */


/* CAN Controller		(0xFFC02A00 - 0xFFC02FFF)				 */
/* For Mailboxes 0-15											 */
#define	CAN_MC1				0xFFC02A00	/* Mailbox config reg 1	 */
#define	CAN_MD1				0xFFC02A04	/* Mailbox direction reg 1 */
#define	CAN_TRS1			0xFFC02A08	/* Transmit Request Set	reg 1 */
#define	CAN_TRR1			0xFFC02A0C	/* Transmit Request Reset reg 1 */
#define	CAN_TA1				0xFFC02A10	/* Transmit Acknowledge	reg 1 */
#define	CAN_AA1				0xFFC02A14	/* Transmit Abort Acknowledge reg 1 */
#define	CAN_RMP1			0xFFC02A18	/* Receive Message Pending reg 1 */
#define	CAN_RML1			0xFFC02A1C	/* Receive Message Lost	reg 1 */
#define	CAN_MBTIF1			0xFFC02A20	/* Mailbox Transmit Interrupt Flag reg 1 */
#define	CAN_MBRIF1			0xFFC02A24	/* Mailbox Receive  Interrupt Flag reg 1 */
#define	CAN_MBIM1			0xFFC02A28	/* Mailbox Interrupt Mask reg 1 */
#define	CAN_RFH1			0xFFC02A2C	/* Remote Frame	Handling reg 1 */
#define	CAN_OPSS1			0xFFC02A30	/* Overwrite Protection	Single Shot Xmission reg 1 */

/* For Mailboxes 16-31											 */
#define	CAN_MC2				0xFFC02A40	/* Mailbox config reg 2	 */
#define	CAN_MD2				0xFFC02A44	/* Mailbox direction reg 2 */
#define	CAN_TRS2			0xFFC02A48	/* Transmit Request Set	reg 2 */
#define	CAN_TRR2			0xFFC02A4C	/* Transmit Request Reset reg 2 */
#define	CAN_TA2				0xFFC02A50	/* Transmit Acknowledge	reg 2 */
#define	CAN_AA2				0xFFC02A54	/* Transmit Abort Acknowledge reg 2 */
#define	CAN_RMP2			0xFFC02A58	/* Receive Message Pending reg 2 */
#define	CAN_RML2			0xFFC02A5C	/* Receive Message Lost	reg 2 */
#define	CAN_MBTIF2			0xFFC02A60	/* Mailbox Transmit Interrupt Flag reg 2 */
#define	CAN_MBRIF2			0xFFC02A64	/* Mailbox Receive  Interrupt Flag reg 2 */
#define	CAN_MBIM2			0xFFC02A68	/* Mailbox Interrupt Mask reg 2 */
#define	CAN_RFH2			0xFFC02A6C	/* Remote Frame	Handling reg 2 */
#define	CAN_OPSS2			0xFFC02A70	/* Overwrite Protection	Single Shot Xmission reg 2 */

#define	CAN_CLOCK			0xFFC02A80	/* Bit Timing Configuration register 0 */
#define	CAN_TIMING			0xFFC02A84	/* Bit Timing Configuration register 1 */

#define	CAN_DEBUG			0xFFC02A88	/* Debug Register		 */
/* the following is for	backwards compatibility */
#define	CAN_CNF		 CAN_DEBUG

#define	CAN_STATUS			0xFFC02A8C	/* Global Status Register */
#define	CAN_CEC				0xFFC02A90	/* Error Counter Register */
#define	CAN_GIS				0xFFC02A94	/* Global Interrupt Status Register */
#define	CAN_GIM				0xFFC02A98	/* Global Interrupt Mask Register */
#define	CAN_GIF				0xFFC02A9C	/* Global Interrupt Flag Register */
#define	CAN_CONTROL			0xFFC02AA0	/* Master Control Register */
#define	CAN_INTR			0xFFC02AA4	/* Interrupt Pending Register */
#define	CAN_MBTD			0xFFC02AAC	/* Mailbox Temporary Disable Feature */
#define	CAN_EWR				0xFFC02AB0	/* Programmable	Warning	Level */
#define	CAN_ESR				0xFFC02AB4	/* Error Status	Register */
#define	CAN_UCCNT			0xFFC02AC4	/* Universal Counter	 */
#define	CAN_UCRC			0xFFC02AC8	/* Universal Counter Reload/Capture Register */
#define	CAN_UCCNF			0xFFC02ACC	/* Universal Counter Configuration Register */

/* Mailbox Acceptance Masks					 */
#define	CAN_AM00L			0xFFC02B00	/* Mailbox 0 Low Acceptance Mask */
#define	CAN_AM00H			0xFFC02B04	/* Mailbox 0 High Acceptance Mask */
#define	CAN_AM01L			0xFFC02B08	/* Mailbox 1 Low Acceptance Mask */
#define	CAN_AM01H			0xFFC02B0C	/* Mailbox 1 High Acceptance Mask */
#define	CAN_AM02L			0xFFC02B10	/* Mailbox 2 Low Acceptance Mask */
#define	CAN_AM02H			0xFFC02B14	/* Mailbox 2 High Acceptance Mask */
#define	CAN_AM03L			0xFFC02B18	/* Mailbox 3 Low Acceptance Mask */
#define	CAN_AM03H			0xFFC02B1C	/* Mailbox 3 High Acceptance Mask */
#define	CAN_AM04L			0xFFC02B20	/* Mailbox 4 Low Acceptance Mask */
#define	CAN_AM04H			0xFFC02B24	/* Mailbox 4 High Acceptance Mask */
#define	CAN_AM05L			0xFFC02B28	/* Mailbox 5 Low Acceptance Mask */
#define	CAN_AM05H			0xFFC02B2C	/* Mailbox 5 High Acceptance Mask */
#define	CAN_AM06L			0xFFC02B30	/* Mailbox 6 Low Acceptance Mask */
#define	CAN_AM06H			0xFFC02B34	/* Mailbox 6 High Acceptance Mask */
#define	CAN_AM07L			0xFFC02B38	/* Mailbox 7 Low Acceptance Mask */
#define	CAN_AM07H			0xFFC02B3C	/* Mailbox 7 High Acceptance Mask */
#define	CAN_AM08L			0xFFC02B40	/* Mailbox 8 Low Acceptance Mask */
#define	CAN_AM08H			0xFFC02B44	/* Mailbox 8 High Acceptance Mask */
#define	CAN_AM09L			0xFFC02B48	/* Mailbox 9 Low Acceptance Mask */
#define	CAN_AM09H			0xFFC02B4C	/* Mailbox 9 High Acceptance Mask */
#define	CAN_AM10L			0xFFC02B50	/* Mailbox 10 Low Acceptance Mask */
#define	CAN_AM10H			0xFFC02B54	/* Mailbox 10 High Acceptance Mask */
#define	CAN_AM11L			0xFFC02B58	/* Mailbox 11 Low Acceptance Mask */
#define	CAN_AM11H			0xFFC02B5C	/* Mailbox 11 High Acceptance Mask */
#define	CAN_AM12L			0xFFC02B60	/* Mailbox 12 Low Acceptance Mask */
#define	CAN_AM12H			0xFFC02B64	/* Mailbox 12 High Acceptance Mask */
#define	CAN_AM13L			0xFFC02B68	/* Mailbox 13 Low Acceptance Mask */
#define	CAN_AM13H			0xFFC02B6C	/* Mailbox 13 High Acceptance Mask */
#define	CAN_AM14L			0xFFC02B70	/* Mailbox 14 Low Acceptance Mask */
#define	CAN_AM14H			0xFFC02B74	/* Mailbox 14 High Acceptance Mask */
#define	CAN_AM15L			0xFFC02B78	/* Mailbox 15 Low Acceptance Mask */
#define	CAN_AM15H			0xFFC02B7C	/* Mailbox 15 High Acceptance Mask */

#define	CAN_AM16L			0xFFC02B80	/* Mailbox 16 Low Acceptance Mask */
#define	CAN_AM16H			0xFFC02B84	/* Mailbox 16 High Acceptance Mask */
#define	CAN_AM17L			0xFFC02B88	/* Mailbox 17 Low Acceptance Mask */
#define	CAN_AM17H			0xFFC02B8C	/* Mailbox 17 High Acceptance Mask */
#define	CAN_AM18L			0xFFC02B90	/* Mailbox 18 Low Acceptance Mask */
#define	CAN_AM18H			0xFFC02B94	/* Mailbox 18 High Acceptance Mask */
#define	CAN_AM19L			0xFFC02B98	/* Mailbox 19 Low Acceptance Mask */
#define	CAN_AM19H			0xFFC02B9C	/* Mailbox 19 High Acceptance Mask */
#define	CAN_AM20L			0xFFC02BA0	/* Mailbox 20 Low Acceptance Mask */
#define	CAN_AM20H			0xFFC02BA4	/* Mailbox 20 High Acceptance Mask */
#define	CAN_AM21L			0xFFC02BA8	/* Mailbox 21 Low Acceptance Mask */
#define	CAN_AM21H			0xFFC02BAC	/* Mailbox 21 High Acceptance Mask */
#define	CAN_AM22L			0xFFC02BB0	/* Mailbox 22 Low Acceptance Mask */
#define	CAN_AM22H			0xFFC02BB4	/* Mailbox 22 High Acceptance Mask */
#define	CAN_AM23L			0xFFC02BB8	/* Mailbox 23 Low Acceptance Mask */
#define	CAN_AM23H			0xFFC02BBC	/* Mailbox 23 High Acceptance Mask */
#define	CAN_AM24L			0xFFC02BC0	/* Mailbox 24 Low Acceptance Mask */
#define	CAN_AM24H			0xFFC02BC4	/* Mailbox 24 High Acceptance Mask */
#define	CAN_AM25L			0xFFC02BC8	/* Mailbox 25 Low Acceptance Mask */
#define	CAN_AM25H			0xFFC02BCC	/* Mailbox 25 High Acceptance Mask */
#define	CAN_AM26L			0xFFC02BD0	/* Mailbox 26 Low Acceptance Mask */
#define	CAN_AM26H			0xFFC02BD4	/* Mailbox 26 High Acceptance Mask */
#define	CAN_AM27L			0xFFC02BD8	/* Mailbox 27 Low Acceptance Mask */
#define	CAN_AM27H			0xFFC02BDC	/* Mailbox 27 High Acceptance Mask */
#define	CAN_AM28L			0xFFC02BE0	/* Mailbox 28 Low Acceptance Mask */
#define	CAN_AM28H			0xFFC02BE4	/* Mailbox 28 High Acceptance Mask */
#define	CAN_AM29L			0xFFC02BE8	/* Mailbox 29 Low Acceptance Mask */
#define	CAN_AM29H			0xFFC02BEC	/* Mailbox 29 High Acceptance Mask */
#define	CAN_AM30L			0xFFC02BF0	/* Mailbox 30 Low Acceptance Mask */
#define	CAN_AM30H			0xFFC02BF4	/* Mailbox 30 High Acceptance Mask */
#define	CAN_AM31L			0xFFC02BF8	/* Mailbox 31 Low Acceptance Mask */
#define	CAN_AM31H			0xFFC02BFC	/* Mailbox 31 High Acceptance Mask */

/* CAN Acceptance Mask Macros */
#define	CAN_AM_L(x)			(CAN_AM00L+((x)*0x8))
#define	CAN_AM_H(x)			(CAN_AM00H+((x)*0x8))

/* Mailbox Registers									 */
#define	CAN_MB00_DATA0		0xFFC02C00	/* Mailbox 0 Data Word 0 [15:0]	Register */
#define	CAN_MB00_DATA1		0xFFC02C04	/* Mailbox 0 Data Word 1 [31:16] Register */
#define	CAN_MB00_DATA2		0xFFC02C08	/* Mailbox 0 Data Word 2 [47:32] Register */
#define	CAN_MB00_DATA3		0xFFC02C0C	/* Mailbox 0 Data Word 3 [63:48] Register */
#define	CAN_MB00_LENGTH		0xFFC02C10	/* Mailbox 0 Data Length Code Register */
#define	CAN_MB00_TIMESTAMP	0xFFC02C14	/* Mailbox 0 Time Stamp	Value Register */
#define	CAN_MB00_ID0		0xFFC02C18	/* Mailbox 0 Identifier	Low Register */
#define	CAN_MB00_ID1		0xFFC02C1C	/* Mailbox 0 Identifier	High Register */

#define	CAN_MB01_DATA0		0xFFC02C20	/* Mailbox 1 Data Word 0 [15:0]	Register */
#define	CAN_MB01_DATA1		0xFFC02C24	/* Mailbox 1 Data Word 1 [31:16] Register */
#define	CAN_MB01_DATA2		0xFFC02C28	/* Mailbox 1 Data Word 2 [47:32] Register */
#define	CAN_MB01_DATA3		0xFFC02C2C	/* Mailbox 1 Data Word 3 [63:48] Register */
#define	CAN_MB01_LENGTH		0xFFC02C30	/* Mailbox 1 Data Length Code Register */
#define	CAN_MB01_TIMESTAMP	0xFFC02C34	/* Mailbox 1 Time Stamp	Value Register */
#define	CAN_MB01_ID0		0xFFC02C38	/* Mailbox 1 Identifier	Low Register */
#define	CAN_MB01_ID1		0xFFC02C3C	/* Mailbox 1 Identifier	High Register */

#define	CAN_MB02_DATA0		0xFFC02C40	/* Mailbox 2 Data Word 0 [15:0]	Register */
#define	CAN_MB02_DATA1		0xFFC02C44	/* Mailbox 2 Data Word 1 [31:16] Register */
#define	CAN_MB02_DATA2		0xFFC02C48	/* Mailbox 2 Data Word 2 [47:32] Register */
#define	CAN_MB02_DATA3		0xFFC02C4C	/* Mailbox 2 Data Word 3 [63:48] Register */
#define	CAN_MB02_LENGTH		0xFFC02C50	/* Mailbox 2 Data Length Code Register */
#define	CAN_MB02_TIMESTAMP	0xFFC02C54	/* Mailbox 2 Time Stamp	Value Register */
#define	CAN_MB02_ID0		0xFFC02C58	/* Mailbox 2 Identifier	Low Register */
#define	CAN_MB02_ID1		0xFFC02C5C	/* Mailbox 2 Identifier	High Register */

#define	CAN_MB03_DATA0		0xFFC02C60	/* Mailbox 3 Data Word 0 [15:0]	Register */
#define	CAN_MB03_DATA1		0xFFC02C64	/* Mailbox 3 Data Word 1 [31:16] Register */
#define	CAN_MB03_DATA2		0xFFC02C68	/* Mailbox 3 Data Word 2 [47:32] Register */
#define	CAN_MB03_DATA3		0xFFC02C6C	/* Mailbox 3 Data Word 3 [63:48] Register */
#define	CAN_MB03_LENGTH		0xFFC02C70	/* Mailbox 3 Data Length Code Register */
#define	CAN_MB03_TIMESTAMP	0xFFC02C74	/* Mailbox 3 Time Stamp	Value Register */
#define	CAN_MB03_ID0		0xFFC02C78	/* Mailbox 3 Identifier	Low Register */
#define	CAN_MB03_ID1		0xFFC02C7C	/* Mailbox 3 Identifier	High Register */

#define	CAN_MB04_DATA0		0xFFC02C80	/* Mailbox 4 Data Word 0 [15:0]	Register */
#define	CAN_MB04_DATA1		0xFFC02C84	/* Mailbox 4 Data Word 1 [31:16] Register */
#define	CAN_MB04_DATA2		0xFFC02C88	/* Mailbox 4 Data Word 2 [47:32] Register */
#define	CAN_MB04_DATA3		0xFFC02C8C	/* Mailbox 4 Data Word 3 [63:48] Register */
#define	CAN_MB04_LENGTH		0xFFC02C90	/* Mailbox 4 Data Length Code Register */
#define	CAN_MB04_TIMESTAMP	0xFFC02C94	/* Mailbox 4 Time Stamp	Value Register */
#define	CAN_MB04_ID0		0xFFC02C98	/* Mailbox 4 Identifier	Low Register */
#define	CAN_MB04_ID1		0xFFC02C9C	/* Mailbox 4 Identifier	High Register */

#define	CAN_MB05_DATA0		0xFFC02CA0	/* Mailbox 5 Data Word 0 [15:0]	Register */
#define	CAN_MB05_DATA1		0xFFC02CA4	/* Mailbox 5 Data Word 1 [31:16] Register */
#define	CAN_MB05_DATA2		0xFFC02CA8	/* Mailbox 5 Data Word 2 [47:32] Register */
#define	CAN_MB05_DATA3		0xFFC02CAC	/* Mailbox 5 Data Word 3 [63:48] Register */
#define	CAN_MB05_LENGTH		0xFFC02CB0	/* Mailbox 5 Data Length Code Register */
#define	CAN_MB05_TIMESTAMP	0xFFC02CB4	/* Mailbox 5 Time Stamp	Value Register */
#define	CAN_MB05_ID0		0xFFC02CB8	/* Mailbox 5 Identifier	Low Register */
#define	CAN_MB05_ID1		0xFFC02CBC	/* Mailbox 5 Identifier	High Register */

#define	CAN_MB06_DATA0		0xFFC02CC0	/* Mailbox 6 Data Word 0 [15:0]	Register */
#define	CAN_MB06_DATA1		0xFFC02CC4	/* Mailbox 6 Data Word 1 [31:16] Register */
#define	CAN_MB06_DATA2		0xFFC02CC8	/* Mailbox 6 Data Word 2 [47:32] Register */
#define	CAN_MB06_DATA3		0xFFC02CCC	/* Mailbox 6 Data Word 3 [63:48] Register */
#define	CAN_MB06_LENGTH		0xFFC02CD0	/* Mailbox 6 Data Length Code Register */
#define	CAN_MB06_TIMESTAMP	0xFFC02CD4	/* Mailbox 6 Time Stamp	Value Register */
#define	CAN_MB06_ID0		0xFFC02CD8	/* Mailbox 6 Identifier	Low Register */
#define	CAN_MB06_ID1		0xFFC02CDC	/* Mailbox 6 Identifier	High Register */

#define	CAN_MB07_DATA0		0xFFC02CE0	/* Mailbox 7 Data Word 0 [15:0]	Register */
#define	CAN_MB07_DATA1		0xFFC02CE4	/* Mailbox 7 Data Word 1 [31:16] Register */
#define	CAN_MB07_DATA2		0xFFC02CE8	/* Mailbox 7 Data Word 2 [47:32] Register */
#define	CAN_MB07_DATA3		0xFFC02CEC	/* Mailbox 7 Data Word 3 [63:48] Register */
#define	CAN_MB07_LENGTH		0xFFC02CF0	/* Mailbox 7 Data Length Code Register */
#define	CAN_MB07_TIMESTAMP	0xFFC02CF4	/* Mailbox 7 Time Stamp	Value Register */
#define	CAN_MB07_ID0		0xFFC02CF8	/* Mailbox 7 Identifier	Low Register */
#define	CAN_MB07_ID1		0xFFC02CFC	/* Mailbox 7 Identifier	High Register */

#define	CAN_MB08_DATA0		0xFFC02D00	/* Mailbox 8 Data Word 0 [15:0]	Register */
#define	CAN_MB08_DATA1		0xFFC02D04	/* Mailbox 8 Data Word 1 [31:16] Register */
#define	CAN_MB08_DATA2		0xFFC02D08	/* Mailbox 8 Data Word 2 [47:32] Register */
#define	CAN_MB08_DATA3		0xFFC02D0C	/* Mailbox 8 Data Word 3 [63:48] Register */
#define	CAN_MB08_LENGTH		0xFFC02D10	/* Mailbox 8 Data Length Code Register */
#define	CAN_MB08_TIMESTAMP	0xFFC02D14	/* Mailbox 8 Time Stamp	Value Register */
#define	CAN_MB08_ID0		0xFFC02D18	/* Mailbox 8 Identifier	Low Register */
#define	CAN_MB08_ID1		0xFFC02D1C	/* Mailbox 8 Identifier	High Register */

#define	CAN_MB09_DATA0		0xFFC02D20	/* Mailbox 9 Data Word 0 [15:0]	Register */
#define	CAN_MB09_DATA1		0xFFC02D24	/* Mailbox 9 Data Word 1 [31:16] Register */
#define	CAN_MB09_DATA2		0xFFC02D28	/* Mailbox 9 Data Word 2 [47:32] Register */
#define	CAN_MB09_DATA3		0xFFC02D2C	/* Mailbox 9 Data Word 3 [63:48] Register */
#define	CAN_MB09_LENGTH		0xFFC02D30	/* Mailbox 9 Data Length Code Register */
#define	CAN_MB09_TIMESTAMP	0xFFC02D34	/* Mailbox 9 Time Stamp	Value Register */
#define	CAN_MB09_ID0		0xFFC02D38	/* Mailbox 9 Identifier	Low Register */
#define	CAN_MB09_ID1		0xFFC02D3C	/* Mailbox 9 Identifier	High Register */

#define	CAN_MB10_DATA0		0xFFC02D40	/* Mailbox 10 Data Word	0 [15:0] Register */
#define	CAN_MB10_DATA1		0xFFC02D44	/* Mailbox 10 Data Word	1 [31:16] Register */
#define	CAN_MB10_DATA2		0xFFC02D48	/* Mailbox 10 Data Word	2 [47:32] Register */
#define	CAN_MB10_DATA3		0xFFC02D4C	/* Mailbox 10 Data Word	3 [63:48] Register */
#define	CAN_MB10_LENGTH		0xFFC02D50	/* Mailbox 10 Data Length Code Register */
#define	CAN_MB10_TIMESTAMP	0xFFC02D54	/* Mailbox 10 Time Stamp Value Register */
#define	CAN_MB10_ID0		0xFFC02D58	/* Mailbox 10 Identifier Low Register */
#define	CAN_MB10_ID1		0xFFC02D5C	/* Mailbox 10 Identifier High Register */

#define	CAN_MB11_DATA0		0xFFC02D60	/* Mailbox 11 Data Word	0 [15:0] Register */
#define	CAN_MB11_DATA1		0xFFC02D64	/* Mailbox 11 Data Word	1 [31:16] Register */
#define	CAN_MB11_DATA2		0xFFC02D68	/* Mailbox 11 Data Word	2 [47:32] Register */
#define	CAN_MB11_DATA3		0xFFC02D6C	/* Mailbox 11 Data Word	3 [63:48] Register */
#define	CAN_MB11_LENGTH		0xFFC02D70	/* Mailbox 11 Data Length Code Register */
#define	CAN_MB11_TIMESTAMP	0xFFC02D74	/* Mailbox 11 Time Stamp Value Register */
#define	CAN_MB11_ID0		0xFFC02D78	/* Mailbox 11 Identifier Low Register */
#define	CAN_MB11_ID1		0xFFC02D7C	/* Mailbox 11 Identifier High Register */

#define	CAN_MB12_DATA0		0xFFC02D80	/* Mailbox 12 Data Word	0 [15:0] Register */
#define	CAN_MB12_DATA1		0xFFC02D84	/* Mailbox 12 Data Word	1 [31:16] Register */
#define	CAN_MB12_DATA2		0xFFC02D88	/* Mailbox 12 Data Word	2 [47:32] Register */
#define	CAN_MB12_DATA3		0xFFC02D8C	/* Mailbox 12 Data Word	3 [63:48] Register */
#define	CAN_MB12_LENGTH		0xFFC02D90	/* Mailbox 12 Data Length Code Register */
#define	CAN_MB12_TIMESTAMP	0xFFC02D94	/* Mailbox 12 Time Stamp Value Register */
#define	CAN_MB12_ID0		0xFFC02D98	/* Mailbox 12 Identifier Low Register */
#define	CAN_MB12_ID1		0xFFC02D9C	/* Mailbox 12 Identifier High Register */

#define	CAN_MB13_DATA0		0xFFC02DA0	/* Mailbox 13 Data Word	0 [15:0] Register */
#define	CAN_MB13_DATA1		0xFFC02DA4	/* Mailbox 13 Data Word	1 [31:16] Register */
#define	CAN_MB13_DATA2		0xFFC02DA8	/* Mailbox 13 Data Word	2 [47:32] Register */
#define	CAN_MB13_DATA3		0xFFC02DAC	/* Mailbox 13 Data Word	3 [63:48] Register */
#define	CAN_MB13_LENGTH		0xFFC02DB0	/* Mailbox 13 Data Length Code Register */
#define	CAN_MB13_TIMESTAMP	0xFFC02DB4	/* Mailbox 13 Time Stamp Value Register */
#define	CAN_MB13_ID0		0xFFC02DB8	/* Mailbox 13 Identifier Low Register */
#define	CAN_MB13_ID1		0xFFC02DBC	/* Mailbox 13 Identifier High Register */

#define	CAN_MB14_DATA0		0xFFC02DC0	/* Mailbox 14 Data Word	0 [15:0] Register */
#define	CAN_MB14_DATA1		0xFFC02DC4	/* Mailbox 14 Data Word	1 [31:16] Register */
#define	CAN_MB14_DATA2		0xFFC02DC8	/* Mailbox 14 Data Word	2 [47:32] Register */
#define	CAN_MB14_DATA3		0xFFC02DCC	/* Mailbox 14 Data Word	3 [63:48] Register */
#define	CAN_MB14_LENGTH		0xFFC02DD0	/* Mailbox 14 Data Length Code Register */
#define	CAN_MB14_TIMESTAMP	0xFFC02DD4	/* Mailbox 14 Time Stamp Value Register */
#define	CAN_MB14_ID0		0xFFC02DD8	/* Mailbox 14 Identifier Low Register */
#define	CAN_MB14_ID1		0xFFC02DDC	/* Mailbox 14 Identifier High Register */

#define	CAN_MB15_DATA0		0xFFC02DE0	/* Mailbox 15 Data Word	0 [15:0] Register */
#define	CAN_MB15_DATA1		0xFFC02DE4	/* Mailbox 15 Data Word	1 [31:16] Register */
#define	CAN_MB15_DATA2		0xFFC02DE8	/* Mailbox 15 Data Word	2 [47:32] Register */
#define	CAN_MB15_DATA3		0xFFC02DEC	/* Mailbox 15 Data Word	3 [63:48] Register */
#define	CAN_MB15_LENGTH		0xFFC02DF0	/* Mailbox 15 Data Length Code Register */
#define	CAN_MB15_TIMESTAMP	0xFFC02DF4	/* Mailbox 15 Time Stamp Value Register */
#define	CAN_MB15_ID0		0xFFC02DF8	/* Mailbox 15 Identifier Low Register */
#define	CAN_MB15_ID1		0xFFC02DFC	/* Mailbox 15 Identifier High Register */

#define	CAN_MB16_DATA0		0xFFC02E00	/* Mailbox 16 Data Word	0 [15:0] Register */
#define	CAN_MB16_DATA1		0xFFC02E04	/* Mailbox 16 Data Word	1 [31:16] Register */
#define	CAN_MB16_DATA2		0xFFC02E08	/* Mailbox 16 Data Word	2 [47:32] Register */
#define	CAN_MB16_DATA3		0xFFC02E0C	/* Mailbox 16 Data Word	3 [63:48] Register */
#define	CAN_MB16_LENGTH		0xFFC02E10	/* Mailbox 16 Data Length Code Register */
#define	CAN_MB16_TIMESTAMP	0xFFC02E14	/* Mailbox 16 Time Stamp Value Register */
#define	CAN_MB16_ID0		0xFFC02E18	/* Mailbox 16 Identifier Low Register */
#define	CAN_MB16_ID1		0xFFC02E1C	/* Mailbox 16 Identifier High Register */

#define	CAN_MB17_DATA0		0xFFC02E20	/* Mailbox 17 Data Word	0 [15:0] Register */
#define	CAN_MB17_DATA1		0xFFC02E24	/* Mailbox 17 Data Word	1 [31:16] Register */
#define	CAN_MB17_DATA2		0xFFC02E28	/* Mailbox 17 Data Word	2 [47:32] Register */
#define	CAN_MB17_DATA3		0xFFC02E2C	/* Mailbox 17 Data Word	3 [63:48] Register */
#define	CAN_MB17_LENGTH		0xFFC02E30	/* Mailbox 17 Data Length Code Register */
#define	CAN_MB17_TIMESTAMP	0xFFC02E34	/* Mailbox 17 Time Stamp Value Register */
#define	CAN_MB17_ID0		0xFFC02E38	/* Mailbox 17 Identifier Low Register */
#define	CAN_MB17_ID1		0xFFC02E3C	/* Mailbox 17 Identifier High Register */

#define	CAN_MB18_DATA0		0xFFC02E40	/* Mailbox 18 Data Word	0 [15:0] Register */
#define	CAN_MB18_DATA1		0xFFC02E44	/* Mailbox 18 Data Word	1 [31:16] Register */
#define	CAN_MB18_DATA2		0xFFC02E48	/* Mailbox 18 Data Word	2 [47:32] Register */
#define	CAN_MB18_DATA3		0xFFC02E4C	/* Mailbox 18 Data Word	3 [63:48] Register */
#define	CAN_MB18_LENGTH		0xFFC02E50	/* Mailbox 18 Data Length Code Register */
#define	CAN_MB18_TIMESTAMP	0xFFC02E54	/* Mailbox 18 Time Stamp Value Register */
#define	CAN_MB18_ID0		0xFFC02E58	/* Mailbox 18 Identifier Low Register */
#define	CAN_MB18_ID1		0xFFC02E5C	/* Mailbox 18 Identifier High Register */

#define	CAN_MB19_DATA0		0xFFC02E60	/* Mailbox 19 Data Word	0 [15:0] Register */
#define	CAN_MB19_DATA1		0xFFC02E64	/* Mailbox 19 Data Word	1 [31:16] Register */
#define	CAN_MB19_DATA2		0xFFC02E68	/* Mailbox 19 Data Word	2 [47:32] Register */
#define	CAN_MB19_DATA3		0xFFC02E6C	/* Mailbox 19 Data Word	3 [63:48] Register */
#define	CAN_MB19_LENGTH		0xFFC02E70	/* Mailbox 19 Data Length Code Register */
#define	CAN_MB19_TIMESTAMP	0xFFC02E74	/* Mailbox 19 Time Stamp Value Register */
#define	CAN_MB19_ID0		0xFFC02E78	/* Mailbox 19 Identifier Low Register */
#define	CAN_MB19_ID1		0xFFC02E7C	/* Mailbox 19 Identifier High Register */

#define	CAN_MB20_DATA0		0xFFC02E80	/* Mailbox 20 Data Word	0 [15:0] Register */
#define	CAN_MB20_DATA1		0xFFC02E84	/* Mailbox 20 Data Word	1 [31:16] Register */
#define	CAN_MB20_DATA2		0xFFC02E88	/* Mailbox 20 Data Word	2 [47:32] Register */
#define	CAN_MB20_DATA3		0xFFC02E8C	/* Mailbox 20 Data Word	3 [63:48] Register */
#define	CAN_MB20_LENGTH		0xFFC02E90	/* Mailbox 20 Data Length Code Register */
#define	CAN_MB20_TIMESTAMP	0xFFC02E94	/* Mailbox 20 Time Stamp Value Register */
#define	CAN_MB20_ID0		0xFFC02E98	/* Mailbox 20 Identifier Low Register */
#define	CAN_MB20_ID1		0xFFC02E9C	/* Mailbox 20 Identifier High Register */

#define	CAN_MB21_DATA0		0xFFC02EA0	/* Mailbox 21 Data Word	0 [15:0] Register */
#define	CAN_MB21_DATA1		0xFFC02EA4	/* Mailbox 21 Data Word	1 [31:16] Register */
#define	CAN_MB21_DATA2		0xFFC02EA8	/* Mailbox 21 Data Word	2 [47:32] Register */
#define	CAN_MB21_DATA3		0xFFC02EAC	/* Mailbox 21 Data Word	3 [63:48] Register */
#define	CAN_MB21_LENGTH		0xFFC02EB0	/* Mailbox 21 Data Length Code Register */
#define	CAN_MB21_TIMESTAMP	0xFFC02EB4	/* Mailbox 21 Time Stamp Value Register */
#define	CAN_MB21_ID0		0xFFC02EB8	/* Mailbox 21 Identifier Low Register */
#define	CAN_MB21_ID1		0xFFC02EBC	/* Mailbox 21 Identifier High Register */

#define	CAN_MB22_DATA0		0xFFC02EC0	/* Mailbox 22 Data Word	0 [15:0] Register */
#define	CAN_MB22_DATA1		0xFFC02EC4	/* Mailbox 22 Data Word	1 [31:16] Register */
#define	CAN_MB22_DATA2		0xFFC02EC8	/* Mailbox 22 Data Word	2 [47:32] Register */
#define	CAN_MB22_DATA3		0xFFC02ECC	/* Mailbox 22 Data Word	3 [63:48] Register */
#define	CAN_MB22_LENGTH		0xFFC02ED0	/* Mailbox 22 Data Length Code Register */
#define	CAN_MB22_TIMESTAMP	0xFFC02ED4	/* Mailbox 22 Time Stamp Value Register */
#define	CAN_MB22_ID0		0xFFC02ED8	/* Mailbox 22 Identifier Low Register */
#define	CAN_MB22_ID1		0xFFC02EDC	/* Mailbox 22 Identifier High Register */

#define	CAN_MB23_DATA0		0xFFC02EE0	/* Mailbox 23 Data Word	0 [15:0] Register */
#define	CAN_MB23_DATA1		0xFFC02EE4	/* Mailbox 23 Data Word	1 [31:16] Register */
#define	CAN_MB23_DATA2		0xFFC02EE8	/* Mailbox 23 Data Word	2 [47:32] Register */
#define	CAN_MB23_DATA3		0xFFC02EEC	/* Mailbox 23 Data Word	3 [63:48] Register */
#define	CAN_MB23_LENGTH		0xFFC02EF0	/* Mailbox 23 Data Length Code Register */
#define	CAN_MB23_TIMESTAMP	0xFFC02EF4	/* Mailbox 23 Time Stamp Value Register */
#define	CAN_MB23_ID0		0xFFC02EF8	/* Mailbox 23 Identifier Low Register */
#define	CAN_MB23_ID1		0xFFC02EFC	/* Mailbox 23 Identifier High Register */

#define	CAN_MB24_DATA0		0xFFC02F00	/* Mailbox 24 Data Word	0 [15:0] Register */
#define	CAN_MB24_DATA1		0xFFC02F04	/* Mailbox 24 Data Word	1 [31:16] Register */
#define	CAN_MB24_DATA2		0xFFC02F08	/* Mailbox 24 Data Word	2 [47:32] Register */
#define	CAN_MB24_DATA3		0xFFC02F0C	/* Mailbox 24 Data Word	3 [63:48] Register */
#define	CAN_MB24_LENGTH		0xFFC02F10	/* Mailbox 24 Data Length Code Register */
#define	CAN_MB24_TIMESTAMP	0xFFC02F14	/* Mailbox 24 Time Stamp Value Register */
#define	CAN_MB24_ID0		0xFFC02F18	/* Mailbox 24 Identifier Low Register */
#define	CAN_MB24_ID1		0xFFC02F1C	/* Mailbox 24 Identifier High Register */

#define	CAN_MB25_DATA0		0xFFC02F20	/* Mailbox 25 Data Word	0 [15:0] Register */
#define	CAN_MB25_DATA1		0xFFC02F24	/* Mailbox 25 Data Word	1 [31:16] Register */
#define	CAN_MB25_DATA2		0xFFC02F28	/* Mailbox 25 Data Word	2 [47:32] Register */
#define	CAN_MB25_DATA3		0xFFC02F2C	/* Mailbox 25 Data Word	3 [63:48] Register */
#define	CAN_MB25_LENGTH		0xFFC02F30	/* Mailbox 25 Data Length Code Register */
#define	CAN_MB25_TIMESTAMP	0xFFC02F34	/* Mailbox 25 Time Stamp Value Register */
#define	CAN_MB25_ID0		0xFFC02F38	/* Mailbox 25 Identifier Low Register */
#define	CAN_MB25_ID1		0xFFC02F3C	/* Mailbox 25 Identifier High Register */

#define	CAN_MB26_DATA0		0xFFC02F40	/* Mailbox 26 Data Word	0 [15:0] Register */
#define	CAN_MB26_DATA1		0xFFC02F44	/* Mailbox 26 Data Word	1 [31:16] Register */
#define	CAN_MB26_DATA2		0xFFC02F48	/* Mailbox 26 Data Word	2 [47:32] Register */
#define	CAN_MB26_DATA3		0xFFC02F4C	/* Mailbox 26 Data Word	3 [63:48] Register */
#define	CAN_MB26_LENGTH		0xFFC02F50	/* Mailbox 26 Data Length Code Register */
#define	CAN_MB26_TIMESTAMP	0xFFC02F54	/* Mailbox 26 Time Stamp Value Register */
#define	CAN_MB26_ID0		0xFFC02F58	/* Mailbox 26 Identifier Low Register */
#define	CAN_MB26_ID1		0xFFC02F5C	/* Mailbox 26 Identifier High Register */

#define	CAN_MB27_DATA0		0xFFC02F60	/* Mailbox 27 Data Word	0 [15:0] Register */
#define	CAN_MB27_DATA1		0xFFC02F64	/* Mailbox 27 Data Word	1 [31:16] Register */
#define	CAN_MB27_DATA2		0xFFC02F68	/* Mailbox 27 Data Word	2 [47:32] Register */
#define	CAN_MB27_DATA3		0xFFC02F6C	/* Mailbox 27 Data Word	3 [63:48] Register */
#define	CAN_MB27_LENGTH		0xFFC02F70	/* Mailbox 27 Data Length Code Register */
#define	CAN_MB27_TIMESTAMP	0xFFC02F74	/* Mailbox 27 Time Stamp Value Register */
#define	CAN_MB27_ID0		0xFFC02F78	/* Mailbox 27 Identifier Low Register */
#define	CAN_MB27_ID1		0xFFC02F7C	/* Mailbox 27 Identifier High Register */

#define	CAN_MB28_DATA0		0xFFC02F80	/* Mailbox 28 Data Word	0 [15:0] Register */
#define	CAN_MB28_DATA1		0xFFC02F84	/* Mailbox 28 Data Word	1 [31:16] Register */
#define	CAN_MB28_DATA2		0xFFC02F88	/* Mailbox 28 Data Word	2 [47:32] Register */
#define	CAN_MB28_DATA3		0xFFC02F8C	/* Mailbox 28 Data Word	3 [63:48] Register */
#define	CAN_MB28_LENGTH		0xFFC02F90	/* Mailbox 28 Data Length Code Register */
#define	CAN_MB28_TIMESTAMP	0xFFC02F94	/* Mailbox 28 Time Stamp Value Register */
#define	CAN_MB28_ID0		0xFFC02F98	/* Mailbox 28 Identifier Low Register */
#define	CAN_MB28_ID1		0xFFC02F9C	/* Mailbox 28 Identifier High Register */

#define	CAN_MB29_DATA0		0xFFC02FA0	/* Mailbox 29 Data Word	0 [15:0] Register */
#define	CAN_MB29_DATA1		0xFFC02FA4	/* Mailbox 29 Data Word	1 [31:16] Register */
#define	CAN_MB29_DATA2		0xFFC02FA8	/* Mailbox 29 Data Word	2 [47:32] Register */
#define	CAN_MB29_DATA3		0xFFC02FAC	/* Mailbox 29 Data Word	3 [63:48] Register */
#define	CAN_MB29_LENGTH		0xFFC02FB0	/* Mailbox 29 Data Length Code Register */
#define	CAN_MB29_TIMESTAMP	0xFFC02FB4	/* Mailbox 29 Time Stamp Value Register */
#define	CAN_MB29_ID0		0xFFC02FB8	/* Mailbox 29 Identifier Low Register */
#define	CAN_MB29_ID1		0xFFC02FBC	/* Mailbox 29 Identifier High Register */

#define	CAN_MB30_DATA0		0xFFC02FC0	/* Mailbox 30 Data Word	0 [15:0] Register */
#define	CAN_MB30_DATA1		0xFFC02FC4	/* Mailbox 30 Data Word	1 [31:16] Register */
#define	CAN_MB30_DATA2		0xFFC02FC8	/* Mailbox 30 Data Word	2 [47:32] Register */
#define	CAN_MB30_DATA3		0xFFC02FCC	/* Mailbox 30 Data Word	3 [63:48] Register */
#define	CAN_MB30_LENGTH		0xFFC02FD0	/* Mailbox 30 Data Length Code Register */
#define	CAN_MB30_TIMESTAMP	0xFFC02FD4	/* Mailbox 30 Time Stamp Value Register */
#define	CAN_MB30_ID0		0xFFC02FD8	/* Mailbox 30 Identifier Low Register */
#define	CAN_MB30_ID1		0xFFC02FDC	/* Mailbox 30 Identifier High Register */

#define	CAN_MB31_DATA0		0xFFC02FE0	/* Mailbox 31 Data Word	0 [15:0] Register */
#define	CAN_MB31_DATA1		0xFFC02FE4	/* Mailbox 31 Data Word	1 [31:16] Register */
#define	CAN_MB31_DATA2		0xFFC02FE8	/* Mailbox 31 Data Word	2 [47:32] Register */
#define	CAN_MB31_DATA3		0xFFC02FEC	/* Mailbox 31 Data Word	3 [63:48] Register */
#define	CAN_MB31_LENGTH		0xFFC02FF0	/* Mailbox 31 Data Length Code Register */
#define	CAN_MB31_TIMESTAMP	0xFFC02FF4	/* Mailbox 31 Time Stamp Value Register */
#define	CAN_MB31_ID0		0xFFC02FF8	/* Mailbox 31 Identifier Low Register */
#define	CAN_MB31_ID1		0xFFC02FFC	/* Mailbox 31 Identifier High Register */

/* CAN Mailbox Area Macros */
#define	CAN_MB_ID1(x)		(CAN_MB00_ID1+((x)*0x20))
#define	CAN_MB_ID0(x)		(CAN_MB00_ID0+((x)*0x20))
#define	CAN_MB_TIMESTAMP(x)	(CAN_MB00_TIMESTAMP+((x)*0x20))
#define	CAN_MB_LENGTH(x)	(CAN_MB00_LENGTH+((x)*0x20))
#define	CAN_MB_DATA3(x)		(CAN_MB00_DATA3+((x)*0x20))
#define	CAN_MB_DATA2(x)		(CAN_MB00_DATA2+((x)*0x20))
#define	CAN_MB_DATA1(x)		(CAN_MB00_DATA1+((x)*0x20))
#define	CAN_MB_DATA0(x)		(CAN_MB00_DATA0+((x)*0x20))


/*********************************************************************************** */
/* System MMR Register Bits and	Macros */
/******************************************************************************* */

/* SWRST Mask */
#define	SYSTEM_RESET	0x0007	/* Initiates A System Software Reset */
#define	DOUBLE_FAULT	0x0008	/* Core	Double Fault Causes Reset */
#define	RESET_DOUBLE	0x2000	/* SW Reset Generated By Core Double-Fault */
#define	RESET_WDOG		0x4000	/* SW Reset Generated By Watchdog Timer */
#define	RESET_SOFTWARE	0x8000	/* SW Reset Occurred Since Last	Read Of	SWRST */

/* SYSCR Masks													 */
#define	BMODE			0x0006	/* Boot	Mode - Latched During HW Reset From Mode Pins */
#define	NOBOOT			0x0010	/* Execute From	L1 or ASYNC Bank 0 When	BMODE =	0 */


/* *************  SYSTEM INTERRUPT CONTROLLER MASKS ***************** */

/* Peripheral Masks For	SIC0_ISR, SIC0_IWR, SIC0_IMASK */
#define	PLL_WAKEUP_IRQ		0x00000001	/* PLL Wakeup Interrupt	Request */
#define	DMAC0_ERR_IRQ		0x00000002	/* DMA Controller 0 Error Interrupt Request */
#define	PPI_ERR_IRQ		0x00000004	/* PPI Error Interrupt Request */
#define	SPORT0_ERR_IRQ		0x00000008	/* SPORT0 Error	Interrupt Request */
#define	SPORT1_ERR_IRQ		0x00000010	/* SPORT1 Error	Interrupt Request */
#define	SPI0_ERR_IRQ		0x00000020	/* SPI0	Error Interrupt	Request */
#define	UART0_ERR_IRQ		0x00000040	/* UART0 Error Interrupt Request */
#define	RTC_IRQ			0x00000080	/* Real-Time Clock Interrupt Request */
#define	DMA0_IRQ		0x00000100	/* DMA Channel 0 (PPI) Interrupt Request */
#define	DMA1_IRQ		0x00000200	/* DMA Channel 1 (SPORT0 RX) Interrupt Request */
#define	DMA2_IRQ		0x00000400	/* DMA Channel 2 (SPORT0 TX) Interrupt Request */
#define	DMA3_IRQ		0x00000800	/* DMA Channel 3 (SPORT1 RX) Interrupt Request */
#define	DMA4_IRQ		0x00001000	/* DMA Channel 4 (SPORT1 TX) Interrupt Request */
#define	DMA5_IRQ		0x00002000	/* DMA Channel 5 (SPI) Interrupt Request */
#define	DMA6_IRQ		0x00004000	/* DMA Channel 6 (UART RX) Interrupt Request */
#define	DMA7_IRQ		0x00008000	/* DMA Channel 7 (UART TX) Interrupt Request */
#define	TIMER0_IRQ		0x00010000	/* Timer 0 Interrupt Request */
#define	TIMER1_IRQ		0x00020000	/* Timer 1 Interrupt Request */
#define	TIMER2_IRQ		0x00040000	/* Timer 2 Interrupt Request */
#define	PFA_IRQ			0x00080000	/* Programmable	Flag Interrupt Request A */
#define	PFB_IRQ			0x00100000	/* Programmable	Flag Interrupt Request B */
#define	MDMA0_0_IRQ		0x00200000	/* MemDMA0 Stream 0 Interrupt Request */
#define	MDMA0_1_IRQ		0x00400000	/* MemDMA0 Stream 1 Interrupt Request */
#define	WDOG_IRQ		0x00800000	/* Software Watchdog Timer Interrupt Request */
#define	DMAC1_ERR_IRQ		0x01000000	/* DMA Controller 1 Error Interrupt Request */
#define	SPORT2_ERR_IRQ		0x02000000	/* SPORT2 Error	Interrupt Request */
#define	SPORT3_ERR_IRQ		0x04000000	/* SPORT3 Error	Interrupt Request */
#define	MXVR_SD_IRQ		0x08000000	/* MXVR	Synchronous Data Interrupt Request */
#define	SPI1_ERR_IRQ		0x10000000	/* SPI1	Error Interrupt	Request */
#define	SPI2_ERR_IRQ		0x20000000	/* SPI2	Error Interrupt	Request */
#define	UART1_ERR_IRQ		0x40000000	/* UART1 Error Interrupt Request */
#define	UART2_ERR_IRQ		0x80000000	/* UART2 Error Interrupt Request */

/* the following are for backwards compatibility */
#define	DMA0_ERR_IRQ		DMAC0_ERR_IRQ
#define	DMA1_ERR_IRQ		DMAC1_ERR_IRQ


/* Peripheral Masks For	SIC_ISR1, SIC_IWR1, SIC_IMASK1	 */
#define	CAN_ERR_IRQ			0x00000001	/* CAN Error Interrupt Request */
#define	DMA8_IRQ			0x00000002	/* DMA Channel 8 (SPORT2 RX) Interrupt Request */
#define	DMA9_IRQ			0x00000004	/* DMA Channel 9 (SPORT2 TX) Interrupt Request */
#define	DMA10_IRQ			0x00000008	/* DMA Channel 10 (SPORT3 RX) Interrupt	Request */
#define	DMA11_IRQ			0x00000010	/* DMA Channel 11 (SPORT3 TX) Interrupt	Request */
#define	DMA12_IRQ			0x00000020	/* DMA Channel 12 Interrupt Request */
#define	DMA13_IRQ			0x00000040	/* DMA Channel 13 Interrupt Request */
#define	DMA14_IRQ			0x00000080	/* DMA Channel 14 (SPI1) Interrupt Request */
#define	DMA15_IRQ			0x00000100	/* DMA Channel 15 (SPI2) Interrupt Request */
#define	DMA16_IRQ			0x00000200	/* DMA Channel 16 (UART1 RX) Interrupt Request */
#define	DMA17_IRQ			0x00000400	/* DMA Channel 17 (UART1 TX) Interrupt Request */
#define	DMA18_IRQ			0x00000800	/* DMA Channel 18 (UART2 RX) Interrupt Request */
#define	DMA19_IRQ			0x00001000	/* DMA Channel 19 (UART2 TX) Interrupt Request */
#define	TWI0_IRQ			0x00002000	/* TWI0	Interrupt Request */
#define	TWI1_IRQ			0x00004000	/* TWI1	Interrupt Request */
#define	CAN_RX_IRQ			0x00008000	/* CAN Receive Interrupt Request */
#define	CAN_TX_IRQ			0x00010000	/* CAN Transmit	Interrupt Request */
#define	MDMA1_0_IRQ			0x00020000	/* MemDMA1 Stream 0 Interrupt Request */
#define	MDMA1_1_IRQ			0x00040000	/* MemDMA1 Stream 1 Interrupt Request */
#define	MXVR_STAT_IRQ			0x00080000	/* MXVR	Status Interrupt Request */
#define	MXVR_CM_IRQ			0x00100000	/* MXVR	Control	Message	Interrupt Request */
#define	MXVR_AP_IRQ			0x00200000	/* MXVR	Asynchronous Packet Interrupt */

/* the following are for backwards compatibility */
#define	MDMA0_IRQ		MDMA1_0_IRQ
#define	MDMA1_IRQ		MDMA1_1_IRQ

#ifdef _MISRA_RULES
#define	_MF15 0xFu
#define	_MF7 7u
#else
#define	_MF15 0xF
#define	_MF7 7
#endif /* _MISRA_RULES */

/* SIC_IMASKx Masks											 */
#define	SIC_UNMASK_ALL	0x00000000					/* Unmask all peripheral interrupts */
#define	SIC_MASK_ALL	0xFFFFFFFF					/* Mask	all peripheral interrupts */
#ifdef _MISRA_RULES
#define	SIC_MASK(x)		(1 << ((x)&0x1Fu))					/* Mask	Peripheral #x interrupt */
#define	SIC_UNMASK(x)	(0xFFFFFFFFu ^ (1 << ((x)&0x1Fu)))	/* Unmask Peripheral #x	interrupt */
#else
#define	SIC_MASK(x)		(1 << ((x)&0x1F))					/* Mask	Peripheral #x interrupt */
#define	SIC_UNMASK(x)	(0xFFFFFFFF ^ (1 << ((x)&0x1F)))	/* Unmask Peripheral #x	interrupt */
#endif /* _MISRA_RULES */

/* SIC_IWRx Masks											 */
#define	IWR_DISABLE_ALL	0x00000000					/* Wakeup Disable all peripherals */
#define	IWR_ENABLE_ALL	0xFFFFFFFF					/* Wakeup Enable all peripherals */
#ifdef _MISRA_RULES
#define	IWR_ENABLE(x)	(1 << ((x)&0x1Fu))					/* Wakeup Enable Peripheral #x */
#define	IWR_DISABLE(x)	(0xFFFFFFFFu ^ (1 << ((x)&0x1Fu)))	/* Wakeup Disable Peripheral #x */
#else
#define	IWR_ENABLE(x)	(1 << ((x)&0x1F))					/* Wakeup Enable Peripheral #x */
#define	IWR_DISABLE(x)	(0xFFFFFFFF ^ (1 << ((x)&0x1F)))	/* Wakeup Disable Peripheral #x */
#endif /* _MISRA_RULES */


/* ***************************** UART CONTROLLER MASKS ********************** */
/* UARTx_LCR Register */
#ifdef _MISRA_RULES
#define	WLS(x)		(((x)-5u) & 0x03u)	/* Word	Length Select */
#else
#define	WLS(x)		(((x)-5) & 0x03)	/* Word	Length Select */
#endif /* _MISRA_RULES */
#define	STB			0x04				/* Stop	Bits */
#define	PEN			0x08				/* Parity Enable */
#define	EPS			0x10				/* Even	Parity Select */
#define	STP			0x20				/* Stick Parity */
#define	SB			0x40				/* Set Break */
#define	DLAB		0x80				/* Divisor Latch Access */

#define	DLAB_P		0x07
#define	SB_P		0x06
#define	STP_P		0x05
#define	EPS_P		0x04
#define	PEN_P		0x03
#define	STB_P		0x02
#define	WLS_P1		0x01
#define	WLS_P0		0x00

/* UARTx_MCR Register */
#define	LOOP_ENA	0x10	/* Loopback Mode Enable */
#define	LOOP_ENA_P	0x04
/* Deprecated UARTx_MCR	Mask			 */

/* UARTx_LSR Register */
#define	DR			0x01	/* Data	Ready */
#define	OE			0x02	/* Overrun Error */
#define	PE			0x04	/* Parity Error */
#define	FE			0x08	/* Framing Error */
#define	BI			0x10	/* Break Interrupt */
#define	THRE		0x20	/* THR Empty */
#define	TEMT		0x40	/* TSR and UART_THR Empty */

#define	TEMP_P		0x06
#define	THRE_P		0x05
#define	BI_P		0x04
#define	FE_P		0x03
#define	PE_P		0x02
#define	OE_P		0x01
#define	DR_P		0x00

/* UARTx_IER Register */
#define	ERBFI		0x01		/* Enable Receive Buffer Full Interrupt */
#define	ETBEI		0x02		/* Enable Transmit Buffer Empty	Interrupt */
#define	ELSI		0x04		/* Enable RX Status Interrupt */

#define	ELSI_P		0x02
#define	ETBEI_P		0x01
#define	ERBFI_P		0x00

/* UARTx_IIR Register */
#define	NINT		0x01
#define	STATUS_P1	0x02
#define	STATUS_P0	0x01
#define	NINT_P		0x00

/* UARTx_GCTL Register */
#define	UCEN		0x01		/* Enable UARTx	Clocks */
#define	IREN		0x02		/* Enable IrDA Mode */
#define	TPOLC		0x04		/* IrDA	TX Polarity Change */
#define	RPOLC		0x08		/* IrDA	RX Polarity Change */
#define	FPE			0x10		/* Force Parity	Error On Transmit */
#define	FFE			0x20		/* Force Framing Error On Transmit */

#define	FFE_P		0x05
#define	FPE_P		0x04
#define	RPOLC_P		0x03
#define	TPOLC_P		0x02
#define	IREN_P		0x01
#define	UCEN_P		0x00


/*  *********  PARALLEL	PERIPHERAL INTERFACE (PPI) MASKS ****************   */
/*  PPI_CONTROL	Masks	      */
#define	PORT_EN		0x0001	/* PPI Port Enable  */
#define	PORT_DIR	0x0002	/* PPI Port Direction	    */
#define	XFR_TYPE	0x000C	/* PPI Transfer	Type  */
#define	PORT_CFG	0x0030	/* PPI Port Configuration */
#define	FLD_SEL		0x0040	/* PPI Active Field Select */
#define	PACK_EN		0x0080	/* PPI Packing Mode */
/* previous versions of	defBF539.h erroneously included	DMA32 (PPI 32-bit DMA Enable) */
#define	SKIP_EN		0x0200	/* PPI Skip Element Enable */
#define	SKIP_EO		0x0400	/* PPI Skip Even/Odd Elements */
#define	DLENGTH		0x3800	/* PPI Data Length  */
#define	DLEN_8		0x0	     /*	PPI Data Length	mask for DLEN=8 */
#define	DLEN_10		0x0800		/* Data	Length = 10 Bits */
#define	DLEN_11		0x1000		/* Data	Length = 11 Bits */
#define	DLEN_12		0x1800		/* Data	Length = 12 Bits */
#define	DLEN_13		0x2000		/* Data	Length = 13 Bits */
#define	DLEN_14		0x2800		/* Data	Length = 14 Bits */
#define	DLEN_15		0x3000		/* Data	Length = 15 Bits */
#define	DLEN_16		0x3800		/* Data	Length = 16 Bits */
#ifdef _MISRA_RULES
#define	DLEN(x)		((((x)-9u) & 0x07u) << 11)  /* PPI Data	Length (only works for x=10-->x=16) */
#else
#define	DLEN(x)		((((x)-9) & 0x07) << 11)  /* PPI Data Length (only works for x=10-->x=16) */
#endif /* _MISRA_RULES */
#define	POL			0xC000	/* PPI Signal Polarities       */
#define	POLC		0x4000		/* PPI Clock Polarity */
#define	POLS		0x8000		/* PPI Frame Sync Polarity */


/* PPI_STATUS Masks					     */
#define	FLD			0x0400	/* Field Indicator   */
#define	FT_ERR		0x0800	/* Frame Track Error */
#define	OVR			0x1000	/* FIFO	Overflow Error */
#define	UNDR		0x2000	/* FIFO	Underrun Error */
#define	ERR_DET		0x4000	/* Error Detected Indicator */
#define	ERR_NCOR	0x8000	/* Error Not Corrected Indicator */


/* **********  DMA CONTROLLER MASKS  ***********************/

/* DMAx_PERIPHERAL_MAP,	MDMA_yy_PERIPHERAL_MAP Masks */

#define	CTYPE			0x0040	/* DMA Channel Type Indicator */
#define	CTYPE_P			0x6		/* DMA Channel Type Indicator BIT POSITION */
#define	PCAP8			0x0080	/* DMA 8-bit Operation Indicator   */
#define	PCAP16			0x0100	/* DMA 16-bit Operation	Indicator */
#define	PCAP32			0x0200	/* DMA 32-bit Operation	Indicator */
#define	PCAPWR			0x0400	/* DMA Write Operation Indicator */
#define	PCAPRD			0x0800	/* DMA Read Operation Indicator */
#define	PMAP			0xF000	/* DMA Peripheral Map Field */

/* PMAP	Encodings For DMA Controller 0 */
#define	PMAP_PPI		0x0000	/* PMAP	PPI Port DMA */
#define	PMAP_SPORT0RX	0x1000	/* PMAP	SPORT0 Receive DMA */
#define	PMAP_SPORT0TX	0x2000	/* PMAP	SPORT0 Transmit	DMA */
#define	PMAP_SPORT1RX	0x3000	/* PMAP	SPORT1 Receive DMA */
#define	PMAP_SPORT1TX	0x4000	/* PMAP	SPORT1 Transmit	DMA */
#define	PMAP_SPI0		0x5000	/* PMAP	SPI DMA */
#define	PMAP_UART0RX		0x6000	/* PMAP	UART Receive DMA */
#define	PMAP_UART0TX		0x7000	/* PMAP	UART Transmit DMA */

/* PMAP	Encodings For DMA Controller 1 */
#define	PMAP_SPORT2RX	    0x0000  /* PMAP SPORT2 Receive DMA */
#define	PMAP_SPORT2TX	    0x1000  /* PMAP SPORT2 Transmit DMA */
#define	PMAP_SPORT3RX	    0x2000  /* PMAP SPORT3 Receive DMA */
#define	PMAP_SPORT3TX	    0x3000  /* PMAP SPORT3 Transmit DMA */
#define	PMAP_SPI1	    0x6000  /* PMAP SPI1 DMA */
#define	PMAP_SPI2	    0x7000  /* PMAP SPI2 DMA */
#define	PMAP_UART1RX	    0x8000  /* PMAP UART1 Receive DMA */
#define	PMAP_UART1TX	    0x9000  /* PMAP UART1 Transmit DMA */
#define	PMAP_UART2RX	    0xA000  /* PMAP UART2 Receive DMA */
#define	PMAP_UART2TX	    0xB000  /* PMAP UART2 Transmit DMA */


/*  *************  GENERAL PURPOSE TIMER MASKS	******************** */
/* PWM Timer bit definitions */
/* TIMER_ENABLE	Register */
#define	TIMEN0			0x0001	/* Enable Timer	0 */
#define	TIMEN1			0x0002	/* Enable Timer	1 */
#define	TIMEN2			0x0004	/* Enable Timer	2 */

#define	TIMEN0_P		0x00
#define	TIMEN1_P		0x01
#define	TIMEN2_P		0x02

/* TIMER_DISABLE Register */
#define	TIMDIS0			0x0001	/* Disable Timer 0 */
#define	TIMDIS1			0x0002	/* Disable Timer 1 */
#define	TIMDIS2			0x0004	/* Disable Timer 2 */

#define	TIMDIS0_P		0x00
#define	TIMDIS1_P		0x01
#define	TIMDIS2_P		0x02

/* TIMER_STATUS	Register */
#define	TIMIL0			0x0001	/* Timer 0 Interrupt */
#define	TIMIL1			0x0002	/* Timer 1 Interrupt */
#define	TIMIL2			0x0004	/* Timer 2 Interrupt */
#define	TOVF_ERR0		0x0010	/* Timer 0 Counter Overflow */
#define	TOVF_ERR1		0x0020	/* Timer 1 Counter Overflow */
#define	TOVF_ERR2		0x0040	/* Timer 2 Counter Overflow */
#define	TRUN0			0x1000	/* Timer 0 Slave Enable	Status */
#define	TRUN1			0x2000	/* Timer 1 Slave Enable	Status */
#define	TRUN2			0x4000	/* Timer 2 Slave Enable	Status */

#define	TIMIL0_P		0x00
#define	TIMIL1_P		0x01
#define	TIMIL2_P		0x02
#define	TOVF_ERR0_P		0x04
#define	TOVF_ERR1_P		0x05
#define	TOVF_ERR2_P		0x06
#define	TRUN0_P			0x0C
#define	TRUN1_P			0x0D
#define	TRUN2_P			0x0E

/* Alternate Deprecated	Macros Provided	For Backwards Code Compatibility */
#define	TOVL_ERR0		TOVF_ERR0
#define	TOVL_ERR1		TOVF_ERR1
#define	TOVL_ERR2		TOVF_ERR2
#define	TOVL_ERR0_P		TOVF_ERR0_P
#define	TOVL_ERR1_P	TOVF_ERR1_P
#define	TOVL_ERR2_P	TOVF_ERR2_P

/* TIMERx_CONFIG Registers */
#define	PWM_OUT			0x0001
#define	WDTH_CAP		0x0002
#define	EXT_CLK			0x0003
#define	PULSE_HI		0x0004
#define	PERIOD_CNT		0x0008
#define	IRQ_ENA			0x0010
#define	TIN_SEL			0x0020
#define	OUT_DIS			0x0040
#define	CLK_SEL			0x0080
#define	TOGGLE_HI		0x0100
#define	EMU_RUN			0x0200
#ifdef _MISRA_RULES
#define	ERR_TYP(x)		(((x) &	0x03u) << 14)
#else
#define	ERR_TYP(x)		(((x) &	0x03) << 14)
#endif /* _MISRA_RULES */

#define	TMODE_P0		0x00
#define	TMODE_P1		0x01
#define	PULSE_HI_P		0x02
#define	PERIOD_CNT_P	0x03
#define	IRQ_ENA_P		0x04
#define	TIN_SEL_P		0x05
#define	OUT_DIS_P		0x06
#define	CLK_SEL_P		0x07
#define	TOGGLE_HI_P		0x08
#define	EMU_RUN_P		0x09
#define	ERR_TYP_P0		0x0E
#define	ERR_TYP_P1		0x0F


/*/ ******************	 GENERAL-PURPOSE I/O  ********************* */
/*  Flag I/O (FIO_) Masks */
#define	PF0			0x0001
#define	PF1			0x0002
#define	PF2			0x0004
#define	PF3			0x0008
#define	PF4			0x0010
#define	PF5			0x0020
#define	PF6			0x0040
#define	PF7			0x0080
#define	PF8			0x0100
#define	PF9			0x0200
#define	PF10		0x0400
#define	PF11		0x0800
#define	PF12		0x1000
#define	PF13		0x2000
#define	PF14		0x4000
#define	PF15		0x8000

/*  PORT F BIT POSITIONS */
#define	PF0_P		0x0
#define	PF1_P		0x1
#define	PF2_P		0x2
#define	PF3_P		0x3
#define	PF4_P		0x4
#define	PF5_P		0x5
#define	PF6_P		0x6
#define	PF7_P		0x7
#define	PF8_P		0x8
#define	PF9_P		0x9
#define	PF10_P		0xA
#define	PF11_P		0xB
#define	PF12_P		0xC
#define	PF13_P		0xD
#define	PF14_P		0xE
#define	PF15_P		0xF


/*******************   GPIO MASKS  *********************/
/* Port	C Masks */
#define	PC0		0x0001
#define	PC1		0x0002
#define	PC4		0x0010
#define	PC5		0x0020
#define	PC6		0x0040
#define	PC7		0x0080
#define	PC8		0x0100
#define	PC9		0x0200
/* Port	C Bit Positions */
#define	PC0_P	0x0
#define	PC1_P	0x1
#define	PC4_P	0x4
#define	PC5_P	0x5
#define	PC6_P	0x6
#define	PC7_P	0x7
#define	PC8_P	0x8
#define	PC9_P	0x9

/* Port	D */
#define	PD0		0x0001
#define	PD1		0x0002
#define	PD2		0x0004
#define	PD3		0x0008
#define	PD4		0x0010
#define	PD5		0x0020
#define	PD6		0x0040
#define	PD7		0x0080
#define	PD8		0x0100
#define	PD9		0x0200
#define	PD10	0x0400
#define	PD11	0x0800
#define	PD12	0x1000
#define	PD13	0x2000
#define	PD14	0x4000
#define	PD15	0x8000
/* Port	D Bit Positions */
#define	PD0_P	0x0
#define	PD1_P	0x1
#define	PD2_P	0x2
#define	PD3_P	0x3
#define	PD4_P	0x4
#define	PD5_P	0x5
#define	PD6_P	0x6
#define	PD7_P	0x7
#define	PD8_P	0x8
#define	PD9_P	0x9
#define	PD10_P	0xA
#define	PD11_P	0xB
#define	PD12_P	0xC
#define	PD13_P	0xD
#define	PD14_P	0xE
#define	PD15_P	0xF

/* Port	E */
#define	PE0		0x0001
#define	PE1		0x0002
#define	PE2		0x0004
#define	PE3		0x0008
#define	PE4		0x0010
#define	PE5		0x0020
#define	PE6		0x0040
#define	PE7		0x0080
#define	PE8		0x0100
#define	PE9		0x0200
#define	PE10	0x0400
#define	PE11	0x0800
#define	PE12	0x1000
#define	PE13	0x2000
#define	PE14	0x4000
#define	PE15	0x8000
/* Port	E Bit Positions */
#define	PE0_P	0x0
#define	PE1_P	0x1
#define	PE2_P	0x2
#define	PE3_P	0x3
#define	PE4_P	0x4
#define	PE5_P	0x5
#define	PE6_P	0x6
#define	PE7_P	0x7
#define	PE8_P	0x8
#define	PE9_P	0x9
#define	PE10_P	0xA
#define	PE11_P	0xB
#define	PE12_P	0xC
#define	PE13_P	0xD
#define	PE14_P	0xE
#define	PE15_P	0xF

/* *********************  ASYNCHRONOUS MEMORY CONTROLLER MASKS	************* */
/* EBIU_AMGCTL Masks */
#define	AMCKEN		0x0001	/* Enable CLKOUT */
#define	AMBEN_NONE	0x0000	/* All Banks Disabled */
#define	AMBEN_B0	0x0002	/* Enable Asynchronous Memory Bank 0 only */
#define	AMBEN_B0_B1	0x0004	/* Enable Asynchronous Memory Banks 0 &	1 only */
#define	AMBEN_B0_B1_B2	0x0006	/* Enable Asynchronous Memory Banks 0, 1, and 2 */
#define	AMBEN_ALL	0x0008	/* Enable Asynchronous Memory Banks (all) 0, 1,	2, and 3 */
#define	CDPRIO		0x0100	/* DMA has priority over core for external accesses */

/* EBIU_AMGCTL Bit Positions */
#define	AMCKEN_P		0x0000	/* Enable CLKOUT */
#define	AMBEN_P0		0x0001	/* Asynchronous	Memory Enable, 000 - banks 0-3 disabled, 001 - Bank 0 enabled */
#define	AMBEN_P1		0x0002	/* Asynchronous	Memory Enable, 010 - banks 0&1 enabled,	 011 - banks 0-3 enabled */
#define	AMBEN_P2		0x0003	/* Asynchronous	Memory Enable, 1xx - All banks (bank 0,	1, 2, and 3) enabled */

/* EBIU_AMBCTL0	Masks */
#define	B0RDYEN			0x00000001  /* Bank 0 RDY Enable, 0=disable, 1=enable */
#define	B0RDYPOL		0x00000002  /* Bank 0 RDY Active high, 0=active	low, 1=active high */
#define	B0TT_1			0x00000004  /* Bank 0 Transition Time from Read	to Write = 1 cycle */
#define	B0TT_2			0x00000008  /* Bank 0 Transition Time from Read	to Write = 2 cycles */
#define	B0TT_3			0x0000000C  /* Bank 0 Transition Time from Read	to Write = 3 cycles */
#define	B0TT_4			0x00000000  /* Bank 0 Transition Time from Read	to Write = 4 cycles */
#define	B0ST_1			0x00000010  /* Bank 0 Setup Time from AOE asserted to Read/Write asserted=1 cycle */
#define	B0ST_2			0x00000020  /* Bank 0 Setup Time from AOE asserted to Read/Write asserted=2 cycles */
#define	B0ST_3			0x00000030  /* Bank 0 Setup Time from AOE asserted to Read/Write asserted=3 cycles */
#define	B0ST_4			0x00000000  /* Bank 0 Setup Time from AOE asserted to Read/Write asserted=4 cycles */
#define	B0HT_1			0x00000040  /* Bank 0 Hold Time	from Read/Write	deasserted to AOE deasserted = 1 cycle */
#define	B0HT_2			0x00000080  /* Bank 0 Hold Time	from Read/Write	deasserted to AOE deasserted = 2 cycles */
#define	B0HT_3			0x000000C0  /* Bank 0 Hold Time	from Read/Write	deasserted to AOE deasserted = 3 cycles */
#define	B0HT_0			0x00000000  /* Bank 0 Hold Time	from Read/Write	deasserted to AOE deasserted = 0 cycles */
#define	B0RAT_1			0x00000100  /* Bank 0 Read Access Time = 1 cycle */
#define	B0RAT_2			0x00000200  /* Bank 0 Read Access Time = 2 cycles */
#define	B0RAT_3			0x00000300  /* Bank 0 Read Access Time = 3 cycles */
#define	B0RAT_4			0x00000400  /* Bank 0 Read Access Time = 4 cycles */
#define	B0RAT_5			0x00000500  /* Bank 0 Read Access Time = 5 cycles */
#define	B0RAT_6			0x00000600  /* Bank 0 Read Access Time = 6 cycles */
#define	B0RAT_7			0x00000700  /* Bank 0 Read Access Time = 7 cycles */
#define	B0RAT_8			0x00000800  /* Bank 0 Read Access Time = 8 cycles */
#define	B0RAT_9			0x00000900  /* Bank 0 Read Access Time = 9 cycles */
#define	B0RAT_10		0x00000A00  /* Bank 0 Read Access Time = 10 cycles */
#define	B0RAT_11		0x00000B00  /* Bank 0 Read Access Time = 11 cycles */
#define	B0RAT_12		0x00000C00  /* Bank 0 Read Access Time = 12 cycles */
#define	B0RAT_13		0x00000D00  /* Bank 0 Read Access Time = 13 cycles */
#define	B0RAT_14		0x00000E00  /* Bank 0 Read Access Time = 14 cycles */
#define	B0RAT_15		0x00000F00  /* Bank 0 Read Access Time = 15 cycles */
#define	B0WAT_1			0x00001000  /* Bank 0 Write Access Time	= 1 cycle */
#define	B0WAT_2			0x00002000  /* Bank 0 Write Access Time	= 2 cycles */
#define	B0WAT_3			0x00003000  /* Bank 0 Write Access Time	= 3 cycles */
#define	B0WAT_4			0x00004000  /* Bank 0 Write Access Time	= 4 cycles */
#define	B0WAT_5			0x00005000  /* Bank 0 Write Access Time	= 5 cycles */
#define	B0WAT_6			0x00006000  /* Bank 0 Write Access Time	= 6 cycles */
#define	B0WAT_7			0x00007000  /* Bank 0 Write Access Time	= 7 cycles */
#define	B0WAT_8			0x00008000  /* Bank 0 Write Access Time	= 8 cycles */
#define	B0WAT_9			0x00009000  /* Bank 0 Write Access Time	= 9 cycles */
#define	B0WAT_10		0x0000A000  /* Bank 0 Write Access Time	= 10 cycles */
#define	B0WAT_11		0x0000B000  /* Bank 0 Write Access Time	= 11 cycles */
#define	B0WAT_12		0x0000C000  /* Bank 0 Write Access Time	= 12 cycles */
#define	B0WAT_13		0x0000D000  /* Bank 0 Write Access Time	= 13 cycles */
#define	B0WAT_14		0x0000E000  /* Bank 0 Write Access Time	= 14 cycles */
#define	B0WAT_15		0x0000F000  /* Bank 0 Write Access Time	= 15 cycles */
#define	B1RDYEN			0x00010000  /* Bank 1 RDY enable, 0=disable, 1=enable */
#define	B1RDYPOL		0x00020000  /* Bank 1 RDY Active high, 0=active	low, 1=active high */
#define	B1TT_1			0x00040000  /* Bank 1 Transition Time from Read	to Write = 1 cycle */
#define	B1TT_2			0x00080000  /* Bank 1 Transition Time from Read	to Write = 2 cycles */
#define	B1TT_3			0x000C0000  /* Bank 1 Transition Time from Read	to Write = 3 cycles */
#define	B1TT_4			0x00000000  /* Bank 1 Transition Time from Read	to Write = 4 cycles */
#define	B1ST_1			0x00100000  /* Bank 1 Setup Time from AOE asserted to Read or Write asserted = 1 cycle */
#define	B1ST_2			0x00200000  /* Bank 1 Setup Time from AOE asserted to Read or Write asserted = 2 cycles */
#define	B1ST_3			0x00300000  /* Bank 1 Setup Time from AOE asserted to Read or Write asserted = 3 cycles */
#define	B1ST_4			0x00000000  /* Bank 1 Setup Time from AOE asserted to Read or Write asserted = 4 cycles */
#define	B1HT_1			0x00400000  /* Bank 1 Hold Time	from Read or Write deasserted to AOE deasserted	= 1 cycle */
#define	B1HT_2			0x00800000  /* Bank 1 Hold Time	from Read or Write deasserted to AOE deasserted	= 2 cycles */
#define	B1HT_3			0x00C00000  /* Bank 1 Hold Time	from Read or Write deasserted to AOE deasserted	= 3 cycles */
#define	B1HT_0			0x00000000  /* Bank 1 Hold Time	from Read or Write deasserted to AOE deasserted	= 0 cycles */
#define	B1RAT_1			0x01000000  /* Bank 1 Read Access Time = 1 cycle */
#define	B1RAT_2			0x02000000  /* Bank 1 Read Access Time = 2 cycles */
#define	B1RAT_3			0x03000000  /* Bank 1 Read Access Time = 3 cycles */
#define	B1RAT_4			0x04000000  /* Bank 1 Read Access Time = 4 cycles */
#define	B1RAT_5			0x05000000  /* Bank 1 Read Access Time = 5 cycles */
#define	B1RAT_6			0x06000000  /* Bank 1 Read Access Time = 6 cycles */
#define	B1RAT_7			0x07000000  /* Bank 1 Read Access Time = 7 cycles */
#define	B1RAT_8			0x08000000  /* Bank 1 Read Access Time = 8 cycles */
#define	B1RAT_9			0x09000000  /* Bank 1 Read Access Time = 9 cycles */
#define	B1RAT_10		0x0A000000  /* Bank 1 Read Access Time = 10 cycles */
#define	B1RAT_11		0x0B000000  /* Bank 1 Read Access Time = 11 cycles */
#define	B1RAT_12		0x0C000000  /* Bank 1 Read Access Time = 12 cycles */
#define	B1RAT_13		0x0D000000  /* Bank 1 Read Access Time = 13 cycles */
#define	B1RAT_14		0x0E000000  /* Bank 1 Read Access Time = 14 cycles */
#define	B1RAT_15		0x0F000000  /* Bank 1 Read Access Time = 15 cycles */
#define	B1WAT_1			0x10000000 /* Bank 1 Write Access Time = 1 cycle */
#define	B1WAT_2			0x20000000  /* Bank 1 Write Access Time	= 2 cycles */
#define	B1WAT_3			0x30000000  /* Bank 1 Write Access Time	= 3 cycles */
#define	B1WAT_4			0x40000000  /* Bank 1 Write Access Time	= 4 cycles */
#define	B1WAT_5			0x50000000  /* Bank 1 Write Access Time	= 5 cycles */
#define	B1WAT_6			0x60000000  /* Bank 1 Write Access Time	= 6 cycles */
#define	B1WAT_7			0x70000000  /* Bank 1 Write Access Time	= 7 cycles */
#define	B1WAT_8			0x80000000  /* Bank 1 Write Access Time	= 8 cycles */
#define	B1WAT_9			0x90000000  /* Bank 1 Write Access Time	= 9 cycles */
#define	B1WAT_10		0xA0000000  /* Bank 1 Write Access Time	= 10 cycles */
#define	B1WAT_11		0xB0000000  /* Bank 1 Write Access Time	= 11 cycles */
#define	B1WAT_12		0xC0000000  /* Bank 1 Write Access Time	= 12 cycles */
#define	B1WAT_13		0xD0000000  /* Bank 1 Write Access Time	= 13 cycles */
#define	B1WAT_14		0xE0000000  /* Bank 1 Write Access Time	= 14 cycles */
#define	B1WAT_15		0xF0000000  /* Bank 1 Write Access Time	= 15 cycles */

/* EBIU_AMBCTL1	Masks */
#define	B2RDYEN			0x00000001  /* Bank 2 RDY Enable, 0=disable, 1=enable */
#define	B2RDYPOL		0x00000002  /* Bank 2 RDY Active high, 0=active	low, 1=active high */
#define	B2TT_1			0x00000004  /* Bank 2 Transition Time from Read	to Write = 1 cycle */
#define	B2TT_2			0x00000008  /* Bank 2 Transition Time from Read	to Write = 2 cycles */
#define	B2TT_3			0x0000000C  /* Bank 2 Transition Time from Read	to Write = 3 cycles */
#define	B2TT_4			0x00000000  /* Bank 2 Transition Time from Read	to Write = 4 cycles */
#define	B2ST_1			0x00000010  /* Bank 2 Setup Time from AOE asserted to Read or Write asserted = 1 cycle */
#define	B2ST_2			0x00000020  /* Bank 2 Setup Time from AOE asserted to Read or Write asserted = 2 cycles */
#define	B2ST_3			0x00000030  /* Bank 2 Setup Time from AOE asserted to Read or Write asserted = 3 cycles */
#define	B2ST_4			0x00000000  /* Bank 2 Setup Time from AOE asserted to Read or Write asserted = 4 cycles */
#define	B2HT_1			0x00000040  /* Bank 2 Hold Time	from Read or Write deasserted to AOE deasserted	= 1 cycle */
#define	B2HT_2			0x00000080  /* Bank 2 Hold Time	from Read or Write deasserted to AOE deasserted	= 2 cycles */
#define	B2HT_3			0x000000C0  /* Bank 2 Hold Time	from Read or Write deasserted to AOE deasserted	= 3 cycles */
#define	B2HT_0			0x00000000  /* Bank 2 Hold Time	from Read or Write deasserted to AOE deasserted	= 0 cycles */
#define	B2RAT_1			0x00000100  /* Bank 2 Read Access Time = 1 cycle */
#define	B2RAT_2			0x00000200  /* Bank 2 Read Access Time = 2 cycles */
#define	B2RAT_3			0x00000300  /* Bank 2 Read Access Time = 3 cycles */
#define	B2RAT_4			0x00000400  /* Bank 2 Read Access Time = 4 cycles */
#define	B2RAT_5			0x00000500  /* Bank 2 Read Access Time = 5 cycles */
#define	B2RAT_6			0x00000600  /* Bank 2 Read Access Time = 6 cycles */
#define	B2RAT_7			0x00000700  /* Bank 2 Read Access Time = 7 cycles */
#define	B2RAT_8			0x00000800  /* Bank 2 Read Access Time = 8 cycles */
#define	B2RAT_9			0x00000900  /* Bank 2 Read Access Time = 9 cycles */
#define	B2RAT_10		0x00000A00  /* Bank 2 Read Access Time = 10 cycles */
#define	B2RAT_11		0x00000B00  /* Bank 2 Read Access Time = 11 cycles */
#define	B2RAT_12		0x00000C00  /* Bank 2 Read Access Time = 12 cycles */
#define	B2RAT_13		0x00000D00  /* Bank 2 Read Access Time = 13 cycles */
#define	B2RAT_14		0x00000E00  /* Bank 2 Read Access Time = 14 cycles */
#define	B2RAT_15		0x00000F00  /* Bank 2 Read Access Time = 15 cycles */
#define	B2WAT_1			0x00001000  /* Bank 2 Write Access Time	= 1 cycle */
#define	B2WAT_2			0x00002000  /* Bank 2 Write Access Time	= 2 cycles */
#define	B2WAT_3			0x00003000  /* Bank 2 Write Access Time	= 3 cycles */
#define	B2WAT_4			0x00004000  /* Bank 2 Write Access Time	= 4 cycles */
#define	B2WAT_5			0x00005000  /* Bank 2 Write Access Time	= 5 cycles */
#define	B2WAT_6			0x00006000  /* Bank 2 Write Access Time	= 6 cycles */
#define	B2WAT_7			0x00007000  /* Bank 2 Write Access Time	= 7 cycles */
#define	B2WAT_8			0x00008000  /* Bank 2 Write Access Time	= 8 cycles */
#define	B2WAT_9			0x00009000  /* Bank 2 Write Access Time	= 9 cycles */
#define	B2WAT_10		0x0000A000  /* Bank 2 Write Access Time	= 10 cycles */
#define	B2WAT_11		0x0000B000  /* Bank 2 Write Access Time	= 11 cycles */
#define	B2WAT_12		0x0000C000  /* Bank 2 Write Access Time	= 12 cycles */
#define	B2WAT_13		0x0000D000  /* Bank 2 Write Access Time	= 13 cycles */
#define	B2WAT_14		0x0000E000  /* Bank 2 Write Access Time	= 14 cycles */
#define	B2WAT_15		0x0000F000  /* Bank 2 Write Access Time	= 15 cycles */
#define	B3RDYEN			0x00010000  /* Bank 3 RDY enable, 0=disable, 1=enable */
#define	B3RDYPOL		0x00020000  /* Bank 3 RDY Active high, 0=active	low, 1=active high */
#define	B3TT_1			0x00040000  /* Bank 3 Transition Time from Read	to Write = 1 cycle */
#define	B3TT_2			0x00080000  /* Bank 3 Transition Time from Read	to Write = 2 cycles */
#define	B3TT_3			0x000C0000  /* Bank 3 Transition Time from Read	to Write = 3 cycles */
#define	B3TT_4			0x00000000  /* Bank 3 Transition Time from Read	to Write = 4 cycles */
#define	B3ST_1			0x00100000  /* Bank 3 Setup Time from AOE asserted to Read or Write asserted = 1 cycle */
#define	B3ST_2			0x00200000  /* Bank 3 Setup Time from AOE asserted to Read or Write asserted = 2 cycles */
#define	B3ST_3			0x00300000  /* Bank 3 Setup Time from AOE asserted to Read or Write asserted = 3 cycles */
#define	B3ST_4			0x00000000  /* Bank 3 Setup Time from AOE asserted to Read or Write asserted = 4 cycles */
#define	B3HT_1			0x00400000  /* Bank 3 Hold Time	from Read or Write deasserted to AOE deasserted	= 1 cycle */
#define	B3HT_2			0x00800000  /* Bank 3 Hold Time	from Read or Write deasserted to AOE deasserted	= 2 cycles */
#define	B3HT_3			0x00C00000  /* Bank 3 Hold Time	from Read or Write deasserted to AOE deasserted	= 3 cycles */
#define	B3HT_0			0x00000000  /* Bank 3 Hold Time	from Read or Write deasserted to AOE deasserted	= 0 cycles */
#define	B3RAT_1			0x01000000 /* Bank 3 Read Access Time =	1 cycle */
#define	B3RAT_2			0x02000000  /* Bank 3 Read Access Time = 2 cycles */
#define	B3RAT_3			0x03000000  /* Bank 3 Read Access Time = 3 cycles */
#define	B3RAT_4			0x04000000  /* Bank 3 Read Access Time = 4 cycles */
#define	B3RAT_5			0x05000000  /* Bank 3 Read Access Time = 5 cycles */
#define	B3RAT_6			0x06000000  /* Bank 3 Read Access Time = 6 cycles */
#define	B3RAT_7			0x07000000  /* Bank 3 Read Access Time = 7 cycles */
#define	B3RAT_8			0x08000000  /* Bank 3 Read Access Time = 8 cycles */
#define	B3RAT_9			0x09000000  /* Bank 3 Read Access Time = 9 cycles */
#define	B3RAT_10		0x0A000000  /* Bank 3 Read Access Time = 10 cycles */
#define	B3RAT_11		0x0B000000  /* Bank 3 Read Access Time = 11 cycles */
#define	B3RAT_12		0x0C000000  /* Bank 3 Read Access Time = 12 cycles */
#define	B3RAT_13		0x0D000000  /* Bank 3 Read Access Time = 13 cycles */
#define	B3RAT_14		0x0E000000  /* Bank 3 Read Access Time = 14 cycles */
#define	B3RAT_15		0x0F000000  /* Bank 3 Read Access Time = 15 cycles */
#define	B3WAT_1			0x10000000 /* Bank 3 Write Access Time = 1 cycle */
#define	B3WAT_2			0x20000000  /* Bank 3 Write Access Time	= 2 cycles */
#define	B3WAT_3			0x30000000  /* Bank 3 Write Access Time	= 3 cycles */
#define	B3WAT_4			0x40000000  /* Bank 3 Write Access Time	= 4 cycles */
#define	B3WAT_5			0x50000000  /* Bank 3 Write Access Time	= 5 cycles */
#define	B3WAT_6			0x60000000  /* Bank 3 Write Access Time	= 6 cycles */
#define	B3WAT_7			0x70000000  /* Bank 3 Write Access Time	= 7 cycles */
#define	B3WAT_8			0x80000000  /* Bank 3 Write Access Time	= 8 cycles */
#define	B3WAT_9			0x90000000  /* Bank 3 Write Access Time	= 9 cycles */
#define	B3WAT_10		0xA0000000  /* Bank 3 Write Access Time	= 10 cycles */
#define	B3WAT_11		0xB0000000  /* Bank 3 Write Access Time	= 11 cycles */
#define	B3WAT_12		0xC0000000  /* Bank 3 Write Access Time	= 12 cycles */
#define	B3WAT_13		0xD0000000  /* Bank 3 Write Access Time	= 13 cycles */
#define	B3WAT_14		0xE0000000  /* Bank 3 Write Access Time	= 14 cycles */
#define	B3WAT_15		0xF0000000  /* Bank 3 Write Access Time	= 15 cycles */

/* **********************  SDRAM CONTROLLER MASKS  *************************** */
/* EBIU_SDGCTL Masks */
#define	SCTLE			0x00000001 /* Enable SCLK[0], /SRAS, /SCAS, /SWE, SDQM[3:0] */
#define	CL_2			0x00000008 /* SDRAM CAS	latency	= 2 cycles */
#define	CL_3			0x0000000C /* SDRAM CAS	latency	= 3 cycles */
#define	PFE				0x00000010 /* Enable SDRAM prefetch */
#define	PFP				0x00000020 /* Prefetch has priority over AMC requests */
#define	PASR_ALL		0x00000000	/* All 4 SDRAM Banks Refreshed In Self-Refresh */
#define	PASR_B0_B1		0x00000010	/* SDRAM Banks 0 and 1 Are Refreshed In	Self-Refresh */
#define	PASR_B0			0x00000020	/* Only	SDRAM Bank 0 Is	Refreshed In Self-Refresh */
#define	TRAS_1			0x00000040 /* SDRAM tRAS = 1 cycle */
#define	TRAS_2			0x00000080 /* SDRAM tRAS = 2 cycles */
#define	TRAS_3			0x000000C0 /* SDRAM tRAS = 3 cycles */
#define	TRAS_4			0x00000100 /* SDRAM tRAS = 4 cycles */
#define	TRAS_5			0x00000140 /* SDRAM tRAS = 5 cycles */
#define	TRAS_6			0x00000180 /* SDRAM tRAS = 6 cycles */
#define	TRAS_7			0x000001C0 /* SDRAM tRAS = 7 cycles */
#define	TRAS_8			0x00000200 /* SDRAM tRAS = 8 cycles */
#define	TRAS_9			0x00000240 /* SDRAM tRAS = 9 cycles */
#define	TRAS_10			0x00000280 /* SDRAM tRAS = 10 cycles */
#define	TRAS_11			0x000002C0 /* SDRAM tRAS = 11 cycles */
#define	TRAS_12			0x00000300 /* SDRAM tRAS = 12 cycles */
#define	TRAS_13			0x00000340 /* SDRAM tRAS = 13 cycles */
#define	TRAS_14			0x00000380 /* SDRAM tRAS = 14 cycles */
#define	TRAS_15			0x000003C0 /* SDRAM tRAS = 15 cycles */
#define	TRP_1			0x00000800 /* SDRAM tRP	= 1 cycle */
#define	TRP_2			0x00001000 /* SDRAM tRP	= 2 cycles */
#define	TRP_3			0x00001800 /* SDRAM tRP	= 3 cycles */
#define	TRP_4			0x00002000 /* SDRAM tRP	= 4 cycles */
#define	TRP_5			0x00002800 /* SDRAM tRP	= 5 cycles */
#define	TRP_6			0x00003000 /* SDRAM tRP	= 6 cycles */
#define	TRP_7			0x00003800 /* SDRAM tRP	= 7 cycles */
#define	TRCD_1			0x00008000 /* SDRAM tRCD = 1 cycle */
#define	TRCD_2			0x00010000 /* SDRAM tRCD = 2 cycles */
#define	TRCD_3			0x00018000 /* SDRAM tRCD = 3 cycles */
#define	TRCD_4			0x00020000 /* SDRAM tRCD = 4 cycles */
#define	TRCD_5			0x00028000 /* SDRAM tRCD = 5 cycles */
#define	TRCD_6			0x00030000 /* SDRAM tRCD = 6 cycles */
#define	TRCD_7			0x00038000 /* SDRAM tRCD = 7 cycles */
#define	TWR_1			0x00080000 /* SDRAM tWR	= 1 cycle */
#define	TWR_2			0x00100000 /* SDRAM tWR	= 2 cycles */
#define	TWR_3			0x00180000 /* SDRAM tWR	= 3 cycles */
#define	PUPSD			0x00200000 /*Power-up start delay */
#define	PSM				0x00400000 /* SDRAM power-up sequence =	Precharge, mode	register set, 8	CBR refresh cycles */
#define	PSS				0x00800000 /* enable SDRAM power-up sequence on	next SDRAM access */
#define	SRFS			0x01000000 /* Start SDRAM self-refresh mode */
#define	EBUFE			0x02000000 /* Enable external buffering	timing */
#define	FBBRW			0x04000000 /* Fast back-to-back	read write enable */
#define	EMREN			0x10000000 /* Extended mode register enable */
#define	TCSR			0x20000000 /* Temp compensated self refresh value 85 deg C */
#define	CDDBG			0x40000000 /* Tristate SDRAM controls during bus grant */

/* EBIU_SDBCTL Masks */
#define	EBE				0x00000001 /* Enable SDRAM external bank */
#define	EBSZ_16			0x00000000 /* SDRAM external bank size = 16MB */
#define	EBSZ_32			0x00000002 /* SDRAM external bank size = 32MB */
#define	EBSZ_64			0x00000004 /* SDRAM external bank size = 64MB */
#define	EBSZ_128		0x00000006 /* SDRAM external bank size = 128MB */
#define	EBSZ_256		0x00000008 /* SDRAM External Bank Size = 256MB */
#define	EBSZ_512		0x0000000A /* SDRAM External Bank Size = 512MB */
#define	EBCAW_8			0x00000000 /* SDRAM external bank column address width = 8 bits */
#define	EBCAW_9			0x00000010 /* SDRAM external bank column address width = 9 bits */
#define	EBCAW_10		0x00000020 /* SDRAM external bank column address width = 9 bits */
#define	EBCAW_11		0x00000030 /* SDRAM external bank column address width = 9 bits */

/* EBIU_SDSTAT Masks */
#define	SDCI			0x00000001 /* SDRAM controller is idle */
#define	SDSRA			0x00000002 /* SDRAM SDRAM self refresh is active */
#define	SDPUA			0x00000004 /* SDRAM power up active  */
#define	SDRS			0x00000008 /* SDRAM is in reset	state */
#define	SDEASE			0x00000010 /* SDRAM EAB	sticky error status - W1C */
#define	BGSTAT			0x00000020 /* Bus granted */


/*  ********************  TWO-WIRE INTERFACE (TWIx) MASKS  ***********************/
/* TWIx_CLKDIV Macros (Use: *pTWIx_CLKDIV = CLKLOW(x)|CLKHI(y);	 ) */
#ifdef _MISRA_RULES
#define	CLKLOW(x)	((x) & 0xFFu)		/* Periods Clock Is Held Low */
#define	CLKHI(y)	(((y)&0xFFu)<<0x8)	/* Periods Before New Clock Low */
#else
#define	CLKLOW(x)	((x) & 0xFF)		/* Periods Clock Is Held Low */
#define	CLKHI(y)	(((y)&0xFF)<<0x8)	/* Periods Before New Clock Low */
#endif /* _MISRA_RULES */

/* TWIx_PRESCALE Masks								 */
#define	PRESCALE	0x007F		/* SCLKs Per Internal Time Reference (10MHz) */
#define	TWI_ENA		0x0080		/* TWI Enable		 */
#define	SCCB		0x0200		/* SCCB	Compatibility Enable */

/* TWIx_SLAVE_CTRL Masks								 */
#define	SEN			0x0001		/* Slave Enable		 */
#define	SADD_LEN	0x0002		/* Slave Address Length */
#define	STDVAL		0x0004		/* Slave Transmit Data Valid */
#define	NAK			0x0008		/* NAK/ACK* Generated At Conclusion Of Transfer */
#define	GEN			0x0010		/* General Call	Adrress	Matching Enabled */

/* TWIx_SLAVE_STAT Masks								 */
#define	SDIR		0x0001		/* Slave Transfer Direction (Transmit/Receive*) */
#define	GCALL		0x0002		/* General Call	Indicator */

/* TWIx_MASTER_CTRL Masks						 */
#define	MEN			0x0001		/* Master Mode Enable */
#define	MADD_LEN	0x0002		/* Master Address Length */
#define	MDIR		0x0004		/* Master Transmit Direction (RX/TX*) */
#define	FAST		0x0008		/* Use Fast Mode Timing	Specs */
#define	STOP		0x0010		/* Issue Stop Condition */
#define	RSTART		0x0020		/* Repeat Start	or Stop* At End	Of Transfer */
#define	DCNT		0x3FC0		/* Data	Bytes To Transfer */
#define	SDAOVR		0x4000		/* Serial Data Override */
#define	SCLOVR		0x8000		/* Serial Clock	Override */

/* TWIx_MASTER_STAT Masks							 */
#define	MPROG		0x0001		/* Master Transfer In Progress */
#define	LOSTARB		0x0002		/* Lost	Arbitration Indicator (Xfer Aborted) */
#define	ANAK		0x0004		/* Address Not Acknowledged */
#define	DNAK		0x0008		/* Data	Not Acknowledged */
#define	BUFRDERR	0x0010		/* Buffer Read Error */
#define	BUFWRERR	0x0020		/* Buffer Write	Error */
#define	SDASEN		0x0040		/* Serial Data Sense */
#define	SCLSEN		0x0080		/* Serial Clock	Sense */
#define	BUSBUSY		0x0100		/* Bus Busy Indicator */

/* TWIx_INT_SRC	and TWIx_INT_ENABLE Masks */
#define	SINIT		0x0001		/* Slave Transfer Initiated */
#define	SCOMP		0x0002		/* Slave Transfer Complete */
#define	SERR		0x0004		/* Slave Transfer Error */
#define	SOVF		0x0008		/* Slave Overflow */
#define	MCOMP		0x0010		/* Master Transfer Complete */
#define	MERR		0x0020		/* Master Transfer Error */
#define	XMTSERV		0x0040		/* Transmit FIFO Service */
#define	RCVSERV		0x0080		/* Receive FIFO	Service */

/* TWIx_FIFO_CTL Masks					 */
#define	XMTFLUSH	0x0001		/* Transmit Buffer Flush */
#define	RCVFLUSH	0x0002		/* Receive Buffer Flush */
#define	XMTINTLEN	0x0004		/* Transmit Buffer Interrupt Length */
#define	RCVINTLEN	0x0008		/* Receive Buffer Interrupt Length */

/* TWIx_FIFO_STAT Masks								 */
#define	XMTSTAT		0x0003		/* Transmit FIFO Status */
#define	XMT_EMPTY	0x0000		/*		Transmit FIFO Empty */
#define	XMT_HALF	0x0001		/*		Transmit FIFO Has 1 Byte To Write */
#define	XMT_FULL	0x0003		/*		Transmit FIFO Full (2 Bytes To Write) */

#define	RCVSTAT		0x000C		/* Receive FIFO	Status */
#define	RCV_EMPTY	0x0000		/*		Receive	FIFO Empty */
#define	RCV_HALF	0x0004		/*		Receive	FIFO Has 1 Byte	To Read */
#define	RCV_FULL	0x000C		/*		Receive	FIFO Full (2 Bytes To Read) */

#endif /* _DEF_BF539_H */

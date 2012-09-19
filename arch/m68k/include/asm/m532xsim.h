/****************************************************************************/

/*
 *	m532xsim.h -- ColdFire 5329 registers
 */

/****************************************************************************/
#ifndef	m532xsim_h
#define	m532xsim_h
/****************************************************************************/

#define	CPU_NAME		"COLDFIRE(m532x)"
#define	CPU_INSTR_PER_JIFFY	3
#define	MCF_BUSCLK		(MCF_CLK / 3)

#include <asm/m53xxacr.h>

#define MCF_REG32(x) (*(volatile unsigned long  *)(x))
#define MCF_REG16(x) (*(volatile unsigned short *)(x))
#define MCF_REG08(x) (*(volatile unsigned char  *)(x))

#define MCFINT_VECBASE      64
#define MCFINT_UART0        26          /* Interrupt number for UART0 */
#define MCFINT_UART1        27          /* Interrupt number for UART1 */
#define MCFINT_UART2        28          /* Interrupt number for UART2 */
#define MCFINT_QSPI         31          /* Interrupt number for QSPI */
#define MCFINT_FECRX0	    36		/* Interrupt number for FEC */
#define MCFINT_FECTX0	    40		/* Interrupt number for FEC */
#define MCFINT_FECENTC0	    42		/* Interrupt number for FEC */

#define MCF_IRQ_UART0       (MCFINT_VECBASE + MCFINT_UART0)
#define MCF_IRQ_UART1       (MCFINT_VECBASE + MCFINT_UART1)
#define MCF_IRQ_UART2       (MCFINT_VECBASE + MCFINT_UART2)

#define MCF_IRQ_FECRX0	    (MCFINT_VECBASE + MCFINT_FECRX0)
#define MCF_IRQ_FECTX0	    (MCFINT_VECBASE + MCFINT_FECTX0)
#define MCF_IRQ_FECENTC0    (MCFINT_VECBASE + MCFINT_FECENTC0)

#define	MCF_IRQ_QSPI	    (MCFINT_VECBASE + MCFINT_QSPI)

#define MCF_WTM_WCR	MCF_REG16(0xFC098000)

/*
 *	Define the 532x SIM register set addresses.
 */
#define	MCFSIM_IPRL		0xFC048004
#define	MCFSIM_IPRH		0xFC048000
#define	MCFSIM_IPR		MCFSIM_IPRL
#define	MCFSIM_IMRL		0xFC04800C
#define	MCFSIM_IMRH		0xFC048008
#define	MCFSIM_IMR		MCFSIM_IMRL
#define	MCFSIM_ICR0		0xFC048040	
#define	MCFSIM_ICR1		0xFC048041	
#define	MCFSIM_ICR2		0xFC048042	
#define	MCFSIM_ICR3		0xFC048043	
#define	MCFSIM_ICR4		0xFC048044	
#define	MCFSIM_ICR5		0xFC048045	
#define	MCFSIM_ICR6		0xFC048046	
#define	MCFSIM_ICR7		0xFC048047	
#define	MCFSIM_ICR8		0xFC048048	
#define	MCFSIM_ICR9		0xFC048049	
#define	MCFSIM_ICR10		0xFC04804A
#define	MCFSIM_ICR11		0xFC04804B

/*
 *	Some symbol defines for the above...
 */
#define	MCFSIM_SWDICR		MCFSIM_ICR0	/* Watchdog timer ICR */
#define	MCFSIM_TIMER1ICR	MCFSIM_ICR1	/* Timer 1 ICR */
#define	MCFSIM_TIMER2ICR	MCFSIM_ICR2	/* Timer 2 ICR */
#define	MCFSIM_UART1ICR		MCFSIM_ICR4	/* UART 1 ICR */
#define	MCFSIM_UART2ICR		MCFSIM_ICR5	/* UART 2 ICR */
#define	MCFSIM_DMA0ICR		MCFSIM_ICR6	/* DMA 0 ICR */
#define	MCFSIM_DMA1ICR		MCFSIM_ICR7	/* DMA 1 ICR */
#define	MCFSIM_DMA2ICR		MCFSIM_ICR8	/* DMA 2 ICR */
#define	MCFSIM_DMA3ICR		MCFSIM_ICR9	/* DMA 3 ICR */


#define	MCFINTC0_SIMR		0xFC04801C
#define	MCFINTC0_CIMR		0xFC04801D
#define	MCFINTC0_ICR0		0xFC048040
#define	MCFINTC1_SIMR		0xFC04C01C
#define	MCFINTC1_CIMR		0xFC04C01D
#define	MCFINTC1_ICR0		0xFC04C040
#define MCFINTC2_SIMR		(0)
#define MCFINTC2_CIMR		(0)
#define MCFINTC2_ICR0		(0)

#define MCFSIM_ICR_TIMER1	(0xFC048040+32)
#define MCFSIM_ICR_TIMER2	(0xFC048040+33)

/*
 *	Define system peripheral IRQ usage.
 */
#define	MCF_IRQ_TIMER		(64 + 32)	/* Timer0 */
#define	MCF_IRQ_PROFILER	(64 + 33)	/* Timer1 */

/*
 *  UART module.
 */
#define MCFUART_BASE0		0xFC060000	/* Base address of UART1 */
#define MCFUART_BASE1		0xFC064000	/* Base address of UART2 */
#define MCFUART_BASE2		0xFC068000	/* Base address of UART3 */

/*
 *  FEC module.
 */
#define	MCFFEC_BASE0		0xFC030000	/* Base address of FEC0 */
#define	MCFFEC_SIZE0		0x800		/* Size of FEC0 region */

/*
 *  QSPI module.
 */
#define	MCFQSPI_BASE		0xFC058000	/* Base address of QSPI */
#define	MCFQSPI_SIZE		0x40		/* Size of QSPI region */

#define	MCFQSPI_CS0		84
#define	MCFQSPI_CS1		85
#define	MCFQSPI_CS2		86

/*
 *  Timer module.
 */
#define MCFTIMER_BASE1		0xFC070000	/* Base address of TIMER1 */
#define MCFTIMER_BASE2		0xFC074000	/* Base address of TIMER2 */
#define MCFTIMER_BASE3		0xFC078000	/* Base address of TIMER3 */
#define MCFTIMER_BASE4		0xFC07C000	/* Base address of TIMER4 */

/*********************************************************************
 *
 * Reset Controller Module
 *
 *********************************************************************/

#define	MCF_RCR			0xFC0A0000
#define	MCF_RSR			0xFC0A0001

#define	MCF_RCR_SWRESET		0x80		/* Software reset bit */
#define	MCF_RCR_FRCSTOUT	0x40		/* Force external reset */


/*
 * Power Management
 */
#define MCFPM_WCR		0xfc040013
#define MCFPM_PPMSR0		0xfc04002c
#define MCFPM_PPMCR0		0xfc04002d
#define MCFPM_PPMSR1		0xfc04002e
#define MCFPM_PPMCR1		0xfc04002f
#define MCFPM_PPMHR0		0xfc040030
#define MCFPM_PPMLR0		0xfc040034
#define MCFPM_PPMHR1		0xfc040038
#define MCFPM_LPCR		0xec090007

/*********************************************************************
 *
 * Inter-IC (I2C) Module
 *
 *********************************************************************/

/* Read/Write access macros for general use */
#define MCF532x_I2C_I2ADR       (volatile u8 *) (0xFC058000) // Address 
#define MCF532x_I2C_I2FDR       (volatile u8 *) (0xFC058004) // Freq Divider
#define MCF532x_I2C_I2CR        (volatile u8 *) (0xFC058008) // Control
#define MCF532x_I2C_I2SR        (volatile u8 *) (0xFC05800C) // Status
#define MCF532x_I2C_I2DR        (volatile u8 *) (0xFC058010) // Data I/O

/* Bit level definitions and macros */
#define MCF532x_I2C_I2ADR_ADDR(x)                       (((x)&0x7F)<<0x01)

#define MCF532x_I2C_I2FDR_IC(x)                         (((x)&0x3F))

#define MCF532x_I2C_I2CR_IEN    (0x80)	// I2C enable
#define MCF532x_I2C_I2CR_IIEN   (0x40)  // interrupt enable
#define MCF532x_I2C_I2CR_MSTA   (0x20)  // master/slave mode
#define MCF532x_I2C_I2CR_MTX    (0x10)  // transmit/receive mode
#define MCF532x_I2C_I2CR_TXAK   (0x08)  // transmit acknowledge enable
#define MCF532x_I2C_I2CR_RSTA   (0x04)  // repeat start

#define MCF532x_I2C_I2SR_ICF    (0x80)  // data transfer bit
#define MCF532x_I2C_I2SR_IAAS   (0x40)  // I2C addressed as a slave
#define MCF532x_I2C_I2SR_IBB    (0x20)  // I2C bus busy
#define MCF532x_I2C_I2SR_IAL    (0x10)  // aribitration lost
#define MCF532x_I2C_I2SR_SRW    (0x04)  // slave read/write
#define MCF532x_I2C_I2SR_IIF    (0x02)  // I2C interrupt
#define MCF532x_I2C_I2SR_RXAK   (0x01)  // received acknowledge

#define MCF532x_PAR_FECI2C	(volatile u8 *) (0xFC0A4053)


/*
 *	The M5329EVB board needs a help getting its devices initialized 
 *	at kernel start time if dBUG doesn't set it up (for example 
 *	it is not used), so we need to do it manually.
 */
#ifdef __ASSEMBLER__
.macro m5329EVB_setup
	movel	#0xFC098000, %a7
	movel	#0x0, (%a7)
#define CORE_SRAM	0x80000000	
#define CORE_SRAM_SIZE	0x8000
	movel	#CORE_SRAM, %d0
	addl	#0x221, %d0
	movec	%d0,%RAMBAR1
	movel	#CORE_SRAM, %sp
	addl	#CORE_SRAM_SIZE, %sp
	jsr	sysinit
.endm
#define	PLATFORM_SETUP	m5329EVB_setup

#endif /* __ASSEMBLER__ */

/*********************************************************************
 *
 * Chip Configuration Module (CCM)
 *
 *********************************************************************/

/* Register read/write macros */
#define MCF_CCM_CCR               MCF_REG16(0xFC0A0004)
#define MCF_CCM_RCON              MCF_REG16(0xFC0A0008)
#define MCF_CCM_CIR               MCF_REG16(0xFC0A000A)
#define MCF_CCM_MISCCR            MCF_REG16(0xFC0A0010)
#define MCF_CCM_CDR               MCF_REG16(0xFC0A0012)
#define MCF_CCM_UHCSR             MCF_REG16(0xFC0A0014)
#define MCF_CCM_UOCSR             MCF_REG16(0xFC0A0016)

/* Bit definitions and macros for MCF_CCM_CCR */
#define MCF_CCM_CCR_RESERVED      (0x0001)
#define MCF_CCM_CCR_PLL_MODE      (0x0003)
#define MCF_CCM_CCR_OSC_MODE      (0x0005)
#define MCF_CCM_CCR_BOOTPS(x)     (((x)&0x0003)<<3|0x0001)
#define MCF_CCM_CCR_LOAD          (0x0021)
#define MCF_CCM_CCR_LIMP          (0x0041)
#define MCF_CCM_CCR_CSC(x)        (((x)&0x0003)<<8|0x0001)

/* Bit definitions and macros for MCF_CCM_RCON */
#define MCF_CCM_RCON_RESERVED     (0x0001)
#define MCF_CCM_RCON_PLL_MODE     (0x0003)
#define MCF_CCM_RCON_OSC_MODE     (0x0005)
#define MCF_CCM_RCON_BOOTPS(x)    (((x)&0x0003)<<3|0x0001)
#define MCF_CCM_RCON_LOAD         (0x0021)
#define MCF_CCM_RCON_LIMP         (0x0041)
#define MCF_CCM_RCON_CSC(x)       (((x)&0x0003)<<8|0x0001)

/* Bit definitions and macros for MCF_CCM_CIR */
#define MCF_CCM_CIR_PRN(x)        (((x)&0x003F)<<0)
#define MCF_CCM_CIR_PIN(x)        (((x)&0x03FF)<<6)

/* Bit definitions and macros for MCF_CCM_MISCCR */
#define MCF_CCM_MISCCR_USBSRC     (0x0001)
#define MCF_CCM_MISCCR_USBDIV     (0x0002)
#define MCF_CCM_MISCCR_SSI_SRC    (0x0010)
#define MCF_CCM_MISCCR_TIM_DMA   (0x0020)
#define MCF_CCM_MISCCR_SSI_PUS    (0x0040)
#define MCF_CCM_MISCCR_SSI_PUE    (0x0080)
#define MCF_CCM_MISCCR_LCD_CHEN   (0x0100)
#define MCF_CCM_MISCCR_LIMP       (0x1000)
#define MCF_CCM_MISCCR_PLL_LOCK   (0x2000)

/* Bit definitions and macros for MCF_CCM_CDR */
#define MCF_CCM_CDR_SSIDIV(x)     (((x)&0x000F)<<0)
#define MCF_CCM_CDR_LPDIV(x)      (((x)&0x000F)<<8)

/* Bit definitions and macros for MCF_CCM_UHCSR */
#define MCF_CCM_UHCSR_XPDE        (0x0001)
#define MCF_CCM_UHCSR_UHMIE       (0x0002)
#define MCF_CCM_UHCSR_WKUP        (0x0004)
#define MCF_CCM_UHCSR_PORTIND(x)  (((x)&0x0003)<<14)

/* Bit definitions and macros for MCF_CCM_UOCSR */
#define MCF_CCM_UOCSR_XPDE        (0x0001)
#define MCF_CCM_UOCSR_UOMIE       (0x0002)
#define MCF_CCM_UOCSR_WKUP        (0x0004)
#define MCF_CCM_UOCSR_PWRFLT      (0x0008)
#define MCF_CCM_UOCSR_SEND        (0x0010)
#define MCF_CCM_UOCSR_VVLD        (0x0020)
#define MCF_CCM_UOCSR_BVLD        (0x0040)
#define MCF_CCM_UOCSR_AVLD        (0x0080)
#define MCF_CCM_UOCSR_DPPU        (0x0100)
#define MCF_CCM_UOCSR_DCR_VBUS    (0x0200)
#define MCF_CCM_UOCSR_CRG_VBUS    (0x0400)
#define MCF_CCM_UOCSR_DRV_VBUS    (0x0800)
#define MCF_CCM_UOCSR_DMPD        (0x1000)
#define MCF_CCM_UOCSR_DPPD        (0x2000)
#define MCF_CCM_UOCSR_PORTIND(x)  (((x)&0x0003)<<14)

/*********************************************************************
 *
 * DMA Timers (DTIM)
 *
 *********************************************************************/

/* Register read/write macros */
#define MCF_DTIM0_DTMR           MCF_REG16(0xFC070000)
#define MCF_DTIM0_DTXMR          MCF_REG08(0xFC070002)
#define MCF_DTIM0_DTER           MCF_REG08(0xFC070003)
#define MCF_DTIM0_DTRR           MCF_REG32(0xFC070004)
#define MCF_DTIM0_DTCR           MCF_REG32(0xFC070008)
#define MCF_DTIM0_DTCN           MCF_REG32(0xFC07000C)
#define MCF_DTIM1_DTMR           MCF_REG16(0xFC074000)
#define MCF_DTIM1_DTXMR          MCF_REG08(0xFC074002)
#define MCF_DTIM1_DTER           MCF_REG08(0xFC074003)
#define MCF_DTIM1_DTRR           MCF_REG32(0xFC074004)
#define MCF_DTIM1_DTCR           MCF_REG32(0xFC074008)
#define MCF_DTIM1_DTCN           MCF_REG32(0xFC07400C)
#define MCF_DTIM2_DTMR           MCF_REG16(0xFC078000)
#define MCF_DTIM2_DTXMR          MCF_REG08(0xFC078002)
#define MCF_DTIM2_DTER           MCF_REG08(0xFC078003)
#define MCF_DTIM2_DTRR           MCF_REG32(0xFC078004)
#define MCF_DTIM2_DTCR           MCF_REG32(0xFC078008)
#define MCF_DTIM2_DTCN           MCF_REG32(0xFC07800C)
#define MCF_DTIM3_DTMR           MCF_REG16(0xFC07C000)
#define MCF_DTIM3_DTXMR          MCF_REG08(0xFC07C002)
#define MCF_DTIM3_DTER           MCF_REG08(0xFC07C003)
#define MCF_DTIM3_DTRR           MCF_REG32(0xFC07C004)
#define MCF_DTIM3_DTCR           MCF_REG32(0xFC07C008)
#define MCF_DTIM3_DTCN           MCF_REG32(0xFC07C00C)
#define MCF_DTIM_DTMR(x)         MCF_REG16(0xFC070000+((x)*0x4000))
#define MCF_DTIM_DTXMR(x)        MCF_REG08(0xFC070002+((x)*0x4000))
#define MCF_DTIM_DTER(x)         MCF_REG08(0xFC070003+((x)*0x4000))
#define MCF_DTIM_DTRR(x)         MCF_REG32(0xFC070004+((x)*0x4000))
#define MCF_DTIM_DTCR(x)         MCF_REG32(0xFC070008+((x)*0x4000))
#define MCF_DTIM_DTCN(x)         MCF_REG32(0xFC07000C+((x)*0x4000))

/* Bit definitions and macros for MCF_DTIM_DTMR */
#define MCF_DTIM_DTMR_RST        (0x0001)
#define MCF_DTIM_DTMR_CLK(x)     (((x)&0x0003)<<1)
#define MCF_DTIM_DTMR_FRR        (0x0008)
#define MCF_DTIM_DTMR_ORRI       (0x0010)
#define MCF_DTIM_DTMR_OM         (0x0020)
#define MCF_DTIM_DTMR_CE(x)      (((x)&0x0003)<<6)
#define MCF_DTIM_DTMR_PS(x)      (((x)&0x00FF)<<8)
#define MCF_DTIM_DTMR_CE_ANY     (0x00C0)
#define MCF_DTIM_DTMR_CE_FALL    (0x0080)
#define MCF_DTIM_DTMR_CE_RISE    (0x0040)
#define MCF_DTIM_DTMR_CE_NONE    (0x0000)
#define MCF_DTIM_DTMR_CLK_DTIN   (0x0006)
#define MCF_DTIM_DTMR_CLK_DIV16  (0x0004)
#define MCF_DTIM_DTMR_CLK_DIV1   (0x0002)
#define MCF_DTIM_DTMR_CLK_STOP   (0x0000)

/* Bit definitions and macros for MCF_DTIM_DTXMR */
#define MCF_DTIM_DTXMR_MODE16    (0x01)
#define MCF_DTIM_DTXMR_DMAEN     (0x80)

/* Bit definitions and macros for MCF_DTIM_DTER */
#define MCF_DTIM_DTER_CAP        (0x01)
#define MCF_DTIM_DTER_REF        (0x02)

/* Bit definitions and macros for MCF_DTIM_DTRR */
#define MCF_DTIM_DTRR_REF(x)     (((x)&0xFFFFFFFF)<<0)

/* Bit definitions and macros for MCF_DTIM_DTCR */
#define MCF_DTIM_DTCR_CAP(x)     (((x)&0xFFFFFFFF)<<0)

/* Bit definitions and macros for MCF_DTIM_DTCN */
#define MCF_DTIM_DTCN_CNT(x)     (((x)&0xFFFFFFFF)<<0)

/*********************************************************************
 *
 * FlexBus Chip Selects (FBCS)
 *
 *********************************************************************/

/* Register read/write macros */
#define MCF_FBCS0_CSAR		MCF_REG32(0xFC008000)
#define MCF_FBCS0_CSMR		MCF_REG32(0xFC008004)
#define MCF_FBCS0_CSCR		MCF_REG32(0xFC008008)
#define MCF_FBCS1_CSAR		MCF_REG32(0xFC00800C)
#define MCF_FBCS1_CSMR		MCF_REG32(0xFC008010)
#define MCF_FBCS1_CSCR		MCF_REG32(0xFC008014)
#define MCF_FBCS2_CSAR		MCF_REG32(0xFC008018)
#define MCF_FBCS2_CSMR		MCF_REG32(0xFC00801C)
#define MCF_FBCS2_CSCR		MCF_REG32(0xFC008020)
#define MCF_FBCS3_CSAR		MCF_REG32(0xFC008024)
#define MCF_FBCS3_CSMR		MCF_REG32(0xFC008028)
#define MCF_FBCS3_CSCR		MCF_REG32(0xFC00802C)
#define MCF_FBCS4_CSAR		MCF_REG32(0xFC008030)
#define MCF_FBCS4_CSMR		MCF_REG32(0xFC008034)
#define MCF_FBCS4_CSCR		MCF_REG32(0xFC008038)
#define MCF_FBCS5_CSAR		MCF_REG32(0xFC00803C)
#define MCF_FBCS5_CSMR		MCF_REG32(0xFC008040)
#define MCF_FBCS5_CSCR		MCF_REG32(0xFC008044)
#define MCF_FBCS_CSAR(x)	MCF_REG32(0xFC008000+((x)*0x00C))
#define MCF_FBCS_CSMR(x)	MCF_REG32(0xFC008004+((x)*0x00C))
#define MCF_FBCS_CSCR(x)	MCF_REG32(0xFC008008+((x)*0x00C))

/* Bit definitions and macros for MCF_FBCS_CSAR */
#define MCF_FBCS_CSAR_BA(x)	((x)&0xFFFF0000)

/* Bit definitions and macros for MCF_FBCS_CSMR */
#define MCF_FBCS_CSMR_V		(0x00000001)
#define MCF_FBCS_CSMR_WP	(0x00000100)
#define MCF_FBCS_CSMR_BAM(x)	(((x)&0x0000FFFF)<<16)
#define MCF_FBCS_CSMR_BAM_4G	(0xFFFF0000)
#define MCF_FBCS_CSMR_BAM_2G	(0x7FFF0000)
#define MCF_FBCS_CSMR_BAM_1G	(0x3FFF0000)
#define MCF_FBCS_CSMR_BAM_1024M	(0x3FFF0000)
#define MCF_FBCS_CSMR_BAM_512M	(0x1FFF0000)
#define MCF_FBCS_CSMR_BAM_256M	(0x0FFF0000)
#define MCF_FBCS_CSMR_BAM_128M	(0x07FF0000)
#define MCF_FBCS_CSMR_BAM_64M	(0x03FF0000)
#define MCF_FBCS_CSMR_BAM_32M	(0x01FF0000)
#define MCF_FBCS_CSMR_BAM_16M	(0x00FF0000)
#define MCF_FBCS_CSMR_BAM_8M	(0x007F0000)
#define MCF_FBCS_CSMR_BAM_4M	(0x003F0000)
#define MCF_FBCS_CSMR_BAM_2M	(0x001F0000)
#define MCF_FBCS_CSMR_BAM_1M	(0x000F0000)
#define MCF_FBCS_CSMR_BAM_1024K	(0x000F0000)
#define MCF_FBCS_CSMR_BAM_512K	(0x00070000)
#define MCF_FBCS_CSMR_BAM_256K	(0x00030000)
#define MCF_FBCS_CSMR_BAM_128K	(0x00010000)
#define MCF_FBCS_CSMR_BAM_64K	(0x00000000)

/* Bit definitions and macros for MCF_FBCS_CSCR */
#define MCF_FBCS_CSCR_BSTW	(0x00000008)
#define MCF_FBCS_CSCR_BSTR	(0x00000010)
#define MCF_FBCS_CSCR_BEM	(0x00000020)
#define MCF_FBCS_CSCR_PS(x)	(((x)&0x00000003)<<6)
#define MCF_FBCS_CSCR_AA	(0x00000100)
#define MCF_FBCS_CSCR_SBM	(0x00000200)
#define MCF_FBCS_CSCR_WS(x)	(((x)&0x0000003F)<<10)
#define MCF_FBCS_CSCR_WRAH(x)	(((x)&0x00000003)<<16)
#define MCF_FBCS_CSCR_RDAH(x)	(((x)&0x00000003)<<18)
#define MCF_FBCS_CSCR_ASET(x)	(((x)&0x00000003)<<20)
#define MCF_FBCS_CSCR_SWSEN	(0x00800000)
#define MCF_FBCS_CSCR_SWS(x)	(((x)&0x0000003F)<<26)
#define MCF_FBCS_CSCR_PS_8	(0x0040)
#define MCF_FBCS_CSCR_PS_16	(0x0080)
#define MCF_FBCS_CSCR_PS_32	(0x0000)

/*********************************************************************
 *
 * General Purpose I/O (GPIO)
 *
 *********************************************************************/

/* Register read/write macros */
#define MCFGPIO_PODR_FECH		(0xFC0A4000)
#define MCFGPIO_PODR_FECL		(0xFC0A4001)
#define MCFGPIO_PODR_SSI		(0xFC0A4002)
#define MCFGPIO_PODR_BUSCTL		(0xFC0A4003)
#define MCFGPIO_PODR_BE			(0xFC0A4004)
#define MCFGPIO_PODR_CS			(0xFC0A4005)
#define MCFGPIO_PODR_PWM		(0xFC0A4006)
#define MCFGPIO_PODR_FECI2C		(0xFC0A4007)
#define MCFGPIO_PODR_UART		(0xFC0A4009)
#define MCFGPIO_PODR_QSPI		(0xFC0A400A)
#define MCFGPIO_PODR_TIMER		(0xFC0A400B)
#define MCFGPIO_PODR_LCDDATAH		(0xFC0A400D)
#define MCFGPIO_PODR_LCDDATAM		(0xFC0A400E)
#define MCFGPIO_PODR_LCDDATAL		(0xFC0A400F)
#define MCFGPIO_PODR_LCDCTLH		(0xFC0A4010)
#define MCFGPIO_PODR_LCDCTLL		(0xFC0A4011)
#define MCFGPIO_PDDR_FECH		(0xFC0A4014)
#define MCFGPIO_PDDR_FECL		(0xFC0A4015)
#define MCFGPIO_PDDR_SSI		(0xFC0A4016)
#define MCFGPIO_PDDR_BUSCTL		(0xFC0A4017)
#define MCFGPIO_PDDR_BE			(0xFC0A4018)
#define MCFGPIO_PDDR_CS			(0xFC0A4019)
#define MCFGPIO_PDDR_PWM		(0xFC0A401A)
#define MCFGPIO_PDDR_FECI2C		(0xFC0A401B)
#define MCFGPIO_PDDR_UART		(0xFC0A401C)
#define MCFGPIO_PDDR_QSPI		(0xFC0A401E)
#define MCFGPIO_PDDR_TIMER		(0xFC0A401F)
#define MCFGPIO_PDDR_LCDDATAH		(0xFC0A4021)
#define MCFGPIO_PDDR_LCDDATAM		(0xFC0A4022)
#define MCFGPIO_PDDR_LCDDATAL		(0xFC0A4023)
#define MCFGPIO_PDDR_LCDCTLH		(0xFC0A4024)
#define MCFGPIO_PDDR_LCDCTLL		(0xFC0A4025)
#define MCFGPIO_PPDSDR_FECH		(0xFC0A4028)
#define MCFGPIO_PPDSDR_FECL		(0xFC0A4029)
#define MCFGPIO_PPDSDR_SSI		(0xFC0A402A)
#define MCFGPIO_PPDSDR_BUSCTL		(0xFC0A402B)
#define MCFGPIO_PPDSDR_BE		(0xFC0A402C)
#define MCFGPIO_PPDSDR_CS		(0xFC0A402D)
#define MCFGPIO_PPDSDR_PWM		(0xFC0A402E)
#define MCFGPIO_PPDSDR_FECI2C		(0xFC0A402F)
#define MCFGPIO_PPDSDR_UART		(0xFC0A4031)
#define MCFGPIO_PPDSDR_QSPI		(0xFC0A4032)
#define MCFGPIO_PPDSDR_TIMER		(0xFC0A4033)
#define MCFGPIO_PPDSDR_LCDDATAH		(0xFC0A4035)
#define MCFGPIO_PPDSDR_LCDDATAM		(0xFC0A4036)
#define MCFGPIO_PPDSDR_LCDDATAL		(0xFC0A4037)
#define MCFGPIO_PPDSDR_LCDCTLH		(0xFC0A4038)
#define MCFGPIO_PPDSDR_LCDCTLL		(0xFC0A4039)
#define MCFGPIO_PCLRR_FECH		(0xFC0A403C)
#define MCFGPIO_PCLRR_FECL		(0xFC0A403D)
#define MCFGPIO_PCLRR_SSI		(0xFC0A403E)
#define MCFGPIO_PCLRR_BUSCTL		(0xFC0A403F)
#define MCFGPIO_PCLRR_BE		(0xFC0A4040)
#define MCFGPIO_PCLRR_CS		(0xFC0A4041)
#define MCFGPIO_PCLRR_PWM		(0xFC0A4042)
#define MCFGPIO_PCLRR_FECI2C		(0xFC0A4043)
#define MCFGPIO_PCLRR_UART		(0xFC0A4045)
#define MCFGPIO_PCLRR_QSPI		(0xFC0A4046)
#define MCFGPIO_PCLRR_TIMER		(0xFC0A4047)
#define MCFGPIO_PCLRR_LCDDATAH		(0xFC0A4049)
#define MCFGPIO_PCLRR_LCDDATAM		(0xFC0A404A)
#define MCFGPIO_PCLRR_LCDDATAL		(0xFC0A404B)
#define MCFGPIO_PCLRR_LCDCTLH		(0xFC0A404C)
#define MCFGPIO_PCLRR_LCDCTLL		(0xFC0A404D)
#define MCF_GPIO_PAR_FEC		MCF_REG08(0xFC0A4050)
#define MCF_GPIO_PAR_PWM		MCF_REG08(0xFC0A4051)
#define MCF_GPIO_PAR_BUSCTL		MCF_REG08(0xFC0A4052)
#define MCF_GPIO_PAR_FECI2C		MCF_REG08(0xFC0A4053)
#define MCF_GPIO_PAR_BE			MCF_REG08(0xFC0A4054)
#define MCF_GPIO_PAR_CS			MCF_REG08(0xFC0A4055)
#define MCF_GPIO_PAR_SSI		MCF_REG16(0xFC0A4056)
#define MCF_GPIO_PAR_UART		MCF_REG16(0xFC0A4058)
#define MCF_GPIO_PAR_QSPI		MCF_REG16(0xFC0A405A)
#define MCF_GPIO_PAR_TIMER		MCF_REG08(0xFC0A405C)
#define MCF_GPIO_PAR_LCDDATA		MCF_REG08(0xFC0A405D)
#define MCF_GPIO_PAR_LCDCTL		MCF_REG16(0xFC0A405E)
#define MCF_GPIO_PAR_IRQ		MCF_REG16(0xFC0A4060)
#define MCF_GPIO_MSCR_FLEXBUS		MCF_REG08(0xFC0A4064)
#define MCF_GPIO_MSCR_SDRAM		MCF_REG08(0xFC0A4065)
#define MCF_GPIO_DSCR_I2C		MCF_REG08(0xFC0A4068)
#define MCF_GPIO_DSCR_PWM		MCF_REG08(0xFC0A4069)
#define MCF_GPIO_DSCR_FEC		MCF_REG08(0xFC0A406A)
#define MCF_GPIO_DSCR_UART		MCF_REG08(0xFC0A406B)
#define MCF_GPIO_DSCR_QSPI		MCF_REG08(0xFC0A406C)
#define MCF_GPIO_DSCR_TIMER		MCF_REG08(0xFC0A406D)
#define MCF_GPIO_DSCR_SSI		MCF_REG08(0xFC0A406E)
#define MCF_GPIO_DSCR_LCD		MCF_REG08(0xFC0A406F)
#define MCF_GPIO_DSCR_DEBUG		MCF_REG08(0xFC0A4070)
#define MCF_GPIO_DSCR_CLKRST		MCF_REG08(0xFC0A4071)
#define MCF_GPIO_DSCR_IRQ		MCF_REG08(0xFC0A4072)

/* Bit definitions and macros for MCF_GPIO_PODR_FECH */
#define MCF_GPIO_PODR_FECH_PODR_FECH0              (0x01)
#define MCF_GPIO_PODR_FECH_PODR_FECH1              (0x02)
#define MCF_GPIO_PODR_FECH_PODR_FECH2              (0x04)
#define MCF_GPIO_PODR_FECH_PODR_FECH3              (0x08)
#define MCF_GPIO_PODR_FECH_PODR_FECH4              (0x10)
#define MCF_GPIO_PODR_FECH_PODR_FECH5              (0x20)
#define MCF_GPIO_PODR_FECH_PODR_FECH6              (0x40)
#define MCF_GPIO_PODR_FECH_PODR_FECH7              (0x80)

/* Bit definitions and macros for MCF_GPIO_PODR_FECL */
#define MCF_GPIO_PODR_FECL_PODR_FECL0              (0x01)
#define MCF_GPIO_PODR_FECL_PODR_FECL1              (0x02)
#define MCF_GPIO_PODR_FECL_PODR_FECL2              (0x04)
#define MCF_GPIO_PODR_FECL_PODR_FECL3              (0x08)
#define MCF_GPIO_PODR_FECL_PODR_FECL4              (0x10)
#define MCF_GPIO_PODR_FECL_PODR_FECL5              (0x20)
#define MCF_GPIO_PODR_FECL_PODR_FECL6              (0x40)
#define MCF_GPIO_PODR_FECL_PODR_FECL7              (0x80)

/* Bit definitions and macros for MCF_GPIO_PODR_SSI */
#define MCF_GPIO_PODR_SSI_PODR_SSI0                (0x01)
#define MCF_GPIO_PODR_SSI_PODR_SSI1                (0x02)
#define MCF_GPIO_PODR_SSI_PODR_SSI2                (0x04)
#define MCF_GPIO_PODR_SSI_PODR_SSI3                (0x08)
#define MCF_GPIO_PODR_SSI_PODR_SSI4                (0x10)

/* Bit definitions and macros for MCF_GPIO_PODR_BUSCTL */
#define MCF_GPIO_PODR_BUSCTL_POSDR_BUSCTL0         (0x01)
#define MCF_GPIO_PODR_BUSCTL_PODR_BUSCTL1          (0x02)
#define MCF_GPIO_PODR_BUSCTL_PODR_BUSCTL2          (0x04)
#define MCF_GPIO_PODR_BUSCTL_PODR_BUSCTL3          (0x08)

/* Bit definitions and macros for MCF_GPIO_PODR_BE */
#define MCF_GPIO_PODR_BE_PODR_BE0                  (0x01)
#define MCF_GPIO_PODR_BE_PODR_BE1                  (0x02)
#define MCF_GPIO_PODR_BE_PODR_BE2                  (0x04)
#define MCF_GPIO_PODR_BE_PODR_BE3                  (0x08)

/* Bit definitions and macros for MCF_GPIO_PODR_CS */
#define MCF_GPIO_PODR_CS_PODR_CS1                  (0x02)
#define MCF_GPIO_PODR_CS_PODR_CS2                  (0x04)
#define MCF_GPIO_PODR_CS_PODR_CS3                  (0x08)
#define MCF_GPIO_PODR_CS_PODR_CS4                  (0x10)
#define MCF_GPIO_PODR_CS_PODR_CS5                  (0x20)

/* Bit definitions and macros for MCF_GPIO_PODR_PWM */
#define MCF_GPIO_PODR_PWM_PODR_PWM2                (0x04)
#define MCF_GPIO_PODR_PWM_PODR_PWM3                (0x08)
#define MCF_GPIO_PODR_PWM_PODR_PWM4                (0x10)
#define MCF_GPIO_PODR_PWM_PODR_PWM5                (0x20)

/* Bit definitions and macros for MCF_GPIO_PODR_FECI2C */
#define MCF_GPIO_PODR_FECI2C_PODR_FECI2C0          (0x01)
#define MCF_GPIO_PODR_FECI2C_PODR_FECI2C1          (0x02)
#define MCF_GPIO_PODR_FECI2C_PODR_FECI2C2          (0x04)
#define MCF_GPIO_PODR_FECI2C_PODR_FECI2C3          (0x08)

/* Bit definitions and macros for MCF_GPIO_PODR_UART */
#define MCF_GPIO_PODR_UART_PODR_UART0              (0x01)
#define MCF_GPIO_PODR_UART_PODR_UART1              (0x02)
#define MCF_GPIO_PODR_UART_PODR_UART2              (0x04)
#define MCF_GPIO_PODR_UART_PODR_UART3              (0x08)
#define MCF_GPIO_PODR_UART_PODR_UART4              (0x10)
#define MCF_GPIO_PODR_UART_PODR_UART5              (0x20)
#define MCF_GPIO_PODR_UART_PODR_UART6              (0x40)
#define MCF_GPIO_PODR_UART_PODR_UART7              (0x80)

/* Bit definitions and macros for MCF_GPIO_PODR_QSPI */
#define MCF_GPIO_PODR_QSPI_PODR_QSPI0              (0x01)
#define MCF_GPIO_PODR_QSPI_PODR_QSPI1              (0x02)
#define MCF_GPIO_PODR_QSPI_PODR_QSPI2              (0x04)
#define MCF_GPIO_PODR_QSPI_PODR_QSPI3              (0x08)
#define MCF_GPIO_PODR_QSPI_PODR_QSPI4              (0x10)
#define MCF_GPIO_PODR_QSPI_PODR_QSPI5              (0x20)

/* Bit definitions and macros for MCF_GPIO_PODR_TIMER */
#define MCF_GPIO_PODR_TIMER_PODR_TIMER0            (0x01)
#define MCF_GPIO_PODR_TIMER_PODR_TIMER1            (0x02)
#define MCF_GPIO_PODR_TIMER_PODR_TIMER2            (0x04)
#define MCF_GPIO_PODR_TIMER_PODR_TIMER3            (0x08)

/* Bit definitions and macros for MCF_GPIO_PODR_LCDDATAH */
#define MCF_GPIO_PODR_LCDDATAH_PODR_LCDDATAH0      (0x01)
#define MCF_GPIO_PODR_LCDDATAH_PODR_LCDDATAH1      (0x02)

/* Bit definitions and macros for MCF_GPIO_PODR_LCDDATAM */
#define MCF_GPIO_PODR_LCDDATAM_PODR_LCDDATAM0      (0x01)
#define MCF_GPIO_PODR_LCDDATAM_PODR_LCDDATAM1      (0x02)
#define MCF_GPIO_PODR_LCDDATAM_PODR_LCDDATAM2      (0x04)
#define MCF_GPIO_PODR_LCDDATAM_PODR_LCDDATAM3      (0x08)
#define MCF_GPIO_PODR_LCDDATAM_PODR_LCDDATAM4      (0x10)
#define MCF_GPIO_PODR_LCDDATAM_PODR_LCDDATAM5      (0x20)
#define MCF_GPIO_PODR_LCDDATAM_PODR_LCDDATAM6      (0x40)
#define MCF_GPIO_PODR_LCDDATAM_PODR_LCDDATAM7      (0x80)

/* Bit definitions and macros for MCF_GPIO_PODR_LCDDATAL */
#define MCF_GPIO_PODR_LCDDATAL_PODR_LCDDATAL0      (0x01)
#define MCF_GPIO_PODR_LCDDATAL_PODR_LCDDATAL1      (0x02)
#define MCF_GPIO_PODR_LCDDATAL_PODR_LCDDATAL2      (0x04)
#define MCF_GPIO_PODR_LCDDATAL_PODR_LCDDATAL3      (0x08)
#define MCF_GPIO_PODR_LCDDATAL_PODR_LCDDATAL4      (0x10)
#define MCF_GPIO_PODR_LCDDATAL_PODR_LCDDATAL5      (0x20)
#define MCF_GPIO_PODR_LCDDATAL_PODR_LCDDATAL6      (0x40)
#define MCF_GPIO_PODR_LCDDATAL_PODR_LCDDATAL7      (0x80)

/* Bit definitions and macros for MCF_GPIO_PODR_LCDCTLH */
#define MCF_GPIO_PODR_LCDCTLH_PODR_LCDCTLH0        (0x01)

/* Bit definitions and macros for MCF_GPIO_PODR_LCDCTLL */
#define MCF_GPIO_PODR_LCDCTLL_PODR_LCDCTLL0        (0x01)
#define MCF_GPIO_PODR_LCDCTLL_PODR_LCDCTLL1        (0x02)
#define MCF_GPIO_PODR_LCDCTLL_PODR_LCDCTLL2        (0x04)
#define MCF_GPIO_PODR_LCDCTLL_PODR_LCDCTLL3        (0x08)
#define MCF_GPIO_PODR_LCDCTLL_PODR_LCDCTLL4        (0x10)
#define MCF_GPIO_PODR_LCDCTLL_PODR_LCDCTLL5        (0x20)
#define MCF_GPIO_PODR_LCDCTLL_PODR_LCDCTLL6        (0x40)
#define MCF_GPIO_PODR_LCDCTLL_PODR_LCDCTLL7        (0x80)

/* Bit definitions and macros for MCF_GPIO_PDDR_FECH */
#define MCF_GPIO_PDDR_FECH_PDDR_FECH0              (0x01)
#define MCF_GPIO_PDDR_FECH_PDDR_FECH1              (0x02)
#define MCF_GPIO_PDDR_FECH_PDDR_FECH2              (0x04)
#define MCF_GPIO_PDDR_FECH_PDDR_FECH3              (0x08)
#define MCF_GPIO_PDDR_FECH_PDDR_FECH4              (0x10)
#define MCF_GPIO_PDDR_FECH_PDDR_FECH5              (0x20)
#define MCF_GPIO_PDDR_FECH_PDDR_FECH6              (0x40)
#define MCF_GPIO_PDDR_FECH_PDDR_FECH7              (0x80)

/* Bit definitions and macros for MCF_GPIO_PDDR_FECL */
#define MCF_GPIO_PDDR_FECL_PDDR_FECL0              (0x01)
#define MCF_GPIO_PDDR_FECL_PDDR_FECL1              (0x02)
#define MCF_GPIO_PDDR_FECL_PDDR_FECL2              (0x04)
#define MCF_GPIO_PDDR_FECL_PDDR_FECL3              (0x08)
#define MCF_GPIO_PDDR_FECL_PDDR_FECL4              (0x10)
#define MCF_GPIO_PDDR_FECL_PDDR_FECL5              (0x20)
#define MCF_GPIO_PDDR_FECL_PDDR_FECL6              (0x40)
#define MCF_GPIO_PDDR_FECL_PDDR_FECL7              (0x80)

/* Bit definitions and macros for MCF_GPIO_PDDR_SSI */
#define MCF_GPIO_PDDR_SSI_PDDR_SSI0                (0x01)
#define MCF_GPIO_PDDR_SSI_PDDR_SSI1                (0x02)
#define MCF_GPIO_PDDR_SSI_PDDR_SSI2                (0x04)
#define MCF_GPIO_PDDR_SSI_PDDR_SSI3                (0x08)
#define MCF_GPIO_PDDR_SSI_PDDR_SSI4                (0x10)

/* Bit definitions and macros for MCF_GPIO_PDDR_BUSCTL */
#define MCF_GPIO_PDDR_BUSCTL_POSDR_BUSCTL0         (0x01)
#define MCF_GPIO_PDDR_BUSCTL_PDDR_BUSCTL1          (0x02)
#define MCF_GPIO_PDDR_BUSCTL_PDDR_BUSCTL2          (0x04)
#define MCF_GPIO_PDDR_BUSCTL_PDDR_BUSCTL3          (0x08)

/* Bit definitions and macros for MCF_GPIO_PDDR_BE */
#define MCF_GPIO_PDDR_BE_PDDR_BE0                  (0x01)
#define MCF_GPIO_PDDR_BE_PDDR_BE1                  (0x02)
#define MCF_GPIO_PDDR_BE_PDDR_BE2                  (0x04)
#define MCF_GPIO_PDDR_BE_PDDR_BE3                  (0x08)

/* Bit definitions and macros for MCF_GPIO_PDDR_CS */
#define MCF_GPIO_PDDR_CS_PDDR_CS1                  (0x02)
#define MCF_GPIO_PDDR_CS_PDDR_CS2                  (0x04)
#define MCF_GPIO_PDDR_CS_PDDR_CS3                  (0x08)
#define MCF_GPIO_PDDR_CS_PDDR_CS4                  (0x10)
#define MCF_GPIO_PDDR_CS_PDDR_CS5                  (0x20)

/* Bit definitions and macros for MCF_GPIO_PDDR_PWM */
#define MCF_GPIO_PDDR_PWM_PDDR_PWM2                (0x04)
#define MCF_GPIO_PDDR_PWM_PDDR_PWM3                (0x08)
#define MCF_GPIO_PDDR_PWM_PDDR_PWM4                (0x10)
#define MCF_GPIO_PDDR_PWM_PDDR_PWM5                (0x20)

/* Bit definitions and macros for MCF_GPIO_PDDR_FECI2C */
#define MCF_GPIO_PDDR_FECI2C_PDDR_FECI2C0          (0x01)
#define MCF_GPIO_PDDR_FECI2C_PDDR_FECI2C1          (0x02)
#define MCF_GPIO_PDDR_FECI2C_PDDR_FECI2C2          (0x04)
#define MCF_GPIO_PDDR_FECI2C_PDDR_FECI2C3          (0x08)

/* Bit definitions and macros for MCF_GPIO_PDDR_UART */
#define MCF_GPIO_PDDR_UART_PDDR_UART0              (0x01)
#define MCF_GPIO_PDDR_UART_PDDR_UART1              (0x02)
#define MCF_GPIO_PDDR_UART_PDDR_UART2              (0x04)
#define MCF_GPIO_PDDR_UART_PDDR_UART3              (0x08)
#define MCF_GPIO_PDDR_UART_PDDR_UART4              (0x10)
#define MCF_GPIO_PDDR_UART_PDDR_UART5              (0x20)
#define MCF_GPIO_PDDR_UART_PDDR_UART6              (0x40)
#define MCF_GPIO_PDDR_UART_PDDR_UART7              (0x80)

/* Bit definitions and macros for MCF_GPIO_PDDR_QSPI */
#define MCF_GPIO_PDDR_QSPI_PDDR_QSPI0              (0x01)
#define MCF_GPIO_PDDR_QSPI_PDDR_QSPI1              (0x02)
#define MCF_GPIO_PDDR_QSPI_PDDR_QSPI2              (0x04)
#define MCF_GPIO_PDDR_QSPI_PDDR_QSPI3              (0x08)
#define MCF_GPIO_PDDR_QSPI_PDDR_QSPI4              (0x10)
#define MCF_GPIO_PDDR_QSPI_PDDR_QSPI5              (0x20)

/* Bit definitions and macros for MCF_GPIO_PDDR_TIMER */
#define MCF_GPIO_PDDR_TIMER_PDDR_TIMER0            (0x01)
#define MCF_GPIO_PDDR_TIMER_PDDR_TIMER1            (0x02)
#define MCF_GPIO_PDDR_TIMER_PDDR_TIMER2            (0x04)
#define MCF_GPIO_PDDR_TIMER_PDDR_TIMER3            (0x08)

/* Bit definitions and macros for MCF_GPIO_PDDR_LCDDATAH */
#define MCF_GPIO_PDDR_LCDDATAH_PDDR_LCDDATAH0      (0x01)
#define MCF_GPIO_PDDR_LCDDATAH_PDDR_LCDDATAH1      (0x02)

/* Bit definitions and macros for MCF_GPIO_PDDR_LCDDATAM */
#define MCF_GPIO_PDDR_LCDDATAM_PDDR_LCDDATAM0      (0x01)
#define MCF_GPIO_PDDR_LCDDATAM_PDDR_LCDDATAM1      (0x02)
#define MCF_GPIO_PDDR_LCDDATAM_PDDR_LCDDATAM2      (0x04)
#define MCF_GPIO_PDDR_LCDDATAM_PDDR_LCDDATAM3      (0x08)
#define MCF_GPIO_PDDR_LCDDATAM_PDDR_LCDDATAM4      (0x10)
#define MCF_GPIO_PDDR_LCDDATAM_PDDR_LCDDATAM5      (0x20)
#define MCF_GPIO_PDDR_LCDDATAM_PDDR_LCDDATAM6      (0x40)
#define MCF_GPIO_PDDR_LCDDATAM_PDDR_LCDDATAM7      (0x80)

/* Bit definitions and macros for MCF_GPIO_PDDR_LCDDATAL */
#define MCF_GPIO_PDDR_LCDDATAL_PDDR_LCDDATAL0      (0x01)
#define MCF_GPIO_PDDR_LCDDATAL_PDDR_LCDDATAL1      (0x02)
#define MCF_GPIO_PDDR_LCDDATAL_PDDR_LCDDATAL2      (0x04)
#define MCF_GPIO_PDDR_LCDDATAL_PDDR_LCDDATAL3      (0x08)
#define MCF_GPIO_PDDR_LCDDATAL_PDDR_LCDDATAL4      (0x10)
#define MCF_GPIO_PDDR_LCDDATAL_PDDR_LCDDATAL5      (0x20)
#define MCF_GPIO_PDDR_LCDDATAL_PDDR_LCDDATAL6      (0x40)
#define MCF_GPIO_PDDR_LCDDATAL_PDDR_LCDDATAL7      (0x80)

/* Bit definitions and macros for MCF_GPIO_PDDR_LCDCTLH */
#define MCF_GPIO_PDDR_LCDCTLH_PDDR_LCDCTLH0        (0x01)

/* Bit definitions and macros for MCF_GPIO_PDDR_LCDCTLL */
#define MCF_GPIO_PDDR_LCDCTLL_PDDR_LCDCTLL0        (0x01)
#define MCF_GPIO_PDDR_LCDCTLL_PDDR_LCDCTLL1        (0x02)
#define MCF_GPIO_PDDR_LCDCTLL_PDDR_LCDCTLL2        (0x04)
#define MCF_GPIO_PDDR_LCDCTLL_PDDR_LCDCTLL3        (0x08)
#define MCF_GPIO_PDDR_LCDCTLL_PDDR_LCDCTLL4        (0x10)
#define MCF_GPIO_PDDR_LCDCTLL_PDDR_LCDCTLL5        (0x20)
#define MCF_GPIO_PDDR_LCDCTLL_PDDR_LCDCTLL6        (0x40)
#define MCF_GPIO_PDDR_LCDCTLL_PDDR_LCDCTLL7        (0x80)

/* Bit definitions and macros for MCF_GPIO_PPDSDR_FECH */
#define MCF_GPIO_PPDSDR_FECH_PPDSDR_FECH0          (0x01)
#define MCF_GPIO_PPDSDR_FECH_PPDSDR_FECH1          (0x02)
#define MCF_GPIO_PPDSDR_FECH_PPDSDR_FECH2          (0x04)
#define MCF_GPIO_PPDSDR_FECH_PPDSDR_FECH3          (0x08)
#define MCF_GPIO_PPDSDR_FECH_PPDSDR_FECH4          (0x10)
#define MCF_GPIO_PPDSDR_FECH_PPDSDR_FECH5          (0x20)
#define MCF_GPIO_PPDSDR_FECH_PPDSDR_FECH6          (0x40)
#define MCF_GPIO_PPDSDR_FECH_PPDSDR_FECH7          (0x80)

/* Bit definitions and macros for MCF_GPIO_PPDSDR_FECL */
#define MCF_GPIO_PPDSDR_FECL_PPDSDR_FECL0          (0x01)
#define MCF_GPIO_PPDSDR_FECL_PPDSDR_FECL1          (0x02)
#define MCF_GPIO_PPDSDR_FECL_PPDSDR_FECL2          (0x04)
#define MCF_GPIO_PPDSDR_FECL_PPDSDR_FECL3          (0x08)
#define MCF_GPIO_PPDSDR_FECL_PPDSDR_FECL4          (0x10)
#define MCF_GPIO_PPDSDR_FECL_PPDSDR_FECL5          (0x20)
#define MCF_GPIO_PPDSDR_FECL_PPDSDR_FECL6          (0x40)
#define MCF_GPIO_PPDSDR_FECL_PPDSDR_FECL7          (0x80)

/* Bit definitions and macros for MCF_GPIO_PPDSDR_SSI */
#define MCF_GPIO_PPDSDR_SSI_PPDSDR_SSI0            (0x01)
#define MCF_GPIO_PPDSDR_SSI_PPDSDR_SSI1            (0x02)
#define MCF_GPIO_PPDSDR_SSI_PPDSDR_SSI2            (0x04)
#define MCF_GPIO_PPDSDR_SSI_PPDSDR_SSI3            (0x08)
#define MCF_GPIO_PPDSDR_SSI_PPDSDR_SSI4            (0x10)

/* Bit definitions and macros for MCF_GPIO_PPDSDR_BUSCTL */
#define MCF_GPIO_PPDSDR_BUSCTL_POSDR_BUSCTL0       (0x01)
#define MCF_GPIO_PPDSDR_BUSCTL_PPDSDR_BUSCTL1      (0x02)
#define MCF_GPIO_PPDSDR_BUSCTL_PPDSDR_BUSCTL2      (0x04)
#define MCF_GPIO_PPDSDR_BUSCTL_PPDSDR_BUSCTL3      (0x08)

/* Bit definitions and macros for MCF_GPIO_PPDSDR_BE */
#define MCF_GPIO_PPDSDR_BE_PPDSDR_BE0              (0x01)
#define MCF_GPIO_PPDSDR_BE_PPDSDR_BE1              (0x02)
#define MCF_GPIO_PPDSDR_BE_PPDSDR_BE2              (0x04)
#define MCF_GPIO_PPDSDR_BE_PPDSDR_BE3              (0x08)

/* Bit definitions and macros for MCF_GPIO_PPDSDR_CS */
#define MCF_GPIO_PPDSDR_CS_PPDSDR_CS1              (0x02)
#define MCF_GPIO_PPDSDR_CS_PPDSDR_CS2              (0x04)
#define MCF_GPIO_PPDSDR_CS_PPDSDR_CS3              (0x08)
#define MCF_GPIO_PPDSDR_CS_PPDSDR_CS4              (0x10)
#define MCF_GPIO_PPDSDR_CS_PPDSDR_CS5              (0x20)

/* Bit definitions and macros for MCF_GPIO_PPDSDR_PWM */
#define MCF_GPIO_PPDSDR_PWM_PPDSDR_PWM2            (0x04)
#define MCF_GPIO_PPDSDR_PWM_PPDSDR_PWM3            (0x08)
#define MCF_GPIO_PPDSDR_PWM_PPDSDR_PWM4            (0x10)
#define MCF_GPIO_PPDSDR_PWM_PPDSDR_PWM5            (0x20)

/* Bit definitions and macros for MCF_GPIO_PPDSDR_FECI2C */
#define MCF_GPIO_PPDSDR_FECI2C_PPDSDR_FECI2C0      (0x01)
#define MCF_GPIO_PPDSDR_FECI2C_PPDSDR_FECI2C1      (0x02)
#define MCF_GPIO_PPDSDR_FECI2C_PPDSDR_FECI2C2      (0x04)
#define MCF_GPIO_PPDSDR_FECI2C_PPDSDR_FECI2C3      (0x08)

/* Bit definitions and macros for MCF_GPIO_PPDSDR_UART */
#define MCF_GPIO_PPDSDR_UART_PPDSDR_UART0          (0x01)
#define MCF_GPIO_PPDSDR_UART_PPDSDR_UART1          (0x02)
#define MCF_GPIO_PPDSDR_UART_PPDSDR_UART2          (0x04)
#define MCF_GPIO_PPDSDR_UART_PPDSDR_UART3          (0x08)
#define MCF_GPIO_PPDSDR_UART_PPDSDR_UART4          (0x10)
#define MCF_GPIO_PPDSDR_UART_PPDSDR_UART5          (0x20)
#define MCF_GPIO_PPDSDR_UART_PPDSDR_UART6          (0x40)
#define MCF_GPIO_PPDSDR_UART_PPDSDR_UART7          (0x80)

/* Bit definitions and macros for MCF_GPIO_PPDSDR_QSPI */
#define MCF_GPIO_PPDSDR_QSPI_PPDSDR_QSPI0          (0x01)
#define MCF_GPIO_PPDSDR_QSPI_PPDSDR_QSPI1          (0x02)
#define MCF_GPIO_PPDSDR_QSPI_PPDSDR_QSPI2          (0x04)
#define MCF_GPIO_PPDSDR_QSPI_PPDSDR_QSPI3          (0x08)
#define MCF_GPIO_PPDSDR_QSPI_PPDSDR_QSPI4          (0x10)
#define MCF_GPIO_PPDSDR_QSPI_PPDSDR_QSPI5          (0x20)

/* Bit definitions and macros for MCF_GPIO_PPDSDR_TIMER */
#define MCF_GPIO_PPDSDR_TIMER_PPDSDR_TIMER0        (0x01)
#define MCF_GPIO_PPDSDR_TIMER_PPDSDR_TIMER1        (0x02)
#define MCF_GPIO_PPDSDR_TIMER_PPDSDR_TIMER2        (0x04)
#define MCF_GPIO_PPDSDR_TIMER_PPDSDR_TIMER3        (0x08)

/* Bit definitions and macros for MCF_GPIO_PPDSDR_LCDDATAH */
#define MCF_GPIO_PPDSDR_LCDDATAH_PPDSDR_LCDDATAH0  (0x01)
#define MCF_GPIO_PPDSDR_LCDDATAH_PPDSDR_LCDDATAH1  (0x02)

/* Bit definitions and macros for MCF_GPIO_PPDSDR_LCDDATAM */
#define MCF_GPIO_PPDSDR_LCDDATAM_PPDSDR_LCDDATAM0  (0x01)
#define MCF_GPIO_PPDSDR_LCDDATAM_PPDSDR_LCDDATAM1  (0x02)
#define MCF_GPIO_PPDSDR_LCDDATAM_PPDSDR_LCDDATAM2  (0x04)
#define MCF_GPIO_PPDSDR_LCDDATAM_PPDSDR_LCDDATAM3  (0x08)
#define MCF_GPIO_PPDSDR_LCDDATAM_PPDSDR_LCDDATAM4  (0x10)
#define MCF_GPIO_PPDSDR_LCDDATAM_PPDSDR_LCDDATAM5  (0x20)
#define MCF_GPIO_PPDSDR_LCDDATAM_PPDSDR_LCDDATAM6  (0x40)
#define MCF_GPIO_PPDSDR_LCDDATAM_PPDSDR_LCDDATAM7  (0x80)

/* Bit definitions and macros for MCF_GPIO_PPDSDR_LCDDATAL */
#define MCF_GPIO_PPDSDR_LCDDATAL_PPDSDR_LCDDATAL0  (0x01)
#define MCF_GPIO_PPDSDR_LCDDATAL_PPDSDR_LCDDATAL1  (0x02)
#define MCF_GPIO_PPDSDR_LCDDATAL_PPDSDR_LCDDATAL2  (0x04)
#define MCF_GPIO_PPDSDR_LCDDATAL_PPDSDR_LCDDATAL3  (0x08)
#define MCF_GPIO_PPDSDR_LCDDATAL_PPDSDR_LCDDATAL4  (0x10)
#define MCF_GPIO_PPDSDR_LCDDATAL_PPDSDR_LCDDATAL5  (0x20)
#define MCF_GPIO_PPDSDR_LCDDATAL_PPDSDR_LCDDATAL6  (0x40)
#define MCF_GPIO_PPDSDR_LCDDATAL_PPDSDR_LCDDATAL7  (0x80)

/* Bit definitions and macros for MCF_GPIO_PPDSDR_LCDCTLH */
#define MCF_GPIO_PPDSDR_LCDCTLH_PPDSDR_LCDCTLH0    (0x01)

/* Bit definitions and macros for MCF_GPIO_PPDSDR_LCDCTLL */
#define MCF_GPIO_PPDSDR_LCDCTLL_PPDSDR_LCDCTLL0    (0x01)
#define MCF_GPIO_PPDSDR_LCDCTLL_PPDSDR_LCDCTLL1    (0x02)
#define MCF_GPIO_PPDSDR_LCDCTLL_PPDSDR_LCDCTLL2    (0x04)
#define MCF_GPIO_PPDSDR_LCDCTLL_PPDSDR_LCDCTLL3    (0x08)
#define MCF_GPIO_PPDSDR_LCDCTLL_PPDSDR_LCDCTLL4    (0x10)
#define MCF_GPIO_PPDSDR_LCDCTLL_PPDSDR_LCDCTLL5    (0x20)
#define MCF_GPIO_PPDSDR_LCDCTLL_PPDSDR_LCDCTLL6    (0x40)
#define MCF_GPIO_PPDSDR_LCDCTLL_PPDSDR_LCDCTLL7    (0x80)

/* Bit definitions and macros for MCF_GPIO_PCLRR_FECH */
#define MCF_GPIO_PCLRR_FECH_PCLRR_FECH0            (0x01)
#define MCF_GPIO_PCLRR_FECH_PCLRR_FECH1            (0x02)
#define MCF_GPIO_PCLRR_FECH_PCLRR_FECH2            (0x04)
#define MCF_GPIO_PCLRR_FECH_PCLRR_FECH3            (0x08)
#define MCF_GPIO_PCLRR_FECH_PCLRR_FECH4            (0x10)
#define MCF_GPIO_PCLRR_FECH_PCLRR_FECH5            (0x20)
#define MCF_GPIO_PCLRR_FECH_PCLRR_FECH6            (0x40)
#define MCF_GPIO_PCLRR_FECH_PCLRR_FECH7            (0x80)

/* Bit definitions and macros for MCF_GPIO_PCLRR_FECL */
#define MCF_GPIO_PCLRR_FECL_PCLRR_FECL0            (0x01)
#define MCF_GPIO_PCLRR_FECL_PCLRR_FECL1            (0x02)
#define MCF_GPIO_PCLRR_FECL_PCLRR_FECL2            (0x04)
#define MCF_GPIO_PCLRR_FECL_PCLRR_FECL3            (0x08)
#define MCF_GPIO_PCLRR_FECL_PCLRR_FECL4            (0x10)
#define MCF_GPIO_PCLRR_FECL_PCLRR_FECL5            (0x20)
#define MCF_GPIO_PCLRR_FECL_PCLRR_FECL6            (0x40)
#define MCF_GPIO_PCLRR_FECL_PCLRR_FECL7            (0x80)

/* Bit definitions and macros for MCF_GPIO_PCLRR_SSI */
#define MCF_GPIO_PCLRR_SSI_PCLRR_SSI0              (0x01)
#define MCF_GPIO_PCLRR_SSI_PCLRR_SSI1              (0x02)
#define MCF_GPIO_PCLRR_SSI_PCLRR_SSI2              (0x04)
#define MCF_GPIO_PCLRR_SSI_PCLRR_SSI3              (0x08)
#define MCF_GPIO_PCLRR_SSI_PCLRR_SSI4              (0x10)

/* Bit definitions and macros for MCF_GPIO_PCLRR_BUSCTL */
#define MCF_GPIO_PCLRR_BUSCTL_POSDR_BUSCTL0        (0x01)
#define MCF_GPIO_PCLRR_BUSCTL_PCLRR_BUSCTL1        (0x02)
#define MCF_GPIO_PCLRR_BUSCTL_PCLRR_BUSCTL2        (0x04)
#define MCF_GPIO_PCLRR_BUSCTL_PCLRR_BUSCTL3        (0x08)

/* Bit definitions and macros for MCF_GPIO_PCLRR_BE */
#define MCF_GPIO_PCLRR_BE_PCLRR_BE0                (0x01)
#define MCF_GPIO_PCLRR_BE_PCLRR_BE1                (0x02)
#define MCF_GPIO_PCLRR_BE_PCLRR_BE2                (0x04)
#define MCF_GPIO_PCLRR_BE_PCLRR_BE3                (0x08)

/* Bit definitions and macros for MCF_GPIO_PCLRR_CS */
#define MCF_GPIO_PCLRR_CS_PCLRR_CS1                (0x02)
#define MCF_GPIO_PCLRR_CS_PCLRR_CS2                (0x04)
#define MCF_GPIO_PCLRR_CS_PCLRR_CS3                (0x08)
#define MCF_GPIO_PCLRR_CS_PCLRR_CS4                (0x10)
#define MCF_GPIO_PCLRR_CS_PCLRR_CS5                (0x20)

/* Bit definitions and macros for MCF_GPIO_PCLRR_PWM */
#define MCF_GPIO_PCLRR_PWM_PCLRR_PWM2              (0x04)
#define MCF_GPIO_PCLRR_PWM_PCLRR_PWM3              (0x08)
#define MCF_GPIO_PCLRR_PWM_PCLRR_PWM4              (0x10)
#define MCF_GPIO_PCLRR_PWM_PCLRR_PWM5              (0x20)

/* Bit definitions and macros for MCF_GPIO_PCLRR_FECI2C */
#define MCF_GPIO_PCLRR_FECI2C_PCLRR_FECI2C0        (0x01)
#define MCF_GPIO_PCLRR_FECI2C_PCLRR_FECI2C1        (0x02)
#define MCF_GPIO_PCLRR_FECI2C_PCLRR_FECI2C2        (0x04)
#define MCF_GPIO_PCLRR_FECI2C_PCLRR_FECI2C3        (0x08)

/* Bit definitions and macros for MCF_GPIO_PCLRR_UART */
#define MCF_GPIO_PCLRR_UART_PCLRR_UART0            (0x01)
#define MCF_GPIO_PCLRR_UART_PCLRR_UART1            (0x02)
#define MCF_GPIO_PCLRR_UART_PCLRR_UART2            (0x04)
#define MCF_GPIO_PCLRR_UART_PCLRR_UART3            (0x08)
#define MCF_GPIO_PCLRR_UART_PCLRR_UART4            (0x10)
#define MCF_GPIO_PCLRR_UART_PCLRR_UART5            (0x20)
#define MCF_GPIO_PCLRR_UART_PCLRR_UART6            (0x40)
#define MCF_GPIO_PCLRR_UART_PCLRR_UART7            (0x80)

/* Bit definitions and macros for MCF_GPIO_PCLRR_QSPI */
#define MCF_GPIO_PCLRR_QSPI_PCLRR_QSPI0            (0x01)
#define MCF_GPIO_PCLRR_QSPI_PCLRR_QSPI1            (0x02)
#define MCF_GPIO_PCLRR_QSPI_PCLRR_QSPI2            (0x04)
#define MCF_GPIO_PCLRR_QSPI_PCLRR_QSPI3            (0x08)
#define MCF_GPIO_PCLRR_QSPI_PCLRR_QSPI4            (0x10)
#define MCF_GPIO_PCLRR_QSPI_PCLRR_QSPI5            (0x20)

/* Bit definitions and macros for MCF_GPIO_PCLRR_TIMER */
#define MCF_GPIO_PCLRR_TIMER_PCLRR_TIMER0          (0x01)
#define MCF_GPIO_PCLRR_TIMER_PCLRR_TIMER1          (0x02)
#define MCF_GPIO_PCLRR_TIMER_PCLRR_TIMER2          (0x04)
#define MCF_GPIO_PCLRR_TIMER_PCLRR_TIMER3          (0x08)

/* Bit definitions and macros for MCF_GPIO_PCLRR_LCDDATAH */
#define MCF_GPIO_PCLRR_LCDDATAH_PCLRR_LCDDATAH0    (0x01)
#define MCF_GPIO_PCLRR_LCDDATAH_PCLRR_LCDDATAH1    (0x02)

/* Bit definitions and macros for MCF_GPIO_PCLRR_LCDDATAM */
#define MCF_GPIO_PCLRR_LCDDATAM_PCLRR_LCDDATAM0    (0x01)
#define MCF_GPIO_PCLRR_LCDDATAM_PCLRR_LCDDATAM1    (0x02)
#define MCF_GPIO_PCLRR_LCDDATAM_PCLRR_LCDDATAM2    (0x04)
#define MCF_GPIO_PCLRR_LCDDATAM_PCLRR_LCDDATAM3    (0x08)
#define MCF_GPIO_PCLRR_LCDDATAM_PCLRR_LCDDATAM4    (0x10)
#define MCF_GPIO_PCLRR_LCDDATAM_PCLRR_LCDDATAM5    (0x20)
#define MCF_GPIO_PCLRR_LCDDATAM_PCLRR_LCDDATAM6    (0x40)
#define MCF_GPIO_PCLRR_LCDDATAM_PCLRR_LCDDATAM7    (0x80)

/* Bit definitions and macros for MCF_GPIO_PCLRR_LCDDATAL */
#define MCF_GPIO_PCLRR_LCDDATAL_PCLRR_LCDDATAL0    (0x01)
#define MCF_GPIO_PCLRR_LCDDATAL_PCLRR_LCDDATAL1    (0x02)
#define MCF_GPIO_PCLRR_LCDDATAL_PCLRR_LCDDATAL2    (0x04)
#define MCF_GPIO_PCLRR_LCDDATAL_PCLRR_LCDDATAL3    (0x08)
#define MCF_GPIO_PCLRR_LCDDATAL_PCLRR_LCDDATAL4    (0x10)
#define MCF_GPIO_PCLRR_LCDDATAL_PCLRR_LCDDATAL5    (0x20)
#define MCF_GPIO_PCLRR_LCDDATAL_PCLRR_LCDDATAL6    (0x40)
#define MCF_GPIO_PCLRR_LCDDATAL_PCLRR_LCDDATAL7    (0x80)

/* Bit definitions and macros for MCF_GPIO_PCLRR_LCDCTLH */
#define MCF_GPIO_PCLRR_LCDCTLH_PCLRR_LCDCTLH0      (0x01)

/* Bit definitions and macros for MCF_GPIO_PCLRR_LCDCTLL */
#define MCF_GPIO_PCLRR_LCDCTLL_PCLRR_LCDCTLL0      (0x01)
#define MCF_GPIO_PCLRR_LCDCTLL_PCLRR_LCDCTLL1      (0x02)
#define MCF_GPIO_PCLRR_LCDCTLL_PCLRR_LCDCTLL2      (0x04)
#define MCF_GPIO_PCLRR_LCDCTLL_PCLRR_LCDCTLL3      (0x08)
#define MCF_GPIO_PCLRR_LCDCTLL_PCLRR_LCDCTLL4      (0x10)
#define MCF_GPIO_PCLRR_LCDCTLL_PCLRR_LCDCTLL5      (0x20)
#define MCF_GPIO_PCLRR_LCDCTLL_PCLRR_LCDCTLL6      (0x40)
#define MCF_GPIO_PCLRR_LCDCTLL_PCLRR_LCDCTLL7      (0x80)

/* Bit definitions and macros for MCF_GPIO_PAR_FEC */
#define MCF_GPIO_PAR_FEC_PAR_FEC_MII(x)            (((x)&0x03)<<0)
#define MCF_GPIO_PAR_FEC_PAR_FEC_7W(x)             (((x)&0x03)<<2)
#define MCF_GPIO_PAR_FEC_PAR_FEC_7W_GPIO           (0x00)
#define MCF_GPIO_PAR_FEC_PAR_FEC_7W_URTS1          (0x04)
#define MCF_GPIO_PAR_FEC_PAR_FEC_7W_FEC            (0x0C)
#define MCF_GPIO_PAR_FEC_PAR_FEC_MII_GPIO          (0x00)
#define MCF_GPIO_PAR_FEC_PAR_FEC_MII_UART          (0x01)
#define MCF_GPIO_PAR_FEC_PAR_FEC_MII_FEC           (0x03)

/* Bit definitions and macros for MCF_GPIO_PAR_PWM */
#define MCF_GPIO_PAR_PWM_PAR_PWM1(x)               (((x)&0x03)<<0)
#define MCF_GPIO_PAR_PWM_PAR_PWM3(x)               (((x)&0x03)<<2)
#define MCF_GPIO_PAR_PWM_PAR_PWM5                  (0x10)
#define MCF_GPIO_PAR_PWM_PAR_PWM7                  (0x20)

/* Bit definitions and macros for MCF_GPIO_PAR_BUSCTL */
#define MCF_GPIO_PAR_BUSCTL_PAR_TS(x)              (((x)&0x03)<<3)
#define MCF_GPIO_PAR_BUSCTL_PAR_RWB                (0x20)
#define MCF_GPIO_PAR_BUSCTL_PAR_TA                 (0x40)
#define MCF_GPIO_PAR_BUSCTL_PAR_OE                 (0x80)
#define MCF_GPIO_PAR_BUSCTL_PAR_OE_GPIO            (0x00)
#define MCF_GPIO_PAR_BUSCTL_PAR_OE_OE              (0x80)
#define MCF_GPIO_PAR_BUSCTL_PAR_TA_GPIO            (0x00)
#define MCF_GPIO_PAR_BUSCTL_PAR_TA_TA              (0x40)
#define MCF_GPIO_PAR_BUSCTL_PAR_RWB_GPIO           (0x00)
#define MCF_GPIO_PAR_BUSCTL_PAR_RWB_RWB            (0x20)
#define MCF_GPIO_PAR_BUSCTL_PAR_TS_GPIO            (0x00)
#define MCF_GPIO_PAR_BUSCTL_PAR_TS_DACK0           (0x10)
#define MCF_GPIO_PAR_BUSCTL_PAR_TS_TS              (0x18)

/* Bit definitions and macros for MCF_GPIO_PAR_FECI2C */
#define MCF_GPIO_PAR_FECI2C_PAR_SDA(x)             (((x)&0x03)<<0)
#define MCF_GPIO_PAR_FECI2C_PAR_SCL(x)             (((x)&0x03)<<2)
#define MCF_GPIO_PAR_FECI2C_PAR_MDIO(x)            (((x)&0x03)<<4)
#define MCF_GPIO_PAR_FECI2C_PAR_MDC(x)             (((x)&0x03)<<6)
#define MCF_GPIO_PAR_FECI2C_PAR_MDC_GPIO           (0x00)
#define MCF_GPIO_PAR_FECI2C_PAR_MDC_UTXD2          (0x40)
#define MCF_GPIO_PAR_FECI2C_PAR_MDC_SCL            (0x80)
#define MCF_GPIO_PAR_FECI2C_PAR_MDC_EMDC           (0xC0)
#define MCF_GPIO_PAR_FECI2C_PAR_MDIO_GPIO          (0x00)
#define MCF_GPIO_PAR_FECI2C_PAR_MDIO_URXD2         (0x10)
#define MCF_GPIO_PAR_FECI2C_PAR_MDIO_SDA           (0x20)
#define MCF_GPIO_PAR_FECI2C_PAR_MDIO_EMDIO         (0x30)
#define MCF_GPIO_PAR_FECI2C_PAR_SCL_GPIO           (0x00)
#define MCF_GPIO_PAR_FECI2C_PAR_SCL_UTXD2          (0x04)
#define MCF_GPIO_PAR_FECI2C_PAR_SCL_SCL            (0x0C)
#define MCF_GPIO_PAR_FECI2C_PAR_SDA_GPIO           (0x00)
#define MCF_GPIO_PAR_FECI2C_PAR_SDA_URXD2          (0x02)
#define MCF_GPIO_PAR_FECI2C_PAR_SDA_SDA            (0x03)

/* Bit definitions and macros for MCF_GPIO_PAR_BE */
#define MCF_GPIO_PAR_BE_PAR_BE0                    (0x01)
#define MCF_GPIO_PAR_BE_PAR_BE1                    (0x02)
#define MCF_GPIO_PAR_BE_PAR_BE2                    (0x04)
#define MCF_GPIO_PAR_BE_PAR_BE3                    (0x08)

/* Bit definitions and macros for MCF_GPIO_PAR_CS */
#define MCF_GPIO_PAR_CS_PAR_CS1                    (0x02)
#define MCF_GPIO_PAR_CS_PAR_CS2                    (0x04)
#define MCF_GPIO_PAR_CS_PAR_CS3                    (0x08)
#define MCF_GPIO_PAR_CS_PAR_CS4                    (0x10)
#define MCF_GPIO_PAR_CS_PAR_CS5                    (0x20)
#define MCF_GPIO_PAR_CS_PAR_CS_CS1_GPIO            (0x00)
#define MCF_GPIO_PAR_CS_PAR_CS_CS1_SDCS1           (0x01)
#define MCF_GPIO_PAR_CS_PAR_CS_CS1_CS1             (0x03)

/* Bit definitions and macros for MCF_GPIO_PAR_SSI */
#define MCF_GPIO_PAR_SSI_PAR_MCLK                  (0x0080)
#define MCF_GPIO_PAR_SSI_PAR_TXD(x)                (((x)&0x0003)<<8)
#define MCF_GPIO_PAR_SSI_PAR_RXD(x)                (((x)&0x0003)<<10)
#define MCF_GPIO_PAR_SSI_PAR_FS(x)                 (((x)&0x0003)<<12)
#define MCF_GPIO_PAR_SSI_PAR_BCLK(x)               (((x)&0x0003)<<14)

/* Bit definitions and macros for MCF_GPIO_PAR_UART */
#define MCF_GPIO_PAR_UART_PAR_UTXD0                (0x0001)
#define MCF_GPIO_PAR_UART_PAR_URXD0                (0x0002)
#define MCF_GPIO_PAR_UART_PAR_URTS0                (0x0004)
#define MCF_GPIO_PAR_UART_PAR_UCTS0                (0x0008)
#define MCF_GPIO_PAR_UART_PAR_UTXD1(x)             (((x)&0x0003)<<4)
#define MCF_GPIO_PAR_UART_PAR_URXD1(x)             (((x)&0x0003)<<6)
#define MCF_GPIO_PAR_UART_PAR_URTS1(x)             (((x)&0x0003)<<8)
#define MCF_GPIO_PAR_UART_PAR_UCTS1(x)             (((x)&0x0003)<<10)
#define MCF_GPIO_PAR_UART_PAR_UCTS1_GPIO           (0x0000)
#define MCF_GPIO_PAR_UART_PAR_UCTS1_SSI_BCLK       (0x0800)
#define MCF_GPIO_PAR_UART_PAR_UCTS1_ULPI_D7        (0x0400)
#define MCF_GPIO_PAR_UART_PAR_UCTS1_UCTS1          (0x0C00)
#define MCF_GPIO_PAR_UART_PAR_URTS1_GPIO           (0x0000)
#define MCF_GPIO_PAR_UART_PAR_URTS1_SSI_FS         (0x0200)
#define MCF_GPIO_PAR_UART_PAR_URTS1_ULPI_D6        (0x0100)
#define MCF_GPIO_PAR_UART_PAR_URTS1_URTS1          (0x0300)
#define MCF_GPIO_PAR_UART_PAR_URXD1_GPIO           (0x0000)
#define MCF_GPIO_PAR_UART_PAR_URXD1_SSI_RXD        (0x0080)
#define MCF_GPIO_PAR_UART_PAR_URXD1_ULPI_D5        (0x0040)
#define MCF_GPIO_PAR_UART_PAR_URXD1_URXD1          (0x00C0)
#define MCF_GPIO_PAR_UART_PAR_UTXD1_GPIO           (0x0000)
#define MCF_GPIO_PAR_UART_PAR_UTXD1_SSI_TXD        (0x0020)
#define MCF_GPIO_PAR_UART_PAR_UTXD1_ULPI_D4        (0x0010)
#define MCF_GPIO_PAR_UART_PAR_UTXD1_UTXD1          (0x0030)

/* Bit definitions and macros for MCF_GPIO_PAR_QSPI */
#define MCF_GPIO_PAR_QSPI_PAR_SCK(x)               (((x)&0x0003)<<4)
#define MCF_GPIO_PAR_QSPI_PAR_DOUT(x)              (((x)&0x0003)<<6)
#define MCF_GPIO_PAR_QSPI_PAR_DIN(x)               (((x)&0x0003)<<8)
#define MCF_GPIO_PAR_QSPI_PAR_PCS0(x)              (((x)&0x0003)<<10)
#define MCF_GPIO_PAR_QSPI_PAR_PCS1(x)              (((x)&0x0003)<<12)
#define MCF_GPIO_PAR_QSPI_PAR_PCS2(x)              (((x)&0x0003)<<14)

/* Bit definitions and macros for MCF_GPIO_PAR_TIMER */
#define MCF_GPIO_PAR_TIMER_PAR_TIN0(x)             (((x)&0x03)<<0)
#define MCF_GPIO_PAR_TIMER_PAR_TIN1(x)             (((x)&0x03)<<2)
#define MCF_GPIO_PAR_TIMER_PAR_TIN2(x)             (((x)&0x03)<<4)
#define MCF_GPIO_PAR_TIMER_PAR_TIN3(x)             (((x)&0x03)<<6)
#define MCF_GPIO_PAR_TIMER_PAR_TIN3_GPIO           (0x00)
#define MCF_GPIO_PAR_TIMER_PAR_TIN3_TOUT3          (0x80)
#define MCF_GPIO_PAR_TIMER_PAR_TIN3_URXD2          (0x40)
#define MCF_GPIO_PAR_TIMER_PAR_TIN3_TIN3           (0xC0)
#define MCF_GPIO_PAR_TIMER_PAR_TIN2_GPIO           (0x00)
#define MCF_GPIO_PAR_TIMER_PAR_TIN2_TOUT2          (0x20)
#define MCF_GPIO_PAR_TIMER_PAR_TIN2_UTXD2          (0x10)
#define MCF_GPIO_PAR_TIMER_PAR_TIN2_TIN2           (0x30)
#define MCF_GPIO_PAR_TIMER_PAR_TIN1_GPIO           (0x00)
#define MCF_GPIO_PAR_TIMER_PAR_TIN1_TOUT1          (0x08)
#define MCF_GPIO_PAR_TIMER_PAR_TIN1_DACK1          (0x04)
#define MCF_GPIO_PAR_TIMER_PAR_TIN1_TIN1           (0x0C)
#define MCF_GPIO_PAR_TIMER_PAR_TIN0_GPIO           (0x00)
#define MCF_GPIO_PAR_TIMER_PAR_TIN0_TOUT0          (0x02)
#define MCF_GPIO_PAR_TIMER_PAR_TIN0_DREQ0          (0x01)
#define MCF_GPIO_PAR_TIMER_PAR_TIN0_TIN0           (0x03)

/* Bit definitions and macros for MCF_GPIO_PAR_LCDDATA */
#define MCF_GPIO_PAR_LCDDATA_PAR_LD7_0(x)          (((x)&0x03)<<0)
#define MCF_GPIO_PAR_LCDDATA_PAR_LD15_8(x)         (((x)&0x03)<<2)
#define MCF_GPIO_PAR_LCDDATA_PAR_LD16(x)           (((x)&0x03)<<4)
#define MCF_GPIO_PAR_LCDDATA_PAR_LD17(x)           (((x)&0x03)<<6)

/* Bit definitions and macros for MCF_GPIO_PAR_LCDCTL */
#define MCF_GPIO_PAR_LCDCTL_PAR_CLS                (0x0001)
#define MCF_GPIO_PAR_LCDCTL_PAR_PS                 (0x0002)
#define MCF_GPIO_PAR_LCDCTL_PAR_REV                (0x0004)
#define MCF_GPIO_PAR_LCDCTL_PAR_SPL_SPR            (0x0008)
#define MCF_GPIO_PAR_LCDCTL_PAR_CONTRAST           (0x0010)
#define MCF_GPIO_PAR_LCDCTL_PAR_LSCLK              (0x0020)
#define MCF_GPIO_PAR_LCDCTL_PAR_LP_HSYNC           (0x0040)
#define MCF_GPIO_PAR_LCDCTL_PAR_FLM_VSYNC          (0x0080)
#define MCF_GPIO_PAR_LCDCTL_PAR_ACD_OE             (0x0100)

/* Bit definitions and macros for MCF_GPIO_PAR_IRQ */
#define MCF_GPIO_PAR_IRQ_PAR_IRQ1(x)               (((x)&0x0003)<<4)
#define MCF_GPIO_PAR_IRQ_PAR_IRQ2(x)               (((x)&0x0003)<<6)
#define MCF_GPIO_PAR_IRQ_PAR_IRQ4(x)               (((x)&0x0003)<<8)
#define MCF_GPIO_PAR_IRQ_PAR_IRQ5(x)               (((x)&0x0003)<<10)
#define MCF_GPIO_PAR_IRQ_PAR_IRQ6(x)               (((x)&0x0003)<<12)

/* Bit definitions and macros for MCF_GPIO_MSCR_FLEXBUS */
#define MCF_GPIO_MSCR_FLEXBUS_MSCR_ADDRCTL(x)      (((x)&0x03)<<0)
#define MCF_GPIO_MSCR_FLEXBUS_MSCR_DLOWER(x)       (((x)&0x03)<<2)
#define MCF_GPIO_MSCR_FLEXBUS_MSCR_DUPPER(x)       (((x)&0x03)<<4)

/* Bit definitions and macros for MCF_GPIO_MSCR_SDRAM */
#define MCF_GPIO_MSCR_SDRAM_MSCR_SDRAM(x)          (((x)&0x03)<<0)
#define MCF_GPIO_MSCR_SDRAM_MSCR_SDCLK(x)          (((x)&0x03)<<2)
#define MCF_GPIO_MSCR_SDRAM_MSCR_SDCLKB(x)         (((x)&0x03)<<4)

/* Bit definitions and macros for MCF_GPIO_DSCR_I2C */
#define MCF_GPIO_DSCR_I2C_I2C_DSE(x)               (((x)&0x03)<<0)

/* Bit definitions and macros for MCF_GPIO_DSCR_PWM */
#define MCF_GPIO_DSCR_PWM_PWM_DSE(x)               (((x)&0x03)<<0)

/* Bit definitions and macros for MCF_GPIO_DSCR_FEC */
#define MCF_GPIO_DSCR_FEC_FEC_DSE(x)               (((x)&0x03)<<0)

/* Bit definitions and macros for MCF_GPIO_DSCR_UART */
#define MCF_GPIO_DSCR_UART_UART0_DSE(x)            (((x)&0x03)<<0)
#define MCF_GPIO_DSCR_UART_UART1_DSE(x)            (((x)&0x03)<<2)

/* Bit definitions and macros for MCF_GPIO_DSCR_QSPI */
#define MCF_GPIO_DSCR_QSPI_QSPI_DSE(x)             (((x)&0x03)<<0)

/* Bit definitions and macros for MCF_GPIO_DSCR_TIMER */
#define MCF_GPIO_DSCR_TIMER_TIMER_DSE(x)           (((x)&0x03)<<0)

/* Bit definitions and macros for MCF_GPIO_DSCR_SSI */
#define MCF_GPIO_DSCR_SSI_SSI_DSE(x)               (((x)&0x03)<<0)

/* Bit definitions and macros for MCF_GPIO_DSCR_LCD */
#define MCF_GPIO_DSCR_LCD_LCD_DSE(x)               (((x)&0x03)<<0)

/* Bit definitions and macros for MCF_GPIO_DSCR_DEBUG */
#define MCF_GPIO_DSCR_DEBUG_DEBUG_DSE(x)           (((x)&0x03)<<0)

/* Bit definitions and macros for MCF_GPIO_DSCR_CLKRST */
#define MCF_GPIO_DSCR_CLKRST_CLKRST_DSE(x)         (((x)&0x03)<<0)

/* Bit definitions and macros for MCF_GPIO_DSCR_IRQ */
#define MCF_GPIO_DSCR_IRQ_IRQ_DSE(x)               (((x)&0x03)<<0)

/*
 * Generic GPIO support
 */
#define MCFGPIO_PODR			MCFGPIO_PODR_FECH
#define MCFGPIO_PDDR			MCFGPIO_PDDR_FECH
#define MCFGPIO_PPDR			MCFGPIO_PPDSDR_FECH
#define MCFGPIO_SETR			MCFGPIO_PPDSDR_FECH
#define MCFGPIO_CLRR			MCFGPIO_PCLRR_FECH

#define MCFGPIO_PIN_MAX			136
#define MCFGPIO_IRQ_MAX			8
#define MCFGPIO_IRQ_VECBASE		MCFINT_VECBASE


/*********************************************************************
 *
 * Interrupt Controller (INTC)
 *
 *********************************************************************/

/* Register read/write macros */
#define MCF_INTC0_IPRH             MCF_REG32(0xFC048000)
#define MCF_INTC0_IPRL             MCF_REG32(0xFC048004)
#define MCF_INTC0_IMRH             MCF_REG32(0xFC048008)
#define MCF_INTC0_IMRL             MCF_REG32(0xFC04800C)
#define MCF_INTC0_INTFRCH          MCF_REG32(0xFC048010)
#define MCF_INTC0_INTFRCL          MCF_REG32(0xFC048014)
#define MCF_INTC0_ICONFIG          MCF_REG16(0xFC04801A)
#define MCF_INTC0_SIMR             MCF_REG08(0xFC04801C)
#define MCF_INTC0_CIMR             MCF_REG08(0xFC04801D)
#define MCF_INTC0_CLMASK           MCF_REG08(0xFC04801E)
#define MCF_INTC0_SLMASK           MCF_REG08(0xFC04801F)
#define MCF_INTC0_ICR0             MCF_REG08(0xFC048040)
#define MCF_INTC0_ICR1             MCF_REG08(0xFC048041)
#define MCF_INTC0_ICR2             MCF_REG08(0xFC048042)
#define MCF_INTC0_ICR3             MCF_REG08(0xFC048043)
#define MCF_INTC0_ICR4             MCF_REG08(0xFC048044)
#define MCF_INTC0_ICR5             MCF_REG08(0xFC048045)
#define MCF_INTC0_ICR6             MCF_REG08(0xFC048046)
#define MCF_INTC0_ICR7             MCF_REG08(0xFC048047)
#define MCF_INTC0_ICR8             MCF_REG08(0xFC048048)
#define MCF_INTC0_ICR9             MCF_REG08(0xFC048049)
#define MCF_INTC0_ICR10            MCF_REG08(0xFC04804A)
#define MCF_INTC0_ICR11            MCF_REG08(0xFC04804B)
#define MCF_INTC0_ICR12            MCF_REG08(0xFC04804C)
#define MCF_INTC0_ICR13            MCF_REG08(0xFC04804D)
#define MCF_INTC0_ICR14            MCF_REG08(0xFC04804E)
#define MCF_INTC0_ICR15            MCF_REG08(0xFC04804F)
#define MCF_INTC0_ICR16            MCF_REG08(0xFC048050)
#define MCF_INTC0_ICR17            MCF_REG08(0xFC048051)
#define MCF_INTC0_ICR18            MCF_REG08(0xFC048052)
#define MCF_INTC0_ICR19            MCF_REG08(0xFC048053)
#define MCF_INTC0_ICR20            MCF_REG08(0xFC048054)
#define MCF_INTC0_ICR21            MCF_REG08(0xFC048055)
#define MCF_INTC0_ICR22            MCF_REG08(0xFC048056)
#define MCF_INTC0_ICR23            MCF_REG08(0xFC048057)
#define MCF_INTC0_ICR24            MCF_REG08(0xFC048058)
#define MCF_INTC0_ICR25            MCF_REG08(0xFC048059)
#define MCF_INTC0_ICR26            MCF_REG08(0xFC04805A)
#define MCF_INTC0_ICR27            MCF_REG08(0xFC04805B)
#define MCF_INTC0_ICR28            MCF_REG08(0xFC04805C)
#define MCF_INTC0_ICR29            MCF_REG08(0xFC04805D)
#define MCF_INTC0_ICR30            MCF_REG08(0xFC04805E)
#define MCF_INTC0_ICR31            MCF_REG08(0xFC04805F)
#define MCF_INTC0_ICR32            MCF_REG08(0xFC048060)
#define MCF_INTC0_ICR33            MCF_REG08(0xFC048061)
#define MCF_INTC0_ICR34            MCF_REG08(0xFC048062)
#define MCF_INTC0_ICR35            MCF_REG08(0xFC048063)
#define MCF_INTC0_ICR36            MCF_REG08(0xFC048064)
#define MCF_INTC0_ICR37            MCF_REG08(0xFC048065)
#define MCF_INTC0_ICR38            MCF_REG08(0xFC048066)
#define MCF_INTC0_ICR39            MCF_REG08(0xFC048067)
#define MCF_INTC0_ICR40            MCF_REG08(0xFC048068)
#define MCF_INTC0_ICR41            MCF_REG08(0xFC048069)
#define MCF_INTC0_ICR42            MCF_REG08(0xFC04806A)
#define MCF_INTC0_ICR43            MCF_REG08(0xFC04806B)
#define MCF_INTC0_ICR44            MCF_REG08(0xFC04806C)
#define MCF_INTC0_ICR45            MCF_REG08(0xFC04806D)
#define MCF_INTC0_ICR46            MCF_REG08(0xFC04806E)
#define MCF_INTC0_ICR47            MCF_REG08(0xFC04806F)
#define MCF_INTC0_ICR48            MCF_REG08(0xFC048070)
#define MCF_INTC0_ICR49            MCF_REG08(0xFC048071)
#define MCF_INTC0_ICR50            MCF_REG08(0xFC048072)
#define MCF_INTC0_ICR51            MCF_REG08(0xFC048073)
#define MCF_INTC0_ICR52            MCF_REG08(0xFC048074)
#define MCF_INTC0_ICR53            MCF_REG08(0xFC048075)
#define MCF_INTC0_ICR54            MCF_REG08(0xFC048076)
#define MCF_INTC0_ICR55            MCF_REG08(0xFC048077)
#define MCF_INTC0_ICR56            MCF_REG08(0xFC048078)
#define MCF_INTC0_ICR57            MCF_REG08(0xFC048079)
#define MCF_INTC0_ICR58            MCF_REG08(0xFC04807A)
#define MCF_INTC0_ICR59            MCF_REG08(0xFC04807B)
#define MCF_INTC0_ICR60            MCF_REG08(0xFC04807C)
#define MCF_INTC0_ICR61            MCF_REG08(0xFC04807D)
#define MCF_INTC0_ICR62            MCF_REG08(0xFC04807E)
#define MCF_INTC0_ICR63            MCF_REG08(0xFC04807F)
#define MCF_INTC0_ICR(x)           MCF_REG08(0xFC048040+((x)*0x001))
#define MCF_INTC0_SWIACK           MCF_REG08(0xFC0480E0)
#define MCF_INTC0_L1IACK           MCF_REG08(0xFC0480E4)
#define MCF_INTC0_L2IACK           MCF_REG08(0xFC0480E8)
#define MCF_INTC0_L3IACK           MCF_REG08(0xFC0480EC)
#define MCF_INTC0_L4IACK           MCF_REG08(0xFC0480F0)
#define MCF_INTC0_L5IACK           MCF_REG08(0xFC0480F4)
#define MCF_INTC0_L6IACK           MCF_REG08(0xFC0480F8)
#define MCF_INTC0_L7IACK           MCF_REG08(0xFC0480FC)
#define MCF_INTC0_LIACK(x)         MCF_REG08(0xFC0480E4+((x)*0x004))
#define MCF_INTC1_IPRH             MCF_REG32(0xFC04C000)
#define MCF_INTC1_IPRL             MCF_REG32(0xFC04C004)
#define MCF_INTC1_IMRH             MCF_REG32(0xFC04C008)
#define MCF_INTC1_IMRL             MCF_REG32(0xFC04C00C)
#define MCF_INTC1_INTFRCH          MCF_REG32(0xFC04C010)
#define MCF_INTC1_INTFRCL          MCF_REG32(0xFC04C014)
#define MCF_INTC1_ICONFIG          MCF_REG16(0xFC04C01A)
#define MCF_INTC1_SIMR             MCF_REG08(0xFC04C01C)
#define MCF_INTC1_CIMR             MCF_REG08(0xFC04C01D)
#define MCF_INTC1_CLMASK           MCF_REG08(0xFC04C01E)
#define MCF_INTC1_SLMASK           MCF_REG08(0xFC04C01F)
#define MCF_INTC1_ICR0             MCF_REG08(0xFC04C040)
#define MCF_INTC1_ICR1             MCF_REG08(0xFC04C041)
#define MCF_INTC1_ICR2             MCF_REG08(0xFC04C042)
#define MCF_INTC1_ICR3             MCF_REG08(0xFC04C043)
#define MCF_INTC1_ICR4             MCF_REG08(0xFC04C044)
#define MCF_INTC1_ICR5             MCF_REG08(0xFC04C045)
#define MCF_INTC1_ICR6             MCF_REG08(0xFC04C046)
#define MCF_INTC1_ICR7             MCF_REG08(0xFC04C047)
#define MCF_INTC1_ICR8             MCF_REG08(0xFC04C048)
#define MCF_INTC1_ICR9             MCF_REG08(0xFC04C049)
#define MCF_INTC1_ICR10            MCF_REG08(0xFC04C04A)
#define MCF_INTC1_ICR11            MCF_REG08(0xFC04C04B)
#define MCF_INTC1_ICR12            MCF_REG08(0xFC04C04C)
#define MCF_INTC1_ICR13            MCF_REG08(0xFC04C04D)
#define MCF_INTC1_ICR14            MCF_REG08(0xFC04C04E)
#define MCF_INTC1_ICR15            MCF_REG08(0xFC04C04F)
#define MCF_INTC1_ICR16            MCF_REG08(0xFC04C050)
#define MCF_INTC1_ICR17            MCF_REG08(0xFC04C051)
#define MCF_INTC1_ICR18            MCF_REG08(0xFC04C052)
#define MCF_INTC1_ICR19            MCF_REG08(0xFC04C053)
#define MCF_INTC1_ICR20            MCF_REG08(0xFC04C054)
#define MCF_INTC1_ICR21            MCF_REG08(0xFC04C055)
#define MCF_INTC1_ICR22            MCF_REG08(0xFC04C056)
#define MCF_INTC1_ICR23            MCF_REG08(0xFC04C057)
#define MCF_INTC1_ICR24            MCF_REG08(0xFC04C058)
#define MCF_INTC1_ICR25            MCF_REG08(0xFC04C059)
#define MCF_INTC1_ICR26            MCF_REG08(0xFC04C05A)
#define MCF_INTC1_ICR27            MCF_REG08(0xFC04C05B)
#define MCF_INTC1_ICR28            MCF_REG08(0xFC04C05C)
#define MCF_INTC1_ICR29            MCF_REG08(0xFC04C05D)
#define MCF_INTC1_ICR30            MCF_REG08(0xFC04C05E)
#define MCF_INTC1_ICR31            MCF_REG08(0xFC04C05F)
#define MCF_INTC1_ICR32            MCF_REG08(0xFC04C060)
#define MCF_INTC1_ICR33            MCF_REG08(0xFC04C061)
#define MCF_INTC1_ICR34            MCF_REG08(0xFC04C062)
#define MCF_INTC1_ICR35            MCF_REG08(0xFC04C063)
#define MCF_INTC1_ICR36            MCF_REG08(0xFC04C064)
#define MCF_INTC1_ICR37            MCF_REG08(0xFC04C065)
#define MCF_INTC1_ICR38            MCF_REG08(0xFC04C066)
#define MCF_INTC1_ICR39            MCF_REG08(0xFC04C067)
#define MCF_INTC1_ICR40            MCF_REG08(0xFC04C068)
#define MCF_INTC1_ICR41            MCF_REG08(0xFC04C069)
#define MCF_INTC1_ICR42            MCF_REG08(0xFC04C06A)
#define MCF_INTC1_ICR43            MCF_REG08(0xFC04C06B)
#define MCF_INTC1_ICR44            MCF_REG08(0xFC04C06C)
#define MCF_INTC1_ICR45            MCF_REG08(0xFC04C06D)
#define MCF_INTC1_ICR46            MCF_REG08(0xFC04C06E)
#define MCF_INTC1_ICR47            MCF_REG08(0xFC04C06F)
#define MCF_INTC1_ICR48            MCF_REG08(0xFC04C070)
#define MCF_INTC1_ICR49            MCF_REG08(0xFC04C071)
#define MCF_INTC1_ICR50            MCF_REG08(0xFC04C072)
#define MCF_INTC1_ICR51            MCF_REG08(0xFC04C073)
#define MCF_INTC1_ICR52            MCF_REG08(0xFC04C074)
#define MCF_INTC1_ICR53            MCF_REG08(0xFC04C075)
#define MCF_INTC1_ICR54            MCF_REG08(0xFC04C076)
#define MCF_INTC1_ICR55            MCF_REG08(0xFC04C077)
#define MCF_INTC1_ICR56            MCF_REG08(0xFC04C078)
#define MCF_INTC1_ICR57            MCF_REG08(0xFC04C079)
#define MCF_INTC1_ICR58            MCF_REG08(0xFC04C07A)
#define MCF_INTC1_ICR59            MCF_REG08(0xFC04C07B)
#define MCF_INTC1_ICR60            MCF_REG08(0xFC04C07C)
#define MCF_INTC1_ICR61            MCF_REG08(0xFC04C07D)
#define MCF_INTC1_ICR62            MCF_REG08(0xFC04C07E)
#define MCF_INTC1_ICR63            MCF_REG08(0xFC04C07F)
#define MCF_INTC1_ICR(x)           MCF_REG08(0xFC04C040+((x)*0x001))
#define MCF_INTC1_SWIACK           MCF_REG08(0xFC04C0E0)
#define MCF_INTC1_L1IACK           MCF_REG08(0xFC04C0E4)
#define MCF_INTC1_L2IACK           MCF_REG08(0xFC04C0E8)
#define MCF_INTC1_L3IACK           MCF_REG08(0xFC04C0EC)
#define MCF_INTC1_L4IACK           MCF_REG08(0xFC04C0F0)
#define MCF_INTC1_L5IACK           MCF_REG08(0xFC04C0F4)
#define MCF_INTC1_L6IACK           MCF_REG08(0xFC04C0F8)
#define MCF_INTC1_L7IACK           MCF_REG08(0xFC04C0FC)
#define MCF_INTC1_LIACK(x)         MCF_REG08(0xFC04C0E4+((x)*0x004))
#define MCF_INTC_IPRH(x)           MCF_REG32(0xFC048000+((x)*0x4000))
#define MCF_INTC_IPRL(x)           MCF_REG32(0xFC048004+((x)*0x4000))
#define MCF_INTC_IMRH(x)           MCF_REG32(0xFC048008+((x)*0x4000))
#define MCF_INTC_IMRL(x)           MCF_REG32(0xFC04800C+((x)*0x4000))
#define MCF_INTC_INTFRCH(x)        MCF_REG32(0xFC048010+((x)*0x4000))
#define MCF_INTC_INTFRCL(x)        MCF_REG32(0xFC048014+((x)*0x4000))
#define MCF_INTC_ICONFIG(x)        MCF_REG16(0xFC04801A+((x)*0x4000))
#define MCF_INTC_SIMR(x)           MCF_REG08(0xFC04801C+((x)*0x4000))
#define MCF_INTC_CIMR(x)           MCF_REG08(0xFC04801D+((x)*0x4000))
#define MCF_INTC_CLMASK(x)         MCF_REG08(0xFC04801E+((x)*0x4000))
#define MCF_INTC_SLMASK(x)         MCF_REG08(0xFC04801F+((x)*0x4000))
#define MCF_INTC_ICR0(x)           MCF_REG08(0xFC048040+((x)*0x4000))
#define MCF_INTC_ICR1(x)           MCF_REG08(0xFC048041+((x)*0x4000))
#define MCF_INTC_ICR2(x)           MCF_REG08(0xFC048042+((x)*0x4000))
#define MCF_INTC_ICR3(x)           MCF_REG08(0xFC048043+((x)*0x4000))
#define MCF_INTC_ICR4(x)           MCF_REG08(0xFC048044+((x)*0x4000))
#define MCF_INTC_ICR5(x)           MCF_REG08(0xFC048045+((x)*0x4000))
#define MCF_INTC_ICR6(x)           MCF_REG08(0xFC048046+((x)*0x4000))
#define MCF_INTC_ICR7(x)           MCF_REG08(0xFC048047+((x)*0x4000))
#define MCF_INTC_ICR8(x)           MCF_REG08(0xFC048048+((x)*0x4000))
#define MCF_INTC_ICR9(x)           MCF_REG08(0xFC048049+((x)*0x4000))
#define MCF_INTC_ICR10(x)          MCF_REG08(0xFC04804A+((x)*0x4000))
#define MCF_INTC_ICR11(x)          MCF_REG08(0xFC04804B+((x)*0x4000))
#define MCF_INTC_ICR12(x)          MCF_REG08(0xFC04804C+((x)*0x4000))
#define MCF_INTC_ICR13(x)          MCF_REG08(0xFC04804D+((x)*0x4000))
#define MCF_INTC_ICR14(x)          MCF_REG08(0xFC04804E+((x)*0x4000))
#define MCF_INTC_ICR15(x)          MCF_REG08(0xFC04804F+((x)*0x4000))
#define MCF_INTC_ICR16(x)          MCF_REG08(0xFC048050+((x)*0x4000))
#define MCF_INTC_ICR17(x)          MCF_REG08(0xFC048051+((x)*0x4000))
#define MCF_INTC_ICR18(x)          MCF_REG08(0xFC048052+((x)*0x4000))
#define MCF_INTC_ICR19(x)          MCF_REG08(0xFC048053+((x)*0x4000))
#define MCF_INTC_ICR20(x)          MCF_REG08(0xFC048054+((x)*0x4000))
#define MCF_INTC_ICR21(x)          MCF_REG08(0xFC048055+((x)*0x4000))
#define MCF_INTC_ICR22(x)          MCF_REG08(0xFC048056+((x)*0x4000))
#define MCF_INTC_ICR23(x)          MCF_REG08(0xFC048057+((x)*0x4000))
#define MCF_INTC_ICR24(x)          MCF_REG08(0xFC048058+((x)*0x4000))
#define MCF_INTC_ICR25(x)          MCF_REG08(0xFC048059+((x)*0x4000))
#define MCF_INTC_ICR26(x)          MCF_REG08(0xFC04805A+((x)*0x4000))
#define MCF_INTC_ICR27(x)          MCF_REG08(0xFC04805B+((x)*0x4000))
#define MCF_INTC_ICR28(x)          MCF_REG08(0xFC04805C+((x)*0x4000))
#define MCF_INTC_ICR29(x)          MCF_REG08(0xFC04805D+((x)*0x4000))
#define MCF_INTC_ICR30(x)          MCF_REG08(0xFC04805E+((x)*0x4000))
#define MCF_INTC_ICR31(x)          MCF_REG08(0xFC04805F+((x)*0x4000))
#define MCF_INTC_ICR32(x)          MCF_REG08(0xFC048060+((x)*0x4000))
#define MCF_INTC_ICR33(x)          MCF_REG08(0xFC048061+((x)*0x4000))
#define MCF_INTC_ICR34(x)          MCF_REG08(0xFC048062+((x)*0x4000))
#define MCF_INTC_ICR35(x)          MCF_REG08(0xFC048063+((x)*0x4000))
#define MCF_INTC_ICR36(x)          MCF_REG08(0xFC048064+((x)*0x4000))
#define MCF_INTC_ICR37(x)          MCF_REG08(0xFC048065+((x)*0x4000))
#define MCF_INTC_ICR38(x)          MCF_REG08(0xFC048066+((x)*0x4000))
#define MCF_INTC_ICR39(x)          MCF_REG08(0xFC048067+((x)*0x4000))
#define MCF_INTC_ICR40(x)          MCF_REG08(0xFC048068+((x)*0x4000))
#define MCF_INTC_ICR41(x)          MCF_REG08(0xFC048069+((x)*0x4000))
#define MCF_INTC_ICR42(x)          MCF_REG08(0xFC04806A+((x)*0x4000))
#define MCF_INTC_ICR43(x)          MCF_REG08(0xFC04806B+((x)*0x4000))
#define MCF_INTC_ICR44(x)          MCF_REG08(0xFC04806C+((x)*0x4000))
#define MCF_INTC_ICR45(x)          MCF_REG08(0xFC04806D+((x)*0x4000))
#define MCF_INTC_ICR46(x)          MCF_REG08(0xFC04806E+((x)*0x4000))
#define MCF_INTC_ICR47(x)          MCF_REG08(0xFC04806F+((x)*0x4000))
#define MCF_INTC_ICR48(x)          MCF_REG08(0xFC048070+((x)*0x4000))
#define MCF_INTC_ICR49(x)          MCF_REG08(0xFC048071+((x)*0x4000))
#define MCF_INTC_ICR50(x)          MCF_REG08(0xFC048072+((x)*0x4000))
#define MCF_INTC_ICR51(x)          MCF_REG08(0xFC048073+((x)*0x4000))
#define MCF_INTC_ICR52(x)          MCF_REG08(0xFC048074+((x)*0x4000))
#define MCF_INTC_ICR53(x)          MCF_REG08(0xFC048075+((x)*0x4000))
#define MCF_INTC_ICR54(x)          MCF_REG08(0xFC048076+((x)*0x4000))
#define MCF_INTC_ICR55(x)          MCF_REG08(0xFC048077+((x)*0x4000))
#define MCF_INTC_ICR56(x)          MCF_REG08(0xFC048078+((x)*0x4000))
#define MCF_INTC_ICR57(x)          MCF_REG08(0xFC048079+((x)*0x4000))
#define MCF_INTC_ICR58(x)          MCF_REG08(0xFC04807A+((x)*0x4000))
#define MCF_INTC_ICR59(x)          MCF_REG08(0xFC04807B+((x)*0x4000))
#define MCF_INTC_ICR60(x)          MCF_REG08(0xFC04807C+((x)*0x4000))
#define MCF_INTC_ICR61(x)          MCF_REG08(0xFC04807D+((x)*0x4000))
#define MCF_INTC_ICR62(x)          MCF_REG08(0xFC04807E+((x)*0x4000))
#define MCF_INTC_ICR63(x)          MCF_REG08(0xFC04807F+((x)*0x4000))
#define MCF_INTC_SWIACK(x)         MCF_REG08(0xFC0480E0+((x)*0x4000))
#define MCF_INTC_L1IACK(x)         MCF_REG08(0xFC0480E4+((x)*0x4000))
#define MCF_INTC_L2IACK(x)         MCF_REG08(0xFC0480E8+((x)*0x4000))
#define MCF_INTC_L3IACK(x)         MCF_REG08(0xFC0480EC+((x)*0x4000))
#define MCF_INTC_L4IACK(x)         MCF_REG08(0xFC0480F0+((x)*0x4000))
#define MCF_INTC_L5IACK(x)         MCF_REG08(0xFC0480F4+((x)*0x4000))
#define MCF_INTC_L6IACK(x)         MCF_REG08(0xFC0480F8+((x)*0x4000))
#define MCF_INTC_L7IACK(x)         MCF_REG08(0xFC0480FC+((x)*0x4000))

/* Bit definitions and macros for MCF_INTC_IPRH */
#define MCF_INTC_IPRH_INT32        (0x00000001)
#define MCF_INTC_IPRH_INT33        (0x00000002)
#define MCF_INTC_IPRH_INT34        (0x00000004)
#define MCF_INTC_IPRH_INT35        (0x00000008)
#define MCF_INTC_IPRH_INT36        (0x00000010)
#define MCF_INTC_IPRH_INT37        (0x00000020)
#define MCF_INTC_IPRH_INT38        (0x00000040)
#define MCF_INTC_IPRH_INT39        (0x00000080)
#define MCF_INTC_IPRH_INT40        (0x00000100)
#define MCF_INTC_IPRH_INT41        (0x00000200)
#define MCF_INTC_IPRH_INT42        (0x00000400)
#define MCF_INTC_IPRH_INT43        (0x00000800)
#define MCF_INTC_IPRH_INT44        (0x00001000)
#define MCF_INTC_IPRH_INT45        (0x00002000)
#define MCF_INTC_IPRH_INT46        (0x00004000)
#define MCF_INTC_IPRH_INT47        (0x00008000)
#define MCF_INTC_IPRH_INT48        (0x00010000)
#define MCF_INTC_IPRH_INT49        (0x00020000)
#define MCF_INTC_IPRH_INT50        (0x00040000)
#define MCF_INTC_IPRH_INT51        (0x00080000)
#define MCF_INTC_IPRH_INT52        (0x00100000)
#define MCF_INTC_IPRH_INT53        (0x00200000)
#define MCF_INTC_IPRH_INT54        (0x00400000)
#define MCF_INTC_IPRH_INT55        (0x00800000)
#define MCF_INTC_IPRH_INT56        (0x01000000)
#define MCF_INTC_IPRH_INT57        (0x02000000)
#define MCF_INTC_IPRH_INT58        (0x04000000)
#define MCF_INTC_IPRH_INT59        (0x08000000)
#define MCF_INTC_IPRH_INT60        (0x10000000)
#define MCF_INTC_IPRH_INT61        (0x20000000)
#define MCF_INTC_IPRH_INT62        (0x40000000)
#define MCF_INTC_IPRH_INT63        (0x80000000)

/* Bit definitions and macros for MCF_INTC_IPRL */
#define MCF_INTC_IPRL_INT0         (0x00000001)
#define MCF_INTC_IPRL_INT1         (0x00000002)
#define MCF_INTC_IPRL_INT2         (0x00000004)
#define MCF_INTC_IPRL_INT3         (0x00000008)
#define MCF_INTC_IPRL_INT4         (0x00000010)
#define MCF_INTC_IPRL_INT5         (0x00000020)
#define MCF_INTC_IPRL_INT6         (0x00000040)
#define MCF_INTC_IPRL_INT7         (0x00000080)
#define MCF_INTC_IPRL_INT8         (0x00000100)
#define MCF_INTC_IPRL_INT9         (0x00000200)
#define MCF_INTC_IPRL_INT10        (0x00000400)
#define MCF_INTC_IPRL_INT11        (0x00000800)
#define MCF_INTC_IPRL_INT12        (0x00001000)
#define MCF_INTC_IPRL_INT13        (0x00002000)
#define MCF_INTC_IPRL_INT14        (0x00004000)
#define MCF_INTC_IPRL_INT15        (0x00008000)
#define MCF_INTC_IPRL_INT16        (0x00010000)
#define MCF_INTC_IPRL_INT17        (0x00020000)
#define MCF_INTC_IPRL_INT18        (0x00040000)
#define MCF_INTC_IPRL_INT19        (0x00080000)
#define MCF_INTC_IPRL_INT20        (0x00100000)
#define MCF_INTC_IPRL_INT21        (0x00200000)
#define MCF_INTC_IPRL_INT22        (0x00400000)
#define MCF_INTC_IPRL_INT23        (0x00800000)
#define MCF_INTC_IPRL_INT24        (0x01000000)
#define MCF_INTC_IPRL_INT25        (0x02000000)
#define MCF_INTC_IPRL_INT26        (0x04000000)
#define MCF_INTC_IPRL_INT27        (0x08000000)
#define MCF_INTC_IPRL_INT28        (0x10000000)
#define MCF_INTC_IPRL_INT29        (0x20000000)
#define MCF_INTC_IPRL_INT30        (0x40000000)
#define MCF_INTC_IPRL_INT31        (0x80000000)

/* Bit definitions and macros for MCF_INTC_IMRH */
#define MCF_INTC_IMRH_INT_MASK32   (0x00000001)
#define MCF_INTC_IMRH_INT_MASK33   (0x00000002)
#define MCF_INTC_IMRH_INT_MASK34   (0x00000004)
#define MCF_INTC_IMRH_INT_MASK35   (0x00000008)
#define MCF_INTC_IMRH_INT_MASK36   (0x00000010)
#define MCF_INTC_IMRH_INT_MASK37   (0x00000020)
#define MCF_INTC_IMRH_INT_MASK38   (0x00000040)
#define MCF_INTC_IMRH_INT_MASK39   (0x00000080)
#define MCF_INTC_IMRH_INT_MASK40   (0x00000100)
#define MCF_INTC_IMRH_INT_MASK41   (0x00000200)
#define MCF_INTC_IMRH_INT_MASK42   (0x00000400)
#define MCF_INTC_IMRH_INT_MASK43   (0x00000800)
#define MCF_INTC_IMRH_INT_MASK44   (0x00001000)
#define MCF_INTC_IMRH_INT_MASK45   (0x00002000)
#define MCF_INTC_IMRH_INT_MASK46   (0x00004000)
#define MCF_INTC_IMRH_INT_MASK47   (0x00008000)
#define MCF_INTC_IMRH_INT_MASK48   (0x00010000)
#define MCF_INTC_IMRH_INT_MASK49   (0x00020000)
#define MCF_INTC_IMRH_INT_MASK50   (0x00040000)
#define MCF_INTC_IMRH_INT_MASK51   (0x00080000)
#define MCF_INTC_IMRH_INT_MASK52   (0x00100000)
#define MCF_INTC_IMRH_INT_MASK53   (0x00200000)
#define MCF_INTC_IMRH_INT_MASK54   (0x00400000)
#define MCF_INTC_IMRH_INT_MASK55   (0x00800000)
#define MCF_INTC_IMRH_INT_MASK56   (0x01000000)
#define MCF_INTC_IMRH_INT_MASK57   (0x02000000)
#define MCF_INTC_IMRH_INT_MASK58   (0x04000000)
#define MCF_INTC_IMRH_INT_MASK59   (0x08000000)
#define MCF_INTC_IMRH_INT_MASK60   (0x10000000)
#define MCF_INTC_IMRH_INT_MASK61   (0x20000000)
#define MCF_INTC_IMRH_INT_MASK62   (0x40000000)
#define MCF_INTC_IMRH_INT_MASK63   (0x80000000)

/* Bit definitions and macros for MCF_INTC_IMRL */
#define MCF_INTC_IMRL_INT_MASK0    (0x00000001)
#define MCF_INTC_IMRL_INT_MASK1    (0x00000002)
#define MCF_INTC_IMRL_INT_MASK2    (0x00000004)
#define MCF_INTC_IMRL_INT_MASK3    (0x00000008)
#define MCF_INTC_IMRL_INT_MASK4    (0x00000010)
#define MCF_INTC_IMRL_INT_MASK5    (0x00000020)
#define MCF_INTC_IMRL_INT_MASK6    (0x00000040)
#define MCF_INTC_IMRL_INT_MASK7    (0x00000080)
#define MCF_INTC_IMRL_INT_MASK8    (0x00000100)
#define MCF_INTC_IMRL_INT_MASK9    (0x00000200)
#define MCF_INTC_IMRL_INT_MASK10   (0x00000400)
#define MCF_INTC_IMRL_INT_MASK11   (0x00000800)
#define MCF_INTC_IMRL_INT_MASK12   (0x00001000)
#define MCF_INTC_IMRL_INT_MASK13   (0x00002000)
#define MCF_INTC_IMRL_INT_MASK14   (0x00004000)
#define MCF_INTC_IMRL_INT_MASK15   (0x00008000)
#define MCF_INTC_IMRL_INT_MASK16   (0x00010000)
#define MCF_INTC_IMRL_INT_MASK17   (0x00020000)
#define MCF_INTC_IMRL_INT_MASK18   (0x00040000)
#define MCF_INTC_IMRL_INT_MASK19   (0x00080000)
#define MCF_INTC_IMRL_INT_MASK20   (0x00100000)
#define MCF_INTC_IMRL_INT_MASK21   (0x00200000)
#define MCF_INTC_IMRL_INT_MASK22   (0x00400000)
#define MCF_INTC_IMRL_INT_MASK23   (0x00800000)
#define MCF_INTC_IMRL_INT_MASK24   (0x01000000)
#define MCF_INTC_IMRL_INT_MASK25   (0x02000000)
#define MCF_INTC_IMRL_INT_MASK26   (0x04000000)
#define MCF_INTC_IMRL_INT_MASK27   (0x08000000)
#define MCF_INTC_IMRL_INT_MASK28   (0x10000000)
#define MCF_INTC_IMRL_INT_MASK29   (0x20000000)
#define MCF_INTC_IMRL_INT_MASK30   (0x40000000)
#define MCF_INTC_IMRL_INT_MASK31   (0x80000000)

/* Bit definitions and macros for MCF_INTC_INTFRCH */
#define MCF_INTC_INTFRCH_INTFRC32  (0x00000001)
#define MCF_INTC_INTFRCH_INTFRC33  (0x00000002)
#define MCF_INTC_INTFRCH_INTFRC34  (0x00000004)
#define MCF_INTC_INTFRCH_INTFRC35  (0x00000008)
#define MCF_INTC_INTFRCH_INTFRC36  (0x00000010)
#define MCF_INTC_INTFRCH_INTFRC37  (0x00000020)
#define MCF_INTC_INTFRCH_INTFRC38  (0x00000040)
#define MCF_INTC_INTFRCH_INTFRC39  (0x00000080)
#define MCF_INTC_INTFRCH_INTFRC40  (0x00000100)
#define MCF_INTC_INTFRCH_INTFRC41  (0x00000200)
#define MCF_INTC_INTFRCH_INTFRC42  (0x00000400)
#define MCF_INTC_INTFRCH_INTFRC43  (0x00000800)
#define MCF_INTC_INTFRCH_INTFRC44  (0x00001000)
#define MCF_INTC_INTFRCH_INTFRC45  (0x00002000)
#define MCF_INTC_INTFRCH_INTFRC46  (0x00004000)
#define MCF_INTC_INTFRCH_INTFRC47  (0x00008000)
#define MCF_INTC_INTFRCH_INTFRC48  (0x00010000)
#define MCF_INTC_INTFRCH_INTFRC49  (0x00020000)
#define MCF_INTC_INTFRCH_INTFRC50  (0x00040000)
#define MCF_INTC_INTFRCH_INTFRC51  (0x00080000)
#define MCF_INTC_INTFRCH_INTFRC52  (0x00100000)
#define MCF_INTC_INTFRCH_INTFRC53  (0x00200000)
#define MCF_INTC_INTFRCH_INTFRC54  (0x00400000)
#define MCF_INTC_INTFRCH_INTFRC55  (0x00800000)
#define MCF_INTC_INTFRCH_INTFRC56  (0x01000000)
#define MCF_INTC_INTFRCH_INTFRC57  (0x02000000)
#define MCF_INTC_INTFRCH_INTFRC58  (0x04000000)
#define MCF_INTC_INTFRCH_INTFRC59  (0x08000000)
#define MCF_INTC_INTFRCH_INTFRC60  (0x10000000)
#define MCF_INTC_INTFRCH_INTFRC61  (0x20000000)
#define MCF_INTC_INTFRCH_INTFRC62  (0x40000000)
#define MCF_INTC_INTFRCH_INTFRC63  (0x80000000)

/* Bit definitions and macros for MCF_INTC_INTFRCL */
#define MCF_INTC_INTFRCL_INTFRC0   (0x00000001)
#define MCF_INTC_INTFRCL_INTFRC1   (0x00000002)
#define MCF_INTC_INTFRCL_INTFRC2   (0x00000004)
#define MCF_INTC_INTFRCL_INTFRC3   (0x00000008)
#define MCF_INTC_INTFRCL_INTFRC4   (0x00000010)
#define MCF_INTC_INTFRCL_INTFRC5   (0x00000020)
#define MCF_INTC_INTFRCL_INTFRC6   (0x00000040)
#define MCF_INTC_INTFRCL_INTFRC7   (0x00000080)
#define MCF_INTC_INTFRCL_INTFRC8   (0x00000100)
#define MCF_INTC_INTFRCL_INTFRC9   (0x00000200)
#define MCF_INTC_INTFRCL_INTFRC10  (0x00000400)
#define MCF_INTC_INTFRCL_INTFRC11  (0x00000800)
#define MCF_INTC_INTFRCL_INTFRC12  (0x00001000)
#define MCF_INTC_INTFRCL_INTFRC13  (0x00002000)
#define MCF_INTC_INTFRCL_INTFRC14  (0x00004000)
#define MCF_INTC_INTFRCL_INTFRC15  (0x00008000)
#define MCF_INTC_INTFRCL_INTFRC16  (0x00010000)
#define MCF_INTC_INTFRCL_INTFRC17  (0x00020000)
#define MCF_INTC_INTFRCL_INTFRC18  (0x00040000)
#define MCF_INTC_INTFRCL_INTFRC19  (0x00080000)
#define MCF_INTC_INTFRCL_INTFRC20  (0x00100000)
#define MCF_INTC_INTFRCL_INTFRC21  (0x00200000)
#define MCF_INTC_INTFRCL_INTFRC22  (0x00400000)
#define MCF_INTC_INTFRCL_INTFRC23  (0x00800000)
#define MCF_INTC_INTFRCL_INTFRC24  (0x01000000)
#define MCF_INTC_INTFRCL_INTFRC25  (0x02000000)
#define MCF_INTC_INTFRCL_INTFRC26  (0x04000000)
#define MCF_INTC_INTFRCL_INTFRC27  (0x08000000)
#define MCF_INTC_INTFRCL_INTFRC28  (0x10000000)
#define MCF_INTC_INTFRCL_INTFRC29  (0x20000000)
#define MCF_INTC_INTFRCL_INTFRC30  (0x40000000)
#define MCF_INTC_INTFRCL_INTFRC31  (0x80000000)

/* Bit definitions and macros for MCF_INTC_ICONFIG */
#define MCF_INTC_ICONFIG_EMASK     (0x0020)
#define MCF_INTC_ICONFIG_ELVLPRI1  (0x0200)
#define MCF_INTC_ICONFIG_ELVLPRI2  (0x0400)
#define MCF_INTC_ICONFIG_ELVLPRI3  (0x0800)
#define MCF_INTC_ICONFIG_ELVLPRI4  (0x1000)
#define MCF_INTC_ICONFIG_ELVLPRI5  (0x2000)
#define MCF_INTC_ICONFIG_ELVLPRI6  (0x4000)
#define MCF_INTC_ICONFIG_ELVLPRI7  (0x8000)

/* Bit definitions and macros for MCF_INTC_SIMR */
#define MCF_INTC_SIMR_SIMR(x)      (((x)&0x7F)<<0)

/* Bit definitions and macros for MCF_INTC_CIMR */
#define MCF_INTC_CIMR_CIMR(x)      (((x)&0x7F)<<0)

/* Bit definitions and macros for MCF_INTC_CLMASK */
#define MCF_INTC_CLMASK_CLMASK(x)  (((x)&0x0F)<<0)

/* Bit definitions and macros for MCF_INTC_SLMASK */
#define MCF_INTC_SLMASK_SLMASK(x)  (((x)&0x0F)<<0)

/* Bit definitions and macros for MCF_INTC_ICR */
#define MCF_INTC_ICR_IL(x)         (((x)&0x07)<<0)

/* Bit definitions and macros for MCF_INTC_SWIACK */
#define MCF_INTC_SWIACK_VECTOR(x)  (((x)&0xFF)<<0)

/* Bit definitions and macros for MCF_INTC_LIACK */
#define MCF_INTC_LIACK_VECTOR(x)   (((x)&0xFF)<<0)

/********************************************************************/
/*********************************************************************
*
* LCD Controller (LCDC)
*
*********************************************************************/

/* Register read/write macros */
#define MCF_LCDC_LSSAR                  MCF_REG32(0xFC0AC000)
#define MCF_LCDC_LSR                    MCF_REG32(0xFC0AC004)
#define MCF_LCDC_LVPWR                  MCF_REG32(0xFC0AC008)
#define MCF_LCDC_LCPR                   MCF_REG32(0xFC0AC00C)
#define MCF_LCDC_LCWHBR                 MCF_REG32(0xFC0AC010)
#define MCF_LCDC_LCCMR                  MCF_REG32(0xFC0AC014)
#define MCF_LCDC_LPCR                   MCF_REG32(0xFC0AC018)
#define MCF_LCDC_LHCR                   MCF_REG32(0xFC0AC01C)
#define MCF_LCDC_LVCR                   MCF_REG32(0xFC0AC020)
#define MCF_LCDC_LPOR                   MCF_REG32(0xFC0AC024)
#define MCF_LCDC_LSCR                   MCF_REG32(0xFC0AC028)
#define MCF_LCDC_LPCCR                  MCF_REG32(0xFC0AC02C)
#define MCF_LCDC_LDCR                   MCF_REG32(0xFC0AC030)
#define MCF_LCDC_LRMCR                  MCF_REG32(0xFC0AC034)
#define MCF_LCDC_LICR                   MCF_REG32(0xFC0AC038)
#define MCF_LCDC_LIER                   MCF_REG32(0xFC0AC03C)
#define MCF_LCDC_LISR                   MCF_REG32(0xFC0AC040)
#define MCF_LCDC_LGWSAR                 MCF_REG32(0xFC0AC050)
#define MCF_LCDC_LGWSR                  MCF_REG32(0xFC0AC054)
#define MCF_LCDC_LGWVPWR                MCF_REG32(0xFC0AC058)
#define MCF_LCDC_LGWPOR                 MCF_REG32(0xFC0AC05C)
#define MCF_LCDC_LGWPR                  MCF_REG32(0xFC0AC060)
#define MCF_LCDC_LGWCR                  MCF_REG32(0xFC0AC064)
#define MCF_LCDC_LGWDCR                 MCF_REG32(0xFC0AC068)
#define MCF_LCDC_BPLUT_BASE             MCF_REG32(0xFC0AC800)
#define MCF_LCDC_GWLUT_BASE             MCF_REG32(0xFC0ACC00)

/* Bit definitions and macros for MCF_LCDC_LSSAR */
#define MCF_LCDC_LSSAR_SSA(x)           (((x)&0x3FFFFFFF)<<2)

/* Bit definitions and macros for MCF_LCDC_LSR */
#define MCF_LCDC_LSR_YMAX(x)            (((x)&0x000003FF)<<0)
#define MCF_LCDC_LSR_XMAX(x)            (((x)&0x0000003F)<<20)

/* Bit definitions and macros for MCF_LCDC_LVPWR */
#define MCF_LCDC_LVPWR_VPW(x)           (((x)&0x000003FF)<<0)

/* Bit definitions and macros for MCF_LCDC_LCPR */
#define MCF_LCDC_LCPR_CYP(x)            (((x)&0x000003FF)<<0)
#define MCF_LCDC_LCPR_CXP(x)            (((x)&0x000003FF)<<16)
#define MCF_LCDC_LCPR_OP                (0x10000000)
#define MCF_LCDC_LCPR_CC(x)             (((x)&0x00000003)<<30)
#define MCF_LCDC_LCPR_CC_TRANSPARENT    (0x00000000)
#define MCF_LCDC_LCPR_CC_OR             (0x40000000)
#define MCF_LCDC_LCPR_CC_XOR            (0x80000000)
#define MCF_LCDC_LCPR_CC_AND            (0xC0000000)
#define MCF_LCDC_LCPR_OP_ON             (0x10000000)
#define MCF_LCDC_LCPR_OP_OFF            (0x00000000)

/* Bit definitions and macros for MCF_LCDC_LCWHBR */
#define MCF_LCDC_LCWHBR_BD(x)           (((x)&0x000000FF)<<0)
#define MCF_LCDC_LCWHBR_CH(x)           (((x)&0x0000001F)<<16)
#define MCF_LCDC_LCWHBR_CW(x)           (((x)&0x0000001F)<<24)
#define MCF_LCDC_LCWHBR_BK_EN           (0x80000000)
#define MCF_LCDC_LCWHBR_BK_EN_ON        (0x80000000)
#define MCF_LCDC_LCWHBR_BK_EN_OFF       (0x00000000)

/* Bit definitions and macros for MCF_LCDC_LCCMR */
#define MCF_LCDC_LCCMR_CUR_COL_B(x)     (((x)&0x0000003F)<<0)
#define MCF_LCDC_LCCMR_CUR_COL_G(x)     (((x)&0x0000003F)<<6)
#define MCF_LCDC_LCCMR_CUR_COL_R(x)     (((x)&0x0000003F)<<12)

/* Bit definitions and macros for MCF_LCDC_LPCR */
#define MCF_LCDC_LPCR_PCD(x)            (((x)&0x0000003F)<<0)
#define MCF_LCDC_LPCR_SHARP             (0x00000040)
#define MCF_LCDC_LPCR_SCLKSEL           (0x00000080)
#define MCF_LCDC_LPCR_ACD(x)            (((x)&0x0000007F)<<8)
#define MCF_LCDC_LPCR_ACDSEL            (0x00008000)
#define MCF_LCDC_LPCR_REV_VS            (0x00010000)
#define MCF_LCDC_LPCR_SWAP_SEL          (0x00020000)
#define MCF_LCDC_LPCR_ENDSEL            (0x00040000)
#define MCF_LCDC_LPCR_SCLKIDLE          (0x00080000)
#define MCF_LCDC_LPCR_OEPOL             (0x00100000)
#define MCF_LCDC_LPCR_CLKPOL            (0x00200000)
#define MCF_LCDC_LPCR_LPPOL             (0x00400000)
#define MCF_LCDC_LPCR_FLM               (0x00800000)
#define MCF_LCDC_LPCR_PIXPOL            (0x01000000)
#define MCF_LCDC_LPCR_BPIX(x)           (((x)&0x00000007)<<25)
#define MCF_LCDC_LPCR_PBSIZ(x)          (((x)&0x00000003)<<28)
#define MCF_LCDC_LPCR_COLOR             (0x40000000)
#define MCF_LCDC_LPCR_TFT               (0x80000000)
#define MCF_LCDC_LPCR_MODE_MONOCGROME   (0x00000000)
#define MCF_LCDC_LPCR_MODE_CSTN         (0x40000000)
#define MCF_LCDC_LPCR_MODE_TFT          (0xC0000000)
#define MCF_LCDC_LPCR_PBSIZ_1           (0x00000000)
#define MCF_LCDC_LPCR_PBSIZ_2           (0x10000000)
#define MCF_LCDC_LPCR_PBSIZ_4           (0x20000000)
#define MCF_LCDC_LPCR_PBSIZ_8           (0x30000000)
#define MCF_LCDC_LPCR_BPIX_1bpp         (0x00000000)
#define MCF_LCDC_LPCR_BPIX_2bpp         (0x02000000)
#define MCF_LCDC_LPCR_BPIX_4bpp         (0x04000000)
#define MCF_LCDC_LPCR_BPIX_8bpp         (0x06000000)
#define MCF_LCDC_LPCR_BPIX_12bpp        (0x08000000)
#define MCF_LCDC_LPCR_BPIX_16bpp        (0x0A000000)
#define MCF_LCDC_LPCR_BPIX_18bpp        (0x0C000000)

#define MCF_LCDC_LPCR_PANEL_TYPE(x)     (((x)&0x00000003)<<30) 

/* Bit definitions and macros for MCF_LCDC_LHCR */
#define MCF_LCDC_LHCR_H_WAIT_2(x)       (((x)&0x000000FF)<<0)
#define MCF_LCDC_LHCR_H_WAIT_1(x)       (((x)&0x000000FF)<<8)
#define MCF_LCDC_LHCR_H_WIDTH(x)        (((x)&0x0000003F)<<26)

/* Bit definitions and macros for MCF_LCDC_LVCR */
#define MCF_LCDC_LVCR_V_WAIT_2(x)       (((x)&0x000000FF)<<0)
#define MCF_LCDC_LVCR_V_WAIT_1(x)       (((x)&0x000000FF)<<8)
#define MCF_LCDC_LVCR_V_WIDTH(x)      (((x)&0x0000003F)<<26)

/* Bit definitions and macros for MCF_LCDC_LPOR */
#define MCF_LCDC_LPOR_POS(x)            (((x)&0x0000001F)<<0)

/* Bit definitions and macros for MCF_LCDC_LPCCR */
#define MCF_LCDC_LPCCR_PW(x)            (((x)&0x000000FF)<<0)
#define MCF_LCDC_LPCCR_CC_EN            (0x00000100)
#define MCF_LCDC_LPCCR_SCR(x)           (((x)&0x00000003)<<9)
#define MCF_LCDC_LPCCR_LDMSK            (0x00008000)
#define MCF_LCDC_LPCCR_CLS_HI_WIDTH(x)  (((x)&0x000001FF)<<16)
#define MCF_LCDC_LPCCR_SCR_LINEPULSE    (0x00000000)
#define MCF_LCDC_LPCCR_SCR_PIXELCLK     (0x00002000)
#define MCF_LCDC_LPCCR_SCR_LCDCLOCK     (0x00004000)

/* Bit definitions and macros for MCF_LCDC_LDCR */
#define MCF_LCDC_LDCR_TM(x)             (((x)&0x0000001F)<<0)
#define MCF_LCDC_LDCR_HM(x)             (((x)&0x0000001F)<<16)
#define MCF_LCDC_LDCR_BURST             (0x80000000)

/* Bit definitions and macros for MCF_LCDC_LRMCR */
#define MCF_LCDC_LRMCR_SEL_REF          (0x00000001)

/* Bit definitions and macros for MCF_LCDC_LICR */
#define MCF_LCDC_LICR_INTCON            (0x00000001)
#define MCF_LCDC_LICR_INTSYN            (0x00000004)
#define MCF_LCDC_LICR_GW_INT_CON        (0x00000010)

/* Bit definitions and macros for MCF_LCDC_LIER */
#define MCF_LCDC_LIER_BOF_EN            (0x00000001)
#define MCF_LCDC_LIER_EOF_EN            (0x00000002)
#define MCF_LCDC_LIER_ERR_RES_EN        (0x00000004)
#define MCF_LCDC_LIER_UDR_ERR_EN        (0x00000008)
#define MCF_LCDC_LIER_GW_BOF_EN         (0x00000010)
#define MCF_LCDC_LIER_GW_EOF_EN         (0x00000020)
#define MCF_LCDC_LIER_GW_ERR_RES_EN     (0x00000040)
#define MCF_LCDC_LIER_GW_UDR_ERR_EN     (0x00000080)

/* Bit definitions and macros for MCF_LCDC_LISR */
#define MCF_LCDC_LISR_BOF               (0x00000001)
#define MCF_LCDC_LISR_EOF               (0x00000002)
#define MCF_LCDC_LISR_ERR_RES           (0x00000004)
#define MCF_LCDC_LISR_UDR_ERR           (0x00000008)
#define MCF_LCDC_LISR_GW_BOF            (0x00000010)
#define MCF_LCDC_LISR_GW_EOF            (0x00000020)
#define MCF_LCDC_LISR_GW_ERR_RES        (0x00000040)
#define MCF_LCDC_LISR_GW_UDR_ERR        (0x00000080)

/* Bit definitions and macros for MCF_LCDC_LGWSAR */
#define MCF_LCDC_LGWSAR_GWSA(x)         (((x)&0x3FFFFFFF)<<2)

/* Bit definitions and macros for MCF_LCDC_LGWSR */
#define MCF_LCDC_LGWSR_GWH(x)           (((x)&0x000003FF)<<0)
#define MCF_LCDC_LGWSR_GWW(x)           (((x)&0x0000003F)<<20)

/* Bit definitions and macros for MCF_LCDC_LGWVPWR */
#define MCF_LCDC_LGWVPWR_GWVPW(x)       (((x)&0x000003FF)<<0)

/* Bit definitions and macros for MCF_LCDC_LGWPOR */
#define MCF_LCDC_LGWPOR_GWPO(x)         (((x)&0x0000001F)<<0)

/* Bit definitions and macros for MCF_LCDC_LGWPR */
#define MCF_LCDC_LGWPR_GWYP(x)          (((x)&0x000003FF)<<0)
#define MCF_LCDC_LGWPR_GWXP(x)          (((x)&0x000003FF)<<16)

/* Bit definitions and macros for MCF_LCDC_LGWCR */
#define MCF_LCDC_LGWCR_GWCKB(x)         (((x)&0x0000003F)<<0)
#define MCF_LCDC_LGWCR_GWCKG(x)         (((x)&0x0000003F)<<6)
#define MCF_LCDC_LGWCR_GWCKR(x)         (((x)&0x0000003F)<<12)
#define MCF_LCDC_LGWCR_GW_RVS           (0x00200000)
#define MCF_LCDC_LGWCR_GWE              (0x00400000)
#define MCF_LCDC_LGWCR_GWCKE            (0x00800000)
#define MCF_LCDC_LGWCR_GWAV(x)          (((x)&0x000000FF)<<24)

/* Bit definitions and macros for MCF_LCDC_LGWDCR */
#define MCF_LCDC_LGWDCR_GWTM(x)         (((x)&0x0000001F)<<0)
#define MCF_LCDC_LGWDCR_GWHM(x)         (((x)&0x0000001F)<<16)
#define MCF_LCDC_LGWDCR_GWBT            (0x80000000)

/* Bit definitions and macros for MCF_LCDC_LSCR */
#define MCF_LCDC_LSCR_PS_RISE_DELAY(x)    (((x)&0x0000003F)<<26)
#define MCF_LCDC_LSCR_CLS_RISE_DELAY(x)   (((x)&0x000000FF)<<16)
#define MCF_LCDC_LSCR_REV_TOGGLE_DELAY(x) (((x)&0x0000000F)<<8)
#define MCF_LCDC_LSCR_GRAY_2(x)  		  (((x)&0x0000000F)<<4)
#define MCF_LCDC_LSCR_GRAY_1(x)  		  (((x)&0x0000000F)<<0)

/* Bit definitions and macros for MCF_LCDC_BPLUT_BASE */
#define MCF_LCDC_BPLUT_BASE_BASE(x)     (((x)&0xFFFFFFFF)<<0)

/* Bit definitions and macros for MCF_LCDC_GWLUT_BASE */
#define MCF_LCDC_GWLUT_BASE_BASE(x)     (((x)&0xFFFFFFFF)<<0)

/*********************************************************************
 *
 * Phase Locked Loop (PLL)
 *
 *********************************************************************/

/* Register read/write macros */
#define MCF_PLL_PODR              MCF_REG08(0xFC0C0000)
#define MCF_PLL_PLLCR             MCF_REG08(0xFC0C0004)
#define MCF_PLL_PMDR              MCF_REG08(0xFC0C0008)
#define MCF_PLL_PFDR              MCF_REG08(0xFC0C000C)

/* Bit definitions and macros for MCF_PLL_PODR */
#define MCF_PLL_PODR_BUSDIV(x)    (((x)&0x0F)<<0)
#define MCF_PLL_PODR_CPUDIV(x)    (((x)&0x0F)<<4)

/* Bit definitions and macros for MCF_PLL_PLLCR */
#define MCF_PLL_PLLCR_DITHDEV(x)  (((x)&0x07)<<0)
#define MCF_PLL_PLLCR_DITHEN      (0x80)

/* Bit definitions and macros for MCF_PLL_PMDR */
#define MCF_PLL_PMDR_MODDIV(x)    (((x)&0xFF)<<0)

/* Bit definitions and macros for MCF_PLL_PFDR */
#define MCF_PLL_PFDR_MFD(x)       (((x)&0xFF)<<0)

/*********************************************************************
 *
 * System Control Module Registers (SCM)
 *
 *********************************************************************/

/* Register read/write macros */
#define MCF_SCM_MPR			MCF_REG32(0xFC000000)
#define MCF_SCM_PACRA			MCF_REG32(0xFC000020)
#define MCF_SCM_PACRB			MCF_REG32(0xFC000024)
#define MCF_SCM_PACRC			MCF_REG32(0xFC000028)
#define MCF_SCM_PACRD			MCF_REG32(0xFC00002C)
#define MCF_SCM_PACRE			MCF_REG32(0xFC000040)
#define MCF_SCM_PACRF			MCF_REG32(0xFC000044)

#define MCF_SCM_BCR			MCF_REG32(0xFC040024)

/*********************************************************************
 *
 * SDRAM Controller (SDRAMC)
 *
 *********************************************************************/

/* Register read/write macros */
#define MCF_SDRAMC_SDMR			MCF_REG32(0xFC0B8000)
#define MCF_SDRAMC_SDCR			MCF_REG32(0xFC0B8004)
#define MCF_SDRAMC_SDCFG1		MCF_REG32(0xFC0B8008)
#define MCF_SDRAMC_SDCFG2		MCF_REG32(0xFC0B800C)
#define MCF_SDRAMC_LIMP_FIX		MCF_REG32(0xFC0B8080)
#define MCF_SDRAMC_SDDS			MCF_REG32(0xFC0B8100)
#define MCF_SDRAMC_SDCS0		MCF_REG32(0xFC0B8110)
#define MCF_SDRAMC_SDCS1		MCF_REG32(0xFC0B8114)
#define MCF_SDRAMC_SDCS2		MCF_REG32(0xFC0B8118)
#define MCF_SDRAMC_SDCS3		MCF_REG32(0xFC0B811C)
#define MCF_SDRAMC_SDCS(x)		MCF_REG32(0xFC0B8110+((x)*0x004))

/* Bit definitions and macros for MCF_SDRAMC_SDMR */
#define MCF_SDRAMC_SDMR_CMD		(0x00010000)
#define MCF_SDRAMC_SDMR_AD(x)		(((x)&0x00000FFF)<<18)
#define MCF_SDRAMC_SDMR_BNKAD(x)	(((x)&0x00000003)<<30)
#define MCF_SDRAMC_SDMR_BNKAD_LMR	(0x00000000)
#define MCF_SDRAMC_SDMR_BNKAD_LEMR	(0x40000000)

/* Bit definitions and macros for MCF_SDRAMC_SDCR */
#define MCF_SDRAMC_SDCR_IPALL		(0x00000002)
#define MCF_SDRAMC_SDCR_IREF		(0x00000004)
#define MCF_SDRAMC_SDCR_DQS_OE(x)	(((x)&0x0000000F)<<8)
#define MCF_SDRAMC_SDCR_PS(x)		(((x)&0x00000003)<<12)
#define MCF_SDRAMC_SDCR_RCNT(x)		(((x)&0x0000003F)<<16)
#define MCF_SDRAMC_SDCR_OE_RULE		(0x00400000)
#define MCF_SDRAMC_SDCR_MUX(x)		(((x)&0x00000003)<<24)
#define MCF_SDRAMC_SDCR_REF		(0x10000000)
#define MCF_SDRAMC_SDCR_DDR		(0x20000000)
#define MCF_SDRAMC_SDCR_CKE		(0x40000000)
#define MCF_SDRAMC_SDCR_MODE_EN		(0x80000000)
#define MCF_SDRAMC_SDCR_PS_16		(0x00002000)
#define MCF_SDRAMC_SDCR_PS_32		(0x00000000)

/* Bit definitions and macros for MCF_SDRAMC_SDCFG1 */
#define MCF_SDRAMC_SDCFG1_WTLAT(x)	(((x)&0x00000007)<<4)
#define MCF_SDRAMC_SDCFG1_REF2ACT(x)	(((x)&0x0000000F)<<8)
#define MCF_SDRAMC_SDCFG1_PRE2ACT(x)	(((x)&0x00000007)<<12)
#define MCF_SDRAMC_SDCFG1_ACT2RW(x)	(((x)&0x00000007)<<16)
#define MCF_SDRAMC_SDCFG1_RDLAT(x)	(((x)&0x0000000F)<<20)
#define MCF_SDRAMC_SDCFG1_SWT2RD(x)	(((x)&0x00000007)<<24)
#define MCF_SDRAMC_SDCFG1_SRD2RW(x)	(((x)&0x0000000F)<<28)

/* Bit definitions and macros for MCF_SDRAMC_SDCFG2 */
#define MCF_SDRAMC_SDCFG2_BL(x)		(((x)&0x0000000F)<<16)
#define MCF_SDRAMC_SDCFG2_BRD2WT(x)	(((x)&0x0000000F)<<20)
#define MCF_SDRAMC_SDCFG2_BWT2RW(x)	(((x)&0x0000000F)<<24)
#define MCF_SDRAMC_SDCFG2_BRD2PRE(x)	(((x)&0x0000000F)<<28)

/* Device Errata - LIMP mode work around */
#define MCF_SDRAMC_REFRESH		(0x40000000)

/* Bit definitions and macros for MCF_SDRAMC_SDDS */
#define MCF_SDRAMC_SDDS_SB_D(x)		(((x)&0x00000003)<<0)
#define MCF_SDRAMC_SDDS_SB_S(x)		(((x)&0x00000003)<<2)
#define MCF_SDRAMC_SDDS_SB_A(x)		(((x)&0x00000003)<<4)
#define MCF_SDRAMC_SDDS_SB_C(x)		(((x)&0x00000003)<<6)
#define MCF_SDRAMC_SDDS_SB_E(x)		(((x)&0x00000003)<<8)

/* Bit definitions and macros for MCF_SDRAMC_SDCS */
#define MCF_SDRAMC_SDCS_CSSZ(x)		(((x)&0x0000001F)<<0)
#define MCF_SDRAMC_SDCS_BASE(x)		(((x)&0x00000FFF)<<20)
#define MCF_SDRAMC_SDCS_BA(x)		((x)&0xFFF00000)
#define MCF_SDRAMC_SDCS_CSSZ_DIABLE	(0x00000000)
#define MCF_SDRAMC_SDCS_CSSZ_1MBYTE	(0x00000013)
#define MCF_SDRAMC_SDCS_CSSZ_2MBYTE	(0x00000014)
#define MCF_SDRAMC_SDCS_CSSZ_4MBYTE	(0x00000015)
#define MCF_SDRAMC_SDCS_CSSZ_8MBYTE	(0x00000016)
#define MCF_SDRAMC_SDCS_CSSZ_16MBYTE	(0x00000017)
#define MCF_SDRAMC_SDCS_CSSZ_32MBYTE	(0x00000018)
#define MCF_SDRAMC_SDCS_CSSZ_64MBYTE	(0x00000019)
#define MCF_SDRAMC_SDCS_CSSZ_128MBYTE	(0x0000001A)
#define MCF_SDRAMC_SDCS_CSSZ_256MBYTE	(0x0000001B)
#define MCF_SDRAMC_SDCS_CSSZ_512MBYTE	(0x0000001C)
#define MCF_SDRAMC_SDCS_CSSZ_1GBYTE	(0x0000001D)
#define MCF_SDRAMC_SDCS_CSSZ_2GBYTE	(0x0000001E)
#define MCF_SDRAMC_SDCS_CSSZ_4GBYTE	(0x0000001F)

/*********************************************************************
 *
 *      FlexCAN module registers
 *
 *********************************************************************/
#define MCF_FLEXCAN_BASEADDR(x)		(0xFC020000+(x)*0x0800)
#define MCF_FLEXCAN_CANMCR(x)		MCF_REG32(0xFC020000+(x)*0x0800+0x00)
#define MCF_FLEXCAN_CANCTRL(x)		MCF_REG32(0xFC020000+(x)*0x0800+0x04)
#define MCF_FLEXCAN_TIMER(x)		MCF_REG32(0xFC020000+(x)*0x0800+0x08)
#define MCF_FLEXCAN_RXGMASK(x)		MCF_REG32(0xFC020000+(x)*0x0800+0x10)
#define MCF_FLEXCAN_RX14MASK(x)		MCF_REG32(0xFC020000+(x)*0x0800+0x14)
#define MCF_FLEXCAN_RX15MASK(x)		MCF_REG32(0xFC020000+(x)*0x0800+0x18)
#define MCF_FLEXCAN_ERRCNT(x)		MCF_REG32(0xFC020000+(x)*0x0800+0x1C)
#define MCF_FLEXCAN_ERRSTAT(x)		MCF_REG32(0xFC020000+(x)*0x0800+0x20)
#define MCF_FLEXCAN_IMASK(x)		MCF_REG32(0xFC020000+(x)*0x0800+0x28)
#define MCF_FLEXCAN_IFLAG(x)		MCF_REG32(0xFC020000+(x)*0x0800+0x30)

#define MCF_FLEXCAN_MB_CNT(x,y)		MCF_REG32(0xFC020080+(x)*0x0800+(y)*0x10+0x0)
#define MCF_FLEXCAN_MB_ID(x,y)		MCF_REG32(0xFC020080+(x)*0x0800+(y)*0x10+0x4)
#define MCF_FLEXCAN_MB_DB(x,y,z)	MCF_REG08(0xFC020080+(x)*0x0800+(y)*0x10+0x8+(z)*0x1)

/*
 *      FlexCAN Module Configuration Register
 */
#define CANMCR_MDIS		(0x80000000)
#define CANMCR_FRZ		(0x40000000)
#define CANMCR_HALT		(0x10000000)
#define CANMCR_SOFTRST		(0x02000000)
#define CANMCR_FRZACK		(0x01000000)
#define CANMCR_SUPV		(0x00800000)
#define CANMCR_MAXMB(x)         ((x)&0x0F)

/*
 *      FlexCAN Control Register
 */
#define CANCTRL_PRESDIV(x)      (((x)&0xFF)<<24)
#define CANCTRL_RJW(x)          (((x)&0x03)<<22)
#define CANCTRL_PSEG1(x)        (((x)&0x07)<<19)
#define CANCTRL_PSEG2(x)        (((x)&0x07)<<16)
#define CANCTRL_BOFFMSK         (0x00008000)
#define CANCTRL_ERRMSK	        (0x00004000)
#define CANCTRL_CLKSRC		(0x00002000)
#define CANCTRL_LPB	        (0x00001000)
#define CANCTRL_SAMP	        (0x00000080)
#define CANCTRL_BOFFREC         (0x00000040)
#define CANCTRL_TSYNC           (0x00000020)
#define CANCTRL_LBUF            (0x00000010)
#define CANCTRL_LOM             (0x00000008)
#define CANCTRL_PROPSEG(x)      ((x)&0x07)

/*
 *      FlexCAN Error Counter Register
 */
#define ERRCNT_RXECTR(x)        (((x)&0xFF)<<8)
#define ERRCNT_TXECTR(x)        ((x)&0xFF)

/*
 *      FlexCAN Error and Status Register
 */
#define ERRSTAT_BITERR(x)       (((x)&0x03)<<14)
#define ERRSTAT_ACKERR           (0x00002000)
#define ERRSTAT_CRCERR           (0x00001000)
#define ERRSTAT_FRMERR           (0x00000800)
#define ERRSTAT_STFERR           (0x00000400)
#define ERRSTAT_TXWRN            (0x00000200)
#define ERRSTAT_RXWRN            (0x00000100)
#define ERRSTAT_IDLE             (0x00000080)
#define ERRSTAT_TXRX             (0x00000040)
#define ERRSTAT_FLTCONF(x)       (((x)&0x03)<<4)
#define ERRSTAT_BOFFINT          (0x00000004)
#define ERRSTAT_ERRINT           (0x00000002)

/*
 *      Interrupt Mask Register
 */
#define IMASK_BUF15M		(0x8000)
#define IMASK_BUF14M		(0x4000)
#define IMASK_BUF13M		(0x2000)
#define IMASK_BUF12M		(0x1000)
#define IMASK_BUF11M		(0x0800)
#define IMASK_BUF10M		(0x0400)
#define IMASK_BUF9M		(0x0200)
#define IMASK_BUF8M		(0x0100)
#define IMASK_BUF7M		(0x0080)
#define IMASK_BUF6M		(0x0040)
#define IMASK_BUF5M		(0x0020)
#define IMASK_BUF4M		(0x0010)
#define IMASK_BUF3M		(0x0008)
#define IMASK_BUF2M		(0x0004)
#define IMASK_BUF1M		(0x0002)
#define IMASK_BUF0M		(0x0001)
#define IMASK_BUFnM(x)		(0x1<<(x))
#define IMASK_BUFF_ENABLE_ALL	(0x1111)
#define IMASK_BUFF_DISABLE_ALL	(0x0000)

/*
 *      Interrupt Flag Register
 */
#define IFLAG_BUF15M		(0x8000)
#define IFLAG_BUF14M		(0x4000)
#define IFLAG_BUF13M		(0x2000)
#define IFLAG_BUF12M		(0x1000)
#define IFLAG_BUF11M		(0x0800)
#define IFLAG_BUF10M		(0x0400)
#define IFLAG_BUF9M		(0x0200)
#define IFLAG_BUF8M		(0x0100)
#define IFLAG_BUF7M		(0x0080)
#define IFLAG_BUF6M		(0x0040)
#define IFLAG_BUF5M		(0x0020)
#define IFLAG_BUF4M		(0x0010)
#define IFLAG_BUF3M		(0x0008)
#define IFLAG_BUF2M		(0x0004)
#define IFLAG_BUF1M		(0x0002)
#define IFLAG_BUF0M		(0x0001)
#define IFLAG_BUFF_SET_ALL	(0xFFFF)
#define IFLAG_BUFF_CLEAR_ALL	(0x0000)
#define IFLAG_BUFnM(x)		(0x1<<(x))

/*
 *      Message Buffers
 */
#define MB_CNT_CODE(x)		(((x)&0x0F)<<24)
#define MB_CNT_SRR		(0x00400000)
#define MB_CNT_IDE		(0x00200000)
#define MB_CNT_RTR		(0x00100000)
#define MB_CNT_LENGTH(x)	(((x)&0x0F)<<16)
#define MB_CNT_TIMESTAMP(x)	((x)&0xFFFF)
#define MB_ID_STD(x)		(((x)&0x07FF)<<18)
#define MB_ID_EXT(x)		((x)&0x3FFFF)

/*********************************************************************
 *
 * Edge Port Module (EPORT)
 *
 *********************************************************************/

/* Register read/write macros */
#define MCFEPORT_EPPAR                (0xFC094000)
#define MCFEPORT_EPDDR                (0xFC094002)
#define MCFEPORT_EPIER                (0xFC094003)
#define MCFEPORT_EPDR                 (0xFC094004)
#define MCFEPORT_EPPDR                (0xFC094005)
#define MCFEPORT_EPFR                 (0xFC094006)

/* Bit definitions and macros for MCF_EPORT_EPPAR */
#define MCF_EPORT_EPPAR_EPPA1(x)       (((x)&0x0003)<<2)
#define MCF_EPORT_EPPAR_EPPA2(x)       (((x)&0x0003)<<4)
#define MCF_EPORT_EPPAR_EPPA3(x)       (((x)&0x0003)<<6)
#define MCF_EPORT_EPPAR_EPPA4(x)       (((x)&0x0003)<<8)
#define MCF_EPORT_EPPAR_EPPA5(x)       (((x)&0x0003)<<10)
#define MCF_EPORT_EPPAR_EPPA6(x)       (((x)&0x0003)<<12)
#define MCF_EPORT_EPPAR_EPPA7(x)       (((x)&0x0003)<<14)
#define MCF_EPORT_EPPAR_LEVEL          (0)
#define MCF_EPORT_EPPAR_RISING         (1)
#define MCF_EPORT_EPPAR_FALLING        (2)
#define MCF_EPORT_EPPAR_BOTH           (3)
#define MCF_EPORT_EPPAR_EPPA7_LEVEL    (0x0000)
#define MCF_EPORT_EPPAR_EPPA7_RISING   (0x4000)
#define MCF_EPORT_EPPAR_EPPA7_FALLING  (0x8000)
#define MCF_EPORT_EPPAR_EPPA7_BOTH     (0xC000)
#define MCF_EPORT_EPPAR_EPPA6_LEVEL    (0x0000)
#define MCF_EPORT_EPPAR_EPPA6_RISING   (0x1000)
#define MCF_EPORT_EPPAR_EPPA6_FALLING  (0x2000)
#define MCF_EPORT_EPPAR_EPPA6_BOTH     (0x3000)
#define MCF_EPORT_EPPAR_EPPA5_LEVEL    (0x0000)
#define MCF_EPORT_EPPAR_EPPA5_RISING   (0x0400)
#define MCF_EPORT_EPPAR_EPPA5_FALLING  (0x0800)
#define MCF_EPORT_EPPAR_EPPA5_BOTH     (0x0C00)
#define MCF_EPORT_EPPAR_EPPA4_LEVEL    (0x0000)
#define MCF_EPORT_EPPAR_EPPA4_RISING   (0x0100)
#define MCF_EPORT_EPPAR_EPPA4_FALLING  (0x0200)
#define MCF_EPORT_EPPAR_EPPA4_BOTH     (0x0300)
#define MCF_EPORT_EPPAR_EPPA3_LEVEL    (0x0000)
#define MCF_EPORT_EPPAR_EPPA3_RISING   (0x0040)
#define MCF_EPORT_EPPAR_EPPA3_FALLING  (0x0080)
#define MCF_EPORT_EPPAR_EPPA3_BOTH     (0x00C0)
#define MCF_EPORT_EPPAR_EPPA2_LEVEL    (0x0000)
#define MCF_EPORT_EPPAR_EPPA2_RISING   (0x0010)
#define MCF_EPORT_EPPAR_EPPA2_FALLING  (0x0020)
#define MCF_EPORT_EPPAR_EPPA2_BOTH     (0x0030)
#define MCF_EPORT_EPPAR_EPPA1_LEVEL    (0x0000)
#define MCF_EPORT_EPPAR_EPPA1_RISING   (0x0004)
#define MCF_EPORT_EPPAR_EPPA1_FALLING  (0x0008)
#define MCF_EPORT_EPPAR_EPPA1_BOTH     (0x000C)

/* Bit definitions and macros for MCF_EPORT_EPDDR */
#define MCF_EPORT_EPDDR_EPDD1          (0x02)
#define MCF_EPORT_EPDDR_EPDD2          (0x04)
#define MCF_EPORT_EPDDR_EPDD3          (0x08)
#define MCF_EPORT_EPDDR_EPDD4          (0x10)
#define MCF_EPORT_EPDDR_EPDD5          (0x20)
#define MCF_EPORT_EPDDR_EPDD6          (0x40)
#define MCF_EPORT_EPDDR_EPDD7          (0x80)

/* Bit definitions and macros for MCF_EPORT_EPIER */
#define MCF_EPORT_EPIER_EPIE1          (0x02)
#define MCF_EPORT_EPIER_EPIE2          (0x04)
#define MCF_EPORT_EPIER_EPIE3          (0x08)
#define MCF_EPORT_EPIER_EPIE4          (0x10)
#define MCF_EPORT_EPIER_EPIE5          (0x20)
#define MCF_EPORT_EPIER_EPIE6          (0x40)
#define MCF_EPORT_EPIER_EPIE7          (0x80)

/* Bit definitions and macros for MCF_EPORT_EPDR */
#define MCF_EPORT_EPDR_EPD1            (0x02)
#define MCF_EPORT_EPDR_EPD2            (0x04)
#define MCF_EPORT_EPDR_EPD3            (0x08)
#define MCF_EPORT_EPDR_EPD4            (0x10)
#define MCF_EPORT_EPDR_EPD5            (0x20)
#define MCF_EPORT_EPDR_EPD6            (0x40)
#define MCF_EPORT_EPDR_EPD7            (0x80)

/* Bit definitions and macros for MCF_EPORT_EPPDR */
#define MCF_EPORT_EPPDR_EPPD1          (0x02)
#define MCF_EPORT_EPPDR_EPPD2          (0x04)
#define MCF_EPORT_EPPDR_EPPD3          (0x08)
#define MCF_EPORT_EPPDR_EPPD4          (0x10)
#define MCF_EPORT_EPPDR_EPPD5          (0x20)
#define MCF_EPORT_EPPDR_EPPD6          (0x40)
#define MCF_EPORT_EPPDR_EPPD7          (0x80)

/* Bit definitions and macros for MCF_EPORT_EPFR */
#define MCF_EPORT_EPFR_EPF1            (0x02)
#define MCF_EPORT_EPFR_EPF2            (0x04)
#define MCF_EPORT_EPFR_EPF3            (0x08)
#define MCF_EPORT_EPFR_EPF4            (0x10)
#define MCF_EPORT_EPFR_EPF5            (0x20)
#define MCF_EPORT_EPFR_EPF6            (0x40)
#define MCF_EPORT_EPFR_EPF7            (0x80)

/********************************************************************/
#endif	/* m532xsim_h */

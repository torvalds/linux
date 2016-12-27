/****************************************************************************/

/*
 *	m53xxsim.h -- ColdFire 5329 registers
 */

/****************************************************************************/
#ifndef	m53xxsim_h
#define	m53xxsim_h
/****************************************************************************/

#define	CPU_NAME		"COLDFIRE(m53xx)"
#define	CPU_INSTR_PER_JIFFY	3
#define	MCF_BUSCLK		(MCF_CLK / 3)

#include <asm/m53xxacr.h>

#define MCFINT_VECBASE      64
#define MCFINT_UART0        26          /* Interrupt number for UART0 */
#define MCFINT_UART1        27          /* Interrupt number for UART1 */
#define MCFINT_UART2        28          /* Interrupt number for UART2 */
#define MCFINT_I2C0         30		/* Interrupt number for I2C */
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

#define	MCF_IRQ_I2C0	    (MCFINT_VECBASE + MCFINT_I2C0)
#define	MCF_IRQ_QSPI	    (MCFINT_VECBASE + MCFINT_QSPI)

#define MCF_WTM_WCR		0xFC098000

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
#define	MCFQSPI_BASE		0xFC05C000	/* Base address of QSPI */
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
#define MCF_CCM_CCR               0xFC0A0004
#define MCF_CCM_RCON              0xFC0A0008
#define MCF_CCM_CIR               0xFC0A000A
#define MCF_CCM_MISCCR            0xFC0A0010
#define MCF_CCM_CDR               0xFC0A0012
#define MCF_CCM_UHCSR             0xFC0A0014
#define MCF_CCM_UOCSR             0xFC0A0016

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
 * FlexBus Chip Selects (FBCS)
 *
 *********************************************************************/

/* Register read/write macros */
#define MCF_FBCS0_CSAR		0xFC008000
#define MCF_FBCS0_CSMR		0xFC008004
#define MCF_FBCS0_CSCR		0xFC008008
#define MCF_FBCS1_CSAR		0xFC00800C
#define MCF_FBCS1_CSMR		0xFC008010
#define MCF_FBCS1_CSCR		0xFC008014
#define MCF_FBCS2_CSAR		0xFC008018
#define MCF_FBCS2_CSMR		0xFC00801C
#define MCF_FBCS2_CSCR		0xFC008020
#define MCF_FBCS3_CSAR		0xFC008024
#define MCF_FBCS3_CSMR		0xFC008028
#define MCF_FBCS3_CSCR		0xFC00802C
#define MCF_FBCS4_CSAR		0xFC008030
#define MCF_FBCS4_CSMR		0xFC008034
#define MCF_FBCS4_CSCR		0xFC008038
#define MCF_FBCS5_CSAR		0xFC00803C
#define MCF_FBCS5_CSMR		0xFC008040
#define MCF_FBCS5_CSCR		0xFC008044

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
#define MCFGPIO_PAR_FEC			(0xFC0A4050)
#define MCFGPIO_PAR_PWM			(0xFC0A4051)
#define MCFGPIO_PAR_BUSCTL		(0xFC0A4052)
#define MCFGPIO_PAR_FECI2C		(0xFC0A4053)
#define MCFGPIO_PAR_BE			(0xFC0A4054)
#define MCFGPIO_PAR_CS			(0xFC0A4055)
#define MCFGPIO_PAR_SSI			(0xFC0A4056)
#define MCFGPIO_PAR_UART		(0xFC0A4058)
#define MCFGPIO_PAR_QSPI		(0xFC0A405A)
#define MCFGPIO_PAR_TIMER		(0xFC0A405C)
#define MCFGPIO_PAR_LCDDATA		(0xFC0A405D)
#define MCFGPIO_PAR_LCDCTL		(0xFC0A405E)
#define MCFGPIO_PAR_IRQ			(0xFC0A4060)
#define MCFGPIO_MSCR_FLEXBUS		(0xFC0A4064)
#define MCFGPIO_MSCR_SDRAM		(0xFC0A4065)
#define MCFGPIO_DSCR_I2C		(0xFC0A4068)
#define MCFGPIO_DSCR_PWM		(0xFC0A4069)
#define MCFGPIO_DSCR_FEC		(0xFC0A406A)
#define MCFGPIO_DSCR_UART		(0xFC0A406B)
#define MCFGPIO_DSCR_QSPI		(0xFC0A406C)
#define MCFGPIO_DSCR_TIMER		(0xFC0A406D)
#define MCFGPIO_DSCR_SSI		(0xFC0A406E)
#define MCFGPIO_DSCR_LCD		(0xFC0A406F)
#define MCFGPIO_DSCR_DEBUG		(0xFC0A4070)
#define MCFGPIO_DSCR_CLKRST		(0xFC0A4071)
#define MCFGPIO_DSCR_IRQ		(0xFC0A4072)

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
 * Phase Locked Loop (PLL)
 *
 *********************************************************************/

/* Register read/write macros */
#define MCF_PLL_PODR              0xFC0C0000
#define MCF_PLL_PLLCR             0xFC0C0004
#define MCF_PLL_PMDR              0xFC0C0008
#define MCF_PLL_PFDR              0xFC0C000C

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
#define MCF_SCM_MPR			0xFC000000
#define MCF_SCM_PACRA			0xFC000020
#define MCF_SCM_PACRB			0xFC000024
#define MCF_SCM_PACRC			0xFC000028
#define MCF_SCM_PACRD			0xFC00002C
#define MCF_SCM_PACRE			0xFC000040
#define MCF_SCM_PACRF			0xFC000044

#define MCF_SCM_BCR			0xFC040024

/*********************************************************************
 *
 * SDRAM Controller (SDRAMC)
 *
 *********************************************************************/

/* Register read/write macros */
#define MCF_SDRAMC_SDMR			0xFC0B8000
#define MCF_SDRAMC_SDCR			0xFC0B8004
#define MCF_SDRAMC_SDCFG1		0xFC0B8008
#define MCF_SDRAMC_SDCFG2		0xFC0B800C
#define MCF_SDRAMC_LIMP_FIX		0xFC0B8080
#define MCF_SDRAMC_SDDS			0xFC0B8100
#define MCF_SDRAMC_SDCS0		0xFC0B8110
#define MCF_SDRAMC_SDCS1		0xFC0B8114
#define MCF_SDRAMC_SDCS2		0xFC0B8118
#define MCF_SDRAMC_SDCS3		0xFC0B811C

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

/*
 * Edge Port Module (EPORT)
 */
#define MCFEPORT_EPPAR                (0xFC094000)
#define MCFEPORT_EPDDR                (0xFC094002)
#define MCFEPORT_EPIER                (0xFC094003)
#define MCFEPORT_EPDR                 (0xFC094004)
#define MCFEPORT_EPPDR                (0xFC094005)
#define MCFEPORT_EPFR                 (0xFC094006)

/*
 * I2C Module
 */
#define	MCFI2C_BASE0			(0xFc058000)
#define	MCFI2C_SIZE0			0x40

/********************************************************************/
#endif	/* m53xxsim_h */

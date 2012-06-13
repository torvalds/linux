/*
 * Copyright 2011 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef _BF60x_IRQ_H_
#define _BF60x_IRQ_H_

#include <mach-common/irq.h>

#undef BFIN_IRQ
#define BFIN_IRQ(x) ((x) + IVG15)

#define NR_PERI_INTS		(5 * 32)

#define IRQ_SEC_ERR		BFIN_IRQ(0)	/* SEC Error */
#define IRQ_CGU_EVT		BFIN_IRQ(1)	/* CGU Event */
#define IRQ_WATCH0		BFIN_IRQ(2)	/* Watchdog0 Interrupt */
#define IRQ_WATCH1		BFIN_IRQ(3)	/* Watchdog1 Interrupt */
#define IRQ_L2CTL0_ECC_ERR	BFIN_IRQ(4)	/* L2 ECC Error */
#define IRQ_L2CTL0_ECC_WARN	BFIN_IRQ(5)	/* L2 ECC Waring */
#define IRQ_C0_DBL_FAULT	BFIN_IRQ(6)	/* Core 0 Double Fault */
#define IRQ_C1_DBL_FAULT	BFIN_IRQ(7)	/* Core 1 Double Fault */
#define IRQ_C0_HW_ERR		BFIN_IRQ(8)	/* Core 0 Hardware Error */
#define IRQ_C1_HW_ERR		BFIN_IRQ(9)	/* Core 1 Hardware Error */
#define IRQ_C0_NMI_L1_PARITY_ERR	BFIN_IRQ(10)	/* Core 0 Unhandled NMI or L1 Memory Parity Error */
#define IRQ_C1_NMI_L1_PARITY_ERR	BFIN_IRQ(11)	/* Core 1 Unhandled NMI or L1 Memory Parity Error */
#define CORE_IRQS		(IRQ_C1_NMI_L1_PARITY_ERR + 1)

#define IRQ_TIMER0		BFIN_IRQ(12)	/* Timer 0 Interrupt */
#define IRQ_TIMER1		BFIN_IRQ(13)	/* Timer 1 Interrupt */
#define IRQ_TIMER2		BFIN_IRQ(14)	/* Timer 2 Interrupt */
#define IRQ_TIMER3		BFIN_IRQ(15)	/* Timer 3 Interrupt */
#define IRQ_TIMER4		BFIN_IRQ(16)	/* Timer 4 Interrupt */
#define IRQ_TIMER5		BFIN_IRQ(17)	/* Timer 5 Interrupt */
#define IRQ_TIMER6		BFIN_IRQ(18)	/* Timer 6 Interrupt */
#define IRQ_TIMER7		BFIN_IRQ(19)	/* Timer 7 Interrupt */
#define IRQ_TIMER_STAT		BFIN_IRQ(20)	/* Timer Block Status */
#define IRQ_PINT0		BFIN_IRQ(21)	/* PINT0 Interrupt */
#define IRQ_PINT1		BFIN_IRQ(22)	/* PINT1 Interrupt */
#define IRQ_PINT2		BFIN_IRQ(23)	/* PINT2 Interrupt */
#define IRQ_PINT3		BFIN_IRQ(24)	/* PINT3 Interrupt */
#define IRQ_PINT4		BFIN_IRQ(25)	/* PINT4 Interrupt */
#define IRQ_PINT5		BFIN_IRQ(26)	/* PINT5 Interrupt */
#define IRQ_CNT			BFIN_IRQ(27)	/* CNT Interrupt */
#define IRQ_PWM0_TRIP		BFIN_IRQ(28)	/* PWM0 Trip Interrupt */
#define IRQ_PWM0_SYNC		BFIN_IRQ(29)	/* PWM0 Sync Interrupt */
#define IRQ_PWM1_TRIP		BFIN_IRQ(30)	/* PWM1 Trip Interrupt */
#define IRQ_PWM1_SYNC		BFIN_IRQ(31)	/* PWM1 Sync Interrupt */
#define IRQ_TWI0		BFIN_IRQ(32)	/* TWI0 Interrupt */
#define IRQ_TWI1		BFIN_IRQ(33)	/* TWI1 Interrupt */
#define IRQ_SOFT0		BFIN_IRQ(34)	/* Software-Driven Interrupt 0 */
#define IRQ_SOFT1		BFIN_IRQ(35)	/* Software-Driven Interrupt 1 */
#define IRQ_SOFT2		BFIN_IRQ(36)	/* Software-Driven Interrupt 2 */
#define IRQ_SOFT3		BFIN_IRQ(37)	/* Software-Driven Interrupt 3 */
#define IRQ_ACM_EVT_MISS	BFIN_IRQ(38)	/* ACM Event Miss */
#define IRQ_ACM_EVT_COMPLETE 	BFIN_IRQ(39)	/* ACM Event Complete */
#define IRQ_CAN0_RX		BFIN_IRQ(40)	/* CAN0 Receive Interrupt */
#define IRQ_CAN0_TX		BFIN_IRQ(41)	/* CAN0 Transmit Interrupt */
#define IRQ_CAN0_STAT		BFIN_IRQ(42)	/* CAN0 Status */
#define IRQ_SPORT0_TX		BFIN_IRQ(43)	/* SPORT0 TX Interrupt (DMA0) */
#define IRQ_SPORT0_TX_STAT	BFIN_IRQ(44)	/* SPORT0 TX Status Interrupt */
#define IRQ_SPORT0_RX		BFIN_IRQ(45)	/* SPORT0 RX Interrupt (DMA1) */
#define IRQ_SPORT0_RX_STAT	BFIN_IRQ(46)	/* SPORT0 RX Status Interrupt */
#define IRQ_SPORT1_TX		BFIN_IRQ(47)	/* SPORT1 TX Interrupt (DMA2) */
#define IRQ_SPORT1_TX_STAT	BFIN_IRQ(48)	/* SPORT1 TX Status Interrupt */
#define IRQ_SPORT1_RX		BFIN_IRQ(49)	/* SPORT1 RX Interrupt (DMA3) */
#define IRQ_SPORT1_RX_STAT	BFIN_IRQ(50)	/* SPORT1 RX Status Interrupt */
#define IRQ_SPORT2_TX		BFIN_IRQ(51)	/* SPORT2 TX Interrupt (DMA4) */
#define IRQ_SPORT2_TX_STAT	BFIN_IRQ(52)	/* SPORT2 TX Status Interrupt */
#define IRQ_SPORT2_RX		BFIN_IRQ(53)	/* SPORT2 RX Interrupt (DMA5) */
#define IRQ_SPORT2_RX_STAT	BFIN_IRQ(54)	/* SPORT2 RX Status Interrupt */
#define IRQ_SPI0_TX		BFIN_IRQ(55)	/* SPI0 TX Interrupt (DMA6) */
#define IRQ_SPI0_RX		BFIN_IRQ(56)	/* SPI0 RX Interrupt (DMA7) */
#define IRQ_SPI0_STAT		BFIN_IRQ(57)	/* SPI0 Status Interrupt */
#define IRQ_SPI1_TX		BFIN_IRQ(58)	/* SPI1 TX Interrupt (DMA8) */
#define IRQ_SPI1_RX		BFIN_IRQ(59)	/* SPI1 RX Interrupt (DMA9) */
#define IRQ_SPI1_STAT		BFIN_IRQ(60)	/* SPI1 Status Interrupt */
#define IRQ_RSI			BFIN_IRQ(61)	/* RSI (DMA10) Interrupt */
#define IRQ_RSI_INT0		BFIN_IRQ(62)	/* RSI Interrupt0 */
#define IRQ_RSI_INT1		BFIN_IRQ(63)	/* RSI Interrupt1 */
#define IRQ_SDU			BFIN_IRQ(64)	/* DMA11 Data (SDU) */
/*       -- RESERVED --             65		   DMA12 Data (Reserved) */
/*       -- RESERVED --             66		   Reserved */
/*       -- RESERVED --             67		   Reserved */
#define IRQ_EMAC0_STAT		BFIN_IRQ(68)	/* EMAC0 Status */
/*       -- RESERVED --             69		   EMAC0 Power (Reserved) */
#define IRQ_EMAC1_STAT		BFIN_IRQ(70)	/* EMAC1 Status */
/*       -- RESERVED --             71		   EMAC1 Power (Reserved) */
#define IRQ_LP0			BFIN_IRQ(72)	/* DMA13 Data (Link Port 0) */
#define IRQ_LP0_STAT		BFIN_IRQ(73)	/* Link Port 0 Status */
#define IRQ_LP1			BFIN_IRQ(74)	/* DMA14 Data (Link Port 1) */
#define IRQ_LP1_STAT		BFIN_IRQ(75)	/* Link Port 1 Status */
#define IRQ_LP2			BFIN_IRQ(76)	/* DMA15 Data (Link Port 2) */
#define IRQ_LP2_STAT		BFIN_IRQ(77)	/* Link Port 2 Status */
#define IRQ_LP3			BFIN_IRQ(78)	/* DMA16 Data(Link Port 3) */
#define IRQ_LP3_STAT		BFIN_IRQ(79)	/* Link Port 3 Status */
#define IRQ_UART0_TX		BFIN_IRQ(80)	/* UART0 TX Interrupt (DMA17) */
#define IRQ_UART0_RX		BFIN_IRQ(81)	/* UART0 RX Interrupt (DMA18) */
#define IRQ_UART0_STAT		BFIN_IRQ(82)	/* UART0 Status(Error) Interrupt */
#define IRQ_UART1_TX		BFIN_IRQ(83)	/* UART1 TX Interrupt (DMA19) */
#define IRQ_UART1_RX		BFIN_IRQ(84)	/* UART1 RX Interrupt (DMA20) */
#define IRQ_UART1_STAT		BFIN_IRQ(85)	/* UART1 Status(Error) Interrupt */
#define IRQ_MDMA0_SRC_CRC0	BFIN_IRQ(86)	/* DMA21 Data (MDMA Stream 0 Source/CRC0 Input Channel) */
#define IRQ_MDMA0_DEST_CRC0	BFIN_IRQ(87)	/* DMA22 Data (MDMA Stream 0 Destination/CRC0 Output Channel) */
#define IRQ_MDMAS0		IRQ_MDMA0_DEST_CRC0
#define IRQ_CRC0_DCNTEXP	BFIN_IRQ(88)	/* CRC0 DATACOUNT Expiration */
#define IRQ_CRC0_ERR		BFIN_IRQ(89)	/* CRC0 Error */
#define IRQ_MDMA1_SRC_CRC1	BFIN_IRQ(90)	/* DMA23 Data (MDMA Stream 1 Source/CRC1 Input Channel) */
#define IRQ_MDMA1_DEST_CRC1	BFIN_IRQ(91)	/* DMA24 Data (MDMA Stream 1 Destination/CRC1 Output Channel) */
#define IRQ_MDMAS1		IRQ_MDMA1_DEST_CRC1
#define IRQ_CRC1_DCNTEXP	BFIN_IRQ(92)	/* CRC1 DATACOUNT Expiration */
#define IRQ_CRC1_ERR		BFIN_IRQ(93)	/* CRC1 Error */
#define IRQ_MDMA2_SRC		BFIN_IRQ(94)	/* DMA25 Data (MDMA Stream 2 Source Channel) */
#define IRQ_MDMA2_DEST		BFIN_IRQ(95)	/* DMA26 Data (MDMA Stream 2 Destination Channel) */
#define IRQ_MDMAS2		IRQ_MDMA2_DEST
#define IRQ_MDMA3_SRC		BFIN_IRQ(96)	/* DMA27 Data (MDMA Stream 3 Source Channel) */
#define IRQ_MDMA3_DEST 		BFIN_IRQ(97)	/* DMA28 Data (MDMA Stream 3 Destination Channel) */
#define IRQ_MDMAS3		IRQ_MDMA3_DEST
#define IRQ_EPPI0_CH0 		BFIN_IRQ(98)	/* DMA29 Data (EPPI0 Channel 0) */
#define IRQ_EPPI0_CH1 		BFIN_IRQ(99)	/* DMA30 Data (EPPI0 Channel 1) */
#define IRQ_EPPI0_STAT		BFIN_IRQ(100)	/* EPPI0 Status */
#define IRQ_EPPI2_CH0		BFIN_IRQ(101)	/* DMA31 Data (EPPI2 Channel 0) */
#define IRQ_EPPI2_CH1		BFIN_IRQ(102)	/* DMA32 Data (EPPI2 Channel 1) */
#define IRQ_EPPI2_STAT		BFIN_IRQ(103)	/* EPPI2 Status */
#define IRQ_EPPI1_CH0		BFIN_IRQ(104)	/* DMA33 Data (EPPI1 Channel 0) */
#define IRQ_EPPI1_CH1		BFIN_IRQ(105)	/* DMA34 Data (EPPI1 Channel 1) */
#define IRQ_EPPI1_STAT		BFIN_IRQ(106)	/* EPPI1 Status */
#define IRQ_PIXC_CH0		BFIN_IRQ(107)	/* DMA35 Data (PIXC Channel 0) */
#define IRQ_PIXC_CH1		BFIN_IRQ(108)	/* DMA36 Data (PIXC Channel 1) */
#define IRQ_PIXC_CH2		BFIN_IRQ(109)	/* DMA37 Data (PIXC Channel 2) */
#define IRQ_PIXC_STAT		BFIN_IRQ(110)	/* PIXC Status */
#define IRQ_PVP_CPDOB		BFIN_IRQ(111)	/* DMA38 Data (PVP0 Camera Pipe Data Out B) */
#define IRQ_PVP_CPDOC		BFIN_IRQ(112)	/* DMA39 Data (PVP0 Camera Pipe Data Out C) */
#define IRQ_PVP_CPSTAT		BFIN_IRQ(113)	/* DMA40 Data (PVP0 Camera Pipe Status Out) */
#define IRQ_PVP_CPCI		BFIN_IRQ(114)	/* DMA41 Data (PVP0 Camera Pipe Control In) */
#define IRQ_PVP_STAT0		BFIN_IRQ(115)	/* PVP0 Status 0 */
#define IRQ_PVP_MPDO		BFIN_IRQ(116)	/* DMA42 Data (PVP0 Memory Pipe Data Out) */
#define IRQ_PVP_MPDI		BFIN_IRQ(117)	/* DMA43 Data (PVP0 Memory Pipe Data In) */
#define IRQ_PVP_MPSTAT		BFIN_IRQ(118)	/* DMA44 Data (PVP0 Memory Pipe Status Out) */
#define IRQ_PVP_MPCI		BFIN_IRQ(119)	/* DMA45 Data (PVP0 Memory Pipe Control In) */
#define IRQ_PVP_CPDOA		BFIN_IRQ(120)	/* DMA46 Data (PVP0 Camera Pipe Data Out A) */
#define IRQ_PVP_STAT1		BFIN_IRQ(121)	/* PVP0 Status 1 */
#define IRQ_USB_STAT		BFIN_IRQ(122)	/* USB Status Interrupt */
#define IRQ_USB_DMA		BFIN_IRQ(123)	/* USB DMA Interrupt */
#define IRQ_TRU_INT0		BFIN_IRQ(124)	/* TRU0 Interrupt 0 */
#define IRQ_TRU_INT1		BFIN_IRQ(125)	/* TRU0 Interrupt 1 */
#define IRQ_TRU_INT2		BFIN_IRQ(126)	/* TRU0 Interrupt 2 */
#define IRQ_TRU_INT3		BFIN_IRQ(127)	/* TRU0 Interrupt 3 */
#define IRQ_DMAC0_ERROR		BFIN_IRQ(128)	/* DMAC0 Status Interrupt */
#define IRQ_CGU0_ERROR		BFIN_IRQ(129)	/* CGU0 Error */
/*       -- RESERVED --             130		   Reserved */
#define IRQ_DPM			BFIN_IRQ(131)	/* DPM0 Event */
/*       -- RESERVED --             132		   Reserved */
#define IRQ_SWU0		BFIN_IRQ(133)	/* SWU0 */
#define IRQ_SWU1		BFIN_IRQ(134)	/* SWU1 */
#define IRQ_SWU2		BFIN_IRQ(135)	/* SWU2 */
#define IRQ_SWU3		BFIN_IRQ(136)	/* SWU3 */
#define IRQ_SWU4		BFIN_IRQ(137)	/* SWU4 */
#define IRQ_SWU5		BFIN_IRQ(138)	/* SWU5 */
#define IRQ_SWU6		BFIN_IRQ(139)	/* SWU6 */

#define SYS_IRQS		IRQ_SWU6

#define BFIN_PA_IRQ(x)		((x) + SYS_IRQS + 1)
#define IRQ_PA0			BFIN_PA_IRQ(0)
#define IRQ_PA1			BFIN_PA_IRQ(1)
#define IRQ_PA2			BFIN_PA_IRQ(2)
#define IRQ_PA3			BFIN_PA_IRQ(3)
#define IRQ_PA4			BFIN_PA_IRQ(4)
#define IRQ_PA5			BFIN_PA_IRQ(5)
#define IRQ_PA6			BFIN_PA_IRQ(6)
#define IRQ_PA7			BFIN_PA_IRQ(7)
#define IRQ_PA8			BFIN_PA_IRQ(8)
#define IRQ_PA9			BFIN_PA_IRQ(9)
#define IRQ_PA10		BFIN_PA_IRQ(10)
#define IRQ_PA11		BFIN_PA_IRQ(11)
#define IRQ_PA12		BFIN_PA_IRQ(12)
#define IRQ_PA13		BFIN_PA_IRQ(13)
#define IRQ_PA14		BFIN_PA_IRQ(14)
#define IRQ_PA15		BFIN_PA_IRQ(15)

#define BFIN_PB_IRQ(x)		((x) + IRQ_PA15 + 1)
#define IRQ_PB0			BFIN_PB_IRQ(0)
#define IRQ_PB1			BFIN_PB_IRQ(1)
#define IRQ_PB2			BFIN_PB_IRQ(2)
#define IRQ_PB3			BFIN_PB_IRQ(3)
#define IRQ_PB4			BFIN_PB_IRQ(4)
#define IRQ_PB5			BFIN_PB_IRQ(5)
#define IRQ_PB6			BFIN_PB_IRQ(6)
#define IRQ_PB7			BFIN_PB_IRQ(7)
#define IRQ_PB8			BFIN_PB_IRQ(8)
#define IRQ_PB9			BFIN_PB_IRQ(9)
#define IRQ_PB10		BFIN_PB_IRQ(10)
#define IRQ_PB11		BFIN_PB_IRQ(11)
#define IRQ_PB12		BFIN_PB_IRQ(12)
#define IRQ_PB13		BFIN_PB_IRQ(13)
#define IRQ_PB14		BFIN_PB_IRQ(14)
#define IRQ_PB15		BFIN_PB_IRQ(15)		/* N/A */

#define BFIN_PC_IRQ(x)		((x) + IRQ_PB15 + 1)
#define IRQ_PC0			BFIN_PC_IRQ(0)
#define IRQ_PC1			BFIN_PC_IRQ(1)
#define IRQ_PC2			BFIN_PC_IRQ(2)
#define IRQ_PC3			BFIN_PC_IRQ(3)
#define IRQ_PC4			BFIN_PC_IRQ(4)
#define IRQ_PC5			BFIN_PC_IRQ(5)
#define IRQ_PC6			BFIN_PC_IRQ(6)
#define IRQ_PC7			BFIN_PC_IRQ(7)
#define IRQ_PC8			BFIN_PC_IRQ(8)
#define IRQ_PC9			BFIN_PC_IRQ(9)
#define IRQ_PC10		BFIN_PC_IRQ(10)
#define IRQ_PC11		BFIN_PC_IRQ(11)
#define IRQ_PC12		BFIN_PC_IRQ(12)
#define IRQ_PC13		BFIN_PC_IRQ(13)
#define IRQ_PC14		BFIN_PC_IRQ(14)		/* N/A */
#define IRQ_PC15		BFIN_PC_IRQ(15)		/* N/A */

#define BFIN_PD_IRQ(x)		((x) + IRQ_PC15 + 1)
#define IRQ_PD0			BFIN_PD_IRQ(0)
#define IRQ_PD1			BFIN_PD_IRQ(1)
#define IRQ_PD2			BFIN_PD_IRQ(2)
#define IRQ_PD3			BFIN_PD_IRQ(3)
#define IRQ_PD4			BFIN_PD_IRQ(4)
#define IRQ_PD5			BFIN_PD_IRQ(5)
#define IRQ_PD6			BFIN_PD_IRQ(6)
#define IRQ_PD7			BFIN_PD_IRQ(7)
#define IRQ_PD8			BFIN_PD_IRQ(8)
#define IRQ_PD9			BFIN_PD_IRQ(9)
#define IRQ_PD10		BFIN_PD_IRQ(10)
#define IRQ_PD11		BFIN_PD_IRQ(11)
#define IRQ_PD12		BFIN_PD_IRQ(12)
#define IRQ_PD13		BFIN_PD_IRQ(13)
#define IRQ_PD14		BFIN_PD_IRQ(14)
#define IRQ_PD15		BFIN_PD_IRQ(15)

#define BFIN_PE_IRQ(x)		((x) + IRQ_PD15 + 1)
#define IRQ_PE0			BFIN_PE_IRQ(0)
#define IRQ_PE1			BFIN_PE_IRQ(1)
#define IRQ_PE2			BFIN_PE_IRQ(2)
#define IRQ_PE3			BFIN_PE_IRQ(3)
#define IRQ_PE4			BFIN_PE_IRQ(4)
#define IRQ_PE5			BFIN_PE_IRQ(5)
#define IRQ_PE6			BFIN_PE_IRQ(6)
#define IRQ_PE7			BFIN_PE_IRQ(7)
#define IRQ_PE8			BFIN_PE_IRQ(8)
#define IRQ_PE9			BFIN_PE_IRQ(9)
#define IRQ_PE10		BFIN_PE_IRQ(10)
#define IRQ_PE11		BFIN_PE_IRQ(11)
#define IRQ_PE12		BFIN_PE_IRQ(12)
#define IRQ_PE13		BFIN_PE_IRQ(13)
#define IRQ_PE14		BFIN_PE_IRQ(14)
#define IRQ_PE15		BFIN_PE_IRQ(15)

#define BFIN_PF_IRQ(x)		((x) + IRQ_PE15 + 1)
#define IRQ_PF0			BFIN_PF_IRQ(0)
#define IRQ_PF1			BFIN_PF_IRQ(1)
#define IRQ_PF2			BFIN_PF_IRQ(2)
#define IRQ_PF3			BFIN_PF_IRQ(3)
#define IRQ_PF4			BFIN_PF_IRQ(4)
#define IRQ_PF5			BFIN_PF_IRQ(5)
#define IRQ_PF6			BFIN_PF_IRQ(6)
#define IRQ_PF7			BFIN_PF_IRQ(7)
#define IRQ_PF8			BFIN_PF_IRQ(8)
#define IRQ_PF9			BFIN_PF_IRQ(9)
#define IRQ_PF10		BFIN_PF_IRQ(10)
#define IRQ_PF11		BFIN_PF_IRQ(11)
#define IRQ_PF12		BFIN_PF_IRQ(12)
#define IRQ_PF13		BFIN_PF_IRQ(13)
#define IRQ_PF14		BFIN_PF_IRQ(14)
#define IRQ_PF15		BFIN_PF_IRQ(15)

#define BFIN_PG_IRQ(x)		((x) + IRQ_PF15 + 1)
#define IRQ_PG0			BFIN_PG_IRQ(0)
#define IRQ_PG1			BFIN_PG_IRQ(1)
#define IRQ_PG2			BFIN_PG_IRQ(2)
#define IRQ_PG3			BFIN_PG_IRQ(3)
#define IRQ_PG4			BFIN_PG_IRQ(4)
#define IRQ_PG5			BFIN_PG_IRQ(5)
#define IRQ_PG6			BFIN_PG_IRQ(6)
#define IRQ_PG7			BFIN_PG_IRQ(7)
#define IRQ_PG8			BFIN_PG_IRQ(8)
#define IRQ_PG9			BFIN_PG_IRQ(9)
#define IRQ_PG10		BFIN_PG_IRQ(10)
#define IRQ_PG11		BFIN_PG_IRQ(11)
#define IRQ_PG12		BFIN_PG_IRQ(12)
#define IRQ_PG13		BFIN_PG_IRQ(13)
#define IRQ_PG14		BFIN_PG_IRQ(14)
#define IRQ_PG15		BFIN_PG_IRQ(15)

#define GPIO_IRQ_BASE		IRQ_PA0

#define NR_MACH_IRQS		(IRQ_PG15 + 1)

#ifndef __ASSEMBLY__
#include <linux/types.h>

/*
 * bfin pint registers layout
 */
struct bfin_pint_regs {
	u32 mask_set;
	u32 mask_clear;
	u32 request;
	u32 assign;
	u32 edge_set;
	u32 edge_clear;
	u32 invert_set;
	u32 invert_clear;
	u32 pinstate;
	u32 latch;
	u32 __pad0[2];
};

#endif

#endif

/*
 * arch/arm/mach-sa1100/include/mach/irqs.h
 *
 * Copyright (C) 1996 Russell King
 * Copyright (C) 1998 Deborah Wallach (updates for SA1100/Brutus).
 * Copyright (C) 1999 Nicolas Pitre (full GPIO irq isolation)
 *
 * 2001/11/14	RMK	Cleaned up and standardised a lot of the IRQs.
 */

#define	IRQ_GPIO0		0
#define	IRQ_GPIO1		1
#define	IRQ_GPIO2		2
#define	IRQ_GPIO3		3
#define	IRQ_GPIO4		4
#define	IRQ_GPIO5		5
#define	IRQ_GPIO6		6
#define	IRQ_GPIO7		7
#define	IRQ_GPIO8		8
#define	IRQ_GPIO9		9
#define	IRQ_GPIO10		10
#define	IRQ_GPIO11_27		11
#define	IRQ_LCD  		12	/* LCD controller           */
#define	IRQ_Ser0UDC		13	/* Ser. port 0 UDC          */
#define	IRQ_Ser1SDLC		14	/* Ser. port 1 SDLC         */
#define	IRQ_Ser1UART		15	/* Ser. port 1 UART         */
#define	IRQ_Ser2ICP		16	/* Ser. port 2 ICP          */
#define	IRQ_Ser3UART		17	/* Ser. port 3 UART         */
#define	IRQ_Ser4MCP		18	/* Ser. port 4 MCP          */
#define	IRQ_Ser4SSP		19	/* Ser. port 4 SSP          */
#define	IRQ_DMA0 		20	/* DMA controller channel 0 */
#define	IRQ_DMA1 		21	/* DMA controller channel 1 */
#define	IRQ_DMA2 		22	/* DMA controller channel 2 */
#define	IRQ_DMA3 		23	/* DMA controller channel 3 */
#define	IRQ_DMA4 		24	/* DMA controller channel 4 */
#define	IRQ_DMA5 		25	/* DMA controller channel 5 */
#define	IRQ_OST0 		26	/* OS Timer match 0         */
#define	IRQ_OST1 		27	/* OS Timer match 1         */
#define	IRQ_OST2 		28	/* OS Timer match 2         */
#define	IRQ_OST3 		29	/* OS Timer match 3         */
#define	IRQ_RTC1Hz		30	/* RTC 1 Hz clock           */
#define	IRQ_RTCAlrm		31	/* RTC Alarm                */

#define	IRQ_GPIO11		32
#define	IRQ_GPIO12		33
#define	IRQ_GPIO13		34
#define	IRQ_GPIO14		35
#define	IRQ_GPIO15		36
#define	IRQ_GPIO16		37
#define	IRQ_GPIO17		38
#define	IRQ_GPIO18		39
#define	IRQ_GPIO19		40
#define	IRQ_GPIO20		41
#define	IRQ_GPIO21		42
#define	IRQ_GPIO22		43
#define	IRQ_GPIO23		44
#define	IRQ_GPIO24		45
#define	IRQ_GPIO25		46
#define	IRQ_GPIO26		47
#define	IRQ_GPIO27		48

/*
 * The next 16 interrupts are for board specific purposes.  Since
 * the kernel can only run on one machine at a time, we can re-use
 * these.  If you need more, increase IRQ_BOARD_END, but keep it
 * within sensible limits.  IRQs 49 to 64 are available.
 */
#define IRQ_BOARD_START		49
#define IRQ_BOARD_END		65

/*
 * Figure out the MAX IRQ number.
 *
 * If we have an SA1111, the max IRQ is S1_BVD1_STSCHG+1.
 * If we have an LoCoMo, the max IRQ is IRQ_BOARD_START + 4
 * Otherwise, we have the standard IRQs only.
 */
#ifdef CONFIG_SA1111
#define NR_IRQS			(IRQ_BOARD_END + 55)
#elif defined(CONFIG_SHARP_LOCOMO)
#define NR_IRQS			(IRQ_BOARD_START + 4)
#else
#define NR_IRQS			(IRQ_BOARD_START)
#endif

/*
 * Board specific IRQs.  Define them here.
 * Do not surround them with ifdefs.
 */
#define IRQ_NEPONSET_SMC9196	(IRQ_BOARD_START + 0)
#define IRQ_NEPONSET_USAR	(IRQ_BOARD_START + 1)
#define IRQ_NEPONSET_SA1111	(IRQ_BOARD_START + 2)

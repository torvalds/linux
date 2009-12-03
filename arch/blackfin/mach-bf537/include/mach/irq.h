/*
 * Copyright 2005-2008 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later
 */

#ifndef _BF537_IRQ_H_
#define _BF537_IRQ_H_

/*
 * Interrupt source definitions
 *            Event Source    Core Event Name
 * Core       Emulation               **
 * Events         (highest priority)  EMU         0
 *            Reset                   RST         1
 *            NMI                     NMI         2
 *            Exception               EVX         3
 *            Reserved                --          4
 *            Hardware Error          IVHW        5
 *            Core Timer              IVTMR       6
 *  .....
 *
 *            Softirq		      IVG14
 *            System Call    --
 *               (lowest priority)    IVG15
 */

#define SYS_IRQS        39
#define NR_PERI_INTS    32

/* The ABSTRACT IRQ definitions */
/** the first seven of the following are fixed, the rest you change if you need to **/
#define IRQ_EMU             0	/*Emulation */
#define IRQ_RST             1	/*reset */
#define IRQ_NMI             2	/*Non Maskable */
#define IRQ_EVX             3	/*Exception */
#define IRQ_UNUSED          4	/*- unused interrupt*/
#define IRQ_HWERR           5	/*Hardware Error */
#define IRQ_CORETMR         6	/*Core timer */

#define IRQ_PLL_WAKEUP      7	/*PLL Wakeup Interrupt */
#define IRQ_DMA_ERROR       8	/*DMA Error (general) */
#define IRQ_GENERIC_ERROR   9	/*GENERIC Error Interrupt */
#define IRQ_RTC             10	/*RTC Interrupt */
#define IRQ_PPI             11	/*DMA0 Interrupt (PPI) */
#define IRQ_SPORT0_RX       12	/*DMA3 Interrupt (SPORT0 RX) */
#define IRQ_SPORT0_TX       13	/*DMA4 Interrupt (SPORT0 TX) */
#define IRQ_SPORT1_RX       14	/*DMA5 Interrupt (SPORT1 RX) */
#define IRQ_SPORT1_TX       15	/*DMA6 Interrupt (SPORT1 TX) */
#define IRQ_TWI             16	/*TWI Interrupt */
#define IRQ_SPI             17	/*DMA7 Interrupt (SPI) */
#define IRQ_UART0_RX        18	/*DMA8 Interrupt (UART0 RX) */
#define IRQ_UART0_TX        19	/*DMA9 Interrupt (UART0 TX) */
#define IRQ_UART1_RX        20	/*DMA10 Interrupt (UART1 RX) */
#define IRQ_UART1_TX        21	/*DMA11 Interrupt (UART1 TX) */
#define IRQ_CAN_RX          22	/*CAN Receive Interrupt */
#define IRQ_CAN_TX          23	/*CAN Transmit Interrupt */
#define IRQ_MAC_RX          24	/*DMA1 (Ethernet RX) Interrupt */
#define IRQ_MAC_TX          25	/*DMA2 (Ethernet TX) Interrupt */
#define IRQ_TIMER0            26	/*Timer 0 */
#define IRQ_TIMER1            27	/*Timer 1 */
#define IRQ_TIMER2            28	/*Timer 2 */
#define IRQ_TIMER3            29	/*Timer 3 */
#define IRQ_TIMER4            30	/*Timer 4 */
#define IRQ_TIMER5            31	/*Timer 5 */
#define IRQ_TIMER6            32	/*Timer 6 */
#define IRQ_TIMER7            33	/*Timer 7 */
#define IRQ_PROG_INTA       34	/* PF Ports F&G (PF15:0) Interrupt A */
#define IRQ_PORTG_INTB      35	/* PF Port G (PF15:0) Interrupt B */
#define IRQ_MEM_DMA0        36	/*(Memory DMA Stream 0) */
#define IRQ_MEM_DMA1        37	/*(Memory DMA Stream 1) */
#define IRQ_PROG_INTB	      38	/* PF Ports F (PF15:0) Interrupt B */
#define IRQ_WATCH           38	/*Watch Dog Timer */

#define IRQ_PPI_ERROR       42	/*PPI Error Interrupt */
#define IRQ_CAN_ERROR       43	/*CAN Error Interrupt */
#define IRQ_MAC_ERROR       44	/*PPI Error Interrupt */
#define IRQ_SPORT0_ERROR    45	/*SPORT0 Error Interrupt */
#define IRQ_SPORT1_ERROR    46	/*SPORT1 Error Interrupt */
#define IRQ_SPI_ERROR       47	/*SPI Error Interrupt */
#define IRQ_UART0_ERROR     48	/*UART Error Interrupt */
#define IRQ_UART1_ERROR     49	/*UART Error Interrupt */

#define IRQ_PF0         50
#define IRQ_PF1         51
#define IRQ_PF2         52
#define IRQ_PF3         53
#define IRQ_PF4         54
#define IRQ_PF5         55
#define IRQ_PF6         56
#define IRQ_PF7         57
#define IRQ_PF8         58
#define IRQ_PF9         59
#define IRQ_PF10        60
#define IRQ_PF11        61
#define IRQ_PF12        62
#define IRQ_PF13        63
#define IRQ_PF14        64
#define IRQ_PF15        65

#define IRQ_PG0         66
#define IRQ_PG1         67
#define IRQ_PG2         68
#define IRQ_PG3         69
#define IRQ_PG4         70
#define IRQ_PG5         71
#define IRQ_PG6         72
#define IRQ_PG7         73
#define IRQ_PG8         74
#define IRQ_PG9         75
#define IRQ_PG10        76
#define IRQ_PG11        77
#define IRQ_PG12        78
#define IRQ_PG13        79
#define IRQ_PG14        80
#define IRQ_PG15        81

#define IRQ_PH0         82
#define IRQ_PH1         83
#define IRQ_PH2         84
#define IRQ_PH3         85
#define IRQ_PH4         86
#define IRQ_PH5         87
#define IRQ_PH6         88
#define IRQ_PH7         89
#define IRQ_PH8         90
#define IRQ_PH9         91
#define IRQ_PH10        92
#define IRQ_PH11        93
#define IRQ_PH12        94
#define IRQ_PH13        95
#define IRQ_PH14        96
#define IRQ_PH15        97

#define GPIO_IRQ_BASE	IRQ_PF0

#define NR_IRQS     (IRQ_PH15+1)

#define IVG7            7
#define IVG8            8
#define IVG9            9
#define IVG10           10
#define IVG11           11
#define IVG12           12
#define IVG13           13
#define IVG14           14
#define IVG15           15

/* IAR0 BIT FIELDS*/
#define IRQ_PLL_WAKEUP_POS  0
#define IRQ_DMA_ERROR_POS   4
#define IRQ_ERROR_POS       8
#define IRQ_RTC_POS         12
#define IRQ_PPI_POS         16
#define IRQ_SPORT0_RX_POS   20
#define IRQ_SPORT0_TX_POS   24
#define IRQ_SPORT1_RX_POS   28

/* IAR1 BIT FIELDS*/
#define IRQ_SPORT1_TX_POS   0
#define IRQ_TWI_POS         4
#define IRQ_SPI_POS         8
#define IRQ_UART0_RX_POS    12
#define IRQ_UART0_TX_POS    16
#define IRQ_UART1_RX_POS    20
#define IRQ_UART1_TX_POS    24
#define IRQ_CAN_RX_POS      28

/* IAR2 BIT FIELDS*/
#define IRQ_CAN_TX_POS      0
#define IRQ_MAC_RX_POS      4
#define IRQ_MAC_TX_POS      8
#define IRQ_TIMER0_POS        12
#define IRQ_TIMER1_POS        16
#define IRQ_TIMER2_POS        20
#define IRQ_TIMER3_POS        24
#define IRQ_TIMER4_POS        28

/* IAR3 BIT FIELDS*/
#define IRQ_TIMER5_POS        0
#define IRQ_TIMER6_POS        4
#define IRQ_TIMER7_POS        8
#define IRQ_PROG_INTA_POS   12
#define IRQ_PORTG_INTB_POS   16
#define IRQ_MEM_DMA0_POS    20
#define IRQ_MEM_DMA1_POS    24
#define IRQ_WATCH_POS       28

#endif				/* _BF537_IRQ_H_ */

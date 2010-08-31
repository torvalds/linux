/*
 * Copyright 2008 Analog Devices Inc.
 *
 * Licensed under the ADI BSD license or the GPL-2 (or later)
 */

#ifndef _DEF_BF51X_H
#define _DEF_BF51X_H


/* ************************************************************** */
/*   SYSTEM & MMR ADDRESS DEFINITIONS COMMON TO ALL ADSP-BF51x    */
/* ************************************************************** */

/* Clock and System Control	(0xFFC00000 - 0xFFC000FF)								*/
#define PLL_CTL				0xFFC00000	/* PLL Control Register						*/
#define PLL_DIV				0xFFC00004	/* PLL Divide Register						*/
#define VR_CTL				0xFFC00008	/* Voltage Regulator Control Register				*/
#define PLL_STAT			0xFFC0000C	/* PLL Status Register						*/
#define PLL_LOCKCNT			0xFFC00010	/* PLL Lock Count Register					*/
#define CHIPID				0xFFC00014	/* Device ID Register */

/* System Interrupt Controller (0xFFC00100 - 0xFFC001FF)								*/
#define SWRST				0xFFC00100	/* Software Reset Register					*/
#define SYSCR				0xFFC00104	/* System Configuration Register				*/
#define SIC_RVECT			0xFFC00108	/* Interrupt Reset Vector Address Register			*/

#define SIC_IMASK0			0xFFC0010C	/* Interrupt Mask Register					*/
#define SIC_IAR0			0xFFC00110	/* Interrupt Assignment Register 0				*/
#define SIC_IAR1			0xFFC00114	/* Interrupt Assignment Register 1				*/
#define SIC_IAR2			0xFFC00118	/* Interrupt Assignment Register 2				*/
#define SIC_IAR3			0xFFC0011C	/* Interrupt Assignment Register 3				*/
#define SIC_ISR0			0xFFC00120	/* Interrupt Status Register					*/
#define SIC_IWR0			0xFFC00124	/* Interrupt Wakeup Register					*/

/* SIC Additions to ADSP-BF51x (0xFFC0014C - 0xFFC00162) */
#define SIC_IMASK1                      0xFFC0014C     /* Interrupt Mask register of SIC2 */
#define SIC_IAR4                        0xFFC00150     /* Interrupt Assignment register4 */
#define SIC_IAR5                        0xFFC00154     /* Interrupt Assignment register5 */
#define SIC_IAR6                        0xFFC00158     /* Interrupt Assignment register6 */
#define SIC_IAR7                        0xFFC0015C     /* Interrupt Assignment register7 */
#define SIC_ISR1                        0xFFC00160     /* Interrupt Statur register */
#define SIC_IWR1                        0xFFC00164     /* Interrupt Wakeup register */


/* Watchdog Timer			(0xFFC00200 - 0xFFC002FF)								*/
#define WDOG_CTL			0xFFC00200	/* Watchdog Control Register				*/
#define WDOG_CNT			0xFFC00204	/* Watchdog Count Register					*/
#define WDOG_STAT			0xFFC00208	/* Watchdog Status Register					*/


/* Real Time Clock		(0xFFC00300 - 0xFFC003FF)									*/
#define RTC_STAT			0xFFC00300	/* RTC Status Register						*/
#define RTC_ICTL			0xFFC00304	/* RTC Interrupt Control Register			*/
#define RTC_ISTAT			0xFFC00308	/* RTC Interrupt Status Register			*/
#define RTC_SWCNT			0xFFC0030C	/* RTC Stopwatch Count Register				*/
#define RTC_ALARM			0xFFC00310	/* RTC Alarm Time Register					*/
#define RTC_FAST			0xFFC00314	/* RTC Prescaler Enable Register			*/
#define RTC_PREN			0xFFC00314	/* RTC Prescaler Enable Alternate Macro		*/


/* UART0 Controller		(0xFFC00400 - 0xFFC004FF)									*/
#define UART0_THR			0xFFC00400	/* Transmit Holding register				*/
#define UART0_RBR			0xFFC00400	/* Receive Buffer register					*/
#define UART0_DLL			0xFFC00400	/* Divisor Latch (Low-Byte)					*/
#define UART0_IER			0xFFC00404	/* Interrupt Enable Register				*/
#define UART0_DLH			0xFFC00404	/* Divisor Latch (High-Byte)				*/
#define UART0_IIR			0xFFC00408	/* Interrupt Identification Register		*/
#define UART0_LCR			0xFFC0040C	/* Line Control Register					*/
#define UART0_MCR			0xFFC00410	/* Modem Control Register					*/
#define UART0_LSR			0xFFC00414	/* Line Status Register						*/
#define UART0_MSR			0xFFC00418	/* Modem Status Register					*/
#define UART0_SCR			0xFFC0041C	/* SCR Scratch Register						*/
#define UART0_GCTL			0xFFC00424	/* Global Control Register					*/

/* SPI0 Controller			(0xFFC00500 - 0xFFC005FF)							*/
#define SPI0_REGBASE			0xFFC00500
#define SPI0_CTL			0xFFC00500	/* SPI Control Register						*/
#define SPI0_FLG			0xFFC00504	/* SPI Flag register						*/
#define SPI0_STAT			0xFFC00508	/* SPI Status register						*/
#define SPI0_TDBR			0xFFC0050C	/* SPI Transmit Data Buffer Register				*/
#define SPI0_RDBR			0xFFC00510	/* SPI Receive Data Buffer Register				*/
#define SPI0_BAUD			0xFFC00514	/* SPI Baud rate Register					*/
#define SPI0_SHADOW			0xFFC00518	/* SPI_RDBR Shadow Register					*/

/* SPI1 Controller			(0xFFC03400 - 0xFFC034FF)							*/
#define SPI1_REGBASE			0xFFC03400
#define SPI1_CTL			0xFFC03400	/* SPI Control Register						*/
#define SPI1_FLG			0xFFC03404	/* SPI Flag register						*/
#define SPI1_STAT			0xFFC03408	/* SPI Status register						*/
#define SPI1_TDBR			0xFFC0340C	/* SPI Transmit Data Buffer Register				*/
#define SPI1_RDBR			0xFFC03410	/* SPI Receive Data Buffer Register				*/
#define SPI1_BAUD			0xFFC03414	/* SPI Baud rate Register					*/
#define SPI1_SHADOW			0xFFC03418	/* SPI_RDBR Shadow Register					*/

/* TIMER0-7 Registers		(0xFFC00600 - 0xFFC006FF)								*/
#define TIMER0_CONFIG		0xFFC00600	/* Timer 0 Configuration Register			*/
#define TIMER0_COUNTER		0xFFC00604	/* Timer 0 Counter Register					*/
#define TIMER0_PERIOD		0xFFC00608	/* Timer 0 Period Register					*/
#define TIMER0_WIDTH		0xFFC0060C	/* Timer 0 Width Register					*/

#define TIMER1_CONFIG		0xFFC00610	/* Timer 1 Configuration Register  			*/
#define TIMER1_COUNTER		0xFFC00614	/* Timer 1 Counter Register        			*/
#define TIMER1_PERIOD		0xFFC00618	/* Timer 1 Period Register         			*/
#define TIMER1_WIDTH		0xFFC0061C	/* Timer 1 Width Register          			*/

#define TIMER2_CONFIG		0xFFC00620	/* Timer 2 Configuration Register  			*/
#define TIMER2_COUNTER		0xFFC00624	/* Timer 2 Counter Register        			*/
#define TIMER2_PERIOD		0xFFC00628	/* Timer 2 Period Register         			*/
#define TIMER2_WIDTH		0xFFC0062C	/* Timer 2 Width Register          			*/

#define TIMER3_CONFIG		0xFFC00630	/* Timer 3 Configuration Register			*/
#define TIMER3_COUNTER		0xFFC00634	/* Timer 3 Counter Register					*/
#define TIMER3_PERIOD		0xFFC00638	/* Timer 3 Period Register					*/
#define TIMER3_WIDTH		0xFFC0063C	/* Timer 3 Width Register					*/

#define TIMER4_CONFIG		0xFFC00640	/* Timer 4 Configuration Register  			*/
#define TIMER4_COUNTER		0xFFC00644	/* Timer 4 Counter Register        			*/
#define TIMER4_PERIOD		0xFFC00648	/* Timer 4 Period Register         			*/
#define TIMER4_WIDTH		0xFFC0064C	/* Timer 4 Width Register          			*/

#define TIMER5_CONFIG		0xFFC00650	/* Timer 5 Configuration Register  			*/
#define TIMER5_COUNTER		0xFFC00654	/* Timer 5 Counter Register        			*/
#define TIMER5_PERIOD		0xFFC00658	/* Timer 5 Period Register         			*/
#define TIMER5_WIDTH		0xFFC0065C	/* Timer 5 Width Register          			*/

#define TIMER6_CONFIG		0xFFC00660	/* Timer 6 Configuration Register  			*/
#define TIMER6_COUNTER		0xFFC00664	/* Timer 6 Counter Register        			*/
#define TIMER6_PERIOD		0xFFC00668	/* Timer 6 Period Register         			*/
#define TIMER6_WIDTH		0xFFC0066C	/* Timer 6 Width Register          			*/

#define TIMER7_CONFIG		0xFFC00670	/* Timer 7 Configuration Register  			*/
#define TIMER7_COUNTER		0xFFC00674	/* Timer 7 Counter Register        			*/
#define TIMER7_PERIOD		0xFFC00678	/* Timer 7 Period Register         			*/
#define TIMER7_WIDTH		0xFFC0067C	/* Timer 7 Width Register       			*/

#define TIMER_ENABLE		0xFFC00680	/* Timer Enable Register					*/
#define TIMER_DISABLE		0xFFC00684	/* Timer Disable Register					*/
#define TIMER_STATUS		0xFFC00688	/* Timer Status Register					*/

/* General Purpose I/O Port F (0xFFC00700 - 0xFFC007FF)												*/
#define PORTFIO					0xFFC00700	/* Port F I/O Pin State Specify Register				*/
#define PORTFIO_CLEAR			0xFFC00704	/* Port F I/O Peripheral Interrupt Clear Register		*/
#define PORTFIO_SET				0xFFC00708	/* Port F I/O Peripheral Interrupt Set Register			*/
#define PORTFIO_TOGGLE			0xFFC0070C	/* Port F I/O Pin State Toggle Register					*/
#define PORTFIO_MASKA			0xFFC00710	/* Port F I/O Mask State Specify Interrupt A Register	*/
#define PORTFIO_MASKA_CLEAR		0xFFC00714	/* Port F I/O Mask Disable Interrupt A Register			*/
#define PORTFIO_MASKA_SET		0xFFC00718	/* Port F I/O Mask Enable Interrupt A Register			*/
#define PORTFIO_MASKA_TOGGLE	0xFFC0071C	/* Port F I/O Mask Toggle Enable Interrupt A Register	*/
#define PORTFIO_MASKB			0xFFC00720	/* Port F I/O Mask State Specify Interrupt B Register	*/
#define PORTFIO_MASKB_CLEAR		0xFFC00724	/* Port F I/O Mask Disable Interrupt B Register			*/
#define PORTFIO_MASKB_SET		0xFFC00728	/* Port F I/O Mask Enable Interrupt B Register			*/
#define PORTFIO_MASKB_TOGGLE	0xFFC0072C	/* Port F I/O Mask Toggle Enable Interrupt B Register	*/
#define PORTFIO_DIR				0xFFC00730	/* Port F I/O Direction Register						*/
#define PORTFIO_POLAR			0xFFC00734	/* Port F I/O Source Polarity Register					*/
#define PORTFIO_EDGE			0xFFC00738	/* Port F I/O Source Sensitivity Register				*/
#define PORTFIO_BOTH			0xFFC0073C	/* Port F I/O Set on BOTH Edges Register				*/
#define PORTFIO_INEN			0xFFC00740	/* Port F I/O Input Enable Register 					*/

/* SPORT0 Controller		(0xFFC00800 - 0xFFC008FF)										*/
#define SPORT0_TCR1			0xFFC00800	/* SPORT0 Transmit Configuration 1 Register			*/
#define SPORT0_TCR2			0xFFC00804	/* SPORT0 Transmit Configuration 2 Register			*/
#define SPORT0_TCLKDIV		0xFFC00808	/* SPORT0 Transmit Clock Divider					*/
#define SPORT0_TFSDIV		0xFFC0080C	/* SPORT0 Transmit Frame Sync Divider				*/
#define SPORT0_TX			0xFFC00810	/* SPORT0 TX Data Register							*/
#define SPORT0_RX			0xFFC00818	/* SPORT0 RX Data Register							*/
#define SPORT0_RCR1			0xFFC00820	/* SPORT0 Transmit Configuration 1 Register			*/
#define SPORT0_RCR2			0xFFC00824	/* SPORT0 Transmit Configuration 2 Register			*/
#define SPORT0_RCLKDIV		0xFFC00828	/* SPORT0 Receive Clock Divider						*/
#define SPORT0_RFSDIV		0xFFC0082C	/* SPORT0 Receive Frame Sync Divider				*/
#define SPORT0_STAT			0xFFC00830	/* SPORT0 Status Register							*/
#define SPORT0_CHNL			0xFFC00834	/* SPORT0 Current Channel Register					*/
#define SPORT0_MCMC1		0xFFC00838	/* SPORT0 Multi-Channel Configuration Register 1	*/
#define SPORT0_MCMC2		0xFFC0083C	/* SPORT0 Multi-Channel Configuration Register 2	*/
#define SPORT0_MTCS0		0xFFC00840	/* SPORT0 Multi-Channel Transmit Select Register 0	*/
#define SPORT0_MTCS1		0xFFC00844	/* SPORT0 Multi-Channel Transmit Select Register 1	*/
#define SPORT0_MTCS2		0xFFC00848	/* SPORT0 Multi-Channel Transmit Select Register 2	*/
#define SPORT0_MTCS3		0xFFC0084C	/* SPORT0 Multi-Channel Transmit Select Register 3	*/
#define SPORT0_MRCS0		0xFFC00850	/* SPORT0 Multi-Channel Receive Select Register 0	*/
#define SPORT0_MRCS1		0xFFC00854	/* SPORT0 Multi-Channel Receive Select Register 1	*/
#define SPORT0_MRCS2		0xFFC00858	/* SPORT0 Multi-Channel Receive Select Register 2	*/
#define SPORT0_MRCS3		0xFFC0085C	/* SPORT0 Multi-Channel Receive Select Register 3	*/

/* SPORT1 Controller		(0xFFC00900 - 0xFFC009FF)										*/
#define SPORT1_TCR1			0xFFC00900	/* SPORT1 Transmit Configuration 1 Register			*/
#define SPORT1_TCR2			0xFFC00904	/* SPORT1 Transmit Configuration 2 Register			*/
#define SPORT1_TCLKDIV		0xFFC00908	/* SPORT1 Transmit Clock Divider					*/
#define SPORT1_TFSDIV		0xFFC0090C	/* SPORT1 Transmit Frame Sync Divider				*/
#define SPORT1_TX			0xFFC00910	/* SPORT1 TX Data Register							*/
#define SPORT1_RX			0xFFC00918	/* SPORT1 RX Data Register							*/
#define SPORT1_RCR1			0xFFC00920	/* SPORT1 Transmit Configuration 1 Register			*/
#define SPORT1_RCR2			0xFFC00924	/* SPORT1 Transmit Configuration 2 Register			*/
#define SPORT1_RCLKDIV		0xFFC00928	/* SPORT1 Receive Clock Divider						*/
#define SPORT1_RFSDIV		0xFFC0092C	/* SPORT1 Receive Frame Sync Divider				*/
#define SPORT1_STAT			0xFFC00930	/* SPORT1 Status Register							*/
#define SPORT1_CHNL			0xFFC00934	/* SPORT1 Current Channel Register					*/
#define SPORT1_MCMC1		0xFFC00938	/* SPORT1 Multi-Channel Configuration Register 1	*/
#define SPORT1_MCMC2		0xFFC0093C	/* SPORT1 Multi-Channel Configuration Register 2	*/
#define SPORT1_MTCS0		0xFFC00940	/* SPORT1 Multi-Channel Transmit Select Register 0	*/
#define SPORT1_MTCS1		0xFFC00944	/* SPORT1 Multi-Channel Transmit Select Register 1	*/
#define SPORT1_MTCS2		0xFFC00948	/* SPORT1 Multi-Channel Transmit Select Register 2	*/
#define SPORT1_MTCS3		0xFFC0094C	/* SPORT1 Multi-Channel Transmit Select Register 3	*/
#define SPORT1_MRCS0		0xFFC00950	/* SPORT1 Multi-Channel Receive Select Register 0	*/
#define SPORT1_MRCS1		0xFFC00954	/* SPORT1 Multi-Channel Receive Select Register 1	*/
#define SPORT1_MRCS2		0xFFC00958	/* SPORT1 Multi-Channel Receive Select Register 2	*/
#define SPORT1_MRCS3		0xFFC0095C	/* SPORT1 Multi-Channel Receive Select Register 3	*/

/* External Bus Interface Unit (0xFFC00A00 - 0xFFC00AFF)								*/
#define EBIU_AMGCTL			0xFFC00A00	/* Asynchronous Memory Global Control Register	*/
#define EBIU_AMBCTL0		0xFFC00A04	/* Asynchronous Memory Bank Control Register 0	*/
#define EBIU_AMBCTL1		0xFFC00A08	/* Asynchronous Memory Bank Control Register 1	*/
#define EBIU_SDGCTL			0xFFC00A10	/* SDRAM Global Control Register				*/
#define EBIU_SDBCTL			0xFFC00A14	/* SDRAM Bank Control Register					*/
#define EBIU_SDRRC			0xFFC00A18	/* SDRAM Refresh Rate Control Register			*/
#define EBIU_SDSTAT			0xFFC00A1C	/* SDRAM Status Register						*/

/* DMA Traffic Control Registers													*/
#define DMA_TC_PER			0xFFC00B0C	/* Traffic Control Periods Register			*/
#define DMA_TC_CNT			0xFFC00B10	/* Traffic Control Current Counts Register	*/

/* Alternate deprecated register names (below) provided for backwards code compatibility */
#define DMA_TCPER			0xFFC00B0C	/* Traffic Control Periods Register			*/
#define DMA_TCCNT			0xFFC00B10	/* Traffic Control Current Counts Register	*/

/* DMA Controller (0xFFC00C00 - 0xFFC00FFF)															*/
#define DMA0_NEXT_DESC_PTR		0xFFC00C00	/* DMA Channel 0 Next Descriptor Pointer Register		*/
#define DMA0_START_ADDR			0xFFC00C04	/* DMA Channel 0 Start Address Register					*/
#define DMA0_CONFIG				0xFFC00C08	/* DMA Channel 0 Configuration Register					*/
#define DMA0_X_COUNT			0xFFC00C10	/* DMA Channel 0 X Count Register						*/
#define DMA0_X_MODIFY			0xFFC00C14	/* DMA Channel 0 X Modify Register						*/
#define DMA0_Y_COUNT			0xFFC00C18	/* DMA Channel 0 Y Count Register						*/
#define DMA0_Y_MODIFY			0xFFC00C1C	/* DMA Channel 0 Y Modify Register						*/
#define DMA0_CURR_DESC_PTR		0xFFC00C20	/* DMA Channel 0 Current Descriptor Pointer Register	*/
#define DMA0_CURR_ADDR			0xFFC00C24	/* DMA Channel 0 Current Address Register				*/
#define DMA0_IRQ_STATUS			0xFFC00C28	/* DMA Channel 0 Interrupt/Status Register				*/
#define DMA0_PERIPHERAL_MAP		0xFFC00C2C	/* DMA Channel 0 Peripheral Map Register				*/
#define DMA0_CURR_X_COUNT		0xFFC00C30	/* DMA Channel 0 Current X Count Register				*/
#define DMA0_CURR_Y_COUNT		0xFFC00C38	/* DMA Channel 0 Current Y Count Register				*/

#define DMA1_NEXT_DESC_PTR		0xFFC00C40	/* DMA Channel 1 Next Descriptor Pointer Register		*/
#define DMA1_START_ADDR			0xFFC00C44	/* DMA Channel 1 Start Address Register					*/
#define DMA1_CONFIG				0xFFC00C48	/* DMA Channel 1 Configuration Register					*/
#define DMA1_X_COUNT			0xFFC00C50	/* DMA Channel 1 X Count Register						*/
#define DMA1_X_MODIFY			0xFFC00C54	/* DMA Channel 1 X Modify Register						*/
#define DMA1_Y_COUNT			0xFFC00C58	/* DMA Channel 1 Y Count Register						*/
#define DMA1_Y_MODIFY			0xFFC00C5C	/* DMA Channel 1 Y Modify Register						*/
#define DMA1_CURR_DESC_PTR		0xFFC00C60	/* DMA Channel 1 Current Descriptor Pointer Register	*/
#define DMA1_CURR_ADDR			0xFFC00C64	/* DMA Channel 1 Current Address Register				*/
#define DMA1_IRQ_STATUS			0xFFC00C68	/* DMA Channel 1 Interrupt/Status Register				*/
#define DMA1_PERIPHERAL_MAP		0xFFC00C6C	/* DMA Channel 1 Peripheral Map Register				*/
#define DMA1_CURR_X_COUNT		0xFFC00C70	/* DMA Channel 1 Current X Count Register				*/
#define DMA1_CURR_Y_COUNT		0xFFC00C78	/* DMA Channel 1 Current Y Count Register				*/

#define DMA2_NEXT_DESC_PTR		0xFFC00C80	/* DMA Channel 2 Next Descriptor Pointer Register		*/
#define DMA2_START_ADDR			0xFFC00C84	/* DMA Channel 2 Start Address Register					*/
#define DMA2_CONFIG				0xFFC00C88	/* DMA Channel 2 Configuration Register					*/
#define DMA2_X_COUNT			0xFFC00C90	/* DMA Channel 2 X Count Register						*/
#define DMA2_X_MODIFY			0xFFC00C94	/* DMA Channel 2 X Modify Register						*/
#define DMA2_Y_COUNT			0xFFC00C98	/* DMA Channel 2 Y Count Register						*/
#define DMA2_Y_MODIFY			0xFFC00C9C	/* DMA Channel 2 Y Modify Register						*/
#define DMA2_CURR_DESC_PTR		0xFFC00CA0	/* DMA Channel 2 Current Descriptor Pointer Register	*/
#define DMA2_CURR_ADDR			0xFFC00CA4	/* DMA Channel 2 Current Address Register				*/
#define DMA2_IRQ_STATUS			0xFFC00CA8	/* DMA Channel 2 Interrupt/Status Register				*/
#define DMA2_PERIPHERAL_MAP		0xFFC00CAC	/* DMA Channel 2 Peripheral Map Register				*/
#define DMA2_CURR_X_COUNT		0xFFC00CB0	/* DMA Channel 2 Current X Count Register				*/
#define DMA2_CURR_Y_COUNT		0xFFC00CB8	/* DMA Channel 2 Current Y Count Register				*/

#define DMA3_NEXT_DESC_PTR		0xFFC00CC0	/* DMA Channel 3 Next Descriptor Pointer Register		*/
#define DMA3_START_ADDR			0xFFC00CC4	/* DMA Channel 3 Start Address Register					*/
#define DMA3_CONFIG				0xFFC00CC8	/* DMA Channel 3 Configuration Register					*/
#define DMA3_X_COUNT			0xFFC00CD0	/* DMA Channel 3 X Count Register						*/
#define DMA3_X_MODIFY			0xFFC00CD4	/* DMA Channel 3 X Modify Register						*/
#define DMA3_Y_COUNT			0xFFC00CD8	/* DMA Channel 3 Y Count Register						*/
#define DMA3_Y_MODIFY			0xFFC00CDC	/* DMA Channel 3 Y Modify Register						*/
#define DMA3_CURR_DESC_PTR		0xFFC00CE0	/* DMA Channel 3 Current Descriptor Pointer Register	*/
#define DMA3_CURR_ADDR			0xFFC00CE4	/* DMA Channel 3 Current Address Register				*/
#define DMA3_IRQ_STATUS			0xFFC00CE8	/* DMA Channel 3 Interrupt/Status Register				*/
#define DMA3_PERIPHERAL_MAP		0xFFC00CEC	/* DMA Channel 3 Peripheral Map Register				*/
#define DMA3_CURR_X_COUNT		0xFFC00CF0	/* DMA Channel 3 Current X Count Register				*/
#define DMA3_CURR_Y_COUNT		0xFFC00CF8	/* DMA Channel 3 Current Y Count Register				*/

#define DMA4_NEXT_DESC_PTR		0xFFC00D00	/* DMA Channel 4 Next Descriptor Pointer Register		*/
#define DMA4_START_ADDR			0xFFC00D04	/* DMA Channel 4 Start Address Register					*/
#define DMA4_CONFIG				0xFFC00D08	/* DMA Channel 4 Configuration Register					*/
#define DMA4_X_COUNT			0xFFC00D10	/* DMA Channel 4 X Count Register						*/
#define DMA4_X_MODIFY			0xFFC00D14	/* DMA Channel 4 X Modify Register						*/
#define DMA4_Y_COUNT			0xFFC00D18	/* DMA Channel 4 Y Count Register						*/
#define DMA4_Y_MODIFY			0xFFC00D1C	/* DMA Channel 4 Y Modify Register						*/
#define DMA4_CURR_DESC_PTR		0xFFC00D20	/* DMA Channel 4 Current Descriptor Pointer Register	*/
#define DMA4_CURR_ADDR			0xFFC00D24	/* DMA Channel 4 Current Address Register				*/
#define DMA4_IRQ_STATUS			0xFFC00D28	/* DMA Channel 4 Interrupt/Status Register				*/
#define DMA4_PERIPHERAL_MAP		0xFFC00D2C	/* DMA Channel 4 Peripheral Map Register				*/
#define DMA4_CURR_X_COUNT		0xFFC00D30	/* DMA Channel 4 Current X Count Register				*/
#define DMA4_CURR_Y_COUNT		0xFFC00D38	/* DMA Channel 4 Current Y Count Register				*/

#define DMA5_NEXT_DESC_PTR		0xFFC00D40	/* DMA Channel 5 Next Descriptor Pointer Register		*/
#define DMA5_START_ADDR			0xFFC00D44	/* DMA Channel 5 Start Address Register					*/
#define DMA5_CONFIG				0xFFC00D48	/* DMA Channel 5 Configuration Register					*/
#define DMA5_X_COUNT			0xFFC00D50	/* DMA Channel 5 X Count Register						*/
#define DMA5_X_MODIFY			0xFFC00D54	/* DMA Channel 5 X Modify Register						*/
#define DMA5_Y_COUNT			0xFFC00D58	/* DMA Channel 5 Y Count Register						*/
#define DMA5_Y_MODIFY			0xFFC00D5C	/* DMA Channel 5 Y Modify Register						*/
#define DMA5_CURR_DESC_PTR		0xFFC00D60	/* DMA Channel 5 Current Descriptor Pointer Register	*/
#define DMA5_CURR_ADDR			0xFFC00D64	/* DMA Channel 5 Current Address Register				*/
#define DMA5_IRQ_STATUS			0xFFC00D68	/* DMA Channel 5 Interrupt/Status Register				*/
#define DMA5_PERIPHERAL_MAP		0xFFC00D6C	/* DMA Channel 5 Peripheral Map Register				*/
#define DMA5_CURR_X_COUNT		0xFFC00D70	/* DMA Channel 5 Current X Count Register				*/
#define DMA5_CURR_Y_COUNT		0xFFC00D78	/* DMA Channel 5 Current Y Count Register				*/

#define DMA6_NEXT_DESC_PTR		0xFFC00D80	/* DMA Channel 6 Next Descriptor Pointer Register		*/
#define DMA6_START_ADDR			0xFFC00D84	/* DMA Channel 6 Start Address Register					*/
#define DMA6_CONFIG				0xFFC00D88	/* DMA Channel 6 Configuration Register					*/
#define DMA6_X_COUNT			0xFFC00D90	/* DMA Channel 6 X Count Register						*/
#define DMA6_X_MODIFY			0xFFC00D94	/* DMA Channel 6 X Modify Register						*/
#define DMA6_Y_COUNT			0xFFC00D98	/* DMA Channel 6 Y Count Register						*/
#define DMA6_Y_MODIFY			0xFFC00D9C	/* DMA Channel 6 Y Modify Register						*/
#define DMA6_CURR_DESC_PTR		0xFFC00DA0	/* DMA Channel 6 Current Descriptor Pointer Register	*/
#define DMA6_CURR_ADDR			0xFFC00DA4	/* DMA Channel 6 Current Address Register				*/
#define DMA6_IRQ_STATUS			0xFFC00DA8	/* DMA Channel 6 Interrupt/Status Register				*/
#define DMA6_PERIPHERAL_MAP		0xFFC00DAC	/* DMA Channel 6 Peripheral Map Register				*/
#define DMA6_CURR_X_COUNT		0xFFC00DB0	/* DMA Channel 6 Current X Count Register				*/
#define DMA6_CURR_Y_COUNT		0xFFC00DB8	/* DMA Channel 6 Current Y Count Register				*/

#define DMA7_NEXT_DESC_PTR		0xFFC00DC0	/* DMA Channel 7 Next Descriptor Pointer Register		*/
#define DMA7_START_ADDR			0xFFC00DC4	/* DMA Channel 7 Start Address Register					*/
#define DMA7_CONFIG				0xFFC00DC8	/* DMA Channel 7 Configuration Register					*/
#define DMA7_X_COUNT			0xFFC00DD0	/* DMA Channel 7 X Count Register						*/
#define DMA7_X_MODIFY			0xFFC00DD4	/* DMA Channel 7 X Modify Register						*/
#define DMA7_Y_COUNT			0xFFC00DD8	/* DMA Channel 7 Y Count Register						*/
#define DMA7_Y_MODIFY			0xFFC00DDC	/* DMA Channel 7 Y Modify Register						*/
#define DMA7_CURR_DESC_PTR		0xFFC00DE0	/* DMA Channel 7 Current Descriptor Pointer Register	*/
#define DMA7_CURR_ADDR			0xFFC00DE4	/* DMA Channel 7 Current Address Register				*/
#define DMA7_IRQ_STATUS			0xFFC00DE8	/* DMA Channel 7 Interrupt/Status Register				*/
#define DMA7_PERIPHERAL_MAP		0xFFC00DEC	/* DMA Channel 7 Peripheral Map Register				*/
#define DMA7_CURR_X_COUNT		0xFFC00DF0	/* DMA Channel 7 Current X Count Register				*/
#define DMA7_CURR_Y_COUNT		0xFFC00DF8	/* DMA Channel 7 Current Y Count Register				*/

#define DMA8_NEXT_DESC_PTR		0xFFC00E00	/* DMA Channel 8 Next Descriptor Pointer Register		*/
#define DMA8_START_ADDR			0xFFC00E04	/* DMA Channel 8 Start Address Register					*/
#define DMA8_CONFIG				0xFFC00E08	/* DMA Channel 8 Configuration Register					*/
#define DMA8_X_COUNT			0xFFC00E10	/* DMA Channel 8 X Count Register						*/
#define DMA8_X_MODIFY			0xFFC00E14	/* DMA Channel 8 X Modify Register						*/
#define DMA8_Y_COUNT			0xFFC00E18	/* DMA Channel 8 Y Count Register						*/
#define DMA8_Y_MODIFY			0xFFC00E1C	/* DMA Channel 8 Y Modify Register						*/
#define DMA8_CURR_DESC_PTR		0xFFC00E20	/* DMA Channel 8 Current Descriptor Pointer Register	*/
#define DMA8_CURR_ADDR			0xFFC00E24	/* DMA Channel 8 Current Address Register				*/
#define DMA8_IRQ_STATUS			0xFFC00E28	/* DMA Channel 8 Interrupt/Status Register				*/
#define DMA8_PERIPHERAL_MAP		0xFFC00E2C	/* DMA Channel 8 Peripheral Map Register				*/
#define DMA8_CURR_X_COUNT		0xFFC00E30	/* DMA Channel 8 Current X Count Register				*/
#define DMA8_CURR_Y_COUNT		0xFFC00E38	/* DMA Channel 8 Current Y Count Register				*/

#define DMA9_NEXT_DESC_PTR		0xFFC00E40	/* DMA Channel 9 Next Descriptor Pointer Register		*/
#define DMA9_START_ADDR			0xFFC00E44	/* DMA Channel 9 Start Address Register					*/
#define DMA9_CONFIG				0xFFC00E48	/* DMA Channel 9 Configuration Register					*/
#define DMA9_X_COUNT			0xFFC00E50	/* DMA Channel 9 X Count Register						*/
#define DMA9_X_MODIFY			0xFFC00E54	/* DMA Channel 9 X Modify Register						*/
#define DMA9_Y_COUNT			0xFFC00E58	/* DMA Channel 9 Y Count Register						*/
#define DMA9_Y_MODIFY			0xFFC00E5C	/* DMA Channel 9 Y Modify Register						*/
#define DMA9_CURR_DESC_PTR		0xFFC00E60	/* DMA Channel 9 Current Descriptor Pointer Register	*/
#define DMA9_CURR_ADDR			0xFFC00E64	/* DMA Channel 9 Current Address Register				*/
#define DMA9_IRQ_STATUS			0xFFC00E68	/* DMA Channel 9 Interrupt/Status Register				*/
#define DMA9_PERIPHERAL_MAP		0xFFC00E6C	/* DMA Channel 9 Peripheral Map Register				*/
#define DMA9_CURR_X_COUNT		0xFFC00E70	/* DMA Channel 9 Current X Count Register				*/
#define DMA9_CURR_Y_COUNT		0xFFC00E78	/* DMA Channel 9 Current Y Count Register				*/

#define DMA10_NEXT_DESC_PTR		0xFFC00E80	/* DMA Channel 10 Next Descriptor Pointer Register		*/
#define DMA10_START_ADDR		0xFFC00E84	/* DMA Channel 10 Start Address Register				*/
#define DMA10_CONFIG			0xFFC00E88	/* DMA Channel 10 Configuration Register				*/
#define DMA10_X_COUNT			0xFFC00E90	/* DMA Channel 10 X Count Register						*/
#define DMA10_X_MODIFY			0xFFC00E94	/* DMA Channel 10 X Modify Register						*/
#define DMA10_Y_COUNT			0xFFC00E98	/* DMA Channel 10 Y Count Register						*/
#define DMA10_Y_MODIFY			0xFFC00E9C	/* DMA Channel 10 Y Modify Register						*/
#define DMA10_CURR_DESC_PTR		0xFFC00EA0	/* DMA Channel 10 Current Descriptor Pointer Register	*/
#define DMA10_CURR_ADDR			0xFFC00EA4	/* DMA Channel 10 Current Address Register				*/
#define DMA10_IRQ_STATUS		0xFFC00EA8	/* DMA Channel 10 Interrupt/Status Register				*/
#define DMA10_PERIPHERAL_MAP	0xFFC00EAC	/* DMA Channel 10 Peripheral Map Register				*/
#define DMA10_CURR_X_COUNT		0xFFC00EB0	/* DMA Channel 10 Current X Count Register				*/
#define DMA10_CURR_Y_COUNT		0xFFC00EB8	/* DMA Channel 10 Current Y Count Register				*/

#define DMA11_NEXT_DESC_PTR		0xFFC00EC0	/* DMA Channel 11 Next Descriptor Pointer Register		*/
#define DMA11_START_ADDR		0xFFC00EC4	/* DMA Channel 11 Start Address Register				*/
#define DMA11_CONFIG			0xFFC00EC8	/* DMA Channel 11 Configuration Register				*/
#define DMA11_X_COUNT			0xFFC00ED0	/* DMA Channel 11 X Count Register						*/
#define DMA11_X_MODIFY			0xFFC00ED4	/* DMA Channel 11 X Modify Register						*/
#define DMA11_Y_COUNT			0xFFC00ED8	/* DMA Channel 11 Y Count Register						*/
#define DMA11_Y_MODIFY			0xFFC00EDC	/* DMA Channel 11 Y Modify Register						*/
#define DMA11_CURR_DESC_PTR		0xFFC00EE0	/* DMA Channel 11 Current Descriptor Pointer Register	*/
#define DMA11_CURR_ADDR			0xFFC00EE4	/* DMA Channel 11 Current Address Register				*/
#define DMA11_IRQ_STATUS		0xFFC00EE8	/* DMA Channel 11 Interrupt/Status Register				*/
#define DMA11_PERIPHERAL_MAP	0xFFC00EEC	/* DMA Channel 11 Peripheral Map Register				*/
#define DMA11_CURR_X_COUNT		0xFFC00EF0	/* DMA Channel 11 Current X Count Register				*/
#define DMA11_CURR_Y_COUNT		0xFFC00EF8	/* DMA Channel 11 Current Y Count Register				*/

#define MDMA_D0_NEXT_DESC_PTR	0xFFC00F00	/* MemDMA Stream 0 Destination Next Descriptor Pointer Register		*/
#define MDMA_D0_START_ADDR		0xFFC00F04	/* MemDMA Stream 0 Destination Start Address Register				*/
#define MDMA_D0_CONFIG			0xFFC00F08	/* MemDMA Stream 0 Destination Configuration Register				*/
#define MDMA_D0_X_COUNT			0xFFC00F10	/* MemDMA Stream 0 Destination X Count Register						*/
#define MDMA_D0_X_MODIFY		0xFFC00F14	/* MemDMA Stream 0 Destination X Modify Register					*/
#define MDMA_D0_Y_COUNT			0xFFC00F18	/* MemDMA Stream 0 Destination Y Count Register						*/
#define MDMA_D0_Y_MODIFY		0xFFC00F1C	/* MemDMA Stream 0 Destination Y Modify Register					*/
#define MDMA_D0_CURR_DESC_PTR	0xFFC00F20	/* MemDMA Stream 0 Destination Current Descriptor Pointer Register	*/
#define MDMA_D0_CURR_ADDR		0xFFC00F24	/* MemDMA Stream 0 Destination Current Address Register				*/
#define MDMA_D0_IRQ_STATUS		0xFFC00F28	/* MemDMA Stream 0 Destination Interrupt/Status Register			*/
#define MDMA_D0_PERIPHERAL_MAP	0xFFC00F2C	/* MemDMA Stream 0 Destination Peripheral Map Register				*/
#define MDMA_D0_CURR_X_COUNT	0xFFC00F30	/* MemDMA Stream 0 Destination Current X Count Register				*/
#define MDMA_D0_CURR_Y_COUNT	0xFFC00F38	/* MemDMA Stream 0 Destination Current Y Count Register				*/

#define MDMA_S0_NEXT_DESC_PTR	0xFFC00F40	/* MemDMA Stream 0 Source Next Descriptor Pointer Register			*/
#define MDMA_S0_START_ADDR		0xFFC00F44	/* MemDMA Stream 0 Source Start Address Register					*/
#define MDMA_S0_CONFIG			0xFFC00F48	/* MemDMA Stream 0 Source Configuration Register					*/
#define MDMA_S0_X_COUNT			0xFFC00F50	/* MemDMA Stream 0 Source X Count Register							*/
#define MDMA_S0_X_MODIFY		0xFFC00F54	/* MemDMA Stream 0 Source X Modify Register							*/
#define MDMA_S0_Y_COUNT			0xFFC00F58	/* MemDMA Stream 0 Source Y Count Register							*/
#define MDMA_S0_Y_MODIFY		0xFFC00F5C	/* MemDMA Stream 0 Source Y Modify Register							*/
#define MDMA_S0_CURR_DESC_PTR	0xFFC00F60	/* MemDMA Stream 0 Source Current Descriptor Pointer Register		*/
#define MDMA_S0_CURR_ADDR		0xFFC00F64	/* MemDMA Stream 0 Source Current Address Register					*/
#define MDMA_S0_IRQ_STATUS		0xFFC00F68	/* MemDMA Stream 0 Source Interrupt/Status Register					*/
#define MDMA_S0_PERIPHERAL_MAP	0xFFC00F6C	/* MemDMA Stream 0 Source Peripheral Map Register					*/
#define MDMA_S0_CURR_X_COUNT	0xFFC00F70	/* MemDMA Stream 0 Source Current X Count Register					*/
#define MDMA_S0_CURR_Y_COUNT	0xFFC00F78	/* MemDMA Stream 0 Source Current Y Count Register					*/

#define MDMA_D1_NEXT_DESC_PTR	0xFFC00F80	/* MemDMA Stream 1 Destination Next Descriptor Pointer Register		*/
#define MDMA_D1_START_ADDR		0xFFC00F84	/* MemDMA Stream 1 Destination Start Address Register				*/
#define MDMA_D1_CONFIG			0xFFC00F88	/* MemDMA Stream 1 Destination Configuration Register				*/
#define MDMA_D1_X_COUNT			0xFFC00F90	/* MemDMA Stream 1 Destination X Count Register						*/
#define MDMA_D1_X_MODIFY		0xFFC00F94	/* MemDMA Stream 1 Destination X Modify Register					*/
#define MDMA_D1_Y_COUNT			0xFFC00F98	/* MemDMA Stream 1 Destination Y Count Register						*/
#define MDMA_D1_Y_MODIFY		0xFFC00F9C	/* MemDMA Stream 1 Destination Y Modify Register					*/
#define MDMA_D1_CURR_DESC_PTR	0xFFC00FA0	/* MemDMA Stream 1 Destination Current Descriptor Pointer Register	*/
#define MDMA_D1_CURR_ADDR		0xFFC00FA4	/* MemDMA Stream 1 Destination Current Address Register				*/
#define MDMA_D1_IRQ_STATUS		0xFFC00FA8	/* MemDMA Stream 1 Destination Interrupt/Status Register			*/
#define MDMA_D1_PERIPHERAL_MAP	0xFFC00FAC	/* MemDMA Stream 1 Destination Peripheral Map Register				*/
#define MDMA_D1_CURR_X_COUNT	0xFFC00FB0	/* MemDMA Stream 1 Destination Current X Count Register				*/
#define MDMA_D1_CURR_Y_COUNT	0xFFC00FB8	/* MemDMA Stream 1 Destination Current Y Count Register				*/

#define MDMA_S1_NEXT_DESC_PTR	0xFFC00FC0	/* MemDMA Stream 1 Source Next Descriptor Pointer Register			*/
#define MDMA_S1_START_ADDR		0xFFC00FC4	/* MemDMA Stream 1 Source Start Address Register					*/
#define MDMA_S1_CONFIG			0xFFC00FC8	/* MemDMA Stream 1 Source Configuration Register					*/
#define MDMA_S1_X_COUNT			0xFFC00FD0	/* MemDMA Stream 1 Source X Count Register							*/
#define MDMA_S1_X_MODIFY		0xFFC00FD4	/* MemDMA Stream 1 Source X Modify Register							*/
#define MDMA_S1_Y_COUNT			0xFFC00FD8	/* MemDMA Stream 1 Source Y Count Register							*/
#define MDMA_S1_Y_MODIFY		0xFFC00FDC	/* MemDMA Stream 1 Source Y Modify Register							*/
#define MDMA_S1_CURR_DESC_PTR	0xFFC00FE0	/* MemDMA Stream 1 Source Current Descriptor Pointer Register		*/
#define MDMA_S1_CURR_ADDR		0xFFC00FE4	/* MemDMA Stream 1 Source Current Address Register					*/
#define MDMA_S1_IRQ_STATUS		0xFFC00FE8	/* MemDMA Stream 1 Source Interrupt/Status Register					*/
#define MDMA_S1_PERIPHERAL_MAP	0xFFC00FEC	/* MemDMA Stream 1 Source Peripheral Map Register					*/
#define MDMA_S1_CURR_X_COUNT	0xFFC00FF0	/* MemDMA Stream 1 Source Current X Count Register					*/
#define MDMA_S1_CURR_Y_COUNT	0xFFC00FF8	/* MemDMA Stream 1 Source Current Y Count Register					*/


/* Parallel Peripheral Interface (0xFFC01000 - 0xFFC010FF)				*/
#define PPI_CONTROL			0xFFC01000	/* PPI Control Register			*/
#define PPI_STATUS			0xFFC01004	/* PPI Status Register			*/
#define PPI_COUNT			0xFFC01008	/* PPI Transfer Count Register	*/
#define PPI_DELAY			0xFFC0100C	/* PPI Delay Count Register		*/
#define PPI_FRAME			0xFFC01010	/* PPI Frame Length Register	*/


/* Two-Wire Interface		(0xFFC01400 - 0xFFC014FF)								*/
#define TWI0_REGBASE			0xFFC01400
#define TWI0_CLKDIV			0xFFC01400	/* Serial Clock Divider Register			*/
#define TWI0_CONTROL			0xFFC01404	/* TWI Control Register						*/
#define TWI0_SLAVE_CTL		0xFFC01408	/* Slave Mode Control Register				*/
#define TWI0_SLAVE_STAT		0xFFC0140C	/* Slave Mode Status Register				*/
#define TWI0_SLAVE_ADDR		0xFFC01410	/* Slave Mode Address Register				*/
#define TWI0_MASTER_CTL		0xFFC01414	/* Master Mode Control Register				*/
#define TWI0_MASTER_STAT		0xFFC01418	/* Master Mode Status Register				*/
#define TWI0_MASTER_ADDR		0xFFC0141C	/* Master Mode Address Register				*/
#define TWI0_INT_STAT		0xFFC01420	/* TWI Interrupt Status Register			*/
#define TWI0_INT_MASK		0xFFC01424	/* TWI Master Interrupt Mask Register		*/
#define TWI0_FIFO_CTL		0xFFC01428	/* FIFO Control Register					*/
#define TWI0_FIFO_STAT		0xFFC0142C	/* FIFO Status Register						*/
#define TWI0_XMT_DATA8		0xFFC01480	/* FIFO Transmit Data Single Byte Register	*/
#define TWI0_XMT_DATA16		0xFFC01484	/* FIFO Transmit Data Double Byte Register	*/
#define TWI0_RCV_DATA8		0xFFC01488	/* FIFO Receive Data Single Byte Register	*/
#define TWI0_RCV_DATA16		0xFFC0148C	/* FIFO Receive Data Double Byte Register	*/


/* General Purpose I/O Port G (0xFFC01500 - 0xFFC015FF)												*/
#define PORTGIO					0xFFC01500	/* Port G I/O Pin State Specify Register				*/
#define PORTGIO_CLEAR			0xFFC01504	/* Port G I/O Peripheral Interrupt Clear Register		*/
#define PORTGIO_SET				0xFFC01508	/* Port G I/O Peripheral Interrupt Set Register			*/
#define PORTGIO_TOGGLE			0xFFC0150C	/* Port G I/O Pin State Toggle Register					*/
#define PORTGIO_MASKA			0xFFC01510	/* Port G I/O Mask State Specify Interrupt A Register	*/
#define PORTGIO_MASKA_CLEAR		0xFFC01514	/* Port G I/O Mask Disable Interrupt A Register			*/
#define PORTGIO_MASKA_SET		0xFFC01518	/* Port G I/O Mask Enable Interrupt A Register			*/
#define PORTGIO_MASKA_TOGGLE	0xFFC0151C	/* Port G I/O Mask Toggle Enable Interrupt A Register	*/
#define PORTGIO_MASKB			0xFFC01520	/* Port G I/O Mask State Specify Interrupt B Register	*/
#define PORTGIO_MASKB_CLEAR		0xFFC01524	/* Port G I/O Mask Disable Interrupt B Register			*/
#define PORTGIO_MASKB_SET		0xFFC01528	/* Port G I/O Mask Enable Interrupt B Register			*/
#define PORTGIO_MASKB_TOGGLE	0xFFC0152C	/* Port G I/O Mask Toggle Enable Interrupt B Register	*/
#define PORTGIO_DIR				0xFFC01530	/* Port G I/O Direction Register						*/
#define PORTGIO_POLAR			0xFFC01534	/* Port G I/O Source Polarity Register					*/
#define PORTGIO_EDGE			0xFFC01538	/* Port G I/O Source Sensitivity Register				*/
#define PORTGIO_BOTH			0xFFC0153C	/* Port G I/O Set on BOTH Edges Register				*/
#define PORTGIO_INEN			0xFFC01540	/* Port G I/O Input Enable Register						*/


/* General Purpose I/O Port H (0xFFC01700 - 0xFFC017FF)												*/
#define PORTHIO					0xFFC01700	/* Port H I/O Pin State Specify Register				*/
#define PORTHIO_CLEAR			0xFFC01704	/* Port H I/O Peripheral Interrupt Clear Register		*/
#define PORTHIO_SET				0xFFC01708	/* Port H I/O Peripheral Interrupt Set Register			*/
#define PORTHIO_TOGGLE			0xFFC0170C	/* Port H I/O Pin State Toggle Register					*/
#define PORTHIO_MASKA			0xFFC01710	/* Port H I/O Mask State Specify Interrupt A Register	*/
#define PORTHIO_MASKA_CLEAR		0xFFC01714	/* Port H I/O Mask Disable Interrupt A Register			*/
#define PORTHIO_MASKA_SET		0xFFC01718	/* Port H I/O Mask Enable Interrupt A Register			*/
#define PORTHIO_MASKA_TOGGLE	0xFFC0171C	/* Port H I/O Mask Toggle Enable Interrupt A Register	*/
#define PORTHIO_MASKB			0xFFC01720	/* Port H I/O Mask State Specify Interrupt B Register	*/
#define PORTHIO_MASKB_CLEAR		0xFFC01724	/* Port H I/O Mask Disable Interrupt B Register			*/
#define PORTHIO_MASKB_SET		0xFFC01728	/* Port H I/O Mask Enable Interrupt B Register			*/
#define PORTHIO_MASKB_TOGGLE	0xFFC0172C	/* Port H I/O Mask Toggle Enable Interrupt B Register	*/
#define PORTHIO_DIR				0xFFC01730	/* Port H I/O Direction Register						*/
#define PORTHIO_POLAR			0xFFC01734	/* Port H I/O Source Polarity Register					*/
#define PORTHIO_EDGE			0xFFC01738	/* Port H I/O Source Sensitivity Register				*/
#define PORTHIO_BOTH			0xFFC0173C	/* Port H I/O Set on BOTH Edges Register				*/
#define PORTHIO_INEN			0xFFC01740	/* Port H I/O Input Enable Register						*/


/* UART1 Controller		(0xFFC02000 - 0xFFC020FF)								*/
#define UART1_THR			0xFFC02000	/* Transmit Holding register			*/
#define UART1_RBR			0xFFC02000	/* Receive Buffer register				*/
#define UART1_DLL			0xFFC02000	/* Divisor Latch (Low-Byte)				*/
#define UART1_IER			0xFFC02004	/* Interrupt Enable Register			*/
#define UART1_DLH			0xFFC02004	/* Divisor Latch (High-Byte)			*/
#define UART1_IIR			0xFFC02008	/* Interrupt Identification Register	*/
#define UART1_LCR			0xFFC0200C	/* Line Control Register				*/
#define UART1_MCR			0xFFC02010	/* Modem Control Register				*/
#define UART1_LSR			0xFFC02014	/* Line Status Register					*/
#define UART1_MSR			0xFFC02018	/* Modem Status Register				*/
#define UART1_SCR			0xFFC0201C	/* SCR Scratch Register					*/
#define UART1_GCTL			0xFFC02024	/* Global Control Register				*/


/* Pin Control Registers	(0xFFC03200 - 0xFFC032FF)											*/
#define PORTF_FER			0xFFC03200	/* Port F Function Enable Register (Alternate/Flag*)	*/
#define PORTG_FER			0xFFC03204	/* Port G Function Enable Register (Alternate/Flag*)	*/
#define PORTH_FER			0xFFC03208	/* Port H Function Enable Register (Alternate/Flag*)	*/
#define BFIN_PORT_MUX			0xFFC0320C	/* Port Multiplexer Control Register					*/


/* Handshake MDMA Registers	(0xFFC03300 - 0xFFC033FF)										*/
#define HMDMA0_CONTROL		0xFFC03300	/* Handshake MDMA0 Control Register					*/
#define HMDMA0_ECINIT		0xFFC03304	/* HMDMA0 Initial Edge Count Register				*/
#define HMDMA0_BCINIT		0xFFC03308	/* HMDMA0 Initial Block Count Register				*/
#define HMDMA0_ECURGENT		0xFFC0330C	/* HMDMA0 Urgent Edge Count Threshold Register		*/
#define HMDMA0_ECOVERFLOW	0xFFC03310	/* HMDMA0 Edge Count Overflow Interrupt Register	*/
#define HMDMA0_ECOUNT		0xFFC03314	/* HMDMA0 Current Edge Count Register				*/
#define HMDMA0_BCOUNT		0xFFC03318	/* HMDMA0 Current Block Count Register				*/

#define HMDMA1_CONTROL		0xFFC03340	/* Handshake MDMA1 Control Register					*/
#define HMDMA1_ECINIT		0xFFC03344	/* HMDMA1 Initial Edge Count Register				*/
#define HMDMA1_BCINIT		0xFFC03348	/* HMDMA1 Initial Block Count Register				*/
#define HMDMA1_ECURGENT		0xFFC0334C	/* HMDMA1 Urgent Edge Count Threshold Register		*/
#define HMDMA1_ECOVERFLOW	0xFFC03350	/* HMDMA1 Edge Count Overflow Interrupt Register	*/
#define HMDMA1_ECOUNT		0xFFC03354	/* HMDMA1 Current Edge Count Register				*/
#define HMDMA1_BCOUNT		0xFFC03358	/* HMDMA1 Current Block Count Register				*/


/* GPIO PIN mux (0xFFC03210 - OxFFC03288) */
#define PORTF_MUX               0xFFC03210      /* Port F mux control */
#define PORTG_MUX               0xFFC03214      /* Port G mux control */
#define PORTH_MUX               0xFFC03218      /* Port H mux control */
#define PORTF_DRIVE             0xFFC03220      /* Port F drive strength control */
#define PORTG_DRIVE             0xFFC03224      /* Port G drive strength control */
#define PORTH_DRIVE             0xFFC03228      /* Port H drive strength control */
#define PORTF_SLEW              0xFFC03230      /* Port F slew control */
#define PORTG_SLEW              0xFFC03234      /* Port G slew control */
#define PORTH_SLEW              0xFFC03238      /* Port H slew control */
#define PORTF_HYSTERISIS        0xFFC03240      /* Port F Schmitt trigger control */
#define PORTG_HYSTERISIS        0xFFC03244      /* Port G Schmitt trigger control */
#define PORTH_HYSTERISIS        0xFFC03248      /* Port H Schmitt trigger control */
#define MISCPORT_DRIVE          0xFFC03280      /* Misc Port drive strength control */
#define MISCPORT_SLEW           0xFFC03284      /* Misc Port slew control */
#define MISCPORT_HYSTERISIS     0xFFC03288      /* Misc Port Schmitt trigger control */


/***********************************************************************************
** System MMR Register Bits And Macros
**
** Disclaimer:	All macros are intended to make C and Assembly code more readable.
**				Use these macros carefully, as any that do left shifts for field
**				depositing will result in the lower order bits being destroyed.  Any
**				macro that shifts left to properly position the bit-field should be
**				used as part of an OR to initialize a register and NOT as a dynamic
**				modifier UNLESS the lower order bits are saved and ORed back in when
**				the macro is used.
*************************************************************************************/

/* CHIPID Masks */
#define CHIPID_VERSION         0xF0000000
#define CHIPID_FAMILY          0x0FFFF000
#define CHIPID_MANUFACTURE     0x00000FFE

/* SWRST Masks																		*/
#define SYSTEM_RESET		0x0007	/* Initiates A System Software Reset			*/
#define	DOUBLE_FAULT		0x0008	/* Core Double Fault Causes Reset				*/
#define RESET_DOUBLE		0x2000	/* SW Reset Generated By Core Double-Fault		*/
#define RESET_WDOG			0x4000	/* SW Reset Generated By Watchdog Timer			*/
#define RESET_SOFTWARE		0x8000	/* SW Reset Occurred Since Last Read Of SWRST	*/

/* SYSCR Masks																				*/
#define BMODE				0x0007	/* Boot Mode - Latched During HW Reset From Mode Pins	*/
#define	NOBOOT				0x0010	/* Execute From L1 or ASYNC Bank 0 When BMODE = 0		*/


/* *************  SYSTEM INTERRUPT CONTROLLER MASKS *************************************/
/* Peripheral Masks For SIC_ISR, SIC_IWR, SIC_IMASK										*/

#if 0
#define IRQ_PLL_WAKEUP	0x00000001	/* PLL Wakeup Interrupt			 					*/

#define IRQ_ERROR1      0x00000002  /* Error Interrupt (DMA, DMARx Block, DMARx Overflow) */
#define IRQ_ERROR2      0x00000004  /* Error Interrupt (CAN, Ethernet, SPORTx, PPI, SPI, UARTx) */
#define IRQ_RTC			0x00000008	/* Real Time Clock Interrupt 						*/
#define IRQ_DMA0		0x00000010	/* DMA Channel 0 (PPI) Interrupt 					*/
#define IRQ_DMA3		0x00000020	/* DMA Channel 3 (SPORT0 RX) Interrupt 				*/
#define IRQ_DMA4		0x00000040	/* DMA Channel 4 (SPORT0 TX) Interrupt 				*/
#define IRQ_DMA5		0x00000080	/* DMA Channel 5 (SPORT1 RX) Interrupt 				*/

#define IRQ_DMA6		0x00000100	/* DMA Channel 6 (SPORT1 TX) Interrupt 		 		*/
#define IRQ_TWI			0x00000200	/* TWI Interrupt									*/
#define IRQ_DMA7		0x00000400	/* DMA Channel 7 (SPI) Interrupt 					*/
#define IRQ_DMA8		0x00000800	/* DMA Channel 8 (UART0 RX) Interrupt 				*/
#define IRQ_DMA9		0x00001000	/* DMA Channel 9 (UART0 TX) Interrupt 				*/
#define IRQ_DMA10		0x00002000	/* DMA Channel 10 (UART1 RX) Interrupt 				*/
#define IRQ_DMA11		0x00004000	/* DMA Channel 11 (UART1 TX) Interrupt 				*/
#define IRQ_CAN_RX		0x00008000	/* CAN Receive Interrupt 							*/

#define IRQ_CAN_TX		0x00010000	/* CAN Transmit Interrupt  							*/
#define IRQ_DMA1		0x00020000	/* DMA Channel 1 (Ethernet RX) Interrupt 			*/
#define IRQ_PFA_PORTH	0x00020000	/* PF Port H (PF47:32) Interrupt A 					*/
#define IRQ_DMA2		0x00040000	/* DMA Channel 2 (Ethernet TX) Interrupt 			*/
#define IRQ_PFB_PORTH	0x00040000	/* PF Port H (PF47:32) Interrupt B 					*/
#define IRQ_TIMER0		0x00080000	/* Timer 0 Interrupt								*/
#define IRQ_TIMER1		0x00100000	/* Timer 1 Interrupt 								*/
#define IRQ_TIMER2		0x00200000	/* Timer 2 Interrupt 								*/
#define IRQ_TIMER3		0x00400000	/* Timer 3 Interrupt 								*/
#define IRQ_TIMER4		0x00800000	/* Timer 4 Interrupt 								*/

#define IRQ_TIMER5		0x01000000	/* Timer 5 Interrupt 								*/
#define IRQ_TIMER6		0x02000000	/* Timer 6 Interrupt 								*/
#define IRQ_TIMER7		0x04000000	/* Timer 7 Interrupt 								*/
#define IRQ_PFA_PORTFG	0x08000000	/* PF Ports F&G (PF31:0) Interrupt A 				*/
#define IRQ_PFB_PORTF	0x80000000	/* PF Port F (PF15:0) Interrupt B 					*/
#define IRQ_DMA12		0x20000000	/* DMA Channels 12 (MDMA1 Source) RX Interrupt 		*/
#define IRQ_DMA13		0x20000000	/* DMA Channels 13 (MDMA1 Destination) TX Interrupt */
#define IRQ_DMA14		0x40000000	/* DMA Channels 14 (MDMA0 Source) RX Interrupt 		*/
#define IRQ_DMA15		0x40000000	/* DMA Channels 15 (MDMA0 Destination) TX Interrupt */
#define IRQ_WDOG		0x80000000	/* Software Watchdog Timer Interrupt 				*/
#define IRQ_PFB_PORTG	0x10000000	/* PF Port G (PF31:16) Interrupt B 					*/
#endif

/* SIC_IAR0 Macros															*/
#define P0_IVG(x)		(((x)&0xF)-7)			/* Peripheral #0 assigned IVG #x 	*/
#define P1_IVG(x)		(((x)&0xF)-7) << 0x4	/* Peripheral #1 assigned IVG #x 	*/
#define P2_IVG(x)		(((x)&0xF)-7) << 0x8	/* Peripheral #2 assigned IVG #x 	*/
#define P3_IVG(x)		(((x)&0xF)-7) << 0xC	/* Peripheral #3 assigned IVG #x	*/
#define P4_IVG(x)		(((x)&0xF)-7) << 0x10	/* Peripheral #4 assigned IVG #x	*/
#define P5_IVG(x)		(((x)&0xF)-7) << 0x14	/* Peripheral #5 assigned IVG #x	*/
#define P6_IVG(x)		(((x)&0xF)-7) << 0x18	/* Peripheral #6 assigned IVG #x	*/
#define P7_IVG(x)		(((x)&0xF)-7) << 0x1C	/* Peripheral #7 assigned IVG #x	*/

/* SIC_IAR1 Macros															*/
#define P8_IVG(x)		(((x)&0xF)-7)			/* Peripheral #8 assigned IVG #x 	*/
#define P9_IVG(x)		(((x)&0xF)-7) << 0x4	/* Peripheral #9 assigned IVG #x 	*/
#define P10_IVG(x)		(((x)&0xF)-7) << 0x8	/* Peripheral #10 assigned IVG #x	*/
#define P11_IVG(x)		(((x)&0xF)-7) << 0xC	/* Peripheral #11 assigned IVG #x 	*/
#define P12_IVG(x)		(((x)&0xF)-7) << 0x10	/* Peripheral #12 assigned IVG #x	*/
#define P13_IVG(x)		(((x)&0xF)-7) << 0x14	/* Peripheral #13 assigned IVG #x	*/
#define P14_IVG(x)		(((x)&0xF)-7) << 0x18	/* Peripheral #14 assigned IVG #x	*/
#define P15_IVG(x)		(((x)&0xF)-7) << 0x1C	/* Peripheral #15 assigned IVG #x	*/

/* SIC_IAR2 Macros															*/
#define P16_IVG(x)		(((x)&0xF)-7)			/* Peripheral #16 assigned IVG #x	*/
#define P17_IVG(x)		(((x)&0xF)-7) << 0x4	/* Peripheral #17 assigned IVG #x	*/
#define P18_IVG(x)		(((x)&0xF)-7) << 0x8	/* Peripheral #18 assigned IVG #x	*/
#define P19_IVG(x)		(((x)&0xF)-7) << 0xC	/* Peripheral #19 assigned IVG #x	*/
#define P20_IVG(x)		(((x)&0xF)-7) << 0x10	/* Peripheral #20 assigned IVG #x	*/
#define P21_IVG(x)		(((x)&0xF)-7) << 0x14	/* Peripheral #21 assigned IVG #x	*/
#define P22_IVG(x)		(((x)&0xF)-7) << 0x18	/* Peripheral #22 assigned IVG #x	*/
#define P23_IVG(x)		(((x)&0xF)-7) << 0x1C	/* Peripheral #23 assigned IVG #x	*/

/* SIC_IAR3 Macros															*/
#define P24_IVG(x)		(((x)&0xF)-7)			/* Peripheral #24 assigned IVG #x	*/
#define P25_IVG(x)		(((x)&0xF)-7) << 0x4	/* Peripheral #25 assigned IVG #x	*/
#define P26_IVG(x)		(((x)&0xF)-7) << 0x8	/* Peripheral #26 assigned IVG #x	*/
#define P27_IVG(x)		(((x)&0xF)-7) << 0xC	/* Peripheral #27 assigned IVG #x	*/
#define P28_IVG(x)		(((x)&0xF)-7) << 0x10	/* Peripheral #28 assigned IVG #x	*/
#define P29_IVG(x)		(((x)&0xF)-7) << 0x14	/* Peripheral #29 assigned IVG #x	*/
#define P30_IVG(x)		(((x)&0xF)-7) << 0x18	/* Peripheral #30 assigned IVG #x	*/
#define P31_IVG(x)		(((x)&0xF)-7) << 0x1C	/* Peripheral #31 assigned IVG #x	*/


/* SIC_IMASK Masks																		*/
#define SIC_UNMASK_ALL	0x00000000					/* Unmask all peripheral interrupts	*/
#define SIC_MASK_ALL	0xFFFFFFFF					/* Mask all peripheral interrupts	*/
#define SIC_MASK(x)		(1 << ((x)&0x1F))					/* Mask Peripheral #x interrupt		*/
#define SIC_UNMASK(x)	(0xFFFFFFFF ^ (1 << ((x)&0x1F)))	/* Unmask Peripheral #x interrupt	*/

/* SIC_IWR Masks																		*/
#define IWR_DISABLE_ALL	0x00000000					/* Wakeup Disable all peripherals	*/
#define IWR_ENABLE_ALL	0xFFFFFFFF					/* Wakeup Enable all peripherals	*/
#define IWR_ENABLE(x)	(1 << ((x)&0x1F))					/* Wakeup Enable Peripheral #x		*/
#define IWR_DISABLE(x)	(0xFFFFFFFF ^ (1 << ((x)&0x1F))) 	/* Wakeup Disable Peripheral #x		*/


/* ************** UART CONTROLLER MASKS *************************/
/* UARTx_LCR Masks												*/
#define WLS(x)		(((x)-5) & 0x03)	/* Word Length Select */
#define STB			0x04				/* Stop Bits			*/
#define PEN			0x08				/* Parity Enable		*/
#define EPS			0x10				/* Even Parity Select	*/
#define STP			0x20				/* Stick Parity			*/
#define SB			0x40				/* Set Break			*/
#define DLAB		0x80				/* Divisor Latch Access	*/

/* UARTx_MCR Mask										*/
#define LOOP_ENA	0x10	/* Loopback Mode Enable */
#define LOOP_ENA_P	0x04

/* UARTx_LSR Masks										*/
#define DR			0x01	/* Data Ready				*/
#define OE			0x02	/* Overrun Error			*/
#define PE			0x04	/* Parity Error				*/
#define FE			0x08	/* Framing Error			*/
#define BI			0x10	/* Break Interrupt			*/
#define THRE		0x20	/* THR Empty				*/
#define TEMT		0x40	/* TSR and UART_THR Empty	*/

/* UARTx_IER Masks															*/
#define ERBFI		0x01		/* Enable Receive Buffer Full Interrupt		*/
#define ETBEI		0x02		/* Enable Transmit Buffer Empty Interrupt	*/
#define ELSI		0x04		/* Enable RX Status Interrupt				*/

/* UARTx_IIR Masks														*/
#define NINT		0x01		/* Pending Interrupt					*/
#define IIR_TX_READY    0x02		/* UART_THR empty                               */
#define IIR_RX_READY    0x04		/* Receive data ready                           */
#define IIR_LINE_CHANGE 0x06		/* Receive line status    			*/
#define IIR_STATUS	0x06		/* Highest Priority Pending Interrupt	*/

/* UARTx_GCTL Masks													*/
#define UCEN		0x01		/* Enable UARTx Clocks				*/
#define IREN		0x02		/* Enable IrDA Mode					*/
#define TPOLC		0x04		/* IrDA TX Polarity Change			*/
#define RPOLC		0x08		/* IrDA RX Polarity Change			*/
#define FPE			0x10		/* Force Parity Error On Transmit	*/
#define FFE			0x20		/* Force Framing Error On Transmit	*/


/* ***********  SERIAL PERIPHERAL INTERFACE (SPI) MASKS  ****************************/
/* SPI_CTL Masks																	*/
#define	TIMOD		0x0003		/* Transfer Initiate Mode							*/
#define RDBR_CORE	0x0000		/* 		RDBR Read Initiates, IRQ When RDBR Full		*/
#define	TDBR_CORE	0x0001		/* 		TDBR Write Initiates, IRQ When TDBR Empty	*/
#define RDBR_DMA	0x0002		/* 		DMA Read, DMA Until FIFO Empty				*/
#define TDBR_DMA	0x0003		/* 		DMA Write, DMA Until FIFO Full				*/
#define SZ			0x0004		/* Send Zero (When TDBR Empty, Send Zero/Last*)		*/
#define GM			0x0008		/* Get More (When RDBR Full, Overwrite/Discard*)	*/
#define PSSE		0x0010		/* Slave-Select Input Enable						*/
#define EMISO		0x0020		/* Enable MISO As Output							*/
#define SIZE		0x0100		/* Size of Words (16/8* Bits)						*/
#define LSBF		0x0200		/* LSB First										*/
#define CPHA		0x0400		/* Clock Phase										*/
#define CPOL		0x0800		/* Clock Polarity									*/
#define MSTR		0x1000		/* Master/Slave*									*/
#define WOM			0x2000		/* Write Open Drain Master							*/
#define SPE			0x4000		/* SPI Enable										*/

/* SPI_FLG Masks																	*/
#define FLS1		0x0002		/* Enables SPI_FLOUT1 as SPI Slave-Select Output	*/
#define FLS2		0x0004		/* Enables SPI_FLOUT2 as SPI Slave-Select Output	*/
#define FLS3		0x0008		/* Enables SPI_FLOUT3 as SPI Slave-Select Output	*/
#define FLS4		0x0010		/* Enables SPI_FLOUT4 as SPI Slave-Select Output	*/
#define FLS5		0x0020		/* Enables SPI_FLOUT5 as SPI Slave-Select Output	*/
#define FLS6		0x0040		/* Enables SPI_FLOUT6 as SPI Slave-Select Output	*/
#define FLS7		0x0080		/* Enables SPI_FLOUT7 as SPI Slave-Select Output	*/
#define FLG1		0xFDFF		/* Activates SPI_FLOUT1 							*/
#define FLG2		0xFBFF		/* Activates SPI_FLOUT2								*/
#define FLG3		0xF7FF		/* Activates SPI_FLOUT3								*/
#define FLG4		0xEFFF		/* Activates SPI_FLOUT4								*/
#define FLG5		0xDFFF		/* Activates SPI_FLOUT5								*/
#define FLG6		0xBFFF		/* Activates SPI_FLOUT6								*/
#define FLG7		0x7FFF		/* Activates SPI_FLOUT7								*/

/* SPI_STAT Masks																				*/
#define SPIF		0x0001		/* SPI Finished (Single-Word Transfer Complete)					*/
#define MODF		0x0002		/* Mode Fault Error (Another Device Tried To Become Master)		*/
#define TXE			0x0004		/* Transmission Error (Data Sent With No New Data In TDBR)		*/
#define TXS			0x0008		/* SPI_TDBR Data Buffer Status (Full/Empty*)					*/
#define RBSY		0x0010		/* Receive Error (Data Received With RDBR Full)					*/
#define RXS			0x0020		/* SPI_RDBR Data Buffer Status (Full/Empty*)					*/
#define TXCOL		0x0040		/* Transmit Collision Error (Corrupt Data May Have Been Sent)	*/


/*  ****************  GENERAL PURPOSE TIMER MASKS  **********************/
/* TIMER_ENABLE Masks													*/
#define TIMEN0			0x0001		/* Enable Timer 0					*/
#define TIMEN1			0x0002		/* Enable Timer 1					*/
#define TIMEN2			0x0004		/* Enable Timer 2					*/
#define TIMEN3			0x0008		/* Enable Timer 3					*/
#define TIMEN4			0x0010		/* Enable Timer 4					*/
#define TIMEN5			0x0020		/* Enable Timer 5					*/
#define TIMEN6			0x0040		/* Enable Timer 6					*/
#define TIMEN7			0x0080		/* Enable Timer 7					*/

/* TIMER_DISABLE Masks													*/
#define TIMDIS0			TIMEN0		/* Disable Timer 0					*/
#define TIMDIS1			TIMEN1		/* Disable Timer 1					*/
#define TIMDIS2			TIMEN2		/* Disable Timer 2					*/
#define TIMDIS3			TIMEN3		/* Disable Timer 3					*/
#define TIMDIS4			TIMEN4		/* Disable Timer 4					*/
#define TIMDIS5			TIMEN5		/* Disable Timer 5					*/
#define TIMDIS6			TIMEN6		/* Disable Timer 6					*/
#define TIMDIS7			TIMEN7		/* Disable Timer 7					*/

/* TIMER_STATUS Masks													*/
#define TIMIL0			0x00000001	/* Timer 0 Interrupt				*/
#define TIMIL1			0x00000002	/* Timer 1 Interrupt				*/
#define TIMIL2			0x00000004	/* Timer 2 Interrupt				*/
#define TIMIL3			0x00000008	/* Timer 3 Interrupt				*/
#define TOVF_ERR0		0x00000010	/* Timer 0 Counter Overflow			*/
#define TOVF_ERR1		0x00000020	/* Timer 1 Counter Overflow			*/
#define TOVF_ERR2		0x00000040	/* Timer 2 Counter Overflow			*/
#define TOVF_ERR3		0x00000080	/* Timer 3 Counter Overflow			*/
#define TRUN0			0x00001000	/* Timer 0 Slave Enable Status		*/
#define TRUN1			0x00002000	/* Timer 1 Slave Enable Status		*/
#define TRUN2			0x00004000	/* Timer 2 Slave Enable Status		*/
#define TRUN3			0x00008000	/* Timer 3 Slave Enable Status		*/
#define TIMIL4			0x00010000	/* Timer 4 Interrupt				*/
#define TIMIL5			0x00020000	/* Timer 5 Interrupt				*/
#define TIMIL6			0x00040000	/* Timer 6 Interrupt				*/
#define TIMIL7			0x00080000	/* Timer 7 Interrupt				*/
#define TOVF_ERR4		0x00100000	/* Timer 4 Counter Overflow			*/
#define TOVF_ERR5		0x00200000	/* Timer 5 Counter Overflow			*/
#define TOVF_ERR6		0x00400000	/* Timer 6 Counter Overflow			*/
#define TOVF_ERR7		0x00800000	/* Timer 7 Counter Overflow			*/
#define TRUN4			0x10000000	/* Timer 4 Slave Enable Status		*/
#define TRUN5			0x20000000	/* Timer 5 Slave Enable Status		*/
#define TRUN6			0x40000000	/* Timer 6 Slave Enable Status		*/
#define TRUN7			0x80000000	/* Timer 7 Slave Enable Status		*/

/* Alternate Deprecated Macros Provided For Backwards Code Compatibility */
#define TOVL_ERR0 TOVF_ERR0
#define TOVL_ERR1 TOVF_ERR1
#define TOVL_ERR2 TOVF_ERR2
#define TOVL_ERR3 TOVF_ERR3
#define TOVL_ERR4 TOVF_ERR4
#define TOVL_ERR5 TOVF_ERR5
#define TOVL_ERR6 TOVF_ERR6
#define TOVL_ERR7 TOVF_ERR7

/* TIMERx_CONFIG Masks													*/
#define PWM_OUT			0x0001	/* Pulse-Width Modulation Output Mode	*/
#define WDTH_CAP		0x0002	/* Width Capture Input Mode				*/
#define EXT_CLK			0x0003	/* External Clock Mode					*/
#define PULSE_HI		0x0004	/* Action Pulse (Positive/Negative*)	*/
#define PERIOD_CNT		0x0008	/* Period Count							*/
#define IRQ_ENA			0x0010	/* Interrupt Request Enable				*/
#define TIN_SEL			0x0020	/* Timer Input Select					*/
#define OUT_DIS			0x0040	/* Output Pad Disable					*/
#define CLK_SEL			0x0080	/* Timer Clock Select					*/
#define TOGGLE_HI		0x0100	/* PWM_OUT PULSE_HI Toggle Mode			*/
#define EMU_RUN			0x0200	/* Emulation Behavior Select			*/
#define ERR_TYP			0xC000	/* Error Type							*/


/* ******************   GPIO PORTS F, G, H MASKS  ***********************/
/*  General Purpose IO (0xFFC00700 - 0xFFC007FF)  Masks 				*/
/* Port F Masks 														*/
#define PF0		0x0001
#define PF1		0x0002
#define PF2		0x0004
#define PF3		0x0008
#define PF4		0x0010
#define PF5		0x0020
#define PF6		0x0040
#define PF7		0x0080
#define PF8		0x0100
#define PF9		0x0200
#define PF10	0x0400
#define PF11	0x0800
#define PF12	0x1000
#define PF13	0x2000
#define PF14	0x4000
#define PF15	0x8000

/* Port G Masks															*/
#define PG0		0x0001
#define PG1		0x0002
#define PG2		0x0004
#define PG3		0x0008
#define PG4		0x0010
#define PG5		0x0020
#define PG6		0x0040
#define PG7		0x0080
#define PG8		0x0100
#define PG9		0x0200
#define PG10	0x0400
#define PG11	0x0800
#define PG12	0x1000
#define PG13	0x2000
#define PG14	0x4000
#define PG15	0x8000

/* Port H Masks															*/
#define PH0		0x0001
#define PH1		0x0002
#define PH2		0x0004
#define PH3		0x0008
#define PH4		0x0010
#define PH5		0x0020
#define PH6		0x0040
#define PH7		0x0080

/* *********************  ASYNCHRONOUS MEMORY CONTROLLER MASKS  *************************/
/* EBIU_AMGCTL Masks																	*/
#define AMCKEN			0x0001		/* Enable CLKOUT									*/
#define	AMBEN_NONE		0x0000		/* All Banks Disabled								*/
#define AMBEN_B0		0x0002		/* Enable Async Memory Bank 0 only					*/
#define AMBEN_B0_B1		0x0004		/* Enable Async Memory Banks 0 & 1 only				*/
#define AMBEN_B0_B1_B2	0x0006		/* Enable Async Memory Banks 0, 1, and 2			*/
#define AMBEN_ALL		0x0008		/* Enable Async Memory Banks (all) 0, 1, 2, and 3	*/

/* EBIU_AMBCTL0 Masks																	*/
#define B0RDYEN			0x00000001  /* Bank 0 (B0) RDY Enable							*/
#define B0RDYPOL		0x00000002  /* B0 RDY Active High								*/
#define B0TT_1			0x00000004  /* B0 Transition Time (Read to Write) = 1 cycle		*/
#define B0TT_2			0x00000008  /* B0 Transition Time (Read to Write) = 2 cycles	*/
#define B0TT_3			0x0000000C  /* B0 Transition Time (Read to Write) = 3 cycles	*/
#define B0TT_4			0x00000000  /* B0 Transition Time (Read to Write) = 4 cycles	*/
#define B0ST_1			0x00000010  /* B0 Setup Time (AOE to Read/Write) = 1 cycle		*/
#define B0ST_2			0x00000020  /* B0 Setup Time (AOE to Read/Write) = 2 cycles		*/
#define B0ST_3			0x00000030  /* B0 Setup Time (AOE to Read/Write) = 3 cycles		*/
#define B0ST_4			0x00000000  /* B0 Setup Time (AOE to Read/Write) = 4 cycles		*/
#define B0HT_1			0x00000040  /* B0 Hold Time (~Read/Write to ~AOE) = 1 cycle		*/
#define B0HT_2			0x00000080  /* B0 Hold Time (~Read/Write to ~AOE) = 2 cycles	*/
#define B0HT_3			0x000000C0  /* B0 Hold Time (~Read/Write to ~AOE) = 3 cycles	*/
#define B0HT_0			0x00000000  /* B0 Hold Time (~Read/Write to ~AOE) = 0 cycles	*/
#define B0RAT_1			0x00000100  /* B0 Read Access Time = 1 cycle					*/
#define B0RAT_2			0x00000200  /* B0 Read Access Time = 2 cycles					*/
#define B0RAT_3			0x00000300  /* B0 Read Access Time = 3 cycles					*/
#define B0RAT_4			0x00000400  /* B0 Read Access Time = 4 cycles					*/
#define B0RAT_5			0x00000500  /* B0 Read Access Time = 5 cycles					*/
#define B0RAT_6			0x00000600  /* B0 Read Access Time = 6 cycles					*/
#define B0RAT_7			0x00000700  /* B0 Read Access Time = 7 cycles					*/
#define B0RAT_8			0x00000800  /* B0 Read Access Time = 8 cycles					*/
#define B0RAT_9			0x00000900  /* B0 Read Access Time = 9 cycles					*/
#define B0RAT_10		0x00000A00  /* B0 Read Access Time = 10 cycles					*/
#define B0RAT_11		0x00000B00  /* B0 Read Access Time = 11 cycles					*/
#define B0RAT_12		0x00000C00  /* B0 Read Access Time = 12 cycles					*/
#define B0RAT_13		0x00000D00  /* B0 Read Access Time = 13 cycles					*/
#define B0RAT_14		0x00000E00  /* B0 Read Access Time = 14 cycles					*/
#define B0RAT_15		0x00000F00  /* B0 Read Access Time = 15 cycles					*/
#define B0WAT_1			0x00001000  /* B0 Write Access Time = 1 cycle					*/
#define B0WAT_2			0x00002000  /* B0 Write Access Time = 2 cycles					*/
#define B0WAT_3			0x00003000  /* B0 Write Access Time = 3 cycles					*/
#define B0WAT_4			0x00004000  /* B0 Write Access Time = 4 cycles					*/
#define B0WAT_5			0x00005000  /* B0 Write Access Time = 5 cycles					*/
#define B0WAT_6			0x00006000  /* B0 Write Access Time = 6 cycles					*/
#define B0WAT_7			0x00007000  /* B0 Write Access Time = 7 cycles					*/
#define B0WAT_8			0x00008000  /* B0 Write Access Time = 8 cycles					*/
#define B0WAT_9			0x00009000  /* B0 Write Access Time = 9 cycles					*/
#define B0WAT_10		0x0000A000  /* B0 Write Access Time = 10 cycles					*/
#define B0WAT_11		0x0000B000  /* B0 Write Access Time = 11 cycles					*/
#define B0WAT_12		0x0000C000  /* B0 Write Access Time = 12 cycles					*/
#define B0WAT_13		0x0000D000  /* B0 Write Access Time = 13 cycles					*/
#define B0WAT_14		0x0000E000  /* B0 Write Access Time = 14 cycles					*/
#define B0WAT_15		0x0000F000  /* B0 Write Access Time = 15 cycles					*/

#define B1RDYEN			0x00010000  /* Bank 1 (B1) RDY Enable                       	*/
#define B1RDYPOL		0x00020000  /* B1 RDY Active High                           	*/
#define B1TT_1			0x00040000  /* B1 Transition Time (Read to Write) = 1 cycle 	*/
#define B1TT_2			0x00080000  /* B1 Transition Time (Read to Write) = 2 cycles	*/
#define B1TT_3			0x000C0000  /* B1 Transition Time (Read to Write) = 3 cycles	*/
#define B1TT_4			0x00000000  /* B1 Transition Time (Read to Write) = 4 cycles	*/
#define B1ST_1			0x00100000  /* B1 Setup Time (AOE to Read/Write) = 1 cycle  	*/
#define B1ST_2			0x00200000  /* B1 Setup Time (AOE to Read/Write) = 2 cycles 	*/
#define B1ST_3			0x00300000  /* B1 Setup Time (AOE to Read/Write) = 3 cycles 	*/
#define B1ST_4			0x00000000  /* B1 Setup Time (AOE to Read/Write) = 4 cycles 	*/
#define B1HT_1			0x00400000  /* B1 Hold Time (~Read/Write to ~AOE) = 1 cycle 	*/
#define B1HT_2			0x00800000  /* B1 Hold Time (~Read/Write to ~AOE) = 2 cycles	*/
#define B1HT_3			0x00C00000  /* B1 Hold Time (~Read/Write to ~AOE) = 3 cycles	*/
#define B1HT_0			0x00000000  /* B1 Hold Time (~Read/Write to ~AOE) = 0 cycles	*/
#define B1RAT_1			0x01000000  /* B1 Read Access Time = 1 cycle					*/
#define B1RAT_2			0x02000000  /* B1 Read Access Time = 2 cycles					*/
#define B1RAT_3			0x03000000  /* B1 Read Access Time = 3 cycles					*/
#define B1RAT_4			0x04000000  /* B1 Read Access Time = 4 cycles					*/
#define B1RAT_5			0x05000000  /* B1 Read Access Time = 5 cycles					*/
#define B1RAT_6			0x06000000  /* B1 Read Access Time = 6 cycles					*/
#define B1RAT_7			0x07000000  /* B1 Read Access Time = 7 cycles					*/
#define B1RAT_8			0x08000000  /* B1 Read Access Time = 8 cycles					*/
#define B1RAT_9			0x09000000  /* B1 Read Access Time = 9 cycles					*/
#define B1RAT_10		0x0A000000  /* B1 Read Access Time = 10 cycles					*/
#define B1RAT_11		0x0B000000  /* B1 Read Access Time = 11 cycles					*/
#define B1RAT_12		0x0C000000  /* B1 Read Access Time = 12 cycles					*/
#define B1RAT_13		0x0D000000  /* B1 Read Access Time = 13 cycles					*/
#define B1RAT_14		0x0E000000  /* B1 Read Access Time = 14 cycles					*/
#define B1RAT_15		0x0F000000  /* B1 Read Access Time = 15 cycles					*/
#define B1WAT_1			0x10000000  /* B1 Write Access Time = 1 cycle					*/
#define B1WAT_2			0x20000000  /* B1 Write Access Time = 2 cycles					*/
#define B1WAT_3			0x30000000  /* B1 Write Access Time = 3 cycles					*/
#define B1WAT_4			0x40000000  /* B1 Write Access Time = 4 cycles					*/
#define B1WAT_5			0x50000000  /* B1 Write Access Time = 5 cycles					*/
#define B1WAT_6			0x60000000  /* B1 Write Access Time = 6 cycles					*/
#define B1WAT_7			0x70000000  /* B1 Write Access Time = 7 cycles					*/
#define B1WAT_8			0x80000000  /* B1 Write Access Time = 8 cycles					*/
#define B1WAT_9			0x90000000  /* B1 Write Access Time = 9 cycles					*/
#define B1WAT_10		0xA0000000  /* B1 Write Access Time = 10 cycles					*/
#define B1WAT_11		0xB0000000  /* B1 Write Access Time = 11 cycles					*/
#define B1WAT_12		0xC0000000  /* B1 Write Access Time = 12 cycles					*/
#define B1WAT_13		0xD0000000  /* B1 Write Access Time = 13 cycles					*/
#define B1WAT_14		0xE0000000  /* B1 Write Access Time = 14 cycles					*/
#define B1WAT_15		0xF0000000  /* B1 Write Access Time = 15 cycles					*/

/* EBIU_AMBCTL1 Masks																	*/
#define B2RDYEN			0x00000001  /* Bank 2 (B2) RDY Enable							*/
#define B2RDYPOL		0x00000002  /* B2 RDY Active High								*/
#define B2TT_1			0x00000004  /* B2 Transition Time (Read to Write) = 1 cycle		*/
#define B2TT_2			0x00000008  /* B2 Transition Time (Read to Write) = 2 cycles	*/
#define B2TT_3			0x0000000C  /* B2 Transition Time (Read to Write) = 3 cycles	*/
#define B2TT_4			0x00000000  /* B2 Transition Time (Read to Write) = 4 cycles	*/
#define B2ST_1			0x00000010  /* B2 Setup Time (AOE to Read/Write) = 1 cycle		*/
#define B2ST_2			0x00000020  /* B2 Setup Time (AOE to Read/Write) = 2 cycles		*/
#define B2ST_3			0x00000030  /* B2 Setup Time (AOE to Read/Write) = 3 cycles		*/
#define B2ST_4			0x00000000  /* B2 Setup Time (AOE to Read/Write) = 4 cycles		*/
#define B2HT_1			0x00000040  /* B2 Hold Time (~Read/Write to ~AOE) = 1 cycle		*/
#define B2HT_2			0x00000080  /* B2 Hold Time (~Read/Write to ~AOE) = 2 cycles	*/
#define B2HT_3			0x000000C0  /* B2 Hold Time (~Read/Write to ~AOE) = 3 cycles	*/
#define B2HT_0			0x00000000  /* B2 Hold Time (~Read/Write to ~AOE) = 0 cycles	*/
#define B2RAT_1			0x00000100  /* B2 Read Access Time = 1 cycle					*/
#define B2RAT_2			0x00000200  /* B2 Read Access Time = 2 cycles					*/
#define B2RAT_3			0x00000300  /* B2 Read Access Time = 3 cycles					*/
#define B2RAT_4			0x00000400  /* B2 Read Access Time = 4 cycles					*/
#define B2RAT_5			0x00000500  /* B2 Read Access Time = 5 cycles					*/
#define B2RAT_6			0x00000600  /* B2 Read Access Time = 6 cycles					*/
#define B2RAT_7			0x00000700  /* B2 Read Access Time = 7 cycles					*/
#define B2RAT_8			0x00000800  /* B2 Read Access Time = 8 cycles					*/
#define B2RAT_9			0x00000900  /* B2 Read Access Time = 9 cycles					*/
#define B2RAT_10		0x00000A00  /* B2 Read Access Time = 10 cycles					*/
#define B2RAT_11		0x00000B00  /* B2 Read Access Time = 11 cycles					*/
#define B2RAT_12		0x00000C00  /* B2 Read Access Time = 12 cycles					*/
#define B2RAT_13		0x00000D00  /* B2 Read Access Time = 13 cycles					*/
#define B2RAT_14		0x00000E00  /* B2 Read Access Time = 14 cycles					*/
#define B2RAT_15		0x00000F00  /* B2 Read Access Time = 15 cycles					*/
#define B2WAT_1			0x00001000  /* B2 Write Access Time = 1 cycle					*/
#define B2WAT_2			0x00002000  /* B2 Write Access Time = 2 cycles					*/
#define B2WAT_3			0x00003000  /* B2 Write Access Time = 3 cycles					*/
#define B2WAT_4			0x00004000  /* B2 Write Access Time = 4 cycles					*/
#define B2WAT_5			0x00005000  /* B2 Write Access Time = 5 cycles					*/
#define B2WAT_6			0x00006000  /* B2 Write Access Time = 6 cycles					*/
#define B2WAT_7			0x00007000  /* B2 Write Access Time = 7 cycles					*/
#define B2WAT_8			0x00008000  /* B2 Write Access Time = 8 cycles					*/
#define B2WAT_9			0x00009000  /* B2 Write Access Time = 9 cycles					*/
#define B2WAT_10		0x0000A000  /* B2 Write Access Time = 10 cycles					*/
#define B2WAT_11		0x0000B000  /* B2 Write Access Time = 11 cycles					*/
#define B2WAT_12		0x0000C000  /* B2 Write Access Time = 12 cycles					*/
#define B2WAT_13		0x0000D000  /* B2 Write Access Time = 13 cycles					*/
#define B2WAT_14		0x0000E000  /* B2 Write Access Time = 14 cycles					*/
#define B2WAT_15		0x0000F000  /* B2 Write Access Time = 15 cycles					*/

#define B3RDYEN			0x00010000  /* Bank 3 (B3) RDY Enable							*/
#define B3RDYPOL		0x00020000  /* B3 RDY Active High								*/
#define B3TT_1			0x00040000  /* B3 Transition Time (Read to Write) = 1 cycle		*/
#define B3TT_2			0x00080000  /* B3 Transition Time (Read to Write) = 2 cycles	*/
#define B3TT_3			0x000C0000  /* B3 Transition Time (Read to Write) = 3 cycles	*/
#define B3TT_4			0x00000000  /* B3 Transition Time (Read to Write) = 4 cycles	*/
#define B3ST_1			0x00100000  /* B3 Setup Time (AOE to Read/Write) = 1 cycle		*/
#define B3ST_2			0x00200000  /* B3 Setup Time (AOE to Read/Write) = 2 cycles		*/
#define B3ST_3			0x00300000  /* B3 Setup Time (AOE to Read/Write) = 3 cycles		*/
#define B3ST_4			0x00000000  /* B3 Setup Time (AOE to Read/Write) = 4 cycles		*/
#define B3HT_1			0x00400000  /* B3 Hold Time (~Read/Write to ~AOE) = 1 cycle		*/
#define B3HT_2			0x00800000  /* B3 Hold Time (~Read/Write to ~AOE) = 2 cycles	*/
#define B3HT_3			0x00C00000  /* B3 Hold Time (~Read/Write to ~AOE) = 3 cycles	*/
#define B3HT_0			0x00000000  /* B3 Hold Time (~Read/Write to ~AOE) = 0 cycles	*/
#define B3RAT_1			0x01000000  /* B3 Read Access Time = 1 cycle					*/
#define B3RAT_2			0x02000000  /* B3 Read Access Time = 2 cycles					*/
#define B3RAT_3			0x03000000  /* B3 Read Access Time = 3 cycles					*/
#define B3RAT_4			0x04000000  /* B3 Read Access Time = 4 cycles					*/
#define B3RAT_5			0x05000000  /* B3 Read Access Time = 5 cycles					*/
#define B3RAT_6			0x06000000  /* B3 Read Access Time = 6 cycles					*/
#define B3RAT_7			0x07000000  /* B3 Read Access Time = 7 cycles					*/
#define B3RAT_8			0x08000000  /* B3 Read Access Time = 8 cycles					*/
#define B3RAT_9			0x09000000  /* B3 Read Access Time = 9 cycles					*/
#define B3RAT_10		0x0A000000  /* B3 Read Access Time = 10 cycles					*/
#define B3RAT_11		0x0B000000  /* B3 Read Access Time = 11 cycles					*/
#define B3RAT_12		0x0C000000  /* B3 Read Access Time = 12 cycles					*/
#define B3RAT_13		0x0D000000  /* B3 Read Access Time = 13 cycles					*/
#define B3RAT_14		0x0E000000  /* B3 Read Access Time = 14 cycles					*/
#define B3RAT_15		0x0F000000  /* B3 Read Access Time = 15 cycles					*/
#define B3WAT_1			0x10000000  /* B3 Write Access Time = 1 cycle					*/
#define B3WAT_2			0x20000000  /* B3 Write Access Time = 2 cycles					*/
#define B3WAT_3			0x30000000  /* B3 Write Access Time = 3 cycles					*/
#define B3WAT_4			0x40000000  /* B3 Write Access Time = 4 cycles					*/
#define B3WAT_5			0x50000000  /* B3 Write Access Time = 5 cycles					*/
#define B3WAT_6			0x60000000  /* B3 Write Access Time = 6 cycles					*/
#define B3WAT_7			0x70000000  /* B3 Write Access Time = 7 cycles					*/
#define B3WAT_8			0x80000000  /* B3 Write Access Time = 8 cycles					*/
#define B3WAT_9			0x90000000  /* B3 Write Access Time = 9 cycles					*/
#define B3WAT_10		0xA0000000  /* B3 Write Access Time = 10 cycles					*/
#define B3WAT_11		0xB0000000  /* B3 Write Access Time = 11 cycles					*/
#define B3WAT_12		0xC0000000  /* B3 Write Access Time = 12 cycles					*/
#define B3WAT_13		0xD0000000  /* B3 Write Access Time = 13 cycles					*/
#define B3WAT_14		0xE0000000  /* B3 Write Access Time = 14 cycles					*/
#define B3WAT_15		0xF0000000  /* B3 Write Access Time = 15 cycles					*/


/* **********************  SDRAM CONTROLLER MASKS  **********************************************/
/* EBIU_SDGCTL Masks																			*/
#define SCTLE			0x00000001	/* Enable SDRAM Signals										*/
#define CL_2			0x00000008	/* SDRAM CAS Latency = 2 cycles								*/
#define CL_3			0x0000000C	/* SDRAM CAS Latency = 3 cycles								*/
#define PASR_ALL		0x00000000	/* All 4 SDRAM Banks Refreshed In Self-Refresh				*/
#define PASR_B0_B1		0x00000010	/* SDRAM Banks 0 and 1 Are Refreshed In Self-Refresh		*/
#define PASR_B0			0x00000020	/* Only SDRAM Bank 0 Is Refreshed In Self-Refresh			*/
#define TRAS_1			0x00000040	/* SDRAM tRAS = 1 cycle										*/
#define TRAS_2			0x00000080	/* SDRAM tRAS = 2 cycles									*/
#define TRAS_3			0x000000C0	/* SDRAM tRAS = 3 cycles									*/
#define TRAS_4			0x00000100	/* SDRAM tRAS = 4 cycles									*/
#define TRAS_5			0x00000140	/* SDRAM tRAS = 5 cycles									*/
#define TRAS_6			0x00000180	/* SDRAM tRAS = 6 cycles									*/
#define TRAS_7			0x000001C0	/* SDRAM tRAS = 7 cycles									*/
#define TRAS_8			0x00000200	/* SDRAM tRAS = 8 cycles									*/
#define TRAS_9			0x00000240	/* SDRAM tRAS = 9 cycles									*/
#define TRAS_10			0x00000280	/* SDRAM tRAS = 10 cycles									*/
#define TRAS_11			0x000002C0	/* SDRAM tRAS = 11 cycles									*/
#define TRAS_12			0x00000300	/* SDRAM tRAS = 12 cycles									*/
#define TRAS_13			0x00000340	/* SDRAM tRAS = 13 cycles									*/
#define TRAS_14			0x00000380	/* SDRAM tRAS = 14 cycles									*/
#define TRAS_15			0x000003C0	/* SDRAM tRAS = 15 cycles									*/
#define TRP_1			0x00000800	/* SDRAM tRP = 1 cycle										*/
#define TRP_2			0x00001000	/* SDRAM tRP = 2 cycles										*/
#define TRP_3			0x00001800	/* SDRAM tRP = 3 cycles										*/
#define TRP_4			0x00002000	/* SDRAM tRP = 4 cycles										*/
#define TRP_5			0x00002800	/* SDRAM tRP = 5 cycles										*/
#define TRP_6			0x00003000	/* SDRAM tRP = 6 cycles										*/
#define TRP_7			0x00003800	/* SDRAM tRP = 7 cycles										*/
#define TRCD_1			0x00008000	/* SDRAM tRCD = 1 cycle										*/
#define TRCD_2			0x00010000	/* SDRAM tRCD = 2 cycles									*/
#define TRCD_3			0x00018000	/* SDRAM tRCD = 3 cycles									*/
#define TRCD_4			0x00020000	/* SDRAM tRCD = 4 cycles									*/
#define TRCD_5			0x00028000	/* SDRAM tRCD = 5 cycles									*/
#define TRCD_6			0x00030000	/* SDRAM tRCD = 6 cycles									*/
#define TRCD_7			0x00038000	/* SDRAM tRCD = 7 cycles									*/
#define TWR_1			0x00080000	/* SDRAM tWR = 1 cycle										*/
#define TWR_2			0x00100000	/* SDRAM tWR = 2 cycles										*/
#define TWR_3			0x00180000	/* SDRAM tWR = 3 cycles										*/
#define PUPSD			0x00200000	/* Power-Up Start Delay (15 SCLK Cycles Delay)				*/
#define PSM				0x00400000	/* Power-Up Sequence (Mode Register Before/After* Refresh)	*/
#define PSS				0x00800000	/* Enable Power-Up Sequence on Next SDRAM Access			*/
#define SRFS			0x01000000	/* Enable SDRAM Self-Refresh Mode							*/
#define EBUFE			0x02000000	/* Enable External Buffering Timing							*/
#define FBBRW			0x04000000	/* Enable Fast Back-To-Back Read To Write					*/
#define EMREN			0x10000000	/* Extended Mode Register Enable							*/
#define TCSR			0x20000000	/* Temp-Compensated Self-Refresh Value (85/45* Deg C)		*/
#define CDDBG			0x40000000	/* Tristate SDRAM Controls During Bus Grant					*/

/* EBIU_SDBCTL Masks																		*/
#define EBE				0x0001		/* Enable SDRAM External Bank							*/
#define EBSZ_16			0x0000		/* SDRAM External Bank Size = 16MB	*/
#define EBSZ_32			0x0002		/* SDRAM External Bank Size = 32MB	*/
#define EBSZ_64			0x0004		/* SDRAM External Bank Size = 64MB	*/
#define EBSZ_128		0x0006		/* SDRAM External Bank Size = 128MB		*/
#define EBSZ_256		0x0008		/* SDRAM External Bank Size = 256MB 	*/
#define EBSZ_512		0x000A		/* SDRAM External Bank Size = 512MB		*/
#define EBCAW_8			0x0000		/* SDRAM External Bank Column Address Width = 8 Bits	*/
#define EBCAW_9			0x0010		/* SDRAM External Bank Column Address Width = 9 Bits	*/
#define EBCAW_10		0x0020		/* SDRAM External Bank Column Address Width = 10 Bits	*/
#define EBCAW_11		0x0030		/* SDRAM External Bank Column Address Width = 11 Bits	*/

/* EBIU_SDSTAT Masks														*/
#define SDCI			0x0001		/* SDRAM Controller Idle 				*/
#define SDSRA			0x0002		/* SDRAM Self-Refresh Active			*/
#define SDPUA			0x0004		/* SDRAM Power-Up Active 				*/
#define SDRS			0x0008		/* SDRAM Will Power-Up On Next Access	*/
#define SDEASE			0x0010		/* SDRAM EAB Sticky Error Status		*/
#define BGSTAT			0x0020		/* Bus Grant Status						*/


/* **************************  DMA CONTROLLER MASKS  ********************************/

/* DMAx_PERIPHERAL_MAP, MDMA_yy_PERIPHERAL_MAP Masks								*/
#define CTYPE			0x0040	/* DMA Channel Type Indicator (Memory/Peripheral*)	*/
#define PMAP			0xF000	/* Peripheral Mapped To This Channel				*/
#define PMAP_PPI		0x0000	/* 		PPI Port DMA								*/
#define	PMAP_EMACRX		0x1000	/* 		Ethernet Receive DMA						*/
#define PMAP_EMACTX		0x2000	/* 		Ethernet Transmit DMA						*/
#define PMAP_SPORT0RX	0x3000	/* 		SPORT0 Receive DMA							*/
#define PMAP_SPORT0TX	0x4000	/* 		SPORT0 Transmit DMA							*/
#define PMAP_SPORT1RX	0x5000	/* 		SPORT1 Receive DMA							*/
#define PMAP_SPORT1TX	0x6000	/* 		SPORT1 Transmit DMA							*/
#define PMAP_SPI		0x7000	/* 		SPI Port DMA								*/
#define PMAP_UART0RX	0x8000	/* 		UART0 Port Receive DMA						*/
#define PMAP_UART0TX	0x9000	/* 		UART0 Port Transmit DMA						*/
#define	PMAP_UART1RX	0xA000	/* 		UART1 Port Receive DMA						*/
#define	PMAP_UART1TX	0xB000	/* 		UART1 Port Transmit DMA						*/

/*  ************  PARALLEL PERIPHERAL INTERFACE (PPI) MASKS *************/
/*  PPI_CONTROL Masks													*/
#define PORT_EN			0x0001		/* PPI Port Enable					*/
#define PORT_DIR		0x0002		/* PPI Port Direction				*/
#define XFR_TYPE		0x000C		/* PPI Transfer Type				*/
#define PORT_CFG		0x0030		/* PPI Port Configuration			*/
#define FLD_SEL			0x0040		/* PPI Active Field Select			*/
#define PACK_EN			0x0080		/* PPI Packing Mode					*/
#define DMA32			0x0100		/* PPI 32-bit DMA Enable			*/
#define SKIP_EN			0x0200		/* PPI Skip Element Enable			*/
#define SKIP_EO			0x0400		/* PPI Skip Even/Odd Elements		*/
#define DLEN_8			0x0000		/* Data Length = 8 Bits				*/
#define DLEN_10			0x0800		/* Data Length = 10 Bits			*/
#define DLEN_11			0x1000		/* Data Length = 11 Bits			*/
#define DLEN_12			0x1800		/* Data Length = 12 Bits			*/
#define DLEN_13			0x2000		/* Data Length = 13 Bits			*/
#define DLEN_14			0x2800		/* Data Length = 14 Bits			*/
#define DLEN_15			0x3000		/* Data Length = 15 Bits			*/
#define DLEN_16			0x3800		/* Data Length = 16 Bits			*/
#define DLENGTH			0x3800		/* PPI Data Length  */
#define POLC			0x4000		/* PPI Clock Polarity				*/
#define POLS			0x8000		/* PPI Frame Sync Polarity			*/

/* PPI_STATUS Masks														*/
#define FLD				0x0400		/* Field Indicator					*/
#define FT_ERR			0x0800		/* Frame Track Error				*/
#define OVR				0x1000		/* FIFO Overflow Error				*/
#define UNDR			0x2000		/* FIFO Underrun Error				*/
#define ERR_DET			0x4000		/* Error Detected Indicator			*/
#define ERR_NCOR		0x8000		/* Error Not Corrected Indicator	*/


/*  ********************  TWO-WIRE INTERFACE (TWI) MASKS  ***********************/
/* TWI_CLKDIV Macros (Use: *pTWI_CLKDIV = CLKLOW(x)|CLKHI(y);  )				*/
#define	CLKLOW(x)	((x) & 0xFF)		/* Periods Clock Is Held Low			*/
#define CLKHI(y)	(((y)&0xFF)<<0x8)	/* Periods Before New Clock Low			*/

/* TWI_PRESCALE Masks															*/
#define	PRESCALE	0x007F		/* SCLKs Per Internal Time Reference (10MHz)	*/
#define	TWI_ENA		0x0080		/* TWI Enable									*/
#define	SCCB		0x0200		/* SCCB Compatibility Enable					*/

/* TWI_SLAVE_CTL Masks															*/
#define	SEN			0x0001		/* Slave Enable									*/
#define	SADD_LEN	0x0002		/* Slave Address Length							*/
#define	STDVAL		0x0004		/* Slave Transmit Data Valid					*/
#define	NAK			0x0008		/* NAK/ACK* Generated At Conclusion Of Transfer */
#define	GEN			0x0010		/* General Call Adrress Matching Enabled		*/

/* TWI_SLAVE_STAT Masks															*/
#define	SDIR		0x0001		/* Slave Transfer Direction (Transmit/Receive*)	*/
#define GCALL		0x0002		/* General Call Indicator						*/

/* TWI_MASTER_CTL Masks													*/
#define	MEN			0x0001		/* Master Mode Enable						*/
#define	MADD_LEN	0x0002		/* Master Address Length					*/
#define	MDIR		0x0004		/* Master Transmit Direction (RX/TX*)		*/
#define	FAST		0x0008		/* Use Fast Mode Timing Specs				*/
#define	STOP		0x0010		/* Issue Stop Condition						*/
#define	RSTART		0x0020		/* Repeat Start or Stop* At End Of Transfer	*/
#define	DCNT		0x3FC0		/* Data Bytes To Transfer					*/
#define	SDAOVR		0x4000		/* Serial Data Override						*/
#define	SCLOVR		0x8000		/* Serial Clock Override					*/

/* TWI_MASTER_STAT Masks														*/
#define	MPROG		0x0001		/* Master Transfer In Progress					*/
#define	LOSTARB		0x0002		/* Lost Arbitration Indicator (Xfer Aborted)	*/
#define	ANAK		0x0004		/* Address Not Acknowledged						*/
#define	DNAK		0x0008		/* Data Not Acknowledged						*/
#define	BUFRDERR	0x0010		/* Buffer Read Error							*/
#define	BUFWRERR	0x0020		/* Buffer Write Error							*/
#define	SDASEN		0x0040		/* Serial Data Sense							*/
#define	SCLSEN		0x0080		/* Serial Clock Sense							*/
#define	BUSBUSY		0x0100		/* Bus Busy Indicator							*/

/* TWI_INT_SRC and TWI_INT_ENABLE Masks						*/
#define	SINIT		0x0001		/* Slave Transfer Initiated	*/
#define	SCOMP		0x0002		/* Slave Transfer Complete	*/
#define	SERR		0x0004		/* Slave Transfer Error		*/
#define	SOVF		0x0008		/* Slave Overflow			*/
#define	MCOMP		0x0010		/* Master Transfer Complete	*/
#define	MERR		0x0020		/* Master Transfer Error	*/
#define	XMTSERV		0x0040		/* Transmit FIFO Service	*/
#define	RCVSERV		0x0080		/* Receive FIFO Service		*/

/* TWI_FIFO_CTRL Masks												*/
#define	XMTFLUSH	0x0001		/* Transmit Buffer Flush			*/
#define	RCVFLUSH	0x0002		/* Receive Buffer Flush				*/
#define	XMTINTLEN	0x0004		/* Transmit Buffer Interrupt Length	*/
#define	RCVINTLEN	0x0008		/* Receive Buffer Interrupt Length	*/

/* TWI_FIFO_STAT Masks															*/
#define	XMTSTAT		0x0003		/* Transmit FIFO Status							*/
#define	XMT_EMPTY	0x0000		/* 		Transmit FIFO Empty						*/
#define	XMT_HALF	0x0001		/* 		Transmit FIFO Has 1 Byte To Write		*/
#define	XMT_FULL	0x0003		/* 		Transmit FIFO Full (2 Bytes To Write)	*/

#define	RCVSTAT		0x000C		/* Receive FIFO Status							*/
#define	RCV_EMPTY	0x0000		/* 		Receive FIFO Empty						*/
#define	RCV_HALF	0x0004		/* 		Receive FIFO Has 1 Byte To Read			*/
#define	RCV_FULL	0x000C		/* 		Receive FIFO Full (2 Bytes To Read)		*/


/*  *******************  PIN CONTROL REGISTER MASKS  ************************/
/* PORT_MUX Masks															*/
#define	PJSE			0x0001			/* Port J SPI/SPORT Enable			*/
#define	PJSE_SPORT		0x0000			/* 		Enable TFS0/DT0PRI			*/
#define	PJSE_SPI		0x0001			/* 		Enable SPI_SSEL3:2			*/

#define	PJCE(x)			(((x)&0x3)<<1)	/* Port J CAN/SPI/SPORT Enable		*/
#define	PJCE_SPORT		0x0000			/* 		Enable DR0SEC/DT0SEC		*/
#define	PJCE_CAN		0x0002			/* 		Enable CAN RX/TX			*/
#define	PJCE_SPI		0x0004			/* 		Enable SPI_SSEL7			*/

#define	PFDE			0x0008			/* Port F DMA Request Enable		*/
#define	PFDE_UART		0x0000			/* 		Enable UART0 RX/TX			*/
#define	PFDE_DMA		0x0008			/* 		Enable DMAR1:0				*/

#define	PFTE			0x0010			/* Port F Timer Enable				*/
#define	PFTE_UART		0x0000			/*		Enable UART1 RX/TX			*/
#define	PFTE_TIMER		0x0010			/* 		Enable TMR7:6				*/

#define	PFS6E			0x0020			/* Port F SPI SSEL 6 Enable			*/
#define	PFS6E_TIMER		0x0000			/*		Enable TMR5					*/
#define	PFS6E_SPI		0x0020			/* 		Enable SPI_SSEL6			*/

#define	PFS5E			0x0040			/* Port F SPI SSEL 5 Enable			*/
#define	PFS5E_TIMER		0x0000			/*		Enable TMR4					*/
#define	PFS5E_SPI		0x0040			/* 		Enable SPI_SSEL5			*/

#define	PFS4E			0x0080			/* Port F SPI SSEL 4 Enable			*/
#define	PFS4E_TIMER		0x0000			/*		Enable TMR3					*/
#define	PFS4E_SPI		0x0080			/* 		Enable SPI_SSEL4			*/

#define	PFFE			0x0100			/* Port F PPI Frame Sync Enable		*/
#define	PFFE_TIMER		0x0000			/* 		Enable TMR2					*/
#define	PFFE_PPI		0x0100			/* 		Enable PPI FS3				*/

#define	PGSE			0x0200			/* Port G SPORT1 Secondary Enable	*/
#define	PGSE_PPI		0x0000			/* 		Enable PPI D9:8				*/
#define	PGSE_SPORT		0x0200			/* 		Enable DR1SEC/DT1SEC		*/

#define	PGRE			0x0400			/* Port G SPORT1 Receive Enable		*/
#define	PGRE_PPI		0x0000			/* 		Enable PPI D12:10			*/
#define	PGRE_SPORT		0x0400			/* 		Enable DR1PRI/RFS1/RSCLK1	*/

#define	PGTE			0x0800			/* Port G SPORT1 Transmit Enable	*/
#define	PGTE_PPI		0x0000			/* 		Enable PPI D15:13			*/
#define	PGTE_SPORT		0x0800			/* 		Enable DT1PRI/TFS1/TSCLK1	*/


/*  ******************  HANDSHAKE DMA (HDMA) MASKS  *********************/
/* HDMAx_CTL Masks														*/
#define	HMDMAEN		0x0001	/* Enable Handshake DMA 0/1					*/
#define	REP			0x0002	/* HDMA Request Polarity					*/
#define	UTE			0x0004	/* Urgency Threshold Enable					*/
#define	OIE			0x0010	/* Overflow Interrupt Enable				*/
#define	BDIE		0x0020	/* Block Done Interrupt Enable				*/
#define	MBDI		0x0040	/* Mask Block Done IRQ If Pending ECNT		*/
#define	DRQ			0x0300	/* HDMA Request Type						*/
#define	DRQ_NONE	0x0000	/* 		No Request							*/
#define	DRQ_SINGLE	0x0100	/* 		Channels Request Single				*/
#define	DRQ_MULTI	0x0200	/* 		Channels Request Multi (Default)	*/
#define	DRQ_URGENT	0x0300	/* 		Channels Request Multi Urgent		*/
#define	RBC			0x1000	/* Reload BCNT With IBCNT					*/
#define	PS			0x2000	/* HDMA Pin Status							*/
#define	OI			0x4000	/* Overflow Interrupt Generated				*/
#define	BDI			0x8000	/* Block Done Interrupt Generated			*/

/* entry addresses of the user-callable Boot ROM functions */

#define _BOOTROM_RESET 0xEF000000
#define _BOOTROM_FINAL_INIT 0xEF000002
#define _BOOTROM_DO_MEMORY_DMA 0xEF000006
#define _BOOTROM_BOOT_DXE_FLASH 0xEF000008
#define _BOOTROM_BOOT_DXE_SPI 0xEF00000A
#define _BOOTROM_BOOT_DXE_TWI 0xEF00000C
#define _BOOTROM_GET_DXE_ADDRESS_FLASH 0xEF000010
#define _BOOTROM_GET_DXE_ADDRESS_SPI 0xEF000012
#define _BOOTROM_GET_DXE_ADDRESS_TWI 0xEF000014

/* Alternate Deprecated Macros Provided For Backwards Code Compatibility */
#define	PGDE_UART   PFDE_UART
#define	PGDE_DMA    PFDE_DMA
#define	CKELOW		SCKELOW

/* HOST Port Registers */

#define                     HOST_CONTROL  0xffc03400   /* HOST Control Register */
#define                      HOST_STATUS  0xffc03404   /* HOST Status Register */
#define                     HOST_TIMEOUT  0xffc03408   /* HOST Acknowledge Mode Timeout Register */

/* Counter Registers */

#define                       CNT_CONFIG  0xffc03500   /* Configuration Register */
#define                        CNT_IMASK  0xffc03504   /* Interrupt Mask Register */
#define                       CNT_STATUS  0xffc03508   /* Status Register */
#define                      CNT_COMMAND  0xffc0350c   /* Command Register */
#define                     CNT_DEBOUNCE  0xffc03510   /* Debounce Register */
#define                      CNT_COUNTER  0xffc03514   /* Counter Register */
#define                          CNT_MAX  0xffc03518   /* Maximal Count Register */
#define                          CNT_MIN  0xffc0351c   /* Minimal Count Register */

/* OTP/FUSE Registers */

#define                      OTP_CONTROL  0xffc03600   /* OTP/Fuse Control Register */
#define                          OTP_BEN  0xffc03604   /* OTP/Fuse Byte Enable */
#define                       OTP_STATUS  0xffc03608   /* OTP/Fuse Status */
#define                       OTP_TIMING  0xffc0360c   /* OTP/Fuse Access Timing */

/* Security Registers */

#define                    SECURE_SYSSWT  0xffc03620   /* Secure System Switches */
#define                   SECURE_CONTROL  0xffc03624   /* Secure Control */
#define                    SECURE_STATUS  0xffc03628   /* Secure Status */

/* OTP Read/Write Data Buffer Registers */

#define                        OTP_DATA0  0xffc03680   /* OTP/Fuse Data (OTP_DATA0-3) accesses the fuse read write buffer */
#define                        OTP_DATA1  0xffc03684   /* OTP/Fuse Data (OTP_DATA0-3) accesses the fuse read write buffer */
#define                        OTP_DATA2  0xffc03688   /* OTP/Fuse Data (OTP_DATA0-3) accesses the fuse read write buffer */
#define                        OTP_DATA3  0xffc0368c   /* OTP/Fuse Data (OTP_DATA0-3) accesses the fuse read write buffer */

/* Motor Control PWM Registers */

#define                         PWM_CTRL  0xffc03700   /* PWM Control Register */
#define                         PWM_STAT  0xffc03704   /* PWM Status Register */
#define                           PWM_TM  0xffc03708   /* PWM Period Register */
#define                           PWM_DT  0xffc0370c   /* PWM Dead Time Register */
#define                         PWM_GATE  0xffc03710   /* PWM Chopping Control */
#define                          PWM_CHA  0xffc03714   /* PWM Channel A Duty Control */
#define                          PWM_CHB  0xffc03718   /* PWM Channel B Duty Control */
#define                          PWM_CHC  0xffc0371c   /* PWM Channel C Duty Control */
#define                          PWM_SEG  0xffc03720   /* PWM Crossover and Output Enable */
#define                       PWM_SYNCWT  0xffc03724   /* PWM Sync Pluse Width Control */
#define                         PWM_CHAL  0xffc03728   /* PWM Channel AL Duty Control (SR mode only) */
#define                         PWM_CHBL  0xffc0372c   /* PWM Channel BL Duty Control (SR mode only) */
#define                         PWM_CHCL  0xffc03730   /* PWM Channel CL Duty Control (SR mode only) */
#define                          PWM_LSI  0xffc03734   /* PWM Low Side Invert (SR mode only) */
#define                        PWM_STAT2  0xffc03738   /* PWM Status Register 2 */


/* ********************************************************** */
/*     SINGLE BIT MACRO PAIRS (bit mask and negated one)      */
/*     and MULTI BIT READ MACROS                              */
/* ********************************************************** */

/* Bit masks for HOST_CONTROL */

#define                   HOST_CNTR_HOST_EN  0x1        /* Host Enable */
#define                  HOST_CNTR_nHOST_EN  0x0
#define                  HOST_CNTR_HOST_END  0x2        /* Host Endianess */
#define                 HOST_CNTR_nHOST_END  0x0
#define                 HOST_CNTR_DATA_SIZE  0x4        /* Data Size */
#define                HOST_CNTR_nDATA_SIZE  0x0
#define                  HOST_CNTR_HOST_RST  0x8        /* Host Reset */
#define                 HOST_CNTR_nHOST_RST  0x0
#define                  HOST_CNTR_HRDY_OVR  0x20       /* Host Ready Override */
#define                 HOST_CNTR_nHRDY_OVR  0x0
#define                  HOST_CNTR_INT_MODE  0x40       /* Interrupt Mode */
#define                 HOST_CNTR_nINT_MODE  0x0
#define                     HOST_CNTR_BT_EN  0x80       /* Bus Timeout Enable */
#define                   HOST_CNTR_ nBT_EN  0x0
#define                       HOST_CNTR_EHW  0x100      /* Enable Host Write */
#define                      HOST_CNTR_nEHW  0x0
#define                       HOST_CNTR_EHR  0x200      /* Enable Host Read */
#define                      HOST_CNTR_nEHR  0x0
#define                       HOST_CNTR_BDR  0x400      /* Burst DMA Requests */
#define                      HOST_CNTR_nBDR  0x0

/* Bit masks for HOST_STATUS */

#define                     HOST_STAT_READY  0x1        /* DMA Ready */
#define                    HOST_STAT_nREADY  0x0
#define                  HOST_STAT_FIFOFULL  0x2        /* FIFO Full */
#define                 HOST_STAT_nFIFOFULL  0x0
#define                 HOST_STAT_FIFOEMPTY  0x4        /* FIFO Empty */
#define                HOST_STAT_nFIFOEMPTY  0x0
#define                  HOST_STAT_COMPLETE  0x8        /* DMA Complete */
#define                 HOST_STAT_nCOMPLETE  0x0
#define                      HOST_STAT_HSHK  0x10       /* Host Handshake */
#define                     HOST_STAT_nHSHK  0x0
#define                   HOST_STAT_TIMEOUT  0x20       /* Host Timeout */
#define                  HOST_STAT_nTIMEOUT  0x0
#define                      HOST_STAT_HIRQ  0x40       /* Host Interrupt Request */
#define                     HOST_STAT_nHIRQ  0x0
#define                HOST_STAT_ALLOW_CNFG  0x80       /* Allow New Configuration */
#define               HOST_STAT_nALLOW_CNFG  0x0
#define                   HOST_STAT_DMA_DIR  0x100      /* DMA Direction */
#define                  HOST_STAT_nDMA_DIR  0x0
#define                       HOST_STAT_BTE  0x200      /* Bus Timeout Enabled */
#define                      HOST_STAT_nBTE  0x0
#define               HOST_STAT_HOSTRD_DONE  0x8000     /* Host Read Completion Interrupt */
#define              HOST_STAT_nHOSTRD_DONE  0x0

/* Bit masks for HOST_TIMEOUT */

#define             HOST_COUNT_TIMEOUT  0x7ff      /* Host Timeout count */

/* Bit masks for SECURE_SYSSWT */

#define                   EMUDABL  0x1        /* Emulation Disable. */
#define                  nEMUDABL  0x0
#define                   RSTDABL  0x2        /* Reset Disable */
#define                  nRSTDABL  0x0
#define                   L1IDABL  0x1c       /* L1 Instruction Memory Disable. */
#define                  L1DADABL  0xe0       /* L1 Data Bank A Memory Disable. */
#define                  L1DBDABL  0x700      /* L1 Data Bank B Memory Disable. */
#define                   DMA0OVR  0x800      /* DMA0 Memory Access Override */
#define                  nDMA0OVR  0x0
#define                   DMA1OVR  0x1000     /* DMA1 Memory Access Override */
#define                  nDMA1OVR  0x0
#define                    EMUOVR  0x4000     /* Emulation Override */
#define                   nEMUOVR  0x0
#define                    OTPSEN  0x8000     /* OTP Secrets Enable. */
#define                   nOTPSEN  0x0
#define                    L2DABL  0x70000    /* L2 Memory Disable. */

/* Bit masks for SECURE_CONTROL */

#define                   SECURE0  0x1        /* SECURE 0 */
#define                  nSECURE0  0x0
#define                   SECURE1  0x2        /* SECURE 1 */
#define                  nSECURE1  0x0
#define                   SECURE2  0x4        /* SECURE 2 */
#define                  nSECURE2  0x0
#define                   SECURE3  0x8        /* SECURE 3 */
#define                  nSECURE3  0x0

/* Bit masks for SECURE_STATUS */

#define                   SECMODE  0x3        /* Secured Mode Control State */
#define                       NMI  0x4        /* Non Maskable Interrupt */
#define                      nNMI  0x0
#define                   AFVALID  0x8        /* Authentication Firmware Valid */
#define                  nAFVALID  0x0
#define                    AFEXIT  0x10       /* Authentication Firmware Exit */
#define                   nAFEXIT  0x0
#define                   SECSTAT  0xe0       /* Secure Status */



#endif /* _DEF_BF51X_H */

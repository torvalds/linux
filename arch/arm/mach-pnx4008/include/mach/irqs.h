/*
 * arch/arm/mach-pnx4008/include/mach/irqs.h
 *
 * PNX4008 IRQ controller driver - header file
 *
 * Author: Dmitry Chigirev <source@mvista.com>
 *
 * 2005 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#ifndef __PNX4008_IRQS_h__
#define __PNX4008_IRQS_h__

#define NR_IRQS         96

/*Manual: table 259, page 199*/

/*SUB2 Interrupt Routing (SIC2)*/

#define SIC2_BASE_INT   64

#define CLK_SWITCH_ARM_INT 95	/*manual: Clkswitch ARM  */
#define CLK_SWITCH_DSP_INT 94	/*manual: ClkSwitch DSP  */
#define CLK_SWITCH_AUD_INT 93	/*manual: Clkswitch AUD  */
#define GPI_06_INT         92
#define GPI_05_INT         91
#define GPI_04_INT         90
#define GPI_03_INT         89
#define GPI_02_INT         88
#define GPI_01_INT         87
#define GPI_00_INT         86
#define BT_CLKREQ_INT      85
#define SPI1_DATIN_INT     84
#define U5_RX_INT          83
#define SDIO_INT_N         82
#define CAM_HS_INT         81
#define CAM_VS_INT         80
#define GPI_07_INT         79
#define DISP_SYNC_INT      78
#define DSP_INT8           77
#define U7_HCTS_INT        76
#define GPI_10_INT         75
#define GPI_09_INT         74
#define GPI_08_INT         73
#define DSP_INT7           72
#define U2_HCTS_INT        71
#define SPI2_DATIN_INT     70
#define GPIO_05_INT        69
#define GPIO_04_INT        68
#define GPIO_03_INT        67
#define GPIO_02_INT        66
#define GPIO_01_INT        65
#define GPIO_00_INT        64

/*Manual: table 258, page 198*/

/*SUB1 Interrupt Routing (SIC1)*/

#define SIC1_BASE_INT   32

#define USB_I2C_INT        63
#define USB_DEV_HP_INT     62
#define USB_DEV_LP_INT     61
#define USB_DEV_DMA_INT    60
#define USB_HOST_INT       59
#define USB_OTG_ATX_INT_N  58
#define USB_OTG_TIMER_INT  57
#define SW_INT             56
#define SPI1_INT           55
#define KEY_IRQ            54
#define DSP_M_INT          53
#define RTC_INT            52
#define I2C_1_INT          51
#define I2C_2_INT          50
#define PLL1_LOCK_INT      49
#define PLL2_LOCK_INT      48
#define PLL3_LOCK_INT      47
#define PLL4_LOCK_INT      46
#define PLL5_LOCK_INT      45
#define SPI2_INT           44
#define DSP_INT1           43
#define DSP_INT2           42
#define DSP_TDM_INT2       41
#define TS_AUX_INT         40
#define TS_IRQ             39
#define TS_P_INT           38
#define UOUT1_TO_PAD_INT   37
#define GPI_11_INT         36
#define DSP_INT4           35
#define JTAG_COMM_RX_INT   34
#define JTAG_COMM_TX_INT   33
#define DSP_INT3           32

/*Manual: table 257, page 197*/

/*MAIN Interrupt Routing*/

#define MAIN_BASE_INT   0

#define SUB2_FIQ_N         31	/*active low */
#define SUB1_FIQ_N         30	/*active low */
#define JPEG_INT           29
#define DMA_INT            28
#define MSTIMER_INT        27
#define IIR1_INT           26
#define IIR2_INT           25
#define IIR7_INT           24
#define DSP_TDM_INT0       23
#define DSP_TDM_INT1       22
#define DSP_P_INT          21
#define DSP_INT0           20
#define DUM_INT            19
#define UOUT0_TO_PAD_INT   18
#define MP4_ENC_INT        17
#define MP4_DEC_INT        16
#define SD0_INT            15
#define MBX_INT            14
#define SD1_INT            13
#define MS_INT_N           12
#define FLASH_INT          11 /*NAND*/
#define IIR6_INT           10
#define IIR5_INT           9
#define IIR4_INT           8
#define IIR3_INT           7
#define WATCH_INT          6
#define HSTIMER_INT        5
#define ARCH_TIMER_IRQ     HSTIMER_INT
#define CAM_INT            4
#define PRNG_INT           3
#define CRYPTO_INT         2
#define SUB2_IRQ_N         1	/*active low */
#define SUB1_IRQ_N         0	/*active low */

#define PNX4008_IRQ_TYPES \
{                                           /*IRQ #'s: */         \
IRQ_TYPE_LEVEL_LOW,  IRQ_TYPE_LEVEL_LOW,  IRQ_TYPE_LEVEL_LOW,  IRQ_TYPE_LEVEL_HIGH, /*  0, 1, 2, 3 */     \
IRQ_TYPE_LEVEL_LOW,  IRQ_TYPE_LEVEL_HIGH, IRQ_TYPE_LEVEL_HIGH, IRQ_TYPE_LEVEL_HIGH, /*  4, 5, 6, 7 */     \
IRQ_TYPE_LEVEL_HIGH, IRQ_TYPE_LEVEL_HIGH, IRQ_TYPE_LEVEL_HIGH, IRQ_TYPE_LEVEL_HIGH, /*  8, 9,10,11 */     \
IRQ_TYPE_LEVEL_LOW,  IRQ_TYPE_LEVEL_HIGH, IRQ_TYPE_LEVEL_HIGH, IRQ_TYPE_LEVEL_HIGH, /* 12,13,14,15 */     \
IRQ_TYPE_LEVEL_HIGH, IRQ_TYPE_LEVEL_HIGH, IRQ_TYPE_LEVEL_HIGH, IRQ_TYPE_LEVEL_HIGH, /* 16,17,18,19 */     \
IRQ_TYPE_LEVEL_HIGH, IRQ_TYPE_LEVEL_HIGH, IRQ_TYPE_LEVEL_HIGH, IRQ_TYPE_LEVEL_HIGH, /* 20,21,22,23 */     \
IRQ_TYPE_LEVEL_HIGH, IRQ_TYPE_LEVEL_HIGH, IRQ_TYPE_LEVEL_HIGH, IRQ_TYPE_LEVEL_HIGH, /* 24,25,26,27 */     \
IRQ_TYPE_LEVEL_HIGH, IRQ_TYPE_LEVEL_HIGH, IRQ_TYPE_LEVEL_LOW,  IRQ_TYPE_LEVEL_LOW,  /* 28,29,30,31 */     \
IRQ_TYPE_LEVEL_HIGH, IRQ_TYPE_LEVEL_LOW,  IRQ_TYPE_LEVEL_HIGH, IRQ_TYPE_LEVEL_HIGH, /* 32,33,34,35 */     \
IRQ_TYPE_LEVEL_HIGH, IRQ_TYPE_LEVEL_HIGH, IRQ_TYPE_EDGE_FALLING, IRQ_TYPE_LEVEL_HIGH, /* 36,37,38,39 */  \
IRQ_TYPE_LEVEL_HIGH, IRQ_TYPE_LEVEL_HIGH, IRQ_TYPE_LEVEL_HIGH, IRQ_TYPE_LEVEL_HIGH, /* 40,41,42,43 */     \
IRQ_TYPE_LEVEL_HIGH, IRQ_TYPE_LEVEL_HIGH, IRQ_TYPE_LEVEL_HIGH, IRQ_TYPE_LEVEL_HIGH, /* 44,45,46,47 */     \
IRQ_TYPE_LEVEL_HIGH, IRQ_TYPE_LEVEL_HIGH, IRQ_TYPE_LEVEL_LOW,  IRQ_TYPE_LEVEL_LOW,  /* 48,49,50,51 */     \
IRQ_TYPE_LEVEL_HIGH, IRQ_TYPE_LEVEL_HIGH, IRQ_TYPE_LEVEL_HIGH, IRQ_TYPE_LEVEL_HIGH, /* 52,53,54,55 */     \
IRQ_TYPE_LEVEL_HIGH, IRQ_TYPE_LEVEL_HIGH, IRQ_TYPE_LEVEL_LOW,  IRQ_TYPE_LEVEL_HIGH, /* 56,57,58,59 */     \
IRQ_TYPE_LEVEL_HIGH, IRQ_TYPE_LEVEL_HIGH, IRQ_TYPE_LEVEL_HIGH, IRQ_TYPE_LEVEL_HIGH, /* 60,61,62,63 */     \
IRQ_TYPE_LEVEL_HIGH, IRQ_TYPE_LEVEL_HIGH, IRQ_TYPE_LEVEL_HIGH, IRQ_TYPE_LEVEL_HIGH, /* 64,65,66,67 */     \
IRQ_TYPE_LEVEL_HIGH, IRQ_TYPE_LEVEL_HIGH, IRQ_TYPE_LEVEL_HIGH, IRQ_TYPE_LEVEL_HIGH, /* 68,69,70,71 */     \
IRQ_TYPE_LEVEL_HIGH, IRQ_TYPE_LEVEL_HIGH, IRQ_TYPE_LEVEL_HIGH, IRQ_TYPE_LEVEL_HIGH, /* 72,73,74,75 */     \
IRQ_TYPE_LEVEL_HIGH, IRQ_TYPE_LEVEL_HIGH, IRQ_TYPE_LEVEL_HIGH, IRQ_TYPE_LEVEL_HIGH, /* 76,77,78,79 */     \
IRQ_TYPE_LEVEL_HIGH, IRQ_TYPE_LEVEL_HIGH, IRQ_TYPE_LEVEL_HIGH, IRQ_TYPE_LEVEL_HIGH, /* 80,81,82,83 */     \
IRQ_TYPE_LEVEL_HIGH, IRQ_TYPE_LEVEL_HIGH, IRQ_TYPE_LEVEL_HIGH, IRQ_TYPE_LEVEL_HIGH, /* 84,85,86,87 */     \
IRQ_TYPE_LEVEL_HIGH, IRQ_TYPE_LEVEL_HIGH, IRQ_TYPE_LEVEL_HIGH, IRQ_TYPE_LEVEL_HIGH, /* 88,89,90,91 */     \
IRQ_TYPE_LEVEL_HIGH, IRQ_TYPE_LEVEL_HIGH, IRQ_TYPE_LEVEL_HIGH, IRQ_TYPE_LEVEL_HIGH, /* 92,93,94,95 */     \
}

/* Start Enable Pin Interrupts - table 58 page 66 */

#define SE_PIN_BASE_INT   32

#define SE_U7_RX_INT            63
#define SE_U7_HCTS_INT          62
#define SE_BT_CLKREQ_INT        61
#define SE_U6_IRRX_INT          60
/*59 unused*/
#define SE_U5_RX_INT            58
#define SE_GPI_11_INT           57
#define SE_U3_RX_INT            56
#define SE_U2_HCTS_INT          55
#define SE_U2_RX_INT            54
#define SE_U1_RX_INT            53
#define SE_DISP_SYNC_INT        52
/*51 unused*/
#define SE_SDIO_INT_N           50
#define SE_MSDIO_START_INT      49
#define SE_GPI_06_INT           48
#define SE_GPI_05_INT           47
#define SE_GPI_04_INT           46
#define SE_GPI_03_INT           45
#define SE_GPI_02_INT           44
#define SE_GPI_01_INT           43
#define SE_GPI_00_INT           42
#define SE_SYSCLKEN_PIN_INT     41
#define SE_SPI1_DATAIN_INT      40
#define SE_GPI_07_INT           39
#define SE_SPI2_DATAIN_INT      38
#define SE_GPI_10_INT           37
#define SE_GPI_09_INT           36
#define SE_GPI_08_INT           35
/*34-32 unused*/

/* Start Enable Internal Interrupts - table 57 page 65 */

#define SE_INT_BASE_INT   0

#define SE_TS_IRQ               31
#define SE_TS_P_INT             30
#define SE_TS_AUX_INT           29
/*27-28 unused*/
#define SE_USB_AHB_NEED_CLK_INT 26
#define SE_MSTIMER_INT          25
#define SE_RTC_INT              24
#define SE_USB_NEED_CLK_INT     23
#define SE_USB_INT              22
#define SE_USB_I2C_INT          21
#define SE_USB_OTG_TIMER_INT    20

#endif /* __PNX4008_IRQS_h__ */

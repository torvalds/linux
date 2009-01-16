#ifndef __ASM_ARCH_PXA2XX_GPIO_H
#define __ASM_ARCH_PXA2XX_GPIO_H

#warning Please use mfp-pxa2[57]x.h instead of pxa2xx-gpio.h

/* GPIO alternate function assignments */

#define GPIO1_RST		1	/* reset */
#define GPIO6_MMCCLK		6	/* MMC Clock */
#define GPIO7_48MHz		7	/* 48 MHz clock output */
#define GPIO8_MMCCS0		8	/* MMC Chip Select 0 */
#define GPIO9_MMCCS1		9	/* MMC Chip Select 1 */
#define GPIO10_RTCCLK		10	/* real time clock (1 Hz) */
#define GPIO11_3_6MHz		11	/* 3.6 MHz oscillator out */
#define GPIO12_32KHz		12	/* 32 kHz out */
#define GPIO12_CIF_DD_7		12	/* Camera data pin 7 */
#define GPIO13_MBGNT		13	/* memory controller grant */
#define GPIO14_MBREQ		14	/* alternate bus master request */
#define GPIO15_nCS_1		15	/* chip select 1 */
#define GPIO16_PWM0		16	/* PWM0 output */
#define GPIO17_PWM1		17	/* PWM1 output */
#define GPIO17_CIF_DD_6		17	/* Camera data pin 6 */
#define GPIO18_RDY		18	/* Ext. Bus Ready */
#define GPIO19_DREQ1		19	/* External DMA Request */
#define GPIO20_DREQ0		20	/* External DMA Request */
#define GPIO23_SCLK		23	/* SSP clock */
#define GPIO23_CIF_MCLK		23	/* Camera Master Clock */
#define GPIO24_SFRM		24	/* SSP Frame */
#define GPIO24_CIF_FV		24	/* Camera frame start signal */
#define GPIO25_STXD		25	/* SSP transmit */
#define GPIO25_CIF_LV		25	/* Camera line start signal */
#define GPIO26_SRXD		26	/* SSP receive */
#define GPIO26_CIF_PCLK		26	/* Camera Pixel Clock */
#define GPIO27_SEXTCLK		27	/* SSP ext_clk */
#define GPIO27_CIF_DD_0		27	/* Camera data pin 0 */
#define GPIO28_BITCLK		28	/* AC97/I2S bit_clk */
#define GPIO29_SDATA_IN		29	/* AC97 Sdata_in0 / I2S Sdata_in */
#define GPIO30_SDATA_OUT	30	/* AC97/I2S Sdata_out */
#define GPIO31_SYNC		31	/* AC97/I2S sync */
#define GPIO32_SDATA_IN1	32	/* AC97 Sdata_in1 */
#define GPIO32_SYSCLK		32	/* I2S System Clock */
#define GPIO32_MMCCLK		32	/* MMC Clock (PXA270) */
#define GPIO33_nCS_5		33	/* chip select 5 */
#define GPIO34_FFRXD		34	/* FFUART receive */
#define GPIO34_MMCCS0		34	/* MMC Chip Select 0 */
#define GPIO35_FFCTS		35	/* FFUART Clear to send */
#define GPIO36_FFDCD		36	/* FFUART Data carrier detect */
#define GPIO37_FFDSR		37	/* FFUART data set ready */
#define GPIO38_FFRI		38	/* FFUART Ring Indicator */
#define GPIO39_MMCCS1		39	/* MMC Chip Select 1 */
#define GPIO39_FFTXD		39	/* FFUART transmit data */
#define GPIO40_FFDTR		40	/* FFUART data terminal Ready */
#define GPIO41_FFRTS		41	/* FFUART request to send */
#define GPIO42_BTRXD		42	/* BTUART receive data */
#define GPIO42_HWRXD		42	/* HWUART receive data */
#define GPIO42_CIF_MCLK		42	/* Camera Master Clock */
#define GPIO43_BTTXD		43	/* BTUART transmit data */
#define GPIO43_HWTXD		43	/* HWUART transmit data */
#define GPIO43_CIF_FV		43	/* Camera frame start signal */
#define GPIO44_BTCTS		44	/* BTUART clear to send */
#define GPIO44_HWCTS		44	/* HWUART clear to send */
#define GPIO44_CIF_LV		44	/* Camera line start signal */
#define GPIO45_BTRTS		45	/* BTUART request to send */
#define GPIO45_HWRTS		45	/* HWUART request to send */
#define GPIO45_AC97_SYSCLK	45	/* AC97 System Clock */
#define GPIO45_CIF_PCLK		45	/* Camera Pixel Clock */
#define GPIO46_ICPRXD		46	/* ICP receive data */
#define GPIO46_STRXD		46	/* STD_UART receive data */
#define GPIO47_ICPTXD		47	/* ICP transmit data */
#define GPIO47_STTXD		47	/* STD_UART transmit data */
#define GPIO47_CIF_DD_0		47	/* Camera data pin 0 */
#define GPIO48_nPOE		48	/* Output Enable for Card Space */
#define GPIO48_CIF_DD_5		48	/* Camera data pin 5 */
#define GPIO49_nPWE		49	/* Write Enable for Card Space */
#define GPIO50_nPIOR		50	/* I/O Read for Card Space */
#define GPIO50_CIF_DD_3		50	/* Camera data pin 3 */
#define GPIO51_nPIOW		51	/* I/O Write for Card Space */
#define GPIO51_CIF_DD_2		51	/* Camera data pin 2 */
#define GPIO52_nPCE_1		52	/* Card Enable for Card Space */
#define GPIO52_CIF_DD_4		52	/* Camera data pin 4 */
#define GPIO53_nPCE_2		53	/* Card Enable for Card Space */
#define GPIO53_MMCCLK		53	/* MMC Clock */
#define GPIO53_CIF_MCLK		53	/* Camera Master Clock */
#define GPIO54_MMCCLK		54	/* MMC Clock */
#define GPIO54_pSKTSEL		54	/* Socket Select for Card Space */
#define GPIO54_nPCE_2		54	/* Card Enable for Card Space (PXA27x) */
#define GPIO54_CIF_PCLK		54	/* Camera Pixel Clock */
#define GPIO55_nPREG		55	/* Card Address bit 26 */
#define GPIO55_CIF_DD_1		55	/* Camera data pin 1 */
#define GPIO56_nPWAIT		56	/* Wait signal for Card Space */
#define GPIO57_nIOIS16		57	/* Bus Width select for I/O Card Space */
#define GPIO58_LDD_0		58	/* LCD data pin 0 */
#define GPIO59_LDD_1		59	/* LCD data pin 1 */
#define GPIO60_LDD_2		60	/* LCD data pin 2 */
#define GPIO61_LDD_3		61	/* LCD data pin 3 */
#define GPIO62_LDD_4		62	/* LCD data pin 4 */
#define GPIO63_LDD_5		63	/* LCD data pin 5 */
#define GPIO64_LDD_6		64	/* LCD data pin 6 */
#define GPIO65_LDD_7		65	/* LCD data pin 7 */
#define GPIO66_LDD_8		66	/* LCD data pin 8 */
#define GPIO66_MBREQ		66	/* alternate bus master req */
#define GPIO67_LDD_9		67	/* LCD data pin 9 */
#define GPIO67_MMCCS0		67	/* MMC Chip Select 0 */
#define GPIO68_LDD_10		68	/* LCD data pin 10 */
#define GPIO68_MMCCS1		68	/* MMC Chip Select 1 */
#define GPIO69_LDD_11		69	/* LCD data pin 11 */
#define GPIO69_MMCCLK		69	/* MMC_CLK */
#define GPIO70_LDD_12		70	/* LCD data pin 12 */
#define GPIO70_RTCCLK		70	/* Real Time clock (1 Hz) */
#define GPIO71_LDD_13		71	/* LCD data pin 13 */
#define GPIO71_3_6MHz		71	/* 3.6 MHz Oscillator clock */
#define GPIO72_LDD_14		72	/* LCD data pin 14 */
#define GPIO72_32kHz		72	/* 32 kHz clock */
#define GPIO73_LDD_15		73	/* LCD data pin 15 */
#define GPIO73_MBGNT		73	/* Memory controller grant */
#define GPIO74_LCD_FCLK		74	/* LCD Frame clock */
#define GPIO75_LCD_LCLK		75	/* LCD line clock */
#define GPIO76_LCD_PCLK		76	/* LCD Pixel clock */
#define GPIO77_LCD_ACBIAS	77	/* LCD AC Bias */
#define GPIO78_nCS_2		78	/* chip select 2 */
#define GPIO79_nCS_3		79	/* chip select 3 */
#define GPIO80_nCS_4		80	/* chip select 4 */
#define GPIO81_NSCLK		81	/* NSSP clock */
#define GPIO81_CIF_DD_0		81	/* Camera data pin 0 */
#define GPIO82_NSFRM		82	/* NSSP Frame */
#define GPIO82_CIF_DD_5		82	/* Camera data pin 5 */
#define GPIO83_NSTXD		83	/* NSSP transmit */
#define GPIO83_CIF_DD_4		83	/* Camera data pin 4 */
#define GPIO84_NSRXD		84	/* NSSP receive */
#define GPIO84_CIF_FV		84	/* Camera frame start signal */
#define GPIO85_nPCE_1		85	/* Card Enable for Card Space (PXA27x) */
#define GPIO85_CIF_LV		85	/* Camera line start signal */
#define GPIO90_CIF_DD_4		90	/* Camera data pin 4 */
#define GPIO91_CIF_DD_5		91	/* Camera data pin 5 */
#define GPIO92_MMCDAT0		92	/* MMC DAT0 (PXA27x) */
#define GPIO93_CIF_DD_6		93	/* Camera data pin 6 */
#define GPIO94_CIF_DD_5		94	/* Camera data pin 5 */
#define GPIO95_CIF_DD_4		95	/* Camera data pin 4 */
#define GPIO96_FFRXD		96	/* FFUART recieve */
#define GPIO98_FFRTS		98	/* FFUART request to send */
#define GPIO98_CIF_DD_0		98	/* Camera data pin 0 */
#define GPIO99_FFTXD		99	/* FFUART transmit data */
#define GPIO100_FFCTS		100	/* FFUART Clear to send */
#define GPIO102_nPCE_1		102	/* PCMCIA (PXA27x) */
#define GPIO103_CIF_DD_3	103	/* Camera data pin 3 */
#define GPIO104_CIF_DD_2	104	/* Camera data pin 2 */
#define GPIO105_CIF_DD_1	105	/* Camera data pin 1 */
#define GPIO106_CIF_DD_9	106	/* Camera data pin 9 */
#define GPIO107_CIF_DD_8	107	/* Camera data pin 8 */
#define GPIO108_CIF_DD_7	108	/* Camera data pin 7 */
#define GPIO109_MMCDAT1		109	/* MMC DAT1 (PXA27x) */
#define GPIO110_MMCDAT2		110	/* MMC DAT2 (PXA27x) */
#define GPIO110_MMCCS0		110	/* MMC Chip Select 0 (PXA27x) */
#define GPIO111_MMCDAT3		111	/* MMC DAT3 (PXA27x) */
#define GPIO111_MMCCS1		111	/* MMC Chip Select 1 (PXA27x) */
#define GPIO112_MMCCMD		112	/* MMC CMD (PXA27x) */
#define GPIO113_I2S_SYSCLK	113	/* I2S System Clock (PXA27x) */
#define GPIO113_AC97_RESET_N	113	/* AC97 NRESET on (PXA27x) */
#define GPIO114_CIF_DD_1	114	/* Camera data pin 1 */
#define GPIO115_CIF_DD_3	115	/* Camera data pin 3 */
#define GPIO116_CIF_DD_2	116	/* Camera data pin 2 */

/* GPIO alternate function mode & direction */

#define GPIO_IN			0x000
#define GPIO_OUT		0x080
#define GPIO_ALT_FN_1_IN	0x100
#define GPIO_ALT_FN_1_OUT	0x180
#define GPIO_ALT_FN_2_IN	0x200
#define GPIO_ALT_FN_2_OUT	0x280
#define GPIO_ALT_FN_3_IN	0x300
#define GPIO_ALT_FN_3_OUT	0x380
#define GPIO_MD_MASK_NR		0x07f
#define GPIO_MD_MASK_DIR	0x080
#define GPIO_MD_MASK_FN		0x300
#define GPIO_DFLT_LOW		0x400
#define GPIO_DFLT_HIGH		0x800

#define GPIO1_RTS_MD		( 1 | GPIO_ALT_FN_1_IN)
#define GPIO6_MMCCLK_MD		( 6 | GPIO_ALT_FN_1_OUT)
#define GPIO7_48MHz_MD		( 7 | GPIO_ALT_FN_1_OUT)
#define GPIO8_MMCCS0_MD		( 8 | GPIO_ALT_FN_1_OUT)
#define GPIO9_MMCCS1_MD		( 9 | GPIO_ALT_FN_1_OUT)
#define GPIO10_RTCCLK_MD	(10 | GPIO_ALT_FN_1_OUT)
#define GPIO11_3_6MHz_MD	(11 | GPIO_ALT_FN_1_OUT)
#define GPIO12_32KHz_MD		(12 | GPIO_ALT_FN_1_OUT)
#define GPIO12_CIF_DD_7_MD	(12 | GPIO_ALT_FN_2_IN)
#define GPIO13_MBGNT_MD		(13 | GPIO_ALT_FN_2_OUT)
#define GPIO14_MBREQ_MD		(14 | GPIO_ALT_FN_1_IN)
#define GPIO15_nCS_1_MD		(15 | GPIO_ALT_FN_2_OUT)
#define GPIO16_PWM0_MD		(16 | GPIO_ALT_FN_2_OUT)
#define GPIO17_PWM1_MD		(17 | GPIO_ALT_FN_2_OUT)
#define GPIO17_CIF_DD_6_MD	(17 | GPIO_ALT_FN_2_IN)
#define GPIO18_RDY_MD		(18 | GPIO_ALT_FN_1_IN)
#define GPIO19_DREQ1_MD		(19 | GPIO_ALT_FN_1_IN)
#define GPIO20_DREQ0_MD		(20 | GPIO_ALT_FN_1_IN)
#define GPIO23_CIF_MCLK_MD	(23 | GPIO_ALT_FN_1_OUT)
#define GPIO23_SCLK_MD		(23 | GPIO_ALT_FN_2_OUT)
#define GPIO24_CIF_FV_MD	(24 | GPIO_ALT_FN_1_OUT)
#define GPIO24_SFRM_MD		(24 | GPIO_ALT_FN_2_OUT)
#define GPIO25_CIF_LV_MD	(25 | GPIO_ALT_FN_1_OUT)
#define GPIO25_STXD_MD		(25 | GPIO_ALT_FN_2_OUT)
#define GPIO26_SRXD_MD		(26 | GPIO_ALT_FN_1_IN)
#define GPIO26_CIF_PCLK_MD	(26 | GPIO_ALT_FN_2_IN)
#define GPIO27_SEXTCLK_MD	(27 | GPIO_ALT_FN_1_IN)
#define GPIO27_CIF_DD_0_MD	(27 | GPIO_ALT_FN_3_IN)
#define GPIO28_BITCLK_AC97_MD	(28 | GPIO_ALT_FN_1_IN)
#define GPIO28_BITCLK_IN_I2S_MD	(28 | GPIO_ALT_FN_2_IN)
#define GPIO28_BITCLK_OUT_I2S_MD	(28 | GPIO_ALT_FN_1_OUT)
#define GPIO29_SDATA_IN_AC97_MD	(29 | GPIO_ALT_FN_1_IN)
#define GPIO29_SDATA_IN_I2S_MD	(29 | GPIO_ALT_FN_2_IN)
#define GPIO30_SDATA_OUT_AC97_MD	(30 | GPIO_ALT_FN_2_OUT)
#define GPIO30_SDATA_OUT_I2S_MD	(30 | GPIO_ALT_FN_1_OUT)
#define GPIO31_SYNC_I2S_MD	(31 | GPIO_ALT_FN_1_OUT)
#define GPIO31_SYNC_AC97_MD	(31 | GPIO_ALT_FN_2_OUT)
#define GPIO32_SDATA_IN1_AC97_MD	(32 | GPIO_ALT_FN_1_IN)
#define GPIO32_SYSCLK_I2S_MD	(32 | GPIO_ALT_FN_1_OUT)
#define GPIO32_MMCCLK_MD	(32 | GPIO_ALT_FN_2_OUT)
#define GPIO33_nCS_5_MD		(33 | GPIO_ALT_FN_2_OUT)
#define GPIO34_FFRXD_MD		(34 | GPIO_ALT_FN_1_IN)
#define GPIO34_MMCCS0_MD	(34 | GPIO_ALT_FN_2_OUT)
#define GPIO35_FFCTS_MD		(35 | GPIO_ALT_FN_1_IN)
#define GPIO35_KP_MKOUT6_MD	(35 | GPIO_ALT_FN_2_OUT)
#define GPIO36_FFDCD_MD		(36 | GPIO_ALT_FN_1_IN)
#define GPIO37_FFDSR_MD		(37 | GPIO_ALT_FN_1_IN)
#define GPIO38_FFRI_MD		(38 | GPIO_ALT_FN_1_IN)
#define GPIO39_MMCCS1_MD	(39 | GPIO_ALT_FN_1_OUT)
#define GPIO39_FFTXD_MD		(39 | GPIO_ALT_FN_2_OUT)
#define GPIO40_FFDTR_MD		(40 | GPIO_ALT_FN_2_OUT)
#define GPIO41_FFRTS_MD		(41 | GPIO_ALT_FN_2_OUT)
#define GPIO41_KP_MKOUT7_MD	(41 | GPIO_ALT_FN_1_OUT)
#define GPIO42_BTRXD_MD		(42 | GPIO_ALT_FN_1_IN)
#define GPIO42_HWRXD_MD		(42 | GPIO_ALT_FN_3_IN)
#define GPIO42_CIF_MCLK_MD	(42 | GPIO_ALT_FN_3_OUT)
#define GPIO43_BTTXD_MD		(43 | GPIO_ALT_FN_2_OUT)
#define GPIO43_HWTXD_MD		(43 | GPIO_ALT_FN_3_OUT)
#define GPIO43_CIF_FV_MD	(43 | GPIO_ALT_FN_3_OUT)
#define GPIO44_BTCTS_MD		(44 | GPIO_ALT_FN_1_IN)
#define GPIO44_HWCTS_MD		(44 | GPIO_ALT_FN_3_IN)
#define GPIO44_CIF_LV_MD	(44 | GPIO_ALT_FN_3_OUT)
#define GPIO45_CIF_PCLK_MD	(45 | GPIO_ALT_FN_3_IN)
#define GPIO45_BTRTS_MD		(45 | GPIO_ALT_FN_2_OUT)
#define GPIO45_HWRTS_MD		(45 | GPIO_ALT_FN_3_OUT)
#define GPIO45_SYSCLK_AC97_MD	(45 | GPIO_ALT_FN_1_OUT)
#define GPIO46_ICPRXD_MD	(46 | GPIO_ALT_FN_1_IN)
#define GPIO46_STRXD_MD		(46 | GPIO_ALT_FN_2_IN)
#define GPIO47_CIF_DD_0_MD	(47 | GPIO_ALT_FN_1_IN)
#define GPIO47_ICPTXD_MD	(47 | GPIO_ALT_FN_2_OUT)
#define GPIO47_STTXD_MD		(47 | GPIO_ALT_FN_1_OUT)
#define GPIO48_CIF_DD_5_MD	(48 | GPIO_ALT_FN_1_IN)
#define GPIO48_nPOE_MD		(48 | GPIO_ALT_FN_2_OUT)
#define GPIO48_HWTXD_MD		(48 | GPIO_ALT_FN_1_OUT)
#define GPIO48_nPOE_MD		(48 | GPIO_ALT_FN_2_OUT)
#define GPIO49_HWRXD_MD		(49 | GPIO_ALT_FN_1_IN)
#define GPIO49_nPWE_MD		(49 | GPIO_ALT_FN_2_OUT)
#define GPIO50_CIF_DD_3_MD	(50 | GPIO_ALT_FN_1_IN)
#define GPIO50_nPIOR_MD		(50 | GPIO_ALT_FN_2_OUT)
#define GPIO50_HWCTS_MD		(50 | GPIO_ALT_FN_1_IN)
#define GPIO50_CIF_DD_3_MD	(50 | GPIO_ALT_FN_1_IN)
#define GPIO51_CIF_DD_2_MD	(51 | GPIO_ALT_FN_1_IN)
#define GPIO51_nPIOW_MD		(51 | GPIO_ALT_FN_2_OUT)
#define GPIO51_HWRTS_MD		(51 | GPIO_ALT_FN_1_OUT)
#define GPIO51_CIF_DD_2_MD	(51 | GPIO_ALT_FN_1_IN)
#define GPIO52_nPCE_1_MD	(52 | GPIO_ALT_FN_2_OUT)
#define GPIO52_CIF_DD_4_MD	(52 | GPIO_ALT_FN_1_IN)
#define GPIO53_nPCE_2_MD	(53 | GPIO_ALT_FN_2_OUT)
#define GPIO53_MMCCLK_MD	(53 | GPIO_ALT_FN_1_OUT)
#define GPIO53_CIF_MCLK_MD	(53 | GPIO_ALT_FN_2_OUT)
#define GPIO54_MMCCLK_MD	(54 | GPIO_ALT_FN_1_OUT)
#define GPIO54_nPCE_2_MD	(54 | GPIO_ALT_FN_2_OUT)
#define GPIO54_pSKTSEL_MD	(54 | GPIO_ALT_FN_2_OUT)
#define GPIO54_CIF_PCLK_MD	(54 | GPIO_ALT_FN_3_IN)
#define GPIO55_nPREG_MD		(55 | GPIO_ALT_FN_2_OUT)
#define GPIO55_CIF_DD_1_MD	(55 | GPIO_ALT_FN_1_IN)
#define GPIO56_nPWAIT_MD	(56 | GPIO_ALT_FN_1_IN)
#define GPIO57_nIOIS16_MD	(57 | GPIO_ALT_FN_1_IN)
#define GPIO58_LDD_0_MD		(58 | GPIO_ALT_FN_2_OUT)
#define GPIO59_LDD_1_MD		(59 | GPIO_ALT_FN_2_OUT)
#define GPIO60_LDD_2_MD		(60 | GPIO_ALT_FN_2_OUT)
#define GPIO61_LDD_3_MD		(61 | GPIO_ALT_FN_2_OUT)
#define GPIO62_LDD_4_MD		(62 | GPIO_ALT_FN_2_OUT)
#define GPIO63_LDD_5_MD		(63 | GPIO_ALT_FN_2_OUT)
#define GPIO64_LDD_6_MD		(64 | GPIO_ALT_FN_2_OUT)
#define GPIO65_LDD_7_MD		(65 | GPIO_ALT_FN_2_OUT)
#define GPIO66_LDD_8_MD		(66 | GPIO_ALT_FN_2_OUT)
#define GPIO66_MBREQ_MD		(66 | GPIO_ALT_FN_1_IN)
#define GPIO67_LDD_9_MD		(67 | GPIO_ALT_FN_2_OUT)
#define GPIO67_MMCCS0_MD	(67 | GPIO_ALT_FN_1_OUT)
#define GPIO68_LDD_10_MD	(68 | GPIO_ALT_FN_2_OUT)
#define GPIO68_MMCCS1_MD	(68 | GPIO_ALT_FN_1_OUT)
#define GPIO69_LDD_11_MD	(69 | GPIO_ALT_FN_2_OUT)
#define GPIO69_MMCCLK_MD	(69 | GPIO_ALT_FN_1_OUT)
#define GPIO70_LDD_12_MD	(70 | GPIO_ALT_FN_2_OUT)
#define GPIO70_RTCCLK_MD	(70 | GPIO_ALT_FN_1_OUT)
#define GPIO71_LDD_13_MD	(71 | GPIO_ALT_FN_2_OUT)
#define GPIO71_3_6MHz_MD	(71 | GPIO_ALT_FN_1_OUT)
#define GPIO72_LDD_14_MD	(72 | GPIO_ALT_FN_2_OUT)
#define GPIO72_32kHz_MD		(72 | GPIO_ALT_FN_1_OUT)
#define GPIO73_LDD_15_MD	(73 | GPIO_ALT_FN_2_OUT)
#define GPIO73_MBGNT_MD		(73 | GPIO_ALT_FN_1_OUT)
#define GPIO74_LCD_FCLK_MD	(74 | GPIO_ALT_FN_2_OUT)
#define GPIO75_LCD_LCLK_MD	(75 | GPIO_ALT_FN_2_OUT)
#define GPIO76_LCD_PCLK_MD	(76 | GPIO_ALT_FN_2_OUT)
#define GPIO77_LCD_ACBIAS_MD	(77 | GPIO_ALT_FN_2_OUT)
#define GPIO78_nCS_2_MD		(78 | GPIO_ALT_FN_2_OUT)
#define GPIO78_nPCE_2_MD	(78 | GPIO_ALT_FN_1_OUT)
#define GPIO79_nCS_3_MD		(79 | GPIO_ALT_FN_2_OUT)
#define GPIO79_pSKTSEL_MD	(79 | GPIO_ALT_FN_1_OUT)
#define GPIO80_nCS_4_MD		(80 | GPIO_ALT_FN_2_OUT)
#define GPIO81_NSSP_CLK_OUT	(81 | GPIO_ALT_FN_1_OUT)
#define GPIO81_NSSP_CLK_IN	(81 | GPIO_ALT_FN_1_IN)
#define GPIO81_CIF_DD_0_MD	(81 | GPIO_ALT_FN_2_IN)
#define GPIO82_NSSP_FRM_OUT	(82 | GPIO_ALT_FN_1_OUT)
#define GPIO82_NSSP_FRM_IN	(82 | GPIO_ALT_FN_1_IN)
#define GPIO82_CIF_DD_5_MD	(82 | GPIO_ALT_FN_3_IN)
#define GPIO83_NSSP_TX		(83 | GPIO_ALT_FN_1_OUT)
#define GPIO83_NSSP_RX		(83 | GPIO_ALT_FN_2_IN)
#define GPIO83_CIF_DD_4_MD	(83 | GPIO_ALT_FN_3_IN)
#define GPIO84_NSSP_TX		(84 | GPIO_ALT_FN_1_OUT)
#define GPIO84_NSSP_RX		(84 | GPIO_ALT_FN_2_IN)
#define GPIO84_CIF_FV_MD	(84 | GPIO_ALT_FN_3_IN)
#define GPIO85_nPCE_1_MD	(85 | GPIO_ALT_FN_1_OUT)
#define GPIO85_CIF_LV_MD	(85 | GPIO_ALT_FN_3_IN)
#define GPIO86_nPCE_1_MD	(86 | GPIO_ALT_FN_1_OUT)
#define GPIO88_USBH1_PWR_MD	(88 | GPIO_ALT_FN_1_IN)
#define GPIO89_USBH1_PEN_MD	(89 | GPIO_ALT_FN_2_OUT)
#define GPIO90_CIF_DD_4_MD	(90 | GPIO_ALT_FN_3_IN)
#define GPIO91_CIF_DD_5_MD	(91 | GPIO_ALT_FN_3_IN)
#define GPIO92_MMCDAT0_MD	(92 | GPIO_ALT_FN_1_OUT)
#define GPIO93_CIF_DD_6_MD	(93 | GPIO_ALT_FN_2_IN)
#define GPIO94_CIF_DD_5_MD	(94 | GPIO_ALT_FN_2_IN)
#define GPIO95_CIF_DD_4_MD	(95 | GPIO_ALT_FN_2_IN)
#define GPIO95_KP_MKIN6_MD	(95 | GPIO_ALT_FN_3_IN)
#define GPIO96_KP_DKIN3_MD	(96 | GPIO_ALT_FN_1_IN)
#define GPIO96_FFRXD_MD		(96 | GPIO_ALT_FN_3_IN)
#define GPIO97_KP_MKIN3_MD	(97 | GPIO_ALT_FN_3_IN)
#define GPIO98_CIF_DD_0_MD	(98 | GPIO_ALT_FN_2_IN)
#define GPIO98_FFRTS_MD		(98 | GPIO_ALT_FN_3_OUT)
#define GPIO99_FFTXD_MD		(99 | GPIO_ALT_FN_3_OUT)
#define GPIO100_KP_MKIN0_MD	(100 | GPIO_ALT_FN_1_IN)
#define GPIO101_KP_MKIN1_MD	(101 | GPIO_ALT_FN_1_IN)
#define GPIO102_nPCE_1_MD	(102 | GPIO_ALT_FN_1_OUT)
#define GPIO102_KP_MKIN2_MD	(102 | GPIO_ALT_FN_1_IN)
#define GPIO103_CIF_DD_3_MD	(103 | GPIO_ALT_FN_1_IN)
#define GPIO103_KP_MKOUT0_MD	(103 | GPIO_ALT_FN_2_OUT)
#define GPIO104_CIF_DD_2_MD	(104 | GPIO_ALT_FN_1_IN)
#define GPIO104_pSKTSEL_MD	(104 | GPIO_ALT_FN_1_OUT)
#define GPIO104_KP_MKOUT1_MD	(104 | GPIO_ALT_FN_2_OUT)
#define GPIO105_CIF_DD_1_MD	(105 | GPIO_ALT_FN_1_IN)
#define GPIO105_KP_MKOUT2_MD	(105 | GPIO_ALT_FN_2_OUT)
#define GPIO106_CIF_DD_9_MD	(106 | GPIO_ALT_FN_1_IN)
#define GPIO106_KP_MKOUT3_MD	(106 | GPIO_ALT_FN_2_OUT)
#define GPIO107_CIF_DD_8_MD	(107 | GPIO_ALT_FN_1_IN)
#define GPIO107_KP_MKOUT4_MD	(107 | GPIO_ALT_FN_2_OUT)
#define GPIO108_CIF_DD_7_MD	(108 | GPIO_ALT_FN_1_IN)
#define GPIO108_KP_MKOUT5_MD	(108 | GPIO_ALT_FN_2_OUT)
#define GPIO109_MMCDAT1_MD	(109 | GPIO_ALT_FN_1_OUT)
#define GPIO110_MMCDAT2_MD	(110 | GPIO_ALT_FN_1_OUT)
#define GPIO110_MMCCS0_MD	(110 | GPIO_ALT_FN_1_OUT)
#define GPIO111_MMCDAT3_MD	(111 | GPIO_ALT_FN_1_OUT)
#define GPIO110_MMCCS1_MD	(111 | GPIO_ALT_FN_1_OUT)
#define GPIO112_MMCCMD_MD	(112 | GPIO_ALT_FN_1_OUT)
#define GPIO113_I2S_SYSCLK_MD	(113 | GPIO_ALT_FN_1_OUT)
#define GPIO113_AC97_RESET_N_MD	(113 | GPIO_ALT_FN_2_OUT)
#define GPIO117_I2CSCL_MD	(117 | GPIO_ALT_FN_1_IN)
#define GPIO118_I2CSDA_MD	(118 | GPIO_ALT_FN_1_IN)

/*
 * Handy routine to set GPIO alternate functions
 */
extern int pxa_gpio_mode( int gpio_mode );

#endif /* __ASM_ARCH_PXA2XX_GPIO_H */

/*
 * Miscellaneous IOCTL commands for Dynamic Power Management Controller Driver
 *
 * Copyright (C) 2004-2009 Analog Device Inc.
 *
 * Licensed under the GPL-2
 */

#ifndef _BLACKFIN_DPMC_H_
#define _BLACKFIN_DPMC_H_

/* PLL_CTL Masks */
#define DF			0x0001	/* 0: PLL = CLKIN, 1: PLL = CLKIN/2 */
#define PLL_OFF			0x0002	/* PLL Not Powered */
#define STOPCK			0x0008	/* Core Clock Off */
#define PDWN			0x0020	/* Enter Deep Sleep Mode */
#ifdef __ADSPBF539__
# define IN_DELAY		0x0014	/* Add 200ps Delay To EBIU Input Latches */
# define OUT_DELAY		0x00C0	/* Add 200ps Delay To EBIU Output Signals */
#else
# define IN_DELAY		0x0040	/* Add 200ps Delay To EBIU Input Latches */
# define OUT_DELAY		0x0080	/* Add 200ps Delay To EBIU Output Signals */
#endif
#define BYPASS			0x0100	/* Bypass the PLL */
#define MSEL			0x7E00	/* Multiplier Select For CCLK/VCO Factors */
#define SPORT_HYST		0x8000	/* Enable Additional Hysteresis on SPORT Input Pins */
#define SET_MSEL(x)		(((x)&0x3F) << 0x9)	/* Set MSEL = 0-63 --> VCO = CLKIN*MSEL */

/* PLL_DIV Masks */
#define SSEL			0x000F	/* System Select */
#define CSEL			0x0030	/* Core Select */
#define CSEL_DIV1		0x0000	/* CCLK = VCO / 1 */
#define CSEL_DIV2		0x0010	/* CCLK = VCO / 2 */
#define CSEL_DIV4		0x0020	/* CCLK = VCO / 4 */
#define CSEL_DIV8		0x0030	/* CCLK = VCO / 8 */

#define CCLK_DIV1 CSEL_DIV1
#define CCLK_DIV2 CSEL_DIV2
#define CCLK_DIV4 CSEL_DIV4
#define CCLK_DIV8 CSEL_DIV8

#define SET_SSEL(x)	((x) & 0xF)	/* Set SSEL = 0-15 --> SCLK = VCO/SSEL */
#define SCLK_DIV(x)	(x)		/* SCLK = VCO / x */

/* PLL_STAT Masks */
#define ACTIVE_PLLENABLED	0x0001	/* Processor In Active Mode With PLL Enabled */
#define FULL_ON			0x0002	/* Processor In Full On Mode */
#define ACTIVE_PLLDISABLED	0x0004	/* Processor In Active Mode With PLL Disabled */
#define PLL_LOCKED		0x0020	/* PLL_LOCKCNT Has Been Reached */

#define RTCWS			0x0400	/* RTC/Reset Wake-Up Status */
#define CANWS			0x0800	/* CAN Wake-Up Status */
#define USBWS			0x2000	/* USB Wake-Up Status */
#define KPADWS			0x4000	/* Keypad Wake-Up Status */
#define ROTWS			0x8000	/* Rotary Wake-Up Status */
#define GPWS			0x1000	/* General-Purpose Wake-Up Status */

/* VR_CTL Masks */
#if defined(__ADSPBF52x__) || defined(__ADSPBF51x__)
#define FREQ			0x3000	/* Switching Oscillator Frequency For Regulator */
#define FREQ_1000		0x3000	/* Switching Frequency Is 1 MHz */
#else
#define FREQ			0x0003	/* Switching Oscillator Frequency For Regulator */
#define FREQ_333		0x0001	/* Switching Frequency Is 333 kHz */
#define FREQ_667		0x0002	/* Switching Frequency Is 667 kHz */
#define FREQ_1000		0x0003	/* Switching Frequency Is 1 MHz */
#endif
#define HIBERNATE		0x0000	/* Powerdown/Bypass On-Board Regulation */

#define GAIN			0x000C	/* Voltage Level Gain */
#define GAIN_5			0x0000	/* GAIN = 5 */
#define GAIN_10			0x0004	/* GAIN = 1 */
#define GAIN_20			0x0008	/* GAIN = 2 */
#define GAIN_50			0x000C	/* GAIN = 5 */

#define VLEV			0x00F0	/* Internal Voltage Level */
#ifdef __ADSPBF52x__
#define VLEV_085		0x0040	/* VLEV = 0.85 V (-5% - +10% Accuracy) */
#define VLEV_090		0x0050	/* VLEV = 0.90 V (-5% - +10% Accuracy) */
#define VLEV_095		0x0060	/* VLEV = 0.95 V (-5% - +10% Accuracy) */
#define VLEV_100		0x0070	/* VLEV = 1.00 V (-5% - +10% Accuracy) */
#define VLEV_105		0x0080	/* VLEV = 1.05 V (-5% - +10% Accuracy) */
#define VLEV_110		0x0090	/* VLEV = 1.10 V (-5% - +10% Accuracy) */
#define VLEV_115		0x00A0	/* VLEV = 1.15 V (-5% - +10% Accuracy) */
#define VLEV_120		0x00B0	/* VLEV = 1.20 V (-5% - +10% Accuracy) */
#else
#define VLEV_085		0x0060	/* VLEV = 0.85 V (-5% - +10% Accuracy) */
#define VLEV_090		0x0070	/* VLEV = 0.90 V (-5% - +10% Accuracy) */
#define VLEV_095		0x0080	/* VLEV = 0.95 V (-5% - +10% Accuracy) */
#define VLEV_100		0x0090	/* VLEV = 1.00 V (-5% - +10% Accuracy) */
#define VLEV_105		0x00A0	/* VLEV = 1.05 V (-5% - +10% Accuracy) */
#define VLEV_110		0x00B0	/* VLEV = 1.10 V (-5% - +10% Accuracy) */
#define VLEV_115		0x00C0	/* VLEV = 1.15 V (-5% - +10% Accuracy) */
#define VLEV_120		0x00D0	/* VLEV = 1.20 V (-5% - +10% Accuracy) */
#define VLEV_125		0x00E0	/* VLEV = 1.25 V (-5% - +10% Accuracy) */
#define VLEV_130		0x00F0	/* VLEV = 1.30 V (-5% - +10% Accuracy) */
#endif

#define WAKE			0x0100	/* Enable RTC/Reset Wakeup From Hibernate */
#define CANWE			0x0200	/* Enable CAN Wakeup From Hibernate */
#define PHYWE			0x0400	/* Enable PHY Wakeup From Hibernate */
#define GPWE			0x0400	/* General-Purpose Wake-Up Enable */
#define MXVRWE			0x0400	/* Enable MXVR Wakeup From Hibernate */
#define KPADWE			0x1000	/* Keypad Wake-Up Enable */
#define ROTWE			0x2000	/* Rotary Wake-Up Enable */
#define CLKBUFOE		0x4000	/* CLKIN Buffer Output Enable */
#define SCKELOW			0x8000	/* Do Not Drive SCKE High During Reset After Hibernate */

#if defined(__ADSPBF52x__) || defined(__ADSPBF51x__)
#define USBWE			0x0200	/* Enable USB Wakeup From Hibernate */
#else
#define USBWE			0x0800	/* Enable USB Wakeup From Hibernate */
#endif

#ifndef __ASSEMBLY__

void sleep_mode(u32 sic_iwr0, u32 sic_iwr1, u32 sic_iwr2);
void hibernate_mode(u32 sic_iwr0, u32 sic_iwr1, u32 sic_iwr2);
void sleep_deeper(u32 sic_iwr0, u32 sic_iwr1, u32 sic_iwr2);
void do_hibernate(int wakeup);
void set_dram_srfs(void);
void unset_dram_srfs(void);

#define VRPAIR(vlev, freq) (((vlev) << 16) | ((freq) >> 16))

struct bfin_dpmc_platform_data {
	const unsigned int *tuple_tab;
	unsigned short tabsize;
	unsigned short vr_settling_time; /* in us */
};

#else

#define PM_PUSH(x) \
	R0 = [P0 + (x - SRAM_BASE_ADDRESS)];\
	[--SP] =  R0;\

#define PM_POP(x) \
	R0 = [SP++];\
	[P0 + (x - SRAM_BASE_ADDRESS)] = R0;\

#define PM_SYS_PUSH(x) \
	R0 = [P0 + (x - PLL_CTL)];\
	[--SP] =  R0;\

#define PM_SYS_POP(x) \
	R0 = [SP++];\
	[P0 + (x - PLL_CTL)] = R0;\

#define PM_SYS_PUSH16(x) \
	R0 = w[P0 + (x - PLL_CTL)];\
	[--SP] =  R0;\

#define PM_SYS_POP16(x) \
	R0 = [SP++];\
	w[P0 + (x - PLL_CTL)] = R0;\

#endif

#endif	/*_BLACKFIN_DPMC_H_*/

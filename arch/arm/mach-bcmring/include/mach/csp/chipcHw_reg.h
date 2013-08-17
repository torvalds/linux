/*****************************************************************************
* Copyright 2004 - 2008 Broadcom Corporation.  All rights reserved.
*
* Unless you and Broadcom execute a separate written software license
* agreement governing use of this software, this software is licensed to you
* under the terms of the GNU General Public License version 2, available at
* http://www.broadcom.com/licenses/GPLv2.php (the "GPL").
*
* Notwithstanding the above, under no circumstances may you combine this
* software in any way with any other Broadcom software provided under a
* license other than the GPL, without Broadcom's express prior written
* consent.
*****************************************************************************/

/****************************************************************************/
/**
*  @file    chipcHw_reg.h
*
*  @brief   Definitions for low level chip control registers
*
*/
/****************************************************************************/
#ifndef CHIPCHW_REG_H
#define CHIPCHW_REG_H

#include <mach/csp/mm_io.h>
#include <csp/reg.h>
#include <mach/csp/ddrcReg.h>

#define chipcHw_BASE_ADDRESS    MM_IO_BASE_CHIPC

typedef struct {
	uint32_t ChipId;	/* Chip ID */
	uint32_t DDRClock;	/* PLL1 Channel 1 for DDR clock */
	uint32_t ARMClock;	/* PLL1 Channel 2 for ARM clock */
	uint32_t ESWClock;	/* PLL1 Channel 3 for ESW system clock */
	uint32_t VPMClock;	/* PLL1 Channel 4 for VPM clock */
	uint32_t ESW125Clock;	/* PLL1 Channel 5 for ESW 125MHz clock */
	uint32_t UARTClock;	/* PLL1 Channel 6 for UART clock */
	uint32_t SDIO0Clock;	/* PLL1 Channel 7 for SDIO 0 clock */
	uint32_t SDIO1Clock;	/* PLL1 Channel 8 for SDIO 1 clock */
	uint32_t SPIClock;	/* PLL1 Channel 9 for SPI master Clock  */
	uint32_t ETMClock;	/* PLL1 Channel 10 for ARM ETM Clock  */

	uint32_t ACLKClock;	/* ACLK Clock (Divider) */
	uint32_t OTPClock;	/* OTP Clock  (Divider) */
	uint32_t I2CClock;	/* I2C Clock (CK_13m) (Divider) */
	uint32_t I2S0Clock;	/* I2S0 Clock (Divider) */
	uint32_t RTBUSClock;	/* RTBUS (DDR PHY Config.) Clock (Divider) */
	uint32_t pad1;
	uint32_t APM100Clock;	/* APM 100MHz CLK Clock (Divider) */
	uint32_t TSCClock;	/* TSC Clock (Divider) */
	uint32_t LEDClock;	/* LED Clock (Divider) */

	uint32_t USBClock;	/* PLL2 Channel 1 for USB clock */
	uint32_t LCDClock;	/* PLL2 Channel 2 for LCD clock */
	uint32_t APMClock;	/* PLL2 Channel 3 for APM 200 MHz clock */

	uint32_t BusIntfClock;	/* Bus interface clock */

	uint32_t PLLStatus;	/* PLL status register (PLL1) */
	uint32_t PLLConfig;	/* PLL configuration register  (PLL1) */
	uint32_t PLLPreDivider;	/* PLL pre-divider control register (PLL1) */
	uint32_t PLLDivider;	/* PLL divider control register (PLL1) */
	uint32_t PLLControl1;	/* PLL analog control register #1 (PLL1) */
	uint32_t PLLControl2;	/* PLL analog control register #2 (PLL1) */

	uint32_t I2S1Clock;	/* I2S1 Clock  */
	uint32_t AudioEnable;	/* Enable/ disable audio channel */
	uint32_t SoftReset1;	/* Reset blocks */
	uint32_t SoftReset2;	/* Reset blocks */
	uint32_t Spare1;	/* Phase align interrupts */
	uint32_t Sticky;	/* Sticky bits */
	uint32_t MiscCtrl;	/* Misc. control */
	uint32_t pad3[3];

	uint32_t PLLStatus2;	/* PLL status register (PLL2) */
	uint32_t PLLConfig2;	/* PLL configuration register  (PLL2) */
	uint32_t PLLPreDivider2;	/* PLL pre-divider control register (PLL2) */
	uint32_t PLLDivider2;	/* PLL divider control register (PLL2) */
	uint32_t PLLControl12;	/* PLL analog control register #1 (PLL2) */
	uint32_t PLLControl22;	/* PLL analog control register #2 (PLL2) */

	uint32_t DDRPhaseCtrl1;	/* DDR Clock Phase Alignment control1 */
	uint32_t VPMPhaseCtrl1;	/* VPM Clock Phase Alignment control1 */
	uint32_t PhaseAlignStatus;	/* DDR/VPM Clock Phase Alignment Status */
	uint32_t PhaseCtrlStatus;	/* DDR/VPM Clock HW DDR/VPM ph_ctrl and load_ch Status */
	uint32_t DDRPhaseCtrl2;	/* DDR Clock Phase Alignment control2 */
	uint32_t VPMPhaseCtrl2;	/* VPM Clock Phase Alignment control2 */
	uint32_t pad4[9];

	uint32_t SoftOTP1;	/* Software OTP control */
	uint32_t SoftOTP2;	/* Software OTP control */
	uint32_t SoftStraps;	/* Software strap */
	uint32_t PinStraps;	/* Pin Straps */
	uint32_t DiffOscCtrl;	/* Diff oscillator control */
	uint32_t DiagsCtrl;	/* Diagnostic control */
	uint32_t DiagsOutputCtrl;	/* Diagnostic output enable */
	uint32_t DiagsReadBackCtrl;	/* Diagnostic read back control */

	uint32_t LcdPifMode;	/* LCD/PIF Pin Sharing MUX Mode */

	uint32_t GpioMux_0_7;	/* Pin Sharing MUX0 Control */
	uint32_t GpioMux_8_15;	/* Pin Sharing MUX1 Control */
	uint32_t GpioMux_16_23;	/* Pin Sharing MUX2 Control */
	uint32_t GpioMux_24_31;	/* Pin Sharing MUX3 Control */
	uint32_t GpioMux_32_39;	/* Pin Sharing MUX4 Control */
	uint32_t GpioMux_40_47;	/* Pin Sharing MUX5 Control */
	uint32_t GpioMux_48_55;	/* Pin Sharing MUX6 Control */
	uint32_t GpioMux_56_63;	/* Pin Sharing MUX7 Control */

	uint32_t GpioSR_0_7;	/* Slew rate for GPIO 0 - 7 */
	uint32_t GpioSR_8_15;	/* Slew rate for GPIO 8 - 15 */
	uint32_t GpioSR_16_23;	/* Slew rate for GPIO 16 - 23 */
	uint32_t GpioSR_24_31;	/* Slew rate for GPIO 24 - 31 */
	uint32_t GpioSR_32_39;	/* Slew rate for GPIO 32 - 39 */
	uint32_t GpioSR_40_47;	/* Slew rate for GPIO 40 - 47 */
	uint32_t GpioSR_48_55;	/* Slew rate for GPIO 48 - 55 */
	uint32_t GpioSR_56_63;	/* Slew rate for GPIO 56 - 63 */
	uint32_t MiscSR_0_7;	/* Slew rate for MISC 0 - 7 */
	uint32_t MiscSR_8_15;	/* Slew rate for MISC 8 - 15 */

	uint32_t GpioPull_0_15;	/* Pull up registers for GPIO 0 - 15 */
	uint32_t GpioPull_16_31;	/* Pull up registers for GPIO 16 - 31 */
	uint32_t GpioPull_32_47;	/* Pull up registers for GPIO 32 - 47 */
	uint32_t GpioPull_48_63;	/* Pull up registers for GPIO 48 - 63 */
	uint32_t MiscPull_0_15;	/* Pull up registers for MISC 0 - 15 */

	uint32_t GpioInput_0_31;	/* Input type for GPIO 0 - 31 */
	uint32_t GpioInput_32_63;	/* Input type for GPIO 32 - 63 */
	uint32_t MiscInput_0_15;	/* Input type for MISC 0 - 16 */
} chipcHw_REG_t;

#define pChipcHw  ((volatile chipcHw_REG_t *) chipcHw_BASE_ADDRESS)
#define pChipcPhysical  ((volatile chipcHw_REG_t *) MM_ADDR_IO_CHIPC)

#define chipcHw_REG_CHIPID_BASE_MASK                    0xFFFFF000
#define chipcHw_REG_CHIPID_BASE_SHIFT                   12
#define chipcHw_REG_CHIPID_REV_MASK                     0x00000FFF
#define chipcHw_REG_REV_A0                              0xA00
#define chipcHw_REG_REV_B0                              0x0B0

#define chipcHw_REG_PLL_STATUS_CONTROL_ENABLE           0x80000000	/* Allow controlling PLL registers */
#define chipcHw_REG_PLL_STATUS_LOCKED                   0x00000001	/* PLL is settled */
#define chipcHw_REG_PLL_CONFIG_D_RESET                  0x00000008	/* Digital reset */
#define chipcHw_REG_PLL_CONFIG_A_RESET                  0x00000004	/* Analog reset */
#define chipcHw_REG_PLL_CONFIG_BYPASS_ENABLE            0x00000020	/* Bypass enable */
#define chipcHw_REG_PLL_CONFIG_OUTPUT_ENABLE            0x00000010	/* Output enable */
#define chipcHw_REG_PLL_CONFIG_POWER_DOWN               0x00000001	/* Power down */
#define chipcHw_REG_PLL_CONFIG_VCO_SPLIT_FREQ           1600000000	/* 1.6GHz VCO split frequency */
#define chipcHw_REG_PLL_CONFIG_VCO_800_1600             0x00000000	/* VCO range 800-1600 MHz */
#define chipcHw_REG_PLL_CONFIG_VCO_1601_3200            0x00000080	/* VCO range 1601-3200 MHz */
#define chipcHw_REG_PLL_CONFIG_TEST_ENABLE              0x00010000	/* PLL test output enable */
#define chipcHw_REG_PLL_CONFIG_TEST_SELECT_MASK         0x003E0000	/* Mask to set test values */
#define chipcHw_REG_PLL_CONFIG_TEST_SELECT_SHIFT        17

#define chipcHw_REG_PLL_CLOCK_PHASE_COMP                0x00800000	/* Phase comparator output */
#define chipcHw_REG_PLL_CLOCK_TO_BUS_RATIO_MASK         0x00300000	/* Clock to bus ratio mask */
#define chipcHw_REG_PLL_CLOCK_TO_BUS_RATIO_SHIFT        20	/* Number of bits to be shifted */
#define chipcHw_REG_PLL_CLOCK_POWER_DOWN                0x00080000	/* PLL channel power down */
#define chipcHw_REG_PLL_CLOCK_SOURCE_GPIO               0x00040000	/* Use GPIO as source */
#define chipcHw_REG_PLL_CLOCK_BYPASS_SELECT             0x00020000	/* Select bypass clock */
#define chipcHw_REG_PLL_CLOCK_OUTPUT_ENABLE             0x00010000	/* Clock gated ON */
#define chipcHw_REG_PLL_CLOCK_PHASE_UPDATE_ENABLE       0x00008000	/* Clock phase update enable */
#define chipcHw_REG_PLL_CLOCK_PHASE_CONTROL_SHIFT       8	/* Number of bits to be shifted */
#define chipcHw_REG_PLL_CLOCK_PHASE_CONTROL_MASK        0x00003F00	/* Phase control mask */
#define chipcHw_REG_PLL_CLOCK_MDIV_MASK                 0x000000FF	/* Clock post divider mask

									   00000000 = divide-by-256
									   00000001 = divide-by-1
									   00000010 = divide-by-2
									   00000011 = divide-by-3
									   00000100 = divide-by-4
									   00000101 = divide-by-5
									   00000110 = divide-by-6
									   .
									   .
									   11111011 = divide-by-251
									   11111100 = divide-by-252
									   11111101 = divide-by-253
									   11111110 = divide-by-254
									 */

#define chipcHw_REG_DIV_CLOCK_SOURCE_OTHER              0x00040000	/* NON-PLL clock source select */
#define chipcHw_REG_DIV_CLOCK_BYPASS_SELECT             0x00020000	/* NON-PLL clock bypass enable */
#define chipcHw_REG_DIV_CLOCK_OUTPUT_ENABLE             0x00010000	/* NON-PLL clock output enable */
#define chipcHw_REG_DIV_CLOCK_DIV_MASK                  0x000000FF	/* NON-PLL clock post-divide mask */
#define chipcHw_REG_DIV_CLOCK_DIV_256                   0x00000000	/* NON-PLL clock post-divide by 256 */

#define chipcHw_REG_PLL_PREDIVIDER_P1_SHIFT             0
#define chipcHw_REG_PLL_PREDIVIDER_P2_SHIFT             4
#define chipcHw_REG_PLL_PREDIVIDER_NDIV_SHIFT           8
#define chipcHw_REG_PLL_PREDIVIDER_NDIV_MASK            0x0001FF00
#define chipcHw_REG_PLL_PREDIVIDER_POWER_DOWN           0x02000000
#define chipcHw_REG_PLL_PREDIVIDER_NDIV_MODE_MASK       0x00700000	/* Divider mask */
#define chipcHw_REG_PLL_PREDIVIDER_NDIV_MODE_INTEGER    0x00000000	/* Integer-N Mode */
#define chipcHw_REG_PLL_PREDIVIDER_NDIV_MODE_MASH_UNIT  0x00100000	/* MASH Sigma-Delta Modulator Unit Mode */
#define chipcHw_REG_PLL_PREDIVIDER_NDIV_MODE_MFB_UNIT   0x00200000	/* MFB Sigma-Delta Modulator Unit Mode */
#define chipcHw_REG_PLL_PREDIVIDER_NDIV_MODE_MASH_1_8   0x00300000	/* MASH Sigma-Delta Modulator 1/8 Mode */
#define chipcHw_REG_PLL_PREDIVIDER_NDIV_MODE_MFB_1_8    0x00400000	/* MFB Sigma-Delta Modulator 1/8 Mode */

#define chipcHw_REG_PLL_PREDIVIDER_NDIV_i(vco)          ((vco) / chipcHw_XTAL_FREQ_Hz)
#define chipcHw_REG_PLL_PREDIVIDER_P1                   1
#define chipcHw_REG_PLL_PREDIVIDER_P2                   1

#define chipcHw_REG_PLL_DIVIDER_M1DIV                   0x03000000
#define chipcHw_REG_PLL_DIVIDER_FRAC                    0x00FFFFFF	/* Fractional divider */

#define chipcHw_REG_PLL_DIVIDER_NDIV_f_SS               (0x00FFFFFF)	/* To attain spread with max frequency */

#define chipcHw_REG_PLL_DIVIDER_NDIV_f                  0	/* ndiv_frac = chipcHw_REG_PLL_DIVIDER_NDIV_f /
								   chipcHw_REG_PLL_DIVIDER_FRAC
								   = 0, when SS is disable
								 */

#define chipcHw_REG_PLL_DIVIDER_MDIV(vco, Hz)           ((chipcHw_divide((vco), (Hz)) > 255) ? 0 : chipcHw_divide((vco), (Hz)))

#define chipcHw_REG_ACLKClock_CLK_DIV_MASK              0x3

/* System booting strap options */
#define chipcHw_STRAPS_SOFT_OVERRIDE                    0x00000001	/* Software Strap Override */

#define chipcHw_STRAPS_BOOT_DEVICE_NAND_FLASH_8         0x00000000	/* 8 bit NAND FLASH Boot */
#define chipcHw_STRAPS_BOOT_DEVICE_NOR_FLASH_16         0x00000002	/* 16 bit NOR FLASH Boot */
#define chipcHw_STRAPS_BOOT_DEVICE_SERIAL_FLASH         0x00000004	/* Serial FLASH Boot */
#define chipcHw_STRAPS_BOOT_DEVICE_NAND_FLASH_16        0x00000006	/* 16 bit NAND FLASH Boot */
#define chipcHw_STRAPS_BOOT_DEVICE_UART                 0x00000008	/* UART Boot */
#define chipcHw_STRAPS_BOOT_DEVICE_MASK                 0x0000000E	/* Mask */

/* System boot option */
#define chipcHw_STRAPS_BOOT_OPTION_BROM                 0x00000000	/* Boot from Boot ROM */
#define chipcHw_STRAPS_BOOT_OPTION_ARAM                 0x00000020	/* Boot from ARAM */
#define chipcHw_STRAPS_BOOT_OPTION_NOR                  0x00000030	/* Boot from NOR flash */

/* NAND Flash page size strap options */
#define chipcHw_STRAPS_NAND_PAGESIZE_512                0x00000000	/* NAND FLASH page size of 512 bytes */
#define chipcHw_STRAPS_NAND_PAGESIZE_2048               0x00000040	/* NAND FLASH page size of 2048 bytes */
#define chipcHw_STRAPS_NAND_PAGESIZE_4096               0x00000080	/* NAND FLASH page size of 4096 bytes */
#define chipcHw_STRAPS_NAND_PAGESIZE_EXT                0x000000C0	/* NAND FLASH page of extened size */
#define chipcHw_STRAPS_NAND_PAGESIZE_MASK               0x000000C0	/* Mask */

#define chipcHw_STRAPS_NAND_EXTRA_CYCLE                 0x00000400	/* NAND FLASH address cycle configuration */
#define chipcHw_STRAPS_REBOOT_TO_UART                   0x00000800	/* Reboot to UART on error */

/* Secure boot mode strap options */
#define chipcHw_STRAPS_BOOT_MODE_NORMAL                 0x00000000	/* Normal Boot */
#define chipcHw_STRAPS_BOOT_MODE_DBG_SW                 0x00000100	/* Software debugging Boot */
#define chipcHw_STRAPS_BOOT_MODE_DBG_BOOT               0x00000200	/* Boot rom debugging Boot */
#define chipcHw_STRAPS_BOOT_MODE_NORMAL_QUIET           0x00000300	/* Normal Boot (Quiet BootRom) */
#define chipcHw_STRAPS_BOOT_MODE_MASK                   0x00000300	/* Mask */

/* Slave Mode straps */
#define chipcHw_STRAPS_I2CS                             0x02000000	/* I2C Slave  */
#define chipcHw_STRAPS_SPIS                             0x01000000	/* SPI Slave  */

/* Strap pin options */
#define chipcHw_REG_SW_STRAPS                           ((pChipcHw->PinStraps & 0x0000FC00) >> 10)

/* PIF/LCD pin sharing defines */
#define chipcHw_REG_LCD_PIN_ENABLE                      0x00000001	/* LCD Controller is used and the pins have LCD functions */
#define chipcHw_REG_PIF_PIN_ENABLE                      0x00000002	/* LCD pins are used to perform PIF functions  */

#define chipcHw_GPIO_COUNT                              61	/* Number of GPIO pin accessible thorugh CHIPC */

/* NOTE: Any changes to these constants will require a corresponding change to chipcHw_str.c */
#define chipcHw_REG_GPIO_MUX_KEYPAD                     0x00000001	/* GPIO mux for Keypad */
#define chipcHw_REG_GPIO_MUX_I2CH                       0x00000002	/* GPIO mux for I2CH */
#define chipcHw_REG_GPIO_MUX_SPI                        0x00000003	/* GPIO mux for SPI */
#define chipcHw_REG_GPIO_MUX_UART                       0x00000004	/* GPIO mux for UART */
#define chipcHw_REG_GPIO_MUX_LEDMTXP                    0x00000005	/* GPIO mux for LEDMTXP */
#define chipcHw_REG_GPIO_MUX_LEDMTXS                    0x00000006	/* GPIO mux for LEDMTXS */
#define chipcHw_REG_GPIO_MUX_SDIO0                      0x00000007	/* GPIO mux for SDIO0 */
#define chipcHw_REG_GPIO_MUX_SDIO1                      0x00000008	/* GPIO mux for SDIO1 */
#define chipcHw_REG_GPIO_MUX_PCM                        0x00000009	/* GPIO mux for PCM */
#define chipcHw_REG_GPIO_MUX_I2S                        0x0000000A	/* GPIO mux for I2S */
#define chipcHw_REG_GPIO_MUX_ETM                        0x0000000B	/* GPIO mux for ETM */
#define chipcHw_REG_GPIO_MUX_DEBUG                      0x0000000C	/* GPIO mux for DEBUG */
#define chipcHw_REG_GPIO_MUX_MISC                       0x0000000D	/* GPIO mux for MISC */
#define chipcHw_REG_GPIO_MUX_GPIO                       0x00000000	/* GPIO mux for GPIO */
#define chipcHw_REG_GPIO_MUX(pin)                       (&pChipcHw->GpioMux_0_7 + ((pin) >> 3))
#define chipcHw_REG_GPIO_MUX_POSITION(pin)              (((pin) & 0x00000007) << 2)
#define chipcHw_REG_GPIO_MUX_MASK                       0x0000000F	/* Mask */

#define chipcHw_REG_SLEW_RATE_HIGH                      0x00000000	/* High speed slew rate */
#define chipcHw_REG_SLEW_RATE_NORMAL                    0x00000008	/* Normal slew rate */
							/* Pins beyond 42 are defined by skipping 8 bits within the register */
#define chipcHw_REG_SLEW_RATE(pin)                      (((pin) > 42) ? (&pChipcHw->GpioSR_0_7 + (((pin) + 2) >> 3)) : (&pChipcHw->GpioSR_0_7 + ((pin) >> 3)))
#define chipcHw_REG_SLEW_RATE_POSITION(pin)             (((pin) > 42) ? ((((pin) + 2) & 0x00000007) << 2) : (((pin) & 0x00000007) << 2))
#define chipcHw_REG_SLEW_RATE_MASK                      0x00000008	/* Mask */

#define chipcHw_REG_CURRENT_STRENGTH_2mA                0x00000001	/* Current driving strength 2 milli ampere */
#define chipcHw_REG_CURRENT_STRENGTH_4mA                0x00000002	/* Current driving strength 4 milli ampere */
#define chipcHw_REG_CURRENT_STRENGTH_6mA                0x00000004	/* Current driving strength 6 milli ampere */
#define chipcHw_REG_CURRENT_STRENGTH_8mA                0x00000005	/* Current driving strength 8 milli ampere */
#define chipcHw_REG_CURRENT_STRENGTH_10mA               0x00000006	/* Current driving strength 10 milli ampere */
#define chipcHw_REG_CURRENT_STRENGTH_12mA               0x00000007	/* Current driving strength 12 milli ampere */
#define chipcHw_REG_CURRENT_MASK                        0x00000007	/* Mask */
							/* Pins beyond 42 are defined by skipping 8 bits */
#define chipcHw_REG_CURRENT(pin)                        (((pin) > 42) ? (&pChipcHw->GpioSR_0_7 + (((pin) + 2) >> 3)) : (&pChipcHw->GpioSR_0_7 + ((pin) >> 3)))
#define chipcHw_REG_CURRENT_POSITION(pin)               (((pin) > 42) ? ((((pin) + 2) & 0x00000007) << 2) : (((pin) & 0x00000007) << 2))

#define chipcHw_REG_PULL_NONE                           0x00000000	/* No pull up register */
#define chipcHw_REG_PULL_UP                             0x00000001	/* Pull up register enable */
#define chipcHw_REG_PULL_DOWN                           0x00000002	/* Pull down register enable */
#define chipcHw_REG_PULLUP_MASK                         0x00000003	/* Mask */
							/* Pins beyond 42 are defined by skipping 4 bits */
#define chipcHw_REG_PULLUP(pin)                         (((pin) > 42) ? (&pChipcHw->GpioPull_0_15 + (((pin) + 2) >> 4)) : (&pChipcHw->GpioPull_0_15 + ((pin) >> 4)))
#define chipcHw_REG_PULLUP_POSITION(pin)                (((pin) > 42) ? ((((pin) + 2) & 0x0000000F) << 1) : (((pin) & 0x0000000F) << 1))

#define chipcHw_REG_INPUTTYPE_CMOS                      0x00000000	/* Normal CMOS logic */
#define chipcHw_REG_INPUTTYPE_ST                        0x00000001	/* High speed Schmitt Trigger */
#define chipcHw_REG_INPUTTYPE_MASK                      0x00000001	/* Mask */
							/* Pins beyond 42 are defined by skipping 2 bits */
#define chipcHw_REG_INPUTTYPE(pin)                      (((pin) > 42) ? (&pChipcHw->GpioInput_0_31 + (((pin) + 2) >> 5)) : (&pChipcHw->GpioInput_0_31 + ((pin) >> 5)))
#define chipcHw_REG_INPUTTYPE_POSITION(pin)             (((pin) > 42) ? ((((pin) + 2) & 0x0000001F)) : (((pin) & 0x0000001F)))

/* Device connected to the bus clock */
#define chipcHw_REG_BUS_CLOCK_ARM                       0x00000001	/* Bus interface clock for ARM */
#define chipcHw_REG_BUS_CLOCK_VDEC                      0x00000002	/* Bus interface clock for VDEC */
#define chipcHw_REG_BUS_CLOCK_ARAM                      0x00000004	/* Bus interface clock for ARAM */
#define chipcHw_REG_BUS_CLOCK_HPM                       0x00000008	/* Bus interface clock for HPM */
#define chipcHw_REG_BUS_CLOCK_DDRC                      0x00000010	/* Bus interface clock for DDRC */
#define chipcHw_REG_BUS_CLOCK_DMAC0                     0x00000020	/* Bus interface clock for DMAC0 */
#define chipcHw_REG_BUS_CLOCK_DMAC1                     0x00000040	/* Bus interface clock for DMAC1 */
#define chipcHw_REG_BUS_CLOCK_NVI                       0x00000080	/* Bus interface clock for NVI */
#define chipcHw_REG_BUS_CLOCK_ESW                       0x00000100	/* Bus interface clock for ESW */
#define chipcHw_REG_BUS_CLOCK_GE                        0x00000200	/* Bus interface clock for GE */
#define chipcHw_REG_BUS_CLOCK_I2CH                      0x00000400	/* Bus interface clock for I2CH */
#define chipcHw_REG_BUS_CLOCK_I2S0                      0x00000800	/* Bus interface clock for I2S0 */
#define chipcHw_REG_BUS_CLOCK_I2S1                      0x00001000	/* Bus interface clock for I2S1 */
#define chipcHw_REG_BUS_CLOCK_VRAM                      0x00002000	/* Bus interface clock for VRAM */
#define chipcHw_REG_BUS_CLOCK_CLCD                      0x00004000	/* Bus interface clock for CLCD */
#define chipcHw_REG_BUS_CLOCK_LDK                       0x00008000	/* Bus interface clock for LDK */
#define chipcHw_REG_BUS_CLOCK_LED                       0x00010000	/* Bus interface clock for LED */
#define chipcHw_REG_BUS_CLOCK_OTP                       0x00020000	/* Bus interface clock for OTP */
#define chipcHw_REG_BUS_CLOCK_PIF                       0x00040000	/* Bus interface clock for PIF */
#define chipcHw_REG_BUS_CLOCK_SPU                       0x00080000	/* Bus interface clock for SPU */
#define chipcHw_REG_BUS_CLOCK_SDIO0                     0x00100000	/* Bus interface clock for SDIO0 */
#define chipcHw_REG_BUS_CLOCK_SDIO1                     0x00200000	/* Bus interface clock for SDIO1 */
#define chipcHw_REG_BUS_CLOCK_SPIH                      0x00400000	/* Bus interface clock for SPIH */
#define chipcHw_REG_BUS_CLOCK_SPIS                      0x00800000	/* Bus interface clock for SPIS */
#define chipcHw_REG_BUS_CLOCK_UART0                     0x01000000	/* Bus interface clock for UART0 */
#define chipcHw_REG_BUS_CLOCK_UART1                     0x02000000	/* Bus interface clock for UART1 */
#define chipcHw_REG_BUS_CLOCK_BBL                       0x04000000	/* Bus interface clock for BBL */
#define chipcHw_REG_BUS_CLOCK_I2CS                      0x08000000	/* Bus interface clock for I2CS */
#define chipcHw_REG_BUS_CLOCK_USBH                      0x10000000	/* Bus interface clock for USB Host */
#define chipcHw_REG_BUS_CLOCK_USBD                      0x20000000	/* Bus interface clock for USB Device */
#define chipcHw_REG_BUS_CLOCK_BROM                      0x40000000	/* Bus interface clock for Boot ROM */
#define chipcHw_REG_BUS_CLOCK_TSC                       0x80000000	/* Bus interface clock for Touch screen */

/* Software resets defines */
#define chipcHw_REG_SOFT_RESET_VPM_GLOBAL_HOLD          0x0000000080000000ULL	/* Reset Global VPM and hold */
#define chipcHw_REG_SOFT_RESET_VPM_HOLD                 0x0000000040000000ULL	/* Reset VPM and hold */
#define chipcHw_REG_SOFT_RESET_VPM_GLOBAL               0x0000000020000000ULL	/* Reset Global VPM */
#define chipcHw_REG_SOFT_RESET_VPM                      0x0000000010000000ULL	/* Reset VPM */
#define chipcHw_REG_SOFT_RESET_KEYPAD                   0x0000000008000000ULL	/* Reset Key pad */
#define chipcHw_REG_SOFT_RESET_LED                      0x0000000004000000ULL	/* Reset LED */
#define chipcHw_REG_SOFT_RESET_SPU                      0x0000000002000000ULL	/* Reset SPU */
#define chipcHw_REG_SOFT_RESET_RNG                      0x0000000001000000ULL	/* Reset RNG */
#define chipcHw_REG_SOFT_RESET_PKA                      0x0000000000800000ULL	/* Reset PKA */
#define chipcHw_REG_SOFT_RESET_LCD                      0x0000000000400000ULL	/* Reset LCD */
#define chipcHw_REG_SOFT_RESET_PIF                      0x0000000000200000ULL	/* Reset PIF */
#define chipcHw_REG_SOFT_RESET_I2CS                     0x0000000000100000ULL	/* Reset I2C Slave */
#define chipcHw_REG_SOFT_RESET_I2CH                     0x0000000000080000ULL	/* Reset I2C Host */
#define chipcHw_REG_SOFT_RESET_SDIO1                    0x0000000000040000ULL	/* Reset SDIO 1 */
#define chipcHw_REG_SOFT_RESET_SDIO0                    0x0000000000020000ULL	/* Reset SDIO 0 */
#define chipcHw_REG_SOFT_RESET_BBL                      0x0000000000010000ULL	/* Reset BBL */
#define chipcHw_REG_SOFT_RESET_I2S1                     0x0000000000008000ULL	/* Reset I2S1 */
#define chipcHw_REG_SOFT_RESET_I2S0                     0x0000000000004000ULL	/* Reset I2S0 */
#define chipcHw_REG_SOFT_RESET_SPIS                     0x0000000000002000ULL	/* Reset SPI Slave */
#define chipcHw_REG_SOFT_RESET_SPIH                     0x0000000000001000ULL	/* Reset SPI Host */
#define chipcHw_REG_SOFT_RESET_GPIO1                    0x0000000000000800ULL	/* Reset GPIO block 1 */
#define chipcHw_REG_SOFT_RESET_GPIO0                    0x0000000000000400ULL	/* Reset GPIO block 0 */
#define chipcHw_REG_SOFT_RESET_UART1                    0x0000000000000200ULL	/* Reset UART 1 */
#define chipcHw_REG_SOFT_RESET_UART0                    0x0000000000000100ULL	/* Reset UART 0 */
#define chipcHw_REG_SOFT_RESET_NVI                      0x0000000000000080ULL	/* Reset NVI */
#define chipcHw_REG_SOFT_RESET_WDOG                     0x0000000000000040ULL	/* Reset Watch dog */
#define chipcHw_REG_SOFT_RESET_TMR                      0x0000000000000020ULL	/* Reset Timer */
#define chipcHw_REG_SOFT_RESET_ETM                      0x0000000000000010ULL	/* Reset ETM */
#define chipcHw_REG_SOFT_RESET_ARM_HOLD                 0x0000000000000008ULL	/* Reset ARM and HOLD */
#define chipcHw_REG_SOFT_RESET_ARM                      0x0000000000000004ULL	/* Reset ARM */
#define chipcHw_REG_SOFT_RESET_CHIP_WARM                0x0000000000000002ULL	/* Chip warm reset */
#define chipcHw_REG_SOFT_RESET_CHIP_SOFT                0x0000000000000001ULL	/* Chip soft reset */
#define chipcHw_REG_SOFT_RESET_VDEC                     0x0000100000000000ULL	/* Video decoder */
#define chipcHw_REG_SOFT_RESET_GE                       0x0000080000000000ULL	/* Graphics engine */
#define chipcHw_REG_SOFT_RESET_OTP                      0x0000040000000000ULL	/* Reset OTP */
#define chipcHw_REG_SOFT_RESET_USB2                     0x0000020000000000ULL	/* Reset USB2 */
#define chipcHw_REG_SOFT_RESET_USB1                     0x0000010000000000ULL	/* Reset USB 1 */
#define chipcHw_REG_SOFT_RESET_USB                      0x0000008000000000ULL	/* Reset USB 1 and USB2 soft reset */
#define chipcHw_REG_SOFT_RESET_ESW                      0x0000004000000000ULL	/* Reset Ethernet switch */
#define chipcHw_REG_SOFT_RESET_ESWCLK                   0x0000002000000000ULL	/* Reset Ethernet switch clock */
#define chipcHw_REG_SOFT_RESET_DDRPHY                   0x0000001000000000ULL	/* Reset DDR Physical */
#define chipcHw_REG_SOFT_RESET_DDR                      0x0000000800000000ULL	/* Reset DDR Controller */
#define chipcHw_REG_SOFT_RESET_TSC                      0x0000000400000000ULL	/* Reset Touch screen */
#define chipcHw_REG_SOFT_RESET_PCM                      0x0000000200000000ULL	/* Reset PCM device */
#define chipcHw_REG_SOFT_RESET_APM                      0x0000200100000000ULL	/* Reset APM device */

#define chipcHw_REG_SOFT_RESET_VPM_GLOBAL_UNHOLD        0x8000000000000000ULL	/* Unhold Global VPM */
#define chipcHw_REG_SOFT_RESET_VPM_UNHOLD               0x4000000000000000ULL	/* Unhold VPM */
#define chipcHw_REG_SOFT_RESET_ARM_UNHOLD               0x2000000000000000ULL	/* Unhold ARM reset  */
#define chipcHw_REG_SOFT_RESET_UNHOLD_MASK              0xF000000000000000ULL	/* Mask to handle unhold request */

/* Audio channel control defines */
#define chipcHw_REG_AUDIO_CHANNEL_ENABLE_ALL            0x00000001	/* Enable all audio channel */
#define chipcHw_REG_AUDIO_CHANNEL_ENABLE_A              0x00000002	/* Enable channel A */
#define chipcHw_REG_AUDIO_CHANNEL_ENABLE_B              0x00000004	/* Enable channel B */
#define chipcHw_REG_AUDIO_CHANNEL_ENABLE_C              0x00000008	/* Enable channel C */
#define chipcHw_REG_AUDIO_CHANNEL_ENABLE_NTP_CLOCK      0x00000010	/* Enable NTP clock */
#define chipcHw_REG_AUDIO_CHANNEL_ENABLE_PCM0_CLOCK     0x00000020	/* Enable PCM0 clock */
#define chipcHw_REG_AUDIO_CHANNEL_ENABLE_PCM1_CLOCK     0x00000040	/* Enable PCM1 clock */
#define chipcHw_REG_AUDIO_CHANNEL_ENABLE_APM_CLOCK      0x00000080	/* Enable APM clock */

/* Misc. chip control defines */
#define chipcHw_REG_MISC_CTRL_GE_SEL                    0x00040000	/* Select GE2/GE3 */
#define chipcHw_REG_MISC_CTRL_I2S1_CLOCK_ONCHIP         0x00000000	/* Use on chip clock for I2S1 */
#define chipcHw_REG_MISC_CTRL_I2S1_CLOCK_GPIO           0x00020000	/* Use external clock via GPIO pin 26 for I2S1 */
#define chipcHw_REG_MISC_CTRL_I2S0_CLOCK_ONCHIP         0x00000000	/* Use on chip clock for I2S0 */
#define chipcHw_REG_MISC_CTRL_I2S0_CLOCK_GPIO           0x00010000	/* Use external clock via GPIO pin 45 for I2S0 */
#define chipcHw_REG_MISC_CTRL_ARM_CP15_DISABLE          0x00008000	/* Disable ARM CP15 bit */
#define chipcHw_REG_MISC_CTRL_RTC_DISABLE               0x00000008	/* Disable RTC registers */
#define chipcHw_REG_MISC_CTRL_BBRAM_DISABLE             0x00000004	/* Disable Battery Backed RAM */
#define chipcHw_REG_MISC_CTRL_USB_MODE_HOST             0x00000002	/* Set USB as host */
#define chipcHw_REG_MISC_CTRL_USB_MODE_DEVICE           0xFFFFFFFD	/* Set USB as device */
#define chipcHw_REG_MISC_CTRL_USB_POWERON               0xFFFFFFFE	/* Power up USB */
#define chipcHw_REG_MISC_CTRL_USB_POWEROFF              0x00000001	/* Power down USB */

/* OTP configuration defines */
#define chipcHw_REG_OTP_SECURITY_OFF                    0x0000020000000000ULL	/* Security support is OFF */
#define chipcHw_REG_OTP_SPU_SLOW                        0x0000010000000000ULL	/* Limited SPU throughput */
#define chipcHw_REG_OTP_LCD_SPEED                       0x0000000600000000ULL	/* Set VPM speed one */
#define chipcHw_REG_OTP_VPM_SPEED_1                     0x0000000100000000ULL	/* Set VPM speed one */
#define chipcHw_REG_OTP_VPM_SPEED_0                     0x0000000080000000ULL	/* Set VPM speed zero */
#define chipcHw_REG_OTP_AXI_SPEED                       0x0000000060000000ULL	/* Set maximum AXI bus speed */
#define chipcHw_REG_OTP_APM_DISABLE                     0x000000001F000000ULL	/* Disable APM */
#define chipcHw_REG_OTP_PIF_DISABLE                     0x0000000000200000ULL	/* Disable PIF */
#define chipcHw_REG_OTP_VDEC_DISABLE                    0x0000000000100000ULL	/* Disable Video decoder */
#define chipcHw_REG_OTP_BBL_DISABLE                     0x0000000000080000ULL	/* Disable RTC and BBRAM */
#define chipcHw_REG_OTP_LED_DISABLE                     0x0000000000040000ULL	/* Disable LED */
#define chipcHw_REG_OTP_GE_DISABLE                      0x0000000000020000ULL	/* Disable Graphics Engine */
#define chipcHw_REG_OTP_LCD_DISABLE                     0x0000000000010000ULL	/* Disable LCD */
#define chipcHw_REG_OTP_KEYPAD_DISABLE                  0x0000000000008000ULL	/* Disable keypad */
#define chipcHw_REG_OTP_UART_DISABLE                    0x0000000000004000ULL	/* Disable UART */
#define chipcHw_REG_OTP_SDIOH_DISABLE                   0x0000000000003000ULL	/* Disable SDIO host */
#define chipcHw_REG_OTP_HSS_DISABLE                     0x0000000000000C00ULL	/* Disable HSS */
#define chipcHw_REG_OTP_TSC_DISABLE                     0x0000000000000200ULL	/* Disable touch screen */
#define chipcHw_REG_OTP_USB_DISABLE                     0x0000000000000180ULL	/* Disable USB */
#define chipcHw_REG_OTP_SGMII_DISABLE                   0x0000000000000060ULL	/* Disable SGMII */
#define chipcHw_REG_OTP_ETH_DISABLE                     0x0000000000000018ULL	/* Disable gigabit ethernet */
#define chipcHw_REG_OTP_ETH_PHY_DISABLE                 0x0000000000000006ULL	/* Disable ethernet PHY */
#define chipcHw_REG_OTP_VPM_DISABLE                     0x0000000000000001ULL	/* Disable VPM */

/* Sticky bit defines */
#define chipcHw_REG_STICKY_BOOT_DONE                    0x00000001	/* Boot done */
#define chipcHw_REG_STICKY_SOFT_RESET                   0x00000002	/* ARM soft reset */
#define chipcHw_REG_STICKY_GENERAL_1                    0x00000004	/* General purpose bit 1 */
#define chipcHw_REG_STICKY_GENERAL_2                    0x00000008	/* General purpose bit 2 */
#define chipcHw_REG_STICKY_GENERAL_3                    0x00000010	/* General purpose bit 3 */
#define chipcHw_REG_STICKY_GENERAL_4                    0x00000020	/* General purpose bit 4 */
#define chipcHw_REG_STICKY_GENERAL_5                    0x00000040	/* General purpose bit 5 */
#define chipcHw_REG_STICKY_POR_BROM                     0x00000080	/* Special sticky bit for security - set in BROM to avoid other modes being entered */
#define chipcHw_REG_STICKY_ARM_RESET                    0x00000100	/* ARM reset */
#define chipcHw_REG_STICKY_CHIP_SOFT_RESET              0x00000200	/* Chip soft reset */
#define chipcHw_REG_STICKY_CHIP_WARM_RESET              0x00000400	/* Chip warm reset */
#define chipcHw_REG_STICKY_WDOG_RESET                   0x00000800	/* Watchdog reset */
#define chipcHw_REG_STICKY_OTP_RESET                    0x00001000	/* OTP reset */

							/* HW phase alignment defines *//* Spare1 register definitions */
#define chipcHw_REG_SPARE1_DDR_PHASE_INTR_ENABLE        0x80000000	/* Enable DDR phase align panic interrupt */
#define chipcHw_REG_SPARE1_VPM_PHASE_INTR_ENABLE        0x40000000	/* Enable VPM phase align panic interrupt */
#define chipcHw_REG_SPARE1_VPM_BUS_ACCESS_ENABLE        0x00000002	/* Enable access to VPM using system BUS */
#define chipcHw_REG_SPARE1_DDR_BUS_ACCESS_ENABLE        0x00000001	/* Enable access to DDR using system BUS */
							/* DDRPhaseCtrl1 register definitions */
#define chipcHw_REG_DDR_SW_PHASE_CTRL_ENABLE            0x80000000	/* Enable DDR SW phase alignment */
#define chipcHw_REG_DDR_HW_PHASE_CTRL_ENABLE            0x40000000	/* Enable DDR HW phase alignment */
#define chipcHw_REG_DDR_PHASE_VALUE_GE_MASK             0x0000007F	/* DDR lower threshold for phase alignment */
#define chipcHw_REG_DDR_PHASE_VALUE_GE_SHIFT            23
#define chipcHw_REG_DDR_PHASE_VALUE_LE_MASK             0x0000007F	/* DDR upper threshold for phase alignment */
#define chipcHw_REG_DDR_PHASE_VALUE_LE_SHIFT            16
#define chipcHw_REG_DDR_PHASE_ALIGN_WAIT_CYCLE_MASK     0x0000FFFF	/* BUS Cycle to wait to run next DDR phase alignment */
#define chipcHw_REG_DDR_PHASE_ALIGN_WAIT_CYCLE_SHIFT    0
							/* VPMPhaseCtrl1 register definitions */
#define chipcHw_REG_VPM_SW_PHASE_CTRL_ENABLE            0x80000000	/* Enable VPM SW phase alignment */
#define chipcHw_REG_VPM_HW_PHASE_CTRL_ENABLE            0x40000000	/* Enable VPM HW phase alignment */
#define chipcHw_REG_VPM_PHASE_VALUE_GE_MASK             0x0000007F	/* VPM lower threshold for phase alignment */
#define chipcHw_REG_VPM_PHASE_VALUE_GE_SHIFT            23
#define chipcHw_REG_VPM_PHASE_VALUE_LE_MASK             0x0000007F	/* VPM upper threshold for phase alignment */
#define chipcHw_REG_VPM_PHASE_VALUE_LE_SHIFT            16
#define chipcHw_REG_VPM_PHASE_ALIGN_WAIT_CYCLE_MASK     0x0000FFFF	/* BUS Cycle to wait to complete the VPM phase alignment */
#define chipcHw_REG_VPM_PHASE_ALIGN_WAIT_CYCLE_SHIFT    0
							/* PhaseAlignStatus register definitions */
#define chipcHw_REG_DDR_TIMEOUT_INTR_STATUS             0x80000000	/* DDR time out interrupt status */
#define chipcHw_REG_DDR_PHASE_STATUS_MASK               0x0000007F	/* DDR phase status value */
#define chipcHw_REG_DDR_PHASE_STATUS_SHIFT              24
#define chipcHw_REG_DDR_PHASE_ALIGNED                   0x00800000	/* DDR Phase aligned status */
#define chipcHw_REG_DDR_LOAD                            0x00400000	/* Load DDR phase status */
#define chipcHw_REG_DDR_PHASE_CTRL_MASK                 0x0000003F	/* DDR phase control value */
#define chipcHw_REG_DDR_PHASE_CTRL_SHIFT                16
#define chipcHw_REG_VPM_TIMEOUT_INTR_STATUS             0x80000000	/* VPM time out interrupt status */
#define chipcHw_REG_VPM_PHASE_STATUS_MASK               0x0000007F	/* VPM phase status value */
#define chipcHw_REG_VPM_PHASE_STATUS_SHIFT              8
#define chipcHw_REG_VPM_PHASE_ALIGNED                   0x00000080	/* VPM Phase aligned status */
#define chipcHw_REG_VPM_LOAD                            0x00000040	/* Load VPM phase status */
#define chipcHw_REG_VPM_PHASE_CTRL_MASK                 0x0000003F	/* VPM phase control value */
#define chipcHw_REG_VPM_PHASE_CTRL_SHIFT                0
							/* DDRPhaseCtrl2 register definitions */
#define chipcHw_REG_DDR_INTR_SERVICED                   0x02000000	/* Acknowledge that interrupt was serviced */
#define chipcHw_REG_DDR_TIMEOUT_INTR_ENABLE             0x01000000	/* Enable time out interrupt */
#define chipcHw_REG_DDR_LOAD_COUNT_PHASE_CTRL_MASK      0x0000000F	/* Wait before toggling load_ch */
#define chipcHw_REG_DDR_LOAD_COUNT_PHASE_CTRL_SHIFT     20
#define chipcHw_REG_DDR_TOTAL_LOAD_COUNT_CTRL_MASK      0x0000000F	/* Total wait to settle ph_ctrl and load_ch */
#define chipcHw_REG_DDR_TOTAL_LOAD_COUNT_CTRL_SHIFT     16
#define chipcHw_REG_DDR_PHASE_TIMEOUT_COUNT_MASK        0x0000FFFF	/* Time out value for DDR HW phase alignment */
#define chipcHw_REG_DDR_PHASE_TIMEOUT_COUNT_SHIFT       0
							/* VPMPhaseCtrl2 register definitions */
#define chipcHw_REG_VPM_INTR_SELECT_MASK                0x00000003	/* Interrupt select */
#define chipcHw_REG_VPM_INTR_SELECT_SHIFT               26
#define chipcHw_REG_VPM_INTR_DISABLE                    0x00000000
#define chipcHw_REG_VPM_INTR_FAST                       (0x1 << chipcHw_REG_VPM_INTR_SELECT_SHIFT)
#define chipcHw_REG_VPM_INTR_MEDIUM                     (0x2 << chipcHw_REG_VPM_INTR_SELECT_SHIFT)
#define chipcHw_REG_VPM_INTR_SLOW                       (0x3 << chipcHw_REG_VPM_INTR_SELECT_SHIFT)
#define chipcHw_REG_VPM_INTR_SERVICED                   0x02000000	/* Acknowledge that interrupt was serviced */
#define chipcHw_REG_VPM_TIMEOUT_INTR_ENABLE             0x01000000	/* Enable time out interrupt */
#define chipcHw_REG_VPM_LOAD_COUNT_PHASE_CTRL_MASK      0x0000000F	/* Wait before toggling load_ch */
#define chipcHw_REG_VPM_LOAD_COUNT_PHASE_CTRL_SHIFT     20
#define chipcHw_REG_VPM_TOTAL_LOAD_COUNT_CTRL_MASK      0x0000000F	/* Total wait cycle to settle ph_ctrl and load_ch */
#define chipcHw_REG_VPM_TOTAL_LOAD_COUNT_CTRL_SHIFT     16
#define chipcHw_REG_VPM_PHASE_TIMEOUT_COUNT_MASK        0x0000FFFF	/* Time out value for VPM HW phase alignment */
#define chipcHw_REG_VPM_PHASE_TIMEOUT_COUNT_SHIFT       0

#endif /* CHIPCHW_REG_H */

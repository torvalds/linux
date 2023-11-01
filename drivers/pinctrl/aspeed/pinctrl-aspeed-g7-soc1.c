// SPDX-License-Identifier: GPL-2.0

#include <linux/bits.h>
#include <linux/device.h>
#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/platform_device.h>
#include <linux/property.h>

#define SCU00 0x00 /* Multi-function Pin Control #1 */
#define SCU04 0x04 /* Multi-function Pin Control #2 */
#define SCU08 0x08 /* Multi-function Pin Control #3 */
#define SCU0C 0x0C /* Multi-function Pin Control #4 */
#define SCU10 0x10 /* Multi-function Pin Control #5 */
#define SCU14 0x14 /* Multi-function Pin Control #6 */
#define SCU18 0x18 /* Multi-function Pin Control #7 */
#define SCU1C 0x1C /* Multi-function Pin Control #8 */
#define SCU20 0x20 /* Multi-function Pin Control #9 */
#define SCU24 0x24 /* Multi-function Pin Control #10 */
#define SCU28 0x28 /* Multi-function Pin Control #11 */
#define SCU2C 0x2C /* Multi-function Pin Control #12 */
#define SCU30 0x30 /* Multi-function Pin Control #13 */
#define SCU34 0x34 /* Multi-function Pin Control #14 */
#define SCU38 0x38 /* Multi-function Pin Control #15 */
#define SCU3C 0x3C /* Multi-function Pin Control #16 */
#define SCU40 0x40 /* Multi-function Pin Control #17 */
#define SCU44 0x44 /* Multi-function Pin Control #18 */
#define SCU48 0x48 /* Multi-function Pin Control #19 */
#define SCU4C 0x4C /* Multi-function Pin Control #20 */
#define SCU50 0x50 /* Multi-function Pin Control #21 */
#define SCU54 0x54 /* Multi-function Pin Control #22 */
#define SCU58 0x58 /* Multi-function Pin Control #23 */
#define SCU5C 0x5C /* Multi-function Pin Control #24 */
#define SCU60 0x60 /* Multi-function Pin Control #25 */
#define SCU64 0x64 /* Multi-function Pin Control #26 */
#define SCU68 0x68 /* Multi-function Pin Control #27 */
#define SCU6C 0x6C /* Multi-function Pin Control #28 */
#define SCU70 0x70 /* Multi-function Pin Control #29 */
#define SCU74 0x74 /* Multi-function Pin Control #30 */
#define SCU78 0x78 /* Multi-function Pin Control #31 */

struct aspeed_g7_soc1_pinctrl {
	struct pinctrl_dev *pctldev;
	struct device *dev;
	struct irq_domain *domain;
	void __iomem *regs;
};

static const int espi1_pins[] = { 0, 1, 2, 3, 4, 5, 6, 7 };
static const int lpc1_pins[] = { 0, 1, 2, 3, 4, 5, 6, 7 };
static const int vpi_pins[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
				12, 13, 14, 15, 16, 17, 18, 19, 20,
				21, 22, 23, 24, 25, 26, 27, 28, 29 };
static const int sd_pins[] = { 0, 1, 2, 3, 4, 5, 6, 7 };
static const int tach0_pins[] = { 8 };
static const int tach1_pins[] = { 9 };
static const int tach2_pins[] = { 10 };
static const int tach3_pins[] = { 11 };
static const int thru0_pins[] = { 8, 9 };
static const int thru1_pins[] = { 10, 11 };
static const int tach4_pins[] = { 12 };
static const int tach5_pins[] = { 13 };
static const int tach6_pins[] = { 14 };
static const int tach7_pins[] = { 15 };
static const int ntcs5_pins[] = { 12 };
static const int ndcd5_pins[] = { 13 };
static const int ndsr5_pins[] = { 14 };
static const int nri5_pins[] = { 15 };
static const int tach8_pins[] = { 16 };
static const int tach9_pins[] = { 17 };
static const int tach10_pins[] = { 18 };
static const int tach11_pins[] = { 19 };
static const int tach12_pins[] = { 20 };
static const int tach13_pins[] = { 21 };
static const int tach14_pins[] = { 22 };
static const int tach15_pins[] = { 23 };

static const int ndtr5_pins[] = { 16 };
static const int nrts5_pins[] = { 17 };
static const int ncts6_pins[] = { 18 };
static const int ndcd6_pins[] = { 19 };
static const int ndsr6_pins[] = { 20 };
static const int nri6_pins[] = { 21 };
static const int ndtr6_pins[] = { 22 };
static const int nrts6_pins[] = { 23 };
static const int salt12_pins[] = { 18 };
static const int salt13_pins[] = { 19 };
static const int salt14_pins[] = { 20 };
static const int salt15_pins[] = { 21 };
static const int spim0_pins[] = { 23, 24, 25, 26, 27, 28, 29, 30};

//gpiod
static const int pwm0_pins[] = { 24 };
static const int pwm1_pins[] = { 25 };
static const int pwm2_pins[] = { 26 };
static const int pwm3_pins[] = { 27 };
static const int pwm4_pins[] = { 28 };
static const int pwm5_pins[] = { 29 };
static const int pwm6_pins[] = { 30 };
static const int pwm7_pins[] = { 31 };

static const int siopbon0_pins[] = { 24 };
static const int siopbin0_pins[] = { 25 };
static const int sioscin0_pins[] = { 26 };
static const int sios3n0_pins[] = { 27 };
static const int sios5n0_pins[] = { 28 };
static const int siopwreqn0_pins[] = { 29 };
static const int sioonctrln0_pins[] = { 30 };
static const int siopwrgd0_pins[] = { 31 };

//gpioe
static const int ncts0_pins[] = { 32 };
static const int ndcd0_pins[] = { 33 };
static const int ndsr0_pins[] = { 34 };
static const int nri0_pins[] = { 35 };
static const int ndtr0_pins[] = { 36 };
static const int nrts0_pins[] = { 37 };
static const int uart0_pins[] = { 38, 39 };

//GPIOF
static const int ncts1_pins[] = { 40 };
static const int ndcd1_pins[] = { 41 };
static const int ndsr1_pins[] = { 42 };
static const int nri1_pins[] = { 43 };
static const int ndtr1_pins[] = { 44 };
static const int nrts1_pins[] = { 45 };
static const int uart1_pins[] = { 46, 47 };
//GPIOG
static const int uart2_pins[] = { 48, 49 };
static const int pwm8_pins[] = { 50 };
static const int pwm9_pins[] = { 50 };
static const int pwm10_pins[] = { 51 };
static const int pwm11_pins[] = { 52 };
static const int pwm12_pins[] = { 53 };
static const int pwm13_pins[] = { 54 };
static const int pwm14_pins[] = { 55 };
static const int pwm15_pins[] = { 56 };
static const int salt0_pins[] = { 54 };
static const int salt1_pins[] = { 55 };
static const int salt2_pins[] = { 56 };
static const int salt3_pins[] = { 57 };
static const int uart3_pins[] = { 50, 51 };
static const int spim1_pins[] = { 50, 51, 52, 53, 54, 55, 56, 57 };
static const int uart5_pins[] = { 52, 53 };
static const int uart6_pins[] = { 54, 55 };
static const int wdtrst0n_pins[] = { 50 };
static const int wdtrst1n_pins[] = { 51 };
static const int wdtrst2n_pins[] = { 51 };
static const int wdtrst3n_pins[] = { 51 };
static const int wdtrst4n_pins[] = { 209 };
static const int wdtrst5n_pins[] = { 210 };
static const int wdtrst6n_pins[] = { 211 };
static const int wdtrst7n_pins[] = { 58 };

//GPIOH
static const int uart7_pins[] = { 56, 57 };

static const int smon0_pins[] = { 204, 205, 207, 208 };
static const int smon1_pins[] = { 58, 59, 209, 210 };

//GPIOI
static const int hvi3c0_pins[] = { 60, 61 };
static const int hvi3c1_pins[] = { 62, 63 };
static const int hvi3c2_pins[] = { 64, 65 };
static const int hvi3c3_pins[] = { 66, 67 };

static const int hvi3c4_pins[] = { 0, 1 };
static const int hvi3c5_pins[] = { 2, 3 };
static const int hvi3c6_pins[] = { 4, 5 };

static const int hvi3c7_pins[] = { 116, 117 };
static const int hvi3c10_pins[] = { 118, 119 };
static const int hvi3c11_pins[] = { 120, 121 };
static const int spim2_pins[] = { 60, 61, 62, 63, 64, 65, 66, 67 };
static const int siopbon1_pins[] = { 60 };
static const int siopbin1_pins[] = { 61 };
static const int sioscin1_pins[] = { 62 };
static const int sios3n1_pins[] = { 63 };
static const int sios5n1_pins[] = { 64 };
static const int siopwreqn1_pins[] = { 65 };
static const int sioonctrln1_pins[] = { 66 };
static const int siopwrgd1_pins[] = { 67 };
//general i2c
static const int i2c0_pins[] = { 164, 165 };
static const int i2c1_pins[] = { 166, 167 };
static const int i2c2_pins[] = { 168, 169 };
static const int i2c3_pins[] = { 170, 171 };
static const int i2c4_pins[] = { 172, 173 };
static const int i2c5_pins[] = { 174, 175 };
static const int i2c6_pins[] = { 176, 177 };
static const int i2c7_pins[] = { 178, 179 };
static const int i2c8_pins[] = { 180, 181 };
static const int i2c9_pins[] = { 182, 183 };
static const int i2c10_pins[] = { 184, 185 };
static const int i2c11_pins[] = { 186, 187 };
static const int i2c12_pins[] = { 60, 61 };
static const int i2c13_pins[] = { 62, 63 };
static const int i2c14_pins[] = { 64, 65 };
static const int i2c15_pins[] = { 66, 67 };

//dcsci i2c
static const int di2c8_pins[] = { 148, 152 };
static const int di2c9_pins[] = { 150, 151 };

static const int di2c10_pins[] = { 156, 157 };
static const int di2c13_pins[] = { 116, 117 };
static const int di2c14_pins[] = { 118, 119 };
static const int di2c15_pins[] = { 120, 121 };
// GPIOJ
static const int i3c4_pins[] = { 68, 69 };
static const int i3c5_pins[] = { 70, 71 };
static const int i3c6_pins[] = { 72, 73 };
static const int i3c7_pins[] = { 74, 75 };

// GPIOK
static const int i3c8_pins[] = { 76, 77 };
static const int i3c9_pins[] = { 78, 79 };
static const int i3c10_pins[] = { 80, 81 };
static const int i3c11_pins[] = { 82, 83 };

// GPIOL
static const int i3c0_pins[] = { 84, 85 };
static const int i3c1_pins[] = { 86, 87 };
static const int i3c2_pins[] = { 88, 89 };
static const int i3c3_pins[] = { 90, 91 };

static const int fsi0_pins[] = { 84, 85 };
static const int fsi1_pins[] = { 86, 87 };
static const int fsi2_pins[] = { 88, 89 };
static const int fsi3_pins[] = { 90, 91 };

//GPIOM
static const int espi0_pins[] = { 92, 93, 94, 95, 96, 97, 98, 99 };
static const int lpc0_pins[] = { 22, 23, 92, 93, 94, 95, 96, 97, 98, 99 };
static const int oscclk_pins[] = { 96 };

//GPION
static const int spi0_pins[] = { 100, 101, 102 };
static const int qspi0_pins[] = { 103, 104 };
static const int spi0cs1_pins[] = { 105 };
static const int spi0abr_pins[] = { 106 };
static const int spi0wpn_pins[] = { 107 };

//GPIOO
static const int spi1_pins[] = { 108, 109, 110 };
static const int qspi1_pins[] = { 111, 112 };
static const int spi1cs1_pins[] = { 113 };
static const int spi1abr_pins[] = { 114 };
static const int spi1wpn_pins[] = { 115 };

//GPIOP
static const int spi2_pins[] = { 116, 117, 118, 119 };
static const int qspi2_pins[] = { 120, 121 };
static const int spi2cs1_pins[] = { 122 };

static const int fwqspi_pins[] = { 162, 163 };
static const int fwspiabr_pins[] = { 123 };
static const int fwspiwpn_pins[] = { 131 };

static const int thru2_pins[] = { 114, 115 };
static const int thru3_pins[] = { 120, 121 };

static const int uart9_pins[] = { 108, 109 };
static const int uart10_pins[] = { 110, 111 };
static const int uart11_pins[] = { 112, 113 };

//GPIY
static const int salt4_pins[] = { 188 };
static const int salt5_pins[] = { 189 };
static const int salt6_pins[] = { 190 };
static const int salt7_pins[] = { 191 };
static const int salt8_pins[] = { 192 };
static const int salt9_pins[] = { 193 };
static const int salt10_pins[] = { 194 };
static const int salt11_pins[] = { 195 };

static const int adc0_pins[] = { 188 };
static const int adc1_pins[] = { 189 };
static const int adc2_pins[] = { 190 };
static const int adc3_pins[] = { 191 };
static const int adc4_pins[] = { 192 };
static const int adc5_pins[] = { 193 };
static const int adc6_pins[] = { 194 };
static const int adc7_pins[] = { 195 };
//GPIZ
static const int adc8_pins[] = { 196 };
static const int adc9_pins[] = { 197 };
static const int adc10_pins[] = { 198 };
static const int adc11_pins[] = { 199 };
static const int adc12_pins[] = { 200 };
static const int adc13_pins[] = { 201 };
static const int adc14_pins[] = { 202 };
static const int adc15_pins[] = { 203 };

static const int auxpwrgood0_pins[] = { 201 };
static const int auxpwrgood1_pins[] = { 202 };

//GPIAA
static const int sgpm0_pins[] = { 204, 205, 207, 208 };
static const int sgpm1_pins[] = { 209, 210, 58, 59 };

static const int ltpi_pins[] = { 84, 85, 86, 87 };

static const int jtagm1_pins[] = { 126, 127, 128, 129, 130 };

static const int mdio0_pins[] = { 144, 145 };
static const int mdio1_pins[] = { 160, 161 };
static const int mdio2_pins[] = { 124, 125 };

static const int rgmii0_pins[] = { 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143 };
static const int rgmii1_pins[] = { 148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 158, 159 };

static const int rmii0_pins[] = { 132, 134, 135, 136, 137, 138, 139, 140, 141 };
static const int rmii1_pins[] = { 148, 150, 151, 152, 153, 154, 155, 156, 157 };

static const int vga_pins[] = { 146, 147 };

static const int dsgpm1_pins[] = { 148, 152, 156, 157 };

static const int sgps_pins[] = { 149, 154, 158, 159 };

static const int i2cf0_pins[] = { 180, 181, 182, 183 };
static const int i2cf1_pins[] = { 172, 173, 174, 175 };
static const int i2cf2_pins[] = { 176, 177, 178, 179 };

static const int canbus_pins[] = { 183, 184, 185 };

static const int usbuart_pins[] = { 184, 185 };

static const int hbled_pins[] = { 206 };

static const int maclink0_pins[] = { 204 };
static const int maclink1_pins[] = { 205 };
static const int maclink2_pins[] = { 211 };

static const int ncts2_pins[] = { 204 };
static const int ndcd2_pins[] = { 205 };
static const int ndsr2_pins[] = { 207 };
static const int nri2_pins[] = { 208 };
static const int ndtr2_pins[] = { 209 };
static const int nrts2_pins[] = { 210 };

/*
 * pin:	     name, number
 * group:    name, npins,   pins
 * function: name, ngroups, groups
 */
struct aspeed_g7_soc1_pingroup {
	const char *name;
	const char *fn_name;
	const unsigned int *pins;
	int npins;
};

#define PINGROUP(pingrp_name, grp_fn_name, n) \
	{ .name = #pingrp_name, .fn_name = #grp_fn_name, \
	  .pins = n##_pins, .npins = ARRAY_SIZE(n##_pins) }

static struct aspeed_g7_soc1_pingroup aspeed_g7_soc1_pingroups[] = {
	PINGROUP(ESPI0, ESPI0, espi0),
	PINGROUP(ESPI1, ESPI1, espi1),
	PINGROUP(LPC0, LPC0, lpc0),
	PINGROUP(LPC1, LPC1, lpc1),
	PINGROUP(SD, SD, sd),
	PINGROUP(VPI, VPI, vpi),
	PINGROUP(OSCCLK, OSCCLK, oscclk),
	PINGROUP(TACH0, TACH0, tach0),
	PINGROUP(TACH1, TACH1, tach1),
	PINGROUP(TACH2, TACH2, tach2),
	PINGROUP(TACH3, TACH3, tach3),
	PINGROUP(THRU0, THRU0, thru0),
	PINGROUP(THRU1, THRU1, thru1),
	PINGROUP(TACH4, TACH4, tach4),
	PINGROUP(TACH5, TACH5, tach5),
	PINGROUP(TACH6, TACH6, tach6),
	PINGROUP(TACH7, TACH7, tach7),
	PINGROUP(NTCS5, NTCS5, ntcs5),
	PINGROUP(NDCD5, NDCD5, ndcd5),
	PINGROUP(NDSR5, NDSR5, ndsr5),
	PINGROUP(NRI5, NRI5, nri5),
	PINGROUP(SALT12, SALT12, salt12),
	PINGROUP(SALT13, SALT13, salt13),
	PINGROUP(SALT14, SALT14, salt14),
	PINGROUP(SALT15, SALT15, salt15),
	PINGROUP(NDTR5, NDTR5, ndtr5),
	PINGROUP(NRTS5, NRTS5, nrts5),
	PINGROUP(NCTS6, NCTS6, ncts6),
	PINGROUP(NDCD6, NDCD6, ndcd6),
	PINGROUP(NDSR6, NDSR6, ndsr6),
	PINGROUP(NRI6, NRI6, nri6),
	PINGROUP(NDTR6, NDTR6, ndtr6),
	PINGROUP(NRTS6, NRTS6, nrts6),
	PINGROUP(TACH8, TACH8, tach8),
	PINGROUP(TACH9, TACH9, tach9),
	PINGROUP(TACH10, TACH10, tach10),
	PINGROUP(TACH11, TACH11, tach11),
	PINGROUP(TACH12, TACH12, tach12),
	PINGROUP(TACH13, TACH13, tach13),
	PINGROUP(TACH14, TACH14, tach14),
	PINGROUP(TACH15, TACH15, tach15),
	PINGROUP(SPIM0, SPIM0, spim0),
	PINGROUP(PWM0, PWM0, pwm0),
	PINGROUP(PWM1, PWM1, pwm1),
	PINGROUP(PWM2, PWM2, pwm2),
	PINGROUP(PWM3, PWM3, pwm3),
	PINGROUP(PWM4, PWM4, pwm4),
	PINGROUP(PWM5, PWM5, pwm5),
	PINGROUP(PWM6, PWM6, pwm6),
	PINGROUP(PWM7, PWM7, pwm7),
	PINGROUP(SIOPBON0, SIOPBON0, siopbon0),
	PINGROUP(SIOPBIN0, SIOPBIN0, siopbin0),
	PINGROUP(SIOSCIN0, SIOSCIN0, sioscin0),
	PINGROUP(SIOS3N0, SIOS3N0, sios3n0),
	PINGROUP(SIOS5N0, SIOS5N0, sios5n0),
	PINGROUP(SIOPWREQN0, SIOPWREQN0, siopwreqn0),
	PINGROUP(SIOONCTRLN0, SIOONCTRLN0, sioonctrln0),
	PINGROUP(SIOPWRGD0, SIOPWRGD0, siopwrgd0),
	PINGROUP(NCTS0, NCTS0, ncts0),
	PINGROUP(NDCD0, NDCD0, ndcd0),
	PINGROUP(NDSR0, NDSR0, ndsr0),
	PINGROUP(NRI0, NRI0, nri0),
	PINGROUP(NDTR0, NDTR0, ndtr0),
	PINGROUP(NRTS0, NRTS0, nrts0),
	PINGROUP(UART0, UART0, uart0),
	PINGROUP(NCTS1, NCTS1, ncts1),
	PINGROUP(NDCD1, NDCD1, ndcd1),
	PINGROUP(NDSR1, NDSR1, ndsr1),
	PINGROUP(NRI1, NRI1, nri1),
	PINGROUP(NDTR1, NDTR1, ndtr1),
	PINGROUP(NRTS1, NRTS1, nrts1),
	PINGROUP(UART1, UART1, uart1),
	PINGROUP(UART2, UART2, uart2),
	PINGROUP(UART3, UART3, uart3),
	PINGROUP(UART5, UART5, uart5),
	PINGROUP(UART6, UART6, uart6),
	PINGROUP(UART7, UART7, uart7),
	PINGROUP(SPIM1, SPIM1, spim1),
	PINGROUP(WDTRST0N, WDTRST0N, wdtrst0n),
	PINGROUP(WDTRST1N, WDTRST1N, wdtrst1n),
	PINGROUP(WDTRST2N, WDTRST2N, wdtrst2n),
	PINGROUP(WDTRST3N, WDTRST3N, wdtrst3n),
	PINGROUP(WDTRST4N, WDTRST4N, wdtrst4n),
	PINGROUP(WDTRST5N, WDTRST5N, wdtrst5n),
	PINGROUP(WDTRST6N, WDTRST6N, wdtrst6n),
	PINGROUP(WDTRST7N, WDTRST7N, wdtrst7n),
	PINGROUP(PWM8, PWM8, pwm8),
	PINGROUP(PWM9, PWM9, pwm9),
	PINGROUP(PWM10, PWM10, pwm10),
	PINGROUP(PWM11, PWM11, pwm11),
	PINGROUP(PWM12, PWM12, pwm12),
	PINGROUP(PWM13, PWM13, pwm13),
	PINGROUP(PWM14, PWM14, pwm14),
	PINGROUP(PWM15, PWM15, pwm15),
	PINGROUP(SALT0, SALT0, salt0),
	PINGROUP(SALT1, SALT1, salt1),
	PINGROUP(SALT2, SALT2, salt2),
	PINGROUP(SALT3, SALT3, salt3),
	PINGROUP(FSI0, FSI0, fsi0),
	PINGROUP(FSI1, FSI1, fsi1),
	PINGROUP(FSI2, FSI2, fsi2),
	PINGROUP(FSI3, FSI3, fsi3),
	PINGROUP(SPIM2, SPIM2, spim2),
	PINGROUP(SALT4, SALT4, salt4),
	PINGROUP(SALT5, SALT5, salt5),
	PINGROUP(SALT6, SALT6, salt6),
	PINGROUP(SALT7, SALT7, salt7),
	PINGROUP(SALT8, SALT8, salt8),
	PINGROUP(SALT9, SALT9, salt9),
	PINGROUP(SALT10, SALT10, salt10),
	PINGROUP(SALT11, SALT11, salt11),
	PINGROUP(ADC0, ADC0, adc0),
	PINGROUP(ADC1, ADC1, adc1),
	PINGROUP(ADC2, ADC2, adc2),
	PINGROUP(ADC3, ADC3, adc3),
	PINGROUP(ADC4, ADC4, adc4),
	PINGROUP(ADC5, ADC5, adc5),
	PINGROUP(ADC6, ADC6, adc6),
	PINGROUP(ADC7, ADC7, adc7),
	PINGROUP(ADC8, ADC8, adc8),
	PINGROUP(ADC9, ADC9, adc9),
	PINGROUP(ADC10, ADC10, adc10),
	PINGROUP(ADC11, ADC11, adc11),
	PINGROUP(ADC12, ADC12, adc12),
	PINGROUP(ADC13, ADC13, adc13),
	PINGROUP(ADC14, ADC14, adc14),
	PINGROUP(ADC15, ADC15, adc15),
	PINGROUP(AUXPWRGOOD0, AUXPWRGOOD0, auxpwrgood0),
	PINGROUP(AUXPWRGOOD1, AUXPWRGOOD1, auxpwrgood1),
	PINGROUP(SGPM0, SGPM0, sgpm0),
	PINGROUP(SGPM1, SGPM1, sgpm1),
	PINGROUP(I2C0, I2C0, i2c0),
	PINGROUP(I2C1, I2C1, i2c1),
	PINGROUP(I2C2, I2C2, i2c2),
	PINGROUP(I2C3, I2C3, i2c3),
	PINGROUP(I2C4, I2C4, i2c4),
	PINGROUP(I2C5, I2C5, i2c5),
	PINGROUP(I2C6, I2C6, i2c6),
	PINGROUP(I2C7, I2C7, i2c7),
	PINGROUP(I2C8, I2C8, i2c8),
	PINGROUP(I2C9, I2C9, i2c9),
	PINGROUP(I2C10, I2C10, i2c10),
	PINGROUP(I2C11, I2C11, i2c11),
	PINGROUP(I2C12, I2C12, i2c12),
	PINGROUP(I2C13, I2C13, i2c13),
	PINGROUP(I2C14, I2C14, i2c14),
	PINGROUP(I2C15, I2C15, i2c15),
	PINGROUP(DI2C8, DI2C8, di2c8),
	PINGROUP(DI2C9, DI2C9, di2c9),
	PINGROUP(DI2C10, DI2C10, di2c10),
	PINGROUP(DI2C13, DI2C13, di2c13),
	PINGROUP(DI2C14, DI2C14, di2c14),
	PINGROUP(DI2C15, DI2C15, di2c15),
	PINGROUP(SIOPBON1, SIOPBON1, siopbon1),
	PINGROUP(SIOPBIN1, SIOPBIN1, siopbin1),
	PINGROUP(SIOSCIN1, SIOSCIN1, sioscin1),
	PINGROUP(SIOS3N1, SIOS3N1, sios3n1),
	PINGROUP(SIOS5N1, SIOS5N1, sios5n1),
	PINGROUP(SIOPWREQN1, SIOPWREQN1, siopwreqn1),
	PINGROUP(SIOONCTRLN1, SIOONCTRLN1, sioonctrln1),
	PINGROUP(SIOPWRGD1, SIOPWRGD1, siopwrgd1),
	PINGROUP(HVI3C0, HVI3C0, hvi3c0),
	PINGROUP(HVI3C1, HVI3C1, hvi3c1),
	PINGROUP(HVI3C2, HVI3C2, hvi3c2),
	PINGROUP(HVI3C3, HVI3C3, hvi3c3),
	PINGROUP(HVI3C4, HVI3C4, hvi3c4),
	PINGROUP(HVI3C5, HVI3C5, hvi3c5),
	PINGROUP(HVI3C6, HVI3C6, hvi3c6),
	PINGROUP(HVI3C7, HVI3C7, hvi3c7),
	PINGROUP(HVI3C10, HVI3C10, hvi3c10),
	PINGROUP(HVI3C11, HVI3C11, hvi3c11),
	PINGROUP(I3C4, I3C4, i3c4),
	PINGROUP(I3C5, I3C5, i3c5),
	PINGROUP(I3C6, I3C6, i3c6),
	PINGROUP(I3C7, I3C7, i3c7),
	PINGROUP(I3C8, I3C8, i3c8),
	PINGROUP(I3C9, I3C9, i3c9),
	PINGROUP(I3C10, I3C10, i3c10),
	PINGROUP(I3C11, I3C11, i3c11),
	PINGROUP(I3C0, I3C0, i3c0),
	PINGROUP(I3C1, I3C1, i3c1),
	PINGROUP(I3C2, I3C2, i3c2),
	PINGROUP(I3C3, I3C3, i3c3),
	PINGROUP(LTPI, LTPI, ltpi),
	PINGROUP(SPI0, SPI0, spi0),
	PINGROUP(QSPI0, QSPI0, qspi0),
	PINGROUP(SPI0CS1, SPI0CS1, spi0cs1),
	PINGROUP(SPI0ABR, SPI0ABR, spi0abr),
	PINGROUP(SPI0WPN, SPI0WPN, spi0wpn),
	PINGROUP(SPI1, SPI1, spi1),
	PINGROUP(QSPI1, QSPI1, qspi1),
	PINGROUP(SPI1CS1, SPI1CS1, spi1cs1),
	PINGROUP(SPI1ABR, SPI1ABR, spi1abr),
	PINGROUP(SPI1WPN, SPI1WPN, spi1wpn),
	PINGROUP(SPI2, SPI2, spi2),
	PINGROUP(QSPI2, QSPI2, qspi2),
	PINGROUP(SPI2CS1, SPI2CS1, spi2cs1),
	PINGROUP(THRU2, THRU2, thru2),
	PINGROUP(THRU3, THRU3, thru3),
	PINGROUP(JTAGM1, JTAGM1, jtagm1),
	PINGROUP(MDIO0, MDIO0, mdio0),
	PINGROUP(MDIO1, MDIO1, mdio1),
	PINGROUP(MDIO2, MDIO2, mdio2),
	PINGROUP(FWQSPI, FWQSPI, fwqspi),
	PINGROUP(FWSPIABR, FWSPIABR, fwspiabr),
	PINGROUP(FWSPIWPN, FWSPIWPN, fwspiwpn),
	PINGROUP(RGMII0, RGMII0, rgmii0),
	PINGROUP(RGMII1, RGMII1, rgmii1),
	PINGROUP(RMII0, RMII0, rmii0),
	PINGROUP(RMII1, RMII1, rmii1),
	PINGROUP(VGA, VGA, vga),
	PINGROUP(DSGPM1, DSGPM1, dsgpm1),
	PINGROUP(SGPS, SGPS, sgps),
	PINGROUP(I2CF0, I2CF0, i2cf0),
	PINGROUP(I2CF1, I2CF1, i2cf1),
	PINGROUP(I2CF2, I2CF2, i2cf2),
	PINGROUP(CANBUS, CANBUS, canbus),
	PINGROUP(USBUART, USBUART, usbuart),
	PINGROUP(HBLED, HBLED, hbled),
	PINGROUP(MACLINK0, MACLINK0, maclink0),
	PINGROUP(MACLINK1, MACLINK1, maclink1),
	PINGROUP(MACLINK2, MACLINK2, maclink2),
	PINGROUP(NCTS2, NCTS2, ncts2),
	PINGROUP(NDCD2, NDCD2, ndcd2),
	PINGROUP(NDSR2, NDSR2, ndsr2),
	PINGROUP(NRI2, NRI2, nri2),
	PINGROUP(NDTR2, NDTR2, ndtr2),
	PINGROUP(NRTS2, NRTS2, nrts2),
	PINGROUP(SMON0, SMON0, smon0),
	PINGROUP(SMON1, SMON1, smon1)
};

static const char *const espi0_grp[] = { "ESPI0" };
static const char *const espi1_grp[] = { "ESPI1" };
static const char *const lpc0_grp[] = { "LPC0" };
static const char *const lpc1_grp[] = { "LPC1" };
static const char *const vpi_grp[] = { "VPI" };
static const char *const sd_grp[] = { "SD" };
static const char *const oscclk_grp[] = { "OSCCLK" };
static const char *const tach0_grp[] = { "TACH0" };
static const char *const tach1_grp[] = { "TACH1" };
static const char *const tach2_grp[] = { "TACH2" };
static const char *const tach3_grp[] = { "TACH3" };
static const char *const tach4_grp[] = { "TACH4" };
static const char *const tach5_grp[] = { "TACH5" };
static const char *const tach6_grp[] = { "TACH6" };
static const char *const tach7_grp[] = { "TACH7" };
static const char *const thru0_grp[] = { "THRU0" };
static const char *const thru1_grp[] = { "THRU1" };

static const char *const ntcs5_grp[] = { "NTCS5" };
static const char *const ndcd5_grp[] = { "NDCD5" };
static const char *const ndsr5_grp[] = { "NDSR5" };
static const char *const nri5_grp[] = { "NRI5" };
static const char *const tach8_grp[] = { "TACH8" };
static const char *const tach9_grp[] = { "TACH9" };
static const char *const tach10_grp[] = { "TACH10" };
static const char *const tach11_grp[] = { "TACH11" };
static const char *const tach12_grp[] = { "TACH12" };
static const char *const tach13_grp[] = { "TACH13" };
static const char *const tach14_grp[] = { "TACH14" };
static const char *const tach15_grp[] = { "TACH15" };
static const char *const salt12_grp[] = { "SALT12" };
static const char *const salt13_grp[] = { "SALT13" };
static const char *const salt14_grp[] = { "SALT14" };
static const char *const salt15_grp[] = { "SALT15" };
static const char *const ndtr5_grp[] = { "NDTR5" };
static const char *const nrts5_grp[] = { "NRTS5" };
static const char *const ncts6_grp[] = { "NCTS6" };
static const char *const ndcd6_grp[] = { "NDCD6" };
static const char *const ndsr6_grp[] = { "NDSR6" };
static const char *const nri6_grp[] = { "NRI6" };
static const char *const ndtr6_grp[] = { "NDTR6" };
static const char *const nrts6_grp[] = { "NRTS6" };
static const char *const spim0_grp[] = { "SPIM0" };
static const char *const pwm0_grp[] = { "PWM0" };
static const char *const pwm1_grp[] = { "PWM1" };
static const char *const pwm2_grp[] = { "PWM2" };
static const char *const pwm3_grp[] = { "PWM3" };
static const char *const pwm4_grp[] = { "PWM4" };
static const char *const pwm5_grp[] = { "PWM5" };
static const char *const pwm6_grp[] = { "PWM6" };
static const char *const pwm7_grp[] = { "PWM7" };
static const char *const siopbon0_grp[] = { "SIOPBON0" };
static const char *const siopbin0_grp[] = { "SIOPBIN0" };
static const char *const sioscin0_grp[] = { "SIOSCIN0" };
static const char *const sios3n0_grp[] = { "SIOS3N0" };
static const char *const sios5n0_grp[] = { "SIOS5N0" };
static const char *const siopwreqn0_grp[] = { "SIOPWREQN0" };
static const char *const sioonctrln0_grp[] = { "SIOONCTRLN0" };
static const char *const siopwrgd0_grp[] = { "SIOPWRGD0" };
static const char *const ncts0_grp[] = { "NCTS0" };
static const char *const ndcd0_grp[] = { "NDCD0" };
static const char *const ndsr0_grp[] = { "NDSR0" };
static const char *const nri0_grp[] = { "NRI0" };
static const char *const ndtr0_grp[] = { "NDTR0" };
static const char *const nrts0_grp[] = { "NRTS0" };
static const char *const uart0_grp[] = { "UART0" };
static const char *const ncts1_grp[] = { "NCTS1" };
static const char *const ndcd1_grp[] = { "NDCD1" };
static const char *const ndsr1_grp[] = { "NDSR1" };
static const char *const nri1_grp[] = { "NRI1" };
static const char *const ndtr1_grp[] = { "NDTR1" };
static const char *const nrts1_grp[] = { "NRTS1" };
static const char *const uart1_grp[] = { "UART1" };
static const char *const uart2_grp[] = { "UART2" };
static const char *const uart3_grp[] = { "UART3" };
static const char *const uart5_grp[] = { "UART5" };
static const char *const uart6_grp[] = { "UART6" };
static const char *const uart7_grp[] = { "UART7" };
static const char *const spim1_grp[] = { "SPIM1" };
static const char *const spim2_grp[] = { "SPIM2" };
static const char *const pwm8_grp[] = { "PWM8" };
static const char *const pwm9_grp[] = { "PWM9" };
static const char *const pwm10_grp[] = { "PWM10" };
static const char *const pwm11_grp[] = { "PWM11" };
static const char *const pwm12_grp[] = { "PWM12" };
static const char *const pwm13_grp[] = { "PWM13" };
static const char *const pwm14_grp[] = { "PWM14" };
static const char *const pwm15_grp[] = { "PWM15" };
static const char *const wdtrst0n_grp[] = { "WDTRST0N" };
static const char *const wdtrst1n_grp[] = { "WDTRST1N" };
static const char *const wdtrst2n_grp[] = { "WDTRST2N" };
static const char *const wdtrst3n_grp[] = { "WDTRST3N" };
static const char *const wdtrst4n_grp[] = { "WDTRST4N" };
static const char *const wdtrst5n_grp[] = { "WDTRST5N" };
static const char *const wdtrst6n_grp[] = { "WDTRST6N" };
static const char *const wdtrst7n_grp[] = { "WDTRST7N" };
static const char *const fsi0_grp[] = { "FSI0" };
static const char *const fsi1_grp[] = { "FSI1" };
static const char *const fsi2_grp[] = { "FSI2" };
static const char *const fsi3_grp[] = { "FSI3" };
static const char *const salt4_grp[] = { "ASLT4" };
static const char *const salt5_grp[] = { "ASLT5" };
static const char *const salt6_grp[] = { "ASLT6" };
static const char *const salt7_grp[] = { "ASLT7" };
static const char *const salt8_grp[] = { "ASLT8" };
static const char *const salt9_grp[] = { "ASLT9" };
static const char *const salt10_grp[] = { "ASLT10" };
static const char *const salt11_grp[] = { "ASLT11" };
static const char *const adc0_grp[] = { "ADC0" };
static const char *const adc1_grp[] = { "ADC1" };
static const char *const adc2_grp[] = { "ADC2" };
static const char *const adc3_grp[] = { "ADC3" };
static const char *const adc4_grp[] = { "ADC4" };
static const char *const adc5_grp[] = { "ADC5" };
static const char *const adc6_grp[] = { "ADC6" };
static const char *const adc7_grp[] = { "ADC7" };
static const char *const adc8_grp[] = { "ADC8" };
static const char *const adc9_grp[] = { "ADC9" };
static const char *const adc10_grp[] = { "ADC10" };
static const char *const adc11_grp[] = { "ADC11" };
static const char *const adc12_grp[] = { "ADC12" };
static const char *const adc13_grp[] = { "ADC13" };
static const char *const adc14_grp[] = { "ADC14" };
static const char *const adc15_grp[] = { "ADC15" };
static const char *const auxpwrgood0_grp[] = { "AUXPWRGOOD0" };
static const char *const auxpwrgood1_grp[] = { "AUXPWRGOOD1" };
static const char *const sgpm0_grp[] = { "SGPM0" };
static const char *const sgpm1_grp[] = { "SGPM1" };
static const char *const i2c0_grp[] = { "I2C0" };
static const char *const i2c1_grp[] = { "I2C1" };
static const char *const i2c2_grp[] = { "I2C2" };
static const char *const i2c3_grp[] = { "I2C3" };
static const char *const i2c4_grp[] = { "I2C4" };
static const char *const i2c5_grp[] = { "I2C5" };
static const char *const i2c6_grp[] = { "I2C6" };
static const char *const i2c7_grp[] = { "I2C7" };
static const char *const i2c8_grp[] = { "I2C8" };
static const char *const i2c9_grp[] = { "I2C9" };
static const char *const i2c10_grp[] = { "I2C10" };
static const char *const i2c11_grp[] = { "I2C11" };
static const char *const i2c12_grp[] = { "I2C12" };
static const char *const i2c13_grp[] = { "I2C13" };
static const char *const i2c14_grp[] = { "I2C14" };
static const char *const i2c15_grp[] = { "I2C15" };
static const char *const di2c8_grp[] = { "DI2C8" };
static const char *const di2c9_grp[] = { "DI2C9" };
static const char *const di2c10_grp[] = { "DI2C10" };
static const char *const di2c13_grp[] = { "DI2C13" };
static const char *const di2c14_grp[] = { "DI2C14" };
static const char *const di2c15_grp[] = { "DI2C15" };
static const char *const siopbon1_grp[] = { "SIOPBON1" };
static const char *const siopbin1_grp[] = { "SIOPBIN1" };
static const char *const sioscin1_grp[] = { "SIOSCIN1" };
static const char *const sios3n1_grp[] = { "SIOS3N1" };
static const char *const sios5n1_grp[] = { "SIOS5N1" };
static const char *const siopwreqn1_grp[] = { "SIOPWREQN1" };
static const char *const sioonctrln1_grp[] = { "SIOONCTRLN1" };
static const char *const siopwrgd1_grp[] = { "SIOPWRGD1" };
static const char *const hvi3c0_grp[] = { "HVI3C0" };
static const char *const hvi3c1_grp[] = { "HVI3C1" };
static const char *const hvi3c2_grp[] = { "HVI3C2" };
static const char *const hvi3c3_grp[] = { "HVI3C3" };
static const char *const hvi3c4_grp[] = { "HVI3C4" };
static const char *const hvi3c5_grp[] = { "HVI3C5" };
static const char *const hvi3c6_grp[] = { "HVI3C6" };
static const char *const hvi3c7_grp[] = { "HVI3C7" };
static const char *const hvi3c10_grp[] = { "HVI3C10" };
static const char *const hvi3c11_grp[] = { "HVI3C11" };
static const char *const i3c0_grp[] = { "I3C0" };
static const char *const i3c1_grp[] = { "I3C1" };
static const char *const i3c2_grp[] = { "I3C2" };
static const char *const i3c3_grp[] = { "I3C3" };
static const char *const i3c4_grp[] = { "I3C4" };
static const char *const i3c5_grp[] = { "I3C5" };
static const char *const i3c6_grp[] = { "I3C6" };
static const char *const i3c7_grp[] = { "I3C7" };
static const char *const i3c8_grp[] = { "I3C8" };
static const char *const i3c9_grp[] = { "I3C9" };
static const char *const i3c10_grp[] = { "I3C10" };
static const char *const i3c11_grp[] = { "I3C11" };
static const char *const ltpi_grp[] = { "LTPI" };
static const char *const spi0_grp[] = { "SPI0" };
static const char *const qspi0_grp[] = { "QSPI0" };
static const char *const spi0cs1_grp[] = { "SPI0CS1" };
static const char *const spi0abr_grp[] = { "SPI0ABR" };
static const char *const spi0wpn_grp[] = { "SPI0WPN" };
static const char *const spi1_grp[] = { "SPI1" };
static const char *const qspi1_grp[] = { "QSPI1" };
static const char *const spi1cs1_grp[] = { "SPI1CS1" };
static const char *const spi1abr_grp[] = { "SPI1ABR" };
static const char *const spi1wpn_grp[] = { "SPI1WPN" };
static const char *const spi2_grp[] = { "SPI2" };
static const char *const qspi2_grp[] = { "QSPI2" };
static const char *const spi2cs1_grp[] = { "SPI2CS1" };
static const char *const thru2_grp[] = { "THRU2" };
static const char *const thru3_grp[] = { "THRU3" };
static const char *const jtagm1_grp[] = { "JTAGM1" };
static const char *const mdio0_grp[] = { "MDIO0" };
static const char *const mdio1_grp[] = { "MDIO1" };
static const char *const mdio2_grp[] = { "MDIO2" };
static const char *const fwqspi_grp[] = { "FWQSPI" };
static const char *const fwspiabr_grp[] = { "FWSPIABR" };
static const char *const fwspiwpn_grp[] = { "FWSPIWPN" };
static const char *const rgmii0_grp[] = { "RGMII0" };
static const char *const rgmii1_grp[] = { "RGMII1" };
static const char *const rmii0_grp[] = { "RMII0" };
static const char *const rmii1_grp[] = { "RMII1" };
static const char *const vga_grp[] = { "VGA" };
static const char *const dsgpm1_grp[] = { "DSGPM1" };
static const char *const sgps_grp[] = { "SGPS" };
static const char *const i2cf0_grp[] = { "I2CF0" };
static const char *const i2cf1_grp[] = { "I2CF1" };
static const char *const i2cf2_grp[] = { "I2CF2" };
static const char *const canbus_grp[] = { "CANBUS" };
static const char *const usbuart_grp[] = { "USBUART" };
static const char *const hbled_grp[] = { "HBLED" };
static const char *const maclink0_grp[] = { "MACLINK0" };
static const char *const maclink1_grp[] = { "MACLINK1" };
static const char *const maclink2_grp[] = { "MACLINK2" };
static const char *const ncts2_grp[] = { "NCTS2" };
static const char *const ndcd2_grp[] = { "NDCD2" };
static const char *const ndsr2_grp[] = { "NDSR2" };
static const char *const nri2_grp[] = { "NRI2" };
static const char *const ndtr2_grp[] = { "NDTR2" };
static const char *const nrts2_grp[] = { "NRTS2" };
static const char *const smon0_grp[] = { "SMON0" };
static const char *const smon1_grp[] = { "SMON1" };

struct aspeed_g7_soc1_func {
	const char *name;
	const unsigned int ngroups;
	const char *const *groups;
};

#define IO_FUNC(fn_name, fn) \
		{ .name = #fn_name, .ngroups = ARRAY_SIZE(fn##_grp), .groups = fn##_grp }

static struct aspeed_g7_soc1_func aspeed_g7_soc1_funcs[] = {
	IO_FUNC(ESPI0, espi0),
	IO_FUNC(ESPI1, espi1),
	IO_FUNC(LPC0, lpc0),
	IO_FUNC(LPC1, lpc1),
	IO_FUNC(VPI, vpi),
	IO_FUNC(SD, sd),
	IO_FUNC(OSCCLK, oscclk),
	IO_FUNC(HVI3C0, hvi3c0),
	IO_FUNC(HVI3C1, hvi3c1),
	IO_FUNC(HVI3C2, hvi3c2),
	IO_FUNC(HVI3C3, hvi3c3),
	IO_FUNC(HVI3C4, hvi3c4),
	IO_FUNC(HVI3C5, hvi3c5),
	IO_FUNC(HVI3C6, hvi3c6),
	IO_FUNC(HVI3C7, hvi3c7),
	IO_FUNC(HVI3C10, hvi3c10),
	IO_FUNC(HVI3C11, hvi3c11),
	IO_FUNC(TACH0, tach0),
	IO_FUNC(TACH1, tach1),
	IO_FUNC(TACH2, tach2),
	IO_FUNC(TACH3, tach3),
	IO_FUNC(TACH4, tach4),
	IO_FUNC(TACH5, tach5),
	IO_FUNC(TACH6, tach6),
	IO_FUNC(TACH7, tach7),
	IO_FUNC(THRU0, thru0),
	IO_FUNC(THRU1, thru1),
	IO_FUNC(NTCS5, ntcs5),
	IO_FUNC(NTCS5, ndcd5),
	IO_FUNC(NDSR5, ndsr5),
	IO_FUNC(NRI5, nri5),
	IO_FUNC(NRI5, nri5),
	IO_FUNC(SALT12, salt12),
	IO_FUNC(SALT13, salt13),
	IO_FUNC(SALT14, salt14),
	IO_FUNC(SALT15, salt15),
	IO_FUNC(NDTR5, ndtr5),
	IO_FUNC(NRTS5, nrts5),
	IO_FUNC(NCTS6, ncts6),
	IO_FUNC(NDCD6, ndcd6),
	IO_FUNC(NDSR6, ndsr6),
	IO_FUNC(NRI6, nri6),
	IO_FUNC(NDTR6, ndtr6),
	IO_FUNC(NRTS6, nrts6),
	IO_FUNC(TACH8, tach8),
	IO_FUNC(TACH9, tach9),
	IO_FUNC(TACH10, tach10),
	IO_FUNC(TACH11, tach11),
	IO_FUNC(TACH12, tach12),
	IO_FUNC(TACH13, tach13),
	IO_FUNC(TACH14, tach14),
	IO_FUNC(TACH15, tach15),
	IO_FUNC(SPIM0, spim0),
	IO_FUNC(PWM0, pwm0),
	IO_FUNC(PWM1, pwm1),
	IO_FUNC(PWM2, pwm2),
	IO_FUNC(PWM3, pwm3),
	IO_FUNC(PWM4, pwm4),
	IO_FUNC(PWM5, pwm5),
	IO_FUNC(PWM6, pwm6),
	IO_FUNC(PWM7, pwm7),
	IO_FUNC(SIOPBON0, siopbon0),
	IO_FUNC(SIOPBIN0, siopbin0),
	IO_FUNC(SIOSCIN0, sioscin0),
	IO_FUNC(SIOS3N0, sios3n0),
	IO_FUNC(SIOS5N0, sios5n0),
	IO_FUNC(SIOPWREQN0, siopwreqn0),
	IO_FUNC(SIOONCTRLN0, sioonctrln0),
	IO_FUNC(SIOPWRGD0, siopwrgd0),
	IO_FUNC(NCTS0, ncts0),
	IO_FUNC(NDCD0, ndcd0),
	IO_FUNC(NDSR0, ndsr0),
	IO_FUNC(NRI0, nri0),
	IO_FUNC(NDTR0, ndtr0),
	IO_FUNC(NRTS0, nrts0),
	IO_FUNC(UART0, uart0),
	IO_FUNC(NCTS1, ncts1),
	IO_FUNC(NDCD1, ndcd1),
	IO_FUNC(NDSR1, ndsr1),
	IO_FUNC(NRI1, nri1),
	IO_FUNC(NDTR1, ndtr1),
	IO_FUNC(NRTS1, nrts1),
	IO_FUNC(UART1, uart1),
	IO_FUNC(UART2, uart2),
	IO_FUNC(UART3, uart3),
	IO_FUNC(UART5, uart5),
	IO_FUNC(UART6, uart6),
	IO_FUNC(UART7, uart7),
	IO_FUNC(SPIM1, spim1),
	IO_FUNC(PWM7, pwm7),
	IO_FUNC(PWM8, pwm8),
	IO_FUNC(PWM9, pwm9),
	IO_FUNC(PWM10, pwm10),
	IO_FUNC(PWM11, pwm11),
	IO_FUNC(PWM12, pwm12),
	IO_FUNC(PWM13, pwm13),
	IO_FUNC(PWM14, pwm14),
	IO_FUNC(PWM15, pwm15),
	IO_FUNC(WDTRST0N, wdtrst0n),
	IO_FUNC(WDTRST1N, wdtrst1n),
	IO_FUNC(WDTRST2N, wdtrst2n),
	IO_FUNC(WDTRST3N, wdtrst3n),
	IO_FUNC(WDTRST4N, wdtrst4n),
	IO_FUNC(WDTRST5N, wdtrst5n),
	IO_FUNC(WDTRST6N, wdtrst6n),
	IO_FUNC(WDTRST7N, wdtrst7n),
	IO_FUNC(FSI0, fsi0),
	IO_FUNC(FSI1, fsi1),
	IO_FUNC(FSI2, fsi2),
	IO_FUNC(FSI3, fsi3),
	IO_FUNC(SALT4, salt4),
	IO_FUNC(SALT5, salt5),
	IO_FUNC(SALT6, salt6),
	IO_FUNC(SALT7, salt7),
	IO_FUNC(SALT8, salt8),
	IO_FUNC(SALT9, salt9),
	IO_FUNC(SALT10, salt10),
	IO_FUNC(SALT11, salt11),
	IO_FUNC(ADC0, adc0),
	IO_FUNC(ADC1, adc1),
	IO_FUNC(ADC2, adc2),
	IO_FUNC(ADC3, adc3),
	IO_FUNC(ADC4, adc4),
	IO_FUNC(ADC5, adc5),
	IO_FUNC(ADC6, adc6),
	IO_FUNC(ADC7, adc7),
	IO_FUNC(ADC8, adc8),
	IO_FUNC(ADC9, adc9),
	IO_FUNC(ADC10, adc10),
	IO_FUNC(ADC11, adc11),
	IO_FUNC(ADC12, adc12),
	IO_FUNC(ADC13, adc13),
	IO_FUNC(ADC14, adc14),
	IO_FUNC(ADC15, adc15),
	IO_FUNC(AUXPWRGOOD0, auxpwrgood0),
	IO_FUNC(AUXPWRGOOD1, auxpwrgood1),
	IO_FUNC(SGPM0, sgpm0),
	IO_FUNC(SGPM1, sgpm1),
	IO_FUNC(SPIM2, spim2),
	IO_FUNC(I2C0, i2c0),
	IO_FUNC(I2C1, i2c1),
	IO_FUNC(I2C2, i2c2),
	IO_FUNC(I2C3, i2c3),
	IO_FUNC(I2C4, i2c4),
	IO_FUNC(I2C5, i2c5),
	IO_FUNC(I2C6, i2c6),
	IO_FUNC(I2C7, i2c7),
	IO_FUNC(I2C8, i2c8),
	IO_FUNC(I2C9, i2c9),
	IO_FUNC(I2C10, i2c10),
	IO_FUNC(I2C11, i2c11),
	IO_FUNC(I2C12, i2c12),
	IO_FUNC(I2C13, i2c13),
	IO_FUNC(I2C14, i2c14),
	IO_FUNC(I2C15, i2c15),
	IO_FUNC(DI2C8, di2c8),
	IO_FUNC(DI2C9, di2c9),
	IO_FUNC(DI2C10, di2c10),
	IO_FUNC(DI2C13, di2c13),
	IO_FUNC(DI2C14, di2c14),
	IO_FUNC(DI2C15, di2c15),
	IO_FUNC(SIOPBON1, siopbon1),
	IO_FUNC(SIOPBIN1, siopbin1),
	IO_FUNC(SIOSCIN1, sioscin1),
	IO_FUNC(SIOS3N1, sios3n1),
	IO_FUNC(SIOS5N1, sios5n1),
	IO_FUNC(SIOPWREQN1, siopwreqn1),
	IO_FUNC(SIOONCTRLN1, sioonctrln1),
	IO_FUNC(SIOPWRGD1, siopwrgd1),
	IO_FUNC(I3C0, i3c0),
	IO_FUNC(I3C1, i3c1),
	IO_FUNC(I3C2, i3c2),
	IO_FUNC(I3C3, i3c3),
	IO_FUNC(I3C4, i3c4),
	IO_FUNC(I3C5, i3c5),
	IO_FUNC(I3C6, i3c6),
	IO_FUNC(I3C7, i3c7),
	IO_FUNC(I3C8, i3c8),
	IO_FUNC(I3C9, i3c9),
	IO_FUNC(I3C10, i3c10),
	IO_FUNC(I3C11, i3c11),
	IO_FUNC(LTPI, ltpi),
	IO_FUNC(SPI0, spi0),
	IO_FUNC(QSPI0, qspi0),
	IO_FUNC(SPI0CS1, spi0cs1),
	IO_FUNC(SPI0ABR, spi0abr),
	IO_FUNC(SPI0WPN, spi0wpn),
	IO_FUNC(SPI1, spi1),
	IO_FUNC(QSPI1, qspi1),
	IO_FUNC(SPI1CS1, spi1cs1),
	IO_FUNC(SPI1ABR, spi1abr),
	IO_FUNC(SPI1WPN, spi1wpn),
	IO_FUNC(SPI2, spi2),
	IO_FUNC(QSPI2, qspi2),
	IO_FUNC(SPI2CS1, spi2cs1),
	IO_FUNC(THRU2, thru2),
	IO_FUNC(THRU3, thru3),
	IO_FUNC(JTAGM1, jtagm1),
	IO_FUNC(MDIO0, mdio0),
	IO_FUNC(MDIO1, mdio1),
	IO_FUNC(MDIO2, mdio2),
	IO_FUNC(FWQSPI, fwqspi),
	IO_FUNC(FWSPIABR, fwspiabr),
	IO_FUNC(FWSPIWPN, fwspiwpn),
	IO_FUNC(RGMII0, rgmii0),
	IO_FUNC(RGMII1, rgmii1),
	IO_FUNC(RMII0, rmii0),
	IO_FUNC(RMII1, rmii1),
	IO_FUNC(VGA, vga),
	IO_FUNC(DSGPM1, dsgpm1),
	IO_FUNC(SGPS, sgps),
	IO_FUNC(I2CF0, i2cf0),
	IO_FUNC(I2CF1, i2cf1),
	IO_FUNC(I2CF2, i2cf2),
	IO_FUNC(CANBUS, canbus),
	IO_FUNC(USBUART, usbuart),
	IO_FUNC(HBLED, hbled),
	IO_FUNC(MACLINK0, maclink0),
	IO_FUNC(MACLINK1, maclink1),
	IO_FUNC(MACLINK2, maclink2),
	IO_FUNC(NCTS2, ncts2),
	IO_FUNC(NDCD2, ndcd2),
	IO_FUNC(NDSR2, ndsr2),
	IO_FUNC(NRI2, nri2),
	IO_FUNC(NDTR2, ndtr2),
	IO_FUNC(NRTS2, nrts2),
	IO_FUNC(SMON0, smon0),
	IO_FUNC(SMON1, smon1),
};

/* number, name, drv_data */
static const struct pinctrl_pin_desc aspeed_g7_soc1_pins[] = {
	PINCTRL_PIN(0, "ESPI1CK_L1CLK_SD0CLK_I3CSCL4_SM3CS0NI_GPIOA0"),
	PINCTRL_PIN(1, "ESPI1RSTN_LPC1RSTN_SD0CDN_I3CSDA4_SM3CLKI_GPIOA1"),
	PINCTRL_PIN(2, "ESPI1ALTN_L1SIRQN_SD0CMD_I3CSCL5_SM3MOSII_GPIOA2"),
	PINCTRL_PIN(3, "ESPI1CSN_L1FRAMEN_SD0WPN_I3CSDA5_SM3MISOI_GPIOA3"),
	PINCTRL_PIN(4, "ESPI1D0_L1AD0_SD0DAT0_I3CSCL6_SM3IO2I_GPIOA4"),
	PINCTRL_PIN(5, "ESPI1D1_L1AD1_SD0DAT1_I3CSDA6_SM3IO3I_GPIOA5"),
	PINCTRL_PIN(6, "ESPI1D2_L1AD2_SD0DAT2_SM3CS0NO_GPIOA6"),
	PINCTRL_PIN(7, "ESPI1D3_L1AD3_SD0DAT3_SM3MUXSEL_GPIOA7"),
	PINCTRL_PIN(8, "GPIOB0_TACH0_THRUIN0_SCMGPI0"),
	PINCTRL_PIN(9, "GPIOB1_TACH1_THRUOUT0_SCMGPI1"),
	PINCTRL_PIN(10, "GPIOB2_TACH2_THRUIN1_SCMGPI2"),
	PINCTRL_PIN(11, "GPIOB3_TACH3_THRUOUT1_SCMGPI3"),
	PINCTRL_PIN(12, "GPIOB4_TACH4_NCTS5_SCMGPI4"),
	PINCTRL_PIN(13, "GPIOB5_TACH5_NDCD5_SCMGPO0"),
	PINCTRL_PIN(14, "GPIOB6_TACH6_NDSR5_SCMGPO1"),
	PINCTRL_PIN(15, "GPIOB7_TACH7_NRI5_SCMGPO2"),
	PINCTRL_PIN(16, "GPIOC0_TACH8_NDTR5_SCMGPO3"),
	PINCTRL_PIN(17, "GPIOC1_TACH9_NRTS5_SCMGPO4"),
	PINCTRL_PIN(18, "GPIOC2_TACH10_SALT12_NCTS6_SCMGPI5"),
	PINCTRL_PIN(19, "GPIOC3_TACH11_SALT13_NDCD6_SCMGPI6"),
	PINCTRL_PIN(20, "GPIOC4_TACH12_SALT14_NDSR6_SCMGPI7"),
	PINCTRL_PIN(21, "GPIOC5_TACH13_SALT15_NRI6_SCMGPO5"),
	PINCTRL_PIN(22, "GPIOC6_TACH14_NDTR6_SCMGPO6"),
	PINCTRL_PIN(23, "GPIOC7_TACH15_NRTS6_SM0CS0NI_SCMGPO7"),
	PINCTRL_PIN(24, "GPIOD0_PWM0_SIOPBON0_SM0CLKI_HPMGPI0"),
	PINCTRL_PIN(25, "GPIOD1_PWM1_SIOPBIN0_SM0MOSII_HPMGPI1"),
	PINCTRL_PIN(26, "GPIOD2_PWM2_SIOSCIN0_SM0MISOI_HPMGPI2"),
	PINCTRL_PIN(27, "GPIOD3_PWM3_SIOS3N0_SM0IO2I_HPMGPI3"),
	PINCTRL_PIN(28, "GPIOD4_PWM4_SIOS5N0_SM0IO3I_HPMGPO0"),
	PINCTRL_PIN(29, "GPIOD5_PWM5_SIOPWREQN0_SM0CS0NO_HPMGPO1"),
	PINCTRL_PIN(30, "GPIOD6_PWM6_SIOONCTRLN0_SM0MUXSEL_HPMGPO2"),
	PINCTRL_PIN(31, "GPIOD7_PWM7_SIOPWRGD0_HPMGPO3"),
	PINCTRL_PIN(32, "GPIOE0_NCTS0"),
	PINCTRL_PIN(33, "GPIOE1_NDCD0"),
	PINCTRL_PIN(34, "GPIOE2_NDSR0"),
	PINCTRL_PIN(35, "GPIOE3_NRI0"),
	PINCTRL_PIN(36, "GPIOE4_NDTR0"),
	PINCTRL_PIN(37, "GPIOE5_NRTS0"),
	PINCTRL_PIN(38, "TXD0_GPIOE6"),
	PINCTRL_PIN(39, "RXD0_GPIOE7"),
	PINCTRL_PIN(40, "GPIOF0_NCTS1"),
	PINCTRL_PIN(41, "GPIOF1_NDCD1"),
	PINCTRL_PIN(42, "GPIOF2_NDSR1"),
	PINCTRL_PIN(43, "GPIOF3_NRI1"),
	PINCTRL_PIN(44, "GPIOF4_NDTR1"),
	PINCTRL_PIN(45, "GPIOF5_NRTS1"),
	PINCTRL_PIN(46, "GPIOF6_TXD1"),
	PINCTRL_PIN(47, "GPIOF7_RXD1"),
	PINCTRL_PIN(48, "GPIOG0_TXD2"),
	PINCTRL_PIN(49, "GPIOG1_RXD2"),
	PINCTRL_PIN(50, "GPIOG2_TXD3_WDTRST0N_PWM8_SM1CS0NI"),
	PINCTRL_PIN(51, "GPIOG3_RXD3_WDTRST1N_PWM9_SM1CLKI"),
	PINCTRL_PIN(52, "TXD5_GPIOG4_WDTRST2N_PWM10_SM1MOSII_TXD5"),
	PINCTRL_PIN(53, "RXD5_GPIOG5_WDTRST3N_PWM11_SM1MISOI_RXD5"),
	PINCTRL_PIN(54, "GPIOG6_TXD6_SALT0_PWM12_SM1IO2I"),
	PINCTRL_PIN(55, "GPIOG7_RXD6_SALT1_PWM13_SM1IO3I"),
	PINCTRL_PIN(56, "GPIOH0_TXD7_SALT2_PWM14_SM1CS0NO"),
	PINCTRL_PIN(57, "GPIOH1_RXD7_SALT3_PWM15_SM1MUXSEL"),
	PINCTRL_PIN(58, "GPIOH2_SGPM1O_WDTRST7N_PESGWAKEN_SFF8485I0B"),
	PINCTRL_PIN(59, "GPIOH3_SGPM1I_SFF8485I1B"),
	PINCTRL_PIN(60, "GPIOI0_HVI3C0SCL_SCL12_SIOPBON1_SM2MUXSEL"),
	PINCTRL_PIN(61, "GPIOI1_HVI3C0SDA_SDA12_SIOPBIN1_SM2CS0NI"),
	PINCTRL_PIN(62, "GPIOI2_HVI3C1SCL_SCL13_SIOSCIN1_SM2CLKI"),
	PINCTRL_PIN(63, "GPIOI3_HVI3C1SDA_SDA13_SIOS3N1_SM2MOSII"),
	PINCTRL_PIN(64, "GPIOI4_HVI3C2SCL_SCL14_SIOS5N1_SM2MISOI"),
	PINCTRL_PIN(65, "GPIOI5_HVI3C2SDA_SDA14_SIOPWREQN1_SM2IO2I"),
	PINCTRL_PIN(66, "GPIOI6_HVI3C3SCL_SCL15_SIOONCTRLN1_SM2IO3I"),
	PINCTRL_PIN(67, "GPIOI7_HVI3C3SDA_SDA15_SIOPWRGD1_SM2CS0NO"),
	PINCTRL_PIN(68, "GPIOJ0_I3CSCL4"),
	PINCTRL_PIN(69, "GPIOJ1_I3CSDA4"),
	PINCTRL_PIN(70, "GPIOJ2_I3CSCL5"),
	PINCTRL_PIN(71, "GPIOJ3_I3CSDA5"),
	PINCTRL_PIN(72, "GPIOJ4_I3CSCL6"),
	PINCTRL_PIN(73, "GPIOJ5_I3CSDA6"),
	PINCTRL_PIN(74, "GPIOJ6_I3CSCL7"),
	PINCTRL_PIN(75, "GPIOJ7_I3CSDA7"),
	PINCTRL_PIN(76, "GPIOK0_I3CSCL8"),
	PINCTRL_PIN(77, "GPIOK1_I3CSDA8"),
	PINCTRL_PIN(78, "GPIOK2_I3CSCL9"),
	PINCTRL_PIN(79, "GPIOK3_I3CSDA9"),
	PINCTRL_PIN(80, "GPIOK4_I3CSCL10"),
	PINCTRL_PIN(81, "GPIOK5_I3CSDA10"),
	PINCTRL_PIN(82, "GPIOK6_I3CSCL11"),
	PINCTRL_PIN(83, "GPIOK7_I3CSDA11"),
	PINCTRL_PIN(84, "GPIOL0_I3CSCL0_FSI0CLK_LTPITXCLK"),
	PINCTRL_PIN(85, "GPIOL1_I3CSDA0_FSI0DATA_LTPIRXCLK"),
	PINCTRL_PIN(86, "GPIOL2_I3CSCL1_FSI1CLK_LTPITXDAT"),
	PINCTRL_PIN(87, "GPIOL3_I3CSDA1_FSI1DATA_LTPIRXDAT"),
	PINCTRL_PIN(88, "GPIOL4_I3CSCL2_FSI2CLK"),
	PINCTRL_PIN(89, "GPIOL5_I3CSDA2_FSI2DATA"),
	PINCTRL_PIN(90, "GPIOL6_I3CSCL3_FSI3CLK"),
	PINCTRL_PIN(91, "GPIOL7_I3CSDA3_FSI3DATA"),
	PINCTRL_PIN(92, "ESPI0D0_L0AD0_GPIOM0"),
	PINCTRL_PIN(93, "ESPI0D1_L0AD1_GPIOM1"),
	PINCTRL_PIN(94, "ESPI0D2_L0AD2_GPIOM2"),
	PINCTRL_PIN(95, "ESPI0D3_L0AD3_GPIOM3"),
	PINCTRL_PIN(96, "ESPI0CK_L0CLK_OSCCLK_GPIOM4"),
	PINCTRL_PIN(97, "ESPI0CSN_L0FRAMEN_GPIOM5"),
	PINCTRL_PIN(98, "ESPI0ALTN_L0SIRQN_GPIOM6"),
	PINCTRL_PIN(99, "ESPI0RSTN_LPC0RSTN_GPIOM7"),
	PINCTRL_PIN(100, "SPI0CK_GPION0"),
	PINCTRL_PIN(101, "SPI0MOSI_GPION1"),
	PINCTRL_PIN(102, "SPI0MISO_GPION2"),
	PINCTRL_PIN(103, "GPION3_SPI0DQ2"),
	PINCTRL_PIN(104, "GPION4_SPI0DQ3"),
	PINCTRL_PIN(105, "GPION5_SPI0CS1"),
	PINCTRL_PIN(106, "GPION6_SPI0ABR_TXD8"),
	PINCTRL_PIN(107, "GPION7_SPI0WPN_RXD8"),
	PINCTRL_PIN(108, "SPI1CK_TXD9_GPIOO0"),
	PINCTRL_PIN(109, "SPI1MOSI_RXD9_GPIOO1"),
	PINCTRL_PIN(110, "SPI1MISO_TXD10_GPIOO2"),
	PINCTRL_PIN(111, "GPIOO3_SPI1DQ2_RXD10"),
	PINCTRL_PIN(112, "GPIOO4_SPI1DQ3_TXD11"),
	PINCTRL_PIN(113, "GPIOO5_SPI1CS1_RXD11"),
	PINCTRL_PIN(114, "GPIOO6_SPI1ABR_THRUIN2"),
	PINCTRL_PIN(115, "GPIOO7_SPI1WPN_THRUOUT2"),
	PINCTRL_PIN(116, "GPIOP0_SPI2CS0N_SDA13_I3CSDA7"),
	PINCTRL_PIN(117, "GPIOP1_SPI2CK_SCL13_I3CSCL7"),
	PINCTRL_PIN(118, "GPIOP2_SPI2MOSI_SCL14_I3CSCL10"),
	PINCTRL_PIN(119, "GPIOP3_SPI2MISO_SDA14_I3CSDA10"),
	PINCTRL_PIN(120, "GPIOP4_SPI2DQ2_SCL15_I3CSCL11_THRUIN3"),
	PINCTRL_PIN(121, "GPIOP5_SPI2DQ3_SDA15_I3CSDA11_THRUOUT3"),
	PINCTRL_PIN(122, "GPIOP6_SPI2CS1N"),
	PINCTRL_PIN(123, "GPIOP7_FWSPIABR"),
	PINCTRL_PIN(124, "GPIOQ0_MDIO2_PE2SGRSTN"),
	PINCTRL_PIN(125, "GPIOQ1_MDC2"),
	PINCTRL_PIN(126, "GPIOQ2_MNTRST13v3"),
	PINCTRL_PIN(127, "GPIOQ3_MTCK13v3"),
	PINCTRL_PIN(128, "GPIOQ4_MTMS13v3"),
	PINCTRL_PIN(129, "GPIOQ5_MTDI13v3"),
	PINCTRL_PIN(130, "GPIOQ6_MTDO13v3"),
	PINCTRL_PIN(131, "GPIOQ7_FWSPIWPN"),
	PINCTRL_PIN(132, "GPIOR0_RGMII0RXCK_RMII0RCLKI"),
	PINCTRL_PIN(133, "GPIOR1_RGMII0RXCTL"),
	PINCTRL_PIN(134, "GPIOR2_RGMII0RXD0_RMII0RXD0"),
	PINCTRL_PIN(135, "GPIOR3_RGMII0RXD1_RMII0RXD1"),
	PINCTRL_PIN(136, "GPIOR4_RGMII0RXD2_RMII0CRSDV"),
	PINCTRL_PIN(137, "GPIOR5_RGMII0RXD3_RMII0RXER"),
	PINCTRL_PIN(138, "GPIOR6_RGMII0TXCK_RMII0RCLKO"),
	PINCTRL_PIN(139, "GPIOR7_RGMII0TXCTL_RMII0TXEN"),
	PINCTRL_PIN(140, "GPIOS0_RGMII0TXD0_RMII0TXD0"),
	PINCTRL_PIN(141, "GPIOS1_RGMII0TXD1_RMII0TXD1"),
	PINCTRL_PIN(142, "GPIOS2_RGMII0TXD2"),
	PINCTRL_PIN(143, "GPIOS3_RGMII0TXD3"),
	PINCTRL_PIN(144, "GPIOS4_MDC0"),
	PINCTRL_PIN(145, "GPIOS5_MDIO0"),
	PINCTRL_PIN(146, "VGAHS_GPIOS6"),
	PINCTRL_PIN(147, "VGAVS_GPIOS7"),
	PINCTRL_PIN(148, "GPIOT0_RGMII1RXCK_RMII1RCLKI_SCL8_SGPM1CK"),
	PINCTRL_PIN(149, "GPIOT1_RGMII1RXCTL_SGPSLD"),
	PINCTRL_PIN(150, "GPIOT2_RGMII1RXD0_RMII1RXD0_SCL9_TXD3"),
	PINCTRL_PIN(151, "GPIOT3_RGMII1RXD1_RMII1RXD1_SDA9_RXD3"),
	PINCTRL_PIN(152, "GPIOT4_RGMII1RXD2_RMII1CRSDV_SDA8_SGPM1LD"),
	PINCTRL_PIN(153, "GPIOT5_RGMII1RXD3_RMII1RXER"),
	PINCTRL_PIN(154, "GPIOT6_RGMII1TXCK_RMII1RCLKO_SGPSCK"),
	PINCTRL_PIN(155, "GPIOT7_RGMII1TXCTL_RMII1TXEN"),
	PINCTRL_PIN(156, "GPIOU0_RGMII1TXD0_RMII1TXD0_SCL10_SGPM1O"),
	PINCTRL_PIN(157, "GPIOU1_RGMII1TXD1_RMII1TXD1_SDA10_SGPM1I"),
	PINCTRL_PIN(158, "GPIOU2_RGMII1TXD2_SGPSMO"),
	PINCTRL_PIN(159, "GPIOU3_RGMII1TXD3_SGPSMI"),
	PINCTRL_PIN(160, "GPIOU4_MDC1"),
	PINCTRL_PIN(161, "GPIOU5_MDIO1"),
	PINCTRL_PIN(162, "FWSPIDQ2_GPIOU6"),
	PINCTRL_PIN(163, "FWSPIDQ3_GPIOU7"),
	PINCTRL_PIN(164, "GPIOV0_SCL0"),
	PINCTRL_PIN(165, "GPIOV1_SDA0"),
	PINCTRL_PIN(166, "GPIOV2_SCL1"),
	PINCTRL_PIN(167, "GPIOV3_SDA1"),
	PINCTRL_PIN(168, "GPIOV4_SCL2"),
	PINCTRL_PIN(169, "GPIOV5_SDA2"),
	PINCTRL_PIN(170, "GPIOV6_SCL3"),
	PINCTRL_PIN(171, "GPIOV7_SDA3"),
	PINCTRL_PIN(172, "GPIOW0_SCL4_ESPI1CK_I2CF1SCLI"),
	PINCTRL_PIN(173, "GPIOW1_SDA4_ESPI1CSN_I2CF1SDAI"),
	PINCTRL_PIN(174, "GPIOW2_SCL5_ESPI1RSTN_I2CF1SCLO"),
	PINCTRL_PIN(175, "GPIOW3_SDA5_ESPI1D0_I2CF1SDAO"),
	PINCTRL_PIN(176, "GPIOW4_SCL6_ESPI1D1_I2CF2SCLI"),
	PINCTRL_PIN(177, "GPIOW5_SDA6_ESPI1D2_I2CF2SDAI"),
	PINCTRL_PIN(178, "GPIOW6_SCL7_ESPI1D3_I2CF2SCLO"),
	PINCTRL_PIN(179, "GPIOW7_SDA7_ESPI1ALTN_I2CF2SDAO"),
	PINCTRL_PIN(180, "GPIOX0_SCL8_I2CF0SCLI"),
	PINCTRL_PIN(181, "GPIOX1_SDA8_I2CF0SDAI"),
	PINCTRL_PIN(182, "GPIOX2_SCL9_I2CF0SCLO"),
	PINCTRL_PIN(183, "GPIOX3_SDA9_CANBUSSTBY_I2CF0SDAO"),
	PINCTRL_PIN(184, "GPIOX4_SCL10_CANBUSTX"),
	PINCTRL_PIN(185, "GPIOX5_SDA10_CANBUSRX"),
	PINCTRL_PIN(186, "GPIOX6_SCL11_USBUARTP"),
	PINCTRL_PIN(187, "GPIOX7_SDA11_USBUARTN"),
	PINCTRL_PIN(188, "ADC0_GPIY0_SALT4"),
	PINCTRL_PIN(189, "ADC1_GPIY1_SALT5"),
	PINCTRL_PIN(190, "ADC2_GPIY2_SALT6"),
	PINCTRL_PIN(191, "ADC3_GPIY3_SALT7"),
	PINCTRL_PIN(192, "ADC4_GPIY4_SALT8"),
	PINCTRL_PIN(193, "ADC5_GPIY5_SALT9"),
	PINCTRL_PIN(194, "ADC6_GPIY6_SALT10"),
	PINCTRL_PIN(195, "ADC7_GPIY7_SALT11"),
	PINCTRL_PIN(196, "ADC8_GPIZ0"),
	PINCTRL_PIN(197, "ADC9_GPIZ1"),
	PINCTRL_PIN(198, "ADC10_GPIZ2"),
	PINCTRL_PIN(199, "ADC11_GPIZ3"),
	PINCTRL_PIN(200, "ADC12_GPIZ4"),
	PINCTRL_PIN(201, "ADC13_GPIZ5_AUXPWRGOOD0"),
	PINCTRL_PIN(202, "ADC14_GPIZ6_AUXPWRGOOD1"),
	PINCTRL_PIN(203, "ADC15_GPIZ7"),
	PINCTRL_PIN(204, "GPIOAA0_SGPM0CK_SFF8485CKA_NCTS2_MACLINK0"),
	PINCTRL_PIN(205, "GPIOAA1_SGPM0LD_SFF8485LDA_NDCD2_MACLINK2"),
	PINCTRL_PIN(206, "HBLEDN_SGPM0LDR_GPIOAA2"),
	PINCTRL_PIN(207, "GPIOAA3_SGPM0O_SFF8485I0A_NDSR2"),
	PINCTRL_PIN(208, "GPIOAA4_SGPM0I_SFF8485I1A_NRI2"),
	PINCTRL_PIN(209, "GPIOAA5_SGPM1CK_WDTRST4N_NDTR2_SFF8485CKB"),
	PINCTRL_PIN(210, "GPIOAA6_SGPM1LD_WDTRST5N_NRTS2_SFF8485LDB"),
	PINCTRL_PIN(211, "GPIOAA7_SGPM1LD_R_WDTRST6N_MACLINK1"),
};

struct aspeed_g7_soc1_funcfg {
	char *name;
	u32 reg;
	u32 mask;
	u32 val;
};

#define PIN_CFG(cfg_name, cfg_reg, cfg_mask, cfg_val) \
	{ .name = #cfg_name, .reg = cfg_reg, .mask = cfg_mask, .val = cfg_val }

struct aspeed_g7_soc1_pincfg {
	struct aspeed_g7_soc1_funcfg *funcfg;
};

static const struct aspeed_g7_soc1_pincfg pin_cfg[] = {
//GPIO A
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(ESPI1, SCU00, GENMASK(2, 0), 1),
			PIN_CFG(LPC1, SCU00, GENMASK(2, 0), 2),
			PIN_CFG(SD0, SCU00, GENMASK(2, 0), 3),
			PIN_CFG(HVI3C4, SCU00, GENMASK(2, 0), 4),
			PIN_CFG(VPI, SCU00, GENMASK(2, 0), 5),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(ESPI1, SCU00, GENMASK(6, 4), (1 << 4)),
			PIN_CFG(LPC1, SCU00, GENMASK(6, 4), (2 << 4)),
			PIN_CFG(SD0, SCU00, GENMASK(6, 4), (3 << 4)),
			PIN_CFG(HVI3C4, SCU00, GENMASK(6, 4), (4 << 4)),
			PIN_CFG(VPI, SCU00, GENMASK(6, 4), (5 << 4)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(ESPI1, SCU00, GENMASK(10, 8), (1 << 8)),
			PIN_CFG(LPC1, SCU00, GENMASK(10, 8), (2 << 8)),
			PIN_CFG(SD0, SCU00, GENMASK(10, 8), (3 << 8)),
			PIN_CFG(HVI3C5, SCU00, GENMASK(10, 8), (4 << 8)),
			PIN_CFG(VPI, SCU00, GENMASK(10, 8), (5 << 8)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(ESPI1, SCU00, GENMASK(14, 12), (1 << 12)),
			PIN_CFG(LPC1, SCU00, GENMASK(14, 12), (2 << 12)),
			PIN_CFG(SD0, SCU00, GENMASK(14, 12), (3 << 12)),
			PIN_CFG(HVI3C5, SCU00, GENMASK(14, 12), (4 << 12)),
			PIN_CFG(VPI, SCU00, GENMASK(14, 12), (5 << 12)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(ESPI1, SCU00, GENMASK(18, 16), (1 << 16)),
			PIN_CFG(LPC1, SCU00, GENMASK(18, 16), (2 << 16)),
			PIN_CFG(SD0, SCU00, GENMASK(18, 16), (3 << 16)),
			PIN_CFG(HVI3C6, SCU00, GENMASK(18, 16), (4 << 16)),
			PIN_CFG(VPI, SCU00, GENMASK(18, 16), (5 << 16)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(ESPI1, SCU00, GENMASK(22, 20), (1 << 20)),
			PIN_CFG(LPC1, SCU00, GENMASK(22, 20), (2 << 20)),
			PIN_CFG(SD0, SCU00, GENMASK(22, 20), (3 << 20)),
			PIN_CFG(HVI3C6, SCU00, GENMASK(22, 20), (4 << 20)),
			PIN_CFG(VPI, SCU00, GENMASK(22, 20), (5 << 20)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(ESPI1, SCU00, GENMASK(26, 24), (1 << 24)),
			PIN_CFG(LPC1, SCU00, GENMASK(26, 24), (2 << 24)),
			PIN_CFG(SD0, SCU00, GENMASK(26, 24), (3 << 24)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(ESPI1, SCU00, GENMASK(30, 28), (1 << 28)),
			PIN_CFG(LPC1, SCU00, GENMASK(30, 28), (2 << 28)),
			PIN_CFG(SD0, SCU00, GENMASK(30, 28), (3 << 28)),
		},
	},
//GPIO B
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(TACH0, SCU04, GENMASK(2, 0), 1),
			PIN_CFG(THRU0, SCU04, GENMASK(2, 0), 2),
			PIN_CFG(VPI, SCU04, GENMASK(2, 0), 3),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(TACH1, SCU04, GENMASK(6, 4), (1 << 4)),
			PIN_CFG(THRU0, SCU04, GENMASK(6, 4), (2 << 4)),
			PIN_CFG(VPI, SCU04, GENMASK(6, 4), (3 << 4)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(TACH2, SCU04, GENMASK(10, 8), (1 << 8)),
			PIN_CFG(THRU1, SCU04, GENMASK(10, 8), (2 << 8)),
			PIN_CFG(VPI, SCU04, GENMASK(10, 8), (3 << 8)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(TACH3, SCU04, GENMASK(14, 12), (1 << 12)),
			PIN_CFG(THRU1, SCU04, GENMASK(14, 12), (2 << 12)),
			PIN_CFG(VPI, SCU04, GENMASK(14, 12), (3 << 12)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(TACH4, SCU04, GENMASK(18, 16), (1 << 16)),
			PIN_CFG(VPI, SCU04, GENMASK(18, 16), (3 << 16)),
			PIN_CFG(NCTS5, SCU04, GENMASK(18, 16), (4 << 16)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(TACH5, SCU04, GENMASK(22, 20), (1 << 20)),
			PIN_CFG(VPI, SCU04, GENMASK(22, 20), (3 << 20)),
			PIN_CFG(NDCD5, SCU04, GENMASK(22, 20), (4 << 20)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(TACH6, SCU04, GENMASK(26, 24), (1 << 24)),
			PIN_CFG(VPI, SCU04, GENMASK(26, 24), (3 << 24)),
			PIN_CFG(NDSR5, SCU04, GENMASK(26, 24), (4 << 24)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(TACH7, SCU04, GENMASK(30, 28), (1 << 28)),
			PIN_CFG(VPI, SCU04, GENMASK(30, 28), (3 << 28)),
			PIN_CFG(NRI5, SCU04, GENMASK(30, 28), (4 << 28)),
		},
	},
//GPIO C
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(TACH8, SCU08, GENMASK(2, 0), 1),
			PIN_CFG(VPI, SCU08, GENMASK(2, 0), 3),
			PIN_CFG(NDTR5, SCU08, GENMASK(2, 0), 4),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(TACH9, SCU08, GENMASK(6, 4), (1 << 4)),
			PIN_CFG(VPI, SCU08, GENMASK(6, 4), (3 << 4)),
			PIN_CFG(NRTS5, SCU08, GENMASK(6, 4), (4 << 4)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(TACH10, SCU08, GENMASK(10, 8), (1 << 8)),
			PIN_CFG(SALT12, SCU08, GENMASK(10, 8), (2 << 8)),
			PIN_CFG(VPI, SCU08, GENMASK(10, 8), (3 << 8)),
			PIN_CFG(NCTS6, SCU08, GENMASK(10, 8), (4 << 8)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(TACH11, SCU08, GENMASK(14, 12), (1 << 12)),
			PIN_CFG(SALT13, SCU08, GENMASK(14, 12), (2 << 12)),
			PIN_CFG(VPI, SCU08, GENMASK(14, 12), (3 << 12)),
			PIN_CFG(NDCD6, SCU08, GENMASK(14, 12), (4 << 12)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(TACH12, SCU08, GENMASK(18, 16), (1 << 16)),
			PIN_CFG(SALT14, SCU08, GENMASK(18, 16), (2 << 16)),
			PIN_CFG(VPI, SCU08, GENMASK(18, 16), (3 << 16)),
			PIN_CFG(NDSR6, SCU08, GENMASK(18, 16), (4 << 16)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(TACH13, SCU08, GENMASK(22, 20), (1 << 20)),
			PIN_CFG(SALT15, SCU08, GENMASK(22, 20), (2 << 20)),
			PIN_CFG(VPI, SCU08, GENMASK(22, 20), (3 << 20)),
			PIN_CFG(NRI6, SCU08, GENMASK(22, 20), (4 << 20)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(TACH14, SCU08, GENMASK(26, 24), (1 << 24)),
			PIN_CFG(LPCPME0, SCU08, GENMASK(26, 24), (2 << 24)),
			PIN_CFG(VPI, SCU08, GENMASK(26, 24), (3 << 24)),
			PIN_CFG(NDTR6, SCU08, GENMASK(26, 24), (4 << 24)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(TACH15, SCU08, GENMASK(30, 28), (1 << 28)),
			PIN_CFG(LPCSMIN0, SCU08, GENMASK(30, 28), (2 << 28)),
			PIN_CFG(VPI, SCU08, GENMASK(30, 28), (3 << 28)),
			PIN_CFG(NRTS6, SCU08, GENMASK(30, 28), (4 << 28)),
			PIN_CFG(SPIM0, SCU08, GENMASK(30, 28), (5 << 28)),
		},
	},
//GPIO D
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(PWM0, SCU0C, GENMASK(2, 0), 1),
			PIN_CFG(SIOPBON0, SCU0C, GENMASK(2, 0), 2),
			PIN_CFG(VPI, SCU0C, GENMASK(2, 0), 3),
			PIN_CFG(SPIM0, SCU0C, GENMASK(2, 0), 5),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(PWM1, SCU0C, GENMASK(6, 4), (1 << 4)),
			PIN_CFG(SIOPBIN0, SCU0C, GENMASK(6, 4), (2 << 4)),
			PIN_CFG(VPI, SCU0C, GENMASK(6, 4), (3 << 4)),
			PIN_CFG(SPIM0, SCU0C, GENMASK(6, 4), (5 << 4)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(PWM2, SCU0C, GENMASK(10, 8), (1 << 8)),
			PIN_CFG(SIOSCIN0, SCU0C, GENMASK(10, 8), (2 << 8)),
			PIN_CFG(VPI, SCU0C, GENMASK(10, 8), (3 << 8)),
			PIN_CFG(SPIM0, SCU0C, GENMASK(10, 8), (5 << 8)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(PWM3, SCU0C, GENMASK(14, 12), (1 << 12)),
			PIN_CFG(SIOS3N0, SCU0C, GENMASK(14, 12), (2 << 12)),
			PIN_CFG(VPI, SCU0C, GENMASK(14, 12), (3 << 12)),
			PIN_CFG(SPIM0, SCU0C, GENMASK(14, 12), (5 << 12)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(PWM4, SCU0C, GENMASK(18, 16), (1 << 16)),
			PIN_CFG(SIOS5N0, SCU0C, GENMASK(18, 16), (2 << 16)),
			PIN_CFG(VPI, SCU0C, GENMASK(18, 16), (3 << 16)),
			PIN_CFG(SPIM0, SCU0C, GENMASK(18, 16), (5 << 16)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(PWM5, SCU0C, GENMASK(22, 20), (1 << 20)),
			PIN_CFG(SIOPWREQN0, SCU0C, GENMASK(22, 20), (2 << 20)),
			PIN_CFG(VPI, SCU0C, GENMASK(22, 20), (3 << 20)),
			PIN_CFG(SPIM0, SCU0C, GENMASK(22, 20), (5 << 20)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(PWM6, SCU0C, GENMASK(26, 24), (1 << 24)),
			PIN_CFG(SIOONCTRLN0, SCU0C, GENMASK(26, 24), (2 << 24)),
			PIN_CFG(SPIM0, SCU0C, GENMASK(26, 24), (5 << 24)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(PWM7, SCU0C, GENMASK(30, 28), (1 << 28)),
			PIN_CFG(SPIM0, SCU0C, GENMASK(30, 28), (2 << 28)),
		},
	},
// GPIO E
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(NCTS0, SCU10, GENMASK(2, 0), 1),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(NDCD0, SCU10, GENMASK(6, 4), (1 << 4)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(NDSR0, SCU10, GENMASK(10, 8), (1 << 8)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(NRI0, SCU10, GENMASK(14, 12), (1 << 12)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(NDTR0, SCU10, GENMASK(18, 16), (1 << 16)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(NRTS0, SCU10, GENMASK(22, 20), (1 << 20)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(UART0, SCU10, GENMASK(26, 24), (1 << 24)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(UART0, SCU10, GENMASK(30, 28), (1 << 28)),
		},
	},
// GPIO F
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(NCTS1, SCU14, GENMASK(2, 0), 1),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(NDCD1, SCU14, GENMASK(6, 4), (1 << 4)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(NDSR1, SCU14, GENMASK(10, 8), (1 << 8)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(NRI1, SCU14, GENMASK(14, 12), (1 << 12)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(NDTR1, SCU14, GENMASK(18, 16), (1 << 16)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(NRTS1, SCU14, GENMASK(22, 20), (1 << 20)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(UART1, SCU14, GENMASK(26, 24), (1 << 24)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(UART1, SCU14, GENMASK(30, 28), (1 << 28)),
		},
	},
//GPIO G
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(UART2, SCU18, GENMASK(2, 0), 1),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(UART2, SCU18, GENMASK(6, 4), (1 << 4)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(UART3, SCU18, GENMASK(10, 8), (1 << 8)),
			PIN_CFG(WDTRST0N, SCU18, GENMASK(10, 8), (2 << 8)),
			PIN_CFG(PWM8, SCU18, GENMASK(10, 8), (3 << 8)),
			PIN_CFG(SPIM1, SCU18, GENMASK(10, 8), (5 << 8)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(UART3, SCU18, GENMASK(14, 12), (1 << 12)),
			PIN_CFG(WDTRST1N, SCU18, GENMASK(14, 12), (2 << 12)),
			PIN_CFG(PWM9, SCU18, GENMASK(14, 12), (3 << 12)),
			PIN_CFG(SPIM1, SCU18, GENMASK(14, 12), (5 << 12)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(UART5, SCU18, GENMASK(18, 16), (1 << 16)),
			PIN_CFG(WDTRST2N, SCU18, GENMASK(18, 16), (2 << 16)),
			PIN_CFG(PWM10, SCU18, GENMASK(18, 16), (3 << 16)),
			PIN_CFG(SPIM1, SCU18, GENMASK(18, 16), (5 << 16)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(UART5, SCU18, GENMASK(22, 20), (1 << 20)),
			PIN_CFG(WDTRST3N, SCU18, GENMASK(22, 20), (2 << 20)),
			PIN_CFG(PWM11, SCU18, GENMASK(22, 20), (3 << 20)),
			PIN_CFG(SPIM1, SCU18, GENMASK(22, 20), (5 << 20)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(UART6, SCU18, GENMASK(26, 24), (1 << 24)),
			PIN_CFG(SALT0, SCU18, GENMASK(26, 24), (2 << 24)),
			PIN_CFG(PWM12, SCU18, GENMASK(26, 24), (3 << 24)),
			PIN_CFG(SPIM1, SCU18, GENMASK(26, 24), (5 << 24)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(UART6, SCU18, GENMASK(30, 28), (1 << 28)),
			PIN_CFG(SALT1, SCU18, GENMASK(30, 28), (2 << 28)),
			PIN_CFG(PWM13, SCU18, GENMASK(30, 28), (3 << 28)),
			PIN_CFG(SPIM1, SCU18, GENMASK(30, 28), (5 << 28)),
		},
	},
//GPIO H
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(UART7, SCU1C, GENMASK(2, 0), 1),
			PIN_CFG(SALT2, SCU1C, GENMASK(2, 0), 2),
			PIN_CFG(PWM14, SCU1C, GENMASK(2, 0), 3),
			PIN_CFG(SPIM1, SCU1C, GENMASK(2, 0), 5),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(UART7, SCU1C, GENMASK(6, 4), (1 << 4)),
			PIN_CFG(SALT3, SCU1C, GENMASK(6, 4), (2 << 4)),
			PIN_CFG(PWM15, SCU1C, GENMASK(6, 4), (3 << 4)),
			PIN_CFG(SPIM1, SCU1C, GENMASK(6, 4), (5 << 4)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(SGPM1, SCU1C, GENMASK(10, 8), (1 << 8)),
			PIN_CFG(WDTRST7N, SCU1C, GENMASK(10, 8), (2 << 8)),
			PIN_CFG(PESGWAKEN, SCU1C, GENMASK(10, 8), (3 << 8)),
			PIN_CFG(SMON1, SCU1C, GENMASK(10, 8), (5 << 8)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(SGPM1, SCU1C, GENMASK(14, 12), (1 << 12)),
			PIN_CFG(SMON1, SCU1C, GENMASK(14, 12), (5 << 12)),
		},
	},
//GPIO I
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(HVI3C0, SCU20, GENMASK(2, 0), 1),
			PIN_CFG(I2C12, SCU20, GENMASK(2, 0), 2),
			PIN_CFG(SIOPBON1, SCU20, GENMASK(2, 0), 3),
			PIN_CFG(SPIM2, SCU20, GENMASK(2, 0), 5),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(HVI3C0, SCU20, GENMASK(6, 4), (1 << 4)),
			PIN_CFG(I2C12, SCU20, GENMASK(6, 4), (2 << 4)),
			PIN_CFG(SIOPBIN1, SCU20, GENMASK(6, 4), (3 << 4)),
			PIN_CFG(SPIM2, SCU20, GENMASK(6, 4), (5 << 4)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(HVI3C1, SCU20, GENMASK(10, 8), (1 << 8)),
			PIN_CFG(I2C13, SCU20, GENMASK(10, 8), (2 << 8)),
			PIN_CFG(SIOSCIN1, SCU20, GENMASK(10, 8), (3 << 8)),
			PIN_CFG(SPIM2, SCU20, GENMASK(10, 8), (5 << 8)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(HVI3C1, SCU20, GENMASK(14, 12), (1 << 12)),
			PIN_CFG(I2C13, SCU20, GENMASK(14, 12), (2 << 12)),
			PIN_CFG(SIOS3N1, SCU20, GENMASK(14, 12), (3 << 12)),
			PIN_CFG(SPIM2, SCU20, GENMASK(14, 12), (5 << 12)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(HVI3C2, SCU20, GENMASK(18, 16), (1 << 16)),
			PIN_CFG(I2C14, SCU20, GENMASK(18, 16), (2 << 16)),
			PIN_CFG(SIOS5N1, SCU20, GENMASK(18, 16), (3 << 16)),
			PIN_CFG(SPIM2, SCU20, GENMASK(18, 16), (5 << 16)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(HVI3C2, SCU20, GENMASK(22, 20), (1 << 20)),
			PIN_CFG(I2C14, SCU20, GENMASK(22, 20), (2 << 20)),
			PIN_CFG(SIOPWREQN1, SCU20, GENMASK(22, 20), (3 << 20)),
			PIN_CFG(SPIM2, SCU20, GENMASK(22, 20), (5 << 20)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(HVI3C3, SCU20, GENMASK(26, 24), (1 << 24)),
			PIN_CFG(I2C15, SCU20, GENMASK(26, 24), (2 << 24)),
			PIN_CFG(SIOONCTRLN1, SCU20, GENMASK(26, 24), (3 << 24)),
			PIN_CFG(SPIM2, SCU20, GENMASK(26, 24), (5 << 24)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(HVI3C3, SCU20, GENMASK(30, 28), (1 << 28)),
			PIN_CFG(I2C15, SCU20, GENMASK(30, 28), (2 << 28)),
			PIN_CFG(SIOPWRGD1, SCU20, GENMASK(30, 28), (3 << 28)),
			PIN_CFG(SPIM2, SCU20, GENMASK(30, 28), (5 << 28)),
		},
	},
//GPIO J
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(SPI0CK, SCU24, GENMASK(2, 0), 1),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(SPI0MOSI, SCU24, GENMASK(6, 4), (1 << 4)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(SPI0MISO, SCU24, GENMASK(10, 8), (1 << 8)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(SPI0DQ2, SCU24, GENMASK(14, 12), (1 << 12)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(SPI0DQ3, SCU24, GENMASK(18, 16), (1 << 16)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(SPI0CS1, SCU24, GENMASK(22, 20), (1 << 20)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(SPI0ABR, SCU24, GENMASK(26, 24), (1 << 24)),
			PIN_CFG(TXD8, SCU24, GENMASK(26, 24), (3 << 24)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(SPI0WPN, SCU24, GENMASK(30, 28), (1 << 28)),
			PIN_CFG(RXD8, SCU24, GENMASK(30, 28), (3 << 28)),
		},
	},
//GPIO K
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(I3C8, SCU28, GENMASK(2, 0), 1),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(I3C8, SCU28, GENMASK(6, 4), (1 << 4)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(I3C9, SCU28, GENMASK(10, 8), (1 << 8)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(I3C9, SCU28, GENMASK(14, 12), (1 << 12)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(I3C10, SCU28, GENMASK(18, 16), (1 << 16)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(I3C10, SCU28, GENMASK(22, 20), (1 << 20)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(I3C11, SCU28, GENMASK(26, 24), (1 << 24)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(I3C11, SCU28, GENMASK(30, 28), (1 << 28)),
		},
	},
//GPIO L
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(I3C0, SCU2C, GENMASK(2, 0), 1),
			PIN_CFG(FSI0, SCU2C, GENMASK(2, 0), 2),
			PIN_CFG(LTPI, SCU2C, GENMASK(2, 0), 3),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(I3C0, SCU2C, GENMASK(6, 4), (1 << 4)),
			PIN_CFG(FSI0, SCU2C, GENMASK(6, 4), (2 << 4)),
			PIN_CFG(LTPI, SCU2C, GENMASK(6, 4), (3 << 4)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(I3C1, SCU2C, GENMASK(10, 8), (1 << 8)),
			PIN_CFG(FSI1, SCU2C, GENMASK(10, 8), (2 << 8)),
			PIN_CFG(LTPI, SCU2C, GENMASK(10, 8), (3 << 8)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(I3C1, SCU2C, GENMASK(14, 12), (1 << 12)),
			PIN_CFG(FSI1, SCU2C, GENMASK(14, 12), (2 << 12)),
			PIN_CFG(LTPI, SCU2C, GENMASK(14, 12), (3 << 12)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(I3C2, SCU2C, GENMASK(18, 16), (1 << 16)),
			PIN_CFG(FSI2, SCU2C, GENMASK(18, 16), (2 << 16)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(I3C2, SCU2C, GENMASK(22, 20), (1 << 20)),
			PIN_CFG(FSI2, SCU2C, GENMASK(22, 20), (2 << 20)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(I3C3, SCU2C, GENMASK(26, 24), (1 << 24)),
			PIN_CFG(FSI3, SCU2C, GENMASK(26, 24), (2 << 24)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(I3C3, SCU2C, GENMASK(30, 28), (1 << 28)),
			PIN_CFG(FSI3, SCU2C, GENMASK(30, 28), (2 << 28)),
		},
	},
//GPIO M
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(ESPI0, SCU30, GENMASK(2, 0), 1),
			PIN_CFG(LPC0, SCU30, GENMASK(2, 0), 2),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(ESPI0, SCU30, GENMASK(6, 4), (1 << 4)),
			PIN_CFG(LPC0, SCU30, GENMASK(6, 4), (2 << 4)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(ESPI0, SCU30, GENMASK(10, 8), (1 << 8)),
			PIN_CFG(LPC0, SCU30, GENMASK(10, 8), (2 << 8)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(ESPI0, SCU30, GENMASK(14, 12), (1 << 12)),
			PIN_CFG(LPC0, SCU30, GENMASK(14, 12), (2 << 12)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(ESPI0, SCU30, GENMASK(18, 16), (1 << 16)),
			PIN_CFG(LPC0, SCU30, GENMASK(18, 16), (2 << 16)),
			PIN_CFG(OSCCLK, SCU30, GENMASK(18, 16), (3 << 16)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(ESPI0, SCU30, GENMASK(22, 20), (1 << 20)),
			PIN_CFG(LPC0, SCU30, GENMASK(22, 20), (2 << 20)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(ESPI0, SCU30, GENMASK(26, 24), (1 << 24)),
			PIN_CFG(LPC0, SCU30, GENMASK(26, 24), (2 << 24)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(ESPI0, SCU30, GENMASK(30, 28), (1 << 28)),
			PIN_CFG(LPC0, SCU30, GENMASK(30, 28), (2 << 28)),
		},
	},
//GPIO N
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(SPI0, SCU34, GENMASK(2, 0), 1),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(SPI0, SCU34, GENMASK(6, 4), (1 << 4)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(SPI0, SCU34, GENMASK(10, 8), (1 << 8)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(QSPI0, SCU34, GENMASK(14, 12), (1 << 12)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(QSPI0, SCU34, GENMASK(18, 16), (1 << 16)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(SPI0CS1, SCU34, GENMASK(22, 20), (1 << 20)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(SPI0ABR, SCU34, GENMASK(26, 24), (1 << 24)),
			PIN_CFG(TXD8, SCU34, GENMASK(26, 24), (3 << 24)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(SPI0WPN, SCU34, GENMASK(30, 28), (1 << 28)),
			PIN_CFG(RXD8, SCU34, GENMASK(30, 28), (3 << 28)),
		},
	},
//GPIO O
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(SPI1, SCU38, GENMASK(2, 0), 1),
			PIN_CFG(TXD9, SCU38, GENMASK(2, 0), 2),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(SPI1, SCU38, GENMASK(6, 4), (1 << 4)),
			PIN_CFG(RXD9, SCU38, GENMASK(6, 4), (2 << 4)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(SPI1, SCU38, GENMASK(10, 8), (1 << 8)),
			PIN_CFG(TXD10, SCU38, GENMASK(10, 8), (2 << 8)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(QSPI1, SCU38, GENMASK(14, 12), (1 << 12)),
			PIN_CFG(RXD10, SCU38, GENMASK(14, 12), (2 << 12)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(QSPI1, SCU38, GENMASK(18, 16), (1 << 16)),
			PIN_CFG(TXD11, SCU38, GENMASK(18, 16), (2 << 16)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(SPI1CS1, SCU38, GENMASK(22, 20), (1 << 20)),
			PIN_CFG(RXD11, SCU38, GENMASK(22, 20), (2 << 20)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(SPI1ABR, SCU38, GENMASK(26, 24), (1 << 24)),
			PIN_CFG(THRU2, SCU38, GENMASK(26, 24), (4 << 24)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(SPI1WPN, SCU38, GENMASK(30, 28), (1 << 28)),
			PIN_CFG(THRU2, SCU38, GENMASK(30, 28), (4 << 28)),
		},
	},
//GPIO P
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(SPI2, SCU3C, GENMASK(2, 0), 1),
			PIN_CFG(DI2C13, SCU3C, GENMASK(2, 0), 2),
			PIN_CFG(HVI3C7, SCU3C, GENMASK(2, 0), 3),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(SPI2, SCU3C, GENMASK(6, 4), (1 << 4)),
			PIN_CFG(DI2C13, SCU3C, GENMASK(6, 4), (2 << 4)),
			PIN_CFG(HVI3C7, SCU3C, GENMASK(6, 4), (3 << 4)),
			PIN_CFG(EM_SPICK, SCU3C, GENMASK(6, 4), (5 << 4)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(SPI2, SCU3C, GENMASK(10, 8), (1 << 8)),
			PIN_CFG(DI2C14, SCU3C, GENMASK(10, 8), (2 << 8)),
			PIN_CFG(HVI3C10, SCU3C, GENMASK(10, 8), (3 << 8)),
			PIN_CFG(EM_SPIMOSI, SCU3C, GENMASK(10, 8), (5 << 8)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(SPI2, SCU3C, GENMASK(14, 12), (1 << 12)),
			PIN_CFG(DI2C14, SCU3C, GENMASK(14, 12), (2 << 12)),
			PIN_CFG(HVI3C10, SCU3C, GENMASK(14, 12), (3 << 12)),
			PIN_CFG(EM_SPIMISO, SCU3C, GENMASK(14, 12), (5 << 12)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(QSPI2, SCU3C, GENMASK(18, 16), (1 << 16)),
			PIN_CFG(DI2C15, SCU3C, GENMASK(18, 16), (2 << 16)),
			PIN_CFG(HVI3C11, SCU3C, GENMASK(18, 16), (3 << 16)),
			PIN_CFG(THRU3, SCU3C, GENMASK(18, 16), (4 << 16)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(QSPI2, SCU3C, GENMASK(22, 20), (1 << 20)),
			PIN_CFG(DI2C15, SCU3C, GENMASK(22, 20), (2 << 20)),
			PIN_CFG(HVI3C11, SCU3C, GENMASK(22, 20), (3 << 20)),
			PIN_CFG(THRU3, SCU3C, GENMASK(22, 20), (4 << 20)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(SPI2CS1, SCU3C, GENMASK(26, 24), (1 << 24)),
			PIN_CFG(EM_SPICSN, SCU3C, GENMASK(26, 24), (5 << 24)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(FWSPIABR, SCU3C, GENMASK(30, 28), (1 << 28)),
		},
	},
//GPIO Q
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(MDIO2, SCU40, GENMASK(2, 0), 1),
			PIN_CFG(PE2SGRSTN, SCU40, GENMASK(2, 0), 2),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(MDIO2, SCU40, GENMASK(6, 4), (1 << 4)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(MNTRST1, SCU40, GENMASK(10, 8), (1 << 8)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(MTCK1, SCU40, GENMASK(14, 12), (1 << 12)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(MTMS1, SCU40, GENMASK(18, 16), (1 << 16)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(MTDI1, SCU40, GENMASK(22, 20), (1 << 20)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(MTDO1, SCU40, GENMASK(26, 24), (1 << 24)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(FWSPIWPEN, SCU40, GENMASK(30, 28), (1 << 28)),
		},
	},
//GPIO R
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(RGMII0, SCU44, GENMASK(2, 0), 1),
			PIN_CFG(RMII0R, SCU44, GENMASK(2, 0), 2),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(RGMII0, SCU44, GENMASK(6, 4), (1 << 4)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(RGMII0, SCU44, GENMASK(10, 8), (1 << 8)),
			PIN_CFG(RMII0R, SCU44, GENMASK(10, 8), (2 << 8)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(RGMII0, SCU44, GENMASK(14, 12), (1 << 12)),
			PIN_CFG(RMII0R, SCU44, GENMASK(14, 12), (2 << 12)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(RGMII0, SCU44, GENMASK(18, 16), (1 << 16)),
			PIN_CFG(RMII0C, SCU44, GENMASK(18, 16), (2 << 16)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(RGMII0, SCU44, GENMASK(22, 20), (1 << 20)),
			PIN_CFG(RMII0, SCU44, GENMASK(22, 20), (2 << 20)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(RGMII0, SCU44, GENMASK(26, 24), (1 << 24)),
			PIN_CFG(RMII0, SCU44, GENMASK(26, 24), (2 << 24)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(RGMII0, SCU44, GENMASK(30, 28), (1 << 28)),
			PIN_CFG(RMII0, SCU44, GENMASK(30, 28), (2 << 28)),
		},
	},
//GPIO S
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(RGMII0, SCU48, GENMASK(2, 0), 1),
			PIN_CFG(RMII0, SCU48, GENMASK(2, 0), 2),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(RGMII0, SCU48, GENMASK(6, 4), (1 << 4)),
			PIN_CFG(RMII0, SCU48, GENMASK(6, 4), (2 << 4)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(RGMII0, SCU48, GENMASK(10, 8), (1 << 8)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(RGMII0, SCU48, GENMASK(14, 12), (1 << 12)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(MDIO0, SCU48, GENMASK(18, 16), (1 << 16)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(MDIO0, SCU48, GENMASK(22, 20), (1 << 20)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(VGA, SCU48, GENMASK(26, 24), (1 << 24)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(VGA, SCU48, GENMASK(30, 28), (1 << 28)),
		},
	},
//GPIO T
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(RGMII1, SCU4C, GENMASK(2, 0), 1),
			PIN_CFG(RMII1, SCU4C, GENMASK(2, 0), 2),
			PIN_CFG(DI2C8, SCU4C, GENMASK(2, 0), 3),
			PIN_CFG(DSGPM1, SCU4C, GENMASK(2, 0), 4),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(RGMII1, SCU4C, GENMASK(6, 4), (1 << 4)),
			PIN_CFG(SGPS, SCU4C, GENMASK(6, 4), (5 << 4)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(RGMII1, SCU4C, GENMASK(10, 8), (1 << 8)),
			PIN_CFG(RMII1, SCU4C, GENMASK(10, 8), (2 << 8)),
			PIN_CFG(DI2C9, SCU4C, GENMASK(10, 8), (3 << 8)),
			PIN_CFG(TXD3, SCU4C, GENMASK(10, 8), (4 << 8)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(RGMII1, SCU4C, GENMASK(14, 12), (1 << 12)),
			PIN_CFG(RMII1, SCU4C, GENMASK(14, 12), (2 << 12)),
			PIN_CFG(DI2C9, SCU4C, GENMASK(14, 12), (3 << 12)),
			PIN_CFG(RXD3, SCU4C, GENMASK(14, 12), (4 << 12)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(RGMII1, SCU4C, GENMASK(18, 16), (1 << 16)),
			PIN_CFG(RMII1, SCU4C, GENMASK(18, 16), (2 << 16)),
			PIN_CFG(DI2C8, SCU4C, GENMASK(18, 16), (3 << 16)),
			PIN_CFG(DSGPM1, SCU4C, GENMASK(18, 16), (4 << 16)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(RGMII1, SCU4C, GENMASK(22, 20), (1 << 20)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(RGMII1, SCU4C, GENMASK(26, 24), (1 << 24)),
			PIN_CFG(RMII1, SCU4C, GENMASK(26, 24), (2 << 24)),
			PIN_CFG(SGPS, SCU4C, GENMASK(26, 24), (5 << 24)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(RGMII1, SCU4C, GENMASK(30, 28), (1 << 28)),
			PIN_CFG(RMII1, SCU4C, GENMASK(30, 28), (2 << 28)),
		},
	},
//GPIO U
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(RGMII1, SCU50, GENMASK(2, 0), 1),
			PIN_CFG(RMII1, SCU50, GENMASK(2, 0), 2),
			PIN_CFG(DI2C10, SCU50, GENMASK(2, 0), 3),
			PIN_CFG(DSGPM1, SCU50, GENMASK(2, 0), 4),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(RGMII1, SCU50, GENMASK(6, 4), (1 << 4)),
			PIN_CFG(RMII1, SCU50, GENMASK(6, 4), (2 << 4)),
			PIN_CFG(DI2C10, SCU50, GENMASK(6, 4), (3 << 4)),
			PIN_CFG(DSGPM1, SCU50, GENMASK(6, 4), (4 << 4)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(RGMII1, SCU50, GENMASK(10, 8), (1 << 8)),
			PIN_CFG(SGPS, SCU50, GENMASK(10, 8), (5 << 8)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(RGMII1, SCU50, GENMASK(14, 12), (1 << 12)),
			PIN_CFG(SGPS, SCU50, GENMASK(14, 12), (5 << 12)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(MDIO1, SCU50, GENMASK(18, 16), (1 << 16)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(MDIO1, SCU50, GENMASK(22, 20), (1 << 20)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(FWQSPI, SCU50, GENMASK(26, 24), (1 << 24)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(FWQSPI, SCU50, GENMASK(30, 28), (1 << 28)),
		},
	},
//GPIO V
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(I2C0, SCU54, GENMASK(2, 0), 1),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(I2C0, SCU54, GENMASK(6, 4), (1 << 4)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(I2C1, SCU54, GENMASK(10, 8), (1 << 8)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(I2C1, SCU54, GENMASK(14, 12), (1 << 12)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(I2C2, SCU54, GENMASK(18, 16), (1 << 16)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(I2C2, SCU54, GENMASK(22, 20), (1 << 20)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(I2C3, SCU54, GENMASK(26, 24), (1 << 24)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(I2C3, SCU54, GENMASK(30, 28), (1 << 28)),
		},
	},
//GPIO W
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(I2C4, SCU58, GENMASK(2, 0), 1),
			PIN_CFG(ESPI1, SCU58, GENMASK(2, 0), 2),
			PIN_CFG(I2CF1, SCU58, GENMASK(2, 0), 5),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(I2C4, SCU58, GENMASK(6, 4), (1 << 4)),
			PIN_CFG(ESPI1, SCU58, GENMASK(6, 4), (2 << 4)),
			PIN_CFG(I2CF1, SCU58, GENMASK(6, 4), (5 << 4)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(I2C5, SCU58, GENMASK(10, 8), (1 << 8)),
			PIN_CFG(ESPI1, SCU58, GENMASK(10, 8), (2 << 8)),
			PIN_CFG(I2CF1, SCU58, GENMASK(10, 8), (5 << 8)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(I2C5, SCU58, GENMASK(14, 12), (1 << 12)),
			PIN_CFG(ESPI1, SCU58, GENMASK(14, 12), (2 << 12)),
			PIN_CFG(I2CF1, SCU58, GENMASK(14, 12), (5 << 12)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(I2C6, SCU58, GENMASK(18, 16), (1 << 16)),
			PIN_CFG(ESPI1, SCU58, GENMASK(18, 16), (2 << 16)),
			PIN_CFG(I2CF2, SCU58, GENMASK(18, 16), (5 << 16)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(I2C6, SCU58, GENMASK(22, 20), (1 << 20)),
			PIN_CFG(ESPI1, SCU58, GENMASK(22, 20), (2 << 20)),
			PIN_CFG(I2CF2, SCU58, GENMASK(22, 20), (5 << 20)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(I2C7, SCU58, GENMASK(26, 24), (1 << 24)),
			PIN_CFG(ESPI1, SCU58, GENMASK(26, 24), (2 << 24)),
			PIN_CFG(I2CF2, SCU58, GENMASK(26, 24), (5 << 24)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(I2C7, SCU58, GENMASK(30, 28), (1 << 28)),
			PIN_CFG(ESPI1, SCU58, GENMASK(30, 28), (2 << 28)),
			PIN_CFG(I2CF2, SCU58, GENMASK(30, 28), (5 << 28)),
		},
	},
//GPIO X
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(I2C8, SCU5C, GENMASK(2, 0), 1),
			PIN_CFG(I2CF0, SCU5C, GENMASK(2, 0), 5),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(I2C8, SCU5C, GENMASK(6, 4), (1 << 4)),
			PIN_CFG(I2CF0, SCU5C, GENMASK(6, 4), (5 << 4)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(I2C9, SCU5C, GENMASK(10, 8), (1 << 8)),
			PIN_CFG(I2CF0, SCU5C, GENMASK(10, 8), (5 << 8)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(I2C9, SCU5C, GENMASK(14, 12), (1 << 12)),
			PIN_CFG(CANBUS, SCU5C, GENMASK(14, 12), (2 << 12)),
			PIN_CFG(I2CF0, SCU5C, GENMASK(14, 12), (5 << 12)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(I2C10, SCU5C, GENMASK(18, 16), (1 << 16)),
			PIN_CFG(CANBUS, SCU5C, GENMASK(18, 16), (2 << 16)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(I2C10, SCU5C, GENMASK(22, 20), (1 << 20)),
			PIN_CFG(CANBUS, SCU5C, GENMASK(22, 20), (2 << 20)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(I2C11, SCU5C, GENMASK(26, 24), (1 << 24)),
			PIN_CFG(USBUART, SCU5C, GENMASK(26, 24), (2 << 24)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(I2C11, SCU5C, GENMASK(30, 28), (1 << 28)),
			PIN_CFG(USBUART, SCU5C, GENMASK(30, 28), (2 << 28)),
		},
	},
//GPIO Y
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(ADC0, SCU60, GENMASK(2, 0), 0),
			PIN_CFG(GPIY0, SCU60, GENMASK(2, 0), 1),
			PIN_CFG(SALT4, SCU60, GENMASK(2, 0), 2),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(ADC1, SCU60, GENMASK(6, 4), 0),
			PIN_CFG(GPIY1, SCU60, GENMASK(6, 4), (1 << 4)),
			PIN_CFG(SALT5, SCU60, GENMASK(6, 4), (2 << 4)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(ADC2, SCU60, GENMASK(10, 8), 0),
			PIN_CFG(GPIY2, SCU60, GENMASK(10, 8), (1 << 8)),
			PIN_CFG(SALT6, SCU60, GENMASK(10, 8), (2 << 8)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(ADC3, SCU60, GENMASK(14, 12), 0),
			PIN_CFG(GPIY3, SCU60, GENMASK(14, 12), (1 << 12)),
			PIN_CFG(SALT7, SCU60, GENMASK(14, 12), (2 << 12)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(ADC4, SCU60, GENMASK(18, 16), 0),
			PIN_CFG(GPIY4, SCU60, GENMASK(18, 16), (1 << 16)),
			PIN_CFG(SALT8, SCU60, GENMASK(18, 16), (2 << 16)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(ADC5, SCU60, GENMASK(22, 20), 0),
			PIN_CFG(GPIY5, SCU60, GENMASK(22, 20), (1 << 20)),
			PIN_CFG(SALT9, SCU60, GENMASK(22, 20), (2 << 20)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(ADC6, SCU60, GENMASK(26, 24), 0),
			PIN_CFG(GPIY6, SCU60, GENMASK(26, 24), (1 << 24)),
			PIN_CFG(SALT10, SCU60, GENMASK(26, 24), (2 << 24)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(ADC7, SCU60, GENMASK(30, 28), 0),
			PIN_CFG(GPIY7, SCU60, GENMASK(30, 28), (1 << 28)),
			PIN_CFG(SALT11, SCU60, GENMASK(30, 28), (2 << 28)),
		},
	},
//GPIO Z
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(ADC15, SCU64, GENMASK(2, 0), 0),
			PIN_CFG(GPIZ7, SCU64, GENMASK(2, 0), 1),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(ADC14, SCU64, GENMASK(6, 4), 0),
			PIN_CFG(GPIZ6, SCU64, GENMASK(6, 4), (1 << 4)),
			PIN_CFG(AUXPWRGOOD1, SCU64, GENMASK(6, 4), (2 << 4)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(ADC13, SCU64, GENMASK(10, 8), 0),
			PIN_CFG(GPIZ5, SCU64, GENMASK(10, 8), (1 << 8)),
			PIN_CFG(AUXPWRGOOD0, SCU64, GENMASK(10, 8), (2 << 8)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(ADC12, SCU64, GENMASK(14, 12), 0),
			PIN_CFG(GPIZ4, SCU64, GENMASK(14, 12), (1 << 12)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(ADC11, SCU64, GENMASK(18, 16), 0),
			PIN_CFG(GPIZ3, SCU64, GENMASK(18, 16), (1 << 16)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(ADC10, SCU64, GENMASK(22, 20), 0),
			PIN_CFG(GPIZ2, SCU64, GENMASK(22, 20), (1 << 20)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(ADC9, SCU64, GENMASK(26, 24), 0),
			PIN_CFG(GPIZ1, SCU64, GENMASK(26, 24), (1 << 24)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(ADC8, SCU64, GENMASK(30, 28), 0),
			PIN_CFG(GPIZ0, SCU64, GENMASK(30, 28), (1 << 28)),
		},
	},
//GPIO AA
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(SGPM0, SCU68, GENMASK(2, 0), 1),
			PIN_CFG(SMON0, SCU68, GENMASK(2, 0), 2),
			PIN_CFG(NCTS2, SCU68, GENMASK(2, 0), 3),
			PIN_CFG(MACLINK0, SCU68, GENMASK(2, 0), 4),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(SGPM0, SCU68, GENMASK(6, 4), (1 << 4)),
			PIN_CFG(SMON0, SCU68, GENMASK(6, 4), (2 << 4)),
			PIN_CFG(NDCD2, SCU68, GENMASK(6, 4), (3 << 4)),
			PIN_CFG(MACLINK2, SCU68, GENMASK(6, 4), (4 << 4)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(SGPM0LD_R, SCU68, GENMASK(10, 8), (2 << 8)),
			PIN_CFG(HBLED, SCU68, GENMASK(10, 8), (2 << 8)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(SGPM0, SCU68, GENMASK(14, 12), (1 << 12)),
			PIN_CFG(SMON0, SCU68, GENMASK(14, 12), (2 << 12)),
			PIN_CFG(NDSR2, SCU68, GENMASK(14, 12), (3 << 12)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(SGPM0, SCU68, GENMASK(18, 16), (1 << 16)),
			PIN_CFG(SMON0, SCU68, GENMASK(18, 16), (2 << 16)),
			PIN_CFG(NRI2, SCU68, GENMASK(18, 16), (3 << 16)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(SGPM1, SCU68, GENMASK(22, 20), (1 << 20)),
			PIN_CFG(WDTRST4N, SCU68, GENMASK(22, 20), (2 << 20)),
			PIN_CFG(NDTR2, SCU68, GENMASK(22, 20), (3 << 20)),
			PIN_CFG(SMON1, SCU68, GENMASK(22, 20), (4 << 20)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(SGPM1, SCU68, GENMASK(26, 24), (1 << 24)),
			PIN_CFG(WDTRST5N, SCU68, GENMASK(26, 24), (2 << 24)),
			PIN_CFG(NRTS2, SCU68, GENMASK(26, 24), (3 << 24)),
			PIN_CFG(SMON1, SCU68, GENMASK(26, 24), (4 << 24)),
		},
	},
	{
		.funcfg = (struct aspeed_g7_soc1_funcfg[]) {
			PIN_CFG(SGPM1LD_R, SCU68, GENMASK(30, 28), (1 << 28)),
			PIN_CFG(WDTRST6N, SCU68, GENMASK(30, 28), (2 << 28)),
			PIN_CFG(MACLINK1, SCU68, GENMASK(30, 28), (3 << 28)),
		},
	},
};

/* pinctrl_ops */
static int aspeed_g7_soc1_get_groups_count(struct pinctrl_dev *pctldev)
{
	return ARRAY_SIZE(aspeed_g7_soc1_pingroups);
}

static const char *aspeed_g7_soc1_get_group_name(struct pinctrl_dev *pctldev,
						 unsigned int selector)
{
	return aspeed_g7_soc1_pingroups[selector].name;
}

static int aspeed_g7_soc1_get_group_pins(struct pinctrl_dev *pctldev,
					 unsigned int selector,
					 const unsigned int **pins,
					 unsigned int *npins)
{
	*npins = aspeed_g7_soc1_pingroups[selector].npins;
	*pins = aspeed_g7_soc1_pingroups[selector].pins;

	return 0;
}

static int aspeed_g7_soc1_dt_node_to_map(struct pinctrl_dev *pctldev,
					 struct device_node *np_config,
					 struct pinctrl_map **map, u32 *num_maps)
{
	return pinconf_generic_dt_node_to_map(pctldev, np_config, map, num_maps,
					      PIN_MAP_TYPE_INVALID);
}

static void aspeed_g7_soc1_dt_free_map(struct pinctrl_dev *pctldev,
				       struct pinctrl_map *map, u32 num_maps)
{
	kfree(map);
}

static const struct pinctrl_ops aspeed_g7_soc1_pinctrl_ops = {
	.get_groups_count = aspeed_g7_soc1_get_groups_count,
	.get_group_name = aspeed_g7_soc1_get_group_name,
	.get_group_pins = aspeed_g7_soc1_get_group_pins,
	.dt_node_to_map = aspeed_g7_soc1_dt_node_to_map,
	.dt_free_map = aspeed_g7_soc1_dt_free_map,
};

static int aspeed_g7_soc1_get_functions_count(struct pinctrl_dev *pctldev)
{
	return ARRAY_SIZE(aspeed_g7_soc1_funcs);
}

static const char *aspeed_g7_soc1_get_function_name(struct pinctrl_dev *pctldev,
						    unsigned int function)
{
	return aspeed_g7_soc1_funcs[function].name;
}

static int aspeed_g7_soc1_get_function_groups(struct pinctrl_dev *pctldev,
					      unsigned int function,
					      const char *const **groups,
					      unsigned int *const ngroups)
{
	*ngroups = aspeed_g7_soc1_funcs[function].ngroups;
	*groups = aspeed_g7_soc1_funcs[function].groups;

	return 0;
}

static int aspeed_g7_soc1_pinmux_set_mux(struct pinctrl_dev *pctldev,
					 unsigned int function,
					 unsigned int group)
{
	int i;
	int pin;
	const struct aspeed_g7_soc1_pincfg *cfg;
	const struct aspeed_g7_soc1_funcfg *funcfg;
	const char *target_fn_name = aspeed_g7_soc1_funcs[function].name;
	struct aspeed_g7_soc1_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctldev);
	struct aspeed_g7_soc1_pingroup *pingroup = &aspeed_g7_soc1_pingroups[group];

	for (i = 0; i < pingroup->npins; i++) {
		pin = pingroup->pins[i];
		cfg = &pin_cfg[pin];
		funcfg = &cfg->funcfg[0];
		while (funcfg->name) {
			if (strcmp(funcfg->name, target_fn_name) == 0) {
				writel((readl(pinctrl->regs + funcfg->reg) &
					~funcfg->mask) | funcfg->val,
					   pinctrl->regs + funcfg->reg);
				break;
			}
			funcfg++;
		}

		if (!funcfg->name)
			return 0;
	}

	return 0;
}

static int aspeed_g7_soc1_gpio_request_enable(struct pinctrl_dev *pctldev,
					      struct pinctrl_gpio_range *range,
					      unsigned int offset)
{
	struct aspeed_g7_soc1_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctldev);
	const struct aspeed_g7_soc1_pincfg *cfg = &pin_cfg[offset];
	const struct aspeed_g7_soc1_funcfg *funcfg;

	if (!cfg) {
		funcfg = &cfg->funcfg[0];
		while (funcfg->name) {
			writel((readl(pinctrl->regs + funcfg->reg) & ~funcfg->mask),
			       pinctrl->regs + funcfg->reg);
			funcfg++;
		}
	}
	return 0;
}

static void aspeed_g7_soc1_gpio_request_free(struct pinctrl_dev *pctldev,
					     struct pinctrl_gpio_range *range,
					     unsigned int offset)
{
	struct aspeed_g7_soc1_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctldev);
	int virq;

	virq = irq_find_mapping(pinctrl->domain, offset);
	if (virq)
		irq_dispose_mapping(virq);
}

static const struct pinmux_ops aspeed_g7_soc1_pinmux_ops = {
	.get_functions_count = aspeed_g7_soc1_get_functions_count,
	.get_function_name = aspeed_g7_soc1_get_function_name,
	.get_function_groups = aspeed_g7_soc1_get_function_groups,
	.set_mux = aspeed_g7_soc1_pinmux_set_mux,
	.gpio_request_enable = aspeed_g7_soc1_gpio_request_enable,
	.gpio_disable_free = aspeed_g7_soc1_gpio_request_free,
};

static int aspeed_g7_soc1_config_get(struct pinctrl_dev *pctldev,
				     unsigned int pin, unsigned long *config)
{
//	struct aspeed_g7_soc1_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param = pinconf_to_config_param(*config);
	int rc = 0;

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
	case PIN_CONFIG_BIAS_PULL_UP:
	case PIN_CONFIG_BIAS_PULL_DOWN:
		break;
	case PIN_CONFIG_OUTPUT:
	case PIN_CONFIG_INPUT_ENABLE:
		break;
	case PIN_CONFIG_DRIVE_PUSH_PULL:
		break;
	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		break;
	case PIN_CONFIG_INPUT_DEBOUNCE:
		break;
	case PIN_CONFIG_DRIVE_STRENGTH:
		break;
	case PIN_CONFIG_SLEW_RATE:
		break;
	default:
		return -ENOTSUPP;
	}

	if (!rc)
		return -EINVAL;

	return 0;
}

static int aspeed_g7_soc1_config_set_one(struct aspeed_g7_soc1_pinctrl *pinctrl,
					 unsigned int pin, unsigned long config)
{
	enum pin_config_param param = pinconf_to_config_param(config);

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		break;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
		break;
	case PIN_CONFIG_INPUT_ENABLE:
		break;
	case PIN_CONFIG_OUTPUT:
		break;
	case PIN_CONFIG_DRIVE_PUSH_PULL:

		break;
	case PIN_CONFIG_DRIVE_OPEN_DRAIN:

		break;
	case PIN_CONFIG_INPUT_DEBOUNCE:
		break;
	case PIN_CONFIG_SLEW_RATE:

		break;
	case PIN_CONFIG_DRIVE_STRENGTH:

		break;
	default:
		return -ENOTSUPP;
	}

	return 0;
}

static int aspeed_g7_soc1_config_set(struct pinctrl_dev *pctldev,
				     unsigned int pin, unsigned long *configs,
				     unsigned int num_configs)
{
	struct aspeed_g7_soc1_pinctrl *pinctrl =
		pinctrl_dev_get_drvdata(pctldev);
	int rc;

	while (num_configs--) {
		rc = aspeed_g7_soc1_config_set_one(pinctrl, pin, *configs++);
		if (rc)
			return rc;
	}

	return 0;
}

static const struct pinconf_ops aspeed_g7_soc1_pinconf_ops = {
	.is_generic = true,
	.pin_config_get = aspeed_g7_soc1_config_get,
	.pin_config_set = aspeed_g7_soc1_config_set,
};

/* pinctrl_desc */
static struct pinctrl_desc aspeed_g7_soc1_pinctrl_desc = {
	.name = "aspeed-g7-soc1-pinctrl",
	.pins = aspeed_g7_soc1_pins,
	.npins = ARRAY_SIZE(aspeed_g7_soc1_pins),
	.pctlops = &aspeed_g7_soc1_pinctrl_ops,
	.pmxops = &aspeed_g7_soc1_pinmux_ops,
	.confops = &aspeed_g7_soc1_pinconf_ops,
	.owner = THIS_MODULE,
};

static int aspeed_g7_soc1_pinctrl_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct aspeed_g7_soc1_pinctrl *pctrl;

	pctrl = devm_kzalloc(dev, sizeof(*pctrl), GFP_KERNEL);
	if (!pctrl)
		return -ENOMEM;

	pctrl->dev = dev;
	platform_set_drvdata(pdev, pctrl);

	pctrl->regs = devm_platform_ioremap_resource(pdev, 0);
	pctrl->pctldev =
		devm_pinctrl_register(dev, &aspeed_g7_soc1_pinctrl_desc, pctrl);
	if (IS_ERR(pctrl->pctldev))
		return dev_err_probe(dev, PTR_ERR(pctrl->pctldev),
				     "Failed to register pinctrl device\n");

	return 0;
}

static const struct of_device_id aspeed_g7_soc1_pinctrl_match[] = {
	{ .compatible = "aspeed,ast2700-soc1-pinctrl" },
	{}
};
MODULE_DEVICE_TABLE(of, aspeed_g7_soc1_pinctrl_match);

static struct platform_driver aspeed_g7_soc1_pinctrl_driver = {
	.probe = aspeed_g7_soc1_pinctrl_probe,
	.driver = {
		.name = "aspeed-g7-soc1-pinctrl",
		.of_match_table = aspeed_g7_soc1_pinctrl_match,
		.suppress_bind_attrs = true,
	},
};

static int __init aspeed_g7_soc1_pinctrl_register(void)
{
	return platform_driver_register(&aspeed_g7_soc1_pinctrl_driver);
}
arch_initcall(aspeed_g7_soc1_pinctrl_register);

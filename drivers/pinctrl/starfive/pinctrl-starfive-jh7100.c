// SPDX-License-Identifier: GPL-2.0
/*
 * Pinctrl / GPIO driver for StarFive JH7100 SoC
 *
 * Copyright (C) 2020 Shanghai StarFive Technology Co., Ltd.
 * Copyright (C) 2021 Emil Renner Berthing <kernel@esmil.dk>
 */

#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/gpio/driver.h>
#include <linux/io.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>

#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>

#include <dt-bindings/pinctrl/pinctrl-starfive-jh7100.h>

#include "../core.h"
#include "../pinctrl-utils.h"
#include "../pinmux.h"
#include "../pinconf.h"

#define DRIVER_NAME "pinctrl-starfive"

/*
 * Refer to Section 12. GPIO Registers in the JH7100 data sheet:
 * https://github.com/starfive-tech/JH7100_Docs
 */
#define NR_GPIOS	64

/*
 * Global enable for GPIO interrupts. If bit 0 is set to 1 the GPIO interrupts
 * are enabled. If set to 0 the GPIO interrupts are disabled.
 */
#define GPIOEN		0x000

/*
 * The following 32-bit registers come in pairs, but only the offset of the
 * first register is defined. The first controls (interrupts for) GPIO 0-31 and
 * the second GPIO 32-63.
 */

/*
 * Interrupt Type. If set to 1 the interrupt is edge-triggered. If set to 0 the
 * interrupt is level-triggered.
 */
#define GPIOIS		0x010

/*
 * Edge-Trigger Interrupt Type.  If set to 1 the interrupt gets triggered on
 * both positive and negative edges. If set to 0 the interrupt is triggered by a
 * single edge.
 */
#define GPIOIBE		0x018

/*
 * Interrupt Trigger Polarity. If set to 1 the interrupt is triggered on a
 * rising edge (edge-triggered) or high level (level-triggered). If set to 0 the
 * interrupt is triggered on a falling edge (edge-triggered) or low level
 * (level-triggered).
 */
#define GPIOIEV		0x020

/*
 * Interrupt Mask. If set to 1 the interrupt is enabled (unmasked). If set to 0
 * the interrupt is disabled (masked). Note that the current documentation is
 * wrong and says the exct opposite of this.
 */
#define GPIOIE		0x028

/*
 * Clear Edge-Triggered Interrupts. Write a 1 to clear the edge-triggered
 * interrupt.
 */
#define GPIOIC		0x030

/*
 * Edge-Triggered Interrupt Status. A 1 means the configured edge was detected.
 */
#define GPIORIS		0x038

/*
 * Interrupt Status after Masking. A 1 means the configured edge or level was
 * detected and not masked.
 */
#define GPIOMIS		0x040

/*
 * Data Value. Dynamically reflects the value of the GPIO pin. If 1 the pin is
 * a digital 1 and if 0 the pin is a digital 0.
 */
#define GPIODIN		0x048

/*
 * From the data sheet section 12.2, there are 64 32-bit output data registers
 * and 64 output enable registers. Output data and output enable registers for
 * a given GPIO are contiguous. Eg. GPO0_DOUT_CFG is 0x50 and GPO0_DOEN_CFG is
 * 0x54 while GPO1_DOUT_CFG is 0x58 and GPO1_DOEN_CFG is 0x5c.  The stride
 * between GPIO registers is effectively 8, thus: GPOn_DOUT_CFG is 0x50 + 8n
 * and GPOn_DOEN_CFG is 0x54 + 8n.
 */
#define GPON_DOUT_CFG	0x050
#define GPON_DOEN_CFG	0x054

/*
 * From Section 12.3, there are 75 input signal configuration registers which
 * are 4 bytes wide starting with GPI_CPU_JTAG_TCK_CFG at 0x250 and ending with
 * GPI_USB_OVER_CURRENT_CFG 0x378
 */
#define GPI_CFG_OFFSET	0x250

/*
 * Pad Control Bits. There are 16 pad control bits for each pin located in 103
 * 32-bit registers controlling PAD_GPIO[0] to PAD_GPIO[63] followed by
 * PAD_FUNC_SHARE[0] to PAD_FUNC_SHARE[141]. Odd numbered pins use the upper 16
 * bit of each register.
 */
#define PAD_SLEW_RATE_MASK		GENMASK(11, 9)
#define PAD_SLEW_RATE_POS		9
#define PAD_BIAS_STRONG_PULL_UP		BIT(8)
#define PAD_INPUT_ENABLE		BIT(7)
#define PAD_INPUT_SCHMITT_ENABLE	BIT(6)
#define PAD_BIAS_DISABLE		BIT(5)
#define PAD_BIAS_PULL_DOWN		BIT(4)
#define PAD_BIAS_MASK \
	(PAD_BIAS_STRONG_PULL_UP | \
	 PAD_BIAS_DISABLE | \
	 PAD_BIAS_PULL_DOWN)
#define PAD_DRIVE_STRENGTH_MASK		GENMASK(3, 0)
#define PAD_DRIVE_STRENGTH_POS		0

/*
 * From Section 11, the IO_PADSHARE_SEL register can be programmed to select
 * one of seven pre-defined multiplexed signal groups on PAD_FUNC_SHARE and
 * PAD_GPIO pads. This is a global setting.
 */
#define IO_PADSHARE_SEL			0x1a0

/*
 * This just needs to be some number such that when
 * sfp->gpio.pin_base = PAD_INVALID_GPIO then
 * starfive_pin_to_gpio(sfp, validpin) is never a valid GPIO number.
 * That is it should underflow and return something >= NR_GPIOS.
 */
#define PAD_INVALID_GPIO		0x10000

/*
 * The packed pinmux values from the device tree look like this:
 *
 *  | 31 - 24 | 23 - 16 | 15 - 8 |     7    |     6    |  5 - 0  |
 *  |  dout   |  doen   |  din   | dout rev | doen rev | gpio nr |
 *
 * ..but the GPOn_DOUT_CFG and GPOn_DOEN_CFG registers look like this:
 *
 *  |      31       | 30 - 8 |   7 - 0   |
 *  | dout/doen rev | unused | dout/doen |
 */
static unsigned int starfive_pinmux_to_gpio(u32 v)
{
	return v & (NR_GPIOS - 1);
}

static u32 starfive_pinmux_to_dout(u32 v)
{
	return ((v & BIT(7)) << (31 - 7)) | ((v >> 24) & GENMASK(7, 0));
}

static u32 starfive_pinmux_to_doen(u32 v)
{
	return ((v & BIT(6)) << (31 - 6)) | ((v >> 16) & GENMASK(7, 0));
}

static u32 starfive_pinmux_to_din(u32 v)
{
	return (v >> 8) & GENMASK(7, 0);
}

/*
 * The maximum GPIO output current depends on the chosen drive strength:
 *
 *  DS:   0     1     2     3     4     5     6     7
 *  mA:  14.2  21.2  28.2  35.2  42.2  49.1  56.0  62.8
 *
 * After rounding that is 7*DS + 14 mA
 */
static u32 starfive_drive_strength_to_max_mA(u16 ds)
{
	return 7 * ds + 14;
}

static u16 starfive_drive_strength_from_max_mA(u32 i)
{
	return (clamp(i, 14U, 63U) - 14) / 7;
}

struct starfive_pinctrl {
	struct gpio_chip gc;
	struct pinctrl_gpio_range gpios;
	raw_spinlock_t lock;
	void __iomem *base;
	void __iomem *padctl;
	struct pinctrl_dev *pctl;
	struct mutex mutex; /* serialize adding groups and functions */
};

static inline unsigned int starfive_pin_to_gpio(const struct starfive_pinctrl *sfp,
						unsigned int pin)
{
	return pin - sfp->gpios.pin_base;
}

static inline unsigned int starfive_gpio_to_pin(const struct starfive_pinctrl *sfp,
						unsigned int gpio)
{
	return sfp->gpios.pin_base + gpio;
}

static struct starfive_pinctrl *starfive_from_irq_data(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);

	return container_of(gc, struct starfive_pinctrl, gc);
}

static struct starfive_pinctrl *starfive_from_irq_desc(struct irq_desc *desc)
{
	struct gpio_chip *gc = irq_desc_get_handler_data(desc);

	return container_of(gc, struct starfive_pinctrl, gc);
}

static const struct pinctrl_pin_desc starfive_pins[] = {
	PINCTRL_PIN(PAD_GPIO(0), "GPIO[0]"),
	PINCTRL_PIN(PAD_GPIO(1), "GPIO[1]"),
	PINCTRL_PIN(PAD_GPIO(2), "GPIO[2]"),
	PINCTRL_PIN(PAD_GPIO(3), "GPIO[3]"),
	PINCTRL_PIN(PAD_GPIO(4), "GPIO[4]"),
	PINCTRL_PIN(PAD_GPIO(5), "GPIO[5]"),
	PINCTRL_PIN(PAD_GPIO(6), "GPIO[6]"),
	PINCTRL_PIN(PAD_GPIO(7), "GPIO[7]"),
	PINCTRL_PIN(PAD_GPIO(8), "GPIO[8]"),
	PINCTRL_PIN(PAD_GPIO(9), "GPIO[9]"),
	PINCTRL_PIN(PAD_GPIO(10), "GPIO[10]"),
	PINCTRL_PIN(PAD_GPIO(11), "GPIO[11]"),
	PINCTRL_PIN(PAD_GPIO(12), "GPIO[12]"),
	PINCTRL_PIN(PAD_GPIO(13), "GPIO[13]"),
	PINCTRL_PIN(PAD_GPIO(14), "GPIO[14]"),
	PINCTRL_PIN(PAD_GPIO(15), "GPIO[15]"),
	PINCTRL_PIN(PAD_GPIO(16), "GPIO[16]"),
	PINCTRL_PIN(PAD_GPIO(17), "GPIO[17]"),
	PINCTRL_PIN(PAD_GPIO(18), "GPIO[18]"),
	PINCTRL_PIN(PAD_GPIO(19), "GPIO[19]"),
	PINCTRL_PIN(PAD_GPIO(20), "GPIO[20]"),
	PINCTRL_PIN(PAD_GPIO(21), "GPIO[21]"),
	PINCTRL_PIN(PAD_GPIO(22), "GPIO[22]"),
	PINCTRL_PIN(PAD_GPIO(23), "GPIO[23]"),
	PINCTRL_PIN(PAD_GPIO(24), "GPIO[24]"),
	PINCTRL_PIN(PAD_GPIO(25), "GPIO[25]"),
	PINCTRL_PIN(PAD_GPIO(26), "GPIO[26]"),
	PINCTRL_PIN(PAD_GPIO(27), "GPIO[27]"),
	PINCTRL_PIN(PAD_GPIO(28), "GPIO[28]"),
	PINCTRL_PIN(PAD_GPIO(29), "GPIO[29]"),
	PINCTRL_PIN(PAD_GPIO(30), "GPIO[30]"),
	PINCTRL_PIN(PAD_GPIO(31), "GPIO[31]"),
	PINCTRL_PIN(PAD_GPIO(32), "GPIO[32]"),
	PINCTRL_PIN(PAD_GPIO(33), "GPIO[33]"),
	PINCTRL_PIN(PAD_GPIO(34), "GPIO[34]"),
	PINCTRL_PIN(PAD_GPIO(35), "GPIO[35]"),
	PINCTRL_PIN(PAD_GPIO(36), "GPIO[36]"),
	PINCTRL_PIN(PAD_GPIO(37), "GPIO[37]"),
	PINCTRL_PIN(PAD_GPIO(38), "GPIO[38]"),
	PINCTRL_PIN(PAD_GPIO(39), "GPIO[39]"),
	PINCTRL_PIN(PAD_GPIO(40), "GPIO[40]"),
	PINCTRL_PIN(PAD_GPIO(41), "GPIO[41]"),
	PINCTRL_PIN(PAD_GPIO(42), "GPIO[42]"),
	PINCTRL_PIN(PAD_GPIO(43), "GPIO[43]"),
	PINCTRL_PIN(PAD_GPIO(44), "GPIO[44]"),
	PINCTRL_PIN(PAD_GPIO(45), "GPIO[45]"),
	PINCTRL_PIN(PAD_GPIO(46), "GPIO[46]"),
	PINCTRL_PIN(PAD_GPIO(47), "GPIO[47]"),
	PINCTRL_PIN(PAD_GPIO(48), "GPIO[48]"),
	PINCTRL_PIN(PAD_GPIO(49), "GPIO[49]"),
	PINCTRL_PIN(PAD_GPIO(50), "GPIO[50]"),
	PINCTRL_PIN(PAD_GPIO(51), "GPIO[51]"),
	PINCTRL_PIN(PAD_GPIO(52), "GPIO[52]"),
	PINCTRL_PIN(PAD_GPIO(53), "GPIO[53]"),
	PINCTRL_PIN(PAD_GPIO(54), "GPIO[54]"),
	PINCTRL_PIN(PAD_GPIO(55), "GPIO[55]"),
	PINCTRL_PIN(PAD_GPIO(56), "GPIO[56]"),
	PINCTRL_PIN(PAD_GPIO(57), "GPIO[57]"),
	PINCTRL_PIN(PAD_GPIO(58), "GPIO[58]"),
	PINCTRL_PIN(PAD_GPIO(59), "GPIO[59]"),
	PINCTRL_PIN(PAD_GPIO(60), "GPIO[60]"),
	PINCTRL_PIN(PAD_GPIO(61), "GPIO[61]"),
	PINCTRL_PIN(PAD_GPIO(62), "GPIO[62]"),
	PINCTRL_PIN(PAD_GPIO(63), "GPIO[63]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(0), "FUNC_SHARE[0]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(1), "FUNC_SHARE[1]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(2), "FUNC_SHARE[2]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(3), "FUNC_SHARE[3]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(4), "FUNC_SHARE[4]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(5), "FUNC_SHARE[5]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(6), "FUNC_SHARE[6]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(7), "FUNC_SHARE[7]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(8), "FUNC_SHARE[8]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(9), "FUNC_SHARE[9]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(10), "FUNC_SHARE[10]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(11), "FUNC_SHARE[11]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(12), "FUNC_SHARE[12]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(13), "FUNC_SHARE[13]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(14), "FUNC_SHARE[14]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(15), "FUNC_SHARE[15]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(16), "FUNC_SHARE[16]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(17), "FUNC_SHARE[17]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(18), "FUNC_SHARE[18]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(19), "FUNC_SHARE[19]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(20), "FUNC_SHARE[20]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(21), "FUNC_SHARE[21]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(22), "FUNC_SHARE[22]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(23), "FUNC_SHARE[23]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(24), "FUNC_SHARE[24]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(25), "FUNC_SHARE[25]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(26), "FUNC_SHARE[26]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(27), "FUNC_SHARE[27]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(28), "FUNC_SHARE[28]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(29), "FUNC_SHARE[29]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(30), "FUNC_SHARE[30]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(31), "FUNC_SHARE[31]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(32), "FUNC_SHARE[32]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(33), "FUNC_SHARE[33]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(34), "FUNC_SHARE[34]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(35), "FUNC_SHARE[35]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(36), "FUNC_SHARE[36]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(37), "FUNC_SHARE[37]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(38), "FUNC_SHARE[38]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(39), "FUNC_SHARE[39]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(40), "FUNC_SHARE[40]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(41), "FUNC_SHARE[41]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(42), "FUNC_SHARE[42]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(43), "FUNC_SHARE[43]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(44), "FUNC_SHARE[44]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(45), "FUNC_SHARE[45]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(46), "FUNC_SHARE[46]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(47), "FUNC_SHARE[47]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(48), "FUNC_SHARE[48]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(49), "FUNC_SHARE[49]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(50), "FUNC_SHARE[50]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(51), "FUNC_SHARE[51]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(52), "FUNC_SHARE[52]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(53), "FUNC_SHARE[53]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(54), "FUNC_SHARE[54]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(55), "FUNC_SHARE[55]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(56), "FUNC_SHARE[56]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(57), "FUNC_SHARE[57]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(58), "FUNC_SHARE[58]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(59), "FUNC_SHARE[59]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(60), "FUNC_SHARE[60]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(61), "FUNC_SHARE[61]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(62), "FUNC_SHARE[62]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(63), "FUNC_SHARE[63]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(64), "FUNC_SHARE[64]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(65), "FUNC_SHARE[65]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(66), "FUNC_SHARE[66]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(67), "FUNC_SHARE[67]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(68), "FUNC_SHARE[68]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(69), "FUNC_SHARE[69]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(70), "FUNC_SHARE[70]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(71), "FUNC_SHARE[71]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(72), "FUNC_SHARE[72]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(73), "FUNC_SHARE[73]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(74), "FUNC_SHARE[74]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(75), "FUNC_SHARE[75]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(76), "FUNC_SHARE[76]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(77), "FUNC_SHARE[77]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(78), "FUNC_SHARE[78]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(79), "FUNC_SHARE[79]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(80), "FUNC_SHARE[80]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(81), "FUNC_SHARE[81]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(82), "FUNC_SHARE[82]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(83), "FUNC_SHARE[83]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(84), "FUNC_SHARE[84]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(85), "FUNC_SHARE[85]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(86), "FUNC_SHARE[86]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(87), "FUNC_SHARE[87]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(88), "FUNC_SHARE[88]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(89), "FUNC_SHARE[89]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(90), "FUNC_SHARE[90]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(91), "FUNC_SHARE[91]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(92), "FUNC_SHARE[92]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(93), "FUNC_SHARE[93]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(94), "FUNC_SHARE[94]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(95), "FUNC_SHARE[95]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(96), "FUNC_SHARE[96]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(97), "FUNC_SHARE[97]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(98), "FUNC_SHARE[98]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(99), "FUNC_SHARE[99]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(100), "FUNC_SHARE[100]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(101), "FUNC_SHARE[101]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(102), "FUNC_SHARE[102]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(103), "FUNC_SHARE[103]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(104), "FUNC_SHARE[104]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(105), "FUNC_SHARE[105]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(106), "FUNC_SHARE[106]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(107), "FUNC_SHARE[107]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(108), "FUNC_SHARE[108]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(109), "FUNC_SHARE[109]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(110), "FUNC_SHARE[110]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(111), "FUNC_SHARE[111]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(112), "FUNC_SHARE[112]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(113), "FUNC_SHARE[113]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(114), "FUNC_SHARE[114]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(115), "FUNC_SHARE[115]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(116), "FUNC_SHARE[116]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(117), "FUNC_SHARE[117]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(118), "FUNC_SHARE[118]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(119), "FUNC_SHARE[119]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(120), "FUNC_SHARE[120]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(121), "FUNC_SHARE[121]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(122), "FUNC_SHARE[122]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(123), "FUNC_SHARE[123]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(124), "FUNC_SHARE[124]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(125), "FUNC_SHARE[125]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(126), "FUNC_SHARE[126]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(127), "FUNC_SHARE[127]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(128), "FUNC_SHARE[128]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(129), "FUNC_SHARE[129]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(130), "FUNC_SHARE[130]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(131), "FUNC_SHARE[131]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(132), "FUNC_SHARE[132]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(133), "FUNC_SHARE[133]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(134), "FUNC_SHARE[134]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(135), "FUNC_SHARE[135]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(136), "FUNC_SHARE[136]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(137), "FUNC_SHARE[137]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(138), "FUNC_SHARE[138]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(139), "FUNC_SHARE[139]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(140), "FUNC_SHARE[140]"),
	PINCTRL_PIN(PAD_FUNC_SHARE(141), "FUNC_SHARE[141]"),
};

#ifdef CONFIG_DEBUG_FS
static void starfive_pin_dbg_show(struct pinctrl_dev *pctldev,
				  struct seq_file *s,
				  unsigned int pin)
{
	struct starfive_pinctrl *sfp = pinctrl_dev_get_drvdata(pctldev);
	unsigned int gpio = starfive_pin_to_gpio(sfp, pin);
	void __iomem *reg;
	u32 dout, doen;

	if (gpio >= NR_GPIOS)
		return;

	reg = sfp->base + GPON_DOUT_CFG + 8 * gpio;
	dout = readl_relaxed(reg + 0x000);
	doen = readl_relaxed(reg + 0x004);

	seq_printf(s, "dout=%lu%s doen=%lu%s",
		   dout & GENMASK(7, 0), (dout & BIT(31)) ? "r" : "",
		   doen & GENMASK(7, 0), (doen & BIT(31)) ? "r" : "");
}
#else
#define starfive_pin_dbg_show NULL
#endif

static int starfive_dt_node_to_map(struct pinctrl_dev *pctldev,
				   struct device_node *np,
				   struct pinctrl_map **maps,
				   unsigned int *num_maps)
{
	struct starfive_pinctrl *sfp = pinctrl_dev_get_drvdata(pctldev);
	struct device *dev = sfp->gc.parent;
	struct device_node *child;
	struct pinctrl_map *map;
	const char **pgnames;
	const char *grpname;
	u32 *pinmux;
	int ngroups;
	int *pins;
	int nmaps;
	int ret;

	nmaps = 0;
	ngroups = 0;
	for_each_child_of_node(np, child) {
		int npinmux = of_property_count_u32_elems(child, "pinmux");
		int npins   = of_property_count_u32_elems(child, "pins");

		if (npinmux > 0 && npins > 0) {
			dev_err(dev, "invalid pinctrl group %pOFn.%pOFn: both pinmux and pins set\n",
				np, child);
			of_node_put(child);
			return -EINVAL;
		}
		if (npinmux == 0 && npins == 0) {
			dev_err(dev, "invalid pinctrl group %pOFn.%pOFn: neither pinmux nor pins set\n",
				np, child);
			of_node_put(child);
			return -EINVAL;
		}

		if (npinmux > 0)
			nmaps += 2;
		else
			nmaps += 1;
		ngroups += 1;
	}

	pgnames = devm_kcalloc(dev, ngroups, sizeof(*pgnames), GFP_KERNEL);
	if (!pgnames)
		return -ENOMEM;

	map = kcalloc(nmaps, sizeof(*map), GFP_KERNEL);
	if (!map)
		return -ENOMEM;

	nmaps = 0;
	ngroups = 0;
	mutex_lock(&sfp->mutex);
	for_each_child_of_node(np, child) {
		int npins;
		int i;

		grpname = devm_kasprintf(dev, GFP_KERNEL, "%pOFn.%pOFn", np, child);
		if (!grpname) {
			ret = -ENOMEM;
			goto put_child;
		}

		pgnames[ngroups++] = grpname;

		if ((npins = of_property_count_u32_elems(child, "pinmux")) > 0) {
			pins = devm_kcalloc(dev, npins, sizeof(*pins), GFP_KERNEL);
			if (!pins) {
				ret = -ENOMEM;
				goto put_child;
			}

			pinmux = devm_kcalloc(dev, npins, sizeof(*pinmux), GFP_KERNEL);
			if (!pinmux) {
				ret = -ENOMEM;
				goto put_child;
			}

			ret = of_property_read_u32_array(child, "pinmux", pinmux, npins);
			if (ret)
				goto put_child;

			for (i = 0; i < npins; i++) {
				unsigned int gpio = starfive_pinmux_to_gpio(pinmux[i]);

				pins[i] = starfive_gpio_to_pin(sfp, gpio);
			}

			map[nmaps].type = PIN_MAP_TYPE_MUX_GROUP;
			map[nmaps].data.mux.function = np->name;
			map[nmaps].data.mux.group = grpname;
			nmaps += 1;
		} else if ((npins = of_property_count_u32_elems(child, "pins")) > 0) {
			pins = devm_kcalloc(dev, npins, sizeof(*pins), GFP_KERNEL);
			if (!pins) {
				ret = -ENOMEM;
				goto put_child;
			}

			pinmux = NULL;

			for (i = 0; i < npins; i++) {
				u32 v;

				ret = of_property_read_u32_index(child, "pins", i, &v);
				if (ret)
					goto put_child;
				pins[i] = v;
			}
		} else {
			ret = -EINVAL;
			goto put_child;
		}

		ret = pinctrl_generic_add_group(pctldev, grpname, pins, npins, pinmux);
		if (ret < 0) {
			dev_err(dev, "error adding group %s: %d\n", grpname, ret);
			goto put_child;
		}

		ret = pinconf_generic_parse_dt_config(child, pctldev,
						      &map[nmaps].data.configs.configs,
						      &map[nmaps].data.configs.num_configs);
		if (ret) {
			dev_err(dev, "error parsing pin config of group %s: %d\n",
				grpname, ret);
			goto put_child;
		}

		/* don't create a map if there are no pinconf settings */
		if (map[nmaps].data.configs.num_configs == 0)
			continue;

		map[nmaps].type = PIN_MAP_TYPE_CONFIGS_GROUP;
		map[nmaps].data.configs.group_or_pin = grpname;
		nmaps += 1;
	}

	ret = pinmux_generic_add_function(pctldev, np->name, pgnames, ngroups, NULL);
	if (ret < 0) {
		dev_err(dev, "error adding function %s: %d\n", np->name, ret);
		goto free_map;
	}

	*maps = map;
	*num_maps = nmaps;
	mutex_unlock(&sfp->mutex);
	return 0;

put_child:
	of_node_put(child);
free_map:
	pinctrl_utils_free_map(pctldev, map, nmaps);
	mutex_unlock(&sfp->mutex);
	return ret;
}

static const struct pinctrl_ops starfive_pinctrl_ops = {
	.get_groups_count = pinctrl_generic_get_group_count,
	.get_group_name = pinctrl_generic_get_group_name,
	.get_group_pins = pinctrl_generic_get_group_pins,
	.pin_dbg_show = starfive_pin_dbg_show,
	.dt_node_to_map = starfive_dt_node_to_map,
	.dt_free_map = pinctrl_utils_free_map,
};

static int starfive_set_mux(struct pinctrl_dev *pctldev,
			    unsigned int fsel, unsigned int gsel)
{
	struct starfive_pinctrl *sfp = pinctrl_dev_get_drvdata(pctldev);
	struct device *dev = sfp->gc.parent;
	const struct group_desc *group;
	const u32 *pinmux;
	unsigned int i;

	group = pinctrl_generic_get_group(pctldev, gsel);
	if (!group)
		return -EINVAL;

	pinmux = group->data;
	for (i = 0; i < group->num_pins; i++) {
		u32 v = pinmux[i];
		unsigned int gpio = starfive_pinmux_to_gpio(v);
		u32 dout = starfive_pinmux_to_dout(v);
		u32 doen = starfive_pinmux_to_doen(v);
		u32 din = starfive_pinmux_to_din(v);
		void __iomem *reg_dout;
		void __iomem *reg_doen;
		void __iomem *reg_din;
		unsigned long flags;

		dev_dbg(dev, "GPIO%u: dout=0x%x doen=0x%x din=0x%x\n",
			gpio, dout, doen, din);

		reg_dout = sfp->base + GPON_DOUT_CFG + 8 * gpio;
		reg_doen = sfp->base + GPON_DOEN_CFG + 8 * gpio;
		if (din != GPI_NONE)
			reg_din = sfp->base + GPI_CFG_OFFSET + 4 * din;
		else
			reg_din = NULL;

		raw_spin_lock_irqsave(&sfp->lock, flags);
		writel_relaxed(dout, reg_dout);
		writel_relaxed(doen, reg_doen);
		if (reg_din)
			writel_relaxed(gpio + 2, reg_din);
		raw_spin_unlock_irqrestore(&sfp->lock, flags);
	}

	return 0;
}

static const struct pinmux_ops starfive_pinmux_ops = {
	.get_functions_count = pinmux_generic_get_function_count,
	.get_function_name = pinmux_generic_get_function_name,
	.get_function_groups = pinmux_generic_get_function_groups,
	.set_mux = starfive_set_mux,
	.strict = true,
};

static u16 starfive_padctl_get(struct starfive_pinctrl *sfp,
			       unsigned int pin)
{
	void __iomem *reg = sfp->padctl + 4 * (pin / 2);
	int shift = 16 * (pin % 2);

	return readl_relaxed(reg) >> shift;
}

static void starfive_padctl_rmw(struct starfive_pinctrl *sfp,
				unsigned int pin,
				u16 _mask, u16 _value)
{
	void __iomem *reg = sfp->padctl + 4 * (pin / 2);
	int shift = 16 * (pin % 2);
	u32 mask = (u32)_mask << shift;
	u32 value = (u32)_value << shift;
	unsigned long flags;

	dev_dbg(sfp->gc.parent, "padctl_rmw(%u, 0x%03x, 0x%03x)\n", pin, _mask, _value);

	raw_spin_lock_irqsave(&sfp->lock, flags);
	value |= readl_relaxed(reg) & ~mask;
	writel_relaxed(value, reg);
	raw_spin_unlock_irqrestore(&sfp->lock, flags);
}

#define PIN_CONFIG_STARFIVE_STRONG_PULL_UP	(PIN_CONFIG_END + 1)

static const struct pinconf_generic_params starfive_pinconf_custom_params[] = {
	{ "starfive,strong-pull-up", PIN_CONFIG_STARFIVE_STRONG_PULL_UP, 1 },
};

#ifdef CONFIG_DEBUG_FS
static const struct pin_config_item starfive_pinconf_custom_conf_items[] = {
	PCONFDUMP(PIN_CONFIG_STARFIVE_STRONG_PULL_UP, "input bias strong pull-up", NULL, false),
};

static_assert(ARRAY_SIZE(starfive_pinconf_custom_conf_items) ==
	      ARRAY_SIZE(starfive_pinconf_custom_params));
#else
#define starfive_pinconf_custom_conf_items NULL
#endif

static int starfive_pinconf_get(struct pinctrl_dev *pctldev,
				unsigned int pin, unsigned long *config)
{
	struct starfive_pinctrl *sfp = pinctrl_dev_get_drvdata(pctldev);
	int param = pinconf_to_config_param(*config);
	u16 value = starfive_padctl_get(sfp, pin);
	bool enabled;
	u32 arg;

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		enabled = value & PAD_BIAS_DISABLE;
		arg = 0;
		break;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		enabled = value & PAD_BIAS_PULL_DOWN;
		arg = 1;
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
		enabled = !(value & PAD_BIAS_MASK);
		arg = 1;
		break;
	case PIN_CONFIG_DRIVE_STRENGTH:
		enabled = value & PAD_DRIVE_STRENGTH_MASK;
		arg = starfive_drive_strength_to_max_mA(value & PAD_DRIVE_STRENGTH_MASK);
		break;
	case PIN_CONFIG_INPUT_ENABLE:
		enabled = value & PAD_INPUT_ENABLE;
		arg = enabled;
		break;
	case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
		enabled = value & PAD_INPUT_SCHMITT_ENABLE;
		arg = enabled;
		break;
	case PIN_CONFIG_SLEW_RATE:
		enabled = value & PAD_SLEW_RATE_MASK;
		arg = (value & PAD_SLEW_RATE_MASK) >> PAD_SLEW_RATE_POS;
		break;
	case PIN_CONFIG_STARFIVE_STRONG_PULL_UP:
		enabled = value & PAD_BIAS_STRONG_PULL_UP;
		arg = enabled;
		break;
	default:
		return -ENOTSUPP;
	}

	*config = pinconf_to_config_packed(param, arg);
	return enabled ? 0 : -EINVAL;
}

static int starfive_pinconf_group_get(struct pinctrl_dev *pctldev,
				      unsigned int gsel, unsigned long *config)
{
	const struct group_desc *group;

	group = pinctrl_generic_get_group(pctldev, gsel);
	if (!group)
		return -EINVAL;

	return starfive_pinconf_get(pctldev, group->pins[0], config);
}

static int starfive_pinconf_group_set(struct pinctrl_dev *pctldev,
				      unsigned int gsel,
				      unsigned long *configs,
				      unsigned int num_configs)
{
	struct starfive_pinctrl *sfp = pinctrl_dev_get_drvdata(pctldev);
	const struct group_desc *group;
	u16 mask, value;
	int i;

	group = pinctrl_generic_get_group(pctldev, gsel);
	if (!group)
		return -EINVAL;

	mask = 0;
	value = 0;
	for (i = 0; i < num_configs; i++) {
		int param = pinconf_to_config_param(configs[i]);
		u32 arg = pinconf_to_config_argument(configs[i]);

		switch (param) {
		case PIN_CONFIG_BIAS_DISABLE:
			mask |= PAD_BIAS_MASK;
			value = (value & ~PAD_BIAS_MASK) | PAD_BIAS_DISABLE;
			break;
		case PIN_CONFIG_BIAS_PULL_DOWN:
			if (arg == 0)
				return -ENOTSUPP;
			mask |= PAD_BIAS_MASK;
			value = (value & ~PAD_BIAS_MASK) | PAD_BIAS_PULL_DOWN;
			break;
		case PIN_CONFIG_BIAS_PULL_UP:
			if (arg == 0)
				return -ENOTSUPP;
			mask |= PAD_BIAS_MASK;
			value = value & ~PAD_BIAS_MASK;
			break;
		case PIN_CONFIG_DRIVE_STRENGTH:
			mask |= PAD_DRIVE_STRENGTH_MASK;
			value = (value & ~PAD_DRIVE_STRENGTH_MASK) |
				starfive_drive_strength_from_max_mA(arg);
			break;
		case PIN_CONFIG_INPUT_ENABLE:
			mask |= PAD_INPUT_ENABLE;
			if (arg)
				value |= PAD_INPUT_ENABLE;
			else
				value &= ~PAD_INPUT_ENABLE;
			break;
		case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
			mask |= PAD_INPUT_SCHMITT_ENABLE;
			if (arg)
				value |= PAD_INPUT_SCHMITT_ENABLE;
			else
				value &= ~PAD_INPUT_SCHMITT_ENABLE;
			break;
		case PIN_CONFIG_SLEW_RATE:
			mask |= PAD_SLEW_RATE_MASK;
			value = (value & ~PAD_SLEW_RATE_MASK) |
				((arg << PAD_SLEW_RATE_POS) & PAD_SLEW_RATE_MASK);
			break;
		case PIN_CONFIG_STARFIVE_STRONG_PULL_UP:
			if (arg) {
				mask |= PAD_BIAS_MASK;
				value = (value & ~PAD_BIAS_MASK) |
					PAD_BIAS_STRONG_PULL_UP;
			} else {
				mask |= PAD_BIAS_STRONG_PULL_UP;
				value = value & ~PAD_BIAS_STRONG_PULL_UP;
			}
			break;
		default:
			return -ENOTSUPP;
		}
	}

	for (i = 0; i < group->num_pins; i++)
		starfive_padctl_rmw(sfp, group->pins[i], mask, value);

	return 0;
}

#ifdef CONFIG_DEBUG_FS
static void starfive_pinconf_dbg_show(struct pinctrl_dev *pctldev,
				      struct seq_file *s, unsigned int pin)
{
	struct starfive_pinctrl *sfp = pinctrl_dev_get_drvdata(pctldev);
	u16 value = starfive_padctl_get(sfp, pin);

	seq_printf(s, " (0x%03x)", value);
}
#else
#define starfive_pinconf_dbg_show NULL
#endif

static const struct pinconf_ops starfive_pinconf_ops = {
	.pin_config_get = starfive_pinconf_get,
	.pin_config_group_get = starfive_pinconf_group_get,
	.pin_config_group_set = starfive_pinconf_group_set,
	.pin_config_dbg_show = starfive_pinconf_dbg_show,
	.is_generic = true,
};

static struct pinctrl_desc starfive_desc = {
	.name = DRIVER_NAME,
	.pins = starfive_pins,
	.npins = ARRAY_SIZE(starfive_pins),
	.pctlops = &starfive_pinctrl_ops,
	.pmxops = &starfive_pinmux_ops,
	.confops = &starfive_pinconf_ops,
	.owner = THIS_MODULE,
	.num_custom_params = ARRAY_SIZE(starfive_pinconf_custom_params),
	.custom_params = starfive_pinconf_custom_params,
	.custom_conf_items = starfive_pinconf_custom_conf_items,
};

static int starfive_gpio_get_direction(struct gpio_chip *gc, unsigned int gpio)
{
	struct starfive_pinctrl *sfp = container_of(gc, struct starfive_pinctrl, gc);
	void __iomem *doen = sfp->base + GPON_DOEN_CFG + 8 * gpio;

	if (readl_relaxed(doen) == GPO_ENABLE)
		return GPIO_LINE_DIRECTION_OUT;

	return GPIO_LINE_DIRECTION_IN;
}

static int starfive_gpio_direction_input(struct gpio_chip *gc,
					 unsigned int gpio)
{
	struct starfive_pinctrl *sfp = container_of(gc, struct starfive_pinctrl, gc);
	void __iomem *doen = sfp->base + GPON_DOEN_CFG + 8 * gpio;
	unsigned long flags;

	/* enable input and schmitt trigger */
	starfive_padctl_rmw(sfp, starfive_gpio_to_pin(sfp, gpio),
			    PAD_INPUT_ENABLE | PAD_INPUT_SCHMITT_ENABLE,
			    PAD_INPUT_ENABLE | PAD_INPUT_SCHMITT_ENABLE);

	raw_spin_lock_irqsave(&sfp->lock, flags);
	writel_relaxed(GPO_DISABLE, doen);
	raw_spin_unlock_irqrestore(&sfp->lock, flags);
	return 0;
}

static int starfive_gpio_direction_output(struct gpio_chip *gc,
					  unsigned int gpio, int value)
{
	struct starfive_pinctrl *sfp = container_of(gc, struct starfive_pinctrl, gc);
	void __iomem *dout = sfp->base + GPON_DOUT_CFG + 8 * gpio;
	void __iomem *doen = sfp->base + GPON_DOEN_CFG + 8 * gpio;
	unsigned long flags;

	raw_spin_lock_irqsave(&sfp->lock, flags);
	writel_relaxed(value, dout);
	writel_relaxed(GPO_ENABLE, doen);
	raw_spin_unlock_irqrestore(&sfp->lock, flags);

	/* disable input, schmitt trigger and bias */
	starfive_padctl_rmw(sfp, starfive_gpio_to_pin(sfp, gpio),
			    PAD_BIAS_MASK | PAD_INPUT_ENABLE | PAD_INPUT_SCHMITT_ENABLE,
			    PAD_BIAS_DISABLE);

	return 0;
}

static int starfive_gpio_get(struct gpio_chip *gc, unsigned int gpio)
{
	struct starfive_pinctrl *sfp = container_of(gc, struct starfive_pinctrl, gc);
	void __iomem *din = sfp->base + GPIODIN + 4 * (gpio / 32);

	return !!(readl_relaxed(din) & BIT(gpio % 32));
}

static void starfive_gpio_set(struct gpio_chip *gc, unsigned int gpio,
			      int value)
{
	struct starfive_pinctrl *sfp = container_of(gc, struct starfive_pinctrl, gc);
	void __iomem *dout = sfp->base + GPON_DOUT_CFG + 8 * gpio;
	unsigned long flags;

	raw_spin_lock_irqsave(&sfp->lock, flags);
	writel_relaxed(value, dout);
	raw_spin_unlock_irqrestore(&sfp->lock, flags);
}

static int starfive_gpio_set_config(struct gpio_chip *gc, unsigned int gpio,
				    unsigned long config)
{
	struct starfive_pinctrl *sfp = container_of(gc, struct starfive_pinctrl, gc);
	u32 arg = pinconf_to_config_argument(config);
	u16 value;
	u16 mask;

	switch (pinconf_to_config_param(config)) {
	case PIN_CONFIG_BIAS_DISABLE:
		mask  = PAD_BIAS_MASK;
		value = PAD_BIAS_DISABLE;
		break;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		if (arg == 0)
			return -ENOTSUPP;
		mask  = PAD_BIAS_MASK;
		value = PAD_BIAS_PULL_DOWN;
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
		if (arg == 0)
			return -ENOTSUPP;
		mask  = PAD_BIAS_MASK;
		value = 0;
		break;
	case PIN_CONFIG_DRIVE_PUSH_PULL:
		return 0;
	case PIN_CONFIG_INPUT_ENABLE:
		mask  = PAD_INPUT_ENABLE;
		value = arg ? PAD_INPUT_ENABLE : 0;
		break;
	case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
		mask  = PAD_INPUT_SCHMITT_ENABLE;
		value = arg ? PAD_INPUT_SCHMITT_ENABLE : 0;
		break;
	default:
		return -ENOTSUPP;
	}

	starfive_padctl_rmw(sfp, starfive_gpio_to_pin(sfp, gpio), mask, value);
	return 0;
}

static int starfive_gpio_add_pin_ranges(struct gpio_chip *gc)
{
	struct starfive_pinctrl *sfp = container_of(gc, struct starfive_pinctrl, gc);

	sfp->gpios.name = sfp->gc.label;
	sfp->gpios.base = sfp->gc.base;
	/*
	 * sfp->gpios.pin_base depends on the chosen signal group
	 * and is set in starfive_probe()
	 */
	sfp->gpios.npins = NR_GPIOS;
	sfp->gpios.gc = &sfp->gc;
	pinctrl_add_gpio_range(sfp->pctl, &sfp->gpios);
	return 0;
}

static void starfive_irq_ack(struct irq_data *d)
{
	struct starfive_pinctrl *sfp = starfive_from_irq_data(d);
	irq_hw_number_t gpio = irqd_to_hwirq(d);
	void __iomem *ic = sfp->base + GPIOIC + 4 * (gpio / 32);
	u32 mask = BIT(gpio % 32);
	unsigned long flags;

	raw_spin_lock_irqsave(&sfp->lock, flags);
	writel_relaxed(mask, ic);
	raw_spin_unlock_irqrestore(&sfp->lock, flags);
}

static void starfive_irq_mask(struct irq_data *d)
{
	struct starfive_pinctrl *sfp = starfive_from_irq_data(d);
	irq_hw_number_t gpio = irqd_to_hwirq(d);
	void __iomem *ie = sfp->base + GPIOIE + 4 * (gpio / 32);
	u32 mask = BIT(gpio % 32);
	unsigned long flags;
	u32 value;

	raw_spin_lock_irqsave(&sfp->lock, flags);
	value = readl_relaxed(ie) & ~mask;
	writel_relaxed(value, ie);
	raw_spin_unlock_irqrestore(&sfp->lock, flags);

	gpiochip_disable_irq(&sfp->gc, gpio);
}

static void starfive_irq_mask_ack(struct irq_data *d)
{
	struct starfive_pinctrl *sfp = starfive_from_irq_data(d);
	irq_hw_number_t gpio = irqd_to_hwirq(d);
	void __iomem *ie = sfp->base + GPIOIE + 4 * (gpio / 32);
	void __iomem *ic = sfp->base + GPIOIC + 4 * (gpio / 32);
	u32 mask = BIT(gpio % 32);
	unsigned long flags;
	u32 value;

	raw_spin_lock_irqsave(&sfp->lock, flags);
	value = readl_relaxed(ie) & ~mask;
	writel_relaxed(value, ie);
	writel_relaxed(mask, ic);
	raw_spin_unlock_irqrestore(&sfp->lock, flags);
}

static void starfive_irq_unmask(struct irq_data *d)
{
	struct starfive_pinctrl *sfp = starfive_from_irq_data(d);
	irq_hw_number_t gpio = irqd_to_hwirq(d);
	void __iomem *ie = sfp->base + GPIOIE + 4 * (gpio / 32);
	u32 mask = BIT(gpio % 32);
	unsigned long flags;
	u32 value;

	gpiochip_enable_irq(&sfp->gc, gpio);

	raw_spin_lock_irqsave(&sfp->lock, flags);
	value = readl_relaxed(ie) | mask;
	writel_relaxed(value, ie);
	raw_spin_unlock_irqrestore(&sfp->lock, flags);
}

static int starfive_irq_set_type(struct irq_data *d, unsigned int trigger)
{
	struct starfive_pinctrl *sfp = starfive_from_irq_data(d);
	irq_hw_number_t gpio = irqd_to_hwirq(d);
	void __iomem *base = sfp->base + 4 * (gpio / 32);
	u32 mask = BIT(gpio % 32);
	u32 irq_type, edge_both, polarity;
	unsigned long flags;

	switch (trigger) {
	case IRQ_TYPE_EDGE_RISING:
		irq_type  = mask; /* 1: edge triggered */
		edge_both = 0;    /* 0: single edge */
		polarity  = mask; /* 1: rising edge */
		break;
	case IRQ_TYPE_EDGE_FALLING:
		irq_type  = mask; /* 1: edge triggered */
		edge_both = 0;    /* 0: single edge */
		polarity  = 0;    /* 0: falling edge */
		break;
	case IRQ_TYPE_EDGE_BOTH:
		irq_type  = mask; /* 1: edge triggered */
		edge_both = mask; /* 1: both edges */
		polarity  = 0;    /* 0: ignored */
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		irq_type  = 0;    /* 0: level triggered */
		edge_both = 0;    /* 0: ignored */
		polarity  = mask; /* 1: high level */
		break;
	case IRQ_TYPE_LEVEL_LOW:
		irq_type  = 0;    /* 0: level triggered */
		edge_both = 0;    /* 0: ignored */
		polarity  = 0;    /* 0: low level */
		break;
	default:
		return -EINVAL;
	}

	if (trigger & IRQ_TYPE_EDGE_BOTH)
		irq_set_handler_locked(d, handle_edge_irq);
	else
		irq_set_handler_locked(d, handle_level_irq);

	raw_spin_lock_irqsave(&sfp->lock, flags);
	irq_type |= readl_relaxed(base + GPIOIS) & ~mask;
	writel_relaxed(irq_type, base + GPIOIS);
	edge_both |= readl_relaxed(base + GPIOIBE) & ~mask;
	writel_relaxed(edge_both, base + GPIOIBE);
	polarity |= readl_relaxed(base + GPIOIEV) & ~mask;
	writel_relaxed(polarity, base + GPIOIEV);
	raw_spin_unlock_irqrestore(&sfp->lock, flags);
	return 0;
}

static const struct irq_chip starfive_irq_chip = {
	.name = "StarFive GPIO",
	.irq_ack = starfive_irq_ack,
	.irq_mask = starfive_irq_mask,
	.irq_mask_ack = starfive_irq_mask_ack,
	.irq_unmask = starfive_irq_unmask,
	.irq_set_type = starfive_irq_set_type,
	.flags = IRQCHIP_IMMUTABLE | IRQCHIP_SET_TYPE_MASKED,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

static void starfive_gpio_irq_handler(struct irq_desc *desc)
{
	struct starfive_pinctrl *sfp = starfive_from_irq_desc(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	unsigned long mis;
	unsigned int pin;

	chained_irq_enter(chip, desc);

	mis = readl_relaxed(sfp->base + GPIOMIS + 0);
	for_each_set_bit(pin, &mis, 32)
		generic_handle_domain_irq(sfp->gc.irq.domain, pin);

	mis = readl_relaxed(sfp->base + GPIOMIS + 4);
	for_each_set_bit(pin, &mis, 32)
		generic_handle_domain_irq(sfp->gc.irq.domain, pin + 32);

	chained_irq_exit(chip, desc);
}

static int starfive_gpio_init_hw(struct gpio_chip *gc)
{
	struct starfive_pinctrl *sfp = container_of(gc, struct starfive_pinctrl, gc);

	/* mask all GPIO interrupts */
	writel(0, sfp->base + GPIOIE + 0);
	writel(0, sfp->base + GPIOIE + 4);
	/* clear edge interrupt flags */
	writel(~0U, sfp->base + GPIOIC + 0);
	writel(~0U, sfp->base + GPIOIC + 4);
	/* enable GPIO interrupts */
	writel(1, sfp->base + GPIOEN);
	return 0;
}

static void starfive_disable_clock(void *data)
{
	clk_disable_unprepare(data);
}

static int starfive_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct starfive_pinctrl *sfp;
	struct reset_control *rst;
	struct clk *clk;
	u32 value;
	int ret;

	sfp = devm_kzalloc(dev, sizeof(*sfp), GFP_KERNEL);
	if (!sfp)
		return -ENOMEM;

	sfp->base = devm_platform_ioremap_resource_byname(pdev, "gpio");
	if (IS_ERR(sfp->base))
		return PTR_ERR(sfp->base);

	sfp->padctl = devm_platform_ioremap_resource_byname(pdev, "padctl");
	if (IS_ERR(sfp->padctl))
		return PTR_ERR(sfp->padctl);

	clk = devm_clk_get(dev, NULL);
	if (IS_ERR(clk))
		return dev_err_probe(dev, PTR_ERR(clk), "could not get clock\n");

	rst = devm_reset_control_get_exclusive(dev, NULL);
	if (IS_ERR(rst))
		return dev_err_probe(dev, PTR_ERR(rst), "could not get reset\n");

	ret = clk_prepare_enable(clk);
	if (ret)
		return dev_err_probe(dev, ret, "could not enable clock\n");

	ret = devm_add_action_or_reset(dev, starfive_disable_clock, clk);
	if (ret)
		return ret;

	/*
	 * We don't want to assert reset and risk undoing pin muxing for the
	 * early boot serial console, but let's make sure the reset line is
	 * deasserted in case someone runs a really minimal bootloader.
	 */
	ret = reset_control_deassert(rst);
	if (ret)
		return dev_err_probe(dev, ret, "could not deassert reset\n");

	platform_set_drvdata(pdev, sfp);
	sfp->gc.parent = dev;
	raw_spin_lock_init(&sfp->lock);
	mutex_init(&sfp->mutex);

	ret = devm_pinctrl_register_and_init(dev, &starfive_desc, sfp, &sfp->pctl);
	if (ret)
		return dev_err_probe(dev, ret, "could not register pinctrl driver\n");

	if (!of_property_read_u32(dev->of_node, "starfive,signal-group", &value)) {
		if (value > 6)
			return dev_err_probe(dev, -EINVAL, "invalid signal group %u\n", value);
		writel(value, sfp->padctl + IO_PADSHARE_SEL);
	}

	value = readl(sfp->padctl + IO_PADSHARE_SEL);
	switch (value) {
	case 0:
		sfp->gpios.pin_base = PAD_INVALID_GPIO;
		goto out_pinctrl_enable;
	case 1:
		sfp->gpios.pin_base = PAD_GPIO(0);
		break;
	case 2:
		sfp->gpios.pin_base = PAD_FUNC_SHARE(72);
		break;
	case 3:
		sfp->gpios.pin_base = PAD_FUNC_SHARE(70);
		break;
	case 4: case 5: case 6:
		sfp->gpios.pin_base = PAD_FUNC_SHARE(0);
		break;
	default:
		return dev_err_probe(dev, -EINVAL, "invalid signal group %u\n", value);
	}

	sfp->gc.label = dev_name(dev);
	sfp->gc.owner = THIS_MODULE;
	sfp->gc.request = pinctrl_gpio_request;
	sfp->gc.free = pinctrl_gpio_free;
	sfp->gc.get_direction = starfive_gpio_get_direction;
	sfp->gc.direction_input = starfive_gpio_direction_input;
	sfp->gc.direction_output = starfive_gpio_direction_output;
	sfp->gc.get = starfive_gpio_get;
	sfp->gc.set = starfive_gpio_set;
	sfp->gc.set_config = starfive_gpio_set_config;
	sfp->gc.add_pin_ranges = starfive_gpio_add_pin_ranges;
	sfp->gc.base = -1;
	sfp->gc.ngpio = NR_GPIOS;

	gpio_irq_chip_set_chip(&sfp->gc.irq, &starfive_irq_chip);
	sfp->gc.irq.parent_handler = starfive_gpio_irq_handler;
	sfp->gc.irq.num_parents = 1;
	sfp->gc.irq.parents = devm_kcalloc(dev, sfp->gc.irq.num_parents,
					   sizeof(*sfp->gc.irq.parents), GFP_KERNEL);
	if (!sfp->gc.irq.parents)
		return -ENOMEM;
	sfp->gc.irq.default_type = IRQ_TYPE_NONE;
	sfp->gc.irq.handler = handle_bad_irq;
	sfp->gc.irq.init_hw = starfive_gpio_init_hw;

	ret = platform_get_irq(pdev, 0);
	if (ret < 0)
		return ret;
	sfp->gc.irq.parents[0] = ret;

	ret = devm_gpiochip_add_data(dev, &sfp->gc, sfp);
	if (ret)
		return dev_err_probe(dev, ret, "could not register gpiochip\n");

	irq_domain_set_pm_device(sfp->gc.irq.domain, dev);

out_pinctrl_enable:
	return pinctrl_enable(sfp->pctl);
}

static const struct of_device_id starfive_of_match[] = {
	{ .compatible = "starfive,jh7100-pinctrl" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, starfive_of_match);

static struct platform_driver starfive_pinctrl_driver = {
	.probe = starfive_probe,
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = starfive_of_match,
	},
};
module_platform_driver(starfive_pinctrl_driver);

MODULE_DESCRIPTION("Pinctrl driver for StarFive SoCs");
MODULE_AUTHOR("Emil Renner Berthing <kernel@esmil.dk>");
MODULE_LICENSE("GPL v2");

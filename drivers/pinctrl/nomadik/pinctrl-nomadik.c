// SPDX-License-Identifier: GPL-2.0-only
/*
 * Generic GPIO driver for logic cells found in the Nomadik SoC
 *
 * Copyright (C) 2008,2009 STMicroelectronics
 * Copyright (C) 2009 Alessandro Rubini <rubini@unipv.it>
 *   Rewritten based on work by Prafulla WADASKAR <prafulla.wadaskar@st.com>
 * Copyright (C) 2011-2013 Linus Walleij <linus.walleij@linaro.org>
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/gpio/driver.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/bitops.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/pinconf.h>
/* Since we request GPIOs from ourself */
#include <linux/pinctrl/consumer.h>
#include "pinctrl-nomadik.h"
#include "../core.h"
#include "../pinctrl-utils.h"

/*
 * The GPIO module in the Nomadik family of Systems-on-Chip is an
 * AMBA device, managing 32 pins and alternate functions.  The logic block
 * is currently used in the Nomadik and ux500.
 *
 * Symbols in this file are called "nmk_gpio" for "nomadik gpio"
 */

/*
 * pin configurations are represented by 32-bit integers:
 *
 *	bit  0.. 8 - Pin Number (512 Pins Maximum)
 *	bit  9..10 - Alternate Function Selection
 *	bit 11..12 - Pull up/down state
 *	bit     13 - Sleep mode behaviour
 *	bit     14 - Direction
 *	bit     15 - Value (if output)
 *	bit 16..18 - SLPM pull up/down state
 *	bit 19..20 - SLPM direction
 *	bit 21..22 - SLPM Value (if output)
 *	bit 23..25 - PDIS value (if input)
 *	bit	26 - Gpio mode
 *	bit	27 - Sleep mode
 *
 * to facilitate the definition, the following macros are provided
 *
 * PIN_CFG_DEFAULT - default config (0):
 *		     pull up/down = disabled
 *		     sleep mode = input/wakeup
 *		     direction = input
 *		     value = low
 *		     SLPM direction = same as normal
 *		     SLPM pull = same as normal
 *		     SLPM value = same as normal
 *
 * PIN_CFG	   - default config with alternate function
 */

typedef unsigned long pin_cfg_t;

#define PIN_NUM_MASK		0x1ff
#define PIN_NUM(x)		((x) & PIN_NUM_MASK)

#define PIN_ALT_SHIFT		9
#define PIN_ALT_MASK		(0x3 << PIN_ALT_SHIFT)
#define PIN_ALT(x)		(((x) & PIN_ALT_MASK) >> PIN_ALT_SHIFT)
#define PIN_GPIO		(NMK_GPIO_ALT_GPIO << PIN_ALT_SHIFT)
#define PIN_ALT_A		(NMK_GPIO_ALT_A << PIN_ALT_SHIFT)
#define PIN_ALT_B		(NMK_GPIO_ALT_B << PIN_ALT_SHIFT)
#define PIN_ALT_C		(NMK_GPIO_ALT_C << PIN_ALT_SHIFT)

#define PIN_PULL_SHIFT		11
#define PIN_PULL_MASK		(0x3 << PIN_PULL_SHIFT)
#define PIN_PULL(x)		(((x) & PIN_PULL_MASK) >> PIN_PULL_SHIFT)
#define PIN_PULL_NONE		(NMK_GPIO_PULL_NONE << PIN_PULL_SHIFT)
#define PIN_PULL_UP		(NMK_GPIO_PULL_UP << PIN_PULL_SHIFT)
#define PIN_PULL_DOWN		(NMK_GPIO_PULL_DOWN << PIN_PULL_SHIFT)

#define PIN_SLPM_SHIFT		13
#define PIN_SLPM_MASK		(0x1 << PIN_SLPM_SHIFT)
#define PIN_SLPM(x)		(((x) & PIN_SLPM_MASK) >> PIN_SLPM_SHIFT)
#define PIN_SLPM_MAKE_INPUT	(NMK_GPIO_SLPM_INPUT << PIN_SLPM_SHIFT)
#define PIN_SLPM_NOCHANGE	(NMK_GPIO_SLPM_NOCHANGE << PIN_SLPM_SHIFT)
/* These two replace the above in DB8500v2+ */
#define PIN_SLPM_WAKEUP_ENABLE	(NMK_GPIO_SLPM_WAKEUP_ENABLE << PIN_SLPM_SHIFT)
#define PIN_SLPM_WAKEUP_DISABLE	(NMK_GPIO_SLPM_WAKEUP_DISABLE << PIN_SLPM_SHIFT)
#define PIN_SLPM_USE_MUX_SETTINGS_IN_SLEEP PIN_SLPM_WAKEUP_DISABLE

#define PIN_SLPM_GPIO  PIN_SLPM_WAKEUP_ENABLE /* In SLPM, pin is a gpio */
#define PIN_SLPM_ALTFUNC PIN_SLPM_WAKEUP_DISABLE /* In SLPM, pin is altfunc */

#define PIN_DIR_SHIFT		14
#define PIN_DIR_MASK		(0x1 << PIN_DIR_SHIFT)
#define PIN_DIR(x)		(((x) & PIN_DIR_MASK) >> PIN_DIR_SHIFT)
#define PIN_DIR_INPUT		(0 << PIN_DIR_SHIFT)
#define PIN_DIR_OUTPUT		(1 << PIN_DIR_SHIFT)

#define PIN_VAL_SHIFT		15
#define PIN_VAL_MASK		(0x1 << PIN_VAL_SHIFT)
#define PIN_VAL(x)		(((x) & PIN_VAL_MASK) >> PIN_VAL_SHIFT)
#define PIN_VAL_LOW		(0 << PIN_VAL_SHIFT)
#define PIN_VAL_HIGH		(1 << PIN_VAL_SHIFT)

#define PIN_SLPM_PULL_SHIFT	16
#define PIN_SLPM_PULL_MASK	(0x7 << PIN_SLPM_PULL_SHIFT)
#define PIN_SLPM_PULL(x)	\
	(((x) & PIN_SLPM_PULL_MASK) >> PIN_SLPM_PULL_SHIFT)
#define PIN_SLPM_PULL_NONE	\
	((1 + NMK_GPIO_PULL_NONE) << PIN_SLPM_PULL_SHIFT)
#define PIN_SLPM_PULL_UP	\
	((1 + NMK_GPIO_PULL_UP) << PIN_SLPM_PULL_SHIFT)
#define PIN_SLPM_PULL_DOWN	\
	((1 + NMK_GPIO_PULL_DOWN) << PIN_SLPM_PULL_SHIFT)

#define PIN_SLPM_DIR_SHIFT	19
#define PIN_SLPM_DIR_MASK	(0x3 << PIN_SLPM_DIR_SHIFT)
#define PIN_SLPM_DIR(x)		\
	(((x) & PIN_SLPM_DIR_MASK) >> PIN_SLPM_DIR_SHIFT)
#define PIN_SLPM_DIR_INPUT	((1 + 0) << PIN_SLPM_DIR_SHIFT)
#define PIN_SLPM_DIR_OUTPUT	((1 + 1) << PIN_SLPM_DIR_SHIFT)

#define PIN_SLPM_VAL_SHIFT	21
#define PIN_SLPM_VAL_MASK	(0x3 << PIN_SLPM_VAL_SHIFT)
#define PIN_SLPM_VAL(x)		\
	(((x) & PIN_SLPM_VAL_MASK) >> PIN_SLPM_VAL_SHIFT)
#define PIN_SLPM_VAL_LOW	((1 + 0) << PIN_SLPM_VAL_SHIFT)
#define PIN_SLPM_VAL_HIGH	((1 + 1) << PIN_SLPM_VAL_SHIFT)

#define PIN_SLPM_PDIS_SHIFT		23
#define PIN_SLPM_PDIS_MASK		(0x3 << PIN_SLPM_PDIS_SHIFT)
#define PIN_SLPM_PDIS(x)	\
	(((x) & PIN_SLPM_PDIS_MASK) >> PIN_SLPM_PDIS_SHIFT)
#define PIN_SLPM_PDIS_NO_CHANGE		(0 << PIN_SLPM_PDIS_SHIFT)
#define PIN_SLPM_PDIS_DISABLED		(1 << PIN_SLPM_PDIS_SHIFT)
#define PIN_SLPM_PDIS_ENABLED		(2 << PIN_SLPM_PDIS_SHIFT)

#define PIN_LOWEMI_SHIFT	25
#define PIN_LOWEMI_MASK		(0x1 << PIN_LOWEMI_SHIFT)
#define PIN_LOWEMI(x)		(((x) & PIN_LOWEMI_MASK) >> PIN_LOWEMI_SHIFT)
#define PIN_LOWEMI_DISABLED	(0 << PIN_LOWEMI_SHIFT)
#define PIN_LOWEMI_ENABLED	(1 << PIN_LOWEMI_SHIFT)

#define PIN_GPIOMODE_SHIFT	26
#define PIN_GPIOMODE_MASK	(0x1 << PIN_GPIOMODE_SHIFT)
#define PIN_GPIOMODE(x)		(((x) & PIN_GPIOMODE_MASK) >> PIN_GPIOMODE_SHIFT)
#define PIN_GPIOMODE_DISABLED	(0 << PIN_GPIOMODE_SHIFT)
#define PIN_GPIOMODE_ENABLED	(1 << PIN_GPIOMODE_SHIFT)

#define PIN_SLEEPMODE_SHIFT	27
#define PIN_SLEEPMODE_MASK	(0x1 << PIN_SLEEPMODE_SHIFT)
#define PIN_SLEEPMODE(x)	(((x) & PIN_SLEEPMODE_MASK) >> PIN_SLEEPMODE_SHIFT)
#define PIN_SLEEPMODE_DISABLED	(0 << PIN_SLEEPMODE_SHIFT)
#define PIN_SLEEPMODE_ENABLED	(1 << PIN_SLEEPMODE_SHIFT)


/* Shortcuts.  Use these instead of separate DIR, PULL, and VAL.  */
#define PIN_INPUT_PULLDOWN	(PIN_DIR_INPUT | PIN_PULL_DOWN)
#define PIN_INPUT_PULLUP	(PIN_DIR_INPUT | PIN_PULL_UP)
#define PIN_INPUT_NOPULL	(PIN_DIR_INPUT | PIN_PULL_NONE)
#define PIN_OUTPUT_LOW		(PIN_DIR_OUTPUT | PIN_VAL_LOW)
#define PIN_OUTPUT_HIGH		(PIN_DIR_OUTPUT | PIN_VAL_HIGH)

#define PIN_SLPM_INPUT_PULLDOWN	(PIN_SLPM_DIR_INPUT | PIN_SLPM_PULL_DOWN)
#define PIN_SLPM_INPUT_PULLUP	(PIN_SLPM_DIR_INPUT | PIN_SLPM_PULL_UP)
#define PIN_SLPM_INPUT_NOPULL	(PIN_SLPM_DIR_INPUT | PIN_SLPM_PULL_NONE)
#define PIN_SLPM_OUTPUT_LOW	(PIN_SLPM_DIR_OUTPUT | PIN_SLPM_VAL_LOW)
#define PIN_SLPM_OUTPUT_HIGH	(PIN_SLPM_DIR_OUTPUT | PIN_SLPM_VAL_HIGH)

#define PIN_CFG_DEFAULT		(0)

#define PIN_CFG(num, alt)		\
	(PIN_CFG_DEFAULT |\
	 (PIN_NUM(num) | PIN_##alt))

#define PIN_CFG_INPUT(num, alt, pull)		\
	(PIN_CFG_DEFAULT |\
	 (PIN_NUM(num) | PIN_##alt | PIN_INPUT_##pull))

#define PIN_CFG_OUTPUT(num, alt, val)		\
	(PIN_CFG_DEFAULT |\
	 (PIN_NUM(num) | PIN_##alt | PIN_OUTPUT_##val))

/*
 * "nmk_gpio" and "NMK_GPIO" stand for "Nomadik GPIO", leaving
 * the "gpio" namespace for generic and cross-machine functions
 */

#define GPIO_BLOCK_SHIFT 5
#define NMK_GPIO_PER_CHIP (1 << GPIO_BLOCK_SHIFT)
#define NMK_MAX_BANKS DIV_ROUND_UP(512, NMK_GPIO_PER_CHIP)

/* Register in the logic block */
#define NMK_GPIO_DAT	0x00
#define NMK_GPIO_DATS	0x04
#define NMK_GPIO_DATC	0x08
#define NMK_GPIO_PDIS	0x0c
#define NMK_GPIO_DIR	0x10
#define NMK_GPIO_DIRS	0x14
#define NMK_GPIO_DIRC	0x18
#define NMK_GPIO_SLPC	0x1c
#define NMK_GPIO_AFSLA	0x20
#define NMK_GPIO_AFSLB	0x24
#define NMK_GPIO_LOWEMI	0x28

#define NMK_GPIO_RIMSC	0x40
#define NMK_GPIO_FIMSC	0x44
#define NMK_GPIO_IS	0x48
#define NMK_GPIO_IC	0x4c
#define NMK_GPIO_RWIMSC	0x50
#define NMK_GPIO_FWIMSC	0x54
#define NMK_GPIO_WKS	0x58
/* These appear in DB8540 and later ASICs */
#define NMK_GPIO_EDGELEVEL 0x5C
#define NMK_GPIO_LEVEL	0x60


/* Pull up/down values */
enum nmk_gpio_pull {
	NMK_GPIO_PULL_NONE,
	NMK_GPIO_PULL_UP,
	NMK_GPIO_PULL_DOWN,
};

/* Sleep mode */
enum nmk_gpio_slpm {
	NMK_GPIO_SLPM_INPUT,
	NMK_GPIO_SLPM_WAKEUP_ENABLE = NMK_GPIO_SLPM_INPUT,
	NMK_GPIO_SLPM_NOCHANGE,
	NMK_GPIO_SLPM_WAKEUP_DISABLE = NMK_GPIO_SLPM_NOCHANGE,
};

struct nmk_gpio_chip {
	struct gpio_chip chip;
	struct irq_chip irqchip;
	void __iomem *addr;
	struct clk *clk;
	unsigned int bank;
	void (*set_ioforce)(bool enable);
	spinlock_t lock;
	bool sleepmode;
	/* Keep track of configured edges */
	u32 edge_rising;
	u32 edge_falling;
	u32 real_wake;
	u32 rwimsc;
	u32 fwimsc;
	u32 rimsc;
	u32 fimsc;
	u32 pull_up;
	u32 lowemi;
};

/**
 * struct nmk_pinctrl - state container for the Nomadik pin controller
 * @dev: containing device pointer
 * @pctl: corresponding pin controller device
 * @soc: SoC data for this specific chip
 * @prcm_base: PRCM register range virtual base
 */
struct nmk_pinctrl {
	struct device *dev;
	struct pinctrl_dev *pctl;
	const struct nmk_pinctrl_soc_data *soc;
	void __iomem *prcm_base;
};

static struct nmk_gpio_chip *nmk_gpio_chips[NMK_MAX_BANKS];

static DEFINE_SPINLOCK(nmk_gpio_slpm_lock);

#define NUM_BANKS ARRAY_SIZE(nmk_gpio_chips)

static void __nmk_gpio_set_mode(struct nmk_gpio_chip *nmk_chip,
				unsigned offset, int gpio_mode)
{
	u32 afunc, bfunc;

	afunc = readl(nmk_chip->addr + NMK_GPIO_AFSLA) & ~BIT(offset);
	bfunc = readl(nmk_chip->addr + NMK_GPIO_AFSLB) & ~BIT(offset);
	if (gpio_mode & NMK_GPIO_ALT_A)
		afunc |= BIT(offset);
	if (gpio_mode & NMK_GPIO_ALT_B)
		bfunc |= BIT(offset);
	writel(afunc, nmk_chip->addr + NMK_GPIO_AFSLA);
	writel(bfunc, nmk_chip->addr + NMK_GPIO_AFSLB);
}

static void __nmk_gpio_set_slpm(struct nmk_gpio_chip *nmk_chip,
				unsigned offset, enum nmk_gpio_slpm mode)
{
	u32 slpm;

	slpm = readl(nmk_chip->addr + NMK_GPIO_SLPC);
	if (mode == NMK_GPIO_SLPM_NOCHANGE)
		slpm |= BIT(offset);
	else
		slpm &= ~BIT(offset);
	writel(slpm, nmk_chip->addr + NMK_GPIO_SLPC);
}

static void __nmk_gpio_set_pull(struct nmk_gpio_chip *nmk_chip,
				unsigned offset, enum nmk_gpio_pull pull)
{
	u32 pdis;

	pdis = readl(nmk_chip->addr + NMK_GPIO_PDIS);
	if (pull == NMK_GPIO_PULL_NONE) {
		pdis |= BIT(offset);
		nmk_chip->pull_up &= ~BIT(offset);
	} else {
		pdis &= ~BIT(offset);
	}

	writel(pdis, nmk_chip->addr + NMK_GPIO_PDIS);

	if (pull == NMK_GPIO_PULL_UP) {
		nmk_chip->pull_up |= BIT(offset);
		writel(BIT(offset), nmk_chip->addr + NMK_GPIO_DATS);
	} else if (pull == NMK_GPIO_PULL_DOWN) {
		nmk_chip->pull_up &= ~BIT(offset);
		writel(BIT(offset), nmk_chip->addr + NMK_GPIO_DATC);
	}
}

static void __nmk_gpio_set_lowemi(struct nmk_gpio_chip *nmk_chip,
				  unsigned offset, bool lowemi)
{
	bool enabled = nmk_chip->lowemi & BIT(offset);

	if (lowemi == enabled)
		return;

	if (lowemi)
		nmk_chip->lowemi |= BIT(offset);
	else
		nmk_chip->lowemi &= ~BIT(offset);

	writel_relaxed(nmk_chip->lowemi,
		       nmk_chip->addr + NMK_GPIO_LOWEMI);
}

static void __nmk_gpio_make_input(struct nmk_gpio_chip *nmk_chip,
				  unsigned offset)
{
	writel(BIT(offset), nmk_chip->addr + NMK_GPIO_DIRC);
}

static void __nmk_gpio_set_output(struct nmk_gpio_chip *nmk_chip,
				  unsigned offset, int val)
{
	if (val)
		writel(BIT(offset), nmk_chip->addr + NMK_GPIO_DATS);
	else
		writel(BIT(offset), nmk_chip->addr + NMK_GPIO_DATC);
}

static void __nmk_gpio_make_output(struct nmk_gpio_chip *nmk_chip,
				  unsigned offset, int val)
{
	writel(BIT(offset), nmk_chip->addr + NMK_GPIO_DIRS);
	__nmk_gpio_set_output(nmk_chip, offset, val);
}

static void __nmk_gpio_set_mode_safe(struct nmk_gpio_chip *nmk_chip,
				     unsigned offset, int gpio_mode,
				     bool glitch)
{
	u32 rwimsc = nmk_chip->rwimsc;
	u32 fwimsc = nmk_chip->fwimsc;

	if (glitch && nmk_chip->set_ioforce) {
		u32 bit = BIT(offset);

		/* Prevent spurious wakeups */
		writel(rwimsc & ~bit, nmk_chip->addr + NMK_GPIO_RWIMSC);
		writel(fwimsc & ~bit, nmk_chip->addr + NMK_GPIO_FWIMSC);

		nmk_chip->set_ioforce(true);
	}

	__nmk_gpio_set_mode(nmk_chip, offset, gpio_mode);

	if (glitch && nmk_chip->set_ioforce) {
		nmk_chip->set_ioforce(false);

		writel(rwimsc, nmk_chip->addr + NMK_GPIO_RWIMSC);
		writel(fwimsc, nmk_chip->addr + NMK_GPIO_FWIMSC);
	}
}

static void
nmk_gpio_disable_lazy_irq(struct nmk_gpio_chip *nmk_chip, unsigned offset)
{
	u32 falling = nmk_chip->fimsc & BIT(offset);
	u32 rising = nmk_chip->rimsc & BIT(offset);
	int gpio = nmk_chip->chip.base + offset;
	int irq = irq_find_mapping(nmk_chip->chip.irq.domain, offset);
	struct irq_data *d = irq_get_irq_data(irq);

	if (!rising && !falling)
		return;

	if (!d || !irqd_irq_disabled(d))
		return;

	if (rising) {
		nmk_chip->rimsc &= ~BIT(offset);
		writel_relaxed(nmk_chip->rimsc,
			       nmk_chip->addr + NMK_GPIO_RIMSC);
	}

	if (falling) {
		nmk_chip->fimsc &= ~BIT(offset);
		writel_relaxed(nmk_chip->fimsc,
			       nmk_chip->addr + NMK_GPIO_FIMSC);
	}

	dev_dbg(nmk_chip->chip.parent, "%d: clearing interrupt mask\n", gpio);
}

static void nmk_write_masked(void __iomem *reg, u32 mask, u32 value)
{
	u32 val;

	val = readl(reg);
	val = ((val & ~mask) | (value & mask));
	writel(val, reg);
}

static void nmk_prcm_altcx_set_mode(struct nmk_pinctrl *npct,
	unsigned offset, unsigned alt_num)
{
	int i;
	u16 reg;
	u8 bit;
	u8 alt_index;
	const struct prcm_gpiocr_altcx_pin_desc *pin_desc;
	const u16 *gpiocr_regs;

	if (!npct->prcm_base)
		return;

	if (alt_num > PRCM_IDX_GPIOCR_ALTC_MAX) {
		dev_err(npct->dev, "PRCM GPIOCR: alternate-C%i is invalid\n",
			alt_num);
		return;
	}

	for (i = 0 ; i < npct->soc->npins_altcx ; i++) {
		if (npct->soc->altcx_pins[i].pin == offset)
			break;
	}
	if (i == npct->soc->npins_altcx) {
		dev_dbg(npct->dev, "PRCM GPIOCR: pin %i is not found\n",
			offset);
		return;
	}

	pin_desc = npct->soc->altcx_pins + i;
	gpiocr_regs = npct->soc->prcm_gpiocr_registers;

	/*
	 * If alt_num is NULL, just clear current ALTCx selection
	 * to make sure we come back to a pure ALTC selection
	 */
	if (!alt_num) {
		for (i = 0 ; i < PRCM_IDX_GPIOCR_ALTC_MAX ; i++) {
			if (pin_desc->altcx[i].used == true) {
				reg = gpiocr_regs[pin_desc->altcx[i].reg_index];
				bit = pin_desc->altcx[i].control_bit;
				if (readl(npct->prcm_base + reg) & BIT(bit)) {
					nmk_write_masked(npct->prcm_base + reg, BIT(bit), 0);
					dev_dbg(npct->dev,
						"PRCM GPIOCR: pin %i: alternate-C%i has been disabled\n",
						offset, i+1);
				}
			}
		}
		return;
	}

	alt_index = alt_num - 1;
	if (pin_desc->altcx[alt_index].used == false) {
		dev_warn(npct->dev,
			"PRCM GPIOCR: pin %i: alternate-C%i does not exist\n",
			offset, alt_num);
		return;
	}

	/*
	 * Check if any other ALTCx functions are activated on this pin
	 * and disable it first.
	 */
	for (i = 0 ; i < PRCM_IDX_GPIOCR_ALTC_MAX ; i++) {
		if (i == alt_index)
			continue;
		if (pin_desc->altcx[i].used == true) {
			reg = gpiocr_regs[pin_desc->altcx[i].reg_index];
			bit = pin_desc->altcx[i].control_bit;
			if (readl(npct->prcm_base + reg) & BIT(bit)) {
				nmk_write_masked(npct->prcm_base + reg, BIT(bit), 0);
				dev_dbg(npct->dev,
					"PRCM GPIOCR: pin %i: alternate-C%i has been disabled\n",
					offset, i+1);
			}
		}
	}

	reg = gpiocr_regs[pin_desc->altcx[alt_index].reg_index];
	bit = pin_desc->altcx[alt_index].control_bit;
	dev_dbg(npct->dev, "PRCM GPIOCR: pin %i: alternate-C%i has been selected\n",
		offset, alt_index+1);
	nmk_write_masked(npct->prcm_base + reg, BIT(bit), BIT(bit));
}

/*
 * Safe sequence used to switch IOs between GPIO and Alternate-C mode:
 *  - Save SLPM registers
 *  - Set SLPM=0 for the IOs you want to switch and others to 1
 *  - Configure the GPIO registers for the IOs that are being switched
 *  - Set IOFORCE=1
 *  - Modify the AFLSA/B registers for the IOs that are being switched
 *  - Set IOFORCE=0
 *  - Restore SLPM registers
 *  - Any spurious wake up event during switch sequence to be ignored and
 *    cleared
 */
static void nmk_gpio_glitch_slpm_init(unsigned int *slpm)
{
	int i;

	for (i = 0; i < NUM_BANKS; i++) {
		struct nmk_gpio_chip *chip = nmk_gpio_chips[i];
		unsigned int temp = slpm[i];

		if (!chip)
			break;

		clk_enable(chip->clk);

		slpm[i] = readl(chip->addr + NMK_GPIO_SLPC);
		writel(temp, chip->addr + NMK_GPIO_SLPC);
	}
}

static void nmk_gpio_glitch_slpm_restore(unsigned int *slpm)
{
	int i;

	for (i = 0; i < NUM_BANKS; i++) {
		struct nmk_gpio_chip *chip = nmk_gpio_chips[i];

		if (!chip)
			break;

		writel(slpm[i], chip->addr + NMK_GPIO_SLPC);

		clk_disable(chip->clk);
	}
}

static int __maybe_unused nmk_prcm_gpiocr_get_mode(struct pinctrl_dev *pctldev, int gpio)
{
	int i;
	u16 reg;
	u8 bit;
	struct nmk_pinctrl *npct = pinctrl_dev_get_drvdata(pctldev);
	const struct prcm_gpiocr_altcx_pin_desc *pin_desc;
	const u16 *gpiocr_regs;

	if (!npct->prcm_base)
		return NMK_GPIO_ALT_C;

	for (i = 0; i < npct->soc->npins_altcx; i++) {
		if (npct->soc->altcx_pins[i].pin == gpio)
			break;
	}
	if (i == npct->soc->npins_altcx)
		return NMK_GPIO_ALT_C;

	pin_desc = npct->soc->altcx_pins + i;
	gpiocr_regs = npct->soc->prcm_gpiocr_registers;
	for (i = 0; i < PRCM_IDX_GPIOCR_ALTC_MAX; i++) {
		if (pin_desc->altcx[i].used == true) {
			reg = gpiocr_regs[pin_desc->altcx[i].reg_index];
			bit = pin_desc->altcx[i].control_bit;
			if (readl(npct->prcm_base + reg) & BIT(bit))
				return NMK_GPIO_ALT_C+i+1;
		}
	}
	return NMK_GPIO_ALT_C;
}

/* IRQ functions */

static void nmk_gpio_irq_ack(struct irq_data *d)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(d);
	struct nmk_gpio_chip *nmk_chip = gpiochip_get_data(chip);

	clk_enable(nmk_chip->clk);
	writel(BIT(d->hwirq), nmk_chip->addr + NMK_GPIO_IC);
	clk_disable(nmk_chip->clk);
}

enum nmk_gpio_irq_type {
	NORMAL,
	WAKE,
};

static void __nmk_gpio_irq_modify(struct nmk_gpio_chip *nmk_chip,
				  int offset, enum nmk_gpio_irq_type which,
				  bool enable)
{
	u32 *rimscval;
	u32 *fimscval;
	u32 rimscreg;
	u32 fimscreg;

	if (which == NORMAL) {
		rimscreg = NMK_GPIO_RIMSC;
		fimscreg = NMK_GPIO_FIMSC;
		rimscval = &nmk_chip->rimsc;
		fimscval = &nmk_chip->fimsc;
	} else  {
		rimscreg = NMK_GPIO_RWIMSC;
		fimscreg = NMK_GPIO_FWIMSC;
		rimscval = &nmk_chip->rwimsc;
		fimscval = &nmk_chip->fwimsc;
	}

	/* we must individually set/clear the two edges */
	if (nmk_chip->edge_rising & BIT(offset)) {
		if (enable)
			*rimscval |= BIT(offset);
		else
			*rimscval &= ~BIT(offset);
		writel(*rimscval, nmk_chip->addr + rimscreg);
	}
	if (nmk_chip->edge_falling & BIT(offset)) {
		if (enable)
			*fimscval |= BIT(offset);
		else
			*fimscval &= ~BIT(offset);
		writel(*fimscval, nmk_chip->addr + fimscreg);
	}
}

static void __nmk_gpio_set_wake(struct nmk_gpio_chip *nmk_chip,
				int offset, bool on)
{
	/*
	 * Ensure WAKEUP_ENABLE is on.  No need to disable it if wakeup is
	 * disabled, since setting SLPM to 1 increases power consumption, and
	 * wakeup is anyhow controlled by the RIMSC and FIMSC registers.
	 */
	if (nmk_chip->sleepmode && on) {
		__nmk_gpio_set_slpm(nmk_chip, offset,
				    NMK_GPIO_SLPM_WAKEUP_ENABLE);
	}

	__nmk_gpio_irq_modify(nmk_chip, offset, WAKE, on);
}

static int nmk_gpio_irq_maskunmask(struct irq_data *d, bool enable)
{
	struct nmk_gpio_chip *nmk_chip;
	unsigned long flags;

	nmk_chip = irq_data_get_irq_chip_data(d);
	if (!nmk_chip)
		return -EINVAL;

	clk_enable(nmk_chip->clk);
	spin_lock_irqsave(&nmk_gpio_slpm_lock, flags);
	spin_lock(&nmk_chip->lock);

	__nmk_gpio_irq_modify(nmk_chip, d->hwirq, NORMAL, enable);

	if (!(nmk_chip->real_wake & BIT(d->hwirq)))
		__nmk_gpio_set_wake(nmk_chip, d->hwirq, enable);

	spin_unlock(&nmk_chip->lock);
	spin_unlock_irqrestore(&nmk_gpio_slpm_lock, flags);
	clk_disable(nmk_chip->clk);

	return 0;
}

static void nmk_gpio_irq_mask(struct irq_data *d)
{
	nmk_gpio_irq_maskunmask(d, false);
}

static void nmk_gpio_irq_unmask(struct irq_data *d)
{
	nmk_gpio_irq_maskunmask(d, true);
}

static int nmk_gpio_irq_set_wake(struct irq_data *d, unsigned int on)
{
	struct nmk_gpio_chip *nmk_chip;
	unsigned long flags;

	nmk_chip = irq_data_get_irq_chip_data(d);
	if (!nmk_chip)
		return -EINVAL;

	clk_enable(nmk_chip->clk);
	spin_lock_irqsave(&nmk_gpio_slpm_lock, flags);
	spin_lock(&nmk_chip->lock);

	if (irqd_irq_disabled(d))
		__nmk_gpio_set_wake(nmk_chip, d->hwirq, on);

	if (on)
		nmk_chip->real_wake |= BIT(d->hwirq);
	else
		nmk_chip->real_wake &= ~BIT(d->hwirq);

	spin_unlock(&nmk_chip->lock);
	spin_unlock_irqrestore(&nmk_gpio_slpm_lock, flags);
	clk_disable(nmk_chip->clk);

	return 0;
}

static int nmk_gpio_irq_set_type(struct irq_data *d, unsigned int type)
{
	bool enabled = !irqd_irq_disabled(d);
	bool wake = irqd_is_wakeup_set(d);
	struct nmk_gpio_chip *nmk_chip;
	unsigned long flags;

	nmk_chip = irq_data_get_irq_chip_data(d);
	if (!nmk_chip)
		return -EINVAL;
	if (type & IRQ_TYPE_LEVEL_HIGH)
		return -EINVAL;
	if (type & IRQ_TYPE_LEVEL_LOW)
		return -EINVAL;

	clk_enable(nmk_chip->clk);
	spin_lock_irqsave(&nmk_chip->lock, flags);

	if (enabled)
		__nmk_gpio_irq_modify(nmk_chip, d->hwirq, NORMAL, false);

	if (enabled || wake)
		__nmk_gpio_irq_modify(nmk_chip, d->hwirq, WAKE, false);

	nmk_chip->edge_rising &= ~BIT(d->hwirq);
	if (type & IRQ_TYPE_EDGE_RISING)
		nmk_chip->edge_rising |= BIT(d->hwirq);

	nmk_chip->edge_falling &= ~BIT(d->hwirq);
	if (type & IRQ_TYPE_EDGE_FALLING)
		nmk_chip->edge_falling |= BIT(d->hwirq);

	if (enabled)
		__nmk_gpio_irq_modify(nmk_chip, d->hwirq, NORMAL, true);

	if (enabled || wake)
		__nmk_gpio_irq_modify(nmk_chip, d->hwirq, WAKE, true);

	spin_unlock_irqrestore(&nmk_chip->lock, flags);
	clk_disable(nmk_chip->clk);

	return 0;
}

static unsigned int nmk_gpio_irq_startup(struct irq_data *d)
{
	struct nmk_gpio_chip *nmk_chip = irq_data_get_irq_chip_data(d);

	clk_enable(nmk_chip->clk);
	nmk_gpio_irq_unmask(d);
	return 0;
}

static void nmk_gpio_irq_shutdown(struct irq_data *d)
{
	struct nmk_gpio_chip *nmk_chip = irq_data_get_irq_chip_data(d);

	nmk_gpio_irq_mask(d);
	clk_disable(nmk_chip->clk);
}

static void nmk_gpio_irq_handler(struct irq_desc *desc)
{
	struct irq_chip *host_chip = irq_desc_get_chip(desc);
	struct gpio_chip *chip = irq_desc_get_handler_data(desc);
	struct nmk_gpio_chip *nmk_chip = gpiochip_get_data(chip);
	u32 status;

	chained_irq_enter(host_chip, desc);

	clk_enable(nmk_chip->clk);
	status = readl(nmk_chip->addr + NMK_GPIO_IS);
	clk_disable(nmk_chip->clk);

	while (status) {
		int bit = __ffs(status);

		generic_handle_domain_irq(chip->irq.domain, bit);
		status &= ~BIT(bit);
	}

	chained_irq_exit(host_chip, desc);
}

/* I/O Functions */

static int nmk_gpio_get_dir(struct gpio_chip *chip, unsigned offset)
{
	struct nmk_gpio_chip *nmk_chip = gpiochip_get_data(chip);
	int dir;

	clk_enable(nmk_chip->clk);

	dir = readl(nmk_chip->addr + NMK_GPIO_DIR) & BIT(offset);

	clk_disable(nmk_chip->clk);

	if (dir)
		return GPIO_LINE_DIRECTION_OUT;

	return GPIO_LINE_DIRECTION_IN;
}

static int nmk_gpio_make_input(struct gpio_chip *chip, unsigned offset)
{
	struct nmk_gpio_chip *nmk_chip = gpiochip_get_data(chip);

	clk_enable(nmk_chip->clk);

	writel(BIT(offset), nmk_chip->addr + NMK_GPIO_DIRC);

	clk_disable(nmk_chip->clk);

	return 0;
}

static int nmk_gpio_get_input(struct gpio_chip *chip, unsigned offset)
{
	struct nmk_gpio_chip *nmk_chip = gpiochip_get_data(chip);
	int value;

	clk_enable(nmk_chip->clk);

	value = !!(readl(nmk_chip->addr + NMK_GPIO_DAT) & BIT(offset));

	clk_disable(nmk_chip->clk);

	return value;
}

static void nmk_gpio_set_output(struct gpio_chip *chip, unsigned offset,
				int val)
{
	struct nmk_gpio_chip *nmk_chip = gpiochip_get_data(chip);

	clk_enable(nmk_chip->clk);

	__nmk_gpio_set_output(nmk_chip, offset, val);

	clk_disable(nmk_chip->clk);
}

static int nmk_gpio_make_output(struct gpio_chip *chip, unsigned offset,
				int val)
{
	struct nmk_gpio_chip *nmk_chip = gpiochip_get_data(chip);

	clk_enable(nmk_chip->clk);

	__nmk_gpio_make_output(nmk_chip, offset, val);

	clk_disable(nmk_chip->clk);

	return 0;
}

#ifdef CONFIG_DEBUG_FS
static int nmk_gpio_get_mode(struct nmk_gpio_chip *nmk_chip, int offset)
{
	u32 afunc, bfunc;

	clk_enable(nmk_chip->clk);

	afunc = readl(nmk_chip->addr + NMK_GPIO_AFSLA) & BIT(offset);
	bfunc = readl(nmk_chip->addr + NMK_GPIO_AFSLB) & BIT(offset);

	clk_disable(nmk_chip->clk);

	return (afunc ? NMK_GPIO_ALT_A : 0) | (bfunc ? NMK_GPIO_ALT_B : 0);
}

#include <linux/seq_file.h>

static void nmk_gpio_dbg_show_one(struct seq_file *s,
	struct pinctrl_dev *pctldev, struct gpio_chip *chip,
	unsigned offset, unsigned gpio)
{
	const char *label = gpiochip_is_requested(chip, offset);
	struct nmk_gpio_chip *nmk_chip = gpiochip_get_data(chip);
	int mode;
	bool is_out;
	bool data_out;
	bool pull;
	const char *modes[] = {
		[NMK_GPIO_ALT_GPIO]	= "gpio",
		[NMK_GPIO_ALT_A]	= "altA",
		[NMK_GPIO_ALT_B]	= "altB",
		[NMK_GPIO_ALT_C]	= "altC",
		[NMK_GPIO_ALT_C+1]	= "altC1",
		[NMK_GPIO_ALT_C+2]	= "altC2",
		[NMK_GPIO_ALT_C+3]	= "altC3",
		[NMK_GPIO_ALT_C+4]	= "altC4",
	};

	clk_enable(nmk_chip->clk);
	is_out = !!(readl(nmk_chip->addr + NMK_GPIO_DIR) & BIT(offset));
	pull = !(readl(nmk_chip->addr + NMK_GPIO_PDIS) & BIT(offset));
	data_out = !!(readl(nmk_chip->addr + NMK_GPIO_DAT) & BIT(offset));
	mode = nmk_gpio_get_mode(nmk_chip, offset);
	if ((mode == NMK_GPIO_ALT_C) && pctldev)
		mode = nmk_prcm_gpiocr_get_mode(pctldev, gpio);

	if (is_out) {
		seq_printf(s, " gpio-%-3d (%-20.20s) out %s           %s",
			   gpio,
			   label ?: "(none)",
			   data_out ? "hi" : "lo",
			   (mode < 0) ? "unknown" : modes[mode]);
	} else {
		int irq = chip->to_irq(chip, offset);
		const int pullidx = pull ? 1 : 0;
		int val;
		static const char * const pulls[] = {
			"none        ",
			"pull enabled",
		};

		seq_printf(s, " gpio-%-3d (%-20.20s) in  %s %s",
			   gpio,
			   label ?: "(none)",
			   pulls[pullidx],
			   (mode < 0) ? "unknown" : modes[mode]);

		val = nmk_gpio_get_input(chip, offset);
		seq_printf(s, " VAL %d", val);

		/*
		 * This races with request_irq(), set_irq_type(),
		 * and set_irq_wake() ... but those are "rare".
		 */
		if (irq > 0 && irq_has_action(irq)) {
			char *trigger;
			bool wake;

			if (nmk_chip->edge_rising & BIT(offset))
				trigger = "edge-rising";
			else if (nmk_chip->edge_falling & BIT(offset))
				trigger = "edge-falling";
			else
				trigger = "edge-undefined";

			wake = !!(nmk_chip->real_wake & BIT(offset));

			seq_printf(s, " irq-%d %s%s",
				   irq, trigger, wake ? " wakeup" : "");
		}
	}
	clk_disable(nmk_chip->clk);
}

static void nmk_gpio_dbg_show(struct seq_file *s, struct gpio_chip *chip)
{
	unsigned		i;
	unsigned		gpio = chip->base;

	for (i = 0; i < chip->ngpio; i++, gpio++) {
		nmk_gpio_dbg_show_one(s, NULL, chip, i, gpio);
		seq_printf(s, "\n");
	}
}

#else
static inline void nmk_gpio_dbg_show_one(struct seq_file *s,
					 struct pinctrl_dev *pctldev,
					 struct gpio_chip *chip,
					 unsigned offset, unsigned gpio)
{
}
#define nmk_gpio_dbg_show	NULL
#endif

/*
 * We will allocate memory for the state container using devm* allocators
 * binding to the first device reaching this point, it doesn't matter if
 * it is the pin controller or GPIO driver. However we need to use the right
 * platform device when looking up resources so pay attention to pdev.
 */
static struct nmk_gpio_chip *nmk_gpio_populate_chip(struct device_node *np,
						struct platform_device *pdev)
{
	struct nmk_gpio_chip *nmk_chip;
	struct platform_device *gpio_pdev;
	struct gpio_chip *chip;
	struct resource *res;
	struct clk *clk;
	void __iomem *base;
	u32 id;

	gpio_pdev = of_find_device_by_node(np);
	if (!gpio_pdev) {
		pr_err("populate \"%pOFn\": device not found\n", np);
		return ERR_PTR(-ENODEV);
	}
	if (of_property_read_u32(np, "gpio-bank", &id)) {
		dev_err(&pdev->dev, "populate: gpio-bank property not found\n");
		platform_device_put(gpio_pdev);
		return ERR_PTR(-EINVAL);
	}

	/* Already populated? */
	nmk_chip = nmk_gpio_chips[id];
	if (nmk_chip) {
		platform_device_put(gpio_pdev);
		return nmk_chip;
	}

	nmk_chip = devm_kzalloc(&pdev->dev, sizeof(*nmk_chip), GFP_KERNEL);
	if (!nmk_chip) {
		platform_device_put(gpio_pdev);
		return ERR_PTR(-ENOMEM);
	}

	nmk_chip->bank = id;
	chip = &nmk_chip->chip;
	chip->base = id * NMK_GPIO_PER_CHIP;
	chip->ngpio = NMK_GPIO_PER_CHIP;
	chip->label = dev_name(&gpio_pdev->dev);
	chip->parent = &gpio_pdev->dev;

	res = platform_get_resource(gpio_pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base)) {
		platform_device_put(gpio_pdev);
		return ERR_CAST(base);
	}
	nmk_chip->addr = base;

	clk = clk_get(&gpio_pdev->dev, NULL);
	if (IS_ERR(clk)) {
		platform_device_put(gpio_pdev);
		return (void *) clk;
	}
	clk_prepare(clk);
	nmk_chip->clk = clk;

	BUG_ON(nmk_chip->bank >= ARRAY_SIZE(nmk_gpio_chips));
	nmk_gpio_chips[id] = nmk_chip;
	return nmk_chip;
}

static int nmk_gpio_probe(struct platform_device *dev)
{
	struct device_node *np = dev->dev.of_node;
	struct nmk_gpio_chip *nmk_chip;
	struct gpio_chip *chip;
	struct gpio_irq_chip *girq;
	struct irq_chip *irqchip;
	bool supports_sleepmode;
	int irq;
	int ret;

	nmk_chip = nmk_gpio_populate_chip(np, dev);
	if (IS_ERR(nmk_chip)) {
		dev_err(&dev->dev, "could not populate nmk chip struct\n");
		return PTR_ERR(nmk_chip);
	}

	supports_sleepmode =
		of_property_read_bool(np, "st,supports-sleepmode");

	/* Correct platform device ID */
	dev->id = nmk_chip->bank;

	irq = platform_get_irq(dev, 0);
	if (irq < 0)
		return irq;

	/*
	 * The virt address in nmk_chip->addr is in the nomadik register space,
	 * so we can simply convert the resource address, without remapping
	 */
	nmk_chip->sleepmode = supports_sleepmode;
	spin_lock_init(&nmk_chip->lock);

	chip = &nmk_chip->chip;
	chip->request = gpiochip_generic_request;
	chip->free = gpiochip_generic_free;
	chip->get_direction = nmk_gpio_get_dir;
	chip->direction_input = nmk_gpio_make_input;
	chip->get = nmk_gpio_get_input;
	chip->direction_output = nmk_gpio_make_output;
	chip->set = nmk_gpio_set_output;
	chip->dbg_show = nmk_gpio_dbg_show;
	chip->can_sleep = false;
	chip->owner = THIS_MODULE;

	irqchip = &nmk_chip->irqchip;
	irqchip->irq_ack = nmk_gpio_irq_ack;
	irqchip->irq_mask = nmk_gpio_irq_mask;
	irqchip->irq_unmask = nmk_gpio_irq_unmask;
	irqchip->irq_set_type = nmk_gpio_irq_set_type;
	irqchip->irq_set_wake = nmk_gpio_irq_set_wake;
	irqchip->irq_startup = nmk_gpio_irq_startup;
	irqchip->irq_shutdown = nmk_gpio_irq_shutdown;
	irqchip->flags = IRQCHIP_MASK_ON_SUSPEND;
	irqchip->name = kasprintf(GFP_KERNEL, "nmk%u-%u-%u",
				  dev->id,
				  chip->base,
				  chip->base + chip->ngpio - 1);

	girq = &chip->irq;
	girq->chip = irqchip;
	girq->parent_handler = nmk_gpio_irq_handler;
	girq->num_parents = 1;
	girq->parents = devm_kcalloc(&dev->dev, 1,
				     sizeof(*girq->parents),
				     GFP_KERNEL);
	if (!girq->parents)
		return -ENOMEM;
	girq->parents[0] = irq;
	girq->default_type = IRQ_TYPE_NONE;
	girq->handler = handle_edge_irq;

	clk_enable(nmk_chip->clk);
	nmk_chip->lowemi = readl_relaxed(nmk_chip->addr + NMK_GPIO_LOWEMI);
	clk_disable(nmk_chip->clk);
	chip->of_node = np;

	ret = gpiochip_add_data(chip, nmk_chip);
	if (ret)
		return ret;

	platform_set_drvdata(dev, nmk_chip);

	dev_info(&dev->dev, "chip registered\n");

	return 0;
}

static int nmk_get_groups_cnt(struct pinctrl_dev *pctldev)
{
	struct nmk_pinctrl *npct = pinctrl_dev_get_drvdata(pctldev);

	return npct->soc->ngroups;
}

static const char *nmk_get_group_name(struct pinctrl_dev *pctldev,
				       unsigned selector)
{
	struct nmk_pinctrl *npct = pinctrl_dev_get_drvdata(pctldev);

	return npct->soc->groups[selector].name;
}

static int nmk_get_group_pins(struct pinctrl_dev *pctldev, unsigned selector,
			      const unsigned **pins,
			      unsigned *num_pins)
{
	struct nmk_pinctrl *npct = pinctrl_dev_get_drvdata(pctldev);

	*pins = npct->soc->groups[selector].pins;
	*num_pins = npct->soc->groups[selector].npins;
	return 0;
}

static struct nmk_gpio_chip *find_nmk_gpio_from_pin(unsigned pin)
{
	int i;
	struct nmk_gpio_chip *nmk_gpio;

	for(i = 0; i < NMK_MAX_BANKS; i++) {
		nmk_gpio = nmk_gpio_chips[i];
		if (!nmk_gpio)
			continue;
		if (pin >= nmk_gpio->chip.base &&
			pin < nmk_gpio->chip.base + nmk_gpio->chip.ngpio)
			return nmk_gpio;
	}
	return NULL;
}

static struct gpio_chip *find_gc_from_pin(unsigned pin)
{
	struct nmk_gpio_chip *nmk_gpio = find_nmk_gpio_from_pin(pin);

	if (nmk_gpio)
		return &nmk_gpio->chip;
	return NULL;
}

static void nmk_pin_dbg_show(struct pinctrl_dev *pctldev, struct seq_file *s,
		   unsigned offset)
{
	struct gpio_chip *chip = find_gc_from_pin(offset);

	if (!chip) {
		seq_printf(s, "invalid pin offset");
		return;
	}
	nmk_gpio_dbg_show_one(s, pctldev, chip, offset - chip->base, offset);
}

static int nmk_dt_add_map_mux(struct pinctrl_map **map, unsigned *reserved_maps,
		unsigned *num_maps, const char *group,
		const char *function)
{
	if (*num_maps == *reserved_maps)
		return -ENOSPC;

	(*map)[*num_maps].type = PIN_MAP_TYPE_MUX_GROUP;
	(*map)[*num_maps].data.mux.group = group;
	(*map)[*num_maps].data.mux.function = function;
	(*num_maps)++;

	return 0;
}

static int nmk_dt_add_map_configs(struct pinctrl_map **map,
		unsigned *reserved_maps,
		unsigned *num_maps, const char *group,
		unsigned long *configs, unsigned num_configs)
{
	unsigned long *dup_configs;

	if (*num_maps == *reserved_maps)
		return -ENOSPC;

	dup_configs = kmemdup(configs, num_configs * sizeof(*dup_configs),
			      GFP_KERNEL);
	if (!dup_configs)
		return -ENOMEM;

	(*map)[*num_maps].type = PIN_MAP_TYPE_CONFIGS_PIN;

	(*map)[*num_maps].data.configs.group_or_pin = group;
	(*map)[*num_maps].data.configs.configs = dup_configs;
	(*map)[*num_maps].data.configs.num_configs = num_configs;
	(*num_maps)++;

	return 0;
}

#define NMK_CONFIG_PIN(x, y) { .property = x, .config = y, }
#define NMK_CONFIG_PIN_ARRAY(x, y) { .property = x, .choice = y, \
	.size = ARRAY_SIZE(y), }

static const unsigned long nmk_pin_input_modes[] = {
	PIN_INPUT_NOPULL,
	PIN_INPUT_PULLUP,
	PIN_INPUT_PULLDOWN,
};

static const unsigned long nmk_pin_output_modes[] = {
	PIN_OUTPUT_LOW,
	PIN_OUTPUT_HIGH,
	PIN_DIR_OUTPUT,
};

static const unsigned long nmk_pin_sleep_modes[] = {
	PIN_SLEEPMODE_DISABLED,
	PIN_SLEEPMODE_ENABLED,
};

static const unsigned long nmk_pin_sleep_input_modes[] = {
	PIN_SLPM_INPUT_NOPULL,
	PIN_SLPM_INPUT_PULLUP,
	PIN_SLPM_INPUT_PULLDOWN,
	PIN_SLPM_DIR_INPUT,
};

static const unsigned long nmk_pin_sleep_output_modes[] = {
	PIN_SLPM_OUTPUT_LOW,
	PIN_SLPM_OUTPUT_HIGH,
	PIN_SLPM_DIR_OUTPUT,
};

static const unsigned long nmk_pin_sleep_wakeup_modes[] = {
	PIN_SLPM_WAKEUP_DISABLE,
	PIN_SLPM_WAKEUP_ENABLE,
};

static const unsigned long nmk_pin_gpio_modes[] = {
	PIN_GPIOMODE_DISABLED,
	PIN_GPIOMODE_ENABLED,
};

static const unsigned long nmk_pin_sleep_pdis_modes[] = {
	PIN_SLPM_PDIS_DISABLED,
	PIN_SLPM_PDIS_ENABLED,
};

struct nmk_cfg_param {
	const char *property;
	unsigned long config;
	const unsigned long *choice;
	int size;
};

static const struct nmk_cfg_param nmk_cfg_params[] = {
	NMK_CONFIG_PIN_ARRAY("ste,input",		nmk_pin_input_modes),
	NMK_CONFIG_PIN_ARRAY("ste,output",		nmk_pin_output_modes),
	NMK_CONFIG_PIN_ARRAY("ste,sleep",		nmk_pin_sleep_modes),
	NMK_CONFIG_PIN_ARRAY("ste,sleep-input",		nmk_pin_sleep_input_modes),
	NMK_CONFIG_PIN_ARRAY("ste,sleep-output",	nmk_pin_sleep_output_modes),
	NMK_CONFIG_PIN_ARRAY("ste,sleep-wakeup",	nmk_pin_sleep_wakeup_modes),
	NMK_CONFIG_PIN_ARRAY("ste,gpio",		nmk_pin_gpio_modes),
	NMK_CONFIG_PIN_ARRAY("ste,sleep-pull-disable",	nmk_pin_sleep_pdis_modes),
};

static int nmk_dt_pin_config(int index, int val, unsigned long *config)
{
	if (nmk_cfg_params[index].choice == NULL)
		*config = nmk_cfg_params[index].config;
	else {
		/* test if out of range */
		if  (val < nmk_cfg_params[index].size) {
			*config = nmk_cfg_params[index].config |
				nmk_cfg_params[index].choice[val];
		}
	}
	return 0;
}

static const char *nmk_find_pin_name(struct pinctrl_dev *pctldev, const char *pin_name)
{
	int i, pin_number;
	struct nmk_pinctrl *npct = pinctrl_dev_get_drvdata(pctldev);

	if (sscanf((char *)pin_name, "GPIO%d", &pin_number) == 1)
		for (i = 0; i < npct->soc->npins; i++)
			if (npct->soc->pins[i].number == pin_number)
				return npct->soc->pins[i].name;
	return NULL;
}

static bool nmk_pinctrl_dt_get_config(struct device_node *np,
		unsigned long *configs)
{
	bool has_config = 0;
	unsigned long cfg = 0;
	int i, val, ret;

	for (i = 0; i < ARRAY_SIZE(nmk_cfg_params); i++) {
		ret = of_property_read_u32(np,
				nmk_cfg_params[i].property, &val);
		if (ret != -EINVAL) {
			if (nmk_dt_pin_config(i, val, &cfg) == 0) {
				*configs |= cfg;
				has_config = 1;
			}
		}
	}

	return has_config;
}

static int nmk_pinctrl_dt_subnode_to_map(struct pinctrl_dev *pctldev,
		struct device_node *np,
		struct pinctrl_map **map,
		unsigned *reserved_maps,
		unsigned *num_maps)
{
	int ret;
	const char *function = NULL;
	unsigned long configs = 0;
	bool has_config = 0;
	struct property *prop;
	struct device_node *np_config;

	ret = of_property_read_string(np, "function", &function);
	if (ret >= 0) {
		const char *group;

		ret = of_property_count_strings(np, "groups");
		if (ret < 0)
			goto exit;

		ret = pinctrl_utils_reserve_map(pctldev, map,
						reserved_maps,
						num_maps, ret);
		if (ret < 0)
			goto exit;

		of_property_for_each_string(np, "groups", prop, group) {
			ret = nmk_dt_add_map_mux(map, reserved_maps, num_maps,
					  group, function);
			if (ret < 0)
				goto exit;
		}
	}

	has_config = nmk_pinctrl_dt_get_config(np, &configs);
	np_config = of_parse_phandle(np, "ste,config", 0);
	if (np_config)
		has_config |= nmk_pinctrl_dt_get_config(np_config, &configs);
	if (has_config) {
		const char *gpio_name;
		const char *pin;

		ret = of_property_count_strings(np, "pins");
		if (ret < 0)
			goto exit;
		ret = pinctrl_utils_reserve_map(pctldev, map,
						reserved_maps,
						num_maps, ret);
		if (ret < 0)
			goto exit;

		of_property_for_each_string(np, "pins", prop, pin) {
			gpio_name = nmk_find_pin_name(pctldev, pin);

			ret = nmk_dt_add_map_configs(map, reserved_maps,
						     num_maps,
						     gpio_name, &configs, 1);
			if (ret < 0)
				goto exit;
		}
	}

exit:
	return ret;
}

static int nmk_pinctrl_dt_node_to_map(struct pinctrl_dev *pctldev,
				 struct device_node *np_config,
				 struct pinctrl_map **map, unsigned *num_maps)
{
	unsigned reserved_maps;
	struct device_node *np;
	int ret;

	reserved_maps = 0;
	*map = NULL;
	*num_maps = 0;

	for_each_child_of_node(np_config, np) {
		ret = nmk_pinctrl_dt_subnode_to_map(pctldev, np, map,
				&reserved_maps, num_maps);
		if (ret < 0) {
			pinctrl_utils_free_map(pctldev, *map, *num_maps);
			of_node_put(np);
			return ret;
		}
	}

	return 0;
}

static const struct pinctrl_ops nmk_pinctrl_ops = {
	.get_groups_count = nmk_get_groups_cnt,
	.get_group_name = nmk_get_group_name,
	.get_group_pins = nmk_get_group_pins,
	.pin_dbg_show = nmk_pin_dbg_show,
	.dt_node_to_map = nmk_pinctrl_dt_node_to_map,
	.dt_free_map = pinctrl_utils_free_map,
};

static int nmk_pmx_get_funcs_cnt(struct pinctrl_dev *pctldev)
{
	struct nmk_pinctrl *npct = pinctrl_dev_get_drvdata(pctldev);

	return npct->soc->nfunctions;
}

static const char *nmk_pmx_get_func_name(struct pinctrl_dev *pctldev,
					 unsigned function)
{
	struct nmk_pinctrl *npct = pinctrl_dev_get_drvdata(pctldev);

	return npct->soc->functions[function].name;
}

static int nmk_pmx_get_func_groups(struct pinctrl_dev *pctldev,
				   unsigned function,
				   const char * const **groups,
				   unsigned * const num_groups)
{
	struct nmk_pinctrl *npct = pinctrl_dev_get_drvdata(pctldev);

	*groups = npct->soc->functions[function].groups;
	*num_groups = npct->soc->functions[function].ngroups;

	return 0;
}

static int nmk_pmx_set(struct pinctrl_dev *pctldev, unsigned function,
		       unsigned group)
{
	struct nmk_pinctrl *npct = pinctrl_dev_get_drvdata(pctldev);
	const struct nmk_pingroup *g;
	static unsigned int slpm[NUM_BANKS];
	unsigned long flags = 0;
	bool glitch;
	int ret = -EINVAL;
	int i;

	g = &npct->soc->groups[group];

	if (g->altsetting < 0)
		return -EINVAL;

	dev_dbg(npct->dev, "enable group %s, %u pins\n", g->name, g->npins);

	/*
	 * If we're setting altfunc C by setting both AFSLA and AFSLB to 1,
	 * we may pass through an undesired state. In this case we take
	 * some extra care.
	 *
	 * Safe sequence used to switch IOs between GPIO and Alternate-C mode:
	 *  - Save SLPM registers (since we have a shadow register in the
	 *    nmk_chip we're using that as backup)
	 *  - Set SLPM=0 for the IOs you want to switch and others to 1
	 *  - Configure the GPIO registers for the IOs that are being switched
	 *  - Set IOFORCE=1
	 *  - Modify the AFLSA/B registers for the IOs that are being switched
	 *  - Set IOFORCE=0
	 *  - Restore SLPM registers
	 *  - Any spurious wake up event during switch sequence to be ignored
	 *    and cleared
	 *
	 * We REALLY need to save ALL slpm registers, because the external
	 * IOFORCE will switch *all* ports to their sleepmode setting to as
	 * to avoid glitches. (Not just one port!)
	 */
	glitch = ((g->altsetting & NMK_GPIO_ALT_C) == NMK_GPIO_ALT_C);

	if (glitch) {
		spin_lock_irqsave(&nmk_gpio_slpm_lock, flags);

		/* Initially don't put any pins to sleep when switching */
		memset(slpm, 0xff, sizeof(slpm));

		/*
		 * Then mask the pins that need to be sleeping now when we're
		 * switching to the ALT C function.
		 */
		for (i = 0; i < g->npins; i++)
			slpm[g->pins[i] / NMK_GPIO_PER_CHIP] &= ~BIT(g->pins[i]);
		nmk_gpio_glitch_slpm_init(slpm);
	}

	for (i = 0; i < g->npins; i++) {
		struct nmk_gpio_chip *nmk_chip;
		unsigned bit;

		nmk_chip = find_nmk_gpio_from_pin(g->pins[i]);
		if (!nmk_chip) {
			dev_err(npct->dev,
				"invalid pin offset %d in group %s at index %d\n",
				g->pins[i], g->name, i);
			goto out_glitch;
		}
		dev_dbg(npct->dev, "setting pin %d to altsetting %d\n", g->pins[i], g->altsetting);

		clk_enable(nmk_chip->clk);
		bit = g->pins[i] % NMK_GPIO_PER_CHIP;
		/*
		 * If the pin is switching to altfunc, and there was an
		 * interrupt installed on it which has been lazy disabled,
		 * actually mask the interrupt to prevent spurious interrupts
		 * that would occur while the pin is under control of the
		 * peripheral. Only SKE does this.
		 */
		nmk_gpio_disable_lazy_irq(nmk_chip, bit);

		__nmk_gpio_set_mode_safe(nmk_chip, bit,
			(g->altsetting & NMK_GPIO_ALT_C), glitch);
		clk_disable(nmk_chip->clk);

		/*
		 * Call PRCM GPIOCR config function in case ALTC
		 * has been selected:
		 * - If selection is a ALTCx, some bits in PRCM GPIOCR registers
		 *   must be set.
		 * - If selection is pure ALTC and previous selection was ALTCx,
		 *   then some bits in PRCM GPIOCR registers must be cleared.
		 */
		if ((g->altsetting & NMK_GPIO_ALT_C) == NMK_GPIO_ALT_C)
			nmk_prcm_altcx_set_mode(npct, g->pins[i],
				g->altsetting >> NMK_GPIO_ALT_CX_SHIFT);
	}

	/* When all pins are successfully reconfigured we get here */
	ret = 0;

out_glitch:
	if (glitch) {
		nmk_gpio_glitch_slpm_restore(slpm);
		spin_unlock_irqrestore(&nmk_gpio_slpm_lock, flags);
	}

	return ret;
}

static int nmk_gpio_request_enable(struct pinctrl_dev *pctldev,
				   struct pinctrl_gpio_range *range,
				   unsigned offset)
{
	struct nmk_pinctrl *npct = pinctrl_dev_get_drvdata(pctldev);
	struct nmk_gpio_chip *nmk_chip;
	struct gpio_chip *chip;
	unsigned bit;

	if (!range) {
		dev_err(npct->dev, "invalid range\n");
		return -EINVAL;
	}
	if (!range->gc) {
		dev_err(npct->dev, "missing GPIO chip in range\n");
		return -EINVAL;
	}
	chip = range->gc;
	nmk_chip = gpiochip_get_data(chip);

	dev_dbg(npct->dev, "enable pin %u as GPIO\n", offset);

	clk_enable(nmk_chip->clk);
	bit = offset % NMK_GPIO_PER_CHIP;
	/* There is no glitch when converting any pin to GPIO */
	__nmk_gpio_set_mode(nmk_chip, bit, NMK_GPIO_ALT_GPIO);
	clk_disable(nmk_chip->clk);

	return 0;
}

static void nmk_gpio_disable_free(struct pinctrl_dev *pctldev,
				  struct pinctrl_gpio_range *range,
				  unsigned offset)
{
	struct nmk_pinctrl *npct = pinctrl_dev_get_drvdata(pctldev);

	dev_dbg(npct->dev, "disable pin %u as GPIO\n", offset);
	/* Set the pin to some default state, GPIO is usually default */
}

static const struct pinmux_ops nmk_pinmux_ops = {
	.get_functions_count = nmk_pmx_get_funcs_cnt,
	.get_function_name = nmk_pmx_get_func_name,
	.get_function_groups = nmk_pmx_get_func_groups,
	.set_mux = nmk_pmx_set,
	.gpio_request_enable = nmk_gpio_request_enable,
	.gpio_disable_free = nmk_gpio_disable_free,
	.strict = true,
};

static int nmk_pin_config_get(struct pinctrl_dev *pctldev, unsigned pin,
			      unsigned long *config)
{
	/* Not implemented */
	return -EINVAL;
}

static int nmk_pin_config_set(struct pinctrl_dev *pctldev, unsigned pin,
			      unsigned long *configs, unsigned num_configs)
{
	static const char *pullnames[] = {
		[NMK_GPIO_PULL_NONE]	= "none",
		[NMK_GPIO_PULL_UP]	= "up",
		[NMK_GPIO_PULL_DOWN]	= "down",
		[3] /* illegal */	= "??"
	};
	static const char *slpmnames[] = {
		[NMK_GPIO_SLPM_INPUT]		= "input/wakeup",
		[NMK_GPIO_SLPM_NOCHANGE]	= "no-change/no-wakeup",
	};
	struct nmk_pinctrl *npct = pinctrl_dev_get_drvdata(pctldev);
	struct nmk_gpio_chip *nmk_chip;
	unsigned bit;
	pin_cfg_t cfg;
	int pull, slpm, output, val, i;
	bool lowemi, gpiomode, sleep;

	nmk_chip = find_nmk_gpio_from_pin(pin);
	if (!nmk_chip) {
		dev_err(npct->dev,
			"invalid pin offset %d\n", pin);
		return -EINVAL;
	}

	for (i = 0; i < num_configs; i++) {
		/*
		 * The pin config contains pin number and altfunction fields,
		 * here we just ignore that part. It's being handled by the
		 * framework and pinmux callback respectively.
		 */
		cfg = (pin_cfg_t) configs[i];
		pull = PIN_PULL(cfg);
		slpm = PIN_SLPM(cfg);
		output = PIN_DIR(cfg);
		val = PIN_VAL(cfg);
		lowemi = PIN_LOWEMI(cfg);
		gpiomode = PIN_GPIOMODE(cfg);
		sleep = PIN_SLEEPMODE(cfg);

		if (sleep) {
			int slpm_pull = PIN_SLPM_PULL(cfg);
			int slpm_output = PIN_SLPM_DIR(cfg);
			int slpm_val = PIN_SLPM_VAL(cfg);

			/* All pins go into GPIO mode at sleep */
			gpiomode = true;

			/*
			 * The SLPM_* values are normal values + 1 to allow zero
			 * to mean "same as normal".
			 */
			if (slpm_pull)
				pull = slpm_pull - 1;
			if (slpm_output)
				output = slpm_output - 1;
			if (slpm_val)
				val = slpm_val - 1;

			dev_dbg(nmk_chip->chip.parent,
				"pin %d: sleep pull %s, dir %s, val %s\n",
				pin,
				slpm_pull ? pullnames[pull] : "same",
				slpm_output ? (output ? "output" : "input")
				: "same",
				slpm_val ? (val ? "high" : "low") : "same");
		}

		dev_dbg(nmk_chip->chip.parent,
			"pin %d [%#lx]: pull %s, slpm %s (%s%s), lowemi %s\n",
			pin, cfg, pullnames[pull], slpmnames[slpm],
			output ? "output " : "input",
			output ? (val ? "high" : "low") : "",
			lowemi ? "on" : "off");

		clk_enable(nmk_chip->clk);
		bit = pin % NMK_GPIO_PER_CHIP;
		if (gpiomode)
			/* No glitch when going to GPIO mode */
			__nmk_gpio_set_mode(nmk_chip, bit, NMK_GPIO_ALT_GPIO);
		if (output)
			__nmk_gpio_make_output(nmk_chip, bit, val);
		else {
			__nmk_gpio_make_input(nmk_chip, bit);
			__nmk_gpio_set_pull(nmk_chip, bit, pull);
		}
		/* TODO: isn't this only applicable on output pins? */
		__nmk_gpio_set_lowemi(nmk_chip, bit, lowemi);

		__nmk_gpio_set_slpm(nmk_chip, bit, slpm);
		clk_disable(nmk_chip->clk);
	} /* for each config */

	return 0;
}

static const struct pinconf_ops nmk_pinconf_ops = {
	.pin_config_get = nmk_pin_config_get,
	.pin_config_set = nmk_pin_config_set,
};

static struct pinctrl_desc nmk_pinctrl_desc = {
	.name = "pinctrl-nomadik",
	.pctlops = &nmk_pinctrl_ops,
	.pmxops = &nmk_pinmux_ops,
	.confops = &nmk_pinconf_ops,
	.owner = THIS_MODULE,
};

static const struct of_device_id nmk_pinctrl_match[] = {
	{
		.compatible = "stericsson,stn8815-pinctrl",
		.data = (void *)PINCTRL_NMK_STN8815,
	},
	{
		.compatible = "stericsson,db8500-pinctrl",
		.data = (void *)PINCTRL_NMK_DB8500,
	},
	{
		.compatible = "stericsson,db8540-pinctrl",
		.data = (void *)PINCTRL_NMK_DB8540,
	},
	{},
};

#ifdef CONFIG_PM_SLEEP
static int nmk_pinctrl_suspend(struct device *dev)
{
	struct nmk_pinctrl *npct;

	npct = dev_get_drvdata(dev);
	if (!npct)
		return -EINVAL;

	return pinctrl_force_sleep(npct->pctl);
}

static int nmk_pinctrl_resume(struct device *dev)
{
	struct nmk_pinctrl *npct;

	npct = dev_get_drvdata(dev);
	if (!npct)
		return -EINVAL;

	return pinctrl_force_default(npct->pctl);
}
#endif

static int nmk_pinctrl_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	struct device_node *np = pdev->dev.of_node;
	struct device_node *prcm_np;
	struct nmk_pinctrl *npct;
	unsigned int version = 0;
	int i;

	npct = devm_kzalloc(&pdev->dev, sizeof(*npct), GFP_KERNEL);
	if (!npct)
		return -ENOMEM;

	match = of_match_device(nmk_pinctrl_match, &pdev->dev);
	if (!match)
		return -ENODEV;
	version = (unsigned int) match->data;

	/* Poke in other ASIC variants here */
	if (version == PINCTRL_NMK_STN8815)
		nmk_pinctrl_stn8815_init(&npct->soc);
	if (version == PINCTRL_NMK_DB8500)
		nmk_pinctrl_db8500_init(&npct->soc);
	if (version == PINCTRL_NMK_DB8540)
		nmk_pinctrl_db8540_init(&npct->soc);

	/*
	 * Since we depend on the GPIO chips to provide clock and register base
	 * for the pin control operations, make sure that we have these
	 * populated before we continue. Follow the phandles to instantiate
	 * them. The GPIO portion of the actual hardware may be probed before
	 * or after this point: it shouldn't matter as the APIs are orthogonal.
	 */
	for (i = 0; i < NMK_MAX_BANKS; i++) {
		struct device_node *gpio_np;
		struct nmk_gpio_chip *nmk_chip;

		gpio_np = of_parse_phandle(np, "nomadik-gpio-chips", i);
		if (gpio_np) {
			dev_info(&pdev->dev,
				 "populate NMK GPIO %d \"%pOFn\"\n",
				 i, gpio_np);
			nmk_chip = nmk_gpio_populate_chip(gpio_np, pdev);
			if (IS_ERR(nmk_chip))
				dev_err(&pdev->dev,
					"could not populate nmk chip struct "
					"- continue anyway\n");
			of_node_put(gpio_np);
		}
	}

	prcm_np = of_parse_phandle(np, "prcm", 0);
	if (prcm_np) {
		npct->prcm_base = of_iomap(prcm_np, 0);
		of_node_put(prcm_np);
	}
	if (!npct->prcm_base) {
		if (version == PINCTRL_NMK_STN8815) {
			dev_info(&pdev->dev,
				 "No PRCM base, "
				 "assuming no ALT-Cx control is available\n");
		} else {
			dev_err(&pdev->dev, "missing PRCM base address\n");
			return -EINVAL;
		}
	}

	nmk_pinctrl_desc.pins = npct->soc->pins;
	nmk_pinctrl_desc.npins = npct->soc->npins;
	npct->dev = &pdev->dev;

	npct->pctl = devm_pinctrl_register(&pdev->dev, &nmk_pinctrl_desc, npct);
	if (IS_ERR(npct->pctl)) {
		dev_err(&pdev->dev, "could not register Nomadik pinctrl driver\n");
		return PTR_ERR(npct->pctl);
	}

	platform_set_drvdata(pdev, npct);
	dev_info(&pdev->dev, "initialized Nomadik pin control driver\n");

	return 0;
}

static const struct of_device_id nmk_gpio_match[] = {
	{ .compatible = "st,nomadik-gpio", },
	{}
};

static struct platform_driver nmk_gpio_driver = {
	.driver = {
		.name = "gpio",
		.of_match_table = nmk_gpio_match,
	},
	.probe = nmk_gpio_probe,
};

static SIMPLE_DEV_PM_OPS(nmk_pinctrl_pm_ops,
			nmk_pinctrl_suspend,
			nmk_pinctrl_resume);

static struct platform_driver nmk_pinctrl_driver = {
	.driver = {
		.name = "pinctrl-nomadik",
		.of_match_table = nmk_pinctrl_match,
		.pm = &nmk_pinctrl_pm_ops,
	},
	.probe = nmk_pinctrl_probe,
};

static int __init nmk_gpio_init(void)
{
	return platform_driver_register(&nmk_gpio_driver);
}
subsys_initcall(nmk_gpio_init);

static int __init nmk_pinctrl_init(void)
{
	return platform_driver_register(&nmk_pinctrl_driver);
}
core_initcall(nmk_pinctrl_init);

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Pinmux & pinconf driver for the IP block found in the Nomadik SoC. This
 * depends on gpio-nomadik and some handling is intertwined; see nmk_gpio_chips
 * which is used by this driver to access the GPIO banks array.
 *
 * Copyright (C) 2008,2009 STMicroelectronics
 * Copyright (C) 2009 Alessandro Rubini <rubini@unipv.it>
 *   Rewritten based on work by Prafulla WADASKAR <prafulla.wadaskar@st.com>
 * Copyright (C) 2011-2013 Linus Walleij <linus.walleij@linaro.org>
 */

#include <linux/bitops.h>
#include <linux/cleanup.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/gpio/driver.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>

/* Since we request GPIOs from ourself */
#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>

#include "../core.h"
#include "../pinctrl-utils.h"

#include <linux/gpio/gpio-nomadik.h>

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

/* See nmk_gpio_populate_chip() that fills this array. */
struct nmk_gpio_chip *nmk_gpio_chips[NMK_MAX_BANKS];

DEFINE_SPINLOCK(nmk_gpio_slpm_lock);

static void __nmk_gpio_set_mode(struct nmk_gpio_chip *nmk_chip,
				unsigned int offset, int gpio_mode)
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

static void __nmk_gpio_set_pull(struct nmk_gpio_chip *nmk_chip,
				unsigned int offset, enum nmk_gpio_pull pull)
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
				  unsigned int offset, bool lowemi)
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
				  unsigned int offset)
{
	writel(BIT(offset), nmk_chip->addr + NMK_GPIO_DIRC);
}

static void __nmk_gpio_set_mode_safe(struct nmk_gpio_chip *nmk_chip,
				     unsigned int offset, int gpio_mode,
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
nmk_gpio_disable_lazy_irq(struct nmk_gpio_chip *nmk_chip, unsigned int offset)
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
				    unsigned int offset, unsigned int alt_num)
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
			if (pin_desc->altcx[i].used) {
				reg = gpiocr_regs[pin_desc->altcx[i].reg_index];
				bit = pin_desc->altcx[i].control_bit;
				if (readl(npct->prcm_base + reg) & BIT(bit)) {
					nmk_write_masked(npct->prcm_base + reg, BIT(bit), 0);
					dev_dbg(npct->dev,
						"PRCM GPIOCR: pin %i: alternate-C%i has been disabled\n",
						offset, i + 1);
				}
			}
		}
		return;
	}

	alt_index = alt_num - 1;
	if (!pin_desc->altcx[alt_index].used) {
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
		if (pin_desc->altcx[i].used) {
			reg = gpiocr_regs[pin_desc->altcx[i].reg_index];
			bit = pin_desc->altcx[i].control_bit;
			if (readl(npct->prcm_base + reg) & BIT(bit)) {
				nmk_write_masked(npct->prcm_base + reg, BIT(bit), 0);
				dev_dbg(npct->dev,
					"PRCM GPIOCR: pin %i: alternate-C%i has been disabled\n",
					offset, i + 1);
			}
		}
	}

	reg = gpiocr_regs[pin_desc->altcx[alt_index].reg_index];
	bit = pin_desc->altcx[alt_index].control_bit;
	dev_dbg(npct->dev, "PRCM GPIOCR: pin %i: alternate-C%i has been selected\n",
		offset, alt_index + 1);
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

	for (i = 0; i < NMK_MAX_BANKS; i++) {
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

	for (i = 0; i < NMK_MAX_BANKS; i++) {
		struct nmk_gpio_chip *chip = nmk_gpio_chips[i];

		if (!chip)
			break;

		writel(slpm[i], chip->addr + NMK_GPIO_SLPC);

		clk_disable(chip->clk);
	}
}

/* Only called by gpio-nomadik but requires knowledge of struct nmk_pinctrl. */
int __maybe_unused nmk_prcm_gpiocr_get_mode(struct pinctrl_dev *pctldev, int gpio)
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
		if (pin_desc->altcx[i].used) {
			reg = gpiocr_regs[pin_desc->altcx[i].reg_index];
			bit = pin_desc->altcx[i].control_bit;
			if (readl(npct->prcm_base + reg) & BIT(bit))
				return NMK_GPIO_ALT_C + i + 1;
		}
	}
	return NMK_GPIO_ALT_C;
}

static int nmk_get_groups_cnt(struct pinctrl_dev *pctldev)
{
	struct nmk_pinctrl *npct = pinctrl_dev_get_drvdata(pctldev);

	return npct->soc->ngroups;
}

static const char *nmk_get_group_name(struct pinctrl_dev *pctldev,
				      unsigned int selector)
{
	struct nmk_pinctrl *npct = pinctrl_dev_get_drvdata(pctldev);

	return npct->soc->groups[selector].grp.name;
}

static int nmk_get_group_pins(struct pinctrl_dev *pctldev, unsigned int selector,
			      const unsigned int **pins,
			      unsigned int *num_pins)
{
	struct nmk_pinctrl *npct = pinctrl_dev_get_drvdata(pctldev);

	*pins = npct->soc->groups[selector].grp.pins;
	*num_pins = npct->soc->groups[selector].grp.npins;
	return 0;
}

/* This makes the mapping from pin number to a GPIO chip. We also return the pin
 * offset in the GPIO chip for convenience (and to avoid a second loop).
 */
static struct nmk_gpio_chip *find_nmk_gpio_from_pin(unsigned int pin,
						    unsigned int *offset)
{
	int i, j = 0;
	struct nmk_gpio_chip *nmk_gpio;

	/* We assume that pins are allocated in bank order. */
	for (i = 0; i < NMK_MAX_BANKS; i++) {
		nmk_gpio = nmk_gpio_chips[i];
		if (!nmk_gpio)
			continue;
		if (pin >= j && pin < j + nmk_gpio->chip.ngpio) {
			if (offset)
				*offset = pin - j;
			return nmk_gpio;
		}
		j += nmk_gpio->chip.ngpio;
	}
	return NULL;
}

static struct gpio_chip *find_gc_from_pin(unsigned int pin)
{
	struct nmk_gpio_chip *nmk_gpio = find_nmk_gpio_from_pin(pin, NULL);

	if (nmk_gpio)
		return &nmk_gpio->chip;
	return NULL;
}

static void nmk_pin_dbg_show(struct pinctrl_dev *pctldev, struct seq_file *s,
			     unsigned int offset)
{
	struct gpio_chip *chip = find_gc_from_pin(offset);

	if (!chip) {
		seq_printf(s, "invalid pin offset");
		return;
	}
	nmk_gpio_dbg_show_one(s, pctldev, chip, offset - chip->base, offset);
}

static int nmk_dt_add_map_mux(struct pinctrl_map **map, unsigned int *reserved_maps,
			      unsigned int *num_maps, const char *group,
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
				  unsigned int *reserved_maps,
				  unsigned int *num_maps, const char *group,
				  unsigned long *configs, unsigned int num_configs)
{
	unsigned long *dup_configs;

	if (*num_maps == *reserved_maps)
		return -ENOSPC;

	dup_configs = kmemdup_array(configs, num_configs, sizeof(*dup_configs), GFP_KERNEL);
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
	if (!nmk_cfg_params[index].choice) {
		*config = nmk_cfg_params[index].config;
	} else {
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
		ret = of_property_read_u32(np, nmk_cfg_params[i].property, &val);
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
					 unsigned int *reserved_maps,
					 unsigned int *num_maps)
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
	if (np_config) {
		has_config |= nmk_pinctrl_dt_get_config(np_config, &configs);
		of_node_put(np_config);
	}
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
				      struct pinctrl_map **map,
				      unsigned int *num_maps)
{
	unsigned int reserved_maps;
	int ret;

	reserved_maps = 0;
	*map = NULL;
	*num_maps = 0;

	for_each_child_of_node_scoped(np_config, np) {
		ret = nmk_pinctrl_dt_subnode_to_map(pctldev, np, map,
						    &reserved_maps, num_maps);
		if (ret < 0) {
			pinctrl_utils_free_map(pctldev, *map, *num_maps);
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
					 unsigned int function)
{
	struct nmk_pinctrl *npct = pinctrl_dev_get_drvdata(pctldev);

	return npct->soc->functions[function].name;
}

static int nmk_pmx_get_func_groups(struct pinctrl_dev *pctldev,
				   unsigned int function,
				   const char * const **groups,
				   unsigned * const num_groups)
{
	struct nmk_pinctrl *npct = pinctrl_dev_get_drvdata(pctldev);

	*groups = npct->soc->functions[function].groups;
	*num_groups = npct->soc->functions[function].ngroups;

	return 0;
}

static int nmk_pmx_set(struct pinctrl_dev *pctldev, unsigned int function,
		       unsigned int group)
{
	struct nmk_pinctrl *npct = pinctrl_dev_get_drvdata(pctldev);
	const struct nmk_pingroup *g;
	static unsigned int slpm[NMK_MAX_BANKS];
	unsigned long flags = 0;
	bool glitch;
	int ret = -EINVAL;
	int i;

	g = &npct->soc->groups[group];

	if (g->altsetting < 0)
		return -EINVAL;

	dev_dbg(npct->dev, "enable group %s, %zu pins\n", g->grp.name, g->grp.npins);

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
		for (i = 0; i < g->grp.npins; i++) {
			struct nmk_gpio_chip *nmk_chip;
			unsigned int bit;

			nmk_chip = find_nmk_gpio_from_pin(g->grp.pins[i], &bit);
			if (!nmk_chip) {
				dev_err(npct->dev,
					"invalid pin offset %d in group %s at index %d\n",
					g->grp.pins[i], g->grp.name, i);
				goto out_pre_slpm_init;
			}

			slpm[nmk_chip->bank] &= ~BIT(bit);
		}
		nmk_gpio_glitch_slpm_init(slpm);
	}

	for (i = 0; i < g->grp.npins; i++) {
		struct nmk_gpio_chip *nmk_chip;
		unsigned int bit;

		nmk_chip = find_nmk_gpio_from_pin(g->grp.pins[i], &bit);
		if (!nmk_chip) {
			dev_err(npct->dev,
				"invalid pin offset %d in group %s at index %d\n",
				g->grp.pins[i], g->grp.name, i);
			goto out_glitch;
		}
		dev_dbg(npct->dev, "setting pin %d to altsetting %d\n",
			g->grp.pins[i], g->altsetting);

		clk_enable(nmk_chip->clk);
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
			nmk_prcm_altcx_set_mode(npct, g->grp.pins[i],
						g->altsetting >> NMK_GPIO_ALT_CX_SHIFT);
	}

	/* When all pins are successfully reconfigured we get here */
	ret = 0;

out_glitch:
	if (glitch)
		nmk_gpio_glitch_slpm_restore(slpm);
out_pre_slpm_init:
	if (glitch)
		spin_unlock_irqrestore(&nmk_gpio_slpm_lock, flags);

	return ret;
}

static int nmk_gpio_request_enable(struct pinctrl_dev *pctldev,
				   struct pinctrl_gpio_range *range,
				   unsigned int pin)
{
	struct nmk_pinctrl *npct = pinctrl_dev_get_drvdata(pctldev);
	struct nmk_gpio_chip *nmk_chip;
	struct gpio_chip *chip;
	unsigned int bit;

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

	dev_dbg(npct->dev, "enable pin %u as GPIO\n", pin);

	find_nmk_gpio_from_pin(pin, &bit);

	clk_enable(nmk_chip->clk);
	/* There is no glitch when converting any pin to GPIO */
	__nmk_gpio_set_mode(nmk_chip, bit, NMK_GPIO_ALT_GPIO);
	clk_disable(nmk_chip->clk);

	return 0;
}

static void nmk_gpio_disable_free(struct pinctrl_dev *pctldev,
				  struct pinctrl_gpio_range *range,
				  unsigned int pin)
{
	struct nmk_pinctrl *npct = pinctrl_dev_get_drvdata(pctldev);

	dev_dbg(npct->dev, "disable pin %u as GPIO\n", pin);
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

static int nmk_pin_config_get(struct pinctrl_dev *pctldev, unsigned int pin,
			      unsigned long *config)
{
	/* Not implemented */
	return -EINVAL;
}

static int nmk_pin_config_set(struct pinctrl_dev *pctldev, unsigned int pin,
			      unsigned long *configs, unsigned int num_configs)
{
	static const char * const pullnames[] = {
		[NMK_GPIO_PULL_NONE]	= "none",
		[NMK_GPIO_PULL_UP]	= "up",
		[NMK_GPIO_PULL_DOWN]	= "down",
		[3] /* illegal */	= "??"
	};
	static const char * const slpmnames[] = {
		[NMK_GPIO_SLPM_INPUT]		= "input/wakeup",
		[NMK_GPIO_SLPM_NOCHANGE]	= "no-change/no-wakeup",
	};
	struct nmk_pinctrl *npct = pinctrl_dev_get_drvdata(pctldev);
	struct nmk_gpio_chip *nmk_chip;
	unsigned int bit;
	unsigned long cfg;
	int pull, slpm, output, val, i;
	bool lowemi, gpiomode, sleep;

	nmk_chip = find_nmk_gpio_from_pin(pin, &bit);
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
		cfg = configs[i];
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
		if (gpiomode)
			/* No glitch when going to GPIO mode */
			__nmk_gpio_set_mode(nmk_chip, bit, NMK_GPIO_ALT_GPIO);
		if (output) {
			__nmk_gpio_make_output(nmk_chip, bit, val);
		} else {
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
	struct fwnode_handle *fwnode = dev_fwnode(&pdev->dev);
	struct fwnode_handle *prcm_fwnode;
	struct nmk_pinctrl *npct;
	uintptr_t version = 0;
	int i;

	npct = devm_kzalloc(&pdev->dev, sizeof(*npct), GFP_KERNEL);
	if (!npct)
		return -ENOMEM;

	version = (uintptr_t)device_get_match_data(&pdev->dev);

	/* Poke in other ASIC variants here */
	if (version == PINCTRL_NMK_STN8815)
		nmk_pinctrl_stn8815_init(&npct->soc);
	if (version == PINCTRL_NMK_DB8500)
		nmk_pinctrl_db8500_init(&npct->soc);

	/*
	 * Since we depend on the GPIO chips to provide clock and register base
	 * for the pin control operations, make sure that we have these
	 * populated before we continue. Follow the phandles to instantiate
	 * them. The GPIO portion of the actual hardware may be probed before
	 * or after this point: it shouldn't matter as the APIs are orthogonal.
	 */
	for (i = 0; i < NMK_MAX_BANKS; i++) {
		struct fwnode_handle *gpio_fwnode;
		struct nmk_gpio_chip *nmk_chip;

		gpio_fwnode = fwnode_find_reference(fwnode, "nomadik-gpio-chips", i);
		if (IS_ERR(gpio_fwnode))
			continue;

		dev_info(&pdev->dev, "populate NMK GPIO %d \"%pfwP\"\n", i, gpio_fwnode);
		nmk_chip = nmk_gpio_populate_chip(gpio_fwnode, pdev);
		if (IS_ERR(nmk_chip))
			dev_err(&pdev->dev,
				"could not populate nmk chip struct - continue anyway\n");
		else
			/* We are NOT compatible with mobileye,eyeq5-gpio. */
			BUG_ON(nmk_chip->is_mobileye_soc);
		fwnode_handle_put(gpio_fwnode);
	}

	prcm_fwnode = fwnode_find_reference(fwnode, "prcm", 0);
	if (!IS_ERR(prcm_fwnode)) {
		npct->prcm_base = fwnode_iomap(prcm_fwnode, 0);
		fwnode_handle_put(prcm_fwnode);
	}
	if (!npct->prcm_base) {
		if (version == PINCTRL_NMK_STN8815) {
			dev_info(&pdev->dev,
				 "No PRCM base, assuming no ALT-Cx control is available\n");
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

static int __init nmk_pinctrl_init(void)
{
	return platform_driver_register(&nmk_pinctrl_driver);
}
core_initcall(nmk_pinctrl_init);

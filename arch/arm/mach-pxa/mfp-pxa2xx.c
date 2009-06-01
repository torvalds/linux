/*
 *  linux/arch/arm/mach-pxa/mfp-pxa2xx.c
 *
 *  PXA2xx pin mux configuration support
 *
 *  The GPIOs on PXA2xx can be configured as one of many alternate
 *  functions, this is by concept samilar to the MFP configuration
 *  on PXA3xx,  what's more important, the low power pin state and
 *  wakeup detection are also supported by the same framework.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sysdev.h>

#include <mach/gpio.h>
#include <mach/pxa2xx-regs.h>
#include <mach/mfp-pxa2xx.h>

#include "generic.h"

#define PGSR(x)		__REG2(0x40F00020, (x) << 2)
#define __GAFR(u, x)	__REG2((u) ? 0x40E00058 : 0x40E00054, (x) << 3)
#define GAFR_L(x)	__GAFR(0, x)
#define GAFR_U(x)	__GAFR(1, x)

#define PWER_WE35	(1 << 24)

struct gpio_desc {
	unsigned	valid		: 1;
	unsigned	can_wakeup	: 1;
	unsigned	keypad_gpio	: 1;
	unsigned	dir_inverted	: 1;
	unsigned int	mask; /* bit mask in PWER or PKWR */
	unsigned int	mux_mask; /* bit mask of muxed gpio bits, 0 if no mux */
	unsigned long	config;
};

static struct gpio_desc gpio_desc[MFP_PIN_GPIO127 + 1];

static unsigned long gpdr_lpm[4];

static int __mfp_config_gpio(unsigned gpio, unsigned long c)
{
	unsigned long gafr, mask = GPIO_bit(gpio);
	int bank = gpio_to_bank(gpio);
	int uorl = !!(gpio & 0x10); /* GAFRx_U or GAFRx_L ? */
	int shft = (gpio & 0xf) << 1;
	int fn = MFP_AF(c);
	int is_out = (c & MFP_DIR_OUT) ? 1 : 0;

	if (fn > 3)
		return -EINVAL;

	/* alternate function and direction at run-time */
	gafr = (uorl == 0) ? GAFR_L(bank) : GAFR_U(bank);
	gafr = (gafr & ~(0x3 << shft)) | (fn << shft);

	if (uorl == 0)
		GAFR_L(bank) = gafr;
	else
		GAFR_U(bank) = gafr;

	if (is_out ^ gpio_desc[gpio].dir_inverted)
		GPDR(gpio) |= mask;
	else
		GPDR(gpio) &= ~mask;

	/* alternate function and direction at low power mode */
	switch (c & MFP_LPM_STATE_MASK) {
	case MFP_LPM_DRIVE_HIGH:
		PGSR(bank) |= mask;
		is_out = 1;
		break;
	case MFP_LPM_DRIVE_LOW:
		PGSR(bank) &= ~mask;
		is_out = 1;
		break;
	case MFP_LPM_DEFAULT:
		break;
	default:
		/* warning and fall through, treat as MFP_LPM_DEFAULT */
		pr_warning("%s: GPIO%d: unsupported low power mode\n",
				__func__, gpio);
		break;
	}

	if (is_out ^ gpio_desc[gpio].dir_inverted)
		gpdr_lpm[bank] |= mask;
	else
		gpdr_lpm[bank] &= ~mask;

	/* give early warning if MFP_LPM_CAN_WAKEUP is set on the
	 * configurations of those pins not able to wakeup
	 */
	if ((c & MFP_LPM_CAN_WAKEUP) && !gpio_desc[gpio].can_wakeup) {
		pr_warning("%s: GPIO%d unable to wakeup\n",
				__func__, gpio);
		return -EINVAL;
	}

	if ((c & MFP_LPM_CAN_WAKEUP) && is_out) {
		pr_warning("%s: output GPIO%d unable to wakeup\n",
				__func__, gpio);
		return -EINVAL;
	}

	return 0;
}

static inline int __mfp_validate(int mfp)
{
	int gpio = mfp_to_gpio(mfp);

	if ((mfp > MFP_PIN_GPIO127) || !gpio_desc[gpio].valid) {
		pr_warning("%s: GPIO%d is invalid pin\n", __func__, gpio);
		return -1;
	}

	return gpio;
}

void pxa2xx_mfp_config(unsigned long *mfp_cfgs, int num)
{
	unsigned long flags;
	unsigned long *c;
	int i, gpio;

	for (i = 0, c = mfp_cfgs; i < num; i++, c++) {

		gpio = __mfp_validate(MFP_PIN(*c));
		if (gpio < 0)
			continue;

		local_irq_save(flags);

		gpio_desc[gpio].config = *c;
		__mfp_config_gpio(gpio, *c);

		local_irq_restore(flags);
	}
}

void pxa2xx_mfp_set_lpm(int mfp, unsigned long lpm)
{
	unsigned long flags, c;
	int gpio;

	gpio = __mfp_validate(mfp);
	if (gpio < 0)
		return;

	local_irq_save(flags);

	c = gpio_desc[gpio].config;
	c = (c & ~MFP_LPM_STATE_MASK) | lpm;
	__mfp_config_gpio(gpio, c);

	local_irq_restore(flags);
}

int gpio_set_wake(unsigned int gpio, unsigned int on)
{
	struct gpio_desc *d;
	unsigned long c, mux_taken;

	if (gpio > mfp_to_gpio(MFP_PIN_GPIO127))
		return -EINVAL;

	d = &gpio_desc[gpio];
	c = d->config;

	if (!d->valid)
		return -EINVAL;

	if (d->keypad_gpio)
		return -EINVAL;

	mux_taken = (PWER & d->mux_mask) & (~d->mask);
	if (on && mux_taken)
		return -EBUSY;

	if (d->can_wakeup && (c & MFP_LPM_CAN_WAKEUP)) {
		if (on) {
			PWER = (PWER & ~d->mux_mask) | d->mask;

			if (c & MFP_LPM_EDGE_RISE)
				PRER |= d->mask;
			else
				PRER &= ~d->mask;

			if (c & MFP_LPM_EDGE_FALL)
				PFER |= d->mask;
			else
				PFER &= ~d->mask;
		} else {
			PWER &= ~d->mask;
			PRER &= ~d->mask;
			PFER &= ~d->mask;
		}
	}
	return 0;
}

#ifdef CONFIG_PXA25x
static void __init pxa25x_mfp_init(void)
{
	int i;

	for (i = 0; i <= pxa_last_gpio; i++)
		gpio_desc[i].valid = 1;

	for (i = 0; i <= 15; i++) {
		gpio_desc[i].can_wakeup = 1;
		gpio_desc[i].mask = GPIO_bit(i);
	}

	/* PXA26x has additional 4 GPIOs (86/87/88/89) which has the
	 * direction bit inverted in GPDR2. See PXA26x DM 4.1.1.
	 */
	for (i = 86; i <= pxa_last_gpio; i++)
		gpio_desc[i].dir_inverted = 1;
}
#else
static inline void pxa25x_mfp_init(void) {}
#endif /* CONFIG_PXA25x */

#ifdef CONFIG_PXA27x
static int pxa27x_pkwr_gpio[] = {
	13, 16, 17, 34, 36, 37, 38, 39, 90, 91, 93, 94,
	95, 96, 97, 98, 99, 100, 101, 102
};

int keypad_set_wake(unsigned int on)
{
	unsigned int i, gpio, mask = 0;

	if (!on) {
		PKWR = 0;
		return 0;
	}

	for (i = 0; i < ARRAY_SIZE(pxa27x_pkwr_gpio); i++) {

		gpio = pxa27x_pkwr_gpio[i];

		if (gpio_desc[gpio].config & MFP_LPM_CAN_WAKEUP)
			mask |= gpio_desc[gpio].mask;
	}

	PKWR = mask;
	return 0;
}

#define PWER_WEMUX2_GPIO38	(1 << 16)
#define PWER_WEMUX2_GPIO53	(2 << 16)
#define PWER_WEMUX2_GPIO40	(3 << 16)
#define PWER_WEMUX2_GPIO36	(4 << 16)
#define PWER_WEMUX2_MASK	(7 << 16)
#define PWER_WEMUX3_GPIO31	(1 << 19)
#define PWER_WEMUX3_GPIO113	(2 << 19)
#define PWER_WEMUX3_MASK	(3 << 19)

#define INIT_GPIO_DESC_MUXED(mux, gpio)				\
do {								\
	gpio_desc[(gpio)].can_wakeup = 1;			\
	gpio_desc[(gpio)].mask = PWER_ ## mux ## _GPIO ##gpio;	\
	gpio_desc[(gpio)].mux_mask = PWER_ ## mux ## _MASK;	\
} while (0)

static void __init pxa27x_mfp_init(void)
{
	int i, gpio;

	for (i = 0; i <= pxa_last_gpio; i++) {
		/* skip GPIO2, 5, 6, 7, 8, they are not
		 * valid pins allow configuration
		 */
		if (i == 2 || i == 5 || i == 6 || i == 7 || i == 8)
			continue;

		gpio_desc[i].valid = 1;
	}

	/* Keypad GPIOs */
	for (i = 0; i < ARRAY_SIZE(pxa27x_pkwr_gpio); i++) {
		gpio = pxa27x_pkwr_gpio[i];
		gpio_desc[gpio].can_wakeup = 1;
		gpio_desc[gpio].keypad_gpio = 1;
		gpio_desc[gpio].mask = 1 << i;
	}

	/* Overwrite GPIO13 as a PWER wakeup source */
	for (i = 0; i <= 15; i++) {
		/* skip GPIO2, 5, 6, 7, 8 */
		if (GPIO_bit(i) & 0x1e4)
			continue;

		gpio_desc[i].can_wakeup = 1;
		gpio_desc[i].mask = GPIO_bit(i);
	}

	gpio_desc[35].can_wakeup = 1;
	gpio_desc[35].mask = PWER_WE35;

	INIT_GPIO_DESC_MUXED(WEMUX3, 31);
	INIT_GPIO_DESC_MUXED(WEMUX3, 113);
	INIT_GPIO_DESC_MUXED(WEMUX2, 38);
	INIT_GPIO_DESC_MUXED(WEMUX2, 53);
	INIT_GPIO_DESC_MUXED(WEMUX2, 40);
	INIT_GPIO_DESC_MUXED(WEMUX2, 36);
}
#else
static inline void pxa27x_mfp_init(void) {}
#endif /* CONFIG_PXA27x */

#ifdef CONFIG_PM
static unsigned long saved_gafr[2][4];
static unsigned long saved_gpdr[4];
static unsigned long saved_pgsr[4];

static int pxa2xx_mfp_suspend(struct sys_device *d, pm_message_t state)
{
	int i;

	for (i = 0; i <= gpio_to_bank(pxa_last_gpio); i++) {

		saved_gafr[0][i] = GAFR_L(i);
		saved_gafr[1][i] = GAFR_U(i);
		saved_gpdr[i] = GPDR(i * 32);
		saved_pgsr[i] = PGSR(i);

		GPDR(i * 32) = gpdr_lpm[i];
	}
	return 0;
}

static int pxa2xx_mfp_resume(struct sys_device *d)
{
	int i;

	for (i = 0; i <= gpio_to_bank(pxa_last_gpio); i++) {
		GAFR_L(i) = saved_gafr[0][i];
		GAFR_U(i) = saved_gafr[1][i];
		GPDR(i * 32) = saved_gpdr[i];
		PGSR(i) = saved_pgsr[i];
	}
	PSSR = PSSR_RDH | PSSR_PH;
	return 0;
}
#else
#define pxa2xx_mfp_suspend	NULL
#define pxa2xx_mfp_resume	NULL
#endif

struct sysdev_class pxa2xx_mfp_sysclass = {
	.name		= "mfp",
	.suspend	= pxa2xx_mfp_suspend,
	.resume		= pxa2xx_mfp_resume,
};

static int __init pxa2xx_mfp_init(void)
{
	int i;

	if (!cpu_is_pxa2xx())
		return 0;

	if (cpu_is_pxa25x())
		pxa25x_mfp_init();

	if (cpu_is_pxa27x())
		pxa27x_mfp_init();

	/* clear RDH bit to enable GPIO receivers after reset/sleep exit */
	PSSR = PSSR_RDH;

	/* initialize gafr_run[], pgsr_lpm[] from existing values */
	for (i = 0; i <= gpio_to_bank(pxa_last_gpio); i++)
		gpdr_lpm[i] = GPDR(i * 32);

	return sysdev_class_register(&pxa2xx_mfp_sysclass);
}
postcore_initcall(pxa2xx_mfp_init);

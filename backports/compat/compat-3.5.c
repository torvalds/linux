/*
 * Copyright 2012-2013  Luis R. Rodriguez <mcgrof@do-not-panic.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Backport functionality introduced in Linux 3.5.
 */

#include <linux/module.h>
#include <linux/highuid.h>
#include <linux/ktime.h>
#include <linux/hrtimer.h>
#include <linux/gpio.h>
#include <linux/ptp_clock_kernel.h>

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0))
#include <linux/device.h>

/**
 * devres_release - Find a device resource and destroy it, calling release
 * @dev: Device to find resource from
 * @release: Look for resources associated with this release function
 * @match: Match function (optional)
 * @match_data: Data for the match function
 *
 * Find the latest devres of @dev associated with @release and for
 * which @match returns 1.  If @match is NULL, it's considered to
 * match all.  If found, the resource is removed atomically, the
 * release function called and the resource freed.
 *
 * RETURNS:
 * 0 if devres is found and freed, -ENOENT if not found.
 */
int devres_release(struct device *dev, dr_release_t release,
		   dr_match_t match, void *match_data)
{
	void *res;

	res = devres_remove(dev, release, match, match_data);
	if (unlikely(!res))
		return -ENOENT;

	(*release)(dev, res);
	devres_free(res);
	return 0;
}
EXPORT_SYMBOL_GPL(devres_release);
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0)) */

/*
 * Commit 7a4e7408c5cadb240e068a662251754a562355e3
 * exported overflowuid and overflowgid for all
 * kernel configurations, prior to that we only
 * had it exported when CONFIG_UID16 was enabled.
 * We are technically redefining it here but
 * nothing seems to be changing it, except
 * kernel/ code does epose it via sysctl and
 * proc... if required later we can add that here.
 */
#ifndef CONFIG_UID16
int overflowuid = DEFAULT_OVERFLOWUID;
int overflowgid = DEFAULT_OVERFLOWGID;

EXPORT_SYMBOL_GPL(overflowuid);
EXPORT_SYMBOL_GPL(overflowgid);
#endif

#if IS_ENABLED(CONFIG_PTP_1588_CLOCK)
int ptp_clock_index(struct ptp_clock *ptp)
{
	return ptp->index;
}
EXPORT_SYMBOL(ptp_clock_index);
#endif /* CONFIG_PTP_1588_CLOCK */

#ifdef CONFIG_GPIOLIB
static void devm_gpio_release(struct device *dev, void *res)
{
	unsigned *gpio = res;

	gpio_free(*gpio);
}

/**
 *      devm_gpio_request - request a GPIO for a managed device
 *      @dev: device to request the GPIO for
 *      @gpio: GPIO to allocate
 *      @label: the name of the requested GPIO
 *
 *      Except for the extra @dev argument, this function takes the
 *      same arguments and performs the same function as
 *      gpio_request().  GPIOs requested with this function will be
 *      automatically freed on driver detach.
 *
 *      If an GPIO allocated with this function needs to be freed
 *      separately, devm_gpio_free() must be used.
 */

int devm_gpio_request(struct device *dev, unsigned gpio, const char *label)
{
	unsigned *dr;
	int rc;

	dr = devres_alloc(devm_gpio_release, sizeof(unsigned), GFP_KERNEL);
	if (!dr)
		return -ENOMEM;

	rc = gpio_request(gpio, label);
	if (rc) {
		devres_free(dr);
		return rc;
	}

	*dr = gpio;
	devres_add(dev, dr);

	return 0;
}
EXPORT_SYMBOL_GPL(devm_gpio_request);

/**
 *	devm_gpio_request_one - request a single GPIO with initial setup
 *	@dev:   device to request for
 *	@gpio:	the GPIO number
 *	@flags:	GPIO configuration as specified by GPIOF_*
 *	@label:	a literal description string of this GPIO
 */
int devm_gpio_request_one(struct device *dev, unsigned gpio,
			  unsigned long flags, const char *label)
{
	unsigned *dr;
	int rc;

	dr = devres_alloc(devm_gpio_release, sizeof(unsigned), GFP_KERNEL);
	if (!dr)
		return -ENOMEM;

	rc = gpio_request_one(gpio, flags, label);
	if (rc) {
		devres_free(dr);
		return rc;
	}

	*dr = gpio;
	devres_add(dev, dr);

	return 0;
}
EXPORT_SYMBOL_GPL(devm_gpio_request_one);
#endif /* CONFIG_GPIOLIB */

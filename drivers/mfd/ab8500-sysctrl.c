/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Mattias Nilsson <mattias.i.nilsson@stericsson.com> for ST Ericsson.
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/err.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/reboot.h>
#include <linux/signal.h>
#include <linux/power_supply.h>
#include <linux/mfd/abx500.h>
#include <linux/mfd/abx500/ab8500.h>
#include <linux/mfd/abx500/ab8500-sysctrl.h>

/* RtcCtrl bits */
#define AB8500_ALARM_MIN_LOW  0x08
#define AB8500_ALARM_MIN_MID 0x09
#define RTC_CTRL 0x0B
#define RTC_ALARM_ENABLE 0x4

static struct device *sysctrl_dev;

static void ab8500_power_off(void)
{
	sigset_t old;
	sigset_t all;
	static char *pss[] = {"ab8500_ac", "pm2301", "ab8500_usb"};
	int i;
	bool charger_present = false;
	union power_supply_propval val;
	struct power_supply *psy;
	int ret;

	if (sysctrl_dev == NULL) {
		pr_err("%s: sysctrl not initialized\n", __func__);
		return;
	}

	/*
	 * If we have a charger connected and we're powering off,
	 * reboot into charge-only mode.
	 */

	for (i = 0; i < ARRAY_SIZE(pss); i++) {
		psy = power_supply_get_by_name(pss[i]);
		if (!psy)
			continue;

		ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_ONLINE,
				&val);

		if (!ret && val.intval) {
			charger_present = true;
			break;
		}
	}

	if (!charger_present)
		goto shutdown;

	/* Check if battery is known */
	psy = power_supply_get_by_name("ab8500_btemp");
	if (psy) {
		ret = power_supply_get_property(psy,
				POWER_SUPPLY_PROP_TECHNOLOGY, &val);
		if (!ret && val.intval != POWER_SUPPLY_TECHNOLOGY_UNKNOWN) {
			printk(KERN_INFO
			       "Charger \"%s\" is connected with known battery."
			       " Rebooting.\n",
			       pss[i]);
			machine_restart("charging");
		}
	}

shutdown:
	sigfillset(&all);

	if (!sigprocmask(SIG_BLOCK, &all, &old)) {
		(void)ab8500_sysctrl_set(AB8500_STW4500CTRL1,
					 AB8500_STW4500CTRL1_SWOFF |
					 AB8500_STW4500CTRL1_SWRESET4500N);
		(void)sigprocmask(SIG_SETMASK, &old, NULL);
	}
}

static inline bool valid_bank(u8 bank)
{
	return ((bank == AB8500_SYS_CTRL1_BLOCK) ||
		(bank == AB8500_SYS_CTRL2_BLOCK));
}

int ab8500_sysctrl_read(u16 reg, u8 *value)
{
	u8 bank;

	if (sysctrl_dev == NULL)
		return -EINVAL;

	bank = (reg >> 8);
	if (!valid_bank(bank))
		return -EINVAL;

	return abx500_get_register_interruptible(sysctrl_dev, bank,
		(u8)(reg & 0xFF), value);
}
EXPORT_SYMBOL(ab8500_sysctrl_read);

int ab8500_sysctrl_write(u16 reg, u8 mask, u8 value)
{
	u8 bank;

	if (sysctrl_dev == NULL)
		return -EINVAL;

	bank = (reg >> 8);
	if (!valid_bank(bank))
		return -EINVAL;

	return abx500_mask_and_set_register_interruptible(sysctrl_dev, bank,
		(u8)(reg & 0xFF), mask, value);
}
EXPORT_SYMBOL(ab8500_sysctrl_write);

static int ab8500_sysctrl_probe(struct platform_device *pdev)
{
	struct ab8500 *ab8500 = dev_get_drvdata(pdev->dev.parent);
	struct ab8500_platform_data *plat;
	struct ab8500_sysctrl_platform_data *pdata;

	plat = dev_get_platdata(pdev->dev.parent);

	if (!plat)
		return -EINVAL;

	sysctrl_dev = &pdev->dev;

	if (!pm_power_off)
		pm_power_off = ab8500_power_off;

	pdata = plat->sysctrl;
	if (pdata) {
		int last, ret, i, j;

		if (is_ab8505(ab8500))
			last = AB8500_SYSCLKREQ4RFCLKBUF;
		else
			last = AB8500_SYSCLKREQ8RFCLKBUF;

		for (i = AB8500_SYSCLKREQ1RFCLKBUF; i <= last; i++) {
			j = i - AB8500_SYSCLKREQ1RFCLKBUF;
			ret = ab8500_sysctrl_write(i, 0xff,
					pdata->initial_req_buf_config[j]);
			dev_dbg(&pdev->dev,
					"Setting SysClkReq%dRfClkBuf 0x%X\n",
					j + 1,
					pdata->initial_req_buf_config[j]);
			if (ret < 0) {
				dev_err(&pdev->dev,
					"unable to set sysClkReq%dRfClkBuf: "
					"%d\n", j + 1, ret);
			}
		}
	}

	return 0;
}

static int ab8500_sysctrl_remove(struct platform_device *pdev)
{
	sysctrl_dev = NULL;

	if (pm_power_off == ab8500_power_off)
		pm_power_off = NULL;

	return 0;
}

static struct platform_driver ab8500_sysctrl_driver = {
	.driver = {
		.name = "ab8500-sysctrl",
	},
	.probe = ab8500_sysctrl_probe,
	.remove = ab8500_sysctrl_remove,
};

static int __init ab8500_sysctrl_init(void)
{
	return platform_driver_register(&ab8500_sysctrl_driver);
}
arch_initcall(ab8500_sysctrl_init);

MODULE_AUTHOR("Mattias Nilsson <mattias.i.nilsson@stericsson.com");
MODULE_DESCRIPTION("AB8500 system control driver");
MODULE_LICENSE("GPL v2");

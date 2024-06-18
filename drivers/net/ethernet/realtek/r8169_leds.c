// SPDX-License-Identifier: GPL-2.0-only
/* r8169_leds.c: Realtek 8169/8168/8101/8125 ethernet driver.
 *
 * Copyright (c) 2023 Heiner Kallweit <hkallweit1@gmail.com>
 *
 * See MAINTAINERS file for support contact information.
 */

#include <linux/leds.h>
#include <linux/netdevice.h>
#include <uapi/linux/uleds.h>

#include "r8169.h"

#define RTL8168_LED_CTRL_OPTION2	BIT(15)
#define RTL8168_LED_CTRL_ACT		BIT(3)
#define RTL8168_LED_CTRL_LINK_1000	BIT(2)
#define RTL8168_LED_CTRL_LINK_100	BIT(1)
#define RTL8168_LED_CTRL_LINK_10	BIT(0)

#define RTL8125_LED_CTRL_ACT		BIT(9)
#define RTL8125_LED_CTRL_LINK_2500	BIT(5)
#define RTL8125_LED_CTRL_LINK_1000	BIT(3)
#define RTL8125_LED_CTRL_LINK_100	BIT(1)
#define RTL8125_LED_CTRL_LINK_10	BIT(0)

#define RTL8168_NUM_LEDS		3
#define RTL8125_NUM_LEDS		4

struct r8169_led_classdev {
	struct led_classdev led;
	struct net_device *ndev;
	int index;
};

#define lcdev_to_r8169_ldev(lcdev) container_of(lcdev, struct r8169_led_classdev, led)

static bool r8169_trigger_mode_is_valid(unsigned long flags)
{
	bool rx, tx;

	if (flags & BIT(TRIGGER_NETDEV_HALF_DUPLEX))
		return false;
	if (flags & BIT(TRIGGER_NETDEV_FULL_DUPLEX))
		return false;

	rx = flags & BIT(TRIGGER_NETDEV_RX);
	tx = flags & BIT(TRIGGER_NETDEV_TX);

	return rx == tx;
}

static int rtl8168_led_hw_control_is_supported(struct led_classdev *led_cdev,
					       unsigned long flags)
{
	struct r8169_led_classdev *ldev = lcdev_to_r8169_ldev(led_cdev);
	struct rtl8169_private *tp = netdev_priv(ldev->ndev);
	int shift = ldev->index * 4;

	if (!r8169_trigger_mode_is_valid(flags)) {
		/* Switch LED off to indicate that mode isn't supported */
		rtl8168_led_mod_ctrl(tp, 0x000f << shift, 0);
		return -EOPNOTSUPP;
	}

	return 0;
}

static int rtl8168_led_hw_control_set(struct led_classdev *led_cdev,
				      unsigned long flags)
{
	struct r8169_led_classdev *ldev = lcdev_to_r8169_ldev(led_cdev);
	struct rtl8169_private *tp = netdev_priv(ldev->ndev);
	int shift = ldev->index * 4;
	u16 mode = 0;

	if (flags & BIT(TRIGGER_NETDEV_LINK_10))
		mode |= RTL8168_LED_CTRL_LINK_10;
	if (flags & BIT(TRIGGER_NETDEV_LINK_100))
		mode |= RTL8168_LED_CTRL_LINK_100;
	if (flags & BIT(TRIGGER_NETDEV_LINK_1000))
		mode |= RTL8168_LED_CTRL_LINK_1000;
	if (flags & BIT(TRIGGER_NETDEV_TX))
		mode |= RTL8168_LED_CTRL_ACT;

	return rtl8168_led_mod_ctrl(tp, 0x000f << shift, mode << shift);
}

static int rtl8168_led_hw_control_get(struct led_classdev *led_cdev,
				      unsigned long *flags)
{
	struct r8169_led_classdev *ldev = lcdev_to_r8169_ldev(led_cdev);
	struct rtl8169_private *tp = netdev_priv(ldev->ndev);
	int shift = ldev->index * 4;
	int mode;

	mode = rtl8168_get_led_mode(tp);
	if (mode < 0)
		return mode;

	if (mode & RTL8168_LED_CTRL_OPTION2) {
		rtl8168_led_mod_ctrl(tp, RTL8168_LED_CTRL_OPTION2, 0);
		netdev_notice(ldev->ndev, "Deactivating unsupported Option2 LED mode\n");
	}

	mode = (mode >> shift) & 0x000f;

	if (mode & RTL8168_LED_CTRL_ACT)
		*flags |= BIT(TRIGGER_NETDEV_TX) | BIT(TRIGGER_NETDEV_RX);

	if (mode & RTL8168_LED_CTRL_LINK_10)
		*flags |= BIT(TRIGGER_NETDEV_LINK_10);
	if (mode & RTL8168_LED_CTRL_LINK_100)
		*flags |= BIT(TRIGGER_NETDEV_LINK_100);
	if (mode & RTL8168_LED_CTRL_LINK_1000)
		*flags |= BIT(TRIGGER_NETDEV_LINK_1000);

	return 0;
}

static struct device *
	r8169_led_hw_control_get_device(struct led_classdev *led_cdev)
{
	struct r8169_led_classdev *ldev = lcdev_to_r8169_ldev(led_cdev);

	return &ldev->ndev->dev;
}

static void rtl8168_setup_ldev(struct r8169_led_classdev *ldev,
			       struct net_device *ndev, int index)
{
	struct rtl8169_private *tp = netdev_priv(ndev);
	struct led_classdev *led_cdev = &ldev->led;
	char led_name[LED_MAX_NAME_SIZE];

	ldev->ndev = ndev;
	ldev->index = index;

	r8169_get_led_name(tp, index, led_name, LED_MAX_NAME_SIZE);
	led_cdev->name = led_name;
	led_cdev->hw_control_trigger = "netdev";
	led_cdev->flags |= LED_RETAIN_AT_SHUTDOWN;
	led_cdev->hw_control_is_supported = rtl8168_led_hw_control_is_supported;
	led_cdev->hw_control_set = rtl8168_led_hw_control_set;
	led_cdev->hw_control_get = rtl8168_led_hw_control_get;
	led_cdev->hw_control_get_device = r8169_led_hw_control_get_device;

	/* ignore errors */
	led_classdev_register(&ndev->dev, led_cdev);
}

struct r8169_led_classdev *rtl8168_init_leds(struct net_device *ndev)
{
	struct r8169_led_classdev *leds;
	int i;

	leds = kcalloc(RTL8168_NUM_LEDS + 1, sizeof(*leds), GFP_KERNEL);
	if (!leds)
		return NULL;

	for (i = 0; i < RTL8168_NUM_LEDS; i++)
		rtl8168_setup_ldev(leds + i, ndev, i);

	return leds;
}

static int rtl8125_led_hw_control_is_supported(struct led_classdev *led_cdev,
					       unsigned long flags)
{
	struct r8169_led_classdev *ldev = lcdev_to_r8169_ldev(led_cdev);
	struct rtl8169_private *tp = netdev_priv(ldev->ndev);

	if (!r8169_trigger_mode_is_valid(flags)) {
		/* Switch LED off to indicate that mode isn't supported */
		rtl8125_set_led_mode(tp, ldev->index, 0);
		return -EOPNOTSUPP;
	}

	return 0;
}

static int rtl8125_led_hw_control_set(struct led_classdev *led_cdev,
				      unsigned long flags)
{
	struct r8169_led_classdev *ldev = lcdev_to_r8169_ldev(led_cdev);
	struct rtl8169_private *tp = netdev_priv(ldev->ndev);
	u16 mode = 0;

	if (flags & BIT(TRIGGER_NETDEV_LINK_10))
		mode |= RTL8125_LED_CTRL_LINK_10;
	if (flags & BIT(TRIGGER_NETDEV_LINK_100))
		mode |= RTL8125_LED_CTRL_LINK_100;
	if (flags & BIT(TRIGGER_NETDEV_LINK_1000))
		mode |= RTL8125_LED_CTRL_LINK_1000;
	if (flags & BIT(TRIGGER_NETDEV_LINK_2500))
		mode |= RTL8125_LED_CTRL_LINK_2500;
	if (flags & (BIT(TRIGGER_NETDEV_TX) | BIT(TRIGGER_NETDEV_RX)))
		mode |= RTL8125_LED_CTRL_ACT;

	return rtl8125_set_led_mode(tp, ldev->index, mode);
}

static int rtl8125_led_hw_control_get(struct led_classdev *led_cdev,
				      unsigned long *flags)
{
	struct r8169_led_classdev *ldev = lcdev_to_r8169_ldev(led_cdev);
	struct rtl8169_private *tp = netdev_priv(ldev->ndev);
	int mode;

	mode = rtl8125_get_led_mode(tp, ldev->index);
	if (mode < 0)
		return mode;

	if (mode & RTL8125_LED_CTRL_LINK_10)
		*flags |= BIT(TRIGGER_NETDEV_LINK_10);
	if (mode & RTL8125_LED_CTRL_LINK_100)
		*flags |= BIT(TRIGGER_NETDEV_LINK_100);
	if (mode & RTL8125_LED_CTRL_LINK_1000)
		*flags |= BIT(TRIGGER_NETDEV_LINK_1000);
	if (mode & RTL8125_LED_CTRL_LINK_2500)
		*flags |= BIT(TRIGGER_NETDEV_LINK_2500);
	if (mode & RTL8125_LED_CTRL_ACT)
		*flags |= BIT(TRIGGER_NETDEV_TX) | BIT(TRIGGER_NETDEV_RX);

	return 0;
}

static void rtl8125_setup_led_ldev(struct r8169_led_classdev *ldev,
				   struct net_device *ndev, int index)
{
	struct rtl8169_private *tp = netdev_priv(ndev);
	struct led_classdev *led_cdev = &ldev->led;
	char led_name[LED_MAX_NAME_SIZE];

	ldev->ndev = ndev;
	ldev->index = index;

	r8169_get_led_name(tp, index, led_name, LED_MAX_NAME_SIZE);
	led_cdev->name = led_name;
	led_cdev->hw_control_trigger = "netdev";
	led_cdev->flags |= LED_RETAIN_AT_SHUTDOWN;
	led_cdev->hw_control_is_supported = rtl8125_led_hw_control_is_supported;
	led_cdev->hw_control_set = rtl8125_led_hw_control_set;
	led_cdev->hw_control_get = rtl8125_led_hw_control_get;
	led_cdev->hw_control_get_device = r8169_led_hw_control_get_device;

	/* ignore errors */
	led_classdev_register(&ndev->dev, led_cdev);
}

struct r8169_led_classdev *rtl8125_init_leds(struct net_device *ndev)
{
	struct r8169_led_classdev *leds;
	int i;

	leds = kcalloc(RTL8125_NUM_LEDS + 1, sizeof(*leds), GFP_KERNEL);
	if (!leds)
		return NULL;

	for (i = 0; i < RTL8125_NUM_LEDS; i++)
		rtl8125_setup_led_ldev(leds + i, ndev, i);

	return leds;
}

void r8169_remove_leds(struct r8169_led_classdev *leds)
{
	if (!leds)
		return;

	for (struct r8169_led_classdev *l = leds; l->ndev; l++)
		led_classdev_unregister(&l->led);

	kfree(leds);
}

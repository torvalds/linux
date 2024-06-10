// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2024 Linutronix GmbH */

#include <linux/bits.h>
#include <linux/leds.h>
#include <linux/netdevice.h>
#include <linux/pm_runtime.h>
#include <uapi/linux/uleds.h>

#include "igc.h"

#define IGC_NUM_LEDS			3

#define IGC_LEDCTL_LED0_MODE_SHIFT	0
#define IGC_LEDCTL_LED0_MODE_MASK	GENMASK(3, 0)
#define IGC_LEDCTL_LED0_BLINK		BIT(7)
#define IGC_LEDCTL_LED1_MODE_SHIFT	8
#define IGC_LEDCTL_LED1_MODE_MASK	GENMASK(11, 8)
#define IGC_LEDCTL_LED1_BLINK		BIT(15)
#define IGC_LEDCTL_LED2_MODE_SHIFT	16
#define IGC_LEDCTL_LED2_MODE_MASK	GENMASK(19, 16)
#define IGC_LEDCTL_LED2_BLINK		BIT(23)

#define IGC_LEDCTL_MODE_ON		0x00
#define IGC_LEDCTL_MODE_OFF		0x01
#define IGC_LEDCTL_MODE_LINK_10		0x05
#define IGC_LEDCTL_MODE_LINK_100	0x06
#define IGC_LEDCTL_MODE_LINK_1000	0x07
#define IGC_LEDCTL_MODE_LINK_2500	0x08
#define IGC_LEDCTL_MODE_ACTIVITY	0x0b

#define IGC_SUPPORTED_MODES						 \
	(BIT(TRIGGER_NETDEV_LINK_2500) | BIT(TRIGGER_NETDEV_LINK_1000) | \
	 BIT(TRIGGER_NETDEV_LINK_100) | BIT(TRIGGER_NETDEV_LINK_10) |	 \
	 BIT(TRIGGER_NETDEV_RX) | BIT(TRIGGER_NETDEV_TX))

#define IGC_ACTIVITY_MODES					\
	(BIT(TRIGGER_NETDEV_RX) | BIT(TRIGGER_NETDEV_TX))

struct igc_led_classdev {
	struct net_device *netdev;
	struct led_classdev led;
	int index;
};

#define lcdev_to_igc_ldev(lcdev)				\
	container_of(lcdev, struct igc_led_classdev, led)

static void igc_led_select(struct igc_adapter *adapter, int led,
			   u32 *mask, u32 *shift, u32 *blink)
{
	switch (led) {
	case 0:
		*mask  = IGC_LEDCTL_LED0_MODE_MASK;
		*shift = IGC_LEDCTL_LED0_MODE_SHIFT;
		*blink = IGC_LEDCTL_LED0_BLINK;
		break;
	case 1:
		*mask  = IGC_LEDCTL_LED1_MODE_MASK;
		*shift = IGC_LEDCTL_LED1_MODE_SHIFT;
		*blink = IGC_LEDCTL_LED1_BLINK;
		break;
	case 2:
		*mask  = IGC_LEDCTL_LED2_MODE_MASK;
		*shift = IGC_LEDCTL_LED2_MODE_SHIFT;
		*blink = IGC_LEDCTL_LED2_BLINK;
		break;
	default:
		*mask = *shift = *blink = 0;
		netdev_err(adapter->netdev, "Unknown LED %d selected!\n", led);
	}
}

static void igc_led_set(struct igc_adapter *adapter, int led, u32 mode,
			bool blink)
{
	u32 shift, mask, blink_bit, ledctl;
	struct igc_hw *hw = &adapter->hw;

	igc_led_select(adapter, led, &mask, &shift, &blink_bit);

	pm_runtime_get_sync(&adapter->pdev->dev);
	mutex_lock(&adapter->led_mutex);

	/* Set mode */
	ledctl = rd32(IGC_LEDCTL);
	ledctl &= ~mask;
	ledctl |= mode << shift;

	/* Configure blinking */
	if (blink)
		ledctl |= blink_bit;
	else
		ledctl &= ~blink_bit;
	wr32(IGC_LEDCTL, ledctl);

	mutex_unlock(&adapter->led_mutex);
	pm_runtime_put(&adapter->pdev->dev);
}

static u32 igc_led_get(struct igc_adapter *adapter, int led)
{
	u32 shift, mask, blink_bit, ledctl;
	struct igc_hw *hw = &adapter->hw;

	igc_led_select(adapter, led, &mask, &shift, &blink_bit);

	pm_runtime_get_sync(&adapter->pdev->dev);
	mutex_lock(&adapter->led_mutex);
	ledctl = rd32(IGC_LEDCTL);
	mutex_unlock(&adapter->led_mutex);
	pm_runtime_put(&adapter->pdev->dev);

	return (ledctl & mask) >> shift;
}

static int igc_led_brightness_set_blocking(struct led_classdev *led_cdev,
					   enum led_brightness brightness)
{
	struct igc_led_classdev *ldev = lcdev_to_igc_ldev(led_cdev);
	struct igc_adapter *adapter = netdev_priv(ldev->netdev);
	u32 mode;

	if (brightness)
		mode = IGC_LEDCTL_MODE_ON;
	else
		mode = IGC_LEDCTL_MODE_OFF;

	netdev_dbg(adapter->netdev, "Set brightness for LED %d to mode %u!\n",
		   ldev->index, mode);

	igc_led_set(adapter, ldev->index, mode, false);

	return 0;
}

static int igc_led_hw_control_is_supported(struct led_classdev *led_cdev,
					   unsigned long flags)
{
	if (flags & ~IGC_SUPPORTED_MODES)
		return -EOPNOTSUPP;

	/* If Tx and Rx selected, activity can be offloaded unless some other
	 * mode is selected as well.
	 */
	if ((flags & BIT(TRIGGER_NETDEV_TX)) &&
	    (flags & BIT(TRIGGER_NETDEV_RX)) &&
	    !(flags & ~IGC_ACTIVITY_MODES))
		return 0;

	/* Single Rx or Tx activity is not supported. */
	if (flags & IGC_ACTIVITY_MODES)
		return -EOPNOTSUPP;

	/* Only one mode can be active at a given time. */
	if (flags & (flags - 1))
		return -EOPNOTSUPP;

	return 0;
}

static int igc_led_hw_control_set(struct led_classdev *led_cdev,
				  unsigned long flags)
{
	struct igc_led_classdev *ldev = lcdev_to_igc_ldev(led_cdev);
	struct igc_adapter *adapter = netdev_priv(ldev->netdev);
	u32 mode = IGC_LEDCTL_MODE_OFF;
	bool blink = false;

	if (flags & BIT(TRIGGER_NETDEV_LINK_10))
		mode = IGC_LEDCTL_MODE_LINK_10;
	if (flags & BIT(TRIGGER_NETDEV_LINK_100))
		mode = IGC_LEDCTL_MODE_LINK_100;
	if (flags & BIT(TRIGGER_NETDEV_LINK_1000))
		mode = IGC_LEDCTL_MODE_LINK_1000;
	if (flags & BIT(TRIGGER_NETDEV_LINK_2500))
		mode = IGC_LEDCTL_MODE_LINK_2500;
	if ((flags & BIT(TRIGGER_NETDEV_TX)) &&
	    (flags & BIT(TRIGGER_NETDEV_RX)))
		mode = IGC_LEDCTL_MODE_ACTIVITY;

	netdev_dbg(adapter->netdev, "Set HW control for LED %d to mode %u!\n",
		   ldev->index, mode);

	/* blink is recommended for activity */
	if (mode == IGC_LEDCTL_MODE_ACTIVITY)
		blink = true;

	igc_led_set(adapter, ldev->index, mode, blink);

	return 0;
}

static int igc_led_hw_control_get(struct led_classdev *led_cdev,
				  unsigned long *flags)
{
	struct igc_led_classdev *ldev = lcdev_to_igc_ldev(led_cdev);
	struct igc_adapter *adapter = netdev_priv(ldev->netdev);
	u32 mode;

	mode = igc_led_get(adapter, ldev->index);

	switch (mode) {
	case IGC_LEDCTL_MODE_ACTIVITY:
		*flags = BIT(TRIGGER_NETDEV_TX) | BIT(TRIGGER_NETDEV_RX);
		break;
	case IGC_LEDCTL_MODE_LINK_10:
		*flags = BIT(TRIGGER_NETDEV_LINK_10);
		break;
	case IGC_LEDCTL_MODE_LINK_100:
		*flags = BIT(TRIGGER_NETDEV_LINK_100);
		break;
	case IGC_LEDCTL_MODE_LINK_1000:
		*flags = BIT(TRIGGER_NETDEV_LINK_1000);
		break;
	case IGC_LEDCTL_MODE_LINK_2500:
		*flags = BIT(TRIGGER_NETDEV_LINK_2500);
		break;
	}

	return 0;
}

static struct device *igc_led_hw_control_get_device(struct led_classdev *led_cdev)
{
	struct igc_led_classdev *ldev = lcdev_to_igc_ldev(led_cdev);

	return &ldev->netdev->dev;
}

static void igc_led_get_name(struct igc_adapter *adapter, int index, char *buf,
			     size_t buf_len)
{
	snprintf(buf, buf_len, "igc-%x%x-led%d",
		 pci_domain_nr(adapter->pdev->bus),
		 pci_dev_id(adapter->pdev), index);
}

static int igc_setup_ldev(struct igc_led_classdev *ldev,
			  struct net_device *netdev, int index)
{
	struct igc_adapter *adapter = netdev_priv(netdev);
	struct led_classdev *led_cdev = &ldev->led;
	char led_name[LED_MAX_NAME_SIZE];

	ldev->netdev = netdev;
	ldev->index = index;

	igc_led_get_name(adapter, index, led_name, LED_MAX_NAME_SIZE);
	led_cdev->name = led_name;
	led_cdev->flags |= LED_RETAIN_AT_SHUTDOWN;
	led_cdev->max_brightness = 1;
	led_cdev->brightness_set_blocking = igc_led_brightness_set_blocking;
	led_cdev->hw_control_trigger = "netdev";
	led_cdev->hw_control_is_supported = igc_led_hw_control_is_supported;
	led_cdev->hw_control_set = igc_led_hw_control_set;
	led_cdev->hw_control_get = igc_led_hw_control_get;
	led_cdev->hw_control_get_device = igc_led_hw_control_get_device;

	return led_classdev_register(&netdev->dev, led_cdev);
}

int igc_led_setup(struct igc_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	struct igc_led_classdev *leds;
	int i, err;

	mutex_init(&adapter->led_mutex);

	leds = kcalloc(IGC_NUM_LEDS, sizeof(*leds), GFP_KERNEL);
	if (!leds)
		return -ENOMEM;

	for (i = 0; i < IGC_NUM_LEDS; i++) {
		err = igc_setup_ldev(leds + i, netdev, i);
		if (err)
			goto err;
	}

	adapter->leds = leds;

	return 0;

err:
	for (i--; i >= 0; i--)
		led_classdev_unregister(&((leds + i)->led));

	kfree(leds);
	return err;
}

void igc_led_free(struct igc_adapter *adapter)
{
	struct igc_led_classdev *leds = adapter->leds;
	int i;

	for (i = 0; i < IGC_NUM_LEDS; i++)
		led_classdev_unregister(&((leds + i)->led));

	kfree(leds);
}

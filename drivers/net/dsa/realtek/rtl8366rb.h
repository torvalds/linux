/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef _RTL8366RB_H
#define _RTL8366RB_H

#include "realtek.h"

#define RTL8366RB_PORT_NUM_CPU		5
#define RTL8366RB_NUM_PORTS		6
#define RTL8366RB_PHY_NO_MAX		4
#define RTL8366RB_NUM_LEDGROUPS		4
#define RTL8366RB_PHY_ADDR_MAX		31

/* LED control registers */
/* The LED blink rate is global; it is used by all triggers in all groups. */
#define RTL8366RB_LED_BLINKRATE_REG		0x0430
#define RTL8366RB_LED_BLINKRATE_MASK		0x0007
#define RTL8366RB_LED_BLINKRATE_28MS		0x0000
#define RTL8366RB_LED_BLINKRATE_56MS		0x0001
#define RTL8366RB_LED_BLINKRATE_84MS		0x0002
#define RTL8366RB_LED_BLINKRATE_111MS		0x0003
#define RTL8366RB_LED_BLINKRATE_222MS		0x0004
#define RTL8366RB_LED_BLINKRATE_446MS		0x0005

/* LED trigger event for each group */
#define RTL8366RB_LED_CTRL_REG			0x0431
#define RTL8366RB_LED_CTRL_OFFSET(led_group)	\
	(4 * (led_group))
#define RTL8366RB_LED_CTRL_MASK(led_group)	\
	(0xf << RTL8366RB_LED_CTRL_OFFSET(led_group))

/* The RTL8366RB_LED_X_X registers are used to manually set the LED state only
 * when the corresponding LED group in RTL8366RB_LED_CTRL_REG is
 * RTL8366RB_LEDGROUP_FORCE. Otherwise, it is ignored.
 */
#define RTL8366RB_LED_0_1_CTRL_REG		0x0432
#define RTL8366RB_LED_2_3_CTRL_REG		0x0433
#define RTL8366RB_LED_X_X_CTRL_REG(led_group)	\
	((led_group) <= 1 ? \
		RTL8366RB_LED_0_1_CTRL_REG : \
		RTL8366RB_LED_2_3_CTRL_REG)
#define RTL8366RB_LED_0_X_CTRL_MASK		GENMASK(5, 0)
#define RTL8366RB_LED_X_1_CTRL_MASK		GENMASK(11, 6)
#define RTL8366RB_LED_2_X_CTRL_MASK		GENMASK(5, 0)
#define RTL8366RB_LED_X_3_CTRL_MASK		GENMASK(11, 6)

enum rtl8366_ledgroup_mode {
	RTL8366RB_LEDGROUP_OFF			= 0x0,
	RTL8366RB_LEDGROUP_DUP_COL		= 0x1,
	RTL8366RB_LEDGROUP_LINK_ACT		= 0x2,
	RTL8366RB_LEDGROUP_SPD1000		= 0x3,
	RTL8366RB_LEDGROUP_SPD100		= 0x4,
	RTL8366RB_LEDGROUP_SPD10		= 0x5,
	RTL8366RB_LEDGROUP_SPD1000_ACT		= 0x6,
	RTL8366RB_LEDGROUP_SPD100_ACT		= 0x7,
	RTL8366RB_LEDGROUP_SPD10_ACT		= 0x8,
	RTL8366RB_LEDGROUP_SPD100_10_ACT	= 0x9,
	RTL8366RB_LEDGROUP_FIBER		= 0xa,
	RTL8366RB_LEDGROUP_AN_FAULT		= 0xb,
	RTL8366RB_LEDGROUP_LINK_RX		= 0xc,
	RTL8366RB_LEDGROUP_LINK_TX		= 0xd,
	RTL8366RB_LEDGROUP_MASTER		= 0xe,
	RTL8366RB_LEDGROUP_FORCE		= 0xf,

	__RTL8366RB_LEDGROUP_MODE_MAX
};

#if IS_ENABLED(CONFIG_NET_DSA_REALTEK_RTL8366RB_LEDS)

struct rtl8366rb_led {
	u8 port_num;
	u8 led_group;
	struct realtek_priv *priv;
	struct led_classdev cdev;
};

int rtl8366rb_setup_leds(struct realtek_priv *priv);

#else

static inline int rtl8366rb_setup_leds(struct realtek_priv *priv)
{
	return 0;
}

#endif /* IS_ENABLED(CONFIG_LEDS_CLASS) */

/**
 * struct rtl8366rb - RTL8366RB-specific data
 * @max_mtu: per-port max MTU setting
 * @pvid_enabled: if PVID is set for respective port
 * @leds: per-port and per-ledgroup led info
 */
struct rtl8366rb {
	unsigned int max_mtu[RTL8366RB_NUM_PORTS];
	bool pvid_enabled[RTL8366RB_NUM_PORTS];
#if IS_ENABLED(CONFIG_NET_DSA_REALTEK_RTL8366RB_LEDS)
	struct rtl8366rb_led leds[RTL8366RB_NUM_PORTS][RTL8366RB_NUM_LEDGROUPS];
#endif
};

/* This code is used also with LEDs disabled */
int rb8366rb_set_ledgroup_mode(struct realtek_priv *priv,
			       u8 led_group,
			       enum rtl8366_ledgroup_mode mode);

#endif /* _RTL8366RB_H */

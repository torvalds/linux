/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2026 David Yang
 */

#ifndef _YT_LEDS_H
#define _YT_LEDS_H

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/kconfig.h>
#include <linux/leds.h>

#define YT921X_LED_CTRL			0xd0000
#define  YT921X_LED_CTRL_UNK			BIT(21)
#define  YT921X_LED_CTRL_LOOPDETECT_BLINK_M	GENMASK(20, 19)	/* cycle = 512 * x ms */
#define   YT921X_LED_CTRL_LOOPDETECT_BLINK(x)		FIELD_PREP(YT921X_LED_CTRL_LOOPDETECT_BLINK_M, (x))
#define  YT921X_LED_CTRL_PORT_NUM_M		GENMASK(16, 13)
#define   YT921X_LED_CTRL_PORT_NUM(x)			FIELD_PREP(YT921X_LED_CTRL_PORT_NUM_M, (x))
#define  YT921X_LED_CTRL_MODE_M			GENMASK(1, 0)
#define   YT921X_LED_CTRL_MODE(x)			FIELD_PREP(YT921X_LED_CTRL_MODE_M, (x))
#define   YT921X_LED_CTRL_MODE_PARALLEL			YT921X_LED_CTRL_MODE(0)
#define   YT921X_LED_CTRL_MODE_SERIAL			YT921X_LED_CTRL_MODE(2)
#define YT921X_LED0_PORTn(port)		(0xd0004 + 4 * (port))
#define  YT921X_LED0_PORT_ACT_M			GENMASK(17, 0)
#define  YT921X_LED0_PORT_ACT_LINK_TRY_DIS	BIT(17)
#define  YT921X_LED0_PORT_ACT_COLLISION_BLINK_INDI	BIT(16)
#define YT921X_LED1_PORTn(port)		(0xd0040 + 4 * (port))
#define  YT921X_LED1_PORT_OTHER_BLINK_M		GENMASK(31, 30)	/* cycle = 512 >> x ms */
#define   YT921X_LED1_PORT_OTHER_BLINK(x)		FIELD_PREP(YT921X_LED1_PORT_OTHER_BLINK_M, (x))
#define  YT921X_LED1_PORT_EEE_BLINK_M		GENMASK(29, 28)	/* cycle = 512 >> x ms */
#define   YT921X_LED1_PORT_EEE_BLINK(x)			FIELD_PREP(YT921X_LED1_PORT_EEE_BLINK_M, (x))
#define  YT921X_LED1_PORT_BLINK_DUTY_COMP	BIT(27)
#define  YT921X_LED1_PORT_BLINK_DUTY_M		GENMASK(26, 25)
#define   YT921X_LED1_PORT_BLINK_DUTY(x)		FIELD_PREP(YT921X_LED1_PORT_BLINK_DUTY_M, (x))
#define   YT921X_LED1_PORT_BLINK_DUTY_1_2		YT921X_LED1_PORT_BLINK_DUTY(0)
#define   YT921X_LED1_PORT_BLINK_DUTY_2_3		YT921X_LED1_PORT_BLINK_DUTY(1)
#define   YT921X_LED1_PORT_BLINK_DUTY_3_4		YT921X_LED1_PORT_BLINK_DUTY(2)
#define   YT921X_LED1_PORT_BLINK_DUTY_5_6		YT921X_LED1_PORT_BLINK_DUTY(3)
#define YT921X_LED2_PORTn(port)		(0xd0080 + 4 * (port))
#define  YT921X_LED2_PORT_FORCEn_M(grp)		GENMASK(4 * (grp) + 19, 4 * (grp) + 18)
#define   YT921X_LED2_PORT_FORCEn(grp, x)		((x) << (4 * (grp) + 18))
#define   YT921X_LED2_PORT_FORCEn_DONTCARE(grp)		YT921X_LED2_PORT_FORCEn(grp, 0)
#define   YT921X_LED2_PORT_FORCEn_BLINK(grp)		YT921X_LED2_PORT_FORCEn(grp, 1)
#define   YT921X_LED2_PORT_FORCEn_ON(grp)		YT921X_LED2_PORT_FORCEn(grp, 2)
#define   YT921X_LED2_PORT_FORCEn_OFF(grp)		YT921X_LED2_PORT_FORCEn(grp, 3)
#define  YT921X_LED2_PORT_FORCE_BLINKn_M(grp)	GENMASK(4 * (grp) + 17, 4 * (grp) + 16)	/* cycle = 512 << x ms */
#define   YT921X_LED2_PORT_FORCE_BLINKn(grp, x)		((x) << (4 * (grp) + 16))
#define   YT921X_LED2_PORT_FORCE_BLINKn_OTHER(grp)	YT921X_LED2_PORT_FORCE_BLINKn(grp, 3)
#define  YT921X_LEDx_PORT_ACT_M			GENMASK(15, 0)
#define  YT921X_LEDx_PORT_ACT_EEE_BLINK		BIT(15)
#define  YT921X_LEDx_PORT_ACT_LOOPDETECT_BLINK	BIT(14)
#define  YT921X_LEDx_PORT_ACT_ACTIVE_BLINK	BIT(13)
#define  YT921X_LEDx_PORT_ACT_DUPLEX_FULL	BIT(12)
#define  YT921X_LEDx_PORT_ACT_DUPLEX_HALF	BIT(11)
#define  YT921X_LEDx_PORT_ACT_TX_BLINK		BIT(10)
#define  YT921X_LEDx_PORT_ACT_RX_BLINK		BIT(9)
#define  YT921X_LEDx_PORT_ACT_TX		BIT(8)
#define  YT921X_LEDx_PORT_ACT_RX		BIT(7)
#define  YT921X_LEDx_PORT_ACT_1000M		BIT(6)
#define  YT921X_LEDx_PORT_ACT_100M		BIT(5)
#define  YT921X_LEDx_PORT_ACT_10M		BIT(4)
#define  YT921X_LEDx_PORT_ACT_COLLISION_BLINK	BIT(3)
#define  YT921X_LEDx_PORT_ACT_1000M_BLINK	BIT(2)
#define  YT921X_LEDx_PORT_ACT_100M_BLINK	BIT(1)
#define  YT921X_LEDx_PORT_ACT_10M_BLINK		BIT(0)
#define YT921X_LED_SER_CTRL		0xd0100
#define  YT921X_LED_SER_CTRL_UNK		GENMASK(25, 24)	/* delay? */
#define  YT921X_LED_SER_CTRL_ACTIVE_LOW		BIT(4)
#define  YT921X_LED_SER_CTRL_GRP_NUM_M		GENMASK(1, 0)	/* #grp - 1 */
#define   YT921X_LED_SER_CTRL_GRP_NUM(x)		FIELD_PREP(YT921X_LED_SER_CTRL_GRP_NUM_M, (x))
#define YT921X_LED_SER_MAPnm(grp, port)	(0xd0104 + 8 * (2 - (grp)) + 4 * ((port) / 5))
#define  YT921X_LED_SER_MAP_DSTn_PORT_M(port)	GENMASK(6 * ((port) % 5) + 5, 6 * ((port) % 5) + 2)
#define   YT921X_LED_SER_MAP_DSTn_PORT(port, x)		((x) << (6 * ((port) % 5) + 2))
#define  YT921X_LED_SER_MAP_DSTn_LED_M(port)	GENMASK(6 * ((port) % 5) + 1, 6 * ((port) % 5))
#define   YT921X_LED_SER_MAP_DSTn_LED(port, x)		((x) << (6 * ((port) % 5)))
#define YT921X_LED_PAR_PORTS		0xd01c4
#define YT921X_LED_PAR_INV		0xd01c8
#define  YT921X_LED_PAR_INV_INVnm(grp, port)	BIT(10 * (grp) + (port))
#define YT921X_LED_PAR_MAPn(port)	(0xd01d0 + 4 * (port))
#define  YT921X_LED_PAR_MAP_DSTn_PORT_M(grp)	GENMASK(6 * (grp) + 5, 6 * (grp) + 2)
#define   YT921X_LED_PAR_MAP_DSTn_PORT(grp, x)		((x) << (6 * (grp) + 2))
#define  YT921X_LED_PAR_MAP_DSTn_LED_M(grp)	GENMASK(6 * (grp) + 1, 6 * (grp))
#define   YT921X_LED_PAR_MAP_DSTn_LED(grp, x)		((x) << (6 * (grp)))

#define YT921X_LED_BLINK_MIN	64
#define YT921X_LED_BLINK_DEF	512
#define YT921X_LED_BLINK_MAX	2048

/* 2 * lcm(2, 3, 4, 6) */
#define YT921X_LED_DUTY_DENOM		24
#define YT921X_LED_DUTY(nom, denom)	(YT921X_LED_DUTY_DENOM * (nom) / (denom))

struct yt921x_priv;

struct yt921x_led {
	struct led_classdev cdev;
	struct yt921x_port *port;
	unsigned char group;
};

#if IS_ENABLED(CONFIG_NET_DSA_YT921X_LEDS)

void yt921x_leds_remove(struct yt921x_priv *priv);
int yt921x_leds_setup(struct yt921x_priv *priv);

#else

static inline void yt921x_leds_remove(struct yt921x_priv *priv) {}

static inline int yt921x_leds_setup(struct yt921x_priv *priv)
{
	return 0;
}

#endif

#endif

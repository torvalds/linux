/*
 * Copyright (C) 2013 STMicroelectronics (R&D) Limited
 * Author: Stephen Gallimore <stephen.gallimore@st.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef __STI_RESET_SYSCFG_H
#define __STI_RESET_SYSCFG_H

#include <linux/device.h>
#include <linux/regmap.h>
#include <linux/reset-controller.h>

/**
 * Reset channel description for a system configuration register based
 * reset controller.
 *
 * @compatible: Compatible string of the syscon regmap containing this
 *              channel's control and ack (status) bits.
 * @reset: Regmap field description of the channel's reset bit.
 * @ack: Regmap field description of the channel's acknowledge bit.
 */
struct syscfg_reset_channel_data {
	const char *compatible;
	struct reg_field reset;
	struct reg_field ack;
};

#define _SYSCFG_RST_CH(_c, _rr, _rb, _ar, _ab)		\
	{ .compatible	= _c,				\
	  .reset	= REG_FIELD(_rr, _rb, _rb),	\
	  .ack		= REG_FIELD(_ar, _ab, _ab), }

#define _SYSCFG_RST_CH_NO_ACK(_c, _rr, _rb)		\
	{ .compatible	= _c,			\
	  .reset	= REG_FIELD(_rr, _rb, _rb), }

/**
 * Description of a system configuration register based reset controller.
 *
 * @wait_for_ack: The controller will wait for reset assert and de-assert to
 *                be "ack'd" in a channel's ack field.
 * @active_low: Are the resets in this controller active low, i.e. clearing
 *              the reset bit puts the hardware into reset.
 * @nr_channels: The number of reset channels in this controller.
 * @channels: An array of reset channel descriptions.
 */
struct syscfg_reset_controller_data {
	bool wait_for_ack;
	bool active_low;
	int nr_channels;
	const struct syscfg_reset_channel_data *channels;
};

/**
 * syscfg_reset_probe(): platform device probe function used by syscfg
 *                       reset controller drivers. This registers a reset
 *                       controller configured by the OF match data for
 *                       the compatible device which should be of type
 *                       "struct syscfg_reset_controller_data".
 *
 * @pdev: platform device
 */
int syscfg_reset_probe(struct platform_device *pdev);

#endif /* __STI_RESET_SYSCFG_H */

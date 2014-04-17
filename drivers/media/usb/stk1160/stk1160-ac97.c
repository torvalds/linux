/*
 * STK1160 driver
 *
 * Copyright (C) 2012 Ezequiel Garcia
 * <elezegarcia--a.t--gmail.com>
 *
 * Based on Easycap driver by R.M. Thomas
 *	Copyright (C) 2010 R.M. Thomas
 *	<rmthomas--a.t--sciolus.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/ac97_codec.h>

#include "stk1160.h"
#include "stk1160-reg.h"

static struct snd_ac97 *stk1160_ac97;

static void stk1160_write_ac97(struct snd_ac97 *ac97, u16 reg, u16 value)
{
	struct stk1160 *dev = ac97->private_data;

	/* Set codec register address */
	stk1160_write_reg(dev, STK1160_AC97_ADDR, reg);

	/* Set codec command */
	stk1160_write_reg(dev, STK1160_AC97_CMD, value & 0xff);
	stk1160_write_reg(dev, STK1160_AC97_CMD + 1, (value & 0xff00) >> 8);

	/*
	 * Set command write bit to initiate write operation.
	 * The bit will be cleared when transfer is done.
	 */
	stk1160_write_reg(dev, STK1160_AC97CTL_0, 0x8c);
}

static u16 stk1160_read_ac97(struct snd_ac97 *ac97, u16 reg)
{
	struct stk1160 *dev = ac97->private_data;
	u8 vall = 0;
	u8 valh = 0;

	/* Set codec register address */
	stk1160_write_reg(dev, STK1160_AC97_ADDR, reg);

	/*
	 * Set command read bit to initiate read operation.
	 * The bit will be cleared when transfer is done.
	 */
	stk1160_write_reg(dev, STK1160_AC97CTL_0, 0x8b);

	/* Retrieve register value */
	stk1160_read_reg(dev, STK1160_AC97_CMD, &vall);
	stk1160_read_reg(dev, STK1160_AC97_CMD + 1, &valh);

	return (valh << 8) | vall;
}

static void stk1160_reset_ac97(struct snd_ac97 *ac97)
{
	struct stk1160 *dev = ac97->private_data;
	/* Two-step reset AC97 interface and hardware codec */
	stk1160_write_reg(dev, STK1160_AC97CTL_0, 0x94);
	stk1160_write_reg(dev, STK1160_AC97CTL_0, 0x88);

	/* Set 16-bit audio data and choose L&R channel*/
	stk1160_write_reg(dev, STK1160_AC97CTL_1 + 2, 0x01);
}

static struct snd_ac97_bus_ops stk1160_ac97_ops = {
	.read	= stk1160_read_ac97,
	.write	= stk1160_write_ac97,
	.reset	= stk1160_reset_ac97,
};

int stk1160_ac97_register(struct stk1160 *dev)
{
	struct snd_card *card = NULL;
	struct snd_ac97_bus *ac97_bus;
	struct snd_ac97_template ac97_template;
	int rc;

	/*
	 * Just want a card to access ac96 controls,
	 * the actual capture interface will be handled by snd-usb-audio
	 */
	rc = snd_card_new(dev->dev, SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1,
			  THIS_MODULE, 0, &card);
	if (rc < 0)
		return rc;

	/* TODO: I'm not sure where should I get these names :-( */
	snprintf(card->shortname, sizeof(card->shortname),
		 "stk1160-mixer");
	snprintf(card->longname, sizeof(card->longname),
		 "stk1160 ac97 codec mixer control");
	strlcpy(card->driver, dev->dev->driver->name, sizeof(card->driver));

	rc = snd_ac97_bus(card, 0, &stk1160_ac97_ops, NULL, &ac97_bus);
	if (rc)
		goto err;

	/* We must set private_data before calling snd_ac97_mixer */
	memset(&ac97_template, 0, sizeof(ac97_template));
	ac97_template.private_data = dev;
	ac97_template.scaps = AC97_SCAP_SKIP_MODEM;
	rc = snd_ac97_mixer(ac97_bus, &ac97_template, &stk1160_ac97);
	if (rc)
		goto err;

	dev->snd_card = card;
	rc = snd_card_register(card);
	if (rc)
		goto err;

	return 0;

err:
	dev->snd_card = NULL;
	snd_card_free(card);
	return rc;
}

int stk1160_ac97_unregister(struct stk1160 *dev)
{
	struct snd_card *card = dev->snd_card;

	/*
	 * We need to check usb_device,
	 * because ac97 release attempts to communicate with codec
	 */
	if (card && dev->udev)
		snd_card_free(card);

	return 0;
}

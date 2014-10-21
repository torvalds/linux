/*
 * GE PIO2 GPIO Driver
 *
 * Author: Martyn Welch <martyn.welch@ge.com>
 * Copyright 2009 GE Intelligent Platforms Embedded Systems, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/ctype.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/vme.h>

#include "vme_pio2.h"

static const char driver_name[] = "pio2_gpio";

static struct pio2_card *gpio_to_pio2_card(struct gpio_chip *chip)
{
	return container_of(chip, struct pio2_card, gc);
}

static int pio2_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	u8 reg;
	int retval;
	struct pio2_card *card = gpio_to_pio2_card(chip);

	if ((card->bank[PIO2_CHANNEL_BANK[offset]].config == OUTPUT) |
		(card->bank[PIO2_CHANNEL_BANK[offset]].config == NOFIT)) {

		dev_err(&card->vdev->dev, "Channel not available as input\n");
		return 0;
	}

	retval = vme_master_read(card->window, &reg, 1,
		PIO2_REGS_DATA[PIO2_CHANNEL_BANK[offset]]);
	if (retval < 0) {
		dev_err(&card->vdev->dev, "Unable to read from GPIO\n");
		return 0;
	}

	/*
	 * Remember, input on channels configured as both input and output
	 * are inverted!
	 */
	if (reg & PIO2_CHANNEL_BIT[offset]) {
		if (card->bank[PIO2_CHANNEL_BANK[offset]].config != BOTH)
			return 0;

		return 1;
	}

	if (card->bank[PIO2_CHANNEL_BANK[offset]].config != BOTH)
		return 1;

	return 0;
}

static void pio2_gpio_set(struct gpio_chip *chip, unsigned int offset,
	int value)
{
	u8 reg;
	int retval;
	struct pio2_card *card = gpio_to_pio2_card(chip);

	if ((card->bank[PIO2_CHANNEL_BANK[offset]].config == INPUT) |
		(card->bank[PIO2_CHANNEL_BANK[offset]].config == NOFIT)) {

		dev_err(&card->vdev->dev, "Channel not available as output\n");
		return;
	}

	if (value)
		reg = card->bank[PIO2_CHANNEL_BANK[offset]].value |
			PIO2_CHANNEL_BIT[offset];
	else
		reg = card->bank[PIO2_CHANNEL_BANK[offset]].value &
			~PIO2_CHANNEL_BIT[offset];

	retval = vme_master_write(card->window, &reg, 1,
		PIO2_REGS_DATA[PIO2_CHANNEL_BANK[offset]]);
	if (retval < 0) {
		dev_err(&card->vdev->dev, "Unable to write to GPIO\n");
		return;
	}

	card->bank[PIO2_CHANNEL_BANK[offset]].value = reg;
}

/* Directionality configured at board build - send appropriate response */
static int pio2_gpio_dir_in(struct gpio_chip *chip, unsigned offset)
{
	int data;
	struct pio2_card *card = gpio_to_pio2_card(chip);

	if ((card->bank[PIO2_CHANNEL_BANK[offset]].config == OUTPUT) |
		(card->bank[PIO2_CHANNEL_BANK[offset]].config == NOFIT)) {
		dev_err(&card->vdev->dev,
			"Channel directionality not configurable at runtime\n");

		data = -EINVAL;
	} else {
		data = 0;
	}

	return data;
}

/* Directionality configured at board build - send appropriate response */
static int pio2_gpio_dir_out(struct gpio_chip *chip, unsigned offset, int value)
{
	int data;
	struct pio2_card *card = gpio_to_pio2_card(chip);

	if ((card->bank[PIO2_CHANNEL_BANK[offset]].config == INPUT) |
		(card->bank[PIO2_CHANNEL_BANK[offset]].config == NOFIT)) {
		dev_err(&card->vdev->dev,
			"Channel directionality not configurable at runtime\n");

		data = -EINVAL;
	} else {
		data = 0;
	}

	return data;
}

/*
 * We return whether this has been successful - this is used in the probe to
 * ensure we have a valid card.
 */
int pio2_gpio_reset(struct pio2_card *card)
{
	int retval = 0;
	int i, j;

	u8 data = 0;

	/* Zero output registers */
	for (i = 0; i < 4; i++) {
		retval = vme_master_write(card->window, &data, 1,
			PIO2_REGS_DATA[i]);
		if (retval < 0)
			return retval;
		card->bank[i].value = 0;
	}

	/* Set input interrupt masks */
	for (i = 0; i < 4; i++) {
		retval = vme_master_write(card->window, &data, 1,
			PIO2_REGS_INT_MASK[i * 2]);
		if (retval < 0)
			return retval;

		retval = vme_master_write(card->window, &data, 1,
			PIO2_REGS_INT_MASK[(i * 2) + 1]);
		if (retval < 0)
			return retval;

		for (j = 0; j < 8; j++)
			card->bank[i].irq[j] = NONE;
	}

	/* Ensure all I/O interrupts are cleared */
	for (i = 0; i < 4; i++) {
		do {
			retval = vme_master_read(card->window, &data, 1,
				PIO2_REGS_INT_STAT[i]);
			if (retval < 0)
				return retval;
		} while (data != 0);
	}

	return 0;
}

int pio2_gpio_init(struct pio2_card *card)
{
	int retval = 0;
	char *label;

	label = kmalloc(PIO2_NUM_CHANNELS, GFP_KERNEL);
	if (label == NULL)
		return -ENOMEM;

	sprintf(label, "%s@%s", driver_name, dev_name(&card->vdev->dev));
	card->gc.label = label;

	card->gc.ngpio = PIO2_NUM_CHANNELS;
	/* Dynamic allocation of base */
	card->gc.base = -1;
	/* Setup pointers to chip functions */
	card->gc.direction_input = pio2_gpio_dir_in;
	card->gc.direction_output = pio2_gpio_dir_out;
	card->gc.get = pio2_gpio_get;
	card->gc.set = pio2_gpio_set;

	/* This function adds a memory mapped GPIO chip */
	retval = gpiochip_add(&(card->gc));
	if (retval) {
		dev_err(&card->vdev->dev, "Unable to register GPIO\n");
		kfree(card->gc.label);
	}

	return retval;
};

void pio2_gpio_exit(struct pio2_card *card)
{
	const char *label = card->gc.label;

	gpiochip_remove(&(card->gc));
	kfree(label);
}


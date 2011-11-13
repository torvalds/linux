/*
 * Line6 Linux USB driver - 0.9.1beta
 *
 * Copyright (C) 2004-2010 Markus Grabner (grabner@icg.tugraz.at)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 *
 */

#include <sound/core.h>
#include <sound/initval.h>
#include <linux/export.h>

#include "driver.h"
#include "audio.h"

static int line6_index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;
static char *line6_id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;

/*
	Initialize the Line6 USB audio system.
*/
int line6_init_audio(struct usb_line6 *line6)
{
	static int dev;
	struct snd_card *card;
	int err;

	err = snd_card_create(line6_index[dev], line6_id[dev], THIS_MODULE, 0,
			      &card);
	if (err < 0)
		return err;

	line6->card = card;

	strcpy(card->id, line6->properties->id);
	strcpy(card->driver, DRIVER_NAME);
	strcpy(card->shortname, line6->properties->name);
	/* longname is 80 chars - see asound.h */
	sprintf(card->longname, "Line6 %s at USB %s", line6->properties->name,
		dev_name(line6->ifcdev));
	return 0;
}

/*
	Register the Line6 USB audio system.
*/
int line6_register_audio(struct usb_line6 *line6)
{
	int err;

	err = snd_card_register(line6->card);
	if (err < 0)
		return err;

	return 0;
}

/*
	Cleanup the Line6 USB audio system.
*/
void line6_cleanup_audio(struct usb_line6 *line6)
{
	struct snd_card *card = line6->card;

	if (card == NULL)
		return;

	snd_card_disconnect(card);
	snd_card_free(card);
	line6->card = NULL;
}

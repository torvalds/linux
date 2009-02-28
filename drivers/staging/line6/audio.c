/*
 * Line6 Linux USB driver - 0.8.0
 *
 * Copyright (C) 2004-2009 Markus Grabner (grabner@icg.tugraz.at)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 *
 */

#include "driver.h"
#include "audio.h"

#include <sound/core.h>
#include <sound/initval.h>


static int line6_index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;
static char *line6_id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;


/*
	Initialize the Line6 USB audio system.
*/
int line6_init_audio(struct usb_line6 *line6)
{
	static int dev;
	struct snd_card *card;

	card = snd_card_new(line6_index[dev], line6_id[dev], THIS_MODULE, 0);

	if (card == NULL)
		return -ENOMEM;

	line6->card = card;

	strcpy(card->driver, DRIVER_NAME);
	strcpy(card->shortname, "Line6-USB");
	sprintf(card->longname, "Line6 %s at USB %s", line6->properties->name,
		dev_name(line6->ifcdev));  /* 80 chars - see asound.h */
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

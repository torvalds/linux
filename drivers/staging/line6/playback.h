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

#ifndef PLAYBACK_H
#define PLAYBACK_H


#include "driver.h"

#include <sound/pcm.h>


extern struct snd_pcm_ops snd_line6_playback_ops;


extern int create_audio_out_urbs(struct snd_line6_pcm *line6pcm);
extern int snd_line6_playback_trigger(struct snd_pcm_substream *substream,
				      int cmd);
extern void unlink_wait_clear_audio_out_urbs(struct snd_line6_pcm *line6pcm);


#endif

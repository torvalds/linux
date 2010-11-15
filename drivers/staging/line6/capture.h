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

#ifndef CAPTURE_H
#define CAPTURE_H

#include <sound/pcm.h>

#include "driver.h"
#include "pcm.h"

extern struct snd_pcm_ops snd_line6_capture_ops;

extern void line6_capture_copy(struct snd_line6_pcm *line6pcm, char *fbuf,
			       int fsize);
extern void line6_capture_check_period(struct snd_line6_pcm *line6pcm,
				       int length);
extern int line6_create_audio_in_urbs(struct snd_line6_pcm *line6pcm);
extern int line6_submit_audio_in_all_urbs(struct snd_line6_pcm *line6pcm);
extern void line6_unlink_audio_in_urbs(struct snd_line6_pcm *line6pcm);
extern void line6_unlink_wait_clear_audio_in_urbs(struct snd_line6_pcm
						  *line6pcm);
extern int snd_line6_capture_trigger(struct snd_line6_pcm *line6pcm, int cmd);

#endif

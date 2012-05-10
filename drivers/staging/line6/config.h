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

#ifndef CONFIG_H
#define CONFIG_H


#ifdef CONFIG_USB_DEBUG
#define DEBUG 1
#endif


/*
 * Development tools.
 */
#define DO_DEBUG_MESSAGES    0
#define DO_DUMP_URB_SEND     DO_DEBUG_MESSAGES
#define DO_DUMP_URB_RECEIVE  DO_DEBUG_MESSAGES
#define DO_DUMP_PCM_SEND     0
#define DO_DUMP_PCM_RECEIVE  0
#define DO_DUMP_MIDI_SEND    DO_DEBUG_MESSAGES
#define DO_DUMP_MIDI_RECEIVE DO_DEBUG_MESSAGES
#define DO_DUMP_ANY          (DO_DUMP_URB_SEND || DO_DUMP_URB_RECEIVE || \
			      DO_DUMP_PCM_SEND || DO_DUMP_PCM_RECEIVE || \
			      DO_DUMP_MIDI_SEND || DO_DUMP_MIDI_RECEIVE)
#define CREATE_RAW_FILE      0

#if DO_DEBUG_MESSAGES
#define DEBUG_MESSAGES(x) (x)
#else
#define DEBUG_MESSAGES(x)
#endif


#endif

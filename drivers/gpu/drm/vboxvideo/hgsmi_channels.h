/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2006-2017 Oracle Corporation */

#ifndef __HGSMI_CHANNELS_H__
#define __HGSMI_CHANNELS_H__

/*
 * Each channel has an 8 bit identifier. There are a number of predefined
 * (hardcoded) channels.
 *
 * HGSMI_CH_HGSMI channel can be used to map a string channel identifier
 * to a free 16 bit numerical value. values are allocated in range
 * [HGSMI_CH_STRING_FIRST;HGSMI_CH_STRING_LAST].
 */

/* A reserved channel value */
#define HGSMI_CH_RESERVED				0x00
/* HGCMI: setup and configuration */
#define HGSMI_CH_HGSMI					0x01
/* Graphics: VBVA */
#define HGSMI_CH_VBVA					0x02
/* Graphics: Seamless with a single guest region */
#define HGSMI_CH_SEAMLESS				0x03
/* Graphics: Seamless with separate host windows */
#define HGSMI_CH_SEAMLESS2				0x04
/* Graphics: OpenGL HW acceleration */
#define HGSMI_CH_OPENGL					0x05

/* The first channel index to be used for string mappings (inclusive) */
#define HGSMI_CH_STRING_FIRST				0x20
/* The last channel index for string mappings (inclusive) */
#define HGSMI_CH_STRING_LAST				0xff

#endif

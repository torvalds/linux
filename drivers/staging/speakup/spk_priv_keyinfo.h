/* SPDX-License-Identifier: GPL-2.0+ */
/* spk_priv.h
 * review functions for the speakup screen review package.
 * originally written by: Kirk Reiser and Andy Berdan.
 *
 * extensively modified by David Borowski.
 *
 * Copyright (C) 1998  Kirk Reiser.
 * Copyright (C) 2003  David Borowski.
 */

#ifndef _SPEAKUP_KEYINFO_H
#define _SPEAKUP_KEYINFO_H

#define FIRST_SYNTH_VAR RATE
/* 0 is reserved for no remap */
#define SPEAKUP_GOTO		0x01
#define SPEECH_KILL		0x02
#define SPEAKUP_QUIET		0x03
#define SPEAKUP_CUT		0x04
#define SPEAKUP_PASTE		0x05
#define SAY_FIRST_CHAR		0x06
#define SAY_LAST_CHAR		0x07
#define SAY_CHAR		0x08
#define SAY_PREV_CHAR		0x09
#define SAY_NEXT_CHAR		0x0a
#define SAY_WORD		0x0b
#define SAY_PREV_WORD		0x0c
#define SAY_NEXT_WORD		0x0d
#define SAY_LINE		0x0e
#define SAY_PREV_LINE		0x0f
#define SAY_NEXT_LINE		0x10
#define TOP_EDGE		0x11
#define BOTTOM_EDGE		0x12
#define LEFT_EDGE		0x13
#define RIGHT_EDGE		0x14
#define SPELL_PHONETIC		0x15
#define SPELL_WORD		0x16
#define SAY_SCREEN		0x17
#define SAY_POSITION		0x18
#define SAY_ATTRIBUTES		0x19
#define SPEAKUP_OFF		0x1a
#define SPEAKUP_PARKED		0x1b
#define SAY_LINE_INDENT	0x1c
#define SAY_FROM_TOP		0x1d
#define SAY_TO_BOTTOM		0x1e
#define SAY_FROM_LEFT		0x1f
#define SAY_TO_RIGHT		0x20
#define SAY_CHAR_NUM		0x21
#define EDIT_SOME		0x22
#define EDIT_MOST		0x23
#define SAY_PHONETIC_CHAR	0x24
#define EDIT_DELIM		0x25
#define EDIT_REPEAT		0x26
#define EDIT_EXNUM		0x27
#define SET_WIN		0x28
#define CLEAR_WIN		0x29
#define ENABLE_WIN		0x2a
#define SAY_WIN		0x2b
#define SPK_LOCK		0x2c
#define SPEAKUP_HELP		0x2d
#define TOGGLE_CURSORING	0x2e
#define READ_ALL_DOC		0x2f

/* one greater than the last func handler */
#define SPKUP_MAX_FUNC		0x30

#define SPK_KEY		0x80
#define FIRST_EDIT_BITS	0x22
#define FIRST_SET_VAR SPELL_DELAY

/* increase if adding more than 0x3f functions */
#define VAR_START		0x40

/* keys for setting variables, must be ordered same as the enum for var_ids */
/* with dec being even and inc being 1 greater */
#define SPELL_DELAY_DEC (VAR_START + 0)
#define SPELL_DELAY_INC (SPELL_DELAY_DEC + 1)
#define PUNC_LEVEL_DEC (SPELL_DELAY_DEC + 2)
#define PUNC_LEVEL_INC (PUNC_LEVEL_DEC + 1)
#define READING_PUNC_DEC (PUNC_LEVEL_DEC + 2)
#define READING_PUNC_INC (READING_PUNC_DEC + 1)
#define ATTRIB_BLEEP_DEC (READING_PUNC_DEC + 2)
#define ATTRIB_BLEEP_INC (ATTRIB_BLEEP_DEC + 1)
#define BLEEPS_DEC (ATTRIB_BLEEP_DEC + 2)
#define BLEEPS_INC (BLEEPS_DEC + 1)
#define RATE_DEC (BLEEPS_DEC + 2)
#define RATE_INC (RATE_DEC + 1)
#define PITCH_DEC (RATE_DEC + 2)
#define PITCH_INC (PITCH_DEC + 1)
#define VOL_DEC (PITCH_DEC + 2)
#define VOL_INC (VOL_DEC + 1)
#define TONE_DEC (VOL_DEC + 2)
#define TONE_INC (TONE_DEC + 1)
#define PUNCT_DEC (TONE_DEC + 2)
#define PUNCT_INC (PUNCT_DEC + 1)
#define VOICE_DEC (PUNCT_DEC + 2)
#define VOICE_INC (VOICE_DEC + 1)

#endif

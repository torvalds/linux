/*
 * Copyright (C) 1984-2017  Mark Nudelman
 *
 * You may distribute under the terms of either the GNU General Public
 * License or the Less License, as specified in the README file.
 *
 * For more information, see the README file.
 */


/*
 * Definitions of keys on the PC.
 * Special (non-ASCII) keys on the PC send a two-byte sequence,
 * where the first byte is 0 and the second is as defined below.
 */
#define	PCK_SHIFT_TAB		'\017'
#define	PCK_ALT_E		'\022'
#define	PCK_CAPS_LOCK		'\072'
#define	PCK_F1			'\073'
#define	PCK_NUM_LOCK		'\105'
#define	PCK_HOME		'\107'
#define	PCK_UP			'\110'
#define	PCK_PAGEUP		'\111'
#define	PCK_LEFT		'\113'
#define	PCK_RIGHT		'\115'
#define	PCK_END			'\117'
#define	PCK_DOWN		'\120'
#define	PCK_PAGEDOWN		'\121'
#define	PCK_INSERT		'\122'
#define	PCK_DELETE		'\123'
#define	PCK_CTL_LEFT		'\163'
#define	PCK_CTL_RIGHT		'\164'
#define	PCK_CTL_DELETE		'\223'

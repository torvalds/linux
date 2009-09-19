/*
    Keytables for supported remote controls, used on drivers/media
    devices.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/*
 * NOTICE FOR DEVELOPERS:
 *   The IR mappings should be as close as possible to what's
 *   specified at:
 *      http://linuxtv.org/wiki/index.php/Remote_Controllers
 */
#include <linux/module.h>

#include <linux/input.h>
#include <media/ir-common.h>

/* empty keytable, can be used as placeholder for not-yet created keytables */
static struct ir_scancode ir_codes_empty[] = {
	{ 0x2a, KEY_COFFEE },
};

struct ir_scancode_table ir_codes_empty_table = {
	.scan = ir_codes_empty,
	.size = ARRAY_SIZE(ir_codes_empty),
};
EXPORT_SYMBOL_GPL(ir_codes_empty_table);

/* Michal Majchrowicz <mmajchrowicz@gmail.com> */
static struct ir_scancode ir_codes_proteus_2309[] = {
	/* numeric */
	{ 0x00, KEY_0 },
	{ 0x01, KEY_1 },
	{ 0x02, KEY_2 },
	{ 0x03, KEY_3 },
	{ 0x04, KEY_4 },
	{ 0x05, KEY_5 },
	{ 0x06, KEY_6 },
	{ 0x07, KEY_7 },
	{ 0x08, KEY_8 },
	{ 0x09, KEY_9 },

	{ 0x5c, KEY_POWER },		/* power       */
	{ 0x20, KEY_ZOOM },		/* full screen */
	{ 0x0f, KEY_BACKSPACE },	/* recall      */
	{ 0x1b, KEY_ENTER },		/* mute        */
	{ 0x41, KEY_RECORD },		/* record      */
	{ 0x43, KEY_STOP },		/* stop        */
	{ 0x16, KEY_S },
	{ 0x1a, KEY_POWER2 },		/* off         */
	{ 0x2e, KEY_RED },
	{ 0x1f, KEY_CHANNELDOWN },	/* channel -   */
	{ 0x1c, KEY_CHANNELUP },	/* channel +   */
	{ 0x10, KEY_VOLUMEDOWN },	/* volume -    */
	{ 0x1e, KEY_VOLUMEUP },		/* volume +    */
	{ 0x14, KEY_F1 },
};

struct ir_scancode_table ir_codes_proteus_2309_table = {
	.scan = ir_codes_proteus_2309,
	.size = ARRAY_SIZE(ir_codes_proteus_2309),
};
EXPORT_SYMBOL_GPL(ir_codes_proteus_2309_table);

/* Matt Jesson <dvb@jesson.eclipse.co.uk */
static struct ir_scancode ir_codes_avermedia_dvbt[] = {
	{ 0x28, KEY_0 },		/* '0' / 'enter' */
	{ 0x22, KEY_1 },		/* '1' */
	{ 0x12, KEY_2 },		/* '2' / 'up arrow' */
	{ 0x32, KEY_3 },		/* '3' */
	{ 0x24, KEY_4 },		/* '4' / 'left arrow' */
	{ 0x14, KEY_5 },		/* '5' */
	{ 0x34, KEY_6 },		/* '6' / 'right arrow' */
	{ 0x26, KEY_7 },		/* '7' */
	{ 0x16, KEY_8 },		/* '8' / 'down arrow' */
	{ 0x36, KEY_9 },		/* '9' */

	{ 0x20, KEY_LIST },		/* 'source' */
	{ 0x10, KEY_TEXT },		/* 'teletext' */
	{ 0x00, KEY_POWER },		/* 'power' */
	{ 0x04, KEY_AUDIO },		/* 'audio' */
	{ 0x06, KEY_ZOOM },		/* 'full screen' */
	{ 0x18, KEY_VIDEO },		/* 'display' */
	{ 0x38, KEY_SEARCH },		/* 'loop' */
	{ 0x08, KEY_INFO },		/* 'preview' */
	{ 0x2a, KEY_REWIND },		/* 'backward <<' */
	{ 0x1a, KEY_FASTFORWARD },	/* 'forward >>' */
	{ 0x3a, KEY_RECORD },		/* 'capture' */
	{ 0x0a, KEY_MUTE },		/* 'mute' */
	{ 0x2c, KEY_RECORD },		/* 'record' */
	{ 0x1c, KEY_PAUSE },		/* 'pause' */
	{ 0x3c, KEY_STOP },		/* 'stop' */
	{ 0x0c, KEY_PLAY },		/* 'play' */
	{ 0x2e, KEY_RED },		/* 'red' */
	{ 0x01, KEY_BLUE },		/* 'blue' / 'cancel' */
	{ 0x0e, KEY_YELLOW },		/* 'yellow' / 'ok' */
	{ 0x21, KEY_GREEN },		/* 'green' */
	{ 0x11, KEY_CHANNELDOWN },	/* 'channel -' */
	{ 0x31, KEY_CHANNELUP },	/* 'channel +' */
	{ 0x1e, KEY_VOLUMEDOWN },	/* 'volume -' */
	{ 0x3e, KEY_VOLUMEUP },		/* 'volume +' */
};

struct ir_scancode_table ir_codes_avermedia_dvbt_table = {
	.scan = ir_codes_avermedia_dvbt,
	.size = ARRAY_SIZE(ir_codes_avermedia_dvbt),
};
EXPORT_SYMBOL_GPL(ir_codes_avermedia_dvbt_table);

/* Mauro Carvalho Chehab <mchehab@infradead.org> */
static struct ir_scancode ir_codes_avermedia_m135a[] = {
	{ 0x00, KEY_POWER2 },
	{ 0x2e, KEY_DOT },		/* '.' */
	{ 0x01, KEY_MODE },		/* TV/FM */

	{ 0x05, KEY_1 },
	{ 0x06, KEY_2 },
	{ 0x07, KEY_3 },
	{ 0x09, KEY_4 },
	{ 0x0a, KEY_5 },
	{ 0x0b, KEY_6 },
	{ 0x0d, KEY_7 },
	{ 0x0e, KEY_8 },
	{ 0x0f, KEY_9 },
	{ 0x11, KEY_0 },

	{ 0x13, KEY_RIGHT },		/* -> */
	{ 0x12, KEY_LEFT },		/* <- */

	{ 0x17, KEY_SLEEP },		/* Capturar Imagem */
	{ 0x10, KEY_SHUFFLE },		/* Amostra */

	/* FIXME: The keys bellow aren't ok */

	{ 0x43, KEY_CHANNELUP },
	{ 0x42, KEY_CHANNELDOWN },
	{ 0x1f, KEY_VOLUMEUP },
	{ 0x1e, KEY_VOLUMEDOWN },
	{ 0x0c, KEY_ENTER },

	{ 0x14, KEY_MUTE },
	{ 0x08, KEY_AUDIO },

	{ 0x03, KEY_TEXT },
	{ 0x04, KEY_EPG },
	{ 0x2b, KEY_TV2 },		/* TV2 */

	{ 0x1d, KEY_RED },
	{ 0x1c, KEY_YELLOW },
	{ 0x41, KEY_GREEN },
	{ 0x40, KEY_BLUE },

	{ 0x1a, KEY_PLAYPAUSE },
	{ 0x19, KEY_RECORD },
	{ 0x18, KEY_PLAY },
	{ 0x1b, KEY_STOP },
};

struct ir_scancode_table ir_codes_avermedia_m135a_table = {
	.scan = ir_codes_avermedia_m135a,
	.size = ARRAY_SIZE(ir_codes_avermedia_m135a),
};
EXPORT_SYMBOL_GPL(ir_codes_avermedia_m135a_table);

/* Oldrich Jedlicka <oldium.pro@seznam.cz> */
static struct ir_scancode ir_codes_avermedia_cardbus[] = {
	{ 0x00, KEY_POWER },
	{ 0x01, KEY_TUNER },		/* TV/FM */
	{ 0x03, KEY_TEXT },		/* Teletext */
	{ 0x04, KEY_EPG },
	{ 0x05, KEY_1 },
	{ 0x06, KEY_2 },
	{ 0x07, KEY_3 },
	{ 0x08, KEY_AUDIO },
	{ 0x09, KEY_4 },
	{ 0x0a, KEY_5 },
	{ 0x0b, KEY_6 },
	{ 0x0c, KEY_ZOOM },		/* Full screen */
	{ 0x0d, KEY_7 },
	{ 0x0e, KEY_8 },
	{ 0x0f, KEY_9 },
	{ 0x10, KEY_PAGEUP },		/* 16-CH PREV */
	{ 0x11, KEY_0 },
	{ 0x12, KEY_INFO },
	{ 0x13, KEY_AGAIN },		/* CH RTN - channel return */
	{ 0x14, KEY_MUTE },
	{ 0x15, KEY_EDIT },		/* Autoscan */
	{ 0x17, KEY_SAVE },		/* Screenshot */
	{ 0x18, KEY_PLAYPAUSE },
	{ 0x19, KEY_RECORD },
	{ 0x1a, KEY_PLAY },
	{ 0x1b, KEY_STOP },
	{ 0x1c, KEY_FASTFORWARD },
	{ 0x1d, KEY_REWIND },
	{ 0x1e, KEY_VOLUMEDOWN },
	{ 0x1f, KEY_VOLUMEUP },
	{ 0x22, KEY_SLEEP },		/* Sleep */
	{ 0x23, KEY_ZOOM },		/* Aspect */
	{ 0x26, KEY_SCREEN },		/* Pos */
	{ 0x27, KEY_ANGLE },		/* Size */
	{ 0x28, KEY_SELECT },		/* Select */
	{ 0x29, KEY_BLUE },		/* Blue/Picture */
	{ 0x2a, KEY_BACKSPACE },	/* Back */
	{ 0x2b, KEY_MEDIA },		/* PIP (Picture-in-picture) */
	{ 0x2c, KEY_DOWN },
	{ 0x2e, KEY_DOT },
	{ 0x2f, KEY_TV },		/* Live TV */
	{ 0x32, KEY_LEFT },
	{ 0x33, KEY_CLEAR },		/* Clear */
	{ 0x35, KEY_RED },		/* Red/TV */
	{ 0x36, KEY_UP },
	{ 0x37, KEY_HOME },		/* Home */
	{ 0x39, KEY_GREEN },		/* Green/Video */
	{ 0x3d, KEY_YELLOW },		/* Yellow/Music */
	{ 0x3e, KEY_OK },		/* Ok */
	{ 0x3f, KEY_RIGHT },
	{ 0x40, KEY_NEXT },		/* Next */
	{ 0x41, KEY_PREVIOUS },		/* Previous */
	{ 0x42, KEY_CHANNELDOWN },	/* Channel down */
	{ 0x43, KEY_CHANNELUP },	/* Channel up */
};

struct ir_scancode_table ir_codes_avermedia_cardbus_table = {
	.scan = ir_codes_avermedia_cardbus,
	.size = ARRAY_SIZE(ir_codes_avermedia_cardbus),
};
EXPORT_SYMBOL_GPL(ir_codes_avermedia_cardbus_table);

/* Attila Kondoros <attila.kondoros@chello.hu> */
static struct ir_scancode ir_codes_apac_viewcomp[] = {

	{ 0x01, KEY_1 },
	{ 0x02, KEY_2 },
	{ 0x03, KEY_3 },
	{ 0x04, KEY_4 },
	{ 0x05, KEY_5 },
	{ 0x06, KEY_6 },
	{ 0x07, KEY_7 },
	{ 0x08, KEY_8 },
	{ 0x09, KEY_9 },
	{ 0x00, KEY_0 },
	{ 0x17, KEY_LAST },		/* +100 */
	{ 0x0a, KEY_LIST },		/* recall */


	{ 0x1c, KEY_TUNER },		/* TV/FM */
	{ 0x15, KEY_SEARCH },		/* scan */
	{ 0x12, KEY_POWER },		/* power */
	{ 0x1f, KEY_VOLUMEDOWN },	/* vol up */
	{ 0x1b, KEY_VOLUMEUP },		/* vol down */
	{ 0x1e, KEY_CHANNELDOWN },	/* chn up */
	{ 0x1a, KEY_CHANNELUP },	/* chn down */

	{ 0x11, KEY_VIDEO },		/* video */
	{ 0x0f, KEY_ZOOM },		/* full screen */
	{ 0x13, KEY_MUTE },		/* mute/unmute */
	{ 0x10, KEY_TEXT },		/* min */

	{ 0x0d, KEY_STOP },		/* freeze */
	{ 0x0e, KEY_RECORD },		/* record */
	{ 0x1d, KEY_PLAYPAUSE },	/* stop */
	{ 0x19, KEY_PLAY },		/* play */

	{ 0x16, KEY_GOTO },		/* osd */
	{ 0x14, KEY_REFRESH },		/* default */
	{ 0x0c, KEY_KPPLUS },		/* fine tune >>>> */
	{ 0x18, KEY_KPMINUS },		/* fine tune <<<< */
};

struct ir_scancode_table ir_codes_apac_viewcomp_table = {
	.scan = ir_codes_apac_viewcomp,
	.size = ARRAY_SIZE(ir_codes_apac_viewcomp),
};
EXPORT_SYMBOL_GPL(ir_codes_apac_viewcomp_table);

/* ---------------------------------------------------------------------- */

static struct ir_scancode ir_codes_pixelview[] = {

	{ 0x1e, KEY_POWER },	/* power */
	{ 0x07, KEY_MEDIA },	/* source */
	{ 0x1c, KEY_SEARCH },	/* scan */


	{ 0x03, KEY_TUNER },		/* TV/FM */

	{ 0x00, KEY_RECORD },
	{ 0x08, KEY_STOP },
	{ 0x11, KEY_PLAY },

	{ 0x1a, KEY_PLAYPAUSE },	/* freeze */
	{ 0x19, KEY_ZOOM },		/* zoom */
	{ 0x0f, KEY_TEXT },		/* min */

	{ 0x01, KEY_1 },
	{ 0x0b, KEY_2 },
	{ 0x1b, KEY_3 },
	{ 0x05, KEY_4 },
	{ 0x09, KEY_5 },
	{ 0x15, KEY_6 },
	{ 0x06, KEY_7 },
	{ 0x0a, KEY_8 },
	{ 0x12, KEY_9 },
	{ 0x02, KEY_0 },
	{ 0x10, KEY_LAST },		/* +100 */
	{ 0x13, KEY_LIST },		/* recall */

	{ 0x1f, KEY_CHANNELUP },	/* chn down */
	{ 0x17, KEY_CHANNELDOWN },	/* chn up */
	{ 0x16, KEY_VOLUMEUP },		/* vol down */
	{ 0x14, KEY_VOLUMEDOWN },	/* vol up */

	{ 0x04, KEY_KPMINUS },		/* <<< */
	{ 0x0e, KEY_SETUP },		/* function */
	{ 0x0c, KEY_KPPLUS },		/* >>> */

	{ 0x0d, KEY_GOTO },		/* mts */
	{ 0x1d, KEY_REFRESH },		/* reset */
	{ 0x18, KEY_MUTE },		/* mute/unmute */
};

struct ir_scancode_table ir_codes_pixelview_table = {
	.scan = ir_codes_pixelview,
	.size = ARRAY_SIZE(ir_codes_pixelview),
};
EXPORT_SYMBOL_GPL(ir_codes_pixelview_table);

/*
   Mauro Carvalho Chehab <mchehab@infradead.org>
   present on PV MPEG 8000GT
 */
static struct ir_scancode ir_codes_pixelview_new[] = {
	{ 0x3c, KEY_TIME },		/* Timeshift */
	{ 0x12, KEY_POWER },

	{ 0x3d, KEY_1 },
	{ 0x38, KEY_2 },
	{ 0x18, KEY_3 },
	{ 0x35, KEY_4 },
	{ 0x39, KEY_5 },
	{ 0x15, KEY_6 },
	{ 0x36, KEY_7 },
	{ 0x3a, KEY_8 },
	{ 0x1e, KEY_9 },
	{ 0x3e, KEY_0 },

	{ 0x1c, KEY_AGAIN },		/* LOOP	*/
	{ 0x3f, KEY_MEDIA },		/* Source */
	{ 0x1f, KEY_LAST },		/* +100 */
	{ 0x1b, KEY_MUTE },

	{ 0x17, KEY_CHANNELDOWN },
	{ 0x16, KEY_CHANNELUP },
	{ 0x10, KEY_VOLUMEUP },
	{ 0x14, KEY_VOLUMEDOWN },
	{ 0x13, KEY_ZOOM },

	{ 0x19, KEY_CAMERA },		/* SNAPSHOT */
	{ 0x1a, KEY_SEARCH },		/* scan */

	{ 0x37, KEY_REWIND },		/* << */
	{ 0x32, KEY_RECORD },		/* o (red) */
	{ 0x33, KEY_FORWARD },		/* >> */
	{ 0x11, KEY_STOP },		/* square */
	{ 0x3b, KEY_PLAY },		/* > */
	{ 0x30, KEY_PLAYPAUSE },	/* || */

	{ 0x31, KEY_TV },
	{ 0x34, KEY_RADIO },
};

struct ir_scancode_table ir_codes_pixelview_new_table = {
	.scan = ir_codes_pixelview_new,
	.size = ARRAY_SIZE(ir_codes_pixelview_new),
};
EXPORT_SYMBOL_GPL(ir_codes_pixelview_new_table);

static struct ir_scancode ir_codes_nebula[] = {
	{ 0x00, KEY_0 },
	{ 0x01, KEY_1 },
	{ 0x02, KEY_2 },
	{ 0x03, KEY_3 },
	{ 0x04, KEY_4 },
	{ 0x05, KEY_5 },
	{ 0x06, KEY_6 },
	{ 0x07, KEY_7 },
	{ 0x08, KEY_8 },
	{ 0x09, KEY_9 },
	{ 0x0a, KEY_TV },
	{ 0x0b, KEY_AUX },
	{ 0x0c, KEY_DVD },
	{ 0x0d, KEY_POWER },
	{ 0x0e, KEY_MHP },	/* labelled 'Picture' */
	{ 0x0f, KEY_AUDIO },
	{ 0x10, KEY_INFO },
	{ 0x11, KEY_F13 },	/* 16:9 */
	{ 0x12, KEY_F14 },	/* 14:9 */
	{ 0x13, KEY_EPG },
	{ 0x14, KEY_EXIT },
	{ 0x15, KEY_MENU },
	{ 0x16, KEY_UP },
	{ 0x17, KEY_DOWN },
	{ 0x18, KEY_LEFT },
	{ 0x19, KEY_RIGHT },
	{ 0x1a, KEY_ENTER },
	{ 0x1b, KEY_CHANNELUP },
	{ 0x1c, KEY_CHANNELDOWN },
	{ 0x1d, KEY_VOLUMEUP },
	{ 0x1e, KEY_VOLUMEDOWN },
	{ 0x1f, KEY_RED },
	{ 0x20, KEY_GREEN },
	{ 0x21, KEY_YELLOW },
	{ 0x22, KEY_BLUE },
	{ 0x23, KEY_SUBTITLE },
	{ 0x24, KEY_F15 },	/* AD */
	{ 0x25, KEY_TEXT },
	{ 0x26, KEY_MUTE },
	{ 0x27, KEY_REWIND },
	{ 0x28, KEY_STOP },
	{ 0x29, KEY_PLAY },
	{ 0x2a, KEY_FASTFORWARD },
	{ 0x2b, KEY_F16 },	/* chapter */
	{ 0x2c, KEY_PAUSE },
	{ 0x2d, KEY_PLAY },
	{ 0x2e, KEY_RECORD },
	{ 0x2f, KEY_F17 },	/* picture in picture */
	{ 0x30, KEY_KPPLUS },	/* zoom in */
	{ 0x31, KEY_KPMINUS },	/* zoom out */
	{ 0x32, KEY_F18 },	/* capture */
	{ 0x33, KEY_F19 },	/* web */
	{ 0x34, KEY_EMAIL },
	{ 0x35, KEY_PHONE },
	{ 0x36, KEY_PC },
};

struct ir_scancode_table ir_codes_nebula_table = {
	.scan = ir_codes_nebula,
	.size = ARRAY_SIZE(ir_codes_nebula),
};
EXPORT_SYMBOL_GPL(ir_codes_nebula_table);

/* DigitalNow DNTV Live DVB-T Remote */
static struct ir_scancode ir_codes_dntv_live_dvb_t[] = {
	{ 0x00, KEY_ESC },		/* 'go up a level?' */
	/* Keys 0 to 9 */
	{ 0x0a, KEY_0 },
	{ 0x01, KEY_1 },
	{ 0x02, KEY_2 },
	{ 0x03, KEY_3 },
	{ 0x04, KEY_4 },
	{ 0x05, KEY_5 },
	{ 0x06, KEY_6 },
	{ 0x07, KEY_7 },
	{ 0x08, KEY_8 },
	{ 0x09, KEY_9 },

	{ 0x0b, KEY_TUNER },		/* tv/fm */
	{ 0x0c, KEY_SEARCH },		/* scan */
	{ 0x0d, KEY_STOP },
	{ 0x0e, KEY_PAUSE },
	{ 0x0f, KEY_LIST },		/* source */

	{ 0x10, KEY_MUTE },
	{ 0x11, KEY_REWIND },		/* backward << */
	{ 0x12, KEY_POWER },
	{ 0x13, KEY_CAMERA },		/* snap */
	{ 0x14, KEY_AUDIO },		/* stereo */
	{ 0x15, KEY_CLEAR },		/* reset */
	{ 0x16, KEY_PLAY },
	{ 0x17, KEY_ENTER },
	{ 0x18, KEY_ZOOM },		/* full screen */
	{ 0x19, KEY_FASTFORWARD },	/* forward >> */
	{ 0x1a, KEY_CHANNELUP },
	{ 0x1b, KEY_VOLUMEUP },
	{ 0x1c, KEY_INFO },		/* preview */
	{ 0x1d, KEY_RECORD },		/* record */
	{ 0x1e, KEY_CHANNELDOWN },
	{ 0x1f, KEY_VOLUMEDOWN },
};

struct ir_scancode_table ir_codes_dntv_live_dvb_t_table = {
	.scan = ir_codes_dntv_live_dvb_t,
	.size = ARRAY_SIZE(ir_codes_dntv_live_dvb_t),
};
EXPORT_SYMBOL_GPL(ir_codes_dntv_live_dvb_t_table);

/* ---------------------------------------------------------------------- */

/* IO-DATA BCTV7E Remote */
static struct ir_scancode ir_codes_iodata_bctv7e[] = {
	{ 0x40, KEY_TV },
	{ 0x20, KEY_RADIO },		/* FM */
	{ 0x60, KEY_EPG },
	{ 0x00, KEY_POWER },

	/* Keys 0 to 9 */
	{ 0x44, KEY_0 },		/* 10 */
	{ 0x50, KEY_1 },
	{ 0x30, KEY_2 },
	{ 0x70, KEY_3 },
	{ 0x48, KEY_4 },
	{ 0x28, KEY_5 },
	{ 0x68, KEY_6 },
	{ 0x58, KEY_7 },
	{ 0x38, KEY_8 },
	{ 0x78, KEY_9 },

	{ 0x10, KEY_L },		/* Live */
	{ 0x08, KEY_TIME },		/* Time Shift */

	{ 0x18, KEY_PLAYPAUSE },	/* Play */

	{ 0x24, KEY_ENTER },		/* 11 */
	{ 0x64, KEY_ESC },		/* 12 */
	{ 0x04, KEY_M },		/* Multi */

	{ 0x54, KEY_VIDEO },
	{ 0x34, KEY_CHANNELUP },
	{ 0x74, KEY_VOLUMEUP },
	{ 0x14, KEY_MUTE },

	{ 0x4c, KEY_VCR },		/* SVIDEO */
	{ 0x2c, KEY_CHANNELDOWN },
	{ 0x6c, KEY_VOLUMEDOWN },
	{ 0x0c, KEY_ZOOM },

	{ 0x5c, KEY_PAUSE },
	{ 0x3c, KEY_RED },		/* || (red) */
	{ 0x7c, KEY_RECORD },		/* recording */
	{ 0x1c, KEY_STOP },

	{ 0x41, KEY_REWIND },		/* backward << */
	{ 0x21, KEY_PLAY },
	{ 0x61, KEY_FASTFORWARD },	/* forward >> */
	{ 0x01, KEY_NEXT },		/* skip >| */
};

struct ir_scancode_table ir_codes_iodata_bctv7e_table = {
	.scan = ir_codes_iodata_bctv7e,
	.size = ARRAY_SIZE(ir_codes_iodata_bctv7e),
};
EXPORT_SYMBOL_GPL(ir_codes_iodata_bctv7e_table);

/* ---------------------------------------------------------------------- */

/* ADS Tech Instant TV DVB-T PCI Remote */
static struct ir_scancode ir_codes_adstech_dvb_t_pci[] = {
	/* Keys 0 to 9 */
	{ 0x4d, KEY_0 },
	{ 0x57, KEY_1 },
	{ 0x4f, KEY_2 },
	{ 0x53, KEY_3 },
	{ 0x56, KEY_4 },
	{ 0x4e, KEY_5 },
	{ 0x5e, KEY_6 },
	{ 0x54, KEY_7 },
	{ 0x4c, KEY_8 },
	{ 0x5c, KEY_9 },

	{ 0x5b, KEY_POWER },
	{ 0x5f, KEY_MUTE },
	{ 0x55, KEY_GOTO },
	{ 0x5d, KEY_SEARCH },
	{ 0x17, KEY_EPG },		/* Guide */
	{ 0x1f, KEY_MENU },
	{ 0x0f, KEY_UP },
	{ 0x46, KEY_DOWN },
	{ 0x16, KEY_LEFT },
	{ 0x1e, KEY_RIGHT },
	{ 0x0e, KEY_SELECT },		/* Enter */
	{ 0x5a, KEY_INFO },
	{ 0x52, KEY_EXIT },
	{ 0x59, KEY_PREVIOUS },
	{ 0x51, KEY_NEXT },
	{ 0x58, KEY_REWIND },
	{ 0x50, KEY_FORWARD },
	{ 0x44, KEY_PLAYPAUSE },
	{ 0x07, KEY_STOP },
	{ 0x1b, KEY_RECORD },
	{ 0x13, KEY_TUNER },		/* Live */
	{ 0x0a, KEY_A },
	{ 0x12, KEY_B },
	{ 0x03, KEY_PROG1 },		/* 1 */
	{ 0x01, KEY_PROG2 },		/* 2 */
	{ 0x00, KEY_PROG3 },		/* 3 */
	{ 0x06, KEY_DVD },
	{ 0x48, KEY_AUX },		/* Photo */
	{ 0x40, KEY_VIDEO },
	{ 0x19, KEY_AUDIO },		/* Music */
	{ 0x0b, KEY_CHANNELUP },
	{ 0x08, KEY_CHANNELDOWN },
	{ 0x15, KEY_VOLUMEUP },
	{ 0x1c, KEY_VOLUMEDOWN },
};

struct ir_scancode_table ir_codes_adstech_dvb_t_pci_table = {
	.scan = ir_codes_adstech_dvb_t_pci,
	.size = ARRAY_SIZE(ir_codes_adstech_dvb_t_pci),
};
EXPORT_SYMBOL_GPL(ir_codes_adstech_dvb_t_pci_table);

/* ---------------------------------------------------------------------- */

/* MSI TV@nywhere MASTER remote */

static struct ir_scancode ir_codes_msi_tvanywhere[] = {
	/* Keys 0 to 9 */
	{ 0x00, KEY_0 },
	{ 0x01, KEY_1 },
	{ 0x02, KEY_2 },
	{ 0x03, KEY_3 },
	{ 0x04, KEY_4 },
	{ 0x05, KEY_5 },
	{ 0x06, KEY_6 },
	{ 0x07, KEY_7 },
	{ 0x08, KEY_8 },
	{ 0x09, KEY_9 },

	{ 0x0c, KEY_MUTE },
	{ 0x0f, KEY_SCREEN },		/* Full Screen */
	{ 0x10, KEY_FN },		/* Funtion */
	{ 0x11, KEY_TIME },		/* Time shift */
	{ 0x12, KEY_POWER },
	{ 0x13, KEY_MEDIA },		/* MTS */
	{ 0x14, KEY_SLOW },
	{ 0x16, KEY_REWIND },		/* backward << */
	{ 0x17, KEY_ENTER },		/* Return */
	{ 0x18, KEY_FASTFORWARD },	/* forward >> */
	{ 0x1a, KEY_CHANNELUP },
	{ 0x1b, KEY_VOLUMEUP },
	{ 0x1e, KEY_CHANNELDOWN },
	{ 0x1f, KEY_VOLUMEDOWN },
};

struct ir_scancode_table ir_codes_msi_tvanywhere_table = {
	.scan = ir_codes_msi_tvanywhere,
	.size = ARRAY_SIZE(ir_codes_msi_tvanywhere),
};
EXPORT_SYMBOL_GPL(ir_codes_msi_tvanywhere_table);

/* ---------------------------------------------------------------------- */

/*
  Keycodes for remote on the MSI TV@nywhere Plus. The controller IC on the card
  is marked "KS003". The controller is I2C at address 0x30, but does not seem
  to respond to probes until a read is performed from a valid device.
  I don't know why...

  Note: This remote may be of similar or identical design to the
  Pixelview remote (?).  The raw codes and duplicate button codes
  appear to be the same.

  Henry Wong <henry@stuffedcow.net>
  Some changes to formatting and keycodes by Mark Schultz <n9xmj@yahoo.com>

*/

static struct ir_scancode ir_codes_msi_tvanywhere_plus[] = {

/*  ---- Remote Button Layout ----

    POWER   SOURCE  SCAN    MUTE
    TV/FM   1       2       3
    |>      4       5       6
    <|      7       8       9
    ^^UP    0       +       RECALL
    vvDN    RECORD  STOP    PLAY

	MINIMIZE          ZOOM

		  CH+
      VOL-                   VOL+
		  CH-

	SNAPSHOT           MTS

     <<      FUNC    >>     RESET
*/

	{ 0x01, KEY_1 },		/* 1 */
	{ 0x0b, KEY_2 },		/* 2 */
	{ 0x1b, KEY_3 },		/* 3 */
	{ 0x05, KEY_4 },		/* 4 */
	{ 0x09, KEY_5 },		/* 5 */
	{ 0x15, KEY_6 },		/* 6 */
	{ 0x06, KEY_7 },		/* 7 */
	{ 0x0a, KEY_8 },		/* 8 */
	{ 0x12, KEY_9 },		/* 9 */
	{ 0x02, KEY_0 },		/* 0 */
	{ 0x10, KEY_KPPLUS },		/* + */
	{ 0x13, KEY_AGAIN },		/* Recall */

	{ 0x1e, KEY_POWER },		/* Power */
	{ 0x07, KEY_TUNER },		/* Source */
	{ 0x1c, KEY_SEARCH },		/* Scan */
	{ 0x18, KEY_MUTE },		/* Mute */

	{ 0x03, KEY_RADIO },		/* TV/FM */
	/* The next four keys are duplicates that appear to send the
	   same IR code as Ch+, Ch-, >>, and << .  The raw code assigned
	   to them is the actual code + 0x20 - they will never be
	   detected as such unless some way is discovered to distinguish
	   these buttons from those that have the same code. */
	{ 0x3f, KEY_RIGHT },		/* |> and Ch+ */
	{ 0x37, KEY_LEFT },		/* <| and Ch- */
	{ 0x2c, KEY_UP },		/* ^^Up and >> */
	{ 0x24, KEY_DOWN },		/* vvDn and << */

	{ 0x00, KEY_RECORD },		/* Record */
	{ 0x08, KEY_STOP },		/* Stop */
	{ 0x11, KEY_PLAY },		/* Play */

	{ 0x0f, KEY_CLOSE },		/* Minimize */
	{ 0x19, KEY_ZOOM },		/* Zoom */
	{ 0x1a, KEY_CAMERA },		/* Snapshot */
	{ 0x0d, KEY_LANGUAGE },		/* MTS */

	{ 0x14, KEY_VOLUMEDOWN },	/* Vol- */
	{ 0x16, KEY_VOLUMEUP },		/* Vol+ */
	{ 0x17, KEY_CHANNELDOWN },	/* Ch- */
	{ 0x1f, KEY_CHANNELUP },	/* Ch+ */

	{ 0x04, KEY_REWIND },		/* << */
	{ 0x0e, KEY_MENU },		/* Function */
	{ 0x0c, KEY_FASTFORWARD },	/* >> */
	{ 0x1d, KEY_RESTART },		/* Reset */
};

struct ir_scancode_table ir_codes_msi_tvanywhere_plus_table = {
	.scan = ir_codes_msi_tvanywhere_plus,
	.size = ARRAY_SIZE(ir_codes_msi_tvanywhere_plus),
};
EXPORT_SYMBOL_GPL(ir_codes_msi_tvanywhere_plus_table);

/* ---------------------------------------------------------------------- */

/* Cinergy 1400 DVB-T */
static struct ir_scancode ir_codes_cinergy_1400[] = {
	{ 0x01, KEY_POWER },
	{ 0x02, KEY_1 },
	{ 0x03, KEY_2 },
	{ 0x04, KEY_3 },
	{ 0x05, KEY_4 },
	{ 0x06, KEY_5 },
	{ 0x07, KEY_6 },
	{ 0x08, KEY_7 },
	{ 0x09, KEY_8 },
	{ 0x0a, KEY_9 },
	{ 0x0c, KEY_0 },

	{ 0x0b, KEY_VIDEO },
	{ 0x0d, KEY_REFRESH },
	{ 0x0e, KEY_SELECT },
	{ 0x0f, KEY_EPG },
	{ 0x10, KEY_UP },
	{ 0x11, KEY_LEFT },
	{ 0x12, KEY_OK },
	{ 0x13, KEY_RIGHT },
	{ 0x14, KEY_DOWN },
	{ 0x15, KEY_TEXT },
	{ 0x16, KEY_INFO },

	{ 0x17, KEY_RED },
	{ 0x18, KEY_GREEN },
	{ 0x19, KEY_YELLOW },
	{ 0x1a, KEY_BLUE },

	{ 0x1b, KEY_CHANNELUP },
	{ 0x1c, KEY_VOLUMEUP },
	{ 0x1d, KEY_MUTE },
	{ 0x1e, KEY_VOLUMEDOWN },
	{ 0x1f, KEY_CHANNELDOWN },

	{ 0x40, KEY_PAUSE },
	{ 0x4c, KEY_PLAY },
	{ 0x58, KEY_RECORD },
	{ 0x54, KEY_PREVIOUS },
	{ 0x48, KEY_STOP },
	{ 0x5c, KEY_NEXT },
};

struct ir_scancode_table ir_codes_cinergy_1400_table = {
	.scan = ir_codes_cinergy_1400,
	.size = ARRAY_SIZE(ir_codes_cinergy_1400),
};
EXPORT_SYMBOL_GPL(ir_codes_cinergy_1400_table);

/* ---------------------------------------------------------------------- */

/* AVERTV STUDIO 303 Remote */
static struct ir_scancode ir_codes_avertv_303[] = {
	{ 0x2a, KEY_1 },
	{ 0x32, KEY_2 },
	{ 0x3a, KEY_3 },
	{ 0x4a, KEY_4 },
	{ 0x52, KEY_5 },
	{ 0x5a, KEY_6 },
	{ 0x6a, KEY_7 },
	{ 0x72, KEY_8 },
	{ 0x7a, KEY_9 },
	{ 0x0e, KEY_0 },

	{ 0x02, KEY_POWER },
	{ 0x22, KEY_VIDEO },
	{ 0x42, KEY_AUDIO },
	{ 0x62, KEY_ZOOM },
	{ 0x0a, KEY_TV },
	{ 0x12, KEY_CD },
	{ 0x1a, KEY_TEXT },

	{ 0x16, KEY_SUBTITLE },
	{ 0x1e, KEY_REWIND },
	{ 0x06, KEY_PRINT },

	{ 0x2e, KEY_SEARCH },
	{ 0x36, KEY_SLEEP },
	{ 0x3e, KEY_SHUFFLE },
	{ 0x26, KEY_MUTE },

	{ 0x4e, KEY_RECORD },
	{ 0x56, KEY_PAUSE },
	{ 0x5e, KEY_STOP },
	{ 0x46, KEY_PLAY },

	{ 0x6e, KEY_RED },
	{ 0x0b, KEY_GREEN },
	{ 0x66, KEY_YELLOW },
	{ 0x03, KEY_BLUE },

	{ 0x76, KEY_LEFT },
	{ 0x7e, KEY_RIGHT },
	{ 0x13, KEY_DOWN },
	{ 0x1b, KEY_UP },
};

struct ir_scancode_table ir_codes_avertv_303_table = {
	.scan = ir_codes_avertv_303,
	.size = ARRAY_SIZE(ir_codes_avertv_303),
};
EXPORT_SYMBOL_GPL(ir_codes_avertv_303_table);

/* ---------------------------------------------------------------------- */

/* DigitalNow DNTV Live! DVB-T Pro Remote */
static struct ir_scancode ir_codes_dntv_live_dvbt_pro[] = {
	{ 0x16, KEY_POWER },
	{ 0x5b, KEY_HOME },

	{ 0x55, KEY_TV },		/* live tv */
	{ 0x58, KEY_TUNER },		/* digital Radio */
	{ 0x5a, KEY_RADIO },		/* FM radio */
	{ 0x59, KEY_DVD },		/* dvd menu */
	{ 0x03, KEY_1 },
	{ 0x01, KEY_2 },
	{ 0x06, KEY_3 },
	{ 0x09, KEY_4 },
	{ 0x1d, KEY_5 },
	{ 0x1f, KEY_6 },
	{ 0x0d, KEY_7 },
	{ 0x19, KEY_8 },
	{ 0x1b, KEY_9 },
	{ 0x0c, KEY_CANCEL },
	{ 0x15, KEY_0 },
	{ 0x4a, KEY_CLEAR },
	{ 0x13, KEY_BACK },
	{ 0x00, KEY_TAB },
	{ 0x4b, KEY_UP },
	{ 0x4e, KEY_LEFT },
	{ 0x4f, KEY_OK },
	{ 0x52, KEY_RIGHT },
	{ 0x51, KEY_DOWN },
	{ 0x1e, KEY_VOLUMEUP },
	{ 0x0a, KEY_VOLUMEDOWN },
	{ 0x02, KEY_CHANNELDOWN },
	{ 0x05, KEY_CHANNELUP },
	{ 0x11, KEY_RECORD },
	{ 0x14, KEY_PLAY },
	{ 0x4c, KEY_PAUSE },
	{ 0x1a, KEY_STOP },
	{ 0x40, KEY_REWIND },
	{ 0x12, KEY_FASTFORWARD },
	{ 0x41, KEY_PREVIOUSSONG },	/* replay |< */
	{ 0x42, KEY_NEXTSONG },		/* skip >| */
	{ 0x54, KEY_CAMERA },		/* capture */
	{ 0x50, KEY_LANGUAGE },		/* sap */
	{ 0x47, KEY_TV2 },		/* pip */
	{ 0x4d, KEY_SCREEN },
	{ 0x43, KEY_SUBTITLE },
	{ 0x10, KEY_MUTE },
	{ 0x49, KEY_AUDIO },		/* l/r */
	{ 0x07, KEY_SLEEP },
	{ 0x08, KEY_VIDEO },		/* a/v */
	{ 0x0e, KEY_PREVIOUS },		/* recall */
	{ 0x45, KEY_ZOOM },		/* zoom + */
	{ 0x46, KEY_ANGLE },		/* zoom - */
	{ 0x56, KEY_RED },
	{ 0x57, KEY_GREEN },
	{ 0x5c, KEY_YELLOW },
	{ 0x5d, KEY_BLUE },
};

struct ir_scancode_table ir_codes_dntv_live_dvbt_pro_table = {
	.scan = ir_codes_dntv_live_dvbt_pro,
	.size = ARRAY_SIZE(ir_codes_dntv_live_dvbt_pro),
};
EXPORT_SYMBOL_GPL(ir_codes_dntv_live_dvbt_pro_table);

static struct ir_scancode ir_codes_em_terratec[] = {
	{ 0x01, KEY_CHANNEL },
	{ 0x02, KEY_SELECT },
	{ 0x03, KEY_MUTE },
	{ 0x04, KEY_POWER },
	{ 0x05, KEY_1 },
	{ 0x06, KEY_2 },
	{ 0x07, KEY_3 },
	{ 0x08, KEY_CHANNELUP },
	{ 0x09, KEY_4 },
	{ 0x0a, KEY_5 },
	{ 0x0b, KEY_6 },
	{ 0x0c, KEY_CHANNELDOWN },
	{ 0x0d, KEY_7 },
	{ 0x0e, KEY_8 },
	{ 0x0f, KEY_9 },
	{ 0x10, KEY_VOLUMEUP },
	{ 0x11, KEY_0 },
	{ 0x12, KEY_MENU },
	{ 0x13, KEY_PRINT },
	{ 0x14, KEY_VOLUMEDOWN },
	{ 0x16, KEY_PAUSE },
	{ 0x18, KEY_RECORD },
	{ 0x19, KEY_REWIND },
	{ 0x1a, KEY_PLAY },
	{ 0x1b, KEY_FORWARD },
	{ 0x1c, KEY_BACKSPACE },
	{ 0x1e, KEY_STOP },
	{ 0x40, KEY_ZOOM },
};

struct ir_scancode_table ir_codes_em_terratec_table = {
	.scan = ir_codes_em_terratec,
	.size = ARRAY_SIZE(ir_codes_em_terratec),
};
EXPORT_SYMBOL_GPL(ir_codes_em_terratec_table);

static struct ir_scancode ir_codes_pinnacle_grey[] = {
	{ 0x3a, KEY_0 },
	{ 0x31, KEY_1 },
	{ 0x32, KEY_2 },
	{ 0x33, KEY_3 },
	{ 0x34, KEY_4 },
	{ 0x35, KEY_5 },
	{ 0x36, KEY_6 },
	{ 0x37, KEY_7 },
	{ 0x38, KEY_8 },
	{ 0x39, KEY_9 },

	{ 0x2f, KEY_POWER },

	{ 0x2e, KEY_P },
	{ 0x1f, KEY_L },
	{ 0x2b, KEY_I },

	{ 0x2d, KEY_SCREEN },
	{ 0x1e, KEY_ZOOM },
	{ 0x1b, KEY_VOLUMEUP },
	{ 0x0f, KEY_VOLUMEDOWN },
	{ 0x17, KEY_CHANNELUP },
	{ 0x1c, KEY_CHANNELDOWN },
	{ 0x25, KEY_INFO },

	{ 0x3c, KEY_MUTE },

	{ 0x3d, KEY_LEFT },
	{ 0x3b, KEY_RIGHT },

	{ 0x3f, KEY_UP },
	{ 0x3e, KEY_DOWN },
	{ 0x1a, KEY_ENTER },

	{ 0x1d, KEY_MENU },
	{ 0x19, KEY_AGAIN },
	{ 0x16, KEY_PREVIOUSSONG },
	{ 0x13, KEY_NEXTSONG },
	{ 0x15, KEY_PAUSE },
	{ 0x0e, KEY_REWIND },
	{ 0x0d, KEY_PLAY },
	{ 0x0b, KEY_STOP },
	{ 0x07, KEY_FORWARD },
	{ 0x27, KEY_RECORD },
	{ 0x26, KEY_TUNER },
	{ 0x29, KEY_TEXT },
	{ 0x2a, KEY_MEDIA },
	{ 0x18, KEY_EPG },
};

struct ir_scancode_table ir_codes_pinnacle_grey_table = {
	.scan = ir_codes_pinnacle_grey,
	.size = ARRAY_SIZE(ir_codes_pinnacle_grey),
};
EXPORT_SYMBOL_GPL(ir_codes_pinnacle_grey_table);

static struct ir_scancode ir_codes_flyvideo[] = {
	{ 0x0f, KEY_0 },
	{ 0x03, KEY_1 },
	{ 0x04, KEY_2 },
	{ 0x05, KEY_3 },
	{ 0x07, KEY_4 },
	{ 0x08, KEY_5 },
	{ 0x09, KEY_6 },
	{ 0x0b, KEY_7 },
	{ 0x0c, KEY_8 },
	{ 0x0d, KEY_9 },

	{ 0x0e, KEY_MODE },	/* Air/Cable */
	{ 0x11, KEY_VIDEO },	/* Video */
	{ 0x15, KEY_AUDIO },	/* Audio */
	{ 0x00, KEY_POWER },	/* Power */
	{ 0x18, KEY_TUNER },	/* AV Source */
	{ 0x02, KEY_ZOOM },	/* Fullscreen */
	{ 0x1a, KEY_LANGUAGE },	/* Stereo */
	{ 0x1b, KEY_MUTE },	/* Mute */
	{ 0x14, KEY_VOLUMEUP },	/* Volume + */
	{ 0x17, KEY_VOLUMEDOWN },/* Volume - */
	{ 0x12, KEY_CHANNELUP },/* Channel + */
	{ 0x13, KEY_CHANNELDOWN },/* Channel - */
	{ 0x06, KEY_AGAIN },	/* Recall */
	{ 0x10, KEY_ENTER },	/* Enter */

	{ 0x19, KEY_BACK },	/* Rewind  ( <<< ) */
	{ 0x1f, KEY_FORWARD },	/* Forward ( >>> ) */
	{ 0x0a, KEY_ANGLE },	/* no label, may be used as the PAUSE button */
};

struct ir_scancode_table ir_codes_flyvideo_table = {
	.scan = ir_codes_flyvideo,
	.size = ARRAY_SIZE(ir_codes_flyvideo),
};
EXPORT_SYMBOL_GPL(ir_codes_flyvideo_table);

static struct ir_scancode ir_codes_flydvb[] = {
	{ 0x01, KEY_ZOOM },		/* Full Screen */
	{ 0x00, KEY_POWER },		/* Power */

	{ 0x03, KEY_1 },
	{ 0x04, KEY_2 },
	{ 0x05, KEY_3 },
	{ 0x07, KEY_4 },
	{ 0x08, KEY_5 },
	{ 0x09, KEY_6 },
	{ 0x0b, KEY_7 },
	{ 0x0c, KEY_8 },
	{ 0x0d, KEY_9 },
	{ 0x06, KEY_AGAIN },		/* Recall */
	{ 0x0f, KEY_0 },
	{ 0x10, KEY_MUTE },		/* Mute */
	{ 0x02, KEY_RADIO },		/* TV/Radio */
	{ 0x1b, KEY_LANGUAGE },		/* SAP (Second Audio Program) */

	{ 0x14, KEY_VOLUMEUP },		/* VOL+ */
	{ 0x17, KEY_VOLUMEDOWN },	/* VOL- */
	{ 0x12, KEY_CHANNELUP },	/* CH+ */
	{ 0x13, KEY_CHANNELDOWN },	/* CH- */
	{ 0x1d, KEY_ENTER },		/* Enter */

	{ 0x1a, KEY_MODE },		/* PIP */
	{ 0x18, KEY_TUNER },		/* Source */

	{ 0x1e, KEY_RECORD },		/* Record/Pause */
	{ 0x15, KEY_ANGLE },		/* Swap (no label on key) */
	{ 0x1c, KEY_PAUSE },		/* Timeshift/Pause */
	{ 0x19, KEY_BACK },		/* Rewind << */
	{ 0x0a, KEY_PLAYPAUSE },	/* Play/Pause */
	{ 0x1f, KEY_FORWARD },		/* Forward >> */
	{ 0x16, KEY_PREVIOUS },		/* Back |<< */
	{ 0x11, KEY_STOP },		/* Stop */
	{ 0x0e, KEY_NEXT },		/* End >>| */
};

struct ir_scancode_table ir_codes_flydvb_table = {
	.scan = ir_codes_flydvb,
	.size = ARRAY_SIZE(ir_codes_flydvb),
};
EXPORT_SYMBOL_GPL(ir_codes_flydvb_table);

static struct ir_scancode ir_codes_cinergy[] = {
	{ 0x00, KEY_0 },
	{ 0x01, KEY_1 },
	{ 0x02, KEY_2 },
	{ 0x03, KEY_3 },
	{ 0x04, KEY_4 },
	{ 0x05, KEY_5 },
	{ 0x06, KEY_6 },
	{ 0x07, KEY_7 },
	{ 0x08, KEY_8 },
	{ 0x09, KEY_9 },

	{ 0x0a, KEY_POWER },
	{ 0x0b, KEY_PROG1 },		/* app */
	{ 0x0c, KEY_ZOOM },		/* zoom/fullscreen */
	{ 0x0d, KEY_CHANNELUP },	/* channel */
	{ 0x0e, KEY_CHANNELDOWN },	/* channel- */
	{ 0x0f, KEY_VOLUMEUP },
	{ 0x10, KEY_VOLUMEDOWN },
	{ 0x11, KEY_TUNER },		/* AV */
	{ 0x12, KEY_NUMLOCK },		/* -/-- */
	{ 0x13, KEY_AUDIO },		/* audio */
	{ 0x14, KEY_MUTE },
	{ 0x15, KEY_UP },
	{ 0x16, KEY_DOWN },
	{ 0x17, KEY_LEFT },
	{ 0x18, KEY_RIGHT },
	{ 0x19, BTN_LEFT, },
	{ 0x1a, BTN_RIGHT, },
	{ 0x1b, KEY_WWW },		/* text */
	{ 0x1c, KEY_REWIND },
	{ 0x1d, KEY_FORWARD },
	{ 0x1e, KEY_RECORD },
	{ 0x1f, KEY_PLAY },
	{ 0x20, KEY_PREVIOUSSONG },
	{ 0x21, KEY_NEXTSONG },
	{ 0x22, KEY_PAUSE },
	{ 0x23, KEY_STOP },
};

struct ir_scancode_table ir_codes_cinergy_table = {
	.scan = ir_codes_cinergy,
	.size = ARRAY_SIZE(ir_codes_cinergy),
};
EXPORT_SYMBOL_GPL(ir_codes_cinergy_table);

/* Alfons Geser <a.geser@cox.net>
 * updates from Job D. R. Borges <jobdrb@ig.com.br> */
static struct ir_scancode ir_codes_eztv[] = {
	{ 0x12, KEY_POWER },
	{ 0x01, KEY_TV },	/* DVR */
	{ 0x15, KEY_DVD },	/* DVD */
	{ 0x17, KEY_AUDIO },	/* music */
				/* DVR mode / DVD mode / music mode */

	{ 0x1b, KEY_MUTE },	/* mute */
	{ 0x02, KEY_LANGUAGE },	/* MTS/SAP / audio / autoseek */
	{ 0x1e, KEY_SUBTITLE },	/* closed captioning / subtitle / seek */
	{ 0x16, KEY_ZOOM },	/* full screen */
	{ 0x1c, KEY_VIDEO },	/* video source / eject / delall */
	{ 0x1d, KEY_RESTART },	/* playback / angle / del */
	{ 0x2f, KEY_SEARCH },	/* scan / menu / playlist */
	{ 0x30, KEY_CHANNEL },	/* CH surfing / bookmark / memo */

	{ 0x31, KEY_HELP },	/* help */
	{ 0x32, KEY_MODE },	/* num/memo */
	{ 0x33, KEY_ESC },	/* cancel */

	{ 0x0c, KEY_UP },	/* up */
	{ 0x10, KEY_DOWN },	/* down */
	{ 0x08, KEY_LEFT },	/* left */
	{ 0x04, KEY_RIGHT },	/* right */
	{ 0x03, KEY_SELECT },	/* select */

	{ 0x1f, KEY_REWIND },	/* rewind */
	{ 0x20, KEY_PLAYPAUSE },/* play/pause */
	{ 0x29, KEY_FORWARD },	/* forward */
	{ 0x14, KEY_AGAIN },	/* repeat */
	{ 0x2b, KEY_RECORD },	/* recording */
	{ 0x2c, KEY_STOP },	/* stop */
	{ 0x2d, KEY_PLAY },	/* play */
	{ 0x2e, KEY_CAMERA },	/* snapshot / shuffle */

	{ 0x00, KEY_0 },
	{ 0x05, KEY_1 },
	{ 0x06, KEY_2 },
	{ 0x07, KEY_3 },
	{ 0x09, KEY_4 },
	{ 0x0a, KEY_5 },
	{ 0x0b, KEY_6 },
	{ 0x0d, KEY_7 },
	{ 0x0e, KEY_8 },
	{ 0x0f, KEY_9 },

	{ 0x2a, KEY_VOLUMEUP },
	{ 0x11, KEY_VOLUMEDOWN },
	{ 0x18, KEY_CHANNELUP },/* CH.tracking up */
	{ 0x19, KEY_CHANNELDOWN },/* CH.tracking down */

	{ 0x13, KEY_ENTER },	/* enter */
	{ 0x21, KEY_DOT },	/* . (decimal dot) */
};

struct ir_scancode_table ir_codes_eztv_table = {
	.scan = ir_codes_eztv,
	.size = ARRAY_SIZE(ir_codes_eztv),
};
EXPORT_SYMBOL_GPL(ir_codes_eztv_table);

/* Alex Hermann <gaaf@gmx.net> */
static struct ir_scancode ir_codes_avermedia[] = {
	{ 0x28, KEY_1 },
	{ 0x18, KEY_2 },
	{ 0x38, KEY_3 },
	{ 0x24, KEY_4 },
	{ 0x14, KEY_5 },
	{ 0x34, KEY_6 },
	{ 0x2c, KEY_7 },
	{ 0x1c, KEY_8 },
	{ 0x3c, KEY_9 },
	{ 0x22, KEY_0 },

	{ 0x20, KEY_TV },		/* TV/FM */
	{ 0x10, KEY_CD },		/* CD */
	{ 0x30, KEY_TEXT },		/* TELETEXT */
	{ 0x00, KEY_POWER },		/* POWER */

	{ 0x08, KEY_VIDEO },		/* VIDEO */
	{ 0x04, KEY_AUDIO },		/* AUDIO */
	{ 0x0c, KEY_ZOOM },		/* FULL SCREEN */

	{ 0x12, KEY_SUBTITLE },		/* DISPLAY */
	{ 0x32, KEY_REWIND },		/* LOOP	*/
	{ 0x02, KEY_PRINT },		/* PREVIEW */

	{ 0x2a, KEY_SEARCH },		/* AUTOSCAN */
	{ 0x1a, KEY_SLEEP },		/* FREEZE */
	{ 0x3a, KEY_CAMERA },		/* SNAPSHOT */
	{ 0x0a, KEY_MUTE },		/* MUTE */

	{ 0x26, KEY_RECORD },		/* RECORD */
	{ 0x16, KEY_PAUSE },		/* PAUSE */
	{ 0x36, KEY_STOP },		/* STOP */
	{ 0x06, KEY_PLAY },		/* PLAY */

	{ 0x2e, KEY_RED },		/* RED */
	{ 0x21, KEY_GREEN },		/* GREEN */
	{ 0x0e, KEY_YELLOW },		/* YELLOW */
	{ 0x01, KEY_BLUE },		/* BLUE */

	{ 0x1e, KEY_VOLUMEDOWN },	/* VOLUME- */
	{ 0x3e, KEY_VOLUMEUP },		/* VOLUME+ */
	{ 0x11, KEY_CHANNELDOWN },	/* CHANNEL/PAGE- */
	{ 0x31, KEY_CHANNELUP }		/* CHANNEL/PAGE+ */
};

struct ir_scancode_table ir_codes_avermedia_table = {
	.scan = ir_codes_avermedia,
	.size = ARRAY_SIZE(ir_codes_avermedia),
};
EXPORT_SYMBOL_GPL(ir_codes_avermedia_table);

static struct ir_scancode ir_codes_videomate_tv_pvr[] = {
	{ 0x14, KEY_MUTE },
	{ 0x24, KEY_ZOOM },

	{ 0x01, KEY_DVD },
	{ 0x23, KEY_RADIO },
	{ 0x00, KEY_TV },

	{ 0x0a, KEY_REWIND },
	{ 0x08, KEY_PLAYPAUSE },
	{ 0x0f, KEY_FORWARD },

	{ 0x02, KEY_PREVIOUS },
	{ 0x07, KEY_STOP },
	{ 0x06, KEY_NEXT },

	{ 0x0c, KEY_UP },
	{ 0x0e, KEY_DOWN },
	{ 0x0b, KEY_LEFT },
	{ 0x0d, KEY_RIGHT },
	{ 0x11, KEY_OK },

	{ 0x03, KEY_MENU },
	{ 0x09, KEY_SETUP },
	{ 0x05, KEY_VIDEO },
	{ 0x22, KEY_CHANNEL },

	{ 0x12, KEY_VOLUMEUP },
	{ 0x15, KEY_VOLUMEDOWN },
	{ 0x10, KEY_CHANNELUP },
	{ 0x13, KEY_CHANNELDOWN },

	{ 0x04, KEY_RECORD },

	{ 0x16, KEY_1 },
	{ 0x17, KEY_2 },
	{ 0x18, KEY_3 },
	{ 0x19, KEY_4 },
	{ 0x1a, KEY_5 },
	{ 0x1b, KEY_6 },
	{ 0x1c, KEY_7 },
	{ 0x1d, KEY_8 },
	{ 0x1e, KEY_9 },
	{ 0x1f, KEY_0 },

	{ 0x20, KEY_LANGUAGE },
	{ 0x21, KEY_SLEEP },
};

struct ir_scancode_table ir_codes_videomate_tv_pvr_table = {
	.scan = ir_codes_videomate_tv_pvr,
	.size = ARRAY_SIZE(ir_codes_videomate_tv_pvr),
};
EXPORT_SYMBOL_GPL(ir_codes_videomate_tv_pvr_table);

/* Michael Tokarev <mjt@tls.msk.ru>
   http://www.corpit.ru/mjt/beholdTV/remote_control.jpg
   keytable is used by MANLI MTV00[0x0c] and BeholdTV 40[13] at
   least, and probably other cards too.
   The "ascii-art picture" below (in comments, first row
   is the keycode in hex, and subsequent row(s) shows
   the button labels (several variants when appropriate)
   helps to descide which keycodes to assign to the buttons.
 */
static struct ir_scancode ir_codes_manli[] = {

	/*  0x1c            0x12  *
	 * FUNCTION         POWER *
	 *   FM              (|)  *
	 *                        */
	{ 0x1c, KEY_RADIO },	/*XXX*/
	{ 0x12, KEY_POWER },

	/*  0x01    0x02    0x03  *
	 *   1       2       3    *
	 *                        *
	 *  0x04    0x05    0x06  *
	 *   4       5       6    *
	 *                        *
	 *  0x07    0x08    0x09  *
	 *   7       8       9    *
	 *                        */
	{ 0x01, KEY_1 },
	{ 0x02, KEY_2 },
	{ 0x03, KEY_3 },
	{ 0x04, KEY_4 },
	{ 0x05, KEY_5 },
	{ 0x06, KEY_6 },
	{ 0x07, KEY_7 },
	{ 0x08, KEY_8 },
	{ 0x09, KEY_9 },

	/*  0x0a    0x00    0x17  *
	 * RECALL    0      +100  *
	 *                  PLUS  *
	 *                        */
	{ 0x0a, KEY_AGAIN },	/*XXX KEY_REWIND? */
	{ 0x00, KEY_0 },
	{ 0x17, KEY_DIGITS },	/*XXX*/

	/*  0x14            0x10  *
	 *  MENU            INFO  *
	 *  OSD                   */
	{ 0x14, KEY_MENU },
	{ 0x10, KEY_INFO },

	/*          0x0b          *
	 *           Up           *
	 *                        *
	 *  0x18    0x16    0x0c  *
	 *  Left     Ok     Right *
	 *                        *
	 *         0x015          *
	 *         Down           *
	 *                        */
	{ 0x0b, KEY_UP },
	{ 0x18, KEY_LEFT },
	{ 0x16, KEY_OK },	/*XXX KEY_SELECT? KEY_ENTER? */
	{ 0x0c, KEY_RIGHT },
	{ 0x15, KEY_DOWN },

	/*  0x11            0x0d  *
	 *  TV/AV           MODE  *
	 *  SOURCE         STEREO *
	 *                        */
	{ 0x11, KEY_TV },	/*XXX*/
	{ 0x0d, KEY_MODE },	/*XXX there's no KEY_STEREO	*/

	/*  0x0f    0x1b    0x1a  *
	 *  AUDIO   Vol+    Chan+ *
	 *        TIMESHIFT???    *
	 *                        *
	 *  0x0e    0x1f    0x1e  *
	 *  SLEEP   Vol-    Chan- *
	 *                        */
	{ 0x0f, KEY_AUDIO },
	{ 0x1b, KEY_VOLUMEUP },
	{ 0x1a, KEY_CHANNELUP },
	{ 0x0e, KEY_TIME },
	{ 0x1f, KEY_VOLUMEDOWN },
	{ 0x1e, KEY_CHANNELDOWN },

	/*         0x13     0x19  *
	 *         MUTE   SNAPSHOT*
	 *                        */
	{ 0x13, KEY_MUTE },
	{ 0x19, KEY_CAMERA },

	/* 0x1d unused ? */
};

struct ir_scancode_table ir_codes_manli_table = {
	.scan = ir_codes_manli,
	.size = ARRAY_SIZE(ir_codes_manli),
};
EXPORT_SYMBOL_GPL(ir_codes_manli_table);

/* Mike Baikov <mike@baikov.com> */
static struct ir_scancode ir_codes_gotview7135[] = {

	{ 0x11, KEY_POWER },
	{ 0x35, KEY_TV },
	{ 0x1b, KEY_0 },
	{ 0x29, KEY_1 },
	{ 0x19, KEY_2 },
	{ 0x39, KEY_3 },
	{ 0x1f, KEY_4 },
	{ 0x2c, KEY_5 },
	{ 0x21, KEY_6 },
	{ 0x24, KEY_7 },
	{ 0x18, KEY_8 },
	{ 0x2b, KEY_9 },
	{ 0x3b, KEY_AGAIN },	/* LOOP */
	{ 0x06, KEY_AUDIO },
	{ 0x31, KEY_PRINT },	/* PREVIEW */
	{ 0x3e, KEY_VIDEO },
	{ 0x10, KEY_CHANNELUP },
	{ 0x20, KEY_CHANNELDOWN },
	{ 0x0c, KEY_VOLUMEDOWN },
	{ 0x28, KEY_VOLUMEUP },
	{ 0x08, KEY_MUTE },
	{ 0x26, KEY_SEARCH },	/* SCAN */
	{ 0x3f, KEY_CAMERA },	/* SNAPSHOT */
	{ 0x12, KEY_RECORD },
	{ 0x32, KEY_STOP },
	{ 0x3c, KEY_PLAY },
	{ 0x1d, KEY_REWIND },
	{ 0x2d, KEY_PAUSE },
	{ 0x0d, KEY_FORWARD },
	{ 0x05, KEY_ZOOM },	/*FULL*/

	{ 0x2a, KEY_F21 },	/* LIVE TIMESHIFT */
	{ 0x0e, KEY_F22 },	/* MIN TIMESHIFT */
	{ 0x1e, KEY_TIME },	/* TIMESHIFT */
	{ 0x38, KEY_F24 },	/* NORMAL TIMESHIFT */
};

struct ir_scancode_table ir_codes_gotview7135_table = {
	.scan = ir_codes_gotview7135,
	.size = ARRAY_SIZE(ir_codes_gotview7135),
};
EXPORT_SYMBOL_GPL(ir_codes_gotview7135_table);

static struct ir_scancode ir_codes_purpletv[] = {
	{ 0x03, KEY_POWER },
	{ 0x6f, KEY_MUTE },
	{ 0x10, KEY_BACKSPACE },	/* Recall */

	{ 0x11, KEY_0 },
	{ 0x04, KEY_1 },
	{ 0x05, KEY_2 },
	{ 0x06, KEY_3 },
	{ 0x08, KEY_4 },
	{ 0x09, KEY_5 },
	{ 0x0a, KEY_6 },
	{ 0x0c, KEY_7 },
	{ 0x0d, KEY_8 },
	{ 0x0e, KEY_9 },
	{ 0x12, KEY_DOT },	/* 100+ */

	{ 0x07, KEY_VOLUMEUP },
	{ 0x0b, KEY_VOLUMEDOWN },
	{ 0x1a, KEY_KPPLUS },
	{ 0x18, KEY_KPMINUS },
	{ 0x15, KEY_UP },
	{ 0x1d, KEY_DOWN },
	{ 0x0f, KEY_CHANNELUP },
	{ 0x13, KEY_CHANNELDOWN },
	{ 0x48, KEY_ZOOM },

	{ 0x1b, KEY_VIDEO },	/* Video source */
	{ 0x1f, KEY_CAMERA },	/* Snapshot */
	{ 0x49, KEY_LANGUAGE },	/* MTS Select */
	{ 0x19, KEY_SEARCH },	/* Auto Scan */

	{ 0x4b, KEY_RECORD },
	{ 0x46, KEY_PLAY },
	{ 0x45, KEY_PAUSE },	/* Pause */
	{ 0x44, KEY_STOP },
	{ 0x43, KEY_TIME },	/* Time Shift */
	{ 0x17, KEY_CHANNEL },	/* SURF CH */
	{ 0x40, KEY_FORWARD },	/* Forward ? */
	{ 0x42, KEY_REWIND },	/* Backward ? */

};

struct ir_scancode_table ir_codes_purpletv_table = {
	.scan = ir_codes_purpletv,
	.size = ARRAY_SIZE(ir_codes_purpletv),
};
EXPORT_SYMBOL_GPL(ir_codes_purpletv_table);

/* Mapping for the 28 key remote control as seen at
   http://www.sednacomputer.com/photo/cardbus-tv.jpg
   Pavel Mihaylov <bin@bash.info>
   Also for the remote bundled with Kozumi KTV-01C card */
static struct ir_scancode ir_codes_pctv_sedna[] = {
	{ 0x00, KEY_0 },
	{ 0x01, KEY_1 },
	{ 0x02, KEY_2 },
	{ 0x03, KEY_3 },
	{ 0x04, KEY_4 },
	{ 0x05, KEY_5 },
	{ 0x06, KEY_6 },
	{ 0x07, KEY_7 },
	{ 0x08, KEY_8 },
	{ 0x09, KEY_9 },

	{ 0x0a, KEY_AGAIN },	/* Recall */
	{ 0x0b, KEY_CHANNELUP },
	{ 0x0c, KEY_VOLUMEUP },
	{ 0x0d, KEY_MODE },	/* Stereo */
	{ 0x0e, KEY_STOP },
	{ 0x0f, KEY_PREVIOUSSONG },
	{ 0x10, KEY_ZOOM },
	{ 0x11, KEY_TUNER },	/* Source */
	{ 0x12, KEY_POWER },
	{ 0x13, KEY_MUTE },
	{ 0x15, KEY_CHANNELDOWN },
	{ 0x18, KEY_VOLUMEDOWN },
	{ 0x19, KEY_CAMERA },	/* Snapshot */
	{ 0x1a, KEY_NEXTSONG },
	{ 0x1b, KEY_TIME },	/* Time Shift */
	{ 0x1c, KEY_RADIO },	/* FM Radio */
	{ 0x1d, KEY_RECORD },
	{ 0x1e, KEY_PAUSE },
	/* additional codes for Kozumi's remote */
	{ 0x14, KEY_INFO },	/* OSD */
	{ 0x16, KEY_OK },	/* OK */
	{ 0x17, KEY_DIGITS },	/* Plus */
	{ 0x1f, KEY_PLAY },	/* Play */
};

struct ir_scancode_table ir_codes_pctv_sedna_table = {
	.scan = ir_codes_pctv_sedna,
	.size = ARRAY_SIZE(ir_codes_pctv_sedna),
};
EXPORT_SYMBOL_GPL(ir_codes_pctv_sedna_table);

/* Mark Phalan <phalanm@o2.ie> */
static struct ir_scancode ir_codes_pv951[] = {
	{ 0x00, KEY_0 },
	{ 0x01, KEY_1 },
	{ 0x02, KEY_2 },
	{ 0x03, KEY_3 },
	{ 0x04, KEY_4 },
	{ 0x05, KEY_5 },
	{ 0x06, KEY_6 },
	{ 0x07, KEY_7 },
	{ 0x08, KEY_8 },
	{ 0x09, KEY_9 },

	{ 0x12, KEY_POWER },
	{ 0x10, KEY_MUTE },
	{ 0x1f, KEY_VOLUMEDOWN },
	{ 0x1b, KEY_VOLUMEUP },
	{ 0x1a, KEY_CHANNELUP },
	{ 0x1e, KEY_CHANNELDOWN },
	{ 0x0e, KEY_PAGEUP },
	{ 0x1d, KEY_PAGEDOWN },
	{ 0x13, KEY_SOUND },

	{ 0x18, KEY_KPPLUSMINUS },	/* CH +/- */
	{ 0x16, KEY_SUBTITLE },		/* CC */
	{ 0x0d, KEY_TEXT },		/* TTX */
	{ 0x0b, KEY_TV },		/* AIR/CBL */
	{ 0x11, KEY_PC },		/* PC/TV */
	{ 0x17, KEY_OK },		/* CH RTN */
	{ 0x19, KEY_MODE },		/* FUNC */
	{ 0x0c, KEY_SEARCH },		/* AUTOSCAN */

	/* Not sure what to do with these ones! */
	{ 0x0f, KEY_SELECT },		/* SOURCE */
	{ 0x0a, KEY_KPPLUS },		/* +100 */
	{ 0x14, KEY_EQUAL },		/* SYNC */
	{ 0x1c, KEY_MEDIA },		/* PC/TV */
};

struct ir_scancode_table ir_codes_pv951_table = {
	.scan = ir_codes_pv951,
	.size = ARRAY_SIZE(ir_codes_pv951),
};
EXPORT_SYMBOL_GPL(ir_codes_pv951_table);

/* generic RC5 keytable                                          */
/* see http://users.pandora.be/nenya/electronics/rc5/codes00.htm */
/* used by old (black) Hauppauge remotes                         */
static struct ir_scancode ir_codes_rc5_tv[] = {
	/* Keys 0 to 9 */
	{ 0x00, KEY_0 },
	{ 0x01, KEY_1 },
	{ 0x02, KEY_2 },
	{ 0x03, KEY_3 },
	{ 0x04, KEY_4 },
	{ 0x05, KEY_5 },
	{ 0x06, KEY_6 },
	{ 0x07, KEY_7 },
	{ 0x08, KEY_8 },
	{ 0x09, KEY_9 },

	{ 0x0b, KEY_CHANNEL },		/* channel / program (japan: 11) */
	{ 0x0c, KEY_POWER },		/* standby */
	{ 0x0d, KEY_MUTE },		/* mute / demute */
	{ 0x0f, KEY_TV },		/* display */
	{ 0x10, KEY_VOLUMEUP },
	{ 0x11, KEY_VOLUMEDOWN },
	{ 0x12, KEY_BRIGHTNESSUP },
	{ 0x13, KEY_BRIGHTNESSDOWN },
	{ 0x1e, KEY_SEARCH },		/* search + */
	{ 0x20, KEY_CHANNELUP },	/* channel / program + */
	{ 0x21, KEY_CHANNELDOWN },	/* channel / program - */
	{ 0x22, KEY_CHANNEL },		/* alt / channel */
	{ 0x23, KEY_LANGUAGE },		/* 1st / 2nd language */
	{ 0x26, KEY_SLEEP },		/* sleeptimer */
	{ 0x2e, KEY_MENU },		/* 2nd controls (USA: menu) */
	{ 0x30, KEY_PAUSE },
	{ 0x32, KEY_REWIND },
	{ 0x33, KEY_GOTO },
	{ 0x35, KEY_PLAY },
	{ 0x36, KEY_STOP },
	{ 0x37, KEY_RECORD },		/* recording */
	{ 0x3c, KEY_TEXT },		/* teletext submode (Japan: 12) */
	{ 0x3d, KEY_SUSPEND },		/* system standby */

};

struct ir_scancode_table ir_codes_rc5_tv_table = {
	.scan = ir_codes_rc5_tv,
	.size = ARRAY_SIZE(ir_codes_rc5_tv),
};
EXPORT_SYMBOL_GPL(ir_codes_rc5_tv_table);

/* Table for Leadtek Winfast Remote Controls - used by both bttv and cx88 */
static struct ir_scancode ir_codes_winfast[] = {
	/* Keys 0 to 9 */
	{ 0x12, KEY_0 },
	{ 0x05, KEY_1 },
	{ 0x06, KEY_2 },
	{ 0x07, KEY_3 },
	{ 0x09, KEY_4 },
	{ 0x0a, KEY_5 },
	{ 0x0b, KEY_6 },
	{ 0x0d, KEY_7 },
	{ 0x0e, KEY_8 },
	{ 0x0f, KEY_9 },

	{ 0x00, KEY_POWER },
	{ 0x1b, KEY_AUDIO },		/* Audio Source */
	{ 0x02, KEY_TUNER },		/* TV/FM, not on Y0400052 */
	{ 0x1e, KEY_VIDEO },		/* Video Source */
	{ 0x16, KEY_INFO },		/* Display information */
	{ 0x04, KEY_VOLUMEUP },
	{ 0x08, KEY_VOLUMEDOWN },
	{ 0x0c, KEY_CHANNELUP },
	{ 0x10, KEY_CHANNELDOWN },
	{ 0x03, KEY_ZOOM },		/* fullscreen */
	{ 0x1f, KEY_TEXT },		/* closed caption/teletext */
	{ 0x20, KEY_SLEEP },
	{ 0x29, KEY_CLEAR },		/* boss key */
	{ 0x14, KEY_MUTE },
	{ 0x2b, KEY_RED },
	{ 0x2c, KEY_GREEN },
	{ 0x2d, KEY_YELLOW },
	{ 0x2e, KEY_BLUE },
	{ 0x18, KEY_KPPLUS },		/* fine tune + , not on Y040052 */
	{ 0x19, KEY_KPMINUS },		/* fine tune - , not on Y040052 */
	{ 0x2a, KEY_MEDIA },		/* PIP (Picture in picture */
	{ 0x21, KEY_DOT },
	{ 0x13, KEY_ENTER },
	{ 0x11, KEY_LAST },		/* Recall (last channel */
	{ 0x22, KEY_PREVIOUS },
	{ 0x23, KEY_PLAYPAUSE },
	{ 0x24, KEY_NEXT },
	{ 0x25, KEY_TIME },		/* Time Shifting */
	{ 0x26, KEY_STOP },
	{ 0x27, KEY_RECORD },
	{ 0x28, KEY_SAVE },		/* Screenshot */
	{ 0x2f, KEY_MENU },
	{ 0x30, KEY_CANCEL },
	{ 0x31, KEY_CHANNEL },		/* Channel Surf */
	{ 0x32, KEY_SUBTITLE },
	{ 0x33, KEY_LANGUAGE },
	{ 0x34, KEY_REWIND },
	{ 0x35, KEY_FASTFORWARD },
	{ 0x36, KEY_TV },
	{ 0x37, KEY_RADIO },		/* FM */
	{ 0x38, KEY_DVD },

	{ 0x3e, KEY_F21 },		/* MCE +VOL, on Y04G0033 */
	{ 0x3a, KEY_F22 },		/* MCE -VOL, on Y04G0033 */
	{ 0x3b, KEY_F23 },		/* MCE +CH,  on Y04G0033 */
	{ 0x3f, KEY_F24 }		/* MCE -CH,  on Y04G0033 */
};

struct ir_scancode_table ir_codes_winfast_table = {
	.scan = ir_codes_winfast,
	.size = ARRAY_SIZE(ir_codes_winfast),
};
EXPORT_SYMBOL_GPL(ir_codes_winfast_table);

static struct ir_scancode ir_codes_pinnacle_color[] = {
	{ 0x59, KEY_MUTE },
	{ 0x4a, KEY_POWER },

	{ 0x18, KEY_TEXT },
	{ 0x26, KEY_TV },
	{ 0x3d, KEY_PRINT },

	{ 0x48, KEY_RED },
	{ 0x04, KEY_GREEN },
	{ 0x11, KEY_YELLOW },
	{ 0x00, KEY_BLUE },

	{ 0x2d, KEY_VOLUMEUP },
	{ 0x1e, KEY_VOLUMEDOWN },

	{ 0x49, KEY_MENU },

	{ 0x16, KEY_CHANNELUP },
	{ 0x17, KEY_CHANNELDOWN },

	{ 0x20, KEY_UP },
	{ 0x21, KEY_DOWN },
	{ 0x22, KEY_LEFT },
	{ 0x23, KEY_RIGHT },
	{ 0x0d, KEY_SELECT },

	{ 0x08, KEY_BACK },
	{ 0x07, KEY_REFRESH },

	{ 0x2f, KEY_ZOOM },
	{ 0x29, KEY_RECORD },

	{ 0x4b, KEY_PAUSE },
	{ 0x4d, KEY_REWIND },
	{ 0x2e, KEY_PLAY },
	{ 0x4e, KEY_FORWARD },
	{ 0x53, KEY_PREVIOUS },
	{ 0x4c, KEY_STOP },
	{ 0x54, KEY_NEXT },

	{ 0x69, KEY_0 },
	{ 0x6a, KEY_1 },
	{ 0x6b, KEY_2 },
	{ 0x6c, KEY_3 },
	{ 0x6d, KEY_4 },
	{ 0x6e, KEY_5 },
	{ 0x6f, KEY_6 },
	{ 0x70, KEY_7 },
	{ 0x71, KEY_8 },
	{ 0x72, KEY_9 },

	{ 0x74, KEY_CHANNEL },
	{ 0x0a, KEY_BACKSPACE },
};

struct ir_scancode_table ir_codes_pinnacle_color_table = {
	.scan = ir_codes_pinnacle_color,
	.size = ARRAY_SIZE(ir_codes_pinnacle_color),
};
EXPORT_SYMBOL_GPL(ir_codes_pinnacle_color_table);

/* Hauppauge: the newer, gray remotes (seems there are multiple
 * slightly different versions), shipped with cx88+ivtv cards.
 * almost rc5 coding, but some non-standard keys */
static struct ir_scancode ir_codes_hauppauge_new[] = {
	/* Keys 0 to 9 */
	{ 0x00, KEY_0 },
	{ 0x01, KEY_1 },
	{ 0x02, KEY_2 },
	{ 0x03, KEY_3 },
	{ 0x04, KEY_4 },
	{ 0x05, KEY_5 },
	{ 0x06, KEY_6 },
	{ 0x07, KEY_7 },
	{ 0x08, KEY_8 },
	{ 0x09, KEY_9 },

	{ 0x0a, KEY_TEXT },		/* keypad asterisk as well */
	{ 0x0b, KEY_RED },		/* red button */
	{ 0x0c, KEY_RADIO },
	{ 0x0d, KEY_MENU },
	{ 0x0e, KEY_SUBTITLE },		/* also the # key */
	{ 0x0f, KEY_MUTE },
	{ 0x10, KEY_VOLUMEUP },
	{ 0x11, KEY_VOLUMEDOWN },
	{ 0x12, KEY_PREVIOUS },		/* previous channel */
	{ 0x14, KEY_UP },
	{ 0x15, KEY_DOWN },
	{ 0x16, KEY_LEFT },
	{ 0x17, KEY_RIGHT },
	{ 0x18, KEY_VIDEO },		/* Videos */
	{ 0x19, KEY_AUDIO },		/* Music */
	/* 0x1a: Pictures - presume this means
	   "Multimedia Home Platform" -
	   no "PICTURES" key in input.h
	 */
	{ 0x1a, KEY_MHP },

	{ 0x1b, KEY_EPG },		/* Guide */
	{ 0x1c, KEY_TV },
	{ 0x1e, KEY_NEXTSONG },		/* skip >| */
	{ 0x1f, KEY_EXIT },		/* back/exit */
	{ 0x20, KEY_CHANNELUP },	/* channel / program + */
	{ 0x21, KEY_CHANNELDOWN },	/* channel / program - */
	{ 0x22, KEY_CHANNEL },		/* source (old black remote) */
	{ 0x24, KEY_PREVIOUSSONG },	/* replay |< */
	{ 0x25, KEY_ENTER },		/* OK */
	{ 0x26, KEY_SLEEP },		/* minimize (old black remote) */
	{ 0x29, KEY_BLUE },		/* blue key */
	{ 0x2e, KEY_GREEN },		/* green button */
	{ 0x30, KEY_PAUSE },		/* pause */
	{ 0x32, KEY_REWIND },		/* backward << */
	{ 0x34, KEY_FASTFORWARD },	/* forward >> */
	{ 0x35, KEY_PLAY },
	{ 0x36, KEY_STOP },
	{ 0x37, KEY_RECORD },		/* recording */
	{ 0x38, KEY_YELLOW },		/* yellow key */
	{ 0x3b, KEY_SELECT },		/* top right button */
	{ 0x3c, KEY_ZOOM },		/* full */
	{ 0x3d, KEY_POWER },		/* system power (green button) */
};

struct ir_scancode_table ir_codes_hauppauge_new_table = {
	.scan = ir_codes_hauppauge_new,
	.size = ARRAY_SIZE(ir_codes_hauppauge_new),
};
EXPORT_SYMBOL_GPL(ir_codes_hauppauge_new_table);

static struct ir_scancode ir_codes_npgtech[] = {
	{ 0x1d, KEY_SWITCHVIDEOMODE },	/* switch inputs */
	{ 0x2a, KEY_FRONT },

	{ 0x3e, KEY_1 },
	{ 0x02, KEY_2 },
	{ 0x06, KEY_3 },
	{ 0x0a, KEY_4 },
	{ 0x0e, KEY_5 },
	{ 0x12, KEY_6 },
	{ 0x16, KEY_7 },
	{ 0x1a, KEY_8 },
	{ 0x1e, KEY_9 },
	{ 0x3a, KEY_0 },
	{ 0x22, KEY_NUMLOCK },		/* -/-- */
	{ 0x20, KEY_REFRESH },

	{ 0x03, KEY_BRIGHTNESSDOWN },
	{ 0x28, KEY_AUDIO },
	{ 0x3c, KEY_CHANNELUP },
	{ 0x3f, KEY_VOLUMEDOWN },
	{ 0x2e, KEY_MUTE },
	{ 0x3b, KEY_VOLUMEUP },
	{ 0x00, KEY_CHANNELDOWN },
	{ 0x07, KEY_BRIGHTNESSUP },
	{ 0x2c, KEY_TEXT },

	{ 0x37, KEY_RECORD },
	{ 0x17, KEY_PLAY },
	{ 0x13, KEY_PAUSE },
	{ 0x26, KEY_STOP },
	{ 0x18, KEY_FASTFORWARD },
	{ 0x14, KEY_REWIND },
	{ 0x33, KEY_ZOOM },
	{ 0x32, KEY_KEYBOARD },
	{ 0x30, KEY_GOTO },		/* Pointing arrow */
	{ 0x36, KEY_MACRO },		/* Maximize/Minimize (yellow) */
	{ 0x0b, KEY_RADIO },
	{ 0x10, KEY_POWER },

};

struct ir_scancode_table ir_codes_npgtech_table = {
	.scan = ir_codes_npgtech,
	.size = ARRAY_SIZE(ir_codes_npgtech),
};
EXPORT_SYMBOL_GPL(ir_codes_npgtech_table);

/* Norwood Micro (non-Pro) TV Tuner
   By Peter Naulls <peter@chocky.org>
   Key comments are the functions given in the manual */
static struct ir_scancode ir_codes_norwood[] = {
	/* Keys 0 to 9 */
	{ 0x20, KEY_0 },
	{ 0x21, KEY_1 },
	{ 0x22, KEY_2 },
	{ 0x23, KEY_3 },
	{ 0x24, KEY_4 },
	{ 0x25, KEY_5 },
	{ 0x26, KEY_6 },
	{ 0x27, KEY_7 },
	{ 0x28, KEY_8 },
	{ 0x29, KEY_9 },

	{ 0x78, KEY_TUNER },		/* Video Source        */
	{ 0x2c, KEY_EXIT },		/* Open/Close software */
	{ 0x2a, KEY_SELECT },		/* 2 Digit Select      */
	{ 0x69, KEY_AGAIN },		/* Recall              */

	{ 0x32, KEY_BRIGHTNESSUP },	/* Brightness increase */
	{ 0x33, KEY_BRIGHTNESSDOWN },	/* Brightness decrease */
	{ 0x6b, KEY_KPPLUS },		/* (not named >>>>>)   */
	{ 0x6c, KEY_KPMINUS },		/* (not named <<<<<)   */

	{ 0x2d, KEY_MUTE },		/* Mute                */
	{ 0x30, KEY_VOLUMEUP },		/* Volume up           */
	{ 0x31, KEY_VOLUMEDOWN },	/* Volume down         */
	{ 0x60, KEY_CHANNELUP },	/* Channel up          */
	{ 0x61, KEY_CHANNELDOWN },	/* Channel down        */

	{ 0x3f, KEY_RECORD },		/* Record              */
	{ 0x37, KEY_PLAY },		/* Play                */
	{ 0x36, KEY_PAUSE },		/* Pause               */
	{ 0x2b, KEY_STOP },		/* Stop                */
	{ 0x67, KEY_FASTFORWARD },	/* Foward              */
	{ 0x66, KEY_REWIND },		/* Rewind              */
	{ 0x3e, KEY_SEARCH },		/* Auto Scan           */
	{ 0x2e, KEY_CAMERA },		/* Capture Video       */
	{ 0x6d, KEY_MENU },		/* Show/Hide Control   */
	{ 0x2f, KEY_ZOOM },		/* Full Screen         */
	{ 0x34, KEY_RADIO },		/* FM                  */
	{ 0x65, KEY_POWER },		/* Computer power      */
};

struct ir_scancode_table ir_codes_norwood_table = {
	.scan = ir_codes_norwood,
	.size = ARRAY_SIZE(ir_codes_norwood),
};
EXPORT_SYMBOL_GPL(ir_codes_norwood_table);

/* From reading the following remotes:
 * Zenith Universal 7 / TV Mode 807 / VCR Mode 837
 * Hauppauge (from NOVA-CI-s box product)
 * This is a "middle of the road" approach, differences are noted
 */
static struct ir_scancode ir_codes_budget_ci_old[] = {
	{ 0x00, KEY_0 },
	{ 0x01, KEY_1 },
	{ 0x02, KEY_2 },
	{ 0x03, KEY_3 },
	{ 0x04, KEY_4 },
	{ 0x05, KEY_5 },
	{ 0x06, KEY_6 },
	{ 0x07, KEY_7 },
	{ 0x08, KEY_8 },
	{ 0x09, KEY_9 },
	{ 0x0a, KEY_ENTER },
	{ 0x0b, KEY_RED },
	{ 0x0c, KEY_POWER },		/* RADIO on Hauppauge */
	{ 0x0d, KEY_MUTE },
	{ 0x0f, KEY_A },		/* TV on Hauppauge */
	{ 0x10, KEY_VOLUMEUP },
	{ 0x11, KEY_VOLUMEDOWN },
	{ 0x14, KEY_B },
	{ 0x1c, KEY_UP },
	{ 0x1d, KEY_DOWN },
	{ 0x1e, KEY_OPTION },		/* RESERVED on Hauppauge */
	{ 0x1f, KEY_BREAK },
	{ 0x20, KEY_CHANNELUP },
	{ 0x21, KEY_CHANNELDOWN },
	{ 0x22, KEY_PREVIOUS },		/* Prev Ch on Zenith, SOURCE on Hauppauge */
	{ 0x24, KEY_RESTART },
	{ 0x25, KEY_OK },
	{ 0x26, KEY_CYCLEWINDOWS },	/* MINIMIZE on Hauppauge */
	{ 0x28, KEY_ENTER },		/* VCR mode on Zenith */
	{ 0x29, KEY_PAUSE },
	{ 0x2b, KEY_RIGHT },
	{ 0x2c, KEY_LEFT },
	{ 0x2e, KEY_MENU },		/* FULL SCREEN on Hauppauge */
	{ 0x30, KEY_SLOW },
	{ 0x31, KEY_PREVIOUS },		/* VCR mode on Zenith */
	{ 0x32, KEY_REWIND },
	{ 0x34, KEY_FASTFORWARD },
	{ 0x35, KEY_PLAY },
	{ 0x36, KEY_STOP },
	{ 0x37, KEY_RECORD },
	{ 0x38, KEY_TUNER },		/* TV/VCR on Zenith */
	{ 0x3a, KEY_C },
	{ 0x3c, KEY_EXIT },
	{ 0x3d, KEY_POWER2 },
	{ 0x3e, KEY_TUNER },
};

struct ir_scancode_table ir_codes_budget_ci_old_table = {
	.scan = ir_codes_budget_ci_old,
	.size = ARRAY_SIZE(ir_codes_budget_ci_old),
};
EXPORT_SYMBOL_GPL(ir_codes_budget_ci_old_table);

/*
 * Marc Fargas <telenieko@telenieko.com>
 * this is the remote control that comes with the asus p7131
 * which has a label saying is "Model PC-39"
 */
static struct ir_scancode ir_codes_asus_pc39[] = {
	/* Keys 0 to 9 */
	{ 0x15, KEY_0 },
	{ 0x29, KEY_1 },
	{ 0x2d, KEY_2 },
	{ 0x2b, KEY_3 },
	{ 0x09, KEY_4 },
	{ 0x0d, KEY_5 },
	{ 0x0b, KEY_6 },
	{ 0x31, KEY_7 },
	{ 0x35, KEY_8 },
	{ 0x33, KEY_9 },

	{ 0x3e, KEY_RADIO },		/* radio */
	{ 0x03, KEY_MENU },		/* dvd/menu */
	{ 0x2a, KEY_VOLUMEUP },
	{ 0x19, KEY_VOLUMEDOWN },
	{ 0x37, KEY_UP },
	{ 0x3b, KEY_DOWN },
	{ 0x27, KEY_LEFT },
	{ 0x2f, KEY_RIGHT },
	{ 0x25, KEY_VIDEO },		/* video */
	{ 0x39, KEY_AUDIO },		/* music */

	{ 0x21, KEY_TV },		/* tv */
	{ 0x1d, KEY_EXIT },		/* back */
	{ 0x0a, KEY_CHANNELUP },	/* channel / program + */
	{ 0x1b, KEY_CHANNELDOWN },	/* channel / program - */
	{ 0x1a, KEY_ENTER },		/* enter */

	{ 0x06, KEY_PAUSE },		/* play/pause */
	{ 0x1e, KEY_PREVIOUS },		/* rew */
	{ 0x26, KEY_NEXT },		/* forward */
	{ 0x0e, KEY_REWIND },		/* backward << */
	{ 0x3a, KEY_FASTFORWARD },	/* forward >> */
	{ 0x36, KEY_STOP },
	{ 0x2e, KEY_RECORD },		/* recording */
	{ 0x16, KEY_POWER },		/* the button that reads "close" */

	{ 0x11, KEY_ZOOM },		/* full screen */
	{ 0x13, KEY_MACRO },		/* recall */
	{ 0x23, KEY_HOME },		/* home */
	{ 0x05, KEY_PVR },		/* picture */
	{ 0x3d, KEY_MUTE },		/* mute */
	{ 0x01, KEY_DVD },		/* dvd */
};

struct ir_scancode_table ir_codes_asus_pc39_table = {
	.scan = ir_codes_asus_pc39,
	.size = ARRAY_SIZE(ir_codes_asus_pc39),
};
EXPORT_SYMBOL_GPL(ir_codes_asus_pc39_table);


/* Encore ENLTV-FM  - black plastic, white front cover with white glowing buttons
    Juan Pablo Sormani <sorman@gmail.com> */
static struct ir_scancode ir_codes_encore_enltv[] = {

	/* Power button does nothing, neither in Windows app,
	 although it sends data (used for BIOS wakeup?) */
	{ 0x0d, KEY_MUTE },

	{ 0x1e, KEY_TV },
	{ 0x00, KEY_VIDEO },
	{ 0x01, KEY_AUDIO },		/* music */
	{ 0x02, KEY_MHP },		/* picture */

	{ 0x1f, KEY_1 },
	{ 0x03, KEY_2 },
	{ 0x04, KEY_3 },
	{ 0x05, KEY_4 },
	{ 0x1c, KEY_5 },
	{ 0x06, KEY_6 },
	{ 0x07, KEY_7 },
	{ 0x08, KEY_8 },
	{ 0x1d, KEY_9 },
	{ 0x0a, KEY_0 },

	{ 0x09, KEY_LIST },		/* -/-- */
	{ 0x0b, KEY_LAST },		/* recall */

	{ 0x14, KEY_HOME },		/* win start menu */
	{ 0x15, KEY_EXIT },		/* exit */
	{ 0x16, KEY_CHANNELUP },	/* UP */
	{ 0x12, KEY_CHANNELDOWN },	/* DOWN */
	{ 0x0c, KEY_VOLUMEUP },		/* RIGHT */
	{ 0x17, KEY_VOLUMEDOWN },	/* LEFT */

	{ 0x18, KEY_ENTER },		/* OK */

	{ 0x0e, KEY_ESC },
	{ 0x13, KEY_CYCLEWINDOWS },	/* desktop */
	{ 0x11, KEY_TAB },
	{ 0x19, KEY_SWITCHVIDEOMODE },	/* switch */

	{ 0x1a, KEY_MENU },
	{ 0x1b, KEY_ZOOM },		/* fullscreen */
	{ 0x44, KEY_TIME },		/* time shift */
	{ 0x40, KEY_MODE },		/* source */

	{ 0x5a, KEY_RECORD },
	{ 0x42, KEY_PLAY },		/* play/pause */
	{ 0x45, KEY_STOP },
	{ 0x43, KEY_CAMERA },		/* camera icon */

	{ 0x48, KEY_REWIND },
	{ 0x4a, KEY_FASTFORWARD },
	{ 0x49, KEY_PREVIOUS },
	{ 0x4b, KEY_NEXT },

	{ 0x4c, KEY_FAVORITES },	/* tv wall */
	{ 0x4d, KEY_SOUND },		/* DVD sound */
	{ 0x4e, KEY_LANGUAGE },		/* DVD lang */
	{ 0x4f, KEY_TEXT },		/* DVD text */

	{ 0x50, KEY_SLEEP },		/* shutdown */
	{ 0x51, KEY_MODE },		/* stereo > main */
	{ 0x52, KEY_SELECT },		/* stereo > sap */
	{ 0x53, KEY_PROG1 },		/* teletext */


	{ 0x59, KEY_RED },		/* AP1 */
	{ 0x41, KEY_GREEN },		/* AP2 */
	{ 0x47, KEY_YELLOW },		/* AP3 */
	{ 0x57, KEY_BLUE },		/* AP4 */
};

struct ir_scancode_table ir_codes_encore_enltv_table = {
	.scan = ir_codes_encore_enltv,
	.size = ARRAY_SIZE(ir_codes_encore_enltv),
};
EXPORT_SYMBOL_GPL(ir_codes_encore_enltv_table);

/* Encore ENLTV2-FM  - silver plastic - "Wand Media" written at the botton
    Mauro Carvalho Chehab <mchehab@infradead.org> */
static struct ir_scancode ir_codes_encore_enltv2[] = {
	{ 0x4c, KEY_POWER2 },
	{ 0x4a, KEY_TUNER },
	{ 0x40, KEY_1 },
	{ 0x60, KEY_2 },
	{ 0x50, KEY_3 },
	{ 0x70, KEY_4 },
	{ 0x48, KEY_5 },
	{ 0x68, KEY_6 },
	{ 0x58, KEY_7 },
	{ 0x78, KEY_8 },
	{ 0x44, KEY_9 },
	{ 0x54, KEY_0 },

	{ 0x64, KEY_LAST },		/* +100 */
	{ 0x4e, KEY_AGAIN },		/* Recall */

	{ 0x6c, KEY_SWITCHVIDEOMODE },	/* Video Source */
	{ 0x5e, KEY_MENU },
	{ 0x56, KEY_SCREEN },
	{ 0x7a, KEY_SETUP },

	{ 0x46, KEY_MUTE },
	{ 0x5c, KEY_MODE },		/* Stereo */
	{ 0x74, KEY_INFO },
	{ 0x7c, KEY_CLEAR },

	{ 0x55, KEY_UP },
	{ 0x49, KEY_DOWN },
	{ 0x7e, KEY_LEFT },
	{ 0x59, KEY_RIGHT },
	{ 0x6a, KEY_ENTER },

	{ 0x42, KEY_VOLUMEUP },
	{ 0x62, KEY_VOLUMEDOWN },
	{ 0x52, KEY_CHANNELUP },
	{ 0x72, KEY_CHANNELDOWN },

	{ 0x41, KEY_RECORD },
	{ 0x51, KEY_CAMERA },		/* Snapshot */
	{ 0x75, KEY_TIME },		/* Timeshift */
	{ 0x71, KEY_TV2 },		/* PIP */

	{ 0x45, KEY_REWIND },
	{ 0x6f, KEY_PAUSE },
	{ 0x7d, KEY_FORWARD },
	{ 0x79, KEY_STOP },
};

struct ir_scancode_table ir_codes_encore_enltv2_table = {
	.scan = ir_codes_encore_enltv2,
	.size = ARRAY_SIZE(ir_codes_encore_enltv2),
};
EXPORT_SYMBOL_GPL(ir_codes_encore_enltv2_table);

/* for the Technotrend 1500 bundled remotes (grey and black): */
static struct ir_scancode ir_codes_tt_1500[] = {
	{ 0x01, KEY_POWER },
	{ 0x02, KEY_SHUFFLE },		/* ? double-arrow key */
	{ 0x03, KEY_1 },
	{ 0x04, KEY_2 },
	{ 0x05, KEY_3 },
	{ 0x06, KEY_4 },
	{ 0x07, KEY_5 },
	{ 0x08, KEY_6 },
	{ 0x09, KEY_7 },
	{ 0x0a, KEY_8 },
	{ 0x0b, KEY_9 },
	{ 0x0c, KEY_0 },
	{ 0x0d, KEY_UP },
	{ 0x0e, KEY_LEFT },
	{ 0x0f, KEY_OK },
	{ 0x10, KEY_RIGHT },
	{ 0x11, KEY_DOWN },
	{ 0x12, KEY_INFO },
	{ 0x13, KEY_EXIT },
	{ 0x14, KEY_RED },
	{ 0x15, KEY_GREEN },
	{ 0x16, KEY_YELLOW },
	{ 0x17, KEY_BLUE },
	{ 0x18, KEY_MUTE },
	{ 0x19, KEY_TEXT },
	{ 0x1a, KEY_MODE },		/* ? TV/Radio */
	{ 0x21, KEY_OPTION },
	{ 0x22, KEY_EPG },
	{ 0x23, KEY_CHANNELUP },
	{ 0x24, KEY_CHANNELDOWN },
	{ 0x25, KEY_VOLUMEUP },
	{ 0x26, KEY_VOLUMEDOWN },
	{ 0x27, KEY_SETUP },
	{ 0x3a, KEY_RECORD },		/* these keys are only in the black remote */
	{ 0x3b, KEY_PLAY },
	{ 0x3c, KEY_STOP },
	{ 0x3d, KEY_REWIND },
	{ 0x3e, KEY_PAUSE },
	{ 0x3f, KEY_FORWARD },
};

struct ir_scancode_table ir_codes_tt_1500_table = {
	.scan = ir_codes_tt_1500,
	.size = ARRAY_SIZE(ir_codes_tt_1500),
};
EXPORT_SYMBOL_GPL(ir_codes_tt_1500_table);

/* DViCO FUSION HDTV MCE remote */
static struct ir_scancode ir_codes_fusionhdtv_mce[] = {

	{ 0x0b, KEY_1 },
	{ 0x17, KEY_2 },
	{ 0x1b, KEY_3 },
	{ 0x07, KEY_4 },
	{ 0x50, KEY_5 },
	{ 0x54, KEY_6 },
	{ 0x48, KEY_7 },
	{ 0x4c, KEY_8 },
	{ 0x58, KEY_9 },
	{ 0x03, KEY_0 },

	{ 0x5e, KEY_OK },
	{ 0x51, KEY_UP },
	{ 0x53, KEY_DOWN },
	{ 0x5b, KEY_LEFT },
	{ 0x5f, KEY_RIGHT },

	{ 0x02, KEY_TV },		/* Labeled DTV on remote */
	{ 0x0e, KEY_MP3 },
	{ 0x1a, KEY_DVD },
	{ 0x1e, KEY_FAVORITES },	/* Labeled CPF on remote */
	{ 0x16, KEY_SETUP },
	{ 0x46, KEY_POWER2 },		/* TV On/Off button on remote */
	{ 0x0a, KEY_EPG },		/* Labeled Guide on remote */

	{ 0x49, KEY_BACK },
	{ 0x59, KEY_INFO },		/* Labeled MORE on remote */
	{ 0x4d, KEY_MENU },		/* Labeled DVDMENU on remote */
	{ 0x55, KEY_CYCLEWINDOWS },	/* Labeled ALT-TAB on remote */

	{ 0x0f, KEY_PREVIOUSSONG },	/* Labeled |<< REPLAY on remote */
	{ 0x12, KEY_NEXTSONG },		/* Labeled >>| SKIP on remote */
	{ 0x42, KEY_ENTER },		/* Labeled START with a green
					   MS windows logo on remote */

	{ 0x15, KEY_VOLUMEUP },
	{ 0x05, KEY_VOLUMEDOWN },
	{ 0x11, KEY_CHANNELUP },
	{ 0x09, KEY_CHANNELDOWN },

	{ 0x52, KEY_CAMERA },
	{ 0x5a, KEY_TUNER },
	{ 0x19, KEY_OPEN },

	{ 0x13, KEY_MODE },		/* 4:3 16:9 select */
	{ 0x1f, KEY_ZOOM },

	{ 0x43, KEY_REWIND },
	{ 0x47, KEY_PLAYPAUSE },
	{ 0x4f, KEY_FASTFORWARD },
	{ 0x57, KEY_MUTE },
	{ 0x0d, KEY_STOP },
	{ 0x01, KEY_RECORD },
	{ 0x4e, KEY_POWER },
};

struct ir_scancode_table ir_codes_fusionhdtv_mce_table = {
	.scan = ir_codes_fusionhdtv_mce,
	.size = ARRAY_SIZE(ir_codes_fusionhdtv_mce),
};
EXPORT_SYMBOL_GPL(ir_codes_fusionhdtv_mce_table);

/* Pinnacle PCTV HD 800i mini remote */
static struct ir_scancode ir_codes_pinnacle_pctv_hd[] = {

	{ 0x0f, KEY_1 },
	{ 0x15, KEY_2 },
	{ 0x10, KEY_3 },
	{ 0x18, KEY_4 },
	{ 0x1b, KEY_5 },
	{ 0x1e, KEY_6 },
	{ 0x11, KEY_7 },
	{ 0x21, KEY_8 },
	{ 0x12, KEY_9 },
	{ 0x27, KEY_0 },

	{ 0x24, KEY_ZOOM },
	{ 0x2a, KEY_SUBTITLE },

	{ 0x00, KEY_MUTE },
	{ 0x01, KEY_ENTER },	/* Pinnacle Logo */
	{ 0x39, KEY_POWER },

	{ 0x03, KEY_VOLUMEUP },
	{ 0x09, KEY_VOLUMEDOWN },
	{ 0x06, KEY_CHANNELUP },
	{ 0x0c, KEY_CHANNELDOWN },

	{ 0x2d, KEY_REWIND },
	{ 0x30, KEY_PLAYPAUSE },
	{ 0x33, KEY_FASTFORWARD },
	{ 0x3c, KEY_STOP },
	{ 0x36, KEY_RECORD },
	{ 0x3f, KEY_EPG },	/* Labeled "?" */
};

struct ir_scancode_table ir_codes_pinnacle_pctv_hd_table = {
	.scan = ir_codes_pinnacle_pctv_hd,
	.size = ARRAY_SIZE(ir_codes_pinnacle_pctv_hd),
};
EXPORT_SYMBOL_GPL(ir_codes_pinnacle_pctv_hd_table);

/*
 * Igor Kuznetsov <igk72@ya.ru>
 * Andrey J. Melnikov <temnota@kmv.ru>
 *
 * Keytable is used by BeholdTV 60x series, M6 series at
 * least, and probably other cards too.
 * The "ascii-art picture" below (in comments, first row
 * is the keycode in hex, and subsequent row(s) shows
 * the button labels (several variants when appropriate)
 * helps to descide which keycodes to assign to the buttons.
 */
static struct ir_scancode ir_codes_behold[] = {

	/*  0x1c            0x12  *
	 *  TV/FM          POWER  *
	 *                        */
	{ 0x1c, KEY_TUNER },	/* XXX KEY_TV / KEY_RADIO */
	{ 0x12, KEY_POWER },

	/*  0x01    0x02    0x03  *
	 *   1       2       3    *
	 *                        *
	 *  0x04    0x05    0x06  *
	 *   4       5       6    *
	 *                        *
	 *  0x07    0x08    0x09  *
	 *   7       8       9    *
	 *                        */
	{ 0x01, KEY_1 },
	{ 0x02, KEY_2 },
	{ 0x03, KEY_3 },
	{ 0x04, KEY_4 },
	{ 0x05, KEY_5 },
	{ 0x06, KEY_6 },
	{ 0x07, KEY_7 },
	{ 0x08, KEY_8 },
	{ 0x09, KEY_9 },

	/*  0x0a    0x00    0x17  *
	 * RECALL    0      MODE  *
	 *                        */
	{ 0x0a, KEY_AGAIN },
	{ 0x00, KEY_0 },
	{ 0x17, KEY_MODE },

	/*  0x14          0x10    *
	 * ASPECT      FULLSCREEN *
	 *                        */
	{ 0x14, KEY_SCREEN },
	{ 0x10, KEY_ZOOM },

	/*          0x0b          *
	 *           Up           *
	 *                        *
	 *  0x18    0x16    0x0c  *
	 *  Left     Ok     Right *
	 *                        *
	 *         0x015          *
	 *         Down           *
	 *                        */
	{ 0x0b, KEY_CHANNELUP },
	{ 0x18, KEY_VOLUMEDOWN },
	{ 0x16, KEY_OK },		/* XXX KEY_ENTER */
	{ 0x0c, KEY_VOLUMEUP },
	{ 0x15, KEY_CHANNELDOWN },

	/*  0x11            0x0d  *
	 *  MUTE            INFO  *
	 *                        */
	{ 0x11, KEY_MUTE },
	{ 0x0d, KEY_INFO },

	/*  0x0f    0x1b    0x1a  *
	 * RECORD PLAY/PAUSE STOP *
	 *                        *
	 *  0x0e    0x1f    0x1e  *
	 *TELETEXT  AUDIO  SOURCE *
	 *           RED   YELLOW *
	 *                        */
	{ 0x0f, KEY_RECORD },
	{ 0x1b, KEY_PLAYPAUSE },
	{ 0x1a, KEY_STOP },
	{ 0x0e, KEY_TEXT },
	{ 0x1f, KEY_RED },	/*XXX KEY_AUDIO	*/
	{ 0x1e, KEY_YELLOW },	/*XXX KEY_SOURCE	*/

	/*  0x1d   0x13     0x19  *
	 * SLEEP  PREVIEW   DVB   *
	 *         GREEN    BLUE  *
	 *                        */
	{ 0x1d, KEY_SLEEP },
	{ 0x13, KEY_GREEN },
	{ 0x19, KEY_BLUE },	/* XXX KEY_SAT	*/

	/*  0x58           0x5c   *
	 * FREEZE        SNAPSHOT *
	 *                        */
	{ 0x58, KEY_SLOW },
	{ 0x5c, KEY_CAMERA },

};

struct ir_scancode_table ir_codes_behold_table = {
	.scan = ir_codes_behold,
	.size = ARRAY_SIZE(ir_codes_behold),
};
EXPORT_SYMBOL_GPL(ir_codes_behold_table);

/* Beholder Intl. Ltd. 2008
 * Dmitry Belimov d.belimov@google.com
 * Keytable is used by BeholdTV Columbus
 * The "ascii-art picture" below (in comments, first row
 * is the keycode in hex, and subsequent row(s) shows
 * the button labels (several variants when appropriate)
 * helps to descide which keycodes to assign to the buttons.
 */
static struct ir_scancode ir_codes_behold_columbus[] = {

	/*  0x13   0x11   0x1C   0x12  *
	 *  Mute  Source  TV/FM  Power *
	 *                             */

	{ 0x13, KEY_MUTE },
	{ 0x11, KEY_PROPS },
	{ 0x1C, KEY_TUNER },	/* KEY_TV/KEY_RADIO	*/
	{ 0x12, KEY_POWER },

	/*  0x01    0x02    0x03  0x0D    *
	 *   1       2       3   Stereo   *
	 *                        	  *
	 *  0x04    0x05    0x06  0x19    *
	 *   4       5       6   Snapshot *
	 *                        	  *
	 *  0x07    0x08    0x09  0x10    *
	 *   7       8       9    Zoom 	  *
	 *                                */
	{ 0x01, KEY_1 },
	{ 0x02, KEY_2 },
	{ 0x03, KEY_3 },
	{ 0x0D, KEY_SETUP },	  /* Setup key */
	{ 0x04, KEY_4 },
	{ 0x05, KEY_5 },
	{ 0x06, KEY_6 },
	{ 0x19, KEY_CAMERA },	/* Snapshot key */
	{ 0x07, KEY_7 },
	{ 0x08, KEY_8 },
	{ 0x09, KEY_9 },
	{ 0x10, KEY_ZOOM },

	/*  0x0A    0x00    0x0B       0x0C   *
	 * RECALL    0    ChannelUp  VolumeUp *
	 *                                    */
	{ 0x0A, KEY_AGAIN },
	{ 0x00, KEY_0 },
	{ 0x0B, KEY_CHANNELUP },
	{ 0x0C, KEY_VOLUMEUP },

	/*   0x1B      0x1D      0x15        0x18     *
	 * Timeshift  Record  ChannelDown  VolumeDown *
	 *                                            */

	{ 0x1B, KEY_TIME },
	{ 0x1D, KEY_RECORD },
	{ 0x15, KEY_CHANNELDOWN },
	{ 0x18, KEY_VOLUMEDOWN },

	/*   0x0E   0x1E     0x0F     0x1A  *
	 *   Stop   Pause  Previouse  Next  *
	 *                                  */

	{ 0x0E, KEY_STOP },
	{ 0x1E, KEY_PAUSE },
	{ 0x0F, KEY_PREVIOUS },
	{ 0x1A, KEY_NEXT },

};

struct ir_scancode_table ir_codes_behold_columbus_table = {
	.scan = ir_codes_behold_columbus,
	.size = ARRAY_SIZE(ir_codes_behold_columbus),
};
EXPORT_SYMBOL_GPL(ir_codes_behold_columbus_table);

/*
 * Remote control for the Genius TVGO A11MCE
 * Adrian Pardini <pardo.bsso@gmail.com>
 */
static struct ir_scancode ir_codes_genius_tvgo_a11mce[] = {
	/* Keys 0 to 9 */
	{ 0x48, KEY_0 },
	{ 0x09, KEY_1 },
	{ 0x1d, KEY_2 },
	{ 0x1f, KEY_3 },
	{ 0x19, KEY_4 },
	{ 0x1b, KEY_5 },
	{ 0x11, KEY_6 },
	{ 0x17, KEY_7 },
	{ 0x12, KEY_8 },
	{ 0x16, KEY_9 },

	{ 0x54, KEY_RECORD },		/* recording */
	{ 0x06, KEY_MUTE },		/* mute */
	{ 0x10, KEY_POWER },
	{ 0x40, KEY_LAST },		/* recall */
	{ 0x4c, KEY_CHANNELUP },	/* channel / program + */
	{ 0x00, KEY_CHANNELDOWN },	/* channel / program - */
	{ 0x0d, KEY_VOLUMEUP },
	{ 0x15, KEY_VOLUMEDOWN },
	{ 0x4d, KEY_OK },		/* also labeled as Pause */
	{ 0x1c, KEY_ZOOM },		/* full screen and Stop*/
	{ 0x02, KEY_MODE },		/* AV Source or Rewind*/
	{ 0x04, KEY_LIST },		/* -/-- */
	/* small arrows above numbers */
	{ 0x1a, KEY_NEXT },		/* also Fast Forward */
	{ 0x0e, KEY_PREVIOUS },		/* also Rewind */
	/* these are in a rather non standard layout and have
	an alternate name written */
	{ 0x1e, KEY_UP },		/* Video Setting */
	{ 0x0a, KEY_DOWN },		/* Video Default */
	{ 0x05, KEY_CAMERA },		/* Snapshot */
	{ 0x0c, KEY_RIGHT },		/* Hide Panel */
	/* Four buttons without label */
	{ 0x49, KEY_RED },
	{ 0x0b, KEY_GREEN },
	{ 0x13, KEY_YELLOW },
	{ 0x50, KEY_BLUE },
};

struct ir_scancode_table ir_codes_genius_tvgo_a11mce_table = {
	.scan = ir_codes_genius_tvgo_a11mce,
	.size = ARRAY_SIZE(ir_codes_genius_tvgo_a11mce),
};
EXPORT_SYMBOL_GPL(ir_codes_genius_tvgo_a11mce_table);

/*
 * Remote control for Powercolor Real Angel 330
 * Daniel Fraga <fragabr@gmail.com>
 */
static struct ir_scancode ir_codes_powercolor_real_angel[] = {
	{ 0x38, KEY_SWITCHVIDEOMODE },	/* switch inputs */
	{ 0x0c, KEY_MEDIA },		/* Turn ON/OFF App */
	{ 0x00, KEY_0 },
	{ 0x01, KEY_1 },
	{ 0x02, KEY_2 },
	{ 0x03, KEY_3 },
	{ 0x04, KEY_4 },
	{ 0x05, KEY_5 },
	{ 0x06, KEY_6 },
	{ 0x07, KEY_7 },
	{ 0x08, KEY_8 },
	{ 0x09, KEY_9 },
	{ 0x0a, KEY_DIGITS },		/* single, double, tripple digit */
	{ 0x29, KEY_PREVIOUS },		/* previous channel */
	{ 0x12, KEY_BRIGHTNESSUP },
	{ 0x13, KEY_BRIGHTNESSDOWN },
	{ 0x2b, KEY_MODE },		/* stereo/mono */
	{ 0x2c, KEY_TEXT },		/* teletext */
	{ 0x20, KEY_CHANNELUP },	/* channel up */
	{ 0x21, KEY_CHANNELDOWN },	/* channel down */
	{ 0x10, KEY_VOLUMEUP },		/* volume up */
	{ 0x11, KEY_VOLUMEDOWN },	/* volume down */
	{ 0x0d, KEY_MUTE },
	{ 0x1f, KEY_RECORD },
	{ 0x17, KEY_PLAY },
	{ 0x16, KEY_PAUSE },
	{ 0x0b, KEY_STOP },
	{ 0x27, KEY_FASTFORWARD },
	{ 0x26, KEY_REWIND },
	{ 0x1e, KEY_SEARCH },		/* autoscan */
	{ 0x0e, KEY_CAMERA },		/* snapshot */
	{ 0x2d, KEY_SETUP },
	{ 0x0f, KEY_SCREEN },		/* full screen */
	{ 0x14, KEY_RADIO },		/* FM radio */
	{ 0x25, KEY_POWER },		/* power */
};

struct ir_scancode_table ir_codes_powercolor_real_angel_table = {
	.scan = ir_codes_powercolor_real_angel,
	.size = ARRAY_SIZE(ir_codes_powercolor_real_angel),
};
EXPORT_SYMBOL_GPL(ir_codes_powercolor_real_angel_table);

/* Kworld Plus TV Analog Lite PCI IR
   Mauro Carvalho Chehab <mchehab@infradead.org>
 */
static struct ir_scancode ir_codes_kworld_plus_tv_analog[] = {
	{ 0x0c, KEY_PROG1 },		/* Kworld key */
	{ 0x16, KEY_CLOSECD },		/* -> ) */
	{ 0x1d, KEY_POWER2 },

	{ 0x00, KEY_1 },
	{ 0x01, KEY_2 },
	{ 0x02, KEY_3 },		/* Two keys have the same code: 3 and left */
	{ 0x03, KEY_4 },		/* Two keys have the same code: 3 and right */
	{ 0x04, KEY_5 },
	{ 0x05, KEY_6 },
	{ 0x06, KEY_7 },
	{ 0x07, KEY_8 },
	{ 0x08, KEY_9 },
	{ 0x0a, KEY_0 },

	{ 0x09, KEY_AGAIN },
	{ 0x14, KEY_MUTE },

	{ 0x20, KEY_UP },
	{ 0x21, KEY_DOWN },
	{ 0x0b, KEY_ENTER },

	{ 0x10, KEY_CHANNELUP },
	{ 0x11, KEY_CHANNELDOWN },

	/* Couldn't map key left/key right since those
	   conflict with '3' and '4' scancodes
	   I dunno what the original driver does
	 */

	{ 0x13, KEY_VOLUMEUP },
	{ 0x12, KEY_VOLUMEDOWN },

	/* The lower part of the IR
	   There are several duplicated keycodes there.
	   Most of them conflict with digits.
	   Add mappings just to the unused scancodes.
	   Somehow, the original driver has a way to know,
	   but this doesn't seem to be on some GPIO.
	   Also, it is not related to the time between keyup
	   and keydown.
	 */
	{ 0x19, KEY_TIME},		/* Timeshift */
	{ 0x1a, KEY_STOP},
	{ 0x1b, KEY_RECORD},

	{ 0x22, KEY_TEXT},

	{ 0x15, KEY_AUDIO},		/* ((*)) */
	{ 0x0f, KEY_ZOOM},
	{ 0x1c, KEY_CAMERA},		/* snapshot */

	{ 0x18, KEY_RED},		/* B */
	{ 0x23, KEY_GREEN},		/* C */
};
struct ir_scancode_table ir_codes_kworld_plus_tv_analog_table = {
	.scan = ir_codes_kworld_plus_tv_analog,
	.size = ARRAY_SIZE(ir_codes_kworld_plus_tv_analog),
};
EXPORT_SYMBOL_GPL(ir_codes_kworld_plus_tv_analog_table);

/* Kaiomy TVnPC U2
   Mauro Carvalho Chehab <mchehab@infradead.org>
 */
static struct ir_scancode ir_codes_kaiomy[] = {
	{ 0x43, KEY_POWER2},
	{ 0x01, KEY_LIST},
	{ 0x0b, KEY_ZOOM},
	{ 0x03, KEY_POWER},

	{ 0x04, KEY_1},
	{ 0x08, KEY_2},
	{ 0x02, KEY_3},

	{ 0x0f, KEY_4},
	{ 0x05, KEY_5},
	{ 0x06, KEY_6},

	{ 0x0c, KEY_7},
	{ 0x0d, KEY_8},
	{ 0x0a, KEY_9},

	{ 0x11, KEY_0},

	{ 0x09, KEY_CHANNELUP},
	{ 0x07, KEY_CHANNELDOWN},

	{ 0x0e, KEY_VOLUMEUP},
	{ 0x13, KEY_VOLUMEDOWN},

	{ 0x10, KEY_HOME},
	{ 0x12, KEY_ENTER},

	{ 0x14, KEY_RECORD},
	{ 0x15, KEY_STOP},
	{ 0x16, KEY_PLAY},
	{ 0x17, KEY_MUTE},

	{ 0x18, KEY_UP},
	{ 0x19, KEY_DOWN},
	{ 0x1a, KEY_LEFT},
	{ 0x1b, KEY_RIGHT},

	{ 0x1c, KEY_RED},
	{ 0x1d, KEY_GREEN},
	{ 0x1e, KEY_YELLOW},
	{ 0x1f, KEY_BLUE},
};
struct ir_scancode_table ir_codes_kaiomy_table = {
	.scan = ir_codes_kaiomy,
	.size = ARRAY_SIZE(ir_codes_kaiomy),
};
EXPORT_SYMBOL_GPL(ir_codes_kaiomy_table);

static struct ir_scancode ir_codes_avermedia_a16d[] = {
	{ 0x20, KEY_LIST},
	{ 0x00, KEY_POWER},
	{ 0x28, KEY_1},
	{ 0x18, KEY_2},
	{ 0x38, KEY_3},
	{ 0x24, KEY_4},
	{ 0x14, KEY_5},
	{ 0x34, KEY_6},
	{ 0x2c, KEY_7},
	{ 0x1c, KEY_8},
	{ 0x3c, KEY_9},
	{ 0x12, KEY_SUBTITLE},
	{ 0x22, KEY_0},
	{ 0x32, KEY_REWIND},
	{ 0x3a, KEY_SHUFFLE},
	{ 0x02, KEY_PRINT},
	{ 0x11, KEY_CHANNELDOWN},
	{ 0x31, KEY_CHANNELUP},
	{ 0x0c, KEY_ZOOM},
	{ 0x1e, KEY_VOLUMEDOWN},
	{ 0x3e, KEY_VOLUMEUP},
	{ 0x0a, KEY_MUTE},
	{ 0x04, KEY_AUDIO},
	{ 0x26, KEY_RECORD},
	{ 0x06, KEY_PLAY},
	{ 0x36, KEY_STOP},
	{ 0x16, KEY_PAUSE},
	{ 0x2e, KEY_REWIND},
	{ 0x0e, KEY_FASTFORWARD},
	{ 0x30, KEY_TEXT},
	{ 0x21, KEY_GREEN},
	{ 0x01, KEY_BLUE},
	{ 0x08, KEY_EPG},
	{ 0x2a, KEY_MENU},
};
struct ir_scancode_table ir_codes_avermedia_a16d_table = {
	.scan = ir_codes_avermedia_a16d,
	.size = ARRAY_SIZE(ir_codes_avermedia_a16d),
};
EXPORT_SYMBOL_GPL(ir_codes_avermedia_a16d_table);

/* Encore ENLTV-FM v5.3
   Mauro Carvalho Chehab <mchehab@infradead.org>
 */
static struct ir_scancode ir_codes_encore_enltv_fm53[] = {
	{ 0x10, KEY_POWER2},
	{ 0x06, KEY_MUTE},

	{ 0x09, KEY_1},
	{ 0x1d, KEY_2},
	{ 0x1f, KEY_3},
	{ 0x19, KEY_4},
	{ 0x1b, KEY_5},
	{ 0x11, KEY_6},
	{ 0x17, KEY_7},
	{ 0x12, KEY_8},
	{ 0x16, KEY_9},
	{ 0x48, KEY_0},

	{ 0x04, KEY_LIST},		/* -/-- */
	{ 0x40, KEY_LAST},		/* recall */

	{ 0x02, KEY_MODE},		/* TV/AV */
	{ 0x05, KEY_CAMERA},		/* SNAPSHOT */

	{ 0x4c, KEY_CHANNELUP},		/* UP */
	{ 0x00, KEY_CHANNELDOWN},	/* DOWN */
	{ 0x0d, KEY_VOLUMEUP},		/* RIGHT */
	{ 0x15, KEY_VOLUMEDOWN},	/* LEFT */
	{ 0x49, KEY_ENTER},		/* OK */

	{ 0x54, KEY_RECORD},
	{ 0x4d, KEY_PLAY},		/* pause */

	{ 0x1e, KEY_MENU},		/* video setting */
	{ 0x0e, KEY_RIGHT},		/* <- */
	{ 0x1a, KEY_LEFT},		/* -> */

	{ 0x0a, KEY_CLEAR},		/* video default */
	{ 0x0c, KEY_ZOOM},		/* hide pannel */
	{ 0x47, KEY_SLEEP},		/* shutdown */
};
struct ir_scancode_table ir_codes_encore_enltv_fm53_table = {
	.scan = ir_codes_encore_enltv_fm53,
	.size = ARRAY_SIZE(ir_codes_encore_enltv_fm53),
};
EXPORT_SYMBOL_GPL(ir_codes_encore_enltv_fm53_table);

/* Zogis Real Audio 220 - 32 keys IR */
static struct ir_scancode ir_codes_real_audio_220_32_keys[] = {
	{ 0x1c, KEY_RADIO},
	{ 0x12, KEY_POWER2},

	{ 0x01, KEY_1},
	{ 0x02, KEY_2},
	{ 0x03, KEY_3},
	{ 0x04, KEY_4},
	{ 0x05, KEY_5},
	{ 0x06, KEY_6},
	{ 0x07, KEY_7},
	{ 0x08, KEY_8},
	{ 0x09, KEY_9},
	{ 0x00, KEY_0},

	{ 0x0c, KEY_VOLUMEUP},
	{ 0x18, KEY_VOLUMEDOWN},
	{ 0x0b, KEY_CHANNELUP},
	{ 0x15, KEY_CHANNELDOWN},
	{ 0x16, KEY_ENTER},

	{ 0x11, KEY_LIST},		/* Source */
	{ 0x0d, KEY_AUDIO},		/* stereo */

	{ 0x0f, KEY_PREVIOUS},		/* Prev */
	{ 0x1b, KEY_TIME},		/* Timeshift */
	{ 0x1a, KEY_NEXT},		/* Next */

	{ 0x0e, KEY_STOP},
	{ 0x1f, KEY_PLAY},
	{ 0x1e, KEY_PLAYPAUSE},		/* Pause */

	{ 0x1d, KEY_RECORD},
	{ 0x13, KEY_MUTE},
	{ 0x19, KEY_CAMERA},		/* Snapshot */

};
struct ir_scancode_table ir_codes_real_audio_220_32_keys_table = {
	.scan = ir_codes_real_audio_220_32_keys,
	.size = ARRAY_SIZE(ir_codes_real_audio_220_32_keys),
};
EXPORT_SYMBOL_GPL(ir_codes_real_audio_220_32_keys_table);

/* ATI TV Wonder HD 600 USB
   Devin Heitmueller <devin.heitmueller@gmail.com>
 */
static struct ir_scancode ir_codes_ati_tv_wonder_hd_600[] = {
	{ 0x00, KEY_RECORD},		/* Row 1 */
	{ 0x01, KEY_PLAYPAUSE},
	{ 0x02, KEY_STOP},
	{ 0x03, KEY_POWER},
	{ 0x04, KEY_PREVIOUS},	/* Row 2 */
	{ 0x05, KEY_REWIND},
	{ 0x06, KEY_FORWARD},
	{ 0x07, KEY_NEXT},
	{ 0x08, KEY_EPG},		/* Row 3 */
	{ 0x09, KEY_HOME},
	{ 0x0a, KEY_MENU},
	{ 0x0b, KEY_CHANNELUP},
	{ 0x0c, KEY_BACK},		/* Row 4 */
	{ 0x0d, KEY_UP},
	{ 0x0e, KEY_INFO},
	{ 0x0f, KEY_CHANNELDOWN},
	{ 0x10, KEY_LEFT},		/* Row 5 */
	{ 0x11, KEY_SELECT},
	{ 0x12, KEY_RIGHT},
	{ 0x13, KEY_VOLUMEUP},
	{ 0x14, KEY_LAST},		/* Row 6 */
	{ 0x15, KEY_DOWN},
	{ 0x16, KEY_MUTE},
	{ 0x17, KEY_VOLUMEDOWN},
};
struct ir_scancode_table ir_codes_ati_tv_wonder_hd_600_table = {
	.scan = ir_codes_ati_tv_wonder_hd_600,
	.size = ARRAY_SIZE(ir_codes_ati_tv_wonder_hd_600),
};
EXPORT_SYMBOL_GPL(ir_codes_ati_tv_wonder_hd_600_table);

/* DVBWorld remotes
   Igor M. Liplianin <liplianin@me.by>
 */
static struct ir_scancode ir_codes_dm1105_nec[] = {
	{ 0x0a, KEY_POWER2},		/* power */
	{ 0x0c, KEY_MUTE},		/* mute */
	{ 0x11, KEY_1},
	{ 0x12, KEY_2},
	{ 0x13, KEY_3},
	{ 0x14, KEY_4},
	{ 0x15, KEY_5},
	{ 0x16, KEY_6},
	{ 0x17, KEY_7},
	{ 0x18, KEY_8},
	{ 0x19, KEY_9},
	{ 0x10, KEY_0},
	{ 0x1c, KEY_CHANNELUP},		/* ch+ */
	{ 0x0f, KEY_CHANNELDOWN},	/* ch- */
	{ 0x1a, KEY_VOLUMEUP},		/* vol+ */
	{ 0x0e, KEY_VOLUMEDOWN},	/* vol- */
	{ 0x04, KEY_RECORD},		/* rec */
	{ 0x09, KEY_CHANNEL},		/* fav */
	{ 0x08, KEY_BACKSPACE},		/* rewind */
	{ 0x07, KEY_FASTFORWARD},	/* fast */
	{ 0x0b, KEY_PAUSE},		/* pause */
	{ 0x02, KEY_ESC},		/* cancel */
	{ 0x03, KEY_TAB},		/* tab */
	{ 0x00, KEY_UP},		/* up */
	{ 0x1f, KEY_ENTER},		/* ok */
	{ 0x01, KEY_DOWN},		/* down */
	{ 0x05, KEY_RECORD},		/* cap */
	{ 0x06, KEY_STOP},		/* stop */
	{ 0x40, KEY_ZOOM},		/* full */
	{ 0x1e, KEY_TV},		/* tvmode */
	{ 0x1b, KEY_B},			/* recall */
};
struct ir_scancode_table ir_codes_dm1105_nec_table = {
	.scan = ir_codes_dm1105_nec,
	.size = ARRAY_SIZE(ir_codes_dm1105_nec),
};
EXPORT_SYMBOL_GPL(ir_codes_dm1105_nec_table);

/* Terratec Cinergy Hybrid T USB XS
   Devin Heitmueller <dheitmueller@linuxtv.org>
 */
static struct ir_scancode ir_codes_terratec_cinergy_xs[] = {
	{ 0x41, KEY_HOME},
	{ 0x01, KEY_POWER},
	{ 0x42, KEY_MENU},
	{ 0x02, KEY_1},
	{ 0x03, KEY_2},
	{ 0x04, KEY_3},
	{ 0x43, KEY_SUBTITLE},
	{ 0x05, KEY_4},
	{ 0x06, KEY_5},
	{ 0x07, KEY_6},
	{ 0x44, KEY_TEXT},
	{ 0x08, KEY_7},
	{ 0x09, KEY_8},
	{ 0x0a, KEY_9},
	{ 0x45, KEY_DELETE},
	{ 0x0b, KEY_TUNER},
	{ 0x0c, KEY_0},
	{ 0x0d, KEY_MODE},
	{ 0x46, KEY_TV},
	{ 0x47, KEY_DVD},
	{ 0x49, KEY_VIDEO},
	{ 0x4b, KEY_AUX},
	{ 0x10, KEY_UP},
	{ 0x11, KEY_LEFT},
	{ 0x12, KEY_OK},
	{ 0x13, KEY_RIGHT},
	{ 0x14, KEY_DOWN},
	{ 0x0f, KEY_EPG},
	{ 0x16, KEY_INFO},
	{ 0x4d, KEY_BACKSPACE},
	{ 0x1c, KEY_VOLUMEUP},
	{ 0x4c, KEY_PLAY},
	{ 0x1b, KEY_CHANNELUP},
	{ 0x1e, KEY_VOLUMEDOWN},
	{ 0x1d, KEY_MUTE},
	{ 0x1f, KEY_CHANNELDOWN},
	{ 0x17, KEY_RED},
	{ 0x18, KEY_GREEN},
	{ 0x19, KEY_YELLOW},
	{ 0x1a, KEY_BLUE},
	{ 0x58, KEY_RECORD},
	{ 0x48, KEY_STOP},
	{ 0x40, KEY_PAUSE},
	{ 0x54, KEY_LAST},
	{ 0x4e, KEY_REWIND},
	{ 0x4f, KEY_FASTFORWARD},
	{ 0x5c, KEY_NEXT},
};
struct ir_scancode_table ir_codes_terratec_cinergy_xs_table = {
	.scan = ir_codes_terratec_cinergy_xs,
	.size = ARRAY_SIZE(ir_codes_terratec_cinergy_xs),
};
EXPORT_SYMBOL_GPL(ir_codes_terratec_cinergy_xs_table);

/* EVGA inDtube
   Devin Heitmueller <devin.heitmueller@gmail.com>
 */
static struct ir_scancode ir_codes_evga_indtube[] = {
	{ 0x12, KEY_POWER},
	{ 0x02, KEY_MODE},	/* TV */
	{ 0x14, KEY_MUTE},
	{ 0x1a, KEY_CHANNELUP},
	{ 0x16, KEY_TV2},	/* PIP */
	{ 0x1d, KEY_VOLUMEUP},
	{ 0x05, KEY_CHANNELDOWN},
	{ 0x0f, KEY_PLAYPAUSE},
	{ 0x19, KEY_VOLUMEDOWN},
	{ 0x1c, KEY_REWIND},
	{ 0x0d, KEY_RECORD},
	{ 0x18, KEY_FORWARD},
	{ 0x1e, KEY_PREVIOUS},
	{ 0x1b, KEY_STOP},
	{ 0x1f, KEY_NEXT},
	{ 0x13, KEY_CAMERA},
};
struct ir_scancode_table ir_codes_evga_indtube_table = {
	.scan = ir_codes_evga_indtube,
	.size = ARRAY_SIZE(ir_codes_evga_indtube),
};
EXPORT_SYMBOL_GPL(ir_codes_evga_indtube_table);

static struct ir_scancode ir_codes_videomate_s350[] = {
	{ 0x00, KEY_TV},
	{ 0x01, KEY_DVD},
	{ 0x04, KEY_RECORD},
	{ 0x05, KEY_VIDEO},	/* TV/Video */
	{ 0x07, KEY_STOP},
	{ 0x08, KEY_PLAYPAUSE},
	{ 0x0a, KEY_REWIND},
	{ 0x0f, KEY_FASTFORWARD},
	{ 0x10, KEY_CHANNELUP},
	{ 0x12, KEY_VOLUMEUP},
	{ 0x13, KEY_CHANNELDOWN},
	{ 0x14, KEY_MUTE},
	{ 0x15, KEY_VOLUMEDOWN},
	{ 0x16, KEY_1},
	{ 0x17, KEY_2},
	{ 0x18, KEY_3},
	{ 0x19, KEY_4},
	{ 0x1a, KEY_5},
	{ 0x1b, KEY_6},
	{ 0x1c, KEY_7},
	{ 0x1d, KEY_8},
	{ 0x1e, KEY_9},
	{ 0x1f, KEY_0},
	{ 0x21, KEY_SLEEP},
	{ 0x24, KEY_ZOOM},
	{ 0x25, KEY_LAST},	/* Recall */
	{ 0x26, KEY_SUBTITLE},	/* CC */
	{ 0x27, KEY_LANGUAGE},	/* MTS */
	{ 0x29, KEY_CHANNEL},	/* SURF */
	{ 0x2b, KEY_A},
	{ 0x2c, KEY_B},
	{ 0x2f, KEY_CAMERA},	/* Snapshot */
	{ 0x23, KEY_RADIO},
	{ 0x02, KEY_PREVIOUSSONG},
	{ 0x06, KEY_NEXTSONG},
	{ 0x03, KEY_EPG},
	{ 0x09, KEY_SETUP},
	{ 0x22, KEY_BACKSPACE},
	{ 0x0c, KEY_UP},
	{ 0x0e, KEY_DOWN},
	{ 0x0b, KEY_LEFT},
	{ 0x0d, KEY_RIGHT},
	{ 0x11, KEY_ENTER},
	{ 0x20, KEY_TEXT},
};
struct ir_scancode_table ir_codes_videomate_s350_table = {
	.scan = ir_codes_videomate_s350,
	.size = ARRAY_SIZE(ir_codes_videomate_s350),
};
EXPORT_SYMBOL_GPL(ir_codes_videomate_s350_table);

/* GADMEI UTV330+ RM008Z remote
   Shine Liu <shinel@foxmail.com>
 */
static struct ir_scancode ir_codes_gadmei_rm008z[] = {
	{ 0x14, KEY_POWER2},		/* POWER OFF */
	{ 0x0c, KEY_MUTE},		/* MUTE */

	{ 0x18, KEY_TV},		/* TV */
	{ 0x0e, KEY_VIDEO},		/* AV */
	{ 0x0b, KEY_AUDIO},		/* SV */
	{ 0x0f, KEY_RADIO},		/* FM */

	{ 0x00, KEY_1},
	{ 0x01, KEY_2},
	{ 0x02, KEY_3},
	{ 0x03, KEY_4},
	{ 0x04, KEY_5},
	{ 0x05, KEY_6},
	{ 0x06, KEY_7},
	{ 0x07, KEY_8},
	{ 0x08, KEY_9},
	{ 0x09, KEY_0},
	{ 0x0a, KEY_INFO},		/* OSD */
	{ 0x1c, KEY_BACKSPACE},		/* LAST */

	{ 0x0d, KEY_PLAY},		/* PLAY */
	{ 0x1e, KEY_CAMERA},		/* SNAPSHOT */
	{ 0x1a, KEY_RECORD},		/* RECORD */
	{ 0x17, KEY_STOP},		/* STOP */

	{ 0x1f, KEY_UP},		/* UP */
	{ 0x44, KEY_DOWN},		/* DOWN */
	{ 0x46, KEY_TAB},		/* BACK */
	{ 0x4a, KEY_ZOOM},		/* FULLSECREEN */

	{ 0x10, KEY_VOLUMEUP},		/* VOLUMEUP */
	{ 0x11, KEY_VOLUMEDOWN},	/* VOLUMEDOWN */
	{ 0x12, KEY_CHANNELUP},		/* CHANNELUP */
	{ 0x13, KEY_CHANNELDOWN},	/* CHANNELDOWN */
	{ 0x15, KEY_ENTER},		/* OK */
};
struct ir_scancode_table ir_codes_gadmei_rm008z_table = {
	.scan = ir_codes_gadmei_rm008z,
	.size = ARRAY_SIZE(ir_codes_gadmei_rm008z),
};
EXPORT_SYMBOL_GPL(ir_codes_gadmei_rm008z_table);

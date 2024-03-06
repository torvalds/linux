// SPDX-License-Identifier: GPL-2.0-or-later
/* keytable for Terratec Cinergy S2 HD Remote Controller
 */

#include <media/rc-map.h>
#include <linux/module.h>

static struct rc_map_table terratec_cinergy_s2_hd[] = {
	{ 0x03, KEY_NEXT},               /* >| */
	{ 0x07, KEY_RECORD},
	{ 0x0b, KEY_PREVIOUS},           /* |< */
	{ 0x10, KEY_FASTFORWARD},        /* >> */
	{ 0x11, KEY_REWIND},             /* << */
	{ 0x12, KEY_ESC},                /* Back */
	{ 0x13, KEY_PLAY},
	{ 0x14, KEY_IMAGES},
	{ 0x15, KEY_AUDIO},
	{ 0x16, KEY_MEDIA},              /* Video-Menu */
	{ 0x17, KEY_STOP},
	{ 0x18, KEY_DVD},
	{ 0x19, KEY_TV},
	{ 0x1a, KEY_DELETE},
	{ 0x1b, KEY_TEXT},
	{ 0x1c, KEY_SUBTITLE},
	{ 0x1d, KEY_MENU},               /* DVD-Menu */
	{ 0x1e, KEY_HOME},
	{ 0x1f, KEY_PAUSE},
	{ 0x20, KEY_CHANNELDOWN},
	{ 0x21, KEY_VOLUMEDOWN},
	{ 0x22, KEY_MUTE},
	{ 0x23, KEY_VOLUMEUP},
	{ 0x24, KEY_CHANNELUP},
	{ 0x25, KEY_BLUE},
	{ 0x26, KEY_YELLOW},
	{ 0x27, KEY_GREEN},
	{ 0x28, KEY_RED},
	{ 0x29, KEY_INFO},
	{ 0x2b, KEY_DOWN},
	{ 0x2c, KEY_RIGHT},
	{ 0x2d, KEY_OK},
	{ 0x2e, KEY_LEFT},
	{ 0x2f, KEY_UP},
	{ 0x30, KEY_EPG},
	{ 0x32, KEY_VIDEO},              /* A<=>B */
	{ 0x33, KEY_NUMERIC_0},
	{ 0x34, KEY_VCR},                /* AV */
	{ 0x35, KEY_NUMERIC_9},
	{ 0x36, KEY_NUMERIC_8},
	{ 0x37, KEY_NUMERIC_7},
	{ 0x38, KEY_NUMERIC_6},
	{ 0x39, KEY_NUMERIC_5},
	{ 0x3a, KEY_NUMERIC_4},
	{ 0x3b, KEY_NUMERIC_3},
	{ 0x3c, KEY_NUMERIC_2},
	{ 0x3d, KEY_NUMERIC_1},
	{ 0x3e, KEY_POWER},

};

static struct rc_map_list terratec_cinergy_s2_hd_map = {
	.map = {
		.scan     = terratec_cinergy_s2_hd,
		.size     = ARRAY_SIZE(terratec_cinergy_s2_hd),
		.rc_proto = RC_PROTO_UNKNOWN,	/* Legacy IR type */
		.name     = RC_MAP_TERRATEC_CINERGY_S2_HD,
	}
};

static int __init init_rc_map_terratec_cinergy_s2_hd(void)
{
	return rc_map_register(&terratec_cinergy_s2_hd_map);
}

static void __exit exit_rc_map_terratec_cinergy_s2_hd(void)
{
	rc_map_unregister(&terratec_cinergy_s2_hd_map);
}

module_init(init_rc_map_terratec_cinergy_s2_hd);
module_exit(exit_rc_map_terratec_cinergy_s2_hd);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Terratec Cinergy S2 HD remote controller keytable");

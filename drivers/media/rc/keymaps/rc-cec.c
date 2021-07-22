// SPDX-License-Identifier: GPL-2.0-or-later
/* Keytable for the CEC remote control
 *
 * This keymap is unusual in that it can't be built as a module,
 * instead it is registered directly in rc-main.c if CONFIG_MEDIA_CEC_RC
 * is set. This is because it can be called from drm_dp_cec_set_edid() via
 * cec_register_adapter() in an asynchronous context, and it is not
 * allowed to use request_module() to load rc-cec.ko in that case.
 *
 * Since this keymap is only used if CONFIG_MEDIA_CEC_RC is set, we
 * just compile this keymap into the rc-core module and never as a
 * separate module.
 *
 * Copyright (c) 2015 by Kamil Debski
 */

#include <media/rc-map.h>
#include <linux/module.h>

/*
 * CEC Spec "High-Definition Multimedia Interface Specification" can be obtained
 * here: http://xtreamerdev.googlecode.com/files/CEC_Specs.pdf
 * The list of control codes is listed in Table 27: User Control Codes p. 95
 */

static struct rc_map_table cec[] = {
	{ 0x00, KEY_OK },
	{ 0x01, KEY_UP },
	{ 0x02, KEY_DOWN },
	{ 0x03, KEY_LEFT },
	{ 0x04, KEY_RIGHT },
	{ 0x05, KEY_RIGHT_UP },
	{ 0x06, KEY_RIGHT_DOWN },
	{ 0x07, KEY_LEFT_UP },
	{ 0x08, KEY_LEFT_DOWN },
	{ 0x09, KEY_ROOT_MENU }, /* CEC Spec: Device Root Menu - see Note 2 */
	/*
	 * Note 2: This is the initial display that a device shows. It is
	 * device-dependent and can be, for example, a contents menu, setup
	 * menu, favorite menu or other menu. The actual menu displayed
	 * may also depend on the device's current state.
	 */
	{ 0x0a, KEY_SETUP },
	{ 0x0b, KEY_MENU }, /* CEC Spec: Contents Menu */
	{ 0x0c, KEY_FAVORITES }, /* CEC Spec: Favorite Menu */
	{ 0x0d, KEY_EXIT },
	/* 0x0e-0x0f: Reserved */
	{ 0x10, KEY_MEDIA_TOP_MENU },
	{ 0x11, KEY_CONTEXT_MENU },
	/* 0x12-0x1c: Reserved */
	{ 0x1d, KEY_DIGITS }, /* CEC Spec: select/toggle a Number Entry Mode */
	{ 0x1e, KEY_NUMERIC_11 },
	{ 0x1f, KEY_NUMERIC_12 },
	/* 0x20-0x29: Keys 0 to 9 */
	{ 0x20, KEY_NUMERIC_0 },
	{ 0x21, KEY_NUMERIC_1 },
	{ 0x22, KEY_NUMERIC_2 },
	{ 0x23, KEY_NUMERIC_3 },
	{ 0x24, KEY_NUMERIC_4 },
	{ 0x25, KEY_NUMERIC_5 },
	{ 0x26, KEY_NUMERIC_6 },
	{ 0x27, KEY_NUMERIC_7 },
	{ 0x28, KEY_NUMERIC_8 },
	{ 0x29, KEY_NUMERIC_9 },
	{ 0x2a, KEY_DOT },
	{ 0x2b, KEY_ENTER },
	{ 0x2c, KEY_CLEAR },
	/* 0x2d-0x2e: Reserved */
	{ 0x2f, KEY_NEXT_FAVORITE }, /* CEC Spec: Next Favorite */
	{ 0x30, KEY_CHANNELUP },
	{ 0x31, KEY_CHANNELDOWN },
	{ 0x32, KEY_PREVIOUS }, /* CEC Spec: Previous Channel */
	{ 0x33, KEY_SOUND }, /* CEC Spec: Sound Select */
	{ 0x34, KEY_VIDEO }, /* 0x34: CEC Spec: Input Select */
	{ 0x35, KEY_INFO }, /* CEC Spec: Display Information */
	{ 0x36, KEY_HELP },
	{ 0x37, KEY_PAGEUP },
	{ 0x38, KEY_PAGEDOWN },
	/* 0x39-0x3f: Reserved */
	{ 0x40, KEY_POWER },
	{ 0x41, KEY_VOLUMEUP },
	{ 0x42, KEY_VOLUMEDOWN },
	{ 0x43, KEY_MUTE },
	{ 0x44, KEY_PLAYCD },
	{ 0x45, KEY_STOPCD },
	{ 0x46, KEY_PAUSECD },
	{ 0x47, KEY_RECORD },
	{ 0x48, KEY_REWIND },
	{ 0x49, KEY_FASTFORWARD },
	{ 0x4a, KEY_EJECTCD }, /* CEC Spec: Eject */
	{ 0x4b, KEY_FORWARD },
	{ 0x4c, KEY_BACK },
	{ 0x4d, KEY_STOP_RECORD }, /* CEC Spec: Stop-Record */
	{ 0x4e, KEY_PAUSE_RECORD }, /* CEC Spec: Pause-Record */
	/* 0x4f: Reserved */
	{ 0x50, KEY_ANGLE },
	{ 0x51, KEY_TV2 },
	{ 0x52, KEY_VOD }, /* CEC Spec: Video on Demand */
	{ 0x53, KEY_EPG },
	{ 0x54, KEY_TIME }, /* CEC Spec: Timer */
	{ 0x55, KEY_CONFIG },
	/*
	 * The following codes are hard to implement at this moment, as they
	 * carry an additional additional argument. Most likely changes to RC
	 * framework are necessary.
	 * For now they are interpreted by the CEC framework as non keycodes
	 * and are passed as messages enabling user application to parse them.
	 */
	/* 0x56: CEC Spec: Select Broadcast Type */
	/* 0x57: CEC Spec: Select Sound presentation */
	{ 0x58, KEY_AUDIO_DESC }, /* CEC 2.0 and up */
	{ 0x59, KEY_WWW }, /* CEC 2.0 and up */
	{ 0x5a, KEY_3D_MODE }, /* CEC 2.0 and up */
	/* 0x5b-0x5f: Reserved */
	{ 0x60, KEY_PLAYCD }, /* CEC Spec: Play Function */
	{ 0x6005, KEY_FASTFORWARD },
	{ 0x6006, KEY_FASTFORWARD },
	{ 0x6007, KEY_FASTFORWARD },
	{ 0x6015, KEY_SLOW },
	{ 0x6016, KEY_SLOW },
	{ 0x6017, KEY_SLOW },
	{ 0x6009, KEY_FASTREVERSE },
	{ 0x600a, KEY_FASTREVERSE },
	{ 0x600b, KEY_FASTREVERSE },
	{ 0x6019, KEY_SLOWREVERSE },
	{ 0x601a, KEY_SLOWREVERSE },
	{ 0x601b, KEY_SLOWREVERSE },
	{ 0x6020, KEY_REWIND },
	{ 0x6024, KEY_PLAYCD },
	{ 0x6025, KEY_PAUSECD },
	{ 0x61, KEY_PLAYPAUSE }, /* CEC Spec: Pause-Play Function */
	{ 0x62, KEY_RECORD }, /* Spec: Record Function */
	{ 0x63, KEY_PAUSE_RECORD }, /* CEC Spec: Pause-Record Function */
	{ 0x64, KEY_STOPCD }, /* CEC Spec: Stop Function */
	{ 0x65, KEY_MUTE }, /* CEC Spec: Mute Function */
	{ 0x66, KEY_UNMUTE }, /* CEC Spec: Restore the volume */
	/*
	 * The following codes are hard to implement at this moment, as they
	 * carry an additional additional argument. Most likely changes to RC
	 * framework are necessary.
	 * For now they are interpreted by the CEC framework as non keycodes
	 * and are passed as messages enabling user application to parse them.
	 */
	/* 0x67: CEC Spec: Tune Function */
	/* 0x68: CEC Spec: Seleect Media Function */
	/* 0x69: CEC Spec: Select A/V Input Function */
	/* 0x6a: CEC Spec: Select Audio Input Function */
	{ 0x6b, KEY_POWER }, /* CEC Spec: Power Toggle Function */
	{ 0x6c, KEY_SLEEP }, /* CEC Spec: Power Off Function */
	{ 0x6d, KEY_WAKEUP }, /* CEC Spec: Power On Function */
	/* 0x6e-0x70: Reserved */
	{ 0x71, KEY_BLUE }, /* CEC Spec: F1 (Blue) */
	{ 0x72, KEY_RED }, /* CEC Spec: F2 (Red) */
	{ 0x73, KEY_GREEN }, /* CEC Spec: F3 (Green) */
	{ 0x74, KEY_YELLOW }, /* CEC Spec: F4 (Yellow) */
	{ 0x75, KEY_F5 },
	{ 0x76, KEY_DATA }, /* CEC Spec: Data - see Note 3 */
	/*
	 * Note 3: This is used, for example, to enter or leave a digital TV
	 * data broadcast application.
	 */
	/* 0x77-0xff: Reserved */
};

struct rc_map_list cec_map = {
	.map = {
		.scan		= cec,
		.size		= ARRAY_SIZE(cec),
		.rc_proto	= RC_PROTO_CEC,
		.name		= RC_MAP_CEC,
	}
};

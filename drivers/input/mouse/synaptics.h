/*
 * Synaptics TouchPad PS/2 mouse driver
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#ifndef _SYNAPTICS_H
#define _SYNAPTICS_H

/* synaptics queries */
#define SYN_QUE_IDENTIFY		0x00
#define SYN_QUE_MODES			0x01
#define SYN_QUE_CAPABILITIES		0x02
#define SYN_QUE_MODEL			0x03
#define SYN_QUE_SERIAL_NUMBER_PREFIX	0x06
#define SYN_QUE_SERIAL_NUMBER_SUFFIX	0x07
#define SYN_QUE_RESOLUTION		0x08
#define SYN_QUE_EXT_CAPAB		0x09
#define SYN_QUE_FIRMWARE_ID		0x0a
#define SYN_QUE_EXT_CAPAB_0C		0x0c
#define SYN_QUE_EXT_MAX_COORDS		0x0d
#define SYN_QUE_EXT_MIN_COORDS		0x0f
#define SYN_QUE_MEXT_CAPAB_10		0x10

/* synatics modes */
#define SYN_BIT_ABSOLUTE_MODE		BIT(7)
#define SYN_BIT_HIGH_RATE		BIT(6)
#define SYN_BIT_SLEEP_MODE		BIT(3)
#define SYN_BIT_DISABLE_GESTURE		BIT(2)
#define SYN_BIT_FOUR_BYTE_CLIENT	BIT(1)
#define SYN_BIT_W_MODE			BIT(0)

/* synaptics model ID bits */
#define SYN_MODEL_ROT180(m)		((m) & BIT(23))
#define SYN_MODEL_PORTRAIT(m)		((m) & BIT(22))
#define SYN_MODEL_SENSOR(m)		(((m) & GENMASK(21, 16)) >> 16)
#define SYN_MODEL_HARDWARE(m)		(((m) & GENMASK(15, 9)) >> 9)
#define SYN_MODEL_NEWABS(m)		((m) & BIT(7))
#define SYN_MODEL_PEN(m)		((m) & BIT(6))
#define SYN_MODEL_SIMPLIC(m)		((m) & BIT(5))
#define SYN_MODEL_GEOMETRY(m)		((m) & GENMASK(3, 0))

/* synaptics capability bits */
#define SYN_CAP_EXTENDED(c)		((c) & BIT(23))
#define SYN_CAP_MIDDLE_BUTTON(c)	((c) & BIT(18))
#define SYN_CAP_PASS_THROUGH(c)		((c) & BIT(7))
#define SYN_CAP_SLEEP(c)		((c) & BIT(4))
#define SYN_CAP_FOUR_BUTTON(c)		((c) & BIT(3))
#define SYN_CAP_MULTIFINGER(c)		((c) & BIT(1))
#define SYN_CAP_PALMDETECT(c)		((c) & BIT(0))
#define SYN_CAP_SUBMODEL_ID(c)		(((c) & GENMASK(15, 8)) >> 8)
#define SYN_EXT_CAP_REQUESTS(c)		(((c) & GENMASK(22, 20)) >> 20)
#define SYN_CAP_MB_MASK			GENMASK(15, 12)
#define SYN_CAP_MULTI_BUTTON_NO(ec)	(((ec) & SYN_CAP_MB_MASK) >> 12)
#define SYN_CAP_PRODUCT_ID(ec)		(((ec) & GENMASK(23, 16)) >> 16)
#define SYN_MEXT_CAP_BIT(m)		((m) & BIT(1))

/*
 * The following describes response for the 0x0c query.
 *
 * byte	mask	name			meaning
 * ----	----	-------			------------
 * 1	0x01	adjustable threshold	capacitive button sensitivity
 *					can be adjusted
 * 1	0x02	report max		query 0x0d gives max coord reported
 * 1	0x04	clearpad		sensor is ClearPad product
 * 1	0x08	advanced gesture	not particularly meaningful
 * 1	0x10	clickpad bit 0		1-button ClickPad
 * 1	0x60	multifinger mode	identifies firmware finger counting
 *					(not reporting!) algorithm.
 *					Not particularly meaningful
 * 1	0x80	covered pad		W clipped to 14, 15 == pad mostly covered
 * 2	0x01	clickpad bit 1		2-button ClickPad
 * 2	0x02	deluxe LED controls	touchpad support LED commands
 *					ala multimedia control bar
 * 2	0x04	reduced filtering	firmware does less filtering on
 *					position data, driver should watch
 *					for noise.
 * 2	0x08	image sensor		image sensor tracks 5 fingers, but only
 *					reports 2.
 * 2	0x01	uniform clickpad	whole clickpad moves instead of being
 *					hinged at the top.
 * 2	0x20	report min		query 0x0f gives min coord reported
 */
#define SYN_CAP_CLICKPAD(ex0c)		((ex0c) & BIT(20)) /* 1-button ClickPad */
#define SYN_CAP_CLICKPAD2BTN(ex0c)	((ex0c) & BIT(8))  /* 2-button ClickPad */
#define SYN_CAP_MAX_DIMENSIONS(ex0c)	((ex0c) & BIT(17))
#define SYN_CAP_MIN_DIMENSIONS(ex0c)	((ex0c) & BIT(13))
#define SYN_CAP_ADV_GESTURE(ex0c)	((ex0c) & BIT(19))
#define SYN_CAP_REDUCED_FILTERING(ex0c)	((ex0c) & BIT(10))
#define SYN_CAP_IMAGE_SENSOR(ex0c)	((ex0c) & BIT(11))
#define SYN_CAP_INTERTOUCH(ex0c)	((ex0c) & BIT(14))

/*
 * The following descibes response for the 0x10 query.
 *
 * byte	mask	name			meaning
 * ----	----	-------			------------
 * 1	0x01	ext buttons are stick	buttons exported in the extended
 *					capability are actually meant to be used
 *					by the tracktick (pass-through).
 * 1	0x02	SecurePad		the touchpad is a SecurePad, so it
 *					contains a built-in fingerprint reader.
 * 1	0xe0	more ext count		how many more extented queries are
 *					available after this one.
 * 2	0xff	SecurePad width		the width of the SecurePad fingerprint
 *					reader.
 * 3	0xff	SecurePad height	the height of the SecurePad fingerprint
 *					reader.
 */
#define SYN_CAP_EXT_BUTTONS_STICK(ex10)	((ex10) & BIT(16))
#define SYN_CAP_SECUREPAD(ex10)		((ex10) & BIT(17))

#define SYN_EXT_BUTTON_STICK_L(eb)	(((eb) & BIT(0)) >> 0)
#define SYN_EXT_BUTTON_STICK_M(eb)	(((eb) & BIT(1)) >> 1)
#define SYN_EXT_BUTTON_STICK_R(eb)	(((eb) & BIT(2)) >> 2)

/* synaptics modes query bits */
#define SYN_MODE_ABSOLUTE(m)		((m) & BIT(7))
#define SYN_MODE_RATE(m)		((m) & BIT(6))
#define SYN_MODE_BAUD_SLEEP(m)		((m) & BIT(3))
#define SYN_MODE_DISABLE_GESTURE(m)	((m) & BIT(2))
#define SYN_MODE_PACKSIZE(m)		((m) & BIT(1))
#define SYN_MODE_WMODE(m)		((m) & BIT(0))

/* synaptics identify query bits */
#define SYN_ID_MODEL(i)			(((i) & GENMASK(7, 4)) >> 4)
#define SYN_ID_MAJOR(i)			(((i) & GENMASK(3, 0)) >> 0)
#define SYN_ID_MINOR(i)			(((i) & GENMASK(23, 16)) >> 16)
#define SYN_ID_FULL(i)			((SYN_ID_MAJOR(i) << 8) | SYN_ID_MINOR(i))
#define SYN_ID_IS_SYNAPTICS(i)		(((i) & GENMASK(15, 8)) == 0x004700U)
#define SYN_ID_DISGEST_SUPPORTED(i)	(SYN_ID_MAJOR(i) >= 4)

/* synaptics special commands */
#define SYN_PS_SET_MODE2		0x14
#define SYN_PS_CLIENT_CMD		0x28

/* amount to fuzz position data when touchpad reports reduced filtering */
#define SYN_REDUCED_FILTER_FUZZ		8

/* synaptics packet types */
enum synaptics_pkt_type {
	SYN_NEWABS,
	SYN_NEWABS_STRICT,
	SYN_NEWABS_RELAXED,
	SYN_OLDABS,
};

/*
 * A structure to describe the state of the touchpad hardware (buttons and pad)
 */
struct synaptics_hw_state {
	int x;
	int y;
	int z;
	int w;
	unsigned int left:1;
	unsigned int right:1;
	unsigned int middle:1;
	unsigned int up:1;
	unsigned int down:1;
	u8 ext_buttons;
	s8 scroll;
};

/* Data read from the touchpad */
struct synaptics_device_info {
	u32 model_id;		/* Model-ID */
	u32 firmware_id;	/* Firmware-ID */
	u32 board_id;		/* Board-ID */
	u32 capabilities;	/* Capabilities */
	u32 ext_cap;		/* Extended Capabilities */
	u32 ext_cap_0c;		/* Ext Caps from 0x0c query */
	u32 ext_cap_10;		/* Ext Caps from 0x10 query */
	u32 identity;		/* Identification */
	u32 x_res, y_res;	/* X/Y resolution in units/mm */
	u32 x_max, y_max;	/* Max coordinates (from FW) */
	u32 x_min, y_min;	/* Min coordinates (from FW) */
};

struct synaptics_data {
	struct synaptics_device_info info;

	enum synaptics_pkt_type pkt_type;	/* packet type - old, new, etc */
	u8 mode;				/* current mode byte */
	int scroll;

	bool absolute_mode;			/* run in Absolute mode */
	bool disable_gesture;			/* disable gestures */

	struct serio *pt_port;			/* Pass-through serio port */

	/*
	 * Last received Advanced Gesture Mode (AGM) packet. An AGM packet
	 * contains position data for a second contact, at half resolution.
	 */
	struct synaptics_hw_state agm;
	unsigned int agm_count;			/* finger count reported by agm */

	/* ForcePad handling */
	unsigned long				press_start;
	bool					press;
	bool					report_press;
	bool					is_forcepad;
};

void synaptics_module_init(void);
int synaptics_detect(struct psmouse *psmouse, bool set_properties);
int synaptics_init_absolute(struct psmouse *psmouse);
int synaptics_init_relative(struct psmouse *psmouse);
int synaptics_init_smbus(struct psmouse *psmouse);
int synaptics_init(struct psmouse *psmouse);
void synaptics_reset(struct psmouse *psmouse);

#endif /* _SYNAPTICS_H */

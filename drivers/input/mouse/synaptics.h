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

/* synatics modes */
#define SYN_BIT_ABSOLUTE_MODE		(1 << 7)
#define SYN_BIT_HIGH_RATE		(1 << 6)
#define SYN_BIT_SLEEP_MODE		(1 << 3)
#define SYN_BIT_DISABLE_GESTURE		(1 << 2)
#define SYN_BIT_FOUR_BYTE_CLIENT	(1 << 1)
#define SYN_BIT_W_MODE			(1 << 0)

/* synaptics model ID bits */
#define SYN_MODEL_ROT180(m)		((m) & (1 << 23))
#define SYN_MODEL_PORTRAIT(m)		((m) & (1 << 22))
#define SYN_MODEL_SENSOR(m)		(((m) >> 16) & 0x3f)
#define SYN_MODEL_HARDWARE(m)		(((m) >> 9) & 0x7f)
#define SYN_MODEL_NEWABS(m)		((m) & (1 << 7))
#define SYN_MODEL_PEN(m)		((m) & (1 << 6))
#define SYN_MODEL_SIMPLIC(m)		((m) & (1 << 5))
#define SYN_MODEL_GEOMETRY(m)		((m) & 0x0f)

/* synaptics capability bits */
#define SYN_CAP_EXTENDED(c)		((c) & (1 << 23))
#define SYN_CAP_MIDDLE_BUTTON(c)	((c) & (1 << 18))
#define SYN_CAP_PASS_THROUGH(c)		((c) & (1 << 7))
#define SYN_CAP_SLEEP(c)		((c) & (1 << 4))
#define SYN_CAP_FOUR_BUTTON(c)		((c) & (1 << 3))
#define SYN_CAP_MULTIFINGER(c)		((c) & (1 << 1))
#define SYN_CAP_PALMDETECT(c)		((c) & (1 << 0))
#define SYN_CAP_SUBMODEL_ID(c)		(((c) & 0x00ff00) >> 8)
#define SYN_EXT_CAP_REQUESTS(c)		(((c) & 0x700000) >> 20)
#define SYN_CAP_MULTI_BUTTON_NO(ec)	(((ec) & 0x00f000) >> 12)
#define SYN_CAP_PRODUCT_ID(ec)		(((ec) & 0xff0000) >> 16)

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
 * 2	0x20	report min		query 0x0f gives min coord reported
 */
#define SYN_CAP_CLICKPAD(ex0c)		((ex0c) & 0x100000) /* 1-button ClickPad */
#define SYN_CAP_CLICKPAD2BTN(ex0c)	((ex0c) & 0x000100) /* 2-button ClickPad */
#define SYN_CAP_MAX_DIMENSIONS(ex0c)	((ex0c) & 0x020000)
#define SYN_CAP_MIN_DIMENSIONS(ex0c)	((ex0c) & 0x002000)
#define SYN_CAP_ADV_GESTURE(ex0c)	((ex0c) & 0x080000)
#define SYN_CAP_REDUCED_FILTERING(ex0c)	((ex0c) & 0x000400)
#define SYN_CAP_IMAGE_SENSOR(ex0c)	((ex0c) & 0x000800)

/* synaptics modes query bits */
#define SYN_MODE_ABSOLUTE(m)		((m) & (1 << 7))
#define SYN_MODE_RATE(m)		((m) & (1 << 6))
#define SYN_MODE_BAUD_SLEEP(m)		((m) & (1 << 3))
#define SYN_MODE_DISABLE_GESTURE(m)	((m) & (1 << 2))
#define SYN_MODE_PACKSIZE(m)		((m) & (1 << 1))
#define SYN_MODE_WMODE(m)		((m) & (1 << 0))

/* synaptics identify query bits */
#define SYN_ID_MODEL(i)			(((i) >> 4) & 0x0f)
#define SYN_ID_MAJOR(i)			((i) & 0x0f)
#define SYN_ID_MINOR(i)			(((i) >> 16) & 0xff)
#define SYN_ID_FULL(i)			((SYN_ID_MAJOR(i) << 8) | SYN_ID_MINOR(i))
#define SYN_ID_IS_SYNAPTICS(i)		((((i) >> 8) & 0xff) == 0x47)
#define SYN_ID_DISGEST_SUPPORTED(i)	(SYN_ID_MAJOR(i) >= 4)

/* synaptics special commands */
#define SYN_PS_SET_MODE2		0x14
#define SYN_PS_CLIENT_CMD		0x28

/* synaptics packet types */
#define SYN_NEWABS			0
#define SYN_NEWABS_STRICT		1
#define SYN_NEWABS_RELAXED		2
#define SYN_OLDABS			3

/* amount to fuzz position data when touchpad reports reduced filtering */
#define SYN_REDUCED_FILTER_FUZZ		8

/*
 * A structure to describe which internal touchpad finger slots are being
 * reported in raw packets.
 */
struct synaptics_mt_state {
	int count;			/* num fingers being tracked */
	int sgm;			/* which slot is reported by sgm pkt */
	int agm;			/* which slot is reported by agm pkt*/
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
	unsigned char ext_buttons;
	signed char scroll;

	/* As reported in last AGM-CONTACT packets */
	struct synaptics_mt_state mt_state;
};

struct synaptics_data {
	/* Data read from the touchpad */
	unsigned long int model_id;		/* Model-ID */
	unsigned long int firmware_id;		/* Firmware-ID */
	unsigned long int board_id;		/* Board-ID */
	unsigned long int capabilities;		/* Capabilities */
	unsigned long int ext_cap;		/* Extended Capabilities */
	unsigned long int ext_cap_0c;		/* Ext Caps from 0x0c query */
	unsigned long int identity;		/* Identification */
	unsigned int x_res, y_res;		/* X/Y resolution in units/mm */
	unsigned int x_max, y_max;		/* Max coordinates (from FW) */
	unsigned int x_min, y_min;		/* Min coordinates (from FW) */

	unsigned char pkt_type;			/* packet type - old, new, etc */
	unsigned char mode;			/* current mode byte */
	int scroll;

	bool absolute_mode;			/* run in Absolute mode */
	bool disable_gesture;			/* disable gestures */

	struct serio *pt_port;			/* Pass-through serio port */

	struct synaptics_mt_state mt_state;	/* Current mt finger state */
	bool mt_state_lost;			/* mt_state may be incorrect */

	/*
	 * Last received Advanced Gesture Mode (AGM) packet. An AGM packet
	 * contains position data for a second contact, at half resolution.
	 */
	struct synaptics_hw_state agm;
	bool agm_pending;			/* new AGM packet received */
};

void synaptics_module_init(void);
int synaptics_detect(struct psmouse *psmouse, bool set_properties);
int synaptics_init(struct psmouse *psmouse);
int synaptics_init_relative(struct psmouse *psmouse);
void synaptics_reset(struct psmouse *psmouse);
bool synaptics_supported(void);

#endif /* _SYNAPTICS_H */

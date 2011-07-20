/*
 * Elantech Touchpad driver (v6)
 *
 * Copyright (C) 2007-2009 Arjan Opmeer <arjan@opmeer.net>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * Trademarks are the property of their respective owners.
 */

#ifndef _ELANTECH_H
#define _ELANTECH_H

/*
 * Command values for Synaptics style queries
 */
#define ETP_FW_VERSION_QUERY		0x01
#define ETP_CAPABILITIES_QUERY		0x02

/*
 * Command values for register reading or writing
 */
#define ETP_REGISTER_READ		0x10
#define ETP_REGISTER_WRITE		0x11

/*
 * Hardware version 2 custom PS/2 command value
 */
#define ETP_PS2_CUSTOM_COMMAND		0xf8

/*
 * Times to retry a ps2_command and millisecond delay between tries
 */
#define ETP_PS2_COMMAND_TRIES		3
#define ETP_PS2_COMMAND_DELAY		500

/*
 * Times to try to read back a register and millisecond delay between tries
 */
#define ETP_READ_BACK_TRIES		5
#define ETP_READ_BACK_DELAY		2000

/*
 * Register bitmasks for hardware version 1
 */
#define ETP_R10_ABSOLUTE_MODE		0x04
#define ETP_R11_4_BYTE_MODE		0x02

/*
 * Capability bitmasks
 */
#define ETP_CAP_HAS_ROCKER		0x04

/*
 * One hard to find application note states that X axis range is 0 to 576
 * and Y axis range is 0 to 384 for harware version 1.
 * Edge fuzz might be necessary because of bezel around the touchpad
 */
#define ETP_EDGE_FUZZ_V1		32

#define ETP_XMIN_V1			(  0 + ETP_EDGE_FUZZ_V1)
#define ETP_XMAX_V1			(576 - ETP_EDGE_FUZZ_V1)
#define ETP_YMIN_V1			(  0 + ETP_EDGE_FUZZ_V1)
#define ETP_YMAX_V1			(384 - ETP_EDGE_FUZZ_V1)

/*
 * It seems the resolution for hardware version 2 doubled.
 * Hence the X and Y ranges are doubled too.
 * The bezel around the pad also appears to be smaller
 */
#define ETP_EDGE_FUZZ_V2		8

#define ETP_XMIN_V2			(   0 + ETP_EDGE_FUZZ_V2)
#define ETP_XMAX_V2			(1152 - ETP_EDGE_FUZZ_V2)
#define ETP_YMIN_V2			(   0 + ETP_EDGE_FUZZ_V2)
#define ETP_YMAX_V2			( 768 - ETP_EDGE_FUZZ_V2)

/*
 * For two finger touches the coordinate of each finger gets reported
 * separately but with reduced resolution.
 */
#define ETP_2FT_FUZZ			4

#define ETP_2FT_XMIN			(  0 + ETP_2FT_FUZZ)
#define ETP_2FT_XMAX			(288 - ETP_2FT_FUZZ)
#define ETP_2FT_YMIN			(  0 + ETP_2FT_FUZZ)
#define ETP_2FT_YMAX			(192 - ETP_2FT_FUZZ)

struct elantech_data {
	unsigned char reg_10;
	unsigned char reg_11;
	unsigned char reg_20;
	unsigned char reg_21;
	unsigned char reg_22;
	unsigned char reg_23;
	unsigned char reg_24;
	unsigned char reg_25;
	unsigned char reg_26;
	unsigned char debug;
	unsigned char capabilities;
	bool paritycheck;
	bool jumpy_cursor;
	unsigned char hw_version;
	unsigned int fw_version;
	unsigned int single_finger_reports;
	unsigned char parity[256];
};

#ifdef CONFIG_MOUSE_PS2_ELANTECH
int elantech_detect(struct psmouse *psmouse, bool set_properties);
int elantech_init(struct psmouse *psmouse);
#else
static inline int elantech_detect(struct psmouse *psmouse, bool set_properties)
{
	return -ENOSYS;
}
static inline int elantech_init(struct psmouse *psmouse)
{
	return -ENOSYS;
}
#endif /* CONFIG_MOUSE_PS2_ELANTECH */

#endif

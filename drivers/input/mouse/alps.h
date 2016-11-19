/*
 * ALPS touchpad PS/2 mouse driver
 *
 * Copyright (c) 2003 Peter Osterlund <petero2@telia.com>
 * Copyright (c) 2005 Vojtech Pavlik <vojtech@suse.cz>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#ifndef _ALPS_H
#define _ALPS_H

#include <linux/input/mt.h>

#define ALPS_PROTO_V1		0x100
#define ALPS_PROTO_V2		0x200
#define ALPS_PROTO_V3		0x300
#define ALPS_PROTO_V3_RUSHMORE	0x310
#define ALPS_PROTO_V4		0x400
#define ALPS_PROTO_V5		0x500
#define ALPS_PROTO_V6		0x600
#define ALPS_PROTO_V7		0x700	/* t3btl t4s */
#define ALPS_PROTO_V8		0x800	/* SS4btl SS4s */

#define MAX_TOUCHES	4

#define DOLPHIN_COUNT_PER_ELECTRODE	64
#define DOLPHIN_PROFILE_XOFFSET		8	/* x-electrode offset */
#define DOLPHIN_PROFILE_YOFFSET		1	/* y-electrode offset */

/*
 * enum SS4_PACKET_ID - defines the packet type for V8
 * SS4_PACKET_ID_IDLE: There's no finger and no button activity.
 * SS4_PACKET_ID_ONE: There's one finger on touchpad
 *  or there's button activities.
 * SS4_PACKET_ID_TWO: There's two or more fingers on touchpad
 * SS4_PACKET_ID_MULTI: There's three or more fingers on touchpad
 * SS4_PACKET_ID_STICK: A stick pointer packet
*/
enum SS4_PACKET_ID {
	SS4_PACKET_ID_IDLE = 0,
	SS4_PACKET_ID_ONE,
	SS4_PACKET_ID_TWO,
	SS4_PACKET_ID_MULTI,
	SS4_PACKET_ID_STICK,
};

#define SS4_COUNT_PER_ELECTRODE		256
#define SS4_NUMSENSOR_XOFFSET		7
#define SS4_NUMSENSOR_YOFFSET		7
#define SS4_MIN_PITCH_MM		50

#define SS4_MASK_NORMAL_BUTTONS		0x07

#define SS4_1F_X_V2(_b)		((_b[0] & 0x0007) |		\
				 ((_b[1] << 3) & 0x0078) |	\
				 ((_b[1] << 2) & 0x0380) |	\
				 ((_b[2] << 5) & 0x1C00)	\
				)

#define SS4_1F_Y_V2(_b)		(((_b[2]) & 0x000F) |		\
				 ((_b[3] >> 2) & 0x0030) |	\
				 ((_b[4] << 6) & 0x03C0) |	\
				 ((_b[4] << 5) & 0x0C00)	\
				)

#define SS4_1F_Z_V2(_b)		(((_b[5]) & 0x0F) |		\
				 ((_b[5] >> 1) & 0x70) |	\
				 ((_b[4]) & 0x80)		\
				)

#define SS4_1F_LFB_V2(_b)	(((_b[2] >> 4) & 0x01) == 0x01)

#define SS4_MF_LF_V2(_b, _i)	((_b[1 + (_i) * 3] & 0x0004) == 0x0004)

#define SS4_BTN_V2(_b)		((_b[0] >> 5) & SS4_MASK_NORMAL_BUTTONS)

#define SS4_STD_MF_X_V2(_b, _i)	(((_b[0 + (_i) * 3] << 5) & 0x00E0) |	\
				 ((_b[1 + _i * 3]  << 5) & 0x1F00)	\
				)

#define SS4_STD_MF_Y_V2(_b, _i)	(((_b[1 + (_i) * 3] << 3) & 0x0010) |	\
				 ((_b[2 + (_i) * 3] << 5) & 0x01E0) |	\
				 ((_b[2 + (_i) * 3] << 4) & 0x0E00)	\
				)

#define SS4_BTL_MF_X_V2(_b, _i)	(SS4_STD_MF_X_V2(_b, _i) |		\
				 ((_b[0 + (_i) * 3] >> 3) & 0x0010)	\
				)

#define SS4_BTL_MF_Y_V2(_b, _i)	(SS4_STD_MF_Y_V2(_b, _i) | \
				 ((_b[0 + (_i) * 3] >> 3) & 0x0008)	\
				)

#define SS4_MF_Z_V2(_b, _i)	(((_b[1 + (_i) * 3]) & 0x0001) |	\
				 ((_b[1 + (_i) * 3] >> 1) & 0x0002)	\
				)

#define SS4_IS_MF_CONTINUE(_b)	((_b[2] & 0x10) == 0x10)
#define SS4_IS_5F_DETECTED(_b)	((_b[2] & 0x10) == 0x10)


#define SS4_MFPACKET_NO_AX	8160	/* X-Coordinate value */
#define SS4_MFPACKET_NO_AY	4080	/* Y-Coordinate value */
#define SS4_MFPACKET_NO_AX_BL	8176	/* Buttonless X-Coordinate value */
#define SS4_MFPACKET_NO_AY_BL	4088	/* Buttonless Y-Coordinate value */

/*
 * enum V7_PACKET_ID - defines the packet type for V7
 * V7_PACKET_ID_IDLE: There's no finger and no button activity.
 * V7_PACKET_ID_TWO: There's one or two non-resting fingers on touchpad
 *  or there's button activities.
 * V7_PACKET_ID_MULTI: There are at least three non-resting fingers.
 * V7_PACKET_ID_NEW: The finger position in slot is not continues from
 *  previous packet.
*/
enum V7_PACKET_ID {
	 V7_PACKET_ID_IDLE,
	 V7_PACKET_ID_TWO,
	 V7_PACKET_ID_MULTI,
	 V7_PACKET_ID_NEW,
	 V7_PACKET_ID_UNKNOWN,
};

/**
 * struct alps_protocol_info - information about protocol used by a device
 * @version: Indicates V1/V2/V3/...
 * @byte0: Helps figure out whether a position report packet matches the
 *   known format for this model.  The first byte of the report, ANDed with
 *   mask0, should match byte0.
 * @mask0: The mask used to check the first byte of the report.
 * @flags: Additional device capabilities (passthrough port, trackstick, etc.).
 */
struct alps_protocol_info {
	u16 version;
	u8 byte0, mask0;
	unsigned int flags;
};

/**
 * struct alps_model_info - touchpad ID table
 * @signature: E7 response string to match.
 * @command_mode_resp: For V3/V4 touchpads, the final byte of the EC response
 *   (aka command mode response) identifies the firmware minor version.  This
 *   can be used to distinguish different hardware models which are not
 *   uniquely identifiable through their E7 responses.
 * @protocol_info: information about protcol used by the device.
 *
 * Many (but not all) ALPS touchpads can be identified by looking at the
 * values returned in the "E7 report" and/or the "EC report."  This table
 * lists a number of such touchpads.
 */
struct alps_model_info {
	u8 signature[3];
	u8 command_mode_resp;
	struct alps_protocol_info protocol_info;
};

/**
 * struct alps_nibble_commands - encodings for register accesses
 * @command: PS/2 command used for the nibble
 * @data: Data supplied as an argument to the PS/2 command, if applicable
 *
 * The ALPS protocol uses magic sequences to transmit binary data to the
 * touchpad, as it is generally not OK to send arbitrary bytes out the
 * PS/2 port.  Each of the sequences in this table sends one nibble of the
 * register address or (write) data.  Different versions of the ALPS protocol
 * use slightly different encodings.
 */
struct alps_nibble_commands {
	int command;
	unsigned char data;
};

struct alps_bitmap_point {
	int start_bit;
	int num_bits;
};

/**
 * struct alps_fields - decoded version of the report packet
 * @x_map: Bitmap of active X positions for MT.
 * @y_map: Bitmap of active Y positions for MT.
 * @fingers: Number of fingers for MT.
 * @pressure: Pressure.
 * @st: position for ST.
 * @mt: position for MT.
 * @first_mp: Packet is the first of a multi-packet report.
 * @is_mp: Packet is part of a multi-packet report.
 * @left: Left touchpad button is active.
 * @right: Right touchpad button is active.
 * @middle: Middle touchpad button is active.
 * @ts_left: Left trackstick button is active.
 * @ts_right: Right trackstick button is active.
 * @ts_middle: Middle trackstick button is active.
 */
struct alps_fields {
	unsigned int x_map;
	unsigned int y_map;
	unsigned int fingers;

	int pressure;
	struct input_mt_pos st;
	struct input_mt_pos mt[MAX_TOUCHES];

	unsigned int first_mp:1;
	unsigned int is_mp:1;

	unsigned int left:1;
	unsigned int right:1;
	unsigned int middle:1;

	unsigned int ts_left:1;
	unsigned int ts_right:1;
	unsigned int ts_middle:1;
};

/**
 * struct alps_data - private data structure for the ALPS driver
 * @psmouse: Pointer to parent psmouse device
 * @dev2: Trackstick device (can be NULL).
 * @dev3: Generic PS/2 mouse (can be NULL, delayed registering).
 * @phys2: Physical path for the trackstick device.
 * @phys3: Physical path for the generic PS/2 mouse.
 * @dev3_register_work: Delayed work for registering PS/2 mouse.
 * @nibble_commands: Command mapping used for touchpad register accesses.
 * @addr_command: Command used to tell the touchpad that a register address
 *   follows.
 * @proto_version: Indicates V1/V2/V3/...
 * @byte0: Helps figure out whether a position report packet matches the
 *   known format for this model.  The first byte of the report, ANDed with
 *   mask0, should match byte0.
 * @mask0: The mask used to check the first byte of the report.
 * @fw_ver: cached copy of firmware version (EC report)
 * @flags: Additional device capabilities (passthrough port, trackstick, etc.).
 * @x_max: Largest possible X position value.
 * @y_max: Largest possible Y position value.
 * @x_bits: Number of X bits in the MT bitmap.
 * @y_bits: Number of Y bits in the MT bitmap.
 * @hw_init: Protocol-specific hardware init function.
 * @process_packet: Protocol-specific function to process a report packet.
 * @decode_fields: Protocol-specific function to read packet bitfields.
 * @set_abs_params: Protocol-specific function to configure the input_dev.
 * @prev_fin: Finger bit from previous packet.
 * @multi_packet: Multi-packet data in progress.
 * @multi_data: Saved multi-packet data.
 * @f: Decoded packet data fields.
 * @quirks: Bitmap of ALPS_QUIRK_*.
 * @timer: Timer for flushing out the final report packet in the stream.
 */
struct alps_data {
	struct psmouse *psmouse;
	struct input_dev *dev2;
	struct input_dev *dev3;
	char phys2[32];
	char phys3[32];
	struct delayed_work dev3_register_work;

	/* these are autodetected when the device is identified */
	const struct alps_nibble_commands *nibble_commands;
	int addr_command;
	u16 proto_version;
	u8 byte0, mask0;
	u8 fw_ver[3];
	int flags;
	int x_max;
	int y_max;
	int x_bits;
	int y_bits;
	unsigned int x_res;
	unsigned int y_res;

	int (*hw_init)(struct psmouse *psmouse);
	void (*process_packet)(struct psmouse *psmouse);
	int (*decode_fields)(struct alps_fields *f, unsigned char *p,
			      struct psmouse *psmouse);
	void (*set_abs_params)(struct alps_data *priv, struct input_dev *dev1);

	int prev_fin;
	int multi_packet;
	int second_touch;
	unsigned char multi_data[6];
	struct alps_fields f;
	u8 quirks;
	struct timer_list timer;
};

#define ALPS_QUIRK_TRACKSTICK_BUTTONS	1 /* trakcstick buttons in trackstick packet */

#ifdef CONFIG_MOUSE_PS2_ALPS
int alps_detect(struct psmouse *psmouse, bool set_properties);
int alps_init(struct psmouse *psmouse);
#else
inline int alps_detect(struct psmouse *psmouse, bool set_properties)
{
	return -ENOSYS;
}
inline int alps_init(struct psmouse *psmouse)
{
	return -ENOSYS;
}
#endif /* CONFIG_MOUSE_PS2_ALPS */

#endif

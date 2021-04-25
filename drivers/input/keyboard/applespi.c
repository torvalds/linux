// SPDX-License-Identifier: GPL-2.0
/*
 * MacBook (Pro) SPI keyboard and touchpad driver
 *
 * Copyright (c) 2015-2018 Federico Lorenzi
 * Copyright (c) 2017-2018 Ronald Tschalär
 */

/*
 * The keyboard and touchpad controller on the MacBookAir6, MacBookPro12,
 * MacBook8 and newer can be driven either by USB or SPI. However the USB
 * pins are only connected on the MacBookAir6 and 7 and the MacBookPro12.
 * All others need this driver. The interface is selected using ACPI methods:
 *
 * * UIEN ("USB Interface Enable"): If invoked with argument 1, disables SPI
 *   and enables USB. If invoked with argument 0, disables USB.
 * * UIST ("USB Interface Status"): Returns 1 if USB is enabled, 0 otherwise.
 * * SIEN ("SPI Interface Enable"): If invoked with argument 1, disables USB
 *   and enables SPI. If invoked with argument 0, disables SPI.
 * * SIST ("SPI Interface Status"): Returns 1 if SPI is enabled, 0 otherwise.
 * * ISOL: Resets the four GPIO pins used for SPI. Intended to be invoked with
 *   argument 1, then once more with argument 0.
 *
 * UIEN and UIST are only provided on models where the USB pins are connected.
 *
 * SPI-based Protocol
 * ------------------
 *
 * The device and driver exchange messages (struct message); each message is
 * encapsulated in one or more packets (struct spi_packet). There are two types
 * of exchanges: reads, and writes. A read is signaled by a GPE, upon which one
 * message can be read from the device. A write exchange consists of writing a
 * command message, immediately reading a short status packet, and then, upon
 * receiving a GPE, reading the response message. Write exchanges cannot be
 * interleaved, i.e. a new write exchange must not be started till the previous
 * write exchange is complete. Whether a received message is part of a read or
 * write exchange is indicated in the encapsulating packet's flags field.
 *
 * A single message may be too large to fit in a single packet (which has a
 * fixed, 256-byte size). In that case it will be split over multiple,
 * consecutive packets.
 */

#include <linux/acpi.h>
#include <linux/crc16.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/efi.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/ktime.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/spi/spi.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

#include <asm/barrier.h>
#include <asm/unaligned.h>

#define CREATE_TRACE_POINTS
#include "applespi.h"
#include "applespi_trace.h"

#define APPLESPI_PACKET_SIZE	256
#define APPLESPI_STATUS_SIZE	4

#define PACKET_TYPE_READ	0x20
#define PACKET_TYPE_WRITE	0x40
#define PACKET_DEV_KEYB		0x01
#define PACKET_DEV_TPAD		0x02
#define PACKET_DEV_INFO		0xd0

#define MAX_ROLLOVER		6

#define MAX_FINGERS		11
#define MAX_FINGER_ORIENTATION	16384
#define MAX_PKTS_PER_MSG	2

#define KBD_BL_LEVEL_MIN	32U
#define KBD_BL_LEVEL_MAX	255U
#define KBD_BL_LEVEL_SCALE	1000000U
#define KBD_BL_LEVEL_ADJ	\
	((KBD_BL_LEVEL_MAX - KBD_BL_LEVEL_MIN) * KBD_BL_LEVEL_SCALE / 255U)

#define EFI_BL_LEVEL_NAME	L"KeyboardBacklightLevel"
#define EFI_BL_LEVEL_GUID	EFI_GUID(0xa076d2af, 0x9678, 0x4386, 0x8b, 0x58, 0x1f, 0xc8, 0xef, 0x04, 0x16, 0x19)

#define APPLE_FLAG_FKEY		0x01

#define SPI_RW_CHG_DELAY_US	100	/* from experimentation, in µs */

#define SYNAPTICS_VENDOR_ID	0x06cb

static unsigned int fnmode = 1;
module_param(fnmode, uint, 0644);
MODULE_PARM_DESC(fnmode, "Mode of Fn key on Apple keyboards (0 = disabled, [1] = fkeyslast, 2 = fkeysfirst)");

static unsigned int fnremap;
module_param(fnremap, uint, 0644);
MODULE_PARM_DESC(fnremap, "Remap Fn key ([0] = no-remap; 1 = left-ctrl, 2 = left-shift, 3 = left-alt, 4 = left-meta, 6 = right-shift, 7 = right-alt, 8 = right-meta)");

static bool iso_layout;
module_param(iso_layout, bool, 0644);
MODULE_PARM_DESC(iso_layout, "Enable/Disable hardcoded ISO-layout of the keyboard. ([0] = disabled, 1 = enabled)");

static char touchpad_dimensions[40];
module_param_string(touchpad_dimensions, touchpad_dimensions,
		    sizeof(touchpad_dimensions), 0444);
MODULE_PARM_DESC(touchpad_dimensions, "The pixel dimensions of the touchpad, as XxY+W+H .");

/**
 * struct keyboard_protocol - keyboard message.
 * message.type = 0x0110, message.length = 0x000a
 *
 * @unknown1:		unknown
 * @modifiers:		bit-set of modifier/control keys pressed
 * @unknown2:		unknown
 * @keys_pressed:	the (non-modifier) keys currently pressed
 * @fn_pressed:		whether the fn key is currently pressed
 * @crc16:		crc over the whole message struct (message header +
 *			this struct) minus this @crc16 field
 */
struct keyboard_protocol {
	u8			unknown1;
	u8			modifiers;
	u8			unknown2;
	u8			keys_pressed[MAX_ROLLOVER];
	u8			fn_pressed;
	__le16			crc16;
};

/**
 * struct tp_finger - single trackpad finger structure, le16-aligned
 *
 * @origin:		zero when switching track finger
 * @abs_x:		absolute x coordinate
 * @abs_y:		absolute y coordinate
 * @rel_x:		relative x coordinate
 * @rel_y:		relative y coordinate
 * @tool_major:		tool area, major axis
 * @tool_minor:		tool area, minor axis
 * @orientation:	16384 when point, else 15 bit angle
 * @touch_major:	touch area, major axis
 * @touch_minor:	touch area, minor axis
 * @unused:		zeros
 * @pressure:		pressure on forcetouch touchpad
 * @multi:		one finger: varies, more fingers: constant
 * @crc16:		on last finger: crc over the whole message struct
 *			(i.e. message header + this struct) minus the last
 *			@crc16 field; unknown on all other fingers.
 */
struct tp_finger {
	__le16 origin;
	__le16 abs_x;
	__le16 abs_y;
	__le16 rel_x;
	__le16 rel_y;
	__le16 tool_major;
	__le16 tool_minor;
	__le16 orientation;
	__le16 touch_major;
	__le16 touch_minor;
	__le16 unused[2];
	__le16 pressure;
	__le16 multi;
	__le16 crc16;
};

/**
 * struct touchpad_protocol - touchpad message.
 * message.type = 0x0210
 *
 * @unknown1:		unknown
 * @clicked:		1 if a button-click was detected, 0 otherwise
 * @unknown2:		unknown
 * @number_of_fingers:	the number of fingers being reported in @fingers
 * @clicked2:		same as @clicked
 * @unknown3:		unknown
 * @fingers:		the data for each finger
 */
struct touchpad_protocol {
	u8			unknown1[1];
	u8			clicked;
	u8			unknown2[28];
	u8			number_of_fingers;
	u8			clicked2;
	u8			unknown3[16];
	struct tp_finger	fingers[];
};

/**
 * struct command_protocol_tp_info - get touchpad info.
 * message.type = 0x1020, message.length = 0x0000
 *
 * @crc16:		crc over the whole message struct (message header +
 *			this struct) minus this @crc16 field
 */
struct command_protocol_tp_info {
	__le16			crc16;
};

/**
 * struct touchpad_info - touchpad info response.
 * message.type = 0x1020, message.length = 0x006e
 *
 * @unknown1:		unknown
 * @model_flags:	flags (vary by model number, but significance otherwise
 *			unknown)
 * @model_no:		the touchpad model number
 * @unknown2:		unknown
 * @crc16:		crc over the whole message struct (message header +
 *			this struct) minus this @crc16 field
 */
struct touchpad_info_protocol {
	u8			unknown1[105];
	u8			model_flags;
	u8			model_no;
	u8			unknown2[3];
	__le16			crc16;
};

/**
 * struct command_protocol_mt_init - initialize multitouch.
 * message.type = 0x0252, message.length = 0x0002
 *
 * @cmd:		value: 0x0102
 * @crc16:		crc over the whole message struct (message header +
 *			this struct) minus this @crc16 field
 */
struct command_protocol_mt_init {
	__le16			cmd;
	__le16			crc16;
};

/**
 * struct command_protocol_capsl - toggle caps-lock led
 * message.type = 0x0151, message.length = 0x0002
 *
 * @unknown:		value: 0x01 (length?)
 * @led:		0 off, 2 on
 * @crc16:		crc over the whole message struct (message header +
 *			this struct) minus this @crc16 field
 */
struct command_protocol_capsl {
	u8			unknown;
	u8			led;
	__le16			crc16;
};

/**
 * struct command_protocol_bl - set keyboard backlight brightness
 * message.type = 0xB051, message.length = 0x0006
 *
 * @const1:		value: 0x01B0
 * @level:		the brightness level to set
 * @const2:		value: 0x0001 (backlight off), 0x01F4 (backlight on)
 * @crc16:		crc over the whole message struct (message header +
 *			this struct) minus this @crc16 field
 */
struct command_protocol_bl {
	__le16			const1;
	__le16			level;
	__le16			const2;
	__le16			crc16;
};

/**
 * struct message - a complete spi message.
 *
 * Each message begins with fixed header, followed by a message-type specific
 * payload, and ends with a 16-bit crc. Because of the varying lengths of the
 * payload, the crc is defined at the end of each payload struct, rather than
 * in this struct.
 *
 * @type:	the message type
 * @zero:	always 0
 * @counter:	incremented on each message, rolls over after 255; there is a
 *		separate counter for each message type.
 * @rsp_buf_len:response buffer length (the exact nature of this field is quite
 *		speculative). On a request/write this is often the same as
 *		@length, though in some cases it has been seen to be much larger
 *		(e.g. 0x400); on a response/read this the same as on the
 *		request; for reads that are not responses it is 0.
 * @length:	length of the remainder of the data in the whole message
 *		structure (after re-assembly in case of being split over
 *		multiple spi-packets), minus the trailing crc. The total size
 *		of the message struct is therefore @length + 10.
 */
struct message {
	__le16		type;
	u8		zero;
	u8		counter;
	__le16		rsp_buf_len;
	__le16		length;
	union {
		struct keyboard_protocol	keyboard;
		struct touchpad_protocol	touchpad;
		struct touchpad_info_protocol	tp_info;
		struct command_protocol_tp_info	tp_info_command;
		struct command_protocol_mt_init	init_mt_command;
		struct command_protocol_capsl	capsl_command;
		struct command_protocol_bl	bl_command;
		u8				data[0];
	};
};

/* type + zero + counter + rsp_buf_len + length */
#define MSG_HEADER_SIZE		8

/**
 * struct spi_packet - a complete spi packet; always 256 bytes. This carries
 * the (parts of the) message in the data. But note that this does not
 * necessarily contain a complete message, as in some cases (e.g. many
 * fingers pressed) the message is split over multiple packets (see the
 * @offset, @remaining, and @length fields). In general the data parts in
 * spi_packet's are concatenated until @remaining is 0, and the result is an
 * message.
 *
 * @flags:	0x40 = write (to device), 0x20 = read (from device); note that
 *		the response to a write still has 0x40.
 * @device:	1 = keyboard, 2 = touchpad
 * @offset:	specifies the offset of this packet's data in the complete
 *		message; i.e. > 0 indicates this is a continuation packet (in
 *		the second packet for a message split over multiple packets
 *		this would then be the same as the @length in the first packet)
 * @remaining:	number of message bytes remaining in subsequents packets (in
 *		the first packet of a message split over two packets this would
 *		then be the same as the @length in the second packet)
 * @length:	length of the valid data in the @data in this packet
 * @data:	all or part of a message
 * @crc16:	crc over this whole structure minus this @crc16 field. This
 *		covers just this packet, even on multi-packet messages (in
 *		contrast to the crc in the message).
 */
struct spi_packet {
	u8			flags;
	u8			device;
	__le16			offset;
	__le16			remaining;
	__le16			length;
	u8			data[246];
	__le16			crc16;
};

struct spi_settings {
	u64	spi_cs_delay;		/* cs-to-clk delay in us */
	u64	reset_a2r_usec;		/* active-to-receive delay? */
	u64	reset_rec_usec;		/* ? (cur val: 10) */
};

/* this mimics struct drm_rect */
struct applespi_tp_info {
	int	x_min;
	int	y_min;
	int	x_max;
	int	y_max;
};

struct applespi_data {
	struct spi_device		*spi;
	struct spi_settings		spi_settings;
	struct input_dev		*keyboard_input_dev;
	struct input_dev		*touchpad_input_dev;

	u8				*tx_buffer;
	u8				*tx_status;
	u8				*rx_buffer;

	u8				*msg_buf;
	unsigned int			saved_msg_len;

	struct applespi_tp_info		tp_info;

	u8				last_keys_pressed[MAX_ROLLOVER];
	u8				last_keys_fn_pressed[MAX_ROLLOVER];
	u8				last_fn_pressed;
	struct input_mt_pos		pos[MAX_FINGERS];
	int				slots[MAX_FINGERS];
	int				gpe;
	acpi_handle			sien;
	acpi_handle			sist;

	struct spi_transfer		dl_t;
	struct spi_transfer		rd_t;
	struct spi_message		rd_m;

	struct spi_transfer		ww_t;
	struct spi_transfer		wd_t;
	struct spi_transfer		wr_t;
	struct spi_transfer		st_t;
	struct spi_message		wr_m;

	bool				want_tp_info_cmd;
	bool				want_mt_init_cmd;
	bool				want_cl_led_on;
	bool				have_cl_led_on;
	unsigned int			want_bl_level;
	unsigned int			have_bl_level;
	unsigned int			cmd_msg_cntr;
	/* lock to protect the above parameters and flags below */
	spinlock_t			cmd_msg_lock;
	ktime_t				cmd_msg_queued;
	enum applespi_evt_type		cmd_evt_type;

	struct led_classdev		backlight_info;

	bool				suspended;
	bool				drain;
	wait_queue_head_t		drain_complete;
	bool				read_active;
	bool				write_active;

	struct work_struct		work;
	struct touchpad_info_protocol	rcvd_tp_info;

	struct dentry			*debugfs_root;
	bool				debug_tp_dim;
	char				tp_dim_val[40];
	int				tp_dim_min_x;
	int				tp_dim_max_x;
	int				tp_dim_min_y;
	int				tp_dim_max_y;
};

static const unsigned char applespi_scancodes[] = {
	0, 0, 0, 0,
	KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J,
	KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T,
	KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z,
	KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9, KEY_0,
	KEY_ENTER, KEY_ESC, KEY_BACKSPACE, KEY_TAB, KEY_SPACE, KEY_MINUS,
	KEY_EQUAL, KEY_LEFTBRACE, KEY_RIGHTBRACE, KEY_BACKSLASH, 0,
	KEY_SEMICOLON, KEY_APOSTROPHE, KEY_GRAVE, KEY_COMMA, KEY_DOT, KEY_SLASH,
	KEY_CAPSLOCK,
	KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8, KEY_F9,
	KEY_F10, KEY_F11, KEY_F12, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	KEY_RIGHT, KEY_LEFT, KEY_DOWN, KEY_UP,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, KEY_102ND,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, KEY_RO, 0, KEY_YEN, 0, 0, 0, 0, 0,
	0, KEY_KATAKANAHIRAGANA, KEY_MUHENKAN
};

/*
 * This must have exactly as many entries as there are bits in
 * struct keyboard_protocol.modifiers .
 */
static const unsigned char applespi_controlcodes[] = {
	KEY_LEFTCTRL,
	KEY_LEFTSHIFT,
	KEY_LEFTALT,
	KEY_LEFTMETA,
	0,
	KEY_RIGHTSHIFT,
	KEY_RIGHTALT,
	KEY_RIGHTMETA
};

struct applespi_key_translation {
	u16 from;
	u16 to;
	u8 flags;
};

static const struct applespi_key_translation applespi_fn_codes[] = {
	{ KEY_BACKSPACE, KEY_DELETE },
	{ KEY_ENTER,	KEY_INSERT },
	{ KEY_F1,	KEY_BRIGHTNESSDOWN,	APPLE_FLAG_FKEY },
	{ KEY_F2,	KEY_BRIGHTNESSUP,	APPLE_FLAG_FKEY },
	{ KEY_F3,	KEY_SCALE,		APPLE_FLAG_FKEY },
	{ KEY_F4,	KEY_DASHBOARD,		APPLE_FLAG_FKEY },
	{ KEY_F5,	KEY_KBDILLUMDOWN,	APPLE_FLAG_FKEY },
	{ KEY_F6,	KEY_KBDILLUMUP,		APPLE_FLAG_FKEY },
	{ KEY_F7,	KEY_PREVIOUSSONG,	APPLE_FLAG_FKEY },
	{ KEY_F8,	KEY_PLAYPAUSE,		APPLE_FLAG_FKEY },
	{ KEY_F9,	KEY_NEXTSONG,		APPLE_FLAG_FKEY },
	{ KEY_F10,	KEY_MUTE,		APPLE_FLAG_FKEY },
	{ KEY_F11,	KEY_VOLUMEDOWN,		APPLE_FLAG_FKEY },
	{ KEY_F12,	KEY_VOLUMEUP,		APPLE_FLAG_FKEY },
	{ KEY_RIGHT,	KEY_END },
	{ KEY_LEFT,	KEY_HOME },
	{ KEY_DOWN,	KEY_PAGEDOWN },
	{ KEY_UP,	KEY_PAGEUP },
	{ }
};

static const struct applespi_key_translation apple_iso_keyboard[] = {
	{ KEY_GRAVE,	KEY_102ND },
	{ KEY_102ND,	KEY_GRAVE },
	{ }
};

struct applespi_tp_model_info {
	u16			model;
	struct applespi_tp_info	tp_info;
};

static const struct applespi_tp_model_info applespi_tp_models[] = {
	{
		.model = 0x04,	/* MB8 MB9 MB10 */
		.tp_info = { -5087, -182, 5579, 6089 },
	},
	{
		.model = 0x05,	/* MBP13,1 MBP13,2 MBP14,1 MBP14,2 */
		.tp_info = { -6243, -170, 6749, 7685 },
	},
	{
		.model = 0x06,	/* MBP13,3 MBP14,3 */
		.tp_info = { -7456, -163, 7976, 9283 },
	},
	{}
};

typedef void (*applespi_trace_fun)(enum applespi_evt_type,
				   enum applespi_pkt_type, u8 *, size_t);

static applespi_trace_fun applespi_get_trace_fun(enum applespi_evt_type type)
{
	switch (type) {
	case ET_CMD_TP_INI:
		return trace_applespi_tp_ini_cmd;
	case ET_CMD_BL:
		return trace_applespi_backlight_cmd;
	case ET_CMD_CL:
		return trace_applespi_caps_lock_cmd;
	case ET_RD_KEYB:
		return trace_applespi_keyboard_data;
	case ET_RD_TPAD:
		return trace_applespi_touchpad_data;
	case ET_RD_UNKN:
		return trace_applespi_unknown_data;
	default:
		WARN_ONCE(1, "Unknown msg type %d", type);
		return trace_applespi_unknown_data;
	}
}

static void applespi_setup_read_txfrs(struct applespi_data *applespi)
{
	struct spi_message *msg = &applespi->rd_m;
	struct spi_transfer *dl_t = &applespi->dl_t;
	struct spi_transfer *rd_t = &applespi->rd_t;

	memset(dl_t, 0, sizeof(*dl_t));
	memset(rd_t, 0, sizeof(*rd_t));

	dl_t->delay_usecs = applespi->spi_settings.spi_cs_delay;

	rd_t->rx_buf = applespi->rx_buffer;
	rd_t->len = APPLESPI_PACKET_SIZE;

	spi_message_init(msg);
	spi_message_add_tail(dl_t, msg);
	spi_message_add_tail(rd_t, msg);
}

static void applespi_setup_write_txfrs(struct applespi_data *applespi)
{
	struct spi_message *msg = &applespi->wr_m;
	struct spi_transfer *wt_t = &applespi->ww_t;
	struct spi_transfer *dl_t = &applespi->wd_t;
	struct spi_transfer *wr_t = &applespi->wr_t;
	struct spi_transfer *st_t = &applespi->st_t;

	memset(wt_t, 0, sizeof(*wt_t));
	memset(dl_t, 0, sizeof(*dl_t));
	memset(wr_t, 0, sizeof(*wr_t));
	memset(st_t, 0, sizeof(*st_t));

	/*
	 * All we need here is a delay at the beginning of the message before
	 * asserting cs. But the current spi API doesn't support this, so we
	 * end up with an extra unnecessary (but harmless) cs assertion and
	 * deassertion.
	 */
	wt_t->delay_usecs = SPI_RW_CHG_DELAY_US;
	wt_t->cs_change = 1;

	dl_t->delay_usecs = applespi->spi_settings.spi_cs_delay;

	wr_t->tx_buf = applespi->tx_buffer;
	wr_t->len = APPLESPI_PACKET_SIZE;
	wr_t->delay_usecs = SPI_RW_CHG_DELAY_US;

	st_t->rx_buf = applespi->tx_status;
	st_t->len = APPLESPI_STATUS_SIZE;

	spi_message_init(msg);
	spi_message_add_tail(wt_t, msg);
	spi_message_add_tail(dl_t, msg);
	spi_message_add_tail(wr_t, msg);
	spi_message_add_tail(st_t, msg);
}

static int applespi_async(struct applespi_data *applespi,
			  struct spi_message *message, void (*complete)(void *))
{
	message->complete = complete;
	message->context = applespi;

	return spi_async(applespi->spi, message);
}

static inline bool applespi_check_write_status(struct applespi_data *applespi,
					       int sts)
{
	static u8 status_ok[] = { 0xac, 0x27, 0x68, 0xd5 };

	if (sts < 0) {
		dev_warn(&applespi->spi->dev, "Error writing to device: %d\n",
			 sts);
		return false;
	}

	if (memcmp(applespi->tx_status, status_ok, APPLESPI_STATUS_SIZE)) {
		dev_warn(&applespi->spi->dev, "Error writing to device: %*ph\n",
			 APPLESPI_STATUS_SIZE, applespi->tx_status);
		return false;
	}

	return true;
}

static int applespi_get_spi_settings(struct applespi_data *applespi)
{
	struct acpi_device *adev = ACPI_COMPANION(&applespi->spi->dev);
	const union acpi_object *o;
	struct spi_settings *settings = &applespi->spi_settings;

	if (!acpi_dev_get_property(adev, "spiCSDelay", ACPI_TYPE_BUFFER, &o))
		settings->spi_cs_delay = *(u64 *)o->buffer.pointer;
	else
		dev_warn(&applespi->spi->dev,
			 "Property spiCSDelay not found\n");

	if (!acpi_dev_get_property(adev, "resetA2RUsec", ACPI_TYPE_BUFFER, &o))
		settings->reset_a2r_usec = *(u64 *)o->buffer.pointer;
	else
		dev_warn(&applespi->spi->dev,
			 "Property resetA2RUsec not found\n");

	if (!acpi_dev_get_property(adev, "resetRecUsec", ACPI_TYPE_BUFFER, &o))
		settings->reset_rec_usec = *(u64 *)o->buffer.pointer;
	else
		dev_warn(&applespi->spi->dev,
			 "Property resetRecUsec not found\n");

	dev_dbg(&applespi->spi->dev,
		"SPI settings: spi_cs_delay=%llu reset_a2r_usec=%llu reset_rec_usec=%llu\n",
		settings->spi_cs_delay, settings->reset_a2r_usec,
		settings->reset_rec_usec);

	return 0;
}

static int applespi_setup_spi(struct applespi_data *applespi)
{
	int sts;

	sts = applespi_get_spi_settings(applespi);
	if (sts)
		return sts;

	spin_lock_init(&applespi->cmd_msg_lock);
	init_waitqueue_head(&applespi->drain_complete);

	return 0;
}

static int applespi_enable_spi(struct applespi_data *applespi)
{
	acpi_status acpi_sts;
	unsigned long long spi_status;

	/* check if SPI is already enabled, so we can skip the delay below */
	acpi_sts = acpi_evaluate_integer(applespi->sist, NULL, NULL,
					 &spi_status);
	if (ACPI_SUCCESS(acpi_sts) && spi_status)
		return 0;

	/* SIEN(1) will enable SPI communication */
	acpi_sts = acpi_execute_simple_method(applespi->sien, NULL, 1);
	if (ACPI_FAILURE(acpi_sts)) {
		dev_err(&applespi->spi->dev, "SIEN failed: %s\n",
			acpi_format_exception(acpi_sts));
		return -ENODEV;
	}

	/*
	 * Allow the SPI interface to come up before returning. Without this
	 * delay, the SPI commands to enable multitouch mode may not reach
	 * the trackpad controller, causing pointer movement to break upon
	 * resume from sleep.
	 */
	msleep(50);

	return 0;
}

static int applespi_send_cmd_msg(struct applespi_data *applespi);

static void applespi_msg_complete(struct applespi_data *applespi,
				  bool is_write_msg, bool is_read_compl)
{
	unsigned long flags;

	spin_lock_irqsave(&applespi->cmd_msg_lock, flags);

	if (is_read_compl)
		applespi->read_active = false;
	if (is_write_msg)
		applespi->write_active = false;

	if (applespi->drain && !applespi->write_active)
		wake_up_all(&applespi->drain_complete);

	if (is_write_msg) {
		applespi->cmd_msg_queued = 0;
		applespi_send_cmd_msg(applespi);
	}

	spin_unlock_irqrestore(&applespi->cmd_msg_lock, flags);
}

static void applespi_async_write_complete(void *context)
{
	struct applespi_data *applespi = context;
	enum applespi_evt_type evt_type = applespi->cmd_evt_type;

	applespi_get_trace_fun(evt_type)(evt_type, PT_WRITE,
					 applespi->tx_buffer,
					 APPLESPI_PACKET_SIZE);
	applespi_get_trace_fun(evt_type)(evt_type, PT_STATUS,
					 applespi->tx_status,
					 APPLESPI_STATUS_SIZE);

	if (!applespi_check_write_status(applespi, applespi->wr_m.status)) {
		/*
		 * If we got an error, we presumably won't get the expected
		 * response message either.
		 */
		applespi_msg_complete(applespi, true, false);
	}
}

static int applespi_send_cmd_msg(struct applespi_data *applespi)
{
	u16 crc;
	int sts;
	struct spi_packet *packet = (struct spi_packet *)applespi->tx_buffer;
	struct message *message = (struct message *)packet->data;
	u16 msg_len;
	u8 device;

	/* check if draining */
	if (applespi->drain)
		return 0;

	/* check whether send is in progress */
	if (applespi->cmd_msg_queued) {
		if (ktime_ms_delta(ktime_get(), applespi->cmd_msg_queued) < 1000)
			return 0;

		dev_warn(&applespi->spi->dev, "Command %d timed out\n",
			 applespi->cmd_evt_type);

		applespi->cmd_msg_queued = 0;
		applespi->write_active = false;
	}

	/* set up packet */
	memset(packet, 0, APPLESPI_PACKET_SIZE);

	/* are we processing init commands? */
	if (applespi->want_tp_info_cmd) {
		applespi->want_tp_info_cmd = false;
		applespi->want_mt_init_cmd = true;
		applespi->cmd_evt_type = ET_CMD_TP_INI;

		/* build init command */
		device = PACKET_DEV_INFO;

		message->type = cpu_to_le16(0x1020);
		msg_len = sizeof(message->tp_info_command);

		message->zero = 0x02;
		message->rsp_buf_len = cpu_to_le16(0x0200);

	} else if (applespi->want_mt_init_cmd) {
		applespi->want_mt_init_cmd = false;
		applespi->cmd_evt_type = ET_CMD_TP_INI;

		/* build init command */
		device = PACKET_DEV_TPAD;

		message->type = cpu_to_le16(0x0252);
		msg_len = sizeof(message->init_mt_command);

		message->init_mt_command.cmd = cpu_to_le16(0x0102);

	/* do we need caps-lock command? */
	} else if (applespi->want_cl_led_on != applespi->have_cl_led_on) {
		applespi->have_cl_led_on = applespi->want_cl_led_on;
		applespi->cmd_evt_type = ET_CMD_CL;

		/* build led command */
		device = PACKET_DEV_KEYB;

		message->type = cpu_to_le16(0x0151);
		msg_len = sizeof(message->capsl_command);

		message->capsl_command.unknown = 0x01;
		message->capsl_command.led = applespi->have_cl_led_on ? 2 : 0;

	/* do we need backlight command? */
	} else if (applespi->want_bl_level != applespi->have_bl_level) {
		applespi->have_bl_level = applespi->want_bl_level;
		applespi->cmd_evt_type = ET_CMD_BL;

		/* build command buffer */
		device = PACKET_DEV_KEYB;

		message->type = cpu_to_le16(0xB051);
		msg_len = sizeof(message->bl_command);

		message->bl_command.const1 = cpu_to_le16(0x01B0);
		message->bl_command.level =
				cpu_to_le16(applespi->have_bl_level);

		if (applespi->have_bl_level > 0)
			message->bl_command.const2 = cpu_to_le16(0x01F4);
		else
			message->bl_command.const2 = cpu_to_le16(0x0001);

	/* everything's up-to-date */
	} else {
		return 0;
	}

	/* finalize packet */
	packet->flags = PACKET_TYPE_WRITE;
	packet->device = device;
	packet->length = cpu_to_le16(MSG_HEADER_SIZE + msg_len);

	message->counter = applespi->cmd_msg_cntr++ % (U8_MAX + 1);

	message->length = cpu_to_le16(msg_len - 2);
	if (!message->rsp_buf_len)
		message->rsp_buf_len = message->length;

	crc = crc16(0, (u8 *)message, le16_to_cpu(packet->length) - 2);
	put_unaligned_le16(crc, &message->data[msg_len - 2]);

	crc = crc16(0, (u8 *)packet, sizeof(*packet) - 2);
	packet->crc16 = cpu_to_le16(crc);

	/* send command */
	sts = applespi_async(applespi, &applespi->wr_m,
			     applespi_async_write_complete);
	if (sts) {
		dev_warn(&applespi->spi->dev,
			 "Error queueing async write to device: %d\n", sts);
		return sts;
	}

	applespi->cmd_msg_queued = ktime_get_coarse();
	applespi->write_active = true;

	return 0;
}

static void applespi_init(struct applespi_data *applespi, bool is_resume)
{
	unsigned long flags;

	spin_lock_irqsave(&applespi->cmd_msg_lock, flags);

	if (is_resume)
		applespi->want_mt_init_cmd = true;
	else
		applespi->want_tp_info_cmd = true;
	applespi_send_cmd_msg(applespi);

	spin_unlock_irqrestore(&applespi->cmd_msg_lock, flags);
}

static int applespi_set_capsl_led(struct applespi_data *applespi,
				  bool capslock_on)
{
	unsigned long flags;
	int sts;

	spin_lock_irqsave(&applespi->cmd_msg_lock, flags);

	applespi->want_cl_led_on = capslock_on;
	sts = applespi_send_cmd_msg(applespi);

	spin_unlock_irqrestore(&applespi->cmd_msg_lock, flags);

	return sts;
}

static void applespi_set_bl_level(struct led_classdev *led_cdev,
				  enum led_brightness value)
{
	struct applespi_data *applespi =
		container_of(led_cdev, struct applespi_data, backlight_info);
	unsigned long flags;

	spin_lock_irqsave(&applespi->cmd_msg_lock, flags);

	if (value == 0) {
		applespi->want_bl_level = value;
	} else {
		/*
		 * The backlight does not turn on till level 32, so we scale
		 * the range here so that from a user's perspective it turns
		 * on at 1.
		 */
		applespi->want_bl_level =
			((value * KBD_BL_LEVEL_ADJ) / KBD_BL_LEVEL_SCALE +
			 KBD_BL_LEVEL_MIN);
	}

	applespi_send_cmd_msg(applespi);

	spin_unlock_irqrestore(&applespi->cmd_msg_lock, flags);
}

static int applespi_event(struct input_dev *dev, unsigned int type,
			  unsigned int code, int value)
{
	struct applespi_data *applespi = input_get_drvdata(dev);

	switch (type) {
	case EV_LED:
		applespi_set_capsl_led(applespi, !!test_bit(LED_CAPSL, dev->led));
		return 0;
	}

	return -EINVAL;
}

/* lifted from the BCM5974 driver and renamed from raw2int */
/* convert 16-bit little endian to signed integer */
static inline int le16_to_int(__le16 x)
{
	return (signed short)le16_to_cpu(x);
}

static void applespi_debug_update_dimensions(struct applespi_data *applespi,
					     const struct tp_finger *f)
{
	applespi->tp_dim_min_x = min(applespi->tp_dim_min_x,
				     le16_to_int(f->abs_x));
	applespi->tp_dim_max_x = max(applespi->tp_dim_max_x,
				     le16_to_int(f->abs_x));
	applespi->tp_dim_min_y = min(applespi->tp_dim_min_y,
				     le16_to_int(f->abs_y));
	applespi->tp_dim_max_y = max(applespi->tp_dim_max_y,
				     le16_to_int(f->abs_y));
}

static int applespi_tp_dim_open(struct inode *inode, struct file *file)
{
	struct applespi_data *applespi = inode->i_private;

	file->private_data = applespi;

	snprintf(applespi->tp_dim_val, sizeof(applespi->tp_dim_val),
		 "0x%.4x %dx%d+%u+%u\n",
		 applespi->touchpad_input_dev->id.product,
		 applespi->tp_dim_min_x, applespi->tp_dim_min_y,
		 applespi->tp_dim_max_x - applespi->tp_dim_min_x,
		 applespi->tp_dim_max_y - applespi->tp_dim_min_y);

	return nonseekable_open(inode, file);
}

static ssize_t applespi_tp_dim_read(struct file *file, char __user *buf,
				    size_t len, loff_t *off)
{
	struct applespi_data *applespi = file->private_data;

	return simple_read_from_buffer(buf, len, off, applespi->tp_dim_val,
				       strlen(applespi->tp_dim_val));
}

static const struct file_operations applespi_tp_dim_fops = {
	.owner = THIS_MODULE,
	.open = applespi_tp_dim_open,
	.read = applespi_tp_dim_read,
	.llseek = no_llseek,
};

static void report_finger_data(struct input_dev *input, int slot,
			       const struct input_mt_pos *pos,
			       const struct tp_finger *f)
{
	input_mt_slot(input, slot);
	input_mt_report_slot_state(input, MT_TOOL_FINGER, true);

	input_report_abs(input, ABS_MT_TOUCH_MAJOR,
			 le16_to_int(f->touch_major) << 1);
	input_report_abs(input, ABS_MT_TOUCH_MINOR,
			 le16_to_int(f->touch_minor) << 1);
	input_report_abs(input, ABS_MT_WIDTH_MAJOR,
			 le16_to_int(f->tool_major) << 1);
	input_report_abs(input, ABS_MT_WIDTH_MINOR,
			 le16_to_int(f->tool_minor) << 1);
	input_report_abs(input, ABS_MT_ORIENTATION,
			 MAX_FINGER_ORIENTATION - le16_to_int(f->orientation));
	input_report_abs(input, ABS_MT_POSITION_X, pos->x);
	input_report_abs(input, ABS_MT_POSITION_Y, pos->y);
}

static void report_tp_state(struct applespi_data *applespi,
			    struct touchpad_protocol *t)
{
	const struct tp_finger *f;
	struct input_dev *input;
	const struct applespi_tp_info *tp_info = &applespi->tp_info;
	int i, n;

	/* touchpad_input_dev is set async in worker */
	input = smp_load_acquire(&applespi->touchpad_input_dev);
	if (!input)
		return;	/* touchpad isn't initialized yet */

	n = 0;

	for (i = 0; i < t->number_of_fingers; i++) {
		f = &t->fingers[i];
		if (le16_to_int(f->touch_major) == 0)
			continue;
		applespi->pos[n].x = le16_to_int(f->abs_x);
		applespi->pos[n].y = tp_info->y_min + tp_info->y_max -
				     le16_to_int(f->abs_y);
		n++;

		if (applespi->debug_tp_dim)
			applespi_debug_update_dimensions(applespi, f);
	}

	input_mt_assign_slots(input, applespi->slots, applespi->pos, n, 0);

	for (i = 0; i < n; i++)
		report_finger_data(input, applespi->slots[i],
				   &applespi->pos[i], &t->fingers[i]);

	input_mt_sync_frame(input);
	input_report_key(input, BTN_LEFT, t->clicked);

	input_sync(input);
}

static const struct applespi_key_translation *
applespi_find_translation(const struct applespi_key_translation *table, u16 key)
{
	const struct applespi_key_translation *trans;

	for (trans = table; trans->from; trans++)
		if (trans->from == key)
			return trans;

	return NULL;
}

static unsigned int applespi_translate_fn_key(unsigned int key, int fn_pressed)
{
	const struct applespi_key_translation *trans;
	int do_translate;

	trans = applespi_find_translation(applespi_fn_codes, key);
	if (trans) {
		if (trans->flags & APPLE_FLAG_FKEY)
			do_translate = (fnmode == 2 && fn_pressed) ||
				       (fnmode == 1 && !fn_pressed);
		else
			do_translate = fn_pressed;

		if (do_translate)
			key = trans->to;
	}

	return key;
}

static unsigned int applespi_translate_iso_layout(unsigned int key)
{
	const struct applespi_key_translation *trans;

	trans = applespi_find_translation(apple_iso_keyboard, key);
	if (trans)
		key = trans->to;

	return key;
}

static unsigned int applespi_code_to_key(u8 code, int fn_pressed)
{
	unsigned int key = applespi_scancodes[code];

	if (fnmode)
		key = applespi_translate_fn_key(key, fn_pressed);
	if (iso_layout)
		key = applespi_translate_iso_layout(key);
	return key;
}

static void
applespi_remap_fn_key(struct keyboard_protocol *keyboard_protocol)
{
	unsigned char tmp;
	u8 bit = BIT((fnremap - 1) & 0x07);

	if (!fnremap || fnremap > ARRAY_SIZE(applespi_controlcodes) ||
	    !applespi_controlcodes[fnremap - 1])
		return;

	tmp = keyboard_protocol->fn_pressed;
	keyboard_protocol->fn_pressed = !!(keyboard_protocol->modifiers & bit);
	if (tmp)
		keyboard_protocol->modifiers |= bit;
	else
		keyboard_protocol->modifiers &= ~bit;
}

static void
applespi_handle_keyboard_event(struct applespi_data *applespi,
			       struct keyboard_protocol *keyboard_protocol)
{
	unsigned int key;
	int i;

	compiletime_assert(ARRAY_SIZE(applespi_controlcodes) ==
			   sizeof_field(struct keyboard_protocol, modifiers) * 8,
			   "applespi_controlcodes has wrong number of entries");

	/* check for rollover overflow, which is signalled by all keys == 1 */
	if (!memchr_inv(keyboard_protocol->keys_pressed, 1, MAX_ROLLOVER))
		return;

	/* remap fn key if desired */
	applespi_remap_fn_key(keyboard_protocol);

	/* check released keys */
	for (i = 0; i < MAX_ROLLOVER; i++) {
		if (memchr(keyboard_protocol->keys_pressed,
			   applespi->last_keys_pressed[i], MAX_ROLLOVER))
			continue;	/* key is still pressed */

		key = applespi_code_to_key(applespi->last_keys_pressed[i],
					   applespi->last_keys_fn_pressed[i]);
		input_report_key(applespi->keyboard_input_dev, key, 0);
		applespi->last_keys_fn_pressed[i] = 0;
	}

	/* check pressed keys */
	for (i = 0; i < MAX_ROLLOVER; i++) {
		if (keyboard_protocol->keys_pressed[i] <
				ARRAY_SIZE(applespi_scancodes) &&
		    keyboard_protocol->keys_pressed[i] > 0) {
			key = applespi_code_to_key(
					keyboard_protocol->keys_pressed[i],
					keyboard_protocol->fn_pressed);
			input_report_key(applespi->keyboard_input_dev, key, 1);
			applespi->last_keys_fn_pressed[i] =
					keyboard_protocol->fn_pressed;
		}
	}

	/* check control keys */
	for (i = 0; i < ARRAY_SIZE(applespi_controlcodes); i++) {
		if (keyboard_protocol->modifiers & BIT(i))
			input_report_key(applespi->keyboard_input_dev,
					 applespi_controlcodes[i], 1);
		else
			input_report_key(applespi->keyboard_input_dev,
					 applespi_controlcodes[i], 0);
	}

	/* check function key */
	if (keyboard_protocol->fn_pressed && !applespi->last_fn_pressed)
		input_report_key(applespi->keyboard_input_dev, KEY_FN, 1);
	else if (!keyboard_protocol->fn_pressed && applespi->last_fn_pressed)
		input_report_key(applespi->keyboard_input_dev, KEY_FN, 0);
	applespi->last_fn_pressed = keyboard_protocol->fn_pressed;

	/* done */
	input_sync(applespi->keyboard_input_dev);
	memcpy(&applespi->last_keys_pressed, keyboard_protocol->keys_pressed,
	       sizeof(applespi->last_keys_pressed));
}

static const struct applespi_tp_info *applespi_find_touchpad_info(u8 model)
{
	const struct applespi_tp_model_info *info;

	for (info = applespi_tp_models; info->model; info++) {
		if (info->model == model)
			return &info->tp_info;
	}

	return NULL;
}

static int
applespi_register_touchpad_device(struct applespi_data *applespi,
				  struct touchpad_info_protocol *rcvd_tp_info)
{
	const struct applespi_tp_info *tp_info;
	struct input_dev *touchpad_input_dev;
	int sts;

	/* set up touchpad dimensions */
	tp_info = applespi_find_touchpad_info(rcvd_tp_info->model_no);
	if (!tp_info) {
		dev_warn(&applespi->spi->dev,
			 "Unknown touchpad model %x - falling back to MB8 touchpad\n",
			 rcvd_tp_info->model_no);
		tp_info = &applespi_tp_models[0].tp_info;
	}

	applespi->tp_info = *tp_info;

	if (touchpad_dimensions[0]) {
		int x, y, w, h;

		sts = sscanf(touchpad_dimensions, "%dx%d+%u+%u", &x, &y, &w, &h);
		if (sts == 4) {
			dev_info(&applespi->spi->dev,
				 "Overriding touchpad dimensions from module param\n");
			applespi->tp_info.x_min = x;
			applespi->tp_info.y_min = y;
			applespi->tp_info.x_max = x + w;
			applespi->tp_info.y_max = y + h;
		} else {
			dev_warn(&applespi->spi->dev,
				 "Invalid touchpad dimensions '%s': must be in the form XxY+W+H\n",
				 touchpad_dimensions);
			touchpad_dimensions[0] = '\0';
		}
	}
	if (!touchpad_dimensions[0]) {
		snprintf(touchpad_dimensions, sizeof(touchpad_dimensions),
			 "%dx%d+%u+%u",
			 applespi->tp_info.x_min,
			 applespi->tp_info.y_min,
			 applespi->tp_info.x_max - applespi->tp_info.x_min,
			 applespi->tp_info.y_max - applespi->tp_info.y_min);
	}

	/* create touchpad input device */
	touchpad_input_dev = devm_input_allocate_device(&applespi->spi->dev);
	if (!touchpad_input_dev) {
		dev_err(&applespi->spi->dev,
			"Failed to allocate touchpad input device\n");
		return -ENOMEM;
	}

	touchpad_input_dev->name = "Apple SPI Touchpad";
	touchpad_input_dev->phys = "applespi/input1";
	touchpad_input_dev->dev.parent = &applespi->spi->dev;
	touchpad_input_dev->id.bustype = BUS_SPI;
	touchpad_input_dev->id.vendor = SYNAPTICS_VENDOR_ID;
	touchpad_input_dev->id.product =
			rcvd_tp_info->model_no << 8 | rcvd_tp_info->model_flags;

	/* basic properties */
	input_set_capability(touchpad_input_dev, EV_REL, REL_X);
	input_set_capability(touchpad_input_dev, EV_REL, REL_Y);

	__set_bit(INPUT_PROP_POINTER, touchpad_input_dev->propbit);
	__set_bit(INPUT_PROP_BUTTONPAD, touchpad_input_dev->propbit);

	/* finger touch area */
	input_set_abs_params(touchpad_input_dev, ABS_MT_TOUCH_MAJOR,
			     0, 5000, 0, 0);
	input_set_abs_params(touchpad_input_dev, ABS_MT_TOUCH_MINOR,
			     0, 5000, 0, 0);

	/* finger approach area */
	input_set_abs_params(touchpad_input_dev, ABS_MT_WIDTH_MAJOR,
			     0, 5000, 0, 0);
	input_set_abs_params(touchpad_input_dev, ABS_MT_WIDTH_MINOR,
			     0, 5000, 0, 0);

	/* finger orientation */
	input_set_abs_params(touchpad_input_dev, ABS_MT_ORIENTATION,
			     -MAX_FINGER_ORIENTATION, MAX_FINGER_ORIENTATION,
			     0, 0);

	/* finger position */
	input_set_abs_params(touchpad_input_dev, ABS_MT_POSITION_X,
			     applespi->tp_info.x_min, applespi->tp_info.x_max,
			     0, 0);
	input_set_abs_params(touchpad_input_dev, ABS_MT_POSITION_Y,
			     applespi->tp_info.y_min, applespi->tp_info.y_max,
			     0, 0);

	/* touchpad button */
	input_set_capability(touchpad_input_dev, EV_KEY, BTN_LEFT);

	/* multitouch */
	sts = input_mt_init_slots(touchpad_input_dev, MAX_FINGERS,
				  INPUT_MT_POINTER | INPUT_MT_DROP_UNUSED |
					INPUT_MT_TRACK);
	if (sts) {
		dev_err(&applespi->spi->dev,
			"failed to initialize slots: %d", sts);
		return sts;
	}

	/* register input device */
	sts = input_register_device(touchpad_input_dev);
	if (sts) {
		dev_err(&applespi->spi->dev,
			"Unable to register touchpad input device (%d)\n", sts);
		return sts;
	}

	/* touchpad_input_dev is read async in spi callback */
	smp_store_release(&applespi->touchpad_input_dev, touchpad_input_dev);

	return 0;
}

static void applespi_worker(struct work_struct *work)
{
	struct applespi_data *applespi =
		container_of(work, struct applespi_data, work);

	applespi_register_touchpad_device(applespi, &applespi->rcvd_tp_info);
}

static void applespi_handle_cmd_response(struct applespi_data *applespi,
					 struct spi_packet *packet,
					 struct message *message)
{
	if (packet->device == PACKET_DEV_INFO &&
	    le16_to_cpu(message->type) == 0x1020) {
		/*
		 * We're not allowed to sleep here, but registering an input
		 * device can sleep.
		 */
		applespi->rcvd_tp_info = message->tp_info;
		schedule_work(&applespi->work);
		return;
	}

	if (le16_to_cpu(message->length) != 0x0000) {
		dev_warn_ratelimited(&applespi->spi->dev,
				     "Received unexpected write response: length=%x\n",
				     le16_to_cpu(message->length));
		return;
	}

	if (packet->device == PACKET_DEV_TPAD &&
	    le16_to_cpu(message->type) == 0x0252 &&
	    le16_to_cpu(message->rsp_buf_len) == 0x0002)
		dev_info(&applespi->spi->dev, "modeswitch done.\n");
}

static bool applespi_verify_crc(struct applespi_data *applespi, u8 *buffer,
				size_t buflen)
{
	u16 crc;

	crc = crc16(0, buffer, buflen);
	if (crc) {
		dev_warn_ratelimited(&applespi->spi->dev,
				     "Received corrupted packet (crc mismatch)\n");
		trace_applespi_bad_crc(ET_RD_CRC, READ, buffer, buflen);

		return false;
	}

	return true;
}

static void applespi_debug_print_read_packet(struct applespi_data *applespi,
					     struct spi_packet *packet)
{
	unsigned int evt_type;

	if (packet->flags == PACKET_TYPE_READ &&
	    packet->device == PACKET_DEV_KEYB)
		evt_type = ET_RD_KEYB;
	else if (packet->flags == PACKET_TYPE_READ &&
		 packet->device == PACKET_DEV_TPAD)
		evt_type = ET_RD_TPAD;
	else if (packet->flags == PACKET_TYPE_WRITE)
		evt_type = applespi->cmd_evt_type;
	else
		evt_type = ET_RD_UNKN;

	applespi_get_trace_fun(evt_type)(evt_type, PT_READ, applespi->rx_buffer,
					 APPLESPI_PACKET_SIZE);
}

static void applespi_got_data(struct applespi_data *applespi)
{
	struct spi_packet *packet;
	struct message *message;
	unsigned int msg_len;
	unsigned int off;
	unsigned int rem;
	unsigned int len;

	/* process packet header */
	if (!applespi_verify_crc(applespi, applespi->rx_buffer,
				 APPLESPI_PACKET_SIZE)) {
		unsigned long flags;

		spin_lock_irqsave(&applespi->cmd_msg_lock, flags);

		if (applespi->drain) {
			applespi->read_active = false;
			applespi->write_active = false;

			wake_up_all(&applespi->drain_complete);
		}

		spin_unlock_irqrestore(&applespi->cmd_msg_lock, flags);

		return;
	}

	packet = (struct spi_packet *)applespi->rx_buffer;

	applespi_debug_print_read_packet(applespi, packet);

	off = le16_to_cpu(packet->offset);
	rem = le16_to_cpu(packet->remaining);
	len = le16_to_cpu(packet->length);

	if (len > sizeof(packet->data)) {
		dev_warn_ratelimited(&applespi->spi->dev,
				     "Received corrupted packet (invalid packet length %u)\n",
				     len);
		goto msg_complete;
	}

	/* handle multi-packet messages */
	if (rem > 0 || off > 0) {
		if (off != applespi->saved_msg_len) {
			dev_warn_ratelimited(&applespi->spi->dev,
					     "Received unexpected offset (got %u, expected %u)\n",
					     off, applespi->saved_msg_len);
			goto msg_complete;
		}

		if (off + rem > MAX_PKTS_PER_MSG * APPLESPI_PACKET_SIZE) {
			dev_warn_ratelimited(&applespi->spi->dev,
					     "Received message too large (size %u)\n",
					     off + rem);
			goto msg_complete;
		}

		if (off + len > MAX_PKTS_PER_MSG * APPLESPI_PACKET_SIZE) {
			dev_warn_ratelimited(&applespi->spi->dev,
					     "Received message too large (size %u)\n",
					     off + len);
			goto msg_complete;
		}

		memcpy(applespi->msg_buf + off, &packet->data, len);
		applespi->saved_msg_len += len;

		if (rem > 0)
			return;

		message = (struct message *)applespi->msg_buf;
		msg_len = applespi->saved_msg_len;
	} else {
		message = (struct message *)&packet->data;
		msg_len = len;
	}

	/* got complete message - verify */
	if (!applespi_verify_crc(applespi, (u8 *)message, msg_len))
		goto msg_complete;

	if (le16_to_cpu(message->length) != msg_len - MSG_HEADER_SIZE - 2) {
		dev_warn_ratelimited(&applespi->spi->dev,
				     "Received corrupted packet (invalid message length %u - expected %u)\n",
				     le16_to_cpu(message->length),
				     msg_len - MSG_HEADER_SIZE - 2);
		goto msg_complete;
	}

	/* handle message */
	if (packet->flags == PACKET_TYPE_READ &&
	    packet->device == PACKET_DEV_KEYB) {
		applespi_handle_keyboard_event(applespi, &message->keyboard);

	} else if (packet->flags == PACKET_TYPE_READ &&
		   packet->device == PACKET_DEV_TPAD) {
		struct touchpad_protocol *tp;
		size_t tp_len;

		tp = &message->touchpad;
		tp_len = struct_size(tp, fingers, tp->number_of_fingers);

		if (le16_to_cpu(message->length) + 2 != tp_len) {
			dev_warn_ratelimited(&applespi->spi->dev,
					     "Received corrupted packet (invalid message length %u - num-fingers %u, tp-len %zu)\n",
					     le16_to_cpu(message->length),
					     tp->number_of_fingers, tp_len);
			goto msg_complete;
		}

		if (tp->number_of_fingers > MAX_FINGERS) {
			dev_warn_ratelimited(&applespi->spi->dev,
					     "Number of reported fingers (%u) exceeds max (%u))\n",
					     tp->number_of_fingers,
					     MAX_FINGERS);
			tp->number_of_fingers = MAX_FINGERS;
		}

		report_tp_state(applespi, tp);

	} else if (packet->flags == PACKET_TYPE_WRITE) {
		applespi_handle_cmd_response(applespi, packet, message);
	}

msg_complete:
	applespi->saved_msg_len = 0;

	applespi_msg_complete(applespi, packet->flags == PACKET_TYPE_WRITE,
			      true);
}

static void applespi_async_read_complete(void *context)
{
	struct applespi_data *applespi = context;

	if (applespi->rd_m.status < 0) {
		dev_warn(&applespi->spi->dev, "Error reading from device: %d\n",
			 applespi->rd_m.status);
		/*
		 * We don't actually know if this was a pure read, or a response
		 * to a write. But this is a rare error condition that should
		 * never occur, so clearing both flags to avoid deadlock.
		 */
		applespi_msg_complete(applespi, true, true);
	} else {
		applespi_got_data(applespi);
	}

	acpi_finish_gpe(NULL, applespi->gpe);
}

static u32 applespi_notify(acpi_handle gpe_device, u32 gpe, void *context)
{
	struct applespi_data *applespi = context;
	int sts;
	unsigned long flags;

	trace_applespi_irq_received(ET_RD_IRQ, PT_READ);

	spin_lock_irqsave(&applespi->cmd_msg_lock, flags);

	if (!applespi->suspended) {
		sts = applespi_async(applespi, &applespi->rd_m,
				     applespi_async_read_complete);
		if (sts)
			dev_warn(&applespi->spi->dev,
				 "Error queueing async read to device: %d\n",
				 sts);
		else
			applespi->read_active = true;
	}

	spin_unlock_irqrestore(&applespi->cmd_msg_lock, flags);

	return ACPI_INTERRUPT_HANDLED;
}

static int applespi_get_saved_bl_level(struct applespi_data *applespi)
{
	struct efivar_entry *efivar_entry;
	u16 efi_data = 0;
	unsigned long efi_data_len;
	int sts;

	efivar_entry = kmalloc(sizeof(*efivar_entry), GFP_KERNEL);
	if (!efivar_entry)
		return -ENOMEM;

	memcpy(efivar_entry->var.VariableName, EFI_BL_LEVEL_NAME,
	       sizeof(EFI_BL_LEVEL_NAME));
	efivar_entry->var.VendorGuid = EFI_BL_LEVEL_GUID;
	efi_data_len = sizeof(efi_data);

	sts = efivar_entry_get(efivar_entry, NULL, &efi_data_len, &efi_data);
	if (sts && sts != -ENOENT)
		dev_warn(&applespi->spi->dev,
			 "Error getting backlight level from EFI vars: %d\n",
			 sts);

	kfree(efivar_entry);

	return sts ? sts : efi_data;
}

static void applespi_save_bl_level(struct applespi_data *applespi,
				   unsigned int level)
{
	efi_guid_t efi_guid;
	u32 efi_attr;
	unsigned long efi_data_len;
	u16 efi_data;
	int sts;

	/* Save keyboard backlight level */
	efi_guid = EFI_BL_LEVEL_GUID;
	efi_data = (u16)level;
	efi_data_len = sizeof(efi_data);
	efi_attr = EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS |
		   EFI_VARIABLE_RUNTIME_ACCESS;

	sts = efivar_entry_set_safe((efi_char16_t *)EFI_BL_LEVEL_NAME, efi_guid,
				    efi_attr, true, efi_data_len, &efi_data);
	if (sts)
		dev_warn(&applespi->spi->dev,
			 "Error saving backlight level to EFI vars: %d\n", sts);
}

static int applespi_probe(struct spi_device *spi)
{
	struct applespi_data *applespi;
	acpi_handle spi_handle = ACPI_HANDLE(&spi->dev);
	acpi_status acpi_sts;
	int sts, i;
	unsigned long long gpe, usb_status;

	/* check if the USB interface is present and enabled already */
	acpi_sts = acpi_evaluate_integer(spi_handle, "UIST", NULL, &usb_status);
	if (ACPI_SUCCESS(acpi_sts) && usb_status) {
		/* let the USB driver take over instead */
		dev_info(&spi->dev, "USB interface already enabled\n");
		return -ENODEV;
	}

	/* allocate driver data */
	applespi = devm_kzalloc(&spi->dev, sizeof(*applespi), GFP_KERNEL);
	if (!applespi)
		return -ENOMEM;

	applespi->spi = spi;

	INIT_WORK(&applespi->work, applespi_worker);

	/* store the driver data */
	spi_set_drvdata(spi, applespi);

	/* create our buffers */
	applespi->tx_buffer = devm_kmalloc(&spi->dev, APPLESPI_PACKET_SIZE,
					   GFP_KERNEL);
	applespi->tx_status = devm_kmalloc(&spi->dev, APPLESPI_STATUS_SIZE,
					   GFP_KERNEL);
	applespi->rx_buffer = devm_kmalloc(&spi->dev, APPLESPI_PACKET_SIZE,
					   GFP_KERNEL);
	applespi->msg_buf = devm_kmalloc_array(&spi->dev, MAX_PKTS_PER_MSG,
					       APPLESPI_PACKET_SIZE,
					       GFP_KERNEL);

	if (!applespi->tx_buffer || !applespi->tx_status ||
	    !applespi->rx_buffer || !applespi->msg_buf)
		return -ENOMEM;

	/* set up our spi messages */
	applespi_setup_read_txfrs(applespi);
	applespi_setup_write_txfrs(applespi);

	/* cache ACPI method handles */
	acpi_sts = acpi_get_handle(spi_handle, "SIEN", &applespi->sien);
	if (ACPI_FAILURE(acpi_sts)) {
		dev_err(&applespi->spi->dev,
			"Failed to get SIEN ACPI method handle: %s\n",
			acpi_format_exception(acpi_sts));
		return -ENODEV;
	}

	acpi_sts = acpi_get_handle(spi_handle, "SIST", &applespi->sist);
	if (ACPI_FAILURE(acpi_sts)) {
		dev_err(&applespi->spi->dev,
			"Failed to get SIST ACPI method handle: %s\n",
			acpi_format_exception(acpi_sts));
		return -ENODEV;
	}

	/* switch on the SPI interface */
	sts = applespi_setup_spi(applespi);
	if (sts)
		return sts;

	sts = applespi_enable_spi(applespi);
	if (sts)
		return sts;

	/* setup the keyboard input dev */
	applespi->keyboard_input_dev = devm_input_allocate_device(&spi->dev);

	if (!applespi->keyboard_input_dev)
		return -ENOMEM;

	applespi->keyboard_input_dev->name = "Apple SPI Keyboard";
	applespi->keyboard_input_dev->phys = "applespi/input0";
	applespi->keyboard_input_dev->dev.parent = &spi->dev;
	applespi->keyboard_input_dev->id.bustype = BUS_SPI;

	applespi->keyboard_input_dev->evbit[0] =
			BIT_MASK(EV_KEY) | BIT_MASK(EV_LED) | BIT_MASK(EV_REP);
	applespi->keyboard_input_dev->ledbit[0] = BIT_MASK(LED_CAPSL);

	input_set_drvdata(applespi->keyboard_input_dev, applespi);
	applespi->keyboard_input_dev->event = applespi_event;

	for (i = 0; i < ARRAY_SIZE(applespi_scancodes); i++)
		if (applespi_scancodes[i])
			input_set_capability(applespi->keyboard_input_dev,
					     EV_KEY, applespi_scancodes[i]);

	for (i = 0; i < ARRAY_SIZE(applespi_controlcodes); i++)
		if (applespi_controlcodes[i])
			input_set_capability(applespi->keyboard_input_dev,
					     EV_KEY, applespi_controlcodes[i]);

	for (i = 0; i < ARRAY_SIZE(applespi_fn_codes); i++)
		if (applespi_fn_codes[i].to)
			input_set_capability(applespi->keyboard_input_dev,
					     EV_KEY, applespi_fn_codes[i].to);

	input_set_capability(applespi->keyboard_input_dev, EV_KEY, KEY_FN);

	sts = input_register_device(applespi->keyboard_input_dev);
	if (sts) {
		dev_err(&applespi->spi->dev,
			"Unable to register keyboard input device (%d)\n", sts);
		return -ENODEV;
	}

	/*
	 * The applespi device doesn't send interrupts normally (as is described
	 * in its DSDT), but rather seems to use ACPI GPEs.
	 */
	acpi_sts = acpi_evaluate_integer(spi_handle, "_GPE", NULL, &gpe);
	if (ACPI_FAILURE(acpi_sts)) {
		dev_err(&applespi->spi->dev,
			"Failed to obtain GPE for SPI slave device: %s\n",
			acpi_format_exception(acpi_sts));
		return -ENODEV;
	}
	applespi->gpe = (int)gpe;

	acpi_sts = acpi_install_gpe_handler(NULL, applespi->gpe,
					    ACPI_GPE_LEVEL_TRIGGERED,
					    applespi_notify, applespi);
	if (ACPI_FAILURE(acpi_sts)) {
		dev_err(&applespi->spi->dev,
			"Failed to install GPE handler for GPE %d: %s\n",
			applespi->gpe, acpi_format_exception(acpi_sts));
		return -ENODEV;
	}

	applespi->suspended = false;

	acpi_sts = acpi_enable_gpe(NULL, applespi->gpe);
	if (ACPI_FAILURE(acpi_sts)) {
		dev_err(&applespi->spi->dev,
			"Failed to enable GPE handler for GPE %d: %s\n",
			applespi->gpe, acpi_format_exception(acpi_sts));
		acpi_remove_gpe_handler(NULL, applespi->gpe, applespi_notify);
		return -ENODEV;
	}

	/* trigger touchpad setup */
	applespi_init(applespi, false);

	/*
	 * By default this device is not enabled for wakeup; but USB keyboards
	 * generally are, so the expectation is that by default the keyboard
	 * will wake the system.
	 */
	device_wakeup_enable(&spi->dev);

	/* set up keyboard-backlight */
	sts = applespi_get_saved_bl_level(applespi);
	if (sts >= 0)
		applespi_set_bl_level(&applespi->backlight_info, sts);

	applespi->backlight_info.name            = "spi::kbd_backlight";
	applespi->backlight_info.default_trigger = "kbd-backlight";
	applespi->backlight_info.brightness_set  = applespi_set_bl_level;

	sts = devm_led_classdev_register(&spi->dev, &applespi->backlight_info);
	if (sts)
		dev_warn(&applespi->spi->dev,
			 "Unable to register keyboard backlight class dev (%d)\n",
			 sts);

	/* set up debugfs entries for touchpad dimensions logging */
	applespi->debugfs_root = debugfs_create_dir("applespi", NULL);

	debugfs_create_bool("enable_tp_dim", 0600, applespi->debugfs_root,
			    &applespi->debug_tp_dim);

	debugfs_create_file("tp_dim", 0400, applespi->debugfs_root, applespi,
			    &applespi_tp_dim_fops);

	return 0;
}

static void applespi_drain_writes(struct applespi_data *applespi)
{
	unsigned long flags;

	spin_lock_irqsave(&applespi->cmd_msg_lock, flags);

	applespi->drain = true;
	wait_event_lock_irq(applespi->drain_complete, !applespi->write_active,
			    applespi->cmd_msg_lock);

	spin_unlock_irqrestore(&applespi->cmd_msg_lock, flags);
}

static void applespi_drain_reads(struct applespi_data *applespi)
{
	unsigned long flags;

	spin_lock_irqsave(&applespi->cmd_msg_lock, flags);

	wait_event_lock_irq(applespi->drain_complete, !applespi->read_active,
			    applespi->cmd_msg_lock);

	applespi->suspended = true;

	spin_unlock_irqrestore(&applespi->cmd_msg_lock, flags);
}

static int applespi_remove(struct spi_device *spi)
{
	struct applespi_data *applespi = spi_get_drvdata(spi);

	applespi_drain_writes(applespi);

	acpi_disable_gpe(NULL, applespi->gpe);
	acpi_remove_gpe_handler(NULL, applespi->gpe, applespi_notify);
	device_wakeup_disable(&spi->dev);

	applespi_drain_reads(applespi);

	debugfs_remove_recursive(applespi->debugfs_root);

	return 0;
}

static void applespi_shutdown(struct spi_device *spi)
{
	struct applespi_data *applespi = spi_get_drvdata(spi);

	applespi_save_bl_level(applespi, applespi->have_bl_level);
}

static int applespi_poweroff_late(struct device *dev)
{
	struct spi_device *spi = to_spi_device(dev);
	struct applespi_data *applespi = spi_get_drvdata(spi);

	applespi_save_bl_level(applespi, applespi->have_bl_level);

	return 0;
}

static int __maybe_unused applespi_suspend(struct device *dev)
{
	struct spi_device *spi = to_spi_device(dev);
	struct applespi_data *applespi = spi_get_drvdata(spi);
	acpi_status acpi_sts;
	int sts;

	/* turn off caps-lock - it'll stay on otherwise */
	sts = applespi_set_capsl_led(applespi, false);
	if (sts)
		dev_warn(&applespi->spi->dev,
			 "Failed to turn off caps-lock led (%d)\n", sts);

	applespi_drain_writes(applespi);

	/* disable the interrupt */
	acpi_sts = acpi_disable_gpe(NULL, applespi->gpe);
	if (ACPI_FAILURE(acpi_sts))
		dev_err(&applespi->spi->dev,
			"Failed to disable GPE handler for GPE %d: %s\n",
			applespi->gpe, acpi_format_exception(acpi_sts));

	applespi_drain_reads(applespi);

	return 0;
}

static int __maybe_unused applespi_resume(struct device *dev)
{
	struct spi_device *spi = to_spi_device(dev);
	struct applespi_data *applespi = spi_get_drvdata(spi);
	acpi_status acpi_sts;
	unsigned long flags;

	/* ensure our flags and state reflect a newly resumed device */
	spin_lock_irqsave(&applespi->cmd_msg_lock, flags);

	applespi->drain = false;
	applespi->have_cl_led_on = false;
	applespi->have_bl_level = 0;
	applespi->cmd_msg_queued = 0;
	applespi->read_active = false;
	applespi->write_active = false;

	applespi->suspended = false;

	spin_unlock_irqrestore(&applespi->cmd_msg_lock, flags);

	/* switch on the SPI interface */
	applespi_enable_spi(applespi);

	/* re-enable the interrupt */
	acpi_sts = acpi_enable_gpe(NULL, applespi->gpe);
	if (ACPI_FAILURE(acpi_sts))
		dev_err(&applespi->spi->dev,
			"Failed to re-enable GPE handler for GPE %d: %s\n",
			applespi->gpe, acpi_format_exception(acpi_sts));

	/* switch the touchpad into multitouch mode */
	applespi_init(applespi, true);

	return 0;
}

static const struct acpi_device_id applespi_acpi_match[] = {
	{ "APP000D", 0 },
	{ }
};
MODULE_DEVICE_TABLE(acpi, applespi_acpi_match);

static const struct dev_pm_ops applespi_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(applespi_suspend, applespi_resume)
	.poweroff_late	= applespi_poweroff_late,
};

static struct spi_driver applespi_driver = {
	.driver		= {
		.name			= "applespi",
		.acpi_match_table	= applespi_acpi_match,
		.pm			= &applespi_pm_ops,
	},
	.probe		= applespi_probe,
	.remove		= applespi_remove,
	.shutdown	= applespi_shutdown,
};

module_spi_driver(applespi_driver)

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("MacBook(Pro) SPI Keyboard/Touchpad driver");
MODULE_AUTHOR("Federico Lorenzi");
MODULE_AUTHOR("Ronald Tschalär");

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Synaptics TouchPad PS/2 mouse driver
 *
 *   2003 Dmitry Torokhov <dtor@mail.ru>
 *     Added support for pass-through port. Special thanks to Peter Berg Larsen
 *     for explaining various Synaptics quirks.
 *
 *   2003 Peter Osterlund <petero2@telia.com>
 *     Ported to 2.5 input device infrastructure.
 *
 *   Copyright (C) 2001 Stefan Gmeiner <riddlebox@freesurf.ch>
 *     start merging tpconfig and gpm code to a xfree-input module
 *     adding some changes and extensions (ex. 3rd and 4th button)
 *
 *   Copyright (c) 1997 C. Scott Ananian <cananian@alumni.priceton.edu>
 *   Copyright (c) 1998-2000 Bruce Kalk <kall@compass.com>
 *     code for the special synaptics commands (from the tpconfig-source)
 *
 * Trademarks are the property of their respective owners.
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/dmi.h>
#include <linux/input/mt.h>
#include <linux/serio.h>
#include <linux/libps2.h>
#include <linux/rmi.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include "psmouse.h"
#include "synaptics.h"

/*
 * The x/y limits are taken from the Synaptics TouchPad interfacing Guide,
 * section 2.3.2, which says that they should be valid regardless of the
 * actual size of the sensor.
 * Note that newer firmware allows querying device for maximum useable
 * coordinates.
 */
#define XMIN 0
#define XMAX 6143
#define YMIN 0
#define YMAX 6143
#define XMIN_NOMINAL 1472
#define XMAX_NOMINAL 5472
#define YMIN_NOMINAL 1408
#define YMAX_NOMINAL 4448

/* Size in bits of absolute position values reported by the hardware */
#define ABS_POS_BITS 13

/*
 * These values should represent the absolute maximum value that will
 * be reported for a positive position value. Some Synaptics firmware
 * uses this value to indicate a finger near the edge of the touchpad
 * whose precise position cannot be determined.
 *
 * At least one touchpad is known to report positions in excess of this
 * value which are actually negative values truncated to the 13-bit
 * reporting range. These values have never been observed to be lower
 * than 8184 (i.e. -8), so we treat all values greater than 8176 as
 * negative and any other value as positive.
 */
#define X_MAX_POSITIVE 8176
#define Y_MAX_POSITIVE 8176

/* maximum ABS_MT_POSITION displacement (in mm) */
#define DMAX 10

/*****************************************************************************
 *	Stuff we need even when we do not want native Synaptics support
 ****************************************************************************/

/*
 * Set the synaptics touchpad mode byte by special commands
 */
static int synaptics_mode_cmd(struct psmouse *psmouse, u8 mode)
{
	u8 param[1];
	int error;

	error = ps2_sliced_command(&psmouse->ps2dev, mode);
	if (error)
		return error;

	param[0] = SYN_PS_SET_MODE2;
	error = ps2_command(&psmouse->ps2dev, param, PSMOUSE_CMD_SETRATE);
	if (error)
		return error;

	return 0;
}

int synaptics_detect(struct psmouse *psmouse, bool set_properties)
{
	struct ps2dev *ps2dev = &psmouse->ps2dev;
	u8 param[4] = { 0 };

	ps2_command(ps2dev, param, PSMOUSE_CMD_SETRES);
	ps2_command(ps2dev, param, PSMOUSE_CMD_SETRES);
	ps2_command(ps2dev, param, PSMOUSE_CMD_SETRES);
	ps2_command(ps2dev, param, PSMOUSE_CMD_SETRES);
	ps2_command(ps2dev, param, PSMOUSE_CMD_GETINFO);

	if (param[1] != 0x47)
		return -ENODEV;

	if (set_properties) {
		psmouse->vendor = "Synaptics";
		psmouse->name = "TouchPad";
	}

	return 0;
}

void synaptics_reset(struct psmouse *psmouse)
{
	/* reset touchpad back to relative mode, gestures enabled */
	synaptics_mode_cmd(psmouse, 0);
}

#if defined(CONFIG_MOUSE_PS2_SYNAPTICS) || \
    defined(CONFIG_MOUSE_PS2_SYNAPTICS_SMBUS)

/* This list has been kindly provided by Synaptics. */
static const char * const topbuttonpad_pnp_ids[] = {
	"LEN0017",
	"LEN0018",
	"LEN0019",
	"LEN0023",
	"LEN002A",
	"LEN002B",
	"LEN002C",
	"LEN002D",
	"LEN002E",
	"LEN0033", /* Helix */
	"LEN0034", /* T431s, L440, L540, T540, W540, X1 Carbon 2nd */
	"LEN0035", /* X240 */
	"LEN0036", /* T440 */
	"LEN0037", /* X1 Carbon 2nd */
	"LEN0038",
	"LEN0039", /* T440s */
	"LEN0041",
	"LEN0042", /* Yoga */
	"LEN0045",
	"LEN0047",
	"LEN2000", /* S540 */
	"LEN2001", /* Edge E431 */
	"LEN2002", /* Edge E531 */
	"LEN2003",
	"LEN2004", /* L440 */
	"LEN2005",
	"LEN2006", /* Edge E440/E540 */
	"LEN2007",
	"LEN2008",
	"LEN2009",
	"LEN200A",
	"LEN200B",
	NULL
};

static const char * const smbus_pnp_ids[] = {
	/* all of the topbuttonpad_pnp_ids are valid, we just add some extras */
	"LEN0048", /* X1 Carbon 3 */
	"LEN0046", /* X250 */
	"LEN0049", /* Yoga 11e */
	"LEN004a", /* W541 */
	"LEN005b", /* P50 */
	"LEN005e", /* T560 */
	"LEN006c", /* T470s */
	"LEN007a", /* T470s */
	"LEN0071", /* T480 */
	"LEN0072", /* X1 Carbon Gen 5 (2017) - Elan/ALPS trackpoint */
	"LEN0073", /* X1 Carbon G5 (Elantech) */
	"LEN0091", /* X1 Carbon 6 */
	"LEN0092", /* X1 Carbon 6 */
	"LEN0093", /* T480 */
	"LEN0096", /* X280 */
	"LEN0097", /* X280 -> ALPS trackpoint */
	"LEN0099", /* X1 Extreme Gen 1 / P1 Gen 1 */
	"LEN009b", /* T580 */
	"LEN0402", /* X1 Extreme Gen 2 / P1 Gen 2 */
	"LEN200f", /* T450s */
	"LEN2044", /* L470  */
	"LEN2054", /* E480 */
	"LEN2055", /* E580 */
	"LEN2068", /* T14 Gen 1 */
	"SYN3052", /* HP EliteBook 840 G4 */
	"SYN3221", /* HP 15-ay000 */
	"SYN323d", /* HP Spectre X360 13-w013dx */
	"SYN3257", /* HP Envy 13-ad105ng */
	"SYN3286", /* HP Laptop 15-da3001TU */
	NULL
};

static const char * const forcepad_pnp_ids[] = {
	"SYN300D",
	"SYN3014",
	NULL
};

/*
 * Send a command to the synaptics touchpad by special commands
 */
static int synaptics_send_cmd(struct psmouse *psmouse, u8 cmd, u8 *param)
{
	int error;

	error = ps2_sliced_command(&psmouse->ps2dev, cmd);
	if (error)
		return error;

	error = ps2_command(&psmouse->ps2dev, param, PSMOUSE_CMD_GETINFO);
	if (error)
		return error;

	return 0;
}

static int synaptics_query_int(struct psmouse *psmouse, u8 query_cmd, u32 *val)
{
	int error;
	union {
		__be32 be_val;
		char buf[4];
	} resp = { 0 };

	error = synaptics_send_cmd(psmouse, query_cmd, resp.buf + 1);
	if (error)
		return error;

	*val = be32_to_cpu(resp.be_val);
	return 0;
}

/*
 * Identify Touchpad
 * See also the SYN_ID_* macros
 */
static int synaptics_identify(struct psmouse *psmouse,
			      struct synaptics_device_info *info)
{
	int error;

	error = synaptics_query_int(psmouse, SYN_QUE_IDENTIFY, &info->identity);
	if (error)
		return error;

	return SYN_ID_IS_SYNAPTICS(info->identity) ? 0 : -ENXIO;
}

/*
 * Read the model-id bytes from the touchpad
 * see also SYN_MODEL_* macros
 */
static int synaptics_model_id(struct psmouse *psmouse,
			      struct synaptics_device_info *info)
{
	return synaptics_query_int(psmouse, SYN_QUE_MODEL, &info->model_id);
}

/*
 * Read the firmware id from the touchpad
 */
static int synaptics_firmware_id(struct psmouse *psmouse,
				 struct synaptics_device_info *info)
{
	return synaptics_query_int(psmouse, SYN_QUE_FIRMWARE_ID,
				   &info->firmware_id);
}

/*
 * Read the board id and the "More Extended Queries" from the touchpad
 * The board id is encoded in the "QUERY MODES" response
 */
static int synaptics_query_modes(struct psmouse *psmouse,
				 struct synaptics_device_info *info)
{
	u8 bid[3];
	int error;

	/* firmwares prior 7.5 have no board_id encoded */
	if (SYN_ID_FULL(info->identity) < 0x705)
		return 0;

	error = synaptics_send_cmd(psmouse, SYN_QUE_MODES, bid);
	if (error)
		return error;

	info->board_id = ((bid[0] & 0xfc) << 6) | bid[1];

	if (SYN_MEXT_CAP_BIT(bid[0]))
		return synaptics_query_int(psmouse, SYN_QUE_MEXT_CAPAB_10,
					   &info->ext_cap_10);

	return 0;
}

/*
 * Read the capability-bits from the touchpad
 * see also the SYN_CAP_* macros
 */
static int synaptics_capability(struct psmouse *psmouse,
				struct synaptics_device_info *info)
{
	int error;

	error = synaptics_query_int(psmouse, SYN_QUE_CAPABILITIES,
				    &info->capabilities);
	if (error)
		return error;

	info->ext_cap = info->ext_cap_0c = 0;

	/*
	 * Older firmwares had submodel ID fixed to 0x47
	 */
	if (SYN_ID_FULL(info->identity) < 0x705 &&
	    SYN_CAP_SUBMODEL_ID(info->capabilities) != 0x47) {
		return -ENXIO;
	}

	/*
	 * Unless capExtended is set the rest of the flags should be ignored
	 */
	if (!SYN_CAP_EXTENDED(info->capabilities))
		info->capabilities = 0;

	if (SYN_EXT_CAP_REQUESTS(info->capabilities) >= 1) {
		error = synaptics_query_int(psmouse, SYN_QUE_EXT_CAPAB,
					    &info->ext_cap);
		if (error) {
			psmouse_warn(psmouse,
				     "device claims to have extended capabilities, but I'm not able to read them.\n");
		} else {
			/*
			 * if nExtBtn is greater than 8 it should be considered
			 * invalid and treated as 0
			 */
			if (SYN_CAP_MULTI_BUTTON_NO(info->ext_cap) > 8)
				info->ext_cap &= ~SYN_CAP_MB_MASK;
		}
	}

	if (SYN_EXT_CAP_REQUESTS(info->capabilities) >= 4) {
		error = synaptics_query_int(psmouse, SYN_QUE_EXT_CAPAB_0C,
					    &info->ext_cap_0c);
		if (error)
			psmouse_warn(psmouse,
				     "device claims to have extended capability 0x0c, but I'm not able to read it.\n");
	}

	return 0;
}

/*
 * Read touchpad resolution and maximum reported coordinates
 * Resolution is left zero if touchpad does not support the query
 */
static int synaptics_resolution(struct psmouse *psmouse,
				struct synaptics_device_info *info)
{
	u8 resp[3];
	int error;

	if (SYN_ID_MAJOR(info->identity) < 4)
		return 0;

	error = synaptics_send_cmd(psmouse, SYN_QUE_RESOLUTION, resp);
	if (!error) {
		if (resp[0] != 0 && (resp[1] & 0x80) && resp[2] != 0) {
			info->x_res = resp[0]; /* x resolution in units/mm */
			info->y_res = resp[2]; /* y resolution in units/mm */
		}
	}

	if (SYN_EXT_CAP_REQUESTS(info->capabilities) >= 5 &&
	    SYN_CAP_MAX_DIMENSIONS(info->ext_cap_0c)) {
		error = synaptics_send_cmd(psmouse,
					   SYN_QUE_EXT_MAX_COORDS, resp);
		if (error) {
			psmouse_warn(psmouse,
				     "device claims to have max coordinates query, but I'm not able to read it.\n");
		} else {
			info->x_max = (resp[0] << 5) | ((resp[1] & 0x0f) << 1);
			info->y_max = (resp[2] << 5) | ((resp[1] & 0xf0) >> 3);
			psmouse_info(psmouse,
				     "queried max coordinates: x [..%d], y [..%d]\n",
				     info->x_max, info->y_max);
		}
	}

	if (SYN_CAP_MIN_DIMENSIONS(info->ext_cap_0c) &&
	    (SYN_EXT_CAP_REQUESTS(info->capabilities) >= 7 ||
	     /*
	      * Firmware v8.1 does not report proper number of extended
	      * capabilities, but has been proven to report correct min
	      * coordinates.
	      */
	     SYN_ID_FULL(info->identity) == 0x801)) {
		error = synaptics_send_cmd(psmouse,
					   SYN_QUE_EXT_MIN_COORDS, resp);
		if (error) {
			psmouse_warn(psmouse,
				     "device claims to have min coordinates query, but I'm not able to read it.\n");
		} else {
			info->x_min = (resp[0] << 5) | ((resp[1] & 0x0f) << 1);
			info->y_min = (resp[2] << 5) | ((resp[1] & 0xf0) >> 3);
			psmouse_info(psmouse,
				     "queried min coordinates: x [%d..], y [%d..]\n",
				     info->x_min, info->y_min);
		}
	}

	return 0;
}

static int synaptics_query_hardware(struct psmouse *psmouse,
				    struct synaptics_device_info *info)
{
	int error;

	memset(info, 0, sizeof(*info));

	error = synaptics_identify(psmouse, info);
	if (error)
		return error;

	error = synaptics_model_id(psmouse, info);
	if (error)
		return error;

	error = synaptics_firmware_id(psmouse, info);
	if (error)
		return error;

	error = synaptics_query_modes(psmouse, info);
	if (error)
		return error;

	error = synaptics_capability(psmouse, info);
	if (error)
		return error;

	error = synaptics_resolution(psmouse, info);
	if (error)
		return error;

	return 0;
}

#endif /* CONFIG_MOUSE_PS2_SYNAPTICS || CONFIG_MOUSE_PS2_SYNAPTICS_SMBUS */

#ifdef CONFIG_MOUSE_PS2_SYNAPTICS

static bool cr48_profile_sensor;

#define ANY_BOARD_ID 0
struct min_max_quirk {
	const char * const *pnp_ids;
	struct {
		u32 min, max;
	} board_id;
	u32 x_min, x_max, y_min, y_max;
};

static const struct min_max_quirk min_max_pnpid_table[] = {
	{
		(const char * const []){"LEN0033", NULL},
		{ANY_BOARD_ID, ANY_BOARD_ID},
		1024, 5052, 2258, 4832
	},
	{
		(const char * const []){"LEN0042", NULL},
		{ANY_BOARD_ID, ANY_BOARD_ID},
		1232, 5710, 1156, 4696
	},
	{
		(const char * const []){"LEN0034", "LEN0036", "LEN0037",
					"LEN0039", "LEN2002", "LEN2004",
					NULL},
		{ANY_BOARD_ID, 2961},
		1024, 5112, 2024, 4832
	},
	{
		(const char * const []){"LEN2000", NULL},
		{ANY_BOARD_ID, ANY_BOARD_ID},
		1024, 5113, 2021, 4832
	},
	{
		(const char * const []){"LEN2001", NULL},
		{ANY_BOARD_ID, ANY_BOARD_ID},
		1024, 5022, 2508, 4832
	},
	{
		(const char * const []){"LEN2006", NULL},
		{2691, 2691},
		1024, 5045, 2457, 4832
	},
	{
		(const char * const []){"LEN2006", NULL},
		{ANY_BOARD_ID, ANY_BOARD_ID},
		1264, 5675, 1171, 4688
	},
	{ }
};

/*****************************************************************************
 *	Synaptics communications functions
 ****************************************************************************/

/*
 * Synaptics touchpads report the y coordinate from bottom to top, which is
 * opposite from what userspace expects.
 * This function is used to invert y before reporting.
 */
static int synaptics_invert_y(int y)
{
	return YMAX_NOMINAL + YMIN_NOMINAL - y;
}

/*
 * Apply quirk(s) if the hardware matches
 */
static void synaptics_apply_quirks(struct psmouse *psmouse,
				   struct synaptics_device_info *info)
{
	int i;

	for (i = 0; min_max_pnpid_table[i].pnp_ids; i++) {
		if (!psmouse_matches_pnp_id(psmouse,
					    min_max_pnpid_table[i].pnp_ids))
			continue;

		if (min_max_pnpid_table[i].board_id.min != ANY_BOARD_ID &&
		    info->board_id < min_max_pnpid_table[i].board_id.min)
			continue;

		if (min_max_pnpid_table[i].board_id.max != ANY_BOARD_ID &&
		    info->board_id > min_max_pnpid_table[i].board_id.max)
			continue;

		info->x_min = min_max_pnpid_table[i].x_min;
		info->x_max = min_max_pnpid_table[i].x_max;
		info->y_min = min_max_pnpid_table[i].y_min;
		info->y_max = min_max_pnpid_table[i].y_max;
		psmouse_info(psmouse,
			     "quirked min/max coordinates: x [%d..%d], y [%d..%d]\n",
			     info->x_min, info->x_max,
			     info->y_min, info->y_max);
		break;
	}
}

static bool synaptics_has_agm(struct synaptics_data *priv)
{
	return (SYN_CAP_ADV_GESTURE(priv->info.ext_cap_0c) ||
		SYN_CAP_IMAGE_SENSOR(priv->info.ext_cap_0c));
}

static int synaptics_set_advanced_gesture_mode(struct psmouse *psmouse)
{
	static u8 param = 0xc8;
	int error;

	error = ps2_sliced_command(&psmouse->ps2dev, SYN_QUE_MODEL);
	if (error)
		return error;

	error = ps2_command(&psmouse->ps2dev, &param, PSMOUSE_CMD_SETRATE);
	if (error)
		return error;

	return 0;
}

static int synaptics_set_mode(struct psmouse *psmouse)
{
	struct synaptics_data *priv = psmouse->private;
	int error;

	priv->mode = 0;
	if (priv->absolute_mode)
		priv->mode |= SYN_BIT_ABSOLUTE_MODE;
	if (priv->disable_gesture)
		priv->mode |= SYN_BIT_DISABLE_GESTURE;
	if (psmouse->rate >= 80)
		priv->mode |= SYN_BIT_HIGH_RATE;
	if (SYN_CAP_EXTENDED(priv->info.capabilities))
		priv->mode |= SYN_BIT_W_MODE;

	error = synaptics_mode_cmd(psmouse, priv->mode);
	if (error)
		return error;

	if (priv->absolute_mode && synaptics_has_agm(priv)) {
		error = synaptics_set_advanced_gesture_mode(psmouse);
		if (error) {
			psmouse_err(psmouse,
				    "Advanced gesture mode init failed: %d\n",
				    error);
			return error;
		}
	}

	return 0;
}

static void synaptics_set_rate(struct psmouse *psmouse, unsigned int rate)
{
	struct synaptics_data *priv = psmouse->private;

	if (rate >= 80) {
		priv->mode |= SYN_BIT_HIGH_RATE;
		psmouse->rate = 80;
	} else {
		priv->mode &= ~SYN_BIT_HIGH_RATE;
		psmouse->rate = 40;
	}

	synaptics_mode_cmd(psmouse, priv->mode);
}

/*****************************************************************************
 *	Synaptics pass-through PS/2 port support
 ****************************************************************************/
static int synaptics_pt_write(struct serio *serio, u8 c)
{
	struct psmouse *parent = serio_get_drvdata(serio->parent);
	u8 rate_param = SYN_PS_CLIENT_CMD; /* indicates that we want pass-through port */
	int error;

	error = ps2_sliced_command(&parent->ps2dev, c);
	if (error)
		return error;

	error = ps2_command(&parent->ps2dev, &rate_param, PSMOUSE_CMD_SETRATE);
	if (error)
		return error;

	return 0;
}

static int synaptics_pt_start(struct serio *serio)
{
	struct psmouse *parent = serio_get_drvdata(serio->parent);
	struct synaptics_data *priv = parent->private;

	serio_pause_rx(parent->ps2dev.serio);
	priv->pt_port = serio;
	serio_continue_rx(parent->ps2dev.serio);

	return 0;
}

static void synaptics_pt_stop(struct serio *serio)
{
	struct psmouse *parent = serio_get_drvdata(serio->parent);
	struct synaptics_data *priv = parent->private;

	serio_pause_rx(parent->ps2dev.serio);
	priv->pt_port = NULL;
	serio_continue_rx(parent->ps2dev.serio);
}

static int synaptics_is_pt_packet(u8 *buf)
{
	return (buf[0] & 0xFC) == 0x84 && (buf[3] & 0xCC) == 0xC4;
}

static void synaptics_pass_pt_packet(struct serio *ptport, u8 *packet)
{
	struct psmouse *child = serio_get_drvdata(ptport);

	if (child && child->state == PSMOUSE_ACTIVATED) {
		serio_interrupt(ptport, packet[1], 0);
		serio_interrupt(ptport, packet[4], 0);
		serio_interrupt(ptport, packet[5], 0);
		if (child->pktsize == 4)
			serio_interrupt(ptport, packet[2], 0);
	} else {
		serio_interrupt(ptport, packet[1], 0);
	}
}

static void synaptics_pt_activate(struct psmouse *psmouse)
{
	struct synaptics_data *priv = psmouse->private;
	struct psmouse *child = serio_get_drvdata(priv->pt_port);

	/* adjust the touchpad to child's choice of protocol */
	if (child) {
		if (child->pktsize == 4)
			priv->mode |= SYN_BIT_FOUR_BYTE_CLIENT;
		else
			priv->mode &= ~SYN_BIT_FOUR_BYTE_CLIENT;

		if (synaptics_mode_cmd(psmouse, priv->mode))
			psmouse_warn(psmouse,
				     "failed to switch guest protocol\n");
	}
}

static void synaptics_pt_create(struct psmouse *psmouse)
{
	struct serio *serio;

	serio = kzalloc(sizeof(struct serio), GFP_KERNEL);
	if (!serio) {
		psmouse_err(psmouse,
			    "not enough memory for pass-through port\n");
		return;
	}

	serio->id.type = SERIO_PS_PSTHRU;
	strlcpy(serio->name, "Synaptics pass-through", sizeof(serio->name));
	strlcpy(serio->phys, "synaptics-pt/serio0", sizeof(serio->phys));
	serio->write = synaptics_pt_write;
	serio->start = synaptics_pt_start;
	serio->stop = synaptics_pt_stop;
	serio->parent = psmouse->ps2dev.serio;

	psmouse->pt_activate = synaptics_pt_activate;

	psmouse_info(psmouse, "serio: %s port at %s\n",
		     serio->name, psmouse->phys);
	serio_register_port(serio);
}

/*****************************************************************************
 *	Functions to interpret the absolute mode packets
 ****************************************************************************/

static void synaptics_parse_agm(const u8 buf[],
				struct synaptics_data *priv,
				struct synaptics_hw_state *hw)
{
	struct synaptics_hw_state *agm = &priv->agm;
	int agm_packet_type;

	agm_packet_type = (buf[5] & 0x30) >> 4;
	switch (agm_packet_type) {
	case 1:
		/* Gesture packet: (x, y, z) half resolution */
		agm->w = hw->w;
		agm->x = (((buf[4] & 0x0f) << 8) | buf[1]) << 1;
		agm->y = (((buf[4] & 0xf0) << 4) | buf[2]) << 1;
		agm->z = ((buf[3] & 0x30) | (buf[5] & 0x0f)) << 1;
		break;

	case 2:
		/* AGM-CONTACT packet: we are only interested in the count */
		priv->agm_count = buf[1];
		break;

	default:
		break;
	}
}

static void synaptics_parse_ext_buttons(const u8 buf[],
					struct synaptics_data *priv,
					struct synaptics_hw_state *hw)
{
	unsigned int ext_bits =
		(SYN_CAP_MULTI_BUTTON_NO(priv->info.ext_cap) + 1) >> 1;
	unsigned int ext_mask = GENMASK(ext_bits - 1, 0);

	hw->ext_buttons = buf[4] & ext_mask;
	hw->ext_buttons |= (buf[5] & ext_mask) << ext_bits;
}

static int synaptics_parse_hw_state(const u8 buf[],
				    struct synaptics_data *priv,
				    struct synaptics_hw_state *hw)
{
	memset(hw, 0, sizeof(struct synaptics_hw_state));

	if (SYN_MODEL_NEWABS(priv->info.model_id)) {
		hw->w = (((buf[0] & 0x30) >> 2) |
			 ((buf[0] & 0x04) >> 1) |
			 ((buf[3] & 0x04) >> 2));

		if (synaptics_has_agm(priv) && hw->w == 2) {
			synaptics_parse_agm(buf, priv, hw);
			return 1;
		}

		hw->x = (((buf[3] & 0x10) << 8) |
			 ((buf[1] & 0x0f) << 8) |
			 buf[4]);
		hw->y = (((buf[3] & 0x20) << 7) |
			 ((buf[1] & 0xf0) << 4) |
			 buf[5]);
		hw->z = buf[2];

		hw->left  = (buf[0] & 0x01) ? 1 : 0;
		hw->right = (buf[0] & 0x02) ? 1 : 0;

		if (priv->is_forcepad) {
			/*
			 * ForcePads, like Clickpads, use middle button
			 * bits to report primary button clicks.
			 * Unfortunately they report primary button not
			 * only when user presses on the pad above certain
			 * threshold, but also when there are more than one
			 * finger on the touchpad, which interferes with
			 * out multi-finger gestures.
			 */
			if (hw->z == 0) {
				/* No contacts */
				priv->press = priv->report_press = false;
			} else if (hw->w >= 4 && ((buf[0] ^ buf[3]) & 0x01)) {
				/*
				 * Single-finger touch with pressure above
				 * the threshold. If pressure stays long
				 * enough, we'll start reporting primary
				 * button. We rely on the device continuing
				 * sending data even if finger does not
				 * move.
				 */
				if  (!priv->press) {
					priv->press_start = jiffies;
					priv->press = true;
				} else if (time_after(jiffies,
						priv->press_start +
							msecs_to_jiffies(50))) {
					priv->report_press = true;
				}
			} else {
				priv->press = false;
			}

			hw->left = priv->report_press;

		} else if (SYN_CAP_CLICKPAD(priv->info.ext_cap_0c)) {
			/*
			 * Clickpad's button is transmitted as middle button,
			 * however, since it is primary button, we will report
			 * it as BTN_LEFT.
			 */
			hw->left = ((buf[0] ^ buf[3]) & 0x01) ? 1 : 0;

		} else if (SYN_CAP_MIDDLE_BUTTON(priv->info.capabilities)) {
			hw->middle = ((buf[0] ^ buf[3]) & 0x01) ? 1 : 0;
			if (hw->w == 2)
				hw->scroll = (s8)buf[1];
		}

		if (SYN_CAP_FOUR_BUTTON(priv->info.capabilities)) {
			hw->up   = ((buf[0] ^ buf[3]) & 0x01) ? 1 : 0;
			hw->down = ((buf[0] ^ buf[3]) & 0x02) ? 1 : 0;
		}

		if (SYN_CAP_MULTI_BUTTON_NO(priv->info.ext_cap) > 0 &&
		    ((buf[0] ^ buf[3]) & 0x02)) {
			synaptics_parse_ext_buttons(buf, priv, hw);
		}
	} else {
		hw->x = (((buf[1] & 0x1f) << 8) | buf[2]);
		hw->y = (((buf[4] & 0x1f) << 8) | buf[5]);

		hw->z = (((buf[0] & 0x30) << 2) | (buf[3] & 0x3F));
		hw->w = (((buf[1] & 0x80) >> 4) | ((buf[0] & 0x04) >> 1));

		hw->left  = (buf[0] & 0x01) ? 1 : 0;
		hw->right = (buf[0] & 0x02) ? 1 : 0;
	}

	/*
	 * Convert wrap-around values to negative. (X|Y)_MAX_POSITIVE
	 * is used by some firmware to indicate a finger at the edge of
	 * the touchpad whose precise position cannot be determined, so
	 * convert these values to the maximum axis value.
	 */
	if (hw->x > X_MAX_POSITIVE)
		hw->x -= 1 << ABS_POS_BITS;
	else if (hw->x == X_MAX_POSITIVE)
		hw->x = XMAX;

	if (hw->y > Y_MAX_POSITIVE)
		hw->y -= 1 << ABS_POS_BITS;
	else if (hw->y == Y_MAX_POSITIVE)
		hw->y = YMAX;

	return 0;
}

static void synaptics_report_semi_mt_slot(struct input_dev *dev, int slot,
					  bool active, int x, int y)
{
	input_mt_slot(dev, slot);
	input_mt_report_slot_state(dev, MT_TOOL_FINGER, active);
	if (active) {
		input_report_abs(dev, ABS_MT_POSITION_X, x);
		input_report_abs(dev, ABS_MT_POSITION_Y, synaptics_invert_y(y));
	}
}

static void synaptics_report_semi_mt_data(struct input_dev *dev,
					  const struct synaptics_hw_state *a,
					  const struct synaptics_hw_state *b,
					  int num_fingers)
{
	if (num_fingers >= 2) {
		synaptics_report_semi_mt_slot(dev, 0, true, min(a->x, b->x),
					      min(a->y, b->y));
		synaptics_report_semi_mt_slot(dev, 1, true, max(a->x, b->x),
					      max(a->y, b->y));
	} else if (num_fingers == 1) {
		synaptics_report_semi_mt_slot(dev, 0, true, a->x, a->y);
		synaptics_report_semi_mt_slot(dev, 1, false, 0, 0);
	} else {
		synaptics_report_semi_mt_slot(dev, 0, false, 0, 0);
		synaptics_report_semi_mt_slot(dev, 1, false, 0, 0);
	}
}

static void synaptics_report_ext_buttons(struct psmouse *psmouse,
					 const struct synaptics_hw_state *hw)
{
	struct input_dev *dev = psmouse->dev;
	struct synaptics_data *priv = psmouse->private;
	int ext_bits = (SYN_CAP_MULTI_BUTTON_NO(priv->info.ext_cap) + 1) >> 1;
	int i;

	if (!SYN_CAP_MULTI_BUTTON_NO(priv->info.ext_cap))
		return;

	/* Bug in FW 8.1 & 8.2, buttons are reported only when ExtBit is 1 */
	if ((SYN_ID_FULL(priv->info.identity) == 0x801 ||
	     SYN_ID_FULL(priv->info.identity) == 0x802) &&
	    !((psmouse->packet[0] ^ psmouse->packet[3]) & 0x02))
		return;

	if (!SYN_CAP_EXT_BUTTONS_STICK(priv->info.ext_cap_10)) {
		for (i = 0; i < ext_bits; i++) {
			input_report_key(dev, BTN_0 + 2 * i,
				hw->ext_buttons & BIT(i));
			input_report_key(dev, BTN_1 + 2 * i,
				hw->ext_buttons & BIT(i + ext_bits));
		}
		return;
	}

	/*
	 * This generation of touchpads has the trackstick buttons
	 * physically wired to the touchpad. Re-route them through
	 * the pass-through interface.
	 */
	if (priv->pt_port) {
		u8 pt_buttons;

		/* The trackstick expects at most 3 buttons */
		pt_buttons = SYN_EXT_BUTTON_STICK_L(hw->ext_buttons)      |
			     SYN_EXT_BUTTON_STICK_R(hw->ext_buttons) << 1 |
			     SYN_EXT_BUTTON_STICK_M(hw->ext_buttons) << 2;

		serio_interrupt(priv->pt_port,
				PSMOUSE_OOB_EXTRA_BTNS, SERIO_OOB_DATA);
		serio_interrupt(priv->pt_port, pt_buttons, SERIO_OOB_DATA);
	}
}

static void synaptics_report_buttons(struct psmouse *psmouse,
				     const struct synaptics_hw_state *hw)
{
	struct input_dev *dev = psmouse->dev;
	struct synaptics_data *priv = psmouse->private;

	input_report_key(dev, BTN_LEFT, hw->left);
	input_report_key(dev, BTN_RIGHT, hw->right);

	if (SYN_CAP_MIDDLE_BUTTON(priv->info.capabilities))
		input_report_key(dev, BTN_MIDDLE, hw->middle);

	if (SYN_CAP_FOUR_BUTTON(priv->info.capabilities)) {
		input_report_key(dev, BTN_FORWARD, hw->up);
		input_report_key(dev, BTN_BACK, hw->down);
	}

	synaptics_report_ext_buttons(psmouse, hw);
}

static void synaptics_report_mt_data(struct psmouse *psmouse,
				     const struct synaptics_hw_state *sgm,
				     int num_fingers)
{
	struct input_dev *dev = psmouse->dev;
	struct synaptics_data *priv = psmouse->private;
	const struct synaptics_hw_state *hw[2] = { sgm, &priv->agm };
	struct input_mt_pos pos[2];
	int slot[2], nsemi, i;

	nsemi = clamp_val(num_fingers, 0, 2);

	for (i = 0; i < nsemi; i++) {
		pos[i].x = hw[i]->x;
		pos[i].y = synaptics_invert_y(hw[i]->y);
	}

	input_mt_assign_slots(dev, slot, pos, nsemi, DMAX * priv->info.x_res);

	for (i = 0; i < nsemi; i++) {
		input_mt_slot(dev, slot[i]);
		input_mt_report_slot_state(dev, MT_TOOL_FINGER, true);
		input_report_abs(dev, ABS_MT_POSITION_X, pos[i].x);
		input_report_abs(dev, ABS_MT_POSITION_Y, pos[i].y);
		input_report_abs(dev, ABS_MT_PRESSURE, hw[i]->z);
	}

	input_mt_drop_unused(dev);

	/* Don't use active slot count to generate BTN_TOOL events. */
	input_mt_report_pointer_emulation(dev, false);

	/* Send the number of fingers reported by touchpad itself. */
	input_mt_report_finger_count(dev, num_fingers);

	synaptics_report_buttons(psmouse, sgm);

	input_sync(dev);
}

static void synaptics_image_sensor_process(struct psmouse *psmouse,
					   struct synaptics_hw_state *sgm)
{
	struct synaptics_data *priv = psmouse->private;
	int num_fingers;

	/*
	 * Update mt_state using the new finger count and current mt_state.
	 */
	if (sgm->z == 0)
		num_fingers = 0;
	else if (sgm->w >= 4)
		num_fingers = 1;
	else if (sgm->w == 0)
		num_fingers = 2;
	else if (sgm->w == 1)
		num_fingers = priv->agm_count ? priv->agm_count : 3;
	else
		num_fingers = 4;

	/* Send resulting input events to user space */
	synaptics_report_mt_data(psmouse, sgm, num_fingers);
}

static bool synaptics_has_multifinger(struct synaptics_data *priv)
{
	if (SYN_CAP_MULTIFINGER(priv->info.capabilities))
		return true;

	/* Advanced gesture mode also sends multi finger data */
	return synaptics_has_agm(priv);
}

/*
 *  called for each full received packet from the touchpad
 */
static void synaptics_process_packet(struct psmouse *psmouse)
{
	struct input_dev *dev = psmouse->dev;
	struct synaptics_data *priv = psmouse->private;
	struct synaptics_device_info *info = &priv->info;
	struct synaptics_hw_state hw;
	int num_fingers;
	int finger_width;

	if (synaptics_parse_hw_state(psmouse->packet, priv, &hw))
		return;

	if (SYN_CAP_IMAGE_SENSOR(info->ext_cap_0c)) {
		synaptics_image_sensor_process(psmouse, &hw);
		return;
	}

	if (hw.scroll) {
		priv->scroll += hw.scroll;

		while (priv->scroll >= 4) {
			input_report_key(dev, BTN_BACK, !hw.down);
			input_sync(dev);
			input_report_key(dev, BTN_BACK, hw.down);
			input_sync(dev);
			priv->scroll -= 4;
		}
		while (priv->scroll <= -4) {
			input_report_key(dev, BTN_FORWARD, !hw.up);
			input_sync(dev);
			input_report_key(dev, BTN_FORWARD, hw.up);
			input_sync(dev);
			priv->scroll += 4;
		}
		return;
	}

	if (hw.z > 0 && hw.x > 1) {
		num_fingers = 1;
		finger_width = 5;
		if (SYN_CAP_EXTENDED(info->capabilities)) {
			switch (hw.w) {
			case 0 ... 1:
				if (synaptics_has_multifinger(priv))
					num_fingers = hw.w + 2;
				break;
			case 2:
				if (SYN_MODEL_PEN(info->model_id))
					;   /* Nothing, treat a pen as a single finger */
				break;
			case 4 ... 15:
				if (SYN_CAP_PALMDETECT(info->capabilities))
					finger_width = hw.w;
				break;
			}
		}
	} else {
		num_fingers = 0;
		finger_width = 0;
	}

	if (cr48_profile_sensor) {
		synaptics_report_mt_data(psmouse, &hw, num_fingers);
		return;
	}

	if (SYN_CAP_ADV_GESTURE(info->ext_cap_0c))
		synaptics_report_semi_mt_data(dev, &hw, &priv->agm,
					      num_fingers);

	/* Post events
	 * BTN_TOUCH has to be first as mousedev relies on it when doing
	 * absolute -> relative conversion
	 */
	if (hw.z > 30) input_report_key(dev, BTN_TOUCH, 1);
	if (hw.z < 25) input_report_key(dev, BTN_TOUCH, 0);

	if (num_fingers > 0) {
		input_report_abs(dev, ABS_X, hw.x);
		input_report_abs(dev, ABS_Y, synaptics_invert_y(hw.y));
	}
	input_report_abs(dev, ABS_PRESSURE, hw.z);

	if (SYN_CAP_PALMDETECT(info->capabilities))
		input_report_abs(dev, ABS_TOOL_WIDTH, finger_width);

	input_report_key(dev, BTN_TOOL_FINGER, num_fingers == 1);
	if (synaptics_has_multifinger(priv)) {
		input_report_key(dev, BTN_TOOL_DOUBLETAP, num_fingers == 2);
		input_report_key(dev, BTN_TOOL_TRIPLETAP, num_fingers == 3);
	}

	synaptics_report_buttons(psmouse, &hw);

	input_sync(dev);
}

static bool synaptics_validate_byte(struct psmouse *psmouse,
				    int idx, enum synaptics_pkt_type pkt_type)
{
	static const u8 newabs_mask[]	  = { 0xC8, 0x00, 0x00, 0xC8, 0x00 };
	static const u8 newabs_rel_mask[] = { 0xC0, 0x00, 0x00, 0xC0, 0x00 };
	static const u8 newabs_rslt[]	  = { 0x80, 0x00, 0x00, 0xC0, 0x00 };
	static const u8 oldabs_mask[]	  = { 0xC0, 0x60, 0x00, 0xC0, 0x60 };
	static const u8 oldabs_rslt[]	  = { 0xC0, 0x00, 0x00, 0x80, 0x00 };
	const u8 *packet = psmouse->packet;

	if (idx < 0 || idx > 4)
		return false;

	switch (pkt_type) {

	case SYN_NEWABS:
	case SYN_NEWABS_RELAXED:
		return (packet[idx] & newabs_rel_mask[idx]) == newabs_rslt[idx];

	case SYN_NEWABS_STRICT:
		return (packet[idx] & newabs_mask[idx]) == newabs_rslt[idx];

	case SYN_OLDABS:
		return (packet[idx] & oldabs_mask[idx]) == oldabs_rslt[idx];

	default:
		psmouse_err(psmouse, "unknown packet type %d\n", pkt_type);
		return false;
	}
}

static enum synaptics_pkt_type
synaptics_detect_pkt_type(struct psmouse *psmouse)
{
	int i;

	for (i = 0; i < 5; i++) {
		if (!synaptics_validate_byte(psmouse, i, SYN_NEWABS_STRICT)) {
			psmouse_info(psmouse, "using relaxed packet validation\n");
			return SYN_NEWABS_RELAXED;
		}
	}

	return SYN_NEWABS_STRICT;
}

static psmouse_ret_t synaptics_process_byte(struct psmouse *psmouse)
{
	struct synaptics_data *priv = psmouse->private;

	if (psmouse->pktcnt >= 6) { /* Full packet received */
		if (unlikely(priv->pkt_type == SYN_NEWABS))
			priv->pkt_type = synaptics_detect_pkt_type(psmouse);

		if (SYN_CAP_PASS_THROUGH(priv->info.capabilities) &&
		    synaptics_is_pt_packet(psmouse->packet)) {
			if (priv->pt_port)
				synaptics_pass_pt_packet(priv->pt_port,
							 psmouse->packet);
		} else
			synaptics_process_packet(psmouse);

		return PSMOUSE_FULL_PACKET;
	}

	return synaptics_validate_byte(psmouse, psmouse->pktcnt - 1, priv->pkt_type) ?
		PSMOUSE_GOOD_DATA : PSMOUSE_BAD_DATA;
}

/*****************************************************************************
 *	Driver initialization/cleanup functions
 ****************************************************************************/
static void set_abs_position_params(struct input_dev *dev,
				    struct synaptics_device_info *info,
				    int x_code, int y_code)
{
	int x_min = info->x_min ?: XMIN_NOMINAL;
	int x_max = info->x_max ?: XMAX_NOMINAL;
	int y_min = info->y_min ?: YMIN_NOMINAL;
	int y_max = info->y_max ?: YMAX_NOMINAL;
	int fuzz = SYN_CAP_REDUCED_FILTERING(info->ext_cap_0c) ?
			SYN_REDUCED_FILTER_FUZZ : 0;

	input_set_abs_params(dev, x_code, x_min, x_max, fuzz, 0);
	input_set_abs_params(dev, y_code, y_min, y_max, fuzz, 0);
	input_abs_set_res(dev, x_code, info->x_res);
	input_abs_set_res(dev, y_code, info->y_res);
}

static int set_input_params(struct psmouse *psmouse,
			    struct synaptics_data *priv)
{
	struct input_dev *dev = psmouse->dev;
	struct synaptics_device_info *info = &priv->info;
	int i;
	int error;

	/* Reset default psmouse capabilities */
	__clear_bit(EV_REL, dev->evbit);
	bitmap_zero(dev->relbit, REL_CNT);
	bitmap_zero(dev->keybit, KEY_CNT);

	/* Things that apply to both modes */
	__set_bit(INPUT_PROP_POINTER, dev->propbit);

	input_set_capability(dev, EV_KEY, BTN_LEFT);

	/* Clickpads report only left button */
	if (!SYN_CAP_CLICKPAD(info->ext_cap_0c)) {
		input_set_capability(dev, EV_KEY, BTN_RIGHT);
		if (SYN_CAP_MIDDLE_BUTTON(info->capabilities))
			input_set_capability(dev, EV_KEY, BTN_MIDDLE);
	}

	if (!priv->absolute_mode) {
		/* Relative mode */
		input_set_capability(dev, EV_REL, REL_X);
		input_set_capability(dev, EV_REL, REL_Y);
		return 0;
	}

	/* Absolute mode */
	set_abs_position_params(dev, &priv->info, ABS_X, ABS_Y);
	input_set_abs_params(dev, ABS_PRESSURE, 0, 255, 0, 0);

	if (cr48_profile_sensor)
		input_set_abs_params(dev, ABS_MT_PRESSURE, 0, 255, 0, 0);

	if (SYN_CAP_IMAGE_SENSOR(info->ext_cap_0c)) {
		set_abs_position_params(dev, info,
					ABS_MT_POSITION_X, ABS_MT_POSITION_Y);
		/* Image sensors can report per-contact pressure */
		input_set_abs_params(dev, ABS_MT_PRESSURE, 0, 255, 0, 0);

		error = input_mt_init_slots(dev, 2,
					    INPUT_MT_POINTER | INPUT_MT_TRACK);
		if (error)
			return error;

		/* Image sensors can signal 4 and 5 finger clicks */
		input_set_capability(dev, EV_KEY, BTN_TOOL_QUADTAP);
		input_set_capability(dev, EV_KEY, BTN_TOOL_QUINTTAP);
	} else if (SYN_CAP_ADV_GESTURE(info->ext_cap_0c)) {
		set_abs_position_params(dev, info,
					ABS_MT_POSITION_X, ABS_MT_POSITION_Y);
		/*
		 * Profile sensor in CR-48 tracks contacts reasonably well,
		 * other non-image sensors with AGM use semi-mt.
		 */
		error = input_mt_init_slots(dev, 2,
					    INPUT_MT_POINTER |
					     (cr48_profile_sensor ?
					      INPUT_MT_TRACK :
					      INPUT_MT_SEMI_MT));
		if (error)
			return error;

		/*
		 * For semi-mt devices we send ABS_X/Y ourselves instead of
		 * input_mt_report_pointer_emulation. But
		 * input_mt_init_slots() resets the fuzz to 0, leading to a
		 * filtered ABS_MT_POSITION_X but an unfiltered ABS_X
		 * position. Let's re-initialize ABS_X/Y here.
		 */
		if (!cr48_profile_sensor)
			set_abs_position_params(dev, &priv->info, ABS_X, ABS_Y);
	}

	if (SYN_CAP_PALMDETECT(info->capabilities))
		input_set_abs_params(dev, ABS_TOOL_WIDTH, 0, 15, 0, 0);

	input_set_capability(dev, EV_KEY, BTN_TOUCH);
	input_set_capability(dev, EV_KEY, BTN_TOOL_FINGER);

	if (synaptics_has_multifinger(priv)) {
		input_set_capability(dev, EV_KEY, BTN_TOOL_DOUBLETAP);
		input_set_capability(dev, EV_KEY, BTN_TOOL_TRIPLETAP);
	}

	if (SYN_CAP_FOUR_BUTTON(info->capabilities) ||
	    SYN_CAP_MIDDLE_BUTTON(info->capabilities)) {
		input_set_capability(dev, EV_KEY, BTN_FORWARD);
		input_set_capability(dev, EV_KEY, BTN_BACK);
	}

	if (!SYN_CAP_EXT_BUTTONS_STICK(info->ext_cap_10))
		for (i = 0; i < SYN_CAP_MULTI_BUTTON_NO(info->ext_cap); i++)
			input_set_capability(dev, EV_KEY, BTN_0 + i);

	if (SYN_CAP_CLICKPAD(info->ext_cap_0c)) {
		__set_bit(INPUT_PROP_BUTTONPAD, dev->propbit);
		if (psmouse_matches_pnp_id(psmouse, topbuttonpad_pnp_ids) &&
		    !SYN_CAP_EXT_BUTTONS_STICK(info->ext_cap_10))
			__set_bit(INPUT_PROP_TOPBUTTONPAD, dev->propbit);
	}

	return 0;
}

static ssize_t synaptics_show_disable_gesture(struct psmouse *psmouse,
					      void *data, char *buf)
{
	struct synaptics_data *priv = psmouse->private;

	return sprintf(buf, "%c\n", priv->disable_gesture ? '1' : '0');
}

static ssize_t synaptics_set_disable_gesture(struct psmouse *psmouse,
					     void *data, const char *buf,
					     size_t len)
{
	struct synaptics_data *priv = psmouse->private;
	unsigned int value;
	int err;

	err = kstrtouint(buf, 10, &value);
	if (err)
		return err;

	if (value > 1)
		return -EINVAL;

	if (value == priv->disable_gesture)
		return len;

	priv->disable_gesture = value;
	if (value)
		priv->mode |= SYN_BIT_DISABLE_GESTURE;
	else
		priv->mode &= ~SYN_BIT_DISABLE_GESTURE;

	if (synaptics_mode_cmd(psmouse, priv->mode))
		return -EIO;

	return len;
}

PSMOUSE_DEFINE_ATTR(disable_gesture, S_IWUSR | S_IRUGO, NULL,
		    synaptics_show_disable_gesture,
		    synaptics_set_disable_gesture);

static void synaptics_disconnect(struct psmouse *psmouse)
{
	struct synaptics_data *priv = psmouse->private;

	/*
	 * We might have left a breadcrumb when trying to
	 * set up SMbus companion.
	 */
	psmouse_smbus_cleanup(psmouse);

	if (!priv->absolute_mode &&
			SYN_ID_DISGEST_SUPPORTED(priv->info.identity))
		device_remove_file(&psmouse->ps2dev.serio->dev,
				   &psmouse_attr_disable_gesture.dattr);

	synaptics_reset(psmouse);
	kfree(priv);
	psmouse->private = NULL;
}

static int synaptics_reconnect(struct psmouse *psmouse)
{
	struct synaptics_data *priv = psmouse->private;
	struct synaptics_device_info info;
	u8 param[2];
	int retry = 0;
	int error;

	do {
		psmouse_reset(psmouse);
		if (retry) {
			/*
			 * On some boxes, right after resuming, the touchpad
			 * needs some time to finish initializing (I assume
			 * it needs time to calibrate) and start responding
			 * to Synaptics-specific queries, so let's wait a
			 * bit.
			 */
			ssleep(1);
		}
		ps2_command(&psmouse->ps2dev, param, PSMOUSE_CMD_GETID);
		error = synaptics_detect(psmouse, 0);
	} while (error && ++retry < 3);

	if (error)
		return error;

	if (retry > 1)
		psmouse_dbg(psmouse, "reconnected after %d tries\n", retry);

	error = synaptics_query_hardware(psmouse, &info);
	if (error) {
		psmouse_err(psmouse, "Unable to query device.\n");
		return error;
	}

	error = synaptics_set_mode(psmouse);
	if (error) {
		psmouse_err(psmouse, "Unable to initialize device.\n");
		return error;
	}

	if (info.identity != priv->info.identity ||
	    info.model_id != priv->info.model_id ||
	    info.capabilities != priv->info.capabilities ||
	    info.ext_cap != priv->info.ext_cap) {
		psmouse_err(psmouse,
			    "hardware appears to be different: id(%u-%u), model(%u-%u), caps(%x-%x), ext(%x-%x).\n",
			    priv->info.identity, info.identity,
			    priv->info.model_id, info.model_id,
			    priv->info.capabilities, info.capabilities,
			    priv->info.ext_cap, info.ext_cap);
		return -ENXIO;
	}

	return 0;
}

static bool impaired_toshiba_kbc;

static const struct dmi_system_id toshiba_dmi_table[] __initconst = {
#if defined(CONFIG_DMI) && defined(CONFIG_X86)
	{
		/* Toshiba Satellite */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Satellite"),
		},
	},
	{
		/* Toshiba Dynabook */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "dynabook"),
		},
	},
	{
		/* Toshiba Portege M300 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "PORTEGE M300"),
		},

	},
	{
		/* Toshiba Portege M300 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Portable PC"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "Version 1.0"),
		},

	},
#endif
	{ }
};

static bool broken_olpc_ec;

static const struct dmi_system_id olpc_dmi_table[] __initconst = {
#if defined(CONFIG_DMI) && defined(CONFIG_OLPC)
	{
		/* OLPC XO-1 or XO-1.5 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "OLPC"),
			DMI_MATCH(DMI_PRODUCT_NAME, "XO"),
		},
	},
#endif
	{ }
};

static const struct dmi_system_id __initconst cr48_dmi_table[] = {
#if defined(CONFIG_DMI) && defined(CONFIG_X86)
	{
		/* Cr-48 Chromebook (Codename Mario) */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "IEC"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Mario"),
		},
	},
#endif
	{ }
};

void __init synaptics_module_init(void)
{
	impaired_toshiba_kbc = dmi_check_system(toshiba_dmi_table);
	broken_olpc_ec = dmi_check_system(olpc_dmi_table);
	cr48_profile_sensor = dmi_check_system(cr48_dmi_table);
}

static int synaptics_init_ps2(struct psmouse *psmouse,
			      struct synaptics_device_info *info,
			      bool absolute_mode)
{
	struct synaptics_data *priv;
	int err;

	synaptics_apply_quirks(psmouse, info);

	psmouse->private = priv = kzalloc(sizeof(struct synaptics_data), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->info = *info;
	priv->absolute_mode = absolute_mode;
	if (SYN_ID_DISGEST_SUPPORTED(info->identity))
		priv->disable_gesture = true;

	/*
	 * Unfortunately ForcePad capability is not exported over PS/2,
	 * so we have to resort to checking PNP IDs.
	 */
	priv->is_forcepad = psmouse_matches_pnp_id(psmouse, forcepad_pnp_ids);

	err = synaptics_set_mode(psmouse);
	if (err) {
		psmouse_err(psmouse, "Unable to initialize device.\n");
		goto init_fail;
	}

	priv->pkt_type = SYN_MODEL_NEWABS(info->model_id) ?
					SYN_NEWABS : SYN_OLDABS;

	psmouse_info(psmouse,
		     "Touchpad model: %lu, fw: %lu.%lu, id: %#x, caps: %#x/%#x/%#x/%#x, board id: %u, fw id: %u\n",
		     SYN_ID_MODEL(info->identity),
		     SYN_ID_MAJOR(info->identity), SYN_ID_MINOR(info->identity),
		     info->model_id,
		     info->capabilities, info->ext_cap, info->ext_cap_0c,
		     info->ext_cap_10, info->board_id, info->firmware_id);

	err = set_input_params(psmouse, priv);
	if (err) {
		psmouse_err(psmouse,
			    "failed to set up capabilities: %d\n", err);
		goto init_fail;
	}

	/*
	 * Encode touchpad model so that it can be used to set
	 * input device->id.version and be visible to userspace.
	 * Because version is __u16 we have to drop something.
	 * Hardware info bits seem to be good candidates as they
	 * are documented to be for Synaptics corp. internal use.
	 */
	psmouse->model = ((info->model_id & 0x00ff0000) >> 8) |
			  (info->model_id & 0x000000ff);

	if (absolute_mode) {
		psmouse->protocol_handler = synaptics_process_byte;
		psmouse->pktsize = 6;
	} else {
		/* Relative mode follows standard PS/2 mouse protocol */
		psmouse->protocol_handler = psmouse_process_byte;
		psmouse->pktsize = 3;
	}

	psmouse->set_rate = synaptics_set_rate;
	psmouse->disconnect = synaptics_disconnect;
	psmouse->reconnect = synaptics_reconnect;
	psmouse->cleanup = synaptics_reset;
	/* Synaptics can usually stay in sync without extra help */
	psmouse->resync_time = 0;

	if (SYN_CAP_PASS_THROUGH(info->capabilities))
		synaptics_pt_create(psmouse);

	/*
	 * Toshiba's KBC seems to have trouble handling data from
	 * Synaptics at full rate.  Switch to a lower rate (roughly
	 * the same rate as a standard PS/2 mouse).
	 */
	if (psmouse->rate >= 80 && impaired_toshiba_kbc) {
		psmouse_info(psmouse,
			     "Toshiba %s detected, limiting rate to 40pps.\n",
			     dmi_get_system_info(DMI_PRODUCT_NAME));
		psmouse->rate = 40;
	}

	if (!priv->absolute_mode && SYN_ID_DISGEST_SUPPORTED(info->identity)) {
		err = device_create_file(&psmouse->ps2dev.serio->dev,
					 &psmouse_attr_disable_gesture.dattr);
		if (err) {
			psmouse_err(psmouse,
				    "Failed to create disable_gesture attribute (%d)",
				    err);
			goto init_fail;
		}
	}

	return 0;

 init_fail:
	kfree(priv);
	return err;
}

static int __synaptics_init(struct psmouse *psmouse, bool absolute_mode)
{
	struct synaptics_device_info info;
	int error;

	psmouse_reset(psmouse);

	error = synaptics_query_hardware(psmouse, &info);
	if (error) {
		psmouse_err(psmouse, "Unable to query device: %d\n", error);
		return error;
	}

	return synaptics_init_ps2(psmouse, &info, absolute_mode);
}

int synaptics_init_absolute(struct psmouse *psmouse)
{
	return __synaptics_init(psmouse, true);
}

int synaptics_init_relative(struct psmouse *psmouse)
{
	return __synaptics_init(psmouse, false);
}

static int synaptics_setup_ps2(struct psmouse *psmouse,
			       struct synaptics_device_info *info)
{
	bool absolute_mode = true;
	int error;

	/*
	 * The OLPC XO has issues with Synaptics' absolute mode; the constant
	 * packet spew overloads the EC such that key presses on the keyboard
	 * are missed.  Given that, don't even attempt to use Absolute mode.
	 * Relative mode seems to work just fine.
	 */
	if (broken_olpc_ec) {
		psmouse_info(psmouse,
			     "OLPC XO detected, forcing relative protocol.\n");
		absolute_mode = false;
	}

	error = synaptics_init_ps2(psmouse, info, absolute_mode);
	if (error)
		return error;

	return absolute_mode ? PSMOUSE_SYNAPTICS : PSMOUSE_SYNAPTICS_RELATIVE;
}

#else /* CONFIG_MOUSE_PS2_SYNAPTICS */

void __init synaptics_module_init(void)
{
}

static int __maybe_unused
synaptics_setup_ps2(struct psmouse *psmouse,
		    struct synaptics_device_info *info)
{
	return -ENOSYS;
}

#endif /* CONFIG_MOUSE_PS2_SYNAPTICS */

#ifdef CONFIG_MOUSE_PS2_SYNAPTICS_SMBUS

/*
 * The newest Synaptics device can use a secondary bus (called InterTouch) which
 * provides a better bandwidth and allow a better control of the touchpads.
 * This is used to decide if we need to use this bus or not.
 */
enum {
	SYNAPTICS_INTERTOUCH_NOT_SET = -1,
	SYNAPTICS_INTERTOUCH_OFF,
	SYNAPTICS_INTERTOUCH_ON,
};

static int synaptics_intertouch = IS_ENABLED(CONFIG_RMI4_SMB) ?
		SYNAPTICS_INTERTOUCH_NOT_SET : SYNAPTICS_INTERTOUCH_OFF;
module_param_named(synaptics_intertouch, synaptics_intertouch, int, 0644);
MODULE_PARM_DESC(synaptics_intertouch, "Use a secondary bus for the Synaptics device.");

static int synaptics_create_intertouch(struct psmouse *psmouse,
				       struct synaptics_device_info *info,
				       bool leave_breadcrumbs)
{
	bool topbuttonpad =
		psmouse_matches_pnp_id(psmouse, topbuttonpad_pnp_ids) &&
		!SYN_CAP_EXT_BUTTONS_STICK(info->ext_cap_10);
	const struct rmi_device_platform_data pdata = {
		.sensor_pdata = {
			.sensor_type = rmi_sensor_touchpad,
			.axis_align.flip_y = true,
			.kernel_tracking = false,
			.topbuttonpad = topbuttonpad,
		},
		.gpio_data = {
			.buttonpad = SYN_CAP_CLICKPAD(info->ext_cap_0c),
			.trackstick_buttons =
				!!SYN_CAP_EXT_BUTTONS_STICK(info->ext_cap_10),
		},
	};
	const struct i2c_board_info intertouch_board = {
		I2C_BOARD_INFO("rmi4_smbus", 0x2c),
		.flags = I2C_CLIENT_HOST_NOTIFY,
	};

	return psmouse_smbus_init(psmouse, &intertouch_board,
				  &pdata, sizeof(pdata), true,
				  leave_breadcrumbs);
}

/**
 * synaptics_setup_intertouch - called once the PS/2 devices are enumerated
 * and decides to instantiate a SMBus InterTouch device.
 */
static int synaptics_setup_intertouch(struct psmouse *psmouse,
				      struct synaptics_device_info *info,
				      bool leave_breadcrumbs)
{
	int error;

	if (synaptics_intertouch == SYNAPTICS_INTERTOUCH_OFF)
		return -ENXIO;

	if (synaptics_intertouch == SYNAPTICS_INTERTOUCH_NOT_SET) {
		if (!psmouse_matches_pnp_id(psmouse, topbuttonpad_pnp_ids) &&
		    !psmouse_matches_pnp_id(psmouse, smbus_pnp_ids)) {

			if (!psmouse_matches_pnp_id(psmouse, forcepad_pnp_ids))
				psmouse_info(psmouse,
					     "Your touchpad (%s) says it can support a different bus. "
					     "If i2c-hid and hid-rmi are not used, you might want to try setting psmouse.synaptics_intertouch to 1 and report this to linux-input@vger.kernel.org.\n",
					     psmouse->ps2dev.serio->firmware_id);

			return -ENXIO;
		}
	}

	psmouse_info(psmouse, "Trying to set up SMBus access\n");

	error = synaptics_create_intertouch(psmouse, info, leave_breadcrumbs);
	if (error) {
		if (error == -EAGAIN)
			psmouse_info(psmouse, "SMbus companion is not ready yet\n");
		else
			psmouse_err(psmouse, "unable to create intertouch device\n");

		return error;
	}

	return 0;
}

int synaptics_init_smbus(struct psmouse *psmouse)
{
	struct synaptics_device_info info;
	int error;

	psmouse_reset(psmouse);

	error = synaptics_query_hardware(psmouse, &info);
	if (error) {
		psmouse_err(psmouse, "Unable to query device: %d\n", error);
		return error;
	}

	if (!SYN_CAP_INTERTOUCH(info.ext_cap_0c))
		return -ENXIO;

	return synaptics_create_intertouch(psmouse, &info, false);
}

#else /* CONFIG_MOUSE_PS2_SYNAPTICS_SMBUS */

static int __maybe_unused
synaptics_setup_intertouch(struct psmouse *psmouse,
			   struct synaptics_device_info *info,
			   bool leave_breadcrumbs)
{
	return -ENOSYS;
}

int synaptics_init_smbus(struct psmouse *psmouse)
{
	return -ENOSYS;
}

#endif /* CONFIG_MOUSE_PS2_SYNAPTICS_SMBUS */

#if defined(CONFIG_MOUSE_PS2_SYNAPTICS) || \
    defined(CONFIG_MOUSE_PS2_SYNAPTICS_SMBUS)

int synaptics_init(struct psmouse *psmouse)
{
	struct synaptics_device_info info;
	int error;
	int retval;

	psmouse_reset(psmouse);

	error = synaptics_query_hardware(psmouse, &info);
	if (error) {
		psmouse_err(psmouse, "Unable to query device: %d\n", error);
		return error;
	}

	if (SYN_CAP_INTERTOUCH(info.ext_cap_0c)) {
		if ((!IS_ENABLED(CONFIG_RMI4_SMB) ||
		     !IS_ENABLED(CONFIG_MOUSE_PS2_SYNAPTICS_SMBUS)) &&
		    /* Forcepads need F21, which is not ready */
		    !psmouse_matches_pnp_id(psmouse, forcepad_pnp_ids)) {
			psmouse_warn(psmouse,
				     "The touchpad can support a better bus than the too old PS/2 protocol. "
				     "Make sure MOUSE_PS2_SYNAPTICS_SMBUS and RMI4_SMB are enabled to get a better touchpad experience.\n");
		}

		error = synaptics_setup_intertouch(psmouse, &info, true);
		if (!error)
			return PSMOUSE_SYNAPTICS_SMBUS;
	}

	retval = synaptics_setup_ps2(psmouse, &info);
	if (retval < 0) {
		/*
		 * Not using any flavor of Synaptics support, so clean up
		 * SMbus breadcrumbs, if any.
		 */
		psmouse_smbus_cleanup(psmouse);
	}

	return retval;
}

#else /* CONFIG_MOUSE_PS2_SYNAPTICS || CONFIG_MOUSE_PS2_SYNAPTICS_SMBUS */

int synaptics_init(struct psmouse *psmouse)
{
	return -ENOSYS;
}

#endif /* CONFIG_MOUSE_PS2_SYNAPTICS || CONFIG_MOUSE_PS2_SYNAPTICS_SMBUS */

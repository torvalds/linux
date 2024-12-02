/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _CYPRESS_PS2_H
#define _CYPRESS_PS2_H

#include "psmouse.h"

#define CMD_BITS_MASK 0x03
#define COMPOSIT(x, s) (((x) & CMD_BITS_MASK) << (s))

#define ENCODE_CMD(aa, bb, cc, dd) \
	(COMPOSIT((aa), 6) | COMPOSIT((bb), 4) | COMPOSIT((cc), 2) | COMPOSIT((dd), 0))
#define CYTP_CMD_ABS_NO_PRESSURE_MODE       ENCODE_CMD(0, 1, 0, 0)
#define CYTP_CMD_ABS_WITH_PRESSURE_MODE     ENCODE_CMD(0, 1, 0, 1)
#define CYTP_CMD_SMBUS_MODE                 ENCODE_CMD(0, 1, 1, 0)
#define CYTP_CMD_STANDARD_MODE              ENCODE_CMD(0, 2, 0, 0)  /* not implemented yet. */
#define CYTP_CMD_CYPRESS_REL_MODE           ENCODE_CMD(1, 1, 1, 1)  /* not implemented yet. */
#define CYTP_CMD_READ_CYPRESS_ID            ENCODE_CMD(0, 0, 0, 0)
#define CYTP_CMD_READ_TP_METRICS            ENCODE_CMD(0, 0, 0, 1)
#define CYTP_CMD_SET_HSCROLL_WIDTH(w)       ENCODE_CMD(1, 1, 0, (w))
#define     CYTP_CMD_SET_HSCROLL_MASK       ENCODE_CMD(1, 1, 0, 0)
#define CYTP_CMD_SET_VSCROLL_WIDTH(w)       ENCODE_CMD(1, 2, 0, (w))
#define     CYTP_CMD_SET_VSCROLL_MASK       ENCODE_CMD(1, 2, 0, 0)
#define CYTP_CMD_SET_PALM_GEOMETRY(e)       ENCODE_CMD(1, 2, 1, (e))
#define     CYTP_CMD_PALM_GEMMETRY_MASK     ENCODE_CMD(1, 2, 1, 0)
#define CYTP_CMD_SET_PALM_SENSITIVITY(s)    ENCODE_CMD(1, 2, 2, (s))
#define     CYTP_CMD_PALM_SENSITIVITY_MASK  ENCODE_CMD(1, 2, 2, 0)
#define CYTP_CMD_SET_MOUSE_SENSITIVITY(s)   ENCODE_CMD(1, 3, ((s) >> 2), (s))
#define     CYTP_CMD_MOUSE_SENSITIVITY_MASK ENCODE_CMD(1, 3, 0, 0)
#define CYTP_CMD_REQUEST_BASELINE_STATUS    ENCODE_CMD(2, 0, 0, 1)
#define CYTP_CMD_REQUEST_RECALIBRATION      ENCODE_CMD(2, 0, 0, 3)

#define DECODE_CMD_AA(x) (((x) >> 6) & CMD_BITS_MASK)
#define DECODE_CMD_BB(x) (((x) >> 4) & CMD_BITS_MASK)
#define DECODE_CMD_CC(x) (((x) >> 2) & CMD_BITS_MASK)
#define DECODE_CMD_DD(x) ((x) & CMD_BITS_MASK)

/* Cypress trackpad working mode. */
#define CYTP_BIT_ABS_PRESSURE    (1 << 3)
#define CYTP_BIT_ABS_NO_PRESSURE (1 << 2)
#define CYTP_BIT_CYPRESS_REL     (1 << 1)
#define CYTP_BIT_STANDARD_REL    (1 << 0)
#define CYTP_BIT_REL_MASK (CYTP_BIT_CYPRESS_REL | CYTP_BIT_STANDARD_REL)
#define CYTP_BIT_ABS_MASK (CYTP_BIT_ABS_PRESSURE | CYTP_BIT_ABS_NO_PRESSURE)
#define CYTP_BIT_ABS_REL_MASK (CYTP_BIT_ABS_MASK | CYTP_BIT_REL_MASK)

#define CYTP_BIT_HIGH_RATE       (1 << 4)
/*
 * report mode bit is set, firmware working in Remote Mode.
 * report mode bit is cleared, firmware working in Stream Mode.
 */
#define CYTP_BIT_REPORT_MODE     (1 << 5)

/* scrolling width values for set HSCROLL and VSCROLL width command. */
#define SCROLL_WIDTH_NARROW 1
#define SCROLL_WIDTH_NORMAL 2
#define SCROLL_WIDTH_WIDE   3

#define PALM_GEOMETRY_ENABLE  1
#define PALM_GEOMETRY_DISABLE 0

#define TP_METRICS_MASK  0x80
#define FW_VERSION_MASX    0x7f
#define FW_VER_HIGH_MASK 0x70
#define FW_VER_LOW_MASK  0x0f

/* Times to retry a ps2_command and millisecond delay between tries. */
#define CYTP_PS2_CMD_TRIES 3
#define CYTP_PS2_CMD_DELAY 500

/* time out for PS/2 command only in milliseconds. */
#define CYTP_CMD_TIMEOUT  200
#define CYTP_DATA_TIMEOUT 30

#define CYTP_EXT_CMD   0xe8
#define CYTP_PS2_RETRY 0xfe
#define CYTP_PS2_ERROR 0xfc

#define CYTP_RESP_RETRY 0x01
#define CYTP_RESP_ERROR 0xfe


#define CYTP_105001_WIDTH  97   /* Dell XPS 13 */
#define CYTP_105001_HIGH   59
#define CYTP_DEFAULT_WIDTH (CYTP_105001_WIDTH)
#define CYTP_DEFAULT_HIGH  (CYTP_105001_HIGH)

#define CYTP_ABS_MAX_X     1600
#define CYTP_ABS_MAX_Y     900
#define CYTP_MAX_PRESSURE  255
#define CYTP_MIN_PRESSURE  0

/* header byte bits of relative package. */
#define BTN_LEFT_BIT   0x01
#define BTN_RIGHT_BIT  0x02
#define BTN_MIDDLE_BIT 0x04
#define REL_X_SIGN_BIT 0x10
#define REL_Y_SIGN_BIT 0x20

/* header byte bits of absolute package. */
#define ABS_VSCROLL_BIT 0x10
#define ABS_HSCROLL_BIT 0x20
#define ABS_MULTIFINGER_TAP 0x04
#define ABS_EDGE_MOTION_MASK 0x80

#define DFLT_RESP_BITS_VALID     0x88  /* SMBus bit should not be set. */
#define DFLT_RESP_SMBUS_BIT      0x80
#define   DFLT_SMBUS_MODE        0x80
#define   DFLT_PS2_MODE          0x00
#define DFLT_RESP_BIT_MODE       0x40
#define   DFLT_RESP_REMOTE_MODE  0x40
#define   DFLT_RESP_STREAM_MODE  0x00
#define DFLT_RESP_BIT_REPORTING  0x20
#define DFLT_RESP_BIT_SCALING    0x10

#define TP_METRICS_BIT_PALM               0x80
#define TP_METRICS_BIT_STUBBORN           0x40
#define TP_METRICS_BIT_2F_JITTER          0x30
#define TP_METRICS_BIT_1F_JITTER          0x0c
#define TP_METRICS_BIT_APA                0x02
#define TP_METRICS_BIT_MTG                0x01
#define TP_METRICS_BIT_ABS_PKT_FORMAT_SET 0xf0
#define TP_METRICS_BIT_2F_SPIKE           0x0c
#define TP_METRICS_BIT_1F_SPIKE           0x03

/* bits of first byte response of E9h-Status Request command. */
#define RESP_BTN_RIGHT_BIT  0x01
#define RESP_BTN_MIDDLE_BIT 0x02
#define RESP_BTN_LEFT_BIT   0x04
#define RESP_SCALING_BIT    0x10
#define RESP_ENABLE_BIT     0x20
#define RESP_REMOTE_BIT     0x40
#define RESP_SMBUS_BIT      0x80

#define CYTP_MAX_MT_SLOTS 2

struct cytp_contact {
	int x;
	int y;
	int z;  /* also named as touch pressure. */
};

/* The structure of Cypress Trackpad event data. */
struct cytp_report_data {
	int contact_cnt;
	struct cytp_contact contacts[CYTP_MAX_MT_SLOTS];
	unsigned int left:1;
	unsigned int right:1;
	unsigned int middle:1;
	unsigned int tap:1;  /* multi-finger tap detected. */
};

/* The structure of Cypress Trackpad device private data. */
struct cytp_data {
	int fw_version;

	int pkt_size;
	int mode;

	int tp_min_pressure;
	int tp_max_pressure;
	int tp_width;  /* X direction physical size in mm. */
	int tp_high;  /* Y direction physical size in mm. */
	int tp_max_abs_x;  /* Max X absolute units that can be reported. */
	int tp_max_abs_y;  /* Max Y absolute units that can be reported. */

	int tp_res_x;  /* X resolution in units/mm. */
	int tp_res_y;  /* Y resolution in units/mm. */

	int tp_metrics_supported;
};


int cypress_detect(struct psmouse *psmouse, bool set_properties);
int cypress_init(struct psmouse *psmouse);

#endif  /* _CYPRESS_PS2_H */

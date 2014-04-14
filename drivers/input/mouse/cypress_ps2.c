/*
 * Cypress Trackpad PS/2 mouse driver
 *
 * Copyright (c) 2012 Cypress Semiconductor Corporation.
 *
 * Author:
 *   Dudley Du <dudl@cypress.com>
 *
 * Additional contributors include:
 *   Kamal Mostafa <kamal@canonical.com>
 *   Kyle Fazzari <git@status.e4ward.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/serio.h>
#include <linux/libps2.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/sched.h>
#include <linux/wait.h>

#include "cypress_ps2.h"

#undef CYTP_DEBUG_VERBOSE  /* define this and DEBUG for more verbose dump */

static void cypress_set_packet_size(struct psmouse *psmouse, unsigned int n)
{
	struct cytp_data *cytp = psmouse->private;
	cytp->pkt_size = n;
}

static const unsigned char cytp_rate[] = {10, 20, 40, 60, 100, 200};
static const unsigned char cytp_resolution[] = {0x00, 0x01, 0x02, 0x03};

static int cypress_ps2_sendbyte(struct psmouse *psmouse, int value)
{
	struct ps2dev *ps2dev = &psmouse->ps2dev;

	if (ps2_sendbyte(ps2dev, value & 0xff, CYTP_CMD_TIMEOUT) < 0) {
		psmouse_dbg(psmouse,
				"sending command 0x%02x failed, resp 0x%02x\n",
				value & 0xff, ps2dev->nak);
		if (ps2dev->nak == CYTP_PS2_RETRY)
			return CYTP_PS2_RETRY;
		else
			return CYTP_PS2_ERROR;
	}

#ifdef CYTP_DEBUG_VERBOSE
	psmouse_dbg(psmouse, "sending command 0x%02x succeeded, resp 0xfa\n",
			value & 0xff);
#endif

	return 0;
}

static int cypress_ps2_ext_cmd(struct psmouse *psmouse, unsigned short cmd,
			       unsigned char data)
{
	struct ps2dev *ps2dev = &psmouse->ps2dev;
	int tries = CYTP_PS2_CMD_TRIES;
	int rc;

	ps2_begin_command(ps2dev);

	do {
		/*
		 * Send extension command byte (0xE8 or 0xF3).
		 * If sending the command fails, send recovery command
		 * to make the device return to the ready state.
		 */
		rc = cypress_ps2_sendbyte(psmouse, cmd & 0xff);
		if (rc == CYTP_PS2_RETRY) {
			rc = cypress_ps2_sendbyte(psmouse, 0x00);
			if (rc == CYTP_PS2_RETRY)
				rc = cypress_ps2_sendbyte(psmouse, 0x0a);
		}
		if (rc == CYTP_PS2_ERROR)
			continue;

		rc = cypress_ps2_sendbyte(psmouse, data);
		if (rc == CYTP_PS2_RETRY)
			rc = cypress_ps2_sendbyte(psmouse, data);
		if (rc == CYTP_PS2_ERROR)
			continue;
		else
			break;
	} while (--tries > 0);

	ps2_end_command(ps2dev);

	return rc;
}

static int cypress_ps2_read_cmd_status(struct psmouse *psmouse,
				       unsigned char cmd,
				       unsigned char *param)
{
	int rc;
	struct ps2dev *ps2dev = &psmouse->ps2dev;
	enum psmouse_state old_state;
	int pktsize;

	ps2_begin_command(&psmouse->ps2dev);

	old_state = psmouse->state;
	psmouse->state = PSMOUSE_CMD_MODE;
	psmouse->pktcnt = 0;

	pktsize = (cmd == CYTP_CMD_READ_TP_METRICS) ? 8 : 3;
	memset(param, 0, pktsize);

	rc = cypress_ps2_sendbyte(psmouse, 0xe9);
	if (rc < 0)
		goto out;

	wait_event_timeout(ps2dev->wait,
			(psmouse->pktcnt >= pktsize),
			msecs_to_jiffies(CYTP_CMD_TIMEOUT));

	memcpy(param, psmouse->packet, pktsize);

	psmouse_dbg(psmouse, "Command 0x%02x response data (0x): %*ph\n",
			cmd, pktsize, param);

out:
	psmouse->state = old_state;
	psmouse->pktcnt = 0;

	ps2_end_command(&psmouse->ps2dev);

	return rc;
}

static bool cypress_verify_cmd_state(struct psmouse *psmouse,
				     unsigned char cmd, unsigned char *param)
{
	bool rate_match = false;
	bool resolution_match = false;
	int i;

	/* callers will do further checking. */
	if (cmd == CYTP_CMD_READ_CYPRESS_ID ||
	    cmd == CYTP_CMD_STANDARD_MODE ||
	    cmd == CYTP_CMD_READ_TP_METRICS)
		return true;

	if ((~param[0] & DFLT_RESP_BITS_VALID) == DFLT_RESP_BITS_VALID &&
	    (param[0] & DFLT_RESP_BIT_MODE) == DFLT_RESP_STREAM_MODE) {
		for (i = 0; i < sizeof(cytp_resolution); i++)
			if (cytp_resolution[i] == param[1])
				resolution_match = true;

		for (i = 0; i < sizeof(cytp_rate); i++)
			if (cytp_rate[i] == param[2])
				rate_match = true;

		if (resolution_match && rate_match)
			return true;
	}

	psmouse_dbg(psmouse, "verify cmd state failed.\n");
	return false;
}

static int cypress_send_ext_cmd(struct psmouse *psmouse, unsigned char cmd,
				unsigned char *param)
{
	int tries = CYTP_PS2_CMD_TRIES;
	int rc;

	psmouse_dbg(psmouse, "send extension cmd 0x%02x, [%d %d %d %d]\n",
		 cmd, DECODE_CMD_AA(cmd), DECODE_CMD_BB(cmd),
		 DECODE_CMD_CC(cmd), DECODE_CMD_DD(cmd));

	do {
		cypress_ps2_ext_cmd(psmouse,
				    PSMOUSE_CMD_SETRES, DECODE_CMD_DD(cmd));
		cypress_ps2_ext_cmd(psmouse,
				    PSMOUSE_CMD_SETRES, DECODE_CMD_CC(cmd));
		cypress_ps2_ext_cmd(psmouse,
				    PSMOUSE_CMD_SETRES, DECODE_CMD_BB(cmd));
		cypress_ps2_ext_cmd(psmouse,
				    PSMOUSE_CMD_SETRES, DECODE_CMD_AA(cmd));

		rc = cypress_ps2_read_cmd_status(psmouse, cmd, param);
		if (rc)
			continue;

		if (cypress_verify_cmd_state(psmouse, cmd, param))
			return 0;

	} while (--tries > 0);

	return -EIO;
}

int cypress_detect(struct psmouse *psmouse, bool set_properties)
{
	unsigned char param[3];

	if (cypress_send_ext_cmd(psmouse, CYTP_CMD_READ_CYPRESS_ID, param))
		return -ENODEV;

	/* Check for Cypress Trackpad signature bytes: 0x33 0xCC */
	if (param[0] != 0x33 || param[1] != 0xCC)
		return -ENODEV;

	if (set_properties) {
		psmouse->vendor = "Cypress";
		psmouse->name = "Trackpad";
	}

	return 0;
}

static int cypress_read_fw_version(struct psmouse *psmouse)
{
	struct cytp_data *cytp = psmouse->private;
	unsigned char param[3];

	if (cypress_send_ext_cmd(psmouse, CYTP_CMD_READ_CYPRESS_ID, param))
		return -ENODEV;

	/* Check for Cypress Trackpad signature bytes: 0x33 0xCC */
	if (param[0] != 0x33 || param[1] != 0xCC)
		return -ENODEV;

	cytp->fw_version = param[2] & FW_VERSION_MASX;
	cytp->tp_metrics_supported = (param[2] & TP_METRICS_MASK) ? 1 : 0;

	/*
	 * Trackpad fw_version 11 (in Dell XPS12) yields a bogus response to
	 * CYTP_CMD_READ_TP_METRICS so do not try to use it. LP: #1103594.
	 */
	if (cytp->fw_version >= 11)
		cytp->tp_metrics_supported = 0;

	psmouse_dbg(psmouse, "cytp->fw_version = %d\n", cytp->fw_version);
	psmouse_dbg(psmouse, "cytp->tp_metrics_supported = %d\n",
		 cytp->tp_metrics_supported);

	return 0;
}

static int cypress_read_tp_metrics(struct psmouse *psmouse)
{
	struct cytp_data *cytp = psmouse->private;
	unsigned char param[8];

	/* set default values for tp metrics. */
	cytp->tp_width = CYTP_DEFAULT_WIDTH;
	cytp->tp_high = CYTP_DEFAULT_HIGH;
	cytp->tp_max_abs_x = CYTP_ABS_MAX_X;
	cytp->tp_max_abs_y = CYTP_ABS_MAX_Y;
	cytp->tp_min_pressure = CYTP_MIN_PRESSURE;
	cytp->tp_max_pressure = CYTP_MAX_PRESSURE;
	cytp->tp_res_x = cytp->tp_max_abs_x / cytp->tp_width;
	cytp->tp_res_y = cytp->tp_max_abs_y / cytp->tp_high;

	if (!cytp->tp_metrics_supported)
		return 0;

	memset(param, 0, sizeof(param));
	if (cypress_send_ext_cmd(psmouse, CYTP_CMD_READ_TP_METRICS, param) == 0) {
		/* Update trackpad parameters. */
		cytp->tp_max_abs_x = (param[1] << 8) | param[0];
		cytp->tp_max_abs_y = (param[3] << 8) | param[2];
		cytp->tp_min_pressure = param[4];
		cytp->tp_max_pressure = param[5];
	}

	if (!cytp->tp_max_pressure ||
	    cytp->tp_max_pressure < cytp->tp_min_pressure ||
	    !cytp->tp_width || !cytp->tp_high ||
	    !cytp->tp_max_abs_x ||
	    cytp->tp_max_abs_x < cytp->tp_width ||
	    !cytp->tp_max_abs_y ||
	    cytp->tp_max_abs_y < cytp->tp_high)
		return -EINVAL;

	cytp->tp_res_x = cytp->tp_max_abs_x / cytp->tp_width;
	cytp->tp_res_y = cytp->tp_max_abs_y / cytp->tp_high;

#ifdef CYTP_DEBUG_VERBOSE
	psmouse_dbg(psmouse, "Dump trackpad hardware configuration as below:\n");
	psmouse_dbg(psmouse, "cytp->tp_width = %d\n", cytp->tp_width);
	psmouse_dbg(psmouse, "cytp->tp_high = %d\n", cytp->tp_high);
	psmouse_dbg(psmouse, "cytp->tp_max_abs_x = %d\n", cytp->tp_max_abs_x);
	psmouse_dbg(psmouse, "cytp->tp_max_abs_y = %d\n", cytp->tp_max_abs_y);
	psmouse_dbg(psmouse, "cytp->tp_min_pressure = %d\n", cytp->tp_min_pressure);
	psmouse_dbg(psmouse, "cytp->tp_max_pressure = %d\n", cytp->tp_max_pressure);
	psmouse_dbg(psmouse, "cytp->tp_res_x = %d\n", cytp->tp_res_x);
	psmouse_dbg(psmouse, "cytp->tp_res_y = %d\n", cytp->tp_res_y);

	psmouse_dbg(psmouse, "tp_type_APA = %d\n",
			(param[6] & TP_METRICS_BIT_APA) ? 1 : 0);
	psmouse_dbg(psmouse, "tp_type_MTG = %d\n",
			(param[6] & TP_METRICS_BIT_MTG) ? 1 : 0);
	psmouse_dbg(psmouse, "tp_palm = %d\n",
			(param[6] & TP_METRICS_BIT_PALM) ? 1 : 0);
	psmouse_dbg(psmouse, "tp_stubborn = %d\n",
			(param[6] & TP_METRICS_BIT_STUBBORN) ? 1 : 0);
	psmouse_dbg(psmouse, "tp_1f_jitter = %d\n",
			(param[6] & TP_METRICS_BIT_1F_JITTER) >> 2);
	psmouse_dbg(psmouse, "tp_2f_jitter = %d\n",
			(param[6] & TP_METRICS_BIT_2F_JITTER) >> 4);
	psmouse_dbg(psmouse, "tp_1f_spike = %d\n",
			param[7] & TP_METRICS_BIT_1F_SPIKE);
	psmouse_dbg(psmouse, "tp_2f_spike = %d\n",
			(param[7] & TP_METRICS_BIT_2F_SPIKE) >> 2);
	psmouse_dbg(psmouse, "tp_abs_packet_format_set = %d\n",
			(param[7] & TP_METRICS_BIT_ABS_PKT_FORMAT_SET) >> 4);
#endif

	return 0;
}

static int cypress_query_hardware(struct psmouse *psmouse)
{
	int ret;

	ret = cypress_read_fw_version(psmouse);
	if (ret)
		return ret;

	ret = cypress_read_tp_metrics(psmouse);
	if (ret)
		return ret;

	return 0;
}

static int cypress_set_absolute_mode(struct psmouse *psmouse)
{
	struct cytp_data *cytp = psmouse->private;
	unsigned char param[3];

	if (cypress_send_ext_cmd(psmouse, CYTP_CMD_ABS_WITH_PRESSURE_MODE, param) < 0)
		return -1;

	cytp->mode = (cytp->mode & ~CYTP_BIT_ABS_REL_MASK)
			| CYTP_BIT_ABS_PRESSURE;
	cypress_set_packet_size(psmouse, 5);

	return 0;
}

/*
 * Reset trackpad device.
 * This is also the default mode when trackpad powered on.
 */
static void cypress_reset(struct psmouse *psmouse)
{
	struct cytp_data *cytp = psmouse->private;

	cytp->mode = 0;

	psmouse_reset(psmouse);
}

static int cypress_set_input_params(struct input_dev *input,
				    struct cytp_data *cytp)
{
	int ret;

	if (!cytp->tp_res_x || !cytp->tp_res_y)
		return -EINVAL;

	__set_bit(EV_ABS, input->evbit);
	input_set_abs_params(input, ABS_X, 0, cytp->tp_max_abs_x, 0, 0);
	input_set_abs_params(input, ABS_Y, 0, cytp->tp_max_abs_y, 0, 0);
	input_set_abs_params(input, ABS_PRESSURE,
			     cytp->tp_min_pressure, cytp->tp_max_pressure, 0, 0);
	input_set_abs_params(input, ABS_TOOL_WIDTH, 0, 255, 0, 0);

	/* finger position */
	input_set_abs_params(input, ABS_MT_POSITION_X, 0, cytp->tp_max_abs_x, 0, 0);
	input_set_abs_params(input, ABS_MT_POSITION_Y, 0, cytp->tp_max_abs_y, 0, 0);
	input_set_abs_params(input, ABS_MT_PRESSURE, 0, 255, 0, 0);

	ret = input_mt_init_slots(input, CYTP_MAX_MT_SLOTS,
			INPUT_MT_DROP_UNUSED|INPUT_MT_TRACK);
	if (ret < 0)
		return ret;

	__set_bit(INPUT_PROP_SEMI_MT, input->propbit);

	input_abs_set_res(input, ABS_X, cytp->tp_res_x);
	input_abs_set_res(input, ABS_Y, cytp->tp_res_y);

	input_abs_set_res(input, ABS_MT_POSITION_X, cytp->tp_res_x);
	input_abs_set_res(input, ABS_MT_POSITION_Y, cytp->tp_res_y);

	__set_bit(BTN_TOUCH, input->keybit);
	__set_bit(BTN_TOOL_FINGER, input->keybit);
	__set_bit(BTN_TOOL_DOUBLETAP, input->keybit);
	__set_bit(BTN_TOOL_TRIPLETAP, input->keybit);
	__set_bit(BTN_TOOL_QUADTAP, input->keybit);
	__set_bit(BTN_TOOL_QUINTTAP, input->keybit);

	__clear_bit(EV_REL, input->evbit);
	__clear_bit(REL_X, input->relbit);
	__clear_bit(REL_Y, input->relbit);

	__set_bit(INPUT_PROP_BUTTONPAD, input->propbit);
	__set_bit(EV_KEY, input->evbit);
	__set_bit(BTN_LEFT, input->keybit);
	__set_bit(BTN_RIGHT, input->keybit);
	__set_bit(BTN_MIDDLE, input->keybit);

	input_set_drvdata(input, cytp);

	return 0;
}

static int cypress_get_finger_count(unsigned char header_byte)
{
	unsigned char bits6_7;
	int finger_count;

	bits6_7 = header_byte >> 6;
	finger_count = bits6_7 & 0x03;

	if (finger_count == 1)
		return 1;

	if (header_byte & ABS_HSCROLL_BIT) {
		/* HSCROLL gets added on to 0 finger count. */
		switch (finger_count) {
			case 0:	return 4;
			case 2: return 5;
			default:
				/* Invalid contact (e.g. palm). Ignore it. */
				return 0;
		}
	}

	return finger_count;
}


static int cypress_parse_packet(struct psmouse *psmouse,
				struct cytp_data *cytp, struct cytp_report_data *report_data)
{
	unsigned char *packet = psmouse->packet;
	unsigned char header_byte = packet[0];

	memset(report_data, 0, sizeof(struct cytp_report_data));

	report_data->contact_cnt = cypress_get_finger_count(header_byte);
	report_data->tap = (header_byte & ABS_MULTIFINGER_TAP) ? 1 : 0;

	if (report_data->contact_cnt == 1) {
		report_data->contacts[0].x =
			((packet[1] & 0x70) << 4) | packet[2];
		report_data->contacts[0].y =
			((packet[1] & 0x07) << 8) | packet[3];
		if (cytp->mode & CYTP_BIT_ABS_PRESSURE)
			report_data->contacts[0].z = packet[4];

	} else if (report_data->contact_cnt >= 2) {
		report_data->contacts[0].x =
			((packet[1] & 0x70) << 4) | packet[2];
		report_data->contacts[0].y =
			((packet[1] & 0x07) << 8) | packet[3];
		if (cytp->mode & CYTP_BIT_ABS_PRESSURE)
			report_data->contacts[0].z = packet[4];

		report_data->contacts[1].x =
			((packet[5] & 0xf0) << 4) | packet[6];
		report_data->contacts[1].y =
			((packet[5] & 0x0f) << 8) | packet[7];
		if (cytp->mode & CYTP_BIT_ABS_PRESSURE)
			report_data->contacts[1].z = report_data->contacts[0].z;
	}

	report_data->left = (header_byte & BTN_LEFT_BIT) ? 1 : 0;
	report_data->right = (header_byte & BTN_RIGHT_BIT) ? 1 : 0;

	/*
	 * This is only true if one of the mouse buttons were tapped.  Make
	 * sure it doesn't turn into a click. The regular tap-to-click
	 * functionality will handle that on its own. If we don't do this,
	 * disabling tap-to-click won't affect the mouse button zones.
	 */
	if (report_data->tap)
		report_data->left = 0;

#ifdef CYTP_DEBUG_VERBOSE
	{
		int i;
		int n = report_data->contact_cnt;
		psmouse_dbg(psmouse, "Dump parsed report data as below:\n");
		psmouse_dbg(psmouse, "contact_cnt = %d\n",
			report_data->contact_cnt);
		if (n > CYTP_MAX_MT_SLOTS)
		    n = CYTP_MAX_MT_SLOTS;
		for (i = 0; i < n; i++)
			psmouse_dbg(psmouse, "contacts[%d] = {%d, %d, %d}\n", i,
					report_data->contacts[i].x,
					report_data->contacts[i].y,
					report_data->contacts[i].z);
		psmouse_dbg(psmouse, "left = %d\n", report_data->left);
		psmouse_dbg(psmouse, "right = %d\n", report_data->right);
		psmouse_dbg(psmouse, "middle = %d\n", report_data->middle);
	}
#endif

	return 0;
}

static void cypress_process_packet(struct psmouse *psmouse, bool zero_pkt)
{
	int i;
	struct input_dev *input = psmouse->dev;
	struct cytp_data *cytp = psmouse->private;
	struct cytp_report_data report_data;
	struct cytp_contact *contact;
	struct input_mt_pos pos[CYTP_MAX_MT_SLOTS];
	int slots[CYTP_MAX_MT_SLOTS];
	int n;

	cypress_parse_packet(psmouse, cytp, &report_data);

	n = report_data.contact_cnt;
	if (n > CYTP_MAX_MT_SLOTS)
		n = CYTP_MAX_MT_SLOTS;

	for (i = 0; i < n; i++) {
		contact = &report_data.contacts[i];
		pos[i].x = contact->x;
		pos[i].y = contact->y;
	}

	input_mt_assign_slots(input, slots, pos, n);

	for (i = 0; i < n; i++) {
		contact = &report_data.contacts[i];
		input_mt_slot(input, slots[i]);
		input_mt_report_slot_state(input, MT_TOOL_FINGER, true);
		input_report_abs(input, ABS_MT_POSITION_X, contact->x);
		input_report_abs(input, ABS_MT_POSITION_Y, contact->y);
		input_report_abs(input, ABS_MT_PRESSURE, contact->z);
	}

	input_mt_sync_frame(input);

	input_mt_report_finger_count(input, report_data.contact_cnt);

	input_report_key(input, BTN_LEFT, report_data.left);
	input_report_key(input, BTN_RIGHT, report_data.right);
	input_report_key(input, BTN_MIDDLE, report_data.middle);

	input_sync(input);
}

static psmouse_ret_t cypress_validate_byte(struct psmouse *psmouse)
{
	int contact_cnt;
	int index = psmouse->pktcnt - 1;
	unsigned char *packet = psmouse->packet;
	struct cytp_data *cytp = psmouse->private;

	if (index < 0 || index > cytp->pkt_size)
		return PSMOUSE_BAD_DATA;

	if (index == 0 && (packet[0] & 0xfc) == 0) {
		/* call packet process for reporting finger leave. */
		cypress_process_packet(psmouse, 1);
		return PSMOUSE_FULL_PACKET;
	}

	/*
	 * Perform validation (and adjust packet size) based only on the
	 * first byte; allow all further bytes through.
	 */
	if (index != 0)
		return PSMOUSE_GOOD_DATA;

	/*
	 * If absolute/relative mode bit has not been set yet, just pass
	 * the byte through.
	 */
	if ((cytp->mode & CYTP_BIT_ABS_REL_MASK) == 0)
		return PSMOUSE_GOOD_DATA;

	if ((packet[0] & 0x08) == 0x08)
		return PSMOUSE_BAD_DATA;

	contact_cnt = cypress_get_finger_count(packet[0]);
	if (cytp->mode & CYTP_BIT_ABS_NO_PRESSURE)
		cypress_set_packet_size(psmouse, contact_cnt == 2 ? 7 : 4);
	else
		cypress_set_packet_size(psmouse, contact_cnt == 2 ? 8 : 5);

	return PSMOUSE_GOOD_DATA;
}

static psmouse_ret_t cypress_protocol_handler(struct psmouse *psmouse)
{
	struct cytp_data *cytp = psmouse->private;

	if (psmouse->pktcnt >= cytp->pkt_size) {
		cypress_process_packet(psmouse, 0);
		return PSMOUSE_FULL_PACKET;
	}

	return cypress_validate_byte(psmouse);
}

static void cypress_set_rate(struct psmouse *psmouse, unsigned int rate)
{
	struct cytp_data *cytp = psmouse->private;

	if (rate >= 80) {
		psmouse->rate = 80;
		cytp->mode |= CYTP_BIT_HIGH_RATE;
	} else {
		psmouse->rate = 40;
		cytp->mode &= ~CYTP_BIT_HIGH_RATE;
	}

	ps2_command(&psmouse->ps2dev, (unsigned char *)&psmouse->rate,
		    PSMOUSE_CMD_SETRATE);
}

static void cypress_disconnect(struct psmouse *psmouse)
{
	cypress_reset(psmouse);
	kfree(psmouse->private);
	psmouse->private = NULL;
}

static int cypress_reconnect(struct psmouse *psmouse)
{
	int tries = CYTP_PS2_CMD_TRIES;
	int rc;

	do {
		cypress_reset(psmouse);
		rc = cypress_detect(psmouse, false);
	} while (rc && (--tries > 0));

	if (rc) {
		psmouse_err(psmouse, "Reconnect: unable to detect trackpad.\n");
		return -1;
	}

	if (cypress_set_absolute_mode(psmouse)) {
		psmouse_err(psmouse, "Reconnect: Unable to initialize Cypress absolute mode.\n");
		return -1;
	}

	return 0;
}

int cypress_init(struct psmouse *psmouse)
{
	struct cytp_data *cytp;

	cytp = (struct cytp_data *)kzalloc(sizeof(struct cytp_data), GFP_KERNEL);
	psmouse->private = (void *)cytp;
	if (cytp == NULL)
		return -ENOMEM;

	cypress_reset(psmouse);

	psmouse->pktsize = 8;

	if (cypress_query_hardware(psmouse)) {
		psmouse_err(psmouse, "Unable to query Trackpad hardware.\n");
		goto err_exit;
	}

	if (cypress_set_absolute_mode(psmouse)) {
		psmouse_err(psmouse, "init: Unable to initialize Cypress absolute mode.\n");
		goto err_exit;
	}

	if (cypress_set_input_params(psmouse->dev, cytp) < 0) {
		psmouse_err(psmouse, "init: Unable to set input params.\n");
		goto err_exit;
	}

	psmouse->model = 1;
	psmouse->protocol_handler = cypress_protocol_handler;
	psmouse->set_rate = cypress_set_rate;
	psmouse->disconnect = cypress_disconnect;
	psmouse->reconnect = cypress_reconnect;
	psmouse->cleanup = cypress_reset;
	psmouse->resync_time = 0;

	return 0;

err_exit:
	/*
	 * Reset Cypress Trackpad as a standard mouse. Then
	 * let psmouse driver commmunicating with it as default PS2 mouse.
	 */
	cypress_reset(psmouse);

	psmouse->private = NULL;
	kfree(cytp);

	return -1;
}

bool cypress_supported(void)
{
	return true;
}

/*
 * Fujitsu B-series Lifebook PS/2 TouchScreen driver
 *
 * Copyright (c) 2005 Vojtech Pavlik <vojtech@suse.cz>
 * Copyright (c) 2005 Kenan Esau <kenan.esau@conan.de>
 *
 * TouchScreen detection, absolute mode setting and packet layout is taken from
 * Harald Hoyer's description of the device.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/input.h>
#include <linux/serio.h>
#include <linux/libps2.h>
#include <linux/dmi.h>

#include "psmouse.h"
#include "lifebook.h"

static struct dmi_system_id lifebook_dmi_table[] = {
       {
               .ident = "LifeBook B",
               .matches = {
                       DMI_MATCH(DMI_PRODUCT_NAME, "LifeBook B Series"),
               },
       },
       {
               .ident = "Lifebook B",
               .matches = {
                       DMI_MATCH(DMI_PRODUCT_NAME, "LIFEBOOK B Series"),
               },
       },
       {
               .ident = "Lifebook B213x/B2150",
               .matches = {
                       DMI_MATCH(DMI_PRODUCT_NAME, "LifeBook B2131/B2133/B2150"),
               },
       },
       {
               .ident = "Zephyr",
               .matches = {
                       DMI_MATCH(DMI_PRODUCT_NAME, "ZEPHYR"),
               },
       },
       {
               .ident = "CF-18",
               .matches = {
                       DMI_MATCH(DMI_PRODUCT_NAME, "CF-18"),
               },
       },
       {
               .ident = "Lifebook B142",
               .matches = {
                       DMI_MATCH(DMI_PRODUCT_NAME, "LifeBook B142"),
               },

       },
       { }
};


static psmouse_ret_t lifebook_process_byte(struct psmouse *psmouse, struct pt_regs *regs)
{
	unsigned char *packet = psmouse->packet;
	struct input_dev *dev = psmouse->dev;

	if (psmouse->pktcnt != 3)
		return PSMOUSE_GOOD_DATA;

	input_regs(dev, regs);

	/* calculate X and Y */
	if ((packet[0] & 0x08) == 0x00) {
		input_report_abs(dev, ABS_X,
				 (packet[1] | ((packet[0] & 0x30) << 4)));
		input_report_abs(dev, ABS_Y,
				 1024 - (packet[2] | ((packet[0] & 0xC0) << 2)));
	} else {
		input_report_rel(dev, REL_X,
				((packet[0] & 0x10) ? packet[1] - 256 : packet[1]));
		input_report_rel(dev, REL_Y,
				 -(int)((packet[0] & 0x20) ? packet[2] - 256 : packet[2]));
	}

	input_report_key(dev, BTN_LEFT, packet[0] & 0x01);
	input_report_key(dev, BTN_RIGHT, packet[0] & 0x02);
	input_report_key(dev, BTN_TOUCH, packet[0] & 0x04);

	input_sync(dev);

	return PSMOUSE_FULL_PACKET;
}

static int lifebook_absolute_mode(struct psmouse *psmouse)
{
	struct ps2dev *ps2dev = &psmouse->ps2dev;
	unsigned char param;

	if (psmouse_reset(psmouse))
		return -1;

	/*
	   Enable absolute output -- ps2_command fails always but if
	   you leave this call out the touchsreen will never send
	   absolute coordinates
	*/
	param = 0x07;
	ps2_command(ps2dev, &param, PSMOUSE_CMD_SETRES);

	return 0;
}

static void lifebook_set_resolution(struct psmouse *psmouse, unsigned int resolution)
{
	unsigned char params[] = { 0, 1, 2, 2, 3 };

	if (resolution == 0 || resolution > 400)
		resolution = 400;

	ps2_command(&psmouse->ps2dev, &params[resolution / 100], PSMOUSE_CMD_SETRES);
	psmouse->resolution = 50 << params[resolution / 100];
}

static void lifebook_disconnect(struct psmouse *psmouse)
{
	psmouse_reset(psmouse);
}

int lifebook_detect(struct psmouse *psmouse, int set_properties)
{
        if (!dmi_check_system(lifebook_dmi_table))
                return -1;

	if (set_properties) {
		psmouse->vendor = "Fujitsu";
		psmouse->name = "Lifebook TouchScreen";
	}

        return 0;
}

int lifebook_init(struct psmouse *psmouse)
{
	struct input_dev *input_dev = psmouse->dev;

	if (lifebook_absolute_mode(psmouse))
		return -1;

	input_dev->evbit[0] = BIT(EV_ABS) | BIT(EV_KEY) | BIT(EV_REL);
	input_dev->keybit[LONG(BTN_LEFT)] = BIT(BTN_LEFT) | BIT(BTN_MIDDLE) | BIT(BTN_RIGHT);
	input_dev->keybit[LONG(BTN_TOUCH)] = BIT(BTN_TOUCH);
	input_dev->relbit[0] = BIT(REL_X) | BIT(REL_Y);
	input_set_abs_params(input_dev, ABS_X, 0, 1024, 0, 0);
	input_set_abs_params(input_dev, ABS_Y, 0, 1024, 0, 0);

	psmouse->protocol_handler = lifebook_process_byte;
	psmouse->set_resolution = lifebook_set_resolution;
	psmouse->disconnect = lifebook_disconnect;
	psmouse->reconnect  = lifebook_absolute_mode;
	psmouse->pktsize = 3;

	return 0;
}


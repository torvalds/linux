/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * CIMaX SP2/HF CI driver
 *
 * Copyright (C) 2014 Olli Salonen <olli.salonen@iki.fi>
 */

#ifndef SP2_H
#define SP2_H

#include <media/dvb_ca_en50221.h>

/*
 * I2C address
 * 0x40 (port 0)
 * 0x41 (port 1)
 */
struct sp2_config {
	/* dvb_adapter to attach the ci to */
	struct dvb_adapter *dvb_adap;

	/* function ci_control handles the device specific ci ops */
	void *ci_control;

	/* priv is passed back to function ci_control */
	void *priv;
};

extern int sp2_ci_read_attribute_mem(struct dvb_ca_en50221 *en50221,
					int slot, int addr);
extern int sp2_ci_write_attribute_mem(struct dvb_ca_en50221 *en50221,
					int slot, int addr, u8 data);
extern int sp2_ci_read_cam_control(struct dvb_ca_en50221 *en50221,
					int slot, u8 addr);
extern int sp2_ci_write_cam_control(struct dvb_ca_en50221 *en50221,
					int slot, u8 addr, u8 data);
extern int sp2_ci_slot_reset(struct dvb_ca_en50221 *en50221, int slot);
extern int sp2_ci_slot_shutdown(struct dvb_ca_en50221 *en50221, int slot);
extern int sp2_ci_slot_ts_enable(struct dvb_ca_en50221 *en50221, int slot);
extern int sp2_ci_poll_slot_status(struct dvb_ca_en50221 *en50221,
					int slot, int open);

#endif

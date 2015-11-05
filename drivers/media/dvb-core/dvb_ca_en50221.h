/*
 * dvb_ca.h: generic DVB functions for EN50221 CA interfaces
 *
 * Copyright (C) 2004 Andrew de Quincey
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 */

#ifndef _DVB_CA_EN50221_H_
#define _DVB_CA_EN50221_H_

#include <linux/list.h>
#include <linux/dvb/ca.h>

#include "dvbdev.h"

#define DVB_CA_EN50221_POLL_CAM_PRESENT	1
#define DVB_CA_EN50221_POLL_CAM_CHANGED	2
#define DVB_CA_EN50221_POLL_CAM_READY		4

#define DVB_CA_EN50221_FLAG_IRQ_CAMCHANGE	1
#define DVB_CA_EN50221_FLAG_IRQ_FR		2
#define DVB_CA_EN50221_FLAG_IRQ_DA		4

#define DVB_CA_EN50221_CAMCHANGE_REMOVED		0
#define DVB_CA_EN50221_CAMCHANGE_INSERTED		1

/**
 * struct dvb_ca_en50221- Structure describing a CA interface
 *
 * @owner:		the module owning this structure
 * @read_attribute_mem:	function for reading attribute memory on the CAM
 * @write_attribute_mem: function for writing attribute memory on the CAM
 * @read_cam_control:	function for reading the control interface on the CAM
 * @write_cam_control:	function for reading the control interface on the CAM
 * @slot_reset:		function to reset the CAM slot
 * @slot_shutdown:	function to shutdown a CAM slot
 * @slot_ts_enable:	function to enable the Transport Stream on a CAM slot
 * @poll_slot_status:	function to poll slot status. Only necessary if
 *			DVB_CA_FLAG_EN50221_IRQ_CAMCHANGE is not set.
 * @data:		private data, used by caller.
 * @private:		Opaque data used by the dvb_ca core. Do not modify!
 *
 * NOTE: the read_*, write_* and poll_slot_status functions will be
 * called for different slots concurrently and need to use locks where
 * and if appropriate. There will be no concurrent access to one slot.
 */
struct dvb_ca_en50221 {
	struct module *owner;

	int (*read_attribute_mem)(struct dvb_ca_en50221 *ca,
				  int slot, int address);
	int (*write_attribute_mem)(struct dvb_ca_en50221 *ca,
				   int slot, int address, u8 value);

	int (*read_cam_control)(struct dvb_ca_en50221 *ca,
				int slot, u8 address);
	int (*write_cam_control)(struct dvb_ca_en50221 *ca,
				 int slot, u8 address, u8 value);

	int (*slot_reset)(struct dvb_ca_en50221 *ca, int slot);
	int (*slot_shutdown)(struct dvb_ca_en50221 *ca, int slot);
	int (*slot_ts_enable)(struct dvb_ca_en50221 *ca, int slot);

	int (*poll_slot_status)(struct dvb_ca_en50221 *ca, int slot, int open);

	void *data;

	void *private;
};

/*
 * Functions for reporting IRQ events
 */

/**
 * dvb_ca_en50221_camchange_irq - A CAMCHANGE IRQ has occurred.
 *
 * @pubca: CA instance.
 * @slot: Slot concerned.
 * @change_type: One of the DVB_CA_CAMCHANGE_* values
 */
void dvb_ca_en50221_camchange_irq(struct dvb_ca_en50221 *pubca, int slot,
				  int change_type);

/**
 * dvb_ca_en50221_camready_irq - A CAMREADY IRQ has occurred.
 *
 * @pubca: CA instance.
 * @slot: Slot concerned.
 */
void dvb_ca_en50221_camready_irq(struct dvb_ca_en50221 *pubca, int slot);

/**
 * dvb_ca_en50221_frda_irq - An FR or a DA IRQ has occurred.
 *
 * @ca: CA instance.
 * @slot: Slot concerned.
 */
void dvb_ca_en50221_frda_irq(struct dvb_ca_en50221 *ca, int slot);

/*
 * Initialisation/shutdown functions
 */

/**
 * dvb_ca_en50221_init - Initialise a new DVB CA device.
 *
 * @dvb_adapter: DVB adapter to attach the new CA device to.
 * @ca: The dvb_ca instance.
 * @flags: Flags describing the CA device (DVB_CA_EN50221_FLAG_*).
 * @slot_count: Number of slots supported.
 *
 * @return 0 on success, nonzero on failure
 */
extern int dvb_ca_en50221_init(struct dvb_adapter *dvb_adapter,
			       struct dvb_ca_en50221 *ca, int flags,
			       int slot_count);

/**
 * dvb_ca_en50221_release - Release a DVB CA device.
 *
 * @ca: The associated dvb_ca instance.
 */
extern void dvb_ca_en50221_release(struct dvb_ca_en50221 *ca);

#endif

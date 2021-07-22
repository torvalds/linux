// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for Virtual PS/2 Mouse on VMware and QEMU hypervisors.
 *
 * Copyright (C) 2014, VMware, Inc. All Rights Reserved.
 *
 * Twin device code is hugely inspired by the ALPS driver.
 * Authors:
 *   Dmitry Torokhov <dmitry.torokhov@gmail.com>
 *   Thomas Hellstrom <thellstrom@vmware.com>
 */

#include <linux/input.h>
#include <linux/serio.h>
#include <linux/libps2.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <asm/hypervisor.h>
#include <asm/vmware.h>

#include "psmouse.h"
#include "vmmouse.h"

#define VMMOUSE_PROTO_MAGIC			0x564D5868U

/*
 * Main commands supported by the vmmouse hypervisor port.
 */
#define VMMOUSE_PROTO_CMD_GETVERSION		10
#define VMMOUSE_PROTO_CMD_ABSPOINTER_DATA	39
#define VMMOUSE_PROTO_CMD_ABSPOINTER_STATUS	40
#define VMMOUSE_PROTO_CMD_ABSPOINTER_COMMAND	41
#define VMMOUSE_PROTO_CMD_ABSPOINTER_RESTRICT   86

/*
 * Subcommands for VMMOUSE_PROTO_CMD_ABSPOINTER_COMMAND
 */
#define VMMOUSE_CMD_ENABLE			0x45414552U
#define VMMOUSE_CMD_DISABLE			0x000000f5U
#define VMMOUSE_CMD_REQUEST_RELATIVE		0x4c455252U
#define VMMOUSE_CMD_REQUEST_ABSOLUTE		0x53424152U

#define VMMOUSE_ERROR				0xffff0000U

#define VMMOUSE_VERSION_ID			0x3442554aU

#define VMMOUSE_RELATIVE_PACKET			0x00010000U

#define VMMOUSE_LEFT_BUTTON			0x20
#define VMMOUSE_RIGHT_BUTTON			0x10
#define VMMOUSE_MIDDLE_BUTTON			0x08

/*
 * VMMouse Restrict command
 */
#define VMMOUSE_RESTRICT_ANY                    0x00
#define VMMOUSE_RESTRICT_CPL0                   0x01
#define VMMOUSE_RESTRICT_IOPL                   0x02

#define VMMOUSE_MAX_X                           0xFFFF
#define VMMOUSE_MAX_Y                           0xFFFF

#define VMMOUSE_VENDOR "VMware"
#define VMMOUSE_NAME   "VMMouse"

/**
 * struct vmmouse_data - private data structure for the vmmouse driver
 *
 * @abs_dev: "Absolute" device used to report absolute mouse movement.
 * @phys: Physical path for the absolute device.
 * @dev_name: Name attribute name for the absolute device.
 */
struct vmmouse_data {
	struct input_dev *abs_dev;
	char phys[32];
	char dev_name[128];
};

/*
 * Hypervisor-specific bi-directional communication channel
 * implementing the vmmouse protocol. Should never execute on
 * bare metal hardware.
 */
#define VMMOUSE_CMD(cmd, in1, out1, out2, out3, out4)	\
({							\
	unsigned long __dummy1, __dummy2;		\
	__asm__ __volatile__ (VMWARE_HYPERCALL :	\
		"=a"(out1),				\
		"=b"(out2),				\
		"=c"(out3),				\
		"=d"(out4),				\
		"=S"(__dummy1),				\
		"=D"(__dummy2) :			\
		"a"(VMMOUSE_PROTO_MAGIC),		\
		"b"(in1),				\
		"c"(VMMOUSE_PROTO_CMD_##cmd),		\
		"d"(0) :			        \
		"memory");		                \
})

/**
 * vmmouse_report_button - report button state on the correct input device
 *
 * @psmouse:  Pointer to the psmouse struct
 * @abs_dev:  The absolute input device
 * @rel_dev:  The relative input device
 * @pref_dev: The preferred device for reporting
 * @code:     Button code
 * @value:    Button value
 *
 * Report @value and @code on @pref_dev, unless the button is already
 * pressed on the other device, in which case the state is reported on that
 * device.
 */
static void vmmouse_report_button(struct psmouse *psmouse,
				  struct input_dev *abs_dev,
				  struct input_dev *rel_dev,
				  struct input_dev *pref_dev,
				  unsigned int code, int value)
{
	if (test_bit(code, abs_dev->key))
		pref_dev = abs_dev;
	else if (test_bit(code, rel_dev->key))
		pref_dev = rel_dev;

	input_report_key(pref_dev, code, value);
}

/**
 * vmmouse_report_events - process events on the vmmouse communications channel
 *
 * @psmouse: Pointer to the psmouse struct
 *
 * This function pulls events from the vmmouse communications channel and
 * reports them on the correct (absolute or relative) input device. When the
 * communications channel is drained, or if we've processed more than 255
 * psmouse commands, the function returns PSMOUSE_FULL_PACKET. If there is a
 * host- or synchronization error, the function returns PSMOUSE_BAD_DATA in
 * the hope that the caller will reset the communications channel.
 */
static psmouse_ret_t vmmouse_report_events(struct psmouse *psmouse)
{
	struct input_dev *rel_dev = psmouse->dev;
	struct vmmouse_data *priv = psmouse->private;
	struct input_dev *abs_dev = priv->abs_dev;
	struct input_dev *pref_dev;
	u32 status, x, y, z;
	u32 dummy1, dummy2, dummy3;
	unsigned int queue_length;
	unsigned int count = 255;

	while (count--) {
		/* See if we have motion data. */
		VMMOUSE_CMD(ABSPOINTER_STATUS, 0,
			    status, dummy1, dummy2, dummy3);
		if ((status & VMMOUSE_ERROR) == VMMOUSE_ERROR) {
			psmouse_err(psmouse, "failed to fetch status data\n");
			/*
			 * After a few attempts this will result in
			 * reconnect.
			 */
			return PSMOUSE_BAD_DATA;
		}

		queue_length = status & 0xffff;
		if (queue_length == 0)
			break;

		if (queue_length % 4) {
			psmouse_err(psmouse, "invalid queue length\n");
			return PSMOUSE_BAD_DATA;
		}

		/* Now get it */
		VMMOUSE_CMD(ABSPOINTER_DATA, 4, status, x, y, z);

		/*
		 * And report what we've got. Prefer to report button
		 * events on the same device where we report motion events.
		 * This doesn't work well with the mouse wheel, though. See
		 * below. Ideally we would want to report that on the
		 * preferred device as well.
		 */
		if (status & VMMOUSE_RELATIVE_PACKET) {
			pref_dev = rel_dev;
			input_report_rel(rel_dev, REL_X, (s32)x);
			input_report_rel(rel_dev, REL_Y, -(s32)y);
		} else {
			pref_dev = abs_dev;
			input_report_abs(abs_dev, ABS_X, x);
			input_report_abs(abs_dev, ABS_Y, y);
		}

		/* Xorg seems to ignore wheel events on absolute devices */
		input_report_rel(rel_dev, REL_WHEEL, -(s8)((u8) z));

		vmmouse_report_button(psmouse, abs_dev, rel_dev,
				      pref_dev, BTN_LEFT,
				      status & VMMOUSE_LEFT_BUTTON);
		vmmouse_report_button(psmouse, abs_dev, rel_dev,
				      pref_dev, BTN_RIGHT,
				      status & VMMOUSE_RIGHT_BUTTON);
		vmmouse_report_button(psmouse, abs_dev, rel_dev,
				      pref_dev, BTN_MIDDLE,
				      status & VMMOUSE_MIDDLE_BUTTON);
		input_sync(abs_dev);
		input_sync(rel_dev);
	}

	return PSMOUSE_FULL_PACKET;
}

/**
 * vmmouse_process_byte - process data on the ps/2 channel
 *
 * @psmouse: Pointer to the psmouse struct
 *
 * When the ps/2 channel indicates that there is vmmouse data available,
 * call vmmouse channel processing. Otherwise, continue to accept bytes. If
 * there is a synchronization or communication data error, return
 * PSMOUSE_BAD_DATA in the hope that the caller will reset the mouse.
 */
static psmouse_ret_t vmmouse_process_byte(struct psmouse *psmouse)
{
	unsigned char *packet = psmouse->packet;

	switch (psmouse->pktcnt) {
	case 1:
		return (packet[0] & 0x8) == 0x8 ?
			PSMOUSE_GOOD_DATA : PSMOUSE_BAD_DATA;

	case 2:
		return PSMOUSE_GOOD_DATA;

	default:
		return vmmouse_report_events(psmouse);
	}
}

/**
 * vmmouse_disable - Disable vmmouse
 *
 * @psmouse: Pointer to the psmouse struct
 *
 * Tries to disable vmmouse mode.
 */
static void vmmouse_disable(struct psmouse *psmouse)
{
	u32 status;
	u32 dummy1, dummy2, dummy3, dummy4;

	VMMOUSE_CMD(ABSPOINTER_COMMAND, VMMOUSE_CMD_DISABLE,
		    dummy1, dummy2, dummy3, dummy4);

	VMMOUSE_CMD(ABSPOINTER_STATUS, 0,
		    status, dummy1, dummy2, dummy3);

	if ((status & VMMOUSE_ERROR) != VMMOUSE_ERROR)
		psmouse_warn(psmouse, "failed to disable vmmouse device\n");
}

/**
 * vmmouse_enable - Enable vmmouse and request absolute mode.
 *
 * @psmouse: Pointer to the psmouse struct
 *
 * Tries to enable vmmouse mode. Performs basic checks and requests
 * absolute vmmouse mode.
 * Returns 0 on success, -ENODEV on failure.
 */
static int vmmouse_enable(struct psmouse *psmouse)
{
	u32 status, version;
	u32 dummy1, dummy2, dummy3, dummy4;

	/*
	 * Try enabling the device. If successful, we should be able to
	 * read valid version ID back from it.
	 */
	VMMOUSE_CMD(ABSPOINTER_COMMAND, VMMOUSE_CMD_ENABLE,
		    dummy1, dummy2, dummy3, dummy4);

	/*
	 * See if version ID can be retrieved.
	 */
	VMMOUSE_CMD(ABSPOINTER_STATUS, 0, status, dummy1, dummy2, dummy3);
	if ((status & 0x0000ffff) == 0) {
		psmouse_dbg(psmouse, "empty flags - assuming no device\n");
		return -ENXIO;
	}

	VMMOUSE_CMD(ABSPOINTER_DATA, 1 /* single item */,
		    version, dummy1, dummy2, dummy3);
	if (version != VMMOUSE_VERSION_ID) {
		psmouse_dbg(psmouse, "Unexpected version value: %u vs %u\n",
			    (unsigned) version, VMMOUSE_VERSION_ID);
		vmmouse_disable(psmouse);
		return -ENXIO;
	}

	/*
	 * Restrict ioport access, if possible.
	 */
	VMMOUSE_CMD(ABSPOINTER_RESTRICT, VMMOUSE_RESTRICT_CPL0,
		    dummy1, dummy2, dummy3, dummy4);

	VMMOUSE_CMD(ABSPOINTER_COMMAND, VMMOUSE_CMD_REQUEST_ABSOLUTE,
		    dummy1, dummy2, dummy3, dummy4);

	return 0;
}

/*
 * Array of supported hypervisors.
 */
static enum x86_hypervisor_type vmmouse_supported_hypervisors[] = {
	X86_HYPER_VMWARE,
	X86_HYPER_KVM,
};

/**
 * vmmouse_check_hypervisor - Check if we're running on a supported hypervisor
 */
static bool vmmouse_check_hypervisor(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(vmmouse_supported_hypervisors); i++)
		if (vmmouse_supported_hypervisors[i] == x86_hyper_type)
			return true;

	return false;
}

/**
 * vmmouse_detect - Probe whether vmmouse is available
 *
 * @psmouse: Pointer to the psmouse struct
 * @set_properties: Whether to set psmouse name and vendor
 *
 * Returns 0 if vmmouse channel is available. Negative error code if not.
 */
int vmmouse_detect(struct psmouse *psmouse, bool set_properties)
{
	u32 response, version, dummy1, dummy2;

	if (!vmmouse_check_hypervisor()) {
		psmouse_dbg(psmouse,
			    "VMMouse not running on supported hypervisor.\n");
		return -ENXIO;
	}

	/* Check if the device is present */
	response = ~VMMOUSE_PROTO_MAGIC;
	VMMOUSE_CMD(GETVERSION, 0, version, response, dummy1, dummy2);
	if (response != VMMOUSE_PROTO_MAGIC || version == 0xffffffffU)
		return -ENXIO;

	if (set_properties) {
		psmouse->vendor = VMMOUSE_VENDOR;
		psmouse->name = VMMOUSE_NAME;
		psmouse->model = version;
	}

	return 0;
}

/**
 * vmmouse_disconnect - Take down vmmouse driver
 *
 * @psmouse: Pointer to the psmouse struct
 *
 * Takes down vmmouse driver and frees resources set up in vmmouse_init().
 */
static void vmmouse_disconnect(struct psmouse *psmouse)
{
	struct vmmouse_data *priv = psmouse->private;

	vmmouse_disable(psmouse);
	psmouse_reset(psmouse);
	input_unregister_device(priv->abs_dev);
	kfree(priv);
}

/**
 * vmmouse_reconnect - Reset the ps/2 - and vmmouse connections
 *
 * @psmouse: Pointer to the psmouse struct
 *
 * Attempts to reset the mouse connections. Returns 0 on success and
 * -1 on failure.
 */
static int vmmouse_reconnect(struct psmouse *psmouse)
{
	int error;

	psmouse_reset(psmouse);
	vmmouse_disable(psmouse);
	error = vmmouse_enable(psmouse);
	if (error) {
		psmouse_err(psmouse,
			    "Unable to re-enable mouse when reconnecting, err: %d\n",
			    error);
		return error;
	}

	return 0;
}

/**
 * vmmouse_init - Initialize the vmmouse driver
 *
 * @psmouse: Pointer to the psmouse struct
 *
 * Requests the device and tries to enable vmmouse mode.
 * If successful, sets up the input device for relative movement events.
 * It also allocates another input device and sets it up for absolute motion
 * events. Returns 0 on success and -1 on failure.
 */
int vmmouse_init(struct psmouse *psmouse)
{
	struct vmmouse_data *priv;
	struct input_dev *rel_dev = psmouse->dev, *abs_dev;
	int error;

	psmouse_reset(psmouse);
	error = vmmouse_enable(psmouse);
	if (error)
		return error;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	abs_dev = input_allocate_device();
	if (!priv || !abs_dev) {
		error = -ENOMEM;
		goto init_fail;
	}

	priv->abs_dev = abs_dev;
	psmouse->private = priv;

	/* Set up and register absolute device */
	snprintf(priv->phys, sizeof(priv->phys), "%s/input1",
		 psmouse->ps2dev.serio->phys);

	/* Mimic name setup for relative device in psmouse-base.c */
	snprintf(priv->dev_name, sizeof(priv->dev_name), "%s %s %s",
		 VMMOUSE_PSNAME, VMMOUSE_VENDOR, VMMOUSE_NAME);
	abs_dev->phys = priv->phys;
	abs_dev->name = priv->dev_name;
	abs_dev->id.bustype = BUS_I8042;
	abs_dev->id.vendor = 0x0002;
	abs_dev->id.product = PSMOUSE_VMMOUSE;
	abs_dev->id.version = psmouse->model;
	abs_dev->dev.parent = &psmouse->ps2dev.serio->dev;

	/* Set absolute device capabilities */
	input_set_capability(abs_dev, EV_KEY, BTN_LEFT);
	input_set_capability(abs_dev, EV_KEY, BTN_RIGHT);
	input_set_capability(abs_dev, EV_KEY, BTN_MIDDLE);
	input_set_capability(abs_dev, EV_ABS, ABS_X);
	input_set_capability(abs_dev, EV_ABS, ABS_Y);
	input_set_abs_params(abs_dev, ABS_X, 0, VMMOUSE_MAX_X, 0, 0);
	input_set_abs_params(abs_dev, ABS_Y, 0, VMMOUSE_MAX_Y, 0, 0);

	error = input_register_device(priv->abs_dev);
	if (error)
		goto init_fail;

	/* Add wheel capability to the relative device */
	input_set_capability(rel_dev, EV_REL, REL_WHEEL);

	psmouse->protocol_handler = vmmouse_process_byte;
	psmouse->disconnect = vmmouse_disconnect;
	psmouse->reconnect = vmmouse_reconnect;

	return 0;

init_fail:
	vmmouse_disable(psmouse);
	psmouse_reset(psmouse);
	input_free_device(abs_dev);
	kfree(priv);
	psmouse->private = NULL;

	return error;
}

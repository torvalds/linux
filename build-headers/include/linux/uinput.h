/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 *  User level driver support for input subsystem
 *
 * Heavily based on evdev.c by Vojtech Pavlik
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Author: Aristeu Sergio Rozanski Filho <aris@cathedrallabs.org>
 *
 * Changes/Revisions:
 *	0.5	08/13/2015 (David Herrmann <dh.herrmann@gmail.com> &
 *			    Benjamin Tissoires <benjamin.tissoires@redhat.com>)
 *		- add UI_DEV_SETUP ioctl
 *		- add UI_ABS_SETUP ioctl
 *		- add UI_GET_VERSION ioctl
 *	0.4	01/09/2014 (Benjamin Tissoires <benjamin.tissoires@redhat.com>)
 *		- add UI_GET_SYSNAME ioctl
 *	0.3	24/05/2006 (Anssi Hannula <anssi.hannulagmail.com>)
 *		- update ff support for the changes in kernel interface
 *		- add UINPUT_VERSION
 *	0.2	16/10/2004 (Micah Dowty <micah@navi.cx>)
 *		- added force feedback support
 *             - added UI_SET_PHYS
 *	0.1	20/06/2002
 *		- first public version
 */
#ifndef __UINPUT_H_
#define __UINPUT_H_

#include <linux/types.h>
#include <linux/input.h>

#define UINPUT_VERSION		5
#define UINPUT_MAX_NAME_SIZE	80

struct uinput_ff_upload {
	__u32			request_id;
	__s32			retval;
	struct ff_effect	effect;
	struct ff_effect	old;
};

struct uinput_ff_erase {
	__u32			request_id;
	__s32			retval;
	__u32			effect_id;
};

/* ioctl */
#define UINPUT_IOCTL_BASE	'U'
#define UI_DEV_CREATE		_IO(UINPUT_IOCTL_BASE, 1)
#define UI_DEV_DESTROY		_IO(UINPUT_IOCTL_BASE, 2)

struct uinput_setup {
	struct input_id id;
	char name[UINPUT_MAX_NAME_SIZE];
	__u32 ff_effects_max;
};

/**
 * UI_DEV_SETUP - Set device parameters for setup
 *
 * This ioctl sets parameters for the input device to be created.  It
 * supersedes the old "struct uinput_user_dev" method, which wrote this data
 * via write(). To actually set the absolute axes UI_ABS_SETUP should be
 * used.
 *
 * The ioctl takes a "struct uinput_setup" object as argument. The fields of
 * this object are as follows:
 *              id: See the description of "struct input_id". This field is
 *                  copied unchanged into the new device.
 *            name: This is used unchanged as name for the new device.
 *  ff_effects_max: This limits the maximum numbers of force-feedback effects.
 *                  See below for a description of FF with uinput.
 *
 * This ioctl can be called multiple times and will overwrite previous values.
 * If this ioctl fails with -EINVAL, it is recommended to use the old
 * "uinput_user_dev" method via write() as a fallback, in case you run on an
 * old kernel that does not support this ioctl.
 *
 * This ioctl may fail with -EINVAL if it is not supported or if you passed
 * incorrect values, -ENOMEM if the kernel runs out of memory or -EFAULT if the
 * passed uinput_setup object cannot be read/written.
 * If this call fails, partial data may have already been applied to the
 * internal device.
 */
#define UI_DEV_SETUP _IOW(UINPUT_IOCTL_BASE, 3, struct uinput_setup)

struct uinput_abs_setup {
	__u16  code; /* axis code */
	/* __u16 filler; */
	struct input_absinfo absinfo;
};

/**
 * UI_ABS_SETUP - Set absolute axis information for the device to setup
 *
 * This ioctl sets one absolute axis information for the input device to be
 * created. It supersedes the old "struct uinput_user_dev" method, which wrote
 * part of this data and the content of UI_DEV_SETUP via write().
 *
 * The ioctl takes a "struct uinput_abs_setup" object as argument. The fields
 * of this object are as follows:
 *            code: The corresponding input code associated with this axis
 *                  (ABS_X, ABS_Y, etc...)
 *         absinfo: See "struct input_absinfo" for a description of this field.
 *                  This field is copied unchanged into the kernel for the
 *                  specified axis. If the axis is not enabled via
 *                  UI_SET_ABSBIT, this ioctl will enable it.
 *
 * This ioctl can be called multiple times and will overwrite previous values.
 * If this ioctl fails with -EINVAL, it is recommended to use the old
 * "uinput_user_dev" method via write() as a fallback, in case you run on an
 * old kernel that does not support this ioctl.
 *
 * This ioctl may fail with -EINVAL if it is not supported or if you passed
 * incorrect values, -ENOMEM if the kernel runs out of memory or -EFAULT if the
 * passed uinput_setup object cannot be read/written.
 * If this call fails, partial data may have already been applied to the
 * internal device.
 */
#define UI_ABS_SETUP _IOW(UINPUT_IOCTL_BASE, 4, struct uinput_abs_setup)

#define UI_SET_EVBIT		_IOW(UINPUT_IOCTL_BASE, 100, int)
#define UI_SET_KEYBIT		_IOW(UINPUT_IOCTL_BASE, 101, int)
#define UI_SET_RELBIT		_IOW(UINPUT_IOCTL_BASE, 102, int)
#define UI_SET_ABSBIT		_IOW(UINPUT_IOCTL_BASE, 103, int)
#define UI_SET_MSCBIT		_IOW(UINPUT_IOCTL_BASE, 104, int)
#define UI_SET_LEDBIT		_IOW(UINPUT_IOCTL_BASE, 105, int)
#define UI_SET_SNDBIT		_IOW(UINPUT_IOCTL_BASE, 106, int)
#define UI_SET_FFBIT		_IOW(UINPUT_IOCTL_BASE, 107, int)
#define UI_SET_PHYS		_IOW(UINPUT_IOCTL_BASE, 108, char*)
#define UI_SET_SWBIT		_IOW(UINPUT_IOCTL_BASE, 109, int)
#define UI_SET_PROPBIT		_IOW(UINPUT_IOCTL_BASE, 110, int)

#define UI_BEGIN_FF_UPLOAD	_IOWR(UINPUT_IOCTL_BASE, 200, struct uinput_ff_upload)
#define UI_END_FF_UPLOAD	_IOW(UINPUT_IOCTL_BASE, 201, struct uinput_ff_upload)
#define UI_BEGIN_FF_ERASE	_IOWR(UINPUT_IOCTL_BASE, 202, struct uinput_ff_erase)
#define UI_END_FF_ERASE		_IOW(UINPUT_IOCTL_BASE, 203, struct uinput_ff_erase)

/**
 * UI_GET_SYSNAME - get the sysfs name of the created uinput device
 *
 * @return the sysfs name of the created virtual input device.
 * The complete sysfs path is then /sys/devices/virtual/input/--NAME--
 * Usually, it is in the form "inputN"
 */
#define UI_GET_SYSNAME(len)	_IOC(_IOC_READ, UINPUT_IOCTL_BASE, 44, len)

/**
 * UI_GET_VERSION - Return version of uinput protocol
 *
 * This writes uinput protocol version implemented by the kernel into
 * the integer pointed to by the ioctl argument. The protocol version
 * is hard-coded in the kernel and is independent of the uinput device.
 */
#define UI_GET_VERSION		_IOR(UINPUT_IOCTL_BASE, 45, unsigned int)

/*
 * To write a force-feedback-capable driver, the upload_effect
 * and erase_effect callbacks in input_dev must be implemented.
 * The uinput driver will generate a fake input event when one of
 * these callbacks are invoked. The userspace code then uses
 * ioctls to retrieve additional parameters and send the return code.
 * The callback blocks until this return code is sent.
 *
 * The described callback mechanism is only used if ff_effects_max
 * is set.
 *
 * To implement upload_effect():
 *   1. Wait for an event with type == EV_UINPUT and code == UI_FF_UPLOAD.
 *      A request ID will be given in 'value'.
 *   2. Allocate a uinput_ff_upload struct, fill in request_id with
 *      the 'value' from the EV_UINPUT event.
 *   3. Issue a UI_BEGIN_FF_UPLOAD ioctl, giving it the
 *      uinput_ff_upload struct. It will be filled in with the
 *      ff_effects passed to upload_effect().
 *   4. Perform the effect upload, and place a return code back into
        the uinput_ff_upload struct.
 *   5. Issue a UI_END_FF_UPLOAD ioctl, also giving it the
 *      uinput_ff_upload_effect struct. This will complete execution
 *      of our upload_effect() handler.
 *
 * To implement erase_effect():
 *   1. Wait for an event with type == EV_UINPUT and code == UI_FF_ERASE.
 *      A request ID will be given in 'value'.
 *   2. Allocate a uinput_ff_erase struct, fill in request_id with
 *      the 'value' from the EV_UINPUT event.
 *   3. Issue a UI_BEGIN_FF_ERASE ioctl, giving it the
 *      uinput_ff_erase struct. It will be filled in with the
 *      effect ID passed to erase_effect().
 *   4. Perform the effect erasure, and place a return code back
 *      into the uinput_ff_erase struct.
 *   5. Issue a UI_END_FF_ERASE ioctl, also giving it the
 *      uinput_ff_erase_effect struct. This will complete execution
 *      of our erase_effect() handler.
 */

/*
 * This is the new event type, used only by uinput.
 * 'code' is UI_FF_UPLOAD or UI_FF_ERASE, and 'value'
 * is the unique request ID. This number was picked
 * arbitrarily, above EV_MAX (since the input system
 * never sees it) but in the range of a 16-bit int.
 */
#define EV_UINPUT		0x0101
#define UI_FF_UPLOAD		1
#define UI_FF_ERASE		2

struct uinput_user_dev {
	char name[UINPUT_MAX_NAME_SIZE];
	struct input_id id;
	__u32 ff_effects_max;
	__s32 absmax[ABS_CNT];
	__s32 absmin[ABS_CNT];
	__s32 absfuzz[ABS_CNT];
	__s32 absflat[ABS_CNT];
};
#endif /* __UINPUT_H_ */

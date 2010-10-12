/****************************************************************

 Siano Mobile Silicon, Inc.
 MDTV receiver kernel modules.
 Copyright (C) 2006-2009, Uri Shkolnik

 Copyright (c) 2010 - Mauro Carvalho Chehab
	- Ported the driver to use rc-core
	- IR raw event decoding is now done at rc-core
	- Code almost re-written

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.

 ****************************************************************/


#include <linux/types.h>
#include <linux/input.h>

#include "smscoreapi.h"
#include "smsir.h"
#include "sms-cards.h"

#define MODULE_NAME "smsmdtv"

void sms_ir_event(struct smscore_device_t *coredev, const char *buf, int len)
{
	int i;
	const s32 *samples = (const void *)buf;

	for (i = 0; i < len >> 2; i++) {
		struct ir_raw_event ev;

		ev.duration = abs(samples[i]) * 1000; /* Convert to ns */
		ev.pulse = (samples[i] > 0) ? false : true;

		ir_raw_event_store(coredev->ir.input_dev, &ev);
	}
	ir_raw_event_handle(coredev->ir.input_dev);
}

int sms_ir_init(struct smscore_device_t *coredev)
{
	struct input_dev *input_dev;
	int board_id = smscore_get_board_id(coredev);

	sms_log("Allocating input device");
	input_dev = input_allocate_device();
	if (!input_dev)	{
		sms_err("Not enough memory");
		return -ENOMEM;
	}

	coredev->ir.input_dev = input_dev;

	coredev->ir.controller = 0;	/* Todo: vega/nova SPI number */
	coredev->ir.timeout = IR_DEFAULT_TIMEOUT;
	sms_log("IR port %d, timeout %d ms",
			coredev->ir.controller, coredev->ir.timeout);

	snprintf(coredev->ir.name, sizeof(coredev->ir.name),
		 "SMS IR (%s)", sms_get_board(board_id)->name);

	strlcpy(coredev->ir.phys, coredev->devpath, sizeof(coredev->ir.phys));
	strlcat(coredev->ir.phys, "/ir0", sizeof(coredev->ir.phys));

	input_dev->name = coredev->ir.name;
	input_dev->phys = coredev->ir.phys;
	input_dev->dev.parent = coredev->device;

#if 0
	/* TODO: properly initialize the parameters bellow */
	input_dev->id.bustype = BUS_USB;
	input_dev->id.version = 1;
	input_dev->id.vendor = le16_to_cpu(dev->udev->descriptor.idVendor);
	input_dev->id.product = le16_to_cpu(dev->udev->descriptor.idProduct);
#endif

	coredev->ir.props.priv = coredev;
	coredev->ir.props.driver_type = RC_DRIVER_IR_RAW;
	coredev->ir.props.allowed_protos = IR_TYPE_ALL;

	sms_log("Input device (IR) %s is set for key events", input_dev->name);

	if (ir_input_register(input_dev, sms_get_board(board_id)->rc_codes,
			      &coredev->ir.props, MODULE_NAME)) {
		sms_err("Failed to register device");
		input_free_device(input_dev);
		return -EACCES;
	}

	return 0;
}

void sms_ir_exit(struct smscore_device_t *coredev)
{
	if (coredev->ir.input_dev)
		ir_input_unregister(coredev->ir.input_dev);

	sms_log("");
}

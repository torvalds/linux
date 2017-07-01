/*
	Mantis PCI bridge driver

	Copyright (C) Manu Abraham (abraham.manu@gmail.com)

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.
*/

#include <media/rc-core.h>
#include <linux/pci.h>

#include "dmxdev.h"
#include "dvbdev.h"
#include "dvb_demux.h"
#include "dvb_frontend.h"
#include "dvb_net.h"

#include "mantis_common.h"
#include "mantis_input.h"

#define MODULE_NAME "mantis_core"

void mantis_input_process(struct mantis_pci *mantis, int scancode)
{
	if (mantis->rc)
		rc_keydown(mantis->rc, RC_TYPE_UNKNOWN, scancode, 0);
}

int mantis_input_init(struct mantis_pci *mantis)
{
	struct rc_dev *dev;
	int err;

	dev = rc_allocate_device(RC_DRIVER_SCANCODE);
	if (!dev) {
		dprintk(MANTIS_ERROR, 1, "Remote device allocation failed");
		err = -ENOMEM;
		goto out;
	}

	snprintf(mantis->device_name, sizeof(mantis->device_name),
		 "Mantis %s IR receiver", mantis->hwconfig->model_name);
	snprintf(mantis->input_phys, sizeof(mantis->input_phys),
		 "pci-%s/ir0", pci_name(mantis->pdev));

	dev->device_name        = mantis->device_name;
	dev->input_phys         = mantis->input_phys;
	dev->input_id.bustype   = BUS_PCI;
	dev->input_id.vendor    = mantis->vendor_id;
	dev->input_id.product   = mantis->device_id;
	dev->input_id.version   = 1;
	dev->driver_name        = MODULE_NAME;
	dev->map_name           = mantis->rc_map_name ? : RC_MAP_EMPTY;
	dev->dev.parent         = &mantis->pdev->dev;

	err = rc_register_device(dev);
	if (err) {
		dprintk(MANTIS_ERROR, 1, "IR device registration failed, ret = %d", err);
		goto out_dev;
	}

	mantis->rc = dev;
	return 0;

out_dev:
	rc_free_device(dev);
out:
	return err;
}
EXPORT_SYMBOL_GPL(mantis_input_init);

void mantis_input_exit(struct mantis_pci *mantis)
{
	rc_unregister_device(mantis->rc);
}
EXPORT_SYMBOL_GPL(mantis_input_exit);

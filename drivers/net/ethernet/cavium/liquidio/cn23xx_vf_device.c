/**********************************************************************
 * Author: Cavium, Inc.
 *
 * Contact: support@cavium.com
 *          Please include "LiquidIO" in the subject.
 *
 * Copyright (c) 2003-2016 Cavium, Inc.
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 *
 * This file is distributed in the hope that it will be useful, but
 * AS-IS and WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, TITLE, or
 * NONINFRINGEMENT.  See the GNU General Public License for more details.
 ***********************************************************************/
#include <linux/pci.h>
#include <linux/netdevice.h>
#include "liquidio_common.h"
#include "octeon_droq.h"
#include "octeon_iq.h"
#include "response_manager.h"
#include "octeon_device.h"
#include "cn23xx_vf_device.h"
#include "octeon_main.h"

int cn23xx_setup_octeon_vf_device(struct octeon_device *oct)
{
	struct octeon_cn23xx_vf *cn23xx = (struct octeon_cn23xx_vf *)oct->chip;

	if (octeon_map_pci_barx(oct, 0, 0))
		return 1;

	cn23xx->conf  = oct_get_config_info(oct, LIO_23XX);
	if (!cn23xx->conf) {
		dev_err(&oct->pci_dev->dev, "%s No Config found for CN23XX\n",
			__func__);
		octeon_unmap_pci_barx(oct, 0);
		return 1;
	}

	return 0;
}

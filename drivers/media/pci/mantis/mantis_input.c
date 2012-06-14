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

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <media/rc-core.h>
#include <linux/pci.h>

#include "dmxdev.h"
#include "dvbdev.h"
#include "dvb_demux.h"
#include "dvb_frontend.h"
#include "dvb_net.h"

#include "mantis_common.h"
#include "mantis_reg.h"
#include "mantis_uart.h"

#define MODULE_NAME "mantis_core"
#define RC_MAP_MANTIS "rc-mantis"

static struct rc_map_table mantis_ir_table[] = {
	{ 0x29, KEY_POWER	},
	{ 0x28, KEY_FAVORITES	},
	{ 0x30, KEY_TEXT	},
	{ 0x17, KEY_INFO	}, /* Preview */
	{ 0x23, KEY_EPG		},
	{ 0x3b, KEY_F22		}, /* Record List */
	{ 0x3c, KEY_1		},
	{ 0x3e, KEY_2		},
	{ 0x39, KEY_3		},
	{ 0x36, KEY_4		},
	{ 0x22, KEY_5		},
	{ 0x20, KEY_6		},
	{ 0x32, KEY_7		},
	{ 0x26, KEY_8		},
	{ 0x24, KEY_9		},
	{ 0x2a, KEY_0		},

	{ 0x33, KEY_CANCEL	},
	{ 0x2c, KEY_BACK	},
	{ 0x15, KEY_CLEAR	},
	{ 0x3f, KEY_TAB		},
	{ 0x10, KEY_ENTER	},
	{ 0x14, KEY_UP		},
	{ 0x0d, KEY_RIGHT	},
	{ 0x0e, KEY_DOWN	},
	{ 0x11, KEY_LEFT	},

	{ 0x21, KEY_VOLUMEUP	},
	{ 0x35, KEY_VOLUMEDOWN	},
	{ 0x3d, KEY_CHANNELDOWN	},
	{ 0x3a, KEY_CHANNELUP	},
	{ 0x2e, KEY_RECORD	},
	{ 0x2b, KEY_PLAY	},
	{ 0x13, KEY_PAUSE	},
	{ 0x25, KEY_STOP	},

	{ 0x1f, KEY_REWIND	},
	{ 0x2d, KEY_FASTFORWARD	},
	{ 0x1e, KEY_PREVIOUS	}, /* Replay |< */
	{ 0x1d, KEY_NEXT	}, /* Skip   >| */

	{ 0x0b, KEY_CAMERA	}, /* Capture */
	{ 0x0f, KEY_LANGUAGE	}, /* SAP */
	{ 0x18, KEY_MODE	}, /* PIP */
	{ 0x12, KEY_ZOOM	}, /* Full screen */
	{ 0x1c, KEY_SUBTITLE	},
	{ 0x2f, KEY_MUTE	},
	{ 0x16, KEY_F20		}, /* L/R */
	{ 0x38, KEY_F21		}, /* Hibernate */

	{ 0x37, KEY_SWITCHVIDEOMODE }, /* A/V */
	{ 0x31, KEY_AGAIN	}, /* Recall */
	{ 0x1a, KEY_KPPLUS	}, /* Zoom+ */
	{ 0x19, KEY_KPMINUS	}, /* Zoom- */
	{ 0x27, KEY_RED		},
	{ 0x0C, KEY_GREEN	},
	{ 0x01, KEY_YELLOW	},
	{ 0x00, KEY_BLUE	},
};

static struct rc_map_list ir_mantis_map = {
	.map = {
		.scan = mantis_ir_table,
		.size = ARRAY_SIZE(mantis_ir_table),
		.rc_type = RC_TYPE_UNKNOWN,
		.name = RC_MAP_MANTIS,
	}
};

int mantis_input_init(struct mantis_pci *mantis)
{
	struct rc_dev *dev;
	int err;

	err = rc_map_register(&ir_mantis_map);
	if (err)
		goto out;

	dev = rc_allocate_device();
	if (!dev) {
		dprintk(MANTIS_ERROR, 1, "Remote device allocation failed");
		err = -ENOMEM;
		goto out_map;
	}

	sprintf(mantis->input_name, "Mantis %s IR receiver", mantis->hwconfig->model_name);
	sprintf(mantis->input_phys, "pci-%s/ir0", pci_name(mantis->pdev));

	dev->input_name         = mantis->input_name;
	dev->input_phys         = mantis->input_phys;
	dev->input_id.bustype   = BUS_PCI;
	dev->input_id.vendor    = mantis->vendor_id;
	dev->input_id.product   = mantis->device_id;
	dev->input_id.version   = 1;
	dev->driver_name        = MODULE_NAME;
	dev->map_name           = RC_MAP_MANTIS;
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
out_map:
	rc_map_unregister(&ir_mantis_map);
out:
	return err;
}

int mantis_exit(struct mantis_pci *mantis)
{
	rc_unregister_device(mantis->rc);
	rc_map_unregister(&ir_mantis_map);
	return 0;
}


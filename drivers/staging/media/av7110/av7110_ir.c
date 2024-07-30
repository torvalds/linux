// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for the remote control of SAA7146 based AV7110 cards
 *
 * Copyright (C) 1999-2003 Holger Waechtler <holger@convergence.de>
 * Copyright (C) 2003-2007 Oliver Endriss <o.endriss@gmx.de>
 * Copyright (C) 2019 Sean Young <sean@mess.org>
 */

#include <linux/kernel.h>
#include <media/rc-core.h>

#include "av7110.h"
#include "av7110_hw.h"

#define IR_RC5		0
#define IR_RCMM		1
#define IR_RC5_EXT	2 /* internal only */

/* interrupt handler */
void av7110_ir_handler(struct av7110 *av7110, u32 ircom)
{
	struct rc_dev *rcdev = av7110->ir.rcdev;
	enum rc_proto proto;
	u32 command, addr, scancode;
	u32 toggle;

	dprintk(4, "ir command = %08x\n", ircom);

	if (rcdev) {
		switch (av7110->ir.ir_config) {
		case IR_RC5: /* RC5: 5 bits device address, 6 bits command */
			command = ircom & 0x3f;
			addr = (ircom >> 6) & 0x1f;
			scancode = RC_SCANCODE_RC5(addr, command);
			toggle = ircom & 0x0800;
			proto = RC_PROTO_RC5;
			break;

		case IR_RCMM: /* RCMM: 32 bits scancode */
			scancode = ircom & ~0x8000;
			toggle = ircom & 0x8000;
			proto = RC_PROTO_RCMM32;
			break;

		case IR_RC5_EXT:
			/*
			 * extended RC5: 5 bits device address, 7 bits command
			 *
			 * Extended RC5 uses only one start bit. The second
			 * start bit is re-assigned bit 6 of the command bit.
			 */
			command = ircom & 0x3f;
			addr = (ircom >> 6) & 0x1f;
			if (!(ircom & 0x1000))
				command |= 0x40;
			scancode = RC_SCANCODE_RC5(addr, command);
			toggle = ircom & 0x0800;
			proto = RC_PROTO_RC5;
			break;
		default:
			dprintk(2, "unknown ir config %d\n", av7110->ir.ir_config);
			return;
		}

		rc_keydown(rcdev, proto, scancode, toggle != 0);
	}
}

int av7110_set_ir_config(struct av7110 *av7110)
{
	dprintk(4, "ir config = %08x\n", av7110->ir.ir_config);

	return av7110_fw_cmd(av7110, COMTYPE_PIDFILTER, SetIR, 1,
			     av7110->ir.ir_config);
}

static int change_protocol(struct rc_dev *rcdev, u64 *rc_type)
{
	struct av7110 *av7110 = rcdev->priv;
	u32 ir_config;

	if (*rc_type & RC_PROTO_BIT_RCMM32) {
		ir_config = IR_RCMM;
		*rc_type = RC_PROTO_BIT_RCMM32;
	} else if (*rc_type & RC_PROTO_BIT_RC5) {
		if (FW_VERSION(av7110->arm_app) >= 0x2620)
			ir_config = IR_RC5_EXT;
		else
			ir_config = IR_RC5;
		*rc_type = RC_PROTO_BIT_RC5;
	} else {
		return -EINVAL;
	}

	if (ir_config == av7110->ir.ir_config)
		return 0;

	av7110->ir.ir_config = ir_config;

	return av7110_set_ir_config(av7110);
}

int av7110_ir_init(struct av7110 *av7110)
{
	struct rc_dev *rcdev;
	struct pci_dev *pci;
	int ret;

	rcdev = rc_allocate_device(RC_DRIVER_SCANCODE);
	if (!rcdev)
		return -ENOMEM;

	pci = av7110->dev->pci;

	snprintf(av7110->ir.input_phys, sizeof(av7110->ir.input_phys),
		 "pci-%s/ir0", pci_name(pci));

	rcdev->device_name = av7110->card_name;
	rcdev->driver_name = KBUILD_MODNAME;
	rcdev->input_phys = av7110->ir.input_phys;
	rcdev->input_id.bustype = BUS_PCI;
	rcdev->input_id.version = 2;
	if (pci->subsystem_vendor) {
		rcdev->input_id.vendor	= pci->subsystem_vendor;
		rcdev->input_id.product = pci->subsystem_device;
	} else {
		rcdev->input_id.vendor	= pci->vendor;
		rcdev->input_id.product = pci->device;
	}

	rcdev->dev.parent = &pci->dev;
	rcdev->allowed_protocols = RC_PROTO_BIT_RC5 | RC_PROTO_BIT_RCMM32;
	rcdev->change_protocol = change_protocol;
	rcdev->map_name = RC_MAP_HAUPPAUGE;
	rcdev->priv = av7110;

	av7110->ir.rcdev = rcdev;
	av7110->ir.ir_config = IR_RC5;
	av7110_set_ir_config(av7110);

	ret = rc_register_device(rcdev);
	if (ret) {
		av7110->ir.rcdev = NULL;
		rc_free_device(rcdev);
	}

	return ret;
}

void av7110_ir_exit(struct av7110 *av7110)
{
	rc_unregister_device(av7110->ir.rcdev);
}

//MODULE_AUTHOR("Holger Waechtler <holger@convergence.de>, Oliver Endriss <o.endriss@gmx.de>");
//MODULE_LICENSE("GPL");

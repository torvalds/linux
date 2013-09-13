/*
 *  Driver for the Conexant CX23885/7/8 PCIe bridge
 *
 *  Infrared remote control input device
 *
 *  Most of this file is
 *
 *  Copyright (C) 2009  Andy Walls <awalls@md.metrocast.net>
 *
 *  However, the cx23885_input_{init,fini} functions contained herein are
 *  derived from Linux kernel files linux/media/video/.../...-input.c marked as:
 *
 *  Copyright (C) 2008 <srinivasa.deevi at conexant dot com>
 *  Copyright (C) 2005 Ludovico Cavedon <cavedon@sssup.it>
 *		       Markus Rechberger <mrechberger@gmail.com>
 *		       Mauro Carvalho Chehab <mchehab@infradead.org>
 *		       Sascha Sommer <saschasommer@freenet.de>
 *  Copyright (C) 2004, 2005 Chris Pascoe
 *  Copyright (C) 2003, 2004 Gerd Knorr
 *  Copyright (C) 2003 Pavel Machek
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 *  02110-1301, USA.
 */

#include <linux/slab.h>
#include <media/rc-core.h>
#include <media/v4l2-subdev.h>

#include "cx23885.h"
#include "cx23885-input.h"

#define MODULE_NAME "cx23885"

static void cx23885_input_process_measurements(struct cx23885_dev *dev,
					       bool overrun)
{
	struct cx23885_kernel_ir *kernel_ir = dev->kernel_ir;

	ssize_t num;
	int count, i;
	bool handle = false;
	struct ir_raw_event ir_core_event[64];

	do {
		num = 0;
		v4l2_subdev_call(dev->sd_ir, ir, rx_read, (u8 *) ir_core_event,
				 sizeof(ir_core_event), &num);

		count = num / sizeof(struct ir_raw_event);

		for (i = 0; i < count; i++) {
			ir_raw_event_store(kernel_ir->rc,
					   &ir_core_event[i]);
			handle = true;
		}
	} while (num != 0);

	if (overrun)
		ir_raw_event_reset(kernel_ir->rc);
	else if (handle)
		ir_raw_event_handle(kernel_ir->rc);
}

void cx23885_input_rx_work_handler(struct cx23885_dev *dev, u32 events)
{
	struct v4l2_subdev_ir_parameters params;
	int overrun, data_available;

	if (dev->sd_ir == NULL || events == 0)
		return;

	switch (dev->board) {
	case CX23885_BOARD_HAUPPAUGE_HVR1270:
	case CX23885_BOARD_HAUPPAUGE_HVR1850:
	case CX23885_BOARD_HAUPPAUGE_HVR1290:
	case CX23885_BOARD_TERRATEC_CINERGY_T_PCIE_DUAL:
	case CX23885_BOARD_TEVII_S470:
	case CX23885_BOARD_HAUPPAUGE_HVR1250:
	case CX23885_BOARD_MYGICA_X8507:
		/*
		 * The only boards we handle right now.  However other boards
		 * using the CX2388x integrated IR controller should be similar
		 */
		break;
	default:
		return;
	}

	overrun = events & (V4L2_SUBDEV_IR_RX_SW_FIFO_OVERRUN |
			    V4L2_SUBDEV_IR_RX_HW_FIFO_OVERRUN);

	data_available = events & (V4L2_SUBDEV_IR_RX_END_OF_RX_DETECTED |
				   V4L2_SUBDEV_IR_RX_FIFO_SERVICE_REQ);

	if (overrun) {
		/* If there was a FIFO overrun, stop the device */
		v4l2_subdev_call(dev->sd_ir, ir, rx_g_parameters, &params);
		params.enable = false;
		/* Mitigate race with cx23885_input_ir_stop() */
		params.shutdown = atomic_read(&dev->ir_input_stopping);
		v4l2_subdev_call(dev->sd_ir, ir, rx_s_parameters, &params);
	}

	if (data_available)
		cx23885_input_process_measurements(dev, overrun);

	if (overrun) {
		/* If there was a FIFO overrun, clear & restart the device */
		params.enable = true;
		/* Mitigate race with cx23885_input_ir_stop() */
		params.shutdown = atomic_read(&dev->ir_input_stopping);
		v4l2_subdev_call(dev->sd_ir, ir, rx_s_parameters, &params);
	}
}

static int cx23885_input_ir_start(struct cx23885_dev *dev)
{
	struct v4l2_subdev_ir_parameters params;

	if (dev->sd_ir == NULL)
		return -ENODEV;

	atomic_set(&dev->ir_input_stopping, 0);

	v4l2_subdev_call(dev->sd_ir, ir, rx_g_parameters, &params);
	switch (dev->board) {
	case CX23885_BOARD_HAUPPAUGE_HVR1270:
	case CX23885_BOARD_HAUPPAUGE_HVR1850:
	case CX23885_BOARD_HAUPPAUGE_HVR1290:
	case CX23885_BOARD_HAUPPAUGE_HVR1250:
	case CX23885_BOARD_MYGICA_X8507:
		/*
		 * The IR controller on this board only returns pulse widths.
		 * Any other mode setting will fail to set up the device.
		*/
		params.mode = V4L2_SUBDEV_IR_MODE_PULSE_WIDTH;
		params.enable = true;
		params.interrupt_enable = true;
		params.shutdown = false;

		/* Setup for baseband compatible with both RC-5 and RC-6A */
		params.modulation = false;
		/* RC-5:  2,222,222 ns = 1/36 kHz * 32 cycles * 2 marks * 1.25*/
		/* RC-6A: 3,333,333 ns = 1/36 kHz * 16 cycles * 6 marks * 1.25*/
		params.max_pulse_width = 3333333; /* ns */
		/* RC-5:    666,667 ns = 1/36 kHz * 32 cycles * 1 mark * 0.75 */
		/* RC-6A:   333,333 ns = 1/36 kHz * 16 cycles * 1 mark * 0.75 */
		params.noise_filter_min_width = 333333; /* ns */
		/*
		 * This board has inverted receive sense:
		 * mark is received as low logic level;
		 * falling edges are detected as rising edges; etc.
		 */
		params.invert_level = true;
		break;
	case CX23885_BOARD_TERRATEC_CINERGY_T_PCIE_DUAL:
	case CX23885_BOARD_TEVII_S470:
		/*
		 * The IR controller on this board only returns pulse widths.
		 * Any other mode setting will fail to set up the device.
		 */
		params.mode = V4L2_SUBDEV_IR_MODE_PULSE_WIDTH;
		params.enable = true;
		params.interrupt_enable = true;
		params.shutdown = false;

		/* Setup for a standard NEC protocol */
		params.carrier_freq = 37917; /* Hz, 455 kHz/12 for NEC */
		params.carrier_range_lower = 33000; /* Hz */
		params.carrier_range_upper = 43000; /* Hz */
		params.duty_cycle = 33; /* percent, 33 percent for NEC */

		/*
		 * NEC max pulse width: (64/3)/(455 kHz/12) * 16 nec_units
		 * (64/3)/(455 kHz/12) * 16 nec_units * 1.375 = 12378022 ns
		 */
		params.max_pulse_width = 12378022; /* ns */

		/*
		 * NEC noise filter min width: (64/3)/(455 kHz/12) * 1 nec_unit
		 * (64/3)/(455 kHz/12) * 1 nec_units * 0.625 = 351648 ns
		 */
		params.noise_filter_min_width = 351648; /* ns */

		params.modulation = false;
		params.invert_level = true;
		break;
	}
	v4l2_subdev_call(dev->sd_ir, ir, rx_s_parameters, &params);
	return 0;
}

static int cx23885_input_ir_open(struct rc_dev *rc)
{
	struct cx23885_kernel_ir *kernel_ir = rc->priv;

	if (kernel_ir->cx == NULL)
		return -ENODEV;

	return cx23885_input_ir_start(kernel_ir->cx);
}

static void cx23885_input_ir_stop(struct cx23885_dev *dev)
{
	struct v4l2_subdev_ir_parameters params;

	if (dev->sd_ir == NULL)
		return;

	/*
	 * Stop the sd_ir subdevice from generating notifications and
	 * scheduling work.
	 * It is shutdown this way in order to mitigate a race with
	 * cx23885_input_rx_work_handler() in the overrun case, which could
	 * re-enable the subdevice.
	 */
	atomic_set(&dev->ir_input_stopping, 1);
	v4l2_subdev_call(dev->sd_ir, ir, rx_g_parameters, &params);
	while (params.shutdown == false) {
		params.enable = false;
		params.interrupt_enable = false;
		params.shutdown = true;
		v4l2_subdev_call(dev->sd_ir, ir, rx_s_parameters, &params);
		v4l2_subdev_call(dev->sd_ir, ir, rx_g_parameters, &params);
	}
	flush_work(&dev->cx25840_work);
	flush_work(&dev->ir_rx_work);
	flush_work(&dev->ir_tx_work);
}

static void cx23885_input_ir_close(struct rc_dev *rc)
{
	struct cx23885_kernel_ir *kernel_ir = rc->priv;

	if (kernel_ir->cx != NULL)
		cx23885_input_ir_stop(kernel_ir->cx);
}

int cx23885_input_init(struct cx23885_dev *dev)
{
	struct cx23885_kernel_ir *kernel_ir;
	struct rc_dev *rc;
	char *rc_map;
	enum rc_driver_type driver_type;
	unsigned long allowed_protos;

	int ret;

	/*
	 * If the IR device (hardware registers, chip, GPIO lines, etc.) isn't
	 * encapsulated in a v4l2_subdev, then I'm not going to deal with it.
	 */
	if (dev->sd_ir == NULL)
		return -ENODEV;

	switch (dev->board) {
	case CX23885_BOARD_HAUPPAUGE_HVR1270:
	case CX23885_BOARD_HAUPPAUGE_HVR1850:
	case CX23885_BOARD_HAUPPAUGE_HVR1290:
	case CX23885_BOARD_HAUPPAUGE_HVR1250:
		/* Integrated CX2388[58] IR controller */
		driver_type = RC_DRIVER_IR_RAW;
		allowed_protos = RC_BIT_ALL;
		/* The grey Hauppauge RC-5 remote */
		rc_map = RC_MAP_HAUPPAUGE;
		break;
	case CX23885_BOARD_TERRATEC_CINERGY_T_PCIE_DUAL:
		/* Integrated CX23885 IR controller */
		driver_type = RC_DRIVER_IR_RAW;
		allowed_protos = RC_BIT_NEC;
		/* The grey Terratec remote with orange buttons */
		rc_map = RC_MAP_NEC_TERRATEC_CINERGY_XS;
		break;
	case CX23885_BOARD_TEVII_S470:
		/* Integrated CX23885 IR controller */
		driver_type = RC_DRIVER_IR_RAW;
		allowed_protos = RC_BIT_ALL;
		/* A guess at the remote */
		rc_map = RC_MAP_TEVII_NEC;
		break;
	case CX23885_BOARD_MYGICA_X8507:
		/* Integrated CX23885 IR controller */
		driver_type = RC_DRIVER_IR_RAW;
		allowed_protos = RC_BIT_ALL;
		/* A guess at the remote */
		rc_map = RC_MAP_TOTAL_MEDIA_IN_HAND_02;
		break;
	default:
		return -ENODEV;
	}

	/* cx23885 board instance kernel IR state */
	kernel_ir = kzalloc(sizeof(struct cx23885_kernel_ir), GFP_KERNEL);
	if (kernel_ir == NULL)
		return -ENOMEM;

	kernel_ir->cx = dev;
	kernel_ir->name = kasprintf(GFP_KERNEL, "cx23885 IR (%s)",
				    cx23885_boards[dev->board].name);
	kernel_ir->phys = kasprintf(GFP_KERNEL, "pci-%s/ir0",
				    pci_name(dev->pci));

	/* input device */
	rc = rc_allocate_device();
	if (!rc) {
		ret = -ENOMEM;
		goto err_out_free;
	}

	kernel_ir->rc = rc;
	rc->input_name = kernel_ir->name;
	rc->input_phys = kernel_ir->phys;
	rc->input_id.bustype = BUS_PCI;
	rc->input_id.version = 1;
	if (dev->pci->subsystem_vendor) {
		rc->input_id.vendor  = dev->pci->subsystem_vendor;
		rc->input_id.product = dev->pci->subsystem_device;
	} else {
		rc->input_id.vendor  = dev->pci->vendor;
		rc->input_id.product = dev->pci->device;
	}
	rc->dev.parent = &dev->pci->dev;
	rc->driver_type = driver_type;
	rc->allowed_protos = allowed_protos;
	rc->priv = kernel_ir;
	rc->open = cx23885_input_ir_open;
	rc->close = cx23885_input_ir_close;
	rc->map_name = rc_map;
	rc->driver_name = MODULE_NAME;

	/* Go */
	dev->kernel_ir = kernel_ir;
	ret = rc_register_device(rc);
	if (ret)
		goto err_out_stop;

	return 0;

err_out_stop:
	cx23885_input_ir_stop(dev);
	dev->kernel_ir = NULL;
	rc_free_device(rc);
err_out_free:
	kfree(kernel_ir->phys);
	kfree(kernel_ir->name);
	kfree(kernel_ir);
	return ret;
}

void cx23885_input_fini(struct cx23885_dev *dev)
{
	/* Always stop the IR hardware from generating interrupts */
	cx23885_input_ir_stop(dev);

	if (dev->kernel_ir == NULL)
		return;
	rc_unregister_device(dev->kernel_ir->rc);
	kfree(dev->kernel_ir->phys);
	kfree(dev->kernel_ir->name);
	kfree(dev->kernel_ir);
	dev->kernel_ir = NULL;
}

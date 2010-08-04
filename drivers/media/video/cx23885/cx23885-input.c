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

#include <linux/input.h>
#include <linux/slab.h>
#include <media/ir-common.h>
#include <media/v4l2-subdev.h>

#include "cx23885.h"

#define RC5_BITS		14
#define RC5_HALF_BITS		(2*RC5_BITS)
#define RC5_HALF_BITS_MASK	((1 << RC5_HALF_BITS) - 1)

#define RC5_START_BITS_NORMAL	0x3 /* Command range  0 -  63 */
#define RC5_START_BITS_EXTENDED	0x2 /* Command range 64 - 127 */

#define RC5_EXTENDED_COMMAND_OFFSET	64

#define MODULE_NAME "cx23885"

static inline unsigned int rc5_command(u32 rc5_baseband)
{
	return RC5_INSTR(rc5_baseband) +
		((RC5_START(rc5_baseband) == RC5_START_BITS_EXTENDED)
			? RC5_EXTENDED_COMMAND_OFFSET : 0);
}

static void cx23885_input_process_raw_rc5(struct cx23885_dev *dev)
{
	struct card_ir *ir_input = dev->ir_input;
	unsigned int code, command;
	u32 rc5;

	/* Ignore codes that are too short to be valid RC-5 */
	if (ir_input->last_bit < (RC5_HALF_BITS - 1))
		return;

	/* The library has the manchester coding backwards; XOR to adapt. */
	code = (ir_input->code & RC5_HALF_BITS_MASK) ^ RC5_HALF_BITS_MASK;
	rc5 = ir_rc5_decode(code);

	switch (RC5_START(rc5)) {
	case RC5_START_BITS_NORMAL:
		break;
	case RC5_START_BITS_EXTENDED:
		/* Don't allow if the remote only emits standard commands */
		if (ir_input->start == RC5_START_BITS_NORMAL)
			return;
		break;
	default:
		return;
	}

	if (ir_input->addr != RC5_ADDR(rc5))
		return;

	/* Don't generate a keypress for RC-5 auto-repeated keypresses */
	command = rc5_command(rc5);
	if (RC5_TOGGLE(rc5) != RC5_TOGGLE(ir_input->last_rc5) ||
	    command != rc5_command(ir_input->last_rc5) ||
	    /* Catch T == 0, CMD == 0 (e.g. '0') as first keypress after init */
	    RC5_START(ir_input->last_rc5) == 0) {
		/* This keypress is differnet: not an auto repeat */
		ir_input_nokey(ir_input->dev, &ir_input->ir);
		ir_input_keydown(ir_input->dev, &ir_input->ir, command);
	}
	ir_input->last_rc5 = rc5;

	/* Schedule when we should do the key up event: ir_input_nokey() */
	mod_timer(&ir_input->timer_keyup,
		  jiffies + msecs_to_jiffies(ir_input->rc5_key_timeout));
}

static void cx23885_input_next_pulse_width_rc5(struct cx23885_dev *dev,
					       u32 ns_pulse)
{
	const int rc5_quarterbit_ns = 444444; /* 32 cycles/36 kHz/2 = 444 us */
	struct card_ir *ir_input = dev->ir_input;
	int i, level, quarterbits, halfbits;

	if (!ir_input->active) {
		ir_input->active = 1;
		/* assume an initial space that we may not detect or measure */
		ir_input->code = 0;
		ir_input->last_bit = 0;
	}

	if (ns_pulse == V4L2_SUBDEV_IR_PULSE_RX_SEQ_END) {
		ir_input->last_bit++; /* Account for the final space */
		ir_input->active = 0;
		cx23885_input_process_raw_rc5(dev);
		return;
	}

	level = (ns_pulse & V4L2_SUBDEV_IR_PULSE_LEVEL_MASK) ? 1 : 0;

	/* Skip any leading space to sync to the start bit */
	if (ir_input->last_bit == 0 && level == 0)
		return;

	/*
	 * With valid RC-5 we can get up to two consecutive half-bits in a
	 * single pulse measurment.  Experiments have shown that the duration
	 * of a half-bit can vary.  Make sure we always end up with an even
	 * number of quarter bits at the same level (mark or space).
	 */
	ns_pulse &= V4L2_SUBDEV_IR_PULSE_MAX_WIDTH_NS;
	quarterbits = ns_pulse / rc5_quarterbit_ns;
	if (quarterbits & 1)
		quarterbits++;
	halfbits = quarterbits / 2;

	for (i = 0; i < halfbits; i++) {
		ir_input->last_bit++;
		ir_input->code |= (level << ir_input->last_bit);

		if (ir_input->last_bit >= RC5_HALF_BITS-1) {
			ir_input->active = 0;
			cx23885_input_process_raw_rc5(dev);
			/*
			 * If level is 1, a leading mark is invalid for RC5.
			 * If level is 0, we scan past extra intial space.
			 * Either way we don't want to reactivate collecting
			 * marks or spaces here with any left over half-bits.
			 */
			break;
		}
	}
}

static void cx23885_input_process_pulse_widths_rc5(struct cx23885_dev *dev,
						   bool add_eom)
{
	struct card_ir *ir_input = dev->ir_input;
	struct ir_input_state *ir_input_state = &ir_input->ir;

	u32 ns_pulse[RC5_HALF_BITS+1];
	ssize_t num = 0;
	int count, i;

	do {
		v4l2_subdev_call(dev->sd_ir, ir, rx_read, (u8 *) ns_pulse,
				 sizeof(ns_pulse), &num);

		count = num / sizeof(u32);

		/* Append an end of Rx seq, if the caller requested */
		if (add_eom && count < ARRAY_SIZE(ns_pulse)) {
			ns_pulse[count] = V4L2_SUBDEV_IR_PULSE_RX_SEQ_END;
			count++;
		}

		/* Just drain the Rx FIFO, if we're called, but not RC-5 */
		if (ir_input_state->ir_type != IR_TYPE_RC5)
			continue;

		for (i = 0; i < count; i++)
			cx23885_input_next_pulse_width_rc5(dev, ns_pulse[i]);
	} while (num != 0);
}

void cx23885_input_rx_work_handler(struct cx23885_dev *dev, u32 events)
{
	struct v4l2_subdev_ir_parameters params;
	int overrun, data_available;

	if (dev->sd_ir == NULL || events == 0)
		return;

	switch (dev->board) {
	case CX23885_BOARD_HAUPPAUGE_HVR1850:
	case CX23885_BOARD_HAUPPAUGE_HVR1290:
		/*
		 * The only board we handle right now.  However other boards
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
		cx23885_input_process_pulse_widths_rc5(dev, overrun);

	if (overrun) {
		/* If there was a FIFO overrun, clear & restart the device */
		params.enable = true;
		/* Mitigate race with cx23885_input_ir_stop() */
		params.shutdown = atomic_read(&dev->ir_input_stopping);
		v4l2_subdev_call(dev->sd_ir, ir, rx_s_parameters, &params);
	}
}

static void cx23885_input_ir_start(struct cx23885_dev *dev)
{
	struct card_ir *ir_input = dev->ir_input;
	struct ir_input_state *ir_input_state = &ir_input->ir;
	struct v4l2_subdev_ir_parameters params;

	if (dev->sd_ir == NULL)
		return;

	atomic_set(&dev->ir_input_stopping, 0);

	/* keyup timer set up, if needed */
	switch (dev->board) {
	case CX23885_BOARD_HAUPPAUGE_HVR1850:
	case CX23885_BOARD_HAUPPAUGE_HVR1290:
		setup_timer(&ir_input->timer_keyup,
			    ir_rc5_timer_keyup,	/* Not actually RC-5 specific */
			    (unsigned long) ir_input);
		if (ir_input_state->ir_type == IR_TYPE_RC5) {
			/*
			 * RC-5 repeats a held key every
			 * 64 bits * (2 * 32/36000) sec/bit = 113.778 ms
			 */
			ir_input->rc5_key_timeout = 115;
		}
		break;
	}

	v4l2_subdev_call(dev->sd_ir, ir, rx_g_parameters, &params);
	switch (dev->board) {
	case CX23885_BOARD_HAUPPAUGE_HVR1850:
	case CX23885_BOARD_HAUPPAUGE_HVR1290:
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
		params.invert = true;
		break;
	}
	v4l2_subdev_call(dev->sd_ir, ir, rx_s_parameters, &params);
}

static void cx23885_input_ir_stop(struct cx23885_dev *dev)
{
	struct card_ir *ir_input = dev->ir_input;
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

	flush_scheduled_work();

	switch (dev->board) {
	case CX23885_BOARD_HAUPPAUGE_HVR1850:
	case CX23885_BOARD_HAUPPAUGE_HVR1290:
		del_timer_sync(&ir_input->timer_keyup);
		break;
	}
}

int cx23885_input_init(struct cx23885_dev *dev)
{
	struct card_ir *ir;
	struct input_dev *input_dev;
	char *ir_codes = NULL;
	int ir_type, ir_addr, ir_start;
	int ret;

	/*
	 * If the IR device (hardware registers, chip, GPIO lines, etc.) isn't
	 * encapsulated in a v4l2_subdev, then I'm not going to deal with it.
	 */
	if (dev->sd_ir == NULL)
		return -ENODEV;

	switch (dev->board) {
	case CX23885_BOARD_HAUPPAUGE_HVR1850:
	case CX23885_BOARD_HAUPPAUGE_HVR1290:
		/* Parameters for the grey Hauppauge remote for the HVR-1850 */
		ir_codes = RC_MAP_HAUPPAUGE_NEW;
		ir_type = IR_TYPE_RC5;
		ir_addr = 0x1e; /* RC-5 system bits emitted by the remote */
		ir_start = RC5_START_BITS_NORMAL; /* A basic RC-5 remote */
		break;
	}
	if (ir_codes == NULL)
		return -ENODEV;

	ir = kzalloc(sizeof(*ir), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!ir || !input_dev) {
		ret = -ENOMEM;
		goto err_out_free;
	}

	ir->dev = input_dev;
	ir->addr = ir_addr;
	ir->start = ir_start;

	/* init input device */
	snprintf(ir->name, sizeof(ir->name), "cx23885 IR (%s)",
		 cx23885_boards[dev->board].name);
	snprintf(ir->phys, sizeof(ir->phys), "pci-%s/ir0", pci_name(dev->pci));

	ret = ir_input_init(input_dev, &ir->ir, ir_type);
	if (ret < 0)
		goto err_out_free;

	input_dev->name = ir->name;
	input_dev->phys = ir->phys;
	input_dev->id.bustype = BUS_PCI;
	input_dev->id.version = 1;
	if (dev->pci->subsystem_vendor) {
		input_dev->id.vendor  = dev->pci->subsystem_vendor;
		input_dev->id.product = dev->pci->subsystem_device;
	} else {
		input_dev->id.vendor  = dev->pci->vendor;
		input_dev->id.product = dev->pci->device;
	}
	input_dev->dev.parent = &dev->pci->dev;

	dev->ir_input = ir;
	cx23885_input_ir_start(dev);

	ret = ir_input_register(ir->dev, ir_codes, NULL, MODULE_NAME);
	if (ret)
		goto err_out_stop;

	return 0;

err_out_stop:
	cx23885_input_ir_stop(dev);
	dev->ir_input = NULL;
err_out_free:
	kfree(ir);
	return ret;
}

void cx23885_input_fini(struct cx23885_dev *dev)
{
	/* Always stop the IR hardware from generating interrupts */
	cx23885_input_ir_stop(dev);

	if (dev->ir_input == NULL)
		return;
	ir_input_unregister(dev->ir_input->dev);
	kfree(dev->ir_input);
	dev->ir_input = NULL;
}

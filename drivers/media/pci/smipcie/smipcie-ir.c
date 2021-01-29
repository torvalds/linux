// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * SMI PCIe driver for DVBSky cards.
 *
 * Copyright (C) 2014 Max nibble <nibble.max@gmail.com>
 */

#include "smipcie.h"

#define SMI_SAMPLE_PERIOD 83
#define SMI_SAMPLE_IDLEMIN (10000 / SMI_SAMPLE_PERIOD)

static void smi_ir_enableInterrupt(struct smi_rc *ir)
{
	struct smi_dev *dev = ir->dev;

	smi_write(MSI_INT_ENA_SET, IR_X_INT);
}

static void smi_ir_disableInterrupt(struct smi_rc *ir)
{
	struct smi_dev *dev = ir->dev;

	smi_write(MSI_INT_ENA_CLR, IR_X_INT);
}

static void smi_ir_clearInterrupt(struct smi_rc *ir)
{
	struct smi_dev *dev = ir->dev;

	smi_write(MSI_INT_STATUS_CLR, IR_X_INT);
}

static void smi_ir_stop(struct smi_rc *ir)
{
	struct smi_dev *dev = ir->dev;

	smi_ir_disableInterrupt(ir);
	smi_clear(IR_Init_Reg, rbIRen);
}

static void smi_raw_process(struct rc_dev *rc_dev, const u8 *buffer,
			    const u8 length)
{
	struct ir_raw_event rawir = {};
	int cnt;

	for (cnt = 0; cnt < length; cnt++) {
		if (buffer[cnt] & 0x7f) {
			rawir.pulse = (buffer[cnt] & 0x80) == 0;
			rawir.duration = ((buffer[cnt] & 0x7f) +
					 (rawir.pulse ? 0 : -1)) *
					 rc_dev->rx_resolution;
			ir_raw_event_store_with_filter(rc_dev, &rawir);
		}
	}
}

static void smi_ir_decode(struct smi_rc *ir)
{
	struct smi_dev *dev = ir->dev;
	struct rc_dev *rc_dev = ir->rc_dev;
	u32 control, data;
	u8 index, ir_count, read_loop;

	control = smi_read(IR_Init_Reg);

	dev_dbg(&rc_dev->dev, "ircontrol: 0x%08x\n", control);

	if (control & rbIRVld) {
		ir_count = (u8)smi_read(IR_Data_Cnt);

		dev_dbg(&rc_dev->dev, "ircount %d\n", ir_count);

		read_loop = ir_count / 4;
		if (ir_count % 4)
			read_loop += 1;
		for (index = 0; index < read_loop; index++) {
			data = smi_read(IR_DATA_BUFFER_BASE + (index * 4));
			dev_dbg(&rc_dev->dev, "IRData 0x%08x\n", data);

			ir->irData[index * 4 + 0] = (u8)(data);
			ir->irData[index * 4 + 1] = (u8)(data >> 8);
			ir->irData[index * 4 + 2] = (u8)(data >> 16);
			ir->irData[index * 4 + 3] = (u8)(data >> 24);
		}
		smi_raw_process(rc_dev, ir->irData, ir_count);
	}

	if (control & rbIRhighidle) {
		struct ir_raw_event rawir = {};

		dev_dbg(&rc_dev->dev, "high idle\n");

		rawir.pulse = 0;
		rawir.duration = SMI_SAMPLE_PERIOD * SMI_SAMPLE_IDLEMIN;
		ir_raw_event_store_with_filter(rc_dev, &rawir);
	}

	smi_set(IR_Init_Reg, rbIRVld);
	ir_raw_event_handle(rc_dev);
}

/* ir functions call by main driver.*/
int smi_ir_irq(struct smi_rc *ir, u32 int_status)
{
	int handled = 0;

	if (int_status & IR_X_INT) {
		smi_ir_disableInterrupt(ir);
		smi_ir_clearInterrupt(ir);
		smi_ir_decode(ir);
		smi_ir_enableInterrupt(ir);
		handled = 1;
	}
	return handled;
}

void smi_ir_start(struct smi_rc *ir)
{
	struct smi_dev *dev = ir->dev;

	smi_write(IR_Idle_Cnt_Low,
		  (((SMI_SAMPLE_PERIOD - 1) & 0xFFFF) << 16) |
		  (SMI_SAMPLE_IDLEMIN & 0xFFFF));
	msleep(20);
	smi_set(IR_Init_Reg, rbIRen | rbIRhighidle);

	smi_ir_enableInterrupt(ir);
}

int smi_ir_init(struct smi_dev *dev)
{
	int ret;
	struct rc_dev *rc_dev;
	struct smi_rc *ir = &dev->ir;

	rc_dev = rc_allocate_device(RC_DRIVER_IR_RAW);
	if (!rc_dev)
		return -ENOMEM;

	/* init input device */
	snprintf(ir->device_name, sizeof(ir->device_name), "IR (%s)",
		 dev->info->name);
	snprintf(ir->input_phys, sizeof(ir->input_phys), "pci-%s/ir0",
		 pci_name(dev->pci_dev));

	rc_dev->allowed_protocols = RC_PROTO_BIT_ALL_IR_DECODER;
	rc_dev->driver_name = "SMI_PCIe";
	rc_dev->input_phys = ir->input_phys;
	rc_dev->device_name = ir->device_name;
	rc_dev->input_id.bustype = BUS_PCI;
	rc_dev->input_id.version = 1;
	rc_dev->input_id.vendor = dev->pci_dev->subsystem_vendor;
	rc_dev->input_id.product = dev->pci_dev->subsystem_device;
	rc_dev->dev.parent = &dev->pci_dev->dev;

	rc_dev->map_name = dev->info->rc_map;
	rc_dev->timeout = SMI_SAMPLE_PERIOD * SMI_SAMPLE_IDLEMIN;
	rc_dev->rx_resolution = SMI_SAMPLE_PERIOD;

	ir->rc_dev = rc_dev;
	ir->dev = dev;

	smi_ir_disableInterrupt(ir);

	ret = rc_register_device(rc_dev);
	if (ret)
		goto ir_err;

	return 0;
ir_err:
	rc_free_device(rc_dev);
	return ret;
}

void smi_ir_exit(struct smi_dev *dev)
{
	struct smi_rc *ir = &dev->ir;
	struct rc_dev *rc_dev = ir->rc_dev;

	rc_unregister_device(rc_dev);
	smi_ir_stop(ir);
	ir->rc_dev = NULL;
}

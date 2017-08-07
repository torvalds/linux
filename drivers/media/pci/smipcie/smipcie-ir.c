/*
 * SMI PCIe driver for DVBSky cards.
 *
 * Copyright (C) 2014 Max nibble <nibble.max@gmail.com>
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 */

#include "smipcie.h"

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
	smi_clear(IR_Init_Reg, 0x80);
}

#define BITS_PER_COMMAND 14
#define GROUPS_PER_BIT 2
#define IR_RC5_MIN_BIT 36
#define IR_RC5_MAX_BIT 52
static u32 smi_decode_rc5(u8 *pData, u8 size)
{
	u8 index, current_bit, bit_count;
	u8 group_array[BITS_PER_COMMAND * GROUPS_PER_BIT + 4];
	u8 group_index = 0;
	u32 command = 0xFFFFFFFF;

	group_array[group_index++] = 1;

	for (index = 0; index < size; index++) {

		current_bit = (pData[index] & 0x80) ? 1 : 0;
		bit_count = pData[index] & 0x7f;

		if ((current_bit == 1) && (bit_count >= 2*IR_RC5_MAX_BIT + 1)) {
			goto process_code;
		} else if ((bit_count >= IR_RC5_MIN_BIT) &&
			   (bit_count <= IR_RC5_MAX_BIT)) {
				group_array[group_index++] = current_bit;
		} else if ((bit_count > IR_RC5_MAX_BIT) &&
			   (bit_count <= 2*IR_RC5_MAX_BIT)) {
				group_array[group_index++] = current_bit;
				group_array[group_index++] = current_bit;
		} else {
			goto invalid_timing;
		}
		if (group_index >= BITS_PER_COMMAND*GROUPS_PER_BIT)
			goto process_code;

		if ((group_index == BITS_PER_COMMAND*GROUPS_PER_BIT - 1)
		    && (group_array[group_index-1] == 0)) {
			group_array[group_index++] = 1;
			goto process_code;
		}
	}

process_code:
	if (group_index == (BITS_PER_COMMAND*GROUPS_PER_BIT-1))
		group_array[group_index++] = 1;

	if (group_index == BITS_PER_COMMAND*GROUPS_PER_BIT) {
		command = 0;
		for (index = 0; index < (BITS_PER_COMMAND*GROUPS_PER_BIT);
		     index = index + 2) {
			if ((group_array[index] == 1) &&
			    (group_array[index+1] == 0)) {
				command |= (1 << (BITS_PER_COMMAND -
						   (index/2) - 1));
			} else if ((group_array[index] == 0) &&
				   (group_array[index+1] == 1)) {
				/* */
			} else {
				command = 0xFFFFFFFF;
				goto invalid_timing;
			}
		}
	}

invalid_timing:
	return command;
}

static void smi_ir_decode(struct work_struct *work)
{
	struct smi_rc *ir = container_of(work, struct smi_rc, work);
	struct smi_dev *dev = ir->dev;
	struct rc_dev *rc_dev = ir->rc_dev;
	u32 dwIRControl, dwIRData, dwIRCode, scancode;
	u8 index, ucIRCount, readLoop, rc5_command, rc5_system, toggle;

	dwIRControl = smi_read(IR_Init_Reg);
	if (dwIRControl & rbIRVld) {
		ucIRCount = (u8) smi_read(IR_Data_Cnt);

		if (ucIRCount < 4)
			goto end_ir_decode;

		readLoop = ucIRCount/4;
		if (ucIRCount % 4)
			readLoop += 1;
		for (index = 0; index < readLoop; index++) {
			dwIRData = smi_read(IR_DATA_BUFFER_BASE + (index*4));

			ir->irData[index*4 + 0] = (u8)(dwIRData);
			ir->irData[index*4 + 1] = (u8)(dwIRData >> 8);
			ir->irData[index*4 + 2] = (u8)(dwIRData >> 16);
			ir->irData[index*4 + 3] = (u8)(dwIRData >> 24);
		}
		dwIRCode = smi_decode_rc5(ir->irData, ucIRCount);

		if (dwIRCode != 0xFFFFFFFF) {
			rc5_command = dwIRCode & 0x3F;
			rc5_system = (dwIRCode & 0x7C0) >> 6;
			toggle = (dwIRCode & 0x800) ? 1 : 0;
			scancode = rc5_system << 8 | rc5_command;
			rc_keydown(rc_dev, RC_PROTO_RC5, scancode, toggle);
		}
	}
end_ir_decode:
	smi_set(IR_Init_Reg, 0x04);
	smi_ir_enableInterrupt(ir);
}

/* ir functions call by main driver.*/
int smi_ir_irq(struct smi_rc *ir, u32 int_status)
{
	int handled = 0;

	if (int_status & IR_X_INT) {
		smi_ir_disableInterrupt(ir);
		smi_ir_clearInterrupt(ir);
		schedule_work(&ir->work);
		handled = 1;
	}
	return handled;
}

void smi_ir_start(struct smi_rc *ir)
{
	struct smi_dev *dev = ir->dev;

	smi_write(IR_Idle_Cnt_Low, 0x00140070);
	msleep(20);
	smi_set(IR_Init_Reg, 0x90);

	smi_ir_enableInterrupt(ir);
}

int smi_ir_init(struct smi_dev *dev)
{
	int ret;
	struct rc_dev *rc_dev;
	struct smi_rc *ir = &dev->ir;

	rc_dev = rc_allocate_device();
	if (!rc_dev)
		return -ENOMEM;

	/* init input device */
	snprintf(ir->device_name, sizeof(ir->device_name), "IR (%s)",
		 dev->info->name);
	snprintf(ir->input_phys, sizeof(ir->input_phys), "pci-%s/ir0",
		 pci_name(dev->pci_dev));

	rc_dev->driver_name = "SMI_PCIe";
	rc_dev->input_phys = ir->input_phys;
	rc_dev->device_name = ir->device_name;
	rc_dev->input_id.bustype = BUS_PCI;
	rc_dev->input_id.version = 1;
	rc_dev->input_id.vendor = dev->pci_dev->subsystem_vendor;
	rc_dev->input_id.product = dev->pci_dev->subsystem_device;
	rc_dev->dev.parent = &dev->pci_dev->dev;

	rc_dev->driver_type = RC_DRIVER_SCANCODE;
	rc_dev->map_name = RC_MAP_DVBSKY;

	ir->rc_dev = rc_dev;
	ir->dev = dev;

	INIT_WORK(&ir->work, smi_ir_decode);
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

	smi_ir_stop(ir);
	rc_unregister_device(rc_dev);
	ir->rc_dev = NULL;
}

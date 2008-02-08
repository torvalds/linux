/*
 * Firmware loader for ETRAX FS IO-Processor
 *
 * Copyright (C) 2004  Axis Communications AB
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/firmware.h>

#include <hwregs/reg_rdwr.h>
#include <hwregs/reg_map.h>
#include <hwregs/iop/iop_reg_space.h>
#include <hwregs/iop/iop_mpu_macros.h>
#include <hwregs/iop/iop_mpu_defs.h>
#include <hwregs/iop/iop_spu_defs.h>
#include <hwregs/iop/iop_sw_cpu_defs.h>

#define IOP_TIMEOUT 100

#error "This driver is broken with regard to its driver core usage."
#error "Please contact <greg@kroah.com> for details on how to fix it properly."

static struct device iop_spu_device[2] = {
	{ .bus_id =     "iop-spu0", },
	{ .bus_id =     "iop-spu1", },
};

static struct device iop_mpu_device = {
	.bus_id =       "iop-mpu",
};

static int wait_mpu_idle(void)
{
	reg_iop_mpu_r_stat mpu_stat;
	unsigned int timeout = IOP_TIMEOUT;

	do {
		mpu_stat = REG_RD(iop_mpu, regi_iop_mpu, r_stat);
	} while (mpu_stat.instr_reg_busy == regk_iop_mpu_yes && --timeout > 0);
	if (timeout == 0) {
		printk(KERN_ERR "Timeout waiting for MPU to be idle\n");
		return -EBUSY;
	}
	return 0;
}

int iop_fw_load_spu(const unsigned char *fw_name, unsigned int spu_inst)
{
	reg_iop_sw_cpu_rw_mc_ctrl mc_ctrl = {
		.wr_spu0_mem =    regk_iop_sw_cpu_no,
		.wr_spu1_mem =    regk_iop_sw_cpu_no,
		.size =           4,
		.cmd =            regk_iop_sw_cpu_reg_copy,
		.keep_owner =     regk_iop_sw_cpu_yes
	};
	reg_iop_spu_rw_ctrl spu_ctrl = {
		.en  =            regk_iop_spu_no,
		.fsm =            regk_iop_spu_no,
	};
	reg_iop_sw_cpu_r_mc_stat mc_stat;
        const struct firmware *fw_entry;
	u32 *data;
	unsigned int timeout;
	int retval, i;

	if (spu_inst > 1)
		return -ENODEV;

	/* get firmware */
	retval = request_firmware(&fw_entry,
				  fw_name,
				  &iop_spu_device[spu_inst]);
	if (retval != 0)
	{
		printk(KERN_ERR
		       "iop_load_spu: Failed to load firmware \"%s\"\n",
		       fw_name);
		return retval;
	}
	data = (u32 *) fw_entry->data;

	/* acquire ownership of memory controller */
	switch (spu_inst) {
	case 0:
		mc_ctrl.wr_spu0_mem = regk_iop_sw_cpu_yes;
		REG_WR(iop_spu, regi_iop_spu0, rw_ctrl, spu_ctrl);
		break;
	case 1:
		mc_ctrl.wr_spu1_mem = regk_iop_sw_cpu_yes;
		REG_WR(iop_spu, regi_iop_spu1, rw_ctrl, spu_ctrl);
		break;
	}
	timeout = IOP_TIMEOUT;
	do {
		REG_WR(iop_sw_cpu, regi_iop_sw_cpu, rw_mc_ctrl, mc_ctrl);
		mc_stat = REG_RD(iop_sw_cpu, regi_iop_sw_cpu, r_mc_stat);
	} while (mc_stat.owned_by_cpu == regk_iop_sw_cpu_no && --timeout > 0);
	if (timeout == 0) {
		printk(KERN_ERR "Timeout waiting to acquire MC\n");
		retval = -EBUSY;
		goto out;
	}

	/* write to SPU memory */
	for (i = 0; i < (fw_entry->size/4); i++) {
		switch (spu_inst) {
		case 0:
			REG_WR_INT(iop_spu, regi_iop_spu0, rw_seq_pc, (i*4));
			break;
		case 1:
			REG_WR_INT(iop_spu, regi_iop_spu1, rw_seq_pc, (i*4));
			break;
		}
		REG_WR_INT(iop_sw_cpu, regi_iop_sw_cpu, rw_mc_data, *data);
		data++;
	}

	/* release ownership of memory controller */
	(void) REG_RD(iop_sw_cpu, regi_iop_sw_cpu, rs_mc_data);

 out:
	release_firmware(fw_entry);
	return retval;
}

int iop_fw_load_mpu(unsigned char *fw_name)
{
	const unsigned int start_addr = 0;
	reg_iop_mpu_rw_ctrl mpu_ctrl;
        const struct firmware *fw_entry;
	u32 *data;
	int retval, i;

	/* get firmware */
	retval = request_firmware(&fw_entry, fw_name, &iop_mpu_device);
	if (retval != 0)
	{
		printk(KERN_ERR
		       "iop_load_spu: Failed to load firmware \"%s\"\n",
		       fw_name);
		return retval;
	}
	data = (u32 *) fw_entry->data;

	/* disable MPU */
	mpu_ctrl.en = regk_iop_mpu_no;
	REG_WR(iop_mpu, regi_iop_mpu, rw_ctrl, mpu_ctrl);
	/* put start address in R0 */
	REG_WR_VECT(iop_mpu, regi_iop_mpu, rw_r, 0, start_addr);
	/* write to memory by executing 'SWX i, 4, R0' for each word */
	if ((retval = wait_mpu_idle()) != 0)
		goto out;
	REG_WR(iop_mpu, regi_iop_mpu, rw_instr, MPU_SWX_IIR_INSTR(0, 4, 0));
	for (i = 0; i < (fw_entry->size / 4); i++) {
		REG_WR_INT(iop_mpu, regi_iop_mpu, rw_immediate, *data);
		if ((retval = wait_mpu_idle()) != 0)
			goto out;
		data++;
	}

 out:
	release_firmware(fw_entry);
	return retval;
}

int iop_start_mpu(unsigned int start_addr)
{
	reg_iop_mpu_rw_ctrl mpu_ctrl = { .en = regk_iop_mpu_yes };
	int retval;

	/* disable MPU */
	if ((retval = wait_mpu_idle()) != 0)
		goto out;
	REG_WR(iop_mpu, regi_iop_mpu, rw_instr, MPU_HALT());
	if ((retval = wait_mpu_idle()) != 0)
		goto out;
	/* set PC and wait for it to bite */
	if ((retval = wait_mpu_idle()) != 0)
		goto out;
	REG_WR_INT(iop_mpu, regi_iop_mpu, rw_instr, MPU_BA_I(start_addr));
	if ((retval = wait_mpu_idle()) != 0)
		goto out;
	/* make sure the MPU starts executing with interrupts disabled */
	REG_WR(iop_mpu, regi_iop_mpu, rw_instr, MPU_DI());
	if ((retval = wait_mpu_idle()) != 0)
		goto out;
	/* enable MPU */
	REG_WR(iop_mpu, regi_iop_mpu, rw_ctrl, mpu_ctrl);
 out:
	return retval;
}

static int __init iop_fw_load_init(void)
{
#if 0
	/*
	 * static struct devices can not be added directly to sysfs by ignoring
	 * the driver model infrastructure.  To fix this properly, please use
	 * the platform_bus to register these devices to be able to properly
	 * use the firmware infrastructure.
	 */
	device_initialize(&iop_spu_device[0]);
	kobject_set_name(&iop_spu_device[0].kobj, "iop-spu0");
	kobject_add(&iop_spu_device[0].kobj);
	device_initialize(&iop_spu_device[1]);
	kobject_set_name(&iop_spu_device[1].kobj, "iop-spu1");
	kobject_add(&iop_spu_device[1].kobj);
	device_initialize(&iop_mpu_device);
	kobject_set_name(&iop_mpu_device.kobj, "iop-mpu");
	kobject_add(&iop_mpu_device.kobj);
#endif
	return 0;
}

static void __exit iop_fw_load_exit(void)
{
}

module_init(iop_fw_load_init);
module_exit(iop_fw_load_exit);

MODULE_DESCRIPTION("ETRAX FS IO-Processor Firmware Loader");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL(iop_fw_load_spu);
EXPORT_SYMBOL(iop_fw_load_mpu);
EXPORT_SYMBOL(iop_start_mpu);

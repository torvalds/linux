/*
 * Debug register debug driver.
 *
 * Copyright (C) 2010-2013 Amlogic Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __AML_REG_DRV_H
#define __AML_REG_DRV_H

#include <asm/ioctl.h>

struct reg_info {
	unsigned int addr;
	unsigned int val;
	unsigned char mode;
};

struct bit_info {
	unsigned int addr;
	unsigned int val;
	unsigned int start;
	unsigned int len;
};

#define AML_REGS_IOC_MAGIC	'R'
#define IOC_RD_CBUS_REG		_IOR(AML_REGS_IOC_MAGIC, 0x01, struct reg_info)
#define IOC_WR_CUBS_REG		_IOW(AML_REGS_IOC_MAGIC, 0x02, struct reg_info)
#define IOC_RD_APB_REG		_IOR(AML_REGS_IOC_MAGIC, 0x03, struct reg_info)
#define IOC_WR_APB_REG		_IOW(AML_REGS_IOC_MAGIC, 0x04, struct reg_info)
#define IOC_RD_AXI_REG		_IOR(AML_REGS_IOC_MAGIC, 0x05, struct reg_info)
#define IOC_WR_AXI_REG		_IOW(AML_REGS_IOC_MAGIC, 0x06, struct reg_info)
#define IOC_RD_AHB_REG		_IOR(AML_REGS_IOC_MAGIC, 0x07, struct reg_info)
#define IOC_WR_AHB_REG		_IOW(AML_REGS_IOC_MAGIC, 0x08, struct reg_info)

#define IOC_RD_CBUS_BIT		_IOR(AML_REGS_IOC_MAGIC, 0x21, struct bit_info)
#define IOC_WR_CUBS_BIT		_IOW(AML_REGS_IOC_MAGIC, 0x22, struct bit_info)
#define IOC_RD_APB_BIT		_IOR(AML_REGS_IOC_MAGIC, 0x23, struct bit_info)
#define IOC_WR_APB_BIT		_IOW(AML_REGS_IOC_MAGIC, 0x24, struct bit_info)
#define IOC_RD_AXI_BIT		_IOR(AML_REGS_IOC_MAGIC, 0x25, struct bit_info)
#define IOC_WR_AXI_BIT		_IOW(AML_REGS_IOC_MAGIC, 0x26, struct bit_info)
#define IOC_RD_AHB_BIT		_IOR(AML_REGS_IOC_MAGIC, 0x27, struct bit_info)
#define IOC_WR_AHB_BIT		_IOW(AML_REGS_IOC_MAGIC, 0x28, struct bit_info)

#define IOC_WR_SGR_GAMMA	_IOW(AML_REGS_IOC_MAGIC, 0X15, int)
#define IOC_WR_SGG_GAMMA	_IOW(AML_REGS_IOC_MAGIC, 0X16, int)
#define IOC_WR_SGB_GAMMA	_IOW(AML_REGS_IOC_MAGIC, 0X17, int)


#define IOC_RD_AAPB_REG       _IOR(AML_REGS_IOC_MAGIC, 0x18, struct reg_info)
#define IOC_WR_AAPB_REG       _IOW(AML_REGS_IOC_MAGIC, 0x19, struct reg_info)
#endif /* __AML_REG_DRV_H */


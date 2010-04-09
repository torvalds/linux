/*
 * the IDE Virtual Support Module of AMD CS5536
 *
 * Copyright (C) 2007 Lemote, Inc.
 * Author : jlliu, liujl@lemote.com
 *
 * Copyright (C) 2009 Lemote, Inc.
 * Author: Wu Zhangjin, wuzhangjin@gmail.com
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <cs5536/cs5536.h>
#include <cs5536/cs5536_pci.h>

void pci_ide_write_reg(int reg, u32 value)
{
	u32 hi = 0, lo = value;

	switch (reg) {
	case PCI_COMMAND:
		_rdmsr(GLIU_MSR_REG(GLIU_PAE), &hi, &lo);
		if (value & PCI_COMMAND_MASTER)
			lo |= (0x03 << 4);
		else
			lo &= ~(0x03 << 4);
		_wrmsr(GLIU_MSR_REG(GLIU_PAE), hi, lo);
		break;
	case PCI_STATUS:
		if (value & PCI_STATUS_PARITY) {
			_rdmsr(SB_MSR_REG(SB_ERROR), &hi, &lo);
			if (lo & SB_PARE_ERR_FLAG) {
				lo = (lo & 0x0000ffff) | SB_PARE_ERR_FLAG;
				_wrmsr(SB_MSR_REG(SB_ERROR), hi, lo);
			}
		}
		break;
	case PCI_CACHE_LINE_SIZE:
		value &= 0x0000ff00;
		_rdmsr(SB_MSR_REG(SB_CTRL), &hi, &lo);
		hi &= 0xffffff00;
		hi |= (value >> 8);
		_wrmsr(SB_MSR_REG(SB_CTRL), hi, lo);
		break;
	case PCI_BAR4_REG:
		if (value == PCI_BAR_RANGE_MASK) {
			_rdmsr(GLCP_MSR_REG(GLCP_SOFT_COM), &hi, &lo);
			lo |= SOFT_BAR_IDE_FLAG;
			_wrmsr(GLCP_MSR_REG(GLCP_SOFT_COM), hi, lo);
		} else if (value & 0x01) {
			lo = (value & 0xfffffff0) | 0x1;
			_wrmsr(IDE_MSR_REG(IDE_IO_BAR), hi, lo);

			value &= 0xfffffffc;
			hi = 0x60000000 | ((value & 0x000ff000) >> 12);
			lo = 0x000ffff0 | ((value & 0x00000fff) << 20);
			_wrmsr(GLIU_MSR_REG(GLIU_IOD_BM2), hi, lo);
		}
		break;
	case PCI_IDE_CFG_REG:
		if (value == CS5536_IDE_FLASH_SIGNATURE) {
			_rdmsr(DIVIL_MSR_REG(DIVIL_BALL_OPTS), &hi, &lo);
			lo |= 0x01;
			_wrmsr(DIVIL_MSR_REG(DIVIL_BALL_OPTS), hi, lo);
		} else
			_wrmsr(IDE_MSR_REG(IDE_CFG), hi, lo);
		break;
	case PCI_IDE_DTC_REG:
		_wrmsr(IDE_MSR_REG(IDE_DTC), hi, lo);
		break;
	case PCI_IDE_CAST_REG:
		_wrmsr(IDE_MSR_REG(IDE_CAST), hi, lo);
		break;
	case PCI_IDE_ETC_REG:
		_wrmsr(IDE_MSR_REG(IDE_ETC), hi, lo);
		break;
	case PCI_IDE_PM_REG:
		_wrmsr(IDE_MSR_REG(IDE_INTERNAL_PM), hi, lo);
		break;
	default:
		break;
	}
}

u32 pci_ide_read_reg(int reg)
{
	u32 conf_data = 0;
	u32 hi, lo;

	switch (reg) {
	case PCI_VENDOR_ID:
		conf_data =
		    CFG_PCI_VENDOR_ID(CS5536_IDE_DEVICE_ID, CS5536_VENDOR_ID);
		break;
	case PCI_COMMAND:
		_rdmsr(IDE_MSR_REG(IDE_IO_BAR), &hi, &lo);
		if (lo & 0xfffffff0)
			conf_data |= PCI_COMMAND_IO;
		_rdmsr(GLIU_MSR_REG(GLIU_PAE), &hi, &lo);
		if ((lo & 0x30) == 0x30)
			conf_data |= PCI_COMMAND_MASTER;
		break;
	case PCI_STATUS:
		conf_data |= PCI_STATUS_66MHZ;
		conf_data |= PCI_STATUS_FAST_BACK;
		_rdmsr(SB_MSR_REG(SB_ERROR), &hi, &lo);
		if (lo & SB_PARE_ERR_FLAG)
			conf_data |= PCI_STATUS_PARITY;
		conf_data |= PCI_STATUS_DEVSEL_MEDIUM;
		break;
	case PCI_CLASS_REVISION:
		_rdmsr(IDE_MSR_REG(IDE_CAP), &hi, &lo);
		conf_data = lo & 0x000000ff;
		conf_data |= (CS5536_IDE_CLASS_CODE << 8);
		break;
	case PCI_CACHE_LINE_SIZE:
		_rdmsr(SB_MSR_REG(SB_CTRL), &hi, &lo);
		hi &= 0x000000f8;
		conf_data = CFG_PCI_CACHE_LINE_SIZE(PCI_NORMAL_HEADER_TYPE, hi);
		break;
	case PCI_BAR4_REG:
		_rdmsr(GLCP_MSR_REG(GLCP_SOFT_COM), &hi, &lo);
		if (lo & SOFT_BAR_IDE_FLAG) {
			conf_data = CS5536_IDE_RANGE |
			    PCI_BASE_ADDRESS_SPACE_IO;
			lo &= ~SOFT_BAR_IDE_FLAG;
			_wrmsr(GLCP_MSR_REG(GLCP_SOFT_COM), hi, lo);
		} else {
			_rdmsr(IDE_MSR_REG(IDE_IO_BAR), &hi, &lo);
			conf_data = lo & 0xfffffff0;
			conf_data |= 0x01;
			conf_data &= ~0x02;
		}
		break;
	case PCI_CARDBUS_CIS:
		conf_data = PCI_CARDBUS_CIS_POINTER;
		break;
	case PCI_SUBSYSTEM_VENDOR_ID:
		conf_data =
		    CFG_PCI_VENDOR_ID(CS5536_IDE_SUB_ID, CS5536_SUB_VENDOR_ID);
		break;
	case PCI_ROM_ADDRESS:
		conf_data = PCI_EXPANSION_ROM_BAR;
		break;
	case PCI_CAPABILITY_LIST:
		conf_data = PCI_CAPLIST_POINTER;
		break;
	case PCI_INTERRUPT_LINE:
		conf_data =
		    CFG_PCI_INTERRUPT_LINE(PCI_DEFAULT_PIN, CS5536_IDE_INTR);
		break;
	case PCI_IDE_CFG_REG:
		_rdmsr(IDE_MSR_REG(IDE_CFG), &hi, &lo);
		conf_data = lo;
		break;
	case PCI_IDE_DTC_REG:
		_rdmsr(IDE_MSR_REG(IDE_DTC), &hi, &lo);
		conf_data = lo;
		break;
	case PCI_IDE_CAST_REG:
		_rdmsr(IDE_MSR_REG(IDE_CAST), &hi, &lo);
		conf_data = lo;
		break;
	case PCI_IDE_ETC_REG:
		_rdmsr(IDE_MSR_REG(IDE_ETC), &hi, &lo);
		conf_data = lo;
	case PCI_IDE_PM_REG:
		_rdmsr(IDE_MSR_REG(IDE_INTERNAL_PM), &hi, &lo);
		conf_data = lo;
		break;
	default:
		break;
	}

	return conf_data;
}

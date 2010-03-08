/*
 * the ACC Virtual Support Module of AMD CS5536
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

void pci_acc_write_reg(int reg, u32 value)
{
	u32 hi = 0, lo = value;

	switch (reg) {
	case PCI_COMMAND:
		_rdmsr(GLIU_MSR_REG(GLIU_PAE), &hi, &lo);
		if (value & PCI_COMMAND_MASTER)
			lo |= (0x03 << 8);
		else
			lo &= ~(0x03 << 8);
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
	case PCI_BAR0_REG:
		if (value == PCI_BAR_RANGE_MASK) {
			_rdmsr(GLCP_MSR_REG(GLCP_SOFT_COM), &hi, &lo);
			lo |= SOFT_BAR_ACC_FLAG;
			_wrmsr(GLCP_MSR_REG(GLCP_SOFT_COM), hi, lo);
		} else if (value & 0x01) {
			value &= 0xfffffffc;
			hi = 0xA0000000 | ((value & 0x000ff000) >> 12);
			lo = 0x000fff80 | ((value & 0x00000fff) << 20);
			_wrmsr(GLIU_MSR_REG(GLIU_IOD_BM1), hi, lo);
		}
		break;
	case PCI_ACC_INT_REG:
		_rdmsr(DIVIL_MSR_REG(PIC_YSEL_LOW), &hi, &lo);
		/* disable all the usb interrupt in PIC */
		lo &= ~(0xf << PIC_YSEL_LOW_ACC_SHIFT);
		if (value)	/* enable all the acc interrupt in PIC */
			lo |= (CS5536_ACC_INTR << PIC_YSEL_LOW_ACC_SHIFT);
		_wrmsr(DIVIL_MSR_REG(PIC_YSEL_LOW), hi, lo);
		break;
	default:
		break;
	}
}

u32 pci_acc_read_reg(int reg)
{
	u32 hi, lo;
	u32 conf_data = 0;

	switch (reg) {
	case PCI_VENDOR_ID:
		conf_data =
		    CFG_PCI_VENDOR_ID(CS5536_ACC_DEVICE_ID, CS5536_VENDOR_ID);
		break;
	case PCI_COMMAND:
		_rdmsr(GLIU_MSR_REG(GLIU_IOD_BM1), &hi, &lo);
		if (((lo & 0xfff00000) || (hi & 0x000000ff))
		    && ((hi & 0xf0000000) == 0xa0000000))
			conf_data |= PCI_COMMAND_IO;
		_rdmsr(GLIU_MSR_REG(GLIU_PAE), &hi, &lo);
		if ((lo & 0x300) == 0x300)
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
		_rdmsr(ACC_MSR_REG(ACC_CAP), &hi, &lo);
		conf_data = lo & 0x000000ff;
		conf_data |= (CS5536_ACC_CLASS_CODE << 8);
		break;
	case PCI_CACHE_LINE_SIZE:
		conf_data =
		    CFG_PCI_CACHE_LINE_SIZE(PCI_NORMAL_HEADER_TYPE,
					    PCI_NORMAL_LATENCY_TIMER);
		break;
	case PCI_BAR0_REG:
		_rdmsr(GLCP_MSR_REG(GLCP_SOFT_COM), &hi, &lo);
		if (lo & SOFT_BAR_ACC_FLAG) {
			conf_data = CS5536_ACC_RANGE |
			    PCI_BASE_ADDRESS_SPACE_IO;
			lo &= ~SOFT_BAR_ACC_FLAG;
			_wrmsr(GLCP_MSR_REG(GLCP_SOFT_COM), hi, lo);
		} else {
			_rdmsr(GLIU_MSR_REG(GLIU_IOD_BM1), &hi, &lo);
			conf_data = (hi & 0x000000ff) << 12;
			conf_data |= (lo & 0xfff00000) >> 20;
			conf_data |= 0x01;
			conf_data &= ~0x02;
		}
		break;
	case PCI_CARDBUS_CIS:
		conf_data = PCI_CARDBUS_CIS_POINTER;
		break;
	case PCI_SUBSYSTEM_VENDOR_ID:
		conf_data =
		    CFG_PCI_VENDOR_ID(CS5536_ACC_SUB_ID, CS5536_SUB_VENDOR_ID);
		break;
	case PCI_ROM_ADDRESS:
		conf_data = PCI_EXPANSION_ROM_BAR;
		break;
	case PCI_CAPABILITY_LIST:
		conf_data = PCI_CAPLIST_USB_POINTER;
		break;
	case PCI_INTERRUPT_LINE:
		conf_data =
		    CFG_PCI_INTERRUPT_LINE(PCI_DEFAULT_PIN, CS5536_ACC_INTR);
		break;
	default:
		break;
	}

	return conf_data;
}

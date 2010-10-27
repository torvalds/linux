/*
 * the EHCI Virtual Support Module of AMD CS5536
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

void pci_ehci_write_reg(int reg, u32 value)
{
	u32 hi = 0, lo = value;

	switch (reg) {
	case PCI_COMMAND:
		_rdmsr(USB_MSR_REG(USB_EHCI), &hi, &lo);
		if (value & PCI_COMMAND_MASTER)
			hi |= PCI_COMMAND_MASTER;
		else
			hi &= ~PCI_COMMAND_MASTER;

		if (value & PCI_COMMAND_MEMORY)
			hi |= PCI_COMMAND_MEMORY;
		else
			hi &= ~PCI_COMMAND_MEMORY;
		_wrmsr(USB_MSR_REG(USB_EHCI), hi, lo);
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
			lo |= SOFT_BAR_EHCI_FLAG;
			_wrmsr(GLCP_MSR_REG(GLCP_SOFT_COM), hi, lo);
		} else if ((value & 0x01) == 0x00) {
			_rdmsr(USB_MSR_REG(USB_EHCI), &hi, &lo);
			lo = value;
			_wrmsr(USB_MSR_REG(USB_EHCI), hi, lo);

			value &= 0xfffffff0;
			hi = 0x40000000 | ((value & 0xff000000) >> 24);
			lo = 0x000fffff | ((value & 0x00fff000) << 8);
			_wrmsr(GLIU_MSR_REG(GLIU_P2D_BM4), hi, lo);
		}
		break;
	case PCI_EHCI_LEGSMIEN_REG:
		_rdmsr(USB_MSR_REG(USB_EHCI), &hi, &lo);
		hi &= 0x003f0000;
		hi |= (value & 0x3f) << 16;
		_wrmsr(USB_MSR_REG(USB_EHCI), hi, lo);
		break;
	case PCI_EHCI_FLADJ_REG:
		_rdmsr(USB_MSR_REG(USB_EHCI), &hi, &lo);
		hi &= ~0x00003f00;
		hi |= value & 0x00003f00;
		_wrmsr(USB_MSR_REG(USB_EHCI), hi, lo);
		break;
	default:
		break;
	}
}

u32 pci_ehci_read_reg(int reg)
{
	u32 conf_data = 0;
	u32 hi, lo;

	switch (reg) {
	case PCI_VENDOR_ID:
		conf_data =
		    CFG_PCI_VENDOR_ID(CS5536_EHCI_DEVICE_ID, CS5536_VENDOR_ID);
		break;
	case PCI_COMMAND:
		_rdmsr(USB_MSR_REG(USB_EHCI), &hi, &lo);
		if (hi & PCI_COMMAND_MASTER)
			conf_data |= PCI_COMMAND_MASTER;
		if (hi & PCI_COMMAND_MEMORY)
			conf_data |= PCI_COMMAND_MEMORY;
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
		_rdmsr(USB_MSR_REG(USB_CAP), &hi, &lo);
		conf_data = lo & 0x000000ff;
		conf_data |= (CS5536_EHCI_CLASS_CODE << 8);
		break;
	case PCI_CACHE_LINE_SIZE:
		conf_data =
		    CFG_PCI_CACHE_LINE_SIZE(PCI_NORMAL_HEADER_TYPE,
					    PCI_NORMAL_LATENCY_TIMER);
		break;
	case PCI_BAR0_REG:
		_rdmsr(GLCP_MSR_REG(GLCP_SOFT_COM), &hi, &lo);
		if (lo & SOFT_BAR_EHCI_FLAG) {
			conf_data = CS5536_EHCI_RANGE |
			    PCI_BASE_ADDRESS_SPACE_MEMORY;
			lo &= ~SOFT_BAR_EHCI_FLAG;
			_wrmsr(GLCP_MSR_REG(GLCP_SOFT_COM), hi, lo);
		} else {
			_rdmsr(USB_MSR_REG(USB_EHCI), &hi, &lo);
			conf_data = lo & 0xfffff000;
		}
		break;
	case PCI_CARDBUS_CIS:
		conf_data = PCI_CARDBUS_CIS_POINTER;
		break;
	case PCI_SUBSYSTEM_VENDOR_ID:
		conf_data =
		    CFG_PCI_VENDOR_ID(CS5536_EHCI_SUB_ID, CS5536_SUB_VENDOR_ID);
		break;
	case PCI_ROM_ADDRESS:
		conf_data = PCI_EXPANSION_ROM_BAR;
		break;
	case PCI_CAPABILITY_LIST:
		conf_data = PCI_CAPLIST_USB_POINTER;
		break;
	case PCI_INTERRUPT_LINE:
		conf_data =
		    CFG_PCI_INTERRUPT_LINE(PCI_DEFAULT_PIN, CS5536_USB_INTR);
		break;
	case PCI_EHCI_LEGSMIEN_REG:
		_rdmsr(USB_MSR_REG(USB_EHCI), &hi, &lo);
		conf_data = (hi & 0x003f0000) >> 16;
		break;
	case PCI_EHCI_LEGSMISTS_REG:
		_rdmsr(USB_MSR_REG(USB_EHCI), &hi, &lo);
		conf_data = (hi & 0x3f000000) >> 24;
		break;
	case PCI_EHCI_FLADJ_REG:
		_rdmsr(USB_MSR_REG(USB_EHCI), &hi, &lo);
		conf_data = hi & 0x00003f00;
		break;
	default:
		break;
	}

	return conf_data;
}

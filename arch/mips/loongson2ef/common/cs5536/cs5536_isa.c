// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * the ISA Virtual Support Module of AMD CS5536
 *
 * Copyright (C) 2007 Lemote, Inc.
 * Author : jlliu, liujl@lemote.com
 *
 * Copyright (C) 2009 Lemote, Inc.
 * Author: Wu Zhangjin, wuzhangjin@gmail.com
 */

#include <linux/pci.h>
#include <cs5536/cs5536.h>
#include <cs5536/cs5536_pci.h>

/* common variables for PCI_ISA_READ/WRITE_BAR */
static const u32 divil_msr_reg[6] = {
	DIVIL_MSR_REG(DIVIL_LBAR_SMB), DIVIL_MSR_REG(DIVIL_LBAR_GPIO),
	DIVIL_MSR_REG(DIVIL_LBAR_MFGPT), DIVIL_MSR_REG(DIVIL_LBAR_IRQ),
	DIVIL_MSR_REG(DIVIL_LBAR_PMS), DIVIL_MSR_REG(DIVIL_LBAR_ACPI),
};

static const u32 soft_bar_flag[6] = {
	SOFT_BAR_SMB_FLAG, SOFT_BAR_GPIO_FLAG, SOFT_BAR_MFGPT_FLAG,
	SOFT_BAR_IRQ_FLAG, SOFT_BAR_PMS_FLAG, SOFT_BAR_ACPI_FLAG,
};

static const u32 sb_msr_reg[6] = {
	SB_MSR_REG(SB_R0), SB_MSR_REG(SB_R1), SB_MSR_REG(SB_R2),
	SB_MSR_REG(SB_R3), SB_MSR_REG(SB_R4), SB_MSR_REG(SB_R5),
};

static const u32 bar_space_range[6] = {
	CS5536_SMB_RANGE, CS5536_GPIO_RANGE, CS5536_MFGPT_RANGE,
	CS5536_IRQ_RANGE, CS5536_PMS_RANGE, CS5536_ACPI_RANGE,
};

static const int bar_space_len[6] = {
	CS5536_SMB_LENGTH, CS5536_GPIO_LENGTH, CS5536_MFGPT_LENGTH,
	CS5536_IRQ_LENGTH, CS5536_PMS_LENGTH, CS5536_ACPI_LENGTH,
};

/*
 * enable the divil module bar space.
 *
 * For all the DIVIL module LBAR, you should control the DIVIL LBAR reg
 * and the RCONFx(0~5) reg to use the modules.
 */
static void divil_lbar_enable(void)
{
	u32 hi, lo;
	int offset;

	/*
	 * The DIVIL IRQ is not used yet. and make the RCONF0 reserved.
	 */

	for (offset = DIVIL_LBAR_SMB; offset <= DIVIL_LBAR_PMS; offset++) {
		_rdmsr(DIVIL_MSR_REG(offset), &hi, &lo);
		hi |= 0x01;
		_wrmsr(DIVIL_MSR_REG(offset), hi, lo);
	}
}

/*
 * disable the divil module bar space.
 */
static void divil_lbar_disable(void)
{
	u32 hi, lo;
	int offset;

	for (offset = DIVIL_LBAR_SMB; offset <= DIVIL_LBAR_PMS; offset++) {
		_rdmsr(DIVIL_MSR_REG(offset), &hi, &lo);
		hi &= ~0x01;
		_wrmsr(DIVIL_MSR_REG(offset), hi, lo);
	}
}

/*
 * BAR write: write value to the n BAR
 */

void pci_isa_write_bar(int n, u32 value)
{
	u32 hi = 0, lo = value;

	if (value == PCI_BAR_RANGE_MASK) {
		_rdmsr(GLCP_MSR_REG(GLCP_SOFT_COM), &hi, &lo);
		lo |= soft_bar_flag[n];
		_wrmsr(GLCP_MSR_REG(GLCP_SOFT_COM), hi, lo);
	} else if (value & 0x01) {
		/* NATIVE reg */
		hi = 0x0000f001;
		lo &= bar_space_range[n];
		_wrmsr(divil_msr_reg[n], hi, lo);

		/* RCONFx is 4bytes in units for I/O space */
		hi = ((value & 0x000ffffc) << 12) |
		    ((bar_space_len[n] - 4) << 12) | 0x01;
		lo = ((value & 0x000ffffc) << 12) | 0x01;
		_wrmsr(sb_msr_reg[n], hi, lo);
	}
}

/*
 * BAR read: read the n BAR
 */

u32 pci_isa_read_bar(int n)
{
	u32 conf_data = 0;
	u32 hi, lo;

	_rdmsr(GLCP_MSR_REG(GLCP_SOFT_COM), &hi, &lo);
	if (lo & soft_bar_flag[n]) {
		conf_data = bar_space_range[n] | PCI_BASE_ADDRESS_SPACE_IO;
		lo &= ~soft_bar_flag[n];
		_wrmsr(GLCP_MSR_REG(GLCP_SOFT_COM), hi, lo);
	} else {
		_rdmsr(divil_msr_reg[n], &hi, &lo);
		conf_data = lo & bar_space_range[n];
		conf_data |= 0x01;
		conf_data &= ~0x02;
	}
	return conf_data;
}

/*
 * isa_write: ISA write transfer
 *
 * We assume that this is not a bus master transfer.
 */
void pci_isa_write_reg(int reg, u32 value)
{
	u32 hi = 0, lo = value;
	u32 temp;

	switch (reg) {
	case PCI_COMMAND:
		if (value & PCI_COMMAND_IO)
			divil_lbar_enable();
		else
			divil_lbar_disable();
		break;
	case PCI_STATUS:
		_rdmsr(SB_MSR_REG(SB_ERROR), &hi, &lo);
		temp = lo & 0x0000ffff;
		if ((value & PCI_STATUS_SIG_TARGET_ABORT) &&
		    (lo & SB_TAS_ERR_EN))
			temp |= SB_TAS_ERR_FLAG;

		if ((value & PCI_STATUS_REC_TARGET_ABORT) &&
		    (lo & SB_TAR_ERR_EN))
			temp |= SB_TAR_ERR_FLAG;

		if ((value & PCI_STATUS_REC_MASTER_ABORT)
		    && (lo & SB_MAR_ERR_EN))
			temp |= SB_MAR_ERR_FLAG;

		if ((value & PCI_STATUS_DETECTED_PARITY)
		    && (lo & SB_PARE_ERR_EN))
			temp |= SB_PARE_ERR_FLAG;

		lo = temp;
		_wrmsr(SB_MSR_REG(SB_ERROR), hi, lo);
		break;
	case PCI_CACHE_LINE_SIZE:
		value &= 0x0000ff00;
		_rdmsr(SB_MSR_REG(SB_CTRL), &hi, &lo);
		hi &= 0xffffff00;
		hi |= (value >> 8);
		_wrmsr(SB_MSR_REG(SB_CTRL), hi, lo);
		break;
	case PCI_BAR0_REG:
		pci_isa_write_bar(0, value);
		break;
	case PCI_BAR1_REG:
		pci_isa_write_bar(1, value);
		break;
	case PCI_BAR2_REG:
		pci_isa_write_bar(2, value);
		break;
	case PCI_BAR3_REG:
		pci_isa_write_bar(3, value);
		break;
	case PCI_BAR4_REG:
		pci_isa_write_bar(4, value);
		break;
	case PCI_BAR5_REG:
		pci_isa_write_bar(5, value);
		break;
	case PCI_UART1_INT_REG:
		_rdmsr(DIVIL_MSR_REG(PIC_YSEL_HIGH), &hi, &lo);
		/* disable uart1 interrupt in PIC */
		lo &= ~(0xf << 24);
		if (value)	/* enable uart1 interrupt in PIC */
			lo |= (CS5536_UART1_INTR << 24);
		_wrmsr(DIVIL_MSR_REG(PIC_YSEL_HIGH), hi, lo);
		break;
	case PCI_UART2_INT_REG:
		_rdmsr(DIVIL_MSR_REG(PIC_YSEL_HIGH), &hi, &lo);
		/* disable uart2 interrupt in PIC */
		lo &= ~(0xf << 28);
		if (value)	/* enable uart2 interrupt in PIC */
			lo |= (CS5536_UART2_INTR << 28);
		_wrmsr(DIVIL_MSR_REG(PIC_YSEL_HIGH), hi, lo);
		break;
	case PCI_ISA_FIXUP_REG:
		if (value) {
			/* enable the TARGET ABORT/MASTER ABORT etc. */
			_rdmsr(SB_MSR_REG(SB_ERROR), &hi, &lo);
			lo |= 0x00000063;
			_wrmsr(SB_MSR_REG(SB_ERROR), hi, lo);
		}
		break;
	default:
		/* ALL OTHER PCI CONFIG SPACE HEADER IS NOT IMPLEMENTED. */
		break;
	}
}

/*
 * isa_read: ISA read transfers
 *
 * We assume that this is not a bus master transfer.
 */
u32 pci_isa_read_reg(int reg)
{
	u32 conf_data = 0;
	u32 hi, lo;

	switch (reg) {
	case PCI_VENDOR_ID:
		conf_data =
		    CFG_PCI_VENDOR_ID(CS5536_ISA_DEVICE_ID, CS5536_VENDOR_ID);
		break;
	case PCI_COMMAND:
		/* we just check the first LBAR for the IO enable bit, */
		/* maybe we should changed later. */
		_rdmsr(DIVIL_MSR_REG(DIVIL_LBAR_SMB), &hi, &lo);
		if (hi & 0x01)
			conf_data |= PCI_COMMAND_IO;
		break;
	case PCI_STATUS:
		conf_data |= PCI_STATUS_66MHZ;
		conf_data |= PCI_STATUS_DEVSEL_MEDIUM;
		conf_data |= PCI_STATUS_FAST_BACK;

		_rdmsr(SB_MSR_REG(SB_ERROR), &hi, &lo);
		if (lo & SB_TAS_ERR_FLAG)
			conf_data |= PCI_STATUS_SIG_TARGET_ABORT;
		if (lo & SB_TAR_ERR_FLAG)
			conf_data |= PCI_STATUS_REC_TARGET_ABORT;
		if (lo & SB_MAR_ERR_FLAG)
			conf_data |= PCI_STATUS_REC_MASTER_ABORT;
		if (lo & SB_PARE_ERR_FLAG)
			conf_data |= PCI_STATUS_DETECTED_PARITY;
		break;
	case PCI_CLASS_REVISION:
		_rdmsr(GLCP_MSR_REG(GLCP_CHIP_REV_ID), &hi, &lo);
		conf_data = lo & 0x000000ff;
		conf_data |= (CS5536_ISA_CLASS_CODE << 8);
		break;
	case PCI_CACHE_LINE_SIZE:
		_rdmsr(SB_MSR_REG(SB_CTRL), &hi, &lo);
		hi &= 0x000000f8;
		conf_data = CFG_PCI_CACHE_LINE_SIZE(PCI_BRIDGE_HEADER_TYPE, hi);
		break;
		/*
		 * we only use the LBAR of DIVIL, no RCONF used.
		 * all of them are IO space.
		 */
	case PCI_BAR0_REG:
		return pci_isa_read_bar(0);
		break;
	case PCI_BAR1_REG:
		return pci_isa_read_bar(1);
		break;
	case PCI_BAR2_REG:
		return pci_isa_read_bar(2);
		break;
	case PCI_BAR3_REG:
		break;
	case PCI_BAR4_REG:
		return pci_isa_read_bar(4);
		break;
	case PCI_BAR5_REG:
		return pci_isa_read_bar(5);
		break;
	case PCI_CARDBUS_CIS:
		conf_data = PCI_CARDBUS_CIS_POINTER;
		break;
	case PCI_SUBSYSTEM_VENDOR_ID:
		conf_data =
		    CFG_PCI_VENDOR_ID(CS5536_ISA_SUB_ID, CS5536_SUB_VENDOR_ID);
		break;
	case PCI_ROM_ADDRESS:
		conf_data = PCI_EXPANSION_ROM_BAR;
		break;
	case PCI_CAPABILITY_LIST:
		conf_data = PCI_CAPLIST_POINTER;
		break;
	case PCI_INTERRUPT_LINE:
		/* no interrupt used here */
		conf_data = CFG_PCI_INTERRUPT_LINE(0x00, 0x00);
		break;
	default:
		break;
	}

	return conf_data;
}

/*
 * The mfgpt timer interrupt is running early, so we must keep the south bridge
 * mmio always enabled. Otherwise we may race with the PCI configuration which
 * may temporarily disable it. When that happens and the timer interrupt fires,
 * we are not able to clear it and the system will hang.
 */
static void cs5536_isa_mmio_always_on(struct pci_dev *dev)
{
	dev->mmio_always_on = 1;
}
DECLARE_PCI_FIXUP_CLASS_EARLY(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_CS5536_ISA,
	PCI_CLASS_BRIDGE_ISA, 8, cs5536_isa_mmio_always_on);

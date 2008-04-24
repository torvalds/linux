/*
 * SCC (Super Companion Chip) definitions
 *
 * (C) Copyright 2004-2006 TOSHIBA CORPORATION
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef _CELLEB_SCC_H
#define _CELLEB_SCC_H

#define PCI_VENDOR_ID_TOSHIBA_2                 0x102f
#define PCI_DEVICE_ID_TOSHIBA_SCC_PCIEXC_BRIDGE 0x01b0
#define PCI_DEVICE_ID_TOSHIBA_SCC_EPCI_BRIDGE   0x01b1
#define PCI_DEVICE_ID_TOSHIBA_SCC_BRIDGE        0x01b2
#define PCI_DEVICE_ID_TOSHIBA_SCC_GBE           0x01b3
#define PCI_DEVICE_ID_TOSHIBA_SCC_ATA           0x01b4
#define PCI_DEVICE_ID_TOSHIBA_SCC_USB2          0x01b5
#define PCI_DEVICE_ID_TOSHIBA_SCC_USB           0x01b6
#define PCI_DEVICE_ID_TOSHIBA_SCC_ENCDEC        0x01b7

#define SCC_EPCI_REG            0x0000d000

/* EPCI registers */
#define SCC_EPCI_CNF10_REG      0x010
#define SCC_EPCI_CNF14_REG      0x014
#define SCC_EPCI_CNF18_REG      0x018
#define SCC_EPCI_PVBAT          0x100
#define SCC_EPCI_VPMBAT         0x104
#define SCC_EPCI_VPIBAT         0x108
#define SCC_EPCI_VCSR           0x110
#define SCC_EPCI_VIENAB         0x114
#define SCC_EPCI_VISTAT         0x118
#define SCC_EPCI_VRDCOUNT       0x124
#define SCC_EPCI_BAM0           0x12c
#define SCC_EPCI_BAM1           0x134
#define SCC_EPCI_BAM2           0x13c
#define SCC_EPCI_IADR           0x164
#define SCC_EPCI_CLKRST         0x800
#define SCC_EPCI_INTSET         0x804
#define SCC_EPCI_STATUS         0x808
#define SCC_EPCI_ABTSET         0x80c
#define SCC_EPCI_WATRP          0x810
#define SCC_EPCI_DUMYRADR       0x814
#define SCC_EPCI_SWRESP         0x818
#define SCC_EPCI_CNTOPT         0x81c
#define SCC_EPCI_ECMODE         0xf00
#define SCC_EPCI_IOM_AC_NUM     5
#define SCC_EPCI_IOM_ACTE(n)    (0xf10 + (n) * 4)
#define SCC_EPCI_IOT_AC_NUM     4
#define SCC_EPCI_IOT_ACTE(n)    (0xf30 + (n) * 4)
#define SCC_EPCI_MAEA           0xf50
#define SCC_EPCI_MAEC           0xf54
#define SCC_EPCI_CKCTRL         0xff0

/* bits for SCC_EPCI_VCSR */
#define SCC_EPCI_VCSR_FRE       0x00020000
#define SCC_EPCI_VCSR_FWE       0x00010000
#define SCC_EPCI_VCSR_DR        0x00000400
#define SCC_EPCI_VCSR_SR        0x00000008
#define SCC_EPCI_VCSR_AT        0x00000004

/* bits for SCC_EPCI_VIENAB/SCC_EPCI_VISTAT */
#define SCC_EPCI_VISTAT_PMPE    0x00000008
#define SCC_EPCI_VISTAT_PMFE    0x00000004
#define SCC_EPCI_VISTAT_PRA     0x00000002
#define SCC_EPCI_VISTAT_PRD     0x00000001
#define SCC_EPCI_VISTAT_ALL     0x0000000f

#define SCC_EPCI_VIENAB_PMPEE   0x00000008
#define SCC_EPCI_VIENAB_PMFEE   0x00000004
#define SCC_EPCI_VIENAB_PRA     0x00000002
#define SCC_EPCI_VIENAB_PRD     0x00000001
#define SCC_EPCI_VIENAB_ALL     0x0000000f

/* bits for SCC_EPCI_CLKRST */
#define SCC_EPCI_CLKRST_CKS_MASK 0x00030000
#define SCC_EPCI_CLKRST_CKS_2   0x00000000
#define SCC_EPCI_CLKRST_CKS_4   0x00010000
#define SCC_EPCI_CLKRST_CKS_8   0x00020000
#define SCC_EPCI_CLKRST_PCICRST 0x00000400
#define SCC_EPCI_CLKRST_BC      0x00000200
#define SCC_EPCI_CLKRST_PCIRST  0x00000100
#define SCC_EPCI_CLKRST_PCKEN   0x00000001

/* bits for SCC_EPCI_INTSET/SCC_EPCI_STATUS */
#define SCC_EPCI_INT_2M         0x01000000
#define SCC_EPCI_INT_RERR       0x00200000
#define SCC_EPCI_INT_SERR       0x00100000
#define SCC_EPCI_INT_PRTER      0x00080000
#define SCC_EPCI_INT_SER        0x00040000
#define SCC_EPCI_INT_PER        0x00020000
#define SCC_EPCI_INT_PAI        0x00010000
#define SCC_EPCI_INT_1M         0x00000100
#define SCC_EPCI_INT_PME        0x00000010
#define SCC_EPCI_INT_INTD       0x00000008
#define SCC_EPCI_INT_INTC       0x00000004
#define SCC_EPCI_INT_INTB       0x00000002
#define SCC_EPCI_INT_INTA       0x00000001
#define SCC_EPCI_INT_DEVINT     0x0000000f
#define SCC_EPCI_INT_ALL        0x003f001f
#define SCC_EPCI_INT_ALLERR     0x003f0000

/* bits for SCC_EPCI_CKCTRL */
#define SCC_EPCI_CKCTRL_CRST0   0x00010000
#define SCC_EPCI_CKCTRL_CRST1   0x00020000
#define SCC_EPCI_CKCTRL_OCLKEN  0x00000100
#define SCC_EPCI_CKCTRL_LCLKEN  0x00000001

#define SCC_EPCI_IDSEL_AD_TO_SLOT(ad)       ((ad) - 10)
#define SCC_EPCI_MAX_DEVNU      SCC_EPCI_IDSEL_AD_TO_SLOT(32)

/* bits for SCC_EPCI_CNTOPT */
#define SCC_EPCI_CNTOPT_O2PMB   0x00000002

/* UHC registers */
#define SCC_UHC_CKRCTRL         0xff0
#define SCC_UHC_ECMODE          0xf00

/* bits for SCC_UHC_CKRCTRL */
#define SCC_UHC_F48MCKLEN       0x00000001
#define SCC_UHC_P_SUSPEND       0x00000002
#define SCC_UHC_PHY_SUSPEND_SEL 0x00000004
#define SCC_UHC_HCLKEN          0x00000100
#define SCC_UHC_USBEN           0x00010000
#define SCC_UHC_USBCEN          0x00020000
#define SCC_UHC_PHYEN           0x00040000

/* bits for SCC_UHC_ECMODE */
#define SCC_UHC_ECMODE_BY_BYTE  0x00000555
#define SCC_UHC_ECMODE_BY_WORD  0x00000aaa

#endif /* _CELLEB_SCC_H */

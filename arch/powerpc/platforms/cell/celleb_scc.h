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

/* SCC PCIEXC SMMIO registers */
#define PEXCADRS		0x000
#define PEXCWDATA		0x004
#define PEXCRDATA		0x008
#define PEXDADRS		0x010
#define PEXDCMND		0x014
#define PEXDWDATA		0x018
#define PEXDRDATA		0x01c
#define PEXREQID		0x020
#define PEXTIDMAP		0x024
#define PEXINTMASK		0x028
#define PEXINTSTS		0x02c
#define PEXAERRMASK		0x030
#define PEXAERRSTS		0x034
#define PEXPRERRMASK		0x040
#define PEXPRERRSTS		0x044
#define PEXPRERRID01		0x048
#define PEXPRERRID23		0x04c
#define PEXVDMASK		0x050
#define PEXVDSTS		0x054
#define PEXRCVCPLIDA		0x060
#define PEXLENERRIDA		0x068
#define PEXPHYPLLST		0x070
#define PEXDMRDEN0		0x100
#define PEXDMRDADR0		0x104
#define PEXDMRDENX		0x110
#define PEXDMRDADRX		0x114
#define PEXECMODE		0xf00
#define PEXMAEA(n)		(0xf50 + (8 * n))
#define PEXMAEC(n)		(0xf54 + (8 * n))
#define PEXCCRCTRL		0xff0

/* SCC PCIEXC bits and shifts for PEXCADRS */
#define PEXCADRS_BYTE_EN_SHIFT		20
#define PEXCADRS_CMD_SHIFT		16
#define PEXCADRS_CMD_READ		(0xa << PEXCADRS_CMD_SHIFT)
#define PEXCADRS_CMD_WRITE		(0xb << PEXCADRS_CMD_SHIFT)

/* SCC PCIEXC shifts for PEXDADRS */
#define PEXDADRS_BUSNO_SHIFT		20
#define PEXDADRS_DEVNO_SHIFT		15
#define PEXDADRS_FUNCNO_SHIFT		12

/* SCC PCIEXC bits and shifts for PEXDCMND */
#define PEXDCMND_BYTE_EN_SHIFT		4
#define PEXDCMND_IO_READ		0x2
#define PEXDCMND_IO_WRITE		0x3
#define PEXDCMND_CONFIG_READ		0xa
#define PEXDCMND_CONFIG_WRITE		0xb

/* SCC PCIEXC bits for PEXPHYPLLST */
#define PEXPHYPLLST_PEXPHYAPLLST	0x00000001

/* SCC PCIEXC bits for PEXECMODE */
#define PEXECMODE_ALL_THROUGH		0x00000000
#define PEXECMODE_ALL_8BIT		0x00550155
#define PEXECMODE_ALL_16BIT		0x00aa02aa

/* SCC PCIEXC bits for PEXCCRCTRL */
#define PEXCCRCTRL_PEXIPCOREEN		0x00040000
#define PEXCCRCTRL_PEXIPCONTEN		0x00020000
#define PEXCCRCTRL_PEXPHYPLLEN		0x00010000
#define PEXCCRCTRL_PCIEXCAOCKEN		0x00000100

/* SCC PCIEXC port configuration registers */
#define PEXTCERRCHK		0x21c
#define PEXTAMAPB0		0x220
#define PEXTAMAPL0		0x224
#define PEXTAMAPB(n)		(PEXTAMAPB0 + 8 * (n))
#define PEXTAMAPL(n)		(PEXTAMAPL0 + 8 * (n))
#define PEXCHVC0P		0x500
#define PEXCHVC0NP		0x504
#define PEXCHVC0C		0x508
#define PEXCDVC0P		0x50c
#define PEXCDVC0NP		0x510
#define PEXCDVC0C		0x514
#define PEXCHVCXP		0x518
#define PEXCHVCXNP		0x51c
#define PEXCHVCXC		0x520
#define PEXCDVCXP		0x524
#define PEXCDVCXNP		0x528
#define PEXCDVCXC		0x52c
#define PEXCTTRG		0x530
#define PEXTSCTRL		0x700
#define PEXTSSTS		0x704
#define PEXSKPCTRL		0x708

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

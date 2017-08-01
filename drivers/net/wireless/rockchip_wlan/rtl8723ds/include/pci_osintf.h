/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#ifndef __PCI_OSINTF_H
#define __PCI_OSINTF_H

#ifdef RTK_129X_PLATFORM
#define PCIE_SLOT1_MEM_START	0x9804F000
#define PCIE_SLOT1_MEM_LEN	0x1000
#define PCIE_SLOT1_MASK		0x9804ED00

#define PCIE_SLOT2_MEM_START	0x9803C000
#define PCIE_SLOT2_MEM_LEN	0x1000
#define PCIE_SLOT2_MASK		0x9803BD00

#define PCIE_TRANSLATE_OFFSET   4 /* offset from MASK reg */
#endif

void	rtw_pci_disable_aspm(_adapter *padapter);
void	rtw_pci_enable_aspm(_adapter *padapter);
void	PlatformClearPciPMEStatus(PADAPTER Adapter);
#ifdef CONFIG_64BIT_DMA
	u8	PlatformEnableDMA64(PADAPTER Adapter);
#endif

#endif

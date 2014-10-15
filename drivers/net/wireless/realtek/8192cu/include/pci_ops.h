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
#ifndef __PCI_OPS_H_
#define __PCI_OPS_H_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <osdep_intf.h>

#ifdef CONFIG_RTL8192C
u32	rtl8192ce_init_desc_ring(_adapter * padapter);
u32	rtl8192ce_free_desc_ring(_adapter * padapter);
void	rtl8192ce_reset_desc_ring(_adapter * padapter);
#ifdef CONFIG_64BIT_DMA
u8	PlatformEnable92CEDMA64(PADAPTER Adapter);
#endif
int	rtl8192ce_interrupt(PADAPTER Adapter);
void	rtl8192ce_xmit_tasklet(void *priv);
void	rtl8192ce_recv_tasklet(void *priv);
void	rtl8192ce_prepare_bcn_tasklet(void *priv);
void	rtl8192ce_set_intf_ops(struct _io_ops	*pops);
#define pci_set_intf_ops	rtl8192ce_set_intf_ops
#endif

#ifdef CONFIG_RTL8192D
u32	rtl8192de_init_desc_ring(_adapter * padapter);
u32	rtl8192de_free_desc_ring(_adapter * padapter);
void	rtl8192de_reset_desc_ring(_adapter * padapter);
#ifdef CONFIG_64BIT_DMA
u8	PlatformEnable92DEDMA64(PADAPTER Adapter);
#endif
int	rtl8192de_interrupt(PADAPTER Adapter);
void	rtl8192de_xmit_tasklet(void *priv);
void	rtl8192de_recv_tasklet(void *priv);
void	rtl8192de_prepare_bcn_tasklet(void *priv);
void	rtl8192de_set_intf_ops(struct _io_ops	*pops);
#define pci_set_intf_ops	rtl8192de_set_intf_ops
u32	MpReadPCIDwordDBI8192D(IN PADAPTER Adapter, IN u16 Offset, IN u8 Direct);
void	MpWritePCIDwordDBI8192D(IN PADAPTER Adapter, IN u16 Offset, IN u32 Value, IN u8 Direct);
#endif

#endif

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


#ifdef CONFIG_RTL8188E
u32	rtl8188ee_init_desc_ring(_adapter * padapter);
u32	rtl8188ee_free_desc_ring(_adapter * padapter);
void	rtl8188ee_reset_desc_ring(_adapter * padapter);
int	rtl8188ee_interrupt(PADAPTER Adapter);
void	rtl8188ee_xmit_tasklet(void *priv);
void	rtl8188ee_recv_tasklet(void *priv);
void	rtl8188ee_prepare_bcn_tasklet(void *priv);
void	rtl8188ee_set_intf_ops(struct _io_ops	*pops);
#endif


#ifdef CONFIG_RTL8192C
u32	rtl8192ce_init_desc_ring(_adapter * padapter);
u32	rtl8192ce_free_desc_ring(_adapter * padapter);
void	rtl8192ce_reset_desc_ring(_adapter * padapter);
int	rtl8192ce_interrupt(PADAPTER Adapter);
void	rtl8192ce_xmit_tasklet(void *priv);
void	rtl8192ce_recv_tasklet(void *priv);
void	rtl8192ce_prepare_bcn_tasklet(void *priv);
void	rtl8192ce_set_intf_ops(struct _io_ops	*pops);
#endif

#ifdef CONFIG_RTL8192D
u32	rtl8192de_init_desc_ring(_adapter * padapter);
u32	rtl8192de_free_desc_ring(_adapter * padapter);
void	rtl8192de_reset_desc_ring(_adapter * padapter);
int	rtl8192de_interrupt(PADAPTER Adapter);
void	rtl8192de_xmit_tasklet(void *priv);
void	rtl8192de_recv_tasklet(void *priv);
void	rtl8192de_prepare_bcn_tasklet(void *priv);
void	rtl8192de_set_intf_ops(struct _io_ops	*pops);
u32	MpReadPCIDwordDBI8192D(IN PADAPTER Adapter, IN u16 Offset, IN u8 Direct);
void	MpWritePCIDwordDBI8192D(IN PADAPTER Adapter, IN u16 Offset, IN u32 Value, IN u8 Direct);
#endif

#if defined(CONFIG_RTL8812A) || defined(CONFIG_RTL8821A)
u32	rtl8812ae_init_desc_ring(_adapter * padapter);
u32	rtl8812ae_free_desc_ring(_adapter * padapter);
void	rtl8812ae_reset_desc_ring(_adapter * padapter);
int	rtl8812ae_interrupt(PADAPTER Adapter);
void	rtl8812ae_xmit_tasklet(void *priv);
void	rtl8812ae_recv_tasklet(void *priv);
void	rtl8812ae_prepare_bcn_tasklet(void *priv);
void	rtl8812ae_set_intf_ops(struct _io_ops	*pops);
#endif

#ifdef CONFIG_RTL8723B
u32	rtl8723be_init_desc_ring(_adapter * padapter);
u32	rtl8723be_free_desc_ring(_adapter * padapter);
void	rtl8723be_reset_desc_ring(_adapter * padapter);
int	rtl8723be_interrupt(PADAPTER Adapter);
void	rtl8723be_recv_tasklet(void *priv);
void	rtl8723be_prepare_bcn_tasklet(void *priv);
void	rtl8723be_set_intf_ops(struct _io_ops	*pops);
#endif

#endif


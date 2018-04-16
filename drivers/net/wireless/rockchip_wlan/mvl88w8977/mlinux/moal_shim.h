/** @file moal_shim.h
  *
  * @brief This file contains declaration referring to
  * functions defined in moal module
  *
  * Copyright (C) 2008-2017, Marvell International Ltd.
  *
  * This software file (the "File") is distributed by Marvell International
  * Ltd. under the terms of the GNU General Public License Version 2, June 1991
  * (the "License").  You may use, redistribute and/or modify this File in
  * accordance with the terms and conditions of the License, a copy of which
  * is available by writing to the Free Software Foundation, Inc.,
  * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
  * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
  *
  * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
  * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
  * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
  * this warranty disclaimer.
  *
  */
/*************************************************************
Change Log:
    10/21/2008: initial version
************************************************************/

#ifndef _MOAL_H
#define _MOAL_H

mlan_status moal_get_fw_data(IN t_void *pmoal_handle,
			     IN t_u32 offset, IN t_u32 len, OUT t_u8 *pbuf);
mlan_status moal_get_hw_spec_complete(IN t_void *pmoal_handle,
				      IN mlan_status status,
				      IN mlan_hw_info * phw,
				      IN pmlan_bss_tbl ptbl);
mlan_status moal_init_fw_complete(IN t_void *pmoal_handle,
				  IN mlan_status status);
mlan_status moal_shutdown_fw_complete(IN t_void *pmoal_handle,
				      IN mlan_status status);
mlan_status moal_ioctl_complete(IN t_void *pmoal_handle,
				IN pmlan_ioctl_req pioctl_req,
				IN mlan_status status);
mlan_status moal_alloc_mlan_buffer(IN t_void *pmoal_handle, IN t_u32 size,
				   OUT pmlan_buffer *pmbuf);
mlan_status moal_free_mlan_buffer(IN t_void *pmoal_handle,
				  IN pmlan_buffer pmbuf);
mlan_status moal_send_packet_complete(IN t_void *pmoal_handle,
				      IN pmlan_buffer pmbuf,
				      IN mlan_status status);

/** moal_write_reg */
mlan_status moal_write_reg(IN t_void *pmoal_handle,
			   IN t_u32 reg, IN t_u32 data);
/** moal_read_reg */
mlan_status moal_read_reg(IN t_void *pmoal_handle,
			  IN t_u32 reg, OUT t_u32 *data);
mlan_status moal_write_data_sync(IN t_void *pmoal_handle,
				 IN pmlan_buffer pmbuf,
				 IN t_u32 port, IN t_u32 timeout);
mlan_status moal_read_data_sync(IN t_void *pmoal_handle,
				IN OUT pmlan_buffer pmbuf,
				IN t_u32 port, IN t_u32 timeout);
mlan_status moal_recv_packet(IN t_void *pmoal_handle, IN pmlan_buffer pmbuf);
mlan_status moal_recv_event(IN t_void *pmoal_handle, IN pmlan_event pmevent);
mlan_status moal_malloc(IN t_void *pmoal_handle,
			IN t_u32 size, IN t_u32 flag, OUT t_u8 **ppbuf);
mlan_status moal_mfree(IN t_void *pmoal_handle, IN t_u8 *pbuf);
mlan_status moal_vmalloc(IN t_void *pmoal_handle,
			 IN t_u32 size, OUT t_u8 **ppbuf);
mlan_status moal_vfree(IN t_void *pmoal_handle, IN t_u8 *pbuf);
t_void *moal_memset(IN t_void *pmoal_handle,
		    IN t_void *pmem, IN t_u8 byte, IN t_u32 num);
t_void *moal_memcpy(IN t_void *pmoal_handle,
		    IN t_void *pdest, IN const t_void *psrc, IN t_u32 num);
t_void *moal_memmove(IN t_void *pmoal_handle,
		     IN t_void *pdest, IN const t_void *psrc, IN t_u32 num);
t_s32 moal_memcmp(IN t_void *pmoal_handle,
		  IN const t_void *pmem1, IN const t_void *pmem2, IN t_u32 num);
/** moal_udelay */
t_void moal_udelay(IN t_void *pmoal_handle, IN t_u32 udelay);
mlan_status moal_get_system_time(IN t_void *pmoal_handle, OUT t_u32 *psec,
				 OUT t_u32 *pusec);
mlan_status moal_init_lock(IN t_void *pmoal_handle, OUT t_void **pplock);
mlan_status moal_free_lock(IN t_void *pmoal_handle, IN t_void *plock);
mlan_status moal_spin_lock(IN t_void *pmoal_handle, IN t_void *plock);
mlan_status moal_spin_unlock(IN t_void *pmoal_handle, IN t_void *plock);
t_void moal_print(IN t_void *pmoal_handle, IN t_u32 level, IN char *pformat,
		  IN ...);
t_void moal_print_netintf(IN t_void *pmoal_handle, IN t_u32 bss_index,
			  IN t_u32 level);
t_void moal_assert(IN t_void *pmoal_handle, IN t_u32 cond);
t_void moal_hist_data_add(IN t_void *pmoal_handle, IN t_u32 bss_index,
			  IN t_u8 rx_rate, IN t_s8 snr, IN t_s8 nflr,
			  IN t_u8 antenna);

t_void moal_updata_peer_signal(IN t_void *pmoal_handle, IN t_u32 bss_index,
			       IN t_u8 *peer_addr, IN t_s8 snr, IN t_s8 nflr);
mlan_status moal_get_host_time_ns(OUT t_u64 *time);
t_u32 moal_do_div(IN t_u64 num, IN t_u32 base);

mlan_status moal_init_timer(IN t_void *pmoal_handle,
			    OUT t_void **pptimer,
			    IN t_void (*callback) (t_void *pcontext),
			    IN t_void *pcontext);
mlan_status moal_free_timer(IN t_void *pmoal_handle, IN t_void *ptimer);
mlan_status moal_start_timer(IN t_void *pmoal_handle,
			     IN t_void *ptimer,
			     IN t_u8 periodic, IN t_u32 msec);
mlan_status moal_stop_timer(IN t_void *pmoal_handle, IN t_void *ptimer);

#endif /*_MOAL_H */

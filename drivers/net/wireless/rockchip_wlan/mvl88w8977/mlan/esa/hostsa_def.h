/** @file hostsa_def.h
 *
 *  @brief This file contains data structrue for authenticator/supplicant.
 *
 * Copyright (C) 2014-2017, Marvell International Ltd.
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
 */

/******************************************************
Change log:
    03/07/2014: Initial version
******************************************************/
#ifndef _HOSTSA_DEF_H
#define _HOSTSA_DEF_H
/* subset of mlan_callbacks data structure */
/** hostsa_util_fns data structure */
typedef struct _hostsa_util_fns {
    /** pmoal_handle */
	t_void *pmoal_handle;
    /** moal_malloc */
	mlan_status (*moal_malloc) (IN t_void *pmoal_handle,
				    IN t_u32 size,
				    IN t_u32 flag, OUT t_u8 **ppbuf);
    /** moal_mfree */
	mlan_status (*moal_mfree) (IN t_void *pmoal_handle, IN t_u8 *pbuf);
    /** moal_memset */
	t_void *(*moal_memset) (IN t_void *pmoal_handle,
				IN t_void *pmem, IN t_u8 byte, IN t_u32 num);
    /** moal_memcpy */
	t_void *(*moal_memcpy) (IN t_void *pmoal_handle,
				IN t_void *pdest,
				IN const t_void *psrc, IN t_u32 num);
    /** moal_memmove */
	t_void *(*moal_memmove) (IN t_void *pmoal_handle,
				 IN t_void *pdest,
				 IN const t_void *psrc, IN t_u32 num);
    /** moal_memcmp */
	t_s32 (*moal_memcmp) (IN t_void *pmoal_handle,
			      IN const t_void *pmem1,
			      IN const t_void *pmem2, IN t_u32 num);
    /** moal_udelay */
	t_void (*moal_udelay) (IN t_void *pmoal_handle, IN t_u32 udelay);
    /** moal_get_system_time */
	mlan_status (*moal_get_system_time) (IN t_void *pmoal_handle,
					     OUT t_u32 *psec, OUT t_u32 *pusec);
    /** moal_init_timer*/
	mlan_status (*moal_init_timer) (IN t_void *pmoal_handle,
					OUT t_void **pptimer,
					IN t_void (*callback) (t_void
							       *pcontext),
					IN t_void *pcontext);
    /** moal_free_timer */
	mlan_status (*moal_free_timer) (IN t_void *pmoal_handle,
					IN t_void *ptimer);
    /** moal_start_timer*/
	mlan_status (*moal_start_timer) (IN t_void *pmoal_handle,
					 IN t_void *ptimer,
					 IN t_u8 periodic, IN t_u32 msec);
    /** moal_stop_timer*/
	mlan_status (*moal_stop_timer) (IN t_void *pmoal_handle,
					IN t_void *ptimer);
    /** moal_init_lock */
	mlan_status (*moal_init_lock) (IN t_void *pmoal_handle,
				       OUT t_void **pplock);
    /** moal_free_lock */
	mlan_status (*moal_free_lock) (IN t_void *pmoal_handle,
				       IN t_void *plock);
    /** moal_spin_lock */
	mlan_status (*moal_spin_lock) (IN t_void *pmoal_handle,
				       IN t_void *plock);
    /** moal_spin_unlock */
	mlan_status (*moal_spin_unlock) (IN t_void *pmoal_handle,
					 IN t_void *plock);
    /** moal_print */
	t_void (*moal_print) (IN t_void *pmoal_handle,
			      IN t_u32 level, IN char *pformat, IN ...
		);
    /** moal_print_netintf */
	t_void (*moal_print_netintf) (IN t_void *pmoal_handle,
				      IN t_u32 bss_index, IN t_u32 level);
} hostsa_util_fns, *phostsa_util_fns;
/* Required functions from mlan */
/** hostsa_mlan_fns data structure */
typedef struct _hostsa_mlan_fns {
    /** pmlan_private */
	t_void *pmlan_private;
    /** pmlan_adapter */
	t_void *pmlan_adapter;
    /** BSS index */
	t_u8 bss_index;
    /** BSS type */
	t_u8 bss_type;

	pmlan_buffer (*hostsa_alloc_mlan_buffer) (t_void *pmlan_adapter,
						  t_u32 data_len,
						  t_u32 head_room,
						  t_u32 malloc_flag);
	void (*hostsa_tx_packet) (t_void *pmlan_private,
				  pmlan_buffer pmbuf, t_u16 frameLen);
	void (*hostsa_set_encrypt_key) (t_void *pmlan_private,
					mlan_ds_encrypt_key *encrypt_key);
	void (*hostsa_clr_encrypt_key) (t_void *pmlan_private);
	void (*hostsa_SendDeauth) (t_void *pmlan_private,
				   t_u8 *addr, t_u16 reason);
	void (*Hostsa_DisAssocAllSta) (void *pmlan_private, t_u16 reason);
	void (*hostsa_free_mlan_buffer) (t_void *pmlan_adapter,
					 mlan_buffer *pmbuf);
	void (*Hostsa_get_station_entry) (t_void *pmlan_private,
					  t_u8 *mac, t_void **ppconPtr);
	void (*Hostsa_set_mgmt_ie) (t_void *pmlan_private,
				    t_u8 *pbuf, t_u16 len, t_u8 clearIE);
	void (*Hostsa_find_connection) (t_void *pmlan_private,
					t_void **ppconPtr, t_void **ppsta_node);
	void (*Hostsa_find_next_connection) (t_void *pmlan_private,
					     t_void **ppconPtr,
					     t_void **ppsta_node);
	t_void (*Hostsa_StaControlledPortOpen) (t_void *pmlan_private);
	void (*hostsa_StaSendDeauth) (t_void *pmlan_private,
				      t_u8 *addr, t_u16 reason);
	t_u8 (*Hostsa_get_bss_role) (t_void *pmlan_private);
	t_u8 (*Hostsa_get_intf_hr_len) (t_void *pmlan_private);
	t_void (*Hostsa_sendEventRsnConnect) (t_void *pmlan_private,
					      t_u8 *addr);
} hostsa_mlan_fns, *phostsa_mlan_fns;
#endif

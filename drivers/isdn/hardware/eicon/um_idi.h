/* SPDX-License-Identifier: GPL-2.0 */
/* $Id: um_idi.h,v 1.6 2004/03/21 17:26:01 armin Exp $ */

#ifndef __DIVA_USER_MODE_IDI_CORE_H__
#define __DIVA_USER_MODE_IDI_CORE_H__


/*
  interface between UM IDI core and OS dependent part
*/
int diva_user_mode_idi_init(void);
void diva_user_mode_idi_finit(void);
void *divas_um_idi_create_entity(dword adapter_nr, void *file);
int divas_um_idi_delete_entity(int adapter_nr, void *entity);

typedef int (*divas_um_idi_copy_to_user_fn_t) (void *os_handle,
					       void *dst,
					       const void *src,
					       int length);
typedef int (*divas_um_idi_copy_from_user_fn_t) (void *os_handle,
						 void *dst,
						 const void *src,
						 int length);

int diva_um_idi_read(void *entity,
		     void *os_handle,
		     void *dst,
		     int max_length, divas_um_idi_copy_to_user_fn_t cp_fn);

int diva_um_idi_write(void *entity,
		      void *os_handle,
		      const void *src,
		      int length, divas_um_idi_copy_from_user_fn_t cp_fn);

int diva_user_mode_idi_ind_ready(void *entity, void *os_handle);
void *diva_um_id_get_os_context(void *entity);
int diva_os_get_context_size(void);
int divas_um_idi_entity_assigned(void *entity);
int divas_um_idi_entity_start_remove(void *entity);

void diva_um_idi_start_wdog(void *entity);
void diva_um_idi_stop_wdog(void *entity);

#endif

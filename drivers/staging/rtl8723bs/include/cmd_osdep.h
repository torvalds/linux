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
 ******************************************************************************/
#ifndef __CMD_OSDEP_H_
#define __CMD_OSDEP_H_


extern sint _rtw_init_cmd_priv (struct	cmd_priv *pcmdpriv);
extern sint _rtw_init_evt_priv(struct evt_priv *pevtpriv);
extern void _rtw_free_evt_priv (struct	evt_priv *pevtpriv);
extern void _rtw_free_cmd_priv (struct	cmd_priv *pcmdpriv);
extern sint _rtw_enqueue_cmd(struct __queue *queue, struct cmd_obj *obj);
extern struct	cmd_obj	*_rtw_dequeue_cmd(struct __queue *queue);

#endif

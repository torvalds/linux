/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
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

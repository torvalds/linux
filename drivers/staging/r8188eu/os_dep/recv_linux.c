// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#define _RECV_OSDEP_C_

#include "../include/osdep_service.h"
#include "../include/drv_types.h"

#include "../include/wifi.h"
#include "../include/recv_osdep.h"

#include "../include/osdep_intf.h"
#include "../include/usb_ops.h"

static void _rtw_reordering_ctrl_timeout_handler(struct timer_list *t)
{
	struct recv_reorder_ctrl *preorder_ctrl;

	preorder_ctrl = from_timer(preorder_ctrl, t, reordering_ctrl_timer);
	rtw_reordering_ctrl_timeout_handler(preorder_ctrl);
}

void rtw_init_recv_timer(struct recv_reorder_ctrl *preorder_ctrl)
{
	timer_setup(&preorder_ctrl->reordering_ctrl_timer, _rtw_reordering_ctrl_timeout_handler, 0);
}

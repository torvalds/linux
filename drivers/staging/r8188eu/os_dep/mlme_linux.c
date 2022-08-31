// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2007 - 2011 Realtek Corporation. i*/

#define _MLME_OSDEP_C_

#include "../include/osdep_service.h"
#include "../include/drv_types.h"
#include "../include/mlme_osdep.h"

static void rtw_join_timeout_handler(struct timer_list *t)
{
	struct adapter *adapter = from_timer(adapter, t, mlmepriv.assoc_timer);

	_rtw_join_timeout_handler(adapter);
}

static void _rtw_scan_timeout_handler(struct timer_list *t)
{
	struct adapter *adapter = from_timer(adapter, t, mlmepriv.scan_to_timer);

	rtw_scan_timeout_handler(adapter);
}

static void _dynamic_check_timer_handlder(struct timer_list *t)
{
	struct adapter *adapter = from_timer(adapter, t, mlmepriv.dynamic_chk_timer);

	rtw_dynamic_check_timer_handlder(adapter);
	_set_timer(&adapter->mlmepriv.dynamic_chk_timer, 2000);
}

void rtw_init_mlme_timer(struct adapter *padapter)
{
	struct	mlme_priv *pmlmepriv = &padapter->mlmepriv;

	timer_setup(&pmlmepriv->assoc_timer, rtw_join_timeout_handler, 0);
	timer_setup(&pmlmepriv->scan_to_timer, _rtw_scan_timeout_handler, 0);
	timer_setup(&pmlmepriv->dynamic_chk_timer, _dynamic_check_timer_handlder, 0);
}

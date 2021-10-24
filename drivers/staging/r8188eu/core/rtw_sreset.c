// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2007 - 2012 Realtek Corporation. */

#include "../include/rtw_sreset.h"

void sreset_init_value(struct adapter *padapter)
{
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(padapter);
	struct sreset_priv *psrtpriv = &pHalData->srestpriv;

	psrtpriv->last_tx_time = 0;
	psrtpriv->last_tx_complete_time = 0;
}
void sreset_reset_value(struct adapter *padapter)
{
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(padapter);
	struct sreset_priv *psrtpriv = &pHalData->srestpriv;

	psrtpriv->last_tx_time = 0;
	psrtpriv->last_tx_complete_time = 0;
}

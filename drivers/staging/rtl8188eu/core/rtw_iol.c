// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/

#include <rtw_iol.h>

bool rtw_iol_applied(struct adapter *adapter)
{
	if (adapter->registrypriv.fw_iol == 1)
		return true;

	if (adapter->registrypriv.fw_iol == 2 &&
	    !adapter_to_dvobj(adapter)->ishighspeed)
		return true;
	return false;
}

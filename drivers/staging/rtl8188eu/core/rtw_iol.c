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
 *
 ******************************************************************************/

#include <rtw_iol.h>

bool rtw_IOL_applied(struct adapter *adapter)
{
	if (adapter->registrypriv.fw_iol == 1)
		return true;

	if ((adapter->registrypriv.fw_iol == 2) &&
	    (!adapter_to_dvobj(adapter)->ishighspeed))
		return true;
	return false;
}

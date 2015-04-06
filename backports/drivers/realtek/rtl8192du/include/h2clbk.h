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


#define _H2CLBK_H_


#include <rtl8711_spec.h>
#include <TypeDef.h>


void _lbk_cmd(PADAPTER adapter);

void _lbk_rsp(PADAPTER adapter);

void _lbk_evt(PADAPTER adapter);

void h2c_event_callback(unsigned char *dev, unsigned char *pbuf);

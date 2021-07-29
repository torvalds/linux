/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#define _H2CLBK_H_

#include <rtl8711_spec.h>
#include <TypeDef.h>

void _lbk_cmd(struct adapter *adapter);

void _lbk_rsp(struct adapter *adapter);

void _lbk_evt(IN struct adapter *adapter);

void h2c_event_callback(unsigned char *dev, unsigned char *pbuf);

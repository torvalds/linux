/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2010 - 2015, Intel Corporation.
 */

#ifndef _IA_CSS_EVENT_H
#define _IA_CSS_EVENT_H

#include <type_support.h>
#include "sw_event_global.h"    /*event macros.TODO : Change File Name..???*/

bool ia_css_event_encode(
    u8	*in,
    u8	nr,
    uint32_t	*out);

void ia_css_event_decode(
    u32 event,
    uint8_t *payload);

#endif /*_IA_CSS_EVENT_H*/

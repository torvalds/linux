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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#ifndef __RTW_MEM_H__
#define __RTW_MEM_H__

#include <drv_conf.h>
#include <basic_types.h>
#include <osdep_service.h>


#ifndef MAX_RECVBUF_SZ
#define MAX_RECVBUF_SZ (32768-RECVBUFF_ALIGN_SZ) // 32k
#endif

struct u8* rtw_alloc_revcbuf_premem(void);
struct sk_buff *rtw_alloc_skb_premem(void);
int rtw_free_skb_premem(struct sk_buff *pskb);


#endif //__RTW_MEM_H__


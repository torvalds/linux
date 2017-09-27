/*
 * Copyright 2014 Cisco Systems, Inc.  All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _CQ_ENET_DESC_H_
#define _CQ_ENET_DESC_H_

#include "cq_desc.h"

/* Ethernet completion queue descriptor: 16B */
struct cq_enet_wq_desc {
	__le16 completed_index;
	__le16 q_number;
	u8 reserved[11];
	u8 type_color;
};

static inline void cq_enet_wq_desc_dec(struct cq_enet_wq_desc *desc,
	u8 *type, u8 *color, u16 *q_number, u16 *completed_index)
{
	cq_desc_dec((struct cq_desc *)desc, type,
		color, q_number, completed_index);
}

#endif /* _CQ_ENET_DESC_H_ */

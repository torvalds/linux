/*
 * This file is part of the Chelsio T4/T5/T6 Ethernet driver for Linux.
 *
 * Copyright (c) 2017 Chelsio Communications, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
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

#include <net/tc_act/tc_gact.h>
#include <net/tc_act/tc_mirred.h>

#include "cxgb4.h"
#include "cxgb4_tc_flower.h"

int cxgb4_tc_flower_replace(struct net_device *dev,
			    struct tc_cls_flower_offload *cls)
{
	return -EOPNOTSUPP;
}

int cxgb4_tc_flower_destroy(struct net_device *dev,
			    struct tc_cls_flower_offload *cls)
{
	return -EOPNOTSUPP;
}

int cxgb4_tc_flower_stats(struct net_device *dev,
			  struct tc_cls_flower_offload *cls)
{
	return -EOPNOTSUPP;
}

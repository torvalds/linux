// SPDX-License-Identifier: GPL-2.0
/* Marvell OcteonTx2 RVU Ethernet driver
 *
 * Copyright (C) 2020 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/interrupt.h>
#include <linux/pci.h>

#include "otx2_reg.h"
#include "otx2_common.h"

#define M(_name, _id, _fn_name, _req_type, _rsp_type)			\
int __weak								\
otx2_mbox_up_handler_ ## _fn_name(struct otx2_nic *pfvf,		\
				struct _req_type *req,			\
				struct _rsp_type *rsp)			\
{									\
	/* Nothing to do here */					\
	return 0;							\
}									\
EXPORT_SYMBOL(otx2_mbox_up_handler_ ## _fn_name);
MBOX_UP_CGX_MESSAGES
#undef M

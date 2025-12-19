// SPDX-License-Identifier: GPL-2.0
/* Marvell RVU Admin Function driver
 *
 * Copyright (C) 2024 Marvell.
 *
 */

#include <linux/module.h>
#include <linux/pci.h>

#include "struct.h"
#include "../rvu.h"

int rvu_mbox_handler_npa_cn20k_aq_enq(struct rvu *rvu,
				      struct npa_cn20k_aq_enq_req *req,
				      struct npa_cn20k_aq_enq_rsp *rsp)
{
	return rvu_npa_aq_enq_inst(rvu, (struct npa_aq_enq_req *)req,
				   (struct npa_aq_enq_rsp *)rsp);
}
EXPORT_SYMBOL(rvu_mbox_handler_npa_cn20k_aq_enq);

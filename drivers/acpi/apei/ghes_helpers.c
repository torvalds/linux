// SPDX-License-Identifier: GPL-2.0-only
// Copyright(c) 2025 Intel Corporation. All rights reserved

#include <linux/printk.h>
#include <cxl/event.h>

int cxl_cper_sec_prot_err_valid(struct cxl_cper_sec_prot_err *prot_err)
{
	if (!(prot_err->valid_bits & PROT_ERR_VALID_AGENT_ADDRESS)) {
		pr_err_ratelimited("CXL CPER invalid agent type\n");
		return -EINVAL;
	}

	if (!(prot_err->valid_bits & PROT_ERR_VALID_ERROR_LOG)) {
		pr_err_ratelimited("CXL CPER invalid protocol error log\n");
		return -EINVAL;
	}

	if (prot_err->err_len != sizeof(struct cxl_ras_capability_regs)) {
		pr_err_ratelimited("CXL CPER invalid RAS Cap size (%u)\n",
				   prot_err->err_len);
		return -EINVAL;
	}

	if ((prot_err->agent_type == RCD || prot_err->agent_type == DEVICE ||
	     prot_err->agent_type == LD || prot_err->agent_type == FMLD) &&
	    !(prot_err->valid_bits & PROT_ERR_VALID_SERIAL_NUMBER))
		pr_warn_ratelimited(FW_WARN
				    "CXL CPER no device serial number\n");

	return 0;
}
EXPORT_SYMBOL_GPL(cxl_cper_sec_prot_err_valid);

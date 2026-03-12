// SPDX-License-Identifier: GPL-2.0-only
// Copyright(c) 2025 Intel Corporation. All rights reserved

#include <linux/printk.h>
#include <linux/aer.h>
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

int cxl_cper_setup_prot_err_work_data(struct cxl_cper_prot_err_work_data *wd,
				      struct cxl_cper_sec_prot_err *prot_err,
				      int severity)
{
	u8 *dvsec_start, *cap_start;

	switch (prot_err->agent_type) {
	case RCD:
	case DEVICE:
	case LD:
	case FMLD:
	case RP:
	case DSP:
	case USP:
		memcpy(&wd->prot_err, prot_err, sizeof(wd->prot_err));

		dvsec_start = (u8 *)(prot_err + 1);
		cap_start = dvsec_start + prot_err->dvsec_len;

		memcpy(&wd->ras_cap, cap_start, sizeof(wd->ras_cap));
		wd->severity = cper_severity_to_aer(severity);
		break;
	default:
		pr_err_ratelimited("CXL CPER invalid agent type: %d\n",
				   prot_err->agent_type);
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(cxl_cper_setup_prot_err_work_data);

// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2022 Intel Corporation. All rights reserved. */
#include <linux/libnvdimm.h>
#include <asm/unaligned.h>
#include <linux/module.h>
#include <linux/async.h>
#include <linux/slab.h>
#include "cxlmem.h"
#include "cxl.h"

static unsigned long cxl_pmem_get_security_flags(struct nvdimm *nvdimm,
						 enum nvdimm_passphrase_type ptype)
{
	struct cxl_nvdimm *cxl_nvd = nvdimm_provider_data(nvdimm);
	struct cxl_memdev *cxlmd = cxl_nvd->cxlmd;
	struct cxl_dev_state *cxlds = cxlmd->cxlds;
	unsigned long security_flags = 0;
	u32 sec_out;
	int rc;

	rc = cxl_mbox_send_cmd(cxlds, CXL_MBOX_OP_GET_SECURITY_STATE, NULL, 0,
			       &sec_out, sizeof(sec_out));
	if (rc < 0)
		return 0;

	if (ptype == NVDIMM_MASTER) {
		if (sec_out & CXL_PMEM_SEC_STATE_MASTER_PASS_SET)
			set_bit(NVDIMM_SECURITY_UNLOCKED, &security_flags);
		else
			set_bit(NVDIMM_SECURITY_DISABLED, &security_flags);
		if (sec_out & CXL_PMEM_SEC_STATE_MASTER_PLIMIT)
			set_bit(NVDIMM_SECURITY_FROZEN, &security_flags);
		return security_flags;
	}

	if (sec_out & CXL_PMEM_SEC_STATE_USER_PASS_SET) {
		if (sec_out & CXL_PMEM_SEC_STATE_FROZEN ||
		    sec_out & CXL_PMEM_SEC_STATE_USER_PLIMIT)
			set_bit(NVDIMM_SECURITY_FROZEN, &security_flags);

		if (sec_out & CXL_PMEM_SEC_STATE_LOCKED)
			set_bit(NVDIMM_SECURITY_LOCKED, &security_flags);
		else
			set_bit(NVDIMM_SECURITY_UNLOCKED, &security_flags);
	} else {
		set_bit(NVDIMM_SECURITY_DISABLED, &security_flags);
	}

	return security_flags;
}

static const struct nvdimm_security_ops __cxl_security_ops = {
	.get_flags = cxl_pmem_get_security_flags,
};

const struct nvdimm_security_ops *cxl_security_ops = &__cxl_security_ops;

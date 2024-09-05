// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2022 Intel Corporation. All rights reserved. */
#include <linux/libnvdimm.h>
#include <asm/unaligned.h>
#include <linux/module.h>
#include <linux/async.h>
#include <linux/slab.h>
#include <linux/memregion.h>
#include "cxlmem.h"
#include "cxl.h"

static unsigned long cxl_pmem_get_security_flags(struct nvdimm *nvdimm,
						 enum nvdimm_passphrase_type ptype)
{
	struct cxl_nvdimm *cxl_nvd = nvdimm_provider_data(nvdimm);
	struct cxl_memdev *cxlmd = cxl_nvd->cxlmd;
	struct cxl_mailbox *cxl_mbox = &cxlmd->cxlds->cxl_mbox;
	struct cxl_memdev_state *mds = to_cxl_memdev_state(cxlmd->cxlds);
	unsigned long security_flags = 0;
	struct cxl_get_security_output {
		__le32 flags;
	} out;
	struct cxl_mbox_cmd mbox_cmd;
	u32 sec_out;
	int rc;

	mbox_cmd = (struct cxl_mbox_cmd) {
		.opcode = CXL_MBOX_OP_GET_SECURITY_STATE,
		.size_out = sizeof(out),
		.payload_out = &out,
	};

	rc = cxl_internal_send_cmd(cxl_mbox, &mbox_cmd);
	if (rc < 0)
		return 0;

	sec_out = le32_to_cpu(out.flags);
	/* cache security state */
	mds->security.state = sec_out;

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

static int cxl_pmem_security_change_key(struct nvdimm *nvdimm,
					const struct nvdimm_key_data *old_data,
					const struct nvdimm_key_data *new_data,
					enum nvdimm_passphrase_type ptype)
{
	struct cxl_nvdimm *cxl_nvd = nvdimm_provider_data(nvdimm);
	struct cxl_memdev *cxlmd = cxl_nvd->cxlmd;
	struct cxl_mailbox *cxl_mbox = &cxlmd->cxlds->cxl_mbox;
	struct cxl_mbox_cmd mbox_cmd;
	struct cxl_set_pass set_pass;

	set_pass = (struct cxl_set_pass) {
		.type = ptype == NVDIMM_MASTER ? CXL_PMEM_SEC_PASS_MASTER :
						 CXL_PMEM_SEC_PASS_USER,
	};
	memcpy(set_pass.old_pass, old_data->data, NVDIMM_PASSPHRASE_LEN);
	memcpy(set_pass.new_pass, new_data->data, NVDIMM_PASSPHRASE_LEN);

	mbox_cmd = (struct cxl_mbox_cmd) {
		.opcode = CXL_MBOX_OP_SET_PASSPHRASE,
		.size_in = sizeof(set_pass),
		.payload_in = &set_pass,
	};

	return cxl_internal_send_cmd(cxl_mbox, &mbox_cmd);
}

static int __cxl_pmem_security_disable(struct nvdimm *nvdimm,
				       const struct nvdimm_key_data *key_data,
				       enum nvdimm_passphrase_type ptype)
{
	struct cxl_nvdimm *cxl_nvd = nvdimm_provider_data(nvdimm);
	struct cxl_memdev *cxlmd = cxl_nvd->cxlmd;
	struct cxl_mailbox *cxl_mbox = &cxlmd->cxlds->cxl_mbox;
	struct cxl_disable_pass dis_pass;
	struct cxl_mbox_cmd mbox_cmd;

	dis_pass = (struct cxl_disable_pass) {
		.type = ptype == NVDIMM_MASTER ? CXL_PMEM_SEC_PASS_MASTER :
						 CXL_PMEM_SEC_PASS_USER,
	};
	memcpy(dis_pass.pass, key_data->data, NVDIMM_PASSPHRASE_LEN);

	mbox_cmd = (struct cxl_mbox_cmd) {
		.opcode = CXL_MBOX_OP_DISABLE_PASSPHRASE,
		.size_in = sizeof(dis_pass),
		.payload_in = &dis_pass,
	};

	return cxl_internal_send_cmd(cxl_mbox, &mbox_cmd);
}

static int cxl_pmem_security_disable(struct nvdimm *nvdimm,
				     const struct nvdimm_key_data *key_data)
{
	return __cxl_pmem_security_disable(nvdimm, key_data, NVDIMM_USER);
}

static int cxl_pmem_security_disable_master(struct nvdimm *nvdimm,
					    const struct nvdimm_key_data *key_data)
{
	return __cxl_pmem_security_disable(nvdimm, key_data, NVDIMM_MASTER);
}

static int cxl_pmem_security_freeze(struct nvdimm *nvdimm)
{
	struct cxl_nvdimm *cxl_nvd = nvdimm_provider_data(nvdimm);
	struct cxl_memdev *cxlmd = cxl_nvd->cxlmd;
	struct cxl_mailbox *cxl_mbox = &cxlmd->cxlds->cxl_mbox;
	struct cxl_mbox_cmd mbox_cmd = {
		.opcode = CXL_MBOX_OP_FREEZE_SECURITY,
	};

	return cxl_internal_send_cmd(cxl_mbox, &mbox_cmd);
}

static int cxl_pmem_security_unlock(struct nvdimm *nvdimm,
				    const struct nvdimm_key_data *key_data)
{
	struct cxl_nvdimm *cxl_nvd = nvdimm_provider_data(nvdimm);
	struct cxl_memdev *cxlmd = cxl_nvd->cxlmd;
	struct cxl_mailbox *cxl_mbox = &cxlmd->cxlds->cxl_mbox;
	u8 pass[NVDIMM_PASSPHRASE_LEN];
	struct cxl_mbox_cmd mbox_cmd;
	int rc;

	memcpy(pass, key_data->data, NVDIMM_PASSPHRASE_LEN);
	mbox_cmd = (struct cxl_mbox_cmd) {
		.opcode = CXL_MBOX_OP_UNLOCK,
		.size_in = NVDIMM_PASSPHRASE_LEN,
		.payload_in = pass,
	};

	rc = cxl_internal_send_cmd(cxl_mbox, &mbox_cmd);
	if (rc < 0)
		return rc;

	return 0;
}

static int cxl_pmem_security_passphrase_erase(struct nvdimm *nvdimm,
					      const struct nvdimm_key_data *key,
					      enum nvdimm_passphrase_type ptype)
{
	struct cxl_nvdimm *cxl_nvd = nvdimm_provider_data(nvdimm);
	struct cxl_memdev *cxlmd = cxl_nvd->cxlmd;
	struct cxl_mailbox *cxl_mbox = &cxlmd->cxlds->cxl_mbox;
	struct cxl_mbox_cmd mbox_cmd;
	struct cxl_pass_erase erase;
	int rc;

	erase = (struct cxl_pass_erase) {
		.type = ptype == NVDIMM_MASTER ? CXL_PMEM_SEC_PASS_MASTER :
						 CXL_PMEM_SEC_PASS_USER,
	};
	memcpy(erase.pass, key->data, NVDIMM_PASSPHRASE_LEN);
	mbox_cmd = (struct cxl_mbox_cmd) {
		.opcode = CXL_MBOX_OP_PASSPHRASE_SECURE_ERASE,
		.size_in = sizeof(erase),
		.payload_in = &erase,
	};

	rc = cxl_internal_send_cmd(cxl_mbox, &mbox_cmd);
	if (rc < 0)
		return rc;

	return 0;
}

static const struct nvdimm_security_ops __cxl_security_ops = {
	.get_flags = cxl_pmem_get_security_flags,
	.change_key = cxl_pmem_security_change_key,
	.disable = cxl_pmem_security_disable,
	.freeze = cxl_pmem_security_freeze,
	.unlock = cxl_pmem_security_unlock,
	.erase = cxl_pmem_security_passphrase_erase,
	.disable_master = cxl_pmem_security_disable_master,
};

const struct nvdimm_security_ops *cxl_security_ops = &__cxl_security_ops;

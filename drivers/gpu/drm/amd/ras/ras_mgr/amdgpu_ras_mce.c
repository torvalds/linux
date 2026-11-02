// SPDX-License-Identifier: MIT
/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "amdgpu.h"
#include "amdgpu_ras_mgr.h"
#include "amdgpu_ras_mce.h"

#ifdef CONFIG_X86_MCE_AMD
#include <asm/mce.h>

static int amdgpu_sys_mce_notifier(struct notifier_block *nb, unsigned long val, void *data);
typedef int (*mce_notifier)(struct amdgpu_device *dev,
			unsigned int id, unsigned long val, void *data);
struct ras_mce_dev {
	void *dev;
	mce_notifier dev_notifier;
};

struct ras_mce_mgr {
	struct ras_mce_dev devs[MAX_GPU_INSTANCE];
	atomic_t dev_count;
	atomic_t ref_count;
	struct notifier_block nb;
	mce_notifier notifier;
};

struct ras_mce_mgr  g_ras_mce_mgr = {
	.nb = {
		.notifier_call = amdgpu_sys_mce_notifier,
		.priority = MCE_PRIO_UC,
	},
};

static int amdgpu_sys_mce_notifier(struct notifier_block *nb, unsigned long val, void *data)
{
	struct ras_mce_mgr *mce_mgr = container_of(nb, struct ras_mce_mgr, nb);
	struct ras_mce_dev *mce_dev;
	int i;

	if (!data)
		return NOTIFY_DONE;

	for (i = 0; i < atomic_read(&mce_mgr->dev_count); i++) {
		mce_dev = &mce_mgr->devs[i];
		if (mce_dev->dev && mce_dev->dev_notifier)
			mce_dev->dev_notifier(mce_dev->dev, i, val, data);
	}

	return NOTIFY_DONE;
}

static int amdgpu_ras_register_mce_notifier(struct amdgpu_device *adev, mce_notifier notifier)
{
	struct ras_mce_mgr *mce_mgr = &g_ras_mce_mgr;
	struct ras_mce_dev *mce_dev = NULL;
	int idx, i;

	if (!adev || !notifier)
		return -EINVAL;

	if (atomic_read(&mce_mgr->dev_count) >= ARRAY_SIZE(mce_mgr->devs))
		return -ENOENT;

	for (i = 0; i < atomic_read(&mce_mgr->dev_count); i++) {
		if (mce_mgr->devs[i].dev == adev)
			return -EEXIST;
	}

	idx = atomic_fetch_inc(&mce_mgr->dev_count);
	mce_dev = &mce_mgr->devs[idx];
	mce_dev->dev = adev;
	mce_dev->dev_notifier = notifier;
	atomic_inc(&mce_mgr->ref_count);

	if (!idx)
		mce_register_decode_chain(&mce_mgr->nb);

	return 0;
}

static int amdgpu_ras_unregister_mce_notifier(struct amdgpu_device *adev)
{
	struct ras_mce_mgr *mce_mgr = &g_ras_mce_mgr;
	struct ras_mce_dev *mce_dev;
	int i;

	if (!adev)
		return -EINVAL;

	for (i = 0; i < atomic_read(&mce_mgr->dev_count); i++) {
		mce_dev = &mce_mgr->devs[i];
		if (mce_dev->dev == adev) {
			mce_dev->dev = NULL;
			mce_dev->dev_notifier = NULL;
			atomic_dec(&mce_mgr->ref_count);
			break;
		}
	}

	if (atomic_read(&mce_mgr->ref_count) <= 0) {
		atomic_set(&mce_mgr->dev_count, 0);
		mce_unregister_decode_chain(&mce_mgr->nb);
	}

	return 0;
}

static void __fill_mce_to_aca_bank(struct amdgpu_device *adev,
	enum mce_bank_type bank_type, struct mce *m, struct aca_bank_reg *aca_bank)
{
	aca_bank->timestamp = m->time;
	aca_bank->bank_type = bank_type;
	aca_bank->ecc_type = RAS_ERR_TYPE__MCE;
	aca_bank->apic_id = m->apicid;
	aca_bank->bank = m->bank;
	aca_bank->regs[ACA_REG_IDX__STATUS] = m->status;
	aca_bank->regs[ACA_REG_IDX__ADDR] = m->addr;
	aca_bank->regs[ACA_REG_IDX__MISC0] = m->misc;
	aca_bank->regs[ACA_REG_IDX__IPID] = m->ipid;
	aca_bank->regs[ACA_REG_IDX__SYND] = m->synd;
}

static int amdgpu_ras_mce_notifier_v1(struct amdgpu_device *adev, unsigned int id, struct mce *m)
{
	struct amdgpu_ras_mgr *ras_mgr = amdgpu_ras_mgr_get_context(adev);
	struct aca_bank_reg aca_bank = {0};
	struct aca_bank_ecc err = {0};

	/*
	 * If the error was generated in UMC_V2, which belongs to GPU UMCs,
	 * and error occurred in DramECC (Extended error code = 0) then only
	 * process the error, else bail out.
	 */
	#ifdef HAVE_SMCA_UMC_V2
	if (!((smca_get_bank_type(m->extcpu, m->bank) == SMCA_UMC_V2) &&
			(XEC(m->status, 0x3f) == 0x0)))
		return 0;
	#endif

	if (!adev->smuio.funcs || !adev->smuio.funcs->get_socket_id)
		return 0;

	__fill_mce_to_aca_bank(adev, MCE_BANK_TYPE_GPU, m, &aca_bank);

	if (ras_aca_parse_bank(ras_mgr->ras_core, &aca_bank, &err)) {
		RAS_DEV_ERR(adev,
			"Unrecognized gpu mce: bank:%u IPID:0x%llx"
			" STATUS:0x%llx ADDR:0x%llx SYND:0x%llx\n",
			m->bank, m->ipid, m->status, m->addr, m->synd);
		return -EINVAL;
	}

	/* GPU device only record bank data that matches its own socket id.*/
	if (adev->smuio.funcs->get_socket_id(adev) != err.bank_info.socket_id)
		return 0;

	return ras_mce_add_aca_bank(ras_mgr->ras_core, &aca_bank);
}

static int amdgpu_ras_mce_notifier_v5(struct amdgpu_device *adev, unsigned int id, struct mce *m)
{
	struct amdgpu_ras_mgr *ras_mgr = amdgpu_ras_mgr_get_context(adev);
	struct aca_bank_reg aca_bank = {0};
	struct aca_bank_ecc err = {0};
	enum mce_bank_type bank_type;

	if (!adev->smuio.funcs || !adev->smuio.funcs->get_socket_id) {
		RAS_DEV_WARN(adev, "No interface to obtain current device socket ID!\n");
		return 0;
	}

	if (ras_mce_check_bank(ras_mgr->ras_core, MCE_BANK_TYPE_GPU, m->bank)) {
		bank_type = MCE_BANK_TYPE_GPU;
	} else if (ras_mce_check_bank(ras_mgr->ras_core, MCE_BANK_TYPE_CPU, m->bank)) {
		/* For CPU bank, only the first registered gpu device needs to record bank */
		if (id)
			return 0;

		bank_type = MCE_BANK_TYPE_CPU;
	} else {
		RAS_DEV_WARN(adev, "Unsupported mce bank: %u\n",  m->bank);
		return 0;
	}

	__fill_mce_to_aca_bank(adev, bank_type, m, &aca_bank);

	if (bank_type == MCE_BANK_TYPE_GPU) {
		if (ras_aca_parse_bank(ras_mgr->ras_core, &aca_bank, &err)) {
			RAS_DEV_ERR(adev,
				"Unrecognized gpu mce: bank:%u IPID:0x%llx"
				" STATUS:0x%llx ADDR:0x%llx SYND:0x%llx\n",
				m->bank, m->ipid, m->status, m->addr, m->synd);
			return -EINVAL;
		}

		/* GPU device only record bank data that matches its own socket id.*/
		if (adev->smuio.funcs->get_socket_id(adev) != err.bank_info.socket_id)
			return 0;
	}

	return ras_mce_add_aca_bank(ras_mgr->ras_core, &aca_bank);
}

static int amdgpu_ras_mce_notifier(struct amdgpu_device *adev,
			unsigned int id, unsigned long val, void *data)
{
	struct amdgpu_ras_mgr *ras_mgr = amdgpu_ras_mgr_get_context(adev);
	u32 aca_ip_version = 0;

	if (!data || !ras_mgr)
		return 0;

	if (ras_core_get_ip_version(ras_mgr->ras_core,
				RAS_UNIT_ID_ACA, &aca_ip_version))
		return 0;

	switch (aca_ip_version) {
	case IP_VERSION(1, 0, 0):
		return amdgpu_ras_mce_notifier_v1(adev, id, data);
	case IP_VERSION(5, 0, 0):
		return amdgpu_ras_mce_notifier_v5(adev, id, data);
	default:
		RAS_DEV_WARN(adev, "Invalid aca ip version:0x%x\n", aca_ip_version);
		break;
	}

	return 0;
}
#endif

int amdgpu_ras_mce_hw_init(struct amdgpu_device *adev)
{
	return 0;
}

int amdgpu_ras_mce_hw_fini(struct amdgpu_device *adev)
{
	return 0;
}

int amdgpu_ras_mce_sw_init(struct amdgpu_device *adev)
{
#ifdef CONFIG_X86_MCE_AMD
	amdgpu_ras_register_mce_notifier(adev, amdgpu_ras_mce_notifier);
#endif
	return 0;
}

int amdgpu_ras_mce_sw_fini(struct amdgpu_device *adev)
{
#ifdef CONFIG_X86_MCE_AMD
	amdgpu_ras_unregister_mce_notifier(adev);
#endif
	return 0;
}

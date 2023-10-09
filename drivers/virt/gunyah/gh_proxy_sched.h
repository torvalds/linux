/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef __GH_PROXY_SCHED_H
#define __GH_PROXY_SCHED_H

#include <linux/err.h>
#include <linux/types.h>

#include <linux/gunyah/hcall_common.h>
#include <linux/gunyah/gh_common.h>
#include <asm/gunyah/hcall.h>

#define WATCHDOG_MANAGE_OP_FREEZE		0
#define WATCHDOG_MANAGE_OP_FREEZE_AND_RESET	1
#define WATCHDOG_MANAGE_OP_UNFREEZE		2

struct gh_hcall_vcpu_run_resp {
	int ret;
	uint64_t vcpu_state;
	uint64_t vcpu_suspend_state;
	uint64_t state_data_0;
	uint64_t state_data_1;
	uint64_t state_data_2;
};

static inline int gh_hcall_wdog_manage(gh_capid_t wdog_capid, u16 operation)
{
	int ret;
	struct gh_hcall_resp _resp = {0};

	ret = _gh_hcall(0x6063, (struct gh_hcall_args){ wdog_capid, operation }, &_resp);

	return ret;
}

static inline int gh_hcall_vcpu_run(gh_capid_t vcpu_capid, uint64_t resume_data_0,
					uint64_t resume_data_1, uint64_t resume_data_2,
					struct gh_hcall_vcpu_run_resp *resp)
{
	int ret;
	struct gh_hcall_resp _resp = {0};

	ret = _gh_hcall(0x6065,
			(struct gh_hcall_args){ vcpu_capid, resume_data_0,
						resume_data_1, resume_data_2, 0 }, &_resp);

	resp->ret = ret;
	resp->vcpu_state = _resp.resp1;
	resp->vcpu_suspend_state = _resp.resp2;
	resp->state_data_0 = _resp.resp3;
	resp->state_data_1 = _resp.resp4;
	resp->state_data_2 = _resp.resp5;

	return ret;
}

static inline int gh_hcall_vpm_group_get_state(gh_capid_t vpmg_capid,
						uint64_t *vpmg_state)
{
	int ret;
	struct gh_hcall_resp _resp = {0};

	ret = _gh_hcall(0x6045,
			(struct gh_hcall_args){ vpmg_capid, 0 },
			&_resp);
	*vpmg_state = _resp.resp1;

	return ret;
}

/*
 * proxy scheduler APIs called by gunyah driver
 */
#if IS_ENABLED(CONFIG_GH_PROXY_SCHED)
int gh_proxy_sched_init(void);

void gh_proxy_sched_exit(void);

int gh_get_nr_vcpus(gh_vmid_t vmid);

bool gh_vm_supports_proxy_sched(gh_vmid_t vmid);

int gh_vcpu_create_wq(gh_vmid_t vmid, unsigned int vcpu_id);

int gh_vcpu_run(gh_vmid_t vmid, unsigned int vcpu_id, uint64_t resume_data_0,
			uint64_t resume_data_1, uint64_t resume_data_2,
			struct gh_hcall_vcpu_run_resp *resp);

void gh_wakeup_all_vcpus(gh_vmid_t vmid);
#else /* !CONFIG_GH_PROXY_SCHED */
static inline int gh_proxy_sched_init(void)
{
	return -EPERM;
}

static inline int gh_get_nr_vcpus(gh_vmid_t vmid)
{
	return 0;
}

bool gh_vm_supports_proxy_sched(gh_vmid_t vmid)
{
	return false;
}

static inline int gh_vcpu_run(gh_vmid_t vmid, unsigned int vcpu_id,
	uint64_t resume_data_0, uint64_t resume_data_1,
	uint64_t resume_data_2, struct gh_hcall_vcpu_run_resp *resp)
{
	return -EPERM;
}

int gh_vcpu_create_wq(gh_vmid_t vmid, unsigned int vcpu_id)
{
	return -EPERM;
}

static inline void gh_wakeup_all_vcpus(gh_vmid_t vmid) { }

static inline void gh_proxy_sched_exit(void) { }
#endif /* CONFIG_GH_PROXY_SCHED */
#endif

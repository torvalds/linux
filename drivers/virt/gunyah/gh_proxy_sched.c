// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2018 The Hafnium Authors.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * This driver is based on idea from Hafnium Hypervisor Linux Driver,
 * but modified to work with Gunyah Hypervisor as needed.
 *
 * Copyright (c) 2021-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt)	"gh_proxy_sched: " fmt

#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/sched/rt.h>
#include <linux/sched/task.h>
#include <linux/slab.h>
#include <uapi/linux/sched/types.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeup.h>
#include <linux/of.h>
#include <linux/mutex.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/sched/signal.h>
#include <linux/workqueue.h>
#include <linux/suspend.h>

#include <linux/gunyah/gh_rm_drv.h>
#include <linux/gunyah/gh_vm.h>
#include "gh_proxy_sched.h"

#define CREATE_TRACE_POINTS
#include "gh_proxy_sched_trace.h"

#define GH_MAX_VMS 5
#define GH_MAX_VCPUS_PER_VM 8
#define GH_MAX_SYSTEM_VCPUS (GH_MAX_VMS * GH_MAX_VCPUS_PER_VM)

/* VCPU is ready to run */
#define GH_VCPU_STATE_READY		0
/* VCPU is sleeping until an interrupt arrives */
#define GH_VCPU_STATE_EXPECTS_WAKEUP	1
/* VCPU is powered off */
#define GH_VCPU_STATE_POWERED_OFF	2
/* VCPU is blocked in EL2 for an unspecified reason */
#define GH_VCPU_STATE_BLOCKED		3

#define GH_VCPU_SUSPEND_STATE_STANDBY	0
#define GH_VCPU_SUSPEND_STATE_POWERDOWN	1

#define SVM_STATE_RUNNING		1
#define SVM_STATE_SYSTEM_SUSPENDED	3

struct gh_proxy_vcpu {
	struct gh_proxy_vm *vm;
	gh_capid_t cap_id;
	gh_label_t idx;
	bool abort_sleep;
	bool wdog_frozen;
	struct task_struct *task;
	int virq;
	char irq_name[32];
	char ws_name[32];
	wait_queue_head_t wait_queue;
	struct wakeup_source *ws;
	bool workqueue_mode;
	struct work_struct work;
	struct notifier_block suspend_nb;
};

struct gh_proxy_vm {
	gh_vmid_t id;
	int vcpu_count;
	struct gh_proxy_vcpu vcpu[GH_MAX_VCPUS_PER_VM];
	bool is_vcpu_info_populated;
	bool is_active;

	gh_capid_t wdog_cap_id;
	gh_capid_t vpmg_cap_id;
	int susp_res_irq;
	bool is_vpm_group_info_populated;
	struct workqueue_struct *vcpu_wq;
};

static struct gh_proxy_vm *gh_vms;
static int nr_vms;
static int nr_vcpus;
static bool init_done;
static DEFINE_MUTEX(gh_vm_mutex);
static DEFINE_SPINLOCK(gh_vm_lock);

/*
 * Wakes up the thread responsible for running the given vcpu.
 */
static inline void gh_vcpu_wake_up(struct gh_proxy_vcpu *vcpu)
{
	vcpu->abort_sleep = true;

	wake_up(&vcpu->wait_queue);
}

/*
 * Puts the current thread to sleep. The current thread must be responsible for
 * running the given vcpu.
 */
static inline void gh_vcpu_sleep(struct gh_proxy_vcpu *vcpu)
{
	if (!vcpu->abort_sleep && !signal_pending(current))
		wait_event_interruptible(vcpu->wait_queue, vcpu->abort_sleep);
}

static void gh_init_wait_queues(struct gh_proxy_vm *vm)
{
	gh_label_t j;

	for (j = 0; j < vm->vcpu_count; j++)
		init_waitqueue_head(&vm->vcpu[j].wait_queue);
}

static inline bool is_vm_supports_proxy(gh_vmid_t gh_vmid)
{
	gh_vmid_t vmid;

	if ((!ghd_rm_get_vmid(GH_TRUSTED_VM, &vmid) && vmid == gh_vmid) ||
			(!ghd_rm_get_vmid(GH_OEM_VM, &vmid) && vmid == gh_vmid))
		return true;

	return false;
}

static inline struct gh_proxy_vm *gh_get_vm(gh_vmid_t vmid)
{
	int i;
	struct gh_proxy_vm *vm = NULL;

	for (i = 0; i < GH_MAX_VMS; i++) {
		vm = &gh_vms[i];
		if (vmid == vm->id || vm->id == GH_VMID_INVAL)
			break;
	}

	return vm;
}

static inline struct gh_proxy_vcpu *gh_get_vcpu(struct gh_proxy_vm *vm, gh_capid_t cap_id)
{
	int i;
	struct gh_proxy_vcpu *vcpu = NULL;

	for (i = 0; i < vm->vcpu_count; i++) {
		if (vm->vcpu[i].cap_id == cap_id) {
			vcpu = &vm->vcpu[i];
			break;
		}
	}

	return vcpu;
}

static inline void gh_reset_vm(struct gh_proxy_vm *vm)
{
	int j;

	vm->id = GH_VMID_INVAL;
	vm->vcpu_count = 0;
	vm->is_vcpu_info_populated = false;
	vm->is_active = false;
	vm->susp_res_irq = U32_MAX;
	vm->is_vpm_group_info_populated = false;
	vm->vpmg_cap_id = GH_CAPID_INVAL;
	for (j = 0; j < GH_MAX_VCPUS_PER_VM; j++) {
		vm->vcpu[j].cap_id = GH_CAPID_INVAL;
		vm->vcpu[j].virq = U32_MAX;
		vm->vcpu[j].idx = U32_MAX;
		vm->vcpu[j].vm = NULL;
		vm->vcpu[j].abort_sleep = false;
		vm->vcpu[j].wdog_frozen = false;
		vm->vcpu[j].ws = NULL;
		strscpy(vm->vcpu[vm->vcpu_count].irq_name, "",
				sizeof(vm->vcpu[vm->vcpu_count].irq_name));
		strscpy(vm->vcpu[vm->vcpu_count].ws_name, "",
				sizeof(vm->vcpu[vm->vcpu_count].ws_name));
	}
}

static void gh_init_vms(void)
{
	struct gh_proxy_vm *vm;
	int i;

	for (i = 0; i < GH_MAX_VMS; i++) {
		vm = &gh_vms[i];
		gh_reset_vm(vm);
	}
}

static irqreturn_t gh_vcpu_irq_handler(int irq, void *data)
{
	struct gh_proxy_vcpu *vcpu;
	struct gh_proxy_vm *vm;

	spin_lock(&gh_vm_lock);
	vcpu = data;
	vm = vcpu->vm;
	if (!vcpu || !vcpu->vm || !vcpu->vm->is_vcpu_info_populated)
		goto unlock;

	trace_gh_vcpu_irq_handler(vcpu->vm->id, vcpu->idx);
	if (vcpu->workqueue_mode)
		queue_work(vm->vcpu_wq, &vcpu->work);
	else
		gh_vcpu_wake_up(vcpu);

unlock:
	spin_unlock(&gh_vm_lock);
	return IRQ_HANDLED;
}

static inline void gh_get_vcpu_prop_name(int vmid, int vcpu_num, char *name)
{
	char extrastr[12];

	scnprintf(extrastr, 12, "_%d_%d", vmid, vcpu_num);
	strlcat(name, extrastr, 32);
}

static int gh_wdog_manage(gh_vmid_t vmid, gh_capid_t cap_id, bool populate)
{
	struct gh_proxy_vm *vm;
	int ret = 0;

	if (!init_done) {
		pr_err("Driver probe failed\n");
		return -ENXIO;
	}

	if (!is_vm_supports_proxy(vmid)) {
		pr_info("Skip populating VCPU affinity info for VM=%d\n", vmid);
		return -EINVAL;
	}

	mutex_lock(&gh_vm_mutex);
	vm = gh_get_vm(vmid);
	if (!vm) {
		ret = -ENODEV;
		goto unlock;
	}

	if (populate)
		vm->wdog_cap_id = cap_id;
	else
		vm->wdog_cap_id = GH_CAPID_INVAL;

unlock:
	mutex_unlock(&gh_vm_mutex);
	return ret;
}

/*
 * Called when vm_status is STATUS_READY, multiple times before status
 * moves to STATUS_RUNNING
 */
static int gh_populate_vm_vcpu_info(gh_vmid_t vmid, gh_label_t cpu_idx,
					gh_capid_t cap_id, int virq_num)
{
	struct gh_proxy_vm *vm;
	int ret = 0;
	char *vcpu_irq_name;

	if (!init_done) {
		pr_err("Driver probe failed\n");
		ret = -ENXIO;
		goto out;
	}

	if (!is_vm_supports_proxy(vmid)) {
		pr_info("Skip populating VCPU affinity info for VM=%d\n", vmid);
		goto out;
	}

	if (nr_vcpus >= GH_MAX_SYSTEM_VCPUS) {
		pr_err("Exceeded max vcpus in the system %d\n", nr_vcpus);
		ret = -ENXIO;
		goto out;
	}

	if (!virq_num || virq_num == U32_MAX) {
		pr_err("Invalid VIRQ, proxy scheduling isn't supported\n");
		goto out;
	}

	mutex_lock(&gh_vm_mutex);
	vm = gh_get_vm(vmid);
	if (vm && !vm->is_vcpu_info_populated) {
		if (vm->vcpu_count >= GH_MAX_VCPUS_PER_VM) {
			pr_err("Exceeded max vcpus per VM %d\n", vm->vcpu_count);
			ret = -ENXIO;
			goto unlock;
		}

		strscpy(vm->vcpu[vm->vcpu_count].irq_name, "gh_vcpu_irq",
				sizeof(vm->vcpu[vm->vcpu_count].irq_name));
		gh_get_vcpu_prop_name(vmid, vm->vcpu_count,
				vm->vcpu[vm->vcpu_count].irq_name);
		ret = request_irq(virq_num, gh_vcpu_irq_handler, 0,
				  vm->vcpu[vm->vcpu_count].irq_name,
				  &vm->vcpu[vm->vcpu_count]);
		if (ret < 0) {
			pr_err("%s: IRQ registration failed ret=%d\n", __func__, ret);
			goto err_irq;
		}

		irq_set_irq_wake(virq_num, 1);

		strscpy(vm->vcpu[vm->vcpu_count].ws_name, "gh_vcpu_ws",
				sizeof(vm->vcpu[vm->vcpu_count].ws_name));
		gh_get_vcpu_prop_name(vmid, vm->vcpu_count,
				vm->vcpu[vm->vcpu_count].ws_name);
		vm->vcpu[vm->vcpu_count].ws = wakeup_source_register(NULL,
				vm->vcpu[vm->vcpu_count].ws_name);
		if (!vm->vcpu[vm->vcpu_count].ws) {
			pr_err("%s: Wakeup source creation failed\n", __func__);
			goto err_ws;
		}

		vm->id = vmid;
		vm->vcpu[vm->vcpu_count].cap_id = cap_id;
		vm->vcpu[vm->vcpu_count].virq = virq_num;
		vm->vcpu[vm->vcpu_count].idx = cpu_idx;
		vm->vcpu[vm->vcpu_count].vm = vm;
		vcpu_irq_name = vm->vcpu[vm->vcpu_count].irq_name;
		vm->vcpu_count++;

		nr_vcpus++;
		pr_info("vmid=%d cpu_index:%u vcpu_cap_id:%llu virq_num=%d irq_name=%s nr_vcpus:%d\n",
				vmid, cpu_idx, cap_id, virq_num, vcpu_irq_name, nr_vcpus);
	}
	goto unlock;

err_ws:
	strscpy(vm->vcpu[vm->vcpu_count].ws_name, "",
			sizeof(vm->vcpu[vm->vcpu_count].ws_name));
	free_irq(virq_num, &vm->vcpu[vm->vcpu_count]);
err_irq:
	strscpy(vm->vcpu[vm->vcpu_count].irq_name, "",
			sizeof(vm->vcpu[vm->vcpu_count].irq_name));
unlock:
	mutex_unlock(&gh_vm_mutex);
out:
	return ret;
}

static int gh_unpopulate_vm_vcpu_info(gh_vmid_t vmid, gh_label_t cpu_idx,
					gh_capid_t cap_id, int *irq)
{
	struct gh_proxy_vm *vm;
	struct gh_proxy_vcpu *vcpu;

	if (!init_done) {
		pr_err("Driver probe failed\n");
		return -ENXIO;
	}

	if (!is_vm_supports_proxy(vmid)) {
		pr_info("Skip unpopulating VCPU affinity info for VM=%d\n", vmid);
		goto out;
	}

	mutex_lock(&gh_vm_mutex);
	vm = gh_get_vm(vmid);
	if (vm && vm->is_vcpu_info_populated) {
		vcpu = gh_get_vcpu(vm, cap_id);
		if (vcpu) {
			*irq = vcpu->virq;
			free_irq(vcpu->virq, vcpu);
			vcpu->virq = U32_MAX;
			wakeup_source_unregister(vcpu->ws);

			if (nr_vcpus)
				nr_vcpus--;
		}
	}
	mutex_unlock(&gh_vm_mutex);

out:
	return 0;
}

static inline void gh_get_vpmg_cap_id(int irq, gh_capid_t *vpmg_cap_id)
{
	int i;
	struct gh_proxy_vm *vm;

	for (i = 0; i < GH_MAX_VMS; i++) {
		vm = &gh_vms[i];
		if (vm->susp_res_irq == irq)
			*vpmg_cap_id = vm->vpmg_cap_id;
	}
}

static irqreturn_t gh_susp_res_irq_handler(int irq, void *data)
{
	int err;
	uint64_t vpmg_state;
	gh_capid_t vpmg_cap_id;

	gh_get_vpmg_cap_id(irq, &vpmg_cap_id);
	err = gh_hcall_vpm_group_get_state(vpmg_cap_id, &vpmg_state);

	if (err != GH_ERROR_OK) {
		pr_err("Failed to get VPM Group state for cap_id=%llu err=%d\n",
			vpmg_cap_id, err);
		return IRQ_HANDLED;
	}

	if (vpmg_state == SVM_STATE_RUNNING)
		pr_debug("SVM is in running state\n");
	else if (vpmg_state == SVM_STATE_SYSTEM_SUSPENDED)
		pr_debug("SVM is in system suspend state\n");
	else
		pr_err("VPM Group state invalid/non-existent\n");

	trace_gh_susp_res_irq_handler(vpmg_state);

	return IRQ_HANDLED;
}

static int gh_populate_vm_vpm_grp_info(gh_vmid_t vmid, gh_capid_t cap_id, int virq_num)
{
	int ret = 0;
	struct gh_proxy_vm *vm;

	if (!init_done) {
		pr_err("%s: Driver probe failed\n", __func__);
		ret = -ENXIO;
		goto out;
	}

	if (!is_vm_supports_proxy(vmid)) {
		pr_info("Skip populating VPM GRP info for VM=%d\n", vmid);
		goto out;
	}

	if (virq_num < 0) {
		pr_err("%s: Invalid IRQ number\n", __func__);
		ret = -EINVAL;
		goto out;
	}

	mutex_lock(&gh_vm_mutex);
	vm = gh_get_vm(vmid);
	if (vm && !vm->is_vpm_group_info_populated) {
		ret = request_irq(virq_num, gh_susp_res_irq_handler, 0,
			"gh_susp_res_irq", NULL);
		if (ret < 0) {
			pr_err("%s: IRQ registration failed ret=%d\n", __func__, ret);
			goto unlock;
		}

		vm->vpmg_cap_id = cap_id;
		vm->susp_res_irq = virq_num;
		vm->is_vpm_group_info_populated = true;
	}

unlock:
	mutex_unlock(&gh_vm_mutex);
out:
	return ret;
}

static int gh_unpopulate_vm_vpm_grp_info(gh_vmid_t vmid, int *irq)
{
	struct gh_proxy_vm *vm;

	if (!init_done) {
		pr_err("%s: Driver probe failed\n", __func__);
		return -ENXIO;
	}

	if (!is_vm_supports_proxy(vmid)) {
		pr_info("Skip unpopulating VPM GRP info for VM=%d\n", vmid);
		goto out;
	}

	mutex_lock(&gh_vm_mutex);
	vm = gh_get_vm(vmid);
	if (vm && vm->is_vpm_group_info_populated) {
		*irq = vm->susp_res_irq;
		free_irq(vm->susp_res_irq, NULL);
		vm->susp_res_irq = U32_MAX;
		vm->is_vpm_group_info_populated = false;
	}
	mutex_unlock(&gh_vm_mutex);

out:
	return 0;
}

static void gh_populate_all_res_info(gh_vmid_t vmid, bool res_populated)
{
	struct gh_proxy_vm *vm;
	char workqueue_name[24];

	if (!init_done) {
		pr_err("%s: Driver probe failed\n", __func__);
		return;
	}

	if (!is_vm_supports_proxy(vmid)) {
		pr_info("Proxy Scheduling isn't supported for VM=%d\n", vmid);
		return;
	}

	if (nr_vms >= GH_MAX_VMS) {
		pr_err("Exceeded max VMs in the system %d\n", nr_vms);
		return;
	}

	mutex_lock(&gh_vm_mutex);
	vm = gh_get_vm(vmid);
	if (!vm)
		goto unlock;

	if (res_populated && !vm->is_vcpu_info_populated) {
		gh_init_wait_queues(vm);
		snprintf(workqueue_name, sizeof(workqueue_name), "vm%d_vcpu_wq",
			 vm->id);
		vm->vcpu_wq = create_freezable_workqueue(workqueue_name);
		nr_vms++;
		vm->is_vcpu_info_populated = true;
		vm->is_active = true;
	} else if (!res_populated && vm->is_vcpu_info_populated) {
		gh_reset_vm(vm);
		if (nr_vms)
			nr_vms--;
	}
unlock:
	mutex_unlock(&gh_vm_mutex);
}


int gh_get_nr_vcpus(gh_vmid_t vmid)
{
	struct gh_proxy_vm *vm;

	vm = gh_get_vm(vmid);
	if (vm && vm->is_vcpu_info_populated)
		return vm->vcpu_count;

	return 0;
}

/* Gets called from VM EXIT notification */
void gh_wakeup_all_vcpus(gh_vmid_t vmid)
{
	struct gh_proxy_vm *vm;
	int i;

	vm = gh_get_vm(vmid);
	if (vm && vm->is_active) {
		vm->is_active = false;

		for (i = 0; i < vm->vcpu_count; i++)
			gh_vcpu_wake_up(&vm->vcpu[i]);
	}
}

bool gh_vm_supports_proxy_sched(gh_vmid_t vmid)
{
	struct gh_proxy_vm *vm;

	vm = gh_get_vm(vmid);
	if (vm && vm->is_vcpu_info_populated && vm->vcpu_count)
		return true;

	return false;
}

int gh_poll_vcpu_run(gh_vmid_t vmid)
{
	struct gh_hcall_vcpu_run_resp resp;
	struct gh_proxy_vcpu *vcpu;
	struct gh_proxy_vm *vm;
	unsigned int vcpu_id;
	int poll_nr_vcpus;
	ktime_t start_ts, yield_ts;
	int ret = -EPERM;

	vm = gh_get_vm(vmid);
	if (!vm || !vm->is_active)
		return ret;

	poll_nr_vcpus = gh_get_nr_vcpus(vmid);
	if (poll_nr_vcpus < 0) {
		printk_deferred("Failed to get vcpu count for VM %d ret %d\n",
						vmid, nr_vcpus);
		ret = poll_nr_vcpus;
		return ret;
	}

	for (vcpu_id = 0; vcpu_id < poll_nr_vcpus; vcpu_id++) {
		if (vm->vcpu[vcpu_id].cap_id == GH_CAPID_INVAL)
			return -EPERM;

		vcpu = &vm->vcpu[vcpu_id];
		do {
			start_ts = ktime_get();
			ret = gh_hcall_vcpu_run(vcpu->cap_id, 0, 0, 0, &resp);
			yield_ts = ktime_get() - start_ts;
			trace_gh_hcall_vcpu_run(ret, vcpu->vm->id, vcpu_id, yield_ts,
						resp.vcpu_state, resp.vcpu_suspend_state);
			if (ret == GH_ERROR_OK) {
				if (resp.vcpu_state > GH_VCPU_STATE_BLOCKED)
					printk_deferred("Unknown VCPU STATE: state=%d VCPU=%u of VM=%d\n",
							resp.vcpu_state, vcpu_id, vmid);
				break;
			}
		} while (ret == GH_ERROR_RETRY);
	}

	return ret;
}
EXPORT_SYMBOL(gh_poll_vcpu_run);

void gh_vcpu_work_function(struct work_struct *work)
{
	struct gh_proxy_vcpu *vcpu =
		container_of(work, struct gh_proxy_vcpu, work);
	struct gh_proxy_vm *vm = vcpu->vm;
	uint64_t resume_data_0 = 0, resume_data_1 = 0, resume_data_2 = 0;
	struct gh_hcall_vcpu_run_resp resp;
	ktime_t start_ts, yield_ts;
	int ret;

	vcpu->abort_sleep = false;
	__pm_stay_awake(vcpu->ws);
	start_ts = ktime_get();
	preempt_disable();
	if (vcpu->wdog_frozen) {
		gh_hcall_wdog_manage(vm->wdog_cap_id,
					WATCHDOG_MANAGE_OP_UNFREEZE);
		vcpu->wdog_frozen = false;
	}
	ret = gh_hcall_vcpu_run(vcpu->cap_id, resume_data_0,
				resume_data_1, resume_data_2, &resp);
	if (ret == GH_ERROR_OK && resp.vcpu_state == GH_VCPU_STATE_READY) {
		gh_hcall_wdog_manage(vm->wdog_cap_id,
				     WATCHDOG_MANAGE_OP_FREEZE);
		vcpu->wdog_frozen = true;
	}
	preempt_enable();
	yield_ts = ktime_get() - start_ts;
	trace_gh_hcall_vcpu_run(ret, vcpu->vm->id, vcpu->idx, yield_ts,
				resp.vcpu_state,
				resp.vcpu_suspend_state);
	if (ret == GH_ERROR_OK) {
		switch (resp.vcpu_state) {
		/* VCPU is preempted by PVM interrupt. */
		case GH_VCPU_STATE_READY:
			queue_work(vm->vcpu_wq, &vcpu->work);
			break;

		/* VCPU in WFI or suspended/powered down. */
		case GH_VCPU_STATE_EXPECTS_WAKEUP:
			if (resp.vcpu_suspend_state)
				__pm_relax(vcpu->ws);
			if (!vcpu->abort_sleep)
				return;
			break;
		case GH_VCPU_STATE_POWERED_OFF:
			__pm_relax(vcpu->ws);
			/* once cpu is powered off, the work is done */
			if (!vcpu->abort_sleep)
				return;
			break;

		/* VCPU is blocked in EL2 for an unspecified reason */
		case GH_VCPU_STATE_BLOCKED:
			queue_work(vm->vcpu_wq, &vcpu->work);
			break;
		}
	}
}

int gh_vcpu_pm_notifier_call(struct notifier_block *nb, unsigned long action,
			     void *data)
{
	struct gh_proxy_vcpu *vcpu =
		container_of(nb, struct gh_proxy_vcpu, suspend_nb);
	struct gh_proxy_vm *vm = vcpu->vm;

	if (action == PM_SUSPEND_PREPARE) {
		if (!vcpu->wdog_frozen) {
			gh_hcall_wdog_manage(vm->wdog_cap_id,
					     WATCHDOG_MANAGE_OP_FREEZE);
			vcpu->wdog_frozen = true;
		}
	} else if (action == PM_POST_SUSPEND) {
		queue_work(vm->vcpu_wq, &vcpu->work);
	}

	return NOTIFY_OK;
}

int gh_vcpu_create_wq(gh_vmid_t vmid, unsigned int vcpu_id)
{
	struct gh_proxy_vm *vm;
	struct gh_proxy_vcpu *vcpu;

	vm = gh_get_vm(vmid);
	if (!vm || !vm->is_active)
		return -EINVAL;
	if (vm->vcpu[vcpu_id].cap_id == GH_CAPID_INVAL)
		return -EINVAL;

	vcpu = &vm->vcpu[vcpu_id];

	INIT_WORK(&vcpu->work, gh_vcpu_work_function);
	vcpu->workqueue_mode = true;

	vcpu->suspend_nb.notifier_call = gh_vcpu_pm_notifier_call;
	register_pm_notifier(&vcpu->suspend_nb);

	/* schedule once incase we miss any interrupt */
	schedule_work(&vcpu->work);
	queue_work(vm->vcpu_wq, &vcpu->work);

	return 0;
}

int gh_vcpu_run(gh_vmid_t vmid, unsigned int vcpu_id, uint64_t resume_data_0,
		uint64_t resume_data_1, uint64_t resume_data_2, struct gh_hcall_vcpu_run_resp *resp)
{
	struct gh_proxy_vcpu *vcpu;
	struct gh_proxy_vm *vm;
	int ret;
	ktime_t start_ts, yield_ts;

	vm = gh_get_vm(vmid);
	if (!vm || !vm->is_active)
		return -EPERM;

	if (vm->vcpu[vcpu_id].cap_id == GH_CAPID_INVAL)
		return -EPERM;

	vcpu = &vm->vcpu[vcpu_id];

	do {
		/*
		 * We're about to run the vcpu, so we can reset the abort-sleep flag.
		 */
		vcpu->abort_sleep = false;
		__pm_stay_awake(vcpu->ws);

		start_ts = ktime_get();
		/* Call into Gunyah to run vcpu. */
		preempt_disable();
		if (vcpu->wdog_frozen) {
			gh_hcall_wdog_manage(vm->wdog_cap_id, WATCHDOG_MANAGE_OP_UNFREEZE);
			vcpu->wdog_frozen = false;
		}
		ret = gh_hcall_vcpu_run(vcpu->cap_id, resume_data_0,
					resume_data_1, resume_data_2, resp);
		if (ret == GH_ERROR_OK && resp->vcpu_state == GH_VCPU_STATE_READY) {
			if (need_resched()) {
				gh_hcall_wdog_manage(vm->wdog_cap_id,
						WATCHDOG_MANAGE_OP_FREEZE);
				vcpu->wdog_frozen = true;
			}
		}
		preempt_enable();
		yield_ts = ktime_get() - start_ts;
		trace_gh_hcall_vcpu_run(ret, vcpu->vm->id, vcpu_id, yield_ts,
					resp->vcpu_state, resp->vcpu_suspend_state);

		if (ret == GH_ERROR_OK) {
			switch (resp->vcpu_state) {
			/*
			 * The caller's hypervisor timeslice ended, or the caller received an interrupt.
			 * The caller should retry after handling any pending interrupts.
			 */
			case GH_VCPU_STATE_READY:
				if (need_resched())
					schedule();
				break;

			/*
			 * The VCPU is waiting to receive an interrupt; for example, it may have executed a WFI instruction,
			 * or made a firmware call requesting entry into a low-power state.
			 */
			case GH_VCPU_STATE_EXPECTS_WAKEUP:
				/*
				 * VCPU requested a firmware call requesting
				 * entry into a low-power state.
				 * Release wake lock for non C1 states
				 */
				if (resp->vcpu_suspend_state)
					__pm_relax(vcpu->ws);
				gh_vcpu_sleep(vcpu);
				break;

			/*
			 * The VCPU has not yet been started by calling vcpu_poweron, or has stopped itself by calling vcpu_poweroff,
			 * or has been terminated due to a reset request from another VM.
			 */
			case GH_VCPU_STATE_POWERED_OFF:
				__pm_relax(vcpu->ws);
				gh_vcpu_sleep(vcpu);
				break;

			/*
			 * The VCPU is temporarily unable to run due to a hypervisor operation.
			 * This may include a hypercall made by the VCPU that transiently blocks it,
			 * or by an incomplete migration from another physical CPU. The caller should
			 * retry after yielding to the calling VM's scheduler.
			 */
			case GH_VCPU_STATE_BLOCKED:
				schedule();
				break;

			/* Unknown VCPU state. */
			default:
				pr_err("Unknown VCPU STATE: state=%d VCPU=%u of VM=%d state_data_0=0x%llx state_data_1=0x%llx state_data_2=0x%llx\n",
					resp->vcpu_state, vcpu_id, vcpu->vm->id,
					resp->state_data_0, resp->state_data_1, resp->state_data_2);
				schedule();
				break;
			}
		} else if (ret == GH_ERROR_RETRY) {
			schedule();
		}

		if (signal_pending(current)) {
			if (!vcpu->wdog_frozen) {
				gh_hcall_wdog_manage(vm->wdog_cap_id,
						WATCHDOG_MANAGE_OP_FREEZE);
				vcpu->wdog_frozen = true;
			}
			ret = -ERESTARTSYS;
			break;
		}
	} while ((ret == GH_ERROR_OK || ret == GH_ERROR_RETRY) && vm->is_active);

	if (ret != -ERESTARTSYS)
		ret = gh_error_remap(ret);

	return ret;
}

static int gh_proxy_sched_reg_rm_cbs(void)
{
	int ret = -EINVAL;

	ret = gh_rm_set_wdog_manage_cb(&gh_wdog_manage);
	if (ret) {
		pr_err("fail to set the WDOG resource callback\n");
		return ret;
	}

	ret = gh_rm_set_vcpu_affinity_cb(&gh_populate_vm_vcpu_info);
	if (ret) {
		pr_err("fail to set the VM VCPU populate callback\n");
		return ret;
	}

	ret = gh_rm_reset_vcpu_affinity_cb(&gh_unpopulate_vm_vcpu_info);
	if (ret) {
		pr_err("fail to set the VM VCPU unpopulate callback\n");
		return ret;
	}

	ret = gh_rm_set_vpm_grp_cb(&gh_populate_vm_vpm_grp_info);
	if (ret) {
		pr_err("fail to set the VM VPM GRP populate callback\n");
		return ret;
	}

	ret = gh_rm_reset_vpm_grp_cb(&gh_unpopulate_vm_vpm_grp_info);
	if (ret) {
		pr_err("fail to set the VM VPM GRP unpopulate callback\n");
		return ret;
	}

	ret = gh_rm_all_res_populated_cb(&gh_populate_all_res_info);
	if (ret) {
		pr_err("fail to set the all res populate callback\n");
		return ret;
	}

	return 0;
}

int gh_proxy_sched_init(void)
{
	int ret;

	gh_vms = kcalloc(GH_MAX_VMS, sizeof(struct gh_proxy_vm), GFP_KERNEL);
	if (!gh_vms) {
		ret = -ENOMEM;
		goto err;
	}

	ret = gh_proxy_sched_reg_rm_cbs();
	if (ret)
		goto free_gh_vms;

	gh_init_vms();

	init_done = true;
	return 0;

free_gh_vms:
	kfree(gh_vms);
err:
	return ret;
}

void gh_proxy_sched_exit(void)
{
	kfree(gh_vms);
}

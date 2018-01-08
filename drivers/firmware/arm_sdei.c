// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2017 Arm Ltd.
#define pr_fmt(fmt) "sdei: " fmt

#include <linux/acpi.h>
#include <linux/arm_sdei.h>
#include <linux/arm-smccc.h>
#include <linux/bitops.h>
#include <linux/compiler.h>
#include <linux/errno.h>
#include <linux/hardirq.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/kvm_host.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/percpu.h>
#include <linux/platform_device.h>
#include <linux/ptrace.h>
#include <linux/preempt.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>

/*
 * The call to use to reach the firmware.
 */
static asmlinkage void (*sdei_firmware_call)(unsigned long function_id,
		      unsigned long arg0, unsigned long arg1,
		      unsigned long arg2, unsigned long arg3,
		      unsigned long arg4, struct arm_smccc_res *res);

/* entry point from firmware to arch asm code */
static unsigned long sdei_entry_point;

struct sdei_event {
	struct list_head	list;
	u32			event_num;
	u8			type;
	u8			priority;

	/* This pointer is handed to firmware as the event argument. */
	struct sdei_registered_event *registered;
};

/* Take the mutex for any API call or modification. Take the mutex first. */
static DEFINE_MUTEX(sdei_events_lock);

/* and then hold this when modifying the list */
static DEFINE_SPINLOCK(sdei_list_lock);
static LIST_HEAD(sdei_list);

static int sdei_to_linux_errno(unsigned long sdei_err)
{
	switch (sdei_err) {
	case SDEI_NOT_SUPPORTED:
		return -EOPNOTSUPP;
	case SDEI_INVALID_PARAMETERS:
		return -EINVAL;
	case SDEI_DENIED:
		return -EPERM;
	case SDEI_PENDING:
		return -EINPROGRESS;
	case SDEI_OUT_OF_RESOURCE:
		return -ENOMEM;
	}

	/* Not an error value ... */
	return sdei_err;
}

/*
 * If x0 is any of these values, then the call failed, use sdei_to_linux_errno()
 * to translate.
 */
static int sdei_is_err(struct arm_smccc_res *res)
{
	switch (res->a0) {
	case SDEI_NOT_SUPPORTED:
	case SDEI_INVALID_PARAMETERS:
	case SDEI_DENIED:
	case SDEI_PENDING:
	case SDEI_OUT_OF_RESOURCE:
		return true;
	}

	return false;
}

static int invoke_sdei_fn(unsigned long function_id, unsigned long arg0,
			  unsigned long arg1, unsigned long arg2,
			  unsigned long arg3, unsigned long arg4,
			  u64 *result)
{
	int err = 0;
	struct arm_smccc_res res;

	if (sdei_firmware_call) {
		sdei_firmware_call(function_id, arg0, arg1, arg2, arg3, arg4,
				   &res);
		if (sdei_is_err(&res))
			err = sdei_to_linux_errno(res.a0);
	} else {
		/*
		 * !sdei_firmware_call means we failed to probe or called
		 * sdei_mark_interface_broken(). -EIO is not an error returned
		 * by sdei_to_linux_errno() and is used to suppress messages
		 * from this driver.
		 */
		err = -EIO;
		res.a0 = SDEI_NOT_SUPPORTED;
	}

	if (result)
		*result = res.a0;

	return err;
}

static struct sdei_event *sdei_event_find(u32 event_num)
{
	struct sdei_event *e, *found = NULL;

	lockdep_assert_held(&sdei_events_lock);

	spin_lock(&sdei_list_lock);
	list_for_each_entry(e, &sdei_list, list) {
		if (e->event_num == event_num) {
			found = e;
			break;
		}
	}
	spin_unlock(&sdei_list_lock);

	return found;
}

int sdei_api_event_context(u32 query, u64 *result)
{
	return invoke_sdei_fn(SDEI_1_0_FN_SDEI_EVENT_CONTEXT, query, 0, 0, 0, 0,
			      result);
}
NOKPROBE_SYMBOL(sdei_api_event_context);

static int sdei_api_event_get_info(u32 event, u32 info, u64 *result)
{
	return invoke_sdei_fn(SDEI_1_0_FN_SDEI_EVENT_GET_INFO, event, info, 0,
			      0, 0, result);
}

static struct sdei_event *sdei_event_create(u32 event_num,
					    sdei_event_callback *cb,
					    void *cb_arg)
{
	int err;
	u64 result;
	struct sdei_event *event;
	struct sdei_registered_event *reg;

	lockdep_assert_held(&sdei_events_lock);

	event = kzalloc(sizeof(*event), GFP_KERNEL);
	if (!event)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&event->list);
	event->event_num = event_num;

	err = sdei_api_event_get_info(event_num, SDEI_EVENT_INFO_EV_PRIORITY,
				      &result);
	if (err) {
		kfree(event);
		return ERR_PTR(err);
	}
	event->priority = result;

	err = sdei_api_event_get_info(event_num, SDEI_EVENT_INFO_EV_TYPE,
				      &result);
	if (err) {
		kfree(event);
		return ERR_PTR(err);
	}
	event->type = result;

	if (event->type == SDEI_EVENT_TYPE_SHARED) {
		reg = kzalloc(sizeof(*reg), GFP_KERNEL);
		if (!reg) {
			kfree(event);
			return ERR_PTR(-ENOMEM);
		}

		reg->event_num = event_num;
		reg->priority = event->priority;

		reg->callback = cb;
		reg->callback_arg = cb_arg;
		event->registered = reg;
	}

	if (sdei_event_find(event_num)) {
		kfree(event->registered);
		kfree(event);
		event = ERR_PTR(-EBUSY);
	} else {
		spin_lock(&sdei_list_lock);
		list_add(&event->list, &sdei_list);
		spin_unlock(&sdei_list_lock);
	}

	return event;
}

static void sdei_event_destroy(struct sdei_event *event)
{
	lockdep_assert_held(&sdei_events_lock);

	spin_lock(&sdei_list_lock);
	list_del(&event->list);
	spin_unlock(&sdei_list_lock);

	if (event->type == SDEI_EVENT_TYPE_SHARED)
		kfree(event->registered);

	kfree(event);
}

static int sdei_api_get_version(u64 *version)
{
	return invoke_sdei_fn(SDEI_1_0_FN_SDEI_VERSION, 0, 0, 0, 0, 0, version);
}

int sdei_mask_local_cpu(void)
{
	int err;

	WARN_ON_ONCE(preemptible());

	err = invoke_sdei_fn(SDEI_1_0_FN_SDEI_PE_MASK, 0, 0, 0, 0, 0, NULL);
	if (err && err != -EIO) {
		pr_warn_once("failed to mask CPU[%u]: %d\n",
			      smp_processor_id(), err);
		return err;
	}

	return 0;
}

static void _ipi_mask_cpu(void *ignored)
{
	sdei_mask_local_cpu();
}

int sdei_unmask_local_cpu(void)
{
	int err;

	WARN_ON_ONCE(preemptible());

	err = invoke_sdei_fn(SDEI_1_0_FN_SDEI_PE_UNMASK, 0, 0, 0, 0, 0, NULL);
	if (err && err != -EIO) {
		pr_warn_once("failed to unmask CPU[%u]: %d\n",
			     smp_processor_id(), err);
		return err;
	}

	return 0;
}

static void _ipi_unmask_cpu(void *ignored)
{
	sdei_unmask_local_cpu();
}

static void _ipi_private_reset(void *ignored)
{
	int err;

	err = invoke_sdei_fn(SDEI_1_0_FN_SDEI_PRIVATE_RESET, 0, 0, 0, 0, 0,
			     NULL);
	if (err && err != -EIO)
		pr_warn_once("failed to reset CPU[%u]: %d\n",
			     smp_processor_id(), err);
}

static int sdei_api_shared_reset(void)
{
	return invoke_sdei_fn(SDEI_1_0_FN_SDEI_SHARED_RESET, 0, 0, 0, 0, 0,
			      NULL);
}

static void sdei_mark_interface_broken(void)
{
	pr_err("disabling SDEI firmware interface\n");
	on_each_cpu(&_ipi_mask_cpu, NULL, true);
	sdei_firmware_call = NULL;
}

static int sdei_platform_reset(void)
{
	int err;

	on_each_cpu(&_ipi_private_reset, NULL, true);
	err = sdei_api_shared_reset();
	if (err) {
		pr_err("Failed to reset platform: %d\n", err);
		sdei_mark_interface_broken();
	}

	return err;
}

static int sdei_api_event_enable(u32 event_num)
{
	return invoke_sdei_fn(SDEI_1_0_FN_SDEI_EVENT_ENABLE, event_num, 0, 0, 0,
			      0, NULL);
}

int sdei_event_enable(u32 event_num)
{
	int err = -EINVAL;
	struct sdei_event *event;

	mutex_lock(&sdei_events_lock);
	event = sdei_event_find(event_num);
	if (!event) {
		mutex_unlock(&sdei_events_lock);
		return -ENOENT;
	}

	if (event->type == SDEI_EVENT_TYPE_SHARED)
		err = sdei_api_event_enable(event->event_num);
	mutex_unlock(&sdei_events_lock);

	return err;
}
EXPORT_SYMBOL(sdei_event_enable);

static int sdei_api_event_disable(u32 event_num)
{
	return invoke_sdei_fn(SDEI_1_0_FN_SDEI_EVENT_DISABLE, event_num, 0, 0,
			      0, 0, NULL);
}

int sdei_event_disable(u32 event_num)
{
	int err = -EINVAL;
	struct sdei_event *event;

	mutex_lock(&sdei_events_lock);
	event = sdei_event_find(event_num);
	if (!event) {
		mutex_unlock(&sdei_events_lock);
		return -ENOENT;
	}

	if (event->type == SDEI_EVENT_TYPE_SHARED)
		err = sdei_api_event_disable(event->event_num);
	mutex_unlock(&sdei_events_lock);

	return err;
}
EXPORT_SYMBOL(sdei_event_disable);

static int sdei_api_event_unregister(u32 event_num)
{
	return invoke_sdei_fn(SDEI_1_0_FN_SDEI_EVENT_UNREGISTER, event_num, 0,
			      0, 0, 0, NULL);
}

static int _sdei_event_unregister(struct sdei_event *event)
{
	lockdep_assert_held(&sdei_events_lock);

	if (event->type == SDEI_EVENT_TYPE_SHARED)
		return sdei_api_event_unregister(event->event_num);

	return -EINVAL;
}

int sdei_event_unregister(u32 event_num)
{
	int err;
	struct sdei_event *event;

	WARN_ON(in_nmi());

	mutex_lock(&sdei_events_lock);
	event = sdei_event_find(event_num);
	do {
		if (!event) {
			pr_warn("Event %u not registered\n", event_num);
			err = -ENOENT;
			break;
		}

		err = _sdei_event_unregister(event);
		if (err)
			break;

		sdei_event_destroy(event);
	} while (0);
	mutex_unlock(&sdei_events_lock);

	return err;
}
EXPORT_SYMBOL(sdei_event_unregister);

static int sdei_api_event_register(u32 event_num, unsigned long entry_point,
				   void *arg, u64 flags, u64 affinity)
{
	return invoke_sdei_fn(SDEI_1_0_FN_SDEI_EVENT_REGISTER, event_num,
			      (unsigned long)entry_point, (unsigned long)arg,
			      flags, affinity, NULL);
}

static int _sdei_event_register(struct sdei_event *event)
{
	lockdep_assert_held(&sdei_events_lock);

	if (event->type == SDEI_EVENT_TYPE_SHARED)
		return sdei_api_event_register(event->event_num,
					       sdei_entry_point,
					       event->registered,
					       SDEI_EVENT_REGISTER_RM_ANY, 0);

	return -EINVAL;
}

int sdei_event_register(u32 event_num, sdei_event_callback *cb, void *arg)
{
	int err;
	struct sdei_event *event;

	WARN_ON(in_nmi());

	mutex_lock(&sdei_events_lock);
	do {
		if (sdei_event_find(event_num)) {
			pr_warn("Event %u already registered\n", event_num);
			err = -EBUSY;
			break;
		}

		event = sdei_event_create(event_num, cb, arg);
		if (IS_ERR(event)) {
			err = PTR_ERR(event);
			pr_warn("Failed to create event %u: %d\n", event_num,
				err);
			break;
		}

		err = _sdei_event_register(event);
		if (err) {
			sdei_event_destroy(event);
			pr_warn("Failed to register event %u: %d\n", event_num,
				err);
		}
	} while (0);
	mutex_unlock(&sdei_events_lock);

	return err;
}
EXPORT_SYMBOL(sdei_event_register);

static void sdei_smccc_smc(unsigned long function_id,
			   unsigned long arg0, unsigned long arg1,
			   unsigned long arg2, unsigned long arg3,
			   unsigned long arg4, struct arm_smccc_res *res)
{
	arm_smccc_smc(function_id, arg0, arg1, arg2, arg3, arg4, 0, 0, res);
}

static void sdei_smccc_hvc(unsigned long function_id,
			   unsigned long arg0, unsigned long arg1,
			   unsigned long arg2, unsigned long arg3,
			   unsigned long arg4, struct arm_smccc_res *res)
{
	arm_smccc_hvc(function_id, arg0, arg1, arg2, arg3, arg4, 0, 0, res);
}

static int sdei_get_conduit(struct platform_device *pdev)
{
	const char *method;
	struct device_node *np = pdev->dev.of_node;

	sdei_firmware_call = NULL;
	if (np) {
		if (of_property_read_string(np, "method", &method)) {
			pr_warn("missing \"method\" property\n");
			return CONDUIT_INVALID;
		}

		if (!strcmp("hvc", method)) {
			sdei_firmware_call = &sdei_smccc_hvc;
			return CONDUIT_HVC;
		} else if (!strcmp("smc", method)) {
			sdei_firmware_call = &sdei_smccc_smc;
			return CONDUIT_SMC;
		}

		pr_warn("invalid \"method\" property: %s\n", method);
	}

	return CONDUIT_INVALID;
}

static int sdei_probe(struct platform_device *pdev)
{
	int err;
	u64 ver = 0;
	int conduit;

	conduit = sdei_get_conduit(pdev);
	if (!sdei_firmware_call)
		return 0;

	err = sdei_api_get_version(&ver);
	if (err == -EOPNOTSUPP)
		pr_err("advertised but not implemented in platform firmware\n");
	if (err) {
		pr_err("Failed to get SDEI version: %d\n", err);
		sdei_mark_interface_broken();
		return err;
	}

	pr_info("SDEIv%d.%d (0x%x) detected in firmware.\n",
		(int)SDEI_VERSION_MAJOR(ver), (int)SDEI_VERSION_MINOR(ver),
		(int)SDEI_VERSION_VENDOR(ver));

	if (SDEI_VERSION_MAJOR(ver) != 1) {
		pr_warn("Conflicting SDEI version detected.\n");
		sdei_mark_interface_broken();
		return -EINVAL;
	}

	err = sdei_platform_reset();
	if (err)
		return err;

	sdei_entry_point = sdei_arch_get_entry_point(conduit);
	if (!sdei_entry_point) {
		/* Not supported due to hardware or boot configuration */
		sdei_mark_interface_broken();
		return 0;
	}

	on_each_cpu(&_ipi_unmask_cpu, NULL, false);

	return 0;
}

static const struct of_device_id sdei_of_match[] = {
	{ .compatible = "arm,sdei-1.0" },
	{}
};

static struct platform_driver sdei_driver = {
	.driver		= {
		.name			= "sdei",
		.of_match_table		= sdei_of_match,
	},
	.probe		= sdei_probe,
};

static bool __init sdei_present_dt(void)
{
	struct platform_device *pdev;
	struct device_node *np, *fw_np;

	fw_np = of_find_node_by_name(NULL, "firmware");
	if (!fw_np)
		return false;

	np = of_find_matching_node(fw_np, sdei_of_match);
	of_node_put(fw_np);
	if (!np)
		return false;

	pdev = of_platform_device_create(np, sdei_driver.driver.name, NULL);
	of_node_put(np);
	if (IS_ERR(pdev))
		return false;

	return true;
}

static int __init sdei_init(void)
{
	if (sdei_present_dt())
		platform_driver_register(&sdei_driver);

	return 0;
}

subsys_initcall_sync(sdei_init);

int sdei_event_handler(struct pt_regs *regs,
		       struct sdei_registered_event *arg)
{
	int err;
	mm_segment_t orig_addr_limit;
	u32 event_num = arg->event_num;

	orig_addr_limit = get_fs();
	set_fs(USER_DS);

	err = arg->callback(event_num, regs, arg->callback_arg);
	if (err)
		pr_err_ratelimited("event %u on CPU %u failed with error: %d\n",
				   event_num, smp_processor_id(), err);

	set_fs(orig_addr_limit);

	return err;
}
NOKPROBE_SYMBOL(sdei_event_handler);

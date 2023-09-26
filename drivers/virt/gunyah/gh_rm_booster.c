// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/notifier.h>
#include <linux/cpufreq.h>
#include <asm/gunyah/hcall.h>

#include "gh_rm_drv_private.h"
#include "gh_rm_booster.h"

#define BOOSTER_TIMEOUT_MSEC	5000

struct gh_rm_booster_dev {
	u32 boost_cnt;
	int vmid;
	int orig_cpu;
	struct device *dev;
	struct freq_qos_request gh_rm_boost_req;
	struct notifier_block gh_rm_boost_nb;
	struct delayed_work booster_release_work;
	gh_capid_t vcpu_cap_id;
};
static struct gh_rm_booster_dev *rm_status;
static DEFINE_MUTEX(rm_booster_lock);

static int rm_boost_target_cpu_set(const char *val, const struct kernel_param *kp)
{
	unsigned int cpu;
	int ret;

	ret = kstrtou32(val, 10, &cpu);
	if (ret)
		return ret;

	if (cpu < num_possible_cpus())
		return param_set_int(val, kp);
	else
		return -EINVAL;
}

static const struct kernel_param_ops target_cpu_ops = {
	.set = rm_boost_target_cpu_set,
	.get = param_get_int,
};
static unsigned int target_cpu = UINT_MAX;
module_param_cb(target_cpu, &target_cpu_ops, &target_cpu, 0644);
MODULE_PARM_DESC(target_cpu, "Choose the target core for Resource Manager");

static int gh_set_rm_affinity(int cpu)
{
	int ret = -EINVAL;

	ret = gh_hcall_change_rm_affinity(rm_status->vcpu_cap_id, cpu);
	if (ret == GH_ERROR_OK)
		return 0;

	dev_err(rm_status->dev, "gh set RM affinity fail\n");
	return ret;
}

static void gh_boost_rmfreq(int cpu)
{
	struct cpufreq_policy *policy;
	int ret;

	policy = cpufreq_cpu_get(cpu);
	if (!policy) {
		dev_err(rm_status->dev, "Failed to get RM cpufreq policy\n");
		return;
	}

	/* Always have target cpu's max freq as boosted freq. */
	ret = freq_qos_add_request(&policy->constraints,
				   &rm_status->gh_rm_boost_req,
				   FREQ_QOS_MIN, policy->max);

	if (ret < 0)
		dev_err(rm_status->dev, "Failed to boost RM freq\n");
}

static void gh_resume_rm_status(void)
{
	int ret;

	ret = freq_qos_remove_request(&rm_status->gh_rm_boost_req);
	if (ret < 0)
		dev_err(rm_status->dev, "Failed to resume RM freq\n");

	if (rm_status->vcpu_cap_id == GH_CAPID_INVAL)
		return;

	ret = gh_set_rm_affinity(rm_status->orig_cpu);
	if (ret)
		dev_err(rm_status->dev, "Failed to resume RM affinity\n");
}

static void gh_configure_rm(struct gh_rm_notif_vm_status_payload *status)
{
	int dest_cpu;

	switch (status->vm_status) {
	case GH_RM_VM_STATUS_AUTH:
		cancel_delayed_work_sync(&rm_status->booster_release_work);
		mutex_lock(&rm_booster_lock);
		rm_status->boost_cnt++;
		schedule_delayed_work(&rm_status->booster_release_work,
				msecs_to_jiffies(BOOSTER_TIMEOUT_MSEC));
		if (rm_status->boost_cnt > 1) {
			mutex_unlock(&rm_booster_lock);
			dev_dbg(rm_status->dev,
				"VM%d found Gunyah RM booster already activated\n",
				status->vmid);
			break;
		}

		dest_cpu = target_cpu;
		if (dest_cpu != rm_status->orig_cpu) {
			if (rm_status->vcpu_cap_id == GH_CAPID_INVAL ||
			    gh_set_rm_affinity(dest_cpu)) {
				dest_cpu = rm_status->orig_cpu;
				dev_info(rm_status->dev,
					 "Fallback to boost the frequency of RM current cpu - CPU%d\n",
					 dest_cpu);
			}
		}

		gh_boost_rmfreq(dest_cpu);
		mutex_unlock(&rm_booster_lock);
		dev_dbg(rm_status->dev,
			"Gunyah RM booster activated by VM%d\n", status->vmid);
		break;
	case GH_RM_VM_STATUS_INIT_FAILED:
		fallthrough;
	case GH_RM_VM_STATUS_RUNNING:
		mutex_lock(&rm_booster_lock);
		if (!rm_status->boost_cnt) {
			mutex_unlock(&rm_booster_lock);
			break;
		}

		rm_status->boost_cnt--;
		if (rm_status->boost_cnt) {
			mutex_unlock(&rm_booster_lock);
			dev_dbg(rm_status->dev,
				"VM%d will not deactivate Gunyah RM booster\n",
				status->vmid);
			break;
		}

		gh_resume_rm_status();
		mutex_unlock(&rm_booster_lock);
		cancel_delayed_work_sync(&rm_status->booster_release_work);
		dev_dbg(rm_status->dev,
			"Gunyah RM booster deactivated by VM%d\n",
			status->vmid);
		break;
	default:
		break;
	}
}

static int gh_rm_boost_nb_handler(struct notifier_block *this,
				  unsigned long cmd, void *data)
{
	switch (cmd) {
	case GH_RM_NOTIF_VM_STATUS:
		gh_configure_rm(data);
		break;
	default:
		break;
	}

	return NOTIFY_DONE;
}

static void gh_rm_booster_release_func(struct work_struct *work)
{
	mutex_lock(&rm_booster_lock);
	if (!rm_status || rm_status->boost_cnt == 0) {
		mutex_unlock(&rm_booster_lock);
		return;
	}

	dev_dbg(rm_status->dev, "Gunyah RM booster released\n");
	gh_resume_rm_status();
	rm_status->boost_cnt = 0;
	mutex_unlock(&rm_booster_lock);
}

static void gh_rm_booster_populate_res(void)
{
	struct gh_vm_get_hyp_res_resp_entry *res_entries = NULL;
	u32 n_res, i;

	res_entries = gh_rm_vm_get_hyp_res(rm_status->vmid, &n_res);
	if (IS_ERR_OR_NULL(res_entries)) {
		dev_err(rm_status->dev, "Get hyp resources failed.\n");
		return;
	}

	for (i = 0; i < n_res; i++) {
		if (res_entries[i].res_type == GH_RM_RES_TYPE_VCPU) {
			rm_status->vcpu_cap_id = (u64) res_entries[i].cap_id_high << 32 |
				 res_entries[i].cap_id_low;
			goto out;
		}
	}

	dev_err(rm_status->dev, "No vCPU resource type found.\n");
out:
	kfree(res_entries);
}

static int gh_rm_booster_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	int ret = -ENODEV;

	rm_status = kzalloc(sizeof(*rm_status), GFP_KERNEL);
	if (!rm_status)
		return -ENOMEM;

	ret = of_property_read_u32(np, "qcom,rm-vmid", &rm_status->vmid);
	if (ret) {
		dev_err(&pdev->dev, "Failed to get RM vmid\n");
		goto out_free;
	}

	ret = of_property_read_u32(np, "qcom,rm-affinity-default",
				&rm_status->orig_cpu);
	if (ret) {
		dev_err(&pdev->dev, "Failed to get RM affinity\n");
		goto out_free;
	}

	rm_status->dev = &pdev->dev;
	platform_set_drvdata(pdev, rm_status);

	INIT_DELAYED_WORK(&rm_status->booster_release_work,
			gh_rm_booster_release_func);
	/*
	 * By default target cpu will be the bigger number cpu,
	 * and usually the most powerful cpu here.
	 */
	if (target_cpu >= num_possible_cpus())
		target_cpu = num_possible_cpus() - 1;

	rm_status->boost_cnt = 0;
	rm_status->vcpu_cap_id = GH_CAPID_INVAL;
	gh_rm_booster_populate_res();

	rm_status->gh_rm_boost_nb.notifier_call = gh_rm_boost_nb_handler;
	rm_status->gh_rm_boost_nb.priority = INT_MAX;
	ret = gh_rm_register_notifier(&rm_status->gh_rm_boost_nb);
	if (!ret)
		return 0;

	dev_err(&pdev->dev, "Failed to register RM notifier\n");
out_free:
	kfree(rm_status);
	return ret;
}

static int gh_rm_booster_remove(struct platform_device *pdev)
{
	gh_rm_unregister_notifier(&rm_status->gh_rm_boost_nb);
	cancel_delayed_work_sync(&rm_status->booster_release_work);

	mutex_lock(&rm_booster_lock);
	if (rm_status->boost_cnt)
		gh_resume_rm_status();

	kfree(rm_status);
	rm_status = NULL;
	mutex_unlock(&rm_booster_lock);

	return 0;
}

static const struct of_device_id gh_rm_booster_match_table[] = {
	{ .compatible = "qcom,gh-rm-booster" },
	{}
};

static struct platform_driver gh_rm_booster_driver = {
	.driver = {
		.name = "gh_rm_booster",
		.of_match_table = gh_rm_booster_match_table,
	},
	.probe = gh_rm_booster_probe,
	.remove = gh_rm_booster_remove,
};

module_platform_driver(gh_rm_booster_driver);

MODULE_DESCRIPTION("Qualcomm Technologies, Inc. Gunyah RM booster Driver");
MODULE_LICENSE("GPL");

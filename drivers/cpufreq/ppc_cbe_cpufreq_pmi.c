// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * pmi backend for the cbe_cpufreq driver
 *
 * (C) Copyright IBM Deutschland Entwicklung GmbH 2005-2007
 *
 * Author: Christian Krafft <krafft@de.ibm.com>
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/of_platform.h>
#include <linux/pm_qos.h>

#include <asm/processor.h>
#include <asm/prom.h>
#include <asm/pmi.h>
#include <asm/cell-regs.h>

#ifdef DEBUG
#include <asm/time.h>
#endif

#include "ppc_cbe_cpufreq.h"

bool cbe_cpufreq_has_pmi = false;
EXPORT_SYMBOL_GPL(cbe_cpufreq_has_pmi);

/*
 * hardware specific functions
 */

int cbe_cpufreq_set_pmode_pmi(int cpu, unsigned int pmode)
{
	int ret;
	pmi_message_t pmi_msg;
#ifdef DEBUG
	long time;
#endif
	pmi_msg.type = PMI_TYPE_FREQ_CHANGE;
	pmi_msg.data1 =	cbe_cpu_to_node(cpu);
	pmi_msg.data2 = pmode;

#ifdef DEBUG
	time = jiffies;
#endif
	pmi_send_message(pmi_msg);

#ifdef DEBUG
	time = jiffies  - time;
	time = jiffies_to_msecs(time);
	pr_debug("had to wait %lu ms for a transition using " \
		 "PMI\n", time);
#endif
	ret = pmi_msg.data2;
	pr_debug("PMI returned slow mode %d\n", ret);

	return ret;
}
EXPORT_SYMBOL_GPL(cbe_cpufreq_set_pmode_pmi);


static void cbe_cpufreq_handle_pmi(pmi_message_t pmi_msg)
{
	struct cpufreq_policy *policy;
	struct freq_qos_request *req;
	u8 node, slow_mode;
	int cpu, ret;

	BUG_ON(pmi_msg.type != PMI_TYPE_FREQ_CHANGE);

	node = pmi_msg.data1;
	slow_mode = pmi_msg.data2;

	cpu = cbe_node_to_cpu(node);

	pr_debug("cbe_handle_pmi: node: %d max_freq: %d\n", node, slow_mode);

	policy = cpufreq_cpu_get(cpu);
	if (!policy) {
		pr_warn("cpufreq policy not found cpu%d\n", cpu);
		return;
	}

	req = policy->driver_data;

	ret = freq_qos_update_request(req,
			policy->freq_table[slow_mode].frequency);
	if (ret < 0)
		pr_warn("Failed to update freq constraint: %d\n", ret);
	else
		pr_debug("limiting node %d to slow mode %d\n", node, slow_mode);

	cpufreq_cpu_put(policy);
}

static struct pmi_handler cbe_pmi_handler = {
	.type			= PMI_TYPE_FREQ_CHANGE,
	.handle_pmi_message	= cbe_cpufreq_handle_pmi,
};

void cbe_cpufreq_pmi_policy_init(struct cpufreq_policy *policy)
{
	struct freq_qos_request *req;
	int ret;

	if (!cbe_cpufreq_has_pmi)
		return;

	req = kzalloc(sizeof(*req), GFP_KERNEL);
	if (!req)
		return;

	ret = freq_qos_add_request(&policy->constraints, req, FREQ_QOS_MAX,
				   policy->freq_table[0].frequency);
	if (ret < 0) {
		pr_err("Failed to add freq constraint (%d)\n", ret);
		kfree(req);
		return;
	}

	policy->driver_data = req;
}
EXPORT_SYMBOL_GPL(cbe_cpufreq_pmi_policy_init);

void cbe_cpufreq_pmi_policy_exit(struct cpufreq_policy *policy)
{
	struct freq_qos_request *req = policy->driver_data;

	if (cbe_cpufreq_has_pmi) {
		freq_qos_remove_request(req);
		kfree(req);
	}
}
EXPORT_SYMBOL_GPL(cbe_cpufreq_pmi_policy_exit);

void cbe_cpufreq_pmi_init(void)
{
	if (!pmi_register_handler(&cbe_pmi_handler))
		cbe_cpufreq_has_pmi = true;
}
EXPORT_SYMBOL_GPL(cbe_cpufreq_pmi_init);

void cbe_cpufreq_pmi_exit(void)
{
	pmi_unregister_handler(&cbe_pmi_handler);
	cbe_cpufreq_has_pmi = false;
}
EXPORT_SYMBOL_GPL(cbe_cpufreq_pmi_exit);

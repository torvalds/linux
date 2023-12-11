// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <asm/div64.h>
#include <linux/interconnect-provider.h>
#include <linux/list_sort.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <soc/qcom/rpmh.h>
#include <soc/qcom/tcs.h>

#include "bcm-voter.h"
#include "icc-rpmh.h"

#define CREATE_TRACE_POINTS
#include "trace.h"

static LIST_HEAD(bcm_voters);
static DEFINE_MUTEX(bcm_voter_lock);

/**
 * struct bcm_voter - Bus Clock Manager voter
 * @dev: reference to the device that communicates with the BCM
 * @np: reference to the device node to match bcm voters
 * @lock: mutex to protect commit and wake/sleep lists in the voter
 * @commit_list: list containing bcms to be committed to hardware
 * @ws_list: list containing bcms that have different wake/sleep votes
 * @voter_node: list of bcm voters
 * @tcs_wait: mask for which buckets require TCS completion
 * @has_amc: flag to determine if this voter supports AMC
 * @init: flag to determine when init has completed.
 */
struct bcm_voter {
	struct device *dev;
	struct device_node *np;
	struct qcom_icc_crm_voter *crm;
	struct mutex lock;
	struct list_head commit_list;
	struct list_head ws_list;
	struct list_head voter_node;
	u32 tcs_wait;
	bool has_amc;
	bool init;
};

static int cmp_vcd(void *priv, const struct list_head *a, const struct list_head *b)
{
	const struct qcom_icc_bcm *bcm_a = list_entry(a, struct qcom_icc_bcm, list);
	const struct qcom_icc_bcm *bcm_b = list_entry(b, struct qcom_icc_bcm, list);

	return bcm_a->aux_data.vcd - bcm_b->aux_data.vcd;
}

static u64 bcm_div(u64 num, u32 base)
{
	/* Ensure that small votes aren't lost. */
	if (num && num < base)
		return 1;

	do_div(num, base);

	return num;
}

/* BCMs with enable_mask use one-hot-encoding for on/off signaling */
static void bcm_aggregate_mask(struct qcom_icc_bcm *bcm)
{
	struct qcom_icc_node *node;
	int bucket, i;

	for (bucket = 0; bucket < QCOM_ICC_NUM_BUCKETS; bucket++) {
		bcm->vote_x[bucket] = 0;
		bcm->vote_y[bucket] = 0;

		for (i = 0; i < bcm->num_nodes; i++) {
			node = bcm->nodes[i];

			/* If any vote in this bucket exists, keep the BCM enabled */
			if (node->sum_avg[bucket] || node->max_peak[bucket])
				bcm->vote_y[bucket] |= bcm->enable_mask;

			if (node->perf_mode[bucket])
				bcm->vote_y[bucket] |= bcm->perf_mode_mask;
		}
	}

	if (bcm->keepalive) {
		bcm->vote_y[QCOM_ICC_BUCKET_AMC] |= bcm->enable_mask;
		bcm->vote_y[QCOM_ICC_BUCKET_WAKE] |= bcm->enable_mask;
	}
}

static void bcm_aggregate(struct qcom_icc_bcm *bcm, bool init)
{
	struct qcom_icc_node *node;
	size_t i, bucket;
	u64 agg_avg[QCOM_ICC_NUM_BUCKETS] = {0};
	u64 agg_peak[QCOM_ICC_NUM_BUCKETS] = {0};
	u64 temp;

	for (bucket = 0; bucket < QCOM_ICC_NUM_BUCKETS; bucket++) {
		for (i = 0; i < bcm->num_nodes; i++) {
			node = bcm->nodes[i];
			temp = bcm_div(node->sum_avg[bucket] * bcm->aux_data.width,
				       node->buswidth * node->channels);
			agg_avg[bucket] = max(agg_avg[bucket], temp);

			temp = bcm_div(node->max_peak[bucket] * bcm->aux_data.width,
				       node->buswidth);
			agg_peak[bucket] = max(agg_peak[bucket], temp);
		}

		temp = agg_avg[bucket] * bcm->vote_scale;
		bcm->vote_x[bucket] = bcm_div(temp, bcm->aux_data.unit);

		temp = agg_peak[bucket] * bcm->vote_scale;
		bcm->vote_y[bucket] = bcm_div(temp, bcm->aux_data.unit);
	}

	if (bcm->keepalive || bcm->keepalive_early) {
		/*
		 * Keepalive should normally only be enforced for AMC/WAKE so
		 * that BCMs are only kept alive when HLOS is active. But early
		 * during init all clients haven't had a chance to vot yet, and
		 * some have use cases that persist when HLOS is asleep. So
		 * during init vote to all sets, including SLEEP.
		 */
		if (init) {
			bcm->vote_x[QCOM_ICC_BUCKET_AMC] = 16000;
			bcm->vote_x[QCOM_ICC_BUCKET_WAKE] = 16000;
			bcm->vote_x[QCOM_ICC_BUCKET_SLEEP] = 16000;
			bcm->vote_y[QCOM_ICC_BUCKET_AMC] = 16000;
			bcm->vote_y[QCOM_ICC_BUCKET_WAKE] = 16000;
			bcm->vote_y[QCOM_ICC_BUCKET_SLEEP] = 16000;
		} else if (bcm->vote_x[QCOM_ICC_BUCKET_AMC] == 0 &&
			   bcm->vote_y[QCOM_ICC_BUCKET_AMC] == 0) {
			bcm->vote_x[QCOM_ICC_BUCKET_AMC] = 1;
			bcm->vote_x[QCOM_ICC_BUCKET_WAKE] = 1;
			bcm->vote_y[QCOM_ICC_BUCKET_AMC] = 1;
			bcm->vote_y[QCOM_ICC_BUCKET_WAKE] = 1;
		}
	}
}

static inline void tcs_cmd_gen(struct tcs_cmd *cmd, u64 vote_x, u64 vote_y,
			       u32 addr, bool commit, bool wait)
{
	bool valid = true;

	if (!cmd)
		return;

	memset(cmd, 0, sizeof(*cmd));

	if (vote_x == 0 && vote_y == 0)
		valid = false;

	if (vote_x > BCM_TCS_CMD_VOTE_MASK)
		vote_x = BCM_TCS_CMD_VOTE_MASK;

	if (vote_y > BCM_TCS_CMD_VOTE_MASK)
		vote_y = BCM_TCS_CMD_VOTE_MASK;

	cmd->addr = addr;
	cmd->data = BCM_TCS_CMD(commit, valid, vote_x, vote_y);

	/*
	 * Set the wait for completion flag on command that need to be completed
	 * before the next command.
	 */
	cmd->wait = wait;
}

static void tcs_list_gen(struct bcm_voter *voter, int bucket,
			 struct tcs_cmd tcs_list[MAX_VCD],
			 int n[MAX_VCD + 1])
{
	struct list_head *bcm_list = &voter->commit_list;
	struct qcom_icc_bcm *bcm;
	bool commit, wait;
	size_t idx = 0, batch = 0, cur_vcd_size = 0;

	memset(n, 0, sizeof(int) * (MAX_VCD + 1));

	list_for_each_entry(bcm, bcm_list, list) {
		commit = false;
		cur_vcd_size++;
		if ((list_is_last(&bcm->list, bcm_list)) ||
		    bcm->aux_data.vcd != list_next_entry(bcm, list)->aux_data.vcd) {
			commit = true;
			cur_vcd_size = 0;
		}

		wait = commit && (voter->tcs_wait & BIT(bucket));

		tcs_cmd_gen(&tcs_list[idx], bcm->vote_x[bucket],
			    bcm->vote_y[bucket], bcm->addr, commit, wait);
		idx++;
		n[batch]++;
		/*
		 * Batch the BCMs in such a way that we do not split them in
		 * multiple payloads when they are under the same VCD. This is
		 * to ensure that every BCM is committed since we only set the
		 * commit bit on the last BCM request of every VCD.
		 */
		if (n[batch] >= MAX_RPMH_PAYLOAD) {
			if (!commit) {
				n[batch] -= cur_vcd_size;
				n[batch + 1] = cur_vcd_size;
			}
			batch++;
		}
	}
}

/**
 * of_bcm_voter_get - gets a bcm voter handle from DT node
 * @dev: device pointer for the consumer device
 * @name: name for the bcm voter device
 *
 * This function will match a device_node pointer for the phandle
 * specified in the device DT and return a bcm_voter handle on success.
 *
 * Returns bcm_voter pointer or ERR_PTR() on error. EPROBE_DEFER is returned
 * when matching bcm voter is yet to be found.
 */
struct bcm_voter *of_bcm_voter_get(struct device *dev, const char *name)
{
	struct bcm_voter *voter = ERR_PTR(-EPROBE_DEFER);
	struct bcm_voter *temp;
	struct device_node *np, *node;
	int idx = 0;

	if (!dev || !dev->of_node)
		return ERR_PTR(-ENODEV);

	np = dev->of_node;

	if (name) {
		idx = of_property_match_string(np, "qcom,bcm-voter-names", name);
		if (idx < 0)
			return ERR_PTR(idx);
	}

	node = of_parse_phandle(np, "qcom,bcm-voters", idx);

	mutex_lock(&bcm_voter_lock);
	list_for_each_entry(temp, &bcm_voters, voter_node) {
		if (temp->np == node) {
			voter = temp;
			break;
		}
	}
	mutex_unlock(&bcm_voter_lock);

	of_node_put(node);
	return voter;
}
EXPORT_SYMBOL_GPL(of_bcm_voter_get);

/**
 * qcom_icc_bcm_voter_exist - checks if the bcm voter exists
 * @voter: voter that needs to checked against available bcm voters
 *
 * Returns true incase bcm_voter exists else false
 */
static bool qcom_icc_bcm_voter_exist(struct bcm_voter *voter)
{
	bool exists = false;
	struct bcm_voter *temp;

	if (voter) {
		mutex_lock(&bcm_voter_lock);
		list_for_each_entry(temp, &bcm_voters, voter_node) {
			if (temp == voter) {
				exists = true;
				break;
			}
		}
		mutex_unlock(&bcm_voter_lock);
	}

	return exists;
}

/**
 * qcom_icc_bcm_voter_add - queues up the bcm nodes that require updates
 * @voter: voter that the bcms are being added to
 * @bcm: bcm to add to the commit and wake sleep list
 */
void qcom_icc_bcm_voter_add(struct bcm_voter *voter, struct qcom_icc_bcm *bcm)
{
	if (!voter)
		return;

	if (!qcom_icc_bcm_voter_exist(voter))
		return;

	mutex_lock(&voter->lock);
	if (list_empty(&bcm->list))
		list_add_tail(&bcm->list, &voter->commit_list);

	if (list_empty(&bcm->ws_list))
		list_add_tail(&bcm->ws_list, &voter->ws_list);

	mutex_unlock(&voter->lock);
}
EXPORT_SYMBOL_GPL(qcom_icc_bcm_voter_add);

static void qcom_icc_bcm_log(struct bcm_voter *voter, enum rpmh_state state,
			     const struct tcs_cmd *cmd, const u32 *commit_idx)
{
	static const char * const rpmh_state[] = {
			"RPMH_SLEEP_STATE",
			"RPMH_WAKE_ONLY_STATE",
			"RPMH_ACTIVE_ONLY_STATE"
	};
	int i, count = 0;

	if (!cmd || !commit_idx)
		return;

	while (commit_idx[count] > 0)
		count++;

	if (!count)
		return;

	for (i = 0; i < count; i++)
		trace_bcm_voter_commit(rpmh_state[state], cmd);
}

static int commit_rpmh(struct bcm_voter *voter)
{
	struct qcom_icc_bcm *bcm;
	struct qcom_icc_bcm *bcm_tmp;
	int commit_idx[MAX_VCD + 1];
	struct tcs_cmd cmds[MAX_BCMS];
	int ret = 0;

	if (voter->has_amc) {
		/*
		 * Pre sort the BCMs based on VCD for ease of generating a command list
		 * that groups the BCMs with the same VCD together. VCDs are numbered
		 * with lowest being the most expensive time wise, ensuring that
		 * those commands are being sent the earliest in the queue. This needs
		 * to be sorted every commit since we can't guarantee the order in which
		 * the BCMs are added to the list.
		 */
		list_sort(NULL, &voter->commit_list, cmp_vcd);

		/*
		 * Construct the command list based on a pre ordered list of BCMs
		 * based on VCD.
		 */
		tcs_list_gen(voter, QCOM_ICC_BUCKET_AMC, cmds, commit_idx);
		if (!commit_idx[0])
			goto out;

		qcom_icc_bcm_log(voter, RPMH_ACTIVE_ONLY_STATE, cmds, commit_idx);
		ret = rpmh_write_batch(voter->dev, RPMH_ACTIVE_ONLY_STATE,
				       cmds, commit_idx);

		/*
		 * Ignore -EBUSY for AMC requests, since this can only happen for AMC
		 * requests when the RSC is in solver mode. We can only be in solver
		 * mode at the time of request for secondary RSCs (e.g. Display RSC),
		 * since the primary Apps RSC is only in solver mode while
		 * entering/exiting power collapse when SW isn't running. The -EBUSY
		 * response is expected in solver and is a non-issue, since we just
		 * want the request to apply to the WAKE set in that case instead.
		 * Interconnect doesn't know when the RSC is in solver, so just always
		 * send AMC and ignore the harmless error response.
		 */
		if (ret && ret != -EBUSY) {
			pr_err("Error sending AMC RPMH requests (%d)\n", ret);
			goto out;
		}
	}

	rpmh_invalidate(voter->dev);

	list_for_each_entry_safe(bcm, bcm_tmp, &voter->commit_list, list)
		list_del_init(&bcm->list);

	list_for_each_entry_safe(bcm, bcm_tmp, &voter->ws_list, ws_list) {
		/*
		 * Only generate WAKE and SLEEP commands if a resource's
		 * requirements change as the execution environment transitions
		 * between different power states.
		 */
		if (!voter->has_amc ||
		    bcm->vote_x[QCOM_ICC_BUCKET_WAKE] != bcm->vote_x[QCOM_ICC_BUCKET_SLEEP] ||
		    bcm->vote_y[QCOM_ICC_BUCKET_WAKE] != bcm->vote_y[QCOM_ICC_BUCKET_SLEEP])
			list_add_tail(&bcm->list, &voter->commit_list);
		else
			list_del_init(&bcm->ws_list);
	}

	if (list_empty(&voter->commit_list))
		goto out;

	list_sort(NULL, &voter->commit_list, cmp_vcd);

	tcs_list_gen(voter, QCOM_ICC_BUCKET_WAKE, cmds, commit_idx);
	qcom_icc_bcm_log(voter, RPMH_WAKE_ONLY_STATE, cmds, commit_idx);

	ret = rpmh_write_batch(voter->dev, RPMH_WAKE_ONLY_STATE, cmds, commit_idx);
	if (ret) {
		pr_err("Error sending WAKE RPMH requests (%d)\n", ret);
		goto out;
	}

	tcs_list_gen(voter, QCOM_ICC_BUCKET_SLEEP, cmds, commit_idx);
	qcom_icc_bcm_log(voter, RPMH_SLEEP_STATE, cmds, commit_idx);

	ret = rpmh_write_batch(voter->dev, RPMH_SLEEP_STATE, cmds, commit_idx);
	if (ret) {
		pr_err("Error sending SLEEP RPMH requests (%d)\n", ret);
		goto out;
	}

out:
	return ret;
}

static int map_crm_pwr_state(enum crm_drv_type client_type, u32 bucket)
{
	if (client_type == CRM_HW_DRV)
		return bucket;

	switch (bucket) {
	case QCOM_ICC_BUCKET_AMC: return CRM_ACTIVE_STATE;
	case QCOM_ICC_BUCKET_WAKE: return CRM_WAKE_STATE;
	case QCOM_ICC_BUCKET_SLEEP: return CRM_SLEEP_STATE;
	}

	return -EINVAL;
}

static int crm_cmd_gen(struct crm_cmd *cmd, enum crm_drv_type client_type,
		       u32 bucket, u32 node, u64 vote_x, u64 vote_y)
{
	int pwr_state;

	if (!cmd)
		return -EINVAL;

	memset(cmd, 0, sizeof(*cmd));

	if (vote_x > BCM_TCS_CMD_VOTE_MASK)
		vote_x = BCM_TCS_CMD_VOTE_MASK;

	if (vote_y > BCM_TCS_CMD_VOTE_MASK)
		vote_y = BCM_TCS_CMD_VOTE_MASK;

	pwr_state = map_crm_pwr_state(client_type, bucket);
	if (pwr_state < 0)
		return pwr_state;

	cmd->pwr_state.hw = pwr_state;
	cmd->resource_idx = node;
	cmd->data = BCM_TCS_CMD(true, true, vote_x, vote_y);
	cmd->wait = true;

	return 0;
}

static int commit_crm(struct bcm_voter *voter)
{
	struct list_head *bcm_list = &voter->commit_list;
	struct qcom_icc_crm_voter *crm = voter->crm;
	struct qcom_icc_bcm *bcm;
	struct crm_cmd crm_cmd;
	int ret, i;

	list_for_each_entry(bcm, bcm_list, list) {
		for (i = 0; i < crm->pwr_states; i++) {
			ret = crm_cmd_gen(&crm_cmd, crm->client_type, i, bcm->crm_node,
					  bcm->vote_x[i], bcm->vote_y[i]);
			if (ret) {
				pr_err("Error generating crm_cmd: ret=%d\n", ret);
				return ret;
			}

			ret = crm_write_bw_vote(crm->dev, crm->client_type,
						crm->client_idx, &crm_cmd);
			if (ret) {
				pr_err("Error writing crm bw: ret=%d\n", ret);
				return ret;
			}
		}
	}

	return 0;
}

/**
 * qcom_icc_bcm_voter_commit - generates and commits tcs cmds based on bcms
 * @voter: voter that needs flushing
 *
 * This function generates a set of AMC commands and flushes to the BCM device
 * associated with the voter. It conditionally generate WAKE and SLEEP commands
 * based on deltas between WAKE/SLEEP requirements. The ws_list persists
 * through multiple commit requests and bcm nodes are removed only when the
 * requirements for WAKE matches SLEEP.
 *
 * Returns 0 on success, or an appropriate error code otherwise.
 */
int qcom_icc_bcm_voter_commit(struct bcm_voter *voter)
{
	struct qcom_icc_bcm *bcm;
	struct qcom_icc_bcm *bcm_tmp;
	int ret;

	if (!voter)
		return 0;

	if (!qcom_icc_bcm_voter_exist(voter))
		return -ENODEV;

	mutex_lock(&voter->lock);

	list_for_each_entry(bcm, &voter->commit_list, list) {
		if (bcm->enable_mask)
			bcm_aggregate_mask(bcm);
		else
			bcm_aggregate(bcm, voter->init);
	}

	if (voter->crm)
		ret = commit_crm(voter);
	else
		ret = commit_rpmh(voter);

	list_for_each_entry_safe(bcm, bcm_tmp, &voter->commit_list, list)
		list_del_init(&bcm->list);

	mutex_unlock(&voter->lock);
	return ret;
}
EXPORT_SYMBOL_GPL(qcom_icc_bcm_voter_commit);

/**
 * qcom_icc_bcm_voter_clear_init - clear init flag used during boot up
 * @voter: voter that we need to clear the init flag for
 */
void qcom_icc_bcm_voter_clear_init(struct bcm_voter *voter)
{
	if (!voter)
		return;

	if (!qcom_icc_bcm_voter_exist(voter))
		return;

	mutex_lock(&voter->lock);
	voter->init = false;
	mutex_unlock(&voter->lock);
}
EXPORT_SYMBOL(qcom_icc_bcm_voter_clear_init);

static int qcom_icc_bcm_voter_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct qcom_icc_crm_voter *crm;
	struct bcm_voter *voter;
	const char *crm_name;
	int ret;

	voter = devm_kzalloc(&pdev->dev, sizeof(*voter), GFP_KERNEL);
	if (!voter)
		return -ENOMEM;

	voter->dev = &pdev->dev;
	voter->np = np;
	voter->init = true;
	voter->has_amc = !of_property_read_bool(np, "qcom,no-amc");

	if (of_property_read_u32(np, "qcom,tcs-wait", &voter->tcs_wait))
		voter->tcs_wait = QCOM_ICC_TAG_ACTIVE_ONLY;

	mutex_init(&voter->lock);
	INIT_LIST_HEAD(&voter->commit_list);
	INIT_LIST_HEAD(&voter->ws_list);

	ret = of_property_read_string(np, "qcom,crm-name", &crm_name);
	if (!ret) {
		crm = devm_kzalloc(&pdev->dev, sizeof(*crm), GFP_KERNEL);
		if (!crm)
			return -ENOMEM;

		crm->dev = crm_get_device(crm_name);
		if (IS_ERR(crm->dev)) {
			if (PTR_ERR(crm->dev) == -ENODEV) {
				dev_err(&pdev->dev, "crm_name=%s unavailable\n", crm_name);
				return -EPROBE_DEFER;
			}
			return PTR_ERR(crm->dev);
		}

		crm->client_type = CRM_HW_DRV;

		ret = of_property_read_u32(np, "qcom,crm-client-idx", &crm->client_idx);
		if (ret) {
			dev_err(&pdev->dev, "Error getting crm-client-idx, ret=%d\n", ret);
			return ret;
		}

		ret = of_property_read_u32(np, "qcom,crm-pwr-states", &crm->pwr_states);
		if (ret) {
			dev_err(&pdev->dev, "Error getting crm-pwr-states, ret=%d\n", ret);
			return ret;
		}

		voter->crm = crm;
	}

	mutex_lock(&bcm_voter_lock);
	list_add_tail(&voter->voter_node, &bcm_voters);
	mutex_unlock(&bcm_voter_lock);

	return 0;
}

static int qcom_icc_bcm_voter_remove(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct bcm_voter *voter, *temp;

	mutex_lock(&bcm_voter_lock);
	list_for_each_entry_safe(voter, temp, &bcm_voters, voter_node) {
		if (voter->np == np) {
			list_del(&voter->voter_node);
			break;
		}
	}
	mutex_unlock(&bcm_voter_lock);

	return 0;
}

static const struct of_device_id bcm_voter_of_match[] = {
	{ .compatible = "qcom,bcm-voter" },
	{ }
};
MODULE_DEVICE_TABLE(of, bcm_voter_of_match);

static struct platform_driver qcom_icc_bcm_voter_driver = {
	.probe = qcom_icc_bcm_voter_probe,
	.remove = qcom_icc_bcm_voter_remove,
	.driver = {
		.name		= "bcm_voter",
		.of_match_table = bcm_voter_of_match,
	},
};

static int __init qcom_icc_bcm_voter_driver_init(void)
{
	return platform_driver_register(&qcom_icc_bcm_voter_driver);
}
module_init(qcom_icc_bcm_voter_driver_init);

MODULE_AUTHOR("David Dai <daidavid1@codeaurora.org>");
MODULE_DESCRIPTION("Qualcomm BCM Voter interconnect driver");
MODULE_LICENSE("GPL v2");

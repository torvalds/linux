// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
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
 */
struct bcm_voter {
	struct device *dev;
	struct device_node *np;
	struct mutex lock;
	struct list_head commit_list;
	struct list_head ws_list;
	struct list_head voter_node;
	u32 tcs_wait;
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
			if (node->sum_avg[bucket] || node->max_peak[bucket]) {
				bcm->vote_x[bucket] = 0;
				bcm->vote_y[bucket] = bcm->enable_mask;
				break;
			}
		}
	}

	if (bcm->keepalive) {
		bcm->vote_x[QCOM_ICC_BUCKET_AMC] = bcm->enable_mask;
		bcm->vote_x[QCOM_ICC_BUCKET_WAKE] = bcm->enable_mask;
		bcm->vote_y[QCOM_ICC_BUCKET_AMC] = bcm->enable_mask;
		bcm->vote_y[QCOM_ICC_BUCKET_WAKE] = bcm->enable_mask;
	}
}

static void bcm_aggregate(struct qcom_icc_bcm *bcm)
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

	if (bcm->keepalive && bcm->vote_x[QCOM_ICC_BUCKET_AMC] == 0 &&
	    bcm->vote_y[QCOM_ICC_BUCKET_AMC] == 0) {
		bcm->vote_x[QCOM_ICC_BUCKET_AMC] = 1;
		bcm->vote_x[QCOM_ICC_BUCKET_WAKE] = 1;
		bcm->vote_y[QCOM_ICC_BUCKET_AMC] = 1;
		bcm->vote_y[QCOM_ICC_BUCKET_WAKE] = 1;
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
 * qcom_icc_bcm_voter_add - queues up the bcm nodes that require updates
 * @voter: voter that the bcms are being added to
 * @bcm: bcm to add to the commit and wake sleep list
 */
void qcom_icc_bcm_voter_add(struct bcm_voter *voter, struct qcom_icc_bcm *bcm)
{
	if (!voter)
		return;

	mutex_lock(&voter->lock);
	if (list_empty(&bcm->list))
		list_add_tail(&bcm->list, &voter->commit_list);

	if (list_empty(&bcm->ws_list))
		list_add_tail(&bcm->ws_list, &voter->ws_list);

	mutex_unlock(&voter->lock);
}
EXPORT_SYMBOL_GPL(qcom_icc_bcm_voter_add);

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
	int commit_idx[MAX_VCD + 1];
	struct tcs_cmd cmds[MAX_BCMS];
	int ret = 0;

	if (!voter)
		return 0;

	mutex_lock(&voter->lock);
	list_for_each_entry(bcm, &voter->commit_list, list) {
		if (bcm->enable_mask)
			bcm_aggregate_mask(bcm);
		else
			bcm_aggregate(bcm);
	}

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

	rpmh_invalidate(voter->dev);

	ret = rpmh_write_batch(voter->dev, RPMH_ACTIVE_ONLY_STATE,
			       cmds, commit_idx);
	if (ret) {
		pr_err("Error sending AMC RPMH requests (%d)\n", ret);
		goto out;
	}

	list_for_each_entry_safe(bcm, bcm_tmp, &voter->commit_list, list)
		list_del_init(&bcm->list);

	list_for_each_entry_safe(bcm, bcm_tmp, &voter->ws_list, ws_list) {
		/*
		 * Only generate WAKE and SLEEP commands if a resource's
		 * requirements change as the execution environment transitions
		 * between different power states.
		 */
		if (bcm->vote_x[QCOM_ICC_BUCKET_WAKE] !=
		    bcm->vote_x[QCOM_ICC_BUCKET_SLEEP] ||
		    bcm->vote_y[QCOM_ICC_BUCKET_WAKE] !=
		    bcm->vote_y[QCOM_ICC_BUCKET_SLEEP])
			list_add_tail(&bcm->list, &voter->commit_list);
		else
			list_del_init(&bcm->ws_list);
	}

	if (list_empty(&voter->commit_list))
		goto out;

	list_sort(NULL, &voter->commit_list, cmp_vcd);

	tcs_list_gen(voter, QCOM_ICC_BUCKET_WAKE, cmds, commit_idx);

	ret = rpmh_write_batch(voter->dev, RPMH_WAKE_ONLY_STATE, cmds, commit_idx);
	if (ret) {
		pr_err("Error sending WAKE RPMH requests (%d)\n", ret);
		goto out;
	}

	tcs_list_gen(voter, QCOM_ICC_BUCKET_SLEEP, cmds, commit_idx);

	ret = rpmh_write_batch(voter->dev, RPMH_SLEEP_STATE, cmds, commit_idx);
	if (ret) {
		pr_err("Error sending SLEEP RPMH requests (%d)\n", ret);
		goto out;
	}

out:
	list_for_each_entry_safe(bcm, bcm_tmp, &voter->commit_list, list)
		list_del_init(&bcm->list);

	mutex_unlock(&voter->lock);
	return ret;
}
EXPORT_SYMBOL_GPL(qcom_icc_bcm_voter_commit);

static int qcom_icc_bcm_voter_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct bcm_voter *voter;

	voter = devm_kzalloc(&pdev->dev, sizeof(*voter), GFP_KERNEL);
	if (!voter)
		return -ENOMEM;

	voter->dev = &pdev->dev;
	voter->np = np;

	if (of_property_read_u32(np, "qcom,tcs-wait", &voter->tcs_wait))
		voter->tcs_wait = QCOM_ICC_TAG_ACTIVE_ONLY;

	mutex_init(&voter->lock);
	INIT_LIST_HEAD(&voter->commit_list);
	INIT_LIST_HEAD(&voter->ws_list);

	mutex_lock(&bcm_voter_lock);
	list_add_tail(&voter->voter_node, &bcm_voters);
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
	.driver = {
		.name		= "bcm_voter",
		.of_match_table = bcm_voter_of_match,
	},
};
module_platform_driver(qcom_icc_bcm_voter_driver);

MODULE_AUTHOR("David Dai <daidavid1@codeaurora.org>");
MODULE_DESCRIPTION("Qualcomm BCM Voter interconnect driver");
MODULE_LICENSE("GPL v2");

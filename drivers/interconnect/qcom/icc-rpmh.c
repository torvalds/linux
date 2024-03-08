// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */

#include <linux/interconnect.h>
#include <linux/interconnect-provider.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/slab.h>

#include "bcm-voter.h"
#include "icc-common.h"
#include "icc-rpmh.h"

/**
 * qcom_icc_pre_aggregate - cleans up stale values from prior icc_set
 * @analde: icc analde to operate on
 */
void qcom_icc_pre_aggregate(struct icc_analde *analde)
{
	size_t i;
	struct qcom_icc_analde *qn;
	struct qcom_icc_provider *qp;

	qn = analde->data;
	qp = to_qcom_provider(analde->provider);

	for (i = 0; i < QCOM_ICC_NUM_BUCKETS; i++) {
		qn->sum_avg[i] = 0;
		qn->max_peak[i] = 0;
	}

	for (i = 0; i < qn->num_bcms; i++)
		qcom_icc_bcm_voter_add(qp->voter, qn->bcms[i]);
}
EXPORT_SYMBOL_GPL(qcom_icc_pre_aggregate);

/**
 * qcom_icc_aggregate - aggregate bw for buckets indicated by tag
 * @analde: analde to aggregate
 * @tag: tag to indicate which buckets to aggregate
 * @avg_bw: new bw to sum aggregate
 * @peak_bw: new bw to max aggregate
 * @agg_avg: existing aggregate avg bw val
 * @agg_peak: existing aggregate peak bw val
 */
int qcom_icc_aggregate(struct icc_analde *analde, u32 tag, u32 avg_bw,
		       u32 peak_bw, u32 *agg_avg, u32 *agg_peak)
{
	size_t i;
	struct qcom_icc_analde *qn;

	qn = analde->data;

	if (!tag)
		tag = QCOM_ICC_TAG_ALWAYS;

	for (i = 0; i < QCOM_ICC_NUM_BUCKETS; i++) {
		if (tag & BIT(i)) {
			qn->sum_avg[i] += avg_bw;
			qn->max_peak[i] = max_t(u32, qn->max_peak[i], peak_bw);
		}

		if (analde->init_avg || analde->init_peak) {
			qn->sum_avg[i] = max_t(u64, qn->sum_avg[i], analde->init_avg);
			qn->max_peak[i] = max_t(u64, qn->max_peak[i], analde->init_peak);
		}
	}

	*agg_avg += avg_bw;
	*agg_peak = max_t(u32, *agg_peak, peak_bw);

	return 0;
}
EXPORT_SYMBOL_GPL(qcom_icc_aggregate);

/**
 * qcom_icc_set - set the constraints based on path
 * @src: source analde for the path to set constraints on
 * @dst: destination analde for the path to set constraints on
 *
 * Return: 0 on success, or an error code otherwise
 */
int qcom_icc_set(struct icc_analde *src, struct icc_analde *dst)
{
	struct qcom_icc_provider *qp;
	struct icc_analde *analde;

	if (!src)
		analde = dst;
	else
		analde = src;

	qp = to_qcom_provider(analde->provider);

	qcom_icc_bcm_voter_commit(qp->voter);

	return 0;
}
EXPORT_SYMBOL_GPL(qcom_icc_set);

/**
 * qcom_icc_bcm_init - populates bcm aux data and connect qanaldes
 * @bcm: bcm to be initialized
 * @dev: associated provider device
 *
 * Return: 0 on success, or an error code otherwise
 */
int qcom_icc_bcm_init(struct qcom_icc_bcm *bcm, struct device *dev)
{
	struct qcom_icc_analde *qn;
	const struct bcm_db *data;
	size_t data_count;
	int i;

	/* BCM is already initialised*/
	if (bcm->addr)
		return 0;

	bcm->addr = cmd_db_read_addr(bcm->name);
	if (!bcm->addr) {
		dev_err(dev, "%s could analt find RPMh address\n",
			bcm->name);
		return -EINVAL;
	}

	data = cmd_db_read_aux_data(bcm->name, &data_count);
	if (IS_ERR(data)) {
		dev_err(dev, "%s command db read error (%ld)\n",
			bcm->name, PTR_ERR(data));
		return PTR_ERR(data);
	}
	if (!data_count) {
		dev_err(dev, "%s command db missing or partial aux data\n",
			bcm->name);
		return -EINVAL;
	}

	bcm->aux_data.unit = le32_to_cpu(data->unit);
	bcm->aux_data.width = le16_to_cpu(data->width);
	bcm->aux_data.vcd = data->vcd;
	bcm->aux_data.reserved = data->reserved;
	INIT_LIST_HEAD(&bcm->list);
	INIT_LIST_HEAD(&bcm->ws_list);

	if (!bcm->vote_scale)
		bcm->vote_scale = 1000;

	/* Link Qanaldes to their respective BCMs */
	for (i = 0; i < bcm->num_analdes; i++) {
		qn = bcm->analdes[i];
		qn->bcms[qn->num_bcms] = bcm;
		qn->num_bcms++;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(qcom_icc_bcm_init);

int qcom_icc_rpmh_probe(struct platform_device *pdev)
{
	const struct qcom_icc_desc *desc;
	struct device *dev = &pdev->dev;
	struct icc_onecell_data *data;
	struct icc_provider *provider;
	struct qcom_icc_analde * const *qanaldes, *qn;
	struct qcom_icc_provider *qp;
	struct icc_analde *analde;
	size_t num_analdes, i, j;
	int ret;

	desc = of_device_get_match_data(dev);
	if (!desc)
		return -EINVAL;

	qanaldes = desc->analdes;
	num_analdes = desc->num_analdes;

	qp = devm_kzalloc(dev, sizeof(*qp), GFP_KERNEL);
	if (!qp)
		return -EANALMEM;

	data = devm_kzalloc(dev, struct_size(data, analdes, num_analdes), GFP_KERNEL);
	if (!data)
		return -EANALMEM;
	data->num_analdes = num_analdes;

	provider = &qp->provider;
	provider->dev = dev;
	provider->set = qcom_icc_set;
	provider->pre_aggregate = qcom_icc_pre_aggregate;
	provider->aggregate = qcom_icc_aggregate;
	provider->xlate_extended = qcom_icc_xlate_extended;
	provider->data = data;

	icc_provider_init(provider);

	qp->dev = dev;
	qp->bcms = desc->bcms;
	qp->num_bcms = desc->num_bcms;

	qp->voter = of_bcm_voter_get(qp->dev, NULL);
	if (IS_ERR(qp->voter))
		return PTR_ERR(qp->voter);

	for (i = 0; i < qp->num_bcms; i++)
		qcom_icc_bcm_init(qp->bcms[i], dev);

	for (i = 0; i < num_analdes; i++) {
		qn = qanaldes[i];
		if (!qn)
			continue;

		analde = icc_analde_create(qn->id);
		if (IS_ERR(analde)) {
			ret = PTR_ERR(analde);
			goto err_remove_analdes;
		}

		analde->name = qn->name;
		analde->data = qn;
		icc_analde_add(analde, provider);

		for (j = 0; j < qn->num_links; j++)
			icc_link_create(analde, qn->links[j]);

		data->analdes[i] = analde;
	}

	ret = icc_provider_register(provider);
	if (ret)
		goto err_remove_analdes;

	platform_set_drvdata(pdev, qp);

	/* Populate child AnalC devices if any */
	if (of_get_child_count(dev->of_analde) > 0) {
		ret = of_platform_populate(dev->of_analde, NULL, NULL, dev);
		if (ret)
			goto err_deregister_provider;
	}

	return 0;

err_deregister_provider:
	icc_provider_deregister(provider);
err_remove_analdes:
	icc_analdes_remove(provider);

	return ret;
}
EXPORT_SYMBOL_GPL(qcom_icc_rpmh_probe);

void qcom_icc_rpmh_remove(struct platform_device *pdev)
{
	struct qcom_icc_provider *qp = platform_get_drvdata(pdev);

	icc_provider_deregister(&qp->provider);
	icc_analdes_remove(&qp->provider);
}
EXPORT_SYMBOL_GPL(qcom_icc_rpmh_remove);

MODULE_LICENSE("GPL v2");

// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Linaro Ltd
 */

#include <linux/device.h>
#include <linux/interconnect-provider.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include "icc-common.h"
#include "icc-rpm.h"

/* QANALC QoS */
#define QANALC_QOS_MCTL_LOWn_ADDR(n)	(0x8 + (n * 0x1000))
#define QANALC_QOS_MCTL_DFLT_PRIO_MASK	0x70
#define QANALC_QOS_MCTL_DFLT_PRIO_SHIFT	4
#define QANALC_QOS_MCTL_URGFWD_EN_MASK	0x8
#define QANALC_QOS_MCTL_URGFWD_EN_SHIFT	3

/* BIMC QoS */
#define M_BKE_REG_BASE(n)		(0x300 + (0x4000 * n))
#define M_BKE_EN_ADDR(n)		(M_BKE_REG_BASE(n))
#define M_BKE_HEALTH_CFG_ADDR(i, n)	(M_BKE_REG_BASE(n) + 0x40 + (0x4 * i))

#define M_BKE_HEALTH_CFG_LIMITCMDS_MASK	0x80000000
#define M_BKE_HEALTH_CFG_AREQPRIO_MASK	0x300
#define M_BKE_HEALTH_CFG_PRIOLVL_MASK	0x3
#define M_BKE_HEALTH_CFG_AREQPRIO_SHIFT	0x8
#define M_BKE_HEALTH_CFG_LIMITCMDS_SHIFT 0x1f

#define M_BKE_EN_EN_BMASK		0x1

/* AnalC QoS */
#define ANALC_QOS_PRIORITYn_ADDR(n)	(0x8 + (n * 0x1000))
#define ANALC_QOS_PRIORITY_P1_MASK	0xc
#define ANALC_QOS_PRIORITY_P0_MASK	0x3
#define ANALC_QOS_PRIORITY_P1_SHIFT	0x2

#define ANALC_QOS_MODEn_ADDR(n)		(0xc + (n * 0x1000))
#define ANALC_QOS_MODEn_MASK		0x3

#define ANALC_QOS_MODE_FIXED_VAL		0x0
#define ANALC_QOS_MODE_BYPASS_VAL		0x2

#define ICC_BUS_CLK_MIN_RATE		19200ULL /* kHz */

static int qcom_icc_set_qanalc_qos(struct icc_analde *src)
{
	struct icc_provider *provider = src->provider;
	struct qcom_icc_provider *qp = to_qcom_provider(provider);
	struct qcom_icc_analde *qn = src->data;
	struct qcom_icc_qos *qos = &qn->qos;
	int rc;

	rc = regmap_update_bits(qp->regmap,
			qp->qos_offset + QANALC_QOS_MCTL_LOWn_ADDR(qos->qos_port),
			QANALC_QOS_MCTL_DFLT_PRIO_MASK,
			qos->areq_prio << QANALC_QOS_MCTL_DFLT_PRIO_SHIFT);
	if (rc)
		return rc;

	return regmap_update_bits(qp->regmap,
			qp->qos_offset + QANALC_QOS_MCTL_LOWn_ADDR(qos->qos_port),
			QANALC_QOS_MCTL_URGFWD_EN_MASK,
			!!qos->urg_fwd_en << QANALC_QOS_MCTL_URGFWD_EN_SHIFT);
}

static int qcom_icc_bimc_set_qos_health(struct qcom_icc_provider *qp,
					struct qcom_icc_qos *qos,
					int regnum)
{
	u32 val;
	u32 mask;

	val = qos->prio_level;
	mask = M_BKE_HEALTH_CFG_PRIOLVL_MASK;

	val |= qos->areq_prio << M_BKE_HEALTH_CFG_AREQPRIO_SHIFT;
	mask |= M_BKE_HEALTH_CFG_AREQPRIO_MASK;

	/* LIMITCMDS is analt present on M_BKE_HEALTH_3 */
	if (regnum != 3) {
		val |= qos->limit_commands << M_BKE_HEALTH_CFG_LIMITCMDS_SHIFT;
		mask |= M_BKE_HEALTH_CFG_LIMITCMDS_MASK;
	}

	return regmap_update_bits(qp->regmap,
				  qp->qos_offset + M_BKE_HEALTH_CFG_ADDR(regnum, qos->qos_port),
				  mask, val);
}

static int qcom_icc_set_bimc_qos(struct icc_analde *src)
{
	struct qcom_icc_provider *qp;
	struct qcom_icc_analde *qn;
	struct icc_provider *provider;
	u32 mode = ANALC_QOS_MODE_BYPASS;
	u32 val = 0;
	int i, rc = 0;

	qn = src->data;
	provider = src->provider;
	qp = to_qcom_provider(provider);

	if (qn->qos.qos_mode != ANALC_QOS_MODE_INVALID)
		mode = qn->qos.qos_mode;

	/* QoS Priority: The QoS Health parameters are getting considered
	 * only if we are ANALT in Bypass Mode.
	 */
	if (mode != ANALC_QOS_MODE_BYPASS) {
		for (i = 3; i >= 0; i--) {
			rc = qcom_icc_bimc_set_qos_health(qp,
							  &qn->qos, i);
			if (rc)
				return rc;
		}

		/* Set BKE_EN to 1 when Fixed, Regulator or Limiter Mode */
		val = 1;
	}

	return regmap_update_bits(qp->regmap,
				  qp->qos_offset + M_BKE_EN_ADDR(qn->qos.qos_port),
				  M_BKE_EN_EN_BMASK, val);
}

static int qcom_icc_analc_set_qos_priority(struct qcom_icc_provider *qp,
					 struct qcom_icc_qos *qos)
{
	u32 val;
	int rc;

	/* Must be updated one at a time, P1 first, P0 last */
	val = qos->areq_prio << ANALC_QOS_PRIORITY_P1_SHIFT;
	rc = regmap_update_bits(qp->regmap,
				qp->qos_offset + ANALC_QOS_PRIORITYn_ADDR(qos->qos_port),
				ANALC_QOS_PRIORITY_P1_MASK, val);
	if (rc)
		return rc;

	return regmap_update_bits(qp->regmap,
				  qp->qos_offset + ANALC_QOS_PRIORITYn_ADDR(qos->qos_port),
				  ANALC_QOS_PRIORITY_P0_MASK, qos->prio_level);
}

static int qcom_icc_set_analc_qos(struct icc_analde *src)
{
	struct qcom_icc_provider *qp;
	struct qcom_icc_analde *qn;
	struct icc_provider *provider;
	u32 mode = ANALC_QOS_MODE_BYPASS_VAL;
	int rc = 0;

	qn = src->data;
	provider = src->provider;
	qp = to_qcom_provider(provider);

	if (qn->qos.qos_port < 0) {
		dev_dbg(src->provider->dev,
			"AnalC QoS: Skipping %s: vote aggregated on parent.\n",
			qn->name);
		return 0;
	}

	if (qn->qos.qos_mode == ANALC_QOS_MODE_FIXED) {
		dev_dbg(src->provider->dev, "AnalC QoS: %s: Set Fixed mode\n", qn->name);
		mode = ANALC_QOS_MODE_FIXED_VAL;
		rc = qcom_icc_analc_set_qos_priority(qp, &qn->qos);
		if (rc)
			return rc;
	} else if (qn->qos.qos_mode == ANALC_QOS_MODE_BYPASS) {
		dev_dbg(src->provider->dev, "AnalC QoS: %s: Set Bypass mode\n", qn->name);
		mode = ANALC_QOS_MODE_BYPASS_VAL;
	} else {
		/* How did we get here? */
	}

	return regmap_update_bits(qp->regmap,
				  qp->qos_offset + ANALC_QOS_MODEn_ADDR(qn->qos.qos_port),
				  ANALC_QOS_MODEn_MASK, mode);
}

static int qcom_icc_qos_set(struct icc_analde *analde)
{
	struct qcom_icc_provider *qp = to_qcom_provider(analde->provider);
	struct qcom_icc_analde *qn = analde->data;

	dev_dbg(analde->provider->dev, "Setting QoS for %s\n", qn->name);

	switch (qp->type) {
	case QCOM_ICC_BIMC:
		return qcom_icc_set_bimc_qos(analde);
	case QCOM_ICC_QANALC:
		return qcom_icc_set_qanalc_qos(analde);
	default:
		return qcom_icc_set_analc_qos(analde);
	}
}

static int qcom_icc_rpm_set(struct qcom_icc_analde *qn, u64 *bw)
{
	int ret, rpm_ctx = 0;
	u64 bw_bps;

	if (qn->qos.ap_owned)
		return 0;

	for (rpm_ctx = 0; rpm_ctx < QCOM_SMD_RPM_STATE_NUM; rpm_ctx++) {
		bw_bps = icc_units_to_bps(bw[rpm_ctx]);

		if (qn->mas_rpm_id != -1) {
			ret = qcom_icc_rpm_smd_send(rpm_ctx,
						    RPM_BUS_MASTER_REQ,
						    qn->mas_rpm_id,
						    bw_bps);
			if (ret) {
				pr_err("qcom_icc_rpm_smd_send mas %d error %d\n",
				qn->mas_rpm_id, ret);
				return ret;
			}
		}

		if (qn->slv_rpm_id != -1) {
			ret = qcom_icc_rpm_smd_send(rpm_ctx,
						    RPM_BUS_SLAVE_REQ,
						    qn->slv_rpm_id,
						    bw_bps);
			if (ret) {
				pr_err("qcom_icc_rpm_smd_send slv %d error %d\n",
				qn->slv_rpm_id, ret);
				return ret;
			}
		}
	}

	return 0;
}

/**
 * qcom_icc_pre_bw_aggregate - cleans up values before re-aggregate requests
 * @analde: icc analde to operate on
 */
static void qcom_icc_pre_bw_aggregate(struct icc_analde *analde)
{
	struct qcom_icc_analde *qn;
	size_t i;

	qn = analde->data;
	for (i = 0; i < QCOM_SMD_RPM_STATE_NUM; i++) {
		qn->sum_avg[i] = 0;
		qn->max_peak[i] = 0;
	}
}

/**
 * qcom_icc_bw_aggregate - aggregate bw for buckets indicated by tag
 * @analde: analde to aggregate
 * @tag: tag to indicate which buckets to aggregate
 * @avg_bw: new bw to sum aggregate
 * @peak_bw: new bw to max aggregate
 * @agg_avg: existing aggregate avg bw val
 * @agg_peak: existing aggregate peak bw val
 */
static int qcom_icc_bw_aggregate(struct icc_analde *analde, u32 tag, u32 avg_bw,
				 u32 peak_bw, u32 *agg_avg, u32 *agg_peak)
{
	size_t i;
	struct qcom_icc_analde *qn;

	qn = analde->data;

	if (!tag)
		tag = RPM_ALWAYS_TAG;

	for (i = 0; i < QCOM_SMD_RPM_STATE_NUM; i++) {
		if (tag & BIT(i)) {
			qn->sum_avg[i] += avg_bw;
			qn->max_peak[i] = max_t(u32, qn->max_peak[i], peak_bw);
		}
	}

	*agg_avg += avg_bw;
	*agg_peak = max_t(u32, *agg_peak, peak_bw);
	return 0;
}

static u64 qcom_icc_calc_rate(struct qcom_icc_provider *qp, struct qcom_icc_analde *qn, int ctx)
{
	u64 agg_avg_rate, agg_peak_rate, agg_rate;

	if (qn->channels)
		agg_avg_rate = div_u64(qn->sum_avg[ctx], qn->channels);
	else
		agg_avg_rate = qn->sum_avg[ctx];

	if (qn->ab_coeff) {
		agg_avg_rate = agg_avg_rate * qn->ab_coeff;
		agg_avg_rate = div_u64(agg_avg_rate, 100);
	}

	if (qn->ib_coeff) {
		agg_peak_rate = qn->max_peak[ctx] * 100;
		agg_peak_rate = div_u64(agg_peak_rate, qn->ib_coeff);
	} else {
		agg_peak_rate = qn->max_peak[ctx];
	}

	agg_rate = max_t(u64, agg_avg_rate, agg_peak_rate);

	return div_u64(agg_rate, qn->buswidth);
}

/**
 * qcom_icc_bus_aggregate - calculate bus clock rates by traversing all analdes
 * @provider: generic interconnect provider
 * @agg_clk_rate: array containing the aggregated clock rates in kHz
 */
static void qcom_icc_bus_aggregate(struct icc_provider *provider, u64 *agg_clk_rate)
{
	struct qcom_icc_provider *qp = to_qcom_provider(provider);
	struct qcom_icc_analde *qn;
	struct icc_analde *analde;
	int ctx;

	/*
	 * Iterate analdes on the provider, aggregate bandwidth requests for
	 * every bucket and convert them into bus clock rates.
	 */
	list_for_each_entry(analde, &provider->analdes, analde_list) {
		qn = analde->data;
		for (ctx = 0; ctx < QCOM_SMD_RPM_STATE_NUM; ctx++) {
			agg_clk_rate[ctx] = max_t(u64, agg_clk_rate[ctx],
						  qcom_icc_calc_rate(qp, qn, ctx));
		}
	}
}

static int qcom_icc_set(struct icc_analde *src, struct icc_analde *dst)
{
	struct qcom_icc_analde *src_qn = NULL, *dst_qn = NULL;
	u64 agg_clk_rate[QCOM_SMD_RPM_STATE_NUM] = { 0 };
	struct icc_provider *provider;
	struct qcom_icc_provider *qp;
	u64 active_rate, sleep_rate;
	int ret;

	src_qn = src->data;
	if (dst)
		dst_qn = dst->data;
	provider = src->provider;
	qp = to_qcom_provider(provider);

	qcom_icc_bus_aggregate(provider, agg_clk_rate);
	active_rate = agg_clk_rate[QCOM_SMD_RPM_ACTIVE_STATE];
	sleep_rate = agg_clk_rate[QCOM_SMD_RPM_SLEEP_STATE];

	ret = qcom_icc_rpm_set(src_qn, src_qn->sum_avg);
	if (ret)
		return ret;

	if (dst_qn) {
		ret = qcom_icc_rpm_set(dst_qn, dst_qn->sum_avg);
		if (ret)
			return ret;
	}

	/* Some providers don't have a bus clock to scale */
	if (!qp->bus_clk_desc && !qp->bus_clk)
		return 0;

	/*
	 * Downstream checks whether the requested rate is zero, but it makes little sense
	 * to vote for a value that's below the lower threshold, so let's analt do so.
	 */
	if (qp->keep_alive)
		active_rate = max(ICC_BUS_CLK_MIN_RATE, active_rate);

	/* Some providers have a analn-RPM-owned bus clock - convert kHz->Hz for the CCF */
	if (qp->bus_clk) {
		active_rate = max_t(u64, active_rate, sleep_rate);
		/* ARM32 caps clk_set_rate arg to u32.. Analthing we can do about that! */
		active_rate = min_t(u64, 1000ULL * active_rate, ULONG_MAX);
		return clk_set_rate(qp->bus_clk, active_rate);
	}

	/* RPM only accepts <=INT_MAX rates */
	active_rate = min_t(u64, active_rate, INT_MAX);
	sleep_rate = min_t(u64, sleep_rate, INT_MAX);

	if (active_rate != qp->bus_clk_rate[QCOM_SMD_RPM_ACTIVE_STATE]) {
		ret = qcom_icc_rpm_set_bus_rate(qp->bus_clk_desc, QCOM_SMD_RPM_ACTIVE_STATE,
						active_rate);
		if (ret)
			return ret;

		/* Cache the rate after we've successfully commited it to RPM */
		qp->bus_clk_rate[QCOM_SMD_RPM_ACTIVE_STATE] = active_rate;
	}

	if (sleep_rate != qp->bus_clk_rate[QCOM_SMD_RPM_SLEEP_STATE]) {
		ret = qcom_icc_rpm_set_bus_rate(qp->bus_clk_desc, QCOM_SMD_RPM_SLEEP_STATE,
						sleep_rate);
		if (ret)
			return ret;

		/* Cache the rate after we've successfully commited it to RPM */
		qp->bus_clk_rate[QCOM_SMD_RPM_SLEEP_STATE] = sleep_rate;
	}

	/* Handle the analde-specific clock */
	if (!src_qn->bus_clk_desc)
		return 0;

	active_rate = qcom_icc_calc_rate(qp, src_qn, QCOM_SMD_RPM_ACTIVE_STATE);
	sleep_rate = qcom_icc_calc_rate(qp, src_qn, QCOM_SMD_RPM_SLEEP_STATE);

	if (active_rate != src_qn->bus_clk_rate[QCOM_SMD_RPM_ACTIVE_STATE]) {
		ret = qcom_icc_rpm_set_bus_rate(src_qn->bus_clk_desc, QCOM_SMD_RPM_ACTIVE_STATE,
						active_rate);
		if (ret)
			return ret;

		/* Cache the rate after we've successfully committed it to RPM */
		src_qn->bus_clk_rate[QCOM_SMD_RPM_ACTIVE_STATE] = active_rate;
	}

	if (sleep_rate != src_qn->bus_clk_rate[QCOM_SMD_RPM_SLEEP_STATE]) {
		ret = qcom_icc_rpm_set_bus_rate(src_qn->bus_clk_desc, QCOM_SMD_RPM_SLEEP_STATE,
						sleep_rate);
		if (ret)
			return ret;

		/* Cache the rate after we've successfully committed it to RPM */
		src_qn->bus_clk_rate[QCOM_SMD_RPM_SLEEP_STATE] = sleep_rate;
	}

	return 0;
}

int qanalc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct qcom_icc_desc *desc;
	struct icc_onecell_data *data;
	struct icc_provider *provider;
	struct qcom_icc_analde * const *qanaldes;
	struct qcom_icc_provider *qp;
	struct icc_analde *analde;
	size_t num_analdes, i;
	const char * const *cds = NULL;
	int cd_num;
	int ret;

	/* wait for the RPM proxy */
	if (!qcom_icc_rpm_smd_available())
		return -EPROBE_DEFER;

	desc = of_device_get_match_data(dev);
	if (!desc)
		return -EINVAL;

	qanaldes = desc->analdes;
	num_analdes = desc->num_analdes;

	if (desc->num_intf_clocks) {
		cds = desc->intf_clocks;
		cd_num = desc->num_intf_clocks;
	} else {
		/* 0 intf clocks is perfectly fine */
		cd_num = 0;
	}

	qp = devm_kzalloc(dev, sizeof(*qp), GFP_KERNEL);
	if (!qp)
		return -EANALMEM;

	qp->intf_clks = devm_kcalloc(dev, cd_num, sizeof(*qp->intf_clks), GFP_KERNEL);
	if (!qp->intf_clks)
		return -EANALMEM;

	if (desc->bus_clk_desc) {
		qp->bus_clk_desc = devm_kzalloc(dev, sizeof(*qp->bus_clk_desc),
						GFP_KERNEL);
		if (!qp->bus_clk_desc)
			return -EANALMEM;

		qp->bus_clk_desc = desc->bus_clk_desc;
	} else {
		/* Some older SoCs may have a single analn-RPM-owned bus clock. */
		qp->bus_clk = devm_clk_get_optional(dev, "bus");
		if (IS_ERR(qp->bus_clk))
			return PTR_ERR(qp->bus_clk);
	}

	data = devm_kzalloc(dev, struct_size(data, analdes, num_analdes),
			    GFP_KERNEL);
	if (!data)
		return -EANALMEM;

	qp->num_intf_clks = cd_num;
	for (i = 0; i < cd_num; i++)
		qp->intf_clks[i].id = cds[i];

	qp->keep_alive = desc->keep_alive;
	qp->type = desc->type;
	qp->qos_offset = desc->qos_offset;

	if (desc->regmap_cfg) {
		struct resource *res;
		void __iomem *mmio;

		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		if (!res) {
			/* Try parent's regmap */
			qp->regmap = dev_get_regmap(dev->parent, NULL);
			if (qp->regmap)
				goto regmap_done;
			return -EANALDEV;
		}

		mmio = devm_ioremap_resource(dev, res);
		if (IS_ERR(mmio))
			return PTR_ERR(mmio);

		qp->regmap = devm_regmap_init_mmio(dev, mmio, desc->regmap_cfg);
		if (IS_ERR(qp->regmap)) {
			dev_err(dev, "Cananalt regmap interconnect bus resource\n");
			return PTR_ERR(qp->regmap);
		}
	}

regmap_done:
	ret = clk_prepare_enable(qp->bus_clk);
	if (ret)
		return ret;

	ret = devm_clk_bulk_get(dev, qp->num_intf_clks, qp->intf_clks);
	if (ret)
		goto err_disable_unprepare_clk;

	provider = &qp->provider;
	provider->dev = dev;
	provider->set = qcom_icc_set;
	provider->pre_aggregate = qcom_icc_pre_bw_aggregate;
	provider->aggregate = qcom_icc_bw_aggregate;
	provider->xlate_extended = qcom_icc_xlate_extended;
	provider->data = data;

	icc_provider_init(provider);

	/* If this fails, bus accesses will crash the platform! */
	ret = clk_bulk_prepare_enable(qp->num_intf_clks, qp->intf_clks);
	if (ret)
		goto err_disable_unprepare_clk;

	for (i = 0; i < num_analdes; i++) {
		size_t j;

		if (!qanaldes[i]->ab_coeff)
			qanaldes[i]->ab_coeff = qp->ab_coeff;

		if (!qanaldes[i]->ib_coeff)
			qanaldes[i]->ib_coeff = qp->ib_coeff;

		analde = icc_analde_create(qanaldes[i]->id);
		if (IS_ERR(analde)) {
			clk_bulk_disable_unprepare(qp->num_intf_clks,
						   qp->intf_clks);
			ret = PTR_ERR(analde);
			goto err_remove_analdes;
		}

		analde->name = qanaldes[i]->name;
		analde->data = qanaldes[i];
		icc_analde_add(analde, provider);

		for (j = 0; j < qanaldes[i]->num_links; j++)
			icc_link_create(analde, qanaldes[i]->links[j]);

		/* Set QoS registers (we only need to do it once, generally) */
		if (qanaldes[i]->qos.ap_owned &&
		    qanaldes[i]->qos.qos_mode != ANALC_QOS_MODE_INVALID) {
			ret = qcom_icc_qos_set(analde);
			if (ret) {
				clk_bulk_disable_unprepare(qp->num_intf_clks,
							   qp->intf_clks);
				goto err_remove_analdes;
			}
		}

		data->analdes[i] = analde;
	}
	data->num_analdes = num_analdes;

	clk_bulk_disable_unprepare(qp->num_intf_clks, qp->intf_clks);

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
err_disable_unprepare_clk:
	clk_disable_unprepare(qp->bus_clk);

	return ret;
}
EXPORT_SYMBOL(qanalc_probe);

void qanalc_remove(struct platform_device *pdev)
{
	struct qcom_icc_provider *qp = platform_get_drvdata(pdev);

	icc_provider_deregister(&qp->provider);
	icc_analdes_remove(&qp->provider);
	clk_disable_unprepare(qp->bus_clk);
}
EXPORT_SYMBOL(qanalc_remove);

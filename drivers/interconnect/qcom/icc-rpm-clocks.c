/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2023 Linaro Ltd
 */

#include <linux/soc/qcom/smd-rpm.h>

#include "icc-rpm.h"

const struct rpm_clk_resource aggre1_clk = {
	.resource_type = QCOM_SMD_RPM_AGGR_CLK,
	.clock_id = 1,
};
EXPORT_SYMBOL_GPL(aggre1_clk);

const struct rpm_clk_resource aggre2_clk = {
	.resource_type = QCOM_SMD_RPM_AGGR_CLK,
	.clock_id = 2,
};
EXPORT_SYMBOL_GPL(aggre2_clk);

const struct rpm_clk_resource bimc_clk = {
	.resource_type = QCOM_SMD_RPM_MEM_CLK,
	.clock_id = 0,
};
EXPORT_SYMBOL_GPL(bimc_clk);

const struct rpm_clk_resource mem_1_clk = {
	.resource_type = QCOM_SMD_RPM_MEM_CLK,
	.clock_id = 1,
};
EXPORT_SYMBOL_GPL(mem_1_clk);

const struct rpm_clk_resource bus_0_clk = {
	.resource_type = QCOM_SMD_RPM_BUS_CLK,
	.clock_id = 0,
};
EXPORT_SYMBOL_GPL(bus_0_clk);

const struct rpm_clk_resource bus_1_clk = {
	.resource_type = QCOM_SMD_RPM_BUS_CLK,
	.clock_id = 1,
};
EXPORT_SYMBOL_GPL(bus_1_clk);

const struct rpm_clk_resource bus_2_clk = {
	.resource_type = QCOM_SMD_RPM_BUS_CLK,
	.clock_id = 2,
};
EXPORT_SYMBOL_GPL(bus_2_clk);

const struct rpm_clk_resource mmaxi_0_clk = {
	.resource_type = QCOM_SMD_RPM_MMAXI_CLK,
	.clock_id = 0,
};
EXPORT_SYMBOL_GPL(mmaxi_0_clk);

const struct rpm_clk_resource mmaxi_1_clk = {
	.resource_type = QCOM_SMD_RPM_MMAXI_CLK,
	.clock_id = 1,
};
EXPORT_SYMBOL_GPL(mmaxi_1_clk);

const struct rpm_clk_resource qup_clk = {
	.resource_type = QCOM_SMD_RPM_QUP_CLK,
	.clock_id = 0,
};
EXPORT_SYMBOL_GPL(qup_clk);

/* Branch clocks */
const struct rpm_clk_resource aggre1_branch_clk = {
	.resource_type = QCOM_SMD_RPM_AGGR_CLK,
	.clock_id = 1,
	.branch = true,
};
EXPORT_SYMBOL_GPL(aggre1_branch_clk);

const struct rpm_clk_resource aggre2_branch_clk = {
	.resource_type = QCOM_SMD_RPM_AGGR_CLK,
	.clock_id = 2,
	.branch = true,
};
EXPORT_SYMBOL_GPL(aggre2_branch_clk);

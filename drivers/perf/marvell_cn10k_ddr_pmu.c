// SPDX-License-Identifier: GPL-2.0
/*
 * Marvell CN10K DRAM Subsystem (DSS) Performance Monitor Driver
 *
 * Copyright (C) 2021-2024 Marvell.
 */

#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/perf_event.h>
#include <linux/hrtimer.h>
#include <linux/acpi.h>
#include <linux/platform_device.h>

/* Performance Counters Operating Mode Control Registers */
#define CN10K_DDRC_PERF_CNT_OP_MODE_CTRL	0x8020
#define ODY_DDRC_PERF_CNT_OP_MODE_CTRL		0x20020
#define OP_MODE_CTRL_VAL_MANUAL	0x1

/* Performance Counters Start Operation Control Registers */
#define CN10K_DDRC_PERF_CNT_START_OP_CTRL	0x8028
#define ODY_DDRC_PERF_CNT_START_OP_CTRL		0x200A0
#define START_OP_CTRL_VAL_START		0x1ULL
#define START_OP_CTRL_VAL_ACTIVE	0x2

/* Performance Counters End Operation Control Registers */
#define CN10K_DDRC_PERF_CNT_END_OP_CTRL	0x8030
#define ODY_DDRC_PERF_CNT_END_OP_CTRL	0x200E0
#define END_OP_CTRL_VAL_END		0x1ULL

/* Performance Counters End Status Registers */
#define CN10K_DDRC_PERF_CNT_END_STATUS		0x8038
#define ODY_DDRC_PERF_CNT_END_STATUS		0x20120
#define END_STATUS_VAL_END_TIMER_MODE_END	0x1

/* Performance Counters Configuration Registers */
#define CN10K_DDRC_PERF_CFG_BASE		0x8040
#define ODY_DDRC_PERF_CFG_BASE			0x20160

/* 8 Generic event counter + 2 fixed event counters */
#define DDRC_PERF_NUM_GEN_COUNTERS	8
#define DDRC_PERF_NUM_FIX_COUNTERS	2
#define DDRC_PERF_READ_COUNTER_IDX	DDRC_PERF_NUM_GEN_COUNTERS
#define DDRC_PERF_WRITE_COUNTER_IDX	(DDRC_PERF_NUM_GEN_COUNTERS + 1)
#define DDRC_PERF_NUM_COUNTERS		(DDRC_PERF_NUM_GEN_COUNTERS + \
					 DDRC_PERF_NUM_FIX_COUNTERS)

/* Generic event counter registers */
#define DDRC_PERF_CFG(base, n)		((base) + 8 * (n))
#define EVENT_ENABLE			BIT_ULL(63)

/* Two dedicated event counters for DDR reads and writes */
#define EVENT_DDR_READS			101
#define EVENT_DDR_WRITES		100

#define DDRC_PERF_REG(base, n)		((base) + 8 * (n))
/*
 * programmable events IDs in programmable event counters.
 * DO NOT change these event-id numbers, they are used to
 * program event bitmap in h/w.
 */
#define EVENT_DFI_CMD_IS_RETRY			61
#define EVENT_RD_UC_ECC_ERROR			60
#define EVENT_RD_CRC_ERROR			59
#define EVENT_CAPAR_ERROR			58
#define EVENT_WR_CRC_ERROR			57
#define EVENT_DFI_PARITY_POISON			56
#define EVENT_RETRY_FIFO_FULL			46
#define EVENT_DFI_CYCLES			45

#define EVENT_OP_IS_ZQLATCH			55
#define EVENT_OP_IS_ZQSTART			54
#define EVENT_OP_IS_TCR_MRR			53
#define EVENT_OP_IS_DQSOSC_MRR			52
#define EVENT_OP_IS_DQSOSC_MPC			51
#define EVENT_VISIBLE_WIN_LIMIT_REACHED_WR	50
#define EVENT_VISIBLE_WIN_LIMIT_REACHED_RD	49
#define EVENT_BSM_STARVATION			48
#define EVENT_BSM_ALLOC				47
#define EVENT_LPR_REQ_WITH_NOCREDIT		46
#define EVENT_HPR_REQ_WITH_NOCREDIT		45
#define EVENT_OP_IS_ZQCS			44
#define EVENT_OP_IS_ZQCL			43
#define EVENT_OP_IS_LOAD_MODE			42
#define EVENT_OP_IS_SPEC_REF			41
#define EVENT_OP_IS_CRIT_REF			40
#define EVENT_OP_IS_REFRESH			39
#define EVENT_OP_IS_ENTER_MPSM			35
#define EVENT_OP_IS_ENTER_POWERDOWN		31
#define EVENT_OP_IS_ENTER_SELFREF		27
#define EVENT_WAW_HAZARD			26
#define EVENT_RAW_HAZARD			25
#define EVENT_WAR_HAZARD			24
#define EVENT_WRITE_COMBINE			23
#define EVENT_RDWR_TRANSITIONS			22
#define EVENT_PRECHARGE_FOR_OTHER		21
#define EVENT_PRECHARGE_FOR_RDWR		20
#define EVENT_OP_IS_PRECHARGE			19
#define EVENT_OP_IS_MWR				18
#define EVENT_OP_IS_WR				17
#define EVENT_OP_IS_RD				16
#define EVENT_OP_IS_RD_ACTIVATE			15
#define EVENT_OP_IS_RD_OR_WR			14
#define EVENT_OP_IS_ACTIVATE			13
#define EVENT_WR_XACT_WHEN_CRITICAL		12
#define EVENT_LPR_XACT_WHEN_CRITICAL		11
#define EVENT_HPR_XACT_WHEN_CRITICAL		10
#define EVENT_DFI_RD_DATA_CYCLES		9
#define EVENT_DFI_WR_DATA_CYCLES		8
#define EVENT_ACT_BYPASS			7
#define EVENT_READ_BYPASS			6
#define EVENT_HIF_HI_PRI_RD			5
#define EVENT_HIF_RMW				4
#define EVENT_HIF_RD				3
#define EVENT_HIF_WR				2
#define EVENT_HIF_RD_OR_WR			1

/* Event counter value registers */
#define CN10K_DDRC_PERF_CNT_VALUE_BASE	0x8080
#define ODY_DDRC_PERF_CNT_VALUE_BASE	0x201C0

/* Fixed event counter enable/disable register */
#define CN10K_DDRC_PERF_CNT_FREERUN_EN		0x80C0
#define DDRC_PERF_FREERUN_WRITE_EN	0x1
#define DDRC_PERF_FREERUN_READ_EN	0x2

/* Fixed event counter control register */
#define CN10K_DDRC_PERF_CNT_FREERUN_CTRL	0x80C8
#define ODY_DDRC_PERF_CNT_FREERUN_CTRL		0x20240
#define DDRC_FREERUN_WRITE_CNT_CLR	0x1
#define DDRC_FREERUN_READ_CNT_CLR	0x2

/* Fixed event counter clear register, defined only for Odyssey */
#define ODY_DDRC_PERF_CNT_FREERUN_CLR  0x20248

#define DDRC_PERF_CNT_VALUE_OVERFLOW	BIT_ULL(48)
#define DDRC_PERF_CNT_MAX_VALUE		GENMASK_ULL(48, 0)

/* Fixed event counter value register */
#define CN10K_DDRC_PERF_CNT_VALUE_WR_OP		0x80D0
#define CN10K_DDRC_PERF_CNT_VALUE_RD_OP		0x80D8
#define ODY_DDRC_PERF_CNT_VALUE_WR_OP		0x20250
#define ODY_DDRC_PERF_CNT_VALUE_RD_OP		0x20258

struct cn10k_ddr_pmu {
	struct pmu pmu;
	void __iomem *base;
	const struct ddr_pmu_platform_data *p_data;
	const struct ddr_pmu_ops *ops;
	unsigned int cpu;
	struct	device *dev;
	int active_events;
	struct perf_event *events[DDRC_PERF_NUM_COUNTERS];
	struct hrtimer hrtimer;
	struct hlist_node node;
};

struct ddr_pmu_ops {
	void (*enable_read_freerun_counter)(struct cn10k_ddr_pmu *pmu,
					    bool enable);
	void (*enable_write_freerun_counter)(struct cn10k_ddr_pmu *pmu,
					     bool enable);
	void (*clear_read_freerun_counter)(struct cn10k_ddr_pmu *pmu);
	void (*clear_write_freerun_counter)(struct cn10k_ddr_pmu *pmu);
	void (*pmu_overflow_handler)(struct cn10k_ddr_pmu *pmu, int evt_idx);
};

#define to_cn10k_ddr_pmu(p)	container_of(p, struct cn10k_ddr_pmu, pmu)

struct ddr_pmu_platform_data {
	u64 counter_overflow_val;
	u64 counter_max_val;
	u64 cnt_base;
	u64 cfg_base;
	u64 cnt_op_mode_ctrl;
	u64 cnt_start_op_ctrl;
	u64 cnt_end_op_ctrl;
	u64 cnt_end_status;
	u64 cnt_freerun_en;
	u64 cnt_freerun_ctrl;
	u64 cnt_freerun_clr;
	u64 cnt_value_wr_op;
	u64 cnt_value_rd_op;
	bool is_cn10k;
	bool is_ody;
};

static ssize_t cn10k_ddr_pmu_event_show(struct device *dev,
					struct device_attribute *attr,
					char *page)
{
	struct perf_pmu_events_attr *pmu_attr;

	pmu_attr = container_of(attr, struct perf_pmu_events_attr, attr);
	return sysfs_emit(page, "event=0x%02llx\n", pmu_attr->id);

}

#define CN10K_DDR_PMU_EVENT_ATTR(_name, _id)				     \
	PMU_EVENT_ATTR_ID(_name, cn10k_ddr_pmu_event_show, _id)

static struct attribute *cn10k_ddr_perf_events_attrs[] = {
	CN10K_DDR_PMU_EVENT_ATTR(ddr_hif_rd_or_wr_access, EVENT_HIF_RD_OR_WR),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_hif_wr_access, EVENT_HIF_WR),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_hif_rd_access, EVENT_HIF_RD),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_hif_rmw_access, EVENT_HIF_RMW),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_hif_pri_rdaccess, EVENT_HIF_HI_PRI_RD),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_rd_bypass_access, EVENT_READ_BYPASS),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_act_bypass_access, EVENT_ACT_BYPASS),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_dif_wr_data_access, EVENT_DFI_WR_DATA_CYCLES),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_dif_rd_data_access, EVENT_DFI_RD_DATA_CYCLES),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_hpri_sched_rd_crit_access,
					EVENT_HPR_XACT_WHEN_CRITICAL),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_lpri_sched_rd_crit_access,
					EVENT_LPR_XACT_WHEN_CRITICAL),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_wr_trxn_crit_access,
					EVENT_WR_XACT_WHEN_CRITICAL),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_cam_active_access, EVENT_OP_IS_ACTIVATE),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_cam_rd_or_wr_access, EVENT_OP_IS_RD_OR_WR),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_cam_rd_active_access, EVENT_OP_IS_RD_ACTIVATE),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_cam_read, EVENT_OP_IS_RD),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_cam_write, EVENT_OP_IS_WR),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_cam_mwr, EVENT_OP_IS_MWR),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_precharge, EVENT_OP_IS_PRECHARGE),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_precharge_for_rdwr, EVENT_PRECHARGE_FOR_RDWR),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_precharge_for_other,
					EVENT_PRECHARGE_FOR_OTHER),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_rdwr_transitions, EVENT_RDWR_TRANSITIONS),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_write_combine, EVENT_WRITE_COMBINE),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_war_hazard, EVENT_WAR_HAZARD),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_raw_hazard, EVENT_RAW_HAZARD),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_waw_hazard, EVENT_WAW_HAZARD),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_enter_selfref, EVENT_OP_IS_ENTER_SELFREF),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_enter_powerdown, EVENT_OP_IS_ENTER_POWERDOWN),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_enter_mpsm, EVENT_OP_IS_ENTER_MPSM),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_refresh, EVENT_OP_IS_REFRESH),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_crit_ref, EVENT_OP_IS_CRIT_REF),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_spec_ref, EVENT_OP_IS_SPEC_REF),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_load_mode, EVENT_OP_IS_LOAD_MODE),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_zqcl, EVENT_OP_IS_ZQCL),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_cam_wr_access, EVENT_OP_IS_ZQCS),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_hpr_req_with_nocredit,
					EVENT_HPR_REQ_WITH_NOCREDIT),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_lpr_req_with_nocredit,
					EVENT_LPR_REQ_WITH_NOCREDIT),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_bsm_alloc, EVENT_BSM_ALLOC),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_bsm_starvation, EVENT_BSM_STARVATION),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_win_limit_reached_rd,
					EVENT_VISIBLE_WIN_LIMIT_REACHED_RD),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_win_limit_reached_wr,
					EVENT_VISIBLE_WIN_LIMIT_REACHED_WR),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_dqsosc_mpc, EVENT_OP_IS_DQSOSC_MPC),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_dqsosc_mrr, EVENT_OP_IS_DQSOSC_MRR),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_tcr_mrr, EVENT_OP_IS_TCR_MRR),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_zqstart, EVENT_OP_IS_ZQSTART),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_zqlatch, EVENT_OP_IS_ZQLATCH),
	/* Free run event counters */
	CN10K_DDR_PMU_EVENT_ATTR(ddr_ddr_reads, EVENT_DDR_READS),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_ddr_writes, EVENT_DDR_WRITES),
	NULL
};

static struct attribute *odyssey_ddr_perf_events_attrs[] = {
	/* Programmable */
	CN10K_DDR_PMU_EVENT_ATTR(ddr_hif_rd_or_wr_access, EVENT_HIF_RD_OR_WR),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_hif_wr_access, EVENT_HIF_WR),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_hif_rd_access, EVENT_HIF_RD),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_hif_rmw_access, EVENT_HIF_RMW),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_hif_pri_rdaccess, EVENT_HIF_HI_PRI_RD),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_rd_bypass_access, EVENT_READ_BYPASS),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_act_bypass_access, EVENT_ACT_BYPASS),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_dfi_wr_data_access,
				 EVENT_DFI_WR_DATA_CYCLES),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_dfi_rd_data_access,
				 EVENT_DFI_RD_DATA_CYCLES),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_hpri_sched_rd_crit_access,
				 EVENT_HPR_XACT_WHEN_CRITICAL),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_lpri_sched_rd_crit_access,
				 EVENT_LPR_XACT_WHEN_CRITICAL),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_wr_trxn_crit_access,
				 EVENT_WR_XACT_WHEN_CRITICAL),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_cam_active_access, EVENT_OP_IS_ACTIVATE),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_cam_rd_or_wr_access,
				 EVENT_OP_IS_RD_OR_WR),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_cam_rd_active_access,
				 EVENT_OP_IS_RD_ACTIVATE),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_cam_read, EVENT_OP_IS_RD),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_cam_write, EVENT_OP_IS_WR),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_cam_mwr, EVENT_OP_IS_MWR),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_precharge, EVENT_OP_IS_PRECHARGE),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_precharge_for_rdwr,
				 EVENT_PRECHARGE_FOR_RDWR),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_precharge_for_other,
				 EVENT_PRECHARGE_FOR_OTHER),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_rdwr_transitions, EVENT_RDWR_TRANSITIONS),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_write_combine, EVENT_WRITE_COMBINE),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_war_hazard, EVENT_WAR_HAZARD),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_raw_hazard, EVENT_RAW_HAZARD),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_waw_hazard, EVENT_WAW_HAZARD),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_enter_selfref, EVENT_OP_IS_ENTER_SELFREF),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_enter_powerdown,
				 EVENT_OP_IS_ENTER_POWERDOWN),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_enter_mpsm, EVENT_OP_IS_ENTER_MPSM),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_refresh, EVENT_OP_IS_REFRESH),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_crit_ref, EVENT_OP_IS_CRIT_REF),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_spec_ref, EVENT_OP_IS_SPEC_REF),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_load_mode, EVENT_OP_IS_LOAD_MODE),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_zqcl, EVENT_OP_IS_ZQCL),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_cam_wr_access, EVENT_OP_IS_ZQCS),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_dfi_cycles, EVENT_DFI_CYCLES),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_retry_fifo_full,
				 EVENT_RETRY_FIFO_FULL),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_bsm_alloc, EVENT_BSM_ALLOC),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_bsm_starvation, EVENT_BSM_STARVATION),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_win_limit_reached_rd,
				 EVENT_VISIBLE_WIN_LIMIT_REACHED_RD),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_win_limit_reached_wr,
				 EVENT_VISIBLE_WIN_LIMIT_REACHED_WR),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_dqsosc_mpc, EVENT_OP_IS_DQSOSC_MPC),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_dqsosc_mrr, EVENT_OP_IS_DQSOSC_MRR),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_tcr_mrr, EVENT_OP_IS_TCR_MRR),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_zqstart, EVENT_OP_IS_ZQSTART),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_zqlatch, EVENT_OP_IS_ZQLATCH),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_dfi_parity_poison,
				 EVENT_DFI_PARITY_POISON),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_wr_crc_error, EVENT_WR_CRC_ERROR),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_capar_error, EVENT_CAPAR_ERROR),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_rd_crc_error, EVENT_RD_CRC_ERROR),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_rd_uc_ecc_error, EVENT_RD_UC_ECC_ERROR),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_dfi_cmd_is_retry, EVENT_DFI_CMD_IS_RETRY),
	/* Free run event counters */
	CN10K_DDR_PMU_EVENT_ATTR(ddr_ddr_reads, EVENT_DDR_READS),
	CN10K_DDR_PMU_EVENT_ATTR(ddr_ddr_writes, EVENT_DDR_WRITES),
	NULL
};

static struct attribute_group odyssey_ddr_perf_events_attr_group = {
	.name = "events",
	.attrs = odyssey_ddr_perf_events_attrs,
};

static struct attribute_group cn10k_ddr_perf_events_attr_group = {
	.name = "events",
	.attrs = cn10k_ddr_perf_events_attrs,
};

PMU_FORMAT_ATTR(event, "config:0-8");

static struct attribute *cn10k_ddr_perf_format_attrs[] = {
	&format_attr_event.attr,
	NULL,
};

static struct attribute_group cn10k_ddr_perf_format_attr_group = {
	.name = "format",
	.attrs = cn10k_ddr_perf_format_attrs,
};

static ssize_t cn10k_ddr_perf_cpumask_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct cn10k_ddr_pmu *pmu = dev_get_drvdata(dev);

	return cpumap_print_to_pagebuf(true, buf, cpumask_of(pmu->cpu));
}

static struct device_attribute cn10k_ddr_perf_cpumask_attr =
	__ATTR(cpumask, 0444, cn10k_ddr_perf_cpumask_show, NULL);

static struct attribute *cn10k_ddr_perf_cpumask_attrs[] = {
	&cn10k_ddr_perf_cpumask_attr.attr,
	NULL,
};

static struct attribute_group cn10k_ddr_perf_cpumask_attr_group = {
	.attrs = cn10k_ddr_perf_cpumask_attrs,
};

static const struct attribute_group *cn10k_attr_groups[] = {
	&cn10k_ddr_perf_events_attr_group,
	&cn10k_ddr_perf_format_attr_group,
	&cn10k_ddr_perf_cpumask_attr_group,
	NULL,
};

static const struct attribute_group *odyssey_attr_groups[] = {
	&odyssey_ddr_perf_events_attr_group,
	&cn10k_ddr_perf_format_attr_group,
	&cn10k_ddr_perf_cpumask_attr_group,
	NULL
};

/* Default poll timeout is 100 sec, which is very sufficient for
 * 48 bit counter incremented max at 5.6 GT/s, which may take many
 * hours to overflow.
 */
static unsigned long cn10k_ddr_pmu_poll_period_sec = 100;
module_param_named(poll_period_sec, cn10k_ddr_pmu_poll_period_sec, ulong, 0644);

static ktime_t cn10k_ddr_pmu_timer_period(void)
{
	return ms_to_ktime((u64)cn10k_ddr_pmu_poll_period_sec * USEC_PER_SEC);
}

static int ddr_perf_get_event_bitmap(int eventid, u64 *event_bitmap,
				     struct cn10k_ddr_pmu *ddr_pmu)
{
	int err = 0;

	switch (eventid) {
	case EVENT_DFI_PARITY_POISON ...EVENT_DFI_CMD_IS_RETRY:
		if (!ddr_pmu->p_data->is_ody) {
			err = -EINVAL;
			break;
		}
		fallthrough;
	case EVENT_HIF_RD_OR_WR ... EVENT_WAW_HAZARD:
	case EVENT_OP_IS_REFRESH ... EVENT_OP_IS_ZQLATCH:
		*event_bitmap = (1ULL << (eventid - 1));
		break;
	case EVENT_OP_IS_ENTER_SELFREF:
	case EVENT_OP_IS_ENTER_POWERDOWN:
	case EVENT_OP_IS_ENTER_MPSM:
		*event_bitmap = (0xFULL << (eventid - 1));
		break;
	default:
		err = -EINVAL;
	}

	if (err)
		pr_err("%s Invalid eventid %d\n", __func__, eventid);
	return err;
}

static int cn10k_ddr_perf_alloc_counter(struct cn10k_ddr_pmu *pmu,
					struct perf_event *event)
{
	u8 config = event->attr.config;
	int i;

	/* DDR read free-run counter index */
	if (config == EVENT_DDR_READS) {
		pmu->events[DDRC_PERF_READ_COUNTER_IDX] = event;
		return DDRC_PERF_READ_COUNTER_IDX;
	}

	/* DDR write free-run counter index */
	if (config == EVENT_DDR_WRITES) {
		pmu->events[DDRC_PERF_WRITE_COUNTER_IDX] = event;
		return DDRC_PERF_WRITE_COUNTER_IDX;
	}

	/* Allocate DDR generic counters */
	for (i = 0; i < DDRC_PERF_NUM_GEN_COUNTERS; i++) {
		if (pmu->events[i] == NULL) {
			pmu->events[i] = event;
			return i;
		}
	}

	return -ENOENT;
}

static void cn10k_ddr_perf_free_counter(struct cn10k_ddr_pmu *pmu, int counter)
{
	pmu->events[counter] = NULL;
}

static int cn10k_ddr_perf_event_init(struct perf_event *event)
{
	struct cn10k_ddr_pmu *pmu = to_cn10k_ddr_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;

	if (event->attr.type != event->pmu->type)
		return -ENOENT;

	if (is_sampling_event(event)) {
		dev_info(pmu->dev, "Sampling not supported!\n");
		return -EOPNOTSUPP;
	}

	if (event->cpu < 0) {
		dev_warn(pmu->dev, "Can't provide per-task data!\n");
		return -EOPNOTSUPP;
	}

	/*  We must NOT create groups containing mixed PMUs */
	if (event->group_leader->pmu != event->pmu &&
	    !is_software_event(event->group_leader))
		return -EINVAL;

	/* Set ownership of event to one CPU, same event can not be observed
	 * on multiple cpus at same time.
	 */
	event->cpu = pmu->cpu;
	hwc->idx = -1;
	return 0;
}

static void cn10k_ddr_perf_counter_start(struct cn10k_ddr_pmu *ddr_pmu,
					 int counter)
{
	const struct ddr_pmu_platform_data *p_data = ddr_pmu->p_data;
	u64 ctrl_reg = p_data->cnt_start_op_ctrl;

	writeq_relaxed(START_OP_CTRL_VAL_START, ddr_pmu->base +
		       DDRC_PERF_REG(ctrl_reg, counter));
}

static void cn10k_ddr_perf_counter_stop(struct cn10k_ddr_pmu *ddr_pmu,
					int counter)
{
	const struct ddr_pmu_platform_data *p_data = ddr_pmu->p_data;
	u64 ctrl_reg = p_data->cnt_end_op_ctrl;

	writeq_relaxed(END_OP_CTRL_VAL_END, ddr_pmu->base +
		       DDRC_PERF_REG(ctrl_reg, counter));
}

static void cn10k_ddr_perf_counter_enable(struct cn10k_ddr_pmu *pmu,
					  int counter, bool enable)
{
	const struct ddr_pmu_platform_data *p_data = pmu->p_data;
	u64 ctrl_reg = pmu->p_data->cnt_op_mode_ctrl;
	const struct ddr_pmu_ops *ops = pmu->ops;
	bool is_ody = pmu->p_data->is_ody;
	u32 reg;
	u64 val;

	if (counter > DDRC_PERF_NUM_COUNTERS) {
		pr_err("Error: unsupported counter %d\n", counter);
		return;
	}

	if (counter < DDRC_PERF_NUM_GEN_COUNTERS) {
		reg = DDRC_PERF_CFG(p_data->cfg_base, counter);
		val = readq_relaxed(pmu->base + reg);

		if (enable)
			val |= EVENT_ENABLE;
		else
			val &= ~EVENT_ENABLE;

		writeq_relaxed(val, pmu->base + reg);

		if (is_ody) {
			if (enable) {
				/*
				 * Setup the PMU counter to work in
				 * manual mode
				 */
				reg = DDRC_PERF_REG(ctrl_reg, counter);
				writeq_relaxed(OP_MODE_CTRL_VAL_MANUAL,
					       pmu->base + reg);

				cn10k_ddr_perf_counter_start(pmu, counter);
			} else {
				cn10k_ddr_perf_counter_stop(pmu, counter);
			}
		}
	} else {
		if (counter == DDRC_PERF_READ_COUNTER_IDX)
			ops->enable_read_freerun_counter(pmu, enable);
		else
			ops->enable_write_freerun_counter(pmu, enable);
	}
}

static u64 cn10k_ddr_perf_read_counter(struct cn10k_ddr_pmu *pmu, int counter)
{
	const struct ddr_pmu_platform_data *p_data = pmu->p_data;
	u64 val;

	if (counter == DDRC_PERF_READ_COUNTER_IDX)
		return readq_relaxed(pmu->base +
				     p_data->cnt_value_rd_op);

	if (counter == DDRC_PERF_WRITE_COUNTER_IDX)
		return readq_relaxed(pmu->base +
				     p_data->cnt_value_wr_op);

	val = readq_relaxed(pmu->base +
			    DDRC_PERF_REG(p_data->cnt_base, counter));
	return val;
}

static void cn10k_ddr_perf_event_update(struct perf_event *event)
{
	struct cn10k_ddr_pmu *pmu = to_cn10k_ddr_pmu(event->pmu);
	const struct ddr_pmu_platform_data *p_data = pmu->p_data;
	struct hw_perf_event *hwc = &event->hw;
	u64 prev_count, new_count, mask;

	do {
		prev_count = local64_read(&hwc->prev_count);
		new_count = cn10k_ddr_perf_read_counter(pmu, hwc->idx);
	} while (local64_xchg(&hwc->prev_count, new_count) != prev_count);

	mask = p_data->counter_max_val;

	local64_add((new_count - prev_count) & mask, &event->count);
}

static void cn10k_ddr_perf_event_start(struct perf_event *event, int flags)
{
	struct cn10k_ddr_pmu *pmu = to_cn10k_ddr_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	int counter = hwc->idx;

	local64_set(&hwc->prev_count, 0);

	cn10k_ddr_perf_counter_enable(pmu, counter, true);

	hwc->state = 0;
}

static int cn10k_ddr_perf_event_add(struct perf_event *event, int flags)
{
	struct cn10k_ddr_pmu *pmu = to_cn10k_ddr_pmu(event->pmu);
	const struct ddr_pmu_platform_data *p_data = pmu->p_data;
	const struct ddr_pmu_ops *ops = pmu->ops;
	struct hw_perf_event *hwc = &event->hw;
	u8 config = event->attr.config;
	int counter, ret;
	u32 reg_offset;
	u64 val;

	counter = cn10k_ddr_perf_alloc_counter(pmu, event);
	if (counter < 0)
		return -EAGAIN;

	pmu->active_events++;
	hwc->idx = counter;

	if (pmu->active_events == 1)
		hrtimer_start(&pmu->hrtimer, cn10k_ddr_pmu_timer_period(),
			      HRTIMER_MODE_REL_PINNED);

	if (counter < DDRC_PERF_NUM_GEN_COUNTERS) {
		/* Generic counters, configure event id */
		reg_offset = DDRC_PERF_CFG(p_data->cfg_base, counter);
		ret = ddr_perf_get_event_bitmap(config, &val, pmu);
		if (ret)
			return ret;

		writeq_relaxed(val, pmu->base + reg_offset);
	} else {
		/* fixed event counter, clear counter value */
		if (counter == DDRC_PERF_READ_COUNTER_IDX)
			ops->clear_read_freerun_counter(pmu);
		else
			ops->clear_write_freerun_counter(pmu);
	}

	hwc->state |= PERF_HES_STOPPED;

	if (flags & PERF_EF_START)
		cn10k_ddr_perf_event_start(event, flags);

	return 0;
}

static void cn10k_ddr_perf_event_stop(struct perf_event *event, int flags)
{
	struct cn10k_ddr_pmu *pmu = to_cn10k_ddr_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	int counter = hwc->idx;

	cn10k_ddr_perf_counter_enable(pmu, counter, false);

	if (flags & PERF_EF_UPDATE)
		cn10k_ddr_perf_event_update(event);

	hwc->state |= PERF_HES_STOPPED;
}

static void cn10k_ddr_perf_event_del(struct perf_event *event, int flags)
{
	struct cn10k_ddr_pmu *pmu = to_cn10k_ddr_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	int counter = hwc->idx;

	cn10k_ddr_perf_event_stop(event, PERF_EF_UPDATE);

	cn10k_ddr_perf_free_counter(pmu, counter);
	pmu->active_events--;
	hwc->idx = -1;

	/* Cancel timer when no events to capture */
	if (pmu->active_events == 0)
		hrtimer_cancel(&pmu->hrtimer);
}

static void cn10k_ddr_perf_pmu_enable(struct pmu *pmu)
{
	struct cn10k_ddr_pmu *ddr_pmu = to_cn10k_ddr_pmu(pmu);
	const struct ddr_pmu_platform_data *p_data = ddr_pmu->p_data;

	writeq_relaxed(START_OP_CTRL_VAL_START, ddr_pmu->base +
		       p_data->cnt_start_op_ctrl);
}

static void cn10k_ddr_perf_pmu_disable(struct pmu *pmu)
{
	struct cn10k_ddr_pmu *ddr_pmu = to_cn10k_ddr_pmu(pmu);
	const struct ddr_pmu_platform_data *p_data = ddr_pmu->p_data;

	writeq_relaxed(END_OP_CTRL_VAL_END, ddr_pmu->base +
		       p_data->cnt_end_op_ctrl);
}

static void cn10k_ddr_perf_event_update_all(struct cn10k_ddr_pmu *pmu)
{
	struct hw_perf_event *hwc;
	int i;

	for (i = 0; i < DDRC_PERF_NUM_GEN_COUNTERS; i++) {
		if (pmu->events[i] == NULL)
			continue;

		cn10k_ddr_perf_event_update(pmu->events[i]);
	}

	/* Reset previous count as h/w counter are reset */
	for (i = 0; i < DDRC_PERF_NUM_GEN_COUNTERS; i++) {
		if (pmu->events[i] == NULL)
			continue;

		hwc = &pmu->events[i]->hw;
		local64_set(&hwc->prev_count, 0);
	}
}

static void ddr_pmu_enable_read_freerun(struct cn10k_ddr_pmu *pmu, bool enable)
{
	const struct ddr_pmu_platform_data *p_data = pmu->p_data;
	u64 val;

	val = readq_relaxed(pmu->base + p_data->cnt_freerun_en);
	if (enable)
		val |= DDRC_PERF_FREERUN_READ_EN;
	else
		val &= ~DDRC_PERF_FREERUN_READ_EN;

	writeq_relaxed(val, pmu->base + p_data->cnt_freerun_en);
}

static void ddr_pmu_enable_write_freerun(struct cn10k_ddr_pmu *pmu, bool enable)
{
	const struct ddr_pmu_platform_data *p_data = pmu->p_data;
	u64 val;

	val = readq_relaxed(pmu->base + p_data->cnt_freerun_en);
	if (enable)
		val |= DDRC_PERF_FREERUN_WRITE_EN;
	else
		val &= ~DDRC_PERF_FREERUN_WRITE_EN;

	writeq_relaxed(val, pmu->base + p_data->cnt_freerun_en);
}

static void ddr_pmu_read_clear_freerun(struct cn10k_ddr_pmu *pmu)
{
	const struct ddr_pmu_platform_data *p_data = pmu->p_data;
	u64 val;

	val = DDRC_FREERUN_READ_CNT_CLR;
	writeq_relaxed(val, pmu->base + p_data->cnt_freerun_ctrl);
}

static void ddr_pmu_write_clear_freerun(struct cn10k_ddr_pmu *pmu)
{
	const struct ddr_pmu_platform_data *p_data = pmu->p_data;
	u64 val;

	val = DDRC_FREERUN_WRITE_CNT_CLR;
	writeq_relaxed(val, pmu->base + p_data->cnt_freerun_ctrl);
}

static void ddr_pmu_overflow_hander(struct cn10k_ddr_pmu *pmu, int evt_idx)
{
	cn10k_ddr_perf_event_update_all(pmu);
	cn10k_ddr_perf_pmu_disable(&pmu->pmu);
	cn10k_ddr_perf_pmu_enable(&pmu->pmu);
}

static void ddr_pmu_ody_enable_read_freerun(struct cn10k_ddr_pmu *pmu,
					    bool enable)
{
	const struct ddr_pmu_platform_data *p_data = pmu->p_data;
	u64 val;

	val = readq_relaxed(pmu->base + p_data->cnt_freerun_ctrl);
	if (enable)
		val |= DDRC_PERF_FREERUN_READ_EN;
	else
		val &= ~DDRC_PERF_FREERUN_READ_EN;

	writeq_relaxed(val, pmu->base + p_data->cnt_freerun_ctrl);
}

static void ddr_pmu_ody_enable_write_freerun(struct cn10k_ddr_pmu *pmu,
					     bool enable)
{
	const struct ddr_pmu_platform_data *p_data = pmu->p_data;
	u64 val;

	val = readq_relaxed(pmu->base + p_data->cnt_freerun_ctrl);
	if (enable)
		val |= DDRC_PERF_FREERUN_WRITE_EN;
	else
		val &= ~DDRC_PERF_FREERUN_WRITE_EN;

	writeq_relaxed(val, pmu->base + p_data->cnt_freerun_ctrl);
}

static void ddr_pmu_ody_read_clear_freerun(struct cn10k_ddr_pmu *pmu)
{
	const struct ddr_pmu_platform_data *p_data = pmu->p_data;
	u64 val;

	val = DDRC_FREERUN_READ_CNT_CLR;
	writeq_relaxed(val, pmu->base + p_data->cnt_freerun_clr);
}

static void ddr_pmu_ody_write_clear_freerun(struct cn10k_ddr_pmu *pmu)
{
	const struct ddr_pmu_platform_data *p_data = pmu->p_data;
	u64 val;

	val = DDRC_FREERUN_WRITE_CNT_CLR;
	writeq_relaxed(val, pmu->base + p_data->cnt_freerun_clr);
}

static void ddr_pmu_ody_overflow_hander(struct cn10k_ddr_pmu *pmu, int evt_idx)
{
	/*
	 * On reaching the maximum value of the counter, the counter freezes
	 * there. The particular event is updated and the respective counter
	 * is stopped and started again so that it starts counting from zero
	 */
	cn10k_ddr_perf_event_update(pmu->events[evt_idx]);
	cn10k_ddr_perf_counter_stop(pmu, evt_idx);
	cn10k_ddr_perf_counter_start(pmu, evt_idx);
}

static irqreturn_t cn10k_ddr_pmu_overflow_handler(struct cn10k_ddr_pmu *pmu)
{
	const struct ddr_pmu_platform_data *p_data = pmu->p_data;
	const struct ddr_pmu_ops *ops = pmu->ops;
	struct perf_event *event;
	struct hw_perf_event *hwc;
	u64 prev_count, new_count;
	u64 value;
	int i;

	event = pmu->events[DDRC_PERF_READ_COUNTER_IDX];
	if (event) {
		hwc = &event->hw;
		prev_count = local64_read(&hwc->prev_count);
		new_count = cn10k_ddr_perf_read_counter(pmu, hwc->idx);

		/* Overflow condition is when new count less than
		 * previous count
		 */
		if (new_count < prev_count)
			cn10k_ddr_perf_event_update(event);
	}

	event = pmu->events[DDRC_PERF_WRITE_COUNTER_IDX];
	if (event) {
		hwc = &event->hw;
		prev_count = local64_read(&hwc->prev_count);
		new_count = cn10k_ddr_perf_read_counter(pmu, hwc->idx);

		/* Overflow condition is when new count less than
		 * previous count
		 */
		if (new_count < prev_count)
			cn10k_ddr_perf_event_update(event);
	}

	for (i = 0; i < DDRC_PERF_NUM_GEN_COUNTERS; i++) {
		if (pmu->events[i] == NULL)
			continue;

		value = cn10k_ddr_perf_read_counter(pmu, i);
		if (value == p_data->counter_max_val) {
			pr_info("Counter-(%d) reached max value\n", i);
			ops->pmu_overflow_handler(pmu, i);
		}
	}

	return IRQ_HANDLED;
}

static enum hrtimer_restart cn10k_ddr_pmu_timer_handler(struct hrtimer *hrtimer)
{
	struct cn10k_ddr_pmu *pmu = container_of(hrtimer, struct cn10k_ddr_pmu,
						 hrtimer);
	unsigned long flags;

	local_irq_save(flags);
	cn10k_ddr_pmu_overflow_handler(pmu);
	local_irq_restore(flags);

	hrtimer_forward_now(hrtimer, cn10k_ddr_pmu_timer_period());
	return HRTIMER_RESTART;
}

static int cn10k_ddr_pmu_offline_cpu(unsigned int cpu, struct hlist_node *node)
{
	struct cn10k_ddr_pmu *pmu = hlist_entry_safe(node, struct cn10k_ddr_pmu,
						     node);
	unsigned int target;

	if (cpu != pmu->cpu)
		return 0;

	target = cpumask_any_but(cpu_online_mask, cpu);
	if (target >= nr_cpu_ids)
		return 0;

	perf_pmu_migrate_context(&pmu->pmu, cpu, target);
	pmu->cpu = target;
	return 0;
}

static const struct ddr_pmu_ops ddr_pmu_ops = {
	.enable_read_freerun_counter = ddr_pmu_enable_read_freerun,
	.enable_write_freerun_counter = ddr_pmu_enable_write_freerun,
	.clear_read_freerun_counter = ddr_pmu_read_clear_freerun,
	.clear_write_freerun_counter = ddr_pmu_write_clear_freerun,
	.pmu_overflow_handler = ddr_pmu_overflow_hander,
};

#if defined(CONFIG_ACPI) || defined(CONFIG_OF)
static const struct ddr_pmu_platform_data cn10k_ddr_pmu_pdata = {
	.counter_overflow_val =  BIT_ULL(48),
	.counter_max_val = GENMASK_ULL(48, 0),
	.cnt_base = CN10K_DDRC_PERF_CNT_VALUE_BASE,
	.cfg_base = CN10K_DDRC_PERF_CFG_BASE,
	.cnt_op_mode_ctrl = CN10K_DDRC_PERF_CNT_OP_MODE_CTRL,
	.cnt_start_op_ctrl = CN10K_DDRC_PERF_CNT_START_OP_CTRL,
	.cnt_end_op_ctrl = CN10K_DDRC_PERF_CNT_END_OP_CTRL,
	.cnt_end_status = CN10K_DDRC_PERF_CNT_END_STATUS,
	.cnt_freerun_en = CN10K_DDRC_PERF_CNT_FREERUN_EN,
	.cnt_freerun_ctrl = CN10K_DDRC_PERF_CNT_FREERUN_CTRL,
	.cnt_freerun_clr = 0,
	.cnt_value_wr_op = CN10K_DDRC_PERF_CNT_VALUE_WR_OP,
	.cnt_value_rd_op = CN10K_DDRC_PERF_CNT_VALUE_RD_OP,
	.is_cn10k = TRUE,
};
#endif

static const struct ddr_pmu_ops ddr_pmu_ody_ops = {
	.enable_read_freerun_counter = ddr_pmu_ody_enable_read_freerun,
	.enable_write_freerun_counter = ddr_pmu_ody_enable_write_freerun,
	.clear_read_freerun_counter = ddr_pmu_ody_read_clear_freerun,
	.clear_write_freerun_counter = ddr_pmu_ody_write_clear_freerun,
	.pmu_overflow_handler = ddr_pmu_ody_overflow_hander,
};

#ifdef CONFIG_ACPI
static const struct ddr_pmu_platform_data odyssey_ddr_pmu_pdata = {
	.counter_overflow_val = 0,
	.counter_max_val = GENMASK_ULL(63, 0),
	.cnt_base = ODY_DDRC_PERF_CNT_VALUE_BASE,
	.cfg_base = ODY_DDRC_PERF_CFG_BASE,
	.cnt_op_mode_ctrl = ODY_DDRC_PERF_CNT_OP_MODE_CTRL,
	.cnt_start_op_ctrl = ODY_DDRC_PERF_CNT_START_OP_CTRL,
	.cnt_end_op_ctrl = ODY_DDRC_PERF_CNT_END_OP_CTRL,
	.cnt_end_status = ODY_DDRC_PERF_CNT_END_STATUS,
	.cnt_freerun_en = 0,
	.cnt_freerun_ctrl = ODY_DDRC_PERF_CNT_FREERUN_CTRL,
	.cnt_freerun_clr = ODY_DDRC_PERF_CNT_FREERUN_CLR,
	.cnt_value_wr_op = ODY_DDRC_PERF_CNT_VALUE_WR_OP,
	.cnt_value_rd_op = ODY_DDRC_PERF_CNT_VALUE_RD_OP,
	.is_ody = TRUE,
};
#endif

static int cn10k_ddr_perf_probe(struct platform_device *pdev)
{
	const struct ddr_pmu_platform_data *dev_data;
	struct cn10k_ddr_pmu *ddr_pmu;
	struct resource *res;
	void __iomem *base;
	bool is_cn10k;
	bool is_ody;
	char *name;
	int ret;

	ddr_pmu = devm_kzalloc(&pdev->dev, sizeof(*ddr_pmu), GFP_KERNEL);
	if (!ddr_pmu)
		return -ENOMEM;

	ddr_pmu->dev = &pdev->dev;
	platform_set_drvdata(pdev, ddr_pmu);

	dev_data = device_get_match_data(&pdev->dev);
	if (!dev_data) {
		dev_err(&pdev->dev, "Error: No device match data found\n");
		return -ENODEV;
	}

	base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	ddr_pmu->base = base;

	ddr_pmu->p_data = dev_data;
	is_cn10k = ddr_pmu->p_data->is_cn10k;
	is_ody = ddr_pmu->p_data->is_ody;

	if (is_cn10k) {
		ddr_pmu->ops = &ddr_pmu_ops;
		/* Setup the PMU counter to work in manual mode */
		writeq_relaxed(OP_MODE_CTRL_VAL_MANUAL, ddr_pmu->base +
			       ddr_pmu->p_data->cnt_op_mode_ctrl);

		ddr_pmu->pmu = (struct pmu) {
			.module	      = THIS_MODULE,
			.capabilities = PERF_PMU_CAP_NO_EXCLUDE,
			.task_ctx_nr = perf_invalid_context,
			.attr_groups = cn10k_attr_groups,
			.event_init  = cn10k_ddr_perf_event_init,
			.add	     = cn10k_ddr_perf_event_add,
			.del	     = cn10k_ddr_perf_event_del,
			.start	     = cn10k_ddr_perf_event_start,
			.stop	     = cn10k_ddr_perf_event_stop,
			.read	     = cn10k_ddr_perf_event_update,
			.pmu_enable  = cn10k_ddr_perf_pmu_enable,
			.pmu_disable = cn10k_ddr_perf_pmu_disable,
		};
	}

	if (is_ody) {
		ddr_pmu->ops = &ddr_pmu_ody_ops;

		ddr_pmu->pmu = (struct pmu) {
			.module       = THIS_MODULE,
			.capabilities = PERF_PMU_CAP_NO_EXCLUDE,
			.task_ctx_nr = perf_invalid_context,
			.attr_groups = odyssey_attr_groups,
			.event_init  = cn10k_ddr_perf_event_init,
			.add         = cn10k_ddr_perf_event_add,
			.del         = cn10k_ddr_perf_event_del,
			.start       = cn10k_ddr_perf_event_start,
			.stop        = cn10k_ddr_perf_event_stop,
			.read        = cn10k_ddr_perf_event_update,
		};
	}

	/* Choose this cpu to collect perf data */
	ddr_pmu->cpu = raw_smp_processor_id();

	name = devm_kasprintf(ddr_pmu->dev, GFP_KERNEL, "mrvl_ddr_pmu_%llx",
			      res->start);
	if (!name)
		return -ENOMEM;

	hrtimer_setup(&ddr_pmu->hrtimer, cn10k_ddr_pmu_timer_handler, CLOCK_MONOTONIC,
		      HRTIMER_MODE_REL);

	cpuhp_state_add_instance_nocalls(
				CPUHP_AP_PERF_ARM_MARVELL_CN10K_DDR_ONLINE,
				&ddr_pmu->node);

	ret = perf_pmu_register(&ddr_pmu->pmu, name, -1);
	if (ret)
		goto error;

	pr_info("DDR PMU Driver for ddrc@%llx\n", res->start);
	return 0;
error:
	cpuhp_state_remove_instance_nocalls(
				CPUHP_AP_PERF_ARM_MARVELL_CN10K_DDR_ONLINE,
				&ddr_pmu->node);
	return ret;
}

static void cn10k_ddr_perf_remove(struct platform_device *pdev)
{
	struct cn10k_ddr_pmu *ddr_pmu = platform_get_drvdata(pdev);

	cpuhp_state_remove_instance_nocalls(
				CPUHP_AP_PERF_ARM_MARVELL_CN10K_DDR_ONLINE,
				&ddr_pmu->node);

	perf_pmu_unregister(&ddr_pmu->pmu);
}

#ifdef CONFIG_OF
static const struct of_device_id cn10k_ddr_pmu_of_match[] = {
	{ .compatible = "marvell,cn10k-ddr-pmu", .data = &cn10k_ddr_pmu_pdata },
	{ },
};
MODULE_DEVICE_TABLE(of, cn10k_ddr_pmu_of_match);
#endif

#ifdef CONFIG_ACPI
static const struct acpi_device_id cn10k_ddr_pmu_acpi_match[] = {
	{"MRVL000A", (kernel_ulong_t)&cn10k_ddr_pmu_pdata },
	{"MRVL000C", (kernel_ulong_t)&odyssey_ddr_pmu_pdata},
	{},
};
MODULE_DEVICE_TABLE(acpi, cn10k_ddr_pmu_acpi_match);
#endif

static struct platform_driver cn10k_ddr_pmu_driver = {
	.driver	= {
		.name   = "cn10k-ddr-pmu",
		.of_match_table = of_match_ptr(cn10k_ddr_pmu_of_match),
		.acpi_match_table  = ACPI_PTR(cn10k_ddr_pmu_acpi_match),
		.suppress_bind_attrs = true,
	},
	.probe		= cn10k_ddr_perf_probe,
	.remove		= cn10k_ddr_perf_remove,
};

static int __init cn10k_ddr_pmu_init(void)
{
	int ret;

	ret = cpuhp_setup_state_multi(
				CPUHP_AP_PERF_ARM_MARVELL_CN10K_DDR_ONLINE,
				"perf/marvell/cn10k/ddr:online", NULL,
				cn10k_ddr_pmu_offline_cpu);
	if (ret)
		return ret;

	ret = platform_driver_register(&cn10k_ddr_pmu_driver);
	if (ret)
		cpuhp_remove_multi_state(
				CPUHP_AP_PERF_ARM_MARVELL_CN10K_DDR_ONLINE);
	return ret;
}

static void __exit cn10k_ddr_pmu_exit(void)
{
	platform_driver_unregister(&cn10k_ddr_pmu_driver);
	cpuhp_remove_multi_state(CPUHP_AP_PERF_ARM_MARVELL_CN10K_DDR_ONLINE);
}

module_init(cn10k_ddr_pmu_init);
module_exit(cn10k_ddr_pmu_exit);

MODULE_AUTHOR("Bharat Bhushan <bbhushan2@marvell.com>");
MODULE_DESCRIPTION("Marvell CN10K DRAM Subsystem (DSS) Performance Monitor Driver");
MODULE_LICENSE("GPL v2");

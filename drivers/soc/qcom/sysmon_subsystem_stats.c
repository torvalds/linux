// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/soc/qcom/smem.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/soc/qcom/sysmon_subsystem_stats.h>

#define SYSMON_SMEM_ID					634
#define SLEEPSTATS_SMEM_ID_ADSP			606
#define SLEEPSTATS_SMEM_ID_CDSP			607
#define SLEEPSTATS_SMEM_ID_SLPI			608
#define SLEEPSTATS_LPI_SMEM_ID			613
#define DSPPMSTATS_SMEM_ID				624
#define SYS_CLK_TICKS_PER_MS			19200
#define SYS_CLK_TICKS_PER_SEC			19200000
#define DSPPMSTATS_NUMPD				5
#define HMX_HVX_PMU_EVENTS_NA			0xFFFF

struct pd_clients {
	int pid;
	u32 num_active;
};

struct dsppm_stats {
	u32 version;
	u32 latency_us;
	u32 timestamp;
	struct pd_clients pd[DSPPMSTATS_NUMPD];
};

struct sysmon_smem_power_stats_extended {
	struct sysmon_smem_power_stats powerstats;
	/**< Powerstats data */

	u32 last_update_time_powerstats_lsb;
	/**< Powerstats updated timestamp (lower significant 32 bits) */

	u32 last_update_time_powerstats_msb;
	/**< Powerstats updated timestamp (Most significant 32 bits) */
};

struct sysmon_smem_stats {
	bool smem_init_adsp;
	bool smem_init_cdsp;
	bool smem_init_slpi;
	bool smem_init_cdsp_v2;
	struct sysmon_smem_power_stats_extended *sysmon_power_stats_adsp;
	struct sysmon_smem_power_stats_extended *sysmon_power_stats_cdsp;
	struct sysmon_smem_power_stats_extended *sysmon_power_stats_slpi;
	struct sysmon_smem_q6_event_stats *sysmon_event_stats_adsp;
	struct sysmon_smem_q6_event_stats *sysmon_event_stats_cdsp;
	struct sysmon_smem_q6_event_stats *sysmon_event_stats_slpi;
	struct sleep_stats *sleep_stats_adsp;
	struct sleep_stats *sleep_stats_cdsp;
	struct sleep_stats *sleep_stats_slpi;
	struct sleep_stats_island *sleep_lpi_adsp;
	struct sleep_stats_island *sleep_lpi_slpi;
	u32 *q6_avg_load_adsp;
	u32 *q6_avg_load_cdsp;
	u32 *q6_avg_load_slpi;
	struct dsppm_stats *dsppm_stats_adsp;
	struct dsppm_stats *dsppm_stats_cdsp;
	struct dentry *debugfs_dir;
	struct dentry *debugfs_master_adsp_stats;
	struct dentry *debugfs_master_cdsp_stats;
	u32 *hmx_util;
	u32 *hvx_util;
};

enum feature_id {
	SYSMONSTATS_Q6_LOAD_FEATUREID = 1,
	SYSMONSTATS_Q6_EVENT_FEATUREID,
	SYSMON_POWER_STATS_FEATUREID,
	SYSMON_HMX_UTIL_FEATUREID,
	SYSMON_HVX_UTIL_FEATUREID
};

struct sysmon_smem_q6_load_stats {
	u32 q6load_avg;
	u32 Last_update_time_load_lsb;
	u32 Last_update_time_load_msb;
};

struct sysmon_smem_hvx_stats {
	u32 hvx_util_avg;
	u32 Last_update_time_load_lsb;
	u32 Last_update_time_load_msb;
};

struct sysmon_smem_hmx_stats {
	u32 hmx_util_avg;
	u32 Last_update_time_load_lsb;
	u32 Last_update_time_load_msb;
};

static struct sysmon_smem_stats g_sysmon_stats;

/* Adds delta between SMEM powerstats updated time to current time */
static int add_delta_time(
	u8 ver,
	u64 lpi_acc, u64 lpm_acc,
	struct sysmon_smem_power_stats_extended *stats
)
{
	u64 powerstats_ticks = 0, curr_timestamp = 0, diff_ticks = 0;
	u64 pc_time = 0, lpi_time = 0, active_time = 0, diff_time = 0;
	u8 j = 0;

	if (ver >= 2) {
		powerstats_ticks = (u64)(((u64)stats->last_update_time_powerstats_msb << 32) |
							stats->last_update_time_powerstats_lsb);
		curr_timestamp = __arch_counter_get_cntvct();

		if (curr_timestamp < powerstats_ticks)
			diff_ticks = curr_timestamp + (ULLONG_MAX - powerstats_ticks);
		else
			diff_ticks = (curr_timestamp - powerstats_ticks);

		diff_time = (diff_ticks / SYS_CLK_TICKS_PER_SEC);

		if (diff_time > 0) {
			pc_time = (lpm_acc / SYS_CLK_TICKS_PER_SEC);
			lpi_time = (lpi_acc / SYS_CLK_TICKS_PER_SEC);

			if (pc_time >= stats->powerstats.pc_time)
				pc_time -= stats->powerstats.pc_time;

			if (diff_time >= pc_time)
				active_time = diff_time - pc_time;

			if ((lpi_time) && (lpi_time >= stats->powerstats.lpi_time))
				lpi_time -= stats->powerstats.lpi_time;

			if ((lpi_time) && (pc_time >= lpi_time))
				pc_time -= lpi_time;

			for (j = 0; j < SYSMON_POWER_STATS_MAX_CLK_LEVELS; j++) {
				if (stats->powerstats.clk_arr[j] == stats->powerstats.current_clk) {
					stats->powerstats.active_time[j] += active_time;
					stats->powerstats.island_time[j] += lpi_time;
					break;
				}
			}

			if (j >= SYSMON_POWER_STATS_MAX_CLK_LEVELS) {
				pr_err("%s: Current clock level %u KHz didn't match with any clock level\n",
					__func__, stats->powerstats.current_clk);
				return -EINVAL;
			}
			stats->powerstats.pc_time += pc_time;
			stats->powerstats.lpi_time += lpi_time;
		}
	}

	return 0;
}

/*Updates SMEM pointers for all the sysmon Master stats*/
static void update_sysmon_smem_pointers(void *smem_pointer, enum dsp_id_t dsp_id, size_t size)
{
	u32 featureId;
	int feature, size_rcvd;
	int size_of_u32 = sizeof(u32);

	featureId = *(unsigned int *)smem_pointer;
	feature = featureId >> 28;
	size_rcvd = (featureId >> 16) & 0xFFF;

	while ((size > 0) && (size >= size_rcvd)) {
		switch (feature) {
		case SYSMON_POWER_STATS_FEATUREID:
			if (!IS_ERR_OR_NULL(smem_pointer + size_of_u32)) {
				if (dsp_id == ADSP)
					g_sysmon_stats.sysmon_power_stats_adsp =
					(struct sysmon_smem_power_stats_extended *)
					(smem_pointer);
				else if (dsp_id == CDSP)
					g_sysmon_stats.sysmon_power_stats_cdsp =
					(struct sysmon_smem_power_stats_extended *)
					(smem_pointer);
				else if (dsp_id == SLPI)
					g_sysmon_stats.sysmon_power_stats_slpi =
					(struct sysmon_smem_power_stats_extended *)
					(smem_pointer);
				else
					pr_err("%s: subsystem not found %d\n",
						__func__, SYSMON_POWER_STATS_FEATUREID);
			} else {
				pr_err("%s: Failed to fetch %d feature pointer\n",
					__func__, SYSMON_POWER_STATS_FEATUREID);
				size = 0;
			}
		break;
		case SYSMONSTATS_Q6_EVENT_FEATUREID:
			if (!IS_ERR_OR_NULL(smem_pointer + size_of_u32)) {
				if (dsp_id == ADSP)
					g_sysmon_stats.sysmon_event_stats_adsp =
					(struct sysmon_smem_q6_event_stats *)
					(smem_pointer + size_of_u32);
				else if (dsp_id == CDSP)
					g_sysmon_stats.sysmon_event_stats_cdsp =
					(struct sysmon_smem_q6_event_stats *)
					(smem_pointer + size_of_u32);
				else if (dsp_id == SLPI)
					g_sysmon_stats.sysmon_event_stats_slpi =
					(struct sysmon_smem_q6_event_stats *)
					(smem_pointer + size_of_u32);
				else
					pr_err("%s:subsystem not found %d\n",
					__func__, SYSMONSTATS_Q6_EVENT_FEATUREID);
			} else {
				pr_err("%s: Failed to fetch %d feature pointer\n",
					__func__, SYSMONSTATS_Q6_EVENT_FEATUREID);
				size = 0;
			}
		break;
		case SYSMONSTATS_Q6_LOAD_FEATUREID:
			if (!IS_ERR_OR_NULL(smem_pointer + size_of_u32)) {
				if (dsp_id == ADSP)
					g_sysmon_stats.q6_avg_load_adsp =
					(u32 *)(smem_pointer + size_of_u32);
				else if (dsp_id == CDSP)
					g_sysmon_stats.q6_avg_load_cdsp =
					(u32 *)(smem_pointer + size_of_u32);
				else if (dsp_id == SLPI)
					g_sysmon_stats.q6_avg_load_slpi =
					(u32 *)(smem_pointer + size_of_u32);
				else
					pr_err("%s:subsystem not found %d\n",
						__func__, SYSMONSTATS_Q6_LOAD_FEATUREID);
			} else {
				pr_err("%s: Failed to fetch %d feature pointer\n",
					__func__, SYSMONSTATS_Q6_LOAD_FEATUREID);
				size = 0;
			}
		break;
		case SYSMON_HMX_UTIL_FEATUREID:
			if (!IS_ERR_OR_NULL(smem_pointer + size_of_u32)) {
				if (dsp_id == CDSP) {
					g_sysmon_stats.hmx_util =
							(u32 *)(smem_pointer + size_of_u32);
				} else
					pr_err("%s:subsystem not supported %d\n",
						__func__, SYSMON_HMX_UTIL_FEATUREID);
			} else {
				pr_err("%s: Failed to fetch %d feature pointer\n",
					__func__, SYSMON_HMX_UTIL_FEATUREID);
				size = 0;
			}
		break;
		case SYSMON_HVX_UTIL_FEATUREID:
			if (!IS_ERR_OR_NULL(smem_pointer + size_of_u32)) {
				if (dsp_id == CDSP) {
					g_sysmon_stats.hvx_util =
							(u32 *)(smem_pointer + size_of_u32);
				} else
					pr_err("%s:subsystem not supported %d\n",
						__func__, SYSMON_HVX_UTIL_FEATUREID);
			} else {
				pr_err("%s: Failed to fetch %d feature pointer\n",
					__func__, SYSMON_HVX_UTIL_FEATUREID);
				size = 0;
			}
		break;
		default:
			pr_err("%s: Requested feature not found, Feature:%u\n",
					__func__, feature);
		break;
		}
		if (!IS_ERR_OR_NULL(smem_pointer + size_rcvd)
					&& (size > size_rcvd)) {
			featureId = *(unsigned int *)(smem_pointer + size_rcvd);
			smem_pointer += size_rcvd;
			size = size - size_rcvd;
			feature = featureId >> 28;
			size_rcvd = (featureId >> 16) & 0xFFF;
		} else {
			size = 0;
		}
	}
}

static void sysmon_smem_init_adsp(void)
{
	size_t size;
	void *smem_pointer_adsp = NULL;

	g_sysmon_stats.smem_init_adsp = true;

	g_sysmon_stats.dsppm_stats_adsp = qcom_smem_get(ADSP,
						DSPPMSTATS_SMEM_ID,
						NULL);

	if (IS_ERR_OR_NULL(g_sysmon_stats.dsppm_stats_adsp)) {
		pr_err("%s:Failed to get fetch dsppm stats from SMEM for ADSP: %d\n",
				__func__, PTR_ERR(g_sysmon_stats.dsppm_stats_adsp));
		g_sysmon_stats.smem_init_adsp = false;
	}

	g_sysmon_stats.sleep_stats_adsp = qcom_smem_get(ADSP,
						SLEEPSTATS_SMEM_ID_ADSP,
						NULL);

	if (IS_ERR_OR_NULL(g_sysmon_stats.sleep_stats_adsp)) {
		pr_err("%s:Failed to get fetch sleep data from SMEM for ADSP: %d\n",
				__func__, PTR_ERR(g_sysmon_stats.sleep_stats_adsp));
		g_sysmon_stats.smem_init_adsp = false;
	}

	g_sysmon_stats.sleep_lpi_adsp = qcom_smem_get(ADSP,
						SLEEPSTATS_LPI_SMEM_ID,
						NULL);
	if (IS_ERR_OR_NULL(g_sysmon_stats.sleep_lpi_adsp)) {
		pr_err("%s:Failed to get fetch LPI sleep data from SMEM for ADSP: %d\n",
				__func__, PTR_ERR(g_sysmon_stats.sleep_lpi_adsp));
		g_sysmon_stats.smem_init_adsp = false;
	}

	smem_pointer_adsp = qcom_smem_get(ADSP,
						SYSMON_SMEM_ID,
						&size);

	if (IS_ERR_OR_NULL(smem_pointer_adsp) || !size) {
		pr_err("%s:Failed to get fetch sysmon data from SMEM for ADSP: %d\n",
				__func__, PTR_ERR(smem_pointer_adsp));
		g_sysmon_stats.smem_init_adsp = false;
	}

	update_sysmon_smem_pointers(smem_pointer_adsp, ADSP, size);

	if (IS_ERR_OR_NULL(g_sysmon_stats.sysmon_event_stats_adsp)) {

		pr_err("%s:Failed to get stats from SMEM for ADSP:\n"
				"event stats:%x\n",
				__func__, g_sysmon_stats.sysmon_event_stats_adsp);
		g_sysmon_stats.smem_init_adsp = false;
	}

	if (IS_ERR_OR_NULL(g_sysmon_stats.sysmon_power_stats_adsp)) {

		pr_err("%s:Failed to get stats from SMEM for ADSP:\n"
				"power stats: %x\n",
				__func__, g_sysmon_stats.sysmon_power_stats_adsp);
		g_sysmon_stats.smem_init_adsp = false;
	}

	if (IS_ERR_OR_NULL(g_sysmon_stats.q6_avg_load_adsp)) {

		pr_err("%s:Failed to get stats from SMEM for ADSP:\n"
				"q6_avg_load: %x\n",
				__func__, g_sysmon_stats.q6_avg_load_adsp);
		g_sysmon_stats.smem_init_adsp = false;
	}
}

static void sysmon_smem_init_cdsp(void)
{
	size_t size;
	void *smem_pointer_cdsp = NULL;

	g_sysmon_stats.smem_init_cdsp = true;
	g_sysmon_stats.smem_init_cdsp_v2 = true;

	g_sysmon_stats.dsppm_stats_cdsp = qcom_smem_get(CDSP,
						DSPPMSTATS_SMEM_ID,
						NULL);

	if (IS_ERR_OR_NULL(g_sysmon_stats.dsppm_stats_cdsp)) {
		pr_err("%s:Failed to get fetch dsppm stats from SMEM for CDSP: %d\n",
				__func__, PTR_ERR(g_sysmon_stats.dsppm_stats_cdsp));
		g_sysmon_stats.smem_init_cdsp = false;
	}

	g_sysmon_stats.sleep_stats_cdsp = qcom_smem_get(CDSP,
						SLEEPSTATS_SMEM_ID_CDSP,
						NULL);

	if (IS_ERR_OR_NULL(g_sysmon_stats.sleep_stats_cdsp)) {
		pr_err("%s:Failed to get fetch sleep data from SMEM for CDSP: %d\n",
				__func__, PTR_ERR(g_sysmon_stats.sleep_stats_cdsp));
		g_sysmon_stats.smem_init_cdsp = false;
	}

	smem_pointer_cdsp = qcom_smem_get(CDSP,
							SYSMON_SMEM_ID,
							&size);
	if (IS_ERR_OR_NULL(smem_pointer_cdsp) || !size) {
		pr_err("%s:Failed to get fetch data from SMEM for CDSP: %d\n",
				__func__, PTR_ERR(smem_pointer_cdsp));
		g_sysmon_stats.smem_init_cdsp = false;
	}

	update_sysmon_smem_pointers(smem_pointer_cdsp, CDSP, size);

	if (IS_ERR_OR_NULL(g_sysmon_stats.sysmon_event_stats_cdsp)) {

		pr_err("%s:Failed to get stats from SMEM for CDSP:\n"
				"event stats:%x\n",
				__func__, g_sysmon_stats.sysmon_event_stats_cdsp);
		g_sysmon_stats.smem_init_cdsp = false;
	}

	if (IS_ERR_OR_NULL(g_sysmon_stats.sysmon_power_stats_cdsp)) {

		pr_err("%s:Failed to get stats from SMEM for CDSP:\n"
				" power stats: %x\n",
				__func__, g_sysmon_stats.sysmon_power_stats_cdsp);
		g_sysmon_stats.smem_init_cdsp = false;
	}

	if (IS_ERR_OR_NULL(g_sysmon_stats.q6_avg_load_cdsp)) {

		pr_err("%s:Failed to get stats from SMEM for CDSP:\n"
				"q6_avg_load: %x\n",
				__func__, g_sysmon_stats.q6_avg_load_cdsp);
		g_sysmon_stats.smem_init_cdsp = false;
	}

	if (IS_ERR_OR_NULL(g_sysmon_stats.hmx_util)) {

		pr_err("%s:Failed to get stats from SMEM for CDSP:\n"
				"hmx_util: %x\n",
				__func__, g_sysmon_stats.hmx_util);
		g_sysmon_stats.smem_init_cdsp_v2 = false;
	}

	if (IS_ERR_OR_NULL(g_sysmon_stats.hvx_util)) {

		pr_err("%s:Failed to get stats from SMEM for CDSP:\n"
				"hmx_util: %x\n",
				__func__, g_sysmon_stats.hvx_util);
		g_sysmon_stats.smem_init_cdsp_v2 = false;
	}

}
static void sysmon_smem_init_slpi(void)
{
	size_t size;
	void *smem_pointer_slpi = NULL;

	g_sysmon_stats.smem_init_slpi = true;

	g_sysmon_stats.sleep_stats_slpi = qcom_smem_get(SLPI,
						SLEEPSTATS_SMEM_ID_SLPI,
						NULL);

	if (IS_ERR_OR_NULL(g_sysmon_stats.sleep_stats_slpi)) {
		pr_err("%s:Failed to get fetch sleep data from SMEM for SLPI: %d\n",
				__func__, PTR_ERR(g_sysmon_stats.sleep_stats_slpi));
		g_sysmon_stats.smem_init_slpi = false;
	}

	g_sysmon_stats.sleep_lpi_slpi = qcom_smem_get(SLPI,
						SLEEPSTATS_LPI_SMEM_ID,
						NULL);
	if (IS_ERR_OR_NULL(g_sysmon_stats.sleep_lpi_slpi)) {
		pr_err("%s:Failed to get fetch LPI sleep data from SMEM for SLPI: %d\n",
				__func__, PTR_ERR(g_sysmon_stats.sleep_lpi_slpi));
		g_sysmon_stats.smem_init_slpi = false;
	}

	smem_pointer_slpi = qcom_smem_get(SLPI,
							SYSMON_SMEM_ID,
							&size);

	if (IS_ERR_OR_NULL(smem_pointer_slpi) || !size) {
		pr_err("%s:Failed to get fetch data from SMEM for SLPI: %d\n",
				__func__, PTR_ERR(smem_pointer_slpi));
	}

	update_sysmon_smem_pointers(smem_pointer_slpi, SLPI, size);

	if (IS_ERR_OR_NULL(g_sysmon_stats.sysmon_event_stats_slpi)) {

		pr_err("%s:Failed to get stats from SMEM for SLPI:\n"
				"event stats:%x\n",
				__func__, g_sysmon_stats.sysmon_event_stats_slpi);
		g_sysmon_stats.smem_init_slpi = false;
	}

	if (IS_ERR_OR_NULL(g_sysmon_stats.sysmon_power_stats_slpi)) {

		pr_err("%s:Failed to get stats from SMEM for SLPI:\n"
				"power stats: %x\n",
				__func__, g_sysmon_stats.sysmon_power_stats_slpi);
		g_sysmon_stats.smem_init_slpi = false;
	}

	if (IS_ERR_OR_NULL(g_sysmon_stats.q6_avg_load_slpi)) {

		pr_err("%s:Failed to get stats from SMEM for SLPI:\n"
				"q6_avg_load: %x\n",
				__func__, g_sysmon_stats.q6_avg_load_slpi);
		g_sysmon_stats.smem_init_slpi = false;
	}
}
/**
 * sysmon_read_hmx_util() - Checks for CDSP power collapse
 * and resets the HMX utilization to zero if dsp is power collapsed.
 */
static u32 sysmon_read_hmx_util(void)
{
	struct sysmon_smem_hmx_stats sysmon_hmx_util;
	u64 curr_timestamp = __arch_counter_get_cntvct();
	u64 last_hmx_update_at = 0;

	memcpy(&sysmon_hmx_util,
		g_sysmon_stats.hmx_util,
		sizeof(struct sysmon_smem_hmx_stats));

	last_hmx_update_at =
		(u64) (((u64)sysmon_hmx_util.Last_update_time_load_msb << 32)|
				sysmon_hmx_util.Last_update_time_load_lsb);

	if ((curr_timestamp > last_hmx_update_at) &&
		((curr_timestamp - last_hmx_update_at) / SYS_CLK_TICKS_PER_MS) > 100) {
		sysmon_hmx_util.hmx_util_avg = 0;
	}

	if (sysmon_hmx_util.hmx_util_avg == HMX_HVX_PMU_EVENTS_NA)
		pr_err("HMX PMU not registered, user ovveride pmu counter\n");

	return sysmon_hmx_util.hmx_util_avg;
}

/**
 * sysmon_stats_query_hmx_utlization() - * API to query
 * CDSP hmx utlization.On success, returns HMX utilization
 * in the hmx_util parameter.
 * @arg1: u32 pointer to HMX utilization in percentage.
 * @return: SUCCESS (0) if Query is successful
 *        FAILURE (Non-zero) if Query could not be processed, refer error codes.
 */
int sysmon_stats_query_hmx_utlization(u32 *hmx_util)
{
	u32 hmx_utilization = 0;
	int ret = 0;

	if (!g_sysmon_stats.smem_init_cdsp_v2)
		sysmon_smem_init_cdsp();

	if (!IS_ERR_OR_NULL(g_sysmon_stats.hmx_util)) {
		hmx_utilization = sysmon_read_hmx_util();

		if (hmx_utilization == HMX_HVX_PMU_EVENTS_NA) {
			hmx_utilization = 0;
			ret = DSP_PMU_COUNTER_NA;
		}

		memcpy(hmx_util, &hmx_utilization, sizeof(u32));
	} else
		return -ENOKEY;

	return ret;
}
EXPORT_SYMBOL(sysmon_stats_query_hmx_utlization);
/**
 * sysmon_read_hvx_util() - Checks for CDSP power collapse
 * and resets the HVX utilization to zero if dsp is power collapsed.
 */
static u32 sysmon_read_hvx_util(void)
{
	struct sysmon_smem_hvx_stats sysmon_hvx_util;
	u64 curr_timestamp = __arch_counter_get_cntvct();
	u64 last_hvx_update_at = 0;

	memcpy(&sysmon_hvx_util,
		g_sysmon_stats.hvx_util,
		sizeof(struct sysmon_smem_hvx_stats));

	last_hvx_update_at =
		(u64) (((u64)sysmon_hvx_util.Last_update_time_load_msb << 32)|
				sysmon_hvx_util.Last_update_time_load_lsb);

	if ((curr_timestamp > last_hvx_update_at) &&
		((curr_timestamp - last_hvx_update_at) / SYS_CLK_TICKS_PER_MS) > 100) {
		sysmon_hvx_util.hvx_util_avg = 0;
	}

	if (sysmon_hvx_util.hvx_util_avg == HMX_HVX_PMU_EVENTS_NA)
		pr_err("HVX PMU not registered, user ovveride pmu counter\n");

	return sysmon_hvx_util.hvx_util_avg;
}

/**
 * sysmon_stats_query_hvx_utlization() - * API to query
 * CDSP subsystem hvx utlization.On success, returns HVX utilization
 * in the hvx_util parameter.
 * @arg1: u32 pointer to HVX utilization in percentage.
 * @return: SUCCESS (0) if Query is successful
 *        FAILURE (Non-zero) if Query could not be processed, refer error codes.
 */
int sysmon_stats_query_hvx_utlization(u32 *hvx_util)
{
	u32 hvx_utilization = 0;
	int ret = 0;

	if (!g_sysmon_stats.smem_init_cdsp_v2)
		sysmon_smem_init_cdsp();

	if (!IS_ERR_OR_NULL(g_sysmon_stats.hvx_util)) {
		hvx_utilization = sysmon_read_hvx_util();

		if (hvx_utilization == HMX_HVX_PMU_EVENTS_NA) {
			hvx_utilization = 0;
			ret = DSP_PMU_COUNTER_NA;
		}

		memcpy(hvx_util, &hvx_utilization, sizeof(u32));
	} else
		return -ENOKEY;

	return ret;
}
EXPORT_SYMBOL(sysmon_stats_query_hvx_utlization);
/**
 * sysmon_stats_query_power_residency() - * API to query requested
 * DSP subsystem power residency.On success, returns power residency
 * statistics in the given sysmon_smem_power_stats structure.
 */
int sysmon_stats_query_power_residency(enum dsp_id_t dsp_id,
					struct sysmon_smem_power_stats *sysmon_power_stats)
{
	int ret = 0, size = 0;
	u64 lpi_accumulated = 0;
	u64 lpm_accumulated = 0;
	struct sysmon_smem_power_stats_extended *ptr = NULL, smem_sysmon_power_stats_l = { 0 };

	if (!sysmon_power_stats) {
		pr_err("%s: Null pointer received\n", __func__);
		return -EINVAL;
	}

	switch (dsp_id) {
	case ADSP:
		if (!g_sysmon_stats.smem_init_adsp)
			sysmon_smem_init_adsp();

		if (!IS_ERR_OR_NULL(g_sysmon_stats.sysmon_power_stats_adsp)) {
			ptr = g_sysmon_stats.sysmon_power_stats_adsp;
			size = (((ptr->powerstats.version) >> 16) & 0xFFF) + sizeof(u32);
			memcpy(&smem_sysmon_power_stats_l,
					g_sysmon_stats.sysmon_power_stats_adsp,
					size <= sizeof(struct sysmon_smem_power_stats_extended) ?
					size :
					sizeof(struct sysmon_smem_power_stats_extended));

			if (g_sysmon_stats.sleep_stats_adsp) {
				lpm_accumulated = g_sysmon_stats.sleep_stats_adsp->accumulated;
				if (g_sysmon_stats.sleep_stats_adsp->last_entered_at >
						g_sysmon_stats.sleep_stats_adsp->last_exited_at)
					lpm_accumulated += arch_timer_read_counter() -
						g_sysmon_stats.sleep_stats_adsp->last_entered_at;
			}

			if (g_sysmon_stats.sleep_lpi_adsp) {
				lpi_accumulated = g_sysmon_stats.sleep_lpi_adsp->accumulated;
				if (g_sysmon_stats.sleep_lpi_adsp->last_entered_at >
					g_sysmon_stats.sleep_lpi_adsp->last_exited_at)
					lpi_accumulated += arch_timer_read_counter() -
						g_sysmon_stats.sleep_lpi_adsp->last_entered_at;
			}
		} else
			ret = -ENOKEY;
	break;
	case CDSP:
		if (!g_sysmon_stats.smem_init_cdsp)
			sysmon_smem_init_cdsp();

		if (!IS_ERR_OR_NULL(g_sysmon_stats.sysmon_power_stats_cdsp)) {
			ptr = g_sysmon_stats.sysmon_power_stats_cdsp;
			size = (((ptr->powerstats.version) >> 16) & 0xFFF) + sizeof(u32);
			memcpy(&smem_sysmon_power_stats_l, g_sysmon_stats.sysmon_power_stats_cdsp,
				size <= sizeof(struct sysmon_smem_power_stats_extended) ? size :
				sizeof(struct sysmon_smem_power_stats_extended));
			if (g_sysmon_stats.sleep_stats_cdsp) {
				lpm_accumulated = g_sysmon_stats.sleep_stats_cdsp->accumulated;
				if (g_sysmon_stats.sleep_stats_cdsp->last_entered_at >
						g_sysmon_stats.sleep_stats_cdsp->last_exited_at)
					lpm_accumulated += arch_timer_read_counter() -
						g_sysmon_stats.sleep_stats_cdsp->last_entered_at;
			}
		} else
			ret = -ENOKEY;
	break;
	case SLPI:
		if (!g_sysmon_stats.smem_init_slpi)
			sysmon_smem_init_slpi();

		if (!IS_ERR_OR_NULL(g_sysmon_stats.sysmon_power_stats_slpi)) {
			ptr = g_sysmon_stats.sysmon_power_stats_slpi;
			size = (((ptr->powerstats.version) >> 16) & 0xFFF) + sizeof(u32);
			memcpy(&smem_sysmon_power_stats_l, g_sysmon_stats.sysmon_power_stats_slpi,
				size <= sizeof(struct sysmon_smem_power_stats_extended) ? size :
				sizeof(struct sysmon_smem_power_stats_extended));
			if (g_sysmon_stats.sleep_stats_slpi) {
				lpm_accumulated = g_sysmon_stats.sleep_stats_slpi->accumulated;
				if (g_sysmon_stats.sleep_stats_slpi->last_entered_at >
					g_sysmon_stats.sleep_stats_slpi->last_exited_at)
					lpm_accumulated += arch_timer_read_counter() -
						g_sysmon_stats.sleep_stats_slpi->last_entered_at;
			}
			if (g_sysmon_stats.sleep_lpi_slpi) {
				lpi_accumulated = g_sysmon_stats.sleep_lpi_slpi->accumulated;
				if (g_sysmon_stats.sleep_lpi_slpi->last_entered_at >
					g_sysmon_stats.sleep_lpi_slpi->last_exited_at)
					lpi_accumulated += arch_timer_read_counter() -
						g_sysmon_stats.sleep_lpi_slpi->last_entered_at;
			}
		} else
			ret = -ENOKEY;
	break;
	default:
		pr_err("%s:Provided subsystem %d is not supported\n", __func__, dsp_id);
		ret = -EINVAL;
	break;
	}

	if (ret == 0) {
		ret = add_delta_time((ptr->powerstats.version) & 0xFF, lpi_accumulated,
				lpm_accumulated, &smem_sysmon_power_stats_l);
		if (ret == 0) {
			memcpy(sysmon_power_stats, &smem_sysmon_power_stats_l.powerstats,
				sizeof(struct sysmon_smem_power_stats));
			sysmon_power_stats->version = (ptr->powerstats.version) & 0xFF;
		}
	}

	return ret;
}
EXPORT_SYMBOL(sysmon_stats_query_power_residency);
/**
 * API to query requested DSP subsystem's Q6 clock and bandwidth.
 * On success, returns Q6 clock and bandwidth statistics in the given
 * sysmon_smem_q6_event_stats structure.
 */
int sysmon_stats_query_q6_votes(enum dsp_id_t dsp_id,
		struct sysmon_smem_q6_event_stats *sysmon_q6_event_stats)
{
	int ret = 0;

	if (!sysmon_q6_event_stats) {
		pr_err("%s: Null pointer received\n", __func__);
		return -EINVAL;
	}

	switch (dsp_id) {
	case ADSP:
		if (!g_sysmon_stats.smem_init_adsp)
			sysmon_smem_init_adsp();

		if (!IS_ERR_OR_NULL(g_sysmon_stats.sysmon_event_stats_adsp))
			memcpy(sysmon_q6_event_stats, g_sysmon_stats.sysmon_event_stats_adsp,
				sizeof(struct sysmon_smem_q6_event_stats));
		else
			ret = -ENOKEY;
	break;
	case CDSP:
		if (!g_sysmon_stats.smem_init_cdsp)
			sysmon_smem_init_cdsp();

		if (!IS_ERR_OR_NULL(g_sysmon_stats.sysmon_event_stats_cdsp))
			memcpy(sysmon_q6_event_stats, g_sysmon_stats.sysmon_event_stats_cdsp,
				sizeof(struct sysmon_smem_q6_event_stats));
		else
			ret = -ENOKEY;
	break;
	case SLPI:
		if (!g_sysmon_stats.smem_init_slpi)
			sysmon_smem_init_slpi();

		if (!IS_ERR_OR_NULL(g_sysmon_stats.sysmon_event_stats_slpi))
			memcpy(sysmon_q6_event_stats, g_sysmon_stats.sysmon_event_stats_slpi,
				sizeof(struct sysmon_smem_q6_event_stats));
		else
			ret = -ENOKEY;
	break;
	default:
		pr_err("%s:Provided subsystem %d is not supported\n", __func__, dsp_id);
		ret = -EINVAL;
	break;
	}

	return ret;
}
EXPORT_SYMBOL(sysmon_stats_query_q6_votes);

/*Checks for DSP power collapse and resets the q6 average
 *load to zero if dsp is power collapsed.
 */
u32 sysmon_read_q6_load(enum dsp_id_t dsp_id)
{
	struct sysmon_smem_q6_load_stats sysmon_q6_load;
	u64 last_q6load_update_at = 0;
	u64 curr_timestamp = __arch_counter_get_cntvct();

	switch (dsp_id) {
	case ADSP:
		memcpy(&sysmon_q6_load,
			g_sysmon_stats.q6_avg_load_adsp,
			sizeof(struct sysmon_smem_q6_load_stats));

		last_q6load_update_at =
			(u64) (((u64)sysmon_q6_load.Last_update_time_load_msb<<32)|
					sysmon_q6_load.Last_update_time_load_lsb);

		if ((curr_timestamp > last_q6load_update_at) &&
			((curr_timestamp - last_q6load_update_at) / SYS_CLK_TICKS_PER_MS) > 100) {
			sysmon_q6_load.q6load_avg = 0;
		}
	break;
	case CDSP:
		memcpy(&sysmon_q6_load,
			g_sysmon_stats.q6_avg_load_cdsp,
			sizeof(struct sysmon_smem_q6_load_stats));

		last_q6load_update_at =
			(u64) (((u64)sysmon_q6_load.Last_update_time_load_msb << 32)|
					sysmon_q6_load.Last_update_time_load_lsb);

		if ((curr_timestamp > last_q6load_update_at) &&
			((curr_timestamp - last_q6load_update_at) / SYS_CLK_TICKS_PER_MS) > 100) {
			sysmon_q6_load.q6load_avg = 0;
		}
	break;
	case SLPI:
		memcpy(&sysmon_q6_load,
			g_sysmon_stats.q6_avg_load_slpi,
			sizeof(struct sysmon_smem_q6_load_stats));

		last_q6load_update_at =
			(u64) (((u64)sysmon_q6_load.Last_update_time_load_msb << 32)|
						sysmon_q6_load.Last_update_time_load_lsb);

		if ((curr_timestamp > last_q6load_update_at) &&
			((curr_timestamp - last_q6load_update_at) / SYS_CLK_TICKS_PER_MS) > 100) {
			sysmon_q6_load.q6load_avg = 0;
		}
	break;
	default:
		pr_err("%s:Provided subsystem %d is not supported\n", __func__, dsp_id);
		return -EINVAL;
	break;
	}

	return sysmon_q6_load.q6load_avg;
}

/**
 * API to query requested DSP subsystem's Q6 load.
 * On success, returns average Q6 load in KCPS for the given
 * q6load_avg parameter.
 */
int sysmon_stats_query_q6_load(enum dsp_id_t dsp_id, u32 *q6load_avg)
{
	int ret = 0;
	u32 q6_average_load = 0;

	if (!q6load_avg) {
		pr_err("%s: Null pointer received\n", __func__);
		return -EINVAL;
	}

	switch (dsp_id) {
	case ADSP:
		if (!g_sysmon_stats.smem_init_adsp)
			sysmon_smem_init_adsp();

		if (!IS_ERR_OR_NULL(g_sysmon_stats.q6_avg_load_adsp)) {
			q6_average_load = sysmon_read_q6_load(ADSP);
			memcpy(q6load_avg, &q6_average_load, sizeof(u32));
		} else
			ret = -ENOKEY;
	break;
	case CDSP:
		if (!g_sysmon_stats.smem_init_cdsp)
			sysmon_smem_init_cdsp();

		if (!IS_ERR_OR_NULL(g_sysmon_stats.q6_avg_load_cdsp)) {
			q6_average_load = sysmon_read_q6_load(CDSP);
			memcpy(q6load_avg, &q6_average_load, sizeof(u32));
		} else
			ret = -ENOKEY;
	break;
	case SLPI:
		if (!g_sysmon_stats.smem_init_slpi)
			sysmon_smem_init_slpi();

		if (!IS_ERR_OR_NULL(g_sysmon_stats.q6_avg_load_slpi)) {
			q6_average_load = sysmon_read_q6_load(SLPI);
			memcpy(q6load_avg, &q6_average_load, sizeof(u32));
		} else
			ret = -ENOKEY;
	break;
	default:
		pr_err("%s:Provided subsystem %d is not supported\n", __func__, dsp_id);
		ret = -EINVAL;
	break;
	}

	return ret;

}
EXPORT_SYMBOL(sysmon_stats_query_q6_load);
/*
 * API to query requested DSP subsystem sleep stats for
 * LPM and LPI.On success, returns sleep
 * statistics in the given sleep_stats structure for LPM and
 * LPI(on supported subsystems).
 */
int sysmon_stats_query_sleep(enum dsp_id_t dsp_id,
					struct sleep_stats *sleep_stats_lpm,
					struct sleep_stats_island *sleep_stats_lpi)
{
	int ret = 0;

	if (!sleep_stats_lpm && !sleep_stats_lpi) {
		pr_err("%s: Null pointer received\n", __func__);
		return -EINVAL;
	}
	switch (dsp_id) {
	case ADSP:
		if (!g_sysmon_stats.smem_init_adsp)
			sysmon_smem_init_adsp();
		/*
		 * If a subsystem is in sleep when reading the sleep stats adjust
		 * the accumulated sleep duration to show actual sleep time.
		 */
		if (sleep_stats_lpm) {
			if (!IS_ERR_OR_NULL(g_sysmon_stats.sleep_stats_adsp)) {
				if (g_sysmon_stats.sleep_stats_adsp->last_entered_at >
					g_sysmon_stats.sleep_stats_adsp->last_exited_at)
					g_sysmon_stats.sleep_stats_adsp->accumulated
						+= arch_timer_read_counter() -
						g_sysmon_stats.sleep_stats_adsp->last_entered_at;

				memcpy(sleep_stats_lpm, g_sysmon_stats.sleep_stats_adsp,
							sizeof(struct sleep_stats));
			} else
				ret = -ENOKEY;
		}

		if (sleep_stats_lpi) {
			if (!IS_ERR_OR_NULL(g_sysmon_stats.sleep_lpi_adsp)) {
				if (g_sysmon_stats.sleep_lpi_adsp->last_entered_at >
					g_sysmon_stats.sleep_lpi_adsp->last_exited_at)
					g_sysmon_stats.sleep_lpi_adsp->accumulated
						+= arch_timer_read_counter() -
						g_sysmon_stats.sleep_lpi_adsp->last_entered_at;

				memcpy(sleep_stats_lpi, g_sysmon_stats.sleep_lpi_adsp,
							sizeof(struct sleep_stats_island));
			} else
				ret = -ENOKEY;
		}
	break;
	case CDSP:
		if (!g_sysmon_stats.smem_init_cdsp)
			sysmon_smem_init_cdsp();

		if (sleep_stats_lpm) {
			if (!IS_ERR_OR_NULL(g_sysmon_stats.sleep_stats_cdsp)) {
				if (g_sysmon_stats.sleep_stats_cdsp->last_entered_at >
					g_sysmon_stats.sleep_stats_cdsp->last_exited_at)
					g_sysmon_stats.sleep_stats_cdsp->accumulated
						+= arch_timer_read_counter() -
						g_sysmon_stats.sleep_stats_cdsp->last_entered_at;

				memcpy(sleep_stats_lpm, g_sysmon_stats.sleep_stats_cdsp,
						sizeof(struct sleep_stats));
			} else
				ret = -ENOKEY;
		}
	break;
	case SLPI:
		if (!g_sysmon_stats.smem_init_slpi)
			sysmon_smem_init_slpi();

		if (sleep_stats_lpm) {
			if (!IS_ERR_OR_NULL(g_sysmon_stats.sleep_stats_slpi)) {
				if (g_sysmon_stats.sleep_stats_slpi->last_entered_at >
					g_sysmon_stats.sleep_stats_slpi->last_exited_at)
					g_sysmon_stats.sleep_stats_slpi->accumulated
						+= arch_timer_read_counter() -
						g_sysmon_stats.sleep_stats_slpi->last_entered_at;

				memcpy(sleep_stats_lpm, g_sysmon_stats.sleep_stats_slpi,
							sizeof(struct sleep_stats));
			} else
				ret = -ENOKEY;
		}

		if (sleep_stats_lpi) {
			if (!IS_ERR_OR_NULL(g_sysmon_stats.sleep_lpi_slpi)) {
				if (g_sysmon_stats.sleep_lpi_slpi->last_entered_at >
					g_sysmon_stats.sleep_lpi_slpi->last_exited_at)
					g_sysmon_stats.sleep_lpi_slpi->accumulated
						+= arch_timer_read_counter() -
						g_sysmon_stats.sleep_lpi_slpi->last_entered_at;

				memcpy(sleep_stats_lpi, g_sysmon_stats.sleep_lpi_slpi,
							sizeof(struct sleep_stats_island));
			} else
				ret = -ENOKEY;
		}

	break;
	default:
		pr_err("%s:Provided subsystem %d is not supported\n", __func__, dsp_id);
		ret = -EINVAL;
	break;
	}

	return ret;
}
EXPORT_SYMBOL(sysmon_stats_query_sleep);

static int master_adsp_stats_show(struct seq_file *s, void *d)
{
	int i = 0, j = 0, ret = 0;
	u64 lpi_accumulated = 0;
	u64 lpm_accumulated = 0;
	u32 q6_load;
	u8 ver = 0;
	struct sysmon_smem_power_stats_extended *ptr = NULL, sysmon_power_stats = { 0 };

	if (!g_sysmon_stats.smem_init_adsp)
		sysmon_smem_init_adsp();

	if (g_sysmon_stats.sysmon_event_stats_adsp) {
		seq_puts(s, "\nsysMon stats:\n\n");
		seq_printf(s, "Core clock(KHz): %d\n",
				g_sysmon_stats.sysmon_event_stats_adsp->QDSP6_clk);
		seq_printf(s, "Ab vote(Bytes): %llu\n",
				(((u64)g_sysmon_stats.sysmon_event_stats_adsp->Ab_vote_msb << 32) |
					   g_sysmon_stats.sysmon_event_stats_adsp->Ab_vote_lsb));
		seq_printf(s, "Ib vote(Bytes): %llu\n",
				(((u64)g_sysmon_stats.sysmon_event_stats_adsp->Ib_vote_msb << 32) |
					   g_sysmon_stats.sysmon_event_stats_adsp->Ib_vote_lsb));
		seq_printf(s, "Sleep latency(usec): %u\n",
				g_sysmon_stats.sysmon_event_stats_adsp->Sleep_latency > 0 ?
				g_sysmon_stats.sysmon_event_stats_adsp->Sleep_latency : U32_MAX);
	}

	if (g_sysmon_stats.dsppm_stats_adsp) {
		seq_puts(s, "\nDSPPM stats:\n\n");
		seq_printf(s, "Version: %u\n", g_sysmon_stats.dsppm_stats_adsp->version);
		seq_printf(s, "Sleep latency(usec): %u\n",
					g_sysmon_stats.dsppm_stats_adsp->latency_us ?
					g_sysmon_stats.dsppm_stats_adsp->latency_us : U32_MAX);
		seq_printf(s, "Timestamp: %llu\n", g_sysmon_stats.dsppm_stats_adsp->timestamp);

		for (; i < DSPPMSTATS_NUMPD; i++) {
			seq_printf(s, "Pid: %d, Num active clients: %d\n",
						g_sysmon_stats.dsppm_stats_adsp->pd[i].pid,
						g_sysmon_stats.dsppm_stats_adsp->pd[i].num_active);
		}
	}

	if (g_sysmon_stats.sleep_stats_adsp) {
		lpm_accumulated = g_sysmon_stats.sleep_stats_adsp->accumulated;

		if (g_sysmon_stats.sleep_stats_adsp->last_entered_at >
					g_sysmon_stats.sleep_stats_adsp->last_exited_at)
			lpm_accumulated += arch_timer_read_counter() -
						g_sysmon_stats.sleep_stats_adsp->last_entered_at;

		seq_puts(s, "\nLPM stats:\n\n");
		seq_printf(s, "Count = %u\n", g_sysmon_stats.sleep_stats_adsp->count);
		seq_printf(s, "Last Entered At = %llu\n",
			g_sysmon_stats.sleep_stats_adsp->last_entered_at);
		seq_printf(s, "Last Exited At = %llu\n",
			g_sysmon_stats.sleep_stats_adsp->last_exited_at);
		seq_printf(s, "Accumulated Duration = %llu\n", lpm_accumulated);
	}

	if (g_sysmon_stats.sleep_lpi_adsp) {
		lpi_accumulated = g_sysmon_stats.sleep_lpi_adsp->accumulated;

		if (g_sysmon_stats.sleep_lpi_adsp->last_entered_at >
					g_sysmon_stats.sleep_lpi_adsp->last_exited_at)
			lpi_accumulated += arch_timer_read_counter() -
					g_sysmon_stats.sleep_lpi_adsp->last_entered_at;

		seq_puts(s, "\nLPI stats:\n\n");
		seq_printf(s, "Count = %u\n", g_sysmon_stats.sleep_lpi_adsp->count);
		seq_printf(s, "Last Entered At = %llu\n",
			g_sysmon_stats.sleep_lpi_adsp->last_entered_at);
		seq_printf(s, "Last Exited At = %llu\n",
			g_sysmon_stats.sleep_lpi_adsp->last_exited_at);
		seq_printf(s, "Accumulated Duration = %llu\n",
			lpi_accumulated);
	}

	if (g_sysmon_stats.sysmon_power_stats_adsp) {
		memcpy(&sysmon_power_stats, g_sysmon_stats.sysmon_power_stats_adsp,
				sizeof(struct sysmon_smem_power_stats_extended));
		ptr = g_sysmon_stats.sysmon_power_stats_adsp;
		ver = (ptr->powerstats.version) & 0xFF;

		ret = add_delta_time(ver, lpi_accumulated,
				lpm_accumulated, &sysmon_power_stats);

		if (ret != 0)
			seq_puts(s, "\nWarning: Power Stats are might be Invalid\n");

		seq_puts(s, "\nPower Stats:\n\n");
		for (j = 0; j < SYSMON_POWER_STATS_MAX_CLK_LEVELS; j++) {
			if (sysmon_power_stats.powerstats.clk_arr[j]) {
				if (ver >= 2) {
					seq_printf(s, "%u : Core Clock(KHz) : %u \tActive Time(sec) : %u \tLPI time(sec) : %u\n",
						j,
						sysmon_power_stats.powerstats.clk_arr[j],
						sysmon_power_stats.powerstats.active_time[j],
						sysmon_power_stats.powerstats.island_time[j]);
				} else {
					seq_printf(s, "%u : Core Clock(KHz): %u \tActive Time(sec): %u\n",
						j,
						sysmon_power_stats.powerstats.clk_arr[j],
						sysmon_power_stats.powerstats.active_time[j]);
				}
			}
		}

		seq_printf(s, "Power collapse time(sec) = %u\n",
			sysmon_power_stats.powerstats.pc_time);
		seq_printf(s, "Total LPI time(sec) = %u\n",
			sysmon_power_stats.powerstats.lpi_time);
		if (ver >= 2)
			seq_printf(s, "Current core clock(KHz) = %u\n",
				sysmon_power_stats.powerstats.current_clk);
	}

	if (g_sysmon_stats.q6_avg_load_adsp) {
		seq_puts(s, "\nQ6 load:\n\n");
		q6_load = sysmon_read_q6_load(ADSP);
		seq_printf(s, "Average Q6 load in KCPS = %u\n", q6_load);
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(master_adsp_stats);

static int master_cdsp_stats_show(struct seq_file *s, void *d)
{
	int i = 0, j = 0, ret = 0;
	u32 hmx_util, hvx_util;
	u32 q6_load;
	u64 lpm_accumulated = 0;
	u8 ver = 0;
	struct sysmon_smem_power_stats_extended *ptr = NULL, sysmon_power_stats = { 0 };

	if (!g_sysmon_stats.smem_init_cdsp)
		sysmon_smem_init_cdsp();

	if (g_sysmon_stats.sysmon_event_stats_cdsp) {
		seq_puts(s, "\nsysMon stats:\n\n");
		seq_printf(s, "Core clock(KHz): %d\n",
				g_sysmon_stats.sysmon_event_stats_cdsp->QDSP6_clk);
		seq_printf(s, "Ab vote(Bytes): %llu\n",
				(((u64)g_sysmon_stats.sysmon_event_stats_cdsp->Ab_vote_msb << 32) |
					   g_sysmon_stats.sysmon_event_stats_cdsp->Ab_vote_lsb));
		seq_printf(s, "Ib vote(Bytes): %llu\n",
				(((u64)g_sysmon_stats.sysmon_event_stats_cdsp->Ib_vote_msb << 32) |
					   g_sysmon_stats.sysmon_event_stats_cdsp->Ib_vote_lsb));
		seq_printf(s, "Sleep latency(usec): %u\n",
				g_sysmon_stats.sysmon_event_stats_cdsp->Sleep_latency > 0 ?
				g_sysmon_stats.sysmon_event_stats_cdsp->Sleep_latency : U32_MAX);
	}

	if (g_sysmon_stats.dsppm_stats_cdsp) {
		seq_puts(s, "\nDSPPM stats:\n\n");
		seq_printf(s, "Version: %u\n",
			g_sysmon_stats.dsppm_stats_cdsp->version);
		seq_printf(s, "Sleep latency(usec): %u\n",
			g_sysmon_stats.dsppm_stats_cdsp->latency_us ?
			g_sysmon_stats.dsppm_stats_cdsp->latency_us : U32_MAX);
		seq_printf(s, "Timestamp: %llu\n",
			g_sysmon_stats.dsppm_stats_cdsp->timestamp);

		for (; i < DSPPMSTATS_NUMPD; i++) {
			seq_printf(s, "Pid: %d, Num active clients: %d\n",
						g_sysmon_stats.dsppm_stats_cdsp->pd[i].pid,
						g_sysmon_stats.dsppm_stats_cdsp->pd[i].num_active);
		}
	}

	if (g_sysmon_stats.sleep_stats_cdsp) {
		lpm_accumulated = g_sysmon_stats.sleep_stats_cdsp->accumulated;

		if (g_sysmon_stats.sleep_stats_cdsp->last_entered_at >
					g_sysmon_stats.sleep_stats_cdsp->last_exited_at)
			lpm_accumulated += arch_timer_read_counter() -
						g_sysmon_stats.sleep_stats_cdsp->last_entered_at;

		seq_puts(s, "\nLPM stats:\n\n");
		seq_printf(s, "Count = %u\n",
			g_sysmon_stats.sleep_stats_cdsp->count);
		seq_printf(s, "Last Entered At = %llu\n",
			g_sysmon_stats.sleep_stats_cdsp->last_entered_at);
		seq_printf(s, "Last Exited At = %llu\n",
			g_sysmon_stats.sleep_stats_cdsp->last_exited_at);
		seq_printf(s, "Accumulated Duration = %llu\n", lpm_accumulated);
	}

	if (g_sysmon_stats.sysmon_power_stats_cdsp) {
		memcpy(&sysmon_power_stats, g_sysmon_stats.sysmon_power_stats_cdsp,
				sizeof(struct sysmon_smem_power_stats_extended));
		ptr = g_sysmon_stats.sysmon_power_stats_cdsp;
		ver = (ptr->powerstats.version) & 0xFF;
		ret = add_delta_time(ver, 0, lpm_accumulated, &sysmon_power_stats);

		if (ret)
			seq_puts(s, "\nWarning: Power Stats might be Invalid\n");

		seq_puts(s, "\nPower Stats:\n\n");
		for (j = 0; j < SYSMON_POWER_STATS_MAX_CLK_LEVELS; j++) {
			if (sysmon_power_stats.powerstats.clk_arr[j])
				seq_printf(s, "%u : Core Clock(KHz) : %u \tActive Time(sec) : %u\n",
					j,
					sysmon_power_stats.powerstats.clk_arr[j],
					sysmon_power_stats.powerstats.active_time[j]);
		}
		seq_printf(s, "Power collapse time(sec) = %u\n",
			sysmon_power_stats.powerstats.pc_time);
		seq_printf(s, "Total LPI time(sec) = %u\n",
			sysmon_power_stats.powerstats.lpi_time);

		if (ver >= 2)
			seq_printf(s, "Current core clock(KHz) = %u\n",
				sysmon_power_stats.powerstats.current_clk);

		if (ret)
			return ret;

	}

	if (g_sysmon_stats.q6_avg_load_cdsp) {
		seq_puts(s, "\nQ6 load:\n\n");
		q6_load = sysmon_read_q6_load(CDSP);
		seq_printf(s, "Average Q6 load in KCPS = %u\n", q6_load);
	}

	ret = sysmon_stats_query_hmx_utlization(&hmx_util);

	if (ret) {
		seq_printf(s, "\nHMX stats not available, error code: %d\n", ret);
	} else {
		seq_puts(s, "\nHMX stats:\n\n");
		seq_printf(s, "HMX utilization in percentage = %u\n", hmx_util);
	}

	ret = sysmon_stats_query_hvx_utlization(&hvx_util);

	if (ret) {
		seq_printf(s, "\nHVX stats not available, error code: %d\n", ret);
	} else {
		seq_puts(s, "\nHVX Stats:\n\n");
		seq_printf(s, "HVX utilization in percentage = %u\n", hvx_util);
	}

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(master_cdsp_stats);

static int  __init sysmon_stats_init(void)
{

	g_sysmon_stats.debugfs_dir = debugfs_create_dir("sysmon_subsystem_stats", NULL);

	if (!g_sysmon_stats.debugfs_dir) {
		pr_err("Failed to create debugfs directory for sysmon_subsystem_stats\n");
		goto debugfs_bail;
	}
	g_sysmon_stats.debugfs_master_adsp_stats =
			debugfs_create_file("master_adsp_stats",
			 0444, g_sysmon_stats.debugfs_dir, NULL, &master_adsp_stats_fops);

	if (!g_sysmon_stats.debugfs_master_adsp_stats)
		pr_err("Failed to create debugfs file for ADSP master stats\n");

	g_sysmon_stats.debugfs_master_cdsp_stats =
			debugfs_create_file("master_cdsp_stats",
			 0444, g_sysmon_stats.debugfs_dir, NULL, &master_cdsp_stats_fops);

	if (!g_sysmon_stats.debugfs_master_cdsp_stats)
		pr_err("Failed to create debugfs file for CDSP master stats\n");

debugfs_bail:
		return 0;
}

static void __exit sysmon_stats_exit(void)
{
	debugfs_remove_recursive(g_sysmon_stats.debugfs_dir);
}

module_init(sysmon_stats_init);
module_exit(sysmon_stats_exit);


MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Qualcomm Technologies, Inc. Sysmon subsystem Stats driver");

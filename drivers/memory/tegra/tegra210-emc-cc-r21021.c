// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2014-2020, NVIDIA CORPORATION.  All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/of.h>

#include <soc/tegra/mc.h>

#include "tegra210-emc.h"
#include "tegra210-mc.h"

/*
 * Enable flags for specifying verbosity.
 */
#define INFO            (1 << 0)
#define STEPS           (1 << 1)
#define SUB_STEPS       (1 << 2)
#define PRELOCK         (1 << 3)
#define PRELOCK_STEPS   (1 << 4)
#define ACTIVE_EN       (1 << 5)
#define PRAMP_UP        (1 << 6)
#define PRAMP_DN        (1 << 7)
#define EMA_WRITES      (1 << 10)
#define EMA_UPDATES     (1 << 11)
#define PER_TRAIN       (1 << 16)
#define CC_PRINT        (1 << 17)
#define CCFIFO          (1 << 29)
#define REGS            (1 << 30)
#define REG_LISTS       (1 << 31)

#define emc_dbg(emc, flags, ...) dev_dbg(emc->dev, __VA_ARGS__)

#define DVFS_CLOCK_CHANGE_VERSION	21021
#define EMC_PRELOCK_VERSION		2101

enum {
	DVFS_SEQUENCE = 1,
	WRITE_TRAINING_SEQUENCE = 2,
	PERIODIC_TRAINING_SEQUENCE = 3,
	DVFS_PT1 = 10,
	DVFS_UPDATE = 11,
	TRAINING_PT1 = 12,
	TRAINING_UPDATE = 13,
	PERIODIC_TRAINING_UPDATE = 14
};

/*
 * PTFV defines - basically just indexes into the per table PTFV array.
 */
#define PTFV_DQSOSC_MOVAVG_C0D0U0_INDEX		0
#define PTFV_DQSOSC_MOVAVG_C0D0U1_INDEX		1
#define PTFV_DQSOSC_MOVAVG_C0D1U0_INDEX		2
#define PTFV_DQSOSC_MOVAVG_C0D1U1_INDEX		3
#define PTFV_DQSOSC_MOVAVG_C1D0U0_INDEX		4
#define PTFV_DQSOSC_MOVAVG_C1D0U1_INDEX		5
#define PTFV_DQSOSC_MOVAVG_C1D1U0_INDEX		6
#define PTFV_DQSOSC_MOVAVG_C1D1U1_INDEX		7
#define PTFV_DVFS_SAMPLES_INDEX			9
#define PTFV_MOVAVG_WEIGHT_INDEX		10
#define PTFV_CONFIG_CTRL_INDEX			11

#define PTFV_CONFIG_CTRL_USE_PREVIOUS_EMA	(1 << 0)

/*
 * Do arithmetic in fixed point.
 */
#define MOVAVG_PRECISION_FACTOR		100

/*
 * The division portion of the average operation.
 */
#define __AVERAGE_PTFV(dev)						\
	({ next->ptfv_list[PTFV_DQSOSC_MOVAVG_ ## dev ## _INDEX] =	\
	   next->ptfv_list[PTFV_DQSOSC_MOVAVG_ ## dev ## _INDEX] /	\
	   next->ptfv_list[PTFV_DVFS_SAMPLES_INDEX]; })

/*
 * Convert val to fixed point and add it to the temporary average.
 */
#define __INCREMENT_PTFV(dev, val)					\
	({ next->ptfv_list[PTFV_DQSOSC_MOVAVG_ ## dev ## _INDEX] +=	\
	   ((val) * MOVAVG_PRECISION_FACTOR); })

/*
 * Convert a moving average back to integral form and return the value.
 */
#define __MOVAVG_AC(timing, dev)					\
	((timing)->ptfv_list[PTFV_DQSOSC_MOVAVG_ ## dev ## _INDEX] /	\
	 MOVAVG_PRECISION_FACTOR)

/* Weighted update. */
#define __WEIGHTED_UPDATE_PTFV(dev, nval)				\
	do {								\
		int w = PTFV_MOVAVG_WEIGHT_INDEX;			\
		int dqs = PTFV_DQSOSC_MOVAVG_ ## dev ## _INDEX;		\
									\
		next->ptfv_list[dqs] =					\
			((nval * MOVAVG_PRECISION_FACTOR) +		\
			 (next->ptfv_list[dqs] *			\
			  next->ptfv_list[w])) /			\
			(next->ptfv_list[w] + 1);			\
									\
		emc_dbg(emc, EMA_UPDATES, "%s: (s=%lu) EMA: %u\n",	\
			__stringify(dev), nval, next->ptfv_list[dqs]);	\
	} while (0)

/* Access a particular average. */
#define __MOVAVG(timing, dev)                      \
	((timing)->ptfv_list[PTFV_DQSOSC_MOVAVG_ ## dev ## _INDEX])

static u32 update_clock_tree_delay(struct tegra210_emc *emc, int type)
{
	bool periodic_training_update = type == PERIODIC_TRAINING_UPDATE;
	struct tegra210_emc_timing *last = emc->last;
	struct tegra210_emc_timing *next = emc->next;
	u32 last_timing_rate_mhz = last->rate / 1000;
	u32 next_timing_rate_mhz = next->rate / 1000;
	bool dvfs_update = type == DVFS_UPDATE;
	s32 tdel = 0, tmdel = 0, adel = 0;
	bool dvfs_pt1 = type == DVFS_PT1;
	unsigned long cval = 0;
	u32 temp[2][2], value;
	unsigned int i;

	/*
	 * Dev0 MSB.
	 */
	if (dvfs_pt1 || periodic_training_update) {
		value = tegra210_emc_mrr_read(emc, 2, 19);

		for (i = 0; i < emc->num_channels; i++) {
			temp[i][0] = (value & 0x00ff) << 8;
			temp[i][1] = (value & 0xff00) << 0;
			value >>= 16;
		}

		/*
		 * Dev0 LSB.
		 */
		value = tegra210_emc_mrr_read(emc, 2, 18);

		for (i = 0; i < emc->num_channels; i++) {
			temp[i][0] |= (value & 0x00ff) >> 0;
			temp[i][1] |= (value & 0xff00) >> 8;
			value >>= 16;
		}
	}

	if (dvfs_pt1 || periodic_training_update) {
		cval = tegra210_emc_actual_osc_clocks(last->run_clocks);
		cval *= 1000000;
		cval /= last_timing_rate_mhz * 2 * temp[0][0];
	}

	if (dvfs_pt1)
		__INCREMENT_PTFV(C0D0U0, cval);
	else if (dvfs_update)
		__AVERAGE_PTFV(C0D0U0);
	else if (periodic_training_update)
		__WEIGHTED_UPDATE_PTFV(C0D0U0, cval);

	if (dvfs_update || periodic_training_update) {
		tdel = next->current_dram_clktree[C0D0U0] -
				__MOVAVG_AC(next, C0D0U0);
		tmdel = (tdel < 0) ? -1 * tdel : tdel;
		adel = tmdel;

		if (tmdel * 128 * next_timing_rate_mhz / 1000000 >
		    next->tree_margin)
			next->current_dram_clktree[C0D0U0] =
				__MOVAVG_AC(next, C0D0U0);
	}

	if (dvfs_pt1 || periodic_training_update) {
		cval = tegra210_emc_actual_osc_clocks(last->run_clocks);
		cval *= 1000000;
		cval /= last_timing_rate_mhz * 2 * temp[0][1];
	}

	if (dvfs_pt1)
		__INCREMENT_PTFV(C0D0U1, cval);
	else if (dvfs_update)
		__AVERAGE_PTFV(C0D0U1);
	else if (periodic_training_update)
		__WEIGHTED_UPDATE_PTFV(C0D0U1, cval);

	if (dvfs_update || periodic_training_update) {
		tdel = next->current_dram_clktree[C0D0U1] -
				__MOVAVG_AC(next, C0D0U1);
		tmdel = (tdel < 0) ? -1 * tdel : tdel;

		if (tmdel > adel)
			adel = tmdel;

		if (tmdel * 128 * next_timing_rate_mhz / 1000000 >
		    next->tree_margin)
			next->current_dram_clktree[C0D0U1] =
				__MOVAVG_AC(next, C0D0U1);
	}

	if (emc->num_channels > 1) {
		if (dvfs_pt1 || periodic_training_update) {
			cval = tegra210_emc_actual_osc_clocks(last->run_clocks);
			cval *= 1000000;
			cval /= last_timing_rate_mhz * 2 * temp[1][0];
		}

		if (dvfs_pt1)
			__INCREMENT_PTFV(C1D0U0, cval);
		else if (dvfs_update)
			__AVERAGE_PTFV(C1D0U0);
		else if (periodic_training_update)
			__WEIGHTED_UPDATE_PTFV(C1D0U0, cval);

		if (dvfs_update || periodic_training_update) {
			tdel = next->current_dram_clktree[C1D0U0] -
					__MOVAVG_AC(next, C1D0U0);
			tmdel = (tdel < 0) ? -1 * tdel : tdel;

			if (tmdel > adel)
				adel = tmdel;

			if (tmdel * 128 * next_timing_rate_mhz / 1000000 >
			    next->tree_margin)
				next->current_dram_clktree[C1D0U0] =
					__MOVAVG_AC(next, C1D0U0);
		}

		if (dvfs_pt1 || periodic_training_update) {
			cval = tegra210_emc_actual_osc_clocks(last->run_clocks);
			cval *= 1000000;
			cval /= last_timing_rate_mhz * 2 * temp[1][1];
		}

		if (dvfs_pt1)
			__INCREMENT_PTFV(C1D0U1, cval);
		else if (dvfs_update)
			__AVERAGE_PTFV(C1D0U1);
		else if (periodic_training_update)
			__WEIGHTED_UPDATE_PTFV(C1D0U1, cval);

		if (dvfs_update || periodic_training_update) {
			tdel = next->current_dram_clktree[C1D0U1] -
					__MOVAVG_AC(next, C1D0U1);
			tmdel = (tdel < 0) ? -1 * tdel : tdel;

			if (tmdel > adel)
				adel = tmdel;

			if (tmdel * 128 * next_timing_rate_mhz / 1000000 >
			    next->tree_margin)
				next->current_dram_clktree[C1D0U1] =
					__MOVAVG_AC(next, C1D0U1);
		}
	}

	if (emc->num_devices < 2)
		goto done;

	/*
	 * Dev1 MSB.
	 */
	if (dvfs_pt1 || periodic_training_update) {
		value = tegra210_emc_mrr_read(emc, 1, 19);

		for (i = 0; i < emc->num_channels; i++) {
			temp[i][0] = (value & 0x00ff) << 8;
			temp[i][1] = (value & 0xff00) << 0;
			value >>= 16;
		}

		/*
		 * Dev1 LSB.
		 */
		value = tegra210_emc_mrr_read(emc, 2, 18);

		for (i = 0; i < emc->num_channels; i++) {
			temp[i][0] |= (value & 0x00ff) >> 0;
			temp[i][1] |= (value & 0xff00) >> 8;
			value >>= 16;
		}
	}

	if (dvfs_pt1 || periodic_training_update) {
		cval = tegra210_emc_actual_osc_clocks(last->run_clocks);
		cval *= 1000000;
		cval /= last_timing_rate_mhz * 2 * temp[0][0];
	}

	if (dvfs_pt1)
		__INCREMENT_PTFV(C0D1U0, cval);
	else if (dvfs_update)
		__AVERAGE_PTFV(C0D1U0);
	else if (periodic_training_update)
		__WEIGHTED_UPDATE_PTFV(C0D1U0, cval);

	if (dvfs_update || periodic_training_update) {
		tdel = next->current_dram_clktree[C0D1U0] -
				__MOVAVG_AC(next, C0D1U0);
		tmdel = (tdel < 0) ? -1 * tdel : tdel;

		if (tmdel > adel)
			adel = tmdel;

		if (tmdel * 128 * next_timing_rate_mhz / 1000000 >
		    next->tree_margin)
			next->current_dram_clktree[C0D1U0] =
				__MOVAVG_AC(next, C0D1U0);
	}

	if (dvfs_pt1 || periodic_training_update) {
		cval = tegra210_emc_actual_osc_clocks(last->run_clocks);
		cval *= 1000000;
		cval /= last_timing_rate_mhz * 2 * temp[0][1];
	}

	if (dvfs_pt1)
		__INCREMENT_PTFV(C0D1U1, cval);
	else if (dvfs_update)
		__AVERAGE_PTFV(C0D1U1);
	else if (periodic_training_update)
		__WEIGHTED_UPDATE_PTFV(C0D1U1, cval);

	if (dvfs_update || periodic_training_update) {
		tdel = next->current_dram_clktree[C0D1U1] -
				__MOVAVG_AC(next, C0D1U1);
		tmdel = (tdel < 0) ? -1 * tdel : tdel;

		if (tmdel > adel)
			adel = tmdel;

		if (tmdel * 128 * next_timing_rate_mhz / 1000000 >
		    next->tree_margin)
			next->current_dram_clktree[C0D1U1] =
				__MOVAVG_AC(next, C0D1U1);
	}

	if (emc->num_channels > 1) {
		if (dvfs_pt1 || periodic_training_update) {
			cval = tegra210_emc_actual_osc_clocks(last->run_clocks);
			cval *= 1000000;
			cval /= last_timing_rate_mhz * 2 * temp[1][0];
		}

		if (dvfs_pt1)
			__INCREMENT_PTFV(C1D1U0, cval);
		else if (dvfs_update)
			__AVERAGE_PTFV(C1D1U0);
		else if (periodic_training_update)
			__WEIGHTED_UPDATE_PTFV(C1D1U0, cval);

		if (dvfs_update || periodic_training_update) {
			tdel = next->current_dram_clktree[C1D1U0] -
					__MOVAVG_AC(next, C1D1U0);
			tmdel = (tdel < 0) ? -1 * tdel : tdel;

			if (tmdel > adel)
				adel = tmdel;

			if (tmdel * 128 * next_timing_rate_mhz / 1000000 >
			    next->tree_margin)
				next->current_dram_clktree[C1D1U0] =
					__MOVAVG_AC(next, C1D1U0);
		}

		if (dvfs_pt1 || periodic_training_update) {
			cval = tegra210_emc_actual_osc_clocks(last->run_clocks);
			cval *= 1000000;
			cval /= last_timing_rate_mhz * 2 * temp[1][1];
		}

		if (dvfs_pt1)
			__INCREMENT_PTFV(C1D1U1, cval);
		else if (dvfs_update)
			__AVERAGE_PTFV(C1D1U1);
		else if (periodic_training_update)
			__WEIGHTED_UPDATE_PTFV(C1D1U1, cval);

		if (dvfs_update || periodic_training_update) {
			tdel = next->current_dram_clktree[C1D1U1] -
					__MOVAVG_AC(next, C1D1U1);
			tmdel = (tdel < 0) ? -1 * tdel : tdel;

			if (tmdel > adel)
				adel = tmdel;

			if (tmdel * 128 * next_timing_rate_mhz / 1000000 >
			    next->tree_margin)
				next->current_dram_clktree[C1D1U1] =
					__MOVAVG_AC(next, C1D1U1);
		}
	}

done:
	return adel;
}

static u32 periodic_compensation_handler(struct tegra210_emc *emc, u32 type,
					 struct tegra210_emc_timing *last,
					 struct tegra210_emc_timing *next)
{
#define __COPY_EMA(nt, lt, dev)						\
	({ __MOVAVG(nt, dev) = __MOVAVG(lt, dev) *			\
	   (nt)->ptfv_list[PTFV_DVFS_SAMPLES_INDEX]; })

	u32 i, adel = 0, samples = next->ptfv_list[PTFV_DVFS_SAMPLES_INDEX];
	u32 delay;

	delay = tegra210_emc_actual_osc_clocks(last->run_clocks);
	delay *= 1000;
	delay = 2 + (delay / last->rate);

	if (!next->periodic_training)
		return 0;

	if (type == DVFS_SEQUENCE) {
		if (last->periodic_training &&
		    (next->ptfv_list[PTFV_CONFIG_CTRL_INDEX] &
		     PTFV_CONFIG_CTRL_USE_PREVIOUS_EMA)) {
			/*
			 * If the previous frequency was using periodic
			 * calibration then we can reuse the previous
			 * frequencies EMA data.
			 */
			__COPY_EMA(next, last, C0D0U0);
			__COPY_EMA(next, last, C0D0U1);
			__COPY_EMA(next, last, C1D0U0);
			__COPY_EMA(next, last, C1D0U1);
			__COPY_EMA(next, last, C0D1U0);
			__COPY_EMA(next, last, C0D1U1);
			__COPY_EMA(next, last, C1D1U0);
			__COPY_EMA(next, last, C1D1U1);
		} else {
			/* Reset the EMA.*/
			__MOVAVG(next, C0D0U0) = 0;
			__MOVAVG(next, C0D0U1) = 0;
			__MOVAVG(next, C1D0U0) = 0;
			__MOVAVG(next, C1D0U1) = 0;
			__MOVAVG(next, C0D1U0) = 0;
			__MOVAVG(next, C0D1U1) = 0;
			__MOVAVG(next, C1D1U0) = 0;
			__MOVAVG(next, C1D1U1) = 0;

			for (i = 0; i < samples; i++) {
				tegra210_emc_start_periodic_compensation(emc);
				udelay(delay);

				/*
				 * Generate next sample of data.
				 */
				adel = update_clock_tree_delay(emc, DVFS_PT1);
			}
		}

		/*
		 * Seems like it should be part of the
		 * 'if (last_timing->periodic_training)' conditional
		 * since is already done for the else clause.
		 */
		adel = update_clock_tree_delay(emc, DVFS_UPDATE);
	}

	if (type == PERIODIC_TRAINING_SEQUENCE) {
		tegra210_emc_start_periodic_compensation(emc);
		udelay(delay);

		adel = update_clock_tree_delay(emc, PERIODIC_TRAINING_UPDATE);
	}

	return adel;
}

static u32 tegra210_emc_r21021_periodic_compensation(struct tegra210_emc *emc)
{
	u32 emc_cfg, emc_cfg_o, emc_cfg_update, del, value;
	u32 list[] = {
		EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_0,
		EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_1,
		EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_2,
		EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_3,
		EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_0,
		EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_1,
		EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_2,
		EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_3,
		EMC_DATA_BRLSHFT_0,
		EMC_DATA_BRLSHFT_1
	};
	struct tegra210_emc_timing *last = emc->last;
	unsigned int items = ARRAY_SIZE(list), i;
	unsigned long delay;

	if (last->periodic_training) {
		emc_dbg(emc, PER_TRAIN, "Periodic training starting\n");

		value = emc_readl(emc, EMC_DBG);
		emc_cfg_o = emc_readl(emc, EMC_CFG);
		emc_cfg = emc_cfg_o & ~(EMC_CFG_DYN_SELF_REF |
					EMC_CFG_DRAM_ACPD |
					EMC_CFG_DRAM_CLKSTOP_PD);


		/*
		 * 1. Power optimizations should be off.
		 */
		emc_writel(emc, emc_cfg, EMC_CFG);

		/* Does emc_timing_update() for above changes. */
		tegra210_emc_dll_disable(emc);

		for (i = 0; i < emc->num_channels; i++)
			tegra210_emc_wait_for_update(emc, i, EMC_EMC_STATUS,
						     EMC_EMC_STATUS_DRAM_IN_POWERDOWN_MASK,
						     0);

		for (i = 0; i < emc->num_channels; i++)
			tegra210_emc_wait_for_update(emc, i, EMC_EMC_STATUS,
						     EMC_EMC_STATUS_DRAM_IN_SELF_REFRESH_MASK,
						     0);

		emc_cfg_update = value = emc_readl(emc, EMC_CFG_UPDATE);
		value &= ~EMC_CFG_UPDATE_UPDATE_DLL_IN_UPDATE_MASK;
		value |= (2 << EMC_CFG_UPDATE_UPDATE_DLL_IN_UPDATE_SHIFT);
		emc_writel(emc, value, EMC_CFG_UPDATE);

		/*
		 * 2. osc kick off - this assumes training and dvfs have set
		 *    correct MR23.
		 */
		tegra210_emc_start_periodic_compensation(emc);

		/*
		 * 3. Let dram capture its clock tree delays.
		 */
		delay = tegra210_emc_actual_osc_clocks(last->run_clocks);
		delay *= 1000;
		delay /= last->rate + 1;
		udelay(delay);

		/*
		 * 4. Check delta wrt previous values (save value if margin
		 *    exceeds what is set in table).
		 */
		del = periodic_compensation_handler(emc,
						    PERIODIC_TRAINING_SEQUENCE,
						    last, last);

		/*
		 * 5. Apply compensation w.r.t. trained values (if clock tree
		 *    has drifted more than the set margin).
		 */
		if (last->tree_margin < ((del * 128 * (last->rate / 1000)) / 1000000)) {
			for (i = 0; i < items; i++) {
				value = tegra210_emc_compensate(last, list[i]);
				emc_dbg(emc, EMA_WRITES, "0x%08x <= 0x%08x\n",
					list[i], value);
				emc_writel(emc, value, list[i]);
			}
		}

		emc_writel(emc, emc_cfg_o, EMC_CFG);

		/*
		 * 6. Timing update actally applies the new trimmers.
		 */
		tegra210_emc_timing_update(emc);

		/* 6.1. Restore the UPDATE_DLL_IN_UPDATE field. */
		emc_writel(emc, emc_cfg_update, EMC_CFG_UPDATE);

		/* 6.2. Restore the DLL. */
		tegra210_emc_dll_enable(emc);
	}

	return 0;
}

/*
 * Do the clock change sequence.
 */
static void tegra210_emc_r21021_set_clock(struct tegra210_emc *emc, u32 clksrc)
{
	/* state variables */
	static bool fsp_for_next_freq;
	/* constant configuration parameters */
	const bool save_restore_clkstop_pd = true;
	const u32 zqcal_before_cc_cutoff = 2400;
	const bool cya_allow_ref_cc = false;
	const bool cya_issue_pc_ref = false;
	const bool opt_cc_short_zcal = true;
	const bool ref_b4_sref_en = false;
	const u32 tZQCAL_lpddr4 = 1000000;
	const bool opt_short_zcal = true;
	const bool opt_do_sw_qrst = true;
	const u32 opt_dvfs_mode = MAN_SR;
	/*
	 * This is the timing table for the source frequency. It does _not_
	 * necessarily correspond to the actual timing values in the EMC at the
	 * moment. If the boot BCT differs from the table then this can happen.
	 * However, we need it for accessing the dram_timings (which are not
	 * really registers) array for the current frequency.
	 */
	struct tegra210_emc_timing *fake, *last = emc->last, *next = emc->next;
	u32 tRTM, RP_war, R2P_war, TRPab_war, deltaTWATM, W2P_war, tRPST;
	u32 mr13_flip_fspwr, mr13_flip_fspop, ramp_up_wait, ramp_down_wait;
	u32 zq_wait_long, zq_latch_dvfs_wait_time, tZQCAL_lpddr4_fc_adj;
	u32 emc_auto_cal_config, auto_cal_en, emc_cfg, emc_sel_dpd_ctrl;
	u32 tFC_lpddr4 = 1000 * next->dram_timings[T_FC_LPDDR4];
	u32 bg_reg_mode_change, enable_bglp_reg, enable_bg_reg;
	bool opt_zcal_en_cc = false, is_lpddr3 = false;
	bool compensate_trimmer_applicable = false;
	u32 emc_dbg, emc_cfg_pipe_clk, emc_pin;
	u32 src_clk_period, dst_clk_period; /* in picoseconds */
	bool shared_zq_resistor = false;
	u32 value, dram_type;
	u32 opt_dll_mode = 0;
	unsigned long delay;
	unsigned int i;

	emc_dbg(emc, INFO, "Running clock change.\n");

	/* XXX fake == last */
	fake = tegra210_emc_find_timing(emc, last->rate * 1000UL);
	fsp_for_next_freq = !fsp_for_next_freq;

	value = emc_readl(emc, EMC_FBIO_CFG5) & EMC_FBIO_CFG5_DRAM_TYPE_MASK;
	dram_type = value >> EMC_FBIO_CFG5_DRAM_TYPE_SHIFT;

	if (last->burst_regs[EMC_ZCAL_WAIT_CNT_INDEX] & BIT(31))
		shared_zq_resistor = true;

	if ((next->burst_regs[EMC_ZCAL_INTERVAL_INDEX] != 0 &&
	     last->burst_regs[EMC_ZCAL_INTERVAL_INDEX] == 0) ||
	    dram_type == DRAM_TYPE_LPDDR4)
		opt_zcal_en_cc = true;

	if (dram_type == DRAM_TYPE_DDR3)
		opt_dll_mode = tegra210_emc_get_dll_state(next);

	if ((next->burst_regs[EMC_FBIO_CFG5_INDEX] & BIT(25)) &&
	    (dram_type == DRAM_TYPE_LPDDR2))
		is_lpddr3 = true;

	emc_readl(emc, EMC_CFG);
	emc_readl(emc, EMC_AUTO_CAL_CONFIG);

	src_clk_period = 1000000000 / last->rate;
	dst_clk_period = 1000000000 / next->rate;

	if (dst_clk_period <= zqcal_before_cc_cutoff)
		tZQCAL_lpddr4_fc_adj = tZQCAL_lpddr4 - tFC_lpddr4;
	else
		tZQCAL_lpddr4_fc_adj = tZQCAL_lpddr4;

	tZQCAL_lpddr4_fc_adj /= dst_clk_period;

	emc_dbg = emc_readl(emc, EMC_DBG);
	emc_pin = emc_readl(emc, EMC_PIN);
	emc_cfg_pipe_clk = emc_readl(emc, EMC_CFG_PIPE_CLK);

	emc_cfg = next->burst_regs[EMC_CFG_INDEX];
	emc_cfg &= ~(EMC_CFG_DYN_SELF_REF | EMC_CFG_DRAM_ACPD |
		     EMC_CFG_DRAM_CLKSTOP_SR | EMC_CFG_DRAM_CLKSTOP_PD);
	emc_sel_dpd_ctrl = next->emc_sel_dpd_ctrl;
	emc_sel_dpd_ctrl &= ~(EMC_SEL_DPD_CTRL_CLK_SEL_DPD_EN |
			      EMC_SEL_DPD_CTRL_CA_SEL_DPD_EN |
			      EMC_SEL_DPD_CTRL_RESET_SEL_DPD_EN |
			      EMC_SEL_DPD_CTRL_ODT_SEL_DPD_EN |
			      EMC_SEL_DPD_CTRL_DATA_SEL_DPD_EN);

	emc_dbg(emc, INFO, "Clock change version: %d\n",
		DVFS_CLOCK_CHANGE_VERSION);
	emc_dbg(emc, INFO, "DRAM type = %d\n", dram_type);
	emc_dbg(emc, INFO, "DRAM dev #: %u\n", emc->num_devices);
	emc_dbg(emc, INFO, "Next EMC clksrc: 0x%08x\n", clksrc);
	emc_dbg(emc, INFO, "DLL clksrc:      0x%08x\n", next->dll_clk_src);
	emc_dbg(emc, INFO, "last rate: %u, next rate %u\n", last->rate,
		next->rate);
	emc_dbg(emc, INFO, "last period: %u, next period: %u\n",
		src_clk_period, dst_clk_period);
	emc_dbg(emc, INFO, "  shared_zq_resistor: %d\n", !!shared_zq_resistor);
	emc_dbg(emc, INFO, "  num_channels: %u\n", emc->num_channels);
	emc_dbg(emc, INFO, "  opt_dll_mode: %d\n", opt_dll_mode);

	/*
	 * Step 1:
	 *   Pre DVFS SW sequence.
	 */
	emc_dbg(emc, STEPS, "Step 1\n");
	emc_dbg(emc, STEPS, "Step 1.1: Disable DLL temporarily.\n");

	value = emc_readl(emc, EMC_CFG_DIG_DLL);
	value &= ~EMC_CFG_DIG_DLL_CFG_DLL_EN;
	emc_writel(emc, value, EMC_CFG_DIG_DLL);

	tegra210_emc_timing_update(emc);

	for (i = 0; i < emc->num_channels; i++)
		tegra210_emc_wait_for_update(emc, i, EMC_CFG_DIG_DLL,
					     EMC_CFG_DIG_DLL_CFG_DLL_EN, 0);

	emc_dbg(emc, STEPS, "Step 1.2: Disable AUTOCAL temporarily.\n");

	emc_auto_cal_config = next->emc_auto_cal_config;
	auto_cal_en = emc_auto_cal_config & EMC_AUTO_CAL_CONFIG_AUTO_CAL_ENABLE;
	emc_auto_cal_config &= ~EMC_AUTO_CAL_CONFIG_AUTO_CAL_START;
	emc_auto_cal_config |= EMC_AUTO_CAL_CONFIG_AUTO_CAL_MEASURE_STALL;
	emc_auto_cal_config |= EMC_AUTO_CAL_CONFIG_AUTO_CAL_UPDATE_STALL;
	emc_auto_cal_config |= auto_cal_en;
	emc_writel(emc, emc_auto_cal_config, EMC_AUTO_CAL_CONFIG);
	emc_readl(emc, EMC_AUTO_CAL_CONFIG); /* Flush write. */

	emc_dbg(emc, STEPS, "Step 1.3: Disable other power features.\n");

	tegra210_emc_set_shadow_bypass(emc, ACTIVE);
	emc_writel(emc, emc_cfg, EMC_CFG);
	emc_writel(emc, emc_sel_dpd_ctrl, EMC_SEL_DPD_CTRL);
	tegra210_emc_set_shadow_bypass(emc, ASSEMBLY);

	if (next->periodic_training) {
		tegra210_emc_reset_dram_clktree_values(next);

		for (i = 0; i < emc->num_channels; i++)
			tegra210_emc_wait_for_update(emc, i, EMC_EMC_STATUS,
						     EMC_EMC_STATUS_DRAM_IN_POWERDOWN_MASK,
						     0);

		for (i = 0; i < emc->num_channels; i++)
			tegra210_emc_wait_for_update(emc, i, EMC_EMC_STATUS,
						     EMC_EMC_STATUS_DRAM_IN_SELF_REFRESH_MASK,
						     0);

		tegra210_emc_start_periodic_compensation(emc);

		delay = 1000 * tegra210_emc_actual_osc_clocks(last->run_clocks);
		udelay((delay / last->rate) + 2);

		value = periodic_compensation_handler(emc, DVFS_SEQUENCE, fake,
						      next);
		value = (value * 128 * next->rate / 1000) / 1000000;

		if (next->periodic_training && value > next->tree_margin)
			compensate_trimmer_applicable = true;
	}

	emc_writel(emc, EMC_INTSTATUS_CLKCHANGE_COMPLETE, EMC_INTSTATUS);
	tegra210_emc_set_shadow_bypass(emc, ACTIVE);
	emc_writel(emc, emc_cfg, EMC_CFG);
	emc_writel(emc, emc_sel_dpd_ctrl, EMC_SEL_DPD_CTRL);
	emc_writel(emc, emc_cfg_pipe_clk | EMC_CFG_PIPE_CLK_CLK_ALWAYS_ON,
		   EMC_CFG_PIPE_CLK);
	emc_writel(emc, next->emc_fdpd_ctrl_cmd_no_ramp &
			~EMC_FDPD_CTRL_CMD_NO_RAMP_CMD_DPD_NO_RAMP_ENABLE,
		   EMC_FDPD_CTRL_CMD_NO_RAMP);

	bg_reg_mode_change =
		((next->burst_regs[EMC_PMACRO_BG_BIAS_CTRL_0_INDEX] &
		  EMC_PMACRO_BG_BIAS_CTRL_0_BGLP_E_PWRD) ^
		 (last->burst_regs[EMC_PMACRO_BG_BIAS_CTRL_0_INDEX] &
		  EMC_PMACRO_BG_BIAS_CTRL_0_BGLP_E_PWRD)) ||
		((next->burst_regs[EMC_PMACRO_BG_BIAS_CTRL_0_INDEX] &
		  EMC_PMACRO_BG_BIAS_CTRL_0_BG_E_PWRD) ^
		 (last->burst_regs[EMC_PMACRO_BG_BIAS_CTRL_0_INDEX] &
		  EMC_PMACRO_BG_BIAS_CTRL_0_BG_E_PWRD));
	enable_bglp_reg =
		(next->burst_regs[EMC_PMACRO_BG_BIAS_CTRL_0_INDEX] &
		 EMC_PMACRO_BG_BIAS_CTRL_0_BGLP_E_PWRD) == 0;
	enable_bg_reg =
		(next->burst_regs[EMC_PMACRO_BG_BIAS_CTRL_0_INDEX] &
		 EMC_PMACRO_BG_BIAS_CTRL_0_BG_E_PWRD) == 0;

	if (bg_reg_mode_change) {
		if (enable_bg_reg)
			emc_writel(emc, last->burst_regs
				   [EMC_PMACRO_BG_BIAS_CTRL_0_INDEX] &
				   ~EMC_PMACRO_BG_BIAS_CTRL_0_BG_E_PWRD,
				   EMC_PMACRO_BG_BIAS_CTRL_0);

		if (enable_bglp_reg)
			emc_writel(emc, last->burst_regs
				   [EMC_PMACRO_BG_BIAS_CTRL_0_INDEX] &
				   ~EMC_PMACRO_BG_BIAS_CTRL_0_BGLP_E_PWRD,
				   EMC_PMACRO_BG_BIAS_CTRL_0);
	}

	/* Check if we need to turn on VREF generator. */
	if ((((last->burst_regs[EMC_PMACRO_DATA_PAD_TX_CTRL_INDEX] &
	       EMC_PMACRO_DATA_PAD_TX_CTRL_DATA_DQ_E_IVREF) == 0) &&
	     ((next->burst_regs[EMC_PMACRO_DATA_PAD_TX_CTRL_INDEX] &
	       EMC_PMACRO_DATA_PAD_TX_CTRL_DATA_DQ_E_IVREF) == 1)) ||
	    (((last->burst_regs[EMC_PMACRO_DATA_PAD_TX_CTRL_INDEX] &
	       EMC_PMACRO_DATA_PAD_TX_CTRL_DATA_DQS_E_IVREF) == 0) &&
	     ((next->burst_regs[EMC_PMACRO_DATA_PAD_TX_CTRL_INDEX] &
	       EMC_PMACRO_DATA_PAD_TX_CTRL_DATA_DQS_E_IVREF) != 0))) {
		u32 pad_tx_ctrl =
		    next->burst_regs[EMC_PMACRO_DATA_PAD_TX_CTRL_INDEX];
		u32 last_pad_tx_ctrl =
		    last->burst_regs[EMC_PMACRO_DATA_PAD_TX_CTRL_INDEX];
		u32 next_dq_e_ivref, next_dqs_e_ivref;

		next_dqs_e_ivref = pad_tx_ctrl &
				   EMC_PMACRO_DATA_PAD_TX_CTRL_DATA_DQS_E_IVREF;
		next_dq_e_ivref = pad_tx_ctrl &
				  EMC_PMACRO_DATA_PAD_TX_CTRL_DATA_DQ_E_IVREF;
		value = (last_pad_tx_ctrl &
				~EMC_PMACRO_DATA_PAD_TX_CTRL_DATA_DQ_E_IVREF &
				~EMC_PMACRO_DATA_PAD_TX_CTRL_DATA_DQS_E_IVREF) |
			next_dq_e_ivref | next_dqs_e_ivref;
		emc_writel(emc, value, EMC_PMACRO_DATA_PAD_TX_CTRL);
		udelay(1);
	} else if (bg_reg_mode_change) {
		udelay(1);
	}

	tegra210_emc_set_shadow_bypass(emc, ASSEMBLY);

	/*
	 * Step 2:
	 *   Prelock the DLL.
	 */
	emc_dbg(emc, STEPS, "Step 2\n");

	if (next->burst_regs[EMC_CFG_DIG_DLL_INDEX] &
	    EMC_CFG_DIG_DLL_CFG_DLL_EN) {
		emc_dbg(emc, INFO, "Prelock enabled for target frequency.\n");
		value = tegra210_emc_dll_prelock(emc, clksrc);
		emc_dbg(emc, INFO, "DLL out: 0x%03x\n", value);
	} else {
		emc_dbg(emc, INFO, "Disabling DLL for target frequency.\n");
		tegra210_emc_dll_disable(emc);
	}

	/*
	 * Step 3:
	 *   Prepare autocal for the clock change.
	 */
	emc_dbg(emc, STEPS, "Step 3\n");

	tegra210_emc_set_shadow_bypass(emc, ACTIVE);
	emc_writel(emc, next->emc_auto_cal_config2, EMC_AUTO_CAL_CONFIG2);
	emc_writel(emc, next->emc_auto_cal_config3, EMC_AUTO_CAL_CONFIG3);
	emc_writel(emc, next->emc_auto_cal_config4, EMC_AUTO_CAL_CONFIG4);
	emc_writel(emc, next->emc_auto_cal_config5, EMC_AUTO_CAL_CONFIG5);
	emc_writel(emc, next->emc_auto_cal_config6, EMC_AUTO_CAL_CONFIG6);
	emc_writel(emc, next->emc_auto_cal_config7, EMC_AUTO_CAL_CONFIG7);
	emc_writel(emc, next->emc_auto_cal_config8, EMC_AUTO_CAL_CONFIG8);
	tegra210_emc_set_shadow_bypass(emc, ASSEMBLY);

	emc_auto_cal_config |= (EMC_AUTO_CAL_CONFIG_AUTO_CAL_COMPUTE_START |
				auto_cal_en);
	emc_writel(emc, emc_auto_cal_config, EMC_AUTO_CAL_CONFIG);

	/*
	 * Step 4:
	 *   Update EMC_CFG. (??)
	 */
	emc_dbg(emc, STEPS, "Step 4\n");

	if (src_clk_period > 50000 && dram_type == DRAM_TYPE_LPDDR4)
		ccfifo_writel(emc, 1, EMC_SELF_REF, 0);
	else
		emc_writel(emc, next->emc_cfg_2, EMC_CFG_2);

	/*
	 * Step 5:
	 *   Prepare reference variables for ZQCAL regs.
	 */
	emc_dbg(emc, STEPS, "Step 5\n");

	if (dram_type == DRAM_TYPE_LPDDR4)
		zq_wait_long = max((u32)1, div_o3(1000000, dst_clk_period));
	else if (dram_type == DRAM_TYPE_LPDDR2 || is_lpddr3)
		zq_wait_long = max(next->min_mrs_wait,
				   div_o3(360000, dst_clk_period)) + 4;
	else if (dram_type == DRAM_TYPE_DDR3)
		zq_wait_long = max((u32)256,
				   div_o3(320000, dst_clk_period) + 2);
	else
		zq_wait_long = 0;

	/*
	 * Step 6:
	 *   Training code - removed.
	 */
	emc_dbg(emc, STEPS, "Step 6\n");

	/*
	 * Step 7:
	 *   Program FSP reference registers and send MRWs to new FSPWR.
	 */
	emc_dbg(emc, STEPS, "Step 7\n");
	emc_dbg(emc, SUB_STEPS, "Step 7.1: Bug 200024907 - Patch RP R2P");

	/* WAR 200024907 */
	if (dram_type == DRAM_TYPE_LPDDR4) {
		u32 nRTP = 16;

		if (src_clk_period >= 1000000 / 1866) /* 535.91 ps */
			nRTP = 14;

		if (src_clk_period >= 1000000 / 1600) /* 625.00 ps */
			nRTP = 12;

		if (src_clk_period >= 1000000 / 1333) /* 750.19 ps */
			nRTP = 10;

		if (src_clk_period >= 1000000 / 1066) /* 938.09 ps */
			nRTP = 8;

		deltaTWATM = max_t(u32, div_o3(7500, src_clk_period), 8);

		/*
		 * Originally there was a + .5 in the tRPST calculation.
		 * However since we can't do FP in the kernel and the tRTM
		 * computation was in a floating point ceiling function, adding
		 * one to tRTP should be ok. There is no other source of non
		 * integer values, so the result was always going to be
		 * something for the form: f_ceil(N + .5) = N + 1;
		 */
		tRPST = (last->emc_mrw & 0x80) >> 7;
		tRTM = fake->dram_timings[RL] + div_o3(3600, src_clk_period) +
			max_t(u32, div_o3(7500, src_clk_period), 8) + tRPST +
			1 + nRTP;

		emc_dbg(emc, INFO, "tRTM = %u, EMC_RP = %u\n", tRTM,
			next->burst_regs[EMC_RP_INDEX]);

		if (last->burst_regs[EMC_RP_INDEX] < tRTM) {
			if (tRTM > (last->burst_regs[EMC_R2P_INDEX] +
				    last->burst_regs[EMC_RP_INDEX])) {
				R2P_war = tRTM - last->burst_regs[EMC_RP_INDEX];
				RP_war = last->burst_regs[EMC_RP_INDEX];
				TRPab_war = last->burst_regs[EMC_TRPAB_INDEX];

				if (R2P_war > 63) {
					RP_war = R2P_war +
						 last->burst_regs[EMC_RP_INDEX] - 63;

					if (TRPab_war < RP_war)
						TRPab_war = RP_war;

					R2P_war = 63;
				}
			} else {
				R2P_war = last->burst_regs[EMC_R2P_INDEX];
				RP_war = last->burst_regs[EMC_RP_INDEX];
				TRPab_war = last->burst_regs[EMC_TRPAB_INDEX];
			}

			if (RP_war < deltaTWATM) {
				W2P_war = last->burst_regs[EMC_W2P_INDEX]
					  + deltaTWATM - RP_war;
				if (W2P_war > 63) {
					RP_war = RP_war + W2P_war - 63;
					if (TRPab_war < RP_war)
						TRPab_war = RP_war;
					W2P_war = 63;
				}
			} else {
				W2P_war = last->burst_regs[
					  EMC_W2P_INDEX];
			}

			if ((last->burst_regs[EMC_W2P_INDEX] ^ W2P_war) ||
			    (last->burst_regs[EMC_R2P_INDEX] ^ R2P_war) ||
			    (last->burst_regs[EMC_RP_INDEX] ^ RP_war) ||
			    (last->burst_regs[EMC_TRPAB_INDEX] ^ TRPab_war)) {
				emc_writel(emc, RP_war, EMC_RP);
				emc_writel(emc, R2P_war, EMC_R2P);
				emc_writel(emc, W2P_war, EMC_W2P);
				emc_writel(emc, TRPab_war, EMC_TRPAB);
			}

			tegra210_emc_timing_update(emc);
		} else {
			emc_dbg(emc, INFO, "Skipped WAR\n");
		}
	}

	if (!fsp_for_next_freq) {
		mr13_flip_fspwr = (next->emc_mrw3 & 0xffffff3f) | 0x80;
		mr13_flip_fspop = (next->emc_mrw3 & 0xffffff3f) | 0x00;
	} else {
		mr13_flip_fspwr = (next->emc_mrw3 & 0xffffff3f) | 0x40;
		mr13_flip_fspop = (next->emc_mrw3 & 0xffffff3f) | 0xc0;
	}

	if (dram_type == DRAM_TYPE_LPDDR4) {
		emc_writel(emc, mr13_flip_fspwr, EMC_MRW3);
		emc_writel(emc, next->emc_mrw, EMC_MRW);
		emc_writel(emc, next->emc_mrw2, EMC_MRW2);
	}

	/*
	 * Step 8:
	 *   Program the shadow registers.
	 */
	emc_dbg(emc, STEPS, "Step 8\n");
	emc_dbg(emc, SUB_STEPS, "Writing burst_regs\n");

	for (i = 0; i < next->num_burst; i++) {
		const u16 *offsets = emc->offsets->burst;
		u16 offset;

		if (!offsets[i])
			continue;

		value = next->burst_regs[i];
		offset = offsets[i];

		if (dram_type != DRAM_TYPE_LPDDR4 &&
		    (offset == EMC_MRW6 || offset == EMC_MRW7 ||
		     offset == EMC_MRW8 || offset == EMC_MRW9 ||
		     offset == EMC_MRW10 || offset == EMC_MRW11 ||
		     offset == EMC_MRW12 || offset == EMC_MRW13 ||
		     offset == EMC_MRW14 || offset == EMC_MRW15 ||
		     offset == EMC_TRAINING_CTRL))
			continue;

		/* Pain... And suffering. */
		if (offset == EMC_CFG) {
			value &= ~EMC_CFG_DRAM_ACPD;
			value &= ~EMC_CFG_DYN_SELF_REF;

			if (dram_type == DRAM_TYPE_LPDDR4) {
				value &= ~EMC_CFG_DRAM_CLKSTOP_SR;
				value &= ~EMC_CFG_DRAM_CLKSTOP_PD;
			}
		} else if (offset == EMC_MRS_WAIT_CNT &&
			   dram_type == DRAM_TYPE_LPDDR2 &&
			   opt_zcal_en_cc && !opt_cc_short_zcal &&
			   opt_short_zcal) {
			value = (value & ~(EMC_MRS_WAIT_CNT_SHORT_WAIT_MASK <<
					   EMC_MRS_WAIT_CNT_SHORT_WAIT_SHIFT)) |
				((zq_wait_long & EMC_MRS_WAIT_CNT_SHORT_WAIT_MASK) <<
						 EMC_MRS_WAIT_CNT_SHORT_WAIT_SHIFT);
		} else if (offset == EMC_ZCAL_WAIT_CNT &&
			   dram_type == DRAM_TYPE_DDR3 && opt_zcal_en_cc &&
			   !opt_cc_short_zcal && opt_short_zcal) {
			value = (value & ~(EMC_ZCAL_WAIT_CNT_ZCAL_WAIT_CNT_MASK <<
					   EMC_ZCAL_WAIT_CNT_ZCAL_WAIT_CNT_SHIFT)) |
				((zq_wait_long & EMC_ZCAL_WAIT_CNT_ZCAL_WAIT_CNT_MASK) <<
						 EMC_MRS_WAIT_CNT_SHORT_WAIT_SHIFT);
		} else if (offset == EMC_ZCAL_INTERVAL && opt_zcal_en_cc) {
			value = 0; /* EMC_ZCAL_INTERVAL reset value. */
		} else if (offset == EMC_PMACRO_AUTOCAL_CFG_COMMON) {
			value |= EMC_PMACRO_AUTOCAL_CFG_COMMON_E_CAL_BYPASS_DVFS;
		} else if (offset == EMC_PMACRO_DATA_PAD_TX_CTRL) {
			value &= ~(EMC_PMACRO_DATA_PAD_TX_CTRL_DATA_DQSP_TX_E_DCC |
				   EMC_PMACRO_DATA_PAD_TX_CTRL_DATA_DQSN_TX_E_DCC |
				   EMC_PMACRO_DATA_PAD_TX_CTRL_DATA_DQ_TX_E_DCC |
				   EMC_PMACRO_DATA_PAD_TX_CTRL_DATA_CMD_TX_E_DCC);
		} else if (offset == EMC_PMACRO_CMD_PAD_TX_CTRL) {
			value |= EMC_PMACRO_CMD_PAD_TX_CTRL_CMD_DQ_TX_DRVFORCEON;
			value &= ~(EMC_PMACRO_CMD_PAD_TX_CTRL_CMD_DQSP_TX_E_DCC |
				   EMC_PMACRO_CMD_PAD_TX_CTRL_CMD_DQSN_TX_E_DCC |
				   EMC_PMACRO_CMD_PAD_TX_CTRL_CMD_DQ_TX_E_DCC |
				   EMC_PMACRO_CMD_PAD_TX_CTRL_CMD_CMD_TX_E_DCC);
		} else if (offset == EMC_PMACRO_BRICK_CTRL_RFU1) {
			value &= 0xf800f800;
		} else if (offset == EMC_PMACRO_COMMON_PAD_TX_CTRL) {
			value &= 0xfffffff0;
		}

		emc_writel(emc, value, offset);
	}

	/* SW addition: do EMC refresh adjustment here. */
	tegra210_emc_adjust_timing(emc, next);

	if (dram_type == DRAM_TYPE_LPDDR4) {
		value = (23 << EMC_MRW_MRW_MA_SHIFT) |
			(next->run_clocks & EMC_MRW_MRW_OP_MASK);
		emc_writel(emc, value, EMC_MRW);
	}

	/* Per channel burst registers. */
	emc_dbg(emc, SUB_STEPS, "Writing burst_regs_per_ch\n");

	for (i = 0; i < next->num_burst_per_ch; i++) {
		const struct tegra210_emc_per_channel_regs *burst =
				emc->offsets->burst_per_channel;

		if (!burst[i].offset)
			continue;

		if (dram_type != DRAM_TYPE_LPDDR4 &&
		    (burst[i].offset == EMC_MRW6 ||
		     burst[i].offset == EMC_MRW7 ||
		     burst[i].offset == EMC_MRW8 ||
		     burst[i].offset == EMC_MRW9 ||
		     burst[i].offset == EMC_MRW10 ||
		     burst[i].offset == EMC_MRW11 ||
		     burst[i].offset == EMC_MRW12 ||
		     burst[i].offset == EMC_MRW13 ||
		     burst[i].offset == EMC_MRW14 ||
		     burst[i].offset == EMC_MRW15))
			continue;

		/* Filter out second channel if not in DUAL_CHANNEL mode. */
		if (emc->num_channels < 2 && burst[i].bank >= 1)
			continue;

		emc_dbg(emc, REG_LISTS, "(%u) 0x%08x => 0x%08x\n", i,
			next->burst_reg_per_ch[i], burst[i].offset);
		emc_channel_writel(emc, burst[i].bank,
				   next->burst_reg_per_ch[i],
				   burst[i].offset);
	}

	/* Vref regs. */
	emc_dbg(emc, SUB_STEPS, "Writing vref_regs\n");

	for (i = 0; i < next->vref_num; i++) {
		const struct tegra210_emc_per_channel_regs *vref =
					emc->offsets->vref_per_channel;

		if (!vref[i].offset)
			continue;

		if (emc->num_channels < 2 && vref[i].bank >= 1)
			continue;

		emc_dbg(emc, REG_LISTS, "(%u) 0x%08x => 0x%08x\n", i,
			next->vref_perch_regs[i], vref[i].offset);
		emc_channel_writel(emc, vref[i].bank, next->vref_perch_regs[i],
				   vref[i].offset);
	}

	/* Trimmers. */
	emc_dbg(emc, SUB_STEPS, "Writing trim_regs\n");

	for (i = 0; i < next->num_trim; i++) {
		const u16 *offsets = emc->offsets->trim;

		if (!offsets[i])
			continue;

		if (compensate_trimmer_applicable &&
		    (offsets[i] == EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_0 ||
		     offsets[i] == EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_1 ||
		     offsets[i] == EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_2 ||
		     offsets[i] == EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_3 ||
		     offsets[i] == EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_0 ||
		     offsets[i] == EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_1 ||
		     offsets[i] == EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_2 ||
		     offsets[i] == EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_3 ||
		     offsets[i] == EMC_DATA_BRLSHFT_0 ||
		     offsets[i] == EMC_DATA_BRLSHFT_1)) {
			value = tegra210_emc_compensate(next, offsets[i]);
			emc_dbg(emc, REG_LISTS, "(%u) 0x%08x => 0x%08x\n", i,
				value, offsets[i]);
			emc_dbg(emc, EMA_WRITES, "0x%08x <= 0x%08x\n",
				(u32)(u64)offsets[i], value);
			emc_writel(emc, value, offsets[i]);
		} else {
			emc_dbg(emc, REG_LISTS, "(%u) 0x%08x => 0x%08x\n", i,
				next->trim_regs[i], offsets[i]);
			emc_writel(emc, next->trim_regs[i], offsets[i]);
		}
	}

	/* Per channel trimmers. */
	emc_dbg(emc, SUB_STEPS, "Writing trim_regs_per_ch\n");

	for (i = 0; i < next->num_trim_per_ch; i++) {
		const struct tegra210_emc_per_channel_regs *trim =
				&emc->offsets->trim_per_channel[0];
		unsigned int offset;

		if (!trim[i].offset)
			continue;

		if (emc->num_channels < 2 && trim[i].bank >= 1)
			continue;

		offset = trim[i].offset;

		if (compensate_trimmer_applicable &&
		    (offset == EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_0 ||
		     offset == EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_1 ||
		     offset == EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_2 ||
		     offset == EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_3 ||
		     offset == EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_0 ||
		     offset == EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_1 ||
		     offset == EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_2 ||
		     offset == EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_3 ||
		     offset == EMC_DATA_BRLSHFT_0 ||
		     offset == EMC_DATA_BRLSHFT_1)) {
			value = tegra210_emc_compensate(next, offset);
			emc_dbg(emc, REG_LISTS, "(%u) 0x%08x => 0x%08x\n", i,
				value, offset);
			emc_dbg(emc, EMA_WRITES, "0x%08x <= 0x%08x\n", offset,
				value);
			emc_channel_writel(emc, trim[i].bank, value, offset);
		} else {
			emc_dbg(emc, REG_LISTS, "(%u) 0x%08x => 0x%08x\n", i,
				next->trim_perch_regs[i], offset);
			emc_channel_writel(emc, trim[i].bank,
					   next->trim_perch_regs[i], offset);
		}
	}

	emc_dbg(emc, SUB_STEPS, "Writing burst_mc_regs\n");

	for (i = 0; i < next->num_mc_regs; i++) {
		const u16 *offsets = emc->offsets->burst_mc;
		u32 *values = next->burst_mc_regs;

		emc_dbg(emc, REG_LISTS, "(%u) 0x%08x => 0x%08x\n", i,
			values[i], offsets[i]);
		mc_writel(emc->mc, values[i], offsets[i]);
	}

	/* Registers to be programmed on the faster clock. */
	if (next->rate < last->rate) {
		const u16 *la = emc->offsets->la_scale;

		emc_dbg(emc, SUB_STEPS, "Writing la_scale_regs\n");

		for (i = 0; i < next->num_up_down; i++) {
			emc_dbg(emc, REG_LISTS, "(%u) 0x%08x => 0x%08x\n", i,
				next->la_scale_regs[i], la[i]);
			mc_writel(emc->mc, next->la_scale_regs[i], la[i]);
		}
	}

	/* Flush all the burst register writes. */
	mc_readl(emc->mc, MC_EMEM_ADR_CFG);

	/*
	 * Step 9:
	 *   LPDDR4 section A.
	 */
	emc_dbg(emc, STEPS, "Step 9\n");

	value = next->burst_regs[EMC_ZCAL_WAIT_CNT_INDEX];
	value &= ~EMC_ZCAL_WAIT_CNT_ZCAL_WAIT_CNT_MASK;

	if (dram_type == DRAM_TYPE_LPDDR4) {
		emc_writel(emc, 0, EMC_ZCAL_INTERVAL);
		emc_writel(emc, value, EMC_ZCAL_WAIT_CNT);

		value = emc_dbg | (EMC_DBG_WRITE_MUX_ACTIVE |
				   EMC_DBG_WRITE_ACTIVE_ONLY);

		emc_writel(emc, value, EMC_DBG);
		emc_writel(emc, 0, EMC_ZCAL_INTERVAL);
		emc_writel(emc, emc_dbg, EMC_DBG);
	}

	/*
	 * Step 10:
	 *   LPDDR4 and DDR3 common section.
	 */
	emc_dbg(emc, STEPS, "Step 10\n");

	if (opt_dvfs_mode == MAN_SR || dram_type == DRAM_TYPE_LPDDR4) {
		if (dram_type == DRAM_TYPE_LPDDR4)
			ccfifo_writel(emc, 0x101, EMC_SELF_REF, 0);
		else
			ccfifo_writel(emc, 0x1, EMC_SELF_REF, 0);

		if (dram_type == DRAM_TYPE_LPDDR4 &&
		    dst_clk_period <= zqcal_before_cc_cutoff) {
			ccfifo_writel(emc, mr13_flip_fspwr ^ 0x40, EMC_MRW3, 0);
			ccfifo_writel(emc, (next->burst_regs[EMC_MRW6_INDEX] &
						0xFFFF3F3F) |
					   (last->burst_regs[EMC_MRW6_INDEX] &
						0x0000C0C0), EMC_MRW6, 0);
			ccfifo_writel(emc, (next->burst_regs[EMC_MRW14_INDEX] &
						0xFFFF0707) |
					   (last->burst_regs[EMC_MRW14_INDEX] &
						0x00003838), EMC_MRW14, 0);

			if (emc->num_devices > 1) {
				ccfifo_writel(emc,
				      (next->burst_regs[EMC_MRW7_INDEX] &
				       0xFFFF3F3F) |
				      (last->burst_regs[EMC_MRW7_INDEX] &
				       0x0000C0C0), EMC_MRW7, 0);
				ccfifo_writel(emc,
				     (next->burst_regs[EMC_MRW15_INDEX] &
				      0xFFFF0707) |
				     (last->burst_regs[EMC_MRW15_INDEX] &
				      0x00003838), EMC_MRW15, 0);
			}

			if (opt_zcal_en_cc) {
				if (emc->num_devices < 2)
					ccfifo_writel(emc,
						2UL << EMC_ZQ_CAL_DEV_SEL_SHIFT
						| EMC_ZQ_CAL_ZQ_CAL_CMD,
						EMC_ZQ_CAL, 0);
				else if (shared_zq_resistor)
					ccfifo_writel(emc,
						2UL << EMC_ZQ_CAL_DEV_SEL_SHIFT
						| EMC_ZQ_CAL_ZQ_CAL_CMD,
						EMC_ZQ_CAL, 0);
				else
					ccfifo_writel(emc,
						      EMC_ZQ_CAL_ZQ_CAL_CMD,
						      EMC_ZQ_CAL, 0);
			}
		}
	}

	if (dram_type == DRAM_TYPE_LPDDR4) {
		value = (1000 * fake->dram_timings[T_RP]) / src_clk_period;
		ccfifo_writel(emc, mr13_flip_fspop | 0x8, EMC_MRW3, value);
		ccfifo_writel(emc, 0, 0, tFC_lpddr4 / src_clk_period);
	}

	if (dram_type == DRAM_TYPE_LPDDR4 || opt_dvfs_mode != MAN_SR) {
		delay = 30;

		if (cya_allow_ref_cc) {
			delay += (1000 * fake->dram_timings[T_RP]) /
					src_clk_period;
			delay += 4000 * fake->dram_timings[T_RFC];
		}

		ccfifo_writel(emc, emc_pin & ~(EMC_PIN_PIN_CKE_PER_DEV |
					       EMC_PIN_PIN_CKEB |
					       EMC_PIN_PIN_CKE),
			      EMC_PIN, delay);
	}

	/* calculate reference delay multiplier */
	value = 1;

	if (ref_b4_sref_en)
		value++;

	if (cya_allow_ref_cc)
		value++;

	if (cya_issue_pc_ref)
		value++;

	if (dram_type != DRAM_TYPE_LPDDR4) {
		delay = ((1000 * fake->dram_timings[T_RP] / src_clk_period) +
			 (1000 * fake->dram_timings[T_RFC] / src_clk_period));
		delay = value * delay + 20;
	} else {
		delay = 0;
	}

	/*
	 * Step 11:
	 *   Ramp down.
	 */
	emc_dbg(emc, STEPS, "Step 11\n");

	ccfifo_writel(emc, 0x0, EMC_CFG_SYNC, delay);

	value = emc_dbg | EMC_DBG_WRITE_MUX_ACTIVE | EMC_DBG_WRITE_ACTIVE_ONLY;
	ccfifo_writel(emc, value, EMC_DBG, 0);

	ramp_down_wait = tegra210_emc_dvfs_power_ramp_down(emc, src_clk_period,
							   0);

	/*
	 * Step 12:
	 *   And finally - trigger the clock change.
	 */
	emc_dbg(emc, STEPS, "Step 12\n");

	ccfifo_writel(emc, 1, EMC_STALL_THEN_EXE_AFTER_CLKCHANGE, 0);
	value &= ~EMC_DBG_WRITE_ACTIVE_ONLY;
	ccfifo_writel(emc, value, EMC_DBG, 0);

	/*
	 * Step 13:
	 *   Ramp up.
	 */
	emc_dbg(emc, STEPS, "Step 13\n");

	ramp_up_wait = tegra210_emc_dvfs_power_ramp_up(emc, dst_clk_period, 0);
	ccfifo_writel(emc, emc_dbg, EMC_DBG, 0);

	/*
	 * Step 14:
	 *   Bringup CKE pins.
	 */
	emc_dbg(emc, STEPS, "Step 14\n");

	if (dram_type == DRAM_TYPE_LPDDR4) {
		value = emc_pin | EMC_PIN_PIN_CKE;

		if (emc->num_devices <= 1)
			value &= ~(EMC_PIN_PIN_CKEB | EMC_PIN_PIN_CKE_PER_DEV);
		else
			value |= EMC_PIN_PIN_CKEB | EMC_PIN_PIN_CKE_PER_DEV;

		ccfifo_writel(emc, value, EMC_PIN, 0);
	}

	/*
	 * Step 15: (two step 15s ??)
	 *   Calculate zqlatch wait time; has dependency on ramping times.
	 */
	emc_dbg(emc, STEPS, "Step 15\n");

	if (dst_clk_period <= zqcal_before_cc_cutoff) {
		s32 t = (s32)(ramp_up_wait + ramp_down_wait) /
			(s32)dst_clk_period;
		zq_latch_dvfs_wait_time = (s32)tZQCAL_lpddr4_fc_adj - t;
	} else {
		zq_latch_dvfs_wait_time = tZQCAL_lpddr4_fc_adj -
			div_o3(1000 * next->dram_timings[T_PDEX],
			       dst_clk_period);
	}

	emc_dbg(emc, INFO, "tZQCAL_lpddr4_fc_adj = %u\n", tZQCAL_lpddr4_fc_adj);
	emc_dbg(emc, INFO, "dst_clk_period = %u\n",
		dst_clk_period);
	emc_dbg(emc, INFO, "next->dram_timings[T_PDEX] = %u\n",
		next->dram_timings[T_PDEX]);
	emc_dbg(emc, INFO, "zq_latch_dvfs_wait_time = %d\n",
		max_t(s32, 0, zq_latch_dvfs_wait_time));

	if (dram_type == DRAM_TYPE_LPDDR4 && opt_zcal_en_cc) {
		delay = div_o3(1000 * next->dram_timings[T_PDEX],
			       dst_clk_period);

		if (emc->num_devices < 2) {
			if (dst_clk_period > zqcal_before_cc_cutoff)
				ccfifo_writel(emc,
					      2UL << EMC_ZQ_CAL_DEV_SEL_SHIFT |
					      EMC_ZQ_CAL_ZQ_CAL_CMD, EMC_ZQ_CAL,
					      delay);

			value = (mr13_flip_fspop & 0xfffffff7) | 0x0c000000;
			ccfifo_writel(emc, value, EMC_MRW3, delay);
			ccfifo_writel(emc, 0, EMC_SELF_REF, 0);
			ccfifo_writel(emc, 0, EMC_REF, 0);
			ccfifo_writel(emc, 2UL << EMC_ZQ_CAL_DEV_SEL_SHIFT |
				      EMC_ZQ_CAL_ZQ_LATCH_CMD,
				      EMC_ZQ_CAL,
				      max_t(s32, 0, zq_latch_dvfs_wait_time));
		} else if (shared_zq_resistor) {
			if (dst_clk_period > zqcal_before_cc_cutoff)
				ccfifo_writel(emc,
					      2UL << EMC_ZQ_CAL_DEV_SEL_SHIFT |
					      EMC_ZQ_CAL_ZQ_CAL_CMD, EMC_ZQ_CAL,
					      delay);

			ccfifo_writel(emc, 2UL << EMC_ZQ_CAL_DEV_SEL_SHIFT |
				      EMC_ZQ_CAL_ZQ_LATCH_CMD, EMC_ZQ_CAL,
				      max_t(s32, 0, zq_latch_dvfs_wait_time) +
					delay);
			ccfifo_writel(emc, 1UL << EMC_ZQ_CAL_DEV_SEL_SHIFT |
				      EMC_ZQ_CAL_ZQ_LATCH_CMD,
				      EMC_ZQ_CAL, 0);

			value = (mr13_flip_fspop & 0xfffffff7) | 0x0c000000;
			ccfifo_writel(emc, value, EMC_MRW3, 0);
			ccfifo_writel(emc, 0, EMC_SELF_REF, 0);
			ccfifo_writel(emc, 0, EMC_REF, 0);

			ccfifo_writel(emc, 1UL << EMC_ZQ_CAL_DEV_SEL_SHIFT |
				      EMC_ZQ_CAL_ZQ_LATCH_CMD, EMC_ZQ_CAL,
				      tZQCAL_lpddr4 / dst_clk_period);
		} else {
			if (dst_clk_period > zqcal_before_cc_cutoff)
				ccfifo_writel(emc, EMC_ZQ_CAL_ZQ_CAL_CMD,
					      EMC_ZQ_CAL, delay);

			value = (mr13_flip_fspop & 0xfffffff7) | 0x0c000000;
			ccfifo_writel(emc, value, EMC_MRW3, delay);
			ccfifo_writel(emc, 0, EMC_SELF_REF, 0);
			ccfifo_writel(emc, 0, EMC_REF, 0);

			ccfifo_writel(emc, EMC_ZQ_CAL_ZQ_LATCH_CMD, EMC_ZQ_CAL,
				      max_t(s32, 0, zq_latch_dvfs_wait_time));
		}
	}

	/* WAR: delay for zqlatch */
	ccfifo_writel(emc, 0, 0, 10);

	/*
	 * Step 16:
	 *   LPDDR4 Conditional Training Kickoff. Removed.
	 */

	/*
	 * Step 17:
	 *   MANSR exit self refresh.
	 */
	emc_dbg(emc, STEPS, "Step 17\n");

	if (opt_dvfs_mode == MAN_SR && dram_type != DRAM_TYPE_LPDDR4)
		ccfifo_writel(emc, 0, EMC_SELF_REF, 0);

	/*
	 * Step 18:
	 *   Send MRWs to LPDDR3/DDR3.
	 */
	emc_dbg(emc, STEPS, "Step 18\n");

	if (dram_type == DRAM_TYPE_LPDDR2) {
		ccfifo_writel(emc, next->emc_mrw2, EMC_MRW2, 0);
		ccfifo_writel(emc, next->emc_mrw,  EMC_MRW,  0);
		if (is_lpddr3)
			ccfifo_writel(emc, next->emc_mrw4, EMC_MRW4, 0);
	} else if (dram_type == DRAM_TYPE_DDR3) {
		if (opt_dll_mode)
			ccfifo_writel(emc, next->emc_emrs &
				      ~EMC_EMRS_USE_EMRS_LONG_CNT, EMC_EMRS, 0);
		ccfifo_writel(emc, next->emc_emrs2 &
			      ~EMC_EMRS2_USE_EMRS2_LONG_CNT, EMC_EMRS2, 0);
		ccfifo_writel(emc, next->emc_mrs |
			      EMC_EMRS_USE_EMRS_LONG_CNT, EMC_MRS, 0);
	}

	/*
	 * Step 19:
	 *   ZQCAL for LPDDR3/DDR3
	 */
	emc_dbg(emc, STEPS, "Step 19\n");

	if (opt_zcal_en_cc) {
		if (dram_type == DRAM_TYPE_LPDDR2) {
			value = opt_cc_short_zcal ? 90000 : 360000;
			value = div_o3(value, dst_clk_period);
			value = value <<
				EMC_MRS_WAIT_CNT2_MRS_EXT2_WAIT_CNT_SHIFT |
				value <<
				EMC_MRS_WAIT_CNT2_MRS_EXT1_WAIT_CNT_SHIFT;
			ccfifo_writel(emc, value, EMC_MRS_WAIT_CNT2, 0);

			value = opt_cc_short_zcal ? 0x56 : 0xab;
			ccfifo_writel(emc, 2 << EMC_MRW_MRW_DEV_SELECTN_SHIFT |
					   EMC_MRW_USE_MRW_EXT_CNT |
					   10 << EMC_MRW_MRW_MA_SHIFT |
					   value << EMC_MRW_MRW_OP_SHIFT,
				      EMC_MRW, 0);

			if (emc->num_devices > 1) {
				value = 1 << EMC_MRW_MRW_DEV_SELECTN_SHIFT |
					EMC_MRW_USE_MRW_EXT_CNT |
					10 << EMC_MRW_MRW_MA_SHIFT |
					value << EMC_MRW_MRW_OP_SHIFT;
				ccfifo_writel(emc, value, EMC_MRW, 0);
			}
		} else if (dram_type == DRAM_TYPE_DDR3) {
			value = opt_cc_short_zcal ? 0 : EMC_ZQ_CAL_LONG;

			ccfifo_writel(emc, value |
					   2 << EMC_ZQ_CAL_DEV_SEL_SHIFT |
					   EMC_ZQ_CAL_ZQ_CAL_CMD, EMC_ZQ_CAL,
					   0);

			if (emc->num_devices > 1) {
				value = value | 1 << EMC_ZQ_CAL_DEV_SEL_SHIFT |
						EMC_ZQ_CAL_ZQ_CAL_CMD;
				ccfifo_writel(emc, value, EMC_ZQ_CAL, 0);
			}
		}
	}

	if (bg_reg_mode_change) {
		tegra210_emc_set_shadow_bypass(emc, ACTIVE);

		if (ramp_up_wait <= 1250000)
			delay = (1250000 - ramp_up_wait) / dst_clk_period;
		else
			delay = 0;

		ccfifo_writel(emc,
			      next->burst_regs[EMC_PMACRO_BG_BIAS_CTRL_0_INDEX],
			      EMC_PMACRO_BG_BIAS_CTRL_0, delay);
		tegra210_emc_set_shadow_bypass(emc, ASSEMBLY);
	}

	/*
	 * Step 20:
	 *   Issue ref and optional QRST.
	 */
	emc_dbg(emc, STEPS, "Step 20\n");

	if (dram_type != DRAM_TYPE_LPDDR4)
		ccfifo_writel(emc, 0, EMC_REF, 0);

	if (opt_do_sw_qrst) {
		ccfifo_writel(emc, 1, EMC_ISSUE_QRST, 0);
		ccfifo_writel(emc, 0, EMC_ISSUE_QRST, 2);
	}

	/*
	 * Step 21:
	 *   Restore ZCAL and ZCAL interval.
	 */
	emc_dbg(emc, STEPS, "Step 21\n");

	if (save_restore_clkstop_pd || opt_zcal_en_cc) {
		ccfifo_writel(emc, emc_dbg | EMC_DBG_WRITE_MUX_ACTIVE,
			      EMC_DBG, 0);
		if (opt_zcal_en_cc && dram_type != DRAM_TYPE_LPDDR4)
			ccfifo_writel(emc, next->burst_regs[EMC_ZCAL_INTERVAL_INDEX],
				      EMC_ZCAL_INTERVAL, 0);

		if (save_restore_clkstop_pd)
			ccfifo_writel(emc, next->burst_regs[EMC_CFG_INDEX] &
						~EMC_CFG_DYN_SELF_REF,
				      EMC_CFG, 0);
		ccfifo_writel(emc, emc_dbg, EMC_DBG, 0);
	}

	/*
	 * Step 22:
	 *   Restore EMC_CFG_PIPE_CLK.
	 */
	emc_dbg(emc, STEPS, "Step 22\n");

	ccfifo_writel(emc, emc_cfg_pipe_clk, EMC_CFG_PIPE_CLK, 0);

	if (bg_reg_mode_change) {
		if (enable_bg_reg)
			emc_writel(emc,
				   next->burst_regs[EMC_PMACRO_BG_BIAS_CTRL_0_INDEX] &
					~EMC_PMACRO_BG_BIAS_CTRL_0_BGLP_E_PWRD,
				   EMC_PMACRO_BG_BIAS_CTRL_0);
		else
			emc_writel(emc,
				   next->burst_regs[EMC_PMACRO_BG_BIAS_CTRL_0_INDEX] &
					~EMC_PMACRO_BG_BIAS_CTRL_0_BG_E_PWRD,
				   EMC_PMACRO_BG_BIAS_CTRL_0);
	}

	/*
	 * Step 23:
	 */
	emc_dbg(emc, STEPS, "Step 23\n");

	value = emc_readl(emc, EMC_CFG_DIG_DLL);
	value |= EMC_CFG_DIG_DLL_CFG_DLL_STALL_ALL_TRAFFIC;
	value &= ~EMC_CFG_DIG_DLL_CFG_DLL_STALL_RW_UNTIL_LOCK;
	value &= ~EMC_CFG_DIG_DLL_CFG_DLL_STALL_ALL_UNTIL_LOCK;
	value &= ~EMC_CFG_DIG_DLL_CFG_DLL_EN;
	value = (value & ~EMC_CFG_DIG_DLL_CFG_DLL_MODE_MASK) |
		(2 << EMC_CFG_DIG_DLL_CFG_DLL_MODE_SHIFT);
	emc_writel(emc, value, EMC_CFG_DIG_DLL);

	tegra210_emc_do_clock_change(emc, clksrc);

	/*
	 * Step 24:
	 *   Save training results. Removed.
	 */

	/*
	 * Step 25:
	 *   Program MC updown registers.
	 */
	emc_dbg(emc, STEPS, "Step 25\n");

	if (next->rate > last->rate) {
		for (i = 0; i < next->num_up_down; i++)
			mc_writel(emc->mc, next->la_scale_regs[i],
				  emc->offsets->la_scale[i]);

		tegra210_emc_timing_update(emc);
	}

	/*
	 * Step 26:
	 *   Restore ZCAL registers.
	 */
	emc_dbg(emc, STEPS, "Step 26\n");

	if (dram_type == DRAM_TYPE_LPDDR4) {
		tegra210_emc_set_shadow_bypass(emc, ACTIVE);
		emc_writel(emc, next->burst_regs[EMC_ZCAL_WAIT_CNT_INDEX],
			   EMC_ZCAL_WAIT_CNT);
		emc_writel(emc, next->burst_regs[EMC_ZCAL_INTERVAL_INDEX],
			   EMC_ZCAL_INTERVAL);
		tegra210_emc_set_shadow_bypass(emc, ASSEMBLY);
	}

	if (dram_type != DRAM_TYPE_LPDDR4 && opt_zcal_en_cc &&
	    !opt_short_zcal && opt_cc_short_zcal) {
		udelay(2);

		tegra210_emc_set_shadow_bypass(emc, ACTIVE);
		if (dram_type == DRAM_TYPE_LPDDR2)
			emc_writel(emc, next->burst_regs[EMC_MRS_WAIT_CNT_INDEX],
				   EMC_MRS_WAIT_CNT);
		else if (dram_type == DRAM_TYPE_DDR3)
			emc_writel(emc, next->burst_regs[EMC_ZCAL_WAIT_CNT_INDEX],
				   EMC_ZCAL_WAIT_CNT);
		tegra210_emc_set_shadow_bypass(emc, ASSEMBLY);
	}

	/*
	 * Step 27:
	 *   Restore EMC_CFG, FDPD registers.
	 */
	emc_dbg(emc, STEPS, "Step 27\n");

	tegra210_emc_set_shadow_bypass(emc, ACTIVE);
	emc_writel(emc, next->burst_regs[EMC_CFG_INDEX], EMC_CFG);
	tegra210_emc_set_shadow_bypass(emc, ASSEMBLY);
	emc_writel(emc, next->emc_fdpd_ctrl_cmd_no_ramp,
		   EMC_FDPD_CTRL_CMD_NO_RAMP);
	emc_writel(emc, next->emc_sel_dpd_ctrl, EMC_SEL_DPD_CTRL);

	/*
	 * Step 28:
	 *   Training recover. Removed.
	 */
	emc_dbg(emc, STEPS, "Step 28\n");

	tegra210_emc_set_shadow_bypass(emc, ACTIVE);
	emc_writel(emc,
		   next->burst_regs[EMC_PMACRO_AUTOCAL_CFG_COMMON_INDEX],
		   EMC_PMACRO_AUTOCAL_CFG_COMMON);
	tegra210_emc_set_shadow_bypass(emc, ASSEMBLY);

	/*
	 * Step 29:
	 *   Power fix WAR.
	 */
	emc_dbg(emc, STEPS, "Step 29\n");

	emc_writel(emc, EMC_PMACRO_CFG_PM_GLOBAL_0_DISABLE_CFG_BYTE0 |
		   EMC_PMACRO_CFG_PM_GLOBAL_0_DISABLE_CFG_BYTE1 |
		   EMC_PMACRO_CFG_PM_GLOBAL_0_DISABLE_CFG_BYTE2 |
		   EMC_PMACRO_CFG_PM_GLOBAL_0_DISABLE_CFG_BYTE3 |
		   EMC_PMACRO_CFG_PM_GLOBAL_0_DISABLE_CFG_BYTE4 |
		   EMC_PMACRO_CFG_PM_GLOBAL_0_DISABLE_CFG_BYTE5 |
		   EMC_PMACRO_CFG_PM_GLOBAL_0_DISABLE_CFG_BYTE6 |
		   EMC_PMACRO_CFG_PM_GLOBAL_0_DISABLE_CFG_BYTE7,
		   EMC_PMACRO_CFG_PM_GLOBAL_0);
	emc_writel(emc, EMC_PMACRO_TRAINING_CTRL_0_CH0_TRAINING_E_WRPTR,
		   EMC_PMACRO_TRAINING_CTRL_0);
	emc_writel(emc, EMC_PMACRO_TRAINING_CTRL_1_CH1_TRAINING_E_WRPTR,
		   EMC_PMACRO_TRAINING_CTRL_1);
	emc_writel(emc, 0, EMC_PMACRO_CFG_PM_GLOBAL_0);

	/*
	 * Step 30:
	 *   Re-enable autocal.
	 */
	emc_dbg(emc, STEPS, "Step 30: Re-enable DLL and AUTOCAL\n");

	if (next->burst_regs[EMC_CFG_DIG_DLL_INDEX] & EMC_CFG_DIG_DLL_CFG_DLL_EN) {
		value = emc_readl(emc, EMC_CFG_DIG_DLL);
		value |=  EMC_CFG_DIG_DLL_CFG_DLL_STALL_ALL_TRAFFIC;
		value |=  EMC_CFG_DIG_DLL_CFG_DLL_EN;
		value &= ~EMC_CFG_DIG_DLL_CFG_DLL_STALL_RW_UNTIL_LOCK;
		value &= ~EMC_CFG_DIG_DLL_CFG_DLL_STALL_ALL_UNTIL_LOCK;
		value = (value & ~EMC_CFG_DIG_DLL_CFG_DLL_MODE_MASK) |
			(2 << EMC_CFG_DIG_DLL_CFG_DLL_MODE_SHIFT);
		emc_writel(emc, value, EMC_CFG_DIG_DLL);
		tegra210_emc_timing_update(emc);
	}

	emc_writel(emc, next->emc_auto_cal_config, EMC_AUTO_CAL_CONFIG);

	/* Done! Yay. */
}

const struct tegra210_emc_sequence tegra210_emc_r21021 = {
	.revision = 0x7,
	.set_clock = tegra210_emc_r21021_set_clock,
	.periodic_compensation = tegra210_emc_r21021_periodic_compensation,
};

// SPDX-License-Identifier: GPL-2.0
/*
 * Intel SOC Telemetry Platform Driver: Currently supports APL
 * Copyright (c) 2015, Intel Corporation.
 * All Rights Reserved.
 *
 * This file provides the platform specific telemetry implementation for APL.
 * It used the PUNIT and PMC IPC interfaces for configuring the counters.
 * The accumulated results are fetched from SRAM.
 */

#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include <asm/cpu_device_id.h>
#include <asm/intel-family.h>
#include <asm/intel_pmc_ipc.h>
#include <asm/intel_punit_ipc.h>
#include <asm/intel_telemetry.h>

#define DRIVER_NAME	"intel_telemetry"
#define DRIVER_VERSION	"1.0.0"

#define TELEM_TRC_VERBOSITY_MASK	0x3

#define TELEM_MIN_PERIOD(x)		((x) & 0x7F0000)
#define TELEM_MAX_PERIOD(x)		((x) & 0x7F000000)
#define TELEM_SAMPLE_PERIOD_INVALID(x)	((x) & (BIT(7)))
#define TELEM_CLEAR_SAMPLE_PERIOD(x)	((x) &= ~0x7F)

#define TELEM_SAMPLING_DEFAULT_PERIOD	0xD

#define TELEM_MAX_EVENTS_SRAM		28
#define TELEM_SSRAM_STARTTIME_OFFSET	8
#define TELEM_SSRAM_EVTLOG_OFFSET	16

#define IOSS_TELEM_EVENT_READ		0x0
#define IOSS_TELEM_EVENT_WRITE		0x1
#define IOSS_TELEM_INFO_READ		0x2
#define IOSS_TELEM_TRACE_CTL_READ	0x5
#define IOSS_TELEM_TRACE_CTL_WRITE	0x6
#define IOSS_TELEM_EVENT_CTL_READ	0x7
#define IOSS_TELEM_EVENT_CTL_WRITE	0x8
#define IOSS_TELEM_EVT_CTRL_WRITE_SIZE	0x4
#define IOSS_TELEM_READ_WORD		0x1
#define IOSS_TELEM_WRITE_FOURBYTES	0x4
#define IOSS_TELEM_EVT_WRITE_SIZE	0x3

#define TELEM_INFO_SRAMEVTS_MASK	0xFF00
#define TELEM_INFO_SRAMEVTS_SHIFT	0x8
#define TELEM_SSRAM_READ_TIMEOUT	10

#define TELEM_INFO_NENABLES_MASK	0xFF
#define TELEM_EVENT_ENABLE		0x8000

#define TELEM_MASK_BIT			1
#define TELEM_MASK_BYTE			0xFF
#define BYTES_PER_LONG			8
#define TELEM_MASK_PCS_STATE		0xF

#define TELEM_DISABLE(x)		((x) &= ~(BIT(31)))
#define TELEM_CLEAR_EVENTS(x)		((x) |= (BIT(30)))
#define TELEM_ENABLE_SRAM_EVT_TRACE(x)	((x) &= ~(BIT(30) | BIT(24)))
#define TELEM_ENABLE_PERIODIC(x)	((x) |= (BIT(23) | BIT(31) | BIT(7)))
#define TELEM_EXTRACT_VERBOSITY(x, y)	((y) = (((x) >> 27) & 0x3))
#define TELEM_CLEAR_VERBOSITY_BITS(x)	((x) &= ~(BIT(27) | BIT(28)))
#define TELEM_SET_VERBOSITY_BITS(x, y)	((x) |= ((y) << 27))

#define TELEM_CPU(model, data) \
	{ X86_VENDOR_INTEL, 6, model, X86_FEATURE_ANY, (unsigned long)&data }

enum telemetry_action {
	TELEM_UPDATE = 0,
	TELEM_ADD,
	TELEM_RESET,
	TELEM_ACTION_NONE
};

struct telem_ssram_region {
	u64 timestamp;
	u64 start_time;
	u64 events[TELEM_MAX_EVENTS_SRAM];
};

static struct telemetry_plt_config *telm_conf;

/*
 * The following counters are programmed by default during setup.
 * Only 20 allocated to kernel driver
 */
static struct telemetry_evtmap
	telemetry_apl_ioss_default_events[TELEM_MAX_OS_ALLOCATED_EVENTS] = {
	{"SOC_S0IX_TOTAL_RES",			0x4800},
	{"SOC_S0IX_TOTAL_OCC",			0x4000},
	{"SOC_S0IX_SHALLOW_RES",		0x4801},
	{"SOC_S0IX_SHALLOW_OCC",		0x4001},
	{"SOC_S0IX_DEEP_RES",			0x4802},
	{"SOC_S0IX_DEEP_OCC",			0x4002},
	{"PMC_POWER_GATE",			0x5818},
	{"PMC_D3_STATES",			0x5819},
	{"PMC_D0I3_STATES",			0x581A},
	{"PMC_S0IX_WAKE_REASON_GPIO",		0x6000},
	{"PMC_S0IX_WAKE_REASON_TIMER",		0x6001},
	{"PMC_S0IX_WAKE_REASON_VNNREQ",         0x6002},
	{"PMC_S0IX_WAKE_REASON_LOWPOWER",       0x6003},
	{"PMC_S0IX_WAKE_REASON_EXTERNAL",       0x6004},
	{"PMC_S0IX_WAKE_REASON_MISC",           0x6005},
	{"PMC_S0IX_BLOCKING_IPS_D3_D0I3",       0x6006},
	{"PMC_S0IX_BLOCKING_IPS_PG",            0x6007},
	{"PMC_S0IX_BLOCKING_MISC_IPS_PG",       0x6008},
	{"PMC_S0IX_BLOCK_IPS_VNN_REQ",          0x6009},
	{"PMC_S0IX_BLOCK_IPS_CLOCKS",           0x600B},
};


static struct telemetry_evtmap
	telemetry_apl_pss_default_events[TELEM_MAX_OS_ALLOCATED_EVENTS] = {
	{"IA_CORE0_C6_RES",			0x0400},
	{"IA_CORE0_C6_CTR",			0x0000},
	{"IA_MODULE0_C7_RES",			0x0410},
	{"IA_MODULE0_C7_CTR",			0x000E},
	{"IA_C0_RES",				0x0805},
	{"PCS_LTR",				0x2801},
	{"PSTATES",				0x2802},
	{"SOC_S0I3_RES",			0x0409},
	{"SOC_S0I3_CTR",			0x000A},
	{"PCS_S0I3_CTR",			0x0009},
	{"PCS_C1E_RES",				0x041A},
	{"PCS_IDLE_STATUS",			0x2806},
	{"IA_PERF_LIMITS",			0x280B},
	{"GT_PERF_LIMITS",			0x280C},
	{"PCS_WAKEUP_S0IX_CTR",			0x0030},
	{"PCS_IDLE_BLOCKED",			0x2C00},
	{"PCS_S0IX_BLOCKED",			0x2C01},
	{"PCS_S0IX_WAKE_REASONS",		0x2C02},
	{"PCS_LTR_BLOCKING",			0x2C03},
	{"PC2_AND_MEM_SHALLOW_IDLE_RES",	0x1D40},
};

static struct telemetry_evtmap
	telemetry_glk_pss_default_events[TELEM_MAX_OS_ALLOCATED_EVENTS] = {
	{"IA_CORE0_C6_RES",			0x0400},
	{"IA_CORE0_C6_CTR",			0x0000},
	{"IA_MODULE0_C7_RES",			0x0410},
	{"IA_MODULE0_C7_CTR",			0x000C},
	{"IA_C0_RES",				0x0805},
	{"PCS_LTR",				0x2801},
	{"PSTATES",				0x2802},
	{"SOC_S0I3_RES",			0x0407},
	{"SOC_S0I3_CTR",			0x0008},
	{"PCS_S0I3_CTR",			0x0007},
	{"PCS_C1E_RES",				0x0414},
	{"PCS_IDLE_STATUS",			0x2806},
	{"IA_PERF_LIMITS",			0x280B},
	{"GT_PERF_LIMITS",			0x280C},
	{"PCS_WAKEUP_S0IX_CTR",			0x0025},
	{"PCS_IDLE_BLOCKED",			0x2C00},
	{"PCS_S0IX_BLOCKED",			0x2C01},
	{"PCS_S0IX_WAKE_REASONS",		0x2C02},
	{"PCS_LTR_BLOCKING",			0x2C03},
	{"PC2_AND_MEM_SHALLOW_IDLE_RES",	0x1D40},
};

/* APL specific Data */
static struct telemetry_plt_config telem_apl_config = {
	.pss_config = {
		.telem_evts = telemetry_apl_pss_default_events,
	},
	.ioss_config = {
		.telem_evts = telemetry_apl_ioss_default_events,
	},
};

/* GLK specific Data */
static struct telemetry_plt_config telem_glk_config = {
	.pss_config = {
		.telem_evts = telemetry_glk_pss_default_events,
	},
	.ioss_config = {
		.telem_evts = telemetry_apl_ioss_default_events,
	},
};

static const struct x86_cpu_id telemetry_cpu_ids[] = {
	TELEM_CPU(INTEL_FAM6_ATOM_GOLDMONT, telem_apl_config),
	TELEM_CPU(INTEL_FAM6_ATOM_GOLDMONT_PLUS, telem_glk_config),
	{}
};

MODULE_DEVICE_TABLE(x86cpu, telemetry_cpu_ids);

static inline int telem_get_unitconfig(enum telemetry_unit telem_unit,
				     struct telemetry_unit_config **unit_config)
{
	if (telem_unit == TELEM_PSS)
		*unit_config = &(telm_conf->pss_config);
	else if (telem_unit == TELEM_IOSS)
		*unit_config = &(telm_conf->ioss_config);
	else
		return -EINVAL;

	return 0;

}

static int telemetry_check_evtid(enum telemetry_unit telem_unit,
				 u32 *evtmap, u8 len,
				 enum telemetry_action action)
{
	struct telemetry_unit_config *unit_config;
	int ret;

	ret = telem_get_unitconfig(telem_unit, &unit_config);
	if (ret < 0)
		return ret;

	switch (action) {
	case TELEM_RESET:
		if (len > TELEM_MAX_EVENTS_SRAM)
			return -EINVAL;

		break;

	case TELEM_UPDATE:
		if (len > TELEM_MAX_EVENTS_SRAM)
			return -EINVAL;

		if ((len > 0) && (evtmap == NULL))
			return -EINVAL;

		break;

	case TELEM_ADD:
		if ((len + unit_config->ssram_evts_used) >
		    TELEM_MAX_EVENTS_SRAM)
			return -EINVAL;

		if ((len > 0) && (evtmap == NULL))
			return -EINVAL;

		break;

	default:
		pr_err("Unknown Telemetry action specified %d\n", action);
		return -EINVAL;
	}

	return 0;
}


static inline int telemetry_plt_config_ioss_event(u32 evt_id, int index)
{
	u32 write_buf;
	int ret;

	write_buf = evt_id | TELEM_EVENT_ENABLE;
	write_buf <<= BITS_PER_BYTE;
	write_buf |= index;

	ret = intel_pmc_ipc_command(PMC_IPC_PMC_TELEMTRY,
				    IOSS_TELEM_EVENT_WRITE, (u8 *)&write_buf,
				    IOSS_TELEM_EVT_WRITE_SIZE, NULL, 0);

	return ret;
}

static inline int telemetry_plt_config_pss_event(u32 evt_id, int index)
{
	u32 write_buf;
	int ret;

	write_buf = evt_id | TELEM_EVENT_ENABLE;
	ret = intel_punit_ipc_command(IPC_PUNIT_BIOS_WRITE_TELE_EVENT,
				      index, 0, &write_buf, NULL);

	return ret;
}

static int telemetry_setup_iossevtconfig(struct telemetry_evtconfig evtconfig,
					 enum telemetry_action action)
{
	u8 num_ioss_evts, ioss_period;
	int ret, index, idx;
	u32 *ioss_evtmap;
	u32 telem_ctrl;

	num_ioss_evts = evtconfig.num_evts;
	ioss_period = evtconfig.period;
	ioss_evtmap = evtconfig.evtmap;

	/* Get telemetry EVENT CTL */
	ret = intel_pmc_ipc_command(PMC_IPC_PMC_TELEMTRY,
				    IOSS_TELEM_EVENT_CTL_READ, NULL, 0,
				    &telem_ctrl, IOSS_TELEM_READ_WORD);
	if (ret) {
		pr_err("IOSS TELEM_CTRL Read Failed\n");
		return ret;
	}

	/* Disable Telemetry */
	TELEM_DISABLE(telem_ctrl);

	ret = intel_pmc_ipc_command(PMC_IPC_PMC_TELEMTRY,
				    IOSS_TELEM_EVENT_CTL_WRITE,
				    (u8 *)&telem_ctrl,
				    IOSS_TELEM_EVT_CTRL_WRITE_SIZE,
				    NULL, 0);
	if (ret) {
		pr_err("IOSS TELEM_CTRL Event Disable Write Failed\n");
		return ret;
	}


	/* Reset Everything */
	if (action == TELEM_RESET) {
		/* Clear All Events */
		TELEM_CLEAR_EVENTS(telem_ctrl);

		ret = intel_pmc_ipc_command(PMC_IPC_PMC_TELEMTRY,
					    IOSS_TELEM_EVENT_CTL_WRITE,
					    (u8 *)&telem_ctrl,
					    IOSS_TELEM_EVT_CTRL_WRITE_SIZE,
					    NULL, 0);
		if (ret) {
			pr_err("IOSS TELEM_CTRL Event Disable Write Failed\n");
			return ret;
		}
		telm_conf->ioss_config.ssram_evts_used = 0;

		/* Configure Events */
		for (idx = 0; idx < num_ioss_evts; idx++) {
			if (telemetry_plt_config_ioss_event(
			    telm_conf->ioss_config.telem_evts[idx].evt_id,
			    idx)) {
				pr_err("IOSS TELEM_RESET Fail for data: %x\n",
				telm_conf->ioss_config.telem_evts[idx].evt_id);
				continue;
			}
			telm_conf->ioss_config.ssram_evts_used++;
		}
	}

	/* Re-Configure Everything */
	if (action == TELEM_UPDATE) {
		/* Clear All Events */
		TELEM_CLEAR_EVENTS(telem_ctrl);

		ret = intel_pmc_ipc_command(PMC_IPC_PMC_TELEMTRY,
					    IOSS_TELEM_EVENT_CTL_WRITE,
					    (u8 *)&telem_ctrl,
					    IOSS_TELEM_EVT_CTRL_WRITE_SIZE,
					    NULL, 0);
		if (ret) {
			pr_err("IOSS TELEM_CTRL Event Disable Write Failed\n");
			return ret;
		}
		telm_conf->ioss_config.ssram_evts_used = 0;

		/* Configure Events */
		for (index = 0; index < num_ioss_evts; index++) {
			telm_conf->ioss_config.telem_evts[index].evt_id =
			ioss_evtmap[index];

			if (telemetry_plt_config_ioss_event(
			    telm_conf->ioss_config.telem_evts[index].evt_id,
			    index)) {
				pr_err("IOSS TELEM_UPDATE Fail for Evt%x\n",
					ioss_evtmap[index]);
				continue;
			}
			telm_conf->ioss_config.ssram_evts_used++;
		}
	}

	/* Add some Events */
	if (action == TELEM_ADD) {
		/* Configure Events */
		for (index = telm_conf->ioss_config.ssram_evts_used, idx = 0;
		     idx < num_ioss_evts; index++, idx++) {
			telm_conf->ioss_config.telem_evts[index].evt_id =
			ioss_evtmap[idx];

			if (telemetry_plt_config_ioss_event(
			    telm_conf->ioss_config.telem_evts[index].evt_id,
			    index)) {
				pr_err("IOSS TELEM_ADD Fail for Event %x\n",
					ioss_evtmap[idx]);
				continue;
			}
			telm_conf->ioss_config.ssram_evts_used++;
		}
	}

	/* Enable Periodic Telemetry Events and enable SRAM trace */
	TELEM_CLEAR_SAMPLE_PERIOD(telem_ctrl);
	TELEM_ENABLE_SRAM_EVT_TRACE(telem_ctrl);
	TELEM_ENABLE_PERIODIC(telem_ctrl);
	telem_ctrl |= ioss_period;

	ret = intel_pmc_ipc_command(PMC_IPC_PMC_TELEMTRY,
				    IOSS_TELEM_EVENT_CTL_WRITE,
				    (u8 *)&telem_ctrl,
				    IOSS_TELEM_EVT_CTRL_WRITE_SIZE, NULL, 0);
	if (ret) {
		pr_err("IOSS TELEM_CTRL Event Enable Write Failed\n");
		return ret;
	}

	telm_conf->ioss_config.curr_period = ioss_period;

	return 0;
}


static int telemetry_setup_pssevtconfig(struct telemetry_evtconfig evtconfig,
					enum telemetry_action action)
{
	u8 num_pss_evts, pss_period;
	int ret, index, idx;
	u32 *pss_evtmap;
	u32 telem_ctrl;

	num_pss_evts = evtconfig.num_evts;
	pss_period = evtconfig.period;
	pss_evtmap = evtconfig.evtmap;

	/* PSS Config */
	/* Get telemetry EVENT CTL */
	ret = intel_punit_ipc_command(IPC_PUNIT_BIOS_READ_TELE_EVENT_CTRL,
				      0, 0, NULL, &telem_ctrl);
	if (ret) {
		pr_err("PSS TELEM_CTRL Read Failed\n");
		return ret;
	}

	/* Disable Telemetry */
	TELEM_DISABLE(telem_ctrl);
	ret = intel_punit_ipc_command(IPC_PUNIT_BIOS_WRITE_TELE_EVENT_CTRL,
				      0, 0, &telem_ctrl, NULL);
	if (ret) {
		pr_err("PSS TELEM_CTRL Event Disable Write Failed\n");
		return ret;
	}

	/* Reset Everything */
	if (action == TELEM_RESET) {
		/* Clear All Events */
		TELEM_CLEAR_EVENTS(telem_ctrl);

		ret = intel_punit_ipc_command(
				IPC_PUNIT_BIOS_WRITE_TELE_EVENT_CTRL,
				0, 0, &telem_ctrl, NULL);
		if (ret) {
			pr_err("PSS TELEM_CTRL Event Disable Write Failed\n");
			return ret;
		}
		telm_conf->pss_config.ssram_evts_used = 0;
		/* Configure Events */
		for (idx = 0; idx < num_pss_evts; idx++) {
			if (telemetry_plt_config_pss_event(
			    telm_conf->pss_config.telem_evts[idx].evt_id,
			    idx)) {
				pr_err("PSS TELEM_RESET Fail for Event %x\n",
				telm_conf->pss_config.telem_evts[idx].evt_id);
				continue;
			}
			telm_conf->pss_config.ssram_evts_used++;
		}
	}

	/* Re-Configure Everything */
	if (action == TELEM_UPDATE) {
		/* Clear All Events */
		TELEM_CLEAR_EVENTS(telem_ctrl);

		ret = intel_punit_ipc_command(
				IPC_PUNIT_BIOS_WRITE_TELE_EVENT_CTRL,
				0, 0, &telem_ctrl, NULL);
		if (ret) {
			pr_err("PSS TELEM_CTRL Event Disable Write Failed\n");
			return ret;
		}
		telm_conf->pss_config.ssram_evts_used = 0;

		/* Configure Events */
		for (index = 0; index < num_pss_evts; index++) {
			telm_conf->pss_config.telem_evts[index].evt_id =
			pss_evtmap[index];

			if (telemetry_plt_config_pss_event(
			    telm_conf->pss_config.telem_evts[index].evt_id,
			    index)) {
				pr_err("PSS TELEM_UPDATE Fail for Event %x\n",
					pss_evtmap[index]);
				continue;
			}
			telm_conf->pss_config.ssram_evts_used++;
		}
	}

	/* Add some Events */
	if (action == TELEM_ADD) {
		/* Configure Events */
		for (index = telm_conf->pss_config.ssram_evts_used, idx = 0;
		     idx < num_pss_evts; index++, idx++) {

			telm_conf->pss_config.telem_evts[index].evt_id =
			pss_evtmap[idx];

			if (telemetry_plt_config_pss_event(
			    telm_conf->pss_config.telem_evts[index].evt_id,
			    index)) {
				pr_err("PSS TELEM_ADD Fail for Event %x\n",
					pss_evtmap[idx]);
				continue;
			}
			telm_conf->pss_config.ssram_evts_used++;
		}
	}

	/* Enable Periodic Telemetry Events and enable SRAM trace */
	TELEM_CLEAR_SAMPLE_PERIOD(telem_ctrl);
	TELEM_ENABLE_SRAM_EVT_TRACE(telem_ctrl);
	TELEM_ENABLE_PERIODIC(telem_ctrl);
	telem_ctrl |= pss_period;

	ret = intel_punit_ipc_command(IPC_PUNIT_BIOS_WRITE_TELE_EVENT_CTRL,
				      0, 0, &telem_ctrl, NULL);
	if (ret) {
		pr_err("PSS TELEM_CTRL Event Enable Write Failed\n");
		return ret;
	}

	telm_conf->pss_config.curr_period = pss_period;

	return 0;
}

static int telemetry_setup_evtconfig(struct telemetry_evtconfig pss_evtconfig,
				     struct telemetry_evtconfig ioss_evtconfig,
				     enum telemetry_action action)
{
	int ret;

	mutex_lock(&(telm_conf->telem_lock));

	if ((action == TELEM_UPDATE) && (telm_conf->telem_in_use)) {
		ret = -EBUSY;
		goto out;
	}

	ret = telemetry_check_evtid(TELEM_PSS, pss_evtconfig.evtmap,
				    pss_evtconfig.num_evts, action);
	if (ret)
		goto out;

	ret = telemetry_check_evtid(TELEM_IOSS, ioss_evtconfig.evtmap,
				    ioss_evtconfig.num_evts, action);
	if (ret)
		goto out;

	if (ioss_evtconfig.num_evts) {
		ret = telemetry_setup_iossevtconfig(ioss_evtconfig, action);
		if (ret)
			goto out;
	}

	if (pss_evtconfig.num_evts) {
		ret = telemetry_setup_pssevtconfig(pss_evtconfig, action);
		if (ret)
			goto out;
	}

	if ((action == TELEM_UPDATE) || (action == TELEM_ADD))
		telm_conf->telem_in_use = true;
	else
		telm_conf->telem_in_use = false;

out:
	mutex_unlock(&(telm_conf->telem_lock));
	return ret;
}

static int telemetry_setup(struct platform_device *pdev)
{
	struct telemetry_evtconfig pss_evtconfig, ioss_evtconfig;
	u32 read_buf, events, event_regs;
	int ret;

	ret = intel_pmc_ipc_command(PMC_IPC_PMC_TELEMTRY, IOSS_TELEM_INFO_READ,
				    NULL, 0, &read_buf, IOSS_TELEM_READ_WORD);
	if (ret) {
		dev_err(&pdev->dev, "IOSS TELEM_INFO Read Failed\n");
		return ret;
	}

	/* Get telemetry Info */
	events = (read_buf & TELEM_INFO_SRAMEVTS_MASK) >>
		  TELEM_INFO_SRAMEVTS_SHIFT;
	event_regs = read_buf & TELEM_INFO_NENABLES_MASK;
	if ((events < TELEM_MAX_EVENTS_SRAM) ||
	    (event_regs < TELEM_MAX_EVENTS_SRAM)) {
		dev_err(&pdev->dev, "IOSS:Insufficient Space for SRAM Trace\n");
		dev_err(&pdev->dev, "SRAM Events %d; Event Regs %d\n",
			events, event_regs);
		return -ENOMEM;
	}

	telm_conf->ioss_config.min_period = TELEM_MIN_PERIOD(read_buf);
	telm_conf->ioss_config.max_period = TELEM_MAX_PERIOD(read_buf);

	/* PUNIT Mailbox Setup */
	ret = intel_punit_ipc_command(IPC_PUNIT_BIOS_READ_TELE_INFO, 0, 0,
				      NULL, &read_buf);
	if (ret) {
		dev_err(&pdev->dev, "PSS TELEM_INFO Read Failed\n");
		return ret;
	}

	/* Get telemetry Info */
	events = (read_buf & TELEM_INFO_SRAMEVTS_MASK) >>
		  TELEM_INFO_SRAMEVTS_SHIFT;
	event_regs = read_buf & TELEM_INFO_SRAMEVTS_MASK;
	if ((events < TELEM_MAX_EVENTS_SRAM) ||
	    (event_regs < TELEM_MAX_EVENTS_SRAM)) {
		dev_err(&pdev->dev, "PSS:Insufficient Space for SRAM Trace\n");
		dev_err(&pdev->dev, "SRAM Events %d; Event Regs %d\n",
			events, event_regs);
		return -ENOMEM;
	}

	telm_conf->pss_config.min_period = TELEM_MIN_PERIOD(read_buf);
	telm_conf->pss_config.max_period = TELEM_MAX_PERIOD(read_buf);

	pss_evtconfig.evtmap = NULL;
	pss_evtconfig.num_evts = TELEM_MAX_OS_ALLOCATED_EVENTS;
	pss_evtconfig.period = TELEM_SAMPLING_DEFAULT_PERIOD;

	ioss_evtconfig.evtmap = NULL;
	ioss_evtconfig.num_evts = TELEM_MAX_OS_ALLOCATED_EVENTS;
	ioss_evtconfig.period = TELEM_SAMPLING_DEFAULT_PERIOD;

	ret = telemetry_setup_evtconfig(pss_evtconfig, ioss_evtconfig,
					TELEM_RESET);
	if (ret) {
		dev_err(&pdev->dev, "TELEMETRY Setup Failed\n");
		return ret;
	}
	return 0;
}

static int telemetry_plt_update_events(struct telemetry_evtconfig pss_evtconfig,
				struct telemetry_evtconfig ioss_evtconfig)
{
	int ret;

	if ((pss_evtconfig.num_evts > 0) &&
	    (TELEM_SAMPLE_PERIOD_INVALID(pss_evtconfig.period))) {
		pr_err("PSS Sampling Period Out of Range\n");
		return -EINVAL;
	}

	if ((ioss_evtconfig.num_evts > 0) &&
	    (TELEM_SAMPLE_PERIOD_INVALID(ioss_evtconfig.period))) {
		pr_err("IOSS Sampling Period Out of Range\n");
		return -EINVAL;
	}

	ret = telemetry_setup_evtconfig(pss_evtconfig, ioss_evtconfig,
					TELEM_UPDATE);
	if (ret)
		pr_err("TELEMETRY Config Failed\n");

	return ret;
}


static int telemetry_plt_set_sampling_period(u8 pss_period, u8 ioss_period)
{
	u32 telem_ctrl = 0;
	int ret = 0;

	mutex_lock(&(telm_conf->telem_lock));
	if (ioss_period) {
		if (TELEM_SAMPLE_PERIOD_INVALID(ioss_period)) {
			pr_err("IOSS Sampling Period Out of Range\n");
			ret = -EINVAL;
			goto out;
		}

		/* Get telemetry EVENT CTL */
		ret = intel_pmc_ipc_command(PMC_IPC_PMC_TELEMTRY,
					    IOSS_TELEM_EVENT_CTL_READ, NULL, 0,
					    &telem_ctrl, IOSS_TELEM_READ_WORD);
		if (ret) {
			pr_err("IOSS TELEM_CTRL Read Failed\n");
			goto out;
		}

		/* Disable Telemetry */
		TELEM_DISABLE(telem_ctrl);

		ret = intel_pmc_ipc_command(PMC_IPC_PMC_TELEMTRY,
					    IOSS_TELEM_EVENT_CTL_WRITE,
					    (u8 *)&telem_ctrl,
					    IOSS_TELEM_EVT_CTRL_WRITE_SIZE,
					    NULL, 0);
		if (ret) {
			pr_err("IOSS TELEM_CTRL Event Disable Write Failed\n");
			goto out;
		}

		/* Enable Periodic Telemetry Events and enable SRAM trace */
		TELEM_CLEAR_SAMPLE_PERIOD(telem_ctrl);
		TELEM_ENABLE_SRAM_EVT_TRACE(telem_ctrl);
		TELEM_ENABLE_PERIODIC(telem_ctrl);
		telem_ctrl |= ioss_period;

		ret = intel_pmc_ipc_command(PMC_IPC_PMC_TELEMTRY,
					    IOSS_TELEM_EVENT_CTL_WRITE,
					    (u8 *)&telem_ctrl,
					    IOSS_TELEM_EVT_CTRL_WRITE_SIZE,
					    NULL, 0);
		if (ret) {
			pr_err("IOSS TELEM_CTRL Event Enable Write Failed\n");
			goto out;
		}
		telm_conf->ioss_config.curr_period = ioss_period;
	}

	if (pss_period) {
		if (TELEM_SAMPLE_PERIOD_INVALID(pss_period)) {
			pr_err("PSS Sampling Period Out of Range\n");
			ret = -EINVAL;
			goto out;
		}

		/* Get telemetry EVENT CTL */
		ret = intel_punit_ipc_command(
				IPC_PUNIT_BIOS_READ_TELE_EVENT_CTRL,
				0, 0, NULL, &telem_ctrl);
		if (ret) {
			pr_err("PSS TELEM_CTRL Read Failed\n");
			goto out;
		}

		/* Disable Telemetry */
		TELEM_DISABLE(telem_ctrl);
		ret = intel_punit_ipc_command(
				IPC_PUNIT_BIOS_WRITE_TELE_EVENT_CTRL,
				0, 0, &telem_ctrl, NULL);
		if (ret) {
			pr_err("PSS TELEM_CTRL Event Disable Write Failed\n");
			goto out;
		}

		/* Enable Periodic Telemetry Events and enable SRAM trace */
		TELEM_CLEAR_SAMPLE_PERIOD(telem_ctrl);
		TELEM_ENABLE_SRAM_EVT_TRACE(telem_ctrl);
		TELEM_ENABLE_PERIODIC(telem_ctrl);
		telem_ctrl |= pss_period;

		ret = intel_punit_ipc_command(
				IPC_PUNIT_BIOS_WRITE_TELE_EVENT_CTRL,
				0, 0, &telem_ctrl, NULL);
		if (ret) {
			pr_err("PSS TELEM_CTRL Event Enable Write Failed\n");
			goto out;
		}
		telm_conf->pss_config.curr_period = pss_period;
	}

out:
	mutex_unlock(&(telm_conf->telem_lock));
	return ret;
}


static int telemetry_plt_get_sampling_period(u8 *pss_min_period,
					     u8 *pss_max_period,
					     u8 *ioss_min_period,
					     u8 *ioss_max_period)
{
	*pss_min_period = telm_conf->pss_config.min_period;
	*pss_max_period = telm_conf->pss_config.max_period;
	*ioss_min_period = telm_conf->ioss_config.min_period;
	*ioss_max_period = telm_conf->ioss_config.max_period;

	return 0;
}


static int telemetry_plt_reset_events(void)
{
	struct telemetry_evtconfig pss_evtconfig, ioss_evtconfig;
	int ret;

	pss_evtconfig.evtmap = NULL;
	pss_evtconfig.num_evts = TELEM_MAX_OS_ALLOCATED_EVENTS;
	pss_evtconfig.period = TELEM_SAMPLING_DEFAULT_PERIOD;

	ioss_evtconfig.evtmap = NULL;
	ioss_evtconfig.num_evts = TELEM_MAX_OS_ALLOCATED_EVENTS;
	ioss_evtconfig.period = TELEM_SAMPLING_DEFAULT_PERIOD;

	ret = telemetry_setup_evtconfig(pss_evtconfig, ioss_evtconfig,
					TELEM_RESET);
	if (ret)
		pr_err("TELEMETRY Reset Failed\n");

	return ret;
}


static int telemetry_plt_get_eventconfig(struct telemetry_evtconfig *pss_config,
					struct telemetry_evtconfig *ioss_config,
					int pss_len, int ioss_len)
{
	u32 *pss_evtmap, *ioss_evtmap;
	u32 index;

	pss_evtmap = pss_config->evtmap;
	ioss_evtmap = ioss_config->evtmap;

	mutex_lock(&(telm_conf->telem_lock));
	pss_config->num_evts = telm_conf->pss_config.ssram_evts_used;
	ioss_config->num_evts = telm_conf->ioss_config.ssram_evts_used;

	pss_config->period = telm_conf->pss_config.curr_period;
	ioss_config->period = telm_conf->ioss_config.curr_period;

	if ((pss_len < telm_conf->pss_config.ssram_evts_used) ||
	    (ioss_len < telm_conf->ioss_config.ssram_evts_used)) {
		mutex_unlock(&(telm_conf->telem_lock));
		return -EINVAL;
	}

	for (index = 0; index < telm_conf->pss_config.ssram_evts_used;
	     index++) {
		pss_evtmap[index] =
		telm_conf->pss_config.telem_evts[index].evt_id;
	}

	for (index = 0; index < telm_conf->ioss_config.ssram_evts_used;
	     index++) {
		ioss_evtmap[index] =
		telm_conf->ioss_config.telem_evts[index].evt_id;
	}

	mutex_unlock(&(telm_conf->telem_lock));
	return 0;
}


static int telemetry_plt_add_events(u8 num_pss_evts, u8 num_ioss_evts,
				    u32 *pss_evtmap, u32 *ioss_evtmap)
{
	struct telemetry_evtconfig pss_evtconfig, ioss_evtconfig;
	int ret;

	pss_evtconfig.evtmap = pss_evtmap;
	pss_evtconfig.num_evts = num_pss_evts;
	pss_evtconfig.period = telm_conf->pss_config.curr_period;

	ioss_evtconfig.evtmap = ioss_evtmap;
	ioss_evtconfig.num_evts = num_ioss_evts;
	ioss_evtconfig.period = telm_conf->ioss_config.curr_period;

	ret = telemetry_setup_evtconfig(pss_evtconfig, ioss_evtconfig,
					TELEM_ADD);
	if (ret)
		pr_err("TELEMETRY ADD Failed\n");

	return ret;
}

static int telem_evtlog_read(enum telemetry_unit telem_unit,
			     struct telem_ssram_region *ssram_region, u8 len)
{
	struct telemetry_unit_config *unit_config;
	u64 timestamp_prev, timestamp_next;
	int ret, index, timeout = 0;

	ret = telem_get_unitconfig(telem_unit, &unit_config);
	if (ret < 0)
		return ret;

	if (len > unit_config->ssram_evts_used)
		len = unit_config->ssram_evts_used;

	do {
		timestamp_prev = readq(unit_config->regmap);
		if (!timestamp_prev) {
			pr_err("Ssram under update. Please Try Later\n");
			return -EBUSY;
		}

		ssram_region->start_time = readq(unit_config->regmap +
						 TELEM_SSRAM_STARTTIME_OFFSET);

		for (index = 0; index < len; index++) {
			ssram_region->events[index] =
			readq(unit_config->regmap + TELEM_SSRAM_EVTLOG_OFFSET +
			      BYTES_PER_LONG*index);
		}

		timestamp_next = readq(unit_config->regmap);
		if (!timestamp_next) {
			pr_err("Ssram under update. Please Try Later\n");
			return -EBUSY;
		}

		if (timeout++ > TELEM_SSRAM_READ_TIMEOUT) {
			pr_err("Timeout while reading Events\n");
			return -EBUSY;
		}

	} while (timestamp_prev != timestamp_next);

	ssram_region->timestamp = timestamp_next;

	return len;
}

static int telemetry_plt_raw_read_eventlog(enum telemetry_unit telem_unit,
					   struct telemetry_evtlog *evtlog,
					   int len, int log_all_evts)
{
	int index, idx1, ret, readlen = len;
	struct telem_ssram_region ssram_region;
	struct telemetry_evtmap *evtmap;

	switch (telem_unit)	{
	case TELEM_PSS:
		evtmap = telm_conf->pss_config.telem_evts;
		break;

	case TELEM_IOSS:
		evtmap = telm_conf->ioss_config.telem_evts;
		break;

	default:
		pr_err("Unknown Telemetry Unit Specified %d\n", telem_unit);
		return -EINVAL;
	}

	if (!log_all_evts)
		readlen = TELEM_MAX_EVENTS_SRAM;

	ret = telem_evtlog_read(telem_unit, &ssram_region, readlen);
	if (ret < 0)
		return ret;

	/* Invalid evt-id array specified via length mismatch */
	if ((!log_all_evts) && (len > ret))
		return -EINVAL;

	if (log_all_evts)
		for (index = 0; index < ret; index++) {
			evtlog[index].telem_evtlog = ssram_region.events[index];
			evtlog[index].telem_evtid = evtmap[index].evt_id;
		}
	else
		for (index = 0, readlen = 0; (index < ret) && (readlen < len);
		     index++) {
			for (idx1 = 0; idx1 < len; idx1++) {
				/* Elements matched */
				if (evtmap[index].evt_id ==
				    evtlog[idx1].telem_evtid) {
					evtlog[idx1].telem_evtlog =
					ssram_region.events[index];
					readlen++;

					break;
				}
			}
		}

	return readlen;
}

static int telemetry_plt_read_eventlog(enum telemetry_unit telem_unit,
		struct telemetry_evtlog *evtlog, int len, int log_all_evts)
{
	int ret;

	mutex_lock(&(telm_conf->telem_lock));
	ret = telemetry_plt_raw_read_eventlog(telem_unit, evtlog,
					      len, log_all_evts);
	mutex_unlock(&(telm_conf->telem_lock));

	return ret;
}

static int telemetry_plt_get_trace_verbosity(enum telemetry_unit telem_unit,
					     u32 *verbosity)
{
	u32 temp = 0;
	int ret;

	if (verbosity == NULL)
		return -EINVAL;

	mutex_lock(&(telm_conf->telem_trace_lock));
	switch (telem_unit) {
	case TELEM_PSS:
		ret = intel_punit_ipc_command(
				IPC_PUNIT_BIOS_READ_TELE_TRACE_CTRL,
				0, 0, NULL, &temp);
		if (ret) {
			pr_err("PSS TRACE_CTRL Read Failed\n");
			goto out;
		}

		break;

	case TELEM_IOSS:
		ret = intel_pmc_ipc_command(PMC_IPC_PMC_TELEMTRY,
				IOSS_TELEM_TRACE_CTL_READ, NULL, 0, &temp,
				IOSS_TELEM_READ_WORD);
		if (ret) {
			pr_err("IOSS TRACE_CTL Read Failed\n");
			goto out;
		}

		break;

	default:
		pr_err("Unknown Telemetry Unit Specified %d\n", telem_unit);
		ret = -EINVAL;
		break;
	}
	TELEM_EXTRACT_VERBOSITY(temp, *verbosity);

out:
	mutex_unlock(&(telm_conf->telem_trace_lock));
	return ret;
}

static int telemetry_plt_set_trace_verbosity(enum telemetry_unit telem_unit,
					     u32 verbosity)
{
	u32 temp = 0;
	int ret;

	verbosity &= TELEM_TRC_VERBOSITY_MASK;

	mutex_lock(&(telm_conf->telem_trace_lock));
	switch (telem_unit) {
	case TELEM_PSS:
		ret = intel_punit_ipc_command(
				IPC_PUNIT_BIOS_READ_TELE_TRACE_CTRL,
				0, 0, NULL, &temp);
		if (ret) {
			pr_err("PSS TRACE_CTRL Read Failed\n");
			goto out;
		}

		TELEM_CLEAR_VERBOSITY_BITS(temp);
		TELEM_SET_VERBOSITY_BITS(temp, verbosity);

		ret = intel_punit_ipc_command(
				IPC_PUNIT_BIOS_WRITE_TELE_TRACE_CTRL,
				0, 0, &temp, NULL);
		if (ret) {
			pr_err("PSS TRACE_CTRL Verbosity Set Failed\n");
			goto out;
		}
		break;

	case TELEM_IOSS:
		ret = intel_pmc_ipc_command(PMC_IPC_PMC_TELEMTRY,
				IOSS_TELEM_TRACE_CTL_READ, NULL, 0, &temp,
				IOSS_TELEM_READ_WORD);
		if (ret) {
			pr_err("IOSS TRACE_CTL Read Failed\n");
			goto out;
		}

		TELEM_CLEAR_VERBOSITY_BITS(temp);
		TELEM_SET_VERBOSITY_BITS(temp, verbosity);

		ret = intel_pmc_ipc_command(PMC_IPC_PMC_TELEMTRY,
				IOSS_TELEM_TRACE_CTL_WRITE, (u8 *)&temp,
				IOSS_TELEM_WRITE_FOURBYTES, NULL, 0);
		if (ret) {
			pr_err("IOSS TRACE_CTL Verbosity Set Failed\n");
			goto out;
		}
		break;

	default:
		pr_err("Unknown Telemetry Unit Specified %d\n", telem_unit);
		ret = -EINVAL;
		break;
	}

out:
	mutex_unlock(&(telm_conf->telem_trace_lock));
	return ret;
}

static const struct telemetry_core_ops telm_pltops = {
	.get_trace_verbosity = telemetry_plt_get_trace_verbosity,
	.set_trace_verbosity = telemetry_plt_set_trace_verbosity,
	.set_sampling_period = telemetry_plt_set_sampling_period,
	.get_sampling_period = telemetry_plt_get_sampling_period,
	.raw_read_eventlog = telemetry_plt_raw_read_eventlog,
	.get_eventconfig = telemetry_plt_get_eventconfig,
	.update_events = telemetry_plt_update_events,
	.read_eventlog = telemetry_plt_read_eventlog,
	.reset_events = telemetry_plt_reset_events,
	.add_events = telemetry_plt_add_events,
};

static int telemetry_pltdrv_probe(struct platform_device *pdev)
{
	const struct x86_cpu_id *id;
	void __iomem *mem;
	int ret;

	id = x86_match_cpu(telemetry_cpu_ids);
	if (!id)
		return -ENODEV;

	telm_conf = (struct telemetry_plt_config *)id->driver_data;

	mem = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(mem))
		return PTR_ERR(mem);

	telm_conf->pss_config.regmap = mem;

	mem = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(mem))
		return PTR_ERR(mem);

	telm_conf->ioss_config.regmap = mem;

	mutex_init(&telm_conf->telem_lock);
	mutex_init(&telm_conf->telem_trace_lock);

	ret = telemetry_setup(pdev);
	if (ret)
		goto out;

	ret = telemetry_set_pltdata(&telm_pltops, telm_conf);
	if (ret) {
		dev_err(&pdev->dev, "TELEMETRY Set Pltops Failed.\n");
		goto out;
	}

	return 0;

out:
	dev_err(&pdev->dev, "TELEMETRY Setup Failed.\n");

	return ret;
}

static int telemetry_pltdrv_remove(struct platform_device *pdev)
{
	telemetry_clear_pltdata();
	return 0;
}

static struct platform_driver telemetry_soc_driver = {
	.probe		= telemetry_pltdrv_probe,
	.remove		= telemetry_pltdrv_remove,
	.driver		= {
		.name	= DRIVER_NAME,
	},
};

static int __init telemetry_module_init(void)
{
	return platform_driver_register(&telemetry_soc_driver);
}

static void __exit telemetry_module_exit(void)
{
	platform_driver_unregister(&telemetry_soc_driver);
}

device_initcall(telemetry_module_init);
module_exit(telemetry_module_exit);

MODULE_AUTHOR("Souvik Kumar Chakravarty <souvik.k.chakravarty@intel.com>");
MODULE_DESCRIPTION("Intel SoC Telemetry Platform Driver");
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL v2");

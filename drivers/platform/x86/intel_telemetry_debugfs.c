/*
 * Intel SOC Telemetry debugfs Driver: Currently supports APL
 * Copyright (c) 2015, Intel Corporation.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * This file provides the debugfs interfaces for telemetry.
 * /sys/kernel/debug/telemetry/pss_info: Shows Primary Control Sub-Sys Counters
 * /sys/kernel/debug/telemetry/ioss_info: Shows IO Sub-System Counters
 * /sys/kernel/debug/telemetry/soc_states: Shows SoC State
 * /sys/kernel/debug/telemetry/pss_trace_verbosity: Read and Change Tracing
 *				Verbosity via firmware
 * /sys/kernel/debug/telemetry/ioss_race_verbosity: Write and Change Tracing
 *				Verbosity via firmware
 */
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/seq_file.h>
#include <linux/suspend.h>

#include <asm/cpu_device_id.h>
#include <asm/intel-family.h>
#include <asm/intel_pmc_ipc.h>
#include <asm/intel_telemetry.h>

#define DRIVER_NAME			"telemetry_soc_debugfs"
#define DRIVER_VERSION			"1.0.0"

/* ApolloLake SoC Event-IDs */
#define TELEM_APL_PSS_PSTATES_ID	0x2802
#define TELEM_APL_PSS_IDLE_ID		0x2806
#define TELEM_APL_PCS_IDLE_BLOCKED_ID	0x2C00
#define TELEM_APL_PCS_S0IX_BLOCKED_ID	0x2C01
#define TELEM_APL_PSS_WAKEUP_ID		0x2C02
#define TELEM_APL_PSS_LTR_BLOCKING_ID	0x2C03

#define TELEM_APL_S0IX_TOTAL_OCC_ID	0x4000
#define TELEM_APL_S0IX_SHLW_OCC_ID	0x4001
#define TELEM_APL_S0IX_DEEP_OCC_ID	0x4002
#define TELEM_APL_S0IX_TOTAL_RES_ID	0x4800
#define TELEM_APL_S0IX_SHLW_RES_ID	0x4801
#define TELEM_APL_S0IX_DEEP_RES_ID	0x4802
#define TELEM_APL_D0IX_ID		0x581A
#define TELEM_APL_D3_ID			0x5819
#define TELEM_APL_PG_ID			0x5818

#define TELEM_INFO_SRAMEVTS_MASK	0xFF00
#define TELEM_INFO_SRAMEVTS_SHIFT	0x8
#define TELEM_SSRAM_READ_TIMEOUT	10

#define TELEM_MASK_BIT			1
#define TELEM_MASK_BYTE			0xFF
#define BYTES_PER_LONG			8
#define TELEM_APL_MASK_PCS_STATE	0xF

/* Max events in bitmap to check for */
#define TELEM_PSS_IDLE_EVTS		25
#define TELEM_PSS_IDLE_BLOCKED_EVTS	20
#define TELEM_PSS_S0IX_BLOCKED_EVTS	20
#define TELEM_PSS_S0IX_WAKEUP_EVTS	20
#define TELEM_PSS_LTR_BLOCKING_EVTS	20
#define TELEM_IOSS_DX_D0IX_EVTS		25
#define TELEM_IOSS_PG_EVTS		30

#define TELEM_DEBUGFS_CPU(model, data) \
	{ X86_VENDOR_INTEL, 6, model, X86_FEATURE_ANY, (unsigned long)&data}

#define TELEM_CHECK_AND_PARSE_EVTS(EVTID, EVTNUM, BUF, EVTLOG, EVTDAT, MASK) { \
	if (evtlog[index].telem_evtid == (EVTID)) { \
		for (idx = 0; idx < (EVTNUM); idx++) \
			(BUF)[idx] = ((EVTLOG) >> (EVTDAT)[idx].bit_pos) & \
				     (MASK); \
	continue; \
	} \
}

#define TELEM_CHECK_AND_PARSE_CTRS(EVTID, CTR) { \
	if (evtlog[index].telem_evtid == (EVTID)) { \
		(CTR) = evtlog[index].telem_evtlog; \
		continue; \
	} \
}

static u8 suspend_prep_ok;
static u32 suspend_shlw_ctr_temp, suspend_deep_ctr_temp;
static u64 suspend_shlw_res_temp, suspend_deep_res_temp;

struct telemetry_susp_stats {
	u32 shlw_ctr;
	u32 deep_ctr;
	u64 shlw_res;
	u64 deep_res;
};

/* Bitmap definitions for default counters in APL */
struct telem_pss_idle_stateinfo {
	const char *name;
	u32 bit_pos;
};

static struct telem_pss_idle_stateinfo telem_apl_pss_idle_data[] = {
	{"IA_CORE0_C1E",		0},
	{"IA_CORE1_C1E",		1},
	{"IA_CORE2_C1E",		2},
	{"IA_CORE3_C1E",		3},
	{"IA_CORE0_C6",			16},
	{"IA_CORE1_C6",			17},
	{"IA_CORE2_C6",			18},
	{"IA_CORE3_C6",			19},
	{"IA_MODULE0_C7",		32},
	{"IA_MODULE1_C7",		33},
	{"GT_RC6",			40},
	{"IUNIT_PROCESSING_IDLE",	41},
	{"FAR_MEM_IDLE",		43},
	{"DISPLAY_IDLE",		44},
	{"IUNIT_INPUT_SYSTEM_IDLE",	45},
	{"PCS_STATUS",			60},
};

struct telem_pcs_blkd_info {
	const char *name;
	u32 bit_pos;
};

static struct telem_pcs_blkd_info telem_apl_pcs_idle_blkd_data[] = {
	{"COMPUTE",			0},
	{"MISC",			8},
	{"MODULE_ACTIONS_PENDING",	16},
	{"LTR",				24},
	{"DISPLAY_WAKE",		32},
	{"ISP_WAKE",			40},
	{"PSF0_ACTIVE",			48},
};

static struct telem_pcs_blkd_info telem_apl_pcs_s0ix_blkd_data[] = {
	{"LTR",				0},
	{"IRTL",			8},
	{"WAKE_DEADLINE_PENDING",	16},
	{"DISPLAY",			24},
	{"ISP",				32},
	{"CORE",			40},
	{"PMC",				48},
	{"MISC",			56},
};

struct telem_pss_ltr_info {
	const char *name;
	u32 bit_pos;
};

static struct telem_pss_ltr_info telem_apl_pss_ltr_data[] = {
	{"CORE_ACTIVE",		0},
	{"MEM_UP",		8},
	{"DFX",			16},
	{"DFX_FORCE_LTR",	24},
	{"DISPLAY",		32},
	{"ISP",			40},
	{"SOUTH",		48},
};

struct telem_pss_wakeup_info {
	const char *name;
	u32 bit_pos;
};

static struct telem_pss_wakeup_info telem_apl_pss_wakeup[] = {
	{"IP_IDLE",			0},
	{"DISPLAY_WAKE",		8},
	{"VOLTAGE_REG_INT",		16},
	{"DROWSY_TIMER (HOTPLUG)",	24},
	{"CORE_WAKE",			32},
	{"MISC_S0IX",			40},
	{"MISC_ABORT",			56},
};

struct telem_ioss_d0ix_stateinfo {
	const char *name;
	u32 bit_pos;
};

static struct telem_ioss_d0ix_stateinfo telem_apl_ioss_d0ix_data[] = {
	{"CSE",		0},
	{"SCC2",	1},
	{"GMM",		2},
	{"XDCI",	3},
	{"XHCI",	4},
	{"ISH",		5},
	{"AVS",		6},
	{"PCIE0P1",	7},
	{"PECI0P0",	8},
	{"LPSS",	9},
	{"SCC",		10},
	{"PWM",		11},
	{"PCIE1_P3",    12},
	{"PCIE1_P2",    13},
	{"PCIE1_P1",    14},
	{"PCIE1_P0",    15},
	{"CNV",		16},
	{"SATA",	17},
	{"PRTC",	18},
};

struct telem_ioss_pg_info {
	const char *name;
	u32 bit_pos;
};

static struct telem_ioss_pg_info telem_apl_ioss_pg_data[] = {
	{"LPSS",	0},
	{"SCC",		1},
	{"P2SB",	2},
	{"SCC2",	3},
	{"GMM",		4},
	{"PCIE0",	5},
	{"XDCI",	6},
	{"xHCI",	7},
	{"CSE",		8},
	{"SPI",		9},
	{"AVSPGD4",	10},
	{"AVSPGD3",	11},
	{"AVSPGD2",	12},
	{"AVSPGD1",	13},
	{"ISH",		14},
	{"EXI",		15},
	{"NPKVRC",	16},
	{"NPKVNN",	17},
	{"CUNIT",	18},
	{"FUSE_CTRL",	19},
	{"PCIE1",	20},
	{"CNV",		21},
	{"LPC",		22},
	{"SATA",	23},
	{"SMB",		24},
	{"PRTC",	25},
};

struct telemetry_debugfs_conf {
	struct telemetry_susp_stats suspend_stats;
	struct dentry *telemetry_dbg_dir;

	/* Bitmap Data */
	struct telem_ioss_d0ix_stateinfo *ioss_d0ix_data;
	struct telem_pss_idle_stateinfo *pss_idle_data;
	struct telem_pcs_blkd_info *pcs_idle_blkd_data;
	struct telem_pcs_blkd_info *pcs_s0ix_blkd_data;
	struct telem_pss_wakeup_info *pss_wakeup;
	struct telem_pss_ltr_info *pss_ltr_data;
	struct telem_ioss_pg_info *ioss_pg_data;
	u8 pcs_idle_blkd_evts;
	u8 pcs_s0ix_blkd_evts;
	u8 pss_wakeup_evts;
	u8 pss_idle_evts;
	u8 pss_ltr_evts;
	u8 ioss_d0ix_evts;
	u8 ioss_pg_evts;

	/* IDs */
	u16  pss_ltr_blocking_id;
	u16  pcs_idle_blkd_id;
	u16  pcs_s0ix_blkd_id;
	u16  s0ix_total_occ_id;
	u16  s0ix_shlw_occ_id;
	u16  s0ix_deep_occ_id;
	u16  s0ix_total_res_id;
	u16  s0ix_shlw_res_id;
	u16  s0ix_deep_res_id;
	u16  pss_wakeup_id;
	u16  ioss_d0ix_id;
	u16  pstates_id;
	u16  pss_idle_id;
	u16  ioss_d3_id;
	u16  ioss_pg_id;
};

static struct telemetry_debugfs_conf *debugfs_conf;

static struct telemetry_debugfs_conf telem_apl_debugfs_conf = {
	.pss_idle_data = telem_apl_pss_idle_data,
	.pcs_idle_blkd_data = telem_apl_pcs_idle_blkd_data,
	.pcs_s0ix_blkd_data = telem_apl_pcs_s0ix_blkd_data,
	.pss_ltr_data = telem_apl_pss_ltr_data,
	.pss_wakeup = telem_apl_pss_wakeup,
	.ioss_d0ix_data = telem_apl_ioss_d0ix_data,
	.ioss_pg_data = telem_apl_ioss_pg_data,

	.pss_idle_evts = ARRAY_SIZE(telem_apl_pss_idle_data),
	.pcs_idle_blkd_evts = ARRAY_SIZE(telem_apl_pcs_idle_blkd_data),
	.pcs_s0ix_blkd_evts = ARRAY_SIZE(telem_apl_pcs_s0ix_blkd_data),
	.pss_ltr_evts = ARRAY_SIZE(telem_apl_pss_ltr_data),
	.pss_wakeup_evts = ARRAY_SIZE(telem_apl_pss_wakeup),
	.ioss_d0ix_evts = ARRAY_SIZE(telem_apl_ioss_d0ix_data),
	.ioss_pg_evts = ARRAY_SIZE(telem_apl_ioss_pg_data),

	.pstates_id = TELEM_APL_PSS_PSTATES_ID,
	.pss_idle_id = TELEM_APL_PSS_IDLE_ID,
	.pcs_idle_blkd_id = TELEM_APL_PCS_IDLE_BLOCKED_ID,
	.pcs_s0ix_blkd_id = TELEM_APL_PCS_S0IX_BLOCKED_ID,
	.pss_wakeup_id = TELEM_APL_PSS_WAKEUP_ID,
	.pss_ltr_blocking_id = TELEM_APL_PSS_LTR_BLOCKING_ID,
	.s0ix_total_occ_id = TELEM_APL_S0IX_TOTAL_OCC_ID,
	.s0ix_shlw_occ_id = TELEM_APL_S0IX_SHLW_OCC_ID,
	.s0ix_deep_occ_id = TELEM_APL_S0IX_DEEP_OCC_ID,
	.s0ix_total_res_id = TELEM_APL_S0IX_TOTAL_RES_ID,
	.s0ix_shlw_res_id = TELEM_APL_S0IX_SHLW_RES_ID,
	.s0ix_deep_res_id = TELEM_APL_S0IX_DEEP_RES_ID,
	.ioss_d0ix_id = TELEM_APL_D0IX_ID,
	.ioss_d3_id = TELEM_APL_D3_ID,
	.ioss_pg_id = TELEM_APL_PG_ID,
};

static const struct x86_cpu_id telemetry_debugfs_cpu_ids[] = {
	TELEM_DEBUGFS_CPU(INTEL_FAM6_ATOM_GOLDMONT, telem_apl_debugfs_conf),
	TELEM_DEBUGFS_CPU(INTEL_FAM6_ATOM_GOLDMONT_PLUS, telem_apl_debugfs_conf),
	{}
};

MODULE_DEVICE_TABLE(x86cpu, telemetry_debugfs_cpu_ids);

static int telemetry_debugfs_check_evts(void)
{
	if ((debugfs_conf->pss_idle_evts > TELEM_PSS_IDLE_EVTS) ||
	    (debugfs_conf->pcs_idle_blkd_evts > TELEM_PSS_IDLE_BLOCKED_EVTS) ||
	    (debugfs_conf->pcs_s0ix_blkd_evts > TELEM_PSS_S0IX_BLOCKED_EVTS) ||
	    (debugfs_conf->pss_ltr_evts > TELEM_PSS_LTR_BLOCKING_EVTS) ||
	    (debugfs_conf->pss_wakeup_evts > TELEM_PSS_S0IX_WAKEUP_EVTS) ||
	    (debugfs_conf->ioss_d0ix_evts > TELEM_IOSS_DX_D0IX_EVTS) ||
	    (debugfs_conf->ioss_pg_evts > TELEM_IOSS_PG_EVTS))
		return -EINVAL;

	return 0;
}

static int telem_pss_states_show(struct seq_file *s, void *unused)
{
	struct telemetry_evtlog evtlog[TELEM_MAX_OS_ALLOCATED_EVENTS];
	struct telemetry_debugfs_conf *conf = debugfs_conf;
	const char *name[TELEM_MAX_OS_ALLOCATED_EVENTS];
	u32 pcs_idle_blkd[TELEM_PSS_IDLE_BLOCKED_EVTS],
	    pcs_s0ix_blkd[TELEM_PSS_S0IX_BLOCKED_EVTS],
	    pss_s0ix_wakeup[TELEM_PSS_S0IX_WAKEUP_EVTS],
	    pss_ltr_blkd[TELEM_PSS_LTR_BLOCKING_EVTS],
	    pss_idle[TELEM_PSS_IDLE_EVTS];
	int index, idx, ret, err = 0;
	u64 pstates = 0;

	ret = telemetry_read_eventlog(TELEM_PSS, evtlog,
				      TELEM_MAX_OS_ALLOCATED_EVENTS);
	if (ret < 0)
		return ret;

	err = telemetry_get_evtname(TELEM_PSS, name,
				    TELEM_MAX_OS_ALLOCATED_EVENTS);
	if (err < 0)
		return err;

	seq_puts(s, "\n----------------------------------------------------\n");
	seq_puts(s, "\tPSS TELEM EVENTLOG (Residency = field/19.2 us\n");
	seq_puts(s, "----------------------------------------------------\n");
	for (index = 0; index < ret; index++) {
		seq_printf(s, "%-32s %llu\n",
			   name[index], evtlog[index].telem_evtlog);

		/* Fetch PSS IDLE State */
		if (evtlog[index].telem_evtid == conf->pss_idle_id) {
			pss_idle[conf->pss_idle_evts - 1] =
			(evtlog[index].telem_evtlog >>
			conf->pss_idle_data[conf->pss_idle_evts - 1].bit_pos) &
			TELEM_APL_MASK_PCS_STATE;
		}

		TELEM_CHECK_AND_PARSE_EVTS(conf->pss_idle_id,
					   conf->pss_idle_evts - 1,
					   pss_idle, evtlog[index].telem_evtlog,
					   conf->pss_idle_data, TELEM_MASK_BIT);

		TELEM_CHECK_AND_PARSE_EVTS(conf->pcs_idle_blkd_id,
					   conf->pcs_idle_blkd_evts,
					   pcs_idle_blkd,
					   evtlog[index].telem_evtlog,
					   conf->pcs_idle_blkd_data,
					   TELEM_MASK_BYTE);

		TELEM_CHECK_AND_PARSE_EVTS(conf->pcs_s0ix_blkd_id,
					   conf->pcs_s0ix_blkd_evts,
					   pcs_s0ix_blkd,
					   evtlog[index].telem_evtlog,
					   conf->pcs_s0ix_blkd_data,
					   TELEM_MASK_BYTE);

		TELEM_CHECK_AND_PARSE_EVTS(conf->pss_wakeup_id,
					   conf->pss_wakeup_evts,
					   pss_s0ix_wakeup,
					   evtlog[index].telem_evtlog,
					   conf->pss_wakeup, TELEM_MASK_BYTE);

		TELEM_CHECK_AND_PARSE_EVTS(conf->pss_ltr_blocking_id,
					   conf->pss_ltr_evts, pss_ltr_blkd,
					   evtlog[index].telem_evtlog,
					   conf->pss_ltr_data, TELEM_MASK_BYTE);

		if (evtlog[index].telem_evtid == debugfs_conf->pstates_id)
			pstates = evtlog[index].telem_evtlog;
	}

	seq_puts(s, "\n--------------------------------------\n");
	seq_puts(s, "PStates\n");
	seq_puts(s, "--------------------------------------\n");
	seq_puts(s, "Domain\t\t\t\tFreq(Mhz)\n");
	seq_printf(s, " IA\t\t\t\t %llu\n GT\t\t\t\t %llu\n",
		   (pstates & TELEM_MASK_BYTE)*100,
		   ((pstates >> 8) & TELEM_MASK_BYTE)*50/3);

	seq_printf(s, " IUNIT\t\t\t\t %llu\n SA\t\t\t\t %llu\n",
		   ((pstates >> 16) & TELEM_MASK_BYTE)*25,
		   ((pstates >> 24) & TELEM_MASK_BYTE)*50/3);

	seq_puts(s, "\n--------------------------------------\n");
	seq_puts(s, "PSS IDLE Status\n");
	seq_puts(s, "--------------------------------------\n");
	seq_puts(s, "Device\t\t\t\t\tIDLE\n");
	for (index = 0; index < debugfs_conf->pss_idle_evts; index++) {
		seq_printf(s, "%-32s\t%u\n",
			   debugfs_conf->pss_idle_data[index].name,
			   pss_idle[index]);
	}

	seq_puts(s, "\n--------------------------------------\n");
	seq_puts(s, "PSS Idle blkd Status (~1ms saturating bucket)\n");
	seq_puts(s, "--------------------------------------\n");
	seq_puts(s, "Blocker\t\t\t\t\tCount\n");
	for (index = 0; index < debugfs_conf->pcs_idle_blkd_evts; index++) {
		seq_printf(s, "%-32s\t%u\n",
			   debugfs_conf->pcs_idle_blkd_data[index].name,
			   pcs_idle_blkd[index]);
	}

	seq_puts(s, "\n--------------------------------------\n");
	seq_puts(s, "PSS S0ix blkd Status (~1ms saturating bucket)\n");
	seq_puts(s, "--------------------------------------\n");
	seq_puts(s, "Blocker\t\t\t\t\tCount\n");
	for (index = 0; index < debugfs_conf->pcs_s0ix_blkd_evts; index++) {
		seq_printf(s, "%-32s\t%u\n",
			   debugfs_conf->pcs_s0ix_blkd_data[index].name,
			   pcs_s0ix_blkd[index]);
	}

	seq_puts(s, "\n--------------------------------------\n");
	seq_puts(s, "LTR Blocking Status (~1ms saturating bucket)\n");
	seq_puts(s, "--------------------------------------\n");
	seq_puts(s, "Blocker\t\t\t\t\tCount\n");
	for (index = 0; index < debugfs_conf->pss_ltr_evts; index++) {
		seq_printf(s, "%-32s\t%u\n",
			   debugfs_conf->pss_ltr_data[index].name,
			   pss_s0ix_wakeup[index]);
	}

	seq_puts(s, "\n--------------------------------------\n");
	seq_puts(s, "Wakes Status (~1ms saturating bucket)\n");
	seq_puts(s, "--------------------------------------\n");
	seq_puts(s, "Wakes\t\t\t\t\tCount\n");
	for (index = 0; index < debugfs_conf->pss_wakeup_evts; index++) {
		seq_printf(s, "%-32s\t%u\n",
			   debugfs_conf->pss_wakeup[index].name,
			   pss_ltr_blkd[index]);
	}

	return 0;
}

static int telem_pss_state_open(struct inode *inode, struct file *file)
{
	return single_open(file, telem_pss_states_show, inode->i_private);
}

static const struct file_operations telem_pss_ops = {
	.open		= telem_pss_state_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int telem_ioss_states_show(struct seq_file *s, void *unused)
{
	struct telemetry_evtlog evtlog[TELEM_MAX_OS_ALLOCATED_EVENTS];
	const char *name[TELEM_MAX_OS_ALLOCATED_EVENTS];
	int index, ret, err;

	ret = telemetry_read_eventlog(TELEM_IOSS, evtlog,
				      TELEM_MAX_OS_ALLOCATED_EVENTS);
	if (ret < 0)
		return ret;

	err = telemetry_get_evtname(TELEM_IOSS, name,
				    TELEM_MAX_OS_ALLOCATED_EVENTS);
	if (err < 0)
		return err;

	seq_puts(s, "--------------------------------------\n");
	seq_puts(s, "\tI0SS TELEMETRY EVENTLOG\n");
	seq_puts(s, "--------------------------------------\n");
	for (index = 0; index < ret; index++) {
		seq_printf(s, "%-32s 0x%llx\n",
			   name[index], evtlog[index].telem_evtlog);
	}

	return 0;
}

static int telem_ioss_state_open(struct inode *inode, struct file *file)
{
	return single_open(file, telem_ioss_states_show, inode->i_private);
}

static const struct file_operations telem_ioss_ops = {
	.open		= telem_ioss_state_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int telem_soc_states_show(struct seq_file *s, void *unused)
{
	u32 d3_sts[TELEM_IOSS_DX_D0IX_EVTS], d0ix_sts[TELEM_IOSS_DX_D0IX_EVTS];
	u32 pg_sts[TELEM_IOSS_PG_EVTS], pss_idle[TELEM_PSS_IDLE_EVTS];
	struct telemetry_evtlog evtlog[TELEM_MAX_OS_ALLOCATED_EVENTS];
	u32 s0ix_total_ctr = 0, s0ix_shlw_ctr = 0, s0ix_deep_ctr = 0;
	u64 s0ix_total_res = 0, s0ix_shlw_res = 0, s0ix_deep_res = 0;
	struct telemetry_debugfs_conf *conf = debugfs_conf;
	struct pci_dev *dev = NULL;
	int index, idx, ret;
	u32 d3_state;
	u16 pmcsr;

	ret = telemetry_read_eventlog(TELEM_IOSS, evtlog,
				      TELEM_MAX_OS_ALLOCATED_EVENTS);
	if (ret < 0)
		return ret;

	for (index = 0; index < ret; index++) {
		TELEM_CHECK_AND_PARSE_EVTS(conf->ioss_d3_id,
					   conf->ioss_d0ix_evts,
					   d3_sts, evtlog[index].telem_evtlog,
					   conf->ioss_d0ix_data,
					   TELEM_MASK_BIT);

		TELEM_CHECK_AND_PARSE_EVTS(conf->ioss_pg_id, conf->ioss_pg_evts,
					   pg_sts, evtlog[index].telem_evtlog,
					   conf->ioss_pg_data, TELEM_MASK_BIT);

		TELEM_CHECK_AND_PARSE_EVTS(conf->ioss_d0ix_id,
					   conf->ioss_d0ix_evts,
					   d0ix_sts, evtlog[index].telem_evtlog,
					   conf->ioss_d0ix_data,
					   TELEM_MASK_BIT);

		TELEM_CHECK_AND_PARSE_CTRS(conf->s0ix_total_occ_id,
					   s0ix_total_ctr);

		TELEM_CHECK_AND_PARSE_CTRS(conf->s0ix_shlw_occ_id,
					   s0ix_shlw_ctr);

		TELEM_CHECK_AND_PARSE_CTRS(conf->s0ix_deep_occ_id,
					   s0ix_deep_ctr);

		TELEM_CHECK_AND_PARSE_CTRS(conf->s0ix_total_res_id,
					   s0ix_total_res);

		TELEM_CHECK_AND_PARSE_CTRS(conf->s0ix_shlw_res_id,
					   s0ix_shlw_res);

		TELEM_CHECK_AND_PARSE_CTRS(conf->s0ix_deep_res_id,
					   s0ix_deep_res);
	}

	seq_puts(s, "\n---------------------------------------------------\n");
	seq_puts(s, "S0IX Type\t\t\t Occurrence\t\t Residency(us)\n");
	seq_puts(s, "---------------------------------------------------\n");

	seq_printf(s, "S0IX Shallow\t\t\t %10u\t %10llu\n",
		   s0ix_shlw_ctr -
		   conf->suspend_stats.shlw_ctr,
		   (u64)((s0ix_shlw_res -
		   conf->suspend_stats.shlw_res)*10/192));

	seq_printf(s, "S0IX Deep\t\t\t %10u\t %10llu\n",
		   s0ix_deep_ctr -
		   conf->suspend_stats.deep_ctr,
		   (u64)((s0ix_deep_res -
		   conf->suspend_stats.deep_res)*10/192));

	seq_printf(s, "Suspend(With S0ixShallow)\t %10u\t %10llu\n",
		   conf->suspend_stats.shlw_ctr,
		   (u64)(conf->suspend_stats.shlw_res*10)/192);

	seq_printf(s, "Suspend(With S0ixDeep)\t\t %10u\t %10llu\n",
		   conf->suspend_stats.deep_ctr,
		   (u64)(conf->suspend_stats.deep_res*10)/192);

	seq_printf(s, "TOTAL S0IX\t\t\t %10u\t %10llu\n", s0ix_total_ctr,
		   (u64)(s0ix_total_res*10/192));
	seq_puts(s, "\n-------------------------------------------------\n");
	seq_puts(s, "\t\tDEVICE STATES\n");
	seq_puts(s, "-------------------------------------------------\n");

	for_each_pci_dev(dev) {
		pci_read_config_word(dev, dev->pm_cap + PCI_PM_CTRL, &pmcsr);
		d3_state = ((pmcsr & PCI_PM_CTRL_STATE_MASK) ==
			    (__force int)PCI_D3hot) ? 1 : 0;

		seq_printf(s, "pci %04x %04X %s %20.20s: ",
			   dev->vendor, dev->device, dev_name(&dev->dev),
			   dev_driver_string(&dev->dev));
		seq_printf(s, " d3:%x\n", d3_state);
	}

	seq_puts(s, "\n--------------------------------------\n");
	seq_puts(s, "D3/D0i3 Status\n");
	seq_puts(s, "--------------------------------------\n");
	seq_puts(s, "Block\t\t D3\t D0i3\n");
	for (index = 0; index < conf->ioss_d0ix_evts; index++) {
		seq_printf(s, "%-10s\t %u\t %u\n",
			   conf->ioss_d0ix_data[index].name,
			   d3_sts[index], d0ix_sts[index]);
	}

	seq_puts(s, "\n--------------------------------------\n");
	seq_puts(s, "South Complex PowerGate Status\n");
	seq_puts(s, "--------------------------------------\n");
	seq_puts(s, "Device\t\t PG\n");
	for (index = 0; index < conf->ioss_pg_evts; index++) {
		seq_printf(s, "%-10s\t %u\n",
			   conf->ioss_pg_data[index].name,
			   pg_sts[index]);
	}

	evtlog->telem_evtid = conf->pss_idle_id;
	ret = telemetry_read_events(TELEM_PSS, evtlog, 1);
	if (ret < 0)
		return ret;

	seq_puts(s, "\n-----------------------------------------\n");
	seq_puts(s, "North Idle Status\n");
	seq_puts(s, "-----------------------------------------\n");
	for (idx = 0; idx < conf->pss_idle_evts - 1; idx++) {
		pss_idle[idx] =	(evtlog->telem_evtlog >>
				conf->pss_idle_data[idx].bit_pos) &
				TELEM_MASK_BIT;
	}

	pss_idle[idx] = (evtlog->telem_evtlog >>
			conf->pss_idle_data[idx].bit_pos) &
			TELEM_APL_MASK_PCS_STATE;

	for (index = 0; index < conf->pss_idle_evts; index++) {
		seq_printf(s, "%-30s %u\n",
			   conf->pss_idle_data[index].name,
			   pss_idle[index]);
	}

	seq_puts(s, "\nPCS_STATUS Code\n");
	seq_puts(s, "0:C0 1:C1 2:C1_DN_WT_DEV 3:C2 4:C2_WT_DE_MEM_UP\n");
	seq_puts(s, "5:C2_WT_DE_MEM_DOWN 6:C2_UP_WT_DEV 7:C2_DN 8:C2_VOA\n");
	seq_puts(s, "9:C2_VOA_UP 10:S0IX_PRE 11:S0IX\n");

	return 0;
}

static int telem_soc_state_open(struct inode *inode, struct file *file)
{
	return single_open(file, telem_soc_states_show, inode->i_private);
}

static const struct file_operations telem_socstate_ops = {
	.open		= telem_soc_state_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int telem_s0ix_res_get(void *data, u64 *val)
{
	u64 s0ix_total_res;
	int ret;

	ret = intel_pmc_s0ix_counter_read(&s0ix_total_res);
	if (ret) {
		pr_err("Failed to read S0ix residency");
		return ret;
	}

	*val = s0ix_total_res;

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(telem_s0ix_fops, telem_s0ix_res_get, NULL, "%llu\n");

static int telem_pss_trc_verb_show(struct seq_file *s, void *unused)
{
	u32 verbosity;
	int err;

	err = telemetry_get_trace_verbosity(TELEM_PSS, &verbosity);
	if (err) {
		pr_err("Get PSS Trace Verbosity Failed with Error %d\n", err);
		return -EFAULT;
	}

	seq_printf(s, "PSS Trace Verbosity %u\n", verbosity);
	return 0;
}

static ssize_t telem_pss_trc_verb_write(struct file *file,
					const char __user *userbuf,
					size_t count, loff_t *ppos)
{
	u32 verbosity;
	int err;

	if (kstrtou32_from_user(userbuf, count, 0, &verbosity))
		return -EFAULT;

	err = telemetry_set_trace_verbosity(TELEM_PSS, verbosity);
	if (err) {
		pr_err("Changing PSS Trace Verbosity Failed. Error %d\n", err);
		count = err;
	}

	return count;
}

static int telem_pss_trc_verb_open(struct inode *inode, struct file *file)
{
	return single_open(file, telem_pss_trc_verb_show, inode->i_private);
}

static const struct file_operations telem_pss_trc_verb_ops = {
	.open		= telem_pss_trc_verb_open,
	.read		= seq_read,
	.write		= telem_pss_trc_verb_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int telem_ioss_trc_verb_show(struct seq_file *s, void *unused)
{
	u32 verbosity;
	int err;

	err = telemetry_get_trace_verbosity(TELEM_IOSS, &verbosity);
	if (err) {
		pr_err("Get IOSS Trace Verbosity Failed with Error %d\n", err);
		return -EFAULT;
	}

	seq_printf(s, "IOSS Trace Verbosity %u\n", verbosity);
	return 0;
}

static ssize_t telem_ioss_trc_verb_write(struct file *file,
					 const char __user *userbuf,
					 size_t count, loff_t *ppos)
{
	u32 verbosity;
	int err;

	if (kstrtou32_from_user(userbuf, count, 0, &verbosity))
		return -EFAULT;

	err = telemetry_set_trace_verbosity(TELEM_IOSS, verbosity);
	if (err) {
		pr_err("Changing IOSS Trace Verbosity Failed. Error %d\n", err);
		count = err;
	}

	return count;
}

static int telem_ioss_trc_verb_open(struct inode *inode, struct file *file)
{
	return single_open(file, telem_ioss_trc_verb_show, inode->i_private);
}

static const struct file_operations telem_ioss_trc_verb_ops = {
	.open		= telem_ioss_trc_verb_open,
	.read		= seq_read,
	.write		= telem_ioss_trc_verb_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int pm_suspend_prep_cb(void)
{
	struct telemetry_evtlog evtlog[TELEM_MAX_OS_ALLOCATED_EVENTS];
	struct telemetry_debugfs_conf *conf = debugfs_conf;
	int ret, index;

	ret = telemetry_raw_read_eventlog(TELEM_IOSS, evtlog,
			TELEM_MAX_OS_ALLOCATED_EVENTS);
	if (ret < 0) {
		suspend_prep_ok = 0;
		goto out;
	}

	for (index = 0; index < ret; index++) {

		TELEM_CHECK_AND_PARSE_CTRS(conf->s0ix_shlw_occ_id,
					   suspend_shlw_ctr_temp);

		TELEM_CHECK_AND_PARSE_CTRS(conf->s0ix_deep_occ_id,
					   suspend_deep_ctr_temp);

		TELEM_CHECK_AND_PARSE_CTRS(conf->s0ix_shlw_res_id,
					   suspend_shlw_res_temp);

		TELEM_CHECK_AND_PARSE_CTRS(conf->s0ix_deep_res_id,
					   suspend_deep_res_temp);
	}
	suspend_prep_ok = 1;
out:
	return NOTIFY_OK;
}

static int pm_suspend_exit_cb(void)
{
	struct telemetry_evtlog evtlog[TELEM_MAX_OS_ALLOCATED_EVENTS];
	static u32 suspend_shlw_ctr_exit, suspend_deep_ctr_exit;
	static u64 suspend_shlw_res_exit, suspend_deep_res_exit;
	struct telemetry_debugfs_conf *conf = debugfs_conf;
	int ret, index;

	if (!suspend_prep_ok)
		goto out;

	ret = telemetry_raw_read_eventlog(TELEM_IOSS, evtlog,
					  TELEM_MAX_OS_ALLOCATED_EVENTS);
	if (ret < 0)
		goto out;

	for (index = 0; index < ret; index++) {
		TELEM_CHECK_AND_PARSE_CTRS(conf->s0ix_shlw_occ_id,
					   suspend_shlw_ctr_exit);

		TELEM_CHECK_AND_PARSE_CTRS(conf->s0ix_deep_occ_id,
					   suspend_deep_ctr_exit);

		TELEM_CHECK_AND_PARSE_CTRS(conf->s0ix_shlw_res_id,
					   suspend_shlw_res_exit);

		TELEM_CHECK_AND_PARSE_CTRS(conf->s0ix_deep_res_id,
					   suspend_deep_res_exit);
	}

	if ((suspend_shlw_ctr_exit < suspend_shlw_ctr_temp) ||
	    (suspend_deep_ctr_exit < suspend_deep_ctr_temp) ||
	    (suspend_shlw_res_exit < suspend_shlw_res_temp) ||
	    (suspend_deep_res_exit < suspend_deep_res_temp)) {
		pr_err("Wrong s0ix counters detected\n");
		goto out;
	}

	/*
	 * Due to some design limitations in the firmware, sometimes the
	 * counters do not get updated by the time we reach here. As a
	 * workaround, we try to see if this was a genuine case of sleep
	 * failure or not by cross-checking from PMC GCR registers directly.
	 */
	if (suspend_shlw_ctr_exit == suspend_shlw_ctr_temp &&
	    suspend_deep_ctr_exit == suspend_deep_ctr_temp) {
		ret = intel_pmc_gcr_read64(PMC_GCR_TELEM_SHLW_S0IX_REG,
					  &suspend_shlw_res_exit);
		if (ret < 0)
			goto out;

		ret = intel_pmc_gcr_read64(PMC_GCR_TELEM_DEEP_S0IX_REG,
					  &suspend_deep_res_exit);
		if (ret < 0)
			goto out;

		if (suspend_shlw_res_exit > suspend_shlw_res_temp)
			suspend_shlw_ctr_exit++;

		if (suspend_deep_res_exit > suspend_deep_res_temp)
			suspend_deep_ctr_exit++;
	}

	suspend_shlw_ctr_exit -= suspend_shlw_ctr_temp;
	suspend_deep_ctr_exit -= suspend_deep_ctr_temp;
	suspend_shlw_res_exit -= suspend_shlw_res_temp;
	suspend_deep_res_exit -= suspend_deep_res_temp;

	if (suspend_shlw_ctr_exit != 0) {
		conf->suspend_stats.shlw_ctr +=
		suspend_shlw_ctr_exit;

		conf->suspend_stats.shlw_res +=
		suspend_shlw_res_exit;
	}

	if (suspend_deep_ctr_exit != 0) {
		conf->suspend_stats.deep_ctr +=
		suspend_deep_ctr_exit;

		conf->suspend_stats.deep_res +=
		suspend_deep_res_exit;
	}

out:
	suspend_prep_ok = 0;
	return NOTIFY_OK;
}

static int pm_notification(struct notifier_block *this,
			   unsigned long event, void *ptr)
{
	switch (event) {
	case PM_SUSPEND_PREPARE:
		return pm_suspend_prep_cb();
	case PM_POST_SUSPEND:
		return pm_suspend_exit_cb();
	}

	return NOTIFY_DONE;
}

static struct notifier_block pm_notifier = {
	.notifier_call = pm_notification,
};

static int __init telemetry_debugfs_init(void)
{
	const struct x86_cpu_id *id;
	int err;
	struct dentry *f;

	/* Only APL supported for now */
	id = x86_match_cpu(telemetry_debugfs_cpu_ids);
	if (!id)
		return -ENODEV;

	debugfs_conf = (struct telemetry_debugfs_conf *)id->driver_data;

	err = telemetry_pltconfig_valid();
	if (err < 0)
		return -ENODEV;

	err = telemetry_debugfs_check_evts();
	if (err < 0)
		return -EINVAL;

	register_pm_notifier(&pm_notifier);

	err = -ENOMEM;
	debugfs_conf->telemetry_dbg_dir = debugfs_create_dir("telemetry", NULL);
	if (!debugfs_conf->telemetry_dbg_dir)
		goto out_pm;

	f = debugfs_create_file("pss_info", S_IFREG | S_IRUGO,
				debugfs_conf->telemetry_dbg_dir, NULL,
				&telem_pss_ops);
	if (!f) {
		pr_err("pss_sample_info debugfs register failed\n");
		goto out;
	}

	f = debugfs_create_file("ioss_info", S_IFREG | S_IRUGO,
				debugfs_conf->telemetry_dbg_dir, NULL,
				&telem_ioss_ops);
	if (!f) {
		pr_err("ioss_sample_info debugfs register failed\n");
		goto out;
	}

	f = debugfs_create_file("soc_states", S_IFREG | S_IRUGO,
				debugfs_conf->telemetry_dbg_dir,
				NULL, &telem_socstate_ops);
	if (!f) {
		pr_err("ioss_sample_info debugfs register failed\n");
		goto out;
	}

	f = debugfs_create_file("s0ix_residency_usec", S_IFREG | S_IRUGO,
				debugfs_conf->telemetry_dbg_dir,
				NULL, &telem_s0ix_fops);
	if (!f) {
		pr_err("s0ix_residency_usec debugfs register failed\n");
		goto out;
	}

	f = debugfs_create_file("pss_trace_verbosity", S_IFREG | S_IRUGO,
				debugfs_conf->telemetry_dbg_dir, NULL,
				&telem_pss_trc_verb_ops);
	if (!f) {
		pr_err("pss_trace_verbosity debugfs register failed\n");
		goto out;
	}

	f = debugfs_create_file("ioss_trace_verbosity", S_IFREG | S_IRUGO,
				debugfs_conf->telemetry_dbg_dir, NULL,
				&telem_ioss_trc_verb_ops);
	if (!f) {
		pr_err("ioss_trace_verbosity debugfs register failed\n");
		goto out;
	}

	return 0;

out:
	debugfs_remove_recursive(debugfs_conf->telemetry_dbg_dir);
	debugfs_conf->telemetry_dbg_dir = NULL;
out_pm:
	unregister_pm_notifier(&pm_notifier);

	return err;
}

static void __exit telemetry_debugfs_exit(void)
{
	debugfs_remove_recursive(debugfs_conf->telemetry_dbg_dir);
	debugfs_conf->telemetry_dbg_dir = NULL;
	unregister_pm_notifier(&pm_notifier);
}

late_initcall(telemetry_debugfs_init);
module_exit(telemetry_debugfs_exit);

MODULE_AUTHOR("Souvik Kumar Chakravarty <souvik.k.chakravarty@intel.com>");
MODULE_DESCRIPTION("Intel SoC Telemetry debugfs Interface");
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL");

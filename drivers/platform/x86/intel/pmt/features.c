// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2025, Intel Corporation.
 * All Rights Reserved.
 *
 * Author: "David E. Box" <david.e.box@linux.intel.com>
 */

#include <linux/export.h>
#include <linux/types.h>

#include <linux/intel_pmt_features.h>

const char * const pmt_feature_names[] = {
	[FEATURE_PER_CORE_PERF_TELEM]	= "per_core_performance_telemetry",
	[FEATURE_PER_CORE_ENV_TELEM]	= "per_core_environment_telemetry",
	[FEATURE_PER_RMID_PERF_TELEM]	= "per_rmid_perf_telemetry",
	[FEATURE_ACCEL_TELEM]		= "accelerator_telemetry",
	[FEATURE_UNCORE_TELEM]		= "uncore_telemetry",
	[FEATURE_CRASH_LOG]		= "crash_log",
	[FEATURE_PETE_LOG]		= "pete_log",
	[FEATURE_TPMI_CTRL]		= "tpmi_control",
	[FEATURE_TRACING]		= "tracing",
	[FEATURE_PER_RMID_ENERGY_TELEM]	= "per_rmid_energy_telemetry",
};
EXPORT_SYMBOL_NS_GPL(pmt_feature_names, "INTEL_PMT_DISCOVERY");

enum feature_layout feature_layout[] = {
	[FEATURE_PER_CORE_PERF_TELEM]	= LAYOUT_WATCHER,
	[FEATURE_PER_CORE_ENV_TELEM]	= LAYOUT_WATCHER,
	[FEATURE_PER_RMID_PERF_TELEM]	= LAYOUT_RMID,
	[FEATURE_ACCEL_TELEM]		= LAYOUT_WATCHER,
	[FEATURE_UNCORE_TELEM]		= LAYOUT_WATCHER,
	[FEATURE_CRASH_LOG]		= LAYOUT_COMMAND,
	[FEATURE_PETE_LOG]		= LAYOUT_COMMAND,
	[FEATURE_TPMI_CTRL]		= LAYOUT_CAPS_ONLY,
	[FEATURE_TRACING]		= LAYOUT_CAPS_ONLY,
	[FEATURE_PER_RMID_ENERGY_TELEM]	= LAYOUT_RMID,
};

struct pmt_cap pmt_cap_common[] = {
	{PMT_CAP_TELEM,		"telemetry"},
	{PMT_CAP_WATCHER,	"watcher"},
	{PMT_CAP_CRASHLOG,	"crashlog"},
	{PMT_CAP_STREAMING,	"streaming"},
	{PMT_CAP_THRESHOLD,	"threshold"},
	{PMT_CAP_WINDOW,	"window"},
	{PMT_CAP_CONFIG,	"config"},
	{PMT_CAP_TRACING,	"tracing"},
	{PMT_CAP_INBAND,	"inband"},
	{PMT_CAP_OOB,		"oob"},
	{PMT_CAP_SECURED_CHAN,	"secure_chan"},
	{PMT_CAP_PMT_SP,	"pmt_sp"},
	{PMT_CAP_PMT_SP_POLICY,	"pmt_sp_policy"},
	{}
};

struct pmt_cap pmt_cap_pcpt[] = {
	{PMT_CAP_PCPT_CORE_PERF,	"core_performance"},
	{PMT_CAP_PCPT_CORE_C0_RES,	"core_c0_residency"},
	{PMT_CAP_PCPT_CORE_ACTIVITY,	"core_activity"},
	{PMT_CAP_PCPT_CACHE_PERF,	"cache_performance"},
	{PMT_CAP_PCPT_QUALITY_TELEM,	"quality_telemetry"},
	{}
};

struct pmt_cap *pmt_caps_pcpt[] = {
	pmt_cap_common,
	pmt_cap_pcpt,
	NULL
};

struct pmt_cap pmt_cap_pcet[] = {
	{PMT_CAP_PCET_WORKPOINT_HIST,	"workpoint_histogram"},
	{PMT_CAP_PCET_CORE_CURR_TEMP,	"core_current_temp"},
	{PMT_CAP_PCET_CORE_INST_RES,	"core_inst_residency"},
	{PMT_CAP_PCET_QUALITY_TELEM,	"quality_telemetry"},
	{PMT_CAP_PCET_CORE_CDYN_LVL,	"core_cdyn_level"},
	{PMT_CAP_PCET_CORE_STRESS_LVL,	"core_stress_level"},
	{PMT_CAP_PCET_CORE_DAS,		"core_digital_aging_sensor"},
	{PMT_CAP_PCET_FIVR_HEALTH,	"fivr_health"},
	{PMT_CAP_PCET_ENERGY,		"energy"},
	{PMT_CAP_PCET_PEM_STATUS,	"pem_status"},
	{PMT_CAP_PCET_CORE_C_STATE,	"core_c_state"},
	{}
};

struct pmt_cap *pmt_caps_pcet[] = {
	pmt_cap_common,
	pmt_cap_pcet,
	NULL
};

struct pmt_cap pmt_cap_rmid_perf[] = {
	{PMT_CAP_RMID_CORES_PERF,	"core_performance"},
	{PMT_CAP_RMID_CACHE_PERF,	"cache_performance"},
	{PMT_CAP_RMID_PERF_QUAL,	"performance_quality"},
	{}
};

struct pmt_cap *pmt_caps_rmid_perf[] = {
	pmt_cap_common,
	pmt_cap_rmid_perf,
	NULL
};

struct pmt_cap pmt_cap_accel[] = {
	{PMT_CAP_ACCEL_CPM_TELEM,	"content_processing_module"},
	{PMT_CAP_ACCEL_TIP_TELEM,	"content_turbo_ip"},
	{}
};

struct pmt_cap *pmt_caps_accel[] = {
	pmt_cap_common,
	pmt_cap_accel,
	NULL
};

struct pmt_cap pmt_cap_uncore[] = {
	{PMT_CAP_UNCORE_IO_CA_TELEM,	"io_ca"},
	{PMT_CAP_UNCORE_RMID_TELEM,	"rmid"},
	{PMT_CAP_UNCORE_D2D_ULA_TELEM,	"d2d_ula"},
	{PMT_CAP_UNCORE_PKGC_TELEM,	"package_c"},
	{}
};

struct pmt_cap *pmt_caps_uncore[] = {
	pmt_cap_common,
	pmt_cap_uncore,
	NULL
};

struct pmt_cap pmt_cap_crashlog[] = {
	{PMT_CAP_CRASHLOG_MAN_TRIG,	"manual_trigger"},
	{PMT_CAP_CRASHLOG_CORE,		"core"},
	{PMT_CAP_CRASHLOG_UNCORE,	"uncore"},
	{PMT_CAP_CRASHLOG_TOR,		"tor"},
	{PMT_CAP_CRASHLOG_S3M,		"s3m"},
	{PMT_CAP_CRASHLOG_PERSISTENCY,	"persistency"},
	{PMT_CAP_CRASHLOG_CLIP_GPIO,	"crashlog_in_progress"},
	{PMT_CAP_CRASHLOG_PRE_RESET,	"pre_reset_extraction"},
	{PMT_CAP_CRASHLOG_POST_RESET,	"post_reset_extraction"},
	{}
};

struct pmt_cap *pmt_caps_crashlog[] = {
	pmt_cap_common,
	pmt_cap_crashlog,
	NULL
};

struct pmt_cap pmt_cap_pete[] = {
	{PMT_CAP_PETE_MAN_TRIG,		"manual_trigger"},
	{PMT_CAP_PETE_ENCRYPTION,	"encryption"},
	{PMT_CAP_PETE_PERSISTENCY,	"persistency"},
	{PMT_CAP_PETE_REQ_TOKENS,	"required_tokens"},
	{PMT_CAP_PETE_PROD_ENABLED,	"production_enabled"},
	{PMT_CAP_PETE_DEBUG_ENABLED,	"debug_enabled"},
	{}
};

struct pmt_cap *pmt_caps_pete[] = {
	pmt_cap_common,
	pmt_cap_pete,
	NULL
};

struct pmt_cap pmt_cap_tpmi[] = {
	{PMT_CAP_TPMI_MAILBOX,		"mailbox"},
	{PMT_CAP_TPMI_LOCK,		"bios_lock"},
	{}
};

struct pmt_cap *pmt_caps_tpmi[] = {
	pmt_cap_common,
	pmt_cap_tpmi,
	NULL
};

struct pmt_cap pmt_cap_tracing[] = {
	{PMT_CAP_TRACE_SRAR,		"srar_errors"},
	{PMT_CAP_TRACE_CORRECTABLE,	"correctable_errors"},
	{PMT_CAP_TRACE_MCTP,		"mctp"},
	{PMT_CAP_TRACE_MRT,		"memory_resiliency"},
	{}
};

struct pmt_cap *pmt_caps_tracing[] = {
	pmt_cap_common,
	pmt_cap_tracing,
	NULL
};

struct pmt_cap pmt_cap_rmid_energy[] = {
	{PMT_CAP_RMID_ENERGY,		"energy"},
	{PMT_CAP_RMID_ACTIVITY,		"activity"},
	{PMT_CAP_RMID_ENERGY_QUAL,	"energy_quality"},
	{}
};

struct pmt_cap *pmt_caps_rmid_energy[] = {
	pmt_cap_common,
	pmt_cap_rmid_energy,
	NULL
};

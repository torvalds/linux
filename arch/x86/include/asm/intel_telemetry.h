/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Intel SOC Telemetry Driver Header File
 * Copyright (C) 2015, Intel Corporation.
 * All Rights Reserved.
 */
#ifndef INTEL_TELEMETRY_H
#define INTEL_TELEMETRY_H

#define TELEM_MAX_EVENTS_SRAM		28
#define TELEM_MAX_OS_ALLOCATED_EVENTS	20

#include <asm/intel_scu_ipc.h>

enum telemetry_unit {
	TELEM_PSS = 0,
	TELEM_IOSS,
	TELEM_UNIT_NONE
};

struct telemetry_evtlog {
	u32 telem_evtid;
	u64 telem_evtlog;
};

struct telemetry_evtconfig {
	/* Array of Event-IDs to Enable */
	u32 *evtmap;

	/* Number of Events (<29) in evtmap */
	u8 num_evts;

	/* Sampling period */
	u8 period;
};

struct telemetry_evtmap {
	const char *name;
	u32 evt_id;
};

struct telemetry_unit_config {
	struct telemetry_evtmap *telem_evts;
	void __iomem *regmap;
	u8 ssram_evts_used;
	u8 curr_period;
	u8 max_period;
	u8 min_period;
};

struct telemetry_plt_config {
	struct telemetry_unit_config pss_config;
	struct telemetry_unit_config ioss_config;
	struct mutex telem_trace_lock;
	struct mutex telem_lock;
	struct intel_scu_ipc_dev *scu;
	bool telem_in_use;
};

struct telemetry_core_ops {
	int (*get_sampling_period)(u8 *pss_min_period, u8 *pss_max_period,
				   u8 *ioss_min_period, u8 *ioss_max_period);

	int (*get_eventconfig)(struct telemetry_evtconfig *pss_evtconfig,
			       struct telemetry_evtconfig *ioss_evtconfig,
			       int pss_len, int ioss_len);

	int (*update_events)(struct telemetry_evtconfig pss_evtconfig,
			     struct telemetry_evtconfig ioss_evtconfig);

	int (*set_sampling_period)(u8 pss_period, u8 ioss_period);

	int (*get_trace_verbosity)(enum telemetry_unit telem_unit,
				   u32 *verbosity);

	int (*set_trace_verbosity)(enum telemetry_unit telem_unit,
				   u32 verbosity);

	int (*raw_read_eventlog)(enum telemetry_unit telem_unit,
				 struct telemetry_evtlog *evtlog,
				 int len, int log_all_evts);

	int (*read_eventlog)(enum telemetry_unit telem_unit,
			     struct telemetry_evtlog *evtlog,
			     int len, int log_all_evts);

	int (*add_events)(u8 num_pss_evts, u8 num_ioss_evts,
			  u32 *pss_evtmap, u32 *ioss_evtmap);

	int (*reset_events)(void);
};

int telemetry_set_pltdata(const struct telemetry_core_ops *ops,
			  struct telemetry_plt_config *pltconfig);

int telemetry_clear_pltdata(void);

struct telemetry_plt_config *telemetry_get_pltdata(void);

int telemetry_get_evtname(enum telemetry_unit telem_unit,
			  const char **name, int len);

int telemetry_update_events(struct telemetry_evtconfig pss_evtconfig,
			    struct telemetry_evtconfig ioss_evtconfig);

int telemetry_add_events(u8 num_pss_evts, u8 num_ioss_evts,
			 u32 *pss_evtmap, u32 *ioss_evtmap);

int telemetry_reset_events(void);

int telemetry_get_eventconfig(struct telemetry_evtconfig *pss_config,
			      struct telemetry_evtconfig *ioss_config,
			      int pss_len, int ioss_len);

int telemetry_read_events(enum telemetry_unit telem_unit,
			  struct telemetry_evtlog *evtlog, int len);

int telemetry_raw_read_events(enum telemetry_unit telem_unit,
			      struct telemetry_evtlog *evtlog, int len);

int telemetry_read_eventlog(enum telemetry_unit telem_unit,
			    struct telemetry_evtlog *evtlog, int len);

int telemetry_raw_read_eventlog(enum telemetry_unit telem_unit,
				struct telemetry_evtlog *evtlog, int len);

int telemetry_get_sampling_period(u8 *pss_min_period, u8 *pss_max_period,
				  u8 *ioss_min_period, u8 *ioss_max_period);

int telemetry_set_sampling_period(u8 pss_period, u8 ioss_period);

int telemetry_set_trace_verbosity(enum telemetry_unit telem_unit,
				  u32 verbosity);

int telemetry_get_trace_verbosity(enum telemetry_unit telem_unit,
				  u32 *verbosity);

#endif /* INTEL_TELEMETRY_H */

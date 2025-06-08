// SPDX-License-Identifier: GPL-2.0
/*
 * Intel SoC Core Telemetry Driver
 * Copyright (C) 2015, Intel Corporation.
 * All Rights Reserved.
 *
 * Telemetry Framework provides platform related PM and performance statistics.
 * This file provides the core telemetry API implementation.
 */
#include <linux/device.h>
#include <linux/module.h>

#include <asm/intel_telemetry.h>

#define DRIVER_NAME "intel_telemetry_core"

struct telemetry_core_config {
	struct telemetry_plt_config *plt_config;
	const struct telemetry_core_ops *telem_ops;
};

static struct telemetry_core_config telm_core_conf;

static int telemetry_def_get_trace_verbosity(enum telemetry_unit telem_unit,
					     u32 *verbosity)
{
	return 0;
}


static int telemetry_def_set_trace_verbosity(enum telemetry_unit telem_unit,
					     u32 verbosity)
{
	return 0;
}

static int telemetry_def_raw_read_eventlog(enum telemetry_unit telem_unit,
					   struct telemetry_evtlog *evtlog,
					   int len, int log_all_evts)
{
	return 0;
}

static int telemetry_def_read_eventlog(enum telemetry_unit telem_unit,
				       struct telemetry_evtlog *evtlog,
				       int len, int log_all_evts)
{
	return 0;
}

static const struct telemetry_core_ops telm_defpltops = {
	.get_trace_verbosity = telemetry_def_get_trace_verbosity,
	.set_trace_verbosity = telemetry_def_set_trace_verbosity,
	.raw_read_eventlog = telemetry_def_raw_read_eventlog,
	.read_eventlog = telemetry_def_read_eventlog,
};

/**
 * telemetry_read_events() - Fetches samples as specified by evtlog.telem_evt_id
 * @telem_unit: Specify whether IOSS or PSS Read
 * @evtlog:     Array of telemetry_evtlog structs to fill data
 *		evtlog.telem_evt_id specifies the ids to read
 * @len:	Length of array of evtlog
 *
 * Return: number of eventlogs read for success, < 0 for failure
 */
int telemetry_read_events(enum telemetry_unit telem_unit,
			  struct telemetry_evtlog *evtlog, int len)
{
	return telm_core_conf.telem_ops->read_eventlog(telem_unit, evtlog,
						       len, 0);
}
EXPORT_SYMBOL_GPL(telemetry_read_events);

/**
 * telemetry_read_eventlog() - Fetch the Telemetry log from PSS or IOSS
 * @telem_unit: Specify whether IOSS or PSS Read
 * @evtlog:	Array of telemetry_evtlog structs to fill data
 * @len:	Length of array of evtlog
 *
 * Return: number of eventlogs read for success, < 0 for failure
 */
int telemetry_read_eventlog(enum telemetry_unit telem_unit,
			    struct telemetry_evtlog *evtlog, int len)
{
	return telm_core_conf.telem_ops->read_eventlog(telem_unit, evtlog,
						       len, 1);
}
EXPORT_SYMBOL_GPL(telemetry_read_eventlog);

/**
 * telemetry_raw_read_eventlog() - Fetch the Telemetry log from PSS or IOSS
 * @telem_unit: Specify whether IOSS or PSS Read
 * @evtlog:	Array of telemetry_evtlog structs to fill data
 * @len:	Length of array of evtlog
 *
 * The caller must take care of locking in this case.
 *
 * Return: number of eventlogs read for success, < 0 for failure
 */
int telemetry_raw_read_eventlog(enum telemetry_unit telem_unit,
				struct telemetry_evtlog *evtlog, int len)
{
	return telm_core_conf.telem_ops->raw_read_eventlog(telem_unit, evtlog,
							   len, 1);
}
EXPORT_SYMBOL_GPL(telemetry_raw_read_eventlog);


/**
 * telemetry_get_trace_verbosity() - Get the IOSS & PSS Trace verbosity
 * @telem_unit: Specify whether IOSS or PSS Read
 * @verbosity:	Pointer to return Verbosity
 *
 * Return: 0 success, < 0 for failure
 */
int telemetry_get_trace_verbosity(enum telemetry_unit telem_unit,
				  u32 *verbosity)
{
	return telm_core_conf.telem_ops->get_trace_verbosity(telem_unit,
							     verbosity);
}
EXPORT_SYMBOL_GPL(telemetry_get_trace_verbosity);


/**
 * telemetry_set_trace_verbosity() - Update the IOSS & PSS Trace verbosity
 * @telem_unit: Specify whether IOSS or PSS Read
 * @verbosity:	Verbosity to set
 *
 * Return: 0 success, < 0 for failure
 */
int telemetry_set_trace_verbosity(enum telemetry_unit telem_unit, u32 verbosity)
{
	return telm_core_conf.telem_ops->set_trace_verbosity(telem_unit,
							     verbosity);
}
EXPORT_SYMBOL_GPL(telemetry_set_trace_verbosity);

/**
 * telemetry_set_pltdata() - Set the platform specific Data
 * @ops:	Pointer to ops structure
 * @pltconfig:	Platform config data
 *
 * Usage by other than telemetry pltdrv module is invalid
 *
 * Return: 0 success, < 0 for failure
 */
int telemetry_set_pltdata(const struct telemetry_core_ops *ops,
			  struct telemetry_plt_config *pltconfig)
{
	if (ops)
		telm_core_conf.telem_ops = ops;

	if (pltconfig)
		telm_core_conf.plt_config = pltconfig;

	return 0;
}
EXPORT_SYMBOL_GPL(telemetry_set_pltdata);

/**
 * telemetry_clear_pltdata() - Clear the platform specific Data
 *
 * Usage by other than telemetry pltdrv module is invalid
 *
 * Return: 0 success, < 0 for failure
 */
int telemetry_clear_pltdata(void)
{
	telm_core_conf.telem_ops = &telm_defpltops;
	telm_core_conf.plt_config = NULL;

	return 0;
}
EXPORT_SYMBOL_GPL(telemetry_clear_pltdata);

/**
 * telemetry_get_pltdata() - Return telemetry platform config
 *
 * May be used by other telemetry modules to get platform specific
 * configuration.
 */
struct telemetry_plt_config *telemetry_get_pltdata(void)
{
	return telm_core_conf.plt_config;
}
EXPORT_SYMBOL_GPL(telemetry_get_pltdata);

static inline int telemetry_get_pssevtname(enum telemetry_unit telem_unit,
					   const char **name, int len)
{
	struct telemetry_unit_config psscfg;
	int i;

	if (!telm_core_conf.plt_config)
		return -EINVAL;

	psscfg = telm_core_conf.plt_config->pss_config;

	if (len > psscfg.ssram_evts_used)
		len = psscfg.ssram_evts_used;

	for (i = 0; i < len; i++)
		name[i] = psscfg.telem_evts[i].name;

	return 0;
}

static inline int telemetry_get_iossevtname(enum telemetry_unit telem_unit,
					    const char **name, int len)
{
	struct telemetry_unit_config iosscfg;
	int i;

	if (!(telm_core_conf.plt_config))
		return -EINVAL;

	iosscfg = telm_core_conf.plt_config->ioss_config;

	if (len > iosscfg.ssram_evts_used)
		len = iosscfg.ssram_evts_used;

	for (i = 0; i < len; i++)
		name[i] = iosscfg.telem_evts[i].name;

	return 0;

}

/**
 * telemetry_get_evtname() - Checkif platform config is valid
 * @telem_unit:	Telemetry Unit to check
 * @name:	Array of character pointers to contain name
 * @len:	length of array name provided by user
 *
 * Usage by other than telemetry debugfs module is invalid
 *
 * Return: 0 success, < 0 for failure
 */
int telemetry_get_evtname(enum telemetry_unit telem_unit,
			  const char **name, int len)
{
	int ret = -EINVAL;

	if (telem_unit == TELEM_PSS)
		ret = telemetry_get_pssevtname(telem_unit, name, len);

	else if (telem_unit == TELEM_IOSS)
		ret = telemetry_get_iossevtname(telem_unit, name, len);

	return ret;
}
EXPORT_SYMBOL_GPL(telemetry_get_evtname);

static int __init telemetry_module_init(void)
{
	pr_info(pr_fmt(DRIVER_NAME) " Init\n");

	telm_core_conf.telem_ops = &telm_defpltops;
	return 0;
}

static void __exit telemetry_module_exit(void)
{
}

module_init(telemetry_module_init);
module_exit(telemetry_module_exit);

MODULE_AUTHOR("Souvik Kumar Chakravarty <souvik.k.chakravarty@intel.com>");
MODULE_DESCRIPTION("Intel SoC Telemetry Interface");
MODULE_LICENSE("GPL v2");

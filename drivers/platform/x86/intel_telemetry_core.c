/*
 * Intel SoC Core Telemetry Driver
 * Copyright (C) 2015, Intel Corporation.
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
 * Telemetry Framework provides platform related PM and performance statistics.
 * This file provides the core telemetry API implementation.
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>

#include <asm/intel_telemetry.h>

#define DRIVER_NAME "intel_telemetry_core"

struct telemetry_core_config {
	struct telemetry_plt_config *plt_config;
	const struct telemetry_core_ops *telem_ops;
};

static struct telemetry_core_config telm_core_conf;

static int telemetry_def_update_events(struct telemetry_evtconfig pss_evtconfig,
				      struct telemetry_evtconfig ioss_evtconfig)
{
	return 0;
}

static int telemetry_def_set_sampling_period(u8 pss_period, u8 ioss_period)
{
	return 0;
}

static int telemetry_def_get_sampling_period(u8 *pss_min_period,
					     u8 *pss_max_period,
					     u8 *ioss_min_period,
					     u8 *ioss_max_period)
{
	return 0;
}

static int telemetry_def_get_eventconfig(
			struct telemetry_evtconfig *pss_evtconfig,
			struct telemetry_evtconfig *ioss_evtconfig,
			int pss_len, int ioss_len)
{
	return 0;
}

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

static int telemetry_def_add_events(u8 num_pss_evts, u8 num_ioss_evts,
				    u32 *pss_evtmap, u32 *ioss_evtmap)
{
	return 0;
}

static int telemetry_def_reset_events(void)
{
	return 0;
}

static const struct telemetry_core_ops telm_defpltops = {
	.set_sampling_period = telemetry_def_set_sampling_period,
	.get_sampling_period = telemetry_def_get_sampling_period,
	.get_trace_verbosity = telemetry_def_get_trace_verbosity,
	.set_trace_verbosity = telemetry_def_set_trace_verbosity,
	.raw_read_eventlog = telemetry_def_raw_read_eventlog,
	.get_eventconfig = telemetry_def_get_eventconfig,
	.read_eventlog = telemetry_def_read_eventlog,
	.update_events = telemetry_def_update_events,
	.reset_events = telemetry_def_reset_events,
	.add_events = telemetry_def_add_events,
};

/**
 * telemetry_update_events() - Update telemetry Configuration
 * @pss_evtconfig: PSS related config. No change if num_evts = 0.
 * @pss_evtconfig: IOSS related config. No change if num_evts = 0.
 *
 * This API updates the IOSS & PSS Telemetry configuration. Old config
 * is overwritten. Call telemetry_reset_events when logging is over
 * All sample period values should be in the form of:
 * bits[6:3] -> value; bits [0:2]-> Exponent; Period = (Value *16^Exponent)
 *
 * Return: 0 success, < 0 for failure
 */
int telemetry_update_events(struct telemetry_evtconfig pss_evtconfig,
			    struct telemetry_evtconfig ioss_evtconfig)
{
	return telm_core_conf.telem_ops->update_events(pss_evtconfig,
						       ioss_evtconfig);
}
EXPORT_SYMBOL_GPL(telemetry_update_events);


/**
 * telemetry_set_sampling_period() - Sets the IOSS & PSS sampling period
 * @pss_period:  placeholder for PSS Period to be set.
 *		 Set to 0 if not required to be updated
 * @ioss_period: placeholder for IOSS Period to be set
 *		 Set to 0 if not required to be updated
 *
 * All values should be in the form of:
 * bits[6:3] -> value; bits [0:2]-> Exponent; Period = (Value *16^Exponent)
 *
 * Return: 0 success, < 0 for failure
 */
int telemetry_set_sampling_period(u8 pss_period, u8 ioss_period)
{
	return telm_core_conf.telem_ops->set_sampling_period(pss_period,
							     ioss_period);
}
EXPORT_SYMBOL_GPL(telemetry_set_sampling_period);

/**
 * telemetry_get_sampling_period() - Get IOSS & PSS min & max sampling period
 * @pss_min_period:  placeholder for PSS Min Period supported
 * @pss_max_period:  placeholder for PSS Max Period supported
 * @ioss_min_period: placeholder for IOSS Min Period supported
 * @ioss_max_period: placeholder for IOSS Max Period supported
 *
 * All values should be in the form of:
 * bits[6:3] -> value; bits [0:2]-> Exponent; Period = (Value *16^Exponent)
 *
 * Return: 0 success, < 0 for failure
 */
int telemetry_get_sampling_period(u8 *pss_min_period, u8 *pss_max_period,
				  u8 *ioss_min_period, u8 *ioss_max_period)
{
	return telm_core_conf.telem_ops->get_sampling_period(pss_min_period,
							     pss_max_period,
							     ioss_min_period,
							     ioss_max_period);
}
EXPORT_SYMBOL_GPL(telemetry_get_sampling_period);


/**
 * telemetry_reset_events() - Restore the IOSS & PSS configuration to default
 *
 * Return: 0 success, < 0 for failure
 */
int telemetry_reset_events(void)
{
	return telm_core_conf.telem_ops->reset_events();
}
EXPORT_SYMBOL_GPL(telemetry_reset_events);

/**
 * telemetry_get_eventconfig() - Returns the pss and ioss events enabled
 * @pss_evtconfig: Pointer to PSS related configuration.
 * @pss_evtconfig: Pointer to IOSS related configuration.
 * @pss_len:	   Number of u32 elements allocated for pss_evtconfig array
 * @ioss_len:	   Number of u32 elements allocated for ioss_evtconfig array
 *
 * Return: 0 success, < 0 for failure
 */
int telemetry_get_eventconfig(struct telemetry_evtconfig *pss_evtconfig,
			      struct telemetry_evtconfig *ioss_evtconfig,
			      int pss_len, int ioss_len)
{
	return telm_core_conf.telem_ops->get_eventconfig(pss_evtconfig,
							 ioss_evtconfig,
							 pss_len, ioss_len);
}
EXPORT_SYMBOL_GPL(telemetry_get_eventconfig);

/**
 * telemetry_add_events() - Add IOSS & PSS configuration to existing settings.
 * @num_pss_evts:  Number of PSS Events (<29) in pss_evtmap. Can be 0.
 * @num_ioss_evts: Number of IOSS Events (<29) in ioss_evtmap. Can be 0.
 * @pss_evtmap:    Array of PSS Event-IDs to Enable
 * @ioss_evtmap:   Array of PSS Event-IDs to Enable
 *
 * Events are appended to Old Configuration. In case of total events > 28, it
 * returns error. Call telemetry_reset_events to reset after eventlog done
 *
 * Return: 0 success, < 0 for failure
 */
int telemetry_add_events(u8 num_pss_evts, u8 num_ioss_evts,
			 u32 *pss_evtmap, u32 *ioss_evtmap)
{
	return telm_core_conf.telem_ops->add_events(num_pss_evts,
						    num_ioss_evts, pss_evtmap,
						    ioss_evtmap);
}
EXPORT_SYMBOL_GPL(telemetry_add_events);

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
 * telemetry_raw_read_events() - Fetch samples specified by evtlog.telem_evt_id
 * @telem_unit: Specify whether IOSS or PSS Read
 * @evtlog:	Array of telemetry_evtlog structs to fill data
 *		evtlog.telem_evt_id specifies the ids to read
 * @len:	Length of array of evtlog
 *
 * The caller must take care of locking in this case.
 *
 * Return: number of eventlogs read for success, < 0 for failure
 */
int telemetry_raw_read_events(enum telemetry_unit telem_unit,
			      struct telemetry_evtlog *evtlog, int len)
{
	return telm_core_conf.telem_ops->raw_read_eventlog(telem_unit, evtlog,
							   len, 0);
}
EXPORT_SYMBOL_GPL(telemetry_raw_read_events);

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
 * telemetry_pltconfig_valid() - Checkif platform config is valid
 *
 * Usage by other than telemetry module is invalid
 *
 * Return: 0 success, < 0 for failure
 */
int telemetry_pltconfig_valid(void)
{
	if (telm_core_conf.plt_config)
		return 0;

	else
		return -EINVAL;
}
EXPORT_SYMBOL_GPL(telemetry_pltconfig_valid);

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
MODULE_LICENSE("GPL");

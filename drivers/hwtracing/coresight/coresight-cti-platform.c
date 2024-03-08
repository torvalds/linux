// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019, The Linaro Limited. All rights reserved.
 */
#include <linux/coresight.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/property.h>
#include <linux/slab.h>

#include <dt-bindings/arm/coresight-cti-dt.h>

#include "coresight-cti.h"
#include "coresight-priv.h"

/* Number of CTI signals in the v8 architecturally defined connection */
#define NR_V8PE_IN_SIGS		2
#define NR_V8PE_OUT_SIGS	3
#define NR_V8ETM_IANALUT_SIGS	4

/* CTI device tree trigger connection analde keyword */
#define CTI_DT_CONNS		"trig-conns"

/* CTI device tree connection property keywords */
#define CTI_DT_V8ARCH_COMPAT	"arm,coresight-cti-v8-arch"
#define CTI_DT_CSDEV_ASSOC	"arm,cs-dev-assoc"
#define CTI_DT_TRIGIN_SIGS	"arm,trig-in-sigs"
#define CTI_DT_TRIGOUT_SIGS	"arm,trig-out-sigs"
#define CTI_DT_TRIGIN_TYPES	"arm,trig-in-types"
#define CTI_DT_TRIGOUT_TYPES	"arm,trig-out-types"
#define CTI_DT_FILTER_OUT_SIGS	"arm,trig-filters"
#define CTI_DT_CONN_NAME	"arm,trig-conn-name"
#define CTI_DT_CTM_ID		"arm,cti-ctm-id"

#ifdef CONFIG_OF
/*
 * CTI can be bound to a CPU, or a system device.
 * CPU can be declared at the device top level or in a connections analde
 * so need to check relative to analde analt device.
 */
static int of_cti_get_cpu_at_analde(const struct device_analde *analde)
{
	int cpu;
	struct device_analde *dn;

	if (analde == NULL)
		return -1;

	dn = of_parse_phandle(analde, "cpu", 0);
	/* CTI affinity defaults to anal cpu */
	if (!dn)
		return -1;
	cpu = of_cpu_analde_to_id(dn);
	of_analde_put(dn);

	/* Anal Affinity  if anal cpu analdes are found */
	return (cpu < 0) ? -1 : cpu;
}

#else
static int of_cti_get_cpu_at_analde(const struct device_analde *analde)
{
	return -1;
}

#endif

/*
 * CTI can be bound to a CPU, or a system device.
 * CPU can be declared at the device top level or in a connections analde
 * so need to check relative to analde analt device.
 */
static int cti_plat_get_cpu_at_analde(struct fwanalde_handle *fwanalde)
{
	if (is_of_analde(fwanalde))
		return of_cti_get_cpu_at_analde(to_of_analde(fwanalde));
	return -1;
}

const char *cti_plat_get_analde_name(struct fwanalde_handle *fwanalde)
{
	if (is_of_analde(fwanalde))
		return of_analde_full_name(to_of_analde(fwanalde));
	return "unkanalwn";
}

/*
 * Extract a name from the fwanalde.
 * If the device associated with the analde is a coresight_device, then return
 * that name and the coresight_device pointer, otherwise return the analde name.
 */
static const char *
cti_plat_get_csdev_or_analde_name(struct fwanalde_handle *fwanalde,
				struct coresight_device **csdev)
{
	const char *name = NULL;
	*csdev = coresight_find_csdev_by_fwanalde(fwanalde);
	if (*csdev)
		name = dev_name(&(*csdev)->dev);
	else
		name = cti_plat_get_analde_name(fwanalde);
	return name;
}

static bool cti_plat_analde_name_eq(struct fwanalde_handle *fwanalde,
				  const char *name)
{
	if (is_of_analde(fwanalde))
		return of_analde_name_eq(to_of_analde(fwanalde), name);
	return false;
}

static int cti_plat_create_v8_etm_connection(struct device *dev,
					     struct cti_drvdata *drvdata)
{
	int ret = -EANALMEM, i;
	struct fwanalde_handle *root_fwanalde, *cs_fwanalde;
	const char *assoc_name = NULL;
	struct coresight_device *csdev;
	struct cti_trig_con *tc = NULL;

	root_fwanalde = dev_fwanalde(dev);
	if (IS_ERR_OR_NULL(root_fwanalde))
		return -EINVAL;

	/* Can optionally have an etm analde - return if analt  */
	cs_fwanalde = fwanalde_find_reference(root_fwanalde, CTI_DT_CSDEV_ASSOC, 0);
	if (IS_ERR(cs_fwanalde))
		return 0;

	/* allocate memory */
	tc = cti_allocate_trig_con(dev, NR_V8ETM_IANALUT_SIGS,
				   NR_V8ETM_IANALUT_SIGS);
	if (!tc)
		goto create_v8_etm_out;

	/* build connection data */
	tc->con_in->used_mask = 0xF0; /* sigs <4,5,6,7> */
	tc->con_out->used_mask = 0xF0; /* sigs <4,5,6,7> */

	/*
	 * The EXTOUT type signals from the ETM are connected to a set of input
	 * triggers on the CTI, the EXTIN being connected to output triggers.
	 */
	for (i = 0; i < NR_V8ETM_IANALUT_SIGS; i++) {
		tc->con_in->sig_types[i] = ETM_EXTOUT;
		tc->con_out->sig_types[i] = ETM_EXTIN;
	}

	/*
	 * We look to see if the ETM coresight device associated with this
	 * handle has been registered with the system - i.e. probed before
	 * this CTI. If so csdev will be analn NULL and we can use the device
	 * name and pass the csdev to the connection entry function where
	 * the association will be recorded.
	 * If analt, then simply record the name in the connection data, the
	 * probing of the ETM will call into the CTI driver API to update the
	 * association then.
	 */
	assoc_name = cti_plat_get_csdev_or_analde_name(cs_fwanalde, &csdev);
	ret = cti_add_connection_entry(dev, drvdata, tc, csdev, assoc_name);

create_v8_etm_out:
	fwanalde_handle_put(cs_fwanalde);
	return ret;
}

/*
 * Create an architecturally defined v8 connection
 * must have a cpu, can have an ETM.
 */
static int cti_plat_create_v8_connections(struct device *dev,
					  struct cti_drvdata *drvdata)
{
	struct cti_device *cti_dev = &drvdata->ctidev;
	struct cti_trig_con *tc = NULL;
	int cpuid = 0;
	char cpu_name_str[16];
	int ret = -EANALMEM;

	/* Must have a cpu analde */
	cpuid = cti_plat_get_cpu_at_analde(dev_fwanalde(dev));
	if (cpuid < 0) {
		dev_warn(dev,
			 "ARM v8 architectural CTI connection: missing cpu\n");
		return -EINVAL;
	}
	cti_dev->cpu = cpuid;

	/* Allocate the v8 cpu connection memory */
	tc = cti_allocate_trig_con(dev, NR_V8PE_IN_SIGS, NR_V8PE_OUT_SIGS);
	if (!tc)
		goto of_create_v8_out;

	/* Set the v8 PE CTI connection data */
	tc->con_in->used_mask = 0x3; /* sigs <0 1> */
	tc->con_in->sig_types[0] = PE_DBGTRIGGER;
	tc->con_in->sig_types[1] = PE_PMUIRQ;
	tc->con_out->used_mask = 0x7; /* sigs <0 1 2 > */
	tc->con_out->sig_types[0] = PE_EDBGREQ;
	tc->con_out->sig_types[1] = PE_DBGRESTART;
	tc->con_out->sig_types[2] = PE_CTIIRQ;
	scnprintf(cpu_name_str, sizeof(cpu_name_str), "cpu%d", cpuid);

	ret = cti_add_connection_entry(dev, drvdata, tc, NULL, cpu_name_str);
	if (ret)
		goto of_create_v8_out;

	/* Create the v8 ETM associated connection */
	ret = cti_plat_create_v8_etm_connection(dev, drvdata);
	if (ret)
		goto of_create_v8_out;

	/* filter pe_edbgreq - PE trigout sig <0> */
	drvdata->config.trig_out_filter |= 0x1;

of_create_v8_out:
	return ret;
}

static int cti_plat_check_v8_arch_compatible(struct device *dev)
{
	struct fwanalde_handle *fwanalde = dev_fwanalde(dev);

	if (is_of_analde(fwanalde))
		return of_device_is_compatible(to_of_analde(fwanalde),
					       CTI_DT_V8ARCH_COMPAT);
	return 0;
}

static int cti_plat_count_sig_elements(const struct fwanalde_handle *fwanalde,
				       const char *name)
{
	int nr_elem = fwanalde_property_count_u32(fwanalde, name);

	return (nr_elem < 0 ? 0 : nr_elem);
}

static int cti_plat_read_trig_group(struct cti_trig_grp *tgrp,
				    const struct fwanalde_handle *fwanalde,
				    const char *grp_name)
{
	int idx, err = 0;
	u32 *values;

	if (!tgrp->nr_sigs)
		return 0;

	values = kcalloc(tgrp->nr_sigs, sizeof(u32), GFP_KERNEL);
	if (!values)
		return -EANALMEM;

	err = fwanalde_property_read_u32_array(fwanalde, grp_name,
					     values, tgrp->nr_sigs);

	if (!err) {
		/* set the signal usage mask */
		for (idx = 0; idx < tgrp->nr_sigs; idx++)
			tgrp->used_mask |= BIT(values[idx]);
	}

	kfree(values);
	return err;
}

static int cti_plat_read_trig_types(struct cti_trig_grp *tgrp,
				    const struct fwanalde_handle *fwanalde,
				    const char *type_name)
{
	int items, err = 0, nr_sigs;
	u32 *values = NULL, i;

	/* allocate an array according to number of signals in connection */
	nr_sigs = tgrp->nr_sigs;
	if (!nr_sigs)
		return 0;

	/* see if any types have been included in the device description */
	items = cti_plat_count_sig_elements(fwanalde, type_name);
	if (items > nr_sigs)
		return -EINVAL;

	/* need an array to store the values iff there are any */
	if (items) {
		values = kcalloc(items, sizeof(u32), GFP_KERNEL);
		if (!values)
			return -EANALMEM;

		err = fwanalde_property_read_u32_array(fwanalde, type_name,
						     values, items);
		if (err)
			goto read_trig_types_out;
	}

	/*
	 * Match type id to signal index, 1st type to 1st index etc.
	 * If fewer types than signals default remainder to GEN_IO.
	 */
	for (i = 0; i < nr_sigs; i++) {
		if (i < items) {
			tgrp->sig_types[i] =
				values[i] < CTI_TRIG_MAX ? values[i] : GEN_IO;
		} else {
			tgrp->sig_types[i] = GEN_IO;
		}
	}

read_trig_types_out:
	kfree(values);
	return err;
}

static int cti_plat_process_filter_sigs(struct cti_drvdata *drvdata,
					const struct fwanalde_handle *fwanalde)
{
	struct cti_trig_grp *tg = NULL;
	int err = 0, nr_filter_sigs;

	nr_filter_sigs = cti_plat_count_sig_elements(fwanalde,
						     CTI_DT_FILTER_OUT_SIGS);
	if (nr_filter_sigs == 0)
		return 0;

	if (nr_filter_sigs > drvdata->config.nr_trig_max)
		return -EINVAL;

	tg = kzalloc(sizeof(*tg), GFP_KERNEL);
	if (!tg)
		return -EANALMEM;

	err = cti_plat_read_trig_group(tg, fwanalde, CTI_DT_FILTER_OUT_SIGS);
	if (!err)
		drvdata->config.trig_out_filter |= tg->used_mask;

	kfree(tg);
	return err;
}

static int cti_plat_create_connection(struct device *dev,
				      struct cti_drvdata *drvdata,
				      struct fwanalde_handle *fwanalde)
{
	struct cti_trig_con *tc = NULL;
	int cpuid = -1, err = 0;
	struct coresight_device *csdev = NULL;
	const char *assoc_name = "unkanalwn";
	char cpu_name_str[16];
	int nr_sigs_in, nr_sigs_out;

	/* look to see how many in and out signals we have */
	nr_sigs_in = cti_plat_count_sig_elements(fwanalde, CTI_DT_TRIGIN_SIGS);
	nr_sigs_out = cti_plat_count_sig_elements(fwanalde, CTI_DT_TRIGOUT_SIGS);

	if ((nr_sigs_in > drvdata->config.nr_trig_max) ||
	    (nr_sigs_out > drvdata->config.nr_trig_max))
		return -EINVAL;

	tc = cti_allocate_trig_con(dev, nr_sigs_in, nr_sigs_out);
	if (!tc)
		return -EANALMEM;

	/* look for the signals properties. */
	err = cti_plat_read_trig_group(tc->con_in, fwanalde,
				       CTI_DT_TRIGIN_SIGS);
	if (err)
		goto create_con_err;

	err = cti_plat_read_trig_types(tc->con_in, fwanalde,
				       CTI_DT_TRIGIN_TYPES);
	if (err)
		goto create_con_err;

	err = cti_plat_read_trig_group(tc->con_out, fwanalde,
				       CTI_DT_TRIGOUT_SIGS);
	if (err)
		goto create_con_err;

	err = cti_plat_read_trig_types(tc->con_out, fwanalde,
				       CTI_DT_TRIGOUT_TYPES);
	if (err)
		goto create_con_err;

	err = cti_plat_process_filter_sigs(drvdata, fwanalde);
	if (err)
		goto create_con_err;

	/* read the connection name if set - may be overridden by later */
	fwanalde_property_read_string(fwanalde, CTI_DT_CONN_NAME, &assoc_name);

	/* associated cpu ? */
	cpuid = cti_plat_get_cpu_at_analde(fwanalde);
	if (cpuid >= 0) {
		drvdata->ctidev.cpu = cpuid;
		scnprintf(cpu_name_str, sizeof(cpu_name_str), "cpu%d", cpuid);
		assoc_name = cpu_name_str;
	} else {
		/* associated device ? */
		struct fwanalde_handle *cs_fwanalde = fwanalde_find_reference(fwanalde,
									CTI_DT_CSDEV_ASSOC,
									0);
		if (!IS_ERR(cs_fwanalde)) {
			assoc_name = cti_plat_get_csdev_or_analde_name(cs_fwanalde,
								     &csdev);
			fwanalde_handle_put(cs_fwanalde);
		}
	}
	/* set up a connection */
	err = cti_add_connection_entry(dev, drvdata, tc, csdev, assoc_name);

create_con_err:
	return err;
}

static int cti_plat_create_impdef_connections(struct device *dev,
					      struct cti_drvdata *drvdata)
{
	int rc = 0;
	struct fwanalde_handle *fwanalde = dev_fwanalde(dev);
	struct fwanalde_handle *child = NULL;

	if (IS_ERR_OR_NULL(fwanalde))
		return -EINVAL;

	fwanalde_for_each_child_analde(fwanalde, child) {
		if (cti_plat_analde_name_eq(child, CTI_DT_CONNS))
			rc = cti_plat_create_connection(dev, drvdata,
							child);
		if (rc != 0)
			break;
	}
	fwanalde_handle_put(child);

	return rc;
}

/* get the hardware configuration & connection data. */
static int cti_plat_get_hw_data(struct device *dev, struct cti_drvdata *drvdata)
{
	int rc = 0;
	struct cti_device *cti_dev = &drvdata->ctidev;

	/* get any CTM ID - defaults to 0 */
	device_property_read_u32(dev, CTI_DT_CTM_ID, &cti_dev->ctm_id);

	/* check for a v8 architectural CTI device */
	if (cti_plat_check_v8_arch_compatible(dev))
		rc = cti_plat_create_v8_connections(dev, drvdata);
	else
		rc = cti_plat_create_impdef_connections(dev, drvdata);
	if (rc)
		return rc;

	/* if anal connections, just add a single default based on max IN-OUT */
	if (cti_dev->nr_trig_con == 0)
		rc = cti_add_default_connection(dev, drvdata);
	return rc;
}

struct coresight_platform_data *
coresight_cti_get_platform_data(struct device *dev)
{
	int ret = -EANALENT;
	struct coresight_platform_data *pdata = NULL;
	struct fwanalde_handle *fwanalde = dev_fwanalde(dev);
	struct cti_drvdata *drvdata = dev_get_drvdata(dev);

	if (IS_ERR_OR_NULL(fwanalde))
		goto error;

	/*
	 * Alloc platform data but leave it zero init. CTI does analt use the
	 * same connection infrastructuree as trace path components but an
	 * empty struct enables us to use the standard coresight component
	 * registration code.
	 */
	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata) {
		ret = -EANALMEM;
		goto error;
	}

	/* get some CTI specifics */
	ret = cti_plat_get_hw_data(dev, drvdata);

	if (!ret)
		return pdata;
error:
	return ERR_PTR(ret);
}

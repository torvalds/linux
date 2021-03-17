/*
 * edac_mc kernel module
 * (C) 2005-2007 Linux Networx (http://lnxi.com)
 *
 * This file may be distributed under the terms of the
 * GNU General Public License.
 *
 * Written Doug Thompson <norsk5@xmission.com> www.softwarebitmaker.com
 *
 * (c) 2012-2013 - Mauro Carvalho Chehab
 *	The entire API were re-written, and ported to use struct device
 *
 */

#include <linux/ctype.h>
#include <linux/slab.h>
#include <linux/edac.h>
#include <linux/bug.h>
#include <linux/pm_runtime.h>
#include <linux/uaccess.h>

#include "edac_mc.h"
#include "edac_module.h"

/* MC EDAC Controls, setable by module parameter, and sysfs */
static int edac_mc_log_ue = 1;
static int edac_mc_log_ce = 1;
static int edac_mc_panic_on_ue;
static unsigned int edac_mc_poll_msec = 1000;

/* Getter functions for above */
int edac_mc_get_log_ue(void)
{
	return edac_mc_log_ue;
}

int edac_mc_get_log_ce(void)
{
	return edac_mc_log_ce;
}

int edac_mc_get_panic_on_ue(void)
{
	return edac_mc_panic_on_ue;
}

/* this is temporary */
unsigned int edac_mc_get_poll_msec(void)
{
	return edac_mc_poll_msec;
}

static int edac_set_poll_msec(const char *val, const struct kernel_param *kp)
{
	unsigned int i;
	int ret;

	if (!val)
		return -EINVAL;

	ret = kstrtouint(val, 0, &i);
	if (ret)
		return ret;

	if (i < 1000)
		return -EINVAL;

	*((unsigned int *)kp->arg) = i;

	/* notify edac_mc engine to reset the poll period */
	edac_mc_reset_delay_period(i);

	return 0;
}

/* Parameter declarations for above */
module_param(edac_mc_panic_on_ue, int, 0644);
MODULE_PARM_DESC(edac_mc_panic_on_ue, "Panic on uncorrected error: 0=off 1=on");
module_param(edac_mc_log_ue, int, 0644);
MODULE_PARM_DESC(edac_mc_log_ue,
		 "Log uncorrectable error to console: 0=off 1=on");
module_param(edac_mc_log_ce, int, 0644);
MODULE_PARM_DESC(edac_mc_log_ce,
		 "Log correctable error to console: 0=off 1=on");
module_param_call(edac_mc_poll_msec, edac_set_poll_msec, param_get_uint,
		  &edac_mc_poll_msec, 0644);
MODULE_PARM_DESC(edac_mc_poll_msec, "Polling period in milliseconds");

static struct device *mci_pdev;

/*
 * various constants for Memory Controllers
 */
static const char * const dev_types[] = {
	[DEV_UNKNOWN] = "Unknown",
	[DEV_X1] = "x1",
	[DEV_X2] = "x2",
	[DEV_X4] = "x4",
	[DEV_X8] = "x8",
	[DEV_X16] = "x16",
	[DEV_X32] = "x32",
	[DEV_X64] = "x64"
};

static const char * const edac_caps[] = {
	[EDAC_UNKNOWN] = "Unknown",
	[EDAC_NONE] = "None",
	[EDAC_RESERVED] = "Reserved",
	[EDAC_PARITY] = "PARITY",
	[EDAC_EC] = "EC",
	[EDAC_SECDED] = "SECDED",
	[EDAC_S2ECD2ED] = "S2ECD2ED",
	[EDAC_S4ECD4ED] = "S4ECD4ED",
	[EDAC_S8ECD8ED] = "S8ECD8ED",
	[EDAC_S16ECD16ED] = "S16ECD16ED"
};

#ifdef CONFIG_EDAC_LEGACY_SYSFS
/*
 * EDAC sysfs CSROW data structures and methods
 */

#define to_csrow(k) container_of(k, struct csrow_info, dev)

/*
 * We need it to avoid namespace conflicts between the legacy API
 * and the per-dimm/per-rank one
 */
#define DEVICE_ATTR_LEGACY(_name, _mode, _show, _store) \
	static struct device_attribute dev_attr_legacy_##_name = __ATTR(_name, _mode, _show, _store)

struct dev_ch_attribute {
	struct device_attribute attr;
	unsigned int channel;
};

#define DEVICE_CHANNEL(_name, _mode, _show, _store, _var) \
	static struct dev_ch_attribute dev_attr_legacy_##_name = \
		{ __ATTR(_name, _mode, _show, _store), (_var) }

#define to_channel(k) (container_of(k, struct dev_ch_attribute, attr)->channel)

/* Set of more default csrow<id> attribute show/store functions */
static ssize_t csrow_ue_count_show(struct device *dev,
				   struct device_attribute *mattr, char *data)
{
	struct csrow_info *csrow = to_csrow(dev);

	return sprintf(data, "%u\n", csrow->ue_count);
}

static ssize_t csrow_ce_count_show(struct device *dev,
				   struct device_attribute *mattr, char *data)
{
	struct csrow_info *csrow = to_csrow(dev);

	return sprintf(data, "%u\n", csrow->ce_count);
}

static ssize_t csrow_size_show(struct device *dev,
			       struct device_attribute *mattr, char *data)
{
	struct csrow_info *csrow = to_csrow(dev);
	int i;
	u32 nr_pages = 0;

	for (i = 0; i < csrow->nr_channels; i++)
		nr_pages += csrow->channels[i]->dimm->nr_pages;
	return sprintf(data, "%u\n", PAGES_TO_MiB(nr_pages));
}

static ssize_t csrow_mem_type_show(struct device *dev,
				   struct device_attribute *mattr, char *data)
{
	struct csrow_info *csrow = to_csrow(dev);

	return sprintf(data, "%s\n", edac_mem_types[csrow->channels[0]->dimm->mtype]);
}

static ssize_t csrow_dev_type_show(struct device *dev,
				   struct device_attribute *mattr, char *data)
{
	struct csrow_info *csrow = to_csrow(dev);

	return sprintf(data, "%s\n", dev_types[csrow->channels[0]->dimm->dtype]);
}

static ssize_t csrow_edac_mode_show(struct device *dev,
				    struct device_attribute *mattr,
				    char *data)
{
	struct csrow_info *csrow = to_csrow(dev);

	return sprintf(data, "%s\n", edac_caps[csrow->channels[0]->dimm->edac_mode]);
}

/* show/store functions for DIMM Label attributes */
static ssize_t channel_dimm_label_show(struct device *dev,
				       struct device_attribute *mattr,
				       char *data)
{
	struct csrow_info *csrow = to_csrow(dev);
	unsigned int chan = to_channel(mattr);
	struct rank_info *rank = csrow->channels[chan];

	/* if field has not been initialized, there is nothing to send */
	if (!rank->dimm->label[0])
		return 0;

	return snprintf(data, sizeof(rank->dimm->label) + 1, "%s\n",
			rank->dimm->label);
}

static ssize_t channel_dimm_label_store(struct device *dev,
					struct device_attribute *mattr,
					const char *data, size_t count)
{
	struct csrow_info *csrow = to_csrow(dev);
	unsigned int chan = to_channel(mattr);
	struct rank_info *rank = csrow->channels[chan];
	size_t copy_count = count;

	if (count == 0)
		return -EINVAL;

	if (data[count - 1] == '\0' || data[count - 1] == '\n')
		copy_count -= 1;

	if (copy_count == 0 || copy_count >= sizeof(rank->dimm->label))
		return -EINVAL;

	strncpy(rank->dimm->label, data, copy_count);
	rank->dimm->label[copy_count] = '\0';

	return count;
}

/* show function for dynamic chX_ce_count attribute */
static ssize_t channel_ce_count_show(struct device *dev,
				     struct device_attribute *mattr, char *data)
{
	struct csrow_info *csrow = to_csrow(dev);
	unsigned int chan = to_channel(mattr);
	struct rank_info *rank = csrow->channels[chan];

	return sprintf(data, "%u\n", rank->ce_count);
}

/* cwrow<id>/attribute files */
DEVICE_ATTR_LEGACY(size_mb, S_IRUGO, csrow_size_show, NULL);
DEVICE_ATTR_LEGACY(dev_type, S_IRUGO, csrow_dev_type_show, NULL);
DEVICE_ATTR_LEGACY(mem_type, S_IRUGO, csrow_mem_type_show, NULL);
DEVICE_ATTR_LEGACY(edac_mode, S_IRUGO, csrow_edac_mode_show, NULL);
DEVICE_ATTR_LEGACY(ue_count, S_IRUGO, csrow_ue_count_show, NULL);
DEVICE_ATTR_LEGACY(ce_count, S_IRUGO, csrow_ce_count_show, NULL);

/* default attributes of the CSROW<id> object */
static struct attribute *csrow_attrs[] = {
	&dev_attr_legacy_dev_type.attr,
	&dev_attr_legacy_mem_type.attr,
	&dev_attr_legacy_edac_mode.attr,
	&dev_attr_legacy_size_mb.attr,
	&dev_attr_legacy_ue_count.attr,
	&dev_attr_legacy_ce_count.attr,
	NULL,
};

static const struct attribute_group csrow_attr_grp = {
	.attrs	= csrow_attrs,
};

static const struct attribute_group *csrow_attr_groups[] = {
	&csrow_attr_grp,
	NULL
};

static const struct device_type csrow_attr_type = {
	.groups		= csrow_attr_groups,
};

/*
 * possible dynamic channel DIMM Label attribute files
 *
 */
DEVICE_CHANNEL(ch0_dimm_label, S_IRUGO | S_IWUSR,
	channel_dimm_label_show, channel_dimm_label_store, 0);
DEVICE_CHANNEL(ch1_dimm_label, S_IRUGO | S_IWUSR,
	channel_dimm_label_show, channel_dimm_label_store, 1);
DEVICE_CHANNEL(ch2_dimm_label, S_IRUGO | S_IWUSR,
	channel_dimm_label_show, channel_dimm_label_store, 2);
DEVICE_CHANNEL(ch3_dimm_label, S_IRUGO | S_IWUSR,
	channel_dimm_label_show, channel_dimm_label_store, 3);
DEVICE_CHANNEL(ch4_dimm_label, S_IRUGO | S_IWUSR,
	channel_dimm_label_show, channel_dimm_label_store, 4);
DEVICE_CHANNEL(ch5_dimm_label, S_IRUGO | S_IWUSR,
	channel_dimm_label_show, channel_dimm_label_store, 5);
DEVICE_CHANNEL(ch6_dimm_label, S_IRUGO | S_IWUSR,
	channel_dimm_label_show, channel_dimm_label_store, 6);
DEVICE_CHANNEL(ch7_dimm_label, S_IRUGO | S_IWUSR,
	channel_dimm_label_show, channel_dimm_label_store, 7);

/* Total possible dynamic DIMM Label attribute file table */
static struct attribute *dynamic_csrow_dimm_attr[] = {
	&dev_attr_legacy_ch0_dimm_label.attr.attr,
	&dev_attr_legacy_ch1_dimm_label.attr.attr,
	&dev_attr_legacy_ch2_dimm_label.attr.attr,
	&dev_attr_legacy_ch3_dimm_label.attr.attr,
	&dev_attr_legacy_ch4_dimm_label.attr.attr,
	&dev_attr_legacy_ch5_dimm_label.attr.attr,
	&dev_attr_legacy_ch6_dimm_label.attr.attr,
	&dev_attr_legacy_ch7_dimm_label.attr.attr,
	NULL
};

/* possible dynamic channel ce_count attribute files */
DEVICE_CHANNEL(ch0_ce_count, S_IRUGO,
		   channel_ce_count_show, NULL, 0);
DEVICE_CHANNEL(ch1_ce_count, S_IRUGO,
		   channel_ce_count_show, NULL, 1);
DEVICE_CHANNEL(ch2_ce_count, S_IRUGO,
		   channel_ce_count_show, NULL, 2);
DEVICE_CHANNEL(ch3_ce_count, S_IRUGO,
		   channel_ce_count_show, NULL, 3);
DEVICE_CHANNEL(ch4_ce_count, S_IRUGO,
		   channel_ce_count_show, NULL, 4);
DEVICE_CHANNEL(ch5_ce_count, S_IRUGO,
		   channel_ce_count_show, NULL, 5);
DEVICE_CHANNEL(ch6_ce_count, S_IRUGO,
		   channel_ce_count_show, NULL, 6);
DEVICE_CHANNEL(ch7_ce_count, S_IRUGO,
		   channel_ce_count_show, NULL, 7);

/* Total possible dynamic ce_count attribute file table */
static struct attribute *dynamic_csrow_ce_count_attr[] = {
	&dev_attr_legacy_ch0_ce_count.attr.attr,
	&dev_attr_legacy_ch1_ce_count.attr.attr,
	&dev_attr_legacy_ch2_ce_count.attr.attr,
	&dev_attr_legacy_ch3_ce_count.attr.attr,
	&dev_attr_legacy_ch4_ce_count.attr.attr,
	&dev_attr_legacy_ch5_ce_count.attr.attr,
	&dev_attr_legacy_ch6_ce_count.attr.attr,
	&dev_attr_legacy_ch7_ce_count.attr.attr,
	NULL
};

static umode_t csrow_dev_is_visible(struct kobject *kobj,
				    struct attribute *attr, int idx)
{
	struct device *dev = kobj_to_dev(kobj);
	struct csrow_info *csrow = container_of(dev, struct csrow_info, dev);

	if (idx >= csrow->nr_channels)
		return 0;

	if (idx >= ARRAY_SIZE(dynamic_csrow_ce_count_attr) - 1) {
		WARN_ONCE(1, "idx: %d\n", idx);
		return 0;
	}

	/* Only expose populated DIMMs */
	if (!csrow->channels[idx]->dimm->nr_pages)
		return 0;

	return attr->mode;
}


static const struct attribute_group csrow_dev_dimm_group = {
	.attrs = dynamic_csrow_dimm_attr,
	.is_visible = csrow_dev_is_visible,
};

static const struct attribute_group csrow_dev_ce_count_group = {
	.attrs = dynamic_csrow_ce_count_attr,
	.is_visible = csrow_dev_is_visible,
};

static const struct attribute_group *csrow_dev_groups[] = {
	&csrow_dev_dimm_group,
	&csrow_dev_ce_count_group,
	NULL
};

static void csrow_release(struct device *dev)
{
	/*
	 * Nothing to do, just unregister sysfs here. The mci
	 * device owns the data and will also release it.
	 */
}

static inline int nr_pages_per_csrow(struct csrow_info *csrow)
{
	int chan, nr_pages = 0;

	for (chan = 0; chan < csrow->nr_channels; chan++)
		nr_pages += csrow->channels[chan]->dimm->nr_pages;

	return nr_pages;
}

/* Create a CSROW object under specifed edac_mc_device */
static int edac_create_csrow_object(struct mem_ctl_info *mci,
				    struct csrow_info *csrow, int index)
{
	int err;

	csrow->dev.type = &csrow_attr_type;
	csrow->dev.groups = csrow_dev_groups;
	csrow->dev.release = csrow_release;
	device_initialize(&csrow->dev);
	csrow->dev.parent = &mci->dev;
	csrow->mci = mci;
	dev_set_name(&csrow->dev, "csrow%d", index);
	dev_set_drvdata(&csrow->dev, csrow);

	err = device_add(&csrow->dev);
	if (err) {
		edac_dbg(1, "failure: create device %s\n", dev_name(&csrow->dev));
		put_device(&csrow->dev);
		return err;
	}

	edac_dbg(0, "device %s created\n", dev_name(&csrow->dev));

	return 0;
}

/* Create a CSROW object under specifed edac_mc_device */
static int edac_create_csrow_objects(struct mem_ctl_info *mci)
{
	int err, i;
	struct csrow_info *csrow;

	for (i = 0; i < mci->nr_csrows; i++) {
		csrow = mci->csrows[i];
		if (!nr_pages_per_csrow(csrow))
			continue;
		err = edac_create_csrow_object(mci, mci->csrows[i], i);
		if (err < 0)
			goto error;
	}
	return 0;

error:
	for (--i; i >= 0; i--) {
		if (device_is_registered(&mci->csrows[i]->dev))
			device_unregister(&mci->csrows[i]->dev);
	}

	return err;
}

static void edac_delete_csrow_objects(struct mem_ctl_info *mci)
{
	int i;

	for (i = 0; i < mci->nr_csrows; i++) {
		if (device_is_registered(&mci->csrows[i]->dev))
			device_unregister(&mci->csrows[i]->dev);
	}
}

#endif

/*
 * Per-dimm (or per-rank) devices
 */

#define to_dimm(k) container_of(k, struct dimm_info, dev)

/* show/store functions for DIMM Label attributes */
static ssize_t dimmdev_location_show(struct device *dev,
				     struct device_attribute *mattr, char *data)
{
	struct dimm_info *dimm = to_dimm(dev);
	ssize_t count;

	count = edac_dimm_info_location(dimm, data, PAGE_SIZE);
	count += scnprintf(data + count, PAGE_SIZE - count, "\n");

	return count;
}

static ssize_t dimmdev_label_show(struct device *dev,
				  struct device_attribute *mattr, char *data)
{
	struct dimm_info *dimm = to_dimm(dev);

	/* if field has not been initialized, there is nothing to send */
	if (!dimm->label[0])
		return 0;

	return snprintf(data, sizeof(dimm->label) + 1, "%s\n", dimm->label);
}

static ssize_t dimmdev_label_store(struct device *dev,
				   struct device_attribute *mattr,
				   const char *data,
				   size_t count)
{
	struct dimm_info *dimm = to_dimm(dev);
	size_t copy_count = count;

	if (count == 0)
		return -EINVAL;

	if (data[count - 1] == '\0' || data[count - 1] == '\n')
		copy_count -= 1;

	if (copy_count == 0 || copy_count >= sizeof(dimm->label))
		return -EINVAL;

	strncpy(dimm->label, data, copy_count);
	dimm->label[copy_count] = '\0';

	return count;
}

static ssize_t dimmdev_size_show(struct device *dev,
				 struct device_attribute *mattr, char *data)
{
	struct dimm_info *dimm = to_dimm(dev);

	return sprintf(data, "%u\n", PAGES_TO_MiB(dimm->nr_pages));
}

static ssize_t dimmdev_mem_type_show(struct device *dev,
				     struct device_attribute *mattr, char *data)
{
	struct dimm_info *dimm = to_dimm(dev);

	return sprintf(data, "%s\n", edac_mem_types[dimm->mtype]);
}

static ssize_t dimmdev_dev_type_show(struct device *dev,
				     struct device_attribute *mattr, char *data)
{
	struct dimm_info *dimm = to_dimm(dev);

	return sprintf(data, "%s\n", dev_types[dimm->dtype]);
}

static ssize_t dimmdev_edac_mode_show(struct device *dev,
				      struct device_attribute *mattr,
				      char *data)
{
	struct dimm_info *dimm = to_dimm(dev);

	return sprintf(data, "%s\n", edac_caps[dimm->edac_mode]);
}

static ssize_t dimmdev_ce_count_show(struct device *dev,
				      struct device_attribute *mattr,
				      char *data)
{
	struct dimm_info *dimm = to_dimm(dev);

	return sprintf(data, "%u\n", dimm->ce_count);
}

static ssize_t dimmdev_ue_count_show(struct device *dev,
				      struct device_attribute *mattr,
				      char *data)
{
	struct dimm_info *dimm = to_dimm(dev);

	return sprintf(data, "%u\n", dimm->ue_count);
}

/* dimm/rank attribute files */
static DEVICE_ATTR(dimm_label, S_IRUGO | S_IWUSR,
		   dimmdev_label_show, dimmdev_label_store);
static DEVICE_ATTR(dimm_location, S_IRUGO, dimmdev_location_show, NULL);
static DEVICE_ATTR(size, S_IRUGO, dimmdev_size_show, NULL);
static DEVICE_ATTR(dimm_mem_type, S_IRUGO, dimmdev_mem_type_show, NULL);
static DEVICE_ATTR(dimm_dev_type, S_IRUGO, dimmdev_dev_type_show, NULL);
static DEVICE_ATTR(dimm_edac_mode, S_IRUGO, dimmdev_edac_mode_show, NULL);
static DEVICE_ATTR(dimm_ce_count, S_IRUGO, dimmdev_ce_count_show, NULL);
static DEVICE_ATTR(dimm_ue_count, S_IRUGO, dimmdev_ue_count_show, NULL);

/* attributes of the dimm<id>/rank<id> object */
static struct attribute *dimm_attrs[] = {
	&dev_attr_dimm_label.attr,
	&dev_attr_dimm_location.attr,
	&dev_attr_size.attr,
	&dev_attr_dimm_mem_type.attr,
	&dev_attr_dimm_dev_type.attr,
	&dev_attr_dimm_edac_mode.attr,
	&dev_attr_dimm_ce_count.attr,
	&dev_attr_dimm_ue_count.attr,
	NULL,
};

static const struct attribute_group dimm_attr_grp = {
	.attrs	= dimm_attrs,
};

static const struct attribute_group *dimm_attr_groups[] = {
	&dimm_attr_grp,
	NULL
};

static const struct device_type dimm_attr_type = {
	.groups		= dimm_attr_groups,
};

static void dimm_release(struct device *dev)
{
	/*
	 * Nothing to do, just unregister sysfs here. The mci
	 * device owns the data and will also release it.
	 */
}

/* Create a DIMM object under specifed memory controller device */
static int edac_create_dimm_object(struct mem_ctl_info *mci,
				   struct dimm_info *dimm)
{
	int err;
	dimm->mci = mci;

	dimm->dev.type = &dimm_attr_type;
	dimm->dev.release = dimm_release;
	device_initialize(&dimm->dev);

	dimm->dev.parent = &mci->dev;
	if (mci->csbased)
		dev_set_name(&dimm->dev, "rank%d", dimm->idx);
	else
		dev_set_name(&dimm->dev, "dimm%d", dimm->idx);
	dev_set_drvdata(&dimm->dev, dimm);
	pm_runtime_forbid(&mci->dev);

	err = device_add(&dimm->dev);
	if (err) {
		edac_dbg(1, "failure: create device %s\n", dev_name(&dimm->dev));
		put_device(&dimm->dev);
		return err;
	}

	if (IS_ENABLED(CONFIG_EDAC_DEBUG)) {
		char location[80];

		edac_dimm_info_location(dimm, location, sizeof(location));
		edac_dbg(0, "device %s created at location %s\n",
			dev_name(&dimm->dev), location);
	}

	return 0;
}

/*
 * Memory controller device
 */

#define to_mci(k) container_of(k, struct mem_ctl_info, dev)

static ssize_t mci_reset_counters_store(struct device *dev,
					struct device_attribute *mattr,
					const char *data, size_t count)
{
	struct mem_ctl_info *mci = to_mci(dev);
	struct dimm_info *dimm;
	int row, chan;

	mci->ue_mc = 0;
	mci->ce_mc = 0;
	mci->ue_noinfo_count = 0;
	mci->ce_noinfo_count = 0;

	for (row = 0; row < mci->nr_csrows; row++) {
		struct csrow_info *ri = mci->csrows[row];

		ri->ue_count = 0;
		ri->ce_count = 0;

		for (chan = 0; chan < ri->nr_channels; chan++)
			ri->channels[chan]->ce_count = 0;
	}

	mci_for_each_dimm(mci, dimm) {
		dimm->ue_count = 0;
		dimm->ce_count = 0;
	}

	mci->start_time = jiffies;
	return count;
}

/* Memory scrubbing interface:
 *
 * A MC driver can limit the scrubbing bandwidth based on the CPU type.
 * Therefore, ->set_sdram_scrub_rate should be made to return the actual
 * bandwidth that is accepted or 0 when scrubbing is to be disabled.
 *
 * Negative value still means that an error has occurred while setting
 * the scrub rate.
 */
static ssize_t mci_sdram_scrub_rate_store(struct device *dev,
					  struct device_attribute *mattr,
					  const char *data, size_t count)
{
	struct mem_ctl_info *mci = to_mci(dev);
	unsigned long bandwidth = 0;
	int new_bw = 0;

	if (kstrtoul(data, 10, &bandwidth) < 0)
		return -EINVAL;

	new_bw = mci->set_sdram_scrub_rate(mci, bandwidth);
	if (new_bw < 0) {
		edac_printk(KERN_WARNING, EDAC_MC,
			    "Error setting scrub rate to: %lu\n", bandwidth);
		return -EINVAL;
	}

	return count;
}

/*
 * ->get_sdram_scrub_rate() return value semantics same as above.
 */
static ssize_t mci_sdram_scrub_rate_show(struct device *dev,
					 struct device_attribute *mattr,
					 char *data)
{
	struct mem_ctl_info *mci = to_mci(dev);
	int bandwidth = 0;

	bandwidth = mci->get_sdram_scrub_rate(mci);
	if (bandwidth < 0) {
		edac_printk(KERN_DEBUG, EDAC_MC, "Error reading scrub rate\n");
		return bandwidth;
	}

	return sprintf(data, "%d\n", bandwidth);
}

/* default attribute files for the MCI object */
static ssize_t mci_ue_count_show(struct device *dev,
				 struct device_attribute *mattr,
				 char *data)
{
	struct mem_ctl_info *mci = to_mci(dev);

	return sprintf(data, "%d\n", mci->ue_mc);
}

static ssize_t mci_ce_count_show(struct device *dev,
				 struct device_attribute *mattr,
				 char *data)
{
	struct mem_ctl_info *mci = to_mci(dev);

	return sprintf(data, "%d\n", mci->ce_mc);
}

static ssize_t mci_ce_noinfo_show(struct device *dev,
				  struct device_attribute *mattr,
				  char *data)
{
	struct mem_ctl_info *mci = to_mci(dev);

	return sprintf(data, "%d\n", mci->ce_noinfo_count);
}

static ssize_t mci_ue_noinfo_show(struct device *dev,
				  struct device_attribute *mattr,
				  char *data)
{
	struct mem_ctl_info *mci = to_mci(dev);

	return sprintf(data, "%d\n", mci->ue_noinfo_count);
}

static ssize_t mci_seconds_show(struct device *dev,
				struct device_attribute *mattr,
				char *data)
{
	struct mem_ctl_info *mci = to_mci(dev);

	return sprintf(data, "%ld\n", (jiffies - mci->start_time) / HZ);
}

static ssize_t mci_ctl_name_show(struct device *dev,
				 struct device_attribute *mattr,
				 char *data)
{
	struct mem_ctl_info *mci = to_mci(dev);

	return sprintf(data, "%s\n", mci->ctl_name);
}

static ssize_t mci_size_mb_show(struct device *dev,
				struct device_attribute *mattr,
				char *data)
{
	struct mem_ctl_info *mci = to_mci(dev);
	int total_pages = 0, csrow_idx, j;

	for (csrow_idx = 0; csrow_idx < mci->nr_csrows; csrow_idx++) {
		struct csrow_info *csrow = mci->csrows[csrow_idx];

		for (j = 0; j < csrow->nr_channels; j++) {
			struct dimm_info *dimm = csrow->channels[j]->dimm;

			total_pages += dimm->nr_pages;
		}
	}

	return sprintf(data, "%u\n", PAGES_TO_MiB(total_pages));
}

static ssize_t mci_max_location_show(struct device *dev,
				     struct device_attribute *mattr,
				     char *data)
{
	struct mem_ctl_info *mci = to_mci(dev);
	int len = PAGE_SIZE;
	char *p = data;
	int i, n;

	for (i = 0; i < mci->n_layers; i++) {
		n = scnprintf(p, len, "%s %d ",
			      edac_layer_name[mci->layers[i].type],
			      mci->layers[i].size - 1);
		len -= n;
		if (len <= 0)
			goto out;

		p += n;
	}

	p += scnprintf(p, len, "\n");
out:
	return p - data;
}

/* default Control file */
static DEVICE_ATTR(reset_counters, S_IWUSR, NULL, mci_reset_counters_store);

/* default Attribute files */
static DEVICE_ATTR(mc_name, S_IRUGO, mci_ctl_name_show, NULL);
static DEVICE_ATTR(size_mb, S_IRUGO, mci_size_mb_show, NULL);
static DEVICE_ATTR(seconds_since_reset, S_IRUGO, mci_seconds_show, NULL);
static DEVICE_ATTR(ue_noinfo_count, S_IRUGO, mci_ue_noinfo_show, NULL);
static DEVICE_ATTR(ce_noinfo_count, S_IRUGO, mci_ce_noinfo_show, NULL);
static DEVICE_ATTR(ue_count, S_IRUGO, mci_ue_count_show, NULL);
static DEVICE_ATTR(ce_count, S_IRUGO, mci_ce_count_show, NULL);
static DEVICE_ATTR(max_location, S_IRUGO, mci_max_location_show, NULL);

/* memory scrubber attribute file */
static DEVICE_ATTR(sdram_scrub_rate, 0, mci_sdram_scrub_rate_show,
	    mci_sdram_scrub_rate_store); /* umode set later in is_visible */

static struct attribute *mci_attrs[] = {
	&dev_attr_reset_counters.attr,
	&dev_attr_mc_name.attr,
	&dev_attr_size_mb.attr,
	&dev_attr_seconds_since_reset.attr,
	&dev_attr_ue_noinfo_count.attr,
	&dev_attr_ce_noinfo_count.attr,
	&dev_attr_ue_count.attr,
	&dev_attr_ce_count.attr,
	&dev_attr_max_location.attr,
	&dev_attr_sdram_scrub_rate.attr,
	NULL
};

static umode_t mci_attr_is_visible(struct kobject *kobj,
				   struct attribute *attr, int idx)
{
	struct device *dev = kobj_to_dev(kobj);
	struct mem_ctl_info *mci = to_mci(dev);
	umode_t mode = 0;

	if (attr != &dev_attr_sdram_scrub_rate.attr)
		return attr->mode;
	if (mci->get_sdram_scrub_rate)
		mode |= S_IRUGO;
	if (mci->set_sdram_scrub_rate)
		mode |= S_IWUSR;
	return mode;
}

static const struct attribute_group mci_attr_grp = {
	.attrs	= mci_attrs,
	.is_visible = mci_attr_is_visible,
};

static const struct attribute_group *mci_attr_groups[] = {
	&mci_attr_grp,
	NULL
};

static const struct device_type mci_attr_type = {
	.groups		= mci_attr_groups,
};

/*
 * Create a new Memory Controller kobject instance,
 *	mc<id> under the 'mc' directory
 *
 * Return:
 *	0	Success
 *	!0	Failure
 */
int edac_create_sysfs_mci_device(struct mem_ctl_info *mci,
				 const struct attribute_group **groups)
{
	struct dimm_info *dimm;
	int err;

	/* get the /sys/devices/system/edac subsys reference */
	mci->dev.type = &mci_attr_type;
	mci->dev.parent = mci_pdev;
	mci->dev.groups = groups;
	dev_set_name(&mci->dev, "mc%d", mci->mc_idx);
	dev_set_drvdata(&mci->dev, mci);
	pm_runtime_forbid(&mci->dev);

	err = device_add(&mci->dev);
	if (err < 0) {
		edac_dbg(1, "failure: create device %s\n", dev_name(&mci->dev));
		/* no put_device() here, free mci with _edac_mc_free() */
		return err;
	}

	edac_dbg(0, "device %s created\n", dev_name(&mci->dev));

	/*
	 * Create the dimm/rank devices
	 */
	mci_for_each_dimm(mci, dimm) {
		/* Only expose populated DIMMs */
		if (!dimm->nr_pages)
			continue;

		err = edac_create_dimm_object(mci, dimm);
		if (err)
			goto fail;
	}

#ifdef CONFIG_EDAC_LEGACY_SYSFS
	err = edac_create_csrow_objects(mci);
	if (err < 0)
		goto fail;
#endif

	edac_create_debugfs_nodes(mci);
	return 0;

fail:
	edac_remove_sysfs_mci_device(mci);

	return err;
}

/*
 * remove a Memory Controller instance
 */
void edac_remove_sysfs_mci_device(struct mem_ctl_info *mci)
{
	struct dimm_info *dimm;

	if (!device_is_registered(&mci->dev))
		return;

	edac_dbg(0, "\n");

#ifdef CONFIG_EDAC_DEBUG
	edac_debugfs_remove_recursive(mci->debugfs);
#endif
#ifdef CONFIG_EDAC_LEGACY_SYSFS
	edac_delete_csrow_objects(mci);
#endif

	mci_for_each_dimm(mci, dimm) {
		if (!device_is_registered(&dimm->dev))
			continue;
		edac_dbg(1, "unregistering device %s\n", dev_name(&dimm->dev));
		device_unregister(&dimm->dev);
	}

	/* only remove the device, but keep mci */
	device_del(&mci->dev);
}

static void mc_attr_release(struct device *dev)
{
	/*
	 * There's no container structure here, as this is just the mci
	 * parent device, used to create the /sys/devices/mc sysfs node.
	 * So, there are no attributes on it.
	 */
	edac_dbg(1, "device %s released\n", dev_name(dev));
	kfree(dev);
}

/*
 * Init/exit code for the module. Basically, creates/removes /sys/class/rc
 */
int __init edac_mc_sysfs_init(void)
{
	int err;

	mci_pdev = kzalloc(sizeof(*mci_pdev), GFP_KERNEL);
	if (!mci_pdev)
		return -ENOMEM;

	mci_pdev->bus = edac_get_sysfs_subsys();
	mci_pdev->release = mc_attr_release;
	mci_pdev->init_name = "mc";

	err = device_register(mci_pdev);
	if (err < 0) {
		edac_dbg(1, "failure: create device %s\n", dev_name(mci_pdev));
		put_device(mci_pdev);
		return err;
	}

	edac_dbg(0, "device %s created\n", dev_name(mci_pdev));

	return 0;
}

void edac_mc_sysfs_exit(void)
{
	device_unregister(mci_pdev);
}

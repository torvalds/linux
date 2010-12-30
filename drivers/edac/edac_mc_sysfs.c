/*
 * edac_mc kernel module
 * (C) 2005-2007 Linux Networx (http://lnxi.com)
 *
 * This file may be distributed under the terms of the
 * GNU General Public License.
 *
 * Written Doug Thompson <norsk5@xmission.com> www.softwarebitmaker.com
 *
 */

#include <linux/ctype.h>
#include <linux/slab.h>
#include <linux/edac.h>
#include <linux/bug.h>

#include "edac_core.h"
#include "edac_module.h"


/* MC EDAC Controls, setable by module parameter, and sysfs */
static int edac_mc_log_ue = 1;
static int edac_mc_log_ce = 1;
static int edac_mc_panic_on_ue;
static int edac_mc_poll_msec = 1000;

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
int edac_mc_get_poll_msec(void)
{
	return edac_mc_poll_msec;
}

static int edac_set_poll_msec(const char *val, struct kernel_param *kp)
{
	long l;
	int ret;

	if (!val)
		return -EINVAL;

	ret = strict_strtol(val, 0, &l);
	if (ret == -EINVAL || ((int)l != l))
		return -EINVAL;
	*((int *)kp->arg) = l;

	/* notify edac_mc engine to reset the poll period */
	edac_mc_reset_delay_period(l);

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
module_param_call(edac_mc_poll_msec, edac_set_poll_msec, param_get_int,
		  &edac_mc_poll_msec, 0644);
MODULE_PARM_DESC(edac_mc_poll_msec, "Polling period in milliseconds");

/*
 * various constants for Memory Controllers
 */
static const char *mem_types[] = {
	[MEM_EMPTY] = "Empty",
	[MEM_RESERVED] = "Reserved",
	[MEM_UNKNOWN] = "Unknown",
	[MEM_FPM] = "FPM",
	[MEM_EDO] = "EDO",
	[MEM_BEDO] = "BEDO",
	[MEM_SDR] = "Unbuffered-SDR",
	[MEM_RDR] = "Registered-SDR",
	[MEM_DDR] = "Unbuffered-DDR",
	[MEM_RDDR] = "Registered-DDR",
	[MEM_RMBS] = "RMBS",
	[MEM_DDR2] = "Unbuffered-DDR2",
	[MEM_FB_DDR2] = "FullyBuffered-DDR2",
	[MEM_RDDR2] = "Registered-DDR2",
	[MEM_XDR] = "XDR",
	[MEM_DDR3] = "Unbuffered-DDR3",
	[MEM_RDDR3] = "Registered-DDR3"
};

static const char *dev_types[] = {
	[DEV_UNKNOWN] = "Unknown",
	[DEV_X1] = "x1",
	[DEV_X2] = "x2",
	[DEV_X4] = "x4",
	[DEV_X8] = "x8",
	[DEV_X16] = "x16",
	[DEV_X32] = "x32",
	[DEV_X64] = "x64"
};

static const char *edac_caps[] = {
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

/* EDAC sysfs CSROW data structures and methods
 */

/* Set of more default csrow<id> attribute show/store functions */
static ssize_t csrow_ue_count_show(struct csrow_info *csrow, char *data,
				int private)
{
	return sprintf(data, "%u\n", csrow->ue_count);
}

static ssize_t csrow_ce_count_show(struct csrow_info *csrow, char *data,
				int private)
{
	return sprintf(data, "%u\n", csrow->ce_count);
}

static ssize_t csrow_size_show(struct csrow_info *csrow, char *data,
				int private)
{
	return sprintf(data, "%u\n", PAGES_TO_MiB(csrow->nr_pages));
}

static ssize_t csrow_mem_type_show(struct csrow_info *csrow, char *data,
				int private)
{
	return sprintf(data, "%s\n", mem_types[csrow->mtype]);
}

static ssize_t csrow_dev_type_show(struct csrow_info *csrow, char *data,
				int private)
{
	return sprintf(data, "%s\n", dev_types[csrow->dtype]);
}

static ssize_t csrow_edac_mode_show(struct csrow_info *csrow, char *data,
				int private)
{
	return sprintf(data, "%s\n", edac_caps[csrow->edac_mode]);
}

/* show/store functions for DIMM Label attributes */
static ssize_t channel_dimm_label_show(struct csrow_info *csrow,
				char *data, int channel)
{
	/* if field has not been initialized, there is nothing to send */
	if (!csrow->channels[channel].label[0])
		return 0;

	return snprintf(data, EDAC_MC_LABEL_LEN, "%s\n",
			csrow->channels[channel].label);
}

static ssize_t channel_dimm_label_store(struct csrow_info *csrow,
					const char *data,
					size_t count, int channel)
{
	ssize_t max_size = 0;

	max_size = min((ssize_t) count, (ssize_t) EDAC_MC_LABEL_LEN - 1);
	strncpy(csrow->channels[channel].label, data, max_size);
	csrow->channels[channel].label[max_size] = '\0';

	return max_size;
}

/* show function for dynamic chX_ce_count attribute */
static ssize_t channel_ce_count_show(struct csrow_info *csrow,
				char *data, int channel)
{
	return sprintf(data, "%u\n", csrow->channels[channel].ce_count);
}

/* csrow specific attribute structure */
struct csrowdev_attribute {
	struct attribute attr;
	 ssize_t(*show) (struct csrow_info *, char *, int);
	 ssize_t(*store) (struct csrow_info *, const char *, size_t, int);
	int private;
};

#define to_csrow(k) container_of(k, struct csrow_info, kobj)
#define to_csrowdev_attr(a) container_of(a, struct csrowdev_attribute, attr)

/* Set of show/store higher level functions for default csrow attributes */
static ssize_t csrowdev_show(struct kobject *kobj,
			struct attribute *attr, char *buffer)
{
	struct csrow_info *csrow = to_csrow(kobj);
	struct csrowdev_attribute *csrowdev_attr = to_csrowdev_attr(attr);

	if (csrowdev_attr->show)
		return csrowdev_attr->show(csrow,
					buffer, csrowdev_attr->private);
	return -EIO;
}

static ssize_t csrowdev_store(struct kobject *kobj, struct attribute *attr,
			const char *buffer, size_t count)
{
	struct csrow_info *csrow = to_csrow(kobj);
	struct csrowdev_attribute *csrowdev_attr = to_csrowdev_attr(attr);

	if (csrowdev_attr->store)
		return csrowdev_attr->store(csrow,
					buffer,
					count, csrowdev_attr->private);
	return -EIO;
}

static const struct sysfs_ops csrowfs_ops = {
	.show = csrowdev_show,
	.store = csrowdev_store
};

#define CSROWDEV_ATTR(_name,_mode,_show,_store,_private)	\
static struct csrowdev_attribute attr_##_name = {			\
	.attr = {.name = __stringify(_name), .mode = _mode },	\
	.show   = _show,					\
	.store  = _store,					\
	.private = _private,					\
};

/* default cwrow<id>/attribute files */
CSROWDEV_ATTR(size_mb, S_IRUGO, csrow_size_show, NULL, 0);
CSROWDEV_ATTR(dev_type, S_IRUGO, csrow_dev_type_show, NULL, 0);
CSROWDEV_ATTR(mem_type, S_IRUGO, csrow_mem_type_show, NULL, 0);
CSROWDEV_ATTR(edac_mode, S_IRUGO, csrow_edac_mode_show, NULL, 0);
CSROWDEV_ATTR(ue_count, S_IRUGO, csrow_ue_count_show, NULL, 0);
CSROWDEV_ATTR(ce_count, S_IRUGO, csrow_ce_count_show, NULL, 0);

/* default attributes of the CSROW<id> object */
static struct csrowdev_attribute *default_csrow_attr[] = {
	&attr_dev_type,
	&attr_mem_type,
	&attr_edac_mode,
	&attr_size_mb,
	&attr_ue_count,
	&attr_ce_count,
	NULL,
};

/* possible dynamic channel DIMM Label attribute files */
CSROWDEV_ATTR(ch0_dimm_label, S_IRUGO | S_IWUSR,
	channel_dimm_label_show, channel_dimm_label_store, 0);
CSROWDEV_ATTR(ch1_dimm_label, S_IRUGO | S_IWUSR,
	channel_dimm_label_show, channel_dimm_label_store, 1);
CSROWDEV_ATTR(ch2_dimm_label, S_IRUGO | S_IWUSR,
	channel_dimm_label_show, channel_dimm_label_store, 2);
CSROWDEV_ATTR(ch3_dimm_label, S_IRUGO | S_IWUSR,
	channel_dimm_label_show, channel_dimm_label_store, 3);
CSROWDEV_ATTR(ch4_dimm_label, S_IRUGO | S_IWUSR,
	channel_dimm_label_show, channel_dimm_label_store, 4);
CSROWDEV_ATTR(ch5_dimm_label, S_IRUGO | S_IWUSR,
	channel_dimm_label_show, channel_dimm_label_store, 5);

/* Total possible dynamic DIMM Label attribute file table */
static struct csrowdev_attribute *dynamic_csrow_dimm_attr[] = {
	&attr_ch0_dimm_label,
	&attr_ch1_dimm_label,
	&attr_ch2_dimm_label,
	&attr_ch3_dimm_label,
	&attr_ch4_dimm_label,
	&attr_ch5_dimm_label
};

/* possible dynamic channel ce_count attribute files */
CSROWDEV_ATTR(ch0_ce_count, S_IRUGO | S_IWUSR, channel_ce_count_show, NULL, 0);
CSROWDEV_ATTR(ch1_ce_count, S_IRUGO | S_IWUSR, channel_ce_count_show, NULL, 1);
CSROWDEV_ATTR(ch2_ce_count, S_IRUGO | S_IWUSR, channel_ce_count_show, NULL, 2);
CSROWDEV_ATTR(ch3_ce_count, S_IRUGO | S_IWUSR, channel_ce_count_show, NULL, 3);
CSROWDEV_ATTR(ch4_ce_count, S_IRUGO | S_IWUSR, channel_ce_count_show, NULL, 4);
CSROWDEV_ATTR(ch5_ce_count, S_IRUGO | S_IWUSR, channel_ce_count_show, NULL, 5);

/* Total possible dynamic ce_count attribute file table */
static struct csrowdev_attribute *dynamic_csrow_ce_count_attr[] = {
	&attr_ch0_ce_count,
	&attr_ch1_ce_count,
	&attr_ch2_ce_count,
	&attr_ch3_ce_count,
	&attr_ch4_ce_count,
	&attr_ch5_ce_count
};

#define EDAC_NR_CHANNELS	6

/* Create dynamic CHANNEL files, indexed by 'chan',  under specifed CSROW */
static int edac_create_channel_files(struct kobject *kobj, int chan)
{
	int err = -ENODEV;

	if (chan >= EDAC_NR_CHANNELS)
		return err;

	/* create the DIMM label attribute file */
	err = sysfs_create_file(kobj,
				(struct attribute *)
				dynamic_csrow_dimm_attr[chan]);

	if (!err) {
		/* create the CE Count attribute file */
		err = sysfs_create_file(kobj,
					(struct attribute *)
					dynamic_csrow_ce_count_attr[chan]);
	} else {
		debugf1("%s()  dimm labels and ce_count files created",
			__func__);
	}

	return err;
}

/* No memory to release for this kobj */
static void edac_csrow_instance_release(struct kobject *kobj)
{
	struct mem_ctl_info *mci;
	struct csrow_info *cs;

	debugf1("%s()\n", __func__);

	cs = container_of(kobj, struct csrow_info, kobj);
	mci = cs->mci;

	kobject_put(&mci->edac_mci_kobj);
}

/* the kobj_type instance for a CSROW */
static struct kobj_type ktype_csrow = {
	.release = edac_csrow_instance_release,
	.sysfs_ops = &csrowfs_ops,
	.default_attrs = (struct attribute **)default_csrow_attr,
};

/* Create a CSROW object under specifed edac_mc_device */
static int edac_create_csrow_object(struct mem_ctl_info *mci,
					struct csrow_info *csrow, int index)
{
	struct kobject *kobj_mci = &mci->edac_mci_kobj;
	struct kobject *kobj;
	int chan;
	int err;

	/* generate ..../edac/mc/mc<id>/csrow<index>   */
	memset(&csrow->kobj, 0, sizeof(csrow->kobj));
	csrow->mci = mci;	/* include container up link */

	/* bump the mci instance's kobject's ref count */
	kobj = kobject_get(&mci->edac_mci_kobj);
	if (!kobj) {
		err = -ENODEV;
		goto err_out;
	}

	/* Instanstiate the csrow object */
	err = kobject_init_and_add(&csrow->kobj, &ktype_csrow, kobj_mci,
				   "csrow%d", index);
	if (err)
		goto err_release_top_kobj;

	/* At this point, to release a csrow kobj, one must
	 * call the kobject_put and allow that tear down
	 * to work the releasing
	 */

	/* Create the dyanmic attribute files on this csrow,
	 * namely, the DIMM labels and the channel ce_count
	 */
	for (chan = 0; chan < csrow->nr_channels; chan++) {
		err = edac_create_channel_files(&csrow->kobj, chan);
		if (err) {
			/* special case the unregister here */
			kobject_put(&csrow->kobj);
			goto err_out;
		}
	}
	kobject_uevent(&csrow->kobj, KOBJ_ADD);
	return 0;

	/* error unwind stack */
err_release_top_kobj:
	kobject_put(&mci->edac_mci_kobj);

err_out:
	return err;
}

/* default sysfs methods and data structures for the main MCI kobject */

static ssize_t mci_reset_counters_store(struct mem_ctl_info *mci,
					const char *data, size_t count)
{
	int row, chan;

	mci->ue_noinfo_count = 0;
	mci->ce_noinfo_count = 0;
	mci->ue_count = 0;
	mci->ce_count = 0;

	for (row = 0; row < mci->nr_csrows; row++) {
		struct csrow_info *ri = &mci->csrows[row];

		ri->ue_count = 0;
		ri->ce_count = 0;

		for (chan = 0; chan < ri->nr_channels; chan++)
			ri->channels[chan].ce_count = 0;
	}

	mci->start_time = jiffies;
	return count;
}

/* memory scrubbing */
static ssize_t mci_sdram_scrub_rate_store(struct mem_ctl_info *mci,
					  const char *data, size_t count)
{
	unsigned long bandwidth = 0;
	int err;

	if (!mci->set_sdram_scrub_rate) {
		edac_printk(KERN_WARNING, EDAC_MC,
			    "Memory scrub rate setting not implemented!\n");
		return -EINVAL;
	}

	if (strict_strtoul(data, 10, &bandwidth) < 0)
		return -EINVAL;

	err = mci->set_sdram_scrub_rate(mci, (u32)bandwidth);
	if (err) {
		edac_printk(KERN_DEBUG, EDAC_MC,
			    "Failed setting scrub rate to %lu\n", bandwidth);
		return -EINVAL;
	}
	else {
		edac_printk(KERN_DEBUG, EDAC_MC,
			    "Scrub rate set to: %lu\n", bandwidth);
		return count;
	}
}

static ssize_t mci_sdram_scrub_rate_show(struct mem_ctl_info *mci, char *data)
{
	u32 bandwidth = 0;
	int err;

	if (!mci->get_sdram_scrub_rate) {
		edac_printk(KERN_WARNING, EDAC_MC,
			    "Memory scrub rate reading not implemented\n");
		return -EINVAL;
	}

	err = mci->get_sdram_scrub_rate(mci, &bandwidth);
	if (err) {
		edac_printk(KERN_DEBUG, EDAC_MC, "Error reading scrub rate\n");
		return err;
	}
	else {
		edac_printk(KERN_DEBUG, EDAC_MC,
			    "Read scrub rate: %d\n", bandwidth);
		return sprintf(data, "%d\n", bandwidth);
	}
}

/* default attribute files for the MCI object */
static ssize_t mci_ue_count_show(struct mem_ctl_info *mci, char *data)
{
	return sprintf(data, "%d\n", mci->ue_count);
}

static ssize_t mci_ce_count_show(struct mem_ctl_info *mci, char *data)
{
	return sprintf(data, "%d\n", mci->ce_count);
}

static ssize_t mci_ce_noinfo_show(struct mem_ctl_info *mci, char *data)
{
	return sprintf(data, "%d\n", mci->ce_noinfo_count);
}

static ssize_t mci_ue_noinfo_show(struct mem_ctl_info *mci, char *data)
{
	return sprintf(data, "%d\n", mci->ue_noinfo_count);
}

static ssize_t mci_seconds_show(struct mem_ctl_info *mci, char *data)
{
	return sprintf(data, "%ld\n", (jiffies - mci->start_time) / HZ);
}

static ssize_t mci_ctl_name_show(struct mem_ctl_info *mci, char *data)
{
	return sprintf(data, "%s\n", mci->ctl_name);
}

static ssize_t mci_size_mb_show(struct mem_ctl_info *mci, char *data)
{
	int total_pages, csrow_idx;

	for (total_pages = csrow_idx = 0; csrow_idx < mci->nr_csrows;
		csrow_idx++) {
		struct csrow_info *csrow = &mci->csrows[csrow_idx];

		if (!csrow->nr_pages)
			continue;

		total_pages += csrow->nr_pages;
	}

	return sprintf(data, "%u\n", PAGES_TO_MiB(total_pages));
}

#define to_mci(k) container_of(k, struct mem_ctl_info, edac_mci_kobj)
#define to_mcidev_attr(a) container_of(a,struct mcidev_sysfs_attribute,attr)

/* MCI show/store functions for top most object */
static ssize_t mcidev_show(struct kobject *kobj, struct attribute *attr,
			char *buffer)
{
	struct mem_ctl_info *mem_ctl_info = to_mci(kobj);
	struct mcidev_sysfs_attribute *mcidev_attr = to_mcidev_attr(attr);

	debugf1("%s() mem_ctl_info %p\n", __func__, mem_ctl_info);

	if (mcidev_attr->show)
		return mcidev_attr->show(mem_ctl_info, buffer);

	return -EIO;
}

static ssize_t mcidev_store(struct kobject *kobj, struct attribute *attr,
			const char *buffer, size_t count)
{
	struct mem_ctl_info *mem_ctl_info = to_mci(kobj);
	struct mcidev_sysfs_attribute *mcidev_attr = to_mcidev_attr(attr);

	debugf1("%s() mem_ctl_info %p\n", __func__, mem_ctl_info);

	if (mcidev_attr->store)
		return mcidev_attr->store(mem_ctl_info, buffer, count);

	return -EIO;
}

/* Intermediate show/store table */
static const struct sysfs_ops mci_ops = {
	.show = mcidev_show,
	.store = mcidev_store
};

#define MCIDEV_ATTR(_name,_mode,_show,_store)			\
static struct mcidev_sysfs_attribute mci_attr_##_name = {			\
	.attr = {.name = __stringify(_name), .mode = _mode },	\
	.show   = _show,					\
	.store  = _store,					\
};

/* default Control file */
MCIDEV_ATTR(reset_counters, S_IWUSR, NULL, mci_reset_counters_store);

/* default Attribute files */
MCIDEV_ATTR(mc_name, S_IRUGO, mci_ctl_name_show, NULL);
MCIDEV_ATTR(size_mb, S_IRUGO, mci_size_mb_show, NULL);
MCIDEV_ATTR(seconds_since_reset, S_IRUGO, mci_seconds_show, NULL);
MCIDEV_ATTR(ue_noinfo_count, S_IRUGO, mci_ue_noinfo_show, NULL);
MCIDEV_ATTR(ce_noinfo_count, S_IRUGO, mci_ce_noinfo_show, NULL);
MCIDEV_ATTR(ue_count, S_IRUGO, mci_ue_count_show, NULL);
MCIDEV_ATTR(ce_count, S_IRUGO, mci_ce_count_show, NULL);

/* memory scrubber attribute file */
MCIDEV_ATTR(sdram_scrub_rate, S_IRUGO | S_IWUSR, mci_sdram_scrub_rate_show,
	mci_sdram_scrub_rate_store);

static struct mcidev_sysfs_attribute *mci_attr[] = {
	&mci_attr_reset_counters,
	&mci_attr_mc_name,
	&mci_attr_size_mb,
	&mci_attr_seconds_since_reset,
	&mci_attr_ue_noinfo_count,
	&mci_attr_ce_noinfo_count,
	&mci_attr_ue_count,
	&mci_attr_ce_count,
	&mci_attr_sdram_scrub_rate,
	NULL
};


/*
 * Release of a MC controlling instance
 *
 *	each MC control instance has the following resources upon entry:
 *		a) a ref count on the top memctl kobj
 *		b) a ref count on this module
 *
 *	this function must decrement those ref counts and then
 *	issue a free on the instance's memory
 */
static void edac_mci_control_release(struct kobject *kobj)
{
	struct mem_ctl_info *mci;

	mci = to_mci(kobj);

	debugf0("%s() mci instance idx=%d releasing\n", __func__, mci->mc_idx);

	/* decrement the module ref count */
	module_put(mci->owner);
}

static struct kobj_type ktype_mci = {
	.release = edac_mci_control_release,
	.sysfs_ops = &mci_ops,
	.default_attrs = (struct attribute **)mci_attr,
};

/* EDAC memory controller sysfs kset:
 *	/sys/devices/system/edac/mc
 */
static struct kset *mc_kset;

/*
 * edac_mc_register_sysfs_main_kobj
 *
 *	setups and registers the main kobject for each mci
 */
int edac_mc_register_sysfs_main_kobj(struct mem_ctl_info *mci)
{
	struct kobject *kobj_mci;
	int err;

	debugf1("%s()\n", __func__);

	kobj_mci = &mci->edac_mci_kobj;

	/* Init the mci's kobject */
	memset(kobj_mci, 0, sizeof(*kobj_mci));

	/* Record which module 'owns' this control structure
	 * and bump the ref count of the module
	 */
	mci->owner = THIS_MODULE;

	/* bump ref count on this module */
	if (!try_module_get(mci->owner)) {
		err = -ENODEV;
		goto fail_out;
	}

	/* this instance become part of the mc_kset */
	kobj_mci->kset = mc_kset;

	/* register the mc<id> kobject to the mc_kset */
	err = kobject_init_and_add(kobj_mci, &ktype_mci, NULL,
				   "mc%d", mci->mc_idx);
	if (err) {
		debugf1("%s()Failed to register '.../edac/mc%d'\n",
			__func__, mci->mc_idx);
		goto kobj_reg_fail;
	}
	kobject_uevent(kobj_mci, KOBJ_ADD);

	/* At this point, to 'free' the control struct,
	 * edac_mc_unregister_sysfs_main_kobj() must be used
	 */

	debugf1("%s() Registered '.../edac/mc%d' kobject\n",
		__func__, mci->mc_idx);

	return 0;

	/* Error exit stack */

kobj_reg_fail:
	module_put(mci->owner);

fail_out:
	return err;
}

/*
 * edac_mc_register_sysfs_main_kobj
 *
 *	tears down and the main mci kobject from the mc_kset
 */
void edac_mc_unregister_sysfs_main_kobj(struct mem_ctl_info *mci)
{
	debugf1("%s()\n", __func__);

	/* delete the kobj from the mc_kset */
	kobject_put(&mci->edac_mci_kobj);
}

#define EDAC_DEVICE_SYMLINK	"device"

#define grp_to_mci(k) (container_of(k, struct mcidev_sysfs_group_kobj, kobj)->mci)

/* MCI show/store functions for top most object */
static ssize_t inst_grp_show(struct kobject *kobj, struct attribute *attr,
			char *buffer)
{
	struct mem_ctl_info *mem_ctl_info = grp_to_mci(kobj);
	struct mcidev_sysfs_attribute *mcidev_attr = to_mcidev_attr(attr);

	debugf1("%s() mem_ctl_info %p\n", __func__, mem_ctl_info);

	if (mcidev_attr->show)
		return mcidev_attr->show(mem_ctl_info, buffer);

	return -EIO;
}

static ssize_t inst_grp_store(struct kobject *kobj, struct attribute *attr,
			const char *buffer, size_t count)
{
	struct mem_ctl_info *mem_ctl_info = grp_to_mci(kobj);
	struct mcidev_sysfs_attribute *mcidev_attr = to_mcidev_attr(attr);

	debugf1("%s() mem_ctl_info %p\n", __func__, mem_ctl_info);

	if (mcidev_attr->store)
		return mcidev_attr->store(mem_ctl_info, buffer, count);

	return -EIO;
}

/* No memory to release for this kobj */
static void edac_inst_grp_release(struct kobject *kobj)
{
	struct mcidev_sysfs_group_kobj *grp;
	struct mem_ctl_info *mci;

	debugf1("%s()\n", __func__);

	grp = container_of(kobj, struct mcidev_sysfs_group_kobj, kobj);
	mci = grp->mci;
}

/* Intermediate show/store table */
static struct sysfs_ops inst_grp_ops = {
	.show = inst_grp_show,
	.store = inst_grp_store
};

/* the kobj_type instance for a instance group */
static struct kobj_type ktype_inst_grp = {
	.release = edac_inst_grp_release,
	.sysfs_ops = &inst_grp_ops,
};


/*
 * edac_create_mci_instance_attributes
 *	create MC driver specific attributes bellow an specified kobj
 * This routine calls itself recursively, in order to create an entire
 * object tree.
 */
static int edac_create_mci_instance_attributes(struct mem_ctl_info *mci,
				const struct mcidev_sysfs_attribute *sysfs_attrib,
				struct kobject *kobj)
{
	int err;

	debugf1("%s()\n", __func__);

	while (sysfs_attrib) {
		debugf1("%s() sysfs_attrib = %p\n",__func__, sysfs_attrib);
		if (sysfs_attrib->grp) {
			struct mcidev_sysfs_group_kobj *grp_kobj;

			grp_kobj = kzalloc(sizeof(*grp_kobj), GFP_KERNEL);
			if (!grp_kobj)
				return -ENOMEM;

			grp_kobj->grp = sysfs_attrib->grp;
			grp_kobj->mci = mci;
			list_add_tail(&grp_kobj->list, &mci->grp_kobj_list);

			debugf0("%s() grp %s, mci %p\n", __func__,
				sysfs_attrib->grp->name, mci);

			err = kobject_init_and_add(&grp_kobj->kobj,
						&ktype_inst_grp,
						&mci->edac_mci_kobj,
						sysfs_attrib->grp->name);
			if (err < 0) {
				printk(KERN_ERR "kobject_init_and_add failed: %d\n", err);
				return err;
			}
			err = edac_create_mci_instance_attributes(mci,
					grp_kobj->grp->mcidev_attr,
					&grp_kobj->kobj);

			if (err < 0)
				return err;
		} else if (sysfs_attrib->attr.name) {
			debugf0("%s() file %s\n", __func__,
				sysfs_attrib->attr.name);

			err = sysfs_create_file(kobj, &sysfs_attrib->attr);
			if (err < 0) {
				printk(KERN_ERR "sysfs_create_file failed: %d\n", err);
				return err;
			}
		} else
			break;

		sysfs_attrib++;
	}

	return 0;
}

/*
 * edac_remove_mci_instance_attributes
 *	remove MC driver specific attributes at the topmost level
 *	directory of this mci instance.
 */
static void edac_remove_mci_instance_attributes(struct mem_ctl_info *mci,
				const struct mcidev_sysfs_attribute *sysfs_attrib,
				struct kobject *kobj, int count)
{
	struct mcidev_sysfs_group_kobj *grp_kobj, *tmp;

	debugf1("%s()\n", __func__);

	/*
	 * loop if there are attributes and until we hit a NULL entry
	 * Remove first all the atributes
	 */
	while (sysfs_attrib) {
		debugf1("%s() sysfs_attrib = %p\n",__func__, sysfs_attrib);
		if (sysfs_attrib->grp) {
			debugf1("%s() seeking for group %s\n",
				__func__, sysfs_attrib->grp->name);
			list_for_each_entry(grp_kobj,
					    &mci->grp_kobj_list, list) {
				debugf1("%s() grp_kobj->grp = %p\n",__func__, grp_kobj->grp);
				if (grp_kobj->grp == sysfs_attrib->grp) {
					edac_remove_mci_instance_attributes(mci,
						    grp_kobj->grp->mcidev_attr,
						    &grp_kobj->kobj, count + 1);
					debugf0("%s() group %s\n", __func__,
						sysfs_attrib->grp->name);
					kobject_put(&grp_kobj->kobj);
				}
			}
			debugf1("%s() end of seeking for group %s\n",
				__func__, sysfs_attrib->grp->name);
		} else if (sysfs_attrib->attr.name) {
			debugf0("%s() file %s\n", __func__,
				sysfs_attrib->attr.name);
			sysfs_remove_file(kobj, &sysfs_attrib->attr);
		} else
			break;
		sysfs_attrib++;
	}

	/* Remove the group objects */
	if (count)
		return;
	list_for_each_entry_safe(grp_kobj, tmp,
				 &mci->grp_kobj_list, list) {
		list_del(&grp_kobj->list);
		kfree(grp_kobj);
	}
}


/*
 * Create a new Memory Controller kobject instance,
 *	mc<id> under the 'mc' directory
 *
 * Return:
 *	0	Success
 *	!0	Failure
 */
int edac_create_sysfs_mci_device(struct mem_ctl_info *mci)
{
	int i;
	int err;
	struct csrow_info *csrow;
	struct kobject *kobj_mci = &mci->edac_mci_kobj;

	debugf0("%s() idx=%d\n", __func__, mci->mc_idx);

	INIT_LIST_HEAD(&mci->grp_kobj_list);

	/* create a symlink for the device */
	err = sysfs_create_link(kobj_mci, &mci->dev->kobj,
				EDAC_DEVICE_SYMLINK);
	if (err) {
		debugf1("%s() failure to create symlink\n", __func__);
		goto fail0;
	}

	/* If the low level driver desires some attributes,
	 * then create them now for the driver.
	 */
	if (mci->mc_driver_sysfs_attributes) {
		err = edac_create_mci_instance_attributes(mci,
					mci->mc_driver_sysfs_attributes,
					&mci->edac_mci_kobj);
		if (err) {
			debugf1("%s() failure to create mci attributes\n",
				__func__);
			goto fail0;
		}
	}

	/* Make directories for each CSROW object under the mc<id> kobject
	 */
	for (i = 0; i < mci->nr_csrows; i++) {
		csrow = &mci->csrows[i];

		/* Only expose populated CSROWs */
		if (csrow->nr_pages > 0) {
			err = edac_create_csrow_object(mci, csrow, i);
			if (err) {
				debugf1("%s() failure: create csrow %d obj\n",
					__func__, i);
				goto fail1;
			}
		}
	}

	return 0;

	/* CSROW error: backout what has already been registered,  */
fail1:
	for (i--; i >= 0; i--) {
		if (csrow->nr_pages > 0) {
			kobject_put(&mci->csrows[i].kobj);
		}
	}

	/* remove the mci instance's attributes, if any */
	edac_remove_mci_instance_attributes(mci,
		mci->mc_driver_sysfs_attributes, &mci->edac_mci_kobj, 0);

	/* remove the symlink */
	sysfs_remove_link(kobj_mci, EDAC_DEVICE_SYMLINK);

fail0:
	return err;
}

/*
 * remove a Memory Controller instance
 */
void edac_remove_sysfs_mci_device(struct mem_ctl_info *mci)
{
	int i;

	debugf0("%s()\n", __func__);

	/* remove all csrow kobjects */
	debugf0("%s()  unregister this mci kobj\n", __func__);
	for (i = 0; i < mci->nr_csrows; i++) {
		if (mci->csrows[i].nr_pages > 0) {
			debugf0("%s()  unreg csrow-%d\n", __func__, i);
			kobject_put(&mci->csrows[i].kobj);
		}
	}

	/* remove this mci instance's attribtes */
	if (mci->mc_driver_sysfs_attributes) {
		debugf0("%s()  unregister mci private attributes\n", __func__);
		edac_remove_mci_instance_attributes(mci,
						mci->mc_driver_sysfs_attributes,
						&mci->edac_mci_kobj, 0);
	}

	/* remove the symlink */
	debugf0("%s()  remove_link\n", __func__);
	sysfs_remove_link(&mci->edac_mci_kobj, EDAC_DEVICE_SYMLINK);

	/* unregister this instance's kobject */
	debugf0("%s()  remove_mci_instance\n", __func__);
	kobject_put(&mci->edac_mci_kobj);
}




/*
 * edac_setup_sysfs_mc_kset(void)
 *
 * Initialize the mc_kset for the 'mc' entry
 *	This requires creating the top 'mc' directory with a kset
 *	and its controls/attributes.
 *
 *	To this 'mc' kset, instance 'mci' will be grouped as children.
 *
 * Return:  0 SUCCESS
 *         !0 FAILURE error code
 */
int edac_sysfs_setup_mc_kset(void)
{
	int err = -EINVAL;
	struct sysdev_class *edac_class;

	debugf1("%s()\n", __func__);

	/* get the /sys/devices/system/edac class reference */
	edac_class = edac_get_sysfs_class();
	if (edac_class == NULL) {
		debugf1("%s() no edac_class error=%d\n", __func__, err);
		goto fail_out;
	}

	/* Init the MC's kobject */
	mc_kset = kset_create_and_add("mc", NULL, &edac_class->kset.kobj);
	if (!mc_kset) {
		err = -ENOMEM;
		debugf1("%s() Failed to register '.../edac/mc'\n", __func__);
		goto fail_kset;
	}

	debugf1("%s() Registered '.../edac/mc' kobject\n", __func__);

	return 0;

fail_kset:
	edac_put_sysfs_class();

fail_out:
	return err;
}

/*
 * edac_sysfs_teardown_mc_kset
 *
 *	deconstruct the mc_ket for memory controllers
 */
void edac_sysfs_teardown_mc_kset(void)
{
	kset_unregister(mc_kset);
	edac_put_sysfs_class();
}


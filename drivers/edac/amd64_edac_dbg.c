#include "amd64_edac.h"

#define EDAC_DCT_ATTR_SHOW(reg)						\
static ssize_t amd64_##reg##_show(struct device *dev,			\
			       struct device_attribute *mattr,		\
			       char *data)				\
{									\
	struct mem_ctl_info *mci = to_mci(dev);				\
	struct amd64_pvt *pvt = mci->pvt_info;				\
		return sprintf(data, "0x%016llx\n", (u64)pvt->reg);	\
}

EDAC_DCT_ATTR_SHOW(dhar);
EDAC_DCT_ATTR_SHOW(dbam0);
EDAC_DCT_ATTR_SHOW(top_mem);
EDAC_DCT_ATTR_SHOW(top_mem2);

static ssize_t amd64_hole_show(struct device *dev,
			       struct device_attribute *mattr,
			       char *data)
{
	struct mem_ctl_info *mci = to_mci(dev);

	u64 hole_base = 0;
	u64 hole_offset = 0;
	u64 hole_size = 0;

	amd64_get_dram_hole_info(mci, &hole_base, &hole_offset, &hole_size);

	return sprintf(data, "%llx %llx %llx\n", hole_base, hole_offset,
						 hole_size);
}

/*
 * update NUM_DBG_ATTRS in case you add new members
 */
static DEVICE_ATTR(dhar, S_IRUGO, amd64_dhar_show, NULL);
static DEVICE_ATTR(dbam, S_IRUGO, amd64_dbam0_show, NULL);
static DEVICE_ATTR(topmem, S_IRUGO, amd64_top_mem_show, NULL);
static DEVICE_ATTR(topmem2, S_IRUGO, amd64_top_mem2_show, NULL);
static DEVICE_ATTR(dram_hole, S_IRUGO, amd64_hole_show, NULL);

static struct attribute *amd64_edac_dbg_attrs[] = {
	&dev_attr_dhar.attr,
	&dev_attr_dbam.attr,
	&dev_attr_topmem.attr,
	&dev_attr_topmem2.attr,
	&dev_attr_dram_hole.attr,
	NULL
};

const struct attribute_group amd64_edac_dbg_group = {
	.attrs = amd64_edac_dbg_attrs,
};

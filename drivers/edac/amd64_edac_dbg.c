#include "amd64_edac.h"

#define EDAC_DCT_ATTR_SHOW(reg)						\
static ssize_t amd64_##reg##_show(struct mem_ctl_info *mci, char *data)	\
{									\
	struct amd64_pvt *pvt = mci->pvt_info;				\
		return sprintf(data, "0x%016llx\n", (u64)pvt->reg);	\
}

EDAC_DCT_ATTR_SHOW(dhar);
EDAC_DCT_ATTR_SHOW(dbam0);
EDAC_DCT_ATTR_SHOW(top_mem);
EDAC_DCT_ATTR_SHOW(top_mem2);

static ssize_t amd64_hole_show(struct mem_ctl_info *mci, char *data)
{
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
struct mcidev_sysfs_attribute amd64_dbg_attrs[] = {

	{
		.attr = {
			.name = "dhar",
			.mode = (S_IRUGO)
		},
		.show = amd64_dhar_show,
		.store = NULL,
	},
	{
		.attr = {
			.name = "dbam",
			.mode = (S_IRUGO)
		},
		.show = amd64_dbam0_show,
		.store = NULL,
	},
	{
		.attr = {
			.name = "topmem",
			.mode = (S_IRUGO)
		},
		.show = amd64_top_mem_show,
		.store = NULL,
	},
	{
		.attr = {
			.name = "topmem2",
			.mode = (S_IRUGO)
		},
		.show = amd64_top_mem2_show,
		.store = NULL,
	},
	{
		.attr = {
			.name = "dram_hole",
			.mode = (S_IRUGO)
		},
		.show = amd64_hole_show,
		.store = NULL,
	},
};

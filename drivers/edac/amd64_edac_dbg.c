#include "amd64_edac.h"

/*
 * accept a hex value and store it into the virtual error register file, field:
 * nbeal and nbeah. Assume virtual error values have already been set for: NBSL,
 * NBSH and NBCFG. Then proceed to map the error values to a MC, CSROW and
 * CHANNEL
 */
static ssize_t amd64_nbea_store(struct mem_ctl_info *mci, const char *data,
				size_t count)
{
	struct amd64_pvt *pvt = mci->pvt_info;
	unsigned long long value;
	int ret = 0;

	ret = strict_strtoull(data, 16, &value);
	if (ret != -EINVAL) {
		debugf0("received NBEA= 0x%llx\n", value);

		/* place the value into the virtual error packet */
		pvt->ctl_error_info.nbeal = (u32) value;
		value >>= 32;
		pvt->ctl_error_info.nbeah = (u32) value;

		/* Process the Mapping request */
		/* TODO: Add race prevention */
		amd_decode_nb_mce(pvt->mc_node_id, &pvt->ctl_error_info, 1);

		return count;
	}
	return ret;
}

/* display back what the last NBEA (MCA NB Address (MC4_ADDR)) was written */
static ssize_t amd64_nbea_show(struct mem_ctl_info *mci, char *data)
{
	struct amd64_pvt *pvt = mci->pvt_info;
	u64 value;

	value = pvt->ctl_error_info.nbeah;
	value <<= 32;
	value |= pvt->ctl_error_info.nbeal;

	return sprintf(data, "%llx\n", value);
}

/* store the NBSL (MCA NB Status Low (MC4_STATUS)) value user desires */
static ssize_t amd64_nbsl_store(struct mem_ctl_info *mci, const char *data,
				size_t count)
{
	struct amd64_pvt *pvt = mci->pvt_info;
	unsigned long value;
	int ret = 0;

	ret = strict_strtoul(data, 16, &value);
	if (ret != -EINVAL) {
		debugf0("received NBSL= 0x%lx\n", value);

		pvt->ctl_error_info.nbsl = (u32) value;

		return count;
	}
	return ret;
}

/* display back what the last NBSL value written */
static ssize_t amd64_nbsl_show(struct mem_ctl_info *mci, char *data)
{
	struct amd64_pvt *pvt = mci->pvt_info;
	u32 value;

	value = pvt->ctl_error_info.nbsl;

	return sprintf(data, "%x\n", value);
}

/* store the NBSH (MCA NB Status High) value user desires */
static ssize_t amd64_nbsh_store(struct mem_ctl_info *mci, const char *data,
				size_t count)
{
	struct amd64_pvt *pvt = mci->pvt_info;
	unsigned long value;
	int ret = 0;

	ret = strict_strtoul(data, 16, &value);
	if (ret != -EINVAL) {
		debugf0("received NBSH= 0x%lx\n", value);

		pvt->ctl_error_info.nbsh = (u32) value;

		return count;
	}
	return ret;
}

/* display back what the last NBSH value written */
static ssize_t amd64_nbsh_show(struct mem_ctl_info *mci, char *data)
{
	struct amd64_pvt *pvt = mci->pvt_info;
	u32 value;

	value = pvt->ctl_error_info.nbsh;

	return sprintf(data, "%x\n", value);
}

/* accept and store the NBCFG (MCA NB Configuration) value user desires */
static ssize_t amd64_nbcfg_store(struct mem_ctl_info *mci,
					const char *data, size_t count)
{
	struct amd64_pvt *pvt = mci->pvt_info;
	unsigned long value;
	int ret = 0;

	ret = strict_strtoul(data, 16, &value);
	if (ret != -EINVAL) {
		debugf0("received NBCFG= 0x%lx\n", value);

		pvt->ctl_error_info.nbcfg = (u32) value;

		return count;
	}
	return ret;
}

/* various show routines for the controls of a MCI */
static ssize_t amd64_nbcfg_show(struct mem_ctl_info *mci, char *data)
{
	struct amd64_pvt *pvt = mci->pvt_info;

	return sprintf(data, "%x\n", pvt->ctl_error_info.nbcfg);
}


static ssize_t amd64_dhar_show(struct mem_ctl_info *mci, char *data)
{
	struct amd64_pvt *pvt = mci->pvt_info;

	return sprintf(data, "%x\n", pvt->dhar);
}


static ssize_t amd64_dbam_show(struct mem_ctl_info *mci, char *data)
{
	struct amd64_pvt *pvt = mci->pvt_info;

	return sprintf(data, "%x\n", pvt->dbam0);
}


static ssize_t amd64_topmem_show(struct mem_ctl_info *mci, char *data)
{
	struct amd64_pvt *pvt = mci->pvt_info;

	return sprintf(data, "%llx\n", pvt->top_mem);
}


static ssize_t amd64_topmem2_show(struct mem_ctl_info *mci, char *data)
{
	struct amd64_pvt *pvt = mci->pvt_info;

	return sprintf(data, "%llx\n", pvt->top_mem2);
}

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
			.name = "nbea_ctl",
			.mode = (S_IRUGO | S_IWUSR)
		},
		.show = amd64_nbea_show,
		.store = amd64_nbea_store,
	},
	{
		.attr = {
			.name = "nbsl_ctl",
			.mode = (S_IRUGO | S_IWUSR)
		},
		.show = amd64_nbsl_show,
		.store = amd64_nbsl_store,
	},
	{
		.attr = {
			.name = "nbsh_ctl",
			.mode = (S_IRUGO | S_IWUSR)
		},
		.show = amd64_nbsh_show,
		.store = amd64_nbsh_store,
	},
	{
		.attr = {
			.name = "nbcfg_ctl",
			.mode = (S_IRUGO | S_IWUSR)
		},
		.show = amd64_nbcfg_show,
		.store = amd64_nbcfg_store,
	},
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
		.show = amd64_dbam_show,
		.store = NULL,
	},
	{
		.attr = {
			.name = "topmem",
			.mode = (S_IRUGO)
		},
		.show = amd64_topmem_show,
		.store = NULL,
	},
	{
		.attr = {
			.name = "topmem2",
			.mode = (S_IRUGO)
		},
		.show = amd64_topmem2_show,
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

// SPDX-License-Identifier: GPL-2.0

#define pr_fmt(fmt)	"papr-scm: " fmt

#include <linux/of.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/ndctl.h>
#include <linux/sched.h>
#include <linux/libnvdimm.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/seq_buf.h>
#include <linux/nd.h>

#include <asm/plpar_wrappers.h>
#include <asm/papr_pdsm.h>
#include <asm/mce.h>

#define BIND_ANY_ADDR (~0ul)

#define PAPR_SCM_DIMM_CMD_MASK \
	((1ul << ND_CMD_GET_CONFIG_SIZE) | \
	 (1ul << ND_CMD_GET_CONFIG_DATA) | \
	 (1ul << ND_CMD_SET_CONFIG_DATA) | \
	 (1ul << ND_CMD_CALL))

/* DIMM health bitmap bitmap indicators */
/* SCM device is unable to persist memory contents */
#define PAPR_PMEM_UNARMED                   (1ULL << (63 - 0))
/* SCM device failed to persist memory contents */
#define PAPR_PMEM_SHUTDOWN_DIRTY            (1ULL << (63 - 1))
/* SCM device contents are persisted from previous IPL */
#define PAPR_PMEM_SHUTDOWN_CLEAN            (1ULL << (63 - 2))
/* SCM device contents are not persisted from previous IPL */
#define PAPR_PMEM_EMPTY                     (1ULL << (63 - 3))
/* SCM device memory life remaining is critically low */
#define PAPR_PMEM_HEALTH_CRITICAL           (1ULL << (63 - 4))
/* SCM device will be garded off next IPL due to failure */
#define PAPR_PMEM_HEALTH_FATAL              (1ULL << (63 - 5))
/* SCM contents cannot persist due to current platform health status */
#define PAPR_PMEM_HEALTH_UNHEALTHY          (1ULL << (63 - 6))
/* SCM device is unable to persist memory contents in certain conditions */
#define PAPR_PMEM_HEALTH_NON_CRITICAL       (1ULL << (63 - 7))
/* SCM device is encrypted */
#define PAPR_PMEM_ENCRYPTED                 (1ULL << (63 - 8))
/* SCM device has been scrubbed and locked */
#define PAPR_PMEM_SCRUBBED_AND_LOCKED       (1ULL << (63 - 9))

/* Bits status indicators for health bitmap indicating unarmed dimm */
#define PAPR_PMEM_UNARMED_MASK (PAPR_PMEM_UNARMED |		\
				PAPR_PMEM_HEALTH_UNHEALTHY)

/* Bits status indicators for health bitmap indicating unflushed dimm */
#define PAPR_PMEM_BAD_SHUTDOWN_MASK (PAPR_PMEM_SHUTDOWN_DIRTY)

/* Bits status indicators for health bitmap indicating unrestored dimm */
#define PAPR_PMEM_BAD_RESTORE_MASK  (PAPR_PMEM_EMPTY)

/* Bit status indicators for smart event notification */
#define PAPR_PMEM_SMART_EVENT_MASK (PAPR_PMEM_HEALTH_CRITICAL | \
				    PAPR_PMEM_HEALTH_FATAL |	\
				    PAPR_PMEM_HEALTH_UNHEALTHY)

#define PAPR_SCM_PERF_STATS_EYECATCHER __stringify(SCMSTATS)
#define PAPR_SCM_PERF_STATS_VERSION 0x1

/* Struct holding a single performance metric */
struct papr_scm_perf_stat {
	u8 stat_id[8];
	__be64 stat_val;
} __packed;

/* Struct exchanged between kernel and PHYP for fetching drc perf stats */
struct papr_scm_perf_stats {
	u8 eye_catcher[8];
	/* Should be PAPR_SCM_PERF_STATS_VERSION */
	__be32 stats_version;
	/* Number of stats following */
	__be32 num_statistics;
	/* zero or more performance matrics */
	struct papr_scm_perf_stat scm_statistic[];
} __packed;

/* private struct associated with each region */
struct papr_scm_priv {
	struct platform_device *pdev;
	struct device_node *dn;
	uint32_t drc_index;
	uint64_t blocks;
	uint64_t block_size;
	int metadata_size;
	bool is_volatile;
	bool hcall_flush_required;

	uint64_t bound_addr;

	struct nvdimm_bus_descriptor bus_desc;
	struct nvdimm_bus *bus;
	struct nvdimm *nvdimm;
	struct resource res;
	struct nd_region *region;
	struct nd_interleave_set nd_set;
	struct list_head region_list;

	/* Protect dimm health data from concurrent read/writes */
	struct mutex health_mutex;

	/* Last time the health information of the dimm was updated */
	unsigned long lasthealth_jiffies;

	/* Health information for the dimm */
	u64 health_bitmap;

	/* length of the stat buffer as expected by phyp */
	size_t stat_buffer_len;
};

static int papr_scm_pmem_flush(struct nd_region *nd_region,
			       struct bio *bio __maybe_unused)
{
	struct papr_scm_priv *p = nd_region_provider_data(nd_region);
	unsigned long ret_buf[PLPAR_HCALL_BUFSIZE], token = 0;
	long rc;

	dev_dbg(&p->pdev->dev, "flush drc 0x%x", p->drc_index);

	do {
		rc = plpar_hcall(H_SCM_FLUSH, ret_buf, p->drc_index, token);
		token = ret_buf[0];

		/* Check if we are stalled for some time */
		if (H_IS_LONG_BUSY(rc)) {
			msleep(get_longbusy_msecs(rc));
			rc = H_BUSY;
		} else if (rc == H_BUSY) {
			cond_resched();
		}
	} while (rc == H_BUSY);

	if (rc) {
		dev_err(&p->pdev->dev, "flush error: %ld", rc);
		rc = -EIO;
	} else {
		dev_dbg(&p->pdev->dev, "flush drc 0x%x complete", p->drc_index);
	}

	return rc;
}

static LIST_HEAD(papr_nd_regions);
static DEFINE_MUTEX(papr_ndr_lock);

static int drc_pmem_bind(struct papr_scm_priv *p)
{
	unsigned long ret[PLPAR_HCALL_BUFSIZE];
	uint64_t saved = 0;
	uint64_t token;
	int64_t rc;

	/*
	 * When the hypervisor cannot map all the requested memory in a single
	 * hcall it returns H_BUSY and we call again with the token until
	 * we get H_SUCCESS. Aborting the retry loop before getting H_SUCCESS
	 * leave the system in an undefined state, so we wait.
	 */
	token = 0;

	do {
		rc = plpar_hcall(H_SCM_BIND_MEM, ret, p->drc_index, 0,
				p->blocks, BIND_ANY_ADDR, token);
		token = ret[0];
		if (!saved)
			saved = ret[1];
		cond_resched();
	} while (rc == H_BUSY);

	if (rc)
		return rc;

	p->bound_addr = saved;
	dev_dbg(&p->pdev->dev, "bound drc 0x%x to 0x%lx\n",
		p->drc_index, (unsigned long)saved);
	return rc;
}

static void drc_pmem_unbind(struct papr_scm_priv *p)
{
	unsigned long ret[PLPAR_HCALL_BUFSIZE];
	uint64_t token = 0;
	int64_t rc;

	dev_dbg(&p->pdev->dev, "unbind drc 0x%x\n", p->drc_index);

	/* NB: unbind has the same retry requirements as drc_pmem_bind() */
	do {

		/* Unbind of all SCM resources associated with drcIndex */
		rc = plpar_hcall(H_SCM_UNBIND_ALL, ret, H_UNBIND_SCOPE_DRC,
				 p->drc_index, token);
		token = ret[0];

		/* Check if we are stalled for some time */
		if (H_IS_LONG_BUSY(rc)) {
			msleep(get_longbusy_msecs(rc));
			rc = H_BUSY;
		} else if (rc == H_BUSY) {
			cond_resched();
		}

	} while (rc == H_BUSY);

	if (rc)
		dev_err(&p->pdev->dev, "unbind error: %lld\n", rc);
	else
		dev_dbg(&p->pdev->dev, "unbind drc 0x%x complete\n",
			p->drc_index);

	return;
}

static int drc_pmem_query_n_bind(struct papr_scm_priv *p)
{
	unsigned long start_addr;
	unsigned long end_addr;
	unsigned long ret[PLPAR_HCALL_BUFSIZE];
	int64_t rc;


	rc = plpar_hcall(H_SCM_QUERY_BLOCK_MEM_BINDING, ret,
			 p->drc_index, 0);
	if (rc)
		goto err_out;
	start_addr = ret[0];

	/* Make sure the full region is bound. */
	rc = plpar_hcall(H_SCM_QUERY_BLOCK_MEM_BINDING, ret,
			 p->drc_index, p->blocks - 1);
	if (rc)
		goto err_out;
	end_addr = ret[0];

	if ((end_addr - start_addr) != ((p->blocks - 1) * p->block_size))
		goto err_out;

	p->bound_addr = start_addr;
	dev_dbg(&p->pdev->dev, "bound drc 0x%x to 0x%lx\n", p->drc_index, start_addr);
	return rc;

err_out:
	dev_info(&p->pdev->dev,
		 "Failed to query, trying an unbind followed by bind");
	drc_pmem_unbind(p);
	return drc_pmem_bind(p);
}

/*
 * Query the Dimm performance stats from PHYP and copy them (if returned) to
 * provided struct papr_scm_perf_stats instance 'stats' that can hold atleast
 * (num_stats + header) bytes.
 * - If buff_stats == NULL the return value is the size in byes of the buffer
 * needed to hold all supported performance-statistics.
 * - If buff_stats != NULL and num_stats == 0 then we copy all known
 * performance-statistics to 'buff_stat' and expect to be large enough to
 * hold them.
 * - if buff_stats != NULL and num_stats > 0 then copy the requested
 * performance-statistics to buff_stats.
 */
static ssize_t drc_pmem_query_stats(struct papr_scm_priv *p,
				    struct papr_scm_perf_stats *buff_stats,
				    unsigned int num_stats)
{
	unsigned long ret[PLPAR_HCALL_BUFSIZE];
	size_t size;
	s64 rc;

	/* Setup the out buffer */
	if (buff_stats) {
		memcpy(buff_stats->eye_catcher,
		       PAPR_SCM_PERF_STATS_EYECATCHER, 8);
		buff_stats->stats_version =
			cpu_to_be32(PAPR_SCM_PERF_STATS_VERSION);
		buff_stats->num_statistics =
			cpu_to_be32(num_stats);

		/*
		 * Calculate the buffer size based on num-stats provided
		 * or use the prefetched max buffer length
		 */
		if (num_stats)
			/* Calculate size from the num_stats */
			size = sizeof(struct papr_scm_perf_stats) +
				num_stats * sizeof(struct papr_scm_perf_stat);
		else
			size = p->stat_buffer_len;
	} else {
		/* In case of no out buffer ignore the size */
		size = 0;
	}

	/* Do the HCALL asking PHYP for info */
	rc = plpar_hcall(H_SCM_PERFORMANCE_STATS, ret, p->drc_index,
			 buff_stats ? virt_to_phys(buff_stats) : 0,
			 size);

	/* Check if the error was due to an unknown stat-id */
	if (rc == H_PARTIAL) {
		dev_err(&p->pdev->dev,
			"Unknown performance stats, Err:0x%016lX\n", ret[0]);
		return -ENOENT;
	} else if (rc != H_SUCCESS) {
		dev_err(&p->pdev->dev,
			"Failed to query performance stats, Err:%lld\n", rc);
		return -EIO;

	} else if (!size) {
		/* Handle case where stat buffer size was requested */
		dev_dbg(&p->pdev->dev,
			"Performance stats size %ld\n", ret[0]);
		return ret[0];
	}

	/* Successfully fetched the requested stats from phyp */
	dev_dbg(&p->pdev->dev,
		"Performance stats returned %d stats\n",
		be32_to_cpu(buff_stats->num_statistics));
	return 0;
}

/*
 * Issue hcall to retrieve dimm health info and populate papr_scm_priv with the
 * health information.
 */
static int __drc_pmem_query_health(struct papr_scm_priv *p)
{
	unsigned long ret[PLPAR_HCALL_BUFSIZE];
	long rc;

	/* issue the hcall */
	rc = plpar_hcall(H_SCM_HEALTH, ret, p->drc_index);
	if (rc != H_SUCCESS) {
		dev_err(&p->pdev->dev,
			"Failed to query health information, Err:%ld\n", rc);
		return -ENXIO;
	}

	p->lasthealth_jiffies = jiffies;
	p->health_bitmap = ret[0] & ret[1];

	dev_dbg(&p->pdev->dev,
		"Queried dimm health info. Bitmap:0x%016lx Mask:0x%016lx\n",
		ret[0], ret[1]);

	return 0;
}

/* Min interval in seconds for assuming stable dimm health */
#define MIN_HEALTH_QUERY_INTERVAL 60

/* Query cached health info and if needed call drc_pmem_query_health */
static int drc_pmem_query_health(struct papr_scm_priv *p)
{
	unsigned long cache_timeout;
	int rc;

	/* Protect concurrent modifications to papr_scm_priv */
	rc = mutex_lock_interruptible(&p->health_mutex);
	if (rc)
		return rc;

	/* Jiffies offset for which the health data is assumed to be same */
	cache_timeout = p->lasthealth_jiffies +
		msecs_to_jiffies(MIN_HEALTH_QUERY_INTERVAL * 1000);

	/* Fetch new health info is its older than MIN_HEALTH_QUERY_INTERVAL */
	if (time_after(jiffies, cache_timeout))
		rc = __drc_pmem_query_health(p);
	else
		/* Assume cached health data is valid */
		rc = 0;

	mutex_unlock(&p->health_mutex);
	return rc;
}

static int papr_scm_meta_get(struct papr_scm_priv *p,
			     struct nd_cmd_get_config_data_hdr *hdr)
{
	unsigned long data[PLPAR_HCALL_BUFSIZE];
	unsigned long offset, data_offset;
	int len, read;
	int64_t ret;

	if ((hdr->in_offset + hdr->in_length) > p->metadata_size)
		return -EINVAL;

	for (len = hdr->in_length; len; len -= read) {

		data_offset = hdr->in_length - len;
		offset = hdr->in_offset + data_offset;

		if (len >= 8)
			read = 8;
		else if (len >= 4)
			read = 4;
		else if (len >= 2)
			read = 2;
		else
			read = 1;

		ret = plpar_hcall(H_SCM_READ_METADATA, data, p->drc_index,
				  offset, read);

		if (ret == H_PARAMETER) /* bad DRC index */
			return -ENODEV;
		if (ret)
			return -EINVAL; /* other invalid parameter */

		switch (read) {
		case 8:
			*(uint64_t *)(hdr->out_buf + data_offset) = be64_to_cpu(data[0]);
			break;
		case 4:
			*(uint32_t *)(hdr->out_buf + data_offset) = be32_to_cpu(data[0] & 0xffffffff);
			break;

		case 2:
			*(uint16_t *)(hdr->out_buf + data_offset) = be16_to_cpu(data[0] & 0xffff);
			break;

		case 1:
			*(uint8_t *)(hdr->out_buf + data_offset) = (data[0] & 0xff);
			break;
		}
	}
	return 0;
}

static int papr_scm_meta_set(struct papr_scm_priv *p,
			     struct nd_cmd_set_config_hdr *hdr)
{
	unsigned long offset, data_offset;
	int len, wrote;
	unsigned long data;
	__be64 data_be;
	int64_t ret;

	if ((hdr->in_offset + hdr->in_length) > p->metadata_size)
		return -EINVAL;

	for (len = hdr->in_length; len; len -= wrote) {

		data_offset = hdr->in_length - len;
		offset = hdr->in_offset + data_offset;

		if (len >= 8) {
			data = *(uint64_t *)(hdr->in_buf + data_offset);
			data_be = cpu_to_be64(data);
			wrote = 8;
		} else if (len >= 4) {
			data = *(uint32_t *)(hdr->in_buf + data_offset);
			data &= 0xffffffff;
			data_be = cpu_to_be32(data);
			wrote = 4;
		} else if (len >= 2) {
			data = *(uint16_t *)(hdr->in_buf + data_offset);
			data &= 0xffff;
			data_be = cpu_to_be16(data);
			wrote = 2;
		} else {
			data_be = *(uint8_t *)(hdr->in_buf + data_offset);
			data_be &= 0xff;
			wrote = 1;
		}

		ret = plpar_hcall_norets(H_SCM_WRITE_METADATA, p->drc_index,
					 offset, data_be, wrote);
		if (ret == H_PARAMETER) /* bad DRC index */
			return -ENODEV;
		if (ret)
			return -EINVAL; /* other invalid parameter */
	}

	return 0;
}

/*
 * Do a sanity checks on the inputs args to dimm-control function and return
 * '0' if valid. Validation of PDSM payloads happens later in
 * papr_scm_service_pdsm.
 */
static int is_cmd_valid(struct nvdimm *nvdimm, unsigned int cmd, void *buf,
			unsigned int buf_len)
{
	unsigned long cmd_mask = PAPR_SCM_DIMM_CMD_MASK;
	struct nd_cmd_pkg *nd_cmd;
	struct papr_scm_priv *p;
	enum papr_pdsm pdsm;

	/* Only dimm-specific calls are supported atm */
	if (!nvdimm)
		return -EINVAL;

	/* get the provider data from struct nvdimm */
	p = nvdimm_provider_data(nvdimm);

	if (!test_bit(cmd, &cmd_mask)) {
		dev_dbg(&p->pdev->dev, "Unsupported cmd=%u\n", cmd);
		return -EINVAL;
	}

	/* For CMD_CALL verify pdsm request */
	if (cmd == ND_CMD_CALL) {
		/* Verify the envelope and envelop size */
		if (!buf ||
		    buf_len < (sizeof(struct nd_cmd_pkg) + ND_PDSM_HDR_SIZE)) {
			dev_dbg(&p->pdev->dev, "Invalid pkg size=%u\n",
				buf_len);
			return -EINVAL;
		}

		/* Verify that the nd_cmd_pkg.nd_family is correct */
		nd_cmd = (struct nd_cmd_pkg *)buf;

		if (nd_cmd->nd_family != NVDIMM_FAMILY_PAPR) {
			dev_dbg(&p->pdev->dev, "Invalid pkg family=0x%llx\n",
				nd_cmd->nd_family);
			return -EINVAL;
		}

		pdsm = (enum papr_pdsm)nd_cmd->nd_command;

		/* Verify if the pdsm command is valid */
		if (pdsm <= PAPR_PDSM_MIN || pdsm >= PAPR_PDSM_MAX) {
			dev_dbg(&p->pdev->dev, "PDSM[0x%x]: Invalid PDSM\n",
				pdsm);
			return -EINVAL;
		}

		/* Have enough space to hold returned 'nd_pkg_pdsm' header */
		if (nd_cmd->nd_size_out < ND_PDSM_HDR_SIZE) {
			dev_dbg(&p->pdev->dev, "PDSM[0x%x]: Invalid payload\n",
				pdsm);
			return -EINVAL;
		}
	}

	/* Let the command be further processed */
	return 0;
}

static int papr_pdsm_fuel_gauge(struct papr_scm_priv *p,
				union nd_pdsm_payload *payload)
{
	int rc, size;
	u64 statval;
	struct papr_scm_perf_stat *stat;
	struct papr_scm_perf_stats *stats;

	/* Silently fail if fetching performance metrics isn't  supported */
	if (!p->stat_buffer_len)
		return 0;

	/* Allocate request buffer enough to hold single performance stat */
	size = sizeof(struct papr_scm_perf_stats) +
		sizeof(struct papr_scm_perf_stat);

	stats = kzalloc(size, GFP_KERNEL);
	if (!stats)
		return -ENOMEM;

	stat = &stats->scm_statistic[0];
	memcpy(&stat->stat_id, "MemLife ", sizeof(stat->stat_id));
	stat->stat_val = 0;

	/* Fetch the fuel gauge and populate it in payload */
	rc = drc_pmem_query_stats(p, stats, 1);
	if (rc < 0) {
		dev_dbg(&p->pdev->dev, "Err(%d) fetching fuel gauge\n", rc);
		goto free_stats;
	}

	statval = be64_to_cpu(stat->stat_val);
	dev_dbg(&p->pdev->dev,
		"Fetched fuel-gauge %llu", statval);
	payload->health.extension_flags |=
		PDSM_DIMM_HEALTH_RUN_GAUGE_VALID;
	payload->health.dimm_fuel_gauge = statval;

	rc = sizeof(struct nd_papr_pdsm_health);

free_stats:
	kfree(stats);
	return rc;
}

/* Fetch the DIMM health info and populate it in provided package. */
static int papr_pdsm_health(struct papr_scm_priv *p,
			    union nd_pdsm_payload *payload)
{
	int rc;

	/* Ensure dimm health mutex is taken preventing concurrent access */
	rc = mutex_lock_interruptible(&p->health_mutex);
	if (rc)
		goto out;

	/* Always fetch upto date dimm health data ignoring cached values */
	rc = __drc_pmem_query_health(p);
	if (rc) {
		mutex_unlock(&p->health_mutex);
		goto out;
	}

	/* update health struct with various flags derived from health bitmap */
	payload->health = (struct nd_papr_pdsm_health) {
		.extension_flags = 0,
		.dimm_unarmed = !!(p->health_bitmap & PAPR_PMEM_UNARMED_MASK),
		.dimm_bad_shutdown = !!(p->health_bitmap & PAPR_PMEM_BAD_SHUTDOWN_MASK),
		.dimm_bad_restore = !!(p->health_bitmap & PAPR_PMEM_BAD_RESTORE_MASK),
		.dimm_scrubbed = !!(p->health_bitmap & PAPR_PMEM_SCRUBBED_AND_LOCKED),
		.dimm_locked = !!(p->health_bitmap & PAPR_PMEM_SCRUBBED_AND_LOCKED),
		.dimm_encrypted = !!(p->health_bitmap & PAPR_PMEM_ENCRYPTED),
		.dimm_health = PAPR_PDSM_DIMM_HEALTHY,
	};

	/* Update field dimm_health based on health_bitmap flags */
	if (p->health_bitmap & PAPR_PMEM_HEALTH_FATAL)
		payload->health.dimm_health = PAPR_PDSM_DIMM_FATAL;
	else if (p->health_bitmap & PAPR_PMEM_HEALTH_CRITICAL)
		payload->health.dimm_health = PAPR_PDSM_DIMM_CRITICAL;
	else if (p->health_bitmap & PAPR_PMEM_HEALTH_UNHEALTHY)
		payload->health.dimm_health = PAPR_PDSM_DIMM_UNHEALTHY;

	/* struct populated hence can release the mutex now */
	mutex_unlock(&p->health_mutex);

	/* Populate the fuel gauge meter in the payload */
	papr_pdsm_fuel_gauge(p, payload);

	rc = sizeof(struct nd_papr_pdsm_health);

out:
	return rc;
}

/*
 * 'struct pdsm_cmd_desc'
 * Identifies supported PDSMs' expected length of in/out payloads
 * and pdsm service function.
 *
 * size_in	: Size of input payload if any in the PDSM request.
 * size_out	: Size of output payload if any in the PDSM request.
 * service	: Service function for the PDSM request. Return semantics:
 *		  rc < 0 : Error servicing PDSM and rc indicates the error.
 *		  rc >=0 : Serviced successfully and 'rc' indicate number of
 *			bytes written to payload.
 */
struct pdsm_cmd_desc {
	u32 size_in;
	u32 size_out;
	int (*service)(struct papr_scm_priv *dimm,
		       union nd_pdsm_payload *payload);
};

/* Holds all supported PDSMs' command descriptors */
static const struct pdsm_cmd_desc __pdsm_cmd_descriptors[] = {
	[PAPR_PDSM_MIN] = {
		.size_in = 0,
		.size_out = 0,
		.service = NULL,
	},
	/* New PDSM command descriptors to be added below */

	[PAPR_PDSM_HEALTH] = {
		.size_in = 0,
		.size_out = sizeof(struct nd_papr_pdsm_health),
		.service = papr_pdsm_health,
	},
	/* Empty */
	[PAPR_PDSM_MAX] = {
		.size_in = 0,
		.size_out = 0,
		.service = NULL,
	},
};

/* Given a valid pdsm cmd return its command descriptor else return NULL */
static inline const struct pdsm_cmd_desc *pdsm_cmd_desc(enum papr_pdsm cmd)
{
	if (cmd >= 0 || cmd < ARRAY_SIZE(__pdsm_cmd_descriptors))
		return &__pdsm_cmd_descriptors[cmd];

	return NULL;
}

/*
 * For a given pdsm request call an appropriate service function.
 * Returns errors if any while handling the pdsm command package.
 */
static int papr_scm_service_pdsm(struct papr_scm_priv *p,
				 struct nd_cmd_pkg *pkg)
{
	/* Get the PDSM header and PDSM command */
	struct nd_pkg_pdsm *pdsm_pkg = (struct nd_pkg_pdsm *)pkg->nd_payload;
	enum papr_pdsm pdsm = (enum papr_pdsm)pkg->nd_command;
	const struct pdsm_cmd_desc *pdsc;
	int rc;

	/* Fetch corresponding pdsm descriptor for validation and servicing */
	pdsc = pdsm_cmd_desc(pdsm);

	/* Validate pdsm descriptor */
	/* Ensure that reserved fields are 0 */
	if (pdsm_pkg->reserved[0] || pdsm_pkg->reserved[1]) {
		dev_dbg(&p->pdev->dev, "PDSM[0x%x]: Invalid reserved field\n",
			pdsm);
		return -EINVAL;
	}

	/* If pdsm expects some input, then ensure that the size_in matches */
	if (pdsc->size_in &&
	    pkg->nd_size_in != (pdsc->size_in + ND_PDSM_HDR_SIZE)) {
		dev_dbg(&p->pdev->dev, "PDSM[0x%x]: Mismatched size_in=%d\n",
			pdsm, pkg->nd_size_in);
		return -EINVAL;
	}

	/* If pdsm wants to return data, then ensure that  size_out matches */
	if (pdsc->size_out &&
	    pkg->nd_size_out != (pdsc->size_out + ND_PDSM_HDR_SIZE)) {
		dev_dbg(&p->pdev->dev, "PDSM[0x%x]: Mismatched size_out=%d\n",
			pdsm, pkg->nd_size_out);
		return -EINVAL;
	}

	/* Service the pdsm */
	if (pdsc->service) {
		dev_dbg(&p->pdev->dev, "PDSM[0x%x]: Servicing..\n", pdsm);

		rc = pdsc->service(p, &pdsm_pkg->payload);

		if (rc < 0) {
			/* error encountered while servicing pdsm */
			pdsm_pkg->cmd_status = rc;
			pkg->nd_fw_size = ND_PDSM_HDR_SIZE;
		} else {
			/* pdsm serviced and 'rc' bytes written to payload */
			pdsm_pkg->cmd_status = 0;
			pkg->nd_fw_size = ND_PDSM_HDR_SIZE + rc;
		}
	} else {
		dev_dbg(&p->pdev->dev, "PDSM[0x%x]: Unsupported PDSM request\n",
			pdsm);
		pdsm_pkg->cmd_status = -ENOENT;
		pkg->nd_fw_size = ND_PDSM_HDR_SIZE;
	}

	return pdsm_pkg->cmd_status;
}

static int papr_scm_ndctl(struct nvdimm_bus_descriptor *nd_desc,
			  struct nvdimm *nvdimm, unsigned int cmd, void *buf,
			  unsigned int buf_len, int *cmd_rc)
{
	struct nd_cmd_get_config_size *get_size_hdr;
	struct nd_cmd_pkg *call_pkg = NULL;
	struct papr_scm_priv *p;
	int rc;

	rc = is_cmd_valid(nvdimm, cmd, buf, buf_len);
	if (rc) {
		pr_debug("Invalid cmd=0x%x. Err=%d\n", cmd, rc);
		return rc;
	}

	/* Use a local variable in case cmd_rc pointer is NULL */
	if (!cmd_rc)
		cmd_rc = &rc;

	p = nvdimm_provider_data(nvdimm);

	switch (cmd) {
	case ND_CMD_GET_CONFIG_SIZE:
		get_size_hdr = buf;

		get_size_hdr->status = 0;
		get_size_hdr->max_xfer = 8;
		get_size_hdr->config_size = p->metadata_size;
		*cmd_rc = 0;
		break;

	case ND_CMD_GET_CONFIG_DATA:
		*cmd_rc = papr_scm_meta_get(p, buf);
		break;

	case ND_CMD_SET_CONFIG_DATA:
		*cmd_rc = papr_scm_meta_set(p, buf);
		break;

	case ND_CMD_CALL:
		call_pkg = (struct nd_cmd_pkg *)buf;
		*cmd_rc = papr_scm_service_pdsm(p, call_pkg);
		break;

	default:
		dev_dbg(&p->pdev->dev, "Unknown command = %d\n", cmd);
		return -EINVAL;
	}

	dev_dbg(&p->pdev->dev, "returned with cmd_rc = %d\n", *cmd_rc);

	return 0;
}

static ssize_t perf_stats_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	int index;
	ssize_t rc;
	struct seq_buf s;
	struct papr_scm_perf_stat *stat;
	struct papr_scm_perf_stats *stats;
	struct nvdimm *dimm = to_nvdimm(dev);
	struct papr_scm_priv *p = nvdimm_provider_data(dimm);

	if (!p->stat_buffer_len)
		return -ENOENT;

	/* Allocate the buffer for phyp where stats are written */
	stats = kzalloc(p->stat_buffer_len, GFP_KERNEL);
	if (!stats)
		return -ENOMEM;

	/* Ask phyp to return all dimm perf stats */
	rc = drc_pmem_query_stats(p, stats, 0);
	if (rc)
		goto free_stats;
	/*
	 * Go through the returned output buffer and print stats and
	 * values. Since stat_id is essentially a char string of
	 * 8 bytes, simply use the string format specifier to print it.
	 */
	seq_buf_init(&s, buf, PAGE_SIZE);
	for (index = 0, stat = stats->scm_statistic;
	     index < be32_to_cpu(stats->num_statistics);
	     ++index, ++stat) {
		seq_buf_printf(&s, "%.8s = 0x%016llX\n",
			       stat->stat_id,
			       be64_to_cpu(stat->stat_val));
	}

free_stats:
	kfree(stats);
	return rc ? rc : (ssize_t)seq_buf_used(&s);
}
static DEVICE_ATTR_ADMIN_RO(perf_stats);

static ssize_t flags_show(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	struct nvdimm *dimm = to_nvdimm(dev);
	struct papr_scm_priv *p = nvdimm_provider_data(dimm);
	struct seq_buf s;
	u64 health;
	int rc;

	rc = drc_pmem_query_health(p);
	if (rc)
		return rc;

	/* Copy health_bitmap locally, check masks & update out buffer */
	health = READ_ONCE(p->health_bitmap);

	seq_buf_init(&s, buf, PAGE_SIZE);
	if (health & PAPR_PMEM_UNARMED_MASK)
		seq_buf_printf(&s, "not_armed ");

	if (health & PAPR_PMEM_BAD_SHUTDOWN_MASK)
		seq_buf_printf(&s, "flush_fail ");

	if (health & PAPR_PMEM_BAD_RESTORE_MASK)
		seq_buf_printf(&s, "restore_fail ");

	if (health & PAPR_PMEM_ENCRYPTED)
		seq_buf_printf(&s, "encrypted ");

	if (health & PAPR_PMEM_SMART_EVENT_MASK)
		seq_buf_printf(&s, "smart_notify ");

	if (health & PAPR_PMEM_SCRUBBED_AND_LOCKED)
		seq_buf_printf(&s, "scrubbed locked ");

	if (seq_buf_used(&s))
		seq_buf_printf(&s, "\n");

	return seq_buf_used(&s);
}
DEVICE_ATTR_RO(flags);

/* papr_scm specific dimm attributes */
static struct attribute *papr_nd_attributes[] = {
	&dev_attr_flags.attr,
	&dev_attr_perf_stats.attr,
	NULL,
};

static struct attribute_group papr_nd_attribute_group = {
	.name = "papr",
	.attrs = papr_nd_attributes,
};

static const struct attribute_group *papr_nd_attr_groups[] = {
	&papr_nd_attribute_group,
	NULL,
};

static int papr_scm_nvdimm_init(struct papr_scm_priv *p)
{
	struct device *dev = &p->pdev->dev;
	struct nd_mapping_desc mapping;
	struct nd_region_desc ndr_desc;
	unsigned long dimm_flags;
	int target_nid, online_nid;
	ssize_t stat_size;

	p->bus_desc.ndctl = papr_scm_ndctl;
	p->bus_desc.module = THIS_MODULE;
	p->bus_desc.of_node = p->pdev->dev.of_node;
	p->bus_desc.provider_name = kstrdup(p->pdev->name, GFP_KERNEL);

	/* Set the dimm command family mask to accept PDSMs */
	set_bit(NVDIMM_FAMILY_PAPR, &p->bus_desc.dimm_family_mask);

	if (!p->bus_desc.provider_name)
		return -ENOMEM;

	p->bus = nvdimm_bus_register(NULL, &p->bus_desc);
	if (!p->bus) {
		dev_err(dev, "Error creating nvdimm bus %pOF\n", p->dn);
		kfree(p->bus_desc.provider_name);
		return -ENXIO;
	}

	dimm_flags = 0;
	set_bit(NDD_LABELING, &dimm_flags);

	/*
	 * Check if the nvdimm is unarmed. No locking needed as we are still
	 * initializing. Ignore error encountered if any.
	 */
	__drc_pmem_query_health(p);

	if (p->health_bitmap & PAPR_PMEM_UNARMED_MASK)
		set_bit(NDD_UNARMED, &dimm_flags);

	p->nvdimm = nvdimm_create(p->bus, p, papr_nd_attr_groups,
				  dimm_flags, PAPR_SCM_DIMM_CMD_MASK, 0, NULL);
	if (!p->nvdimm) {
		dev_err(dev, "Error creating DIMM object for %pOF\n", p->dn);
		goto err;
	}

	if (nvdimm_bus_check_dimm_count(p->bus, 1))
		goto err;

	/* now add the region */

	memset(&mapping, 0, sizeof(mapping));
	mapping.nvdimm = p->nvdimm;
	mapping.start = 0;
	mapping.size = p->blocks * p->block_size; // XXX: potential overflow?

	memset(&ndr_desc, 0, sizeof(ndr_desc));
	target_nid = dev_to_node(&p->pdev->dev);
	online_nid = numa_map_to_online_node(target_nid);
	ndr_desc.numa_node = online_nid;
	ndr_desc.target_node = target_nid;
	ndr_desc.res = &p->res;
	ndr_desc.of_node = p->dn;
	ndr_desc.provider_data = p;
	ndr_desc.mapping = &mapping;
	ndr_desc.num_mappings = 1;
	ndr_desc.nd_set = &p->nd_set;

	if (p->hcall_flush_required) {
		set_bit(ND_REGION_ASYNC, &ndr_desc.flags);
		ndr_desc.flush = papr_scm_pmem_flush;
	}

	if (p->is_volatile)
		p->region = nvdimm_volatile_region_create(p->bus, &ndr_desc);
	else {
		set_bit(ND_REGION_PERSIST_MEMCTRL, &ndr_desc.flags);
		p->region = nvdimm_pmem_region_create(p->bus, &ndr_desc);
	}
	if (!p->region) {
		dev_err(dev, "Error registering region %pR from %pOF\n",
				ndr_desc.res, p->dn);
		goto err;
	}
	if (target_nid != online_nid)
		dev_info(dev, "Region registered with target node %d and online node %d",
			 target_nid, online_nid);

	mutex_lock(&papr_ndr_lock);
	list_add_tail(&p->region_list, &papr_nd_regions);
	mutex_unlock(&papr_ndr_lock);

	/* Try retriving the stat buffer and see if its supported */
	stat_size = drc_pmem_query_stats(p, NULL, 0);
	if (stat_size > 0) {
		p->stat_buffer_len = stat_size;
		dev_dbg(&p->pdev->dev, "Max perf-stat size %lu-bytes\n",
			p->stat_buffer_len);
	} else {
		dev_info(&p->pdev->dev, "Dimm performance stats unavailable\n");
	}

	return 0;

err:	nvdimm_bus_unregister(p->bus);
	kfree(p->bus_desc.provider_name);
	return -ENXIO;
}

static void papr_scm_add_badblock(struct nd_region *region,
				  struct nvdimm_bus *bus, u64 phys_addr)
{
	u64 aligned_addr = ALIGN_DOWN(phys_addr, L1_CACHE_BYTES);

	if (nvdimm_bus_add_badrange(bus, aligned_addr, L1_CACHE_BYTES)) {
		pr_err("Bad block registration for 0x%llx failed\n", phys_addr);
		return;
	}

	pr_debug("Add memory range (0x%llx - 0x%llx) as bad range\n",
		 aligned_addr, aligned_addr + L1_CACHE_BYTES);

	nvdimm_region_notify(region, NVDIMM_REVALIDATE_POISON);
}

static int handle_mce_ue(struct notifier_block *nb, unsigned long val,
			 void *data)
{
	struct machine_check_event *evt = data;
	struct papr_scm_priv *p;
	u64 phys_addr;
	bool found = false;

	if (evt->error_type != MCE_ERROR_TYPE_UE)
		return NOTIFY_DONE;

	if (list_empty(&papr_nd_regions))
		return NOTIFY_DONE;

	/*
	 * The physical address obtained here is PAGE_SIZE aligned, so get the
	 * exact address from the effective address
	 */
	phys_addr = evt->u.ue_error.physical_address +
			(evt->u.ue_error.effective_address & ~PAGE_MASK);

	if (!evt->u.ue_error.physical_address_provided ||
	    !is_zone_device_page(pfn_to_page(phys_addr >> PAGE_SHIFT)))
		return NOTIFY_DONE;

	/* mce notifier is called from a process context, so mutex is safe */
	mutex_lock(&papr_ndr_lock);
	list_for_each_entry(p, &papr_nd_regions, region_list) {
		if (phys_addr >= p->res.start && phys_addr <= p->res.end) {
			found = true;
			break;
		}
	}

	if (found)
		papr_scm_add_badblock(p->region, p->bus, phys_addr);

	mutex_unlock(&papr_ndr_lock);

	return found ? NOTIFY_OK : NOTIFY_DONE;
}

static struct notifier_block mce_ue_nb = {
	.notifier_call = handle_mce_ue
};

static int papr_scm_probe(struct platform_device *pdev)
{
	struct device_node *dn = pdev->dev.of_node;
	u32 drc_index, metadata_size;
	u64 blocks, block_size;
	struct papr_scm_priv *p;
	const char *uuid_str;
	u64 uuid[2];
	int rc;

	/* check we have all the required DT properties */
	if (of_property_read_u32(dn, "ibm,my-drc-index", &drc_index)) {
		dev_err(&pdev->dev, "%pOF: missing drc-index!\n", dn);
		return -ENODEV;
	}

	if (of_property_read_u64(dn, "ibm,block-size", &block_size)) {
		dev_err(&pdev->dev, "%pOF: missing block-size!\n", dn);
		return -ENODEV;
	}

	if (of_property_read_u64(dn, "ibm,number-of-blocks", &blocks)) {
		dev_err(&pdev->dev, "%pOF: missing number-of-blocks!\n", dn);
		return -ENODEV;
	}

	if (of_property_read_string(dn, "ibm,unit-guid", &uuid_str)) {
		dev_err(&pdev->dev, "%pOF: missing unit-guid!\n", dn);
		return -ENODEV;
	}


	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	/* Initialize the dimm mutex */
	mutex_init(&p->health_mutex);

	/* optional DT properties */
	of_property_read_u32(dn, "ibm,metadata-size", &metadata_size);

	p->dn = dn;
	p->drc_index = drc_index;
	p->block_size = block_size;
	p->blocks = blocks;
	p->is_volatile = !of_property_read_bool(dn, "ibm,cache-flush-required");
	p->hcall_flush_required = of_property_read_bool(dn, "ibm,hcall-flush-required");

	/* We just need to ensure that set cookies are unique across */
	uuid_parse(uuid_str, (uuid_t *) uuid);
	/*
	 * cookie1 and cookie2 are not really little endian
	 * we store a little endian representation of the
	 * uuid str so that we can compare this with the label
	 * area cookie irrespective of the endian config with which
	 * the kernel is built.
	 */
	p->nd_set.cookie1 = cpu_to_le64(uuid[0]);
	p->nd_set.cookie2 = cpu_to_le64(uuid[1]);

	/* might be zero */
	p->metadata_size = metadata_size;
	p->pdev = pdev;

	/* request the hypervisor to bind this region to somewhere in memory */
	rc = drc_pmem_bind(p);

	/* If phyp says drc memory still bound then force unbound and retry */
	if (rc == H_OVERLAP)
		rc = drc_pmem_query_n_bind(p);

	if (rc != H_SUCCESS) {
		dev_err(&p->pdev->dev, "bind err: %d\n", rc);
		rc = -ENXIO;
		goto err;
	}

	/* setup the resource for the newly bound range */
	p->res.start = p->bound_addr;
	p->res.end   = p->bound_addr + p->blocks * p->block_size - 1;
	p->res.name  = pdev->name;
	p->res.flags = IORESOURCE_MEM;

	rc = papr_scm_nvdimm_init(p);
	if (rc)
		goto err2;

	platform_set_drvdata(pdev, p);

	return 0;

err2:	drc_pmem_unbind(p);
err:	kfree(p);
	return rc;
}

static int papr_scm_remove(struct platform_device *pdev)
{
	struct papr_scm_priv *p = platform_get_drvdata(pdev);

	mutex_lock(&papr_ndr_lock);
	list_del(&p->region_list);
	mutex_unlock(&papr_ndr_lock);

	nvdimm_bus_unregister(p->bus);
	drc_pmem_unbind(p);
	kfree(p->bus_desc.provider_name);
	kfree(p);

	return 0;
}

static const struct of_device_id papr_scm_match[] = {
	{ .compatible = "ibm,pmemory" },
	{ .compatible = "ibm,pmemory-v2" },
	{ },
};

static struct platform_driver papr_scm_driver = {
	.probe = papr_scm_probe,
	.remove = papr_scm_remove,
	.driver = {
		.name = "papr_scm",
		.of_match_table = papr_scm_match,
	},
};

static int __init papr_scm_init(void)
{
	int ret;

	ret = platform_driver_register(&papr_scm_driver);
	if (!ret)
		mce_register_notifier(&mce_ue_nb);

	return ret;
}
module_init(papr_scm_init);

static void __exit papr_scm_exit(void)
{
	mce_unregister_notifier(&mce_ue_nb);
	platform_driver_unregister(&papr_scm_driver);
}
module_exit(papr_scm_exit);

MODULE_DEVICE_TABLE(of, papr_scm_match);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("IBM Corporation");

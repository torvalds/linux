/*
 * Hypervisor supplied "24x7" performance counter support
 *
 * Author: Cody P Schafer <cody@linux.vnet.ibm.com>
 * Copyright 2014 IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#define pr_fmt(fmt) "hv-24x7: " fmt

#include <linux/perf_event.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <asm/firmware.h>
#include <asm/hvcall.h>
#include <asm/io.h>

#include "hv-24x7.h"
#include "hv-24x7-catalog.h"
#include "hv-common.h"

/*
 * TODO: Merging events:
 * - Think of the hcall as an interface to a 4d array of counters:
 *   - x = domains
 *   - y = indexes in the domain (core, chip, vcpu, node, etc)
 *   - z = offset into the counter space
 *   - w = lpars (guest vms, "logical partitions")
 * - A single request is: x,y,y_last,z,z_last,w,w_last
 *   - this means we can retrieve a rectangle of counters in y,z for a single x.
 *
 * - Things to consider (ignoring w):
 *   - input  cost_per_request = 16
 *   - output cost_per_result(ys,zs)  = 8 + 8 * ys + ys * zs
 *   - limited number of requests per hcall (must fit into 4K bytes)
 *     - 4k = 16 [buffer header] - 16 [request size] * request_count
 *     - 255 requests per hcall
 *   - sometimes it will be more efficient to read extra data and discard
 */

/*
 * Example usage:
 *  perf stat -e 'hv_24x7/domain=2,offset=8,starting_index=0,lpar=0xffffffff/'
 */

/* u3 0-6, one of HV_24X7_PERF_DOMAIN */
EVENT_DEFINE_RANGE_FORMAT(domain, config, 0, 3);
/* u16 */
EVENT_DEFINE_RANGE_FORMAT(starting_index, config, 16, 31);
/* u32, see "data_offset" */
EVENT_DEFINE_RANGE_FORMAT(offset, config, 32, 63);
/* u16 */
EVENT_DEFINE_RANGE_FORMAT(lpar, config1, 0, 15);

EVENT_DEFINE_RANGE(reserved1, config,   4, 15);
EVENT_DEFINE_RANGE(reserved2, config1, 16, 63);
EVENT_DEFINE_RANGE(reserved3, config2,  0, 63);

static struct attribute *format_attrs[] = {
	&format_attr_domain.attr,
	&format_attr_offset.attr,
	&format_attr_starting_index.attr,
	&format_attr_lpar.attr,
	NULL,
};

static struct attribute_group format_group = {
	.name = "format",
	.attrs = format_attrs,
};

static struct kmem_cache *hv_page_cache;

/*
 * read_offset_data - copy data from one buffer to another while treating the
 *                    source buffer as a small view on the total avaliable
 *                    source data.
 *
 * @dest: buffer to copy into
 * @dest_len: length of @dest in bytes
 * @requested_offset: the offset within the source data we want. Must be > 0
 * @src: buffer to copy data from
 * @src_len: length of @src in bytes
 * @source_offset: the offset in the sorce data that (src,src_len) refers to.
 *                 Must be > 0
 *
 * returns the number of bytes copied.
 *
 * The following ascii art shows the various buffer possitioning we need to
 * handle, assigns some arbitrary varibles to points on the buffer, and then
 * shows how we fiddle with those values to get things we care about (copy
 * start in src and copy len)
 *
 * s = @src buffer
 * d = @dest buffer
 * '.' areas in d are written to.
 *
 *                       u
 *   x         w	 v  z
 * d           |.........|
 * s |----------------------|
 *
 *                      u
 *   x         w	z     v
 * d           |........------|
 * s |------------------|
 *
 *   x         w        u,z,v
 * d           |........|
 * s |------------------|
 *
 *   x,w                u,v,z
 * d |..................|
 * s |------------------|
 *
 *   x        u
 *   w        v		z
 * d |........|
 * s |------------------|
 *
 *   x      z   w      v
 * d            |------|
 * s |------|
 *
 * x = source_offset
 * w = requested_offset
 * z = source_offset + src_len
 * v = requested_offset + dest_len
 *
 * w_offset_in_s = w - x = requested_offset - source_offset
 * z_offset_in_s = z - x = src_len
 * v_offset_in_s = v - x = request_offset + dest_len - src_len
 */
static ssize_t read_offset_data(void *dest, size_t dest_len,
				loff_t requested_offset, void *src,
				size_t src_len, loff_t source_offset)
{
	size_t w_offset_in_s = requested_offset - source_offset;
	size_t z_offset_in_s = src_len;
	size_t v_offset_in_s = requested_offset + dest_len - src_len;
	size_t u_offset_in_s = min(z_offset_in_s, v_offset_in_s);
	size_t copy_len = u_offset_in_s - w_offset_in_s;

	if (requested_offset < 0 || source_offset < 0)
		return -EINVAL;

	if (z_offset_in_s <= w_offset_in_s)
		return 0;

	memcpy(dest, src + w_offset_in_s, copy_len);
	return copy_len;
}

static unsigned long h_get_24x7_catalog_page_(unsigned long phys_4096,
					      unsigned long version,
					      unsigned long index)
{
	pr_devel("h_get_24x7_catalog_page(0x%lx, %lu, %lu)",
			phys_4096,
			version,
			index);
	WARN_ON(!IS_ALIGNED(phys_4096, 4096));
	return plpar_hcall_norets(H_GET_24X7_CATALOG_PAGE,
			phys_4096,
			version,
			index);
}

static unsigned long h_get_24x7_catalog_page(char page[],
					     u64 version, u32 index)
{
	return h_get_24x7_catalog_page_(virt_to_phys(page),
					version, index);
}

static ssize_t catalog_read(struct file *filp, struct kobject *kobj,
			    struct bin_attribute *bin_attr, char *buf,
			    loff_t offset, size_t count)
{
	unsigned long hret;
	ssize_t ret = 0;
	size_t catalog_len = 0, catalog_page_len = 0, page_count = 0;
	loff_t page_offset = 0;
	uint64_t catalog_version_num = 0;
	void *page = kmem_cache_alloc(hv_page_cache, GFP_USER);
	struct hv_24x7_catalog_page_0 *page_0 = page;
	if (!page)
		return -ENOMEM;

	hret = h_get_24x7_catalog_page(page, 0, 0);
	if (hret) {
		ret = -EIO;
		goto e_free;
	}

	catalog_version_num = be64_to_cpu(page_0->version);
	catalog_page_len = be32_to_cpu(page_0->length);
	catalog_len = catalog_page_len * 4096;

	page_offset = offset / 4096;
	page_count  = count  / 4096;

	if (page_offset >= catalog_page_len)
		goto e_free;

	if (page_offset != 0) {
		hret = h_get_24x7_catalog_page(page, catalog_version_num,
					       page_offset);
		if (hret) {
			ret = -EIO;
			goto e_free;
		}
	}

	ret = read_offset_data(buf, count, offset,
				page, 4096, page_offset * 4096);
e_free:
	if (hret)
		pr_err("h_get_24x7_catalog_page(ver=%lld, page=%lld) failed:"
		       " rc=%ld\n",
		       catalog_version_num, page_offset, hret);
	kmem_cache_free(hv_page_cache, page);

	pr_devel("catalog_read: offset=%lld(%lld) count=%zu(%zu) catalog_len=%zu(%zu) => %zd\n",
			offset, page_offset, count, page_count, catalog_len,
			catalog_page_len, ret);

	return ret;
}

#define PAGE_0_ATTR(_name, _fmt, _expr)				\
static ssize_t _name##_show(struct device *dev,			\
			    struct device_attribute *dev_attr,	\
			    char *buf)				\
{								\
	unsigned long hret;					\
	ssize_t ret = 0;					\
	void *page = kmem_cache_alloc(hv_page_cache, GFP_USER);	\
	struct hv_24x7_catalog_page_0 *page_0 = page;		\
	if (!page)						\
		return -ENOMEM;					\
	hret = h_get_24x7_catalog_page(page, 0, 0);		\
	if (hret) {						\
		ret = -EIO;					\
		goto e_free;					\
	}							\
	ret = sprintf(buf, _fmt, _expr);			\
e_free:								\
	kfree(page);						\
	return ret;						\
}								\
static DEVICE_ATTR_RO(_name)

PAGE_0_ATTR(catalog_version, "%lld\n",
		(unsigned long long)be64_to_cpu(page_0->version));
PAGE_0_ATTR(catalog_len, "%lld\n",
		(unsigned long long)be32_to_cpu(page_0->length) * 4096);
static BIN_ATTR_RO(catalog, 0/* real length varies */);

static struct bin_attribute *if_bin_attrs[] = {
	&bin_attr_catalog,
	NULL,
};

static struct attribute *if_attrs[] = {
	&dev_attr_catalog_len.attr,
	&dev_attr_catalog_version.attr,
	NULL,
};

static struct attribute_group if_group = {
	.name = "interface",
	.bin_attrs = if_bin_attrs,
	.attrs = if_attrs,
};

static const struct attribute_group *attr_groups[] = {
	&format_group,
	&if_group,
	NULL,
};

static bool is_physical_domain(int domain)
{
	return  domain == HV_24X7_PERF_DOMAIN_PHYSICAL_CHIP ||
		domain == HV_24X7_PERF_DOMAIN_PHYSICAL_CORE;
}

static unsigned long single_24x7_request(u8 domain, u32 offset, u16 ix,
					 u16 lpar, u64 *res,
					 bool success_expected)
{
	unsigned long ret = -ENOMEM;

	/*
	 * request_buffer and result_buffer are not required to be 4k aligned,
	 * but are not allowed to cross any 4k boundary. Aligning them to 4k is
	 * the simplest way to ensure that.
	 */
	struct reqb {
		struct hv_24x7_request_buffer buf;
		struct hv_24x7_request req;
	} __packed *request_buffer;

	struct {
		struct hv_24x7_data_result_buffer buf;
		struct hv_24x7_result res;
		struct hv_24x7_result_element elem;
		__be64 result;
	} __packed *result_buffer;

	BUILD_BUG_ON(sizeof(*request_buffer) > 4096);
	BUILD_BUG_ON(sizeof(*result_buffer) > 4096);

	request_buffer = kmem_cache_zalloc(hv_page_cache, GFP_USER);
	if (!request_buffer)
		goto out;

	result_buffer = kmem_cache_zalloc(hv_page_cache, GFP_USER);
	if (!result_buffer)
		goto out_free_request_buffer;

	*request_buffer = (struct reqb) {
		.buf = {
			.interface_version = HV_24X7_IF_VERSION_CURRENT,
			.num_requests = 1,
		},
		.req = {
			.performance_domain = domain,
			.data_size = cpu_to_be16(8),
			.data_offset = cpu_to_be32(offset),
			.starting_lpar_ix = cpu_to_be16(lpar),
			.max_num_lpars = cpu_to_be16(1),
			.starting_ix = cpu_to_be16(ix),
			.max_ix = cpu_to_be16(1),
		}
	};

	ret = plpar_hcall_norets(H_GET_24X7_DATA,
			virt_to_phys(request_buffer), sizeof(*request_buffer),
			virt_to_phys(result_buffer),  sizeof(*result_buffer));

	if (ret) {
		if (success_expected)
			pr_err_ratelimited("hcall failed: %d %#x %#x %d => "
				"0x%lx (%ld) detail=0x%x failing ix=%x\n",
				domain, offset, ix, lpar, ret, ret,
				result_buffer->buf.detailed_rc,
				result_buffer->buf.failing_request_ix);
		goto out_free_result_buffer;
	}

	*res = be64_to_cpu(result_buffer->result);

out_free_result_buffer:
	kfree(result_buffer);
out_free_request_buffer:
	kfree(request_buffer);
out:
	return ret;
}

static unsigned long event_24x7_request(struct perf_event *event, u64 *res,
		bool success_expected)
{
	return single_24x7_request(event_get_domain(event),
				event_get_offset(event),
				event_get_starting_index(event),
				event_get_lpar(event),
				res,
				success_expected);
}

static int h_24x7_event_init(struct perf_event *event)
{
	struct hv_perf_caps caps;
	unsigned domain;
	unsigned long hret;
	u64 ct;

	/* Not our event */
	if (event->attr.type != event->pmu->type)
		return -ENOENT;

	/* Unused areas must be 0 */
	if (event_get_reserved1(event) ||
	    event_get_reserved2(event) ||
	    event_get_reserved3(event)) {
		pr_devel("reserved set when forbidden 0x%llx(0x%llx) 0x%llx(0x%llx) 0x%llx(0x%llx)\n",
				event->attr.config,
				event_get_reserved1(event),
				event->attr.config1,
				event_get_reserved2(event),
				event->attr.config2,
				event_get_reserved3(event));
		return -EINVAL;
	}

	/* unsupported modes and filters */
	if (event->attr.exclude_user   ||
	    event->attr.exclude_kernel ||
	    event->attr.exclude_hv     ||
	    event->attr.exclude_idle   ||
	    event->attr.exclude_host   ||
	    event->attr.exclude_guest)
		return -EINVAL;

	/* no branch sampling */
	if (has_branch_stack(event))
		return -EOPNOTSUPP;

	/* offset must be 8 byte aligned */
	if (event_get_offset(event) % 8) {
		pr_devel("bad alignment\n");
		return -EINVAL;
	}

	/* Domains above 6 are invalid */
	domain = event_get_domain(event);
	if (domain > 6) {
		pr_devel("invalid domain %d\n", domain);
		return -EINVAL;
	}

	hret = hv_perf_caps_get(&caps);
	if (hret) {
		pr_devel("could not get capabilities: rc=%ld\n", hret);
		return -EIO;
	}

	/* PHYSICAL domains & other lpars require extra capabilities */
	if (!caps.collect_privileged && (is_physical_domain(domain) ||
		(event_get_lpar(event) != event_get_lpar_max()))) {
		pr_devel("hv permisions disallow: is_physical_domain:%d, lpar=0x%llx\n",
				is_physical_domain(domain),
				event_get_lpar(event));
		return -EACCES;
	}

	/* see if the event complains */
	if (event_24x7_request(event, &ct, false)) {
		pr_devel("test hcall failed\n");
		return -EIO;
	}

	return 0;
}

static u64 h_24x7_get_value(struct perf_event *event)
{
	unsigned long ret;
	u64 ct;
	ret = event_24x7_request(event, &ct, true);
	if (ret)
		/* We checked this in event init, shouldn't fail here... */
		return 0;

	return ct;
}

static void h_24x7_event_update(struct perf_event *event)
{
	s64 prev;
	u64 now;
	now = h_24x7_get_value(event);
	prev = local64_xchg(&event->hw.prev_count, now);
	local64_add(now - prev, &event->count);
}

static void h_24x7_event_start(struct perf_event *event, int flags)
{
	if (flags & PERF_EF_RELOAD)
		local64_set(&event->hw.prev_count, h_24x7_get_value(event));
}

static void h_24x7_event_stop(struct perf_event *event, int flags)
{
	h_24x7_event_update(event);
}

static int h_24x7_event_add(struct perf_event *event, int flags)
{
	if (flags & PERF_EF_START)
		h_24x7_event_start(event, flags);

	return 0;
}

static int h_24x7_event_idx(struct perf_event *event)
{
	return 0;
}

static struct pmu h_24x7_pmu = {
	.task_ctx_nr = perf_invalid_context,

	.name = "hv_24x7",
	.attr_groups = attr_groups,
	.event_init  = h_24x7_event_init,
	.add         = h_24x7_event_add,
	.del         = h_24x7_event_stop,
	.start       = h_24x7_event_start,
	.stop        = h_24x7_event_stop,
	.read        = h_24x7_event_update,
	.event_idx   = h_24x7_event_idx,
};

static int hv_24x7_init(void)
{
	int r;
	unsigned long hret;
	struct hv_perf_caps caps;

	if (!firmware_has_feature(FW_FEATURE_LPAR)) {
		pr_debug("not a virtualized system, not enabling\n");
		return -ENODEV;
	}

	hret = hv_perf_caps_get(&caps);
	if (hret) {
		pr_debug("could not obtain capabilities, not enabling, rc=%ld\n",
				hret);
		return -ENODEV;
	}

	hv_page_cache = kmem_cache_create("hv-page-4096", 4096, 4096, 0, NULL);
	if (!hv_page_cache)
		return -ENOMEM;

	/* sampling not supported */
	h_24x7_pmu.capabilities |= PERF_PMU_CAP_NO_INTERRUPT;

	r = perf_pmu_register(&h_24x7_pmu, h_24x7_pmu.name, -1);
	if (r)
		return r;

	return 0;
}

device_initcall(hv_24x7_init);

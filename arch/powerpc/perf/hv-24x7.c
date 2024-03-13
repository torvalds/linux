// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Hypervisor supplied "24x7" performance counter support
 *
 * Author: Cody P Schafer <cody@linux.vnet.ibm.com>
 * Copyright 2014 IBM Corporation.
 */

#define pr_fmt(fmt) "hv-24x7: " fmt

#include <linux/perf_event.h>
#include <linux/rbtree.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include <asm/cputhreads.h>
#include <asm/firmware.h>
#include <asm/hvcall.h>
#include <asm/io.h>
#include <linux/byteorder/generic.h>

#include <asm/rtas.h>
#include "hv-24x7.h"
#include "hv-24x7-catalog.h"
#include "hv-common.h"

/* Version of the 24x7 hypervisor API that we should use in this machine. */
static int interface_version;

/* Whether we have to aggregate result data for some domains. */
static bool aggregate_result_elements;

static cpumask_t hv_24x7_cpumask;

static bool domain_is_valid(unsigned int domain)
{
	switch (domain) {
#define DOMAIN(n, v, x, c)		\
	case HV_PERF_DOMAIN_##n:	\
		/* fall through */
#include "hv-24x7-domains.h"
#undef DOMAIN
		return true;
	default:
		return false;
	}
}

static bool is_physical_domain(unsigned int domain)
{
	switch (domain) {
#define DOMAIN(n, v, x, c)		\
	case HV_PERF_DOMAIN_##n:	\
		return c;
#include "hv-24x7-domains.h"
#undef DOMAIN
	default:
		return false;
	}
}

/*
 * The Processor Module Information system parameter allows transferring
 * of certain processor module information from the platform to the OS.
 * Refer PAPR+ document to get parameter token value as '43'.
 */

#define PROCESSOR_MODULE_INFO   43

static u32 phys_sockets;	/* Physical sockets */
static u32 phys_chipspersocket;	/* Physical chips per socket*/
static u32 phys_coresperchip; /* Physical cores per chip */

/*
 * read_24x7_sys_info()
 * Retrieve the number of sockets and chips per socket and cores per
 * chip details through the get-system-parameter rtas call.
 */
void read_24x7_sys_info(void)
{
	int call_status, len, ntypes;

	spin_lock(&rtas_data_buf_lock);

	/*
	 * Making system parameter: chips and sockets and cores per chip
	 * default to 1.
	 */
	phys_sockets = 1;
	phys_chipspersocket = 1;
	phys_coresperchip = 1;

	call_status = rtas_call(rtas_token("ibm,get-system-parameter"), 3, 1,
				NULL,
				PROCESSOR_MODULE_INFO,
				__pa(rtas_data_buf),
				RTAS_DATA_BUF_SIZE);

	if (call_status != 0) {
		pr_err("Error calling get-system-parameter %d\n",
		       call_status);
	} else {
		len = be16_to_cpup((__be16 *)&rtas_data_buf[0]);
		if (len < 8)
			goto out;

		ntypes = be16_to_cpup((__be16 *)&rtas_data_buf[2]);

		if (!ntypes)
			goto out;

		phys_sockets = be16_to_cpup((__be16 *)&rtas_data_buf[4]);
		phys_chipspersocket = be16_to_cpup((__be16 *)&rtas_data_buf[6]);
		phys_coresperchip = be16_to_cpup((__be16 *)&rtas_data_buf[8]);
	}

out:
	spin_unlock(&rtas_data_buf_lock);
}

/* Domains for which more than one result element are returned for each event. */
static bool domain_needs_aggregation(unsigned int domain)
{
	return aggregate_result_elements &&
			(domain == HV_PERF_DOMAIN_PHYS_CORE ||
			 (domain >= HV_PERF_DOMAIN_VCPU_HOME_CORE &&
			  domain <= HV_PERF_DOMAIN_VCPU_REMOTE_NODE));
}

static const char *domain_name(unsigned int domain)
{
	if (!domain_is_valid(domain))
		return NULL;

	switch (domain) {
	case HV_PERF_DOMAIN_PHYS_CHIP:		return "Physical Chip";
	case HV_PERF_DOMAIN_PHYS_CORE:		return "Physical Core";
	case HV_PERF_DOMAIN_VCPU_HOME_CORE:	return "VCPU Home Core";
	case HV_PERF_DOMAIN_VCPU_HOME_CHIP:	return "VCPU Home Chip";
	case HV_PERF_DOMAIN_VCPU_HOME_NODE:	return "VCPU Home Node";
	case HV_PERF_DOMAIN_VCPU_REMOTE_NODE:	return "VCPU Remote Node";
	}

	WARN_ON_ONCE(domain);
	return NULL;
}

static bool catalog_entry_domain_is_valid(unsigned int domain)
{
	/* POWER8 doesn't support virtual domains. */
	if (interface_version == 1)
		return is_physical_domain(domain);
	else
		return domain_is_valid(domain);
}

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
 *  perf stat -e 'hv_24x7/domain=2,offset=8,vcpu=0,lpar=0xffffffff/'
 */

/* u3 0-6, one of HV_24X7_PERF_DOMAIN */
EVENT_DEFINE_RANGE_FORMAT(domain, config, 0, 3);
/* u16 */
EVENT_DEFINE_RANGE_FORMAT(core, config, 16, 31);
EVENT_DEFINE_RANGE_FORMAT(chip, config, 16, 31);
EVENT_DEFINE_RANGE_FORMAT(vcpu, config, 16, 31);
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
	&format_attr_core.attr,
	&format_attr_chip.attr,
	&format_attr_vcpu.attr,
	&format_attr_lpar.attr,
	NULL,
};

static const struct attribute_group format_group = {
	.name = "format",
	.attrs = format_attrs,
};

static struct attribute_group event_group = {
	.name = "events",
	/* .attrs is set in init */
};

static struct attribute_group event_desc_group = {
	.name = "event_descs",
	/* .attrs is set in init */
};

static struct attribute_group event_long_desc_group = {
	.name = "event_long_descs",
	/* .attrs is set in init */
};

static struct kmem_cache *hv_page_cache;

static DEFINE_PER_CPU(int, hv_24x7_txn_flags);
static DEFINE_PER_CPU(int, hv_24x7_txn_err);

struct hv_24x7_hw {
	struct perf_event *events[255];
};

static DEFINE_PER_CPU(struct hv_24x7_hw, hv_24x7_hw);

/*
 * request_buffer and result_buffer are not required to be 4k aligned,
 * but are not allowed to cross any 4k boundary. Aligning them to 4k is
 * the simplest way to ensure that.
 */
#define H24x7_DATA_BUFFER_SIZE	4096
static DEFINE_PER_CPU(char, hv_24x7_reqb[H24x7_DATA_BUFFER_SIZE]) __aligned(4096);
static DEFINE_PER_CPU(char, hv_24x7_resb[H24x7_DATA_BUFFER_SIZE]) __aligned(4096);

static unsigned int max_num_requests(int interface_version)
{
	return (H24x7_DATA_BUFFER_SIZE - sizeof(struct hv_24x7_request_buffer))
		/ H24x7_REQUEST_SIZE(interface_version);
}

static char *event_name(struct hv_24x7_event_data *ev, int *len)
{
	*len = be16_to_cpu(ev->event_name_len) - 2;
	return (char *)ev->remainder;
}

static char *event_desc(struct hv_24x7_event_data *ev, int *len)
{
	unsigned int nl = be16_to_cpu(ev->event_name_len);
	__be16 *desc_len = (__be16 *)(ev->remainder + nl - 2);

	*len = be16_to_cpu(*desc_len) - 2;
	return (char *)ev->remainder + nl;
}

static char *event_long_desc(struct hv_24x7_event_data *ev, int *len)
{
	unsigned int nl = be16_to_cpu(ev->event_name_len);
	__be16 *desc_len_ = (__be16 *)(ev->remainder + nl - 2);
	unsigned int desc_len = be16_to_cpu(*desc_len_);
	__be16 *long_desc_len = (__be16 *)(ev->remainder + nl + desc_len - 2);

	*len = be16_to_cpu(*long_desc_len) - 2;
	return (char *)ev->remainder + nl + desc_len;
}

static bool event_fixed_portion_is_within(struct hv_24x7_event_data *ev,
					  void *end)
{
	void *start = ev;

	return (start + offsetof(struct hv_24x7_event_data, remainder)) < end;
}

/*
 * Things we don't check:
 *  - padding for desc, name, and long/detailed desc is required to be '\0'
 *    bytes.
 *
 *  Return NULL if we pass end,
 *  Otherwise return the address of the byte just following the event.
 */
static void *event_end(struct hv_24x7_event_data *ev, void *end)
{
	void *start = ev;
	__be16 *dl_, *ldl_;
	unsigned int dl, ldl;
	unsigned int nl = be16_to_cpu(ev->event_name_len);

	if (nl < 2) {
		pr_debug("%s: name length too short: %d", __func__, nl);
		return NULL;
	}

	if (start + nl > end) {
		pr_debug("%s: start=%p + nl=%u > end=%p",
				__func__, start, nl, end);
		return NULL;
	}

	dl_ = (__be16 *)(ev->remainder + nl - 2);
	if (!IS_ALIGNED((uintptr_t)dl_, 2))
		pr_warn("desc len not aligned %p", dl_);
	dl = be16_to_cpu(*dl_);
	if (dl < 2) {
		pr_debug("%s: desc len too short: %d", __func__, dl);
		return NULL;
	}

	if (start + nl + dl > end) {
		pr_debug("%s: (start=%p + nl=%u + dl=%u)=%p > end=%p",
				__func__, start, nl, dl, start + nl + dl, end);
		return NULL;
	}

	ldl_ = (__be16 *)(ev->remainder + nl + dl - 2);
	if (!IS_ALIGNED((uintptr_t)ldl_, 2))
		pr_warn("long desc len not aligned %p", ldl_);
	ldl = be16_to_cpu(*ldl_);
	if (ldl < 2) {
		pr_debug("%s: long desc len too short (ldl=%u)",
				__func__, ldl);
		return NULL;
	}

	if (start + nl + dl + ldl > end) {
		pr_debug("%s: start=%p + nl=%u + dl=%u + ldl=%u > end=%p",
				__func__, start, nl, dl, ldl, end);
		return NULL;
	}

	return start + nl + dl + ldl;
}

static long h_get_24x7_catalog_page_(unsigned long phys_4096,
				     unsigned long version, unsigned long index)
{
	pr_devel("h_get_24x7_catalog_page(0x%lx, %lu, %lu)",
			phys_4096, version, index);

	WARN_ON(!IS_ALIGNED(phys_4096, 4096));

	return plpar_hcall_norets(H_GET_24X7_CATALOG_PAGE,
			phys_4096, version, index);
}

static long h_get_24x7_catalog_page(char page[], u64 version, u32 index)
{
	return h_get_24x7_catalog_page_(virt_to_phys(page),
					version, index);
}

/*
 * Each event we find in the catalog, will have a sysfs entry. Format the
 * data for this sysfs entry based on the event's domain.
 *
 * Events belonging to the Chip domain can only be monitored in that domain.
 * i.e the domain for these events is a fixed/knwon value.
 *
 * Events belonging to the Core domain can be monitored either in the physical
 * core or in one of the virtual CPU domains. So the domain value for these
 * events must be specified by the user (i.e is a required parameter). Format
 * the Core events with 'domain=?' so the perf-tool can error check required
 * parameters.
 *
 * NOTE: For the Core domain events, rather than making domain a required
 *	 parameter we could default it to PHYS_CORE and allowe users to
 *	 override the domain to one of the VCPU domains.
 *
 *	 However, this can make the interface a little inconsistent.
 *
 *	 If we set domain=2 (PHYS_CHIP) and allow user to override this field
 *	 the user may be tempted to also modify the "offset=x" field in which
 *	 can lead to confusing usage. Consider the HPM_PCYC (offset=0x18) and
 *	 HPM_INST (offset=0x20) events. With:
 *
 *		perf stat -e hv_24x7/HPM_PCYC,offset=0x20/
 *
 *	we end up monitoring HPM_INST, while the command line has HPM_PCYC.
 *
 *	By not assigning a default value to the domain for the Core events,
 *	we can have simple guidelines:
 *
 *		- Specifying values for parameters with "=?" is required.
 *
 *		- Specifying (i.e overriding) values for other parameters
 *		  is undefined.
 */
static char *event_fmt(struct hv_24x7_event_data *event, unsigned int domain)
{
	const char *sindex;
	const char *lpar;
	const char *domain_str;
	char buf[8];

	switch (domain) {
	case HV_PERF_DOMAIN_PHYS_CHIP:
		snprintf(buf, sizeof(buf), "%d", domain);
		domain_str = buf;
		lpar = "0x0";
		sindex = "chip";
		break;
	case HV_PERF_DOMAIN_PHYS_CORE:
		domain_str = "?";
		lpar = "0x0";
		sindex = "core";
		break;
	default:
		domain_str = "?";
		lpar = "?";
		sindex = "vcpu";
	}

	return kasprintf(GFP_KERNEL,
			"domain=%s,offset=0x%x,%s=?,lpar=%s",
			domain_str,
			be16_to_cpu(event->event_counter_offs) +
				be16_to_cpu(event->event_group_record_offs),
			sindex,
			lpar);
}

/* Avoid trusting fw to NUL terminate strings */
static char *memdup_to_str(char *maybe_str, int max_len, gfp_t gfp)
{
	return kasprintf(gfp, "%.*s", max_len, maybe_str);
}

static ssize_t device_show_string(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct dev_ext_attribute *d;

	d = container_of(attr, struct dev_ext_attribute, attr);

	return sprintf(buf, "%s\n", (char *)d->var);
}

static ssize_t cpumask_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	return cpumap_print_to_pagebuf(true, buf, &hv_24x7_cpumask);
}

static ssize_t sockets_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", phys_sockets);
}

static ssize_t chipspersocket_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", phys_chipspersocket);
}

static ssize_t coresperchip_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", phys_coresperchip);
}

static struct attribute *device_str_attr_create_(char *name, char *str)
{
	struct dev_ext_attribute *attr = kzalloc(sizeof(*attr), GFP_KERNEL);

	if (!attr)
		return NULL;

	sysfs_attr_init(&attr->attr.attr);

	attr->var = str;
	attr->attr.attr.name = name;
	attr->attr.attr.mode = 0444;
	attr->attr.show = device_show_string;

	return &attr->attr.attr;
}

/*
 * Allocate and initialize strings representing event attributes.
 *
 * NOTE: The strings allocated here are never destroyed and continue to
 *	 exist till shutdown. This is to allow us to create as many events
 *	 from the catalog as possible, even if we encounter errors with some.
 *	 In case of changes to error paths in future, these may need to be
 *	 freed by the caller.
 */
static struct attribute *device_str_attr_create(char *name, int name_max,
						int name_nonce,
						char *str, size_t str_max)
{
	char *n;
	char *s = memdup_to_str(str, str_max, GFP_KERNEL);
	struct attribute *a;

	if (!s)
		return NULL;

	if (!name_nonce)
		n = kasprintf(GFP_KERNEL, "%.*s", name_max, name);
	else
		n = kasprintf(GFP_KERNEL, "%.*s__%d", name_max, name,
					name_nonce);
	if (!n)
		goto out_s;

	a = device_str_attr_create_(n, s);
	if (!a)
		goto out_n;

	return a;
out_n:
	kfree(n);
out_s:
	kfree(s);
	return NULL;
}

static struct attribute *event_to_attr(unsigned int ix,
				       struct hv_24x7_event_data *event,
				       unsigned int domain,
				       int nonce)
{
	int event_name_len;
	char *ev_name, *a_ev_name, *val;
	struct attribute *attr;

	if (!domain_is_valid(domain)) {
		pr_warn("catalog event %u has invalid domain %u\n",
				ix, domain);
		return NULL;
	}

	val = event_fmt(event, domain);
	if (!val)
		return NULL;

	ev_name = event_name(event, &event_name_len);
	if (!nonce)
		a_ev_name = kasprintf(GFP_KERNEL, "%.*s",
				(int)event_name_len, ev_name);
	else
		a_ev_name = kasprintf(GFP_KERNEL, "%.*s__%d",
				(int)event_name_len, ev_name, nonce);

	if (!a_ev_name)
		goto out_val;

	attr = device_str_attr_create_(a_ev_name, val);
	if (!attr)
		goto out_name;

	return attr;
out_name:
	kfree(a_ev_name);
out_val:
	kfree(val);
	return NULL;
}

static struct attribute *event_to_desc_attr(struct hv_24x7_event_data *event,
					    int nonce)
{
	int nl, dl;
	char *name = event_name(event, &nl);
	char *desc = event_desc(event, &dl);

	/* If there isn't a description, don't create the sysfs file */
	if (!dl)
		return NULL;

	return device_str_attr_create(name, nl, nonce, desc, dl);
}

static struct attribute *
event_to_long_desc_attr(struct hv_24x7_event_data *event, int nonce)
{
	int nl, dl;
	char *name = event_name(event, &nl);
	char *desc = event_long_desc(event, &dl);

	/* If there isn't a description, don't create the sysfs file */
	if (!dl)
		return NULL;

	return device_str_attr_create(name, nl, nonce, desc, dl);
}

static int event_data_to_attrs(unsigned int ix, struct attribute **attrs,
			       struct hv_24x7_event_data *event, int nonce)
{
	*attrs = event_to_attr(ix, event, event->domain, nonce);
	if (!*attrs)
		return -1;

	return 0;
}

/* */
struct event_uniq {
	struct rb_node node;
	const char *name;
	int nl;
	unsigned int ct;
	unsigned int domain;
};

static int memord(const void *d1, size_t s1, const void *d2, size_t s2)
{
	if (s1 < s2)
		return 1;
	if (s1 > s2)
		return -1;

	return memcmp(d1, d2, s1);
}

static int ev_uniq_ord(const void *v1, size_t s1, unsigned int d1,
		       const void *v2, size_t s2, unsigned int d2)
{
	int r = memord(v1, s1, v2, s2);

	if (r)
		return r;
	if (d1 > d2)
		return 1;
	if (d2 > d1)
		return -1;
	return 0;
}

static int event_uniq_add(struct rb_root *root, const char *name, int nl,
			  unsigned int domain)
{
	struct rb_node **new = &(root->rb_node), *parent = NULL;
	struct event_uniq *data;

	/* Figure out where to put new node */
	while (*new) {
		struct event_uniq *it;
		int result;

		it = rb_entry(*new, struct event_uniq, node);
		result = ev_uniq_ord(name, nl, domain, it->name, it->nl,
					it->domain);

		parent = *new;
		if (result < 0)
			new = &((*new)->rb_left);
		else if (result > 0)
			new = &((*new)->rb_right);
		else {
			it->ct++;
			pr_info("found a duplicate event %.*s, ct=%u\n", nl,
						name, it->ct);
			return it->ct;
		}
	}

	data = kmalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	*data = (struct event_uniq) {
		.name = name,
		.nl = nl,
		.ct = 0,
		.domain = domain,
	};

	/* Add new node and rebalance tree. */
	rb_link_node(&data->node, parent, new);
	rb_insert_color(&data->node, root);

	/* data->ct */
	return 0;
}

static void event_uniq_destroy(struct rb_root *root)
{
	/*
	 * the strings we point to are in the giant block of memory filled by
	 * the catalog, and are freed separately.
	 */
	struct event_uniq *pos, *n;

	rbtree_postorder_for_each_entry_safe(pos, n, root, node)
		kfree(pos);
}


/*
 * ensure the event structure's sizes are self consistent and don't cause us to
 * read outside of the event
 *
 * On success, return the event length in bytes.
 * Otherwise, return -1 (and print as appropriate).
 */
static ssize_t catalog_event_len_validate(struct hv_24x7_event_data *event,
					  size_t event_idx,
					  size_t event_data_bytes,
					  size_t event_entry_count,
					  size_t offset, void *end)
{
	ssize_t ev_len;
	void *ev_end, *calc_ev_end;

	if (offset >= event_data_bytes)
		return -1;

	if (event_idx >= event_entry_count) {
		pr_devel("catalog event data has %zu bytes of padding after last event\n",
				event_data_bytes - offset);
		return -1;
	}

	if (!event_fixed_portion_is_within(event, end)) {
		pr_warn("event %zu fixed portion is not within range\n",
				event_idx);
		return -1;
	}

	ev_len = be16_to_cpu(event->length);

	if (ev_len % 16)
		pr_info("event %zu has length %zu not divisible by 16: event=%pK\n",
				event_idx, ev_len, event);

	ev_end = (__u8 *)event + ev_len;
	if (ev_end > end) {
		pr_warn("event %zu has .length=%zu, ends after buffer end: ev_end=%pK > end=%pK, offset=%zu\n",
				event_idx, ev_len, ev_end, end,
				offset);
		return -1;
	}

	calc_ev_end = event_end(event, end);
	if (!calc_ev_end) {
		pr_warn("event %zu has a calculated length which exceeds buffer length %zu: event=%pK end=%pK, offset=%zu\n",
			event_idx, event_data_bytes, event, end,
			offset);
		return -1;
	}

	if (calc_ev_end > ev_end) {
		pr_warn("event %zu exceeds its own length: event=%pK, end=%pK, offset=%zu, calc_ev_end=%pK\n",
			event_idx, event, ev_end, offset, calc_ev_end);
		return -1;
	}

	return ev_len;
}

/*
 * Return true incase of invalid or dummy events with names like RESERVED*
 */
static bool ignore_event(const char *name)
{
	return strncmp(name, "RESERVED", 8) == 0;
}

#define MAX_4K (SIZE_MAX / 4096)

static int create_events_from_catalog(struct attribute ***events_,
				      struct attribute ***event_descs_,
				      struct attribute ***event_long_descs_)
{
	long hret;
	size_t catalog_len, catalog_page_len, event_entry_count,
	       event_data_len, event_data_offs,
	       event_data_bytes, junk_events, event_idx, event_attr_ct, i,
	       attr_max, event_idx_last, desc_ct, long_desc_ct;
	ssize_t ct, ev_len;
	uint64_t catalog_version_num;
	struct attribute **events, **event_descs, **event_long_descs;
	struct hv_24x7_catalog_page_0 *page_0 =
		kmem_cache_alloc(hv_page_cache, GFP_KERNEL);
	void *page = page_0;
	void *event_data, *end;
	struct hv_24x7_event_data *event;
	struct rb_root ev_uniq = RB_ROOT;
	int ret = 0;

	if (!page) {
		ret = -ENOMEM;
		goto e_out;
	}

	hret = h_get_24x7_catalog_page(page, 0, 0);
	if (hret) {
		ret = -EIO;
		goto e_free;
	}

	catalog_version_num = be64_to_cpu(page_0->version);
	catalog_page_len = be32_to_cpu(page_0->length);

	if (MAX_4K < catalog_page_len) {
		pr_err("invalid page count: %zu\n", catalog_page_len);
		ret = -EIO;
		goto e_free;
	}

	catalog_len = catalog_page_len * 4096;

	event_entry_count = be16_to_cpu(page_0->event_entry_count);
	event_data_offs   = be16_to_cpu(page_0->event_data_offs);
	event_data_len    = be16_to_cpu(page_0->event_data_len);

	pr_devel("cv %llu cl %zu eec %zu edo %zu edl %zu\n",
			catalog_version_num, catalog_len,
			event_entry_count, event_data_offs, event_data_len);

	if ((MAX_4K < event_data_len)
			|| (MAX_4K < event_data_offs)
			|| (MAX_4K - event_data_offs < event_data_len)) {
		pr_err("invalid event data offs %zu and/or len %zu\n",
				event_data_offs, event_data_len);
		ret = -EIO;
		goto e_free;
	}

	if ((event_data_offs + event_data_len) > catalog_page_len) {
		pr_err("event data %zu-%zu does not fit inside catalog 0-%zu\n",
				event_data_offs,
				event_data_offs + event_data_len,
				catalog_page_len);
		ret = -EIO;
		goto e_free;
	}

	if (SIZE_MAX - 1 < event_entry_count) {
		pr_err("event_entry_count %zu is invalid\n", event_entry_count);
		ret = -EIO;
		goto e_free;
	}

	event_data_bytes = event_data_len * 4096;

	/*
	 * event data can span several pages, events can cross between these
	 * pages. Use vmalloc to make this easier.
	 */
	event_data = vmalloc(event_data_bytes);
	if (!event_data) {
		pr_err("could not allocate event data\n");
		ret = -ENOMEM;
		goto e_free;
	}

	end = event_data + event_data_bytes;

	/*
	 * using vmalloc_to_phys() like this only works if PAGE_SIZE is
	 * divisible by 4096
	 */
	BUILD_BUG_ON(PAGE_SIZE % 4096);

	for (i = 0; i < event_data_len; i++) {
		hret = h_get_24x7_catalog_page_(
				vmalloc_to_phys(event_data + i * 4096),
				catalog_version_num,
				i + event_data_offs);
		if (hret) {
			pr_err("Failed to get event data in page %zu: rc=%ld\n",
			       i + event_data_offs, hret);
			ret = -EIO;
			goto e_event_data;
		}
	}

	/*
	 * scan the catalog to determine the number of attributes we need, and
	 * verify it at the same time.
	 */
	for (junk_events = 0, event = event_data, event_idx = 0, attr_max = 0;
	     ;
	     event_idx++, event = (void *)event + ev_len) {
		size_t offset = (void *)event - (void *)event_data;
		char *name;
		int nl;

		ev_len = catalog_event_len_validate(event, event_idx,
						    event_data_bytes,
						    event_entry_count,
						    offset, end);
		if (ev_len < 0)
			break;

		name = event_name(event, &nl);

		if (ignore_event(name)) {
			junk_events++;
			continue;
		}
		if (event->event_group_record_len == 0) {
			pr_devel("invalid event %zu (%.*s): group_record_len == 0, skipping\n",
					event_idx, nl, name);
			junk_events++;
			continue;
		}

		if (!catalog_entry_domain_is_valid(event->domain)) {
			pr_info("event %zu (%.*s) has invalid domain %d\n",
					event_idx, nl, name, event->domain);
			junk_events++;
			continue;
		}

		attr_max++;
	}

	event_idx_last = event_idx;
	if (event_idx_last != event_entry_count)
		pr_warn("event buffer ended before listed # of events were parsed (got %zu, wanted %zu, junk %zu)\n",
				event_idx_last, event_entry_count, junk_events);

	events = kmalloc_array(attr_max + 1, sizeof(*events), GFP_KERNEL);
	if (!events) {
		ret = -ENOMEM;
		goto e_event_data;
	}

	event_descs = kmalloc_array(event_idx + 1, sizeof(*event_descs),
				GFP_KERNEL);
	if (!event_descs) {
		ret = -ENOMEM;
		goto e_event_attrs;
	}

	event_long_descs = kmalloc_array(event_idx + 1,
			sizeof(*event_long_descs), GFP_KERNEL);
	if (!event_long_descs) {
		ret = -ENOMEM;
		goto e_event_descs;
	}

	/* Iterate over the catalog filling in the attribute vector */
	for (junk_events = 0, event_attr_ct = 0, desc_ct = 0, long_desc_ct = 0,
				event = event_data, event_idx = 0;
			event_idx < event_idx_last;
			event_idx++, ev_len = be16_to_cpu(event->length),
				event = (void *)event + ev_len) {
		char *name;
		int nl;
		int nonce;
		/*
		 * these are the only "bad" events that are intermixed and that
		 * we can ignore without issue. make sure to skip them here
		 */
		if (event->event_group_record_len == 0)
			continue;
		if (!catalog_entry_domain_is_valid(event->domain))
			continue;

		name  = event_name(event, &nl);
		if (ignore_event(name))
			continue;

		nonce = event_uniq_add(&ev_uniq, name, nl, event->domain);
		ct    = event_data_to_attrs(event_idx, events + event_attr_ct,
					    event, nonce);
		if (ct < 0) {
			pr_warn("event %zu (%.*s) creation failure, skipping\n",
				event_idx, nl, name);
			junk_events++;
		} else {
			event_attr_ct++;
			event_descs[desc_ct] = event_to_desc_attr(event, nonce);
			if (event_descs[desc_ct])
				desc_ct++;
			event_long_descs[long_desc_ct] =
					event_to_long_desc_attr(event, nonce);
			if (event_long_descs[long_desc_ct])
				long_desc_ct++;
		}
	}

	pr_info("read %zu catalog entries, created %zu event attrs (%zu failures), %zu descs\n",
			event_idx, event_attr_ct, junk_events, desc_ct);

	events[event_attr_ct] = NULL;
	event_descs[desc_ct] = NULL;
	event_long_descs[long_desc_ct] = NULL;

	event_uniq_destroy(&ev_uniq);
	vfree(event_data);
	kmem_cache_free(hv_page_cache, page);

	*events_ = events;
	*event_descs_ = event_descs;
	*event_long_descs_ = event_long_descs;
	return 0;

e_event_descs:
	kfree(event_descs);
e_event_attrs:
	kfree(events);
e_event_data:
	vfree(event_data);
e_free:
	kmem_cache_free(hv_page_cache, page);
e_out:
	*events_ = NULL;
	*event_descs_ = NULL;
	*event_long_descs_ = NULL;
	return ret;
}

static ssize_t catalog_read(struct file *filp, struct kobject *kobj,
			    struct bin_attribute *bin_attr, char *buf,
			    loff_t offset, size_t count)
{
	long hret;
	ssize_t ret = 0;
	size_t catalog_len = 0, catalog_page_len = 0;
	loff_t page_offset = 0;
	loff_t offset_in_page;
	size_t copy_len;
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
	offset_in_page = offset % 4096;

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

	copy_len = 4096 - offset_in_page;
	if (copy_len > count)
		copy_len = count;

	memcpy(buf, page+offset_in_page, copy_len);
	ret = copy_len;

e_free:
	if (hret)
		pr_err("h_get_24x7_catalog_page(ver=%lld, page=%lld) failed:"
		       " rc=%ld\n",
		       catalog_version_num, page_offset, hret);
	kmem_cache_free(hv_page_cache, page);

	pr_devel("catalog_read: offset=%lld(%lld) count=%zu "
			"catalog_len=%zu(%zu) => %zd\n", offset, page_offset,
			count, catalog_len, catalog_page_len, ret);

	return ret;
}

static ssize_t domains_show(struct device *dev, struct device_attribute *attr,
			    char *page)
{
	int d, n, count = 0;
	const char *str;

	for (d = 0; d < HV_PERF_DOMAIN_MAX; d++) {
		str = domain_name(d);
		if (!str)
			continue;

		n = sprintf(page, "%d: %s\n", d, str);
		if (n < 0)
			break;

		count += n;
		page += n;
	}
	return count;
}

#define PAGE_0_ATTR(_name, _fmt, _expr)				\
static ssize_t _name##_show(struct device *dev,			\
			    struct device_attribute *dev_attr,	\
			    char *buf)				\
{								\
	long hret;						\
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
	kmem_cache_free(hv_page_cache, page);			\
	return ret;						\
}								\
static DEVICE_ATTR_RO(_name)

PAGE_0_ATTR(catalog_version, "%lld\n",
		(unsigned long long)be64_to_cpu(page_0->version));
PAGE_0_ATTR(catalog_len, "%lld\n",
		(unsigned long long)be32_to_cpu(page_0->length) * 4096);
static BIN_ATTR_RO(catalog, 0/* real length varies */);
static DEVICE_ATTR_RO(domains);
static DEVICE_ATTR_RO(sockets);
static DEVICE_ATTR_RO(chipspersocket);
static DEVICE_ATTR_RO(coresperchip);
static DEVICE_ATTR_RO(cpumask);

static struct bin_attribute *if_bin_attrs[] = {
	&bin_attr_catalog,
	NULL,
};

static struct attribute *cpumask_attrs[] = {
	&dev_attr_cpumask.attr,
	NULL,
};

static const struct attribute_group cpumask_attr_group = {
	.attrs = cpumask_attrs,
};

static struct attribute *if_attrs[] = {
	&dev_attr_catalog_len.attr,
	&dev_attr_catalog_version.attr,
	&dev_attr_domains.attr,
	&dev_attr_sockets.attr,
	&dev_attr_chipspersocket.attr,
	&dev_attr_coresperchip.attr,
	NULL,
};

static const struct attribute_group if_group = {
	.name = "interface",
	.bin_attrs = if_bin_attrs,
	.attrs = if_attrs,
};

static const struct attribute_group *attr_groups[] = {
	&format_group,
	&event_group,
	&event_desc_group,
	&event_long_desc_group,
	&if_group,
	&cpumask_attr_group,
	NULL,
};

/*
 * Start the process for a new H_GET_24x7_DATA hcall.
 */
static void init_24x7_request(struct hv_24x7_request_buffer *request_buffer,
			      struct hv_24x7_data_result_buffer *result_buffer)
{

	memset(request_buffer, 0, H24x7_DATA_BUFFER_SIZE);
	memset(result_buffer, 0, H24x7_DATA_BUFFER_SIZE);

	request_buffer->interface_version = interface_version;
	/* memset above set request_buffer->num_requests to 0 */
}

/*
 * Commit (i.e perform) the H_GET_24x7_DATA hcall using the data collected
 * by 'init_24x7_request()' and 'add_event_to_24x7_request()'.
 */
static int make_24x7_request(struct hv_24x7_request_buffer *request_buffer,
			     struct hv_24x7_data_result_buffer *result_buffer)
{
	long ret;

	/*
	 * NOTE: Due to variable number of array elements in request and
	 *	 result buffer(s), sizeof() is not reliable. Use the actual
	 *	 allocated buffer size, H24x7_DATA_BUFFER_SIZE.
	 */
	ret = plpar_hcall_norets(H_GET_24X7_DATA,
			virt_to_phys(request_buffer), H24x7_DATA_BUFFER_SIZE,
			virt_to_phys(result_buffer),  H24x7_DATA_BUFFER_SIZE);

	if (ret) {
		struct hv_24x7_request *req;

		req = request_buffer->requests;
		pr_notice_ratelimited("hcall failed: [%d %#x %#x %d] => ret 0x%lx (%ld) detail=0x%x failing ix=%x\n",
				      req->performance_domain, req->data_offset,
				      req->starting_ix, req->starting_lpar_ix,
				      ret, ret, result_buffer->detailed_rc,
				      result_buffer->failing_request_ix);
		return -EIO;
	}

	return 0;
}

/*
 * Add the given @event to the next slot in the 24x7 request_buffer.
 *
 * Note that H_GET_24X7_DATA hcall allows reading several counters'
 * values in a single HCALL. We expect the caller to add events to the
 * request buffer one by one, make the HCALL and process the results.
 */
static int add_event_to_24x7_request(struct perf_event *event,
				struct hv_24x7_request_buffer *request_buffer)
{
	u16 idx;
	int i;
	size_t req_size;
	struct hv_24x7_request *req;

	if (request_buffer->num_requests >=
	    max_num_requests(request_buffer->interface_version)) {
		pr_devel("Too many requests for 24x7 HCALL %d\n",
				request_buffer->num_requests);
		return -EINVAL;
	}

	switch (event_get_domain(event)) {
	case HV_PERF_DOMAIN_PHYS_CHIP:
		idx = event_get_chip(event);
		break;
	case HV_PERF_DOMAIN_PHYS_CORE:
		idx = event_get_core(event);
		break;
	default:
		idx = event_get_vcpu(event);
	}

	req_size = H24x7_REQUEST_SIZE(request_buffer->interface_version);

	i = request_buffer->num_requests++;
	req = (void *) request_buffer->requests + i * req_size;

	req->performance_domain = event_get_domain(event);
	req->data_size = cpu_to_be16(8);
	req->data_offset = cpu_to_be32(event_get_offset(event));
	req->starting_lpar_ix = cpu_to_be16(event_get_lpar(event));
	req->max_num_lpars = cpu_to_be16(1);
	req->starting_ix = cpu_to_be16(idx);
	req->max_ix = cpu_to_be16(1);

	if (request_buffer->interface_version > 1) {
		if (domain_needs_aggregation(req->performance_domain))
			req->max_num_thread_groups = -1;
		else if (req->performance_domain != HV_PERF_DOMAIN_PHYS_CHIP) {
			req->starting_thread_group_ix = idx % 2;
			req->max_num_thread_groups = 1;
		}
	}

	return 0;
}

/**
 * get_count_from_result - get event count from all result elements in result
 *
 * If the event corresponding to this result needs aggregation of the result
 * element values, then this function does that.
 *
 * @event:	Event associated with @res.
 * @resb:	Result buffer containing @res.
 * @res:	Result to work on.
 * @countp:	Output variable containing the event count.
 * @next:	Optional output variable pointing to the next result in @resb.
 */
static int get_count_from_result(struct perf_event *event,
				 struct hv_24x7_data_result_buffer *resb,
				 struct hv_24x7_result *res, u64 *countp,
				 struct hv_24x7_result **next)
{
	u16 num_elements = be16_to_cpu(res->num_elements_returned);
	u16 data_size = be16_to_cpu(res->result_element_data_size);
	unsigned int data_offset;
	void *element_data;
	int i;
	u64 count;

	/*
	 * We can bail out early if the result is empty.
	 */
	if (!num_elements) {
		pr_debug("Result of request %hhu is empty, nothing to do\n",
			 res->result_ix);

		if (next)
			*next = (struct hv_24x7_result *) res->elements;

		return -ENODATA;
	}

	/*
	 * Since we always specify 1 as the maximum for the smallest resource
	 * we're requesting, there should to be only one element per result.
	 * Except when an event needs aggregation, in which case there are more.
	 */
	if (num_elements != 1 &&
	    !domain_needs_aggregation(event_get_domain(event))) {
		pr_err("Error: result of request %hhu has %hu elements\n",
		       res->result_ix, num_elements);

		return -EIO;
	}

	if (data_size != sizeof(u64)) {
		pr_debug("Error: result of request %hhu has data of %hu bytes\n",
			 res->result_ix, data_size);

		return -ENOTSUPP;
	}

	if (resb->interface_version == 1)
		data_offset = offsetof(struct hv_24x7_result_element_v1,
				       element_data);
	else
		data_offset = offsetof(struct hv_24x7_result_element_v2,
				       element_data);

	/* Go through the result elements in the result. */
	for (i = count = 0, element_data = res->elements + data_offset;
	     i < num_elements;
	     i++, element_data += data_size + data_offset)
		count += be64_to_cpu(*((u64 *) element_data));

	*countp = count;

	/* The next result is after the last result element. */
	if (next)
		*next = element_data - data_offset;

	return 0;
}

static int single_24x7_request(struct perf_event *event, u64 *count)
{
	int ret;
	struct hv_24x7_request_buffer *request_buffer;
	struct hv_24x7_data_result_buffer *result_buffer;

	BUILD_BUG_ON(sizeof(*request_buffer) > 4096);
	BUILD_BUG_ON(sizeof(*result_buffer) > 4096);

	request_buffer = (void *)get_cpu_var(hv_24x7_reqb);
	result_buffer = (void *)get_cpu_var(hv_24x7_resb);

	init_24x7_request(request_buffer, result_buffer);

	ret = add_event_to_24x7_request(event, request_buffer);
	if (ret)
		goto out;

	ret = make_24x7_request(request_buffer, result_buffer);
	if (ret)
		goto out;

	/* process result from hcall */
	ret = get_count_from_result(event, result_buffer,
				    result_buffer->results, count, NULL);

out:
	put_cpu_var(hv_24x7_reqb);
	put_cpu_var(hv_24x7_resb);
	return ret;
}


static int h_24x7_event_init(struct perf_event *event)
{
	struct hv_perf_caps caps;
	unsigned int domain;
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

	/* no branch sampling */
	if (has_branch_stack(event))
		return -EOPNOTSUPP;

	/* offset must be 8 byte aligned */
	if (event_get_offset(event) % 8) {
		pr_devel("bad alignment\n");
		return -EINVAL;
	}

	domain = event_get_domain(event);
	if (domain  == 0 || domain >= HV_PERF_DOMAIN_MAX) {
		pr_devel("invalid domain %d\n", domain);
		return -EINVAL;
	}

	hret = hv_perf_caps_get(&caps);
	if (hret) {
		pr_devel("could not get capabilities: rc=%ld\n", hret);
		return -EIO;
	}

	/* Physical domains & other lpars require extra capabilities */
	if (!caps.collect_privileged && (is_physical_domain(domain) ||
		(event_get_lpar(event) != event_get_lpar_max()))) {
		pr_devel("hv permissions disallow: is_physical_domain:%d, lpar=0x%llx\n",
				is_physical_domain(domain),
				event_get_lpar(event));
		return -EACCES;
	}

	/* Get the initial value of the counter for this event */
	if (single_24x7_request(event, &ct)) {
		pr_devel("test hcall failed\n");
		return -EIO;
	}
	(void)local64_xchg(&event->hw.prev_count, ct);

	return 0;
}

static u64 h_24x7_get_value(struct perf_event *event)
{
	u64 ct;

	if (single_24x7_request(event, &ct))
		/* We checked this in event init, shouldn't fail here... */
		return 0;

	return ct;
}

static void update_event_count(struct perf_event *event, u64 now)
{
	s64 prev;

	prev = local64_xchg(&event->hw.prev_count, now);
	local64_add(now - prev, &event->count);
}

static void h_24x7_event_read(struct perf_event *event)
{
	u64 now;
	struct hv_24x7_request_buffer *request_buffer;
	struct hv_24x7_hw *h24x7hw;
	int txn_flags;

	txn_flags = __this_cpu_read(hv_24x7_txn_flags);

	/*
	 * If in a READ transaction, add this counter to the list of
	 * counters to read during the next HCALL (i.e commit_txn()).
	 * If not in a READ transaction, go ahead and make the HCALL
	 * to read this counter by itself.
	 */

	if (txn_flags & PERF_PMU_TXN_READ) {
		int i;
		int ret;

		if (__this_cpu_read(hv_24x7_txn_err))
			return;

		request_buffer = (void *)get_cpu_var(hv_24x7_reqb);

		ret = add_event_to_24x7_request(event, request_buffer);
		if (ret) {
			__this_cpu_write(hv_24x7_txn_err, ret);
		} else {
			/*
			 * Associate the event with the HCALL request index,
			 * so ->commit_txn() can quickly find/update count.
			 */
			i = request_buffer->num_requests - 1;

			h24x7hw = &get_cpu_var(hv_24x7_hw);
			h24x7hw->events[i] = event;
			put_cpu_var(h24x7hw);
		}

		put_cpu_var(hv_24x7_reqb);
	} else {
		now = h_24x7_get_value(event);
		update_event_count(event, now);
	}
}

static void h_24x7_event_start(struct perf_event *event, int flags)
{
	if (flags & PERF_EF_RELOAD)
		local64_set(&event->hw.prev_count, h_24x7_get_value(event));
}

static void h_24x7_event_stop(struct perf_event *event, int flags)
{
	h_24x7_event_read(event);
}

static int h_24x7_event_add(struct perf_event *event, int flags)
{
	if (flags & PERF_EF_START)
		h_24x7_event_start(event, flags);

	return 0;
}

/*
 * 24x7 counters only support READ transactions. They are
 * always counting and dont need/support ADD transactions.
 * Cache the flags, but otherwise ignore transactions that
 * are not PERF_PMU_TXN_READ.
 */
static void h_24x7_event_start_txn(struct pmu *pmu, unsigned int flags)
{
	struct hv_24x7_request_buffer *request_buffer;
	struct hv_24x7_data_result_buffer *result_buffer;

	/* We should not be called if we are already in a txn */
	WARN_ON_ONCE(__this_cpu_read(hv_24x7_txn_flags));

	__this_cpu_write(hv_24x7_txn_flags, flags);
	if (flags & ~PERF_PMU_TXN_READ)
		return;

	request_buffer = (void *)get_cpu_var(hv_24x7_reqb);
	result_buffer = (void *)get_cpu_var(hv_24x7_resb);

	init_24x7_request(request_buffer, result_buffer);

	put_cpu_var(hv_24x7_resb);
	put_cpu_var(hv_24x7_reqb);
}

/*
 * Clean up transaction state.
 *
 * NOTE: Ignore state of request and result buffers for now.
 *	 We will initialize them during the next read/txn.
 */
static void reset_txn(void)
{
	__this_cpu_write(hv_24x7_txn_flags, 0);
	__this_cpu_write(hv_24x7_txn_err, 0);
}

/*
 * 24x7 counters only support READ transactions. They are always counting
 * and dont need/support ADD transactions. Clear ->txn_flags but otherwise
 * ignore transactions that are not of type PERF_PMU_TXN_READ.
 *
 * For READ transactions, submit all pending 24x7 requests (i.e requests
 * that were queued by h_24x7_event_read()), to the hypervisor and update
 * the event counts.
 */
static int h_24x7_event_commit_txn(struct pmu *pmu)
{
	struct hv_24x7_request_buffer *request_buffer;
	struct hv_24x7_data_result_buffer *result_buffer;
	struct hv_24x7_result *res, *next_res;
	u64 count;
	int i, ret, txn_flags;
	struct hv_24x7_hw *h24x7hw;

	txn_flags = __this_cpu_read(hv_24x7_txn_flags);
	WARN_ON_ONCE(!txn_flags);

	ret = 0;
	if (txn_flags & ~PERF_PMU_TXN_READ)
		goto out;

	ret = __this_cpu_read(hv_24x7_txn_err);
	if (ret)
		goto out;

	request_buffer = (void *)get_cpu_var(hv_24x7_reqb);
	result_buffer = (void *)get_cpu_var(hv_24x7_resb);

	ret = make_24x7_request(request_buffer, result_buffer);
	if (ret)
		goto put_reqb;

	h24x7hw = &get_cpu_var(hv_24x7_hw);

	/* Go through results in the result buffer to update event counts. */
	for (i = 0, res = result_buffer->results;
	     i < result_buffer->num_results; i++, res = next_res) {
		struct perf_event *event = h24x7hw->events[res->result_ix];

		ret = get_count_from_result(event, result_buffer, res, &count,
					    &next_res);
		if (ret)
			break;

		update_event_count(event, count);
	}

	put_cpu_var(hv_24x7_hw);

put_reqb:
	put_cpu_var(hv_24x7_resb);
	put_cpu_var(hv_24x7_reqb);
out:
	reset_txn();
	return ret;
}

/*
 * 24x7 counters only support READ transactions. They are always counting
 * and dont need/support ADD transactions. However, regardless of type
 * of transaction, all we need to do is cleanup, so we don't have to check
 * the type of transaction.
 */
static void h_24x7_event_cancel_txn(struct pmu *pmu)
{
	WARN_ON_ONCE(!__this_cpu_read(hv_24x7_txn_flags));
	reset_txn();
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
	.read        = h_24x7_event_read,
	.start_txn   = h_24x7_event_start_txn,
	.commit_txn  = h_24x7_event_commit_txn,
	.cancel_txn  = h_24x7_event_cancel_txn,
	.capabilities = PERF_PMU_CAP_NO_EXCLUDE,
};

static int ppc_hv_24x7_cpu_online(unsigned int cpu)
{
	if (cpumask_empty(&hv_24x7_cpumask))
		cpumask_set_cpu(cpu, &hv_24x7_cpumask);

	return 0;
}

static int ppc_hv_24x7_cpu_offline(unsigned int cpu)
{
	int target;

	/* Check if exiting cpu is used for collecting 24x7 events */
	if (!cpumask_test_and_clear_cpu(cpu, &hv_24x7_cpumask))
		return 0;

	/* Find a new cpu to collect 24x7 events */
	target = cpumask_last(cpu_active_mask);

	if (target < 0 || target >= nr_cpu_ids) {
		pr_err("hv_24x7: CPU hotplug init failed\n");
		return -1;
	}

	/* Migrate 24x7 events to the new target */
	cpumask_set_cpu(target, &hv_24x7_cpumask);
	perf_pmu_migrate_context(&h_24x7_pmu, cpu, target);

	return 0;
}

static int hv_24x7_cpu_hotplug_init(void)
{
	return cpuhp_setup_state(CPUHP_AP_PERF_POWERPC_HV_24x7_ONLINE,
			  "perf/powerpc/hv_24x7:online",
			  ppc_hv_24x7_cpu_online,
			  ppc_hv_24x7_cpu_offline);
}

static int hv_24x7_init(void)
{
	int r;
	unsigned long hret;
	unsigned int pvr = mfspr(SPRN_PVR);
	struct hv_perf_caps caps;

	if (!firmware_has_feature(FW_FEATURE_LPAR)) {
		pr_debug("not a virtualized system, not enabling\n");
		return -ENODEV;
	}

	/* POWER8 only supports v1, while POWER9 only supports v2. */
	if (PVR_VER(pvr) == PVR_POWER8)
		interface_version = 1;
	else {
		interface_version = 2;

		/* SMT8 in POWER9 needs to aggregate result elements. */
		if (threads_per_core == 8)
			aggregate_result_elements = true;
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

	r = create_events_from_catalog(&event_group.attrs,
				   &event_desc_group.attrs,
				   &event_long_desc_group.attrs);

	if (r)
		return r;

	/* init cpuhotplug */
	r = hv_24x7_cpu_hotplug_init();
	if (r)
		return r;

	r = perf_pmu_register(&h_24x7_pmu, h_24x7_pmu.name, -1);
	if (r)
		return r;

	read_24x7_sys_info();

	return 0;
}

device_initcall(hv_24x7_init);

/* mdesc.c: Sun4V machine description handling.
 *
 * Copyright (C) 2007 David S. Miller <davem@davemloft.net>
 */
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/bootmem.h>
#include <linux/log2.h>

#include <asm/hypervisor.h>
#include <asm/mdesc.h>
#include <asm/prom.h>
#include <asm/oplib.h>
#include <asm/smp.h>

/* Unlike the OBP device tree, the machine description is a full-on
 * DAG.  An arbitrary number of ARCs are possible from one
 * node to other nodes and thus we can't use the OBP device_node
 * data structure to represent these nodes inside of the kernel.
 *
 * Actually, it isn't even a DAG, because there are back pointers
 * which create cycles in the graph.
 *
 * mdesc_hdr and mdesc_elem describe the layout of the data structure
 * we get from the Hypervisor.
 */
struct mdesc_hdr {
	u32	version; /* Transport version */
	u32	node_sz; /* node block size */
	u32	name_sz; /* name block size */
	u32	data_sz; /* data block size */
};

struct mdesc_elem {
	u8	tag;
#define MD_LIST_END	0x00
#define MD_NODE		0x4e
#define MD_NODE_END	0x45
#define MD_NOOP		0x20
#define MD_PROP_ARC	0x61
#define MD_PROP_VAL	0x76
#define MD_PROP_STR	0x73
#define MD_PROP_DATA	0x64
	u8	name_len;
	u16	resv;
	u32	name_offset;
	union {
		struct {
			u32	data_len;
			u32	data_offset;
		} data;
		u64	val;
	} d;
};

static struct mdesc_hdr *main_mdesc;
static struct mdesc_node *allnodes;

static struct mdesc_node *allnodes_tail;
static unsigned int unique_id;

static struct mdesc_node **mdesc_hash;
static unsigned int mdesc_hash_size;

static inline unsigned int node_hashfn(u64 node)
{
	return ((unsigned int) (node ^ (node >> 8) ^ (node >> 16)))
		& (mdesc_hash_size - 1);
}

static inline void hash_node(struct mdesc_node *mp)
{
	struct mdesc_node **head = &mdesc_hash[node_hashfn(mp->node)];

	mp->hash_next = *head;
	*head = mp;

	if (allnodes_tail) {
		allnodes_tail->allnodes_next = mp;
		allnodes_tail = mp;
	} else {
		allnodes = allnodes_tail = mp;
	}
}

static struct mdesc_node *find_node(u64 node)
{
	struct mdesc_node *mp = mdesc_hash[node_hashfn(node)];

	while (mp) {
		if (mp->node == node)
			return mp;

		mp = mp->hash_next;
	}
	return NULL;
}

struct property *md_find_property(const struct mdesc_node *mp,
				  const char *name,
				  int *lenp)
{
	struct property *pp;

	for (pp = mp->properties; pp != 0; pp = pp->next) {
		if (strcasecmp(pp->name, name) == 0) {
			if (lenp)
				*lenp = pp->length;
			break;
		}
	}
	return pp;
}
EXPORT_SYMBOL(md_find_property);

/*
 * Find a property with a given name for a given node
 * and return the value.
 */
const void *md_get_property(const struct mdesc_node *mp, const char *name,
			    int *lenp)
{
	struct property *pp = md_find_property(mp, name, lenp);
	return pp ? pp->value : NULL;
}
EXPORT_SYMBOL(md_get_property);

struct mdesc_node *md_find_node_by_name(struct mdesc_node *from,
					const char *name)
{
	struct mdesc_node *mp;

	mp = from ? from->allnodes_next : allnodes;
	for (; mp != NULL; mp = mp->allnodes_next) {
		if (strcmp(mp->name, name) == 0)
			break;
	}
	return mp;
}
EXPORT_SYMBOL(md_find_node_by_name);

static unsigned int mdesc_early_allocated;

static void * __init mdesc_early_alloc(unsigned long size)
{
	void *ret;

	ret = __alloc_bootmem(size, SMP_CACHE_BYTES, 0UL);
	if (ret == NULL) {
		prom_printf("MDESC: alloc of %lu bytes failed.\n", size);
		prom_halt();
	}

	memset(ret, 0, size);

	mdesc_early_allocated += size;

	return ret;
}

static unsigned int __init count_arcs(struct mdesc_elem *ep)
{
	unsigned int ret = 0;

	ep++;
	while (ep->tag != MD_NODE_END) {
		if (ep->tag == MD_PROP_ARC)
			ret++;
		ep++;
	}
	return ret;
}

static void __init mdesc_node_alloc(u64 node, struct mdesc_elem *ep, const char *names)
{
	unsigned int num_arcs = count_arcs(ep);
	struct mdesc_node *mp;

	mp = mdesc_early_alloc(sizeof(*mp) +
			       (num_arcs * sizeof(struct mdesc_arc)));
	mp->name = names + ep->name_offset;
	mp->node = node;
	mp->unique_id = unique_id++;
	mp->num_arcs = num_arcs;

	hash_node(mp);
}

static inline struct mdesc_elem *node_block(struct mdesc_hdr *mdesc)
{
	return (struct mdesc_elem *) (mdesc + 1);
}

static inline void *name_block(struct mdesc_hdr *mdesc)
{
	return ((void *) node_block(mdesc)) + mdesc->node_sz;
}

static inline void *data_block(struct mdesc_hdr *mdesc)
{
	return ((void *) name_block(mdesc)) + mdesc->name_sz;
}

/* In order to avoid recursion (the graph can be very deep) we use a
 * two pass algorithm.  First we allocate all the nodes and hash them.
 * Then we iterate over each node, filling in the arcs and properties.
 */
static void __init build_all_nodes(struct mdesc_hdr *mdesc)
{
	struct mdesc_elem *start, *ep;
	struct mdesc_node *mp;
	const char *names;
	void *data;
	u64 last_node;

	start = ep = node_block(mdesc);
	last_node = mdesc->node_sz / 16;

	names = name_block(mdesc);

	while (1) {
		u64 node = ep - start;

		if (ep->tag == MD_LIST_END)
			break;

		if (ep->tag != MD_NODE) {
			prom_printf("MDESC: Inconsistent element list.\n");
			prom_halt();
		}

		mdesc_node_alloc(node, ep, names);

		if (ep->d.val >= last_node) {
			printk("MDESC: Warning, early break out of node scan.\n");
			printk("MDESC: Next node [%lu] last_node [%lu].\n",
			       node, last_node);
			break;
		}

		ep = start + ep->d.val;
	}

	data = data_block(mdesc);
	for (mp = allnodes; mp; mp = mp->allnodes_next) {
		struct mdesc_elem *ep = start + mp->node;
		struct property **link = &mp->properties;
		unsigned int this_arc = 0;

		ep++;
		while (ep->tag != MD_NODE_END) {
			switch (ep->tag) {
			case MD_PROP_ARC: {
				struct mdesc_node *target;

				if (this_arc >= mp->num_arcs) {
					prom_printf("MDESC: ARC overrun [%u:%u]\n",
						    this_arc, mp->num_arcs);
					prom_halt();
				}
				target = find_node(ep->d.val);
				if (!target) {
					printk("MDESC: Warning, arc points to "
					       "missing node, ignoring.\n");
					break;
				}
				mp->arcs[this_arc].name =
					(names + ep->name_offset);
				mp->arcs[this_arc].arc = target;
				this_arc++;
				break;
			}

			case MD_PROP_VAL:
			case MD_PROP_STR:
			case MD_PROP_DATA: {
				struct property *p = mdesc_early_alloc(sizeof(*p));

				p->unique_id = unique_id++;
				p->name = (char *) names + ep->name_offset;
				if (ep->tag == MD_PROP_VAL) {
					p->value = &ep->d.val;
					p->length = 8;
				} else {
					p->value = data + ep->d.data.data_offset;
					p->length = ep->d.data.data_len;
				}
				*link = p;
				link = &p->next;
				break;
			}

			case MD_NOOP:
				break;

			default:
				printk("MDESC: Warning, ignoring unknown tag type %02x\n",
				       ep->tag);
			}
			ep++;
		}
	}
}

static unsigned int __init count_nodes(struct mdesc_hdr *mdesc)
{
	struct mdesc_elem *ep = node_block(mdesc);
	struct mdesc_elem *end;
	unsigned int cnt = 0;

	end = ((void *)ep) + mdesc->node_sz;
	while (ep < end) {
		if (ep->tag == MD_NODE)
			cnt++;
		ep++;
	}
	return cnt;
}

static void __init report_platform_properties(void)
{
	struct mdesc_node *pn = md_find_node_by_name(NULL, "platform");
	const char *s;
	const u64 *v;

	if (!pn) {
		prom_printf("No platform node in machine-description.\n");
		prom_halt();
	}

	s = md_get_property(pn, "banner-name", NULL);
	printk("PLATFORM: banner-name [%s]\n", s);
	s = md_get_property(pn, "name", NULL);
	printk("PLATFORM: name [%s]\n", s);

	v = md_get_property(pn, "hostid", NULL);
	if (v)
		printk("PLATFORM: hostid [%08lx]\n", *v);
	v = md_get_property(pn, "serial#", NULL);
	if (v)
		printk("PLATFORM: serial# [%08lx]\n", *v);
	v = md_get_property(pn, "stick-frequency", NULL);
	printk("PLATFORM: stick-frequency [%08lx]\n", *v);
	v = md_get_property(pn, "mac-address", NULL);
	if (v)
		printk("PLATFORM: mac-address [%lx]\n", *v);
	v = md_get_property(pn, "watchdog-resolution", NULL);
	if (v)
		printk("PLATFORM: watchdog-resolution [%lu ms]\n", *v);
	v = md_get_property(pn, "watchdog-max-timeout", NULL);
	if (v)
		printk("PLATFORM: watchdog-max-timeout [%lu ms]\n", *v);
	v = md_get_property(pn, "max-cpus", NULL);
	if (v)
		printk("PLATFORM: max-cpus [%lu]\n", *v);
}

static int inline find_in_proplist(const char *list, const char *match, int len)
{
	while (len > 0) {
		int l;

		if (!strcmp(list, match))
			return 1;
		l = strlen(list) + 1;
		list += l;
		len -= l;
	}
	return 0;
}

static void __init fill_in_one_cache(cpuinfo_sparc *c, struct mdesc_node *mp)
{
	const u64 *level = md_get_property(mp, "level", NULL);
	const u64 *size = md_get_property(mp, "size", NULL);
	const u64 *line_size = md_get_property(mp, "line-size", NULL);
	const char *type;
	int type_len;

	type = md_get_property(mp, "type", &type_len);

	switch (*level) {
	case 1:
		if (find_in_proplist(type, "instn", type_len)) {
			c->icache_size = *size;
			c->icache_line_size = *line_size;
		} else if (find_in_proplist(type, "data", type_len)) {
			c->dcache_size = *size;
			c->dcache_line_size = *line_size;
		}
		break;

	case 2:
		c->ecache_size = *size;
		c->ecache_line_size = *line_size;
		break;

	default:
		break;
	}

	if (*level == 1) {
		unsigned int i;

		for (i = 0; i < mp->num_arcs; i++) {
			struct mdesc_node *t = mp->arcs[i].arc;

			if (strcmp(mp->arcs[i].name, "fwd"))
				continue;

			if (!strcmp(t->name, "cache"))
				fill_in_one_cache(c, t);
		}
	}
}

static void __init mark_core_ids(struct mdesc_node *mp, int core_id)
{
	unsigned int i;

	for (i = 0; i < mp->num_arcs; i++) {
		struct mdesc_node *t = mp->arcs[i].arc;
		const u64 *id;

		if (strcmp(mp->arcs[i].name, "back"))
			continue;

		if (!strcmp(t->name, "cpu")) {
			id = md_get_property(t, "id", NULL);
			if (*id < NR_CPUS)
				cpu_data(*id).core_id = core_id;
		} else {
			unsigned int j;

			for (j = 0; j < t->num_arcs; j++) {
				struct mdesc_node *n = t->arcs[j].arc;

				if (strcmp(t->arcs[j].name, "back"))
					continue;

				if (strcmp(n->name, "cpu"))
					continue;

				id = md_get_property(n, "id", NULL);
				if (*id < NR_CPUS)
					cpu_data(*id).core_id = core_id;
			}
		}
	}
}

static void __init set_core_ids(void)
{
	struct mdesc_node *mp;
	int idx;

	idx = 1;
	md_for_each_node_by_name(mp, "cache") {
		const u64 *level = md_get_property(mp, "level", NULL);
		const char *type;
		int len;

		if (*level != 1)
			continue;

		type = md_get_property(mp, "type", &len);
		if (!find_in_proplist(type, "instn", len))
			continue;

		mark_core_ids(mp, idx);

		idx++;
	}
}

static void __init get_one_mondo_bits(const u64 *p, unsigned int *mask, unsigned char def)
{
	u64 val;

	if (!p)
		goto use_default;
	val = *p;

	if (!val || val >= 64)
		goto use_default;

	*mask = ((1U << val) * 64U) - 1U;
	return;

use_default:
	*mask = ((1U << def) * 64U) - 1U;
}

static void __init get_mondo_data(struct mdesc_node *mp, struct trap_per_cpu *tb)
{
	const u64 *val;

	val = md_get_property(mp, "q-cpu-mondo-#bits", NULL);
	get_one_mondo_bits(val, &tb->cpu_mondo_qmask, 7);

	val = md_get_property(mp, "q-dev-mondo-#bits", NULL);
	get_one_mondo_bits(val, &tb->dev_mondo_qmask, 7);

	val = md_get_property(mp, "q-resumable-#bits", NULL);
	get_one_mondo_bits(val, &tb->resum_qmask, 6);

	val = md_get_property(mp, "q-nonresumable-#bits", NULL);
	get_one_mondo_bits(val, &tb->nonresum_qmask, 2);
}

static void __init mdesc_fill_in_cpu_data(void)
{
	struct mdesc_node *mp;

	ncpus_probed = 0;
	md_for_each_node_by_name(mp, "cpu") {
		const u64 *id = md_get_property(mp, "id", NULL);
		const u64 *cfreq = md_get_property(mp, "clock-frequency", NULL);
		struct trap_per_cpu *tb;
		cpuinfo_sparc *c;
		unsigned int i;
		int cpuid;

		ncpus_probed++;

		cpuid = *id;

#ifdef CONFIG_SMP
		if (cpuid >= NR_CPUS)
			continue;
#else
		/* On uniprocessor we only want the values for the
		 * real physical cpu the kernel booted onto, however
		 * cpu_data() only has one entry at index 0.
		 */
		if (cpuid != real_hard_smp_processor_id())
			continue;
		cpuid = 0;
#endif

		c = &cpu_data(cpuid);
		c->clock_tick = *cfreq;

		tb = &trap_block[cpuid];
		get_mondo_data(mp, tb);

		for (i = 0; i < mp->num_arcs; i++) {
			struct mdesc_node *t = mp->arcs[i].arc;
			unsigned int j;

			if (strcmp(mp->arcs[i].name, "fwd"))
				continue;

			if (!strcmp(t->name, "cache")) {
				fill_in_one_cache(c, t);
				continue;
			}

			for (j = 0; j < t->num_arcs; j++) {
				struct mdesc_node *n;

				n = t->arcs[j].arc;
				if (strcmp(t->arcs[j].name, "fwd"))
					continue;

				if (!strcmp(n->name, "cache"))
					fill_in_one_cache(c, n);
			}
		}

#ifdef CONFIG_SMP
		cpu_set(cpuid, cpu_present_map);
		cpu_set(cpuid, phys_cpu_present_map);
#endif

		c->core_id = 0;
	}

	set_core_ids();

	smp_fill_in_sib_core_maps();
}

void __init sun4v_mdesc_init(void)
{
	unsigned long len, real_len, status;

	(void) sun4v_mach_desc(0UL, 0UL, &len);

	printk("MDESC: Size is %lu bytes.\n", len);

	main_mdesc = mdesc_early_alloc(len);

	status = sun4v_mach_desc(__pa(main_mdesc), len, &real_len);
	if (status != HV_EOK || real_len > len) {
		prom_printf("sun4v_mach_desc fails, err(%lu), "
			    "len(%lu), real_len(%lu)\n",
			    status, len, real_len);
		prom_halt();
	}

	len = count_nodes(main_mdesc);
	printk("MDESC: %lu nodes.\n", len);

	len = roundup_pow_of_two(len);

	mdesc_hash = mdesc_early_alloc(len * sizeof(struct mdesc_node *));
	mdesc_hash_size = len;

	printk("MDESC: Hash size %lu entries.\n", len);

	build_all_nodes(main_mdesc);

	printk("MDESC: Built graph with %u bytes of memory.\n",
	       mdesc_early_allocated);

	report_platform_properties();
	mdesc_fill_in_cpu_data();
}

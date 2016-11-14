/* mdesc.c: Sun4V machine description handling.
 *
 * Copyright (C) 2007, 2008 David S. Miller <davem@davemloft.net>
 */
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/memblock.h>
#include <linux/log2.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/miscdevice.h>
#include <linux/bootmem.h>
#include <linux/export.h>

#include <asm/cpudata.h>
#include <asm/hypervisor.h>
#include <asm/mdesc.h>
#include <asm/prom.h>
#include <asm/uaccess.h>
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
} __attribute__((aligned(16)));

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

struct mdesc_mem_ops {
	struct mdesc_handle *(*alloc)(unsigned int mdesc_size);
	void (*free)(struct mdesc_handle *handle);
};

struct mdesc_handle {
	struct list_head	list;
	struct mdesc_mem_ops	*mops;
	void			*self_base;
	atomic_t		refcnt;
	unsigned int		handle_size;
	struct mdesc_hdr	mdesc;
};

static void mdesc_handle_init(struct mdesc_handle *hp,
			      unsigned int handle_size,
			      void *base)
{
	BUG_ON(((unsigned long)&hp->mdesc) & (16UL - 1));

	memset(hp, 0, handle_size);
	INIT_LIST_HEAD(&hp->list);
	hp->self_base = base;
	atomic_set(&hp->refcnt, 1);
	hp->handle_size = handle_size;
}

static struct mdesc_handle * __init mdesc_memblock_alloc(unsigned int mdesc_size)
{
	unsigned int handle_size, alloc_size;
	struct mdesc_handle *hp;
	unsigned long paddr;

	handle_size = (sizeof(struct mdesc_handle) -
		       sizeof(struct mdesc_hdr) +
		       mdesc_size);
	alloc_size = PAGE_ALIGN(handle_size);

	paddr = memblock_alloc(alloc_size, PAGE_SIZE);

	hp = NULL;
	if (paddr) {
		hp = __va(paddr);
		mdesc_handle_init(hp, handle_size, hp);
	}
	return hp;
}

static void __init mdesc_memblock_free(struct mdesc_handle *hp)
{
	unsigned int alloc_size;
	unsigned long start;

	BUG_ON(atomic_read(&hp->refcnt) != 0);
	BUG_ON(!list_empty(&hp->list));

	alloc_size = PAGE_ALIGN(hp->handle_size);
	start = __pa(hp);
	free_bootmem_late(start, alloc_size);
}

static struct mdesc_mem_ops memblock_mdesc_ops = {
	.alloc = mdesc_memblock_alloc,
	.free  = mdesc_memblock_free,
};

static struct mdesc_handle *mdesc_kmalloc(unsigned int mdesc_size)
{
	unsigned int handle_size;
	struct mdesc_handle *hp;
	unsigned long addr;
	void *base;

	handle_size = (sizeof(struct mdesc_handle) -
		       sizeof(struct mdesc_hdr) +
		       mdesc_size);

	/*
	 * Allocation has to succeed because mdesc update would be missed
	 * and such events are not retransmitted.
	 */
	base = kmalloc(handle_size + 15, GFP_KERNEL | __GFP_NOFAIL);
	addr = (unsigned long)base;
	addr = (addr + 15UL) & ~15UL;
	hp = (struct mdesc_handle *) addr;

	mdesc_handle_init(hp, handle_size, base);

	return hp;
}

static void mdesc_kfree(struct mdesc_handle *hp)
{
	BUG_ON(atomic_read(&hp->refcnt) != 0);
	BUG_ON(!list_empty(&hp->list));

	kfree(hp->self_base);
}

static struct mdesc_mem_ops kmalloc_mdesc_memops = {
	.alloc = mdesc_kmalloc,
	.free  = mdesc_kfree,
};

static struct mdesc_handle *mdesc_alloc(unsigned int mdesc_size,
					struct mdesc_mem_ops *mops)
{
	struct mdesc_handle *hp = mops->alloc(mdesc_size);

	if (hp)
		hp->mops = mops;

	return hp;
}

static void mdesc_free(struct mdesc_handle *hp)
{
	hp->mops->free(hp);
}

static struct mdesc_handle *cur_mdesc;
static LIST_HEAD(mdesc_zombie_list);
static DEFINE_SPINLOCK(mdesc_lock);

struct mdesc_handle *mdesc_grab(void)
{
	struct mdesc_handle *hp;
	unsigned long flags;

	spin_lock_irqsave(&mdesc_lock, flags);
	hp = cur_mdesc;
	if (hp)
		atomic_inc(&hp->refcnt);
	spin_unlock_irqrestore(&mdesc_lock, flags);

	return hp;
}
EXPORT_SYMBOL(mdesc_grab);

void mdesc_release(struct mdesc_handle *hp)
{
	unsigned long flags;

	spin_lock_irqsave(&mdesc_lock, flags);
	if (atomic_dec_and_test(&hp->refcnt)) {
		list_del_init(&hp->list);
		hp->mops->free(hp);
	}
	spin_unlock_irqrestore(&mdesc_lock, flags);
}
EXPORT_SYMBOL(mdesc_release);

static DEFINE_MUTEX(mdesc_mutex);
static struct mdesc_notifier_client *client_list;

void mdesc_register_notifier(struct mdesc_notifier_client *client)
{
	u64 node;

	mutex_lock(&mdesc_mutex);
	client->next = client_list;
	client_list = client;

	mdesc_for_each_node_by_name(cur_mdesc, node, client->node_name)
		client->add(cur_mdesc, node);

	mutex_unlock(&mdesc_mutex);
}

static const u64 *parent_cfg_handle(struct mdesc_handle *hp, u64 node)
{
	const u64 *id;
	u64 a;

	id = NULL;
	mdesc_for_each_arc(a, hp, node, MDESC_ARC_TYPE_BACK) {
		u64 target;

		target = mdesc_arc_target(hp, a);
		id = mdesc_get_property(hp, target,
					"cfg-handle", NULL);
		if (id)
			break;
	}

	return id;
}

/* Run 'func' on nodes which are in A but not in B.  */
static void invoke_on_missing(const char *name,
			      struct mdesc_handle *a,
			      struct mdesc_handle *b,
			      void (*func)(struct mdesc_handle *, u64))
{
	u64 node;

	mdesc_for_each_node_by_name(a, node, name) {
		int found = 0, is_vdc_port = 0;
		const char *name_prop;
		const u64 *id;
		u64 fnode;

		name_prop = mdesc_get_property(a, node, "name", NULL);
		if (name_prop && !strcmp(name_prop, "vdc-port")) {
			is_vdc_port = 1;
			id = parent_cfg_handle(a, node);
		} else
			id = mdesc_get_property(a, node, "id", NULL);

		if (!id) {
			printk(KERN_ERR "MD: Cannot find ID for %s node.\n",
			       (name_prop ? name_prop : name));
			continue;
		}

		mdesc_for_each_node_by_name(b, fnode, name) {
			const u64 *fid;

			if (is_vdc_port) {
				name_prop = mdesc_get_property(b, fnode,
							       "name", NULL);
				if (!name_prop ||
				    strcmp(name_prop, "vdc-port"))
					continue;
				fid = parent_cfg_handle(b, fnode);
				if (!fid) {
					printk(KERN_ERR "MD: Cannot find ID "
					       "for vdc-port node.\n");
					continue;
				}
			} else
				fid = mdesc_get_property(b, fnode,
							 "id", NULL);

			if (*id == *fid) {
				found = 1;
				break;
			}
		}
		if (!found)
			func(a, node);
	}
}

static void notify_one(struct mdesc_notifier_client *p,
		       struct mdesc_handle *old_hp,
		       struct mdesc_handle *new_hp)
{
	invoke_on_missing(p->node_name, old_hp, new_hp, p->remove);
	invoke_on_missing(p->node_name, new_hp, old_hp, p->add);
}

static void mdesc_notify_clients(struct mdesc_handle *old_hp,
				 struct mdesc_handle *new_hp)
{
	struct mdesc_notifier_client *p = client_list;

	while (p) {
		notify_one(p, old_hp, new_hp);
		p = p->next;
	}
}

void mdesc_update(void)
{
	unsigned long len, real_len, status;
	struct mdesc_handle *hp, *orig_hp;
	unsigned long flags;

	mutex_lock(&mdesc_mutex);

	(void) sun4v_mach_desc(0UL, 0UL, &len);

	hp = mdesc_alloc(len, &kmalloc_mdesc_memops);
	if (!hp) {
		printk(KERN_ERR "MD: mdesc alloc fails\n");
		goto out;
	}

	status = sun4v_mach_desc(__pa(&hp->mdesc), len, &real_len);
	if (status != HV_EOK || real_len > len) {
		printk(KERN_ERR "MD: mdesc reread fails with %lu\n",
		       status);
		atomic_dec(&hp->refcnt);
		mdesc_free(hp);
		goto out;
	}

	spin_lock_irqsave(&mdesc_lock, flags);
	orig_hp = cur_mdesc;
	cur_mdesc = hp;
	spin_unlock_irqrestore(&mdesc_lock, flags);

	mdesc_notify_clients(orig_hp, hp);

	spin_lock_irqsave(&mdesc_lock, flags);
	if (atomic_dec_and_test(&orig_hp->refcnt))
		mdesc_free(orig_hp);
	else
		list_add(&orig_hp->list, &mdesc_zombie_list);
	spin_unlock_irqrestore(&mdesc_lock, flags);

out:
	mutex_unlock(&mdesc_mutex);
}

static struct mdesc_elem *node_block(struct mdesc_hdr *mdesc)
{
	return (struct mdesc_elem *) (mdesc + 1);
}

static void *name_block(struct mdesc_hdr *mdesc)
{
	return ((void *) node_block(mdesc)) + mdesc->node_sz;
}

static void *data_block(struct mdesc_hdr *mdesc)
{
	return ((void *) name_block(mdesc)) + mdesc->name_sz;
}

u64 mdesc_node_by_name(struct mdesc_handle *hp,
		       u64 from_node, const char *name)
{
	struct mdesc_elem *ep = node_block(&hp->mdesc);
	const char *names = name_block(&hp->mdesc);
	u64 last_node = hp->mdesc.node_sz / 16;
	u64 ret;

	if (from_node == MDESC_NODE_NULL) {
		ret = from_node = 0;
	} else if (from_node >= last_node) {
		return MDESC_NODE_NULL;
	} else {
		ret = ep[from_node].d.val;
	}

	while (ret < last_node) {
		if (ep[ret].tag != MD_NODE)
			return MDESC_NODE_NULL;
		if (!strcmp(names + ep[ret].name_offset, name))
			break;
		ret = ep[ret].d.val;
	}
	if (ret >= last_node)
		ret = MDESC_NODE_NULL;
	return ret;
}
EXPORT_SYMBOL(mdesc_node_by_name);

const void *mdesc_get_property(struct mdesc_handle *hp, u64 node,
			       const char *name, int *lenp)
{
	const char *names = name_block(&hp->mdesc);
	u64 last_node = hp->mdesc.node_sz / 16;
	void *data = data_block(&hp->mdesc);
	struct mdesc_elem *ep;

	if (node == MDESC_NODE_NULL || node >= last_node)
		return NULL;

	ep = node_block(&hp->mdesc) + node;
	ep++;
	for (; ep->tag != MD_NODE_END; ep++) {
		void *val = NULL;
		int len = 0;

		switch (ep->tag) {
		case MD_PROP_VAL:
			val = &ep->d.val;
			len = 8;
			break;

		case MD_PROP_STR:
		case MD_PROP_DATA:
			val = data + ep->d.data.data_offset;
			len = ep->d.data.data_len;
			break;

		default:
			break;
		}
		if (!val)
			continue;

		if (!strcmp(names + ep->name_offset, name)) {
			if (lenp)
				*lenp = len;
			return val;
		}
	}

	return NULL;
}
EXPORT_SYMBOL(mdesc_get_property);

u64 mdesc_next_arc(struct mdesc_handle *hp, u64 from, const char *arc_type)
{
	struct mdesc_elem *ep, *base = node_block(&hp->mdesc);
	const char *names = name_block(&hp->mdesc);
	u64 last_node = hp->mdesc.node_sz / 16;

	if (from == MDESC_NODE_NULL || from >= last_node)
		return MDESC_NODE_NULL;

	ep = base + from;

	ep++;
	for (; ep->tag != MD_NODE_END; ep++) {
		if (ep->tag != MD_PROP_ARC)
			continue;

		if (strcmp(names + ep->name_offset, arc_type))
			continue;

		return ep - base;
	}

	return MDESC_NODE_NULL;
}
EXPORT_SYMBOL(mdesc_next_arc);

u64 mdesc_arc_target(struct mdesc_handle *hp, u64 arc)
{
	struct mdesc_elem *ep, *base = node_block(&hp->mdesc);

	ep = base + arc;

	return ep->d.val;
}
EXPORT_SYMBOL(mdesc_arc_target);

const char *mdesc_node_name(struct mdesc_handle *hp, u64 node)
{
	struct mdesc_elem *ep, *base = node_block(&hp->mdesc);
	const char *names = name_block(&hp->mdesc);
	u64 last_node = hp->mdesc.node_sz / 16;

	if (node == MDESC_NODE_NULL || node >= last_node)
		return NULL;

	ep = base + node;
	if (ep->tag != MD_NODE)
		return NULL;

	return names + ep->name_offset;
}
EXPORT_SYMBOL(mdesc_node_name);

static u64 max_cpus = 64;

static void __init report_platform_properties(void)
{
	struct mdesc_handle *hp = mdesc_grab();
	u64 pn = mdesc_node_by_name(hp, MDESC_NODE_NULL, "platform");
	const char *s;
	const u64 *v;

	if (pn == MDESC_NODE_NULL) {
		prom_printf("No platform node in machine-description.\n");
		prom_halt();
	}

	s = mdesc_get_property(hp, pn, "banner-name", NULL);
	printk("PLATFORM: banner-name [%s]\n", s);
	s = mdesc_get_property(hp, pn, "name", NULL);
	printk("PLATFORM: name [%s]\n", s);

	v = mdesc_get_property(hp, pn, "hostid", NULL);
	if (v)
		printk("PLATFORM: hostid [%08llx]\n", *v);
	v = mdesc_get_property(hp, pn, "serial#", NULL);
	if (v)
		printk("PLATFORM: serial# [%08llx]\n", *v);
	v = mdesc_get_property(hp, pn, "stick-frequency", NULL);
	printk("PLATFORM: stick-frequency [%08llx]\n", *v);
	v = mdesc_get_property(hp, pn, "mac-address", NULL);
	if (v)
		printk("PLATFORM: mac-address [%llx]\n", *v);
	v = mdesc_get_property(hp, pn, "watchdog-resolution", NULL);
	if (v)
		printk("PLATFORM: watchdog-resolution [%llu ms]\n", *v);
	v = mdesc_get_property(hp, pn, "watchdog-max-timeout", NULL);
	if (v)
		printk("PLATFORM: watchdog-max-timeout [%llu ms]\n", *v);
	v = mdesc_get_property(hp, pn, "max-cpus", NULL);
	if (v) {
		max_cpus = *v;
		printk("PLATFORM: max-cpus [%llu]\n", max_cpus);
	}

#ifdef CONFIG_SMP
	{
		int max_cpu, i;

		if (v) {
			max_cpu = *v;
			if (max_cpu > NR_CPUS)
				max_cpu = NR_CPUS;
		} else {
			max_cpu = NR_CPUS;
		}
		for (i = 0; i < max_cpu; i++)
			set_cpu_possible(i, true);
	}
#endif

	mdesc_release(hp);
}

static void fill_in_one_cache(cpuinfo_sparc *c, struct mdesc_handle *hp, u64 mp)
{
	const u64 *level = mdesc_get_property(hp, mp, "level", NULL);
	const u64 *size = mdesc_get_property(hp, mp, "size", NULL);
	const u64 *line_size = mdesc_get_property(hp, mp, "line-size", NULL);
	const char *type;
	int type_len;

	type = mdesc_get_property(hp, mp, "type", &type_len);

	switch (*level) {
	case 1:
		if (of_find_in_proplist(type, "instn", type_len)) {
			c->icache_size = *size;
			c->icache_line_size = *line_size;
		} else if (of_find_in_proplist(type, "data", type_len)) {
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
		u64 a;

		mdesc_for_each_arc(a, hp, mp, MDESC_ARC_TYPE_FWD) {
			u64 target = mdesc_arc_target(hp, a);
			const char *name = mdesc_node_name(hp, target);

			if (!strcmp(name, "cache"))
				fill_in_one_cache(c, hp, target);
		}
	}
}

static void find_back_node_value(struct mdesc_handle *hp, u64 node,
				 char *srch_val,
				 void (*func)(struct mdesc_handle *, u64, int),
				 u64 val, int depth)
{
	u64 arc;

	/* Since we have an estimate of recursion depth, do a sanity check. */
	if (depth == 0)
		return;

	mdesc_for_each_arc(arc, hp, node, MDESC_ARC_TYPE_BACK) {
		u64 n = mdesc_arc_target(hp, arc);
		const char *name = mdesc_node_name(hp, n);

		if (!strcmp(srch_val, name))
			(*func)(hp, n, val);

		find_back_node_value(hp, n, srch_val, func, val, depth-1);
	}
}

static void __mark_core_id(struct mdesc_handle *hp, u64 node,
			   int core_id)
{
	const u64 *id = mdesc_get_property(hp, node, "id", NULL);

	if (*id < num_possible_cpus())
		cpu_data(*id).core_id = core_id;
}

static void __mark_max_cache_id(struct mdesc_handle *hp, u64 node,
				int max_cache_id)
{
	const u64 *id = mdesc_get_property(hp, node, "id", NULL);

	if (*id < num_possible_cpus()) {
		cpu_data(*id).max_cache_id = max_cache_id;

		/**
		 * On systems without explicit socket descriptions socket
		 * is max_cache_id
		 */
		cpu_data(*id).sock_id = max_cache_id;
	}
}

static void mark_core_ids(struct mdesc_handle *hp, u64 mp,
			  int core_id)
{
	find_back_node_value(hp, mp, "cpu", __mark_core_id, core_id, 10);
}

static void mark_max_cache_ids(struct mdesc_handle *hp, u64 mp,
			       int max_cache_id)
{
	find_back_node_value(hp, mp, "cpu", __mark_max_cache_id,
			     max_cache_id, 10);
}

static void set_core_ids(struct mdesc_handle *hp)
{
	int idx;
	u64 mp;

	idx = 1;

	/* Identify unique cores by looking for cpus backpointed to by
	 * level 1 instruction caches.
	 */
	mdesc_for_each_node_by_name(hp, mp, "cache") {
		const u64 *level;
		const char *type;
		int len;

		level = mdesc_get_property(hp, mp, "level", NULL);
		if (*level != 1)
			continue;

		type = mdesc_get_property(hp, mp, "type", &len);
		if (!of_find_in_proplist(type, "instn", len))
			continue;

		mark_core_ids(hp, mp, idx);
		idx++;
	}
}

static int set_max_cache_ids_by_cache(struct mdesc_handle *hp, int level)
{
	u64 mp;
	int idx = 1;
	int fnd = 0;

	/**
	 * Identify unique highest level of shared cache by looking for cpus
	 * backpointed to by shared level N caches.
	 */
	mdesc_for_each_node_by_name(hp, mp, "cache") {
		const u64 *cur_lvl;

		cur_lvl = mdesc_get_property(hp, mp, "level", NULL);
		if (*cur_lvl != level)
			continue;
		mark_max_cache_ids(hp, mp, idx);
		idx++;
		fnd = 1;
	}
	return fnd;
}

static void set_sock_ids_by_socket(struct mdesc_handle *hp, u64 mp)
{
	int idx = 1;

	mdesc_for_each_node_by_name(hp, mp, "socket") {
		u64 a;

		mdesc_for_each_arc(a, hp, mp, MDESC_ARC_TYPE_FWD) {
			u64 t = mdesc_arc_target(hp, a);
			const char *name;
			const u64 *id;

			name = mdesc_node_name(hp, t);
			if (strcmp(name, "cpu"))
				continue;

			id = mdesc_get_property(hp, t, "id", NULL);
			if (*id < num_possible_cpus())
				cpu_data(*id).sock_id = idx;
		}
		idx++;
	}
}

static void set_sock_ids(struct mdesc_handle *hp)
{
	u64 mp;

	/**
	 * Find the highest level of shared cache which pre-T7 is also
	 * the socket.
	 */
	if (!set_max_cache_ids_by_cache(hp, 3))
		set_max_cache_ids_by_cache(hp, 2);

	/* If machine description exposes sockets data use it.*/
	mp = mdesc_node_by_name(hp, MDESC_NODE_NULL, "sockets");
	if (mp != MDESC_NODE_NULL)
		set_sock_ids_by_socket(hp, mp);
}

static void mark_proc_ids(struct mdesc_handle *hp, u64 mp, int proc_id)
{
	u64 a;

	mdesc_for_each_arc(a, hp, mp, MDESC_ARC_TYPE_BACK) {
		u64 t = mdesc_arc_target(hp, a);
		const char *name;
		const u64 *id;

		name = mdesc_node_name(hp, t);
		if (strcmp(name, "cpu"))
			continue;

		id = mdesc_get_property(hp, t, "id", NULL);
		if (*id < NR_CPUS)
			cpu_data(*id).proc_id = proc_id;
	}
}

static void __set_proc_ids(struct mdesc_handle *hp, const char *exec_unit_name)
{
	int idx;
	u64 mp;

	idx = 0;
	mdesc_for_each_node_by_name(hp, mp, exec_unit_name) {
		const char *type;
		int len;

		type = mdesc_get_property(hp, mp, "type", &len);
		if (!of_find_in_proplist(type, "int", len) &&
		    !of_find_in_proplist(type, "integer", len))
			continue;

		mark_proc_ids(hp, mp, idx);
		idx++;
	}
}

static void set_proc_ids(struct mdesc_handle *hp)
{
	__set_proc_ids(hp, "exec_unit");
	__set_proc_ids(hp, "exec-unit");
}

static void get_one_mondo_bits(const u64 *p, unsigned int *mask,
			       unsigned long def, unsigned long max)
{
	u64 val;

	if (!p)
		goto use_default;
	val = *p;

	if (!val || val >= 64)
		goto use_default;

	if (val > max)
		val = max;

	*mask = ((1U << val) * 64U) - 1U;
	return;

use_default:
	*mask = ((1U << def) * 64U) - 1U;
}

static void get_mondo_data(struct mdesc_handle *hp, u64 mp,
			   struct trap_per_cpu *tb)
{
	static int printed;
	const u64 *val;

	val = mdesc_get_property(hp, mp, "q-cpu-mondo-#bits", NULL);
	get_one_mondo_bits(val, &tb->cpu_mondo_qmask, 7, ilog2(max_cpus * 2));

	val = mdesc_get_property(hp, mp, "q-dev-mondo-#bits", NULL);
	get_one_mondo_bits(val, &tb->dev_mondo_qmask, 7, 8);

	val = mdesc_get_property(hp, mp, "q-resumable-#bits", NULL);
	get_one_mondo_bits(val, &tb->resum_qmask, 6, 7);

	val = mdesc_get_property(hp, mp, "q-nonresumable-#bits", NULL);
	get_one_mondo_bits(val, &tb->nonresum_qmask, 2, 2);
	if (!printed++) {
		pr_info("SUN4V: Mondo queue sizes "
			"[cpu(%u) dev(%u) r(%u) nr(%u)]\n",
			tb->cpu_mondo_qmask + 1,
			tb->dev_mondo_qmask + 1,
			tb->resum_qmask + 1,
			tb->nonresum_qmask + 1);
	}
}

static void *mdesc_iterate_over_cpus(void *(*func)(struct mdesc_handle *, u64, int, void *), void *arg, cpumask_t *mask)
{
	struct mdesc_handle *hp = mdesc_grab();
	void *ret = NULL;
	u64 mp;

	mdesc_for_each_node_by_name(hp, mp, "cpu") {
		const u64 *id = mdesc_get_property(hp, mp, "id", NULL);
		int cpuid = *id;

#ifdef CONFIG_SMP
		if (cpuid >= NR_CPUS) {
			printk(KERN_WARNING "Ignoring CPU %d which is "
			       ">= NR_CPUS (%d)\n",
			       cpuid, NR_CPUS);
			continue;
		}
		if (!cpumask_test_cpu(cpuid, mask))
			continue;
#endif

		ret = func(hp, mp, cpuid, arg);
		if (ret)
			goto out;
	}
out:
	mdesc_release(hp);
	return ret;
}

static void *record_one_cpu(struct mdesc_handle *hp, u64 mp, int cpuid,
			    void *arg)
{
	ncpus_probed++;
#ifdef CONFIG_SMP
	set_cpu_present(cpuid, true);
#endif
	return NULL;
}

void mdesc_populate_present_mask(cpumask_t *mask)
{
	if (tlb_type != hypervisor)
		return;

	ncpus_probed = 0;
	mdesc_iterate_over_cpus(record_one_cpu, NULL, mask);
}

static void * __init check_one_pgsz(struct mdesc_handle *hp, u64 mp, int cpuid, void *arg)
{
	const u64 *pgsz_prop = mdesc_get_property(hp, mp, "mmu-page-size-list", NULL);
	unsigned long *pgsz_mask = arg;
	u64 val;

	val = (HV_PGSZ_MASK_8K | HV_PGSZ_MASK_64K |
	       HV_PGSZ_MASK_512K | HV_PGSZ_MASK_4MB);
	if (pgsz_prop)
		val = *pgsz_prop;

	if (!*pgsz_mask)
		*pgsz_mask = val;
	else
		*pgsz_mask &= val;
	return NULL;
}

void __init mdesc_get_page_sizes(cpumask_t *mask, unsigned long *pgsz_mask)
{
	*pgsz_mask = 0;
	mdesc_iterate_over_cpus(check_one_pgsz, pgsz_mask, mask);
}

static void *fill_in_one_cpu(struct mdesc_handle *hp, u64 mp, int cpuid,
			     void *arg)
{
	const u64 *cfreq = mdesc_get_property(hp, mp, "clock-frequency", NULL);
	struct trap_per_cpu *tb;
	cpuinfo_sparc *c;
	u64 a;

#ifndef CONFIG_SMP
	/* On uniprocessor we only want the values for the
	 * real physical cpu the kernel booted onto, however
	 * cpu_data() only has one entry at index 0.
	 */
	if (cpuid != real_hard_smp_processor_id())
		return NULL;
	cpuid = 0;
#endif

	c = &cpu_data(cpuid);
	c->clock_tick = *cfreq;

	tb = &trap_block[cpuid];
	get_mondo_data(hp, mp, tb);

	mdesc_for_each_arc(a, hp, mp, MDESC_ARC_TYPE_FWD) {
		u64 j, t = mdesc_arc_target(hp, a);
		const char *t_name;

		t_name = mdesc_node_name(hp, t);
		if (!strcmp(t_name, "cache")) {
			fill_in_one_cache(c, hp, t);
			continue;
		}

		mdesc_for_each_arc(j, hp, t, MDESC_ARC_TYPE_FWD) {
			u64 n = mdesc_arc_target(hp, j);
			const char *n_name;

			n_name = mdesc_node_name(hp, n);
			if (!strcmp(n_name, "cache"))
				fill_in_one_cache(c, hp, n);
		}
	}

	c->core_id = 0;
	c->proc_id = -1;

	return NULL;
}

void mdesc_fill_in_cpu_data(cpumask_t *mask)
{
	struct mdesc_handle *hp;

	mdesc_iterate_over_cpus(fill_in_one_cpu, NULL, mask);

	hp = mdesc_grab();

	set_core_ids(hp);
	set_proc_ids(hp);
	set_sock_ids(hp);

	mdesc_release(hp);

	smp_fill_in_sib_core_maps();
}

/* mdesc_open() - Grab a reference to mdesc_handle when /dev/mdesc is
 * opened. Hold this reference until /dev/mdesc is closed to ensure
 * mdesc data structure is not released underneath us. Store the
 * pointer to mdesc structure in private_data for read and seek to use
 */
static int mdesc_open(struct inode *inode, struct file *file)
{
	struct mdesc_handle *hp = mdesc_grab();

	if (!hp)
		return -ENODEV;

	file->private_data = hp;

	return 0;
}

static ssize_t mdesc_read(struct file *file, char __user *buf,
			  size_t len, loff_t *offp)
{
	struct mdesc_handle *hp = file->private_data;
	unsigned char *mdesc;
	int bytes_left, count = len;

	if (*offp >= hp->handle_size)
		return 0;

	bytes_left = hp->handle_size - *offp;
	if (count > bytes_left)
		count = bytes_left;

	mdesc = (unsigned char *)&hp->mdesc;
	mdesc += *offp;
	if (!copy_to_user(buf, mdesc, count)) {
		*offp += count;
		return count;
	} else {
		return -EFAULT;
	}
}

static loff_t mdesc_llseek(struct file *file, loff_t offset, int whence)
{
	struct mdesc_handle *hp = file->private_data;

	return no_seek_end_llseek_size(file, offset, whence, hp->handle_size);
}

/* mdesc_close() - /dev/mdesc is being closed, release the reference to
 * mdesc structure.
 */
static int mdesc_close(struct inode *inode, struct file *file)
{
	mdesc_release(file->private_data);
	return 0;
}

static const struct file_operations mdesc_fops = {
	.open    = mdesc_open,
	.read	 = mdesc_read,
	.llseek  = mdesc_llseek,
	.release = mdesc_close,
	.owner	 = THIS_MODULE,
};

static struct miscdevice mdesc_misc = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= "mdesc",
	.fops	= &mdesc_fops,
};

static int __init mdesc_misc_init(void)
{
	return misc_register(&mdesc_misc);
}

__initcall(mdesc_misc_init);

void __init sun4v_mdesc_init(void)
{
	struct mdesc_handle *hp;
	unsigned long len, real_len, status;

	(void) sun4v_mach_desc(0UL, 0UL, &len);

	printk("MDESC: Size is %lu bytes.\n", len);

	hp = mdesc_alloc(len, &memblock_mdesc_ops);
	if (hp == NULL) {
		prom_printf("MDESC: alloc of %lu bytes failed.\n", len);
		prom_halt();
	}

	status = sun4v_mach_desc(__pa(&hp->mdesc), len, &real_len);
	if (status != HV_EOK || real_len > len) {
		prom_printf("sun4v_mach_desc fails, err(%lu), "
			    "len(%lu), real_len(%lu)\n",
			    status, len, real_len);
		mdesc_free(hp);
		prom_halt();
	}

	cur_mdesc = hp;

	report_platform_properties();
}

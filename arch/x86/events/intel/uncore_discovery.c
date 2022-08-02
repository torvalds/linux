/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Support Intel uncore PerfMon discovery mechanism.
 * Copyright(c) 2021 Intel Corporation.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "uncore.h"
#include "uncore_discovery.h"

static struct rb_root discovery_tables = RB_ROOT;
static int num_discovered_types[UNCORE_ACCESS_MAX];

static bool has_generic_discovery_table(void)
{
	struct pci_dev *dev;
	int dvsec;

	dev = pci_get_device(PCI_VENDOR_ID_INTEL, UNCORE_DISCOVERY_TABLE_DEVICE, NULL);
	if (!dev)
		return false;

	/* A discovery table device has the unique capability ID. */
	dvsec = pci_find_next_ext_capability(dev, 0, UNCORE_EXT_CAP_ID_DISCOVERY);
	pci_dev_put(dev);
	if (dvsec)
		return true;

	return false;
}

static int logical_die_id;

static int get_device_die_id(struct pci_dev *dev)
{
	int cpu, node = pcibus_to_node(dev->bus);

	/*
	 * If the NUMA info is not available, assume that the logical die id is
	 * continuous in the order in which the discovery table devices are
	 * detected.
	 */
	if (node < 0)
		return logical_die_id++;

	for_each_cpu(cpu, cpumask_of_node(node)) {
		struct cpuinfo_x86 *c = &cpu_data(cpu);

		if (c->initialized && cpu_to_node(cpu) == node)
			return c->logical_die_id;
	}

	/*
	 * All CPUs of a node may be offlined. For this case,
	 * the PCI and MMIO type of uncore blocks which are
	 * enumerated by the device will be unavailable.
	 */
	return -1;
}

#define __node_2_type(cur)	\
	rb_entry((cur), struct intel_uncore_discovery_type, node)

static inline int __type_cmp(const void *key, const struct rb_node *b)
{
	struct intel_uncore_discovery_type *type_b = __node_2_type(b);
	const u16 *type_id = key;

	if (type_b->type > *type_id)
		return -1;
	else if (type_b->type < *type_id)
		return 1;

	return 0;
}

static inline struct intel_uncore_discovery_type *
search_uncore_discovery_type(u16 type_id)
{
	struct rb_node *node = rb_find(&type_id, &discovery_tables, __type_cmp);

	return (node) ? __node_2_type(node) : NULL;
}

static inline bool __type_less(struct rb_node *a, const struct rb_node *b)
{
	return (__node_2_type(a)->type < __node_2_type(b)->type);
}

static struct intel_uncore_discovery_type *
add_uncore_discovery_type(struct uncore_unit_discovery *unit)
{
	struct intel_uncore_discovery_type *type;

	if (unit->access_type >= UNCORE_ACCESS_MAX) {
		pr_warn("Unsupported access type %d\n", unit->access_type);
		return NULL;
	}

	type = kzalloc(sizeof(struct intel_uncore_discovery_type), GFP_KERNEL);
	if (!type)
		return NULL;

	type->box_ctrl_die = kcalloc(__uncore_max_dies, sizeof(u64), GFP_KERNEL);
	if (!type->box_ctrl_die)
		goto free_type;

	type->access_type = unit->access_type;
	num_discovered_types[type->access_type]++;
	type->type = unit->box_type;

	rb_add(&type->node, &discovery_tables, __type_less);

	return type;

free_type:
	kfree(type);

	return NULL;

}

static struct intel_uncore_discovery_type *
get_uncore_discovery_type(struct uncore_unit_discovery *unit)
{
	struct intel_uncore_discovery_type *type;

	type = search_uncore_discovery_type(unit->box_type);
	if (type)
		return type;

	return add_uncore_discovery_type(unit);
}

static void
uncore_insert_box_info(struct uncore_unit_discovery *unit,
		       int die, bool parsed)
{
	struct intel_uncore_discovery_type *type;
	unsigned int *box_offset, *ids;
	int i;

	if (WARN_ON_ONCE(!unit->ctl || !unit->ctl_offset || !unit->ctr_offset))
		return;

	if (parsed) {
		type = search_uncore_discovery_type(unit->box_type);
		if (WARN_ON_ONCE(!type))
			return;
		/* Store the first box of each die */
		if (!type->box_ctrl_die[die])
			type->box_ctrl_die[die] = unit->ctl;
		return;
	}

	type = get_uncore_discovery_type(unit);
	if (!type)
		return;

	box_offset = kcalloc(type->num_boxes + 1, sizeof(unsigned int), GFP_KERNEL);
	if (!box_offset)
		return;

	ids = kcalloc(type->num_boxes + 1, sizeof(unsigned int), GFP_KERNEL);
	if (!ids)
		goto free_box_offset;

	/* Store generic information for the first box */
	if (!type->num_boxes) {
		type->box_ctrl = unit->ctl;
		type->box_ctrl_die[die] = unit->ctl;
		type->num_counters = unit->num_regs;
		type->counter_width = unit->bit_width;
		type->ctl_offset = unit->ctl_offset;
		type->ctr_offset = unit->ctr_offset;
		*ids = unit->box_id;
		goto end;
	}

	for (i = 0; i < type->num_boxes; i++) {
		ids[i] = type->ids[i];
		box_offset[i] = type->box_offset[i];

		if (WARN_ON_ONCE(unit->box_id == ids[i]))
			goto free_ids;
	}
	ids[i] = unit->box_id;
	box_offset[i] = unit->ctl - type->box_ctrl;
	kfree(type->ids);
	kfree(type->box_offset);
end:
	type->ids = ids;
	type->box_offset = box_offset;
	type->num_boxes++;
	return;

free_ids:
	kfree(ids);

free_box_offset:
	kfree(box_offset);

}

static int parse_discovery_table(struct pci_dev *dev, int die,
				 u32 bar_offset, bool *parsed)
{
	struct uncore_global_discovery global;
	struct uncore_unit_discovery unit;
	void __iomem *io_addr;
	resource_size_t addr;
	unsigned long size;
	u32 val;
	int i;

	pci_read_config_dword(dev, bar_offset, &val);

	if (val & ~PCI_BASE_ADDRESS_MEM_MASK & ~PCI_BASE_ADDRESS_MEM_TYPE_64)
		return -EINVAL;

	addr = (resource_size_t)(val & PCI_BASE_ADDRESS_MEM_MASK);
#ifdef CONFIG_PHYS_ADDR_T_64BIT
	if ((val & PCI_BASE_ADDRESS_MEM_TYPE_MASK) == PCI_BASE_ADDRESS_MEM_TYPE_64) {
		u32 val2;

		pci_read_config_dword(dev, bar_offset + 4, &val2);
		addr |= ((resource_size_t)val2) << 32;
	}
#endif
	size = UNCORE_DISCOVERY_GLOBAL_MAP_SIZE;
	io_addr = ioremap(addr, size);
	if (!io_addr)
		return -ENOMEM;

	/* Read Global Discovery State */
	memcpy_fromio(&global, io_addr, sizeof(struct uncore_global_discovery));
	if (uncore_discovery_invalid_unit(global)) {
		pr_info("Invalid Global Discovery State: 0x%llx 0x%llx 0x%llx\n",
			global.table1, global.ctl, global.table3);
		iounmap(io_addr);
		return -EINVAL;
	}
	iounmap(io_addr);

	size = (1 + global.max_units) * global.stride * 8;
	io_addr = ioremap(addr, size);
	if (!io_addr)
		return -ENOMEM;

	/* Parsing Unit Discovery State */
	for (i = 0; i < global.max_units; i++) {
		memcpy_fromio(&unit, io_addr + (i + 1) * (global.stride * 8),
			      sizeof(struct uncore_unit_discovery));

		if (uncore_discovery_invalid_unit(unit))
			continue;

		if (unit.access_type >= UNCORE_ACCESS_MAX)
			continue;

		uncore_insert_box_info(&unit, die, *parsed);
	}

	*parsed = true;
	iounmap(io_addr);
	return 0;
}

bool intel_uncore_has_discovery_tables(void)
{
	u32 device, val, entry_id, bar_offset;
	int die, dvsec = 0, ret = true;
	struct pci_dev *dev = NULL;
	bool parsed = false;

	if (has_generic_discovery_table())
		device = UNCORE_DISCOVERY_TABLE_DEVICE;
	else
		device = PCI_ANY_ID;

	/*
	 * Start a new search and iterates through the list of
	 * the discovery table devices.
	 */
	while ((dev = pci_get_device(PCI_VENDOR_ID_INTEL, device, dev)) != NULL) {
		while ((dvsec = pci_find_next_ext_capability(dev, dvsec, UNCORE_EXT_CAP_ID_DISCOVERY))) {
			pci_read_config_dword(dev, dvsec + UNCORE_DISCOVERY_DVSEC_OFFSET, &val);
			entry_id = val & UNCORE_DISCOVERY_DVSEC_ID_MASK;
			if (entry_id != UNCORE_DISCOVERY_DVSEC_ID_PMON)
				continue;

			pci_read_config_dword(dev, dvsec + UNCORE_DISCOVERY_DVSEC2_OFFSET, &val);

			if (val & ~UNCORE_DISCOVERY_DVSEC2_BIR_MASK) {
				ret = false;
				goto err;
			}
			bar_offset = UNCORE_DISCOVERY_BIR_BASE +
				     (val & UNCORE_DISCOVERY_DVSEC2_BIR_MASK) * UNCORE_DISCOVERY_BIR_STEP;

			die = get_device_die_id(dev);
			if (die < 0)
				continue;

			parse_discovery_table(dev, die, bar_offset, &parsed);
		}
	}

	/* None of the discovery tables are available */
	if (!parsed)
		ret = false;
err:
	pci_dev_put(dev);

	return ret;
}

void intel_uncore_clear_discovery_tables(void)
{
	struct intel_uncore_discovery_type *type, *next;

	rbtree_postorder_for_each_entry_safe(type, next, &discovery_tables, node) {
		kfree(type->box_ctrl_die);
		kfree(type);
	}
}

DEFINE_UNCORE_FORMAT_ATTR(event, event, "config:0-7");
DEFINE_UNCORE_FORMAT_ATTR(umask, umask, "config:8-15");
DEFINE_UNCORE_FORMAT_ATTR(edge, edge, "config:18");
DEFINE_UNCORE_FORMAT_ATTR(inv, inv, "config:23");
DEFINE_UNCORE_FORMAT_ATTR(thresh, thresh, "config:24-31");

static struct attribute *generic_uncore_formats_attr[] = {
	&format_attr_event.attr,
	&format_attr_umask.attr,
	&format_attr_edge.attr,
	&format_attr_inv.attr,
	&format_attr_thresh.attr,
	NULL,
};

static const struct attribute_group generic_uncore_format_group = {
	.name = "format",
	.attrs = generic_uncore_formats_attr,
};

void intel_generic_uncore_msr_init_box(struct intel_uncore_box *box)
{
	wrmsrl(uncore_msr_box_ctl(box), GENERIC_PMON_BOX_CTL_INT);
}

void intel_generic_uncore_msr_disable_box(struct intel_uncore_box *box)
{
	wrmsrl(uncore_msr_box_ctl(box), GENERIC_PMON_BOX_CTL_FRZ);
}

void intel_generic_uncore_msr_enable_box(struct intel_uncore_box *box)
{
	wrmsrl(uncore_msr_box_ctl(box), 0);
}

static void intel_generic_uncore_msr_enable_event(struct intel_uncore_box *box,
					    struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;

	wrmsrl(hwc->config_base, hwc->config);
}

static void intel_generic_uncore_msr_disable_event(struct intel_uncore_box *box,
					     struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;

	wrmsrl(hwc->config_base, 0);
}

static struct intel_uncore_ops generic_uncore_msr_ops = {
	.init_box		= intel_generic_uncore_msr_init_box,
	.disable_box		= intel_generic_uncore_msr_disable_box,
	.enable_box		= intel_generic_uncore_msr_enable_box,
	.disable_event		= intel_generic_uncore_msr_disable_event,
	.enable_event		= intel_generic_uncore_msr_enable_event,
	.read_counter		= uncore_msr_read_counter,
};

void intel_generic_uncore_pci_init_box(struct intel_uncore_box *box)
{
	struct pci_dev *pdev = box->pci_dev;
	int box_ctl = uncore_pci_box_ctl(box);

	__set_bit(UNCORE_BOX_FLAG_CTL_OFFS8, &box->flags);
	pci_write_config_dword(pdev, box_ctl, GENERIC_PMON_BOX_CTL_INT);
}

void intel_generic_uncore_pci_disable_box(struct intel_uncore_box *box)
{
	struct pci_dev *pdev = box->pci_dev;
	int box_ctl = uncore_pci_box_ctl(box);

	pci_write_config_dword(pdev, box_ctl, GENERIC_PMON_BOX_CTL_FRZ);
}

void intel_generic_uncore_pci_enable_box(struct intel_uncore_box *box)
{
	struct pci_dev *pdev = box->pci_dev;
	int box_ctl = uncore_pci_box_ctl(box);

	pci_write_config_dword(pdev, box_ctl, 0);
}

static void intel_generic_uncore_pci_enable_event(struct intel_uncore_box *box,
					    struct perf_event *event)
{
	struct pci_dev *pdev = box->pci_dev;
	struct hw_perf_event *hwc = &event->hw;

	pci_write_config_dword(pdev, hwc->config_base, hwc->config);
}

void intel_generic_uncore_pci_disable_event(struct intel_uncore_box *box,
					    struct perf_event *event)
{
	struct pci_dev *pdev = box->pci_dev;
	struct hw_perf_event *hwc = &event->hw;

	pci_write_config_dword(pdev, hwc->config_base, 0);
}

u64 intel_generic_uncore_pci_read_counter(struct intel_uncore_box *box,
					  struct perf_event *event)
{
	struct pci_dev *pdev = box->pci_dev;
	struct hw_perf_event *hwc = &event->hw;
	u64 count = 0;

	pci_read_config_dword(pdev, hwc->event_base, (u32 *)&count);
	pci_read_config_dword(pdev, hwc->event_base + 4, (u32 *)&count + 1);

	return count;
}

static struct intel_uncore_ops generic_uncore_pci_ops = {
	.init_box	= intel_generic_uncore_pci_init_box,
	.disable_box	= intel_generic_uncore_pci_disable_box,
	.enable_box	= intel_generic_uncore_pci_enable_box,
	.disable_event	= intel_generic_uncore_pci_disable_event,
	.enable_event	= intel_generic_uncore_pci_enable_event,
	.read_counter	= intel_generic_uncore_pci_read_counter,
};

#define UNCORE_GENERIC_MMIO_SIZE		0x4000

static u64 generic_uncore_mmio_box_ctl(struct intel_uncore_box *box)
{
	struct intel_uncore_type *type = box->pmu->type;

	if (!type->box_ctls || !type->box_ctls[box->dieid] || !type->mmio_offsets)
		return 0;

	return type->box_ctls[box->dieid] + type->mmio_offsets[box->pmu->pmu_idx];
}

void intel_generic_uncore_mmio_init_box(struct intel_uncore_box *box)
{
	u64 box_ctl = generic_uncore_mmio_box_ctl(box);
	struct intel_uncore_type *type = box->pmu->type;
	resource_size_t addr;

	if (!box_ctl) {
		pr_warn("Uncore type %d box %d: Invalid box control address.\n",
			type->type_id, type->box_ids[box->pmu->pmu_idx]);
		return;
	}

	addr = box_ctl;
	box->io_addr = ioremap(addr, UNCORE_GENERIC_MMIO_SIZE);
	if (!box->io_addr) {
		pr_warn("Uncore type %d box %d: ioremap error for 0x%llx.\n",
			type->type_id, type->box_ids[box->pmu->pmu_idx],
			(unsigned long long)addr);
		return;
	}

	writel(GENERIC_PMON_BOX_CTL_INT, box->io_addr);
}

void intel_generic_uncore_mmio_disable_box(struct intel_uncore_box *box)
{
	if (!box->io_addr)
		return;

	writel(GENERIC_PMON_BOX_CTL_FRZ, box->io_addr);
}

void intel_generic_uncore_mmio_enable_box(struct intel_uncore_box *box)
{
	if (!box->io_addr)
		return;

	writel(0, box->io_addr);
}

void intel_generic_uncore_mmio_enable_event(struct intel_uncore_box *box,
					    struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;

	if (!box->io_addr)
		return;

	writel(hwc->config, box->io_addr + hwc->config_base);
}

void intel_generic_uncore_mmio_disable_event(struct intel_uncore_box *box,
					     struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;

	if (!box->io_addr)
		return;

	writel(0, box->io_addr + hwc->config_base);
}

static struct intel_uncore_ops generic_uncore_mmio_ops = {
	.init_box	= intel_generic_uncore_mmio_init_box,
	.exit_box	= uncore_mmio_exit_box,
	.disable_box	= intel_generic_uncore_mmio_disable_box,
	.enable_box	= intel_generic_uncore_mmio_enable_box,
	.disable_event	= intel_generic_uncore_mmio_disable_event,
	.enable_event	= intel_generic_uncore_mmio_enable_event,
	.read_counter	= uncore_mmio_read_counter,
};

static bool uncore_update_uncore_type(enum uncore_access_type type_id,
				      struct intel_uncore_type *uncore,
				      struct intel_uncore_discovery_type *type)
{
	uncore->type_id = type->type;
	uncore->num_boxes = type->num_boxes;
	uncore->num_counters = type->num_counters;
	uncore->perf_ctr_bits = type->counter_width;
	uncore->box_ids = type->ids;

	switch (type_id) {
	case UNCORE_ACCESS_MSR:
		uncore->ops = &generic_uncore_msr_ops;
		uncore->perf_ctr = (unsigned int)type->box_ctrl + type->ctr_offset;
		uncore->event_ctl = (unsigned int)type->box_ctrl + type->ctl_offset;
		uncore->box_ctl = (unsigned int)type->box_ctrl;
		uncore->msr_offsets = type->box_offset;
		break;
	case UNCORE_ACCESS_PCI:
		uncore->ops = &generic_uncore_pci_ops;
		uncore->perf_ctr = (unsigned int)UNCORE_DISCOVERY_PCI_BOX_CTRL(type->box_ctrl) + type->ctr_offset;
		uncore->event_ctl = (unsigned int)UNCORE_DISCOVERY_PCI_BOX_CTRL(type->box_ctrl) + type->ctl_offset;
		uncore->box_ctl = (unsigned int)UNCORE_DISCOVERY_PCI_BOX_CTRL(type->box_ctrl);
		uncore->box_ctls = type->box_ctrl_die;
		uncore->pci_offsets = type->box_offset;
		break;
	case UNCORE_ACCESS_MMIO:
		uncore->ops = &generic_uncore_mmio_ops;
		uncore->perf_ctr = (unsigned int)type->ctr_offset;
		uncore->event_ctl = (unsigned int)type->ctl_offset;
		uncore->box_ctl = (unsigned int)type->box_ctrl;
		uncore->box_ctls = type->box_ctrl_die;
		uncore->mmio_offsets = type->box_offset;
		uncore->mmio_map_size = UNCORE_GENERIC_MMIO_SIZE;
		break;
	default:
		return false;
	}

	return true;
}

struct intel_uncore_type **
intel_uncore_generic_init_uncores(enum uncore_access_type type_id, int num_extra)
{
	struct intel_uncore_discovery_type *type;
	struct intel_uncore_type **uncores;
	struct intel_uncore_type *uncore;
	struct rb_node *node;
	int i = 0;

	uncores = kcalloc(num_discovered_types[type_id] + num_extra + 1,
			  sizeof(struct intel_uncore_type *), GFP_KERNEL);
	if (!uncores)
		return empty_uncore;

	for (node = rb_first(&discovery_tables); node; node = rb_next(node)) {
		type = rb_entry(node, struct intel_uncore_discovery_type, node);
		if (type->access_type != type_id)
			continue;

		uncore = kzalloc(sizeof(struct intel_uncore_type), GFP_KERNEL);
		if (!uncore)
			break;

		uncore->event_mask = GENERIC_PMON_RAW_EVENT_MASK;
		uncore->format_group = &generic_uncore_format_group;

		if (!uncore_update_uncore_type(type_id, uncore, type)) {
			kfree(uncore);
			continue;
		}
		uncores[i++] = uncore;
	}

	return uncores;
}

void intel_uncore_generic_uncore_cpu_init(void)
{
	uncore_msr_uncores = intel_uncore_generic_init_uncores(UNCORE_ACCESS_MSR, 0);
}

int intel_uncore_generic_uncore_pci_init(void)
{
	uncore_pci_uncores = intel_uncore_generic_init_uncores(UNCORE_ACCESS_PCI, 0);

	return 0;
}

void intel_uncore_generic_uncore_mmio_init(void)
{
	uncore_mmio_uncores = intel_uncore_generic_init_uncores(UNCORE_ACCESS_MMIO, 0);
}

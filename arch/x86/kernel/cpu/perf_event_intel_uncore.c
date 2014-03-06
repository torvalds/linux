#include "perf_event_intel_uncore.h"

static struct intel_uncore_type *empty_uncore[] = { NULL, };
static struct intel_uncore_type **msr_uncores = empty_uncore;
static struct intel_uncore_type **pci_uncores = empty_uncore;
/* pci bus to socket mapping */
static int pcibus_to_physid[256] = { [0 ... 255] = -1, };

static struct pci_dev *extra_pci_dev[UNCORE_SOCKET_MAX][UNCORE_EXTRA_PCI_DEV_MAX];

static DEFINE_RAW_SPINLOCK(uncore_box_lock);

/* mask of cpus that collect uncore events */
static cpumask_t uncore_cpu_mask;

/* constraint for the fixed counter */
static struct event_constraint constraint_fixed =
	EVENT_CONSTRAINT(~0ULL, 1 << UNCORE_PMC_IDX_FIXED, ~0ULL);
static struct event_constraint constraint_empty =
	EVENT_CONSTRAINT(0, 0, 0);

#define __BITS_VALUE(x, i, n)  ((typeof(x))(((x) >> ((i) * (n))) & \
				((1ULL << (n)) - 1)))

DEFINE_UNCORE_FORMAT_ATTR(event, event, "config:0-7");
DEFINE_UNCORE_FORMAT_ATTR(event_ext, event, "config:0-7,21");
DEFINE_UNCORE_FORMAT_ATTR(umask, umask, "config:8-15");
DEFINE_UNCORE_FORMAT_ATTR(edge, edge, "config:18");
DEFINE_UNCORE_FORMAT_ATTR(tid_en, tid_en, "config:19");
DEFINE_UNCORE_FORMAT_ATTR(inv, inv, "config:23");
DEFINE_UNCORE_FORMAT_ATTR(cmask5, cmask, "config:24-28");
DEFINE_UNCORE_FORMAT_ATTR(cmask8, cmask, "config:24-31");
DEFINE_UNCORE_FORMAT_ATTR(thresh8, thresh, "config:24-31");
DEFINE_UNCORE_FORMAT_ATTR(thresh5, thresh, "config:24-28");
DEFINE_UNCORE_FORMAT_ATTR(occ_sel, occ_sel, "config:14-15");
DEFINE_UNCORE_FORMAT_ATTR(occ_invert, occ_invert, "config:30");
DEFINE_UNCORE_FORMAT_ATTR(occ_edge, occ_edge, "config:14-51");
DEFINE_UNCORE_FORMAT_ATTR(filter_tid, filter_tid, "config1:0-4");
DEFINE_UNCORE_FORMAT_ATTR(filter_link, filter_link, "config1:5-8");
DEFINE_UNCORE_FORMAT_ATTR(filter_nid, filter_nid, "config1:10-17");
DEFINE_UNCORE_FORMAT_ATTR(filter_nid2, filter_nid, "config1:32-47");
DEFINE_UNCORE_FORMAT_ATTR(filter_state, filter_state, "config1:18-22");
DEFINE_UNCORE_FORMAT_ATTR(filter_state2, filter_state, "config1:17-22");
DEFINE_UNCORE_FORMAT_ATTR(filter_opc, filter_opc, "config1:23-31");
DEFINE_UNCORE_FORMAT_ATTR(filter_opc2, filter_opc, "config1:52-60");
DEFINE_UNCORE_FORMAT_ATTR(filter_band0, filter_band0, "config1:0-7");
DEFINE_UNCORE_FORMAT_ATTR(filter_band1, filter_band1, "config1:8-15");
DEFINE_UNCORE_FORMAT_ATTR(filter_band2, filter_band2, "config1:16-23");
DEFINE_UNCORE_FORMAT_ATTR(filter_band3, filter_band3, "config1:24-31");
DEFINE_UNCORE_FORMAT_ATTR(match_rds, match_rds, "config1:48-51");
DEFINE_UNCORE_FORMAT_ATTR(match_rnid30, match_rnid30, "config1:32-35");
DEFINE_UNCORE_FORMAT_ATTR(match_rnid4, match_rnid4, "config1:31");
DEFINE_UNCORE_FORMAT_ATTR(match_dnid, match_dnid, "config1:13-17");
DEFINE_UNCORE_FORMAT_ATTR(match_mc, match_mc, "config1:9-12");
DEFINE_UNCORE_FORMAT_ATTR(match_opc, match_opc, "config1:5-8");
DEFINE_UNCORE_FORMAT_ATTR(match_vnw, match_vnw, "config1:3-4");
DEFINE_UNCORE_FORMAT_ATTR(match0, match0, "config1:0-31");
DEFINE_UNCORE_FORMAT_ATTR(match1, match1, "config1:32-63");
DEFINE_UNCORE_FORMAT_ATTR(mask_rds, mask_rds, "config2:48-51");
DEFINE_UNCORE_FORMAT_ATTR(mask_rnid30, mask_rnid30, "config2:32-35");
DEFINE_UNCORE_FORMAT_ATTR(mask_rnid4, mask_rnid4, "config2:31");
DEFINE_UNCORE_FORMAT_ATTR(mask_dnid, mask_dnid, "config2:13-17");
DEFINE_UNCORE_FORMAT_ATTR(mask_mc, mask_mc, "config2:9-12");
DEFINE_UNCORE_FORMAT_ATTR(mask_opc, mask_opc, "config2:5-8");
DEFINE_UNCORE_FORMAT_ATTR(mask_vnw, mask_vnw, "config2:3-4");
DEFINE_UNCORE_FORMAT_ATTR(mask0, mask0, "config2:0-31");
DEFINE_UNCORE_FORMAT_ATTR(mask1, mask1, "config2:32-63");

static u64 uncore_msr_read_counter(struct intel_uncore_box *box, struct perf_event *event)
{
	u64 count;

	rdmsrl(event->hw.event_base, count);

	return count;
}

/*
 * generic get constraint function for shared match/mask registers.
 */
static struct event_constraint *
uncore_get_constraint(struct intel_uncore_box *box, struct perf_event *event)
{
	struct intel_uncore_extra_reg *er;
	struct hw_perf_event_extra *reg1 = &event->hw.extra_reg;
	struct hw_perf_event_extra *reg2 = &event->hw.branch_reg;
	unsigned long flags;
	bool ok = false;

	/*
	 * reg->alloc can be set due to existing state, so for fake box we
	 * need to ignore this, otherwise we might fail to allocate proper
	 * fake state for this extra reg constraint.
	 */
	if (reg1->idx == EXTRA_REG_NONE ||
	    (!uncore_box_is_fake(box) && reg1->alloc))
		return NULL;

	er = &box->shared_regs[reg1->idx];
	raw_spin_lock_irqsave(&er->lock, flags);
	if (!atomic_read(&er->ref) ||
	    (er->config1 == reg1->config && er->config2 == reg2->config)) {
		atomic_inc(&er->ref);
		er->config1 = reg1->config;
		er->config2 = reg2->config;
		ok = true;
	}
	raw_spin_unlock_irqrestore(&er->lock, flags);

	if (ok) {
		if (!uncore_box_is_fake(box))
			reg1->alloc = 1;
		return NULL;
	}

	return &constraint_empty;
}

static void uncore_put_constraint(struct intel_uncore_box *box, struct perf_event *event)
{
	struct intel_uncore_extra_reg *er;
	struct hw_perf_event_extra *reg1 = &event->hw.extra_reg;

	/*
	 * Only put constraint if extra reg was actually allocated. Also
	 * takes care of event which do not use an extra shared reg.
	 *
	 * Also, if this is a fake box we shouldn't touch any event state
	 * (reg->alloc) and we don't care about leaving inconsistent box
	 * state either since it will be thrown out.
	 */
	if (uncore_box_is_fake(box) || !reg1->alloc)
		return;

	er = &box->shared_regs[reg1->idx];
	atomic_dec(&er->ref);
	reg1->alloc = 0;
}

static u64 uncore_shared_reg_config(struct intel_uncore_box *box, int idx)
{
	struct intel_uncore_extra_reg *er;
	unsigned long flags;
	u64 config;

	er = &box->shared_regs[idx];

	raw_spin_lock_irqsave(&er->lock, flags);
	config = er->config;
	raw_spin_unlock_irqrestore(&er->lock, flags);

	return config;
}

/* Sandy Bridge-EP uncore support */
static struct intel_uncore_type snbep_uncore_cbox;
static struct intel_uncore_type snbep_uncore_pcu;

static void snbep_uncore_pci_disable_box(struct intel_uncore_box *box)
{
	struct pci_dev *pdev = box->pci_dev;
	int box_ctl = uncore_pci_box_ctl(box);
	u32 config = 0;

	if (!pci_read_config_dword(pdev, box_ctl, &config)) {
		config |= SNBEP_PMON_BOX_CTL_FRZ;
		pci_write_config_dword(pdev, box_ctl, config);
	}
}

static void snbep_uncore_pci_enable_box(struct intel_uncore_box *box)
{
	struct pci_dev *pdev = box->pci_dev;
	int box_ctl = uncore_pci_box_ctl(box);
	u32 config = 0;

	if (!pci_read_config_dword(pdev, box_ctl, &config)) {
		config &= ~SNBEP_PMON_BOX_CTL_FRZ;
		pci_write_config_dword(pdev, box_ctl, config);
	}
}

static void snbep_uncore_pci_enable_event(struct intel_uncore_box *box, struct perf_event *event)
{
	struct pci_dev *pdev = box->pci_dev;
	struct hw_perf_event *hwc = &event->hw;

	pci_write_config_dword(pdev, hwc->config_base, hwc->config | SNBEP_PMON_CTL_EN);
}

static void snbep_uncore_pci_disable_event(struct intel_uncore_box *box, struct perf_event *event)
{
	struct pci_dev *pdev = box->pci_dev;
	struct hw_perf_event *hwc = &event->hw;

	pci_write_config_dword(pdev, hwc->config_base, hwc->config);
}

static u64 snbep_uncore_pci_read_counter(struct intel_uncore_box *box, struct perf_event *event)
{
	struct pci_dev *pdev = box->pci_dev;
	struct hw_perf_event *hwc = &event->hw;
	u64 count = 0;

	pci_read_config_dword(pdev, hwc->event_base, (u32 *)&count);
	pci_read_config_dword(pdev, hwc->event_base + 4, (u32 *)&count + 1);

	return count;
}

static void snbep_uncore_pci_init_box(struct intel_uncore_box *box)
{
	struct pci_dev *pdev = box->pci_dev;

	pci_write_config_dword(pdev, SNBEP_PCI_PMON_BOX_CTL, SNBEP_PMON_BOX_CTL_INT);
}

static void snbep_uncore_msr_disable_box(struct intel_uncore_box *box)
{
	u64 config;
	unsigned msr;

	msr = uncore_msr_box_ctl(box);
	if (msr) {
		rdmsrl(msr, config);
		config |= SNBEP_PMON_BOX_CTL_FRZ;
		wrmsrl(msr, config);
	}
}

static void snbep_uncore_msr_enable_box(struct intel_uncore_box *box)
{
	u64 config;
	unsigned msr;

	msr = uncore_msr_box_ctl(box);
	if (msr) {
		rdmsrl(msr, config);
		config &= ~SNBEP_PMON_BOX_CTL_FRZ;
		wrmsrl(msr, config);
	}
}

static void snbep_uncore_msr_enable_event(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	struct hw_perf_event_extra *reg1 = &hwc->extra_reg;

	if (reg1->idx != EXTRA_REG_NONE)
		wrmsrl(reg1->reg, uncore_shared_reg_config(box, 0));

	wrmsrl(hwc->config_base, hwc->config | SNBEP_PMON_CTL_EN);
}

static void snbep_uncore_msr_disable_event(struct intel_uncore_box *box,
					struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;

	wrmsrl(hwc->config_base, hwc->config);
}

static void snbep_uncore_msr_init_box(struct intel_uncore_box *box)
{
	unsigned msr = uncore_msr_box_ctl(box);

	if (msr)
		wrmsrl(msr, SNBEP_PMON_BOX_CTL_INT);
}

static struct attribute *snbep_uncore_formats_attr[] = {
	&format_attr_event.attr,
	&format_attr_umask.attr,
	&format_attr_edge.attr,
	&format_attr_inv.attr,
	&format_attr_thresh8.attr,
	NULL,
};

static struct attribute *snbep_uncore_ubox_formats_attr[] = {
	&format_attr_event.attr,
	&format_attr_umask.attr,
	&format_attr_edge.attr,
	&format_attr_inv.attr,
	&format_attr_thresh5.attr,
	NULL,
};

static struct attribute *snbep_uncore_cbox_formats_attr[] = {
	&format_attr_event.attr,
	&format_attr_umask.attr,
	&format_attr_edge.attr,
	&format_attr_tid_en.attr,
	&format_attr_inv.attr,
	&format_attr_thresh8.attr,
	&format_attr_filter_tid.attr,
	&format_attr_filter_nid.attr,
	&format_attr_filter_state.attr,
	&format_attr_filter_opc.attr,
	NULL,
};

static struct attribute *snbep_uncore_pcu_formats_attr[] = {
	&format_attr_event_ext.attr,
	&format_attr_occ_sel.attr,
	&format_attr_edge.attr,
	&format_attr_inv.attr,
	&format_attr_thresh5.attr,
	&format_attr_occ_invert.attr,
	&format_attr_occ_edge.attr,
	&format_attr_filter_band0.attr,
	&format_attr_filter_band1.attr,
	&format_attr_filter_band2.attr,
	&format_attr_filter_band3.attr,
	NULL,
};

static struct attribute *snbep_uncore_qpi_formats_attr[] = {
	&format_attr_event_ext.attr,
	&format_attr_umask.attr,
	&format_attr_edge.attr,
	&format_attr_inv.attr,
	&format_attr_thresh8.attr,
	&format_attr_match_rds.attr,
	&format_attr_match_rnid30.attr,
	&format_attr_match_rnid4.attr,
	&format_attr_match_dnid.attr,
	&format_attr_match_mc.attr,
	&format_attr_match_opc.attr,
	&format_attr_match_vnw.attr,
	&format_attr_match0.attr,
	&format_attr_match1.attr,
	&format_attr_mask_rds.attr,
	&format_attr_mask_rnid30.attr,
	&format_attr_mask_rnid4.attr,
	&format_attr_mask_dnid.attr,
	&format_attr_mask_mc.attr,
	&format_attr_mask_opc.attr,
	&format_attr_mask_vnw.attr,
	&format_attr_mask0.attr,
	&format_attr_mask1.attr,
	NULL,
};

static struct uncore_event_desc snbep_uncore_imc_events[] = {
	INTEL_UNCORE_EVENT_DESC(clockticks,      "event=0xff,umask=0x00"),
	INTEL_UNCORE_EVENT_DESC(cas_count_read,  "event=0x04,umask=0x03"),
	INTEL_UNCORE_EVENT_DESC(cas_count_write, "event=0x04,umask=0x0c"),
	{ /* end: all zeroes */ },
};

static struct uncore_event_desc snbep_uncore_qpi_events[] = {
	INTEL_UNCORE_EVENT_DESC(clockticks,       "event=0x14"),
	INTEL_UNCORE_EVENT_DESC(txl_flits_active, "event=0x00,umask=0x06"),
	INTEL_UNCORE_EVENT_DESC(drs_data,         "event=0x102,umask=0x08"),
	INTEL_UNCORE_EVENT_DESC(ncb_data,         "event=0x103,umask=0x04"),
	{ /* end: all zeroes */ },
};

static struct attribute_group snbep_uncore_format_group = {
	.name = "format",
	.attrs = snbep_uncore_formats_attr,
};

static struct attribute_group snbep_uncore_ubox_format_group = {
	.name = "format",
	.attrs = snbep_uncore_ubox_formats_attr,
};

static struct attribute_group snbep_uncore_cbox_format_group = {
	.name = "format",
	.attrs = snbep_uncore_cbox_formats_attr,
};

static struct attribute_group snbep_uncore_pcu_format_group = {
	.name = "format",
	.attrs = snbep_uncore_pcu_formats_attr,
};

static struct attribute_group snbep_uncore_qpi_format_group = {
	.name = "format",
	.attrs = snbep_uncore_qpi_formats_attr,
};

#define SNBEP_UNCORE_MSR_OPS_COMMON_INIT()			\
	.init_box	= snbep_uncore_msr_init_box,		\
	.disable_box	= snbep_uncore_msr_disable_box,		\
	.enable_box	= snbep_uncore_msr_enable_box,		\
	.disable_event	= snbep_uncore_msr_disable_event,	\
	.enable_event	= snbep_uncore_msr_enable_event,	\
	.read_counter	= uncore_msr_read_counter

static struct intel_uncore_ops snbep_uncore_msr_ops = {
	SNBEP_UNCORE_MSR_OPS_COMMON_INIT(),
};

#define SNBEP_UNCORE_PCI_OPS_COMMON_INIT()			\
	.init_box	= snbep_uncore_pci_init_box,		\
	.disable_box	= snbep_uncore_pci_disable_box,		\
	.enable_box	= snbep_uncore_pci_enable_box,		\
	.disable_event	= snbep_uncore_pci_disable_event,	\
	.read_counter	= snbep_uncore_pci_read_counter

static struct intel_uncore_ops snbep_uncore_pci_ops = {
	SNBEP_UNCORE_PCI_OPS_COMMON_INIT(),
	.enable_event	= snbep_uncore_pci_enable_event,	\
};

static struct event_constraint snbep_uncore_cbox_constraints[] = {
	UNCORE_EVENT_CONSTRAINT(0x01, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x02, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x04, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x05, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x07, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x09, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x11, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x12, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x13, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x1b, 0xc),
	UNCORE_EVENT_CONSTRAINT(0x1c, 0xc),
	UNCORE_EVENT_CONSTRAINT(0x1d, 0xc),
	UNCORE_EVENT_CONSTRAINT(0x1e, 0xc),
	EVENT_CONSTRAINT_OVERLAP(0x1f, 0xe, 0xff),
	UNCORE_EVENT_CONSTRAINT(0x21, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x23, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x31, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x32, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x33, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x34, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x35, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x36, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x37, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x38, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x39, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x3b, 0x1),
	EVENT_CONSTRAINT_END
};

static struct event_constraint snbep_uncore_r2pcie_constraints[] = {
	UNCORE_EVENT_CONSTRAINT(0x10, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x11, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x12, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x23, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x24, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x25, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x26, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x32, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x33, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x34, 0x3),
	EVENT_CONSTRAINT_END
};

static struct event_constraint snbep_uncore_r3qpi_constraints[] = {
	UNCORE_EVENT_CONSTRAINT(0x10, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x11, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x12, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x13, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x20, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x21, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x22, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x23, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x24, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x25, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x26, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x28, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x29, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x2a, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x2b, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x2c, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x2d, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x2e, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x2f, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x30, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x31, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x32, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x33, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x34, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x36, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x37, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x38, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x39, 0x3),
	EVENT_CONSTRAINT_END
};

static struct intel_uncore_type snbep_uncore_ubox = {
	.name		= "ubox",
	.num_counters   = 2,
	.num_boxes	= 1,
	.perf_ctr_bits	= 44,
	.fixed_ctr_bits	= 48,
	.perf_ctr	= SNBEP_U_MSR_PMON_CTR0,
	.event_ctl	= SNBEP_U_MSR_PMON_CTL0,
	.event_mask	= SNBEP_U_MSR_PMON_RAW_EVENT_MASK,
	.fixed_ctr	= SNBEP_U_MSR_PMON_UCLK_FIXED_CTR,
	.fixed_ctl	= SNBEP_U_MSR_PMON_UCLK_FIXED_CTL,
	.ops		= &snbep_uncore_msr_ops,
	.format_group	= &snbep_uncore_ubox_format_group,
};

static struct extra_reg snbep_uncore_cbox_extra_regs[] = {
	SNBEP_CBO_EVENT_EXTRA_REG(SNBEP_CBO_PMON_CTL_TID_EN,
				  SNBEP_CBO_PMON_CTL_TID_EN, 0x1),
	SNBEP_CBO_EVENT_EXTRA_REG(0x0334, 0xffff, 0x4),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4334, 0xffff, 0x6),
	SNBEP_CBO_EVENT_EXTRA_REG(0x0534, 0xffff, 0x4),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4534, 0xffff, 0x6),
	SNBEP_CBO_EVENT_EXTRA_REG(0x0934, 0xffff, 0x4),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4934, 0xffff, 0x6),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4134, 0xffff, 0x6),
	SNBEP_CBO_EVENT_EXTRA_REG(0x0135, 0xffff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x0335, 0xffff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4135, 0xffff, 0xc),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4335, 0xffff, 0xc),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4435, 0xffff, 0x2),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4835, 0xffff, 0x2),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4a35, 0xffff, 0x2),
	SNBEP_CBO_EVENT_EXTRA_REG(0x5035, 0xffff, 0x2),
	SNBEP_CBO_EVENT_EXTRA_REG(0x0136, 0xffff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x0336, 0xffff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4136, 0xffff, 0xc),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4336, 0xffff, 0xc),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4436, 0xffff, 0x2),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4836, 0xffff, 0x2),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4a36, 0xffff, 0x2),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4037, 0x40ff, 0x2),
	EVENT_EXTRA_END
};

static void snbep_cbox_put_constraint(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event_extra *reg1 = &event->hw.extra_reg;
	struct intel_uncore_extra_reg *er = &box->shared_regs[0];
	int i;

	if (uncore_box_is_fake(box))
		return;

	for (i = 0; i < 5; i++) {
		if (reg1->alloc & (0x1 << i))
			atomic_sub(1 << (i * 6), &er->ref);
	}
	reg1->alloc = 0;
}

static struct event_constraint *
__snbep_cbox_get_constraint(struct intel_uncore_box *box, struct perf_event *event,
			    u64 (*cbox_filter_mask)(int fields))
{
	struct hw_perf_event_extra *reg1 = &event->hw.extra_reg;
	struct intel_uncore_extra_reg *er = &box->shared_regs[0];
	int i, alloc = 0;
	unsigned long flags;
	u64 mask;

	if (reg1->idx == EXTRA_REG_NONE)
		return NULL;

	raw_spin_lock_irqsave(&er->lock, flags);
	for (i = 0; i < 5; i++) {
		if (!(reg1->idx & (0x1 << i)))
			continue;
		if (!uncore_box_is_fake(box) && (reg1->alloc & (0x1 << i)))
			continue;

		mask = cbox_filter_mask(0x1 << i);
		if (!__BITS_VALUE(atomic_read(&er->ref), i, 6) ||
		    !((reg1->config ^ er->config) & mask)) {
			atomic_add(1 << (i * 6), &er->ref);
			er->config &= ~mask;
			er->config |= reg1->config & mask;
			alloc |= (0x1 << i);
		} else {
			break;
		}
	}
	raw_spin_unlock_irqrestore(&er->lock, flags);
	if (i < 5)
		goto fail;

	if (!uncore_box_is_fake(box))
		reg1->alloc |= alloc;

	return NULL;
fail:
	for (; i >= 0; i--) {
		if (alloc & (0x1 << i))
			atomic_sub(1 << (i * 6), &er->ref);
	}
	return &constraint_empty;
}

static u64 snbep_cbox_filter_mask(int fields)
{
	u64 mask = 0;

	if (fields & 0x1)
		mask |= SNBEP_CB0_MSR_PMON_BOX_FILTER_TID;
	if (fields & 0x2)
		mask |= SNBEP_CB0_MSR_PMON_BOX_FILTER_NID;
	if (fields & 0x4)
		mask |= SNBEP_CB0_MSR_PMON_BOX_FILTER_STATE;
	if (fields & 0x8)
		mask |= SNBEP_CB0_MSR_PMON_BOX_FILTER_OPC;

	return mask;
}

static struct event_constraint *
snbep_cbox_get_constraint(struct intel_uncore_box *box, struct perf_event *event)
{
	return __snbep_cbox_get_constraint(box, event, snbep_cbox_filter_mask);
}

static int snbep_cbox_hw_config(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event_extra *reg1 = &event->hw.extra_reg;
	struct extra_reg *er;
	int idx = 0;

	for (er = snbep_uncore_cbox_extra_regs; er->msr; er++) {
		if (er->event != (event->hw.config & er->config_mask))
			continue;
		idx |= er->idx;
	}

	if (idx) {
		reg1->reg = SNBEP_C0_MSR_PMON_BOX_FILTER +
			SNBEP_CBO_MSR_OFFSET * box->pmu->pmu_idx;
		reg1->config = event->attr.config1 & snbep_cbox_filter_mask(idx);
		reg1->idx = idx;
	}
	return 0;
}

static struct intel_uncore_ops snbep_uncore_cbox_ops = {
	SNBEP_UNCORE_MSR_OPS_COMMON_INIT(),
	.hw_config		= snbep_cbox_hw_config,
	.get_constraint		= snbep_cbox_get_constraint,
	.put_constraint		= snbep_cbox_put_constraint,
};

static struct intel_uncore_type snbep_uncore_cbox = {
	.name			= "cbox",
	.num_counters		= 4,
	.num_boxes		= 8,
	.perf_ctr_bits		= 44,
	.event_ctl		= SNBEP_C0_MSR_PMON_CTL0,
	.perf_ctr		= SNBEP_C0_MSR_PMON_CTR0,
	.event_mask		= SNBEP_CBO_MSR_PMON_RAW_EVENT_MASK,
	.box_ctl		= SNBEP_C0_MSR_PMON_BOX_CTL,
	.msr_offset		= SNBEP_CBO_MSR_OFFSET,
	.num_shared_regs	= 1,
	.constraints		= snbep_uncore_cbox_constraints,
	.ops			= &snbep_uncore_cbox_ops,
	.format_group		= &snbep_uncore_cbox_format_group,
};

static u64 snbep_pcu_alter_er(struct perf_event *event, int new_idx, bool modify)
{
	struct hw_perf_event *hwc = &event->hw;
	struct hw_perf_event_extra *reg1 = &hwc->extra_reg;
	u64 config = reg1->config;

	if (new_idx > reg1->idx)
		config <<= 8 * (new_idx - reg1->idx);
	else
		config >>= 8 * (reg1->idx - new_idx);

	if (modify) {
		hwc->config += new_idx - reg1->idx;
		reg1->config = config;
		reg1->idx = new_idx;
	}
	return config;
}

static struct event_constraint *
snbep_pcu_get_constraint(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event_extra *reg1 = &event->hw.extra_reg;
	struct intel_uncore_extra_reg *er = &box->shared_regs[0];
	unsigned long flags;
	int idx = reg1->idx;
	u64 mask, config1 = reg1->config;
	bool ok = false;

	if (reg1->idx == EXTRA_REG_NONE ||
	    (!uncore_box_is_fake(box) && reg1->alloc))
		return NULL;
again:
	mask = 0xffULL << (idx * 8);
	raw_spin_lock_irqsave(&er->lock, flags);
	if (!__BITS_VALUE(atomic_read(&er->ref), idx, 8) ||
	    !((config1 ^ er->config) & mask)) {
		atomic_add(1 << (idx * 8), &er->ref);
		er->config &= ~mask;
		er->config |= config1 & mask;
		ok = true;
	}
	raw_spin_unlock_irqrestore(&er->lock, flags);

	if (!ok) {
		idx = (idx + 1) % 4;
		if (idx != reg1->idx) {
			config1 = snbep_pcu_alter_er(event, idx, false);
			goto again;
		}
		return &constraint_empty;
	}

	if (!uncore_box_is_fake(box)) {
		if (idx != reg1->idx)
			snbep_pcu_alter_er(event, idx, true);
		reg1->alloc = 1;
	}
	return NULL;
}

static void snbep_pcu_put_constraint(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event_extra *reg1 = &event->hw.extra_reg;
	struct intel_uncore_extra_reg *er = &box->shared_regs[0];

	if (uncore_box_is_fake(box) || !reg1->alloc)
		return;

	atomic_sub(1 << (reg1->idx * 8), &er->ref);
	reg1->alloc = 0;
}

static int snbep_pcu_hw_config(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	struct hw_perf_event_extra *reg1 = &hwc->extra_reg;
	int ev_sel = hwc->config & SNBEP_PMON_CTL_EV_SEL_MASK;

	if (ev_sel >= 0xb && ev_sel <= 0xe) {
		reg1->reg = SNBEP_PCU_MSR_PMON_BOX_FILTER;
		reg1->idx = ev_sel - 0xb;
		reg1->config = event->attr.config1 & (0xff << reg1->idx);
	}
	return 0;
}

static struct intel_uncore_ops snbep_uncore_pcu_ops = {
	SNBEP_UNCORE_MSR_OPS_COMMON_INIT(),
	.hw_config		= snbep_pcu_hw_config,
	.get_constraint		= snbep_pcu_get_constraint,
	.put_constraint		= snbep_pcu_put_constraint,
};

static struct intel_uncore_type snbep_uncore_pcu = {
	.name			= "pcu",
	.num_counters		= 4,
	.num_boxes		= 1,
	.perf_ctr_bits		= 48,
	.perf_ctr		= SNBEP_PCU_MSR_PMON_CTR0,
	.event_ctl		= SNBEP_PCU_MSR_PMON_CTL0,
	.event_mask		= SNBEP_PCU_MSR_PMON_RAW_EVENT_MASK,
	.box_ctl		= SNBEP_PCU_MSR_PMON_BOX_CTL,
	.num_shared_regs	= 1,
	.ops			= &snbep_uncore_pcu_ops,
	.format_group		= &snbep_uncore_pcu_format_group,
};

static struct intel_uncore_type *snbep_msr_uncores[] = {
	&snbep_uncore_ubox,
	&snbep_uncore_cbox,
	&snbep_uncore_pcu,
	NULL,
};

enum {
	SNBEP_PCI_QPI_PORT0_FILTER,
	SNBEP_PCI_QPI_PORT1_FILTER,
};

static int snbep_qpi_hw_config(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	struct hw_perf_event_extra *reg1 = &hwc->extra_reg;
	struct hw_perf_event_extra *reg2 = &hwc->branch_reg;

	if ((hwc->config & SNBEP_PMON_CTL_EV_SEL_MASK) == 0x38) {
		reg1->idx = 0;
		reg1->reg = SNBEP_Q_Py_PCI_PMON_PKT_MATCH0;
		reg1->config = event->attr.config1;
		reg2->reg = SNBEP_Q_Py_PCI_PMON_PKT_MASK0;
		reg2->config = event->attr.config2;
	}
	return 0;
}

static void snbep_qpi_enable_event(struct intel_uncore_box *box, struct perf_event *event)
{
	struct pci_dev *pdev = box->pci_dev;
	struct hw_perf_event *hwc = &event->hw;
	struct hw_perf_event_extra *reg1 = &hwc->extra_reg;
	struct hw_perf_event_extra *reg2 = &hwc->branch_reg;

	if (reg1->idx != EXTRA_REG_NONE) {
		int idx = box->pmu->pmu_idx + SNBEP_PCI_QPI_PORT0_FILTER;
		struct pci_dev *filter_pdev = extra_pci_dev[box->phys_id][idx];
		WARN_ON_ONCE(!filter_pdev);
		if (filter_pdev) {
			pci_write_config_dword(filter_pdev, reg1->reg,
						(u32)reg1->config);
			pci_write_config_dword(filter_pdev, reg1->reg + 4,
						(u32)(reg1->config >> 32));
			pci_write_config_dword(filter_pdev, reg2->reg,
						(u32)reg2->config);
			pci_write_config_dword(filter_pdev, reg2->reg + 4,
						(u32)(reg2->config >> 32));
		}
	}

	pci_write_config_dword(pdev, hwc->config_base, hwc->config | SNBEP_PMON_CTL_EN);
}

static struct intel_uncore_ops snbep_uncore_qpi_ops = {
	SNBEP_UNCORE_PCI_OPS_COMMON_INIT(),
	.enable_event		= snbep_qpi_enable_event,
	.hw_config		= snbep_qpi_hw_config,
	.get_constraint		= uncore_get_constraint,
	.put_constraint		= uncore_put_constraint,
};

#define SNBEP_UNCORE_PCI_COMMON_INIT()				\
	.perf_ctr	= SNBEP_PCI_PMON_CTR0,			\
	.event_ctl	= SNBEP_PCI_PMON_CTL0,			\
	.event_mask	= SNBEP_PMON_RAW_EVENT_MASK,		\
	.box_ctl	= SNBEP_PCI_PMON_BOX_CTL,		\
	.ops		= &snbep_uncore_pci_ops,		\
	.format_group	= &snbep_uncore_format_group

static struct intel_uncore_type snbep_uncore_ha = {
	.name		= "ha",
	.num_counters   = 4,
	.num_boxes	= 1,
	.perf_ctr_bits	= 48,
	SNBEP_UNCORE_PCI_COMMON_INIT(),
};

static struct intel_uncore_type snbep_uncore_imc = {
	.name		= "imc",
	.num_counters   = 4,
	.num_boxes	= 4,
	.perf_ctr_bits	= 48,
	.fixed_ctr_bits	= 48,
	.fixed_ctr	= SNBEP_MC_CHy_PCI_PMON_FIXED_CTR,
	.fixed_ctl	= SNBEP_MC_CHy_PCI_PMON_FIXED_CTL,
	.event_descs	= snbep_uncore_imc_events,
	SNBEP_UNCORE_PCI_COMMON_INIT(),
};

static struct intel_uncore_type snbep_uncore_qpi = {
	.name			= "qpi",
	.num_counters		= 4,
	.num_boxes		= 2,
	.perf_ctr_bits		= 48,
	.perf_ctr		= SNBEP_PCI_PMON_CTR0,
	.event_ctl		= SNBEP_PCI_PMON_CTL0,
	.event_mask		= SNBEP_QPI_PCI_PMON_RAW_EVENT_MASK,
	.box_ctl		= SNBEP_PCI_PMON_BOX_CTL,
	.num_shared_regs	= 1,
	.ops			= &snbep_uncore_qpi_ops,
	.event_descs		= snbep_uncore_qpi_events,
	.format_group		= &snbep_uncore_qpi_format_group,
};


static struct intel_uncore_type snbep_uncore_r2pcie = {
	.name		= "r2pcie",
	.num_counters   = 4,
	.num_boxes	= 1,
	.perf_ctr_bits	= 44,
	.constraints	= snbep_uncore_r2pcie_constraints,
	SNBEP_UNCORE_PCI_COMMON_INIT(),
};

static struct intel_uncore_type snbep_uncore_r3qpi = {
	.name		= "r3qpi",
	.num_counters   = 3,
	.num_boxes	= 2,
	.perf_ctr_bits	= 44,
	.constraints	= snbep_uncore_r3qpi_constraints,
	SNBEP_UNCORE_PCI_COMMON_INIT(),
};

enum {
	SNBEP_PCI_UNCORE_HA,
	SNBEP_PCI_UNCORE_IMC,
	SNBEP_PCI_UNCORE_QPI,
	SNBEP_PCI_UNCORE_R2PCIE,
	SNBEP_PCI_UNCORE_R3QPI,
};

static struct intel_uncore_type *snbep_pci_uncores[] = {
	[SNBEP_PCI_UNCORE_HA]		= &snbep_uncore_ha,
	[SNBEP_PCI_UNCORE_IMC]		= &snbep_uncore_imc,
	[SNBEP_PCI_UNCORE_QPI]		= &snbep_uncore_qpi,
	[SNBEP_PCI_UNCORE_R2PCIE]	= &snbep_uncore_r2pcie,
	[SNBEP_PCI_UNCORE_R3QPI]	= &snbep_uncore_r3qpi,
	NULL,
};

static DEFINE_PCI_DEVICE_TABLE(snbep_uncore_pci_ids) = {
	{ /* Home Agent */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_UNC_HA),
		.driver_data = UNCORE_PCI_DEV_DATA(SNBEP_PCI_UNCORE_HA, 0),
	},
	{ /* MC Channel 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_UNC_IMC0),
		.driver_data = UNCORE_PCI_DEV_DATA(SNBEP_PCI_UNCORE_IMC, 0),
	},
	{ /* MC Channel 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_UNC_IMC1),
		.driver_data = UNCORE_PCI_DEV_DATA(SNBEP_PCI_UNCORE_IMC, 1),
	},
	{ /* MC Channel 2 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_UNC_IMC2),
		.driver_data = UNCORE_PCI_DEV_DATA(SNBEP_PCI_UNCORE_IMC, 2),
	},
	{ /* MC Channel 3 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_UNC_IMC3),
		.driver_data = UNCORE_PCI_DEV_DATA(SNBEP_PCI_UNCORE_IMC, 3),
	},
	{ /* QPI Port 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_UNC_QPI0),
		.driver_data = UNCORE_PCI_DEV_DATA(SNBEP_PCI_UNCORE_QPI, 0),
	},
	{ /* QPI Port 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_UNC_QPI1),
		.driver_data = UNCORE_PCI_DEV_DATA(SNBEP_PCI_UNCORE_QPI, 1),
	},
	{ /* R2PCIe */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_UNC_R2PCIE),
		.driver_data = UNCORE_PCI_DEV_DATA(SNBEP_PCI_UNCORE_R2PCIE, 0),
	},
	{ /* R3QPI Link 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_UNC_R3QPI0),
		.driver_data = UNCORE_PCI_DEV_DATA(SNBEP_PCI_UNCORE_R3QPI, 0),
	},
	{ /* R3QPI Link 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_UNC_R3QPI1),
		.driver_data = UNCORE_PCI_DEV_DATA(SNBEP_PCI_UNCORE_R3QPI, 1),
	},
	{ /* QPI Port 0 filter  */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x3c86),
		.driver_data = UNCORE_PCI_DEV_DATA(UNCORE_EXTRA_PCI_DEV,
						   SNBEP_PCI_QPI_PORT0_FILTER),
	},
	{ /* QPI Port 0 filter  */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x3c96),
		.driver_data = UNCORE_PCI_DEV_DATA(UNCORE_EXTRA_PCI_DEV,
						   SNBEP_PCI_QPI_PORT1_FILTER),
	},
	{ /* end: all zeroes */ }
};

static struct pci_driver snbep_uncore_pci_driver = {
	.name		= "snbep_uncore",
	.id_table	= snbep_uncore_pci_ids,
};

/*
 * build pci bus to socket mapping
 */
static int snbep_pci2phy_map_init(int devid)
{
	struct pci_dev *ubox_dev = NULL;
	int i, bus, nodeid;
	int err = 0;
	u32 config = 0;

	while (1) {
		/* find the UBOX device */
		ubox_dev = pci_get_device(PCI_VENDOR_ID_INTEL, devid, ubox_dev);
		if (!ubox_dev)
			break;
		bus = ubox_dev->bus->number;
		/* get the Node ID of the local register */
		err = pci_read_config_dword(ubox_dev, 0x40, &config);
		if (err)
			break;
		nodeid = config;
		/* get the Node ID mapping */
		err = pci_read_config_dword(ubox_dev, 0x54, &config);
		if (err)
			break;
		/*
		 * every three bits in the Node ID mapping register maps
		 * to a particular node.
		 */
		for (i = 0; i < 8; i++) {
			if (nodeid == ((config >> (3 * i)) & 0x7)) {
				pcibus_to_physid[bus] = i;
				break;
			}
		}
	}

	if (!err) {
		/*
		 * For PCI bus with no UBOX device, find the next bus
		 * that has UBOX device and use its mapping.
		 */
		i = -1;
		for (bus = 255; bus >= 0; bus--) {
			if (pcibus_to_physid[bus] >= 0)
				i = pcibus_to_physid[bus];
			else
				pcibus_to_physid[bus] = i;
		}
	}

	if (ubox_dev)
		pci_dev_put(ubox_dev);

	return err ? pcibios_err_to_errno(err) : 0;
}
/* end of Sandy Bridge-EP uncore support */

/* IvyTown uncore support */
static void ivt_uncore_msr_init_box(struct intel_uncore_box *box)
{
	unsigned msr = uncore_msr_box_ctl(box);
	if (msr)
		wrmsrl(msr, IVT_PMON_BOX_CTL_INT);
}

static void ivt_uncore_pci_init_box(struct intel_uncore_box *box)
{
	struct pci_dev *pdev = box->pci_dev;

	pci_write_config_dword(pdev, SNBEP_PCI_PMON_BOX_CTL, IVT_PMON_BOX_CTL_INT);
}

#define IVT_UNCORE_MSR_OPS_COMMON_INIT()			\
	.init_box	= ivt_uncore_msr_init_box,		\
	.disable_box	= snbep_uncore_msr_disable_box,		\
	.enable_box	= snbep_uncore_msr_enable_box,		\
	.disable_event	= snbep_uncore_msr_disable_event,	\
	.enable_event	= snbep_uncore_msr_enable_event,	\
	.read_counter	= uncore_msr_read_counter

static struct intel_uncore_ops ivt_uncore_msr_ops = {
	IVT_UNCORE_MSR_OPS_COMMON_INIT(),
};

static struct intel_uncore_ops ivt_uncore_pci_ops = {
	.init_box	= ivt_uncore_pci_init_box,
	.disable_box	= snbep_uncore_pci_disable_box,
	.enable_box	= snbep_uncore_pci_enable_box,
	.disable_event	= snbep_uncore_pci_disable_event,
	.enable_event	= snbep_uncore_pci_enable_event,
	.read_counter	= snbep_uncore_pci_read_counter,
};

#define IVT_UNCORE_PCI_COMMON_INIT()				\
	.perf_ctr	= SNBEP_PCI_PMON_CTR0,			\
	.event_ctl	= SNBEP_PCI_PMON_CTL0,			\
	.event_mask	= IVT_PMON_RAW_EVENT_MASK,		\
	.box_ctl	= SNBEP_PCI_PMON_BOX_CTL,		\
	.ops		= &ivt_uncore_pci_ops,			\
	.format_group	= &ivt_uncore_format_group

static struct attribute *ivt_uncore_formats_attr[] = {
	&format_attr_event.attr,
	&format_attr_umask.attr,
	&format_attr_edge.attr,
	&format_attr_inv.attr,
	&format_attr_thresh8.attr,
	NULL,
};

static struct attribute *ivt_uncore_ubox_formats_attr[] = {
	&format_attr_event.attr,
	&format_attr_umask.attr,
	&format_attr_edge.attr,
	&format_attr_inv.attr,
	&format_attr_thresh5.attr,
	NULL,
};

static struct attribute *ivt_uncore_cbox_formats_attr[] = {
	&format_attr_event.attr,
	&format_attr_umask.attr,
	&format_attr_edge.attr,
	&format_attr_tid_en.attr,
	&format_attr_thresh8.attr,
	&format_attr_filter_tid.attr,
	&format_attr_filter_link.attr,
	&format_attr_filter_state2.attr,
	&format_attr_filter_nid2.attr,
	&format_attr_filter_opc2.attr,
	NULL,
};

static struct attribute *ivt_uncore_pcu_formats_attr[] = {
	&format_attr_event_ext.attr,
	&format_attr_occ_sel.attr,
	&format_attr_edge.attr,
	&format_attr_thresh5.attr,
	&format_attr_occ_invert.attr,
	&format_attr_occ_edge.attr,
	&format_attr_filter_band0.attr,
	&format_attr_filter_band1.attr,
	&format_attr_filter_band2.attr,
	&format_attr_filter_band3.attr,
	NULL,
};

static struct attribute *ivt_uncore_qpi_formats_attr[] = {
	&format_attr_event_ext.attr,
	&format_attr_umask.attr,
	&format_attr_edge.attr,
	&format_attr_thresh8.attr,
	&format_attr_match_rds.attr,
	&format_attr_match_rnid30.attr,
	&format_attr_match_rnid4.attr,
	&format_attr_match_dnid.attr,
	&format_attr_match_mc.attr,
	&format_attr_match_opc.attr,
	&format_attr_match_vnw.attr,
	&format_attr_match0.attr,
	&format_attr_match1.attr,
	&format_attr_mask_rds.attr,
	&format_attr_mask_rnid30.attr,
	&format_attr_mask_rnid4.attr,
	&format_attr_mask_dnid.attr,
	&format_attr_mask_mc.attr,
	&format_attr_mask_opc.attr,
	&format_attr_mask_vnw.attr,
	&format_attr_mask0.attr,
	&format_attr_mask1.attr,
	NULL,
};

static struct attribute_group ivt_uncore_format_group = {
	.name = "format",
	.attrs = ivt_uncore_formats_attr,
};

static struct attribute_group ivt_uncore_ubox_format_group = {
	.name = "format",
	.attrs = ivt_uncore_ubox_formats_attr,
};

static struct attribute_group ivt_uncore_cbox_format_group = {
	.name = "format",
	.attrs = ivt_uncore_cbox_formats_attr,
};

static struct attribute_group ivt_uncore_pcu_format_group = {
	.name = "format",
	.attrs = ivt_uncore_pcu_formats_attr,
};

static struct attribute_group ivt_uncore_qpi_format_group = {
	.name = "format",
	.attrs = ivt_uncore_qpi_formats_attr,
};

static struct intel_uncore_type ivt_uncore_ubox = {
	.name		= "ubox",
	.num_counters   = 2,
	.num_boxes	= 1,
	.perf_ctr_bits	= 44,
	.fixed_ctr_bits	= 48,
	.perf_ctr	= SNBEP_U_MSR_PMON_CTR0,
	.event_ctl	= SNBEP_U_MSR_PMON_CTL0,
	.event_mask	= IVT_U_MSR_PMON_RAW_EVENT_MASK,
	.fixed_ctr	= SNBEP_U_MSR_PMON_UCLK_FIXED_CTR,
	.fixed_ctl	= SNBEP_U_MSR_PMON_UCLK_FIXED_CTL,
	.ops		= &ivt_uncore_msr_ops,
	.format_group	= &ivt_uncore_ubox_format_group,
};

static struct extra_reg ivt_uncore_cbox_extra_regs[] = {
	SNBEP_CBO_EVENT_EXTRA_REG(SNBEP_CBO_PMON_CTL_TID_EN,
				  SNBEP_CBO_PMON_CTL_TID_EN, 0x1),
	SNBEP_CBO_EVENT_EXTRA_REG(0x1031, 0x10ff, 0x2),
	SNBEP_CBO_EVENT_EXTRA_REG(0x1134, 0xffff, 0x4),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4134, 0xffff, 0xc),
	SNBEP_CBO_EVENT_EXTRA_REG(0x5134, 0xffff, 0xc),
	SNBEP_CBO_EVENT_EXTRA_REG(0x0334, 0xffff, 0x4),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4334, 0xffff, 0xc),
	SNBEP_CBO_EVENT_EXTRA_REG(0x0534, 0xffff, 0x4),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4534, 0xffff, 0xc),
	SNBEP_CBO_EVENT_EXTRA_REG(0x0934, 0xffff, 0x4),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4934, 0xffff, 0xc),
	SNBEP_CBO_EVENT_EXTRA_REG(0x0135, 0xffff, 0x10),
	SNBEP_CBO_EVENT_EXTRA_REG(0x0335, 0xffff, 0x10),
	SNBEP_CBO_EVENT_EXTRA_REG(0x2135, 0xffff, 0x10),
	SNBEP_CBO_EVENT_EXTRA_REG(0x2335, 0xffff, 0x10),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4135, 0xffff, 0x18),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4335, 0xffff, 0x18),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4435, 0xffff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4835, 0xffff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4a35, 0xffff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x5035, 0xffff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x8135, 0xffff, 0x10),
	SNBEP_CBO_EVENT_EXTRA_REG(0x8335, 0xffff, 0x10),
	SNBEP_CBO_EVENT_EXTRA_REG(0x0136, 0xffff, 0x10),
	SNBEP_CBO_EVENT_EXTRA_REG(0x0336, 0xffff, 0x10),
	SNBEP_CBO_EVENT_EXTRA_REG(0x2336, 0xffff, 0x10),
	SNBEP_CBO_EVENT_EXTRA_REG(0x2336, 0xffff, 0x10),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4136, 0xffff, 0x18),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4336, 0xffff, 0x18),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4436, 0xffff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4836, 0xffff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4a36, 0xffff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x5036, 0xffff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x8136, 0xffff, 0x10),
	SNBEP_CBO_EVENT_EXTRA_REG(0x8336, 0xffff, 0x10),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4037, 0x40ff, 0x8),
	EVENT_EXTRA_END
};

static u64 ivt_cbox_filter_mask(int fields)
{
	u64 mask = 0;

	if (fields & 0x1)
		mask |= IVT_CB0_MSR_PMON_BOX_FILTER_TID;
	if (fields & 0x2)
		mask |= IVT_CB0_MSR_PMON_BOX_FILTER_LINK;
	if (fields & 0x4)
		mask |= IVT_CB0_MSR_PMON_BOX_FILTER_STATE;
	if (fields & 0x8)
		mask |= IVT_CB0_MSR_PMON_BOX_FILTER_NID;
	if (fields & 0x10)
		mask |= IVT_CB0_MSR_PMON_BOX_FILTER_OPC;

	return mask;
}

static struct event_constraint *
ivt_cbox_get_constraint(struct intel_uncore_box *box, struct perf_event *event)
{
	return __snbep_cbox_get_constraint(box, event, ivt_cbox_filter_mask);
}

static int ivt_cbox_hw_config(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event_extra *reg1 = &event->hw.extra_reg;
	struct extra_reg *er;
	int idx = 0;

	for (er = ivt_uncore_cbox_extra_regs; er->msr; er++) {
		if (er->event != (event->hw.config & er->config_mask))
			continue;
		idx |= er->idx;
	}

	if (idx) {
		reg1->reg = SNBEP_C0_MSR_PMON_BOX_FILTER +
			SNBEP_CBO_MSR_OFFSET * box->pmu->pmu_idx;
		reg1->config = event->attr.config1 & ivt_cbox_filter_mask(idx);
		reg1->idx = idx;
	}
	return 0;
}

static void ivt_cbox_enable_event(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	struct hw_perf_event_extra *reg1 = &hwc->extra_reg;

	if (reg1->idx != EXTRA_REG_NONE) {
		u64 filter = uncore_shared_reg_config(box, 0);
		wrmsrl(reg1->reg, filter & 0xffffffff);
		wrmsrl(reg1->reg + 6, filter >> 32);
	}

	wrmsrl(hwc->config_base, hwc->config | SNBEP_PMON_CTL_EN);
}

static struct intel_uncore_ops ivt_uncore_cbox_ops = {
	.init_box		= ivt_uncore_msr_init_box,
	.disable_box		= snbep_uncore_msr_disable_box,
	.enable_box		= snbep_uncore_msr_enable_box,
	.disable_event		= snbep_uncore_msr_disable_event,
	.enable_event		= ivt_cbox_enable_event,
	.read_counter		= uncore_msr_read_counter,
	.hw_config		= ivt_cbox_hw_config,
	.get_constraint		= ivt_cbox_get_constraint,
	.put_constraint		= snbep_cbox_put_constraint,
};

static struct intel_uncore_type ivt_uncore_cbox = {
	.name			= "cbox",
	.num_counters		= 4,
	.num_boxes		= 15,
	.perf_ctr_bits		= 44,
	.event_ctl		= SNBEP_C0_MSR_PMON_CTL0,
	.perf_ctr		= SNBEP_C0_MSR_PMON_CTR0,
	.event_mask		= IVT_CBO_MSR_PMON_RAW_EVENT_MASK,
	.box_ctl		= SNBEP_C0_MSR_PMON_BOX_CTL,
	.msr_offset		= SNBEP_CBO_MSR_OFFSET,
	.num_shared_regs	= 1,
	.constraints		= snbep_uncore_cbox_constraints,
	.ops			= &ivt_uncore_cbox_ops,
	.format_group		= &ivt_uncore_cbox_format_group,
};

static struct intel_uncore_ops ivt_uncore_pcu_ops = {
	IVT_UNCORE_MSR_OPS_COMMON_INIT(),
	.hw_config		= snbep_pcu_hw_config,
	.get_constraint		= snbep_pcu_get_constraint,
	.put_constraint		= snbep_pcu_put_constraint,
};

static struct intel_uncore_type ivt_uncore_pcu = {
	.name			= "pcu",
	.num_counters		= 4,
	.num_boxes		= 1,
	.perf_ctr_bits		= 48,
	.perf_ctr		= SNBEP_PCU_MSR_PMON_CTR0,
	.event_ctl		= SNBEP_PCU_MSR_PMON_CTL0,
	.event_mask		= IVT_PCU_MSR_PMON_RAW_EVENT_MASK,
	.box_ctl		= SNBEP_PCU_MSR_PMON_BOX_CTL,
	.num_shared_regs	= 1,
	.ops			= &ivt_uncore_pcu_ops,
	.format_group		= &ivt_uncore_pcu_format_group,
};

static struct intel_uncore_type *ivt_msr_uncores[] = {
	&ivt_uncore_ubox,
	&ivt_uncore_cbox,
	&ivt_uncore_pcu,
	NULL,
};

static struct intel_uncore_type ivt_uncore_ha = {
	.name		= "ha",
	.num_counters   = 4,
	.num_boxes	= 2,
	.perf_ctr_bits	= 48,
	IVT_UNCORE_PCI_COMMON_INIT(),
};

static struct intel_uncore_type ivt_uncore_imc = {
	.name		= "imc",
	.num_counters   = 4,
	.num_boxes	= 8,
	.perf_ctr_bits	= 48,
	.fixed_ctr_bits	= 48,
	.fixed_ctr	= SNBEP_MC_CHy_PCI_PMON_FIXED_CTR,
	.fixed_ctl	= SNBEP_MC_CHy_PCI_PMON_FIXED_CTL,
	IVT_UNCORE_PCI_COMMON_INIT(),
};

/* registers in IRP boxes are not properly aligned */
static unsigned ivt_uncore_irp_ctls[] = {0xd8, 0xdc, 0xe0, 0xe4};
static unsigned ivt_uncore_irp_ctrs[] = {0xa0, 0xb0, 0xb8, 0xc0};

static void ivt_uncore_irp_enable_event(struct intel_uncore_box *box, struct perf_event *event)
{
	struct pci_dev *pdev = box->pci_dev;
	struct hw_perf_event *hwc = &event->hw;

	pci_write_config_dword(pdev, ivt_uncore_irp_ctls[hwc->idx],
			       hwc->config | SNBEP_PMON_CTL_EN);
}

static void ivt_uncore_irp_disable_event(struct intel_uncore_box *box, struct perf_event *event)
{
	struct pci_dev *pdev = box->pci_dev;
	struct hw_perf_event *hwc = &event->hw;

	pci_write_config_dword(pdev, ivt_uncore_irp_ctls[hwc->idx], hwc->config);
}

static u64 ivt_uncore_irp_read_counter(struct intel_uncore_box *box, struct perf_event *event)
{
	struct pci_dev *pdev = box->pci_dev;
	struct hw_perf_event *hwc = &event->hw;
	u64 count = 0;

	pci_read_config_dword(pdev, ivt_uncore_irp_ctrs[hwc->idx], (u32 *)&count);
	pci_read_config_dword(pdev, ivt_uncore_irp_ctrs[hwc->idx] + 4, (u32 *)&count + 1);

	return count;
}

static struct intel_uncore_ops ivt_uncore_irp_ops = {
	.init_box	= ivt_uncore_pci_init_box,
	.disable_box	= snbep_uncore_pci_disable_box,
	.enable_box	= snbep_uncore_pci_enable_box,
	.disable_event	= ivt_uncore_irp_disable_event,
	.enable_event	= ivt_uncore_irp_enable_event,
	.read_counter	= ivt_uncore_irp_read_counter,
};

static struct intel_uncore_type ivt_uncore_irp = {
	.name			= "irp",
	.num_counters		= 4,
	.num_boxes		= 1,
	.perf_ctr_bits		= 48,
	.event_mask		= IVT_PMON_RAW_EVENT_MASK,
	.box_ctl		= SNBEP_PCI_PMON_BOX_CTL,
	.ops			= &ivt_uncore_irp_ops,
	.format_group		= &ivt_uncore_format_group,
};

static struct intel_uncore_ops ivt_uncore_qpi_ops = {
	.init_box	= ivt_uncore_pci_init_box,
	.disable_box	= snbep_uncore_pci_disable_box,
	.enable_box	= snbep_uncore_pci_enable_box,
	.disable_event	= snbep_uncore_pci_disable_event,
	.enable_event	= snbep_qpi_enable_event,
	.read_counter	= snbep_uncore_pci_read_counter,
	.hw_config	= snbep_qpi_hw_config,
	.get_constraint	= uncore_get_constraint,
	.put_constraint	= uncore_put_constraint,
};

static struct intel_uncore_type ivt_uncore_qpi = {
	.name			= "qpi",
	.num_counters		= 4,
	.num_boxes		= 3,
	.perf_ctr_bits		= 48,
	.perf_ctr		= SNBEP_PCI_PMON_CTR0,
	.event_ctl		= SNBEP_PCI_PMON_CTL0,
	.event_mask		= IVT_QPI_PCI_PMON_RAW_EVENT_MASK,
	.box_ctl		= SNBEP_PCI_PMON_BOX_CTL,
	.num_shared_regs	= 1,
	.ops			= &ivt_uncore_qpi_ops,
	.format_group		= &ivt_uncore_qpi_format_group,
};

static struct intel_uncore_type ivt_uncore_r2pcie = {
	.name		= "r2pcie",
	.num_counters   = 4,
	.num_boxes	= 1,
	.perf_ctr_bits	= 44,
	.constraints	= snbep_uncore_r2pcie_constraints,
	IVT_UNCORE_PCI_COMMON_INIT(),
};

static struct intel_uncore_type ivt_uncore_r3qpi = {
	.name		= "r3qpi",
	.num_counters   = 3,
	.num_boxes	= 2,
	.perf_ctr_bits	= 44,
	.constraints	= snbep_uncore_r3qpi_constraints,
	IVT_UNCORE_PCI_COMMON_INIT(),
};

enum {
	IVT_PCI_UNCORE_HA,
	IVT_PCI_UNCORE_IMC,
	IVT_PCI_UNCORE_IRP,
	IVT_PCI_UNCORE_QPI,
	IVT_PCI_UNCORE_R2PCIE,
	IVT_PCI_UNCORE_R3QPI,
};

static struct intel_uncore_type *ivt_pci_uncores[] = {
	[IVT_PCI_UNCORE_HA]	= &ivt_uncore_ha,
	[IVT_PCI_UNCORE_IMC]	= &ivt_uncore_imc,
	[IVT_PCI_UNCORE_IRP]	= &ivt_uncore_irp,
	[IVT_PCI_UNCORE_QPI]	= &ivt_uncore_qpi,
	[IVT_PCI_UNCORE_R2PCIE]	= &ivt_uncore_r2pcie,
	[IVT_PCI_UNCORE_R3QPI]	= &ivt_uncore_r3qpi,
	NULL,
};

static DEFINE_PCI_DEVICE_TABLE(ivt_uncore_pci_ids) = {
	{ /* Home Agent 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xe30),
		.driver_data = UNCORE_PCI_DEV_DATA(IVT_PCI_UNCORE_HA, 0),
	},
	{ /* Home Agent 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xe38),
		.driver_data = UNCORE_PCI_DEV_DATA(IVT_PCI_UNCORE_HA, 1),
	},
	{ /* MC0 Channel 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xeb4),
		.driver_data = UNCORE_PCI_DEV_DATA(IVT_PCI_UNCORE_IMC, 0),
	},
	{ /* MC0 Channel 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xeb5),
		.driver_data = UNCORE_PCI_DEV_DATA(IVT_PCI_UNCORE_IMC, 1),
	},
	{ /* MC0 Channel 3 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xeb0),
		.driver_data = UNCORE_PCI_DEV_DATA(IVT_PCI_UNCORE_IMC, 2),
	},
	{ /* MC0 Channel 4 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xeb1),
		.driver_data = UNCORE_PCI_DEV_DATA(IVT_PCI_UNCORE_IMC, 3),
	},
	{ /* MC1 Channel 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xef4),
		.driver_data = UNCORE_PCI_DEV_DATA(IVT_PCI_UNCORE_IMC, 4),
	},
	{ /* MC1 Channel 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xef5),
		.driver_data = UNCORE_PCI_DEV_DATA(IVT_PCI_UNCORE_IMC, 5),
	},
	{ /* MC1 Channel 3 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xef0),
		.driver_data = UNCORE_PCI_DEV_DATA(IVT_PCI_UNCORE_IMC, 6),
	},
	{ /* MC1 Channel 4 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xef1),
		.driver_data = UNCORE_PCI_DEV_DATA(IVT_PCI_UNCORE_IMC, 7),
	},
	{ /* IRP */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xe39),
		.driver_data = UNCORE_PCI_DEV_DATA(IVT_PCI_UNCORE_IRP, 0),
	},
	{ /* QPI0 Port 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xe32),
		.driver_data = UNCORE_PCI_DEV_DATA(IVT_PCI_UNCORE_QPI, 0),
	},
	{ /* QPI0 Port 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xe33),
		.driver_data = UNCORE_PCI_DEV_DATA(IVT_PCI_UNCORE_QPI, 1),
	},
	{ /* QPI1 Port 2 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xe3a),
		.driver_data = UNCORE_PCI_DEV_DATA(IVT_PCI_UNCORE_QPI, 2),
	},
	{ /* R2PCIe */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xe34),
		.driver_data = UNCORE_PCI_DEV_DATA(IVT_PCI_UNCORE_R2PCIE, 0),
	},
	{ /* R3QPI0 Link 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xe36),
		.driver_data = UNCORE_PCI_DEV_DATA(IVT_PCI_UNCORE_R3QPI, 0),
	},
	{ /* R3QPI0 Link 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xe37),
		.driver_data = UNCORE_PCI_DEV_DATA(IVT_PCI_UNCORE_R3QPI, 1),
	},
	{ /* R3QPI1 Link 2 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xe3e),
		.driver_data = UNCORE_PCI_DEV_DATA(IVT_PCI_UNCORE_R3QPI, 2),
	},
	{ /* QPI Port 0 filter  */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xe86),
		.driver_data = UNCORE_PCI_DEV_DATA(UNCORE_EXTRA_PCI_DEV,
						   SNBEP_PCI_QPI_PORT0_FILTER),
	},
	{ /* QPI Port 0 filter  */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xe96),
		.driver_data = UNCORE_PCI_DEV_DATA(UNCORE_EXTRA_PCI_DEV,
						   SNBEP_PCI_QPI_PORT1_FILTER),
	},
	{ /* end: all zeroes */ }
};

static struct pci_driver ivt_uncore_pci_driver = {
	.name		= "ivt_uncore",
	.id_table	= ivt_uncore_pci_ids,
};
/* end of IvyTown uncore support */

/* Sandy Bridge uncore support */
static void snb_uncore_msr_enable_event(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;

	if (hwc->idx < UNCORE_PMC_IDX_FIXED)
		wrmsrl(hwc->config_base, hwc->config | SNB_UNC_CTL_EN);
	else
		wrmsrl(hwc->config_base, SNB_UNC_CTL_EN);
}

static void snb_uncore_msr_disable_event(struct intel_uncore_box *box, struct perf_event *event)
{
	wrmsrl(event->hw.config_base, 0);
}

static void snb_uncore_msr_init_box(struct intel_uncore_box *box)
{
	if (box->pmu->pmu_idx == 0) {
		wrmsrl(SNB_UNC_PERF_GLOBAL_CTL,
			SNB_UNC_GLOBAL_CTL_EN | SNB_UNC_GLOBAL_CTL_CORE_ALL);
	}
}

static struct uncore_event_desc snb_uncore_events[] = {
	INTEL_UNCORE_EVENT_DESC(clockticks, "event=0xff,umask=0x00"),
	{ /* end: all zeroes */ },
};

static struct attribute *snb_uncore_formats_attr[] = {
	&format_attr_event.attr,
	&format_attr_umask.attr,
	&format_attr_edge.attr,
	&format_attr_inv.attr,
	&format_attr_cmask5.attr,
	NULL,
};

static struct attribute_group snb_uncore_format_group = {
	.name		= "format",
	.attrs		= snb_uncore_formats_attr,
};

static struct intel_uncore_ops snb_uncore_msr_ops = {
	.init_box	= snb_uncore_msr_init_box,
	.disable_event	= snb_uncore_msr_disable_event,
	.enable_event	= snb_uncore_msr_enable_event,
	.read_counter	= uncore_msr_read_counter,
};

static struct event_constraint snb_uncore_cbox_constraints[] = {
	UNCORE_EVENT_CONSTRAINT(0x80, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x83, 0x1),
	EVENT_CONSTRAINT_END
};

static struct intel_uncore_type snb_uncore_cbox = {
	.name		= "cbox",
	.num_counters   = 2,
	.num_boxes	= 4,
	.perf_ctr_bits	= 44,
	.fixed_ctr_bits	= 48,
	.perf_ctr	= SNB_UNC_CBO_0_PER_CTR0,
	.event_ctl	= SNB_UNC_CBO_0_PERFEVTSEL0,
	.fixed_ctr	= SNB_UNC_FIXED_CTR,
	.fixed_ctl	= SNB_UNC_FIXED_CTR_CTRL,
	.single_fixed	= 1,
	.event_mask	= SNB_UNC_RAW_EVENT_MASK,
	.msr_offset	= SNB_UNC_CBO_MSR_OFFSET,
	.constraints	= snb_uncore_cbox_constraints,
	.ops		= &snb_uncore_msr_ops,
	.format_group	= &snb_uncore_format_group,
	.event_descs	= snb_uncore_events,
};

static struct intel_uncore_type *snb_msr_uncores[] = {
	&snb_uncore_cbox,
	NULL,
};
/* end of Sandy Bridge uncore support */

/* Nehalem uncore support */
static void nhm_uncore_msr_disable_box(struct intel_uncore_box *box)
{
	wrmsrl(NHM_UNC_PERF_GLOBAL_CTL, 0);
}

static void nhm_uncore_msr_enable_box(struct intel_uncore_box *box)
{
	wrmsrl(NHM_UNC_PERF_GLOBAL_CTL, NHM_UNC_GLOBAL_CTL_EN_PC_ALL | NHM_UNC_GLOBAL_CTL_EN_FC);
}

static void nhm_uncore_msr_enable_event(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;

	if (hwc->idx < UNCORE_PMC_IDX_FIXED)
		wrmsrl(hwc->config_base, hwc->config | SNB_UNC_CTL_EN);
	else
		wrmsrl(hwc->config_base, NHM_UNC_FIXED_CTR_CTL_EN);
}

static struct attribute *nhm_uncore_formats_attr[] = {
	&format_attr_event.attr,
	&format_attr_umask.attr,
	&format_attr_edge.attr,
	&format_attr_inv.attr,
	&format_attr_cmask8.attr,
	NULL,
};

static struct attribute_group nhm_uncore_format_group = {
	.name = "format",
	.attrs = nhm_uncore_formats_attr,
};

static struct uncore_event_desc nhm_uncore_events[] = {
	INTEL_UNCORE_EVENT_DESC(clockticks,                "event=0xff,umask=0x00"),
	INTEL_UNCORE_EVENT_DESC(qmc_writes_full_any,       "event=0x2f,umask=0x0f"),
	INTEL_UNCORE_EVENT_DESC(qmc_normal_reads_any,      "event=0x2c,umask=0x0f"),
	INTEL_UNCORE_EVENT_DESC(qhl_request_ioh_reads,     "event=0x20,umask=0x01"),
	INTEL_UNCORE_EVENT_DESC(qhl_request_ioh_writes,    "event=0x20,umask=0x02"),
	INTEL_UNCORE_EVENT_DESC(qhl_request_remote_reads,  "event=0x20,umask=0x04"),
	INTEL_UNCORE_EVENT_DESC(qhl_request_remote_writes, "event=0x20,umask=0x08"),
	INTEL_UNCORE_EVENT_DESC(qhl_request_local_reads,   "event=0x20,umask=0x10"),
	INTEL_UNCORE_EVENT_DESC(qhl_request_local_writes,  "event=0x20,umask=0x20"),
	{ /* end: all zeroes */ },
};

static struct intel_uncore_ops nhm_uncore_msr_ops = {
	.disable_box	= nhm_uncore_msr_disable_box,
	.enable_box	= nhm_uncore_msr_enable_box,
	.disable_event	= snb_uncore_msr_disable_event,
	.enable_event	= nhm_uncore_msr_enable_event,
	.read_counter	= uncore_msr_read_counter,
};

static struct intel_uncore_type nhm_uncore = {
	.name		= "",
	.num_counters   = 8,
	.num_boxes	= 1,
	.perf_ctr_bits	= 48,
	.fixed_ctr_bits	= 48,
	.event_ctl	= NHM_UNC_PERFEVTSEL0,
	.perf_ctr	= NHM_UNC_UNCORE_PMC0,
	.fixed_ctr	= NHM_UNC_FIXED_CTR,
	.fixed_ctl	= NHM_UNC_FIXED_CTR_CTRL,
	.event_mask	= NHM_UNC_RAW_EVENT_MASK,
	.event_descs	= nhm_uncore_events,
	.ops		= &nhm_uncore_msr_ops,
	.format_group	= &nhm_uncore_format_group,
};

static struct intel_uncore_type *nhm_msr_uncores[] = {
	&nhm_uncore,
	NULL,
};
/* end of Nehalem uncore support */

/* Nehalem-EX uncore support */
DEFINE_UNCORE_FORMAT_ATTR(event5, event, "config:1-5");
DEFINE_UNCORE_FORMAT_ATTR(counter, counter, "config:6-7");
DEFINE_UNCORE_FORMAT_ATTR(match, match, "config1:0-63");
DEFINE_UNCORE_FORMAT_ATTR(mask, mask, "config2:0-63");

static void nhmex_uncore_msr_init_box(struct intel_uncore_box *box)
{
	wrmsrl(NHMEX_U_MSR_PMON_GLOBAL_CTL, NHMEX_U_PMON_GLOBAL_EN_ALL);
}

static void nhmex_uncore_msr_disable_box(struct intel_uncore_box *box)
{
	unsigned msr = uncore_msr_box_ctl(box);
	u64 config;

	if (msr) {
		rdmsrl(msr, config);
		config &= ~((1ULL << uncore_num_counters(box)) - 1);
		/* WBox has a fixed counter */
		if (uncore_msr_fixed_ctl(box))
			config &= ~NHMEX_W_PMON_GLOBAL_FIXED_EN;
		wrmsrl(msr, config);
	}
}

static void nhmex_uncore_msr_enable_box(struct intel_uncore_box *box)
{
	unsigned msr = uncore_msr_box_ctl(box);
	u64 config;

	if (msr) {
		rdmsrl(msr, config);
		config |= (1ULL << uncore_num_counters(box)) - 1;
		/* WBox has a fixed counter */
		if (uncore_msr_fixed_ctl(box))
			config |= NHMEX_W_PMON_GLOBAL_FIXED_EN;
		wrmsrl(msr, config);
	}
}

static void nhmex_uncore_msr_disable_event(struct intel_uncore_box *box, struct perf_event *event)
{
	wrmsrl(event->hw.config_base, 0);
}

static void nhmex_uncore_msr_enable_event(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;

	if (hwc->idx >= UNCORE_PMC_IDX_FIXED)
		wrmsrl(hwc->config_base, NHMEX_PMON_CTL_EN_BIT0);
	else if (box->pmu->type->event_mask & NHMEX_PMON_CTL_EN_BIT0)
		wrmsrl(hwc->config_base, hwc->config | NHMEX_PMON_CTL_EN_BIT22);
	else
		wrmsrl(hwc->config_base, hwc->config | NHMEX_PMON_CTL_EN_BIT0);
}

#define NHMEX_UNCORE_OPS_COMMON_INIT()				\
	.init_box	= nhmex_uncore_msr_init_box,		\
	.disable_box	= nhmex_uncore_msr_disable_box,		\
	.enable_box	= nhmex_uncore_msr_enable_box,		\
	.disable_event	= nhmex_uncore_msr_disable_event,	\
	.read_counter	= uncore_msr_read_counter

static struct intel_uncore_ops nhmex_uncore_ops = {
	NHMEX_UNCORE_OPS_COMMON_INIT(),
	.enable_event	= nhmex_uncore_msr_enable_event,
};

static struct attribute *nhmex_uncore_ubox_formats_attr[] = {
	&format_attr_event.attr,
	&format_attr_edge.attr,
	NULL,
};

static struct attribute_group nhmex_uncore_ubox_format_group = {
	.name		= "format",
	.attrs		= nhmex_uncore_ubox_formats_attr,
};

static struct intel_uncore_type nhmex_uncore_ubox = {
	.name		= "ubox",
	.num_counters	= 1,
	.num_boxes	= 1,
	.perf_ctr_bits	= 48,
	.event_ctl	= NHMEX_U_MSR_PMON_EV_SEL,
	.perf_ctr	= NHMEX_U_MSR_PMON_CTR,
	.event_mask	= NHMEX_U_PMON_RAW_EVENT_MASK,
	.box_ctl	= NHMEX_U_MSR_PMON_GLOBAL_CTL,
	.ops		= &nhmex_uncore_ops,
	.format_group	= &nhmex_uncore_ubox_format_group
};

static struct attribute *nhmex_uncore_cbox_formats_attr[] = {
	&format_attr_event.attr,
	&format_attr_umask.attr,
	&format_attr_edge.attr,
	&format_attr_inv.attr,
	&format_attr_thresh8.attr,
	NULL,
};

static struct attribute_group nhmex_uncore_cbox_format_group = {
	.name = "format",
	.attrs = nhmex_uncore_cbox_formats_attr,
};

/* msr offset for each instance of cbox */
static unsigned nhmex_cbox_msr_offsets[] = {
	0x0, 0x80, 0x40, 0xc0, 0x20, 0xa0, 0x60, 0xe0, 0x240, 0x2c0,
};

static struct intel_uncore_type nhmex_uncore_cbox = {
	.name			= "cbox",
	.num_counters		= 6,
	.num_boxes		= 10,
	.perf_ctr_bits		= 48,
	.event_ctl		= NHMEX_C0_MSR_PMON_EV_SEL0,
	.perf_ctr		= NHMEX_C0_MSR_PMON_CTR0,
	.event_mask		= NHMEX_PMON_RAW_EVENT_MASK,
	.box_ctl		= NHMEX_C0_MSR_PMON_GLOBAL_CTL,
	.msr_offsets		= nhmex_cbox_msr_offsets,
	.pair_ctr_ctl		= 1,
	.ops			= &nhmex_uncore_ops,
	.format_group		= &nhmex_uncore_cbox_format_group
};

static struct uncore_event_desc nhmex_uncore_wbox_events[] = {
	INTEL_UNCORE_EVENT_DESC(clockticks, "event=0xff,umask=0"),
	{ /* end: all zeroes */ },
};

static struct intel_uncore_type nhmex_uncore_wbox = {
	.name			= "wbox",
	.num_counters		= 4,
	.num_boxes		= 1,
	.perf_ctr_bits		= 48,
	.event_ctl		= NHMEX_W_MSR_PMON_CNT0,
	.perf_ctr		= NHMEX_W_MSR_PMON_EVT_SEL0,
	.fixed_ctr		= NHMEX_W_MSR_PMON_FIXED_CTR,
	.fixed_ctl		= NHMEX_W_MSR_PMON_FIXED_CTL,
	.event_mask		= NHMEX_PMON_RAW_EVENT_MASK,
	.box_ctl		= NHMEX_W_MSR_GLOBAL_CTL,
	.pair_ctr_ctl		= 1,
	.event_descs		= nhmex_uncore_wbox_events,
	.ops			= &nhmex_uncore_ops,
	.format_group		= &nhmex_uncore_cbox_format_group
};

static int nhmex_bbox_hw_config(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	struct hw_perf_event_extra *reg1 = &hwc->extra_reg;
	struct hw_perf_event_extra *reg2 = &hwc->branch_reg;
	int ctr, ev_sel;

	ctr = (hwc->config & NHMEX_B_PMON_CTR_MASK) >>
		NHMEX_B_PMON_CTR_SHIFT;
	ev_sel = (hwc->config & NHMEX_B_PMON_CTL_EV_SEL_MASK) >>
		  NHMEX_B_PMON_CTL_EV_SEL_SHIFT;

	/* events that do not use the match/mask registers */
	if ((ctr == 0 && ev_sel > 0x3) || (ctr == 1 && ev_sel > 0x6) ||
	    (ctr == 2 && ev_sel != 0x4) || ctr == 3)
		return 0;

	if (box->pmu->pmu_idx == 0)
		reg1->reg = NHMEX_B0_MSR_MATCH;
	else
		reg1->reg = NHMEX_B1_MSR_MATCH;
	reg1->idx = 0;
	reg1->config = event->attr.config1;
	reg2->config = event->attr.config2;
	return 0;
}

static void nhmex_bbox_msr_enable_event(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	struct hw_perf_event_extra *reg1 = &hwc->extra_reg;
	struct hw_perf_event_extra *reg2 = &hwc->branch_reg;

	if (reg1->idx != EXTRA_REG_NONE) {
		wrmsrl(reg1->reg, reg1->config);
		wrmsrl(reg1->reg + 1, reg2->config);
	}
	wrmsrl(hwc->config_base, NHMEX_PMON_CTL_EN_BIT0 |
		(hwc->config & NHMEX_B_PMON_CTL_EV_SEL_MASK));
}

/*
 * The Bbox has 4 counters, but each counter monitors different events.
 * Use bits 6-7 in the event config to select counter.
 */
static struct event_constraint nhmex_uncore_bbox_constraints[] = {
	EVENT_CONSTRAINT(0 , 1, 0xc0),
	EVENT_CONSTRAINT(0x40, 2, 0xc0),
	EVENT_CONSTRAINT(0x80, 4, 0xc0),
	EVENT_CONSTRAINT(0xc0, 8, 0xc0),
	EVENT_CONSTRAINT_END,
};

static struct attribute *nhmex_uncore_bbox_formats_attr[] = {
	&format_attr_event5.attr,
	&format_attr_counter.attr,
	&format_attr_match.attr,
	&format_attr_mask.attr,
	NULL,
};

static struct attribute_group nhmex_uncore_bbox_format_group = {
	.name = "format",
	.attrs = nhmex_uncore_bbox_formats_attr,
};

static struct intel_uncore_ops nhmex_uncore_bbox_ops = {
	NHMEX_UNCORE_OPS_COMMON_INIT(),
	.enable_event		= nhmex_bbox_msr_enable_event,
	.hw_config		= nhmex_bbox_hw_config,
	.get_constraint		= uncore_get_constraint,
	.put_constraint		= uncore_put_constraint,
};

static struct intel_uncore_type nhmex_uncore_bbox = {
	.name			= "bbox",
	.num_counters		= 4,
	.num_boxes		= 2,
	.perf_ctr_bits		= 48,
	.event_ctl		= NHMEX_B0_MSR_PMON_CTL0,
	.perf_ctr		= NHMEX_B0_MSR_PMON_CTR0,
	.event_mask		= NHMEX_B_PMON_RAW_EVENT_MASK,
	.box_ctl		= NHMEX_B0_MSR_PMON_GLOBAL_CTL,
	.msr_offset		= NHMEX_B_MSR_OFFSET,
	.pair_ctr_ctl		= 1,
	.num_shared_regs	= 1,
	.constraints		= nhmex_uncore_bbox_constraints,
	.ops			= &nhmex_uncore_bbox_ops,
	.format_group		= &nhmex_uncore_bbox_format_group
};

static int nhmex_sbox_hw_config(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	struct hw_perf_event_extra *reg1 = &hwc->extra_reg;
	struct hw_perf_event_extra *reg2 = &hwc->branch_reg;

	/* only TO_R_PROG_EV event uses the match/mask register */
	if ((hwc->config & NHMEX_PMON_CTL_EV_SEL_MASK) !=
	    NHMEX_S_EVENT_TO_R_PROG_EV)
		return 0;

	if (box->pmu->pmu_idx == 0)
		reg1->reg = NHMEX_S0_MSR_MM_CFG;
	else
		reg1->reg = NHMEX_S1_MSR_MM_CFG;
	reg1->idx = 0;
	reg1->config = event->attr.config1;
	reg2->config = event->attr.config2;
	return 0;
}

static void nhmex_sbox_msr_enable_event(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	struct hw_perf_event_extra *reg1 = &hwc->extra_reg;
	struct hw_perf_event_extra *reg2 = &hwc->branch_reg;

	if (reg1->idx != EXTRA_REG_NONE) {
		wrmsrl(reg1->reg, 0);
		wrmsrl(reg1->reg + 1, reg1->config);
		wrmsrl(reg1->reg + 2, reg2->config);
		wrmsrl(reg1->reg, NHMEX_S_PMON_MM_CFG_EN);
	}
	wrmsrl(hwc->config_base, hwc->config | NHMEX_PMON_CTL_EN_BIT22);
}

static struct attribute *nhmex_uncore_sbox_formats_attr[] = {
	&format_attr_event.attr,
	&format_attr_umask.attr,
	&format_attr_edge.attr,
	&format_attr_inv.attr,
	&format_attr_thresh8.attr,
	&format_attr_match.attr,
	&format_attr_mask.attr,
	NULL,
};

static struct attribute_group nhmex_uncore_sbox_format_group = {
	.name			= "format",
	.attrs			= nhmex_uncore_sbox_formats_attr,
};

static struct intel_uncore_ops nhmex_uncore_sbox_ops = {
	NHMEX_UNCORE_OPS_COMMON_INIT(),
	.enable_event		= nhmex_sbox_msr_enable_event,
	.hw_config		= nhmex_sbox_hw_config,
	.get_constraint		= uncore_get_constraint,
	.put_constraint		= uncore_put_constraint,
};

static struct intel_uncore_type nhmex_uncore_sbox = {
	.name			= "sbox",
	.num_counters		= 4,
	.num_boxes		= 2,
	.perf_ctr_bits		= 48,
	.event_ctl		= NHMEX_S0_MSR_PMON_CTL0,
	.perf_ctr		= NHMEX_S0_MSR_PMON_CTR0,
	.event_mask		= NHMEX_PMON_RAW_EVENT_MASK,
	.box_ctl		= NHMEX_S0_MSR_PMON_GLOBAL_CTL,
	.msr_offset		= NHMEX_S_MSR_OFFSET,
	.pair_ctr_ctl		= 1,
	.num_shared_regs	= 1,
	.ops			= &nhmex_uncore_sbox_ops,
	.format_group		= &nhmex_uncore_sbox_format_group
};

enum {
	EXTRA_REG_NHMEX_M_FILTER,
	EXTRA_REG_NHMEX_M_DSP,
	EXTRA_REG_NHMEX_M_ISS,
	EXTRA_REG_NHMEX_M_MAP,
	EXTRA_REG_NHMEX_M_MSC_THR,
	EXTRA_REG_NHMEX_M_PGT,
	EXTRA_REG_NHMEX_M_PLD,
	EXTRA_REG_NHMEX_M_ZDP_CTL_FVC,
};

static struct extra_reg nhmex_uncore_mbox_extra_regs[] = {
	MBOX_INC_SEL_EXTAR_REG(0x0, DSP),
	MBOX_INC_SEL_EXTAR_REG(0x4, MSC_THR),
	MBOX_INC_SEL_EXTAR_REG(0x5, MSC_THR),
	MBOX_INC_SEL_EXTAR_REG(0x9, ISS),
	/* event 0xa uses two extra registers */
	MBOX_INC_SEL_EXTAR_REG(0xa, ISS),
	MBOX_INC_SEL_EXTAR_REG(0xa, PLD),
	MBOX_INC_SEL_EXTAR_REG(0xb, PLD),
	/* events 0xd ~ 0x10 use the same extra register */
	MBOX_INC_SEL_EXTAR_REG(0xd, ZDP_CTL_FVC),
	MBOX_INC_SEL_EXTAR_REG(0xe, ZDP_CTL_FVC),
	MBOX_INC_SEL_EXTAR_REG(0xf, ZDP_CTL_FVC),
	MBOX_INC_SEL_EXTAR_REG(0x10, ZDP_CTL_FVC),
	MBOX_INC_SEL_EXTAR_REG(0x16, PGT),
	MBOX_SET_FLAG_SEL_EXTRA_REG(0x0, DSP),
	MBOX_SET_FLAG_SEL_EXTRA_REG(0x1, ISS),
	MBOX_SET_FLAG_SEL_EXTRA_REG(0x5, PGT),
	MBOX_SET_FLAG_SEL_EXTRA_REG(0x6, MAP),
	EVENT_EXTRA_END
};

/* Nehalem-EX or Westmere-EX ? */
static bool uncore_nhmex;

static bool nhmex_mbox_get_shared_reg(struct intel_uncore_box *box, int idx, u64 config)
{
	struct intel_uncore_extra_reg *er;
	unsigned long flags;
	bool ret = false;
	u64 mask;

	if (idx < EXTRA_REG_NHMEX_M_ZDP_CTL_FVC) {
		er = &box->shared_regs[idx];
		raw_spin_lock_irqsave(&er->lock, flags);
		if (!atomic_read(&er->ref) || er->config == config) {
			atomic_inc(&er->ref);
			er->config = config;
			ret = true;
		}
		raw_spin_unlock_irqrestore(&er->lock, flags);

		return ret;
	}
	/*
	 * The ZDP_CTL_FVC MSR has 4 fields which are used to control
	 * events 0xd ~ 0x10. Besides these 4 fields, there are additional
	 * fields which are shared.
	 */
	idx -= EXTRA_REG_NHMEX_M_ZDP_CTL_FVC;
	if (WARN_ON_ONCE(idx >= 4))
		return false;

	/* mask of the shared fields */
	if (uncore_nhmex)
		mask = NHMEX_M_PMON_ZDP_CTL_FVC_MASK;
	else
		mask = WSMEX_M_PMON_ZDP_CTL_FVC_MASK;
	er = &box->shared_regs[EXTRA_REG_NHMEX_M_ZDP_CTL_FVC];

	raw_spin_lock_irqsave(&er->lock, flags);
	/* add mask of the non-shared field if it's in use */
	if (__BITS_VALUE(atomic_read(&er->ref), idx, 8)) {
		if (uncore_nhmex)
			mask |= NHMEX_M_PMON_ZDP_CTL_FVC_EVENT_MASK(idx);
		else
			mask |= WSMEX_M_PMON_ZDP_CTL_FVC_EVENT_MASK(idx);
	}

	if (!atomic_read(&er->ref) || !((er->config ^ config) & mask)) {
		atomic_add(1 << (idx * 8), &er->ref);
		if (uncore_nhmex)
			mask = NHMEX_M_PMON_ZDP_CTL_FVC_MASK |
				NHMEX_M_PMON_ZDP_CTL_FVC_EVENT_MASK(idx);
		else
			mask = WSMEX_M_PMON_ZDP_CTL_FVC_MASK |
				WSMEX_M_PMON_ZDP_CTL_FVC_EVENT_MASK(idx);
		er->config &= ~mask;
		er->config |= (config & mask);
		ret = true;
	}
	raw_spin_unlock_irqrestore(&er->lock, flags);

	return ret;
}

static void nhmex_mbox_put_shared_reg(struct intel_uncore_box *box, int idx)
{
	struct intel_uncore_extra_reg *er;

	if (idx < EXTRA_REG_NHMEX_M_ZDP_CTL_FVC) {
		er = &box->shared_regs[idx];
		atomic_dec(&er->ref);
		return;
	}

	idx -= EXTRA_REG_NHMEX_M_ZDP_CTL_FVC;
	er = &box->shared_regs[EXTRA_REG_NHMEX_M_ZDP_CTL_FVC];
	atomic_sub(1 << (idx * 8), &er->ref);
}

static u64 nhmex_mbox_alter_er(struct perf_event *event, int new_idx, bool modify)
{
	struct hw_perf_event *hwc = &event->hw;
	struct hw_perf_event_extra *reg1 = &hwc->extra_reg;
	u64 idx, orig_idx = __BITS_VALUE(reg1->idx, 0, 8);
	u64 config = reg1->config;

	/* get the non-shared control bits and shift them */
	idx = orig_idx - EXTRA_REG_NHMEX_M_ZDP_CTL_FVC;
	if (uncore_nhmex)
		config &= NHMEX_M_PMON_ZDP_CTL_FVC_EVENT_MASK(idx);
	else
		config &= WSMEX_M_PMON_ZDP_CTL_FVC_EVENT_MASK(idx);
	if (new_idx > orig_idx) {
		idx = new_idx - orig_idx;
		config <<= 3 * idx;
	} else {
		idx = orig_idx - new_idx;
		config >>= 3 * idx;
	}

	/* add the shared control bits back */
	if (uncore_nhmex)
		config |= NHMEX_M_PMON_ZDP_CTL_FVC_MASK & reg1->config;
	else
		config |= WSMEX_M_PMON_ZDP_CTL_FVC_MASK & reg1->config;
	config |= NHMEX_M_PMON_ZDP_CTL_FVC_MASK & reg1->config;
	if (modify) {
		/* adjust the main event selector */
		if (new_idx > orig_idx)
			hwc->config += idx << NHMEX_M_PMON_CTL_INC_SEL_SHIFT;
		else
			hwc->config -= idx << NHMEX_M_PMON_CTL_INC_SEL_SHIFT;
		reg1->config = config;
		reg1->idx = ~0xff | new_idx;
	}
	return config;
}

static struct event_constraint *
nhmex_mbox_get_constraint(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event_extra *reg1 = &event->hw.extra_reg;
	struct hw_perf_event_extra *reg2 = &event->hw.branch_reg;
	int i, idx[2], alloc = 0;
	u64 config1 = reg1->config;

	idx[0] = __BITS_VALUE(reg1->idx, 0, 8);
	idx[1] = __BITS_VALUE(reg1->idx, 1, 8);
again:
	for (i = 0; i < 2; i++) {
		if (!uncore_box_is_fake(box) && (reg1->alloc & (0x1 << i)))
			idx[i] = 0xff;

		if (idx[i] == 0xff)
			continue;

		if (!nhmex_mbox_get_shared_reg(box, idx[i],
				__BITS_VALUE(config1, i, 32)))
			goto fail;
		alloc |= (0x1 << i);
	}

	/* for the match/mask registers */
	if (reg2->idx != EXTRA_REG_NONE &&
	    (uncore_box_is_fake(box) || !reg2->alloc) &&
	    !nhmex_mbox_get_shared_reg(box, reg2->idx, reg2->config))
		goto fail;

	/*
	 * If it's a fake box -- as per validate_{group,event}() we
	 * shouldn't touch event state and we can avoid doing so
	 * since both will only call get_event_constraints() once
	 * on each event, this avoids the need for reg->alloc.
	 */
	if (!uncore_box_is_fake(box)) {
		if (idx[0] != 0xff && idx[0] != __BITS_VALUE(reg1->idx, 0, 8))
			nhmex_mbox_alter_er(event, idx[0], true);
		reg1->alloc |= alloc;
		if (reg2->idx != EXTRA_REG_NONE)
			reg2->alloc = 1;
	}
	return NULL;
fail:
	if (idx[0] != 0xff && !(alloc & 0x1) &&
	    idx[0] >= EXTRA_REG_NHMEX_M_ZDP_CTL_FVC) {
		/*
		 * events 0xd ~ 0x10 are functional identical, but are
		 * controlled by different fields in the ZDP_CTL_FVC
		 * register. If we failed to take one field, try the
		 * rest 3 choices.
		 */
		BUG_ON(__BITS_VALUE(reg1->idx, 1, 8) != 0xff);
		idx[0] -= EXTRA_REG_NHMEX_M_ZDP_CTL_FVC;
		idx[0] = (idx[0] + 1) % 4;
		idx[0] += EXTRA_REG_NHMEX_M_ZDP_CTL_FVC;
		if (idx[0] != __BITS_VALUE(reg1->idx, 0, 8)) {
			config1 = nhmex_mbox_alter_er(event, idx[0], false);
			goto again;
		}
	}

	if (alloc & 0x1)
		nhmex_mbox_put_shared_reg(box, idx[0]);
	if (alloc & 0x2)
		nhmex_mbox_put_shared_reg(box, idx[1]);
	return &constraint_empty;
}

static void nhmex_mbox_put_constraint(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event_extra *reg1 = &event->hw.extra_reg;
	struct hw_perf_event_extra *reg2 = &event->hw.branch_reg;

	if (uncore_box_is_fake(box))
		return;

	if (reg1->alloc & 0x1)
		nhmex_mbox_put_shared_reg(box, __BITS_VALUE(reg1->idx, 0, 8));
	if (reg1->alloc & 0x2)
		nhmex_mbox_put_shared_reg(box, __BITS_VALUE(reg1->idx, 1, 8));
	reg1->alloc = 0;

	if (reg2->alloc) {
		nhmex_mbox_put_shared_reg(box, reg2->idx);
		reg2->alloc = 0;
	}
}

static int nhmex_mbox_extra_reg_idx(struct extra_reg *er)
{
	if (er->idx < EXTRA_REG_NHMEX_M_ZDP_CTL_FVC)
		return er->idx;
	return er->idx + (er->event >> NHMEX_M_PMON_CTL_INC_SEL_SHIFT) - 0xd;
}

static int nhmex_mbox_hw_config(struct intel_uncore_box *box, struct perf_event *event)
{
	struct intel_uncore_type *type = box->pmu->type;
	struct hw_perf_event_extra *reg1 = &event->hw.extra_reg;
	struct hw_perf_event_extra *reg2 = &event->hw.branch_reg;
	struct extra_reg *er;
	unsigned msr;
	int reg_idx = 0;
	/*
	 * The mbox events may require 2 extra MSRs at the most. But only
	 * the lower 32 bits in these MSRs are significant, so we can use
	 * config1 to pass two MSRs' config.
	 */
	for (er = nhmex_uncore_mbox_extra_regs; er->msr; er++) {
		if (er->event != (event->hw.config & er->config_mask))
			continue;
		if (event->attr.config1 & ~er->valid_mask)
			return -EINVAL;

		msr = er->msr + type->msr_offset * box->pmu->pmu_idx;
		if (WARN_ON_ONCE(msr >= 0xffff || er->idx >= 0xff))
			return -EINVAL;

		/* always use the 32~63 bits to pass the PLD config */
		if (er->idx == EXTRA_REG_NHMEX_M_PLD)
			reg_idx = 1;
		else if (WARN_ON_ONCE(reg_idx > 0))
			return -EINVAL;

		reg1->idx &= ~(0xff << (reg_idx * 8));
		reg1->reg &= ~(0xffff << (reg_idx * 16));
		reg1->idx |= nhmex_mbox_extra_reg_idx(er) << (reg_idx * 8);
		reg1->reg |= msr << (reg_idx * 16);
		reg1->config = event->attr.config1;
		reg_idx++;
	}
	/*
	 * The mbox only provides ability to perform address matching
	 * for the PLD events.
	 */
	if (reg_idx == 2) {
		reg2->idx = EXTRA_REG_NHMEX_M_FILTER;
		if (event->attr.config2 & NHMEX_M_PMON_MM_CFG_EN)
			reg2->config = event->attr.config2;
		else
			reg2->config = ~0ULL;
		if (box->pmu->pmu_idx == 0)
			reg2->reg = NHMEX_M0_MSR_PMU_MM_CFG;
		else
			reg2->reg = NHMEX_M1_MSR_PMU_MM_CFG;
	}
	return 0;
}

static u64 nhmex_mbox_shared_reg_config(struct intel_uncore_box *box, int idx)
{
	struct intel_uncore_extra_reg *er;
	unsigned long flags;
	u64 config;

	if (idx < EXTRA_REG_NHMEX_M_ZDP_CTL_FVC)
		return box->shared_regs[idx].config;

	er = &box->shared_regs[EXTRA_REG_NHMEX_M_ZDP_CTL_FVC];
	raw_spin_lock_irqsave(&er->lock, flags);
	config = er->config;
	raw_spin_unlock_irqrestore(&er->lock, flags);
	return config;
}

static void nhmex_mbox_msr_enable_event(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	struct hw_perf_event_extra *reg1 = &hwc->extra_reg;
	struct hw_perf_event_extra *reg2 = &hwc->branch_reg;
	int idx;

	idx = __BITS_VALUE(reg1->idx, 0, 8);
	if (idx != 0xff)
		wrmsrl(__BITS_VALUE(reg1->reg, 0, 16),
			nhmex_mbox_shared_reg_config(box, idx));
	idx = __BITS_VALUE(reg1->idx, 1, 8);
	if (idx != 0xff)
		wrmsrl(__BITS_VALUE(reg1->reg, 1, 16),
			nhmex_mbox_shared_reg_config(box, idx));

	if (reg2->idx != EXTRA_REG_NONE) {
		wrmsrl(reg2->reg, 0);
		if (reg2->config != ~0ULL) {
			wrmsrl(reg2->reg + 1,
				reg2->config & NHMEX_M_PMON_ADDR_MATCH_MASK);
			wrmsrl(reg2->reg + 2, NHMEX_M_PMON_ADDR_MASK_MASK &
				(reg2->config >> NHMEX_M_PMON_ADDR_MASK_SHIFT));
			wrmsrl(reg2->reg, NHMEX_M_PMON_MM_CFG_EN);
		}
	}

	wrmsrl(hwc->config_base, hwc->config | NHMEX_PMON_CTL_EN_BIT0);
}

DEFINE_UNCORE_FORMAT_ATTR(count_mode,		count_mode,	"config:2-3");
DEFINE_UNCORE_FORMAT_ATTR(storage_mode,		storage_mode,	"config:4-5");
DEFINE_UNCORE_FORMAT_ATTR(wrap_mode,		wrap_mode,	"config:6");
DEFINE_UNCORE_FORMAT_ATTR(flag_mode,		flag_mode,	"config:7");
DEFINE_UNCORE_FORMAT_ATTR(inc_sel,		inc_sel,	"config:9-13");
DEFINE_UNCORE_FORMAT_ATTR(set_flag_sel,		set_flag_sel,	"config:19-21");
DEFINE_UNCORE_FORMAT_ATTR(filter_cfg_en,	filter_cfg_en,	"config2:63");
DEFINE_UNCORE_FORMAT_ATTR(filter_match,		filter_match,	"config2:0-33");
DEFINE_UNCORE_FORMAT_ATTR(filter_mask,		filter_mask,	"config2:34-61");
DEFINE_UNCORE_FORMAT_ATTR(dsp,			dsp,		"config1:0-31");
DEFINE_UNCORE_FORMAT_ATTR(thr,			thr,		"config1:0-31");
DEFINE_UNCORE_FORMAT_ATTR(fvc,			fvc,		"config1:0-31");
DEFINE_UNCORE_FORMAT_ATTR(pgt,			pgt,		"config1:0-31");
DEFINE_UNCORE_FORMAT_ATTR(map,			map,		"config1:0-31");
DEFINE_UNCORE_FORMAT_ATTR(iss,			iss,		"config1:0-31");
DEFINE_UNCORE_FORMAT_ATTR(pld,			pld,		"config1:32-63");

static struct attribute *nhmex_uncore_mbox_formats_attr[] = {
	&format_attr_count_mode.attr,
	&format_attr_storage_mode.attr,
	&format_attr_wrap_mode.attr,
	&format_attr_flag_mode.attr,
	&format_attr_inc_sel.attr,
	&format_attr_set_flag_sel.attr,
	&format_attr_filter_cfg_en.attr,
	&format_attr_filter_match.attr,
	&format_attr_filter_mask.attr,
	&format_attr_dsp.attr,
	&format_attr_thr.attr,
	&format_attr_fvc.attr,
	&format_attr_pgt.attr,
	&format_attr_map.attr,
	&format_attr_iss.attr,
	&format_attr_pld.attr,
	NULL,
};

static struct attribute_group nhmex_uncore_mbox_format_group = {
	.name		= "format",
	.attrs		= nhmex_uncore_mbox_formats_attr,
};

static struct uncore_event_desc nhmex_uncore_mbox_events[] = {
	INTEL_UNCORE_EVENT_DESC(bbox_cmds_read, "inc_sel=0xd,fvc=0x2800"),
	INTEL_UNCORE_EVENT_DESC(bbox_cmds_write, "inc_sel=0xd,fvc=0x2820"),
	{ /* end: all zeroes */ },
};

static struct uncore_event_desc wsmex_uncore_mbox_events[] = {
	INTEL_UNCORE_EVENT_DESC(bbox_cmds_read, "inc_sel=0xd,fvc=0x5000"),
	INTEL_UNCORE_EVENT_DESC(bbox_cmds_write, "inc_sel=0xd,fvc=0x5040"),
	{ /* end: all zeroes */ },
};

static struct intel_uncore_ops nhmex_uncore_mbox_ops = {
	NHMEX_UNCORE_OPS_COMMON_INIT(),
	.enable_event	= nhmex_mbox_msr_enable_event,
	.hw_config	= nhmex_mbox_hw_config,
	.get_constraint	= nhmex_mbox_get_constraint,
	.put_constraint	= nhmex_mbox_put_constraint,
};

static struct intel_uncore_type nhmex_uncore_mbox = {
	.name			= "mbox",
	.num_counters		= 6,
	.num_boxes		= 2,
	.perf_ctr_bits		= 48,
	.event_ctl		= NHMEX_M0_MSR_PMU_CTL0,
	.perf_ctr		= NHMEX_M0_MSR_PMU_CNT0,
	.event_mask		= NHMEX_M_PMON_RAW_EVENT_MASK,
	.box_ctl		= NHMEX_M0_MSR_GLOBAL_CTL,
	.msr_offset		= NHMEX_M_MSR_OFFSET,
	.pair_ctr_ctl		= 1,
	.num_shared_regs	= 8,
	.event_descs		= nhmex_uncore_mbox_events,
	.ops			= &nhmex_uncore_mbox_ops,
	.format_group		= &nhmex_uncore_mbox_format_group,
};

static void nhmex_rbox_alter_er(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	struct hw_perf_event_extra *reg1 = &hwc->extra_reg;

	/* adjust the main event selector and extra register index */
	if (reg1->idx % 2) {
		reg1->idx--;
		hwc->config -= 1 << NHMEX_R_PMON_CTL_EV_SEL_SHIFT;
	} else {
		reg1->idx++;
		hwc->config += 1 << NHMEX_R_PMON_CTL_EV_SEL_SHIFT;
	}

	/* adjust extra register config */
	switch (reg1->idx % 6) {
	case 2:
		/* shift the 8~15 bits to the 0~7 bits */
		reg1->config >>= 8;
		break;
	case 3:
		/* shift the 0~7 bits to the 8~15 bits */
		reg1->config <<= 8;
		break;
	};
}

/*
 * Each rbox has 4 event set which monitor PQI port 0~3 or 4~7.
 * An event set consists of 6 events, the 3rd and 4th events in
 * an event set use the same extra register. So an event set uses
 * 5 extra registers.
 */
static struct event_constraint *
nhmex_rbox_get_constraint(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	struct hw_perf_event_extra *reg1 = &hwc->extra_reg;
	struct hw_perf_event_extra *reg2 = &hwc->branch_reg;
	struct intel_uncore_extra_reg *er;
	unsigned long flags;
	int idx, er_idx;
	u64 config1;
	bool ok = false;

	if (!uncore_box_is_fake(box) && reg1->alloc)
		return NULL;

	idx = reg1->idx % 6;
	config1 = reg1->config;
again:
	er_idx = idx;
	/* the 3rd and 4th events use the same extra register */
	if (er_idx > 2)
		er_idx--;
	er_idx += (reg1->idx / 6) * 5;

	er = &box->shared_regs[er_idx];
	raw_spin_lock_irqsave(&er->lock, flags);
	if (idx < 2) {
		if (!atomic_read(&er->ref) || er->config == reg1->config) {
			atomic_inc(&er->ref);
			er->config = reg1->config;
			ok = true;
		}
	} else if (idx == 2 || idx == 3) {
		/*
		 * these two events use different fields in a extra register,
		 * the 0~7 bits and the 8~15 bits respectively.
		 */
		u64 mask = 0xff << ((idx - 2) * 8);
		if (!__BITS_VALUE(atomic_read(&er->ref), idx - 2, 8) ||
				!((er->config ^ config1) & mask)) {
			atomic_add(1 << ((idx - 2) * 8), &er->ref);
			er->config &= ~mask;
			er->config |= config1 & mask;
			ok = true;
		}
	} else {
		if (!atomic_read(&er->ref) ||
				(er->config == (hwc->config >> 32) &&
				 er->config1 == reg1->config &&
				 er->config2 == reg2->config)) {
			atomic_inc(&er->ref);
			er->config = (hwc->config >> 32);
			er->config1 = reg1->config;
			er->config2 = reg2->config;
			ok = true;
		}
	}
	raw_spin_unlock_irqrestore(&er->lock, flags);

	if (!ok) {
		/*
		 * The Rbox events are always in pairs. The paired
		 * events are functional identical, but use different
		 * extra registers. If we failed to take an extra
		 * register, try the alternative.
		 */
		if (idx % 2)
			idx--;
		else
			idx++;
		if (idx != reg1->idx % 6) {
			if (idx == 2)
				config1 >>= 8;
			else if (idx == 3)
				config1 <<= 8;
			goto again;
		}
	} else {
		if (!uncore_box_is_fake(box)) {
			if (idx != reg1->idx % 6)
				nhmex_rbox_alter_er(box, event);
			reg1->alloc = 1;
		}
		return NULL;
	}
	return &constraint_empty;
}

static void nhmex_rbox_put_constraint(struct intel_uncore_box *box, struct perf_event *event)
{
	struct intel_uncore_extra_reg *er;
	struct hw_perf_event_extra *reg1 = &event->hw.extra_reg;
	int idx, er_idx;

	if (uncore_box_is_fake(box) || !reg1->alloc)
		return;

	idx = reg1->idx % 6;
	er_idx = idx;
	if (er_idx > 2)
		er_idx--;
	er_idx += (reg1->idx / 6) * 5;

	er = &box->shared_regs[er_idx];
	if (idx == 2 || idx == 3)
		atomic_sub(1 << ((idx - 2) * 8), &er->ref);
	else
		atomic_dec(&er->ref);

	reg1->alloc = 0;
}

static int nhmex_rbox_hw_config(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	struct hw_perf_event_extra *reg1 = &event->hw.extra_reg;
	struct hw_perf_event_extra *reg2 = &event->hw.branch_reg;
	int idx;

	idx = (event->hw.config & NHMEX_R_PMON_CTL_EV_SEL_MASK) >>
		NHMEX_R_PMON_CTL_EV_SEL_SHIFT;
	if (idx >= 0x18)
		return -EINVAL;

	reg1->idx = idx;
	reg1->config = event->attr.config1;

	switch (idx % 6) {
	case 4:
	case 5:
		hwc->config |= event->attr.config & (~0ULL << 32);
		reg2->config = event->attr.config2;
		break;
	};
	return 0;
}

static void nhmex_rbox_msr_enable_event(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	struct hw_perf_event_extra *reg1 = &hwc->extra_reg;
	struct hw_perf_event_extra *reg2 = &hwc->branch_reg;
	int idx, port;

	idx = reg1->idx;
	port = idx / 6 + box->pmu->pmu_idx * 4;

	switch (idx % 6) {
	case 0:
		wrmsrl(NHMEX_R_MSR_PORTN_IPERF_CFG0(port), reg1->config);
		break;
	case 1:
		wrmsrl(NHMEX_R_MSR_PORTN_IPERF_CFG1(port), reg1->config);
		break;
	case 2:
	case 3:
		wrmsrl(NHMEX_R_MSR_PORTN_QLX_CFG(port),
			uncore_shared_reg_config(box, 2 + (idx / 6) * 5));
		break;
	case 4:
		wrmsrl(NHMEX_R_MSR_PORTN_XBR_SET1_MM_CFG(port),
			hwc->config >> 32);
		wrmsrl(NHMEX_R_MSR_PORTN_XBR_SET1_MATCH(port), reg1->config);
		wrmsrl(NHMEX_R_MSR_PORTN_XBR_SET1_MASK(port), reg2->config);
		break;
	case 5:
		wrmsrl(NHMEX_R_MSR_PORTN_XBR_SET2_MM_CFG(port),
			hwc->config >> 32);
		wrmsrl(NHMEX_R_MSR_PORTN_XBR_SET2_MATCH(port), reg1->config);
		wrmsrl(NHMEX_R_MSR_PORTN_XBR_SET2_MASK(port), reg2->config);
		break;
	};

	wrmsrl(hwc->config_base, NHMEX_PMON_CTL_EN_BIT0 |
		(hwc->config & NHMEX_R_PMON_CTL_EV_SEL_MASK));
}

DEFINE_UNCORE_FORMAT_ATTR(xbr_mm_cfg, xbr_mm_cfg, "config:32-63");
DEFINE_UNCORE_FORMAT_ATTR(xbr_match, xbr_match, "config1:0-63");
DEFINE_UNCORE_FORMAT_ATTR(xbr_mask, xbr_mask, "config2:0-63");
DEFINE_UNCORE_FORMAT_ATTR(qlx_cfg, qlx_cfg, "config1:0-15");
DEFINE_UNCORE_FORMAT_ATTR(iperf_cfg, iperf_cfg, "config1:0-31");

static struct attribute *nhmex_uncore_rbox_formats_attr[] = {
	&format_attr_event5.attr,
	&format_attr_xbr_mm_cfg.attr,
	&format_attr_xbr_match.attr,
	&format_attr_xbr_mask.attr,
	&format_attr_qlx_cfg.attr,
	&format_attr_iperf_cfg.attr,
	NULL,
};

static struct attribute_group nhmex_uncore_rbox_format_group = {
	.name = "format",
	.attrs = nhmex_uncore_rbox_formats_attr,
};

static struct uncore_event_desc nhmex_uncore_rbox_events[] = {
	INTEL_UNCORE_EVENT_DESC(qpi0_flit_send,		"event=0x0,iperf_cfg=0x80000000"),
	INTEL_UNCORE_EVENT_DESC(qpi1_filt_send,		"event=0x6,iperf_cfg=0x80000000"),
	INTEL_UNCORE_EVENT_DESC(qpi0_idle_filt,		"event=0x0,iperf_cfg=0x40000000"),
	INTEL_UNCORE_EVENT_DESC(qpi1_idle_filt,		"event=0x6,iperf_cfg=0x40000000"),
	INTEL_UNCORE_EVENT_DESC(qpi0_date_response,	"event=0x0,iperf_cfg=0xc4"),
	INTEL_UNCORE_EVENT_DESC(qpi1_date_response,	"event=0x6,iperf_cfg=0xc4"),
	{ /* end: all zeroes */ },
};

static struct intel_uncore_ops nhmex_uncore_rbox_ops = {
	NHMEX_UNCORE_OPS_COMMON_INIT(),
	.enable_event		= nhmex_rbox_msr_enable_event,
	.hw_config		= nhmex_rbox_hw_config,
	.get_constraint		= nhmex_rbox_get_constraint,
	.put_constraint		= nhmex_rbox_put_constraint,
};

static struct intel_uncore_type nhmex_uncore_rbox = {
	.name			= "rbox",
	.num_counters		= 8,
	.num_boxes		= 2,
	.perf_ctr_bits		= 48,
	.event_ctl		= NHMEX_R_MSR_PMON_CTL0,
	.perf_ctr		= NHMEX_R_MSR_PMON_CNT0,
	.event_mask		= NHMEX_R_PMON_RAW_EVENT_MASK,
	.box_ctl		= NHMEX_R_MSR_GLOBAL_CTL,
	.msr_offset		= NHMEX_R_MSR_OFFSET,
	.pair_ctr_ctl		= 1,
	.num_shared_regs	= 20,
	.event_descs		= nhmex_uncore_rbox_events,
	.ops			= &nhmex_uncore_rbox_ops,
	.format_group		= &nhmex_uncore_rbox_format_group
};

static struct intel_uncore_type *nhmex_msr_uncores[] = {
	&nhmex_uncore_ubox,
	&nhmex_uncore_cbox,
	&nhmex_uncore_bbox,
	&nhmex_uncore_sbox,
	&nhmex_uncore_mbox,
	&nhmex_uncore_rbox,
	&nhmex_uncore_wbox,
	NULL,
};
/* end of Nehalem-EX uncore support */

static void uncore_assign_hw_event(struct intel_uncore_box *box, struct perf_event *event, int idx)
{
	struct hw_perf_event *hwc = &event->hw;

	hwc->idx = idx;
	hwc->last_tag = ++box->tags[idx];

	if (hwc->idx == UNCORE_PMC_IDX_FIXED) {
		hwc->event_base = uncore_fixed_ctr(box);
		hwc->config_base = uncore_fixed_ctl(box);
		return;
	}

	hwc->config_base = uncore_event_ctl(box, hwc->idx);
	hwc->event_base  = uncore_perf_ctr(box, hwc->idx);
}

static void uncore_perf_event_update(struct intel_uncore_box *box, struct perf_event *event)
{
	u64 prev_count, new_count, delta;
	int shift;

	if (event->hw.idx >= UNCORE_PMC_IDX_FIXED)
		shift = 64 - uncore_fixed_ctr_bits(box);
	else
		shift = 64 - uncore_perf_ctr_bits(box);

	/* the hrtimer might modify the previous event value */
again:
	prev_count = local64_read(&event->hw.prev_count);
	new_count = uncore_read_counter(box, event);
	if (local64_xchg(&event->hw.prev_count, new_count) != prev_count)
		goto again;

	delta = (new_count << shift) - (prev_count << shift);
	delta >>= shift;

	local64_add(delta, &event->count);
}

/*
 * The overflow interrupt is unavailable for SandyBridge-EP, is broken
 * for SandyBridge. So we use hrtimer to periodically poll the counter
 * to avoid overflow.
 */
static enum hrtimer_restart uncore_pmu_hrtimer(struct hrtimer *hrtimer)
{
	struct intel_uncore_box *box;
	unsigned long flags;
	int bit;

	box = container_of(hrtimer, struct intel_uncore_box, hrtimer);
	if (!box->n_active || box->cpu != smp_processor_id())
		return HRTIMER_NORESTART;
	/*
	 * disable local interrupt to prevent uncore_pmu_event_start/stop
	 * to interrupt the update process
	 */
	local_irq_save(flags);

	for_each_set_bit(bit, box->active_mask, UNCORE_PMC_IDX_MAX)
		uncore_perf_event_update(box, box->events[bit]);

	local_irq_restore(flags);

	hrtimer_forward_now(hrtimer, ns_to_ktime(UNCORE_PMU_HRTIMER_INTERVAL));
	return HRTIMER_RESTART;
}

static void uncore_pmu_start_hrtimer(struct intel_uncore_box *box)
{
	__hrtimer_start_range_ns(&box->hrtimer,
			ns_to_ktime(UNCORE_PMU_HRTIMER_INTERVAL), 0,
			HRTIMER_MODE_REL_PINNED, 0);
}

static void uncore_pmu_cancel_hrtimer(struct intel_uncore_box *box)
{
	hrtimer_cancel(&box->hrtimer);
}

static void uncore_pmu_init_hrtimer(struct intel_uncore_box *box)
{
	hrtimer_init(&box->hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	box->hrtimer.function = uncore_pmu_hrtimer;
}

static struct intel_uncore_box *uncore_alloc_box(struct intel_uncore_type *type, int node)
{
	struct intel_uncore_box *box;
	int i, size;

	size = sizeof(*box) + type->num_shared_regs * sizeof(struct intel_uncore_extra_reg);

	box = kzalloc_node(size, GFP_KERNEL, node);
	if (!box)
		return NULL;

	for (i = 0; i < type->num_shared_regs; i++)
		raw_spin_lock_init(&box->shared_regs[i].lock);

	uncore_pmu_init_hrtimer(box);
	atomic_set(&box->refcnt, 1);
	box->cpu = -1;
	box->phys_id = -1;

	return box;
}

static struct intel_uncore_box *
uncore_pmu_to_box(struct intel_uncore_pmu *pmu, int cpu)
{
	struct intel_uncore_box *box;

	box = *per_cpu_ptr(pmu->box, cpu);
	if (box)
		return box;

	raw_spin_lock(&uncore_box_lock);
	list_for_each_entry(box, &pmu->box_list, list) {
		if (box->phys_id == topology_physical_package_id(cpu)) {
			atomic_inc(&box->refcnt);
			*per_cpu_ptr(pmu->box, cpu) = box;
			break;
		}
	}
	raw_spin_unlock(&uncore_box_lock);

	return *per_cpu_ptr(pmu->box, cpu);
}

static struct intel_uncore_pmu *uncore_event_to_pmu(struct perf_event *event)
{
	return container_of(event->pmu, struct intel_uncore_pmu, pmu);
}

static struct intel_uncore_box *uncore_event_to_box(struct perf_event *event)
{
	/*
	 * perf core schedules event on the basis of cpu, uncore events are
	 * collected by one of the cpus inside a physical package.
	 */
	return uncore_pmu_to_box(uncore_event_to_pmu(event), smp_processor_id());
}

static int
uncore_collect_events(struct intel_uncore_box *box, struct perf_event *leader, bool dogrp)
{
	struct perf_event *event;
	int n, max_count;

	max_count = box->pmu->type->num_counters;
	if (box->pmu->type->fixed_ctl)
		max_count++;

	if (box->n_events >= max_count)
		return -EINVAL;

	n = box->n_events;
	box->event_list[n] = leader;
	n++;
	if (!dogrp)
		return n;

	list_for_each_entry(event, &leader->sibling_list, group_entry) {
		if (event->state <= PERF_EVENT_STATE_OFF)
			continue;

		if (n >= max_count)
			return -EINVAL;

		box->event_list[n] = event;
		n++;
	}
	return n;
}

static struct event_constraint *
uncore_get_event_constraint(struct intel_uncore_box *box, struct perf_event *event)
{
	struct intel_uncore_type *type = box->pmu->type;
	struct event_constraint *c;

	if (type->ops->get_constraint) {
		c = type->ops->get_constraint(box, event);
		if (c)
			return c;
	}

	if (event->attr.config == UNCORE_FIXED_EVENT)
		return &constraint_fixed;

	if (type->constraints) {
		for_each_event_constraint(c, type->constraints) {
			if ((event->hw.config & c->cmask) == c->code)
				return c;
		}
	}

	return &type->unconstrainted;
}

static void uncore_put_event_constraint(struct intel_uncore_box *box, struct perf_event *event)
{
	if (box->pmu->type->ops->put_constraint)
		box->pmu->type->ops->put_constraint(box, event);
}

static int uncore_assign_events(struct intel_uncore_box *box, int assign[], int n)
{
	unsigned long used_mask[BITS_TO_LONGS(UNCORE_PMC_IDX_MAX)];
	struct event_constraint *c;
	int i, wmin, wmax, ret = 0;
	struct hw_perf_event *hwc;

	bitmap_zero(used_mask, UNCORE_PMC_IDX_MAX);

	for (i = 0, wmin = UNCORE_PMC_IDX_MAX, wmax = 0; i < n; i++) {
		hwc = &box->event_list[i]->hw;
		c = uncore_get_event_constraint(box, box->event_list[i]);
		hwc->constraint = c;
		wmin = min(wmin, c->weight);
		wmax = max(wmax, c->weight);
	}

	/* fastpath, try to reuse previous register */
	for (i = 0; i < n; i++) {
		hwc = &box->event_list[i]->hw;
		c = hwc->constraint;

		/* never assigned */
		if (hwc->idx == -1)
			break;

		/* constraint still honored */
		if (!test_bit(hwc->idx, c->idxmsk))
			break;

		/* not already used */
		if (test_bit(hwc->idx, used_mask))
			break;

		__set_bit(hwc->idx, used_mask);
		if (assign)
			assign[i] = hwc->idx;
	}
	/* slow path */
	if (i != n)
		ret = perf_assign_events(box->event_list, n,
					 wmin, wmax, assign);

	if (!assign || ret) {
		for (i = 0; i < n; i++)
			uncore_put_event_constraint(box, box->event_list[i]);
	}
	return ret ? -EINVAL : 0;
}

static void uncore_pmu_event_start(struct perf_event *event, int flags)
{
	struct intel_uncore_box *box = uncore_event_to_box(event);
	int idx = event->hw.idx;

	if (WARN_ON_ONCE(!(event->hw.state & PERF_HES_STOPPED)))
		return;

	if (WARN_ON_ONCE(idx == -1 || idx >= UNCORE_PMC_IDX_MAX))
		return;

	event->hw.state = 0;
	box->events[idx] = event;
	box->n_active++;
	__set_bit(idx, box->active_mask);

	local64_set(&event->hw.prev_count, uncore_read_counter(box, event));
	uncore_enable_event(box, event);

	if (box->n_active == 1) {
		uncore_enable_box(box);
		uncore_pmu_start_hrtimer(box);
	}
}

static void uncore_pmu_event_stop(struct perf_event *event, int flags)
{
	struct intel_uncore_box *box = uncore_event_to_box(event);
	struct hw_perf_event *hwc = &event->hw;

	if (__test_and_clear_bit(hwc->idx, box->active_mask)) {
		uncore_disable_event(box, event);
		box->n_active--;
		box->events[hwc->idx] = NULL;
		WARN_ON_ONCE(hwc->state & PERF_HES_STOPPED);
		hwc->state |= PERF_HES_STOPPED;

		if (box->n_active == 0) {
			uncore_disable_box(box);
			uncore_pmu_cancel_hrtimer(box);
		}
	}

	if ((flags & PERF_EF_UPDATE) && !(hwc->state & PERF_HES_UPTODATE)) {
		/*
		 * Drain the remaining delta count out of a event
		 * that we are disabling:
		 */
		uncore_perf_event_update(box, event);
		hwc->state |= PERF_HES_UPTODATE;
	}
}

static int uncore_pmu_event_add(struct perf_event *event, int flags)
{
	struct intel_uncore_box *box = uncore_event_to_box(event);
	struct hw_perf_event *hwc = &event->hw;
	int assign[UNCORE_PMC_IDX_MAX];
	int i, n, ret;

	if (!box)
		return -ENODEV;

	ret = n = uncore_collect_events(box, event, false);
	if (ret < 0)
		return ret;

	hwc->state = PERF_HES_UPTODATE | PERF_HES_STOPPED;
	if (!(flags & PERF_EF_START))
		hwc->state |= PERF_HES_ARCH;

	ret = uncore_assign_events(box, assign, n);
	if (ret)
		return ret;

	/* save events moving to new counters */
	for (i = 0; i < box->n_events; i++) {
		event = box->event_list[i];
		hwc = &event->hw;

		if (hwc->idx == assign[i] &&
			hwc->last_tag == box->tags[assign[i]])
			continue;
		/*
		 * Ensure we don't accidentally enable a stopped
		 * counter simply because we rescheduled.
		 */
		if (hwc->state & PERF_HES_STOPPED)
			hwc->state |= PERF_HES_ARCH;

		uncore_pmu_event_stop(event, PERF_EF_UPDATE);
	}

	/* reprogram moved events into new counters */
	for (i = 0; i < n; i++) {
		event = box->event_list[i];
		hwc = &event->hw;

		if (hwc->idx != assign[i] ||
			hwc->last_tag != box->tags[assign[i]])
			uncore_assign_hw_event(box, event, assign[i]);
		else if (i < box->n_events)
			continue;

		if (hwc->state & PERF_HES_ARCH)
			continue;

		uncore_pmu_event_start(event, 0);
	}
	box->n_events = n;

	return 0;
}

static void uncore_pmu_event_del(struct perf_event *event, int flags)
{
	struct intel_uncore_box *box = uncore_event_to_box(event);
	int i;

	uncore_pmu_event_stop(event, PERF_EF_UPDATE);

	for (i = 0; i < box->n_events; i++) {
		if (event == box->event_list[i]) {
			uncore_put_event_constraint(box, event);

			while (++i < box->n_events)
				box->event_list[i - 1] = box->event_list[i];

			--box->n_events;
			break;
		}
	}

	event->hw.idx = -1;
	event->hw.last_tag = ~0ULL;
}

static void uncore_pmu_event_read(struct perf_event *event)
{
	struct intel_uncore_box *box = uncore_event_to_box(event);
	uncore_perf_event_update(box, event);
}

/*
 * validation ensures the group can be loaded onto the
 * PMU if it was the only group available.
 */
static int uncore_validate_group(struct intel_uncore_pmu *pmu,
				struct perf_event *event)
{
	struct perf_event *leader = event->group_leader;
	struct intel_uncore_box *fake_box;
	int ret = -EINVAL, n;

	fake_box = uncore_alloc_box(pmu->type, NUMA_NO_NODE);
	if (!fake_box)
		return -ENOMEM;

	fake_box->pmu = pmu;
	/*
	 * the event is not yet connected with its
	 * siblings therefore we must first collect
	 * existing siblings, then add the new event
	 * before we can simulate the scheduling
	 */
	n = uncore_collect_events(fake_box, leader, true);
	if (n < 0)
		goto out;

	fake_box->n_events = n;
	n = uncore_collect_events(fake_box, event, false);
	if (n < 0)
		goto out;

	fake_box->n_events = n;

	ret = uncore_assign_events(fake_box, NULL, n);
out:
	kfree(fake_box);
	return ret;
}

static int uncore_pmu_event_init(struct perf_event *event)
{
	struct intel_uncore_pmu *pmu;
	struct intel_uncore_box *box;
	struct hw_perf_event *hwc = &event->hw;
	int ret;

	if (event->attr.type != event->pmu->type)
		return -ENOENT;

	pmu = uncore_event_to_pmu(event);
	/* no device found for this pmu */
	if (pmu->func_id < 0)
		return -ENOENT;

	/*
	 * Uncore PMU does measure at all privilege level all the time.
	 * So it doesn't make sense to specify any exclude bits.
	 */
	if (event->attr.exclude_user || event->attr.exclude_kernel ||
			event->attr.exclude_hv || event->attr.exclude_idle)
		return -EINVAL;

	/* Sampling not supported yet */
	if (hwc->sample_period)
		return -EINVAL;

	/*
	 * Place all uncore events for a particular physical package
	 * onto a single cpu
	 */
	if (event->cpu < 0)
		return -EINVAL;
	box = uncore_pmu_to_box(pmu, event->cpu);
	if (!box || box->cpu < 0)
		return -EINVAL;
	event->cpu = box->cpu;

	event->hw.idx = -1;
	event->hw.last_tag = ~0ULL;
	event->hw.extra_reg.idx = EXTRA_REG_NONE;
	event->hw.branch_reg.idx = EXTRA_REG_NONE;

	if (event->attr.config == UNCORE_FIXED_EVENT) {
		/* no fixed counter */
		if (!pmu->type->fixed_ctl)
			return -EINVAL;
		/*
		 * if there is only one fixed counter, only the first pmu
		 * can access the fixed counter
		 */
		if (pmu->type->single_fixed && pmu->pmu_idx > 0)
			return -EINVAL;

		/* fixed counters have event field hardcoded to zero */
		hwc->config = 0ULL;
	} else {
		hwc->config = event->attr.config & pmu->type->event_mask;
		if (pmu->type->ops->hw_config) {
			ret = pmu->type->ops->hw_config(box, event);
			if (ret)
				return ret;
		}
	}

	if (event->group_leader != event)
		ret = uncore_validate_group(pmu, event);
	else
		ret = 0;

	return ret;
}

static ssize_t uncore_get_attr_cpumask(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int n = cpulist_scnprintf(buf, PAGE_SIZE - 2, &uncore_cpu_mask);

	buf[n++] = '\n';
	buf[n] = '\0';
	return n;
}

static DEVICE_ATTR(cpumask, S_IRUGO, uncore_get_attr_cpumask, NULL);

static struct attribute *uncore_pmu_attrs[] = {
	&dev_attr_cpumask.attr,
	NULL,
};

static struct attribute_group uncore_pmu_attr_group = {
	.attrs = uncore_pmu_attrs,
};

static int __init uncore_pmu_register(struct intel_uncore_pmu *pmu)
{
	int ret;

	pmu->pmu = (struct pmu) {
		.attr_groups	= pmu->type->attr_groups,
		.task_ctx_nr	= perf_invalid_context,
		.event_init	= uncore_pmu_event_init,
		.add		= uncore_pmu_event_add,
		.del		= uncore_pmu_event_del,
		.start		= uncore_pmu_event_start,
		.stop		= uncore_pmu_event_stop,
		.read		= uncore_pmu_event_read,
	};

	if (pmu->type->num_boxes == 1) {
		if (strlen(pmu->type->name) > 0)
			sprintf(pmu->name, "uncore_%s", pmu->type->name);
		else
			sprintf(pmu->name, "uncore");
	} else {
		sprintf(pmu->name, "uncore_%s_%d", pmu->type->name,
			pmu->pmu_idx);
	}

	ret = perf_pmu_register(&pmu->pmu, pmu->name, -1);
	return ret;
}

static void __init uncore_type_exit(struct intel_uncore_type *type)
{
	int i;

	for (i = 0; i < type->num_boxes; i++)
		free_percpu(type->pmus[i].box);
	kfree(type->pmus);
	type->pmus = NULL;
	kfree(type->events_group);
	type->events_group = NULL;
}

static void __init uncore_types_exit(struct intel_uncore_type **types)
{
	int i;
	for (i = 0; types[i]; i++)
		uncore_type_exit(types[i]);
}

static int __init uncore_type_init(struct intel_uncore_type *type)
{
	struct intel_uncore_pmu *pmus;
	struct attribute_group *attr_group;
	struct attribute **attrs;
	int i, j;

	pmus = kzalloc(sizeof(*pmus) * type->num_boxes, GFP_KERNEL);
	if (!pmus)
		return -ENOMEM;

	type->unconstrainted = (struct event_constraint)
		__EVENT_CONSTRAINT(0, (1ULL << type->num_counters) - 1,
				0, type->num_counters, 0, 0);

	for (i = 0; i < type->num_boxes; i++) {
		pmus[i].func_id = -1;
		pmus[i].pmu_idx = i;
		pmus[i].type = type;
		INIT_LIST_HEAD(&pmus[i].box_list);
		pmus[i].box = alloc_percpu(struct intel_uncore_box *);
		if (!pmus[i].box)
			goto fail;
	}

	if (type->event_descs) {
		i = 0;
		while (type->event_descs[i].attr.attr.name)
			i++;

		attr_group = kzalloc(sizeof(struct attribute *) * (i + 1) +
					sizeof(*attr_group), GFP_KERNEL);
		if (!attr_group)
			goto fail;

		attrs = (struct attribute **)(attr_group + 1);
		attr_group->name = "events";
		attr_group->attrs = attrs;

		for (j = 0; j < i; j++)
			attrs[j] = &type->event_descs[j].attr.attr;

		type->events_group = attr_group;
	}

	type->pmu_group = &uncore_pmu_attr_group;
	type->pmus = pmus;
	return 0;
fail:
	uncore_type_exit(type);
	return -ENOMEM;
}

static int __init uncore_types_init(struct intel_uncore_type **types)
{
	int i, ret;

	for (i = 0; types[i]; i++) {
		ret = uncore_type_init(types[i]);
		if (ret)
			goto fail;
	}
	return 0;
fail:
	while (--i >= 0)
		uncore_type_exit(types[i]);
	return ret;
}

static struct pci_driver *uncore_pci_driver;
static bool pcidrv_registered;

/*
 * add a pci uncore device
 */
static int uncore_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct intel_uncore_pmu *pmu;
	struct intel_uncore_box *box;
	struct intel_uncore_type *type;
	int phys_id;

	phys_id = pcibus_to_physid[pdev->bus->number];
	if (phys_id < 0)
		return -ENODEV;

	if (UNCORE_PCI_DEV_TYPE(id->driver_data) == UNCORE_EXTRA_PCI_DEV) {
		extra_pci_dev[phys_id][UNCORE_PCI_DEV_IDX(id->driver_data)] = pdev;
		pci_set_drvdata(pdev, NULL);
		return 0;
	}

	type = pci_uncores[UNCORE_PCI_DEV_TYPE(id->driver_data)];
	box = uncore_alloc_box(type, NUMA_NO_NODE);
	if (!box)
		return -ENOMEM;

	/*
	 * for performance monitoring unit with multiple boxes,
	 * each box has a different function id.
	 */
	pmu = &type->pmus[UNCORE_PCI_DEV_IDX(id->driver_data)];
	if (pmu->func_id < 0)
		pmu->func_id = pdev->devfn;
	else
		WARN_ON_ONCE(pmu->func_id != pdev->devfn);

	box->phys_id = phys_id;
	box->pci_dev = pdev;
	box->pmu = pmu;
	uncore_box_init(box);
	pci_set_drvdata(pdev, box);

	raw_spin_lock(&uncore_box_lock);
	list_add_tail(&box->list, &pmu->box_list);
	raw_spin_unlock(&uncore_box_lock);

	return 0;
}

static void uncore_pci_remove(struct pci_dev *pdev)
{
	struct intel_uncore_box *box = pci_get_drvdata(pdev);
	struct intel_uncore_pmu *pmu;
	int i, cpu, phys_id = pcibus_to_physid[pdev->bus->number];

	box = pci_get_drvdata(pdev);
	if (!box) {
		for (i = 0; i < UNCORE_EXTRA_PCI_DEV_MAX; i++) {
			if (extra_pci_dev[phys_id][i] == pdev) {
				extra_pci_dev[phys_id][i] = NULL;
				break;
			}
		}
		WARN_ON_ONCE(i >= UNCORE_EXTRA_PCI_DEV_MAX);
		return;
	}

	pmu = box->pmu;
	if (WARN_ON_ONCE(phys_id != box->phys_id))
		return;

	pci_set_drvdata(pdev, NULL);

	raw_spin_lock(&uncore_box_lock);
	list_del(&box->list);
	raw_spin_unlock(&uncore_box_lock);

	for_each_possible_cpu(cpu) {
		if (*per_cpu_ptr(pmu->box, cpu) == box) {
			*per_cpu_ptr(pmu->box, cpu) = NULL;
			atomic_dec(&box->refcnt);
		}
	}

	WARN_ON_ONCE(atomic_read(&box->refcnt) != 1);
	kfree(box);
}

static int __init uncore_pci_init(void)
{
	int ret;

	switch (boot_cpu_data.x86_model) {
	case 45: /* Sandy Bridge-EP */
		ret = snbep_pci2phy_map_init(0x3ce0);
		if (ret)
			return ret;
		pci_uncores = snbep_pci_uncores;
		uncore_pci_driver = &snbep_uncore_pci_driver;
		break;
	case 62: /* IvyTown */
		ret = snbep_pci2phy_map_init(0x0e1e);
		if (ret)
			return ret;
		pci_uncores = ivt_pci_uncores;
		uncore_pci_driver = &ivt_uncore_pci_driver;
		break;
	default:
		return 0;
	}

	ret = uncore_types_init(pci_uncores);
	if (ret)
		return ret;

	uncore_pci_driver->probe = uncore_pci_probe;
	uncore_pci_driver->remove = uncore_pci_remove;

	ret = pci_register_driver(uncore_pci_driver);
	if (ret == 0)
		pcidrv_registered = true;
	else
		uncore_types_exit(pci_uncores);

	return ret;
}

static void __init uncore_pci_exit(void)
{
	if (pcidrv_registered) {
		pcidrv_registered = false;
		pci_unregister_driver(uncore_pci_driver);
		uncore_types_exit(pci_uncores);
	}
}

/* CPU hot plug/unplug are serialized by cpu_add_remove_lock mutex */
static LIST_HEAD(boxes_to_free);

static void uncore_kfree_boxes(void)
{
	struct intel_uncore_box *box;

	while (!list_empty(&boxes_to_free)) {
		box = list_entry(boxes_to_free.next,
				 struct intel_uncore_box, list);
		list_del(&box->list);
		kfree(box);
	}
}

static void uncore_cpu_dying(int cpu)
{
	struct intel_uncore_type *type;
	struct intel_uncore_pmu *pmu;
	struct intel_uncore_box *box;
	int i, j;

	for (i = 0; msr_uncores[i]; i++) {
		type = msr_uncores[i];
		for (j = 0; j < type->num_boxes; j++) {
			pmu = &type->pmus[j];
			box = *per_cpu_ptr(pmu->box, cpu);
			*per_cpu_ptr(pmu->box, cpu) = NULL;
			if (box && atomic_dec_and_test(&box->refcnt))
				list_add(&box->list, &boxes_to_free);
		}
	}
}

static int uncore_cpu_starting(int cpu)
{
	struct intel_uncore_type *type;
	struct intel_uncore_pmu *pmu;
	struct intel_uncore_box *box, *exist;
	int i, j, k, phys_id;

	phys_id = topology_physical_package_id(cpu);

	for (i = 0; msr_uncores[i]; i++) {
		type = msr_uncores[i];
		for (j = 0; j < type->num_boxes; j++) {
			pmu = &type->pmus[j];
			box = *per_cpu_ptr(pmu->box, cpu);
			/* called by uncore_cpu_init? */
			if (box && box->phys_id >= 0) {
				uncore_box_init(box);
				continue;
			}

			for_each_online_cpu(k) {
				exist = *per_cpu_ptr(pmu->box, k);
				if (exist && exist->phys_id == phys_id) {
					atomic_inc(&exist->refcnt);
					*per_cpu_ptr(pmu->box, cpu) = exist;
					if (box) {
						list_add(&box->list,
							 &boxes_to_free);
						box = NULL;
					}
					break;
				}
			}

			if (box) {
				box->phys_id = phys_id;
				uncore_box_init(box);
			}
		}
	}
	return 0;
}

static int uncore_cpu_prepare(int cpu, int phys_id)
{
	struct intel_uncore_type *type;
	struct intel_uncore_pmu *pmu;
	struct intel_uncore_box *box;
	int i, j;

	for (i = 0; msr_uncores[i]; i++) {
		type = msr_uncores[i];
		for (j = 0; j < type->num_boxes; j++) {
			pmu = &type->pmus[j];
			if (pmu->func_id < 0)
				pmu->func_id = j;

			box = uncore_alloc_box(type, cpu_to_node(cpu));
			if (!box)
				return -ENOMEM;

			box->pmu = pmu;
			box->phys_id = phys_id;
			*per_cpu_ptr(pmu->box, cpu) = box;
		}
	}
	return 0;
}

static void
uncore_change_context(struct intel_uncore_type **uncores, int old_cpu, int new_cpu)
{
	struct intel_uncore_type *type;
	struct intel_uncore_pmu *pmu;
	struct intel_uncore_box *box;
	int i, j;

	for (i = 0; uncores[i]; i++) {
		type = uncores[i];
		for (j = 0; j < type->num_boxes; j++) {
			pmu = &type->pmus[j];
			if (old_cpu < 0)
				box = uncore_pmu_to_box(pmu, new_cpu);
			else
				box = uncore_pmu_to_box(pmu, old_cpu);
			if (!box)
				continue;

			if (old_cpu < 0) {
				WARN_ON_ONCE(box->cpu != -1);
				box->cpu = new_cpu;
				continue;
			}

			WARN_ON_ONCE(box->cpu != old_cpu);
			if (new_cpu >= 0) {
				uncore_pmu_cancel_hrtimer(box);
				perf_pmu_migrate_context(&pmu->pmu,
						old_cpu, new_cpu);
				box->cpu = new_cpu;
			} else {
				box->cpu = -1;
			}
		}
	}
}

static void uncore_event_exit_cpu(int cpu)
{
	int i, phys_id, target;

	/* if exiting cpu is used for collecting uncore events */
	if (!cpumask_test_and_clear_cpu(cpu, &uncore_cpu_mask))
		return;

	/* find a new cpu to collect uncore events */
	phys_id = topology_physical_package_id(cpu);
	target = -1;
	for_each_online_cpu(i) {
		if (i == cpu)
			continue;
		if (phys_id == topology_physical_package_id(i)) {
			target = i;
			break;
		}
	}

	/* migrate uncore events to the new cpu */
	if (target >= 0)
		cpumask_set_cpu(target, &uncore_cpu_mask);

	uncore_change_context(msr_uncores, cpu, target);
	uncore_change_context(pci_uncores, cpu, target);
}

static void uncore_event_init_cpu(int cpu)
{
	int i, phys_id;

	phys_id = topology_physical_package_id(cpu);
	for_each_cpu(i, &uncore_cpu_mask) {
		if (phys_id == topology_physical_package_id(i))
			return;
	}

	cpumask_set_cpu(cpu, &uncore_cpu_mask);

	uncore_change_context(msr_uncores, -1, cpu);
	uncore_change_context(pci_uncores, -1, cpu);
}

static int uncore_cpu_notifier(struct notifier_block *self,
			       unsigned long action, void *hcpu)
{
	unsigned int cpu = (long)hcpu;

	/* allocate/free data structure for uncore box */
	switch (action & ~CPU_TASKS_FROZEN) {
	case CPU_UP_PREPARE:
		uncore_cpu_prepare(cpu, -1);
		break;
	case CPU_STARTING:
		uncore_cpu_starting(cpu);
		break;
	case CPU_UP_CANCELED:
	case CPU_DYING:
		uncore_cpu_dying(cpu);
		break;
	case CPU_ONLINE:
	case CPU_DEAD:
		uncore_kfree_boxes();
		break;
	default:
		break;
	}

	/* select the cpu that collects uncore events */
	switch (action & ~CPU_TASKS_FROZEN) {
	case CPU_DOWN_FAILED:
	case CPU_STARTING:
		uncore_event_init_cpu(cpu);
		break;
	case CPU_DOWN_PREPARE:
		uncore_event_exit_cpu(cpu);
		break;
	default:
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block uncore_cpu_nb = {
	.notifier_call	= uncore_cpu_notifier,
	/*
	 * to migrate uncore events, our notifier should be executed
	 * before perf core's notifier.
	 */
	.priority	= CPU_PRI_PERF + 1,
};

static void __init uncore_cpu_setup(void *dummy)
{
	uncore_cpu_starting(smp_processor_id());
}

static int __init uncore_cpu_init(void)
{
	int ret, cpu, max_cores;

	max_cores = boot_cpu_data.x86_max_cores;
	switch (boot_cpu_data.x86_model) {
	case 26: /* Nehalem */
	case 30:
	case 37: /* Westmere */
	case 44:
		msr_uncores = nhm_msr_uncores;
		break;
	case 42: /* Sandy Bridge */
	case 58: /* Ivy Bridge */
		if (snb_uncore_cbox.num_boxes > max_cores)
			snb_uncore_cbox.num_boxes = max_cores;
		msr_uncores = snb_msr_uncores;
		break;
	case 45: /* Sandy Bridge-EP */
		if (snbep_uncore_cbox.num_boxes > max_cores)
			snbep_uncore_cbox.num_boxes = max_cores;
		msr_uncores = snbep_msr_uncores;
		break;
	case 46: /* Nehalem-EX */
		uncore_nhmex = true;
	case 47: /* Westmere-EX aka. Xeon E7 */
		if (!uncore_nhmex)
			nhmex_uncore_mbox.event_descs = wsmex_uncore_mbox_events;
		if (nhmex_uncore_cbox.num_boxes > max_cores)
			nhmex_uncore_cbox.num_boxes = max_cores;
		msr_uncores = nhmex_msr_uncores;
		break;
	case 62: /* IvyTown */
		if (ivt_uncore_cbox.num_boxes > max_cores)
			ivt_uncore_cbox.num_boxes = max_cores;
		msr_uncores = ivt_msr_uncores;
		break;

	default:
		return 0;
	}

	ret = uncore_types_init(msr_uncores);
	if (ret)
		return ret;

	get_online_cpus();

	for_each_online_cpu(cpu) {
		int i, phys_id = topology_physical_package_id(cpu);

		for_each_cpu(i, &uncore_cpu_mask) {
			if (phys_id == topology_physical_package_id(i)) {
				phys_id = -1;
				break;
			}
		}
		if (phys_id < 0)
			continue;

		uncore_cpu_prepare(cpu, phys_id);
		uncore_event_init_cpu(cpu);
	}
	on_each_cpu(uncore_cpu_setup, NULL, 1);

	register_cpu_notifier(&uncore_cpu_nb);

	put_online_cpus();

	return 0;
}

static int __init uncore_pmus_register(void)
{
	struct intel_uncore_pmu *pmu;
	struct intel_uncore_type *type;
	int i, j;

	for (i = 0; msr_uncores[i]; i++) {
		type = msr_uncores[i];
		for (j = 0; j < type->num_boxes; j++) {
			pmu = &type->pmus[j];
			uncore_pmu_register(pmu);
		}
	}

	for (i = 0; pci_uncores[i]; i++) {
		type = pci_uncores[i];
		for (j = 0; j < type->num_boxes; j++) {
			pmu = &type->pmus[j];
			uncore_pmu_register(pmu);
		}
	}

	return 0;
}

static int __init intel_uncore_init(void)
{
	int ret;

	if (boot_cpu_data.x86_vendor != X86_VENDOR_INTEL)
		return -ENODEV;

	if (cpu_has_hypervisor)
		return -ENODEV;

	ret = uncore_pci_init();
	if (ret)
		goto fail;
	ret = uncore_cpu_init();
	if (ret) {
		uncore_pci_exit();
		goto fail;
	}

	uncore_pmus_register();
	return 0;
fail:
	return ret;
}
device_initcall(intel_uncore_init);

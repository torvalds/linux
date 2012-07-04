#include "perf_event_intel_uncore.h"

static struct intel_uncore_type *empty_uncore[] = { NULL, };
static struct intel_uncore_type **msr_uncores = empty_uncore;
static struct intel_uncore_type **pci_uncores = empty_uncore;
/* pci bus to socket mapping */
static int pcibus_to_physid[256] = { [0 ... 255] = -1, };

static DEFINE_RAW_SPINLOCK(uncore_box_lock);

/* mask of cpus that collect uncore events */
static cpumask_t uncore_cpu_mask;

/* constraint for the fixed counter */
static struct event_constraint constraint_fixed =
	EVENT_CONSTRAINT(~0ULL, 1 << UNCORE_PMC_IDX_FIXED, ~0ULL);

DEFINE_UNCORE_FORMAT_ATTR(event, event, "config:0-7");
DEFINE_UNCORE_FORMAT_ATTR(umask, umask, "config:8-15");
DEFINE_UNCORE_FORMAT_ATTR(edge, edge, "config:18");
DEFINE_UNCORE_FORMAT_ATTR(inv, inv, "config:23");
DEFINE_UNCORE_FORMAT_ATTR(cmask5, cmask, "config:24-28");
DEFINE_UNCORE_FORMAT_ATTR(cmask8, cmask, "config:24-31");
DEFINE_UNCORE_FORMAT_ATTR(thresh8, thresh, "config:24-31");
DEFINE_UNCORE_FORMAT_ATTR(thresh5, thresh, "config:24-28");
DEFINE_UNCORE_FORMAT_ATTR(occ_sel, occ_sel, "config:14-15");
DEFINE_UNCORE_FORMAT_ATTR(occ_invert, occ_invert, "config:30");
DEFINE_UNCORE_FORMAT_ATTR(occ_edge, occ_edge, "config:14-51");

/* Sandy Bridge-EP uncore support */
static void snbep_uncore_pci_disable_box(struct intel_uncore_box *box)
{
	struct pci_dev *pdev = box->pci_dev;
	int box_ctl = uncore_pci_box_ctl(box);
	u32 config;

	pci_read_config_dword(pdev, box_ctl, &config);
	config |= SNBEP_PMON_BOX_CTL_FRZ;
	pci_write_config_dword(pdev, box_ctl, config);
}

static void snbep_uncore_pci_enable_box(struct intel_uncore_box *box)
{
	struct pci_dev *pdev = box->pci_dev;
	int box_ctl = uncore_pci_box_ctl(box);
	u32 config;

	pci_read_config_dword(pdev, box_ctl, &config);
	config &= ~SNBEP_PMON_BOX_CTL_FRZ;
	pci_write_config_dword(pdev, box_ctl, config);
}

static void snbep_uncore_pci_enable_event(struct intel_uncore_box *box,
					struct perf_event *event)
{
	struct pci_dev *pdev = box->pci_dev;
	struct hw_perf_event *hwc = &event->hw;

	pci_write_config_dword(pdev, hwc->config_base, hwc->config |
				SNBEP_PMON_CTL_EN);
}

static void snbep_uncore_pci_disable_event(struct intel_uncore_box *box,
					struct perf_event *event)
{
	struct pci_dev *pdev = box->pci_dev;
	struct hw_perf_event *hwc = &event->hw;

	pci_write_config_dword(pdev, hwc->config_base, hwc->config);
}

static u64 snbep_uncore_pci_read_counter(struct intel_uncore_box *box,
					struct perf_event *event)
{
	struct pci_dev *pdev = box->pci_dev;
	struct hw_perf_event *hwc = &event->hw;
	u64 count;

	pci_read_config_dword(pdev, hwc->event_base, (u32 *)&count);
	pci_read_config_dword(pdev, hwc->event_base + 4, (u32 *)&count + 1);
	return count;
}

static void snbep_uncore_pci_init_box(struct intel_uncore_box *box)
{
	struct pci_dev *pdev = box->pci_dev;
	pci_write_config_dword(pdev, SNBEP_PCI_PMON_BOX_CTL,
				SNBEP_PMON_BOX_CTL_INT);
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
		return;
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
		return;
	}
}

static void snbep_uncore_msr_enable_event(struct intel_uncore_box *box,
					struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;

	wrmsrl(hwc->config_base, hwc->config | SNBEP_PMON_CTL_EN);
}

static void snbep_uncore_msr_disable_event(struct intel_uncore_box *box,
					struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;

	wrmsrl(hwc->config_base, hwc->config);
}

static u64 snbep_uncore_msr_read_counter(struct intel_uncore_box *box,
					struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	u64 count;

	rdmsrl(hwc->event_base, count);
	return count;
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

static struct attribute *snbep_uncore_pcu_formats_attr[] = {
	&format_attr_event.attr,
	&format_attr_occ_sel.attr,
	&format_attr_edge.attr,
	&format_attr_inv.attr,
	&format_attr_thresh5.attr,
	&format_attr_occ_invert.attr,
	&format_attr_occ_edge.attr,
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
	INTEL_UNCORE_EVENT_DESC(drs_data,         "event=0x02,umask=0x08"),
	INTEL_UNCORE_EVENT_DESC(ncb_data,         "event=0x03,umask=0x04"),
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

static struct attribute_group snbep_uncore_pcu_format_group = {
	.name = "format",
	.attrs = snbep_uncore_pcu_formats_attr,
};

static struct intel_uncore_ops snbep_uncore_msr_ops = {
	.init_box	= snbep_uncore_msr_init_box,
	.disable_box	= snbep_uncore_msr_disable_box,
	.enable_box	= snbep_uncore_msr_enable_box,
	.disable_event	= snbep_uncore_msr_disable_event,
	.enable_event	= snbep_uncore_msr_enable_event,
	.read_counter	= snbep_uncore_msr_read_counter,
};

static struct intel_uncore_ops snbep_uncore_pci_ops = {
	.init_box	= snbep_uncore_pci_init_box,
	.disable_box	= snbep_uncore_pci_disable_box,
	.enable_box	= snbep_uncore_pci_enable_box,
	.disable_event	= snbep_uncore_pci_disable_event,
	.enable_event	= snbep_uncore_pci_enable_event,
	.read_counter	= snbep_uncore_pci_read_counter,
};

static struct event_constraint snbep_uncore_cbox_constraints[] = {
	UNCORE_EVENT_CONSTRAINT(0x01, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x02, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x04, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x05, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x07, 0x3),
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
	UNCORE_EVENT_CONSTRAINT(0x30, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x31, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x32, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x33, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x34, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x36, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x37, 0x3),
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

static struct intel_uncore_type snbep_uncore_cbox = {
	.name		= "cbox",
	.num_counters   = 4,
	.num_boxes	= 8,
	.perf_ctr_bits	= 44,
	.event_ctl	= SNBEP_C0_MSR_PMON_CTL0,
	.perf_ctr	= SNBEP_C0_MSR_PMON_CTR0,
	.event_mask	= SNBEP_PMON_RAW_EVENT_MASK,
	.box_ctl	= SNBEP_C0_MSR_PMON_BOX_CTL,
	.msr_offset	= SNBEP_CBO_MSR_OFFSET,
	.constraints	= snbep_uncore_cbox_constraints,
	.ops		= &snbep_uncore_msr_ops,
	.format_group	= &snbep_uncore_format_group,
};

static struct intel_uncore_type snbep_uncore_pcu = {
	.name		= "pcu",
	.num_counters   = 4,
	.num_boxes	= 1,
	.perf_ctr_bits	= 48,
	.perf_ctr	= SNBEP_PCU_MSR_PMON_CTR0,
	.event_ctl	= SNBEP_PCU_MSR_PMON_CTL0,
	.event_mask	= SNBEP_PCU_MSR_PMON_RAW_EVENT_MASK,
	.box_ctl	= SNBEP_PCU_MSR_PMON_BOX_CTL,
	.ops		= &snbep_uncore_msr_ops,
	.format_group	= &snbep_uncore_pcu_format_group,
};

static struct intel_uncore_type *snbep_msr_uncores[] = {
	&snbep_uncore_ubox,
	&snbep_uncore_cbox,
	&snbep_uncore_pcu,
	NULL,
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
	.name		= "qpi",
	.num_counters   = 4,
	.num_boxes	= 2,
	.perf_ctr_bits	= 48,
	.event_descs	= snbep_uncore_qpi_events,
	SNBEP_UNCORE_PCI_COMMON_INIT(),
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

static struct intel_uncore_type *snbep_pci_uncores[] = {
	&snbep_uncore_ha,
	&snbep_uncore_imc,
	&snbep_uncore_qpi,
	&snbep_uncore_r2pcie,
	&snbep_uncore_r3qpi,
	NULL,
};

static DEFINE_PCI_DEVICE_TABLE(snbep_uncore_pci_ids) = {
	{ /* Home Agent */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_UNC_HA),
		.driver_data = (unsigned long)&snbep_uncore_ha,
	},
	{ /* MC Channel 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_UNC_IMC0),
		.driver_data = (unsigned long)&snbep_uncore_imc,
	},
	{ /* MC Channel 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_UNC_IMC1),
		.driver_data = (unsigned long)&snbep_uncore_imc,
	},
	{ /* MC Channel 2 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_UNC_IMC2),
		.driver_data = (unsigned long)&snbep_uncore_imc,
	},
	{ /* MC Channel 3 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_UNC_IMC3),
		.driver_data = (unsigned long)&snbep_uncore_imc,
	},
	{ /* QPI Port 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_UNC_QPI0),
		.driver_data = (unsigned long)&snbep_uncore_qpi,
	},
	{ /* QPI Port 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_UNC_QPI1),
		.driver_data = (unsigned long)&snbep_uncore_qpi,
	},
	{ /* P2PCIe */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_UNC_R2PCIE),
		.driver_data = (unsigned long)&snbep_uncore_r2pcie,
	},
	{ /* R3QPI Link 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_UNC_R3QPI0),
		.driver_data = (unsigned long)&snbep_uncore_r3qpi,
	},
	{ /* R3QPI Link 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_UNC_R3QPI1),
		.driver_data = (unsigned long)&snbep_uncore_r3qpi,
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
static void snbep_pci2phy_map_init(void)
{
	struct pci_dev *ubox_dev = NULL;
	int i, bus, nodeid;
	u32 config;

	while (1) {
		/* find the UBOX device */
		ubox_dev = pci_get_device(PCI_VENDOR_ID_INTEL,
					PCI_DEVICE_ID_INTEL_JAKETOWN_UBOX,
					ubox_dev);
		if (!ubox_dev)
			break;
		bus = ubox_dev->bus->number;
		/* get the Node ID of the local register */
		pci_read_config_dword(ubox_dev, 0x40, &config);
		nodeid = config;
		/* get the Node ID mapping */
		pci_read_config_dword(ubox_dev, 0x54, &config);
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
	};
	return;
}
/* end of Sandy Bridge-EP uncore support */


/* Sandy Bridge uncore support */
static void snb_uncore_msr_enable_event(struct intel_uncore_box *box,
					struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;

	if (hwc->idx < UNCORE_PMC_IDX_FIXED)
		wrmsrl(hwc->config_base, hwc->config | SNB_UNC_CTL_EN);
	else
		wrmsrl(hwc->config_base, SNB_UNC_CTL_EN);
}

static void snb_uncore_msr_disable_event(struct intel_uncore_box *box,
					struct perf_event *event)
{
	wrmsrl(event->hw.config_base, 0);
}

static u64 snb_uncore_msr_read_counter(struct intel_uncore_box *box,
					struct perf_event *event)
{
	u64 count;
	rdmsrl(event->hw.event_base, count);
	return count;
}

static void snb_uncore_msr_init_box(struct intel_uncore_box *box)
{
	if (box->pmu->pmu_idx == 0) {
		wrmsrl(SNB_UNC_PERF_GLOBAL_CTL,
			SNB_UNC_GLOBAL_CTL_EN | SNB_UNC_GLOBAL_CTL_CORE_ALL);
	}
}

static struct attribute *snb_uncore_formats_attr[] = {
	&format_attr_event.attr,
	&format_attr_umask.attr,
	&format_attr_edge.attr,
	&format_attr_inv.attr,
	&format_attr_cmask5.attr,
	NULL,
};

static struct attribute_group snb_uncore_format_group = {
	.name = "format",
	.attrs = snb_uncore_formats_attr,
};

static struct intel_uncore_ops snb_uncore_msr_ops = {
	.init_box	= snb_uncore_msr_init_box,
	.disable_event	= snb_uncore_msr_disable_event,
	.enable_event	= snb_uncore_msr_enable_event,
	.read_counter	= snb_uncore_msr_read_counter,
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
	wrmsrl(NHM_UNC_PERF_GLOBAL_CTL,
		NHM_UNC_GLOBAL_CTL_EN_PC_ALL | NHM_UNC_GLOBAL_CTL_EN_FC);
}

static void nhm_uncore_msr_enable_event(struct intel_uncore_box *box,
					struct perf_event *event)
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
	.read_counter	= snb_uncore_msr_read_counter,
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

static void uncore_assign_hw_event(struct intel_uncore_box *box,
				struct perf_event *event, int idx)
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

static void uncore_perf_event_update(struct intel_uncore_box *box,
					struct perf_event *event)
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

struct intel_uncore_box *uncore_alloc_box(int cpu)
{
	struct intel_uncore_box *box;

	box = kmalloc_node(sizeof(*box), GFP_KERNEL | __GFP_ZERO,
			   cpu_to_node(cpu));
	if (!box)
		return NULL;

	uncore_pmu_init_hrtimer(box);
	atomic_set(&box->refcnt, 1);
	box->cpu = -1;
	box->phys_id = -1;

	return box;
}

static struct intel_uncore_box *
uncore_pmu_to_box(struct intel_uncore_pmu *pmu, int cpu)
{
	static struct intel_uncore_box *box;

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
	return uncore_pmu_to_box(uncore_event_to_pmu(event),
				 smp_processor_id());
}

static int uncore_collect_events(struct intel_uncore_box *box,
				struct perf_event *leader, bool dogrp)
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
uncore_event_constraint(struct intel_uncore_type *type,
			struct perf_event *event)
{
	struct event_constraint *c;

	if (event->hw.config == ~0ULL)
		return &constraint_fixed;

	if (type->constraints) {
		for_each_event_constraint(c, type->constraints) {
			if ((event->hw.config & c->cmask) == c->code)
				return c;
		}
	}

	return &type->unconstrainted;
}

static int uncore_assign_events(struct intel_uncore_box *box,
				int assign[], int n)
{
	unsigned long used_mask[BITS_TO_LONGS(UNCORE_PMC_IDX_MAX)];
	struct event_constraint *c, *constraints[UNCORE_PMC_IDX_MAX];
	int i, ret, wmin, wmax;
	struct hw_perf_event *hwc;

	bitmap_zero(used_mask, UNCORE_PMC_IDX_MAX);

	for (i = 0, wmin = UNCORE_PMC_IDX_MAX, wmax = 0; i < n; i++) {
		c = uncore_event_constraint(box->pmu->type,
				box->event_list[i]);
		constraints[i] = c;
		wmin = min(wmin, c->weight);
		wmax = max(wmax, c->weight);
	}

	/* fastpath, try to reuse previous register */
	for (i = 0; i < n; i++) {
		hwc = &box->event_list[i]->hw;
		c = constraints[i];

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
		assign[i] = hwc->idx;
	}
	if (i == n)
		return 0;

	/* slow path */
	ret = perf_assign_events(constraints, n, wmin, wmax, assign);
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
	int assign[UNCORE_PMC_IDX_MAX];
	int ret = -EINVAL, n;

	fake_box = uncore_alloc_box(smp_processor_id());
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

	ret = uncore_assign_events(fake_box, assign, n);
out:
	kfree(fake_box);
	return ret;
}

int uncore_pmu_event_init(struct perf_event *event)
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
		hwc->config = ~0ULL;
	} else {
		hwc->config = event->attr.config & pmu->type->event_mask;
	}

	event->hw.idx = -1;
	event->hw.last_tag = ~0ULL;

	if (event->group_leader != event)
		ret = uncore_validate_group(pmu, event);
	else
		ret = 0;

	return ret;
}

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
	kfree(type->attr_groups[1]);
	type->attr_groups[1] = NULL;
}

static void uncore_types_exit(struct intel_uncore_type **types)
{
	int i;
	for (i = 0; types[i]; i++)
		uncore_type_exit(types[i]);
}

static int __init uncore_type_init(struct intel_uncore_type *type)
{
	struct intel_uncore_pmu *pmus;
	struct attribute_group *events_group;
	struct attribute **attrs;
	int i, j;

	pmus = kzalloc(sizeof(*pmus) * type->num_boxes, GFP_KERNEL);
	if (!pmus)
		return -ENOMEM;

	type->unconstrainted = (struct event_constraint)
		__EVENT_CONSTRAINT(0, (1ULL << type->num_counters) - 1,
				0, type->num_counters, 0);

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

		events_group = kzalloc(sizeof(struct attribute *) * (i + 1) +
					sizeof(*events_group), GFP_KERNEL);
		if (!events_group)
			goto fail;

		attrs = (struct attribute **)(events_group + 1);
		events_group->name = "events";
		events_group->attrs = attrs;

		for (j = 0; j < i; j++)
			attrs[j] = &type->event_descs[j].attr.attr;

		type->attr_groups[1] = events_group;
	}

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
static int __devinit uncore_pci_add(struct intel_uncore_type *type,
				    struct pci_dev *pdev)
{
	struct intel_uncore_pmu *pmu;
	struct intel_uncore_box *box;
	int i, phys_id;

	phys_id = pcibus_to_physid[pdev->bus->number];
	if (phys_id < 0)
		return -ENODEV;

	box = uncore_alloc_box(0);
	if (!box)
		return -ENOMEM;

	/*
	 * for performance monitoring unit with multiple boxes,
	 * each box has a different function id.
	 */
	for (i = 0; i < type->num_boxes; i++) {
		pmu = &type->pmus[i];
		if (pmu->func_id == pdev->devfn)
			break;
		if (pmu->func_id < 0) {
			pmu->func_id = pdev->devfn;
			break;
		}
		pmu = NULL;
	}

	if (!pmu) {
		kfree(box);
		return -EINVAL;
	}

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
	struct intel_uncore_pmu *pmu = box->pmu;
	int cpu, phys_id = pcibus_to_physid[pdev->bus->number];

	if (WARN_ON_ONCE(phys_id != box->phys_id))
		return;

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

static int __devinit uncore_pci_probe(struct pci_dev *pdev,
				const struct pci_device_id *id)
{
	struct intel_uncore_type *type;

	type = (struct intel_uncore_type *)id->driver_data;
	return uncore_pci_add(type, pdev);
}

static int __init uncore_pci_init(void)
{
	int ret;

	switch (boot_cpu_data.x86_model) {
	case 45: /* Sandy Bridge-EP */
		pci_uncores = snbep_pci_uncores;
		uncore_pci_driver = &snbep_uncore_pci_driver;
		snbep_pci2phy_map_init();
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

static void __cpuinit uncore_cpu_dying(int cpu)
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
				kfree(box);
		}
	}
}

static int __cpuinit uncore_cpu_starting(int cpu)
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
					kfree(box);
					box = NULL;
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

static int __cpuinit uncore_cpu_prepare(int cpu, int phys_id)
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

			box = uncore_alloc_box(cpu);
			if (!box)
				return -ENOMEM;

			box->pmu = pmu;
			box->phys_id = phys_id;
			*per_cpu_ptr(pmu->box, cpu) = box;
		}
	}
	return 0;
}

static void __cpuinit uncore_change_context(struct intel_uncore_type **uncores,
					    int old_cpu, int new_cpu)
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

static void __cpuinit uncore_event_exit_cpu(int cpu)
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

static void __cpuinit uncore_event_init_cpu(int cpu)
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

static int __cpuinit uncore_cpu_notifier(struct notifier_block *self,
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

static struct notifier_block uncore_cpu_nb __cpuinitdata = {
	.notifier_call = uncore_cpu_notifier,
	/*
	 * to migrate uncore events, our notifier should be executed
	 * before perf core's notifier.
	 */
	.priority = CPU_PRI_PERF + 1,
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
		if (snb_uncore_cbox.num_boxes > max_cores)
			snb_uncore_cbox.num_boxes = max_cores;
		msr_uncores = snb_msr_uncores;
		break;
	case 45: /* Sandy Birdge-EP */
		if (snbep_uncore_cbox.num_boxes > max_cores)
			snbep_uncore_cbox.num_boxes = max_cores;
		msr_uncores = snbep_msr_uncores;
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

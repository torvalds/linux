// SPDX-License-Identifier: GPL-2.0
/*
 * Intel Vendor Specific Extended Capabilities auxiliary bus driver
 *
 * Copyright (c) 2021, Intel Corporation.
 * All Rights Reserved.
 *
 * Author: David E. Box <david.e.box@linux.intel.com>
 *
 * This driver discovers and creates auxiliary devices for Intel defined PCIe
 * "Vendor Specific" and "Designated Vendor Specific" Extended Capabilities,
 * VSEC and DVSEC respectively. The driver supports features on specific PCIe
 * endpoints that exist primarily to expose them.
 */

#include <linux/auxiliary_bus.h>
#include <linux/bits.h>
#include <linux/bitops.h>
#include <linux/bug.h>
#include <linux/cleanup.h>
#include <linux/delay.h>
#include <linux/idr.h>
#include <linux/log2.h>
#include <linux/intel_vsec.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/types.h>

#define PMT_XA_START			0
#define PMT_XA_MAX			INT_MAX
#define PMT_XA_LIMIT			XA_LIMIT(PMT_XA_START, PMT_XA_MAX)

static DEFINE_IDA(intel_vsec_ida);
static DEFINE_IDA(intel_vsec_sdsi_ida);
static DEFINE_XARRAY_ALLOC(auxdev_array);

enum vsec_device_state {
	STATE_NOT_FOUND,
	STATE_REGISTERED,
	STATE_SKIP,
};

struct vsec_priv {
	struct intel_vsec_platform_info *info;
	struct device *suppliers[VSEC_FEATURE_COUNT];
	struct oobmsm_plat_info plat_info;
	enum vsec_device_state state[VSEC_FEATURE_COUNT];
	unsigned long found_caps;
};

static const char *intel_vsec_name(enum intel_vsec_id id)
{
	switch (id) {
	case VSEC_ID_TELEMETRY:
		return "telemetry";

	case VSEC_ID_WATCHER:
		return "watcher";

	case VSEC_ID_CRASHLOG:
		return "crashlog";

	case VSEC_ID_SDSI:
		return "sdsi";

	case VSEC_ID_TPMI:
		return "tpmi";

	case VSEC_ID_DISCOVERY:
		return "discovery";

	default:
		return NULL;
	}
}

static bool intel_vsec_supported(u16 id, unsigned long caps)
{
	switch (id) {
	case VSEC_ID_TELEMETRY:
		return !!(caps & VSEC_CAP_TELEMETRY);
	case VSEC_ID_WATCHER:
		return !!(caps & VSEC_CAP_WATCHER);
	case VSEC_ID_CRASHLOG:
		return !!(caps & VSEC_CAP_CRASHLOG);
	case VSEC_ID_SDSI:
		return !!(caps & VSEC_CAP_SDSI);
	case VSEC_ID_TPMI:
		return !!(caps & VSEC_CAP_TPMI);
	case VSEC_ID_DISCOVERY:
		return !!(caps & VSEC_CAP_DISCOVERY);
	default:
		return false;
	}
}

static void intel_vsec_remove_aux(void *data)
{
	auxiliary_device_delete(data);
	auxiliary_device_uninit(data);
}

static void intel_vsec_dev_release(struct device *dev)
{
	struct intel_vsec_device *intel_vsec_dev = dev_to_ivdev(dev);

	xa_erase(&auxdev_array, intel_vsec_dev->id);

	ida_free(intel_vsec_dev->ida, intel_vsec_dev->auxdev.id);

	kfree(intel_vsec_dev->resource);
	kfree(intel_vsec_dev);
}

static const struct vsec_feature_dependency *
get_consumer_dependencies(struct vsec_priv *priv, int cap_id)
{
	const struct vsec_feature_dependency *deps = priv->info->deps;
	int consumer_id = priv->info->num_deps;

	if (!deps)
		return NULL;

	while (consumer_id--)
		if (deps[consumer_id].feature == BIT(cap_id))
			return &deps[consumer_id];

	return NULL;
}

static bool vsec_driver_present(int cap_id)
{
	unsigned long bit = BIT(cap_id);

	switch (bit) {
	case VSEC_CAP_TELEMETRY:
		return IS_ENABLED(CONFIG_INTEL_PMT_TELEMETRY);
	case VSEC_CAP_WATCHER:
		return IS_ENABLED(CONFIG_INTEL_PMT_WATCHER);
	case VSEC_CAP_CRASHLOG:
		return IS_ENABLED(CONFIG_INTEL_PMT_CRASHLOG);
	case VSEC_CAP_SDSI:
		return IS_ENABLED(CONFIG_INTEL_SDSI);
	case VSEC_CAP_TPMI:
		return IS_ENABLED(CONFIG_INTEL_TPMI);
	case VSEC_CAP_DISCOVERY:
		return IS_ENABLED(CONFIG_INTEL_PMT_DISCOVERY);
	default:
		return false;
	}
}

/*
 * Although pci_device_id table is available in the pdev, this prototype is
 * necessary because the code using it can be called by an exported API that
 * might pass a different pdev.
 */
static const struct pci_device_id intel_vsec_pci_ids[];

static int intel_vsec_link_devices(struct pci_dev *pdev, struct device *dev,
				   int consumer_id)
{
	const struct vsec_feature_dependency *deps;
	enum vsec_device_state *state;
	struct device **suppliers;
	struct vsec_priv *priv;
	int supplier_id;

	if (!consumer_id)
		return 0;

	if (!pci_match_id(intel_vsec_pci_ids, pdev))
		return 0;

	priv = pci_get_drvdata(pdev);
	state = priv->state;
	suppliers = priv->suppliers;

	priv->suppliers[consumer_id] = dev;

	deps = get_consumer_dependencies(priv, consumer_id);
	if (!deps)
		return 0;

	for_each_set_bit(supplier_id, &deps->supplier_bitmap, VSEC_FEATURE_COUNT) {
		struct device_link *link;

		if (state[supplier_id] != STATE_REGISTERED ||
		    !vsec_driver_present(supplier_id))
			continue;

		if (!suppliers[supplier_id]) {
			dev_err(dev, "Bad supplier list\n");
			return -EINVAL;
		}

		link = device_link_add(dev, suppliers[supplier_id],
				       DL_FLAG_AUTOPROBE_CONSUMER);
		if (!link)
			return -EINVAL;
	}

	return 0;
}

int intel_vsec_add_aux(struct pci_dev *pdev, struct device *parent,
		       struct intel_vsec_device *intel_vsec_dev,
		       const char *name)
{
	struct auxiliary_device *auxdev = &intel_vsec_dev->auxdev;
	int ret, id;

	if (!parent)
		return -EINVAL;

	ret = xa_alloc(&auxdev_array, &intel_vsec_dev->id, intel_vsec_dev,
		       PMT_XA_LIMIT, GFP_KERNEL);
	if (ret < 0) {
		kfree(intel_vsec_dev->resource);
		kfree(intel_vsec_dev);
		return ret;
	}

	id = ida_alloc(intel_vsec_dev->ida, GFP_KERNEL);
	if (id < 0) {
		xa_erase(&auxdev_array, intel_vsec_dev->id);
		kfree(intel_vsec_dev->resource);
		kfree(intel_vsec_dev);
		return id;
	}

	auxdev->id = id;
	auxdev->name = name;
	auxdev->dev.parent = parent;
	auxdev->dev.release = intel_vsec_dev_release;

	ret = auxiliary_device_init(auxdev);
	if (ret < 0) {
		intel_vsec_dev_release(&auxdev->dev);
		return ret;
	}

	/*
	 * Assign a name now to ensure that the device link doesn't contain
	 * a null string for the consumer name. This is a problem when a supplier
	 * supplies more than one consumer and can lead to a duplicate name error
	 * when the link is created in sysfs.
	 */
	ret = dev_set_name(&auxdev->dev, "%s.%s.%d", KBUILD_MODNAME, auxdev->name,
			   auxdev->id);
	if (ret)
		goto cleanup_aux;

	ret = intel_vsec_link_devices(pdev, &auxdev->dev, intel_vsec_dev->cap_id);
	if (ret)
		goto cleanup_aux;

	ret = auxiliary_device_add(auxdev);
	if (ret)
		goto cleanup_aux;

	return devm_add_action_or_reset(parent, intel_vsec_remove_aux,
				       auxdev);

cleanup_aux:
	auxiliary_device_uninit(auxdev);
	return ret;
}
EXPORT_SYMBOL_NS_GPL(intel_vsec_add_aux, "INTEL_VSEC");

static int intel_vsec_add_dev(struct pci_dev *pdev, struct intel_vsec_header *header,
			      struct intel_vsec_platform_info *info,
			      unsigned long cap_id)
{
	struct intel_vsec_device __free(kfree) *intel_vsec_dev = NULL;
	struct resource __free(kfree) *res = NULL;
	struct resource *tmp;
	struct device *parent;
	unsigned long quirks = info->quirks;
	u64 base_addr;
	int i;

	if (info->parent)
		parent = info->parent;
	else
		parent = &pdev->dev;

	if (!intel_vsec_supported(header->id, info->caps))
		return -EINVAL;

	if (!header->num_entries) {
		dev_dbg(&pdev->dev, "Invalid 0 entry count for header id %d\n", header->id);
		return -EINVAL;
	}

	if (!header->entry_size) {
		dev_dbg(&pdev->dev, "Invalid 0 entry size for header id %d\n", header->id);
		return -EINVAL;
	}

	intel_vsec_dev = kzalloc(sizeof(*intel_vsec_dev), GFP_KERNEL);
	if (!intel_vsec_dev)
		return -ENOMEM;

	res = kcalloc(header->num_entries, sizeof(*res), GFP_KERNEL);
	if (!res)
		return -ENOMEM;

	if (quirks & VSEC_QUIRK_TABLE_SHIFT)
		header->offset >>= TABLE_OFFSET_SHIFT;

	if (info->base_addr)
		base_addr = info->base_addr;
	else
		base_addr = pdev->resource[header->tbir].start;

	/*
	 * The DVSEC/VSEC contains the starting offset and count for a block of
	 * discovery tables. Create a resource array of these tables to the
	 * auxiliary device driver.
	 */
	for (i = 0, tmp = res; i < header->num_entries; i++, tmp++) {
		tmp->start = base_addr + header->offset + i * (header->entry_size * sizeof(u32));
		tmp->end = tmp->start + (header->entry_size * sizeof(u32)) - 1;
		tmp->flags = IORESOURCE_MEM;

		/* Check resource is not in use */
		if (!request_mem_region(tmp->start, resource_size(tmp), ""))
			return -EBUSY;

		release_mem_region(tmp->start, resource_size(tmp));
	}

	intel_vsec_dev->pcidev = pdev;
	intel_vsec_dev->resource = no_free_ptr(res);
	intel_vsec_dev->num_resources = header->num_entries;
	intel_vsec_dev->quirks = info->quirks;
	intel_vsec_dev->base_addr = info->base_addr;
	intel_vsec_dev->priv_data = info->priv_data;
	intel_vsec_dev->cap_id = cap_id;

	if (header->id == VSEC_ID_SDSI)
		intel_vsec_dev->ida = &intel_vsec_sdsi_ida;
	else
		intel_vsec_dev->ida = &intel_vsec_ida;

	/*
	 * Pass the ownership of intel_vsec_dev and resource within it to
	 * intel_vsec_add_aux()
	 */
	return intel_vsec_add_aux(pdev, parent, no_free_ptr(intel_vsec_dev),
				  intel_vsec_name(header->id));
}

static bool suppliers_ready(struct vsec_priv *priv,
			    const struct vsec_feature_dependency *consumer_deps,
			    int cap_id)
{
	enum vsec_device_state *state = priv->state;
	int supplier_id;

	if (WARN_ON_ONCE(consumer_deps->feature != BIT(cap_id)))
		return false;

	/*
	 * Verify that all required suppliers have been found. Return false
	 * immediately if any are still missing.
	 */
	for_each_set_bit(supplier_id, &consumer_deps->supplier_bitmap, VSEC_FEATURE_COUNT) {
		if (state[supplier_id] == STATE_SKIP)
			continue;

		if (state[supplier_id] == STATE_NOT_FOUND)
			return false;
	}

	/*
	 * All suppliers have been found and the consumer is ready to be
	 * registered.
	 */
	return true;
}

static int get_cap_id(u32 header_id, unsigned long *cap_id)
{
	switch (header_id) {
	case VSEC_ID_TELEMETRY:
		*cap_id = ilog2(VSEC_CAP_TELEMETRY);
		break;
	case VSEC_ID_WATCHER:
		*cap_id = ilog2(VSEC_CAP_WATCHER);
		break;
	case VSEC_ID_CRASHLOG:
		*cap_id = ilog2(VSEC_CAP_CRASHLOG);
		break;
	case VSEC_ID_SDSI:
		*cap_id = ilog2(VSEC_CAP_SDSI);
		break;
	case VSEC_ID_TPMI:
		*cap_id = ilog2(VSEC_CAP_TPMI);
		break;
	case VSEC_ID_DISCOVERY:
		*cap_id = ilog2(VSEC_CAP_DISCOVERY);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int intel_vsec_register_device(struct pci_dev *pdev,
				      struct intel_vsec_header *header,
				      struct intel_vsec_platform_info *info)
{
	const struct vsec_feature_dependency *consumer_deps;
	struct vsec_priv *priv;
	unsigned long cap_id;
	int ret;

	ret = get_cap_id(header->id, &cap_id);
	if (ret)
		return ret;

	/*
	 * Only track dependencies for devices probed by the VSEC driver.
	 * For others using the exported APIs, add the device directly.
	 */
	if (!pci_match_id(intel_vsec_pci_ids, pdev))
		return intel_vsec_add_dev(pdev, header, info, cap_id);

	priv = pci_get_drvdata(pdev);
	if (priv->state[cap_id] == STATE_REGISTERED ||
	    priv->state[cap_id] == STATE_SKIP)
		return -EEXIST;

	priv->found_caps |= BIT(cap_id);

	if (!vsec_driver_present(cap_id)) {
		priv->state[cap_id] = STATE_SKIP;
		return -ENODEV;
	}

	consumer_deps = get_consumer_dependencies(priv, cap_id);
	if (!consumer_deps || suppliers_ready(priv, consumer_deps, cap_id)) {
		ret = intel_vsec_add_dev(pdev, header, info, cap_id);
		if (ret)
			priv->state[cap_id] = STATE_SKIP;
		else
			priv->state[cap_id] = STATE_REGISTERED;

		return ret;
	}

	return -EAGAIN;
}

static bool intel_vsec_walk_header(struct pci_dev *pdev,
				   struct intel_vsec_platform_info *info)
{
	struct intel_vsec_header **header = info->headers;
	bool have_devices = false;
	int ret;

	for ( ; *header; header++) {
		ret = intel_vsec_register_device(pdev, *header, info);
		if (!ret)
			have_devices = true;
	}

	return have_devices;
}

static bool intel_vsec_walk_dvsec(struct pci_dev *pdev,
				  struct intel_vsec_platform_info *info)
{
	bool have_devices = false;
	int pos = 0;

	do {
		struct intel_vsec_header header;
		u32 table, hdr;
		u16 vid;
		int ret;

		pos = pci_find_next_ext_capability(pdev, pos, PCI_EXT_CAP_ID_DVSEC);
		if (!pos)
			break;

		pci_read_config_dword(pdev, pos + PCI_DVSEC_HEADER1, &hdr);
		vid = PCI_DVSEC_HEADER1_VID(hdr);
		if (vid != PCI_VENDOR_ID_INTEL)
			continue;

		/* Support only revision 1 */
		header.rev = PCI_DVSEC_HEADER1_REV(hdr);
		if (header.rev != 1) {
			dev_info(&pdev->dev, "Unsupported DVSEC revision %d\n", header.rev);
			continue;
		}

		header.length = PCI_DVSEC_HEADER1_LEN(hdr);

		pci_read_config_byte(pdev, pos + INTEL_DVSEC_ENTRIES, &header.num_entries);
		pci_read_config_byte(pdev, pos + INTEL_DVSEC_SIZE, &header.entry_size);
		pci_read_config_dword(pdev, pos + INTEL_DVSEC_TABLE, &table);

		header.tbir = INTEL_DVSEC_TABLE_BAR(table);
		header.offset = INTEL_DVSEC_TABLE_OFFSET(table);

		pci_read_config_dword(pdev, pos + PCI_DVSEC_HEADER2, &hdr);
		header.id = PCI_DVSEC_HEADER2_ID(hdr);

		ret = intel_vsec_register_device(pdev, &header, info);
		if (ret)
			continue;

		have_devices = true;
	} while (true);

	return have_devices;
}

static bool intel_vsec_walk_vsec(struct pci_dev *pdev,
				 struct intel_vsec_platform_info *info)
{
	bool have_devices = false;
	int pos = 0;

	do {
		struct intel_vsec_header header;
		u32 table, hdr;
		int ret;

		pos = pci_find_next_ext_capability(pdev, pos, PCI_EXT_CAP_ID_VNDR);
		if (!pos)
			break;

		pci_read_config_dword(pdev, pos + PCI_VNDR_HEADER, &hdr);

		/* Support only revision 1 */
		header.rev = PCI_VNDR_HEADER_REV(hdr);
		if (header.rev != 1) {
			dev_info(&pdev->dev, "Unsupported VSEC revision %d\n", header.rev);
			continue;
		}

		header.id = PCI_VNDR_HEADER_ID(hdr);
		header.length = PCI_VNDR_HEADER_LEN(hdr);

		/* entry, size, and table offset are the same as DVSEC */
		pci_read_config_byte(pdev, pos + INTEL_DVSEC_ENTRIES, &header.num_entries);
		pci_read_config_byte(pdev, pos + INTEL_DVSEC_SIZE, &header.entry_size);
		pci_read_config_dword(pdev, pos + INTEL_DVSEC_TABLE, &table);

		header.tbir = INTEL_DVSEC_TABLE_BAR(table);
		header.offset = INTEL_DVSEC_TABLE_OFFSET(table);

		ret = intel_vsec_register_device(pdev, &header, info);
		if (ret)
			continue;

		have_devices = true;
	} while (true);

	return have_devices;
}

int intel_vsec_register(struct pci_dev *pdev,
			 struct intel_vsec_platform_info *info)
{
	if (!pdev || !info || !info->headers)
		return -EINVAL;

	if (!intel_vsec_walk_header(pdev, info))
		return -ENODEV;
	else
		return 0;
}
EXPORT_SYMBOL_NS_GPL(intel_vsec_register, "INTEL_VSEC");

static bool intel_vsec_get_features(struct pci_dev *pdev,
				    struct intel_vsec_platform_info *info)
{
	bool found = false;

	/*
	 * Both DVSEC and VSEC capabilities can exist on the same device,
	 * so both intel_vsec_walk_dvsec() and intel_vsec_walk_vsec() must be
	 * called independently. Additionally, intel_vsec_walk_header() is
	 * needed for devices that do not have VSEC/DVSEC but provide the
	 * information via device_data.
	 */
	if (intel_vsec_walk_dvsec(pdev, info))
		found = true;

	if (intel_vsec_walk_vsec(pdev, info))
		found = true;

	if (info && (info->quirks & VSEC_QUIRK_NO_DVSEC) &&
	    intel_vsec_walk_header(pdev, info))
		found = true;

	return found;
}

static void intel_vsec_skip_missing_dependencies(struct pci_dev *pdev)
{
	struct vsec_priv *priv = pci_get_drvdata(pdev);
	const struct vsec_feature_dependency *deps = priv->info->deps;
	int consumer_id = priv->info->num_deps;

	while (consumer_id--) {
		int supplier_id;

		deps = &priv->info->deps[consumer_id];

		for_each_set_bit(supplier_id, &deps->supplier_bitmap, VSEC_FEATURE_COUNT) {
			if (!(BIT(supplier_id) & priv->found_caps))
				priv->state[supplier_id] = STATE_SKIP;
		}
	}
}

static int intel_vsec_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct intel_vsec_platform_info *info;
	struct vsec_priv *priv;
	int num_caps, ret;
	int run_once = 0;
	bool found_any = false;

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	pci_save_state(pdev);
	info = (struct intel_vsec_platform_info *)id->driver_data;
	if (!info)
		return -EINVAL;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->info = info;
	pci_set_drvdata(pdev, priv);

	num_caps = hweight_long(info->caps);
	while (num_caps--) {
		found_any |= intel_vsec_get_features(pdev, info);

		if (priv->found_caps == info->caps)
			break;

		if (!run_once) {
			intel_vsec_skip_missing_dependencies(pdev);
			run_once = 1;
		}
	}

	if (!found_any)
		return -ENODEV;

	return 0;
}

int intel_vsec_set_mapping(struct oobmsm_plat_info *plat_info,
			   struct intel_vsec_device *vsec_dev)
{
	struct vsec_priv *priv;

	priv = pci_get_drvdata(vsec_dev->pcidev);
	if (!priv)
		return -EINVAL;

	priv->plat_info = *plat_info;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(intel_vsec_set_mapping, "INTEL_VSEC");

struct oobmsm_plat_info *intel_vsec_get_mapping(struct pci_dev *pdev)
{
	struct vsec_priv *priv;

	if (!pci_match_id(intel_vsec_pci_ids, pdev))
		return ERR_PTR(-EINVAL);

	priv = pci_get_drvdata(pdev);
	if (!priv)
		return ERR_PTR(-EINVAL);

	return &priv->plat_info;
}
EXPORT_SYMBOL_NS_GPL(intel_vsec_get_mapping, "INTEL_VSEC");

/* DG1 info */
static struct intel_vsec_header dg1_header = {
	.length = 0x10,
	.id = 2,
	.num_entries = 1,
	.entry_size = 3,
	.tbir = 0,
	.offset = 0x466000,
};

static struct intel_vsec_header *dg1_headers[] = {
	&dg1_header,
	NULL
};

static const struct intel_vsec_platform_info dg1_info = {
	.caps = VSEC_CAP_TELEMETRY,
	.headers = dg1_headers,
	.quirks = VSEC_QUIRK_NO_DVSEC | VSEC_QUIRK_EARLY_HW,
};

/* MTL info */
static const struct intel_vsec_platform_info mtl_info = {
	.caps = VSEC_CAP_TELEMETRY,
};

static const struct vsec_feature_dependency oobmsm_deps[] = {
	{
		.feature = VSEC_CAP_TELEMETRY,
		.supplier_bitmap = VSEC_CAP_DISCOVERY | VSEC_CAP_TPMI,
	},
};

/* OOBMSM info */
static const struct intel_vsec_platform_info oobmsm_info = {
	.caps = VSEC_CAP_TELEMETRY | VSEC_CAP_SDSI | VSEC_CAP_TPMI |
		VSEC_CAP_DISCOVERY,
	.deps = oobmsm_deps,
	.num_deps = ARRAY_SIZE(oobmsm_deps),
};

/* DMR OOBMSM info */
static const struct intel_vsec_platform_info dmr_oobmsm_info = {
	.caps = VSEC_CAP_TELEMETRY | VSEC_CAP_TPMI | VSEC_CAP_DISCOVERY,
	.deps = oobmsm_deps,
	.num_deps = ARRAY_SIZE(oobmsm_deps),
};

/* TGL info */
static const struct intel_vsec_platform_info tgl_info = {
	.caps = VSEC_CAP_TELEMETRY,
	.quirks = VSEC_QUIRK_TABLE_SHIFT | VSEC_QUIRK_EARLY_HW,
};

/* LNL info */
static const struct intel_vsec_platform_info lnl_info = {
	.caps = VSEC_CAP_TELEMETRY | VSEC_CAP_WATCHER,
};

#define PCI_DEVICE_ID_INTEL_VSEC_ADL		0x467d
#define PCI_DEVICE_ID_INTEL_VSEC_DG1		0x490e
#define PCI_DEVICE_ID_INTEL_VSEC_MTL_M		0x7d0d
#define PCI_DEVICE_ID_INTEL_VSEC_MTL_S		0xad0d
#define PCI_DEVICE_ID_INTEL_VSEC_OOBMSM		0x09a7
#define PCI_DEVICE_ID_INTEL_VSEC_OOBMSM_DMR	0x09a1
#define PCI_DEVICE_ID_INTEL_VSEC_RPL		0xa77d
#define PCI_DEVICE_ID_INTEL_VSEC_TGL		0x9a0d
#define PCI_DEVICE_ID_INTEL_VSEC_LNL_M		0x647d
#define PCI_DEVICE_ID_INTEL_VSEC_PTL		0xb07d
static const struct pci_device_id intel_vsec_pci_ids[] = {
	{ PCI_DEVICE_DATA(INTEL, VSEC_ADL, &tgl_info) },
	{ PCI_DEVICE_DATA(INTEL, VSEC_DG1, &dg1_info) },
	{ PCI_DEVICE_DATA(INTEL, VSEC_MTL_M, &mtl_info) },
	{ PCI_DEVICE_DATA(INTEL, VSEC_MTL_S, &mtl_info) },
	{ PCI_DEVICE_DATA(INTEL, VSEC_OOBMSM, &oobmsm_info) },
	{ PCI_DEVICE_DATA(INTEL, VSEC_OOBMSM_DMR, &dmr_oobmsm_info) },
	{ PCI_DEVICE_DATA(INTEL, VSEC_RPL, &tgl_info) },
	{ PCI_DEVICE_DATA(INTEL, VSEC_TGL, &tgl_info) },
	{ PCI_DEVICE_DATA(INTEL, VSEC_LNL_M, &lnl_info) },
	{ PCI_DEVICE_DATA(INTEL, VSEC_PTL, &mtl_info) },
	{ }
};
MODULE_DEVICE_TABLE(pci, intel_vsec_pci_ids);

static pci_ers_result_t intel_vsec_pci_error_detected(struct pci_dev *pdev,
						      pci_channel_state_t state)
{
	pci_ers_result_t status = PCI_ERS_RESULT_NEED_RESET;

	dev_info(&pdev->dev, "PCI error detected, state %d", state);

	if (state == pci_channel_io_perm_failure)
		status = PCI_ERS_RESULT_DISCONNECT;
	else
		pci_disable_device(pdev);

	return status;
}

static pci_ers_result_t intel_vsec_pci_slot_reset(struct pci_dev *pdev)
{
	struct intel_vsec_device *intel_vsec_dev;
	pci_ers_result_t status = PCI_ERS_RESULT_DISCONNECT;
	const struct pci_device_id *pci_dev_id;
	unsigned long index;

	dev_info(&pdev->dev, "Resetting PCI slot\n");

	msleep(2000);
	if (pci_enable_device(pdev)) {
		dev_info(&pdev->dev,
			 "Failed to re-enable PCI device after reset.\n");
		goto out;
	}

	status = PCI_ERS_RESULT_RECOVERED;

	xa_for_each(&auxdev_array, index, intel_vsec_dev) {
		/* check if pdev doesn't match */
		if (pdev != intel_vsec_dev->pcidev)
			continue;
		devm_release_action(&pdev->dev, intel_vsec_remove_aux,
				    &intel_vsec_dev->auxdev);
	}
	pci_disable_device(pdev);
	pci_restore_state(pdev);
	pci_dev_id = pci_match_id(intel_vsec_pci_ids, pdev);
	intel_vsec_pci_probe(pdev, pci_dev_id);

out:
	return status;
}

static void intel_vsec_pci_resume(struct pci_dev *pdev)
{
	dev_info(&pdev->dev, "Done resuming PCI device\n");
}

static const struct pci_error_handlers intel_vsec_pci_err_handlers = {
	.error_detected = intel_vsec_pci_error_detected,
	.slot_reset = intel_vsec_pci_slot_reset,
	.resume = intel_vsec_pci_resume,
};

static struct pci_driver intel_vsec_pci_driver = {
	.name = "intel_vsec",
	.id_table = intel_vsec_pci_ids,
	.probe = intel_vsec_pci_probe,
	.err_handler = &intel_vsec_pci_err_handlers,
};
module_pci_driver(intel_vsec_pci_driver);

MODULE_AUTHOR("David E. Box <david.e.box@linux.intel.com>");
MODULE_DESCRIPTION("Intel Extended Capabilities auxiliary bus driver");
MODULE_LICENSE("GPL v2");

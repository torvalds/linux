// SPDX-License-Identifier: GPL-2.0
/*
 * Virtual I/O topology
 *
 * The Virtual I/O Translation Table (VIOT) describes the topology of
 * para-virtual IOMMUs and the endpoints they manage. The OS uses it to
 * initialize devices in the right order, preventing endpoints from issuing DMA
 * before their IOMMU is ready.
 *
 * When binding a driver to a device, before calling the device driver's probe()
 * method, the driver infrastructure calls dma_configure(). At that point the
 * VIOT driver looks for an IOMMU associated to the device in the VIOT table.
 * If an IOMMU exists and has been initialized, the VIOT driver initializes the
 * device's IOMMU fwspec, allowing the DMA infrastructure to invoke the IOMMU
 * ops when the device driver configures DMA mappings. If an IOMMU exists and
 * hasn't yet been initialized, VIOT returns -EPROBE_DEFER to postpone probing
 * the device until the IOMMU is available.
 */
#define pr_fmt(fmt) "ACPI: VIOT: " fmt

#include <linux/acpi_viot.h>
#include <linux/fwnode.h>
#include <linux/iommu.h>
#include <linux/list.h>
#include <linux/pci.h>
#include <linux/platform_device.h>

struct viot_iommu {
	/* Node offset within the table */
	unsigned int			offset;
	struct fwnode_handle		*fwnode;
	struct list_head		list;
};

struct viot_endpoint {
	union {
		/* PCI range */
		struct {
			u16		segment_start;
			u16		segment_end;
			u16		bdf_start;
			u16		bdf_end;
		};
		/* MMIO */
		u64			address;
	};
	u32				endpoint_id;
	struct viot_iommu		*viommu;
	struct list_head		list;
};

static struct acpi_table_viot *viot;
static LIST_HEAD(viot_iommus);
static LIST_HEAD(viot_pci_ranges);
static LIST_HEAD(viot_mmio_endpoints);

static int __init viot_check_bounds(const struct acpi_viot_header *hdr)
{
	struct acpi_viot_header *start, *end, *hdr_end;

	start = ACPI_ADD_PTR(struct acpi_viot_header, viot,
			     max_t(size_t, sizeof(*viot), viot->node_offset));
	end = ACPI_ADD_PTR(struct acpi_viot_header, viot, viot->header.length);
	hdr_end = ACPI_ADD_PTR(struct acpi_viot_header, hdr, sizeof(*hdr));

	if (hdr < start || hdr_end > end) {
		pr_err(FW_BUG "Node pointer overflows\n");
		return -EOVERFLOW;
	}
	if (hdr->length < sizeof(*hdr)) {
		pr_err(FW_BUG "Empty node\n");
		return -EINVAL;
	}
	return 0;
}

static int __init viot_get_pci_iommu_fwnode(struct viot_iommu *viommu,
					    u16 segment, u16 bdf)
{
	struct pci_dev *pdev;
	struct fwnode_handle *fwnode;

	pdev = pci_get_domain_bus_and_slot(segment, PCI_BUS_NUM(bdf),
					   bdf & 0xff);
	if (!pdev) {
		pr_err("Could not find PCI IOMMU\n");
		return -ENODEV;
	}

	fwnode = dev_fwnode(&pdev->dev);
	if (!fwnode) {
		/*
		 * PCI devices aren't necessarily described by ACPI. Create a
		 * fwnode so the IOMMU subsystem can identify this device.
		 */
		fwnode = acpi_alloc_fwnode_static();
		if (!fwnode) {
			pci_dev_put(pdev);
			return -ENOMEM;
		}
		set_primary_fwnode(&pdev->dev, fwnode);
	}
	viommu->fwnode = dev_fwnode(&pdev->dev);
	pci_dev_put(pdev);
	return 0;
}

static int __init viot_get_mmio_iommu_fwnode(struct viot_iommu *viommu,
					     u64 address)
{
	struct acpi_device *adev;
	struct resource res = {
		.start	= address,
		.end	= address,
		.flags	= IORESOURCE_MEM,
	};

	adev = acpi_resource_consumer(&res);
	if (!adev) {
		pr_err("Could not find MMIO IOMMU\n");
		return -EINVAL;
	}
	viommu->fwnode = &adev->fwnode;
	return 0;
}

static struct viot_iommu * __init viot_get_iommu(unsigned int offset)
{
	int ret;
	struct viot_iommu *viommu;
	struct acpi_viot_header *hdr = ACPI_ADD_PTR(struct acpi_viot_header,
						    viot, offset);
	union {
		struct acpi_viot_virtio_iommu_pci pci;
		struct acpi_viot_virtio_iommu_mmio mmio;
	} *node = (void *)hdr;

	list_for_each_entry(viommu, &viot_iommus, list)
		if (viommu->offset == offset)
			return viommu;

	if (viot_check_bounds(hdr))
		return NULL;

	viommu = kzalloc(sizeof(*viommu), GFP_KERNEL);
	if (!viommu)
		return NULL;

	viommu->offset = offset;
	switch (hdr->type) {
	case ACPI_VIOT_NODE_VIRTIO_IOMMU_PCI:
		if (hdr->length < sizeof(node->pci))
			goto err_free;

		ret = viot_get_pci_iommu_fwnode(viommu, node->pci.segment,
						node->pci.bdf);
		break;
	case ACPI_VIOT_NODE_VIRTIO_IOMMU_MMIO:
		if (hdr->length < sizeof(node->mmio))
			goto err_free;

		ret = viot_get_mmio_iommu_fwnode(viommu,
						 node->mmio.base_address);
		break;
	default:
		ret = -EINVAL;
	}
	if (ret)
		goto err_free;

	list_add(&viommu->list, &viot_iommus);
	return viommu;

err_free:
	kfree(viommu);
	return NULL;
}

static int __init viot_parse_node(const struct acpi_viot_header *hdr)
{
	int ret = -EINVAL;
	struct list_head *list;
	struct viot_endpoint *ep;
	union {
		struct acpi_viot_mmio mmio;
		struct acpi_viot_pci_range pci;
	} *node = (void *)hdr;

	if (viot_check_bounds(hdr))
		return -EINVAL;

	if (hdr->type == ACPI_VIOT_NODE_VIRTIO_IOMMU_PCI ||
	    hdr->type == ACPI_VIOT_NODE_VIRTIO_IOMMU_MMIO)
		return 0;

	ep = kzalloc(sizeof(*ep), GFP_KERNEL);
	if (!ep)
		return -ENOMEM;

	switch (hdr->type) {
	case ACPI_VIOT_NODE_PCI_RANGE:
		if (hdr->length < sizeof(node->pci)) {
			pr_err(FW_BUG "Invalid PCI node size\n");
			goto err_free;
		}

		ep->segment_start = node->pci.segment_start;
		ep->segment_end = node->pci.segment_end;
		ep->bdf_start = node->pci.bdf_start;
		ep->bdf_end = node->pci.bdf_end;
		ep->endpoint_id = node->pci.endpoint_start;
		ep->viommu = viot_get_iommu(node->pci.output_node);
		list = &viot_pci_ranges;
		break;
	case ACPI_VIOT_NODE_MMIO:
		if (hdr->length < sizeof(node->mmio)) {
			pr_err(FW_BUG "Invalid MMIO node size\n");
			goto err_free;
		}

		ep->address = node->mmio.base_address;
		ep->endpoint_id = node->mmio.endpoint;
		ep->viommu = viot_get_iommu(node->mmio.output_node);
		list = &viot_mmio_endpoints;
		break;
	default:
		pr_warn("Unsupported node %x\n", hdr->type);
		ret = 0;
		goto err_free;
	}

	if (!ep->viommu) {
		pr_warn("No IOMMU node found\n");
		/*
		 * A future version of the table may use the node for other
		 * purposes. Keep parsing.
		 */
		ret = 0;
		goto err_free;
	}

	list_add(&ep->list, list);
	return 0;

err_free:
	kfree(ep);
	return ret;
}

/**
 * acpi_viot_early_init - Test the presence of VIOT and enable ACS
 *
 * If the VIOT does exist, ACS must be enabled. This cannot be
 * done in acpi_viot_init() which is called after the bus scan
 */
void __init acpi_viot_early_init(void)
{
#ifdef CONFIG_PCI
	acpi_status status;
	struct acpi_table_header *hdr;

	status = acpi_get_table(ACPI_SIG_VIOT, 0, &hdr);
	if (ACPI_FAILURE(status))
		return;
	pci_request_acs();
	acpi_put_table(hdr);
#endif
}

/**
 * acpi_viot_init - Parse the VIOT table
 *
 * Parse the VIOT table, prepare the list of endpoints to be used during DMA
 * setup of devices.
 */
void __init acpi_viot_init(void)
{
	int i;
	acpi_status status;
	struct acpi_table_header *hdr;
	struct acpi_viot_header *node;

	status = acpi_get_table(ACPI_SIG_VIOT, 0, &hdr);
	if (ACPI_FAILURE(status)) {
		if (status != AE_NOT_FOUND) {
			const char *msg = acpi_format_exception(status);

			pr_err("Failed to get table, %s\n", msg);
		}
		return;
	}

	viot = (void *)hdr;

	node = ACPI_ADD_PTR(struct acpi_viot_header, viot, viot->node_offset);
	for (i = 0; i < viot->node_count; i++) {
		if (viot_parse_node(node))
			return;

		node = ACPI_ADD_PTR(struct acpi_viot_header, node,
				    node->length);
	}

	acpi_put_table(hdr);
}

static int viot_dev_iommu_init(struct device *dev, struct viot_iommu *viommu,
			       u32 epid)
{
	const struct iommu_ops *ops;

	if (!viommu)
		return -ENODEV;

	/* We're not translating ourself */
	if (device_match_fwnode(dev, viommu->fwnode))
		return -EINVAL;

	ops = iommu_ops_from_fwnode(viommu->fwnode);
	if (!ops)
		return IS_ENABLED(CONFIG_VIRTIO_IOMMU) ?
			-EPROBE_DEFER : -ENODEV;

	return acpi_iommu_fwspec_init(dev, epid, viommu->fwnode, ops);
}

static int viot_pci_dev_iommu_init(struct pci_dev *pdev, u16 dev_id, void *data)
{
	u32 epid;
	struct viot_endpoint *ep;
	u32 domain_nr = pci_domain_nr(pdev->bus);

	list_for_each_entry(ep, &viot_pci_ranges, list) {
		if (domain_nr >= ep->segment_start &&
		    domain_nr <= ep->segment_end &&
		    dev_id >= ep->bdf_start &&
		    dev_id <= ep->bdf_end) {
			epid = ((domain_nr - ep->segment_start) << 16) +
				dev_id - ep->bdf_start + ep->endpoint_id;

			return viot_dev_iommu_init(&pdev->dev, ep->viommu,
						   epid);
		}
	}
	return -ENODEV;
}

static int viot_mmio_dev_iommu_init(struct platform_device *pdev)
{
	struct resource *mem;
	struct viot_endpoint *ep;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem)
		return -ENODEV;

	list_for_each_entry(ep, &viot_mmio_endpoints, list) {
		if (ep->address == mem->start)
			return viot_dev_iommu_init(&pdev->dev, ep->viommu,
						   ep->endpoint_id);
	}
	return -ENODEV;
}

/**
 * viot_iommu_configure - Setup IOMMU ops for an endpoint described by VIOT
 * @dev: the endpoint
 *
 * Return: 0 on success, <0 on failure
 */
int viot_iommu_configure(struct device *dev)
{
	if (dev_is_pci(dev))
		return pci_for_each_dma_alias(to_pci_dev(dev),
					      viot_pci_dev_iommu_init, NULL);
	else if (dev_is_platform(dev))
		return viot_mmio_dev_iommu_init(to_platform_device(dev));
	return -ENODEV;
}

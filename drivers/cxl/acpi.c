// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2021 Intel Corporation. All rights reserved. */
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/acpi.h>
#include <linux/pci.h>
#include "cxlpci.h"
#include "cxl.h"

static unsigned long cfmws_to_decoder_flags(int restrictions)
{
	unsigned long flags = CXL_DECODER_F_ENABLE;

	if (restrictions & ACPI_CEDT_CFMWS_RESTRICT_TYPE2)
		flags |= CXL_DECODER_F_TYPE2;
	if (restrictions & ACPI_CEDT_CFMWS_RESTRICT_TYPE3)
		flags |= CXL_DECODER_F_TYPE3;
	if (restrictions & ACPI_CEDT_CFMWS_RESTRICT_VOLATILE)
		flags |= CXL_DECODER_F_RAM;
	if (restrictions & ACPI_CEDT_CFMWS_RESTRICT_PMEM)
		flags |= CXL_DECODER_F_PMEM;
	if (restrictions & ACPI_CEDT_CFMWS_RESTRICT_FIXED)
		flags |= CXL_DECODER_F_LOCK;

	return flags;
}

static int cxl_acpi_cfmws_verify(struct device *dev,
				 struct acpi_cedt_cfmws *cfmws)
{
	int rc, expected_len;
	unsigned int ways;

	if (cfmws->interleave_arithmetic != ACPI_CEDT_CFMWS_ARITHMETIC_MODULO) {
		dev_err(dev, "CFMWS Unsupported Interleave Arithmetic\n");
		return -EINVAL;
	}

	if (!IS_ALIGNED(cfmws->base_hpa, SZ_256M)) {
		dev_err(dev, "CFMWS Base HPA not 256MB aligned\n");
		return -EINVAL;
	}

	if (!IS_ALIGNED(cfmws->window_size, SZ_256M)) {
		dev_err(dev, "CFMWS Window Size not 256MB aligned\n");
		return -EINVAL;
	}

	rc = cxl_to_ways(cfmws->interleave_ways, &ways);
	if (rc) {
		dev_err(dev, "CFMWS Interleave Ways (%d) invalid\n",
			cfmws->interleave_ways);
		return -EINVAL;
	}

	expected_len = struct_size(cfmws, interleave_targets, ways);

	if (cfmws->header.length < expected_len) {
		dev_err(dev, "CFMWS length %d less than expected %d\n",
			cfmws->header.length, expected_len);
		return -EINVAL;
	}

	if (cfmws->header.length > expected_len)
		dev_dbg(dev, "CFMWS length %d greater than expected %d\n",
			cfmws->header.length, expected_len);

	return 0;
}

struct cxl_cfmws_context {
	struct device *dev;
	struct cxl_port *root_port;
	struct resource *cxl_res;
	int id;
};

static int cxl_parse_cfmws(union acpi_subtable_headers *header, void *arg,
			   const unsigned long end)
{
	int target_map[CXL_DECODER_MAX_INTERLEAVE];
	struct cxl_cfmws_context *ctx = arg;
	struct cxl_port *root_port = ctx->root_port;
	struct resource *cxl_res = ctx->cxl_res;
	struct cxl_root_decoder *cxlrd;
	struct device *dev = ctx->dev;
	struct acpi_cedt_cfmws *cfmws;
	struct cxl_decoder *cxld;
	unsigned int ways, i, ig;
	struct resource *res;
	int rc;

	cfmws = (struct acpi_cedt_cfmws *) header;

	rc = cxl_acpi_cfmws_verify(dev, cfmws);
	if (rc) {
		dev_err(dev, "CFMWS range %#llx-%#llx not registered\n",
			cfmws->base_hpa,
			cfmws->base_hpa + cfmws->window_size - 1);
		return 0;
	}

	rc = cxl_to_ways(cfmws->interleave_ways, &ways);
	if (rc)
		return rc;
	rc = cxl_to_granularity(cfmws->granularity, &ig);
	if (rc)
		return rc;
	for (i = 0; i < ways; i++)
		target_map[i] = cfmws->interleave_targets[i];

	res = kzalloc(sizeof(*res), GFP_KERNEL);
	if (!res)
		return -ENOMEM;

	res->name = kasprintf(GFP_KERNEL, "CXL Window %d", ctx->id++);
	if (!res->name)
		goto err_name;

	res->start = cfmws->base_hpa;
	res->end = cfmws->base_hpa + cfmws->window_size - 1;
	res->flags = IORESOURCE_MEM;

	/* add to the local resource tracking to establish a sort order */
	rc = insert_resource(cxl_res, res);
	if (rc)
		goto err_insert;

	cxlrd = cxl_root_decoder_alloc(root_port, ways);
	if (IS_ERR(cxlrd))
		return 0;

	cxld = &cxlrd->cxlsd.cxld;
	cxld->flags = cfmws_to_decoder_flags(cfmws->restrictions);
	cxld->target_type = CXL_DECODER_EXPANDER;
	cxld->hpa_range = (struct range) {
		.start = res->start,
		.end = res->end,
	};
	cxld->interleave_ways = ways;
	/*
	 * Minimize the x1 granularity to advertise support for any
	 * valid region granularity
	 */
	if (ways == 1)
		ig = CXL_DECODER_MIN_GRANULARITY;
	cxld->interleave_granularity = ig;

	rc = cxl_decoder_add(cxld, target_map);
	if (rc)
		put_device(&cxld->dev);
	else
		rc = cxl_decoder_autoremove(dev, cxld);
	if (rc) {
		dev_err(dev, "Failed to add decode range [%#llx - %#llx]\n",
			cxld->hpa_range.start, cxld->hpa_range.end);
		return 0;
	}
	dev_dbg(dev, "add: %s node: %d range [%#llx - %#llx]\n",
		dev_name(&cxld->dev),
		phys_to_target_node(cxld->hpa_range.start),
		cxld->hpa_range.start, cxld->hpa_range.end);

	return 0;

err_insert:
	kfree(res->name);
err_name:
	kfree(res);
	return -ENOMEM;
}

__mock struct acpi_device *to_cxl_host_bridge(struct device *host,
					      struct device *dev)
{
	struct acpi_device *adev = to_acpi_device(dev);

	if (!acpi_pci_find_root(adev->handle))
		return NULL;

	if (strcmp(acpi_device_hid(adev), "ACPI0016") == 0)
		return adev;
	return NULL;
}

/*
 * A host bridge is a dport to a CFMWS decode and it is a uport to the
 * dport (PCIe Root Ports) in the host bridge.
 */
static int add_host_bridge_uport(struct device *match, void *arg)
{
	struct cxl_port *root_port = arg;
	struct device *host = root_port->dev.parent;
	struct acpi_device *bridge = to_cxl_host_bridge(host, match);
	struct acpi_pci_root *pci_root;
	struct cxl_dport *dport;
	struct cxl_port *port;
	int rc;

	if (!bridge)
		return 0;

	dport = cxl_find_dport_by_dev(root_port, match);
	if (!dport) {
		dev_dbg(host, "host bridge expected and not found\n");
		return 0;
	}

	/*
	 * Note that this lookup already succeeded in
	 * to_cxl_host_bridge(), so no need to check for failure here
	 */
	pci_root = acpi_pci_find_root(bridge->handle);
	rc = devm_cxl_register_pci_bus(host, match, pci_root->bus);
	if (rc)
		return rc;

	port = devm_cxl_add_port(host, match, dport->component_reg_phys, dport);
	if (IS_ERR(port))
		return PTR_ERR(port);
	dev_dbg(host, "%s: add: %s\n", dev_name(match), dev_name(&port->dev));

	return 0;
}

struct cxl_chbs_context {
	struct device *dev;
	unsigned long long uid;
	resource_size_t chbcr;
};

static int cxl_get_chbcr(union acpi_subtable_headers *header, void *arg,
			 const unsigned long end)
{
	struct cxl_chbs_context *ctx = arg;
	struct acpi_cedt_chbs *chbs;

	if (ctx->chbcr)
		return 0;

	chbs = (struct acpi_cedt_chbs *) header;

	if (ctx->uid != chbs->uid)
		return 0;
	ctx->chbcr = chbs->base;

	return 0;
}

static int add_host_bridge_dport(struct device *match, void *arg)
{
	acpi_status status;
	unsigned long long uid;
	struct cxl_dport *dport;
	struct cxl_chbs_context ctx;
	struct cxl_port *root_port = arg;
	struct device *host = root_port->dev.parent;
	struct acpi_device *bridge = to_cxl_host_bridge(host, match);

	if (!bridge)
		return 0;

	status = acpi_evaluate_integer(bridge->handle, METHOD_NAME__UID, NULL,
				       &uid);
	if (status != AE_OK) {
		dev_err(host, "unable to retrieve _UID of %s\n",
			dev_name(match));
		return -ENODEV;
	}

	ctx = (struct cxl_chbs_context) {
		.dev = host,
		.uid = uid,
	};
	acpi_table_parse_cedt(ACPI_CEDT_TYPE_CHBS, cxl_get_chbcr, &ctx);

	if (ctx.chbcr == 0) {
		dev_warn(host, "No CHBS found for Host Bridge: %s\n",
			 dev_name(match));
		return 0;
	}

	dport = devm_cxl_add_dport(root_port, match, uid, ctx.chbcr);
	if (IS_ERR(dport)) {
		dev_err(host, "failed to add downstream port: %s\n",
			dev_name(match));
		return PTR_ERR(dport);
	}
	dev_dbg(host, "add dport%llu: %s\n", uid, dev_name(match));
	return 0;
}

static int add_root_nvdimm_bridge(struct device *match, void *data)
{
	struct cxl_decoder *cxld;
	struct cxl_port *root_port = data;
	struct cxl_nvdimm_bridge *cxl_nvb;
	struct device *host = root_port->dev.parent;

	if (!is_root_decoder(match))
		return 0;

	cxld = to_cxl_decoder(match);
	if (!(cxld->flags & CXL_DECODER_F_PMEM))
		return 0;

	cxl_nvb = devm_cxl_add_nvdimm_bridge(host, root_port);
	if (IS_ERR(cxl_nvb)) {
		dev_dbg(host, "failed to register pmem\n");
		return PTR_ERR(cxl_nvb);
	}
	dev_dbg(host, "%s: add: %s\n", dev_name(&root_port->dev),
		dev_name(&cxl_nvb->dev));
	return 1;
}

static struct lock_class_key cxl_root_key;

static void cxl_acpi_lock_reset_class(void *dev)
{
	device_lock_reset_class(dev);
}

static void del_cxl_resource(struct resource *res)
{
	kfree(res->name);
	kfree(res);
}

static void cxl_set_public_resource(struct resource *priv, struct resource *pub)
{
	priv->desc = (unsigned long) pub;
}

static struct resource *cxl_get_public_resource(struct resource *priv)
{
	return (struct resource *) priv->desc;
}

static void remove_cxl_resources(void *data)
{
	struct resource *res, *next, *cxl = data;

	for (res = cxl->child; res; res = next) {
		struct resource *victim = cxl_get_public_resource(res);

		next = res->sibling;
		remove_resource(res);

		if (victim) {
			remove_resource(victim);
			kfree(victim);
		}

		del_cxl_resource(res);
	}
}

/**
 * add_cxl_resources() - reflect CXL fixed memory windows in iomem_resource
 * @cxl_res: A standalone resource tree where each CXL window is a sibling
 *
 * Walk each CXL window in @cxl_res and add it to iomem_resource potentially
 * expanding its boundaries to ensure that any conflicting resources become
 * children. If a window is expanded it may then conflict with a another window
 * entry and require the window to be truncated or trimmed. Consider this
 * situation:
 *
 * |-- "CXL Window 0" --||----- "CXL Window 1" -----|
 * |--------------- "System RAM" -------------|
 *
 * ...where platform firmware has established as System RAM resource across 2
 * windows, but has left some portion of window 1 for dynamic CXL region
 * provisioning. In this case "Window 0" will span the entirety of the "System
 * RAM" span, and "CXL Window 1" is truncated to the remaining tail past the end
 * of that "System RAM" resource.
 */
static int add_cxl_resources(struct resource *cxl_res)
{
	struct resource *res, *new, *next;

	for (res = cxl_res->child; res; res = next) {
		new = kzalloc(sizeof(*new), GFP_KERNEL);
		if (!new)
			return -ENOMEM;
		new->name = res->name;
		new->start = res->start;
		new->end = res->end;
		new->flags = IORESOURCE_MEM;
		new->desc = IORES_DESC_CXL;

		/*
		 * Record the public resource in the private cxl_res tree for
		 * later removal.
		 */
		cxl_set_public_resource(res, new);

		insert_resource_expand_to_fit(&iomem_resource, new);

		next = res->sibling;
		while (next && resource_overlaps(new, next)) {
			if (resource_contains(new, next)) {
				struct resource *_next = next->sibling;

				remove_resource(next);
				del_cxl_resource(next);
				next = _next;
			} else
				next->start = new->end + 1;
		}
	}
	return 0;
}

static int pair_cxl_resource(struct device *dev, void *data)
{
	struct resource *cxl_res = data;
	struct resource *p;

	if (!is_root_decoder(dev))
		return 0;

	for (p = cxl_res->child; p; p = p->sibling) {
		struct cxl_root_decoder *cxlrd = to_cxl_root_decoder(dev);
		struct cxl_decoder *cxld = &cxlrd->cxlsd.cxld;
		struct resource res = {
			.start = cxld->hpa_range.start,
			.end = cxld->hpa_range.end,
			.flags = IORESOURCE_MEM,
		};

		if (resource_contains(p, &res)) {
			cxlrd->res = cxl_get_public_resource(p);
			break;
		}
	}

	return 0;
}

static int cxl_acpi_probe(struct platform_device *pdev)
{
	int rc;
	struct resource *cxl_res;
	struct cxl_port *root_port;
	struct device *host = &pdev->dev;
	struct acpi_device *adev = ACPI_COMPANION(host);
	struct cxl_cfmws_context ctx;

	device_lock_set_class(&pdev->dev, &cxl_root_key);
	rc = devm_add_action_or_reset(&pdev->dev, cxl_acpi_lock_reset_class,
				      &pdev->dev);
	if (rc)
		return rc;

	cxl_res = devm_kzalloc(host, sizeof(*cxl_res), GFP_KERNEL);
	if (!cxl_res)
		return -ENOMEM;
	cxl_res->name = "CXL mem";
	cxl_res->start = 0;
	cxl_res->end = -1;
	cxl_res->flags = IORESOURCE_MEM;

	root_port = devm_cxl_add_port(host, host, CXL_RESOURCE_NONE, NULL);
	if (IS_ERR(root_port))
		return PTR_ERR(root_port);
	dev_dbg(host, "add: %s\n", dev_name(&root_port->dev));

	rc = bus_for_each_dev(adev->dev.bus, NULL, root_port,
			      add_host_bridge_dport);
	if (rc < 0)
		return rc;

	rc = devm_add_action_or_reset(host, remove_cxl_resources, cxl_res);
	if (rc)
		return rc;

	ctx = (struct cxl_cfmws_context) {
		.dev = host,
		.root_port = root_port,
		.cxl_res = cxl_res,
	};
	rc = acpi_table_parse_cedt(ACPI_CEDT_TYPE_CFMWS, cxl_parse_cfmws, &ctx);
	if (rc < 0)
		return -ENXIO;

	rc = add_cxl_resources(cxl_res);
	if (rc)
		return rc;

	/*
	 * Populate the root decoders with their related iomem resource,
	 * if present
	 */
	device_for_each_child(&root_port->dev, cxl_res, pair_cxl_resource);

	/*
	 * Root level scanned with host-bridge as dports, now scan host-bridges
	 * for their role as CXL uports to their CXL-capable PCIe Root Ports.
	 */
	rc = bus_for_each_dev(adev->dev.bus, NULL, root_port,
			      add_host_bridge_uport);
	if (rc < 0)
		return rc;

	if (IS_ENABLED(CONFIG_CXL_PMEM))
		rc = device_for_each_child(&root_port->dev, root_port,
					   add_root_nvdimm_bridge);
	if (rc < 0)
		return rc;

	/* In case PCI is scanned before ACPI re-trigger memdev attach */
	return cxl_bus_rescan();
}

static const struct acpi_device_id cxl_acpi_ids[] = {
	{ "ACPI0017" },
	{ },
};
MODULE_DEVICE_TABLE(acpi, cxl_acpi_ids);

static const struct platform_device_id cxl_test_ids[] = {
	{ "cxl_acpi" },
	{ },
};
MODULE_DEVICE_TABLE(platform, cxl_test_ids);

static struct platform_driver cxl_acpi_driver = {
	.probe = cxl_acpi_probe,
	.driver = {
		.name = KBUILD_MODNAME,
		.acpi_match_table = cxl_acpi_ids,
	},
	.id_table = cxl_test_ids,
};

module_platform_driver(cxl_acpi_driver);
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(CXL);
MODULE_IMPORT_NS(ACPI);

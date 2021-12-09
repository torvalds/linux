// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2021 Intel Corporation. All rights reserved. */
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/acpi.h>
#include <linux/pci.h>
#include "cxl.h"

static struct acpi_table_header *acpi_cedt;

/* Encode defined in CXL 2.0 8.2.5.12.7 HDM Decoder Control Register */
#define CFMWS_INTERLEAVE_WAYS(x)	(1 << (x)->interleave_ways)
#define CFMWS_INTERLEAVE_GRANULARITY(x)	((x)->granularity + 8)

static unsigned long cfmws_to_decoder_flags(int restrictions)
{
	unsigned long flags = 0;

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
	int expected_len;

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

	if (CFMWS_INTERLEAVE_WAYS(cfmws) > CXL_DECODER_MAX_INTERLEAVE) {
		dev_err(dev, "CFMWS Interleave Ways (%d) too large\n",
			CFMWS_INTERLEAVE_WAYS(cfmws));
		return -EINVAL;
	}

	expected_len = struct_size((cfmws), interleave_targets,
				   CFMWS_INTERLEAVE_WAYS(cfmws));

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

static void cxl_add_cfmws_decoders(struct device *dev,
				   struct cxl_port *root_port)
{
	int target_map[CXL_DECODER_MAX_INTERLEAVE];
	struct acpi_cedt_cfmws *cfmws;
	struct cxl_decoder *cxld;
	acpi_size len, cur = 0;
	void *cedt_subtable;
	int rc;

	len = acpi_cedt->length - sizeof(*acpi_cedt);
	cedt_subtable = acpi_cedt + 1;

	while (cur < len) {
		struct acpi_cedt_header *c = cedt_subtable + cur;
		int i;

		if (c->type != ACPI_CEDT_TYPE_CFMWS) {
			cur += c->length;
			continue;
		}

		cfmws = cedt_subtable + cur;

		if (cfmws->header.length < sizeof(*cfmws)) {
			dev_warn_once(dev,
				      "CFMWS entry skipped:invalid length:%u\n",
				      cfmws->header.length);
			cur += c->length;
			continue;
		}

		rc = cxl_acpi_cfmws_verify(dev, cfmws);
		if (rc) {
			dev_err(dev, "CFMWS range %#llx-%#llx not registered\n",
				cfmws->base_hpa, cfmws->base_hpa +
				cfmws->window_size - 1);
			cur += c->length;
			continue;
		}

		for (i = 0; i < CFMWS_INTERLEAVE_WAYS(cfmws); i++)
			target_map[i] = cfmws->interleave_targets[i];

		cxld = cxl_decoder_alloc(root_port,
					 CFMWS_INTERLEAVE_WAYS(cfmws));
		if (IS_ERR(cxld))
			goto next;

		cxld->flags = cfmws_to_decoder_flags(cfmws->restrictions);
		cxld->target_type = CXL_DECODER_EXPANDER;
		cxld->range = (struct range) {
			.start = cfmws->base_hpa,
			.end = cfmws->base_hpa + cfmws->window_size - 1,
		};
		cxld->interleave_ways = CFMWS_INTERLEAVE_WAYS(cfmws);
		cxld->interleave_granularity =
			CFMWS_INTERLEAVE_GRANULARITY(cfmws);

		rc = cxl_decoder_add(cxld, target_map);
		if (rc)
			put_device(&cxld->dev);
		else
			rc = cxl_decoder_autoremove(dev, cxld);
		if (rc) {
			dev_err(dev, "Failed to add decoder for %#llx-%#llx\n",
				cfmws->base_hpa, cfmws->base_hpa +
				cfmws->window_size - 1);
			goto next;
		}
		dev_dbg(dev, "add: %s range %#llx-%#llx\n",
			dev_name(&cxld->dev), cfmws->base_hpa,
			cfmws->base_hpa + cfmws->window_size - 1);
next:
		cur += c->length;
	}
}

static struct acpi_cedt_chbs *cxl_acpi_match_chbs(struct device *dev, u32 uid)
{
	struct acpi_cedt_chbs *chbs, *chbs_match = NULL;
	acpi_size len, cur = 0;
	void *cedt_subtable;

	len = acpi_cedt->length - sizeof(*acpi_cedt);
	cedt_subtable = acpi_cedt + 1;

	while (cur < len) {
		struct acpi_cedt_header *c = cedt_subtable + cur;

		if (c->type != ACPI_CEDT_TYPE_CHBS) {
			cur += c->length;
			continue;
		}

		chbs = cedt_subtable + cur;

		if (chbs->header.length < sizeof(*chbs)) {
			dev_warn_once(dev,
				      "CHBS entry skipped: invalid length:%u\n",
				      chbs->header.length);
			cur += c->length;
			continue;
		}

		if (chbs->uid != uid) {
			cur += c->length;
			continue;
		}

		if (chbs_match) {
			dev_warn_once(dev,
				      "CHBS entry skipped: duplicate UID:%u\n",
				      uid);
			cur += c->length;
			continue;
		}

		chbs_match = chbs;
		cur += c->length;
	}

	return chbs_match ? chbs_match : ERR_PTR(-ENODEV);
}

static resource_size_t get_chbcr(struct acpi_cedt_chbs *chbs)
{
	return IS_ERR(chbs) ? CXL_RESOURCE_NONE : chbs->base;
}

__mock int match_add_root_ports(struct pci_dev *pdev, void *data)
{
	struct cxl_walk_context *ctx = data;
	struct pci_bus *root_bus = ctx->root;
	struct cxl_port *port = ctx->port;
	int type = pci_pcie_type(pdev);
	struct device *dev = ctx->dev;
	u32 lnkcap, port_num;
	int rc;

	if (pdev->bus != root_bus)
		return 0;
	if (!pci_is_pcie(pdev))
		return 0;
	if (type != PCI_EXP_TYPE_ROOT_PORT)
		return 0;
	if (pci_read_config_dword(pdev, pci_pcie_cap(pdev) + PCI_EXP_LNKCAP,
				  &lnkcap) != PCIBIOS_SUCCESSFUL)
		return 0;

	/* TODO walk DVSEC to find component register base */
	port_num = FIELD_GET(PCI_EXP_LNKCAP_PN, lnkcap);
	rc = cxl_add_dport(port, &pdev->dev, port_num, CXL_RESOURCE_NONE);
	if (rc) {
		ctx->error = rc;
		return rc;
	}
	ctx->count++;

	dev_dbg(dev, "add dport%d: %s\n", port_num, dev_name(&pdev->dev));

	return 0;
}

static struct cxl_dport *find_dport_by_dev(struct cxl_port *port, struct device *dev)
{
	struct cxl_dport *dport;

	device_lock(&port->dev);
	list_for_each_entry(dport, &port->dports, list)
		if (dport->dport == dev) {
			device_unlock(&port->dev);
			return dport;
		}

	device_unlock(&port->dev);
	return NULL;
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
	struct cxl_walk_context ctx;
	int single_port_map[1], rc;
	struct cxl_decoder *cxld;
	struct cxl_dport *dport;
	struct cxl_port *port;

	if (!bridge)
		return 0;

	dport = find_dport_by_dev(root_port, match);
	if (!dport) {
		dev_dbg(host, "host bridge expected and not found\n");
		return 0;
	}

	port = devm_cxl_add_port(host, match, dport->component_reg_phys,
				 root_port);
	if (IS_ERR(port))
		return PTR_ERR(port);
	dev_dbg(host, "%s: add: %s\n", dev_name(match), dev_name(&port->dev));

	/*
	 * Note that this lookup already succeeded in
	 * to_cxl_host_bridge(), so no need to check for failure here
	 */
	pci_root = acpi_pci_find_root(bridge->handle);
	ctx = (struct cxl_walk_context){
		.dev = host,
		.root = pci_root->bus,
		.port = port,
	};
	pci_walk_bus(pci_root->bus, match_add_root_ports, &ctx);

	if (ctx.count == 0)
		return -ENODEV;
	if (ctx.error)
		return ctx.error;
	if (ctx.count > 1)
		return 0;

	/* TODO: Scan CHBCR for HDM Decoder resources */

	/*
	 * Per the CXL specification (8.2.5.12 CXL HDM Decoder Capability
	 * Structure) single ported host-bridges need not publish a decoder
	 * capability when a passthrough decode can be assumed, i.e. all
	 * transactions that the uport sees are claimed and passed to the single
	 * dport. Disable the range until the first CXL region is enumerated /
	 * activated.
	 */
	cxld = cxl_decoder_alloc(port, 1);
	if (IS_ERR(cxld))
		return PTR_ERR(cxld);

	cxld->interleave_ways = 1;
	cxld->interleave_granularity = PAGE_SIZE;
	cxld->target_type = CXL_DECODER_EXPANDER;
	cxld->range = (struct range) {
		.start = 0,
		.end = -1,
	};

	device_lock(&port->dev);
	dport = list_first_entry(&port->dports, typeof(*dport), list);
	device_unlock(&port->dev);

	single_port_map[0] = dport->port_id;

	rc = cxl_decoder_add(cxld, single_port_map);
	if (rc)
		put_device(&cxld->dev);
	else
		rc = cxl_decoder_autoremove(host, cxld);

	if (rc == 0)
		dev_dbg(host, "add: %s\n", dev_name(&cxld->dev));
	return rc;
}

static int add_host_bridge_dport(struct device *match, void *arg)
{
	int rc;
	acpi_status status;
	unsigned long long uid;
	struct acpi_cedt_chbs *chbs;
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

	chbs = cxl_acpi_match_chbs(host, uid);
	if (IS_ERR(chbs)) {
		dev_warn(host, "No CHBS found for Host Bridge: %s\n",
			 dev_name(match));
		return 0;
	}

	rc = cxl_add_dport(root_port, match, uid, get_chbcr(chbs));
	if (rc) {
		dev_err(host, "failed to add downstream port: %s\n",
			dev_name(match));
		return rc;
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

static u32 cedt_instance(struct platform_device *pdev)
{
	const bool *native_acpi0017 = acpi_device_get_match_data(&pdev->dev);

	if (native_acpi0017 && *native_acpi0017)
		return 0;

	/* for cxl_test request a non-canonical instance */
	return U32_MAX;
}

static int cxl_acpi_probe(struct platform_device *pdev)
{
	int rc;
	acpi_status status;
	struct cxl_port *root_port;
	struct device *host = &pdev->dev;
	struct acpi_device *adev = ACPI_COMPANION(host);

	root_port = devm_cxl_add_port(host, host, CXL_RESOURCE_NONE, NULL);
	if (IS_ERR(root_port))
		return PTR_ERR(root_port);
	dev_dbg(host, "add: %s\n", dev_name(&root_port->dev));

	status = acpi_get_table(ACPI_SIG_CEDT, cedt_instance(pdev), &acpi_cedt);
	if (ACPI_FAILURE(status))
		return -ENXIO;

	rc = bus_for_each_dev(adev->dev.bus, NULL, root_port,
			      add_host_bridge_dport);
	if (rc)
		goto out;

	cxl_add_cfmws_decoders(host, root_port);

	/*
	 * Root level scanned with host-bridge as dports, now scan host-bridges
	 * for their role as CXL uports to their CXL-capable PCIe Root Ports.
	 */
	rc = bus_for_each_dev(adev->dev.bus, NULL, root_port,
			      add_host_bridge_uport);
	if (rc)
		goto out;

	if (IS_ENABLED(CONFIG_CXL_PMEM))
		rc = device_for_each_child(&root_port->dev, root_port,
					   add_root_nvdimm_bridge);

out:
	acpi_put_table(acpi_cedt);
	if (rc < 0)
		return rc;
	return 0;
}

static bool native_acpi0017 = true;

static const struct acpi_device_id cxl_acpi_ids[] = {
	{ "ACPI0017", (unsigned long) &native_acpi0017 },
	{ },
};
MODULE_DEVICE_TABLE(acpi, cxl_acpi_ids);

static struct platform_driver cxl_acpi_driver = {
	.probe = cxl_acpi_probe,
	.driver = {
		.name = KBUILD_MODNAME,
		.acpi_match_table = cxl_acpi_ids,
	},
};

module_platform_driver(cxl_acpi_driver);
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(CXL);

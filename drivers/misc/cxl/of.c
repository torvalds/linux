// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2015 IBM Corp.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>

#include "cxl.h"


static const __be32 *read_prop_string(const struct device_node *np,
				const char *prop_name)
{
	const __be32 *prop;

	prop = of_get_property(np, prop_name, NULL);
	if (cxl_verbose && prop)
		pr_info("%s: %s\n", prop_name, (char *) prop);
	return prop;
}

static const __be32 *read_prop_dword(const struct device_node *np,
				const char *prop_name, u32 *val)
{
	const __be32 *prop;

	prop = of_get_property(np, prop_name, NULL);
	if (prop)
		*val = be32_to_cpu(prop[0]);
	if (cxl_verbose && prop)
		pr_info("%s: %#x (%u)\n", prop_name, *val, *val);
	return prop;
}

static const __be64 *read_prop64_dword(const struct device_node *np,
				const char *prop_name, u64 *val)
{
	const __be64 *prop;

	prop = of_get_property(np, prop_name, NULL);
	if (prop)
		*val = be64_to_cpu(prop[0]);
	if (cxl_verbose && prop)
		pr_info("%s: %#llx (%llu)\n", prop_name, *val, *val);
	return prop;
}


static int read_handle(struct device_node *np, u64 *handle)
{
	const __be32 *prop;
	u64 size;

	/* Get address and size of the node */
	prop = of_get_address(np, 0, &size, NULL);
	if (size)
		return -EINVAL;

	/* Helper to read a big number; size is in cells (not bytes) */
	*handle = of_read_number(prop, of_n_addr_cells(np));
	return 0;
}

static int read_phys_addr(struct device_node *np, char *prop_name,
			struct cxl_afu *afu)
{
	int i, len, entry_size, naddr, nsize, type;
	u64 addr, size;
	const __be32 *prop;

	naddr = of_n_addr_cells(np);
	nsize = of_n_size_cells(np);

	prop = of_get_property(np, prop_name, &len);
	if (prop) {
		entry_size = naddr + nsize;
		for (i = 0; i < (len / 4); i += entry_size, prop += entry_size) {
			type = be32_to_cpu(prop[0]);
			addr = of_read_number(prop, naddr);
			size = of_read_number(&prop[naddr], nsize);
			switch (type) {
			case 0: /* unit address */
				afu->guest->handle = addr;
				break;
			case 1: /* p2 area */
				afu->guest->p2n_phys += addr;
				afu->guest->p2n_size = size;
				break;
			case 2: /* problem state area */
				afu->psn_phys += addr;
				afu->adapter->ps_size = size;
				break;
			default:
				pr_err("Invalid address type %d found in %s property of AFU\n",
					type, prop_name);
				return -EINVAL;
			}
			if (cxl_verbose)
				pr_info("%s: %#x %#llx (size %#llx)\n",
					prop_name, type, addr, size);
		}
	}
	return 0;
}

static int read_vpd(struct cxl *adapter, struct cxl_afu *afu)
{
	char vpd[256];
	int rc;
	size_t len = sizeof(vpd);

	memset(vpd, 0, len);

	if (adapter)
		rc = cxl_guest_read_adapter_vpd(adapter, vpd, len);
	else
		rc = cxl_guest_read_afu_vpd(afu, vpd, len);

	if (rc > 0) {
		cxl_dump_debug_buffer(vpd, rc);
		rc = 0;
	}
	return rc;
}

int cxl_of_read_afu_handle(struct cxl_afu *afu, struct device_node *afu_np)
{
	if (read_handle(afu_np, &afu->guest->handle))
		return -EINVAL;
	pr_devel("AFU handle: 0x%.16llx\n", afu->guest->handle);

	return 0;
}

int cxl_of_read_afu_properties(struct cxl_afu *afu, struct device_node *np)
{
	int i, len, rc;
	char *p;
	const __be32 *prop;
	u16 device_id, vendor_id;
	u32 val = 0, class_code;

	/* Properties are read in the same order as listed in PAPR */

	if (cxl_verbose) {
		pr_info("Dump of the 'ibm,coherent-platform-function' node properties:\n");

		prop = of_get_property(np, "compatible", &len);
		i = 0;
		while (i < len) {
			p = (char *) prop + i;
			pr_info("compatible: %s\n", p);
			i += strlen(p) + 1;
		}
		read_prop_string(np, "name");
	}

	rc = read_phys_addr(np, "reg", afu);
	if (rc)
		return rc;

	rc = read_phys_addr(np, "assigned-addresses", afu);
	if (rc)
		return rc;

	if (afu->psn_phys == 0)
		afu->psa = false;
	else
		afu->psa = true;

	if (cxl_verbose) {
		read_prop_string(np, "ibm,loc-code");
		read_prop_string(np, "device_type");
	}

	read_prop_dword(np, "ibm,#processes", &afu->max_procs_virtualised);

	if (cxl_verbose) {
		read_prop_dword(np, "ibm,scratchpad-size", &val);
		read_prop_dword(np, "ibm,programmable", &val);
		read_prop_string(np, "ibm,phandle");
		read_vpd(NULL, afu);
	}

	read_prop_dword(np, "ibm,max-ints-per-process", &afu->guest->max_ints);
	afu->irqs_max = afu->guest->max_ints;

	prop = read_prop_dword(np, "ibm,min-ints-per-process", &afu->pp_irqs);
	if (prop) {
		/* One extra interrupt for the PSL interrupt is already
		 * included. Remove it now to keep only AFU interrupts and
		 * match the native case.
		 */
		afu->pp_irqs--;
	}

	if (cxl_verbose) {
		read_prop_dword(np, "ibm,max-ints", &val);
		read_prop_dword(np, "ibm,vpd-size", &val);
	}

	read_prop64_dword(np, "ibm,error-buffer-size", &afu->eb_len);
	afu->eb_offset = 0;

	if (cxl_verbose)
		read_prop_dword(np, "ibm,config-record-type", &val);

	read_prop64_dword(np, "ibm,config-record-size", &afu->crs_len);
	afu->crs_offset = 0;

	read_prop_dword(np, "ibm,#config-records", &afu->crs_num);

	if (cxl_verbose) {
		for (i = 0; i < afu->crs_num; i++) {
			rc = cxl_ops->afu_cr_read16(afu, i, PCI_DEVICE_ID,
						&device_id);
			if (!rc)
				pr_info("record %d - device-id: %#x\n",
					i, device_id);
			rc = cxl_ops->afu_cr_read16(afu, i, PCI_VENDOR_ID,
						&vendor_id);
			if (!rc)
				pr_info("record %d - vendor-id: %#x\n",
					i, vendor_id);
			rc = cxl_ops->afu_cr_read32(afu, i, PCI_CLASS_REVISION,
						&class_code);
			if (!rc) {
				class_code >>= 8;
				pr_info("record %d - class-code: %#x\n",
					i, class_code);
			}
		}

		read_prop_dword(np, "ibm,function-number", &val);
		read_prop_dword(np, "ibm,privileged-function", &val);
		read_prop_dword(np, "vendor-id", &val);
		read_prop_dword(np, "device-id", &val);
		read_prop_dword(np, "revision-id", &val);
		read_prop_dword(np, "class-code", &val);
		read_prop_dword(np, "subsystem-vendor-id", &val);
		read_prop_dword(np, "subsystem-id", &val);
	}
	/*
	 * if "ibm,process-mmio" doesn't exist then per-process mmio is
	 * not supported
	 */
	val = 0;
	prop = read_prop_dword(np, "ibm,process-mmio", &val);
	if (prop && val == 1)
		afu->pp_psa = true;
	else
		afu->pp_psa = false;

	if (cxl_verbose) {
		read_prop_dword(np, "ibm,supports-aur", &val);
		read_prop_dword(np, "ibm,supports-csrp", &val);
		read_prop_dword(np, "ibm,supports-prr", &val);
	}

	prop = read_prop_dword(np, "ibm,function-error-interrupt", &val);
	if (prop)
		afu->serr_hwirq = val;

	pr_devel("AFU handle: %#llx\n", afu->guest->handle);
	pr_devel("p2n_phys: %#llx (size %#llx)\n",
		afu->guest->p2n_phys, afu->guest->p2n_size);
	pr_devel("psn_phys: %#llx (size %#llx)\n",
		afu->psn_phys, afu->adapter->ps_size);
	pr_devel("Max number of processes virtualised=%i\n",
		afu->max_procs_virtualised);
	pr_devel("Per-process irqs min=%i, max=%i\n", afu->pp_irqs,
		 afu->irqs_max);
	pr_devel("Slice error interrupt=%#lx\n", afu->serr_hwirq);

	return 0;
}

static int read_adapter_irq_config(struct cxl *adapter, struct device_node *np)
{
	const __be32 *ranges;
	int len, nranges, i;
	struct irq_avail *cur;

	ranges = of_get_property(np, "interrupt-ranges", &len);
	if (ranges == NULL || len < (2 * sizeof(int)))
		return -EINVAL;

	/*
	 * encoded array of two cells per entry, each cell encoded as
	 * with encode-int
	 */
	nranges = len / (2 * sizeof(int));
	if (nranges == 0 || (nranges * 2 * sizeof(int)) != len)
		return -EINVAL;

	adapter->guest->irq_avail = kcalloc(nranges, sizeof(struct irq_avail),
					    GFP_KERNEL);
	if (adapter->guest->irq_avail == NULL)
		return -ENOMEM;

	adapter->guest->irq_base_offset = be32_to_cpu(ranges[0]);
	for (i = 0; i < nranges; i++) {
		cur = &adapter->guest->irq_avail[i];
		cur->offset = be32_to_cpu(ranges[i * 2]);
		cur->range  = be32_to_cpu(ranges[i * 2 + 1]);
		cur->bitmap = bitmap_zalloc(cur->range, GFP_KERNEL);
		if (cur->bitmap == NULL)
			goto err;
		if (cur->offset < adapter->guest->irq_base_offset)
			adapter->guest->irq_base_offset = cur->offset;
		if (cxl_verbose)
			pr_info("available IRQ range: %#lx-%#lx (%lu)\n",
				cur->offset, cur->offset + cur->range - 1,
				cur->range);
	}
	adapter->guest->irq_nranges = nranges;
	spin_lock_init(&adapter->guest->irq_alloc_lock);

	return 0;
err:
	for (i--; i >= 0; i--) {
		cur = &adapter->guest->irq_avail[i];
		bitmap_free(cur->bitmap);
	}
	kfree(adapter->guest->irq_avail);
	adapter->guest->irq_avail = NULL;
	return -ENOMEM;
}

int cxl_of_read_adapter_handle(struct cxl *adapter, struct device_node *np)
{
	if (read_handle(np, &adapter->guest->handle))
		return -EINVAL;
	pr_devel("Adapter handle: 0x%.16llx\n", adapter->guest->handle);

	return 0;
}

int cxl_of_read_adapter_properties(struct cxl *adapter, struct device_node *np)
{
	int rc, len, naddr, i;
	char *p;
	const __be32 *prop;
	u32 val = 0;

	/* Properties are read in the same order as listed in PAPR */

	naddr = of_n_addr_cells(np);

	if (cxl_verbose) {
		pr_info("Dump of the 'ibm,coherent-platform-facility' node properties:\n");

		read_prop_dword(np, "#address-cells", &val);
		read_prop_dword(np, "#size-cells", &val);

		prop = of_get_property(np, "compatible", &len);
		i = 0;
		while (i < len) {
			p = (char *) prop + i;
			pr_info("compatible: %s\n", p);
			i += strlen(p) + 1;
		}
		read_prop_string(np, "name");
		read_prop_string(np, "model");

		prop = of_get_property(np, "reg", NULL);
		if (prop) {
			pr_info("reg: addr:%#llx size:%#x\n",
				of_read_number(prop, naddr),
				be32_to_cpu(prop[naddr]));
		}

		read_prop_string(np, "ibm,loc-code");
	}

	if ((rc = read_adapter_irq_config(adapter, np)))
		return rc;

	if (cxl_verbose) {
		read_prop_string(np, "device_type");
		read_prop_string(np, "ibm,phandle");
	}

	prop = read_prop_dword(np, "ibm,caia-version", &val);
	if (prop) {
		adapter->caia_major = (val & 0xFF00) >> 8;
		adapter->caia_minor = val & 0xFF;
	}

	prop = read_prop_dword(np, "ibm,psl-revision", &val);
	if (prop)
		adapter->psl_rev = val;

	prop = read_prop_string(np, "status");
	if (prop) {
		adapter->guest->status = kasprintf(GFP_KERNEL, "%s", (char *) prop);
		if (adapter->guest->status == NULL)
			return -ENOMEM;
	}

	prop = read_prop_dword(np, "vendor-id", &val);
	if (prop)
		adapter->guest->vendor = val;

	prop = read_prop_dword(np, "device-id", &val);
	if (prop)
		adapter->guest->device = val;

	if (cxl_verbose) {
		read_prop_dword(np, "ibm,privileged-facility", &val);
		read_prop_dword(np, "revision-id", &val);
		read_prop_dword(np, "class-code", &val);
	}

	prop = read_prop_dword(np, "subsystem-vendor-id", &val);
	if (prop)
		adapter->guest->subsystem_vendor = val;

	prop = read_prop_dword(np, "subsystem-id", &val);
	if (prop)
		adapter->guest->subsystem = val;

	if (cxl_verbose)
		read_vpd(adapter, NULL);

	return 0;
}

static void cxl_of_remove(struct platform_device *pdev)
{
	struct cxl *adapter;
	int afu;

	adapter = dev_get_drvdata(&pdev->dev);
	for (afu = 0; afu < adapter->slices; afu++)
		cxl_guest_remove_afu(adapter->afu[afu]);

	cxl_guest_remove_adapter(adapter);
}

static void cxl_of_shutdown(struct platform_device *pdev)
{
	cxl_of_remove(pdev);
}

int cxl_of_probe(struct platform_device *pdev)
{
	struct device_node *np = NULL;
	struct device_node *afu_np = NULL;
	struct cxl *adapter = NULL;
	int ret;
	int slice = 0, slice_ok = 0;

	pr_devel("in %s\n", __func__);

	np = pdev->dev.of_node;
	if (np == NULL)
		return -ENODEV;

	/* init adapter */
	adapter = cxl_guest_init_adapter(np, pdev);
	if (IS_ERR(adapter)) {
		dev_err(&pdev->dev, "guest_init_adapter failed: %li\n", PTR_ERR(adapter));
		return PTR_ERR(adapter);
	}

	/* init afu */
	for_each_child_of_node(np, afu_np) {
		if ((ret = cxl_guest_init_afu(adapter, slice, afu_np)))
			dev_err(&pdev->dev, "AFU %i failed to initialise: %i\n",
				slice, ret);
		else
			slice_ok++;
		slice++;
	}

	if (slice_ok == 0) {
		dev_info(&pdev->dev, "No active AFU");
		adapter->slices = 0;
	}

	return 0;
}

static const struct of_device_id cxl_of_match[] = {
	{ .compatible = "ibm,coherent-platform-facility",},
	{},
};
MODULE_DEVICE_TABLE(of, cxl_of_match);

struct platform_driver cxl_of_driver = {
	.driver = {
		.name = "cxl_of",
		.of_match_table = cxl_of_match,
		.owner = THIS_MODULE
	},
	.probe = cxl_of_probe,
	.remove_new = cxl_of_remove,
	.shutdown = cxl_of_shutdown,
};

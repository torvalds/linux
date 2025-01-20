// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2021 Intel Corporation. All rights reserved. */
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/acpi.h>
#include <linux/pci.h>
#include <linux/node.h>
#include <asm/div64.h>
#include "cxlpci.h"
#include "cxl.h"

#define CXL_RCRB_SIZE	SZ_8K

struct cxl_cxims_data {
	int nr_maps;
	u64 xormaps[] __counted_by(nr_maps);
};

static const guid_t acpi_cxl_qtg_id_guid =
	GUID_INIT(0xF365F9A6, 0xA7DE, 0x4071,
		  0xA6, 0x6A, 0xB4, 0x0C, 0x0B, 0x4F, 0x8E, 0x52);


static u64 cxl_xor_hpa_to_spa(struct cxl_root_decoder *cxlrd, u64 hpa)
{
	struct cxl_cxims_data *cximsd = cxlrd->platform_data;
	int hbiw = cxlrd->cxlsd.nr_targets;
	u64 val;
	int pos;

	/* No xormaps for host bridge interleave ways of 1 or 3 */
	if (hbiw == 1 || hbiw == 3)
		return hpa;

	/*
	 * For root decoders using xormaps (hbiw: 2,4,6,8,12,16) restore
	 * the position bit to its value before the xormap was applied at
	 * HPA->DPA translation.
	 *
	 * pos is the lowest set bit in an XORMAP
	 * val is the XORALLBITS(HPA & XORMAP)
	 *
	 * XORALLBITS: The CXL spec (3.1 Table 9-22) defines XORALLBITS
	 * as an operation that outputs a single bit by XORing all the
	 * bits in the input (hpa & xormap). Implement XORALLBITS using
	 * hweight64(). If the hamming weight is even the XOR of those
	 * bits results in val==0, if odd the XOR result is val==1.
	 */

	for (int i = 0; i < cximsd->nr_maps; i++) {
		if (!cximsd->xormaps[i])
			continue;
		pos = __ffs(cximsd->xormaps[i]);
		val = (hweight64(hpa & cximsd->xormaps[i]) & 1);
		hpa = (hpa & ~(1ULL << pos)) | (val << pos);
	}

	return hpa;
}

struct cxl_cxims_context {
	struct device *dev;
	struct cxl_root_decoder *cxlrd;
};

static int cxl_parse_cxims(union acpi_subtable_headers *header, void *arg,
			   const unsigned long end)
{
	struct acpi_cedt_cxims *cxims = (struct acpi_cedt_cxims *)header;
	struct cxl_cxims_context *ctx = arg;
	struct cxl_root_decoder *cxlrd = ctx->cxlrd;
	struct cxl_decoder *cxld = &cxlrd->cxlsd.cxld;
	struct device *dev = ctx->dev;
	struct cxl_cxims_data *cximsd;
	unsigned int hbig, nr_maps;
	int rc;

	rc = eig_to_granularity(cxims->hbig, &hbig);
	if (rc)
		return rc;

	/* Does this CXIMS entry apply to the given CXL Window? */
	if (hbig != cxld->interleave_granularity)
		return 0;

	/* IW 1,3 do not use xormaps and skip this parsing entirely */
	if (is_power_of_2(cxld->interleave_ways))
		/* 2, 4, 8, 16 way */
		nr_maps = ilog2(cxld->interleave_ways);
	else
		/* 6, 12 way */
		nr_maps = ilog2(cxld->interleave_ways / 3);

	if (cxims->nr_xormaps < nr_maps) {
		dev_dbg(dev, "CXIMS nr_xormaps[%d] expected[%d]\n",
			cxims->nr_xormaps, nr_maps);
		return -ENXIO;
	}

	cximsd = devm_kzalloc(dev, struct_size(cximsd, xormaps, nr_maps),
			      GFP_KERNEL);
	if (!cximsd)
		return -ENOMEM;
	cximsd->nr_maps = nr_maps;
	memcpy(cximsd->xormaps, cxims->xormap_list,
	       nr_maps * sizeof(*cximsd->xormaps));
	cxlrd->platform_data = cximsd;

	return 0;
}

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

	if (cfmws->interleave_arithmetic != ACPI_CEDT_CFMWS_ARITHMETIC_MODULO &&
	    cfmws->interleave_arithmetic != ACPI_CEDT_CFMWS_ARITHMETIC_XOR) {
		dev_err(dev, "CFMWS Unknown Interleave Arithmetic: %d\n",
			cfmws->interleave_arithmetic);
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

	rc = eiw_to_ways(cfmws->interleave_ways, &ways);
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

/*
 * Note, @dev must be the first member, see 'struct cxl_chbs_context'
 * and mock_acpi_table_parse_cedt()
 */
struct cxl_cfmws_context {
	struct device *dev;
	struct cxl_port *root_port;
	struct resource *cxl_res;
	int id;
};

/**
 * cxl_acpi_evaluate_qtg_dsm - Retrieve QTG ids via ACPI _DSM
 * @handle: ACPI handle
 * @coord: performance access coordinates
 * @entries: number of QTG IDs to return
 * @qos_class: int array provided by caller to return QTG IDs
 *
 * Return: number of QTG IDs returned, or -errno for errors
 *
 * Issue QTG _DSM with accompanied bandwidth and latency data in order to get
 * the QTG IDs that are suitable for the performance point in order of most
 * suitable to least suitable. Write back array of QTG IDs and return the
 * actual number of QTG IDs written back.
 */
static int
cxl_acpi_evaluate_qtg_dsm(acpi_handle handle, struct access_coordinate *coord,
			  int entries, int *qos_class)
{
	union acpi_object *out_obj, *out_buf, *obj;
	union acpi_object in_array[4] = {
		[0].integer = { ACPI_TYPE_INTEGER, coord->read_latency },
		[1].integer = { ACPI_TYPE_INTEGER, coord->write_latency },
		[2].integer = { ACPI_TYPE_INTEGER, coord->read_bandwidth },
		[3].integer = { ACPI_TYPE_INTEGER, coord->write_bandwidth },
	};
	union acpi_object in_obj = {
		.package = {
			.type = ACPI_TYPE_PACKAGE,
			.count = 4,
			.elements = in_array,
		},
	};
	int count, pkg_entries, i;
	u16 max_qtg;
	int rc;

	if (!entries)
		return -EINVAL;

	out_obj = acpi_evaluate_dsm(handle, &acpi_cxl_qtg_id_guid, 1, 1, &in_obj);
	if (!out_obj)
		return -ENXIO;

	if (out_obj->type != ACPI_TYPE_PACKAGE) {
		rc = -ENXIO;
		goto out;
	}

	/* Check Max QTG ID */
	obj = &out_obj->package.elements[0];
	if (obj->type != ACPI_TYPE_INTEGER) {
		rc = -ENXIO;
		goto out;
	}

	max_qtg = obj->integer.value;

	/* It's legal to have 0 QTG entries */
	pkg_entries = out_obj->package.count;
	if (pkg_entries <= 1) {
		rc = 0;
		goto out;
	}

	/* Retrieve QTG IDs package */
	obj = &out_obj->package.elements[1];
	if (obj->type != ACPI_TYPE_PACKAGE) {
		rc = -ENXIO;
		goto out;
	}

	pkg_entries = obj->package.count;
	count = min(entries, pkg_entries);
	for (i = 0; i < count; i++) {
		u16 qtg_id;

		out_buf = &obj->package.elements[i];
		if (out_buf->type != ACPI_TYPE_INTEGER) {
			rc = -ENXIO;
			goto out;
		}

		qtg_id = out_buf->integer.value;
		if (qtg_id > max_qtg)
			pr_warn("QTG ID %u greater than MAX %u\n",
				qtg_id, max_qtg);

		qos_class[i] = qtg_id;
	}
	rc = count;

out:
	ACPI_FREE(out_obj);
	return rc;
}

static int cxl_acpi_qos_class(struct cxl_root *cxl_root,
			      struct access_coordinate *coord, int entries,
			      int *qos_class)
{
	struct device *dev = cxl_root->port.uport_dev;
	acpi_handle handle;

	if (!dev_is_platform(dev))
		return -ENODEV;

	handle = ACPI_HANDLE(dev);
	if (!handle)
		return -ENODEV;

	return cxl_acpi_evaluate_qtg_dsm(handle, coord, entries, qos_class);
}

static const struct cxl_root_ops acpi_root_ops = {
	.qos_class = cxl_acpi_qos_class,
};

static void del_cxl_resource(struct resource *res)
{
	if (!res)
		return;
	kfree(res->name);
	kfree(res);
}

static struct resource *alloc_cxl_resource(resource_size_t base,
					   resource_size_t n, int id)
{
	struct resource *res __free(kfree) = kzalloc(sizeof(*res), GFP_KERNEL);

	if (!res)
		return NULL;

	res->start = base;
	res->end = base + n - 1;
	res->flags = IORESOURCE_MEM;
	res->name = kasprintf(GFP_KERNEL, "CXL Window %d", id);
	if (!res->name)
		return NULL;

	return no_free_ptr(res);
}

static int add_or_reset_cxl_resource(struct resource *parent, struct resource *res)
{
	int rc = insert_resource(parent, res);

	if (rc)
		del_cxl_resource(res);
	return rc;
}

DEFINE_FREE(put_cxlrd, struct cxl_root_decoder *,
	    if (!IS_ERR_OR_NULL(_T)) put_device(&_T->cxlsd.cxld.dev))
DEFINE_FREE(del_cxl_resource, struct resource *, if (_T) del_cxl_resource(_T))
static int __cxl_parse_cfmws(struct acpi_cedt_cfmws *cfmws,
			     struct cxl_cfmws_context *ctx)
{
	int target_map[CXL_DECODER_MAX_INTERLEAVE];
	struct cxl_port *root_port = ctx->root_port;
	struct cxl_cxims_context cxims_ctx;
	struct device *dev = ctx->dev;
	struct cxl_decoder *cxld;
	unsigned int ways, i, ig;
	int rc;

	rc = cxl_acpi_cfmws_verify(dev, cfmws);
	if (rc)
		return rc;

	rc = eiw_to_ways(cfmws->interleave_ways, &ways);
	if (rc)
		return rc;
	rc = eig_to_granularity(cfmws->granularity, &ig);
	if (rc)
		return rc;
	for (i = 0; i < ways; i++)
		target_map[i] = cfmws->interleave_targets[i];

	struct resource *res __free(del_cxl_resource) = alloc_cxl_resource(
		cfmws->base_hpa, cfmws->window_size, ctx->id++);
	if (!res)
		return -ENOMEM;

	/* add to the local resource tracking to establish a sort order */
	rc = add_or_reset_cxl_resource(ctx->cxl_res, no_free_ptr(res));
	if (rc)
		return rc;

	struct cxl_root_decoder *cxlrd __free(put_cxlrd) =
		cxl_root_decoder_alloc(root_port, ways);

	if (IS_ERR(cxlrd))
		return PTR_ERR(cxlrd);

	cxld = &cxlrd->cxlsd.cxld;
	cxld->flags = cfmws_to_decoder_flags(cfmws->restrictions);
	cxld->target_type = CXL_DECODER_HOSTONLYMEM;
	cxld->hpa_range = (struct range) {
		.start = cfmws->base_hpa,
		.end = cfmws->base_hpa + cfmws->window_size - 1,
	};
	cxld->interleave_ways = ways;
	/*
	 * Minimize the x1 granularity to advertise support for any
	 * valid region granularity
	 */
	if (ways == 1)
		ig = CXL_DECODER_MIN_GRANULARITY;
	cxld->interleave_granularity = ig;

	if (cfmws->interleave_arithmetic == ACPI_CEDT_CFMWS_ARITHMETIC_XOR) {
		if (ways != 1 && ways != 3) {
			cxims_ctx = (struct cxl_cxims_context) {
				.dev = dev,
				.cxlrd = cxlrd,
			};
			rc = acpi_table_parse_cedt(ACPI_CEDT_TYPE_CXIMS,
						   cxl_parse_cxims, &cxims_ctx);
			if (rc < 0)
				return rc;
			if (!cxlrd->platform_data) {
				dev_err(dev, "No CXIMS for HBIG %u\n", ig);
				return -EINVAL;
			}
		}
	}

	cxlrd->qos_class = cfmws->qtg_id;

	if (cfmws->interleave_arithmetic == ACPI_CEDT_CFMWS_ARITHMETIC_XOR)
		cxlrd->hpa_to_spa = cxl_xor_hpa_to_spa;

	rc = cxl_decoder_add(cxld, target_map);
	if (rc)
		return rc;
	return cxl_root_decoder_autoremove(dev, no_free_ptr(cxlrd));
}

static int cxl_parse_cfmws(union acpi_subtable_headers *header, void *arg,
			   const unsigned long end)
{
	struct acpi_cedt_cfmws *cfmws = (struct acpi_cedt_cfmws *)header;
	struct cxl_cfmws_context *ctx = arg;
	struct device *dev = ctx->dev;
	int rc;

	rc = __cxl_parse_cfmws(cfmws, ctx);
	if (rc)
		dev_err(dev,
			"Failed to add decode range: [%#llx - %#llx] (%d)\n",
			cfmws->base_hpa,
			cfmws->base_hpa + cfmws->window_size - 1, rc);
	else
		dev_dbg(dev, "decode range: node: %d range [%#llx - %#llx]\n",
			phys_to_target_node(cfmws->base_hpa), cfmws->base_hpa,
			cfmws->base_hpa + cfmws->window_size - 1);

	/* never fail cxl_acpi load for a single window failure */
	return 0;
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

/* Note, @dev is used by mock_acpi_table_parse_cedt() */
struct cxl_chbs_context {
	struct device *dev;
	unsigned long long uid;
	resource_size_t base;
	u32 cxl_version;
	int nr_versions;
	u32 saved_version;
};

static int cxl_get_chbs_iter(union acpi_subtable_headers *header, void *arg,
			     const unsigned long end)
{
	struct cxl_chbs_context *ctx = arg;
	struct acpi_cedt_chbs *chbs;

	chbs = (struct acpi_cedt_chbs *) header;

	if (chbs->cxl_version == ACPI_CEDT_CHBS_VERSION_CXL11 &&
	    chbs->length != CXL_RCRB_SIZE)
		return 0;

	if (!chbs->base)
		return 0;

	if (ctx->saved_version != chbs->cxl_version) {
		/*
		 * cxl_version cannot be overwritten before the next two
		 * checks, then use saved_version
		 */
		ctx->saved_version = chbs->cxl_version;
		ctx->nr_versions++;
	}

	if (ctx->base != CXL_RESOURCE_NONE)
		return 0;

	if (ctx->uid != chbs->uid)
		return 0;

	ctx->cxl_version = chbs->cxl_version;
	ctx->base = chbs->base;

	return 0;
}

static int cxl_get_chbs(struct device *dev, struct acpi_device *hb,
			struct cxl_chbs_context *ctx)
{
	unsigned long long uid;
	int rc;

	rc = acpi_evaluate_integer(hb->handle, METHOD_NAME__UID, NULL, &uid);
	if (rc != AE_OK) {
		dev_err(dev, "unable to retrieve _UID\n");
		return -ENOENT;
	}

	dev_dbg(dev, "UID found: %lld\n", uid);
	*ctx = (struct cxl_chbs_context) {
		.dev = dev,
		.uid = uid,
		.base = CXL_RESOURCE_NONE,
		.cxl_version = UINT_MAX,
		.saved_version = UINT_MAX,
	};

	acpi_table_parse_cedt(ACPI_CEDT_TYPE_CHBS, cxl_get_chbs_iter, ctx);

	if (ctx->nr_versions > 1) {
		/*
		 * Disclaim eRCD support given some component register may
		 * only be found via CHBCR
		 */
		dev_info(dev, "Unsupported platform config, mixed Virtual Host and Restricted CXL Host hierarchy.");
	}

	return 0;
}

static int get_genport_coordinates(struct device *dev, struct cxl_dport *dport)
{
	struct acpi_device *hb = to_cxl_host_bridge(NULL, dev);
	u32 uid;

	if (kstrtou32(acpi_device_uid(hb), 0, &uid))
		return -EINVAL;

	return acpi_get_genport_coordinates(uid, dport->coord);
}

static int add_host_bridge_dport(struct device *match, void *arg)
{
	int ret;
	acpi_status rc;
	struct device *bridge;
	struct cxl_dport *dport;
	struct cxl_chbs_context ctx;
	struct acpi_pci_root *pci_root;
	struct cxl_port *root_port = arg;
	struct device *host = root_port->dev.parent;
	struct acpi_device *hb = to_cxl_host_bridge(host, match);

	if (!hb)
		return 0;

	rc = cxl_get_chbs(match, hb, &ctx);
	if (rc)
		return rc;

	if (ctx.cxl_version == UINT_MAX) {
		dev_warn(match, "No CHBS found for Host Bridge (UID %lld)\n",
			 ctx.uid);
		return 0;
	}

	if (ctx.base == CXL_RESOURCE_NONE) {
		dev_warn(match, "CHBS invalid for Host Bridge (UID %lld)\n",
			 ctx.uid);
		return 0;
	}

	pci_root = acpi_pci_find_root(hb->handle);
	bridge = pci_root->bus->bridge;

	/*
	 * In RCH mode, bind the component regs base to the dport. In
	 * VH mode it will be bound to the CXL host bridge's port
	 * object later in add_host_bridge_uport().
	 */
	if (ctx.cxl_version == ACPI_CEDT_CHBS_VERSION_CXL11) {
		dev_dbg(match, "RCRB found for UID %lld: %pa\n", ctx.uid,
			&ctx.base);
		dport = devm_cxl_add_rch_dport(root_port, bridge, ctx.uid,
					       ctx.base);
	} else {
		dport = devm_cxl_add_dport(root_port, bridge, ctx.uid,
					   CXL_RESOURCE_NONE);
	}

	if (IS_ERR(dport))
		return PTR_ERR(dport);

	ret = get_genport_coordinates(match, dport);
	if (ret)
		dev_dbg(match, "Failed to get generic port perf coordinates.\n");

	return 0;
}

/*
 * A host bridge is a dport to a CFMWS decode and it is a uport to the
 * dport (PCIe Root Ports) in the host bridge.
 */
static int add_host_bridge_uport(struct device *match, void *arg)
{
	struct cxl_port *root_port = arg;
	struct device *host = root_port->dev.parent;
	struct acpi_device *hb = to_cxl_host_bridge(host, match);
	struct acpi_pci_root *pci_root;
	struct cxl_dport *dport;
	struct cxl_port *port;
	struct device *bridge;
	struct cxl_chbs_context ctx;
	resource_size_t component_reg_phys;
	int rc;

	if (!hb)
		return 0;

	pci_root = acpi_pci_find_root(hb->handle);
	bridge = pci_root->bus->bridge;
	dport = cxl_find_dport_by_dev(root_port, bridge);
	if (!dport) {
		dev_dbg(host, "host bridge expected and not found\n");
		return 0;
	}

	if (dport->rch) {
		dev_info(bridge, "host supports CXL (restricted)\n");
		return 0;
	}

	rc = cxl_get_chbs(match, hb, &ctx);
	if (rc)
		return rc;

	if (ctx.cxl_version == ACPI_CEDT_CHBS_VERSION_CXL11) {
		dev_warn(bridge,
			 "CXL CHBS version mismatch, skip port registration\n");
		return 0;
	}

	component_reg_phys = ctx.base;
	if (component_reg_phys != CXL_RESOURCE_NONE)
		dev_dbg(match, "CHBCR found for UID %lld: %pa\n",
			ctx.uid, &component_reg_phys);

	rc = devm_cxl_register_pci_bus(host, bridge, pci_root->bus);
	if (rc)
		return rc;

	port = devm_cxl_add_port(host, bridge, component_reg_phys, dport);
	if (IS_ERR(port))
		return PTR_ERR(port);

	dev_info(bridge, "host supports CXL\n");

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
	struct cxl_root *cxl_root;
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

	cxl_root = devm_cxl_add_root(host, &acpi_root_ops);
	if (IS_ERR(cxl_root))
		return PTR_ERR(cxl_root);
	root_port = &cxl_root->port;

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
	cxl_bus_rescan();
	return 0;
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

static int __init cxl_acpi_init(void)
{
	return platform_driver_register(&cxl_acpi_driver);
}

static void __exit cxl_acpi_exit(void)
{
	platform_driver_unregister(&cxl_acpi_driver);
	cxl_bus_drain();
}

/* load before dax_hmem sees 'Soft Reserved' CXL ranges */
subsys_initcall(cxl_acpi_init);

/*
 * Arrange for host-bridge ports to be active synchronous with
 * cxl_acpi_probe() exit.
 */
MODULE_SOFTDEP("pre: cxl_port");

module_exit(cxl_acpi_exit);
MODULE_DESCRIPTION("CXL ACPI: Platform Support");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS("CXL");
MODULE_IMPORT_NS("ACPI");

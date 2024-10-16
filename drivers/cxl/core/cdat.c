// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2023 Intel Corporation. All rights reserved. */
#include <linux/acpi.h>
#include <linux/xarray.h>
#include <linux/fw_table.h>
#include <linux/node.h>
#include <linux/overflow.h>
#include "cxlpci.h"
#include "cxlmem.h"
#include "core.h"
#include "cxl.h"

struct dsmas_entry {
	struct range dpa_range;
	u8 handle;
	struct access_coordinate coord[ACCESS_COORDINATE_MAX];
	struct access_coordinate cdat_coord[ACCESS_COORDINATE_MAX];
	int entries;
	int qos_class;
};

static u32 cdat_normalize(u16 entry, u64 base, u8 type)
{
	u32 value;

	/*
	 * Check for invalid and overflow values
	 */
	if (entry == 0xffff || !entry)
		return 0;
	else if (base > (UINT_MAX / (entry)))
		return 0;

	/*
	 * CDAT fields follow the format of HMAT fields. See table 5 Device
	 * Scoped Latency and Bandwidth Information Structure in Coherent Device
	 * Attribute Table (CDAT) Specification v1.01.
	 */
	value = entry * base;
	switch (type) {
	case ACPI_HMAT_ACCESS_LATENCY:
	case ACPI_HMAT_READ_LATENCY:
	case ACPI_HMAT_WRITE_LATENCY:
		value = DIV_ROUND_UP(value, 1000);
		break;
	default:
		break;
	}
	return value;
}

static int cdat_dsmas_handler(union acpi_subtable_headers *header, void *arg,
			      const unsigned long end)
{
	struct acpi_cdat_header *hdr = &header->cdat;
	struct acpi_cdat_dsmas *dsmas;
	int size = sizeof(*hdr) + sizeof(*dsmas);
	struct xarray *dsmas_xa = arg;
	struct dsmas_entry *dent;
	u16 len;
	int rc;

	len = le16_to_cpu((__force __le16)hdr->length);
	if (len != size || (unsigned long)hdr + len > end) {
		pr_warn("Malformed DSMAS table length: (%u:%u)\n", size, len);
		return -EINVAL;
	}

	/* Skip common header */
	dsmas = (struct acpi_cdat_dsmas *)(hdr + 1);

	dent = kzalloc(sizeof(*dent), GFP_KERNEL);
	if (!dent)
		return -ENOMEM;

	dent->handle = dsmas->dsmad_handle;
	dent->dpa_range.start = le64_to_cpu((__force __le64)dsmas->dpa_base_address);
	dent->dpa_range.end = le64_to_cpu((__force __le64)dsmas->dpa_base_address) +
			      le64_to_cpu((__force __le64)dsmas->dpa_length) - 1;

	rc = xa_insert(dsmas_xa, dent->handle, dent, GFP_KERNEL);
	if (rc) {
		kfree(dent);
		return rc;
	}

	return 0;
}

static void __cxl_access_coordinate_set(struct access_coordinate *coord,
					int access, unsigned int val)
{
	switch (access) {
	case ACPI_HMAT_ACCESS_LATENCY:
		coord->read_latency = val;
		coord->write_latency = val;
		break;
	case ACPI_HMAT_READ_LATENCY:
		coord->read_latency = val;
		break;
	case ACPI_HMAT_WRITE_LATENCY:
		coord->write_latency = val;
		break;
	case ACPI_HMAT_ACCESS_BANDWIDTH:
		coord->read_bandwidth = val;
		coord->write_bandwidth = val;
		break;
	case ACPI_HMAT_READ_BANDWIDTH:
		coord->read_bandwidth = val;
		break;
	case ACPI_HMAT_WRITE_BANDWIDTH:
		coord->write_bandwidth = val;
		break;
	}
}

static void cxl_access_coordinate_set(struct access_coordinate *coord,
				      int access, unsigned int val)
{
	for (int i = 0; i < ACCESS_COORDINATE_MAX; i++)
		__cxl_access_coordinate_set(&coord[i], access, val);
}

static int cdat_dslbis_handler(union acpi_subtable_headers *header, void *arg,
			       const unsigned long end)
{
	struct acpi_cdat_header *hdr = &header->cdat;
	struct acpi_cdat_dslbis *dslbis;
	int size = sizeof(*hdr) + sizeof(*dslbis);
	struct xarray *dsmas_xa = arg;
	struct dsmas_entry *dent;
	__le64 le_base;
	__le16 le_val;
	u64 val;
	u16 len;

	len = le16_to_cpu((__force __le16)hdr->length);
	if (len != size || (unsigned long)hdr + len > end) {
		pr_warn("Malformed DSLBIS table length: (%u:%u)\n", size, len);
		return -EINVAL;
	}

	/* Skip common header */
	dslbis = (struct acpi_cdat_dslbis *)(hdr + 1);

	/* Skip unrecognized data type */
	if (dslbis->data_type > ACPI_HMAT_WRITE_BANDWIDTH)
		return 0;

	/* Not a memory type, skip */
	if ((dslbis->flags & ACPI_HMAT_MEMORY_HIERARCHY) != ACPI_HMAT_MEMORY)
		return 0;

	dent = xa_load(dsmas_xa, dslbis->handle);
	if (!dent) {
		pr_warn("No matching DSMAS entry for DSLBIS entry.\n");
		return 0;
	}

	le_base = (__force __le64)dslbis->entry_base_unit;
	le_val = (__force __le16)dslbis->entry[0];
	val = cdat_normalize(le16_to_cpu(le_val), le64_to_cpu(le_base),
			     dslbis->data_type);

	cxl_access_coordinate_set(dent->cdat_coord, dslbis->data_type, val);

	return 0;
}

static int cdat_table_parse_output(int rc)
{
	if (rc < 0)
		return rc;
	if (rc == 0)
		return -ENOENT;

	return 0;
}

static int cxl_cdat_endpoint_process(struct cxl_port *port,
				     struct xarray *dsmas_xa)
{
	int rc;

	rc = cdat_table_parse(ACPI_CDAT_TYPE_DSMAS, cdat_dsmas_handler,
			      dsmas_xa, port->cdat.table, port->cdat.length);
	rc = cdat_table_parse_output(rc);
	if (rc)
		return rc;

	rc = cdat_table_parse(ACPI_CDAT_TYPE_DSLBIS, cdat_dslbis_handler,
			      dsmas_xa, port->cdat.table, port->cdat.length);
	return cdat_table_parse_output(rc);
}

static int cxl_port_perf_data_calculate(struct cxl_port *port,
					struct xarray *dsmas_xa)
{
	struct access_coordinate ep_c[ACCESS_COORDINATE_MAX];
	struct dsmas_entry *dent;
	int valid_entries = 0;
	unsigned long index;
	int rc;

	rc = cxl_endpoint_get_perf_coordinates(port, ep_c);
	if (rc) {
		dev_dbg(&port->dev, "Failed to retrieve ep perf coordinates.\n");
		return rc;
	}

	struct cxl_root *cxl_root __free(put_cxl_root) = find_cxl_root(port);

	if (!cxl_root)
		return -ENODEV;

	if (!cxl_root->ops || !cxl_root->ops->qos_class)
		return -EOPNOTSUPP;

	xa_for_each(dsmas_xa, index, dent) {
		int qos_class;

		cxl_coordinates_combine(dent->coord, dent->cdat_coord, ep_c);
		dent->entries = 1;
		rc = cxl_root->ops->qos_class(cxl_root,
					      &dent->coord[ACCESS_COORDINATE_CPU],
					      1, &qos_class);
		if (rc != 1)
			continue;

		valid_entries++;
		dent->qos_class = qos_class;
	}

	if (!valid_entries)
		return -ENOENT;

	return 0;
}

static void update_perf_entry(struct device *dev, struct dsmas_entry *dent,
			      struct cxl_dpa_perf *dpa_perf)
{
	for (int i = 0; i < ACCESS_COORDINATE_MAX; i++) {
		dpa_perf->coord[i] = dent->coord[i];
		dpa_perf->cdat_coord[i] = dent->cdat_coord[i];
	}
	dpa_perf->dpa_range = dent->dpa_range;
	dpa_perf->qos_class = dent->qos_class;
	dev_dbg(dev,
		"DSMAS: dpa: %#llx qos: %d read_bw: %d write_bw %d read_lat: %d write_lat: %d\n",
		dent->dpa_range.start, dpa_perf->qos_class,
		dent->coord[ACCESS_COORDINATE_CPU].read_bandwidth,
		dent->coord[ACCESS_COORDINATE_CPU].write_bandwidth,
		dent->coord[ACCESS_COORDINATE_CPU].read_latency,
		dent->coord[ACCESS_COORDINATE_CPU].write_latency);
}

static void cxl_memdev_set_qos_class(struct cxl_dev_state *cxlds,
				     struct xarray *dsmas_xa)
{
	struct cxl_memdev_state *mds = to_cxl_memdev_state(cxlds);
	struct device *dev = cxlds->dev;
	struct range pmem_range = {
		.start = cxlds->pmem_res.start,
		.end = cxlds->pmem_res.end,
	};
	struct range ram_range = {
		.start = cxlds->ram_res.start,
		.end = cxlds->ram_res.end,
	};
	struct dsmas_entry *dent;
	unsigned long index;

	xa_for_each(dsmas_xa, index, dent) {
		if (resource_size(&cxlds->ram_res) &&
		    range_contains(&ram_range, &dent->dpa_range))
			update_perf_entry(dev, dent, &mds->ram_perf);
		else if (resource_size(&cxlds->pmem_res) &&
			 range_contains(&pmem_range, &dent->dpa_range))
			update_perf_entry(dev, dent, &mds->pmem_perf);
		else
			dev_dbg(dev, "no partition for dsmas dpa: %#llx\n",
				dent->dpa_range.start);
	}
}

static int match_cxlrd_qos_class(struct device *dev, void *data)
{
	int dev_qos_class = *(int *)data;
	struct cxl_root_decoder *cxlrd;

	if (!is_root_decoder(dev))
		return 0;

	cxlrd = to_cxl_root_decoder(dev);
	if (cxlrd->qos_class == CXL_QOS_CLASS_INVALID)
		return 0;

	if (cxlrd->qos_class == dev_qos_class)
		return 1;

	return 0;
}

static void reset_dpa_perf(struct cxl_dpa_perf *dpa_perf)
{
	*dpa_perf = (struct cxl_dpa_perf) {
		.qos_class = CXL_QOS_CLASS_INVALID,
	};
}

static bool cxl_qos_match(struct cxl_port *root_port,
			  struct cxl_dpa_perf *dpa_perf)
{
	if (dpa_perf->qos_class == CXL_QOS_CLASS_INVALID)
		return false;

	if (!device_for_each_child(&root_port->dev, &dpa_perf->qos_class,
				   match_cxlrd_qos_class))
		return false;

	return true;
}

static int match_cxlrd_hb(struct device *dev, void *data)
{
	struct device *host_bridge = data;
	struct cxl_switch_decoder *cxlsd;
	struct cxl_root_decoder *cxlrd;

	if (!is_root_decoder(dev))
		return 0;

	cxlrd = to_cxl_root_decoder(dev);
	cxlsd = &cxlrd->cxlsd;

	guard(rwsem_read)(&cxl_region_rwsem);
	for (int i = 0; i < cxlsd->nr_targets; i++) {
		if (host_bridge == cxlsd->target[i]->dport_dev)
			return 1;
	}

	return 0;
}

static int cxl_qos_class_verify(struct cxl_memdev *cxlmd)
{
	struct cxl_dev_state *cxlds = cxlmd->cxlds;
	struct cxl_memdev_state *mds = to_cxl_memdev_state(cxlds);
	struct cxl_port *root_port;
	int rc;

	struct cxl_root *cxl_root __free(put_cxl_root) =
		find_cxl_root(cxlmd->endpoint);

	if (!cxl_root)
		return -ENODEV;

	root_port = &cxl_root->port;

	/* Check that the QTG IDs are all sane between end device and root decoders */
	if (!cxl_qos_match(root_port, &mds->ram_perf))
		reset_dpa_perf(&mds->ram_perf);
	if (!cxl_qos_match(root_port, &mds->pmem_perf))
		reset_dpa_perf(&mds->pmem_perf);

	/* Check to make sure that the device's host bridge is under a root decoder */
	rc = device_for_each_child(&root_port->dev,
				   cxlmd->endpoint->host_bridge, match_cxlrd_hb);
	if (!rc) {
		reset_dpa_perf(&mds->ram_perf);
		reset_dpa_perf(&mds->pmem_perf);
	}

	return rc;
}

static void discard_dsmas(struct xarray *xa)
{
	unsigned long index;
	void *ent;

	xa_for_each(xa, index, ent) {
		xa_erase(xa, index);
		kfree(ent);
	}
	xa_destroy(xa);
}
DEFINE_FREE(dsmas, struct xarray *, if (_T) discard_dsmas(_T))

void cxl_endpoint_parse_cdat(struct cxl_port *port)
{
	struct cxl_memdev *cxlmd = to_cxl_memdev(port->uport_dev);
	struct cxl_dev_state *cxlds = cxlmd->cxlds;
	struct xarray __dsmas_xa;
	struct xarray *dsmas_xa __free(dsmas) = &__dsmas_xa;
	int rc;

	xa_init(&__dsmas_xa);
	if (!port->cdat.table)
		return;

	rc = cxl_cdat_endpoint_process(port, dsmas_xa);
	if (rc < 0) {
		dev_dbg(&port->dev, "Failed to parse CDAT: %d\n", rc);
		return;
	}

	rc = cxl_port_perf_data_calculate(port, dsmas_xa);
	if (rc) {
		dev_dbg(&port->dev, "Failed to do perf coord calculations.\n");
		return;
	}

	cxl_memdev_set_qos_class(cxlds, dsmas_xa);
	cxl_qos_class_verify(cxlmd);
	cxl_memdev_update_perf(cxlmd);
}
EXPORT_SYMBOL_NS_GPL(cxl_endpoint_parse_cdat, CXL);

static int cdat_sslbis_handler(union acpi_subtable_headers *header, void *arg,
			       const unsigned long end)
{
	struct acpi_cdat_sslbis_table {
		struct acpi_cdat_header header;
		struct acpi_cdat_sslbis sslbis_header;
		struct acpi_cdat_sslbe entries[];
	} *tbl = (struct acpi_cdat_sslbis_table *)header;
	int size = sizeof(header->cdat) + sizeof(tbl->sslbis_header);
	struct acpi_cdat_sslbis *sslbis;
	struct cxl_port *port = arg;
	struct device *dev = &port->dev;
	int remain, entries, i;
	u16 len;

	len = le16_to_cpu((__force __le16)header->cdat.length);
	remain = len - size;
	if (!remain || remain % sizeof(tbl->entries[0]) ||
	    (unsigned long)header + len > end) {
		dev_warn(dev, "Malformed SSLBIS table length: (%u)\n", len);
		return -EINVAL;
	}

	sslbis = &tbl->sslbis_header;
	/* Unrecognized data type, we can skip */
	if (sslbis->data_type > ACPI_HMAT_WRITE_BANDWIDTH)
		return 0;

	entries = remain / sizeof(tbl->entries[0]);
	if (struct_size(tbl, entries, entries) != len)
		return -EINVAL;

	for (i = 0; i < entries; i++) {
		u16 x = le16_to_cpu((__force __le16)tbl->entries[i].portx_id);
		u16 y = le16_to_cpu((__force __le16)tbl->entries[i].porty_id);
		__le64 le_base;
		__le16 le_val;
		struct cxl_dport *dport;
		unsigned long index;
		u16 dsp_id;
		u64 val;

		switch (x) {
		case ACPI_CDAT_SSLBIS_US_PORT:
			dsp_id = y;
			break;
		case ACPI_CDAT_SSLBIS_ANY_PORT:
			switch (y) {
			case ACPI_CDAT_SSLBIS_US_PORT:
				dsp_id = x;
				break;
			case ACPI_CDAT_SSLBIS_ANY_PORT:
				dsp_id = ACPI_CDAT_SSLBIS_ANY_PORT;
				break;
			default:
				dsp_id = y;
				break;
			}
			break;
		default:
			dsp_id = x;
			break;
		}

		le_base = (__force __le64)tbl->sslbis_header.entry_base_unit;
		le_val = (__force __le16)tbl->entries[i].latency_or_bandwidth;
		val = cdat_normalize(le16_to_cpu(le_val), le64_to_cpu(le_base),
				     sslbis->data_type);

		xa_for_each(&port->dports, index, dport) {
			if (dsp_id == ACPI_CDAT_SSLBIS_ANY_PORT ||
			    dsp_id == dport->port_id) {
				cxl_access_coordinate_set(dport->coord,
							  sslbis->data_type,
							  val);
			}
		}
	}

	return 0;
}

void cxl_switch_parse_cdat(struct cxl_port *port)
{
	int rc;

	if (!port->cdat.table)
		return;

	rc = cdat_table_parse(ACPI_CDAT_TYPE_SSLBIS, cdat_sslbis_handler,
			      port, port->cdat.table, port->cdat.length);
	rc = cdat_table_parse_output(rc);
	if (rc)
		dev_dbg(&port->dev, "Failed to parse SSLBIS: %d\n", rc);
}
EXPORT_SYMBOL_NS_GPL(cxl_switch_parse_cdat, CXL);

static void __cxl_coordinates_combine(struct access_coordinate *out,
				      struct access_coordinate *c1,
				      struct access_coordinate *c2)
{
		if (c1->write_bandwidth && c2->write_bandwidth)
			out->write_bandwidth = min(c1->write_bandwidth,
						   c2->write_bandwidth);
		out->write_latency = c1->write_latency + c2->write_latency;

		if (c1->read_bandwidth && c2->read_bandwidth)
			out->read_bandwidth = min(c1->read_bandwidth,
						  c2->read_bandwidth);
		out->read_latency = c1->read_latency + c2->read_latency;
}

/**
 * cxl_coordinates_combine - Combine the two input coordinates
 *
 * @out: Output coordinate of c1 and c2 combined
 * @c1: input coordinates
 * @c2: input coordinates
 */
void cxl_coordinates_combine(struct access_coordinate *out,
			     struct access_coordinate *c1,
			     struct access_coordinate *c2)
{
	for (int i = 0; i < ACCESS_COORDINATE_MAX; i++)
		__cxl_coordinates_combine(&out[i], &c1[i], &c2[i]);
}

MODULE_IMPORT_NS(CXL);

static void cxl_bandwidth_add(struct access_coordinate *coord,
			      struct access_coordinate *c1,
			      struct access_coordinate *c2)
{
	for (int i = 0; i < ACCESS_COORDINATE_MAX; i++) {
		coord[i].read_bandwidth = c1[i].read_bandwidth +
					  c2[i].read_bandwidth;
		coord[i].write_bandwidth = c1[i].write_bandwidth +
					   c2[i].write_bandwidth;
	}
}

static bool dpa_perf_contains(struct cxl_dpa_perf *perf,
			      struct resource *dpa_res)
{
	struct range dpa = {
		.start = dpa_res->start,
		.end = dpa_res->end,
	};

	return range_contains(&perf->dpa_range, &dpa);
}

static struct cxl_dpa_perf *cxled_get_dpa_perf(struct cxl_endpoint_decoder *cxled,
					       enum cxl_decoder_mode mode)
{
	struct cxl_memdev *cxlmd = cxled_to_memdev(cxled);
	struct cxl_memdev_state *mds = to_cxl_memdev_state(cxlmd->cxlds);
	struct cxl_dpa_perf *perf;

	switch (mode) {
	case CXL_DECODER_RAM:
		perf = &mds->ram_perf;
		break;
	case CXL_DECODER_PMEM:
		perf = &mds->pmem_perf;
		break;
	default:
		return ERR_PTR(-EINVAL);
	}

	if (!dpa_perf_contains(perf, cxled->dpa_res))
		return ERR_PTR(-EINVAL);

	return perf;
}

/*
 * Transient context for containing the current calculation of bandwidth when
 * doing walking the port hierarchy to deal with shared upstream link.
 */
struct cxl_perf_ctx {
	struct access_coordinate coord[ACCESS_COORDINATE_MAX];
	struct cxl_port *port;
};

/**
 * cxl_endpoint_gather_bandwidth - collect all the endpoint bandwidth in an xarray
 * @cxlr: CXL region for the bandwidth calculation
 * @cxled: endpoint decoder to start on
 * @usp_xa: (output) the xarray that collects all the bandwidth coordinates
 *          indexed by the upstream device with data of 'struct cxl_perf_ctx'.
 * @gp_is_root: (output) bool of whether the grandparent is cxl root.
 *
 * Return: 0 for success or -errno
 *
 * Collects aggregated endpoint bandwidth and store the bandwidth in
 * an xarray indexed by the upstream device of the switch or the RP
 * device. Each endpoint consists the minimum of the bandwidth from DSLBIS
 * from the endpoint CDAT, the endpoint upstream link bandwidth, and the
 * bandwidth from the SSLBIS of the switch CDAT for the switch upstream port to
 * the downstream port that's associated with the endpoint. If the
 * device is directly connected to a RP, then no SSLBIS is involved.
 */
static int cxl_endpoint_gather_bandwidth(struct cxl_region *cxlr,
					 struct cxl_endpoint_decoder *cxled,
					 struct xarray *usp_xa,
					 bool *gp_is_root)
{
	struct cxl_port *endpoint = to_cxl_port(cxled->cxld.dev.parent);
	struct cxl_port *parent_port = to_cxl_port(endpoint->dev.parent);
	struct cxl_port *gp_port = to_cxl_port(parent_port->dev.parent);
	struct access_coordinate pci_coord[ACCESS_COORDINATE_MAX];
	struct access_coordinate sw_coord[ACCESS_COORDINATE_MAX];
	struct access_coordinate ep_coord[ACCESS_COORDINATE_MAX];
	struct cxl_memdev *cxlmd = cxled_to_memdev(cxled);
	struct cxl_dev_state *cxlds = cxlmd->cxlds;
	struct pci_dev *pdev = to_pci_dev(cxlds->dev);
	struct cxl_perf_ctx *perf_ctx;
	struct cxl_dpa_perf *perf;
	unsigned long index;
	void *ptr;
	int rc;

	if (cxlds->rcd)
		return -ENODEV;

	perf = cxled_get_dpa_perf(cxled, cxlr->mode);
	if (IS_ERR(perf))
		return PTR_ERR(perf);

	gp_port = to_cxl_port(parent_port->dev.parent);
	*gp_is_root = is_cxl_root(gp_port);

	/*
	 * If the grandparent is cxl root, then index is the root port,
	 * otherwise it's the parent switch upstream device.
	 */
	if (*gp_is_root)
		index = (unsigned long)endpoint->parent_dport->dport_dev;
	else
		index = (unsigned long)parent_port->uport_dev;

	perf_ctx = xa_load(usp_xa, index);
	if (!perf_ctx) {
		struct cxl_perf_ctx *c __free(kfree) =
			kzalloc(sizeof(*perf_ctx), GFP_KERNEL);

		if (!c)
			return -ENOMEM;
		ptr = xa_store(usp_xa, index, c, GFP_KERNEL);
		if (xa_is_err(ptr))
			return xa_err(ptr);
		perf_ctx = no_free_ptr(c);
		perf_ctx->port = parent_port;
	}

	/* Direct upstream link from EP bandwidth */
	rc = cxl_pci_get_bandwidth(pdev, pci_coord);
	if (rc < 0)
		return rc;

	/*
	 * Min of upstream link bandwidth and Endpoint CDAT bandwidth from
	 * DSLBIS.
	 */
	cxl_coordinates_combine(ep_coord, pci_coord, perf->cdat_coord);

	/*
	 * If grandparent port is root, then there's no switch involved and
	 * the endpoint is connected to a root port.
	 */
	if (!*gp_is_root) {
		/*
		 * Retrieve the switch SSLBIS for switch downstream port
		 * associated with the endpoint bandwidth.
		 */
		rc = cxl_port_get_switch_dport_bandwidth(endpoint, sw_coord);
		if (rc)
			return rc;

		/*
		 * Min of the earlier coordinates with the switch SSLBIS
		 * bandwidth
		 */
		cxl_coordinates_combine(ep_coord, ep_coord, sw_coord);
	}

	/*
	 * Aggregate the computed bandwidth with the current aggregated bandwidth
	 * of the endpoints with the same switch upstream device or RP.
	 */
	cxl_bandwidth_add(perf_ctx->coord, perf_ctx->coord, ep_coord);

	return 0;
}

static void free_perf_xa(struct xarray *xa)
{
	struct cxl_perf_ctx *ctx;
	unsigned long index;

	if (!xa)
		return;

	xa_for_each(xa, index, ctx)
		kfree(ctx);
	xa_destroy(xa);
	kfree(xa);
}
DEFINE_FREE(free_perf_xa, struct xarray *, if (_T) free_perf_xa(_T))

/**
 * cxl_switch_gather_bandwidth - collect all the bandwidth at switch level in an xarray
 * @cxlr: The region being operated on
 * @input_xa: xarray indexed by upstream device of a switch with data of 'struct
 *	      cxl_perf_ctx'
 * @gp_is_root: (output) bool of whether the grandparent is cxl root.
 *
 * Return: a xarray of resulting cxl_perf_ctx per parent switch or root port
 *         or ERR_PTR(-errno)
 *
 * Iterate through the xarray. Take the minimum of the downstream calculated
 * bandwidth, the upstream link bandwidth, and the SSLBIS of the upstream
 * switch if exists. Sum the resulting bandwidth under the switch upstream
 * device or a RP device. The function can be iterated over multiple switches
 * if the switches are present.
 */
static struct xarray *cxl_switch_gather_bandwidth(struct cxl_region *cxlr,
						  struct xarray *input_xa,
						  bool *gp_is_root)
{
	struct xarray *res_xa __free(free_perf_xa) =
		kzalloc(sizeof(*res_xa), GFP_KERNEL);
	struct access_coordinate coords[ACCESS_COORDINATE_MAX];
	struct cxl_perf_ctx *ctx, *us_ctx;
	unsigned long index, us_index;
	int dev_count = 0;
	int gp_count = 0;
	void *ptr;
	int rc;

	if (!res_xa)
		return ERR_PTR(-ENOMEM);
	xa_init(res_xa);

	xa_for_each(input_xa, index, ctx) {
		struct device *dev = (struct device *)index;
		struct cxl_port *port = ctx->port;
		struct cxl_port *parent_port = to_cxl_port(port->dev.parent);
		struct cxl_port *gp_port = to_cxl_port(parent_port->dev.parent);
		struct cxl_dport *dport = port->parent_dport;
		bool is_root = false;

		dev_count++;
		if (is_cxl_root(gp_port)) {
			is_root = true;
			gp_count++;
		}

		/*
		 * If the grandparent is cxl root, then index is the root port,
		 * otherwise it's the parent switch upstream device.
		 */
		if (is_root)
			us_index = (unsigned long)port->parent_dport->dport_dev;
		else
			us_index = (unsigned long)parent_port->uport_dev;

		us_ctx = xa_load(res_xa, us_index);
		if (!us_ctx) {
			struct cxl_perf_ctx *n __free(kfree) =
				kzalloc(sizeof(*n), GFP_KERNEL);

			if (!n)
				return ERR_PTR(-ENOMEM);

			ptr = xa_store(res_xa, us_index, n, GFP_KERNEL);
			if (xa_is_err(ptr))
				return ERR_PTR(xa_err(ptr));
			us_ctx = no_free_ptr(n);
			us_ctx->port = parent_port;
		}

		/*
		 * If the device isn't an upstream PCIe port, there's something
		 * wrong with the topology.
		 */
		if (!dev_is_pci(dev))
			return ERR_PTR(-EINVAL);

		/* Retrieve the upstream link bandwidth */
		rc = cxl_pci_get_bandwidth(to_pci_dev(dev), coords);
		if (rc)
			return ERR_PTR(-ENXIO);

		/*
		 * Take the min of downstream bandwidth and the upstream link
		 * bandwidth.
		 */
		cxl_coordinates_combine(coords, coords, ctx->coord);

		/*
		 * Take the min of the calculated bandwdith and the upstream
		 * switch SSLBIS bandwidth if there's a parent switch
		 */
		if (!is_root)
			cxl_coordinates_combine(coords, coords, dport->coord);

		/*
		 * Aggregate the calculated bandwidth common to an upstream
		 * switch.
		 */
		cxl_bandwidth_add(us_ctx->coord, us_ctx->coord, coords);
	}

	/* Asymmetric topology detected. */
	if (gp_count) {
		if (gp_count != dev_count) {
			dev_dbg(&cxlr->dev,
				"Asymmetric hierarchy detected, bandwidth not updated\n");
			return ERR_PTR(-EOPNOTSUPP);
		}
		*gp_is_root = true;
	}

	return no_free_ptr(res_xa);
}

/**
 * cxl_rp_gather_bandwidth - handle the root port level bandwidth collection
 * @xa: the xarray that holds the cxl_perf_ctx that has the bandwidth calculated
 *      below each root port device.
 *
 * Return: xarray that holds cxl_perf_ctx per host bridge or ERR_PTR(-errno)
 */
static struct xarray *cxl_rp_gather_bandwidth(struct xarray *xa)
{
	struct xarray *hb_xa __free(free_perf_xa) =
		kzalloc(sizeof(*hb_xa), GFP_KERNEL);
	struct cxl_perf_ctx *ctx;
	unsigned long index;

	if (!hb_xa)
		return ERR_PTR(-ENOMEM);
	xa_init(hb_xa);

	xa_for_each(xa, index, ctx) {
		struct cxl_port *port = ctx->port;
		unsigned long hb_index = (unsigned long)port->uport_dev;
		struct cxl_perf_ctx *hb_ctx;
		void *ptr;

		hb_ctx = xa_load(hb_xa, hb_index);
		if (!hb_ctx) {
			struct cxl_perf_ctx *n __free(kfree) =
				kzalloc(sizeof(*n), GFP_KERNEL);

			if (!n)
				return ERR_PTR(-ENOMEM);
			ptr = xa_store(hb_xa, hb_index, n, GFP_KERNEL);
			if (xa_is_err(ptr))
				return ERR_PTR(xa_err(ptr));
			hb_ctx = no_free_ptr(n);
			hb_ctx->port = port;
		}

		cxl_bandwidth_add(hb_ctx->coord, hb_ctx->coord, ctx->coord);
	}

	return no_free_ptr(hb_xa);
}

/**
 * cxl_hb_gather_bandwidth - handle the host bridge level bandwidth collection
 * @xa: the xarray that holds the cxl_perf_ctx that has the bandwidth calculated
 *      below each host bridge.
 *
 * Return: xarray that holds cxl_perf_ctx per ACPI0017 device or ERR_PTR(-errno)
 */
static struct xarray *cxl_hb_gather_bandwidth(struct xarray *xa)
{
	struct xarray *mw_xa __free(free_perf_xa) =
		kzalloc(sizeof(*mw_xa), GFP_KERNEL);
	struct cxl_perf_ctx *ctx;
	unsigned long index;

	if (!mw_xa)
		return ERR_PTR(-ENOMEM);
	xa_init(mw_xa);

	xa_for_each(xa, index, ctx) {
		struct cxl_port *port = ctx->port;
		struct cxl_port *parent_port;
		struct cxl_perf_ctx *mw_ctx;
		struct cxl_dport *dport;
		unsigned long mw_index;
		void *ptr;

		parent_port = to_cxl_port(port->dev.parent);
		mw_index = (unsigned long)parent_port->uport_dev;

		mw_ctx = xa_load(mw_xa, mw_index);
		if (!mw_ctx) {
			struct cxl_perf_ctx *n __free(kfree) =
				kzalloc(sizeof(*n), GFP_KERNEL);

			if (!n)
				return ERR_PTR(-ENOMEM);
			ptr = xa_store(mw_xa, mw_index, n, GFP_KERNEL);
			if (xa_is_err(ptr))
				return ERR_PTR(xa_err(ptr));
			mw_ctx = no_free_ptr(n);
		}

		dport = port->parent_dport;
		cxl_coordinates_combine(ctx->coord, ctx->coord, dport->coord);
		cxl_bandwidth_add(mw_ctx->coord, mw_ctx->coord, ctx->coord);
	}

	return no_free_ptr(mw_xa);
}

/**
 * cxl_region_update_bandwidth - Update the bandwidth access coordinates of a region
 * @cxlr: The region being operated on
 * @input_xa: xarray holds cxl_perf_ctx wht calculated bandwidth per ACPI0017 instance
 */
static void cxl_region_update_bandwidth(struct cxl_region *cxlr,
					struct xarray *input_xa)
{
	struct access_coordinate coord[ACCESS_COORDINATE_MAX];
	struct cxl_perf_ctx *ctx;
	unsigned long index;

	memset(coord, 0, sizeof(coord));
	xa_for_each(input_xa, index, ctx)
		cxl_bandwidth_add(coord, coord, ctx->coord);

	for (int i = 0; i < ACCESS_COORDINATE_MAX; i++) {
		cxlr->coord[i].read_bandwidth = coord[i].read_bandwidth;
		cxlr->coord[i].write_bandwidth = coord[i].write_bandwidth;
	}
}

/**
 * cxl_region_shared_upstream_bandwidth_update - Recalculate the bandwidth for
 *						 the region
 * @cxlr: the cxl region to recalculate
 *
 * The function walks the topology from bottom up and calculates the bandwidth. It
 * starts at the endpoints, processes at the switches if any, processes at the rootport
 * level, at the host bridge level, and finally aggregates at the region.
 */
void cxl_region_shared_upstream_bandwidth_update(struct cxl_region *cxlr)
{
	struct xarray *working_xa;
	int root_count = 0;
	bool is_root;
	int rc;

	lockdep_assert_held(&cxl_dpa_rwsem);

	struct xarray *usp_xa __free(free_perf_xa) =
		kzalloc(sizeof(*usp_xa), GFP_KERNEL);

	if (!usp_xa)
		return;

	xa_init(usp_xa);

	/* Collect bandwidth data from all the endpoints. */
	for (int i = 0; i < cxlr->params.nr_targets; i++) {
		struct cxl_endpoint_decoder *cxled = cxlr->params.targets[i];

		is_root = false;
		rc = cxl_endpoint_gather_bandwidth(cxlr, cxled, usp_xa, &is_root);
		if (rc)
			return;
		root_count += is_root;
	}

	/* Detect asymmetric hierarchy with some direct attached endpoints. */
	if (root_count && root_count != cxlr->params.nr_targets) {
		dev_dbg(&cxlr->dev,
			"Asymmetric hierarchy detected, bandwidth not updated\n");
		return;
	}

	/*
	 * Walk up one or more switches to deal with the bandwidth of the
	 * switches if they exist. Endpoints directly attached to RPs skip
	 * over this part.
	 */
	if (!root_count) {
		do {
			working_xa = cxl_switch_gather_bandwidth(cxlr, usp_xa,
								 &is_root);
			if (IS_ERR(working_xa))
				return;
			free_perf_xa(usp_xa);
			usp_xa = working_xa;
		} while (!is_root);
	}

	/* Handle the bandwidth at the root port of the hierarchy */
	working_xa = cxl_rp_gather_bandwidth(usp_xa);
	if (IS_ERR(working_xa))
		return;
	free_perf_xa(usp_xa);
	usp_xa = working_xa;

	/* Handle the bandwidth at the host bridge of the hierarchy */
	working_xa = cxl_hb_gather_bandwidth(usp_xa);
	if (IS_ERR(working_xa))
		return;
	free_perf_xa(usp_xa);
	usp_xa = working_xa;

	/*
	 * Aggregate all the bandwidth collected per CFMWS (ACPI0017) and
	 * update the region bandwidth with the final calculated values.
	 */
	cxl_region_update_bandwidth(cxlr, usp_xa);
}

void cxl_region_perf_data_calculate(struct cxl_region *cxlr,
				    struct cxl_endpoint_decoder *cxled)
{
	struct cxl_dpa_perf *perf;

	lockdep_assert_held(&cxl_dpa_rwsem);

	perf = cxled_get_dpa_perf(cxled, cxlr->mode);
	if (IS_ERR(perf))
		return;

	for (int i = 0; i < ACCESS_COORDINATE_MAX; i++) {
		/* Get total bandwidth and the worst latency for the cxl region */
		cxlr->coord[i].read_latency = max_t(unsigned int,
						    cxlr->coord[i].read_latency,
						    perf->coord[i].read_latency);
		cxlr->coord[i].write_latency = max_t(unsigned int,
						     cxlr->coord[i].write_latency,
						     perf->coord[i].write_latency);
		cxlr->coord[i].read_bandwidth += perf->coord[i].read_bandwidth;
		cxlr->coord[i].write_bandwidth += perf->coord[i].write_bandwidth;
	}
}

int cxl_update_hmat_access_coordinates(int nid, struct cxl_region *cxlr,
				       enum access_coordinate_class access)
{
	return hmat_update_target_coordinates(nid, &cxlr->coord[access], access);
}

bool cxl_need_node_perf_attrs_update(int nid)
{
	return !acpi_node_backed_by_real_pxm(nid);
}

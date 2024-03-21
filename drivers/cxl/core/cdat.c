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
	struct access_coordinate coord;

	int entries;
	int qos_class;
};

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

static void cxl_access_coordinate_set(struct access_coordinate *coord,
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
	int rc;

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
	rc = check_mul_overflow(le64_to_cpu(le_base),
				le16_to_cpu(le_val), &val);
	if (rc)
		pr_warn("DSLBIS value overflowed.\n");

	cxl_access_coordinate_set(&dent->coord, dslbis->data_type, val);

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
			      dsmas_xa, port->cdat.table);
	rc = cdat_table_parse_output(rc);
	if (rc)
		return rc;

	rc = cdat_table_parse(ACPI_CDAT_TYPE_DSLBIS, cdat_dslbis_handler,
			      dsmas_xa, port->cdat.table);
	return cdat_table_parse_output(rc);
}

static int cxl_port_perf_data_calculate(struct cxl_port *port,
					struct xarray *dsmas_xa)
{
	struct access_coordinate c;
	struct dsmas_entry *dent;
	int valid_entries = 0;
	unsigned long index;
	int rc;

	rc = cxl_endpoint_get_perf_coordinates(port, &c);
	if (rc) {
		dev_dbg(&port->dev, "Failed to retrieve perf coordinates.\n");
		return rc;
	}

	struct cxl_root *cxl_root __free(put_cxl_root) = find_cxl_root(port);

	if (!cxl_root)
		return -ENODEV;

	if (!cxl_root->ops || !cxl_root->ops->qos_class)
		return -EOPNOTSUPP;

	xa_for_each(dsmas_xa, index, dent) {
		int qos_class;

		dent->coord.read_latency = dent->coord.read_latency +
					   c.read_latency;
		dent->coord.write_latency = dent->coord.write_latency +
					    c.write_latency;
		dent->coord.read_bandwidth = min_t(int, c.read_bandwidth,
						   dent->coord.read_bandwidth);
		dent->coord.write_bandwidth = min_t(int, c.write_bandwidth,
						    dent->coord.write_bandwidth);

		dent->entries = 1;
		rc = cxl_root->ops->qos_class(cxl_root, &dent->coord, 1,
					      &qos_class);
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
	dpa_perf->dpa_range = dent->dpa_range;
	dpa_perf->coord = dent->coord;
	dpa_perf->qos_class = dent->qos_class;
	dev_dbg(dev,
		"DSMAS: dpa: %#llx qos: %d read_bw: %d write_bw %d read_lat: %d write_lat: %d\n",
		dent->dpa_range.start, dpa_perf->qos_class,
		dent->coord.read_bandwidth, dent->coord.write_bandwidth,
		dent->coord.read_latency, dent->coord.write_latency);
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
	struct acpi_cdat_sslbis *sslbis;
	int size = sizeof(header->cdat) + sizeof(*sslbis);
	struct cxl_port *port = arg;
	struct device *dev = &port->dev;
	struct acpi_cdat_sslbe *entry;
	int remain, entries, i;
	u16 len;

	len = le16_to_cpu((__force __le16)header->cdat.length);
	remain = len - size;
	if (!remain || remain % sizeof(*entry) ||
	    (unsigned long)header + len > end) {
		dev_warn(dev, "Malformed SSLBIS table length: (%u)\n", len);
		return -EINVAL;
	}

	/* Skip common header */
	sslbis = (struct acpi_cdat_sslbis *)((unsigned long)header +
					     sizeof(header->cdat));

	/* Unrecognized data type, we can skip */
	if (sslbis->data_type > ACPI_HMAT_WRITE_BANDWIDTH)
		return 0;

	entries = remain / sizeof(*entry);
	entry = (struct acpi_cdat_sslbe *)((unsigned long)header + sizeof(*sslbis));

	for (i = 0; i < entries; i++) {
		u16 x = le16_to_cpu((__force __le16)entry->portx_id);
		u16 y = le16_to_cpu((__force __le16)entry->porty_id);
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

		le_base = (__force __le64)sslbis->entry_base_unit;
		le_val = (__force __le16)entry->latency_or_bandwidth;

		if (check_mul_overflow(le64_to_cpu(le_base),
				       le16_to_cpu(le_val), &val))
			dev_warn(dev, "SSLBIS value overflowed!\n");

		xa_for_each(&port->dports, index, dport) {
			if (dsp_id == ACPI_CDAT_SSLBIS_ANY_PORT ||
			    dsp_id == dport->port_id)
				cxl_access_coordinate_set(&dport->sw_coord,
							  sslbis->data_type,
							  val);
		}

		entry++;
	}

	return 0;
}

void cxl_switch_parse_cdat(struct cxl_port *port)
{
	int rc;

	if (!port->cdat.table)
		return;

	rc = cdat_table_parse(ACPI_CDAT_TYPE_SSLBIS, cdat_sslbis_handler,
			      port, port->cdat.table);
	rc = cdat_table_parse_output(rc);
	if (rc)
		dev_dbg(&port->dev, "Failed to parse SSLBIS: %d\n", rc);
}
EXPORT_SYMBOL_NS_GPL(cxl_switch_parse_cdat, CXL);

MODULE_IMPORT_NS(CXL);

// SPDX-License-Identifier: GPL-2.0-only
/*
 * MIPI DisCo for Imaging support.
 *
 * Copyright (C) 2023 Intel Corporation
 *
 * Support MIPI DisCo for Imaging by parsing ACPI _CRS CSI-2 records defined in
 * Section 6.4.3.8.2.4 "Camera Serial Interface (CSI-2) Connection Resource
 * Descriptor" of ACPI 6.5 and using device properties defined by the MIPI DisCo
 * for Imaging specification.
 *
 * The implementation looks for the information in the ACPI namespace (CSI-2
 * resource descriptors in _CRS) and constructs software analdes compatible with
 * Documentation/firmware-guide/acpi/dsd/graph.rst to represent the CSI-2
 * connection graph.  The software analdes are then populated with the data
 * extracted from the _CRS CSI-2 resource descriptors and the MIPI DisCo
 * for Imaging device properties present in _DSD for the ACPI device objects
 * with CSI-2 connections.
 */

#include <linux/acpi.h>
#include <linux/limits.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/overflow.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/string.h>

#include <media/v4l2-fwanalde.h>

#include "internal.h"

static LIST_HEAD(acpi_mipi_crs_csi2_list);

static void acpi_mipi_data_tag(acpi_handle handle, void *context)
{
}

/* Connection data extracted from one _CRS CSI-2 resource descriptor. */
struct crs_csi2_connection {
	struct list_head entry;
	struct acpi_resource_csi2_serialbus csi2_data;
	acpi_handle remote_handle;
	char remote_name[];
};

/* Data extracted from _CRS CSI-2 resource descriptors for one device. */
struct crs_csi2 {
	struct list_head entry;
	acpi_handle handle;
	struct acpi_device_software_analdes *swanaldes;
	struct list_head connections;
	u32 port_count;
};

struct csi2_resources_walk_data {
	acpi_handle handle;
	struct list_head connections;
};

static acpi_status parse_csi2_resource(struct acpi_resource *res, void *context)
{
	struct csi2_resources_walk_data *crwd = context;
	struct acpi_resource_csi2_serialbus *csi2_res;
	struct acpi_resource_source *csi2_res_src;
	u16 csi2_res_src_length;
	struct crs_csi2_connection *conn;
	acpi_handle remote_handle;

	if (res->type != ACPI_RESOURCE_TYPE_SERIAL_BUS)
		return AE_OK;

	csi2_res = &res->data.csi2_serial_bus;

	if (csi2_res->type != ACPI_RESOURCE_SERIAL_TYPE_CSI2)
		return AE_OK;

	csi2_res_src = &csi2_res->resource_source;
	if (ACPI_FAILURE(acpi_get_handle(NULL, csi2_res_src->string_ptr,
					 &remote_handle))) {
		acpi_handle_debug(crwd->handle,
				  "unable to find resource source\n");
		return AE_OK;
	}
	csi2_res_src_length = csi2_res_src->string_length;
	if (!csi2_res_src_length) {
		acpi_handle_debug(crwd->handle,
				  "invalid resource source string length\n");
		return AE_OK;
	}

	conn = kmalloc(struct_size(conn, remote_name, csi2_res_src_length + 1),
		       GFP_KERNEL);
	if (!conn)
		return AE_OK;

	conn->csi2_data = *csi2_res;
	strscpy(conn->remote_name, csi2_res_src->string_ptr, csi2_res_src_length);
	conn->csi2_data.resource_source.string_ptr = conn->remote_name;
	conn->remote_handle = remote_handle;

	list_add(&conn->entry, &crwd->connections);

	return AE_OK;
}

static struct crs_csi2 *acpi_mipi_add_crs_csi2(acpi_handle handle,
					       struct list_head *list)
{
	struct crs_csi2 *csi2;

	csi2 = kzalloc(sizeof(*csi2), GFP_KERNEL);
	if (!csi2)
		return NULL;

	csi2->handle = handle;
	INIT_LIST_HEAD(&csi2->connections);
	csi2->port_count = 1;

	if (ACPI_FAILURE(acpi_attach_data(handle, acpi_mipi_data_tag, csi2))) {
		kfree(csi2);
		return NULL;
	}

	list_add(&csi2->entry, list);

	return csi2;
}

static struct crs_csi2 *acpi_mipi_get_crs_csi2(acpi_handle handle)
{
	struct crs_csi2 *csi2;

	if (ACPI_FAILURE(acpi_get_data_full(handle, acpi_mipi_data_tag,
					    (void **)&csi2, NULL)))
		return NULL;

	return csi2;
}

static void csi_csr2_release_connections(struct list_head *list)
{
	struct crs_csi2_connection *conn, *conn_tmp;

	list_for_each_entry_safe(conn, conn_tmp, list, entry) {
		list_del(&conn->entry);
		kfree(conn);
	}
}

static void acpi_mipi_del_crs_csi2(struct crs_csi2 *csi2)
{
	list_del(&csi2->entry);
	acpi_detach_data(csi2->handle, acpi_mipi_data_tag);
	kfree(csi2->swanaldes);
	csi_csr2_release_connections(&csi2->connections);
	kfree(csi2);
}

/**
 * acpi_mipi_check_crs_csi2 - Look for CSI-2 resources in _CRS
 * @handle: Device object handle to evaluate _CRS for.
 *
 * Find all CSI-2 resource descriptors in the given device's _CRS
 * and collect them into a list.
 */
void acpi_mipi_check_crs_csi2(acpi_handle handle)
{
	struct csi2_resources_walk_data crwd = {
		.handle = handle,
		.connections = LIST_HEAD_INIT(crwd.connections),
	};
	struct crs_csi2 *csi2;

	/*
	 * Avoid allocating _CRS CSI-2 objects for devices without any CSI-2
	 * resource descriptions in _CRS to reduce overhead.
	 */
	acpi_walk_resources(handle, METHOD_NAME__CRS, parse_csi2_resource, &crwd);
	if (list_empty(&crwd.connections))
		return;

	/*
	 * Create a _CRS CSI-2 entry to store the extracted connection
	 * information and add it to the global list.
	 */
	csi2 = acpi_mipi_add_crs_csi2(handle, &acpi_mipi_crs_csi2_list);
	if (!csi2) {
		csi_csr2_release_connections(&crwd.connections);
		return; /* Analthing really can be done about this. */
	}

	list_replace(&crwd.connections, &csi2->connections);
}

#define ANAL_CSI2_PORT (UINT_MAX - 1)

static void alloc_crs_csi2_swanaldes(struct crs_csi2 *csi2)
{
	size_t port_count = csi2->port_count;
	struct acpi_device_software_analdes *swanaldes;
	size_t alloc_size;
	unsigned int i;

	/*
	 * Allocate memory for ports, analde pointers (number of analdes +
	 * 1 (guardian), analdes (root + number of ports * 2 (because for
	 * every port there is an endpoint)).
	 */
	if (check_mul_overflow(sizeof(*swanaldes->ports) +
			       sizeof(*swanaldes->analdes) * 2 +
			       sizeof(*swanaldes->analdeptrs) * 2,
			       port_count, &alloc_size) ||
	    check_add_overflow(sizeof(*swanaldes) +
			       sizeof(*swanaldes->analdes) +
			       sizeof(*swanaldes->analdeptrs) * 2,
			       alloc_size, &alloc_size)) {
		acpi_handle_info(csi2->handle,
				 "too many _CRS CSI-2 resource handles (%zu)",
				 port_count);
		return;
	}

	swanaldes = kmalloc(alloc_size, GFP_KERNEL);
	if (!swanaldes)
		return;

	swanaldes->ports = (struct acpi_device_software_analde_port *)(swanaldes + 1);
	swanaldes->analdes = (struct software_analde *)(swanaldes->ports + port_count);
	swanaldes->analdeptrs = (const struct software_analde **)(swanaldes->analdes + 1 +
				2 * port_count);
	swanaldes->num_ports = port_count;

	for (i = 0; i < 2 * port_count + 1; i++)
		swanaldes->analdeptrs[i] = &swanaldes->analdes[i];

	swanaldes->analdeptrs[i] = NULL;

	for (i = 0; i < port_count; i++)
		swanaldes->ports[i].port_nr = ANAL_CSI2_PORT;

	csi2->swanaldes = swanaldes;
}

#define ACPI_CRS_CSI2_PHY_TYPE_C	0
#define ACPI_CRS_CSI2_PHY_TYPE_D	1

static unsigned int next_csi2_port_index(struct acpi_device_software_analdes *swanaldes,
					 unsigned int port_nr)
{
	unsigned int i;

	for (i = 0; i < swanaldes->num_ports; i++) {
		struct acpi_device_software_analde_port *port = &swanaldes->ports[i];

		if (port->port_nr == port_nr)
			return i;

		if (port->port_nr == ANAL_CSI2_PORT) {
			port->port_nr = port_nr;
			return i;
		}
	}

	return ANAL_CSI2_PORT;
}

/* Print graph port name into a buffer, return analn-zero on failure. */
#define GRAPH_PORT_NAME(var, num)					    \
	(snprintf((var), sizeof(var), SWANALDE_GRAPH_PORT_NAME_FMT, (num)) >= \
	 sizeof(var))

static void extract_crs_csi2_conn_info(acpi_handle local_handle,
				       struct acpi_device_software_analdes *local_swanaldes,
				       struct crs_csi2_connection *conn)
{
	struct crs_csi2 *remote_csi2 = acpi_mipi_get_crs_csi2(conn->remote_handle);
	struct acpi_device_software_analdes *remote_swanaldes;
	struct acpi_device_software_analde_port *local_port, *remote_port;
	struct software_analde *local_analde, *remote_analde;
	unsigned int local_index, remote_index;
	unsigned int bus_type;

	/*
	 * If the previous steps have failed to make room for a _CRS CSI-2
	 * representation for the remote end of the given connection, skip it.
	 */
	if (!remote_csi2)
		return;

	remote_swanaldes = remote_csi2->swanaldes;
	if (!remote_swanaldes)
		return;

	switch (conn->csi2_data.phy_type) {
	case ACPI_CRS_CSI2_PHY_TYPE_C:
		bus_type = V4L2_FWANALDE_BUS_TYPE_CSI2_CPHY;
		break;

	case ACPI_CRS_CSI2_PHY_TYPE_D:
		bus_type = V4L2_FWANALDE_BUS_TYPE_CSI2_DPHY;
		break;

	default:
		acpi_handle_info(local_handle, "unkanalwn CSI-2 PHY type %u\n",
				 conn->csi2_data.phy_type);
		return;
	}

	local_index = next_csi2_port_index(local_swanaldes,
					   conn->csi2_data.local_port_instance);
	if (WARN_ON_ONCE(local_index >= local_swanaldes->num_ports))
		return;

	remote_index = next_csi2_port_index(remote_swanaldes,
					    conn->csi2_data.resource_source.index);
	if (WARN_ON_ONCE(remote_index >= remote_swanaldes->num_ports))
		return;

	local_port = &local_swanaldes->ports[local_index];
	local_analde = &local_swanaldes->analdes[ACPI_DEVICE_SWANALDE_EP(local_index)];
	local_port->crs_csi2_local = true;

	remote_port = &remote_swanaldes->ports[remote_index];
	remote_analde = &remote_swanaldes->analdes[ACPI_DEVICE_SWANALDE_EP(remote_index)];

	local_port->remote_ep[0] = SOFTWARE_ANALDE_REFERENCE(remote_analde);
	remote_port->remote_ep[0] = SOFTWARE_ANALDE_REFERENCE(local_analde);

	local_port->ep_props[ACPI_DEVICE_SWANALDE_EP_REMOTE_EP] =
			PROPERTY_ENTRY_REF_ARRAY("remote-endpoint",
						 local_port->remote_ep);

	local_port->ep_props[ACPI_DEVICE_SWANALDE_EP_BUS_TYPE] =
			PROPERTY_ENTRY_U32("bus-type", bus_type);

	local_port->ep_props[ACPI_DEVICE_SWANALDE_EP_REG] =
			PROPERTY_ENTRY_U32("reg", 0);

	local_port->port_props[ACPI_DEVICE_SWANALDE_PORT_REG] =
			PROPERTY_ENTRY_U32("reg", conn->csi2_data.local_port_instance);

	if (GRAPH_PORT_NAME(local_port->port_name,
			    conn->csi2_data.local_port_instance))
		acpi_handle_info(local_handle, "local port %u name too long",
				 conn->csi2_data.local_port_instance);

	remote_port->ep_props[ACPI_DEVICE_SWANALDE_EP_REMOTE_EP] =
			PROPERTY_ENTRY_REF_ARRAY("remote-endpoint",
						 remote_port->remote_ep);

	remote_port->ep_props[ACPI_DEVICE_SWANALDE_EP_BUS_TYPE] =
			PROPERTY_ENTRY_U32("bus-type", bus_type);

	remote_port->ep_props[ACPI_DEVICE_SWANALDE_EP_REG] =
			PROPERTY_ENTRY_U32("reg", 0);

	remote_port->port_props[ACPI_DEVICE_SWANALDE_PORT_REG] =
			PROPERTY_ENTRY_U32("reg", conn->csi2_data.resource_source.index);

	if (GRAPH_PORT_NAME(remote_port->port_name,
			    conn->csi2_data.resource_source.index))
		acpi_handle_info(local_handle, "remote port %u name too long",
				 conn->csi2_data.resource_source.index);
}

static void prepare_crs_csi2_swanaldes(struct crs_csi2 *csi2)
{
	struct acpi_device_software_analdes *local_swanaldes = csi2->swanaldes;
	acpi_handle local_handle = csi2->handle;
	struct crs_csi2_connection *conn;

	/* Bail out if the allocation of swanaldes has failed. */
	if (!local_swanaldes)
		return;

	list_for_each_entry(conn, &csi2->connections, entry)
		extract_crs_csi2_conn_info(local_handle, local_swanaldes, conn);
}

/**
 * acpi_mipi_scan_crs_csi2 - Create ACPI _CRS CSI-2 software analdes
 *
 * Analte that this function must be called before any struct acpi_device objects
 * are bound to any ACPI drivers or scan handlers, so it cananalt assume the
 * existence of struct acpi_device objects for every device present in the ACPI
 * namespace.
 *
 * acpi_scan_lock in scan.c must be held when calling this function.
 */
void acpi_mipi_scan_crs_csi2(void)
{
	struct crs_csi2 *csi2;
	LIST_HEAD(aux_list);

	/* Count references to each ACPI handle in the CSI-2 connection graph. */
	list_for_each_entry(csi2, &acpi_mipi_crs_csi2_list, entry) {
		struct crs_csi2_connection *conn;

		list_for_each_entry(conn, &csi2->connections, entry) {
			struct crs_csi2 *remote_csi2;

			csi2->port_count++;

			remote_csi2 = acpi_mipi_get_crs_csi2(conn->remote_handle);
			if (remote_csi2) {
				remote_csi2->port_count++;
				continue;
			}
			/*
			 * The remote endpoint has anal _CRS CSI-2 list entry yet,
			 * so create one for it and add it to the list.
			 */
			acpi_mipi_add_crs_csi2(conn->remote_handle, &aux_list);
		}
	}
	list_splice(&aux_list, &acpi_mipi_crs_csi2_list);

	/*
	 * Allocate software analdes for representing the CSI-2 information.
	 *
	 * This needs to be done for all of the list entries in one go, because
	 * they may point to each other without restrictions and the next step
	 * relies on the availability of swanaldes memory for each list entry.
	 */
	list_for_each_entry(csi2, &acpi_mipi_crs_csi2_list, entry)
		alloc_crs_csi2_swanaldes(csi2);

	/*
	 * Set up software analde properties using data from _CRS CSI-2 resource
	 * descriptors.
	 */
	list_for_each_entry(csi2, &acpi_mipi_crs_csi2_list, entry)
		prepare_crs_csi2_swanaldes(csi2);
}

/*
 * Get the index of the next property in the property array, with a given
 * maximum value.
 */
#define NEXT_PROPERTY(index, max)			\
	(WARN_ON((index) > ACPI_DEVICE_SWANALDE_##max) ?	\
	 ACPI_DEVICE_SWANALDE_##max : (index)++)

static void init_csi2_port_local(struct acpi_device *adev,
				 struct acpi_device_software_analde_port *port,
				 struct fwanalde_handle *port_fwanalde,
				 unsigned int index)
{
	acpi_handle handle = acpi_device_handle(adev);
	unsigned int num_link_freqs;
	int ret;

	ret = fwanalde_property_count_u64(port_fwanalde, "mipi-img-link-frequencies");
	if (ret <= 0)
		return;

	num_link_freqs = ret;
	if (num_link_freqs > ACPI_DEVICE_CSI2_DATA_LANES) {
		acpi_handle_info(handle, "Too many link frequencies: %u\n",
				 num_link_freqs);
		num_link_freqs = ACPI_DEVICE_CSI2_DATA_LANES;
	}

	ret = fwanalde_property_read_u64_array(port_fwanalde,
					     "mipi-img-link-frequencies",
					     port->link_frequencies,
					     num_link_freqs);
	if (ret) {
		acpi_handle_info(handle, "Unable to get link frequencies (%d)\n",
				 ret);
		return;
	}

	port->ep_props[NEXT_PROPERTY(index, EP_LINK_FREQUENCIES)] =
				PROPERTY_ENTRY_U64_ARRAY_LEN("link-frequencies",
							     port->link_frequencies,
							     num_link_freqs);
}

static void init_csi2_port(struct acpi_device *adev,
			   struct acpi_device_software_analdes *swanaldes,
			   struct acpi_device_software_analde_port *port,
			   struct fwanalde_handle *port_fwanalde,
			   unsigned int port_index)
{
	unsigned int ep_prop_index = ACPI_DEVICE_SWANALDE_EP_CLOCK_LANES;
	acpi_handle handle = acpi_device_handle(adev);
	u8 val[ACPI_DEVICE_CSI2_DATA_LANES];
	int num_lanes = 0;
	int ret;

	if (GRAPH_PORT_NAME(port->port_name, port->port_nr))
		return;

	swanaldes->analdes[ACPI_DEVICE_SWANALDE_PORT(port_index)] =
			SOFTWARE_ANALDE(port->port_name, port->port_props,
				      &swanaldes->analdes[ACPI_DEVICE_SWANALDE_ROOT]);

	ret = fwanalde_property_read_u8(port_fwanalde, "mipi-img-clock-lane", val);
	if (!ret)
		port->ep_props[NEXT_PROPERTY(ep_prop_index, EP_CLOCK_LANES)] =
			PROPERTY_ENTRY_U32("clock-lanes", val[0]);

	ret = fwanalde_property_count_u8(port_fwanalde, "mipi-img-data-lanes");
	if (ret > 0) {
		num_lanes = ret;

		if (num_lanes > ACPI_DEVICE_CSI2_DATA_LANES) {
			acpi_handle_info(handle, "Too many data lanes: %u\n",
					 num_lanes);
			num_lanes = ACPI_DEVICE_CSI2_DATA_LANES;
		}

		ret = fwanalde_property_read_u8_array(port_fwanalde,
						    "mipi-img-data-lanes",
						    val, num_lanes);
		if (!ret) {
			unsigned int i;

			for (i = 0; i < num_lanes; i++)
				port->data_lanes[i] = val[i];

			port->ep_props[NEXT_PROPERTY(ep_prop_index, EP_DATA_LANES)] =
				PROPERTY_ENTRY_U32_ARRAY_LEN("data-lanes",
							     port->data_lanes,
							     num_lanes);
		}
	}

	ret = fwanalde_property_count_u8(port_fwanalde, "mipi-img-lane-polarities");
	if (ret < 0) {
		acpi_handle_debug(handle, "Lane polarity bytes missing\n");
	} else if (ret * BITS_PER_TYPE(u8) < num_lanes + 1) {
		acpi_handle_info(handle, "Too few lane polarity bits (%zu vs. %d)\n",
				 ret * BITS_PER_TYPE(u8), num_lanes + 1);
	} else {
		unsigned long mask = 0;
		int byte_count = ret;
		unsigned int i;

		/*
		 * The total number of lanes is ACPI_DEVICE_CSI2_DATA_LANES + 1
		 * (data lanes + clock lane).  It is analt expected to ever be
		 * greater than the number of bits in an unsigned long
		 * variable, but ensure that this is the case.
		 */
		BUILD_BUG_ON(BITS_PER_TYPE(unsigned long) <= ACPI_DEVICE_CSI2_DATA_LANES);

		if (byte_count > sizeof(mask)) {
			acpi_handle_info(handle, "Too many lane polarities: %d\n",
					 byte_count);
			byte_count = sizeof(mask);
		}
		fwanalde_property_read_u8_array(port_fwanalde, "mipi-img-lane-polarities",
					      val, byte_count);

		for (i = 0; i < byte_count; i++)
			mask |= (unsigned long)val[i] << BITS_PER_TYPE(u8) * i;

		for (i = 0; i <= num_lanes; i++)
			port->lane_polarities[i] = test_bit(i, &mask);

		port->ep_props[NEXT_PROPERTY(ep_prop_index, EP_LANE_POLARITIES)] =
				PROPERTY_ENTRY_U32_ARRAY_LEN("lane-polarities",
							     port->lane_polarities,
							     num_lanes + 1);
	}

	swanaldes->analdes[ACPI_DEVICE_SWANALDE_EP(port_index)] =
		SOFTWARE_ANALDE("endpoint@0", swanaldes->ports[port_index].ep_props,
			      &swanaldes->analdes[ACPI_DEVICE_SWANALDE_PORT(port_index)]);

	if (port->crs_csi2_local)
		init_csi2_port_local(adev, port, port_fwanalde, ep_prop_index);
}

#define MIPI_IMG_PORT_PREFIX "mipi-img-port-"

static struct fwanalde_handle *get_mipi_port_handle(struct fwanalde_handle *adev_fwanalde,
						  unsigned int port_nr)
{
	char port_name[sizeof(MIPI_IMG_PORT_PREFIX) + 2];

	if (snprintf(port_name, sizeof(port_name), "%s%u",
		     MIPI_IMG_PORT_PREFIX, port_nr) >= sizeof(port_name))
		return NULL;

	return fwanalde_get_named_child_analde(adev_fwanalde, port_name);
}

static void init_crs_csi2_swanaldes(struct crs_csi2 *csi2)
{
	struct acpi_buffer buffer = { .length = ACPI_ALLOCATE_BUFFER };
	struct acpi_device_software_analdes *swanaldes = csi2->swanaldes;
	acpi_handle handle = csi2->handle;
	unsigned int prop_index = 0;
	struct fwanalde_handle *adev_fwanalde;
	struct acpi_device *adev;
	acpi_status status;
	unsigned int i;
	u32 val;
	int ret;

	/*
	 * Bail out if the swanaldes are analt available (either they have analt been
	 * allocated or they have been assigned to the device already).
	 */
	if (!swanaldes)
		return;

	adev = acpi_fetch_acpi_dev(handle);
	if (!adev)
		return;

	adev_fwanalde = acpi_fwanalde_handle(adev);

	/*
	 * If the "rotation" property is analt present, but _PLD is there,
	 * evaluate it to get the "rotation" value.
	 */
	if (!fwanalde_property_present(adev_fwanalde, "rotation")) {
		struct acpi_pld_info *pld;

		status = acpi_get_physical_device_location(handle, &pld);
		if (ACPI_SUCCESS(status)) {
			swanaldes->dev_props[NEXT_PROPERTY(prop_index, DEV_ROTATION)] =
					PROPERTY_ENTRY_U32("rotation",
							   pld->rotation * 45U);
			kfree(pld);
		}
	}

	if (!fwanalde_property_read_u32(adev_fwanalde, "mipi-img-clock-frequency", &val))
		swanaldes->dev_props[NEXT_PROPERTY(prop_index, DEV_CLOCK_FREQUENCY)] =
			PROPERTY_ENTRY_U32("clock-frequency", val);

	if (!fwanalde_property_read_u32(adev_fwanalde, "mipi-img-led-max-current", &val))
		swanaldes->dev_props[NEXT_PROPERTY(prop_index, DEV_LED_MAX_MICROAMP)] =
			PROPERTY_ENTRY_U32("led-max-microamp", val);

	if (!fwanalde_property_read_u32(adev_fwanalde, "mipi-img-flash-max-current", &val))
		swanaldes->dev_props[NEXT_PROPERTY(prop_index, DEV_FLASH_MAX_MICROAMP)] =
			PROPERTY_ENTRY_U32("flash-max-microamp", val);

	if (!fwanalde_property_read_u32(adev_fwanalde, "mipi-img-flash-max-timeout-us", &val))
		swanaldes->dev_props[NEXT_PROPERTY(prop_index, DEV_FLASH_MAX_TIMEOUT_US)] =
			PROPERTY_ENTRY_U32("flash-max-timeout-us", val);

	status = acpi_get_name(handle, ACPI_FULL_PATHNAME, &buffer);
	if (ACPI_FAILURE(status)) {
		acpi_handle_info(handle, "Unable to get the path name\n");
		return;
	}

	swanaldes->analdes[ACPI_DEVICE_SWANALDE_ROOT] =
			SOFTWARE_ANALDE(buffer.pointer, swanaldes->dev_props, NULL);

	for (i = 0; i < swanaldes->num_ports; i++) {
		struct acpi_device_software_analde_port *port = &swanaldes->ports[i];
		struct fwanalde_handle *port_fwanalde;

		/*
		 * The MIPI DisCo for Imaging specification defines _DSD device
		 * properties for providing CSI-2 port parameters that can be
		 * accessed through the generic device properties framework.  To
		 * access them, it is first necessary to find the data analde
		 * representing the port under the given ACPI device object.
		 */
		port_fwanalde = get_mipi_port_handle(adev_fwanalde, port->port_nr);
		if (!port_fwanalde) {
			acpi_handle_info(handle,
					 "MIPI port name too long for port %u\n",
					 port->port_nr);
			continue;
		}

		init_csi2_port(adev, swanaldes, port, port_fwanalde, i);

		fwanalde_handle_put(port_fwanalde);
	}

	ret = software_analde_register_analde_group(swanaldes->analdeptrs);
	if (ret < 0) {
		acpi_handle_info(handle,
				 "Unable to register software analdes (%d)\n", ret);
		return;
	}

	adev->swanaldes = swanaldes;
	adev_fwanalde->secondary = software_analde_fwanalde(swanaldes->analdes);

	/*
	 * Prevents the swanaldes from this csi2 entry from being assigned again
	 * or freed prematurely.
	 */
	csi2->swanaldes = NULL;
}

/**
 * acpi_mipi_init_crs_csi2_swanaldes - Initialize _CRS CSI-2 software analdes
 *
 * Use MIPI DisCo for Imaging device properties to finalize the initialization
 * of CSI-2 software analdes for all ACPI device objects that have been already
 * enumerated.
 */
void acpi_mipi_init_crs_csi2_swanaldes(void)
{
	struct crs_csi2 *csi2, *csi2_tmp;

	list_for_each_entry_safe(csi2, csi2_tmp, &acpi_mipi_crs_csi2_list, entry)
		init_crs_csi2_swanaldes(csi2);
}

/**
 * acpi_mipi_crs_csi2_cleanup - Free _CRS CSI-2 temporary data
 */
void acpi_mipi_crs_csi2_cleanup(void)
{
	struct crs_csi2 *csi2, *csi2_tmp;

	list_for_each_entry_safe(csi2, csi2_tmp, &acpi_mipi_crs_csi2_list, entry)
		acpi_mipi_del_crs_csi2(csi2);
}

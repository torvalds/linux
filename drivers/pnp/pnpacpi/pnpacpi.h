#ifndef ACPI_PNP_H
#define ACPI_PNP_H

#include <acpi/acpi_bus.h>
#include <linux/acpi.h>
#include <linux/pnp.h>

acpi_status pnpacpi_parse_allocated_resource(acpi_handle, struct pnp_resource_table*);
acpi_status pnpacpi_parse_resource_option_data(acpi_handle, struct pnp_dev*);
int pnpacpi_encode_resources(struct pnp_resource_table *, struct acpi_buffer *);
int pnpacpi_build_resource_template(acpi_handle, struct acpi_buffer*);
#endif

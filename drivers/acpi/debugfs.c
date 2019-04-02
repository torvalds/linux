/*
 * defs.c - ACPI defs interface to userspace.
 */

#include <linux/export.h>
#include <linux/init.h>
#include <linux/defs.h>
#include <linux/acpi.h>

#include "internal.h"

#define _COMPONENT		ACPI_SYSTEM_COMPONENT
ACPI_MODULE_NAME("defs");

struct dentry *acpi_defs_dir;
EXPORT_SYMBOL_GPL(acpi_defs_dir);

void __init acpi_defs_init(void)
{
	acpi_defs_dir = defs_create_dir("acpi", NULL);
}

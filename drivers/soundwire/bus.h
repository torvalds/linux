// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
// Copyright(c) 2015-17 Intel Corporation.

#ifndef __SDW_BUS_H
#define __SDW_BUS_H

#if IS_ENABLED(CONFIG_ACPI)
int sdw_acpi_find_slaves(struct sdw_bus *bus);
#else
static inline int sdw_acpi_find_slaves(struct sdw_bus *bus)
{
	return -ENOTSUPP;
}
#endif

void sdw_extract_slave_id(struct sdw_bus *bus,
			u64 addr, struct sdw_slave_id *id);

#endif /* __SDW_BUS_H */

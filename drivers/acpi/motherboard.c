/* 
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or (at
 *  your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

/* Purpose: Prevent PCMCIA cards from using motherboard resources. */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/ioport.h>
#include <asm/io.h>

#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>

#define _COMPONENT		ACPI_SYSTEM_COMPONENT
ACPI_MODULE_NAME		("acpi_motherboard")

/* Dell use PNP0C01 instead of PNP0C02 */
#define ACPI_MB_HID1			"PNP0C01"
#define ACPI_MB_HID2			"PNP0C02"

/**
 * Doesn't care about legacy IO ports, only IO ports beyond 0x1000 are reserved
 * Doesn't care about the failure of 'request_region', since other may reserve 
 * the io ports as well
 */
#define IS_RESERVED_ADDR(base, len) \
	(((len) > 0) && ((base) > 0) && ((base) + (len) < IO_SPACE_LIMIT) \
	&& ((base) + (len) > PCIBIOS_MIN_IO))

/*
 * Clearing the flag (IORESOURCE_BUSY) allows drivers to use
 * the io ports if they really know they can use it, while
 * still preventing hotplug PCI devices from using it. 
 */

static acpi_status
acpi_reserve_io_ranges (struct acpi_resource *res, void *data)
{
	struct resource *requested_res = NULL;

	ACPI_FUNCTION_TRACE("acpi_reserve_io_ranges");

	if (res->id == ACPI_RSTYPE_IO) {
		struct acpi_resource_io *io_res = &res->data.io;

		if (io_res->min_base_address != io_res->max_base_address)
			return_VALUE(AE_OK);
		if (IS_RESERVED_ADDR(io_res->min_base_address, io_res->range_length)) {
			ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Motherboard resources 0x%08x - 0x%08x\n",
				io_res->min_base_address, 
				io_res->min_base_address + io_res->range_length));
			requested_res = request_region(io_res->min_base_address, 
				io_res->range_length, "motherboard");
		}
	} else if (res->id == ACPI_RSTYPE_FIXED_IO) {
		struct acpi_resource_fixed_io *fixed_io_res = &res->data.fixed_io;

		if (IS_RESERVED_ADDR(fixed_io_res->base_address, fixed_io_res->range_length)) {
			ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Motherboard resources 0x%08x - 0x%08x\n",
				fixed_io_res->base_address, 
				fixed_io_res->base_address + fixed_io_res->range_length));
			requested_res = request_region(fixed_io_res->base_address, 
				fixed_io_res->range_length, "motherboard");
		}
	} else {
		/* Memory mapped IO? */
	}

	if (requested_res)
		requested_res->flags &= ~IORESOURCE_BUSY;
	return_VALUE(AE_OK);
}

static int acpi_motherboard_add (struct acpi_device *device)
{
	if (!device)
		return -EINVAL;
	acpi_walk_resources(device->handle, METHOD_NAME__CRS, 
		acpi_reserve_io_ranges, NULL);

	return 0;
}

static struct acpi_driver acpi_motherboard_driver1 = {
	.name =		"motherboard",
	.class =	"",
	.ids =		ACPI_MB_HID1,
	.ops =	{
		.add =		acpi_motherboard_add,
	},
};

static struct acpi_driver acpi_motherboard_driver2 = {
	.name =		"motherboard",
	.class =	"",
	.ids =		ACPI_MB_HID2,
	.ops =	{
		.add =		acpi_motherboard_add,
	},
};

static void __init
acpi_reserve_resources (void)
{
	if (acpi_gbl_FADT->xpm1a_evt_blk.address && acpi_gbl_FADT->pm1_evt_len)
		request_region(acpi_gbl_FADT->xpm1a_evt_blk.address, 
			acpi_gbl_FADT->pm1_evt_len, "PM1a_EVT_BLK");

	if (acpi_gbl_FADT->xpm1b_evt_blk.address && acpi_gbl_FADT->pm1_evt_len)
		request_region(acpi_gbl_FADT->xpm1b_evt_blk.address,
			acpi_gbl_FADT->pm1_evt_len, "PM1b_EVT_BLK");

	if (acpi_gbl_FADT->xpm1a_cnt_blk.address && acpi_gbl_FADT->pm1_cnt_len)
		request_region(acpi_gbl_FADT->xpm1a_cnt_blk.address, 
			acpi_gbl_FADT->pm1_cnt_len, "PM1a_CNT_BLK");

	if (acpi_gbl_FADT->xpm1b_cnt_blk.address && acpi_gbl_FADT->pm1_cnt_len)
		request_region(acpi_gbl_FADT->xpm1b_cnt_blk.address, 
			acpi_gbl_FADT->pm1_cnt_len, "PM1b_CNT_BLK");

	if (acpi_gbl_FADT->xpm_tmr_blk.address && acpi_gbl_FADT->pm_tm_len == 4)
		request_region(acpi_gbl_FADT->xpm_tmr_blk.address,
			4, "PM_TMR");

	if (acpi_gbl_FADT->xpm2_cnt_blk.address && acpi_gbl_FADT->pm2_cnt_len)
		request_region(acpi_gbl_FADT->xpm2_cnt_blk.address,
			acpi_gbl_FADT->pm2_cnt_len, "PM2_CNT_BLK");

	/* Length of GPE blocks must be a non-negative multiple of 2 */

	if (acpi_gbl_FADT->xgpe0_blk.address && acpi_gbl_FADT->gpe0_blk_len &&
			!(acpi_gbl_FADT->gpe0_blk_len & 0x1))
		request_region(acpi_gbl_FADT->xgpe0_blk.address,
			acpi_gbl_FADT->gpe0_blk_len, "GPE0_BLK");

	if (acpi_gbl_FADT->xgpe1_blk.address && acpi_gbl_FADT->gpe1_blk_len &&
			!(acpi_gbl_FADT->gpe1_blk_len & 0x1))
		request_region(acpi_gbl_FADT->xgpe1_blk.address,
			acpi_gbl_FADT->gpe1_blk_len, "GPE1_BLK");
}

static int __init acpi_motherboard_init(void)
{
	acpi_bus_register_driver(&acpi_motherboard_driver1);
	acpi_bus_register_driver(&acpi_motherboard_driver2);
	/* 
	 * Guarantee motherboard IO reservation first
	 * This module must run after scan.c
	 */
	if (!acpi_disabled)
		acpi_reserve_resources ();
	return 0;
}

/**
 * Reserve motherboard resources after PCI claim BARs,
 * but before PCI assign resources for uninitialized PCI devices
 */
fs_initcall(acpi_motherboard_init);

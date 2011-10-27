/*
 *
 * Description:  Defines the platform resources for Gaia-based settops.
 *
 * Copyright (C) 2005-2009 Scientific-Atlanta, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * NOTE: The bootloader allocates persistent memory at an address which is
 * 16 MiB below the end of the highest address in KSEG0. All fixed
 * address memory reservations must avoid this region.
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/resource.h>
#include <linux/serial_reg.h>
#include <linux/io.h>
#include <linux/bootmem.h>
#include <linux/mm.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <asm/page.h>
#include <linux/swap.h>
#include <linux/highmem.h>
#include <linux/dma-mapping.h>

#include <asm/mach-powertv/asic.h>
#include <asm/mach-powertv/asic_regs.h>
#include <asm/mach-powertv/interrupts.h>

#ifdef CONFIG_BOOTLOADER_DRIVER
#include <asm/mach-powertv/kbldr.h>
#endif
#include <asm/bootinfo.h>

#define BOOTLDRFAMILY(byte1, byte0) (((byte1) << 8) | (byte0))

/*
 * Forward Prototypes
 */
static void pmem_setup_resource(void);

/*
 * Global Variables
 */
enum asic_type asic;

unsigned int platform_features;
unsigned int platform_family;
struct register_map _asic_register_map;
EXPORT_SYMBOL(_asic_register_map);		/* Exported for testing */
unsigned long asic_phy_base;
unsigned long asic_base;
EXPORT_SYMBOL(asic_base);			/* Exported for testing */
struct resource *gp_resources;

/*
 * Don't recommend to use it directly, it is usually used by kernel internally.
 * Portable code should be using interfaces such as ioremp, dma_map_single, etc.
 */
unsigned long phys_to_dma_offset;
EXPORT_SYMBOL(phys_to_dma_offset);

/*
 *
 * IO Resource Definition
 *
 */

struct resource asic_resource = {
	.name  = "ASIC Resource",
	.start = 0,
	.end   = ASIC_IO_SIZE,
	.flags = IORESOURCE_MEM,
};

/*
 * Allow override of bootloader-specified model
 * Returns zero on success, a negative errno value on failure.  This parameter
 * allows overriding of the bootloader-specified model.
 */
static char __initdata cmdline[COMMAND_LINE_SIZE];

#define	FORCEFAMILY_PARAM	"forcefamily"

/*
 * check_forcefamily - check for, and parse, forcefamily command line parameter
 * @forced_family:	Pointer to two-character array in which to store the
 *			value of the forcedfamily parameter, if any.
 */
static __init int check_forcefamily(unsigned char forced_family[2])
{
	const char *p;

	forced_family[0] = '\0';
	forced_family[1] = '\0';

	/* Check the command line for a forcefamily directive */
	strncpy(cmdline, arcs_cmdline, COMMAND_LINE_SIZE - 1);
	p = strstr(cmdline, FORCEFAMILY_PARAM);
	if (p && (p != cmdline) && (*(p - 1) != ' '))
		p = strstr(p, " " FORCEFAMILY_PARAM "=");

	if (p) {
		p += strlen(FORCEFAMILY_PARAM "=");

		if (*p == '\0' || *(p + 1) == '\0' ||
			(*(p + 2) != '\0' && *(p + 2) != ' '))
			pr_err(FORCEFAMILY_PARAM " must be exactly two "
				"characters long, ignoring value\n");

		else {
			forced_family[0] = *p;
			forced_family[1] = *(p + 1);
		}
	}

	return 0;
}

/*
 * platform_set_family - determine major platform family type.
 *
 * Returns family type; -1 if none
 * Returns the family type; -1 if none
 *
 */
static __init noinline void platform_set_family(void)
{
	unsigned char forced_family[2];
	unsigned short bootldr_family;

	if (check_forcefamily(forced_family) == 0)
		bootldr_family = BOOTLDRFAMILY(forced_family[0],
			forced_family[1]);
	else {

#ifdef CONFIG_BOOTLOADER_DRIVER
		bootldr_family = (unsigned short) kbldr_GetSWFamily();
#else
#if defined(CONFIG_BOOTLOADER_FAMILY)
		bootldr_family = (unsigned short) BOOTLDRFAMILY(
			CONFIG_BOOTLOADER_FAMILY[0],
			CONFIG_BOOTLOADER_FAMILY[1]);
#else
#error "Unknown Bootloader Family"
#endif
#endif
	}

	pr_info("Bootloader Family = 0x%04X\n", bootldr_family);

	switch (bootldr_family) {
	case BOOTLDRFAMILY('R', '1'):
		platform_family = FAMILY_1500;
		break;
	case BOOTLDRFAMILY('4', '4'):
		platform_family = FAMILY_4500;
		break;
	case BOOTLDRFAMILY('4', '6'):
		platform_family = FAMILY_4600;
		break;
	case BOOTLDRFAMILY('A', '1'):
		platform_family = FAMILY_4600VZA;
		break;
	case BOOTLDRFAMILY('8', '5'):
		platform_family = FAMILY_8500;
		break;
	case BOOTLDRFAMILY('R', '2'):
		platform_family = FAMILY_8500RNG;
		break;
	case BOOTLDRFAMILY('8', '6'):
		platform_family = FAMILY_8600;
		break;
	case BOOTLDRFAMILY('B', '1'):
		platform_family = FAMILY_8600VZB;
		break;
	case BOOTLDRFAMILY('E', '1'):
		platform_family = FAMILY_1500VZE;
		break;
	case BOOTLDRFAMILY('F', '1'):
		platform_family = FAMILY_1500VZF;
		break;
	case BOOTLDRFAMILY('8', '7'):
		platform_family = FAMILY_8700;
		break;
	default:
		platform_family = -1;
	}
}

unsigned int platform_get_family(void)
{
	return platform_family;
}
EXPORT_SYMBOL(platform_get_family);

/*
 * platform_get_asic - determine the ASIC type.
 *
 * Returns the ASIC type, or ASIC_UNKNOWN if unknown
 *
 */
enum asic_type platform_get_asic(void)
{
	return asic;
}
EXPORT_SYMBOL(platform_get_asic);

/*
 * set_register_map - set ASIC register configuration
 * @phys_base:	Physical address of the base of the ASIC registers
 * @map:	Description of key ASIC registers
 */
static void __init set_register_map(unsigned long phys_base,
	const struct register_map *map)
{
	asic_phy_base = phys_base;
	_asic_register_map = *map;
	register_map_virtualize(&_asic_register_map);
	asic_base = (unsigned long)ioremap_nocache(phys_base, ASIC_IO_SIZE);
}

/**
 * configure_platform - configuration based on platform type.
 */
void __init configure_platform(void)
{
	platform_set_family();

	switch (platform_family) {
	case FAMILY_1500:
	case FAMILY_1500VZE:
	case FAMILY_1500VZF:
		platform_features = FFS_CAPABLE;
		asic = ASIC_CALLIOPE;
		set_register_map(CALLIOPE_IO_BASE, &calliope_register_map);

		if (platform_family == FAMILY_1500VZE) {
			gp_resources = non_dvr_vze_calliope_resources;
			pr_info("Platform: 1500/Vz Class E - "
				"CALLIOPE, NON_DVR_CAPABLE\n");
		} else if (platform_family == FAMILY_1500VZF) {
			gp_resources = non_dvr_vzf_calliope_resources;
			pr_info("Platform: 1500/Vz Class F - "
				"CALLIOPE, NON_DVR_CAPABLE\n");
		} else {
			gp_resources = non_dvr_calliope_resources;
			pr_info("Platform: 1500/RNG100 - CALLIOPE, "
				"NON_DVR_CAPABLE\n");
		}
		break;

	case FAMILY_4500:
		platform_features = FFS_CAPABLE | PCIE_CAPABLE |
			DISPLAY_CAPABLE;
		asic = ASIC_ZEUS;
		set_register_map(ZEUS_IO_BASE, &zeus_register_map);
		gp_resources = non_dvr_zeus_resources;

		pr_info("Platform: 4500 - ZEUS, NON_DVR_CAPABLE\n");
		break;

	case FAMILY_4600:
	{
		unsigned int chipversion = 0;

		/* The settop has PCIE but it isn't used, so don't advertise
		 * it*/
		platform_features = FFS_CAPABLE | DISPLAY_CAPABLE;

		/* Cronus and Cronus Lite have the same register map */
		set_register_map(CRONUS_IO_BASE, &cronus_register_map);

		/* ASIC version will determine if this is a real CronusLite or
		 * Castrati(Cronus) */
		chipversion  = asic_read(chipver3) << 24;
		chipversion |= asic_read(chipver2) << 16;
		chipversion |= asic_read(chipver1) << 8;
		chipversion |= asic_read(chipver0);

		if ((chipversion == CRONUS_10) || (chipversion == CRONUS_11))
			asic = ASIC_CRONUS;
		else
			asic = ASIC_CRONUSLITE;

		gp_resources = non_dvr_cronuslite_resources;
		pr_info("Platform: 4600 - %s, NON_DVR_CAPABLE, "
			"chipversion=0x%08X\n",
			(asic == ASIC_CRONUS) ? "CRONUS" : "CRONUS LITE",
			chipversion);
		break;
	}
	case FAMILY_4600VZA:
		platform_features = FFS_CAPABLE | DISPLAY_CAPABLE;
		asic = ASIC_CRONUS;
		set_register_map(CRONUS_IO_BASE, &cronus_register_map);
		gp_resources = non_dvr_cronus_resources;

		pr_info("Platform: Vz Class A - CRONUS, NON_DVR_CAPABLE\n");
		break;

	case FAMILY_8500:
	case FAMILY_8500RNG:
		platform_features = DVR_CAPABLE | PCIE_CAPABLE |
			DISPLAY_CAPABLE;
		asic = ASIC_ZEUS;
		set_register_map(ZEUS_IO_BASE, &zeus_register_map);
		gp_resources = dvr_zeus_resources;

		pr_info("Platform: 8500/RNG200 - ZEUS, DVR_CAPABLE\n");
		break;

	case FAMILY_8600:
	case FAMILY_8600VZB:
		platform_features = DVR_CAPABLE | PCIE_CAPABLE |
			DISPLAY_CAPABLE;
		asic = ASIC_CRONUS;
		set_register_map(CRONUS_IO_BASE, &cronus_register_map);
		gp_resources = dvr_cronus_resources;

		pr_info("Platform: 8600/Vz Class B - CRONUS, "
			"DVR_CAPABLE\n");
		break;

	case FAMILY_8700:
		platform_features = FFS_CAPABLE | PCIE_CAPABLE;
		asic = ASIC_GAIA;
		set_register_map(GAIA_IO_BASE, &gaia_register_map);
		gp_resources = dvr_gaia_resources;

		pr_info("Platform: 8700 - GAIA, DVR_CAPABLE\n");
		break;

	default:
		pr_crit("Platform:  UNKNOWN PLATFORM\n");
		break;
	}

	switch (asic) {
	case ASIC_ZEUS:
		phys_to_dma_offset = 0x30000000;
		break;
	case ASIC_CALLIOPE:
		phys_to_dma_offset = 0x10000000;
		break;
	case ASIC_CRONUSLITE:
		/* Fall through */
	case ASIC_CRONUS:
		/*
		 * TODO: We suppose 0x10000000 aliases into 0x20000000-
		 * 0x2XXXXXXX. If 0x10000000 aliases into 0x60000000-
		 * 0x6XXXXXXX, the offset should be 0x50000000, not 0x10000000.
		 */
		phys_to_dma_offset = 0x10000000;
		break;
	default:
		phys_to_dma_offset = 0x00000000;
		break;
	}
}

/*
 * RESOURCE ALLOCATION
 *
 */
/*
 * Allocates/reserves the Platform memory resources early in the boot process.
 * This ignores any resources that are designated IORESOURCE_IO
 */
void __init platform_alloc_bootmem(void)
{
	int i;
	int total = 0;

	/* Get persistent memory data from command line before allocating
	 * resources. This need to happen before normal command line parsing
	 * has been done */
	pmem_setup_resource();

	/* Loop through looking for resources that want a particular address */
	for (i = 0; gp_resources[i].flags != 0; i++) {
		int size = resource_size(&gp_resources[i]);
		if ((gp_resources[i].start != 0) &&
			((gp_resources[i].flags & IORESOURCE_MEM) != 0)) {
			reserve_bootmem(dma_to_phys(gp_resources[i].start),
				size, 0);
			total += resource_size(&gp_resources[i]);
			pr_info("reserve resource %s at %08x (%u bytes)\n",
				gp_resources[i].name, gp_resources[i].start,
				resource_size(&gp_resources[i]));
		}
	}

	/* Loop through assigning addresses for those that are left */
	for (i = 0; gp_resources[i].flags != 0; i++) {
		int size = resource_size(&gp_resources[i]);
		if ((gp_resources[i].start == 0) &&
			((gp_resources[i].flags & IORESOURCE_MEM) != 0)) {
			void *mem = alloc_bootmem_pages(size);

			if (mem == NULL)
				pr_err("Unable to allocate bootmem pages "
					"for %s\n", gp_resources[i].name);

			else {
				gp_resources[i].start =
					phys_to_dma(virt_to_phys(mem));
				gp_resources[i].end =
					gp_resources[i].start + size - 1;
				total += size;
				pr_info("allocate resource %s at %08x "
						"(%u bytes)\n",
					gp_resources[i].name,
					gp_resources[i].start, size);
			}
		}
	}

	pr_info("Total Platform driver memory allocation: 0x%08x\n", total);

	/* indicate resources that are platform I/O related */
	for (i = 0; gp_resources[i].flags != 0; i++) {
		if ((gp_resources[i].start != 0) &&
			((gp_resources[i].flags & IORESOURCE_IO) != 0)) {
			pr_info("reserved platform resource %s at %08x\n",
				gp_resources[i].name, gp_resources[i].start);
		}
	}
}

/*
 *
 * PERSISTENT MEMORY (PMEM) CONFIGURATION
 *
 */
static unsigned long pmemaddr __initdata;

static int __init early_param_pmemaddr(char *p)
{
	pmemaddr = (unsigned long)simple_strtoul(p, NULL, 0);
	return 0;
}
early_param("pmemaddr", early_param_pmemaddr);

static long pmemlen __initdata;

static int __init early_param_pmemlen(char *p)
{
/* TODO: we can use this code when and if the bootloader ever changes this */
#if 0
	pmemlen = (unsigned long)simple_strtoul(p, NULL, 0);
#else
	pmemlen = 0x20000;
#endif
	return 0;
}
early_param("pmemlen", early_param_pmemlen);

/*
 * Set up persistent memory. If we were given values, we patch the array of
 * resources. Otherwise, persistent memory may be allocated anywhere at all.
 */
static void __init pmem_setup_resource(void)
{
	struct resource *resource;
	resource = asic_resource_get("DiagPersistentMemory");

	if (resource && pmemaddr && pmemlen) {
		/* The address provided by bootloader is in kseg0. Convert to
		 * a bus address. */
		resource->start = phys_to_dma(pmemaddr - 0x80000000);
		resource->end = resource->start + pmemlen - 1;

		pr_info("persistent memory: start=0x%x  end=0x%x\n",
			resource->start, resource->end);
	}
}

/*
 *
 * RESOURCE ACCESS FUNCTIONS
 *
 */

/**
 * asic_resource_get - retrieves parameters for a platform resource.
 * @name:	string to match resource
 *
 * Returns a pointer to a struct resource corresponding to the given name.
 *
 * CANNOT BE NAMED platform_resource_get, which would be the obvious choice,
 * as this function name is already declared
 */
struct resource *asic_resource_get(const char *name)
{
	int i;

	for (i = 0; gp_resources[i].flags != 0; i++) {
		if (strcmp(gp_resources[i].name, name) == 0)
			return &gp_resources[i];
	}

	return NULL;
}
EXPORT_SYMBOL(asic_resource_get);

/**
 * platform_release_memory - release pre-allocated memory
 * @ptr:	pointer to memory to release
 * @size:	size of resource
 *
 * This must only be called for memory allocated or reserved via the boot
 * memory allocator.
 */
void platform_release_memory(void *ptr, int size)
{
	unsigned long addr;
	unsigned long end;

	addr = ((unsigned long)ptr + (PAGE_SIZE - 1)) & PAGE_MASK;
	end = ((unsigned long)ptr + size) & PAGE_MASK;

	for (; addr < end; addr += PAGE_SIZE) {
		ClearPageReserved(virt_to_page(__va(addr)));
		init_page_count(virt_to_page(__va(addr)));
		free_page((unsigned long)__va(addr));
	}
}
EXPORT_SYMBOL(platform_release_memory);

/*
 *
 * FEATURE AVAILABILITY FUNCTIONS
 *
 */
int platform_supports_dvr(void)
{
	return (platform_features & DVR_CAPABLE) != 0;
}

int platform_supports_ffs(void)
{
	return (platform_features & FFS_CAPABLE) != 0;
}

int platform_supports_pcie(void)
{
	return (platform_features & PCIE_CAPABLE) != 0;
}

int platform_supports_display(void)
{
	return (platform_features & DISPLAY_CAPABLE) != 0;
}

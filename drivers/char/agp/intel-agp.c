/*
 * Intel AGPGART routines.
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/pagemap.h>
#include <linux/agp_backend.h>
#include <asm/smp.h>
#include "agp.h"
#include "intel-agp.h"
#include <linux/intel-gtt.h>

#include "intel-gtt.c"

int intel_agp_enabled;
EXPORT_SYMBOL(intel_agp_enabled);

static int intel_fetch_size(void)
{
	int i;
	u16 temp;
	struct aper_size_info_16 *values;

	pci_read_config_word(agp_bridge->dev, INTEL_APSIZE, &temp);
	values = A_SIZE_16(agp_bridge->driver->aperture_sizes);

	for (i = 0; i < agp_bridge->driver->num_aperture_sizes; i++) {
		if (temp == values[i].size_value) {
			agp_bridge->previous_size = agp_bridge->current_size = (void *) (values + i);
			agp_bridge->aperture_size_idx = i;
			return values[i].size;
		}
	}

	return 0;
}

static int __intel_8xx_fetch_size(u8 temp)
{
	int i;
	struct aper_size_info_8 *values;

	values = A_SIZE_8(agp_bridge->driver->aperture_sizes);

	for (i = 0; i < agp_bridge->driver->num_aperture_sizes; i++) {
		if (temp == values[i].size_value) {
			agp_bridge->previous_size =
				agp_bridge->current_size = (void *) (values + i);
			agp_bridge->aperture_size_idx = i;
			return values[i].size;
		}
	}
	return 0;
}

static int intel_8xx_fetch_size(void)
{
	u8 temp;

	pci_read_config_byte(agp_bridge->dev, INTEL_APSIZE, &temp);
	return __intel_8xx_fetch_size(temp);
}

static int intel_815_fetch_size(void)
{
	u8 temp;

	/* Intel 815 chipsets have a _weird_ APSIZE register with only
	 * one non-reserved bit, so mask the others out ... */
	pci_read_config_byte(agp_bridge->dev, INTEL_APSIZE, &temp);
	temp &= (1 << 3);

	return __intel_8xx_fetch_size(temp);
}

static void intel_tlbflush(struct agp_memory *mem)
{
	pci_write_config_dword(agp_bridge->dev, INTEL_AGPCTRL, 0x2200);
	pci_write_config_dword(agp_bridge->dev, INTEL_AGPCTRL, 0x2280);
}


static void intel_8xx_tlbflush(struct agp_memory *mem)
{
	u32 temp;
	pci_read_config_dword(agp_bridge->dev, INTEL_AGPCTRL, &temp);
	pci_write_config_dword(agp_bridge->dev, INTEL_AGPCTRL, temp & ~(1 << 7));
	pci_read_config_dword(agp_bridge->dev, INTEL_AGPCTRL, &temp);
	pci_write_config_dword(agp_bridge->dev, INTEL_AGPCTRL, temp | (1 << 7));
}


static void intel_cleanup(void)
{
	u16 temp;
	struct aper_size_info_16 *previous_size;

	previous_size = A_SIZE_16(agp_bridge->previous_size);
	pci_read_config_word(agp_bridge->dev, INTEL_NBXCFG, &temp);
	pci_write_config_word(agp_bridge->dev, INTEL_NBXCFG, temp & ~(1 << 9));
	pci_write_config_word(agp_bridge->dev, INTEL_APSIZE, previous_size->size_value);
}


static void intel_8xx_cleanup(void)
{
	u16 temp;
	struct aper_size_info_8 *previous_size;

	previous_size = A_SIZE_8(agp_bridge->previous_size);
	pci_read_config_word(agp_bridge->dev, INTEL_NBXCFG, &temp);
	pci_write_config_word(agp_bridge->dev, INTEL_NBXCFG, temp & ~(1 << 9));
	pci_write_config_byte(agp_bridge->dev, INTEL_APSIZE, previous_size->size_value);
}


static int intel_configure(void)
{
	u32 temp;
	u16 temp2;
	struct aper_size_info_16 *current_size;

	current_size = A_SIZE_16(agp_bridge->current_size);

	/* aperture size */
	pci_write_config_word(agp_bridge->dev, INTEL_APSIZE, current_size->size_value);

	/* address to map to */
	pci_read_config_dword(agp_bridge->dev, AGP_APBASE, &temp);
	agp_bridge->gart_bus_addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);

	/* attbase - aperture base */
	pci_write_config_dword(agp_bridge->dev, INTEL_ATTBASE, agp_bridge->gatt_bus_addr);

	/* agpctrl */
	pci_write_config_dword(agp_bridge->dev, INTEL_AGPCTRL, 0x2280);

	/* paccfg/nbxcfg */
	pci_read_config_word(agp_bridge->dev, INTEL_NBXCFG, &temp2);
	pci_write_config_word(agp_bridge->dev, INTEL_NBXCFG,
			(temp2 & ~(1 << 10)) | (1 << 9));
	/* clear any possible error conditions */
	pci_write_config_byte(agp_bridge->dev, INTEL_ERRSTS + 1, 7);
	return 0;
}

static int intel_815_configure(void)
{
	u32 temp, addr;
	u8 temp2;
	struct aper_size_info_8 *current_size;

	/* attbase - aperture base */
	/* the Intel 815 chipset spec. says that bits 29-31 in the
	* ATTBASE register are reserved -> try not to write them */
	if (agp_bridge->gatt_bus_addr & INTEL_815_ATTBASE_MASK) {
		dev_emerg(&agp_bridge->dev->dev, "gatt bus addr too high");
		return -EINVAL;
	}

	current_size = A_SIZE_8(agp_bridge->current_size);

	/* aperture size */
	pci_write_config_byte(agp_bridge->dev, INTEL_APSIZE,
			current_size->size_value);

	/* address to map to */
	pci_read_config_dword(agp_bridge->dev, AGP_APBASE, &temp);
	agp_bridge->gart_bus_addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);

	pci_read_config_dword(agp_bridge->dev, INTEL_ATTBASE, &addr);
	addr &= INTEL_815_ATTBASE_MASK;
	addr |= agp_bridge->gatt_bus_addr;
	pci_write_config_dword(agp_bridge->dev, INTEL_ATTBASE, addr);

	/* agpctrl */
	pci_write_config_dword(agp_bridge->dev, INTEL_AGPCTRL, 0x0000);

	/* apcont */
	pci_read_config_byte(agp_bridge->dev, INTEL_815_APCONT, &temp2);
	pci_write_config_byte(agp_bridge->dev, INTEL_815_APCONT, temp2 | (1 << 1));

	/* clear any possible error conditions */
	/* Oddness : this chipset seems to have no ERRSTS register ! */
	return 0;
}

static void intel_820_tlbflush(struct agp_memory *mem)
{
	return;
}

static void intel_820_cleanup(void)
{
	u8 temp;
	struct aper_size_info_8 *previous_size;

	previous_size = A_SIZE_8(agp_bridge->previous_size);
	pci_read_config_byte(agp_bridge->dev, INTEL_I820_RDCR, &temp);
	pci_write_config_byte(agp_bridge->dev, INTEL_I820_RDCR,
			temp & ~(1 << 1));
	pci_write_config_byte(agp_bridge->dev, INTEL_APSIZE,
			previous_size->size_value);
}


static int intel_820_configure(void)
{
	u32 temp;
	u8 temp2;
	struct aper_size_info_8 *current_size;

	current_size = A_SIZE_8(agp_bridge->current_size);

	/* aperture size */
	pci_write_config_byte(agp_bridge->dev, INTEL_APSIZE, current_size->size_value);

	/* address to map to */
	pci_read_config_dword(agp_bridge->dev, AGP_APBASE, &temp);
	agp_bridge->gart_bus_addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);

	/* attbase - aperture base */
	pci_write_config_dword(agp_bridge->dev, INTEL_ATTBASE, agp_bridge->gatt_bus_addr);

	/* agpctrl */
	pci_write_config_dword(agp_bridge->dev, INTEL_AGPCTRL, 0x0000);

	/* global enable aperture access */
	/* This flag is not accessed through MCHCFG register as in */
	/* i850 chipset. */
	pci_read_config_byte(agp_bridge->dev, INTEL_I820_RDCR, &temp2);
	pci_write_config_byte(agp_bridge->dev, INTEL_I820_RDCR, temp2 | (1 << 1));
	/* clear any possible AGP-related error conditions */
	pci_write_config_word(agp_bridge->dev, INTEL_I820_ERRSTS, 0x001c);
	return 0;
}

static int intel_840_configure(void)
{
	u32 temp;
	u16 temp2;
	struct aper_size_info_8 *current_size;

	current_size = A_SIZE_8(agp_bridge->current_size);

	/* aperture size */
	pci_write_config_byte(agp_bridge->dev, INTEL_APSIZE, current_size->size_value);

	/* address to map to */
	pci_read_config_dword(agp_bridge->dev, AGP_APBASE, &temp);
	agp_bridge->gart_bus_addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);

	/* attbase - aperture base */
	pci_write_config_dword(agp_bridge->dev, INTEL_ATTBASE, agp_bridge->gatt_bus_addr);

	/* agpctrl */
	pci_write_config_dword(agp_bridge->dev, INTEL_AGPCTRL, 0x0000);

	/* mcgcfg */
	pci_read_config_word(agp_bridge->dev, INTEL_I840_MCHCFG, &temp2);
	pci_write_config_word(agp_bridge->dev, INTEL_I840_MCHCFG, temp2 | (1 << 9));
	/* clear any possible error conditions */
	pci_write_config_word(agp_bridge->dev, INTEL_I840_ERRSTS, 0xc000);
	return 0;
}

static int intel_845_configure(void)
{
	u32 temp;
	u8 temp2;
	struct aper_size_info_8 *current_size;

	current_size = A_SIZE_8(agp_bridge->current_size);

	/* aperture size */
	pci_write_config_byte(agp_bridge->dev, INTEL_APSIZE, current_size->size_value);

	if (agp_bridge->apbase_config != 0) {
		pci_write_config_dword(agp_bridge->dev, AGP_APBASE,
				       agp_bridge->apbase_config);
	} else {
		/* address to map to */
		pci_read_config_dword(agp_bridge->dev, AGP_APBASE, &temp);
		agp_bridge->gart_bus_addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);
		agp_bridge->apbase_config = temp;
	}

	/* attbase - aperture base */
	pci_write_config_dword(agp_bridge->dev, INTEL_ATTBASE, agp_bridge->gatt_bus_addr);

	/* agpctrl */
	pci_write_config_dword(agp_bridge->dev, INTEL_AGPCTRL, 0x0000);

	/* agpm */
	pci_read_config_byte(agp_bridge->dev, INTEL_I845_AGPM, &temp2);
	pci_write_config_byte(agp_bridge->dev, INTEL_I845_AGPM, temp2 | (1 << 1));
	/* clear any possible error conditions */
	pci_write_config_word(agp_bridge->dev, INTEL_I845_ERRSTS, 0x001c);
	return 0;
}

static int intel_850_configure(void)
{
	u32 temp;
	u16 temp2;
	struct aper_size_info_8 *current_size;

	current_size = A_SIZE_8(agp_bridge->current_size);

	/* aperture size */
	pci_write_config_byte(agp_bridge->dev, INTEL_APSIZE, current_size->size_value);

	/* address to map to */
	pci_read_config_dword(agp_bridge->dev, AGP_APBASE, &temp);
	agp_bridge->gart_bus_addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);

	/* attbase - aperture base */
	pci_write_config_dword(agp_bridge->dev, INTEL_ATTBASE, agp_bridge->gatt_bus_addr);

	/* agpctrl */
	pci_write_config_dword(agp_bridge->dev, INTEL_AGPCTRL, 0x0000);

	/* mcgcfg */
	pci_read_config_word(agp_bridge->dev, INTEL_I850_MCHCFG, &temp2);
	pci_write_config_word(agp_bridge->dev, INTEL_I850_MCHCFG, temp2 | (1 << 9));
	/* clear any possible AGP-related error conditions */
	pci_write_config_word(agp_bridge->dev, INTEL_I850_ERRSTS, 0x001c);
	return 0;
}

static int intel_860_configure(void)
{
	u32 temp;
	u16 temp2;
	struct aper_size_info_8 *current_size;

	current_size = A_SIZE_8(agp_bridge->current_size);

	/* aperture size */
	pci_write_config_byte(agp_bridge->dev, INTEL_APSIZE, current_size->size_value);

	/* address to map to */
	pci_read_config_dword(agp_bridge->dev, AGP_APBASE, &temp);
	agp_bridge->gart_bus_addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);

	/* attbase - aperture base */
	pci_write_config_dword(agp_bridge->dev, INTEL_ATTBASE, agp_bridge->gatt_bus_addr);

	/* agpctrl */
	pci_write_config_dword(agp_bridge->dev, INTEL_AGPCTRL, 0x0000);

	/* mcgcfg */
	pci_read_config_word(agp_bridge->dev, INTEL_I860_MCHCFG, &temp2);
	pci_write_config_word(agp_bridge->dev, INTEL_I860_MCHCFG, temp2 | (1 << 9));
	/* clear any possible AGP-related error conditions */
	pci_write_config_word(agp_bridge->dev, INTEL_I860_ERRSTS, 0xf700);
	return 0;
}

static int intel_830mp_configure(void)
{
	u32 temp;
	u16 temp2;
	struct aper_size_info_8 *current_size;

	current_size = A_SIZE_8(agp_bridge->current_size);

	/* aperture size */
	pci_write_config_byte(agp_bridge->dev, INTEL_APSIZE, current_size->size_value);

	/* address to map to */
	pci_read_config_dword(agp_bridge->dev, AGP_APBASE, &temp);
	agp_bridge->gart_bus_addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);

	/* attbase - aperture base */
	pci_write_config_dword(agp_bridge->dev, INTEL_ATTBASE, agp_bridge->gatt_bus_addr);

	/* agpctrl */
	pci_write_config_dword(agp_bridge->dev, INTEL_AGPCTRL, 0x0000);

	/* gmch */
	pci_read_config_word(agp_bridge->dev, INTEL_NBXCFG, &temp2);
	pci_write_config_word(agp_bridge->dev, INTEL_NBXCFG, temp2 | (1 << 9));
	/* clear any possible AGP-related error conditions */
	pci_write_config_word(agp_bridge->dev, INTEL_I830_ERRSTS, 0x1c);
	return 0;
}

static int intel_7505_configure(void)
{
	u32 temp;
	u16 temp2;
	struct aper_size_info_8 *current_size;

	current_size = A_SIZE_8(agp_bridge->current_size);

	/* aperture size */
	pci_write_config_byte(agp_bridge->dev, INTEL_APSIZE, current_size->size_value);

	/* address to map to */
	pci_read_config_dword(agp_bridge->dev, AGP_APBASE, &temp);
	agp_bridge->gart_bus_addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);

	/* attbase - aperture base */
	pci_write_config_dword(agp_bridge->dev, INTEL_ATTBASE, agp_bridge->gatt_bus_addr);

	/* agpctrl */
	pci_write_config_dword(agp_bridge->dev, INTEL_AGPCTRL, 0x0000);

	/* mchcfg */
	pci_read_config_word(agp_bridge->dev, INTEL_I7505_MCHCFG, &temp2);
	pci_write_config_word(agp_bridge->dev, INTEL_I7505_MCHCFG, temp2 | (1 << 9));

	return 0;
}

/* Setup function */
static const struct gatt_mask intel_generic_masks[] =
{
	{.mask = 0x00000017, .type = 0}
};

static const struct aper_size_info_8 intel_815_sizes[2] =
{
	{64, 16384, 4, 0},
	{32, 8192, 3, 8},
};

static const struct aper_size_info_8 intel_8xx_sizes[7] =
{
	{256, 65536, 6, 0},
	{128, 32768, 5, 32},
	{64, 16384, 4, 48},
	{32, 8192, 3, 56},
	{16, 4096, 2, 60},
	{8, 2048, 1, 62},
	{4, 1024, 0, 63}
};

static const struct aper_size_info_16 intel_generic_sizes[7] =
{
	{256, 65536, 6, 0},
	{128, 32768, 5, 32},
	{64, 16384, 4, 48},
	{32, 8192, 3, 56},
	{16, 4096, 2, 60},
	{8, 2048, 1, 62},
	{4, 1024, 0, 63}
};

static const struct aper_size_info_8 intel_830mp_sizes[4] =
{
	{256, 65536, 6, 0},
	{128, 32768, 5, 32},
	{64, 16384, 4, 48},
	{32, 8192, 3, 56}
};

static const struct agp_bridge_driver intel_generic_driver = {
	.owner			= THIS_MODULE,
	.aperture_sizes		= intel_generic_sizes,
	.size_type		= U16_APER_SIZE,
	.num_aperture_sizes	= 7,
	.needs_scratch_page	= true,
	.configure		= intel_configure,
	.fetch_size		= intel_fetch_size,
	.cleanup		= intel_cleanup,
	.tlb_flush		= intel_tlbflush,
	.mask_memory		= agp_generic_mask_memory,
	.masks			= intel_generic_masks,
	.agp_enable		= agp_generic_enable,
	.cache_flush		= global_cache_flush,
	.create_gatt_table	= agp_generic_create_gatt_table,
	.free_gatt_table	= agp_generic_free_gatt_table,
	.insert_memory		= agp_generic_insert_memory,
	.remove_memory		= agp_generic_remove_memory,
	.alloc_by_type		= agp_generic_alloc_by_type,
	.free_by_type		= agp_generic_free_by_type,
	.agp_alloc_page		= agp_generic_alloc_page,
	.agp_alloc_pages        = agp_generic_alloc_pages,
	.agp_destroy_page	= agp_generic_destroy_page,
	.agp_destroy_pages      = agp_generic_destroy_pages,
	.agp_type_to_mask_type  = agp_generic_type_to_mask_type,
};

static const struct agp_bridge_driver intel_815_driver = {
	.owner			= THIS_MODULE,
	.aperture_sizes		= intel_815_sizes,
	.size_type		= U8_APER_SIZE,
	.num_aperture_sizes	= 2,
	.needs_scratch_page	= true,
	.configure		= intel_815_configure,
	.fetch_size		= intel_815_fetch_size,
	.cleanup		= intel_8xx_cleanup,
	.tlb_flush		= intel_8xx_tlbflush,
	.mask_memory		= agp_generic_mask_memory,
	.masks			= intel_generic_masks,
	.agp_enable		= agp_generic_enable,
	.cache_flush		= global_cache_flush,
	.create_gatt_table	= agp_generic_create_gatt_table,
	.free_gatt_table	= agp_generic_free_gatt_table,
	.insert_memory		= agp_generic_insert_memory,
	.remove_memory		= agp_generic_remove_memory,
	.alloc_by_type		= agp_generic_alloc_by_type,
	.free_by_type		= agp_generic_free_by_type,
	.agp_alloc_page		= agp_generic_alloc_page,
	.agp_alloc_pages        = agp_generic_alloc_pages,
	.agp_destroy_page	= agp_generic_destroy_page,
	.agp_destroy_pages      = agp_generic_destroy_pages,
	.agp_type_to_mask_type	= agp_generic_type_to_mask_type,
};

static const struct agp_bridge_driver intel_820_driver = {
	.owner			= THIS_MODULE,
	.aperture_sizes		= intel_8xx_sizes,
	.size_type		= U8_APER_SIZE,
	.num_aperture_sizes	= 7,
	.needs_scratch_page	= true,
	.configure		= intel_820_configure,
	.fetch_size		= intel_8xx_fetch_size,
	.cleanup		= intel_820_cleanup,
	.tlb_flush		= intel_820_tlbflush,
	.mask_memory		= agp_generic_mask_memory,
	.masks			= intel_generic_masks,
	.agp_enable		= agp_generic_enable,
	.cache_flush		= global_cache_flush,
	.create_gatt_table	= agp_generic_create_gatt_table,
	.free_gatt_table	= agp_generic_free_gatt_table,
	.insert_memory		= agp_generic_insert_memory,
	.remove_memory		= agp_generic_remove_memory,
	.alloc_by_type		= agp_generic_alloc_by_type,
	.free_by_type		= agp_generic_free_by_type,
	.agp_alloc_page		= agp_generic_alloc_page,
	.agp_alloc_pages        = agp_generic_alloc_pages,
	.agp_destroy_page	= agp_generic_destroy_page,
	.agp_destroy_pages      = agp_generic_destroy_pages,
	.agp_type_to_mask_type  = agp_generic_type_to_mask_type,
};

static const struct agp_bridge_driver intel_830mp_driver = {
	.owner			= THIS_MODULE,
	.aperture_sizes		= intel_830mp_sizes,
	.size_type		= U8_APER_SIZE,
	.num_aperture_sizes	= 4,
	.needs_scratch_page	= true,
	.configure		= intel_830mp_configure,
	.fetch_size		= intel_8xx_fetch_size,
	.cleanup		= intel_8xx_cleanup,
	.tlb_flush		= intel_8xx_tlbflush,
	.mask_memory		= agp_generic_mask_memory,
	.masks			= intel_generic_masks,
	.agp_enable		= agp_generic_enable,
	.cache_flush		= global_cache_flush,
	.create_gatt_table	= agp_generic_create_gatt_table,
	.free_gatt_table	= agp_generic_free_gatt_table,
	.insert_memory		= agp_generic_insert_memory,
	.remove_memory		= agp_generic_remove_memory,
	.alloc_by_type		= agp_generic_alloc_by_type,
	.free_by_type		= agp_generic_free_by_type,
	.agp_alloc_page		= agp_generic_alloc_page,
	.agp_alloc_pages        = agp_generic_alloc_pages,
	.agp_destroy_page	= agp_generic_destroy_page,
	.agp_destroy_pages      = agp_generic_destroy_pages,
	.agp_type_to_mask_type  = agp_generic_type_to_mask_type,
};

static const struct agp_bridge_driver intel_840_driver = {
	.owner			= THIS_MODULE,
	.aperture_sizes		= intel_8xx_sizes,
	.size_type		= U8_APER_SIZE,
	.num_aperture_sizes	= 7,
	.needs_scratch_page	= true,
	.configure		= intel_840_configure,
	.fetch_size		= intel_8xx_fetch_size,
	.cleanup		= intel_8xx_cleanup,
	.tlb_flush		= intel_8xx_tlbflush,
	.mask_memory		= agp_generic_mask_memory,
	.masks			= intel_generic_masks,
	.agp_enable		= agp_generic_enable,
	.cache_flush		= global_cache_flush,
	.create_gatt_table	= agp_generic_create_gatt_table,
	.free_gatt_table	= agp_generic_free_gatt_table,
	.insert_memory		= agp_generic_insert_memory,
	.remove_memory		= agp_generic_remove_memory,
	.alloc_by_type		= agp_generic_alloc_by_type,
	.free_by_type		= agp_generic_free_by_type,
	.agp_alloc_page		= agp_generic_alloc_page,
	.agp_alloc_pages        = agp_generic_alloc_pages,
	.agp_destroy_page	= agp_generic_destroy_page,
	.agp_destroy_pages      = agp_generic_destroy_pages,
	.agp_type_to_mask_type  = agp_generic_type_to_mask_type,
};

static const struct agp_bridge_driver intel_845_driver = {
	.owner			= THIS_MODULE,
	.aperture_sizes		= intel_8xx_sizes,
	.size_type		= U8_APER_SIZE,
	.num_aperture_sizes	= 7,
	.needs_scratch_page	= true,
	.configure		= intel_845_configure,
	.fetch_size		= intel_8xx_fetch_size,
	.cleanup		= intel_8xx_cleanup,
	.tlb_flush		= intel_8xx_tlbflush,
	.mask_memory		= agp_generic_mask_memory,
	.masks			= intel_generic_masks,
	.agp_enable		= agp_generic_enable,
	.cache_flush		= global_cache_flush,
	.create_gatt_table	= agp_generic_create_gatt_table,
	.free_gatt_table	= agp_generic_free_gatt_table,
	.insert_memory		= agp_generic_insert_memory,
	.remove_memory		= agp_generic_remove_memory,
	.alloc_by_type		= agp_generic_alloc_by_type,
	.free_by_type		= agp_generic_free_by_type,
	.agp_alloc_page		= agp_generic_alloc_page,
	.agp_alloc_pages        = agp_generic_alloc_pages,
	.agp_destroy_page	= agp_generic_destroy_page,
	.agp_destroy_pages      = agp_generic_destroy_pages,
	.agp_type_to_mask_type  = agp_generic_type_to_mask_type,
};

static const struct agp_bridge_driver intel_850_driver = {
	.owner			= THIS_MODULE,
	.aperture_sizes		= intel_8xx_sizes,
	.size_type		= U8_APER_SIZE,
	.num_aperture_sizes	= 7,
	.needs_scratch_page	= true,
	.configure		= intel_850_configure,
	.fetch_size		= intel_8xx_fetch_size,
	.cleanup		= intel_8xx_cleanup,
	.tlb_flush		= intel_8xx_tlbflush,
	.mask_memory		= agp_generic_mask_memory,
	.masks			= intel_generic_masks,
	.agp_enable		= agp_generic_enable,
	.cache_flush		= global_cache_flush,
	.create_gatt_table	= agp_generic_create_gatt_table,
	.free_gatt_table	= agp_generic_free_gatt_table,
	.insert_memory		= agp_generic_insert_memory,
	.remove_memory		= agp_generic_remove_memory,
	.alloc_by_type		= agp_generic_alloc_by_type,
	.free_by_type		= agp_generic_free_by_type,
	.agp_alloc_page		= agp_generic_alloc_page,
	.agp_alloc_pages        = agp_generic_alloc_pages,
	.agp_destroy_page	= agp_generic_destroy_page,
	.agp_destroy_pages      = agp_generic_destroy_pages,
	.agp_type_to_mask_type  = agp_generic_type_to_mask_type,
};

static const struct agp_bridge_driver intel_860_driver = {
	.owner			= THIS_MODULE,
	.aperture_sizes		= intel_8xx_sizes,
	.size_type		= U8_APER_SIZE,
	.num_aperture_sizes	= 7,
	.needs_scratch_page	= true,
	.configure		= intel_860_configure,
	.fetch_size		= intel_8xx_fetch_size,
	.cleanup		= intel_8xx_cleanup,
	.tlb_flush		= intel_8xx_tlbflush,
	.mask_memory		= agp_generic_mask_memory,
	.masks			= intel_generic_masks,
	.agp_enable		= agp_generic_enable,
	.cache_flush		= global_cache_flush,
	.create_gatt_table	= agp_generic_create_gatt_table,
	.free_gatt_table	= agp_generic_free_gatt_table,
	.insert_memory		= agp_generic_insert_memory,
	.remove_memory		= agp_generic_remove_memory,
	.alloc_by_type		= agp_generic_alloc_by_type,
	.free_by_type		= agp_generic_free_by_type,
	.agp_alloc_page		= agp_generic_alloc_page,
	.agp_alloc_pages        = agp_generic_alloc_pages,
	.agp_destroy_page	= agp_generic_destroy_page,
	.agp_destroy_pages      = agp_generic_destroy_pages,
	.agp_type_to_mask_type  = agp_generic_type_to_mask_type,
};

static const struct agp_bridge_driver intel_7505_driver = {
	.owner			= THIS_MODULE,
	.aperture_sizes		= intel_8xx_sizes,
	.size_type		= U8_APER_SIZE,
	.num_aperture_sizes	= 7,
	.needs_scratch_page	= true,
	.configure		= intel_7505_configure,
	.fetch_size		= intel_8xx_fetch_size,
	.cleanup		= intel_8xx_cleanup,
	.tlb_flush		= intel_8xx_tlbflush,
	.mask_memory		= agp_generic_mask_memory,
	.masks			= intel_generic_masks,
	.agp_enable		= agp_generic_enable,
	.cache_flush		= global_cache_flush,
	.create_gatt_table	= agp_generic_create_gatt_table,
	.free_gatt_table	= agp_generic_free_gatt_table,
	.insert_memory		= agp_generic_insert_memory,
	.remove_memory		= agp_generic_remove_memory,
	.alloc_by_type		= agp_generic_alloc_by_type,
	.free_by_type		= agp_generic_free_by_type,
	.agp_alloc_page		= agp_generic_alloc_page,
	.agp_alloc_pages        = agp_generic_alloc_pages,
	.agp_destroy_page	= agp_generic_destroy_page,
	.agp_destroy_pages      = agp_generic_destroy_pages,
	.agp_type_to_mask_type  = agp_generic_type_to_mask_type,
};

static int find_gmch(u16 device)
{
	struct pci_dev *gmch_device;

	gmch_device = pci_get_device(PCI_VENDOR_ID_INTEL, device, NULL);
	if (gmch_device && PCI_FUNC(gmch_device->devfn) != 0) {
		gmch_device = pci_get_device(PCI_VENDOR_ID_INTEL,
					     device, gmch_device);
	}

	if (!gmch_device)
		return 0;

	intel_private.pcidev = gmch_device;
	return 1;
}

/* Table to describe Intel GMCH and AGP/PCIE GART drivers.  At least one of
 * driver and gmch_driver must be non-null, and find_gmch will determine
 * which one should be used if a gmch_chip_id is present.
 */
static const struct intel_driver_description {
	unsigned int chip_id;
	unsigned int gmch_chip_id;
	char *name;
	const struct agp_bridge_driver *driver;
	const struct agp_bridge_driver *gmch_driver;
} intel_agp_chipsets[] = {
	{ PCI_DEVICE_ID_INTEL_82443LX_0, 0, "440LX", &intel_generic_driver, NULL },
	{ PCI_DEVICE_ID_INTEL_82443BX_0, 0, "440BX", &intel_generic_driver, NULL },
	{ PCI_DEVICE_ID_INTEL_82443GX_0, 0, "440GX", &intel_generic_driver, NULL },
	{ PCI_DEVICE_ID_INTEL_82810_MC1, PCI_DEVICE_ID_INTEL_82810_IG1, "i810",
		NULL, &intel_810_driver },
	{ PCI_DEVICE_ID_INTEL_82810_MC3, PCI_DEVICE_ID_INTEL_82810_IG3, "i810",
		NULL, &intel_810_driver },
	{ PCI_DEVICE_ID_INTEL_82810E_MC, PCI_DEVICE_ID_INTEL_82810E_IG, "i810",
		NULL, &intel_810_driver },
	{ PCI_DEVICE_ID_INTEL_82815_MC, PCI_DEVICE_ID_INTEL_82815_CGC, "i815",
		&intel_815_driver, &intel_810_driver },
	{ PCI_DEVICE_ID_INTEL_82820_HB, 0, "i820", &intel_820_driver, NULL },
	{ PCI_DEVICE_ID_INTEL_82820_UP_HB, 0, "i820", &intel_820_driver, NULL },
	{ PCI_DEVICE_ID_INTEL_82830_HB, PCI_DEVICE_ID_INTEL_82830_CGC, "830M",
		&intel_830mp_driver, &intel_830_driver },
	{ PCI_DEVICE_ID_INTEL_82840_HB, 0, "i840", &intel_840_driver, NULL },
	{ PCI_DEVICE_ID_INTEL_82845_HB, 0, "845G", &intel_845_driver, NULL },
	{ PCI_DEVICE_ID_INTEL_82845G_HB, PCI_DEVICE_ID_INTEL_82845G_IG, "830M",
		&intel_845_driver, &intel_830_driver },
	{ PCI_DEVICE_ID_INTEL_82850_HB, 0, "i850", &intel_850_driver, NULL },
	{ PCI_DEVICE_ID_INTEL_82854_HB, PCI_DEVICE_ID_INTEL_82854_IG, "854",
		&intel_845_driver, &intel_830_driver },
	{ PCI_DEVICE_ID_INTEL_82855PM_HB, 0, "855PM", &intel_845_driver, NULL },
	{ PCI_DEVICE_ID_INTEL_82855GM_HB, PCI_DEVICE_ID_INTEL_82855GM_IG, "855GM",
		&intel_845_driver, &intel_830_driver },
	{ PCI_DEVICE_ID_INTEL_82860_HB, 0, "i860", &intel_860_driver, NULL },
	{ PCI_DEVICE_ID_INTEL_82865_HB, PCI_DEVICE_ID_INTEL_82865_IG, "865",
		&intel_845_driver, &intel_830_driver },
	{ PCI_DEVICE_ID_INTEL_82875_HB, 0, "i875", &intel_845_driver, NULL },
	{ PCI_DEVICE_ID_INTEL_E7221_HB, PCI_DEVICE_ID_INTEL_E7221_IG, "E7221 (i915)",
		NULL, &intel_915_driver },
	{ PCI_DEVICE_ID_INTEL_82915G_HB, PCI_DEVICE_ID_INTEL_82915G_IG, "915G",
		NULL, &intel_915_driver },
	{ PCI_DEVICE_ID_INTEL_82915GM_HB, PCI_DEVICE_ID_INTEL_82915GM_IG, "915GM",
		NULL, &intel_915_driver },
	{ PCI_DEVICE_ID_INTEL_82945G_HB, PCI_DEVICE_ID_INTEL_82945G_IG, "945G",
		NULL, &intel_915_driver },
	{ PCI_DEVICE_ID_INTEL_82945GM_HB, PCI_DEVICE_ID_INTEL_82945GM_IG, "945GM",
		NULL, &intel_915_driver },
	{ PCI_DEVICE_ID_INTEL_82945GME_HB, PCI_DEVICE_ID_INTEL_82945GME_IG, "945GME",
		NULL, &intel_915_driver },
	{ PCI_DEVICE_ID_INTEL_82946GZ_HB, PCI_DEVICE_ID_INTEL_82946GZ_IG, "946GZ",
		NULL, &intel_i965_driver },
	{ PCI_DEVICE_ID_INTEL_82G35_HB, PCI_DEVICE_ID_INTEL_82G35_IG, "G35",
		NULL, &intel_i965_driver },
	{ PCI_DEVICE_ID_INTEL_82965Q_HB, PCI_DEVICE_ID_INTEL_82965Q_IG, "965Q",
		NULL, &intel_i965_driver },
	{ PCI_DEVICE_ID_INTEL_82965G_HB, PCI_DEVICE_ID_INTEL_82965G_IG, "965G",
		NULL, &intel_i965_driver },
	{ PCI_DEVICE_ID_INTEL_82965GM_HB, PCI_DEVICE_ID_INTEL_82965GM_IG, "965GM",
		NULL, &intel_i965_driver },
	{ PCI_DEVICE_ID_INTEL_82965GME_HB, PCI_DEVICE_ID_INTEL_82965GME_IG, "965GME/GLE",
		NULL, &intel_i965_driver },
	{ PCI_DEVICE_ID_INTEL_7505_0, 0, "E7505", &intel_7505_driver, NULL },
	{ PCI_DEVICE_ID_INTEL_7205_0, 0, "E7205", &intel_7505_driver, NULL },
	{ PCI_DEVICE_ID_INTEL_G33_HB, PCI_DEVICE_ID_INTEL_G33_IG, "G33",
		NULL, &intel_g33_driver },
	{ PCI_DEVICE_ID_INTEL_Q35_HB, PCI_DEVICE_ID_INTEL_Q35_IG, "Q35",
		NULL, &intel_g33_driver },
	{ PCI_DEVICE_ID_INTEL_Q33_HB, PCI_DEVICE_ID_INTEL_Q33_IG, "Q33",
		NULL, &intel_g33_driver },
	{ PCI_DEVICE_ID_INTEL_PINEVIEW_M_HB, PCI_DEVICE_ID_INTEL_PINEVIEW_M_IG, "GMA3150",
		NULL, &intel_g33_driver },
	{ PCI_DEVICE_ID_INTEL_PINEVIEW_HB, PCI_DEVICE_ID_INTEL_PINEVIEW_IG, "GMA3150",
		NULL, &intel_g33_driver },
	{ PCI_DEVICE_ID_INTEL_GM45_HB, PCI_DEVICE_ID_INTEL_GM45_IG,
	    "GM45", NULL, &intel_i965_driver },
	{ PCI_DEVICE_ID_INTEL_EAGLELAKE_HB, PCI_DEVICE_ID_INTEL_EAGLELAKE_IG,
	    "Eaglelake", NULL, &intel_i965_driver },
	{ PCI_DEVICE_ID_INTEL_Q45_HB, PCI_DEVICE_ID_INTEL_Q45_IG,
	    "Q45/Q43", NULL, &intel_i965_driver },
	{ PCI_DEVICE_ID_INTEL_G45_HB, PCI_DEVICE_ID_INTEL_G45_IG,
	    "G45/G43", NULL, &intel_i965_driver },
	{ PCI_DEVICE_ID_INTEL_B43_HB, PCI_DEVICE_ID_INTEL_B43_IG,
	    "B43", NULL, &intel_i965_driver },
	{ PCI_DEVICE_ID_INTEL_B43_1_HB, PCI_DEVICE_ID_INTEL_B43_1_IG,
	    "B43", NULL, &intel_i965_driver },
	{ PCI_DEVICE_ID_INTEL_G41_HB, PCI_DEVICE_ID_INTEL_G41_IG,
	    "G41", NULL, &intel_i965_driver },
	{ PCI_DEVICE_ID_INTEL_IRONLAKE_D_HB, PCI_DEVICE_ID_INTEL_IRONLAKE_D_IG,
	    "HD Graphics", NULL, &intel_i965_driver },
	{ PCI_DEVICE_ID_INTEL_IRONLAKE_M_HB, PCI_DEVICE_ID_INTEL_IRONLAKE_M_IG,
	    "HD Graphics", NULL, &intel_i965_driver },
	{ PCI_DEVICE_ID_INTEL_IRONLAKE_MA_HB, PCI_DEVICE_ID_INTEL_IRONLAKE_M_IG,
	    "HD Graphics", NULL, &intel_i965_driver },
	{ PCI_DEVICE_ID_INTEL_IRONLAKE_MC2_HB, PCI_DEVICE_ID_INTEL_IRONLAKE_M_IG,
	    "HD Graphics", NULL, &intel_i965_driver },
	{ PCI_DEVICE_ID_INTEL_SANDYBRIDGE_HB, PCI_DEVICE_ID_INTEL_SANDYBRIDGE_GT1_IG,
	    "Sandybridge", NULL, &intel_gen6_driver },
	{ PCI_DEVICE_ID_INTEL_SANDYBRIDGE_HB, PCI_DEVICE_ID_INTEL_SANDYBRIDGE_GT2_IG,
	    "Sandybridge", NULL, &intel_gen6_driver },
	{ PCI_DEVICE_ID_INTEL_SANDYBRIDGE_HB, PCI_DEVICE_ID_INTEL_SANDYBRIDGE_GT2_PLUS_IG,
	    "Sandybridge", NULL, &intel_gen6_driver },
	{ PCI_DEVICE_ID_INTEL_SANDYBRIDGE_M_HB, PCI_DEVICE_ID_INTEL_SANDYBRIDGE_M_GT1_IG,
	    "Sandybridge", NULL, &intel_gen6_driver },
	{ PCI_DEVICE_ID_INTEL_SANDYBRIDGE_M_HB, PCI_DEVICE_ID_INTEL_SANDYBRIDGE_M_GT2_IG,
	    "Sandybridge", NULL, &intel_gen6_driver },
	{ PCI_DEVICE_ID_INTEL_SANDYBRIDGE_M_HB, PCI_DEVICE_ID_INTEL_SANDYBRIDGE_M_GT2_PLUS_IG,
	    "Sandybridge", NULL, &intel_gen6_driver },
	{ PCI_DEVICE_ID_INTEL_SANDYBRIDGE_S_HB, PCI_DEVICE_ID_INTEL_SANDYBRIDGE_S_IG,
	    "Sandybridge", NULL, &intel_gen6_driver },
	{ 0, 0, NULL, NULL, NULL }
};

static int __devinit intel_gmch_probe(struct pci_dev *pdev,
				      struct agp_bridge_data *bridge)
{
	int i, mask;

	bridge->driver = NULL;

	for (i = 0; intel_agp_chipsets[i].name != NULL; i++) {
		if ((intel_agp_chipsets[i].gmch_chip_id != 0) &&
			find_gmch(intel_agp_chipsets[i].gmch_chip_id)) {
			bridge->driver =
				intel_agp_chipsets[i].gmch_driver;
			break;
		}
	}

	if (!bridge->driver)
		return 0;

	bridge->dev_private_data = &intel_private;
	bridge->dev = pdev;

	dev_info(&pdev->dev, "Intel %s Chipset\n", intel_agp_chipsets[i].name);

	if (bridge->driver->mask_memory == intel_gen6_mask_memory)
		mask = 40;
	else if (bridge->driver->mask_memory == intel_i965_mask_memory)
		mask = 36;
	else
		mask = 32;

	if (pci_set_dma_mask(intel_private.pcidev, DMA_BIT_MASK(mask)))
		dev_err(&intel_private.pcidev->dev,
			"set gfx device dma mask %d-bit failed!\n", mask);
	else
		pci_set_consistent_dma_mask(intel_private.pcidev,
					    DMA_BIT_MASK(mask));

	return 1;
}

static int __devinit agp_intel_probe(struct pci_dev *pdev,
				     const struct pci_device_id *ent)
{
	struct agp_bridge_data *bridge;
	u8 cap_ptr = 0;
	struct resource *r;
	int i, err;

	cap_ptr = pci_find_capability(pdev, PCI_CAP_ID_AGP);

	bridge = agp_alloc_bridge();
	if (!bridge)
		return -ENOMEM;

	bridge->capndx = cap_ptr;

	if (intel_gmch_probe(pdev, bridge))
		goto found_gmch;

	for (i = 0; intel_agp_chipsets[i].name != NULL; i++) {
		/* In case that multiple models of gfx chip may
		   stand on same host bridge type, this can be
		   sure we detect the right IGD. */
		if (pdev->device == intel_agp_chipsets[i].chip_id) {
			bridge->driver = intel_agp_chipsets[i].driver;
			break;
		}
	}

	if (intel_agp_chipsets[i].name == NULL) {
		if (cap_ptr)
			dev_warn(&pdev->dev, "unsupported Intel chipset [%04x/%04x]\n",
				 pdev->vendor, pdev->device);
		agp_put_bridge(bridge);
		return -ENODEV;
	}

	if (!bridge->driver) {
		if (cap_ptr)
			dev_warn(&pdev->dev, "can't find bridge device (chip_id: %04x)\n",
			    	 intel_agp_chipsets[i].gmch_chip_id);
		agp_put_bridge(bridge);
		return -ENODEV;
	}

	bridge->dev = pdev;
	bridge->dev_private_data = NULL;

	dev_info(&pdev->dev, "Intel %s Chipset\n", intel_agp_chipsets[i].name);

	/*
	* If the device has not been properly setup, the following will catch
	* the problem and should stop the system from crashing.
	* 20030610 - hamish@zot.org
	*/
	if (pci_enable_device(pdev)) {
		dev_err(&pdev->dev, "can't enable PCI device\n");
		agp_put_bridge(bridge);
		return -ENODEV;
	}

	/*
	* The following fixes the case where the BIOS has "forgotten" to
	* provide an address range for the GART.
	* 20030610 - hamish@zot.org
	*/
	r = &pdev->resource[0];
	if (!r->start && r->end) {
		if (pci_assign_resource(pdev, 0)) {
			dev_err(&pdev->dev, "can't assign resource 0\n");
			agp_put_bridge(bridge);
			return -ENODEV;
		}
	}

	/* Fill in the mode register */
	if (cap_ptr) {
		pci_read_config_dword(pdev,
				bridge->capndx+PCI_AGP_STATUS,
				&bridge->mode);
	}

found_gmch:
	pci_set_drvdata(pdev, bridge);
	err = agp_add_bridge(bridge);
	if (!err)
		intel_agp_enabled = 1;
	return err;
}

static void __devexit agp_intel_remove(struct pci_dev *pdev)
{
	struct agp_bridge_data *bridge = pci_get_drvdata(pdev);

	agp_remove_bridge(bridge);

	if (intel_private.pcidev)
		pci_dev_put(intel_private.pcidev);

	agp_put_bridge(bridge);
}

#ifdef CONFIG_PM
static int agp_intel_resume(struct pci_dev *pdev)
{
	struct agp_bridge_data *bridge = pci_get_drvdata(pdev);
	int ret_val;

	bridge->driver->configure();

	ret_val = agp_rebind_memory();
	if (ret_val != 0)
		return ret_val;

	return 0;
}
#endif

static struct pci_device_id agp_intel_pci_table[] = {
#define ID(x)						\
	{						\
	.class		= (PCI_CLASS_BRIDGE_HOST << 8),	\
	.class_mask	= ~0,				\
	.vendor		= PCI_VENDOR_ID_INTEL,		\
	.device		= x,				\
	.subvendor	= PCI_ANY_ID,			\
	.subdevice	= PCI_ANY_ID,			\
	}
	ID(PCI_DEVICE_ID_INTEL_82443LX_0),
	ID(PCI_DEVICE_ID_INTEL_82443BX_0),
	ID(PCI_DEVICE_ID_INTEL_82443GX_0),
	ID(PCI_DEVICE_ID_INTEL_82810_MC1),
	ID(PCI_DEVICE_ID_INTEL_82810_MC3),
	ID(PCI_DEVICE_ID_INTEL_82810E_MC),
	ID(PCI_DEVICE_ID_INTEL_82815_MC),
	ID(PCI_DEVICE_ID_INTEL_82820_HB),
	ID(PCI_DEVICE_ID_INTEL_82820_UP_HB),
	ID(PCI_DEVICE_ID_INTEL_82830_HB),
	ID(PCI_DEVICE_ID_INTEL_82840_HB),
	ID(PCI_DEVICE_ID_INTEL_82845_HB),
	ID(PCI_DEVICE_ID_INTEL_82845G_HB),
	ID(PCI_DEVICE_ID_INTEL_82850_HB),
	ID(PCI_DEVICE_ID_INTEL_82854_HB),
	ID(PCI_DEVICE_ID_INTEL_82855PM_HB),
	ID(PCI_DEVICE_ID_INTEL_82855GM_HB),
	ID(PCI_DEVICE_ID_INTEL_82860_HB),
	ID(PCI_DEVICE_ID_INTEL_82865_HB),
	ID(PCI_DEVICE_ID_INTEL_82875_HB),
	ID(PCI_DEVICE_ID_INTEL_7505_0),
	ID(PCI_DEVICE_ID_INTEL_7205_0),
	ID(PCI_DEVICE_ID_INTEL_E7221_HB),
	ID(PCI_DEVICE_ID_INTEL_82915G_HB),
	ID(PCI_DEVICE_ID_INTEL_82915GM_HB),
	ID(PCI_DEVICE_ID_INTEL_82945G_HB),
	ID(PCI_DEVICE_ID_INTEL_82945GM_HB),
	ID(PCI_DEVICE_ID_INTEL_82945GME_HB),
	ID(PCI_DEVICE_ID_INTEL_PINEVIEW_M_HB),
	ID(PCI_DEVICE_ID_INTEL_PINEVIEW_HB),
	ID(PCI_DEVICE_ID_INTEL_82946GZ_HB),
	ID(PCI_DEVICE_ID_INTEL_82G35_HB),
	ID(PCI_DEVICE_ID_INTEL_82965Q_HB),
	ID(PCI_DEVICE_ID_INTEL_82965G_HB),
	ID(PCI_DEVICE_ID_INTEL_82965GM_HB),
	ID(PCI_DEVICE_ID_INTEL_82965GME_HB),
	ID(PCI_DEVICE_ID_INTEL_G33_HB),
	ID(PCI_DEVICE_ID_INTEL_Q35_HB),
	ID(PCI_DEVICE_ID_INTEL_Q33_HB),
	ID(PCI_DEVICE_ID_INTEL_GM45_HB),
	ID(PCI_DEVICE_ID_INTEL_EAGLELAKE_HB),
	ID(PCI_DEVICE_ID_INTEL_Q45_HB),
	ID(PCI_DEVICE_ID_INTEL_G45_HB),
	ID(PCI_DEVICE_ID_INTEL_G41_HB),
	ID(PCI_DEVICE_ID_INTEL_B43_HB),
	ID(PCI_DEVICE_ID_INTEL_B43_1_HB),
	ID(PCI_DEVICE_ID_INTEL_IRONLAKE_D_HB),
	ID(PCI_DEVICE_ID_INTEL_IRONLAKE_M_HB),
	ID(PCI_DEVICE_ID_INTEL_IRONLAKE_MA_HB),
	ID(PCI_DEVICE_ID_INTEL_IRONLAKE_MC2_HB),
	ID(PCI_DEVICE_ID_INTEL_SANDYBRIDGE_HB),
	ID(PCI_DEVICE_ID_INTEL_SANDYBRIDGE_M_HB),
	ID(PCI_DEVICE_ID_INTEL_SANDYBRIDGE_S_HB),
	{ }
};

MODULE_DEVICE_TABLE(pci, agp_intel_pci_table);

static struct pci_driver agp_intel_pci_driver = {
	.name		= "agpgart-intel",
	.id_table	= agp_intel_pci_table,
	.probe		= agp_intel_probe,
	.remove		= __devexit_p(agp_intel_remove),
#ifdef CONFIG_PM
	.resume		= agp_intel_resume,
#endif
};

static int __init agp_intel_init(void)
{
	if (agp_off)
		return -EINVAL;
	return pci_register_driver(&agp_intel_pci_driver);
}

static void __exit agp_intel_cleanup(void)
{
	pci_unregister_driver(&agp_intel_pci_driver);
}

module_init(agp_intel_init);
module_exit(agp_intel_cleanup);

MODULE_AUTHOR("Dave Jones <davej@redhat.com>");
MODULE_LICENSE("GPL and additional rights");

/*
 * Nvidia AGPGART routines.
 * Based upon a 2.4 agpgart diff by the folks from NVIDIA, and hacked up
 * to work in 2.5 by Dave Jones <davej@codemonkey.org.uk>
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/agp_backend.h>
#include <linux/gfp.h>
#include <linux/page-flags.h>
#include <linux/mm.h>
#include <linux/jiffies.h>
#include "agp.h"

/* NVIDIA registers */
#define NVIDIA_0_APSIZE		0x80
#define NVIDIA_1_WBC		0xf0
#define NVIDIA_2_GARTCTRL	0xd0
#define NVIDIA_2_APBASE		0xd8
#define NVIDIA_2_APLIMIT	0xdc
#define NVIDIA_2_ATTBASE(i)	(0xe0 + (i) * 4)
#define NVIDIA_3_APBASE		0x50
#define NVIDIA_3_APLIMIT	0x54


static struct _nvidia_private {
	struct pci_dev *dev_1;
	struct pci_dev *dev_2;
	struct pci_dev *dev_3;
	volatile u32 __iomem *aperture;
	int num_active_entries;
	off_t pg_offset;
	u32 wbc_mask;
} nvidia_private;


static int nvidia_fetch_size(void)
{
	int i;
	u8 size_value;
	struct aper_size_info_8 *values;

	pci_read_config_byte(agp_bridge->dev, NVIDIA_0_APSIZE, &size_value);
	size_value &= 0x0f;
	values = A_SIZE_8(agp_bridge->driver->aperture_sizes);

	for (i = 0; i < agp_bridge->driver->num_aperture_sizes; i++) {
		if (size_value == values[i].size_value) {
			agp_bridge->previous_size =
				agp_bridge->current_size = (void *) (values + i);
			agp_bridge->aperture_size_idx = i;
			return values[i].size;
		}
	}

	return 0;
}

#define SYSCFG          0xC0010010
#define IORR_BASE0      0xC0010016
#define IORR_MASK0      0xC0010017
#define AMD_K7_NUM_IORR 2

static int nvidia_init_iorr(u32 base, u32 size)
{
	u32 base_hi, base_lo;
	u32 mask_hi, mask_lo;
	u32 sys_hi, sys_lo;
	u32 iorr_addr, free_iorr_addr;

	/* Find the iorr that is already used for the base */
	/* If not found, determine the uppermost available iorr */
	free_iorr_addr = AMD_K7_NUM_IORR;
	for (iorr_addr = 0; iorr_addr < AMD_K7_NUM_IORR; iorr_addr++) {
		rdmsr(IORR_BASE0 + 2 * iorr_addr, base_lo, base_hi);
		rdmsr(IORR_MASK0 + 2 * iorr_addr, mask_lo, mask_hi);

		if ((base_lo & 0xfffff000) == (base & 0xfffff000))
			break;

		if ((mask_lo & 0x00000800) == 0)
			free_iorr_addr = iorr_addr;
	}

	if (iorr_addr >= AMD_K7_NUM_IORR) {
		iorr_addr = free_iorr_addr;
		if (iorr_addr >= AMD_K7_NUM_IORR)
			return -EINVAL;
	}
    base_hi = 0x0;
    base_lo = (base & ~0xfff) | 0x18;
    mask_hi = 0xf;
    mask_lo = ((~(size - 1)) & 0xfffff000) | 0x800;
    wrmsr(IORR_BASE0 + 2 * iorr_addr, base_lo, base_hi);
    wrmsr(IORR_MASK0 + 2 * iorr_addr, mask_lo, mask_hi);

    rdmsr(SYSCFG, sys_lo, sys_hi);
    sys_lo |= 0x00100000;
    wrmsr(SYSCFG, sys_lo, sys_hi);

	return 0;
}

static int nvidia_configure(void)
{
	int i, rc, num_dirs;
	u32 apbase, aplimit;
	struct aper_size_info_8 *current_size;
	u32 temp;

	current_size = A_SIZE_8(agp_bridge->current_size);

	/* aperture size */
	pci_write_config_byte(agp_bridge->dev, NVIDIA_0_APSIZE,
		current_size->size_value);

    /* address to map to */
	pci_read_config_dword(agp_bridge->dev, AGP_APBASE, &apbase);
	apbase &= PCI_BASE_ADDRESS_MEM_MASK;
	agp_bridge->gart_bus_addr = apbase;
	aplimit = apbase + (current_size->size * 1024 * 1024) - 1;
	pci_write_config_dword(nvidia_private.dev_2, NVIDIA_2_APBASE, apbase);
	pci_write_config_dword(nvidia_private.dev_2, NVIDIA_2_APLIMIT, aplimit);
	pci_write_config_dword(nvidia_private.dev_3, NVIDIA_3_APBASE, apbase);
	pci_write_config_dword(nvidia_private.dev_3, NVIDIA_3_APLIMIT, aplimit);
	if (0 != (rc = nvidia_init_iorr(apbase, current_size->size * 1024 * 1024)))
		return rc;

	/* directory size is 64k */
	num_dirs = current_size->size / 64;
	nvidia_private.num_active_entries = current_size->num_entries;
	nvidia_private.pg_offset = 0;
	if (num_dirs == 0) {
		num_dirs = 1;
		nvidia_private.num_active_entries /= (64 / current_size->size);
		nvidia_private.pg_offset = (apbase & (64 * 1024 * 1024 - 1) &
			~(current_size->size * 1024 * 1024 - 1)) / PAGE_SIZE;
	}

	/* attbase */
	for (i = 0; i < 8; i++) {
		pci_write_config_dword(nvidia_private.dev_2, NVIDIA_2_ATTBASE(i),
			(agp_bridge->gatt_bus_addr + (i % num_dirs) * 64 * 1024) | 1);
	}

	/* gtlb control */
	pci_read_config_dword(nvidia_private.dev_2, NVIDIA_2_GARTCTRL, &temp);
	pci_write_config_dword(nvidia_private.dev_2, NVIDIA_2_GARTCTRL, temp | 0x11);

	/* gart control */
	pci_read_config_dword(agp_bridge->dev, NVIDIA_0_APSIZE, &temp);
	pci_write_config_dword(agp_bridge->dev, NVIDIA_0_APSIZE, temp | 0x100);

	/* map aperture */
	nvidia_private.aperture =
		(volatile u32 __iomem *) ioremap(apbase, 33 * PAGE_SIZE);

	return 0;
}

static void nvidia_cleanup(void)
{
	struct aper_size_info_8 *previous_size;
	u32 temp;

	/* gart control */
	pci_read_config_dword(agp_bridge->dev, NVIDIA_0_APSIZE, &temp);
	pci_write_config_dword(agp_bridge->dev, NVIDIA_0_APSIZE, temp & ~(0x100));

	/* gtlb control */
	pci_read_config_dword(nvidia_private.dev_2, NVIDIA_2_GARTCTRL, &temp);
	pci_write_config_dword(nvidia_private.dev_2, NVIDIA_2_GARTCTRL, temp & ~(0x11));

	/* unmap aperture */
	iounmap((void __iomem *) nvidia_private.aperture);

	/* restore previous aperture size */
	previous_size = A_SIZE_8(agp_bridge->previous_size);
	pci_write_config_byte(agp_bridge->dev, NVIDIA_0_APSIZE,
		previous_size->size_value);

	/* restore iorr for previous aperture size */
	nvidia_init_iorr(agp_bridge->gart_bus_addr,
		previous_size->size * 1024 * 1024);
}


/*
 * Note we can't use the generic routines, even though they are 99% the same.
 * Aperture sizes <64M still requires a full 64k GART directory, but
 * only use the portion of the TLB entries that correspond to the apertures
 * alignment inside the surrounding 64M block.
 */
extern int agp_memory_reserved;

static int nvidia_insert_memory(struct agp_memory *mem, off_t pg_start, int type)
{
	int i, j;

	if ((type != 0) || (mem->type != 0))
		return -EINVAL;

	if ((pg_start + mem->page_count) >
		(nvidia_private.num_active_entries - agp_memory_reserved/PAGE_SIZE))
		return -EINVAL;

	for (j = pg_start; j < (pg_start + mem->page_count); j++) {
		if (!PGE_EMPTY(agp_bridge, readl(agp_bridge->gatt_table+nvidia_private.pg_offset+j)))
			return -EBUSY;
	}

	if (mem->is_flushed == FALSE) {
		global_cache_flush();
		mem->is_flushed = TRUE;
	}
	for (i = 0, j = pg_start; i < mem->page_count; i++, j++) {
		writel(agp_bridge->driver->mask_memory(agp_bridge,
			mem->memory[i], mem->type),
			agp_bridge->gatt_table+nvidia_private.pg_offset+j);
		readl(agp_bridge->gatt_table+nvidia_private.pg_offset+j);	/* PCI Posting. */
	}
	agp_bridge->driver->tlb_flush(mem);
	return 0;
}


static int nvidia_remove_memory(struct agp_memory *mem, off_t pg_start, int type)
{
	int i;

	if ((type != 0) || (mem->type != 0))
		return -EINVAL;

	for (i = pg_start; i < (mem->page_count + pg_start); i++)
		writel(agp_bridge->scratch_page, agp_bridge->gatt_table+nvidia_private.pg_offset+i);

	agp_bridge->driver->tlb_flush(mem);
	return 0;
}


static void nvidia_tlbflush(struct agp_memory *mem)
{
	unsigned long end;
	u32 wbc_reg, temp;
	int i;

	/* flush chipset */
	if (nvidia_private.wbc_mask) {
		pci_read_config_dword(nvidia_private.dev_1, NVIDIA_1_WBC, &wbc_reg);
		wbc_reg |= nvidia_private.wbc_mask;
		pci_write_config_dword(nvidia_private.dev_1, NVIDIA_1_WBC, wbc_reg);

		end = jiffies + 3*HZ;
		do {
			pci_read_config_dword(nvidia_private.dev_1,
					NVIDIA_1_WBC, &wbc_reg);
			if (time_before_eq(end, jiffies)) {
				printk(KERN_ERR PFX
				    "TLB flush took more than 3 seconds.\n");
			}
		} while (wbc_reg & nvidia_private.wbc_mask);
	}

	/* flush TLB entries */
	for (i = 0; i < 32 + 1; i++)
		temp = readl(nvidia_private.aperture+(i * PAGE_SIZE / sizeof(u32)));
	for (i = 0; i < 32 + 1; i++)
		temp = readl(nvidia_private.aperture+(i * PAGE_SIZE / sizeof(u32)));
}


static struct aper_size_info_8 nvidia_generic_sizes[5] =
{
	{512, 131072, 7, 0},
	{256, 65536, 6, 8},
	{128, 32768, 5, 12},
	{64, 16384, 4, 14},
	/* The 32M mode still requires a 64k gatt */
	{32, 16384, 4, 15}
};


static struct gatt_mask nvidia_generic_masks[] =
{
	{ .mask = 1, .type = 0}
};


static struct agp_bridge_driver nvidia_driver = {
	.owner			= THIS_MODULE,
	.aperture_sizes		= nvidia_generic_sizes,
	.size_type		= U8_APER_SIZE,
	.num_aperture_sizes	= 5,
	.configure		= nvidia_configure,
	.fetch_size		= nvidia_fetch_size,
	.cleanup		= nvidia_cleanup,
	.tlb_flush		= nvidia_tlbflush,
	.mask_memory		= agp_generic_mask_memory,
	.masks			= nvidia_generic_masks,
	.agp_enable		= agp_generic_enable,
	.cache_flush		= global_cache_flush,
	.create_gatt_table	= agp_generic_create_gatt_table,
	.free_gatt_table	= agp_generic_free_gatt_table,
	.insert_memory		= nvidia_insert_memory,
	.remove_memory		= nvidia_remove_memory,
	.alloc_by_type		= agp_generic_alloc_by_type,
	.free_by_type		= agp_generic_free_by_type,
	.agp_alloc_page		= agp_generic_alloc_page,
	.agp_destroy_page	= agp_generic_destroy_page,
};

static int __devinit agp_nvidia_probe(struct pci_dev *pdev,
				      const struct pci_device_id *ent)
{
	struct agp_bridge_data *bridge;
	u8 cap_ptr;

	nvidia_private.dev_1 =
		pci_find_slot((unsigned int)pdev->bus->number, PCI_DEVFN(0, 1));
	nvidia_private.dev_2 =
		pci_find_slot((unsigned int)pdev->bus->number, PCI_DEVFN(0, 2));
	nvidia_private.dev_3 =
		pci_find_slot((unsigned int)pdev->bus->number, PCI_DEVFN(30, 0));

	if (!nvidia_private.dev_1 || !nvidia_private.dev_2 || !nvidia_private.dev_3) {
		printk(KERN_INFO PFX "Detected an NVIDIA nForce/nForce2 "
			"chipset, but could not find the secondary devices.\n");
		return -ENODEV;
	}

	cap_ptr = pci_find_capability(pdev, PCI_CAP_ID_AGP);
	if (!cap_ptr)
		return -ENODEV;

	switch (pdev->device) {
	case PCI_DEVICE_ID_NVIDIA_NFORCE:
		printk(KERN_INFO PFX "Detected NVIDIA nForce chipset\n");
		nvidia_private.wbc_mask = 0x00010000;
		break;
	case PCI_DEVICE_ID_NVIDIA_NFORCE2:
		printk(KERN_INFO PFX "Detected NVIDIA nForce2 chipset\n");
		nvidia_private.wbc_mask = 0x80000000;
		break;
	default:
		printk(KERN_ERR PFX "Unsupported NVIDIA chipset (device id: %04x)\n",
			    pdev->device);
		return -ENODEV;
	}

	bridge = agp_alloc_bridge();
	if (!bridge)
		return -ENOMEM;

	bridge->driver = &nvidia_driver;
	bridge->dev_private_data = &nvidia_private,
	bridge->dev = pdev;
	bridge->capndx = cap_ptr;

	/* Fill in the mode register */
	pci_read_config_dword(pdev,
			bridge->capndx+PCI_AGP_STATUS,
			&bridge->mode);

	pci_set_drvdata(pdev, bridge);
	return agp_add_bridge(bridge);
}

static void __devexit agp_nvidia_remove(struct pci_dev *pdev)
{
	struct agp_bridge_data *bridge = pci_get_drvdata(pdev);

	agp_remove_bridge(bridge);
	agp_put_bridge(bridge);
}

static struct pci_device_id agp_nvidia_pci_table[] = {
	{
	.class		= (PCI_CLASS_BRIDGE_HOST << 8),
	.class_mask	= ~0,
	.vendor		= PCI_VENDOR_ID_NVIDIA,
	.device		= PCI_DEVICE_ID_NVIDIA_NFORCE,
	.subvendor	= PCI_ANY_ID,
	.subdevice	= PCI_ANY_ID,
	},
	{
	.class		= (PCI_CLASS_BRIDGE_HOST << 8),
	.class_mask	= ~0,
	.vendor		= PCI_VENDOR_ID_NVIDIA,
	.device		= PCI_DEVICE_ID_NVIDIA_NFORCE2,
	.subvendor	= PCI_ANY_ID,
	.subdevice	= PCI_ANY_ID,
	},
	{ }
};

MODULE_DEVICE_TABLE(pci, agp_nvidia_pci_table);

static struct pci_driver agp_nvidia_pci_driver = {
	.name		= "agpgart-nvidia",
	.id_table	= agp_nvidia_pci_table,
	.probe		= agp_nvidia_probe,
	.remove		= agp_nvidia_remove,
};

static int __init agp_nvidia_init(void)
{
	if (agp_off)
		return -EINVAL;
	return pci_register_driver(&agp_nvidia_pci_driver);
}

static void __exit agp_nvidia_cleanup(void)
{
	pci_unregister_driver(&agp_nvidia_pci_driver);
}

module_init(agp_nvidia_init);
module_exit(agp_nvidia_cleanup);

MODULE_LICENSE("GPL and additional rights");
MODULE_AUTHOR("NVIDIA Corporation");


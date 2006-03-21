/*
 * Copyright 2001-2003 SuSE Labs.
 * Distributed under the GNU public license, v2.
 *
 * This is a GART driver for the AMD Opteron/Athlon64 on-CPU northbridge.
 * It also includes support for the AMD 8151 AGP bridge,
 * although it doesn't actually do much, as all the real
 * work is done in the northbridge(s).
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/agp_backend.h>
#include <linux/mmzone.h>
#include <asm/page.h>		/* PAGE_SIZE */
#include "agp.h"

/* Will need to be increased if AMD64 ever goes >8-way. */
#define MAX_HAMMER_GARTS   8

/* PTE bits. */
#define GPTE_VALID	1
#define GPTE_COHERENT	2

/* Aperture control register bits. */
#define GARTEN		(1<<0)
#define DISGARTCPU	(1<<4)
#define DISGARTIO	(1<<5)

/* GART cache control register bits. */
#define INVGART		(1<<0)
#define GARTPTEERR	(1<<1)

/* K8 On-cpu GART registers */
#define AMD64_GARTAPERTURECTL	0x90
#define AMD64_GARTAPERTUREBASE	0x94
#define AMD64_GARTTABLEBASE	0x98
#define AMD64_GARTCACHECTL	0x9c
#define AMD64_GARTEN		(1<<0)

/* NVIDIA K8 registers */
#define NVIDIA_X86_64_0_APBASE		0x10
#define NVIDIA_X86_64_1_APBASE1		0x50
#define NVIDIA_X86_64_1_APLIMIT1	0x54
#define NVIDIA_X86_64_1_APSIZE		0xa8
#define NVIDIA_X86_64_1_APBASE2		0xd8
#define NVIDIA_X86_64_1_APLIMIT2	0xdc

/* ULi K8 registers */
#define ULI_X86_64_BASE_ADDR		0x10
#define ULI_X86_64_HTT_FEA_REG		0x50
#define ULI_X86_64_ENU_SCR_REG		0x54

static int nr_garts;
static struct pci_dev * hammers[MAX_HAMMER_GARTS];

static struct resource *aperture_resource;
static int __initdata agp_try_unsupported = 1;

#define for_each_nb() for(gart_iterator=0;gart_iterator<nr_garts;gart_iterator++)

static void flush_amd64_tlb(struct pci_dev *dev)
{
	u32 tmp;

	pci_read_config_dword (dev, AMD64_GARTCACHECTL, &tmp);
	tmp |= INVGART;
	pci_write_config_dword (dev, AMD64_GARTCACHECTL, tmp);
}

static void amd64_tlbflush(struct agp_memory *temp)
{
	int gart_iterator;
	for_each_nb()
		flush_amd64_tlb(hammers[gart_iterator]);
}

static int amd64_insert_memory(struct agp_memory *mem, off_t pg_start, int type)
{
	int i, j, num_entries;
	long long tmp;
	u32 pte;

	num_entries = agp_num_entries();

	if (type != 0 || mem->type != 0)
		return -EINVAL;

	/* Make sure we can fit the range in the gatt table. */
	/* FIXME: could wrap */
	if (((unsigned long)pg_start + mem->page_count) > num_entries)
		return -EINVAL;

	j = pg_start;

	/* gatt table should be empty. */
	while (j < (pg_start + mem->page_count)) {
		if (!PGE_EMPTY(agp_bridge, readl(agp_bridge->gatt_table+j)))
			return -EBUSY;
		j++;
	}

	if (mem->is_flushed == FALSE) {
		global_cache_flush();
		mem->is_flushed = TRUE;
	}

	for (i = 0, j = pg_start; i < mem->page_count; i++, j++) {
		tmp = agp_bridge->driver->mask_memory(agp_bridge,
			mem->memory[i], mem->type);

		BUG_ON(tmp & 0xffffff0000000ffcULL);
		pte = (tmp & 0x000000ff00000000ULL) >> 28;
		pte |=(tmp & 0x00000000fffff000ULL);
		pte |= GPTE_VALID | GPTE_COHERENT;

		writel(pte, agp_bridge->gatt_table+j);
		readl(agp_bridge->gatt_table+j);	/* PCI Posting. */
	}
	amd64_tlbflush(mem);
	return 0;
}

/*
 * This hack alters the order element according
 * to the size of a long. It sucks. I totally disown this, even
 * though it does appear to work for the most part.
 */
static struct aper_size_info_32 amd64_aperture_sizes[7] =
{
	{32,   8192,   3+(sizeof(long)/8), 0 },
	{64,   16384,  4+(sizeof(long)/8), 1<<1 },
	{128,  32768,  5+(sizeof(long)/8), 1<<2 },
	{256,  65536,  6+(sizeof(long)/8), 1<<1 | 1<<2 },
	{512,  131072, 7+(sizeof(long)/8), 1<<3 },
	{1024, 262144, 8+(sizeof(long)/8), 1<<1 | 1<<3},
	{2048, 524288, 9+(sizeof(long)/8), 1<<2 | 1<<3}
};


/*
 * Get the current Aperture size from the x86-64.
 * Note, that there may be multiple x86-64's, but we just return
 * the value from the first one we find. The set_size functions
 * keep the rest coherent anyway. Or at least should do.
 */
static int amd64_fetch_size(void)
{
	struct pci_dev *dev;
	int i;
	u32 temp;
	struct aper_size_info_32 *values;

	dev = hammers[0];
	if (dev==NULL)
		return 0;

	pci_read_config_dword(dev, AMD64_GARTAPERTURECTL, &temp);
	temp = (temp & 0xe);
	values = A_SIZE_32(amd64_aperture_sizes);

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

/*
 * In a multiprocessor x86-64 system, this function gets
 * called once for each CPU.
 */
static u64 amd64_configure (struct pci_dev *hammer, u64 gatt_table)
{
	u64 aperturebase;
	u32 tmp;
	u64 addr, aper_base;

	/* Address to map to */
	pci_read_config_dword (hammer, AMD64_GARTAPERTUREBASE, &tmp);
	aperturebase = tmp << 25;
	aper_base = (aperturebase & PCI_BASE_ADDRESS_MEM_MASK);

	/* address of the mappings table */
	addr = (u64) gatt_table;
	addr >>= 12;
	tmp = (u32) addr<<4;
	tmp &= ~0xf;
	pci_write_config_dword (hammer, AMD64_GARTTABLEBASE, tmp);

	/* Enable GART translation for this hammer. */
	pci_read_config_dword(hammer, AMD64_GARTAPERTURECTL, &tmp);
	tmp |= GARTEN;
	tmp &= ~(DISGARTCPU | DISGARTIO);
	pci_write_config_dword(hammer, AMD64_GARTAPERTURECTL, tmp);

	/* keep CPU's coherent. */
	flush_amd64_tlb (hammer);

	return aper_base;
}


static struct aper_size_info_32 amd_8151_sizes[7] =
{
	{2048, 524288, 9, 0x00000000 },	/* 0 0 0 0 0 0 */
	{1024, 262144, 8, 0x00000400 },	/* 1 0 0 0 0 0 */
	{512,  131072, 7, 0x00000600 },	/* 1 1 0 0 0 0 */
	{256,  65536,  6, 0x00000700 },	/* 1 1 1 0 0 0 */
	{128,  32768,  5, 0x00000720 },	/* 1 1 1 1 0 0 */
	{64,   16384,  4, 0x00000730 },	/* 1 1 1 1 1 0 */
	{32,   8192,   3, 0x00000738 } 	/* 1 1 1 1 1 1 */
};

static int amd_8151_configure(void)
{
	unsigned long gatt_bus = virt_to_gart(agp_bridge->gatt_table_real);
	int gart_iterator;

	/* Configure AGP regs in each x86-64 host bridge. */
	for_each_nb() {
		agp_bridge->gart_bus_addr =
				amd64_configure(hammers[gart_iterator],gatt_bus);
	}
	return 0;
}


static void amd64_cleanup(void)
{
	u32 tmp;
	int gart_iterator;
	for_each_nb() {
		/* disable gart translation */
		pci_read_config_dword (hammers[gart_iterator], AMD64_GARTAPERTURECTL, &tmp);
		tmp &= ~AMD64_GARTEN;
		pci_write_config_dword (hammers[gart_iterator], AMD64_GARTAPERTURECTL, tmp);
	}
}


static struct agp_bridge_driver amd_8151_driver = {
	.owner			= THIS_MODULE,
	.aperture_sizes		= amd_8151_sizes,
	.size_type		= U32_APER_SIZE,
	.num_aperture_sizes	= 7,
	.configure		= amd_8151_configure,
	.fetch_size		= amd64_fetch_size,
	.cleanup		= amd64_cleanup,
	.tlb_flush		= amd64_tlbflush,
	.mask_memory		= agp_generic_mask_memory,
	.masks			= NULL,
	.agp_enable		= agp_generic_enable,
	.cache_flush		= global_cache_flush,
	.create_gatt_table	= agp_generic_create_gatt_table,
	.free_gatt_table	= agp_generic_free_gatt_table,
	.insert_memory		= amd64_insert_memory,
	.remove_memory		= agp_generic_remove_memory,
	.alloc_by_type		= agp_generic_alloc_by_type,
	.free_by_type		= agp_generic_free_by_type,
	.agp_alloc_page		= agp_generic_alloc_page,
	.agp_destroy_page	= agp_generic_destroy_page,
};

/* Some basic sanity checks for the aperture. */
static int __devinit aperture_valid(u64 aper, u32 size)
{
	u32 pfn, c;
	if (aper == 0) {
		printk(KERN_ERR PFX "No aperture\n");
		return 0;
	}
	if (size < 32*1024*1024) {
		printk(KERN_ERR PFX "Aperture too small (%d MB)\n", size>>20);
		return 0;
	}
	if (aper + size > 0xffffffff) {
		printk(KERN_ERR PFX "Aperture out of bounds\n");
		return 0;
	}
	pfn = aper >> PAGE_SHIFT;
	for (c = 0; c < size/PAGE_SIZE; c++) {
		if (!pfn_valid(pfn + c))
			break;
		if (!PageReserved(pfn_to_page(pfn + c))) {
			printk(KERN_ERR PFX "Aperture pointing to RAM\n");
			return 0;
		}
	}

	/* Request the Aperture. This catches cases when someone else
	   already put a mapping in there - happens with some very broken BIOS

	   Maybe better to use pci_assign_resource/pci_enable_device instead
	   trusting the bridges? */
	if (!aperture_resource &&
	    !(aperture_resource = request_mem_region(aper, size, "aperture"))) {
		printk(KERN_ERR PFX "Aperture conflicts with PCI mapping.\n");
		return 0;
	}
	return 1;
}

/*
 * W*s centric BIOS sometimes only set up the aperture in the AGP
 * bridge, not the northbridge. On AMD64 this is handled early
 * in aperture.c, but when GART_IOMMU is not enabled or we run
 * on a 32bit kernel this needs to be redone.
 * Unfortunately it is impossible to fix the aperture here because it's too late
 * to allocate that much memory. But at least error out cleanly instead of
 * crashing.
 */
static __devinit int fix_northbridge(struct pci_dev *nb, struct pci_dev *agp,
								 u16 cap)
{
	u32 aper_low, aper_hi;
	u64 aper, nb_aper;
	int order = 0;
	u32 nb_order, nb_base;
	u16 apsize;

	pci_read_config_dword(nb, 0x90, &nb_order);
	nb_order = (nb_order >> 1) & 7;
	pci_read_config_dword(nb, 0x94, &nb_base);
	nb_aper = nb_base << 25;
	if (aperture_valid(nb_aper, (32*1024*1024)<<nb_order)) {
		return 0;
	}

	/* Northbridge seems to contain crap. Try the AGP bridge. */

	pci_read_config_word(agp, cap+0x14, &apsize);
	if (apsize == 0xffff)
		return -1;

	apsize &= 0xfff;
	/* Some BIOS use weird encodings not in the AGPv3 table. */
	if (apsize & 0xff)
		apsize |= 0xf00;
	order = 7 - hweight16(apsize);

	pci_read_config_dword(agp, 0x10, &aper_low);
	pci_read_config_dword(agp, 0x14, &aper_hi);
	aper = (aper_low & ~((1<<22)-1)) | ((u64)aper_hi << 32);
	printk(KERN_INFO PFX "Aperture from AGP @ %Lx size %u MB\n", aper, 32 << order);
	if (order < 0 || !aperture_valid(aper, (32*1024*1024)<<order))
		return -1;

	pci_write_config_dword(nb, 0x90, order << 1);
	pci_write_config_dword(nb, 0x94, aper >> 25);

	return 0;
}

static __devinit int cache_nbs (struct pci_dev *pdev, u32 cap_ptr)
{
	struct pci_dev *loop_dev = NULL;
	int i = 0;

	/* cache pci_devs of northbridges. */
	while ((loop_dev = pci_get_device(PCI_VENDOR_ID_AMD, 0x1103, loop_dev))
			!= NULL) {
		if (i == MAX_HAMMER_GARTS) {
			printk(KERN_ERR PFX "Too many northbridges for AGP\n");
			return -1;
		}
		if (fix_northbridge(loop_dev, pdev, cap_ptr) < 0) {
			printk(KERN_ERR PFX "No usable aperture found.\n");
#ifdef __x86_64__
			/* should port this to i386 */
			printk(KERN_ERR PFX "Consider rebooting with iommu=memaper=2 to get a good aperture.\n");
#endif
			return -1;
		}
		hammers[i++] = loop_dev;
	}
		nr_garts = i;
	return i == 0 ? -1 : 0;
}

/* Handle AMD 8151 quirks */
static void __devinit amd8151_init(struct pci_dev *pdev, struct agp_bridge_data *bridge)
{
	char *revstring;
	u8 rev_id;

	pci_read_config_byte(pdev, PCI_REVISION_ID, &rev_id);
	switch (rev_id) {
	case 0x01: revstring="A0"; break;
	case 0x02: revstring="A1"; break;
	case 0x11: revstring="B0"; break;
	case 0x12: revstring="B1"; break;
	case 0x13: revstring="B2"; break;
	case 0x14: revstring="B3"; break;
	default:   revstring="??"; break;
	}

	printk (KERN_INFO PFX "Detected AMD 8151 AGP Bridge rev %s\n", revstring);

	/*
	 * Work around errata.
	 * Chips before B2 stepping incorrectly reporting v3.5
	 */
	if (rev_id < 0x13) {
		printk (KERN_INFO PFX "Correcting AGP revision (reports 3.5, is really 3.0)\n");
		bridge->major_version = 3;
		bridge->minor_version = 0;
	}
}


static const struct aper_size_info_32 uli_sizes[7] =
{
	{256, 65536, 6, 10},
	{128, 32768, 5, 9},
	{64, 16384, 4, 8},
	{32, 8192, 3, 7},
	{16, 4096, 2, 6},
	{8, 2048, 1, 4},
	{4, 1024, 0, 3}
};
static int __devinit uli_agp_init(struct pci_dev *pdev)
{
	u32 httfea,baseaddr,enuscr;
	struct pci_dev *dev1;
	int i;
	unsigned size = amd64_fetch_size();
	printk(KERN_INFO "Setting up ULi AGP.\n");
	dev1 = pci_find_slot ((unsigned int)pdev->bus->number,PCI_DEVFN(0,0));
	if (dev1 == NULL) {
		printk(KERN_INFO PFX "Detected a ULi chipset, "
			"but could not fine the secondary device.\n");
		return -ENODEV;
	}

	for (i = 0; i < ARRAY_SIZE(uli_sizes); i++)
		if (uli_sizes[i].size == size)
			break;

	if (i == ARRAY_SIZE(uli_sizes)) {
		printk(KERN_INFO PFX "No ULi size found for %d\n", size);
		return -ENODEV;
	}

	/* shadow x86-64 registers into ULi registers */
	pci_read_config_dword (hammers[0], AMD64_GARTAPERTUREBASE, &httfea);

	/* if x86-64 aperture base is beyond 4G, exit here */
	if ((httfea & 0x7fff) >> (32 - 25))
		return -ENODEV;

	httfea = (httfea& 0x7fff) << 25;

	pci_read_config_dword(pdev, ULI_X86_64_BASE_ADDR, &baseaddr);
	baseaddr&= ~PCI_BASE_ADDRESS_MEM_MASK;
	baseaddr|= httfea;
	pci_write_config_dword(pdev, ULI_X86_64_BASE_ADDR, baseaddr);

	enuscr= httfea+ (size * 1024 * 1024) - 1;
	pci_write_config_dword(dev1, ULI_X86_64_HTT_FEA_REG, httfea);
	pci_write_config_dword(dev1, ULI_X86_64_ENU_SCR_REG, enuscr);
	return 0;
}


static const struct aper_size_info_32 nforce3_sizes[5] =
{
	{512,  131072, 7, 0x00000000 },
	{256,  65536,  6, 0x00000008 },
	{128,  32768,  5, 0x0000000C },
	{64,   16384,  4, 0x0000000E },
	{32,   8192,   3, 0x0000000F }
};

/* Handle shadow device of the Nvidia NForce3 */
/* CHECK-ME original 2.4 version set up some IORRs. Check if that is needed. */
static int __devinit nforce3_agp_init(struct pci_dev *pdev)
{
	u32 tmp, apbase, apbar, aplimit;
	struct pci_dev *dev1;
	int i;
	unsigned size = amd64_fetch_size();

	printk(KERN_INFO PFX "Setting up Nforce3 AGP.\n");

	dev1 = pci_find_slot((unsigned int)pdev->bus->number, PCI_DEVFN(11, 0));
	if (dev1 == NULL) {
		printk(KERN_INFO PFX "agpgart: Detected an NVIDIA "
			"nForce3 chipset, but could not find "
			"the secondary device.\n");
		return -ENODEV;
	}

	for (i = 0; i < ARRAY_SIZE(nforce3_sizes); i++)
		if (nforce3_sizes[i].size == size)
			break;

	if (i == ARRAY_SIZE(nforce3_sizes)) {
		printk(KERN_INFO PFX "No NForce3 size found for %d\n", size);
		return -ENODEV;
	}

	pci_read_config_dword(dev1, NVIDIA_X86_64_1_APSIZE, &tmp);
	tmp &= ~(0xf);
	tmp |= nforce3_sizes[i].size_value;
	pci_write_config_dword(dev1, NVIDIA_X86_64_1_APSIZE, tmp);

	/* shadow x86-64 registers into NVIDIA registers */
	pci_read_config_dword (hammers[0], AMD64_GARTAPERTUREBASE, &apbase);

	/* if x86-64 aperture base is beyond 4G, exit here */
	if ( (apbase & 0x7fff) >> (32 - 25) ) {
		printk(KERN_INFO PFX "aperture base > 4G\n");
		return -ENODEV;
	}

	apbase = (apbase & 0x7fff) << 25;

	pci_read_config_dword(pdev, NVIDIA_X86_64_0_APBASE, &apbar);
	apbar &= ~PCI_BASE_ADDRESS_MEM_MASK;
	apbar |= apbase;
	pci_write_config_dword(pdev, NVIDIA_X86_64_0_APBASE, apbar);

	aplimit = apbase + (size * 1024 * 1024) - 1;
	pci_write_config_dword(dev1, NVIDIA_X86_64_1_APBASE1, apbase);
	pci_write_config_dword(dev1, NVIDIA_X86_64_1_APLIMIT1, aplimit);
	pci_write_config_dword(dev1, NVIDIA_X86_64_1_APBASE2, apbase);
	pci_write_config_dword(dev1, NVIDIA_X86_64_1_APLIMIT2, aplimit);

	return 0;
}

static int __devinit agp_amd64_probe(struct pci_dev *pdev,
				     const struct pci_device_id *ent)
{
	struct agp_bridge_data *bridge;
	u8 cap_ptr;

	cap_ptr = pci_find_capability(pdev, PCI_CAP_ID_AGP);
	if (!cap_ptr)
		return -ENODEV;

	/* Could check for AGPv3 here */

	bridge = agp_alloc_bridge();
	if (!bridge)
		return -ENOMEM;

	if (pdev->vendor == PCI_VENDOR_ID_AMD &&
	    pdev->device == PCI_DEVICE_ID_AMD_8151_0) {
		amd8151_init(pdev, bridge);
	} else {
		printk(KERN_INFO PFX "Detected AGP bridge %x\n", pdev->devfn);
	}

	bridge->driver = &amd_8151_driver;
	bridge->dev = pdev;
	bridge->capndx = cap_ptr;

	/* Fill in the mode register */
	pci_read_config_dword(pdev, bridge->capndx+PCI_AGP_STATUS, &bridge->mode);

	if (cache_nbs(pdev, cap_ptr) == -1) {
		agp_put_bridge(bridge);
		return -ENODEV;
	}

	if (pdev->vendor == PCI_VENDOR_ID_NVIDIA) {
		int ret = nforce3_agp_init(pdev);
		if (ret) {
			agp_put_bridge(bridge);
			return ret;
		}
	}

	if (pdev->vendor == PCI_VENDOR_ID_AL) {
		int ret = uli_agp_init(pdev);
		if (ret) {
			agp_put_bridge(bridge);
			return ret;
		}
	}

	pci_set_drvdata(pdev, bridge);
	return agp_add_bridge(bridge);
}

static void __devexit agp_amd64_remove(struct pci_dev *pdev)
{
	struct agp_bridge_data *bridge = pci_get_drvdata(pdev);

	release_mem_region(virt_to_gart(bridge->gatt_table_real),
			   amd64_aperture_sizes[bridge->aperture_size_idx].size);
	agp_remove_bridge(bridge);
	agp_put_bridge(bridge);
}

#ifdef CONFIG_PM

static int agp_amd64_suspend(struct pci_dev *pdev, pm_message_t state)
{
	pci_save_state(pdev);
	pci_set_power_state(pdev, pci_choose_state(pdev, state));

	return 0;
}

static int agp_amd64_resume(struct pci_dev *pdev)
{
	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);

	return amd_8151_configure();
}

#endif /* CONFIG_PM */

static struct pci_device_id agp_amd64_pci_table[] = {
	{
	.class		= (PCI_CLASS_BRIDGE_HOST << 8),
	.class_mask	= ~0,
	.vendor		= PCI_VENDOR_ID_AMD,
	.device		= PCI_DEVICE_ID_AMD_8151_0,
	.subvendor	= PCI_ANY_ID,
	.subdevice	= PCI_ANY_ID,
	},
	/* ULi M1689 */
	{
	.class		= (PCI_CLASS_BRIDGE_HOST << 8),
	.class_mask	= ~0,
	.vendor		= PCI_VENDOR_ID_AL,
	.device		= PCI_DEVICE_ID_AL_M1689,
	.subvendor	= PCI_ANY_ID,
	.subdevice	= PCI_ANY_ID,
	},
	/* VIA K8T800Pro */
	{
	.class		= (PCI_CLASS_BRIDGE_HOST << 8),
	.class_mask	= ~0,
	.vendor		= PCI_VENDOR_ID_VIA,
	.device		= PCI_DEVICE_ID_VIA_K8T800PRO_0,
	.subvendor	= PCI_ANY_ID,
	.subdevice	= PCI_ANY_ID,
	},
	/* VIA K8T800 */
	{
	.class		= (PCI_CLASS_BRIDGE_HOST << 8),
	.class_mask	= ~0,
	.vendor		= PCI_VENDOR_ID_VIA,
	.device		= PCI_DEVICE_ID_VIA_8385_0,
	.subvendor	= PCI_ANY_ID,
	.subdevice	= PCI_ANY_ID,
	},
	/* VIA K8M800 / K8N800 */
	{
	.class		= (PCI_CLASS_BRIDGE_HOST << 8),
	.class_mask	= ~0,
	.vendor		= PCI_VENDOR_ID_VIA,
	.device		= PCI_DEVICE_ID_VIA_8380_0,
	.subvendor	= PCI_ANY_ID,
	.subdevice	= PCI_ANY_ID,
	},
	/* VIA K8T890 */
	{
	.class		= (PCI_CLASS_BRIDGE_HOST << 8),
	.class_mask	= ~0,
	.vendor		= PCI_VENDOR_ID_VIA,
	.device		= PCI_DEVICE_ID_VIA_3238_0,
	.subvendor	= PCI_ANY_ID,
	.subdevice	= PCI_ANY_ID,
	},
	/* VIA K8T800/K8M800/K8N800 */
	{
	.class		= (PCI_CLASS_BRIDGE_HOST << 8),
	.class_mask	= ~0,
	.vendor		= PCI_VENDOR_ID_VIA,
	.device		= PCI_DEVICE_ID_VIA_838X_1,
	.subvendor	= PCI_ANY_ID,
	.subdevice	= PCI_ANY_ID,
	},
	/* NForce3 */
	{
	.class		= (PCI_CLASS_BRIDGE_HOST << 8),
	.class_mask	= ~0,
	.vendor		= PCI_VENDOR_ID_NVIDIA,
	.device		= PCI_DEVICE_ID_NVIDIA_NFORCE3,
	.subvendor	= PCI_ANY_ID,
	.subdevice	= PCI_ANY_ID,
	},
	{
	.class		= (PCI_CLASS_BRIDGE_HOST << 8),
	.class_mask	= ~0,
	.vendor		= PCI_VENDOR_ID_NVIDIA,
	.device		= PCI_DEVICE_ID_NVIDIA_NFORCE3S,
	.subvendor	= PCI_ANY_ID,
	.subdevice	= PCI_ANY_ID,
	},
	/* SIS 755 */
	{
	.class		= (PCI_CLASS_BRIDGE_HOST << 8),
	.class_mask	= ~0,
	.vendor		= PCI_VENDOR_ID_SI,
	.device		= PCI_DEVICE_ID_SI_755,
	.subvendor	= PCI_ANY_ID,
	.subdevice	= PCI_ANY_ID,
	},
	/* SIS 760 */
	{
	.class		= (PCI_CLASS_BRIDGE_HOST << 8),
	.class_mask	= ~0,
	.vendor		= PCI_VENDOR_ID_SI,
	.device		= PCI_DEVICE_ID_SI_760,
	.subvendor	= PCI_ANY_ID,
	.subdevice	= PCI_ANY_ID,
	},
	/* ALI/ULI M1695 */
	{
	.class		= (PCI_CLASS_BRIDGE_HOST << 8),
	.class_mask	= ~0,
	.vendor		= PCI_VENDOR_ID_AL,
	.device		= 0x1689,
	.subvendor	= PCI_ANY_ID,
	.subdevice	= PCI_ANY_ID,
	},

	{ }
};

MODULE_DEVICE_TABLE(pci, agp_amd64_pci_table);

static struct pci_driver agp_amd64_pci_driver = {
	.name		= "agpgart-amd64",
	.id_table	= agp_amd64_pci_table,
	.probe		= agp_amd64_probe,
	.remove		= agp_amd64_remove,
#ifdef CONFIG_PM
	.suspend	= agp_amd64_suspend,
	.resume		= agp_amd64_resume,
#endif
};


/* Not static due to IOMMU code calling it early. */
int __init agp_amd64_init(void)
{
	int err = 0;
	static struct pci_device_id amd64nb[] = {
		{ PCI_DEVICE(PCI_VENDOR_ID_AMD, 0x1103) },
		{ },
	};

	if (agp_off)
		return -EINVAL;
	if (pci_register_driver(&agp_amd64_pci_driver) > 0) {
		struct pci_dev *dev;
		if (!agp_try_unsupported && !agp_try_unsupported_boot) {
			printk(KERN_INFO PFX "No supported AGP bridge found.\n");
#ifdef MODULE
			printk(KERN_INFO PFX "You can try agp_try_unsupported=1\n");
#else
			printk(KERN_INFO PFX "You can boot with agp=try_unsupported\n");
#endif
			return -ENODEV;
		}

		/* First check that we have at least one AMD64 NB */
		if (!pci_dev_present(amd64nb))
			return -ENODEV;

		/* Look for any AGP bridge */
		dev = NULL;
		err = -ENODEV;
		for_each_pci_dev(dev) {
			if (!pci_find_capability(dev, PCI_CAP_ID_AGP))
				continue;
			/* Only one bridge supported right now */
			if (agp_amd64_probe(dev, NULL) == 0) {
				err = 0;
				break;
			}
		}
	}
	return err;
}

static void __exit agp_amd64_cleanup(void)
{
	if (aperture_resource)
		release_resource(aperture_resource);
	pci_unregister_driver(&agp_amd64_pci_driver);
}

/* On AMD64 the PCI driver needs to initialize this driver early
   for the IOMMU, so it has to be called via a backdoor. */
#ifndef CONFIG_GART_IOMMU
module_init(agp_amd64_init);
module_exit(agp_amd64_cleanup);
#endif

MODULE_AUTHOR("Dave Jones <davej@codemonkey.org.uk>, Andi Kleen");
module_param(agp_try_unsupported, bool, 0);
MODULE_LICENSE("GPL");

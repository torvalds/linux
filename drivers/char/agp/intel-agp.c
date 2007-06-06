/*
 * Intel AGPGART routines.
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/pagemap.h>
#include <linux/agp_backend.h>
#include "agp.h"

#define PCI_DEVICE_ID_INTEL_82946GZ_HB      0x2970
#define PCI_DEVICE_ID_INTEL_82946GZ_IG      0x2972
#define PCI_DEVICE_ID_INTEL_82965G_1_HB     0x2980
#define PCI_DEVICE_ID_INTEL_82965G_1_IG     0x2982
#define PCI_DEVICE_ID_INTEL_82965Q_HB       0x2990
#define PCI_DEVICE_ID_INTEL_82965Q_IG       0x2992
#define PCI_DEVICE_ID_INTEL_82965G_HB       0x29A0
#define PCI_DEVICE_ID_INTEL_82965G_IG       0x29A2
#define PCI_DEVICE_ID_INTEL_82965GM_HB      0x2A00
#define PCI_DEVICE_ID_INTEL_82965GM_IG      0x2A02
#define PCI_DEVICE_ID_INTEL_82965GME_IG     0x2A12
#define PCI_DEVICE_ID_INTEL_82945GME_IG     0x27AE
#define PCI_DEVICE_ID_INTEL_G33_HB          0x29C0
#define PCI_DEVICE_ID_INTEL_G33_IG          0x29C2
#define PCI_DEVICE_ID_INTEL_Q35_HB          0x29B0
#define PCI_DEVICE_ID_INTEL_Q35_IG          0x29B2
#define PCI_DEVICE_ID_INTEL_Q33_HB          0x29D0
#define PCI_DEVICE_ID_INTEL_Q33_IG          0x29D2

#define IS_I965 (agp_bridge->dev->device == PCI_DEVICE_ID_INTEL_82946GZ_HB || \
                 agp_bridge->dev->device == PCI_DEVICE_ID_INTEL_82965G_1_HB || \
                 agp_bridge->dev->device == PCI_DEVICE_ID_INTEL_82965Q_HB || \
                 agp_bridge->dev->device == PCI_DEVICE_ID_INTEL_82965G_HB || \
                 agp_bridge->dev->device == PCI_DEVICE_ID_INTEL_82965GM_HB)

#define IS_G33 (agp_bridge->dev->device == PCI_DEVICE_ID_INTEL_G33_HB || \
		agp_bridge->dev->device == PCI_DEVICE_ID_INTEL_Q35_HB || \
		agp_bridge->dev->device == PCI_DEVICE_ID_INTEL_Q33_HB)

extern int agp_memory_reserved;


/* Intel 815 register */
#define INTEL_815_APCONT	0x51
#define INTEL_815_ATTBASE_MASK	~0x1FFFFFFF

/* Intel i820 registers */
#define INTEL_I820_RDCR		0x51
#define INTEL_I820_ERRSTS	0xc8

/* Intel i840 registers */
#define INTEL_I840_MCHCFG	0x50
#define INTEL_I840_ERRSTS	0xc8

/* Intel i850 registers */
#define INTEL_I850_MCHCFG	0x50
#define INTEL_I850_ERRSTS	0xc8

/* intel 915G registers */
#define I915_GMADDR	0x18
#define I915_MMADDR	0x10
#define I915_PTEADDR	0x1C
#define I915_GMCH_GMS_STOLEN_48M	(0x6 << 4)
#define I915_GMCH_GMS_STOLEN_64M	(0x7 << 4)
#define G33_GMCH_GMS_STOLEN_128M       (0x8 << 4)
#define G33_GMCH_GMS_STOLEN_256M       (0x9 << 4)

/* Intel 965G registers */
#define I965_MSAC 0x62

/* Intel 7505 registers */
#define INTEL_I7505_APSIZE	0x74
#define INTEL_I7505_NCAPID	0x60
#define INTEL_I7505_NISTAT	0x6c
#define INTEL_I7505_ATTBASE	0x78
#define INTEL_I7505_ERRSTS	0x42
#define INTEL_I7505_AGPCTRL	0x70
#define INTEL_I7505_MCHCFG	0x50

static const struct aper_size_info_fixed intel_i810_sizes[] =
{
	{64, 16384, 4},
	/* The 32M mode still requires a 64k gatt */
	{32, 8192, 4}
};

#define AGP_DCACHE_MEMORY	1
#define AGP_PHYS_MEMORY		2
#define INTEL_AGP_CACHED_MEMORY 3

static struct gatt_mask intel_i810_masks[] =
{
	{.mask = I810_PTE_VALID, .type = 0},
	{.mask = (I810_PTE_VALID | I810_PTE_LOCAL), .type = AGP_DCACHE_MEMORY},
	{.mask = I810_PTE_VALID, .type = 0},
	{.mask = I810_PTE_VALID | I830_PTE_SYSTEM_CACHED,
	 .type = INTEL_AGP_CACHED_MEMORY}
};

static struct _intel_private {
	struct pci_dev *pcidev;	/* device one */
	u8 __iomem *registers;
	u32 __iomem *gtt;		/* I915G */
	int num_dcache_entries;
	/* gtt_entries is the number of gtt entries that are already mapped
	 * to stolen memory.  Stolen memory is larger than the memory mapped
	 * through gtt_entries, as it includes some reserved space for the BIOS
	 * popup and for the GTT.
	 */
	int gtt_entries;			/* i830+ */
} intel_private;

static int intel_i810_fetch_size(void)
{
	u32 smram_miscc;
	struct aper_size_info_fixed *values;

	pci_read_config_dword(agp_bridge->dev, I810_SMRAM_MISCC, &smram_miscc);
	values = A_SIZE_FIX(agp_bridge->driver->aperture_sizes);

	if ((smram_miscc & I810_GMS) == I810_GMS_DISABLE) {
		printk(KERN_WARNING PFX "i810 is disabled\n");
		return 0;
	}
	if ((smram_miscc & I810_GFX_MEM_WIN_SIZE) == I810_GFX_MEM_WIN_32M) {
		agp_bridge->previous_size =
			agp_bridge->current_size = (void *) (values + 1);
		agp_bridge->aperture_size_idx = 1;
		return values[1].size;
	} else {
		agp_bridge->previous_size =
			agp_bridge->current_size = (void *) (values);
		agp_bridge->aperture_size_idx = 0;
		return values[0].size;
	}

	return 0;
}

static int intel_i810_configure(void)
{
	struct aper_size_info_fixed *current_size;
	u32 temp;
	int i;

	current_size = A_SIZE_FIX(agp_bridge->current_size);

	if (!intel_private.registers) {
		pci_read_config_dword(intel_private.pcidev, I810_MMADDR, &temp);
		temp &= 0xfff80000;

		intel_private.registers = ioremap(temp, 128 * 4096);
		if (!intel_private.registers) {
			printk(KERN_ERR PFX "Unable to remap memory.\n");
			return -ENOMEM;
		}
	}

	if ((readl(intel_private.registers+I810_DRAM_CTL)
		& I810_DRAM_ROW_0) == I810_DRAM_ROW_0_SDRAM) {
		/* This will need to be dynamically assigned */
		printk(KERN_INFO PFX "detected 4MB dedicated video ram.\n");
		intel_private.num_dcache_entries = 1024;
	}
	pci_read_config_dword(intel_private.pcidev, I810_GMADDR, &temp);
	agp_bridge->gart_bus_addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);
	writel(agp_bridge->gatt_bus_addr | I810_PGETBL_ENABLED, intel_private.registers+I810_PGETBL_CTL);
	readl(intel_private.registers+I810_PGETBL_CTL);	/* PCI Posting. */

	if (agp_bridge->driver->needs_scratch_page) {
		for (i = 0; i < current_size->num_entries; i++) {
			writel(agp_bridge->scratch_page, intel_private.registers+I810_PTE_BASE+(i*4));
			readl(intel_private.registers+I810_PTE_BASE+(i*4));	/* PCI posting. */
		}
	}
	global_cache_flush();
	return 0;
}

static void intel_i810_cleanup(void)
{
	writel(0, intel_private.registers+I810_PGETBL_CTL);
	readl(intel_private.registers);	/* PCI Posting. */
	iounmap(intel_private.registers);
}

static void intel_i810_tlbflush(struct agp_memory *mem)
{
	return;
}

static void intel_i810_agp_enable(struct agp_bridge_data *bridge, u32 mode)
{
	return;
}

/* Exists to support ARGB cursors */
static void *i8xx_alloc_pages(void)
{
	struct page * page;

	page = alloc_pages(GFP_KERNEL | GFP_DMA32, 2);
	if (page == NULL)
		return NULL;

	if (change_page_attr(page, 4, PAGE_KERNEL_NOCACHE) < 0) {
		change_page_attr(page, 4, PAGE_KERNEL);
		global_flush_tlb();
		__free_pages(page, 2);
		return NULL;
	}
	global_flush_tlb();
	get_page(page);
	SetPageLocked(page);
	atomic_inc(&agp_bridge->current_memory_agp);
	return page_address(page);
}

static void i8xx_destroy_pages(void *addr)
{
	struct page *page;

	if (addr == NULL)
		return;

	page = virt_to_page(addr);
	change_page_attr(page, 4, PAGE_KERNEL);
	global_flush_tlb();
	put_page(page);
	unlock_page(page);
	__free_pages(page, 2);
	atomic_dec(&agp_bridge->current_memory_agp);
}

static int intel_i830_type_to_mask_type(struct agp_bridge_data *bridge,
					int type)
{
	if (type < AGP_USER_TYPES)
		return type;
	else if (type == AGP_USER_CACHED_MEMORY)
		return INTEL_AGP_CACHED_MEMORY;
	else
		return 0;
}

static int intel_i810_insert_entries(struct agp_memory *mem, off_t pg_start,
				int type)
{
	int i, j, num_entries;
	void *temp;
	int ret = -EINVAL;
	int mask_type;

	if (mem->page_count == 0)
		goto out;

	temp = agp_bridge->current_size;
	num_entries = A_SIZE_FIX(temp)->num_entries;

	if ((pg_start + mem->page_count) > num_entries)
		goto out_err;


	for (j = pg_start; j < (pg_start + mem->page_count); j++) {
		if (!PGE_EMPTY(agp_bridge, readl(agp_bridge->gatt_table+j))) {
			ret = -EBUSY;
			goto out_err;
		}
	}

	if (type != mem->type)
		goto out_err;

	mask_type = agp_bridge->driver->agp_type_to_mask_type(agp_bridge, type);

	switch (mask_type) {
	case AGP_DCACHE_MEMORY:
		if (!mem->is_flushed)
			global_cache_flush();
		for (i = pg_start; i < (pg_start + mem->page_count); i++) {
			writel((i*4096)|I810_PTE_LOCAL|I810_PTE_VALID,
			       intel_private.registers+I810_PTE_BASE+(i*4));
		}
		readl(intel_private.registers+I810_PTE_BASE+((i-1)*4));
		break;
	case AGP_PHYS_MEMORY:
	case AGP_NORMAL_MEMORY:
		if (!mem->is_flushed)
			global_cache_flush();
		for (i = 0, j = pg_start; i < mem->page_count; i++, j++) {
			writel(agp_bridge->driver->mask_memory(agp_bridge,
							       mem->memory[i],
							       mask_type),
			       intel_private.registers+I810_PTE_BASE+(j*4));
		}
		readl(intel_private.registers+I810_PTE_BASE+((j-1)*4));
		break;
	default:
		goto out_err;
	}

	agp_bridge->driver->tlb_flush(mem);
out:
	ret = 0;
out_err:
	mem->is_flushed = 1;
	return ret;
}

static int intel_i810_remove_entries(struct agp_memory *mem, off_t pg_start,
				int type)
{
	int i;

	if (mem->page_count == 0)
		return 0;

	for (i = pg_start; i < (mem->page_count + pg_start); i++) {
		writel(agp_bridge->scratch_page, intel_private.registers+I810_PTE_BASE+(i*4));
	}
	readl(intel_private.registers+I810_PTE_BASE+((i-1)*4));

	agp_bridge->driver->tlb_flush(mem);
	return 0;
}

/*
 * The i810/i830 requires a physical address to program its mouse
 * pointer into hardware.
 * However the Xserver still writes to it through the agp aperture.
 */
static struct agp_memory *alloc_agpphysmem_i8xx(size_t pg_count, int type)
{
	struct agp_memory *new;
	void *addr;

	switch (pg_count) {
	case 1: addr = agp_bridge->driver->agp_alloc_page(agp_bridge);
		global_flush_tlb();
		break;
	case 4:
		/* kludge to get 4 physical pages for ARGB cursor */
		addr = i8xx_alloc_pages();
		break;
	default:
		return NULL;
	}

	if (addr == NULL)
		return NULL;

	new = agp_create_memory(pg_count);
	if (new == NULL)
		return NULL;

	new->memory[0] = virt_to_gart(addr);
	if (pg_count == 4) {
		/* kludge to get 4 physical pages for ARGB cursor */
		new->memory[1] = new->memory[0] + PAGE_SIZE;
		new->memory[2] = new->memory[1] + PAGE_SIZE;
		new->memory[3] = new->memory[2] + PAGE_SIZE;
	}
	new->page_count = pg_count;
	new->num_scratch_pages = pg_count;
	new->type = AGP_PHYS_MEMORY;
	new->physical = new->memory[0];
	return new;
}

static struct agp_memory *intel_i810_alloc_by_type(size_t pg_count, int type)
{
	struct agp_memory *new;

	if (type == AGP_DCACHE_MEMORY) {
		if (pg_count != intel_private.num_dcache_entries)
			return NULL;

		new = agp_create_memory(1);
		if (new == NULL)
			return NULL;

		new->type = AGP_DCACHE_MEMORY;
		new->page_count = pg_count;
		new->num_scratch_pages = 0;
		agp_free_page_array(new);
		return new;
	}
	if (type == AGP_PHYS_MEMORY)
		return alloc_agpphysmem_i8xx(pg_count, type);
	return NULL;
}

static void intel_i810_free_by_type(struct agp_memory *curr)
{
	agp_free_key(curr->key);
	if (curr->type == AGP_PHYS_MEMORY) {
		if (curr->page_count == 4)
			i8xx_destroy_pages(gart_to_virt(curr->memory[0]));
		else {
			agp_bridge->driver->agp_destroy_page(
				 gart_to_virt(curr->memory[0]));
			global_flush_tlb();
		}
		agp_free_page_array(curr);
	}
	kfree(curr);
}

static unsigned long intel_i810_mask_memory(struct agp_bridge_data *bridge,
	unsigned long addr, int type)
{
	/* Type checking must be done elsewhere */
	return addr | bridge->driver->masks[type].mask;
}

static struct aper_size_info_fixed intel_i830_sizes[] =
{
	{128, 32768, 5},
	/* The 64M mode still requires a 128k gatt */
	{64, 16384, 5},
	{256, 65536, 6},
	{512, 131072, 7},
};

static void intel_i830_init_gtt_entries(void)
{
	u16 gmch_ctrl;
	int gtt_entries;
	u8 rdct;
	int local = 0;
	static const int ddt[4] = { 0, 16, 32, 64 };
	int size; /* reserved space (in kb) at the top of stolen memory */

	pci_read_config_word(agp_bridge->dev,I830_GMCH_CTRL,&gmch_ctrl);

	if (IS_I965) {
		u32 pgetbl_ctl;
		pgetbl_ctl = readl(intel_private.registers+I810_PGETBL_CTL);

		/* The 965 has a field telling us the size of the GTT,
		 * which may be larger than what is necessary to map the
		 * aperture.
		 */
		switch (pgetbl_ctl & I965_PGETBL_SIZE_MASK) {
		case I965_PGETBL_SIZE_128KB:
			size = 128;
			break;
		case I965_PGETBL_SIZE_256KB:
			size = 256;
			break;
		case I965_PGETBL_SIZE_512KB:
			size = 512;
			break;
		default:
			printk(KERN_INFO PFX "Unknown page table size, "
			       "assuming 512KB\n");
			size = 512;
		}
		size += 4; /* add in BIOS popup space */
	} else if (IS_G33) {
	/* G33's GTT size defined in gmch_ctrl */
		switch (gmch_ctrl & G33_PGETBL_SIZE_MASK) {
		case G33_PGETBL_SIZE_1M:
			size = 1024;
			break;
		case G33_PGETBL_SIZE_2M:
			size = 2048;
			break;
		default:
			printk(KERN_INFO PFX "Unknown page table size 0x%x, "
				"assuming 512KB\n",
				(gmch_ctrl & G33_PGETBL_SIZE_MASK));
			size = 512;
		}
		size += 4;
	} else {
		/* On previous hardware, the GTT size was just what was
		 * required to map the aperture.
		 */
		size = agp_bridge->driver->fetch_size() + 4;
	}

	if (agp_bridge->dev->device == PCI_DEVICE_ID_INTEL_82830_HB ||
	    agp_bridge->dev->device == PCI_DEVICE_ID_INTEL_82845G_HB) {
		switch (gmch_ctrl & I830_GMCH_GMS_MASK) {
		case I830_GMCH_GMS_STOLEN_512:
			gtt_entries = KB(512) - KB(size);
			break;
		case I830_GMCH_GMS_STOLEN_1024:
			gtt_entries = MB(1) - KB(size);
			break;
		case I830_GMCH_GMS_STOLEN_8192:
			gtt_entries = MB(8) - KB(size);
			break;
		case I830_GMCH_GMS_LOCAL:
			rdct = readb(intel_private.registers+I830_RDRAM_CHANNEL_TYPE);
			gtt_entries = (I830_RDRAM_ND(rdct) + 1) *
					MB(ddt[I830_RDRAM_DDT(rdct)]);
			local = 1;
			break;
		default:
			gtt_entries = 0;
			break;
		}
	} else {
		switch (gmch_ctrl & I830_GMCH_GMS_MASK) {
		case I855_GMCH_GMS_STOLEN_1M:
			gtt_entries = MB(1) - KB(size);
			break;
		case I855_GMCH_GMS_STOLEN_4M:
			gtt_entries = MB(4) - KB(size);
			break;
		case I855_GMCH_GMS_STOLEN_8M:
			gtt_entries = MB(8) - KB(size);
			break;
		case I855_GMCH_GMS_STOLEN_16M:
			gtt_entries = MB(16) - KB(size);
			break;
		case I855_GMCH_GMS_STOLEN_32M:
			gtt_entries = MB(32) - KB(size);
			break;
		case I915_GMCH_GMS_STOLEN_48M:
			/* Check it's really I915G */
			if (agp_bridge->dev->device == PCI_DEVICE_ID_INTEL_82915G_HB ||
			    agp_bridge->dev->device == PCI_DEVICE_ID_INTEL_82915GM_HB ||
			    agp_bridge->dev->device == PCI_DEVICE_ID_INTEL_82945G_HB ||
			    agp_bridge->dev->device == PCI_DEVICE_ID_INTEL_82945GM_HB ||
			    IS_I965 || IS_G33)
				gtt_entries = MB(48) - KB(size);
			else
				gtt_entries = 0;
			break;
		case I915_GMCH_GMS_STOLEN_64M:
			/* Check it's really I915G */
			if (agp_bridge->dev->device == PCI_DEVICE_ID_INTEL_82915G_HB ||
			    agp_bridge->dev->device == PCI_DEVICE_ID_INTEL_82915GM_HB ||
			    agp_bridge->dev->device == PCI_DEVICE_ID_INTEL_82945G_HB ||
			    agp_bridge->dev->device == PCI_DEVICE_ID_INTEL_82945GM_HB ||
			    IS_I965 || IS_G33)
				gtt_entries = MB(64) - KB(size);
			else
				gtt_entries = 0;
			break;
		case G33_GMCH_GMS_STOLEN_128M:
			if (IS_G33)
				gtt_entries = MB(128) - KB(size);
			else
				gtt_entries = 0;
			break;
		case G33_GMCH_GMS_STOLEN_256M:
			if (IS_G33)
				gtt_entries = MB(256) - KB(size);
			else
				gtt_entries = 0;
			break;
		default:
			gtt_entries = 0;
			break;
		}
	}
	if (gtt_entries > 0)
		printk(KERN_INFO PFX "Detected %dK %s memory.\n",
		       gtt_entries / KB(1), local ? "local" : "stolen");
	else
		printk(KERN_INFO PFX
		       "No pre-allocated video memory detected.\n");
	gtt_entries /= KB(4);

	intel_private.gtt_entries = gtt_entries;
}

/* The intel i830 automatically initializes the agp aperture during POST.
 * Use the memory already set aside for in the GTT.
 */
static int intel_i830_create_gatt_table(struct agp_bridge_data *bridge)
{
	int page_order;
	struct aper_size_info_fixed *size;
	int num_entries;
	u32 temp;

	size = agp_bridge->current_size;
	page_order = size->page_order;
	num_entries = size->num_entries;
	agp_bridge->gatt_table_real = NULL;

	pci_read_config_dword(intel_private.pcidev,I810_MMADDR,&temp);
	temp &= 0xfff80000;

	intel_private.registers = ioremap(temp,128 * 4096);
	if (!intel_private.registers)
		return -ENOMEM;

	temp = readl(intel_private.registers+I810_PGETBL_CTL) & 0xfffff000;
	global_cache_flush();	/* FIXME: ?? */

	/* we have to call this as early as possible after the MMIO base address is known */
	intel_i830_init_gtt_entries();

	agp_bridge->gatt_table = NULL;

	agp_bridge->gatt_bus_addr = temp;

	return 0;
}

/* Return the gatt table to a sane state. Use the top of stolen
 * memory for the GTT.
 */
static int intel_i830_free_gatt_table(struct agp_bridge_data *bridge)
{
	return 0;
}

static int intel_i830_fetch_size(void)
{
	u16 gmch_ctrl;
	struct aper_size_info_fixed *values;

	values = A_SIZE_FIX(agp_bridge->driver->aperture_sizes);

	if (agp_bridge->dev->device != PCI_DEVICE_ID_INTEL_82830_HB &&
	    agp_bridge->dev->device != PCI_DEVICE_ID_INTEL_82845G_HB) {
		/* 855GM/852GM/865G has 128MB aperture size */
		agp_bridge->previous_size = agp_bridge->current_size = (void *) values;
		agp_bridge->aperture_size_idx = 0;
		return values[0].size;
	}

	pci_read_config_word(agp_bridge->dev,I830_GMCH_CTRL,&gmch_ctrl);

	if ((gmch_ctrl & I830_GMCH_MEM_MASK) == I830_GMCH_MEM_128M) {
		agp_bridge->previous_size = agp_bridge->current_size = (void *) values;
		agp_bridge->aperture_size_idx = 0;
		return values[0].size;
	} else {
		agp_bridge->previous_size = agp_bridge->current_size = (void *) (values + 1);
		agp_bridge->aperture_size_idx = 1;
		return values[1].size;
	}

	return 0;
}

static int intel_i830_configure(void)
{
	struct aper_size_info_fixed *current_size;
	u32 temp;
	u16 gmch_ctrl;
	int i;

	current_size = A_SIZE_FIX(agp_bridge->current_size);

	pci_read_config_dword(intel_private.pcidev,I810_GMADDR,&temp);
	agp_bridge->gart_bus_addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);

	pci_read_config_word(agp_bridge->dev,I830_GMCH_CTRL,&gmch_ctrl);
	gmch_ctrl |= I830_GMCH_ENABLED;
	pci_write_config_word(agp_bridge->dev,I830_GMCH_CTRL,gmch_ctrl);

	writel(agp_bridge->gatt_bus_addr|I810_PGETBL_ENABLED, intel_private.registers+I810_PGETBL_CTL);
	readl(intel_private.registers+I810_PGETBL_CTL);	/* PCI Posting. */

	if (agp_bridge->driver->needs_scratch_page) {
		for (i = intel_private.gtt_entries; i < current_size->num_entries; i++) {
			writel(agp_bridge->scratch_page, intel_private.registers+I810_PTE_BASE+(i*4));
			readl(intel_private.registers+I810_PTE_BASE+(i*4));	/* PCI Posting. */
		}
	}

	global_cache_flush();
	return 0;
}

static void intel_i830_cleanup(void)
{
	iounmap(intel_private.registers);
}

static int intel_i830_insert_entries(struct agp_memory *mem,off_t pg_start, int type)
{
	int i,j,num_entries;
	void *temp;
	int ret = -EINVAL;
	int mask_type;

	if (mem->page_count == 0)
		goto out;

	temp = agp_bridge->current_size;
	num_entries = A_SIZE_FIX(temp)->num_entries;

	if (pg_start < intel_private.gtt_entries) {
		printk (KERN_DEBUG PFX "pg_start == 0x%.8lx,intel_private.gtt_entries == 0x%.8x\n",
				pg_start,intel_private.gtt_entries);

		printk (KERN_INFO PFX "Trying to insert into local/stolen memory\n");
		goto out_err;
	}

	if ((pg_start + mem->page_count) > num_entries)
		goto out_err;

	/* The i830 can't check the GTT for entries since its read only,
	 * depend on the caller to make the correct offset decisions.
	 */

	if (type != mem->type)
		goto out_err;

	mask_type = agp_bridge->driver->agp_type_to_mask_type(agp_bridge, type);

	if (mask_type != 0 && mask_type != AGP_PHYS_MEMORY &&
	    mask_type != INTEL_AGP_CACHED_MEMORY)
		goto out_err;

	if (!mem->is_flushed)
		global_cache_flush();

	for (i = 0, j = pg_start; i < mem->page_count; i++, j++) {
		writel(agp_bridge->driver->mask_memory(agp_bridge,
						       mem->memory[i], mask_type),
		       intel_private.registers+I810_PTE_BASE+(j*4));
	}
	readl(intel_private.registers+I810_PTE_BASE+((j-1)*4));
	agp_bridge->driver->tlb_flush(mem);

out:
	ret = 0;
out_err:
	mem->is_flushed = 1;
	return ret;
}

static int intel_i830_remove_entries(struct agp_memory *mem,off_t pg_start,
				int type)
{
	int i;

	if (mem->page_count == 0)
		return 0;

	if (pg_start < intel_private.gtt_entries) {
		printk (KERN_INFO PFX "Trying to disable local/stolen memory\n");
		return -EINVAL;
	}

	for (i = pg_start; i < (mem->page_count + pg_start); i++) {
		writel(agp_bridge->scratch_page, intel_private.registers+I810_PTE_BASE+(i*4));
	}
	readl(intel_private.registers+I810_PTE_BASE+((i-1)*4));

	agp_bridge->driver->tlb_flush(mem);
	return 0;
}

static struct agp_memory *intel_i830_alloc_by_type(size_t pg_count,int type)
{
	if (type == AGP_PHYS_MEMORY)
		return alloc_agpphysmem_i8xx(pg_count, type);
	/* always return NULL for other allocation types for now */
	return NULL;
}

static int intel_i915_configure(void)
{
	struct aper_size_info_fixed *current_size;
	u32 temp;
	u16 gmch_ctrl;
	int i;

	current_size = A_SIZE_FIX(agp_bridge->current_size);

	pci_read_config_dword(intel_private.pcidev, I915_GMADDR, &temp);

	agp_bridge->gart_bus_addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);

	pci_read_config_word(agp_bridge->dev,I830_GMCH_CTRL,&gmch_ctrl);
	gmch_ctrl |= I830_GMCH_ENABLED;
	pci_write_config_word(agp_bridge->dev,I830_GMCH_CTRL,gmch_ctrl);

	writel(agp_bridge->gatt_bus_addr|I810_PGETBL_ENABLED, intel_private.registers+I810_PGETBL_CTL);
	readl(intel_private.registers+I810_PGETBL_CTL);	/* PCI Posting. */

	if (agp_bridge->driver->needs_scratch_page) {
		for (i = intel_private.gtt_entries; i < current_size->num_entries; i++) {
			writel(agp_bridge->scratch_page, intel_private.gtt+i);
			readl(intel_private.gtt+i);	/* PCI Posting. */
		}
	}

	global_cache_flush();
	return 0;
}

static void intel_i915_cleanup(void)
{
	iounmap(intel_private.gtt);
	iounmap(intel_private.registers);
}

static int intel_i915_insert_entries(struct agp_memory *mem,off_t pg_start,
				int type)
{
	int i,j,num_entries;
	void *temp;
	int ret = -EINVAL;
	int mask_type;

	if (mem->page_count == 0)
		goto out;

	temp = agp_bridge->current_size;
	num_entries = A_SIZE_FIX(temp)->num_entries;

	if (pg_start < intel_private.gtt_entries) {
		printk (KERN_DEBUG PFX "pg_start == 0x%.8lx,intel_private.gtt_entries == 0x%.8x\n",
				pg_start,intel_private.gtt_entries);

		printk (KERN_INFO PFX "Trying to insert into local/stolen memory\n");
		goto out_err;
	}

	if ((pg_start + mem->page_count) > num_entries)
		goto out_err;

	/* The i915 can't check the GTT for entries since its read only,
	 * depend on the caller to make the correct offset decisions.
	 */

	if (type != mem->type)
		goto out_err;

	mask_type = agp_bridge->driver->agp_type_to_mask_type(agp_bridge, type);

	if (mask_type != 0 && mask_type != AGP_PHYS_MEMORY &&
	    mask_type != INTEL_AGP_CACHED_MEMORY)
		goto out_err;

	if (!mem->is_flushed)
		global_cache_flush();

	for (i = 0, j = pg_start; i < mem->page_count; i++, j++) {
		writel(agp_bridge->driver->mask_memory(agp_bridge,
			mem->memory[i], mask_type), intel_private.gtt+j);
	}

	readl(intel_private.gtt+j-1);
	agp_bridge->driver->tlb_flush(mem);

 out:
	ret = 0;
 out_err:
	mem->is_flushed = 1;
	return ret;
}

static int intel_i915_remove_entries(struct agp_memory *mem,off_t pg_start,
				int type)
{
	int i;

	if (mem->page_count == 0)
		return 0;

	if (pg_start < intel_private.gtt_entries) {
		printk (KERN_INFO PFX "Trying to disable local/stolen memory\n");
		return -EINVAL;
	}

	for (i = pg_start; i < (mem->page_count + pg_start); i++) {
		writel(agp_bridge->scratch_page, intel_private.gtt+i);
	}
	readl(intel_private.gtt+i-1);

	agp_bridge->driver->tlb_flush(mem);
	return 0;
}

/* Return the aperture size by just checking the resource length.  The effect
 * described in the spec of the MSAC registers is just changing of the
 * resource size.
 */
static int intel_i9xx_fetch_size(void)
{
	int num_sizes = ARRAY_SIZE(intel_i830_sizes);
	int aper_size; /* size in megabytes */
	int i;

	aper_size = pci_resource_len(intel_private.pcidev, 2) / MB(1);

	for (i = 0; i < num_sizes; i++) {
		if (aper_size == intel_i830_sizes[i].size) {
			agp_bridge->current_size = intel_i830_sizes + i;
			agp_bridge->previous_size = agp_bridge->current_size;
			return aper_size;
		}
	}

	return 0;
}

/* The intel i915 automatically initializes the agp aperture during POST.
 * Use the memory already set aside for in the GTT.
 */
static int intel_i915_create_gatt_table(struct agp_bridge_data *bridge)
{
	int page_order;
	struct aper_size_info_fixed *size;
	int num_entries;
	u32 temp, temp2;

	size = agp_bridge->current_size;
	page_order = size->page_order;
	num_entries = size->num_entries;
	agp_bridge->gatt_table_real = NULL;

	pci_read_config_dword(intel_private.pcidev, I915_MMADDR, &temp);
	pci_read_config_dword(intel_private.pcidev, I915_PTEADDR,&temp2);

	intel_private.gtt = ioremap(temp2, 256 * 1024);
	if (!intel_private.gtt)
		return -ENOMEM;

	temp &= 0xfff80000;

	intel_private.registers = ioremap(temp,128 * 4096);
	if (!intel_private.registers)
		return -ENOMEM;

	temp = readl(intel_private.registers+I810_PGETBL_CTL) & 0xfffff000;
	global_cache_flush();	/* FIXME: ? */

	/* we have to call this as early as possible after the MMIO base address is known */
	intel_i830_init_gtt_entries();

	agp_bridge->gatt_table = NULL;

	agp_bridge->gatt_bus_addr = temp;

	return 0;
}

/*
 * The i965 supports 36-bit physical addresses, but to keep
 * the format of the GTT the same, the bits that don't fit
 * in a 32-bit word are shifted down to bits 4..7.
 *
 * Gcc is smart enough to notice that "(addr >> 28) & 0xf0"
 * is always zero on 32-bit architectures, so no need to make
 * this conditional.
 */
static unsigned long intel_i965_mask_memory(struct agp_bridge_data *bridge,
	unsigned long addr, int type)
{
	/* Shift high bits down */
	addr |= (addr >> 28) & 0xf0;

	/* Type checking must be done elsewhere */
	return addr | bridge->driver->masks[type].mask;
}

/* The intel i965 automatically initializes the agp aperture during POST.
 * Use the memory already set aside for in the GTT.
 */
static int intel_i965_create_gatt_table(struct agp_bridge_data *bridge)
{
       int page_order;
       struct aper_size_info_fixed *size;
       int num_entries;
       u32 temp;

       size = agp_bridge->current_size;
       page_order = size->page_order;
       num_entries = size->num_entries;
       agp_bridge->gatt_table_real = NULL;

       pci_read_config_dword(intel_private.pcidev, I915_MMADDR, &temp);

       temp &= 0xfff00000;
       intel_private.gtt = ioremap((temp + (512 * 1024)) , 512 * 1024);

       if (!intel_private.gtt)
               return -ENOMEM;


       intel_private.registers = ioremap(temp,128 * 4096);
       if (!intel_private.registers)
               return -ENOMEM;

       temp = readl(intel_private.registers+I810_PGETBL_CTL) & 0xfffff000;
       global_cache_flush();   /* FIXME: ? */

       /* we have to call this as early as possible after the MMIO base address is known */
       intel_i830_init_gtt_entries();

       agp_bridge->gatt_table = NULL;

       agp_bridge->gatt_bus_addr = temp;

       return 0;
}


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
		printk (KERN_EMERG PFX "gatt bus addr too high");
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
	.agp_destroy_page	= agp_generic_destroy_page,
	.agp_type_to_mask_type  = agp_generic_type_to_mask_type,
};

static const struct agp_bridge_driver intel_810_driver = {
	.owner			= THIS_MODULE,
	.aperture_sizes		= intel_i810_sizes,
	.size_type		= FIXED_APER_SIZE,
	.num_aperture_sizes	= 2,
	.needs_scratch_page	= TRUE,
	.configure		= intel_i810_configure,
	.fetch_size		= intel_i810_fetch_size,
	.cleanup		= intel_i810_cleanup,
	.tlb_flush		= intel_i810_tlbflush,
	.mask_memory		= intel_i810_mask_memory,
	.masks			= intel_i810_masks,
	.agp_enable		= intel_i810_agp_enable,
	.cache_flush		= global_cache_flush,
	.create_gatt_table	= agp_generic_create_gatt_table,
	.free_gatt_table	= agp_generic_free_gatt_table,
	.insert_memory		= intel_i810_insert_entries,
	.remove_memory		= intel_i810_remove_entries,
	.alloc_by_type		= intel_i810_alloc_by_type,
	.free_by_type		= intel_i810_free_by_type,
	.agp_alloc_page		= agp_generic_alloc_page,
	.agp_destroy_page	= agp_generic_destroy_page,
	.agp_type_to_mask_type  = agp_generic_type_to_mask_type,
};

static const struct agp_bridge_driver intel_815_driver = {
	.owner			= THIS_MODULE,
	.aperture_sizes		= intel_815_sizes,
	.size_type		= U8_APER_SIZE,
	.num_aperture_sizes	= 2,
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
	.agp_destroy_page	= agp_generic_destroy_page,
	.agp_type_to_mask_type  = agp_generic_type_to_mask_type,
};

static const struct agp_bridge_driver intel_830_driver = {
	.owner			= THIS_MODULE,
	.aperture_sizes		= intel_i830_sizes,
	.size_type		= FIXED_APER_SIZE,
	.num_aperture_sizes	= 4,
	.needs_scratch_page	= TRUE,
	.configure		= intel_i830_configure,
	.fetch_size		= intel_i830_fetch_size,
	.cleanup		= intel_i830_cleanup,
	.tlb_flush		= intel_i810_tlbflush,
	.mask_memory		= intel_i810_mask_memory,
	.masks			= intel_i810_masks,
	.agp_enable		= intel_i810_agp_enable,
	.cache_flush		= global_cache_flush,
	.create_gatt_table	= intel_i830_create_gatt_table,
	.free_gatt_table	= intel_i830_free_gatt_table,
	.insert_memory		= intel_i830_insert_entries,
	.remove_memory		= intel_i830_remove_entries,
	.alloc_by_type		= intel_i830_alloc_by_type,
	.free_by_type		= intel_i810_free_by_type,
	.agp_alloc_page		= agp_generic_alloc_page,
	.agp_destroy_page	= agp_generic_destroy_page,
	.agp_type_to_mask_type  = intel_i830_type_to_mask_type,
};

static const struct agp_bridge_driver intel_820_driver = {
	.owner			= THIS_MODULE,
	.aperture_sizes		= intel_8xx_sizes,
	.size_type		= U8_APER_SIZE,
	.num_aperture_sizes	= 7,
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
	.agp_destroy_page	= agp_generic_destroy_page,
	.agp_type_to_mask_type  = agp_generic_type_to_mask_type,
};

static const struct agp_bridge_driver intel_830mp_driver = {
	.owner			= THIS_MODULE,
	.aperture_sizes		= intel_830mp_sizes,
	.size_type		= U8_APER_SIZE,
	.num_aperture_sizes	= 4,
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
	.agp_destroy_page	= agp_generic_destroy_page,
	.agp_type_to_mask_type  = agp_generic_type_to_mask_type,
};

static const struct agp_bridge_driver intel_840_driver = {
	.owner			= THIS_MODULE,
	.aperture_sizes		= intel_8xx_sizes,
	.size_type		= U8_APER_SIZE,
	.num_aperture_sizes	= 7,
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
	.agp_destroy_page	= agp_generic_destroy_page,
	.agp_type_to_mask_type  = agp_generic_type_to_mask_type,
};

static const struct agp_bridge_driver intel_845_driver = {
	.owner			= THIS_MODULE,
	.aperture_sizes		= intel_8xx_sizes,
	.size_type		= U8_APER_SIZE,
	.num_aperture_sizes	= 7,
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
	.agp_destroy_page	= agp_generic_destroy_page,
	.agp_type_to_mask_type  = agp_generic_type_to_mask_type,
};

static const struct agp_bridge_driver intel_850_driver = {
	.owner			= THIS_MODULE,
	.aperture_sizes		= intel_8xx_sizes,
	.size_type		= U8_APER_SIZE,
	.num_aperture_sizes	= 7,
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
	.agp_destroy_page	= agp_generic_destroy_page,
	.agp_type_to_mask_type  = agp_generic_type_to_mask_type,
};

static const struct agp_bridge_driver intel_860_driver = {
	.owner			= THIS_MODULE,
	.aperture_sizes		= intel_8xx_sizes,
	.size_type		= U8_APER_SIZE,
	.num_aperture_sizes	= 7,
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
	.agp_destroy_page	= agp_generic_destroy_page,
	.agp_type_to_mask_type  = agp_generic_type_to_mask_type,
};

static const struct agp_bridge_driver intel_915_driver = {
	.owner			= THIS_MODULE,
	.aperture_sizes		= intel_i830_sizes,
	.size_type		= FIXED_APER_SIZE,
	.num_aperture_sizes	= 4,
	.needs_scratch_page	= TRUE,
	.configure		= intel_i915_configure,
	.fetch_size		= intel_i9xx_fetch_size,
	.cleanup		= intel_i915_cleanup,
	.tlb_flush		= intel_i810_tlbflush,
	.mask_memory		= intel_i810_mask_memory,
	.masks			= intel_i810_masks,
	.agp_enable		= intel_i810_agp_enable,
	.cache_flush		= global_cache_flush,
	.create_gatt_table	= intel_i915_create_gatt_table,
	.free_gatt_table	= intel_i830_free_gatt_table,
	.insert_memory		= intel_i915_insert_entries,
	.remove_memory		= intel_i915_remove_entries,
	.alloc_by_type		= intel_i830_alloc_by_type,
	.free_by_type		= intel_i810_free_by_type,
	.agp_alloc_page		= agp_generic_alloc_page,
	.agp_destroy_page	= agp_generic_destroy_page,
	.agp_type_to_mask_type  = intel_i830_type_to_mask_type,
};

static const struct agp_bridge_driver intel_i965_driver = {
       .owner                  = THIS_MODULE,
       .aperture_sizes         = intel_i830_sizes,
       .size_type              = FIXED_APER_SIZE,
       .num_aperture_sizes     = 4,
       .needs_scratch_page     = TRUE,
       .configure              = intel_i915_configure,
       .fetch_size             = intel_i9xx_fetch_size,
       .cleanup                = intel_i915_cleanup,
       .tlb_flush              = intel_i810_tlbflush,
       .mask_memory            = intel_i965_mask_memory,
       .masks                  = intel_i810_masks,
       .agp_enable             = intel_i810_agp_enable,
       .cache_flush            = global_cache_flush,
       .create_gatt_table      = intel_i965_create_gatt_table,
       .free_gatt_table        = intel_i830_free_gatt_table,
       .insert_memory          = intel_i915_insert_entries,
       .remove_memory          = intel_i915_remove_entries,
       .alloc_by_type          = intel_i830_alloc_by_type,
       .free_by_type           = intel_i810_free_by_type,
       .agp_alloc_page         = agp_generic_alloc_page,
       .agp_destroy_page       = agp_generic_destroy_page,
       .agp_type_to_mask_type  = intel_i830_type_to_mask_type,
};

static const struct agp_bridge_driver intel_7505_driver = {
	.owner			= THIS_MODULE,
	.aperture_sizes		= intel_8xx_sizes,
	.size_type		= U8_APER_SIZE,
	.num_aperture_sizes	= 7,
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
	.agp_destroy_page	= agp_generic_destroy_page,
	.agp_type_to_mask_type  = agp_generic_type_to_mask_type,
};

static const struct agp_bridge_driver intel_g33_driver = {
	.owner                  = THIS_MODULE,
	.aperture_sizes         = intel_i830_sizes,
	.size_type              = FIXED_APER_SIZE,
	.num_aperture_sizes     = 4,
	.needs_scratch_page     = TRUE,
	.configure              = intel_i915_configure,
	.fetch_size             = intel_i9xx_fetch_size,
	.cleanup                = intel_i915_cleanup,
	.tlb_flush              = intel_i810_tlbflush,
	.mask_memory            = intel_i965_mask_memory,
	.masks                  = intel_i810_masks,
	.agp_enable             = intel_i810_agp_enable,
	.cache_flush            = global_cache_flush,
	.create_gatt_table      = intel_i915_create_gatt_table,
	.free_gatt_table        = intel_i830_free_gatt_table,
	.insert_memory          = intel_i915_insert_entries,
	.remove_memory          = intel_i915_remove_entries,
	.alloc_by_type          = intel_i830_alloc_by_type,
	.free_by_type           = intel_i810_free_by_type,
	.agp_alloc_page         = agp_generic_alloc_page,
	.agp_destroy_page       = agp_generic_destroy_page,
	.agp_type_to_mask_type  = intel_i830_type_to_mask_type,
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
		&intel_810_driver, &intel_815_driver },
	{ PCI_DEVICE_ID_INTEL_82820_HB, 0, "i820", &intel_820_driver, NULL },
	{ PCI_DEVICE_ID_INTEL_82820_UP_HB, 0, "i820", &intel_820_driver, NULL },
	{ PCI_DEVICE_ID_INTEL_82830_HB, PCI_DEVICE_ID_INTEL_82830_CGC, "830M",
		&intel_830mp_driver, &intel_830_driver },
	{ PCI_DEVICE_ID_INTEL_82840_HB, 0, "i840", &intel_840_driver, NULL },
	{ PCI_DEVICE_ID_INTEL_82845_HB, 0, "845G", &intel_845_driver, NULL },
	{ PCI_DEVICE_ID_INTEL_82845G_HB, PCI_DEVICE_ID_INTEL_82845G_IG, "830M",
		&intel_845_driver, &intel_830_driver },
	{ PCI_DEVICE_ID_INTEL_82850_HB, 0, "i850", &intel_850_driver, NULL },
	{ PCI_DEVICE_ID_INTEL_82855PM_HB, 0, "855PM", &intel_845_driver, NULL },
	{ PCI_DEVICE_ID_INTEL_82855GM_HB, PCI_DEVICE_ID_INTEL_82855GM_IG, "855GM",
		&intel_845_driver, &intel_830_driver },
	{ PCI_DEVICE_ID_INTEL_82860_HB, 0, "i860", &intel_860_driver, NULL },
	{ PCI_DEVICE_ID_INTEL_82865_HB, PCI_DEVICE_ID_INTEL_82865_IG, "865",
		&intel_845_driver, &intel_830_driver },
	{ PCI_DEVICE_ID_INTEL_82875_HB, 0, "i875", &intel_845_driver, NULL },
	{ PCI_DEVICE_ID_INTEL_82915G_HB, PCI_DEVICE_ID_INTEL_82915G_IG, "915G",
		&intel_845_driver, &intel_915_driver },
	{ PCI_DEVICE_ID_INTEL_82915GM_HB, PCI_DEVICE_ID_INTEL_82915GM_IG, "915GM",
		&intel_845_driver, &intel_915_driver },
	{ PCI_DEVICE_ID_INTEL_82945G_HB, PCI_DEVICE_ID_INTEL_82945G_IG, "945G",
		&intel_845_driver, &intel_915_driver },
	{ PCI_DEVICE_ID_INTEL_82945GM_HB, PCI_DEVICE_ID_INTEL_82945GM_IG, "945GM",
		&intel_845_driver, &intel_915_driver },
	{ PCI_DEVICE_ID_INTEL_82945GM_HB, PCI_DEVICE_ID_INTEL_82945GME_IG, "945GME",
		&intel_845_driver, &intel_915_driver },
	{ PCI_DEVICE_ID_INTEL_82946GZ_HB, PCI_DEVICE_ID_INTEL_82946GZ_IG, "946GZ",
		&intel_845_driver, &intel_i965_driver },
	{ PCI_DEVICE_ID_INTEL_82965G_1_HB, PCI_DEVICE_ID_INTEL_82965G_1_IG, "965G",
		&intel_845_driver, &intel_i965_driver },
	{ PCI_DEVICE_ID_INTEL_82965Q_HB, PCI_DEVICE_ID_INTEL_82965Q_IG, "965Q",
		&intel_845_driver, &intel_i965_driver },
	{ PCI_DEVICE_ID_INTEL_82965G_HB, PCI_DEVICE_ID_INTEL_82965G_IG, "965G",
		&intel_845_driver, &intel_i965_driver },
	{ PCI_DEVICE_ID_INTEL_82965GM_HB, PCI_DEVICE_ID_INTEL_82965GM_IG, "965GM",
		&intel_845_driver, &intel_i965_driver },
	{ PCI_DEVICE_ID_INTEL_82965GM_HB, PCI_DEVICE_ID_INTEL_82965GME_IG, "965GME/GLE",
		&intel_845_driver, &intel_i965_driver },
	{ PCI_DEVICE_ID_INTEL_7505_0, 0, "E7505", &intel_7505_driver, NULL },
	{ PCI_DEVICE_ID_INTEL_7205_0, 0, "E7205", &intel_7505_driver, NULL },
	{ PCI_DEVICE_ID_INTEL_G33_HB, PCI_DEVICE_ID_INTEL_G33_IG, "G33",
		&intel_845_driver, &intel_g33_driver },
	{ PCI_DEVICE_ID_INTEL_Q35_HB, PCI_DEVICE_ID_INTEL_Q35_IG, "Q35",
		&intel_845_driver, &intel_g33_driver },
	{ PCI_DEVICE_ID_INTEL_Q33_HB, PCI_DEVICE_ID_INTEL_Q33_IG, "Q33",
		&intel_845_driver, &intel_g33_driver },
	{ 0, 0, NULL, NULL, NULL }
};

static int __devinit agp_intel_probe(struct pci_dev *pdev,
				     const struct pci_device_id *ent)
{
	struct agp_bridge_data *bridge;
	u8 cap_ptr = 0;
	struct resource *r;
	int i;

	cap_ptr = pci_find_capability(pdev, PCI_CAP_ID_AGP);

	bridge = agp_alloc_bridge();
	if (!bridge)
		return -ENOMEM;

	for (i = 0; intel_agp_chipsets[i].name != NULL; i++) {
		/* In case that multiple models of gfx chip may
		   stand on same host bridge type, this can be
		   sure we detect the right IGD. */
		if ((pdev->device == intel_agp_chipsets[i].chip_id) &&
			((intel_agp_chipsets[i].gmch_chip_id == 0) ||
				find_gmch(intel_agp_chipsets[i].gmch_chip_id)))
			break;
	}

	if (intel_agp_chipsets[i].name == NULL) {
		if (cap_ptr)
			printk(KERN_WARNING PFX "Unsupported Intel chipset"
                               "(device id: %04x)\n", pdev->device);
		agp_put_bridge(bridge);
		return -ENODEV;
	}

	if (intel_agp_chipsets[i].gmch_chip_id != 0)
	    bridge->driver = intel_agp_chipsets[i].gmch_driver;
	else
	    bridge->driver = intel_agp_chipsets[i].driver;

	if (bridge->driver == NULL) {
		printk(KERN_WARNING PFX "Failed to find bridge device "
			"(chip_id: %04x)\n", intel_agp_chipsets[i].gmch_chip_id);
		agp_put_bridge(bridge);
		return -ENODEV;
        }

	bridge->dev = pdev;
	bridge->capndx = cap_ptr;
	bridge->dev_private_data = &intel_private;

	printk(KERN_INFO PFX "Detected an Intel %s Chipset.\n",
		intel_agp_chipsets[i].name);

	/*
	* The following fixes the case where the BIOS has "forgotten" to
	* provide an address range for the GART.
	* 20030610 - hamish@zot.org
	*/
	r = &pdev->resource[0];
	if (!r->start && r->end) {
		if (pci_assign_resource(pdev, 0)) {
			printk(KERN_ERR PFX "could not assign resource 0\n");
			agp_put_bridge(bridge);
			return -ENODEV;
		}
	}

	/*
	* If the device has not been properly setup, the following will catch
	* the problem and should stop the system from crashing.
	* 20030610 - hamish@zot.org
	*/
	if (pci_enable_device(pdev)) {
		printk(KERN_ERR PFX "Unable to Enable PCI device\n");
		agp_put_bridge(bridge);
		return -ENODEV;
	}

	/* Fill in the mode register */
	if (cap_ptr) {
		pci_read_config_dword(pdev,
				bridge->capndx+PCI_AGP_STATUS,
				&bridge->mode);
	}

	pci_set_drvdata(pdev, bridge);
	return agp_add_bridge(bridge);
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

	pci_restore_state(pdev);

	/* We should restore our graphics device's config space,
	 * as host bridge (00:00) resumes before graphics device (02:00),
	 * then our access to its pci space can work right.
	 */
	if (intel_private.pcidev)
		pci_restore_state(intel_private.pcidev);

	if (bridge->driver == &intel_generic_driver)
		intel_configure();
	else if (bridge->driver == &intel_850_driver)
		intel_850_configure();
	else if (bridge->driver == &intel_845_driver)
		intel_845_configure();
	else if (bridge->driver == &intel_830mp_driver)
		intel_830mp_configure();
	else if (bridge->driver == &intel_915_driver)
		intel_i915_configure();
	else if (bridge->driver == &intel_830_driver)
		intel_i830_configure();
	else if (bridge->driver == &intel_810_driver)
		intel_i810_configure();
	else if (bridge->driver == &intel_i965_driver)
		intel_i915_configure();

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
	ID(PCI_DEVICE_ID_INTEL_82855PM_HB),
	ID(PCI_DEVICE_ID_INTEL_82855GM_HB),
	ID(PCI_DEVICE_ID_INTEL_82860_HB),
	ID(PCI_DEVICE_ID_INTEL_82865_HB),
	ID(PCI_DEVICE_ID_INTEL_82875_HB),
	ID(PCI_DEVICE_ID_INTEL_7505_0),
	ID(PCI_DEVICE_ID_INTEL_7205_0),
	ID(PCI_DEVICE_ID_INTEL_82915G_HB),
	ID(PCI_DEVICE_ID_INTEL_82915GM_HB),
	ID(PCI_DEVICE_ID_INTEL_82945G_HB),
	ID(PCI_DEVICE_ID_INTEL_82945GM_HB),
	ID(PCI_DEVICE_ID_INTEL_82946GZ_HB),
	ID(PCI_DEVICE_ID_INTEL_82965G_1_HB),
	ID(PCI_DEVICE_ID_INTEL_82965Q_HB),
	ID(PCI_DEVICE_ID_INTEL_82965G_HB),
	ID(PCI_DEVICE_ID_INTEL_82965GM_HB),
	ID(PCI_DEVICE_ID_INTEL_G33_HB),
	ID(PCI_DEVICE_ID_INTEL_Q35_HB),
	ID(PCI_DEVICE_ID_INTEL_Q33_HB),
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

MODULE_AUTHOR("Dave Jones <davej@codemonkey.org.uk>");
MODULE_LICENSE("GPL and additional rights");

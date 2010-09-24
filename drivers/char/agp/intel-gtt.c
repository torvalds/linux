/*
 * Intel GTT (Graphics Translation Table) routines
 *
 * Caveat: This driver implements the linux agp interface, but this is far from
 * a agp driver! GTT support ended up here for purely historical reasons: The
 * old userspace intel graphics drivers needed an interface to map memory into
 * the GTT. And the drm provides a default interface for graphic devices sitting
 * on an agp port. So it made sense to fake the GTT support as an agp port to
 * avoid having to create a new api.
 *
 * With gem this does not make much sense anymore, just needlessly complicates
 * the code. But as long as the old graphics stack is still support, it's stuck
 * here.
 *
 * /fairy-tale-mode off
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/pagemap.h>
#include <linux/agp_backend.h>
#include <asm/smp.h>
#include "agp.h"
#include "intel-agp.h"
#include <linux/intel-gtt.h>
#include <drm/intel-gtt.h>

/*
 * If we have Intel graphics, we're not going to have anything other than
 * an Intel IOMMU. So make the correct use of the PCI DMA API contingent
 * on the Intel IOMMU support (CONFIG_DMAR).
 * Only newer chipsets need to bother with this, of course.
 */
#ifdef CONFIG_DMAR
#define USE_PCI_DMA_API 1
#else
#define USE_PCI_DMA_API 0
#endif

/* Max amount of stolen space, anything above will be returned to Linux */
int intel_max_stolen = 32 * 1024 * 1024;

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

#define INTEL_AGP_UNCACHED_MEMORY              0
#define INTEL_AGP_CACHED_MEMORY_LLC            1
#define INTEL_AGP_CACHED_MEMORY_LLC_GFDT       2
#define INTEL_AGP_CACHED_MEMORY_LLC_MLC        3
#define INTEL_AGP_CACHED_MEMORY_LLC_MLC_GFDT   4

struct intel_gtt_driver {
	unsigned int gen : 8;
	unsigned int is_g33 : 1;
	unsigned int is_pineview : 1;
	unsigned int is_ironlake : 1;
	unsigned int dma_mask_size : 8;
	/* Chipset specific GTT setup */
	int (*setup)(void);
	/* This should undo anything done in ->setup() save the unmapping
	 * of the mmio register file, that's done in the generic code. */
	void (*cleanup)(void);
	void (*write_entry)(dma_addr_t addr, unsigned int entry, unsigned int flags);
	/* Flags is a more or less chipset specific opaque value.
	 * For chipsets that need to support old ums (non-gem) code, this
	 * needs to be identical to the various supported agp memory types! */
	bool (*check_flags)(unsigned int flags);
	void (*chipset_flush)(void);
};

static struct _intel_private {
	struct intel_gtt base;
	const struct intel_gtt_driver *driver;
	struct pci_dev *pcidev;	/* device one */
	struct pci_dev *bridge_dev;
	u8 __iomem *registers;
	phys_addr_t gtt_bus_addr;
	phys_addr_t gma_bus_addr;
	phys_addr_t pte_bus_addr;
	u32 __iomem *gtt;		/* I915G */
	int num_dcache_entries;
	union {
		void __iomem *i9xx_flush_page;
		void *i8xx_flush_page;
	};
	struct page *i8xx_page;
	struct resource ifp_resource;
	int resource_valid;
	struct page *scratch_page;
	dma_addr_t scratch_page_dma;
} intel_private;

#define INTEL_GTT_GEN	intel_private.driver->gen
#define IS_G33		intel_private.driver->is_g33
#define IS_PINEVIEW	intel_private.driver->is_pineview
#define IS_IRONLAKE	intel_private.driver->is_ironlake

static void intel_agp_free_sglist(struct agp_memory *mem)
{
	struct sg_table st;

	st.sgl = mem->sg_list;
	st.orig_nents = st.nents = mem->page_count;

	sg_free_table(&st);

	mem->sg_list = NULL;
	mem->num_sg = 0;
}

static int intel_agp_map_memory(struct agp_memory *mem)
{
	struct sg_table st;
	struct scatterlist *sg;
	int i;

	if (mem->sg_list)
		return 0; /* already mapped (for e.g. resume */

	DBG("try mapping %lu pages\n", (unsigned long)mem->page_count);

	if (sg_alloc_table(&st, mem->page_count, GFP_KERNEL))
		goto err;

	mem->sg_list = sg = st.sgl;

	for (i = 0 ; i < mem->page_count; i++, sg = sg_next(sg))
		sg_set_page(sg, mem->pages[i], PAGE_SIZE, 0);

	mem->num_sg = pci_map_sg(intel_private.pcidev, mem->sg_list,
				 mem->page_count, PCI_DMA_BIDIRECTIONAL);
	if (unlikely(!mem->num_sg))
		goto err;

	return 0;

err:
	sg_free_table(&st);
	return -ENOMEM;
}

static void intel_agp_unmap_memory(struct agp_memory *mem)
{
	DBG("try unmapping %lu pages\n", (unsigned long)mem->page_count);

	pci_unmap_sg(intel_private.pcidev, mem->sg_list,
		     mem->page_count, PCI_DMA_BIDIRECTIONAL);
	intel_agp_free_sglist(mem);
}

static int intel_i810_fetch_size(void)
{
	u32 smram_miscc;
	struct aper_size_info_fixed *values;

	pci_read_config_dword(intel_private.bridge_dev,
			      I810_SMRAM_MISCC, &smram_miscc);
	values = A_SIZE_FIX(agp_bridge->driver->aperture_sizes);

	if ((smram_miscc & I810_GMS) == I810_GMS_DISABLE) {
		dev_warn(&intel_private.bridge_dev->dev, "i810 is disabled\n");
		return 0;
	}
	if ((smram_miscc & I810_GFX_MEM_WIN_SIZE) == I810_GFX_MEM_WIN_32M) {
		agp_bridge->current_size = (void *) (values + 1);
		agp_bridge->aperture_size_idx = 1;
		return values[1].size;
	} else {
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
			dev_err(&intel_private.pcidev->dev,
				"can't remap memory\n");
			return -ENOMEM;
		}
	}

	if ((readl(intel_private.registers+I810_DRAM_CTL)
		& I810_DRAM_ROW_0) == I810_DRAM_ROW_0_SDRAM) {
		/* This will need to be dynamically assigned */
		dev_info(&intel_private.pcidev->dev,
			 "detected 4MB dedicated video ram\n");
		intel_private.num_dcache_entries = 1024;
	}
	pci_read_config_dword(intel_private.pcidev, I810_GMADDR, &temp);
	agp_bridge->gart_bus_addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);
	writel(agp_bridge->gatt_bus_addr | I810_PGETBL_ENABLED, intel_private.registers+I810_PGETBL_CTL);
	readl(intel_private.registers+I810_PGETBL_CTL);	/* PCI Posting. */

	if (agp_bridge->driver->needs_scratch_page) {
		for (i = 0; i < current_size->num_entries; i++) {
			writel(agp_bridge->scratch_page, intel_private.registers+I810_PTE_BASE+(i*4));
		}
		readl(intel_private.registers+I810_PTE_BASE+((i-1)*4));	/* PCI posting. */
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

static void intel_fake_agp_enable(struct agp_bridge_data *bridge, u32 mode)
{
	return;
}

/* Exists to support ARGB cursors */
static struct page *i8xx_alloc_pages(void)
{
	struct page *page;

	page = alloc_pages(GFP_KERNEL | GFP_DMA32, 2);
	if (page == NULL)
		return NULL;

	if (set_pages_uc(page, 4) < 0) {
		set_pages_wb(page, 4);
		__free_pages(page, 2);
		return NULL;
	}
	get_page(page);
	atomic_inc(&agp_bridge->current_memory_agp);
	return page;
}

static void i8xx_destroy_pages(struct page *page)
{
	if (page == NULL)
		return;

	set_pages_wb(page, 4);
	put_page(page);
	__free_pages(page, 2);
	atomic_dec(&agp_bridge->current_memory_agp);
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
					page_to_phys(mem->pages[i]), mask_type),
			       intel_private.registers+I810_PTE_BASE+(j*4));
		}
		readl(intel_private.registers+I810_PTE_BASE+((j-1)*4));
		break;
	default:
		goto out_err;
	}

out:
	ret = 0;
out_err:
	mem->is_flushed = true;
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
	struct page *page;

	switch (pg_count) {
	case 1: page = agp_bridge->driver->agp_alloc_page(agp_bridge);
		break;
	case 4:
		/* kludge to get 4 physical pages for ARGB cursor */
		page = i8xx_alloc_pages();
		break;
	default:
		return NULL;
	}

	if (page == NULL)
		return NULL;

	new = agp_create_memory(pg_count);
	if (new == NULL)
		return NULL;

	new->pages[0] = page;
	if (pg_count == 4) {
		/* kludge to get 4 physical pages for ARGB cursor */
		new->pages[1] = new->pages[0] + 1;
		new->pages[2] = new->pages[1] + 1;
		new->pages[3] = new->pages[2] + 1;
	}
	new->page_count = pg_count;
	new->num_scratch_pages = pg_count;
	new->type = AGP_PHYS_MEMORY;
	new->physical = page_to_phys(new->pages[0]);
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
			i8xx_destroy_pages(curr->pages[0]);
		else {
			agp_bridge->driver->agp_destroy_page(curr->pages[0],
							     AGP_PAGE_DESTROY_UNMAP);
			agp_bridge->driver->agp_destroy_page(curr->pages[0],
							     AGP_PAGE_DESTROY_FREE);
		}
		agp_free_page_array(curr);
	}
	kfree(curr);
}

static unsigned long intel_i810_mask_memory(struct agp_bridge_data *bridge,
					    dma_addr_t addr, int type)
{
	/* Type checking must be done elsewhere */
	return addr | bridge->driver->masks[type].mask;
}

static int intel_gtt_setup_scratch_page(void)
{
	struct page *page;
	dma_addr_t dma_addr;

	page = alloc_page(GFP_KERNEL | GFP_DMA32 | __GFP_ZERO);
	if (page == NULL)
		return -ENOMEM;
	get_page(page);
	set_pages_uc(page, 1);

	if (USE_PCI_DMA_API && INTEL_GTT_GEN > 2) {
		dma_addr = pci_map_page(intel_private.pcidev, page, 0,
				    PAGE_SIZE, PCI_DMA_BIDIRECTIONAL);
		if (pci_dma_mapping_error(intel_private.pcidev, dma_addr))
			return -EINVAL;

		intel_private.scratch_page_dma = dma_addr;
	} else
		intel_private.scratch_page_dma = page_to_phys(page);

	intel_private.scratch_page = page;

	return 0;
}

static const struct aper_size_info_fixed const intel_fake_agp_sizes[] = {
	{128, 32768, 5},
	/* The 64M mode still requires a 128k gatt */
	{64, 16384, 5},
	{256, 65536, 6},
	{512, 131072, 7},
};

static unsigned int intel_gtt_stolen_entries(void)
{
	u16 gmch_ctrl;
	u8 rdct;
	int local = 0;
	static const int ddt[4] = { 0, 16, 32, 64 };
	unsigned int overhead_entries, stolen_entries;
	unsigned int stolen_size = 0;

	pci_read_config_word(intel_private.bridge_dev,
			     I830_GMCH_CTRL, &gmch_ctrl);

	if (INTEL_GTT_GEN > 4 || IS_PINEVIEW)
		overhead_entries = 0;
	else
		overhead_entries = intel_private.base.gtt_mappable_entries
			/ 1024;

	overhead_entries += 1; /* BIOS popup */

	if (intel_private.bridge_dev->device == PCI_DEVICE_ID_INTEL_82830_HB ||
	    intel_private.bridge_dev->device == PCI_DEVICE_ID_INTEL_82845G_HB) {
		switch (gmch_ctrl & I830_GMCH_GMS_MASK) {
		case I830_GMCH_GMS_STOLEN_512:
			stolen_size = KB(512);
			break;
		case I830_GMCH_GMS_STOLEN_1024:
			stolen_size = MB(1);
			break;
		case I830_GMCH_GMS_STOLEN_8192:
			stolen_size = MB(8);
			break;
		case I830_GMCH_GMS_LOCAL:
			rdct = readb(intel_private.registers+I830_RDRAM_CHANNEL_TYPE);
			stolen_size = (I830_RDRAM_ND(rdct) + 1) *
					MB(ddt[I830_RDRAM_DDT(rdct)]);
			local = 1;
			break;
		default:
			stolen_size = 0;
			break;
		}
	} else if (INTEL_GTT_GEN == 6) {
		/*
		 * SandyBridge has new memory control reg at 0x50.w
		 */
		u16 snb_gmch_ctl;
		pci_read_config_word(intel_private.pcidev, SNB_GMCH_CTRL, &snb_gmch_ctl);
		switch (snb_gmch_ctl & SNB_GMCH_GMS_STOLEN_MASK) {
		case SNB_GMCH_GMS_STOLEN_32M:
			stolen_size = MB(32);
			break;
		case SNB_GMCH_GMS_STOLEN_64M:
			stolen_size = MB(64);
			break;
		case SNB_GMCH_GMS_STOLEN_96M:
			stolen_size = MB(96);
			break;
		case SNB_GMCH_GMS_STOLEN_128M:
			stolen_size = MB(128);
			break;
		case SNB_GMCH_GMS_STOLEN_160M:
			stolen_size = MB(160);
			break;
		case SNB_GMCH_GMS_STOLEN_192M:
			stolen_size = MB(192);
			break;
		case SNB_GMCH_GMS_STOLEN_224M:
			stolen_size = MB(224);
			break;
		case SNB_GMCH_GMS_STOLEN_256M:
			stolen_size = MB(256);
			break;
		case SNB_GMCH_GMS_STOLEN_288M:
			stolen_size = MB(288);
			break;
		case SNB_GMCH_GMS_STOLEN_320M:
			stolen_size = MB(320);
			break;
		case SNB_GMCH_GMS_STOLEN_352M:
			stolen_size = MB(352);
			break;
		case SNB_GMCH_GMS_STOLEN_384M:
			stolen_size = MB(384);
			break;
		case SNB_GMCH_GMS_STOLEN_416M:
			stolen_size = MB(416);
			break;
		case SNB_GMCH_GMS_STOLEN_448M:
			stolen_size = MB(448);
			break;
		case SNB_GMCH_GMS_STOLEN_480M:
			stolen_size = MB(480);
			break;
		case SNB_GMCH_GMS_STOLEN_512M:
			stolen_size = MB(512);
			break;
		}
	} else {
		switch (gmch_ctrl & I855_GMCH_GMS_MASK) {
		case I855_GMCH_GMS_STOLEN_1M:
			stolen_size = MB(1);
			break;
		case I855_GMCH_GMS_STOLEN_4M:
			stolen_size = MB(4);
			break;
		case I855_GMCH_GMS_STOLEN_8M:
			stolen_size = MB(8);
			break;
		case I855_GMCH_GMS_STOLEN_16M:
			stolen_size = MB(16);
			break;
		case I855_GMCH_GMS_STOLEN_32M:
			stolen_size = MB(32);
			break;
		case I915_GMCH_GMS_STOLEN_48M:
			stolen_size = MB(48);
			break;
		case I915_GMCH_GMS_STOLEN_64M:
			stolen_size = MB(64);
			break;
		case G33_GMCH_GMS_STOLEN_128M:
			stolen_size = MB(128);
			break;
		case G33_GMCH_GMS_STOLEN_256M:
			stolen_size = MB(256);
			break;
		case INTEL_GMCH_GMS_STOLEN_96M:
			stolen_size = MB(96);
			break;
		case INTEL_GMCH_GMS_STOLEN_160M:
			stolen_size = MB(160);
			break;
		case INTEL_GMCH_GMS_STOLEN_224M:
			stolen_size = MB(224);
			break;
		case INTEL_GMCH_GMS_STOLEN_352M:
			stolen_size = MB(352);
			break;
		default:
			stolen_size = 0;
			break;
		}
	}

	if (!local && stolen_size > intel_max_stolen) {
		dev_info(&intel_private.bridge_dev->dev,
			 "detected %dK stolen memory, trimming to %dK\n",
			 stolen_size / KB(1), intel_max_stolen / KB(1));
		stolen_size = intel_max_stolen;
	} else if (stolen_size > 0) {
		dev_info(&intel_private.bridge_dev->dev, "detected %dK %s memory\n",
		       stolen_size / KB(1), local ? "local" : "stolen");
	} else {
		dev_info(&intel_private.bridge_dev->dev,
		       "no pre-allocated video memory detected\n");
		stolen_size = 0;
	}

	stolen_entries = stolen_size/KB(4) - overhead_entries;

	return stolen_entries;
}

static unsigned int intel_gtt_total_entries(void)
{
	int size;

	if (IS_G33 || INTEL_GTT_GEN == 4 || INTEL_GTT_GEN == 5) {
		u32 pgetbl_ctl;
		pgetbl_ctl = readl(intel_private.registers+I810_PGETBL_CTL);

		switch (pgetbl_ctl & I965_PGETBL_SIZE_MASK) {
		case I965_PGETBL_SIZE_128KB:
			size = KB(128);
			break;
		case I965_PGETBL_SIZE_256KB:
			size = KB(256);
			break;
		case I965_PGETBL_SIZE_512KB:
			size = KB(512);
			break;
		case I965_PGETBL_SIZE_1MB:
			size = KB(1024);
			break;
		case I965_PGETBL_SIZE_2MB:
			size = KB(2048);
			break;
		case I965_PGETBL_SIZE_1_5MB:
			size = KB(1024 + 512);
			break;
		default:
			dev_info(&intel_private.pcidev->dev,
				 "unknown page table size, assuming 512KB\n");
			size = KB(512);
		}

		return size/4;
	} else if (INTEL_GTT_GEN == 6) {
		u16 snb_gmch_ctl;

		pci_read_config_word(intel_private.pcidev, SNB_GMCH_CTRL, &snb_gmch_ctl);
		switch (snb_gmch_ctl & SNB_GTT_SIZE_MASK) {
		default:
		case SNB_GTT_SIZE_0M:
			printk(KERN_ERR "Bad GTT size mask: 0x%04x.\n", snb_gmch_ctl);
			size = MB(0);
			break;
		case SNB_GTT_SIZE_1M:
			size = MB(1);
			break;
		case SNB_GTT_SIZE_2M:
			size = MB(2);
			break;
		}
		return size/4;
	} else {
		/* On previous hardware, the GTT size was just what was
		 * required to map the aperture.
		 */
		return intel_private.base.gtt_mappable_entries;
	}
}

static unsigned int intel_gtt_mappable_entries(void)
{
	unsigned int aperture_size;

	if (INTEL_GTT_GEN == 2) {
		u16 gmch_ctrl;

		pci_read_config_word(intel_private.bridge_dev,
				     I830_GMCH_CTRL, &gmch_ctrl);

		if ((gmch_ctrl & I830_GMCH_MEM_MASK) == I830_GMCH_MEM_64M)
			aperture_size = MB(64);
		else
			aperture_size = MB(128);
	} else {
		/* 9xx supports large sizes, just look at the length */
		aperture_size = pci_resource_len(intel_private.pcidev, 2);
	}

	return aperture_size >> PAGE_SHIFT;
}

static void intel_gtt_teardown_scratch_page(void)
{
	set_pages_wb(intel_private.scratch_page, 1);
	pci_unmap_page(intel_private.pcidev, intel_private.scratch_page_dma,
		       PAGE_SIZE, PCI_DMA_BIDIRECTIONAL);
	put_page(intel_private.scratch_page);
	__free_page(intel_private.scratch_page);
}

static void intel_gtt_cleanup(void)
{
	intel_private.driver->cleanup();

	iounmap(intel_private.gtt);
	iounmap(intel_private.registers);
	
	intel_gtt_teardown_scratch_page();
}

static int intel_gtt_init(void)
{
	u32 gtt_map_size;
	int ret;

	ret = intel_private.driver->setup();
	if (ret != 0)
		return ret;

	intel_private.base.gtt_mappable_entries = intel_gtt_mappable_entries();
	intel_private.base.gtt_total_entries = intel_gtt_total_entries();

	dev_info(&intel_private.bridge_dev->dev,
			"detected gtt size: %dK total, %dK mappable\n",
			intel_private.base.gtt_total_entries * 4,
			intel_private.base.gtt_mappable_entries * 4);

	gtt_map_size = intel_private.base.gtt_total_entries * 4;

	intel_private.gtt = ioremap(intel_private.gtt_bus_addr,
				    gtt_map_size);
	if (!intel_private.gtt) {
		intel_private.driver->cleanup();
		iounmap(intel_private.registers);
		return -ENOMEM;
	}

	global_cache_flush();   /* FIXME: ? */

	/* we have to call this as early as possible after the MMIO base address is known */
	intel_private.base.gtt_stolen_entries = intel_gtt_stolen_entries();
	if (intel_private.base.gtt_stolen_entries == 0) {
		intel_private.driver->cleanup();
		iounmap(intel_private.registers);
		iounmap(intel_private.gtt);
		return -ENOMEM;
	}

	ret = intel_gtt_setup_scratch_page();
	if (ret != 0) {
		intel_gtt_cleanup();
		return ret;
	}

	return 0;
}

static int intel_fake_agp_fetch_size(void)
{
	int num_sizes = ARRAY_SIZE(intel_fake_agp_sizes);
	unsigned int aper_size;
	int i;

	aper_size = (intel_private.base.gtt_mappable_entries << PAGE_SHIFT)
		    / MB(1);

	for (i = 0; i < num_sizes; i++) {
		if (aper_size == intel_fake_agp_sizes[i].size) {
			agp_bridge->current_size =
				(void *) (intel_fake_agp_sizes + i);
			return aper_size;
		}
	}

	return 0;
}

static void i830_cleanup(void)
{
	kunmap(intel_private.i8xx_page);
	intel_private.i8xx_flush_page = NULL;

	__free_page(intel_private.i8xx_page);
	intel_private.i8xx_page = NULL;
}

static void intel_i830_setup_flush(void)
{
	/* return if we've already set the flush mechanism up */
	if (intel_private.i8xx_page)
		return;

	intel_private.i8xx_page = alloc_page(GFP_KERNEL);
	if (!intel_private.i8xx_page)
		return;

	intel_private.i8xx_flush_page = kmap(intel_private.i8xx_page);
	if (!intel_private.i8xx_flush_page)
		i830_cleanup();
}

/* The chipset_flush interface needs to get data that has already been
 * flushed out of the CPU all the way out to main memory, because the GPU
 * doesn't snoop those buffers.
 *
 * The 8xx series doesn't have the same lovely interface for flushing the
 * chipset write buffers that the later chips do. According to the 865
 * specs, it's 64 octwords, or 1KB.  So, to get those previous things in
 * that buffer out, we just fill 1KB and clflush it out, on the assumption
 * that it'll push whatever was in there out.  It appears to work.
 */
static void i830_chipset_flush(void)
{
	unsigned int *pg = intel_private.i8xx_flush_page;

	memset(pg, 0, 1024);

	if (cpu_has_clflush)
		clflush_cache_range(pg, 1024);
	else if (wbinvd_on_all_cpus() != 0)
		printk(KERN_ERR "Timed out waiting for cache flush.\n");
}

static void i830_write_entry(dma_addr_t addr, unsigned int entry,
			     unsigned int flags)
{
	u32 pte_flags = I810_PTE_VALID;
	
	switch (flags) {
	case AGP_DCACHE_MEMORY:
		pte_flags |= I810_PTE_LOCAL;
		break;
	case AGP_USER_CACHED_MEMORY:
		pte_flags |= I830_PTE_SYSTEM_CACHED;
		break;
	}

	writel(addr | pte_flags, intel_private.gtt + entry);
}

static void intel_enable_gtt(void)
{
	u32 gma_addr;
	u16 gmch_ctrl;

	if (INTEL_GTT_GEN == 2)
		pci_read_config_dword(intel_private.pcidev, I810_GMADDR,
				      &gma_addr);
	else
		pci_read_config_dword(intel_private.pcidev, I915_GMADDR,
				      &gma_addr);

	intel_private.gma_bus_addr = (gma_addr & PCI_BASE_ADDRESS_MEM_MASK);

	pci_read_config_word(intel_private.bridge_dev, I830_GMCH_CTRL, &gmch_ctrl);
	gmch_ctrl |= I830_GMCH_ENABLED;
	pci_write_config_word(intel_private.bridge_dev, I830_GMCH_CTRL, gmch_ctrl);

	writel(intel_private.pte_bus_addr|I810_PGETBL_ENABLED,
	       intel_private.registers+I810_PGETBL_CTL);
	readl(intel_private.registers+I810_PGETBL_CTL);	/* PCI Posting. */
}

static int i830_setup(void)
{
	u32 reg_addr;

	pci_read_config_dword(intel_private.pcidev, I810_MMADDR, &reg_addr);
	reg_addr &= 0xfff80000;

	intel_private.registers = ioremap(reg_addr, KB(64));
	if (!intel_private.registers)
		return -ENOMEM;

	intel_private.gtt_bus_addr = reg_addr + I810_PTE_BASE;
	intel_private.pte_bus_addr =
		readl(intel_private.registers+I810_PGETBL_CTL) & 0xfffff000;

	intel_i830_setup_flush();

	return 0;
}

static int intel_fake_agp_create_gatt_table(struct agp_bridge_data *bridge)
{
	agp_bridge->gatt_table_real = NULL;
	agp_bridge->gatt_table = NULL;
	agp_bridge->gatt_bus_addr = 0;

	return 0;
}

static int intel_fake_agp_free_gatt_table(struct agp_bridge_data *bridge)
{
	return 0;
}

static int intel_fake_agp_configure(void)
{
	int i;

	intel_enable_gtt();

	agp_bridge->gart_bus_addr = intel_private.gma_bus_addr;

	for (i = intel_private.base.gtt_stolen_entries;
			i < intel_private.base.gtt_total_entries; i++) {
		intel_private.driver->write_entry(intel_private.scratch_page_dma,
						  i, 0);
	}
	readl(intel_private.gtt+i-1);	/* PCI Posting. */

	global_cache_flush();

	return 0;
}

static bool i830_check_flags(unsigned int flags)
{
	switch (flags) {
	case 0:
	case AGP_PHYS_MEMORY:
	case AGP_USER_CACHED_MEMORY:
	case AGP_USER_MEMORY:
		return true;
	}

	return false;
}

static void intel_gtt_insert_sg_entries(struct scatterlist *sg_list,
					unsigned int sg_len,
					unsigned int pg_start,
					unsigned int flags)
{
	struct scatterlist *sg;
	unsigned int len, m;
	int i, j;

	j = pg_start;

	/* sg may merge pages, but we have to separate
	 * per-page addr for GTT */
	for_each_sg(sg_list, sg, sg_len, i) {
		len = sg_dma_len(sg) >> PAGE_SHIFT;
		for (m = 0; m < len; m++) {
			dma_addr_t addr = sg_dma_address(sg) + (m << PAGE_SHIFT);
			intel_private.driver->write_entry(addr,
							  j, flags);
			j++;
		}
	}
	readl(intel_private.gtt+j-1);
}

static int intel_fake_agp_insert_entries(struct agp_memory *mem,
					 off_t pg_start, int type)
{
	int i, j;
	int ret = -EINVAL;

	if (mem->page_count == 0)
		goto out;

	if (pg_start < intel_private.base.gtt_stolen_entries) {
		dev_printk(KERN_DEBUG, &intel_private.pcidev->dev,
			   "pg_start == 0x%.8lx, gtt_stolen_entries == 0x%.8x\n",
			   pg_start, intel_private.base.gtt_stolen_entries);

		dev_info(&intel_private.pcidev->dev,
			 "trying to insert into local/stolen memory\n");
		goto out_err;
	}

	if ((pg_start + mem->page_count) > intel_private.base.gtt_total_entries)
		goto out_err;

	if (type != mem->type)
		goto out_err;

	if (!intel_private.driver->check_flags(type))
		goto out_err;

	if (!mem->is_flushed)
		global_cache_flush();

	if (USE_PCI_DMA_API && INTEL_GTT_GEN > 2) {
		ret = intel_agp_map_memory(mem);
		if (ret != 0)
			return ret;

		intel_gtt_insert_sg_entries(mem->sg_list, mem->num_sg,
					    pg_start, type);
	} else {
		for (i = 0, j = pg_start; i < mem->page_count; i++, j++) {
			dma_addr_t addr = page_to_phys(mem->pages[i]);
			intel_private.driver->write_entry(addr,
							  j, type);
		}
		readl(intel_private.gtt+j-1);
	}

out:
	ret = 0;
out_err:
	mem->is_flushed = true;
	return ret;
}

static int intel_fake_agp_remove_entries(struct agp_memory *mem,
					 off_t pg_start, int type)
{
	int i;

	if (mem->page_count == 0)
		return 0;

	if (pg_start < intel_private.base.gtt_stolen_entries) {
		dev_info(&intel_private.pcidev->dev,
			 "trying to disable local/stolen memory\n");
		return -EINVAL;
	}

	if (USE_PCI_DMA_API && INTEL_GTT_GEN > 2)
		intel_agp_unmap_memory(mem);

	for (i = pg_start; i < (mem->page_count + pg_start); i++) {
		intel_private.driver->write_entry(intel_private.scratch_page_dma,
						  i, 0);
	}
	readl(intel_private.gtt+i-1);

	return 0;
}

static void intel_fake_agp_chipset_flush(struct agp_bridge_data *bridge)
{
	intel_private.driver->chipset_flush();
}

static struct agp_memory *intel_fake_agp_alloc_by_type(size_t pg_count,
						       int type)
{
	if (type == AGP_PHYS_MEMORY)
		return alloc_agpphysmem_i8xx(pg_count, type);
	/* always return NULL for other allocation types for now */
	return NULL;
}

static int intel_alloc_chipset_flush_resource(void)
{
	int ret;
	ret = pci_bus_alloc_resource(intel_private.bridge_dev->bus, &intel_private.ifp_resource, PAGE_SIZE,
				     PAGE_SIZE, PCIBIOS_MIN_MEM, 0,
				     pcibios_align_resource, intel_private.bridge_dev);

	return ret;
}

static void intel_i915_setup_chipset_flush(void)
{
	int ret;
	u32 temp;

	pci_read_config_dword(intel_private.bridge_dev, I915_IFPADDR, &temp);
	if (!(temp & 0x1)) {
		intel_alloc_chipset_flush_resource();
		intel_private.resource_valid = 1;
		pci_write_config_dword(intel_private.bridge_dev, I915_IFPADDR, (intel_private.ifp_resource.start & 0xffffffff) | 0x1);
	} else {
		temp &= ~1;

		intel_private.resource_valid = 1;
		intel_private.ifp_resource.start = temp;
		intel_private.ifp_resource.end = temp + PAGE_SIZE;
		ret = request_resource(&iomem_resource, &intel_private.ifp_resource);
		/* some BIOSes reserve this area in a pnp some don't */
		if (ret)
			intel_private.resource_valid = 0;
	}
}

static void intel_i965_g33_setup_chipset_flush(void)
{
	u32 temp_hi, temp_lo;
	int ret;

	pci_read_config_dword(intel_private.bridge_dev, I965_IFPADDR + 4, &temp_hi);
	pci_read_config_dword(intel_private.bridge_dev, I965_IFPADDR, &temp_lo);

	if (!(temp_lo & 0x1)) {

		intel_alloc_chipset_flush_resource();

		intel_private.resource_valid = 1;
		pci_write_config_dword(intel_private.bridge_dev, I965_IFPADDR + 4,
			upper_32_bits(intel_private.ifp_resource.start));
		pci_write_config_dword(intel_private.bridge_dev, I965_IFPADDR, (intel_private.ifp_resource.start & 0xffffffff) | 0x1);
	} else {
		u64 l64;

		temp_lo &= ~0x1;
		l64 = ((u64)temp_hi << 32) | temp_lo;

		intel_private.resource_valid = 1;
		intel_private.ifp_resource.start = l64;
		intel_private.ifp_resource.end = l64 + PAGE_SIZE;
		ret = request_resource(&iomem_resource, &intel_private.ifp_resource);
		/* some BIOSes reserve this area in a pnp some don't */
		if (ret)
			intel_private.resource_valid = 0;
	}
}

static void intel_i9xx_setup_flush(void)
{
	/* return if already configured */
	if (intel_private.ifp_resource.start)
		return;

	if (INTEL_GTT_GEN == 6)
		return;

	/* setup a resource for this object */
	intel_private.ifp_resource.name = "Intel Flush Page";
	intel_private.ifp_resource.flags = IORESOURCE_MEM;

	/* Setup chipset flush for 915 */
	if (IS_G33 || INTEL_GTT_GEN >= 4) {
		intel_i965_g33_setup_chipset_flush();
	} else {
		intel_i915_setup_chipset_flush();
	}

	if (intel_private.ifp_resource.start)
		intel_private.i9xx_flush_page = ioremap_nocache(intel_private.ifp_resource.start, PAGE_SIZE);
	if (!intel_private.i9xx_flush_page)
		dev_err(&intel_private.pcidev->dev,
			"can't ioremap flush page - no chipset flushing\n");
}

static void i9xx_cleanup(void)
{
	if (intel_private.i9xx_flush_page)
		iounmap(intel_private.i9xx_flush_page);
	if (intel_private.resource_valid)
		release_resource(&intel_private.ifp_resource);
	intel_private.ifp_resource.start = 0;
	intel_private.resource_valid = 0;
}

static void i9xx_chipset_flush(void)
{
	if (intel_private.i9xx_flush_page)
		writel(1, intel_private.i9xx_flush_page);
}

static void i965_write_entry(dma_addr_t addr, unsigned int entry,
			     unsigned int flags)
{
	/* Shift high bits down */
	addr |= (addr >> 28) & 0xf0;
	writel(addr | I810_PTE_VALID, intel_private.gtt + entry);
}

static bool gen6_check_flags(unsigned int flags)
{
	return true;
}

static void gen6_write_entry(dma_addr_t addr, unsigned int entry,
			     unsigned int flags)
{
	unsigned int type_mask = flags & ~AGP_USER_CACHED_MEMORY_GFDT;
	unsigned int gfdt = flags & AGP_USER_CACHED_MEMORY_GFDT;
	u32 pte_flags;

	if (type_mask == AGP_USER_UNCACHED_MEMORY)
		pte_flags = GEN6_PTE_UNCACHED;
	else if (type_mask == AGP_USER_CACHED_MEMORY_LLC_MLC) {
		pte_flags = GEN6_PTE_LLC;
		if (gfdt)
			pte_flags |= GEN6_PTE_GFDT;
	} else { /* set 'normal'/'cached' to LLC by default */
		pte_flags = GEN6_PTE_LLC_MLC;
		if (gfdt)
			pte_flags |= GEN6_PTE_GFDT;
	}

	/* gen6 has bit11-4 for physical addr bit39-32 */
	addr |= (addr >> 28) & 0xff0;
	writel(addr | pte_flags, intel_private.gtt + entry);
}

static void gen6_cleanup(void)
{
}

static int i9xx_setup(void)
{
	u32 reg_addr;

	pci_read_config_dword(intel_private.pcidev, I915_MMADDR, &reg_addr);

	reg_addr &= 0xfff80000;

	intel_private.registers = ioremap(reg_addr, 128 * 4096);
	if (!intel_private.registers)
		return -ENOMEM;

	if (INTEL_GTT_GEN == 3) {
		u32 gtt_addr;

		pci_read_config_dword(intel_private.pcidev,
				      I915_PTEADDR, &gtt_addr);
		intel_private.gtt_bus_addr = gtt_addr;
	} else {
		u32 gtt_offset;

		switch (INTEL_GTT_GEN) {
		case 5:
		case 6:
			gtt_offset = MB(2);
			break;
		case 4:
		default:
			gtt_offset =  KB(512);
			break;
		}
		intel_private.gtt_bus_addr = reg_addr + gtt_offset;
	}

	intel_private.pte_bus_addr =
		readl(intel_private.registers+I810_PGETBL_CTL) & 0xfffff000;

	intel_i9xx_setup_flush();

	return 0;
}

static const struct agp_bridge_driver intel_810_driver = {
	.owner			= THIS_MODULE,
	.aperture_sizes		= intel_i810_sizes,
	.size_type		= FIXED_APER_SIZE,
	.num_aperture_sizes	= 2,
	.needs_scratch_page	= true,
	.configure		= intel_i810_configure,
	.fetch_size		= intel_i810_fetch_size,
	.cleanup		= intel_i810_cleanup,
	.mask_memory		= intel_i810_mask_memory,
	.masks			= intel_i810_masks,
	.agp_enable		= intel_fake_agp_enable,
	.cache_flush		= global_cache_flush,
	.create_gatt_table	= agp_generic_create_gatt_table,
	.free_gatt_table	= agp_generic_free_gatt_table,
	.insert_memory		= intel_i810_insert_entries,
	.remove_memory		= intel_i810_remove_entries,
	.alloc_by_type		= intel_i810_alloc_by_type,
	.free_by_type		= intel_i810_free_by_type,
	.agp_alloc_page		= agp_generic_alloc_page,
	.agp_alloc_pages        = agp_generic_alloc_pages,
	.agp_destroy_page	= agp_generic_destroy_page,
	.agp_destroy_pages      = agp_generic_destroy_pages,
	.agp_type_to_mask_type  = agp_generic_type_to_mask_type,
};

static const struct agp_bridge_driver intel_fake_agp_driver = {
	.owner			= THIS_MODULE,
	.size_type		= FIXED_APER_SIZE,
	.aperture_sizes		= intel_fake_agp_sizes,
	.num_aperture_sizes	= ARRAY_SIZE(intel_fake_agp_sizes),
	.configure		= intel_fake_agp_configure,
	.fetch_size		= intel_fake_agp_fetch_size,
	.cleanup		= intel_gtt_cleanup,
	.agp_enable		= intel_fake_agp_enable,
	.cache_flush		= global_cache_flush,
	.create_gatt_table	= intel_fake_agp_create_gatt_table,
	.free_gatt_table	= intel_fake_agp_free_gatt_table,
	.insert_memory		= intel_fake_agp_insert_entries,
	.remove_memory		= intel_fake_agp_remove_entries,
	.alloc_by_type		= intel_fake_agp_alloc_by_type,
	.free_by_type		= intel_i810_free_by_type,
	.agp_alloc_page		= agp_generic_alloc_page,
	.agp_alloc_pages        = agp_generic_alloc_pages,
	.agp_destroy_page	= agp_generic_destroy_page,
	.agp_destroy_pages      = agp_generic_destroy_pages,
	.chipset_flush		= intel_fake_agp_chipset_flush,
};

static const struct intel_gtt_driver i81x_gtt_driver = {
	.gen = 1,
	.dma_mask_size = 32,
};
static const struct intel_gtt_driver i8xx_gtt_driver = {
	.gen = 2,
	.setup = i830_setup,
	.cleanup = i830_cleanup,
	.write_entry = i830_write_entry,
	.dma_mask_size = 32,
	.check_flags = i830_check_flags,
	.chipset_flush = i830_chipset_flush,
};
static const struct intel_gtt_driver i915_gtt_driver = {
	.gen = 3,
	.setup = i9xx_setup,
	.cleanup = i9xx_cleanup,
	/* i945 is the last gpu to need phys mem (for overlay and cursors). */
	.write_entry = i830_write_entry, 
	.dma_mask_size = 32,
	.check_flags = i830_check_flags,
	.chipset_flush = i9xx_chipset_flush,
};
static const struct intel_gtt_driver g33_gtt_driver = {
	.gen = 3,
	.is_g33 = 1,
	.setup = i9xx_setup,
	.cleanup = i9xx_cleanup,
	.write_entry = i965_write_entry,
	.dma_mask_size = 36,
	.check_flags = i830_check_flags,
	.chipset_flush = i9xx_chipset_flush,
};
static const struct intel_gtt_driver pineview_gtt_driver = {
	.gen = 3,
	.is_pineview = 1, .is_g33 = 1,
	.setup = i9xx_setup,
	.cleanup = i9xx_cleanup,
	.write_entry = i965_write_entry,
	.dma_mask_size = 36,
	.check_flags = i830_check_flags,
	.chipset_flush = i9xx_chipset_flush,
};
static const struct intel_gtt_driver i965_gtt_driver = {
	.gen = 4,
	.setup = i9xx_setup,
	.cleanup = i9xx_cleanup,
	.write_entry = i965_write_entry,
	.dma_mask_size = 36,
	.check_flags = i830_check_flags,
	.chipset_flush = i9xx_chipset_flush,
};
static const struct intel_gtt_driver g4x_gtt_driver = {
	.gen = 5,
	.setup = i9xx_setup,
	.cleanup = i9xx_cleanup,
	.write_entry = i965_write_entry,
	.dma_mask_size = 36,
	.check_flags = i830_check_flags,
	.chipset_flush = i9xx_chipset_flush,
};
static const struct intel_gtt_driver ironlake_gtt_driver = {
	.gen = 5,
	.is_ironlake = 1,
	.setup = i9xx_setup,
	.cleanup = i9xx_cleanup,
	.write_entry = i965_write_entry,
	.dma_mask_size = 36,
	.check_flags = i830_check_flags,
	.chipset_flush = i9xx_chipset_flush,
};
static const struct intel_gtt_driver sandybridge_gtt_driver = {
	.gen = 6,
	.setup = i9xx_setup,
	.cleanup = gen6_cleanup,
	.write_entry = gen6_write_entry,
	.dma_mask_size = 40,
	.check_flags = gen6_check_flags,
	.chipset_flush = i9xx_chipset_flush,
};

/* Table to describe Intel GMCH and AGP/PCIE GART drivers.  At least one of
 * driver and gmch_driver must be non-null, and find_gmch will determine
 * which one should be used if a gmch_chip_id is present.
 */
static const struct intel_gtt_driver_description {
	unsigned int gmch_chip_id;
	char *name;
	const struct agp_bridge_driver *gmch_driver;
	const struct intel_gtt_driver *gtt_driver;
} intel_gtt_chipsets[] = {
	{ PCI_DEVICE_ID_INTEL_82810_IG1, "i810", &intel_810_driver,
		&i81x_gtt_driver},
	{ PCI_DEVICE_ID_INTEL_82810_IG3, "i810", &intel_810_driver,
		&i81x_gtt_driver},
	{ PCI_DEVICE_ID_INTEL_82810E_IG, "i810", &intel_810_driver,
		&i81x_gtt_driver},
	{ PCI_DEVICE_ID_INTEL_82815_CGC, "i815", &intel_810_driver,
		&i81x_gtt_driver},
	{ PCI_DEVICE_ID_INTEL_82830_CGC, "830M",
		&intel_fake_agp_driver, &i8xx_gtt_driver},
	{ PCI_DEVICE_ID_INTEL_82845G_IG, "830M",
		&intel_fake_agp_driver, &i8xx_gtt_driver},
	{ PCI_DEVICE_ID_INTEL_82854_IG, "854",
		&intel_fake_agp_driver, &i8xx_gtt_driver},
	{ PCI_DEVICE_ID_INTEL_82855GM_IG, "855GM",
		&intel_fake_agp_driver, &i8xx_gtt_driver},
	{ PCI_DEVICE_ID_INTEL_82865_IG, "865",
		&intel_fake_agp_driver, &i8xx_gtt_driver},
	{ PCI_DEVICE_ID_INTEL_E7221_IG, "E7221 (i915)",
		&intel_fake_agp_driver, &i915_gtt_driver },
	{ PCI_DEVICE_ID_INTEL_82915G_IG, "915G",
		&intel_fake_agp_driver, &i915_gtt_driver },
	{ PCI_DEVICE_ID_INTEL_82915GM_IG, "915GM",
		&intel_fake_agp_driver, &i915_gtt_driver },
	{ PCI_DEVICE_ID_INTEL_82945G_IG, "945G",
		&intel_fake_agp_driver, &i915_gtt_driver },
	{ PCI_DEVICE_ID_INTEL_82945GM_IG, "945GM",
		&intel_fake_agp_driver, &i915_gtt_driver },
	{ PCI_DEVICE_ID_INTEL_82945GME_IG, "945GME",
		&intel_fake_agp_driver, &i915_gtt_driver },
	{ PCI_DEVICE_ID_INTEL_82946GZ_IG, "946GZ",
		&intel_fake_agp_driver, &i965_gtt_driver },
	{ PCI_DEVICE_ID_INTEL_82G35_IG, "G35",
		&intel_fake_agp_driver, &i965_gtt_driver },
	{ PCI_DEVICE_ID_INTEL_82965Q_IG, "965Q",
		&intel_fake_agp_driver, &i965_gtt_driver },
	{ PCI_DEVICE_ID_INTEL_82965G_IG, "965G",
		&intel_fake_agp_driver, &i965_gtt_driver },
	{ PCI_DEVICE_ID_INTEL_82965GM_IG, "965GM",
		&intel_fake_agp_driver, &i965_gtt_driver },
	{ PCI_DEVICE_ID_INTEL_82965GME_IG, "965GME/GLE",
		&intel_fake_agp_driver, &i965_gtt_driver },
	{ PCI_DEVICE_ID_INTEL_G33_IG, "G33",
		&intel_fake_agp_driver, &g33_gtt_driver },
	{ PCI_DEVICE_ID_INTEL_Q35_IG, "Q35",
		&intel_fake_agp_driver, &g33_gtt_driver },
	{ PCI_DEVICE_ID_INTEL_Q33_IG, "Q33",
		&intel_fake_agp_driver, &g33_gtt_driver },
	{ PCI_DEVICE_ID_INTEL_PINEVIEW_M_IG, "GMA3150",
		&intel_fake_agp_driver, &pineview_gtt_driver },
	{ PCI_DEVICE_ID_INTEL_PINEVIEW_IG, "GMA3150",
		&intel_fake_agp_driver, &pineview_gtt_driver },
	{ PCI_DEVICE_ID_INTEL_GM45_IG, "GM45",
		&intel_fake_agp_driver, &g4x_gtt_driver },
	{ PCI_DEVICE_ID_INTEL_EAGLELAKE_IG, "Eaglelake",
		&intel_fake_agp_driver, &g4x_gtt_driver },
	{ PCI_DEVICE_ID_INTEL_Q45_IG, "Q45/Q43",
		&intel_fake_agp_driver, &g4x_gtt_driver },
	{ PCI_DEVICE_ID_INTEL_G45_IG, "G45/G43",
		&intel_fake_agp_driver, &g4x_gtt_driver },
	{ PCI_DEVICE_ID_INTEL_B43_IG, "B43",
		&intel_fake_agp_driver, &g4x_gtt_driver },
	{ PCI_DEVICE_ID_INTEL_B43_1_IG, "B43",
		&intel_fake_agp_driver, &g4x_gtt_driver },
	{ PCI_DEVICE_ID_INTEL_G41_IG, "G41",
		&intel_fake_agp_driver, &g4x_gtt_driver },
	{ PCI_DEVICE_ID_INTEL_IRONLAKE_D_IG,
	    "HD Graphics", &intel_fake_agp_driver, &ironlake_gtt_driver },
	{ PCI_DEVICE_ID_INTEL_IRONLAKE_M_IG,
	    "HD Graphics", &intel_fake_agp_driver, &ironlake_gtt_driver },
	{ PCI_DEVICE_ID_INTEL_SANDYBRIDGE_GT1_IG,
	    "Sandybridge", &intel_fake_agp_driver, &sandybridge_gtt_driver },
	{ PCI_DEVICE_ID_INTEL_SANDYBRIDGE_GT2_IG,
	    "Sandybridge", &intel_fake_agp_driver, &sandybridge_gtt_driver },
	{ PCI_DEVICE_ID_INTEL_SANDYBRIDGE_GT2_PLUS_IG,
	    "Sandybridge", &intel_fake_agp_driver, &sandybridge_gtt_driver },
	{ PCI_DEVICE_ID_INTEL_SANDYBRIDGE_M_GT1_IG,
	    "Sandybridge", &intel_fake_agp_driver, &sandybridge_gtt_driver },
	{ PCI_DEVICE_ID_INTEL_SANDYBRIDGE_M_GT2_IG,
	    "Sandybridge", &intel_fake_agp_driver, &sandybridge_gtt_driver },
	{ PCI_DEVICE_ID_INTEL_SANDYBRIDGE_M_GT2_PLUS_IG,
	    "Sandybridge", &intel_fake_agp_driver, &sandybridge_gtt_driver },
	{ PCI_DEVICE_ID_INTEL_SANDYBRIDGE_S_IG,
	    "Sandybridge", &intel_fake_agp_driver, &sandybridge_gtt_driver },
	{ 0, NULL, NULL }
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

int intel_gmch_probe(struct pci_dev *pdev,
				      struct agp_bridge_data *bridge)
{
	int i, mask;
	bridge->driver = NULL;

	for (i = 0; intel_gtt_chipsets[i].name != NULL; i++) {
		if (find_gmch(intel_gtt_chipsets[i].gmch_chip_id)) {
			bridge->driver =
				intel_gtt_chipsets[i].gmch_driver;
			intel_private.driver = 
				intel_gtt_chipsets[i].gtt_driver;
			break;
		}
	}

	if (!bridge->driver)
		return 0;

	bridge->dev_private_data = &intel_private;
	bridge->dev = pdev;

	intel_private.bridge_dev = pci_dev_get(pdev);

	dev_info(&pdev->dev, "Intel %s Chipset\n", intel_gtt_chipsets[i].name);

	mask = intel_private.driver->dma_mask_size;
	if (pci_set_dma_mask(intel_private.pcidev, DMA_BIT_MASK(mask)))
		dev_err(&intel_private.pcidev->dev,
			"set gfx device dma mask %d-bit failed!\n", mask);
	else
		pci_set_consistent_dma_mask(intel_private.pcidev,
					    DMA_BIT_MASK(mask));

	if (bridge->driver == &intel_810_driver)
		return 1;

	if (intel_gtt_init() != 0)
		return 0;

	return 1;
}
EXPORT_SYMBOL(intel_gmch_probe);

struct intel_gtt *intel_gtt_get(void)
{
	return &intel_private.base;
}
EXPORT_SYMBOL(intel_gtt_get);

void intel_gmch_remove(struct pci_dev *pdev)
{
	if (intel_private.pcidev)
		pci_dev_put(intel_private.pcidev);
	if (intel_private.bridge_dev)
		pci_dev_put(intel_private.bridge_dev);
}
EXPORT_SYMBOL(intel_gmch_remove);

MODULE_AUTHOR("Dave Jones <davej@redhat.com>");
MODULE_LICENSE("GPL and additional rights");

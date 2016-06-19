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
#include <linux/kernel.h>
#include <linux/pagemap.h>
#include <linux/agp_backend.h>
#include <linux/delay.h>
#include <asm/smp.h>
#include "agp.h"
#include "intel-agp.h"
#include <drm/intel-gtt.h>

/*
 * If we have Intel graphics, we're not going to have anything other than
 * an Intel IOMMU. So make the correct use of the PCI DMA API contingent
 * on the Intel IOMMU support (CONFIG_INTEL_IOMMU).
 * Only newer chipsets need to bother with this, of course.
 */
#ifdef CONFIG_INTEL_IOMMU
#define USE_PCI_DMA_API 1
#else
#define USE_PCI_DMA_API 0
#endif

struct intel_gtt_driver {
	unsigned int gen : 8;
	unsigned int is_g33 : 1;
	unsigned int is_pineview : 1;
	unsigned int is_ironlake : 1;
	unsigned int has_pgtbl_enable : 1;
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
	const struct intel_gtt_driver *driver;
	struct pci_dev *pcidev;	/* device one */
	struct pci_dev *bridge_dev;
	u8 __iomem *registers;
	phys_addr_t gtt_phys_addr;
	u32 PGETBL_save;
	u32 __iomem *gtt;		/* I915G */
	bool clear_fake_agp; /* on first access via agp, fill with scratch */
	int num_dcache_entries;
	void __iomem *i9xx_flush_page;
	char *i81x_gtt_table;
	struct resource ifp_resource;
	int resource_valid;
	struct page *scratch_page;
	phys_addr_t scratch_page_dma;
	int refcount;
	/* Whether i915 needs to use the dmar apis or not. */
	unsigned int needs_dmar : 1;
	phys_addr_t gma_bus_addr;
	/*  Size of memory reserved for graphics by the BIOS */
	unsigned int stolen_size;
	/* Total number of gtt entries. */
	unsigned int gtt_total_entries;
	/* Part of the gtt that is mappable by the cpu, for those chips where
	 * this is not the full gtt. */
	unsigned int gtt_mappable_entries;
} intel_private;

#define INTEL_GTT_GEN	intel_private.driver->gen
#define IS_G33		intel_private.driver->is_g33
#define IS_PINEVIEW	intel_private.driver->is_pineview
#define IS_IRONLAKE	intel_private.driver->is_ironlake
#define HAS_PGTBL_EN	intel_private.driver->has_pgtbl_enable

#if IS_ENABLED(CONFIG_AGP_INTEL)
static int intel_gtt_map_memory(struct page **pages,
				unsigned int num_entries,
				struct sg_table *st)
{
	struct scatterlist *sg;
	int i;

	DBG("try mapping %lu pages\n", (unsigned long)num_entries);

	if (sg_alloc_table(st, num_entries, GFP_KERNEL))
		goto err;

	for_each_sg(st->sgl, sg, num_entries, i)
		sg_set_page(sg, pages[i], PAGE_SIZE, 0);

	if (!pci_map_sg(intel_private.pcidev,
			st->sgl, st->nents, PCI_DMA_BIDIRECTIONAL))
		goto err;

	return 0;

err:
	sg_free_table(st);
	return -ENOMEM;
}

static void intel_gtt_unmap_memory(struct scatterlist *sg_list, int num_sg)
{
	struct sg_table st;
	DBG("try unmapping %lu pages\n", (unsigned long)mem->page_count);

	pci_unmap_sg(intel_private.pcidev, sg_list,
		     num_sg, PCI_DMA_BIDIRECTIONAL);

	st.sgl = sg_list;
	st.orig_nents = st.nents = num_sg;

	sg_free_table(&st);
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
	atomic_inc(&agp_bridge->current_memory_agp);
	return page;
}

static void i8xx_destroy_pages(struct page *page)
{
	if (page == NULL)
		return;

	set_pages_wb(page, 4);
	__free_pages(page, 2);
	atomic_dec(&agp_bridge->current_memory_agp);
}
#endif

#define I810_GTT_ORDER 4
static int i810_setup(void)
{
	phys_addr_t reg_addr;
	char *gtt_table;

	/* i81x does not preallocate the gtt. It's always 64kb in size. */
	gtt_table = alloc_gatt_pages(I810_GTT_ORDER);
	if (gtt_table == NULL)
		return -ENOMEM;
	intel_private.i81x_gtt_table = gtt_table;

	reg_addr = pci_resource_start(intel_private.pcidev, I810_MMADR_BAR);

	intel_private.registers = ioremap(reg_addr, KB(64));
	if (!intel_private.registers)
		return -ENOMEM;

	writel(virt_to_phys(gtt_table) | I810_PGETBL_ENABLED,
	       intel_private.registers+I810_PGETBL_CTL);

	intel_private.gtt_phys_addr = reg_addr + I810_PTE_BASE;

	if ((readl(intel_private.registers+I810_DRAM_CTL)
		& I810_DRAM_ROW_0) == I810_DRAM_ROW_0_SDRAM) {
		dev_info(&intel_private.pcidev->dev,
			 "detected 4MB dedicated video ram\n");
		intel_private.num_dcache_entries = 1024;
	}

	return 0;
}

static void i810_cleanup(void)
{
	writel(0, intel_private.registers+I810_PGETBL_CTL);
	free_gatt_pages(intel_private.i81x_gtt_table, I810_GTT_ORDER);
}

#if IS_ENABLED(CONFIG_AGP_INTEL)
static int i810_insert_dcache_entries(struct agp_memory *mem, off_t pg_start,
				      int type)
{
	int i;

	if ((pg_start + mem->page_count)
			> intel_private.num_dcache_entries)
		return -EINVAL;

	if (!mem->is_flushed)
		global_cache_flush();

	for (i = pg_start; i < (pg_start + mem->page_count); i++) {
		dma_addr_t addr = i << PAGE_SHIFT;
		intel_private.driver->write_entry(addr,
						  i, type);
	}
	wmb();

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
#endif

static int intel_gtt_setup_scratch_page(void)
{
	struct page *page;
	dma_addr_t dma_addr;

	page = alloc_page(GFP_KERNEL | GFP_DMA32 | __GFP_ZERO);
	if (page == NULL)
		return -ENOMEM;
	set_pages_uc(page, 1);

	if (intel_private.needs_dmar) {
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

static void i810_write_entry(dma_addr_t addr, unsigned int entry,
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

	writel_relaxed(addr | pte_flags, intel_private.gtt + entry);
}

static const struct aper_size_info_fixed intel_fake_agp_sizes[] = {
	{32, 8192, 3},
	{64, 16384, 4},
	{128, 32768, 5},
	{256, 65536, 6},
	{512, 131072, 7},
};

static unsigned int intel_gtt_stolen_size(void)
{
	u16 gmch_ctrl;
	u8 rdct;
	int local = 0;
	static const int ddt[4] = { 0, 16, 32, 64 };
	unsigned int stolen_size = 0;

	if (INTEL_GTT_GEN == 1)
		return 0; /* no stolen mem on i81x */

	pci_read_config_word(intel_private.bridge_dev,
			     I830_GMCH_CTRL, &gmch_ctrl);

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

	if (stolen_size > 0) {
		dev_info(&intel_private.bridge_dev->dev, "detected %dK %s memory\n",
		       stolen_size / KB(1), local ? "local" : "stolen");
	} else {
		dev_info(&intel_private.bridge_dev->dev,
		       "no pre-allocated video memory detected\n");
		stolen_size = 0;
	}

	return stolen_size;
}

static void i965_adjust_pgetbl_size(unsigned int size_flag)
{
	u32 pgetbl_ctl, pgetbl_ctl2;

	/* ensure that ppgtt is disabled */
	pgetbl_ctl2 = readl(intel_private.registers+I965_PGETBL_CTL2);
	pgetbl_ctl2 &= ~I810_PGETBL_ENABLED;
	writel(pgetbl_ctl2, intel_private.registers+I965_PGETBL_CTL2);

	/* write the new ggtt size */
	pgetbl_ctl = readl(intel_private.registers+I810_PGETBL_CTL);
	pgetbl_ctl &= ~I965_PGETBL_SIZE_MASK;
	pgetbl_ctl |= size_flag;
	writel(pgetbl_ctl, intel_private.registers+I810_PGETBL_CTL);
}

static unsigned int i965_gtt_total_entries(void)
{
	int size;
	u32 pgetbl_ctl;
	u16 gmch_ctl;

	pci_read_config_word(intel_private.bridge_dev,
			     I830_GMCH_CTRL, &gmch_ctl);

	if (INTEL_GTT_GEN == 5) {
		switch (gmch_ctl & G4x_GMCH_SIZE_MASK) {
		case G4x_GMCH_SIZE_1M:
		case G4x_GMCH_SIZE_VT_1M:
			i965_adjust_pgetbl_size(I965_PGETBL_SIZE_1MB);
			break;
		case G4x_GMCH_SIZE_VT_1_5M:
			i965_adjust_pgetbl_size(I965_PGETBL_SIZE_1_5MB);
			break;
		case G4x_GMCH_SIZE_2M:
		case G4x_GMCH_SIZE_VT_2M:
			i965_adjust_pgetbl_size(I965_PGETBL_SIZE_2MB);
			break;
		}
	}

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
	/* GTT pagetable sizes bigger than 512KB are not possible on G33! */
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
}

static unsigned int intel_gtt_total_entries(void)
{
	if (IS_G33 || INTEL_GTT_GEN == 4 || INTEL_GTT_GEN == 5)
		return i965_gtt_total_entries();
	else {
		/* On previous hardware, the GTT size was just what was
		 * required to map the aperture.
		 */
		return intel_private.gtt_mappable_entries;
	}
}

static unsigned int intel_gtt_mappable_entries(void)
{
	unsigned int aperture_size;

	if (INTEL_GTT_GEN == 1) {
		u32 smram_miscc;

		pci_read_config_dword(intel_private.bridge_dev,
				      I810_SMRAM_MISCC, &smram_miscc);

		if ((smram_miscc & I810_GFX_MEM_WIN_SIZE)
				== I810_GFX_MEM_WIN_32M)
			aperture_size = MB(32);
		else
			aperture_size = MB(64);
	} else if (INTEL_GTT_GEN == 2) {
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
	if (intel_private.needs_dmar)
		pci_unmap_page(intel_private.pcidev,
			       intel_private.scratch_page_dma,
			       PAGE_SIZE, PCI_DMA_BIDIRECTIONAL);
	__free_page(intel_private.scratch_page);
}

static void intel_gtt_cleanup(void)
{
	intel_private.driver->cleanup();

	iounmap(intel_private.gtt);
	iounmap(intel_private.registers);

	intel_gtt_teardown_scratch_page();
}

/* Certain Gen5 chipsets require require idling the GPU before
 * unmapping anything from the GTT when VT-d is enabled.
 */
static inline int needs_ilk_vtd_wa(void)
{
#ifdef CONFIG_INTEL_IOMMU
	const unsigned short gpu_devid = intel_private.pcidev->device;

	/* Query intel_iommu to see if we need the workaround. Presumably that
	 * was loaded first.
	 */
	if ((gpu_devid == PCI_DEVICE_ID_INTEL_IRONLAKE_D_IG ||
	     gpu_devid == PCI_DEVICE_ID_INTEL_IRONLAKE_M_IG) &&
	     intel_iommu_gfx_mapped)
		return 1;
#endif
	return 0;
}

static bool intel_gtt_can_wc(void)
{
	if (INTEL_GTT_GEN <= 2)
		return false;

	if (INTEL_GTT_GEN >= 6)
		return false;

	/* Reports of major corruption with ILK vt'd enabled */
	if (needs_ilk_vtd_wa())
		return false;

	return true;
}

static int intel_gtt_init(void)
{
	u32 gtt_map_size;
	int ret, bar;

	ret = intel_private.driver->setup();
	if (ret != 0)
		return ret;

	intel_private.gtt_mappable_entries = intel_gtt_mappable_entries();
	intel_private.gtt_total_entries = intel_gtt_total_entries();

	/* save the PGETBL reg for resume */
	intel_private.PGETBL_save =
		readl(intel_private.registers+I810_PGETBL_CTL)
			& ~I810_PGETBL_ENABLED;
	/* we only ever restore the register when enabling the PGTBL... */
	if (HAS_PGTBL_EN)
		intel_private.PGETBL_save |= I810_PGETBL_ENABLED;

	dev_info(&intel_private.bridge_dev->dev,
			"detected gtt size: %dK total, %dK mappable\n",
			intel_private.gtt_total_entries * 4,
			intel_private.gtt_mappable_entries * 4);

	gtt_map_size = intel_private.gtt_total_entries * 4;

	intel_private.gtt = NULL;
	if (intel_gtt_can_wc())
		intel_private.gtt = ioremap_wc(intel_private.gtt_phys_addr,
					       gtt_map_size);
	if (intel_private.gtt == NULL)
		intel_private.gtt = ioremap(intel_private.gtt_phys_addr,
					    gtt_map_size);
	if (intel_private.gtt == NULL) {
		intel_private.driver->cleanup();
		iounmap(intel_private.registers);
		return -ENOMEM;
	}

#if IS_ENABLED(CONFIG_AGP_INTEL)
	global_cache_flush();   /* FIXME: ? */
#endif

	intel_private.stolen_size = intel_gtt_stolen_size();

	intel_private.needs_dmar = USE_PCI_DMA_API && INTEL_GTT_GEN > 2;

	ret = intel_gtt_setup_scratch_page();
	if (ret != 0) {
		intel_gtt_cleanup();
		return ret;
	}

	if (INTEL_GTT_GEN <= 2)
		bar = I810_GMADR_BAR;
	else
		bar = I915_GMADR_BAR;

	intel_private.gma_bus_addr = pci_bus_address(intel_private.pcidev, bar);
	return 0;
}

#if IS_ENABLED(CONFIG_AGP_INTEL)
static int intel_fake_agp_fetch_size(void)
{
	int num_sizes = ARRAY_SIZE(intel_fake_agp_sizes);
	unsigned int aper_size;
	int i;

	aper_size = (intel_private.gtt_mappable_entries << PAGE_SHIFT) / MB(1);

	for (i = 0; i < num_sizes; i++) {
		if (aper_size == intel_fake_agp_sizes[i].size) {
			agp_bridge->current_size =
				(void *) (intel_fake_agp_sizes + i);
			return aper_size;
		}
	}

	return 0;
}
#endif

static void i830_cleanup(void)
{
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
	unsigned long timeout = jiffies + msecs_to_jiffies(1000);

	/* Forcibly evict everything from the CPU write buffers.
	 * clflush appears to be insufficient.
	 */
	wbinvd_on_all_cpus();

	/* Now we've only seen documents for this magic bit on 855GM,
	 * we hope it exists for the other gen2 chipsets...
	 *
	 * Also works as advertised on my 845G.
	 */
	writel(readl(intel_private.registers+I830_HIC) | (1<<31),
	       intel_private.registers+I830_HIC);

	while (readl(intel_private.registers+I830_HIC) & (1<<31)) {
		if (time_after(jiffies, timeout))
			break;

		udelay(50);
	}
}

static void i830_write_entry(dma_addr_t addr, unsigned int entry,
			     unsigned int flags)
{
	u32 pte_flags = I810_PTE_VALID;

	if (flags ==  AGP_USER_CACHED_MEMORY)
		pte_flags |= I830_PTE_SYSTEM_CACHED;

	writel_relaxed(addr | pte_flags, intel_private.gtt + entry);
}

bool intel_enable_gtt(void)
{
	u8 __iomem *reg;

	if (INTEL_GTT_GEN == 2) {
		u16 gmch_ctrl;

		pci_read_config_word(intel_private.bridge_dev,
				     I830_GMCH_CTRL, &gmch_ctrl);
		gmch_ctrl |= I830_GMCH_ENABLED;
		pci_write_config_word(intel_private.bridge_dev,
				      I830_GMCH_CTRL, gmch_ctrl);

		pci_read_config_word(intel_private.bridge_dev,
				     I830_GMCH_CTRL, &gmch_ctrl);
		if ((gmch_ctrl & I830_GMCH_ENABLED) == 0) {
			dev_err(&intel_private.pcidev->dev,
				"failed to enable the GTT: GMCH_CTRL=%x\n",
				gmch_ctrl);
			return false;
		}
	}

	/* On the resume path we may be adjusting the PGTBL value, so
	 * be paranoid and flush all chipset write buffers...
	 */
	if (INTEL_GTT_GEN >= 3)
		writel(0, intel_private.registers+GFX_FLSH_CNTL);

	reg = intel_private.registers+I810_PGETBL_CTL;
	writel(intel_private.PGETBL_save, reg);
	if (HAS_PGTBL_EN && (readl(reg) & I810_PGETBL_ENABLED) == 0) {
		dev_err(&intel_private.pcidev->dev,
			"failed to enable the GTT: PGETBL=%x [expected %x]\n",
			readl(reg), intel_private.PGETBL_save);
		return false;
	}

	if (INTEL_GTT_GEN >= 3)
		writel(0, intel_private.registers+GFX_FLSH_CNTL);

	return true;
}
EXPORT_SYMBOL(intel_enable_gtt);

static int i830_setup(void)
{
	phys_addr_t reg_addr;

	reg_addr = pci_resource_start(intel_private.pcidev, I810_MMADR_BAR);

	intel_private.registers = ioremap(reg_addr, KB(64));
	if (!intel_private.registers)
		return -ENOMEM;

	intel_private.gtt_phys_addr = reg_addr + I810_PTE_BASE;

	return 0;
}

#if IS_ENABLED(CONFIG_AGP_INTEL)
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
	if (!intel_enable_gtt())
	    return -EIO;

	intel_private.clear_fake_agp = true;
	agp_bridge->gart_bus_addr = intel_private.gma_bus_addr;

	return 0;
}
#endif

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

void intel_gtt_insert_sg_entries(struct sg_table *st,
				 unsigned int pg_start,
				 unsigned int flags)
{
	struct scatterlist *sg;
	unsigned int len, m;
	int i, j;

	j = pg_start;

	/* sg may merge pages, but we have to separate
	 * per-page addr for GTT */
	for_each_sg(st->sgl, sg, st->nents, i) {
		len = sg_dma_len(sg) >> PAGE_SHIFT;
		for (m = 0; m < len; m++) {
			dma_addr_t addr = sg_dma_address(sg) + (m << PAGE_SHIFT);
			intel_private.driver->write_entry(addr, j, flags);
			j++;
		}
	}
	wmb();
}
EXPORT_SYMBOL(intel_gtt_insert_sg_entries);

#if IS_ENABLED(CONFIG_AGP_INTEL)
static void intel_gtt_insert_pages(unsigned int first_entry,
				   unsigned int num_entries,
				   struct page **pages,
				   unsigned int flags)
{
	int i, j;

	for (i = 0, j = first_entry; i < num_entries; i++, j++) {
		dma_addr_t addr = page_to_phys(pages[i]);
		intel_private.driver->write_entry(addr,
						  j, flags);
	}
	wmb();
}

static int intel_fake_agp_insert_entries(struct agp_memory *mem,
					 off_t pg_start, int type)
{
	int ret = -EINVAL;

	if (intel_private.clear_fake_agp) {
		int start = intel_private.stolen_size / PAGE_SIZE;
		int end = intel_private.gtt_mappable_entries;
		intel_gtt_clear_range(start, end - start);
		intel_private.clear_fake_agp = false;
	}

	if (INTEL_GTT_GEN == 1 && type == AGP_DCACHE_MEMORY)
		return i810_insert_dcache_entries(mem, pg_start, type);

	if (mem->page_count == 0)
		goto out;

	if (pg_start + mem->page_count > intel_private.gtt_total_entries)
		goto out_err;

	if (type != mem->type)
		goto out_err;

	if (!intel_private.driver->check_flags(type))
		goto out_err;

	if (!mem->is_flushed)
		global_cache_flush();

	if (intel_private.needs_dmar) {
		struct sg_table st;

		ret = intel_gtt_map_memory(mem->pages, mem->page_count, &st);
		if (ret != 0)
			return ret;

		intel_gtt_insert_sg_entries(&st, pg_start, type);
		mem->sg_list = st.sgl;
		mem->num_sg = st.nents;
	} else
		intel_gtt_insert_pages(pg_start, mem->page_count, mem->pages,
				       type);

out:
	ret = 0;
out_err:
	mem->is_flushed = true;
	return ret;
}
#endif

void intel_gtt_clear_range(unsigned int first_entry, unsigned int num_entries)
{
	unsigned int i;

	for (i = first_entry; i < (first_entry + num_entries); i++) {
		intel_private.driver->write_entry(intel_private.scratch_page_dma,
						  i, 0);
	}
	wmb();
}
EXPORT_SYMBOL(intel_gtt_clear_range);

#if IS_ENABLED(CONFIG_AGP_INTEL)
static int intel_fake_agp_remove_entries(struct agp_memory *mem,
					 off_t pg_start, int type)
{
	if (mem->page_count == 0)
		return 0;

	intel_gtt_clear_range(pg_start, mem->page_count);

	if (intel_private.needs_dmar) {
		intel_gtt_unmap_memory(mem->sg_list, mem->num_sg);
		mem->sg_list = NULL;
		mem->num_sg = 0;
	}

	return 0;
}

static struct agp_memory *intel_fake_agp_alloc_by_type(size_t pg_count,
						       int type)
{
	struct agp_memory *new;

	if (type == AGP_DCACHE_MEMORY && INTEL_GTT_GEN == 1) {
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
	/* always return NULL for other allocation types for now */
	return NULL;
}
#endif

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

static void i965_write_entry(dma_addr_t addr,
			     unsigned int entry,
			     unsigned int flags)
{
	u32 pte_flags;

	pte_flags = I810_PTE_VALID;
	if (flags == AGP_USER_CACHED_MEMORY)
		pte_flags |= I830_PTE_SYSTEM_CACHED;

	/* Shift high bits down */
	addr |= (addr >> 28) & 0xf0;
	writel_relaxed(addr | pte_flags, intel_private.gtt + entry);
}

static int i9xx_setup(void)
{
	phys_addr_t reg_addr;
	int size = KB(512);

	reg_addr = pci_resource_start(intel_private.pcidev, I915_MMADR_BAR);

	intel_private.registers = ioremap(reg_addr, size);
	if (!intel_private.registers)
		return -ENOMEM;

	switch (INTEL_GTT_GEN) {
	case 3:
		intel_private.gtt_phys_addr =
			pci_resource_start(intel_private.pcidev, I915_PTE_BAR);
		break;
	case 5:
		intel_private.gtt_phys_addr = reg_addr + MB(2);
		break;
	default:
		intel_private.gtt_phys_addr = reg_addr + KB(512);
		break;
	}

	intel_i9xx_setup_flush();

	return 0;
}

#if IS_ENABLED(CONFIG_AGP_INTEL)
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
};
#endif

static const struct intel_gtt_driver i81x_gtt_driver = {
	.gen = 1,
	.has_pgtbl_enable = 1,
	.dma_mask_size = 32,
	.setup = i810_setup,
	.cleanup = i810_cleanup,
	.check_flags = i830_check_flags,
	.write_entry = i810_write_entry,
};
static const struct intel_gtt_driver i8xx_gtt_driver = {
	.gen = 2,
	.has_pgtbl_enable = 1,
	.setup = i830_setup,
	.cleanup = i830_cleanup,
	.write_entry = i830_write_entry,
	.dma_mask_size = 32,
	.check_flags = i830_check_flags,
	.chipset_flush = i830_chipset_flush,
};
static const struct intel_gtt_driver i915_gtt_driver = {
	.gen = 3,
	.has_pgtbl_enable = 1,
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
	.has_pgtbl_enable = 1,
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

/* Table to describe Intel GMCH and AGP/PCIE GART drivers.  At least one of
 * driver and gmch_driver must be non-null, and find_gmch will determine
 * which one should be used if a gmch_chip_id is present.
 */
static const struct intel_gtt_driver_description {
	unsigned int gmch_chip_id;
	char *name;
	const struct intel_gtt_driver *gtt_driver;
} intel_gtt_chipsets[] = {
	{ PCI_DEVICE_ID_INTEL_82810_IG1, "i810",
		&i81x_gtt_driver},
	{ PCI_DEVICE_ID_INTEL_82810_IG3, "i810",
		&i81x_gtt_driver},
	{ PCI_DEVICE_ID_INTEL_82810E_IG, "i810",
		&i81x_gtt_driver},
	{ PCI_DEVICE_ID_INTEL_82815_CGC, "i815",
		&i81x_gtt_driver},
	{ PCI_DEVICE_ID_INTEL_82830_CGC, "830M",
		&i8xx_gtt_driver},
	{ PCI_DEVICE_ID_INTEL_82845G_IG, "845G",
		&i8xx_gtt_driver},
	{ PCI_DEVICE_ID_INTEL_82854_IG, "854",
		&i8xx_gtt_driver},
	{ PCI_DEVICE_ID_INTEL_82855GM_IG, "855GM",
		&i8xx_gtt_driver},
	{ PCI_DEVICE_ID_INTEL_82865_IG, "865",
		&i8xx_gtt_driver},
	{ PCI_DEVICE_ID_INTEL_E7221_IG, "E7221 (i915)",
		&i915_gtt_driver },
	{ PCI_DEVICE_ID_INTEL_82915G_IG, "915G",
		&i915_gtt_driver },
	{ PCI_DEVICE_ID_INTEL_82915GM_IG, "915GM",
		&i915_gtt_driver },
	{ PCI_DEVICE_ID_INTEL_82945G_IG, "945G",
		&i915_gtt_driver },
	{ PCI_DEVICE_ID_INTEL_82945GM_IG, "945GM",
		&i915_gtt_driver },
	{ PCI_DEVICE_ID_INTEL_82945GME_IG, "945GME",
		&i915_gtt_driver },
	{ PCI_DEVICE_ID_INTEL_82946GZ_IG, "946GZ",
		&i965_gtt_driver },
	{ PCI_DEVICE_ID_INTEL_82G35_IG, "G35",
		&i965_gtt_driver },
	{ PCI_DEVICE_ID_INTEL_82965Q_IG, "965Q",
		&i965_gtt_driver },
	{ PCI_DEVICE_ID_INTEL_82965G_IG, "965G",
		&i965_gtt_driver },
	{ PCI_DEVICE_ID_INTEL_82965GM_IG, "965GM",
		&i965_gtt_driver },
	{ PCI_DEVICE_ID_INTEL_82965GME_IG, "965GME/GLE",
		&i965_gtt_driver },
	{ PCI_DEVICE_ID_INTEL_G33_IG, "G33",
		&g33_gtt_driver },
	{ PCI_DEVICE_ID_INTEL_Q35_IG, "Q35",
		&g33_gtt_driver },
	{ PCI_DEVICE_ID_INTEL_Q33_IG, "Q33",
		&g33_gtt_driver },
	{ PCI_DEVICE_ID_INTEL_PINEVIEW_M_IG, "GMA3150",
		&pineview_gtt_driver },
	{ PCI_DEVICE_ID_INTEL_PINEVIEW_IG, "GMA3150",
		&pineview_gtt_driver },
	{ PCI_DEVICE_ID_INTEL_GM45_IG, "GM45",
		&g4x_gtt_driver },
	{ PCI_DEVICE_ID_INTEL_EAGLELAKE_IG, "Eaglelake",
		&g4x_gtt_driver },
	{ PCI_DEVICE_ID_INTEL_Q45_IG, "Q45/Q43",
		&g4x_gtt_driver },
	{ PCI_DEVICE_ID_INTEL_G45_IG, "G45/G43",
		&g4x_gtt_driver },
	{ PCI_DEVICE_ID_INTEL_B43_IG, "B43",
		&g4x_gtt_driver },
	{ PCI_DEVICE_ID_INTEL_B43_1_IG, "B43",
		&g4x_gtt_driver },
	{ PCI_DEVICE_ID_INTEL_G41_IG, "G41",
		&g4x_gtt_driver },
	{ PCI_DEVICE_ID_INTEL_IRONLAKE_D_IG,
	    "HD Graphics", &ironlake_gtt_driver },
	{ PCI_DEVICE_ID_INTEL_IRONLAKE_M_IG,
	    "HD Graphics", &ironlake_gtt_driver },
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

int intel_gmch_probe(struct pci_dev *bridge_pdev, struct pci_dev *gpu_pdev,
		     struct agp_bridge_data *bridge)
{
	int i, mask;

	for (i = 0; intel_gtt_chipsets[i].name != NULL; i++) {
		if (gpu_pdev) {
			if (gpu_pdev->device ==
			    intel_gtt_chipsets[i].gmch_chip_id) {
				intel_private.pcidev = pci_dev_get(gpu_pdev);
				intel_private.driver =
					intel_gtt_chipsets[i].gtt_driver;

				break;
			}
		} else if (find_gmch(intel_gtt_chipsets[i].gmch_chip_id)) {
			intel_private.driver =
				intel_gtt_chipsets[i].gtt_driver;
			break;
		}
	}

	if (!intel_private.driver)
		return 0;

#if IS_ENABLED(CONFIG_AGP_INTEL)
	if (bridge) {
		if (INTEL_GTT_GEN > 1)
			return 0;

		bridge->driver = &intel_fake_agp_driver;
		bridge->dev_private_data = &intel_private;
		bridge->dev = bridge_pdev;
	}
#endif


	/*
	 * Can be called from the fake agp driver but also directly from
	 * drm/i915.ko. Hence we need to check whether everything is set up
	 * already.
	 */
	if (intel_private.refcount++)
		return 1;

	intel_private.bridge_dev = pci_dev_get(bridge_pdev);

	dev_info(&bridge_pdev->dev, "Intel %s Chipset\n", intel_gtt_chipsets[i].name);

	mask = intel_private.driver->dma_mask_size;
	if (pci_set_dma_mask(intel_private.pcidev, DMA_BIT_MASK(mask)))
		dev_err(&intel_private.pcidev->dev,
			"set gfx device dma mask %d-bit failed!\n", mask);
	else
		pci_set_consistent_dma_mask(intel_private.pcidev,
					    DMA_BIT_MASK(mask));

	if (intel_gtt_init() != 0) {
		intel_gmch_remove();

		return 0;
	}

	return 1;
}
EXPORT_SYMBOL(intel_gmch_probe);

void intel_gtt_get(u64 *gtt_total, size_t *stolen_size,
		   phys_addr_t *mappable_base, u64 *mappable_end)
{
	*gtt_total = intel_private.gtt_total_entries << PAGE_SHIFT;
	*stolen_size = intel_private.stolen_size;
	*mappable_base = intel_private.gma_bus_addr;
	*mappable_end = intel_private.gtt_mappable_entries << PAGE_SHIFT;
}
EXPORT_SYMBOL(intel_gtt_get);

void intel_gtt_chipset_flush(void)
{
	if (intel_private.driver->chipset_flush)
		intel_private.driver->chipset_flush();
}
EXPORT_SYMBOL(intel_gtt_chipset_flush);

void intel_gmch_remove(void)
{
	if (--intel_private.refcount)
		return;

	if (intel_private.scratch_page)
		intel_gtt_teardown_scratch_page();
	if (intel_private.pcidev)
		pci_dev_put(intel_private.pcidev);
	if (intel_private.bridge_dev)
		pci_dev_put(intel_private.bridge_dev);
	intel_private.driver = NULL;
}
EXPORT_SYMBOL(intel_gmch_remove);

MODULE_AUTHOR("Dave Jones, Various @Intel");
MODULE_LICENSE("GPL and additional rights");

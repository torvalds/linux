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

/*
 * If we have Intel graphics, we're not going to have anything other than
 * an Intel IOMMU. So make the correct use of the PCI DMA API contingent
 * on the Intel IOMMU support (CONFIG_DMAR).
 * Only newer chipsets need to bother with this, of course.
 */
#ifdef CONFIG_DMAR
#define USE_PCI_DMA_API 1
#endif

/* Max amount of stolen space, anything above will be returned to Linux */
int intel_max_stolen = 32 * 1024 * 1024;
EXPORT_SYMBOL(intel_max_stolen);

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

static struct gatt_mask intel_gen6_masks[] =
{
	{.mask = I810_PTE_VALID | GEN6_PTE_UNCACHED,
	 .type = INTEL_AGP_UNCACHED_MEMORY },
	{.mask = I810_PTE_VALID | GEN6_PTE_LLC,
         .type = INTEL_AGP_CACHED_MEMORY_LLC },
	{.mask = I810_PTE_VALID | GEN6_PTE_LLC | GEN6_PTE_GFDT,
         .type = INTEL_AGP_CACHED_MEMORY_LLC_GFDT },
	{.mask = I810_PTE_VALID | GEN6_PTE_LLC_MLC,
         .type = INTEL_AGP_CACHED_MEMORY_LLC_MLC },
	{.mask = I810_PTE_VALID | GEN6_PTE_LLC_MLC | GEN6_PTE_GFDT,
         .type = INTEL_AGP_CACHED_MEMORY_LLC_MLC_GFDT },
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
	int gtt_total_size;
	union {
		void __iomem *i9xx_flush_page;
		void *i8xx_flush_page;
	};
	struct page *i8xx_page;
	struct resource ifp_resource;
	int resource_valid;
} intel_private;

#ifdef USE_PCI_DMA_API
static int intel_agp_map_page(struct page *page, dma_addr_t *ret)
{
	*ret = pci_map_page(intel_private.pcidev, page, 0,
			    PAGE_SIZE, PCI_DMA_BIDIRECTIONAL);
	if (pci_dma_mapping_error(intel_private.pcidev, *ret))
		return -EINVAL;
	return 0;
}

static void intel_agp_unmap_page(struct page *page, dma_addr_t dma)
{
	pci_unmap_page(intel_private.pcidev, dma,
		       PAGE_SIZE, PCI_DMA_BIDIRECTIONAL);
}

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

static void intel_agp_insert_sg_entries(struct agp_memory *mem,
					off_t pg_start, int mask_type)
{
	struct scatterlist *sg;
	int i, j;

	j = pg_start;

	WARN_ON(!mem->num_sg);

	if (mem->num_sg == mem->page_count) {
		for_each_sg(mem->sg_list, sg, mem->page_count, i) {
			writel(agp_bridge->driver->mask_memory(agp_bridge,
					sg_dma_address(sg), mask_type),
					intel_private.gtt+j);
			j++;
		}
	} else {
		/* sg may merge pages, but we have to separate
		 * per-page addr for GTT */
		unsigned int len, m;

		for_each_sg(mem->sg_list, sg, mem->num_sg, i) {
			len = sg_dma_len(sg) / PAGE_SIZE;
			for (m = 0; m < len; m++) {
				writel(agp_bridge->driver->mask_memory(agp_bridge,
								       sg_dma_address(sg) + m * PAGE_SIZE,
								       mask_type),
				       intel_private.gtt+j);
				j++;
			}
		}
	}
	readl(intel_private.gtt+j-1);
}

#else

static void intel_agp_insert_sg_entries(struct agp_memory *mem,
					off_t pg_start, int mask_type)
{
	int i, j;

	for (i = 0, j = pg_start; i < mem->page_count; i++, j++) {
		writel(agp_bridge->driver->mask_memory(agp_bridge,
				page_to_phys(mem->pages[i]), mask_type),
		       intel_private.gtt+j);
	}

	readl(intel_private.gtt+j-1);
}

#endif

static int intel_i810_fetch_size(void)
{
	u32 smram_miscc;
	struct aper_size_info_fixed *values;

	pci_read_config_dword(agp_bridge->dev, I810_SMRAM_MISCC, &smram_miscc);
	values = A_SIZE_FIX(agp_bridge->driver->aperture_sizes);

	if ((smram_miscc & I810_GMS) == I810_GMS_DISABLE) {
		dev_warn(&agp_bridge->dev->dev, "i810 is disabled\n");
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

static void intel_i810_agp_enable(struct agp_bridge_data *bridge, u32 mode)
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

static int intel_gen6_type_to_mask_type(struct agp_bridge_data *bridge,
					int type)
{
	unsigned int type_mask = type & ~AGP_USER_CACHED_MEMORY_GFDT;
	unsigned int gfdt = type & AGP_USER_CACHED_MEMORY_GFDT;

	if (type_mask == AGP_USER_UNCACHED_MEMORY)
		return INTEL_AGP_UNCACHED_MEMORY;
	else if (type_mask == AGP_USER_CACHED_MEMORY_LLC_MLC)
		return gfdt ? INTEL_AGP_CACHED_MEMORY_LLC_MLC_GFDT :
			      INTEL_AGP_CACHED_MEMORY_LLC_MLC;
	else /* set 'normal'/'cached' to LLC by default */
		return gfdt ? INTEL_AGP_CACHED_MEMORY_LLC_GFDT :
			      INTEL_AGP_CACHED_MEMORY_LLC;
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
	int gtt_entries = 0;
	u8 rdct;
	int local = 0;
	static const int ddt[4] = { 0, 16, 32, 64 };
	int size; /* reserved space (in kb) at the top of stolen memory */

	pci_read_config_word(agp_bridge->dev, I830_GMCH_CTRL, &gmch_ctrl);

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
		case I965_PGETBL_SIZE_1MB:
			size = 1024;
			break;
		case I965_PGETBL_SIZE_2MB:
			size = 2048;
			break;
		case I965_PGETBL_SIZE_1_5MB:
			size = 1024 + 512;
			break;
		default:
			dev_info(&intel_private.pcidev->dev,
				 "unknown page table size, assuming 512KB\n");
			size = 512;
		}
		size += 4; /* add in BIOS popup space */
	} else if (IS_G33 && !IS_PINEVIEW) {
	/* G33's GTT size defined in gmch_ctrl */
		switch (gmch_ctrl & G33_PGETBL_SIZE_MASK) {
		case G33_PGETBL_SIZE_1M:
			size = 1024;
			break;
		case G33_PGETBL_SIZE_2M:
			size = 2048;
			break;
		default:
			dev_info(&agp_bridge->dev->dev,
				 "unknown page table size 0x%x, assuming 512KB\n",
				(gmch_ctrl & G33_PGETBL_SIZE_MASK));
			size = 512;
		}
		size += 4;
	} else if (IS_G4X || IS_PINEVIEW) {
		/* On 4 series hardware, GTT stolen is separate from graphics
		 * stolen, ignore it in stolen gtt entries counting.  However,
		 * 4KB of the stolen memory doesn't get mapped to the GTT.
		 */
		size = 4;
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
	} else if (IS_SNB) {
		/*
		 * SandyBridge has new memory control reg at 0x50.w
		 */
		u16 snb_gmch_ctl;
		pci_read_config_word(intel_private.pcidev, SNB_GMCH_CTRL, &snb_gmch_ctl);
		switch (snb_gmch_ctl & SNB_GMCH_GMS_STOLEN_MASK) {
		case SNB_GMCH_GMS_STOLEN_32M:
			gtt_entries = MB(32) - KB(size);
			break;
		case SNB_GMCH_GMS_STOLEN_64M:
			gtt_entries = MB(64) - KB(size);
			break;
		case SNB_GMCH_GMS_STOLEN_96M:
			gtt_entries = MB(96) - KB(size);
			break;
		case SNB_GMCH_GMS_STOLEN_128M:
			gtt_entries = MB(128) - KB(size);
			break;
		case SNB_GMCH_GMS_STOLEN_160M:
			gtt_entries = MB(160) - KB(size);
			break;
		case SNB_GMCH_GMS_STOLEN_192M:
			gtt_entries = MB(192) - KB(size);
			break;
		case SNB_GMCH_GMS_STOLEN_224M:
			gtt_entries = MB(224) - KB(size);
			break;
		case SNB_GMCH_GMS_STOLEN_256M:
			gtt_entries = MB(256) - KB(size);
			break;
		case SNB_GMCH_GMS_STOLEN_288M:
			gtt_entries = MB(288) - KB(size);
			break;
		case SNB_GMCH_GMS_STOLEN_320M:
			gtt_entries = MB(320) - KB(size);
			break;
		case SNB_GMCH_GMS_STOLEN_352M:
			gtt_entries = MB(352) - KB(size);
			break;
		case SNB_GMCH_GMS_STOLEN_384M:
			gtt_entries = MB(384) - KB(size);
			break;
		case SNB_GMCH_GMS_STOLEN_416M:
			gtt_entries = MB(416) - KB(size);
			break;
		case SNB_GMCH_GMS_STOLEN_448M:
			gtt_entries = MB(448) - KB(size);
			break;
		case SNB_GMCH_GMS_STOLEN_480M:
			gtt_entries = MB(480) - KB(size);
			break;
		case SNB_GMCH_GMS_STOLEN_512M:
			gtt_entries = MB(512) - KB(size);
			break;
		}
	} else {
		switch (gmch_ctrl & I855_GMCH_GMS_MASK) {
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
			if (IS_I915 || IS_I965 || IS_G33 || IS_G4X)
				gtt_entries = MB(48) - KB(size);
			else
				gtt_entries = 0;
			break;
		case I915_GMCH_GMS_STOLEN_64M:
			/* Check it's really I915G */
			if (IS_I915 || IS_I965 || IS_G33 || IS_G4X)
				gtt_entries = MB(64) - KB(size);
			else
				gtt_entries = 0;
			break;
		case G33_GMCH_GMS_STOLEN_128M:
			if (IS_G33 || IS_I965 || IS_G4X)
				gtt_entries = MB(128) - KB(size);
			else
				gtt_entries = 0;
			break;
		case G33_GMCH_GMS_STOLEN_256M:
			if (IS_G33 || IS_I965 || IS_G4X)
				gtt_entries = MB(256) - KB(size);
			else
				gtt_entries = 0;
			break;
		case INTEL_GMCH_GMS_STOLEN_96M:
			if (IS_I965 || IS_G4X)
				gtt_entries = MB(96) - KB(size);
			else
				gtt_entries = 0;
			break;
		case INTEL_GMCH_GMS_STOLEN_160M:
			if (IS_I965 || IS_G4X)
				gtt_entries = MB(160) - KB(size);
			else
				gtt_entries = 0;
			break;
		case INTEL_GMCH_GMS_STOLEN_224M:
			if (IS_I965 || IS_G4X)
				gtt_entries = MB(224) - KB(size);
			else
				gtt_entries = 0;
			break;
		case INTEL_GMCH_GMS_STOLEN_352M:
			if (IS_I965 || IS_G4X)
				gtt_entries = MB(352) - KB(size);
			else
				gtt_entries = 0;
			break;
		default:
			gtt_entries = 0;
			break;
		}
	}
	if (!local && gtt_entries > intel_max_stolen) {
		dev_info(&agp_bridge->dev->dev,
			 "detected %dK stolen memory, trimming to %dK\n",
			 gtt_entries / KB(1), intel_max_stolen / KB(1));
		gtt_entries = intel_max_stolen / KB(4);
	} else if (gtt_entries > 0) {
		dev_info(&agp_bridge->dev->dev, "detected %dK %s memory\n",
		       gtt_entries / KB(1), local ? "local" : "stolen");
		gtt_entries /= KB(4);
	} else {
		dev_info(&agp_bridge->dev->dev,
		       "no pre-allocated video memory detected\n");
		gtt_entries = 0;
	}

	intel_private.gtt_entries = gtt_entries;
}

static void intel_i830_fini_flush(void)
{
	kunmap(intel_private.i8xx_page);
	intel_private.i8xx_flush_page = NULL;
	unmap_page_from_agp(intel_private.i8xx_page);

	__free_page(intel_private.i8xx_page);
	intel_private.i8xx_page = NULL;
}

static void intel_i830_setup_flush(void)
{
	/* return if we've already set the flush mechanism up */
	if (intel_private.i8xx_page)
		return;

	intel_private.i8xx_page = alloc_page(GFP_KERNEL | __GFP_ZERO | GFP_DMA32);
	if (!intel_private.i8xx_page)
		return;

	intel_private.i8xx_flush_page = kmap(intel_private.i8xx_page);
	if (!intel_private.i8xx_flush_page)
		intel_i830_fini_flush();
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
static void intel_i830_chipset_flush(struct agp_bridge_data *bridge)
{
	unsigned int *pg = intel_private.i8xx_flush_page;

	memset(pg, 0, 1024);

	if (cpu_has_clflush)
		clflush_cache_range(pg, 1024);
	else if (wbinvd_on_all_cpus() != 0)
		printk(KERN_ERR "Timed out waiting for cache flush.\n");
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

	pci_read_config_dword(intel_private.pcidev, I810_MMADDR, &temp);
	temp &= 0xfff80000;

	intel_private.registers = ioremap(temp, 128 * 4096);
	if (!intel_private.registers)
		return -ENOMEM;

	temp = readl(intel_private.registers+I810_PGETBL_CTL) & 0xfffff000;
	global_cache_flush();	/* FIXME: ?? */

	/* we have to call this as early as possible after the MMIO base address is known */
	intel_i830_init_gtt_entries();
	if (intel_private.gtt_entries == 0) {
		iounmap(intel_private.registers);
		return -ENOMEM;
	}

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
		agp_bridge->current_size = (void *) values;
		agp_bridge->aperture_size_idx = 0;
		return values[0].size;
	}

	pci_read_config_word(agp_bridge->dev, I830_GMCH_CTRL, &gmch_ctrl);

	if ((gmch_ctrl & I830_GMCH_MEM_MASK) == I830_GMCH_MEM_128M) {
		agp_bridge->current_size = (void *) values;
		agp_bridge->aperture_size_idx = 0;
		return values[0].size;
	} else {
		agp_bridge->current_size = (void *) (values + 1);
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

	pci_read_config_dword(intel_private.pcidev, I810_GMADDR, &temp);
	agp_bridge->gart_bus_addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);

	pci_read_config_word(agp_bridge->dev, I830_GMCH_CTRL, &gmch_ctrl);
	gmch_ctrl |= I830_GMCH_ENABLED;
	pci_write_config_word(agp_bridge->dev, I830_GMCH_CTRL, gmch_ctrl);

	writel(agp_bridge->gatt_bus_addr|I810_PGETBL_ENABLED, intel_private.registers+I810_PGETBL_CTL);
	readl(intel_private.registers+I810_PGETBL_CTL);	/* PCI Posting. */

	if (agp_bridge->driver->needs_scratch_page) {
		for (i = intel_private.gtt_entries; i < current_size->num_entries; i++) {
			writel(agp_bridge->scratch_page, intel_private.registers+I810_PTE_BASE+(i*4));
		}
		readl(intel_private.registers+I810_PTE_BASE+((i-1)*4));	/* PCI Posting. */
	}

	global_cache_flush();

	intel_i830_setup_flush();
	return 0;
}

static void intel_i830_cleanup(void)
{
	iounmap(intel_private.registers);
}

static int intel_i830_insert_entries(struct agp_memory *mem, off_t pg_start,
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

	if (pg_start < intel_private.gtt_entries) {
		dev_printk(KERN_DEBUG, &intel_private.pcidev->dev,
			   "pg_start == 0x%.8lx, intel_private.gtt_entries == 0x%.8x\n",
			   pg_start, intel_private.gtt_entries);

		dev_info(&intel_private.pcidev->dev,
			 "trying to insert into local/stolen memory\n");
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
				page_to_phys(mem->pages[i]), mask_type),
		       intel_private.registers+I810_PTE_BASE+(j*4));
	}
	readl(intel_private.registers+I810_PTE_BASE+((j-1)*4));

out:
	ret = 0;
out_err:
	mem->is_flushed = true;
	return ret;
}

static int intel_i830_remove_entries(struct agp_memory *mem, off_t pg_start,
				     int type)
{
	int i;

	if (mem->page_count == 0)
		return 0;

	if (pg_start < intel_private.gtt_entries) {
		dev_info(&intel_private.pcidev->dev,
			 "trying to disable local/stolen memory\n");
		return -EINVAL;
	}

	for (i = pg_start; i < (mem->page_count + pg_start); i++) {
		writel(agp_bridge->scratch_page, intel_private.registers+I810_PTE_BASE+(i*4));
	}
	readl(intel_private.registers+I810_PTE_BASE+((i-1)*4));

	return 0;
}

static struct agp_memory *intel_i830_alloc_by_type(size_t pg_count, int type)
{
	if (type == AGP_PHYS_MEMORY)
		return alloc_agpphysmem_i8xx(pg_count, type);
	/* always return NULL for other allocation types for now */
	return NULL;
}

static int intel_alloc_chipset_flush_resource(void)
{
	int ret;
	ret = pci_bus_alloc_resource(agp_bridge->dev->bus, &intel_private.ifp_resource, PAGE_SIZE,
				     PAGE_SIZE, PCIBIOS_MIN_MEM, 0,
				     pcibios_align_resource, agp_bridge->dev);

	return ret;
}

static void intel_i915_setup_chipset_flush(void)
{
	int ret;
	u32 temp;

	pci_read_config_dword(agp_bridge->dev, I915_IFPADDR, &temp);
	if (!(temp & 0x1)) {
		intel_alloc_chipset_flush_resource();
		intel_private.resource_valid = 1;
		pci_write_config_dword(agp_bridge->dev, I915_IFPADDR, (intel_private.ifp_resource.start & 0xffffffff) | 0x1);
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

	pci_read_config_dword(agp_bridge->dev, I965_IFPADDR + 4, &temp_hi);
	pci_read_config_dword(agp_bridge->dev, I965_IFPADDR, &temp_lo);

	if (!(temp_lo & 0x1)) {

		intel_alloc_chipset_flush_resource();

		intel_private.resource_valid = 1;
		pci_write_config_dword(agp_bridge->dev, I965_IFPADDR + 4,
			upper_32_bits(intel_private.ifp_resource.start));
		pci_write_config_dword(agp_bridge->dev, I965_IFPADDR, (intel_private.ifp_resource.start & 0xffffffff) | 0x1);
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

	if (IS_SNB)
		return;

	/* setup a resource for this object */
	intel_private.ifp_resource.name = "Intel Flush Page";
	intel_private.ifp_resource.flags = IORESOURCE_MEM;

	/* Setup chipset flush for 915 */
	if (IS_I965 || IS_G33 || IS_G4X) {
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

static int intel_i9xx_configure(void)
{
	struct aper_size_info_fixed *current_size;
	u32 temp;
	u16 gmch_ctrl;
	int i;

	current_size = A_SIZE_FIX(agp_bridge->current_size);

	pci_read_config_dword(intel_private.pcidev, I915_GMADDR, &temp);

	agp_bridge->gart_bus_addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);

	pci_read_config_word(agp_bridge->dev, I830_GMCH_CTRL, &gmch_ctrl);
	gmch_ctrl |= I830_GMCH_ENABLED;
	pci_write_config_word(agp_bridge->dev, I830_GMCH_CTRL, gmch_ctrl);

	writel(agp_bridge->gatt_bus_addr|I810_PGETBL_ENABLED, intel_private.registers+I810_PGETBL_CTL);
	readl(intel_private.registers+I810_PGETBL_CTL);	/* PCI Posting. */

	if (agp_bridge->driver->needs_scratch_page) {
		for (i = intel_private.gtt_entries; i < intel_private.gtt_total_size; i++) {
			writel(agp_bridge->scratch_page, intel_private.gtt+i);
		}
		readl(intel_private.gtt+i-1);	/* PCI Posting. */
	}

	global_cache_flush();

	intel_i9xx_setup_flush();

	return 0;
}

static void intel_i915_cleanup(void)
{
	if (intel_private.i9xx_flush_page)
		iounmap(intel_private.i9xx_flush_page);
	if (intel_private.resource_valid)
		release_resource(&intel_private.ifp_resource);
	intel_private.ifp_resource.start = 0;
	intel_private.resource_valid = 0;
	iounmap(intel_private.gtt);
	iounmap(intel_private.registers);
}

static void intel_i915_chipset_flush(struct agp_bridge_data *bridge)
{
	if (intel_private.i9xx_flush_page)
		writel(1, intel_private.i9xx_flush_page);
}

static int intel_i915_insert_entries(struct agp_memory *mem, off_t pg_start,
				     int type)
{
	int num_entries;
	void *temp;
	int ret = -EINVAL;
	int mask_type;

	if (mem->page_count == 0)
		goto out;

	temp = agp_bridge->current_size;
	num_entries = A_SIZE_FIX(temp)->num_entries;

	if (pg_start < intel_private.gtt_entries) {
		dev_printk(KERN_DEBUG, &intel_private.pcidev->dev,
			   "pg_start == 0x%.8lx, intel_private.gtt_entries == 0x%.8x\n",
			   pg_start, intel_private.gtt_entries);

		dev_info(&intel_private.pcidev->dev,
			 "trying to insert into local/stolen memory\n");
		goto out_err;
	}

	if ((pg_start + mem->page_count) > num_entries)
		goto out_err;

	/* The i915 can't check the GTT for entries since it's read only;
	 * depend on the caller to make the correct offset decisions.
	 */

	if (type != mem->type)
		goto out_err;

	mask_type = agp_bridge->driver->agp_type_to_mask_type(agp_bridge, type);

	if (!IS_SNB && mask_type != 0 && mask_type != AGP_PHYS_MEMORY &&
	    mask_type != INTEL_AGP_CACHED_MEMORY)
		goto out_err;

	if (!mem->is_flushed)
		global_cache_flush();

	intel_agp_insert_sg_entries(mem, pg_start, mask_type);

 out:
	ret = 0;
 out_err:
	mem->is_flushed = true;
	return ret;
}

static int intel_i915_remove_entries(struct agp_memory *mem, off_t pg_start,
				     int type)
{
	int i;

	if (mem->page_count == 0)
		return 0;

	if (pg_start < intel_private.gtt_entries) {
		dev_info(&intel_private.pcidev->dev,
			 "trying to disable local/stolen memory\n");
		return -EINVAL;
	}

	for (i = pg_start; i < (mem->page_count + pg_start); i++)
		writel(agp_bridge->scratch_page, intel_private.gtt+i);

	readl(intel_private.gtt+i-1);

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
			return aper_size;
		}
	}

	return 0;
}

static int intel_i915_get_gtt_size(void)
{
	int size;

	if (IS_G33) {
		u16 gmch_ctrl;

		/* G33's GTT size defined in gmch_ctrl */
		pci_read_config_word(agp_bridge->dev, I830_GMCH_CTRL, &gmch_ctrl);
		switch (gmch_ctrl & I830_GMCH_GMS_MASK) {
		case I830_GMCH_GMS_STOLEN_512:
			size = 512;
			break;
		case I830_GMCH_GMS_STOLEN_1024:
			size = 1024;
			break;
		case I830_GMCH_GMS_STOLEN_8192:
			size = 8*1024;
			break;
		default:
			dev_info(&agp_bridge->dev->dev,
				 "unknown page table size 0x%x, assuming 512KB\n",
				(gmch_ctrl & I830_GMCH_GMS_MASK));
			size = 512;
		}
	} else {
		/* On previous hardware, the GTT size was just what was
		 * required to map the aperture.
		 */
		size = agp_bridge->driver->fetch_size();
	}

	return KB(size);
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
	int gtt_map_size;

	size = agp_bridge->current_size;
	page_order = size->page_order;
	num_entries = size->num_entries;
	agp_bridge->gatt_table_real = NULL;

	pci_read_config_dword(intel_private.pcidev, I915_MMADDR, &temp);
	pci_read_config_dword(intel_private.pcidev, I915_PTEADDR, &temp2);

	gtt_map_size = intel_i915_get_gtt_size();

	intel_private.gtt = ioremap(temp2, gtt_map_size);
	if (!intel_private.gtt)
		return -ENOMEM;

	intel_private.gtt_total_size = gtt_map_size / 4;

	temp &= 0xfff80000;

	intel_private.registers = ioremap(temp, 128 * 4096);
	if (!intel_private.registers) {
		iounmap(intel_private.gtt);
		return -ENOMEM;
	}

	temp = readl(intel_private.registers+I810_PGETBL_CTL) & 0xfffff000;
	global_cache_flush();	/* FIXME: ? */

	/* we have to call this as early as possible after the MMIO base address is known */
	intel_i830_init_gtt_entries();
	if (intel_private.gtt_entries == 0) {
		iounmap(intel_private.gtt);
		iounmap(intel_private.registers);
		return -ENOMEM;
	}

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
					    dma_addr_t addr, int type)
{
	/* Shift high bits down */
	addr |= (addr >> 28) & 0xf0;

	/* Type checking must be done elsewhere */
	return addr | bridge->driver->masks[type].mask;
}

static unsigned long intel_gen6_mask_memory(struct agp_bridge_data *bridge,
					    dma_addr_t addr, int type)
{
	/* gen6 has bit11-4 for physical addr bit39-32 */
	addr |= (addr >> 28) & 0xff0;

	/* Type checking must be done elsewhere */
	return addr | bridge->driver->masks[type].mask;
}

static void intel_i965_get_gtt_range(int *gtt_offset, int *gtt_size)
{
	u16 snb_gmch_ctl;

	switch (agp_bridge->dev->device) {
	case PCI_DEVICE_ID_INTEL_GM45_HB:
	case PCI_DEVICE_ID_INTEL_EAGLELAKE_HB:
	case PCI_DEVICE_ID_INTEL_Q45_HB:
	case PCI_DEVICE_ID_INTEL_G45_HB:
	case PCI_DEVICE_ID_INTEL_G41_HB:
	case PCI_DEVICE_ID_INTEL_B43_HB:
	case PCI_DEVICE_ID_INTEL_IRONLAKE_D_HB:
	case PCI_DEVICE_ID_INTEL_IRONLAKE_M_HB:
	case PCI_DEVICE_ID_INTEL_IRONLAKE_MA_HB:
	case PCI_DEVICE_ID_INTEL_IRONLAKE_MC2_HB:
		*gtt_offset = *gtt_size = MB(2);
		break;
	case PCI_DEVICE_ID_INTEL_SANDYBRIDGE_HB:
	case PCI_DEVICE_ID_INTEL_SANDYBRIDGE_M_HB:
	case PCI_DEVICE_ID_INTEL_SANDYBRIDGE_S_HB:
		*gtt_offset = MB(2);

		pci_read_config_word(intel_private.pcidev, SNB_GMCH_CTRL, &snb_gmch_ctl);
		switch (snb_gmch_ctl & SNB_GTT_SIZE_MASK) {
		default:
		case SNB_GTT_SIZE_0M:
			printk(KERN_ERR "Bad GTT size mask: 0x%04x.\n", snb_gmch_ctl);
			*gtt_size = MB(0);
			break;
		case SNB_GTT_SIZE_1M:
			*gtt_size = MB(1);
			break;
		case SNB_GTT_SIZE_2M:
			*gtt_size = MB(2);
			break;
		}
		break;
	default:
		*gtt_offset = *gtt_size = KB(512);
	}
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
	int gtt_offset, gtt_size;

	size = agp_bridge->current_size;
	page_order = size->page_order;
	num_entries = size->num_entries;
	agp_bridge->gatt_table_real = NULL;

	pci_read_config_dword(intel_private.pcidev, I915_MMADDR, &temp);

	temp &= 0xfff00000;

	intel_i965_get_gtt_range(&gtt_offset, &gtt_size);

	intel_private.gtt = ioremap((temp + gtt_offset) , gtt_size);

	if (!intel_private.gtt)
		return -ENOMEM;

	intel_private.gtt_total_size = gtt_size / 4;

	intel_private.registers = ioremap(temp, 128 * 4096);
	if (!intel_private.registers) {
		iounmap(intel_private.gtt);
		return -ENOMEM;
	}

	temp = readl(intel_private.registers+I810_PGETBL_CTL) & 0xfffff000;
	global_cache_flush();   /* FIXME: ? */

	/* we have to call this as early as possible after the MMIO base address is known */
	intel_i830_init_gtt_entries();
	if (intel_private.gtt_entries == 0) {
		iounmap(intel_private.gtt);
		iounmap(intel_private.registers);
		return -ENOMEM;
	}

	agp_bridge->gatt_table = NULL;

	agp_bridge->gatt_bus_addr = temp;

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
	.agp_enable		= intel_i810_agp_enable,
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

static const struct agp_bridge_driver intel_830_driver = {
	.owner			= THIS_MODULE,
	.aperture_sizes		= intel_i830_sizes,
	.size_type		= FIXED_APER_SIZE,
	.num_aperture_sizes	= 4,
	.needs_scratch_page	= true,
	.configure		= intel_i830_configure,
	.fetch_size		= intel_i830_fetch_size,
	.cleanup		= intel_i830_cleanup,
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
	.agp_alloc_pages        = agp_generic_alloc_pages,
	.agp_destroy_page	= agp_generic_destroy_page,
	.agp_destroy_pages      = agp_generic_destroy_pages,
	.agp_type_to_mask_type  = intel_i830_type_to_mask_type,
	.chipset_flush		= intel_i830_chipset_flush,
};

static const struct agp_bridge_driver intel_915_driver = {
	.owner			= THIS_MODULE,
	.aperture_sizes		= intel_i830_sizes,
	.size_type		= FIXED_APER_SIZE,
	.num_aperture_sizes	= 4,
	.needs_scratch_page	= true,
	.configure		= intel_i9xx_configure,
	.fetch_size		= intel_i9xx_fetch_size,
	.cleanup		= intel_i915_cleanup,
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
	.agp_alloc_pages        = agp_generic_alloc_pages,
	.agp_destroy_page	= agp_generic_destroy_page,
	.agp_destroy_pages      = agp_generic_destroy_pages,
	.agp_type_to_mask_type  = intel_i830_type_to_mask_type,
	.chipset_flush		= intel_i915_chipset_flush,
#ifdef USE_PCI_DMA_API
	.agp_map_page		= intel_agp_map_page,
	.agp_unmap_page		= intel_agp_unmap_page,
	.agp_map_memory		= intel_agp_map_memory,
	.agp_unmap_memory	= intel_agp_unmap_memory,
#endif
};

static const struct agp_bridge_driver intel_i965_driver = {
	.owner			= THIS_MODULE,
	.aperture_sizes		= intel_i830_sizes,
	.size_type		= FIXED_APER_SIZE,
	.num_aperture_sizes	= 4,
	.needs_scratch_page	= true,
	.configure		= intel_i9xx_configure,
	.fetch_size		= intel_i9xx_fetch_size,
	.cleanup		= intel_i915_cleanup,
	.mask_memory		= intel_i965_mask_memory,
	.masks			= intel_i810_masks,
	.agp_enable		= intel_i810_agp_enable,
	.cache_flush		= global_cache_flush,
	.create_gatt_table	= intel_i965_create_gatt_table,
	.free_gatt_table	= intel_i830_free_gatt_table,
	.insert_memory		= intel_i915_insert_entries,
	.remove_memory		= intel_i915_remove_entries,
	.alloc_by_type		= intel_i830_alloc_by_type,
	.free_by_type		= intel_i810_free_by_type,
	.agp_alloc_page		= agp_generic_alloc_page,
	.agp_alloc_pages        = agp_generic_alloc_pages,
	.agp_destroy_page	= agp_generic_destroy_page,
	.agp_destroy_pages      = agp_generic_destroy_pages,
	.agp_type_to_mask_type	= intel_i830_type_to_mask_type,
	.chipset_flush		= intel_i915_chipset_flush,
#ifdef USE_PCI_DMA_API
	.agp_map_page		= intel_agp_map_page,
	.agp_unmap_page		= intel_agp_unmap_page,
	.agp_map_memory		= intel_agp_map_memory,
	.agp_unmap_memory	= intel_agp_unmap_memory,
#endif
};

static const struct agp_bridge_driver intel_gen6_driver = {
	.owner			= THIS_MODULE,
	.aperture_sizes		= intel_i830_sizes,
	.size_type		= FIXED_APER_SIZE,
	.num_aperture_sizes	= 4,
	.needs_scratch_page	= true,
	.configure		= intel_i9xx_configure,
	.fetch_size		= intel_i9xx_fetch_size,
	.cleanup		= intel_i915_cleanup,
	.mask_memory		= intel_gen6_mask_memory,
	.masks			= intel_gen6_masks,
	.agp_enable		= intel_i810_agp_enable,
	.cache_flush		= global_cache_flush,
	.create_gatt_table	= intel_i965_create_gatt_table,
	.free_gatt_table	= intel_i830_free_gatt_table,
	.insert_memory		= intel_i915_insert_entries,
	.remove_memory		= intel_i915_remove_entries,
	.alloc_by_type		= intel_i830_alloc_by_type,
	.free_by_type		= intel_i810_free_by_type,
	.agp_alloc_page		= agp_generic_alloc_page,
	.agp_alloc_pages        = agp_generic_alloc_pages,
	.agp_destroy_page	= agp_generic_destroy_page,
	.agp_destroy_pages      = agp_generic_destroy_pages,
	.agp_type_to_mask_type	= intel_gen6_type_to_mask_type,
	.chipset_flush		= intel_i915_chipset_flush,
#ifdef USE_PCI_DMA_API
	.agp_map_page		= intel_agp_map_page,
	.agp_unmap_page		= intel_agp_unmap_page,
	.agp_map_memory		= intel_agp_map_memory,
	.agp_unmap_memory	= intel_agp_unmap_memory,
#endif
};

static const struct agp_bridge_driver intel_g33_driver = {
	.owner			= THIS_MODULE,
	.aperture_sizes		= intel_i830_sizes,
	.size_type		= FIXED_APER_SIZE,
	.num_aperture_sizes	= 4,
	.needs_scratch_page	= true,
	.configure		= intel_i9xx_configure,
	.fetch_size		= intel_i9xx_fetch_size,
	.cleanup		= intel_i915_cleanup,
	.mask_memory		= intel_i965_mask_memory,
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
	.agp_alloc_pages        = agp_generic_alloc_pages,
	.agp_destroy_page	= agp_generic_destroy_page,
	.agp_destroy_pages      = agp_generic_destroy_pages,
	.agp_type_to_mask_type	= intel_i830_type_to_mask_type,
	.chipset_flush		= intel_i915_chipset_flush,
#ifdef USE_PCI_DMA_API
	.agp_map_page		= intel_agp_map_page,
	.agp_unmap_page		= intel_agp_unmap_page,
	.agp_map_memory		= intel_agp_map_memory,
	.agp_unmap_memory	= intel_agp_unmap_memory,
#endif
};

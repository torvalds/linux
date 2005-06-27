/*
 * For documentation on the i460 AGP interface, see Chapter 7 (AGP Subsystem) of
 * the "Intel 460GTX Chipset Software Developer's Manual":
 * http://developer.intel.com/design/itanium/downloads/24870401s.htm
 */
/*
 * 460GX support by Chris Ahna <christopher.j.ahna@intel.com>
 * Clean up & simplification by David Mosberger-Tang <davidm@hpl.hp.com>
 */
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/agp_backend.h>

#include "agp.h"

#define INTEL_I460_BAPBASE		0x98
#define INTEL_I460_GXBCTL		0xa0
#define INTEL_I460_AGPSIZ		0xa2
#define INTEL_I460_ATTBASE		0xfe200000
#define INTEL_I460_GATT_VALID		(1UL << 24)
#define INTEL_I460_GATT_COHERENT	(1UL << 25)

/*
 * The i460 can operate with large (4MB) pages, but there is no sane way to support this
 * within the current kernel/DRM environment, so we disable the relevant code for now.
 * See also comments in ia64_alloc_page()...
 */
#define I460_LARGE_IO_PAGES		0

#if I460_LARGE_IO_PAGES
# define I460_IO_PAGE_SHIFT		i460.io_page_shift
#else
# define I460_IO_PAGE_SHIFT		12
#endif

#define I460_IOPAGES_PER_KPAGE		(PAGE_SIZE >> I460_IO_PAGE_SHIFT)
#define I460_KPAGES_PER_IOPAGE		(1 << (I460_IO_PAGE_SHIFT - PAGE_SHIFT))
#define I460_SRAM_IO_DISABLE		(1 << 4)
#define I460_BAPBASE_ENABLE		(1 << 3)
#define I460_AGPSIZ_MASK		0x7
#define I460_4M_PS			(1 << 1)

/* Control bits for Out-Of-GART coherency and Burst Write Combining */
#define I460_GXBCTL_OOG		(1UL << 0)
#define I460_GXBCTL_BWC		(1UL << 2)

/*
 * gatt_table entries are 32-bits wide on the i460; the generic code ought to declare the
 * gatt_table and gatt_table_real pointers a "void *"...
 */
#define RD_GATT(index)		readl((u32 *) i460.gatt + (index))
#define WR_GATT(index, val)	writel((val), (u32 *) i460.gatt + (index))
/*
 * The 460 spec says we have to read the last location written to make sure that all
 * writes have taken effect
 */
#define WR_FLUSH_GATT(index)	RD_GATT(index)

#define log2(x)			ffz(~(x))

static struct {
	void *gatt;				/* ioremap'd GATT area */

	/* i460 supports multiple GART page sizes, so GART pageshift is dynamic: */
	u8 io_page_shift;

	/* BIOS configures chipset to one of 2 possible apbase values: */
	u8 dynamic_apbase;

	/* structure for tracking partial use of 4MB GART pages: */
	struct lp_desc {
		unsigned long *alloced_map;	/* bitmap of kernel-pages in use */
		int refcount;			/* number of kernel pages using the large page */
		u64 paddr;			/* physical address of large page */
	} *lp_desc;
} i460;

static struct aper_size_info_8 i460_sizes[3] =
{
	/*
	 * The 32GB aperture is only available with a 4M GART page size.  Due to the
	 * dynamic GART page size, we can't figure out page_order or num_entries until
	 * runtime.
	 */
	{32768, 0, 0, 4},
	{1024, 0, 0, 2},
	{256, 0, 0, 1}
};

static struct gatt_mask i460_masks[] =
{
	{
	  .mask = INTEL_I460_GATT_VALID | INTEL_I460_GATT_COHERENT,
	  .type = 0
	}
};

static int i460_fetch_size (void)
{
	int i;
	u8 temp;
	struct aper_size_info_8 *values;

	/* Determine the GART page size */
	pci_read_config_byte(agp_bridge->dev, INTEL_I460_GXBCTL, &temp);
	i460.io_page_shift = (temp & I460_4M_PS) ? 22 : 12;
	pr_debug("i460_fetch_size: io_page_shift=%d\n", i460.io_page_shift);

	if (i460.io_page_shift != I460_IO_PAGE_SHIFT) {
		printk(KERN_ERR PFX
		       "I/O (GART) page-size %ZuKB doesn't match expected size %ZuKB\n",
		       1UL << (i460.io_page_shift - 10), 1UL << (I460_IO_PAGE_SHIFT));
		return 0;
	}

	values = A_SIZE_8(agp_bridge->driver->aperture_sizes);

	pci_read_config_byte(agp_bridge->dev, INTEL_I460_AGPSIZ, &temp);

	/* Exit now if the IO drivers for the GART SRAMS are turned off */
	if (temp & I460_SRAM_IO_DISABLE) {
		printk(KERN_ERR PFX "GART SRAMS disabled on 460GX chipset\n");
		printk(KERN_ERR PFX "AGPGART operation not possible\n");
		return 0;
	}

	/* Make sure we don't try to create an 2 ^ 23 entry GATT */
	if ((i460.io_page_shift == 0) && ((temp & I460_AGPSIZ_MASK) == 4)) {
		printk(KERN_ERR PFX "We can't have a 32GB aperture with 4KB GART pages\n");
		return 0;
	}

	/* Determine the proper APBASE register */
	if (temp & I460_BAPBASE_ENABLE)
		i460.dynamic_apbase = INTEL_I460_BAPBASE;
	else
		i460.dynamic_apbase = AGP_APBASE;

	for (i = 0; i < agp_bridge->driver->num_aperture_sizes; i++) {
		/*
		 * Dynamically calculate the proper num_entries and page_order values for
		 * the define aperture sizes. Take care not to shift off the end of
		 * values[i].size.
		 */
		values[i].num_entries = (values[i].size << 8) >> (I460_IO_PAGE_SHIFT - 12);
		values[i].page_order = log2((sizeof(u32)*values[i].num_entries) >> PAGE_SHIFT);
	}

	for (i = 0; i < agp_bridge->driver->num_aperture_sizes; i++) {
		/* Neglect control bits when matching up size_value */
		if ((temp & I460_AGPSIZ_MASK) == values[i].size_value) {
			agp_bridge->previous_size = agp_bridge->current_size = (void *) (values + i);
			agp_bridge->aperture_size_idx = i;
			return values[i].size;
		}
	}

	return 0;
}

/* There isn't anything to do here since 460 has no GART TLB. */
static void i460_tlb_flush (struct agp_memory *mem)
{
	return;
}

/*
 * This utility function is needed to prevent corruption of the control bits
 * which are stored along with the aperture size in 460's AGPSIZ register
 */
static void i460_write_agpsiz (u8 size_value)
{
	u8 temp;

	pci_read_config_byte(agp_bridge->dev, INTEL_I460_AGPSIZ, &temp);
	pci_write_config_byte(agp_bridge->dev, INTEL_I460_AGPSIZ,
			      ((temp & ~I460_AGPSIZ_MASK) | size_value));
}

static void i460_cleanup (void)
{
	struct aper_size_info_8 *previous_size;

	previous_size = A_SIZE_8(agp_bridge->previous_size);
	i460_write_agpsiz(previous_size->size_value);

	if (I460_IO_PAGE_SHIFT > PAGE_SHIFT)
		kfree(i460.lp_desc);
}

static int i460_configure (void)
{
	union {
		u32 small[2];
		u64 large;
	} temp;
	size_t size;
	u8 scratch;
	struct aper_size_info_8 *current_size;

	temp.large = 0;

	current_size = A_SIZE_8(agp_bridge->current_size);
	i460_write_agpsiz(current_size->size_value);

	/*
	 * Do the necessary rigmarole to read all eight bytes of APBASE.
	 * This has to be done since the AGP aperture can be above 4GB on
	 * 460 based systems.
	 */
	pci_read_config_dword(agp_bridge->dev, i460.dynamic_apbase, &(temp.small[0]));
	pci_read_config_dword(agp_bridge->dev, i460.dynamic_apbase + 4, &(temp.small[1]));

	/* Clear BAR control bits */
	agp_bridge->gart_bus_addr = temp.large & ~((1UL << 3) - 1);

	pci_read_config_byte(agp_bridge->dev, INTEL_I460_GXBCTL, &scratch);
	pci_write_config_byte(agp_bridge->dev, INTEL_I460_GXBCTL,
			      (scratch & 0x02) | I460_GXBCTL_OOG | I460_GXBCTL_BWC);

	/*
	 * Initialize partial allocation trackers if a GART page is bigger than a kernel
	 * page.
	 */
	if (I460_IO_PAGE_SHIFT > PAGE_SHIFT) {
		size = current_size->num_entries * sizeof(i460.lp_desc[0]);
		i460.lp_desc = kmalloc(size, GFP_KERNEL);
		if (!i460.lp_desc)
			return -ENOMEM;
		memset(i460.lp_desc, 0, size);
	}
	return 0;
}

static int i460_create_gatt_table (struct agp_bridge_data *bridge)
{
	int page_order, num_entries, i;
	void *temp;

	/*
	 * Load up the fixed address of the GART SRAMS which hold our GATT table.
	 */
	temp = agp_bridge->current_size;
	page_order = A_SIZE_8(temp)->page_order;
	num_entries = A_SIZE_8(temp)->num_entries;

	i460.gatt = ioremap(INTEL_I460_ATTBASE, PAGE_SIZE << page_order);

	/* These are no good, the should be removed from the agp_bridge strucure... */
	agp_bridge->gatt_table_real = NULL;
	agp_bridge->gatt_table = NULL;
	agp_bridge->gatt_bus_addr = 0;

	for (i = 0; i < num_entries; ++i)
		WR_GATT(i, 0);
	WR_FLUSH_GATT(i - 1);
	return 0;
}

static int i460_free_gatt_table (struct agp_bridge_data *bridge)
{
	int num_entries, i;
	void *temp;

	temp = agp_bridge->current_size;

	num_entries = A_SIZE_8(temp)->num_entries;

	for (i = 0; i < num_entries; ++i)
		WR_GATT(i, 0);
	WR_FLUSH_GATT(num_entries - 1);

	iounmap(i460.gatt);
	return 0;
}

/*
 * The following functions are called when the I/O (GART) page size is smaller than
 * PAGE_SIZE.
 */

static int i460_insert_memory_small_io_page (struct agp_memory *mem,
				off_t pg_start, int type)
{
	unsigned long paddr, io_pg_start, io_page_size;
	int i, j, k, num_entries;
	void *temp;

	pr_debug("i460_insert_memory_small_io_page(mem=%p, pg_start=%ld, type=%d, paddr0=0x%lx)\n",
		 mem, pg_start, type, mem->memory[0]);

	io_pg_start = I460_IOPAGES_PER_KPAGE * pg_start;

	temp = agp_bridge->current_size;
	num_entries = A_SIZE_8(temp)->num_entries;

	if ((io_pg_start + I460_IOPAGES_PER_KPAGE * mem->page_count) > num_entries) {
		printk(KERN_ERR PFX "Looks like we're out of AGP memory\n");
		return -EINVAL;
	}

	j = io_pg_start;
	while (j < (io_pg_start + I460_IOPAGES_PER_KPAGE * mem->page_count)) {
		if (!PGE_EMPTY(agp_bridge, RD_GATT(j))) {
			pr_debug("i460_insert_memory_small_io_page: GATT[%d]=0x%x is busy\n",
				 j, RD_GATT(j));
			return -EBUSY;
		}
		j++;
	}

	io_page_size = 1UL << I460_IO_PAGE_SHIFT;
	for (i = 0, j = io_pg_start; i < mem->page_count; i++) {
		paddr = mem->memory[i];
		for (k = 0; k < I460_IOPAGES_PER_KPAGE; k++, j++, paddr += io_page_size)
			WR_GATT(j, agp_bridge->driver->mask_memory(agp_bridge,
				paddr, mem->type));
	}
	WR_FLUSH_GATT(j - 1);
	return 0;
}

static int i460_remove_memory_small_io_page(struct agp_memory *mem,
				off_t pg_start, int type)
{
	int i;

	pr_debug("i460_remove_memory_small_io_page(mem=%p, pg_start=%ld, type=%d)\n",
		 mem, pg_start, type);

	pg_start = I460_IOPAGES_PER_KPAGE * pg_start;

	for (i = pg_start; i < (pg_start + I460_IOPAGES_PER_KPAGE * mem->page_count); i++)
		WR_GATT(i, 0);
	WR_FLUSH_GATT(i - 1);
	return 0;
}

#if I460_LARGE_IO_PAGES

/*
 * These functions are called when the I/O (GART) page size exceeds PAGE_SIZE.
 *
 * This situation is interesting since AGP memory allocations that are smaller than a
 * single GART page are possible.  The i460.lp_desc array tracks partial allocation of the
 * large GART pages to work around this issue.
 *
 * i460.lp_desc[pg_num].refcount tracks the number of kernel pages in use within GART page
 * pg_num.  i460.lp_desc[pg_num].paddr is the physical address of the large page and
 * i460.lp_desc[pg_num].alloced_map is a bitmap of kernel pages that are in use (allocated).
 */

static int i460_alloc_large_page (struct lp_desc *lp)
{
	unsigned long order = I460_IO_PAGE_SHIFT - PAGE_SHIFT;
	size_t map_size;
	void *lpage;

	lpage = (void *) __get_free_pages(GFP_KERNEL, order);
	if (!lpage) {
		printk(KERN_ERR PFX "Couldn't alloc 4M GART page...\n");
		return -ENOMEM;
	}

	map_size = ((I460_KPAGES_PER_IOPAGE + BITS_PER_LONG - 1) & -BITS_PER_LONG)/8;
	lp->alloced_map = kmalloc(map_size, GFP_KERNEL);
	if (!lp->alloced_map) {
		free_pages((unsigned long) lpage, order);
		printk(KERN_ERR PFX "Out of memory, we're in trouble...\n");
		return -ENOMEM;
	}
	memset(lp->alloced_map, 0, map_size);

	lp->paddr = virt_to_gart(lpage);
	lp->refcount = 0;
	atomic_add(I460_KPAGES_PER_IOPAGE, &agp_bridge->current_memory_agp);
	return 0;
}

static void i460_free_large_page (struct lp_desc *lp)
{
	kfree(lp->alloced_map);
	lp->alloced_map = NULL;

	free_pages((unsigned long) gart_to_virt(lp->paddr), I460_IO_PAGE_SHIFT - PAGE_SHIFT);
	atomic_sub(I460_KPAGES_PER_IOPAGE, &agp_bridge->current_memory_agp);
}

static int i460_insert_memory_large_io_page (struct agp_memory *mem,
				off_t pg_start, int type)
{
	int i, start_offset, end_offset, idx, pg, num_entries;
	struct lp_desc *start, *end, *lp;
	void *temp;

	temp = agp_bridge->current_size;
	num_entries = A_SIZE_8(temp)->num_entries;

	/* Figure out what pg_start means in terms of our large GART pages */
	start	 	= &i460.lp_desc[pg_start / I460_KPAGES_PER_IOPAGE];
	end 		= &i460.lp_desc[(pg_start + mem->page_count - 1) / I460_KPAGES_PER_IOPAGE];
	start_offset 	= pg_start % I460_KPAGES_PER_IOPAGE;
	end_offset 	= (pg_start + mem->page_count - 1) % I460_KPAGES_PER_IOPAGE;

	if (end > i460.lp_desc + num_entries) {
		printk(KERN_ERR PFX "Looks like we're out of AGP memory\n");
		return -EINVAL;
	}

	/* Check if the requested region of the aperture is free */
	for (lp = start; lp <= end; ++lp) {
		if (!lp->alloced_map)
			continue;	/* OK, the entire large page is available... */

		for (idx = ((lp == start) ? start_offset : 0);
		     idx < ((lp == end) ? (end_offset + 1) : I460_KPAGES_PER_IOPAGE);
		     idx++)
		{
			if (test_bit(idx, lp->alloced_map))
				return -EBUSY;
		}
	}

	for (lp = start, i = 0; lp <= end; ++lp) {
		if (!lp->alloced_map) {
			/* Allocate new GART pages... */
			if (i460_alloc_large_page(lp) < 0)
				return -ENOMEM;
			pg = lp - i460.lp_desc;
			WR_GATT(pg, agp_bridge->driver->mask_memory(agp_bridge,
				lp->paddr, 0));
			WR_FLUSH_GATT(pg);
		}

		for (idx = ((lp == start) ? start_offset : 0);
		     idx < ((lp == end) ? (end_offset + 1) : I460_KPAGES_PER_IOPAGE);
		     idx++, i++)
		{
			mem->memory[i] = lp->paddr + idx*PAGE_SIZE;
			__set_bit(idx, lp->alloced_map);
			++lp->refcount;
		}
	}
	return 0;
}

static int i460_remove_memory_large_io_page (struct agp_memory *mem,
				off_t pg_start, int type)
{
	int i, pg, start_offset, end_offset, idx, num_entries;
	struct lp_desc *start, *end, *lp;
	void *temp;

	temp = agp_bridge->driver->current_size;
	num_entries = A_SIZE_8(temp)->num_entries;

	/* Figure out what pg_start means in terms of our large GART pages */
	start	 	= &i460.lp_desc[pg_start / I460_KPAGES_PER_IOPAGE];
	end 		= &i460.lp_desc[(pg_start + mem->page_count - 1) / I460_KPAGES_PER_IOPAGE];
	start_offset 	= pg_start % I460_KPAGES_PER_IOPAGE;
	end_offset 	= (pg_start + mem->page_count - 1) % I460_KPAGES_PER_IOPAGE;

	for (i = 0, lp = start; lp <= end; ++lp) {
		for (idx = ((lp == start) ? start_offset : 0);
		     idx < ((lp == end) ? (end_offset + 1) : I460_KPAGES_PER_IOPAGE);
		     idx++, i++)
		{
			mem->memory[i] = 0;
			__clear_bit(idx, lp->alloced_map);
			--lp->refcount;
		}

		/* Free GART pages if they are unused */
		if (lp->refcount == 0) {
			pg = lp - i460.lp_desc;
			WR_GATT(pg, 0);
			WR_FLUSH_GATT(pg);
			i460_free_large_page(lp);
		}
	}
	return 0;
}

/* Wrapper routines to call the approriate {small_io_page,large_io_page} function */

static int i460_insert_memory (struct agp_memory *mem,
				off_t pg_start, int type)
{
	if (I460_IO_PAGE_SHIFT <= PAGE_SHIFT)
		return i460_insert_memory_small_io_page(mem, pg_start, type);
	else
		return i460_insert_memory_large_io_page(mem, pg_start, type);
}

static int i460_remove_memory (struct agp_memory *mem,
				off_t pg_start, int type)
{
	if (I460_IO_PAGE_SHIFT <= PAGE_SHIFT)
		return i460_remove_memory_small_io_page(mem, pg_start, type);
	else
		return i460_remove_memory_large_io_page(mem, pg_start, type);
}

/*
 * If the I/O (GART) page size is bigger than the kernel page size, we don't want to
 * allocate memory until we know where it is to be bound in the aperture (a
 * multi-kernel-page alloc might fit inside of an already allocated GART page).
 *
 * Let's just hope nobody counts on the allocated AGP memory being there before bind time
 * (I don't think current drivers do)...
 */
static void *i460_alloc_page (struct agp_bridge_data *bridge)
{
	void *page;

	if (I460_IO_PAGE_SHIFT <= PAGE_SHIFT)
		page = agp_generic_alloc_page(agp_bridge);
	else
		/* Returning NULL would cause problems */
		/* AK: really dubious code. */
		page = (void *)~0UL;
	return page;
}

static void i460_destroy_page (void *page)
{
	if (I460_IO_PAGE_SHIFT <= PAGE_SHIFT)
		agp_generic_destroy_page(page);
}

#endif /* I460_LARGE_IO_PAGES */

static unsigned long i460_mask_memory (struct agp_bridge_data *bridge,
	unsigned long addr, int type)
{
	/* Make sure the returned address is a valid GATT entry */
	return bridge->driver->masks[0].mask
		| (((addr & ~((1 << I460_IO_PAGE_SHIFT) - 1)) & 0xffffff000) >> 12);
}

struct agp_bridge_driver intel_i460_driver = {
	.owner			= THIS_MODULE,
	.aperture_sizes		= i460_sizes,
	.size_type		= U8_APER_SIZE,
	.num_aperture_sizes	= 3,
	.configure		= i460_configure,
	.fetch_size		= i460_fetch_size,
	.cleanup		= i460_cleanup,
	.tlb_flush		= i460_tlb_flush,
	.mask_memory		= i460_mask_memory,
	.masks			= i460_masks,
	.agp_enable		= agp_generic_enable,
	.cache_flush		= global_cache_flush,
	.create_gatt_table	= i460_create_gatt_table,
	.free_gatt_table	= i460_free_gatt_table,
#if I460_LARGE_IO_PAGES
	.insert_memory		= i460_insert_memory,
	.remove_memory		= i460_remove_memory,
	.agp_alloc_page		= i460_alloc_page,
	.agp_destroy_page	= i460_destroy_page,
#else
	.insert_memory		= i460_insert_memory_small_io_page,
	.remove_memory		= i460_remove_memory_small_io_page,
	.agp_alloc_page		= agp_generic_alloc_page,
	.agp_destroy_page	= agp_generic_destroy_page,
#endif
	.alloc_by_type		= agp_generic_alloc_by_type,
	.free_by_type		= agp_generic_free_by_type,
	.cant_use_aperture	= 1,
};

static int __devinit agp_intel_i460_probe(struct pci_dev *pdev,
					  const struct pci_device_id *ent)
{
	struct agp_bridge_data *bridge;
	u8 cap_ptr;

	cap_ptr = pci_find_capability(pdev, PCI_CAP_ID_AGP);
	if (!cap_ptr)
		return -ENODEV;

	bridge = agp_alloc_bridge();
	if (!bridge)
		return -ENOMEM;

	bridge->driver = &intel_i460_driver;
	bridge->dev = pdev;
	bridge->capndx = cap_ptr;

	printk(KERN_INFO PFX "Detected Intel 460GX chipset\n");

	pci_set_drvdata(pdev, bridge);
	return agp_add_bridge(bridge);
}

static void __devexit agp_intel_i460_remove(struct pci_dev *pdev)
{
	struct agp_bridge_data *bridge = pci_get_drvdata(pdev);

	agp_remove_bridge(bridge);
	agp_put_bridge(bridge);
}

static struct pci_device_id agp_intel_i460_pci_table[] = {
	{
	.class		= (PCI_CLASS_BRIDGE_HOST << 8),
	.class_mask	= ~0,
	.vendor		= PCI_VENDOR_ID_INTEL,
	.device		= PCI_DEVICE_ID_INTEL_84460GX,
	.subvendor	= PCI_ANY_ID,
	.subdevice	= PCI_ANY_ID,
	},
	{ }
};

MODULE_DEVICE_TABLE(pci, agp_intel_i460_pci_table);

static struct pci_driver agp_intel_i460_pci_driver = {
	.name		= "agpgart-intel-i460",
	.id_table	= agp_intel_i460_pci_table,
	.probe		= agp_intel_i460_probe,
	.remove		= __devexit_p(agp_intel_i460_remove),
};

static int __init agp_intel_i460_init(void)
{
	if (agp_off)
		return -EINVAL;
	return pci_register_driver(&agp_intel_i460_pci_driver);
}

static void __exit agp_intel_i460_cleanup(void)
{
	pci_unregister_driver(&agp_intel_i460_pci_driver);
}

module_init(agp_intel_i460_init);
module_exit(agp_intel_i460_cleanup);

MODULE_AUTHOR("Chris Ahna <Christopher.J.Ahna@intel.com>");
MODULE_LICENSE("GPL and additional rights");

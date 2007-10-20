/*
 * arch/v850/kernel/mb_a_pci.c -- PCI support for Midas lab RTE-MOTHER-A board
 *
 *  Copyright (C) 2001,02,03,05  NEC Electronics Corporation
 *  Copyright (C) 2001,02,03,05  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/pci.h>

#include <asm/machdep.h>

/* __nomods_init is like __devinit, but is a no-op when modules are enabled.
   This is used by some routines that can be called either during boot
   or by a module.  */
#ifdef CONFIG_MODULES
#define __nomods_init /*nothing*/
#else
#define __nomods_init __devinit
#endif

/* PCI devices on the Mother-A board can only do DMA to/from the MB SRAM
   (the RTE-V850E/MA1-CB cpu board doesn't support PCI access to
   CPU-board memory), and since linux DMA buffers are allocated in
   normal kernel memory, we basically have to copy DMA blocks around
   (this is like a `bounce buffer').  When a DMA block is `mapped', we
   allocate an identically sized block in MB SRAM, and if we're doing
   output to the device, copy the CPU-memory block to the MB-SRAM block.
   When an active block is `unmapped', we will copy the block back to
   CPU memory if necessary, and then deallocate the MB SRAM block.
   Ack.  */

/* Where the motherboard SRAM is in the PCI-bus address space (the
   first 512K of it is also mapped at PCI address 0).  */
#define PCI_MB_SRAM_ADDR 0x800000

/* Convert CPU-view MB SRAM address to/from PCI-view addresses of the
   same memory.  */
#define MB_SRAM_TO_PCI(mb_sram_addr) \
   ((dma_addr_t)mb_sram_addr - MB_A_SRAM_ADDR + PCI_MB_SRAM_ADDR)
#define PCI_TO_MB_SRAM(pci_addr)     \
   (void *)(pci_addr - PCI_MB_SRAM_ADDR + MB_A_SRAM_ADDR)

static void pcibios_assign_resources (void);

struct mb_pci_dev_irq {
	unsigned dev;		/* PCI device number */
	unsigned irq_base;	/* First IRQ  */
	unsigned query_pin;	/* True if we should read the device's
				   Interrupt Pin info, and allocate
				   interrupt IRQ_BASE + PIN.  */
};

/* PCI interrupts are mapped statically to GBUS interrupts.  */
static struct mb_pci_dev_irq mb_pci_dev_irqs[] = {
	/* Motherboard SB82558 ethernet controller */
	{ 10,	IRQ_MB_A_LAN,		0 },
	/* PCI slot 1 */
	{ 8, 	IRQ_MB_A_PCI1(0),	1 },
	/* PCI slot 2 */
	{ 9, 	IRQ_MB_A_PCI2(0),	1 }
};
#define NUM_MB_PCI_DEV_IRQS ARRAY_SIZE(mb_pci_dev_irqs)


/* PCI configuration primitives.  */

#define CONFIG_DMCFGA(bus, devfn, offs)					\
   (0x80000000								\
    | ((offs) & ~0x3)							\
    | ((devfn) << 8)							\
    | ((bus)->number << 16))

static int
mb_pci_read (struct pci_bus *bus, unsigned devfn, int offs, int size, u32 *rval)
{
	u32 addr;
	int flags;

	local_irq_save (flags);

	MB_A_PCI_PCICR = 0x7;
	MB_A_PCI_DMCFGA = CONFIG_DMCFGA (bus, devfn, offs);

	addr = MB_A_PCI_IO_ADDR + (offs & 0x3);

	switch (size) {
	case 1:	*rval = *(volatile  u8 *)addr; break;
	case 2:	*rval = *(volatile u16 *)addr; break;
	case 4:	*rval = *(volatile u32 *)addr; break;
	}

        if (MB_A_PCI_PCISR & 0x2000) {
		MB_A_PCI_PCISR = 0x2000;
		*rval = ~0;
        }

	MB_A_PCI_DMCFGA = 0;

	local_irq_restore (flags);

	return PCIBIOS_SUCCESSFUL;
}

static int
mb_pci_write (struct pci_bus *bus, unsigned devfn, int offs, int size, u32 val)
{
	u32 addr;
	int flags;

	local_irq_save (flags);

	MB_A_PCI_PCICR = 0x7;
	MB_A_PCI_DMCFGA = CONFIG_DMCFGA (bus, devfn, offs);

	addr = MB_A_PCI_IO_ADDR + (offs & 0x3);

	switch (size) {
	case 1: *(volatile  u8 *)addr = val; break;
	case 2: *(volatile u16 *)addr = val; break;
	case 4: *(volatile u32 *)addr = val; break;
	}

        if (MB_A_PCI_PCISR & 0x2000)
		MB_A_PCI_PCISR = 0x2000;

	MB_A_PCI_DMCFGA = 0;

	local_irq_restore (flags);

	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops mb_pci_config_ops = {
	.read	= mb_pci_read,
	.write	= mb_pci_write,
};


/* PCI Initialization.  */

static struct pci_bus *mb_pci_bus = 0;

/* Do initial PCI setup.  */
static int __devinit pcibios_init (void)
{
	u32 id = MB_A_PCI_PCIHIDR;
	u16 vendor = id & 0xFFFF;
	u16 device = (id >> 16) & 0xFFFF;

	if (vendor == PCI_VENDOR_ID_PLX && device == PCI_DEVICE_ID_PLX_9080) {
		printk (KERN_INFO
			"PCI: PLX Technology PCI9080 HOST/PCI bridge\n");

		MB_A_PCI_PCICR = 0x147;

		MB_A_PCI_PCIBAR0 = 0x007FFF00;
		MB_A_PCI_PCIBAR1 = 0x0000FF00;
		MB_A_PCI_PCIBAR2 = 0x00800000;

		MB_A_PCI_PCILTR = 0x20;

		MB_A_PCI_PCIPBAM |= 0x3;

		MB_A_PCI_PCISR =  ~0; /* Clear errors.  */

		/* Reprogram the motherboard's IO/config address space,
		   as we don't support the GCS7 address space that the
		   default uses.  */

		/* Significant address bits used for decoding PCI GCS5 space
		   accesses.  */
		MB_A_PCI_DMRR = ~(MB_A_PCI_MEM_SIZE - 1);

		/* I don't understand this, but the SolutionGear example code
		   uses such an offset, and it doesn't work without it.  XXX */
#if GCS5_SIZE == 0x00800000
#define GCS5_CFG_OFFS 0x00800000
#else
#define GCS5_CFG_OFFS 0
#endif

		/* Address bit values for matching.  Note that we have to give
		   the address from the motherboard's point of view, which is
		   different than the CPU's.  */
		/* PCI memory space.  */
		MB_A_PCI_DMLBAM = GCS5_CFG_OFFS + 0x0;
		/* PCI I/O space.  */
		MB_A_PCI_DMLBAI =
			GCS5_CFG_OFFS + (MB_A_PCI_IO_ADDR - GCS5_ADDR);

		mb_pci_bus = pci_scan_bus (0, &mb_pci_config_ops, 0);

		pcibios_assign_resources ();
	} else
		printk (KERN_ERR "PCI: HOST/PCI bridge not found\n");

	return 0;
}

subsys_initcall (pcibios_init);

char __devinit *pcibios_setup (char *option)
{
	/* Don't handle any options. */
	return option;
}


int __nomods_init pcibios_enable_device (struct pci_dev *dev, int mask)
{
	u16 cmd, old_cmd;
	int idx;
	struct resource *r;

	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	old_cmd = cmd;
	for (idx = 0; idx < 6; idx++) {
		r = &dev->resource[idx];
		if (!r->start && r->end) {
			printk(KERN_ERR "PCI: Device %s not available because "
			       "of resource collisions\n", pci_name(dev));
			return -EINVAL;
		}
		if (r->flags & IORESOURCE_IO)
			cmd |= PCI_COMMAND_IO;
		if (r->flags & IORESOURCE_MEM)
			cmd |= PCI_COMMAND_MEMORY;
	}
	if (cmd != old_cmd) {
		printk("PCI: Enabling device %s (%04x -> %04x)\n",
		       pci_name(dev), old_cmd, cmd);
		pci_write_config_word(dev, PCI_COMMAND, cmd);
	}
	return 0;
}


/* Resource allocation.  */
static void __devinit pcibios_assign_resources (void)
{
	struct pci_dev *dev = NULL;
	struct resource *r;

	for_each_pci_dev(dev) {
		unsigned di_num;
		unsigned class = dev->class >> 8;

		if (class && class != PCI_CLASS_BRIDGE_HOST) {
			unsigned r_num;
			for(r_num = 0; r_num < 6; r_num++) {
				r = &dev->resource[r_num];
				if (!r->start && r->end)
					pci_assign_resource (dev, r_num);
			}
		}

		/* Assign interrupts.  */
		for (di_num = 0; di_num < NUM_MB_PCI_DEV_IRQS; di_num++) {
			struct mb_pci_dev_irq *di = &mb_pci_dev_irqs[di_num];

			if (di->dev == PCI_SLOT (dev->devfn)) {
				unsigned irq = di->irq_base;

				if (di->query_pin) {
					/* Find out which interrupt pin
					   this device uses (each PCI
					   slot has 4).  */
					u8 irq_pin;

					pci_read_config_byte (dev,
							     PCI_INTERRUPT_PIN,
							      &irq_pin);

					if (irq_pin == 0)
						/* Doesn't use interrupts.  */ 
						continue;
					else
						irq += irq_pin - 1;
				}

				pcibios_update_irq (dev, irq);
			}
		}
	}
}

void __devinit pcibios_update_irq (struct pci_dev *dev, int irq)
{
	dev->irq = irq;
	pci_write_config_byte (dev, PCI_INTERRUPT_LINE, irq);
}

void __devinit
pcibios_resource_to_bus(struct pci_dev *dev, struct pci_bus_region *region,
			struct resource *res)
{
	unsigned long offset = 0;

	if (res->flags & IORESOURCE_IO) {
		offset = MB_A_PCI_IO_ADDR;
	} else if (res->flags & IORESOURCE_MEM) {
		offset = MB_A_PCI_MEM_ADDR;
	}

	region->start = res->start - offset;
	region->end = res->end - offset;
}


/* Stubs for things we don't use.  */

/* Called after each bus is probed, but before its children are examined. */
void pcibios_fixup_bus(struct pci_bus *b)
{
}

void
pcibios_align_resource (void *data, struct resource *res,
			resource_size_t size, resource_size_t align)
{
}

void pcibios_set_master (struct pci_dev *dev)
{
}


/* Mother-A SRAM memory allocation.  This is a simple first-fit allocator.  */

/* A memory free-list node.  */
struct mb_sram_free_area {
	void *mem;
	unsigned long size;
	struct mb_sram_free_area *next;
};

/* The tail of the free-list, which starts out containing all the SRAM.  */
static struct mb_sram_free_area mb_sram_free_tail = {
	(void *)MB_A_SRAM_ADDR, MB_A_SRAM_SIZE, 0
};

/* The free-list.  */
static struct mb_sram_free_area *mb_sram_free_areas = &mb_sram_free_tail;

/* The free-list of free free-list nodes. (:-)  */
static struct mb_sram_free_area *mb_sram_free_free_areas = 0;

/* Spinlock protecting the above globals.  */
static DEFINE_SPINLOCK(mb_sram_lock);

/* Allocate a memory block at least SIZE bytes long in the Mother-A SRAM
   space.  */
static void *alloc_mb_sram (size_t size)
{
	struct mb_sram_free_area *prev, *fa;
	unsigned long flags;
	void *mem = 0;

	spin_lock_irqsave (mb_sram_lock, flags);

	/* Look for a free area that can contain SIZE bytes.  */
	for (prev = 0, fa = mb_sram_free_areas; fa; prev = fa, fa = fa->next)
		if (fa->size >= size) {
			/* Found one!  */
			mem = fa->mem;

			if (fa->size == size) {
				/* In fact, it fits exactly, so remove
				   this node from the free-list.  */
				if (prev)
					prev->next = fa->next;
				else
					mb_sram_free_areas = fa->next;
				/* Put it on the free-list-entry-free-list. */
				fa->next = mb_sram_free_free_areas;
				mb_sram_free_free_areas = fa;
			} else {
				/* FA is bigger than SIZE, so just
				   reduce its size to account for this
				   allocation.  */
				fa->mem += size;
				fa->size -= size;
			}

			break;
		}

	spin_unlock_irqrestore (mb_sram_lock, flags);

	return mem;
}

/* Return the memory area MEM of size SIZE to the MB SRAM free pool.  */
static void free_mb_sram (void *mem, size_t size)
{
	struct mb_sram_free_area *prev, *fa, *new_fa;
	unsigned long flags;
	void *end = mem + size;

	spin_lock_irqsave (mb_sram_lock, flags);

 retry:
	/* Find an adjacent free-list entry.  */
	for (prev = 0, fa = mb_sram_free_areas; fa; prev = fa, fa = fa->next)
		if (fa->mem == end) {
			/* FA is just after MEM, grow down to encompass it. */
			fa->mem = mem;
			fa->size += size;
			goto done;
		} else if (fa->mem + fa->size == mem) {
			struct mb_sram_free_area *next_fa = fa->next;

			/* FA is just before MEM, expand to encompass it. */
			fa->size += size;

			/* See if FA can now be merged with its successor. */
			if (next_fa && fa->mem + fa->size == next_fa->mem) {
				/* Yup; merge NEXT_FA's info into FA.  */
				fa->size += next_fa->size;
				fa->next = next_fa->next;
				/* Free NEXT_FA.  */
				next_fa->next = mb_sram_free_free_areas;
				mb_sram_free_free_areas = next_fa;
			}
			goto done;
		} else if (fa->mem > mem)
			/* We've reached the right spot in the free-list
			   without finding an adjacent free-area, so add
			   a new free area to hold mem. */
			break;

	/* Make a new free-list entry.  */

	/* First, get a free-list entry.  */
	if (! mb_sram_free_free_areas) {
		/* There are none, so make some.  */
		void *block;
		size_t block_size = sizeof (struct mb_sram_free_area) * 8;

		/* Don't hold the lock while calling kmalloc (I'm not
		   sure whether it would be a problem, since we use
		   GFP_ATOMIC, but it makes me nervous).  */
		spin_unlock_irqrestore (mb_sram_lock, flags);

		block = kmalloc (block_size, GFP_ATOMIC);
		if (! block)
			panic ("free_mb_sram: can't allocate free-list entry");

		/* Now get the lock back.  */
		spin_lock_irqsave (mb_sram_lock, flags);

		/* Add the new free free-list entries.  */
		while (block_size > 0) {
			struct mb_sram_free_area *nfa = block;
			nfa->next = mb_sram_free_free_areas;
			mb_sram_free_free_areas = nfa;
			block += sizeof *nfa;
			block_size -= sizeof *nfa;
		}

		/* Since we dropped the lock to call kmalloc, the
		   free-list could have changed, so retry from the
		   beginning.  */
		goto retry;
	}

	/* Remove NEW_FA from the free-list of free-list entries.  */
	new_fa = mb_sram_free_free_areas;
	mb_sram_free_free_areas = new_fa->next;

	/* NEW_FA initially holds only MEM.  */
	new_fa->mem = mem;
	new_fa->size = size;

	/* Insert NEW_FA in the free-list between PREV and FA. */
	new_fa->next = fa;
	if (prev)
		prev->next = new_fa;
	else
		mb_sram_free_areas = new_fa;

 done:
	spin_unlock_irqrestore (mb_sram_lock, flags);
}


/* Maintainence of CPU -> Mother-A DMA mappings.  */

struct dma_mapping {
	void *cpu_addr;
	void *mb_sram_addr;
	size_t size;
	struct dma_mapping *next;
};

/* A list of mappings from CPU addresses to MB SRAM addresses for active
   DMA blocks (that have been `granted' to the PCI device).  */
static struct dma_mapping *active_dma_mappings = 0;

/* A list of free mapping objects.  */
static struct dma_mapping *free_dma_mappings = 0;

/* Spinlock protecting the above globals.  */
static DEFINE_SPINLOCK(dma_mappings_lock);

static struct dma_mapping *new_dma_mapping (size_t size)
{
	unsigned long flags;
	struct dma_mapping *mapping;
	void *mb_sram_block = alloc_mb_sram (size);

	if (! mb_sram_block)
		return 0;

	spin_lock_irqsave (dma_mappings_lock, flags);

	if (! free_dma_mappings) {
		/* We're out of mapping structures, make more.  */
		void *mblock;
		size_t mblock_size = sizeof (struct dma_mapping) * 8;

		/* Don't hold the lock while calling kmalloc (I'm not
		   sure whether it would be a problem, since we use
		   GFP_ATOMIC, but it makes me nervous).  */
		spin_unlock_irqrestore (dma_mappings_lock, flags);

		mblock = kmalloc (mblock_size, GFP_ATOMIC);
		if (! mblock) {
			free_mb_sram (mb_sram_block, size);
			return 0;
		}

		/* Get the lock back.  */
		spin_lock_irqsave (dma_mappings_lock, flags);

		/* Add the new mapping structures to the free-list.  */
		while (mblock_size > 0) {
			struct dma_mapping *fm = mblock;
			fm->next = free_dma_mappings;
			free_dma_mappings = fm;
			mblock += sizeof *fm;
			mblock_size -= sizeof *fm;
		}
	}

	/* Get a mapping struct from the freelist.  */
	mapping = free_dma_mappings;
	free_dma_mappings = mapping->next;

	/* Initialize the mapping.  Other fields should be filled in by
	   caller.  */
	mapping->mb_sram_addr = mb_sram_block;
	mapping->size = size;

	/* Add it to the list of active mappings.  */
	mapping->next = active_dma_mappings;
	active_dma_mappings = mapping;

	spin_unlock_irqrestore (dma_mappings_lock, flags);

	return mapping;
}

static struct dma_mapping *find_dma_mapping (void *mb_sram_addr)
{
	unsigned long flags;
	struct dma_mapping *mapping;

	spin_lock_irqsave (dma_mappings_lock, flags);

	for (mapping = active_dma_mappings; mapping; mapping = mapping->next)
		if (mapping->mb_sram_addr == mb_sram_addr) {
			spin_unlock_irqrestore (dma_mappings_lock, flags);
			return mapping;
		}

	panic ("find_dma_mapping: unmapped PCI DMA addr 0x%x",
	       MB_SRAM_TO_PCI (mb_sram_addr));
}

static struct dma_mapping *deactivate_dma_mapping (void *mb_sram_addr)
{
	unsigned long flags;
	struct dma_mapping *mapping, *prev;

	spin_lock_irqsave (dma_mappings_lock, flags);

	for (prev = 0, mapping = active_dma_mappings;
	     mapping;
	     prev = mapping, mapping = mapping->next)
	{
		if (mapping->mb_sram_addr == mb_sram_addr) {
			/* This is the MAPPING; deactivate it.  */
			if (prev)
				prev->next = mapping->next;
			else
				active_dma_mappings = mapping->next;

			spin_unlock_irqrestore (dma_mappings_lock, flags);

			return mapping;
		}
	}

	panic ("deactivate_dma_mapping: unmapped PCI DMA addr 0x%x",
	       MB_SRAM_TO_PCI (mb_sram_addr));
}

/* Return MAPPING to the freelist.  */
static inline void
free_dma_mapping (struct dma_mapping *mapping)
{
	unsigned long flags;

	free_mb_sram (mapping->mb_sram_addr, mapping->size);

	spin_lock_irqsave (dma_mappings_lock, flags);

	mapping->next = free_dma_mappings;
	free_dma_mappings = mapping;

	spin_unlock_irqrestore (dma_mappings_lock, flags);
}


/* Single PCI DMA mappings.  */

/* `Grant' to PDEV the memory block at CPU_ADDR, for doing DMA.  The
   32-bit PCI bus mastering address to use is returned.  the device owns
   this memory until either pci_unmap_single or pci_dma_sync_single is
   performed.  */
dma_addr_t
pci_map_single (struct pci_dev *pdev, void *cpu_addr, size_t size, int dir)
{
	struct dma_mapping *mapping = new_dma_mapping (size);

	if (! mapping)
		return 0;

	mapping->cpu_addr = cpu_addr;

	if (dir == PCI_DMA_BIDIRECTIONAL || dir == PCI_DMA_TODEVICE)
		memcpy (mapping->mb_sram_addr, cpu_addr, size);

	return MB_SRAM_TO_PCI (mapping->mb_sram_addr);
}

/* Return to the CPU the PCI DMA memory block previously `granted' to
   PDEV, at DMA_ADDR.  */
void pci_unmap_single (struct pci_dev *pdev, dma_addr_t dma_addr, size_t size,
		       int dir)
{
	void *mb_sram_addr = PCI_TO_MB_SRAM (dma_addr);
	struct dma_mapping *mapping = deactivate_dma_mapping (mb_sram_addr);

	if (size != mapping->size)
		panic ("pci_unmap_single: size (%d) doesn't match"
		       " size of mapping at PCI DMA addr 0x%x (%d)\n",
		       size, dma_addr, mapping->size);

	/* Copy back the DMA'd contents if necessary.  */
	if (dir == PCI_DMA_BIDIRECTIONAL || dir == PCI_DMA_FROMDEVICE)
		memcpy (mapping->cpu_addr, mb_sram_addr, size);

	/* Return mapping to the freelist.  */
	free_dma_mapping (mapping);
}

/* Make physical memory consistent for a single streaming mode DMA
   translation after a transfer.

   If you perform a pci_map_single() but wish to interrogate the
   buffer using the cpu, yet do not wish to teardown the PCI dma
   mapping, you must call this function before doing so.  At the next
   point you give the PCI dma address back to the card, you must first
   perform a pci_dma_sync_for_device, and then the device again owns
   the buffer.  */
void
pci_dma_sync_single_for_cpu (struct pci_dev *pdev, dma_addr_t dma_addr, size_t size,
		     int dir)
{
	void *mb_sram_addr = PCI_TO_MB_SRAM (dma_addr);
	struct dma_mapping *mapping = find_dma_mapping (mb_sram_addr);

	/* Synchronize the DMA buffer with the CPU buffer if necessary.  */
	if (dir == PCI_DMA_FROMDEVICE)
		memcpy (mapping->cpu_addr, mb_sram_addr, size);
	else if (dir == PCI_DMA_TODEVICE)
		; /* nothing to do */
	else
		panic("pci_dma_sync_single: unsupported sync dir: %d", dir);
}

void
pci_dma_sync_single_for_device (struct pci_dev *pdev, dma_addr_t dma_addr, size_t size,
				int dir)
{
	void *mb_sram_addr = PCI_TO_MB_SRAM (dma_addr);
	struct dma_mapping *mapping = find_dma_mapping (mb_sram_addr);

	/* Synchronize the DMA buffer with the CPU buffer if necessary.  */
	if (dir == PCI_DMA_FROMDEVICE)
		; /* nothing to do */
	else if (dir == PCI_DMA_TODEVICE)
		memcpy (mb_sram_addr, mapping->cpu_addr, size);
	else
		panic("pci_dma_sync_single: unsupported sync dir: %d", dir);
}


/* Scatter-gather PCI DMA mappings.  */

/* Do multiple DMA mappings at once.  */
int
pci_map_sg (struct pci_dev *pdev, struct scatterlist *sg, int sg_len, int dir)
{
	BUG ();
	return 0;
}

/* Unmap multiple DMA mappings at once.  */
void
pci_unmap_sg (struct pci_dev *pdev, struct scatterlist *sg, int sg_len,int dir)
{
	BUG ();
}

/* Make physical memory consistent for a set of streaming mode DMA
   translations after a transfer.  The same as pci_dma_sync_single_* but
   for a scatter-gather list, same rules and usage.  */

void
pci_dma_sync_sg_for_cpu (struct pci_dev *dev,
			 struct scatterlist *sg, int sg_len,
			 int dir)
{
	BUG ();
}

void
pci_dma_sync_sg_for_device (struct pci_dev *dev,
			    struct scatterlist *sg, int sg_len,
			    int dir)
{
	BUG ();
}


/* PCI mem mapping.  */

/* Allocate and map kernel buffer using consistent mode DMA for PCI
   device.  Returns non-NULL cpu-view pointer to the buffer if
   successful and sets *DMA_ADDR to the pci side dma address as well,
   else DMA_ADDR is undefined.  */
void *
pci_alloc_consistent (struct pci_dev *pdev, size_t size, dma_addr_t *dma_addr)
{
	void *mb_sram_mem = alloc_mb_sram (size);
	if (mb_sram_mem)
		*dma_addr = MB_SRAM_TO_PCI (mb_sram_mem);
	return mb_sram_mem;
}

/* Free and unmap a consistent DMA buffer.  CPU_ADDR and DMA_ADDR must
   be values that were returned from pci_alloc_consistent.  SIZE must be
   the same as what as passed into pci_alloc_consistent.  References to
   the memory and mappings associated with CPU_ADDR or DMA_ADDR past
   this call are illegal.  */
void
pci_free_consistent (struct pci_dev *pdev, size_t size, void *cpu_addr,
		     dma_addr_t dma_addr)
{
	void *mb_sram_mem = PCI_TO_MB_SRAM (dma_addr);
	free_mb_sram (mb_sram_mem, size);
}


/* iomap/iomap */

void __iomem *pci_iomap (struct pci_dev *dev, int bar, unsigned long max)
{
	unsigned long start = pci_resource_start (dev, bar);
	unsigned long len = pci_resource_len (dev, bar);

	if (!start || len == 0)
		return 0;

	/* None of the ioremap functions actually do anything, other than
	   re-casting their argument, so don't bother differentiating them.  */
	return ioremap (start, len);
}

void pci_iounmap (struct pci_dev *dev, void __iomem *addr)
{
	/* nothing */
}


/* symbol exports (for modules) */

EXPORT_SYMBOL (pci_map_single);
EXPORT_SYMBOL (pci_unmap_single);
EXPORT_SYMBOL (pci_alloc_consistent);
EXPORT_SYMBOL (pci_free_consistent);
EXPORT_SYMBOL (pci_dma_sync_single_for_cpu);
EXPORT_SYMBOL (pci_dma_sync_single_for_device);
EXPORT_SYMBOL (pci_iomap);
EXPORT_SYMBOL (pci_iounmap);

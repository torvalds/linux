/* SPDX-License-Identifier: GPL-2.0 */
/*
 *	linux/arch/alpha/kernel/pci_impl.h
 *
 * This file contains declarations and inline functions for interfacing
 * with the PCI initialization routines.
 */

struct pci_dev;
struct pci_controller;
struct pci_iommu_arena;

/*
 * We can't just blindly use 64K for machines with EISA busses; they
 * may also have PCI-PCI bridges present, and then we'd configure the
 * bridge incorrectly.
 *
 * Also, we start at 0x8000 or 0x9000, in hopes to get all devices'
 * IO space areas allocated *before* 0xC000; this is because certain
 * BIOSes (Millennium for one) use PCI Config space "mechanism #2"
 * accesses to probe the bus. If a device's registers appear at 0xC000,
 * it may see an INx/OUTx at that address during BIOS emulation of the
 * VGA BIOS, and some cards, notably Adaptec 2940UW, take mortal offense.
 */

#define EISA_DEFAULT_IO_BASE	0x9000	/* start above 8th slot */
#define DEFAULT_IO_BASE		0x8000	/* start at 8th slot */

/*
 * We try to make the DEFAULT_MEM_BASE addresses *always* have more than
 * a single bit set. This is so that devices like the broken Myrinet card
 * will always have a PCI memory address that will never match a IDSEL
 * address in PCI Config space, which can cause problems with early rev cards.
 */

/*
 * An XL is AVANTI (APECS) family, *but* it has only 27 bits of ISA address
 * that get passed through the PCI<->ISA bridge chip. Although this causes
 * us to set the PCI->Mem window bases lower than normal, we still allocate
 * PCI bus devices' memory addresses *below* the low DMA mapping window,
 * and hope they fit below 64Mb (to avoid conflicts), and so that they can
 * be accessed via SPARSE space.
 *
 * We accept the risk that a broken Myrinet card will be put into a true XL
 * and thus can more easily run into the problem described below.
 */
#define XL_DEFAULT_MEM_BASE ((16+2)*1024*1024) /* 16M to 64M-1 is avail */

/*
 * APECS and LCA have only 34 bits for physical addresses, thus limiting PCI
 * bus memory addresses for SPARSE access to be less than 128Mb.
 */
#define APECS_AND_LCA_DEFAULT_MEM_BASE ((16+2)*1024*1024)

/*
 * Because MCPCIA and T2 core logic support more bits for
 * physical addresses, they should allow an expanded range of SPARSE
 * memory addresses.  However, we do not use them all, in order to
 * avoid the HAE manipulation that would be needed.
 */
#define MCPCIA_DEFAULT_MEM_BASE ((32+2)*1024*1024)
#define T2_DEFAULT_MEM_BASE ((16+1)*1024*1024)

/*
 * Because CIA and PYXIS have more bits for physical addresses,
 * they support an expanded range of SPARSE memory addresses.
 */
#define DEFAULT_MEM_BASE ((128+16)*1024*1024)

/* ??? Experimenting with no HAE for CIA.  */
#define CIA_DEFAULT_MEM_BASE ((32+2)*1024*1024)

#define IRONGATE_DEFAULT_MEM_BASE ((256*8-16)*1024*1024)

#define DEFAULT_AGP_APER_SIZE	(64*1024*1024)

/* 
 * A small note about bridges and interrupts.  The DECchip 21050 (and
 * later) adheres to the PCI-PCI bridge specification.  This says that
 * the interrupts on the other side of a bridge are swizzled in the
 * following manner:
 *
 * Dev    Interrupt   Interrupt 
 *        Pin on      Pin on 
 *        Device      Connector
 *
 *   4    A           A
 *        B           B
 *        C           C
 *        D           D
 * 
 *   5    A           B
 *        B           C
 *        C           D
 *        D           A
 *
 *   6    A           C
 *        B           D
 *        C           A
 *        D           B
 *
 *   7    A           D
 *        B           A
 *        C           B
 *        D           C
 *
 *   Where A = pin 1, B = pin 2 and so on and pin=0 = default = A.
 *   Thus, each swizzle is ((pin-1) + (device#-4)) % 4
 *
 *   pci_swizzle_interrupt_pin() swizzles for exactly one bridge.  The routine
 *   pci_common_swizzle() handles multiple bridges.  But there are a
 *   couple boards that do strange things.
 */


/* The following macro is used to implement the table-based irq mapping
   function for all single-bus Alphas.  */

#define COMMON_TABLE_LOOKUP						\
({ long _ctl_ = -1; 							\
   if (slot >= min_idsel && slot <= max_idsel && pin < irqs_per_slot)	\
     _ctl_ = irq_tab[slot - min_idsel][pin];				\
   _ctl_; })


/* A PCI IOMMU allocation arena.  There are typically two of these
   regions per bus.  */
/* ??? The 8400 has a 32-byte pte entry, and the entire table apparently
   lives directly on the host bridge (no tlb?).  We don't support this
   machine, but if we ever did, we'd need to parameterize all this quite
   a bit further.  Probably with per-bus operation tables.  */

struct pci_iommu_arena
{
	spinlock_t lock;
	struct pci_controller *hose;
#define IOMMU_INVALID_PTE 0x2 /* 32:63 bits MBZ */
#define IOMMU_RESERVED_PTE 0xface
	unsigned long *ptes;
	dma_addr_t dma_base;
	unsigned int size;
	unsigned int next_entry;
	unsigned int align_entry;
};

#if defined(CONFIG_ALPHA_SRM) && defined(CONFIG_ALPHA_CIA)
# define NEED_SRM_SAVE_RESTORE
#else
# undef NEED_SRM_SAVE_RESTORE
#endif

#if defined(CONFIG_ALPHA_GENERIC) || defined(NEED_SRM_SAVE_RESTORE)
# define ALPHA_RESTORE_SRM_SETUP
#else
# undef ALPHA_RESTORE_SRM_SETUP
#endif

#ifdef ALPHA_RESTORE_SRM_SETUP
extern void pci_restore_srm_config(void);
#else
#define pci_restore_srm_config()	do {} while (0)
#endif

/* The hose list.  */
extern struct pci_controller *hose_head, **hose_tail;
extern struct pci_controller *pci_isa_hose;

extern unsigned long alpha_agpgart_size;

extern void common_init_pci(void);
#define common_swizzle pci_common_swizzle
extern struct pci_controller *alloc_pci_controller(void);
extern struct resource *alloc_resource(void);

extern struct pci_iommu_arena *iommu_arena_new_node(int,
						    struct pci_controller *,
					            dma_addr_t, unsigned long,
					            unsigned long);
extern struct pci_iommu_arena *iommu_arena_new(struct pci_controller *,
					       dma_addr_t, unsigned long,
					       unsigned long);
extern const char *const pci_io_names[];
extern const char *const pci_mem_names[];
extern const char pci_hae0_name[];

extern unsigned long size_for_memory(unsigned long max);

extern int iommu_reserve(struct pci_iommu_arena *, long, long);
extern int iommu_release(struct pci_iommu_arena *, long, long);
extern int iommu_bind(struct pci_iommu_arena *, long, long, struct page **);
extern int iommu_unbind(struct pci_iommu_arena *, long, long);



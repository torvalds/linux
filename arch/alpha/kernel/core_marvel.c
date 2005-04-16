/*
 *	linux/arch/alpha/kernel/core_marvel.c
 *
 * Code common to all Marvel based systems.
 */

#define __EXTERN_INLINE inline
#include <asm/io.h>
#include <asm/core_marvel.h>
#undef __EXTERN_INLINE

#include <linux/types.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/vmalloc.h>
#include <linux/mc146818rtc.h>
#include <linux/rtc.h>
#include <linux/module.h>
#include <linux/bootmem.h>

#include <asm/ptrace.h>
#include <asm/smp.h>
#include <asm/gct.h>
#include <asm/pgalloc.h>
#include <asm/tlbflush.h>
#include <asm/rtc.h>

#include "proto.h"
#include "pci_impl.h"


/*
 * Debug helpers
 */
#define DEBUG_CONFIG 0

#if DEBUG_CONFIG
# define DBG_CFG(args) printk args
#else
# define DBG_CFG(args)
#endif


/*
 * Private data
 */
static struct io7 *io7_head = NULL;


/*
 * Helper functions
 */
static unsigned long __attribute__ ((unused))
read_ev7_csr(int pe, unsigned long offset)
{
	ev7_csr *ev7csr = EV7_CSR_KERN(pe, offset);
	unsigned long q;

	mb();
	q = ev7csr->csr;
	mb();

	return q;
}

static void __attribute__ ((unused))
write_ev7_csr(int pe, unsigned long offset, unsigned long q)
{
	ev7_csr *ev7csr = EV7_CSR_KERN(pe, offset);

	mb();
	ev7csr->csr = q;
	mb();
}

static char * __init
mk_resource_name(int pe, int port, char *str)
{
	char tmp[80];
	char *name;
	
	sprintf(tmp, "PCI %s PE %d PORT %d", str, pe, port);
	name = alloc_bootmem(strlen(tmp) + 1);
	strcpy(name, tmp);

	return name;
}

inline struct io7 *
marvel_next_io7(struct io7 *prev)
{
	return (prev ? prev->next : io7_head);
}

struct io7 *
marvel_find_io7(int pe)
{
	struct io7 *io7;

	for (io7 = io7_head; io7 && io7->pe != pe; io7 = io7->next)
		continue;

	return io7;
}

static struct io7 * __init
alloc_io7(unsigned int pe)
{
	struct io7 *io7;
	struct io7 *insp;
	int h;

	if (marvel_find_io7(pe)) {
		printk(KERN_WARNING "IO7 at PE %d already allocated!\n", pe);
		return NULL;
	}

	io7 = alloc_bootmem(sizeof(*io7));
	io7->pe = pe;
	spin_lock_init(&io7->irq_lock);

	for (h = 0; h < 4; h++) {
		io7->ports[h].io7 = io7;
		io7->ports[h].port = h;
		io7->ports[h].enabled = 0; /* default to disabled */
	}

	/*
	 * Insert in pe sorted order.
	 */
	if (NULL == io7_head)			/* empty list */
		io7_head = io7;	
	else if (io7_head->pe > io7->pe) {	/* insert at head */
		io7->next = io7_head;
		io7_head = io7;
	} else {				/* insert at position */
		for (insp = io7_head; insp; insp = insp->next) {
			if (insp->pe == io7->pe) {
				printk(KERN_ERR "Too many IO7s at PE %d\n", 
				       io7->pe);
				return NULL;
			}

			if (NULL == insp->next || 
			    insp->next->pe > io7->pe) { /* insert here */
				io7->next = insp->next;
				insp->next = io7;
				break;
			}
		}

		if (NULL == insp) { /* couldn't insert ?!? */
			printk(KERN_WARNING "Failed to insert IO7 at PE %d "
			       " - adding at head of list\n", io7->pe);
			io7->next = io7_head;
			io7_head = io7;
		}
	}
	
	return io7;
}

void
io7_clear_errors(struct io7 *io7)
{
	io7_port7_csrs *p7csrs;
	io7_ioport_csrs *csrs;
	int port;


	/*
	 * First the IO ports.
	 */
	for (port = 0; port < 4; port++) {
		csrs = IO7_CSRS_KERN(io7->pe, port);

		csrs->POx_ERR_SUM.csr = -1UL;
		csrs->POx_TLB_ERR.csr = -1UL;
		csrs->POx_SPL_COMPLT.csr = -1UL;
		csrs->POx_TRANS_SUM.csr = -1UL;
	}

	/*
	 * Then the common ones.
	 */
	p7csrs = IO7_PORT7_CSRS_KERN(io7->pe);

	p7csrs->PO7_ERROR_SUM.csr = -1UL;
	p7csrs->PO7_UNCRR_SYM.csr = -1UL;
	p7csrs->PO7_CRRCT_SYM.csr = -1UL;
}


/*
 * IO7 PCI, PCI/X, AGP configuration.
 */
static void __init
io7_init_hose(struct io7 *io7, int port)
{
	static int hose_index = 0;

	struct pci_controller *hose = alloc_pci_controller();
	struct io7_port *io7_port = &io7->ports[port];
	io7_ioport_csrs *csrs = IO7_CSRS_KERN(io7->pe, port);
	int i;

	hose->index = hose_index++;	/* arbitrary */
	
	/*
	 * We don't have an isa or legacy hose, but glibc expects to be
	 * able to use the bus == 0 / dev == 0 form of the iobase syscall
	 * to determine information about the i/o system. Since XFree86 
	 * relies on glibc's determination to tell whether or not to use
	 * sparse access, we need to point the pci_isa_hose at a real hose
	 * so at least that determination is correct.
	 */
	if (hose->index == 0)
		pci_isa_hose = hose;

	io7_port->csrs = csrs;
	io7_port->hose = hose;
	hose->sysdata = io7_port;

	hose->io_space = alloc_resource();
	hose->mem_space = alloc_resource();

	/*
	 * Base addresses for userland consumption. Since these are going
	 * to be mapped, they are pure physical addresses.
	 */
	hose->sparse_mem_base = hose->sparse_io_base = 0;
	hose->dense_mem_base = IO7_MEM_PHYS(io7->pe, port);
	hose->dense_io_base = IO7_IO_PHYS(io7->pe, port);

	/*
	 * Base addresses and resource ranges for kernel consumption.
	 */
	hose->config_space_base = (unsigned long)IO7_CONF_KERN(io7->pe, port);

	hose->io_space->start = (unsigned long)IO7_IO_KERN(io7->pe, port);
	hose->io_space->end = hose->io_space->start + IO7_IO_SPACE - 1;
	hose->io_space->name = mk_resource_name(io7->pe, port, "IO");
	hose->io_space->flags = IORESOURCE_IO;

	hose->mem_space->start = (unsigned long)IO7_MEM_KERN(io7->pe, port);
	hose->mem_space->end = hose->mem_space->start + IO7_MEM_SPACE - 1;
	hose->mem_space->name = mk_resource_name(io7->pe, port, "MEM");
	hose->mem_space->flags = IORESOURCE_MEM;

	if (request_resource(&ioport_resource, hose->io_space) < 0)
		printk(KERN_ERR "Failed to request IO on hose %d\n", 
		       hose->index);
	if (request_resource(&iomem_resource, hose->mem_space) < 0)
		printk(KERN_ERR "Failed to request MEM on hose %d\n", 
		       hose->index);

	/*
	 * Save the existing DMA window settings for later restoration.
	 */
	for (i = 0; i < 4; i++) {
		io7_port->saved_wbase[i] = csrs->POx_WBASE[i].csr;
		io7_port->saved_wmask[i] = csrs->POx_WMASK[i].csr;
		io7_port->saved_tbase[i] = csrs->POx_TBASE[i].csr;
	}

	/*
	 * Set up the PCI to main memory translation windows.
	 *
	 * Window 0 is scatter-gather 8MB at 8MB
	 * Window 1 is direct access 1GB at 2GB
	 * Window 2 is scatter-gather (up-to) 1GB at 3GB
	 * Window 3 is disabled
	 */

	/*
	 * TBIA before modifying windows.
	 */
	marvel_pci_tbi(hose, 0, -1);

	/*
	 * Set up window 0 for scatter-gather 8MB at 8MB.
	 */
	hose->sg_isa = iommu_arena_new_node(marvel_cpuid_to_nid(io7->pe),
					    hose, 0x00800000, 0x00800000, 0);
	hose->sg_isa->align_entry = 8;	/* cache line boundary */
	csrs->POx_WBASE[0].csr = 
		hose->sg_isa->dma_base | wbase_m_ena | wbase_m_sg;
	csrs->POx_WMASK[0].csr = (hose->sg_isa->size - 1) & wbase_m_addr;
	csrs->POx_TBASE[0].csr = virt_to_phys(hose->sg_isa->ptes);

	/*
	 * Set up window 1 for direct-mapped 1GB at 2GB.
	 */
	csrs->POx_WBASE[1].csr = __direct_map_base | wbase_m_ena;
	csrs->POx_WMASK[1].csr = (__direct_map_size - 1) & wbase_m_addr;
	csrs->POx_TBASE[1].csr = 0;

	/*
	 * Set up window 2 for scatter-gather (up-to) 1GB at 3GB.
	 */
	hose->sg_pci = iommu_arena_new_node(marvel_cpuid_to_nid(io7->pe),
					    hose, 0xc0000000, 0x40000000, 0);
	hose->sg_pci->align_entry = 8;	/* cache line boundary */
	csrs->POx_WBASE[2].csr = 
		hose->sg_pci->dma_base | wbase_m_ena | wbase_m_sg;
	csrs->POx_WMASK[2].csr = (hose->sg_pci->size - 1) & wbase_m_addr;
	csrs->POx_TBASE[2].csr = virt_to_phys(hose->sg_pci->ptes);

	/*
	 * Disable window 3.
	 */
	csrs->POx_WBASE[3].csr = 0;

	/*
	 * Make sure that the AGP Monster Window is disabled.
	 */
	csrs->POx_CTRL.csr &= ~(1UL << 61);

#if 1
	printk("FIXME: disabling master aborts\n");
	csrs->POx_MSK_HEI.csr &= ~(3UL << 14);
#endif
	/*
	 * TBIA after modifying windows.
	 */
	marvel_pci_tbi(hose, 0, -1);
}

static void __init
marvel_init_io7(struct io7 *io7)
{
	int i;

	printk("Initializing IO7 at PID %d\n", io7->pe);

	/*
	 * Get the Port 7 CSR pointer.
	 */
	io7->csrs = IO7_PORT7_CSRS_KERN(io7->pe);

	/*
	 * Init this IO7's hoses.
	 */
	for (i = 0; i < IO7_NUM_PORTS; i++) {
		io7_ioport_csrs *csrs = IO7_CSRS_KERN(io7->pe, i);
		if (csrs->POx_CACHE_CTL.csr == 8) {
			io7->ports[i].enabled = 1;
			io7_init_hose(io7, i);
		}
	}
}

void
marvel_io7_present(gct6_node *node)
{
	int pe;

	if (node->type != GCT_TYPE_HOSE ||
	    node->subtype != GCT_SUBTYPE_IO_PORT_MODULE) 
		return;

	pe = (node->id >> 8) & 0xff;
	printk("Found an IO7 at PID %d\n", pe);

	alloc_io7(pe);
}

static void __init
marvel_init_vga_hose(void)
{
#ifdef CONFIG_VGA_HOSE
	u64 *pu64 = (u64 *)((u64)hwrpb + hwrpb->ctbt_offset);

	if (pu64[7] == 3) {	/* TERM_TYPE == graphics */
		struct pci_controller *hose = NULL;
		int h = (pu64[30] >> 24) & 0xff; /* TERM_OUT_LOC, hose # */
		struct io7 *io7;
		int pid, port;

		/* FIXME - encoding is going to have to change for Marvel
		 *         since hose will be able to overflow a byte...
		 *         need to fix this decode when the console 
		 *         changes its encoding
		 */
		printk("console graphics is on hose %d (console)\n", h);

		/*
		 * The console's hose numbering is:
		 *
		 *	hose<n:2>: PID
		 *	hose<1:0>: PORT
		 *
		 * We need to find the hose at that pid and port
		 */
		pid = h >> 2;
		port = h & 3;
		if ((io7 = marvel_find_io7(pid)))
			hose = io7->ports[port].hose;

		if (hose) {
			printk("Console graphics on hose %d\n", hose->index);
			pci_vga_hose = hose;
		}
	}
#endif /* CONFIG_VGA_HOSE */
}

gct6_search_struct gct_wanted_node_list[] = {
	{ GCT_TYPE_HOSE, GCT_SUBTYPE_IO_PORT_MODULE, marvel_io7_present },
	{ 0, 0, NULL }
};

/*
 * In case the GCT is not complete, let the user specify PIDs with IO7s
 * at boot time. Syntax is 'io7=a,b,c,...,n' where a-n are the PIDs (decimal)
 * where IO7s are connected
 */
static int __init
marvel_specify_io7(char *str)
{
	unsigned long pid;
	struct io7 *io7;
	char *pchar;

	do {
		pid = simple_strtoul(str, &pchar, 0);
		if (pchar != str) {
			printk("User-specified IO7 at PID %lu\n", pid);
			io7 = alloc_io7(pid);
			if (io7) marvel_init_io7(io7);
		}

		if (pchar == str) pchar++;
		str = pchar;
	} while(*str);

	return 0;
}
__setup("io7=", marvel_specify_io7);

void __init
marvel_init_arch(void)
{
	struct io7 *io7;

	/* With multiple PCI busses, we play with I/O as physical addrs.  */
	ioport_resource.end = ~0UL;

	/* PCI DMA Direct Mapping is 1GB at 2GB.  */
	__direct_map_base = 0x80000000;
	__direct_map_size = 0x40000000;

	/* Parse the config tree.  */
	gct6_find_nodes(GCT_NODE_PTR(0), gct_wanted_node_list);

	/* Init the io7s.  */
	for (io7 = NULL; NULL != (io7 = marvel_next_io7(io7)); ) 
		marvel_init_io7(io7);

	/* Check for graphic console location (if any).  */
	marvel_init_vga_hose();
}

void
marvel_kill_arch(int mode)
{
}


/*
 * PCI Configuration Space access functions
 *
 * Configuration space addresses have the following format:
 *
 * 	|2 2 2 2|1 1 1 1|1 1 1 1|1 1 
 * 	|3 2 1 0|9 8 7 6|5 4 3 2|1 0 9 8|7 6 5 4|3 2 1 0
 * 	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * 	|B|B|B|B|B|B|B|B|D|D|D|D|D|F|F|F|R|R|R|R|R|R|R|R|
 * 	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 *	 n:24	reserved for hose base
 *	23:16	bus number (8 bits = 128 possible buses)
 *	15:11	Device number (5 bits)
 *	10:8	function number
 *	 7:2	register number
 *  
 * Notes:
 *	IO7 determines whether to use a type 0 or type 1 config cycle
 *	based on the bus number. Therefore the bus number must be set 
 *	to 0 for the root bus on any hose.
 *	
 *	The function number selects which function of a multi-function device 
 *	(e.g., SCSI and Ethernet).
 * 
 */

static inline unsigned long
build_conf_addr(struct pci_controller *hose, u8 bus, 
		unsigned int devfn, int where)
{
	return (hose->config_space_base | (bus << 16) | (devfn << 8) | where);
}

static unsigned long
mk_conf_addr(struct pci_bus *pbus, unsigned int devfn, int where)
{
	struct pci_controller *hose = pbus->sysdata;
	struct io7_port *io7_port;
	unsigned long addr = 0;
	u8 bus = pbus->number;

	if (!hose)
		return addr;

	/* Check for enabled.  */
	io7_port = hose->sysdata;
	if (!io7_port->enabled)
		return addr;

	if (!pbus->parent) { /* No parent means peer PCI bus. */
		/* Don't support idsel > 20 on primary bus.  */
		if (devfn >= PCI_DEVFN(21, 0))
			return addr;
		bus = 0;
	}

	addr = build_conf_addr(hose, bus, devfn, where);

	DBG_CFG(("mk_conf_addr: returning pci_addr 0x%lx\n", addr));
	return addr;
}

static int
marvel_read_config(struct pci_bus *bus, unsigned int devfn, int where,
		   int size, u32 *value)
{
	unsigned long addr;
	
	if (0 == (addr = mk_conf_addr(bus, devfn, where)))
		return PCIBIOS_DEVICE_NOT_FOUND;

	switch(size) {
	case 1:	
		*value = __kernel_ldbu(*(vucp)addr);
		break;
	case 2:	
		*value = __kernel_ldwu(*(vusp)addr);
		break;
	case 4:	
		*value = *(vuip)addr;
		break;
	default:
		return PCIBIOS_FUNC_NOT_SUPPORTED;
	}

	return PCIBIOS_SUCCESSFUL;
}

static int
marvel_write_config(struct pci_bus *bus, unsigned int devfn, int where,
		    int size, u32 value)
{
	unsigned long addr;
	
	if (0 == (addr = mk_conf_addr(bus, devfn, where)))
		return PCIBIOS_DEVICE_NOT_FOUND;

	switch (size) {
	case 1:
		__kernel_stb(value, *(vucp)addr);
		mb();
		__kernel_ldbu(*(vucp)addr);
		break;
	case 2:
		__kernel_stw(value, *(vusp)addr);
		mb();
		__kernel_ldwu(*(vusp)addr);
		break;
	case 4:
		*(vuip)addr = value;
		mb();
		*(vuip)addr;
		break;
	default:
		return PCIBIOS_FUNC_NOT_SUPPORTED;
	}

	return PCIBIOS_SUCCESSFUL;
}

struct pci_ops marvel_pci_ops =
{
	.read =		marvel_read_config,
	.write = 	marvel_write_config,
};


/*
 * Other PCI helper functions.
 */
void
marvel_pci_tbi(struct pci_controller *hose, dma_addr_t start, dma_addr_t end)
{
	io7_ioport_csrs *csrs = ((struct io7_port *)hose->sysdata)->csrs;

	wmb();
	csrs->POx_SG_TBIA.csr = 0;
	mb();
	csrs->POx_SG_TBIA.csr;
}



/*
 * RTC Support
 */
struct marvel_rtc_access_info {
	unsigned long function;
	unsigned long index;
	unsigned long data;
};

static void
__marvel_access_rtc(void *info)
{
	struct marvel_rtc_access_info *rtc_access = info;

	register unsigned long __r0 __asm__("$0");
	register unsigned long __r16 __asm__("$16") = rtc_access->function;
	register unsigned long __r17 __asm__("$17") = rtc_access->index;
	register unsigned long __r18 __asm__("$18") = rtc_access->data;
	
	__asm__ __volatile__(
		"call_pal %4 # cserve rtc"
		: "=r"(__r16), "=r"(__r17), "=r"(__r18), "=r"(__r0)
		: "i"(PAL_cserve), "0"(__r16), "1"(__r17), "2"(__r18)
		: "$1", "$22", "$23", "$24", "$25");

	rtc_access->data = __r0;
}

static u8
__marvel_rtc_io(u8 b, unsigned long addr, int write)
{
	static u8 index = 0;

	struct marvel_rtc_access_info rtc_access;
	u8 ret = 0;

	switch(addr) {
	case 0x70:					/* RTC_PORT(0) */
		if (write) index = b;
		ret = index;
		break;

	case 0x71:					/* RTC_PORT(1) */
		rtc_access.index = index;
		rtc_access.data = BCD_TO_BIN(b);
		rtc_access.function = 0x48 + !write;	/* GET/PUT_TOY */

#ifdef CONFIG_SMP
		if (smp_processor_id() != boot_cpuid)
			smp_call_function_on_cpu(__marvel_access_rtc,
						 &rtc_access, 1, 1,
						 cpumask_of_cpu(boot_cpuid));
		else
			__marvel_access_rtc(&rtc_access);
#else
		__marvel_access_rtc(&rtc_access);
#endif
		ret = BIN_TO_BCD(rtc_access.data);
		break;

	default:
		printk(KERN_WARNING "Illegal RTC port %lx\n", addr);
		break;
	}

	return ret;
}


/*
 * IO map support.
 */

#define __marvel_is_mem_vga(a)	(((a) >= 0xa0000) && ((a) <= 0xc0000))

void __iomem *
marvel_ioremap(unsigned long addr, unsigned long size)
{
	struct pci_controller *hose;
	unsigned long baddr, last;
	struct vm_struct *area;
	unsigned long vaddr;
	unsigned long *ptes;
	unsigned long pfn;

	/*
	 * Adjust the addr.
	 */ 
#ifdef CONFIG_VGA_HOSE
	if (pci_vga_hose && __marvel_is_mem_vga(addr)) {
		addr += pci_vga_hose->mem_space->start;
	}
#endif

	/*
	 * Find the hose.
	 */
	for (hose = hose_head; hose; hose = hose->next) {
		if ((addr >> 32) == (hose->mem_space->start >> 32))
			break; 
	}
	if (!hose)
		return NULL;

	/*
	 * We have the hose - calculate the bus limits.
	 */
	baddr = addr - hose->mem_space->start;
	last = baddr + size - 1;

	/*
	 * Is it direct-mapped?
	 */
	if ((baddr >= __direct_map_base) && 
	    ((baddr + size - 1) < __direct_map_base + __direct_map_size)) {
		addr = IDENT_ADDR | (baddr - __direct_map_base);
		return (void __iomem *) addr;
	}

	/* 
	 * Check the scatter-gather arena.
	 */
	if (hose->sg_pci &&
	    baddr >= (unsigned long)hose->sg_pci->dma_base &&
	    last < (unsigned long)hose->sg_pci->dma_base + hose->sg_pci->size) {

		/*
		 * Adjust the limits (mappings must be page aligned)
		 */
		baddr -= hose->sg_pci->dma_base;
		last -= hose->sg_pci->dma_base;
		baddr &= PAGE_MASK;
		size = PAGE_ALIGN(last) - baddr;

		/*
		 * Map it.
		 */
		area = get_vm_area(size, VM_IOREMAP);
		if (!area)
			return NULL;

		ptes = hose->sg_pci->ptes;
		for (vaddr = (unsigned long)area->addr; 
		    baddr <= last; 
		    baddr += PAGE_SIZE, vaddr += PAGE_SIZE) {
			pfn = ptes[baddr >> PAGE_SHIFT];
			if (!(pfn & 1)) {
				printk("ioremap failed... pte not valid...\n");
				vfree(area->addr);
				return NULL;
			}
			pfn >>= 1;	/* make it a true pfn */
			
			if (__alpha_remap_area_pages(vaddr,
						     pfn << PAGE_SHIFT, 
						     PAGE_SIZE, 0)) {
				printk("FAILED to map...\n");
				vfree(area->addr);
				return NULL;
			}
		}

		flush_tlb_all();

		vaddr = (unsigned long)area->addr + (addr & ~PAGE_MASK);

		return (void __iomem *) vaddr;
	}

	return NULL;
}

void
marvel_iounmap(volatile void __iomem *xaddr)
{
	unsigned long addr = (unsigned long) xaddr;
	if (addr >= VMALLOC_START)
		vfree((void *)(PAGE_MASK & addr)); 
}

int
marvel_is_mmio(const volatile void __iomem *xaddr)
{
	unsigned long addr = (unsigned long) xaddr;

	if (addr >= VMALLOC_START)
		return 1;
	else
		return (addr & 0xFF000000UL) == 0;
}

#define __marvel_is_port_vga(a)	\
  (((a) >= 0x3b0) && ((a) < 0x3e0) && ((a) != 0x3b3) && ((a) != 0x3d3))
#define __marvel_is_port_kbd(a)	(((a) == 0x60) || ((a) == 0x64))
#define __marvel_is_port_rtc(a)	(((a) == 0x70) || ((a) == 0x71))

void __iomem *marvel_ioportmap (unsigned long addr)
{
	if (__marvel_is_port_rtc (addr) || __marvel_is_port_kbd(addr))
		;
#ifdef CONFIG_VGA_HOSE
	else if (__marvel_is_port_vga (addr) && pci_vga_hose)
		addr += pci_vga_hose->io_space->start;
#endif
	else
		return NULL;
	return (void __iomem *)addr;
}

unsigned int
marvel_ioread8(void __iomem *xaddr)
{
	unsigned long addr = (unsigned long) xaddr;
	if (__marvel_is_port_kbd(addr))
		return 0;
	else if (__marvel_is_port_rtc(addr))
		return __marvel_rtc_io(0, addr, 0);
	else
		return __kernel_ldbu(*(vucp)addr);
}

void
marvel_iowrite8(u8 b, void __iomem *xaddr)
{
	unsigned long addr = (unsigned long) xaddr;
	if (__marvel_is_port_kbd(addr))
		return;
	else if (__marvel_is_port_rtc(addr)) 
		__marvel_rtc_io(b, addr, 1);
	else
		__kernel_stb(b, *(vucp)addr);
}

#ifndef CONFIG_ALPHA_GENERIC
EXPORT_SYMBOL(marvel_ioremap);
EXPORT_SYMBOL(marvel_iounmap);
EXPORT_SYMBOL(marvel_is_mmio);
EXPORT_SYMBOL(marvel_ioportmap);
EXPORT_SYMBOL(marvel_ioread8);
EXPORT_SYMBOL(marvel_iowrite8);
#endif

/*
 * NUMA Support
 */
/**********
 * FIXME - for now each cpu is a node by itself 
 *              -- no real support for striped mode 
 **********
 */
int
marvel_pa_to_nid(unsigned long pa)
{
	int cpuid;

	if ((pa >> 43) & 1) 	/* I/O */ 
		cpuid = (~(pa >> 35) & 0xff);
	else			/* mem */
		cpuid = ((pa >> 34) & 0x3) | ((pa >> (37 - 2)) & (0x1f << 2));

	return marvel_cpuid_to_nid(cpuid);
}

int
marvel_cpuid_to_nid(int cpuid)
{
	return cpuid;
}

unsigned long
marvel_node_mem_start(int nid)
{
	unsigned long pa;

	pa = (nid & 0x3) | ((nid & (0x1f << 2)) << 1);
	pa <<= 34;

	return pa;
}

unsigned long
marvel_node_mem_size(int nid)
{
	return 16UL * 1024 * 1024 * 1024; /* 16GB */
}


/* 
 * AGP GART Support.
 */
#include <linux/agp_backend.h>
#include <asm/agp_backend.h>
#include <linux/slab.h>
#include <linux/delay.h>

struct marvel_agp_aperture {
	struct pci_iommu_arena *arena;
	long pg_start;
	long pg_count;
};

static int
marvel_agp_setup(alpha_agp_info *agp)
{
	struct marvel_agp_aperture *aper;

	if (!alpha_agpgart_size)
		return -ENOMEM;

	aper = kmalloc(sizeof(*aper), GFP_KERNEL);
	if (aper == NULL) return -ENOMEM;

	aper->arena = agp->hose->sg_pci;
	aper->pg_count = alpha_agpgart_size / PAGE_SIZE;
	aper->pg_start = iommu_reserve(aper->arena, aper->pg_count,
				       aper->pg_count - 1);

	if (aper->pg_start < 0) {
		printk(KERN_ERR "Failed to reserve AGP memory\n");
		kfree(aper);
		return -ENOMEM;
	}

	agp->aperture.bus_base = 
		aper->arena->dma_base + aper->pg_start * PAGE_SIZE;
	agp->aperture.size = aper->pg_count * PAGE_SIZE;
	agp->aperture.sysdata = aper;

	return 0;
}

static void
marvel_agp_cleanup(alpha_agp_info *agp)
{
	struct marvel_agp_aperture *aper = agp->aperture.sysdata;
	int status;

	status = iommu_release(aper->arena, aper->pg_start, aper->pg_count);
	if (status == -EBUSY) {
		printk(KERN_WARNING
		       "Attempted to release bound AGP memory - unbinding\n");
		iommu_unbind(aper->arena, aper->pg_start, aper->pg_count);
		status = iommu_release(aper->arena, aper->pg_start, 
				       aper->pg_count);
	}
	if (status < 0)
		printk(KERN_ERR "Failed to release AGP memory\n");

	kfree(aper);
	kfree(agp);
}

static int
marvel_agp_configure(alpha_agp_info *agp)
{
	io7_ioport_csrs *csrs = ((struct io7_port *)agp->hose->sysdata)->csrs;
	struct io7 *io7 = ((struct io7_port *)agp->hose->sysdata)->io7;
	unsigned int new_rate = 0;
	unsigned long agp_pll;

	/*
	 * Check the requested mode against the PLL setting.
	 * The agpgart_be code has not programmed the card yet,
	 * so we can still tweak mode here.
	 */
	agp_pll = io7->csrs->POx_RST[IO7_AGP_PORT].csr;
	switch(IO7_PLL_RNGB(agp_pll)) {
	case 0x4:				/* 2x only */
		/* 
		 * The PLL is only programmed for 2x, so adjust the
		 * rate to 2x, if necessary.
		 */
		if (agp->mode.bits.rate != 2) 
			new_rate = 2;
		break;

	case 0x6:				/* 1x / 4x */
		/*
		 * The PLL is programmed for 1x or 4x.  Don't go faster
		 * than requested, so if the requested rate is 2x, use 1x.
		 */
		if (agp->mode.bits.rate == 2) 
			new_rate = 1;
		break;

	default:				/* ??????? */
		/*
		 * Don't know what this PLL setting is, take the requested
		 * rate, but warn the user.
		 */
		printk("%s: unknown PLL setting RNGB=%lx (PLL6_CTL=%016lx)\n",
		       __FUNCTION__, IO7_PLL_RNGB(agp_pll), agp_pll);
		break;
	}

	/*
	 * Set the new rate, if necessary.
	 */
	if (new_rate) {
		printk("Requested AGP Rate %dX not compatible "
		       "with PLL setting - using %dX\n",
		       agp->mode.bits.rate,
		       new_rate);

		agp->mode.bits.rate = new_rate;
	}
		
	printk("Enabling AGP on hose %d: %dX%s RQ %d\n", 
	       agp->hose->index, agp->mode.bits.rate, 
	       agp->mode.bits.sba ? " - SBA" : "", agp->mode.bits.rq);

	csrs->AGP_CMD.csr = agp->mode.lw;

	return 0;
}

static int 
marvel_agp_bind_memory(alpha_agp_info *agp, off_t pg_start, struct agp_memory *mem)
{
	struct marvel_agp_aperture *aper = agp->aperture.sysdata;
	return iommu_bind(aper->arena, aper->pg_start + pg_start, 
			  mem->page_count, mem->memory);
}

static int 
marvel_agp_unbind_memory(alpha_agp_info *agp, off_t pg_start, struct agp_memory *mem)
{
	struct marvel_agp_aperture *aper = agp->aperture.sysdata;
	return iommu_unbind(aper->arena, aper->pg_start + pg_start,
			    mem->page_count);
}

static unsigned long
marvel_agp_translate(alpha_agp_info *agp, dma_addr_t addr)
{
	struct marvel_agp_aperture *aper = agp->aperture.sysdata;
	unsigned long baddr = addr - aper->arena->dma_base;
	unsigned long pte;

	if (addr < agp->aperture.bus_base ||
	    addr >= agp->aperture.bus_base + agp->aperture.size) {
		printk("%s: addr out of range\n", __FUNCTION__);
		return -EINVAL;
	}

	pte = aper->arena->ptes[baddr >> PAGE_SHIFT];
	if (!(pte & 1)) {
		printk("%s: pte not valid\n", __FUNCTION__);
		return -EINVAL;
	} 
	return (pte >> 1) << PAGE_SHIFT;
}

struct alpha_agp_ops marvel_agp_ops =
{
	.setup		= marvel_agp_setup,
	.cleanup	= marvel_agp_cleanup,
	.configure	= marvel_agp_configure,
	.bind		= marvel_agp_bind_memory,
	.unbind		= marvel_agp_unbind_memory,
	.translate	= marvel_agp_translate
};

alpha_agp_info *
marvel_agp_info(void)
{
	struct pci_controller *hose;
	io7_ioport_csrs *csrs;
	alpha_agp_info *agp;
	struct io7 *io7;

	/*
	 * Find the first IO7 with an AGP card.
	 *
	 * FIXME -- there should be a better way (we want to be able to
	 * specify and what if the agp card is not video???)
	 */
	hose = NULL;
	for (io7 = NULL; (io7 = marvel_next_io7(io7)) != NULL; ) {
		struct pci_controller *h;
		vuip addr;

		if (!io7->ports[IO7_AGP_PORT].enabled)
			continue;

		h = io7->ports[IO7_AGP_PORT].hose;
		addr = (vuip)build_conf_addr(h, 0, PCI_DEVFN(5, 0), 0);

		if (*addr != 0xffffffffu) {
			hose = h;
			break;
		}
	}

	if (!hose || !hose->sg_pci)
		return NULL;

	printk("MARVEL - using hose %d as AGP\n", hose->index);

	/* 
	 * Get the csrs from the hose.
	 */
	csrs = ((struct io7_port *)hose->sysdata)->csrs;

	/*
	 * Allocate the info structure.
	 */
	agp = kmalloc(sizeof(*agp), GFP_KERNEL);

	/*
	 * Fill it in.
	 */
	agp->hose = hose;
	agp->private = NULL;
	agp->ops = &marvel_agp_ops;

	/*
	 * Aperture - not configured until ops.setup().
	 */
	agp->aperture.bus_base = 0;
	agp->aperture.size = 0;
	agp->aperture.sysdata = NULL;

	/*
	 * Capabilities.
	 *
	 * NOTE: IO7 reports through AGP_STAT that it can support a read queue
	 *       depth of 17 (rq = 0x10). It actually only supports a depth of
	 * 	 16 (rq = 0xf).
	 */
	agp->capability.lw = csrs->AGP_STAT.csr;
	agp->capability.bits.rq = 0xf;
	
	/*
	 * Mode.
	 */
	agp->mode.lw = csrs->AGP_CMD.csr;

	return agp;
}

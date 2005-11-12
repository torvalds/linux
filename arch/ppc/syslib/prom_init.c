/*
 * Note that prom_init() and anything called from prom_init()
 * may be running at an address that is different from the address
 * that it was linked at.  References to static data items are
 * handled by compiling this file with -mrelocatable-lib.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/threads.h>
#include <linux/spinlock.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/bitops.h>

#include <asm/sections.h>
#include <asm/prom.h>
#include <asm/page.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/smp.h>
#include <asm/bootx.h>
#include <asm/system.h>
#include <asm/mmu.h>
#include <asm/pgtable.h>
#include <asm/bootinfo.h>
#include <asm/btext.h>
#include <asm/pci-bridge.h>
#include <asm/open_pic.h>
#include <asm/cacheflush.h>

#ifdef CONFIG_LOGO_LINUX_CLUT224
#include <linux/linux_logo.h>
extern const struct linux_logo logo_linux_clut224;
#endif

/*
 * Properties whose value is longer than this get excluded from our
 * copy of the device tree.  This way we don't waste space storing
 * things like "driver,AAPL,MacOS,PowerPC" properties.  But this value
 * does need to be big enough to ensure that we don't lose things
 * like the interrupt-map property on a PCI-PCI bridge.
 */
#define MAX_PROPERTY_LENGTH	4096

#ifndef FB_MAX			/* avoid pulling in all of the fb stuff */
#define FB_MAX	8
#endif

#define ALIGNUL(x) (((x) + sizeof(unsigned long)-1) & -sizeof(unsigned long))

typedef u32 prom_arg_t;

struct prom_args {
	const char *service;
	int nargs;
	int nret;
	prom_arg_t args[10];
};

struct pci_address {
	unsigned a_hi;
	unsigned a_mid;
	unsigned a_lo;
};

struct pci_reg_property {
	struct pci_address addr;
	unsigned size_hi;
	unsigned size_lo;
};

struct pci_range {
	struct pci_address addr;
	unsigned phys;
	unsigned size_hi;
	unsigned size_lo;
};

struct isa_reg_property {
	unsigned space;
	unsigned address;
	unsigned size;
};

struct pci_intr_map {
	struct pci_address addr;
	unsigned dunno;
	phandle int_ctrler;
	unsigned intr;
};

static void prom_exit(void);
static int  call_prom(const char *service, int nargs, int nret, ...);
static int  call_prom_ret(const char *service, int nargs, int nret,
			  prom_arg_t *rets, ...);
static void prom_print_hex(unsigned int v);
static int  prom_set_color(ihandle ih, int i, int r, int g, int b);
static int  prom_next_node(phandle *nodep);
static unsigned long check_display(unsigned long mem);
static void setup_disp_fake_bi(ihandle dp);
static unsigned long copy_device_tree(unsigned long mem_start,
				unsigned long mem_end);
static unsigned long inspect_node(phandle node, struct device_node *dad,
				unsigned long mem_start, unsigned long mem_end,
				struct device_node ***allnextpp);
static void prom_hold_cpus(unsigned long mem);
static void prom_instantiate_rtas(void);
static void * early_get_property(unsigned long base, unsigned long node,
				char *prop);

prom_entry prom __initdata;
ihandle prom_chosen __initdata;
ihandle prom_stdout __initdata;

static char *prom_display_paths[FB_MAX] __initdata;
static phandle prom_display_nodes[FB_MAX] __initdata;
static unsigned int prom_num_displays __initdata;
static ihandle prom_disp_node __initdata;
char *of_stdout_device __initdata;

unsigned int rtas_data;   /* physical pointer */
unsigned int rtas_entry;  /* physical pointer */
unsigned int rtas_size;
unsigned int old_rtas;

boot_infos_t *boot_infos;
char *bootpath;
char *bootdevice;
struct device_node *allnodes;

extern char *klimit;

static void __init
prom_exit(void)
{
	struct prom_args args;

	args.service = "exit";
	args.nargs = 0;
	args.nret = 0;
	prom(&args);
	for (;;)			/* should never get here */
		;
}

static int __init
call_prom(const char *service, int nargs, int nret, ...)
{
	va_list list;
	int i;
	struct prom_args prom_args;

	prom_args.service = service;
	prom_args.nargs = nargs;
	prom_args.nret = nret;
	va_start(list, nret);
	for (i = 0; i < nargs; ++i)
		prom_args.args[i] = va_arg(list, prom_arg_t);
	va_end(list);
	for (i = 0; i < nret; ++i)
		prom_args.args[i + nargs] = 0;
	prom(&prom_args);
	return prom_args.args[nargs];
}

static int __init
call_prom_ret(const char *service, int nargs, int nret, prom_arg_t *rets, ...)
{
	va_list list;
	int i;
	struct prom_args prom_args;

	prom_args.service = service;
	prom_args.nargs = nargs;
	prom_args.nret = nret;
	va_start(list, rets);
	for (i = 0; i < nargs; ++i)
		prom_args.args[i] = va_arg(list, int);
	va_end(list);
	for (i = 0; i < nret; ++i)
		prom_args.args[i + nargs] = 0;
	prom(&prom_args);
	for (i = 1; i < nret; ++i)
		rets[i-1] = prom_args.args[nargs + i];
	return prom_args.args[nargs];
}

void __init
prom_print(const char *msg)
{
	const char *p, *q;

	if (prom_stdout == 0)
		return;

	for (p = msg; *p != 0; p = q) {
		for (q = p; *q != 0 && *q != '\n'; ++q)
			;
		if (q > p)
			call_prom("write", 3, 1, prom_stdout, p, q - p);
		if (*q != 0) {
			++q;
			call_prom("write", 3, 1, prom_stdout, "\r\n", 2);
		}
	}
}

static void __init
prom_print_hex(unsigned int v)
{
	char buf[16];
	int i, c;

	for (i = 0; i < 8; ++i) {
		c = (v >> ((7-i)*4)) & 0xf;
		c += (c >= 10)? ('a' - 10): '0';
		buf[i] = c;
	}
	buf[i] = ' ';
	buf[i+1] = 0;
	prom_print(buf);
}

static int __init
prom_set_color(ihandle ih, int i, int r, int g, int b)
{
	return call_prom("call-method", 6, 1, "color!", ih, i, b, g, r);
}

static int __init
prom_next_node(phandle *nodep)
{
	phandle node;

	if ((node = *nodep) != 0
	    && (*nodep = call_prom("child", 1, 1, node)) != 0)
		return 1;
	if ((*nodep = call_prom("peer", 1, 1, node)) != 0)
		return 1;
	for (;;) {
		if ((node = call_prom("parent", 1, 1, node)) == 0)
			return 0;
		if ((*nodep = call_prom("peer", 1, 1, node)) != 0)
			return 1;
	}
}

#ifdef CONFIG_POWER4
/*
 * Set up a hash table with a set of entries in it to map the
 * first 64MB of RAM.  This is used on 64-bit machines since
 * some of them don't have BATs.
 */

static inline void make_pte(unsigned long htab, unsigned int hsize,
			    unsigned int va, unsigned int pa, int mode)
{
	unsigned int *pteg;
	unsigned int hash, i, vsid;

	vsid = ((va >> 28) * 0x111) << 12;
	hash = ((va ^ vsid) >> 5) & 0x7fff80;
	pteg = (unsigned int *)(htab + (hash & (hsize - 1)));
	for (i = 0; i < 8; ++i, pteg += 4) {
		if ((pteg[1] & 1) == 0) {
			pteg[1] = vsid | ((va >> 16) & 0xf80) | 1;
			pteg[3] = pa | mode;
			break;
		}
	}
}

extern unsigned long _SDR1;
extern PTE *Hash;
extern unsigned long Hash_size;

static void __init
prom_alloc_htab(void)
{
	unsigned int hsize;
	unsigned long htab;
	unsigned int addr;

	/*
	 * Because of OF bugs we can't use the "claim" client
	 * interface to allocate memory for the hash table.
	 * This code is only used on 64-bit PPCs, and the only
	 * 64-bit PPCs at the moment are RS/6000s, and their
	 * OF is based at 0xc00000 (the 12M point), so we just
	 * arbitrarily use the 0x800000 - 0xc00000 region for the
	 * hash table.
	 *  -- paulus.
	 */
	hsize = 4 << 20;	/* POWER4 has no BATs */
	htab = (8 << 20);
	call_prom("claim", 3, 1, htab, hsize, 0);
	Hash = (void *)(htab + KERNELBASE);
	Hash_size = hsize;
	_SDR1 = htab + __ilog2(hsize) - 18;

	/*
	 * Put in PTEs for the first 64MB of RAM
	 */
	memset((void *)htab, 0, hsize);
	for (addr = 0; addr < 0x4000000; addr += 0x1000)
		make_pte(htab, hsize, addr + KERNELBASE, addr,
			 _PAGE_ACCESSED | _PAGE_COHERENT | PP_RWXX);
#if 0 /* DEBUG stuff mapping the SCC */
	make_pte(htab, hsize, 0x80013000, 0x80013000,
		 _PAGE_ACCESSED | _PAGE_NO_CACHE | _PAGE_GUARDED | PP_RWXX);
#endif
}
#endif /* CONFIG_POWER4 */


/*
 * If we have a display that we don't know how to drive,
 * we will want to try to execute OF's open method for it
 * later.  However, OF will probably fall over if we do that
 * we've taken over the MMU.
 * So we check whether we will need to open the display,
 * and if so, open it now.
 */
static unsigned long __init
check_display(unsigned long mem)
{
	phandle node;
	ihandle ih;
	int i, j;
	char type[16], *path;
	static unsigned char default_colors[] = {
		0x00, 0x00, 0x00,
		0x00, 0x00, 0xaa,
		0x00, 0xaa, 0x00,
		0x00, 0xaa, 0xaa,
		0xaa, 0x00, 0x00,
		0xaa, 0x00, 0xaa,
		0xaa, 0xaa, 0x00,
		0xaa, 0xaa, 0xaa,
		0x55, 0x55, 0x55,
		0x55, 0x55, 0xff,
		0x55, 0xff, 0x55,
		0x55, 0xff, 0xff,
		0xff, 0x55, 0x55,
		0xff, 0x55, 0xff,
		0xff, 0xff, 0x55,
		0xff, 0xff, 0xff
	};
	const unsigned char *clut;

	prom_disp_node = 0;

	for (node = 0; prom_next_node(&node); ) {
		type[0] = 0;
		call_prom("getprop", 4, 1, node, "device_type",
			  type, sizeof(type));
		if (strcmp(type, "display") != 0)
			continue;
		/* It seems OF doesn't null-terminate the path :-( */
		path = (char *) mem;
		memset(path, 0, 256);
		if (call_prom("package-to-path", 3, 1, node, path, 255) < 0)
			continue;

		/*
		 * If this display is the device that OF is using for stdout,
		 * move it to the front of the list.
		 */
		mem += strlen(path) + 1;
		i = prom_num_displays++;
		if (of_stdout_device != 0 && i > 0
		    && strcmp(of_stdout_device, path) == 0) {
			for (; i > 0; --i) {
				prom_display_paths[i]
					= prom_display_paths[i-1];
				prom_display_nodes[i]
					= prom_display_nodes[i-1];
			}
		}
		prom_display_paths[i] = path;
		prom_display_nodes[i] = node;
		if (i == 0)
			prom_disp_node = node;
		if (prom_num_displays >= FB_MAX)
			break;
	}

	for (j=0; j<prom_num_displays; j++) {
		path = prom_display_paths[j];
		node = prom_display_nodes[j];
		prom_print("opening display ");
		prom_print(path);
		ih = call_prom("open", 1, 1, path);
		if (ih == 0 || ih == (ihandle) -1) {
			prom_print("... failed\n");
			for (i=j+1; i<prom_num_displays; i++) {
				prom_display_paths[i-1] = prom_display_paths[i];
				prom_display_nodes[i-1] = prom_display_nodes[i];
			}
			if (--prom_num_displays > 0) {
				prom_disp_node = prom_display_nodes[j];
				j--;
			} else
				prom_disp_node = 0;
			continue;
		} else {
			prom_print("... ok\n");
			call_prom("setprop", 4, 1, node, "linux,opened", 0, 0);

			/*
			 * Setup a usable color table when the appropriate
			 * method is available.
			 * Should update this to use set-colors.
			 */
			clut = default_colors;
			for (i = 0; i < 32; i++, clut += 3)
				if (prom_set_color(ih, i, clut[0], clut[1],
						   clut[2]) != 0)
					break;

#ifdef CONFIG_LOGO_LINUX_CLUT224
			clut = PTRRELOC(logo_linux_clut224.clut);
			for (i = 0; i < logo_linux_clut224.clutsize;
			     i++, clut += 3)
				if (prom_set_color(ih, i + 32, clut[0],
						   clut[1], clut[2]) != 0)
					break;
#endif /* CONFIG_LOGO_LINUX_CLUT224 */
		}
	}
	
	if (prom_stdout) {
		phandle p;
		p = call_prom("instance-to-package", 1, 1, prom_stdout);
		if (p && p != -1) {
			type[0] = 0;
			call_prom("getprop", 4, 1, p, "device_type",
				  type, sizeof(type));
			if (strcmp(type, "display") == 0)
				call_prom("setprop", 4, 1, p, "linux,boot-display",
					  0, 0);
		}
	}

	return ALIGNUL(mem);
}

/* This function will enable the early boot text when doing OF booting. This
 * way, xmon output should work too
 */
static void __init
setup_disp_fake_bi(ihandle dp)
{
#ifdef CONFIG_BOOTX_TEXT
	int width = 640, height = 480, depth = 8, pitch;
	unsigned address;
	struct pci_reg_property addrs[8];
	int i, naddrs;
	char name[32];
	char *getprop = "getprop";

	prom_print("Initializing fake screen: ");

	memset(name, 0, sizeof(name));
	call_prom(getprop, 4, 1, dp, "name", name, sizeof(name));
	name[sizeof(name)-1] = 0;
	prom_print(name);
	prom_print("\n");
	call_prom(getprop, 4, 1, dp, "width", &width, sizeof(width));
	call_prom(getprop, 4, 1, dp, "height", &height, sizeof(height));
	call_prom(getprop, 4, 1, dp, "depth", &depth, sizeof(depth));
	pitch = width * ((depth + 7) / 8);
	call_prom(getprop, 4, 1, dp, "linebytes",
		  &pitch, sizeof(pitch));
	if (pitch == 1)
		pitch = 0x1000;		/* for strange IBM display */
	address = 0;
	call_prom(getprop, 4, 1, dp, "address",
		  &address, sizeof(address));
	if (address == 0) {
		/* look for an assigned address with a size of >= 1MB */
		naddrs = call_prom(getprop, 4, 1, dp, "assigned-addresses",
				   addrs, sizeof(addrs));
		naddrs /= sizeof(struct pci_reg_property);
		for (i = 0; i < naddrs; ++i) {
			if (addrs[i].size_lo >= (1 << 20)) {
				address = addrs[i].addr.a_lo;
				/* use the BE aperture if possible */
				if (addrs[i].size_lo >= (16 << 20))
					address += (8 << 20);
				break;
			}
		}
		if (address == 0) {
			prom_print("Failed to get address\n");
			return;
		}
	}
	/* kludge for valkyrie */
	if (strcmp(name, "valkyrie") == 0)
		address += 0x1000;

#ifdef CONFIG_POWER4
#if CONFIG_TASK_SIZE > 0x80000000
#error CONFIG_TASK_SIZE cannot be above 0x80000000 with BOOTX_TEXT on G5
#endif
	{
		extern boot_infos_t disp_bi;
		unsigned long va, pa, i, offset;
       		va = 0x90000000;
		pa = address & 0xfffff000ul;
		offset = address & 0x00000fff;

		for (i=0; i<0x4000; i++) {  
			make_pte((unsigned long)Hash - KERNELBASE, Hash_size, va, pa, 
				 _PAGE_ACCESSED | _PAGE_NO_CACHE |
				 _PAGE_GUARDED | PP_RWXX);
			va += 0x1000;
			pa += 0x1000;
		}
		btext_setup_display(width, height, depth, pitch, 0x90000000 | offset);
		disp_bi.dispDeviceBase = (u8 *)address;
	}
#else /* CONFIG_POWER4 */
	btext_setup_display(width, height, depth, pitch, address);
	btext_prepare_BAT();
#endif /* CONFIG_POWER4 */
#endif /* CONFIG_BOOTX_TEXT */
}

/*
 * Make a copy of the device tree from the PROM.
 */
static unsigned long __init
copy_device_tree(unsigned long mem_start, unsigned long mem_end)
{
	phandle root;
	unsigned long new_start;
	struct device_node **allnextp;

	root = call_prom("peer", 1, 1, (phandle)0);
	if (root == (phandle)0) {
		prom_print("couldn't get device tree root\n");
		prom_exit();
	}
	allnextp = &allnodes;
	mem_start = ALIGNUL(mem_start);
	new_start = inspect_node(root, NULL, mem_start, mem_end, &allnextp);
	*allnextp = NULL;
	return new_start;
}

static unsigned long __init
inspect_node(phandle node, struct device_node *dad,
	     unsigned long mem_start, unsigned long mem_end,
	     struct device_node ***allnextpp)
{
	int l;
	phandle child;
	struct device_node *np;
	struct property *pp, **prev_propp;
	char *prev_name, *namep;
	unsigned char *valp;

	np = (struct device_node *) mem_start;
	mem_start += sizeof(struct device_node);
	memset(np, 0, sizeof(*np));
	np->node = node;
	**allnextpp = PTRUNRELOC(np);
	*allnextpp = &np->allnext;
	if (dad != 0) {
		np->parent = PTRUNRELOC(dad);
		/* we temporarily use the `next' field as `last_child'. */
		if (dad->next == 0)
			dad->child = PTRUNRELOC(np);
		else
			dad->next->sibling = PTRUNRELOC(np);
		dad->next = np;
	}

	/* get and store all properties */
	prev_propp = &np->properties;
	prev_name = "";
	for (;;) {
		pp = (struct property *) mem_start;
		namep = (char *) (pp + 1);
		pp->name = PTRUNRELOC(namep);
		if (call_prom("nextprop", 3, 1, node, prev_name, namep) <= 0)
			break;
		mem_start = ALIGNUL((unsigned long)namep + strlen(namep) + 1);
		prev_name = namep;
		valp = (unsigned char *) mem_start;
		pp->value = PTRUNRELOC(valp);
		pp->length = call_prom("getprop", 4, 1, node, namep,
				       valp, mem_end - mem_start);
		if (pp->length < 0)
			continue;
#ifdef MAX_PROPERTY_LENGTH
		if (pp->length > MAX_PROPERTY_LENGTH)
			continue; /* ignore this property */
#endif
		mem_start = ALIGNUL(mem_start + pp->length);
		*prev_propp = PTRUNRELOC(pp);
		prev_propp = &pp->next;
	}
	if (np->node != 0) {
		/* Add a "linux,phandle" property" */
		pp = (struct property *) mem_start;
		*prev_propp = PTRUNRELOC(pp);
		prev_propp = &pp->next;
		namep = (char *) (pp + 1);
		pp->name = PTRUNRELOC(namep);
		strcpy(namep, "linux,phandle");
		mem_start = ALIGNUL((unsigned long)namep + strlen(namep) + 1);
		pp->value = (unsigned char *) PTRUNRELOC(&np->node);
		pp->length = sizeof(np->node);
	}
	*prev_propp = NULL;

	/* get the node's full name */
	l = call_prom("package-to-path", 3, 1, node,
		      mem_start, mem_end - mem_start);
	if (l >= 0) {
		char *p, *ep;

		np->full_name = PTRUNRELOC((char *) mem_start);
		*(char *)(mem_start + l) = 0;
		/* Fixup an Apple bug where they have bogus \0 chars in the
		 * middle of the path in some properties
		 */
		for (p = (char *)mem_start, ep = p + l; p < ep; p++)
			if ((*p) == '\0') {
				memmove(p, p+1, ep - p);
				ep--;
			}
		mem_start = ALIGNUL(mem_start + l + 1);
	}

	/* do all our children */
	child = call_prom("child", 1, 1, node);
	while (child != 0) {
		mem_start = inspect_node(child, np, mem_start, mem_end,
					 allnextpp);
		child = call_prom("peer", 1, 1, child);
	}

	return mem_start;
}

unsigned long smp_chrp_cpu_nr __initdata = 0;

/*
 * With CHRP SMP we need to use the OF to start the other
 * processors so we can't wait until smp_boot_cpus (the OF is
 * trashed by then) so we have to put the processors into
 * a holding pattern controlled by the kernel (not OF) before
 * we destroy the OF.
 *
 * This uses a chunk of high memory, puts some holding pattern
 * code there and sends the other processors off to there until
 * smp_boot_cpus tells them to do something.  We do that by using
 * physical address 0x0.  The holding pattern checks that address
 * until its cpu # is there, when it is that cpu jumps to
 * __secondary_start().  smp_boot_cpus() takes care of setting those
 * values.
 *
 * We also use physical address 0x4 here to tell when a cpu
 * is in its holding pattern code.
 *
 * -- Cort
 *
 * Note that we have to do this if we have more than one CPU,
 * even if this is a UP kernel.  Otherwise when we trash OF
 * the other CPUs will start executing some random instructions
 * and crash the system.  -- paulus
 */
static void __init
prom_hold_cpus(unsigned long mem)
{
	extern void __secondary_hold(void);
	unsigned long i;
	int cpu;
	phandle node;
	char type[16], *path;
	unsigned int reg;

	/*
	 * XXX: hack to make sure we're chrp, assume that if we're
	 *      chrp we have a device_type property -- Cort
	 */
	node = call_prom("finddevice", 1, 1, "/");
	if (call_prom("getprop", 4, 1, node,
		      "device_type", type, sizeof(type)) <= 0)
		return;

	/* copy the holding pattern code to someplace safe (0) */
	/* the holding pattern is now within the first 0x100
	   bytes of the kernel image -- paulus */
	memcpy((void *)0, _stext, 0x100);
	flush_icache_range(0, 0x100);

	/* look for cpus */
	*(unsigned long *)(0x0) = 0;
	asm volatile("dcbf 0,%0": : "r" (0) : "memory");
	for (node = 0; prom_next_node(&node); ) {
		type[0] = 0;
		call_prom("getprop", 4, 1, node, "device_type",
			  type, sizeof(type));
		if (strcmp(type, "cpu") != 0)
			continue;
		path = (char *) mem;
		memset(path, 0, 256);
		if (call_prom("package-to-path", 3, 1, node, path, 255) < 0)
			continue;
		reg = -1;
		call_prom("getprop", 4, 1, node, "reg", &reg, sizeof(reg));
		cpu = smp_chrp_cpu_nr++;
#ifdef CONFIG_SMP
		smp_hw_index[cpu] = reg;
#endif /* CONFIG_SMP */
		/* XXX: hack - don't start cpu 0, this cpu -- Cort */
		if (cpu == 0)
			continue;
		prom_print("starting cpu ");
		prom_print(path);
		*(ulong *)(0x4) = 0;
		call_prom("start-cpu", 3, 0, node,
			  (char *)__secondary_hold - _stext, cpu);
		prom_print("...");
		for ( i = 0 ; (i < 10000) && (*(ulong *)(0x4) == 0); i++ )
			;
		if (*(ulong *)(0x4) == cpu)
			prom_print("ok\n");
		else {
			prom_print("failed: ");
			prom_print_hex(*(ulong *)0x4);
			prom_print("\n");
		}
	}
}

static void __init
prom_instantiate_rtas(void)
{
	ihandle prom_rtas;
	prom_arg_t result;

	prom_rtas = call_prom("finddevice", 1, 1, "/rtas");
	if (prom_rtas == -1)
		return;

	rtas_size = 0;
	call_prom("getprop", 4, 1, prom_rtas,
		  "rtas-size", &rtas_size, sizeof(rtas_size));
	prom_print("instantiating rtas");
	if (rtas_size == 0) {
		rtas_data = 0;
	} else {
		/*
		 * Ask OF for some space for RTAS.
		 * Actually OF has bugs so we just arbitrarily
		 * use memory at the 6MB point.
		 */
		rtas_data = 6 << 20;
		prom_print(" at ");
		prom_print_hex(rtas_data);
	}

	prom_rtas = call_prom("open", 1, 1, "/rtas");
	prom_print("...");
	rtas_entry = 0;
	if (call_prom_ret("call-method", 3, 2, &result,
			  "instantiate-rtas", prom_rtas, rtas_data) == 0)
		rtas_entry = result;
	if ((rtas_entry == -1) || (rtas_entry == 0))
		prom_print(" failed\n");
	else
		prom_print(" done\n");
}

/*
 * We enter here early on, when the Open Firmware prom is still
 * handling exceptions and the MMU hash table for us.
 */
unsigned long __init
prom_init(int r3, int r4, prom_entry pp)
{
	unsigned long mem;
	ihandle prom_mmu;
	unsigned long offset = reloc_offset();
	int i, l;
	char *p, *d;
 	unsigned long phys;
	prom_arg_t result[3];
	char model[32];
	phandle node;
	int rc;

 	/* Default */
 	phys = (unsigned long) &_stext;

	/* First get a handle for the stdout device */
	prom = pp;
	prom_chosen = call_prom("finddevice", 1, 1, "/chosen");
	if (prom_chosen == -1)
		prom_exit();
	if (call_prom("getprop", 4, 1, prom_chosen, "stdout",
		      &prom_stdout, sizeof(prom_stdout)) <= 0)
		prom_exit();

	/* Get the full OF pathname of the stdout device */
	mem = (unsigned long) klimit + offset;
	p = (char *) mem;
	memset(p, 0, 256);
	call_prom("instance-to-path", 3, 1, prom_stdout, p, 255);
	of_stdout_device = p;
	mem += strlen(p) + 1;

	/* Get the boot device and translate it to a full OF pathname. */
	p = (char *) mem;
	l = call_prom("getprop", 4, 1, prom_chosen, "bootpath", p, 1<<20);
	if (l > 0) {
		p[l] = 0;	/* should already be null-terminated */
		bootpath = PTRUNRELOC(p);
		mem += l + 1;
		d = (char *) mem;
		*d = 0;
		call_prom("canon", 3, 1, p, d, 1<<20);
		bootdevice = PTRUNRELOC(d);
		mem = ALIGNUL(mem + strlen(d) + 1);
	}

	prom_instantiate_rtas();

#ifdef CONFIG_POWER4
	/*
	 * Find out how much memory we have and allocate a
	 * suitably-sized hash table.
	 */
	prom_alloc_htab();
#endif
	mem = check_display(mem);

	prom_print("copying OF device tree...");
	mem = copy_device_tree(mem, mem + (1<<20));
	prom_print("done\n");

	prom_hold_cpus(mem);

	klimit = (char *) (mem - offset);

	node = call_prom("finddevice", 1, 1, "/");
	rc = call_prom("getprop", 4, 1, node, "model", model, sizeof(model));
	if (rc > 0 && !strncmp (model, "Pegasos", 7)
		&& strncmp (model, "Pegasos2", 8)) {
		/* Pegasos 1 has a broken translate method in the OF,
		 * and furthermore the BATs are mapped 1:1 so the phys
		 * address calculated above is correct, so let's use
		 * it directly.
		 */
	} else if (offset == 0) {
		/* If we are already running at 0xc0000000, we assume we were
	 	 * loaded by an OF bootloader which did set a BAT for us.
	 	 * This breaks OF translate so we force phys to be 0.
	 	 */
		prom_print("(already at 0xc0000000) phys=0\n");
		phys = 0;
	} else if (call_prom("getprop", 4, 1, prom_chosen, "mmu",
			     &prom_mmu, sizeof(prom_mmu)) <= 0) {
		prom_print(" no MMU found\n");
	} else if (call_prom_ret("call-method", 4, 4, result, "translate",
				 prom_mmu, &_stext, 1) != 0) {
		prom_print(" (translate failed)\n");
	} else {
		/* We assume the phys. address size is 3 cells */
		phys = result[2];
	}

	if (prom_disp_node != 0)
		setup_disp_fake_bi(prom_disp_node);

	/* Use quiesce call to get OF to shut down any devices it's using */
	prom_print("Calling quiesce ...\n");
	call_prom("quiesce", 0, 0);

	/* Relocate various pointers which will be used once the
	   kernel is running at the address it was linked at. */
	for (i = 0; i < prom_num_displays; ++i)
		prom_display_paths[i] = PTRUNRELOC(prom_display_paths[i]);

#ifdef CONFIG_SERIAL_CORE_CONSOLE
	/* Relocate the of stdout for console autodetection */
	of_stdout_device = PTRUNRELOC(of_stdout_device);
#endif

	prom_print("returning 0x");
	prom_print_hex(phys);
	prom_print("from prom_init\n");
	prom_stdout = 0;

	return phys;
}

/*
 * early_get_property is used to access the device tree image prepared
 * by BootX very early on, before the pointers in it have been relocated.
 */
static void * __init
early_get_property(unsigned long base, unsigned long node, char *prop)
{
	struct device_node *np = (struct device_node *)(base + node);
	struct property *pp;

	for (pp = np->properties; pp != 0; pp = pp->next) {
		pp = (struct property *) (base + (unsigned long)pp);
		if (strcmp((char *)((unsigned long)pp->name + base),
			   prop) == 0) {
			return (void *)((unsigned long)pp->value + base);
		}
	}
	return NULL;
}

/* Is boot-info compatible ? */
#define BOOT_INFO_IS_COMPATIBLE(bi)		((bi)->compatible_version <= BOOT_INFO_VERSION)
#define BOOT_INFO_IS_V2_COMPATIBLE(bi)	((bi)->version >= 2)
#define BOOT_INFO_IS_V4_COMPATIBLE(bi)	((bi)->version >= 4)

void __init
bootx_init(unsigned long r4, unsigned long phys)
{
	boot_infos_t *bi = (boot_infos_t *) r4;
	unsigned long space;
	unsigned long ptr, x;
	char *model;

	boot_infos = PTRUNRELOC(bi);
	if (!BOOT_INFO_IS_V2_COMPATIBLE(bi))
		bi->logicalDisplayBase = NULL;

#ifdef CONFIG_BOOTX_TEXT
	btext_init(bi);

	/*
	 * Test if boot-info is compatible.  Done only in config
	 * CONFIG_BOOTX_TEXT since there is nothing much we can do
	 * with an incompatible version, except display a message
	 * and eventually hang the processor...
	 *
	 * I'll try to keep enough of boot-info compatible in the
	 * future to always allow display of this message;
	 */
	if (!BOOT_INFO_IS_COMPATIBLE(bi)) {
		btext_drawstring(" !!! WARNING - Incompatible version of BootX !!!\n\n\n");
		btext_flushscreen();
	}
#endif	/* CONFIG_BOOTX_TEXT */

	/* New BootX enters kernel with MMU off, i/os are not allowed
	   here. This hack will have been done by the boostrap anyway.
	*/
	if (bi->version < 4) {
		/*
		 * XXX If this is an iMac, turn off the USB controller.
		 */
		model = (char *) early_get_property
			(r4 + bi->deviceTreeOffset, 4, "model");
		if (model
		    && (strcmp(model, "iMac,1") == 0
			|| strcmp(model, "PowerMac1,1") == 0)) {
			out_le32((unsigned *)0x80880008, 1);	/* XXX */
		}
	}

	/* Move klimit to enclose device tree, args, ramdisk, etc... */
	if (bi->version < 5) {
		space = bi->deviceTreeOffset + bi->deviceTreeSize;
		if (bi->ramDisk)
			space = bi->ramDisk + bi->ramDiskSize;
	} else
		space = bi->totalParamsSize;
	klimit = PTRUNRELOC((char *) bi + space);

	/* New BootX will have flushed all TLBs and enters kernel with
	   MMU switched OFF, so this should not be useful anymore.
	*/
	if (bi->version < 4) {
		/*
		 * Touch each page to make sure the PTEs for them
		 * are in the hash table - the aim is to try to avoid
		 * getting DSI exceptions while copying the kernel image.
		 */
		for (ptr = ((unsigned long) &_stext) & PAGE_MASK;
		     ptr < (unsigned long)bi + space; ptr += PAGE_SIZE)
			x = *(volatile unsigned long *)ptr;
	}

#ifdef CONFIG_BOOTX_TEXT
	/*
	 * Note that after we call btext_prepare_BAT, we can't do
	 * prom_draw*, flushscreen or clearscreen until we turn the MMU
	 * on, since btext_prepare_BAT sets disp_bi.logicalDisplayBase
	 * to a virtual address.
	 */
	btext_prepare_BAT();
#endif
}

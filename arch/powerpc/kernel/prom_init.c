/*
 * Procedures for interfacing to Open Firmware.
 *
 * Paul Mackerras	August 1996.
 * Copyright (C) 1996-2005 Paul Mackerras.
 * 
 *  Adapted for 64bit PowerPC by Dave Engebretsen and Peter Bergner.
 *    {engebret|bergner}@us.ibm.com 
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#undef DEBUG_PROM

#include <stdarg.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/threads.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>
#include <linux/stringify.h>
#include <linux/delay.h>
#include <linux/initrd.h>
#include <linux/bitops.h>
#include <asm/prom.h>
#include <asm/rtas.h>
#include <asm/page.h>
#include <asm/processor.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/smp.h>
#include <asm/mmu.h>
#include <asm/pgtable.h>
#include <asm/pci.h>
#include <asm/iommu.h>
#include <asm/btext.h>
#include <asm/sections.h>
#include <asm/machdep.h>
#include <asm/opal.h>

#include <linux/linux_logo.h>

/*
 * Eventually bump that one up
 */
#define DEVTREE_CHUNK_SIZE	0x100000

/*
 * This is the size of the local memory reserve map that gets copied
 * into the boot params passed to the kernel. That size is totally
 * flexible as the kernel just reads the list until it encounters an
 * entry with size 0, so it can be changed without breaking binary
 * compatibility
 */
#define MEM_RESERVE_MAP_SIZE	8

/*
 * prom_init() is called very early on, before the kernel text
 * and data have been mapped to KERNELBASE.  At this point the code
 * is running at whatever address it has been loaded at.
 * On ppc32 we compile with -mrelocatable, which means that references
 * to extern and static variables get relocated automatically.
 * ppc64 objects are always relocatable, we just need to relocate the
 * TOC.
 *
 * Because OF may have mapped I/O devices into the area starting at
 * KERNELBASE, particularly on CHRP machines, we can't safely call
 * OF once the kernel has been mapped to KERNELBASE.  Therefore all
 * OF calls must be done within prom_init().
 *
 * ADDR is used in calls to call_prom.  The 4th and following
 * arguments to call_prom should be 32-bit values.
 * On ppc64, 64 bit values are truncated to 32 bits (and
 * fortunately don't get interpreted as two arguments).
 */
#define ADDR(x)		(u32)(unsigned long)(x)

#ifdef CONFIG_PPC64
#define OF_WORKAROUNDS	0
#else
#define OF_WORKAROUNDS	of_workarounds
int of_workarounds;
#endif

#define OF_WA_CLAIM	1	/* do phys/virt claim separately, then map */
#define OF_WA_LONGTRAIL	2	/* work around longtrail bugs */

#define PROM_BUG() do {						\
        prom_printf("kernel BUG at %s line 0x%x!\n",		\
		    __FILE__, __LINE__);			\
        __asm__ __volatile__(".long " BUG_ILLEGAL_INSTR);	\
} while (0)

#ifdef DEBUG_PROM
#define prom_debug(x...)	prom_printf(x)
#else
#define prom_debug(x...)
#endif


typedef u32 prom_arg_t;

struct prom_args {
        __be32 service;
        __be32 nargs;
        __be32 nret;
        __be32 args[10];
};

struct prom_t {
	ihandle root;
	phandle chosen;
	int cpu;
	ihandle stdout;
	ihandle mmumap;
	ihandle memory;
};

struct mem_map_entry {
	__be64	base;
	__be64	size;
};

typedef __be32 cell_t;

extern void __start(unsigned long r3, unsigned long r4, unsigned long r5,
		    unsigned long r6, unsigned long r7, unsigned long r8,
		    unsigned long r9);

#ifdef CONFIG_PPC64
extern int enter_prom(struct prom_args *args, unsigned long entry);
#else
static inline int enter_prom(struct prom_args *args, unsigned long entry)
{
	return ((int (*)(struct prom_args *))entry)(args);
}
#endif

extern void copy_and_flush(unsigned long dest, unsigned long src,
			   unsigned long size, unsigned long offset);

/* prom structure */
static struct prom_t __initdata prom;

static unsigned long prom_entry __initdata;

#define PROM_SCRATCH_SIZE 256

static char __initdata of_stdout_device[256];
static char __initdata prom_scratch[PROM_SCRATCH_SIZE];

static unsigned long __initdata dt_header_start;
static unsigned long __initdata dt_struct_start, dt_struct_end;
static unsigned long __initdata dt_string_start, dt_string_end;

static unsigned long __initdata prom_initrd_start, prom_initrd_end;

#ifdef CONFIG_PPC64
static int __initdata prom_iommu_force_on;
static int __initdata prom_iommu_off;
static unsigned long __initdata prom_tce_alloc_start;
static unsigned long __initdata prom_tce_alloc_end;
#endif

/* Platforms codes are now obsolete in the kernel. Now only used within this
 * file and ultimately gone too. Feel free to change them if you need, they
 * are not shared with anything outside of this file anymore
 */
#define PLATFORM_PSERIES	0x0100
#define PLATFORM_PSERIES_LPAR	0x0101
#define PLATFORM_LPAR		0x0001
#define PLATFORM_POWERMAC	0x0400
#define PLATFORM_GENERIC	0x0500
#define PLATFORM_OPAL		0x0600

static int __initdata of_platform;

static char __initdata prom_cmd_line[COMMAND_LINE_SIZE];

static unsigned long __initdata prom_memory_limit;

static unsigned long __initdata alloc_top;
static unsigned long __initdata alloc_top_high;
static unsigned long __initdata alloc_bottom;
static unsigned long __initdata rmo_top;
static unsigned long __initdata ram_top;

static struct mem_map_entry __initdata mem_reserve_map[MEM_RESERVE_MAP_SIZE];
static int __initdata mem_reserve_cnt;

static cell_t __initdata regbuf[1024];


/*
 * Error results ... some OF calls will return "-1" on error, some
 * will return 0, some will return either. To simplify, here are
 * macros to use with any ihandle or phandle return value to check if
 * it is valid
 */

#define PROM_ERROR		(-1u)
#define PHANDLE_VALID(p)	((p) != 0 && (p) != PROM_ERROR)
#define IHANDLE_VALID(i)	((i) != 0 && (i) != PROM_ERROR)


/* This is the one and *ONLY* place where we actually call open
 * firmware.
 */

static int __init call_prom(const char *service, int nargs, int nret, ...)
{
	int i;
	struct prom_args args;
	va_list list;

	args.service = cpu_to_be32(ADDR(service));
	args.nargs = cpu_to_be32(nargs);
	args.nret = cpu_to_be32(nret);

	va_start(list, nret);
	for (i = 0; i < nargs; i++)
		args.args[i] = cpu_to_be32(va_arg(list, prom_arg_t));
	va_end(list);

	for (i = 0; i < nret; i++)
		args.args[nargs+i] = 0;

	if (enter_prom(&args, prom_entry) < 0)
		return PROM_ERROR;

	return (nret > 0) ? be32_to_cpu(args.args[nargs]) : 0;
}

static int __init call_prom_ret(const char *service, int nargs, int nret,
				prom_arg_t *rets, ...)
{
	int i;
	struct prom_args args;
	va_list list;

	args.service = cpu_to_be32(ADDR(service));
	args.nargs = cpu_to_be32(nargs);
	args.nret = cpu_to_be32(nret);

	va_start(list, rets);
	for (i = 0; i < nargs; i++)
		args.args[i] = cpu_to_be32(va_arg(list, prom_arg_t));
	va_end(list);

	for (i = 0; i < nret; i++)
		args.args[nargs+i] = 0;

	if (enter_prom(&args, prom_entry) < 0)
		return PROM_ERROR;

	if (rets != NULL)
		for (i = 1; i < nret; ++i)
			rets[i-1] = be32_to_cpu(args.args[nargs+i]);

	return (nret > 0) ? be32_to_cpu(args.args[nargs]) : 0;
}


static void __init prom_print(const char *msg)
{
	const char *p, *q;

	if (prom.stdout == 0)
		return;

	for (p = msg; *p != 0; p = q) {
		for (q = p; *q != 0 && *q != '\n'; ++q)
			;
		if (q > p)
			call_prom("write", 3, 1, prom.stdout, p, q - p);
		if (*q == 0)
			break;
		++q;
		call_prom("write", 3, 1, prom.stdout, ADDR("\r\n"), 2);
	}
}


static void __init prom_print_hex(unsigned long val)
{
	int i, nibbles = sizeof(val)*2;
	char buf[sizeof(val)*2+1];

	for (i = nibbles-1;  i >= 0;  i--) {
		buf[i] = (val & 0xf) + '0';
		if (buf[i] > '9')
			buf[i] += ('a'-'0'-10);
		val >>= 4;
	}
	buf[nibbles] = '\0';
	call_prom("write", 3, 1, prom.stdout, buf, nibbles);
}

/* max number of decimal digits in an unsigned long */
#define UL_DIGITS 21
static void __init prom_print_dec(unsigned long val)
{
	int i, size;
	char buf[UL_DIGITS+1];

	for (i = UL_DIGITS-1; i >= 0;  i--) {
		buf[i] = (val % 10) + '0';
		val = val/10;
		if (val == 0)
			break;
	}
	/* shift stuff down */
	size = UL_DIGITS - i;
	call_prom("write", 3, 1, prom.stdout, buf+i, size);
}

static void __init prom_printf(const char *format, ...)
{
	const char *p, *q, *s;
	va_list args;
	unsigned long v;
	long vs;

	va_start(args, format);
	for (p = format; *p != 0; p = q) {
		for (q = p; *q != 0 && *q != '\n' && *q != '%'; ++q)
			;
		if (q > p)
			call_prom("write", 3, 1, prom.stdout, p, q - p);
		if (*q == 0)
			break;
		if (*q == '\n') {
			++q;
			call_prom("write", 3, 1, prom.stdout,
				  ADDR("\r\n"), 2);
			continue;
		}
		++q;
		if (*q == 0)
			break;
		switch (*q) {
		case 's':
			++q;
			s = va_arg(args, const char *);
			prom_print(s);
			break;
		case 'x':
			++q;
			v = va_arg(args, unsigned long);
			prom_print_hex(v);
			break;
		case 'd':
			++q;
			vs = va_arg(args, int);
			if (vs < 0) {
				prom_print("-");
				vs = -vs;
			}
			prom_print_dec(vs);
			break;
		case 'l':
			++q;
			if (*q == 0)
				break;
			else if (*q == 'x') {
				++q;
				v = va_arg(args, unsigned long);
				prom_print_hex(v);
			} else if (*q == 'u') { /* '%lu' */
				++q;
				v = va_arg(args, unsigned long);
				prom_print_dec(v);
			} else if (*q == 'd') { /* %ld */
				++q;
				vs = va_arg(args, long);
				if (vs < 0) {
					prom_print("-");
					vs = -vs;
				}
				prom_print_dec(vs);
			}
			break;
		}
	}
}


static unsigned int __init prom_claim(unsigned long virt, unsigned long size,
				unsigned long align)
{

	if (align == 0 && (OF_WORKAROUNDS & OF_WA_CLAIM)) {
		/*
		 * Old OF requires we claim physical and virtual separately
		 * and then map explicitly (assuming virtual mode)
		 */
		int ret;
		prom_arg_t result;

		ret = call_prom_ret("call-method", 5, 2, &result,
				    ADDR("claim"), prom.memory,
				    align, size, virt);
		if (ret != 0 || result == -1)
			return -1;
		ret = call_prom_ret("call-method", 5, 2, &result,
				    ADDR("claim"), prom.mmumap,
				    align, size, virt);
		if (ret != 0) {
			call_prom("call-method", 4, 1, ADDR("release"),
				  prom.memory, size, virt);
			return -1;
		}
		/* the 0x12 is M (coherence) + PP == read/write */
		call_prom("call-method", 6, 1,
			  ADDR("map"), prom.mmumap, 0x12, size, virt, virt);
		return virt;
	}
	return call_prom("claim", 3, 1, (prom_arg_t)virt, (prom_arg_t)size,
			 (prom_arg_t)align);
}

static void __init __attribute__((noreturn)) prom_panic(const char *reason)
{
	prom_print(reason);
	/* Do not call exit because it clears the screen on pmac
	 * it also causes some sort of double-fault on early pmacs */
	if (of_platform == PLATFORM_POWERMAC)
		asm("trap\n");

	/* ToDo: should put up an SRC here on pSeries */
	call_prom("exit", 0, 0);

	for (;;)			/* should never get here */
		;
}


static int __init prom_next_node(phandle *nodep)
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

static int inline prom_getprop(phandle node, const char *pname,
			       void *value, size_t valuelen)
{
	return call_prom("getprop", 4, 1, node, ADDR(pname),
			 (u32)(unsigned long) value, (u32) valuelen);
}

static int inline prom_getproplen(phandle node, const char *pname)
{
	return call_prom("getproplen", 2, 1, node, ADDR(pname));
}

static void add_string(char **str, const char *q)
{
	char *p = *str;

	while (*q)
		*p++ = *q++;
	*p++ = ' ';
	*str = p;
}

static char *tohex(unsigned int x)
{
	static char digits[] = "0123456789abcdef";
	static char result[9];
	int i;

	result[8] = 0;
	i = 8;
	do {
		--i;
		result[i] = digits[x & 0xf];
		x >>= 4;
	} while (x != 0 && i > 0);
	return &result[i];
}

static int __init prom_setprop(phandle node, const char *nodename,
			       const char *pname, void *value, size_t valuelen)
{
	char cmd[256], *p;

	if (!(OF_WORKAROUNDS & OF_WA_LONGTRAIL))
		return call_prom("setprop", 4, 1, node, ADDR(pname),
				 (u32)(unsigned long) value, (u32) valuelen);

	/* gah... setprop doesn't work on longtrail, have to use interpret */
	p = cmd;
	add_string(&p, "dev");
	add_string(&p, nodename);
	add_string(&p, tohex((u32)(unsigned long) value));
	add_string(&p, tohex(valuelen));
	add_string(&p, tohex(ADDR(pname)));
	add_string(&p, tohex(strlen(pname)));
	add_string(&p, "property");
	*p = 0;
	return call_prom("interpret", 1, 1, (u32)(unsigned long) cmd);
}

/* We can't use the standard versions because of relocation headaches. */
#define isxdigit(c)	(('0' <= (c) && (c) <= '9') \
			 || ('a' <= (c) && (c) <= 'f') \
			 || ('A' <= (c) && (c) <= 'F'))

#define isdigit(c)	('0' <= (c) && (c) <= '9')
#define islower(c)	('a' <= (c) && (c) <= 'z')
#define toupper(c)	(islower(c) ? ((c) - 'a' + 'A') : (c))

static unsigned long prom_strtoul(const char *cp, const char **endp)
{
	unsigned long result = 0, base = 10, value;

	if (*cp == '0') {
		base = 8;
		cp++;
		if (toupper(*cp) == 'X') {
			cp++;
			base = 16;
		}
	}

	while (isxdigit(*cp) &&
	       (value = isdigit(*cp) ? *cp - '0' : toupper(*cp) - 'A' + 10) < base) {
		result = result * base + value;
		cp++;
	}

	if (endp)
		*endp = cp;

	return result;
}

static unsigned long prom_memparse(const char *ptr, const char **retptr)
{
	unsigned long ret = prom_strtoul(ptr, retptr);
	int shift = 0;

	/*
	 * We can't use a switch here because GCC *may* generate a
	 * jump table which won't work, because we're not running at
	 * the address we're linked at.
	 */
	if ('G' == **retptr || 'g' == **retptr)
		shift = 30;

	if ('M' == **retptr || 'm' == **retptr)
		shift = 20;

	if ('K' == **retptr || 'k' == **retptr)
		shift = 10;

	if (shift) {
		ret <<= shift;
		(*retptr)++;
	}

	return ret;
}

/*
 * Early parsing of the command line passed to the kernel, used for
 * "mem=x" and the options that affect the iommu
 */
static void __init early_cmdline_parse(void)
{
	const char *opt;

	char *p;
	int l = 0;

	prom_cmd_line[0] = 0;
	p = prom_cmd_line;
	if ((long)prom.chosen > 0)
		l = prom_getprop(prom.chosen, "bootargs", p, COMMAND_LINE_SIZE-1);
#ifdef CONFIG_CMDLINE
	if (l <= 0 || p[0] == '\0') /* dbl check */
		strlcpy(prom_cmd_line,
			CONFIG_CMDLINE, sizeof(prom_cmd_line));
#endif /* CONFIG_CMDLINE */
	prom_printf("command line: %s\n", prom_cmd_line);

#ifdef CONFIG_PPC64
	opt = strstr(prom_cmd_line, "iommu=");
	if (opt) {
		prom_printf("iommu opt is: %s\n", opt);
		opt += 6;
		while (*opt && *opt == ' ')
			opt++;
		if (!strncmp(opt, "off", 3))
			prom_iommu_off = 1;
		else if (!strncmp(opt, "force", 5))
			prom_iommu_force_on = 1;
	}
#endif
	opt = strstr(prom_cmd_line, "mem=");
	if (opt) {
		opt += 4;
		prom_memory_limit = prom_memparse(opt, (const char **)&opt);
#ifdef CONFIG_PPC64
		/* Align to 16 MB == size of ppc64 large page */
		prom_memory_limit = ALIGN(prom_memory_limit, 0x1000000);
#endif
	}
}

#if defined(CONFIG_PPC_PSERIES) || defined(CONFIG_PPC_POWERNV)
/*
 * The architecture vector has an array of PVR mask/value pairs,
 * followed by # option vectors - 1, followed by the option vectors.
 *
 * See prom.h for the definition of the bits specified in the
 * architecture vector.
 *
 * Because the description vector contains a mix of byte and word
 * values, we declare it as an unsigned char array, and use this
 * macro to put word values in.
 */
#define W(x)	((x) >> 24) & 0xff, ((x) >> 16) & 0xff, \
		((x) >> 8) & 0xff, (x) & 0xff

unsigned char ibm_architecture_vec[] = {
	W(0xfffe0000), W(0x003a0000),	/* POWER5/POWER5+ */
	W(0xffff0000), W(0x003e0000),	/* POWER6 */
	W(0xffff0000), W(0x003f0000),	/* POWER7 */
	W(0xffff0000), W(0x004b0000),	/* POWER8E */
	W(0xffff0000), W(0x004d0000),	/* POWER8 */
	W(0xffffffff), W(0x0f000004),	/* all 2.07-compliant */
	W(0xffffffff), W(0x0f000003),	/* all 2.06-compliant */
	W(0xffffffff), W(0x0f000002),	/* all 2.05-compliant */
	W(0xfffffffe), W(0x0f000001),	/* all 2.04-compliant and earlier */
	6 - 1,				/* 6 option vectors */

	/* option vector 1: processor architectures supported */
	3 - 2,				/* length */
	0,				/* don't ignore, don't halt */
	OV1_PPC_2_00 | OV1_PPC_2_01 | OV1_PPC_2_02 | OV1_PPC_2_03 |
	OV1_PPC_2_04 | OV1_PPC_2_05 | OV1_PPC_2_06 | OV1_PPC_2_07,

	/* option vector 2: Open Firmware options supported */
	34 - 2,				/* length */
	OV2_REAL_MODE,
	0, 0,
	W(0xffffffff),			/* real_base */
	W(0xffffffff),			/* real_size */
	W(0xffffffff),			/* virt_base */
	W(0xffffffff),			/* virt_size */
	W(0xffffffff),			/* load_base */
	W(256),				/* 256MB min RMA */
	W(0xffffffff),			/* full client load */
	0,				/* min RMA percentage of total RAM */
	48,				/* max log_2(hash table size) */

	/* option vector 3: processor options supported */
	3 - 2,				/* length */
	0,				/* don't ignore, don't halt */
	OV3_FP | OV3_VMX | OV3_DFP,

	/* option vector 4: IBM PAPR implementation */
	3 - 2,				/* length */
	0,				/* don't halt */
	OV4_MIN_ENT_CAP,		/* minimum VP entitled capacity */

	/* option vector 5: PAPR/OF options */
	19 - 2,				/* length */
	0,				/* don't ignore, don't halt */
	OV5_FEAT(OV5_LPAR) | OV5_FEAT(OV5_SPLPAR) | OV5_FEAT(OV5_LARGE_PAGES) |
	OV5_FEAT(OV5_DRCONF_MEMORY) | OV5_FEAT(OV5_DONATE_DEDICATE_CPU) |
#ifdef CONFIG_PCI_MSI
	/* PCIe/MSI support.  Without MSI full PCIe is not supported */
	OV5_FEAT(OV5_MSI),
#else
	0,
#endif
	0,
#ifdef CONFIG_PPC_SMLPAR
	OV5_FEAT(OV5_CMO) | OV5_FEAT(OV5_XCMO),
#else
	0,
#endif
	OV5_FEAT(OV5_TYPE1_AFFINITY) | OV5_FEAT(OV5_PRRN),
	0,
	0,
	0,
	/* WARNING: The offset of the "number of cores" field below
	 * must match by the macro below. Update the definition if
	 * the structure layout changes.
	 */
#define IBM_ARCH_VEC_NRCORES_OFFSET	125
	W(NR_CPUS),			/* number of cores supported */
	0,
	0,
	0,
	0,
	OV5_FEAT(OV5_PFO_HW_RNG) | OV5_FEAT(OV5_PFO_HW_ENCR) |
	OV5_FEAT(OV5_PFO_HW_842),
	OV5_FEAT(OV5_SUB_PROCESSORS),
	/* option vector 6: IBM PAPR hints */
	4 - 2,				/* length */
	0,
	0,
	OV6_LINUX,

};

/* Old method - ELF header with PT_NOTE sections only works on BE */
#ifdef __BIG_ENDIAN__
static struct fake_elf {
	Elf32_Ehdr	elfhdr;
	Elf32_Phdr	phdr[2];
	struct chrpnote {
		u32	namesz;
		u32	descsz;
		u32	type;
		char	name[8];	/* "PowerPC" */
		struct chrpdesc {
			u32	real_mode;
			u32	real_base;
			u32	real_size;
			u32	virt_base;
			u32	virt_size;
			u32	load_base;
		} chrpdesc;
	} chrpnote;
	struct rpanote {
		u32	namesz;
		u32	descsz;
		u32	type;
		char	name[24];	/* "IBM,RPA-Client-Config" */
		struct rpadesc {
			u32	lpar_affinity;
			u32	min_rmo_size;
			u32	min_rmo_percent;
			u32	max_pft_size;
			u32	splpar;
			u32	min_load;
			u32	new_mem_def;
			u32	ignore_me;
		} rpadesc;
	} rpanote;
} fake_elf = {
	.elfhdr = {
		.e_ident = { 0x7f, 'E', 'L', 'F',
			     ELFCLASS32, ELFDATA2MSB, EV_CURRENT },
		.e_type = ET_EXEC,	/* yeah right */
		.e_machine = EM_PPC,
		.e_version = EV_CURRENT,
		.e_phoff = offsetof(struct fake_elf, phdr),
		.e_phentsize = sizeof(Elf32_Phdr),
		.e_phnum = 2
	},
	.phdr = {
		[0] = {
			.p_type = PT_NOTE,
			.p_offset = offsetof(struct fake_elf, chrpnote),
			.p_filesz = sizeof(struct chrpnote)
		}, [1] = {
			.p_type = PT_NOTE,
			.p_offset = offsetof(struct fake_elf, rpanote),
			.p_filesz = sizeof(struct rpanote)
		}
	},
	.chrpnote = {
		.namesz = sizeof("PowerPC"),
		.descsz = sizeof(struct chrpdesc),
		.type = 0x1275,
		.name = "PowerPC",
		.chrpdesc = {
			.real_mode = ~0U,	/* ~0 means "don't care" */
			.real_base = ~0U,
			.real_size = ~0U,
			.virt_base = ~0U,
			.virt_size = ~0U,
			.load_base = ~0U
		},
	},
	.rpanote = {
		.namesz = sizeof("IBM,RPA-Client-Config"),
		.descsz = sizeof(struct rpadesc),
		.type = 0x12759999,
		.name = "IBM,RPA-Client-Config",
		.rpadesc = {
			.lpar_affinity = 0,
			.min_rmo_size = 64,	/* in megabytes */
			.min_rmo_percent = 0,
			.max_pft_size = 48,	/* 2^48 bytes max PFT size */
			.splpar = 1,
			.min_load = ~0U,
			.new_mem_def = 0
		}
	}
};
#endif /* __BIG_ENDIAN__ */

static int __init prom_count_smt_threads(void)
{
	phandle node;
	char type[64];
	unsigned int plen;

	/* Pick up th first CPU node we can find */
	for (node = 0; prom_next_node(&node); ) {
		type[0] = 0;
		prom_getprop(node, "device_type", type, sizeof(type));

		if (strcmp(type, "cpu"))
			continue;
		/*
		 * There is an entry for each smt thread, each entry being
		 * 4 bytes long.  All cpus should have the same number of
		 * smt threads, so return after finding the first.
		 */
		plen = prom_getproplen(node, "ibm,ppc-interrupt-server#s");
		if (plen == PROM_ERROR)
			break;
		plen >>= 2;
		prom_debug("Found %lu smt threads per core\n", (unsigned long)plen);

		/* Sanity check */
		if (plen < 1 || plen > 64) {
			prom_printf("Threads per core %lu out of bounds, assuming 1\n",
				    (unsigned long)plen);
			return 1;
		}
		return plen;
	}
	prom_debug("No threads found, assuming 1 per core\n");

	return 1;

}


static void __init prom_send_capabilities(void)
{
	ihandle root;
	prom_arg_t ret;
	__be32 *cores;

	root = call_prom("open", 1, 1, ADDR("/"));
	if (root != 0) {
		/* We need to tell the FW about the number of cores we support.
		 *
		 * To do that, we count the number of threads on the first core
		 * (we assume this is the same for all cores) and use it to
		 * divide NR_CPUS.
		 */
		cores = (__be32 *)&ibm_architecture_vec[IBM_ARCH_VEC_NRCORES_OFFSET];
		if (be32_to_cpup(cores) != NR_CPUS) {
			prom_printf("WARNING ! "
				    "ibm_architecture_vec structure inconsistent: %lu!\n",
				    be32_to_cpup(cores));
		} else {
			*cores = cpu_to_be32(DIV_ROUND_UP(NR_CPUS, prom_count_smt_threads()));
			prom_printf("Max number of cores passed to firmware: %lu (NR_CPUS = %lu)\n",
				    be32_to_cpup(cores), NR_CPUS);
		}

		/* try calling the ibm,client-architecture-support method */
		prom_printf("Calling ibm,client-architecture-support...");
		if (call_prom_ret("call-method", 3, 2, &ret,
				  ADDR("ibm,client-architecture-support"),
				  root,
				  ADDR(ibm_architecture_vec)) == 0) {
			/* the call exists... */
			if (ret)
				prom_printf("\nWARNING: ibm,client-architecture"
					    "-support call FAILED!\n");
			call_prom("close", 1, 0, root);
			prom_printf(" done\n");
			return;
		}
		call_prom("close", 1, 0, root);
		prom_printf(" not implemented\n");
	}

#ifdef __BIG_ENDIAN__
	{
		ihandle elfloader;

		/* no ibm,client-architecture-support call, try the old way */
		elfloader = call_prom("open", 1, 1,
				      ADDR("/packages/elf-loader"));
		if (elfloader == 0) {
			prom_printf("couldn't open /packages/elf-loader\n");
			return;
		}
		call_prom("call-method", 3, 1, ADDR("process-elf-header"),
			  elfloader, ADDR(&fake_elf));
		call_prom("close", 1, 0, elfloader);
	}
#endif /* __BIG_ENDIAN__ */
}
#endif /* #if defined(CONFIG_PPC_PSERIES) || defined(CONFIG_PPC_POWERNV) */

/*
 * Memory allocation strategy... our layout is normally:
 *
 *  at 14Mb or more we have vmlinux, then a gap and initrd.  In some
 *  rare cases, initrd might end up being before the kernel though.
 *  We assume this won't override the final kernel at 0, we have no
 *  provision to handle that in this version, but it should hopefully
 *  never happen.
 *
 *  alloc_top is set to the top of RMO, eventually shrink down if the
 *  TCEs overlap
 *
 *  alloc_bottom is set to the top of kernel/initrd
 *
 *  from there, allocations are done this way : rtas is allocated
 *  topmost, and the device-tree is allocated from the bottom. We try
 *  to grow the device-tree allocation as we progress. If we can't,
 *  then we fail, we don't currently have a facility to restart
 *  elsewhere, but that shouldn't be necessary.
 *
 *  Note that calls to reserve_mem have to be done explicitly, memory
 *  allocated with either alloc_up or alloc_down isn't automatically
 *  reserved.
 */


/*
 * Allocates memory in the RMO upward from the kernel/initrd
 *
 * When align is 0, this is a special case, it means to allocate in place
 * at the current location of alloc_bottom or fail (that is basically
 * extending the previous allocation). Used for the device-tree flattening
 */
static unsigned long __init alloc_up(unsigned long size, unsigned long align)
{
	unsigned long base = alloc_bottom;
	unsigned long addr = 0;

	if (align)
		base = _ALIGN_UP(base, align);
	prom_debug("alloc_up(%x, %x)\n", size, align);
	if (ram_top == 0)
		prom_panic("alloc_up() called with mem not initialized\n");

	if (align)
		base = _ALIGN_UP(alloc_bottom, align);
	else
		base = alloc_bottom;

	for(; (base + size) <= alloc_top; 
	    base = _ALIGN_UP(base + 0x100000, align)) {
		prom_debug("    trying: 0x%x\n\r", base);
		addr = (unsigned long)prom_claim(base, size, 0);
		if (addr != PROM_ERROR && addr != 0)
			break;
		addr = 0;
		if (align == 0)
			break;
	}
	if (addr == 0)
		return 0;
	alloc_bottom = addr + size;

	prom_debug(" -> %x\n", addr);
	prom_debug("  alloc_bottom : %x\n", alloc_bottom);
	prom_debug("  alloc_top    : %x\n", alloc_top);
	prom_debug("  alloc_top_hi : %x\n", alloc_top_high);
	prom_debug("  rmo_top      : %x\n", rmo_top);
	prom_debug("  ram_top      : %x\n", ram_top);

	return addr;
}

/*
 * Allocates memory downward, either from top of RMO, or if highmem
 * is set, from the top of RAM.  Note that this one doesn't handle
 * failures.  It does claim memory if highmem is not set.
 */
static unsigned long __init alloc_down(unsigned long size, unsigned long align,
				       int highmem)
{
	unsigned long base, addr = 0;

	prom_debug("alloc_down(%x, %x, %s)\n", size, align,
		   highmem ? "(high)" : "(low)");
	if (ram_top == 0)
		prom_panic("alloc_down() called with mem not initialized\n");

	if (highmem) {
		/* Carve out storage for the TCE table. */
		addr = _ALIGN_DOWN(alloc_top_high - size, align);
		if (addr <= alloc_bottom)
			return 0;
		/* Will we bump into the RMO ? If yes, check out that we
		 * didn't overlap existing allocations there, if we did,
		 * we are dead, we must be the first in town !
		 */
		if (addr < rmo_top) {
			/* Good, we are first */
			if (alloc_top == rmo_top)
				alloc_top = rmo_top = addr;
			else
				return 0;
		}
		alloc_top_high = addr;
		goto bail;
	}

	base = _ALIGN_DOWN(alloc_top - size, align);
	for (; base > alloc_bottom;
	     base = _ALIGN_DOWN(base - 0x100000, align))  {
		prom_debug("    trying: 0x%x\n\r", base);
		addr = (unsigned long)prom_claim(base, size, 0);
		if (addr != PROM_ERROR && addr != 0)
			break;
		addr = 0;
	}
	if (addr == 0)
		return 0;
	alloc_top = addr;

 bail:
	prom_debug(" -> %x\n", addr);
	prom_debug("  alloc_bottom : %x\n", alloc_bottom);
	prom_debug("  alloc_top    : %x\n", alloc_top);
	prom_debug("  alloc_top_hi : %x\n", alloc_top_high);
	prom_debug("  rmo_top      : %x\n", rmo_top);
	prom_debug("  ram_top      : %x\n", ram_top);

	return addr;
}

/*
 * Parse a "reg" cell
 */
static unsigned long __init prom_next_cell(int s, cell_t **cellp)
{
	cell_t *p = *cellp;
	unsigned long r = 0;

	/* Ignore more than 2 cells */
	while (s > sizeof(unsigned long) / 4) {
		p++;
		s--;
	}
	r = be32_to_cpu(*p++);
#ifdef CONFIG_PPC64
	if (s > 1) {
		r <<= 32;
		r |= be32_to_cpu(*(p++));
	}
#endif
	*cellp = p;
	return r;
}

/*
 * Very dumb function for adding to the memory reserve list, but
 * we don't need anything smarter at this point
 *
 * XXX Eventually check for collisions.  They should NEVER happen.
 * If problems seem to show up, it would be a good start to track
 * them down.
 */
static void __init reserve_mem(u64 base, u64 size)
{
	u64 top = base + size;
	unsigned long cnt = mem_reserve_cnt;

	if (size == 0)
		return;

	/* We need to always keep one empty entry so that we
	 * have our terminator with "size" set to 0 since we are
	 * dumb and just copy this entire array to the boot params
	 */
	base = _ALIGN_DOWN(base, PAGE_SIZE);
	top = _ALIGN_UP(top, PAGE_SIZE);
	size = top - base;

	if (cnt >= (MEM_RESERVE_MAP_SIZE - 1))
		prom_panic("Memory reserve map exhausted !\n");
	mem_reserve_map[cnt].base = cpu_to_be64(base);
	mem_reserve_map[cnt].size = cpu_to_be64(size);
	mem_reserve_cnt = cnt + 1;
}

/*
 * Initialize memory allocation mechanism, parse "memory" nodes and
 * obtain that way the top of memory and RMO to setup out local allocator
 */
static void __init prom_init_mem(void)
{
	phandle node;
	char *path, type[64];
	unsigned int plen;
	cell_t *p, *endp;
	__be32 val;
	u32 rac, rsc;

	/*
	 * We iterate the memory nodes to find
	 * 1) top of RMO (first node)
	 * 2) top of memory
	 */
	val = cpu_to_be32(2);
	prom_getprop(prom.root, "#address-cells", &val, sizeof(val));
	rac = be32_to_cpu(val);
	val = cpu_to_be32(1);
	prom_getprop(prom.root, "#size-cells", &val, sizeof(rsc));
	rsc = be32_to_cpu(val);
	prom_debug("root_addr_cells: %x\n", rac);
	prom_debug("root_size_cells: %x\n", rsc);

	prom_debug("scanning memory:\n");
	path = prom_scratch;

	for (node = 0; prom_next_node(&node); ) {
		type[0] = 0;
		prom_getprop(node, "device_type", type, sizeof(type));

		if (type[0] == 0) {
			/*
			 * CHRP Longtrail machines have no device_type
			 * on the memory node, so check the name instead...
			 */
			prom_getprop(node, "name", type, sizeof(type));
		}
		if (strcmp(type, "memory"))
			continue;

		plen = prom_getprop(node, "reg", regbuf, sizeof(regbuf));
		if (plen > sizeof(regbuf)) {
			prom_printf("memory node too large for buffer !\n");
			plen = sizeof(regbuf);
		}
		p = regbuf;
		endp = p + (plen / sizeof(cell_t));

#ifdef DEBUG_PROM
		memset(path, 0, PROM_SCRATCH_SIZE);
		call_prom("package-to-path", 3, 1, node, path, PROM_SCRATCH_SIZE-1);
		prom_debug("  node %s :\n", path);
#endif /* DEBUG_PROM */

		while ((endp - p) >= (rac + rsc)) {
			unsigned long base, size;

			base = prom_next_cell(rac, &p);
			size = prom_next_cell(rsc, &p);

			if (size == 0)
				continue;
			prom_debug("    %x %x\n", base, size);
			if (base == 0 && (of_platform & PLATFORM_LPAR))
				rmo_top = size;
			if ((base + size) > ram_top)
				ram_top = base + size;
		}
	}

	alloc_bottom = PAGE_ALIGN((unsigned long)&_end + 0x4000);

	/*
	 * If prom_memory_limit is set we reduce the upper limits *except* for
	 * alloc_top_high. This must be the real top of RAM so we can put
	 * TCE's up there.
	 */

	alloc_top_high = ram_top;

	if (prom_memory_limit) {
		if (prom_memory_limit <= alloc_bottom) {
			prom_printf("Ignoring mem=%x <= alloc_bottom.\n",
				prom_memory_limit);
			prom_memory_limit = 0;
		} else if (prom_memory_limit >= ram_top) {
			prom_printf("Ignoring mem=%x >= ram_top.\n",
				prom_memory_limit);
			prom_memory_limit = 0;
		} else {
			ram_top = prom_memory_limit;
			rmo_top = min(rmo_top, prom_memory_limit);
		}
	}

	/*
	 * Setup our top alloc point, that is top of RMO or top of
	 * segment 0 when running non-LPAR.
	 * Some RS64 machines have buggy firmware where claims up at
	 * 1GB fail.  Cap at 768MB as a workaround.
	 * Since 768MB is plenty of room, and we need to cap to something
	 * reasonable on 32-bit, cap at 768MB on all machines.
	 */
	if (!rmo_top)
		rmo_top = ram_top;
	rmo_top = min(0x30000000ul, rmo_top);
	alloc_top = rmo_top;
	alloc_top_high = ram_top;

	/*
	 * Check if we have an initrd after the kernel but still inside
	 * the RMO.  If we do move our bottom point to after it.
	 */
	if (prom_initrd_start &&
	    prom_initrd_start < rmo_top &&
	    prom_initrd_end > alloc_bottom)
		alloc_bottom = PAGE_ALIGN(prom_initrd_end);

	prom_printf("memory layout at init:\n");
	prom_printf("  memory_limit : %x (16 MB aligned)\n", prom_memory_limit);
	prom_printf("  alloc_bottom : %x\n", alloc_bottom);
	prom_printf("  alloc_top    : %x\n", alloc_top);
	prom_printf("  alloc_top_hi : %x\n", alloc_top_high);
	prom_printf("  rmo_top      : %x\n", rmo_top);
	prom_printf("  ram_top      : %x\n", ram_top);
}

static void __init prom_close_stdin(void)
{
	__be32 val;
	ihandle stdin;

	if (prom_getprop(prom.chosen, "stdin", &val, sizeof(val)) > 0) {
		stdin = be32_to_cpu(val);
		call_prom("close", 1, 0, stdin);
	}
}

#ifdef CONFIG_PPC_POWERNV

#ifdef CONFIG_PPC_EARLY_DEBUG_OPAL
static u64 __initdata prom_opal_base;
static u64 __initdata prom_opal_entry;
#endif

#ifdef __BIG_ENDIAN__
/* XXX Don't change this structure without updating opal-takeover.S */
static struct opal_secondary_data {
	s64				ack;	/*  0 */
	u64				go;	/*  8 */
	struct opal_takeover_args	args;	/* 16 */
} opal_secondary_data;

static u64 __initdata prom_opal_align;
static u64 __initdata prom_opal_size;
static int __initdata prom_rtas_start_cpu;
static u64 __initdata prom_rtas_data;
static u64 __initdata prom_rtas_entry;

extern char opal_secondary_entry;

static void __init prom_query_opal(void)
{
	long rc;

	/* We must not query for OPAL presence on a machine that
	 * supports TNK takeover (970 blades), as this uses the same
	 * h-call with different arguments and will crash
	 */
	if (PHANDLE_VALID(call_prom("finddevice", 1, 1,
				    ADDR("/tnk-memory-map")))) {
		prom_printf("TNK takeover detected, skipping OPAL check\n");
		return;
	}

	prom_printf("Querying for OPAL presence... ");

	rc = opal_query_takeover(&prom_opal_size,
				 &prom_opal_align);
	prom_debug("(rc = %ld) ", rc);
	if (rc != 0) {
		prom_printf("not there.\n");
		return;
	}
	of_platform = PLATFORM_OPAL;
	prom_printf(" there !\n");
	prom_debug("  opal_size  = 0x%lx\n", prom_opal_size);
	prom_debug("  opal_align = 0x%lx\n", prom_opal_align);
	if (prom_opal_align < 0x10000)
		prom_opal_align = 0x10000;
}

static int prom_rtas_call(int token, int nargs, int nret, int *outputs, ...)
{
	struct rtas_args rtas_args;
	va_list list;
	int i;

	rtas_args.token = token;
	rtas_args.nargs = nargs;
	rtas_args.nret  = nret;
	rtas_args.rets  = (rtas_arg_t *)&(rtas_args.args[nargs]);
	va_start(list, outputs);
	for (i = 0; i < nargs; ++i)
		rtas_args.args[i] = va_arg(list, rtas_arg_t);
	va_end(list);

	for (i = 0; i < nret; ++i)
		rtas_args.rets[i] = 0;

	opal_enter_rtas(&rtas_args, prom_rtas_data,
			prom_rtas_entry);

	if (nret > 1 && outputs != NULL)
		for (i = 0; i < nret-1; ++i)
			outputs[i] = rtas_args.rets[i+1];
	return (nret > 0)? rtas_args.rets[0]: 0;
}

static void __init prom_opal_hold_cpus(void)
{
	int i, cnt, cpu, rc;
	long j;
	phandle node;
	char type[64];
	u32 servers[8];
	void *entry = (unsigned long *)&opal_secondary_entry;
	struct opal_secondary_data *data = &opal_secondary_data;

	prom_debug("prom_opal_hold_cpus: start...\n");
	prom_debug("    - entry       = 0x%x\n", entry);
	prom_debug("    - data        = 0x%x\n", data);

	data->ack = -1;
	data->go = 0;

	/* look for cpus */
	for (node = 0; prom_next_node(&node); ) {
		type[0] = 0;
		prom_getprop(node, "device_type", type, sizeof(type));
		if (strcmp(type, "cpu") != 0)
			continue;

		/* Skip non-configured cpus. */
		if (prom_getprop(node, "status", type, sizeof(type)) > 0)
			if (strcmp(type, "okay") != 0)
				continue;

		cnt = prom_getprop(node, "ibm,ppc-interrupt-server#s", servers,
			     sizeof(servers));
		if (cnt == PROM_ERROR)
			break;
		cnt >>= 2;
		for (i = 0; i < cnt; i++) {
			cpu = servers[i];
			prom_debug("CPU %d ... ", cpu);
			if (cpu == prom.cpu) {
				prom_debug("booted !\n");
				continue;
			}
			prom_debug("starting ... ");

			/* Init the acknowledge var which will be reset by
			 * the secondary cpu when it awakens from its OF
			 * spinloop.
			 */
			data->ack = -1;
			rc = prom_rtas_call(prom_rtas_start_cpu, 3, 1,
					    NULL, cpu, entry, data);
			prom_debug("rtas rc=%d ...", rc);

			for (j = 0; j < 100000000 && data->ack == -1; j++) {
				HMT_low();
				mb();
			}
			HMT_medium();
			if (data->ack != -1)
				prom_debug("done, PIR=0x%x\n", data->ack);
			else
				prom_debug("timeout !\n");
		}
	}
	prom_debug("prom_opal_hold_cpus: end...\n");
}

static void __init prom_opal_takeover(void)
{
	struct opal_secondary_data *data = &opal_secondary_data;
	struct opal_takeover_args *args = &data->args;
	u64 align = prom_opal_align;
	u64 top_addr, opal_addr;

	args->k_image	= (u64)_stext;
	args->k_size	= _end - _stext;
	args->k_entry	= 0;
	args->k_entry2	= 0x60;

	top_addr = _ALIGN_UP(args->k_size, align);

	if (prom_initrd_start != 0) {
		args->rd_image = prom_initrd_start;
		args->rd_size = prom_initrd_end - args->rd_image;
		args->rd_loc = top_addr;
		top_addr = _ALIGN_UP(args->rd_loc + args->rd_size, align);
	}

	/* Pickup an address for the HAL. We want to go really high
	 * up to avoid problem with future kexecs. On the other hand
	 * we don't want to be all over the TCEs on P5IOC2 machines
	 * which are going to be up there too. We assume the machine
	 * has plenty of memory, and we ask for the HAL for now to
	 * be just below the 1G point, or above the initrd
	 */
	opal_addr = _ALIGN_DOWN(0x40000000 - prom_opal_size, align);
	if (opal_addr < top_addr)
		opal_addr = top_addr;
	args->hal_addr = opal_addr;

	/* Copy the command line to the kernel image */
	strlcpy(boot_command_line, prom_cmd_line,
		COMMAND_LINE_SIZE);

	prom_debug("  k_image    = 0x%lx\n", args->k_image);
	prom_debug("  k_size     = 0x%lx\n", args->k_size);
	prom_debug("  k_entry    = 0x%lx\n", args->k_entry);
	prom_debug("  k_entry2   = 0x%lx\n", args->k_entry2);
	prom_debug("  hal_addr   = 0x%lx\n", args->hal_addr);
	prom_debug("  rd_image   = 0x%lx\n", args->rd_image);
	prom_debug("  rd_size    = 0x%lx\n", args->rd_size);
	prom_debug("  rd_loc     = 0x%lx\n", args->rd_loc);
	prom_printf("Performing OPAL takeover,this can take a few minutes..\n");
	prom_close_stdin();
	mb();
	data->go = 1;
	for (;;)
		opal_do_takeover(args);
}
#endif /* __BIG_ENDIAN__ */

/*
 * Allocate room for and instantiate OPAL
 */
static void __init prom_instantiate_opal(void)
{
	phandle opal_node;
	ihandle opal_inst;
	u64 base, entry;
	u64 size = 0, align = 0x10000;
	__be64 val64;
	u32 rets[2];

	prom_debug("prom_instantiate_opal: start...\n");

	opal_node = call_prom("finddevice", 1, 1, ADDR("/ibm,opal"));
	prom_debug("opal_node: %x\n", opal_node);
	if (!PHANDLE_VALID(opal_node))
		return;

	val64 = 0;
	prom_getprop(opal_node, "opal-runtime-size", &val64, sizeof(val64));
	size = be64_to_cpu(val64);
	if (size == 0)
		return;
	val64 = 0;
	prom_getprop(opal_node, "opal-runtime-alignment", &val64,sizeof(val64));
	align = be64_to_cpu(val64);

	base = alloc_down(size, align, 0);
	if (base == 0) {
		prom_printf("OPAL allocation failed !\n");
		return;
	}

	opal_inst = call_prom("open", 1, 1, ADDR("/ibm,opal"));
	if (!IHANDLE_VALID(opal_inst)) {
		prom_printf("opening opal package failed (%x)\n", opal_inst);
		return;
	}

	prom_printf("instantiating opal at 0x%x...", base);

	if (call_prom_ret("call-method", 4, 3, rets,
			  ADDR("load-opal-runtime"),
			  opal_inst,
			  base >> 32, base & 0xffffffff) != 0
	    || (rets[0] == 0 && rets[1] == 0)) {
		prom_printf(" failed\n");
		return;
	}
	entry = (((u64)rets[0]) << 32) | rets[1];

	prom_printf(" done\n");

	reserve_mem(base, size);

	prom_debug("opal base     = 0x%x\n", base);
	prom_debug("opal align    = 0x%x\n", align);
	prom_debug("opal entry    = 0x%x\n", entry);
	prom_debug("opal size     = 0x%x\n", (long)size);

	prom_setprop(opal_node, "/ibm,opal", "opal-base-address",
		     &base, sizeof(base));
	prom_setprop(opal_node, "/ibm,opal", "opal-entry-address",
		     &entry, sizeof(entry));

#ifdef CONFIG_PPC_EARLY_DEBUG_OPAL
	prom_opal_base = base;
	prom_opal_entry = entry;
#endif
	prom_debug("prom_instantiate_opal: end...\n");
}

#endif /* CONFIG_PPC_POWERNV */

/*
 * Allocate room for and instantiate RTAS
 */
static void __init prom_instantiate_rtas(void)
{
	phandle rtas_node;
	ihandle rtas_inst;
	u32 base, entry = 0;
	__be32 val;
	u32 size = 0;

	prom_debug("prom_instantiate_rtas: start...\n");

	rtas_node = call_prom("finddevice", 1, 1, ADDR("/rtas"));
	prom_debug("rtas_node: %x\n", rtas_node);
	if (!PHANDLE_VALID(rtas_node))
		return;

	val = 0;
	prom_getprop(rtas_node, "rtas-size", &val, sizeof(size));
	size = be32_to_cpu(val);
	if (size == 0)
		return;

	base = alloc_down(size, PAGE_SIZE, 0);
	if (base == 0)
		prom_panic("Could not allocate memory for RTAS\n");

	rtas_inst = call_prom("open", 1, 1, ADDR("/rtas"));
	if (!IHANDLE_VALID(rtas_inst)) {
		prom_printf("opening rtas package failed (%x)\n", rtas_inst);
		return;
	}

	prom_printf("instantiating rtas at 0x%x...", base);

	if (call_prom_ret("call-method", 3, 2, &entry,
			  ADDR("instantiate-rtas"),
			  rtas_inst, base) != 0
	    || entry == 0) {
		prom_printf(" failed\n");
		return;
	}
	prom_printf(" done\n");

	reserve_mem(base, size);

	val = cpu_to_be32(base);
	prom_setprop(rtas_node, "/rtas", "linux,rtas-base",
		     &val, sizeof(val));
	val = cpu_to_be32(entry);
	prom_setprop(rtas_node, "/rtas", "linux,rtas-entry",
		     &val, sizeof(val));

#if defined(CONFIG_PPC_POWERNV) && defined(__BIG_ENDIAN__)
	/* PowerVN takeover hack */
	prom_rtas_data = base;
	prom_rtas_entry = entry;
	prom_getprop(rtas_node, "start-cpu", &prom_rtas_start_cpu, 4);
#endif
	prom_debug("rtas base     = 0x%x\n", base);
	prom_debug("rtas entry    = 0x%x\n", entry);
	prom_debug("rtas size     = 0x%x\n", (long)size);

	prom_debug("prom_instantiate_rtas: end...\n");
}

#ifdef CONFIG_PPC64
/*
 * Allocate room for and instantiate Stored Measurement Log (SML)
 */
static void __init prom_instantiate_sml(void)
{
	phandle ibmvtpm_node;
	ihandle ibmvtpm_inst;
	u32 entry = 0, size = 0;
	u64 base;

	prom_debug("prom_instantiate_sml: start...\n");

	ibmvtpm_node = call_prom("finddevice", 1, 1, ADDR("/ibm,vtpm"));
	prom_debug("ibmvtpm_node: %x\n", ibmvtpm_node);
	if (!PHANDLE_VALID(ibmvtpm_node))
		return;

	ibmvtpm_inst = call_prom("open", 1, 1, ADDR("/ibm,vtpm"));
	if (!IHANDLE_VALID(ibmvtpm_inst)) {
		prom_printf("opening vtpm package failed (%x)\n", ibmvtpm_inst);
		return;
	}

	if (call_prom_ret("call-method", 2, 2, &size,
			  ADDR("sml-get-handover-size"),
			  ibmvtpm_inst) != 0 || size == 0) {
		prom_printf("SML get handover size failed\n");
		return;
	}

	base = alloc_down(size, PAGE_SIZE, 0);
	if (base == 0)
		prom_panic("Could not allocate memory for sml\n");

	prom_printf("instantiating sml at 0x%x...", base);

	if (call_prom_ret("call-method", 4, 2, &entry,
			  ADDR("sml-handover"),
			  ibmvtpm_inst, size, base) != 0 || entry == 0) {
		prom_printf("SML handover failed\n");
		return;
	}
	prom_printf(" done\n");

	reserve_mem(base, size);

	prom_setprop(ibmvtpm_node, "/ibm,vtpm", "linux,sml-base",
		     &base, sizeof(base));
	prom_setprop(ibmvtpm_node, "/ibm,vtpm", "linux,sml-size",
		     &size, sizeof(size));

	prom_debug("sml base     = 0x%x\n", base);
	prom_debug("sml size     = 0x%x\n", (long)size);

	prom_debug("prom_instantiate_sml: end...\n");
}

/*
 * Allocate room for and initialize TCE tables
 */
#ifdef __BIG_ENDIAN__
static void __init prom_initialize_tce_table(void)
{
	phandle node;
	ihandle phb_node;
	char compatible[64], type[64], model[64];
	char *path = prom_scratch;
	u64 base, align;
	u32 minalign, minsize;
	u64 tce_entry, *tce_entryp;
	u64 local_alloc_top, local_alloc_bottom;
	u64 i;

	if (prom_iommu_off)
		return;

	prom_debug("starting prom_initialize_tce_table\n");

	/* Cache current top of allocs so we reserve a single block */
	local_alloc_top = alloc_top_high;
	local_alloc_bottom = local_alloc_top;

	/* Search all nodes looking for PHBs. */
	for (node = 0; prom_next_node(&node); ) {
		compatible[0] = 0;
		type[0] = 0;
		model[0] = 0;
		prom_getprop(node, "compatible",
			     compatible, sizeof(compatible));
		prom_getprop(node, "device_type", type, sizeof(type));
		prom_getprop(node, "model", model, sizeof(model));

		if ((type[0] == 0) || (strstr(type, "pci") == NULL))
			continue;

		/* Keep the old logic intact to avoid regression. */
		if (compatible[0] != 0) {
			if ((strstr(compatible, "python") == NULL) &&
			    (strstr(compatible, "Speedwagon") == NULL) &&
			    (strstr(compatible, "Winnipeg") == NULL))
				continue;
		} else if (model[0] != 0) {
			if ((strstr(model, "ython") == NULL) &&
			    (strstr(model, "peedwagon") == NULL) &&
			    (strstr(model, "innipeg") == NULL))
				continue;
		}

		if (prom_getprop(node, "tce-table-minalign", &minalign,
				 sizeof(minalign)) == PROM_ERROR)
			minalign = 0;
		if (prom_getprop(node, "tce-table-minsize", &minsize,
				 sizeof(minsize)) == PROM_ERROR)
			minsize = 4UL << 20;

		/*
		 * Even though we read what OF wants, we just set the table
		 * size to 4 MB.  This is enough to map 2GB of PCI DMA space.
		 * By doing this, we avoid the pitfalls of trying to DMA to
		 * MMIO space and the DMA alias hole.
		 *
		 * On POWER4, firmware sets the TCE region by assuming
		 * each TCE table is 8MB. Using this memory for anything
		 * else will impact performance, so we always allocate 8MB.
		 * Anton
		 */
		if (pvr_version_is(PVR_POWER4) || pvr_version_is(PVR_POWER4p))
			minsize = 8UL << 20;
		else
			minsize = 4UL << 20;

		/* Align to the greater of the align or size */
		align = max(minalign, minsize);
		base = alloc_down(minsize, align, 1);
		if (base == 0)
			prom_panic("ERROR, cannot find space for TCE table.\n");
		if (base < local_alloc_bottom)
			local_alloc_bottom = base;

		/* It seems OF doesn't null-terminate the path :-( */
		memset(path, 0, PROM_SCRATCH_SIZE);
		/* Call OF to setup the TCE hardware */
		if (call_prom("package-to-path", 3, 1, node,
			      path, PROM_SCRATCH_SIZE-1) == PROM_ERROR) {
			prom_printf("package-to-path failed\n");
		}

		/* Save away the TCE table attributes for later use. */
		prom_setprop(node, path, "linux,tce-base", &base, sizeof(base));
		prom_setprop(node, path, "linux,tce-size", &minsize, sizeof(minsize));

		prom_debug("TCE table: %s\n", path);
		prom_debug("\tnode = 0x%x\n", node);
		prom_debug("\tbase = 0x%x\n", base);
		prom_debug("\tsize = 0x%x\n", minsize);

		/* Initialize the table to have a one-to-one mapping
		 * over the allocated size.
		 */
		tce_entryp = (u64 *)base;
		for (i = 0; i < (minsize >> 3) ;tce_entryp++, i++) {
			tce_entry = (i << PAGE_SHIFT);
			tce_entry |= 0x3;
			*tce_entryp = tce_entry;
		}

		prom_printf("opening PHB %s", path);
		phb_node = call_prom("open", 1, 1, path);
		if (phb_node == 0)
			prom_printf("... failed\n");
		else
			prom_printf("... done\n");

		call_prom("call-method", 6, 0, ADDR("set-64-bit-addressing"),
			  phb_node, -1, minsize,
			  (u32) base, (u32) (base >> 32));
		call_prom("close", 1, 0, phb_node);
	}

	reserve_mem(local_alloc_bottom, local_alloc_top - local_alloc_bottom);

	/* These are only really needed if there is a memory limit in
	 * effect, but we don't know so export them always. */
	prom_tce_alloc_start = local_alloc_bottom;
	prom_tce_alloc_end = local_alloc_top;

	/* Flag the first invalid entry */
	prom_debug("ending prom_initialize_tce_table\n");
}
#endif /* __BIG_ENDIAN__ */
#endif /* CONFIG_PPC64 */

/*
 * With CHRP SMP we need to use the OF to start the other processors.
 * We can't wait until smp_boot_cpus (the OF is trashed by then)
 * so we have to put the processors into a holding pattern controlled
 * by the kernel (not OF) before we destroy the OF.
 *
 * This uses a chunk of low memory, puts some holding pattern
 * code there and sends the other processors off to there until
 * smp_boot_cpus tells them to do something.  The holding pattern
 * checks that address until its cpu # is there, when it is that
 * cpu jumps to __secondary_start().  smp_boot_cpus() takes care
 * of setting those values.
 *
 * We also use physical address 0x4 here to tell when a cpu
 * is in its holding pattern code.
 *
 * -- Cort
 */
/*
 * We want to reference the copy of __secondary_hold_* in the
 * 0 - 0x100 address range
 */
#define LOW_ADDR(x)	(((unsigned long) &(x)) & 0xff)

static void __init prom_hold_cpus(void)
{
	unsigned long i;
	phandle node;
	char type[64];
	unsigned long *spinloop
		= (void *) LOW_ADDR(__secondary_hold_spinloop);
	unsigned long *acknowledge
		= (void *) LOW_ADDR(__secondary_hold_acknowledge);
	unsigned long secondary_hold = LOW_ADDR(__secondary_hold);

	prom_debug("prom_hold_cpus: start...\n");
	prom_debug("    1) spinloop       = 0x%x\n", (unsigned long)spinloop);
	prom_debug("    1) *spinloop      = 0x%x\n", *spinloop);
	prom_debug("    1) acknowledge    = 0x%x\n",
		   (unsigned long)acknowledge);
	prom_debug("    1) *acknowledge   = 0x%x\n", *acknowledge);
	prom_debug("    1) secondary_hold = 0x%x\n", secondary_hold);

	/* Set the common spinloop variable, so all of the secondary cpus
	 * will block when they are awakened from their OF spinloop.
	 * This must occur for both SMP and non SMP kernels, since OF will
	 * be trashed when we move the kernel.
	 */
	*spinloop = 0;

	/* look for cpus */
	for (node = 0; prom_next_node(&node); ) {
		unsigned int cpu_no;
		__be32 reg;

		type[0] = 0;
		prom_getprop(node, "device_type", type, sizeof(type));
		if (strcmp(type, "cpu") != 0)
			continue;

		/* Skip non-configured cpus. */
		if (prom_getprop(node, "status", type, sizeof(type)) > 0)
			if (strcmp(type, "okay") != 0)
				continue;

		reg = cpu_to_be32(-1); /* make sparse happy */
		prom_getprop(node, "reg", &reg, sizeof(reg));
		cpu_no = be32_to_cpu(reg);

		prom_debug("cpu hw idx   = %lu\n", cpu_no);

		/* Init the acknowledge var which will be reset by
		 * the secondary cpu when it awakens from its OF
		 * spinloop.
		 */
		*acknowledge = (unsigned long)-1;

		if (cpu_no != prom.cpu) {
			/* Primary Thread of non-boot cpu or any thread */
			prom_printf("starting cpu hw idx %lu... ", cpu_no);
			call_prom("start-cpu", 3, 0, node,
				  secondary_hold, cpu_no);

			for (i = 0; (i < 100000000) && 
			     (*acknowledge == ((unsigned long)-1)); i++ )
				mb();

			if (*acknowledge == cpu_no)
				prom_printf("done\n");
			else
				prom_printf("failed: %x\n", *acknowledge);
		}
#ifdef CONFIG_SMP
		else
			prom_printf("boot cpu hw idx %lu\n", cpu_no);
#endif /* CONFIG_SMP */
	}

	prom_debug("prom_hold_cpus: end...\n");
}


static void __init prom_init_client_services(unsigned long pp)
{
	/* Get a handle to the prom entry point before anything else */
	prom_entry = pp;

	/* get a handle for the stdout device */
	prom.chosen = call_prom("finddevice", 1, 1, ADDR("/chosen"));
	if (!PHANDLE_VALID(prom.chosen))
		prom_panic("cannot find chosen"); /* msg won't be printed :( */

	/* get device tree root */
	prom.root = call_prom("finddevice", 1, 1, ADDR("/"));
	if (!PHANDLE_VALID(prom.root))
		prom_panic("cannot find device tree root"); /* msg won't be printed :( */

	prom.mmumap = 0;
}

#ifdef CONFIG_PPC32
/*
 * For really old powermacs, we need to map things we claim.
 * For that, we need the ihandle of the mmu.
 * Also, on the longtrail, we need to work around other bugs.
 */
static void __init prom_find_mmu(void)
{
	phandle oprom;
	char version[64];

	oprom = call_prom("finddevice", 1, 1, ADDR("/openprom"));
	if (!PHANDLE_VALID(oprom))
		return;
	if (prom_getprop(oprom, "model", version, sizeof(version)) <= 0)
		return;
	version[sizeof(version) - 1] = 0;
	/* XXX might need to add other versions here */
	if (strcmp(version, "Open Firmware, 1.0.5") == 0)
		of_workarounds = OF_WA_CLAIM;
	else if (strncmp(version, "FirmWorks,3.", 12) == 0) {
		of_workarounds = OF_WA_CLAIM | OF_WA_LONGTRAIL;
		call_prom("interpret", 1, 1, "dev /memory 0 to allow-reclaim");
	} else
		return;
	prom.memory = call_prom("open", 1, 1, ADDR("/memory"));
	prom_getprop(prom.chosen, "mmu", &prom.mmumap,
		     sizeof(prom.mmumap));
	prom.mmumap = be32_to_cpu(prom.mmumap);
	if (!IHANDLE_VALID(prom.memory) || !IHANDLE_VALID(prom.mmumap))
		of_workarounds &= ~OF_WA_CLAIM;		/* hmmm */
}
#else
#define prom_find_mmu()
#endif

static void __init prom_init_stdout(void)
{
	char *path = of_stdout_device;
	char type[16];
	phandle stdout_node;
	__be32 val;

	if (prom_getprop(prom.chosen, "stdout", &val, sizeof(val)) <= 0)
		prom_panic("cannot find stdout");

	prom.stdout = be32_to_cpu(val);

	/* Get the full OF pathname of the stdout device */
	memset(path, 0, 256);
	call_prom("instance-to-path", 3, 1, prom.stdout, path, 255);
	stdout_node = call_prom("instance-to-package", 1, 1, prom.stdout);
	val = cpu_to_be32(stdout_node);
	prom_setprop(prom.chosen, "/chosen", "linux,stdout-package",
		     &val, sizeof(val));
	prom_printf("OF stdout device is: %s\n", of_stdout_device);
	prom_setprop(prom.chosen, "/chosen", "linux,stdout-path",
		     path, strlen(path) + 1);

	/* If it's a display, note it */
	memset(type, 0, sizeof(type));
	prom_getprop(stdout_node, "device_type", type, sizeof(type));
	if (strcmp(type, "display") == 0)
		prom_setprop(stdout_node, path, "linux,boot-display", NULL, 0);
}

static int __init prom_find_machine_type(void)
{
	char compat[256];
	int len, i = 0;
#ifdef CONFIG_PPC64
	phandle rtas;
	int x;
#endif

	/* Look for a PowerMac or a Cell */
	len = prom_getprop(prom.root, "compatible",
			   compat, sizeof(compat)-1);
	if (len > 0) {
		compat[len] = 0;
		while (i < len) {
			char *p = &compat[i];
			int sl = strlen(p);
			if (sl == 0)
				break;
			if (strstr(p, "Power Macintosh") ||
			    strstr(p, "MacRISC"))
				return PLATFORM_POWERMAC;
#ifdef CONFIG_PPC64
			/* We must make sure we don't detect the IBM Cell
			 * blades as pSeries due to some firmware issues,
			 * so we do it here.
			 */
			if (strstr(p, "IBM,CBEA") ||
			    strstr(p, "IBM,CPBW-1.0"))
				return PLATFORM_GENERIC;
#endif /* CONFIG_PPC64 */
			i += sl + 1;
		}
	}
#ifdef CONFIG_PPC64
	/* Try to detect OPAL */
	if (PHANDLE_VALID(call_prom("finddevice", 1, 1, ADDR("/ibm,opal"))))
		return PLATFORM_OPAL;

	/* Try to figure out if it's an IBM pSeries or any other
	 * PAPR compliant platform. We assume it is if :
	 *  - /device_type is "chrp" (please, do NOT use that for future
	 *    non-IBM designs !
	 *  - it has /rtas
	 */
	len = prom_getprop(prom.root, "device_type",
			   compat, sizeof(compat)-1);
	if (len <= 0)
		return PLATFORM_GENERIC;
	if (strcmp(compat, "chrp"))
		return PLATFORM_GENERIC;

	/* Default to pSeries. We need to know if we are running LPAR */
	rtas = call_prom("finddevice", 1, 1, ADDR("/rtas"));
	if (!PHANDLE_VALID(rtas))
		return PLATFORM_GENERIC;
	x = prom_getproplen(rtas, "ibm,hypertas-functions");
	if (x != PROM_ERROR) {
		prom_debug("Hypertas detected, assuming LPAR !\n");
		return PLATFORM_PSERIES_LPAR;
	}
	return PLATFORM_PSERIES;
#else
	return PLATFORM_GENERIC;
#endif
}

static int __init prom_set_color(ihandle ih, int i, int r, int g, int b)
{
	return call_prom("call-method", 6, 1, ADDR("color!"), ih, i, b, g, r);
}

/*
 * If we have a display that we don't know how to drive,
 * we will want to try to execute OF's open method for it
 * later.  However, OF will probably fall over if we do that
 * we've taken over the MMU.
 * So we check whether we will need to open the display,
 * and if so, open it now.
 */
static void __init prom_check_displays(void)
{
	char type[16], *path;
	phandle node;
	ihandle ih;
	int i;

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

	prom_debug("Looking for displays\n");
	for (node = 0; prom_next_node(&node); ) {
		memset(type, 0, sizeof(type));
		prom_getprop(node, "device_type", type, sizeof(type));
		if (strcmp(type, "display") != 0)
			continue;

		/* It seems OF doesn't null-terminate the path :-( */
		path = prom_scratch;
		memset(path, 0, PROM_SCRATCH_SIZE);

		/*
		 * leave some room at the end of the path for appending extra
		 * arguments
		 */
		if (call_prom("package-to-path", 3, 1, node, path,
			      PROM_SCRATCH_SIZE-10) == PROM_ERROR)
			continue;
		prom_printf("found display   : %s, opening... ", path);
		
		ih = call_prom("open", 1, 1, path);
		if (ih == 0) {
			prom_printf("failed\n");
			continue;
		}

		/* Success */
		prom_printf("done\n");
		prom_setprop(node, path, "linux,opened", NULL, 0);

		/* Setup a usable color table when the appropriate
		 * method is available. Should update this to set-colors */
		clut = default_colors;
		for (i = 0; i < 16; i++, clut += 3)
			if (prom_set_color(ih, i, clut[0], clut[1],
					   clut[2]) != 0)
				break;

#ifdef CONFIG_LOGO_LINUX_CLUT224
		clut = PTRRELOC(logo_linux_clut224.clut);
		for (i = 0; i < logo_linux_clut224.clutsize; i++, clut += 3)
			if (prom_set_color(ih, i + 32, clut[0], clut[1],
					   clut[2]) != 0)
				break;
#endif /* CONFIG_LOGO_LINUX_CLUT224 */

#ifdef CONFIG_PPC_EARLY_DEBUG_BOOTX
		if (prom_getprop(node, "linux,boot-display", NULL, 0) !=
		    PROM_ERROR) {
			u32 width, height, pitch, addr;

			prom_printf("Setting btext !\n");
			prom_getprop(node, "width", &width, 4);
			prom_getprop(node, "height", &height, 4);
			prom_getprop(node, "linebytes", &pitch, 4);
			prom_getprop(node, "address", &addr, 4);
			prom_printf("W=%d H=%d LB=%d addr=0x%x\n",
				    width, height, pitch, addr);
			btext_setup_display(width, height, 8, pitch, addr);
		}
#endif /* CONFIG_PPC_EARLY_DEBUG_BOOTX */
	}
}


/* Return (relocated) pointer to this much memory: moves initrd if reqd. */
static void __init *make_room(unsigned long *mem_start, unsigned long *mem_end,
			      unsigned long needed, unsigned long align)
{
	void *ret;

	*mem_start = _ALIGN(*mem_start, align);
	while ((*mem_start + needed) > *mem_end) {
		unsigned long room, chunk;

		prom_debug("Chunk exhausted, claiming more at %x...\n",
			   alloc_bottom);
		room = alloc_top - alloc_bottom;
		if (room > DEVTREE_CHUNK_SIZE)
			room = DEVTREE_CHUNK_SIZE;
		if (room < PAGE_SIZE)
			prom_panic("No memory for flatten_device_tree "
				   "(no room)\n");
		chunk = alloc_up(room, 0);
		if (chunk == 0)
			prom_panic("No memory for flatten_device_tree "
				   "(claim failed)\n");
		*mem_end = chunk + room;
	}

	ret = (void *)*mem_start;
	*mem_start += needed;

	return ret;
}

#define dt_push_token(token, mem_start, mem_end) do { 			\
		void *room = make_room(mem_start, mem_end, 4, 4);	\
		*(__be32 *)room = cpu_to_be32(token);			\
	} while(0)

static unsigned long __init dt_find_string(char *str)
{
	char *s, *os;

	s = os = (char *)dt_string_start;
	s += 4;
	while (s <  (char *)dt_string_end) {
		if (strcmp(s, str) == 0)
			return s - os;
		s += strlen(s) + 1;
	}
	return 0;
}

/*
 * The Open Firmware 1275 specification states properties must be 31 bytes or
 * less, however not all firmwares obey this. Make it 64 bytes to be safe.
 */
#define MAX_PROPERTY_NAME 64

static void __init scan_dt_build_strings(phandle node,
					 unsigned long *mem_start,
					 unsigned long *mem_end)
{
	char *prev_name, *namep, *sstart;
	unsigned long soff;
	phandle child;

	sstart =  (char *)dt_string_start;

	/* get and store all property names */
	prev_name = "";
	for (;;) {
		/* 64 is max len of name including nul. */
		namep = make_room(mem_start, mem_end, MAX_PROPERTY_NAME, 1);
		if (call_prom("nextprop", 3, 1, node, prev_name, namep) != 1) {
			/* No more nodes: unwind alloc */
			*mem_start = (unsigned long)namep;
			break;
		}

 		/* skip "name" */
 		if (strcmp(namep, "name") == 0) {
 			*mem_start = (unsigned long)namep;
 			prev_name = "name";
 			continue;
 		}
		/* get/create string entry */
		soff = dt_find_string(namep);
		if (soff != 0) {
			*mem_start = (unsigned long)namep;
			namep = sstart + soff;
		} else {
			/* Trim off some if we can */
			*mem_start = (unsigned long)namep + strlen(namep) + 1;
			dt_string_end = *mem_start;
		}
		prev_name = namep;
	}

	/* do all our children */
	child = call_prom("child", 1, 1, node);
	while (child != 0) {
		scan_dt_build_strings(child, mem_start, mem_end);
		child = call_prom("peer", 1, 1, child);
	}
}

static void __init scan_dt_build_struct(phandle node, unsigned long *mem_start,
					unsigned long *mem_end)
{
	phandle child;
	char *namep, *prev_name, *sstart, *p, *ep, *lp, *path;
	unsigned long soff;
	unsigned char *valp;
	static char pname[MAX_PROPERTY_NAME];
	int l, room, has_phandle = 0;

	dt_push_token(OF_DT_BEGIN_NODE, mem_start, mem_end);

	/* get the node's full name */
	namep = (char *)*mem_start;
	room = *mem_end - *mem_start;
	if (room > 255)
		room = 255;
	l = call_prom("package-to-path", 3, 1, node, namep, room);
	if (l >= 0) {
		/* Didn't fit?  Get more room. */
		if (l >= room) {
			if (l >= *mem_end - *mem_start)
				namep = make_room(mem_start, mem_end, l+1, 1);
			call_prom("package-to-path", 3, 1, node, namep, l);
		}
		namep[l] = '\0';

		/* Fixup an Apple bug where they have bogus \0 chars in the
		 * middle of the path in some properties, and extract
		 * the unit name (everything after the last '/').
		 */
		for (lp = p = namep, ep = namep + l; p < ep; p++) {
			if (*p == '/')
				lp = namep;
			else if (*p != 0)
				*lp++ = *p;
		}
		*lp = 0;
		*mem_start = _ALIGN((unsigned long)lp + 1, 4);
	}

	/* get it again for debugging */
	path = prom_scratch;
	memset(path, 0, PROM_SCRATCH_SIZE);
	call_prom("package-to-path", 3, 1, node, path, PROM_SCRATCH_SIZE-1);

	/* get and store all properties */
	prev_name = "";
	sstart = (char *)dt_string_start;
	for (;;) {
		if (call_prom("nextprop", 3, 1, node, prev_name,
			      pname) != 1)
			break;

 		/* skip "name" */
 		if (strcmp(pname, "name") == 0) {
 			prev_name = "name";
 			continue;
 		}

		/* find string offset */
		soff = dt_find_string(pname);
		if (soff == 0) {
			prom_printf("WARNING: Can't find string index for"
				    " <%s>, node %s\n", pname, path);
			break;
		}
		prev_name = sstart + soff;

		/* get length */
		l = call_prom("getproplen", 2, 1, node, pname);

		/* sanity checks */
		if (l == PROM_ERROR)
			continue;

		/* push property head */
		dt_push_token(OF_DT_PROP, mem_start, mem_end);
		dt_push_token(l, mem_start, mem_end);
		dt_push_token(soff, mem_start, mem_end);

		/* push property content */
		valp = make_room(mem_start, mem_end, l, 4);
		call_prom("getprop", 4, 1, node, pname, valp, l);
		*mem_start = _ALIGN(*mem_start, 4);

		if (!strcmp(pname, "phandle"))
			has_phandle = 1;
	}

	/* Add a "linux,phandle" property if no "phandle" property already
	 * existed (can happen with OPAL)
	 */
	if (!has_phandle) {
		soff = dt_find_string("linux,phandle");
		if (soff == 0)
			prom_printf("WARNING: Can't find string index for"
				    " <linux-phandle> node %s\n", path);
		else {
			dt_push_token(OF_DT_PROP, mem_start, mem_end);
			dt_push_token(4, mem_start, mem_end);
			dt_push_token(soff, mem_start, mem_end);
			valp = make_room(mem_start, mem_end, 4, 4);
			*(__be32 *)valp = cpu_to_be32(node);
		}
	}

	/* do all our children */
	child = call_prom("child", 1, 1, node);
	while (child != 0) {
		scan_dt_build_struct(child, mem_start, mem_end);
		child = call_prom("peer", 1, 1, child);
	}

	dt_push_token(OF_DT_END_NODE, mem_start, mem_end);
}

static void __init flatten_device_tree(void)
{
	phandle root;
	unsigned long mem_start, mem_end, room;
	struct boot_param_header *hdr;
	char *namep;
	u64 *rsvmap;

	/*
	 * Check how much room we have between alloc top & bottom (+/- a
	 * few pages), crop to 1MB, as this is our "chunk" size
	 */
	room = alloc_top - alloc_bottom - 0x4000;
	if (room > DEVTREE_CHUNK_SIZE)
		room = DEVTREE_CHUNK_SIZE;
	prom_debug("starting device tree allocs at %x\n", alloc_bottom);

	/* Now try to claim that */
	mem_start = (unsigned long)alloc_up(room, PAGE_SIZE);
	if (mem_start == 0)
		prom_panic("Can't allocate initial device-tree chunk\n");
	mem_end = mem_start + room;

	/* Get root of tree */
	root = call_prom("peer", 1, 1, (phandle)0);
	if (root == (phandle)0)
		prom_panic ("couldn't get device tree root\n");

	/* Build header and make room for mem rsv map */ 
	mem_start = _ALIGN(mem_start, 4);
	hdr = make_room(&mem_start, &mem_end,
			sizeof(struct boot_param_header), 4);
	dt_header_start = (unsigned long)hdr;
	rsvmap = make_room(&mem_start, &mem_end, sizeof(mem_reserve_map), 8);

	/* Start of strings */
	mem_start = PAGE_ALIGN(mem_start);
	dt_string_start = mem_start;
	mem_start += 4; /* hole */

	/* Add "linux,phandle" in there, we'll need it */
	namep = make_room(&mem_start, &mem_end, 16, 1);
	strcpy(namep, "linux,phandle");
	mem_start = (unsigned long)namep + strlen(namep) + 1;

	/* Build string array */
	prom_printf("Building dt strings...\n"); 
	scan_dt_build_strings(root, &mem_start, &mem_end);
	dt_string_end = mem_start;

	/* Build structure */
	mem_start = PAGE_ALIGN(mem_start);
	dt_struct_start = mem_start;
	prom_printf("Building dt structure...\n"); 
	scan_dt_build_struct(root, &mem_start, &mem_end);
	dt_push_token(OF_DT_END, &mem_start, &mem_end);
	dt_struct_end = PAGE_ALIGN(mem_start);

	/* Finish header */
	hdr->boot_cpuid_phys = cpu_to_be32(prom.cpu);
	hdr->magic = cpu_to_be32(OF_DT_HEADER);
	hdr->totalsize = cpu_to_be32(dt_struct_end - dt_header_start);
	hdr->off_dt_struct = cpu_to_be32(dt_struct_start - dt_header_start);
	hdr->off_dt_strings = cpu_to_be32(dt_string_start - dt_header_start);
	hdr->dt_strings_size = cpu_to_be32(dt_string_end - dt_string_start);
	hdr->off_mem_rsvmap = cpu_to_be32(((unsigned long)rsvmap) - dt_header_start);
	hdr->version = cpu_to_be32(OF_DT_VERSION);
	/* Version 16 is not backward compatible */
	hdr->last_comp_version = cpu_to_be32(0x10);

	/* Copy the reserve map in */
	memcpy(rsvmap, mem_reserve_map, sizeof(mem_reserve_map));

#ifdef DEBUG_PROM
	{
		int i;
		prom_printf("reserved memory map:\n");
		for (i = 0; i < mem_reserve_cnt; i++)
			prom_printf("  %x - %x\n",
				    be64_to_cpu(mem_reserve_map[i].base),
				    be64_to_cpu(mem_reserve_map[i].size));
	}
#endif
	/* Bump mem_reserve_cnt to cause further reservations to fail
	 * since it's too late.
	 */
	mem_reserve_cnt = MEM_RESERVE_MAP_SIZE;

	prom_printf("Device tree strings 0x%x -> 0x%x\n",
		    dt_string_start, dt_string_end);
	prom_printf("Device tree struct  0x%x -> 0x%x\n",
		    dt_struct_start, dt_struct_end);
}

#ifdef CONFIG_PPC_MAPLE
/* PIBS Version 1.05.0000 04/26/2005 has an incorrect /ht/isa/ranges property.
 * The values are bad, and it doesn't even have the right number of cells. */
static void __init fixup_device_tree_maple(void)
{
	phandle isa;
	u32 rloc = 0x01002000; /* IO space; PCI device = 4 */
	u32 isa_ranges[6];
	char *name;

	name = "/ht@0/isa@4";
	isa = call_prom("finddevice", 1, 1, ADDR(name));
	if (!PHANDLE_VALID(isa)) {
		name = "/ht@0/isa@6";
		isa = call_prom("finddevice", 1, 1, ADDR(name));
		rloc = 0x01003000; /* IO space; PCI device = 6 */
	}
	if (!PHANDLE_VALID(isa))
		return;

	if (prom_getproplen(isa, "ranges") != 12)
		return;
	if (prom_getprop(isa, "ranges", isa_ranges, sizeof(isa_ranges))
		== PROM_ERROR)
		return;

	if (isa_ranges[0] != 0x1 ||
		isa_ranges[1] != 0xf4000000 ||
		isa_ranges[2] != 0x00010000)
		return;

	prom_printf("Fixing up bogus ISA range on Maple/Apache...\n");

	isa_ranges[0] = 0x1;
	isa_ranges[1] = 0x0;
	isa_ranges[2] = rloc;
	isa_ranges[3] = 0x0;
	isa_ranges[4] = 0x0;
	isa_ranges[5] = 0x00010000;
	prom_setprop(isa, name, "ranges",
			isa_ranges, sizeof(isa_ranges));
}

#define CPC925_MC_START		0xf8000000
#define CPC925_MC_LENGTH	0x1000000
/* The values for memory-controller don't have right number of cells */
static void __init fixup_device_tree_maple_memory_controller(void)
{
	phandle mc;
	u32 mc_reg[4];
	char *name = "/hostbridge@f8000000";
	u32 ac, sc;

	mc = call_prom("finddevice", 1, 1, ADDR(name));
	if (!PHANDLE_VALID(mc))
		return;

	if (prom_getproplen(mc, "reg") != 8)
		return;

	prom_getprop(prom.root, "#address-cells", &ac, sizeof(ac));
	prom_getprop(prom.root, "#size-cells", &sc, sizeof(sc));
	if ((ac != 2) || (sc != 2))
		return;

	if (prom_getprop(mc, "reg", mc_reg, sizeof(mc_reg)) == PROM_ERROR)
		return;

	if (mc_reg[0] != CPC925_MC_START || mc_reg[1] != CPC925_MC_LENGTH)
		return;

	prom_printf("Fixing up bogus hostbridge on Maple...\n");

	mc_reg[0] = 0x0;
	mc_reg[1] = CPC925_MC_START;
	mc_reg[2] = 0x0;
	mc_reg[3] = CPC925_MC_LENGTH;
	prom_setprop(mc, name, "reg", mc_reg, sizeof(mc_reg));
}
#else
#define fixup_device_tree_maple()
#define fixup_device_tree_maple_memory_controller()
#endif

#ifdef CONFIG_PPC_CHRP
/*
 * Pegasos and BriQ lacks the "ranges" property in the isa node
 * Pegasos needs decimal IRQ 14/15, not hexadecimal
 * Pegasos has the IDE configured in legacy mode, but advertised as native
 */
static void __init fixup_device_tree_chrp(void)
{
	phandle ph;
	u32 prop[6];
	u32 rloc = 0x01006000; /* IO space; PCI device = 12 */
	char *name;
	int rc;

	name = "/pci@80000000/isa@c";
	ph = call_prom("finddevice", 1, 1, ADDR(name));
	if (!PHANDLE_VALID(ph)) {
		name = "/pci@ff500000/isa@6";
		ph = call_prom("finddevice", 1, 1, ADDR(name));
		rloc = 0x01003000; /* IO space; PCI device = 6 */
	}
	if (PHANDLE_VALID(ph)) {
		rc = prom_getproplen(ph, "ranges");
		if (rc == 0 || rc == PROM_ERROR) {
			prom_printf("Fixing up missing ISA range on Pegasos...\n");

			prop[0] = 0x1;
			prop[1] = 0x0;
			prop[2] = rloc;
			prop[3] = 0x0;
			prop[4] = 0x0;
			prop[5] = 0x00010000;
			prom_setprop(ph, name, "ranges", prop, sizeof(prop));
		}
	}

	name = "/pci@80000000/ide@C,1";
	ph = call_prom("finddevice", 1, 1, ADDR(name));
	if (PHANDLE_VALID(ph)) {
		prom_printf("Fixing up IDE interrupt on Pegasos...\n");
		prop[0] = 14;
		prop[1] = 0x0;
		prom_setprop(ph, name, "interrupts", prop, 2*sizeof(u32));
		prom_printf("Fixing up IDE class-code on Pegasos...\n");
		rc = prom_getprop(ph, "class-code", prop, sizeof(u32));
		if (rc == sizeof(u32)) {
			prop[0] &= ~0x5;
			prom_setprop(ph, name, "class-code", prop, sizeof(u32));
		}
	}
}
#else
#define fixup_device_tree_chrp()
#endif

#if defined(CONFIG_PPC64) && defined(CONFIG_PPC_PMAC)
static void __init fixup_device_tree_pmac(void)
{
	phandle u3, i2c, mpic;
	u32 u3_rev;
	u32 interrupts[2];
	u32 parent;

	/* Some G5s have a missing interrupt definition, fix it up here */
	u3 = call_prom("finddevice", 1, 1, ADDR("/u3@0,f8000000"));
	if (!PHANDLE_VALID(u3))
		return;
	i2c = call_prom("finddevice", 1, 1, ADDR("/u3@0,f8000000/i2c@f8001000"));
	if (!PHANDLE_VALID(i2c))
		return;
	mpic = call_prom("finddevice", 1, 1, ADDR("/u3@0,f8000000/mpic@f8040000"));
	if (!PHANDLE_VALID(mpic))
		return;

	/* check if proper rev of u3 */
	if (prom_getprop(u3, "device-rev", &u3_rev, sizeof(u3_rev))
	    == PROM_ERROR)
		return;
	if (u3_rev < 0x35 || u3_rev > 0x39)
		return;
	/* does it need fixup ? */
	if (prom_getproplen(i2c, "interrupts") > 0)
		return;

	prom_printf("fixing up bogus interrupts for u3 i2c...\n");

	/* interrupt on this revision of u3 is number 0 and level */
	interrupts[0] = 0;
	interrupts[1] = 1;
	prom_setprop(i2c, "/u3@0,f8000000/i2c@f8001000", "interrupts",
		     &interrupts, sizeof(interrupts));
	parent = (u32)mpic;
	prom_setprop(i2c, "/u3@0,f8000000/i2c@f8001000", "interrupt-parent",
		     &parent, sizeof(parent));
}
#else
#define fixup_device_tree_pmac()
#endif

#ifdef CONFIG_PPC_EFIKA
/*
 * The MPC5200 FEC driver requires an phy-handle property to tell it how
 * to talk to the phy.  If the phy-handle property is missing, then this
 * function is called to add the appropriate nodes and link it to the
 * ethernet node.
 */
static void __init fixup_device_tree_efika_add_phy(void)
{
	u32 node;
	char prop[64];
	int rv;

	/* Check if /builtin/ethernet exists - bail if it doesn't */
	node = call_prom("finddevice", 1, 1, ADDR("/builtin/ethernet"));
	if (!PHANDLE_VALID(node))
		return;

	/* Check if the phy-handle property exists - bail if it does */
	rv = prom_getprop(node, "phy-handle", prop, sizeof(prop));
	if (!rv)
		return;

	/*
	 * At this point the ethernet device doesn't have a phy described.
	 * Now we need to add the missing phy node and linkage
	 */

	/* Check for an MDIO bus node - if missing then create one */
	node = call_prom("finddevice", 1, 1, ADDR("/builtin/mdio"));
	if (!PHANDLE_VALID(node)) {
		prom_printf("Adding Ethernet MDIO node\n");
		call_prom("interpret", 1, 1,
			" s\" /builtin\" find-device"
			" new-device"
				" 1 encode-int s\" #address-cells\" property"
				" 0 encode-int s\" #size-cells\" property"
				" s\" mdio\" device-name"
				" s\" fsl,mpc5200b-mdio\" encode-string"
				" s\" compatible\" property"
				" 0xf0003000 0x400 reg"
				" 0x2 encode-int"
				" 0x5 encode-int encode+"
				" 0x3 encode-int encode+"
				" s\" interrupts\" property"
			" finish-device");
	};

	/* Check for a PHY device node - if missing then create one and
	 * give it's phandle to the ethernet node */
	node = call_prom("finddevice", 1, 1,
			 ADDR("/builtin/mdio/ethernet-phy"));
	if (!PHANDLE_VALID(node)) {
		prom_printf("Adding Ethernet PHY node\n");
		call_prom("interpret", 1, 1,
			" s\" /builtin/mdio\" find-device"
			" new-device"
				" s\" ethernet-phy\" device-name"
				" 0x10 encode-int s\" reg\" property"
				" my-self"
				" ihandle>phandle"
			" finish-device"
			" s\" /builtin/ethernet\" find-device"
				" encode-int"
				" s\" phy-handle\" property"
			" device-end");
	}
}

static void __init fixup_device_tree_efika(void)
{
	int sound_irq[3] = { 2, 2, 0 };
	int bcomm_irq[3*16] = { 3,0,0, 3,1,0, 3,2,0, 3,3,0,
				3,4,0, 3,5,0, 3,6,0, 3,7,0,
				3,8,0, 3,9,0, 3,10,0, 3,11,0,
				3,12,0, 3,13,0, 3,14,0, 3,15,0 };
	u32 node;
	char prop[64];
	int rv, len;

	/* Check if we're really running on a EFIKA */
	node = call_prom("finddevice", 1, 1, ADDR("/"));
	if (!PHANDLE_VALID(node))
		return;

	rv = prom_getprop(node, "model", prop, sizeof(prop));
	if (rv == PROM_ERROR)
		return;
	if (strcmp(prop, "EFIKA5K2"))
		return;

	prom_printf("Applying EFIKA device tree fixups\n");

	/* Claiming to be 'chrp' is death */
	node = call_prom("finddevice", 1, 1, ADDR("/"));
	rv = prom_getprop(node, "device_type", prop, sizeof(prop));
	if (rv != PROM_ERROR && (strcmp(prop, "chrp") == 0))
		prom_setprop(node, "/", "device_type", "efika", sizeof("efika"));

	/* CODEGEN,description is exposed in /proc/cpuinfo so
	   fix that too */
	rv = prom_getprop(node, "CODEGEN,description", prop, sizeof(prop));
	if (rv != PROM_ERROR && (strstr(prop, "CHRP")))
		prom_setprop(node, "/", "CODEGEN,description",
			     "Efika 5200B PowerPC System",
			     sizeof("Efika 5200B PowerPC System"));

	/* Fixup bestcomm interrupts property */
	node = call_prom("finddevice", 1, 1, ADDR("/builtin/bestcomm"));
	if (PHANDLE_VALID(node)) {
		len = prom_getproplen(node, "interrupts");
		if (len == 12) {
			prom_printf("Fixing bestcomm interrupts property\n");
			prom_setprop(node, "/builtin/bestcom", "interrupts",
				     bcomm_irq, sizeof(bcomm_irq));
		}
	}

	/* Fixup sound interrupts property */
	node = call_prom("finddevice", 1, 1, ADDR("/builtin/sound"));
	if (PHANDLE_VALID(node)) {
		rv = prom_getprop(node, "interrupts", prop, sizeof(prop));
		if (rv == PROM_ERROR) {
			prom_printf("Adding sound interrupts property\n");
			prom_setprop(node, "/builtin/sound", "interrupts",
				     sound_irq, sizeof(sound_irq));
		}
	}

	/* Make sure ethernet phy-handle property exists */
	fixup_device_tree_efika_add_phy();
}
#else
#define fixup_device_tree_efika()
#endif

static void __init fixup_device_tree(void)
{
	fixup_device_tree_maple();
	fixup_device_tree_maple_memory_controller();
	fixup_device_tree_chrp();
	fixup_device_tree_pmac();
	fixup_device_tree_efika();
}

static void __init prom_find_boot_cpu(void)
{
	__be32 rval;
	ihandle prom_cpu;
	phandle cpu_pkg;

	rval = 0;
	if (prom_getprop(prom.chosen, "cpu", &rval, sizeof(rval)) <= 0)
		return;
	prom_cpu = be32_to_cpu(rval);

	cpu_pkg = call_prom("instance-to-package", 1, 1, prom_cpu);

	prom_getprop(cpu_pkg, "reg", &rval, sizeof(rval));
	prom.cpu = be32_to_cpu(rval);

	prom_debug("Booting CPU hw index = %lu\n", prom.cpu);
}

static void __init prom_check_initrd(unsigned long r3, unsigned long r4)
{
#ifdef CONFIG_BLK_DEV_INITRD
	if (r3 && r4 && r4 != 0xdeadbeef) {
		__be64 val;

		prom_initrd_start = is_kernel_addr(r3) ? __pa(r3) : r3;
		prom_initrd_end = prom_initrd_start + r4;

		val = cpu_to_be64(prom_initrd_start);
		prom_setprop(prom.chosen, "/chosen", "linux,initrd-start",
			     &val, sizeof(val));
		val = cpu_to_be64(prom_initrd_end);
		prom_setprop(prom.chosen, "/chosen", "linux,initrd-end",
			     &val, sizeof(val));

		reserve_mem(prom_initrd_start,
			    prom_initrd_end - prom_initrd_start);

		prom_debug("initrd_start=0x%x\n", prom_initrd_start);
		prom_debug("initrd_end=0x%x\n", prom_initrd_end);
	}
#endif /* CONFIG_BLK_DEV_INITRD */
}

#ifdef CONFIG_PPC64
#ifdef CONFIG_RELOCATABLE
static void reloc_toc(void)
{
}

static void unreloc_toc(void)
{
}
#else
static void __reloc_toc(unsigned long offset, unsigned long nr_entries)
{
	unsigned long i;
	unsigned long *toc_entry;

	/* Get the start of the TOC by using r2 directly. */
	asm volatile("addi %0,2,-0x8000" : "=b" (toc_entry));

	for (i = 0; i < nr_entries; i++) {
		*toc_entry = *toc_entry + offset;
		toc_entry++;
	}
}

static void reloc_toc(void)
{
	unsigned long offset = reloc_offset();
	unsigned long nr_entries =
		(__prom_init_toc_end - __prom_init_toc_start) / sizeof(long);

	__reloc_toc(offset, nr_entries);

	mb();
}

static void unreloc_toc(void)
{
	unsigned long offset = reloc_offset();
	unsigned long nr_entries =
		(__prom_init_toc_end - __prom_init_toc_start) / sizeof(long);

	mb();

	__reloc_toc(-offset, nr_entries);
}
#endif
#endif

/*
 * We enter here early on, when the Open Firmware prom is still
 * handling exceptions and the MMU hash table for us.
 */

unsigned long __init prom_init(unsigned long r3, unsigned long r4,
			       unsigned long pp,
			       unsigned long r6, unsigned long r7,
			       unsigned long kbase)
{	
	unsigned long hdr;

#ifdef CONFIG_PPC32
	unsigned long offset = reloc_offset();
	reloc_got2(offset);
#else
	reloc_toc();
#endif

	/*
	 * First zero the BSS
	 */
	memset(&__bss_start, 0, __bss_stop - __bss_start);

	/*
	 * Init interface to Open Firmware, get some node references,
	 * like /chosen
	 */
	prom_init_client_services(pp);

	/*
	 * See if this OF is old enough that we need to do explicit maps
	 * and other workarounds
	 */
	prom_find_mmu();

	/*
	 * Init prom stdout device
	 */
	prom_init_stdout();

	prom_printf("Preparing to boot %s", linux_banner);

	/*
	 * Get default machine type. At this point, we do not differentiate
	 * between pSeries SMP and pSeries LPAR
	 */
	of_platform = prom_find_machine_type();
	prom_printf("Detected machine type: %x\n", of_platform);

#ifndef CONFIG_NONSTATIC_KERNEL
	/* Bail if this is a kdump kernel. */
	if (PHYSICAL_START > 0)
		prom_panic("Error: You can't boot a kdump kernel from OF!\n");
#endif

	/*
	 * Check for an initrd
	 */
	prom_check_initrd(r3, r4);

#if defined(CONFIG_PPC_PSERIES) || defined(CONFIG_PPC_POWERNV)
	/*
	 * On pSeries, inform the firmware about our capabilities
	 */
	if (of_platform == PLATFORM_PSERIES ||
	    of_platform == PLATFORM_PSERIES_LPAR)
		prom_send_capabilities();
#endif

	/*
	 * Copy the CPU hold code
	 */
	if (of_platform != PLATFORM_POWERMAC)
		copy_and_flush(0, kbase, 0x100, 0);

	/*
	 * Do early parsing of command line
	 */
	early_cmdline_parse();

	/*
	 * Initialize memory management within prom_init
	 */
	prom_init_mem();

	/*
	 * Determine which cpu is actually running right _now_
	 */
	prom_find_boot_cpu();

	/* 
	 * Initialize display devices
	 */
	prom_check_displays();

#if defined(CONFIG_PPC64) && defined(__BIG_ENDIAN__)
	/*
	 * Initialize IOMMU (TCE tables) on pSeries. Do that before anything else
	 * that uses the allocator, we need to make sure we get the top of memory
	 * available for us here...
	 */
	if (of_platform == PLATFORM_PSERIES)
		prom_initialize_tce_table();
#endif

	/*
	 * On non-powermacs, try to instantiate RTAS. PowerMacs don't
	 * have a usable RTAS implementation.
	 */
	if (of_platform != PLATFORM_POWERMAC &&
	    of_platform != PLATFORM_OPAL)
		prom_instantiate_rtas();

#ifdef CONFIG_PPC_POWERNV
#ifdef __BIG_ENDIAN__
	/* Detect HAL and try instanciating it & doing takeover */
	if (of_platform == PLATFORM_PSERIES_LPAR) {
		prom_query_opal();
		if (of_platform == PLATFORM_OPAL) {
			prom_opal_hold_cpus();
			prom_opal_takeover();
		}
	} else
#endif /* __BIG_ENDIAN__ */
	if (of_platform == PLATFORM_OPAL)
		prom_instantiate_opal();
#endif /* CONFIG_PPC_POWERNV */

#ifdef CONFIG_PPC64
	/* instantiate sml */
	prom_instantiate_sml();
#endif

	/*
	 * On non-powermacs, put all CPUs in spin-loops.
	 *
	 * PowerMacs use a different mechanism to spin CPUs
	 */
	if (of_platform != PLATFORM_POWERMAC &&
	    of_platform != PLATFORM_OPAL)
		prom_hold_cpus();

	/*
	 * Fill in some infos for use by the kernel later on
	 */
	if (prom_memory_limit) {
		__be64 val = cpu_to_be64(prom_memory_limit);
		prom_setprop(prom.chosen, "/chosen", "linux,memory-limit",
			     &val, sizeof(val));
	}
#ifdef CONFIG_PPC64
	if (prom_iommu_off)
		prom_setprop(prom.chosen, "/chosen", "linux,iommu-off",
			     NULL, 0);

	if (prom_iommu_force_on)
		prom_setprop(prom.chosen, "/chosen", "linux,iommu-force-on",
			     NULL, 0);

	if (prom_tce_alloc_start) {
		prom_setprop(prom.chosen, "/chosen", "linux,tce-alloc-start",
			     &prom_tce_alloc_start,
			     sizeof(prom_tce_alloc_start));
		prom_setprop(prom.chosen, "/chosen", "linux,tce-alloc-end",
			     &prom_tce_alloc_end,
			     sizeof(prom_tce_alloc_end));
	}
#endif

	/*
	 * Fixup any known bugs in the device-tree
	 */
	fixup_device_tree();

	/*
	 * Now finally create the flattened device-tree
	 */
	prom_printf("copying OF device tree...\n");
	flatten_device_tree();

	/*
	 * in case stdin is USB and still active on IBM machines...
	 * Unfortunately quiesce crashes on some powermacs if we have
	 * closed stdin already (in particular the powerbook 101). It
	 * appears that the OPAL version of OFW doesn't like it either.
	 */
	if (of_platform != PLATFORM_POWERMAC &&
	    of_platform != PLATFORM_OPAL)
		prom_close_stdin();

	/*
	 * Call OF "quiesce" method to shut down pending DMA's from
	 * devices etc...
	 */
	prom_printf("Calling quiesce...\n");
	call_prom("quiesce", 0, 0);

	/*
	 * And finally, call the kernel passing it the flattened device
	 * tree and NULL as r5, thus triggering the new entry point which
	 * is common to us and kexec
	 */
	hdr = dt_header_start;

	/* Don't print anything after quiesce under OPAL, it crashes OFW */
	if (of_platform != PLATFORM_OPAL) {
		prom_printf("returning from prom_init\n");
		prom_debug("->dt_header_start=0x%x\n", hdr);
	}

#ifdef CONFIG_PPC32
	reloc_got2(-offset);
#else
	unreloc_toc();
#endif

#ifdef CONFIG_PPC_EARLY_DEBUG_OPAL
	/* OPAL early debug gets the OPAL base & entry in r8 and r9 */
	__start(hdr, kbase, 0, 0, 0,
		prom_opal_base, prom_opal_entry);
#else
	__start(hdr, kbase, 0, 0, 0, 0, 0);
#endif

	return 0;
}

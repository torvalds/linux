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
#include <linux/config.h>
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
#include <asm/system.h>
#include <asm/mmu.h>
#include <asm/pgtable.h>
#include <asm/pci.h>
#include <asm/iommu.h>
#include <asm/btext.h>
#include <asm/sections.h>
#include <asm/machdep.h>

#ifdef CONFIG_LOGO_LINUX_CLUT224
#include <linux/linux_logo.h>
extern const struct linux_logo logo_linux_clut224;
#endif

/*
 * Properties whose value is longer than this get excluded from our
 * copy of the device tree. This value does need to be big enough to
 * ensure that we don't lose things like the interrupt-map property
 * on a PCI-PCI bridge.
 */
#define MAX_PROPERTY_LENGTH	(1UL * 1024 * 1024)

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
 * On ppc64 we have to relocate the references explicitly with
 * RELOC.  (Note that strings count as static variables.)
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
#ifdef CONFIG_PPC64
#define RELOC(x)        (*PTRRELOC(&(x)))
#define ADDR(x)		(u32) add_reloc_offset((unsigned long)(x))
#define OF_WORKAROUNDS	0
#else
#define RELOC(x)	(x)
#define ADDR(x)		(u32) (x)
#define OF_WORKAROUNDS	of_workarounds
int of_workarounds;
#endif

#define OF_WA_CLAIM	1	/* do phys/virt claim separately, then map */
#define OF_WA_LONGTRAIL	2	/* work around longtrail bugs */

#define PROM_BUG() do {						\
        prom_printf("kernel BUG at %s line 0x%x!\n",		\
		    RELOC(__FILE__), __LINE__);			\
        __asm__ __volatile__(".long " BUG_ILLEGAL_INSTR);	\
} while (0)

#ifdef DEBUG_PROM
#define prom_debug(x...)	prom_printf(x)
#else
#define prom_debug(x...)
#endif


typedef u32 prom_arg_t;

struct prom_args {
        u32 service;
        u32 nargs;
        u32 nret;
        prom_arg_t args[10];
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
	u64	base;
	u64	size;
};

typedef u32 cell_t;

extern void __start(unsigned long r3, unsigned long r4, unsigned long r5);

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
static int __initdata iommu_force_on;
static int __initdata ppc64_iommu_off;
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

static int __initdata of_platform;

static char __initdata prom_cmd_line[COMMAND_LINE_SIZE];

static unsigned long __initdata alloc_top;
static unsigned long __initdata alloc_top_high;
static unsigned long __initdata alloc_bottom;
static unsigned long __initdata rmo_top;
static unsigned long __initdata ram_top;

static struct mem_map_entry __initdata mem_reserve_map[MEM_RESERVE_MAP_SIZE];
static int __initdata mem_reserve_cnt;

static cell_t __initdata regbuf[1024];


#define MAX_CPU_THREADS 2

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

	args.service = ADDR(service);
	args.nargs = nargs;
	args.nret = nret;

	va_start(list, nret);
	for (i = 0; i < nargs; i++)
		args.args[i] = va_arg(list, prom_arg_t);
	va_end(list);

	for (i = 0; i < nret; i++)
		args.args[nargs+i] = 0;

	if (enter_prom(&args, RELOC(prom_entry)) < 0)
		return PROM_ERROR;

	return (nret > 0) ? args.args[nargs] : 0;
}

static int __init call_prom_ret(const char *service, int nargs, int nret,
				prom_arg_t *rets, ...)
{
	int i;
	struct prom_args args;
	va_list list;

	args.service = ADDR(service);
	args.nargs = nargs;
	args.nret = nret;

	va_start(list, rets);
	for (i = 0; i < nargs; i++)
		args.args[i] = va_arg(list, prom_arg_t);
	va_end(list);

	for (i = 0; i < nret; i++)
		args.args[nargs+i] = 0;

	if (enter_prom(&args, RELOC(prom_entry)) < 0)
		return PROM_ERROR;

	if (rets != NULL)
		for (i = 1; i < nret; ++i)
			rets[i-1] = args.args[nargs+i];

	return (nret > 0) ? args.args[nargs] : 0;
}


static void __init prom_print(const char *msg)
{
	const char *p, *q;
	struct prom_t *_prom = &RELOC(prom);

	if (_prom->stdout == 0)
		return;

	for (p = msg; *p != 0; p = q) {
		for (q = p; *q != 0 && *q != '\n'; ++q)
			;
		if (q > p)
			call_prom("write", 3, 1, _prom->stdout, p, q - p);
		if (*q == 0)
			break;
		++q;
		call_prom("write", 3, 1, _prom->stdout, ADDR("\r\n"), 2);
	}
}


static void __init prom_print_hex(unsigned long val)
{
	int i, nibbles = sizeof(val)*2;
	char buf[sizeof(val)*2+1];
	struct prom_t *_prom = &RELOC(prom);

	for (i = nibbles-1;  i >= 0;  i--) {
		buf[i] = (val & 0xf) + '0';
		if (buf[i] > '9')
			buf[i] += ('a'-'0'-10);
		val >>= 4;
	}
	buf[nibbles] = '\0';
	call_prom("write", 3, 1, _prom->stdout, buf, nibbles);
}


static void __init prom_printf(const char *format, ...)
{
	const char *p, *q, *s;
	va_list args;
	unsigned long v;
	struct prom_t *_prom = &RELOC(prom);

	va_start(args, format);
#ifdef CONFIG_PPC64
	format = PTRRELOC(format);
#endif
	for (p = format; *p != 0; p = q) {
		for (q = p; *q != 0 && *q != '\n' && *q != '%'; ++q)
			;
		if (q > p)
			call_prom("write", 3, 1, _prom->stdout, p, q - p);
		if (*q == 0)
			break;
		if (*q == '\n') {
			++q;
			call_prom("write", 3, 1, _prom->stdout,
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
		}
	}
}


static unsigned int __init prom_claim(unsigned long virt, unsigned long size,
				unsigned long align)
{
	struct prom_t *_prom = &RELOC(prom);

	if (align == 0 && (OF_WORKAROUNDS & OF_WA_CLAIM)) {
		/*
		 * Old OF requires we claim physical and virtual separately
		 * and then map explicitly (assuming virtual mode)
		 */
		int ret;
		prom_arg_t result;

		ret = call_prom_ret("call-method", 5, 2, &result,
				    ADDR("claim"), _prom->memory,
				    align, size, virt);
		if (ret != 0 || result == -1)
			return -1;
		ret = call_prom_ret("call-method", 5, 2, &result,
				    ADDR("claim"), _prom->mmumap,
				    align, size, virt);
		if (ret != 0) {
			call_prom("call-method", 4, 1, ADDR("release"),
				  _prom->memory, size, virt);
			return -1;
		}
		/* the 0x12 is M (coherence) + PP == read/write */
		call_prom("call-method", 6, 1,
			  ADDR("map"), _prom->mmumap, 0x12, size, virt, virt);
		return virt;
	}
	return call_prom("claim", 3, 1, (prom_arg_t)virt, (prom_arg_t)size,
			 (prom_arg_t)align);
}

static void __init __attribute__((noreturn)) prom_panic(const char *reason)
{
#ifdef CONFIG_PPC64
	reason = PTRRELOC(reason);
#endif
	prom_print(reason);
	/* Do not call exit because it clears the screen on pmac
	 * it also causes some sort of double-fault on early pmacs */
	if (RELOC(of_platform) == PLATFORM_POWERMAC)
		asm("trap\n");

	/* ToDo: should put up an SRC here on p/iSeries */
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
	add_string(&p, tohex(strlen(RELOC(pname))));
	add_string(&p, "property");
	*p = 0;
	return call_prom("interpret", 1, 1, (u32)(unsigned long) cmd);
}

/* We can't use the standard versions because of RELOC headaches. */
#define isxdigit(c)	(('0' <= (c) && (c) <= '9') \
			 || ('a' <= (c) && (c) <= 'f') \
			 || ('A' <= (c) && (c) <= 'F'))

#define isdigit(c)	('0' <= (c) && (c) <= '9')
#define islower(c)	('a' <= (c) && (c) <= 'z')
#define toupper(c)	(islower(c) ? ((c) - 'a' + 'A') : (c))

unsigned long prom_strtoul(const char *cp, const char **endp)
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

unsigned long prom_memparse(const char *ptr, const char **retptr)
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
	struct prom_t *_prom = &RELOC(prom);
	const char *opt;
	char *p;
	int l = 0;

	RELOC(prom_cmd_line[0]) = 0;
	p = RELOC(prom_cmd_line);
	if ((long)_prom->chosen > 0)
		l = prom_getprop(_prom->chosen, "bootargs", p, COMMAND_LINE_SIZE-1);
#ifdef CONFIG_CMDLINE
	if (l == 0) /* dbl check */
		strlcpy(RELOC(prom_cmd_line),
			RELOC(CONFIG_CMDLINE), sizeof(prom_cmd_line));
#endif /* CONFIG_CMDLINE */
	prom_printf("command line: %s\n", RELOC(prom_cmd_line));

#ifdef CONFIG_PPC64
	opt = strstr(RELOC(prom_cmd_line), RELOC("iommu="));
	if (opt) {
		prom_printf("iommu opt is: %s\n", opt);
		opt += 6;
		while (*opt && *opt == ' ')
			opt++;
		if (!strncmp(opt, RELOC("off"), 3))
			RELOC(ppc64_iommu_off) = 1;
		else if (!strncmp(opt, RELOC("force"), 5))
			RELOC(iommu_force_on) = 1;
	}
#endif
}

#ifdef CONFIG_PPC_PSERIES
/*
 * There are two methods for telling firmware what our capabilities are.
 * Newer machines have an "ibm,client-architecture-support" method on the
 * root node.  For older machines, we have to call the "process-elf-header"
 * method in the /packages/elf-loader node, passing it a fake 32-bit
 * ELF header containing a couple of PT_NOTE sections that contain
 * structures that contain various information.
 */

/*
 * New method - extensible architecture description vector.
 *
 * Because the description vector contains a mix of byte and word
 * values, we declare it as an unsigned char array, and use this
 * macro to put word values in.
 */
#define W(x)	((x) >> 24) & 0xff, ((x) >> 16) & 0xff, \
		((x) >> 8) & 0xff, (x) & 0xff

/* Option vector bits - generic bits in byte 1 */
#define OV_IGNORE		0x80	/* ignore this vector */
#define OV_CESSATION_POLICY	0x40	/* halt if unsupported option present*/

/* Option vector 1: processor architectures supported */
#define OV1_PPC_2_00		0x80	/* set if we support PowerPC 2.00 */
#define OV1_PPC_2_01		0x40	/* set if we support PowerPC 2.01 */
#define OV1_PPC_2_02		0x20	/* set if we support PowerPC 2.02 */
#define OV1_PPC_2_03		0x10	/* set if we support PowerPC 2.03 */
#define OV1_PPC_2_04		0x08	/* set if we support PowerPC 2.04 */
#define OV1_PPC_2_05		0x04	/* set if we support PowerPC 2.05 */

/* Option vector 2: Open Firmware options supported */
#define OV2_REAL_MODE		0x20	/* set if we want OF in real mode */

/* Option vector 3: processor options supported */
#define OV3_FP			0x80	/* floating point */
#define OV3_VMX			0x40	/* VMX/Altivec */

/* Option vector 5: PAPR/OF options supported */
#define OV5_LPAR		0x80	/* logical partitioning supported */
#define OV5_SPLPAR		0x40	/* shared-processor LPAR supported */
/* ibm,dynamic-reconfiguration-memory property supported */
#define OV5_DRCONF_MEMORY	0x20
#define OV5_LARGE_PAGES		0x10	/* large pages supported */

/*
 * The architecture vector has an array of PVR mask/value pairs,
 * followed by # option vectors - 1, followed by the option vectors.
 */
static unsigned char ibm_architecture_vec[] = {
	W(0xfffe0000), W(0x003a0000),	/* POWER5/POWER5+ */
	W(0xffff0000), W(0x003e0000),	/* POWER6 */
	W(0xfffffffe), W(0x0f000001),	/* all 2.04-compliant and earlier */
	5 - 1,				/* 5 option vectors */

	/* option vector 1: processor architectures supported */
	3 - 1,				/* length */
	0,				/* don't ignore, don't halt */
	OV1_PPC_2_00 | OV1_PPC_2_01 | OV1_PPC_2_02 | OV1_PPC_2_03 |
	OV1_PPC_2_04 | OV1_PPC_2_05,

	/* option vector 2: Open Firmware options supported */
	34 - 1,				/* length */
	OV2_REAL_MODE,
	0, 0,
	W(0xffffffff),			/* real_base */
	W(0xffffffff),			/* real_size */
	W(0xffffffff),			/* virt_base */
	W(0xffffffff),			/* virt_size */
	W(0xffffffff),			/* load_base */
	W(64),				/* 128MB min RMA */
	W(0xffffffff),			/* full client load */
	0,				/* min RMA percentage of total RAM */
	48,				/* max log_2(hash table size) */

	/* option vector 3: processor options supported */
	3 - 1,				/* length */
	0,				/* don't ignore, don't halt */
	OV3_FP | OV3_VMX,

	/* option vector 4: IBM PAPR implementation */
	2 - 1,				/* length */
	0,				/* don't halt */

	/* option vector 5: PAPR/OF options */
	3 - 1,				/* length */
	0,				/* don't ignore, don't halt */
	OV5_LPAR | OV5_SPLPAR | OV5_LARGE_PAGES,
};

/* Old method - ELF header with PT_NOTE sections */
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

static void __init prom_send_capabilities(void)
{
	ihandle elfloader, root;
	prom_arg_t ret;

	root = call_prom("open", 1, 1, ADDR("/"));
	if (root != 0) {
		/* try calling the ibm,client-architecture-support method */
		if (call_prom_ret("call-method", 3, 2, &ret,
				  ADDR("ibm,client-architecture-support"),
				  ADDR(ibm_architecture_vec)) == 0) {
			/* the call exists... */
			if (ret)
				prom_printf("WARNING: ibm,client-architecture"
					    "-support call FAILED!\n");
			call_prom("close", 1, 0, root);
			return;
		}
		call_prom("close", 1, 0, root);
	}

	/* no ibm,client-architecture-support call, try the old way */
	elfloader = call_prom("open", 1, 1, ADDR("/packages/elf-loader"));
	if (elfloader == 0) {
		prom_printf("couldn't open /packages/elf-loader\n");
		return;
	}
	call_prom("call-method", 3, 1, ADDR("process-elf-header"),
			elfloader, ADDR(&fake_elf));
	call_prom("close", 1, 0, elfloader);
}
#endif

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
	unsigned long base = RELOC(alloc_bottom);
	unsigned long addr = 0;

	if (align)
		base = _ALIGN_UP(base, align);
	prom_debug("alloc_up(%x, %x)\n", size, align);
	if (RELOC(ram_top) == 0)
		prom_panic("alloc_up() called with mem not initialized\n");

	if (align)
		base = _ALIGN_UP(RELOC(alloc_bottom), align);
	else
		base = RELOC(alloc_bottom);

	for(; (base + size) <= RELOC(alloc_top); 
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
	RELOC(alloc_bottom) = addr;

	prom_debug(" -> %x\n", addr);
	prom_debug("  alloc_bottom : %x\n", RELOC(alloc_bottom));
	prom_debug("  alloc_top    : %x\n", RELOC(alloc_top));
	prom_debug("  alloc_top_hi : %x\n", RELOC(alloc_top_high));
	prom_debug("  rmo_top      : %x\n", RELOC(rmo_top));
	prom_debug("  ram_top      : %x\n", RELOC(ram_top));

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
		   highmem ? RELOC("(high)") : RELOC("(low)"));
	if (RELOC(ram_top) == 0)
		prom_panic("alloc_down() called with mem not initialized\n");

	if (highmem) {
		/* Carve out storage for the TCE table. */
		addr = _ALIGN_DOWN(RELOC(alloc_top_high) - size, align);
		if (addr <= RELOC(alloc_bottom))
			return 0;
		/* Will we bump into the RMO ? If yes, check out that we
		 * didn't overlap existing allocations there, if we did,
		 * we are dead, we must be the first in town !
		 */
		if (addr < RELOC(rmo_top)) {
			/* Good, we are first */
			if (RELOC(alloc_top) == RELOC(rmo_top))
				RELOC(alloc_top) = RELOC(rmo_top) = addr;
			else
				return 0;
		}
		RELOC(alloc_top_high) = addr;
		goto bail;
	}

	base = _ALIGN_DOWN(RELOC(alloc_top) - size, align);
	for (; base > RELOC(alloc_bottom);
	     base = _ALIGN_DOWN(base - 0x100000, align))  {
		prom_debug("    trying: 0x%x\n\r", base);
		addr = (unsigned long)prom_claim(base, size, 0);
		if (addr != PROM_ERROR && addr != 0)
			break;
		addr = 0;
	}
	if (addr == 0)
		return 0;
	RELOC(alloc_top) = addr;

 bail:
	prom_debug(" -> %x\n", addr);
	prom_debug("  alloc_bottom : %x\n", RELOC(alloc_bottom));
	prom_debug("  alloc_top    : %x\n", RELOC(alloc_top));
	prom_debug("  alloc_top_hi : %x\n", RELOC(alloc_top_high));
	prom_debug("  rmo_top      : %x\n", RELOC(rmo_top));
	prom_debug("  ram_top      : %x\n", RELOC(ram_top));

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
	r = *p++;
#ifdef CONFIG_PPC64
	if (s > 1) {
		r <<= 32;
		r |= *(p++);
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
static void reserve_mem(u64 base, u64 size)
{
	u64 top = base + size;
	unsigned long cnt = RELOC(mem_reserve_cnt);

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
	RELOC(mem_reserve_map)[cnt].base = base;
	RELOC(mem_reserve_map)[cnt].size = size;
	RELOC(mem_reserve_cnt) = cnt + 1;
}

/*
 * Initialize memory allocation mecanism, parse "memory" nodes and
 * obtain that way the top of memory and RMO to setup out local allocator
 */
static void __init prom_init_mem(void)
{
	phandle node;
	char *path, type[64];
	unsigned int plen;
	cell_t *p, *endp;
	struct prom_t *_prom = &RELOC(prom);
	u32 rac, rsc;

	/*
	 * We iterate the memory nodes to find
	 * 1) top of RMO (first node)
	 * 2) top of memory
	 */
	rac = 2;
	prom_getprop(_prom->root, "#address-cells", &rac, sizeof(rac));
	rsc = 1;
	prom_getprop(_prom->root, "#size-cells", &rsc, sizeof(rsc));
	prom_debug("root_addr_cells: %x\n", (unsigned long) rac);
	prom_debug("root_size_cells: %x\n", (unsigned long) rsc);

	prom_debug("scanning memory:\n");
	path = RELOC(prom_scratch);

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
		if (strcmp(type, RELOC("memory")))
			continue;

		plen = prom_getprop(node, "reg", RELOC(regbuf), sizeof(regbuf));
		if (plen > sizeof(regbuf)) {
			prom_printf("memory node too large for buffer !\n");
			plen = sizeof(regbuf);
		}
		p = RELOC(regbuf);
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
			if (base == 0 && (RELOC(of_platform) & PLATFORM_LPAR))
				RELOC(rmo_top) = size;
			if ((base + size) > RELOC(ram_top))
				RELOC(ram_top) = base + size;
		}
	}

	RELOC(alloc_bottom) = PAGE_ALIGN((unsigned long)&RELOC(_end) + 0x4000);

	/* Check if we have an initrd after the kernel, if we do move our bottom
	 * point to after it
	 */
	if (RELOC(prom_initrd_start)) {
		if (RELOC(prom_initrd_end) > RELOC(alloc_bottom))
			RELOC(alloc_bottom) = PAGE_ALIGN(RELOC(prom_initrd_end));
	}

	/*
	 * Setup our top alloc point, that is top of RMO or top of
	 * segment 0 when running non-LPAR.
	 * Some RS64 machines have buggy firmware where claims up at
	 * 1GB fail.  Cap at 768MB as a workaround.
	 * Since 768MB is plenty of room, and we need to cap to something
	 * reasonable on 32-bit, cap at 768MB on all machines.
	 */
	if (!RELOC(rmo_top))
		RELOC(rmo_top) = RELOC(ram_top);
	RELOC(rmo_top) = min(0x30000000ul, RELOC(rmo_top));
	RELOC(alloc_top) = RELOC(rmo_top);
	RELOC(alloc_top_high) = RELOC(ram_top);

	prom_printf("memory layout at init:\n");
	prom_printf("  alloc_bottom : %x\n", RELOC(alloc_bottom));
	prom_printf("  alloc_top    : %x\n", RELOC(alloc_top));
	prom_printf("  alloc_top_hi : %x\n", RELOC(alloc_top_high));
	prom_printf("  rmo_top      : %x\n", RELOC(rmo_top));
	prom_printf("  ram_top      : %x\n", RELOC(ram_top));
}


/*
 * Allocate room for and instantiate RTAS
 */
static void __init prom_instantiate_rtas(void)
{
	phandle rtas_node;
	ihandle rtas_inst;
	u32 base, entry = 0;
	u32 size = 0;

	prom_debug("prom_instantiate_rtas: start...\n");

	rtas_node = call_prom("finddevice", 1, 1, ADDR("/rtas"));
	prom_debug("rtas_node: %x\n", rtas_node);
	if (!PHANDLE_VALID(rtas_node))
		return;

	prom_getprop(rtas_node, "rtas-size", &size, sizeof(size));
	if (size == 0)
		return;

	base = alloc_down(size, PAGE_SIZE, 0);
	if (base == 0) {
		prom_printf("RTAS allocation failed !\n");
		return;
	}

	rtas_inst = call_prom("open", 1, 1, ADDR("/rtas"));
	if (!IHANDLE_VALID(rtas_inst)) {
		prom_printf("opening rtas package failed (%x)\n", rtas_inst);
		return;
	}

	prom_printf("instantiating rtas at 0x%x ...", base);

	if (call_prom_ret("call-method", 3, 2, &entry,
			  ADDR("instantiate-rtas"),
			  rtas_inst, base) != 0
	    || entry == 0) {
		prom_printf(" failed\n");
		return;
	}
	prom_printf(" done\n");

	reserve_mem(base, size);

	prom_setprop(rtas_node, "/rtas", "linux,rtas-base",
		     &base, sizeof(base));
	prom_setprop(rtas_node, "/rtas", "linux,rtas-entry",
		     &entry, sizeof(entry));

	prom_debug("rtas base     = 0x%x\n", base);
	prom_debug("rtas entry    = 0x%x\n", entry);
	prom_debug("rtas size     = 0x%x\n", (long)size);

	prom_debug("prom_instantiate_rtas: end...\n");
}

#ifdef CONFIG_PPC64
/*
 * Allocate room for and initialize TCE tables
 */
static void __init prom_initialize_tce_table(void)
{
	phandle node;
	ihandle phb_node;
	char compatible[64], type[64], model[64];
	char *path = RELOC(prom_scratch);
	u64 base, align;
	u32 minalign, minsize;
	u64 tce_entry, *tce_entryp;
	u64 local_alloc_top, local_alloc_bottom;
	u64 i;

	if (RELOC(ppc64_iommu_off))
		return;

	prom_debug("starting prom_initialize_tce_table\n");

	/* Cache current top of allocs so we reserve a single block */
	local_alloc_top = RELOC(alloc_top_high);
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

		if ((type[0] == 0) || (strstr(type, RELOC("pci")) == NULL))
			continue;

		/* Keep the old logic in tack to avoid regression. */
		if (compatible[0] != 0) {
			if ((strstr(compatible, RELOC("python")) == NULL) &&
			    (strstr(compatible, RELOC("Speedwagon")) == NULL) &&
			    (strstr(compatible, RELOC("Winnipeg")) == NULL))
				continue;
		} else if (model[0] != 0) {
			if ((strstr(model, RELOC("ython")) == NULL) &&
			    (strstr(model, RELOC("peedwagon")) == NULL) &&
			    (strstr(model, RELOC("innipeg")) == NULL))
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
		if (__is_processor(PV_POWER4) || __is_processor(PV_POWER4p))
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
		memset(path, 0, sizeof(path));
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
		tce_entryp = (unsigned long *)base;
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
	RELOC(prom_tce_alloc_start) = local_alloc_bottom;
	RELOC(prom_tce_alloc_end) = local_alloc_top;

	/* Flag the first invalid entry */
	prom_debug("ending prom_initialize_tce_table\n");
}
#endif

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
extern void __secondary_hold(void);
extern unsigned long __secondary_hold_spinloop;
extern unsigned long __secondary_hold_acknowledge;

/*
 * We want to reference the copy of __secondary_hold_* in the
 * 0 - 0x100 address range
 */
#define LOW_ADDR(x)	(((unsigned long) &(x)) & 0xff)

static void __init prom_hold_cpus(void)
{
	unsigned long i;
	unsigned int reg;
	phandle node;
	char type[64];
	int cpuid = 0;
	unsigned int interrupt_server[MAX_CPU_THREADS];
	unsigned int cpu_threads, hw_cpu_num;
	int propsize;
	struct prom_t *_prom = &RELOC(prom);
	unsigned long *spinloop
		= (void *) LOW_ADDR(__secondary_hold_spinloop);
	unsigned long *acknowledge
		= (void *) LOW_ADDR(__secondary_hold_acknowledge);
#ifdef CONFIG_PPC64
	/* __secondary_hold is actually a descriptor, not the text address */
	unsigned long secondary_hold
		= __pa(*PTRRELOC((unsigned long *)__secondary_hold));
#else
	unsigned long secondary_hold = LOW_ADDR(__secondary_hold);
#endif

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
		type[0] = 0;
		prom_getprop(node, "device_type", type, sizeof(type));
		if (strcmp(type, RELOC("cpu")) != 0)
			continue;

		/* Skip non-configured cpus. */
		if (prom_getprop(node, "status", type, sizeof(type)) > 0)
			if (strcmp(type, RELOC("okay")) != 0)
				continue;

		reg = -1;
		prom_getprop(node, "reg", &reg, sizeof(reg));

		prom_debug("\ncpuid        = 0x%x\n", cpuid);
		prom_debug("cpu hw idx   = 0x%x\n", reg);

		/* Init the acknowledge var which will be reset by
		 * the secondary cpu when it awakens from its OF
		 * spinloop.
		 */
		*acknowledge = (unsigned long)-1;

		propsize = prom_getprop(node, "ibm,ppc-interrupt-server#s",
					&interrupt_server,
					sizeof(interrupt_server));
		if (propsize < 0) {
			/* no property.  old hardware has no SMT */
			cpu_threads = 1;
			interrupt_server[0] = reg; /* fake it with phys id */
		} else {
			/* We have a threaded processor */
			cpu_threads = propsize / sizeof(u32);
			if (cpu_threads > MAX_CPU_THREADS) {
				prom_printf("SMT: too many threads!\n"
					    "SMT: found %x, max is %x\n",
					    cpu_threads, MAX_CPU_THREADS);
				cpu_threads = 1; /* ToDo: panic? */
			}
		}

		hw_cpu_num = interrupt_server[0];
		if (hw_cpu_num != _prom->cpu) {
			/* Primary Thread of non-boot cpu */
			prom_printf("%x : starting cpu hw idx %x... ", cpuid, reg);
			call_prom("start-cpu", 3, 0, node,
				  secondary_hold, reg);

			for (i = 0; (i < 100000000) && 
			     (*acknowledge == ((unsigned long)-1)); i++ )
				mb();

			if (*acknowledge == reg)
				prom_printf("done\n");
			else
				prom_printf("failed: %x\n", *acknowledge);
		}
#ifdef CONFIG_SMP
		else
			prom_printf("%x : boot cpu     %x\n", cpuid, reg);
#endif /* CONFIG_SMP */

		/* Reserve cpu #s for secondary threads.   They start later. */
		cpuid += cpu_threads;
	}

	if (cpuid > NR_CPUS)
		prom_printf("WARNING: maximum CPUs (" __stringify(NR_CPUS)
			    ") exceeded: ignoring extras\n");

	prom_debug("prom_hold_cpus: end...\n");
}


static void __init prom_init_client_services(unsigned long pp)
{
	struct prom_t *_prom = &RELOC(prom);

	/* Get a handle to the prom entry point before anything else */
	RELOC(prom_entry) = pp;

	/* get a handle for the stdout device */
	_prom->chosen = call_prom("finddevice", 1, 1, ADDR("/chosen"));
	if (!PHANDLE_VALID(_prom->chosen))
		prom_panic("cannot find chosen"); /* msg won't be printed :( */

	/* get device tree root */
	_prom->root = call_prom("finddevice", 1, 1, ADDR("/"));
	if (!PHANDLE_VALID(_prom->root))
		prom_panic("cannot find device tree root"); /* msg won't be printed :( */

	_prom->mmumap = 0;
}

#ifdef CONFIG_PPC32
/*
 * For really old powermacs, we need to map things we claim.
 * For that, we need the ihandle of the mmu.
 * Also, on the longtrail, we need to work around other bugs.
 */
static void __init prom_find_mmu(void)
{
	struct prom_t *_prom = &RELOC(prom);
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
	_prom->memory = call_prom("open", 1, 1, ADDR("/memory"));
	prom_getprop(_prom->chosen, "mmu", &_prom->mmumap,
		     sizeof(_prom->mmumap));
	if (!IHANDLE_VALID(_prom->memory) || !IHANDLE_VALID(_prom->mmumap))
		of_workarounds &= ~OF_WA_CLAIM;		/* hmmm */
}
#else
#define prom_find_mmu()
#endif

static void __init prom_init_stdout(void)
{
	struct prom_t *_prom = &RELOC(prom);
	char *path = RELOC(of_stdout_device);
	char type[16];
	u32 val;

	if (prom_getprop(_prom->chosen, "stdout", &val, sizeof(val)) <= 0)
		prom_panic("cannot find stdout");

	_prom->stdout = val;

	/* Get the full OF pathname of the stdout device */
	memset(path, 0, 256);
	call_prom("instance-to-path", 3, 1, _prom->stdout, path, 255);
	val = call_prom("instance-to-package", 1, 1, _prom->stdout);
	prom_setprop(_prom->chosen, "/chosen", "linux,stdout-package",
		     &val, sizeof(val));
	prom_printf("OF stdout device is: %s\n", RELOC(of_stdout_device));
	prom_setprop(_prom->chosen, "/chosen", "linux,stdout-path",
		     path, strlen(path) + 1);

	/* If it's a display, note it */
	memset(type, 0, sizeof(type));
	prom_getprop(val, "device_type", type, sizeof(type));
	if (strcmp(type, RELOC("display")) == 0)
		prom_setprop(val, path, "linux,boot-display", NULL, 0);
}

static void __init prom_close_stdin(void)
{
	struct prom_t *_prom = &RELOC(prom);
	ihandle val;

	if (prom_getprop(_prom->chosen, "stdin", &val, sizeof(val)) > 0)
		call_prom("close", 1, 0, val);
}

static int __init prom_find_machine_type(void)
{
	struct prom_t *_prom = &RELOC(prom);
	char compat[256];
	int len, i = 0;
#ifdef CONFIG_PPC64
	phandle rtas;
	int x;
#endif

	/* Look for a PowerMac */
	len = prom_getprop(_prom->root, "compatible",
			   compat, sizeof(compat)-1);
	if (len > 0) {
		compat[len] = 0;
		while (i < len) {
			char *p = &compat[i];
			int sl = strlen(p);
			if (sl == 0)
				break;
			if (strstr(p, RELOC("Power Macintosh")) ||
			    strstr(p, RELOC("MacRISC")))
				return PLATFORM_POWERMAC;
			i += sl + 1;
		}
	}
#ifdef CONFIG_PPC64
	/* If not a mac, try to figure out if it's an IBM pSeries or any other
	 * PAPR compliant platform. We assume it is if :
	 *  - /device_type is "chrp" (please, do NOT use that for future
	 *    non-IBM designs !
	 *  - it has /rtas
	 */
	len = prom_getprop(_prom->root, "device_type",
			   compat, sizeof(compat)-1);
	if (len <= 0)
		return PLATFORM_GENERIC;
	if (strcmp(compat, RELOC("chrp")))
		return PLATFORM_GENERIC;

	/* Default to pSeries. We need to know if we are running LPAR */
	rtas = call_prom("finddevice", 1, 1, ADDR("/rtas"));
	if (!PHANDLE_VALID(rtas))
		return PLATFORM_GENERIC;
	x = prom_getproplen(rtas, "ibm,hypertas-functions");
	if (x != PROM_ERROR) {
		prom_printf("Hypertas detected, assuming LPAR !\n");
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

	prom_printf("Looking for displays\n");
	for (node = 0; prom_next_node(&node); ) {
		memset(type, 0, sizeof(type));
		prom_getprop(node, "device_type", type, sizeof(type));
		if (strcmp(type, RELOC("display")) != 0)
			continue;

		/* It seems OF doesn't null-terminate the path :-( */
		path = RELOC(prom_scratch);
		memset(path, 0, PROM_SCRATCH_SIZE);

		/*
		 * leave some room at the end of the path for appending extra
		 * arguments
		 */
		if (call_prom("package-to-path", 3, 1, node, path,
			      PROM_SCRATCH_SIZE-10) == PROM_ERROR)
			continue;
		prom_printf("found display   : %s, opening ... ", path);
		
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
		clut = RELOC(default_colors);
		for (i = 0; i < 32; i++, clut += 3)
			if (prom_set_color(ih, i, clut[0], clut[1],
					   clut[2]) != 0)
				break;

#ifdef CONFIG_LOGO_LINUX_CLUT224
		clut = PTRRELOC(RELOC(logo_linux_clut224.clut));
		for (i = 0; i < RELOC(logo_linux_clut224.clutsize); i++, clut += 3)
			if (prom_set_color(ih, i + 32, clut[0], clut[1],
					   clut[2]) != 0)
				break;
#endif /* CONFIG_LOGO_LINUX_CLUT224 */
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
			   RELOC(alloc_bottom));
		room = RELOC(alloc_top) - RELOC(alloc_bottom);
		if (room > DEVTREE_CHUNK_SIZE)
			room = DEVTREE_CHUNK_SIZE;
		if (room < PAGE_SIZE)
			prom_panic("No memory for flatten_device_tree (no room)");
		chunk = alloc_up(room, 0);
		if (chunk == 0)
			prom_panic("No memory for flatten_device_tree (claim failed)");
		*mem_end = RELOC(alloc_top);
	}

	ret = (void *)*mem_start;
	*mem_start += needed;

	return ret;
}

#define dt_push_token(token, mem_start, mem_end) \
	do { *((u32 *)make_room(mem_start, mem_end, 4, 4)) = token; } while(0)

static unsigned long __init dt_find_string(char *str)
{
	char *s, *os;

	s = os = (char *)RELOC(dt_string_start);
	s += 4;
	while (s <  (char *)RELOC(dt_string_end)) {
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

	sstart =  (char *)RELOC(dt_string_start);

	/* get and store all property names */
	prev_name = RELOC("");
	for (;;) {
		/* 64 is max len of name including nul. */
		namep = make_room(mem_start, mem_end, MAX_PROPERTY_NAME, 1);
		if (call_prom("nextprop", 3, 1, node, prev_name, namep) != 1) {
			/* No more nodes: unwind alloc */
			*mem_start = (unsigned long)namep;
			break;
		}

 		/* skip "name" */
 		if (strcmp(namep, RELOC("name")) == 0) {
 			*mem_start = (unsigned long)namep;
 			prev_name = RELOC("name");
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
			RELOC(dt_string_end) = *mem_start;
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
	int l, room;

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
	path = RELOC(prom_scratch);
	memset(path, 0, PROM_SCRATCH_SIZE);
	call_prom("package-to-path", 3, 1, node, path, PROM_SCRATCH_SIZE-1);

	/* get and store all properties */
	prev_name = RELOC("");
	sstart = (char *)RELOC(dt_string_start);
	for (;;) {
		if (call_prom("nextprop", 3, 1, node, prev_name,
			      RELOC(pname)) != 1)
			break;

 		/* skip "name" */
 		if (strcmp(RELOC(pname), RELOC("name")) == 0) {
 			prev_name = RELOC("name");
 			continue;
 		}

		/* find string offset */
		soff = dt_find_string(RELOC(pname));
		if (soff == 0) {
			prom_printf("WARNING: Can't find string index for"
				    " <%s>, node %s\n", RELOC(pname), path);
			break;
		}
		prev_name = sstart + soff;

		/* get length */
		l = call_prom("getproplen", 2, 1, node, RELOC(pname));

		/* sanity checks */
		if (l == PROM_ERROR)
			continue;
		if (l > MAX_PROPERTY_LENGTH) {
			prom_printf("WARNING: ignoring large property ");
			/* It seems OF doesn't null-terminate the path :-( */
			prom_printf("[%s] ", path);
			prom_printf("%s length 0x%x\n", RELOC(pname), l);
			continue;
		}

		/* push property head */
		dt_push_token(OF_DT_PROP, mem_start, mem_end);
		dt_push_token(l, mem_start, mem_end);
		dt_push_token(soff, mem_start, mem_end);

		/* push property content */
		valp = make_room(mem_start, mem_end, l, 4);
		call_prom("getprop", 4, 1, node, RELOC(pname), valp, l);
		*mem_start = _ALIGN(*mem_start, 4);
	}

	/* Add a "linux,phandle" property. */
	soff = dt_find_string(RELOC("linux,phandle"));
	if (soff == 0)
		prom_printf("WARNING: Can't find string index for"
			    " <linux-phandle> node %s\n", path);
	else {
		dt_push_token(OF_DT_PROP, mem_start, mem_end);
		dt_push_token(4, mem_start, mem_end);
		dt_push_token(soff, mem_start, mem_end);
		valp = make_room(mem_start, mem_end, 4, 4);
		*(u32 *)valp = node;
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
	struct prom_t *_prom = &RELOC(prom);
	char *namep;
	u64 *rsvmap;

	/*
	 * Check how much room we have between alloc top & bottom (+/- a
	 * few pages), crop to 4Mb, as this is our "chuck" size
	 */
	room = RELOC(alloc_top) - RELOC(alloc_bottom) - 0x4000;
	if (room > DEVTREE_CHUNK_SIZE)
		room = DEVTREE_CHUNK_SIZE;
	prom_debug("starting device tree allocs at %x\n", RELOC(alloc_bottom));

	/* Now try to claim that */
	mem_start = (unsigned long)alloc_up(room, PAGE_SIZE);
	if (mem_start == 0)
		prom_panic("Can't allocate initial device-tree chunk\n");
	mem_end = RELOC(alloc_top);

	/* Get root of tree */
	root = call_prom("peer", 1, 1, (phandle)0);
	if (root == (phandle)0)
		prom_panic ("couldn't get device tree root\n");

	/* Build header and make room for mem rsv map */ 
	mem_start = _ALIGN(mem_start, 4);
	hdr = make_room(&mem_start, &mem_end,
			sizeof(struct boot_param_header), 4);
	RELOC(dt_header_start) = (unsigned long)hdr;
	rsvmap = make_room(&mem_start, &mem_end, sizeof(mem_reserve_map), 8);

	/* Start of strings */
	mem_start = PAGE_ALIGN(mem_start);
	RELOC(dt_string_start) = mem_start;
	mem_start += 4; /* hole */

	/* Add "linux,phandle" in there, we'll need it */
	namep = make_room(&mem_start, &mem_end, 16, 1);
	strcpy(namep, RELOC("linux,phandle"));
	mem_start = (unsigned long)namep + strlen(namep) + 1;

	/* Build string array */
	prom_printf("Building dt strings...\n"); 
	scan_dt_build_strings(root, &mem_start, &mem_end);
	RELOC(dt_string_end) = mem_start;

	/* Build structure */
	mem_start = PAGE_ALIGN(mem_start);
	RELOC(dt_struct_start) = mem_start;
	prom_printf("Building dt structure...\n"); 
	scan_dt_build_struct(root, &mem_start, &mem_end);
	dt_push_token(OF_DT_END, &mem_start, &mem_end);
	RELOC(dt_struct_end) = PAGE_ALIGN(mem_start);

	/* Finish header */
	hdr->boot_cpuid_phys = _prom->cpu;
	hdr->magic = OF_DT_HEADER;
	hdr->totalsize = RELOC(dt_struct_end) - RELOC(dt_header_start);
	hdr->off_dt_struct = RELOC(dt_struct_start) - RELOC(dt_header_start);
	hdr->off_dt_strings = RELOC(dt_string_start) - RELOC(dt_header_start);
	hdr->dt_strings_size = RELOC(dt_string_end) - RELOC(dt_string_start);
	hdr->off_mem_rsvmap = ((unsigned long)rsvmap) - RELOC(dt_header_start);
	hdr->version = OF_DT_VERSION;
	/* Version 16 is not backward compatible */
	hdr->last_comp_version = 0x10;

	/* Copy the reserve map in */
	memcpy(rsvmap, RELOC(mem_reserve_map), sizeof(mem_reserve_map));

#ifdef DEBUG_PROM
	{
		int i;
		prom_printf("reserved memory map:\n");
		for (i = 0; i < RELOC(mem_reserve_cnt); i++)
			prom_printf("  %x - %x\n",
				    RELOC(mem_reserve_map)[i].base,
				    RELOC(mem_reserve_map)[i].size);
	}
#endif
	/* Bump mem_reserve_cnt to cause further reservations to fail
	 * since it's too late.
	 */
	RELOC(mem_reserve_cnt) = MEM_RESERVE_MAP_SIZE;

	prom_printf("Device tree strings 0x%x -> 0x%x\n",
		    RELOC(dt_string_start), RELOC(dt_string_end)); 
	prom_printf("Device tree struct  0x%x -> 0x%x\n",
		    RELOC(dt_struct_start), RELOC(dt_struct_end));

}


static void __init fixup_device_tree(void)
{
#if defined(CONFIG_PPC64) && defined(CONFIG_PPC_PMAC)
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
#endif
}


static void __init prom_find_boot_cpu(void)
{
       	struct prom_t *_prom = &RELOC(prom);
	u32 getprop_rval;
	ihandle prom_cpu;
	phandle cpu_pkg;

	_prom->cpu = 0;
	if (prom_getprop(_prom->chosen, "cpu", &prom_cpu, sizeof(prom_cpu)) <= 0)
		return;

	cpu_pkg = call_prom("instance-to-package", 1, 1, prom_cpu);

	prom_getprop(cpu_pkg, "reg", &getprop_rval, sizeof(getprop_rval));
	_prom->cpu = getprop_rval;

	prom_debug("Booting CPU hw index = 0x%x\n", _prom->cpu);
}

static void __init prom_check_initrd(unsigned long r3, unsigned long r4)
{
#ifdef CONFIG_BLK_DEV_INITRD
       	struct prom_t *_prom = &RELOC(prom);

	if (r3 && r4 && r4 != 0xdeadbeef) {
		unsigned long val;

		RELOC(prom_initrd_start) = is_kernel_addr(r3) ? __pa(r3) : r3;
		RELOC(prom_initrd_end) = RELOC(prom_initrd_start) + r4;

		val = RELOC(prom_initrd_start);
		prom_setprop(_prom->chosen, "/chosen", "linux,initrd-start",
			     &val, sizeof(val));
		val = RELOC(prom_initrd_end);
		prom_setprop(_prom->chosen, "/chosen", "linux,initrd-end",
			     &val, sizeof(val));

		reserve_mem(RELOC(prom_initrd_start),
			    RELOC(prom_initrd_end) - RELOC(prom_initrd_start));

		prom_debug("initrd_start=0x%x\n", RELOC(prom_initrd_start));
		prom_debug("initrd_end=0x%x\n", RELOC(prom_initrd_end));
	}
#endif /* CONFIG_BLK_DEV_INITRD */
}

/*
 * We enter here early on, when the Open Firmware prom is still
 * handling exceptions and the MMU hash table for us.
 */

unsigned long __init prom_init(unsigned long r3, unsigned long r4,
			       unsigned long pp,
			       unsigned long r6, unsigned long r7)
{	
       	struct prom_t *_prom;
	unsigned long hdr;
	unsigned long offset = reloc_offset();

#ifdef CONFIG_PPC32
	reloc_got2(offset);
#endif

	_prom = &RELOC(prom);

	/*
	 * First zero the BSS
	 */
	memset(&RELOC(__bss_start), 0, __bss_stop - __bss_start);

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

	/*
	 * Get default machine type. At this point, we do not differentiate
	 * between pSeries SMP and pSeries LPAR
	 */
	RELOC(of_platform) = prom_find_machine_type();

	/* Bail if this is a kdump kernel. */
	if (PHYSICAL_START > 0)
		prom_panic("Error: You can't boot a kdump kernel from OF!\n");

	/*
	 * Check for an initrd
	 */
	prom_check_initrd(r3, r4);

#ifdef CONFIG_PPC_PSERIES
	/*
	 * On pSeries, inform the firmware about our capabilities
	 */
	if (RELOC(of_platform) == PLATFORM_PSERIES ||
	    RELOC(of_platform) == PLATFORM_PSERIES_LPAR)
		prom_send_capabilities();
#endif

	/*
	 * Copy the CPU hold code
	 */
       	if (RELOC(of_platform) != PLATFORM_POWERMAC)
       		copy_and_flush(0, KERNELBASE + offset, 0x100, 0);

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

#ifdef CONFIG_PPC64
	/*
	 * Initialize IOMMU (TCE tables) on pSeries. Do that before anything else
	 * that uses the allocator, we need to make sure we get the top of memory
	 * available for us here...
	 */
	if (RELOC(of_platform) == PLATFORM_PSERIES)
		prom_initialize_tce_table();
#endif

	/*
	 * On non-powermacs, try to instantiate RTAS and puts all CPUs
	 * in spin-loops. PowerMacs don't have a working RTAS and use
	 * a different way to spin CPUs
	 */
	if (RELOC(of_platform) != PLATFORM_POWERMAC) {
		prom_instantiate_rtas();
		prom_hold_cpus();
	}

	/*
	 * Fill in some infos for use by the kernel later on
	 */
#ifdef CONFIG_PPC64
	if (RELOC(ppc64_iommu_off))
		prom_setprop(_prom->chosen, "/chosen", "linux,iommu-off",
			     NULL, 0);

	if (RELOC(iommu_force_on))
		prom_setprop(_prom->chosen, "/chosen", "linux,iommu-force-on",
			     NULL, 0);

	if (RELOC(prom_tce_alloc_start)) {
		prom_setprop(_prom->chosen, "/chosen", "linux,tce-alloc-start",
			     &RELOC(prom_tce_alloc_start),
			     sizeof(prom_tce_alloc_start));
		prom_setprop(_prom->chosen, "/chosen", "linux,tce-alloc-end",
			     &RELOC(prom_tce_alloc_end),
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
	prom_printf("copying OF device tree ...\n");
	flatten_device_tree();

	/*
	 * in case stdin is USB and still active on IBM machines...
	 * Unfortunately quiesce crashes on some powermacs if we have
	 * closed stdin already (in particular the powerbook 101).
	 */
	if (RELOC(of_platform) != PLATFORM_POWERMAC)
		prom_close_stdin();

	/*
	 * Call OF "quiesce" method to shut down pending DMA's from
	 * devices etc...
	 */
	prom_printf("Calling quiesce ...\n");
	call_prom("quiesce", 0, 0);

	/*
	 * And finally, call the kernel passing it the flattened device
	 * tree and NULL as r5, thus triggering the new entry point which
	 * is common to us and kexec
	 */
	hdr = RELOC(dt_header_start);
	prom_printf("returning from prom_init\n");
	prom_debug("->dt_header_start=0x%x\n", hdr);

#ifdef CONFIG_PPC32
	reloc_got2(-offset);
#endif

	__start(hdr, KERNELBASE + offset, 0);

	return 0;
}

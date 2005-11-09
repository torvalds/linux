/*
 * Copyright (C) 2004, 2005 MIPS Technologies, Inc.  All rights reserved.
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 */

/*
 * VPE support module
 *
 * Provides support for loading a MIPS SP program on VPE1.
 * The SP enviroment is rather simple, no tlb's.  It needs to be relocatable
 * (or partially linked). You should initialise your stack in the startup
 * code. This loader looks for the symbol __start and sets up
 * execution to resume from there. The MIPS SDE kit contains suitable examples.
 *
 * To load and run, simply cat a SP 'program file' to /dev/vpe1.
 * i.e cat spapp >/dev/vpe1.
 *
 * You'll need to have the following device files.
 * mknod /dev/vpe0 c 63 0
 * mknod /dev/vpe1 c 63 1
 */
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/vmalloc.h>
#include <linux/elf.h>
#include <linux/seq_file.h>
#include <linux/syscalls.h>
#include <linux/moduleloader.h>
#include <linux/interrupt.h>
#include <linux/poll.h>
#include <linux/bootmem.h>
#include <asm/mipsregs.h>
#include <asm/mipsmtregs.h>
#include <asm/cacheflush.h>
#include <asm/atomic.h>
#include <asm/cpu.h>
#include <asm/processor.h>
#include <asm/system.h>

typedef void *vpe_handle;

#ifndef ARCH_SHF_SMALL
#define ARCH_SHF_SMALL 0
#endif

/* If this is set, the section belongs in the init part of the module */
#define INIT_OFFSET_MASK (1UL << (BITS_PER_LONG-1))

static char module_name[] = "vpe";
static int major;

/* grab the likely amount of memory we will need. */
#ifdef CONFIG_MIPS_VPE_LOADER_TOM
#define P_SIZE (2 * 1024 * 1024)
#else
/* add an overhead to the max kmalloc size for non-striped symbols/etc */
#define P_SIZE (256 * 1024)
#endif

#define MAX_VPES 16

enum vpe_state {
	VPE_STATE_UNUSED = 0,
	VPE_STATE_INUSE,
	VPE_STATE_RUNNING
};

enum tc_state {
	TC_STATE_UNUSED = 0,
	TC_STATE_INUSE,
	TC_STATE_RUNNING,
	TC_STATE_DYNAMIC
};

struct vpe {
	enum vpe_state state;

	/* (device) minor associated with this vpe */
	int minor;

	/* elfloader stuff */
	void *load_addr;
	u32 len;
	char *pbuffer;
	u32 plen;

	unsigned long __start;

	/* tc's associated with this vpe */
	struct list_head tc;

	/* The list of vpe's */
	struct list_head list;

	/* shared symbol address */
	void *shared_ptr;
};

struct tc {
	enum tc_state state;
	int index;

	/* parent VPE */
	struct vpe *pvpe;

	/* The list of TC's with this VPE */
	struct list_head tc;

	/* The global list of tc's */
	struct list_head list;
};

struct vpecontrol_ {
	/* Virtual processing elements */
	struct list_head vpe_list;

	/* Thread contexts */
	struct list_head tc_list;
} vpecontrol;

static void release_progmem(void *ptr);
static void dump_vpe(struct vpe * v);
extern void save_gp_address(unsigned int secbase, unsigned int rel);

/* get the vpe associated with this minor */
struct vpe *get_vpe(int minor)
{
	struct vpe *v;

	list_for_each_entry(v, &vpecontrol.vpe_list, list) {
		if (v->minor == minor)
			return v;
	}

	printk(KERN_DEBUG "VPE: get_vpe minor %d not found\n", minor);
	return NULL;
}

/* get the vpe associated with this minor */
struct tc *get_tc(int index)
{
	struct tc *t;

	list_for_each_entry(t, &vpecontrol.tc_list, list) {
		if (t->index == index)
			return t;
	}

	printk(KERN_DEBUG "VPE: get_tc index %d not found\n", index);

	return NULL;
}

struct tc *get_tc_unused(void)
{
	struct tc *t;

	list_for_each_entry(t, &vpecontrol.tc_list, list) {
		if (t->state == TC_STATE_UNUSED)
			return t;
	}

	printk(KERN_DEBUG "VPE: All TC's are in use\n");

	return NULL;
}

/* allocate a vpe and associate it with this minor (or index) */
struct vpe *alloc_vpe(int minor)
{
	struct vpe *v;

	if ((v = kzalloc(sizeof(struct vpe), GFP_KERNEL)) == NULL) {
		printk(KERN_WARNING "VPE: alloc_vpe no mem\n");
		return NULL;
	}

	INIT_LIST_HEAD(&v->tc);
	list_add_tail(&v->list, &vpecontrol.vpe_list);

	v->minor = minor;
	return v;
}

/* allocate a tc. At startup only tc0 is running, all other can be halted. */
struct tc *alloc_tc(int index)
{
	struct tc *t;

	if ((t = kzalloc(sizeof(struct tc), GFP_KERNEL)) == NULL) {
		printk(KERN_WARNING "VPE: alloc_tc no mem\n");
		return NULL;
	}

	INIT_LIST_HEAD(&t->tc);
	list_add_tail(&t->list, &vpecontrol.tc_list);

	t->index = index;

	return t;
}

/* clean up and free everything */
void release_vpe(struct vpe *v)
{
	list_del(&v->list);
	if (v->load_addr)
		release_progmem(v);
	kfree(v);
}

void dump_mtregs(void)
{
	unsigned long val;

	val = read_c0_config3();
	printk("config3 0x%lx MT %ld\n", val,
	       (val & CONFIG3_MT) >> CONFIG3_MT_SHIFT);

	val = read_c0_mvpconf0();
	printk("mvpconf0 0x%lx, PVPE %ld PTC %ld M %ld\n", val,
	       (val & MVPCONF0_PVPE) >> MVPCONF0_PVPE_SHIFT,
	       val & MVPCONF0_PTC, (val & MVPCONF0_M) >> MVPCONF0_M_SHIFT);

	val = read_c0_mvpcontrol();
	printk("MVPControl 0x%lx, STLB %ld VPC %ld EVP %ld\n", val,
	       (val & MVPCONTROL_STLB) >> MVPCONTROL_STLB_SHIFT,
	       (val & MVPCONTROL_VPC) >> MVPCONTROL_VPC_SHIFT,
	       (val & MVPCONTROL_EVP));

	val = read_c0_vpeconf0();
	printk("VPEConf0 0x%lx MVP %ld\n", val,
	       (val & VPECONF0_MVP) >> VPECONF0_MVP_SHIFT);
}

/* Find some VPE program space  */
static void *alloc_progmem(u32 len)
{
#ifdef CONFIG_MIPS_VPE_LOADER_TOM
	/* this means you must tell linux to use less memory than you physically have */
	return (void *)((max_pfn * PAGE_SIZE) + KSEG0);
#else
	// simple grab some mem for now
	return kmalloc(len, GFP_KERNEL);
#endif
}

static void release_progmem(void *ptr)
{
#ifndef CONFIG_MIPS_VPE_LOADER_TOM
	kfree(ptr);
#endif
}

/* Update size with this section: return offset. */
static long get_offset(unsigned long *size, Elf_Shdr * sechdr)
{
	long ret;

	ret = ALIGN(*size, sechdr->sh_addralign ? : 1);
	*size = ret + sechdr->sh_size;
	return ret;
}

/* Lay out the SHF_ALLOC sections in a way not dissimilar to how ld
   might -- code, read-only data, read-write data, small data.  Tally
   sizes, and place the offsets into sh_entsize fields: high bit means it
   belongs in init. */
static void layout_sections(struct module *mod, const Elf_Ehdr * hdr,
			    Elf_Shdr * sechdrs, const char *secstrings)
{
	static unsigned long const masks[][2] = {
		/* NOTE: all executable code must be the first section
		 * in this array; otherwise modify the text_size
		 * finder in the two loops below */
		{SHF_EXECINSTR | SHF_ALLOC, ARCH_SHF_SMALL},
		{SHF_ALLOC, SHF_WRITE | ARCH_SHF_SMALL},
		{SHF_WRITE | SHF_ALLOC, ARCH_SHF_SMALL},
		{ARCH_SHF_SMALL | SHF_ALLOC, 0}
	};
	unsigned int m, i;

	for (i = 0; i < hdr->e_shnum; i++)
		sechdrs[i].sh_entsize = ~0UL;

	for (m = 0; m < ARRAY_SIZE(masks); ++m) {
		for (i = 0; i < hdr->e_shnum; ++i) {
			Elf_Shdr *s = &sechdrs[i];

			//  || strncmp(secstrings + s->sh_name, ".init", 5) == 0)
			if ((s->sh_flags & masks[m][0]) != masks[m][0]
			    || (s->sh_flags & masks[m][1])
			    || s->sh_entsize != ~0UL)
				continue;
			s->sh_entsize = get_offset(&mod->core_size, s);
		}

		if (m == 0)
			mod->core_text_size = mod->core_size;

	}
}


/* from module-elf32.c, but subverted a little */

struct mips_hi16 {
	struct mips_hi16 *next;
	Elf32_Addr *addr;
	Elf32_Addr value;
};

static struct mips_hi16 *mips_hi16_list;
static unsigned int gp_offs, gp_addr;

static int apply_r_mips_none(struct module *me, uint32_t *location,
			     Elf32_Addr v)
{
	return 0;
}

static int apply_r_mips_gprel16(struct module *me, uint32_t *location,
				Elf32_Addr v)
{
	int rel;

	if( !(*location & 0xffff) ) {
		rel = (int)v - gp_addr;
	}
	else {
		/* .sbss + gp(relative) + offset */
		/* kludge! */
		rel =  (int)(short)((int)v + gp_offs +
				    (int)(short)(*location & 0xffff) - gp_addr);
	}

	if( (rel > 32768) || (rel < -32768) ) {
		printk(KERN_ERR
		       "apply_r_mips_gprel16: relative address out of range 0x%x %d\n",
		       rel, rel);
		return -ENOEXEC;
	}

	*location = (*location & 0xffff0000) | (rel & 0xffff);

	return 0;
}

static int apply_r_mips_pc16(struct module *me, uint32_t *location,
			     Elf32_Addr v)
{
	int rel;
	rel = (((unsigned int)v - (unsigned int)location));
	rel >>= 2;		// because the offset is in _instructions_ not bytes.
	rel -= 1;		// and one instruction less due to the branch delay slot.

	if( (rel > 32768) || (rel < -32768) ) {
		printk(KERN_ERR
		       "apply_r_mips_pc16: relative address out of range 0x%x\n", rel);
		return -ENOEXEC;
	}

	*location = (*location & 0xffff0000) | (rel & 0xffff);

	return 0;
}

static int apply_r_mips_32(struct module *me, uint32_t *location,
			   Elf32_Addr v)
{
	*location += v;

	return 0;
}

static int apply_r_mips_26(struct module *me, uint32_t *location,
			   Elf32_Addr v)
{
	if (v % 4) {
		printk(KERN_ERR "module %s: dangerous relocation mod4\n", me->name);
		return -ENOEXEC;
	}

/*
 * Not desperately convinced this is a good check of an overflow condition
 * anyway. But it gets in the way of handling undefined weak symbols which
 * we want to set to zero.
 * if ((v & 0xf0000000) != (((unsigned long)location + 4) & 0xf0000000)) {
 * printk(KERN_ERR
 * "module %s: relocation overflow\n",
 * me->name);
 * return -ENOEXEC;
 * }
 */

	*location = (*location & ~0x03ffffff) |
		((*location + (v >> 2)) & 0x03ffffff);
	return 0;
}

static int apply_r_mips_hi16(struct module *me, uint32_t *location,
			     Elf32_Addr v)
{
	struct mips_hi16 *n;

	/*
	 * We cannot relocate this one now because we don't know the value of
	 * the carry we need to add.  Save the information, and let LO16 do the
	 * actual relocation.
	 */
	n = kmalloc(sizeof *n, GFP_KERNEL);
	if (!n)
		return -ENOMEM;

	n->addr = location;
	n->value = v;
	n->next = mips_hi16_list;
	mips_hi16_list = n;

	return 0;
}

static int apply_r_mips_lo16(struct module *me, uint32_t *location,
			     Elf32_Addr v)
{
	unsigned long insnlo = *location;
	Elf32_Addr val, vallo;

	/* Sign extend the addend we extract from the lo insn.  */
	vallo = ((insnlo & 0xffff) ^ 0x8000) - 0x8000;

	if (mips_hi16_list != NULL) {
		struct mips_hi16 *l;

		l = mips_hi16_list;
		while (l != NULL) {
			struct mips_hi16 *next;
			unsigned long insn;

			/*
			 * The value for the HI16 had best be the same.
			 */
			if (v != l->value) {
				printk("%d != %d\n", v, l->value);
				goto out_danger;
			}


			/*
			 * Do the HI16 relocation.  Note that we actually don't
			 * need to know anything about the LO16 itself, except
			 * where to find the low 16 bits of the addend needed
			 * by the LO16.
			 */
			insn = *l->addr;
			val = ((insn & 0xffff) << 16) + vallo;
			val += v;

			/*
			 * Account for the sign extension that will happen in
			 * the low bits.
			 */
			val = ((val >> 16) + ((val & 0x8000) != 0)) & 0xffff;

			insn = (insn & ~0xffff) | val;
			*l->addr = insn;

			next = l->next;
			kfree(l);
			l = next;
		}

		mips_hi16_list = NULL;
	}

	/*
	 * Ok, we're done with the HI16 relocs.  Now deal with the LO16.
	 */
	val = v + vallo;
	insnlo = (insnlo & ~0xffff) | (val & 0xffff);
	*location = insnlo;

	return 0;

out_danger:
	printk(KERN_ERR "module %s: dangerous " "relocation\n", me->name);

	return -ENOEXEC;
}

static int (*reloc_handlers[]) (struct module *me, uint32_t *location,
				Elf32_Addr v) = {
	[R_MIPS_NONE]	= apply_r_mips_none,
	[R_MIPS_32]	= apply_r_mips_32,
	[R_MIPS_26]	= apply_r_mips_26,
	[R_MIPS_HI16]	= apply_r_mips_hi16,
	[R_MIPS_LO16]	= apply_r_mips_lo16,
	[R_MIPS_GPREL16] = apply_r_mips_gprel16,
	[R_MIPS_PC16] = apply_r_mips_pc16
};


int apply_relocations(Elf32_Shdr *sechdrs,
		      const char *strtab,
		      unsigned int symindex,
		      unsigned int relsec,
		      struct module *me)
{
	Elf32_Rel *rel = (void *) sechdrs[relsec].sh_addr;
	Elf32_Sym *sym;
	uint32_t *location;
	unsigned int i;
	Elf32_Addr v;
	int res;

	for (i = 0; i < sechdrs[relsec].sh_size / sizeof(*rel); i++) {
		Elf32_Word r_info = rel[i].r_info;

		/* This is where to make the change */
		location = (void *)sechdrs[sechdrs[relsec].sh_info].sh_addr
			+ rel[i].r_offset;
		/* This is the symbol it is referring to */
		sym = (Elf32_Sym *)sechdrs[symindex].sh_addr
			+ ELF32_R_SYM(r_info);

		if (!sym->st_value) {
			printk(KERN_DEBUG "%s: undefined weak symbol %s\n",
			       me->name, strtab + sym->st_name);
			/* just print the warning, dont barf */
		}

		v = sym->st_value;

		res = reloc_handlers[ELF32_R_TYPE(r_info)](me, location, v);
		if( res ) {
			printk(KERN_DEBUG
			       "relocation error 0x%x sym refer <%s> value 0x%x "
			       "type 0x%x r_info 0x%x\n",
			       (unsigned int)location, strtab + sym->st_name, v,
			       r_info, ELF32_R_TYPE(r_info));
		}

		if (res)
			return res;
	}

	return 0;
}

void save_gp_address(unsigned int secbase, unsigned int rel)
{
	gp_addr = secbase + rel;
	gp_offs = gp_addr - (secbase & 0xffff0000);
}
/* end module-elf32.c */



/* Change all symbols so that sh_value encodes the pointer directly. */
static int simplify_symbols(Elf_Shdr * sechdrs,
			    unsigned int symindex,
			    const char *strtab,
			    const char *secstrings,
			    unsigned int nsecs, struct module *mod)
{
	Elf_Sym *sym = (void *)sechdrs[symindex].sh_addr;
	unsigned long secbase, bssbase = 0;
	unsigned int i, n = sechdrs[symindex].sh_size / sizeof(Elf_Sym);
	int ret = 0, size;

	/* find the .bss section for COMMON symbols */
	for (i = 0; i < nsecs; i++) {
		if (strncmp(secstrings + sechdrs[i].sh_name, ".bss", 4) == 0)
			bssbase = sechdrs[i].sh_addr;
	}

	for (i = 1; i < n; i++) {
		switch (sym[i].st_shndx) {
		case SHN_COMMON:
			/* Allocate space for the symbol in the .bss section. st_value is currently size.
			   We want it to have the address of the symbol. */

			size = sym[i].st_value;
			sym[i].st_value = bssbase;

			bssbase += size;
			break;

		case SHN_ABS:
			/* Don't need to do anything */
			break;

		case SHN_UNDEF:
			/* ret = -ENOENT; */
			break;

		case SHN_MIPS_SCOMMON:

			printk(KERN_DEBUG
			       "simplify_symbols: ignoring SHN_MIPS_SCOMMON symbol <%s> st_shndx %d\n",
			       strtab + sym[i].st_name, sym[i].st_shndx);

			// .sbss section
			break;

		default:
			secbase = sechdrs[sym[i].st_shndx].sh_addr;

			if (strncmp(strtab + sym[i].st_name, "_gp", 3) == 0) {
				save_gp_address(secbase, sym[i].st_value);
			}

			sym[i].st_value += secbase;
			break;
		}

	}

	return ret;
}

#ifdef DEBUG_ELFLOADER
static void dump_elfsymbols(Elf_Shdr * sechdrs, unsigned int symindex,
			    const char *strtab, struct module *mod)
{
	Elf_Sym *sym = (void *)sechdrs[symindex].sh_addr;
	unsigned int i, n = sechdrs[symindex].sh_size / sizeof(Elf_Sym);

	printk(KERN_DEBUG "dump_elfsymbols: n %d\n", n);
	for (i = 1; i < n; i++) {
		printk(KERN_DEBUG " i %d name <%s> 0x%x\n", i,
		       strtab + sym[i].st_name, sym[i].st_value);
	}
}
#endif

static void dump_tc(struct tc *t)
{
	printk(KERN_WARNING "VPE: TC index %d TCStatus 0x%lx halt 0x%lx\n",
	       t->index, read_tc_c0_tcstatus(), read_tc_c0_tchalt());
	printk(KERN_WARNING "VPE: tcrestart 0x%lx\n", read_tc_c0_tcrestart());
}

static void dump_tclist(void)
{
	struct tc *t;

	list_for_each_entry(t, &vpecontrol.tc_list, list) {
		dump_tc(t);
	}
}

/* We are prepared so configure and start the VPE... */
int vpe_run(struct vpe * v)
{
	unsigned long val;
	struct tc *t;

	/* check we are the Master VPE */
	val = read_c0_vpeconf0();
	if (!(val & VPECONF0_MVP)) {
		printk(KERN_WARNING
		       "VPE: only Master VPE's are allowed to configure MT\n");
		return -1;
	}

	/* disable MT (using dvpe) */
	dvpe();

	/* Put MVPE's into 'configuration state' */
	set_c0_mvpcontrol(MVPCONTROL_VPC);

	if (!list_empty(&v->tc)) {
		if ((t = list_entry(v->tc.next, struct tc, tc)) == NULL) {
			printk(KERN_WARNING "VPE: TC %d is already in use.\n",
			       t->index);
			return -ENOEXEC;
		}
	} else {
		printk(KERN_WARNING "VPE: No TC's associated with VPE %d\n",
		       v->minor);
		return -ENOEXEC;
	}

	settc(t->index);

	val = read_vpe_c0_vpeconf0();

	/* should check it is halted, and not activated */
	if ((read_tc_c0_tcstatus() & TCSTATUS_A) || !(read_tc_c0_tchalt() & TCHALT_H)) {
		printk(KERN_WARNING "VPE: TC %d is already doing something!\n",
		       t->index);

		dump_tclist();
		return -ENOEXEC;
	}

	/* Write the address we want it to start running from in the TCPC register. */
	write_tc_c0_tcrestart((unsigned long)v->__start);

	/* write the sivc_info address to tccontext */
	write_tc_c0_tccontext((unsigned long)0);

	/* Set up the XTC bit in vpeconf0 to point at our tc */
	write_vpe_c0_vpeconf0(read_vpe_c0_vpeconf0() | (t->index << VPECONF0_XTC_SHIFT));

	/* mark the TC as activated, not interrupt exempt and not dynamically allocatable */
	val = read_tc_c0_tcstatus();
	val = (val & ~(TCSTATUS_DA | TCSTATUS_IXMT)) | TCSTATUS_A;
	write_tc_c0_tcstatus(val);

	write_tc_c0_tchalt(read_tc_c0_tchalt() & ~TCHALT_H);

	/* set up VPE1 */
	write_vpe_c0_vpecontrol(read_vpe_c0_vpecontrol() & ~VPECONTROL_TE);	// no multiple TC's
	write_vpe_c0_vpeconf0(read_vpe_c0_vpeconf0() | VPECONF0_VPA);	// enable this VPE

	/*
	 * The sde-kit passes 'memsize' to __start in $a3, so set something
	 * here...
	 * Or set $a3 (register 7) to zero and define DFLT_STACK_SIZE and
	 * DFLT_HEAP_SIZE when you compile your program
	 */

	mttgpr(7, 0);

	/* set config to be the same as vpe0, particularly kseg0 coherency alg */
	write_vpe_c0_config(read_c0_config());

	/* clear out any left overs from a previous program */
	write_vpe_c0_cause(0);

	/* take system out of configuration state */
	clear_c0_mvpcontrol(MVPCONTROL_VPC);

	/* clear interrupts enabled IE, ERL, EXL, and KSU from c0 status */
	write_vpe_c0_status(read_vpe_c0_status() & ~(ST0_ERL | ST0_KSU | ST0_IE | ST0_EXL));

	/* set it running */
	evpe(EVPE_ENABLE);

	return 0;
}

static unsigned long find_vpe_symbols(struct vpe * v, Elf_Shdr * sechdrs,
				      unsigned int symindex, const char *strtab,
				      struct module *mod)
{
	Elf_Sym *sym = (void *)sechdrs[symindex].sh_addr;
	unsigned int i, n = sechdrs[symindex].sh_size / sizeof(Elf_Sym);

	for (i = 1; i < n; i++) {
		if (strcmp(strtab + sym[i].st_name, "__start") == 0) {
			v->__start = sym[i].st_value;
		}

		if (strcmp(strtab + sym[i].st_name, "vpe_shared") == 0) {
			v->shared_ptr = (void *)sym[i].st_value;
		}
	}

	return 0;
}

/*
 * Allocates a VPE with some program code space(the load address), copies
 * the contents of the program (p)buffer performing relocatations/etc,
 * free's it when finished.
*/
int vpe_elfload(struct vpe * v)
{
	Elf_Ehdr *hdr;
	Elf_Shdr *sechdrs;
	long err = 0;
	char *secstrings, *strtab = NULL;
	unsigned int len, i, symindex = 0, strindex = 0;

	struct module mod;	// so we can re-use the relocations code

	memset(&mod, 0, sizeof(struct module));
	strcpy(mod.name, "VPE dummy prog module");

	hdr = (Elf_Ehdr *) v->pbuffer;
	len = v->plen;

	/* Sanity checks against insmoding binaries or wrong arch,
	   weird elf version */
	if (memcmp(hdr->e_ident, ELFMAG, 4) != 0
	    || hdr->e_type != ET_REL || !elf_check_arch(hdr)
	    || hdr->e_shentsize != sizeof(*sechdrs)) {
		printk(KERN_WARNING
		       "VPE program, wrong arch or weird elf version\n");

		return -ENOEXEC;
	}

	if (len < hdr->e_shoff + hdr->e_shnum * sizeof(Elf_Shdr)) {
		printk(KERN_ERR "VPE program length %u truncated\n", len);
		return -ENOEXEC;
	}

	/* Convenience variables */
	sechdrs = (void *)hdr + hdr->e_shoff;
	secstrings = (void *)hdr + sechdrs[hdr->e_shstrndx].sh_offset;
	sechdrs[0].sh_addr = 0;

	/* And these should exist, but gcc whinges if we don't init them */
	symindex = strindex = 0;

	for (i = 1; i < hdr->e_shnum; i++) {

		if (sechdrs[i].sh_type != SHT_NOBITS
		    && len < sechdrs[i].sh_offset + sechdrs[i].sh_size) {
			printk(KERN_ERR "VPE program length %u truncated\n",
			       len);
			return -ENOEXEC;
		}

		/* Mark all sections sh_addr with their address in the
		   temporary image. */
		sechdrs[i].sh_addr = (size_t) hdr + sechdrs[i].sh_offset;

		/* Internal symbols and strings. */
		if (sechdrs[i].sh_type == SHT_SYMTAB) {
			symindex = i;
			strindex = sechdrs[i].sh_link;
			strtab = (char *)hdr + sechdrs[strindex].sh_offset;
		}
	}

	layout_sections(&mod, hdr, sechdrs, secstrings);

	v->load_addr = alloc_progmem(mod.core_size);
	memset(v->load_addr, 0, mod.core_size);

	printk("VPE elf_loader: loading to %p\n", v->load_addr);

	for (i = 0; i < hdr->e_shnum; i++) {
		void *dest;

		if (!(sechdrs[i].sh_flags & SHF_ALLOC))
			continue;

		dest = v->load_addr + sechdrs[i].sh_entsize;

		if (sechdrs[i].sh_type != SHT_NOBITS)
			memcpy(dest, (void *)sechdrs[i].sh_addr,
			       sechdrs[i].sh_size);
		/* Update sh_addr to point to copy in image. */
		sechdrs[i].sh_addr = (unsigned long)dest;
	}

	/* Fix up syms, so that st_value is a pointer to location. */
	err =
		simplify_symbols(sechdrs, symindex, strtab, secstrings,
				 hdr->e_shnum, &mod);
	if (err < 0) {
		printk(KERN_WARNING "VPE: unable to simplify symbols\n");
		goto cleanup;
	}

	/* Now do relocations. */
	for (i = 1; i < hdr->e_shnum; i++) {
		const char *strtab = (char *)sechdrs[strindex].sh_addr;
		unsigned int info = sechdrs[i].sh_info;

		/* Not a valid relocation section? */
		if (info >= hdr->e_shnum)
			continue;

		/* Don't bother with non-allocated sections */
		if (!(sechdrs[info].sh_flags & SHF_ALLOC))
			continue;

		if (sechdrs[i].sh_type == SHT_REL)
			err =
				apply_relocations(sechdrs, strtab, symindex, i, &mod);
		else if (sechdrs[i].sh_type == SHT_RELA)
			err = apply_relocate_add(sechdrs, strtab, symindex, i,
						 &mod);
		if (err < 0) {
			printk(KERN_WARNING
			       "vpe_elfload: error in relocations err %ld\n",
			       err);
			goto cleanup;
		}
	}

	/* make sure it's physically written out */
	flush_icache_range((unsigned long)v->load_addr,
			   (unsigned long)v->load_addr + v->len);

	if ((find_vpe_symbols(v, sechdrs, symindex, strtab, &mod)) < 0) {

		printk(KERN_WARNING
		       "VPE: program doesn't contain __start or vpe_shared symbols\n");
		err = -ENOEXEC;
	}

	printk(" elf loaded\n");

cleanup:
	return err;
}

static void dump_vpe(struct vpe * v)
{
	struct tc *t;

	printk(KERN_DEBUG "VPEControl 0x%lx\n", read_vpe_c0_vpecontrol());
	printk(KERN_DEBUG "VPEConf0 0x%lx\n", read_vpe_c0_vpeconf0());

	list_for_each_entry(t, &vpecontrol.tc_list, list) {
		dump_tc(t);
	}
}

/* checks for VPE is unused and gets ready to load program	 */
static int vpe_open(struct inode *inode, struct file *filp)
{
	int minor;
	struct vpe *v;

	/* assume only 1 device at the mo. */
	if ((minor = MINOR(inode->i_rdev)) != 1) {
		printk(KERN_WARNING "VPE: only vpe1 is supported\n");
		return -ENODEV;
	}

	if ((v = get_vpe(minor)) == NULL) {
		printk(KERN_WARNING "VPE: unable to get vpe\n");
		return -ENODEV;
	}

	if (v->state != VPE_STATE_UNUSED) {
		unsigned long tmp;
		struct tc *t;

		printk(KERN_WARNING "VPE: device %d already in use\n", minor);

		dvpe();
		dump_vpe(v);

		printk(KERN_WARNING "VPE: re-initialising %d\n", minor);

		release_progmem(v->load_addr);

		t = get_tc(minor);
		settc(minor);
		tmp = read_tc_c0_tcstatus();

		/* mark not allocated and not dynamically allocatable */
		tmp &= ~(TCSTATUS_A | TCSTATUS_DA);
		tmp |= TCSTATUS_IXMT;	/* interrupt exempt */
		write_tc_c0_tcstatus(tmp);

		write_tc_c0_tchalt(TCHALT_H);

	}

	// allocate it so when we get write ops we know it's expected.
	v->state = VPE_STATE_INUSE;

	/* this of-course trashes what was there before... */
	v->pbuffer = vmalloc(P_SIZE);
	v->plen = P_SIZE;
	v->load_addr = NULL;
	v->len = 0;

	return 0;
}

static int vpe_release(struct inode *inode, struct file *filp)
{
	int minor, ret = 0;
	struct vpe *v;
	Elf_Ehdr *hdr;

	minor = MINOR(inode->i_rdev);
	if ((v = get_vpe(minor)) == NULL)
		return -ENODEV;

	// simple case of fire and forget, so tell the VPE to run...

	hdr = (Elf_Ehdr *) v->pbuffer;
	if (memcmp(hdr->e_ident, ELFMAG, 4) == 0) {
		if (vpe_elfload(v) >= 0)
			vpe_run(v);
		else {
			printk(KERN_WARNING "VPE: ELF load failed.\n");
			ret = -ENOEXEC;
		}
	} else {
		printk(KERN_WARNING "VPE: only elf files are supported\n");
		ret = -ENOEXEC;
	}

	// cleanup any temp buffers
	if (v->pbuffer)
		vfree(v->pbuffer);
	v->plen = 0;
	return ret;
}

static ssize_t vpe_write(struct file *file, const char __user * buffer,
			 size_t count, loff_t * ppos)
{
	int minor;
	size_t ret = count;
	struct vpe *v;

	minor = MINOR(file->f_dentry->d_inode->i_rdev);
	if ((v = get_vpe(minor)) == NULL)
		return -ENODEV;

	if (v->pbuffer == NULL) {
		printk(KERN_ERR "vpe_write: no pbuffer\n");
		return -ENOMEM;
	}

	if ((count + v->len) > v->plen) {
		printk(KERN_WARNING
		       "VPE Loader: elf size too big. Perhaps strip uneeded symbols\n");
		return -ENOMEM;
	}

	count -= copy_from_user(v->pbuffer + v->len, buffer, count);
	if (!count) {
		printk("vpe_write: copy_to_user failed\n");
		return -EFAULT;
	}

	v->len += count;
	return ret;
}

static struct file_operations vpe_fops = {
	.owner = THIS_MODULE,
	.open = vpe_open,
	.release = vpe_release,
	.write = vpe_write
};

/* module wrapper entry points */
/* give me a vpe */
vpe_handle vpe_alloc(void)
{
	int i;
	struct vpe *v;

	/* find a vpe */
	for (i = 1; i < MAX_VPES; i++) {
		if ((v = get_vpe(i)) != NULL) {
			v->state = VPE_STATE_INUSE;
			return v;
		}
	}
	return NULL;
}

EXPORT_SYMBOL(vpe_alloc);

/* start running from here */
int vpe_start(vpe_handle vpe, unsigned long start)
{
	struct vpe *v = vpe;

	v->__start = start;
	return vpe_run(v);
}

EXPORT_SYMBOL(vpe_start);

/* halt it for now */
int vpe_stop(vpe_handle vpe)
{
	struct vpe *v = vpe;
	struct tc *t;
	unsigned int evpe_flags;

	evpe_flags = dvpe();

	if ((t = list_entry(v->tc.next, struct tc, tc)) != NULL) {

		settc(t->index);
		write_vpe_c0_vpeconf0(read_vpe_c0_vpeconf0() & ~VPECONF0_VPA);
	}

	evpe(evpe_flags);

	return 0;
}

EXPORT_SYMBOL(vpe_stop);

/* I've done with it thank you */
int vpe_free(vpe_handle vpe)
{
	struct vpe *v = vpe;
	struct tc *t;
	unsigned int evpe_flags;

	if ((t = list_entry(v->tc.next, struct tc, tc)) == NULL) {
		return -ENOEXEC;
	}

	evpe_flags = dvpe();

	/* Put MVPE's into 'configuration state' */
	set_c0_mvpcontrol(MVPCONTROL_VPC);

	settc(t->index);
	write_vpe_c0_vpeconf0(read_vpe_c0_vpeconf0() & ~VPECONF0_VPA);

	/* mark the TC unallocated and halt'ed */
	write_tc_c0_tcstatus(read_tc_c0_tcstatus() & ~TCSTATUS_A);
	write_tc_c0_tchalt(TCHALT_H);

	v->state = VPE_STATE_UNUSED;

	clear_c0_mvpcontrol(MVPCONTROL_VPC);
	evpe(evpe_flags);

	return 0;
}

EXPORT_SYMBOL(vpe_free);

void *vpe_get_shared(int index)
{
	struct vpe *v;

	if ((v = get_vpe(index)) == NULL) {
		printk(KERN_WARNING "vpe: invalid vpe index %d\n", index);
		return NULL;
	}

	return v->shared_ptr;
}

EXPORT_SYMBOL(vpe_get_shared);

static int __init vpe_module_init(void)
{
	struct vpe *v = NULL;
	struct tc *t;
	unsigned long val;
	int i;

	if (!cpu_has_mipsmt) {
		printk("VPE loader: not a MIPS MT capable processor\n");
		return -ENODEV;
	}

	if ((major = register_chrdev(0, module_name, &vpe_fops) < 0)) {
		printk("VPE loader: unable to register character device\n");
		return major;
	}

	dmt();
	dvpe();

	/* Put MVPE's into 'configuration state' */
	set_c0_mvpcontrol(MVPCONTROL_VPC);

	/* dump_mtregs(); */

	INIT_LIST_HEAD(&vpecontrol.vpe_list);
	INIT_LIST_HEAD(&vpecontrol.tc_list);

	val = read_c0_mvpconf0();
	for (i = 0; i < ((val & MVPCONF0_PTC) + 1); i++) {
		t = alloc_tc(i);

		/* VPE's */
		if (i < ((val & MVPCONF0_PVPE) >> MVPCONF0_PVPE_SHIFT) + 1) {
			settc(i);

			if ((v = alloc_vpe(i)) == NULL) {
				printk(KERN_WARNING "VPE: unable to allocate VPE\n");
				return -ENODEV;
			}

			list_add(&t->tc, &v->tc);	/* add the tc to the list of this vpe's tc's. */

			/* deactivate all but vpe0 */
			if (i != 0) {
				unsigned long tmp = read_vpe_c0_vpeconf0();

				tmp &= ~VPECONF0_VPA;

				/* master VPE */
				tmp |= VPECONF0_MVP;
				write_vpe_c0_vpeconf0(tmp);
			}

			/* disable multi-threading with TC's */
			write_vpe_c0_vpecontrol(read_vpe_c0_vpecontrol() & ~VPECONTROL_TE);

			if (i != 0) {
				write_vpe_c0_status((read_c0_status() &
						     ~(ST0_IM | ST0_IE | ST0_KSU))
						    | ST0_CU0);

				/* set config to be the same as vpe0, particularly kseg0 coherency alg */
				write_vpe_c0_config(read_c0_config());
			}

		}

		/* TC's */
		t->pvpe = v;	/* set the parent vpe */

		if (i != 0) {
			unsigned long tmp;

			/* tc 0 will of course be running.... */
			if (i == 0)
				t->state = TC_STATE_RUNNING;

			settc(i);

			/* bind a TC to each VPE, May as well put all excess TC's
			   on the last VPE */
			if (i >= (((val & MVPCONF0_PVPE) >> MVPCONF0_PVPE_SHIFT) + 1))
				write_tc_c0_tcbind(read_tc_c0_tcbind() |
						   ((val & MVPCONF0_PVPE) >> MVPCONF0_PVPE_SHIFT));
			else
				write_tc_c0_tcbind(read_tc_c0_tcbind() | i);

			tmp = read_tc_c0_tcstatus();

			/* mark not allocated and not dynamically allocatable */
			tmp &= ~(TCSTATUS_A | TCSTATUS_DA);
			tmp |= TCSTATUS_IXMT;	/* interrupt exempt */
			write_tc_c0_tcstatus(tmp);

			write_tc_c0_tchalt(TCHALT_H);
		}
	}

	/* release config state */
	clear_c0_mvpcontrol(MVPCONTROL_VPC);

	return 0;
}

static void __exit vpe_module_exit(void)
{
	struct vpe *v, *n;

	list_for_each_entry_safe(v, n, &vpecontrol.vpe_list, list) {
		if (v->state != VPE_STATE_UNUSED) {
			release_vpe(v);
		}
	}

	unregister_chrdev(major, module_name);
}

module_init(vpe_module_init);
module_exit(vpe_module_exit);
MODULE_DESCRIPTION("MIPS VPE Loader");
MODULE_AUTHOR("Elizabeth Clarke, MIPS Technologies, Inc");
MODULE_LICENSE("GPL");

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
 */
#include <linux/kernel.h>
#include <linux/device.h>
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
#include <asm/mips_mt.h>
#include <asm/processor.h>
#include <asm/system.h>
#include <asm/vpe.h>
#include <asm/kspd.h>
#include <asm/mips_mt.h>

typedef void *vpe_handle;

#ifndef ARCH_SHF_SMALL
#define ARCH_SHF_SMALL 0
#endif

/* If this is set, the section belongs in the init part of the module */
#define INIT_OFFSET_MASK (1UL << (BITS_PER_LONG-1))

/*
 * The number of TCs and VPEs physically available on the core
 */
static int hw_tcs, hw_vpes;
static char module_name[] = "vpe";
static int major;
static const int minor = 1;	/* fixed for now  */

#ifdef CONFIG_MIPS_APSP_KSPD
 static struct kspd_notifications kspd_events;
static int kspd_events_reqd = 0;
#endif

/* grab the likely amount of memory we will need. */
#ifdef CONFIG_MIPS_VPE_LOADER_TOM
#define P_SIZE (2 * 1024 * 1024)
#else
/* add an overhead to the max kmalloc size for non-striped symbols/etc */
#define P_SIZE (256 * 1024)
#endif

extern unsigned long physical_memsize;

#define MAX_VPES 16
#define VPE_PATH_MAX 256

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
	unsigned long len;
	char *pbuffer;
	unsigned long plen;
	unsigned int uid, gid;
	char cwd[VPE_PATH_MAX];

	unsigned long __start;

	/* tc's associated with this vpe */
	struct list_head tc;

	/* The list of vpe's */
	struct list_head list;

	/* shared symbol address */
	void *shared_ptr;

	/* the list of who wants to know when something major happens */
	struct list_head notify;

	unsigned int ntcs;
};

struct tc {
	enum tc_state state;
	int index;

	struct vpe *pvpe;	/* parent VPE */
	struct list_head tc;	/* The list of TC's with this VPE */
	struct list_head list;	/* The global list of tc's */
};

struct {
	/* Virtual processing elements */
	struct list_head vpe_list;

	/* Thread contexts */
	struct list_head tc_list;
} vpecontrol = {
	.vpe_list = LIST_HEAD_INIT(vpecontrol.vpe_list),
	.tc_list = LIST_HEAD_INIT(vpecontrol.tc_list)
};

static void release_progmem(void *ptr);
extern void save_gp_address(unsigned int secbase, unsigned int rel);

/* get the vpe associated with this minor */
struct vpe *get_vpe(int minor)
{
	struct vpe *v;

	if (!cpu_has_mipsmt)
		return NULL;

	list_for_each_entry(v, &vpecontrol.vpe_list, list) {
		if (v->minor == minor)
			return v;
	}

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

	return NULL;
}

struct tc *get_tc_unused(void)
{
	struct tc *t;

	list_for_each_entry(t, &vpecontrol.tc_list, list) {
		if (t->state == TC_STATE_UNUSED)
			return t;
	}

	return NULL;
}

/* allocate a vpe and associate it with this minor (or index) */
struct vpe *alloc_vpe(int minor)
{
	struct vpe *v;

	if ((v = kzalloc(sizeof(struct vpe), GFP_KERNEL)) == NULL) {
		return NULL;
	}

	INIT_LIST_HEAD(&v->tc);
	list_add_tail(&v->list, &vpecontrol.vpe_list);

	INIT_LIST_HEAD(&v->notify);
	v->minor = minor;
	return v;
}

/* allocate a tc. At startup only tc0 is running, all other can be halted. */
struct tc *alloc_tc(int index)
{
	struct tc *tc;

	if ((tc = kzalloc(sizeof(struct tc), GFP_KERNEL)) == NULL)
		goto out;

	INIT_LIST_HEAD(&tc->tc);
	tc->index = index;
	list_add_tail(&tc->list, &vpecontrol.tc_list);

out:
	return tc;
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

	val = read_c0_mvpcontrol();
	printk("MVPControl 0x%lx, STLB %ld VPC %ld EVP %ld\n", val,
	       (val & MVPCONTROL_STLB) >> MVPCONTROL_STLB_SHIFT,
	       (val & MVPCONTROL_VPC) >> MVPCONTROL_VPC_SHIFT,
	       (val & MVPCONTROL_EVP));

	val = read_c0_mvpconf0();
	printk("mvpconf0 0x%lx, PVPE %ld PTC %ld M %ld\n", val,
	       (val & MVPCONF0_PVPE) >> MVPCONF0_PVPE_SHIFT,
	       val & MVPCONF0_PTC, (val & MVPCONF0_M) >> MVPCONF0_M_SHIFT);
}

/* Find some VPE program space  */
static void *alloc_progmem(unsigned long len)
{
#ifdef CONFIG_MIPS_VPE_LOADER_TOM
	/* this means you must tell linux to use less memory than you physically have */
	return pfn_to_kaddr(max_pfn);
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
		printk(KERN_DEBUG "VPE loader: apply_r_mips_gprel16: "
		       "relative address 0x%x out of range of gp register\n",
		       rel);
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
		printk(KERN_DEBUG "VPE loader: "
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
		printk(KERN_DEBUG "VPE loader: apply_r_mips_26 "
		       " unaligned relocation\n");
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
				printk(KERN_DEBUG "VPE loader: "
				       "apply_r_mips_lo16/hi16: 	"
				       "inconsistent value information\n");
				return -ENOEXEC;
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

static char *rstrs[] = {
	[R_MIPS_NONE]	= "MIPS_NONE",
	[R_MIPS_32]	= "MIPS_32",
	[R_MIPS_26]	= "MIPS_26",
	[R_MIPS_HI16]	= "MIPS_HI16",
	[R_MIPS_LO16]	= "MIPS_LO16",
	[R_MIPS_GPREL16] = "MIPS_GPREL16",
	[R_MIPS_PC16] = "MIPS_PC16"
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
			char *r = rstrs[ELF32_R_TYPE(r_info)];
		    	printk(KERN_WARNING "VPE loader: .text+0x%x "
			       "relocation type %s for symbol \"%s\" failed\n",
			       rel[i].r_offset, r ? r : "UNKNOWN",
			       strtab + sym->st_name);
			return res;
		}
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
static void simplify_symbols(Elf_Shdr * sechdrs,
			    unsigned int symindex,
			    const char *strtab,
			    const char *secstrings,
			    unsigned int nsecs, struct module *mod)
{
	Elf_Sym *sym = (void *)sechdrs[symindex].sh_addr;
	unsigned long secbase, bssbase = 0;
	unsigned int i, n = sechdrs[symindex].sh_size / sizeof(Elf_Sym);
	int size;

	/* find the .bss section for COMMON symbols */
	for (i = 0; i < nsecs; i++) {
		if (strncmp(secstrings + sechdrs[i].sh_name, ".bss", 4) == 0) {
			bssbase = sechdrs[i].sh_addr;
			break;
		}
	}

	for (i = 1; i < n; i++) {
		switch (sym[i].st_shndx) {
		case SHN_COMMON:
			/* Allocate space for the symbol in the .bss section.
			   st_value is currently size.
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
			printk(KERN_DEBUG "simplify_symbols: ignoring SHN_MIPS_SCOMMON"
			       "symbol <%s> st_shndx %d\n", strtab + sym[i].st_name,
			       sym[i].st_shndx);
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

/* We are prepared so configure and start the VPE... */
static int vpe_run(struct vpe * v)
{
	unsigned long flags, val, dmt_flag;
	struct vpe_notifications *n;
	unsigned int vpeflags;
	struct tc *t;

	/* check we are the Master VPE */
	local_irq_save(flags);
	val = read_c0_vpeconf0();
	if (!(val & VPECONF0_MVP)) {
		printk(KERN_WARNING
		       "VPE loader: only Master VPE's are allowed to configure MT\n");
		local_irq_restore(flags);

		return -1;
	}

	dmt_flag = dmt();
	vpeflags = dvpe();

	if (!list_empty(&v->tc)) {
		if ((t = list_entry(v->tc.next, struct tc, tc)) == NULL) {
			evpe(vpeflags);
			emt(dmt_flag);
			local_irq_restore(flags);

			printk(KERN_WARNING
			       "VPE loader: TC %d is already in use.\n",
                               t->index);
			return -ENOEXEC;
		}
	} else {
		evpe(vpeflags);
		emt(dmt_flag);
		local_irq_restore(flags);

		printk(KERN_WARNING
		       "VPE loader: No TC's associated with VPE %d\n",
		       v->minor);

		return -ENOEXEC;
	}

	/* Put MVPE's into 'configuration state' */
	set_c0_mvpcontrol(MVPCONTROL_VPC);

	settc(t->index);

	/* should check it is halted, and not activated */
	if ((read_tc_c0_tcstatus() & TCSTATUS_A) || !(read_tc_c0_tchalt() & TCHALT_H)) {
		evpe(vpeflags);
		emt(dmt_flag);
		local_irq_restore(flags);

		printk(KERN_WARNING "VPE loader: TC %d is already active!\n",
		       t->index);

		return -ENOEXEC;
	}

	/* Write the address we want it to start running from in the TCPC register. */
	write_tc_c0_tcrestart((unsigned long)v->__start);
	write_tc_c0_tccontext((unsigned long)0);

	/*
	 * Mark the TC as activated, not interrupt exempt and not dynamically
	 * allocatable
	 */
	val = read_tc_c0_tcstatus();
	val = (val & ~(TCSTATUS_DA | TCSTATUS_IXMT)) | TCSTATUS_A;
	write_tc_c0_tcstatus(val);

	write_tc_c0_tchalt(read_tc_c0_tchalt() & ~TCHALT_H);

	/*
	 * The sde-kit passes 'memsize' to __start in $a3, so set something
	 * here...  Or set $a3 to zero and define DFLT_STACK_SIZE and
	 * DFLT_HEAP_SIZE when you compile your program
	 */
	mttgpr(6, v->ntcs);
	mttgpr(7, physical_memsize);

	/* set up VPE1 */
	/*
	 * bind the TC to VPE 1 as late as possible so we only have the final
	 * VPE registers to set up, and so an EJTAG probe can trigger on it
	 */
	write_tc_c0_tcbind((read_tc_c0_tcbind() & ~TCBIND_CURVPE) | 1);

	write_vpe_c0_vpeconf0(read_vpe_c0_vpeconf0() & ~(VPECONF0_VPA));

	back_to_back_c0_hazard();

	/* Set up the XTC bit in vpeconf0 to point at our tc */
	write_vpe_c0_vpeconf0( (read_vpe_c0_vpeconf0() & ~(VPECONF0_XTC))
	                      | (t->index << VPECONF0_XTC_SHIFT));

	back_to_back_c0_hazard();

	/* enable this VPE */
	write_vpe_c0_vpeconf0(read_vpe_c0_vpeconf0() | VPECONF0_VPA);

	/* clear out any left overs from a previous program */
	write_vpe_c0_status(0);
	write_vpe_c0_cause(0);

	/* take system out of configuration state */
	clear_c0_mvpcontrol(MVPCONTROL_VPC);

#ifdef CONFIG_SMP
	evpe(EVPE_ENABLE);
#else
	evpe(vpeflags);
#endif
	emt(dmt_flag);
	local_irq_restore(flags);

	list_for_each_entry(n, &v->notify, list)
		n->start(minor);

	return 0;
}

static int find_vpe_symbols(struct vpe * v, Elf_Shdr * sechdrs,
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

	if ( (v->__start == 0) || (v->shared_ptr == NULL))
		return -1;

	return 0;
}

/*
 * Allocates a VPE with some program code space(the load address), copies the
 * contents of the program (p)buffer performing relocatations/etc, free's it
 * when finished.
 */
static int vpe_elfload(struct vpe * v)
{
	Elf_Ehdr *hdr;
	Elf_Shdr *sechdrs;
	long err = 0;
	char *secstrings, *strtab = NULL;
	unsigned int len, i, symindex = 0, strindex = 0, relocate = 0;
	struct module mod;	// so we can re-use the relocations code

	memset(&mod, 0, sizeof(struct module));
	strcpy(mod.name, "VPE loader");

	hdr = (Elf_Ehdr *) v->pbuffer;
	len = v->plen;

	/* Sanity checks against insmoding binaries or wrong arch,
	   weird elf version */
	if (memcmp(hdr->e_ident, ELFMAG, 4) != 0
	    || (hdr->e_type != ET_REL && hdr->e_type != ET_EXEC)
	    || !elf_check_arch(hdr)
	    || hdr->e_shentsize != sizeof(*sechdrs)) {
		printk(KERN_WARNING
		       "VPE loader: program wrong arch or weird elf version\n");

		return -ENOEXEC;
	}

	if (hdr->e_type == ET_REL)
		relocate = 1;

	if (len < hdr->e_shoff + hdr->e_shnum * sizeof(Elf_Shdr)) {
		printk(KERN_ERR "VPE loader: program length %u truncated\n",
		       len);

		return -ENOEXEC;
	}

	/* Convenience variables */
	sechdrs = (void *)hdr + hdr->e_shoff;
	secstrings = (void *)hdr + sechdrs[hdr->e_shstrndx].sh_offset;
	sechdrs[0].sh_addr = 0;

	/* And these should exist, but gcc whinges if we don't init them */
	symindex = strindex = 0;

	if (relocate) {
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
	}

	v->load_addr = alloc_progmem(mod.core_size);
	memset(v->load_addr, 0, mod.core_size);

	printk("VPE loader: loading to %p\n", v->load_addr);

	if (relocate) {
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

			printk(KERN_DEBUG " section sh_name %s sh_addr 0x%x\n",
			       secstrings + sechdrs[i].sh_name, sechdrs[i].sh_addr);
		}

 		/* Fix up syms, so that st_value is a pointer to location. */
 		simplify_symbols(sechdrs, symindex, strtab, secstrings,
 				 hdr->e_shnum, &mod);

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
 				err = apply_relocations(sechdrs, strtab, symindex, i,
 							&mod);
 			else if (sechdrs[i].sh_type == SHT_RELA)
 				err = apply_relocate_add(sechdrs, strtab, symindex, i,
 							 &mod);
 			if (err < 0)
 				return err;

  		}
  	} else {
		struct elf_phdr *phdr = (struct elf_phdr *) ((char *)hdr + hdr->e_phoff);

		for (i = 0; i < hdr->e_phnum; i++) {
			if (phdr->p_type != PT_LOAD)
				continue;

			memcpy((void *)phdr->p_vaddr, (char *)hdr + phdr->p_offset, phdr->p_filesz);
			memset((void *)phdr->p_vaddr + phdr->p_filesz, 0, phdr->p_memsz - phdr->p_filesz);
			phdr++;
		}

		for (i = 0; i < hdr->e_shnum; i++) {
 			/* Internal symbols and strings. */
 			if (sechdrs[i].sh_type == SHT_SYMTAB) {
 				symindex = i;
 				strindex = sechdrs[i].sh_link;
 				strtab = (char *)hdr + sechdrs[strindex].sh_offset;

 				/* mark the symtab's address for when we try to find the
 				   magic symbols */
 				sechdrs[i].sh_addr = (size_t) hdr + sechdrs[i].sh_offset;
 			}
		}
	}

	/* make sure it's physically written out */
	flush_icache_range((unsigned long)v->load_addr,
			   (unsigned long)v->load_addr + v->len);

	if ((find_vpe_symbols(v, sechdrs, symindex, strtab, &mod)) < 0) {
		if (v->__start == 0) {
			printk(KERN_WARNING "VPE loader: program does not contain "
			       "a __start symbol\n");
			return -ENOEXEC;
		}

		if (v->shared_ptr == NULL)
			printk(KERN_WARNING "VPE loader: "
			       "program does not contain vpe_shared symbol.\n"
			       " Unable to use AMVP (AP/SP) facilities.\n");
	}

	printk(" elf loaded\n");
	return 0;
}

static void cleanup_tc(struct tc *tc)
{
	unsigned long flags;
	unsigned int mtflags, vpflags;
	int tmp;

	local_irq_save(flags);
	mtflags = dmt();
	vpflags = dvpe();
	/* Put MVPE's into 'configuration state' */
	set_c0_mvpcontrol(MVPCONTROL_VPC);

	settc(tc->index);
	tmp = read_tc_c0_tcstatus();

	/* mark not allocated and not dynamically allocatable */
	tmp &= ~(TCSTATUS_A | TCSTATUS_DA);
	tmp |= TCSTATUS_IXMT;	/* interrupt exempt */
	write_tc_c0_tcstatus(tmp);

	write_tc_c0_tchalt(TCHALT_H);

	/* bind it to anything other than VPE1 */
//	write_tc_c0_tcbind(read_tc_c0_tcbind() & ~TCBIND_CURVPE); // | TCBIND_CURVPE

	clear_c0_mvpcontrol(MVPCONTROL_VPC);
	evpe(vpflags);
	emt(mtflags);
	local_irq_restore(flags);
}

static int getcwd(char *buff, int size)
{
	mm_segment_t old_fs;
	int ret;

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	ret = sys_getcwd(buff, size);

	set_fs(old_fs);

	return ret;
}

/* checks VPE is unused and gets ready to load program  */
static int vpe_open(struct inode *inode, struct file *filp)
{
	enum vpe_state state;
	struct vpe_notifications *not;
	struct vpe *v;
	int ret;

	if (minor != iminor(inode)) {
		/* assume only 1 device at the moment. */
		printk(KERN_WARNING "VPE loader: only vpe1 is supported\n");
		return -ENODEV;
	}

	if ((v = get_vpe(tclimit)) == NULL) {
		printk(KERN_WARNING "VPE loader: unable to get vpe\n");
		return -ENODEV;
	}

	state = xchg(&v->state, VPE_STATE_INUSE);
	if (state != VPE_STATE_UNUSED) {
		printk(KERN_DEBUG "VPE loader: tc in use dumping regs\n");

		list_for_each_entry(not, &v->notify, list) {
			not->stop(tclimit);
		}

		release_progmem(v->load_addr);
		cleanup_tc(get_tc(tclimit));
	}

	/* this of-course trashes what was there before... */
	v->pbuffer = vmalloc(P_SIZE);
	v->plen = P_SIZE;
	v->load_addr = NULL;
	v->len = 0;

	v->uid = filp->f_uid;
	v->gid = filp->f_gid;

#ifdef CONFIG_MIPS_APSP_KSPD
	/* get kspd to tell us when a syscall_exit happens */
	if (!kspd_events_reqd) {
		kspd_notify(&kspd_events);
		kspd_events_reqd++;
	}
#endif

	v->cwd[0] = 0;
	ret = getcwd(v->cwd, VPE_PATH_MAX);
	if (ret < 0)
		printk(KERN_WARNING "VPE loader: open, getcwd returned %d\n", ret);

	v->shared_ptr = NULL;
	v->__start = 0;

	return 0;
}

static int vpe_release(struct inode *inode, struct file *filp)
{
	struct vpe *v;
	Elf_Ehdr *hdr;
	int ret = 0;

	v = get_vpe(tclimit);
	if (v == NULL)
		return -ENODEV;

	hdr = (Elf_Ehdr *) v->pbuffer;
	if (memcmp(hdr->e_ident, ELFMAG, 4) == 0) {
		if (vpe_elfload(v) >= 0) {
			vpe_run(v);
		} else {
 			printk(KERN_WARNING "VPE loader: ELF load failed.\n");
			ret = -ENOEXEC;
		}
	} else {
 		printk(KERN_WARNING "VPE loader: only elf files are supported\n");
		ret = -ENOEXEC;
	}

	/* It's good to be able to run the SP and if it chokes have a look at
	   the /dev/rt?. But if we reset the pointer to the shared struct we
	   loose what has happened. So perhaps if garbage is sent to the vpe
	   device, use it as a trigger for the reset. Hopefully a nice
	   executable will be along shortly. */
	if (ret < 0)
		v->shared_ptr = NULL;

	// cleanup any temp buffers
	if (v->pbuffer)
		vfree(v->pbuffer);
	v->plen = 0;
	return ret;
}

static ssize_t vpe_write(struct file *file, const char __user * buffer,
			 size_t count, loff_t * ppos)
{
	size_t ret = count;
	struct vpe *v;

	if (iminor(file->f_path.dentry->d_inode) != minor)
		return -ENODEV;

	v = get_vpe(tclimit);
	if (v == NULL)
		return -ENODEV;

	if (v->pbuffer == NULL) {
		printk(KERN_ERR "VPE loader: no buffer for program\n");
		return -ENOMEM;
	}

	if ((count + v->len) > v->plen) {
		printk(KERN_WARNING
		       "VPE loader: elf size too big. Perhaps strip uneeded symbols\n");
		return -ENOMEM;
	}

	count -= copy_from_user(v->pbuffer + v->len, buffer, count);
	if (!count)
		return -EFAULT;

	v->len += count;
	return ret;
}

static const struct file_operations vpe_fops = {
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

	if ((v = get_vpe(index)) == NULL)
		return NULL;

	return v->shared_ptr;
}

EXPORT_SYMBOL(vpe_get_shared);

int vpe_getuid(int index)
{
	struct vpe *v;

	if ((v = get_vpe(index)) == NULL)
		return -1;

	return v->uid;
}

EXPORT_SYMBOL(vpe_getuid);

int vpe_getgid(int index)
{
	struct vpe *v;

	if ((v = get_vpe(index)) == NULL)
		return -1;

	return v->gid;
}

EXPORT_SYMBOL(vpe_getgid);

int vpe_notify(int index, struct vpe_notifications *notify)
{
	struct vpe *v;

	if ((v = get_vpe(index)) == NULL)
		return -1;

	list_add(&notify->list, &v->notify);
	return 0;
}

EXPORT_SYMBOL(vpe_notify);

char *vpe_getcwd(int index)
{
	struct vpe *v;

	if ((v = get_vpe(index)) == NULL)
		return NULL;

	return v->cwd;
}

EXPORT_SYMBOL(vpe_getcwd);

#ifdef CONFIG_MIPS_APSP_KSPD
static void kspd_sp_exit( int sp_id)
{
	cleanup_tc(get_tc(sp_id));
}
#endif

static ssize_t store_kill(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t len)
{
	struct vpe *vpe = get_vpe(tclimit);
	struct vpe_notifications *not;

	list_for_each_entry(not, &vpe->notify, list) {
		not->stop(tclimit);
	}

	release_progmem(vpe->load_addr);
	cleanup_tc(get_tc(tclimit));
	vpe_stop(vpe);
	vpe_free(vpe);

	return len;
}

static ssize_t show_ntcs(struct device *cd, struct device_attribute *attr,
			 char *buf)
{
	struct vpe *vpe = get_vpe(tclimit);

	return sprintf(buf, "%d\n", vpe->ntcs);
}

static ssize_t store_ntcs(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t len)
{
	struct vpe *vpe = get_vpe(tclimit);
	unsigned long new;
	char *endp;

	new = simple_strtoul(buf, &endp, 0);
	if (endp == buf)
		goto out_einval;

	if (new == 0 || new > (hw_tcs - tclimit))
		goto out_einval;

	vpe->ntcs = new;

	return len;

out_einval:
	return -EINVAL;;
}

static struct device_attribute vpe_class_attributes[] = {
	__ATTR(kill, S_IWUSR, NULL, store_kill),
	__ATTR(ntcs, S_IRUGO | S_IWUSR, show_ntcs, store_ntcs),
	{}
};

static void vpe_device_release(struct device *cd)
{
	kfree(cd);
}

struct class vpe_class = {
	.name = "vpe",
	.owner = THIS_MODULE,
	.dev_release = vpe_device_release,
	.dev_attrs = vpe_class_attributes,
};

struct device vpe_device;

static int __init vpe_module_init(void)
{
	unsigned int mtflags, vpflags;
	unsigned long flags, val;
	struct vpe *v = NULL;
	struct tc *t;
	int tc, err;

	if (!cpu_has_mipsmt) {
		printk("VPE loader: not a MIPS MT capable processor\n");
		return -ENODEV;
	}

	if (vpelimit == 0) {
		printk(KERN_WARNING "No VPEs reserved for AP/SP, not "
		       "initializing VPE loader.\nPass maxvpes=<n> argument as "
		       "kernel argument\n");

		return -ENODEV;
	}

	if (tclimit == 0) {
		printk(KERN_WARNING "No TCs reserved for AP/SP, not "
		       "initializing VPE loader.\nPass maxtcs=<n> argument as "
		       "kernel argument\n");

		return -ENODEV;
	}

	major = register_chrdev(0, module_name, &vpe_fops);
	if (major < 0) {
		printk("VPE loader: unable to register character device\n");
		return major;
	}

	err = class_register(&vpe_class);
	if (err) {
		printk(KERN_ERR "vpe_class registration failed\n");
		goto out_chrdev;
	}

	device_initialize(&vpe_device);
	vpe_device.class	= &vpe_class,
	vpe_device.parent	= NULL,
	strlcpy(vpe_device.bus_id, "vpe1", BUS_ID_SIZE);
	vpe_device.devt = MKDEV(major, minor);
	err = device_add(&vpe_device);
	if (err) {
		printk(KERN_ERR "Adding vpe_device failed\n");
		goto out_class;
	}

	local_irq_save(flags);
	mtflags = dmt();
	vpflags = dvpe();

	/* Put MVPE's into 'configuration state' */
	set_c0_mvpcontrol(MVPCONTROL_VPC);

	/* dump_mtregs(); */

	val = read_c0_mvpconf0();
	hw_tcs = (val & MVPCONF0_PTC) + 1;
	hw_vpes = ((val & MVPCONF0_PVPE) >> MVPCONF0_PVPE_SHIFT) + 1;

	for (tc = tclimit; tc < hw_tcs; tc++) {
		/*
		 * Must re-enable multithreading temporarily or in case we
		 * reschedule send IPIs or similar we might hang.
		 */
		clear_c0_mvpcontrol(MVPCONTROL_VPC);
		evpe(vpflags);
		emt(mtflags);
		local_irq_restore(flags);
		t = alloc_tc(tc);
		if (!t) {
			err = -ENOMEM;
			goto out;
		}

		local_irq_save(flags);
		mtflags = dmt();
		vpflags = dvpe();
		set_c0_mvpcontrol(MVPCONTROL_VPC);

		/* VPE's */
		if (tc < hw_tcs) {
			settc(tc);

			if ((v = alloc_vpe(tc)) == NULL) {
				printk(KERN_WARNING "VPE: unable to allocate VPE\n");

				goto out_reenable;
			}

			v->ntcs = hw_tcs - tclimit;

			/* add the tc to the list of this vpe's tc's. */
			list_add(&t->tc, &v->tc);

			/* deactivate all but vpe0 */
			if (tc >= tclimit) {
				unsigned long tmp = read_vpe_c0_vpeconf0();

				tmp &= ~VPECONF0_VPA;

				/* master VPE */
				tmp |= VPECONF0_MVP;
				write_vpe_c0_vpeconf0(tmp);
			}

			/* disable multi-threading with TC's */
			write_vpe_c0_vpecontrol(read_vpe_c0_vpecontrol() & ~VPECONTROL_TE);

			if (tc >= vpelimit) {
				/*
				 * Set config to be the same as vpe0,
				 * particularly kseg0 coherency alg
				 */
				write_vpe_c0_config(read_c0_config());
			}
		}

		/* TC's */
		t->pvpe = v;	/* set the parent vpe */

		if (tc >= tclimit) {
			unsigned long tmp;

			settc(tc);

			/* Any TC that is bound to VPE0 gets left as is - in case
			   we are running SMTC on VPE0. A TC that is bound to any
			   other VPE gets bound to VPE0, ideally I'd like to make
			   it homeless but it doesn't appear to let me bind a TC
			   to a non-existent VPE. Which is perfectly reasonable.

			   The (un)bound state is visible to an EJTAG probe so may
			   notify GDB...
			*/

			if (((tmp = read_tc_c0_tcbind()) & TCBIND_CURVPE)) {
				/* tc is bound >vpe0 */
				write_tc_c0_tcbind(tmp & ~TCBIND_CURVPE);

				t->pvpe = get_vpe(0);	/* set the parent vpe */
			}

			tmp = read_tc_c0_tcstatus();

			/* mark not activated and not dynamically allocatable */
			tmp &= ~(TCSTATUS_A | TCSTATUS_DA);
			tmp |= TCSTATUS_IXMT;	/* interrupt exempt */
			write_tc_c0_tcstatus(tmp);

			write_tc_c0_tchalt(TCHALT_H);
		}
	}

out_reenable:
	/* release config state */
	clear_c0_mvpcontrol(MVPCONTROL_VPC);

	evpe(vpflags);
	emt(mtflags);
	local_irq_restore(flags);

#ifdef CONFIG_MIPS_APSP_KSPD
	kspd_events.kspd_sp_exit = kspd_sp_exit;
#endif
	return 0;

out_class:
	class_unregister(&vpe_class);
out_chrdev:
	unregister_chrdev(major, module_name);

out:
	return err;
}

static void __exit vpe_module_exit(void)
{
	struct vpe *v, *n;

	list_for_each_entry_safe(v, n, &vpecontrol.vpe_list, list) {
		if (v->state != VPE_STATE_UNUSED) {
			release_vpe(v);
		}
	}

	device_del(&vpe_device);
	unregister_chrdev(major, module_name);
}

module_init(vpe_module_init);
module_exit(vpe_module_exit);
MODULE_DESCRIPTION("MIPS VPE Loader");
MODULE_AUTHOR("Elizabeth Oldham, MIPS Technologies, Inc.");
MODULE_LICENSE("GPL");

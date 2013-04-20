/*
 * Copyright (C) 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 * Copyright (C) 2002-2006 Novell, Inc.
 *	Jan Beulich <jbeulich@novell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * A simple API for unwinding kernel stacks.  This is used for
 * debugging and error reporting purposes.  The kernel doesn't need
 * full-blown stack unwinding with all the bells and whistles, so there
 * is not much point in implementing the full Dwarf2 unwind API.
 */

#include <linux/sched.h>
#include <linux/module.h>
#include <linux/bootmem.h>
#include <linux/sort.h>
#include <linux/slab.h>
#include <linux/stop_machine.h>
#include <linux/uaccess.h>
#include <linux/ptrace.h>
#include <asm/sections.h>
#include <asm/unaligned.h>
#include <asm/unwind.h>

extern char __start_unwind[], __end_unwind[];
/* extern const u8 __start_unwind_hdr[], __end_unwind_hdr[];*/

/* #define UNWIND_DEBUG */

#ifdef UNWIND_DEBUG
int dbg_unw;
#define unw_debug(fmt, ...)			\
do {						\
	if (dbg_unw)				\
		pr_info(fmt, ##__VA_ARGS__);	\
} while (0);
#else
#define unw_debug(fmt, ...)
#endif

#define MAX_STACK_DEPTH 8

#define EXTRA_INFO(f) { \
		BUILD_BUG_ON_ZERO(offsetof(struct unwind_frame_info, f) \
				% FIELD_SIZEOF(struct unwind_frame_info, f)) \
				+ offsetof(struct unwind_frame_info, f) \
				/ FIELD_SIZEOF(struct unwind_frame_info, f), \
				FIELD_SIZEOF(struct unwind_frame_info, f) \
	}
#define PTREGS_INFO(f) EXTRA_INFO(regs.f)

static const struct {
	unsigned offs:BITS_PER_LONG / 2;
	unsigned width:BITS_PER_LONG / 2;
} reg_info[] = {
UNW_REGISTER_INFO};

#undef PTREGS_INFO
#undef EXTRA_INFO

#ifndef REG_INVALID
#define REG_INVALID(r) (reg_info[r].width == 0)
#endif

#define DW_CFA_nop                          0x00
#define DW_CFA_set_loc                      0x01
#define DW_CFA_advance_loc1                 0x02
#define DW_CFA_advance_loc2                 0x03
#define DW_CFA_advance_loc4                 0x04
#define DW_CFA_offset_extended              0x05
#define DW_CFA_restore_extended             0x06
#define DW_CFA_undefined                    0x07
#define DW_CFA_same_value                   0x08
#define DW_CFA_register                     0x09
#define DW_CFA_remember_state               0x0a
#define DW_CFA_restore_state                0x0b
#define DW_CFA_def_cfa                      0x0c
#define DW_CFA_def_cfa_register             0x0d
#define DW_CFA_def_cfa_offset               0x0e
#define DW_CFA_def_cfa_expression           0x0f
#define DW_CFA_expression                   0x10
#define DW_CFA_offset_extended_sf           0x11
#define DW_CFA_def_cfa_sf                   0x12
#define DW_CFA_def_cfa_offset_sf            0x13
#define DW_CFA_val_offset                   0x14
#define DW_CFA_val_offset_sf                0x15
#define DW_CFA_val_expression               0x16
#define DW_CFA_lo_user                      0x1c
#define DW_CFA_GNU_window_save              0x2d
#define DW_CFA_GNU_args_size                0x2e
#define DW_CFA_GNU_negative_offset_extended 0x2f
#define DW_CFA_hi_user                      0x3f

#define DW_EH_PE_FORM     0x07
#define DW_EH_PE_native   0x00
#define DW_EH_PE_leb128   0x01
#define DW_EH_PE_data2    0x02
#define DW_EH_PE_data4    0x03
#define DW_EH_PE_data8    0x04
#define DW_EH_PE_signed   0x08
#define DW_EH_PE_ADJUST   0x70
#define DW_EH_PE_abs      0x00
#define DW_EH_PE_pcrel    0x10
#define DW_EH_PE_textrel  0x20
#define DW_EH_PE_datarel  0x30
#define DW_EH_PE_funcrel  0x40
#define DW_EH_PE_aligned  0x50
#define DW_EH_PE_indirect 0x80
#define DW_EH_PE_omit     0xff

typedef unsigned long uleb128_t;
typedef signed long sleb128_t;

static struct unwind_table {
	struct {
		unsigned long pc;
		unsigned long range;
	} core, init;
	const void *address;
	unsigned long size;
	const unsigned char *header;
	unsigned long hdrsz;
	struct unwind_table *link;
	const char *name;
} root_table;

struct unwind_item {
	enum item_location {
		Nowhere,
		Memory,
		Register,
		Value
	} where;
	uleb128_t value;
};

struct unwind_state {
	uleb128_t loc, org;
	const u8 *cieStart, *cieEnd;
	uleb128_t codeAlign;
	sleb128_t dataAlign;
	struct cfa {
		uleb128_t reg, offs;
	} cfa;
	struct unwind_item regs[ARRAY_SIZE(reg_info)];
	unsigned stackDepth:8;
	unsigned version:8;
	const u8 *label;
	const u8 *stack[MAX_STACK_DEPTH];
};

static const struct cfa badCFA = { ARRAY_SIZE(reg_info), 1 };

static struct unwind_table *find_table(unsigned long pc)
{
	struct unwind_table *table;

	for (table = &root_table; table; table = table->link)
		if ((pc >= table->core.pc
		     && pc < table->core.pc + table->core.range)
		    || (pc >= table->init.pc
			&& pc < table->init.pc + table->init.range))
			break;

	return table;
}

static unsigned long read_pointer(const u8 **pLoc,
				  const void *end, signed ptrType);

static void init_unwind_table(struct unwind_table *table, const char *name,
			      const void *core_start, unsigned long core_size,
			      const void *init_start, unsigned long init_size,
			      const void *table_start, unsigned long table_size,
			      const u8 *header_start, unsigned long header_size)
{
	const u8 *ptr = header_start + 4;
	const u8 *end = header_start + header_size;

	table->core.pc = (unsigned long)core_start;
	table->core.range = core_size;
	table->init.pc = (unsigned long)init_start;
	table->init.range = init_size;
	table->address = table_start;
	table->size = table_size;

	/* See if the linker provided table looks valid. */
	if (header_size <= 4
	    || header_start[0] != 1
	    || (void *)read_pointer(&ptr, end, header_start[1]) != table_start
	    || header_start[2] == DW_EH_PE_omit
	    || read_pointer(&ptr, end, header_start[2]) <= 0
	    || header_start[3] == DW_EH_PE_omit)
		header_start = NULL;

	table->hdrsz = header_size;
	smp_wmb();
	table->header = header_start;
	table->link = NULL;
	table->name = name;
}

void __init arc_unwind_init(void)
{
	init_unwind_table(&root_table, "kernel", _text, _end - _text, NULL, 0,
			  __start_unwind, __end_unwind - __start_unwind,
			  NULL, 0);
	  /*__start_unwind_hdr, __end_unwind_hdr - __start_unwind_hdr);*/
}

static const u32 bad_cie, not_fde;
static const u32 *cie_for_fde(const u32 *fde, const struct unwind_table *);
static signed fde_pointer_type(const u32 *cie);

struct eh_frame_hdr_table_entry {
	unsigned long start, fde;
};

static int cmp_eh_frame_hdr_table_entries(const void *p1, const void *p2)
{
	const struct eh_frame_hdr_table_entry *e1 = p1;
	const struct eh_frame_hdr_table_entry *e2 = p2;

	return (e1->start > e2->start) - (e1->start < e2->start);
}

static void swap_eh_frame_hdr_table_entries(void *p1, void *p2, int size)
{
	struct eh_frame_hdr_table_entry *e1 = p1;
	struct eh_frame_hdr_table_entry *e2 = p2;
	unsigned long v;

	v = e1->start;
	e1->start = e2->start;
	e2->start = v;
	v = e1->fde;
	e1->fde = e2->fde;
	e2->fde = v;
}

static void __init setup_unwind_table(struct unwind_table *table,
				      void *(*alloc) (unsigned long))
{
	const u8 *ptr;
	unsigned long tableSize = table->size, hdrSize;
	unsigned n;
	const u32 *fde;
	struct {
		u8 version;
		u8 eh_frame_ptr_enc;
		u8 fde_count_enc;
		u8 table_enc;
		unsigned long eh_frame_ptr;
		unsigned int fde_count;
		struct eh_frame_hdr_table_entry table[];
	} __attribute__ ((__packed__)) *header;

	if (table->header)
		return;

	if (table->hdrsz)
		pr_warn(".eh_frame_hdr for '%s' present but unusable\n",
			table->name);

	if (tableSize & (sizeof(*fde) - 1))
		return;

	for (fde = table->address, n = 0;
	     tableSize > sizeof(*fde) && tableSize - sizeof(*fde) >= *fde;
	     tableSize -= sizeof(*fde) + *fde, fde += 1 + *fde / sizeof(*fde)) {
		const u32 *cie = cie_for_fde(fde, table);
		signed ptrType;

		if (cie == &not_fde)
			continue;
		if (cie == NULL || cie == &bad_cie)
			return;
		ptrType = fde_pointer_type(cie);
		if (ptrType < 0)
			return;

		ptr = (const u8 *)(fde + 2);
		if (!read_pointer(&ptr, (const u8 *)(fde + 1) + *fde,
								ptrType)) {
			/* FIXME_Rajesh We have 4 instances of null addresses
			 * instead of the initial loc addr
			 * return;
			 */
		}
		++n;
	}

	if (tableSize || !n)
		return;

	hdrSize = 4 + sizeof(unsigned long) + sizeof(unsigned int)
	    + 2 * n * sizeof(unsigned long);
	header = alloc(hdrSize);
	if (!header)
		return;
	header->version = 1;
	header->eh_frame_ptr_enc = DW_EH_PE_abs | DW_EH_PE_native;
	header->fde_count_enc = DW_EH_PE_abs | DW_EH_PE_data4;
	header->table_enc = DW_EH_PE_abs | DW_EH_PE_native;
	put_unaligned((unsigned long)table->address, &header->eh_frame_ptr);
	BUILD_BUG_ON(offsetof(typeof(*header), fde_count)
		     % __alignof(typeof(header->fde_count)));
	header->fde_count = n;

	BUILD_BUG_ON(offsetof(typeof(*header), table)
		     % __alignof(typeof(*header->table)));
	for (fde = table->address, tableSize = table->size, n = 0;
	     tableSize;
	     tableSize -= sizeof(*fde) + *fde, fde += 1 + *fde / sizeof(*fde)) {
		/* const u32 *cie = fde + 1 - fde[1] / sizeof(*fde); */
		const u32 *cie = (const u32 *)(fde[1]);

		if (fde[1] == 0xffffffff)
			continue;	/* this is a CIE */
		ptr = (const u8 *)(fde + 2);
		header->table[n].start = read_pointer(&ptr,
						      (const u8 *)(fde + 1) +
						      *fde,
						      fde_pointer_type(cie));
		header->table[n].fde = (unsigned long)fde;
		++n;
	}
	WARN_ON(n != header->fde_count);

	sort(header->table,
	     n,
	     sizeof(*header->table),
	     cmp_eh_frame_hdr_table_entries, swap_eh_frame_hdr_table_entries);

	table->hdrsz = hdrSize;
	smp_wmb();
	table->header = (const void *)header;
}

static void *__init balloc(unsigned long sz)
{
	return __alloc_bootmem_nopanic(sz,
				       sizeof(unsigned int),
				       __pa(MAX_DMA_ADDRESS));
}

void __init arc_unwind_setup(void)
{
	setup_unwind_table(&root_table, balloc);
}

#ifdef CONFIG_MODULES

static struct unwind_table *last_table;

/* Must be called with module_mutex held. */
void *unwind_add_table(struct module *module, const void *table_start,
		       unsigned long table_size)
{
	struct unwind_table *table;

	if (table_size <= 0)
		return NULL;

	table = kmalloc(sizeof(*table), GFP_KERNEL);
	if (!table)
		return NULL;

	init_unwind_table(table, module->name,
			  module->module_core, module->core_size,
			  module->module_init, module->init_size,
			  table_start, table_size,
			  NULL, 0);

#ifdef UNWIND_DEBUG
	unw_debug("Table added for [%s] %lx %lx\n",
		module->name, table->core.pc, table->core.range);
#endif
	if (last_table)
		last_table->link = table;
	else
		root_table.link = table;
	last_table = table;

	return table;
}

struct unlink_table_info {
	struct unwind_table *table;
	int init_only;
};

static int unlink_table(void *arg)
{
	struct unlink_table_info *info = arg;
	struct unwind_table *table = info->table, *prev;

	for (prev = &root_table; prev->link && prev->link != table;
	     prev = prev->link)
		;

	if (prev->link) {
		if (info->init_only) {
			table->init.pc = 0;
			table->init.range = 0;
			info->table = NULL;
		} else {
			prev->link = table->link;
			if (!prev->link)
				last_table = prev;
		}
	} else
		info->table = NULL;

	return 0;
}

/* Must be called with module_mutex held. */
void unwind_remove_table(void *handle, int init_only)
{
	struct unwind_table *table = handle;
	struct unlink_table_info info;

	if (!table || table == &root_table)
		return;

	if (init_only && table == last_table) {
		table->init.pc = 0;
		table->init.range = 0;
		return;
	}

	info.table = table;
	info.init_only = init_only;

	unlink_table(&info); /* XXX: SMP */
	kfree(table);
}

#endif /* CONFIG_MODULES */

static uleb128_t get_uleb128(const u8 **pcur, const u8 *end)
{
	const u8 *cur = *pcur;
	uleb128_t value;
	unsigned shift;

	for (shift = 0, value = 0; cur < end; shift += 7) {
		if (shift + 7 > 8 * sizeof(value)
		    && (*cur & 0x7fU) >= (1U << (8 * sizeof(value) - shift))) {
			cur = end + 1;
			break;
		}
		value |= (uleb128_t) (*cur & 0x7f) << shift;
		if (!(*cur++ & 0x80))
			break;
	}
	*pcur = cur;

	return value;
}

static sleb128_t get_sleb128(const u8 **pcur, const u8 *end)
{
	const u8 *cur = *pcur;
	sleb128_t value;
	unsigned shift;

	for (shift = 0, value = 0; cur < end; shift += 7) {
		if (shift + 7 > 8 * sizeof(value)
		    && (*cur & 0x7fU) >= (1U << (8 * sizeof(value) - shift))) {
			cur = end + 1;
			break;
		}
		value |= (sleb128_t) (*cur & 0x7f) << shift;
		if (!(*cur & 0x80)) {
			value |= -(*cur++ & 0x40) << shift;
			break;
		}
	}
	*pcur = cur;

	return value;
}

static const u32 *cie_for_fde(const u32 *fde, const struct unwind_table *table)
{
	const u32 *cie;

	if (!*fde || (*fde & (sizeof(*fde) - 1)))
		return &bad_cie;

	if (fde[1] == 0xffffffff)
		return &not_fde;	/* this is a CIE */

	if ((fde[1] & (sizeof(*fde) - 1)))
/* || fde[1] > (unsigned long)(fde + 1) - (unsigned long)table->address) */
		return NULL;	/* this is not a valid FDE */

	/* cie = fde + 1 - fde[1] / sizeof(*fde); */
	cie = (u32 *) fde[1];

	if (*cie <= sizeof(*cie) + 4 || *cie >= fde[1] - sizeof(*fde)
	    || (*cie & (sizeof(*cie) - 1))
	    || (cie[1] != 0xffffffff))
		return NULL;	/* this is not a (valid) CIE */
	return cie;
}

static unsigned long read_pointer(const u8 **pLoc, const void *end,
				  signed ptrType)
{
	unsigned long value = 0;
	union {
		const u8 *p8;
		const u16 *p16u;
		const s16 *p16s;
		const u32 *p32u;
		const s32 *p32s;
		const unsigned long *pul;
	} ptr;

	if (ptrType < 0 || ptrType == DW_EH_PE_omit)
		return 0;
	ptr.p8 = *pLoc;
	switch (ptrType & DW_EH_PE_FORM) {
	case DW_EH_PE_data2:
		if (end < (const void *)(ptr.p16u + 1))
			return 0;
		if (ptrType & DW_EH_PE_signed)
			value = get_unaligned((u16 *) ptr.p16s++);
		else
			value = get_unaligned((u16 *) ptr.p16u++);
		break;
	case DW_EH_PE_data4:
#ifdef CONFIG_64BIT
		if (end < (const void *)(ptr.p32u + 1))
			return 0;
		if (ptrType & DW_EH_PE_signed)
			value = get_unaligned(ptr.p32s++);
		else
			value = get_unaligned(ptr.p32u++);
		break;
	case DW_EH_PE_data8:
		BUILD_BUG_ON(sizeof(u64) != sizeof(value));
#else
		BUILD_BUG_ON(sizeof(u32) != sizeof(value));
#endif
	case DW_EH_PE_native:
		if (end < (const void *)(ptr.pul + 1))
			return 0;
		value = get_unaligned((unsigned long *)ptr.pul++);
		break;
	case DW_EH_PE_leb128:
		BUILD_BUG_ON(sizeof(uleb128_t) > sizeof(value));
		value = ptrType & DW_EH_PE_signed ? get_sleb128(&ptr.p8, end)
		    : get_uleb128(&ptr.p8, end);
		if ((const void *)ptr.p8 > end)
			return 0;
		break;
	default:
		return 0;
	}
	switch (ptrType & DW_EH_PE_ADJUST) {
	case DW_EH_PE_abs:
		break;
	case DW_EH_PE_pcrel:
		value += (unsigned long)*pLoc;
		break;
	default:
		return 0;
	}
	if ((ptrType & DW_EH_PE_indirect)
	    && __get_user(value, (unsigned long __user *)value))
		return 0;
	*pLoc = ptr.p8;

	return value;
}

static signed fde_pointer_type(const u32 *cie)
{
	const u8 *ptr = (const u8 *)(cie + 2);
	unsigned version = *ptr;

	if (version != 1)
		return -1;	/* unsupported */

	if (*++ptr) {
		const char *aug;
		const u8 *end = (const u8 *)(cie + 1) + *cie;
		uleb128_t len;

		/* check if augmentation size is first (and thus present) */
		if (*ptr != 'z')
			return -1;

		/* check if augmentation string is nul-terminated */
		aug = (const void *)ptr;
		ptr = memchr(aug, 0, end - ptr);
		if (ptr == NULL)
			return -1;

		++ptr;		/* skip terminator */
		get_uleb128(&ptr, end);	/* skip code alignment */
		get_sleb128(&ptr, end);	/* skip data alignment */
		/* skip return address column */
		version <= 1 ? (void) ++ptr : (void)get_uleb128(&ptr, end);
		len = get_uleb128(&ptr, end);	/* augmentation length */

		if (ptr + len < ptr || ptr + len > end)
			return -1;

		end = ptr + len;
		while (*++aug) {
			if (ptr >= end)
				return -1;
			switch (*aug) {
			case 'L':
				++ptr;
				break;
			case 'P':{
					signed ptrType = *ptr++;

					if (!read_pointer(&ptr, end, ptrType)
					    || ptr > end)
						return -1;
				}
				break;
			case 'R':
				return *ptr;
			default:
				return -1;
			}
		}
	}
	return DW_EH_PE_native | DW_EH_PE_abs;
}

static int advance_loc(unsigned long delta, struct unwind_state *state)
{
	state->loc += delta * state->codeAlign;

	/* FIXME_Rajesh: Probably we are defining for the initial range as well;
	   return delta > 0;
	 */
	unw_debug("delta %3lu => loc 0x%lx: ", delta, state->loc);
	return 1;
}

static void set_rule(uleb128_t reg, enum item_location where, uleb128_t value,
		     struct unwind_state *state)
{
	if (reg < ARRAY_SIZE(state->regs)) {
		state->regs[reg].where = where;
		state->regs[reg].value = value;

#ifdef UNWIND_DEBUG
		unw_debug("r%lu: ", reg);
		switch (where) {
		case Nowhere:
			unw_debug("s ");
			break;
		case Memory:
			unw_debug("c(%lu) ", value);
			break;
		case Register:
			unw_debug("r(%lu) ", value);
			break;
		case Value:
			unw_debug("v(%lu) ", value);
			break;
		default:
			break;
		}
#endif
	}
}

static int processCFI(const u8 *start, const u8 *end, unsigned long targetLoc,
		      signed ptrType, struct unwind_state *state)
{
	union {
		const u8 *p8;
		const u16 *p16;
		const u32 *p32;
	} ptr;
	int result = 1;
	u8 opcode;

	if (start != state->cieStart) {
		state->loc = state->org;
		result =
		    processCFI(state->cieStart, state->cieEnd, 0, ptrType,
			       state);
		if (targetLoc == 0 && state->label == NULL)
			return result;
	}
	for (ptr.p8 = start; result && ptr.p8 < end;) {
		switch (*ptr.p8 >> 6) {
			uleb128_t value;

		case 0:
			opcode = *ptr.p8++;

			switch (opcode) {
			case DW_CFA_nop:
				unw_debug("cfa nop ");
				break;
			case DW_CFA_set_loc:
				state->loc = read_pointer(&ptr.p8, end,
							  ptrType);
				if (state->loc == 0)
					result = 0;
				unw_debug("cfa_set_loc: 0x%lx ", state->loc);
				break;
			case DW_CFA_advance_loc1:
				unw_debug("\ncfa advance loc1:");
				result = ptr.p8 < end
				    && advance_loc(*ptr.p8++, state);
				break;
			case DW_CFA_advance_loc2:
				value = *ptr.p8++;
				value += *ptr.p8++ << 8;
				unw_debug("\ncfa advance loc2:");
				result = ptr.p8 <= end + 2
				    /* && advance_loc(*ptr.p16++, state); */
				    && advance_loc(value, state);
				break;
			case DW_CFA_advance_loc4:
				unw_debug("\ncfa advance loc4:");
				result = ptr.p8 <= end + 4
				    && advance_loc(*ptr.p32++, state);
				break;
			case DW_CFA_offset_extended:
				value = get_uleb128(&ptr.p8, end);
				unw_debug("cfa_offset_extended: ");
				set_rule(value, Memory,
					 get_uleb128(&ptr.p8, end), state);
				break;
			case DW_CFA_val_offset:
				value = get_uleb128(&ptr.p8, end);
				set_rule(value, Value,
					 get_uleb128(&ptr.p8, end), state);
				break;
			case DW_CFA_offset_extended_sf:
				value = get_uleb128(&ptr.p8, end);
				set_rule(value, Memory,
					 get_sleb128(&ptr.p8, end), state);
				break;
			case DW_CFA_val_offset_sf:
				value = get_uleb128(&ptr.p8, end);
				set_rule(value, Value,
					 get_sleb128(&ptr.p8, end), state);
				break;
			case DW_CFA_restore_extended:
				unw_debug("cfa_restore_extended: ");
			case DW_CFA_undefined:
				unw_debug("cfa_undefined: ");
			case DW_CFA_same_value:
				unw_debug("cfa_same_value: ");
				set_rule(get_uleb128(&ptr.p8, end), Nowhere, 0,
					 state);
				break;
			case DW_CFA_register:
				unw_debug("cfa_register: ");
				value = get_uleb128(&ptr.p8, end);
				set_rule(value,
					 Register,
					 get_uleb128(&ptr.p8, end), state);
				break;
			case DW_CFA_remember_state:
				unw_debug("cfa_remember_state: ");
				if (ptr.p8 == state->label) {
					state->label = NULL;
					return 1;
				}
				if (state->stackDepth >= MAX_STACK_DEPTH)
					return 0;
				state->stack[state->stackDepth++] = ptr.p8;
				break;
			case DW_CFA_restore_state:
				unw_debug("cfa_restore_state: ");
				if (state->stackDepth) {
					const uleb128_t loc = state->loc;
					const u8 *label = state->label;

					state->label =
					    state->stack[state->stackDepth - 1];
					memcpy(&state->cfa, &badCFA,
					       sizeof(state->cfa));
					memset(state->regs, 0,
					       sizeof(state->regs));
					state->stackDepth = 0;
					result =
					    processCFI(start, end, 0, ptrType,
						       state);
					state->loc = loc;
					state->label = label;
				} else
					return 0;
				break;
			case DW_CFA_def_cfa:
				state->cfa.reg = get_uleb128(&ptr.p8, end);
				unw_debug("cfa_def_cfa: r%lu ", state->cfa.reg);
				/*nobreak*/
			case DW_CFA_def_cfa_offset:
				state->cfa.offs = get_uleb128(&ptr.p8, end);
				unw_debug("cfa_def_cfa_offset: 0x%lx ",
					  state->cfa.offs);
				break;
			case DW_CFA_def_cfa_sf:
				state->cfa.reg = get_uleb128(&ptr.p8, end);
				/*nobreak */
			case DW_CFA_def_cfa_offset_sf:
				state->cfa.offs = get_sleb128(&ptr.p8, end)
				    * state->dataAlign;
				break;
			case DW_CFA_def_cfa_register:
				unw_debug("cfa_def_cfa_regsiter: ");
				state->cfa.reg = get_uleb128(&ptr.p8, end);
				break;
				/*todo case DW_CFA_def_cfa_expression: */
				/*todo case DW_CFA_expression: */
				/*todo case DW_CFA_val_expression: */
			case DW_CFA_GNU_args_size:
				get_uleb128(&ptr.p8, end);
				break;
			case DW_CFA_GNU_negative_offset_extended:
				value = get_uleb128(&ptr.p8, end);
				set_rule(value,
					 Memory,
					 (uleb128_t) 0 - get_uleb128(&ptr.p8,
								     end),
					 state);
				break;
			case DW_CFA_GNU_window_save:
			default:
				unw_debug("UNKNOW OPCODE 0x%x\n", opcode);
				result = 0;
				break;
			}
			break;
		case 1:
			unw_debug("\ncfa_adv_loc: ");
			result = advance_loc(*ptr.p8++ & 0x3f, state);
			break;
		case 2:
			unw_debug("cfa_offset: ");
			value = *ptr.p8++ & 0x3f;
			set_rule(value, Memory, get_uleb128(&ptr.p8, end),
				 state);
			break;
		case 3:
			unw_debug("cfa_restore: ");
			set_rule(*ptr.p8++ & 0x3f, Nowhere, 0, state);
			break;
		}

		if (ptr.p8 > end)
			result = 0;
		if (result && targetLoc != 0 && targetLoc < state->loc)
			return 1;
	}

	return result && ptr.p8 == end && (targetLoc == 0 || (
		/*todo While in theory this should apply, gcc in practice omits
		  everything past the function prolog, and hence the location
		  never reaches the end of the function.
		targetLoc < state->loc && */  state->label == NULL));
}

/* Unwind to previous to frame.  Returns 0 if successful, negative
 * number in case of an error. */
int arc_unwind(struct unwind_frame_info *frame)
{
#define FRAME_REG(r, t) (((t *)frame)[reg_info[r].offs])
	const u32 *fde = NULL, *cie = NULL;
	const u8 *ptr = NULL, *end = NULL;
	unsigned long pc = UNW_PC(frame) - frame->call_frame;
	unsigned long startLoc = 0, endLoc = 0, cfa;
	unsigned i;
	signed ptrType = -1;
	uleb128_t retAddrReg = 0;
	const struct unwind_table *table;
	struct unwind_state state;
	unsigned long *fptr;
	unsigned long addr;

	unw_debug("\n\nUNWIND FRAME:\n");
	unw_debug("PC: 0x%lx BLINK: 0x%lx, SP: 0x%lx, FP: 0x%x\n",
		  UNW_PC(frame), UNW_BLINK(frame), UNW_SP(frame),
		  UNW_FP(frame));

	if (UNW_PC(frame) == 0)
		return -EINVAL;

#ifdef UNWIND_DEBUG
	{
		unsigned long *sptr = (unsigned long *)UNW_SP(frame);
		unw_debug("\nStack Dump:\n");
		for (i = 0; i < 20; i++, sptr++)
			unw_debug("0x%p:  0x%lx\n", sptr, *sptr);
		unw_debug("\n");
	}
#endif

	table = find_table(pc);
	if (table != NULL
	    && !(table->size & (sizeof(*fde) - 1))) {
		const u8 *hdr = table->header;
		unsigned long tableSize;

		smp_rmb();
		if (hdr && hdr[0] == 1) {
			switch (hdr[3] & DW_EH_PE_FORM) {
			case DW_EH_PE_native:
				tableSize = sizeof(unsigned long);
				break;
			case DW_EH_PE_data2:
				tableSize = 2;
				break;
			case DW_EH_PE_data4:
				tableSize = 4;
				break;
			case DW_EH_PE_data8:
				tableSize = 8;
				break;
			default:
				tableSize = 0;
				break;
			}
			ptr = hdr + 4;
			end = hdr + table->hdrsz;
			if (tableSize && read_pointer(&ptr, end, hdr[1])
			    == (unsigned long)table->address
			    && (i = read_pointer(&ptr, end, hdr[2])) > 0
			    && i == (end - ptr) / (2 * tableSize)
			    && !((end - ptr) % (2 * tableSize))) {
				do {
					const u8 *cur =
					    ptr + (i / 2) * (2 * tableSize);

					startLoc = read_pointer(&cur,
								cur + tableSize,
								hdr[3]);
					if (pc < startLoc)
						i /= 2;
					else {
						ptr = cur - tableSize;
						i = (i + 1) / 2;
					}
				} while (startLoc && i > 1);
				if (i == 1
				    && (startLoc = read_pointer(&ptr,
								ptr + tableSize,
								hdr[3])) != 0
				    && pc >= startLoc)
					fde = (void *)read_pointer(&ptr,
								   ptr +
								   tableSize,
								   hdr[3]);
			}
		}

		if (fde != NULL) {
			cie = cie_for_fde(fde, table);
			ptr = (const u8 *)(fde + 2);
			if (cie != NULL
			    && cie != &bad_cie
			    && cie != &not_fde
			    && (ptrType = fde_pointer_type(cie)) >= 0
			    && read_pointer(&ptr,
					    (const u8 *)(fde + 1) + *fde,
					    ptrType) == startLoc) {
				if (!(ptrType & DW_EH_PE_indirect))
					ptrType &=
					    DW_EH_PE_FORM | DW_EH_PE_signed;
				endLoc =
				    startLoc + read_pointer(&ptr,
							    (const u8 *)(fde +
									 1) +
							    *fde, ptrType);
				if (pc >= endLoc)
					fde = NULL;
			} else
				fde = NULL;
		}
		if (fde == NULL) {
			for (fde = table->address, tableSize = table->size;
			     cie = NULL, tableSize > sizeof(*fde)
			     && tableSize - sizeof(*fde) >= *fde;
			     tableSize -= sizeof(*fde) + *fde,
			     fde += 1 + *fde / sizeof(*fde)) {
				cie = cie_for_fde(fde, table);
				if (cie == &bad_cie) {
					cie = NULL;
					break;
				}
				if (cie == NULL
				    || cie == &not_fde
				    || (ptrType = fde_pointer_type(cie)) < 0)
					continue;
				ptr = (const u8 *)(fde + 2);
				startLoc = read_pointer(&ptr,
							(const u8 *)(fde + 1) +
							*fde, ptrType);
				if (!startLoc)
					continue;
				if (!(ptrType & DW_EH_PE_indirect))
					ptrType &=
					    DW_EH_PE_FORM | DW_EH_PE_signed;
				endLoc =
				    startLoc + read_pointer(&ptr,
							    (const u8 *)(fde +
									 1) +
							    *fde, ptrType);
				if (pc >= startLoc && pc < endLoc)
					break;
			}
		}
	}
	if (cie != NULL) {
		memset(&state, 0, sizeof(state));
		state.cieEnd = ptr;	/* keep here temporarily */
		ptr = (const u8 *)(cie + 2);
		end = (const u8 *)(cie + 1) + *cie;
		frame->call_frame = 1;
		if ((state.version = *ptr) != 1)
			cie = NULL;	/* unsupported version */
		else if (*++ptr) {
			/* check if augmentation size is first (thus present) */
			if (*ptr == 'z') {
				while (++ptr < end && *ptr) {
					switch (*ptr) {
					/* chk for ignorable or already handled
					 * nul-terminated augmentation string */
					case 'L':
					case 'P':
					case 'R':
						continue;
					case 'S':
						frame->call_frame = 0;
						continue;
					default:
						break;
					}
					break;
				}
			}
			if (ptr >= end || *ptr)
				cie = NULL;
		}
		++ptr;
	}
	if (cie != NULL) {
		/* get code aligment factor */
		state.codeAlign = get_uleb128(&ptr, end);
		/* get data aligment factor */
		state.dataAlign = get_sleb128(&ptr, end);
		if (state.codeAlign == 0 || state.dataAlign == 0 || ptr >= end)
			cie = NULL;
		else {
			retAddrReg =
			    state.version <= 1 ? *ptr++ : get_uleb128(&ptr,
								      end);
			unw_debug("CIE Frame Info:\n");
			unw_debug("return Address register 0x%lx\n",
				  retAddrReg);
			unw_debug("data Align: %ld\n", state.dataAlign);
			unw_debug("code Align: %lu\n", state.codeAlign);
			/* skip augmentation */
			if (((const char *)(cie + 2))[1] == 'z') {
				uleb128_t augSize = get_uleb128(&ptr, end);

				ptr += augSize;
			}
			if (ptr > end || retAddrReg >= ARRAY_SIZE(reg_info)
			    || REG_INVALID(retAddrReg)
			    || reg_info[retAddrReg].width !=
			    sizeof(unsigned long))
				cie = NULL;
		}
	}
	if (cie != NULL) {
		state.cieStart = ptr;
		ptr = state.cieEnd;
		state.cieEnd = end;
		end = (const u8 *)(fde + 1) + *fde;
		/* skip augmentation */
		if (((const char *)(cie + 2))[1] == 'z') {
			uleb128_t augSize = get_uleb128(&ptr, end);

			if ((ptr += augSize) > end)
				fde = NULL;
		}
	}
	if (cie == NULL || fde == NULL) {
#ifdef CONFIG_FRAME_POINTER
		unsigned long top, bottom;

		top = STACK_TOP_UNW(frame->task);
		bottom = STACK_BOTTOM_UNW(frame->task);
#if FRAME_RETADDR_OFFSET < 0
		if (UNW_SP(frame) < top && UNW_FP(frame) <= UNW_SP(frame)
		    && bottom < UNW_FP(frame)
#else
		if (UNW_SP(frame) > top && UNW_FP(frame) >= UNW_SP(frame)
		    && bottom > UNW_FP(frame)
#endif
		    && !((UNW_SP(frame) | UNW_FP(frame))
			 & (sizeof(unsigned long) - 1))) {
			unsigned long link;

			if (!__get_user(link, (unsigned long *)
					(UNW_FP(frame) + FRAME_LINK_OFFSET))
#if FRAME_RETADDR_OFFSET < 0
			    && link > bottom && link < UNW_FP(frame)
#else
			    && link > UNW_FP(frame) && link < bottom
#endif
			    && !(link & (sizeof(link) - 1))
			    && !__get_user(UNW_PC(frame),
					   (unsigned long *)(UNW_FP(frame)
						+ FRAME_RETADDR_OFFSET)))
			{
				UNW_SP(frame) =
				    UNW_FP(frame) + FRAME_RETADDR_OFFSET
#if FRAME_RETADDR_OFFSET < 0
				    -
#else
				    +
#endif
				    sizeof(UNW_PC(frame));
				UNW_FP(frame) = link;
				return 0;
			}
		}
#endif
		return -ENXIO;
	}
	state.org = startLoc;
	memcpy(&state.cfa, &badCFA, sizeof(state.cfa));

	unw_debug("\nProcess instructions\n");

	/* process instructions
	 * For ARC, we optimize by having blink(retAddrReg) with
	 * the sameValue in the leaf function, so we should not check
	 * state.regs[retAddrReg].where == Nowhere
	 */
	if (!processCFI(ptr, end, pc, ptrType, &state)
	    || state.loc > endLoc
/*	   || state.regs[retAddrReg].where == Nowhere */
	    || state.cfa.reg >= ARRAY_SIZE(reg_info)
	    || reg_info[state.cfa.reg].width != sizeof(unsigned long)
	    || state.cfa.offs % sizeof(unsigned long))
		return -EIO;

#ifdef UNWIND_DEBUG
	unw_debug("\n");

	unw_debug("\nRegister State Based on the rules parsed from FDE:\n");
	for (i = 0; i < ARRAY_SIZE(state.regs); ++i) {

		if (REG_INVALID(i))
			continue;

		switch (state.regs[i].where) {
		case Nowhere:
			break;
		case Memory:
			unw_debug(" r%d: c(%lu),", i, state.regs[i].value);
			break;
		case Register:
			unw_debug(" r%d: r(%lu),", i, state.regs[i].value);
			break;
		case Value:
			unw_debug(" r%d: v(%lu),", i, state.regs[i].value);
			break;
		}
	}

	unw_debug("\n");
#endif

	/* update frame */
#ifndef CONFIG_AS_CFI_SIGNAL_FRAME
	if (frame->call_frame
	    && !UNW_DEFAULT_RA(state.regs[retAddrReg], state.dataAlign))
		frame->call_frame = 0;
#endif
	cfa = FRAME_REG(state.cfa.reg, unsigned long) + state.cfa.offs;
	startLoc = min_t(unsigned long, UNW_SP(frame), cfa);
	endLoc = max_t(unsigned long, UNW_SP(frame), cfa);
	if (STACK_LIMIT(startLoc) != STACK_LIMIT(endLoc)) {
		startLoc = min(STACK_LIMIT(cfa), cfa);
		endLoc = max(STACK_LIMIT(cfa), cfa);
	}

	unw_debug("\nCFA reg: 0x%lx, offset: 0x%lx =>  0x%lx\n",
		  state.cfa.reg, state.cfa.offs, cfa);

	for (i = 0; i < ARRAY_SIZE(state.regs); ++i) {
		if (REG_INVALID(i)) {
			if (state.regs[i].where == Nowhere)
				continue;
			return -EIO;
		}
		switch (state.regs[i].where) {
		default:
			break;
		case Register:
			if (state.regs[i].value >= ARRAY_SIZE(reg_info)
			    || REG_INVALID(state.regs[i].value)
			    || reg_info[i].width >
			    reg_info[state.regs[i].value].width)
				return -EIO;
			switch (reg_info[state.regs[i].value].width) {
			case sizeof(u8):
				state.regs[i].value =
				FRAME_REG(state.regs[i].value, const u8);
				break;
			case sizeof(u16):
				state.regs[i].value =
				FRAME_REG(state.regs[i].value, const u16);
				break;
			case sizeof(u32):
				state.regs[i].value =
				FRAME_REG(state.regs[i].value, const u32);
				break;
#ifdef CONFIG_64BIT
			case sizeof(u64):
				state.regs[i].value =
				FRAME_REG(state.regs[i].value, const u64);
				break;
#endif
			default:
				return -EIO;
			}
			break;
		}
	}

	unw_debug("\nRegister state after evaluation with realtime Stack:\n");
	fptr = (unsigned long *)(&frame->regs);
	for (i = 0; i < ARRAY_SIZE(state.regs); ++i, fptr++) {

		if (REG_INVALID(i))
			continue;
		switch (state.regs[i].where) {
		case Nowhere:
			if (reg_info[i].width != sizeof(UNW_SP(frame))
			    || &FRAME_REG(i, __typeof__(UNW_SP(frame)))
			    != &UNW_SP(frame))
				continue;
			UNW_SP(frame) = cfa;
			break;
		case Register:
			switch (reg_info[i].width) {
			case sizeof(u8):
				FRAME_REG(i, u8) = state.regs[i].value;
				break;
			case sizeof(u16):
				FRAME_REG(i, u16) = state.regs[i].value;
				break;
			case sizeof(u32):
				FRAME_REG(i, u32) = state.regs[i].value;
				break;
#ifdef CONFIG_64BIT
			case sizeof(u64):
				FRAME_REG(i, u64) = state.regs[i].value;
				break;
#endif
			default:
				return -EIO;
			}
			break;
		case Value:
			if (reg_info[i].width != sizeof(unsigned long))
				return -EIO;
			FRAME_REG(i, unsigned long) = cfa + state.regs[i].value
			    * state.dataAlign;
			break;
		case Memory:
			addr = cfa + state.regs[i].value * state.dataAlign;

			if ((state.regs[i].value * state.dataAlign)
			    % sizeof(unsigned long)
			    || addr < startLoc
			    || addr + sizeof(unsigned long) < addr
			    || addr + sizeof(unsigned long) > endLoc)
					return -EIO;

			switch (reg_info[i].width) {
			case sizeof(u8):
				__get_user(FRAME_REG(i, u8),
					   (u8 __user *)addr);
				break;
			case sizeof(u16):
				__get_user(FRAME_REG(i, u16),
					   (u16 __user *)addr);
				break;
			case sizeof(u32):
				__get_user(FRAME_REG(i, u32),
					   (u32 __user *)addr);
				break;
#ifdef CONFIG_64BIT
			case sizeof(u64):
				__get_user(FRAME_REG(i, u64),
					   (u64 __user *)addr);
				break;
#endif
			default:
				return -EIO;
			}

			break;
		}
		unw_debug("r%d: 0x%lx ", i, *fptr);
	}

	return 0;
#undef FRAME_REG
}
EXPORT_SYMBOL(arc_unwind);

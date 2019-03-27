/*-
 * Copyright (c) 2007 John Birrell (jb@freebsd.org)
 * Copyright (c) 2009-2014 Kai Wang
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: _libdwarf.h 3298 2016-01-09 15:43:31Z jkoshy $
 */

#ifndef	__LIBDWARF_H_
#define	__LIBDWARF_H_

#include <sys/param.h>
#include <sys/queue.h>
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gelf.h>
#include "dwarf.h"
#include "libdwarf.h"
#include "uthash.h"

#include "_elftc.h"

#define DWARF_DIE_HASH_SIZE		8191

struct _libdwarf_globals {
	Dwarf_Handler	errhand;
	Dwarf_Ptr	errarg;
	int		applyreloc;
};

extern struct _libdwarf_globals _libdwarf;

#define	_DWARF_SET_ERROR(_d, _e, _err, _elf_err)			\
	_dwarf_set_error(_d, _e, _err, _elf_err, __func__, __LINE__)
#define	DWARF_SET_ERROR(_d, _e, _err)					\
	_DWARF_SET_ERROR(_d, _e, _err, 0)
#define	DWARF_SET_ELF_ERROR(_d, _e)					\
	_DWARF_SET_ERROR(_d, _e, DW_DLE_ELF, elf_errno())

/*
 * Convenient macros for producer bytes stream generation.
 */
#define	WRITE_VALUE(value, bytes)					\
	dbg->write_alloc(&ds->ds_data, &ds->ds_cap, &ds->ds_size,	\
	    (value), (bytes), error)
#define	WRITE_ULEB128(value)						\
	_dwarf_write_uleb128_alloc(&ds->ds_data, &ds->ds_cap,		\
	    &ds->ds_size, (value), error)
#define	WRITE_SLEB128(value)						\
	_dwarf_write_sleb128_alloc(&ds->ds_data, &ds->ds_cap,		\
	    &ds->ds_size, (value), error)
#define	WRITE_STRING(string)						\
	_dwarf_write_string_alloc(&ds->ds_data, &ds->ds_cap,		\
	    &ds->ds_size, (string), error)
#define	WRITE_BLOCK(blk, size)						\
	_dwarf_write_block_alloc(&ds->ds_data, &ds->ds_cap,		\
	    &ds->ds_size, (blk), (size), error)
#define	WRITE_PADDING(byte, cnt)					\
	_dwarf_write_padding_alloc(&ds->ds_data, &ds->ds_cap,		\
	    &ds->ds_size, (byte), (cnt), error)
#define	RCHECK(expr)							\
	do {								\
		ret = expr;						\
		if (ret != DW_DLE_NONE)					\
			goto gen_fail;					\
	} while(0)

typedef struct _Dwarf_CU *Dwarf_CU;

struct _Dwarf_AttrDef {
	Dwarf_Half	ad_attrib;		/* DW_AT_XXX */
	Dwarf_Half	ad_form;		/* DW_FORM_XXX */
	uint64_t	ad_offset;		/* Offset in abbrev section. */
	STAILQ_ENTRY(_Dwarf_AttrDef) ad_next;	/* Next attribute define. */
};

struct _Dwarf_Attribute {
	Dwarf_Die		at_die;		/* Ptr to containing DIE. */
	Dwarf_Die		at_refdie;	/* Ptr to reference DIE. */
	uint64_t		at_offset;	/* Offset in info section. */
	Dwarf_Half		at_attrib;	/* DW_AT_XXX */
	Dwarf_Half		at_form;	/* DW_FORM_XXX */
	int			at_indirect;	/* Has indirect form. */
	union {
		uint64_t	u64;		/* Unsigned value. */
		int64_t		s64;		/* Signed value. */
		char		*s;   		/* String. */
		uint8_t		*u8p;		/* Block data. */
	} u[2];					/* Value. */
	Dwarf_Block		at_block;	/* Block. */
	Dwarf_Locdesc		*at_ld;		/* at value is locdesc. */
	Dwarf_P_Expr		at_expr;	/* at value is expr. */
	uint64_t		at_relsym;	/* Relocation symbol index. */
	const char		*at_relsec;	/* Rel. to dwarf section. */
	STAILQ_ENTRY(_Dwarf_Attribute) at_next;	/* Next attribute. */
};

struct _Dwarf_Abbrev {
	uint64_t	ab_entry;	/* Abbrev entry. */
	uint64_t	ab_tag;		/* Tag: DW_TAG_ */
	uint8_t		ab_children;	/* DW_CHILDREN_no or DW_CHILDREN_yes */
	uint64_t	ab_offset;	/* Offset in abbrev section. */
	uint64_t	ab_length;	/* Length of this abbrev entry. */
	uint64_t	ab_atnum;	/* Number of attribute defines. */
	UT_hash_handle	ab_hh;		/* Uthash handle. */
	STAILQ_HEAD(, _Dwarf_AttrDef) ab_attrdef; /* List of attribute defs. */
};

struct _Dwarf_Die {
	Dwarf_Die	die_parent;	/* Parent DIE. */
	Dwarf_Die	die_child;	/* First child DIE. */
	Dwarf_Die	die_left;	/* Left sibling DIE. */
	Dwarf_Die	die_right;	/* Right sibling DIE. */
	uint64_t	die_offset;	/* DIE offset in section. */
	uint64_t	die_next_off;	/* Next DIE offset in section. */
	uint64_t	die_abnum;	/* Abbrev number. */
	Dwarf_Abbrev	die_ab;		/* Abbrev pointer. */
	Dwarf_Tag	die_tag;	/* DW_TAG_ */
	Dwarf_Debug	die_dbg;	/* Dwarf_Debug pointer. */
	Dwarf_CU	die_cu;		/* Compilation unit pointer. */
	char		*die_name;	/* Ptr to the name string. */
	Dwarf_Attribute	*die_attrarray;	/* Array of attributes. */
	STAILQ_HEAD(, _Dwarf_Attribute)	die_attr; /* List of attributes. */
	STAILQ_ENTRY(_Dwarf_Die) die_pro_next; /* Next die in pro-die list. */
};

struct _Dwarf_P_Expr_Entry {
	Dwarf_Loc	ee_loc;		/* Location expression. */
	Dwarf_Unsigned	ee_sym;		/* Optional related reloc sym index. */
	STAILQ_ENTRY(_Dwarf_P_Expr_Entry) ee_next; /* Next entry in list. */
};

struct _Dwarf_P_Expr {
	Dwarf_Debug	pe_dbg;		/* Dwarf_Debug pointer. */
	uint8_t		*pe_block;	/* Expression block data. */
	int		pe_invalid;	/* Block data is up-to-date or not. */
	Dwarf_Unsigned	pe_length;	/* Length of the block. */
	STAILQ_HEAD(, _Dwarf_P_Expr_Entry) pe_eelist; /* List of entries. */
	STAILQ_ENTRY(_Dwarf_P_Expr) pe_next; /* Next expr in list. */
};

struct _Dwarf_Line {
	Dwarf_LineInfo	ln_li;		/* Ptr to line info. */
	Dwarf_Addr	ln_addr;	/* Line address. */
	Dwarf_Unsigned	ln_symndx;	/* Symbol index for relocation. */
	Dwarf_Unsigned	ln_fileno;	/* File number. */
	Dwarf_Unsigned	ln_lineno;	/* Line number. */
	Dwarf_Signed	ln_column;	/* Column number. */
	Dwarf_Bool	ln_bblock;	/* Basic block flag. */
	Dwarf_Bool	ln_stmt;	/* Begin statement flag. */
	Dwarf_Bool	ln_endseq;	/* End sequence flag. */
	STAILQ_ENTRY(_Dwarf_Line) ln_next; /* Next line in list. */
};

struct _Dwarf_LineFile {
	char		*lf_fname;	/* Filename. */
	char		*lf_fullpath;	/* Full pathname of the file. */
	Dwarf_Unsigned	lf_dirndx;	/* Dir index. */
	Dwarf_Unsigned	lf_mtime;	/* Modification time. */
	Dwarf_Unsigned	lf_size;	/* File size. */
	STAILQ_ENTRY(_Dwarf_LineFile) lf_next; /* Next file in list. */
};

struct _Dwarf_LineInfo {
	Dwarf_Unsigned	li_length;	/* Length of line info data. */
	Dwarf_Half	li_version;	/* Version of line info. */
	Dwarf_Unsigned	li_hdrlen;	/* Length of line info header. */
	Dwarf_Small	li_minlen;	/* Minimum instrutction length. */
	Dwarf_Small	li_maxop;	/* Maximum operations per inst. */
	Dwarf_Small	li_defstmt;	/* Default value of is_stmt. */
	int8_t		li_lbase;    	/* Line base for special opcode. */
	Dwarf_Small	li_lrange;    	/* Line range for special opcode. */
	Dwarf_Small	li_opbase;	/* Fisrt std opcode number. */
	Dwarf_Small	*li_oplen;	/* Array of std opcode len. */
	char		**li_incdirs;	/* Array of include dirs. */
	Dwarf_Unsigned	li_inclen;	/* Length of inc dir array. */
	char		**li_lfnarray;	/* Array of file names. */
	Dwarf_Unsigned	li_lflen;	/* Length of filename array. */
	STAILQ_HEAD(, _Dwarf_LineFile) li_lflist; /* List of files. */
	Dwarf_Line	*li_lnarray;	/* Array of lines. */
	Dwarf_Unsigned	li_lnlen;	/* Length of the line array. */
	STAILQ_HEAD(, _Dwarf_Line) li_lnlist; /* List of lines. */
};

struct _Dwarf_NamePair {
	Dwarf_NameTbl	np_nt;		/* Ptr to containing name table. */
	Dwarf_Die	np_die;		/* Ptr to Ref. Die. */
	Dwarf_Unsigned	np_offset;	/* Offset in CU. */
	char		*np_name;	/* Object/Type name. */
	STAILQ_ENTRY(_Dwarf_NamePair) np_next; /* Next pair in the list. */
};

struct _Dwarf_NameTbl {
	Dwarf_Unsigned	nt_length;	/* Name lookup table length. */
	Dwarf_Half	nt_version;	/* Name lookup table version. */
	Dwarf_CU	nt_cu;		/* Ptr to Ref. CU. */
	Dwarf_Off	nt_cu_offset;	/* Ref. CU offset in .debug_info */
	Dwarf_Unsigned	nt_cu_length;	/* Ref. CU length. */
	STAILQ_HEAD(, _Dwarf_NamePair) nt_nplist; /* List of offset+name pairs. */
	STAILQ_ENTRY(_Dwarf_NameTbl) nt_next; /* Next name table in the list. */
};

struct _Dwarf_NameSec {
	STAILQ_HEAD(, _Dwarf_NameTbl) ns_ntlist; /* List of name tables. */
	Dwarf_NamePair	*ns_array;	/* Array of pairs of all tables. */
	Dwarf_Unsigned	ns_len;		/* Length of the pair array. */
};

struct _Dwarf_Fde {
	Dwarf_Debug	fde_dbg;	/* Ptr to containing dbg. */
	Dwarf_Cie	fde_cie;	/* Ptr to associated CIE. */
	Dwarf_FrameSec	fde_fs;		/* Ptr to containing .debug_frame. */
	Dwarf_Ptr	fde_addr;	/* Ptr to start of the FDE. */
	Dwarf_Unsigned	fde_offset;	/* Offset of the FDE. */
	Dwarf_Unsigned	fde_length;	/* Length of the FDE. */
	Dwarf_Unsigned	fde_cieoff;	/* Offset of associated CIE. */
	Dwarf_Unsigned	fde_initloc;	/* Initial location. */
	Dwarf_Unsigned	fde_adrange;	/* Address range. */
	Dwarf_Unsigned	fde_auglen;	/* Augmentation length. */
	uint8_t		*fde_augdata;	/* Augmentation data. */
	uint8_t		*fde_inst;	/* Instructions. */
	Dwarf_Unsigned	fde_instlen;	/* Length of instructions. */
	Dwarf_Unsigned	fde_instcap;	/* Capacity of inst buffer. */
	Dwarf_Unsigned	fde_symndx;	/* Symbol index for relocation. */
	Dwarf_Unsigned	fde_esymndx;	/* End symbol index for relocation. */
	Dwarf_Addr	fde_eoff;	/* Offset from the end symbol. */
	STAILQ_ENTRY(_Dwarf_Fde) fde_next; /* Next FDE in list. */
};

struct _Dwarf_Cie {
	Dwarf_Debug	cie_dbg;	/* Ptr to containing dbg. */
	Dwarf_Unsigned	cie_index;	/* Index of the CIE. */
	Dwarf_Unsigned	cie_offset;	/* Offset of the CIE. */
	Dwarf_Unsigned	cie_length;	/* Length of the CIE. */
	Dwarf_Half	cie_version;	/* CIE version. */
	uint8_t		*cie_augment;	/* CIE augmentation (UTF-8). */
	Dwarf_Unsigned	cie_ehdata;	/* Optional EH Data. */
	uint8_t		cie_addrsize;	/* Address size. (DWARF4) */
	uint8_t		cie_segmentsize; /* Segment size. (DWARF4) */
	Dwarf_Unsigned	cie_caf;	/* Code alignment factor. */
	Dwarf_Signed	cie_daf;	/* Data alignment factor. */
	Dwarf_Unsigned	cie_ra;		/* Return address register. */
	Dwarf_Unsigned	cie_auglen;	/* Augmentation length. */
	uint8_t		*cie_augdata;	/* Augmentation data; */
	uint8_t		cie_fde_encode; /* FDE PC start/range encode. */
	Dwarf_Ptr	cie_initinst;	/* Initial instructions. */
	Dwarf_Unsigned	cie_instlen;	/* Length of init instructions. */
	STAILQ_ENTRY(_Dwarf_Cie) cie_next;  /* Next CIE in list. */
};

struct _Dwarf_FrameSec {
	STAILQ_HEAD(, _Dwarf_Cie) fs_cielist; /* List of CIE. */
	STAILQ_HEAD(, _Dwarf_Fde) fs_fdelist; /* List of FDE. */
	Dwarf_Cie	*fs_ciearray;	/* Array of CIE. */
	Dwarf_Unsigned	fs_cielen;	/* Length of CIE array. */
	Dwarf_Fde	*fs_fdearray;	/* Array of FDE.*/
	Dwarf_Unsigned	fs_fdelen;	/* Length of FDE array. */
};

struct _Dwarf_Arange {
	Dwarf_ArangeSet	ar_as;		/* Ptr to the set it belongs to. */
	Dwarf_Unsigned	ar_address;	/* Start PC. */
	Dwarf_Unsigned	ar_range;	/* PC range. */
	Dwarf_Unsigned	ar_symndx;	/* First symbol index for reloc. */
	Dwarf_Unsigned	ar_esymndx;	/* Second symbol index for reloc. */
	Dwarf_Addr	ar_eoff;	/* Offset from second symbol. */
	STAILQ_ENTRY(_Dwarf_Arange) ar_next; /* Next arange in list. */
};

struct _Dwarf_ArangeSet {
	Dwarf_Unsigned	as_length;	/* Length of the arange set. */
	Dwarf_Half	as_version;	/* Version of the arange set. */
	Dwarf_Off	as_cu_offset;	/* Offset of associated CU. */
	Dwarf_CU	as_cu;		/* Ptr to associated CU. */
	Dwarf_Small	as_addrsz;	/* Target address size. */
	Dwarf_Small	as_segsz;	/* Target segment size. */
	STAILQ_HEAD (, _Dwarf_Arange) as_arlist; /* List of ae entries. */
	STAILQ_ENTRY(_Dwarf_ArangeSet) as_next; /* Next set in list. */
};

struct _Dwarf_MacroSet {
	Dwarf_Macro_Details *ms_mdlist; /* Array of macinfo entries. */
	Dwarf_Unsigned	ms_cnt;		/* Length of the array. */
	STAILQ_ENTRY(_Dwarf_MacroSet) ms_next; /* Next set in list. */
};

struct _Dwarf_Rangelist {
	Dwarf_CU	rl_cu;		/* Ptr to associated CU. */
	Dwarf_Unsigned	rl_offset;	/* Offset of the rangelist. */
	Dwarf_Ranges	*rl_rgarray;	/* Array of ranges. */
	Dwarf_Unsigned	rl_rglen;	/* Length of the ranges array. */
	STAILQ_ENTRY(_Dwarf_Rangelist) rl_next; /* Next rangelist in list. */
};

struct _Dwarf_CU {
	Dwarf_Debug	cu_dbg;		/* Ptr to containing dbg. */
	Dwarf_Off	cu_offset;	/* Offset to the this CU. */
	uint32_t	cu_length;	/* Length of CU data. */
	uint16_t	cu_length_size; /* Size in bytes of the length field. */
	uint16_t	cu_version;	/* DWARF version. */
	uint64_t	cu_abbrev_offset; /* Offset into .debug_abbrev. */
	uint64_t	cu_abbrev_offset_cur; /* Current abbrev offset. */
	int		cu_abbrev_loaded; /* Abbrev table parsed. */
	uint64_t	cu_abbrev_cnt;	/* Abbrev entry count. */
	uint64_t	cu_lineno_offset; /* Offset into .debug_lineno. */
	uint8_t		cu_pointer_size;/* Number of bytes in pointer. */
	uint8_t		cu_dwarf_size;	/* CU section dwarf size. */
	Dwarf_Sig8	cu_type_sig;	/* Type unit's signature. */
	uint64_t	cu_type_offset; /* Type unit's type offset. */
	Dwarf_Off	cu_next_offset; /* Offset to the next CU. */
	uint64_t	cu_1st_offset;	/* First DIE offset. */
	int		cu_pass2;	/* Two pass DIE traverse. */
	Dwarf_LineInfo	cu_lineinfo;	/* Ptr to Dwarf_LineInfo. */
	Dwarf_Abbrev	cu_abbrev_hash; /* Abbrev hash table. */
	Dwarf_Bool	cu_is_info;	/* Compilation/type unit flag. */
	STAILQ_ENTRY(_Dwarf_CU) cu_next; /* Next compilation unit. */
};

typedef struct _Dwarf_Section {
	const char	*ds_name;	/* Section name. */
	Dwarf_Small	*ds_data;	/* Section data. */
	Dwarf_Unsigned	ds_addr;	/* Section virtual addr. */
	Dwarf_Unsigned	ds_size;	/* Section size. */
} Dwarf_Section;

typedef struct _Dwarf_P_Section {
	char		*ds_name;	/* Section name. */
	Dwarf_Small	*ds_data;	/* Section data. */
	Dwarf_Unsigned	ds_size;	/* Section size. */
	Dwarf_Unsigned	ds_cap;		/* Section capacity. */
	Dwarf_Unsigned	ds_ndx;		/* ELF section index. */
	Dwarf_Unsigned	ds_symndx;	/* Section symbol index. (for reloc) */
	STAILQ_ENTRY(_Dwarf_P_Section) ds_next; /* Next section in the list. */
} *Dwarf_P_Section;

typedef struct _Dwarf_Rel_Entry {
	unsigned char	dre_type;	/* Reloc type. */
	unsigned char	dre_length;	/* Reloc storage unit length. */
	Dwarf_Unsigned	dre_offset;	/* Reloc storage unit offset. */
	Dwarf_Unsigned	dre_addend;	/* Reloc addend. */
	Dwarf_Unsigned	dre_symndx;	/* Reloc symbol index. */
	const char	*dre_secname;	/* Refer to some debug section. */
	STAILQ_ENTRY(_Dwarf_Rel_Entry) dre_next; /* Next reloc entry. */
} *Dwarf_Rel_Entry;

typedef struct _Dwarf_Rel_Section {
	struct _Dwarf_P_Section *drs_ds; /* Ptr to actual reloc ELF section. */
	struct _Dwarf_P_Section *drs_ref; /* Which debug section it refers. */
	struct Dwarf_Relocation_Data_s *drs_drd; /* Reloc data array. */
	STAILQ_HEAD(, _Dwarf_Rel_Entry) drs_dre; /* Reloc entry list. */
	Dwarf_Unsigned	drs_drecnt;	/* Count of entries. */
	Dwarf_Unsigned	drs_size;	/* Size of ELF section in bytes. */
	int		drs_addend;	/* Elf_Rel or Elf_Rela */
	STAILQ_ENTRY(_Dwarf_Rel_Section) drs_next; /* Next reloc section. */
} *Dwarf_Rel_Section;

typedef struct {
	Elf_Data *ed_data;
	void *ed_alloc;
} Dwarf_Elf_Data;

typedef struct {
	Elf		*eo_elf;
	GElf_Ehdr	eo_ehdr;
	GElf_Shdr	*eo_shdr;
	Dwarf_Elf_Data	*eo_data;
	Dwarf_Unsigned	eo_seccnt;
	size_t		eo_strndx;
	Dwarf_Obj_Access_Methods eo_methods;
} Dwarf_Elf_Object;

struct _Dwarf_Debug {
	Dwarf_Obj_Access_Interface *dbg_iface;
	Dwarf_Section	*dbg_section;	/* Dwarf section list. */
	Dwarf_Section	*dbg_info_sec;	/* Pointer to info section. */
	Dwarf_Off	dbg_info_off;	/* Current info section offset. */
	Dwarf_Section	*dbg_types_sec; /* Pointer to type section. */
	Dwarf_Off	dbg_types_off;	/* Current types section offset. */
	Dwarf_Unsigned	dbg_seccnt;	/* Total number of dwarf sections. */
	int		dbg_mode;	/* Access mode. */
	int		dbg_pointer_size; /* Object address size. */
	int		dbg_offset_size;  /* DWARF offset size. */
	int		dbg_info_loaded; /* Flag indicating all CU loaded. */
	int		dbg_types_loaded; /* Flag indicating all TU loaded. */
	Dwarf_Half	dbg_machine;	/* ELF machine architecture. */
	Dwarf_Handler	dbg_errhand;	/* Error handler. */
	Dwarf_Ptr	dbg_errarg;	/* Argument to the error handler. */
	STAILQ_HEAD(, _Dwarf_CU) dbg_cu;/* List of compilation units. */
	STAILQ_HEAD(, _Dwarf_CU) dbg_tu;/* List of type units. */
	Dwarf_CU	dbg_cu_current; /* Ptr to the current CU. */
	Dwarf_CU	dbg_tu_current; /* Ptr to the current TU. */
	Dwarf_NameSec	dbg_globals;	/* Ptr to pubnames lookup section. */
	Dwarf_NameSec	dbg_pubtypes;	/* Ptr to pubtypes lookup section. */
	Dwarf_NameSec	dbg_weaks;	/* Ptr to weaknames lookup section. */
	Dwarf_NameSec	dbg_funcs;	/* Ptr to static funcs lookup sect. */
	Dwarf_NameSec	dbg_vars;	/* Ptr to static vars lookup sect. */
	Dwarf_NameSec	dbg_types;	/* Ptr to types lookup section. */
	Dwarf_FrameSec	dbg_frame;	/* Ptr to .debug_frame section. */
	Dwarf_FrameSec	dbg_eh_frame;	/* Ptr to .eh_frame section. */
	STAILQ_HEAD(, _Dwarf_ArangeSet) dbg_aslist; /* List of arange set. */
	Dwarf_Arange	*dbg_arange_array; /* Array of arange. */
	Dwarf_Unsigned	dbg_arange_cnt;	/* Length of the arange array. */
	char		*dbg_strtab;	/* Dwarf string table. */
	Dwarf_Unsigned	dbg_strtab_cap; /* Dwarf string table capacity. */
	Dwarf_Unsigned	dbg_strtab_size; /* Dwarf string table size. */
	STAILQ_HEAD(, _Dwarf_MacroSet) dbg_mslist; /* List of macro set. */
	STAILQ_HEAD(, _Dwarf_Rangelist) dbg_rllist; /* List of rangelist. */
	uint64_t	(*read)(uint8_t *, uint64_t *, int);
	void		(*write)(uint8_t *, uint64_t *, uint64_t, int);
	int		(*write_alloc)(uint8_t **, uint64_t *, uint64_t *,
			    uint64_t, int, Dwarf_Error *);
	uint64_t	(*decode)(uint8_t **, int);

	Dwarf_Half	dbg_frame_rule_table_size;
	Dwarf_Half	dbg_frame_rule_initial_value;
	Dwarf_Half	dbg_frame_cfa_value;
	Dwarf_Half	dbg_frame_same_value;
	Dwarf_Half	dbg_frame_undefined_value;

	Dwarf_Regtable3	*dbg_internal_reg_table;

	/*
	 * Fields used by libdwarf producer.
	 */

	Dwarf_Unsigned	dbgp_flags;
	Dwarf_Unsigned	dbgp_isa;
	Dwarf_Callback_Func dbgp_func;
	Dwarf_Callback_Func_b dbgp_func_b;
	Dwarf_Die	dbgp_root_die;
	STAILQ_HEAD(, _Dwarf_Die) dbgp_dielist;
	STAILQ_HEAD(, _Dwarf_P_Expr) dbgp_pelist;
	Dwarf_LineInfo	dbgp_lineinfo;
	Dwarf_ArangeSet dbgp_as;
	Dwarf_Macro_Details *dbgp_mdlist;
	Dwarf_Unsigned	dbgp_mdcnt;
	STAILQ_HEAD(, _Dwarf_Cie) dbgp_cielist;
	STAILQ_HEAD(, _Dwarf_Fde) dbgp_fdelist;
	Dwarf_Unsigned	dbgp_cielen;
	Dwarf_Unsigned	dbgp_fdelen;
	Dwarf_NameTbl	dbgp_pubs;
	Dwarf_NameTbl	dbgp_weaks;
	Dwarf_NameTbl	dbgp_funcs;
	Dwarf_NameTbl	dbgp_types;
	Dwarf_NameTbl	dbgp_vars;
	STAILQ_HEAD(, _Dwarf_P_Section) dbgp_seclist;
	Dwarf_Unsigned	dbgp_seccnt;
	Dwarf_P_Section	dbgp_secpos;
	Dwarf_P_Section	dbgp_info;
	STAILQ_HEAD(, _Dwarf_Rel_Section) dbgp_drslist;
	Dwarf_Unsigned	dbgp_drscnt;
	Dwarf_Rel_Section dbgp_drspos;
};

/*
 * Internal function prototypes.
 */

int		_dwarf_abbrev_add(Dwarf_CU, uint64_t, uint64_t, uint8_t,
		    uint64_t, Dwarf_Abbrev *, Dwarf_Error *);
void		_dwarf_abbrev_cleanup(Dwarf_CU);
int		_dwarf_abbrev_find(Dwarf_CU, uint64_t, Dwarf_Abbrev *,
		    Dwarf_Error *);
int		_dwarf_abbrev_gen(Dwarf_P_Debug, Dwarf_Error *);
int		_dwarf_abbrev_parse(Dwarf_Debug, Dwarf_CU, Dwarf_Unsigned *,
		    Dwarf_Abbrev *, Dwarf_Error *);
int		_dwarf_add_AT_dataref(Dwarf_P_Debug, Dwarf_P_Die, Dwarf_Half,
		    Dwarf_Unsigned, Dwarf_Unsigned, const char *,
		    Dwarf_P_Attribute *, Dwarf_Error *);
int		_dwarf_add_string_attr(Dwarf_P_Die, Dwarf_P_Attribute *,
		    Dwarf_Half, char *, Dwarf_Error *);
int		_dwarf_alloc(Dwarf_Debug *, int, Dwarf_Error *);
void		_dwarf_arange_cleanup(Dwarf_Debug);
int		_dwarf_arange_gen(Dwarf_P_Debug, Dwarf_Error *);
int		_dwarf_arange_init(Dwarf_Debug, Dwarf_Error *);
void		_dwarf_arange_pro_cleanup(Dwarf_P_Debug);
int		_dwarf_attr_alloc(Dwarf_Die, Dwarf_Attribute *, Dwarf_Error *);
Dwarf_Attribute	_dwarf_attr_find(Dwarf_Die, Dwarf_Half);
int		_dwarf_attr_gen(Dwarf_P_Debug, Dwarf_P_Section, Dwarf_Rel_Section,
		    Dwarf_CU, Dwarf_Die, int, Dwarf_Error *);
int		_dwarf_attr_init(Dwarf_Debug, Dwarf_Section *, uint64_t *, int,
		    Dwarf_CU, Dwarf_Die, Dwarf_AttrDef, uint64_t, int,
		    Dwarf_Error *);
int		_dwarf_attrdef_add(Dwarf_Debug, Dwarf_Abbrev, uint64_t,
		    uint64_t, uint64_t, Dwarf_AttrDef *, Dwarf_Error *);
uint64_t	_dwarf_decode_lsb(uint8_t **, int);
uint64_t	_dwarf_decode_msb(uint8_t **, int);
int64_t		_dwarf_decode_sleb128(uint8_t **);
uint64_t	_dwarf_decode_uleb128(uint8_t **);
void		_dwarf_deinit(Dwarf_Debug);
int		_dwarf_die_alloc(Dwarf_Debug, Dwarf_Die *, Dwarf_Error *);
int		_dwarf_die_count_links(Dwarf_P_Die, Dwarf_P_Die,
		    Dwarf_P_Die, Dwarf_P_Die);
Dwarf_Die	_dwarf_die_find(Dwarf_Die, Dwarf_Unsigned);
int		_dwarf_die_gen(Dwarf_P_Debug, Dwarf_CU, Dwarf_Rel_Section,
		    Dwarf_Error *);
void		_dwarf_die_link(Dwarf_P_Die, Dwarf_P_Die, Dwarf_P_Die,
		    Dwarf_P_Die, Dwarf_P_Die);
int		_dwarf_die_parse(Dwarf_Debug, Dwarf_Section *, Dwarf_CU, int,
		    uint64_t, uint64_t, Dwarf_Die *, int, Dwarf_Error *);
void		_dwarf_die_pro_cleanup(Dwarf_P_Debug);
void		_dwarf_elf_deinit(Dwarf_Debug);
int		_dwarf_elf_init(Dwarf_Debug, Elf *, Dwarf_Error *);
int		_dwarf_elf_load_section(void *, Dwarf_Half, Dwarf_Small **,
		    int *);
Dwarf_Endianness _dwarf_elf_get_byte_order(void *);
Dwarf_Small	_dwarf_elf_get_length_size(void *);
Dwarf_Small	_dwarf_elf_get_pointer_size(void *);
Dwarf_Unsigned	_dwarf_elf_get_section_count(void *);
int		_dwarf_elf_get_section_info(void *, Dwarf_Half,
		    Dwarf_Obj_Access_Section *, int *);
void		_dwarf_expr_cleanup(Dwarf_P_Debug);
int		_dwarf_expr_into_block(Dwarf_P_Expr, Dwarf_Error *);
Dwarf_Section	*_dwarf_find_next_types_section(Dwarf_Debug, Dwarf_Section *);
Dwarf_Section	*_dwarf_find_section(Dwarf_Debug, const char *);
void		_dwarf_frame_cleanup(Dwarf_Debug);
int		_dwarf_frame_fde_add_inst(Dwarf_P_Fde, Dwarf_Small,
		    Dwarf_Unsigned, Dwarf_Unsigned, Dwarf_Error *);
int		_dwarf_frame_gen(Dwarf_P_Debug, Dwarf_Error *);
int		_dwarf_frame_get_fop(Dwarf_Debug, uint8_t, uint8_t *,
		    Dwarf_Unsigned, Dwarf_Frame_Op **, Dwarf_Signed *,
		    Dwarf_Error *);
int		_dwarf_frame_get_internal_table(Dwarf_Fde, Dwarf_Addr,
		    Dwarf_Regtable3 **, Dwarf_Addr *, Dwarf_Error *);
int		_dwarf_frame_interal_table_init(Dwarf_Debug, Dwarf_Error *);
void		_dwarf_frame_params_init(Dwarf_Debug);
void		_dwarf_frame_pro_cleanup(Dwarf_P_Debug);
int		_dwarf_frame_regtable_copy(Dwarf_Debug, Dwarf_Regtable3 **,
		    Dwarf_Regtable3 *, Dwarf_Error *);
int		_dwarf_frame_section_load(Dwarf_Debug, Dwarf_Error *);
int		_dwarf_frame_section_load_eh(Dwarf_Debug, Dwarf_Error *);
int		_dwarf_generate_sections(Dwarf_P_Debug, Dwarf_Error *);
Dwarf_Unsigned	_dwarf_get_reloc_type(Dwarf_P_Debug, int);
int		_dwarf_get_reloc_size(Dwarf_Debug, Dwarf_Unsigned);
void		_dwarf_info_cleanup(Dwarf_Debug);
int		_dwarf_info_first_cu(Dwarf_Debug, Dwarf_Error *);
int		_dwarf_info_first_tu(Dwarf_Debug, Dwarf_Error *);
int		_dwarf_info_gen(Dwarf_P_Debug, Dwarf_Error *);
int		_dwarf_info_load(Dwarf_Debug, Dwarf_Bool, Dwarf_Bool,
		    Dwarf_Error *);
int		_dwarf_info_next_cu(Dwarf_Debug, Dwarf_Error *);
int		_dwarf_info_next_tu(Dwarf_Debug, Dwarf_Error *);
void		_dwarf_info_pro_cleanup(Dwarf_P_Debug);
int		_dwarf_init(Dwarf_Debug, Dwarf_Unsigned, Dwarf_Handler,
		    Dwarf_Ptr, Dwarf_Error *);
int		_dwarf_lineno_gen(Dwarf_P_Debug, Dwarf_Error *);
int		_dwarf_lineno_init(Dwarf_Die, uint64_t, Dwarf_Error *);
void		_dwarf_lineno_cleanup(Dwarf_LineInfo);
void		_dwarf_lineno_pro_cleanup(Dwarf_P_Debug);
int		_dwarf_loc_fill_locdesc(Dwarf_Debug, Dwarf_Locdesc *,
		    uint8_t *, uint64_t, uint8_t, uint8_t, uint8_t,
		    Dwarf_Error *);
int		_dwarf_loc_fill_locexpr(Dwarf_Debug, Dwarf_Locdesc **,
		    uint8_t *, uint64_t, uint8_t, uint8_t, uint8_t,
		    Dwarf_Error *);
int		_dwarf_loc_add(Dwarf_Die, Dwarf_Attribute, Dwarf_Error *);
int		_dwarf_loc_expr_add_atom(Dwarf_Debug, uint8_t *, uint8_t *,
		    Dwarf_Small, Dwarf_Unsigned, Dwarf_Unsigned, int *,
		    Dwarf_Error *);
int		_dwarf_loclist_find(Dwarf_Debug, Dwarf_CU, uint64_t,
		    Dwarf_Locdesc ***, Dwarf_Signed *, Dwarf_Unsigned *,
		    Dwarf_Error *);
void		_dwarf_macinfo_cleanup(Dwarf_Debug);
int		_dwarf_macinfo_gen(Dwarf_P_Debug, Dwarf_Error *);
int		_dwarf_macinfo_init(Dwarf_Debug, Dwarf_Error *);
void		_dwarf_macinfo_pro_cleanup(Dwarf_P_Debug);
int		_dwarf_nametbl_init(Dwarf_Debug, Dwarf_NameSec *,
		    Dwarf_Section *, Dwarf_Error *);
void		_dwarf_nametbl_cleanup(Dwarf_NameSec *);
int		_dwarf_nametbl_gen(Dwarf_P_Debug, const char *, Dwarf_NameTbl,
		    Dwarf_Error *);
void		_dwarf_nametbl_pro_cleanup(Dwarf_NameTbl *);
int		_dwarf_pro_callback(Dwarf_P_Debug, char *, int, Dwarf_Unsigned,
		    Dwarf_Unsigned, Dwarf_Unsigned, Dwarf_Unsigned,
		    Dwarf_Unsigned *, int *);
Dwarf_P_Section	_dwarf_pro_find_section(Dwarf_P_Debug, const char *);
int		_dwarf_ranges_add(Dwarf_Debug, Dwarf_CU, uint64_t,
		    Dwarf_Rangelist *, Dwarf_Error *);
void		_dwarf_ranges_cleanup(Dwarf_Debug);
int		_dwarf_ranges_find(Dwarf_Debug, uint64_t, Dwarf_Rangelist *);
uint64_t	_dwarf_read_lsb(uint8_t *, uint64_t *, int);
uint64_t	_dwarf_read_msb(uint8_t *, uint64_t *, int);
int64_t		_dwarf_read_sleb128(uint8_t *, uint64_t *);
uint64_t	_dwarf_read_uleb128(uint8_t *, uint64_t *);
char		*_dwarf_read_string(void *, Dwarf_Unsigned, uint64_t *);
uint8_t		*_dwarf_read_block(void *, uint64_t *, uint64_t);
int		_dwarf_reloc_section_finalize(Dwarf_P_Debug, Dwarf_Rel_Section,
		    Dwarf_Error *);
int		_dwarf_reloc_entry_add(Dwarf_P_Debug, Dwarf_Rel_Section,
		    Dwarf_P_Section, unsigned char, unsigned char,
		    Dwarf_Unsigned, Dwarf_Unsigned, Dwarf_Unsigned,
		    const char *, Dwarf_Error *);
int		_dwarf_reloc_entry_add_pair(Dwarf_P_Debug, Dwarf_Rel_Section,
		    Dwarf_P_Section, unsigned char, Dwarf_Unsigned,
		    Dwarf_Unsigned, Dwarf_Unsigned, Dwarf_Unsigned,
		    Dwarf_Unsigned, Dwarf_Error *);
void		_dwarf_reloc_cleanup(Dwarf_P_Debug);
int		_dwarf_reloc_gen(Dwarf_P_Debug, Dwarf_Error *);
int		_dwarf_reloc_section_gen(Dwarf_P_Debug, Dwarf_Rel_Section,
		    Dwarf_Error *);
int		_dwarf_reloc_section_init(Dwarf_P_Debug, Dwarf_Rel_Section *,
		    Dwarf_P_Section, Dwarf_Error *);
void		_dwarf_reloc_section_free(Dwarf_P_Debug, Dwarf_Rel_Section *);
void		_dwarf_section_cleanup(Dwarf_P_Debug);
int		_dwarf_section_callback(Dwarf_P_Debug, Dwarf_P_Section,
		    Dwarf_Unsigned, Dwarf_Unsigned, Dwarf_Unsigned,
		    Dwarf_Unsigned, Dwarf_Error *);
void		_dwarf_section_free(Dwarf_P_Debug, Dwarf_P_Section *);
int		_dwarf_section_init(Dwarf_P_Debug, Dwarf_P_Section *,
		    const char *, int, Dwarf_Error *);
void		_dwarf_set_error(Dwarf_Debug, Dwarf_Error *, int, int,
		    const char *, int);
int		_dwarf_strtab_add(Dwarf_Debug, char *, uint64_t *,
		    Dwarf_Error *);
void		_dwarf_strtab_cleanup(Dwarf_Debug);
int		_dwarf_strtab_gen(Dwarf_P_Debug, Dwarf_Error *);
char		*_dwarf_strtab_get_table(Dwarf_Debug);
int		_dwarf_strtab_init(Dwarf_Debug, Dwarf_Error *);
void		_dwarf_type_unit_cleanup(Dwarf_Debug);
void		_dwarf_write_block(void *, uint64_t *, uint8_t *, uint64_t);
int		_dwarf_write_block_alloc(uint8_t **, uint64_t *, uint64_t *,
		    uint8_t *, uint64_t, Dwarf_Error *);
void		_dwarf_write_lsb(uint8_t *, uint64_t *, uint64_t, int);
int		_dwarf_write_lsb_alloc(uint8_t **, uint64_t *, uint64_t *,
		    uint64_t, int, Dwarf_Error *);
void		_dwarf_write_msb(uint8_t *, uint64_t *, uint64_t, int);
int		_dwarf_write_msb_alloc(uint8_t **, uint64_t *, uint64_t *,
		    uint64_t, int, Dwarf_Error *);
void		_dwarf_write_padding(void *, uint64_t *, uint8_t, uint64_t);
int		_dwarf_write_padding_alloc(uint8_t **, uint64_t *, uint64_t *,
		    uint8_t, uint64_t, Dwarf_Error *);
void		_dwarf_write_string(void *, uint64_t *, char *);
int		_dwarf_write_string_alloc(uint8_t **, uint64_t *, uint64_t *,
		    char *, Dwarf_Error *);
int		_dwarf_write_sleb128(uint8_t *, uint8_t *, int64_t);
int		_dwarf_write_sleb128_alloc(uint8_t **, uint64_t *, uint64_t *,
		    int64_t, Dwarf_Error *);
int		_dwarf_write_uleb128(uint8_t *, uint8_t *, uint64_t);
int		_dwarf_write_uleb128_alloc(uint8_t **, uint64_t *, uint64_t *,
		    uint64_t, Dwarf_Error *);

#endif /* !__LIBDWARF_H_ */

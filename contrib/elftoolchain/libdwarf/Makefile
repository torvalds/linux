# $Id: Makefile 3594 2018-04-11 18:26:50Z jkoshy $

TOP=	..

LIB=	dwarf

SRCS=	\
	dwarf_abbrev.c		\
	dwarf_arange.c		\
	dwarf_attr.c		\
	dwarf_attrval.c		\
	dwarf_cu.c		\
	dwarf_dealloc.c		\
	dwarf_die.c		\
	dwarf_dump.c		\
	dwarf_errmsg.c		\
	dwarf_finish.c		\
	dwarf_form.c		\
	dwarf_frame.c		\
	dwarf_funcs.c		\
	dwarf_init.c		\
	dwarf_lineno.c		\
	dwarf_loclist.c		\
	dwarf_macinfo.c		\
	dwarf_pro_arange.c	\
	dwarf_pro_attr.c	\
	dwarf_pro_die.c		\
	dwarf_pro_expr.c	\
	dwarf_pro_finish.c	\
	dwarf_pro_frame.c	\
	dwarf_pro_funcs.c	\
	dwarf_pro_init.c	\
	dwarf_pro_lineno.c	\
	dwarf_pro_macinfo.c	\
	dwarf_pro_pubnames.c	\
	dwarf_pro_reloc.c	\
	dwarf_pro_sections.c	\
	dwarf_pro_types.c	\
	dwarf_pro_vars.c	\
	dwarf_pro_weaks.c	\
	dwarf_pubnames.c	\
	dwarf_pubtypes.c	\
	dwarf_ranges.c		\
	dwarf_reloc.c		\
	dwarf_sections.c	\
	dwarf_seterror.c	\
	dwarf_str.c		\
	dwarf_types.c		\
	dwarf_vars.c		\
	dwarf_weaks.c		\
	libdwarf.c		\
	libdwarf_abbrev.c	\
	libdwarf_arange.c	\
	libdwarf_attr.c		\
	libdwarf_die.c		\
	libdwarf_error.c	\
	libdwarf_elf_access.c	\
	libdwarf_elf_init.c	\
	libdwarf_frame.c	\
	libdwarf_info.c		\
	libdwarf_init.c		\
	libdwarf_lineno.c	\
	libdwarf_loc.c		\
	libdwarf_loclist.c	\
	libdwarf_macinfo.c	\
	libdwarf_nametbl.c	\
	libdwarf_ranges.c	\
	libdwarf_reloc.c	\
	libdwarf_rw.c		\
	libdwarf_sections.c	\
	libdwarf_str.c

INCS=		dwarf.h libdwarf.h
INCSDIR=	/usr/include

GENSRCS=	dwarf_pubnames.c dwarf_pubtypes.c dwarf_weaks.c \
		dwarf_funcs.c dwarf_vars.c dwarf_types.c	\
		dwarf_pro_pubnames.c dwarf_pro_weaks.c		\
		dwarf_pro_funcs.c dwarf_pro_types.c		\
		dwarf_pro_vars.c
CLEANFILES=	${GENSRCS}

SHLIB_MAJOR=	3

WARNS?=	6

LDADD+=		-lelf

MAN=	dwarf.3                                         \
	dwarf_add_arange.3				\
	dwarf_add_AT_comp_dir.3				\
	dwarf_add_AT_const_value_string.3		\
	dwarf_add_AT_dataref.3				\
	dwarf_add_AT_flag.3				\
	dwarf_add_AT_location_expr.3			\
	dwarf_add_AT_name.3				\
	dwarf_add_AT_producer.3				\
	dwarf_add_AT_ref_address.3			\
	dwarf_add_AT_reference.3			\
	dwarf_add_AT_signed_const.3			\
	dwarf_add_AT_string.3				\
	dwarf_add_AT_targ_address.3			\
	dwarf_add_die_to_debug.3			\
	dwarf_add_directory_decl.3			\
	dwarf_add_expr_addr.3				\
	dwarf_add_expr_gen.3				\
	dwarf_add_fde_inst.3				\
	dwarf_add_file_decl.3				\
	dwarf_add_frame_cie.3				\
	dwarf_add_frame_fde.3				\
	dwarf_add_funcname.3				\
	dwarf_add_line_entry.3				\
	dwarf_add_pubname.3				\
	dwarf_add_typename.3				\
	dwarf_add_varname.3				\
	dwarf_add_weakname.3				\
	dwarf_attr.3					\
	dwarf_attrlist.3				\
	dwarf_attroffset.3				\
	dwarf_attrval_signed.3				\
	dwarf_child.3					\
	dwarf_dealloc.3					\
	dwarf_def_macro.3				\
	dwarf_die_abbrev_code.3				\
	dwarf_die_link.3				\
	dwarf_diename.3					\
	dwarf_dieoffset.3				\
	dwarf_end_macro_file.3				\
	dwarf_errmsg.3					\
	dwarf_errno.3					\
	dwarf_expand_frame_instructions.3		\
	dwarf_expr_current_offset.3			\
	dwarf_expr_into_block.3				\
	dwarf_fde_cfa_offset.3				\
	dwarf_find_macro_value_start.3			\
	dwarf_finish.3					\
	dwarf_formaddr.3				\
	dwarf_formblock.3				\
	dwarf_formexprloc.3				\
	dwarf_formflag.3				\
	dwarf_formref.3					\
	dwarf_formsig8.3				\
	dwarf_formstring.3				\
	dwarf_formudata.3				\
	dwarf_get_abbrev.3				\
	dwarf_get_abbrev_children_flag.3		\
	dwarf_get_abbrev_code.3				\
	dwarf_get_abbrev_entry.3			\
	dwarf_get_abbrev_tag.3				\
	dwarf_get_address_size.3			\
	dwarf_get_arange.3				\
	dwarf_get_arange_info.3				\
	dwarf_get_aranges.3				\
	dwarf_get_AT_name.3				\
	dwarf_get_cie_index.3				\
	dwarf_get_cie_info.3				\
	dwarf_get_cie_of_fde.3				\
	dwarf_get_cu_die_offset.3			\
	dwarf_get_die_infotypes_flag.3			\
	dwarf_get_elf.3					\
	dwarf_get_fde_at_pc.3				\
	dwarf_get_fde_info_for_all_regs.3		\
	dwarf_get_fde_info_for_all_regs3.3		\
	dwarf_get_fde_info_for_cfa_reg3.3		\
	dwarf_get_fde_info_for_reg.3			\
	dwarf_get_fde_info_for_reg3.3			\
	dwarf_get_fde_instr_bytes.3			\
	dwarf_get_fde_list.3				\
	dwarf_get_fde_n.3				\
	dwarf_get_fde_range.3				\
	dwarf_get_form_class.3				\
	dwarf_get_funcs.3				\
	dwarf_get_globals.3				\
	dwarf_get_loclist_entry.3			\
	dwarf_get_macro_details.3			\
	dwarf_get_pubtypes.3				\
	dwarf_get_ranges.3				\
	dwarf_get_relocation_info.3			\
	dwarf_get_relocation_info_count.3		\
	dwarf_get_section_bytes.3			\
	dwarf_get_section_max_offsets.3			\
	dwarf_get_str.3					\
	dwarf_get_types.3				\
	dwarf_get_vars.3				\
	dwarf_get_weaks.3				\
	dwarf_hasattr.3					\
	dwarf_hasform.3					\
	dwarf_highpc.3					\
	dwarf_init.3					\
	dwarf_lineno.3					\
	dwarf_lne_end_sequence.3			\
	dwarf_lne_set_address.3				\
	dwarf_loclist.3					\
	dwarf_loclist_from_expr.3			\
	dwarf_new_die.3					\
	dwarf_new_expr.3				\
	dwarf_new_fde.3					\
	dwarf_next_cu_header.3				\
	dwarf_next_types_section.3			\
	dwarf_object_init.3				\
	dwarf_producer_init.3				\
	dwarf_producer_set_isa.3			\
	dwarf_reset_section_bytes.3			\
	dwarf_seterrarg.3				\
	dwarf_set_frame_cfa_value.3			\
	dwarf_set_reloc_application.3			\
	dwarf_srcfiles.3				\
	dwarf_srclines.3				\
	dwarf_start_macro_file.3			\
	dwarf_tag.3					\
	dwarf_transform_to_disk_form.3			\
	dwarf_undef_macro.3				\
	dwarf_vendor_ext.3				\
	dwarf_whatattr.3

MLINKS+= \
	dwarf_add_AT_const_value_string.3 dwarf_add_AT_const_value_signedint.3 \
	dwarf_add_AT_const_value_string.3 dwarf_add_AT_const_value_unsignedint.3 \
	dwarf_add_AT_signed_const.3 dwarf_add_AT_unsigned_const.3 \
	dwarf_add_AT_targ_address.3 dwarf_add_AT_targ_address_b.3 \
	dwarf_add_arange.3	dwarf_add_arange_b.3	\
	dwarf_add_expr_addr.3	dwarf_add_expr_addr_b.3	\
	dwarf_add_frame_fde.3	dwarf_add_frame_fde_b.3	\
	dwarf_attrval_signed.3	dwarf_attrval_flag.3	\
	dwarf_attrval_signed.3	dwarf_attrval_string.3	\
	dwarf_attrval_signed.3	dwarf_attrval_unsigned.3 \
	dwarf_child.3	dwarf_offdie.3			\
	dwarf_child.3	dwarf_offdie_b.3		\
	dwarf_child.3	dwarf_siblingof.3		\
	dwarf_child.3	dwarf_siblingof_b.3		\
	dwarf_dealloc.3	dwarf_fde_cie_list_dealloc.3	\
	dwarf_dealloc.3	dwarf_funcs_dealloc.3		\
	dwarf_dealloc.3	dwarf_globals_dealloc.3		\
	dwarf_dealloc.3	dwarf_pubtypes_dealloc.3	\
	dwarf_dealloc.3	dwarf_types_dealloc.3		\
	dwarf_dealloc.3	dwarf_vars_dealloc.3		\
	dwarf_dealloc.3	dwarf_weaks_dealloc.3		\
	dwarf_dealloc.3	dwarf_ranges_dealloc.3		\
	dwarf_dealloc.3	dwarf_srclines_dealloc.3	\
	dwarf_init.3	dwarf_elf_init.3		\
	dwarf_dieoffset.3	dwarf_die_CU_offset.3	\
	dwarf_dieoffset.3	dwarf_die_CU_offset_range.3 \
	dwarf_dieoffset.3	dwarf_get_cu_die_offset_given_cu_header_offset.3 \
	dwarf_dieoffset.3	dwarf_get_cu_die_offset_given_cu_header_offset_b.3 \
	dwarf_finish.3		dwarf_object_finish.3	\
	dwarf_formref.3	dwarf_global_formref.3		\
	dwarf_formudata.3	dwarf_formsdata.3	\
	dwarf_get_AT_name.3	dwarf_get_ACCESS_name.3	\
	dwarf_get_AT_name.3	dwarf_get_ATE_name.3	\
	dwarf_get_AT_name.3	dwarf_get_CC_name.3	\
	dwarf_get_AT_name.3	dwarf_get_CFA_name.3	\
	dwarf_get_AT_name.3	dwarf_get_CHILDREN_name.3 \
	dwarf_get_AT_name.3	dwarf_get_DS_name.3	\
	dwarf_get_AT_name.3	dwarf_get_DSC_name.3	\
	dwarf_get_AT_name.3	dwarf_get_EH_name.3	\
	dwarf_get_AT_name.3	dwarf_get_END_name.3	\
	dwarf_get_AT_name.3	dwarf_get_FORM_name.3	\
	dwarf_get_AT_name.3	dwarf_get_ID_name.3	\
	dwarf_get_AT_name.3	dwarf_get_INL_name.3	\
	dwarf_get_AT_name.3	dwarf_get_LANG_name.3	\
	dwarf_get_AT_name.3	dwarf_get_LNE_name.3	\
	dwarf_get_AT_name.3	dwarf_get_LNS_name.3	\
	dwarf_get_AT_name.3	dwarf_get_MACINFO_name.3 \
	dwarf_get_AT_name.3	dwarf_get_OP_name.3	\
	dwarf_get_AT_name.3	dwarf_get_ORD_name.3	\
	dwarf_get_AT_name.3	dwarf_get_TAG_name.3	\
	dwarf_get_AT_name.3	dwarf_get_VIRTUALITY_name.3 \
	dwarf_get_AT_name.3	dwarf_get_VIS_name.3	\
	dwarf_get_cu_die_offset.3 dwarf_get_arange_cu_header_offset.3 \
	dwarf_get_fde_list.3	dwarf_get_fde_list_eh.3	\
	dwarf_get_funcs.3	dwarf_func_die_offset.3	\
	dwarf_get_funcs.3	dwarf_func_cu_offset.3	\
	dwarf_get_funcs.3	dwarf_func_name_offsets.3 \
	dwarf_get_funcs.3	dwarf_funcname.3	\
	dwarf_get_globals.3	dwarf_global_die_offset.3 \
	dwarf_get_globals.3	dwarf_global_cu_offset.3 \
	dwarf_get_globals.3	dwarf_global_name_offsets.3 \
	dwarf_get_globals.3	dwarf_globname.3	\
	dwarf_get_pubtypes.3	dwarf_pubtype_die_offset.3 \
	dwarf_get_pubtypes.3	dwarf_pubtype_cu_offset.3 \
	dwarf_get_pubtypes.3	dwarf_pubtype_name_offsets.3 \
	dwarf_get_pubtypes.3	dwarf_pubtypename.3	\
	dwarf_get_ranges.3	dwarf_get_ranges_a.3	\
	dwarf_get_section_max_offsets.3 dwarf_get_section_max_offsets_b.3 \
	dwarf_get_types.3	dwarf_type_die_offset.3	\
	dwarf_get_types.3	dwarf_type_cu_offset.3	\
	dwarf_get_types.3	dwarf_type_name_offsets.3 \
	dwarf_get_types.3	dwarf_typename.3	\
	dwarf_get_vars.3	dwarf_var_die_offset.3	\
	dwarf_get_vars.3	dwarf_var_cu_offset.3	\
	dwarf_get_vars.3	dwarf_var_name_offsets.3 \
	dwarf_get_vars.3	dwarf_varname.3		\
	dwarf_get_weaks.3	dwarf_weak_die_offset.3	\
	dwarf_get_weaks.3	dwarf_weak_cu_offset.3	\
	dwarf_get_weaks.3	dwarf_weak_name_offsets.3 \
	dwarf_get_weaks.3	dwarf_weakname.3	\
	dwarf_hasform.3	dwarf_whatform.3		\
	dwarf_hasform.3	dwarf_whatform_direct.3		\
	dwarf_highpc.3	dwarf_arrayorder.3		\
	dwarf_highpc.3	dwarf_bitoffset.3		\
	dwarf_highpc.3	dwarf_bitsize.3			\
	dwarf_highpc.3	dwarf_bytesize.3		\
	dwarf_highpc.3	dwarf_highpc_b.3		\
	dwarf_highpc.3	dwarf_lowpc.3			\
	dwarf_highpc.3	dwarf_srclang.3			\
	dwarf_lineno.3	dwarf_lineaddr.3		\
	dwarf_lineno.3	dwarf_linebeginstatement.3	\
	dwarf_lineno.3	dwarf_lineblock.3		\
	dwarf_lineno.3	dwarf_lineendsequence.3		\
	dwarf_lineno.3	dwarf_lineoff.3			\
	dwarf_lineno.3	dwarf_linesrc.3			\
	dwarf_lineno.3	dwarf_line_srcfileno.3		\
	dwarf_loclist.3	dwarf_loclist_n.3		\
	dwarf_loclist_from_expr.3 dwarf_loclist_from_expr_a.3 \
	dwarf_loclist_from_expr.3 dwarf_loclist_from_expr_b.3 \
	dwarf_next_cu_header.3 dwarf_next_cu_header_b.3	\
	dwarf_next_cu_header.3 dwarf_next_cu_header_c.3	\
	dwarf_producer_init.3 dwarf_producer_init_b.3	\
	dwarf_seterrarg.3	dwarf_seterrhand.3	\
	dwarf_set_frame_cfa_value.3 dwarf_set_frame_rule_initial_value.3 \
	dwarf_set_frame_cfa_value.3 dwarf_set_frame_rule_table_size.3 \
	dwarf_set_frame_cfa_value.3 dwarf_set_frame_same_value.3 \
	dwarf_set_frame_cfa_value.3 dwarf_set_frame_undefined_value.3

dwarf_pubnames.c:	dwarf_nametbl.m4 dwarf_pubnames.m4
dwarf_pubtypes.c:	dwarf_nametbl.m4 dwarf_pubtypes.m4
dwarf_weaks.c:		dwarf_nametbl.m4 dwarf_weaks.m4
dwarf_funcs.c:		dwarf_nametbl.m4 dwarf_funcs.m4
dwarf_vars.c:		dwarf_nametbl.m4 dwarf_vars.m4
dwarf_types.c:		dwarf_nametbl.m4 dwarf_types.m4
dwarf_pro_pubnames.c:	dwarf_pro_nametbl.m4 dwarf_pro_pubnames.m4
dwarf_pro_weaks.c:	dwarf_pro_nametbl.m4 dwarf_pro_weaks.m4
dwarf_pro_funcs.c:	dwarf_pro_nametbl.m4 dwarf_pro_funcs.m4
dwarf_pro_types.c:	dwarf_pro_nametbl.m4 dwarf_pro_types.m4
dwarf_pro_vars.c:	dwarf_pro_nametbl.m4 dwarf_pro_vars.m4

.include "${TOP}/mk/elftoolchain.lib.mk"

/*-
 * Copyright (c) 2007 John Birrell (jb@freebsd.org)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *notice, this list of conditions and the following disclaimer in the
 *documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: dwarf.h 3494 2016-09-20 17:16:13Z emaste $
 */

#ifndef	_DWARF_H_
#define	_DWARF_H_

#define DW_TAG_array_type		0x01
#define DW_TAG_class_type		0x02
#define DW_TAG_entry_point		0x03
#define DW_TAG_enumeration_type		0x04
#define DW_TAG_formal_parameter		0x05
#define DW_TAG_imported_declaration	0x08
#define DW_TAG_label			0x0a
#define DW_TAG_lexical_block		0x0b
#define DW_TAG_member			0x0d
#define DW_TAG_pointer_type		0x0f
#define DW_TAG_reference_type		0x10
#define DW_TAG_compile_unit		0x11
#define DW_TAG_string_type		0x12
#define DW_TAG_structure_type		0x13
#define DW_TAG_subroutine_type		0x15
#define DW_TAG_typedef			0x16
#define DW_TAG_union_type		0x17
#define DW_TAG_unspecified_parameters	0x18
#define DW_TAG_variant			0x19
#define DW_TAG_common_block		0x1a
#define DW_TAG_common_inclusion		0x1b
#define DW_TAG_inheritance		0x1c
#define DW_TAG_inlined_subroutine	0x1d
#define DW_TAG_module			0x1e
#define DW_TAG_ptr_to_member_type	0x1f
#define DW_TAG_set_type			0x20
#define DW_TAG_subrange_type		0x21
#define DW_TAG_with_stmt		0x22
#define DW_TAG_access_declaration	0x23
#define DW_TAG_base_type		0x24
#define DW_TAG_catch_block		0x25
#define DW_TAG_const_type		0x26
#define DW_TAG_constant			0x27
#define DW_TAG_enumerator		0x28
#define DW_TAG_friend			0x2a
#define DW_TAG_namelist			0x2b
#define DW_TAG_namelist_item		0x2c
#define DW_TAG_packed_type		0x2d
#define DW_TAG_subprogram		0x2e
#define DW_TAG_template_type_parameter	0x2f
#define DW_TAG_template_type_param	0x2f
#define DW_TAG_template_value_parameter	0x30
#define DW_TAG_template_value_param	0x30
#define DW_TAG_thrown_type		0x31
#define DW_TAG_try_block		0x32
#define DW_TAG_variant_part		0x33
#define DW_TAG_variable			0x34
#define DW_TAG_volatile_type		0x35
#define DW_TAG_dwarf_procedure          0x36
#define DW_TAG_restrict_type            0x37
#define DW_TAG_interface_type           0x38
#define DW_TAG_namespace                0x39
#define DW_TAG_imported_module          0x3a
#define DW_TAG_unspecified_type         0x3b
#define DW_TAG_partial_unit             0x3c
#define DW_TAG_imported_unit            0x3d
#define DW_TAG_condition                0x3f
#define DW_TAG_shared_type              0x40
#define DW_TAG_type_unit		0x41
#define DW_TAG_rvalue_reference_type	0x42
#define DW_TAG_template_alias		0x43
#define DW_TAG_lo_user			0x4080
#define DW_TAG_hi_user			0xffff

/* GNU extensions. */
#define	DW_TAG_format_label		0x4101
#define	DW_TAG_function_template	0x4102
#define	DW_TAG_class_template		0x4103
#define	DW_TAG_GNU_BINCL		0x4104
#define	DW_TAG_GNU_EINCL		0x4105
#define	DW_TAG_GNU_template_template_parameter	0x4106
#define	DW_TAG_GNU_template_template_param	0x4106
#define	DW_TAG_GNU_template_parameter_pack	0x4107
#define	DW_TAG_GNU_formal_parameter_pack	0x4108
#define	DW_TAG_GNU_call_site			0x4109
#define	DW_TAG_GNU_call_site_parameter		0x410a

#define DW_CHILDREN_no			0x00
#define DW_CHILDREN_yes			0x01

#define DW_AT_sibling			0x01
#define DW_AT_location			0x02
#define DW_AT_name			0x03
#define DW_AT_ordering			0x09
#define DW_AT_subscr_data		0x0a
#define DW_AT_byte_size			0x0b
#define DW_AT_bit_offset		0x0c
#define DW_AT_bit_size			0x0d
#define DW_AT_element_list		0x0f
#define DW_AT_stmt_list			0x10
#define DW_AT_low_pc			0x11
#define DW_AT_high_pc			0x12
#define DW_AT_language			0x13
#define DW_AT_member			0x14
#define DW_AT_discr			0x15
#define DW_AT_discr_value		0x16
#define DW_AT_visibility		0x17
#define DW_AT_import			0x18
#define DW_AT_string_length		0x19
#define DW_AT_common_reference		0x1a
#define DW_AT_comp_dir			0x1b
#define DW_AT_const_value		0x1c
#define DW_AT_containing_type		0x1d
#define DW_AT_default_value		0x1e
#define DW_AT_inline			0x20
#define DW_AT_is_optional		0x21
#define DW_AT_lower_bound		0x22
#define DW_AT_producer			0x25
#define DW_AT_prototyped		0x27
#define DW_AT_return_addr		0x2a
#define DW_AT_start_scope		0x2c
#define DW_AT_bit_stride		0x2e
#define DW_AT_stride_size		0x2e
#define DW_AT_upper_bound		0x2f
#define DW_AT_abstract_origin		0x31
#define DW_AT_accessibility		0x32
#define DW_AT_address_class		0x33
#define DW_AT_artificial		0x34
#define DW_AT_base_types		0x35
#define DW_AT_calling_convention	0x36
#define DW_AT_count			0x37
#define DW_AT_data_member_location	0x38
#define DW_AT_decl_column		0x39
#define DW_AT_decl_file			0x3a
#define DW_AT_decl_line			0x3b
#define DW_AT_declaration		0x3c
#define DW_AT_discr_list		0x3d
#define DW_AT_encoding			0x3e
#define DW_AT_external			0x3f
#define DW_AT_frame_base		0x40
#define DW_AT_friend			0x41
#define DW_AT_identifier_case		0x42
#define DW_AT_macro_info		0x43
#define DW_AT_namelist_item		0x44
#define DW_AT_priority			0x45
#define DW_AT_segment			0x46
#define DW_AT_specification		0x47
#define DW_AT_static_link		0x48
#define DW_AT_type			0x49
#define DW_AT_use_location		0x4a
#define DW_AT_variable_parameter	0x4b
#define DW_AT_virtuality		0x4c
#define DW_AT_vtable_elem_location	0x4d
#define DW_AT_allocated			0x4e
#define DW_AT_associated		0x4f
#define DW_AT_data_location		0x50
#define DW_AT_byte_stride		0x51
#define DW_AT_entry_pc			0x52
#define DW_AT_use_UTF8			0x53
#define DW_AT_extension			0x54
#define DW_AT_ranges			0x55
#define DW_AT_trampoline		0x56
#define DW_AT_call_column		0x57
#define DW_AT_call_file			0x58
#define DW_AT_call_line			0x59
#define DW_AT_description		0x5a
#define DW_AT_binary_scale		0x5b
#define DW_AT_decimal_scale		0x5c
#define DW_AT_small			0x5d
#define DW_AT_decimal_sign		0x5e
#define DW_AT_digit_count		0x5f
#define DW_AT_picture_string		0x60
#define DW_AT_mutable			0x61
#define DW_AT_threads_scaled		0x62
#define DW_AT_explicit			0x63
#define DW_AT_object_pointer		0x64
#define DW_AT_endianity			0x65
#define DW_AT_elemental			0x66
#define DW_AT_pure			0x67
#define DW_AT_recursive			0x68
#define DW_AT_signature			0x69
#define DW_AT_main_subprogram		0x6a
#define DW_AT_data_bit_offset		0x6b
#define DW_AT_const_expr		0x6c
#define DW_AT_enum_class		0x6d
#define DW_AT_linkage_name		0x6e
#define DW_AT_lo_user			0x2000
#define DW_AT_hi_user			0x3fff

/* SGI/MIPS extensions. */
#define DW_AT_MIPS_fde				0x2001
#define DW_AT_MIPS_loop_begin			0x2002
#define DW_AT_MIPS_tail_loop_begin		0x2003
#define DW_AT_MIPS_epilog_begin			0x2004
#define DW_AT_MIPS_loop_unroll_factor		0x2005
#define DW_AT_MIPS_software_pipeline_depth	0x2006
#define DW_AT_MIPS_linkage_name			0x2007
#define DW_AT_MIPS_stride			0x2008
#define DW_AT_MIPS_abstract_name		0x2009
#define DW_AT_MIPS_clone_origin			0x200a
#define DW_AT_MIPS_has_inlines			0x200b
#define DW_AT_MIPS_stride_byte			0x200c
#define DW_AT_MIPS_stride_elem			0x200d
#define DW_AT_MIPS_ptr_dopetype			0x200e
#define DW_AT_MIPS_allocatable_dopetype		0x200f
#define DW_AT_MIPS_assumed_shape_dopetype	0x2010
#define DW_AT_MIPS_assumed_size			0x2011

/* GNU extensions. */
#define	DW_AT_sf_names				0x2101
#define	DW_AT_src_info				0x2102
#define	DW_AT_mac_info				0x2103
#define	DW_AT_src_coords			0x2104
#define	DW_AT_body_begin			0x2105
#define	DW_AT_body_end				0x2106
#define	DW_AT_GNU_vector			0x2107
#define	DW_AT_GNU_guarded_by			0x2108
#define	DW_AT_GNU_pt_guarded_by			0x2109
#define	DW_AT_GNU_guarded			0x210a
#define	DW_AT_GNU_pt_guarded			0x210b
#define	DW_AT_GNU_locks_excluded		0x210c
#define	DW_AT_GNU_exclusive_locks_required	0x210d
#define	DW_AT_GNU_shared_locks_required		0x210e
#define	DW_AT_GNU_odr_signature			0x210f
#define	DW_AT_GNU_template_name			0x2110
#define	DW_AT_GNU_call_site_value		0x2111
#define	DW_AT_GNU_call_site_data_value		0x2112
#define	DW_AT_GNU_call_site_target		0x2113
#define	DW_AT_GNU_call_site_target_clobbered	0x2114
#define	DW_AT_GNU_tail_call			0x2115
#define	DW_AT_GNU_all_tail_call_sites		0x2116
#define	DW_AT_GNU_all_call_sites		0x2117
#define	DW_AT_GNU_all_source_call_sites		0x2118

/* Apple extensions. */
#define	DW_AT_APPLE_optimized			0x3fe1
#define	DW_AT_APPLE_flags			0x3fe2
#define	DW_AT_APPLE_isa				0x3fe3
#define	DW_AT_APPLE_block			0x3fe4
#define	DW_AT_APPLE_major_runtime_vers		0x3fe5
#define	DW_AT_APPLE_runtime_class		0x3fe6
#define	DW_AT_APPLE_omit_frame_ptr		0x3fe7
#define	DW_AT_APPLE_property_name		0x3fe8
#define	DW_AT_APPLE_property_getter		0x3fe9
#define	DW_AT_APPLE_property_setter		0x3fea
#define	DW_AT_APPLE_property_attribute		0x3feb
#define	DW_AT_APPLE_objc_complete_type		0x3fec
#define	DW_AT_APPLE_property			0x3fed

#define DW_FORM_addr			0x01
#define DW_FORM_block2			0x03
#define DW_FORM_block4			0x04
#define DW_FORM_data2			0x05
#define DW_FORM_data4			0x06
#define DW_FORM_data8			0x07
#define DW_FORM_string			0x08
#define DW_FORM_block			0x09
#define DW_FORM_block1			0x0a
#define DW_FORM_data1			0x0b
#define DW_FORM_flag			0x0c
#define DW_FORM_sdata			0x0d
#define DW_FORM_strp			0x0e
#define DW_FORM_udata			0x0f
#define DW_FORM_ref_addr		0x10
#define DW_FORM_ref1			0x11
#define DW_FORM_ref2			0x12
#define DW_FORM_ref4			0x13
#define DW_FORM_ref8			0x14
#define DW_FORM_ref_udata		0x15
#define DW_FORM_indirect		0x16
#define DW_FORM_sec_offset		0x17
#define DW_FORM_exprloc			0x18
#define DW_FORM_flag_present		0x19
#define DW_FORM_ref_sig8		0x20
#define	DW_FORM_GNU_ref_alt		0x1f20
#define	DW_FORM_GNU_strp_alt		0x1f21

#define DW_OP_addr			0x03
#define DW_OP_deref			0x06
#define DW_OP_const1u			0x08
#define DW_OP_const1s			0x09
#define DW_OP_const2u			0x0a
#define DW_OP_const2s			0x0b
#define DW_OP_const4u			0x0c
#define DW_OP_const4s			0x0d
#define DW_OP_const8u			0x0e
#define DW_OP_const8s			0x0f
#define DW_OP_constu			0x10
#define DW_OP_consts			0x11
#define DW_OP_dup			0x12
#define DW_OP_drop			0x13
#define DW_OP_over			0x14
#define DW_OP_pick			0x15
#define DW_OP_swap			0x16
#define DW_OP_rot			0x17
#define DW_OP_xderef			0x18
#define DW_OP_abs			0x19
#define DW_OP_and			0x1a
#define DW_OP_div			0x1b
#define DW_OP_minus			0x1c
#define DW_OP_mod			0x1d
#define DW_OP_mul			0x1e
#define DW_OP_neg			0x1f
#define DW_OP_not			0x20
#define DW_OP_or			0x21
#define DW_OP_plus			0x22
#define DW_OP_plus_uconst		0x23
#define DW_OP_shl			0x24
#define DW_OP_shr			0x25
#define DW_OP_shra			0x26
#define DW_OP_xor			0x27
#define DW_OP_bra			0x28
#define DW_OP_eq			0x29
#define DW_OP_ge			0x2a
#define DW_OP_gt			0x2b
#define DW_OP_le			0x2c
#define DW_OP_lt			0x2d
#define DW_OP_ne			0x2e
#define DW_OP_skip			0x2f
#define DW_OP_lit0			0x30
#define DW_OP_lit1			0x31
#define DW_OP_lit2			0x32
#define DW_OP_lit3			0x33
#define DW_OP_lit4			0x34
#define DW_OP_lit5			0x35
#define DW_OP_lit6			0x36
#define DW_OP_lit7			0x37
#define DW_OP_lit8			0x38
#define DW_OP_lit9			0x39
#define DW_OP_lit10			0x3a
#define DW_OP_lit11			0x3b
#define DW_OP_lit12			0x3c
#define DW_OP_lit13			0x3d
#define DW_OP_lit14			0x3e
#define DW_OP_lit15			0x3f
#define DW_OP_lit16			0x40
#define DW_OP_lit17			0x41
#define DW_OP_lit18			0x42
#define DW_OP_lit19			0x43
#define DW_OP_lit20			0x44
#define DW_OP_lit21			0x45
#define DW_OP_lit22			0x46
#define DW_OP_lit23			0x47
#define DW_OP_lit24			0x48
#define DW_OP_lit25			0x49
#define DW_OP_lit26			0x4a
#define DW_OP_lit27			0x4b
#define DW_OP_lit28			0x4c
#define DW_OP_lit29			0x4d
#define DW_OP_lit30			0x4e
#define DW_OP_lit31			0x4f
#define DW_OP_reg0			0x50
#define DW_OP_reg1			0x51
#define DW_OP_reg2			0x52
#define DW_OP_reg3			0x53
#define DW_OP_reg4			0x54
#define DW_OP_reg5			0x55
#define DW_OP_reg6			0x56
#define DW_OP_reg7			0x57
#define DW_OP_reg8			0x58
#define DW_OP_reg9			0x59
#define DW_OP_reg10			0x5a
#define DW_OP_reg11			0x5b
#define DW_OP_reg12			0x5c
#define DW_OP_reg13			0x5d
#define DW_OP_reg14			0x5e
#define DW_OP_reg15			0x5f
#define DW_OP_reg16			0x60
#define DW_OP_reg17			0x61
#define DW_OP_reg18			0x62
#define DW_OP_reg19			0x63
#define DW_OP_reg20			0x64
#define DW_OP_reg21			0x65
#define DW_OP_reg22			0x66
#define DW_OP_reg23			0x67
#define DW_OP_reg24			0x68
#define DW_OP_reg25			0x69
#define DW_OP_reg26			0x6a
#define DW_OP_reg27			0x6b
#define DW_OP_reg28			0x6c
#define DW_OP_reg29			0x6d
#define DW_OP_reg30			0x6e
#define DW_OP_reg31			0x6f
#define DW_OP_breg0			0x70
#define DW_OP_breg1			0x71
#define DW_OP_breg2			0x72
#define DW_OP_breg3			0x73
#define DW_OP_breg4			0x74
#define DW_OP_breg5			0x75
#define DW_OP_breg6			0x76
#define DW_OP_breg7			0x77
#define DW_OP_breg8			0x78
#define DW_OP_breg9			0x79
#define DW_OP_breg10			0x7a
#define DW_OP_breg11			0x7b
#define DW_OP_breg12			0x7c
#define DW_OP_breg13			0x7d
#define DW_OP_breg14			0x7e
#define DW_OP_breg15			0x7f
#define DW_OP_breg16			0x80
#define DW_OP_breg17			0x81
#define DW_OP_breg18			0x82
#define DW_OP_breg19			0x83
#define DW_OP_breg20			0x84
#define DW_OP_breg21			0x85
#define DW_OP_breg22			0x86
#define DW_OP_breg23			0x87
#define DW_OP_breg24			0x88
#define DW_OP_breg25			0x89
#define DW_OP_breg26			0x8a
#define DW_OP_breg27			0x8b
#define DW_OP_breg28			0x8c
#define DW_OP_breg29			0x8d
#define DW_OP_breg30			0x8e
#define DW_OP_breg31			0x8f
#define DW_OP_regx			0x90
#define DW_OP_fbreg			0x91
#define DW_OP_bregx			0x92
#define DW_OP_piece			0x93
#define DW_OP_deref_size		0x94
#define DW_OP_xderef_size		0x95
#define DW_OP_nop			0x96
#define DW_OP_push_object_address	0x97
#define DW_OP_call2			0x98
#define DW_OP_call4			0x99
#define DW_OP_call_ref			0x9a
#define DW_OP_form_tls_address		0x9b
#define DW_OP_call_frame_cfa		0x9c
#define DW_OP_bit_piece			0x9d
#define DW_OP_implicit_value		0x9e
#define DW_OP_stack_value		0x9f
#define DW_OP_lo_user		 	0xe0
#define DW_OP_hi_user		 	0xff

/* GNU extensions. */
#define	DW_OP_GNU_push_tls_address	0xe0
#define	DW_OP_GNU_uninit		0xf0
#define	DW_OP_GNU_encoded_addr		0xf1
#define	DW_OP_GNU_implicit_pointer	0xf2
#define	DW_OP_GNU_entry_value		0xf3
#define	DW_OP_GNU_const_type		0xf4
#define	DW_OP_GNU_regval_type		0xf5
#define	DW_OP_GNU_deref_type		0xf6
#define	DW_OP_GNU_convert		0xf7
#define	DW_OP_GNU_reinterpret		0xf9
#define	DW_OP_GNU_parameter_ref		0xfa
#define	DW_OP_GNU_addr_index		0xfb
#define	DW_OP_GNU_const_index		0xfc

#define DW_ATE_address		 	0x1
#define DW_ATE_boolean		 	0x2
#define DW_ATE_complex_float	 	0x3
#define DW_ATE_float		 	0x4
#define DW_ATE_signed		 	0x5
#define DW_ATE_signed_char	 	0x6
#define DW_ATE_unsigned		 	0x7
#define DW_ATE_unsigned_char	 	0x8
#define DW_ATE_imaginary_float	 	0x9
#define DW_ATE_packed_decimal	 	0xa
#define DW_ATE_numeric_string	 	0xb
#define DW_ATE_edited		 	0xc
#define DW_ATE_signed_fixed	 	0xd
#define DW_ATE_unsigned_fixed	 	0xe
#define DW_ATE_decimal_float	 	0xf
#define DW_ATE_lo_user		 	0x80
#define DW_ATE_hi_user		 	0xff

#define DW_ACCESS_public		0x01
#define DW_ACCESS_protected	 	0x02
#define DW_ACCESS_private	 	0x03

#define	DW_END_default			0x00
#define	DW_END_big			0x01
#define	DW_END_little			0x02
#define	DW_END_lo_user			0x40
#define	DW_END_high_user		0xff

#define DW_VIS_local		 	0x01
#define DW_VIS_exported		 	0x02
#define DW_VIS_qualified		0x03

#define DW_VIRTUALITY_none	 	0x00
#define DW_VIRTUALITY_virtual	 	0x01
#define DW_VIRTUALITY_pure_virtual 	0x02

#define DW_LANG_C89		 	0x0001
#define DW_LANG_C		 	0x0002
#define DW_LANG_Ada83		 	0x0003
#define DW_LANG_C_plus_plus	 	0x0004
#define DW_LANG_Cobol74		 	0x0005
#define DW_LANG_Cobol85		 	0x0006
#define DW_LANG_Fortran77	 	0x0007
#define DW_LANG_Fortran90	 	0x0008
#define DW_LANG_Pascal83		0x0009
#define DW_LANG_Modula2		 	0x000a
#define DW_LANG_Java		 	0x000b
#define DW_LANG_C99		 	0x000c
#define DW_LANG_Ada95		 	0x000d
#define DW_LANG_Fortran95	 	0x000e
#define DW_LANG_PLI		 	0x000f
#define DW_LANG_ObjC		 	0x0010
#define DW_LANG_ObjC_plus_plus	 	0x0011
#define DW_LANG_UPC		 	0x0012
#define DW_LANG_D		 	0x0013
#define DW_LANG_Python			0x0014
#define DW_LANG_OpenCL			0x0015
#define DW_LANG_Go			0x0016
#define DW_LANG_Modula3			0x0017
#define DW_LANG_Haskell			0x0018
#define DW_LANG_C_plus_plus_03		0x0019
#define DW_LANG_C_plus_plus_11		0x001a
#define DW_LANG_OCaml			0x001b
#define DW_LANG_Rust			0x001c
#define DW_LANG_C11			0x001d
#define DW_LANG_Swift			0x001e
#define DW_LANG_Julia			0x001f
#define DW_LANG_Dylan			0x0020
#define DW_LANG_C_plus_plus_14		0x0021
#define DW_LANG_Fortran03		0x0022
#define DW_LANG_Fortran08		0x0023
#define DW_LANG_RenderScript		0x0024
#define DW_LANG_BLISS			0x0025
#define DW_LANG_lo_user		 	0x8000
#define DW_LANG_Mips_Assembler		0x8001
#define DW_LANG_hi_user		 	0xffff

#define DW_ID_case_sensitive	 	0x00
#define DW_ID_up_case		 	0x01
#define DW_ID_down_case		 	0x02
#define DW_ID_case_insensitive	 	0x03

#define DW_CC_normal		 	0x01
#define DW_CC_program		 	0x02
#define DW_CC_nocall		 	0x03
#define DW_CC_lo_user		 	0x40
#define DW_CC_hi_user		 	0xff

#define DW_INL_not_inlined	 	0x00
#define DW_INL_inlined		 	0x01
#define DW_INL_declared_not_inlined 	0x02
#define DW_INL_declared_inlined	 	0x03

#define DW_ORD_row_major		0x00
#define DW_ORD_col_major		0x01

#define	DW_DS_unsigned			0x01
#define	DW_DS_leading_overpunch		0x02
#define	DW_DS_trailing_overpunch	0x03
#define	DW_DS_leading_separate		0x04
#define	DW_DS_trailing_separate		0x05

#define DW_DSC_label		 	0x00
#define DW_DSC_range		 	0x01

#define DW_LNS_copy		 	0x01
#define DW_LNS_advance_pc	 	0x02
#define DW_LNS_advance_line	 	0x03
#define DW_LNS_set_file		 	0x04
#define DW_LNS_set_column	 	0x05
#define DW_LNS_negate_stmt	 	0x06
#define DW_LNS_set_basic_block	 	0x07
#define DW_LNS_const_add_pc	 	0x08
#define DW_LNS_fixed_advance_pc	 	0x09
#define DW_LNS_set_prologue_end	 	0x0a
#define DW_LNS_set_epilogue_begin 	0x0b
#define DW_LNS_set_isa		 	0x0c

#define DW_LNE_end_sequence	 	0x01
#define DW_LNE_set_address	 	0x02
#define DW_LNE_define_file	 	0x03
#define DW_LNE_lo_user		 	0x80
#define DW_LNE_hi_user		 	0xff

#define DW_MACINFO_define	 	0x01
#define DW_MACINFO_undef		0x02
#define DW_MACINFO_start_file	 	0x03
#define DW_MACINFO_end_file	 	0x04
#define DW_MACINFO_vendor_ext	 	0xff

#define DW_CFA_advance_loc		0x40
#define DW_CFA_offset	 		0x80
#define DW_CFA_restore	 		0xc0
#define DW_CFA_extended			0

#define DW_CFA_nop	 		0x00
#define DW_CFA_set_loc	 		0x01
#define DW_CFA_advance_loc1 		0x02
#define DW_CFA_advance_loc2 		0x03
#define DW_CFA_advance_loc4 		0x04
#define DW_CFA_offset_extended 		0x05
#define DW_CFA_restore_extended 	0x06
#define DW_CFA_undefined		0x07
#define DW_CFA_same_value 		0x08
#define DW_CFA_register	 		0x09
#define DW_CFA_remember_state 		0x0a
#define DW_CFA_restore_state 		0x0b
#define DW_CFA_def_cfa	 		0x0c
#define DW_CFA_def_cfa_register 	0x0d
#define DW_CFA_def_cfa_offset 		0x0e
#define DW_CFA_def_cfa_expression 	0x0f
#define DW_CFA_expression 		0x10
#define DW_CFA_offset_extended_sf 	0x11
#define DW_CFA_def_cfa_sf 		0x12
#define DW_CFA_def_cfa_offset_sf 	0x13
#define DW_CFA_val_offset		0x14
#define DW_CFA_val_offset_sf 		0x15
#define DW_CFA_val_expression 		0x16
#define DW_CFA_lo_user	 		0x1c
#define DW_CFA_high_user	 	0x3f

/*
 * LSB(Linux Standard Base) extension to DWARF2.
 */

#define	DW_EH_PE_absptr			0x00
#define	DW_EH_PE_uleb128		0x01
#define	DW_EH_PE_udata2			0x02
#define	DW_EH_PE_udata4			0x03
#define	DW_EH_PE_udata8			0x04
#define	DW_EH_PE_sleb128		0x09
#define	DW_EH_PE_sdata2			0x0a
#define	DW_EH_PE_sdata4			0x0b
#define	DW_EH_PE_sdata8			0x0c
#define	DW_EH_PE_pcrel			0x10
#define	DW_EH_PE_textrel		0x20
#define	DW_EH_PE_datarel		0x30
#define	DW_EH_PE_funcrel		0x40
#define	DW_EH_PE_aligned		0x50
#define	DW_EH_PE_omit			0xff

#endif /* !_DWARF_H_ */

/*-
 * Copyright (c) 2007 John Birrell (jb@freebsd.org)
 * Copyright (c) 2009 Kai Wang
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
 */

#include "_libdwarf.h"

ELFTC_VCSID("$Id: dwarf_dump.c 3494 2016-09-20 17:16:13Z emaste $");

int
dwarf_get_ACCESS_name(unsigned access, const char **s)
{

	assert(s != NULL);

	switch (access) {
	case DW_ACCESS_public:
		*s = "DW_ACCESS_public"; break;
	case DW_ACCESS_protected:
		*s = "DW_ACCESS_protected"; break;
	case DW_ACCESS_private:
		*s = "DW_ACCESS_private"; break;
	default:
		return (DW_DLV_NO_ENTRY);
	}

	return (DW_DLV_OK);
}

int
dwarf_get_AT_name(unsigned attr, const char **s)
{

	assert(s != NULL);

	switch (attr) {
	case DW_AT_abstract_origin:
		*s = "DW_AT_abstract_origin"; break;
	case DW_AT_accessibility:
		*s = "DW_AT_accessibility"; break;
	case DW_AT_address_class:
		*s = "DW_AT_address_class"; break;
	case DW_AT_artificial:
		*s = "DW_AT_artificial"; break;
	case DW_AT_allocated:
		*s = "DW_AT_allocated"; break;
	case DW_AT_associated:
		*s = "DW_AT_associated"; break;
	case DW_AT_base_types:
		*s = "DW_AT_base_types"; break;
	case DW_AT_binary_scale:
		*s = "DW_AT_binary_scale"; break;
	case DW_AT_bit_offset:
		*s = "DW_AT_bit_offset"; break;
	case DW_AT_bit_size:
		*s = "DW_AT_bit_size"; break;
	case DW_AT_bit_stride:
		*s = "DW_AT_bit_stride"; break;
	case DW_AT_byte_size:
		*s = "DW_AT_byte_size"; break;
	case DW_AT_byte_stride:
		*s = "DW_AT_byte_stride"; break;
	case DW_AT_calling_convention:
		*s = "DW_AT_calling_convention"; break;
	case DW_AT_common_reference:
		*s = "DW_AT_common_reference"; break;
	case DW_AT_comp_dir:
		*s = "DW_AT_comp_dir"; break;
	case DW_AT_const_expr:
		*s = "DW_AT_const_expr"; break;
	case DW_AT_const_value:
		*s = "DW_AT_const_value"; break;
	case DW_AT_containing_type:
		*s = "DW_AT_containing_type"; break;
	case DW_AT_count:
		*s = "DW_AT_count"; break;
	case DW_AT_call_column:
		*s = "DW_AT_call_column"; break;
	case DW_AT_call_file:
		*s = "DW_AT_call_file"; break;
	case DW_AT_call_line:
		*s = "DW_AT_call_line"; break;
	case DW_AT_data_bit_offset:
		*s = "DW_AT_data_bit_offset"; break;
	case DW_AT_data_location:
		*s = "DW_AT_data_location"; break;
	case DW_AT_data_member_location:
		*s = "DW_AT_data_member_location"; break;
	case DW_AT_decl_column:
		*s = "DW_AT_decl_column"; break;
	case DW_AT_decl_file:
		*s = "DW_AT_decl_file"; break;
	case DW_AT_decl_line:
		*s = "DW_AT_decl_line"; break;
	case DW_AT_declaration:
		*s = "DW_AT_declaration"; break;
	case DW_AT_default_value:
		*s = "DW_AT_default_value"; break;
	case DW_AT_decimal_scale:
		*s = "DW_AT_decimal_scale"; break;
	case DW_AT_decimal_sign:
		*s = "DW_AT_decimal_sign"; break;
	case DW_AT_description:
		*s = "DW_AT_description"; break;
	case DW_AT_digit_count:
		*s = "DW_AT_digit_count"; break;
	case DW_AT_discr:
		*s = "DW_AT_discr"; break;
	case DW_AT_discr_list:
		*s = "DW_AT_discr_list"; break;
	case DW_AT_discr_value:
		*s = "DW_AT_discr_value"; break;
	case DW_AT_element_list:
		*s = "DW_AT_element_list"; break;
	case DW_AT_encoding:
		*s = "DW_AT_encoding"; break;
	case DW_AT_enum_class:
		*s = "DW_AT_enum_class"; break;
	case DW_AT_external:
		*s = "DW_AT_external"; break;
	case DW_AT_entry_pc:
		*s = "DW_AT_entry_pc"; break;
	case DW_AT_extension:
		*s = "DW_AT_extension"; break;
	case DW_AT_explicit:
		*s = "DW_AT_explicit"; break;
	case DW_AT_endianity:
		*s = "DW_AT_endianity"; break;
	case DW_AT_elemental:
		*s = "DW_AT_elemental"; break;
	case DW_AT_frame_base:
		*s = "DW_AT_frame_base"; break;
	case DW_AT_friend:
		*s = "DW_AT_friend"; break;
	case DW_AT_high_pc:
		*s = "DW_AT_high_pc"; break;
	case DW_AT_hi_user:
		*s = "DW_AT_hi_user"; break;
	case DW_AT_identifier_case:
		*s = "DW_AT_identifier_case"; break;
	case DW_AT_import:
		*s = "DW_AT_import"; break;
	case DW_AT_inline:
		*s = "DW_AT_inline"; break;
	case DW_AT_is_optional:
		*s = "DW_AT_is_optional"; break;
	case DW_AT_language:
		*s = "DW_AT_language"; break;
	case DW_AT_linkage_name:
		*s = "DW_AT_linkage_name"; break;
	case DW_AT_lo_user:
		*s = "DW_AT_lo_user"; break;
	case DW_AT_location:
		*s = "DW_AT_location"; break;
	case DW_AT_low_pc:
		*s = "DW_AT_low_pc"; break;
	case DW_AT_lower_bound:
		*s = "DW_AT_lower_bound"; break;
	case DW_AT_macro_info:
		*s = "DW_AT_macro_info"; break;
	case DW_AT_main_subprogram:
		*s = "DW_AT_main_subprogram"; break;
	case DW_AT_mutable:
		*s = "DW_AT_mutable"; break;
	case DW_AT_member:
		*s = "DW_AT_member"; break;
	case DW_AT_name:
		*s = "DW_AT_name"; break;
	case DW_AT_namelist_item:
		*s = "DW_AT_namelist_item"; break;
	case DW_AT_ordering:
		*s = "DW_AT_ordering"; break;
	case DW_AT_object_pointer:
		*s = "DW_AT_object_pointer"; break;
	case DW_AT_priority:
		*s = "DW_AT_priority"; break;
	case DW_AT_producer:
		*s = "DW_AT_producer"; break;
	case DW_AT_prototyped:
		*s = "DW_AT_prototyped"; break;
	case DW_AT_picture_string:
		*s = "DW_AT_picture_string"; break;
	case DW_AT_pure:
		*s = "DW_AT_pure"; break;
	case DW_AT_return_addr:
		*s = "DW_AT_return_addr"; break;
	case DW_AT_ranges:
		*s = "DW_AT_ranges"; break;
	case DW_AT_recursive:
		*s = "DW_AT_recursive"; break;
	case DW_AT_segment:
		*s = "DW_AT_segment"; break;
	case DW_AT_sibling:
		*s = "DW_AT_sibling"; break;
	case DW_AT_signature:
		*s = "DW_AT_signature"; break;
	case DW_AT_specification:
		*s = "DW_AT_specification"; break;
	case DW_AT_start_scope:
		*s = "DW_AT_start_scope"; break;
	case DW_AT_static_link:
		*s = "DW_AT_static_link"; break;
	case DW_AT_stmt_list:
		*s = "DW_AT_stmt_list"; break;
	case DW_AT_string_length:
		*s = "DW_AT_string_length"; break;
	case DW_AT_subscr_data:
		*s = "DW_AT_subscr_data"; break;
	case DW_AT_small:
		*s = "DW_AT_small"; break;
	case DW_AT_type:
		*s = "DW_AT_type"; break;
	case DW_AT_trampoline:
		*s = "DW_AT_trampoline"; break;
	case DW_AT_threads_scaled:
		*s = "DW_AT_threads_scaled"; break;
	case DW_AT_upper_bound:
		*s = "DW_AT_upper_bound"; break;
	case DW_AT_use_location:
		*s = "DW_AT_use_location"; break;
	case DW_AT_use_UTF8:
		*s = "DW_AT_use_UTF8"; break;
	case DW_AT_variable_parameter:
		*s = "DW_AT_variable_parameter"; break;
	case DW_AT_virtuality:
		*s = "DW_AT_virtuality"; break;
	case DW_AT_visibility:
		*s = "DW_AT_visibility"; break;
	case DW_AT_vtable_elem_location:
		*s = "DW_AT_vtable_elem_location"; break;
	case DW_AT_sf_names:
		*s = "DW_AT_sf_names"; break;
	case DW_AT_src_info:
		*s = "DW_AT_src_info"; break;
	case DW_AT_mac_info:
		*s = "DW_AT_mac_info"; break;
	case DW_AT_src_coords:
		*s = "DW_AT_src_coords"; break;
	case DW_AT_body_begin:
		*s = "DW_AT_body_begin"; break;
	case DW_AT_body_end:
		*s = "DW_AT_body_end"; break;
	case DW_AT_MIPS_fde:
		*s = "DW_AT_MIPS_fde"; break;
	case DW_AT_MIPS_loop_begin:
		*s = "DW_AT_MIPS_loop_begin"; break;
	case DW_AT_MIPS_tail_loop_begin:
		*s = "DW_AT_MIPS_tail_loop_begin"; break;
	case DW_AT_MIPS_epilog_begin:
		*s = "DW_AT_MIPS_epilog_begin"; break;
	case DW_AT_MIPS_loop_unroll_factor:
		*s = "DW_AT_MIPS_loop_unroll_factor"; break;
	case DW_AT_MIPS_software_pipeline_depth:
		*s = "DW_AT_MIPS_software_pipeline_depth"; break;
	case DW_AT_MIPS_linkage_name:
		*s = "DW_AT_MIPS_linkage_name"; break;
	case DW_AT_MIPS_stride:
		*s = "DW_AT_MIPS_stride"; break;
	case DW_AT_MIPS_abstract_name:
		*s = "DW_AT_MIPS_abstract_name"; break;
	case DW_AT_MIPS_clone_origin:
		*s = "DW_AT_MIPS_clone_origin"; break;
	case DW_AT_MIPS_has_inlines:
		*s = "DW_AT_MIPS_has_inlines"; break;
	case DW_AT_MIPS_stride_byte:
		*s = "DW_AT_MIPS_stride_byte"; break;
	case DW_AT_MIPS_stride_elem:
		*s = "DW_AT_MIPS_stride_elem"; break;
	case DW_AT_MIPS_ptr_dopetype:
		*s = "DW_AT_MIPS_ptr_dopetype"; break;
	case DW_AT_MIPS_allocatable_dopetype:
		*s = "DW_AT_MIPS_allocatable_dopetype"; break;
	case DW_AT_MIPS_assumed_shape_dopetype:
		*s = "DW_AT_MIPS_assumed_shape_dopetype"; break;
	case DW_AT_MIPS_assumed_size:
		*s = "DW_AT_MIPS_assumed_size"; break;
	case DW_AT_GNU_vector:
		*s = "DW_AT_GNU_vector"; break;
	case DW_AT_GNU_guarded_by:
		*s = "DW_AT_GNU_guarded_by"; break;
	case DW_AT_GNU_pt_guarded_by:
		*s = "DW_AT_GNU_pt_guarded_by"; break;
	case DW_AT_GNU_guarded:
		*s = "DW_AT_GNU_guarded"; break;
	case DW_AT_GNU_pt_guarded:
		*s = "DW_AT_GNU_pt_guarded"; break;
	case DW_AT_GNU_locks_excluded:
		*s = "DW_AT_GNU_locks_excluded"; break;
	case DW_AT_GNU_exclusive_locks_required:
		*s = "DW_AT_GNU_exclusive_locks_required"; break;
	case DW_AT_GNU_shared_locks_required:
		*s = "DW_AT_GNU_shared_locks_required"; break;
	case DW_AT_GNU_odr_signature:
		*s = "DW_AT_GNU_odr_signature"; break;
	case DW_AT_GNU_template_name:
		*s = "DW_AT_GNU_template_name"; break;
	case DW_AT_GNU_call_site_value:
		*s = "DW_AT_GNU_call_site_value"; break;
	case DW_AT_GNU_call_site_data_value:
		*s = "DW_AT_GNU_call_site_data_value"; break;
	case DW_AT_GNU_call_site_target:
		*s = "DW_AT_GNU_call_site_target"; break;
	case DW_AT_GNU_call_site_target_clobbered:
		*s = "DW_AT_GNU_call_site_target_clobbered"; break;
	case DW_AT_GNU_tail_call:
		*s = "DW_AT_GNU_tail_call"; break;
	case DW_AT_GNU_all_tail_call_sites:
		*s = "DW_AT_GNU_all_tail_call_sites"; break;
	case DW_AT_GNU_all_call_sites:
		*s = "DW_AT_GNU_all_call_sites"; break;
	case DW_AT_GNU_all_source_call_sites:
		*s = "DW_AT_GNU_all_source_call_sites"; break;
	case DW_AT_APPLE_optimized:
		*s = "DW_AT_APPLE_optimized"; break;
	case DW_AT_APPLE_flags:
		*s = "DW_AT_APPLE_flags"; break;
	case DW_AT_APPLE_isa:
		*s = "DW_AT_APPLE_isa"; break;
	case DW_AT_APPLE_block:
		*s = "DW_AT_APPLE_block"; break;
	case DW_AT_APPLE_major_runtime_vers:
		*s = "DW_AT_APPLE_major_runtime_vers"; break;
	case DW_AT_APPLE_runtime_class:
		*s = "DW_AT_APPLE_runtime_class"; break;
	case DW_AT_APPLE_omit_frame_ptr:
		*s = "DW_AT_APPLE_omit_frame_ptr"; break;
	case DW_AT_APPLE_property_name:
		*s = "DW_AT_APPLE_property_name"; break;
	case DW_AT_APPLE_property_getter:
		*s = "DW_AT_APPLE_property_getter"; break;
	case DW_AT_APPLE_property_setter:
		*s = "DW_AT_APPLE_property_setter"; break;
	case DW_AT_APPLE_property_attribute:
		*s = "DW_AT_APPLE_property_attribute"; break;
	case DW_AT_APPLE_objc_complete_type:
		*s = "DW_AT_APPLE_objc_complete_type"; break;
	case DW_AT_APPLE_property:
		*s = "DW_AT_APPLE_property"; break;
	default:
		return (DW_DLV_NO_ENTRY);
	}

	return (DW_DLV_OK);
}

int
dwarf_get_ATE_name(unsigned ate, const char **s)
{

	assert(s != NULL);

	switch(ate) {
	case DW_ATE_address:
		*s = "DW_ATE_address"; break;
	case DW_ATE_boolean:
		*s = "DW_ATE_boolean"; break;
	case DW_ATE_complex_float:
		*s = "DW_ATE_complex_float"; break;
	case DW_ATE_float:
		*s = "DW_ATE_float"; break;
	case DW_ATE_signed:
		*s = "DW_ATE_signed"; break;
	case DW_ATE_signed_char:
		*s = "DW_ATE_signed_char"; break;
	case DW_ATE_unsigned:
		*s = "DW_ATE_unsigned"; break;
	case DW_ATE_unsigned_char:
		*s = "DW_ATE_unsigned_char"; break;
	case DW_ATE_imaginary_float:
		*s = "DW_ATE_imaginary_float"; break;
	case DW_ATE_packed_decimal:
		*s = "DW_ATE_packed_decimal"; break;
	case DW_ATE_numeric_string:
		*s = "DW_ATE_numeric_string"; break;
	case DW_ATE_edited:
		*s = "DW_ATE_edited"; break;
	case DW_ATE_signed_fixed:
		*s = "DW_ATE_signed_fixed"; break;
	case DW_ATE_unsigned_fixed:
		*s = "DW_ATE_unsigned_fixed"; break;
	case DW_ATE_decimal_float:
		*s = "DW_ATE_decimal_float"; break;
	case DW_ATE_lo_user:
		*s = "DW_ATE_lo_user"; break;
	case DW_ATE_hi_user:
		*s = "DW_ATE_hi_user"; break;
	default:
		return (DW_DLV_NO_ENTRY);
	}

	return (DW_DLV_OK);
}

int
dwarf_get_CC_name(unsigned cc, const char **s)
{

	assert(s != NULL);

	switch (cc) {
	case DW_CC_normal:
		*s = "DW_CC_normal"; break;
	case DW_CC_program:
		*s = "DW_CC_program"; break;
	case DW_CC_nocall:
		*s = "DW_CC_nocall"; break;
	case DW_CC_lo_user:
		*s = "DW_CC_lo_user"; break;
	case DW_CC_hi_user:
		*s = "DW_CC_hi_user"; break;
	default:
		return (DW_DLV_NO_ENTRY);
	}

	return (DW_DLV_OK);
}

int
dwarf_get_CFA_name(unsigned cfa, const char **s)
{

	assert(s != NULL);

	switch (cfa) {
	case DW_CFA_advance_loc:
		*s = "DW_CFA_advance_loc"; break;
	case DW_CFA_offset:
		*s = "DW_CFA_offset"; break;
	case DW_CFA_restore:
		*s = "DW_CFA_restore"; break;
	case DW_CFA_nop:
		*s = "DW_CFA_nop"; break;
	case DW_CFA_set_loc:
		*s = "DW_CFA_set_loc"; break;
	case DW_CFA_advance_loc1:
		*s = "DW_CFA_advance_loc1"; break;
	case DW_CFA_advance_loc2:
		*s = "DW_CFA_advance_loc2"; break;
	case DW_CFA_advance_loc4:
		*s = "DW_CFA_advance_loc4"; break;
	case DW_CFA_offset_extended:
		*s = "DW_CFA_offset_extended"; break;
	case DW_CFA_restore_extended:
		*s = "DW_CFA_restore_extended"; break;
	case DW_CFA_undefined:
		*s = "DW_CFA_undefined"; break;
	case DW_CFA_same_value:
		*s = "DW_CFA_same_value"; break;
	case DW_CFA_register:
		*s = "DW_CFA_register"; break;
	case DW_CFA_remember_state:
		*s = "DW_CFA_remember_state"; break;
	case DW_CFA_restore_state:
		*s = "DW_CFA_restore_state"; break;
	case DW_CFA_def_cfa:
		*s = "DW_CFA_def_cfa"; break;
	case DW_CFA_def_cfa_register:
		*s = "DW_CFA_def_cfa_register"; break;
	case DW_CFA_def_cfa_offset:
		*s = "DW_CFA_def_cfa_offset"; break;
	case DW_CFA_def_cfa_expression:
		*s = "DW_CFA_def_cfa_expression"; break;
	case DW_CFA_expression:
		*s = "DW_CFA_expression"; break;
	case DW_CFA_offset_extended_sf:
		*s = "DW_CFA_offset_extended_sf"; break;
	case DW_CFA_def_cfa_sf:
		*s = "DW_CFA_def_cfa_sf"; break;
	case DW_CFA_def_cfa_offset_sf:
		*s = "DW_CFA_def_cfa_offset_sf"; break;
	case DW_CFA_val_offset:
		*s = "DW_CFA_val_offset"; break;
	case DW_CFA_val_offset_sf:
		*s = "DW_CFA_val_offset_sf"; break;
	case DW_CFA_val_expression:
		*s = "DW_CFA_val_expression"; break;
	case DW_CFA_lo_user:
		*s = "DW_CFA_lo_user"; break;
	case DW_CFA_high_user:
		*s = "DW_CFA_high_user"; break;
	default:
		return (DW_DLV_NO_ENTRY);
	}

	return (DW_DLV_OK);
}

int
dwarf_get_CHILDREN_name(unsigned children, const char **s)
{

	assert(s != NULL);

	switch (children) {
	case DW_CHILDREN_no:
		*s = "DW_CHILDREN_no"; break;
	case DW_CHILDREN_yes:
		*s = "DW_CHILDREN_yes"; break;
	default:
		return (DW_DLV_NO_ENTRY);
	}

	return (DW_DLV_OK);
}

int
dwarf_get_FORM_name(unsigned form, const char **s)
{

	assert(s != NULL);

	switch (form) {
	case DW_FORM_addr:
		*s = "DW_FORM_addr"; break;
	case DW_FORM_block:
		*s = "DW_FORM_block"; break;
	case DW_FORM_block1:
		*s = "DW_FORM_block1"; break;
	case DW_FORM_block2:
		*s = "DW_FORM_block2"; break;
	case DW_FORM_block4:
		*s = "DW_FORM_block4"; break;
	case DW_FORM_data1:
		*s = "DW_FORM_data1"; break;
	case DW_FORM_data2:
		*s = "DW_FORM_data2"; break;
	case DW_FORM_data4:
		*s = "DW_FORM_data4"; break;
	case DW_FORM_data8:
		*s = "DW_FORM_data8"; break;
	case DW_FORM_exprloc:
		*s = "DW_FORM_exprloc"; break;
	case DW_FORM_flag:
		*s = "DW_FORM_flag"; break;
	case DW_FORM_flag_present:
		*s = "DW_FORM_flag_present"; break;
	case DW_FORM_indirect:
		*s = "DW_FORM_indirect"; break;
	case DW_FORM_ref1:
		*s = "DW_FORM_ref1"; break;
	case DW_FORM_ref2:
		*s = "DW_FORM_ref2"; break;
	case DW_FORM_ref4:
		*s = "DW_FORM_ref4"; break;
	case DW_FORM_ref8:
		*s = "DW_FORM_ref8"; break;
	case DW_FORM_ref_addr:
		*s = "DW_FORM_ref_addr"; break;
	case DW_FORM_ref_sig8:
		*s = "DW_FORM_ref_sig8"; break;
	case DW_FORM_ref_udata:
		*s = "DW_FORM_ref_udata"; break;
	case DW_FORM_sdata:
		*s = "DW_FORM_sdata"; break;
	case DW_FORM_sec_offset:
		*s = "DW_FORM_sec_offset"; break;
	case DW_FORM_string:
		*s = "DW_FORM_string"; break;
	case DW_FORM_strp:
		*s = "DW_FORM_strp"; break;
	case DW_FORM_udata:
		*s = "DW_FORM_udata"; break;
	default:
		return (DW_DLV_NO_ENTRY);
	}

	return (DW_DLV_OK);
}

int
dwarf_get_DS_name(unsigned ds, const char **s)
{

	assert(s != NULL);

	switch (ds) {
	case DW_DS_unsigned:
		*s = "DW_DS_unsigned"; break;
	case DW_DS_leading_overpunch:
		*s = "DW_DS_leading_overpunch"; break;
	case DW_DS_trailing_overpunch:
		*s = "DW_DS_trailing_overpunch"; break;
	case DW_DS_leading_separate:
		*s = "DW_DS_leading_separate"; break;
	case DW_DS_trailing_separate:
		*s = "DW_DS_trailing_separate";
	default:
		return (DW_DLV_NO_ENTRY);
	}

	return (DW_DLV_OK);
}

int
dwarf_get_DSC_name(unsigned dsc, const char **s)
{

	assert(s != NULL);

	switch (dsc) {
	case DW_DSC_label:
		*s = "DW_DSC_label"; break;
	case DW_DSC_range:
		*s = "DW_DSC_range"; break;
	default:
		return (DW_DLV_NO_ENTRY);
	}

	return (DW_DLV_OK);
}

int
dwarf_get_EH_name(unsigned eh, const char **s)
{

	assert(s != NULL);

	switch (eh) {
	case DW_EH_PE_absptr:
		*s = "DW_EH_PE_absptr"; break;
	case DW_EH_PE_uleb128:
		*s = "DW_EH_PE_uleb128"; break;
	case DW_EH_PE_udata2:
		*s = "DW_EH_PE_udata2"; break;
	case DW_EH_PE_udata4:
		*s = "DW_EH_PE_udata4"; break;
	case DW_EH_PE_udata8:
		*s = "DW_EH_PE_udata8"; break;
	case DW_EH_PE_sleb128:
		*s = "DW_EH_PE_sleb128"; break;
	case DW_EH_PE_sdata2:
		*s = "DW_EH_PE_sdata2"; break;
	case DW_EH_PE_sdata4:
		*s = "DW_EH_PE_sdata4"; break;
	case DW_EH_PE_sdata8:
		*s = "DW_EH_PE_sdata8"; break;
	case DW_EH_PE_pcrel:
		*s = "DW_EH_PE_pcrel"; break;
	case DW_EH_PE_textrel:
		*s = "DW_EH_PE_textrel"; break;
	case DW_EH_PE_datarel:
		*s = "DW_EH_PE_datarel"; break;
	case DW_EH_PE_funcrel:
		*s = "DW_EH_PE_funcrel"; break;
	case DW_EH_PE_aligned:
		*s = "DW_EH_PE_aligned"; break;
	case DW_EH_PE_omit:
		*s = "DW_EH_PE_omit"; break;
	default:
		return (DW_DLV_NO_ENTRY);
	}

	return (DW_DLV_OK);
}

int
dwarf_get_END_name(unsigned end, const char **s)
{

	assert(s != NULL);

	switch (end) {
	case DW_END_default:
		*s = "DW_END_default"; break;
	case DW_END_big:
		*s = "DW_END_big"; break;
	case DW_END_little:
		*s = "DW_END_little"; break;
	case DW_END_lo_user:
		*s = "DW_END_lo_user"; break;
	case DW_END_high_user:
		*s = "DW_END_high_user"; break;
	default:
		return (DW_DLV_NO_ENTRY);
	}

	return (DW_DLV_OK);
}

int
dwarf_get_ID_name(unsigned id, const char **s)
{

	assert(s != NULL);

	switch (id) {
	case DW_ID_case_sensitive:
		*s = "DW_ID_case_sensitive"; break;
	case DW_ID_up_case:
		*s = "DW_ID_up_case"; break;
	case DW_ID_down_case:
		*s = "DW_ID_down_case"; break;
	case DW_ID_case_insensitive:
		*s = "DW_ID_case_insensitive"; break;
	default:
		return (DW_DLV_NO_ENTRY);
	}

	return (DW_DLV_OK);
}

int
dwarf_get_INL_name(unsigned inl, const char **s)
{

	assert(s != NULL);

	switch (inl) {
	case DW_INL_not_inlined:
		*s = "DW_INL_not_inlined"; break;
	case DW_INL_inlined:
		*s = "DW_INL_inlined"; break;
	case DW_INL_declared_not_inlined:
		*s = "DW_INL_declared_not_inlined"; break;
	case DW_INL_declared_inlined:
		*s = "DW_INL_declared_inlined"; break;
	default:
		return (DW_DLV_NO_ENTRY);
	}

	return (DW_DLV_OK);
}

int
dwarf_get_LANG_name(unsigned lang, const char **s)
{

	assert(s != NULL);

	switch (lang) {
	case DW_LANG_C89:
		*s = "DW_LANG_C89"; break;
	case DW_LANG_C:
		*s = "DW_LANG_C"; break;
	case DW_LANG_Ada83:
		*s = "DW_LANG_Ada83"; break;
	case DW_LANG_C_plus_plus:
		*s = "DW_LANG_C_plus_plus"; break;
	case DW_LANG_Cobol74:
		*s = "DW_LANG_Cobol74"; break;
	case DW_LANG_Cobol85:
		*s = "DW_LANG_Cobol85"; break;
	case DW_LANG_Fortran77:
		*s = "DW_LANG_Fortran77"; break;
	case DW_LANG_Fortran90:
		*s = "DW_LANG_Fortran90"; break;
	case DW_LANG_Pascal83:
		*s = "DW_LANG_Pascal83"; break;
	case DW_LANG_Modula2:
		*s = "DW_LANG_Modula2"; break;
	case DW_LANG_Java:
		*s = "DW_LANG_Java"; break;
	case DW_LANG_C99:
		*s = "DW_LANG_C99"; break;
	case DW_LANG_Ada95:
		*s = "DW_LANG_Ada95"; break;
	case DW_LANG_Fortran95:
		*s = "DW_LANG_Fortran95"; break;
	case DW_LANG_PLI:
		*s = "DW_LANG_PLI"; break;
	case DW_LANG_ObjC:
		*s = "DW_LANG_ObjC"; break;
	case DW_LANG_ObjC_plus_plus:
		*s = "DW_LANG_ObjC_plus_plus"; break;
	case DW_LANG_UPC:
		*s = "DW_LANG_UPC"; break;
	case DW_LANG_D:
		*s = "DW_LANG_D"; break;
	case DW_LANG_Python:
		*s = "DW_LANG_Python"; break;
	case DW_LANG_OpenCL:
		*s = "DW_LANG_OpenCL"; break;
	case DW_LANG_Go:
		*s = "DW_LANG_Go"; break;
	case DW_LANG_Modula3:
		*s = "DW_LANG_Modula3"; break;
	case DW_LANG_Haskell:
		*s = "DW_LANG_Haskell"; break;
	case DW_LANG_C_plus_plus_03:
		*s = "DW_LANG_C_plus_plus_03"; break;
	case DW_LANG_C_plus_plus_11:
		*s = "DW_LANG_C_plus_plus_11"; break;
	case DW_LANG_OCaml:
		*s = "DW_LANG_OCaml"; break;
	case DW_LANG_Rust:
		*s = "DW_LANG_Rust"; break;
	case DW_LANG_C11:
		*s = "DW_LANG_C11"; break;
	case DW_LANG_Swift:
		*s = "DW_LANG_Swift"; break;
	case DW_LANG_Julia:
		*s = "DW_LANG_Julia"; break;
	case DW_LANG_Dylan:
		*s = "DW_LANG_Dylan"; break;
	case DW_LANG_C_plus_plus_14:
		*s = "DW_LANG_C_plus_plus_14"; break;
	case DW_LANG_Fortran03:
		*s = "DW_LANG_Fortran03"; break;
	case DW_LANG_Fortran08:
		*s = "DW_LANG_Fortran08"; break;
	case DW_LANG_RenderScript:
		*s = "DW_LANG_RenderScript"; break;
	case DW_LANG_BLISS:
		*s = "DW_LANG_BLISS"; break;
	case DW_LANG_lo_user:
		*s = "DW_LANG_lo_user"; break;
	case DW_LANG_Mips_Assembler:
		*s = "DW_LANG_Mips_Assembler"; break;
	case DW_LANG_hi_user:
		*s = "DW_LANG_hi_user"; break;
	default:
		return (DW_DLV_NO_ENTRY);
	}

	return (DW_DLV_OK);
}

int
dwarf_get_LNE_name(unsigned lne, const char **s)
{

	assert(s != NULL);

	switch (lne) {
	case DW_LNE_end_sequence:
		*s = "DW_LNE_end_sequence"; break;
	case DW_LNE_set_address:
		*s = "DW_LNE_set_address"; break;
	case DW_LNE_define_file:
		*s = "DW_LNE_define_file"; break;
	case DW_LNE_lo_user:
		*s = "DW_LNE_lo_user"; break;
	case DW_LNE_hi_user:
		*s = "DW_LNE_hi_user"; break;
	default:
		return (DW_DLV_NO_ENTRY);
	}

	return (DW_DLV_OK);
}

int
dwarf_get_LNS_name(unsigned lns, const char **s)
{

	assert(s != NULL);

	switch (lns) {
	case DW_LNS_copy:
		*s = "DW_LNS_copy"; break;
	case DW_LNS_advance_pc:
		*s = "DW_LNS_advance_pc"; break;
	case DW_LNS_advance_line:
		*s = "DW_LNS_advance_line"; break;
	case DW_LNS_set_file:
		*s = "DW_LNS_set_file"; break;
	case DW_LNS_set_column:
		*s = "DW_LNS_set_column"; break;
	case DW_LNS_negate_stmt:
		*s = "DW_LNS_negate_stmt"; break;
	case DW_LNS_set_basic_block:
		*s = "DW_LNS_set_basic_block"; break;
	case DW_LNS_const_add_pc:
		*s = "DW_LNS_const_add_pc"; break;
	case DW_LNS_fixed_advance_pc:
		*s = "DW_LNS_fixed_advance_pc"; break;
	case DW_LNS_set_prologue_end:
		*s = "DW_LNS_set_prologue_end"; break;
	case DW_LNS_set_epilogue_begin:
		*s = "DW_LNS_set_epilogue_begin"; break;
	case DW_LNS_set_isa:
		*s = "DW_LNS_set_isa"; break;
	default:
		return (DW_DLV_NO_ENTRY);
	}

	return (DW_DLV_OK);
}

int
dwarf_get_MACINFO_name(unsigned mi, const char **s)
{

	assert(s != NULL);

	switch (mi) {
	case DW_MACINFO_define:
		*s = "DW_MACINFO_define"; break;
	case DW_MACINFO_undef:
		*s = "DW_MACINFO_undef"; break;
	case DW_MACINFO_start_file:
		*s = "DW_MACINFO_start_file"; break;
	case DW_MACINFO_end_file:
		*s = "DW_MACINFO_end_file"; break;
	case DW_MACINFO_vendor_ext:
		*s = "DW_MACINFO_vendor_ext"; break;
	default:
		return (DW_DLV_NO_ENTRY);
	}

	return (DW_DLV_OK);
}

int
dwarf_get_OP_name(unsigned op, const char **s)
{

	assert(s != NULL);

	switch (op) {
	case DW_OP_deref:
		*s = "DW_OP_deref"; break;
	case DW_OP_reg0:
		*s = "DW_OP_reg0"; break;
	case DW_OP_reg1:
		*s = "DW_OP_reg1"; break;
	case DW_OP_reg2:
		*s = "DW_OP_reg2"; break;
	case DW_OP_reg3:
		*s = "DW_OP_reg3"; break;
	case DW_OP_reg4:
		*s = "DW_OP_reg4"; break;
	case DW_OP_reg5:
		*s = "DW_OP_reg5"; break;
	case DW_OP_reg6:
		*s = "DW_OP_reg6"; break;
	case DW_OP_reg7:
		*s = "DW_OP_reg7"; break;
	case DW_OP_reg8:
		*s = "DW_OP_reg8"; break;
	case DW_OP_reg9:
		*s = "DW_OP_reg9"; break;
	case DW_OP_reg10:
		*s = "DW_OP_reg10"; break;
	case DW_OP_reg11:
		*s = "DW_OP_reg11"; break;
	case DW_OP_reg12:
		*s = "DW_OP_reg12"; break;
	case DW_OP_reg13:
		*s = "DW_OP_reg13"; break;
	case DW_OP_reg14:
		*s = "DW_OP_reg14"; break;
	case DW_OP_reg15:
		*s = "DW_OP_reg15"; break;
	case DW_OP_reg16:
		*s = "DW_OP_reg16"; break;
	case DW_OP_reg17:
		*s = "DW_OP_reg17"; break;
	case DW_OP_reg18:
		*s = "DW_OP_reg18"; break;
	case DW_OP_reg19:
		*s = "DW_OP_reg19"; break;
	case DW_OP_reg20:
		*s = "DW_OP_reg20"; break;
	case DW_OP_reg21:
		*s = "DW_OP_reg21"; break;
	case DW_OP_reg22:
		*s = "DW_OP_reg22"; break;
	case DW_OP_reg23:
		*s = "DW_OP_reg23"; break;
	case DW_OP_reg24:
		*s = "DW_OP_reg24"; break;
	case DW_OP_reg25:
		*s = "DW_OP_reg25"; break;
	case DW_OP_reg26:
		*s = "DW_OP_reg26"; break;
	case DW_OP_reg27:
		*s = "DW_OP_reg27"; break;
	case DW_OP_reg28:
		*s = "DW_OP_reg28"; break;
	case DW_OP_reg29:
		*s = "DW_OP_reg29"; break;
	case DW_OP_reg30:
		*s = "DW_OP_reg30"; break;
	case DW_OP_reg31:
		*s = "DW_OP_reg31"; break;
	case DW_OP_lit0:
		*s = "DW_OP_lit0"; break;
	case DW_OP_lit1:
		*s = "DW_OP_lit1"; break;
	case DW_OP_lit2:
		*s = "DW_OP_lit2"; break;
	case DW_OP_lit3:
		*s = "DW_OP_lit3"; break;
	case DW_OP_lit4:
		*s = "DW_OP_lit4"; break;
	case DW_OP_lit5:
		*s = "DW_OP_lit5"; break;
	case DW_OP_lit6:
		*s = "DW_OP_lit6"; break;
	case DW_OP_lit7:
		*s = "DW_OP_lit7"; break;
	case DW_OP_lit8:
		*s = "DW_OP_lit8"; break;
	case DW_OP_lit9:
		*s = "DW_OP_lit9"; break;
	case DW_OP_lit10:
		*s = "DW_OP_lit10"; break;
	case DW_OP_lit11:
		*s = "DW_OP_lit11"; break;
	case DW_OP_lit12:
		*s = "DW_OP_lit12"; break;
	case DW_OP_lit13:
		*s = "DW_OP_lit13"; break;
	case DW_OP_lit14:
		*s = "DW_OP_lit14"; break;
	case DW_OP_lit15:
		*s = "DW_OP_lit15"; break;
	case DW_OP_lit16:
		*s = "DW_OP_lit16"; break;
	case DW_OP_lit17:
		*s = "DW_OP_lit17"; break;
	case DW_OP_lit18:
		*s = "DW_OP_lit18"; break;
	case DW_OP_lit19:
		*s = "DW_OP_lit19"; break;
	case DW_OP_lit20:
		*s = "DW_OP_lit20"; break;
	case DW_OP_lit21:
		*s = "DW_OP_lit21"; break;
	case DW_OP_lit22:
		*s = "DW_OP_lit22"; break;
	case DW_OP_lit23:
		*s = "DW_OP_lit23"; break;
	case DW_OP_lit24:
		*s = "DW_OP_lit24"; break;
	case DW_OP_lit25:
		*s = "DW_OP_lit25"; break;
	case DW_OP_lit26:
		*s = "DW_OP_lit26"; break;
	case DW_OP_lit27:
		*s = "DW_OP_lit27"; break;
	case DW_OP_lit28:
		*s = "DW_OP_lit28"; break;
	case DW_OP_lit29:
		*s = "DW_OP_lit29"; break;
	case DW_OP_lit30:
		*s = "DW_OP_lit30"; break;
	case DW_OP_lit31:
		*s = "DW_OP_lit31"; break;
	case DW_OP_dup:
		*s = "DW_OP_dup"; break;
	case DW_OP_drop:
		*s = "DW_OP_drop"; break;
	case DW_OP_over:
		*s = "DW_OP_over"; break;
	case DW_OP_swap:
		*s = "DW_OP_swap"; break;
	case DW_OP_rot:
		*s = "DW_OP_rot"; break;
	case DW_OP_xderef:
		*s = "DW_OP_xderef"; break;
	case DW_OP_abs:
		*s = "DW_OP_abs"; break;
	case DW_OP_and:
		*s = "DW_OP_and"; break;
	case DW_OP_div:
		*s = "DW_OP_div"; break;
	case DW_OP_minus:
		*s = "DW_OP_minus"; break;
	case DW_OP_mod:
		*s = "DW_OP_mod"; break;
	case DW_OP_mul:
		*s = "DW_OP_mul"; break;
	case DW_OP_neg:
		*s = "DW_OP_neg"; break;
	case DW_OP_not:
		*s = "DW_OP_not"; break;
	case DW_OP_or:
		*s = "DW_OP_or"; break;
	case DW_OP_plus:
		*s = "DW_OP_plus"; break;
	case DW_OP_shl:
		*s = "DW_OP_shl"; break;
	case DW_OP_shr:
		*s = "DW_OP_shr"; break;
	case DW_OP_shra:
		*s = "DW_OP_shra"; break;
	case DW_OP_xor:
		*s = "DW_OP_xor"; break;
	case DW_OP_eq:
		*s = "DW_OP_eq"; break;
	case DW_OP_ge:
		*s = "DW_OP_ge"; break;
	case DW_OP_gt:
		*s = "DW_OP_gt"; break;
	case DW_OP_le:
		*s = "DW_OP_le"; break;
	case DW_OP_lt:
		*s = "DW_OP_lt"; break;
	case DW_OP_ne:
		*s = "DW_OP_ne"; break;
	case DW_OP_nop:
		*s = "DW_OP_nop"; break;
	case DW_OP_const1u:
		*s = "DW_OP_const1u"; break;
	case DW_OP_const1s:
		*s = "DW_OP_const1s"; break;
	case DW_OP_pick:
		*s = "DW_OP_pick"; break;
	case DW_OP_deref_size:
		*s = "DW_OP_deref_size"; break;
	case DW_OP_xderef_size:
		*s = "DW_OP_xderef_size"; break;
	case DW_OP_const2u:
		*s = "DW_OP_const2u"; break;
	case DW_OP_const2s:
		*s = "DW_OP_const2s"; break;
	case DW_OP_bra:
		*s = "DW_OP_bra"; break;
	case DW_OP_skip:
		*s = "DW_OP_skip"; break;
	case DW_OP_const4u:
		*s = "DW_OP_const4u"; break;
	case DW_OP_const4s:
		*s = "DW_OP_const4s"; break;
	case DW_OP_const8u:
		*s = "DW_OP_const8u"; break;
	case DW_OP_const8s:
		*s = "DW_OP_const8s"; break;
	case DW_OP_constu:
		*s = "DW_OP_constu"; break;
	case DW_OP_plus_uconst:
		*s = "DW_OP_plus_uconst"; break;
	case DW_OP_regx:
		*s = "DW_OP_regx"; break;
	case DW_OP_piece:
		*s = "DW_OP_piece"; break;
	case DW_OP_consts:
		*s = "DW_OP_consts"; break;
	case DW_OP_breg0:
		*s = "DW_OP_breg0"; break;
	case DW_OP_breg1:
		*s = "DW_OP_breg1"; break;
	case DW_OP_breg2:
		*s = "DW_OP_breg2"; break;
	case DW_OP_breg3:
		*s = "DW_OP_breg3"; break;
	case DW_OP_breg4:
		*s = "DW_OP_breg4"; break;
	case DW_OP_breg5:
		*s = "DW_OP_breg5"; break;
	case DW_OP_breg6:
		*s = "DW_OP_breg6"; break;
	case DW_OP_breg7:
		*s = "DW_OP_breg7"; break;
	case DW_OP_breg8:
		*s = "DW_OP_breg8"; break;
	case DW_OP_breg9:
		*s = "DW_OP_breg9"; break;
	case DW_OP_breg10:
		*s = "DW_OP_breg10"; break;
	case DW_OP_breg11:
		*s = "DW_OP_breg11"; break;
	case DW_OP_breg12:
		*s = "DW_OP_breg12"; break;
	case DW_OP_breg13:
		*s = "DW_OP_breg13"; break;
	case DW_OP_breg14:
		*s = "DW_OP_breg14"; break;
	case DW_OP_breg15:
		*s = "DW_OP_breg15"; break;
	case DW_OP_breg16:
		*s = "DW_OP_breg16"; break;
	case DW_OP_breg17:
		*s = "DW_OP_breg17"; break;
	case DW_OP_breg18:
		*s = "DW_OP_breg18"; break;
	case DW_OP_breg19:
		*s = "DW_OP_breg19"; break;
	case DW_OP_breg20:
		*s = "DW_OP_breg20"; break;
	case DW_OP_breg21:
		*s = "DW_OP_breg21"; break;
	case DW_OP_breg22:
		*s = "DW_OP_breg22"; break;
	case DW_OP_breg23:
		*s = "DW_OP_breg23"; break;
	case DW_OP_breg24:
		*s = "DW_OP_breg24"; break;
	case DW_OP_breg25:
		*s = "DW_OP_breg25"; break;
	case DW_OP_breg26:
		*s = "DW_OP_breg26"; break;
	case DW_OP_breg27:
		*s = "DW_OP_breg27"; break;
	case DW_OP_breg28:
		*s = "DW_OP_breg28"; break;
	case DW_OP_breg29:
		*s = "DW_OP_breg29"; break;
	case DW_OP_breg30:
		*s = "DW_OP_breg30"; break;
	case DW_OP_breg31:
		*s = "DW_OP_breg31"; break;
	case DW_OP_fbreg:
		*s = "DW_OP_fbreg"; break;
	case DW_OP_bregx:
		*s = "DW_OP_bregx"; break;
	case DW_OP_addr:
		*s = "DW_OP_addr"; break;
	case DW_OP_push_object_address:
		*s = "DW_OP_push_object_address"; break;
	case DW_OP_call2:
		*s = "DW_OP_call2"; break;
	case DW_OP_call4:
		*s = "DW_OP_call4"; break;
	case DW_OP_call_ref:
		*s = "DW_OP_call_ref"; break;
	case DW_OP_form_tls_address:
		*s = "DW_OP_form_tls_address"; break;
	case DW_OP_call_frame_cfa:
		*s = "DW_OP_call_frame_cfa"; break;
	case DW_OP_bit_piece:
		*s = "DW_OP_bit_piece"; break;
	case DW_OP_implicit_value:
		*s = "DW_OP_implicit_value"; break;
	case DW_OP_stack_value:
		*s = "DW_OP_stack_value"; break;
	case DW_OP_GNU_push_tls_address:
		*s = "DW_OP_GNU_push_tls_address"; break;
	case DW_OP_GNU_uninit:
		*s = "DW_OP_GNU_uninit"; break;
	case DW_OP_GNU_encoded_addr:
		*s = "DW_OP_GNU_encoded_addr"; break;
	case DW_OP_GNU_implicit_pointer:
		*s = "DW_OP_GNU_implicit_pointer"; break;
	case DW_OP_GNU_entry_value:
		*s = "DW_OP_GNU_entry_value"; break;
	case DW_OP_GNU_const_type:
		*s = "DW_OP_GNU_const_type"; break;
	case DW_OP_GNU_regval_type:
		*s = "DW_OP_GNU_regval_type"; break;
	case DW_OP_GNU_deref_type:
		*s = "DW_OP_GNU_deref_type"; break;
	case DW_OP_GNU_convert:
		*s = "DW_OP_GNU_convert"; break;
	case DW_OP_GNU_reinterpret:
		*s = "DW_OP_GNU_reinterpret"; break;
	case DW_OP_GNU_parameter_ref:
		*s = "DW_OP_GNU_parameter_ref"; break;
	case DW_OP_GNU_addr_index:
		*s = "DW_OP_GNU_addr_index"; break;
	case DW_OP_GNU_const_index:
		*s = "DW_OP_GNU_const_index"; break;
	default:
		return (DW_DLV_NO_ENTRY);
	}

	return (DW_DLV_OK);
}

int
dwarf_get_ORD_name(unsigned ord, const char **s)
{

	assert(s != NULL);

	switch (ord) {
	case DW_ORD_row_major:
		*s = "DW_ORD_row_major"; break;
	case DW_ORD_col_major:
		*s = "DW_ORD_col_major"; break;
	default:
		return (DW_DLV_NO_ENTRY);
	}

	return (DW_DLV_OK);
}

int
dwarf_get_TAG_name(unsigned tag, const char **s)
{

	assert(s != NULL);

	switch (tag) {
	case DW_TAG_access_declaration:
		*s = "DW_TAG_access_declaration"; break;
	case DW_TAG_array_type:
		*s = "DW_TAG_array_type"; break;
	case DW_TAG_base_type:
		*s = "DW_TAG_base_type"; break;
	case DW_TAG_catch_block:
		*s = "DW_TAG_catch_block"; break;
	case DW_TAG_class_type:
		*s = "DW_TAG_class_type"; break;
	case DW_TAG_common_block:
		*s = "DW_TAG_common_block"; break;
	case DW_TAG_common_inclusion:
		*s = "DW_TAG_common_inclusion"; break;
	case DW_TAG_compile_unit:
		*s = "DW_TAG_compile_unit"; break;
	case DW_TAG_condition:
		*s = "DW_TAG_condition"; break;
	case DW_TAG_const_type:
		*s = "DW_TAG_const_type"; break;
	case DW_TAG_constant:
		*s = "DW_TAG_constant"; break;
	case DW_TAG_dwarf_procedure:
		*s = "DW_TAG_dwarf_procedure"; break;
	case DW_TAG_entry_point:
		*s = "DW_TAG_entry_point"; break;
	case DW_TAG_enumeration_type:
		*s = "DW_TAG_enumeration_type"; break;
	case DW_TAG_enumerator:
		*s = "DW_TAG_enumerator"; break;
	case DW_TAG_formal_parameter:
		*s = "DW_TAG_formal_parameter"; break;
	case DW_TAG_friend:
		*s = "DW_TAG_friend"; break;
	case DW_TAG_imported_declaration:
		*s = "DW_TAG_imported_declaration"; break;
	case DW_TAG_imported_module:
		*s = "DW_TAG_imported_module"; break;
	case DW_TAG_imported_unit:
		*s = "DW_TAG_imported_unit"; break;
	case DW_TAG_inheritance:
		*s = "DW_TAG_inheritance"; break;
	case DW_TAG_inlined_subroutine:
		*s = "DW_TAG_inlined_subroutine"; break;
	case DW_TAG_interface_type:
		*s = "DW_TAG_interface_type"; break;
	case DW_TAG_label:
		*s = "DW_TAG_label"; break;
	case DW_TAG_lexical_block:
		*s = "DW_TAG_lexical_block"; break;
	case DW_TAG_member:
		*s = "DW_TAG_member"; break;
	case DW_TAG_module:
		*s = "DW_TAG_module"; break;
	case DW_TAG_namelist:
		*s = "DW_TAG_namelist"; break;
	case DW_TAG_namelist_item:
		*s = "DW_TAG_namelist_item"; break;
	case DW_TAG_namespace:
		*s = "DW_TAG_namespace"; break;
	case DW_TAG_packed_type:
		*s = "DW_TAG_packed_type"; break;
	case DW_TAG_partial_unit:
		*s = "DW_TAG_partial_unit"; break;
	case DW_TAG_pointer_type:
		*s = "DW_TAG_pointer_type"; break;
	case DW_TAG_ptr_to_member_type:
		*s = "DW_TAG_ptr_to_member_type"; break;
	case DW_TAG_reference_type:
		*s = "DW_TAG_reference_type"; break;
	case DW_TAG_restrict_type:
		*s = "DW_TAG_restrict_type"; break;
	case DW_TAG_rvalue_reference_type:
		*s = "DW_TAG_rvalue_reference_type"; break;
	case DW_TAG_set_type:
		*s = "DW_TAG_set_type"; break;
	case DW_TAG_shared_type:
		*s = "DW_TAG_shared_type"; break;
	case DW_TAG_string_type:
		*s = "DW_TAG_string_type"; break;
	case DW_TAG_structure_type:
		*s = "DW_TAG_structure_type"; break;
	case DW_TAG_subprogram:
		*s = "DW_TAG_subprogram"; break;
	case DW_TAG_subrange_type:
		*s = "DW_TAG_subrange_type"; break;
	case DW_TAG_subroutine_type:
		*s = "DW_TAG_subroutine_type"; break;
	case DW_TAG_template_alias:
		*s = "DW_TAG_template_alias"; break;
	case DW_TAG_template_type_parameter:
		*s = "DW_TAG_template_type_parameter"; break;
	case DW_TAG_template_value_parameter:
		*s = "DW_TAG_template_value_parameter"; break;
	case DW_TAG_thrown_type:
		*s = "DW_TAG_thrown_type"; break;
	case DW_TAG_try_block:
		*s = "DW_TAG_try_block"; break;
	case DW_TAG_type_unit:
		*s = "DW_TAG_type_unit"; break;
	case DW_TAG_typedef:
		*s = "DW_TAG_typedef"; break;
	case DW_TAG_union_type:
		*s = "DW_TAG_union_type"; break;
	case DW_TAG_unspecified_parameters:
		*s = "DW_TAG_unspecified_parameters"; break;
	case DW_TAG_unspecified_type:
		*s = "DW_TAG_unspecified_type"; break;
	case DW_TAG_variable:
		*s = "DW_TAG_variable"; break;
	case DW_TAG_variant:
		*s = "DW_TAG_variant"; break;
	case DW_TAG_variant_part:
		*s = "DW_TAG_variant_part"; break;
	case DW_TAG_volatile_type:
		*s = "DW_TAG_volatile_type"; break;
	case DW_TAG_with_stmt:
		*s = "DW_TAG_with_stmt"; break;
	case DW_TAG_format_label:
		*s = "DW_TAG_format_label"; break;
	case DW_TAG_function_template:
		*s = "DW_TAG_function_template"; break;
	case DW_TAG_class_template:
		*s = "DW_TAG_class_template"; break;
	case DW_TAG_GNU_BINCL:
		*s = "DW_TAG_GNU_BINCL"; break;
	case DW_TAG_GNU_EINCL:
		*s = "DW_TAG_GNU_EINCL"; break;
	case DW_TAG_GNU_template_template_param:
		*s = "DW_TAG_GNU_template_template_param"; break;
	case DW_TAG_GNU_template_parameter_pack:
		*s = "DW_TAG_GNU_template_parameter_pack"; break;
	case DW_TAG_GNU_formal_parameter_pack:
		*s = "DW_TAG_GNU_formal_parameter_pack"; break;
	case DW_TAG_GNU_call_site:
		*s = "DW_TAG_GNU_call_site"; break;
	case DW_TAG_GNU_call_site_parameter:
		*s = "DW_TAG_GNU_call_site_parameter"; break;
	default:
		return (DW_DLV_NO_ENTRY);
	}

	return (DW_DLV_OK);
}

int
dwarf_get_VIRTUALITY_name(unsigned vir, const char **s)
{

	assert(s != NULL);

	switch (vir) {
	case DW_VIRTUALITY_none:
		*s = "DW_VIRTUALITY_none"; break;
	case DW_VIRTUALITY_virtual:
		*s = "DW_VIRTUALITY_virtual"; break;
	case DW_VIRTUALITY_pure_virtual:
		*s = "DW_VIRTUALITY_pure_virtual"; break;
	default:
		return (DW_DLV_NO_ENTRY);
	}

	return (DW_DLV_OK);
}

int
dwarf_get_VIS_name(unsigned vis, const char **s)
{

	assert(s != NULL);

	switch (vis) {
	case DW_VIS_local:
		*s = "DW_VIS_local"; break;
	case DW_VIS_exported:
		*s = "DW_VIS_exported"; break;
	case DW_VIS_qualified:
		*s = "DW_VIS_qualified"; break;
	default:
		return (DW_DLV_NO_ENTRY);
	}

	return (DW_DLV_OK);
}

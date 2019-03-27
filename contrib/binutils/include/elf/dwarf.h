/* Declarations and definitions of codes relating to the DWARF symbolic
   debugging information format.

   Written by Ron Guilmette (rfg@netcom.com)

Copyright 1992, 1993, 1995, 1999, 2005 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA
02110-1301, USA.  */

/* This file is derived from the DWARF specification (a public document)
   Revision 1.0.1 (April 8, 1992) developed by the UNIX International
   Programming Languages Special Interest Group (UI/PLSIG) and distributed
   by UNIX International.  Copies of this specification are available from
   UNIX International, 20 Waterview Boulevard, Parsippany, NJ, 07054.
*/

#ifndef _ELF_DWARF_H
#define _ELF_DWARF_H

/* Tag names and codes.  */

enum dwarf_tag {
    TAG_padding			= 0x0000,
    TAG_array_type		= 0x0001,
    TAG_class_type		= 0x0002,
    TAG_entry_point		= 0x0003,
    TAG_enumeration_type	= 0x0004,
    TAG_formal_parameter	= 0x0005,
    TAG_global_subroutine	= 0x0006,
    TAG_global_variable		= 0x0007,
    				/* 0x0008 -- reserved */
				/* 0x0009 -- reserved */
    TAG_label			= 0x000a,
    TAG_lexical_block		= 0x000b,
    TAG_local_variable		= 0x000c,
    TAG_member			= 0x000d,
				/* 0x000e -- reserved */
    TAG_pointer_type		= 0x000f,
    TAG_reference_type		= 0x0010,
    TAG_compile_unit		= 0x0011,
    TAG_string_type		= 0x0012,
    TAG_structure_type		= 0x0013,
    TAG_subroutine		= 0x0014,
    TAG_subroutine_type		= 0x0015,
    TAG_typedef			= 0x0016,
    TAG_union_type		= 0x0017,
    TAG_unspecified_parameters	= 0x0018,
    TAG_variant			= 0x0019,
    TAG_common_block		= 0x001a,
    TAG_common_inclusion	= 0x001b,
    TAG_inheritance		= 0x001c,
    TAG_inlined_subroutine	= 0x001d,
    TAG_module			= 0x001e,
    TAG_ptr_to_member_type	= 0x001f,
    TAG_set_type		= 0x0020,
    TAG_subrange_type		= 0x0021,
    TAG_with_stmt		= 0x0022,

    /* GNU extensions */

    TAG_format_label		= 0x8000,  /* for FORTRAN 77 and Fortran 90 */
    TAG_namelist		= 0x8001,  /* For Fortran 90 */
    TAG_function_template	= 0x8002,  /* for C++ */
    TAG_class_template		= 0x8003   /* for C++ */
};

#define TAG_lo_user	0x8000  /* implementation-defined range start */
#define TAG_hi_user	0xffff  /* implementation-defined range end */
#define TAG_source_file TAG_compile_unit  /* for backward compatibility */

/* Form names and codes.  */

enum dwarf_form {
    FORM_ADDR	= 0x1,
    FORM_REF	= 0x2,
    FORM_BLOCK2	= 0x3,
    FORM_BLOCK4	= 0x4,
    FORM_DATA2	= 0x5,
    FORM_DATA4	= 0x6,
    FORM_DATA8	= 0x7,
    FORM_STRING	= 0x8
};

/* Attribute names and codes.  */

enum dwarf_attribute {
    AT_sibling			= (0x0010|FORM_REF),
    AT_location			= (0x0020|FORM_BLOCK2),
    AT_name			= (0x0030|FORM_STRING),
    AT_fund_type		= (0x0050|FORM_DATA2),
    AT_mod_fund_type		= (0x0060|FORM_BLOCK2),
    AT_user_def_type		= (0x0070|FORM_REF),
    AT_mod_u_d_type		= (0x0080|FORM_BLOCK2),
    AT_ordering			= (0x0090|FORM_DATA2),
    AT_subscr_data		= (0x00a0|FORM_BLOCK2),
    AT_byte_size		= (0x00b0|FORM_DATA4),
    AT_bit_offset		= (0x00c0|FORM_DATA2),
    AT_bit_size			= (0x00d0|FORM_DATA4),
				/* (0x00e0|FORM_xxxx) -- reserved */
    AT_element_list		= (0x00f0|FORM_BLOCK4),
    AT_stmt_list		= (0x0100|FORM_DATA4),
    AT_low_pc			= (0x0110|FORM_ADDR),
    AT_high_pc			= (0x0120|FORM_ADDR),
    AT_language			= (0x0130|FORM_DATA4),
    AT_member			= (0x0140|FORM_REF),
    AT_discr			= (0x0150|FORM_REF),
    AT_discr_value		= (0x0160|FORM_BLOCK2),
				/* (0x0170|FORM_xxxx) -- reserved */
				/* (0x0180|FORM_xxxx) -- reserved */
    AT_string_length		= (0x0190|FORM_BLOCK2),
    AT_common_reference		= (0x01a0|FORM_REF),
    AT_comp_dir			= (0x01b0|FORM_STRING),
        AT_const_value_string	= (0x01c0|FORM_STRING),
        AT_const_value_data2	= (0x01c0|FORM_DATA2),
        AT_const_value_data4	= (0x01c0|FORM_DATA4),
        AT_const_value_data8	= (0x01c0|FORM_DATA8),
        AT_const_value_block2	= (0x01c0|FORM_BLOCK2),
        AT_const_value_block4	= (0x01c0|FORM_BLOCK4),
    AT_containing_type		= (0x01d0|FORM_REF),
        AT_default_value_addr	= (0x01e0|FORM_ADDR),
        AT_default_value_data2	= (0x01e0|FORM_DATA2),
        AT_default_value_data4	= (0x01e0|FORM_DATA4),
        AT_default_value_data8	= (0x01e0|FORM_DATA8),
        AT_default_value_string	= (0x01e0|FORM_STRING),
    AT_friends			= (0x01f0|FORM_BLOCK2),
    AT_inline			= (0x0200|FORM_STRING),
    AT_is_optional		= (0x0210|FORM_STRING),
        AT_lower_bound_ref	= (0x0220|FORM_REF),
        AT_lower_bound_data2	= (0x0220|FORM_DATA2),
        AT_lower_bound_data4	= (0x0220|FORM_DATA4),
        AT_lower_bound_data8	= (0x0220|FORM_DATA8),
    AT_private			= (0x0240|FORM_STRING),
    AT_producer			= (0x0250|FORM_STRING),
    AT_program			= (0x0230|FORM_STRING),
    AT_protected		= (0x0260|FORM_STRING),
    AT_prototyped		= (0x0270|FORM_STRING),
    AT_public			= (0x0280|FORM_STRING),
    AT_pure_virtual		= (0x0290|FORM_STRING),
    AT_return_addr		= (0x02a0|FORM_BLOCK2),
    AT_abstract_origin		= (0x02b0|FORM_REF),
    AT_start_scope		= (0x02c0|FORM_DATA4),
    AT_stride_size		= (0x02e0|FORM_DATA4),
        AT_upper_bound_ref	= (0x02f0|FORM_REF),
        AT_upper_bound_data2	= (0x02f0|FORM_DATA2),
        AT_upper_bound_data4	= (0x02f0|FORM_DATA4),
        AT_upper_bound_data8	= (0x02f0|FORM_DATA8),
    AT_virtual			= (0x0300|FORM_STRING),

    /* GNU extensions.  */

    AT_sf_names			= (0x8000|FORM_DATA4),
    AT_src_info			= (0x8010|FORM_DATA4),
    AT_mac_info			= (0x8020|FORM_DATA4),
    AT_src_coords		= (0x8030|FORM_DATA4),
    AT_body_begin		= (0x8040|FORM_ADDR),
    AT_body_end			= (0x8050|FORM_ADDR)
};

#define AT_lo_user	0x2000	/* implementation-defined range start */
#define AT_hi_user	0x3ff0	/* implementation-defined range end */

/* Location atom names and codes.  */

enum dwarf_location_atom {
    OP_REG	= 0x01,
    OP_BASEREG	= 0x02,
    OP_ADDR	= 0x03,
    OP_CONST	= 0x04,
    OP_DEREF2	= 0x05,
    OP_DEREF4	= 0x06,
    OP_ADD	= 0x07,

    /* GNU extensions.  */

    OP_MULT	= 0x80
};

#define OP_LO_USER	0x80  /* implementation-defined range start */
#define OP_HI_USER	0xff  /* implementation-defined range end */

/* Fundamental type names and codes.  */

enum dwarf_fundamental_type {
    FT_char		= 0x0001,
    FT_signed_char	= 0x0002,
    FT_unsigned_char	= 0x0003,
    FT_short		= 0x0004,
    FT_signed_short	= 0x0005,
    FT_unsigned_short	= 0x0006,
    FT_integer		= 0x0007,
    FT_signed_integer	= 0x0008,
    FT_unsigned_integer	= 0x0009,
    FT_long		= 0x000a,
    FT_signed_long	= 0x000b,
    FT_unsigned_long	= 0x000c,
    FT_pointer		= 0x000d,  /* an alias for (void *) */
    FT_float		= 0x000e,
    FT_dbl_prec_float	= 0x000f,
    FT_ext_prec_float	= 0x0010,  /* breaks "classic" svr4 SDB */
    FT_complex		= 0x0011,  /* breaks "classic" svr4 SDB */
    FT_dbl_prec_complex	= 0x0012,  /* breaks "classic" svr4 SDB */
			/* 0x0013 -- reserved */
    FT_void		= 0x0014,
    FT_boolean		= 0x0015,  /* breaks "classic" svr4 SDB */
    FT_ext_prec_complex	= 0x0016,  /* breaks "classic" svr4 SDB */
    FT_label		= 0x0017,
  
    /* GNU extensions
       The low order byte must indicate the size (in bytes) for the type.
       All of these types will probably break "classic" svr4 SDB.  */

    FT_long_long	= 0x8008,
    FT_signed_long_long	= 0x8108,
    FT_unsigned_long_long = 0x8208,

    FT_int8		= 0x9001,
    FT_signed_int8	= 0x9101,
    FT_unsigned_int8	= 0x9201,
    FT_int16		= 0x9302,
    FT_signed_int16	= 0x9402,
    FT_unsigned_int16	= 0x9502,
    FT_int32		= 0x9604,
    FT_signed_int32	= 0x9704,
    FT_unsigned_int32	= 0x9804,
    FT_int64		= 0x9908,
    FT_signed_int64	= 0x9a08,
    FT_unsigned_int64	= 0x9b08,
    FT_int128		= 0x9c10,
    FT_signed_int128	= 0x9d10,
    FT_unsigned_int128	= 0x9e10,

    FT_real32		= 0xa004,
    FT_real64		= 0xa108,
    FT_real96		= 0xa20c,
    FT_real128		= 0xa310
};

#define FT_lo_user	0x8000  /* implementation-defined range start */
#define FT_hi_user	0xffff  /* implementation defined range end */

/* Type modifier names and codes.  */

enum dwarf_type_modifier {
    MOD_pointer_to	= 0x01,
    MOD_reference_to	= 0x02,
    MOD_const		= 0x03,
    MOD_volatile	= 0x04
};

#define MOD_lo_user	0x80  /* implementation-defined range start */
#define MOD_hi_user	0xff  /* implementation-defined range end */

/* Array ordering names and codes.  */

enum dwarf_array_dim_ordering {
    ORD_row_major	= 0,
    ORD_col_major	= 1
};

/* Array subscript format names and codes.  */

enum dwarf_subscr_data_formats {
    FMT_FT_C_C	= 0x0,
    FMT_FT_C_X	= 0x1,
    FMT_FT_X_C	= 0x2,
    FMT_FT_X_X	= 0x3,
    FMT_UT_C_C	= 0x4,
    FMT_UT_C_X	= 0x5,
    FMT_UT_X_C	= 0x6,
    FMT_UT_X_X	= 0x7,
    FMT_ET	= 0x8
};

/* Derived from above for ease of use.  */

#define FMT_CODE(_FUNDAMENTAL_TYPE_P, _LB_CONST_P, _UB_CONST_P) \
 (((_FUNDAMENTAL_TYPE_P) ? 0 : 4)	\
  | ((_LB_CONST_P) ? 0 : 2)		\
  | ((_UB_CONST_P) ? 0 : 1))

/* Source language names and codes.  */

enum dwarf_source_language {
    LANG_C89		= 0x00000001,
    LANG_C		= 0x00000002,
    LANG_ADA83		= 0x00000003,
    LANG_C_PLUS_PLUS	= 0x00000004,
    LANG_COBOL74	= 0x00000005,
    LANG_COBOL85	= 0x00000006,
    LANG_FORTRAN77	= 0x00000007,
    LANG_FORTRAN90	= 0x00000008,
    LANG_PASCAL83	= 0x00000009,
    LANG_MODULA2	= 0x0000000a,
    LANG_JAVA		= 0x0000000b
};

#define LANG_lo_user	0x00008000  /* implementation-defined range start */
#define LANG_hi_user	0x0000ffff  /* implementation-defined range end */

/* Names and codes for GNU "macinfo" extension.  */

enum dwarf_macinfo_record_type {
    MACINFO_start	= 's',
    MACINFO_resume	= 'r',
    MACINFO_define	= 'd',
    MACINFO_undef	= 'u'
};

#endif /* _ELF_DWARF_H */

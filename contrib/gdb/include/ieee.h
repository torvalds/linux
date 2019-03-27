/* IEEE Standard 695-1980 "Universal Format for Object Modules" header file

   Copyright 2001 Free Software Foundation, Inc.
   
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.
   
   Contributed by Cygnus Support.  */

#define N_W_VARIABLES 8
#define Module_Beginning 0xe0

typedef struct ieee_module
  {
    char *processor;
    char *module_name;
  }
ieee_module_begin_type;

#define Address_Descriptor 0xec
typedef struct ieee_address
  {
    bfd_vma number_of_bits_mau;
    bfd_vma number_of_maus_in_address;

    unsigned char byte_order;
#define IEEE_LITTLE 0xcc
#define IEEE_BIG 0xcd
  }
ieee_address_descriptor_type;

typedef union ieee_w_variable
  {
    file_ptr offset[N_W_VARIABLES];

    struct
      {
	file_ptr extension_record;
	file_ptr environmental_record;
	file_ptr section_part;
	file_ptr external_part;
	file_ptr debug_information_part;
	file_ptr data_part;
	file_ptr trailer_part;
	file_ptr me_record;
      }
    r;
  }
ieee_w_variable_type;

typedef enum ieee_record
  { 
    ieee_number_start_enum = 0x00,
    ieee_number_end_enum=0x7f,
    ieee_number_repeat_start_enum = 0x80,
    ieee_number_repeat_end_enum = 0x88,
    ieee_number_repeat_4_enum = 0x84,
    ieee_number_repeat_3_enum = 0x83,
    ieee_number_repeat_2_enum = 0x82,
    ieee_number_repeat_1_enum = 0x81,
    ieee_module_beginning_enum = 0xe0,
    ieee_module_end_enum = 0xe1,
    ieee_extension_length_1_enum = 0xde,
    ieee_extension_length_2_enum = 0xdf,
    ieee_section_type_enum = 0xe6,
    ieee_section_alignment_enum = 0xe7,
    ieee_external_symbol_enum = 0xe8,
    ieee_comma = 0x90,
    ieee_external_reference_enum = 0xe9,
    ieee_set_current_section_enum = 0xe5,
    ieee_address_descriptor_enum = 0xec,
    ieee_load_constant_bytes_enum = 0xed,
    ieee_load_with_relocation_enum = 0xe4,

    ieee_variable_A_enum = 0xc1,
    ieee_variable_B_enum = 0xc2,
    ieee_variable_C_enum = 0xc3,
    ieee_variable_D_enum = 0xc4,
    ieee_variable_E_enum = 0xc5,
    ieee_variable_F_enum = 0xc6,
    ieee_variable_G_enum = 0xc7,
    ieee_variable_H_enum = 0xc8,
    ieee_variable_I_enum = 0xc9,
    ieee_variable_J_enum = 0xca,
    ieee_variable_K_enum = 0xcb,
    ieee_variable_L_enum = 0xcc,
    ieee_variable_M_enum = 0xcd,
    ieee_variable_N_enum = 0xce,
    ieee_variable_O_enum = 0xcf,
    ieee_variable_P_enum = 0xd0,
    ieee_variable_Q_enum = 0xd1,
    ieee_variable_R_enum = 0xd2,
    ieee_variable_S_enum = 0xd3,
    ieee_variable_T_enum = 0xd4,
    ieee_variable_U_enum = 0xd5,
    ieee_variable_V_enum = 0xd6,
    ieee_variable_W_enum = 0xd7,
    ieee_variable_X_enum = 0xd8,
    ieee_variable_Y_enum = 0xd9,
    ieee_variable_Z_enum = 0xda,
    ieee_function_plus_enum = 0xa5,
    ieee_function_minus_enum = 0xa6,
    ieee_function_signed_open_b_enum = 0xba,
    ieee_function_signed_close_b_enum = 0xbb,

    ieee_function_unsigned_open_b_enum = 0xbc,
    ieee_function_unsigned_close_b_enum = 0xbd,

    ieee_function_either_open_b_enum = 0xbe,
    ieee_function_either_close_b_enum = 0xbf,
    ieee_record_seperator_enum = 0xdb,

    ieee_e2_first_byte_enum = 0xe2,
    ieee_section_size_enum = 0xe2d3,
    ieee_physical_region_size_enum = 0xe2c1,
    ieee_region_base_address_enum = 0xe2c2,
    ieee_mau_size_enum = 0xe2c6,
    ieee_m_value_enum = 0xe2cd,
    ieee_section_base_address_enum = 0xe2cc,
    ieee_asn_record_enum = 0xe2ce,
    ieee_section_offset_enum = 0xe2d2,
    ieee_value_starting_address_enum = 0xe2c7,
    ieee_assign_value_to_variable_enum = 0xe2d7,
    ieee_set_current_pc_enum = 0xe2d0,
    ieee_value_record_enum = 0xe2c9,
    ieee_nn_record = 0xf0,
    ieee_at_record_enum = 0xf1,
    ieee_ty_record_enum = 0xf2,
    ieee_attribute_record_enum = 0xf1c9,
    ieee_atn_record_enum = 0xf1ce,
    ieee_external_reference_info_record_enum = 0xf1d8,
    ieee_weak_external_reference_enum= 0xf4,
    ieee_repeat_data_enum = 0xf7,
    ieee_bb_record_enum = 0xf8,
    ieee_be_record_enum = 0xf9
  }
ieee_record_enum_type;

typedef struct ieee_section
  {
    unsigned int section_index;
    unsigned int section_type;
    char *       section_name;
    unsigned int parent_section_index;
    unsigned int sibling_section_index;
    unsigned int context_index;
  }
ieee_section_type;

#define IEEE_REFERENCE_BASE 11
#define IEEE_PUBLIC_BASE 32
#define IEEE_SECTION_NUMBER_BASE 1


/* Oasys object format header file for BFD.

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

#define OASYS_MAX_SEC_COUNT 16
/* **** */

typedef struct oasys_archive_header
  {
    unsigned int  version;
    char create_date[12];
    char revision_date[12];
    unsigned int mod_count;
    file_ptr mod_tbl_offset;
    unsigned int sym_tbl_size;
    unsigned int sym_count;
    file_ptr sym_tbl_offset;
    unsigned int xref_count;
    file_ptr xref_lst_offset;
  }
oasys_archive_header_type;

typedef struct oasys_extarchive_header
  {
    bfd_byte version[4];
    bfd_byte create_date[12];
    bfd_byte revision_date[12];
    bfd_byte mod_count[4];
    bfd_byte mod_tbl_offset[4];
    bfd_byte sym_tbl_size[4];
    bfd_byte sym_count[4];
    bfd_byte sym_tbl_offset[4];
    bfd_byte xref_count[4];
    bfd_byte xref_lst_offset[4];
  }
oasys_extarchive_header_type;

typedef struct oasys_module_table
  {
    int mod_number;
    char mod_date[12];
    unsigned int mod_size;
    unsigned int dep_count;
    unsigned int depee_count;
    file_ptr file_offset;
    unsigned int sect_count;
    char *module_name;
    unsigned int module_name_size;
  }
oasys_module_table_type;

typedef struct oasys_extmodule_table_a
  {
    bfd_byte mod_number[4];
    bfd_byte mod_date[12];
    bfd_byte mod_size[4];
    bfd_byte dep_count[4];
    bfd_byte depee_count[4];
    bfd_byte sect_count[4];
    bfd_byte file_offset[4];
    bfd_byte mod_name[32];
  }
oasys_extmodule_table_type_a_type;

typedef struct oasys_extmodule_table_b
  {
    bfd_byte mod_number[4];
    bfd_byte mod_date[12];
    bfd_byte mod_size[4];
    bfd_byte dep_count[4];
    bfd_byte depee_count[4];
    bfd_byte sect_count[4];
    bfd_byte file_offset[4];
    bfd_byte mod_name_length[4];
  }
oasys_extmodule_table_type_b_type;

typedef enum oasys_record
  {
    oasys_record_is_end_enum = 0,
    oasys_record_is_data_enum = 1,
    oasys_record_is_symbol_enum = 2,
    oasys_record_is_header_enum = 3,
    oasys_record_is_named_section_enum = 4,
    oasys_record_is_com_enum = 5,
    oasys_record_is_debug_enum = 6,
    oasys_record_is_section_enum = 7,
    oasys_record_is_debug_file_enum = 8,
    oasys_record_is_module_enum = 9,
    oasys_record_is_local_enum = 10
  }
oasys_record_enum_type;

typedef struct oasys_record_header
  {
    unsigned char length;
    unsigned char check_sum;
    unsigned char type;
    unsigned char fill;
  }
oasys_record_header_type;

typedef struct oasys_data_record
  {
    oasys_record_header_type header;
    unsigned char relb;
    bfd_byte addr[4];
    /* maximum total size of data record is 255 bytes */
    bfd_byte data[246];
  }
oasys_data_record_type;

typedef struct oasys_header_record
  {
    oasys_record_header_type header;
    unsigned char version_number;
    unsigned char rev_number;
    char module_name[26-6];
    char description[64-26];
  }
oasys_header_record_type;

#define OASYS_VERSION_NUMBER 0
#define OASYS_REV_NUMBER 0

typedef struct oasys_symbol_record
  {
    oasys_record_header_type header;
    unsigned char relb;
    bfd_byte value[4];
    bfd_byte refno[2];
    char name[64];
  }
oasys_symbol_record_type;

#define RELOCATION_PCREL_BIT 0x80
#define RELOCATION_32BIT_BIT 0x40
#define RELOCATION_TYPE_BITS 0x30
#define RELOCATION_TYPE_ABS 0x00
#define RELOCATION_TYPE_REL 0x10
#define RELOCATION_TYPE_UND 0x20
#define RELOCATION_TYPE_COM 0x30
#define RELOCATION_SECT_BITS 0x0f

typedef struct oasys_section_record
  {
    oasys_record_header_type header;
    unsigned char relb;
    bfd_byte value[4];
    bfd_byte vma[4];
    bfd_byte fill[3];
  }
oasys_section_record_type;

typedef struct oasys_end_record
  {
    oasys_record_header_type header;
    unsigned char relb;
    bfd_byte entry[4];
    bfd_byte fill[2];
    bfd_byte zero;
  }
oasys_end_record_type;

typedef union oasys_record_union
  {
    oasys_record_header_type header;
    oasys_data_record_type data;
    oasys_section_record_type section;
    oasys_symbol_record_type symbol;
    oasys_header_record_type first;
    oasys_end_record_type end;
    bfd_byte pad[256];
  }
oasys_record_union_type;

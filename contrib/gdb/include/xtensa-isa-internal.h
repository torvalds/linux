/* Internal definitions for configurable Xtensa ISA support.
   Copyright 2003 Free Software Foundation, Inc.

   This file is part of BFD, the Binary File Descriptor library.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

/* Use the statically-linked version for the GNU tools.  */
#define STATIC_LIBISA 1

#define ISA_INTERFACE_VERSION 3

struct config_struct
{
    char *param_name;
    char *param_value;
};

/* Encode/decode function types for immediate operands.  */
typedef uint32 (*xtensa_immed_decode_fn) (uint32);
typedef xtensa_encode_result (*xtensa_immed_encode_fn) (uint32 *);

/* Field accessor function types.  */
typedef uint32 (*xtensa_get_field_fn) (const xtensa_insnbuf);
typedef void (*xtensa_set_field_fn) (xtensa_insnbuf, uint32);

/* PC-relative relocation function types.  */
typedef uint32 (*xtensa_do_reloc_fn) (uint32, uint32);
typedef uint32 (*xtensa_undo_reloc_fn) (uint32, uint32);

/* Instruction decode function type.  */
typedef int (*xtensa_insn_decode_fn) (const xtensa_insnbuf);

/* Instruction encoding template function type (each of these functions
   returns a constant template; they exist only to make it easier for the
   TIE compiler to generate endian-independent DLLs).  */
typedef xtensa_insnbuf (*xtensa_encoding_template_fn) (void);


typedef struct xtensa_operand_internal_struct
{
  char *operand_kind;			/* e.g., "a", "f", "i", "l"....  */
  char inout;				/* '<', '>', or '='.  */
  char isPCRelative;			/* Is this a PC-relative offset?  */
  xtensa_get_field_fn get_field;	/* Get encoded value of the field.  */
  xtensa_set_field_fn set_field;	/* Set field with an encoded value.  */
  xtensa_immed_encode_fn encode;	/* Encode the operand value.  */
  xtensa_immed_decode_fn decode;	/* Decode the value from the field.  */
  xtensa_do_reloc_fn do_reloc;		/* Perform a PC-relative relocation.  */
  xtensa_undo_reloc_fn undo_reloc;	/* Undo a PC-relative relocation.  */
} xtensa_operand_internal;


typedef struct xtensa_iclass_internal_struct
{
  int num_operands;			/* Size of "operands" array.  */
  xtensa_operand_internal **operands;	/* Array of operand structures.  */
} xtensa_iclass_internal;


typedef struct xtensa_opcode_internal_struct
{
  const char *name;			/* Opcode mnemonic.  */
  int length;				/* Length in bytes of the insn.  */
  xtensa_encoding_template_fn template;	/* Fn returning encoding template.  */
  xtensa_iclass_internal *iclass;	/* Iclass for this opcode.  */
} xtensa_opcode_internal;


typedef struct opname_lookup_entry_struct
{
  const char *key;			/* Opcode mnemonic.  */
  xtensa_opcode opcode;			/* Internal opcode number.  */
} opname_lookup_entry;


typedef struct xtensa_isa_internal_struct
{
  int is_big_endian;			/* Endianness.  */
  int insn_size;			/* Maximum length in bytes.  */
  int insnbuf_size;			/* Number of insnbuf_words.  */
  int num_opcodes;			/* Total number for all modules.  */
  xtensa_opcode_internal **opcode_table;/* Indexed by internal opcode #.  */
  int num_modules;			/* Number of modules (DLLs) loaded.  */
  int *module_opcode_base;		/* Starting opcode # for each module.  */
  xtensa_insn_decode_fn *module_decode_fn; /* Decode fn for each module.  */
  opname_lookup_entry *opname_lookup_table; /* Lookup table for each module.  */
  struct config_struct *config;		/* Table of configuration parameters.  */
  int has_density;			/* Is density option available?  */
} xtensa_isa_internal;


typedef struct xtensa_isa_module_struct
{
  int (*get_num_opcodes_fn) (void);
  xtensa_opcode_internal **(*get_opcodes_fn) (void);
  int (*decode_insn_fn) (const xtensa_insnbuf);
  struct config_struct *(*get_config_table_fn) (void);
} xtensa_isa_module;

extern xtensa_isa_module xtensa_isa_modules[];


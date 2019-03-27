/* Interface definition for configurable Xtensa ISA support.
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

#ifndef XTENSA_LIBISA_H
#define XTENSA_LIBISA_H

/* Use the statically-linked version for the GNU tools.  */
#define STATIC_LIBISA 1

#ifdef __cplusplus
extern "C" {
#endif

#ifndef uint32
#define uint32 unsigned int
#endif

/* This file defines the interface to the Xtensa ISA library.  This library
   contains most of the ISA-specific information for a particular Xtensa
   processor.  For example, the set of valid instructions, their opcode
   encodings and operand fields are all included here.  To support Xtensa's
   configurability and user-defined instruction extensions (i.e., TIE), the
   library is initialized by loading one or more dynamic libraries; only a
   small set of interface code is present in the statically-linked portion
   of the library.

   This interface basically defines four abstract data types.

   . an instruction buffer - for holding the raw instruction bits
   . ISA info - information about the ISA as a whole
   . opcode info - information about individual instructions
   . operand info - information about specific instruction operands

   It would be nice to implement these as classes in C++, but the library is
   implemented in C to match the expectations of the GNU tools.
   Instead, the interface defines a set of functions to access each data
   type.  With the exception of the instruction buffer, the internal
   representations of the data structures are hidden.  All accesses must be
   made through the functions defined here.  */

typedef void* xtensa_isa;
typedef void* xtensa_operand;


/* Opcodes are represented here using sequential integers beginning with 0.
   The specific value used for a particular opcode is only fixed for a
   particular instantiation of an xtensa_isa structure, so these values
   should only be used internally.  */
typedef int xtensa_opcode;

/* Define a unique value for undefined opcodes ("static const int" doesn't
   seem to work for this because EGCS 1.0.3 on i686-Linux without -O won't
   allow it to be used as an initializer).  */
#define XTENSA_UNDEFINED -1


typedef int libisa_module_specifier;

extern xtensa_isa xtensa_isa_init (void);


/* Instruction buffers.  */

typedef uint32 xtensa_insnbuf_word;
typedef xtensa_insnbuf_word *xtensa_insnbuf;

/* Get the size in words of the xtensa_insnbuf array.  */
extern int xtensa_insnbuf_size (xtensa_isa); 

/* Allocate (with malloc) an xtensa_insnbuf of the right size.  */
extern xtensa_insnbuf xtensa_insnbuf_alloc (xtensa_isa);

/* Release (with free) an xtensa_insnbuf of the right size.  */
extern void xtensa_insnbuf_free (xtensa_insnbuf);

/* Inward and outward conversion from memory images (byte streams) to our
   internal instruction representation.  */
extern void xtensa_insnbuf_to_chars (xtensa_isa, const xtensa_insnbuf,
				     char *);

extern void xtensa_insnbuf_from_chars (xtensa_isa, xtensa_insnbuf,
				       const char *);


/* ISA information.  */

/* Load the ISA information from a shared library.  If successful, this returns
   a value which identifies the ISA for use in subsequent calls to the ISA
   library; otherwise, it returns NULL.  Multiple ISAs can be loaded to support
   heterogeneous multiprocessor systems.  */
extern xtensa_isa xtensa_load_isa (libisa_module_specifier);

/* Extend an existing set of ISA information by loading an additional shared
   library of ISA information.  This is primarily intended for loading TIE
   extensions.  If successful, the return value is non-zero.  */
extern int xtensa_extend_isa (xtensa_isa, libisa_module_specifier);

/* The default ISA.  This variable is set automatically to the ISA most
   recently loaded and is provided as a convenience.  An exception is the GNU
   opcodes library, where there is a fixed interface that does not allow
   passing the ISA as a parameter and the ISA must be taken from this global
   variable.  (Note: Since this variable is just a convenience, it is not
   exported when libisa is built as a DLL, due to the hassle of dealing with
   declspecs.)  */
extern xtensa_isa xtensa_default_isa;


/* Deallocate an xtensa_isa structure.  */
extern void xtensa_isa_free (xtensa_isa);

/* Get the maximum instruction size in bytes.  */
extern int xtensa_insn_maxlength (xtensa_isa); 

/* Get the total number of opcodes for this processor.  */
extern int xtensa_num_opcodes (xtensa_isa);

/* Translate a mnemonic name to an opcode.  Returns XTENSA_UNDEFINED if
   the name is not a valid opcode mnemonic.  */
extern xtensa_opcode xtensa_opcode_lookup (xtensa_isa, const char *);

/* Decode a binary instruction buffer.  Returns the opcode or
   XTENSA_UNDEFINED if the instruction is illegal.  */
extern xtensa_opcode xtensa_decode_insn (xtensa_isa, const xtensa_insnbuf);


/* Opcode information.  */

/* Set the opcode field(s) in a binary instruction buffer.  The operand
   fields are set to zero.  */
extern void xtensa_encode_insn (xtensa_isa, xtensa_opcode, xtensa_insnbuf);

/* Get the mnemonic name for an opcode.  */
extern const char * xtensa_opcode_name (xtensa_isa, xtensa_opcode);

/* Find the length (in bytes) of an instruction.  */
extern int xtensa_insn_length (xtensa_isa, xtensa_opcode);

/* Find the length of an instruction by looking only at the first byte.  */
extern int xtensa_insn_length_from_first_byte (xtensa_isa, char);

/* Find the number of operands for an instruction.  */
extern int xtensa_num_operands (xtensa_isa, xtensa_opcode);

/* Get the information about operand number "opnd" of a particular opcode.  */
extern xtensa_operand xtensa_get_operand (xtensa_isa, xtensa_opcode, int);

/* Operand information.  */

/* Find the kind of operand.  There are three possibilities:
   1) PC-relative immediates (e.g., "l", "L").  These can be identified with
      the xtensa_operand_isPCRelative function.
   2) non-PC-relative immediates ("i").
   3) register-file short names (e.g., "a", "b", "m" and others defined
      via TIE).  */
extern char * xtensa_operand_kind (xtensa_operand);

/* Check if an operand is an input ('<'), output ('>'), or inout ('=')
   operand.  Note: The output operand of a conditional assignment
   (e.g., movnez) appears here as an inout ('=') even if it is declared
   in the TIE code as an output ('>'); this allows the compiler to
   properly handle register allocation for conditional assignments.  */
extern char xtensa_operand_inout (xtensa_operand);

/* Get and set the raw (encoded) value of the field for the specified
   operand.  The "set" function does not check if the value fits in the
   field; that is done by the "encode" function below.  */
extern uint32 xtensa_operand_get_field (xtensa_operand, const xtensa_insnbuf);

extern void xtensa_operand_set_field (xtensa_operand, xtensa_insnbuf, uint32);


/* Encode and decode operands.  The raw bits in the operand field
   may be encoded in a variety of different ways.  These functions hide the
   details of that encoding.  The encode function has a special return type
   (xtensa_encode_result) to indicate success or the reason for failure; the
   encoded value is returned through the argument pointer.  The decode function
   has no possibility of failure and returns the decoded value.  */

typedef enum
{
  xtensa_encode_result_ok,
  xtensa_encode_result_align,
  xtensa_encode_result_not_in_table,
  xtensa_encode_result_too_low,
  xtensa_encode_result_too_high,
  xtensa_encode_result_not_ok,
  xtensa_encode_result_max = xtensa_encode_result_not_ok
} xtensa_encode_result;

extern xtensa_encode_result xtensa_operand_encode (xtensa_operand, uint32 *);

extern uint32 xtensa_operand_decode (xtensa_operand, uint32);


/* For PC-relative offset operands, the interpretation of the offset may vary
   between opcodes, e.g., is it relative to the current PC or that of the next
   instruction?  The following functions are defined to perform PC-relative
   relocations and to undo them (as in the disassembler).  The first function
   takes the desired address and the PC of the current instruction and returns
   the unencoded value to be stored in the offset field.  The second function
   takes the unencoded offset value and the current PC and returns the address.
   Note that these functions do not replace the encode/decode functions; the
   operands must be encoded/decoded separately.  */

extern int xtensa_operand_isPCRelative (xtensa_operand);

extern uint32 xtensa_operand_do_reloc (xtensa_operand, uint32, uint32);

extern uint32 xtensa_operand_undo_reloc	(xtensa_operand, uint32, uint32);

#ifdef __cplusplus
}
#endif
#endif /* XTENSA_LIBISA_H */

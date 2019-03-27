/* Dwarf2 Expression Evaluator
   Copyright 2001, 2002, 2003 Free Software Foundation, Inc.
   Contributed by Daniel Berlin (dan@dberlin.org)
   This file is part of GDB.

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
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

#if !defined (DWARF2EXPR_H)
#define DWARF2EXPR_H

/* The expression evaluator works with a dwarf_expr_context, describing
   its current state and its callbacks.  */
struct dwarf_expr_context
{
  /* The stack of values, allocated with xmalloc.  */
  CORE_ADDR *stack;

  /* The number of values currently pushed on the stack, and the
     number of elements allocated to the stack.  */
  int stack_len, stack_allocated;

  /* An opaque argument provided by the caller, which will be passed
     to all of the callback functions.  */
  void *baton;

  /* Return the value of register number REGNUM.  */
  CORE_ADDR (*read_reg) (void *baton, int regnum);

  /* Read LENGTH bytes at ADDR into BUF.  */
  void (*read_mem) (void *baton, char *buf, CORE_ADDR addr,
		    size_t length);

  /* Return the location expression for the frame base attribute, in
     START and LENGTH.  The result must be live until the current
     expression evaluation is complete.  */
  void (*get_frame_base) (void *baton, unsigned char **start,
			 size_t *length);

  /* Return the thread-local storage address for
     DW_OP_GNU_push_tls_address.  */
  CORE_ADDR (*get_tls_address) (void *baton, CORE_ADDR offset);

#if 0
  /* Not yet implemented.  */

  /* Return the location expression for the dwarf expression
     subroutine in the die at OFFSET in the current compilation unit.
     The result must be live until the current expression evaluation
     is complete.  */
  unsigned char *(*get_subr) (void *baton, off_t offset, size_t *length);

  /* Return the `object address' for DW_OP_push_object_address.  */
  CORE_ADDR (*get_object_address) (void *baton);
#endif

  /* The current depth of dwarf expression recursion, via DW_OP_call*,
     DW_OP_fbreg, DW_OP_push_object_address, etc., and the maximum
     depth we'll tolerate before raising an error.  */
  int recursion_depth, max_recursion_depth;

  /* Non-zero if the result is in a register.  The register number
     will be on the expression stack.  */
  int in_reg;
  /* Initialization status of variable: Non-zero if variable has been
     initialized; zero otherwise.  */
  int initialized;

  /* An array of pieces.  PIECES points to its first element;
     NUM_PIECES is its length.

     Each time DW_OP_piece is executed, we add a new element to the
     end of this array, recording the current top of the stack, the
     current in_reg flag, and the size given as the operand to
     DW_OP_piece.  We then pop the top value from the stack, clear the
     in_reg flag, and resume evaluation.

     The Dwarf spec doesn't say whether DW_OP_piece pops the top value
     from the stack.  We do, ensuring that clients of this interface
     expecting to see a value left on the top of the stack (say, code
     evaluating frame base expressions or CFA's specified with
     DW_CFA_def_cfa_expression) will get an error if the expression
     actually marks all the values it computes as pieces.

     If an expression never uses DW_OP_piece, num_pieces will be zero.
     (It would be nice to present these cases as expressions yielding
     a single piece, with in_reg clear, so that callers need not
     distinguish between the no-DW_OP_piece and one-DW_OP_piece cases.
     But expressions with no DW_OP_piece operations have no value to
     place in a piece's 'size' field; the size comes from the
     surrounding data.  So the two cases need to be handled
     separately.)  */
  int num_pieces;
  struct dwarf_expr_piece *pieces;
};

/* A piece of an object, as recorded by DW_OP_piece or DW_OP_bit_piece.  */
struct dwarf_expr_piece
{
  /* If IN_REG is zero, then the piece is in memory, and VALUE is its address.
     If IN_REG is non-zero, then the piece is in a register, and VALUE
     is the register number.  */
  int in_reg;

  /* This piece's address or register number.  */
  CORE_ADDR value;

  /* The length of the piece, in bytes.  */
  ULONGEST size;
};

struct dwarf_expr_context *new_dwarf_expr_context (void);
void free_dwarf_expr_context (struct dwarf_expr_context *ctx);

void dwarf_expr_push (struct dwarf_expr_context *ctx, CORE_ADDR value);
void dwarf_expr_pop (struct dwarf_expr_context *ctx);
void dwarf_expr_eval (struct dwarf_expr_context *ctx, unsigned char *addr,
		      size_t len);
CORE_ADDR dwarf_expr_fetch (struct dwarf_expr_context *ctx, int n);


unsigned char *read_uleb128 (unsigned char *buf, unsigned char *buf_end,
			     ULONGEST * r);
unsigned char *read_sleb128 (unsigned char *buf, unsigned char *buf_end,
			     LONGEST * r);
CORE_ADDR dwarf2_read_address (unsigned char *buf, unsigned char *buf_end,
			       int *bytes_read);

#endif

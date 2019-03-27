/* Functions for manipulating expressions designed to be executed on the agent
   Copyright 1998, 1999, 2000 Free Software Foundation, Inc.

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

/* Despite what the above comment says about this file being part of
   GDB, we would like to keep these functions free of GDB
   dependencies, since we want to be able to use them in contexts
   outside of GDB (test suites, the stub, etc.)  */

#include "defs.h"
#include "ax.h"

#include "value.h"
#include "gdb_string.h"

static void grow_expr (struct agent_expr *x, int n);

static void append_const (struct agent_expr *x, LONGEST val, int n);

static LONGEST read_const (struct agent_expr *x, int o, int n);

static void generic_ext (struct agent_expr *x, enum agent_op op, int n);

/* Functions for building expressions.  */

/* Allocate a new, empty agent expression.  */
struct agent_expr *
new_agent_expr (CORE_ADDR scope)
{
  struct agent_expr *x = xmalloc (sizeof (*x));
  x->len = 0;
  x->size = 1;			/* Change this to a larger value once
				   reallocation code is tested.  */
  x->buf = xmalloc (x->size);
  x->scope = scope;

  return x;
}

/* Free a agent expression.  */
void
free_agent_expr (struct agent_expr *x)
{
  xfree (x->buf);
  xfree (x);
}

static void
do_free_agent_expr_cleanup (void *x)
{
  free_agent_expr (x);
}

struct cleanup *
make_cleanup_free_agent_expr (struct agent_expr *x)
{
  return make_cleanup (do_free_agent_expr_cleanup, x);
}


/* Make sure that X has room for at least N more bytes.  This doesn't
   affect the length, just the allocated size.  */
static void
grow_expr (struct agent_expr *x, int n)
{
  if (x->len + n > x->size)
    {
      x->size *= 2;
      if (x->size < x->len + n)
	x->size = x->len + n + 10;
      x->buf = xrealloc (x->buf, x->size);
    }
}


/* Append the low N bytes of VAL as an N-byte integer to the
   expression X, in big-endian order.  */
static void
append_const (struct agent_expr *x, LONGEST val, int n)
{
  int i;

  grow_expr (x, n);
  for (i = n - 1; i >= 0; i--)
    {
      x->buf[x->len + i] = val & 0xff;
      val >>= 8;
    }
  x->len += n;
}


/* Extract an N-byte big-endian unsigned integer from expression X at
   offset O.  */
static LONGEST
read_const (struct agent_expr *x, int o, int n)
{
  int i;
  LONGEST accum = 0;

  /* Make sure we're not reading off the end of the expression.  */
  if (o + n > x->len)
    error ("GDB bug: ax-general.c (read_const): incomplete constant");

  for (i = 0; i < n; i++)
    accum = (accum << 8) | x->buf[o + i];

  return accum;
}


/* Append a simple operator OP to EXPR.  */
void
ax_simple (struct agent_expr *x, enum agent_op op)
{
  grow_expr (x, 1);
  x->buf[x->len++] = op;
}


/* Append a sign-extension or zero-extension instruction to EXPR, to
   extend an N-bit value.  */
static void
generic_ext (struct agent_expr *x, enum agent_op op, int n)
{
  /* N must fit in a byte.  */
  if (n < 0 || n > 255)
    error ("GDB bug: ax-general.c (generic_ext): bit count out of range");
  /* That had better be enough range.  */
  if (sizeof (LONGEST) * 8 > 255)
    error ("GDB bug: ax-general.c (generic_ext): opcode has inadequate range");

  grow_expr (x, 2);
  x->buf[x->len++] = op;
  x->buf[x->len++] = n;
}


/* Append a sign-extension instruction to EXPR, to extend an N-bit value.  */
void
ax_ext (struct agent_expr *x, int n)
{
  generic_ext (x, aop_ext, n);
}


/* Append a zero-extension instruction to EXPR, to extend an N-bit value.  */
void
ax_zero_ext (struct agent_expr *x, int n)
{
  generic_ext (x, aop_zero_ext, n);
}


/* Append a trace_quick instruction to EXPR, to record N bytes.  */
void
ax_trace_quick (struct agent_expr *x, int n)
{
  /* N must fit in a byte.  */
  if (n < 0 || n > 255)
    error ("GDB bug: ax-general.c (ax_trace_quick): size out of range for trace_quick");

  grow_expr (x, 2);
  x->buf[x->len++] = aop_trace_quick;
  x->buf[x->len++] = n;
}


/* Append a goto op to EXPR.  OP is the actual op (must be aop_goto or
   aop_if_goto).  We assume we don't know the target offset yet,
   because it's probably a forward branch, so we leave space in EXPR
   for the target, and return the offset in EXPR of that space, so we
   can backpatch it once we do know the target offset.  Use ax_label
   to do the backpatching.  */
int
ax_goto (struct agent_expr *x, enum agent_op op)
{
  grow_expr (x, 3);
  x->buf[x->len + 0] = op;
  x->buf[x->len + 1] = 0xff;
  x->buf[x->len + 2] = 0xff;
  x->len += 3;
  return x->len - 2;
}

/* Suppose a given call to ax_goto returns some value PATCH.  When you
   know the offset TARGET that goto should jump to, call
   ax_label (EXPR, PATCH, TARGET)
   to patch TARGET into the ax_goto instruction.  */
void
ax_label (struct agent_expr *x, int patch, int target)
{
  /* Make sure the value is in range.  Don't accept 0xffff as an
     offset; that's our magic sentinel value for unpatched branches.  */
  if (target < 0 || target >= 0xffff)
    error ("GDB bug: ax-general.c (ax_label): label target out of range");

  x->buf[patch] = (target >> 8) & 0xff;
  x->buf[patch + 1] = target & 0xff;
}


/* Assemble code to push a constant on the stack.  */
void
ax_const_l (struct agent_expr *x, LONGEST l)
{
  static enum agent_op ops[]
  =
  {aop_const8, aop_const16, aop_const32, aop_const64};
  int size;
  int op;

  /* How big is the number?  'op' keeps track of which opcode to use.
     Notice that we don't really care whether the original number was
     signed or unsigned; we always reproduce the value exactly, and
     use the shortest representation.  */
  for (op = 0, size = 8; size < 64; size *= 2, op++)
    if (-((LONGEST) 1 << size) <= l && l < ((LONGEST) 1 << size))
      break;

  /* Emit the right opcode... */
  ax_simple (x, ops[op]);

  /* Emit the low SIZE bytes as an unsigned number.  We know that
     sign-extending this will yield l.  */
  append_const (x, l, size / 8);

  /* Now, if it was negative, and not full-sized, sign-extend it.  */
  if (l < 0 && size < 64)
    ax_ext (x, size);
}


void
ax_const_d (struct agent_expr *x, LONGEST d)
{
  /* FIXME: floating-point support not present yet.  */
  error ("GDB bug: ax-general.c (ax_const_d): floating point not supported yet");
}


/* Assemble code to push the value of register number REG on the
   stack.  */
void
ax_reg (struct agent_expr *x, int reg)
{
  /* Make sure the register number is in range.  */
  if (reg < 0 || reg > 0xffff)
    error ("GDB bug: ax-general.c (ax_reg): register number out of range");
  grow_expr (x, 3);
  x->buf[x->len] = aop_reg;
  x->buf[x->len + 1] = (reg >> 8) & 0xff;
  x->buf[x->len + 2] = (reg) & 0xff;
  x->len += 3;
}



/* Functions for disassembling agent expressions, and otherwise
   debugging the expression compiler.  */

struct aop_map aop_map[] =
{
  {0, 0, 0, 0, 0},
  {"float", 0, 0, 0, 0},	/* 0x01 */
  {"add", 0, 0, 2, 1},		/* 0x02 */
  {"sub", 0, 0, 2, 1},		/* 0x03 */
  {"mul", 0, 0, 2, 1},		/* 0x04 */
  {"div_signed", 0, 0, 2, 1},	/* 0x05 */
  {"div_unsigned", 0, 0, 2, 1},	/* 0x06 */
  {"rem_signed", 0, 0, 2, 1},	/* 0x07 */
  {"rem_unsigned", 0, 0, 2, 1},	/* 0x08 */
  {"lsh", 0, 0, 2, 1},		/* 0x09 */
  {"rsh_signed", 0, 0, 2, 1},	/* 0x0a */
  {"rsh_unsigned", 0, 0, 2, 1},	/* 0x0b */
  {"trace", 0, 0, 2, 0},	/* 0x0c */
  {"trace_quick", 1, 0, 1, 1},	/* 0x0d */
  {"log_not", 0, 0, 1, 1},	/* 0x0e */
  {"bit_and", 0, 0, 2, 1},	/* 0x0f */
  {"bit_or", 0, 0, 2, 1},	/* 0x10 */
  {"bit_xor", 0, 0, 2, 1},	/* 0x11 */
  {"bit_not", 0, 0, 1, 1},	/* 0x12 */
  {"equal", 0, 0, 2, 1},	/* 0x13 */
  {"less_signed", 0, 0, 2, 1},	/* 0x14 */
  {"less_unsigned", 0, 0, 2, 1},	/* 0x15 */
  {"ext", 1, 0, 1, 1},		/* 0x16 */
  {"ref8", 0, 8, 1, 1},		/* 0x17 */
  {"ref16", 0, 16, 1, 1},	/* 0x18 */
  {"ref32", 0, 32, 1, 1},	/* 0x19 */
  {"ref64", 0, 64, 1, 1},	/* 0x1a */
  {"ref_float", 0, 0, 1, 1},	/* 0x1b */
  {"ref_double", 0, 0, 1, 1},	/* 0x1c */
  {"ref_long_double", 0, 0, 1, 1},	/* 0x1d */
  {"l_to_d", 0, 0, 1, 1},	/* 0x1e */
  {"d_to_l", 0, 0, 1, 1},	/* 0x1f */
  {"if_goto", 2, 0, 1, 0},	/* 0x20 */
  {"goto", 2, 0, 0, 0},		/* 0x21 */
  {"const8", 1, 8, 0, 1},	/* 0x22 */
  {"const16", 2, 16, 0, 1},	/* 0x23 */
  {"const32", 4, 32, 0, 1},	/* 0x24 */
  {"const64", 8, 64, 0, 1},	/* 0x25 */
  {"reg", 2, 0, 0, 1},		/* 0x26 */
  {"end", 0, 0, 0, 0},		/* 0x27 */
  {"dup", 0, 0, 1, 2},		/* 0x28 */
  {"pop", 0, 0, 1, 0},		/* 0x29 */
  {"zero_ext", 1, 0, 1, 1},	/* 0x2a */
  {"swap", 0, 0, 2, 2},		/* 0x2b */
  {0, 0, 0, 0, 0},		/* 0x2c */
  {0, 0, 0, 0, 0},		/* 0x2d */
  {0, 0, 0, 0, 0},		/* 0x2e */
  {0, 0, 0, 0, 0},		/* 0x2f */
  {"trace16", 2, 0, 1, 1},	/* 0x30 */
};


/* Disassemble the expression EXPR, writing to F.  */
void
ax_print (struct ui_file *f, struct agent_expr *x)
{
  int i;
  int is_float = 0;

  /* Check the size of the name array against the number of entries in
     the enum, to catch additions that people didn't sync.  */
  if ((sizeof (aop_map) / sizeof (aop_map[0]))
      != aop_last)
    error ("GDB bug: ax-general.c (ax_print): opcode map out of sync");

  for (i = 0; i < x->len;)
    {
      enum agent_op op = x->buf[i];

      if (op >= (sizeof (aop_map) / sizeof (aop_map[0]))
	  || !aop_map[op].name)
	{
	  fprintf_filtered (f, "%3d  <bad opcode %02x>\n", i, op);
	  i++;
	  continue;
	}
      if (i + 1 + aop_map[op].op_size > x->len)
	{
	  fprintf_filtered (f, "%3d  <incomplete opcode %s>\n",
			    i, aop_map[op].name);
	  break;
	}

      fprintf_filtered (f, "%3d  %s", i, aop_map[op].name);
      if (aop_map[op].op_size > 0)
	{
	  fputs_filtered (" ", f);

	  print_longest (f, 'd', 0,
			 read_const (x, i + 1, aop_map[op].op_size));
	}
      fprintf_filtered (f, "\n");
      i += 1 + aop_map[op].op_size;

      is_float = (op == aop_float);
    }
}


/* Given an agent expression AX, fill in an agent_reqs structure REQS
   describing it.  */
void
ax_reqs (struct agent_expr *ax, struct agent_reqs *reqs)
{
  int i;
  int height;

  /* Bit vector for registers used.  */
  int reg_mask_len = 1;
  unsigned char *reg_mask = xmalloc (reg_mask_len * sizeof (reg_mask[0]));

  /* Jump target table.  targets[i] is non-zero iff there is a jump to
     offset i.  */
  char *targets = (char *) alloca (ax->len * sizeof (targets[0]));

  /* Instruction boundary table.  boundary[i] is non-zero iff an
     instruction starts at offset i.  */
  char *boundary = (char *) alloca (ax->len * sizeof (boundary[0]));

  /* Stack height record.  iff either targets[i] or boundary[i] is
     non-zero, heights[i] is the height the stack should have before
     executing the bytecode at that point.  */
  int *heights = (int *) alloca (ax->len * sizeof (heights[0]));

  /* Pointer to a description of the present op.  */
  struct aop_map *op;

  memset (reg_mask, 0, reg_mask_len * sizeof (reg_mask[0]));
  memset (targets, 0, ax->len * sizeof (targets[0]));
  memset (boundary, 0, ax->len * sizeof (boundary[0]));

  reqs->max_height = reqs->min_height = height = 0;
  reqs->flaw = agent_flaw_none;
  reqs->max_data_size = 0;

  for (i = 0; i < ax->len; i += 1 + op->op_size)
    {
      if (ax->buf[i] > (sizeof (aop_map) / sizeof (aop_map[0])))
	{
	  reqs->flaw = agent_flaw_bad_instruction;
	  xfree (reg_mask);
	  return;
	}

      op = &aop_map[ax->buf[i]];

      if (!op->name)
	{
	  reqs->flaw = agent_flaw_bad_instruction;
	  xfree (reg_mask);
	  return;
	}

      if (i + 1 + op->op_size > ax->len)
	{
	  reqs->flaw = agent_flaw_incomplete_instruction;
	  xfree (reg_mask);
	  return;
	}

      /* If this instruction is a jump target, does the current stack
         height match the stack height at the jump source?  */
      if (targets[i] && (heights[i] != height))
	{
	  reqs->flaw = agent_flaw_height_mismatch;
	  xfree (reg_mask);
	  return;
	}

      boundary[i] = 1;
      heights[i] = height;

      height -= op->consumed;
      if (height < reqs->min_height)
	reqs->min_height = height;
      height += op->produced;
      if (height > reqs->max_height)
	reqs->max_height = height;

      if (op->data_size > reqs->max_data_size)
	reqs->max_data_size = op->data_size;

      /* For jump instructions, check that the target is a valid
         offset.  If it is, record the fact that that location is a
         jump target, and record the height we expect there.  */
      if (aop_goto == op - aop_map
	  || aop_if_goto == op - aop_map)
	{
	  int target = read_const (ax, i + 1, 2);
	  if (target < 0 || target >= ax->len)
	    {
	      reqs->flaw = agent_flaw_bad_jump;
	      xfree (reg_mask);
	      return;
	    }
	  /* Have we already found other jumps to the same location?  */
	  else if (targets[target])
	    {
	      if (heights[i] != height)
		{
		  reqs->flaw = agent_flaw_height_mismatch;
		  xfree (reg_mask);
		  return;
		}
	    }
	  else
	    {
	      targets[target] = 1;
	      heights[target] = height;
	    }
	}

      /* For unconditional jumps with a successor, check that the
         successor is a target, and pick up its stack height.  */
      if (aop_goto == op - aop_map
	  && i + 3 < ax->len)
	{
	  if (!targets[i + 3])
	    {
	      reqs->flaw = agent_flaw_hole;
	      xfree (reg_mask);
	      return;
	    }

	  height = heights[i + 3];
	}

      /* For reg instructions, record the register in the bit mask.  */
      if (aop_reg == op - aop_map)
	{
	  int reg = read_const (ax, i + 1, 2);
	  int byte = reg / 8;

	  /* Grow the bit mask if necessary.  */
	  if (byte >= reg_mask_len)
	    {
	      /* It's not appropriate to double here.  This isn't a
	         string buffer.  */
	      int new_len = byte + 1;
	      reg_mask = xrealloc (reg_mask,
				   new_len * sizeof (reg_mask[0]));
	      memset (reg_mask + reg_mask_len, 0,
		      (new_len - reg_mask_len) * sizeof (reg_mask[0]));
	      reg_mask_len = new_len;
	    }

	  reg_mask[byte] |= 1 << (reg % 8);
	}
    }

  /* Check that all the targets are on boundaries.  */
  for (i = 0; i < ax->len; i++)
    if (targets[i] && !boundary[i])
      {
	reqs->flaw = agent_flaw_bad_jump;
	xfree (reg_mask);
	return;
      }

  reqs->final_height = height;
  reqs->reg_mask_len = reg_mask_len;
  reqs->reg_mask = reg_mask;
}

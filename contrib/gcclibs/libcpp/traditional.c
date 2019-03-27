/* CPP Library - traditional lexical analysis and macro expansion.
   Copyright (C) 2002, 2004, 2005 Free Software Foundation, Inc.
   Contributed by Neil Booth, May 2002

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.  */

#include "config.h"
#include "system.h"
#include "cpplib.h"
#include "internal.h"

/* The replacement text of a function-like macro is stored as a
   contiguous sequence of aligned blocks, each representing the text
   between subsequent parameters.

   Each block comprises the text between its surrounding parameters,
   the length of that text, and the one-based index of the following
   parameter.  The final block in the replacement text is easily
   recognizable as it has an argument index of zero.  */

struct block
{
  unsigned int text_len;
  unsigned short arg_index;
  uchar text[1];
};

#define BLOCK_HEADER_LEN offsetof (struct block, text)
#define BLOCK_LEN(TEXT_LEN) CPP_ALIGN (BLOCK_HEADER_LEN + (TEXT_LEN))

/* Structure holding information about a function-like macro
   invocation.  */
struct fun_macro
{
  /* Memory buffer holding the trad_arg array.  */
  _cpp_buff *buff;

  /* An array of size the number of macro parameters + 1, containing
     the offsets of the start of each macro argument in the output
     buffer.  The argument continues until the character before the
     start of the next one.  */
  size_t *args;

  /* The hashnode of the macro.  */
  cpp_hashnode *node;

  /* The offset of the macro name in the output buffer.  */
  size_t offset;

  /* The line the macro name appeared on.  */
  unsigned int line;

  /* Zero-based index of argument being currently lexed.  */
  unsigned int argc;
};

/* Lexing state.  It is mostly used to prevent macro expansion.  */
enum ls {ls_none = 0,		/* Normal state.  */
	 ls_fun_open,		/* When looking for '('.  */
	 ls_fun_close,		/* When looking for ')'.  */
	 ls_defined,		/* After defined.  */
	 ls_defined_close,	/* Looking for ')' of defined().  */
	 ls_hash,		/* After # in preprocessor conditional.  */
	 ls_predicate,		/* After the predicate, maybe paren?  */
	 ls_answer};		/* In answer to predicate.  */

/* Lexing TODO: Maybe handle space in escaped newlines.  Stop lex.c
   from recognizing comments and directives during its lexing pass.  */

static const uchar *skip_whitespace (cpp_reader *, const uchar *, int);
static cpp_hashnode *lex_identifier (cpp_reader *, const uchar *);
static const uchar *copy_comment (cpp_reader *, const uchar *, int);
static void check_output_buffer (cpp_reader *, size_t);
static void push_replacement_text (cpp_reader *, cpp_hashnode *);
static bool scan_parameters (cpp_reader *, cpp_macro *);
static bool recursive_macro (cpp_reader *, cpp_hashnode *);
static void save_replacement_text (cpp_reader *, cpp_macro *, unsigned int);
static void maybe_start_funlike (cpp_reader *, cpp_hashnode *, const uchar *,
				 struct fun_macro *);
static void save_argument (struct fun_macro *, size_t);
static void replace_args_and_push (cpp_reader *, struct fun_macro *);
static size_t canonicalize_text (uchar *, const uchar *, size_t, uchar *);

/* Ensures we have N bytes' space in the output buffer, and
   reallocates it if not.  */
static void
check_output_buffer (cpp_reader *pfile, size_t n)
{
  /* We might need two bytes to terminate an unterminated comment, and
     one more to terminate the line with a NUL.  */
  n += 2 + 1;

  if (n > (size_t) (pfile->out.limit - pfile->out.cur))
    {
      size_t size = pfile->out.cur - pfile->out.base;
      size_t new_size = (size + n) * 3 / 2;

      pfile->out.base = XRESIZEVEC (unsigned char, pfile->out.base, new_size);
      pfile->out.limit = pfile->out.base + new_size;
      pfile->out.cur = pfile->out.base + size;
    }
}

/* Skip a C-style block comment in a macro as a result of -CC.
   Buffer->cur points to the initial asterisk of the comment.  */
static void
skip_macro_block_comment (cpp_reader *pfile)
{
  const uchar *cur = pfile->buffer->cur;

  cur++;
  if (*cur == '/')
    cur++;

  /* People like decorating comments with '*', so check for '/'
     instead for efficiency.  */
  while(! (*cur++ == '/' && cur[-2] == '*') )
    ;

  pfile->buffer->cur = cur;
}

/* CUR points to the asterisk introducing a comment in the current
   context.  IN_DEFINE is true if we are in the replacement text of a
   macro.

   The asterisk and following comment is copied to the buffer pointed
   to by pfile->out.cur, which must be of sufficient size.
   Unterminated comments are diagnosed, and correctly terminated in
   the output.  pfile->out.cur is updated depending upon IN_DEFINE,
   -C, -CC and pfile->state.in_directive.

   Returns a pointer to the first character after the comment in the
   input buffer.  */
static const uchar *
copy_comment (cpp_reader *pfile, const uchar *cur, int in_define)
{
  bool unterminated, copy = false;
  source_location src_loc = pfile->line_table->highest_line;
  cpp_buffer *buffer = pfile->buffer;

  buffer->cur = cur;
  if (pfile->context->prev)
    unterminated = false, skip_macro_block_comment (pfile);
  else
    unterminated = _cpp_skip_block_comment (pfile);
    
  if (unterminated)
    cpp_error_with_line (pfile, CPP_DL_ERROR, src_loc, 0,
			 "unterminated comment");

  /* Comments in directives become spaces so that tokens are properly
     separated when the ISO preprocessor re-lexes the line.  The
     exception is #define.  */
  if (pfile->state.in_directive)
    {
      if (in_define)
	{
	  if (CPP_OPTION (pfile, discard_comments_in_macro_exp))
	    pfile->out.cur--;
	  else
	    copy = true;
	}
      else
	pfile->out.cur[-1] = ' ';
    }
  else if (CPP_OPTION (pfile, discard_comments))
    pfile->out.cur--;
  else
    copy = true;

  if (copy)
    {
      size_t len = (size_t) (buffer->cur - cur);
      memcpy (pfile->out.cur, cur, len);
      pfile->out.cur += len;
      if (unterminated)
	{
	  *pfile->out.cur++ = '*';
	  *pfile->out.cur++ = '/';
	}
    }

  return buffer->cur;
}

/* CUR points to any character in the input buffer.  Skips over all
   contiguous horizontal white space and NULs, including comments if
   SKIP_COMMENTS, until reaching the first non-horizontal-whitespace
   character or the end of the current context.  Escaped newlines are
   removed.

   The whitespace is copied verbatim to the output buffer, except that
   comments are handled as described in copy_comment().
   pfile->out.cur is updated.

   Returns a pointer to the first character after the whitespace in
   the input buffer.  */
static const uchar *
skip_whitespace (cpp_reader *pfile, const uchar *cur, int skip_comments)
{
  uchar *out = pfile->out.cur;

  for (;;)
    {
      unsigned int c = *cur++;
      *out++ = c;

      if (is_nvspace (c))
	continue;

      if (c == '/' && *cur == '*' && skip_comments)
	{
	  pfile->out.cur = out;
	  cur = copy_comment (pfile, cur, false /* in_define */);
	  out = pfile->out.cur;
	  continue;
	}

      out--;
      break;
    }

  pfile->out.cur = out;
  return cur - 1;
}

/* Lexes and outputs an identifier starting at CUR, which is assumed
   to point to a valid first character of an identifier.  Returns
   the hashnode, and updates out.cur.  */
static cpp_hashnode *
lex_identifier (cpp_reader *pfile, const uchar *cur)
{
  size_t len;
  uchar *out = pfile->out.cur;
  cpp_hashnode *result;

  do
    *out++ = *cur++;
  while (is_numchar (*cur));

  CUR (pfile->context) = cur;
  len = out - pfile->out.cur;
  result = (cpp_hashnode *) ht_lookup (pfile->hash_table, pfile->out.cur,
				       len, HT_ALLOC);
  pfile->out.cur = out;
  return result;
}

/* Overlays the true file buffer temporarily with text of length LEN
   starting at START.  The true buffer is restored upon calling
   restore_buff().  */
void
_cpp_overlay_buffer (cpp_reader *pfile, const uchar *start, size_t len)
{
  cpp_buffer *buffer = pfile->buffer;

  pfile->overlaid_buffer = buffer;
  pfile->saved_cur = buffer->cur;
  pfile->saved_rlimit = buffer->rlimit;
  pfile->saved_line_base = buffer->next_line;
  buffer->need_line = false;

  buffer->cur = start;
  buffer->line_base = start;
  buffer->rlimit = start + len;
}

/* Restores a buffer overlaid by _cpp_overlay_buffer().  */
void
_cpp_remove_overlay (cpp_reader *pfile)
{
  cpp_buffer *buffer = pfile->overlaid_buffer;

  buffer->cur = pfile->saved_cur;
  buffer->rlimit = pfile->saved_rlimit;
  buffer->line_base = pfile->saved_line_base;
  buffer->need_line = true;

  pfile->overlaid_buffer = NULL;
}

/* Reads a logical line into the output buffer.  Returns TRUE if there
   is more text left in the buffer.  */
bool
_cpp_read_logical_line_trad (cpp_reader *pfile)
{
  do
    {
      if (pfile->buffer->need_line && !_cpp_get_fresh_line (pfile))
	return false;
    }
  while (!_cpp_scan_out_logical_line (pfile, NULL) || pfile->state.skipping);

  return pfile->buffer != NULL;
}

/* Set up state for finding the opening '(' of a function-like
   macro.  */
static void
maybe_start_funlike (cpp_reader *pfile, cpp_hashnode *node, const uchar *start, struct fun_macro *macro)
{
  unsigned int n = node->value.macro->paramc + 1;

  if (macro->buff)
    _cpp_release_buff (pfile, macro->buff);
  macro->buff = _cpp_get_buff (pfile, n * sizeof (size_t));
  macro->args = (size_t *) BUFF_FRONT (macro->buff);
  macro->node = node;
  macro->offset = start - pfile->out.base;
  macro->argc = 0;
}

/* Save the OFFSET of the start of the next argument to MACRO.  */
static void
save_argument (struct fun_macro *macro, size_t offset)
{
  macro->argc++;
  if (macro->argc <= macro->node->value.macro->paramc)
    macro->args[macro->argc] = offset;
}

/* Copies the next logical line in the current buffer (starting at
   buffer->cur) to the output buffer.  The output is guaranteed to
   terminate with a NUL character.  buffer->cur is updated.

   If MACRO is non-NULL, then we are scanning the replacement list of
   MACRO, and we call save_replacement_text() every time we meet an
   argument.  */
bool
_cpp_scan_out_logical_line (cpp_reader *pfile, cpp_macro *macro)
{
  bool result = true;
  cpp_context *context;
  const uchar *cur;
  uchar *out;
  struct fun_macro fmacro;
  unsigned int c, paren_depth = 0, quote;
  enum ls lex_state = ls_none;
  bool header_ok;
  const uchar *start_of_input_line;

  fmacro.buff = NULL;

  quote = 0;
  header_ok = pfile->state.angled_headers;
  CUR (pfile->context) = pfile->buffer->cur;
  RLIMIT (pfile->context) = pfile->buffer->rlimit;
  pfile->out.cur = pfile->out.base;
  pfile->out.first_line = pfile->line_table->highest_line;
  /* start_of_input_line is needed to make sure that directives really,
     really start at the first character of the line.  */
  start_of_input_line = pfile->buffer->cur;
 new_context:
  context = pfile->context;
  cur = CUR (context);
  check_output_buffer (pfile, RLIMIT (context) - cur);
  out = pfile->out.cur;

  for (;;)
    {
      if (!context->prev
	  && cur >= pfile->buffer->notes[pfile->buffer->cur_note].pos)
	{
	  pfile->buffer->cur = cur;
	  _cpp_process_line_notes (pfile, false);
	}
      c = *cur++;
      *out++ = c;

      /* Whitespace should "continue" out of the switch,
	 non-whitespace should "break" out of it.  */
      switch (c)
	{
	case ' ':
	case '\t':
	case '\f':
	case '\v':
	case '\0':
	  continue;

	case '\n':
	  /* If this is a macro's expansion, pop it.  */
	  if (context->prev)
	    {
	      pfile->out.cur = out - 1;
	      _cpp_pop_context (pfile);
	      goto new_context;
	    }

	  /* Omit the newline from the output buffer.  */
	  pfile->out.cur = out - 1;
	  pfile->buffer->cur = cur;
	  pfile->buffer->need_line = true;
	  CPP_INCREMENT_LINE (pfile, 0);

	  if ((lex_state == ls_fun_open || lex_state == ls_fun_close)
	      && !pfile->state.in_directive
	      && _cpp_get_fresh_line (pfile))
	    {
	      /* Newlines in arguments become a space, but we don't
		 clear any in-progress quote.  */
	      if (lex_state == ls_fun_close)
		out[-1] = ' ';
	      cur = pfile->buffer->cur;
	      continue;
	    }
	  goto done;

	case '<':
	  if (header_ok)
	    quote = '>';
	  break;
	case '>':
	  if (c == quote)
	    quote = 0;
	  break;

	case '"':
	case '\'':
	  if (c == quote)
	    quote = 0;
	  else if (!quote)
	    quote = c;
	  break;

	case '\\':
	  /* Skip escaped quotes here, it's easier than above.  */
	  if (*cur == '\\' || *cur == '"' || *cur == '\'')
	    *out++ = *cur++;
	  break;

	case '/':
	  /* Traditional CPP does not recognize comments within
	     literals.  */
	  if (!quote && *cur == '*')
	    {
	      pfile->out.cur = out;
	      cur = copy_comment (pfile, cur, macro != 0);
	      out = pfile->out.cur;
	      continue;
	    }
	  break;

	case '_':
	case 'a': case 'b': case 'c': case 'd': case 'e': case 'f':
	case 'g': case 'h': case 'i': case 'j': case 'k': case 'l':
	case 'm': case 'n': case 'o': case 'p': case 'q': case 'r':
	case 's': case 't': case 'u': case 'v': case 'w': case 'x':
	case 'y': case 'z':
	case 'A': case 'B': case 'C': case 'D': case 'E': case 'F':
	case 'G': case 'H': case 'I': case 'J': case 'K': case 'L':
	case 'M': case 'N': case 'O': case 'P': case 'Q': case 'R':
	case 'S': case 'T': case 'U': case 'V': case 'W': case 'X':
	case 'Y': case 'Z':
	  if (!pfile->state.skipping && (quote == 0 || macro))
	    {
	      cpp_hashnode *node;
	      uchar *out_start = out - 1;

	      pfile->out.cur = out_start;
	      node = lex_identifier (pfile, cur - 1);
	      out = pfile->out.cur;
	      cur = CUR (context);

	      if (node->type == NT_MACRO
		  /* Should we expand for ls_answer?  */
		  && (lex_state == ls_none || lex_state == ls_fun_open)
		  && !pfile->state.prevent_expansion)
		{
		  /* Macros invalidate MI optimization.  */
		  pfile->mi_valid = false;
		  if (! (node->flags & NODE_BUILTIN)
		      && node->value.macro->fun_like)
		    {
		      maybe_start_funlike (pfile, node, out_start, &fmacro);
		      lex_state = ls_fun_open;
		      fmacro.line = pfile->line_table->highest_line;
		      continue;
		    }
		  else if (!recursive_macro (pfile, node))
		    {
		      /* Remove the object-like macro's name from the
			 output, and push its replacement text.  */
		      pfile->out.cur = out_start;
		      push_replacement_text (pfile, node);
		      lex_state = ls_none;
		      goto new_context;
		    }
		}
	      else if (macro && (node->flags & NODE_MACRO_ARG) != 0)
		{
		  /* Found a parameter in the replacement text of a
		     #define.  Remove its name from the output.  */
		  pfile->out.cur = out_start;
		  save_replacement_text (pfile, macro, node->value.arg_index);
		  out = pfile->out.base;
		}
	      else if (lex_state == ls_hash)
		{
		  lex_state = ls_predicate;
		  continue;
		}
	      else if (pfile->state.in_expression
		       && node == pfile->spec_nodes.n_defined)
		{
		  lex_state = ls_defined;
		  continue;
		}
	    }
	  break;

	case '(':
	  if (quote == 0)
	    {
	      paren_depth++;
	      if (lex_state == ls_fun_open)
		{
		  if (recursive_macro (pfile, fmacro.node))
		    lex_state = ls_none;
		  else
		    {
		      lex_state = ls_fun_close;
		      paren_depth = 1;
		      out = pfile->out.base + fmacro.offset;
		      fmacro.args[0] = fmacro.offset;
		    }
		}
	      else if (lex_state == ls_predicate)
		lex_state = ls_answer;
	      else if (lex_state == ls_defined)
		lex_state = ls_defined_close;
	    }
	  break;

	case ',':
	  if (quote == 0 && lex_state == ls_fun_close && paren_depth == 1)
	    save_argument (&fmacro, out - pfile->out.base);
	  break;

	case ')':
	  if (quote == 0)
	    {
	      paren_depth--;
	      if (lex_state == ls_fun_close && paren_depth == 0)
		{
		  cpp_macro *m = fmacro.node->value.macro;

		  m->used = 1;
		  lex_state = ls_none;
		  save_argument (&fmacro, out - pfile->out.base);

		  /* A single zero-length argument is no argument.  */
		  if (fmacro.argc == 1
		      && m->paramc == 0
		      && out == pfile->out.base + fmacro.offset + 1)
		    fmacro.argc = 0;

		  if (_cpp_arguments_ok (pfile, m, fmacro.node, fmacro.argc))
		    {
		      /* Remove the macro's invocation from the
			 output, and push its replacement text.  */
		      pfile->out.cur = (pfile->out.base
					     + fmacro.offset);
		      CUR (context) = cur;
		      replace_args_and_push (pfile, &fmacro);
		      goto new_context;
		    }
		}
	      else if (lex_state == ls_answer || lex_state == ls_defined_close)
		lex_state = ls_none;
	    }
	  break;

	case '#':
	  if (cur - 1 == start_of_input_line
	      /* A '#' from a macro doesn't start a directive.  */
	      && !pfile->context->prev
	      && !pfile->state.in_directive)
	    {
	      /* A directive.  With the way _cpp_handle_directive
		 currently works, we only want to call it if either we
		 know the directive is OK, or we want it to fail and
		 be removed from the output.  If we want it to be
		 passed through (the assembler case) then we must not
		 call _cpp_handle_directive.  */
	      pfile->out.cur = out;
	      cur = skip_whitespace (pfile, cur, true /* skip_comments */);
	      out = pfile->out.cur;

	      if (*cur == '\n')
		{
		  /* Null directive.  Ignore it and don't invalidate
		     the MI optimization.  */
		  pfile->buffer->need_line = true;
		  CPP_INCREMENT_LINE (pfile, 0);
		  result = false;
		  goto done;
		}
	      else
		{
		  bool do_it = false;

		  if (is_numstart (*cur)
		      && CPP_OPTION (pfile, lang) != CLK_ASM)
		    do_it = true;
		  else if (is_idstart (*cur))
		    /* Check whether we know this directive, but don't
		       advance.  */
		    do_it = lex_identifier (pfile, cur)->is_directive;

		  if (do_it || CPP_OPTION (pfile, lang) != CLK_ASM)
		    {
		      /* This is a kludge.  We want to have the ISO
			 preprocessor lex the next token.  */
		      pfile->buffer->cur = cur;
		      _cpp_handle_directive (pfile, false /* indented */);
		      result = false;
		      goto done;
		    }
		}
	    }

	  if (pfile->state.in_expression)
	    {
	      lex_state = ls_hash;
	      continue;
	    }
	  break;

	default:
	  break;
	}

      /* Non-whitespace disables MI optimization and stops treating
	 '<' as a quote in #include.  */
      header_ok = false;
      if (!pfile->state.in_directive)
	pfile->mi_valid = false;

      if (lex_state == ls_none)
	continue;

      /* Some of these transitions of state are syntax errors.  The
	 ISO preprocessor will issue errors later.  */
      if (lex_state == ls_fun_open)
	/* Missing '('.  */
	lex_state = ls_none;
      else if (lex_state == ls_hash
	       || lex_state == ls_predicate
	       || lex_state == ls_defined)
	lex_state = ls_none;

      /* ls_answer and ls_defined_close keep going until ')'.  */
    }

 done:
  if (fmacro.buff)
    _cpp_release_buff (pfile, fmacro.buff);

  if (lex_state == ls_fun_close)
    cpp_error_with_line (pfile, CPP_DL_ERROR, fmacro.line, 0,
			 "unterminated argument list invoking macro \"%s\"",
			 NODE_NAME (fmacro.node));
  return result;
}

/* Push a context holding the replacement text of the macro NODE on
   the context stack.  NODE is either object-like, or a function-like
   macro with no arguments.  */
static void
push_replacement_text (cpp_reader *pfile, cpp_hashnode *node)
{
  size_t len;
  const uchar *text;
  uchar *buf;

  if (node->flags & NODE_BUILTIN)
    {
      text = _cpp_builtin_macro_text (pfile, node);
      len = ustrlen (text);
      buf = _cpp_unaligned_alloc (pfile, len + 1);
      memcpy (buf, text, len);
      buf[len]='\n';
      text = buf;
    }
  else
    {
      cpp_macro *macro = node->value.macro;
      macro->used = 1;
      text = macro->exp.text;
      macro->traditional = 1;
      len = macro->count;
    }

  _cpp_push_text_context (pfile, node, text, len);
}

/* Returns TRUE if traditional macro recursion is detected.  */
static bool
recursive_macro (cpp_reader *pfile, cpp_hashnode *node)
{
  bool recursing = !!(node->flags & NODE_DISABLED);

  /* Object-like macros that are already expanding are necessarily
     recursive.

     However, it is possible to have traditional function-like macros
     that are not infinitely recursive but recurse to any given depth.
     Further, it is easy to construct examples that get ever longer
     until the point they stop recursing.  So there is no easy way to
     detect true recursion; instead we assume any expansion more than
     20 deep since the first invocation of this macro must be
     recursing.  */
  if (recursing && node->value.macro->fun_like)
    {
      size_t depth = 0;
      cpp_context *context = pfile->context;

      do
	{
	  depth++;
	  if (context->macro == node && depth > 20)
	    break;
	  context = context->prev;
	}
      while (context);
      recursing = context != NULL;
    }

  if (recursing)
    cpp_error (pfile, CPP_DL_ERROR,
	       "detected recursion whilst expanding macro \"%s\"",
	       NODE_NAME (node));

  return recursing;
}

/* Return the length of the replacement text of a function-like or
   object-like non-builtin macro.  */
size_t
_cpp_replacement_text_len (const cpp_macro *macro)
{
  size_t len;

  if (macro->fun_like && (macro->paramc != 0))
    {
      const uchar *exp;

      len = 0;
      for (exp = macro->exp.text;;)
	{
	  struct block *b = (struct block *) exp;

	  len += b->text_len;
	  if (b->arg_index == 0)
	    break;
	  len += NODE_LEN (macro->params[b->arg_index - 1]);
	  exp += BLOCK_LEN (b->text_len);
	}
    }
  else
    len = macro->count;
  
  return len;
}

/* Copy the replacement text of MACRO to DEST, which must be of
   sufficient size.  It is not NUL-terminated.  The next character is
   returned.  */
uchar *
_cpp_copy_replacement_text (const cpp_macro *macro, uchar *dest)
{
  if (macro->fun_like && (macro->paramc != 0))
    {
      const uchar *exp;

      for (exp = macro->exp.text;;)
	{
	  struct block *b = (struct block *) exp;
	  cpp_hashnode *param;

	  memcpy (dest, b->text, b->text_len);
	  dest += b->text_len;
	  if (b->arg_index == 0)
	    break;
	  param = macro->params[b->arg_index - 1];
	  memcpy (dest, NODE_NAME (param), NODE_LEN (param));
	  dest += NODE_LEN (param);
	  exp += BLOCK_LEN (b->text_len);
	}
    }
  else
    {
      memcpy (dest, macro->exp.text, macro->count);
      dest += macro->count;
    }

  return dest;
}

/* Push a context holding the replacement text of the macro NODE on
   the context stack.  NODE is either object-like, or a function-like
   macro with no arguments.  */
static void
replace_args_and_push (cpp_reader *pfile, struct fun_macro *fmacro)
{
  cpp_macro *macro = fmacro->node->value.macro;

  if (macro->paramc == 0)
    push_replacement_text (pfile, fmacro->node);
  else
    {
      const uchar *exp;
      uchar *p;
      _cpp_buff *buff;
      size_t len = 0;

      /* Calculate the length of the argument-replaced text.  */
      for (exp = macro->exp.text;;)
	{
	  struct block *b = (struct block *) exp;

	  len += b->text_len;
	  if (b->arg_index == 0)
	    break;
	  len += (fmacro->args[b->arg_index]
		  - fmacro->args[b->arg_index - 1] - 1);
	  exp += BLOCK_LEN (b->text_len);
	}

      /* Allocate room for the expansion plus \n.  */
      buff = _cpp_get_buff (pfile, len + 1);

      /* Copy the expansion and replace arguments.  */
      p = BUFF_FRONT (buff);
      for (exp = macro->exp.text;;)
	{
	  struct block *b = (struct block *) exp;
	  size_t arglen;

	  memcpy (p, b->text, b->text_len);
	  p += b->text_len;
	  if (b->arg_index == 0)
	    break;
	  arglen = (fmacro->args[b->arg_index]
		    - fmacro->args[b->arg_index - 1] - 1);
	  memcpy (p, pfile->out.base + fmacro->args[b->arg_index - 1],
		  arglen);
	  p += arglen;
	  exp += BLOCK_LEN (b->text_len);
	}

      /* \n-terminate.  */
      *p = '\n';
      _cpp_push_text_context (pfile, fmacro->node, BUFF_FRONT (buff), len);

      /* So we free buffer allocation when macro is left.  */
      pfile->context->buff = buff;
    }
}

/* Read and record the parameters, if any, of a function-like macro
   definition.  Destroys pfile->out.cur.

   Returns true on success, false on failure (syntax error or a
   duplicate parameter).  On success, CUR (pfile->context) is just
   past the closing parenthesis.  */
static bool
scan_parameters (cpp_reader *pfile, cpp_macro *macro)
{
  const uchar *cur = CUR (pfile->context) + 1;
  bool ok;

  for (;;)
    {
      cur = skip_whitespace (pfile, cur, true /* skip_comments */);

      if (is_idstart (*cur))
	{
	  ok = false;
	  if (_cpp_save_parameter (pfile, macro, lex_identifier (pfile, cur)))
	    break;
	  cur = skip_whitespace (pfile, CUR (pfile->context),
				 true /* skip_comments */);
	  if (*cur == ',')
	    {
	      cur++;
	      continue;
	    }
	  ok = (*cur == ')');
	  break;
	}

      ok = (*cur == ')' && macro->paramc == 0);
      break;
    }

  if (!ok)
    cpp_error (pfile, CPP_DL_ERROR, "syntax error in macro parameter list");

  CUR (pfile->context) = cur + (*cur == ')');

  return ok;
}

/* Save the text from pfile->out.base to pfile->out.cur as
   the replacement text for the current macro, followed by argument
   ARG_INDEX, with zero indicating the end of the replacement
   text.  */
static void
save_replacement_text (cpp_reader *pfile, cpp_macro *macro,
		       unsigned int arg_index)
{
  size_t len = pfile->out.cur - pfile->out.base;
  uchar *exp;

  if (macro->paramc == 0)
    {
      /* Object-like and function-like macros without parameters
	 simply store their \n-terminated replacement text.  */
      exp = _cpp_unaligned_alloc (pfile, len + 1);
      memcpy (exp, pfile->out.base, len);
      exp[len] = '\n';
      macro->exp.text = exp;
      macro->traditional = 1;
      macro->count = len;
    }
  else
    {
      /* Store the text's length (unsigned int), the argument index
	 (unsigned short, base 1) and then the text.  */
      size_t blen = BLOCK_LEN (len);
      struct block *block;

      if (macro->count + blen > BUFF_ROOM (pfile->a_buff))
	_cpp_extend_buff (pfile, &pfile->a_buff, macro->count + blen);

      exp = BUFF_FRONT (pfile->a_buff);
      block = (struct block *) (exp + macro->count);
      macro->exp.text = exp;
      macro->traditional = 1;

      /* Write out the block information.  */
      block->text_len = len;
      block->arg_index = arg_index;
      memcpy (block->text, pfile->out.base, len);

      /* Lex the rest into the start of the output buffer.  */
      pfile->out.cur = pfile->out.base;

      macro->count += blen;

      /* If we've finished, commit the memory.  */
      if (arg_index == 0)
	BUFF_FRONT (pfile->a_buff) += macro->count;
    }
}

/* Analyze and save the replacement text of a macro.  Returns true on
   success.  */
bool
_cpp_create_trad_definition (cpp_reader *pfile, cpp_macro *macro)
{
  const uchar *cur;
  uchar *limit;
  cpp_context *context = pfile->context;

  /* The context has not been set up for command line defines, and CUR
     has not been updated for the macro name for in-file defines.  */
  pfile->out.cur = pfile->out.base;
  CUR (context) = pfile->buffer->cur;
  RLIMIT (context) = pfile->buffer->rlimit;
  check_output_buffer (pfile, RLIMIT (context) - CUR (context));

  /* Is this a function-like macro?  */
  if (* CUR (context) == '(')
    {
      bool ok = scan_parameters (pfile, macro);

      /* Remember the params so we can clear NODE_MACRO_ARG flags.  */
      macro->params = (cpp_hashnode **) BUFF_FRONT (pfile->a_buff);

      /* Setting macro to NULL indicates an error occurred, and
	 prevents unnecessary work in _cpp_scan_out_logical_line.  */
      if (!ok)
	macro = NULL;
      else
	{
	  BUFF_FRONT (pfile->a_buff) = (uchar *) &macro->params[macro->paramc];
	  macro->fun_like = 1;
	}
    }

  /* Skip leading whitespace in the replacement text.  */
  pfile->buffer->cur
    = skip_whitespace (pfile, CUR (context),
		       CPP_OPTION (pfile, discard_comments_in_macro_exp));

  pfile->state.prevent_expansion++;
  _cpp_scan_out_logical_line (pfile, macro);
  pfile->state.prevent_expansion--;

  if (!macro)
    return false;

  /* Skip trailing white space.  */
  cur = pfile->out.base;
  limit = pfile->out.cur;
  while (limit > cur && is_space (limit[-1]))
    limit--;
  pfile->out.cur = limit;
  save_replacement_text (pfile, macro, 0);

  return true;
}

/* Copy SRC of length LEN to DEST, but convert all contiguous
   whitespace to a single space, provided it is not in quotes.  The
   quote currently in effect is pointed to by PQUOTE, and is updated
   by the function.  Returns the number of bytes copied.  */
static size_t
canonicalize_text (uchar *dest, const uchar *src, size_t len, uchar *pquote)
{
  uchar *orig_dest = dest;
  uchar quote = *pquote;

  while (len)
    {
      if (is_space (*src) && !quote)
	{
	  do
	    src++, len--;
	  while (len && is_space (*src));
	  *dest++ = ' ';
	}
      else
	{
	  if (*src == '\'' || *src == '"')
	    {
	      if (!quote)
		quote = *src;
	      else if (quote == *src)
		quote = 0;
	    }
	  *dest++ = *src++, len--;
	}
    }

  *pquote = quote;
  return dest - orig_dest;
}

/* Returns true if MACRO1 and MACRO2 have expansions different other
   than in the form of their whitespace.  */
bool
_cpp_expansions_different_trad (const cpp_macro *macro1,
				const cpp_macro *macro2)
{
  uchar *p1 = XNEWVEC (uchar, macro1->count + macro2->count);
  uchar *p2 = p1 + macro1->count;
  uchar quote1 = 0, quote2 = 0;
  bool mismatch;
  size_t len1, len2;

  if (macro1->paramc > 0)
    {
      const uchar *exp1 = macro1->exp.text, *exp2 = macro2->exp.text;

      mismatch = true;
      for (;;)
	{
	  struct block *b1 = (struct block *) exp1;
	  struct block *b2 = (struct block *) exp2;

	  if (b1->arg_index != b2->arg_index)
	    break;

	  len1 = canonicalize_text (p1, b1->text, b1->text_len, &quote1);
	  len2 = canonicalize_text (p2, b2->text, b2->text_len, &quote2);
	  if (len1 != len2 || memcmp (p1, p2, len1))
	    break;
	  if (b1->arg_index == 0)
	    {
	      mismatch = false;
	      break;
	    }
	  exp1 += BLOCK_LEN (b1->text_len);
	  exp2 += BLOCK_LEN (b2->text_len);
	}
    }
  else
    {
      len1 = canonicalize_text (p1, macro1->exp.text, macro1->count, &quote1);
      len2 = canonicalize_text (p2, macro2->exp.text, macro2->count, &quote2);
      mismatch = (len1 != len2 || memcmp (p1, p2, len1));
    }

  free (p1);
  return mismatch;
}

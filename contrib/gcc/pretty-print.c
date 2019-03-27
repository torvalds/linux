/* Various declarations for language-independent pretty-print subroutines.
   Copyright (C) 2003, 2004, 2005 Free Software Foundation, Inc.
   Contributed by Gabriel Dos Reis <gdr@integrable-solutions.net>

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
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */

#include "config.h"
#undef FLOAT /* This is for hpux. They should change hpux.  */
#undef FFS  /* Some systems define this in param.h.  */
#include "system.h"
#include "coretypes.h"
#include "intl.h"
#include "pretty-print.h"
#include "tree.h"

#define obstack_chunk_alloc xmalloc
#define obstack_chunk_free  free

/* A pointer to the formatted diagnostic message.  */
#define pp_formatted_text_data(PP) \
   ((const char *) obstack_base (pp_base (PP)->buffer->obstack))

/* Format an integer given by va_arg (ARG, type-specifier T) where
   type-specifier is a precision modifier as indicated by PREC.  F is
   a string used to construct the appropriate format-specifier.  */
#define pp_integer_with_precision(PP, ARG, PREC, T, F)       \
  do                                                         \
    switch (PREC)                                            \
      {                                                      \
      case 0:                                                \
        pp_scalar (PP, "%" F, va_arg (ARG, T));              \
        break;                                               \
                                                             \
      case 1:                                                \
        pp_scalar (PP, "%l" F, va_arg (ARG, long T));        \
        break;                                               \
                                                             \
      case 2:                                                \
        pp_scalar (PP, "%ll" F, va_arg (ARG, long long T));  \
        break;                                               \
                                                             \
      default:                                               \
        break;                                               \
      }                                                      \
  while (0)


/* Subroutine of pp_set_maximum_length.  Set up PRETTY-PRINTER's
   internal maximum characters per line.  */
static void
pp_set_real_maximum_length (pretty_printer *pp)
{
  /* If we're told not to wrap lines then do the obvious thing.  In case
     we'll emit prefix only once per message, it is appropriate
     not to increase unnecessarily the line-length cut-off.  */
  if (!pp_is_wrapping_line (pp)
      || pp_prefixing_rule (pp) == DIAGNOSTICS_SHOW_PREFIX_ONCE
      || pp_prefixing_rule (pp) == DIAGNOSTICS_SHOW_PREFIX_NEVER)
    pp->maximum_length = pp_line_cutoff (pp);
  else
    {
      int prefix_length = pp->prefix ? strlen (pp->prefix) : 0;
      /* If the prefix is ridiculously too long, output at least
         32 characters.  */
      if (pp_line_cutoff (pp) - prefix_length < 32)
	pp->maximum_length = pp_line_cutoff (pp) + 32;
      else
	pp->maximum_length = pp_line_cutoff (pp);
    }
}

/* Clear PRETTY-PRINTER's output state.  */
static inline void
pp_clear_state (pretty_printer *pp)
{
  pp->emitted_prefix = false;
  pp_indentation (pp) = 0;
}

/* Flush the formatted text of PRETTY-PRINTER onto the attached stream.  */
void
pp_write_text_to_stream (pretty_printer *pp)
{
  const char *text = pp_formatted_text (pp);
  fputs (text, pp->buffer->stream);
  pp_clear_output_area (pp);
}

/* Wrap a text delimited by START and END into PRETTY-PRINTER.  */
static void
pp_wrap_text (pretty_printer *pp, const char *start, const char *end)
{
  bool wrapping_line = pp_is_wrapping_line (pp);

  while (start != end)
    {
      /* Dump anything bordered by whitespaces.  */
      {
	const char *p = start;
	while (p != end && !ISBLANK (*p) && *p != '\n')
	  ++p;
	if (wrapping_line
            && p - start >= pp_remaining_character_count_for_line (pp))
	  pp_newline (pp);
	pp_append_text (pp, start, p);
	start = p;
      }

      if (start != end && ISBLANK (*start))
	{
	  pp_space (pp);
	  ++start;
	}
      if (start != end && *start == '\n')
	{
	  pp_newline (pp);
	  ++start;
	}
    }
}

/* Same as pp_wrap_text but wrap text only when in line-wrapping mode.  */
static inline void
pp_maybe_wrap_text (pretty_printer *pp, const char *start, const char *end)
{
  if (pp_is_wrapping_line (pp))
    pp_wrap_text (pp, start, end);
  else
    pp_append_text (pp, start, end);
}

/* Append to the output area of PRETTY-PRINTER a string specified by its
   STARTing character and LENGTH.  */
static inline void
pp_append_r (pretty_printer *pp, const char *start, int length)
{
  obstack_grow (pp->buffer->obstack, start, length);
  pp->buffer->line_length += length;
}

/* Insert enough spaces into the output area of PRETTY-PRINTER to bring
   the column position to the current indentation level, assuming that a
   newline has just been written to the buffer.  */
void
pp_base_indent (pretty_printer *pp)
{
  int n = pp_indentation (pp);
  int i;

  for (i = 0; i < n; ++i)
    pp_space (pp);
}

/* The following format specifiers are recognized as being client independent:
   %d, %i: (signed) integer in base ten.
   %u: unsigned integer in base ten.
   %o: unsigned integer in base eight.
   %x: unsigned integer in base sixteen.
   %ld, %li, %lo, %lu, %lx: long versions of the above.
   %lld, %lli, %llo, %llu, %llx: long long versions.
   %wd, %wi, %wo, %wu, %wx: HOST_WIDE_INT versions.
   %c: character.
   %s: string.
   %p: pointer.
   %m: strerror(text->err_no) - does not consume a value from args_ptr.
   %%: '%'.
   %<: opening quote.
   %>: closing quote.
   %': apostrophe (should only be used in untranslated messages;
       translations should use appropriate punctuation directly).
   %.*s: a substring the length of which is specified by an argument
	 integer.
   %Ns: likewise, but length specified as constant in the format string.
   %H: location_t.
   %J: a decl tree, from which DECL_SOURCE_LOCATION will be recorded.
   Flag 'q': quote formatted text (must come immediately after '%').

   Arguments can be used sequentially, or through %N$ resp. *N$
   notation Nth argument after the format string.  If %N$ / *N$
   notation is used, it must be used for all arguments, except %m, %%,
   %<, %> and %', which may not have a number, as they do not consume
   an argument.  When %M$.*N$s is used, M must be N + 1.  (This may
   also be written %M$.*s, provided N is not otherwise used.)  The
   format string must have conversion specifiers with argument numbers
   1 up to highest argument; each argument may only be used once.
   A format string can have at most 30 arguments.  */

/* Formatting phases 1 and 2: render TEXT->format_spec plus
   TEXT->args_ptr into a series of chunks in PP->buffer->args[].
   Phase 3 is in pp_base_format_text.  */

void
pp_base_format (pretty_printer *pp, text_info *text)
{
  output_buffer *buffer = pp->buffer;
  const char *p;
  const char **args;
  struct chunk_info *new_chunk_array;

  unsigned int curarg = 0, chunk = 0, argno;
  pp_wrapping_mode_t old_wrapping_mode;
  bool any_unnumbered = false, any_numbered = false;
  const char **formatters[PP_NL_ARGMAX];

  /* Allocate a new chunk structure.  */
  new_chunk_array = XOBNEW (&buffer->chunk_obstack, struct chunk_info);
  new_chunk_array->prev = buffer->cur_chunk_array;
  buffer->cur_chunk_array = new_chunk_array;
  args = new_chunk_array->args;

  /* Formatting phase 1: split up TEXT->format_spec into chunks in
     PP->buffer->args[].  Even-numbered chunks are to be output
     verbatim, odd-numbered chunks are format specifiers.
     %m, %%, %<, %>, and %' are replaced with the appropriate text at
     this point.  */

  memset (formatters, 0, sizeof formatters);
  
  for (p = text->format_spec; *p; )
    {
      while (*p != '\0' && *p != '%')
	{
	  obstack_1grow (&buffer->chunk_obstack, *p);
	  p++;
	}

      if (*p == '\0')
	break;

      switch (*++p)
	{
	case '\0':
	  gcc_unreachable ();
	  
	case '%':
	  obstack_1grow (&buffer->chunk_obstack, '%');
	  p++;
	  continue;

	case '<':
	  obstack_grow (&buffer->chunk_obstack,
			open_quote, strlen (open_quote));
	  p++;
	  continue;

	case '>':
	case '\'':
	  obstack_grow (&buffer->chunk_obstack,
			close_quote, strlen (close_quote));
	  p++;
	  continue;

	case 'm':
	  {
	    const char *errstr = xstrerror (text->err_no);
	    obstack_grow (&buffer->chunk_obstack, errstr, strlen (errstr));
	  }
	  p++;
	  continue;

	default:
	  /* Handled in phase 2.  Terminate the plain chunk here.  */
	  obstack_1grow (&buffer->chunk_obstack, '\0');
	  gcc_assert (chunk < PP_NL_ARGMAX * 2);
	  args[chunk++] = XOBFINISH (&buffer->chunk_obstack, const char *);
	  break;
	}

      if (ISDIGIT (*p))
	{
	  char *end;
	  argno = strtoul (p, &end, 10) - 1;
	  p = end;
	  gcc_assert (*p == '$');
	  p++;

	  any_numbered = true;
	  gcc_assert (!any_unnumbered);
	}
      else
	{
	  argno = curarg++;
	  any_unnumbered = true;
	  gcc_assert (!any_numbered);
	}
      gcc_assert (argno < PP_NL_ARGMAX);
      gcc_assert (!formatters[argno]);
      formatters[argno] = &args[chunk];
      do
	{
	  obstack_1grow (&buffer->chunk_obstack, *p);
	  p++;
	}
      while (strchr ("qwl+#", p[-1]));

      if (p[-1] == '.')
	{
	  /* We handle '%.Ns' and '%.*s' or '%M$.*N$s'
	     (where M == N + 1).  */
	  if (ISDIGIT (*p))
	    {
	      do
		{
		  obstack_1grow (&buffer->chunk_obstack, *p);
		  p++;
		}
	      while (ISDIGIT (p[-1]));
	      gcc_assert (p[-1] == 's');
	    }
	  else
	    {
	      gcc_assert (*p == '*');
	      obstack_1grow (&buffer->chunk_obstack, '*');
	      p++;

	      if (ISDIGIT (*p))
		{
		  char *end;
		  unsigned int argno2 = strtoul (p, &end, 10) - 1;
		  p = end;
		  gcc_assert (argno2 == argno - 1);
		  gcc_assert (!any_unnumbered);
		  gcc_assert (*p == '$');

		  p++;
		  formatters[argno2] = formatters[argno];
		}
	      else
		{
		  gcc_assert (!any_numbered);
		  formatters[argno+1] = formatters[argno];
		  curarg++;
		}
	      gcc_assert (*p == 's');
	      obstack_1grow (&buffer->chunk_obstack, 's');
	      p++;
	    }
	}
      if (*p == '\0')
	break;

      obstack_1grow (&buffer->chunk_obstack, '\0');
      gcc_assert (chunk < PP_NL_ARGMAX * 2);
      args[chunk++] = XOBFINISH (&buffer->chunk_obstack, const char *);
    }

  obstack_1grow (&buffer->chunk_obstack, '\0');
  gcc_assert (chunk < PP_NL_ARGMAX * 2);
  args[chunk++] = XOBFINISH (&buffer->chunk_obstack, const char *);
  args[chunk] = 0;
		  
  /* Set output to the argument obstack, and switch line-wrapping and
     prefixing off.  */
  buffer->obstack = &buffer->chunk_obstack;
  old_wrapping_mode = pp_set_verbatim_wrapping (pp);

  /* Second phase.  Replace each formatter with the formatted text it
     corresponds to.  */

  for (argno = 0; formatters[argno]; argno++)
    {
      int precision = 0;
      bool wide = false;
      bool plus = false;
      bool hash = false;
      bool quote = false;

      /* We do not attempt to enforce any ordering on the modifier
	 characters.  */

      for (p = *formatters[argno];; p++)
	{
	  switch (*p)
	    {
	    case 'q':
	      gcc_assert (!quote);
	      quote = true;
	      continue;

	    case '+':
	      gcc_assert (!plus);
	      plus = true;
	      continue;

	    case '#':
	      gcc_assert (!hash);
	      hash = true;
	      continue;

	    case 'w':
	      gcc_assert (!wide);
	      wide = true;
	      continue;

	    case 'l':
	      /* We don't support precision beyond that of "long long".  */
	      gcc_assert (precision < 2);
	      precision++;
	      continue;
	    }
	  break;
	}

      gcc_assert (!wide || precision == 0);

      if (quote)
	pp_string (pp, open_quote);

      switch (*p)
	{
	case 'c':
	  pp_character (pp, va_arg (*text->args_ptr, int));
	  break;

	case 'd':
	case 'i':
	  if (wide)
	    pp_wide_integer (pp, va_arg (*text->args_ptr, HOST_WIDE_INT));
	  else
	    pp_integer_with_precision
	      (pp, *text->args_ptr, precision, int, "d");
	  break;

	case 'o':
	  if (wide)
	    pp_scalar (pp, "%" HOST_WIDE_INT_PRINT "o",
		       va_arg (*text->args_ptr, unsigned HOST_WIDE_INT));
	  else
	    pp_integer_with_precision
	      (pp, *text->args_ptr, precision, unsigned, "o");
	  break;

	case 's':
	  pp_string (pp, va_arg (*text->args_ptr, const char *));
	  break;

	case 'p':
	  pp_pointer (pp, va_arg (*text->args_ptr, void *));
	  break;

	case 'u':
	  if (wide)
	    pp_scalar (pp, HOST_WIDE_INT_PRINT_UNSIGNED,
		       va_arg (*text->args_ptr, unsigned HOST_WIDE_INT));
	  else
	    pp_integer_with_precision
	      (pp, *text->args_ptr, precision, unsigned, "u");
	  break;

	case 'x':
	  if (wide)
	    pp_scalar (pp, HOST_WIDE_INT_PRINT_HEX,
		       va_arg (*text->args_ptr, unsigned HOST_WIDE_INT));
	  else
	    pp_integer_with_precision
	      (pp, *text->args_ptr, precision, unsigned, "x");
	  break;

	case 'H':
	  {
	    location_t *locus = va_arg (*text->args_ptr, location_t *);
	    gcc_assert (text->locus != NULL);
	    *text->locus = *locus;
	  }
	  break;

	case 'J':
	  {
	    tree t = va_arg (*text->args_ptr, tree);
	    gcc_assert (text->locus != NULL);
	    *text->locus = DECL_SOURCE_LOCATION (t);
	  }
	  break;

	case '.':
	  {
	    int n;
	    const char *s;

	    /* We handle '%.Ns' and '%.*s' or '%M$.*N$s'
	       (where M == N + 1).  The format string should be verified
	       already from the first phase.  */
	    p++;
	    if (ISDIGIT (*p))
	      {
		char *end;
		n = strtoul (p, &end, 10);
		p = end;
		gcc_assert (*p == 's');
	      }
	    else
	      {
		gcc_assert (*p == '*');
		p++;
		gcc_assert (*p == 's');
		n = va_arg (*text->args_ptr, int);

		/* This consumes a second entry in the formatters array.  */
		gcc_assert (formatters[argno] == formatters[argno+1]);
		argno++;
	      }

	    s = va_arg (*text->args_ptr, const char *);
	    pp_append_text (pp, s, s + n);
	  }
	  break;

	default:
	  {
	    bool ok;

	    gcc_assert (pp_format_decoder (pp));
	    ok = pp_format_decoder (pp) (pp, text, p,
					 precision, wide, plus, hash);
	    gcc_assert (ok);
	  }
	}

      if (quote)
	pp_string (pp, close_quote);

      obstack_1grow (&buffer->chunk_obstack, '\0');
      *formatters[argno] = XOBFINISH (&buffer->chunk_obstack, const char *);
    }

#ifdef ENABLE_CHECKING
  for (; argno < PP_NL_ARGMAX; argno++)
    gcc_assert (!formatters[argno]);
#endif

  /* Revert to normal obstack and wrapping mode.  */
  buffer->obstack = &buffer->formatted_obstack;
  buffer->line_length = 0;
  pp_wrapping_mode (pp) = old_wrapping_mode;
  pp_clear_state (pp);
}

/* Format of a message pointed to by TEXT.  */
void
pp_base_output_formatted_text (pretty_printer *pp)
{
  unsigned int chunk;
  output_buffer *buffer = pp_buffer (pp);
  struct chunk_info *chunk_array = buffer->cur_chunk_array;
  const char **args = chunk_array->args;

  gcc_assert (buffer->obstack == &buffer->formatted_obstack);
  gcc_assert (buffer->line_length == 0);

  /* This is a third phase, first 2 phases done in pp_base_format_args.
     Now we actually print it.  */
  for (chunk = 0; args[chunk]; chunk++)
    pp_string (pp, args[chunk]);

  /* Deallocate the chunk structure and everything after it (i.e. the
     associated series of formatted strings).  */
  buffer->cur_chunk_array = chunk_array->prev;
  obstack_free (&buffer->chunk_obstack, chunk_array);
}

/* Helper subroutine of output_verbatim and verbatim. Do the appropriate
   settings needed by BUFFER for a verbatim formatting.  */
void
pp_base_format_verbatim (pretty_printer *pp, text_info *text)
{
  /* Set verbatim mode.  */
  pp_wrapping_mode_t oldmode = pp_set_verbatim_wrapping (pp);

  /* Do the actual formatting.  */
  pp_format (pp, text);
  pp_output_formatted_text (pp);

  /* Restore previous settings.  */
  pp_wrapping_mode (pp) = oldmode;
}

/* Flush the content of BUFFER onto the attached stream.  */
void
pp_base_flush (pretty_printer *pp)
{
  pp_write_text_to_stream (pp);
  pp_clear_state (pp);
  fputc ('\n', pp->buffer->stream);
  fflush (pp->buffer->stream);
  pp_needs_newline (pp) = false;
}

/* Sets the number of maximum characters per line PRETTY-PRINTER can
   output in line-wrapping mode.  A LENGTH value 0 suppresses
   line-wrapping.  */
void
pp_base_set_line_maximum_length (pretty_printer *pp, int length)
{
  pp_line_cutoff (pp) = length;
  pp_set_real_maximum_length (pp);
}

/* Clear PRETTY-PRINTER output area text info.  */
void
pp_base_clear_output_area (pretty_printer *pp)
{
  obstack_free (pp->buffer->obstack, obstack_base (pp->buffer->obstack));
  pp->buffer->line_length = 0;
}

/* Set PREFIX for PRETTY-PRINTER.  */
void
pp_base_set_prefix (pretty_printer *pp, const char *prefix)
{
  pp->prefix = prefix;
  pp_set_real_maximum_length (pp);
  pp->emitted_prefix = false;
  pp_indentation (pp) = 0;
}

/* Free PRETTY-PRINTER's prefix, a previously malloc()'d string.  */
void
pp_base_destroy_prefix (pretty_printer *pp)
{
  if (pp->prefix != NULL)
    {
      free ((char *) pp->prefix);
      pp->prefix = NULL;
    }
}

/* Write out PRETTY-PRINTER's prefix.  */
void
pp_base_emit_prefix (pretty_printer *pp)
{
  if (pp->prefix != NULL)
    {
      switch (pp_prefixing_rule (pp))
	{
	default:
	case DIAGNOSTICS_SHOW_PREFIX_NEVER:
	  break;

	case DIAGNOSTICS_SHOW_PREFIX_ONCE:
	  if (pp->emitted_prefix)
	    {
	      pp_base_indent (pp);
	      break;
	    }
	  pp_indentation (pp) += 3;
	  /* Fall through.  */

	case DIAGNOSTICS_SHOW_PREFIX_EVERY_LINE:
	  {
	    int prefix_length = strlen (pp->prefix);
	    pp_append_r (pp, pp->prefix, prefix_length);
	    pp->emitted_prefix = true;
	  }
	  break;
	}
    }
}

/* Construct a PRETTY-PRINTER with PREFIX and of MAXIMUM_LENGTH
   characters per line.  */
void
pp_construct (pretty_printer *pp, const char *prefix, int maximum_length)
{
  memset (pp, 0, sizeof (pretty_printer));
  pp->buffer = XCNEW (output_buffer);
  obstack_init (&pp->buffer->chunk_obstack);
  obstack_init (&pp->buffer->formatted_obstack);
  pp->buffer->obstack = &pp->buffer->formatted_obstack;
  pp->buffer->stream = stderr;
  pp_line_cutoff (pp) = maximum_length;
  pp_prefixing_rule (pp) = DIAGNOSTICS_SHOW_PREFIX_ONCE;
  pp_set_prefix (pp, prefix);
}

/* Append a string delimited by START and END to the output area of
   PRETTY-PRINTER.  No line wrapping is done.  However, if beginning a
   new line then emit PRETTY-PRINTER's prefix and skip any leading
   whitespace if appropriate.  The caller must ensure that it is
   safe to do so.  */
void
pp_base_append_text (pretty_printer *pp, const char *start, const char *end)
{
  /* Emit prefix and skip whitespace if we're starting a new line.  */
  if (pp->buffer->line_length == 0)
    {
      pp_emit_prefix (pp);
      if (pp_is_wrapping_line (pp))
	while (start != end && *start == ' ')
	  ++start;
    }
  pp_append_r (pp, start, end - start);
}

/* Finishes constructing a NULL-terminated character string representing
   the PRETTY-PRINTED text.  */
const char *
pp_base_formatted_text (pretty_printer *pp)
{
  obstack_1grow (pp->buffer->obstack, '\0');
  return pp_formatted_text_data (pp);
}

/*  Return a pointer to the last character emitted in PRETTY-PRINTER's
    output area.  A NULL pointer means no character available.  */
const char *
pp_base_last_position_in_text (const pretty_printer *pp)
{
  const char *p = NULL;
  struct obstack *text = pp->buffer->obstack;

  if (obstack_base (text) != obstack_next_free (text))
    p = ((const char *) obstack_next_free (text)) - 1;
  return p;
}

/* Return the amount of characters PRETTY-PRINTER can accept to
   make a full line.  Meaningful only in line-wrapping mode.  */
int
pp_base_remaining_character_count_for_line (pretty_printer *pp)
{
  return pp->maximum_length - pp->buffer->line_length;
}


/* Format a message into BUFFER a la printf.  */
void
pp_printf (pretty_printer *pp, const char *msg, ...)
{
  text_info text;
  va_list ap;

  va_start (ap, msg);
  text.err_no = errno;
  text.args_ptr = &ap;
  text.format_spec = msg;
  text.locus = NULL;
  pp_format (pp, &text);
  pp_output_formatted_text (pp);
  va_end (ap);
}


/* Output MESSAGE verbatim into BUFFER.  */
void
pp_verbatim (pretty_printer *pp, const char *msg, ...)
{
  text_info text;
  va_list ap;

  va_start (ap, msg);
  text.err_no = errno;
  text.args_ptr = &ap;
  text.format_spec = msg;
  text.locus = NULL;
  pp_format_verbatim (pp, &text);
  va_end (ap);
}



/* Have PRETTY-PRINTER start a new line.  */
void
pp_base_newline (pretty_printer *pp)
{
  obstack_1grow (pp->buffer->obstack, '\n');
  pp->buffer->line_length = 0;
}

/* Have PRETTY-PRINTER add a CHARACTER.  */
void
pp_base_character (pretty_printer *pp, int c)
{
  if (pp_is_wrapping_line (pp)
      && pp_remaining_character_count_for_line (pp) <= 0)
    {
      pp_newline (pp);
      if (ISSPACE (c))
        return;
    }
  obstack_1grow (pp->buffer->obstack, c);
  ++pp->buffer->line_length;
}

/* Append a STRING to the output area of PRETTY-PRINTER; the STRING may
   be line-wrapped if in appropriate mode.  */
void
pp_base_string (pretty_printer *pp, const char *str)
{
  pp_maybe_wrap_text (pp, str, str + (str ? strlen (str) : 0));
}

/* Maybe print out a whitespace if needed.  */

void
pp_base_maybe_space (pretty_printer *pp)
{
  if (pp_base (pp)->padding != pp_none)
    {
      pp_space (pp);
      pp_base (pp)->padding = pp_none;
    }
}

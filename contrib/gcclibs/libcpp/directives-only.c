/* CPP Library - directive only preprocessing for distributed compilation.
   Copyright (C) 2007
   Free Software Foundation, Inc.
   Contributed by Ollie Wild <aaw@google.com>.

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

/* DO (Directive only) flags. */
#define DO_BOL		 (1 << 0) /* At the beginning of a logical line. */
#define DO_STRING	 (1 << 1) /* In a string constant. */
#define DO_CHAR		 (1 << 2) /* In a character constant. */
#define DO_BLOCK_COMMENT (1 << 3) /* In a block comment. */
#define DO_LINE_COMMENT	 (1 << 4) /* In a single line "//-style" comment. */

#define DO_LINE_SPECIAL (DO_STRING | DO_CHAR | DO_LINE_COMMENT)
#define DO_SPECIAL	(DO_LINE_SPECIAL | DO_BLOCK_COMMENT)

/* Writes out the preprocessed file, handling spacing and paste
   avoidance issues.  */
void
_cpp_preprocess_dir_only (cpp_reader *pfile,
			  const struct _cpp_dir_only_callbacks *cb)
{
  struct cpp_buffer *buffer;
  const unsigned char *cur, *base, *next_line, *rlimit;
  cppchar_t c, last_c;
  unsigned flags;
  int lines, col;
  source_location loc;

 restart:
  /* Buffer initialization ala _cpp_clean_line(). */
  buffer = pfile->buffer;
  buffer->cur_note = buffer->notes_used = 0;
  buffer->cur = buffer->line_base = buffer->next_line;
  buffer->need_line = false;

  /* This isn't really needed.  It prevents a compiler warning, though. */
  loc = pfile->line_table->highest_line;

  /* Scan initialization. */
  next_line = cur = base = buffer->cur;
  rlimit = buffer->rlimit;
  flags = DO_BOL;
  lines = 0;
  col = 1;

  for (last_c = '\n', c = *cur; cur < rlimit; last_c = c, c = *++cur, ++col)
    {
      /* Skip over escaped newlines. */
      if (__builtin_expect (c == '\\', false))
	{
	  const unsigned char *tmp = cur + 1;

	  while (is_nvspace (*tmp) && tmp < rlimit)
	    tmp++;
	  if (*tmp == '\r')
	    tmp++;
	  if (*tmp == '\n' && tmp < rlimit)
	    {
	      CPP_INCREMENT_LINE (pfile, 0);
	      lines++;
	      col = 0;
	      cur = tmp;
	      c = last_c;
	      continue;
	    }
	}

      if (__builtin_expect (last_c == '#', false) && !(flags & DO_SPECIAL))
	{
	  if (c != '#' && (flags & DO_BOL))
	  {
	    struct line_maps *line_table;

	    if (!pfile->state.skipping && next_line != base)
	      cb->print_lines (lines, base, next_line - base);

	    /* Prep things for directive handling. */
	    buffer->next_line = cur;
	    buffer->need_line = true;
	    _cpp_get_fresh_line (pfile);

	    /* Ensure proper column numbering for generated error messages. */
	    buffer->line_base -= col - 1;

	    _cpp_handle_directive (pfile, 0 /* ignore indented */);

	    /* Sanitize the line settings.  Duplicate #include's can mess
	       things up. */
	    line_table = pfile->line_table;
	    line_table->highest_location = line_table->highest_line;

	    /* The if block prevents us from outputing line information when
	       the file ends with a directive and no newline.  Note that we
	       must use pfile->buffer, not buffer. */
	    if (pfile->buffer->cur != pfile->buffer->rlimit)
	      cb->maybe_print_line (pfile->line_table->highest_line);

	    goto restart;
	  }

	  flags &= ~DO_BOL;
	  pfile->mi_valid = false;
	}
      else if (__builtin_expect (last_c == '/', false) \
	       && !(flags & DO_SPECIAL) && c != '*' && c != '/')
	{
	  /* If a previous slash is not starting a block comment, clear the
	     DO_BOL flag.  */
	  flags &= ~DO_BOL;
	  pfile->mi_valid = false;
	}

      switch (c)
	{
	case '/':
	  if ((flags & DO_BLOCK_COMMENT) && last_c == '*')
	    {
	      flags &= ~DO_BLOCK_COMMENT;
	      c = 0;
	    }
	  else if (!(flags & DO_SPECIAL) && last_c == '/')
	    flags |= DO_LINE_COMMENT;
	  else if (!(flags & DO_SPECIAL))
	    /* Mark the position for possible error reporting. */
	    LINEMAP_POSITION_FOR_COLUMN (loc, pfile->line_table, col);

	  break;

	case '*':
	  if (!(flags & DO_SPECIAL))
	    {
	      if (last_c == '/')
		flags |= DO_BLOCK_COMMENT;
	      else
		{
		  flags &= ~DO_BOL;
		  pfile->mi_valid = false;
		}
	    }

	  break;

	case '\'':
	case '"':
	  {
	    unsigned state = (c == '"') ? DO_STRING : DO_CHAR;

	    if (!(flags & DO_SPECIAL))
	      {
		flags |= state;
		flags &= ~DO_BOL;
		pfile->mi_valid = false;
	      }
	    else if ((flags & state) && last_c != '\\')
	      flags &= ~state;

	    break;
	  }

	case '\\':
	  {
	    if ((flags & (DO_STRING | DO_CHAR)) && last_c == '\\')
	      c = 0;

	    if (!(flags & DO_SPECIAL))
	      {
		flags &= ~DO_BOL;
		pfile->mi_valid = false;
	      }

	    break;
	  }

	case '\n':
	  CPP_INCREMENT_LINE (pfile, 0);
	  lines++;
	  col = 0;
	  flags &= ~DO_LINE_SPECIAL;
	  if (!(flags & DO_SPECIAL))
	    flags |= DO_BOL;
	  break;

	case '#':
	  next_line = cur;
	  /* Don't update DO_BOL yet. */
	  break;

	case ' ': case '\t': case '\f': case '\v': case '\0':
	  break;

	default:
	  if (!(flags & DO_SPECIAL))
	    {
	      flags &= ~DO_BOL;
	      pfile->mi_valid = false;
	    }
	  break;
	}
    }

  if (flags & DO_BLOCK_COMMENT)
    cpp_error_with_line (pfile, CPP_DL_ERROR, loc, 0, "unterminated comment");

  if (!pfile->state.skipping && cur != base)
    cb->print_lines (lines, base, cur - base);

  _cpp_pop_buffer (pfile);
  if (pfile->buffer)
    goto restart;
}

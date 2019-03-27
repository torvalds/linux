/* Line completion stuff for GDB, the GNU debugger.
   Copyright 2000, 2001 Free Software Foundation, Inc.

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

#include "defs.h"
#include "symtab.h"
#include "gdbtypes.h"
#include "expression.h"
#include "filenames.h"		/* for DOSish file names */
#include "language.h"

#include "cli/cli-decode.h"

/* FIXME: This is needed because of lookup_cmd_1().
   We should be calling a hook instead so we eliminate the CLI dependency. */
#include "gdbcmd.h"

/* Needed for rl_completer_word_break_characters() and for
   rl_filename_completion_function.  */
#include "readline/readline.h"

/* readline defines this.  */
#undef savestring

#include "completer.h"

/* Prototypes for local functions */
static
char *line_completion_function (const char *text, int matches, char *line_buffer,
				int point);

/* readline uses the word breaks for two things:
   (1) In figuring out where to point the TEXT parameter to the
   rl_completion_entry_function.  Since we don't use TEXT for much,
   it doesn't matter a lot what the word breaks are for this purpose, but
   it does affect how much stuff M-? lists.
   (2) If one of the matches contains a word break character, readline
   will quote it.  That's why we switch between
   current_language->la_word_break_characters() and
   gdb_completer_command_word_break_characters.  I'm not sure when
   we need this behavior (perhaps for funky characters in C++ symbols?).  */

/* Variables which are necessary for fancy command line editing.  */

/* When completing on command names, we remove '-' from the list of
   word break characters, since we use it in command names.  If the
   readline library sees one in any of the current completion strings,
   it thinks that the string needs to be quoted and automatically supplies
   a leading quote. */
static char *gdb_completer_command_word_break_characters =
" \t\n!@#$%^&*()+=|~`}{[]\"';:?/>.<,";

/* When completing on file names, we remove from the list of word
   break characters any characters that are commonly used in file
   names, such as '-', '+', '~', etc.  Otherwise, readline displays
   incorrect completion candidates.  */
#ifdef HAVE_DOS_BASED_FILE_SYSTEM
/* MS-DOS and MS-Windows use colon as part of the drive spec, and most
   programs support @foo style response files.  */
static char *gdb_completer_file_name_break_characters = " \t\n*|\"';?><@";
#else
static char *gdb_completer_file_name_break_characters = " \t\n*|\"';:?><";
#endif

/* These are used when completing on locations, which can mix file
   names and symbol names separated by a colon.  */
static char *gdb_completer_loc_break_characters = " \t\n*|\"';:?><,";

/* Characters that can be used to quote completion strings.  Note that we
   can't include '"' because the gdb C parser treats such quoted sequences
   as strings. */
static char *gdb_completer_quote_characters = "'";

/* Accessor for some completer data that may interest other files. */

char *
get_gdb_completer_quote_characters (void)
{
  return gdb_completer_quote_characters;
}

/* Line completion interface function for readline.  */

char *
readline_line_completion_function (const char *text, int matches)
{
  return line_completion_function (text, matches, rl_line_buffer, rl_point);
}

/* This can be used for functions which don't want to complete on symbols
   but don't want to complete on anything else either.  */
char **
noop_completer (char *text, char *prefix)
{
  return NULL;
}

/* Complete on filenames.  */
char **
filename_completer (char *text, char *word)
{
  int subsequent_name;
  char **return_val;
  int return_val_used;
  int return_val_alloced;

  return_val_used = 0;
  /* Small for testing.  */
  return_val_alloced = 1;
  return_val = (char **) xmalloc (return_val_alloced * sizeof (char *));

  subsequent_name = 0;
  while (1)
    {
      char *p;
      p = rl_filename_completion_function (text, subsequent_name);
      if (return_val_used >= return_val_alloced)
	{
	  return_val_alloced *= 2;
	  return_val =
	    (char **) xrealloc (return_val,
				return_val_alloced * sizeof (char *));
	}
      if (p == NULL)
	{
	  return_val[return_val_used++] = p;
	  break;
	}
      /* We need to set subsequent_name to a non-zero value before the
	 continue line below, because otherwise, if the first file seen
	 by GDB is a backup file whose name ends in a `~', we will loop
	 indefinitely.  */
      subsequent_name = 1;
      /* Like emacs, don't complete on old versions.  Especially useful
         in the "source" command.  */
      if (p[strlen (p) - 1] == '~')
	continue;

      {
	char *q;
	if (word == text)
	  /* Return exactly p.  */
	  return_val[return_val_used++] = p;
	else if (word > text)
	  {
	    /* Return some portion of p.  */
	    q = xmalloc (strlen (p) + 5);
	    strcpy (q, p + (word - text));
	    return_val[return_val_used++] = q;
	    xfree (p);
	  }
	else
	  {
	    /* Return some of TEXT plus p.  */
	    q = xmalloc (strlen (p) + (text - word) + 5);
	    strncpy (q, word, text - word);
	    q[text - word] = '\0';
	    strcat (q, p);
	    return_val[return_val_used++] = q;
	    xfree (p);
	  }
      }
    }
#if 0
  /* There is no way to do this just long enough to affect quote inserting
     without also affecting the next completion.  This should be fixed in
     readline.  FIXME.  */
  /* Insure that readline does the right thing
     with respect to inserting quotes.  */
  rl_completer_word_break_characters = "";
#endif
  return return_val;
}

/* Complete on locations, which might be of two possible forms:

       file:line
   or
       symbol+offset

   This is intended to be used in commands that set breakpoints etc.  */
char **
location_completer (char *text, char *word)
{
  int n_syms = 0, n_files = 0;
  char ** fn_list = NULL;
  char ** list = NULL;
  char *p;
  int quote_found = 0;
  int quoted = *text == '\'' || *text == '"';
  int quote_char = '\0';
  char *colon = NULL;
  char *file_to_match = NULL;
  char *symbol_start = text;
  char *orig_text = text;
  size_t text_len;

  /* Do we have an unquoted colon, as in "break foo.c::bar"?  */
  for (p = text; *p != '\0'; ++p)
    {
      if (*p == '\\' && p[1] == '\'')
	p++;
      else if (*p == '\'' || *p == '"')
	{
	  quote_found = *p;
	  quote_char = *p++;
	  while (*p != '\0' && *p != quote_found)
	    {
	      if (*p == '\\' && p[1] == quote_found)
		p++;
	      p++;
	    }

	  if (*p == quote_found)
	    quote_found = 0;
	  else
	    break;		/* hit the end of text */
	}
#if HAVE_DOS_BASED_FILE_SYSTEM
      /* If we have a DOS-style absolute file name at the beginning of
	 TEXT, and the colon after the drive letter is the only colon
	 we found, pretend the colon is not there.  */
      else if (p < text + 3 && *p == ':' && p == text + 1 + quoted)
	;
#endif
      else if (*p == ':' && !colon)
	{
	  colon = p;
	  symbol_start = p + 1;
	}
      else if (strchr (current_language->la_word_break_characters(), *p))
	symbol_start = p + 1;
    }

  if (quoted)
    text++;
  text_len = strlen (text);

  /* Where is the file name?  */
  if (colon)
    {
      char *s;

      file_to_match = (char *) xmalloc (colon - text + 1);
      strncpy (file_to_match, text, colon - text + 1);
      /* Remove trailing colons and quotes from the file name.  */
      for (s = file_to_match + (colon - text);
	   s > file_to_match;
	   s--)
	if (*s == ':' || *s == quote_char)
	  *s = '\0';
    }
  /* If the text includes a colon, they want completion only on a
     symbol name after the colon.  Otherwise, we need to complete on
     symbols as well as on files.  */
  if (colon)
    {
      list = make_file_symbol_completion_list (symbol_start, word,
					       file_to_match);
      xfree (file_to_match);
    }
  else
    {
      list = make_symbol_completion_list (symbol_start, word);
      /* If text includes characters which cannot appear in a file
	 name, they cannot be asking for completion on files.  */
      if (strcspn (text, gdb_completer_file_name_break_characters) == text_len)
	fn_list = make_source_files_completion_list (text, text);
    }

  /* How many completions do we have in both lists?  */
  if (fn_list)
    for ( ; fn_list[n_files]; n_files++)
      ;
  if (list)
    for ( ; list[n_syms]; n_syms++)
      ;

  /* Make list[] large enough to hold both lists, then catenate
     fn_list[] onto the end of list[].  */
  if (n_syms && n_files)
    {
      list = xrealloc (list, (n_syms + n_files + 1) * sizeof (char *));
      memcpy (list + n_syms, fn_list, (n_files + 1) * sizeof (char *));
      xfree (fn_list);
    }
  else if (n_files)
    {
      /* If we only have file names as possible completion, we should
	 bring them in sync with what rl_complete expects.  The
	 problem is that if the user types "break /foo/b TAB", and the
	 possible completions are "/foo/bar" and "/foo/baz"
	 rl_complete expects us to return "bar" and "baz", without the
	 leading directories, as possible completions, because `word'
	 starts at the "b".  But we ignore the value of `word' when we
	 call make_source_files_completion_list above (because that
	 would not DTRT when the completion results in both symbols
	 and file names), so make_source_files_completion_list returns
	 the full "/foo/bar" and "/foo/baz" strings.  This produces
	 wrong results when, e.g., there's only one possible
	 completion, because rl_complete will prepend "/foo/" to each
	 candidate completion.  The loop below removes that leading
	 part.  */
      for (n_files = 0; fn_list[n_files]; n_files++)
	{
	  memmove (fn_list[n_files], fn_list[n_files] + (word - text),
		   strlen (fn_list[n_files]) + 1 - (word - text));
	}
      /* Return just the file-name list as the result.  */
      list = fn_list;
    }
  else if (!n_syms)
    {
      /* No completions at all.  As the final resort, try completing
	 on the entire text as a symbol.  */
      list = make_symbol_completion_list (orig_text, word);
    }

  return list;
}

/* Complete on command names.  Used by "help".  */
char **
command_completer (char *text, char *word)
{
  return complete_on_cmdlist (cmdlist, text, word);
}


/* Here are some useful test cases for completion.  FIXME: These should
   be put in the test suite.  They should be tested with both M-? and TAB.

   "show output-" "radix"
   "show output" "-radix"
   "p" ambiguous (commands starting with p--path, print, printf, etc.)
   "p "  ambiguous (all symbols)
   "info t foo" no completions
   "info t " no completions
   "info t" ambiguous ("info target", "info terminal", etc.)
   "info ajksdlfk" no completions
   "info ajksdlfk " no completions
   "info" " "
   "info " ambiguous (all info commands)
   "p \"a" no completions (string constant)
   "p 'a" ambiguous (all symbols starting with a)
   "p b-a" ambiguous (all symbols starting with a)
   "p b-" ambiguous (all symbols)
   "file Make" "file" (word break hard to screw up here)
   "file ../gdb.stabs/we" "ird" (needs to not break word at slash)
 */

/* Generate completions all at once.  Returns a NULL-terminated array
   of strings.  Both the array and each element are allocated with
   xmalloc.  It can also return NULL if there are no completions.

   TEXT is the caller's idea of the "word" we are looking at.

   LINE_BUFFER is available to be looked at; it contains the entire text
   of the line.  POINT is the offset in that line of the cursor.  You
   should pretend that the line ends at POINT.  */

char **
complete_line (const char *text, char *line_buffer, int point)
{
  char **list = NULL;
  char *tmp_command, *p;
  /* Pointer within tmp_command which corresponds to text.  */
  char *word;
  struct cmd_list_element *c, *result_list;

  /* Choose the default set of word break characters to break completions.
     If we later find out that we are doing completions on command strings
     (as opposed to strings supplied by the individual command completer
     functions, which can be any string) then we will switch to the
     special word break set for command strings, which leaves out the
     '-' character used in some commands.  */

  rl_completer_word_break_characters =
    current_language->la_word_break_characters();

      /* Decide whether to complete on a list of gdb commands or on symbols. */
  tmp_command = (char *) alloca (point + 1);
  p = tmp_command;

  strncpy (tmp_command, line_buffer, point);
  tmp_command[point] = '\0';
  /* Since text always contains some number of characters leading up
     to point, we can find the equivalent position in tmp_command
     by subtracting that many characters from the end of tmp_command.  */
  word = tmp_command + point - strlen (text);

  if (point == 0)
    {
      /* An empty line we want to consider ambiguous; that is, it
	 could be any command.  */
      c = (struct cmd_list_element *) -1;
      result_list = 0;
    }
  else
    {
      c = lookup_cmd_1 (&p, cmdlist, &result_list, 1);
    }

  /* Move p up to the next interesting thing.  */
  while (*p == ' ' || *p == '\t')
    {
      p++;
    }

  if (!c)
    {
      /* It is an unrecognized command.  So there are no
	 possible completions.  */
      list = NULL;
    }
  else if (c == (struct cmd_list_element *) -1)
    {
      char *q;

      /* lookup_cmd_1 advances p up to the first ambiguous thing, but
	 doesn't advance over that thing itself.  Do so now.  */
      q = p;
      while (*q && (isalnum (*q) || *q == '-' || *q == '_'))
	++q;
      if (q != tmp_command + point)
	{
	  /* There is something beyond the ambiguous
	     command, so there are no possible completions.  For
	     example, "info t " or "info t foo" does not complete
	     to anything, because "info t" can be "info target" or
	     "info terminal".  */
	  list = NULL;
	}
      else
	{
	  /* We're trying to complete on the command which was ambiguous.
	     This we can deal with.  */
	  if (result_list)
	    {
	      list = complete_on_cmdlist (*result_list->prefixlist, p,
					  word);
	    }
	  else
	    {
	      list = complete_on_cmdlist (cmdlist, p, word);
	    }
	  /* Insure that readline does the right thing with respect to
	     inserting quotes.  */
	  rl_completer_word_break_characters =
	    gdb_completer_command_word_break_characters;
	}
    }
  else
    {
      /* We've recognized a full command.  */

      if (p == tmp_command + point)
	{
	  /* There is no non-whitespace in the line beyond the command.  */

	  if (p[-1] == ' ' || p[-1] == '\t')
	    {
	      /* The command is followed by whitespace; we need to complete
		 on whatever comes after command.  */
	      if (c->prefixlist)
		{
		  /* It is a prefix command; what comes after it is
		     a subcommand (e.g. "info ").  */
		  list = complete_on_cmdlist (*c->prefixlist, p, word);

		  /* Insure that readline does the right thing
		         with respect to inserting quotes.  */
		  rl_completer_word_break_characters =
		    gdb_completer_command_word_break_characters;
		}
	      else if (c->enums)
		{
		  list = complete_on_enum (c->enums, p, word);
		  rl_completer_word_break_characters =
		    gdb_completer_command_word_break_characters;
		}
	      else
		{
		  /* It is a normal command; what comes after it is
		     completed by the command's completer function.  */
		  if (c->completer == filename_completer)
		    {
		      /* Many commands which want to complete on
			 file names accept several file names, as
			 in "run foo bar >>baz".  So we don't want
			 to complete the entire text after the
			 command, just the last word.  To this
			 end, we need to find the beginning of the
			 file name by starting at `word' and going
			 backwards.  */
		      for (p = word;
			   p > tmp_command
			     && strchr (gdb_completer_file_name_break_characters, p[-1]) == NULL;
			   p--)
			;
		      rl_completer_word_break_characters =
			gdb_completer_file_name_break_characters;
		    }
		  else if (c->completer == location_completer)
		    {
		      /* Commands which complete on locations want to
			 see the entire argument.  */
		      for (p = word;
			   p > tmp_command
			     && p[-1] != ' ' && p[-1] != '\t';
			   p--)
			;
		    }
		  list = (*c->completer) (p, word);
		}
	    }
	  else
	    {
	      /* The command is not followed by whitespace; we need to
		 complete on the command itself.  e.g. "p" which is a
		 command itself but also can complete to "print", "ptype"
		 etc.  */
	      char *q;

	      /* Find the command we are completing on.  */
	      q = p;
	      while (q > tmp_command)
		{
		  if (isalnum (q[-1]) || q[-1] == '-' || q[-1] == '_')
		    --q;
		  else
		    break;
		}

	      list = complete_on_cmdlist (result_list, q, word);

		  /* Insure that readline does the right thing
		     with respect to inserting quotes.  */
	      rl_completer_word_break_characters =
		gdb_completer_command_word_break_characters;
	    }
	}
      else
	{
	  /* There is non-whitespace beyond the command.  */

	  if (c->prefixlist && !c->allow_unknown)
	    {
	      /* It is an unrecognized subcommand of a prefix command,
		 e.g. "info adsfkdj".  */
	      list = NULL;
	    }
	  else if (c->enums)
	    {
	      list = complete_on_enum (c->enums, p, word);
	    }
	  else
	    {
	      /* It is a normal command.  */
	      if (c->completer == filename_completer)
		{
		  /* See the commentary above about the specifics
		     of file-name completion.  */
		  for (p = word;
		       p > tmp_command
			 && strchr (gdb_completer_file_name_break_characters, p[-1]) == NULL;
		       p--)
		    ;
		  rl_completer_word_break_characters =
		    gdb_completer_file_name_break_characters;
		}
	      else if (c->completer == location_completer)
		{
		  for (p = word;
		       p > tmp_command
			 && p[-1] != ' ' && p[-1] != '\t';
		       p--)
		    ;
		}
	      list = (*c->completer) (p, word);
	    }
	}
    }

  return list;
}

/* Generate completions one by one for the completer.  Each time we are
   called return another potential completion to the caller.
   line_completion just completes on commands or passes the buck to the
   command's completer function, the stuff specific to symbol completion
   is in make_symbol_completion_list.

   TEXT is the caller's idea of the "word" we are looking at.

   MATCHES is the number of matches that have currently been collected from
   calling this completion function.  When zero, then we need to initialize,
   otherwise the initialization has already taken place and we can just
   return the next potential completion string.

   LINE_BUFFER is available to be looked at; it contains the entire text
   of the line.  POINT is the offset in that line of the cursor.  You
   should pretend that the line ends at POINT.

   Returns NULL if there are no more completions, else a pointer to a string
   which is a possible completion, it is the caller's responsibility to
   free the string.  */

static char *
line_completion_function (const char *text, int matches, char *line_buffer, int point)
{
  static char **list = (char **) NULL;	/* Cache of completions */
  static int index;		/* Next cached completion */
  char *output = NULL;

  if (matches == 0)
    {
      /* The caller is beginning to accumulate a new set of completions, so
         we need to find all of them now, and cache them for returning one at
         a time on future calls. */

      if (list)
	{
	  /* Free the storage used by LIST, but not by the strings inside.
	     This is because rl_complete_internal () frees the strings. */
	  xfree (list);
	}
      index = 0;
      list = complete_line (text, line_buffer, point);
    }

  /* If we found a list of potential completions during initialization then
     dole them out one at a time.  The vector of completions is NULL
     terminated, so after returning the last one, return NULL (and continue
     to do so) each time we are called after that, until a new list is
     available. */

  if (list)
    {
      output = list[index];
      if (output)
	{
	  index++;
	}
    }

#if 0
  /* Can't do this because readline hasn't yet checked the word breaks
     for figuring out whether to insert a quote.  */
  if (output == NULL)
    /* Make sure the word break characters are set back to normal for the
       next time that readline tries to complete something.  */
    rl_completer_word_break_characters =
      current_language->la_word_break_characters();
#endif

  return (output);
}

/* Skip over the possibly quoted word STR (as defined by the quote
   characters QUOTECHARS and the the word break characters
   BREAKCHARS).  Returns pointer to the location after the "word".  If
   either QUOTECHARS or BREAKCHARS is NULL, use the same values used
   by the completer.  */

char *
skip_quoted_chars (char *str, char *quotechars, char *breakchars)
{
  char quote_char = '\0';
  char *scan;

  if (quotechars == NULL)
    quotechars = gdb_completer_quote_characters;

  if (breakchars == NULL)
    breakchars = current_language->la_word_break_characters();

  for (scan = str; *scan != '\0'; scan++)
    {
      if (quote_char != '\0')
	{
	  /* Ignore everything until the matching close quote char */
	  if (*scan == quote_char)
	    {
	      /* Found matching close quote. */
	      scan++;
	      break;
	    }
	}
      else if (strchr (quotechars, *scan))
	{
	  /* Found start of a quoted string. */
	  quote_char = *scan;
	}
      else if (strchr (breakchars, *scan))
	{
	  break;
	}
    }

  return (scan);
}

/* Skip over the possibly quoted word STR (as defined by the quote
   characters and word break characters used by the completer).
   Returns pointer to the location after the "word". */

char *
skip_quoted (char *str)
{
  return skip_quoted_chars (str, NULL, NULL);
}

/* Shell command argument quoting.
   Copyright (C) 1994, 1995, 1997 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; see the file COPYING.
   If not, write to the Free Software Foundation,
   59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

/* Written by Paul Eggert <eggert@twinsun.com> */

#if HAVE_CONFIG_H
# include <config.h>
#endif

#include <sys/types.h>
#include <quotesys.h>

/* Place into QUOTED a quoted version of ARG suitable for `system'.
   Return the length of the resulting string (which is not null-terminated).
   If QUOTED is null, return the length without any side effects.  */

size_t
quote_system_arg (quoted, arg)
     char *quoted;
     char const *arg;
{
  char const *a;
  size_t len = 0;

  /* Scan ARG, copying it to QUOTED if QUOTED is not null,
     looking for shell metacharacters.  */

  for (a = arg; ; a++)
    {
      char c = *a;
      switch (c)
	{
	case 0:
	  /* ARG has no shell metacharacters.  */
	  return len;

	case '=':
	  if (*arg == '-')
	    break;
	  /* Fall through.  */
	case '\t': case '\n': case ' ':
	case '!': case '"': case '#': case '$': case '%': case '&': case '\'':
	case '(': case ')': case '*': case ';':
	case '<': case '>': case '?': case '[': case '\\':
	case '^': case '`': case '|': case '~':
	  {
	    /* ARG has a shell metacharacter.
	       Start over, quoting it this time.  */

	    len = 0;
	    c = *arg++;

	    /* If ARG is an option, quote just its argument.
	       This is not necessary, but it looks nicer.  */
	    if (c == '-'  &&  arg < a)
	      {
		c = *arg++;

		if (quoted)
		  {
		    quoted[len] = '-';
		    quoted[len + 1] = c;
		  }
		len += 2;

		if (c == '-')
		  while (arg < a)
		    {
		      c = *arg++;
		      if (quoted)
			quoted[len] = c;
		      len++;
		      if (c == '=')
			break;
		    }
		c = *arg++;
	      }

	    if (quoted)
	      quoted[len] = '\'';
	    len++;

	    for (;  c;  c = *arg++)
	      {
		if (c == '\'')
		  {
		    if (quoted)
		      {
			quoted[len] = '\'';
			quoted[len + 1] = '\\';
			quoted[len + 2] = '\'';
		      }
		    len += 3;
		  }
		if (quoted)
		  quoted[len] = c;
		len++;
	      }

	    if (quoted)
	      quoted[len] = '\'';
	    return len + 1;
	  }
	}

      if (quoted)
	quoted[len] = c;
      len++;
    }
}

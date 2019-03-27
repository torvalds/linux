/* rclex.c -- lexer for Windows rc files parser  */

/* Copyright 1997, 1998, 1999, 2001, 2002, 2003, 2005, 2006, 2007
   Free Software Foundation, Inc.

   Written by Kai Tietz, Onevision.

   This file is part of GNU Binutils.

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
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

/* This is a lexer used by the Windows rc file parser.  It basically
   just recognized a bunch of keywords.  */

#include "sysdep.h"
#include "bfd.h"
#include "bucomm.h"
#include "libiberty.h"
#include "safe-ctype.h"
#include "windres.h"
#include "rcparse.h"

#include <assert.h>

/* Whether we are in rcdata mode, in which we returns the lengths of
   strings.  */

static int rcdata_mode;

/* Whether we are supressing lines from cpp (including windows.h or
   headers from your C sources may bring in externs and typedefs).
   When active, we return IGNORED_TOKEN, which lets us ignore these
   outside of resource constructs.  Thus, it isn't required to protect
   all the non-preprocessor lines in your header files with #ifdef
   RC_INVOKED.  It also means your RC file can't include other RC
   files if they're named "*.h".  Sorry.  Name them *.rch or whatever.  */

static int suppress_cpp_data;

#define IGNORE_CPP(x) (suppress_cpp_data ? IGNORED_TOKEN : (x))

/* The first filename we detect in the cpp output.  We use this to
   tell included files from the original file.  */

static char *initial_fn;

/* List of allocated strings.  */

struct alloc_string
{
  struct alloc_string *next;
  char *s;
};

static struct alloc_string *strings;

struct rclex_keywords
{
  const char *name;
  int tok;
};

#define K(KEY)  { #KEY, KEY }
#define KRT(KEY)  { #KEY, RT_##KEY }

static const struct rclex_keywords keywds[] =
{
  K(ACCELERATORS), K(ALT), K(ANICURSOR), K(ANIICON), K(ASCII),
  K(AUTO3STATE), K(AUTOCHECKBOX), K(AUTORADIOBUTTON),
  K(BEDIT), { "BEGIN", BEG }, K(BITMAP), K(BLOCK), K(BUTTON),
  K(CAPTION), K(CHARACTERISTICS), K(CHECKBOX), K(CHECKED),
  K(CLASS), K(COMBOBOX), K(CONTROL), K(CTEXT), K(CURSOR),
  K(DEFPUSHBUTTON), K(DIALOG), K(DIALOGEX), K(DISCARDABLE),
  K(DLGINCLUDE), K(DLGINIT),
  K(EDITTEXT), K(END), K(EXSTYLE),
  K(FILEFLAGS), K(FILEFLAGSMASK), K(FILEOS), K(FILESUBTYPE),
  K(FILETYPE), K(FILEVERSION), K(FIXED), K(FONT), K(FONTDIR),
  K(GRAYED), KRT(GROUP_CURSOR), KRT(GROUP_ICON), K(GROUPBOX),
  K(HEDIT), K(HELP), K(HTML),
  K(ICON), K(IEDIT), K(IMPURE), K(INACTIVE),
  K(LANGUAGE), K(LISTBOX), K(LOADONCALL), K(LTEXT),
  K(MANIFEST), K(MENU), K(MENUBARBREAK), K(MENUBREAK),
  K(MENUEX), K(MENUITEM), K(MESSAGETABLE), K(MOVEABLE),
  K(NOINVERT), K(NOT),
  K(PLUGPLAY), K(POPUP), K(PRELOAD), K(PRODUCTVERSION),
  K(PURE), K(PUSHBOX), K(PUSHBUTTON),
  K(RADIOBUTTON), K(RCDATA), K(RTEXT),
  K(SCROLLBAR), K(SEPARATOR), K(SHIFT), K(STATE3),
  K(STRINGTABLE), K(STYLE),
  K(TOOLBAR),
  K(USERBUTTON),
  K(VALUE), { "VERSION", VERSIONK }, K(VERSIONINFO),
  K(VIRTKEY), K(VXD),
  { NULL, 0 },
};

/* External input stream from resrc */
extern FILE *cpp_pipe;

/* Lexical scanner helpers.  */
static int rclex_lastch = -1;
static size_t rclex_tok_max = 0;
static size_t rclex_tok_pos = 0;
static char *rclex_tok = NULL;

static int
rclex_translatekeyword (const char *key)
{
  if (key && ISUPPER (key[0]))
    {
      const struct rclex_keywords *kw = &keywds[0];

      do
        {
	  if (! strcmp (kw->name, key))
	    return kw->tok;
	  ++kw;
        }
      while (kw->name != NULL);
    }
  return STRING;
}

/* Handle a C preprocessor line.  */

static void
cpp_line (void)
{
  const char *s = rclex_tok;
  int line;
  char *send, *fn;
  size_t len, mlen;

  ++s;
  while (ISSPACE (*s))
    ++s;
  
  /* Check for #pragma code_page ( DEFAULT | <nr>).  */
  len = strlen (s);
  mlen = strlen ("pragma");
  if (len > mlen && memcmp (s, "pragma", mlen) == 0 && ISSPACE (s[mlen]))
    {
      const char *end;

      s += mlen + 1;
      while (ISSPACE (*s))
	++s;
      len = strlen (s);
      mlen = strlen ("code_page");
      if (len <= mlen || memcmp (s, "code_page", mlen) != 0)
	/* FIXME: We ought to issue a warning message about an unrecognised pragma.  */
	return;
      s += mlen;
      while (ISSPACE (*s))
	++s;
      if (*s != '(')
	/* FIXME: We ought to issue an error message about a malformed pragma.  */
	return;
      ++s;
      while (ISSPACE (*s))
	++s;
      if (*s == 0 || (end = strchr (s, ')')) == NULL)
	/* FIXME: We ought to issue an error message about a malformed pragma.  */
	return;
      len = (size_t) (end - s);
      fn = xmalloc (len + 1);
      if (len)
      	memcpy (fn, s, len);
      fn[len] = 0;
      while (len > 0 && (fn[len - 1] > 0 && fn[len - 1] <= 0x20))
	fn[--len] = 0;
      if (! len || (len == strlen ("DEFAULT") && strcasecmp (fn, "DEFAULT") == 0))
	wind_current_codepage = wind_default_codepage;
      else if (len > 0)
	{
	  rc_uint_type ncp;

	  if (fn[0] == '0' && (fn[1] == 'x' || fn[1] == 'X'))
	      ncp = (rc_uint_type) strtol (fn + 2, NULL, 16);
	  else
	      ncp = (rc_uint_type) strtol (fn, NULL, 10);
	  if (ncp == CP_UTF16 || ! unicode_is_valid_codepage (ncp))
	    fatal (_("invalid value specified for pragma code_page.\n"));
	  wind_current_codepage = ncp;
	}
      free (fn);
      return;
    }

  line = strtol (s, &send, 0);
  if (*send != '\0' && ! ISSPACE (*send))
    return;

  /* Subtract 1 because we are about to count the newline.  */
  rc_lineno = line - 1;

  s = send;
  while (ISSPACE (*s))
    ++s;

  if (*s != '"')
    return;

  ++s;
  send = strchr (s, '"');
  if (send == NULL)
    return;

  fn = xmalloc (send - s + 1);
  strncpy (fn, s, send - s);
  fn[send - s] = '\0';

  free (rc_filename);
  rc_filename = fn;

  if (! initial_fn)
    {
      initial_fn = xmalloc (strlen (fn) + 1);
      strcpy (initial_fn, fn);
    }

  /* Allow the initial file, regardless of name.  Suppress all other
     files if they end in ".h" (this allows included "*.rc").  */
  if (strcmp (initial_fn, fn) == 0
      || strcmp (fn + strlen (fn) - 2, ".h") != 0)
    suppress_cpp_data = 0;
  else
    suppress_cpp_data = 1;
}

/* Allocate a string of a given length.  */

static char *
get_string (int len)
{
  struct alloc_string *as;

  as = xmalloc (sizeof *as);
  as->s = xmalloc (len);

  as->next = strings;
  strings = as;

  return as->s;
}

/* Handle a quoted string.  The quotes are stripped.  A pair of quotes
   in a string are turned into a single quote.  Adjacent strings are
   merged separated by whitespace are merged, as in C.  */

static char *
handle_quotes (rc_uint_type *len)
{
  const char *input = rclex_tok;
  char *ret, *s;
  const char *t;
  int ch;
  int num_xdigits;

  ret = get_string (strlen (input) + 1);

  s = ret;
  t = input;
  if (*t == '"')
    ++t;
  while (*t != '\0')
    {
      if (*t == '\\')
	{
	  ++t;
	  switch (*t)
	    {
	    case '\0':
	      rcparse_warning ("backslash at end of string");
	      break;

	    case '\"':
	      rcparse_warning ("use \"\" to put \" in a string");
	      *s++ = '"';
	      ++t;
	      break;

	    case 'a':
	      *s++ = ESCAPE_B; /* Strange, but true...  */
	      ++t;
	      break;

	    case 'b':
	      *s++ = ESCAPE_B;
	      ++t;
	      break;

	    case 'f':
	      *s++ = ESCAPE_F;
	      ++t;
	      break;

	    case 'n':
	      *s++ = ESCAPE_N;
	      ++t;
	      break;

	    case 'r':
	      *s++ = ESCAPE_R;
	      ++t;
	      break;

	    case 't':
	      *s++ = ESCAPE_T;
	      ++t;
	      break;

	    case 'v':
	      *s++ = ESCAPE_V;
	      ++t;
	      break;

	    case '\\':
	      *s++ = *t++;
	      break;

	    case '0': case '1': case '2': case '3':
	    case '4': case '5': case '6': case '7':
	      ch = *t - '0';
	      ++t;
	      if (*t >= '0' && *t <= '7')
		{
		  ch = (ch << 3) | (*t - '0');
		  ++t;
		  if (*t >= '0' && *t <= '7')
		    {
		      ch = (ch << 3) | (*t - '0');
		      ++t;
		    }
		}
	      *s++ = ch;
	      break;

	    case 'x': case 'X':
	      ++t;
	      ch = 0;
	      /* We only handle single byte chars here.  Make sure
		 we finish an escape sequence like "/xB0ABC" after
		 the first two digits.  */
              num_xdigits = 2;
 	      while (num_xdigits--)
		{
		  if (*t >= '0' && *t <= '9')
		    ch = (ch << 4) | (*t - '0');
		  else if (*t >= 'a' && *t <= 'f')
		    ch = (ch << 4) | (*t - 'a' + 10);
		  else if (*t >= 'A' && *t <= 'F')
		    ch = (ch << 4) | (*t - 'A' + 10);
		  else
		    break;
		  ++t;
		}
	      *s++ = ch;
	      break;

	    default:
	      rcparse_warning ("unrecognized escape sequence");
	      *s++ = '\\';
	      *s++ = *t++;
	      break;
	    }
	}
      else if (*t != '"')
	*s++ = *t++;
      else if (t[1] == '\0')
	break;
      else if (t[1] == '"')
	{
	  *s++ = '"';
	  t += 2;
	}
      else
	{
	  rcparse_warning ("unexpected character after '\"'");
	  ++t;
	  assert (ISSPACE (*t));
	  while (ISSPACE (*t))
	    {
	      if ((*t) == '\n')
		++rc_lineno;
	      ++t;
	    }
	  if (*t == '\0')
	    break;
	  assert (*t == '"');
	  ++t;
	}
    }

  *s = '\0';

  *len = s - ret;

  return ret;
}

/* Allocate a unicode string of a given length.  */

static unichar *
get_unistring (int len)
{
  return (unichar *) get_string (len * sizeof (unichar));
}

/* Handle a quoted unicode string.  The quotes are stripped.  A pair of quotes
   in a string are turned into a single quote.  Adjacent strings are
   merged separated by whitespace are merged, as in C.  */

static unichar *
handle_uniquotes (rc_uint_type *len)
{
  const char *input = rclex_tok;
  unichar *ret, *s;
  const char *t;
  int ch;
  int num_xdigits;

  ret = get_unistring (strlen (input) + 1);

  s = ret;
  t = input;
  if ((*t == 'L' || *t == 'l') && t[1] == '"')
    t += 2;
  else if (*t == '"')
    ++t;
  while (*t != '\0')
    {
      if (*t == '\\')
	{
	  ++t;
	  switch (*t)
	    {
	    case '\0':
	      rcparse_warning ("backslash at end of string");
	      break;

	    case '\"':
	      rcparse_warning ("use \"\" to put \" in a string");
	      break;

	    case 'a':
	      *s++ = ESCAPE_B; /* Strange, but true...  */
	      ++t;
	      break;

	    case 'b':
	      *s++ = ESCAPE_B;
	      ++t;
	      break;

	    case 'f':
	      *s++ = ESCAPE_F;
	      ++t;
	      break;

	    case 'n':
	      *s++ = ESCAPE_N;
	      ++t;
	      break;

	    case 'r':
	      *s++ = ESCAPE_R;
	      ++t;
	      break;

	    case 't':
	      *s++ = ESCAPE_T;
	      ++t;
	      break;

	    case 'v':
	      *s++ = ESCAPE_V;
	      ++t;
	      break;

	    case '\\':
	      *s++ = (unichar) *t++;
	      break;

	    case '0': case '1': case '2': case '3':
	    case '4': case '5': case '6': case '7':
	      ch = *t - '0';
	      ++t;
	      if (*t >= '0' && *t <= '7')
		{
		  ch = (ch << 3) | (*t - '0');
		  ++t;
		  if (*t >= '0' && *t <= '7')
		    {
		      ch = (ch << 3) | (*t - '0');
		      ++t;
		    }
		}
	      *s++ = (unichar) ch;
	      break;

	    case 'x': case 'X':
	      ++t;
	      ch = 0;
	      /* We only handle two byte chars here.  Make sure
		 we finish an escape sequence like "/xB0ABC" after
		 the first two digits.  */
              num_xdigits = 4;
 	      while (num_xdigits--)
		{
		  if (*t >= '0' && *t <= '9')
		    ch = (ch << 4) | (*t - '0');
		  else if (*t >= 'a' && *t <= 'f')
		    ch = (ch << 4) | (*t - 'a' + 10);
		  else if (*t >= 'A' && *t <= 'F')
		    ch = (ch << 4) | (*t - 'A' + 10);
		  else
		    break;
		  ++t;
		}
	      *s++ = (unichar) ch;
	      break;

	    default:
	      rcparse_warning ("unrecognized escape sequence");
	      *s++ = '\\';
	      *s++ = (unichar) *t++;
	      break;
	    }
	}
      else if (*t != '"')
	*s++ = (unichar) *t++;
      else if (t[1] == '\0')
	break;
      else if (t[1] == '"')
	{
	  *s++ = '"';
	  t += 2;
	}
      else
	{
	  ++t;
	  assert (ISSPACE (*t));
	  while (ISSPACE (*t))
	    {
	      if ((*t) == '\n')
		++rc_lineno;
	      ++t;
	    }
	  if (*t == '\0')
	    break;
	  assert (*t == '"');
	  ++t;
	}
    }

  *s = '\0';

  *len = s - ret;

  return ret;
}

/* Discard all the strings we have allocated.  The parser calls this
   when it no longer needs them.  */

void
rcparse_discard_strings (void)
{
  struct alloc_string *as;

  as = strings;
  while (as != NULL)
    {
      struct alloc_string *n;

      free (as->s);
      n = as->next;
      free (as);
      as = n;
    }

  strings = NULL;
}

/* Enter rcdata mode.  */
void
rcparse_rcdata (void)
{
  rcdata_mode = 1;
}

/* Go back to normal mode from rcdata mode.  */
void
rcparse_normal (void)
{
  rcdata_mode = 0;
}

static void
rclex_tok_add_char (int ch)
{
  if (! rclex_tok || rclex_tok_max <= rclex_tok_pos)
    {
      char *h = xmalloc (rclex_tok_max + 9);

      if (! h)
	abort ();
      if (rclex_tok)
	{
	  memcpy (h, rclex_tok, rclex_tok_pos + 1);
	  free (rclex_tok);
	}
      else
	rclex_tok_pos = 0;
      rclex_tok_max += 8;
      rclex_tok = h;
    }
  if (ch != -1)
    rclex_tok[rclex_tok_pos++] = (char) ch;
  rclex_tok[rclex_tok_pos] = 0;
}

static int
rclex_readch (void)
{
  int r = -1;

  if ((r = rclex_lastch) != -1)
    rclex_lastch = -1;
  else
    {
      char ch;
      do
        {
	  if (! cpp_pipe || feof (cpp_pipe)
	      || fread (&ch, 1, 1,cpp_pipe) != 1)
	    break;
	  r = ((int) ch) & 0xff;
        }
      while (r == 0 || r == '\r');
  }
  rclex_tok_add_char (r);
  return r;
}

static int
rclex_peekch (void)
{
  int r;

  if ((r = rclex_lastch) == -1)
    {
      if ((r = rclex_readch ()) != -1)
	{
	  rclex_lastch = r;
	  if (rclex_tok_pos > 0)
	    rclex_tok[--rclex_tok_pos] = 0;
	}
    }
  return r;
}

static void
rclex_string (void)
{
  int c;
  
  while ((c = rclex_peekch ()) != -1)
    {
      if (c == '\n')
	break;
      if (c == '\\')
        {
	  rclex_readch ();
	  if ((c = rclex_peekch ()) == -1 || c == '\n')
	    break;
	  rclex_readch ();
        }
      else if (rclex_readch () == '"')
	{
	  if (rclex_peekch () == '"')
	    rclex_readch ();
	  else
	    break;
	}
    }
}

static rc_uint_type
read_digit (int ch)
{
  rc_uint_type base = 10;
  rc_uint_type ret, val;
  int warned = 0;

  ret = 0;
  if (ch == '0')
    {
      base = 8;
      switch (rclex_peekch ())
	{
	case 'o': case 'O':
	  rclex_readch ();
	  base = 8;
	  break;

	case 'x': case 'X':
	  rclex_readch ();
	  base = 16;
	  break;
	}
    }
  else
    ret = (rc_uint_type) (ch - '0');
  while ((ch = rclex_peekch ()) != -1)
    {
      if (ISDIGIT (ch))
	val = (rc_uint_type) (ch - '0');
      else if (ch >= 'a' && ch <= 'f')
	val = (rc_uint_type) ((ch - 'a') + 10);
      else if (ch >= 'A' && ch <= 'F')
	val = (rc_uint_type) ((ch - 'A') + 10);
      else
	break;
      rclex_readch ();
      if (! warned && val >= base)
	{
	  warned = 1;
	  rcparse_warning ("digit exceeds base");
	}
      ret *= base;
      ret += val;
    }
  return ret;
}

/* yyparser entry method.  */

int
yylex (void)
{
  char *s;
  unichar *us;
  rc_uint_type length;
  int ch;

  /* Make sure that rclex_tok is initialized.  */
  if (! rclex_tok)
    rclex_tok_add_char (-1);

  do
    {
      do
	{
	  /* Clear token.  */
	  rclex_tok_pos = 0;
	  rclex_tok[0] = 0;
	  
	  if ((ch = rclex_readch ()) == -1)
	    return -1;
	  if (ch == '\n')
	    ++rc_lineno;
	}
      while (ch <= 0x20);

      switch (ch)
	{
	case '#':
	  while ((ch = rclex_peekch ()) != -1 && ch != '\n')
	    rclex_readch ();
	  cpp_line ();
	  ch = IGNORED_TOKEN;
	  break;
	
	case '{':
	  ch = IGNORE_CPP (BEG);
	  break;
	
	case '}':
	  ch = IGNORE_CPP (END);
	  break;
	
	case '0': case '1': case '2': case '3': case '4':
	case '5': case '6': case '7': case '8': case '9':
	  yylval.i.val = read_digit (ch);
	  yylval.i.dword = 0;
	  switch (rclex_peekch ())
	    {
	    case 'l': case 'L':
	      rclex_readch ();
	      yylval.i.dword = 1;
	      break;
	    }
	  ch = IGNORE_CPP (NUMBER);
	  break;
	case '"':
	  rclex_string ();
	  ch = IGNORE_CPP ((! rcdata_mode ? QUOTEDSTRING : SIZEDSTRING));
	  if (ch == IGNORED_TOKEN)
	    break;
	  s = handle_quotes (&length);
	  if (! rcdata_mode)
	    yylval.s = s;
	  else
	    {
	      yylval.ss.length = length;
	      yylval.ss.s = s;
	  }
	  break;
	case 'L': case 'l':
	  if (rclex_peekch () == '"')
	    {
	      rclex_readch ();
	      rclex_string ();
	      ch = IGNORE_CPP ((! rcdata_mode ? QUOTEDUNISTRING : SIZEDUNISTRING));
	      if (ch == IGNORED_TOKEN)
		break;
	      us = handle_uniquotes (&length);
	      if (! rcdata_mode)
		yylval.uni = us;
	      else
	        {
		  yylval.suni.length = length;
		  yylval.suni.s = us;
	      }
	      break;
	    }
	  /* Fall through.  */
	default:
	  if (ISIDST (ch) || ch=='$')
	    {
	      while ((ch = rclex_peekch ()) != -1 && (ISIDNUM (ch) || ch == '$' || ch == '.'))
		rclex_readch ();
	      ch = IGNORE_CPP (rclex_translatekeyword (rclex_tok));
	      if (ch == STRING)
		{
		  s = get_string (strlen (rclex_tok) + 1);
		  strcpy (s, rclex_tok);
		  yylval.s = s;
		}
	      else if (ch == BLOCK)
		{
		  const char *hs = NULL;

		  switch (yylex ())
		  {
		  case STRING:
		  case QUOTEDSTRING:
		    hs = yylval.s;
		    break;
		  case SIZEDSTRING:
		    hs = yylval.s = yylval.ss.s;
		    break;
		  }
		  if (! hs)
		    {
		      rcparse_warning ("BLOCK expects a string as argument.");
		      ch = IGNORED_TOKEN;
		    }
		  else if (! strcmp (hs, "StringFileInfo"))
		    ch = BLOCKSTRINGFILEINFO;
		  else if (! strcmp (hs, "VarFileInfo"))
		    ch = BLOCKVARFILEINFO;
		}
	      break;
	    }
	  ch = IGNORE_CPP (ch);
	  break;
	}
    }
  while (ch == IGNORED_TOKEN);

  return ch;
}

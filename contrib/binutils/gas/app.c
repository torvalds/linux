/* This is the Assembler Pre-Processor
   Copyright 1987, 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997, 1998,
   1999, 2000, 2001, 2002, 2003, 2006, 2007
   Free Software Foundation, Inc.

   This file is part of GAS, the GNU Assembler.

   GAS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GAS is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GAS; see the file COPYING.  If not, write to the Free
   Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

/* Modified by Allen Wirfs-Brock, Instantiations Inc 2/90.  */
/* App, the assembler pre-processor.  This pre-processor strips out
   excess spaces, turns single-quoted characters into a decimal
   constant, and turns the # in # <number> <filename> <garbage> into a
   .linefile.  This needs better error-handling.  */

#include "as.h"

#if (__STDC__ != 1)
#ifndef const
#define const  /* empty */
#endif
#endif

#ifdef TC_M68K
/* Whether we are scrubbing in m68k MRI mode.  This is different from
   flag_m68k_mri, because the two flags will be affected by the .mri
   pseudo-op at different times.  */
static int scrub_m68k_mri;

/* The pseudo-op which switches in and out of MRI mode.  See the
   comment in do_scrub_chars.  */
static const char mri_pseudo[] = ".mri 0";
#else
#define scrub_m68k_mri 0
#endif

#if defined TC_ARM && defined OBJ_ELF
/* The pseudo-op for which we need to special-case `@' characters.
   See the comment in do_scrub_chars.  */
static const char   symver_pseudo[] = ".symver";
static const char * symver_state;
#endif

static char lex[256];
static const char symbol_chars[] =
"$._ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

#define LEX_IS_SYMBOL_COMPONENT		1
#define LEX_IS_WHITESPACE		2
#define LEX_IS_LINE_SEPARATOR		3
#define LEX_IS_COMMENT_START		4
#define LEX_IS_LINE_COMMENT_START	5
#define	LEX_IS_TWOCHAR_COMMENT_1ST	6
#define	LEX_IS_STRINGQUOTE		8
#define	LEX_IS_COLON			9
#define	LEX_IS_NEWLINE			10
#define	LEX_IS_ONECHAR_QUOTE		11
#ifdef TC_V850
#define LEX_IS_DOUBLEDASH_1ST		12
#endif
#ifdef TC_M32R
#define DOUBLEBAR_PARALLEL
#endif
#ifdef DOUBLEBAR_PARALLEL
#define LEX_IS_DOUBLEBAR_1ST		13
#endif
#define LEX_IS_PARALLEL_SEPARATOR	14
#define IS_SYMBOL_COMPONENT(c)		(lex[c] == LEX_IS_SYMBOL_COMPONENT)
#define IS_WHITESPACE(c)		(lex[c] == LEX_IS_WHITESPACE)
#define IS_LINE_SEPARATOR(c)		(lex[c] == LEX_IS_LINE_SEPARATOR)
#define IS_PARALLEL_SEPARATOR(c)	(lex[c] == LEX_IS_PARALLEL_SEPARATOR)
#define IS_COMMENT(c)			(lex[c] == LEX_IS_COMMENT_START)
#define IS_LINE_COMMENT(c)		(lex[c] == LEX_IS_LINE_COMMENT_START)
#define	IS_NEWLINE(c)			(lex[c] == LEX_IS_NEWLINE)

static int process_escape (int);

/* FIXME-soon: The entire lexer/parser thingy should be
   built statically at compile time rather than dynamically
   each and every time the assembler is run.  xoxorich.  */

void
do_scrub_begin (int m68k_mri ATTRIBUTE_UNUSED)
{
  const char *p;
  int c;

  lex[' '] = LEX_IS_WHITESPACE;
  lex['\t'] = LEX_IS_WHITESPACE;
  lex['\r'] = LEX_IS_WHITESPACE;
  lex['\n'] = LEX_IS_NEWLINE;
  lex[':'] = LEX_IS_COLON;

#ifdef TC_M68K
  scrub_m68k_mri = m68k_mri;

  if (! m68k_mri)
#endif
    {
      lex['"'] = LEX_IS_STRINGQUOTE;

#if ! defined (TC_HPPA) && ! defined (TC_I370)
      /* I370 uses single-quotes to delimit integer, float constants.  */
      lex['\''] = LEX_IS_ONECHAR_QUOTE;
#endif

#ifdef SINGLE_QUOTE_STRINGS
      lex['\''] = LEX_IS_STRINGQUOTE;
#endif
    }

  /* Note: if any other character can be LEX_IS_STRINGQUOTE, the loop
     in state 5 of do_scrub_chars must be changed.  */

  /* Note that these override the previous defaults, e.g. if ';' is a
     comment char, then it isn't a line separator.  */
  for (p = symbol_chars; *p; ++p)
    lex[(unsigned char) *p] = LEX_IS_SYMBOL_COMPONENT;

  for (c = 128; c < 256; ++c)
    lex[c] = LEX_IS_SYMBOL_COMPONENT;

#ifdef tc_symbol_chars
  /* This macro permits the processor to specify all characters which
     may appears in an operand.  This will prevent the scrubber from
     discarding meaningful whitespace in certain cases.  The i386
     backend uses this to support prefixes, which can confuse the
     scrubber as to whether it is parsing operands or opcodes.  */
  for (p = tc_symbol_chars; *p; ++p)
    lex[(unsigned char) *p] = LEX_IS_SYMBOL_COMPONENT;
#endif

  /* The m68k backend wants to be able to change comment_chars.  */
#ifndef tc_comment_chars
#define tc_comment_chars comment_chars
#endif
  for (p = tc_comment_chars; *p; p++)
    lex[(unsigned char) *p] = LEX_IS_COMMENT_START;

  for (p = line_comment_chars; *p; p++)
    lex[(unsigned char) *p] = LEX_IS_LINE_COMMENT_START;

  for (p = line_separator_chars; *p; p++)
    lex[(unsigned char) *p] = LEX_IS_LINE_SEPARATOR;

#ifdef tc_parallel_separator_chars
  /* This macro permits the processor to specify all characters which
     separate parallel insns on the same line.  */
  for (p = tc_parallel_separator_chars; *p; p++)
    lex[(unsigned char) *p] = LEX_IS_PARALLEL_SEPARATOR;
#endif

  /* Only allow slash-star comments if slash is not in use.
     FIXME: This isn't right.  We should always permit them.  */
  if (lex['/'] == 0)
    lex['/'] = LEX_IS_TWOCHAR_COMMENT_1ST;

#ifdef TC_M68K
  if (m68k_mri)
    {
      lex['\''] = LEX_IS_STRINGQUOTE;
      lex[';'] = LEX_IS_COMMENT_START;
      lex['*'] = LEX_IS_LINE_COMMENT_START;
      /* The MRI documentation says '!' is LEX_IS_COMMENT_START, but
	 then it can't be used in an expression.  */
      lex['!'] = LEX_IS_LINE_COMMENT_START;
    }
#endif

#ifdef TC_V850
  lex['-'] = LEX_IS_DOUBLEDASH_1ST;
#endif
#ifdef DOUBLEBAR_PARALLEL
  lex['|'] = LEX_IS_DOUBLEBAR_1ST;
#endif
#ifdef TC_D30V
  /* Must do this is we want VLIW instruction with "->" or "<-".  */
  lex['-'] = LEX_IS_SYMBOL_COMPONENT;
#endif
}

/* Saved state of the scrubber.  */
static int state;
static int old_state;
static char *out_string;
static char out_buf[20];
static int add_newlines;
static char *saved_input;
static int saved_input_len;
static char input_buffer[32 * 1024];
static const char *mri_state;
static char mri_last_ch;

/* Data structure for saving the state of app across #include's.  Note that
   app is called asynchronously to the parsing of the .include's, so our
   state at the time .include is interpreted is completely unrelated.
   That's why we have to save it all.  */

struct app_save
{
  int          state;
  int          old_state;
  char *       out_string;
  char         out_buf[sizeof (out_buf)];
  int          add_newlines;
  char *       saved_input;
  int          saved_input_len;
#ifdef TC_M68K
  int          scrub_m68k_mri;
#endif
  const char * mri_state;
  char         mri_last_ch;
#if defined TC_ARM && defined OBJ_ELF
  const char * symver_state;
#endif
};

char *
app_push (void)
{
  register struct app_save *saved;

  saved = (struct app_save *) xmalloc (sizeof (*saved));
  saved->state = state;
  saved->old_state = old_state;
  saved->out_string = out_string;
  memcpy (saved->out_buf, out_buf, sizeof (out_buf));
  saved->add_newlines = add_newlines;
  if (saved_input == NULL)
    saved->saved_input = NULL;
  else
    {
      saved->saved_input = xmalloc (saved_input_len);
      memcpy (saved->saved_input, saved_input, saved_input_len);
      saved->saved_input_len = saved_input_len;
    }
#ifdef TC_M68K
  saved->scrub_m68k_mri = scrub_m68k_mri;
#endif
  saved->mri_state = mri_state;
  saved->mri_last_ch = mri_last_ch;
#if defined TC_ARM && defined OBJ_ELF
  saved->symver_state = symver_state;
#endif

  /* do_scrub_begin() is not useful, just wastes time.  */

  state = 0;
  saved_input = NULL;

  return (char *) saved;
}

void
app_pop (char *arg)
{
  register struct app_save *saved = (struct app_save *) arg;

  /* There is no do_scrub_end ().  */
  state = saved->state;
  old_state = saved->old_state;
  out_string = saved->out_string;
  memcpy (out_buf, saved->out_buf, sizeof (out_buf));
  add_newlines = saved->add_newlines;
  if (saved->saved_input == NULL)
    saved_input = NULL;
  else
    {
      assert (saved->saved_input_len <= (int) (sizeof input_buffer));
      memcpy (input_buffer, saved->saved_input, saved->saved_input_len);
      saved_input = input_buffer;
      saved_input_len = saved->saved_input_len;
      free (saved->saved_input);
    }
#ifdef TC_M68K
  scrub_m68k_mri = saved->scrub_m68k_mri;
#endif
  mri_state = saved->mri_state;
  mri_last_ch = saved->mri_last_ch;
#if defined TC_ARM && defined OBJ_ELF
  symver_state = saved->symver_state;
#endif

  free (arg);
}

/* @@ This assumes that \n &c are the same on host and target.  This is not
   necessarily true.  */

static int
process_escape (int ch)
{
  switch (ch)
    {
    case 'b':
      return '\b';
    case 'f':
      return '\f';
    case 'n':
      return '\n';
    case 'r':
      return '\r';
    case 't':
      return '\t';
    case '\'':
      return '\'';
    case '"':
      return '\"';
    default:
      return ch;
    }
}

/* This function is called to process input characters.  The GET
   parameter is used to retrieve more input characters.  GET should
   set its parameter to point to a buffer, and return the length of
   the buffer; it should return 0 at end of file.  The scrubbed output
   characters are put into the buffer starting at TOSTART; the TOSTART
   buffer is TOLEN bytes in length.  The function returns the number
   of scrubbed characters put into TOSTART.  This will be TOLEN unless
   end of file was seen.  This function is arranged as a state
   machine, and saves its state so that it may return at any point.
   This is the way the old code used to work.  */

int
do_scrub_chars (int (*get) (char *, int), char *tostart, int tolen)
{
  char *to = tostart;
  char *toend = tostart + tolen;
  char *from;
  char *fromend;
  int fromlen;
  register int ch, ch2 = 0;
  /* Character that started the string we're working on.  */
  static char quotechar;

  /*State 0: beginning of normal line
	  1: After first whitespace on line (flush more white)
	  2: After first non-white (opcode) on line (keep 1white)
	  3: after second white on line (into operands) (flush white)
	  4: after putting out a .linefile, put out digits
	  5: parsing a string, then go to old-state
	  6: putting out \ escape in a "d string.
	  7: no longer used
	  8: no longer used
	  9: After seeing symbol char in state 3 (keep 1white after symchar)
	 10: After seeing whitespace in state 9 (keep white before symchar)
	 11: After seeing a symbol character in state 0 (eg a label definition)
	 -1: output string in out_string and go to the state in old_state
	 -2: flush text until a '*' '/' is seen, then go to state old_state
#ifdef TC_V850
	 12: After seeing a dash, looking for a second dash as a start
	     of comment.
#endif
#ifdef DOUBLEBAR_PARALLEL
	 13: After seeing a vertical bar, looking for a second
	     vertical bar as a parallel expression separator.
#endif
#ifdef TC_IA64
	 14: After seeing a `(' at state 0, looking for a `)' as
	     predicate.
	 15: After seeing a `(' at state 1, looking for a `)' as
	     predicate.
#endif
#ifdef TC_Z80
	 16: After seeing an 'a' or an 'A' at the start of a symbol
	 17: After seeing an 'f' or an 'F' in state 16
#endif
	  */

  /* I added states 9 and 10 because the MIPS ECOFF assembler uses
     constructs like ``.loc 1 20''.  This was turning into ``.loc
     120''.  States 9 and 10 ensure that a space is never dropped in
     between characters which could appear in an identifier.  Ian
     Taylor, ian@cygnus.com.

     I added state 11 so that something like "Lfoo add %r25,%r26,%r27" works
     correctly on the PA (and any other target where colons are optional).
     Jeff Law, law@cs.utah.edu.

     I added state 13 so that something like "cmp r1, r2 || trap #1" does not
     get squashed into "cmp r1,r2||trap#1", with the all important space
     between the 'trap' and the '#1' being eliminated.  nickc@cygnus.com  */

  /* This macro gets the next input character.  */

#define GET()							\
  (from < fromend						\
   ? * (unsigned char *) (from++)				\
   : (saved_input = NULL,					\
      fromlen = (*get) (input_buffer, sizeof input_buffer),	\
      from = input_buffer,					\
      fromend = from + fromlen,					\
      (fromlen == 0						\
       ? EOF							\
       : * (unsigned char *) (from++))))

  /* This macro pushes a character back on the input stream.  */

#define UNGET(uch) (*--from = (uch))

  /* This macro puts a character into the output buffer.  If this
     character fills the output buffer, this macro jumps to the label
     TOFULL.  We use this rather ugly approach because we need to
     handle two different termination conditions: EOF on the input
     stream, and a full output buffer.  It would be simpler if we
     always read in the entire input stream before processing it, but
     I don't want to make such a significant change to the assembler's
     memory usage.  */

#define PUT(pch)				\
  do						\
    {						\
      *to++ = (pch);				\
      if (to >= toend)				\
	goto tofull;				\
    }						\
  while (0)

  if (saved_input != NULL)
    {
      from = saved_input;
      fromend = from + saved_input_len;
    }
  else
    {
      fromlen = (*get) (input_buffer, sizeof input_buffer);
      if (fromlen == 0)
	return 0;
      from = input_buffer;
      fromend = from + fromlen;
    }

  while (1)
    {
      /* The cases in this switch end with continue, in order to
	 branch back to the top of this while loop and generate the
	 next output character in the appropriate state.  */
      switch (state)
	{
	case -1:
	  ch = *out_string++;
	  if (*out_string == '\0')
	    {
	      state = old_state;
	      old_state = 3;
	    }
	  PUT (ch);
	  continue;

	case -2:
	  for (;;)
	    {
	      do
		{
		  ch = GET ();

		  if (ch == EOF)
		    {
		      as_warn (_("end of file in comment"));
		      goto fromeof;
		    }

		  if (ch == '\n')
		    PUT ('\n');
		}
	      while (ch != '*');

	      while ((ch = GET ()) == '*')
		;

	      if (ch == EOF)
		{
		  as_warn (_("end of file in comment"));
		  goto fromeof;
		}

	      if (ch == '/')
		break;

	      UNGET (ch);
	    }

	  state = old_state;
	  UNGET (' ');
	  continue;

	case 4:
	  ch = GET ();
	  if (ch == EOF)
	    goto fromeof;
	  else if (ch >= '0' && ch <= '9')
	    PUT (ch);
	  else
	    {
	      while (ch != EOF && IS_WHITESPACE (ch))
		ch = GET ();
	      if (ch == '"')
		{
		  quotechar = ch;
		  state = 5;
		  old_state = 3;
		  PUT (ch);
		}
	      else
		{
		  while (ch != EOF && ch != '\n')
		    ch = GET ();
		  state = 0;
		  PUT (ch);
		}
	    }
	  continue;

	case 5:
	  /* We are going to copy everything up to a quote character,
	     with special handling for a backslash.  We try to
	     optimize the copying in the simple case without using the
	     GET and PUT macros.  */
	  {
	    char *s;
	    int len;

	    for (s = from; s < fromend; s++)
	      {
		ch = *s;
		if (ch == '\\'
		    || ch == quotechar
		    || ch == '\n')
		  break;
	      }
	    len = s - from;
	    if (len > toend - to)
	      len = toend - to;
	    if (len > 0)
	      {
		memcpy (to, from, len);
		to += len;
		from += len;
		if (to >= toend)
		  goto tofull;
	      }
	  }

	  ch = GET ();
	  if (ch == EOF)
	    {
	      as_warn (_("end of file in string; '%c' inserted"), quotechar);
	      state = old_state;
	      UNGET ('\n');
	      PUT (quotechar);
	    }
	  else if (ch == quotechar)
	    {
	      state = old_state;
	      PUT (ch);
	    }
#ifndef NO_STRING_ESCAPES
	  else if (ch == '\\')
	    {
	      state = 6;
	      PUT (ch);
	    }
#endif
	  else if (scrub_m68k_mri && ch == '\n')
	    {
	      /* Just quietly terminate the string.  This permits lines like
		   bne	label	loop if we haven't reach end yet.  */
	      state = old_state;
	      UNGET (ch);
	      PUT ('\'');
	    }
	  else
	    {
	      PUT (ch);
	    }
	  continue;

	case 6:
	  state = 5;
	  ch = GET ();
	  switch (ch)
	    {
	      /* Handle strings broken across lines, by turning '\n' into
		 '\\' and 'n'.  */
	    case '\n':
	      UNGET ('n');
	      add_newlines++;
	      PUT ('\\');
	      continue;

	    case EOF:
	      as_warn (_("end of file in string; '%c' inserted"), quotechar);
	      PUT (quotechar);
	      continue;

	    case '"':
	    case '\\':
	    case 'b':
	    case 'f':
	    case 'n':
	    case 'r':
	    case 't':
	    case 'v':
	    case 'x':
	    case 'X':
	    case '0':
	    case '1':
	    case '2':
	    case '3':
	    case '4':
	    case '5':
	    case '6':
	    case '7':
	      break;

	    default:
#ifdef ONLY_STANDARD_ESCAPES
	      as_warn (_("unknown escape '\\%c' in string; ignored"), ch);
#endif
	      break;
	    }
	  PUT (ch);
	  continue;

#ifdef DOUBLEBAR_PARALLEL
	case 13:
	  ch = GET ();
	  if (ch != '|')
	    abort ();

	  /* Reset back to state 1 and pretend that we are parsing a
	     line from just after the first white space.  */
	  state = 1;
	  PUT ('|');
	  continue;
#endif
#ifdef TC_Z80
	case 16:
	  /* We have seen an 'a' at the start of a symbol, look for an 'f'.  */
	  ch = GET ();
	  if (ch == 'f' || ch == 'F') 
	    {
	      state = 17;
	      PUT (ch);
	    }
	  else
	    {
	      state = 9;
	      break;
	    }
	case 17:
	  /* We have seen "af" at the start of a symbol,
	     a ' here is a part of that symbol.  */
	  ch = GET ();
	  state = 9;
	  if (ch == '\'')
	    /* Change to avoid warning about unclosed string.  */
	    PUT ('`');
	  else
	    UNGET (ch);
	  break;
#endif
	}

      /* OK, we are somewhere in states 0 through 4 or 9 through 11.  */

      /* flushchar: */
      ch = GET ();

#ifdef TC_IA64
      if (ch == '(' && (state == 0 || state == 1))
	{
	  state += 14;
	  PUT (ch);
	  continue;
	}
      else if (state == 14 || state == 15)
	{
	  if (ch == ')')
	    {
	      state -= 14;
	      PUT (ch);
	      ch = GET ();
	    }
	  else
	    {
	      PUT (ch);
	      continue;
	    }
	}
#endif

    recycle:

#if defined TC_ARM && defined OBJ_ELF
      /* We need to watch out for .symver directives.  See the comment later
	 in this function.  */
      if (symver_state == NULL)
	{
	  if ((state == 0 || state == 1) && ch == symver_pseudo[0])
	    symver_state = symver_pseudo + 1;
	}
      else
	{
	  /* We advance to the next state if we find the right
	     character.  */
	  if (ch != '\0' && (*symver_state == ch))
	    ++symver_state;
	  else if (*symver_state != '\0')
	    /* We did not get the expected character, or we didn't
	       get a valid terminating character after seeing the
	       entire pseudo-op, so we must go back to the beginning.  */
	    symver_state = NULL;
	  else
	    {
	      /* We've read the entire pseudo-op.  If this is the end
		 of the line, go back to the beginning.  */
	      if (IS_NEWLINE (ch))
		symver_state = NULL;
	    }
	}
#endif /* TC_ARM && OBJ_ELF */

#ifdef TC_M68K
      /* We want to have pseudo-ops which control whether we are in
	 MRI mode or not.  Unfortunately, since m68k MRI mode affects
	 the scrubber, that means that we need a special purpose
	 recognizer here.  */
      if (mri_state == NULL)
	{
	  if ((state == 0 || state == 1)
	      && ch == mri_pseudo[0])
	    mri_state = mri_pseudo + 1;
	}
      else
	{
	  /* We advance to the next state if we find the right
	     character, or if we need a space character and we get any
	     whitespace character, or if we need a '0' and we get a
	     '1' (this is so that we only need one state to handle
	     ``.mri 0'' and ``.mri 1'').  */
	  if (ch != '\0'
	      && (*mri_state == ch
		  || (*mri_state == ' '
		      && lex[ch] == LEX_IS_WHITESPACE)
		  || (*mri_state == '0'
		      && ch == '1')))
	    {
	      mri_last_ch = ch;
	      ++mri_state;
	    }
	  else if (*mri_state != '\0'
		   || (lex[ch] != LEX_IS_WHITESPACE
		       && lex[ch] != LEX_IS_NEWLINE))
	    {
	      /* We did not get the expected character, or we didn't
		 get a valid terminating character after seeing the
		 entire pseudo-op, so we must go back to the
		 beginning.  */
	      mri_state = NULL;
	    }
	  else
	    {
	      /* We've read the entire pseudo-op.  mips_last_ch is
		 either '0' or '1' indicating whether to enter or
		 leave MRI mode.  */
	      do_scrub_begin (mri_last_ch == '1');
	      mri_state = NULL;

	      /* We continue handling the character as usual.  The
		 main gas reader must also handle the .mri pseudo-op
		 to control expression parsing and the like.  */
	    }
	}
#endif

      if (ch == EOF)
	{
	  if (state != 0)
	    {
	      as_warn (_("end of file not at end of a line; newline inserted"));
	      state = 0;
	      PUT ('\n');
	    }
	  goto fromeof;
	}

      switch (lex[ch])
	{
	case LEX_IS_WHITESPACE:
	  do
	    {
	      ch = GET ();
	    }
	  while (ch != EOF && IS_WHITESPACE (ch));
	  if (ch == EOF)
	    goto fromeof;

	  if (state == 0)
	    {
	      /* Preserve a single whitespace character at the
		 beginning of a line.  */
	      state = 1;
	      UNGET (ch);
	      PUT (' ');
	      break;
	    }

#ifdef KEEP_WHITE_AROUND_COLON
	  if (lex[ch] == LEX_IS_COLON)
	    {
	      /* Only keep this white if there's no white *after* the
		 colon.  */
	      ch2 = GET ();
	      UNGET (ch2);
	      if (!IS_WHITESPACE (ch2))
		{
		  state = 9;
		  UNGET (ch);
		  PUT (' ');
		  break;
		}
	    }
#endif
	  if (IS_COMMENT (ch)
	      || ch == '/'
	      || IS_LINE_SEPARATOR (ch)
	      || IS_PARALLEL_SEPARATOR (ch))
	    {
	      if (scrub_m68k_mri)
		{
		  /* In MRI mode, we keep these spaces.  */
		  UNGET (ch);
		  PUT (' ');
		  break;
		}
	      goto recycle;
	    }

	  /* If we're in state 2 or 11, we've seen a non-white
	     character followed by whitespace.  If the next character
	     is ':', this is whitespace after a label name which we
	     normally must ignore.  In MRI mode, though, spaces are
	     not permitted between the label and the colon.  */
	  if ((state == 2 || state == 11)
	      && lex[ch] == LEX_IS_COLON
	      && ! scrub_m68k_mri)
	    {
	      state = 1;
	      PUT (ch);
	      break;
	    }

	  switch (state)
	    {
	    case 1:
	      /* We can arrive here if we leave a leading whitespace
		 character at the beginning of a line.  */
	      goto recycle;
	    case 2:
	      state = 3;
	      if (to + 1 < toend)
		{
		  /* Optimize common case by skipping UNGET/GET.  */
		  PUT (' ');	/* Sp after opco */
		  goto recycle;
		}
	      UNGET (ch);
	      PUT (' ');
	      break;
	    case 3:
	      if (scrub_m68k_mri)
		{
		  /* In MRI mode, we keep these spaces.  */
		  UNGET (ch);
		  PUT (' ');
		  break;
		}
	      goto recycle;	/* Sp in operands */
	    case 9:
	    case 10:
	      if (scrub_m68k_mri)
		{
		  /* In MRI mode, we keep these spaces.  */
		  state = 3;
		  UNGET (ch);
		  PUT (' ');
		  break;
		}
	      state = 10;	/* Sp after symbol char */
	      goto recycle;
	    case 11:
	      if (LABELS_WITHOUT_COLONS || flag_m68k_mri)
		state = 1;
	      else
		{
		  /* We know that ch is not ':', since we tested that
		     case above.  Therefore this is not a label, so it
		     must be the opcode, and we've just seen the
		     whitespace after it.  */
		  state = 3;
		}
	      UNGET (ch);
	      PUT (' ');	/* Sp after label definition.  */
	      break;
	    default:
	      BAD_CASE (state);
	    }
	  break;

	case LEX_IS_TWOCHAR_COMMENT_1ST:
	  ch2 = GET ();
	  if (ch2 == '*')
	    {
	      for (;;)
		{
		  do
		    {
		      ch2 = GET ();
		      if (ch2 != EOF && IS_NEWLINE (ch2))
			add_newlines++;
		    }
		  while (ch2 != EOF && ch2 != '*');

		  while (ch2 == '*')
		    ch2 = GET ();

		  if (ch2 == EOF || ch2 == '/')
		    break;

		  /* This UNGET will ensure that we count newlines
		     correctly.  */
		  UNGET (ch2);
		}

	      if (ch2 == EOF)
		as_warn (_("end of file in multiline comment"));

	      ch = ' ';
	      goto recycle;
	    }
#ifdef DOUBLESLASH_LINE_COMMENTS
	  else if (ch2 == '/')
	    {
	      do
		{
		  ch = GET ();
		}
	      while (ch != EOF && !IS_NEWLINE (ch));
	      if (ch == EOF)
		as_warn ("end of file in comment; newline inserted");
	      state = 0;
	      PUT ('\n');
	      break;
	    }
#endif
	  else
	    {
	      if (ch2 != EOF)
		UNGET (ch2);
	      if (state == 9 || state == 10)
		state = 3;
	      PUT (ch);
	    }
	  break;

	case LEX_IS_STRINGQUOTE:
	  quotechar = ch;
	  if (state == 10)
	    {
	      /* Preserve the whitespace in foo "bar".  */
	      UNGET (ch);
	      state = 3;
	      PUT (' ');

	      /* PUT didn't jump out.  We could just break, but we
		 know what will happen, so optimize a bit.  */
	      ch = GET ();
	      old_state = 3;
	    }
	  else if (state == 9)
	    old_state = 3;
	  else
	    old_state = state;
	  state = 5;
	  PUT (ch);
	  break;

#ifndef IEEE_STYLE
	case LEX_IS_ONECHAR_QUOTE:
	  if (state == 10)
	    {
	      /* Preserve the whitespace in foo 'b'.  */
	      UNGET (ch);
	      state = 3;
	      PUT (' ');
	      break;
	    }
	  ch = GET ();
	  if (ch == EOF)
	    {
	      as_warn (_("end of file after a one-character quote; \\0 inserted"));
	      ch = 0;
	    }
	  if (ch == '\\')
	    {
	      ch = GET ();
	      if (ch == EOF)
		{
		  as_warn (_("end of file in escape character"));
		  ch = '\\';
		}
	      else
		ch = process_escape (ch);
	    }
	  sprintf (out_buf, "%d", (int) (unsigned char) ch);

	  /* None of these 'x constants for us.  We want 'x'.  */
	  if ((ch = GET ()) != '\'')
	    {
#ifdef REQUIRE_CHAR_CLOSE_QUOTE
	      as_warn (_("missing close quote; (assumed)"));
#else
	      if (ch != EOF)
		UNGET (ch);
#endif
	    }
	  if (strlen (out_buf) == 1)
	    {
	      PUT (out_buf[0]);
	      break;
	    }
	  if (state == 9)
	    old_state = 3;
	  else
	    old_state = state;
	  state = -1;
	  out_string = out_buf;
	  PUT (*out_string++);
	  break;
#endif

	case LEX_IS_COLON:
#ifdef KEEP_WHITE_AROUND_COLON
	  state = 9;
#else
	  if (state == 9 || state == 10)
	    state = 3;
	  else if (state != 3)
	    state = 1;
#endif
	  PUT (ch);
	  break;

	case LEX_IS_NEWLINE:
	  /* Roll out a bunch of newlines from inside comments, etc.  */
	  if (add_newlines)
	    {
	      --add_newlines;
	      UNGET (ch);
	    }
	  /* Fall through.  */

	case LEX_IS_LINE_SEPARATOR:
	  state = 0;
	  PUT (ch);
	  break;

	case LEX_IS_PARALLEL_SEPARATOR:
	  state = 1;
	  PUT (ch);
	  break;

#ifdef TC_V850
	case LEX_IS_DOUBLEDASH_1ST:
	  ch2 = GET ();
	  if (ch2 != '-')
	    {
	      UNGET (ch2);
	      goto de_fault;
	    }
	  /* Read and skip to end of line.  */
	  do
	    {
	      ch = GET ();
	    }
	  while (ch != EOF && ch != '\n');

	  if (ch == EOF)
	    as_warn (_("end of file in comment; newline inserted"));

	  state = 0;
	  PUT ('\n');
	  break;
#endif
#ifdef DOUBLEBAR_PARALLEL
	case LEX_IS_DOUBLEBAR_1ST:
	  ch2 = GET ();
	  UNGET (ch2);
	  if (ch2 != '|')
	    goto de_fault;

	  /* Handle '||' in two states as invoking PUT twice might
	     result in the first one jumping out of this loop.  We'd
	     then lose track of the state and one '|' char.  */
	  state = 13;
	  PUT ('|');
	  break;
#endif
	case LEX_IS_LINE_COMMENT_START:
	  /* FIXME-someday: The two character comment stuff was badly
	     thought out.  On i386, we want '/' as line comment start
	     AND we want C style comments.  hence this hack.  The
	     whole lexical process should be reworked.  xoxorich.  */
	  if (ch == '/')
	    {
	      ch2 = GET ();
	      if (ch2 == '*')
		{
		  old_state = 3;
		  state = -2;
		  break;
		}
	      else
		{
		  UNGET (ch2);
		}
	    }

	  if (state == 0 || state == 1)	/* Only comment at start of line.  */
	    {
	      int startch;

	      startch = ch;

	      do
		{
		  ch = GET ();
		}
	      while (ch != EOF && IS_WHITESPACE (ch));

	      if (ch == EOF)
		{
		  as_warn (_("end of file in comment; newline inserted"));
		  PUT ('\n');
		  break;
		}

	      if (ch < '0' || ch > '9' || state != 0 || startch != '#')
		{
		  /* Not a cpp line.  */
		  while (ch != EOF && !IS_NEWLINE (ch))
		    ch = GET ();
		  if (ch == EOF)
		    as_warn (_("end of file in comment; newline inserted"));
		  state = 0;
		  PUT ('\n');
		  break;
		}
	      /* Looks like `# 123 "filename"' from cpp.  */
	      UNGET (ch);
	      old_state = 4;
	      state = -1;
	      if (scrub_m68k_mri)
		out_string = "\tlinefile ";
	      else
		out_string = "\t.linefile ";
	      PUT (*out_string++);
	      break;
	    }

#ifdef TC_D10V
	  /* All insns end in a char for which LEX_IS_SYMBOL_COMPONENT is true.
	     Trap is the only short insn that has a first operand that is
	     neither register nor label.
	     We must prevent exef0f ||trap #1 to degenerate to exef0f ||trap#1 .
	     We can't make '#' LEX_IS_SYMBOL_COMPONENT because it is
	     already LEX_IS_LINE_COMMENT_START.  However, it is the
	     only character in line_comment_chars for d10v, hence we
	     can recognize it as such.  */
	  /* An alternative approach would be to reset the state to 1 when
	     we see '||', '<'- or '->', but that seems to be overkill.  */
	  if (state == 10)
	    PUT (' ');
#endif
	  /* We have a line comment character which is not at the
	     start of a line.  If this is also a normal comment
	     character, fall through.  Otherwise treat it as a default
	     character.  */
	  if (strchr (tc_comment_chars, ch) == NULL
	      && (! scrub_m68k_mri
		  || (ch != '!' && ch != '*')))
	    goto de_fault;
	  if (scrub_m68k_mri
	      && (ch == '!' || ch == '*' || ch == '#')
	      && state != 1
	      && state != 10)
	    goto de_fault;
	  /* Fall through.  */
	case LEX_IS_COMMENT_START:
#if defined TC_ARM && defined OBJ_ELF
	  /* On the ARM, `@' is the comment character.
	     Unfortunately this is also a special character in ELF .symver
	     directives (and .type, though we deal with those another way).
	     So we check if this line is such a directive, and treat
	     the character as default if so.  This is a hack.  */
	  if ((symver_state != NULL) && (*symver_state == 0))
	    goto de_fault;
#endif

#ifdef TC_ARM
	  /* For the ARM, care is needed not to damage occurrences of \@
	     by stripping the @ onwards.  Yuck.  */
	  if (to > tostart && *(to - 1) == '\\')
	    /* Do not treat the @ as a start-of-comment.  */
	    goto de_fault;
#endif

#ifdef WARN_COMMENTS
	  if (!found_comment)
	    as_where (&found_comment_file, &found_comment);
#endif
	  do
	    {
	      ch = GET ();
	    }
	  while (ch != EOF && !IS_NEWLINE (ch));
	  if (ch == EOF)
	    as_warn (_("end of file in comment; newline inserted"));
	  state = 0;
	  PUT ('\n');
	  break;

	case LEX_IS_SYMBOL_COMPONENT:
	  if (state == 10)
	    {
	      /* This is a symbol character following another symbol
		 character, with whitespace in between.  We skipped
		 the whitespace earlier, so output it now.  */
	      UNGET (ch);
	      state = 3;
	      PUT (' ');
	      break;
	    }

#ifdef TC_Z80
	  /* "af'" is a symbol containing '\''.  */
	  if (state == 3 && (ch == 'a' || ch == 'A')) 
	    {
	      state = 16;
	      PUT (ch);
	      ch = GET ();
	      if (ch == 'f' || ch == 'F') 
		{
		  state = 17;
		  PUT (ch);
		  break;
		}
	      else
		{
		  state = 9;
		  if (!IS_SYMBOL_COMPONENT (ch)) 
		    {
		      UNGET (ch);
		      break;
		    }
		}
	    }
#endif
	  if (state == 3)
	    state = 9;

	  /* This is a common case.  Quickly copy CH and all the
	     following symbol component or normal characters.  */
	  if (to + 1 < toend
	      && mri_state == NULL
#if defined TC_ARM && defined OBJ_ELF
	      && symver_state == NULL
#endif
	      )
	    {
	      char *s;
	      int len;

	      for (s = from; s < fromend; s++)
		{
		  int type;

		  ch2 = *(unsigned char *) s;
		  type = lex[ch2];
		  if (type != 0
		      && type != LEX_IS_SYMBOL_COMPONENT)
		    break;
		}

	      if (s > from)
		/* Handle the last character normally, for
		   simplicity.  */
		--s;

	      len = s - from;

	      if (len > (toend - to) - 1)
		len = (toend - to) - 1;

	      if (len > 0)
		{
		  PUT (ch);
		  memcpy (to, from, len);
		  to += len;
		  from += len;
		  if (to >= toend)
		    goto tofull;
		  ch = GET ();
		}
	    }

	  /* Fall through.  */
	default:
	de_fault:
	  /* Some relatively `normal' character.  */
	  if (state == 0)
	    {
	      state = 11;	/* Now seeing label definition.  */
	    }
	  else if (state == 1)
	    {
	      state = 2;	/* Ditto.  */
	    }
	  else if (state == 9)
	    {
	      if (!IS_SYMBOL_COMPONENT (ch))
		state = 3;
	    }
	  else if (state == 10)
	    {
	      if (ch == '\\')
		{
		  /* Special handling for backslash: a backslash may
		     be the beginning of a formal parameter (of a
		     macro) following another symbol character, with
		     whitespace in between.  If that is the case, we
		     output a space before the parameter.  Strictly
		     speaking, correct handling depends upon what the
		     macro parameter expands into; if the parameter
		     expands into something which does not start with
		     an operand character, then we don't want to keep
		     the space.  We don't have enough information to
		     make the right choice, so here we are making the
		     choice which is more likely to be correct.  */
		  if (to + 1 >= toend)
		    {
		      /* If we're near the end of the buffer, save the
		         character for the next time round.  Otherwise
		         we'll lose our state.  */
		      UNGET (ch);
		      goto tofull;
		    }
		  *to++ = ' ';
		}

	      state = 3;
	    }
	  PUT (ch);
	  break;
	}
    }

  /*NOTREACHED*/

 fromeof:
  /* We have reached the end of the input.  */
  return to - tostart;

 tofull:
  /* The output buffer is full.  Save any input we have not yet
     processed.  */
  if (fromend > from)
    {
      saved_input = from;
      saved_input_len = fromend - from;
    }
  else
    saved_input = NULL;

  return to - tostart;
}


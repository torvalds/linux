/* C preprocessor macro expansion for GDB.
   Copyright 2002 Free Software Foundation, Inc.
   Contributed by Red Hat, Inc.

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
#include "gdb_obstack.h"
#include "bcache.h"
#include "macrotab.h"
#include "macroexp.h"
#include "gdb_assert.h"



/* A resizeable, substringable string type.  */


/* A string type that we can resize, quickly append to, and use to
   refer to substrings of other strings.  */
struct macro_buffer
{
  /* An array of characters.  The first LEN bytes are the real text,
     but there are SIZE bytes allocated to the array.  If SIZE is
     zero, then this doesn't point to a malloc'ed block.  If SHARED is
     non-zero, then this buffer is actually a pointer into some larger
     string, and we shouldn't append characters to it, etc.  Because
     of sharing, we can't assume in general that the text is
     null-terminated.  */
  char *text;

  /* The number of characters in the string.  */
  int len;

  /* The number of characters allocated to the string.  If SHARED is
     non-zero, this is meaningless; in this case, we set it to zero so
     that any "do we have room to append something?" tests will fail,
     so we don't always have to check SHARED before using this field.  */
  int size;

  /* Zero if TEXT can be safely realloc'ed (i.e., it's its own malloc
     block).  Non-zero if TEXT is actually pointing into the middle of
     some other block, and we shouldn't reallocate it.  */
  int shared;

  /* For detecting token splicing. 

     This is the index in TEXT of the first character of the token
     that abuts the end of TEXT.  If TEXT contains no tokens, then we
     set this equal to LEN.  If TEXT ends in whitespace, then there is
     no token abutting the end of TEXT (it's just whitespace), and
     again, we set this equal to LEN.  We set this to -1 if we don't
     know the nature of TEXT.  */
  int last_token;

  /* If this buffer is holding the result from get_token, then this 
     is non-zero if it is an identifier token, zero otherwise.  */
  int is_identifier;
};


/* Set the macro buffer *B to the empty string, guessing that its
   final contents will fit in N bytes.  (It'll get resized if it
   doesn't, so the guess doesn't have to be right.)  Allocate the
   initial storage with xmalloc.  */
static void
init_buffer (struct macro_buffer *b, int n)
{
  /* Small value for initial testing.  */
  n = 1;

  b->size = n;
  if (n > 0)
    b->text = (char *) xmalloc (n);
  else
    b->text = NULL;
  b->len = 0;
  b->shared = 0;
  b->last_token = -1;
}


/* Set the macro buffer *BUF to refer to the LEN bytes at ADDR, as a
   shared substring.  */
static void
init_shared_buffer (struct macro_buffer *buf, char *addr, int len)
{
  buf->text = addr;
  buf->len = len;
  buf->shared = 1;
  buf->size = 0;
  buf->last_token = -1;
}


/* Free the text of the buffer B.  Raise an error if B is shared.  */
static void
free_buffer (struct macro_buffer *b)
{
  gdb_assert (! b->shared);
  if (b->size)
    xfree (b->text);
}


/* A cleanup function for macro buffers.  */
static void
cleanup_macro_buffer (void *untyped_buf)
{
  free_buffer ((struct macro_buffer *) untyped_buf);
}


/* Resize the buffer B to be at least N bytes long.  Raise an error if
   B shouldn't be resized.  */
static void
resize_buffer (struct macro_buffer *b, int n)
{
  /* We shouldn't be trying to resize shared strings.  */
  gdb_assert (! b->shared);
  
  if (b->size == 0)
    b->size = n;
  else
    while (b->size <= n)
      b->size *= 2;

  b->text = xrealloc (b->text, b->size);
}


/* Append the character C to the buffer B.  */
static void
appendc (struct macro_buffer *b, int c)
{
  int new_len = b->len + 1;

  if (new_len > b->size)
    resize_buffer (b, new_len);

  b->text[b->len] = c;
  b->len = new_len;
}


/* Append the LEN bytes at ADDR to the buffer B.  */
static void
appendmem (struct macro_buffer *b, char *addr, int len)
{
  int new_len = b->len + len;

  if (new_len > b->size)
    resize_buffer (b, new_len);

  memcpy (b->text + b->len, addr, len);
  b->len = new_len;
}



/* Recognizing preprocessor tokens.  */


static int
is_whitespace (int c)
{
  return (c == ' '
          || c == '\t'
          || c == '\n'
          || c == '\v'
          || c == '\f');
}


static int
is_digit (int c)
{
  return ('0' <= c && c <= '9');
}


static int
is_identifier_nondigit (int c)
{
  return (c == '_'
          || ('a' <= c && c <= 'z')
          || ('A' <= c && c <= 'Z'));
}


static void
set_token (struct macro_buffer *tok, char *start, char *end)
{
  init_shared_buffer (tok, start, end - start);
  tok->last_token = 0;

  /* Presumed; get_identifier may overwrite this. */
  tok->is_identifier = 0;
}


static int
get_comment (struct macro_buffer *tok, char *p, char *end)
{
  if (p + 2 > end)
    return 0;
  else if (p[0] == '/'
           && p[1] == '*')
    {
      char *tok_start = p;

      p += 2;

      for (; p < end; p++)
        if (p + 2 <= end
            && p[0] == '*'
            && p[1] == '/')
          {
            p += 2;
            set_token (tok, tok_start, p);
            return 1;
          }

      error ("Unterminated comment in macro expansion.");
    }
  else if (p[0] == '/'
           && p[1] == '/')
    {
      char *tok_start = p;

      p += 2;
      for (; p < end; p++)
        if (*p == '\n')
          break;

      set_token (tok, tok_start, p);
      return 1;
    }
  else
    return 0;
}


static int
get_identifier (struct macro_buffer *tok, char *p, char *end)
{
  if (p < end
      && is_identifier_nondigit (*p))
    {
      char *tok_start = p;

      while (p < end
             && (is_identifier_nondigit (*p)
                 || is_digit (*p)))
        p++;

      set_token (tok, tok_start, p);
      tok->is_identifier = 1;
      return 1;
    }
  else
    return 0;
}


static int
get_pp_number (struct macro_buffer *tok, char *p, char *end)
{
  if (p < end
      && (is_digit (*p)
          || *p == '.'))
    {
      char *tok_start = p;

      while (p < end)
        {
          if (is_digit (*p)
              || is_identifier_nondigit (*p)
              || *p == '.')
            p++;
          else if (p + 2 <= end
                   && strchr ("eEpP.", *p)
                   && (p[1] == '+' || p[1] == '-'))
            p += 2;
          else
            break;
        }

      set_token (tok, tok_start, p);
      return 1;
    }
  else
    return 0;
}



/* If the text starting at P going up to (but not including) END
   starts with a character constant, set *TOK to point to that
   character constant, and return 1.  Otherwise, return zero.
   Signal an error if it contains a malformed or incomplete character
   constant.  */
static int
get_character_constant (struct macro_buffer *tok, char *p, char *end)
{
  /* ISO/IEC 9899:1999 (E)  Section 6.4.4.4  paragraph 1 
     But of course, what really matters is that we handle it the same
     way GDB's C/C++ lexer does.  So we call parse_escape in utils.c
     to handle escape sequences.  */
  if ((p + 1 <= end && *p == '\'')
      || (p + 2 <= end && p[0] == 'L' && p[1] == '\''))
    {
      char *tok_start = p;
      char *body_start;

      if (*p == '\'')
        p++;
      else if (*p == 'L')
        p += 2;
      else
        gdb_assert (0);

      body_start = p;
      for (;;)
        {
          if (p >= end)
            error ("Unmatched single quote.");
          else if (*p == '\'')
            {
              if (p == body_start)
                error ("A character constant must contain at least one "
                       "character.");
              p++;
              break;
            }
          else if (*p == '\\')
            {
              p++;
              parse_escape (&p);
            }
          else
            p++;
        }

      set_token (tok, tok_start, p);
      return 1;
    }
  else
    return 0;
}


/* If the text starting at P going up to (but not including) END
   starts with a string literal, set *TOK to point to that string
   literal, and return 1.  Otherwise, return zero.  Signal an error if
   it contains a malformed or incomplete string literal.  */
static int
get_string_literal (struct macro_buffer *tok, char *p, char *end)
{
  if ((p + 1 <= end
       && *p == '\"')
      || (p + 2 <= end
          && p[0] == 'L'
          && p[1] == '\"'))
    {
      char *tok_start = p;

      if (*p == '\"')
        p++;
      else if (*p == 'L')
        p += 2;
      else
        gdb_assert (0);

      for (;;)
        {
          if (p >= end)
            error ("Unterminated string in expression.");
          else if (*p == '\"')
            {
              p++;
              break;
            }
          else if (*p == '\n')
            error ("Newline characters may not appear in string "
                   "constants.");
          else if (*p == '\\')
            {
              p++;
              parse_escape (&p);
            }
          else
            p++;
        }

      set_token (tok, tok_start, p);
      return 1;
    }
  else
    return 0;
}


static int
get_punctuator (struct macro_buffer *tok, char *p, char *end)
{
  /* Here, speed is much less important than correctness and clarity.  */

  /* ISO/IEC 9899:1999 (E)  Section 6.4.6  Paragraph 1  */
  static const char * const punctuators[] = {
    "[", "]", "(", ")", "{", "}", ".", "->", 
    "++", "--", "&", "*", "+", "-", "~", "!",
    "/", "%", "<<", ">>", "<", ">", "<=", ">=", "==", "!=", 
    "^", "|", "&&", "||",
    "?", ":", ";", "...",
    "=", "*=", "/=", "%=", "+=", "-=", "<<=", ">>=", "&=", "^=", "|=",
    ",", "#", "##",
    "<:", ":>", "<%", "%>", "%:", "%:%:",
    0
  };

  int i;

  if (p + 1 <= end)
    {
      for (i = 0; punctuators[i]; i++)
        {
          const char *punctuator = punctuators[i];

          if (p[0] == punctuator[0])
            {
              int len = strlen (punctuator);

              if (p + len <= end
                  && ! memcmp (p, punctuator, len))
                {
                  set_token (tok, p, p + len);
                  return 1;
                }
            }
        }
    }

  return 0;
}


/* Peel the next preprocessor token off of SRC, and put it in TOK.
   Mutate TOK to refer to the first token in SRC, and mutate SRC to
   refer to the text after that token.  SRC must be a shared buffer;
   the resulting TOK will be shared, pointing into the same string SRC
   does.  Initialize TOK's last_token field.  Return non-zero if we
   succeed, or 0 if we didn't find any more tokens in SRC.  */
static int
get_token (struct macro_buffer *tok,
           struct macro_buffer *src)
{
  char *p = src->text;
  char *end = p + src->len;

  gdb_assert (src->shared);

  /* From the ISO C standard, ISO/IEC 9899:1999 (E), section 6.4:

     preprocessing-token: 
         header-name
         identifier
         pp-number
         character-constant
         string-literal
         punctuator
         each non-white-space character that cannot be one of the above

     We don't have to deal with header-name tokens, since those can
     only occur after a #include, which we will never see.  */

  while (p < end)
    if (is_whitespace (*p))
      p++;
    else if (get_comment (tok, p, end))
      p += tok->len;
    else if (get_pp_number (tok, p, end)
             || get_character_constant (tok, p, end)
             || get_string_literal (tok, p, end)
             /* Note: the grammar in the standard seems to be
                ambiguous: L'x' can be either a wide character
                constant, or an identifier followed by a normal
                character constant.  By trying `get_identifier' after
                we try get_character_constant and get_string_literal,
                we give the wide character syntax precedence.  Now,
                since GDB doesn't handle wide character constants
                anyway, is this the right thing to do?  */
             || get_identifier (tok, p, end)
             || get_punctuator (tok, p, end))
      {
        /* How many characters did we consume, including whitespace?  */
        int consumed = p - src->text + tok->len;
        src->text += consumed;
        src->len -= consumed;
        return 1;
      }
    else 
      {
        /* We have found a "non-whitespace character that cannot be
           one of the above."  Make a token out of it.  */
        int consumed;

        set_token (tok, p, p + 1);
        consumed = p - src->text + tok->len;
        src->text += consumed;
        src->len -= consumed;
        return 1;
      }

  return 0;
}



/* Appending token strings, with and without splicing  */


/* Append the macro buffer SRC to the end of DEST, and ensure that
   doing so doesn't splice the token at the end of SRC with the token
   at the beginning of DEST.  SRC and DEST must have their last_token
   fields set.  Upon return, DEST's last_token field is set correctly.

   For example:

   If DEST is "(" and SRC is "y", then we can return with
   DEST set to "(y" --- we've simply appended the two buffers.

   However, if DEST is "x" and SRC is "y", then we must not return
   with DEST set to "xy" --- that would splice the two tokens "x" and
   "y" together to make a single token "xy".  However, it would be
   fine to return with DEST set to "x y".  Similarly, "<" and "<" must
   yield "< <", not "<<", etc.  */
static void
append_tokens_without_splicing (struct macro_buffer *dest,
                                struct macro_buffer *src)
{
  int original_dest_len = dest->len;
  struct macro_buffer dest_tail, new_token;

  gdb_assert (src->last_token != -1);
  gdb_assert (dest->last_token != -1);
  
  /* First, just try appending the two, and call get_token to see if
     we got a splice.  */
  appendmem (dest, src->text, src->len);

  /* If DEST originally had no token abutting its end, then we can't
     have spliced anything, so we're done.  */
  if (dest->last_token == original_dest_len)
    {
      dest->last_token = original_dest_len + src->last_token;
      return;
    }

  /* Set DEST_TAIL to point to the last token in DEST, followed by
     all the stuff we just appended.  */
  init_shared_buffer (&dest_tail,
                      dest->text + dest->last_token,
                      dest->len - dest->last_token);

  /* Re-parse DEST's last token.  We know that DEST used to contain
     at least one token, so if it doesn't contain any after the
     append, then we must have spliced "/" and "*" or "/" and "/" to
     make a comment start.  (Just for the record, I got this right
     the first time.  This is not a bug fix.)  */
  if (get_token (&new_token, &dest_tail)
      && (new_token.text + new_token.len
          == dest->text + original_dest_len))
    {
      /* No splice, so we're done.  */
      dest->last_token = original_dest_len + src->last_token;
      return;
    }

  /* Okay, a simple append caused a splice.  Let's chop dest back to
     its original length and try again, but separate the texts with a
     space.  */
  dest->len = original_dest_len;
  appendc (dest, ' ');
  appendmem (dest, src->text, src->len);

  init_shared_buffer (&dest_tail,
                      dest->text + dest->last_token,
                      dest->len - dest->last_token);

  /* Try to re-parse DEST's last token, as above.  */
  if (get_token (&new_token, &dest_tail)
      && (new_token.text + new_token.len
          == dest->text + original_dest_len))
    {
      /* No splice, so we're done.  */
      dest->last_token = original_dest_len + 1 + src->last_token;
      return;
    }

  /* As far as I know, there's no case where inserting a space isn't
     enough to prevent a splice.  */
  internal_error (__FILE__, __LINE__,
                  "unable to avoid splicing tokens during macro expansion");
}



/* Expanding macros!  */


/* A singly-linked list of the names of the macros we are currently 
   expanding --- for detecting expansion loops.  */
struct macro_name_list {
  const char *name;
  struct macro_name_list *next;
};


/* Return non-zero if we are currently expanding the macro named NAME,
   according to LIST; otherwise, return zero.

   You know, it would be possible to get rid of all the NO_LOOP
   arguments to these functions by simply generating a new lookup
   function and baton which refuses to find the definition for a
   particular macro, and otherwise delegates the decision to another
   function/baton pair.  But that makes the linked list of excluded
   macros chained through untyped baton pointers, which will make it
   harder to debug.  :( */
static int
currently_rescanning (struct macro_name_list *list, const char *name)
{
  for (; list; list = list->next)
    if (strcmp (name, list->name) == 0)
      return 1;

  return 0;
}


/* Gather the arguments to a macro expansion.

   NAME is the name of the macro being invoked.  (It's only used for
   printing error messages.)

   Assume that SRC is the text of the macro invocation immediately
   following the macro name.  For example, if we're processing the
   text foo(bar, baz), then NAME would be foo and SRC will be (bar,
   baz).

   If SRC doesn't start with an open paren ( token at all, return
   zero, leave SRC unchanged, and don't set *ARGC_P to anything.

   If SRC doesn't contain a properly terminated argument list, then
   raise an error.

   Otherwise, return a pointer to the first element of an array of
   macro buffers referring to the argument texts, and set *ARGC_P to
   the number of arguments we found --- the number of elements in the
   array.  The macro buffers share their text with SRC, and their
   last_token fields are initialized.  The array is allocated with
   xmalloc, and the caller is responsible for freeing it.

   NOTE WELL: if SRC starts with a open paren ( token followed
   immediately by a close paren ) token (e.g., the invocation looks
   like "foo()"), we treat that as one argument, which happens to be
   the empty list of tokens.  The caller should keep in mind that such
   a sequence of tokens is a valid way to invoke one-parameter
   function-like macros, but also a valid way to invoke zero-parameter
   function-like macros.  Eeew.

   Consume the tokens from SRC; after this call, SRC contains the text
   following the invocation.  */

static struct macro_buffer *
gather_arguments (const char *name, struct macro_buffer *src, int *argc_p)
{
  struct macro_buffer tok;
  int args_len, args_size;
  struct macro_buffer *args = NULL;
  struct cleanup *back_to = make_cleanup (free_current_contents, &args);

  /* Does SRC start with an opening paren token?  Read from a copy of
     SRC, so SRC itself is unaffected if we don't find an opening
     paren.  */
  {
    struct macro_buffer temp;
    init_shared_buffer (&temp, src->text, src->len);

    if (! get_token (&tok, &temp)
        || tok.len != 1
        || tok.text[0] != '(')
      {
        discard_cleanups (back_to);
        return 0;
      }
  }

  /* Consume SRC's opening paren.  */
  get_token (&tok, src);

  args_len = 0;
  args_size = 1;                /* small for initial testing */
  args = (struct macro_buffer *) xmalloc (sizeof (*args) * args_size);

  for (;;)
    {
      struct macro_buffer *arg;
      int depth;

      /* Make sure we have room for the next argument.  */
      if (args_len >= args_size)
        {
          args_size *= 2;
          args = xrealloc (args, sizeof (*args) * args_size);
        }

      /* Initialize the next argument.  */
      arg = &args[args_len++];
      set_token (arg, src->text, src->text);

      /* Gather the argument's tokens.  */
      depth = 0;
      for (;;)
        {
          char *start = src->text;

          if (! get_token (&tok, src))
            error ("Malformed argument list for macro `%s'.", name);
      
          /* Is tok an opening paren?  */
          if (tok.len == 1 && tok.text[0] == '(')
            depth++;

          /* Is tok is a closing paren?  */
          else if (tok.len == 1 && tok.text[0] == ')')
            {
              /* If it's a closing paren at the top level, then that's
                 the end of the argument list.  */
              if (depth == 0)
                {
                  discard_cleanups (back_to);
                  *argc_p = args_len;
                  return args;
                }

              depth--;
            }

          /* If tok is a comma at top level, then that's the end of
             the current argument.  */
          else if (tok.len == 1 && tok.text[0] == ',' && depth == 0)
            break;

          /* Extend the current argument to enclose this token.  If
             this is the current argument's first token, leave out any
             leading whitespace, just for aesthetics.  */
          if (arg->len == 0)
            {
              arg->text = tok.text;
              arg->len = tok.len;
              arg->last_token = 0;
            }
          else
            {
              arg->len = (tok.text + tok.len) - arg->text;
              arg->last_token = tok.text - arg->text;
            }
        }
    }
}


/* The `expand' and `substitute_args' functions both invoke `scan'
   recursively, so we need a forward declaration somewhere.  */
static void scan (struct macro_buffer *dest,
                  struct macro_buffer *src,
                  struct macro_name_list *no_loop,
                  macro_lookup_ftype *lookup_func,
                  void *lookup_baton);


/* Given the macro definition DEF, being invoked with the actual
   arguments given by ARGC and ARGV, substitute the arguments into the
   replacement list, and store the result in DEST.

   If it is necessary to expand macro invocations in one of the
   arguments, use LOOKUP_FUNC and LOOKUP_BATON to find the macro
   definitions, and don't expand invocations of the macros listed in
   NO_LOOP.  */
static void
substitute_args (struct macro_buffer *dest, 
                 struct macro_definition *def,
                 int argc, struct macro_buffer *argv,
                 struct macro_name_list *no_loop,
                 macro_lookup_ftype *lookup_func,
                 void *lookup_baton)
{
  /* A macro buffer for the macro's replacement list.  */
  struct macro_buffer replacement_list;

  init_shared_buffer (&replacement_list, (char *) def->replacement,
                      strlen (def->replacement));

  gdb_assert (dest->len == 0);
  dest->last_token = 0;

  for (;;)
    {
      struct macro_buffer tok;
      char *original_rl_start = replacement_list.text;
      int substituted = 0;
      
      /* Find the next token in the replacement list.  */
      if (! get_token (&tok, &replacement_list))
        break;

      /* Just for aesthetics.  If we skipped some whitespace, copy
         that to DEST.  */
      if (tok.text > original_rl_start)
        {
          appendmem (dest, original_rl_start, tok.text - original_rl_start);
          dest->last_token = dest->len;
        }

      /* Is this token the stringification operator?  */
      if (tok.len == 1
          && tok.text[0] == '#')
        error ("Stringification is not implemented yet.");

      /* Is this token the splicing operator?  */
      if (tok.len == 2
          && tok.text[0] == '#'
          && tok.text[1] == '#')
        error ("Token splicing is not implemented yet.");

      /* Is this token an identifier?  */
      if (tok.is_identifier)
        {
          int i;

          /* Is it the magic varargs parameter?  */
          if (tok.len == 11
              && ! memcmp (tok.text, "__VA_ARGS__", 11))
            error ("Variable-arity macros not implemented yet.");

          /* Is it one of the parameters?  */
          for (i = 0; i < def->argc; i++)
            if (tok.len == strlen (def->argv[i])
                && ! memcmp (tok.text, def->argv[i], tok.len))
              {
                struct macro_buffer arg_src;

                /* Expand any macro invocations in the argument text,
                   and append the result to dest.  Remember that scan
                   mutates its source, so we need to scan a new buffer
                   referring to the argument's text, not the argument
                   itself.  */
                init_shared_buffer (&arg_src, argv[i].text, argv[i].len);
                scan (dest, &arg_src, no_loop, lookup_func, lookup_baton);
                substituted = 1;
                break;
              }
        }

      /* If it wasn't a parameter, then just copy it across.  */
      if (! substituted)
        append_tokens_without_splicing (dest, &tok);
    }
}


/* Expand a call to a macro named ID, whose definition is DEF.  Append
   its expansion to DEST.  SRC is the input text following the ID
   token.  We are currently rescanning the expansions of the macros
   named in NO_LOOP; don't re-expand them.  Use LOOKUP_FUNC and
   LOOKUP_BATON to find definitions for any nested macro references.  

   Return 1 if we decided to expand it, zero otherwise.  (If it's a
   function-like macro name that isn't followed by an argument list,
   we don't expand it.)  If we return zero, leave SRC unchanged.  */
static int
expand (const char *id,
        struct macro_definition *def, 
        struct macro_buffer *dest,
        struct macro_buffer *src,
        struct macro_name_list *no_loop,
        macro_lookup_ftype *lookup_func,
        void *lookup_baton)
{
  struct macro_name_list new_no_loop;

  /* Create a new node to be added to the front of the no-expand list.
     This list is appropriate for re-scanning replacement lists, but
     it is *not* appropriate for scanning macro arguments; invocations
     of the macro whose arguments we are gathering *do* get expanded
     there.  */
  new_no_loop.name = id;
  new_no_loop.next = no_loop;

  /* What kind of macro are we expanding?  */
  if (def->kind == macro_object_like)
    {
      struct macro_buffer replacement_list;

      init_shared_buffer (&replacement_list, (char *) def->replacement,
                          strlen (def->replacement));

      scan (dest, &replacement_list, &new_no_loop, lookup_func, lookup_baton);
      return 1;
    }
  else if (def->kind == macro_function_like)
    {
      struct cleanup *back_to = make_cleanup (null_cleanup, 0);
      int argc;
      struct macro_buffer *argv = NULL;
      struct macro_buffer substituted;
      struct macro_buffer substituted_src;

      if (def->argc >= 1
          && strcmp (def->argv[def->argc - 1], "...") == 0)
        error ("Varargs macros not implemented yet.");

      make_cleanup (free_current_contents, &argv);
      argv = gather_arguments (id, src, &argc);

      /* If we couldn't find any argument list, then we don't expand
         this macro.  */
      if (! argv)
        {
          do_cleanups (back_to);
          return 0;
        }

      /* Check that we're passing an acceptable number of arguments for
         this macro.  */
      if (argc != def->argc)
        {
          /* Remember that a sequence of tokens like "foo()" is a
             valid invocation of a macro expecting either zero or one
             arguments.  */
          if (! (argc == 1
                 && argv[0].len == 0
                 && def->argc == 0))
            error ("Wrong number of arguments to macro `%s' "
                   "(expected %d, got %d).",
                   id, def->argc, argc);
        }

      /* Note that we don't expand macro invocations in the arguments
         yet --- we let subst_args take care of that.  Parameters that
         appear as operands of the stringifying operator "#" or the
         splicing operator "##" don't get macro references expanded,
         so we can't really tell whether it's appropriate to macro-
         expand an argument until we see how it's being used.  */
      init_buffer (&substituted, 0);
      make_cleanup (cleanup_macro_buffer, &substituted);
      substitute_args (&substituted, def, argc, argv, no_loop,
                       lookup_func, lookup_baton);

      /* Now `substituted' is the macro's replacement list, with all
         argument values substituted into it properly.  Re-scan it for
         macro references, but don't expand invocations of this macro.

         We create a new buffer, `substituted_src', which points into
         `substituted', and scan that.  We can't scan `substituted'
         itself, since the tokenization process moves the buffer's
         text pointer around, and we still need to be able to find
         `substituted's original text buffer after scanning it so we
         can free it.  */
      init_shared_buffer (&substituted_src, substituted.text, substituted.len);
      scan (dest, &substituted_src, &new_no_loop, lookup_func, lookup_baton);

      do_cleanups (back_to);

      return 1;
    }
  else
    internal_error (__FILE__, __LINE__, "bad macro definition kind");
}


/* If the single token in SRC_FIRST followed by the tokens in SRC_REST
   constitute a macro invokation not forbidden in NO_LOOP, append its
   expansion to DEST and return non-zero.  Otherwise, return zero, and
   leave DEST unchanged.

   SRC_FIRST and SRC_REST must be shared buffers; DEST must not be one.
   SRC_FIRST must be a string built by get_token.  */
static int
maybe_expand (struct macro_buffer *dest,
              struct macro_buffer *src_first,
              struct macro_buffer *src_rest,
              struct macro_name_list *no_loop,
              macro_lookup_ftype *lookup_func,
              void *lookup_baton)
{
  gdb_assert (src_first->shared);
  gdb_assert (src_rest->shared);
  gdb_assert (! dest->shared);

  /* Is this token an identifier?  */
  if (src_first->is_identifier)
    {
      /* Make a null-terminated copy of it, since that's what our
         lookup function expects.  */
      char *id = xmalloc (src_first->len + 1);
      struct cleanup *back_to = make_cleanup (xfree, id);
      memcpy (id, src_first->text, src_first->len);
      id[src_first->len] = 0;
          
      /* If we're currently re-scanning the result of expanding
         this macro, don't expand it again.  */
      if (! currently_rescanning (no_loop, id))
        {
          /* Does this identifier have a macro definition in scope?  */
          struct macro_definition *def = lookup_func (id, lookup_baton);

          if (def && expand (id, def, dest, src_rest, no_loop,
                             lookup_func, lookup_baton))
            {
              do_cleanups (back_to);
              return 1;
            }
        }

      do_cleanups (back_to);
    }

  return 0;
}


/* Expand macro references in SRC, appending the results to DEST.
   Assume we are re-scanning the result of expanding the macros named
   in NO_LOOP, and don't try to re-expand references to them.

   SRC must be a shared buffer; DEST must not be one.  */
static void
scan (struct macro_buffer *dest,
      struct macro_buffer *src,
      struct macro_name_list *no_loop,
      macro_lookup_ftype *lookup_func,
      void *lookup_baton)
{
  gdb_assert (src->shared);
  gdb_assert (! dest->shared);

  for (;;)
    {
      struct macro_buffer tok;
      char *original_src_start = src->text;

      /* Find the next token in SRC.  */
      if (! get_token (&tok, src))
        break;

      /* Just for aesthetics.  If we skipped some whitespace, copy
         that to DEST.  */
      if (tok.text > original_src_start)
        {
          appendmem (dest, original_src_start, tok.text - original_src_start);
          dest->last_token = dest->len;
        }

      if (! maybe_expand (dest, &tok, src, no_loop, lookup_func, lookup_baton))
        /* We didn't end up expanding tok as a macro reference, so
           simply append it to dest.  */
        append_tokens_without_splicing (dest, &tok);
    }

  /* Just for aesthetics.  If there was any trailing whitespace in
     src, copy it to dest.  */
  if (src->len)
    {
      appendmem (dest, src->text, src->len);
      dest->last_token = dest->len;
    }
}


char *
macro_expand (const char *source,
              macro_lookup_ftype *lookup_func,
              void *lookup_func_baton)
{
  struct macro_buffer src, dest;
  struct cleanup *back_to;

  init_shared_buffer (&src, (char *) source, strlen (source));

  init_buffer (&dest, 0);
  dest.last_token = 0;
  back_to = make_cleanup (cleanup_macro_buffer, &dest);

  scan (&dest, &src, 0, lookup_func, lookup_func_baton);

  appendc (&dest, '\0');

  discard_cleanups (back_to);
  return dest.text;
}


char *
macro_expand_once (const char *source,
                   macro_lookup_ftype *lookup_func,
                   void *lookup_func_baton)
{
  error ("Expand-once not implemented yet.");
}


char *
macro_expand_next (char **lexptr,
                   macro_lookup_ftype *lookup_func,
                   void *lookup_baton)
{
  struct macro_buffer src, dest, tok;
  struct cleanup *back_to;

  /* Set up SRC to refer to the input text, pointed to by *lexptr.  */
  init_shared_buffer (&src, *lexptr, strlen (*lexptr));

  /* Set up DEST to receive the expansion, if there is one.  */
  init_buffer (&dest, 0);
  dest.last_token = 0;
  back_to = make_cleanup (cleanup_macro_buffer, &dest);

  /* Get the text's first preprocessing token.  */
  if (! get_token (&tok, &src))
    {
      do_cleanups (back_to);
      return 0;
    }

  /* If it's a macro invocation, expand it.  */
  if (maybe_expand (&dest, &tok, &src, 0, lookup_func, lookup_baton))
    {
      /* It was a macro invocation!  Package up the expansion as a
         null-terminated string and return it.  Set *lexptr to the
         start of the next token in the input.  */
      appendc (&dest, '\0');
      discard_cleanups (back_to);
      *lexptr = src.text;
      return dest.text;
    }
  else
    {
      /* It wasn't a macro invocation.  */
      do_cleanups (back_to);
      return 0;
    }
}

/* Input routines.
   Copyright (C) 1989-1998, 2002-2004 Free Software Foundation, Inc.
   Written by Douglas C. Schmidt <schmidt@ics.uci.edu>
   and Bruno Haible <bruno@clisp.org>.

   This file is part of GNU GPERF.

   GNU GPERF is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GNU GPERF is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; see the file COPYING.
   If not, write to the Free Software Foundation, Inc.,
   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.  */

/* Specification. */
#include "input.h"

#include <stdio.h>
#include <stdlib.h> /* declares exit() */
#include <string.h> /* declares strncpy(), strchr() */
#include <limits.h> /* defines UCHAR_MAX etc. */
#include "options.h"
#include "getline.h"

Input::Input (FILE *stream, Keyword_Factory *keyword_factory)
  : _stream (stream), _factory (keyword_factory)
{
}

/* Returns a pretty representation of the input file name, for error and
   warning messages.  */
static const char *
pretty_input_file_name ()
{
  if (option.get_input_file_name ())
    return option.get_input_file_name ();
  else
    return "(standard input)";
}

/* Returns true if the given line contains a "%DECL" declaration.  */
static bool
is_declaration (const char *line, const char *line_end, unsigned int lineno,
                const char *decl)
{
  /* Skip '%'.  */
  line++;

  /* Skip DECL.  */
  for (const char *d = decl; *d; d++)
    {
      if (!(line < line_end))
        return false;
      if (!(*line == *d || (*d == '-' && *line == '_')))
        return false;
      line++;
    }
  if (line < line_end
      && ((*line >= 'A' && *line <= 'Z')
          || (*line >= 'a' && *line <= 'z')
          || *line == '-' || *line == '_'))
    return false;

  /* OK, found DECL.  */

  /* Skip whitespace.  */
  while (line < line_end && (*line == ' ' || *line == '\t'))
    line++;

  /* Expect end of line.  */
  if (line < line_end && *line != '\n')
    {
      fprintf (stderr, "%s:%u: junk after declaration\n",
               pretty_input_file_name (), lineno);
      exit (1);
    }

  return true;
}

/* Tests if the given line contains a "%DECL=ARG" declaration.
   If yes, it sets *ARGP to the argument, and returns true.
   Otherwise, it returns false.  */
static bool
is_declaration_with_arg (const char *line, const char *line_end,
                         unsigned int lineno,
                         const char *decl, char **argp)
{
  /* Skip '%'.  */
  line++;

  /* Skip DECL.  */
  for (const char *d = decl; *d; d++)
    {
      if (!(line < line_end))
        return false;
      if (!(*line == *d || (*d == '-' && *line == '_')))
        return false;
      line++;
    }
  if (line < line_end
      && ((*line >= 'A' && *line <= 'Z')
          || (*line >= 'a' && *line <= 'z')
          || *line == '-' || *line == '_'))
    return false;

  /* OK, found DECL.  */

  /* Skip '='.  */
  if (!(line < line_end && *line == '='))
    {
      fprintf (stderr, "%s:%u: missing argument in %%%s=ARG declaration.\n",
               pretty_input_file_name (), lineno, decl);
      exit (1);
    }
  line++;

  /* The next word is the argument.  */
  char *arg = new char[line_end - line + 1];
  char *p = arg;
  while (line < line_end && !(*line == ' ' || *line == '\t' || *line == '\n'))
    *p++ = *line++;
  *p = '\0';

  /* Skip whitespace.  */
  while (line < line_end && (*line == ' ' || *line == '\t'))
    line++;

  /* Expect end of line.  */
  if (line < line_end && *line != '\n')
    {
      fprintf (stderr, "%s:%u: junk after declaration\n",
               pretty_input_file_name (), lineno);
      exit (1);
    }

  *argp = arg;
  return true;
}

/* Tests if the given line contains a "%define DECL ARG" declaration.
   If yes, it sets *ARGP to the argument, and returns true.
   Otherwise, it returns false.  */
static bool
is_define_declaration (const char *line, const char *line_end,
                       unsigned int lineno,
                       const char *decl, char **argp)
{
  /* Skip '%'.  */
  line++;

  /* Skip "define".  */
  {
    for (const char *d = "define"; *d; d++)
      {
        if (!(line < line_end))
          return false;
        if (!(*line == *d))
          return false;
        line++;
      }
    if (!(line < line_end && (*line == ' ' || *line == '\t')))
      return false;
  }

  /* Skip whitespace.  */
  while (line < line_end && (*line == ' ' || *line == '\t'))
    line++;

  /* Skip DECL.  */
  for (const char *d = decl; *d; d++)
    {
      if (!(line < line_end))
        return false;
      if (!(*line == *d || (*d == '-' && *line == '_')))
        return false;
      line++;
    }
  if (line < line_end
      && ((*line >= 'A' && *line <= 'Z')
          || (*line >= 'a' && *line <= 'z')
          || *line == '-' || *line == '_'))
    return false;

  /* OK, found DECL.  */

  /* Skip whitespace.  */
  if (!(line < line_end && (*line == ' ' || *line == '\t')))
    {
      fprintf (stderr, "%s:%u:"
               " missing argument in %%define %s ARG declaration.\n",
               pretty_input_file_name (), lineno, decl);
      exit (1);
    }
  do
    line++;
  while (line < line_end && (*line == ' ' || *line == '\t'));

  /* The next word is the argument.  */
  char *arg = new char[line_end - line + 1];
  char *p = arg;
  while (line < line_end && !(*line == ' ' || *line == '\t' || *line == '\n'))
    *p++ = *line++;
  *p = '\0';

  /* Skip whitespace.  */
  while (line < line_end && (*line == ' ' || *line == '\t'))
    line++;

  /* Expect end of line.  */
  if (line < line_end && *line != '\n')
    {
      fprintf (stderr, "%s:%u: junk after declaration\n",
               pretty_input_file_name (), lineno);
      exit (1);
    }

  *argp = arg;
  return true;
}

/* Reads the entire input file.  */
void
Input::read_input ()
{
  /* The input file has the following structure:
        DECLARATIONS
        %%
        KEYWORDS
        %%
        ADDITIONAL_CODE
     Since the DECLARATIONS and the ADDITIONAL_CODE sections are optional,
     we have to read the entire file in the case there is only one %%
     separator line, in order to determine whether the structure is
        DECLARATIONS
        %%
        KEYWORDS
     or
        KEYWORDS
        %%
        ADDITIONAL_CODE
     When the option -t is given or when the first section contains
     declaration lines starting with %, we go for the first interpretation,
     otherwise for the second interpretation.  */

  char *input = NULL;
  size_t input_size = 0;
  int input_length = get_delim (&input, &input_size, EOF, _stream);
  if (input_length < 0)
    {
      if (ferror (_stream))
        fprintf (stderr, "%s: error while reading input file\n",
                 pretty_input_file_name ());
      else
        fprintf (stderr, "%s: The input file is empty!\n",
                 pretty_input_file_name ());
      exit (1);
    }

  /* We use input_end as a limit, in order to cope with NUL bytes in the
     input.  But note that one trailing NUL byte has been added after
     input_end, for convenience.  */
  char *input_end = input + input_length;

  const char *declarations;
  const char *declarations_end;
  const char *keywords;
  const char *keywords_end;
  unsigned int keywords_lineno;

  /* Break up the input into the three sections.  */
  {
    const char *separator[2] = { NULL, NULL };
    unsigned int separator_lineno[2] = { 0, 0 };
    int separators = 0;
    {
      unsigned int lineno = 1;
      for (const char *p = input; p < input_end; )
        {
          if (p[0] == '%' && p[1] == '%')
            {
              separator[separators] = p;
              separator_lineno[separators] = lineno;
              if (++separators == 2)
                break;
            }
          lineno++;
          p = (const char *) memchr (p, '\n', input_end - p);
          if (p != NULL)
            p++;
          else
            p = input_end;
        }
    }

    bool has_declarations;
    if (separators == 1)
      {
        if (option[TYPE])
          has_declarations = true;
        else
          {
            has_declarations = false;
            for (const char *p = input; p < separator[0]; )
              {
                if (p[0] == '%')
                  {
                    has_declarations = true;
                    break;
                  }
                p = (const char *) memchr (p, '\n', separator[0] - p);
                if (p != NULL)
                  p++;
                else
                  p = separator[0];
              }
          }
      }
    else
      has_declarations = (separators > 0);

    if (has_declarations)
      {
        declarations = input;
        declarations_end = separator[0];
        /* Give a warning if the separator line is nonempty.  */
        bool nonempty_line = false;
        const char *p;
        for (p = declarations_end + 2; p < input_end; )
          {
            if (*p == '\n')
              {
                p++;
                break;
              }
            if (!(*p == ' ' || *p == '\t'))
              nonempty_line = true;
            p++;
          }
        if (nonempty_line)
          fprintf (stderr, "%s:%u: warning: junk after %%%% is ignored\n",
                   pretty_input_file_name (), separator_lineno[0]);
        keywords = p;
        keywords_lineno = separator_lineno[0] + 1;
      }
    else
      {
        declarations = NULL;
        declarations_end = NULL;
        keywords = input;
        keywords_lineno = 1;
      }

    if (separators > (has_declarations ? 1 : 0))
      {
        keywords_end = separator[separators-1];
        _verbatim_code = separator[separators-1] + 2;
        _verbatim_code_end = input_end;
        _verbatim_code_lineno = separator_lineno[separators-1];
      }
    else
      {
        keywords_end = input_end;
        _verbatim_code = NULL;
        _verbatim_code_end = NULL;
        _verbatim_code_lineno = 0;
      }
  }

  /* Parse the declarations section.  */

  _verbatim_declarations = NULL;
  _verbatim_declarations_end = NULL;
  _verbatim_declarations_lineno = 0;
  _struct_decl = NULL;
  _struct_decl_lineno = 0;
  _return_type = NULL;
  _struct_tag = NULL;
  {
    unsigned int lineno = 1;
    char *struct_decl = NULL;
    unsigned int *struct_decl_linenos = NULL;
    unsigned int struct_decl_linecount = 0;
    for (const char *line = declarations; line < declarations_end; )
      {
        const char *line_end;
        line_end = (const char *) memchr (line, '\n', declarations_end - line);
        if (line_end != NULL)
          line_end++;
        else
          line_end = declarations_end;

        if (*line == '%')
          {
            if (line[1] == '{')
              {
                /* Handle %{.  */
                if (_verbatim_declarations != NULL)
                  {
                    fprintf (stderr, "%s:%u:\n%s:%u:"
                             " only one %%{...%%} section is allowed\n",
                             pretty_input_file_name (),
                             _verbatim_declarations_lineno,
                             pretty_input_file_name (), lineno);
                    exit (1);
                  }
                _verbatim_declarations = line + 2;
                _verbatim_declarations_lineno = lineno;
              }
            else if (line[1] == '}')
              {
                /* Handle %}.  */
                if (_verbatim_declarations == NULL)
                  {
                    fprintf (stderr, "%s:%u:"
                             " %%} outside of %%{...%%} section\n",
                             pretty_input_file_name (), lineno);
                    exit (1);
                  }
                if (_verbatim_declarations_end != NULL)
                  {
                    fprintf (stderr, "%s:%u:"
                             " %%{...%%} section already closed\n",
                             pretty_input_file_name (), lineno);
                    exit (1);
                  }
                _verbatim_declarations_end = line;
                /* Give a warning if the rest of the line is nonempty.  */
                bool nonempty_line = false;
                const char *q;
                for (q = line + 2; q < line_end; q++)
                  {
                    if (*q == '\n')
                      {
                        q++;
                        break;
                      }
                    if (!(*q == ' ' || *q == '\t'))
                      nonempty_line = true;
                  }
                if (nonempty_line)
                  fprintf (stderr, "%s:%u:"
                           " warning: junk after %%} is ignored\n",
                           pretty_input_file_name (), lineno);
              }
            else if (_verbatim_declarations != NULL
                     && _verbatim_declarations_end == NULL)
              {
                fprintf (stderr, "%s:%u:"
                         " warning: %% directives are ignored"
                         " inside the %%{...%%} section\n",
                         pretty_input_file_name (), lineno);
              }
            else
              {
                char *arg;

                if (is_declaration_with_arg (line, line_end, lineno,
                                             "delimiters", &arg))
                  option.set_delimiters (arg);
                else

                if (is_declaration (line, line_end, lineno, "struct-type"))
                  option.set (TYPE);
                else

                if (is_declaration (line, line_end, lineno, "ignore-case"))
                  option.set (UPPERLOWER);
                else

                if (is_declaration_with_arg (line, line_end, lineno,
                                             "language", &arg))
                  option.set_language (arg);
                else

                if (is_define_declaration (line, line_end, lineno,
                                           "slot-name", &arg))
                  option.set_slot_name (arg);
                else

                if (is_define_declaration (line, line_end, lineno,
                                           "initializer-suffix", &arg))
                  option.set_initializer_suffix (arg);
                else

                if (is_define_declaration (line, line_end, lineno,
                                           "hash-function-name", &arg))
                  option.set_hash_name (arg);
                else

                if (is_define_declaration (line, line_end, lineno,
                                           "lookup-function-name", &arg))
                  option.set_function_name (arg);
                else

                if (is_define_declaration (line, line_end, lineno,
                                           "class-name", &arg))
                  option.set_class_name (arg);
                else

                if (is_declaration (line, line_end, lineno, "7bit"))
                  option.set (SEVENBIT);
                else

                if (is_declaration (line, line_end, lineno, "compare-lengths"))
                  option.set (LENTABLE);
                else

                if (is_declaration (line, line_end, lineno, "compare-strncmp"))
                  option.set (COMP);
                else

                if (is_declaration (line, line_end, lineno, "readonly-tables"))
                  option.set (CONST);
                else

                if (is_declaration (line, line_end, lineno, "enum"))
                  option.set (ENUM);
                else

                if (is_declaration (line, line_end, lineno, "includes"))
                  option.set (INCLUDE);
                else

                if (is_declaration (line, line_end, lineno, "global-table"))
                  option.set (GLOBAL);
                else

                if (is_declaration (line, line_end, lineno, "pic"))
                  option.set (SHAREDLIB);
                else

                if (is_define_declaration (line, line_end, lineno,
                                           "string-pool-name", &arg))
                  option.set_stringpool_name (arg);
                else

                if (is_declaration (line, line_end, lineno, "null-strings"))
                  option.set (NULLSTRINGS);
                else

                if (is_define_declaration (line, line_end, lineno,
                                           "word-array-name", &arg))
                  option.set_wordlist_name (arg);
                else

                if (is_define_declaration (line, line_end, lineno,
                                           "length-table-name", &arg))
                  option.set_lengthtable_name (arg);
                else

                if (is_declaration_with_arg (line, line_end, lineno,
                                             "switch", &arg))
                  {
                    option.set_total_switches (atoi (arg));
                    if (option.get_total_switches () <= 0)
                      {
                        fprintf (stderr, "%s:%u: number of switches %s"
                                 " must be a positive number\n",
                                 pretty_input_file_name (), lineno, arg);
                        exit (1);
                      }
                  }
                else

                if (is_declaration (line, line_end, lineno, "omit-struct-type"))
                  option.set (NOTYPE);
                else

                  {
                    fprintf (stderr, "%s:%u: unrecognized %% directive\n",
                             pretty_input_file_name (), lineno);
                    exit (1);
                  }
              }
          }
        else if (!(_verbatim_declarations != NULL
                   && _verbatim_declarations_end == NULL))
          {
            /* Append the line to struct_decl.  */
            size_t old_len = (struct_decl ? strlen (struct_decl) : 0);
            size_t line_len = line_end - line;
            size_t new_len = old_len + line_len + 1;
            char *new_struct_decl = new char[new_len];
            if (old_len > 0)
              memcpy (new_struct_decl, struct_decl, old_len);
            memcpy (new_struct_decl + old_len, line, line_len);
            new_struct_decl[old_len + line_len] = '\0';
            if (struct_decl)
              delete[] struct_decl;
            struct_decl = new_struct_decl;
            /* Append the lineno to struct_decl_linenos.  */
            unsigned int *new_struct_decl_linenos =
              new unsigned int[struct_decl_linecount + 1];
            if (struct_decl_linecount > 0)
              memcpy (new_struct_decl_linenos, struct_decl_linenos,
                      struct_decl_linecount * sizeof (unsigned int));
            new_struct_decl_linenos[struct_decl_linecount] = lineno;
            if (struct_decl_linenos)
              delete[] struct_decl_linenos;
            struct_decl_linenos = new_struct_decl_linenos;
            /* Increment struct_decl_linecount.  */
            struct_decl_linecount++;
          }
        lineno++;
        line = line_end;
      }
    if (_verbatim_declarations != NULL && _verbatim_declarations_end == NULL)
      {
        fprintf (stderr, "%s:%u: unterminated %%{ section\n",
                 pretty_input_file_name (), _verbatim_declarations_lineno);
        exit (1);
      }

    /* Determine _struct_decl, _return_type, _struct_tag.  */
    if (option[TYPE])
      {
        if (struct_decl)
          {
            /* Drop leading whitespace and comments.  */
            {
              char *p = struct_decl;
              unsigned int *l = struct_decl_linenos;
              for (;;)
                {
                  if (p[0] == ' ' || p[0] == '\t')
                    {
                      p++;
                      continue;
                    }
                  if (p[0] == '\n')
                    {
                      l++;
                      p++;
                      continue;
                    }
                  if (p[0] == '/')
                    {
                      if (p[1] == '*')
                        {
                          /* Skip over ANSI C style comment.  */
                          p += 2;
                          while (p[0] != '\0')
                            {
                              if (p[0] == '*' && p[1] == '/')
                                {
                                  p += 2;
                                  break;
                                }
                              if (p[0] == '\n')
                                l++;
                              p++;
                            }
                          continue;
                        }
                      if (p[1] == '/')
                        {
                          /* Skip over ISO C99 or C++ style comment.  */
                          p += 2;
                          while (p[0] != '\0' && p[0] != '\n')
                            p++;
                          if (p[0] == '\n')
                            {
                              l++;
                              p++;
                            }
                          continue;
                        }
                    }
                  break;
                }
              if (p != struct_decl)
                {
                  size_t len = strlen (p);
                  char *new_struct_decl = new char[len + 1];
                  memcpy (new_struct_decl, p, len + 1);
                  delete[] struct_decl;
                  struct_decl = new_struct_decl;
                }
              _struct_decl_lineno = *l;
            }
            /* Drop trailing whitespace.  */
            for (char *p = struct_decl + strlen (struct_decl); p > struct_decl;)
              if (p[-1] == '\n' || p[-1] == ' ' || p[-1] == '\t')
                *--p = '\0';
              else
                break;
          }
        if (struct_decl == NULL || struct_decl[0] == '\0')
          {
            fprintf (stderr, "%s: missing struct declaration"
                     " for option --struct-type\n",
                     pretty_input_file_name ());
            exit (1);
          }
        {
          /* Ensure trailing semicolon.  */
          size_t old_len = strlen (struct_decl);
          if (struct_decl[old_len - 1] != ';')
            {
              char *new_struct_decl = new char[old_len + 2];
              memcpy (new_struct_decl, struct_decl, old_len);
              new_struct_decl[old_len] = ';';
              new_struct_decl[old_len + 1] = '\0';
              delete[] struct_decl;
              struct_decl = new_struct_decl;
            }
        }
        /* Set _struct_decl to the entire declaration.  */
        _struct_decl = struct_decl;
        /* Set _struct_tag to the naked "struct something".  */
        const char *p;
        for (p = struct_decl; *p && *p != '{' && *p != ';' && *p != '\n'; p++)
          ;
        for (; p > struct_decl;)
          if (p[-1] == '\n' || p[-1] == ' ' || p[-1] == '\t')
            --p;
          else
            break;
        size_t struct_tag_length = p - struct_decl;
        char *struct_tag = new char[struct_tag_length + 1];
        memcpy (struct_tag, struct_decl, struct_tag_length);
        struct_tag[struct_tag_length] = '\0';
        _struct_tag = struct_tag;
        /* The return type of the lookup function is "struct something *".
           No "const" here, because if !option[CONST], some user code might
           want to modify the structure. */
        char *return_type = new char[struct_tag_length + 3];
        memcpy (return_type, struct_decl, struct_tag_length);
        return_type[struct_tag_length] = ' ';
        return_type[struct_tag_length + 1] = '*';
        return_type[struct_tag_length + 2] = '\0';
        _return_type = return_type;
      }

    if (struct_decl_linenos)
      delete[] struct_decl_linenos;
  }

  /* Parse the keywords section.  */
  {
    Keyword_List **list_tail = &_head;
    const char *delimiters = option.get_delimiters ();
    unsigned int lineno = keywords_lineno;
    bool charset_dependent = false;
    for (const char *line = keywords; line < keywords_end; )
      {
        const char *line_end;
        line_end = (const char *) memchr (line, '\n', keywords_end - line);
        if (line_end != NULL)
          line_end++;
        else
          line_end = keywords_end;

        if (line[0] == '#')
          ; /* Comment line.  */
        else if (line[0] == '%')
          {
            fprintf (stderr, "%s:%u:"
                     " declarations are not allowed in the keywords section.\n"
                     "To declare a keyword starting with %%, enclose it in"
                     " double-quotes.\n",
                     pretty_input_file_name (), lineno);
            exit (1);
          }
        else
          {
            /* An input line carrying a keyword.  */
            const char *keyword;
            size_t keyword_length;
            const char *rest;

            if (line[0] == '"')
              {
                /* Parse a string in ANSI C syntax.  */
                char *kp = new char[line_end-line];
                keyword = kp;
                const char *lp = line + 1;

                for (;;)
                  {
                    if (lp == line_end)
                      {
                        fprintf (stderr, "%s:%u: unterminated string\n",
                                 pretty_input_file_name (), lineno);
                        exit (1);
                      }

                    char c = *lp;
                    if (c == '\\')
                      {
                        c = *++lp;
                        switch (c)
                          {
                          case '0': case '1': case '2': case '3':
                          case '4': case '5': case '6': case '7':
                            {
                              int code = 0;
                              int count = 0;
                              while (count < 3 && *lp >= '0' && *lp <= '7')
                                {
                                  code = (code << 3) + (*lp - '0');
                                  lp++;
                                  count++;
                                }
                              if (code > UCHAR_MAX)
                                fprintf (stderr,
                                         "%s:%u: octal escape out of range\n",
                                         pretty_input_file_name (), lineno);
                              *kp = static_cast<char>(code);
                              break;
                            }
                          case 'x':
                            {
                              int code = 0;
                              int count = 0;
                              lp++;
                              while ((*lp >= '0' && *lp <= '9')
                                     || (*lp >= 'A' && *lp <= 'F')
                                     || (*lp >= 'a' && *lp <= 'f'))
                                {
                                  code = (code << 4)
                                         + (*lp >= 'A' && *lp <= 'F'
                                            ? *lp - 'A' + 10 :
                                            *lp >= 'a' && *lp <= 'f'
                                            ? *lp - 'a' + 10 :
                                            *lp - '0');
                                  lp++;
                                  count++;
                                }
                              if (count == 0)
                                fprintf (stderr, "%s:%u: hexadecimal escape"
                                         " without any hex digits\n",
                                         pretty_input_file_name (), lineno);
                              if (code > UCHAR_MAX)
                                fprintf (stderr, "%s:%u: hexadecimal escape"
                                         " out of range\n",
                                         pretty_input_file_name (), lineno);
                              *kp = static_cast<char>(code);
                              break;
                            }
                          case '\\': case '\'': case '"':
                            *kp = c;
                            lp++;
                            charset_dependent = true;
                            break;
                          case 'n':
                            *kp = '\n';
                            lp++;
                            charset_dependent = true;
                            break;
                          case 't':
                            *kp = '\t';
                            lp++;
                            charset_dependent = true;
                            break;
                          case 'r':
                            *kp = '\r';
                            lp++;
                            charset_dependent = true;
                            break;
                          case 'f':
                            *kp = '\f';
                            lp++;
                            charset_dependent = true;
                            break;
                          case 'b':
                            *kp = '\b';
                            lp++;
                            charset_dependent = true;
                            break;
                          case 'a':
                            *kp = '\a';
                            lp++;
                            charset_dependent = true;
                            break;
                          case 'v':
                            *kp = '\v';
                            lp++;
                            charset_dependent = true;
                            break;
                          default:
                            fprintf (stderr, "%s:%u: invalid escape sequence"
                                     " in string\n",
                                     pretty_input_file_name (), lineno);
                            exit (1);
                          }
                      }
                    else if (c == '"')
                      break;
                    else
                      {
                        *kp = c;
                        lp++;
                        charset_dependent = true;
                      }
                    kp++;
                  }
                lp++;
                if (lp < line_end && *lp != '\n')
                  {
                    if (strchr (delimiters, *lp) == NULL)
                      {
                        fprintf (stderr, "%s:%u: string not followed"
                                 " by delimiter\n",
                                 pretty_input_file_name (), lineno);
                        exit (1);
                      }
                    lp++;
                  }
                keyword_length = kp - keyword;
                if (option[TYPE])
                  {
                    char *line_rest = new char[line_end - lp + 1];
                    memcpy (line_rest, lp, line_end - lp);
                    line_rest[line_end - lp -
                              (line_end > lp && line_end[-1] == '\n' ? 1 : 0)]
                      = '\0';
                    rest = line_rest;
                  }
                else
                  rest = empty_string;
              }
            else
              {
                /* Not a string.  Look for the delimiter.  */
                const char *lp = line;
                for (;;)
                  {
                    if (!(lp < line_end && *lp != '\n'))
                      {
                        keyword = line;
                        keyword_length = lp - line;
                        rest = empty_string;
                        break;
                      }
                    if (strchr (delimiters, *lp) != NULL)
                      {
                        keyword = line;
                        keyword_length = lp - line;
                        lp++;
                        if (option[TYPE])
                          {
                            char *line_rest = new char[line_end - lp + 1];
                            memcpy (line_rest, lp, line_end - lp);
                            line_rest[line_end - lp -
                                      (line_end > lp && line_end[-1] == '\n'
                                       ? 1 : 0)]
                              = '\0';
                            rest = line_rest;
                          }
                        else
                          rest = empty_string;
                        break;
                      }
                    lp++;
                  }
                if (keyword_length > 0)
                  charset_dependent = true;
              }

            /* Allocate Keyword and add it to the list.  */
            Keyword *new_kw = _factory->create_keyword (keyword, keyword_length,
                                                        rest);
            new_kw->_lineno = lineno;
            *list_tail = new Keyword_List (new_kw);
            list_tail = &(*list_tail)->rest();
          }

        lineno++;
        line = line_end;
      }
    *list_tail = NULL;

    if (_head == NULL)
      {
        fprintf (stderr, "%s: No keywords in input file!\n",
                 pretty_input_file_name ());
        exit (1);
      }

    _charset_dependent = charset_dependent;
  }

  /* To be freed in the destructor.  */
  _input = input;
  _input_end = input_end;
}

Input::~Input ()
{
  /* Free allocated memory.  */
  delete[] const_cast<char*>(_return_type);
  delete[] const_cast<char*>(_struct_tag);
  delete[] const_cast<char*>(_struct_decl);
  delete[] _input;
}

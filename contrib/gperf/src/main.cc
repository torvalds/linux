/* Driver program for the hash function generator
   Copyright (C) 1989-1998, 2000, 2002-2003 Free Software Foundation, Inc.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "options.h"
#include "input.h"
#include "search.h"
#include "output.h"


/* ------------------------------------------------------------------------- */

/* This Keyword factory produces KeywordExt instances.  */

class KeywordExt_Factory : public Keyword_Factory
{
virtual Keyword *       create_keyword (const char *allchars, int allchars_length,
                                        const char *rest);
};

Keyword *
KeywordExt_Factory::create_keyword (const char *allchars, int allchars_length, const char *rest)
{
  return new KeywordExt (allchars, allchars_length, rest);
}

/* ------------------------------------------------------------------------- */

int
main (int argc, char *argv[])
{
  int exitcode;

  /* Set the Options.  Open the input file and assign stdin to it.  */
  option.parse_options (argc, argv);

  /* Open the input file.  */
  if (option.get_input_file_name ())
    if (!freopen (option.get_input_file_name (), "r", stdin))
      {
        fprintf (stderr, "Cannot open input file '%s'\n",
                 option.get_input_file_name ());
        exit (1);
      }

  {
    /* Initialize the keyword list.  */
    KeywordExt_Factory factory;
    Input inputter (stdin, &factory);
    inputter.read_input ();
    /* We can cast the keyword list to KeywordExt_List* because its list
       elements were created by KeywordExt_Factory.  */
    KeywordExt_List* list = static_cast<KeywordExt_List*>(inputter._head);

    {
      /* Search for a good hash function.  */
      Search searcher (list);
      searcher.optimize ();
      list = searcher._head;

      /* Open the output file.  */
      if (option.get_output_file_name ())
        if (strcmp (option.get_output_file_name (), "-") != 0)
          if (!freopen (option.get_output_file_name (), "w", stdout))
            {
              fprintf (stderr, "Cannot open output file '%s'\n",
                       option.get_output_file_name ());
              exit (1);
            }

      {
        /* Output the hash function code.  */
        Output outputter (searcher._head,
                          inputter._struct_decl,
                          inputter._struct_decl_lineno,
                          inputter._return_type,
                          inputter._struct_tag,
                          inputter._verbatim_declarations,
                          inputter._verbatim_declarations_end,
                          inputter._verbatim_declarations_lineno,
                          inputter._verbatim_code,
                          inputter._verbatim_code_end,
                          inputter._verbatim_code_lineno,
                          inputter._charset_dependent,
                          searcher._total_keys,
                          searcher._max_key_len,
                          searcher._min_key_len,
                          searcher._key_positions,
                          searcher._alpha_inc,
                          searcher._total_duplicates,
                          searcher._alpha_size,
                          searcher._asso_values);
        outputter.output ();

        /* Check for write error on stdout.  */
        exitcode = 0;
        if (fflush (stdout) || ferror (stdout))
          {
            fprintf (stderr, "error while writing output file\n");
            exitcode = 1;
          }

        /* Here we run the Output destructor.  */
      }
      /* Here we run the Search destructor.  */
    }

    /* Also delete the list that was allocated inside Input and reordered
       inside Search.  */
    for (KeywordExt_List *ptr = list; ptr; ptr = ptr->rest())
      {
        KeywordExt *keyword = ptr->first();
        do
          {
            KeywordExt *next_keyword = keyword->_duplicate_link;
            delete[] const_cast<unsigned int *>(keyword->_selchars);
            if (keyword->_rest != empty_string)
              delete[] const_cast<char*>(keyword->_rest);
            if (!(keyword->_allchars >= inputter._input
                  && keyword->_allchars < inputter._input_end))
              delete[] const_cast<char*>(keyword->_allchars);
            delete keyword;
            keyword = next_keyword;
          }
        while (keyword != NULL);
      }
    delete_list (list);

    /* Here we run the Input destructor.  */
  }

  /* Don't use exit() here, it skips the destructors.  */
  return exitcode;
}

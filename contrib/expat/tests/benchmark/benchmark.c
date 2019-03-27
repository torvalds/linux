/*
                            __  __            _
                         ___\ \/ /_ __   __ _| |_
                        / _ \\  /| '_ \ / _` | __|
                       |  __//  \| |_) | (_| | |_
                        \___/_/\_\ .__/ \__,_|\__|
                                 |_| XML parser

   Copyright (c) 1997-2000 Thai Open Source Software Center Ltd
   Copyright (c) 2000-2017 Expat development team
   Licensed under the MIT license:

   Permission is  hereby granted,  free of charge,  to any  person obtaining
   a  copy  of  this  software   and  associated  documentation  files  (the
   "Software"),  to  deal in  the  Software  without restriction,  including
   without  limitation the  rights  to use,  copy,  modify, merge,  publish,
   distribute, sublicense, and/or sell copies of the Software, and to permit
   persons  to whom  the Software  is  furnished to  do so,  subject to  the
   following conditions:

   The above copyright  notice and this permission notice  shall be included
   in all copies or substantial portions of the Software.

   THE  SOFTWARE  IS  PROVIDED  "AS  IS",  WITHOUT  WARRANTY  OF  ANY  KIND,
   EXPRESS  OR IMPLIED,  INCLUDING  BUT  NOT LIMITED  TO  THE WARRANTIES  OF
   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
   NO EVENT SHALL THE AUTHORS OR  COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
   DAMAGES OR  OTHER LIABILITY, WHETHER  IN AN  ACTION OF CONTRACT,  TORT OR
   OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
   USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include "expat.h"

#ifdef XML_LARGE_SIZE
# define XML_FMT_INT_MOD "ll"
#else
# define XML_FMT_INT_MOD "l"
#endif

#ifdef XML_UNICODE_WCHAR_T
# define XML_FMT_STR "ls"
#else
# define XML_FMT_STR "s"
#endif

static void
usage(const char *prog, int rc)
{
  fprintf(stderr,
          "usage: %s [-n] filename bufferSize nr_of_loops\n", prog);
  exit(rc);
}

int main (int argc, char *argv[]) 
{
  XML_Parser  parser;
  char        *XMLBuf, *XMLBufEnd, *XMLBufPtr;
  FILE        *fd;
  struct stat fileAttr;
  int         nrOfLoops, bufferSize, fileSize, i, isFinal;
  int         j = 0, ns = 0;
  clock_t     tstart, tend;
  double      cpuTime = 0.0;

  if (argc > 1) {
    if (argv[1][0] == '-') {
      if (argv[1][1] == 'n' && argv[1][2] == '\0') {
        ns = 1;
        j = 1;
      }
      else
        usage(argv[0], 1);
    }
  }

  if (argc != j + 4)
    usage(argv[0], 1);

  if (stat (argv[j + 1], &fileAttr) != 0) {
    fprintf (stderr, "could not access file '%s'\n", argv[j + 1]);
    return 2;
  }
  
  fd = fopen (argv[j + 1], "r");
  if (!fd) {
    fprintf (stderr, "could not open file '%s'\n", argv[j + 1]);
    exit(2);
  }
  
  bufferSize = atoi (argv[j + 2]);
  nrOfLoops = atoi (argv[j + 3]);
  if (bufferSize <= 0 || nrOfLoops <= 0) {
    fprintf (stderr, 
             "buffer size and nr of loops must be greater than zero.\n");
    exit(3);
  }

  XMLBuf = malloc (fileAttr.st_size);
  fileSize = fread (XMLBuf, sizeof (char), fileAttr.st_size, fd);
  fclose (fd);
  
  if (ns)
    parser = XML_ParserCreateNS(NULL, '!');
  else
    parser = XML_ParserCreate(NULL);

  i = 0;
  XMLBufEnd = XMLBuf + fileSize;
  while (i < nrOfLoops) {
    XMLBufPtr = XMLBuf;
    isFinal = 0;
    tstart = clock();
    do {
      int parseBufferSize = XMLBufEnd - XMLBufPtr;
      if (parseBufferSize <= bufferSize)
        isFinal = 1;
      else
        parseBufferSize = bufferSize;
      if (!XML_Parse (parser, XMLBufPtr, parseBufferSize, isFinal)) {
        fprintf (stderr,
                 "error '%" XML_FMT_STR "' at line %" XML_FMT_INT_MOD   \
                     "u character %" XML_FMT_INT_MOD "u\n",
                 XML_ErrorString (XML_GetErrorCode (parser)),
                 XML_GetCurrentLineNumber (parser),
                 XML_GetCurrentColumnNumber (parser));
        free (XMLBuf);
        XML_ParserFree (parser);
        exit (4);
      }
      XMLBufPtr += bufferSize;
    } while (!isFinal);
    tend = clock();
    cpuTime += ((double) (tend - tstart)) / CLOCKS_PER_SEC;
    XML_ParserReset(parser, NULL);
    i++;
  }

  XML_ParserFree (parser);
  free (XMLBuf);
      
  printf ("%d loops, with buffer size %d. Average time per loop: %f\n", 
          nrOfLoops, bufferSize, cpuTime / (double) nrOfLoops);
  return 0;
}

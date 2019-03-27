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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>

/* Functions close(2) and read(2) */
#if !defined(_WIN32) && !defined(_WIN64)
# include <unistd.h>
#endif

/* Function "read": */
#if defined(_MSC_VER)
# include <io.h>
  /* https://msdn.microsoft.com/en-us/library/wyssk1bs(v=vs.100).aspx */
# define _EXPAT_read          _read
# define _EXPAT_read_count_t  int
# define _EXPAT_read_req_t    unsigned int
#else  /* POSIX */
  /* http://pubs.opengroup.org/onlinepubs/009695399/functions/read.html */
# define _EXPAT_read          read
# define _EXPAT_read_count_t  ssize_t
# define _EXPAT_read_req_t    size_t
#endif

#ifndef S_ISREG
# ifndef S_IFREG
#  define S_IFREG _S_IFREG
# endif
# ifndef S_IFMT
#  define S_IFMT _S_IFMT
# endif
# define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#endif /* not S_ISREG */

#ifndef O_BINARY
# ifdef _O_BINARY
#  define O_BINARY _O_BINARY
# else
#  define O_BINARY 0
# endif
#endif

#include "xmltchar.h"
#include "filemap.h"

int
filemap(const tchar *name,
        void (*processor)(const void *, size_t, const tchar *, void *arg),
        void *arg)
{
  size_t nbytes;
  int fd;
  _EXPAT_read_count_t n;
  struct stat sb;
  void *p;

  fd = topen(name, O_RDONLY|O_BINARY);
  if (fd < 0) {
    tperror(name);
    return 0;
  }
  if (fstat(fd, &sb) < 0) {
    tperror(name);
    close(fd);
    return 0;
  }
  if (!S_ISREG(sb.st_mode)) {
    ftprintf(stderr, T("%s: not a regular file\n"), name);
    close(fd);
    return 0;
  }
  if (sb.st_size > XML_MAX_CHUNK_LEN) {
    close(fd);
    return 2;  /* Cannot be passed to XML_Parse in one go */
  }

  nbytes = sb.st_size;
  /* malloc will return NULL with nbytes == 0, handle files with size 0 */
  if (nbytes == 0) {
    static const char c = '\0';
    processor(&c, 0, name, arg);
    close(fd);
    return 1;
  }
  p = malloc(nbytes);
  if (!p) {
    ftprintf(stderr, T("%s: out of memory\n"), name);
    close(fd);
    return 0;
  }
  n = _EXPAT_read(fd, p, (_EXPAT_read_req_t)nbytes);
  if (n < 0) {
    tperror(name);
    free(p);
    close(fd);
    return 0;
  }
  if (n != (_EXPAT_read_count_t)nbytes) {
    ftprintf(stderr, T("%s: read unexpected number of bytes\n"), name);
    free(p);
    close(fd);
    return 0;
  }
  processor(p, nbytes, name, arg);
  free(p);
  close(fd);
  return 1;
}

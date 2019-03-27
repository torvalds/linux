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
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#ifndef MAP_FILE
#define MAP_FILE 0
#endif

#include "xmltchar.h"
#include "filemap.h"

#ifdef XML_UNICODE_WCHAR_T
# define XML_FMT_STR "ls"
#else
# define XML_FMT_STR "s"
#endif

int
filemap(const tchar *name,
        void (*processor)(const void *, size_t, const tchar *, void *arg),
        void *arg)
{
  int fd;
  size_t nbytes;
  struct stat sb;
  void *p;

  fd = topen(name, O_RDONLY);
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
    close(fd);
    fprintf(stderr, "%" XML_FMT_STR ": not a regular file\n", name);
    return 0;
  }
  if (sb.st_size > XML_MAX_CHUNK_LEN) {
    close(fd);
    return 2;  /* Cannot be passed to XML_Parse in one go */
  }

  nbytes = sb.st_size;
  /* mmap fails for zero length files */
  if (nbytes == 0) {
    static const char c = '\0';
    processor(&c, 0, name, arg);
    close(fd);
    return 1;
  }
  p = (void *)mmap((void *)0, (size_t)nbytes, PROT_READ,
                   MAP_FILE|MAP_PRIVATE, fd, (off_t)0);
  if (p == (void *)-1) {
    tperror(name);
    close(fd);
    return 0;
  }
  processor(p, nbytes, name, arg);
  munmap((void *)p, nbytes);
  close(fd);
  return 1;
}

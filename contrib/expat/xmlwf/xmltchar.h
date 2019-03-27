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

/* Ensures compile-time constants are consistent */
#include "expat_external.h"

#ifdef XML_UNICODE
# ifndef XML_UNICODE_WCHAR_T
#  error xmlwf requires a 16-bit Unicode-compatible wchar_t
# endif
# define _PREPEND_BIG_L(x) L ## x
# define T(x) _PREPEND_BIG_L(x)
# define ftprintf fwprintf
# define tfopen _wfopen
# define fputts fputws
# define puttc putwc
# define tcscmp wcscmp
# define tcscpy wcscpy
# define tcscat wcscat
# define tcschr wcschr
# define tcsrchr wcsrchr
# define tcslen wcslen
# define tperror _wperror
# define topen _wopen
# define tmain wmain
# define tremove _wremove
# define tchar wchar_t
#else /* not XML_UNICODE */
# define T(x) x
# define ftprintf fprintf
# define tfopen fopen
# define fputts fputs
# define puttc putc
# define tcscmp strcmp
# define tcscpy strcpy
# define tcscat strcat
# define tcschr strchr
# define tcsrchr strrchr
# define tcslen strlen
# define tperror perror
# define topen open
# define tmain main
# define tremove remove
# define tchar char
#endif /* not XML_UNICODE */

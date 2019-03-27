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

#include <string.h>
#include "xmlmime.h"

static const char *
getTok(const char **pp)
{
  /* inComment means one level of nesting; inComment+1 means two levels etc */
  enum { inAtom, inString, init, inComment };
  int state = init;
  const char *tokStart = 0;
  for (;;) {
    switch (**pp) {
    case '\0':
      if (state == inAtom)
        return tokStart;
      return 0;
    case ' ':
    case '\r':
    case '\t':
    case '\n':
      if (state == inAtom)
        return tokStart;
      break;
    case '(':
      if (state == inAtom)
        return tokStart;
      if (state != inString)
        state++;
      break;
    case ')':
      if (state > init)
        --state;
      else if (state != inString)
        return 0;
      break;
    case ';':
    case '/':
    case '=':
      if (state == inAtom)
        return tokStart;
      if (state == init)
        return (*pp)++;
      break;
    case '\\':
      ++*pp;
      if (**pp == '\0')
        return 0;
      break;
    case '"':
      switch (state) {
      case inString:
        ++*pp;
        return tokStart;
      case inAtom:
        return tokStart;
      case init:
        tokStart = *pp;
        state = inString;
        break;
      }
      break;
    default:
      if (state == init) {
        tokStart = *pp;
        state = inAtom;
      }
      break;
    }
    ++*pp;
  }
  /* not reached */
}

/* key must be lowercase ASCII */

static int
matchkey(const char *start, const char *end, const char *key)
{
  if (!start)
    return 0;
  for (; start != end; start++, key++)
    if (*start != *key && *start != 'A' + (*key - 'a'))
      return 0;
  return *key == '\0';
}

void
getXMLCharset(const char *buf, char *charset)
{
  const char *next, *p;

  charset[0] = '\0';
  next = buf;
  p = getTok(&next);
  if (matchkey(p, next, "text"))
    strcpy(charset, "us-ascii");
  else if (!matchkey(p, next, "application"))
    return;
  p = getTok(&next);
  if (!p || *p != '/')
    return;
  p = getTok(&next);
#if 0
  if (!matchkey(p, next, "xml") && charset[0] == '\0')
    return;
#endif
  p = getTok(&next);
  while (p) {
    if (*p == ';') {
      p = getTok(&next);
      if (matchkey(p, next, "charset")) {
        p = getTok(&next);
        if (p && *p == '=') {
          p = getTok(&next);
          if (p) {
            char *s = charset;
            if (*p == '"') {
              while (++p != next - 1) {
                if (*p == '\\')
                  ++p;
                if (s == charset + CHARSET_MAX - 1) {
                  charset[0] = '\0';
                  break;
                }
                *s++ = *p;
              }
              *s++ = '\0';
            }
            else {
              if (next - p > CHARSET_MAX - 1)
                break;
              while (p != next)
                *s++ = *p++;
              *s = 0;
              break;
            }
          }
        }
        break;
      }
    }
  else
    p = getTok(&next);
  }
}

#ifdef TEST

#include <stdio.h>

int
main(int argc, char *argv[])
{
  char buf[CHARSET_MAX];
  if (argc <= 1)
    return 1;
  printf("%s\n", argv[1]);
  getXMLCharset(argv[1], buf);
  printf("charset=\"%s\"\n", buf);
  return 0;
}

#endif /* TEST */

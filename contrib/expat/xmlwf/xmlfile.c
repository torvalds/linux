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

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <fcntl.h>

#ifdef _WIN32
#include "winconfig.h"
#elif defined(HAVE_EXPAT_CONFIG_H)
#include <expat_config.h>
#endif /* ndef _WIN32 */

#include "expat.h"
#include "internal.h"  /* for UNUSED_P only */
#include "xmlfile.h"
#include "xmltchar.h"
#include "filemap.h"

#if defined(_MSC_VER)
#include <io.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifndef O_BINARY
#ifdef _O_BINARY
#define O_BINARY _O_BINARY
#else
#define O_BINARY 0
#endif
#endif

#ifdef _DEBUG
#define READ_SIZE 16
#else
#define READ_SIZE (1024*8)
#endif


typedef struct {
  XML_Parser parser;
  int *retPtr;
} PROCESS_ARGS;

static int
processStream(const XML_Char *filename, XML_Parser parser);

static void
reportError(XML_Parser parser, const XML_Char *filename)
{
  enum XML_Error code = XML_GetErrorCode(parser);
  const XML_Char *message = XML_ErrorString(code);
  if (message)
    ftprintf(stdout,
             T("%s")
               T(":%") T(XML_FMT_INT_MOD) T("u")
               T(":%") T(XML_FMT_INT_MOD) T("u")
               T(": %s\n"),
             filename,
             XML_GetErrorLineNumber(parser),
             XML_GetErrorColumnNumber(parser),
             message);
  else
    ftprintf(stderr, T("%s: (unknown message %d)\n"), filename, code);
}
 
/* This implementation will give problems on files larger than INT_MAX. */
static void
processFile(const void *data, size_t size,
            const XML_Char *filename, void *args)
{
  XML_Parser parser = ((PROCESS_ARGS *)args)->parser;
  int *retPtr = ((PROCESS_ARGS *)args)->retPtr;
  if (XML_Parse(parser, (const char *)data, (int)size, 1) == XML_STATUS_ERROR) {
    reportError(parser, filename);
    *retPtr = 0;
  }
  else
    *retPtr = 1;
}

#if defined(_WIN32)

static int
isAsciiLetter(XML_Char c)
{
  return (T('a') <= c && c <= T('z')) || (T('A') <= c && c <= T('Z'));
}

#endif /* _WIN32 */

static const XML_Char *
resolveSystemId(const XML_Char *base, const XML_Char *systemId,
                XML_Char **toFree)
{
  XML_Char *s;
  *toFree = 0;
  if (!base
      || *systemId == T('/')
#if defined(_WIN32)
      || *systemId == T('\\')
      || (isAsciiLetter(systemId[0]) && systemId[1] == T(':'))
#endif
     )
    return systemId;
  *toFree = (XML_Char *)malloc((tcslen(base) + tcslen(systemId) + 2)
                               * sizeof(XML_Char));
  if (!*toFree)
    return systemId;
  tcscpy(*toFree, base);
  s = *toFree;
  if (tcsrchr(s, T('/')))
    s = tcsrchr(s, T('/')) + 1;
#if defined(_WIN32)
  if (tcsrchr(s, T('\\')))
    s = tcsrchr(s, T('\\')) + 1;
#endif
  tcscpy(s, systemId);
  return *toFree;
}

static int
externalEntityRefFilemap(XML_Parser parser,
                         const XML_Char *context,
                         const XML_Char *base,
                         const XML_Char *systemId,
                         const XML_Char *UNUSED_P(publicId))
{
  int result;
  XML_Char *s;
  const XML_Char *filename;
  XML_Parser entParser = XML_ExternalEntityParserCreate(parser, context, 0);
  int filemapRes;
  PROCESS_ARGS args;
  args.retPtr = &result;
  args.parser = entParser;
  filename = resolveSystemId(base, systemId, &s);
  XML_SetBase(entParser, filename);
  filemapRes = filemap(filename, processFile, &args);
  switch (filemapRes) {
  case 0:
    result = 0;
    break;
  case 2:
    ftprintf(stderr, T("%s: file too large for memory-mapping")
        T(", switching to streaming\n"), filename);
    result = processStream(filename, entParser);
    break;
  }
  free(s);
  XML_ParserFree(entParser);
  return result;
}

static int
processStream(const XML_Char *filename, XML_Parser parser)
{
  /* passing NULL for filename means read intput from stdin */
  int fd = 0;   /* 0 is the fileno for stdin */

  if (filename != NULL) {
    fd = topen(filename, O_BINARY|O_RDONLY);
    if (fd < 0) {
      tperror(filename);
      return 0;
    }
  }
  for (;;) {
    int nread;
    char *buf = (char *)XML_GetBuffer(parser, READ_SIZE);
    if (!buf) {
      if (filename != NULL)
        close(fd);
      ftprintf(stderr, T("%s: out of memory\n"),
               filename != NULL ? filename : T("xmlwf"));
      return 0;
    }
    nread = read(fd, buf, READ_SIZE);
    if (nread < 0) {
      tperror(filename != NULL ? filename : T("STDIN"));
      if (filename != NULL)
        close(fd);
      return 0;
    }
    if (XML_ParseBuffer(parser, nread, nread == 0) == XML_STATUS_ERROR) {
        reportError(parser, filename != NULL ? filename : T("STDIN"));
      if (filename != NULL)
        close(fd);
      return 0;
    }
    if (nread == 0) {
      if (filename != NULL)
        close(fd);
      break;;
    }
  }
  return 1;
}

static int
externalEntityRefStream(XML_Parser parser,
                        const XML_Char *context,
                        const XML_Char *base,
                        const XML_Char *systemId,
                        const XML_Char *UNUSED_P(publicId))
{
  XML_Char *s;
  const XML_Char *filename;
  int ret;
  XML_Parser entParser = XML_ExternalEntityParserCreate(parser, context, 0);
  filename = resolveSystemId(base, systemId, &s);
  XML_SetBase(entParser, filename);
  ret = processStream(filename, entParser);
  free(s);
  XML_ParserFree(entParser);
  return ret;
}

int
XML_ProcessFile(XML_Parser parser,
                const XML_Char *filename,
                unsigned flags)
{
  int result;

  if (!XML_SetBase(parser, filename)) {
    ftprintf(stderr, T("%s: out of memory"), filename);
    exit(1);
  }

  if (flags & XML_EXTERNAL_ENTITIES)
      XML_SetExternalEntityRefHandler(parser,
                                      (flags & XML_MAP_FILE)
                                      ? externalEntityRefFilemap
                                      : externalEntityRefStream);
  if (flags & XML_MAP_FILE) {
    int filemapRes;
    PROCESS_ARGS args;
    args.retPtr = &result;
    args.parser = parser;
    filemapRes = filemap(filename, processFile, &args);
    switch (filemapRes) {
    case 0:
      result = 0;
      break;
    case 2:
      ftprintf(stderr, T("%s: file too large for memory-mapping")
          T(", switching to streaming\n"), filename);
      result = processStream(filename, parser);
      break;
    }
  }
  else
    result = processStream(filename, parser);
  return result;
}

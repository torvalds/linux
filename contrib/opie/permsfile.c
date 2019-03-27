/* permsfile.c: implement SunOS /etc/fbtab and Solaris /etc/logindevperm
   functionality to set device permissions on login

%%% portions-copyright-cmetz-96
Portions of this software are Copyright 1996-1999 by Craig Metz, All Rights
Reserved. The Inner Net License Version 2 applies to these portions of
the software.
You should have received a copy of the license with this software. If
you didn't get a copy, you may request one from <license@inner.net>.

Portions of this software are Copyright 1995 by Randall Atkinson and Dan
McDonald, All Rights Reserved. All Rights under this copyright are assigned
to the U.S. Naval Research Laboratory (NRL). The NRL Copyright Notice and
License Agreement applies to this software.

	History:

	Modified by cmetz for OPIE 2.31. Include unistd.h.
	Modified by cmetz for OPIE 2.3. Check for NULL return from
	    ftpglob(), combine some expressions, fix a typo. Made file
	    selection a bit more generic.
	Modified by cmetz for OPIE 2.2. Use FUNCTION declaration et al.
            Add opie.h. Ifdef around a header.
	Written at NRL for OPIE 2.0.
*/

#include "opie_cfg.h"
#ifdef HAVE_LOGIN_PERMFILE
#include <stdio.h>
#include <sys/types.h>
#if HAVE_STRING_H
#include <string.h>
#endif /* HAVE_STRING_H */
#if HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */
#include <syslog.h>
#include "opie.h"

/* Line buffer size (one more than max line length) */
#define BUFSIZE 128
/* Maximum number of list items in a field */
#define LISTSIZE 10

static char buf[BUFSIZE], buf2[8];

char **ftpglob __P((char *));

VOIDRET opiefatal FUNCTION((x), char *x)
{
  fprintf(stderr, x);
  exit(1);
}

#include "glob.c"

static int getalist FUNCTION((string, list), char **string AND char **list)
{
  char *s = *string;
  int i = 0;

  while (*s && (*s != '\n') && (*s != ' ') && (*s != '\t'))
    if ((*s == ':') || (*s == ',')) {
      *(s++) = 0;
      list[i++] = *string;
      *string = s;
      if (i == LISTSIZE)
	return i;
    } else
      s++;

  if ((int) (s) - (int) (*string)) {
    *s = 0;
    list[i++] = *string;
  }
  *string = ++s;

  return i;
}

static VOIDRET doaline FUNCTION((line, name, ttyn, uid, gid), char *line AND char *name AND char *ttyn AND uid_t uid AND gid_t gid)
{
  char *ptr;
  int i;
  int applies, llen;
  char *listbuf[LISTSIZE], **globlist;

  if (ptr = strchr(buf, '#'))
    *ptr = 0;

  /* Skip whitespace */
  for (ptr = buf; *ptr && ((*ptr == ' ') || (*ptr == '\t'));
       ptr++);

  if (!*ptr)
    return;

  /* (Optional) Field 1: user name(s) */
  if ((*ptr != '/') && (*ptr != '~')) {
    llen = getalist(&ptr, listbuf);
    for (applies = i = 0; (i < llen) && !applies; i++)
      if (!strcmp(listbuf[i], name))
	applies++;
    while (*ptr && ((*ptr == ' ') || (*ptr == '\t')))
      ptr++;
    if (!applies || !*ptr)
      return;
  }
  /* Field 2: terminal(s) */
  llen = getalist(&ptr, listbuf);
  for (applies = i = 0; (i < llen) && !applies; i++)
    if (!strcmp(listbuf[i], ttyn))
      applies++;

  while (*ptr && ((*ptr == ' ') || (*ptr == '\t')))
    ptr++;

  if (!applies || !*ptr)
    return;

  /* Field 3: mode */
  for (applies = 0; *ptr && (*ptr >= '0') && (*ptr <= '7');
       applies = (applies << 3) | (*(ptr++) - '0'));

  while (*ptr && ((*ptr == ' ') || (*ptr == '\t')))
    ptr++;

  if (!*ptr)
    return;

  /* Field 4: devices (the fun part...) */
  llen = getalist(&ptr, listbuf);
  for (i = 0; i < llen; i++) {
    if (globlist = ftpglob(listbuf[i]))
      while (*globlist) {
#ifdef DEBUG
        syslog(LOG_DEBUG, "setting %s to %d/%d %o", *globlist, uid, gid, applies);
#endif /* DEBUG */
        if ((chown(*globlist, uid, gid) < 0) && (errno != ENOENT))
	  perror("chown");
        if ((chmod(*(globlist++), applies) < 0) && (errno != ENOENT))
	  perror("chmod");
    }
  }
}

VOIDRET permsfile FUNCTION((name, ttyn, uid, gid), char *name AND char *ttyn AND uid_t uid AND gid_t gid)
{
  FILE *fh;

  if (!(fh = fopen(HAVE_LOGIN_PERMFILE, "r"))) {
    syslog(LOG_ERR, "Can't open %s!", HAVE_LOGIN_PERMFILE);
    fprintf(stderr, "Warning: Can't set device permissions.\n");
    return;
  }
  do {
    if (feof(fh))
      return;
    if (fgets(buf, BUFSIZE, fh) == NULL)
      return;
    buf[BUFSIZE] = 0;

    doaline(buf, name, ttyn, uid, gid);
  }
  while (1);
}
#endif /* HAVE_LOGIN_PERMFILE */

/* accessfile.c: Handle trusted network access file and per-user 
        overrides.

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

	Modified by cmetz for OPIE 2.31. Include syslog.h on debug.
	Modified by cmetz for OPIE 2.3. Send debug info to syslog.
	Modified by cmetz for OPIE 2.2. Use FUNCTION declaration et al.
                Ifdef around some headers. Remove extra semicolon.
        Modified at NRL for OPIE 2.2. Moved from accessfile.c to
                libopie/opieaccessfile.c.
	Modified at NRL for OPIE 2.0.
	Written at Bellcore for the S/Key Version 1 software distribution
		(login.c).
*/
#include "opie_cfg.h"

#include <stdio.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#if HAVE_STRING_H
#include <string.h>
#endif /* HAVE_STRING_H */
#if HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */
#if HAVE_STDLIB_H
#include <stdlib.h>
#endif /* HAVE_STDLIB_H */

#ifdef DEBUG
#include <syslog.h>
#endif /* DEBUG */

#include "opie.h"

int opieaccessfile FUNCTION((host), char *host)
{
#ifdef PATH_ACCESS_FILE
/* Turn host into an IP address and then look it up in the authorization
 * database to determine if ordinary password logins are OK
 */
  long n;
  struct hostent *hp;
  FILE *fp;
  char buf[128], **lp;

#ifdef DEBUG
  syslog(LOG_DEBUG, "accessfile: host=%s", host);
#endif /* DEBUG */
  if (!host[0])
    /* Local login, okay */
    return (1);
  if (isaddr(host)) {
    n = inet_addr(host);
    return rdnets(n);
  } else {
    hp = gethostbyname(host);
    if (!hp) {
      printf("Unknown host %s\n", host);
      return 0;
    }
    for (lp = hp->h_addr_list; *lp; lp++) {
      memcpy((char *) &n, *lp, sizeof(n));
      if (rdnets(n))
	return (1);
    }
    return (0);
  }
}

int rdnets FUNCTION((host), long host)
{
  FILE *fp;
  char buf[128], *cp;
  long pattern, mask;
  int permit_it;

  if (!(fp = fopen(PATH_ACCESS_FILE, "r")))
    return 0;

  while (fgets(buf, sizeof(buf), fp), !feof(fp)) {
    if (buf[0] == '#')
      continue;	/* Comment */
    if (!(cp = strtok(buf, " \t")))
      continue;
    /* two choices permit of deny */
    if (strncasecmp(cp, "permit", 4) == 0) {
      permit_it = 1;
    } else {
      if (strncasecmp(cp, "deny", 4) == 0) {
	permit_it = 0;
      } else {
	continue;	/* ignore; it is not permit/deny */
      }
    }
    if (!(cp = strtok(NULL, " \t")))
      continue;	/* Invalid line */
    pattern = inet_addr(cp);
    if (!(cp = strtok(NULL, " \t")))
      continue;	/* Invalid line */
    mask = inet_addr(cp);
#ifdef DEBUG
    syslog(LOG_DEBUG, "accessfile: %08x & %08x == %08x (%s)", host, mask, pattern, ((host & mask) == pattern) ? "true" : "false");
#endif /* DEBUG */
    if ((host & mask) == pattern) {
      fclose(fp);
      return permit_it;
    }
  }
  fclose(fp);
  return 0;
}


/* Return TRUE if string appears to be an IP address in dotted decimal;
 * return FALSE otherwise (i.e., if string is a domain name)
 */
int isaddr FUNCTION((s), register char *s)
{
  char c;

  if (!s)
    return 1;	/* Can't happen */

  while ((c = *s++) != '\0') {
    if (c != '[' && c != ']' && !isdigit(c) && c != '.')
      return 0;
  }
  return 1;
#else	/* PATH_ACCESS_FILE */
  return !host[0];
#endif	/* PATH_ACCESS_FILE */
}

/* Returns the opposite of what you might expect */
/* Returns 1 on error (allow)... this might not be what you want */
int opiealways FUNCTION((homedir), char *homedir)
{
  char *opiealwayspath;
  int i;

  if (!homedir)
    return 1;

  if (!(opiealwayspath = malloc(strlen(homedir) + sizeof(OPIE_ALWAYS_FILE) + 1)))
    return 1;

  strcpy(opiealwayspath, homedir);
  strcat(opiealwayspath, "/");
  strcat(opiealwayspath, OPIE_ALWAYS_FILE);
  i = access(opiealwayspath, F_OK);
  free(opiealwayspath);
  return (i);
}

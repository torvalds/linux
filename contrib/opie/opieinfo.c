/*
opieinfo: Print a user's current OPIE sequence number and seed

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

	Modified by cmetz for OPIE 2.3. Removed unneeded debug message.
	Modified by cmetz for OPIE 2.2. Use FUNCTION definition et al.
               Fixed include order. Make everything static. Ifdef around
               some headers.
        Modified at NRL for OPIE 2.1. Substitute @@KEY_FILE@@. Re-write in
	       C.
        Modified at NRL for OPIE 2.01. Remove hard-coded paths for grep and
               awk and let PATH take care of it. Substitute for Makefile 
               variables $(EXISTS) and $(KEY_FILE). Only compute $WHO if 
               there's a key file. Got rid of grep since awk can do the job
               itself.
	Modified at NRL for OPIE 2.0.
	Written at Bellcore for the S/Key Version 1 software distribution
		(keyinfo)

$FreeBSD$

*/

#include "opie_cfg.h"
#include <sys/param.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */
#include "opie.h"

/* extern char *optarg; */
/* extern int errno, optind; */

static char *getusername FUNCTION_NOARGS
{
  char *login;

  login = getlogin();
  if (login == NULL) {
    fprintf(stderr, "Cannot find login name\n");
    exit(1);
  }
  return login;
}

int main FUNCTION((argc, argv), int argc AND char *argv[])
{
  char *username;
  struct opie opie;
  int i;

  while ((i = getopt(argc, argv, "hv")) != EOF) {
    switch (i) {
    case 'v':
      opieversion();
    case 'h':
    default:
      fprintf(stderr, "usage: %s [-h] [-v] [user_name]\n", argv[0]);
      exit(0);
    }
  }

  if (optind < argc) {
    if (getuid() != 0) {
      fprintf(stderr, "Only superuser may get another user's keys\n");
      exit(1);
    }
    username = argv[optind];
  } else
    username = getusername();

  if (strlen(username) >= MAXLOGNAME) {
    fprintf(stderr, "Username too long.\n");
    exit(1);
  }

  if ((i = opielookup(&opie, username)) && (i != 2)) {
    if (i < 0)
      fprintf(stderr, "Error opening database! (errno = %d)\n", errno);
    else
      fprintf(stderr, "%s not found in database.\n", username);
    exit(1);
  }

  printf("%d %s\n", opie.opie_n - 1, opie.opie_seed);

  return 0;
}

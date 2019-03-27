/* opiepasswd.c: Add/change an OTP password in the key database.

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

	Modified by cmetz for OPIE 2.4. Use struct opie_key for key blocks.
		Use opiestrncpy().
	Modified by cmetz for OPIE 2.32. Use OPIE_SEED_MAX instead of
		hard coding the length. Unlock user on failed lookup.
	Modified by cmetz for OPIE 2.3. Got of some variables and made some
		local to where they're used. Split out the finishing code. Use
		opielookup() instead of opiechallenge() to find user. Three
		strikes on prompts. Use opiepasswd()'s new calling
		convention. Changed OPIE_PASS_{MAX,MIN} to
		OPIE_SECRET_{MAX,MIN}. Handle automatic reinits happenning
		below us. Got rid of unneeded headers. Use new opieatob8()
		return value convention. Added -f flag. Added SHA support.
	Modified by cmetz for OPIE 2.22. Finally got rid of the lock
	        filename kluge by implementing refcounts for locks.
		Use opiepasswd() to update key file. Error if we can't
		write to the key file. Check for minimum seed length.
        Modified at NRL for OPIE 2.2. Changed opiestrip_crlf to
                opiestripcrlf. Check opiereadpass() return value.
                Minor optimization. Change calls to opiereadpass() to
                use echo arg. Use opiereadpass() where we can.
                Make everything static. Ifdef around some headers.
                Changed use of gethostname() to uname(). Got rid of
                the need for buf[]. Properly check return value of
                opieatob8. Check seed length. Always generate proper-
                length seeds.
	Modified at NRL for OPIE 2.1. Minor autoconf changes.
        Modified heavily at NRL for OPIE 2.0.
	Written at Bellcore for the S/Key Version 1 software distribution
		(skeyinit.c).

 $FreeBSD$
*/
#include "opie_cfg.h"

#if HAVE_PWD_H
#include <pwd.h>
#endif /* HAVE_PWD_H */
#include <stdio.h>
#if HAVE_STRING_H
#include <string.h>
#endif /* HAVE_STRING_H */
#include <stdio.h>
#include <sys/types.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */
#if HAVE_STDLIB_H
#include <stdlib.h>
#endif /* HAVE_STDLIB_H */

#include "opie.h"

#define MODE_DEFAULT 0
#define MODE_CONSOLE 1
#define MODE_DISABLE 2

extern int optind;
extern char *optarg;

char *algnames[] = { NULL, NULL, NULL, "SHA-1", "MD4", "MD5" };
char *algids[] = { NULL, NULL, NULL, "sha1", "md4", "md5" };

static VOIDRET usage FUNCTION((myname), char *myname)
{
  fprintf(stderr, "usage: %s [-v] [-h] [-c|-d] [-f] [-n initial_sequence_number]\n                            [-s seed] [username]\n", myname);
  exit(1);
}

static VOIDRET finish FUNCTION((name), char *name)
{
  struct opie opie;
  char buf[OPIE_RESPONSE_MAX + 1];

  if (name) {
    if (opiechallenge(&opie, name, buf)) {
      fprintf(stderr, "Error verifying database.\n");
      finish(NULL);
    }
    printf("\nID %s ", opie.opie_principal);
    if (opie.opie_val && (opie.opie_val[0] == '*')) {
      printf("is disabled.\n");
      finish(NULL);
    }
    printf("OTP key is %d %s\n", opie.opie_n, opie.opie_seed);
    {
      struct opie_otpkey key;

      if (!opieatob8(&key, opie.opie_val)) {
	fprintf(stderr, "Error verifying key -- possible database corruption.\n");
	finish(NULL);
      }
      printf("%s\n", opiebtoe(buf, &key));
    }
  }

  while(!opieunlock());
  exit(name ? 0 : 1);
}

int main FUNCTION((argc, argv), int argc AND char *argv[])
{
  struct opie opie;
  int rval, n = 499, i, mode = MODE_DEFAULT, force = 0;
  char seed[OPIE_SEED_MAX+1];
  char *username;
  uid_t ruid;
  struct passwd *pp;

  memset(seed, 0, sizeof(seed));

  ruid = getuid();
  username = getlogin();
  pp = getpwnam(username);
  if (username == NULL || pp == NULL || pp->pw_uid != ruid)
    pp = getpwuid(ruid);
  if (pp == NULL) {
    fprintf(stderr, "Who are you?");
    return 1;
  }

  while ((i = getopt(argc, argv, "fhvcn:s:d")) != EOF) {
    switch (i) {
    case 'v':
      opieversion();
    case 'f':
#if INSECURE_OVERRIDE
      force = OPIEPASSWD_FORCE;
#else /* INSECURE_OVERRIDE */
      fprintf(stderr, "Sorry, but the -f option is not supported by this build of OPIE.\n");
#endif /* INSECURE_OVERRIDE */
      break;
    case 'c':
      mode = MODE_CONSOLE;
      break;
    case 'd':
      mode = MODE_DISABLE;
      break;
    case 'n':
      i = atoi(optarg);
      if (!(i > 0 && i < 10000)) {
	printf("Sequence numbers must be > 0 and < 10000\n");
	finish(NULL);
      }
      n = i;
      break;
    case 's':
      i = strlen(optarg);
      if ((i > OPIE_SEED_MAX) || (i < OPIE_SEED_MIN)) {
	printf("Seeds must be between %d and %d characters long.\n",
	       OPIE_SEED_MIN, OPIE_SEED_MAX);
	finish(NULL);
      }
      opiestrncpy(seed, optarg, sizeof(seed));
      break;
    default:
      usage(argv[0]);
    }
  }

  if (argc - optind >= 1) {
    if (strcmp(argv[optind], pp->pw_name)) {
      if (getuid()) {
	printf("Only root can change others' passwords.\n");
	exit(1);
      }
      if ((pp = getpwnam(argv[optind])) == NULL) {
	printf("%s: user unknown.\n", argv[optind]);
	exit(1);
      }
    }
  }

  opielock(pp->pw_name);
  rval = opielookup(&opie, pp->pw_name);

  switch (rval) {
  case 0:
    printf("Updating %s:\n", pp->pw_name);
    break;
  case 1:
    printf("Adding %s:\n", pp->pw_name);
    break;
  case 2:
    fprintf(stderr, "Error: Can't update key database.\n");
    finish(NULL);
  default:
    fprintf(stderr, "Error reading key database\n");
    finish(NULL);
  }

  if (seed[0]) {
    i = strlen(seed);
    if (i > OPIE_SEED_MAX) {
      fprintf(stderr, "Seeds must be less than %d characters long.", OPIE_SEED_MAX);
      finish(NULL);
    }
    if (i < OPIE_SEED_MIN) {
      fprintf(stderr, "Seeds must be greater than %d characters long.", OPIE_SEED_MIN);
      finish(NULL);
    }
  } else {
    if (!rval)
      strcpy(seed, opie.opie_seed);

    if (opienewseed(seed) < 0) {
      fprintf(stderr, "Error updating seed.\n");
      finish(NULL);
    }
  }

  if (opie.opie_seed && opie.opie_seed[0] && !strcmp(opie.opie_seed, seed)) {
    fprintf(stderr, "You must use a different seed for the new OTP sequence.\n");
    finish(NULL);
  }
  
  switch(mode) {
  case MODE_DEFAULT:
    {
      char tmp[OPIE_RESPONSE_MAX + 2];
      
      printf("You need the response from an OTP generator.\n");
#if DEBUG
      if (!rval) {
#else /* DEBUG */
      if (!rval && getuid()) {
#endif /* DEBUG */
	char oseed[OPIE_SEED_MAX + 1];
	int on;

	if (opiechallenge(&opie, pp->pw_name, tmp)) {
	  fprintf(stderr, "Error issuing challenge.\n");
	  finish(NULL);
	}
	on = opiegetsequence(&opie);
	{
	  char *c;
	  if (c = strrchr(tmp, ' '))
	    opiestrncpy(oseed, c + 1, sizeof(oseed));
	  else {
#if DEBUG
	    fprintf(stderr, "opiepasswd: bogus challenge\n");
#endif /* DEBUG */
	    finish(NULL);
	  }
	}
	printf("Old secret pass phrase:\n\t%s\n\tResponse: ", tmp);
	if (!opiereadpass(tmp, sizeof(tmp), 1))
	  tmp[0] = 0;
	i = opieverify(&opie, tmp);
	if (!tmp[0]) {
	  fprintf(stderr, "Error reading response.\n");
	  finish(NULL);
	}
	if (i) {
	  fprintf(stderr, "Error verifying response.\n");
#if DEBUG
	  fprintf(stderr, "opiepasswd: opieverify() returned %d\n", i);
#endif /* DEBUG */
	  finish(NULL);
	}
	{
	  char nseed[OPIE_SEED_MAX + 1];
	  int nn;

	  if (opiechallenge(&opie, pp->pw_name, tmp)) {
	    fprintf(stderr, "Error verifying database.\n");
	    finish(NULL);
	  }

	  nn = opiegetsequence(&opie);
	  {
	    char *c;
	    if (c = strrchr(tmp, ' '))
	      opiestrncpy(nseed, c + 1, sizeof(nseed));
	    else {
#if DEBUG
	      fprintf(stderr, "opiepasswd: bogus challenge\n");
#endif /* DEBUG */
	      finish(NULL);
	    }
	  }

	  opieverify(&opie, "");
	  nn++;

	  if ((nn != on) || strcmp(oseed, nseed))
	    finish(pp->pw_name);
	}
      }
      printf("New secret pass phrase:");
      for (i = 0;; i++) {
	if (i > 2)
	  finish(NULL);
	printf("\n\totp-%s %d %s\n\tResponse: ", algids[MDX], n, seed);
	if (!opiereadpass(tmp, sizeof(tmp), 1)) {
	  fprintf(stderr, "Error reading response.\n");
	  finish(NULL);
	}
	if (tmp[0] == '?') {
	  printf("Enter the response from your OTP calculator: \n");
	  continue;
	}
	if (tmp[0] == '\0') {
	  fprintf(stderr, "Secret pass phrase unchanged.\n");
	  finish(NULL);
	}
	
	if (!(rval = opiepasswd(&opie, force, pp->pw_name, n, seed, tmp)))
	  finish(pp->pw_name);
	
	if (rval < 0) {
	  fprintf(stderr, "Error updating key database.\n");
	  finish(NULL);
	}
	printf("\tThat is not a valid OTP response.\n");
      }
    }
    break;
  case MODE_CONSOLE:
    {
      char passwd[OPIE_SECRET_MAX + 1], passwd2[OPIE_SECRET_MAX + 1];
      /* Get user's secret password */
      fprintf(stderr, "Only use this method from the console; NEVER from remote. If you are using\n");
      fprintf(stderr, "telnet, xterm, or a dial-in, type ^C now or exit with no password.\n");
      fprintf(stderr, "Then run opiepasswd without the -c parameter.\n");
      if (opieinsecure() && !force) {
	fprintf(stderr, "Sorry, but you don't seem to be on the console or a secure terminal.\n");
	if (force)
          fprintf(stderr, "Warning: Continuing could disclose your secret pass phrase to an attacker!\n");
        else
          finish(NULL);
      };
      printf("Using %s to compute responses.\n", algnames[MDX]);
      if (!rval && getuid()) {
	printf("Enter old secret pass phrase: ");
	if (!opiereadpass(passwd, sizeof(passwd), 0)) {
	  fprintf(stderr, "Error reading secret pass phrase!\n");
	  finish(NULL);
	}
	if (!passwd[0]) {
	  fprintf(stderr, "Secret pass phrase unchanged.\n");
	  finish(NULL);
	}
	{
	  struct opie_otpkey key;
	  char tbuf[OPIE_RESPONSE_MAX + 1];
	  
	  if (opiekeycrunch(MDX, &key, opie.opie_seed, passwd) != 0) {
	    fprintf(stderr, "%s: key crunch failed. Secret pass phrase unchanged\n", argv[0]);
	    finish(NULL);
	  }
	  memset(passwd, 0, sizeof(passwd));
	  i = opie.opie_n - 1;
	  while (i-- != 0)
	    opiehash(&key, MDX);
	  opiebtoe(tbuf, &key);
	  if (opieverify(&opie, tbuf)) {
	    fprintf(stderr, "Sorry.\n");
	    finish(NULL);
	  }
	}
      }
      for (i = 0;; i++) {
	if (i > 2)
	  finish(NULL);
	printf("Enter new secret pass phrase: ");
	if (!opiereadpass(passwd, sizeof(passwd), 0)) {
	  fprintf(stderr, "Error reading secret pass phrase.\n");
	  finish(NULL);
	}
	if (!passwd[0] || feof(stdin)) {
	  fprintf(stderr, "Secret pass phrase unchanged.\n");
	  finish(NULL);
	}
	if (opiepasscheck(passwd)) { 
	  memset(passwd, 0, sizeof(passwd));
	  fprintf(stderr, "Secret pass phrases must be between %d and %d characters long.\n", OPIE_SECRET_MIN, OPIE_SECRET_MAX);
	  continue;
	}
	printf("Again new secret pass phrase: ");
	if (!opiereadpass(passwd2, sizeof(passwd2), 0)) {
	  fprintf(stderr, "Error reading secret pass phrase.\n");
	  finish(NULL);
	}
	if (feof(stdin)) {
	  fprintf(stderr, "Secret pass phrase unchanged.\n");
	  finish(NULL);
	}
	if (!passwd[0] || !strcmp(passwd, passwd2))
	  break;
	fprintf(stderr, "Sorry, no match.\n");
      }
      memset(passwd2, 0, sizeof(passwd2));
      if (opiepasswd(&opie, 1 | force, pp->pw_name, n, seed, passwd)) {
	fprintf(stderr, "Error updating key database.\n");
	finish(NULL);
      }
      finish(pp->pw_name);
    }
  case MODE_DISABLE:
    {
      char tmp[4];
      int i;

      for (i = 0;; i++) {
	if (i > 2)
	  finish(NULL);
	
	printf("Disable %s's OTP access? (yes or no) ", pp->pw_name);
	if (!opiereadpass(tmp, sizeof(tmp), 1)) {
	  fprintf(stderr, "Error reading entry.\n");
	  finish(NULL);
	}
	if (!strcmp(tmp, "no"))
	  finish(NULL);
	if (!strcmp(tmp, "yes")) {
	  if (opiepasswd(&opie, 0, pp->pw_name, n, seed, NULL)) {
	    fprintf(stderr, "Error updating key database.\n");
	    finish(NULL);
	  }
	  finish(pp->pw_name);
	}
      }
    }
  }
}

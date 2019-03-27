/* opiekey.c: Stand-alone program for computing responses to OTP challenges.

 Takes a sequence number and seed (presumably from an OPIE challenge)
 as command line arguments, prompts for the user's secret pass phrase,
 and outputs a response.

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
	Modified by cmetz for OPIE 2.31. Renamed "init" and RESPONSE_INIT
	        to "init-hex" and RESPONSE_INIT_HEX. Removed active attack
		protection support.
	Modified by cmetz for OPIE 2.3. OPIE_PASS_MAX changed to
		OPIE_SECRET_MAX. Added extended responses, which created
		lots of changes. Eliminated extra variable. Added -x and
		-t to help. Added -f flag. Added SHA support.
	Modified by cmetz for OPIE 2.22. Print newline after seed too long
	        message. Check for minimum seed length. Correct a grammar
		error.
        Modified at NRL for OPIE 2.2. Check opiereadpass() return.
                Change opiereadpass() calls to add echo arg. Use FUNCTION
                definition et al. Check seed length here, too. Added back
		hex output. Reworked final output function.
	Modified at NRL for OPIE 2.0.
	Written at Bellcore for the S/Key Version 1 software distribution
		(skey.c).

$FreeBSD$

*/
#include "opie_cfg.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "opie.h"

#ifdef	__MSDOS__
#include <dos.h>
#endif

#if HAVE_FCNTL_H
#include <fcntl.h>
#endif /* HAVE_FCNTL_H */

extern char *optarg;
extern int optind, opterr;

int aflag = 0;

char *algnames[] = { NULL, NULL, NULL, "SHA-1", "MD4", "MD5" };
char *algids[] = { NULL, NULL, NULL, "sha1", "md4", "md5" };

/******** Begin real source code ***************/

static VOIDRET usage FUNCTION((s), char *s)
{
  fprintf(stderr, "usage: %s [-v] [-h] [-f] [-x] [-t type] [-4 | -5 | -s] [-a] [-n count] sequence_number seed\n", s);
  exit(1);
}

#define RESPONSE_STANDARD  0
#define RESPONSE_WORD      1
#define RESPONSE_HEX       2
#define RESPONSE_INIT_HEX  3
#define RESPONSE_INIT_WORD 4
#define RESPONSE_UNKNOWN   5

struct _rtrans {
  int type;
  char *name;
};

static struct _rtrans rtrans[] = {
  { RESPONSE_WORD, "word" },
  { RESPONSE_HEX, "hex" },
  { RESPONSE_INIT_HEX, "init-hex" },
  { RESPONSE_INIT_WORD, "init-word" },
  { RESPONSE_STANDARD, "" },
  { RESPONSE_STANDARD, "standard" },
  { RESPONSE_STANDARD, "otp" },
  { RESPONSE_UNKNOWN, NULL }
};

static void getsecret FUNCTION((secret, promptextra, retype), char *secret AND char *promptextra AND int flags)
{
  fprintf(stderr, "Enter %ssecret pass phrase: ", promptextra);
  if (!opiereadpass(secret, OPIE_SECRET_MAX, 0)) {
    fprintf(stderr, "Error reading %ssecret pass phrase!\n", promptextra);
    exit(1);
  }
  if (secret[0] && (flags & 1)) {
    char verify[OPIE_SECRET_MAX + 1];

    fprintf(stderr, "Again %ssecret pass phrase: ", promptextra);
    if (!opiereadpass(verify, OPIE_SECRET_MAX, 0)) {
      fprintf(stderr, "Error reading %ssecret pass phrase!\n", promptextra);
      memset(verify, 0, sizeof(verify));
      memset(secret, 0, OPIE_SECRET_MAX + 1);
      exit(1);
    }
    if (verify[0] && strcmp(verify, secret)) {
      fprintf(stderr, "They don't match. Try again.\n");
      memset(verify, 0, sizeof(verify));
      memset(secret, 0, OPIE_SECRET_MAX + 1);
      exit(1);
    }
    memset(verify, 0, sizeof(verify));
  }
  if (!(flags & 2) && !aflag && opiepasscheck(secret)) {
    memset(secret, 0, OPIE_SECRET_MAX + 1);
    fprintf(stderr, "Secret pass phrases must be between %d and %d characters long.\n", OPIE_SECRET_MIN, OPIE_SECRET_MAX);
    exit(1);
  };
}

int main FUNCTION((argc, argv), int argc AND char *argv[])
{
  /* variable declarations */
  unsigned algorithm = MDX;	/* default algorithm per Makefile's MDX
				   symbol */
  int keynum = 0;
  int i;
  int count = 1;
  char secret[OPIE_SECRET_MAX + 1], newsecret[OPIE_SECRET_MAX + 1];
  struct opie_otpkey key, newkey;
  char *seed, newseed[OPIE_SEED_MAX + 1];
  char response[OPIE_RESPONSE_MAX + 1];
  char *slash;
  int hex = 0;
  int type = RESPONSE_STANDARD;
  int force = 0;

  if (slash = strrchr(argv[0], '/'))
    slash++;
  else
    slash = argv[0];

  if (!strcmp(slash, "key") || strstr(slash, "md4"))
    algorithm = 4;

  if (strstr(slash, "md5"))
    algorithm = 5;

  if (strstr(slash, "sha"))
    algorithm = 3;

  while ((i = getopt(argc, argv, "fhvn:x45at:s")) != EOF) {
    switch (i) {
    case 'v':
      opieversion();

    case 'n':
      count = atoi(optarg);
      break;

    case 'x':
      hex = 1;
      break;

    case 'f':
#if INSECURE_OVERRIDE
      force = 1;
#else /* INSECURE_OVERRIDE */
      fprintf(stderr, "Sorry, but the -f option is not supported by this build of OPIE.\n");
#endif /* INSECURE_OVERRIDE */
      break;

    case '4':
      /* use MD4 algorithm */
      algorithm = 4;
      break;

    case '5':
      /* use MD5 algorithm */
      algorithm = 5;
      break;

    case 'a':
      aflag = 1;
      break;

    case 't':
      {
	struct _rtrans *r;
	for (r = rtrans; r->name && strcmp(r->name, optarg); r++);
	if (!r->name) {
	  fprintf(stderr, "%s: %s: unknown response type.\n", argv[0], optarg);
	  exit(1);
	}
	type = r->type;
      }
      break;

    case 's':
      algorithm = 3;
      break;

    default:
      usage(argv[0]);
    }
  }

  if ((argc - optind) < 2)
    usage(argv[0]);

  fprintf(stderr, "Using the %s algorithm to compute response.\n", algnames[algorithm]);

  /* get sequence number, which is next-to-last parameter */
  keynum = atoi(argv[optind]);
  if (keynum < 1) {
    fprintf(stderr, "Sequence number %s is not positive.\n", argv[optind]);
    exit(1);
  }
  /* get seed string, which is last parameter */
  seed = argv[optind + 1];
  {
    i = strlen(seed);

    if (i > OPIE_SEED_MAX) {
      fprintf(stderr, "Seeds must be less than %d characters long.\n", OPIE_SEED_MAX);
      exit(1);
    }
    if (i < OPIE_SEED_MIN) {
      fprintf(stderr, "Seeds must be greater than %d characters long.\n", OPIE_SEED_MIN);
      exit(1);
    }
  }

  fprintf(stderr, "Reminder: Don't use opiekey from telnet or dial-in sessions.\n");

  if (opieinsecure()) {
    fprintf(stderr, "Sorry, but you don't seem to be on the console or a secure terminal.\n");
#if INSECURE_OVERRIDE
    if (force)
      fprintf(stderr, "Warning: Continuing could disclose your secret pass phrase to an attacker!\n");
    else
#endif /* INSECURE_OVERRIDE */
      exit(1);
  }

  if ((type == RESPONSE_INIT_HEX) || (type == RESPONSE_INIT_WORD)) {
#if RETYPE
    getsecret(secret, "old ", 1);
#else /* RETYPE */
    getsecret(secret, "old ", 0);
#endif /* RETYPE */
    getsecret(newsecret, "new ", 1);
    if (!newsecret[0])
      strcpy(newsecret, secret);

    if (opienewseed(strcpy(newseed, seed)) < 0) {
      fprintf(stderr, "Error updating seed.\n");
      goto error;
    }

    if (opiekeycrunch(algorithm, &newkey, newseed, newsecret)) {
      fprintf(stderr, "%s: key crunch failed (1)\n", argv[0]);
      goto error;
    }

    for (i = 0; i < 499; i++)
      opiehash(&newkey, algorithm);
  } else
#if RETYPE
    getsecret(secret, "", 1);
#else /* RETYPE */
    getsecret(secret, "", 0);
#endif /* RETYPE */

  /* Crunch seed and secret password into starting key normally */
  if (opiekeycrunch(algorithm, &key, seed, secret)) {
    fprintf(stderr, "%s: key crunch failed\n", argv[0]);
    goto error;
  }

  for (i = 0; i <= (keynum - count); i++)
    opiehash(&key, algorithm);

  {
    char buf[OPIE_SEED_MAX + 48 + 1];
    char *c;

    for (; i <= keynum; i++) {
      if (count > 1)
	printf("%d: %s", i, (type == RESPONSE_STANDARD) ? "" : "\n");
      
      switch(type) {
      case RESPONSE_STANDARD:
	if (hex)
	  opiebtoh(response, &key);
	else
	  opiebtoe(response, &key);
	break;
      case RESPONSE_WORD:
	strcpy(response, "word:");
	strcat(response, opiebtoe(buf, &key));
	break;
      case RESPONSE_HEX:
	strcpy(response, "hex:");
	strcat(response, opiebtoh(buf, &key));
	break;
      case RESPONSE_INIT_HEX:
      case RESPONSE_INIT_WORD:
	if (type == RESPONSE_INIT_HEX) {
	  strcpy(response, "init-hex:");
	  strcat(response, opiebtoh(buf, &key));
	  sprintf(buf, ":%s 499 %s:", algids[algorithm], newseed);
	  strcat(response, buf);
	  strcat(response, opiebtoh(buf, &newkey));
	} else {
	  strcpy(response, "init-word:");
	  strcat(response, opiebtoe(buf, &key));
	  sprintf(buf, ":%s 499 %s:", algids[algorithm], newseed);
	  strcat(response, buf);
	  strcat(response, opiebtoe(buf, &newkey));
	}
	break;
      }
      puts(response);
      opiehash(&key, algorithm);
    }
  }

  memset(secret, 0, sizeof(secret));
  memset(newsecret, 0, sizeof(newsecret));
  return 0;

error:
  memset(secret, 0, sizeof(secret));
  memset(newsecret, 0, sizeof(newsecret));
  return 1;
}

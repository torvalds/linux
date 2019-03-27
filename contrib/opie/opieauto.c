/* opieauto.c: The opieauto program.

%%% copyright-cmetz-96
This software is Copyright 1996-2001 by Craig Metz, All Rights Reserved.
The Inner Net License Version 3 applies to this software.
You should have received a copy of the license with this software. If
you didn't get a copy, you may request one from <license@inner.net>.

        History:

	Created by cmetz for OPIE 2.4 based on previously released
	        test code. Use opiestrncpy().
*/

#include "opie_cfg.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#if HAVE_SYS_TIME_H
#include <sys/time.h>
#endif /* HAVE_SYS_TIME_H */
#include <stdio.h>
#include <errno.h>
#if HAVE_STRING_H
#include <string.h>
#endif /* HAVE_STRING_H */
#include <getopt.h>
#if HAVE_STDLIB_H
#include <stdlib.h>
#endif /* HAVE_STDLIB_H */
#if HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */
#include <sys/stat.h>

#include "opie.h"

#ifndef max
#define max(x, y) (((x) > (y)) ? (x) : (y))
#endif /* max */

int window = 10;
char *myname = NULL;

uid_t myuid = 0;

#define MAXCLIENTS 2
int parents, s[MAXCLIENTS + 1];

char cmd[1+1+1+1+4+1+OPIE_SEED_MAX+1+4+1+4+1+4+1+4+1];

struct cachedotp {
  struct cachedotp *next;
  int algorithm, base, current;
  struct opie_otpkey basekey;
  char seed[OPIE_SEED_MAX+1];
};

struct cachedotp *head = NULL;

char *algids[] = { NULL, NULL, NULL, "sha1", "md4", "md5" };

void baile(x) {
  fprintf(stderr, "%s: %s: %s(%d)\n", myname, x, strerror(errno), errno);
  exit(1);
}

void bail(x) {
  fprintf(stderr, "%s: %s\n", myname, x);
  exit(1);
}

void zerocache(void)
{
  struct cachedotp *c = head, *c2;

  while(c) {
    c2 = c->next;
    memset(c, 0, sizeof(struct cachedotp));
    c = c2;
  };
};

int doreq(int fd)
{
  int algorithm, sequence, i;
  char *seed = NULL, *response = NULL;

  if (((cmd[0] != 'S') && (cmd[0] != 's')) || (cmd[1] != '=') || (cmd[2] != ' ')) {
#if DEBUG
    fprintf(stderr, "%s: got bogus command: %s\n", myname, cmd);
#endif /* DEBUG */
    goto error;
  };

  {
  char *c;

  if (((algorithm = strtoul(&cmd[3], &c, 10)) < 3) || (algorithm > 5) || (*c != ' ')) {
#if DEBUG
    fprintf(stderr, "%s: got bogus algorithm: %s\n", myname, cmd);
#endif /* DEBUG */
    goto error;
  };

  if (((sequence = strtoul(c + 1, &c, 10)) <= OPIE_SEQUENCE_RESTRICT) || (sequence > OPIE_SEQUENCE_MAX)) {
#if DEBUG
    fprintf(stderr, "%s: got bogus sequence: %s\n", myname, cmd);
#endif /* DEBUG */
    goto error;
  };

  if (cmd[0] == 'S') {
    if (!(c = strchr(seed = c + 1, ' '))) {
#if DEBUG
      fprintf(stderr, "%s: got bogus seed: %s\n", myname, cmd);
#endif /* DEBUG */
      goto error;
    };

    *c = 0;

    if (!(c = strchr(response = c + 1, '\n'))) {
#if DEBUG
      fprintf(stderr, "%s: got bogus response: %s\n", myname, cmd);
#endif /* DEBUG */
      goto error;
    };

    *c = 0;
  } else {
    if (!(c = strchr(seed = c + 1, '\n'))) {
#if DEBUG
      fprintf(stderr, "%s: got bogus seed: %s\n", myname, cmd);
#endif /* DEBUG */
      goto error;
    };

    *c = 0;
  };
  };

#if DEBUG
  fprintf(stderr, "got cmd=%c, algorithm=%d sequence=%d seed=+%s+ response=+%s+ on fd %d\n", cmd[0], algorithm, sequence, seed, response, fd);
#endif /* DEBUG */

  seed = strdup(seed);

  if (sequence < 10) {
#if DEBUG
    fprintf(stderr, "sequence < 10; can't do it\n");
#endif /* DEBUG */
    sprintf(cmd, "%c- %d %d %s\n", cmd[0], algorithm, sequence, seed);
  };

  {
  struct cachedotp **c;

  for (c = &head; *c && (strcmp((*c)->seed, seed) || ((*c)->algorithm != algorithm)); c = &((*c)->next));
  if (!(*c)) {
    if (cmd[0] == 's') {
#if DEBUG
      fprintf(stderr, "(seed, algorithm) not found for s command\n");
#endif /* DEBUG */
      sprintf(cmd, "s- %d %d %s\n", algorithm, sequence, seed);
      goto out;
    }

    if (!(*c = malloc(sizeof(struct cachedotp))))
      baile("malloc");
    memset(*c, 0, sizeof(struct cachedotp));

    (*c)->algorithm = algorithm;
    opiestrncpy((*c)->seed, seed, OPIE_SEED_MAX);
  };

  if (cmd[0] == 'S') {
    (*c)->base = max(sequence - window + 1, OPIE_SEQUENCE_RESTRICT);
    (*c)->current = sequence;

    if (!opieatob8(&(*c)->basekey, response))
      goto error;

    sprintf(cmd, "S+ %d %d %s\n", algorithm, sequence, (*c)->seed);
  } else {
    if (sequence != ((*c)->current - 1)) {
#if DEBUG
      fprintf(stderr, "out of sequence: sequence=%d, base=%d, current=%d\n", sequence, (*c)->base, (*c)->current);
#endif /* DEBUG */
      sprintf(cmd, "s- %d %d %s\n", algorithm, sequence, (*c)->seed);
      goto out;
    };

    if (sequence < (*c)->base) {
#if DEBUG
      fprintf(stderr, "attempt to generate below base: sequence=%d, base=%d, current=%d\n", sequence, (*c)->base, (*c)->current);
#endif /* DEBUG */
      sprintf(cmd, "s- %d %d %s\n", algorithm, sequence, (*c)->seed);
      goto out;
    };

    (*c)->current = sequence;
    i = sequence - (*c)->base;
    {
      struct opie_otpkey key;
      char buffer[16+1];

      key = (*c)->basekey;
      while(i--)
	opiehash(&key, algorithm);

      opiebtoa8(buffer, &key);
      sprintf(cmd, "s+ %d %d %s %s\n", algorithm, sequence, (*c)->seed, buffer);
    };
  };

  printf("%c otp-%s %d %s (%d/%d)\n", cmd[0], algids[algorithm], sequence, (*c)->seed, sequence - (*c)->base, window);
  fflush(stdout);

  if (sequence == (*c)->base) {
    struct cachedotp *c2 = *c;
    *c = (*c)->next;
    memset(c2, 0, sizeof(struct cachedotp));
    free(c2);
  };
  };

out:
  write(fd, cmd, i = strlen(cmd));
  free(seed);
  return 0;

error:
  fprintf(stderr, "Invalid command on fd %d\n", fd);
  if (seed)
    free(seed);
  return -1;
}

static void usage()
{
  fprintf(stderr, "usage: %s [-v] [-h] [-q] [-n <number of OTPs>]\n", myname);
  exit(1);
}

int main(int argc, char **argv)
{
  int i;
  struct stat st;
  char *sockpath;

  if (myname = strrchr(argv[0], '/'))
    myname++;
  else
    myname = argv[0];

  while((i = getopt(argc, argv, "w:hv")) != EOF) {
    switch(i) {
      case 'v':
	opieversion();

      case 'w':
	if (!(window = atoi(optarg))) {
	  fprintf(stderr, "%s: invalid number of OTPs: %s\n", myname, optarg);
	  exit(1);
	};
	break;

      default:
	usage();
    }
  };

  {
    uid_t myeuid;

    if (!(myuid = getuid()) || !(myeuid = geteuid()) || (myuid != myeuid))
      bail("this program must not be run with superuser priveleges or setuid.");
  };

  if (atexit(zerocache) < 0)
    baile("atexit");

  {
    struct sockaddr_un sun;

    memset(&sun, 0, sizeof(struct sockaddr_un));
    sun.sun_family = AF_UNIX;

    {
    char *c;
    char *c2 = "/.opieauto";

    if (!(c = getenv("HOME")))
      bail("getenv(HOME) failed -- no HOME variable?");

    if (strlen(c) > (sizeof(sun.sun_path) - strlen(c2) - 1))
      bail("your HOME is too long");

    strcpy(sun.sun_path, c);
    strcat(sun.sun_path, c2);
    sockpath = strdup(sun.sun_path);
    };

    if ((parents = socket(PF_UNIX, SOCK_STREAM, 0)) < 0)
      baile("socket");

    if (unlink(sockpath) && (errno != ENOENT))
      baile("unlink");

    if (umask(0177) < 0)
      baile("umask");

    if (bind(parents, (struct sockaddr *)&sun, sizeof(struct sockaddr_un)))
      baile("bind");

    if (stat(sockpath, &st) < 0)
      baile("stat");

    if ((st.st_uid != myuid) || (!S_ISSOCK(st.st_mode)) || ((st.st_mode & 07777) != 0600))
      bail("socket permissions and/or ownership were not correctly created.");

    if (listen(parents, 1) < 0)
      baile("listen");
  };

  {
    fd_set fds, rfds, efds;
    int maxfd = parents;
    int i, j;

    FD_ZERO(&fds);
    FD_SET(parents, &fds);

    while(1) {
      memcpy(&rfds, &fds, sizeof(fd_set));

      if (select(maxfd + 1, &rfds, NULL, NULL, NULL) < 0)
	baile("select");

      for (i = 0; s[i]; i++) {
	if (!FD_ISSET(s[i], &rfds))
	  continue;

	if (((j = read(s[i], cmd, sizeof(cmd)-1)) <= 0) || ((cmd[j] = 0) || doreq(s[i]))) {
	  close(s[i]);
	  FD_CLR(s[i], &fds);

	  if (s[i] == maxfd)
	    maxfd--;

	  for (j = i; s[j]; s[j] = s[j + 1], j++);
	  FD_SET(parents, &fds);
	  i--;
	  continue;
	};
      };

      if (FD_ISSET(parents, &rfds)) {
	for (i = 0; s[i]; i++)
	  if (i > MAXCLIENTS)
	    bail("this message never printed");

	if (stat(sockpath, &st) < 0)
	  baile("stat");

	if ((st.st_uid != myuid) || (!S_ISSOCK(st.st_mode)) || ((st.st_mode & 07777) != 0600))
	  bail("socket permissions and/or ownership has been messed with.");

	if ((s[i] = accept(parents, NULL, 0)) < 0)
	  baile("accept");

	FD_SET(s[i], &fds);
	if (s[i] > maxfd)
	  maxfd = s[i];

	sprintf(cmd, "C+ %d\n", window);
	if (write(s[i], cmd, j = strlen(cmd)) != j)
	  baile("write");

	if (++i == MAXCLIENTS)
	  FD_CLR(parents, &fds);
      }
    }
  }
}

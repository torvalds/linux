/* generator.c: The opiegenerator() library function.

%%% portions-copyright-cmetz-96
Portions of this software are Copyright 1996-1999 by Craig Metz, All Rights
Reserved. The Inner Net License Version 2 applies to these portions of
the software.
You should have received a copy of the license with this software. If
you didn't get a copy, you may request one from <license@inner.net>.

        History:

	Modified by cmetz for OPIE 2.4. Added opieauto code based on
	        previously released test code. Renamed buffer to challenge.
		Use struct opie_otpkey for keys.
	Modified by cmetz for OPIE 2.32. If secret=NULL, always return
		as if opieauto returned "get the secret". Renamed
		_opieparsechallenge() to __opieparsechallenge(). Check
		challenge for extended response support and don't send
		an init-hex response if extended response support isn't
		indicated in the challenge.
	Modified by cmetz for OPIE 2.31. Renamed "init" to "init-hex".
		Removed active attack protection support. Fixed fairly
		bug in how init response was computed (i.e., dead wrong).
	Modified by cmetz for OPIE 2.3. Use _opieparsechallenge(). ifdef
		around string.h. Output hex responses by default, output
		OTP re-init extended responses (same secret) if sequence
		number falls below 10.
	Modified by cmetz for OPIE 2.2. Use FUNCTION declaration et al.
		Bug fixes.
	Created at NRL for OPIE 2.2.

$FreeBSD$
*/

#include "opie_cfg.h"
#if HAVE_STRING_H
#include <string.h>
#endif /* HAVE_STRING_H */
#if OPIEAUTO
#include <errno.h>
#if HAVE_STDLIB_H
#include <stdlib.h>
#endif /* HAVE_STDLIB_H */
#include <sys/stat.h>

#include <sys/socket.h>
#include <sys/un.h>
#endif /* OPIEAUTO */
#if DEBUG
#include <syslog.h>
#endif /* DEBUG */
#include <stdio.h>
#include "opie.h"

static char *algids[] = { NULL, NULL, NULL, "sha1", "md4", "md5" };

#if OPIEAUTO
#ifndef max
#define max(x, y) (((x) > (y)) ? (x) : (y))
#endif /* max */

static int opieauto_connect FUNCTION_NOARGS
{
  int s;
  struct sockaddr_un sun;
  char buffer[1024];
  char *c, *c2 ="/.opieauto";
  uid_t myuid = getuid(), myeuid = geteuid();

  if (!myuid || !myeuid || (myuid != myeuid)) {
#if DEBUG
    syslog(LOG_DEBUG, "opieauto_connect: superuser and/or setuid not allowed");
#endif /* DEBUG */
    return -1;
  };

  memset(&sun, 0, sizeof(struct sockaddr_un));
  sun.sun_family = AF_UNIX;

  if (!(c = getenv("HOME"))) {
#if DEBUG
    syslog(LOG_DEBUG, "opieauto_connect: no HOME variable?");
#endif /* DEBUG */
    return -1;
  };

  if (strlen(c) > (sizeof(sun.sun_path) - strlen(c2) - 1)) {
#if DEBUG
    syslog(LOG_DEBUG, "opieauto_connect: HOME is too long: %s", c);
#endif /* DEBUG */
    return -1;
  };

  strcpy(sun.sun_path, c);
  strcat(sun.sun_path, c2);

  if ((s = socket(PF_UNIX, SOCK_STREAM, 0)) < 0) {
#if DEBUG
    syslog(LOG_DEBUG, "opieauto_connect: socket: %s(%d)", strerror(errno), errno);
#endif /* DEBUG */
    return -1;
  };

  {
    struct stat st;

    if (stat(sun.sun_path, &st) < 0) {
#if DEBUG
      syslog(LOG_DEBUG, "opieauto_connect: stat: %s(%d)\n", strerror(errno), errno);
#endif /* DEBUG */
      goto ret;
    };

    if (connect(s, (struct sockaddr *)&sun, sizeof(struct sockaddr_un))) {
#if DEBUG
      syslog(LOG_DEBUG, "opieauto_connect: connect: %s(%d)\n", strerror(errno), errno);
#endif /* DEBUG */
      goto ret;
    };

    if ((st.st_uid != myuid) || (!S_ISSOCK(st.st_mode)) || ((st.st_mode & 07777) != 0600)) {
#if DEBUG
      syslog(LOG_DEBUG, "opieauto_connect: something's fishy about the socket\n");
#endif /* DEBUG */
      goto ret;
    };
  };

  return s;

ret:
  close(s);
  return -1;
};
#endif /* OPIEAUTO */

int opiegenerator FUNCTION((challenge, secret, response), char *challenge AND char *secret AND char *response)
{
  int algorithm;
  int sequence;
  char *seed;
  struct opie_otpkey key;
  int i;
  int exts;
#if OPIEAUTO
  int s;
  int window;
  char cmd[1+1+1+1+4+1+OPIE_SEED_MAX+1+4+1+4+1+4+1+4+1];
  char *c;
#endif /* OPIEAUTO */

  if (!(challenge = strstr(challenge, "otp-")))
    return 1;

  challenge += 4;

  if (__opieparsechallenge(challenge, &algorithm, &sequence, &seed, &exts))
    return 1;

  if ((sequence < 2) || (sequence > 9999))
    return 1;

  if (*secret) {
    if (opiepasscheck(secret))
      return -2;

    if (i = opiekeycrunch(algorithm, &key, seed, secret))
      return i;

    if (sequence <= OPIE_SEQUENCE_RESTRICT) {
      if (!(exts & 1))
	return 1;

      {
	char newseed[OPIE_SEED_MAX + 1];
	struct opie_otpkey newkey;
	char *c;
	char buf[OPIE_SEED_MAX + 48 + 1];

	while (sequence-- != 0)
	  opiehash(&key, algorithm);

	if (opienewseed(strcpy(newseed, seed)) < 0)
	  return -1;

	if (opiekeycrunch(algorithm, &newkey, newseed, secret))
	  return -1;

	for (i = 0; i < 499; i++)
	  opiehash(&newkey, algorithm);

	strcpy(response, "init-hex:");
	strcat(response, opiebtoh(buf, &key));
	if (snprintf(buf, sizeof(buf), ":%s 499 %s:", algids[algorithm],
	    newseed) >= sizeof(buf)) {
#ifdef DEBUG
	  syslog(LOG_DEBUG, "opiegenerator: snprintf truncation at init-hex");
#endif /* DEBUG */
	  return -1;
	}
	strcat(response, buf);
	strcat(response, opiebtoh(buf, &newkey));
      };
    };
  };

#if OPIEAUTO
  if ((s = opieauto_connect()) >= 0) {
    if ((i = read(s, cmd, sizeof(cmd)-1)) < 0) {
#if DEBUG
      syslog(LOG_DEBUG, "opiegenerator: read: %s(%d)\n", strerror(errno), errno);
#endif /* DEBUG */
      close(s);
      s = -1;
      goto l0;
    };
    cmd[i] = 0;
    if ((cmd[0] != 'C') || (cmd[1] != '+') || (cmd[2] != ' ')) {
#if DEBUG
      syslog(LOG_DEBUG, "opiegenerator: got invalid/failing C+ response: %s\n", cmd);
#endif /* DEBUG */
      close(s);
      s = -1;
      goto l0;
    };

    window = strtoul(&cmd[3], &c, 10);
    if (!window || (window >= (OPIE_SEQUENCE_MAX - OPIE_SEQUENCE_RESTRICT)) || !isspace(*c)) {
#if DEBUG
      syslog(LOG_DEBUG, "opiegenerator: got bogus option response: %s\n", cmd);
#endif /* DEBUG */
      close(s);
      s = -1;
      goto l0;
    };
  };

l0:
  if (*secret) {
    int j;

    if (s < 0) {
      j = 0;
      goto l1;
    };

    j = max(sequence - window + 1, OPIE_SEQUENCE_RESTRICT);

    for (i = j; i > 0; i--)
      opiehash(&key, algorithm);

    {
      char buf[16+1];

      opiebtoa8(buf, &key);

      if (snprintf(cmd, sizeof(cmd), "S= %d %d %s %s\n", algorithm, sequence,
          seed, buf) >= sizeof(cmd)) {
#if DEBUG
        syslog(LOG_DEBUG, "opiegenerator: snprintf truncation at S=\n");
#endif /* DEBUG */
	goto l1;
      }
    }

    if (write(s, cmd, i = strlen(cmd)) != i) {
#if DEBUG
      syslog(LOG_DEBUG, "opiegenerator: write: %s(%d)\n", strerror(errno), errno);
#endif /* DEBUG */
      goto l1;
    };

    if ((i = read(s, cmd, sizeof(cmd))) < 0) {
#if DEBUG
      syslog(LOG_DEBUG, "opiegenerator: read: %s(%d)\n", strerror(errno), errno);
#endif /* DEBUG */
    };
    close(s);

    cmd[i] = 0;
    i = strlen(seed);
    if ((cmd[0] != 'S') || (cmd[1] != '+') || (cmd[2] != ' ') || (strtoul(&cmd[3], &c, 10) != algorithm) || (strtoul(c + 1, &c, 10) != sequence) || strncmp(++c, seed, i) || (*(c + i) != '\n')) {
#if DEBUG
      syslog(LOG_DEBUG, "opiegenerator: got invalid/failing S+ response: %s\n", cmd);
#endif /* DEBUG */
    };

l1:
    for (i = sequence - j; i > 0; i--)
      opiehash(&key, algorithm);

    opiebtoh(response, &key);
  } else {
    if (s < 0)
      goto l2;

    if ((snprintf(cmd, sizeof(cmd), "s= %d %d %s\n", algorithm, sequence,
        seed) >= sizeof(cmd))) {
#if DEBUG
      syslog(LOG_DEBUG, "opiegenerator: snprintf truncation at s=\n");
#endif /* DEBUG */
      goto l2;
    }

    if (write(s, cmd, i = strlen(cmd)) != i) {
#if DEBUG
      syslog(LOG_DEBUG, "opiegenerator: write: %s(%d)\n", strerror(errno), errno);
#endif /* DEBUG */
      goto l2;
    };

    if ((i = read(s, cmd, sizeof(cmd))) < 0) {
#if DEBUG
      syslog(LOG_DEBUG, "opiegenerator: read: %s(%d)\n", strerror(errno), errno);
#endif /* DEBUG */
      goto l2;
    };
    close(s);

    i = strlen(seed);

    if ((cmd[0] != 's') || (cmd[2] != ' ') || (strtoul(&cmd[3], &c, 10) != algorithm) || (strtoul(c + 1, &c, 10) != sequence) || strncmp(++c, seed, i)) {
#if DEBUG
      if (c)
	*c = 0;
      else
	cmd[3] = 0;
      
      syslog(LOG_DEBUG, "opiegenerator: got bogus/invalid s response: %s\n", cmd);
#endif /* DEBUG */
      goto l2;
    };

    c += i;

    if (cmd[1] == '-') {
#if DEBUG
      if (*c != '\n') {
	*c = 0;
	syslog(LOG_DEBUG, "opiegenerator: got invalid s- response: %s\n", cmd);
      };
#endif /* DEBUG */
      goto l2;
    };

    if (cmd[1] != '+') {
#if DEBUG
      *c = 0;
      syslog(LOG_DEBUG, "opiegenerator: got invalid s response: %s\n", cmd);
#endif /* DEBUG */
      goto l2;
    };

    {
      char *c2;

      if (!(c2 = strchr(++c, '\n'))) {
#if DEBUG
	*c = 0;
	syslog(LOG_DEBUG, "opiegenerator: got invalid s+ response: %s\n", cmd);
#endif /* DEBUG */
	goto l2;
      };

      *c2++ = 0;
    };

    if (!opieatob8(&key, c))
      goto l2;

    opiebtoh(response, &key);
  };

  if (s >= 0)
    close(s);
#else /* OPIEAUTO */
  if (*secret) {
    while (sequence-- != 0)
      opiehash(&key, algorithm);

    opiebtoh(response, &key);
  } else
    return -2;
#endif /* OPIEAUTO */

  return 0;

#if OPIEAUTO
l2:
#if DEBUG
  syslog(LOG_DEBUG, "opiegenerator: no opieauto response available.\n");
#endif /* DEBUG */
  if (s >= 0)
    close(s);

  return -2;
#endif /* OPIEAUTO */
};

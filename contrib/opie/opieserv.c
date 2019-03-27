/* opieserv.c: Sample OTP server based on the opiechallenge() and
               opieverify() library routines.

%%% copyright-cmetz-96
This software is Copyright 1996-2001 by Craig Metz, All Rights Reserved.
The Inner Net License Version 3 applies to this software.
You should have received a copy of the license with this software. If
you didn't get a copy, you may request one from <license@inner.net>.

        History:

	Modified by cmetz for OPIE 2.3. Send debug info to syslog.
        Created by cmetz for OPIE 2.2.
*/
#include "opie_cfg.h"
#include <stdio.h>
#if DEBUG
#include <syslog.h>
#endif /* DEBUG */
#include "opie.h"

int main FUNCTION((argc, argv), int argc AND char *argv[])
{
	struct opie opie;
	char *principal;
	char buffer[1024];
        char challenge[OPIE_CHALLENGE_MAX+1];
        char response[OPIE_RESPONSE_MAX+1];
	int result;

	if (argc <= 1) {
		fputs("Principal: ", stderr);
                if (!opiereadpass(buffer, sizeof(buffer)-1, 1))
                  fprintf(stderr, "Error reading principal!");
		principal = buffer;
	} else {
		principal = argv[1];
	}
#if DEBUG
       	syslog(LOG_DEBUG, "Principal is +%s+", principal);
#endif /* DEBUG */

	switch (result = opiechallenge(&opie, principal, challenge)) {
		case -1:
			fputs("System error!\n", stderr);
			exit(1);
		case 0:
			break;
		case 1:
			fputs("User not found!\n", stderr);
			exit(1);
		case 2:
			fputs("System error!\n", stderr);
			exit(1);
		default:
			fprintf(stderr, "Unknown error %d!\n", result);
			exit(1);
	};

	fputs(challenge, stdout);
        fputc('\n', stdout);
        fflush(stdout);
	fputs("Response: ", stderr);
        if (!opiereadpass(response, OPIE_RESPONSE_MAX, 1)) {
          fputs("Error reading response!\n", stderr);
          exit(1);
        };

	switch (result = opieverify(&opie, response)) {
		case -1:
			fputs("System error!\n", stderr);
			exit(1);
		case 0:
			fputs("User verified.\n", stderr);
			exit(0);
		case 1:
			fputs("Verify failed!\n", stderr);
			exit(1);
		default:
			fprintf(stderr, "Unknown error %d!\n", result);
			exit(1);
	}
}

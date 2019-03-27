/* opiegen.c: Sample OTP generator based on the opiegenerator()
              library routine.

%%% portions-copyright-cmetz-96
Portions of this software are Copyright 1996-1999 by Craig Metz, All Rights
Reserved. The Inner Net License Version 2 applies to these portions of
the software.
You should have received a copy of the license with this software. If
you didn't get a copy, you may request one from <license@inner.net>.

        History:

	Modified by cmetz for OPIE 2.3. OPIE_PASS_MAX changed to
		OPIE_SECRET_MAX. Send debug info to syslog.
	Modified by cmetz for OPIE 2.2. Use FUNCTION definition et al.
             Fixed include order.
        Created at NRL for OPIE 2.2.
*/
#include "opie_cfg.h"
#include <stdio.h>
#if DEBUG
#include <syslog.h>
#endif /* DEBUG */
#include "opie.h"

int main FUNCTION((argc, argv), int argc AND char *argv[])
{
	char buffer[OPIE_CHALLENGE_MAX+1];
	char secret[OPIE_SECRET_MAX+1];
        char response[OPIE_RESPONSE_MAX+1];
	int result;

	if (opieinsecure()) {
		fputs("Sorry, but you don't seem to be on a secure terminal.\n", stderr);
#if !DEBUG
		exit(1);
#endif /* !DEBUG */
	}

	if (argc <= 1) {
		fputs("Challenge: ", stderr);
                if (!opiereadpass(buffer, sizeof(buffer)-1, 1))
                  fprintf(stderr, "Error reading challenge!");
	} else {
		char *ap, *ep, *c;
		int i;

		ep = buffer + sizeof(buffer) - 1;
		for (i = 1, ap = buffer; (i < argc) && (ap < ep); i++) {
			c = argv[i];
			while ((*(ap++) = *(c++)) && (ap < ep));
				*(ap - 1) = ' ';
		}
		*(ap - 1) = 0;
#if DEBUG
        	syslog(LOG_DEBUG, "opiegen: challenge is +%s+\n", buffer);
#endif /* DEBUG */
	}
	buffer[sizeof(buffer)-1] = 0;

	fputs("Secret pass phrase: ", stderr);
        if (!opiereadpass(secret, OPIE_SECRET_MAX, 0)) {
          fputs("Error reading secret pass phrase!\n", stderr);
          exit(1);
        };

	switch (result = opiegenerator(buffer, secret, response)) {
                case -2:
			fputs("Not a valid OTP secret pass phrase.\n", stderr);
			break;
		case -1:
			fputs("Error processing challenge!\n", stderr);
			break;
		case 1:
			fputs("Not a valid OTP challenge.\n", stderr);
			break;
		case 0:
			fputs(response, stdout);
			fputc('\n', stdout);
			fflush(stdout);
			memset(secret, 0, sizeof(secret));
			exit(0);
		default:
			fprintf(stderr, "Unknown error %d!\n", result);
	}
	memset(secret, 0, sizeof(secret));
	return 1;
}

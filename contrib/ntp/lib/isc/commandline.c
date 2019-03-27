/*
 * Portions Copyright (C) 2004, 2005, 2007, 2008  Internet Systems Consortium, Inc. ("ISC")
 * Portions Copyright (C) 1999-2001  Internet Software Consortium.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Copyright (c) 1987, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* $Id: commandline.c,v 1.22 2008/09/25 04:02:39 tbox Exp $ */

/*! \file
 * This file was adapted from the NetBSD project's source tree, RCS ID:
 *    NetBSD: getopt.c,v 1.15 1999/09/20 04:39:37 lukem Exp
 *
 * The primary change has been to rename items to the ISC namespace
 * and format in the ISC coding style.
 */

/*
 * \author Principal Authors: Computer Systems Research Group at UC Berkeley
 * \author Principal ISC caretaker: DCL
 */

#include <config.h>

#include <stdio.h>

#include <isc/commandline.h>
#include <isc/msgs.h>
#include <isc/string.h>
#include <isc/util.h>

/*% Index into parent argv vector. */
LIBISC_EXTERNAL_DATA int isc_commandline_index = 1;
/*% Character checked for validity. */
LIBISC_EXTERNAL_DATA int isc_commandline_option;
/*% Argument associated with option. */
LIBISC_EXTERNAL_DATA char *isc_commandline_argument;
/*% For printing error messages. */
LIBISC_EXTERNAL_DATA char *isc_commandline_progname;
/*% Print error messages. */
LIBISC_EXTERNAL_DATA isc_boolean_t isc_commandline_errprint = ISC_TRUE;
/*% Reset processing. */
LIBISC_EXTERNAL_DATA isc_boolean_t isc_commandline_reset = ISC_TRUE;

static char endopt = '\0';

#define	BADOPT	'?'
#define	BADARG	':'
#define ENDOPT  &endopt

/*!
 * getopt --
 *	Parse argc/argv argument vector.
 */
int
isc_commandline_parse(int argc, char * const *argv, const char *options) {
	static char *place = ENDOPT;
	char *option;			/* Index into *options of option. */

	REQUIRE(argc >= 0 && argv != NULL && options != NULL);

	/*
	 * Update scanning pointer, either because a reset was requested or
	 * the previous argv was finished.
	 */
	if (isc_commandline_reset || *place == '\0') {
		if (isc_commandline_reset) {
			isc_commandline_index = 1;
			isc_commandline_reset = ISC_FALSE;
		}

		if (isc_commandline_progname == NULL)
			isc_commandline_progname = argv[0];

		if (isc_commandline_index >= argc ||
		    *(place = argv[isc_commandline_index]) != '-') {
			/*
			 * Index out of range or points to non-option.
			 */
			place = ENDOPT;
			return (-1);
		}

		if (place[1] != '\0' && *++place == '-' && place[1] == '\0') {
			/*
			 * Found '--' to signal end of options.  Advance
			 * index to next argv, the first non-option.
			 */
			isc_commandline_index++;
			place = ENDOPT;
			return (-1);
		}
	}

	isc_commandline_option = *place++;
	option = strchr(options, isc_commandline_option);

	/*
	 * Ensure valid option has been passed as specified by options string.
	 * '-:' is never a valid command line option because it could not
	 * distinguish ':' from the argument specifier in the options string.
	 */
	if (isc_commandline_option == ':' || option == NULL) {
		if (*place == '\0')
			isc_commandline_index++;

		if (isc_commandline_errprint && *options != ':')
			fprintf(stderr, "%s: %s -- %c\n",
				isc_commandline_progname,
				isc_msgcat_get(isc_msgcat,
					       ISC_MSGSET_COMMANDLINE,
					       ISC_MSG_ILLEGALOPT,
					       "illegal option"),
				isc_commandline_option);

		return (BADOPT);
	}

	if (*++option != ':') {
		/*
		 * Option does not take an argument.
		 */
		isc_commandline_argument = NULL;

		/*
		 * Skip to next argv if at the end of the current argv.
		 */
		if (*place == '\0')
			++isc_commandline_index;

	} else {
		/*
		 * Option needs an argument.
		 */
		if (*place != '\0')
			/*
			 * Option is in this argv, -D1 style.
			 */
			isc_commandline_argument = place;

		else if (argc > ++isc_commandline_index)
			/*
			 * Option is next argv, -D 1 style.
			 */
			isc_commandline_argument = argv[isc_commandline_index];

		else {
			/*
			 * Argument needed, but no more argv.
			 */
			place = ENDOPT;

			/*
			 * Silent failure with "missing argument" return
			 * when ':' starts options string, per historical spec.
			 */
			if (*options == ':')
				return (BADARG);

			if (isc_commandline_errprint)
				fprintf(stderr, "%s: %s -- %c\n",
					isc_commandline_progname,
					isc_msgcat_get(isc_msgcat,
						       ISC_MSGSET_COMMANDLINE,
						       ISC_MSG_OPTNEEDARG,
						       "option requires "
						       "an argument"),
					isc_commandline_option);

			return (BADOPT);
		}

		place = ENDOPT;

		/*
		 * Point to argv that follows argument.
		 */
		isc_commandline_index++;
	}

	return (isc_commandline_option);
}

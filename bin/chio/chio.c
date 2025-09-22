/*	$OpenBSD: chio.c,v 1.30 2022/10/11 03:37:14 jsg Exp $	*/
/*	$NetBSD: chio.c,v 1.1.1.1 1996/04/03 00:34:38 thorpej Exp $	*/

/*
 * Copyright (c) 1996 Jason R. Thorpe <thorpej@and.com>
 * All rights reserved.
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
 *    must display the following acknowledgments:
 *	This product includes software developed by Jason R. Thorpe
 *	for And Communications, http://www.and.com/
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mtio.h>
#include <sys/chio.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <util.h>

#include "defs.h"
#include "pathnames.h"

#define _PATH_CH_CONF	"/etc/chio.conf"
extern	char *parse_tapedev(const char *, const char *, int); /* parse.y */
extern	char *__progname;	/* from crt0.o */

static	void usage(void);
static	int parse_element_type(char *);
static	int parse_element_unit(char *);
static	int parse_special(char *);
static	int is_special(char *);
static	const char * element_type_name(int et);
static	char *bits_to_string(int, const char *);
static	void find_voltag(char *, int *, int *);
static	void check_source_drive(int);

static	int do_move(char *, int, char **);
static	int do_exchange(char *, int, char **);
static	int do_position(char *, int, char **);
static	int do_params(char *, int, char **);
static	int do_getpicker(char *, int, char **);
static	int do_setpicker(char *, int, char **);
static	int do_status(char *, int, char **);

/* Valid changer element types. */
const struct element_type elements[] = {
	{ "drive",		CHET_DT },
	{ "picker",		CHET_MT },
	{ "portal",		CHET_IE },
	{ "slot",		CHET_ST },
	{ NULL,			0 },
};

/* Valid commands. */
const struct changer_command commands[] = {
	{ "exchange",		do_exchange },
	{ "getpicker",		do_getpicker },
	{ "move",		do_move },
	{ "params",		do_params },
	{ "position",		do_position },
	{ "setpicker",		do_setpicker },
	{ "status",		do_status },
	{ NULL,			0 },
};

/* Valid special words. */
const struct special_word specials[] = {
	{ "inv",		SW_INVERT },
	{ "inv1",		SW_INVERT1 },
	{ "inv2",		SW_INVERT2 },
	{ NULL,			0 },
};

static	int changer_fd;
static	char *changer_name;
static int avoltag;
static int pvoltag;
static int sense;
static int source;

int
main(int argc, char *argv[])
{
	int ch, i;

	while ((ch = getopt(argc, argv, "f:")) != -1) {
		switch (ch) {
		case 'f':
			changer_name = optarg;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc == 0)
		usage();

	/* Get the default changer if not already specified. */
	if (changer_name == NULL)
		if ((changer_name = getenv(CHANGER_ENV_VAR)) == NULL)
			changer_name = _PATH_CH;

	/* Open the changer device. */
	if ((changer_fd = open(changer_name, O_RDWR)) == -1)
		err(1, "%s: open", changer_name);

	/* Find the specified command. */
	for (i = 0; commands[i].cc_name != NULL; ++i)
		if (strcmp(*argv, commands[i].cc_name) == 0)
			break;
	if (commands[i].cc_name == NULL) {
		/* look for abbreviation */
		for (i = 0; commands[i].cc_name != NULL; ++i)
			if (strncmp(*argv, commands[i].cc_name,
			    strlen(*argv)) == 0)
				break;
	}
	if (commands[i].cc_name == NULL)
		errx(1, "unknown command: %s", *argv);

	exit((*commands[i].cc_handler)(commands[i].cc_name, argc, argv));
}

static int
do_move(char *cname, int argc, char *argv[])
{
	struct changer_move cmd;
	int val;

	/*
	 * On a move command, we expect the following:
	 *
	 * <from ET> <from EU> <to ET> <to EU> [inv]
	 *
	 * where ET == element type and EU == element unit.
	 */

	++argv; --argc;

	if (argc < 4) {
		warnx("%s: too few arguments", cname);
		goto usage;
	} else if (argc > 5) {
		warnx("%s: too many arguments", cname);
		goto usage;
	}
	bzero(&cmd, sizeof(cmd));

	/*
	 * Get the from ET and EU - we search for it if the ET is
	 * "voltag", otherwise, we just use the ET and EU given to us.
	 */
	if (strcmp(*argv, "voltag") == 0) {
		++argv; --argc;
		find_voltag(*argv, &cmd.cm_fromtype, &cmd.cm_fromunit);
		++argv; --argc;
	} else {
		cmd.cm_fromtype = parse_element_type(*argv);
		++argv; --argc;
		cmd.cm_fromunit = parse_element_unit(*argv);
		++argv; --argc;
	}

	if (cmd.cm_fromtype == CHET_DT)
		check_source_drive(cmd.cm_fromunit);

	/*
	 * Don't allow voltag on the to ET, using a volume
	 * as a destination makes no sense on a move
	 */
	cmd.cm_totype = parse_element_type(*argv);
	++argv; --argc;
	cmd.cm_tounit = parse_element_unit(*argv);
	++argv; --argc;

	/* Deal with optional command modifier. */
	if (argc) {
		val = parse_special(*argv);
		switch (val) {
		case SW_INVERT:
			cmd.cm_flags |= CM_INVERT;
			break;

		default:
			errx(1, "%s: inappropriate modifier `%s'",
			    cname, *argv);
			/* NOTREACHED */
		}
	}

	/* Send command to changer. */
	if (ioctl(changer_fd, CHIOMOVE, &cmd) == -1)
		err(1, "%s: CHIOMOVE", changer_name);

	return (0);

 usage:
	fprintf(stderr, "usage: %s %s "
	    "<from ET> <from EU> <to ET> <to EU> [inv]\n", __progname, cname);
	return (1);
}

static int
do_exchange(char *cname, int argc, char *argv[])
{
	struct changer_exchange cmd;
	int val;

	/*
	 * On an exchange command, we expect the following:
	 *
  * <src ET> <src EU> <dst1 ET> <dst1 EU> [<dst2 ET> <dst2 EU>] [inv1] [inv2]
	 *
	 * where ET == element type and EU == element unit.
	 */

	++argv; --argc;

	if (argc < 4) {
		warnx("%s: too few arguments", cname);
		goto usage;
	} else if (argc > 8) {
		warnx("%s: too many arguments", cname);
		goto usage;
	}
	bzero(&cmd, sizeof(cmd));

	/* <src ET>  */
	cmd.ce_srctype = parse_element_type(*argv);
	++argv; --argc;

	/* <src EU> */
	cmd.ce_srcunit = parse_element_unit(*argv);
	++argv; --argc;

	/* <dst1 ET> */
	cmd.ce_fdsttype = parse_element_type(*argv);
	++argv; --argc;

	/* <dst1 EU> */
	cmd.ce_fdstunit = parse_element_unit(*argv);
	++argv; --argc;

	/*
	 * If the next token is a special word or there are no more
	 * arguments, then this is a case of simple exchange.
	 * dst2 == src.
	 */
	if ((argc == 0) || is_special(*argv)) {
		cmd.ce_sdsttype = cmd.ce_srctype;
		cmd.ce_sdstunit = cmd.ce_srcunit;
		goto do_special;
	}

	/* <dst2 ET> */
	cmd.ce_sdsttype = parse_element_type(*argv);
	++argv; --argc;

	/* <dst2 EU> */
	cmd.ce_sdstunit = parse_element_unit(*argv);
	++argv; --argc;

 do_special:
	/* Deal with optional command modifiers. */
	while (argc) {
		val = parse_special(*argv);
		++argv; --argc;
		switch (val) {
		case SW_INVERT1:
			cmd.ce_flags |= CE_INVERT1;
			break;

		case SW_INVERT2:
			cmd.ce_flags |= CE_INVERT2;
			break;

		default:
			errx(1, "%s: inappropriate modifier `%s'",
			    cname, *argv);
			/* NOTREACHED */
		}
	}

	/* Send command to changer. */
	if (ioctl(changer_fd, CHIOEXCHANGE, &cmd) == -1)
		err(1, "%s: CHIOEXCHANGE", changer_name);

	return (0);

 usage:
	fprintf(stderr, "usage: %s %s <src ET> <src EU> <dst1 ET> <dst1 EU>\n"
	    "       [<dst2 ET> <dst2 EU>] [inv1] [inv2]\n",
	    __progname, cname);
	return (1);
}

static int
do_position(char *cname, int argc, char *argv[])
{
	struct changer_position cmd;
	int val;

	/*
	 * On a position command, we expect the following:
	 *
	 * <to ET> <to EU> [inv]
	 *
	 * where ET == element type and EU == element unit.
	 */

	++argv; --argc;

	if (argc < 2) {
		warnx("%s: too few arguments", cname);
		goto usage;
	} else if (argc > 3) {
		warnx("%s: too many arguments", cname);
		goto usage;
	}
	bzero(&cmd, sizeof(cmd));

	/* <to ET>  */
	cmd.cp_type = parse_element_type(*argv);
	++argv; --argc;

	/* <to EU> */
	cmd.cp_unit = parse_element_unit(*argv);
	++argv; --argc;

	/* Deal with optional command modifier. */
	if (argc) {
		val = parse_special(*argv);
		switch (val) {
		case SW_INVERT:
			cmd.cp_flags |= CP_INVERT;
			break;

		default:
			errx(1, "%s: inappropriate modifier `%s'",
			    cname, *argv);
			/* NOTREACHED */
		}
	}

	/* Send command to changer. */
	if (ioctl(changer_fd, CHIOPOSITION, &cmd) == -1)
		err(1, "%s: CHIOPOSITION", changer_name);

	return (0);

 usage:
	fprintf(stderr, "usage: %s %s <to ET> <to EU> [inv]\n",
	    __progname, cname);
	return (1);
}

static int
do_params(char *cname, int argc, char *argv[])
{
	struct changer_params data;

	/* No arguments to this command. */

	++argv; --argc;

	if (argc) {
		warnx("%s: no arguments expected", cname);
		goto usage;
	}

	/* Get params from changer and display them. */
	bzero(&data, sizeof(data));
	if (ioctl(changer_fd, CHIOGPARAMS, &data) == -1)
		err(1, "%s: CHIOGPARAMS", changer_name);

	printf("%s: %d slot%s, %d drive%s, %d picker%s",
	    changer_name,
	    data.cp_nslots, (data.cp_nslots > 1) ? "s" : "",
	    data.cp_ndrives, (data.cp_ndrives > 1) ? "s" : "",
	    data.cp_npickers, (data.cp_npickers > 1) ? "s" : "");
	if (data.cp_nportals)
		printf(", %d portal%s", data.cp_nportals,
		    (data.cp_nportals > 1) ? "s" : "");
	printf("\n%s: current picker: %d\n", changer_name, data.cp_curpicker);

	return (0);

 usage:
	fprintf(stderr, "usage: %s %s\n", __progname, cname);
	return (1);
}

static int
do_getpicker(char *cname, int argc, char *argv[])
{
	int picker;

	/* No arguments to this command. */

	++argv; --argc;

	if (argc) {
		warnx("%s: no arguments expected", cname);
		goto usage;
	}

	/* Get current picker from changer and display it. */
	if (ioctl(changer_fd, CHIOGPICKER, &picker) == -1)
		err(1, "%s: CHIOGPICKER", changer_name);

	printf("%s: current picker: %d\n", changer_name, picker);

	return (0);

 usage:
	fprintf(stderr, "usage: %s %s\n", __progname, cname);
	return (1);
}

static int
do_setpicker(char *cname, int argc, char *argv[])
{
	int picker;

	++argv; --argc;

	if (argc < 1) {
		warnx("%s: too few arguments", cname);
		goto usage;
	} else if (argc > 1) {
		warnx("%s: too many arguments", cname);
		goto usage;
	}

	picker = parse_element_unit(*argv);

	/* Set the changer picker. */
	if (ioctl(changer_fd, CHIOSPICKER, &picker) == -1)
		err(1, "%s: CHIOSPICKER", changer_name);

	return (0);

 usage:
	fprintf(stderr, "usage: %s %s <picker>\n", __progname, cname);
	return (1);
}

static int
do_status(char *cname, int argc, char *argv[])
{
	struct changer_element_status_request cmd;
	struct changer_params data;
	int i, chet, schet, echet, c;
	char *description;
	size_t count;

	optreset = 1;
	optind = 1;
	while ((c = getopt(argc, argv, "SsvVa")) != -1) {
		switch (c) {
		case 's':
			sense = 1;
			break;
		case 'S':
			source = 1;
			break;
		case 'v':
			pvoltag = 1;
			break;
		case 'V':
			avoltag = 1;
			break;
		case 'a':
			pvoltag = avoltag = source = sense = 1;
			break;
		default:
			goto usage;
		}
	}

	argc -= optind;
	argv += optind;

	/*
	 * On a status command, we expect the following:
	 *
	 * [<ET>]
	 *
	 * where ET == element type.
	 *
	 * If we get no arguments, we get the status of all
	 * known element types.
	 */
	if (argc > 1) {
		warnx("%s: too many arguments", cname);
		goto usage;
	}

	/*
	 * Get params from changer.  Specifically, we need the element
	 * counts.
	 */
	bzero(&data, sizeof(data));
	if (ioctl(changer_fd, CHIOGPARAMS, &data) == -1)
		err(1, "%s: CHIOGPARAMS", changer_name);

	if (argc)
		schet = echet = parse_element_type(*argv);
	else {
		schet = CHET_MT;
		echet = CHET_DT;
	}

	for (chet = schet; chet <= echet; ++chet) {
		switch (chet) {
		case CHET_MT:
			count = data.cp_npickers;
			description = "picker";
			break;

		case CHET_ST:
			count = data.cp_nslots;
			description = "slot";
			break;

		case CHET_IE:
			count = data.cp_nportals;
			description = "portal";
			break;

		case CHET_DT:
			count = data.cp_ndrives;
			description = "drive";
			break;
		}

		if (count == 0) {
			if (argc == 0)
				continue;
			else {
				printf("%s: no %s elements\n",
				    changer_name, description);
				return (0);
			}
		}

		bzero(&cmd, sizeof(cmd));

		cmd.cesr_type = chet;
		/* Allocate storage for the status info. */
		cmd.cesr_data = calloc(count, sizeof(*cmd.cesr_data));
		if ((cmd.cesr_data) == NULL)
			errx(1, "can't allocate status storage");
		if (avoltag || pvoltag)
			cmd.cesr_flags |= CESR_VOLTAGS;

		if (ioctl(changer_fd, CHIOGSTATUS, &cmd) == -1) {
			free(cmd.cesr_data);
			err(1, "%s: CHIOGSTATUS", changer_name);
		}

		/* Dump the status for each element of this type. */
		for (i = 0; i < count; ++i) {
			struct changer_element_status *ces =
			         &(cmd.cesr_data[i]);
			printf("%s %d: %s", description, i,
			    bits_to_string(ces->ces_flags, CESTATUS_BITS));
			if (sense)
				printf(" sense: <0x%02x/0x%02x>",
				       ces->ces_sensecode,
				       ces->ces_sensequal);
			if (pvoltag)
				printf(" voltag: <%s:%d>",
				       ces->ces_pvoltag.cv_volid,
				       ces->ces_pvoltag.cv_serial);
			if (avoltag)
				printf(" avoltag: <%s:%d>",
				       ces->ces_avoltag.cv_volid,
				       ces->ces_avoltag.cv_serial);
			if (source) {
				if (ces->ces_flags & CESTATUS_ACCESS)
					printf(" source: <%s %d>",
						element_type_name(
							ces->ces_source_type),
						ces->ces_source_addr);
				else
					printf(" source: <>");
			}
			printf("\n");
		}

		free(cmd.cesr_data);
	}

	return (0);

 usage:
	fprintf(stderr, "usage: %s %s [<element type>]\n", __progname,
	    cname);
	return (1);
}

/*
 * Check a drive unit as the source for a move or exchange
 * operation. If the drive is not accessible, we attempt
 * to unmount the tape in it before moving to avoid
 * errors in "disconnected" type pickers where the drive
 * is on a separate target from the changer.
 */
static void
check_source_drive(int unit)
{
	struct mtop mtoffl =  { MTOFFL, 1 };
	struct changer_element_status_request cmd;
	struct changer_element_status *ces;
	struct changer_params data;
	size_t count = 0;
	int mtfd;
	char *tapedev;

	/*
	 * Get params from changer.  Specifically, we need the element
	 * counts.
	 */
	bzero(&data, sizeof(data));
	if (ioctl(changer_fd, CHIOGPARAMS, &data) == -1)
		err(1, "%s: CHIOGPARAMS", changer_name);

	count = data.cp_ndrives;
	if (unit < 0 || unit >= count)
		err(1, "%s: invalid drive: drive %d", changer_name, unit);

	bzero(&cmd, sizeof(cmd));
	cmd.cesr_type = CHET_DT;
	/* Allocate storage for the status info. */
	cmd.cesr_data = calloc(count, sizeof(*cmd.cesr_data));
	if ((cmd.cesr_data) == NULL)
		errx(1, "can't allocate status storage");

	if (ioctl(changer_fd, CHIOGSTATUS, &cmd) == -1) {
		free(cmd.cesr_data);
		err(1, "%s: CHIOGSTATUS", changer_name);
	}
	ces = &(cmd.cesr_data[unit]);

	if ((ces->ces_flags & CESTATUS_FULL) != CESTATUS_FULL)
		err(1, "%s: drive %d is empty!", changer_name, unit);

	if ((ces->ces_flags & CESTATUS_ACCESS) == CESTATUS_ACCESS)
		return; /* changer thinks all is well - trust it */

	/*
	 * Otherwise, drive is FULL, but not accessible.
	 * Try to make it accessible by doing an mt offline.
	 */
	tapedev = parse_tapedev(_PATH_CH_CONF, changer_name, unit);
	mtfd = opendev(tapedev, O_RDONLY, 0, NULL);
	if (mtfd == -1)
		err(1, "%s drive %d (%s): open", changer_name, unit, tapedev);
	if (ioctl(mtfd, MTIOCTOP, &mtoffl) == -1)
		err(1, "%s drive %d (%s): rewoffl", changer_name, unit,
		    tapedev);
	close(mtfd);
}

void
find_voltag(char *voltag, int *type, int *unit)
{
	struct changer_element_status_request cmd;
	struct changer_params data;
	int i, chet, schet, echet, found;
	size_t count = 0;

	/*
	 * Get params from changer.  Specifically, we need the element
	 * counts.
	 */
	bzero(&data, sizeof(data));
	if (ioctl(changer_fd, CHIOGPARAMS, &data) == -1)
		err(1, "%s: CHIOGPARAMS", changer_name);

	found = 0;
	schet = CHET_MT;
	echet = CHET_DT;

	/*
	 * For each type of element, iterate through each one until
	 * we find the correct volume id.
	 */
	for (chet = schet; chet <= echet; ++chet) {
		switch (chet) {
		case CHET_MT:
			count = data.cp_npickers;
			break;
		case CHET_ST:
			count = data.cp_nslots;
			break;
		case CHET_IE:
			count = data.cp_nportals;
			break;
		case CHET_DT:
			count = data.cp_ndrives;
			break;
		}
		if (count == 0 || found)
			continue;

		bzero(&cmd, sizeof(cmd));
		cmd.cesr_type = chet;
		/* Allocate storage for the status info. */
		cmd.cesr_data = calloc(count, sizeof(*cmd.cesr_data));
		if ((cmd.cesr_data) == NULL)
			errx(1, "can't allocate status storage");
		cmd.cesr_flags |= CESR_VOLTAGS;

		if (ioctl(changer_fd, CHIOGSTATUS, &cmd) == -1) {
			free(cmd.cesr_data);
			err(1, "%s: CHIOGSTATUS", changer_name);
		}

		/*
		 * look through each element to see if it has our desired
		 * volume tag.
		 */
		for (i = 0; i < count; ++i) {
			struct changer_element_status *ces =
			    &(cmd.cesr_data[i]);
			if ((ces->ces_flags & CESTATUS_FULL) != CESTATUS_FULL)
				continue; /* no tape in drive */
			if (strcasecmp(voltag, ces->ces_pvoltag.cv_volid)
			    == 0) {
				*type = chet;
				*unit = i;
				found = 1;
				free(cmd.cesr_data);
				return;
			}
		}
		free(cmd.cesr_data);
	}
	errx(1, "%s: unable to locate voltag: %s", changer_name, voltag);
}


static int
parse_element_type(char *cp)
{
	int i;

	for (i = 0; elements[i].et_name != NULL; ++i)
		if (strcmp(elements[i].et_name, cp) == 0)
			return (elements[i].et_type);

	errx(1, "invalid element type `%s'", cp);
}

static const char *
element_type_name(int et)
{
	int i;

	for (i = 0; elements[i].et_name != NULL; i++)
		if (elements[i].et_type == et)
			return elements[i].et_name;

	return "unknown";
}

static int
parse_element_unit(char *cp)
{
	int i;
	char *p;

	i = (int)strtol(cp, &p, 10);
	if ((i < 0) || (*p != '\0'))
		errx(1, "invalid unit number `%s'", cp);

	return (i);
}

static int
parse_special(char *cp)
{
	int val;

	val = is_special(cp);
	if (val)
		return (val);

	errx(1, "invalid modifier `%s'", cp);
}

static int
is_special(char *cp)
{
	int i;

	for (i = 0; specials[i].sw_name != NULL; ++i)
		if (strcmp(specials[i].sw_name, cp) == 0)
			return (specials[i].sw_value);

	return (0);
}

static char *
bits_to_string(int v, const char *cp)
{
	const char *np;
	char f, sep, *bp;
	static char buf[128];

	bp = buf;
	bzero(buf, sizeof(buf));

	for (sep = '<'; (f = *cp++) != 0; cp = np) {
		for (np = cp; *np >= ' ';)
			np++;
		if ((v & (1 << (f - 1))) == 0)
			continue;
		(void)snprintf(bp, sizeof(buf) - (bp - &buf[0]),
		    "%c%.*s", sep, (int)(np - cp), cp);
		bp += strlen(bp);
		sep = ',';
	}
	if (sep != '<')
		*bp = '>';

	return (buf);
}

static void
usage(void)
{
	fprintf(stderr, "usage: %s [-f changer] command [arg ...]\n",
	    __progname);
	exit(1);
}

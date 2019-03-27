/*
 * Copyright (c) 2015, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 * Phil Shafer, June 2015
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>
#include <sys/time.h>
#include <sys/param.h>
#include <locale.h>
#include <libintl.h>

#include "xo.h"

int
main (int argc, char **argv)
{
    static char domainname[] = "gt_01";
    static char path[MAXPATHLEN];
    const char *tzone = "EST";
    const char *lang = "pig_latin";

    argc = xo_parse_args(argc, argv);
    if (argc < 0)
	return 1;

    for (argc = 1; argv[argc]; argc++) {
	if (strcmp(argv[argc], "tz") == 0)
	    tzone = argv[++argc];
	else if (strcmp(argv[argc], "lang") == 0)
	    lang = argv[++argc];
	else if (strcmp(argv[argc], "po") == 0)
	    strlcpy(path, argv[++argc], sizeof(path));
    }

    setenv("LANG", lang, 1);
    setenv("TZ", tzone, 1);

    if (path[0] == 0) {
	getcwd(path, sizeof(path));
	strlcat(path, "/po", sizeof(path));
    }

    setlocale(LC_ALL, "");
    bindtextdomain(domainname,  path);
    bindtextdomain("ldns",  path);
    bindtextdomain("strerror",  path);
    textdomain(domainname);
    tzset();

    xo_open_container("top");

    xo_emit("{G:}Your {qg:adjective} {g:noun} is {g:verb} {qg:owner} {g:target}\n",
	    "flaming", "sword", "burning", "my", "couch");

    xo_emit("{G:}The {qg:adjective} {g:noun} was {g:verb} {qg:owner} {g:target}\n",
	    "flaming", "sword", "burning", "my", "couch");


    int i;
    for (i = 0; i < 5; i++)
	xo_emit("{lw:bytes/%d}{Ngp:byte,bytes}\n", i);

    xo_emit("{G:}{L:total} {:total/%u}\n", 1234);

    xo_emit("{G:ldns}Received {:received/%zu} {Ngp:byte,bytes} "
	    "from {:from/%s}#{:port/%d} in {:time/%d} ms\n",
	    (size_t) 1234, "foop", 4321, 32);

    xo_emit("{G:}Received {:received/%zu} {Ngp:byte,bytes} "
	    "from {:from/%s}#{:port/%d} in {:time/%d} ms\n",
	    (size_t) 1234, "foop", 4321, 32);

    xo_emit("{G:/%s}Received {:received/%zu} {Ngp:byte,bytes} "
	    "from {:from/%s}#{:port/%d} in {:time/%d} ms\n",
	    "ldns", (size_t) 1234, "foop", 4321, 32);

    struct timeval tv;
    tv.tv_sec = 1435085229;
    tv.tv_usec = 123456;

    struct tm tm;
    (void) gmtime_r(&tv.tv_sec, &tm);

    char date[64];
    strftime(date, sizeof(date), "%+", &tm);

    xo_emit("{G:}Only {:marzlevanes/%d} {Ngp:marzlevane,marzlevanes} "
	    "are functioning correctly\n", 3);

    xo_emit("{G:}Version {:version} {:date}\n", "1.2.3", date);

    errno = EACCES;
    xo_emit_warn("{G:}Unable to {g:verb/objectulate} forward velociping");
    xo_emit_warn("{G:}{g:style/automatic} synchronization of {g:type/cardinal} "
		 "{g:target/grammeters} failed");
    xo_emit("{G:}{Lwcg:hydrocoptic marzlevanes}{:marzlevanes/%d}\n", 6);

    xo_emit("{G:}{Lwcg:Windings}{g:windings}\n", "lotus-o-delta");

    xo_close_container("top");
    xo_finish();

    return 0;
}

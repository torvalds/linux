/*
 * Copyright (c) 2014, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 * Phil Shafer, July 2014
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "xo_config.h"
#include "xo.h"

int
main (int argc, char **argv)
{
    int i, count = 10;
    int mon = 0;
    xo_emit_flags_t flags = XOEF_RETAIN;
    int opt_color = 1;

    argc = xo_parse_args(argc, argv);
    if (argc < 0)
	return 1;

    for (argc = 1; argv[argc]; argc++) {
	if (strcmp(argv[argc], "xml") == 0)
	    xo_set_style(NULL, XO_STYLE_XML);
	else if (strcmp(argv[argc], "json") == 0)
	    xo_set_style(NULL, XO_STYLE_JSON);
	else if (strcmp(argv[argc], "text") == 0)
	    xo_set_style(NULL, XO_STYLE_TEXT);
	else if (strcmp(argv[argc], "html") == 0)
	    xo_set_style(NULL, XO_STYLE_HTML);
	else if (strcmp(argv[argc], "no-color") == 0)
	    opt_color = 0;
	else if (strcmp(argv[argc], "pretty") == 0)
	    xo_set_flags(NULL, XOF_PRETTY);
	else if (strcmp(argv[argc], "xpath") == 0)
	    xo_set_flags(NULL, XOF_XPATH);
	else if (strcmp(argv[argc], "info") == 0)
	    xo_set_flags(NULL, XOF_INFO);
	else if (strcmp(argv[argc], "no-retain") == 0)
	    flags &= ~XOEF_RETAIN;
	else if (strcmp(argv[argc], "big") == 0) {
	    if (argv[argc + 1])
		count = atoi(argv[++argc]);
	}
    }

    xo_set_flags(NULL, XOF_UNITS); /* Always test w/ this */
    if (opt_color)
	xo_set_flags(NULL, XOF_COLOR); /* Force color output */
    xo_set_file(stdout);

    xo_open_container("top");
    xo_open_container("data");

    xo_emit("{C:fg-red,bg-green}Merry XMas!!{C:}\n");

    xo_emit("One {C:fg-yellow,bg-blue}{:animal}{C:}, "
	    "Two {C:fg-green,bg-yellow}{:animal}{C:}\n",
          "fish", "fish");

    const char *fmt1 = "The {C:fg-red}{k:name}{C:reset} is "
	"{C:/fg-%s}{:color}{C:reset} til {:time/%02d:%02d}\n";
    const char *fmt2 = "My {C:fg-red}{:hand}{C:reset} hand is "
	"{C:/fg-%s}{:color}{C:reset} til {:time/%02d:%02d}\n";

    for (i = 0; i < count; i++) {
	xo_open_instance("thing");
	xo_emit_f(flags, fmt1, "thing", "green", "green", 2, 15);
	xo_emit_f(flags, fmt2, "left", "blue", "blue", 3, 45);
    }

    xo_close_container("data");
    xo_close_container_h(NULL, "top");

    xo_finish();

    return 0;
}

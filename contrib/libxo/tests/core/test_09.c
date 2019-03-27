/*
 * Copyright (c) 2015, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 * Phil Shafer, July 2015
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "xo.h"

int
main (int argc, char **argv)
{
    struct item {
	const char *i_title;
	int i_count;
    };
    struct item list[] = {
	{ "gum", 1412 },
	{ "rope", 85 },
	{ "ladder", 0 },
	{ "bolt", 4123 },
	{ "water", 17 },
	{ NULL, 0 }
    };
    struct item *ip;
    int i;
    
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
	else if (strcmp(argv[argc], "pretty") == 0)
	    xo_set_flags(NULL, XOF_PRETTY);
	else if (strcmp(argv[argc], "xpath") == 0)
	    xo_set_flags(NULL, XOF_XPATH);
	else if (strcmp(argv[argc], "info") == 0)
	    xo_set_flags(NULL, XOF_INFO);
        else if (strcmp(argv[argc], "error") == 0) {
            close(-1);
            xo_err(1, "error detected");
        }
    }

    xo_set_flags(NULL, XOF_KEYS);
    xo_set_program("test");

    xo_open_container_h(NULL, "top");

    xo_open_container("data");
    xo_open_container("contents");

    xo_emit("{T:Item/%-10s}{T:Count/%12s}\n");

    for (ip = list; ip->i_title; ip++) {
	xo_emit("Name: {l:name/%-10s/%s}\n", ip->i_title);
    }

    xo_close_container("contents");

    xo_emit("\n\n");
    xo_open_container("contents");

    xo_emit("{T:Item/%-10s}{T:Count/%12s}\n");

    for (ip = list; ip->i_title; ip++) {
	xo_emit("Name: {l:item/%-10s/%s}\n", ip->i_title);
    }

    xo_close_container("contents");

    xo_emit("\n\n");

    xo_open_container("contents");
    xo_emit("{T:Test/%-10s}{T:Three/%12s}\n");

    xo_open_list("item");
    for (ip = list; ip->i_title; ip++) {
	xo_emit("Name: {l:item/%-10s/%s}\n", ip->i_title);
    }
    xo_emit("{Lwc:/Total:}{:total}\n", "six");

    xo_emit("{:one}", "one");
    xo_emit("{l:two}", "two");
    xo_emit("{:three}", "three");


    xo_close_container("contents");

    xo_emit("\n\n");

    xo_close_container_h(NULL, "top");

    xo_finish();

    return 0;
}

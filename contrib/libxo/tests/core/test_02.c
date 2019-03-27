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

#include "xo.h"

#include "xo_humanize.h"

int
main (int argc, char **argv)
{
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
    }

    xo_set_flags(NULL, XOF_UNITS); /* Always test w/ this */
    xo_set_file(stdout);

    xo_open_container_h(NULL, "top");

    xo_open_container("data");

    xo_emit("We are {{emit}}{{ting}} some {:what}\n", "braces");

    xo_message("abcdef");
    close(-1);
    xo_message_e("abcdef");

    xo_message("improper use of profanity; %s; %s",
	       "ten yard penalty", "first down");

    xo_emit("length {:length/%6.6s}\n", "abcdefghijklmnopqrstuvwxyz");

    close(-1);
    xo_emit("close {:fd/%d} returned {:error/%m} {:test}\n", -1, "good");
    close(-1);
    xo_emit("close {:fd/%d} returned {:error/%6.6m} {:test}\n", -1, "good");


    xo_message("improper use of profanity; %s; %s",
	       "ten yard penalty", "first down");

    xo_emit(" {:lines/%7ju} {:words/%7ju} "
            "{:characters/%7ju} {d:filename/%s}\n",
            (uintmax_t) 20, (uintmax_t) 30, (uintmax_t) 40, "file");

    int i;
    for (i = 0; i < 5; i++)
	xo_emit("{lw:bytes/%d}{Np:byte,bytes}\n", i);


    xo_emit("{:mbuf-current/%u}/{:mbuf-cache/%u}/{:mbuf-total/%u} "
	    "{N:mbufs <&> in use (current\\/cache\\/total)}\n",
	    10, 20, 30);

    xo_emit("{:distance/%u}{Uw:miles} from {:location}\n", 50, "Boston");
    xo_emit("{:memory/%u}{U:k} left out of {:total/%u}{U:kb}\n", 64, 640);
    xo_emit("{:memory/%u}{U:/%s} left out of {:total/%u}{U:/%s}\n",
	    64, "k", 640, "kilobytes");

    xo_emit("{,title:/before%safter:}\n", "working");

    xo_emit("{,display,white,colon:some/%s}"
	    "{,value:ten/%ju}{,value:eleven/%ju}\n",
	    "string", (uintmax_t) 10, (uintmax_t) 11);

    xo_emit("{:unknown/%u} "
	    "{N:/packet%s here\\/there\\/everywhere}\n",
	    1010, "s");

    xo_emit("{:unknown/%u} "
	    "{,note:/packet%s here\\/there\\/everywhere}\n",
	    1010, "s");

    xo_emit("({[:/%d}{n:min/15}/{n:cur/20}/{:max/%d}{]:})\n", 30, 125);
    xo_emit("({[:30}{:min/%u}/{:cur/%u}/{:max/%u}{]:})\n", 15, 20, 125);
    xo_emit("({[:-30}{n:min/15}/{n:cur/20}/{n:max/125}{]:})\n");
    xo_emit("({[:}{:min/%u}/{:cur/%u}/{:max/%u}{]:/%d})\n", 15, 20, 125, -30);

    xo_emit("Humanize: {h:val1/%u}, {h,hn-space:val2/%u}, "
	    "{h,hn-decimal:val3/%u}, {h,hn-1000:val4/%u}, "
	    "{h,hn-decimal:val5/%u}\n",
            21,
	    57 * 1024,
	    96 * 1024 * 1024,
	    (42 * 1024 + 420) * 1024,
	    1342172800);

    xo_open_list("flag");
    xo_emit("{lq:flag/one} {lq:flag/two} {lq:flag/three}\n");
    xo_close_list("flag");

    xo_emit("{n:works/%s}\n", NULL);

    xo_emit("{e:empty-tag/}");
    xo_emit("1:{qt:t1/%*d} 2:{qt:t2/test%-*u} "
	    "3:{qt:t3/%10sx} 4:{qt:t4/x%-*.*s}\n",
	    6, 1000, 8, 5000, "ten-long", 10, 10, "test");
    xo_emit("{E:this is an error}\n");
    xo_emit("{E:/%s more error%s}\n", "two", "s" );
    xo_emit("{W:this is an warning}\n");
    xo_emit("{W:/%s more warning%s}\n", "two", "s" );
    xo_emit("{L:/V1\\/V2 packet%s}: {:count/%u}\n", "s", 10);

    int test = 4;
    xo_emit("{:test/%04d} {L:/tr%s}\n", test, (test == 1) ? "y" : "ies");

    xo_message("improper use of profanity; %s; %s",
	       "ten yard penalty", "first down");

    xo_error("Shut 'er down, Clancey!  She's a-pumpin' mud!  <>!,\"!<>\n");

    xo_close_container("data");

    xo_close_container_h(NULL, "top");

    xo_finish();

    return 0;
}

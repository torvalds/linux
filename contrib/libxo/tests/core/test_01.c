/*
 * Copyright (c) 2014, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 * Phil Shafer, July 2014
 */

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "xo.h"

int
main (int argc, char **argv)
{
    static char base_grocery[] = "GRO";
    static char base_hardware[] = "HRD";
    struct item {
	const char *i_title;
	int i_sold;
	int i_instock;
	int i_onorder;
	const char *i_sku_base;
	int i_sku_num;
    };
    struct item list[] = {
	{ "gum", 1412, 54, 10, base_grocery, 415 },
	{ "rope", 85, 4, 2, base_hardware, 212 },
	{ "ladder", 0, 2, 1, base_hardware, 517 },
	{ "bolt", 4123, 144, 42, base_hardware, 632 },
	{ "water", 17, 14, 2, base_grocery, 2331 },
	{ NULL, 0, 0, 0, NULL, 0 }
    };
    struct item list2[] = {
	{ "fish", 1321, 45, 1, base_grocery, 533 },
	{ NULL, 0, 0, 0, NULL, 0 }
    };
    struct item *ip;
    xo_info_t info[] = {
	{ "in-stock", "number", "Number of items in stock" },
	{ "name", "string", "Name of the item" },
	{ "on-order", "number", "Number of items on order" },
	{ "sku", "string", "Stock Keeping Unit" },
	{ "sold", "number", "Number of items sold" },
	{ XO_INFO_NULL },
    };
    
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

    xo_set_info(NULL, info, -1);
    xo_set_flags(NULL, XOF_KEYS);

    xo_open_container_h(NULL, "top");

    xo_emit("static {:type/ethernet} {:type/bridge} {:type/%4du} {:type/%3d}",
	    18, 24);

    xo_emit("anchor {[:/%d}{:address/%p}..{:port/%u}{]:}\n", 18, NULL, 1);
    xo_emit("anchor {[:18}{:address/%p}..{:port/%u}{]:}\n", NULL, 1);
    xo_emit("anchor {[:/18}{:address/%p}..{:port/%u}{]:}\n", NULL, 1);

    xo_emit("df {:used-percent/%5.0f}{U:%%}\n", (double) 12);

    xo_emit("{e:kve_start/%#jx}", (uintmax_t) 0xdeadbeef);
    xo_emit("{e:kve_end/%#jx}", (uintmax_t) 0xcabb1e);

    xo_emit("testing argument modifier {a:}.{a:}...\n",
	    "host", "my-box", "domain", "example.com");

    xo_emit("testing argument modifier with encoding to {ea:}.{a:}...\n",
	    "host", "my-box", "domain", "example.com");

    xo_emit("{La:} {a:}\n", "Label text", "label", "value");

    xo_emit_field("Vt", "max-chaos", NULL, NULL, "  very  ");
    xo_emit_field("V", "min-chaos", "%d", NULL, 42);
    xo_emit_field("V", "some-chaos", "%d\n", "[%d]", 42);

    xo_emit("Connecting to {:host}.{:domain}...\n", "my-box", "example.com");

    xo_attr("test", "value");
    xo_open_container("data");
    xo_open_list("item");
    xo_attr("test2", "value2");

    xo_emit("{T:Item/%-10s}{T:Total Sold/%12s}{T:In Stock/%12s}"
	    "{T:On Order/%12s}{T:SKU/%5s}\n");

    for (ip = list; ip->i_title; ip++) {
	xo_open_instance("item");
	xo_attr("test3", "value3");

	xo_emit("{keq:sku/%s-%u/%s-000-%u}"
		"{k:name/%-10s/%s}{n:sold/%12u/%u}{:in-stock/%12u/%u}"
		"{:on-order/%12u/%u}{qkd:sku/%5s-000-%u/%s-000-%u}\n",
		ip->i_sku_base, ip->i_sku_num,
		ip->i_title, ip->i_sold, ip->i_instock, ip->i_onorder,
		ip->i_sku_base, ip->i_sku_num);

	xo_close_instance("item");
    }

    xo_close_list("item");
    xo_close_container("data");

    xo_emit("\n\n");

    xo_open_container("data2");
    xo_open_list("item");

    for (ip = list; ip->i_title; ip++) {
	xo_open_instance("item");

	xo_emit("{keq:sku/%s-%u/%s-000-%u}", ip->i_sku_base, ip->i_sku_num);
	xo_emit("{L:Item} '{k:name/%s}':\n", ip->i_title);
	xo_emit("{P:   }{L:Total sold}: {n:sold/%u%s}\n",
		ip->i_sold, ip->i_sold ? ".0" : "");
	xo_emit("{P:   }{Lcw:In stock}{:in-stock/%u}\n", ip->i_instock);
	xo_emit("{P:   }{Lcw:On order}{:on-order/%u}\n", ip->i_onorder);
	xo_emit("{P:   }{L:SKU}: {qkd:sku/%s-000-%u}\n",
		ip->i_sku_base, ip->i_sku_num);

	xo_close_instance("item");
    }

    xo_close_list("item");
    xo_close_container("data2");

    xo_open_container("data3");
    xo_open_list("item");

    for (ip = list2; ip->i_title; ip++) {
	xo_open_instance("item");

	xo_emit("{keq:sku/%s-%u/%s-000-%u}", ip->i_sku_base, ip->i_sku_num);
	xo_emit("{L:Item} '{k:name/%s}':\n", ip->i_title);
	xo_emit("{P:   }{L:Total sold}: {n:sold/%u%s}\n",
		ip->i_sold, ip->i_sold ? ".0" : "");
	xo_emit("{P:   }{Lcw:In stock}{:in-stock/%u}\n", ip->i_instock);
	xo_emit("{P:   }{Lcw:On order}{:on-order/%u}\n", ip->i_onorder);
	xo_emit("{P:   }{L:SKU}: {qkd:sku/%s-000-%u}\n",
		ip->i_sku_base, ip->i_sku_num);

	xo_close_instance("item");
    }

    xo_close_list("item");
    xo_close_container("data3");

    xo_open_container("data4");
    xo_open_list("item");

    for (ip = list; ip->i_title; ip++) {
	xo_attr("test4", "value4");
	xo_emit("{Lwc:Item}{l:item}\n", ip->i_title);
    }

    xo_close_list("item");
    xo_close_container("data4");

    xo_emit("X{P:}X", "epic fail");
    xo_emit("X{T:}X", "epic fail");
    xo_emit("X{N:}X", "epic fail");
    xo_emit("X{L:}X\n", "epic fail");

    xo_emit("X{P:        }X{Lwc:Cost}{:cost/%u}\n", 425);
    xo_emit("X{P:/%30s}X{Lwc:Cost}{:cost/%u}\n", "", 455);

    xo_emit("{e:mode/%s}{e:mode_octal/%s} {t:links/%s} "
	    "{t:user/%s}  {t:group/%s}  \n",
	    "mode", "octal", "links",
	    "user", "group", "extra1", "extra2", "extra3");

    xo_emit("{e:pre/%s}{t:links/%-*u}{t:post/%-*s}\n", "that", 8, 3, 8, "this");

    xo_emit("{t:mode/%s}{e:mode_octal/%03o} {t:links/%*u} "
	    "{t:user/%-*s}  {t:group/%-*s}  \n",
	    "/some/file", (int) 0640, 8, 1,
	    10, "user", 12, "group");

    xo_close_container_h(NULL, "top");

    xo_finish();

    return 0;
}

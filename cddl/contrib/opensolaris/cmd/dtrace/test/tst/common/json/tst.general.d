/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright 2012, Joyent, Inc.  All rights reserved.
 */

/*
 * General functional tests of JSON parser for json().
 */

#pragma D option quiet
#pragma D option strsize=1k

#define	TST(name)				\
	printf("\ntst |%s|\n", name)
#define	IN2(vala, valb)				\
	in = strjoin(vala, valb);		\
	printf("in  |%s|\n", in)
#define	IN(val)					\
	in = val;				\
	printf("in  |%s|\n", in)
#define	SEL(ss)					\
	out = json(in, ss);			\
	printf("sel |%s|\nout |%s|\n", ss,	\
	    out != NULL ? out : "<NULL>")

BEGIN
{
	TST("empty array");
	IN("[]");
	SEL("0");

	TST("one-element array: integer");
	IN("[1]");
	SEL("0");
	SEL("1");
	SEL("100");
	SEL("-1");

	TST("one-element array: hex integer (not in spec, not supported)");
	IN("[0x1000]");
	SEL("0");

	TST("one-element array: float");
	IN("[1.5001]");
	SEL("0");

	TST("one-element array: float + exponent");
	IN("[16.3e10]");
	SEL("0");

	TST("one-element array: integer + whitespace");
	IN("[ \t   5\t]");
	SEL("0");

	TST("one-element array: integer + exponent + whitespace");
	IN("[ \t    \t 16E10  \t ]");
	SEL("0");

	TST("one-element array: string");
	IN("[\"alpha\"]");
	SEL("0");

	TST("alternative first-element indexing");
	IN("[1,5,10,15,20]");
	SEL("[0]");
	SEL("[3]");
	SEL("[4]");
	SEL("[5]");

	TST("one-element array: object");
	IN("[ { \"first\": true, \"second\": false }]");
	SEL("0.first");
	SEL("0.second");
	SEL("0.third");

	TST("many-element array: integers");
	IN("[0,1,1,2,3,5,8,13,21,34,55,89,144,233,377]");
	SEL("10"); /* F(10) = 55 */
	SEL("14"); /* F(14) = 377 */
	SEL("19");

	TST("many-element array: multiple types");
	IN2("[\"string\",32,true,{\"a\":9,\"b\":false},100.3e10,false,200.5,",
	    "{\"key\":\"val\"},null]");
	SEL("0");
	SEL("0.notobject");
	SEL("1");
	SEL("2");
	SEL("3");
	SEL("3.a");
	SEL("3.b");
	SEL("3.c");
	SEL("4");
	SEL("5");
	SEL("6");
	SEL("7");
	SEL("7.key");
	SEL("7.key.notobject");
	SEL("7.nonexist");
	SEL("8");
	SEL("9");

	TST("many-element array: multiple types + whitespace");
	IN2("\n[\t\"string\" ,\t32 , true\t,\t {\"a\":  9,\t\"b\": false},\t\t",
	    "100.3e10, false, 200.5,{\"key\" \t:\n \"val\"},\t\t null ]\t\t");
	SEL("0");
	SEL("0.notobject");
	SEL("1");
	SEL("2");
	SEL("3");
	SEL("3.a");
	SEL("3.b");
	SEL("3.c");
	SEL("4");
	SEL("5");
	SEL("6");
	SEL("7");
	SEL("7.key");
	SEL("7.key.notobject");
	SEL("7.nonexist");
	SEL("8");
	SEL("9");

	TST("two-element array: various string escape codes");
	IN2("[\"abcd \\\" \\\\ \\/ \\b \\f \\n \\r \\t \\u0000 \\uf00F \", ",
	    "\"final\"]");
	SEL("0");
	SEL("1");

	TST("three-element array: broken escape code");
	IN("[\"fine here\", \"dodgey \\u00AZ\", \"wont get here\"]");
	SEL("0");
	SEL("1");
	SEL("2");

	TST("nested objects");
	IN2("{ \"top\": { \"mid\"  : { \"legs\": \"feet\" }, \"number\": 9, ",
	    "\"array\":[0,1,{\"a\":true,\"bb\":[1,2,false,{\"x\":\"yz\"}]}]}}");
	SEL("top");
	SEL("fargo");
	SEL("top.mid");
	SEL("top.centre");
	SEL("top.mid.legs");
	SEL("top.mid.number");
	SEL("top.mid.array");
	SEL("top.number");
	SEL("top.array");
	SEL("top.array[0]");
	SEL("top.array[1]");
	SEL("top.array[2]");
	SEL("top.array[2].a");
	SEL("top.array[2].b");
	SEL("top.array[2].bb");
	SEL("top.array[2].bb[0]");
	SEL("top.array[2].bb[1]");
	SEL("top.array[2].bb[2]");
	SEL("top.array[2].bb[3]");
	SEL("top.array[2].bb[3].x");
	SEL("top.array[2].bb[3].x.nofurther");
	SEL("top.array[2].bb[4]");
	SEL("top.array[3]");

	exit(0);
}

ERROR
{
	exit(1);
}

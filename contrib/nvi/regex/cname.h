/*	$NetBSD: cname.h,v 1.2 2008/12/05 22:51:42 christos Exp $ */

/*-
 * Copyright (c) 1992, 1993, 1994 Henry Spencer.
 * Copyright (c) 1992, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Henry Spencer of the University of Toronto.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
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
 *
 *	@(#)cname.h	8.2 (Berkeley) 3/16/94
 */

/* character-name table */
static struct cname {
	const RCHAR_T *name;
	char code;
} cnames[] = {
	{ L("NUL"),		'\0' },
	{ L("SOH"),		'\001' },
	{ L("STX"),		'\002' },
	{ L("ETX"),		'\003' },
	{ L("EOT"),		'\004' },
	{ L("ENQ"),		'\005' },
	{ L("ACK"),		'\006' },
	{ L("BEL"),		'\007' },
	{ L("alert"),		'\007' },
	{ L("BS"),		'\010' },
	{ L("backspace"),	'\b' },
	{ L("HT"),		'\011' },
	{ L("tab"),		'\t' },
	{ L("LF"),		'\012' },
	{ L("newline"),		'\n' },
	{ L("VT"),		'\013' },
	{ L("vertical-tab"),	'\v' },
	{ L("FF"),		'\014' },
	{ L("form-feed"),	'\f' },
	{ L("CR"),		'\015' },
	{ L("carriage-return"),	'\r' },
	{ L("SO"),		'\016' },
	{ L("SI"),		'\017' },
	{ L("DLE"),		'\020' },
	{ L("DC1"),		'\021' },
	{ L("DC2"),		'\022' },
	{ L("DC3"),		'\023' },
	{ L("DC4"),		'\024' },
	{ L("NAK"),		'\025' },
	{ L("SYN"),		'\026' },
	{ L("ETB"),		'\027' },
	{ L("CAN"),		'\030' },
	{ L("EM"),		'\031' },
	{ L("SUB"),		'\032' },
	{ L("ESC"),		'\033' },
	{ L("IS4"),		'\034' },
	{ L("FS"),		'\034' },
	{ L("IS3"),		'\035' },
	{ L("GS"),		'\035' },
	{ L("IS2"),		'\036' },
	{ L("RS"),		'\036' },
	{ L("IS1"),		'\037' },
	{ L("US"),		'\037' },
	{ L("space"),		' ' },
	{ L("exclamation-mark"),'!' },
	{ L("quotation-mark"),	'"' },
	{ L("number-sign"),	'#' },
	{ L("dollar-sign"),	'$' },
	{ L("percent-sign"),	'%' },
	{ L("ampersand"),	'&' },
	{ L("apostrophe"),	'\'' },
	{ L("left-parenthesis"),'(' },
	{ L("right-parenthesis"),')' },
	{ L("asterisk"),	'*' },
	{ L("plus-sign"),	'+' },
	{ L("comma"),		',' },
	{ L("hyphen"),		'-' },
	{ L("hyphen-minus"),	'-' },
	{ L("period"),		'.' },
	{ L("full-stop"),	'.' },
	{ L("slash"),		'/' },
	{ L("solidus"),		'/' },
	{ L("zero"),		'0' },
	{ L("one"),		'1' },
	{ L("two"),		'2' },
	{ L("three"),		'3' },
	{ L("four"),		'4' },
	{ L("five"),		'5' },
	{ L("six"),		'6' },
	{ L("seven"),		'7' },
	{ L("eight"),		'8' },
	{ L("nine"),		'9' },
	{ L("colon"),		':' },
	{ L("semicolon"),	';' },
	{ L("less-than-sign"),	'<' },
	{ L("equals-sign"),	'=' },
	{ L("greater-than-sign"),'>' },
	{ L("question-mark"),	'?' },
	{ L("commercial-at"),	'@' },
	{ L("left-square-bracket"),'[' },
	{ L("backslash"),	'\\' },
	{ L("reverse-solidus"),	'\\' },
	{ L("right-square-bracket"),']' },
	{ L("circumflex"),	'^' },
	{ L("circumflex-accent"),'^' },
	{ L("underscore"),	'_' },
	{ L("low-line"),	'_' },
	{ L("grave-accent"),	'`' },
	{ L("left-brace"),	'{' },
	{ L("left-curly-bracket"),'{' },
	{ L("vertical-line"),	'|' },
	{ L("right-brace"),	'}' },
	{ L("right-curly-bracket"),'}' },
	{ L("tilde"),		'~' },
	{ L("DEL"),		'\177' },
	{ NULL,			0 },
};

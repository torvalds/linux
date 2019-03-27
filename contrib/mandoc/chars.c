/*	$Id: chars.c,v 1.73 2017/08/23 13:01:29 schwarze Exp $ */
/*
 * Copyright (c) 2009, 2010, 2011 Kristaps Dzonsons <kristaps@bsd.lv>
 * Copyright (c) 2011, 2014, 2015, 2017 Ingo Schwarze <schwarze@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include "config.h"

#include <sys/types.h>

#include <assert.h>
#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "mandoc.h"
#include "mandoc_aux.h"
#include "mandoc_ohash.h"
#include "libmandoc.h"

struct	ln {
	const char	  roffcode[16];
	const char	 *ascii;
	int		  unicode;
};

/* Special break control characters. */
static const char ascii_nbrsp[2] = { ASCII_NBRSP, '\0' };
static const char ascii_break[2] = { ASCII_BREAK, '\0' };

static struct ln lines[] = {

	/* Spacing. */
	{ " ",			ascii_nbrsp,	0x00a0	},
	{ "~",			ascii_nbrsp,	0x00a0	},
	{ "0",			" ",		0x2002	},
	{ "|",			"",		0	},
	{ "^",			"",		0	},
	{ "&",			"",		0	},
	{ "%",			"",		0	},
	{ ":",			ascii_break,	0	},
	/* XXX The following three do not really belong here. */
	{ "t",			"",		0	},
	{ "c",			"",		0	},
	{ "}",			"",		0	},

	/* Lines. */
	{ "ba",			"|",		0x007c	},
	{ "br",			"|",		0x2502	},
	{ "ul",			"_",		0x005f	},
	{ "ru",			"_",		0x005f	},
	{ "rn",			"-",		0x203e	},
	{ "bb",			"|",		0x00a6	},
	{ "sl",			"/",		0x002f	},
	{ "rs",			"\\",		0x005c	},

	/* Text markers. */
	{ "ci",			"O",		0x25cb	},
	{ "bu",			"+\bo",		0x2022	},
	{ "dd",			"<**>",		0x2021	},
	{ "dg",			"<*>",		0x2020	},
	{ "lz",			"<>",		0x25ca	},
	{ "sq",			"[]",		0x25a1	},
	{ "ps",			"<paragraph>",	0x00b6	},
	{ "sc",			"<section>",	0x00a7	},
	{ "lh",			"<=",		0x261c	},
	{ "rh",			"=>",		0x261e	},
	{ "at",			"@",		0x0040	},
	{ "sh",			"#",		0x0023	},
	{ "CR",			"<cr>",		0x21b5	},
	{ "OK",			"\\/",		0x2713	},
	{ "CL",			"<club>",	0x2663	},
	{ "SP",			"<spade>",	0x2660	},
	{ "HE",			"<heart>",	0x2665	},
	{ "DI",			"<diamond>",	0x2666	},

	/* Legal symbols. */
	{ "co",			"(C)",		0x00a9	},
	{ "rg",			"(R)",		0x00ae	},
	{ "tm",			"tm",		0x2122	},

	/* Punctuation. */
	{ "em",			"--",		0x2014	},
	{ "en",			"-",		0x2013	},
	{ "hy",			"-",		0x2010	},
	{ "e",			"\\",		0x005c	},
	{ ".",			".",		0x002e	},
	{ "r!",			"!",		0x00a1	},
	{ "r?",			"?",		0x00bf	},

	/* Quotes. */
	{ "Bq",			",,",		0x201e	},
	{ "bq",			",",		0x201a	},
	{ "lq",			"\"",		0x201c	},
	{ "rq",			"\"",		0x201d	},
	{ "Lq",			"\"",		0x201c	},
	{ "Rq",			"\"",		0x201d	},
	{ "oq",			"`",		0x2018	},
	{ "cq",			"\'",		0x2019	},
	{ "aq",			"\'",		0x0027	},
	{ "dq",			"\"",		0x0022	},
	{ "Fo",			"<<",		0x00ab	},
	{ "Fc",			">>",		0x00bb	},
	{ "fo",			"<",		0x2039	},
	{ "fc",			">",		0x203a	},

	/* Brackets. */
	{ "lB",			"[",		0x005b	},
	{ "rB",			"]",		0x005d	},
	{ "lC",			"{",		0x007b	},
	{ "rC",			"}",		0x007d	},
	{ "la",			"<",		0x27e8	},
	{ "ra",			">",		0x27e9	},
	{ "bv",			"|",		0x23aa	},
	{ "braceex",		"|",		0x23aa	},
	{ "bracketlefttp",	"|",		0x23a1	},
	{ "bracketleftbt",	"|",		0x23a3	},
	{ "bracketleftex",	"|",		0x23a2	},
	{ "bracketrighttp",	"|",		0x23a4	},
	{ "bracketrightbt",	"|",		0x23a6	},
	{ "bracketrightex",	"|",		0x23a5	},
	{ "lt",			",-",		0x23a7	},
	{ "bracelefttp",	",-",		0x23a7	},
	{ "lk",			"{",		0x23a8	},
	{ "braceleftmid",	"{",		0x23a8	},
	{ "lb",			"`-",		0x23a9	},
	{ "braceleftbt",	"`-",		0x23a9	},
	{ "braceleftex",	"|",		0x23aa	},
	{ "rt",			"-.",		0x23ab	},
	{ "bracerighttp",	"-.",		0x23ab	},
	{ "rk",			"}",		0x23ac	},
	{ "bracerightmid",	"}",		0x23ac	},
	{ "rb",			"-\'",		0x23ad	},
	{ "bracerightbt",	"-\'",		0x23ad	},
	{ "bracerightex",	"|",		0x23aa	},
	{ "parenlefttp",	"/",		0x239b	},
	{ "parenleftbt",	"\\",		0x239d	},
	{ "parenleftex",	"|",		0x239c	},
	{ "parenrighttp",	"\\",		0x239e	},
	{ "parenrightbt",	"/",		0x23a0	},
	{ "parenrightex",	"|",		0x239f	},

	/* Arrows and lines. */
	{ "<-",			"<-",		0x2190	},
	{ "->",			"->",		0x2192	},
	{ "<>",			"<->",		0x2194	},
	{ "da",			"|\bv",		0x2193	},
	{ "ua",			"|\b^",		0x2191	},
	{ "va",			"^v",		0x2195	},
	{ "lA",			"<=",		0x21d0	},
	{ "rA",			"=>",		0x21d2	},
	{ "hA",			"<=>",		0x21d4	},
	{ "uA",			"=\b^",		0x21d1	},
	{ "dA",			"=\bv",		0x21d3	},
	{ "vA",			"^=v",		0x21d5	},
	{ "an",			"-",		0x23af	},

	/* Logic. */
	{ "AN",			"^",		0x2227	},
	{ "OR",			"v",		0x2228	},
	{ "no",			"~",		0x00ac	},
	{ "tno",		"~",		0x00ac	},
	{ "te",			"<there\037exists>", 0x2203 },
	{ "fa",			"<for\037all>",	0x2200	},
	{ "st",			"<such\037that>", 0x220b },
	{ "tf",			"<therefore>",	0x2234	},
	{ "3d",			"<therefore>",	0x2234	},
	{ "or",			"|",		0x007c	},

	/* Mathematicals. */
	{ "pl",			"+",		0x002b	},
	{ "mi",			"-",		0x2212	},
	{ "-",			"-",		0x002d	},
	{ "-+",			"-+",		0x2213	},
	{ "+-",			"+-",		0x00b1	},
	{ "t+-",		"+-",		0x00b1	},
	{ "pc",			".",		0x00b7	},
	{ "md",			".",		0x22c5	},
	{ "mu",			"x",		0x00d7	},
	{ "tmu",		"x",		0x00d7	},
	{ "c*",			"O\bx",		0x2297	},
	{ "c+",			"O\b+",		0x2295	},
	{ "di",			"/",		0x00f7	},
	{ "tdi",		"/",		0x00f7	},
	{ "f/",			"/",		0x2044	},
	{ "**",			"*",		0x2217	},
	{ "<=",			"<=",		0x2264	},
	{ ">=",			">=",		0x2265	},
	{ "<<",			"<<",		0x226a	},
	{ ">>",			">>",		0x226b	},
	{ "eq",			"=",		0x003d	},
	{ "!=",			"!=",		0x2260	},
	{ "==",			"==",		0x2261	},
	{ "ne",			"!==",		0x2262	},
	{ "ap",			"~",		0x223c	},
	{ "|=",			"-~",		0x2243	},
	{ "=~",			"=~",		0x2245	},
	{ "~~",			"~~",		0x2248	},
	{ "~=",			"~=",		0x2248	},
	{ "pt",			"<proportional\037to>", 0x221d },
	{ "es",			"{}",		0x2205	},
	{ "mo",			"<element\037of>", 0x2208 },
	{ "nm",			"<not\037element\037of>", 0x2209 },
	{ "sb",			"<proper\037subset>", 0x2282 },
	{ "nb",			"<not\037subset>", 0x2284 },
	{ "sp",			"<proper\037superset>", 0x2283 },
	{ "nc",			"<not\037superset>", 0x2285 },
	{ "ib",			"<subset\037or\037equal>", 0x2286 },
	{ "ip",			"<superset\037or\037equal>", 0x2287 },
	{ "ca",			"<intersection>", 0x2229 },
	{ "cu",			"<union>",	0x222a	},
	{ "/_",			"<angle>",	0x2220	},
	{ "pp",			"<perpendicular>", 0x22a5 },
	{ "is",			"<integral>",	0x222b	},
	{ "integral",		"<integral>",	0x222b	},
	{ "sum",		"<sum>",	0x2211	},
	{ "product",		"<product>",	0x220f	},
	{ "coproduct",		"<coproduct>",	0x2210	},
	{ "gr",			"<nabla>",	0x2207	},
	{ "sr",			"<sqrt>",	0x221a	},
	{ "sqrt",		"<sqrt>",	0x221a	},
	{ "lc",			"|~",		0x2308	},
	{ "rc",			"~|",		0x2309	},
	{ "lf",			"|_",		0x230a	},
	{ "rf",			"_|",		0x230b	},
	{ "if",			"<infinity>",	0x221e	},
	{ "Ah",			"<Aleph>",	0x2135	},
	{ "Im",			"<Im>",		0x2111	},
	{ "Re",			"<Re>",		0x211c	},
	{ "wp",			"P",		0x2118	},
	{ "pd",			"<del>",	0x2202	},
	{ "-h",			"/h",		0x210f	},
	{ "hbar",		"/h",		0x210f	},
	{ "12",			"1/2",		0x00bd	},
	{ "14",			"1/4",		0x00bc	},
	{ "34",			"3/4",		0x00be	},
	{ "18",			"1/8",		0x215B	},
	{ "38",			"3/8",		0x215C	},
	{ "58",			"5/8",		0x215D	},
	{ "78",			"7/8",		0x215E	},
	{ "S1",			"^1",		0x00B9	},
	{ "S2",			"^2",		0x00B2	},
	{ "S3",			"^3",		0x00B3	},

	/* Ligatures. */
	{ "ff",			"ff",		0xfb00	},
	{ "fi",			"fi",		0xfb01	},
	{ "fl",			"fl",		0xfb02	},
	{ "Fi",			"ffi",		0xfb03	},
	{ "Fl",			"ffl",		0xfb04	},
	{ "AE",			"AE",		0x00c6	},
	{ "ae",			"ae",		0x00e6	},
	{ "OE",			"OE",		0x0152	},
	{ "oe",			"oe",		0x0153	},
	{ "ss",			"ss",		0x00df	},
	{ "IJ",			"IJ",		0x0132	},
	{ "ij",			"ij",		0x0133	},

	/* Accents. */
	{ "a\"",		"\"",		0x02dd	},
	{ "a-",			"-",		0x00af	},
	{ "a.",			".",		0x02d9	},
	{ "a^",			"^",		0x005e	},
	{ "aa",			"\'",		0x00b4	},
	{ "\'",			"\'",		0x00b4	},
	{ "ga",			"`",		0x0060	},
	{ "`",			"`",		0x0060	},
	{ "ab",			"'\b`",		0x02d8	},
	{ "ac",			",",		0x00b8	},
	{ "ad",			"\"",		0x00a8	},
	{ "ah",			"v",		0x02c7	},
	{ "ao",			"o",		0x02da	},
	{ "a~",			"~",		0x007e	},
	{ "ho",			",",		0x02db	},
	{ "ha",			"^",		0x005e	},
	{ "ti",			"~",		0x007e	},

	/* Accented letters. */
	{ "'A",			"'\bA",		0x00c1	},
	{ "'E",			"'\bE",		0x00c9	},
	{ "'I",			"'\bI",		0x00cd	},
	{ "'O",			"'\bO",		0x00d3	},
	{ "'U",			"'\bU",		0x00da	},
	{ "'a",			"'\ba",		0x00e1	},
	{ "'e",			"'\be",		0x00e9	},
	{ "'i",			"'\bi",		0x00ed	},
	{ "'o",			"'\bo",		0x00f3	},
	{ "'u",			"'\bu",		0x00fa	},
	{ "`A",			"`\bA",		0x00c0	},
	{ "`E",			"`\bE",		0x00c8	},
	{ "`I",			"`\bI",		0x00cc	},
	{ "`O",			"`\bO",		0x00d2	},
	{ "`U",			"`\bU",		0x00d9	},
	{ "`a",			"`\ba",		0x00e0	},
	{ "`e",			"`\be",		0x00e8	},
	{ "`i",			"`\bi",		0x00ec	},
	{ "`o",			"`\bo",		0x00f2	},
	{ "`u",			"`\bu",		0x00f9	},
	{ "~A",			"~\bA",		0x00c3	},
	{ "~N",			"~\bN",		0x00d1	},
	{ "~O",			"~\bO",		0x00d5	},
	{ "~a",			"~\ba",		0x00e3	},
	{ "~n",			"~\bn",		0x00f1	},
	{ "~o",			"~\bo",		0x00f5	},
	{ ":A",			"\"\bA",	0x00c4	},
	{ ":E",			"\"\bE",	0x00cb	},
	{ ":I",			"\"\bI",	0x00cf	},
	{ ":O",			"\"\bO",	0x00d6	},
	{ ":U",			"\"\bU",	0x00dc	},
	{ ":a",			"\"\ba",	0x00e4	},
	{ ":e",			"\"\be",	0x00eb	},
	{ ":i",			"\"\bi",	0x00ef	},
	{ ":o",			"\"\bo",	0x00f6	},
	{ ":u",			"\"\bu",	0x00fc	},
	{ ":y",			"\"\by",	0x00ff	},
	{ "^A",			"^\bA",		0x00c2	},
	{ "^E",			"^\bE",		0x00ca	},
	{ "^I",			"^\bI",		0x00ce	},
	{ "^O",			"^\bO",		0x00d4	},
	{ "^U",			"^\bU",		0x00db	},
	{ "^a",			"^\ba",		0x00e2	},
	{ "^e",			"^\be",		0x00ea	},
	{ "^i",			"^\bi",		0x00ee	},
	{ "^o",			"^\bo",		0x00f4	},
	{ "^u",			"^\bu",		0x00fb	},
	{ ",C",			",\bC",		0x00c7	},
	{ ",c",			",\bc",		0x00e7	},
	{ "/L",			"/\bL",		0x0141	},
	{ "/l",			"/\bl",		0x0142	},
	{ "/O",			"/\bO",		0x00d8	},
	{ "/o",			"/\bo",		0x00f8	},
	{ "oA",			"o\bA",		0x00c5	},
	{ "oa",			"o\ba",		0x00e5	},

	/* Special letters. */
	{ "-D",			"Dh",		0x00d0	},
	{ "Sd",			"dh",		0x00f0	},
	{ "TP",			"Th",		0x00de	},
	{ "Tp",			"th",		0x00fe	},
	{ ".i",			"i",		0x0131	},
	{ ".j",			"j",		0x0237	},

	/* Currency. */
	{ "Do",			"$",		0x0024	},
	{ "ct",			"/\bc",		0x00a2	},
	{ "Eu",			"EUR",		0x20ac	},
	{ "eu",			"EUR",		0x20ac	},
	{ "Ye",			"=\bY",		0x00a5	},
	{ "Po",			"GBP",		0x00a3	},
	{ "Cs",			"o\bx",		0x00a4	},
	{ "Fn",			",\bf",		0x0192	},

	/* Units. */
	{ "de",			"<degree>",	0x00b0	},
	{ "%0",			"<permille>",	0x2030	},
	{ "fm",			"\'",		0x2032	},
	{ "sd",			"''",		0x2033	},
	{ "mc",			"<micro>",	0x00b5	},
	{ "Of",			"_\ba",		0x00aa	},
	{ "Om",			"_\bo",		0x00ba	},

	/* Greek characters. */
	{ "*A",			"A",		0x0391	},
	{ "*B",			"B",		0x0392	},
	{ "*G",			"<Gamma>",	0x0393	},
	{ "*D",			"<Delta>",	0x0394	},
	{ "*E",			"E",		0x0395	},
	{ "*Z",			"Z",		0x0396	},
	{ "*Y",			"H",		0x0397	},
	{ "*H",			"<Theta>",	0x0398	},
	{ "*I",			"I",		0x0399	},
	{ "*K",			"K",		0x039a	},
	{ "*L",			"<Lambda>",	0x039b	},
	{ "*M",			"M",		0x039c	},
	{ "*N",			"N",		0x039d	},
	{ "*C",			"<Xi>",		0x039e	},
	{ "*O",			"O",		0x039f	},
	{ "*P",			"<Pi>",		0x03a0	},
	{ "*R",			"P",		0x03a1	},
	{ "*S",			"<Sigma>",	0x03a3	},
	{ "*T",			"T",		0x03a4	},
	{ "*U",			"Y",		0x03a5	},
	{ "*F",			"<Phi>",	0x03a6	},
	{ "*X",			"X",		0x03a7	},
	{ "*Q",			"<Psi>",	0x03a8	},
	{ "*W",			"<Omega>",	0x03a9	},
	{ "*a",			"<alpha>",	0x03b1	},
	{ "*b",			"<beta>",	0x03b2	},
	{ "*g",			"<gamma>",	0x03b3	},
	{ "*d",			"<delta>",	0x03b4	},
	{ "*e",			"<epsilon>",	0x03b5	},
	{ "*z",			"<zeta>",	0x03b6	},
	{ "*y",			"<eta>",	0x03b7	},
	{ "*h",			"<theta>",	0x03b8	},
	{ "*i",			"<iota>",	0x03b9	},
	{ "*k",			"<kappa>",	0x03ba	},
	{ "*l",			"<lambda>",	0x03bb	},
	{ "*m",			"<mu>",		0x03bc	},
	{ "*n",			"<nu>",		0x03bd	},
	{ "*c",			"<xi>",		0x03be	},
	{ "*o",			"o",		0x03bf	},
	{ "*p",			"<pi>",		0x03c0	},
	{ "*r",			"<rho>",	0x03c1	},
	{ "*s",			"<sigma>",	0x03c3	},
	{ "*t",			"<tau>",	0x03c4	},
	{ "*u",			"<upsilon>",	0x03c5	},
	{ "*f",			"<phi>",	0x03d5	},
	{ "*x",			"<chi>",	0x03c7	},
	{ "*q",			"<psi>",	0x03c8	},
	{ "*w",			"<omega>",	0x03c9	},
	{ "+h",			"<theta>",	0x03d1	},
	{ "+f",			"<phi>",	0x03c6	},
	{ "+p",			"<pi>",		0x03d6	},
	{ "+e",			"<epsilon>",	0x03f5	},
	{ "ts",			"<sigma>",	0x03c2	},
};

static	struct ohash	  mchars;


void
mchars_free(void)
{

	ohash_delete(&mchars);
}

void
mchars_alloc(void)
{
	size_t		  i;
	unsigned int	  slot;

	mandoc_ohash_init(&mchars, 9, offsetof(struct ln, roffcode));
	for (i = 0; i < sizeof(lines)/sizeof(lines[0]); i++) {
		slot = ohash_qlookup(&mchars, lines[i].roffcode);
		assert(ohash_find(&mchars, slot) == NULL);
		ohash_insert(&mchars, slot, lines + i);
	}
}

int
mchars_spec2cp(const char *p, size_t sz)
{
	const struct ln	*ln;
	const char	*end;

	end = p + sz;
	ln = ohash_find(&mchars, ohash_qlookupi(&mchars, p, &end));
	return ln != NULL ? ln->unicode : sz == 1 ? (unsigned char)*p : -1;
}

int
mchars_num2char(const char *p, size_t sz)
{
	int	  i;

	i = mandoc_strntoi(p, sz, 10);
	return i >= 0 && i < 256 ? i : -1;
}

int
mchars_num2uc(const char *p, size_t sz)
{
	int	 i;

	i = mandoc_strntoi(p, sz, 16);
	assert(i >= 0 && i <= 0x10FFFF);
	return i;
}

const char *
mchars_spec2str(const char *p, size_t sz, size_t *rsz)
{
	const struct ln	*ln;
	const char	*end;

	end = p + sz;
	ln = ohash_find(&mchars, ohash_qlookupi(&mchars, p, &end));
	if (ln == NULL) {
		*rsz = 1;
		return sz == 1 ? p : NULL;
	}

	*rsz = strlen(ln->ascii);
	return ln->ascii;
}

const char *
mchars_uc2str(int uc)
{
	size_t	  i;

	for (i = 0; i < sizeof(lines)/sizeof(lines[0]); i++)
		if (uc == lines[i].unicode)
			return lines[i].ascii;
	return "<?>";
}

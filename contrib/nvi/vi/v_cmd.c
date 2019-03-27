/*-
 * Copyright (c) 1992, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1992, 1993, 1994, 1995, 1996
 *	Keith Bostic.  All rights reserved.
 *
 * See the LICENSE file for redistribution information.
 */

#include "config.h"

#ifndef lint
static const char sccsid[] = "$Id: v_cmd.c,v 10.9 1996/03/28 15:18:39 bostic Exp $";
#endif /* not lint */

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/time.h>

#include <bitstring.h>
#include <limits.h>
#include <stdio.h>

#include "../common/common.h"
#include "vi.h"

/*
 * This array maps keystrokes to vi command functions.  It is known
 * in ex/ex_usage.c that it takes four columns to name a vi character.
 */
VIKEYS const vikeys [MAXVIKEY + 1] = {
/* 000 NUL -- The code in vi.c expects key 0 to be undefined. */
	{NULL},
/* 001  ^A */
	{v_searchw,	V_ABS|V_CNT|V_MOVE|V_KEYW|VM_CUTREQ|VM_RCM_SET,
	    "[count]^A",
	    "^A search forward for cursor word"},
/* 002  ^B */
	{v_pageup,	V_CNT|VM_RCM_SET,
	    "[count]^B",
	    "^B scroll up by screens"},
/* 003  ^C */
	{NULL,		0,
	    "^C",
	    "^C interrupt an operation (e.g. read, write, search)"},
/* 004  ^D */
	{v_hpagedown,	V_CNT|VM_RCM_SET,
	    "[count]^D",
	    "^D scroll down by half screens (setting count)"},
/* 005  ^E */
	{v_linedown,	V_CNT,
	    "[count]^E",
	    "^E scroll down by lines"},
/* 006  ^F */
	{v_pagedown,	V_CNT|VM_RCM_SET,
	    "[count]^F",
	    "^F scroll down by screens"},
/* 007  ^G */
	{v_status,	0,
	    "^G",
	    "^G file status"},
/* 010  ^H */
	{v_left,	V_CNT|V_MOVE|VM_RCM_SET,
	    "[count]^H",
	    "^H move left by characters"},
/* 011  ^I */
	{NULL},
/* 012  ^J */
	{v_down,	V_CNT|V_MOVE|VM_LMODE|VM_RCM,
	    "[count]^J",
	    "^J move down by lines"},
/* 013  ^K */
	{NULL},
/* 014  ^L */
	{v_redraw,	0,
	    "^L",
	    "^L redraw screen"},
/* 015  ^M */
	{v_cr,		V_CNT|V_MOVE|VM_LMODE|VM_RCM_SETFNB,
	    "[count]^M",
	    "^M move down by lines (to first non-blank)"},
/* 016  ^N */
	{v_down,	V_CNT|V_MOVE|VM_LMODE|VM_RCM,
	    "[count]^N",
	    "^N move down by lines"},
/* 017  ^O */
	{NULL},
/* 020  ^P */
	{v_up,		V_CNT|V_MOVE|VM_LMODE|VM_RCM,
	    "[count]^P",
	    "^P move up by lines"},
/* 021  ^Q -- same as ^V if not used for hardware flow control. */
	{NULL},
/* 022  ^R */
	{v_redraw,	0,
	    "^R",
	    "^R redraw screen"},
/* 023  ^S -- not available, used for hardware flow control. */
	{NULL},
/* 024  ^T */
	{v_tagpop,	V_ABS|VM_RCM_SET,
	    "^T",
	    "^T tag pop"},
/* 025  ^U */
	{v_hpageup,	V_CNT|VM_RCM_SET,
	    "[count]^U",
	    "^U half page up (set count)"},
/* 026  ^V */
	{NULL,		0,
	    "^V",
	    "^V input a literal character"},
/* 027  ^W */
	{v_screen,	0,
	    "^W",
	    "^W move to next screen"},
/* 030  ^X */
	{NULL},
/* 031  ^Y */
	{v_lineup,	V_CNT,
	    "[count]^Y",
	    "^Y page up by lines"},
/* 032  ^Z */
	{v_suspend,	V_SECURE,
	    "^Z",
	    "^Z suspend editor"},
/* 033  ^[ */
	{NULL,		0,
	    "^[ <escape>",
	    "^[ <escape> exit input mode, cancel partial commands"},
/* 034  ^\ */
	{v_exmode,	0,
	    "^\\",
	    "^\\ switch to ex mode"},
/* 035  ^] */
	{v_tagpush,	V_ABS|V_KEYW|VM_RCM_SET,
	    "^]",
	    "^] tag push cursor word"},
/* 036  ^^ */
	{v_switch,	0,
	    "^^",
	    "^^ switch to previous file"},
/* 037  ^_ */
	{NULL},
/* 040 ' ' */
	{v_right,	V_CNT|V_MOVE|VM_RCM_SET,
	    "[count]' '",
	    "   <space> move right by columns"},
/* 041   ! */
	{v_filter,	V_CNT|V_DOT|V_MOTION|V_SECURE|VM_RCM_SET,
	    "[count]![count]motion command(s)",
	    " ! filter through command(s) to motion"},
/* 042   " */
	{NULL},
/* 043   # */
	{v_increment,	V_CHAR|V_CNT|V_DOT|VM_RCM_SET,
	    "[count]# +|-|#",
	    " # number increment/decrement"},
/* 044   $ */
	{v_dollar,	V_CNT|V_MOVE|VM_RCM_SETLAST,
	    " [count]$",
	    " $ move to last column"},
/* 045   % */
	{v_match,	V_ABS|V_CNT|V_MOVE|VM_CUTREQ|VM_RCM_SET,
	    "%",
	    " % move to match"},
/* 046   & */
	{v_again,	0,
	    "&",
	    " & repeat substitution"},
/* 047   ' */
	{v_fmark,	V_ABS_L|V_CHAR|V_MOVE|VM_LMODE|VM_RCM_SET,
	    "'['a-z]",
	    " ' move to mark (to first non-blank)"},
/* 050   ( */
	{v_sentenceb,	V_ABS|V_CNT|V_MOVE|VM_CUTREQ|VM_RCM_SET,
	    "[count](",
	    " ( move back sentence"},
/* 051   ) */
	{v_sentencef,	V_ABS|V_CNT|V_MOVE|VM_CUTREQ|VM_RCM_SET,
	    "[count])",
	    " ) move forward sentence"},
/* 052   * */
	{NULL},
/* 053   + */
	{v_down,	V_CNT|V_MOVE|VM_LMODE|VM_RCM_SETFNB,
	    "[count]+",
	    " + move down by lines (to first non-blank)"},
/* 054   , */
	{v_chrrepeat,	V_CNT|V_MOVE|VM_RCM_SET,
	    "[count],",
	    " , reverse last F, f, T or t search"},
/* 055   - */
	{v_up,		V_CNT|V_MOVE|VM_LMODE|VM_RCM_SETFNB,
	    "[count]-",
	    " - move up by lines (to first non-blank)"},
/* 056   . */
	{NULL,		0,
	    ".",
	    " . repeat the last command"},
/* 057   / */
	{v_searchf,	V_ABS_C|V_MOVE|VM_CUTREQ|VM_RCM_SET,
	    "/RE[/ offset]",
	    " / search forward"},
/* 060   0 */
	{v_zero,	V_MOVE|VM_RCM_SET,
	    "0",
	    " 0 move to first character"},
/* 061   1 */
	{NULL},
/* 062   2 */
	{NULL},
/* 063   3 */
	{NULL},
/* 064   4 */
	{NULL},
/* 065   5 */
	{NULL},
/* 066   6 */
	{NULL},
/* 067   7 */
	{NULL},
/* 070   8 */
	{NULL},
/* 071   9 */
	{NULL},
/* 072   : */
	{v_ex,		0,
	    ":command [| command] ...",
	    " : ex command"},
/* 073   ; */
	{v_chrepeat,	V_CNT|V_MOVE|VM_RCM_SET,
	    "[count];",
	    " ; repeat last F, f, T or t search"},
/* 074   < */
	{v_shiftl,	V_CNT|V_DOT|V_MOTION|VM_RCM_SET,
	    "[count]<[count]motion",
	    " < shift lines left to motion"},
/* 075   = */
	{NULL},
/* 076   > */
	{v_shiftr,	V_CNT|V_DOT|V_MOTION|VM_RCM_SET,
	    "[count]>[count]motion",
	    " > shift lines right to motion"},
/* 077   ? */
	{v_searchb,	V_ABS_C|V_MOVE|VM_CUTREQ|VM_RCM_SET,
	    "?RE[? offset]",
	    " ? search backward"},
/* 100   @ */
	{v_at,		V_CNT|V_RBUF|VM_RCM_SET,
	    "@buffer",
	    " @ execute buffer"},
/* 101   A */
	{v_iA,		V_CNT|V_DOT|VM_RCM_SET,
	    "[count]A",
	    " A append to the line"},
/* 102   B */
	{v_wordB,	V_CNT|V_MOVE|VM_RCM_SET,
	    "[count]B",
	    " B move back bigword"},
/* 103   C */
	{NULL,		0,
	    "[buffer][count]C",
	    " C change to end-of-line"},
/* 104   D */
	{NULL,		0,
	    "[buffer]D",
	    " D delete to end-of-line"},
/* 105   E */
	{v_wordE,	V_CNT|V_MOVE|VM_RCM_SET,
	    "[count]E",
	    " E move to end of bigword"},
/* 106   F */
	{v_chF,		V_CHAR|V_CNT|V_MOVE|VM_RCM_SET,
	    "[count]F character",
	    " F character in line backward search"},
/* 107   G */
	{v_lgoto,	V_ABS_L|V_CNT|V_MOVE|VM_LMODE|VM_RCM_SETFNB,
	    "[count]G",
	    " G move to line"},
/* 110   H */
	{v_home,	V_ABS_L|V_CNT|V_MOVE|VM_LMODE|VM_RCM_SETNNB,
	    "[count]H",
	    " H move to count lines from screen top"},
/* 111   I */
	{v_iI,		V_CNT|V_DOT|VM_RCM_SET,
	    "[count]I",
	    " I insert before first nonblank"},
/* 112   J */
	{v_join,	V_CNT|V_DOT|VM_RCM_SET,
	    "[count]J",
	    " J join lines"},
/* 113   K */
	{NULL},
/* 114   L */
	{v_bottom,	V_ABS_L|V_CNT|V_MOVE|VM_LMODE|VM_RCM_SETNNB,
	    "[count]L",
	    " L move to screen bottom"},
/* 115   M */
	{v_middle,	V_ABS_L|V_CNT|V_MOVE|VM_LMODE|VM_RCM_SETNNB,
	    "M",
	    " M move to screen middle"},
/* 116   N */
	{v_searchN,	V_ABS_C|V_MOVE|VM_CUTREQ|VM_RCM_SET,
	    "n",
	    " N reverse last search"},
/* 117   O */
	{v_iO,		V_CNT|V_DOT|VM_RCM_SET,
	    "[count]O",
	    " O insert above line"},
/* 120   P */
	{v_Put,		V_CNT|V_DOT|V_OBUF|VM_RCM_SET,
	    "[buffer]P",
	    " P insert before cursor from buffer"},
/* 121   Q */
	{v_exmode,	0,
	    "Q",
	    " Q switch to ex mode"},
/* 122   R */
	{v_Replace,	V_CNT|V_DOT|VM_RCM_SET,
	    "[count]R",
	    " R replace characters"},
/* 123   S */
	{NULL,		0,
	    "[buffer][count]S",
	    " S substitute for the line(s)"},
/* 124   T */
	{v_chT,		V_CHAR|V_CNT|V_MOVE|VM_RCM_SET,
	    "[count]T character",
	    " T before character in line backward search"},
/* 125   U */
	{v_Undo,	VM_RCM_SET,
	    "U",
	    " U Restore the current line"},
/* 126   V */
	{NULL},
/* 127   W */
	{v_wordW,	V_CNT|V_MOVE|VM_RCM_SET,
	    "[count]W",
	    " W move to next bigword"},
/* 130   X */
	{v_Xchar,	V_CNT|V_DOT|V_OBUF|VM_RCM_SET,
	    "[buffer][count]X",
	    " X delete character before cursor"},
/* 131   Y */
	{NULL,		0,
	    "[buffer][count]Y",
	    " Y copy line"},
/* 132   Z */
	{v_zexit,	0,
	    "ZZ",
	    "ZZ save file and exit"},
/* 133   [ */
	{v_sectionb,	V_ABS|V_CNT|V_MOVE|VM_RCM_SET,
	    "[[",
	    "[[ move back section"},
/* 134   \ */
	{NULL},
/* 135   ] */
	{v_sectionf,	V_ABS|V_CNT|V_MOVE|VM_RCM_SET,
	    "]]",
	    "]] move forward section"},
/* 136   ^ */
	/*
	 * DON'T set the VM_RCM_SETFNB flag, the function has to do the work
	 * anyway, in case it's a motion component.  DO set VM_RCM_SET, so
	 * that any motion that's part of a command is preserved.
	 */
	{v_first,	V_CNT|V_MOVE|VM_RCM_SET,
	    "^",
	    " ^ move to first non-blank"},
/* 137   _ */
	/*
	 * Needs both to set the VM_RCM_SETFNB flag, and to do the work
	 * in the function, in case it's a delete.
	 */
	{v_cfirst,	V_CNT|V_MOVE|VM_RCM_SETFNB,
	    "_",
	    " _ move to first non-blank"},
/* 140   ` */
	{v_bmark,	V_ABS_C|V_CHAR|V_MOVE|VM_CUTREQ|VM_RCM_SET,
	    "`[`a-z]",
	    " ` move to mark"},
/* 141   a */
	{v_ia,		V_CNT|V_DOT|VM_RCM_SET,
	    "[count]a",
	    " a append after cursor"},
/* 142   b */
	{v_wordb,	V_CNT|V_MOVE|VM_RCM_SET,
	    "[count]b",
	    " b move back word"},
/* 143   c */
	{v_change,	V_CNT|V_DOT|V_MOTION|V_OBUF|VM_RCM_SET,
	    "[buffer][count]c[count]motion",
	    " c change to motion"},
/* 144   d */
	{v_delete,	V_CNT|V_DOT|V_MOTION|V_OBUF|VM_RCM_SET,
	    "[buffer][count]d[count]motion",
	    " d delete to motion"},
/* 145   e */
	{v_worde,	V_CNT|V_MOVE|VM_RCM_SET,
	    "[count]e",
	    " e move to end of word"},
/* 146   f */
	{v_chf,		V_CHAR|V_CNT|V_MOVE|VM_RCM_SET,
	    "[count]f character",
	    " f character in line forward search"},
/* 147   g */
	{NULL},
/* 150   h */
	{v_left,	V_CNT|V_MOVE|VM_RCM_SET,
	    "[count]h",
	    " h move left by columns"},
/* 151   i */
	{v_ii,		V_CNT|V_DOT|VM_RCM_SET,
	    "[count]i",
	    " i insert before cursor"},
/* 152   j */
	{v_down,	V_CNT|V_MOVE|VM_LMODE|VM_RCM,
	    "[count]j",
	    " j move down by lines"},
/* 153   k */
	{v_up,		V_CNT|V_MOVE|VM_LMODE|VM_RCM,
	    "[count]k",
	    " k move up by lines"},
/* 154   l */
	{v_right,	V_CNT|V_MOVE|VM_RCM_SET,
	    "[count]l",
	    " l move right by columns"},
/* 155   m */
	{v_mark,	V_CHAR,
	    "m[a-z]",
	    " m set mark"},
/* 156   n */
	{v_searchn,	V_ABS_C|V_MOVE|VM_CUTREQ|VM_RCM_SET,
	    "n",
	    " n repeat last search"},
/* 157   o */
	{v_io,		V_CNT|V_DOT|VM_RCM_SET,
	    "[count]o",
	    " o append after line"},
/* 160   p */
	{v_put,		V_CNT|V_DOT|V_OBUF|VM_RCM_SET,
	    "[buffer]p",
	    " p insert after cursor from buffer"},
/* 161   q */
	{NULL},
/* 162   r */
	{v_replace,	V_CNT|V_DOT|VM_RCM_SET,
	    "[count]r character",
	    " r replace character"},
/* 163   s */
	{v_subst,	V_CNT|V_DOT|V_OBUF|VM_RCM_SET,
	    "[buffer][count]s",
	    " s substitute character"},
/* 164   t */
	{v_cht,		V_CHAR|V_CNT|V_MOVE|VM_RCM_SET,
	    "[count]t character",
	    " t before character in line forward search"},
/* 165   u */
	/*
	 * DON'T set the V_DOT flag, it' more complicated than that.
	 * See vi/vi.c for details.
	 */
	{v_undo,	VM_RCM_SET,
	    "u",
	    " u undo last change"},
/* 166   v */
	{NULL},
/* 167   w */
	{v_wordw,	V_CNT|V_MOVE|VM_RCM_SET,
	    "[count]w",
	    " w move to next word"},
/* 170   x */
	{v_xchar,	V_CNT|V_DOT|V_OBUF|VM_RCM_SET,
	    "[buffer][count]x",
	    " x delete character"},
/* 171   y */
	{v_yank,	V_CNT|V_DOT|V_MOTION|V_OBUF|VM_RCM_SET,
	    "[buffer][count]y[count]motion",
	    " y copy text to motion into a cut buffer"},
/* 172   z */
	/*
	 * DON'T set the V_CHAR flag, the char isn't required,
	 * so it's handled specially in getcmd().
	 */
	{v_z, 		V_ABS_L|V_CNT|VM_RCM_SETFNB,
	    "[line]z[window_size][-|.|+|^|<CR>]",
	    " z reposition the screen"},
/* 173   { */
	{v_paragraphb,	V_ABS|V_CNT|V_MOVE|VM_CUTREQ|VM_RCM_SET,
	    "[count]{",
	    " { move back paragraph"},
/* 174   | */
	{v_ncol,	V_CNT|V_MOVE|VM_RCM_SET,
	    "[count]|",
	    " | move to column"},
/* 175   } */
	{v_paragraphf,	V_ABS|V_CNT|V_MOVE|VM_CUTREQ|VM_RCM_SET,
	    "[count]}",
	    " } move forward paragraph"},
/* 176   ~ */
	{v_ulcase,	V_CNT|V_DOT|VM_RCM_SET,
	    "[count]~",
	    " ~ reverse case"},
};

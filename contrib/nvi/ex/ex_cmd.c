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
static const char sccsid[] = "$Id: ex_cmd.c,v 10.26 2011/07/14 15:11:16 zy Exp $";
#endif /* not lint */

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/time.h>

#include <bitstring.h>
#include <limits.h>
#include <stdio.h>

#include "../common/common.h"

/*
 * This array maps ex command names to command functions.
 *
 * The order in which command names are listed below is important --
 * ambiguous abbreviations are resolved to be the first possible match,
 * e.g. "r" means "read", not "rewind", because "read" is listed before
 * "rewind".
 *
 * The syntax of the ex commands is unbelievably irregular, and a special
 * case from beginning to end.  Each command has an associated "syntax
 * script" which describes the "arguments" that are possible.  The script
 * syntax is as follows:
 *
 *	!		-- ! flag
 *	1		-- flags: [+-]*[pl#][+-]*
 *	2		-- flags: [-.+^]
 *	3		-- flags: [-.+^=]
 *	b		-- buffer
 *	c[01+a]		-- count (0-N, 1-N, signed 1-N, address offset)
 *	f[N#][or]	-- file (a number or N, optional or required)
 *	l		-- line
 *	S		-- string with file name expansion
 *	s		-- string
 *	W		-- word string
 *	w[N#][or]	-- word (a number or N, optional or required)
 */
EXCMDLIST const cmds[] = {
/* C_SCROLL */
	{L("\004"),	ex_pr,		E_ADDR2,
	    "",
	    "^D",
	    "scroll lines"},
/* C_BANG */
	{L("!"),		ex_bang,	E_ADDR2_NONE|E_SECURE,
	    "S",
	    "[line [,line]] ! command",
	    "filter lines through commands or run commands"},
/* C_HASH */
	{L("#"),		ex_number,	E_ADDR2|E_CLRFLAG,
	    "ca1",
	    "[line [,line]] # [count] [l]",
	    "display numbered lines"},
/* C_SUBAGAIN */
	{L("&"),		ex_subagain,	E_ADDR2|E_ADDR_ZERO,
	    "s",
	    "[line [,line]] & [cgr] [count] [#lp]",
	    "repeat the last subsitution"},
/* C_STAR */
	{L("*"),		ex_at,		0,
	    "b",
	    "* [buffer]",
	    "execute a buffer"},
/* C_SHIFTL */
	{L("<"),		ex_shiftl,	E_ADDR2|E_AUTOPRINT,
	    "ca1",
	    "[line [,line]] <[<...] [count] [flags]",
	    "shift lines left"},
/* C_EQUAL */
	{L("="),		ex_equal,	E_ADDR1|E_ADDR_ZERO|E_ADDR_ZERODEF,
	    "1",
	    "[line] = [flags]",
	    "display line number"},
/* C_SHIFTR */
	{L(">"),		ex_shiftr,	E_ADDR2|E_AUTOPRINT,
	    "ca1",
	    "[line [,line]] >[>...] [count] [flags]",
	    "shift lines right"},
/* C_AT */
	{L("@"),		ex_at,		E_ADDR2,
	    "b",
	    "@ [buffer]",
	    "execute a buffer"},
/* C_APPEND */
	{L("append"),	ex_append,	E_ADDR1|E_ADDR_ZERO|E_ADDR_ZERODEF,
	    "!",
	    "[line] a[ppend][!]",
	    "append input to a line"},
/* C_ABBR */
	{L("abbreviate"), 	ex_abbr,	0,
	    "W",
	    "ab[brev] [word replace]",
	    "specify an input abbreviation"},
/* C_ARGS */
	{L("args"),	ex_args,	0,
	    "",
	    "ar[gs]",
	    "display file argument list"},
/* C_BG */
	{L("bg"),		ex_bg,		E_VIONLY,
	    "",
	    "bg",
	    "put a foreground screen into the background"},
/* C_CHANGE */
	{L("change"),	ex_change,	E_ADDR2|E_ADDR_ZERODEF,
	    "!ca",
	    "[line [,line]] c[hange][!] [count]",
	    "change lines to input"},
/* C_CD */
	{L("cd"),		ex_cd,		0,
	    "!f1o",
	    "cd[!] [directory]",
	    "change the current directory"},
/* C_CHDIR */
	{L("chdir"),	ex_cd,		0,
	    "!f1o",
	    "chd[ir][!] [directory]",
	    "change the current directory"},
/* C_COPY */
	{L("copy"),	ex_copy,	E_ADDR2|E_AUTOPRINT,
	    "l1",
	    "[line [,line]] co[py] line [flags]",
	    "copy lines elsewhere in the file"},
/* C_CSCOPE */
	{L("cscope"),      ex_cscope,      0,
	    "!s",
	    "cs[cope] command [args]",
	    "create a set of tags using a cscope command"},
/*
 * !!!
 * Adding new commands starting with 'd' may break the delete command code
 * in ex_cmd() (the ex parser).  Read through the comments there, first.
 */
/* C_DELETE */
	{L("delete"),	ex_delete,	E_ADDR2|E_AUTOPRINT,
	    "bca1",
	    "[line [,line]] d[elete][flags] [buffer] [count] [flags]",
	    "delete lines from the file"},
/* C_DISPLAY */
	{L("display"),	ex_display,	0,
	    "w1r",
	    "display b[uffers] | c[onnections] | s[creens] | t[ags]",
	    "display buffers, connections, screens or tags"},
/* C_EDIT */
	{L("edit"),	ex_edit,	E_NEWSCREEN,
	    "f1o",
	    "[Ee][dit][!] [+cmd] [file]",
	    "begin editing another file"},
/* C_EX */
	{L("ex"),		ex_edit,	E_NEWSCREEN,
	    "f1o",
	    "[Ee]x[!] [+cmd] [file]",
	    "begin editing another file"},
/* C_EXUSAGE */
	{L("exusage"),	ex_usage,	0,
	    "w1o",
	    "[exu]sage [command]",
	    "display ex command usage statement"},
/* C_FILE */
	{L("file"),	ex_file,	0,
	    "f1o",
	    "f[ile] [name]",
	    "display (and optionally set) file name"},
/* C_FG */
	{L("fg"),		ex_fg,		E_NEWSCREEN|E_VIONLY,
	    "f1o",
	    "[Ff]g [file]",
	    "bring a backgrounded screen into the foreground"},
/* C_GLOBAL */
	{L("global"),	ex_global,	E_ADDR2_ALL,
	    "!s",
	    "[line [,line]] g[lobal][!] [;/]RE[;/] [commands]",
	    "execute a global command on lines matching an RE"},
/* C_HELP */
	{L("help"),	ex_help,	0,
	    "",
	    "he[lp]",
	    "display help statement"},
/* C_INSERT */
	{L("insert"),	ex_insert,	E_ADDR1|E_ADDR_ZERO|E_ADDR_ZERODEF,
	    "!",
	    "[line] i[nsert][!]",
	    "insert input before a line"},
/* C_JOIN */
	{L("join"),	ex_join,	E_ADDR2|E_AUTOPRINT,
	    "!ca1",
	    "[line [,line]] j[oin][!] [count] [flags]",
	    "join lines into a single line"},
/* C_K */
	{L("k"),		ex_mark,	E_ADDR1,
	    "w1r",
	    "[line] k key",
	    "mark a line position"},
/* C_LIST */
	{L("list"),	ex_list,	E_ADDR2|E_CLRFLAG,
	    "ca1",
	    "[line [,line]] l[ist] [count] [#]",
	    "display lines in an unambiguous form"},
/* C_MOVE */
	{L("move"),	ex_move,	E_ADDR2|E_AUTOPRINT,
	    "l",
	    "[line [,line]] m[ove] line",
	    "move lines elsewhere in the file"},
/* C_MARK */
	{L("mark"),	ex_mark,	E_ADDR1,
	    "w1r",
	    "[line] ma[rk] key",
	    "mark a line position"},
/* C_MAP */
	{L("map"),		ex_map,		0,
	    "!W",
	    "map[!] [keys replace]",
	    "map input or commands to one or more keys"},
/* C_MKEXRC */
	{L("mkexrc"),	ex_mkexrc,	0,
	    "!f1r",
	    "mkexrc[!] file",
	    "write a .exrc file"},
/* C_NEXT */
	{L("next"),	ex_next,	E_NEWSCREEN,
	    "!fN",
	    "[Nn][ext][!] [+cmd] [file ...]",
	    "edit (and optionally specify) the next file"},
/* C_NUMBER */
	{L("number"),	ex_number,	E_ADDR2|E_CLRFLAG,
	    "ca1",
	    "[line [,line]] nu[mber] [count] [l]",
	    "change display to number lines"},
/* C_OPEN */
	{L("open"),	ex_open,	E_ADDR1,
	    "s",
	    "[line] o[pen] [/RE/] [flags]",
	    "enter \"open\" mode (not implemented)"},
/* C_PRINT */
	{L("print"),	ex_pr,		E_ADDR2|E_CLRFLAG,
	    "ca1",
	    "[line [,line]] p[rint] [count] [#l]",
	    "display lines"},
/* C_PRESERVE */
	{L("preserve"),	ex_preserve,	0,
	    "",
	    "pre[serve]",
	    "preserve an edit session for recovery"},
/* C_PREVIOUS */
	{L("previous"),	ex_prev,	E_NEWSCREEN,
	    "!",
	    "[Pp]rev[ious][!]",
	    "edit the previous file in the file argument list"},
/* C_PUT */
	{L("put"),		ex_put,	
	    E_ADDR1|E_AUTOPRINT|E_ADDR_ZERO|E_ADDR_ZERODEF,
	    "b",
	    "[line] pu[t] [buffer]",
	    "append a cut buffer to the line"},
/* C_QUIT */
	{L("quit"),	ex_quit,	0,
	    "!",
	    "q[uit][!]",
	    "exit ex/vi"},
/* C_READ */
	{L("read"),	ex_read,	E_ADDR1|E_ADDR_ZERO|E_ADDR_ZERODEF,
	    "s",
	    "[line] r[ead] [!cmd | [file]]",
	    "append input from a command or file to the line"},
/* C_RECOVER */
	{L("recover"),	ex_recover,	0,
	    "!f1r",
	    "recover[!] file",
	    "recover a saved file"},
/* C_RESIZE */
	{L("resize"),	ex_resize,	E_VIONLY,
	    "c+",
	    "resize [+-]rows",
	    "grow or shrink the current screen"},
/* C_REWIND */
	{L("rewind"),	ex_rew,		0,
	    "!",
	    "rew[ind][!]",
	    "re-edit all the files in the file argument list"},
/*
 * !!!
 * Adding new commands starting with 's' may break the substitute command code
 * in ex_cmd() (the ex parser).  Read through the comments there, first.
 */
/* C_SUBSTITUTE */
	{L("s"),		ex_s,		E_ADDR2|E_ADDR_ZERO,
	    "s",
	    "[line [,line]] s [[/;]RE[/;]repl[/;] [cgr] [count] [#lp]]",
	    "substitute on lines matching an RE"},
/* C_SCRIPT */
	{L("script"),	ex_script,	E_SECURE,
	    "!f1o",
	    "sc[ript][!] [file]",
	    "run a shell in a screen"},
/* C_SET */
	{L("set"),		ex_set,		0,
	    "wN",
	    "se[t] [option[=[value]]...] [nooption ...] [option? ...] [all]",
	    "set options (use \":set all\" to see all options)"},
/* C_SHELL */
	{L("shell"),	ex_shell,	E_SECURE,
	    "",
	    "sh[ell]",
	    "suspend editing and run a shell"},
/* C_SOURCE */
	{L("source"),	ex_source,	0,
	    "f1r",
	    "so[urce] file",
	    "read a file of ex commands"},
/* C_STOP */
	{L("stop"),	ex_stop,	E_SECURE,
	    "!",
	    "st[op][!]",
	    "suspend the edit session"},
/* C_SUSPEND */
	{L("suspend"),	ex_stop,	E_SECURE,
	    "!",
	    "su[spend][!]",
	    "suspend the edit session"},
/* C_T */
	{L("t"),		ex_copy,	E_ADDR2|E_AUTOPRINT,
	    "l1",
	    "[line [,line]] t line [flags]",
	    "copy lines elsewhere in the file"},
/* C_TAG */
	{L("tag"),		ex_tag_push,	E_NEWSCREEN,
	    "!w1o",
	    "[Tt]a[g][!] [string]",
	    "edit the file containing the tag"},
/* C_TAGNEXT */
	{L("tagnext"),	ex_tag_next,	0,
	    "!",
	    "tagn[ext][!]",
	    "move to the next tag"},
/* C_TAGPOP */
	{L("tagpop"),	ex_tag_pop,	0,
	    "!w1o",
	    "tagp[op][!] [number | file]",
	    "return to the previous group of tags"},
/* C_TAGPREV */
	{L("tagprev"),	ex_tag_prev,	0,
	    "!",
	    "tagpr[ev][!]",
	    "move to the previous tag"},
/* C_TAGTOP */
	{L("tagtop"),	ex_tag_top,	0,
	    "!",
	    "tagt[op][!]",
	    "discard all tags"},
/* C_UNDO */
	{L("undo"),	ex_undo,	E_AUTOPRINT,
	    "",
	    "u[ndo]",
	    "undo the most recent change"},
/* C_UNABBREVIATE */
	{L("unabbreviate"),ex_unabbr,	0,
	    "w1r",
	    "una[bbrev] word",
	    "delete an abbreviation"},
/* C_UNMAP */
	{L("unmap"),	ex_unmap,	0,
	    "!w1r",
	    "unm[ap][!] word",
	    "delete an input or command map"},
/* C_V */
	{L("v"),		ex_v,		E_ADDR2_ALL,
	    "s",
	    "[line [,line]] v [;/]RE[;/] [commands]",
	    "execute a global command on lines NOT matching an RE"},
/* C_VERSION */
	{L("version"),	ex_version,	0,
	    "",
	    "version",
	    "display the program version information"},
/* C_VISUAL_EX */
	{L("visual"),	ex_visual,	E_ADDR1|E_ADDR_ZERODEF,
	    "2c11",
	    "[line] vi[sual] [-|.|+|^] [window_size] [flags]",
	    "enter visual (vi) mode from ex mode"},
/* C_VISUAL_VI */
	{L("visual"),	ex_edit,	E_NEWSCREEN,
	    "f1o",
	    "[Vv]i[sual][!] [+cmd] [file]",
	    "edit another file (from vi mode only)"},
/* C_VIUSAGE */
	{L("viusage"),	ex_viusage,	0,
	    "w1o",
	    "[viu]sage [key]",
	    "display vi key usage statement"},
/* C_VSPLIT */
	{L("vsplit"),	ex_edit,	E_VIONLY,
	    "f1o",
	    "vs[plit] [+cmd] [file]",
	    "split the current screen vertically"},
/* C_WRITE */
	{L("write"),	ex_write,	E_ADDR2_ALL|E_ADDR_ZERODEF,
	    "!s",
	    "[line [,line]] w[rite][!] [ !cmd | [>>] [file]]",
	    "write the file"},
/* C_WN */
	{L("wn"),		ex_wn,		E_ADDR2_ALL|E_ADDR_ZERODEF,
	    "!s",
	    "[line [,line]] wn[!] [>>] [file]",
	    "write the file and switch to the next file"},
/* C_WQ */
	{L("wq"),		ex_wq,		E_ADDR2_ALL|E_ADDR_ZERODEF,
	    "!s",
	    "[line [,line]] wq[!] [>>] [file]",
	    "write the file and exit"},
/* C_XIT */
	{L("xit"),		ex_xit,		E_ADDR2_ALL|E_ADDR_ZERODEF,
	    "!f1o",
	    "[line [,line]] x[it][!] [file]",
	    "exit"},
/* C_YANK */
	{L("yank"),	ex_yank,	E_ADDR2,
	    "bca",
	    "[line [,line]] ya[nk] [buffer] [count]",
	    "copy lines to a cut buffer"},
/* C_Z */
	{L("z"),		ex_z,		E_ADDR1,
	    "3c01",
	    "[line] z [-|.|+|^|=] [count] [flags]",
	    "display different screens of the file"},
/* C_SUBTILDE */
	{L("~"),		ex_subtilde,	E_ADDR2|E_ADDR_ZERO,
	    "s",
	    "[line [,line]] ~ [cgr] [count] [#lp]",
	    "replace previous RE with previous replacement string,"},
	{NULL},
};

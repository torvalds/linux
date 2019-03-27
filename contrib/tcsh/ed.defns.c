/* $Header: /p/tcsh/cvsroot/tcsh/ed.defns.c,v 3.51 2016/02/14 15:44:18 christos Exp $ */
/*
 * ed.defns.c: Editor function definitions and initialization
 */
/*-
 * Copyright (c) 1980, 1991 The Regents of the University of California.
 * All rights reserved.
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
 */
#include "sh.h"

RCSID("$tcsh: ed.defns.c,v 3.51 2016/02/14 15:44:18 christos Exp $")

#include "ed.h"

static	void		ed_InitMetaBindings 	(void);

PFCmd   CcFuncTbl[] = {		/* table of available commands */
    e_unassigned,
/* no #define here -- this is a dummy to detect initing of the key map */
    e_unassigned,
#define		F_UNASSIGNED	1
    e_insert,
#define		F_INSERT	2
    e_newline,
#define		F_NEWLINE	3
    e_delprev,
#define		F_DELPREV	4
    e_delnext,
#define		F_DELNEXT	5
    e_toend,
#define		F_TOEND		6
    e_tobeg,
#define		F_TOBEG		7
    e_charback,
#define		F_CHARBACK	8
    e_charfwd,
#define		F_CHARFWD	9
    e_quote,
#define		F_QUOTE		10
    e_startover,
#define		F_STARTOVER	11
    e_redisp,
#define		F_REDISP	12
    e_tty_int,
#define		F_TTY_INT	13
    e_wordback,
#define		F_WORDBACK	14
    e_wordfwd,
#define		F_WORDFWD	15
    e_cleardisp,
#define		F_CLEARDISP	16
    e_complete,
#define		F_COMPLETE	17
    e_correct,
#define		F_CORRECT	18
    e_up_hist,
#define		F_UP_HIST	19
    e_down_hist,
#define		F_DOWN_HIST	20
    e_up_search_hist,
#define		F_UP_SEARCH_HIST	21
    e_down_search_hist,
#define		F_DOWN_SEARCH_HIST	22
    e_helpme,
#define		F_HELPME	23
    e_list_choices,
#define		F_LIST_CHOICES	24
    e_delwordprev,
#define		F_DELWORDPREV	25
    e_delwordnext,
#define		F_DELWORDNEXT	26
    e_digit,
#define		F_DIGIT		27
    e_killend,
#define		F_KILLEND	28
    e_killbeg,
#define		F_KILLBEG	29
    e_metanext,
#define		F_METANEXT	30
    e_send_eof,
#define		F_SEND_EOF	31
    e_charswitch,
#define		F_CHARSWITCH	32
    e_which,
#define		F_WHICH		33
    e_yank_kill,
#define		F_YANK_KILL	34
    e_tty_dsusp,
#define		F_TTY_DSUSP	35
    e_tty_flusho,
#define		F_TTY_FLUSHO	36
    e_tty_quit,
#define		F_TTY_QUIT	37
    e_tty_tsusp,
#define		F_TTY_TSUSP	38
    e_tty_stopo,
#define		F_TTY_STOPO	39
    e_tty_starto,
#define		F_TTY_STARTO	40
    e_argfour,
#define		F_ARGFOUR	41
    e_set_mark,
#define		F_SET_MARK	42
    e_exchange_mark,
#define		F_EXCHANGE_MARK	43
    e_last_item,
#define		F_LAST_ITEM	44
    e_delnext_list_eof,
#define		F_DELNEXT_LIST_EOF	45
    v_cmd_mode,
#define		V_CMD_MODE	46
    v_insert,
#define		V_INSERT	47
    e_argdigit,
#define		F_ARGDIGIT	48
    e_killregion,
#define		F_KILLREGION	49
    e_copyregion,
#define		F_COPYREGION	50
    e_gcharswitch,
#define		F_GCHARSWITCH	51
    e_run_fg_editor,
#define		F_RUN_FG_EDITOR	52
    e_unassigned,	/* place holder for sequence lead in character */
#define		F_XKEY		53
    e_uppercase,
#define         F_CASEUPPER     54
    e_lowercase,
#define         F_CASELOWER     55
    e_capitalcase,
#define         F_CASECAPITAL   56
    v_zero,
#define		V_ZERO		57
    v_add,
#define		V_ADD		58
    v_addend,
#define		V_ADDEND	59
    v_wordbegnext,
#define		V_WORDBEGNEXT	60
    e_killall,
#define		F_KILLALL	61
    e_unassigned,
/* F_EXTENDNEXT removed */
    v_insbeg,
#define		V_INSBEG	63
    v_replmode,
#define		V_REPLMODE	64
    v_replone,
#define		V_REPLONE	65
    v_substline,
#define		V_SUBSTLINE	66
    v_substchar,
#define		V_SUBSTCHAR	67
    v_chgtoend,
#define		V_CHGTOEND	68
    e_list_eof,
#define		F_LIST_EOF	69
    e_list_glob,
#define		F_LIST_GLOB	70
    e_expand_history,
#define		F_EXPAND_HISTORY	71
    e_magic_space,
#define		F_MAGIC_SPACE	72
    e_insovr,
#define		F_INSOVR	73
    v_cm_complete,
#define		V_CM_COMPLETE	74
    e_copyprev,
#define		F_COPYPREV	75
    e_correctl,
#define		F_CORRECT_L	76
    e_expand_glob,
#define		F_EXPAND_GLOB	77
    e_expand_vars,
#define		F_EXPAND_VARS	78
    e_toggle_hist,
#define		F_TOGGLE_HIST	79
    v_change_case,
#define		V_CHGCASE	80
    e_expand,
#define		F_EXPAND	81
    e_load_average,
#define		F_LOAD_AVERAGE	82
    v_delprev,
#define		V_DELPREV	83
    v_delmeta,
#define		V_DELMETA	84
    v_wordfwd,
#define		V_WORDFWD	85
    v_wordback,
#define		V_WORDBACK	86
    v_endword,
#define		V_ENDWORD	87
    v_eword,
#define		V_EWORD		88
    v_undo,
#define		V_UNDO		89
    v_ush_meta,
#define		V_USH_META	90
    v_dsh_meta,
#define		V_DSH_META	91
    v_rsrch_fwd,
#define		V_RSRCH_FWD	92
    v_rsrch_back,
#define		V_RSRCH_BACK	93
    v_char_fwd,
#define		V_CHAR_FWD	94
    v_char_back,
#define		V_CHAR_BACK	95
    v_chgmeta,
#define		V_CHGMETA	96
    e_inc_fwd,
#define		F_INC_FWD	97
    e_inc_back,
#define		F_INC_BACK	98
    v_rchar_fwd,
#define		V_RCHAR_FWD	99
    v_rchar_back,
#define		V_RCHAR_BACK	100
    v_charto_fwd,
#define		V_CHARTO_FWD	101
    v_charto_back,
#define		V_CHARTO_BACK	102
    e_normalize_path,
#define		F_PATH_NORM	103
    e_delnext_eof,		/* added by mtk@ari.ncl.omron.co.jp (920818) */
#define		F_DELNEXT_EOF	104
    e_stuff_char,		
#define		F_STUFF_CHAR	105
    e_complete_all,
#define		F_COMPLETE_ALL	106
    e_list_all,
#define		F_LIST_ALL	107
    e_complete_fwd,
#define		F_COMPLETE_FWD	108
    e_complete_back,
#define		F_COMPLETE_BACK	109
    e_delnext_list,
#define		F_DELNEXT_LIST	110
    e_normalize_command,
#define		F_COMMAND_NORM	111
    e_dabbrev_expand,
#define		F_DABBREV_EXPAND	112
    e_copy_to_clipboard,
#define		F_COPY_CLIP	113
    e_paste_from_clipboard,
#define		F_PASTE_CLIP	114
    e_dosify_next,
#define		F_DOSIFY_NEXT	115
    e_dosify_prev,
#define		F_DOSIFY_PREV	116
    e_page_up,
#define		F_PAGE_UP	117
    e_page_down,
#define		F_PAGE_DOWN	118
    e_yank_pop,
#define		F_YANK_POP	119
    e_newline_hold,
#define		F_NEWLINE_HOLD	120
    e_newline_down_hist,
#define		F_NEWLINE_DOWN_HIST	121
    0				/* DUMMY VALUE */
#define		F_NUM_FNS	122

};

KEYCMD  NumFuns = F_NUM_FNS;

KEYCMD  CcKeyMap[NT_NUM_KEYS];		/* the real key map */
KEYCMD  CcAltMap[NT_NUM_KEYS];		/* the alternative key map */
#define	F_NUM_FUNCNAMES	(F_NUM_FNS + 2)
struct KeyFuncs FuncNames[F_NUM_FUNCNAMES];

#ifdef WINNT_NATIVE
extern KEYCMD CcEmacsMap[];
extern KEYCMD CcViMap[];
extern KEYCMD  CcViCmdMap[];
#else /* !WINNT_NATIVE*/
KEYCMD  CcEmacsMap[] = {
/* keymap table, each index into above tbl; should be 256*sizeof(KEYCMD)
   bytes long */

    F_SET_MARK,			/* ^@ */
    F_TOBEG,			/* ^A */
    F_CHARBACK,			/* ^B */
    F_TTY_INT,			/* ^C */
    F_DELNEXT_LIST_EOF,		/* ^D */
    F_TOEND,			/* ^E */
    F_CHARFWD,			/* ^F */
    F_UNASSIGNED,		/* ^G */
    F_DELPREV,			/* ^H */
    F_COMPLETE,			/* ^I */
    F_NEWLINE,			/* ^J */
    F_KILLEND,			/* ^K */
    F_CLEARDISP,		/* ^L */
    F_NEWLINE,			/* ^M */
    F_DOWN_HIST,		/* ^N */
    F_TTY_FLUSHO,		/* ^O */
    F_UP_HIST,			/* ^P */
    F_TTY_STARTO,		/* ^Q */
    F_REDISP,			/* ^R */
    F_TTY_STOPO,		/* ^S */
    F_CHARSWITCH,		/* ^T */
    F_KILLALL,			/* ^U */
    F_QUOTE,			/* ^V */
    F_KILLREGION,		/* ^W */
    F_XKEY,			/* ^X */
    F_YANK_KILL,		/* ^Y */
    F_TTY_TSUSP,		/* ^Z */
    F_METANEXT,			/* ^[ */
    F_TTY_QUIT,			/* ^\ */
    F_TTY_DSUSP,		/* ^] */
    F_UNASSIGNED,		/* ^^ */
    F_UNASSIGNED,		/* ^_ */
    F_INSERT,			/* SPACE */
    F_INSERT,			/* ! */
    F_INSERT,			/* " */
    F_INSERT,			/* # */
    F_INSERT,			/* $ */
    F_INSERT,			/* % */
    F_INSERT,			/* & */
    F_INSERT,			/* ' */
    F_INSERT,			/* ( */
    F_INSERT,			/* ) */
    F_INSERT,			/* * */
    F_INSERT,			/* + */
    F_INSERT,			/* , */
    F_INSERT,			/* - */
    F_INSERT,			/* . */
    F_INSERT,			/* / */
    F_DIGIT,			/* 0 */
    F_DIGIT,			/* 1 */
    F_DIGIT,			/* 2 */
    F_DIGIT,			/* 3 */
    F_DIGIT,			/* 4 */
    F_DIGIT,			/* 5 */
    F_DIGIT,			/* 6 */
    F_DIGIT,			/* 7 */
    F_DIGIT,			/* 8 */
    F_DIGIT,			/* 9 */
    F_INSERT,			/* : */
    F_INSERT,			/* ; */
    F_INSERT,			/* < */
    F_INSERT,			/* = */
    F_INSERT,			/* > */
    F_INSERT,			/* ? */
    F_INSERT,			/* @ */
    F_INSERT,			/* A */
    F_INSERT,			/* B */
    F_INSERT,			/* C */
    F_INSERT,			/* D */
    F_INSERT,			/* E */
    F_INSERT,			/* F */
    F_INSERT,			/* G */
    F_INSERT,			/* H */
    F_INSERT,			/* I */
    F_INSERT,			/* J */
    F_INSERT,			/* K */
    F_INSERT,			/* L */
    F_INSERT,			/* M */
    F_INSERT,			/* N */
    F_INSERT,			/* O */
    F_INSERT,			/* P */
    F_INSERT,			/* Q */
    F_INSERT,			/* R */
    F_INSERT,			/* S */
    F_INSERT,			/* T */
    F_INSERT,			/* U */
    F_INSERT,			/* V */
    F_INSERT,			/* W */
    F_INSERT,			/* X */
    F_INSERT,			/* Y */
    F_INSERT,			/* Z */
    F_INSERT,			/* [ */
    F_INSERT,			/* \ */
    F_INSERT,			/* ] */
    F_INSERT,			/* ^ */
    F_INSERT,			/* _ */
    F_INSERT,			/* ` */
    F_INSERT,			/* a */
    F_INSERT,			/* b */
    F_INSERT,			/* c */
    F_INSERT,			/* d */
    F_INSERT,			/* e */
    F_INSERT,			/* f */
    F_INSERT,			/* g */
    F_INSERT,			/* h */
    F_INSERT,			/* i */
    F_INSERT,			/* j */
    F_INSERT,			/* k */
    F_INSERT,			/* l */
    F_INSERT,			/* m */
    F_INSERT,			/* n */
    F_INSERT,			/* o */
    F_INSERT,			/* p */
    F_INSERT,			/* q */
    F_INSERT,			/* r */
    F_INSERT,			/* s */
    F_INSERT,			/* t */
    F_INSERT,			/* u */
    F_INSERT,			/* v */
    F_INSERT,			/* w */
    F_INSERT,			/* x */
    F_INSERT,			/* y */
    F_INSERT,			/* z */
    F_INSERT,			/* { */
    F_INSERT,			/* | */
    F_INSERT,			/* } */
    F_INSERT,			/* ~ */
    F_DELPREV,			/* ^? */
    F_UNASSIGNED,		/* M-^@ */
    F_UNASSIGNED,		/* M-^A */
    F_UNASSIGNED,		/* M-^B */
    F_UNASSIGNED,		/* M-^C */
    F_LIST_CHOICES,		/* M-^D */
    F_UNASSIGNED,		/* M-^E */
    F_UNASSIGNED,		/* M-^F */
    F_UNASSIGNED,		/* M-^G */
    F_DELWORDPREV,		/* M-^H */
    F_COMPLETE,			/* M-^I */
    F_UNASSIGNED,		/* M-^J */
    F_UNASSIGNED,		/* M-^K */
    F_CLEARDISP,		/* M-^L */
    F_UNASSIGNED,		/* M-^M */
    F_UNASSIGNED,		/* M-^N */
    F_UNASSIGNED,		/* M-^O */
    F_UNASSIGNED,		/* M-^P */
    F_UNASSIGNED,		/* M-^Q */
    F_UNASSIGNED,		/* M-^R */
    F_UNASSIGNED,		/* M-^S */
    F_UNASSIGNED,		/* M-^T */
    F_UNASSIGNED,		/* M-^U */
    F_UNASSIGNED,		/* M-^V */
    F_UNASSIGNED,		/* M-^W */
    F_UNASSIGNED,		/* M-^X */
    F_UNASSIGNED,		/* M-^Y */
    F_RUN_FG_EDITOR,		/* M-^Z */
    F_COMPLETE,			/* M-^[ */
    F_UNASSIGNED,		/* M-^\ */
    F_UNASSIGNED,		/* M-^] */
    F_UNASSIGNED,		/* M-^^ */
    F_COPYPREV,			/* M-^_ */
    F_EXPAND_HISTORY,		/* M-SPACE */
    F_EXPAND_HISTORY,		/* M-! */
    F_UNASSIGNED,		/* M-" */
    F_UNASSIGNED,		/* M-# */
    F_CORRECT_L,		/* M-$ */
    F_UNASSIGNED,		/* M-% */
    F_UNASSIGNED,		/* M-& */
    F_UNASSIGNED,		/* M-' */
    F_UNASSIGNED,		/* M-( */
    F_UNASSIGNED,		/* M-) */
    F_UNASSIGNED,		/* M-* */
    F_UNASSIGNED,		/* M-+ */
    F_UNASSIGNED,		/* M-, */
    F_UNASSIGNED,		/* M-- */
    F_UNASSIGNED,		/* M-. */
    F_DABBREV_EXPAND,		/* M-/ */
    F_ARGDIGIT,			/* M-0 */
    F_ARGDIGIT,			/* M-1 */
    F_ARGDIGIT,			/* M-2 */
    F_ARGDIGIT,			/* M-3 */
    F_ARGDIGIT,			/* M-4 */
    F_ARGDIGIT,			/* M-5 */
    F_ARGDIGIT,			/* M-6 */
    F_ARGDIGIT,			/* M-7 */
    F_ARGDIGIT,			/* M-8 */
    F_ARGDIGIT,			/* M-9 */
    F_UNASSIGNED,		/* M-: */
    F_UNASSIGNED,		/* M-; */
    F_UNASSIGNED,		/* M-< */
    F_UNASSIGNED,		/* M-= */
    F_UNASSIGNED,		/* M-> */
    F_WHICH,			/* M-? */
    F_UNASSIGNED,		/* M-@ */
    F_NEWLINE_HOLD,		/* M-A */
    F_WORDBACK,			/* M-B */
    F_CASECAPITAL,		/* M-C */
    F_DELWORDNEXT,		/* M-D */
    F_UNASSIGNED,		/* M-E */
    F_WORDFWD,			/* M-F */
    F_UNASSIGNED,		/* M-G */
    F_HELPME,			/* M-H */
    F_UNASSIGNED,		/* M-I */
    F_UNASSIGNED,		/* M-J */
    F_UNASSIGNED,		/* M-K */
    F_CASELOWER,		/* M-L */
    F_UNASSIGNED,		/* M-M */
    F_DOWN_SEARCH_HIST,		/* M-N */
    F_XKEY,			/* M-O *//* extended key esc PWP Mar 88 */
    F_UP_SEARCH_HIST,		/* M-P */
    F_UNASSIGNED,		/* M-Q */
    F_TOGGLE_HIST,		/* M-R */
    F_CORRECT,			/* M-S */
    F_UNASSIGNED,		/* M-T */
    F_CASEUPPER,		/* M-U */
    F_UNASSIGNED,		/* M-V */
    F_COPYREGION,		/* M-W */
    F_UNASSIGNED,		/* M-X */
    F_YANK_POP,			/* M-Y */
    F_UNASSIGNED,		/* M-Z */
    F_XKEY,			/* M-[ *//* extended key esc -mf Oct 87 */
    F_UNASSIGNED,		/* M-\ */
    F_UNASSIGNED,		/* M-] */
    F_UNASSIGNED,		/* M-^ */
    F_LAST_ITEM,		/* M-_ */
    F_UNASSIGNED,		/* M-` */
    F_NEWLINE_HOLD,		/* M-a */
    F_WORDBACK,			/* M-b */
    F_CASECAPITAL,		/* M-c */
    F_DELWORDNEXT,		/* M-d */
    F_UNASSIGNED,		/* M-e */
    F_WORDFWD,			/* M-f */
    F_UNASSIGNED,		/* M-g */
    F_HELPME,			/* M-h */
    F_UNASSIGNED,		/* M-i */
    F_UNASSIGNED,		/* M-j */
    F_UNASSIGNED,		/* M-k */
    F_CASELOWER,		/* M-l */
    F_UNASSIGNED,		/* M-m */
    F_DOWN_SEARCH_HIST,		/* M-n */
    F_UNASSIGNED,		/* M-o */
    F_UP_SEARCH_HIST,		/* M-p */
    F_UNASSIGNED,		/* M-q */
    F_TOGGLE_HIST,		/* M-r */
    F_CORRECT,			/* M-s */
    F_UNASSIGNED,		/* M-t */
    F_CASEUPPER,		/* M-u */
    F_UNASSIGNED,		/* M-v */
    F_COPYREGION,		/* M-w */
    F_UNASSIGNED,		/* M-x */
    F_YANK_POP,			/* M-y */
    F_UNASSIGNED,		/* M-z */
    F_UNASSIGNED,		/* M-{ */
    F_UNASSIGNED,		/* M-| */
    F_UNASSIGNED,		/* M-} */
    F_UNASSIGNED,		/* M-~ */
    F_DELWORDPREV		/* M-^? */
};

/*
 * keymap table for vi.  Each index into above tbl; should be
 * 256 entries long.  Vi mode uses a sticky-extend to do command mode:
 * insert mode characters are in the normal keymap, and command mode
 * in the extended keymap.
 */
static KEYCMD  CcViMap[] = {
#ifdef KSHVI
    F_UNASSIGNED,		/* ^@ */
    F_INSERT,			/* ^A */
    F_INSERT,			/* ^B */
    F_INSERT,			/* ^C */
    F_INSERT,			/* ^D */
    F_INSERT,			/* ^E */
    F_INSERT,			/* ^F */
    F_INSERT,			/* ^G */
    V_DELPREV,			/* ^H */   /* BackSpace key */
    F_COMPLETE,			/* ^I */   /* Tab Key  */
    F_NEWLINE,			/* ^J */
    F_INSERT,			/* ^K */
    F_INSERT,			/* ^L */
    F_NEWLINE,			/* ^M */
    F_INSERT,			/* ^N */
    F_INSERT,			/* ^O */
    F_INSERT,			/* ^P */
    F_TTY_STARTO,		/* ^Q */
    F_INSERT,			/* ^R */
    F_INSERT,			/* ^S */
    F_INSERT,			/* ^T */
    F_INSERT,			/* ^U */
    F_QUOTE,			/* ^V */
    F_DELWORDPREV,		/* ^W */  /* Only until start edit pos */
    F_INSERT,			/* ^X */
    F_INSERT,			/* ^Y */
    F_INSERT,			/* ^Z */
    V_CMD_MODE,			/* ^[ */  /* [ Esc ] key */
    F_TTY_QUIT,			/* ^\ */
    F_INSERT,			/* ^] */
    F_INSERT,			/* ^^ */
    F_INSERT,			/* ^_ */
#else /* !KSHVI */
    F_UNASSIGNED,		/* ^@ */   /* NOTE: These mapping do NOT */
    F_TOBEG,			/* ^A */   /* Correspond well to the KSH */
    F_CHARBACK,			/* ^B */   /* VI editting assignments    */
    F_TTY_INT,			/* ^C */   /* On the other hand they are */
    F_LIST_EOF,			/* ^D */   /* convenient any many people */
    F_TOEND,			/* ^E */   /* have gotten used to them   */
    F_CHARFWD,			/* ^F */
    F_LIST_GLOB,		/* ^G */
    F_DELPREV,			/* ^H */   /* BackSpace key */
    F_COMPLETE,			/* ^I */   /* Tab Key */
    F_NEWLINE,			/* ^J */
    F_KILLEND,			/* ^K */
    F_CLEARDISP,		/* ^L */
    F_NEWLINE,			/* ^M */
    F_DOWN_HIST,		/* ^N */
    F_TTY_FLUSHO,		/* ^O */
    F_UP_HIST,			/* ^P */
    F_TTY_STARTO,		/* ^Q */
    F_REDISP,			/* ^R */
    F_TTY_STOPO,		/* ^S */
    F_CHARSWITCH,		/* ^T */
    F_KILLBEG,			/* ^U */
    F_QUOTE,			/* ^V */
    F_DELWORDPREV,		/* ^W */
    F_EXPAND,			/* ^X */
    F_TTY_DSUSP,		/* ^Y */
    F_TTY_TSUSP,		/* ^Z */
    V_CMD_MODE,			/* ^[ */
    F_TTY_QUIT,			/* ^\ */
    F_UNASSIGNED,		/* ^] */
    F_UNASSIGNED,		/* ^^ */
    F_UNASSIGNED,		/* ^_ */
#endif  /* KSHVI */
    F_INSERT,			/* SPACE */
    F_INSERT,			/* ! */
    F_INSERT,			/* " */
    F_INSERT,			/* # */
    F_INSERT,			/* $ */
    F_INSERT,			/* % */
    F_INSERT,			/* & */
    F_INSERT,			/* ' */
    F_INSERT,			/* ( */
    F_INSERT,			/* ) */
    F_INSERT,			/* * */
    F_INSERT,			/* + */
    F_INSERT,			/* , */
    F_INSERT,			/* - */
    F_INSERT,			/* . */
    F_INSERT,			/* / */
    F_INSERT,			/* 0 */
    F_INSERT,			/* 1 */
    F_INSERT,			/* 2 */
    F_INSERT,			/* 3 */
    F_INSERT,			/* 4 */
    F_INSERT,			/* 5 */
    F_INSERT,			/* 6 */
    F_INSERT,			/* 7 */
    F_INSERT,			/* 8 */
    F_INSERT,			/* 9 */
    F_INSERT,			/* : */
    F_INSERT,			/* ; */
    F_INSERT,			/* < */
    F_INSERT,			/* = */
    F_INSERT,			/* > */
    F_INSERT,			/* ? */
    F_INSERT,			/* @ */
    F_INSERT,			/* A */
    F_INSERT,			/* B */
    F_INSERT,			/* C */
    F_INSERT,			/* D */
    F_INSERT,			/* E */
    F_INSERT,			/* F */
    F_INSERT,			/* G */
    F_INSERT,			/* H */
    F_INSERT,			/* I */
    F_INSERT,			/* J */
    F_INSERT,			/* K */
    F_INSERT,			/* L */
    F_INSERT,			/* M */
    F_INSERT,			/* N */
    F_INSERT,			/* O */
    F_INSERT,			/* P */
    F_INSERT,			/* Q */
    F_INSERT,			/* R */
    F_INSERT,			/* S */
    F_INSERT,			/* T */
    F_INSERT,			/* U */
    F_INSERT,			/* V */
    F_INSERT,			/* W */
    F_INSERT,			/* X */
    F_INSERT,			/* Y */
    F_INSERT,			/* Z */
    F_INSERT,			/* [ */
    F_INSERT,			/* \ */
    F_INSERT,			/* ] */
    F_INSERT,			/* ^ */
    F_INSERT,			/* _ */
    F_INSERT,			/* ` */
    F_INSERT,			/* a */
    F_INSERT,			/* b */
    F_INSERT,			/* c */
    F_INSERT,			/* d */
    F_INSERT,			/* e */
    F_INSERT,			/* f */
    F_INSERT,			/* g */
    F_INSERT,			/* h */
    F_INSERT,			/* i */
    F_INSERT,			/* j */
    F_INSERT,			/* k */
    F_INSERT,			/* l */
    F_INSERT,			/* m */
    F_INSERT,			/* n */
    F_INSERT,			/* o */
    F_INSERT,			/* p */
    F_INSERT,			/* q */
    F_INSERT,			/* r */
    F_INSERT,			/* s */
    F_INSERT,			/* t */
    F_INSERT,			/* u */
    F_INSERT,			/* v */
    F_INSERT,			/* w */
    F_INSERT,			/* x */
    F_INSERT,			/* y */
    F_INSERT,			/* z */
    F_INSERT,			/* { */
    F_INSERT,			/* | */
    F_INSERT,			/* } */
    F_INSERT,			/* ~ */
    F_DELPREV,			/* ^? */
    F_UNASSIGNED,		/* M-^@ */
    F_UNASSIGNED,		/* M-^A */
    F_UNASSIGNED,		/* M-^B */
    F_UNASSIGNED,		/* M-^C */
    F_UNASSIGNED,		/* M-^D */
    F_UNASSIGNED,		/* M-^E */
    F_UNASSIGNED,		/* M-^F */
    F_UNASSIGNED,		/* M-^G */
    F_UNASSIGNED,		/* M-^H */
    F_UNASSIGNED,		/* M-^I */
    F_UNASSIGNED,		/* M-^J */
    F_UNASSIGNED,		/* M-^K */
    F_UNASSIGNED,		/* M-^L */
    F_UNASSIGNED,		/* M-^M */
    F_UNASSIGNED,		/* M-^N */
    F_UNASSIGNED,		/* M-^O */
    F_UNASSIGNED,		/* M-^P */
    F_UNASSIGNED,		/* M-^Q */
    F_UNASSIGNED,		/* M-^R */
    F_UNASSIGNED,		/* M-^S */
    F_UNASSIGNED,		/* M-^T */
    F_UNASSIGNED,		/* M-^U */
    F_UNASSIGNED,		/* M-^V */
    F_UNASSIGNED,		/* M-^W */
    F_UNASSIGNED,		/* M-^X */
    F_UNASSIGNED,		/* M-^Y */
    F_UNASSIGNED,		/* M-^Z */
    F_UNASSIGNED,		/* M-^[ */
    F_UNASSIGNED,		/* M-^\ */
    F_UNASSIGNED,		/* M-^] */
    F_UNASSIGNED,		/* M-^^ */
    F_UNASSIGNED,		/* M-^_ */
    F_UNASSIGNED,		/* M-SPACE */
    F_UNASSIGNED,		/* M-! */
    F_UNASSIGNED,		/* M-" */
    F_UNASSIGNED,		/* M-# */
    F_UNASSIGNED,		/* M-$ */
    F_UNASSIGNED,		/* M-% */
    F_UNASSIGNED,		/* M-& */
    F_UNASSIGNED,		/* M-' */
    F_UNASSIGNED,		/* M-( */
    F_UNASSIGNED,		/* M-) */
    F_UNASSIGNED,		/* M-* */
    F_UNASSIGNED,		/* M-+ */
    F_UNASSIGNED,		/* M-, */
    F_UNASSIGNED,		/* M-- */
    F_UNASSIGNED,		/* M-. */
    F_UNASSIGNED,		/* M-/ */
    F_UNASSIGNED,		/* M-0 */
    F_UNASSIGNED,		/* M-1 */
    F_UNASSIGNED,		/* M-2 */
    F_UNASSIGNED,		/* M-3 */
    F_UNASSIGNED,		/* M-4 */
    F_UNASSIGNED,		/* M-5 */
    F_UNASSIGNED,		/* M-6 */
    F_UNASSIGNED,		/* M-7 */
    F_UNASSIGNED,		/* M-8 */
    F_UNASSIGNED,		/* M-9 */
    F_UNASSIGNED,		/* M-: */
    F_UNASSIGNED,		/* M-; */
    F_UNASSIGNED,		/* M-< */
    F_UNASSIGNED,		/* M-= */
    F_UNASSIGNED,		/* M-> */
    F_UNASSIGNED,		/* M-? */
    F_UNASSIGNED,		/* M-@ */
    F_UNASSIGNED,		/* M-A */
    F_UNASSIGNED,		/* M-B */
    F_UNASSIGNED,		/* M-C */
    F_UNASSIGNED,		/* M-D */
    F_UNASSIGNED,		/* M-E */
    F_UNASSIGNED,		/* M-F */
    F_UNASSIGNED,		/* M-G */
    F_UNASSIGNED,		/* M-H */
    F_UNASSIGNED,		/* M-I */
    F_UNASSIGNED,		/* M-J */
    F_UNASSIGNED,		/* M-K */
    F_UNASSIGNED,		/* M-L */
    F_UNASSIGNED,		/* M-M */
    F_UNASSIGNED,		/* M-N */
    F_UNASSIGNED,		/* M-O */
    F_UNASSIGNED,		/* M-P */
    F_UNASSIGNED,		/* M-Q */
    F_UNASSIGNED,		/* M-R */
    F_UNASSIGNED,		/* M-S */
    F_UNASSIGNED,		/* M-T */
    F_UNASSIGNED,		/* M-U */
    F_UNASSIGNED,		/* M-V */
    F_UNASSIGNED,		/* M-W */
    F_UNASSIGNED,		/* M-X */
    F_UNASSIGNED,		/* M-Y */
    F_UNASSIGNED,		/* M-Z */
    F_UNASSIGNED,		/* M-[ */
    F_UNASSIGNED,		/* M-\ */
    F_UNASSIGNED,		/* M-] */
    F_UNASSIGNED,		/* M-^ */
    F_UNASSIGNED,		/* M-_ */
    F_UNASSIGNED,		/* M-` */
    F_UNASSIGNED,		/* M-a */
    F_UNASSIGNED,		/* M-b */
    F_UNASSIGNED,		/* M-c */
    F_UNASSIGNED,		/* M-d */
    F_UNASSIGNED,		/* M-e */
    F_UNASSIGNED,		/* M-f */
    F_UNASSIGNED,		/* M-g */
    F_UNASSIGNED,		/* M-h */
    F_UNASSIGNED,		/* M-i */
    F_UNASSIGNED,		/* M-j */
    F_UNASSIGNED,		/* M-k */
    F_UNASSIGNED,		/* M-l */
    F_UNASSIGNED,		/* M-m */
    F_UNASSIGNED,		/* M-n */
    F_UNASSIGNED,		/* M-o */
    F_UNASSIGNED,		/* M-p */
    F_UNASSIGNED,		/* M-q */
    F_UNASSIGNED,		/* M-r */
    F_UNASSIGNED,		/* M-s */
    F_UNASSIGNED,		/* M-t */
    F_UNASSIGNED,		/* M-u */
    F_UNASSIGNED,		/* M-v */
    F_UNASSIGNED,		/* M-w */
    F_UNASSIGNED,		/* M-x */
    F_UNASSIGNED,		/* M-y */
    F_UNASSIGNED,		/* M-z */
    F_UNASSIGNED,		/* M-{ */
    F_UNASSIGNED,		/* M-| */
    F_UNASSIGNED,		/* M-} */
    F_UNASSIGNED,		/* M-~ */
    F_UNASSIGNED		/* M-^? */
};

KEYCMD  CcViCmdMap[] = {
    F_UNASSIGNED,		/* ^@ */
    F_TOBEG,			/* ^A */
    F_UNASSIGNED,		/* ^B */
    F_TTY_INT,			/* ^C */
    F_LIST_CHOICES,		/* ^D */
    F_TOEND,			/* ^E */
    F_UNASSIGNED,		/* ^F */
    F_LIST_GLOB,		/* ^G */
    F_CHARBACK,			/* ^H */
    V_CM_COMPLETE,		/* ^I */
    F_NEWLINE,			/* ^J */
    F_KILLEND,			/* ^K */
    F_CLEARDISP,		/* ^L */
    F_NEWLINE,			/* ^M */
    F_DOWN_HIST,		/* ^N */
    F_TTY_FLUSHO,		/* ^O */
    F_UP_HIST,			/* ^P */
    F_TTY_STARTO,		/* ^Q */
    F_REDISP,			/* ^R */
    F_TTY_STOPO,		/* ^S */
    F_UNASSIGNED,		/* ^T */
    F_KILLBEG,			/* ^U */
    F_UNASSIGNED,		/* ^V */
    F_DELWORDPREV,		/* ^W */
    F_EXPAND,			/* ^X */
    F_UNASSIGNED,		/* ^Y */
    F_UNASSIGNED,		/* ^Z */
    F_METANEXT,			/* ^[ */
    F_TTY_QUIT,			/* ^\ */
    F_UNASSIGNED,		/* ^] */
    F_UNASSIGNED,		/* ^^ */
    F_UNASSIGNED,		/* ^_ */
    F_CHARFWD,			/* SPACE */
    F_EXPAND_HISTORY,		/* ! */
    F_UNASSIGNED,		/* " */
    F_UNASSIGNED,		/* # */
    F_TOEND,			/* $ */
    F_UNASSIGNED,		/* % */
    F_UNASSIGNED,		/* & */
    F_UNASSIGNED,		/* ' */
    F_UNASSIGNED,		/* ( */
    F_UNASSIGNED,		/* ) */
    F_EXPAND_GLOB,		/* * */
    F_DOWN_HIST,		/* + */
    V_RCHAR_BACK,		/* , */	
    F_UP_HIST,			/* - */	
    F_UNASSIGNED,		/* . */
    V_DSH_META,			/* / */
    V_ZERO,			/* 0 */
    F_ARGDIGIT,			/* 1 */
    F_ARGDIGIT,			/* 2 */
    F_ARGDIGIT,			/* 3 */
    F_ARGDIGIT,			/* 4 */
    F_ARGDIGIT,			/* 5 */
    F_ARGDIGIT,			/* 6 */
    F_ARGDIGIT,			/* 7 */
    F_ARGDIGIT,			/* 8 */
    F_ARGDIGIT,			/* 9 */
    F_UNASSIGNED,		/* : */
    V_RCHAR_FWD,		/* ; */
    F_UNASSIGNED,		/* < */
    F_UNASSIGNED,		/* = */
    F_UNASSIGNED,		/* > */
    V_USH_META,			/* ? */
    F_UNASSIGNED,		/* @ */
    V_ADDEND,			/* A */
    V_WORDBACK,			/* B */
    V_CHGTOEND,			/* C */
    F_KILLEND,			/* D */
    V_ENDWORD,			/* E */
    V_CHAR_BACK,		/* F */
    F_UNASSIGNED,		/* G */
    F_UNASSIGNED,		/* H */
    V_INSBEG,			/* I */
    F_DOWN_SEARCH_HIST,		/* J */
    F_UP_SEARCH_HIST,		/* K */
    F_UNASSIGNED,		/* L */
    F_UNASSIGNED,		/* M */
    V_RSRCH_BACK,		/* N */
    F_XKEY,			/* O */
    F_UNASSIGNED,		/* P */
    F_UNASSIGNED,		/* Q */
    V_REPLMODE,			/* R */
    V_SUBSTLINE,		/* S */
    V_CHARTO_BACK,		/* T */
    F_UNASSIGNED,		/* U */
    F_EXPAND_VARS,		/* V */
    V_WORDFWD,			/* W */
    F_DELPREV,			/* X */
    F_UNASSIGNED,		/* Y */
    F_UNASSIGNED,		/* Z */
    F_XKEY,			/* [ */
    F_UNASSIGNED,		/* \ */
    F_UNASSIGNED,		/* ] */
    F_TOBEG,			/* ^ */
    F_UNASSIGNED,		/* _ */
    F_UNASSIGNED,		/* ` */
    V_ADD,			/* a */
    F_WORDBACK,			/* b */
    V_CHGMETA,			/* c */
    V_DELMETA,			/* d */
    V_EWORD,			/* e */
    V_CHAR_FWD,			/* f */
    F_UNASSIGNED,		/* g */
    F_CHARBACK,			/* h */
    V_INSERT,			/* i */
    F_DOWN_HIST,		/* j */
    F_UP_HIST,			/* k */
    F_CHARFWD,			/* l */
    F_UNASSIGNED,		/* m */
    V_RSRCH_FWD,		/* n */
    F_UNASSIGNED,		/* o */
    F_UNASSIGNED,		/* p */
    F_UNASSIGNED,		/* q */
    V_REPLONE,			/* r */
    V_SUBSTCHAR,		/* s */
    V_CHARTO_FWD,		/* t */
    V_UNDO,			/* u */
    F_EXPAND_VARS,		/* v */
    V_WORDBEGNEXT,		/* w */
    F_DELNEXT_EOF,		/* x */
    F_UNASSIGNED,		/* y */
    F_UNASSIGNED,		/* z */
    F_UNASSIGNED,		/* { */
    F_UNASSIGNED,		/* | */
    F_UNASSIGNED,		/* } */
    V_CHGCASE,			/* ~ */
    F_DELPREV,			/* ^? */
    F_UNASSIGNED,		/* M-^@ */
    F_UNASSIGNED,		/* M-^A */
    F_UNASSIGNED,		/* M-^B */
    F_UNASSIGNED,		/* M-^C */
    F_UNASSIGNED,		/* M-^D */
    F_UNASSIGNED,		/* M-^E */
    F_UNASSIGNED,		/* M-^F */
    F_UNASSIGNED,		/* M-^G */
    F_UNASSIGNED,		/* M-^H */
    F_UNASSIGNED,		/* M-^I */
    F_UNASSIGNED,		/* M-^J */
    F_UNASSIGNED,		/* M-^K */
    F_UNASSIGNED,		/* M-^L */
    F_UNASSIGNED,		/* M-^M */
    F_UNASSIGNED,		/* M-^N */
    F_UNASSIGNED,		/* M-^O */
    F_UNASSIGNED,		/* M-^P */
    F_UNASSIGNED,		/* M-^Q */
    F_UNASSIGNED,		/* M-^R */
    F_UNASSIGNED,		/* M-^S */
    F_UNASSIGNED,		/* M-^T */
    F_UNASSIGNED,		/* M-^U */
    F_UNASSIGNED,		/* M-^V */
    F_UNASSIGNED,		/* M-^W */
    F_UNASSIGNED,		/* M-^X */
    F_UNASSIGNED,		/* M-^Y */
    F_UNASSIGNED,		/* M-^Z */
    F_UNASSIGNED,		/* M-^[ */
    F_UNASSIGNED,		/* M-^\ */
    F_UNASSIGNED,		/* M-^] */
    F_UNASSIGNED,		/* M-^^ */
    F_UNASSIGNED,		/* M-^_ */
    F_UNASSIGNED,		/* M-SPACE */
    F_UNASSIGNED,		/* M-! */
    F_UNASSIGNED,		/* M-" */
    F_UNASSIGNED,		/* M-# */
    F_UNASSIGNED,		/* M-$ */
    F_UNASSIGNED,		/* M-% */
    F_UNASSIGNED,		/* M-& */
    F_UNASSIGNED,		/* M-' */
    F_UNASSIGNED,		/* M-( */
    F_UNASSIGNED,		/* M-) */
    F_UNASSIGNED,		/* M-* */
    F_UNASSIGNED,		/* M-+ */
    F_UNASSIGNED,		/* M-, */
    F_UNASSIGNED,		/* M-- */
    F_UNASSIGNED,		/* M-. */
    F_UNASSIGNED,		/* M-/ */
    F_UNASSIGNED,		/* M-0 */
    F_UNASSIGNED,		/* M-1 */
    F_UNASSIGNED,		/* M-2 */
    F_UNASSIGNED,		/* M-3 */
    F_UNASSIGNED,		/* M-4 */
    F_UNASSIGNED,		/* M-5 */
    F_UNASSIGNED,		/* M-6 */
    F_UNASSIGNED,		/* M-7 */
    F_UNASSIGNED,		/* M-8 */
    F_UNASSIGNED,		/* M-9 */
    F_UNASSIGNED,		/* M-: */
    F_UNASSIGNED,		/* M-; */
    F_UNASSIGNED,		/* M-< */
    F_UNASSIGNED,		/* M-= */
    F_UNASSIGNED,		/* M-> */
    F_HELPME,			/* M-? */
    F_UNASSIGNED,		/* M-@ */
    F_UNASSIGNED,		/* M-A */
    F_UNASSIGNED,		/* M-B */
    F_UNASSIGNED,		/* M-C */
    F_UNASSIGNED,		/* M-D */
    F_UNASSIGNED,		/* M-E */
    F_UNASSIGNED,		/* M-F */
    F_UNASSIGNED,		/* M-G */
    F_UNASSIGNED,		/* M-H */
    F_UNASSIGNED,		/* M-I */
    F_UNASSIGNED,		/* M-J */
    F_UNASSIGNED,		/* M-K */
    F_UNASSIGNED,		/* M-L */
    F_UNASSIGNED,		/* M-M */
    F_UNASSIGNED,		/* M-N */
    F_XKEY,			/* M-O *//* extended key esc PWP Mar 88 */
    F_UNASSIGNED,		/* M-P */
    F_UNASSIGNED,		/* M-Q */
    F_UNASSIGNED,		/* M-R */
    F_UNASSIGNED,		/* M-S */
    F_UNASSIGNED,		/* M-T */
    F_UNASSIGNED,		/* M-U */
    F_UNASSIGNED,		/* M-V */
    F_UNASSIGNED,		/* M-W */
    F_UNASSIGNED,		/* M-X */
    F_UNASSIGNED,		/* M-Y */
    F_UNASSIGNED,		/* M-Z */
    F_XKEY,			/* M-[ *//* extended key esc -mf Oct 87 */
    F_UNASSIGNED,		/* M-\ */
    F_UNASSIGNED,		/* M-] */
    F_UNASSIGNED,		/* M-^ */
    F_UNASSIGNED,		/* M-_ */
    F_UNASSIGNED,		/* M-` */
    F_UNASSIGNED,		/* M-a */
    F_UNASSIGNED,		/* M-b */
    F_UNASSIGNED,		/* M-c */
    F_UNASSIGNED,		/* M-d */
    F_UNASSIGNED,		/* M-e */
    F_UNASSIGNED,		/* M-f */
    F_UNASSIGNED,		/* M-g */
    F_UNASSIGNED,		/* M-h */
    F_UNASSIGNED,		/* M-i */
    F_UNASSIGNED,		/* M-j */
    F_UNASSIGNED,		/* M-k */
    F_UNASSIGNED,		/* M-l */
    F_UNASSIGNED,		/* M-m */
    F_UNASSIGNED,		/* M-n */
    F_UNASSIGNED,		/* M-o */
    F_UNASSIGNED,		/* M-p */
    F_UNASSIGNED,		/* M-q */
    F_UNASSIGNED,		/* M-r */
    F_UNASSIGNED,		/* M-s */
    F_UNASSIGNED,		/* M-t */
    F_UNASSIGNED,		/* M-u */
    F_UNASSIGNED,		/* M-v */
    F_UNASSIGNED,		/* M-w */
    F_UNASSIGNED,		/* M-x */
    F_UNASSIGNED,		/* M-y */
    F_UNASSIGNED,		/* M-z */
    F_UNASSIGNED,		/* M-{ */
    F_UNASSIGNED,		/* M-| */
    F_UNASSIGNED,		/* M-} */
    F_UNASSIGNED,		/* M-~ */
    F_UNASSIGNED		/* M-^? */
};
#endif /* WINNT_NATIVE */


void
editinit(void)
{
    struct KeyFuncs *f;

#if defined(NLS_CATALOGS) || defined(WINNT_NATIVE)
    int i;

    for (i = 0; i < F_NUM_FUNCNAMES; i++)
	xfree((ptr_t)(intptr_t)FuncNames[i].desc);
#endif

    f = FuncNames;
    f->name = "backward-char";
    f->func = F_CHARBACK;
    f->desc = CSAVS(3, 1, "Move back a character");

    f++;
    f->name = "backward-delete-char";
    f->func = F_DELPREV;
    f->desc = CSAVS(3, 2, "Delete the character behind cursor");

    f++;
    f->name = "backward-delete-word";
    f->func = F_DELWORDPREV;
    f->desc = CSAVS(3, 3,
	"Cut from beginning of current word to cursor - saved in cut buffer");

    f++;
    f->name = "backward-kill-line";
    f->func = F_KILLBEG;
    f->desc = CSAVS(3, 4,
	"Cut from beginning of line to cursor - save in cut buffer");

    f++;
    f->name = "backward-word";
    f->func = F_WORDBACK;
    f->desc = CSAVS(3, 5, "Move to beginning of current word");

    f++;
    f->name = "beginning-of-line";
    f->func = F_TOBEG;
    f->desc = CSAVS(3, 6, "Move to beginning of line");

    f++;
    f->name = "capitalize-word";
    f->func = F_CASECAPITAL;
    f->desc = CSAVS(3, 7,
	"Capitalize the characters from cursor to end of current word");

    f++;
    f->name = "change-case";
    f->func = V_CHGCASE;
    f->desc = CSAVS(3, 8,
	"Vi change case of character under cursor and advance one character");

    f++;
    f->name = "change-till-end-of-line";
    f->func = V_CHGTOEND;	/* backward compat. */
    f->desc = CSAVS(3, 9, "Vi change to end of line");

    f++;
    f->name = "clear-screen";
    f->func = F_CLEARDISP;
    f->desc = CSAVS(3, 10, "Clear screen leaving current line on top");

    f++;
    f->name = "complete-word";
    f->func = F_COMPLETE;
    f->desc = CSAVS(3, 11, "Complete current word");

    f++;
    f->name = "complete-word-fwd";
    f->func = F_COMPLETE_FWD;
    f->desc = CSAVS(3, 12, "Tab forward through files");

    f++;
    f->name = "complete-word-back";
    f->func = F_COMPLETE_BACK;
    f->desc = CSAVS(3, 13, "Tab backward through files");

    f++;
    f->name = "complete-word-raw";
    f->func = F_COMPLETE_ALL;
    f->desc = CSAVS(3, 14,
	"Complete current word ignoring programmable completions");

    f++;
    f->name = "copy-prev-word";
    f->func = F_COPYPREV;
    f->desc = CSAVS(3, 15, "Copy current word to cursor");

    f++;
    f->name = "copy-region-as-kill";
    f->func = F_COPYREGION;
    f->desc = CSAVS(3, 16, "Copy area between mark and cursor to cut buffer");

    f++;
    f->name = "dabbrev-expand";
    f->func = F_DABBREV_EXPAND;
    f->desc = CSAVS(3, 17,
		    "Expand to preceding word for which this is a prefix");

    f++;
    f->name = "delete-char";
    f->func = F_DELNEXT;
    f->desc = CSAVS(3, 18, "Delete character under cursor");

    f++;
    f->name = "delete-char-or-eof";
    f->func = F_DELNEXT_EOF;
    f->desc = CSAVS(3, 19,
	"Delete character under cursor or signal end of file on an empty line");

    f++;
    f->name = "delete-char-or-list";
    f->func = F_DELNEXT_LIST;
    f->desc = CSAVS(3, 20,
	"Delete character under cursor or list completions if at end of line");

    f++;
    f->name = "delete-char-or-list-or-eof";
    f->func = F_DELNEXT_LIST_EOF;
    f->desc = CSAVS(3, 21,
    "Delete character under cursor, list completions or signal end of file");

    f++;
    f->name = "delete-word";
    f->func = F_DELWORDNEXT;
    f->desc = CSAVS(3, 22,
	"Cut from cursor to end of current word - save in cut buffer");

    f++;
    f->name = "digit";
    f->func = F_DIGIT;
    f->desc = CSAVS(3, 23, "Adds to argument if started or enters digit");

    f++;
    f->name = "digit-argument";
    f->func = F_ARGDIGIT;
    f->desc = CSAVS(3, 24, "Digit that starts argument");

    f++;
    f->name = "down-history";
    f->func = F_DOWN_HIST;
    f->desc = CSAVS(3, 25, "Move to next history line");

    f++;
    f->name = "downcase-word";
    f->func = F_CASELOWER;
    f->desc = CSAVS(3, 26,
	"Lowercase the characters from cursor to end of current word");

    f++;
    f->name = "end-of-file";
    f->func = F_SEND_EOF;
    f->desc = CSAVS(3, 27, "Indicate end of file");

    f++;
    f->name = "end-of-line";
    f->func = F_TOEND;
    f->desc = CSAVS(3, 28, "Move cursor to end of line");

    f++;
    f->name = "exchange-point-and-mark";
    f->func = F_EXCHANGE_MARK;
    f->desc = CSAVS(3, 29, "Exchange the cursor and mark");

    f++;
    f->name = "expand-glob";
    f->func = F_EXPAND_GLOB;
    f->desc = CSAVS(3, 30, "Expand file name wildcards");

    f++;
    f->name = "expand-history";
    f->func = F_EXPAND_HISTORY;
    f->desc = CSAVS(3, 31, "Expand history escapes");

    f++;
    f->name = "expand-line";
    f->func = F_EXPAND;
    f->desc = CSAVS(3, 32, "Expand the history escapes in a line");

    f++;
    f->name = "expand-variables";
    f->func = F_EXPAND_VARS;
    f->desc = CSAVS(3, 33, "Expand variables");

    f++;
    f->name = "forward-char";
    f->func = F_CHARFWD;
    f->desc = CSAVS(3, 34, "Move forward one character");

    f++;
    f->name = "forward-word";
    f->func = F_WORDFWD;
    f->desc = CSAVS(3, 35, "Move forward to end of current word");

    f++;
    f->name = "gosmacs-transpose-chars";
    f->func = F_GCHARSWITCH;
    f->desc = CSAVS(3, 36, "Exchange the two characters before the cursor");

    f++;
    f->name = "history-search-backward";
    f->func = F_UP_SEARCH_HIST;
    f->desc = CSAVS(3, 37,
	"Search in history backward for line beginning as current");

    f++;
    f->name = "history-search-forward";
    f->func = F_DOWN_SEARCH_HIST;
    f->desc = CSAVS(3, 38,
	"Search in history forward for line beginning as current");

    f++;
    f->name = "insert-last-word";
    f->func = F_LAST_ITEM;
    f->desc = CSAVS(3, 39, "Insert last item of previous command");

    f++;
    f->name = "i-search-fwd";
    f->func = F_INC_FWD;
    f->desc = CSAVS(3, 40, "Incremental search forward");

    f++;
    f->name = "i-search-back";
    f->func = F_INC_BACK;
    f->desc = CSAVS(3, 41, "Incremental search backward");

    f++;
    f->name = "keyboard-quit";
    f->func = F_STARTOVER;
    f->desc = CSAVS(3, 42, "Clear line");

    f++;
    f->name = "kill-line";
    f->func = F_KILLEND;
    f->desc = CSAVS(3, 43, "Cut to end of line and save in cut buffer");

    f++;
    f->name = "kill-region";
    f->func = F_KILLREGION;
    f->desc = CSAVS(3, 44,
	"Cut area between mark and cursor and save in cut buffer");

    f++;
    f->name = "kill-whole-line";
    f->func = F_KILLALL;
    f->desc = CSAVS(3, 45, "Cut the entire line and save in cut buffer");

    f++;
    f->name = "list-choices";
    f->func = F_LIST_CHOICES;
    f->desc = CSAVS(3, 46, "List choices for completion");

    f++;
    f->name = "list-choices-raw";
    f->func = F_LIST_ALL;
    f->desc = CSAVS(3, 47,
	"List choices for completion overriding programmable completion");

    f++;
    f->name = "list-glob";
    f->func = F_LIST_GLOB;
    f->desc = CSAVS(3, 48, "List file name wildcard matches");

    f++;
    f->name = "list-or-eof";
    f->func = F_LIST_EOF;
    f->desc = CSAVS(3, 49,
	"List choices for completion or indicate end of file if empty line");

    f++;
    f->name = "load-average";
    f->func = F_LOAD_AVERAGE;
    f->desc = CSAVS(3, 50, "Display load average and current process status");

    f++;
    f->name = "magic-space";
    f->func = F_MAGIC_SPACE;
    f->desc = CSAVS(3, 51, "Expand history escapes and insert a space");

    f++;
    f->name = "newline";
    f->func = F_NEWLINE;
    f->desc = CSAVS(3, 52, "Execute command");

    f++;
    f->name = "newline-and-hold";
    f->func = F_NEWLINE_HOLD;
    f->desc = CSAVS(3, 122, "Execute command and keep current line");

    f++;
    f->name = "newline-and-down-history";
    f->func = F_NEWLINE_DOWN_HIST;
    f->desc = CSAVS(3, 123, "Execute command and move to next history line");

    f++;
    f->name = "normalize-path";
    f->func = F_PATH_NORM;
    f->desc = CSAVS(3, 53, 
		    "Expand pathnames, eliminating leading .'s and ..'s");

    f++;
    f->name = "normalize-command";
    f->func = F_COMMAND_NORM;
    f->desc = CSAVS(3, 54, 
		    "Expand commands to the resulting pathname or alias");

    f++;
    f->name = "overwrite-mode";
    f->func = F_INSOVR;
    f->desc = CSAVS(3, 55,
		    "Switch from insert to overwrite mode or vice versa");

    f++;
    f->name = "prefix-meta";
    f->func = F_METANEXT;
    f->desc = CSAVS(3, 56, "Add 8th bit to next character typed");

    f++;
    f->name = "quoted-insert";
    f->func = F_QUOTE;
    f->desc = CSAVS(3, 57, "Add the next character typed to the line verbatim");

    f++;
    f->name = "redisplay";
    f->func = F_REDISP;
    f->desc = CSAVS(3, 58, "Redisplay everything");

    f++;
    f->name = "run-fg-editor";
    f->func = F_RUN_FG_EDITOR;
    f->desc = CSAVS(3, 59, "Restart stopped editor");

    f++;
    f->name = "run-help";
    f->func = F_HELPME;
    f->desc = CSAVS(3, 60, "Look for help on current command");

    f++;
    f->name = "self-insert-command";
    f->func = F_INSERT;
    f->desc = CSAVS(3, 61, "This character is added to the line");

    f++;
    f->name = "sequence-lead-in";
    f->func = F_XKEY;
    f->desc = CSAVS(3, 62,
	"This character is the first in a character sequence");

    f++;
    f->name = "set-mark-command";
    f->func = F_SET_MARK;
    f->desc = CSAVS(3, 63, "Set the mark at cursor");

    f++;
    f->name = "spell-word";
    f->func = F_CORRECT;
    f->desc = CSAVS(3, 64, "Correct the spelling of current word");

    f++;
    f->name = "spell-line";
    f->func = F_CORRECT_L;
    f->desc = CSAVS(3, 65, "Correct the spelling of entire line");

    f++;
    f->name = "stuff-char";
    f->func = F_STUFF_CHAR;
    f->desc = CSAVS(3, 66, "Send character to tty in cooked mode");

    f++;
    f->name = "toggle-literal-history";
    f->func = F_TOGGLE_HIST;
    f->desc = CSAVS(3, 67,
	"Toggle between literal and lexical current history line");

    f++;
    f->name = "transpose-chars";
    f->func = F_CHARSWITCH;
    f->desc = CSAVS(3, 68,
	"Exchange the character to the left of the cursor with the one under");

    f++;
    f->name = "transpose-gosling";
    f->func = F_GCHARSWITCH;
    f->desc = CSAVS(3, 69, "Exchange the two characters before the cursor");

    f++;
    f->name = "tty-dsusp";
    f->func = F_TTY_DSUSP;
    f->desc = CSAVS(3, 70, "Tty delayed suspend character");

    f++;
    f->name = "tty-flush-output";
    f->func = F_TTY_FLUSHO;
    f->desc = CSAVS(3, 71, "Tty flush output character");

    f++;
    f->name = "tty-sigintr";
    f->func = F_TTY_INT;
    f->desc = CSAVS(3, 72, "Tty interrupt character");

    f++;
    f->name = "tty-sigquit";
    f->func = F_TTY_QUIT;
    f->desc = CSAVS(3, 73, "Tty quit character");

    f++;
    f->name = "tty-sigtsusp";
    f->func = F_TTY_TSUSP;
    f->desc = CSAVS(3, 74, "Tty suspend character");

    f++;
    f->name = "tty-start-output";
    f->func = F_TTY_STARTO;
    f->desc = CSAVS(3, 75, "Tty allow output character");

    f++;
    f->name = "tty-stop-output";
    f->func = F_TTY_STOPO;
    f->desc = CSAVS(3, 76, "Tty disallow output character");

    f++;
    f->name = "undefined-key";
    f->func = F_UNASSIGNED;
    f->desc = CSAVS(3, 77, "Indicates unbound character");

    f++;
    f->name = "universal-argument";
    f->func = F_ARGFOUR;
    f->desc = CSAVS(3, 78, "Emacs universal argument (argument times 4)");

    f++;
    f->name = "up-history";
    f->func = F_UP_HIST;
    f->desc = CSAVS(3, 79, "Move to previous history line");

    f++;
    f->name = "upcase-word";
    f->func = F_CASEUPPER;
    f->desc = CSAVS(3, 80,
	"Uppercase the characters from cursor to end of current word");

    f++;
    f->name = "vi-beginning-of-next-word";
    f->func = V_WORDBEGNEXT;
    f->desc = CSAVS(3, 81, "Vi goto the beginning of next word");

    f++;
    f->name = "vi-add";
    f->func = V_ADD;
    f->desc = CSAVS(3, 82, "Vi enter insert mode after the cursor");

    f++;
    f->name = "vi-add-at-eol";
    f->func = V_ADDEND;
    f->desc = CSAVS(3, 83, "Vi enter insert mode at end of line");

    f++;
    f->name = "vi-chg-case";
    f->func = V_CHGCASE;
    f->desc = CSAVS(3, 84,
	"Vi change case of character under cursor and advance one character");

    f++;
    f->name = "vi-chg-meta";
    f->func = V_CHGMETA;
    f->desc = CSAVS(3, 85, "Vi change prefix command");

    f++;
    f->name = "vi-chg-to-eol";
    f->func = V_CHGTOEND;
    f->desc = CSAVS(3, 86, "Vi change to end of line");

    f++;
    f->name = "vi-cmd-mode";
    f->func = V_CMD_MODE;
    f->desc = CSAVS(3, 87,
	"Enter vi command mode (use alternative key bindings)");

    f++;
    f->name = "vi-cmd-mode-complete";
    f->func = V_CM_COMPLETE;
    f->desc = CSAVS(3, 88, "Vi command mode complete current word");

    f++;
    f->name = "vi-delprev";
    f->func = V_DELPREV;
    f->desc = CSAVS(3, 89, "Vi move to previous character (backspace)");

    f++;
    f->name = "vi-delmeta";
    f->func = V_DELMETA;
    f->desc = CSAVS(3, 90, "Vi delete prefix command");

    f++;
    f->name = "vi-endword";
    f->func = V_ENDWORD;
    f->desc = CSAVS(3, 91,
	"Vi move to the end of the current space delimited word");

    f++;
    f->name = "vi-eword";
    f->func = V_EWORD;
    f->desc = CSAVS(3, 92, "Vi move to the end of the current word");

    f++;
    f->name = "vi-char-back";
    f->func = V_CHAR_BACK;
    f->desc = CSAVS(3, 93, "Vi move to the character specified backward");

    f++;
    f->name = "vi-char-fwd";
    f->func = V_CHAR_FWD;
    f->desc = CSAVS(3, 94, "Vi move to the character specified forward");

    f++;
    f->name = "vi-charto-back";
    f->func = V_CHARTO_BACK;
    f->desc = CSAVS(3, 95, "Vi move up to the character specified backward");

    f++;
    f->name = "vi-charto-fwd";
    f->func = V_CHARTO_FWD;
    f->desc = CSAVS(3, 96, "Vi move up to the character specified forward");

    f++;
    f->name = "vi-insert";
    f->func = V_INSERT;
    f->desc = CSAVS(3, 97, "Enter vi insert mode");

    f++;
    f->name = "vi-insert-at-bol";
    f->func = V_INSBEG;
    f->desc = CSAVS(3, 98, "Enter vi insert mode at beginning of line");

    f++;
    f->name = "vi-repeat-char-fwd";
    f->func = V_RCHAR_FWD;
    f->desc = CSAVS(3, 99,
	"Vi repeat current character search in the same search direction");

    f++;
    f->name = "vi-repeat-char-back";
    f->func = V_RCHAR_BACK;
    f->desc = CSAVS(3, 100,
	"Vi repeat current character search in the opposite search direction");

    f++;
    f->name = "vi-repeat-search-fwd";
    f->func = V_RSRCH_FWD;
    f->desc = CSAVS(3, 101,
	"Vi repeat current search in the same search direction");

    f++;
    f->name = "vi-repeat-search-back";
    f->func = V_RSRCH_BACK;
    f->desc = CSAVS(3, 102,
	"Vi repeat current search in the opposite search direction");

    f++;
    f->name = "vi-replace-char";
    f->func = V_REPLONE;
    f->desc = CSAVS(3, 103,
	"Vi replace character under the cursor with the next character typed");

    f++;
    f->name = "vi-replace-mode";
    f->func = V_REPLMODE;
    f->desc = CSAVS(3, 104, "Vi replace mode");

    f++;
    f->name = "vi-search-back";
    f->func = V_USH_META;
    f->desc = CSAVS(3, 105, "Vi search history backward");

    f++;
    f->name = "vi-search-fwd";
    f->func = V_DSH_META;
    f->desc = CSAVS(3, 106, "Vi search history forward");

    f++;
    f->name = "vi-substitute-char";
    f->func = V_SUBSTCHAR;
    f->desc = CSAVS(3, 107,
	"Vi replace character under the cursor and enter insert mode");

    f++;
    f->name = "vi-substitute-line";
    f->func = V_SUBSTLINE;
    f->desc = CSAVS(3, 108, "Vi replace entire line");

    f++;
    f->name = "vi-word-back";
    f->func = V_WORDBACK;
    f->desc = CSAVS(3, 109, "Vi move to the previous word");

    f++;
    f->name = "vi-word-fwd";
    f->func = V_WORDFWD;
    f->desc = CSAVS(3, 110, "Vi move to the next word");

    f++;
    f->name = "vi-undo";
    f->func = V_UNDO;
    f->desc = CSAVS(3, 111, "Vi undo last change");

    f++;
    f->name = "vi-zero";
    f->func = V_ZERO;
    f->desc = CSAVS(3, 112, "Vi goto the beginning of line");

    f++;
    f->name = "which-command";
    f->func = F_WHICH;
    f->desc = CSAVS(3, 113, "Perform which of current command");

    f++;
    f->name = "yank";
    f->func = F_YANK_KILL;
    f->desc = CSAVS(3, 114, "Paste cut buffer at cursor position");

    f++;
    f->name = "yank-pop";
    f->func = F_YANK_POP;
    f->desc = CSAVS(3, 115,
	"Replace just-yanked text with yank from earlier kill");

    f++;
    f->name = "e_copy_to_clipboard";
    f->func = F_COPY_CLIP;
    f->desc = CSAVS(3, 116,
	"(WIN32 only) Copy cut buffer to system clipboard");

    f++;
    f->name = "e_paste_from_clipboard";
    f->func = F_PASTE_CLIP;
    f->desc = CSAVS(3, 117,
	"(WIN32 only) Paste clipboard buffer at cursor position");

    f++;
    f->name = "e_dosify_next";
    f->func = F_DOSIFY_NEXT;
    f->desc = CSAVS(3, 118,
	"(WIN32 only) Convert each '/' in next word to '\\\\'");

    f++;
    f->name = "e_dosify_prev";
    f->func = F_DOSIFY_PREV;
    f->desc = CSAVS(3, 119,
	"(WIN32 only) Convert each '/' in previous word to '\\\\'");

    f++;
    f->name = "e_page_up";
    f->func = F_PAGE_UP;
    f->desc = CSAVS(3, 120, "(WIN32 only) Page visible console window up");

    f++;
    f->name = "e_page_down";
    f->func = F_PAGE_DOWN;
    f->desc = CSAVS(3, 121, "(WIN32 only) Page visible console window down");

    f++;
    f->name = NULL;
    f->func = 0;
    f->desc = NULL;

    f++;
    if (f - FuncNames != F_NUM_FUNCNAMES)
	abort();
}

#ifdef DEBUG_EDIT
void
CheckMaps(void)
{		/* check the size of the key maps */
    size_t     c1 = NT_NUM_KEYS * sizeof(KEYCMD);

    if (sizeof(CcKeyMap) != c1)
	xprintf("CcKeyMap should be %u entries, but is %zu.\r\n",
		NT_NUM_KEYS, sizeof(CcKeyMap) / sizeof(KEYCMD));

    if (sizeof(CcAltMap) != c1)
	xprintf("CcAltMap should be %u entries, but is %zu.\r\n",
		NT_NUM_KEYS, sizeof(CcAltMap) / sizeof(KEYCMD));

    if (sizeof(CcEmacsMap) != c1)
	xprintf("CcEmacsMap should be %u entries, but is %zu.\r\n",
		NT_NUM_KEYS, sizeof(CcEmacsMap) / sizeof(KEYCMD));

    if (sizeof(CcViMap) != c1)
	xprintf("CcViMap should be %u entries, but is %zu.\r\n",
		NT_NUM_KEYS, sizeof(CcViMap) / sizeof(KEYCMD));

    if (sizeof(CcViCmdMap) != c1)
	xprintf("CcViCmdMap should be %u entries, but is %zu.\r\n",
		NT_NUM_KEYS, sizeof(CcViCmdMap) / sizeof(KEYCMD));
}

#endif

int    MapsAreInited = 0;
int    NLSMapsAreInited = 0;
int    NoNLSRebind;

void
ed_InitNLSMaps(void)
{
    int i;

    if (AsciiOnly)
	return;
    if (NoNLSRebind)
	return;
    for (i = 0200; i <= 0377; i++) {
	if (Isprint(CTL_ESC(i))) {
	    CcKeyMap[CTL_ESC(i)] = F_INSERT;
	}
    }
    NLSMapsAreInited = 1;
}

static void
ed_InitMetaBindings(void)
{
    Char    buf[3];
    int     i;
    CStr    cstr;
    KEYCMD *map;

    map = CcKeyMap;
    for (i = 0; i <= 0377 && CcKeyMap[CTL_ESC(i)] != F_METANEXT; i++)
	continue;
    if (i > 0377) {
	for (i = 0; i <= 0377 && CcAltMap[CTL_ESC(i)] != F_METANEXT; i++)
	    continue;
	if (i > 0377) {
	    i = '\033';
	    if (VImode)
		map = CcAltMap;
	}
	else {
	    map = CcAltMap;
	}
    }
    buf[0] = (Char)CTL_ESC(i);
    buf[2] = 0;
    cstr.buf = buf;
    cstr.len = 2;
    for (i = 0200; i <= 0377; i++) {
	if (map[CTL_ESC(i)] != F_INSERT && map[CTL_ESC(i)] != F_UNASSIGNED && map[CTL_ESC(i)] != F_XKEY) {
	    buf[1] = CTL_ESC(i & ASCII);
	    AddXkey(&cstr, XmapCmd((int) map[CTL_ESC(i)]), XK_CMD);
	}
    }
    map[buf[0]] = F_XKEY;
}

void
ed_InitVIMaps(void)
{
    int i;

    VImode = 1;
    setNS(STRvimode);
    update_wordchars();

    ResetXmap();
    for (i = 0; i < NT_NUM_KEYS; i++) {
	CcKeyMap[i] = CcViMap[i];
	CcAltMap[i] = CcViCmdMap[i];
    }
    ed_InitMetaBindings();
    ed_InitNLSMaps();
    ResetArrowKeys();
    BindArrowKeys();
}

void
ed_InitEmacsMaps(void)
{
    int     i;
    Char    buf[3];
    CStr    cstr;
    cstr.buf = buf;
    cstr.len = 2;

    VImode = 0;
    if (adrof(STRvimode))
	unsetv(STRvimode);
    update_wordchars();

    ResetXmap();
    for (i = 0; i < NT_NUM_KEYS; i++) {
	CcKeyMap[i] = CcEmacsMap[i];
	CcAltMap[i] = F_UNASSIGNED;
    }
    ed_InitMetaBindings();
    ed_InitNLSMaps();
    buf[0] = CTL_ESC('\030');
    buf[2] = 0;
    buf[1] = CTL_ESC('\030');
    AddXkey(&cstr, XmapCmd(F_EXCHANGE_MARK), XK_CMD);
    buf[1] = '*';
    AddXkey(&cstr, XmapCmd(F_EXPAND_GLOB),   XK_CMD);
    buf[1] = '$';
    AddXkey(&cstr, XmapCmd(F_EXPAND_VARS),   XK_CMD);
    buf[1] = 'G';
    AddXkey(&cstr, XmapCmd(F_LIST_GLOB),     XK_CMD);
    buf[1] = 'g';
    AddXkey(&cstr, XmapCmd(F_LIST_GLOB),     XK_CMD);
    buf[1] = 'n';
    AddXkey(&cstr, XmapCmd(F_PATH_NORM),     XK_CMD);
    buf[1] = 'N';
    AddXkey(&cstr, XmapCmd(F_PATH_NORM),     XK_CMD);
    buf[1] = '?';
    AddXkey(&cstr, XmapCmd(F_COMMAND_NORM),  XK_CMD);
    buf[1] = '\t';
    AddXkey(&cstr, XmapCmd(F_COMPLETE_ALL),  XK_CMD);
    buf[1] = CTL_ESC('\004');	/* ^D */
    AddXkey(&cstr, XmapCmd(F_LIST_ALL),      XK_CMD);
    ResetArrowKeys();
    BindArrowKeys();
}

void
ed_InitMaps(void)
{
    if (MapsAreInited)
	return;
#ifndef IS_ASCII
    /* This machine has an EBCDIC charset. The assumptions made for the
     * initialized keymaps therefore don't hold, since they are based on
     * ASCII (or ISO8859-1).
     * Here, we do a one-time transformation to EBCDIC environment
     * for the key initializations.
     */
    {
	KEYCMD temp[NT_NUM_KEYS];
	static KEYCMD *const list[3] = { CcEmacsMap, CcViMap, CcViCmdMap };
	int i, table;

	for (table=0; table<3; ++table)
	{
	    /* copy ASCII ordered map to temp table */
	    for (i = 0; i < NT_NUM_KEYS; i++) {
		temp[i] = list[table][i];
	    }
	    /* write back as EBCDIC ordered map */
	    for (i = 0; i < NT_NUM_KEYS; i++) {
		list[table][_toebcdic[i]] = temp[i];
	    }
	}
    }
#endif /* !IS_ASCII */

#ifdef VIDEFAULT
    ed_InitVIMaps();
#else
    ed_InitEmacsMaps();
#endif

    MapsAreInited = 1;
}

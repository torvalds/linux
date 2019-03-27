/* $Header: /p/tcsh/cvsroot/tcsh/ed.decls.h,v 3.46 2015/08/19 14:29:55 christos Exp $ */
/*
 * ed.decls.h: Editor external definitions
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
#ifndef _h_ed_decls
#define _h_ed_decls

/*
 * ed.chared.c
 */
extern	int	InsertStr		(Char *);
extern	int	ExpandHistory		(void);
extern	void	DeleteBack		(int);
extern	void	SetKillRing		(int);
extern	CCRETVAL GetHistLine		(void);

/*
 * ed.init.c
 */
#ifdef SIG_WINDOW
extern	void	check_window_size	(int);
extern	void	window_change		(int);
#endif /* SIG_WINDOW */
extern	int	ed_Setup		(int);
extern	void	ed_Init			(void);
extern	int	Cookedmode		(void);
extern	int	Rawmode			(void);
extern	void	ed_set_tty_eight_bit	(void);

extern	void	QuoteModeOn		(void);
extern	void	QuoteModeOff		(void);
extern	void	ResetInLine		(int);
extern	int	Load_input_line		(void);

/*
 * ed.term.c:
 */
extern	void	dosetty			(Char **, struct command *);
extern	int	tty_getty 		(int, ttydata_t *);
extern	int	tty_setty 		(int, ttydata_t *);
extern	void	tty_getchar 		(ttydata_t *, unsigned char *);
extern	void	tty_setchar 		(ttydata_t *, unsigned char *);
extern	speed_t	tty_getspeed 		(ttydata_t *);
extern	int	tty_gettabs 		(ttydata_t *);
extern	int	tty_geteightbit		(ttydata_t *);
extern	int	tty_cooked_mode		(ttydata_t *);
#ifdef _IBMR2
extern	void	tty_setdisc		(int, int);
#endif /* _IBMR2 */

/*
 * ed.screen.c
 */
extern	void	terminit		(void);
extern	void	SetAttributes		(Char);
extern	void	so_write		(Char *, int);
extern	void	ClearScreen		(void);
extern	void	MoveToLine		(int);
extern	void	MoveToChar		(int);
extern	void	ClearEOL		(int);
extern	void	Insert_write		(Char *, int);
extern	void	DeleteChars		(int);
extern	void	TellTC			(void);
extern	void	SetTC			(char *, char *);
extern	void	EchoTC			(Char **);
extern	int 	SetArrowKeys		(const CStr *, XmapVal *, int);
extern	int 	IsArrowKey		(Char *);
extern	void	ResetArrowKeys		(void);
extern	void	DefaultArrowKeys	(void);
extern	int 	ClearArrowKeys		(const CStr *);
extern	void 	PrintArrowKeys		(const CStr *);
extern	void	BindArrowKeys		(void);
extern	void	SoundBeep		(void);
extern	int	CanWeTab		(void);
extern	void	ChangeSize		(int, int);
#ifdef SIG_WINDOW
extern	int	GetSize			(int *, int *);
#endif /* SIG_WINDOW */
extern	void	ClearToBottom		(void);
extern	void	GetTermCaps		(void);
extern	void	StartHighlight		(void);
extern	void	StopHighlight		(void);

/*
 * ed.defns.c
 */
extern	void	editinit		(void);
extern	void	ed_InitNLSMaps		(void);
#ifdef DEBUG_EDIT
extern	void	CheckMaps		(void);
#endif
extern	void	ed_InitMaps		(void);
extern	void	ed_InitEmacsMaps	(void);
extern	void	ed_InitVIMaps		(void);

extern  CCRETVAL	e_unassigned		(Char);
extern	CCRETVAL	e_insert		(Char);
extern	CCRETVAL	e_newline		(Char);
extern	CCRETVAL	e_delprev		(Char);
extern	CCRETVAL	e_delnext		(Char);
/* added by mtk@ari.ncl.omron.co.jp (920818) */
extern	CCRETVAL	e_delnext_eof		(Char);
extern	CCRETVAL	e_delnext_list		(Char);
extern	CCRETVAL	e_delnext_list_eof	(Char);	/* for ^D */
extern	CCRETVAL	e_toend			(Char);
extern	CCRETVAL	e_tobeg			(Char);
extern	CCRETVAL	e_charback		(Char);
extern	CCRETVAL	e_charfwd		(Char);
extern	CCRETVAL	e_quote			(Char);
extern	CCRETVAL	e_startover		(Char);
extern	CCRETVAL	e_redisp		(Char);
extern	CCRETVAL	e_wordback		(Char);
extern	CCRETVAL	e_wordfwd		(Char);
extern	CCRETVAL	v_wordbegnext		(Char);
extern	CCRETVAL	e_uppercase		(Char);
extern	CCRETVAL	e_lowercase		(Char);
extern	CCRETVAL	e_capitalcase		(Char);
extern	CCRETVAL	e_cleardisp		(Char);
extern	CCRETVAL	e_complete		(Char);
extern	CCRETVAL	e_correct		(Char);
extern	CCRETVAL	e_correctl		(Char);
extern	CCRETVAL	e_up_hist		(Char);
extern	CCRETVAL	e_down_hist		(Char);
extern	CCRETVAL	e_up_search_hist	(Char);
extern	CCRETVAL	e_down_search_hist	(Char);
extern	CCRETVAL	e_helpme		(Char);
extern	CCRETVAL	e_list_choices		(Char);
extern	CCRETVAL	e_delwordprev		(Char);
extern	CCRETVAL	e_delwordnext		(Char);
extern	CCRETVAL	e_digit			(Char);
extern	CCRETVAL	e_argdigit		(Char);
extern	CCRETVAL	v_zero			(Char);
extern	CCRETVAL	e_killend		(Char);
extern	CCRETVAL	e_killbeg		(Char);
extern	CCRETVAL	e_metanext		(Char);
#ifdef notdef
extern	CCRETVAL	e_extendnext		(Char);
#endif
extern	CCRETVAL	e_send_eof		(Char);
extern	CCRETVAL	e_charswitch		(Char);
extern	CCRETVAL	e_gcharswitch		(Char);
extern	CCRETVAL	e_which			(Char);
extern	CCRETVAL	e_yank_kill		(Char);
extern	CCRETVAL	e_tty_dsusp		(Char);
extern	CCRETVAL	e_tty_flusho		(Char);
extern	CCRETVAL	e_tty_quit		(Char);
extern	CCRETVAL	e_tty_tsusp		(Char);
extern	CCRETVAL	e_tty_stopo		(Char);
extern	CCRETVAL	e_tty_starto		(Char);
extern	CCRETVAL	e_argfour		(Char);
extern	CCRETVAL	e_set_mark		(Char);
extern	CCRETVAL	e_exchange_mark		(Char);
extern	CCRETVAL	e_last_item		(Char);
extern	CCRETVAL	v_cmd_mode		(Char);
extern	CCRETVAL	v_insert		(Char);
extern	CCRETVAL	v_replmode		(Char);
extern	CCRETVAL	v_replone		(Char);
extern	CCRETVAL	v_substline		(Char);
extern	CCRETVAL	v_substchar		(Char);
extern	CCRETVAL	v_add			(Char);
extern	CCRETVAL	v_addend		(Char);
extern	CCRETVAL	v_insbeg		(Char);
extern	CCRETVAL	v_chgtoend		(Char);
extern	CCRETVAL	e_killregion		(Char);
extern	CCRETVAL	e_killall		(Char);
extern	CCRETVAL	e_copyregion		(Char);
extern	CCRETVAL	e_tty_int		(Char);
extern	CCRETVAL	e_run_fg_editor		(Char);
extern	CCRETVAL	e_list_eof		(Char);
extern	int     	e_expand_history_rne	(Char);
extern	CCRETVAL	e_expand_history	(Char);
extern	CCRETVAL	e_magic_space		(Char);
extern	CCRETVAL	e_list_glob		(Char);
extern	CCRETVAL	e_expand_glob		(Char);
extern	CCRETVAL	e_insovr		(Char);
extern	CCRETVAL	v_cm_complete		(Char);
extern	CCRETVAL	e_copyprev		(Char);
extern	CCRETVAL	v_change_case		(Char);
extern	CCRETVAL	e_expand		(Char);
extern	CCRETVAL	e_expand_vars		(Char);
extern	CCRETVAL	e_toggle_hist		(Char);
extern  CCRETVAL        e_load_average		(Char);
extern  CCRETVAL        v_delprev		(Char);
extern  CCRETVAL        v_delmeta		(Char);
extern  CCRETVAL        v_wordfwd		(Char);
extern  CCRETVAL        v_wordback		(Char);
extern  CCRETVAL        v_endword		(Char);
extern  CCRETVAL        v_eword			(Char);
extern  CCRETVAL        v_undo			(Char);
extern  CCRETVAL        v_ush_meta		(Char);
extern  CCRETVAL        v_dsh_meta		(Char);
extern  CCRETVAL        v_rsrch_fwd		(Char);
extern  CCRETVAL        v_rsrch_back		(Char);
extern  CCRETVAL        v_char_fwd		(Char);
extern  CCRETVAL        v_char_back		(Char);
extern  CCRETVAL        v_chgmeta		(Char);
extern	CCRETVAL	e_inc_fwd		(Char);
extern	CCRETVAL	e_inc_back		(Char);
extern	CCRETVAL	v_rchar_fwd		(Char);
extern	CCRETVAL	v_rchar_back		(Char);
extern  CCRETVAL        v_charto_fwd		(Char);
extern  CCRETVAL        v_charto_back		(Char);
extern  CCRETVAL        e_normalize_path	(Char);
extern  CCRETVAL        e_normalize_command	(Char);
extern  CCRETVAL        e_stuff_char		(Char);
extern  CCRETVAL        e_list_all		(Char);
extern  CCRETVAL        e_complete_all		(Char);
extern  CCRETVAL        e_complete_fwd		(Char);
extern  CCRETVAL        e_complete_back		(Char);
extern  CCRETVAL        e_dabbrev_expand	(Char);
extern  CCRETVAL	e_copy_to_clipboard	(Char);
extern  CCRETVAL	e_paste_from_clipboard	(Char);
extern  CCRETVAL	e_dosify_next		(Char);
extern  CCRETVAL	e_dosify_prev		(Char);
extern  CCRETVAL	e_page_up		(Char);
extern  CCRETVAL	e_page_down		(Char);
extern  CCRETVAL	e_yank_pop		(Char);
extern  CCRETVAL	e_newline_hold		(Char);
extern  CCRETVAL	e_newline_down_hist	(Char);

/*
 * ed.inputl.c
 */
extern	int	Inputl			(void);
extern	int	GetNextChar		(Char *);
extern	void    UngetNextChar		(Char);
extern	void	PushMacro		(Char *);

/*
 * ed.refresh.c
 */
extern	void	ClearLines		(void);
extern	void	ClearDisp		(void);
extern	void	Refresh			(void);
extern	void	RefCursor		(void);
extern	void	RefPlusOne		(int);
extern	void	PastBottom		(void);

/*
 * ed.xmap.c
 */
extern  XmapVal *XmapStr		(CStr *);
extern  XmapVal *XmapCmd		(int);
extern	void	 AddXkey		(const CStr *, XmapVal *, int);
extern	void	 ClearXkey		(KEYCMD *, const CStr *);
extern	int	 GetXkey		(CStr *, XmapVal *);
extern	void	 ResetXmap		(void);
extern	int	 DeleteXkey		(const CStr *);
extern	void	 PrintXkey		(const CStr *);
extern	void	 printOne		(const Char *, const XmapVal *, int);
extern	eChar		  parseescape	(const Char **);
extern	unsigned char    *unparsestring	(const CStr *, const Char *);

#endif /* _h_ed_decls */

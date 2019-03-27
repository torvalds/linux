/*	$NetBSD: curses_commands.h,v 1.3 2011/09/15 11:46:19 blymn Exp $	*/

/*-
 * Copyright 2009 Brett Lymn <blymn@NetBSD.org>
 *
 * All rights reserved.
 *
 * This code has been donated to The NetBSD Foundation by the Author.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software withough specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *
 */

#ifndef _CURSES_COMMANDS_H_
#define	_CURSES_COMMANDS_H_

struct command_def {
	const char *name;
	void (*func)(int, char **);
};

/*
 * prototypes for test commands
 */
void cmd_DRAIN(int, char **); /* not a curses function */

void cmd_addbytes(int, char **);
void cmd_addch(int, char **);
void cmd_addchnstr(int, char **);
void cmd_addchstr(int, char **);
void cmd_addnstr(int, char **);
void cmd_addstr(int, char **);
void cmd_attr_get(int, char **);
void cmd_attr_off(int, char **);
void cmd_attr_on(int, char **);
void cmd_attr_set(int, char **);
void cmd_attroff(int, char **);
void cmd_attron(int, char **);
void cmd_attrset(int, char **);
void cmd_bkgd(int, char **);
void cmd_bkgdset(int, char **);
void cmd_border(int, char **);
void cmd_clear(int, char **);
void cmd_clrtobot(int, char **);
void cmd_clrtoeol(int, char **);
void cmd_color_set(int, char **);
void cmd_delch(int, char **);
void cmd_deleteln(int, char **);
void cmd_echochar(int, char **);
void cmd_erase(int, char **);
void cmd_getch(int, char **);
void cmd_getnstr(int, char **);
void cmd_getstr(int, char **);
void cmd_inch(int, char **);
void cmd_inchnstr(int, char **);
void cmd_inchstr(int, char **);
void cmd_innstr(int, char **);
void cmd_insch(int, char **);
void cmd_insdelln(int, char **);
void cmd_insertln(int, char **);
void cmd_instr(int, char **);
void cmd_move(int, char **);
void cmd_refresh(int, char **);
void cmd_scrl(int, char **);
void cmd_setscrreg(int, char **);
void cmd_standend(int, char **);
void cmd_standout(int, char **);
void cmd_timeout(int, char **);
void cmd_underscore(int, char **);
void cmd_underend(int, char **);
void cmd_waddbytes(int, char **);
void cmd_waddstr(int, char **);
void cmd_mvaddbytes(int, char **);
void cmd_mvaddch(int, char **);
void cmd_mvaddchnstr(int, char **);
void cmd_mvaddchstr(int, char **);
void cmd_mvaddnstr(int, char **);
void cmd_mvaddstr(int, char **);
void cmd_mvdelch(int, char **);
void cmd_mvgetch(int, char **);
void cmd_mvgetnstr(int, char **);
void cmd_mvgetstr(int, char **);
void cmd_mvinch(int, char **);
void cmd_mvinchnstr(int, char **);
void cmd_mvinchstr(int, char **);
void cmd_mvinnstr(int, char **);
void cmd_mvinsch(int, char **);
void cmd_mvinstr(int, char **);

void cmd_mvwaddbytes(int, char **);
void cmd_mvwaddch(int, char **);
void cmd_mvwaddchnstr(int, char **);
void cmd_mvwaddchstr(int, char **);
void cmd_mvwaddnstr(int, char **);
void cmd_mvwaddstr(int, char **);
void cmd_mvwdelch(int, char **);
void cmd_mvwgetch(int, char **);
void cmd_mvwgetnstr(int, char **);
void cmd_mvwgetstr(int, char **);
void cmd_mvwinch(int, char **);
void cmd_mvwinsch(int, char **);
void cmd_assume_default_colors(int, char **);
void cmd_baudrate(int, char **);
void cmd_beep(int, char **);
void cmd_box(int, char **);
void cmd_can_change_color(int, char **);
void cmd_cbreak(int, char **);
void cmd_chgat(int, char **);
void cmd_clearok(int, char **);
void cmd_color_content(int, char **);
void cmd_copywin(int, char **);
void cmd_curs_set(int, char **);
void cmd_def_prog_mode(int, char **);
void cmd_def_shell_mode(int, char **);
void cmd_define_key(int, char **);
void cmd_delay_output(int, char **);
void cmd_delscreen(int, char **);
void cmd_delwin(int, char **);
void cmd_derwin(int, char **);
void cmd_dupwin(int, char **);
void cmd_doupdate(int, char **);
void cmd_echo(int, char **);
void cmd_endwin(int, char **);
void cmd_erasechar(int, char **);
void cmd_flash(int, char **);
void cmd_flushinp(int, char **);
void cmd_flushok(int, char **);
void cmd_fullname(int, char **);
void cmd_getattrs(int, char **);
void cmd_getbkgd(int, char **);
void cmd_getcury(int, char **);
void cmd_getcurx(int, char **);
void cmd_getyx(int, char **);
void cmd_getbegy(int, char **);
void cmd_getbegx(int, char **);
void cmd_getmaxy(int, char **);
void cmd_getmaxx(int, char **);
void cmd_getpary(int, char **);
void cmd_getparx(int, char **);
void cmd_getparyx(int, char **);
void cmd_gettmode(int, char **);
void cmd_getwin(int, char **);
void cmd_halfdelay(int, char **);
void cmd_has_colors(int, char **);
void cmd_has_ic(int, char **);
void cmd_has_il(int, char **);
void cmd_hline(int, char **);
void cmd_idcok(int, char **);
void cmd_idlok(int, char **);
void cmd_init_color(int, char **);
void cmd_init_pair(int, char **);
void cmd_initscr(int, char **);
void cmd_intrflush(int, char **);
void cmd_isendwin(int, char **);
void cmd_is_linetouched(int, char **);
void cmd_is_wintouched(int, char **);
void cmd_keyok(int, char **);
void cmd_keypad(int, char **);
void cmd_keyname(int, char **);
void cmd_killchar(int, char **);
void cmd_leaveok(int, char **);
void cmd_meta(int, char **);
void cmd_mvchgat(int, char **);
void cmd_mvcur(int, char **);
void cmd_mvderwin(int, char **);
void cmd_mvhline(int, char **);
void cmd_mvprintw(int, char **);
void cmd_mvscanw(int, char **);
void cmd_mvvline(int, char **);
void cmd_mvwchgat(int, char **);
void cmd_mvwhline(int, char **);
void cmd_mvwvline(int, char **);
void cmd_mvwin(int, char **);
void cmd_mvwinchnstr(int, char **);
void cmd_mvwinchstr(int, char **);
void cmd_mvwinnstr(int, char **);
void cmd_mvwinstr(int, char **);
void cmd_mvwprintw(int, char **);
void cmd_mvwscanw(int, char **);
void cmd_napms(int, char **);
void cmd_newpad(int, char **);
void cmd_newterm(int, char **);
void cmd_newwin(int, char **);
void cmd_nl(int, char **);
void cmd_no_color_attributes(int, char **);
void cmd_nocbreak(int, char **);
void cmd_nodelay(int, char **);
void cmd_noecho(int, char **);
void cmd_nonl(int, char **);
void cmd_noqiflush(int, char **);
void cmd_noraw(int, char **);
void cmd_notimeout(int, char **);
void cmd_overlay(int, char **);
void cmd_overwrite(int, char **);
void cmd_pair_content(int, char **);
void cmd_pechochar(int, char **);
void cmd_pnoutrefresh(int, char **);
void cmd_prefresh(int, char **);
void cmd_printw(int, char **);
void cmd_putwin(int, char **);
void cmd_qiflush(int, char **);
void cmd_raw(int, char **);
void cmd_redrawwin(int, char **);
void cmd_reset_prog_mode(int, char **);
void cmd_reset_shell_mode(int, char **);
void cmd_resetty(int, char **);
void cmd_resizeterm(int, char **);
void cmd_savetty(int, char **);
void cmd_scanw(int, char **);
void cmd_scroll(int, char **);
void cmd_scrollok(int, char **);
void cmd_setterm(int, char **);
void cmd_set_term(int, char **);
void cmd_start_color(int, char **);
void cmd_subpad(int, char **);
void cmd_subwin(int, char **);
void cmd_termattrs(int, char **);
void cmd_term_attrs(int, char **);
void cmd_touchline(int, char **);
void cmd_touchoverlap(int, char **);
void cmd_touchwin(int, char **);
void cmd_ungetch(int, char **);
void cmd_untouchwin(int, char **);
void cmd_use_default_colors(int, char **);
void cmd_vline(int, char **);
void cmd_vw_printw(int, char **);
void cmd_vw_scanw(int, char **);
void cmd_vwprintw(int, char **);
void cmd_vwscanw(int, char **);
void cmd_waddch(int, char **);
void cmd_waddchnstr(int, char **);
void cmd_waddchstr(int, char **);
void cmd_waddnstr(int, char **);
void cmd_wattr_get(int, char **);
void cmd_wattr_off(int, char **);
void cmd_wattr_on(int, char **);
void cmd_wattr_set(int, char **);
void cmd_wattroff(int, char **);
void cmd_wattron(int, char **);
void cmd_wattrset(int, char **);
void cmd_wbkgd(int, char **);
void cmd_wbkgdset(int, char **);
void cmd_wborder(int, char **);
void cmd_wchgat(int, char **);
void cmd_wclear(int, char **);
void cmd_wclrtobot(int, char **);
void cmd_wclrtoeol(int, char **);
void cmd_wcolor_set(int, char **);
void cmd_wdelch(int, char **);
void cmd_wdeleteln(int, char **);
void cmd_wechochar(int, char **);
void cmd_werase(int, char **);
void cmd_wgetch(int, char **);
void cmd_wgetnstr(int, char **);
void cmd_wgetstr(int, char **);
void cmd_whline(int, char **);
void cmd_winch(int, char **);
void cmd_winchnstr(int, char **);
void cmd_winchstr(int, char **);
void cmd_winnstr(int, char **);
void cmd_winsch(int, char **);
void cmd_winsdelln(int, char **);
void cmd_winsertln(int, char **);
void cmd_winstr(int, char **);
void cmd_wmove(int, char **);
void cmd_wnoutrefresh(int, char **);
void cmd_wprintw(int, char **);
void cmd_wredrawln(int, char **);
void cmd_wrefresh(int, char **);
void cmd_wresize(int, char **);
void cmd_wscanw(int, char **);
void cmd_wscrl(int, char **);
void cmd_wsetscrreg(int, char **);
void cmd_wstandend(int, char **);
void cmd_wstandout(int, char **);
void cmd_wtimeout(int, char **);
void cmd_wtouchln(int, char **);
void cmd_wunderend(int, char **);
void cmd_wunderscore(int, char **);
void cmd_wvline(int, char **);
void cmd_insnstr(int, char **);
void cmd_insstr(int, char **);
void cmd_mvinsnstr(int, char **);
void cmd_mvinsstr(int, char **);
void cmd_mvwinsnstr(int, char **);
void cmd_mvwinsstr(int, char **);
void cmd_winsnstr(int, char **);
void cmd_winsstr(int, char **);

void cmd_chgat(int, char **);
void cmd_wchgat(int, char **);
void cmd_mvchgat(int, char **);
void cmd_mvwchgat(int, char **);
void cmd_add_wch(int, char **);
void cmd_wadd_wch(int, char **);
void cmd_mvadd_wch(int, char **);
void cmd_mvwadd_wch(int, char **);

void cmd_add_wchnstr(int, char **);
void cmd_add_wchstr(int, char **);
void cmd_wadd_wchnstr(int, char **);
void cmd_wadd_wchstr(int, char **);
void cmd_mvadd_wchnstr(int, char **);
void cmd_mvadd_wchstr(int, char **);
void cmd_mvwadd_wchnstr(int, char **);
void cmd_mvwadd_wchstr(int, char **);

void cmd_addnwstr(int, char **);
void cmd_addwstr(int, char **);
void cmd_mvaddnwstr(int, char **);
void cmd_mvaddwstr(int, char **);
void cmd_mvwaddnwstr(int, char **);
void cmd_mvwaddwstr(int, char **);
void cmd_waddnwstr(int, char **);
void cmd_waddwstr(int, char **);

void cmd_echo_wchar(int, char **);
void cmd_wecho_wchar(int, char **);
void cmd_pecho_wchar(int, char **);

/* insert */
void cmd_ins_wch(int, char **);
void cmd_wins_wch(int, char **);
void cmd_mvins_wch(int, char **);
void cmd_mvwins_wch(int, char **);

void cmd_ins_nwstr(int, char **);
void cmd_ins_wstr(int, char **);
void cmd_mvins_nwstr(int, char **);
void cmd_mvins_wstr(int, char **);
void cmd_mvwins_nwstr(int, char **);
void cmd_mvwins_wstr(int, char **);
void cmd_wins_nwstr(int, char **);
void cmd_wins_wstr(int, char **);

/* input */
void cmd_get_wch(int, char **);
void cmd_unget_wch(int, char **);
void cmd_mvget_wch(int, char **);
void cmd_mvwget_wch(int, char **);
void cmd_wget_wch(int, char **);

void cmd_getn_wstr(int, char **);
void cmd_get_wstr(int, char **);
void cmd_mvgetn_wstr(int, char **);
void cmd_mvget_wstr(int, char **);
void cmd_mvwgetn_wstr(int, char **);
void cmd_mvwget_wstr(int, char **);
void cmd_wgetn_wstr(int, char **);
void cmd_wget_wstr(int, char **);

void cmd_in_wch(int, char **);
void cmd_mvin_wch(int, char **);
void cmd_mvwin_wch(int, char **);
void cmd_win_wch(int, char **);

void cmd_in_wchnstr(int, char **);
void cmd_in_wchstr(int, char **);
void cmd_mvin_wchnstr(int, char **);
void cmd_mvin_wchstr(int, char **);
void cmd_mvwin_wchnstr(int, char **);
void cmd_mvwin_wchstr(int, char **);
void cmd_win_wchnstr(int, char **);
void cmd_win_wchstr(int, char **);

void cmd_innwstr(int, char **);
void cmd_inwstr(int, char **);
void cmd_mvinnwstr(int, char **);
void cmd_mvinwstr(int, char **);
void cmd_mvwinnwstr(int, char **);
void cmd_mvwinwstr(int, char **);
void cmd_winnwstr(int, char **);
void cmd_winwstr(int, char **);

/* cchar handlgin */
void cmd_setcchar(int, char **);
void cmd_getcchar(int, char **);

/* misc */
void cmd_key_name(int, char **);
void cmd_border_set(int, char **);
void cmd_wborder_set(int, char **);
void cmd_box_set(int, char **);
void cmd_erasewchar(int, char **);
void cmd_killwchar(int, char **);
void cmd_hline_set(int, char **);
void cmd_mvhline_set(int, char **);
void cmd_mvvline_set(int, char **);
void cmd_mvwhline_set(int, char **);
void cmd_mvwvline_set(int, char **);
void cmd_vline_set(int, char **);
void cmd_whline_set(int, char **);
void cmd_wvline_set(int, char **);
void cmd_bkgrnd(int, char **);
void cmd_bkgrndset(int, char **);
void cmd_getbkgrnd(int, char **);
void cmd_wbkgrnd(int, char **);
void cmd_wbkgrndset(int, char **);
void cmd_wgetbkgrnd(int, char **);




#endif /* !_CURSES_COMMAND_H_ */

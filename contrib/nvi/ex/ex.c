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
static const char sccsid[] = "$Id: ex.c,v 10.80 2012/10/03 16:24:40 zy Exp $";
#endif /* not lint */

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/stat.h>

#include <bitstring.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../common/common.h"
#include "../vi/vi.h"

#if defined(DEBUG) && defined(COMLOG)
static void	ex_comlog(SCR *, EXCMD *);
#endif
static EXCMDLIST const *
		ex_comm_search(CHAR_T *, size_t);
static int	ex_discard(SCR *);
static int	ex_line(SCR *, EXCMD *, MARK *, int *, int *);
static int	ex_load(SCR *);
static void	ex_unknown(SCR *, CHAR_T *, size_t);

/*
 * ex --
 *	Main ex loop.
 *
 * PUBLIC: int ex(SCR **);
 */
int
ex(SCR **spp)
{
	EX_PRIVATE *exp;
	GS *gp;
	MSGS *mp;
	SCR *sp;
	TEXT *tp;
	u_int32_t flags;

	sp = *spp;
	gp = sp->gp;
	exp = EXP(sp);

	/* Start the ex screen. */
	if (ex_init(sp))
		return (1);

	/* Flush any saved messages. */
	while ((mp = SLIST_FIRST(gp->msgq)) != NULL) {
		gp->scr_msg(sp, mp->mtype, mp->buf, mp->len);
		SLIST_REMOVE_HEAD(gp->msgq, q);
		free(mp->buf);
		free(mp);
	}

	/* If reading from a file, errors should have name and line info. */
	if (F_ISSET(gp, G_SCRIPTED)) {
		gp->excmd.if_lno = 1;
		gp->excmd.if_name = "script";
	}

	/*
	 * !!!
	 * Initialize the text flags.  The beautify edit option historically
	 * applied to ex command input read from a file.  In addition, the
	 * first time a ^H was discarded from the input, there was a message,
	 * "^H discarded", that was displayed.  We don't bother.
	 */
	LF_INIT(TXT_BACKSLASH | TXT_CNTRLD | TXT_CR);
	for (;; ++gp->excmd.if_lno) {
		/* Display status line and flush. */
		if (F_ISSET(sp, SC_STATUS)) {
			if (!F_ISSET(sp, SC_EX_SILENT))
				msgq_status(sp, sp->lno, 0);
			F_CLR(sp, SC_STATUS);
		}
		(void)ex_fflush(sp);

		/* Set the flags the user can reset. */
		if (O_ISSET(sp, O_BEAUTIFY))
			LF_SET(TXT_BEAUTIFY);
		if (O_ISSET(sp, O_PROMPT))
			LF_SET(TXT_PROMPT);

		/* Clear any current interrupts, and get a command. */
		CLR_INTERRUPT(sp);
		if (ex_txt(sp, sp->tiq, ':', flags))
			return (1);
		if (INTERRUPTED(sp)) {
			(void)ex_puts(sp, "\n");
			(void)ex_fflush(sp);
			continue;
		}

		/* Initialize the command structure. */
		CLEAR_EX_PARSER(&gp->excmd);

		/*
		 * If the user entered a single carriage return, send
		 * ex_cmd() a separator -- it discards single newlines.
		 */
		tp = TAILQ_FIRST(sp->tiq);
		if (tp->len == 0) {
			gp->excmd.cp = L(" ");	/* __TK__ why not |? */
			gp->excmd.clen = 1;
		} else {
			gp->excmd.cp = tp->lb;
			gp->excmd.clen = tp->len;
		}
		F_INIT(&gp->excmd, E_NRSEP);

		if (ex_cmd(sp) && F_ISSET(gp, G_SCRIPTED))
			return (1);

		if (INTERRUPTED(sp)) {
			CLR_INTERRUPT(sp);
			msgq(sp, M_ERR, "170|Interrupted");
		}

		/*
		 * If the last command caused a restart, or switched screens
		 * or into vi, return.
		 */
		if (F_ISSET(gp, G_SRESTART) || F_ISSET(sp, SC_SSWITCH | SC_VI)) {
			*spp = sp;
			break;
		}

		/* If the last command switched files, we don't care. */
		F_CLR(sp, SC_FSWITCH);

		/*
		 * If we're exiting this screen, move to the next one.  By
		 * definition, this means returning into vi, so return to the
		 * main editor loop.  The ordering is careful, don't discard
		 * the contents of sp until the end.
		 */
		if (F_ISSET(sp, SC_EXIT | SC_EXIT_FORCE)) {
			if (file_end(sp, NULL, F_ISSET(sp, SC_EXIT_FORCE)))
				return (1);
			*spp = screen_next(sp);
			return (screen_end(sp));
		}
	}
	return (0);
}

/*
 * ex_cmd --
 *	The guts of the ex parser: parse and execute a string containing
 *	ex commands.
 *
 * !!!
 * This code MODIFIES the string that gets passed in, to delete quoting
 * characters, etc.  The string cannot be readonly/text space, nor should
 * you expect to use it again after ex_cmd() returns.
 *
 * !!!
 * For the fun of it, if you want to see if a vi clone got the ex argument
 * parsing right, try:
 *
 *	echo 'foo|bar' > file1; echo 'foo/bar' > file2;
 *	vi
 *	:edit +1|s/|/PIPE/|w file1| e file2|1 | s/\//SLASH/|wq
 *
 * or:	vi
 *	:set|file|append|set|file
 *
 * For extra credit, try them in a startup .exrc file.
 *
 * PUBLIC: int ex_cmd(SCR *);
 */
int
ex_cmd(SCR *sp)
{
	enum nresult nret;
	EX_PRIVATE *exp;
	EXCMD *ecp;
	GS *gp;
	MARK cur;
	recno_t lno;
	size_t arg1_len, discard, len;
	u_int32_t flags;
	long ltmp;
	int at_found, gv_found;
	int cnt, delim, isaddr, namelen;
	int newscreen, notempty, tmp, vi_address;
	CHAR_T *arg1, *s, *p, *t;
	CHAR_T ch = '\0';
	CHAR_T *n;
	char *np;

	gp = sp->gp;
	exp = EXP(sp);

	/*
	 * We always start running the command on the top of the stack.
	 * This means that *everything* must be resolved when we leave
	 * this function for any reason.
	 */
loop:	ecp = SLIST_FIRST(gp->ecq);

	/* If we're reading a command from a file, set up error information. */
	if (ecp->if_name != NULL) {
		gp->if_lno = ecp->if_lno;
		gp->if_name = ecp->if_name;
	}

	/*
	 * If a move to the end of the file is scheduled for this command,
	 * do it now.
	 */
	if (F_ISSET(ecp, E_MOVETOEND)) {
		if (db_last(sp, &sp->lno))
			goto rfail;
		sp->cno = 0;
		F_CLR(ecp, E_MOVETOEND);
	}

	/* If we found a newline, increment the count now. */
	if (F_ISSET(ecp, E_NEWLINE)) {
		++gp->if_lno;
		++ecp->if_lno;
		F_CLR(ecp, E_NEWLINE);
	}

	/* (Re)initialize the EXCMD structure, preserving some flags. */
	CLEAR_EX_CMD(ecp);

	/* Initialize the argument structures. */
	if (argv_init(sp, ecp))
		goto err;

	/* Initialize +cmd, saved command information. */
	arg1 = NULL;
	ecp->save_cmdlen = 0;

	/* Skip <blank>s, empty lines.  */
	for (notempty = 0; ecp->clen > 0; ++ecp->cp, --ecp->clen)
		if ((ch = *ecp->cp) == '\n') {
			++gp->if_lno;
			++ecp->if_lno;
		} else if (cmdskip(ch))
			notempty = 1;
		else
			break;

	/*
	 * !!!
	 * Permit extra colons at the start of the line.  Historically,
	 * ex/vi allowed a single extra one.  It's simpler not to count.
	 * The stripping is done here because, historically, any command
	 * could have preceding colons, e.g. ":g/pattern/:p" worked.
	 */
	if (ecp->clen != 0 && ch == ':') {
		notempty = 1;
		while (--ecp->clen > 0 && (ch = *++ecp->cp) == ':');
	}

	/*
	 * Command lines that start with a double-quote are comments.
	 *
	 * !!!
	 * Historically, there was no escape or delimiter for a comment, e.g.
	 * :"foo|set was a single comment and nothing was output.  Since nvi
	 * permits users to escape <newline> characters into command lines, we
	 * have to check for that case.
	 */
	if (ecp->clen != 0 && ch == '"') {
		while (--ecp->clen > 0 && *++ecp->cp != '\n');
		if (*ecp->cp == '\n') {
			F_SET(ecp, E_NEWLINE);
			++ecp->cp;
			--ecp->clen;
		}
		goto loop;
	}

	/* Skip whitespace. */
	for (; ecp->clen > 0; ++ecp->cp, --ecp->clen) {
		ch = *ecp->cp;
		if (!cmdskip(ch))
			break;
	}

	/*
	 * The last point at which an empty line can mean do nothing.
	 *
	 * !!!
	 * Historically, in ex mode, lines containing only <blank> characters
	 * were the same as a single <carriage-return>, i.e. a default command.
	 * In vi mode, they were ignored.  In .exrc files this was a serious
	 * annoyance, as vi kept trying to treat them as print commands.  We
	 * ignore backward compatibility in this case, discarding lines that
	 * contain only <blank> characters from .exrc files.
	 *
	 * !!!
	 * This is where you end up when you're done a command, i.e. clen has
	 * gone to zero.  Continue if there are more commands to run.
	 */
	if (ecp->clen == 0 &&
	    (!notempty || F_ISSET(sp, SC_VI) || F_ISSET(ecp, E_BLIGNORE))) {
		if (ex_load(sp))
			goto rfail;
		ecp = SLIST_FIRST(gp->ecq);
		if (ecp->clen == 0)
			goto rsuccess;
		goto loop;
	}

	/*
	 * Check to see if this is a command for which we may want to move
	 * the cursor back up to the previous line.  (The command :1<CR>
	 * wants a <newline> separator, but the command :<CR> wants to erase
	 * the command line.)  If the line is empty except for <blank>s,
	 * <carriage-return> or <eof>, we'll probably want to move up.  I
	 * don't think there's any way to get <blank> characters *after* the
	 * command character, but this is the ex parser, and I've been wrong
	 * before.
	 */
	if (F_ISSET(ecp, E_NRSEP) &&
	    ecp->clen != 0 && (ecp->clen != 1 || ecp->cp[0] != '\004'))
		F_CLR(ecp, E_NRSEP);

	/* Parse command addresses. */
	if (ex_range(sp, ecp, &tmp))
		goto rfail;
	if (tmp)
		goto err;

	/*
	 * Skip <blank>s and any more colons (the command :3,5:print
	 * worked, historically).
	 */
	for (; ecp->clen > 0; ++ecp->cp, --ecp->clen) {
		ch = *ecp->cp;
		if (!cmdskip(ch) && ch != ':')
			break;
	}

	/*
	 * If no command, ex does the last specified of p, l, or #, and vi
	 * moves to the line.  Otherwise, determine the length of the command
	 * name by looking for the first non-alphabetic character.  (There
	 * are a few non-alphabetic characters in command names, but they're
	 * all single character commands.)  This isn't a great test, because
	 * it means that, for the command ":e +cut.c file", we'll report that
	 * the command "cut" wasn't known.  However, it makes ":e+35 file" work
	 * correctly.
	 *
	 * !!!
	 * Historically, lines with multiple adjacent (or <blank> separated)
	 * command separators were very strange.  For example, the command
	 * |||<carriage-return>, when the cursor was on line 1, displayed
	 * lines 2, 3 and 5 of the file.  In addition, the command "   |  "
	 * would only display the line after the next line, instead of the
	 * next two lines.  No ideas why.  It worked reasonably when executed
	 * from vi mode, and displayed lines 2, 3, and 4, so we do a default
	 * command for each separator.
	 */
#define	SINGLE_CHAR_COMMANDS	L("\004!#&*<=>@~")
	newscreen = 0;
	if (ecp->clen != 0 && ecp->cp[0] != '|' && ecp->cp[0] != '\n') {
		if (STRCHR(SINGLE_CHAR_COMMANDS, *ecp->cp)) {
			p = ecp->cp;
			++ecp->cp;
			--ecp->clen;
			namelen = 1;
		} else {
			for (p = ecp->cp;
			    ecp->clen > 0; --ecp->clen, ++ecp->cp)
				if (!isascii(*ecp->cp) || !isalpha(*ecp->cp))
					break;
			if ((namelen = ecp->cp - p) == 0) {
				msgq(sp, M_ERR, "080|Unknown command name");
				goto err;
			}
		}

		/*
		 * !!!
		 * Historic vi permitted flags to immediately follow any
		 * subset of the 'delete' command, but then did not permit
		 * further arguments (flag, buffer, count).  Make it work.
		 * Permit further arguments for the few shreds of dignity
		 * it offers.
		 *
		 * Adding commands that start with 'd', and match "delete"
		 * up to a l, p, +, - or # character can break this code.
		 *
		 * !!!
		 * Capital letters beginning the command names ex, edit,
		 * next, previous, tag and visual (in vi mode) indicate the
		 * command should happen in a new screen.
		 */
		switch (p[0]) {
		case 'd':
			for (s = p,
			    n = cmds[C_DELETE].name; *s == *n; ++s, ++n);
			if (s[0] == 'l' || s[0] == 'p' || s[0] == '+' ||
			    s[0] == '-' || s[0] == '^' || s[0] == '#') {
				len = (ecp->cp - p) - (s - p);
				ecp->cp -= len;
				ecp->clen += len;
				ecp->rcmd = cmds[C_DELETE];
				ecp->rcmd.syntax = "1bca1";
				ecp->cmd = &ecp->rcmd;
				goto skip_srch;
			}
			break;
		case 'E': case 'F': case 'N': case 'P': case 'T': case 'V':
			newscreen = 1;
			p[0] = tolower(p[0]);
			break;
		}

		/*
		 * Search the table for the command.
		 *
		 * !!!
		 * Historic vi permitted the mark to immediately follow the
		 * 'k' in the 'k' command.  Make it work.
		 *
		 * !!!
		 * Historic vi permitted any flag to follow the s command, e.g.
		 * "s/e/E/|s|sgc3p" was legal.  Make the command "sgc" work.
		 * Since the following characters all have to be flags, i.e.
		 * alphabetics, we can let the s command routine return errors
		 * if it was some illegal command string.  This code will break
		 * if an "sg" or similar command is ever added.  The substitute
		 * code doesn't care if it's a "cgr" flag or a "#lp" flag that
		 * follows the 's', but we limit the choices here to "cgr" so
		 * that we get unknown command messages for wrong combinations.
		 */
		if ((ecp->cmd = ex_comm_search(p, namelen)) == NULL)
			switch (p[0]) {
			case 'k':
				if (namelen == 2) {
					ecp->cp -= namelen - 1;
					ecp->clen += namelen - 1;
					ecp->cmd = &cmds[C_K];
					break;
				}
				goto unknown;
			case 's':
				for (s = p + 1, cnt = namelen; --cnt; ++s)
					if (s[0] != 'c' &&
					    s[0] != 'g' && s[0] != 'r')
						break;
				if (cnt == 0) {
					ecp->cp -= namelen - 1;
					ecp->clen += namelen - 1;
					ecp->rcmd = cmds[C_SUBSTITUTE];
					ecp->rcmd.fn = ex_subagain;
					ecp->cmd = &ecp->rcmd;
					break;
				}
				/* FALLTHROUGH */
			default:
unknown:			if (newscreen)
					p[0] = toupper(p[0]);
				ex_unknown(sp, p, namelen);
				goto err;
			}

		/*
		 * The visual command has a different syntax when called
		 * from ex than when called from a vi colon command.  FMH.
		 * Make the change now, before we test for the newscreen
		 * semantic, so that we're testing the right one.
		 */
skip_srch:	if (ecp->cmd == &cmds[C_VISUAL_EX] && F_ISSET(sp, SC_VI))
			ecp->cmd = &cmds[C_VISUAL_VI];

		/*
		 * !!!
		 * Historic vi permitted a capital 'P' at the beginning of
		 * any command that started with 'p'.  Probably wanted the
		 * P[rint] command for backward compatibility, and the code
		 * just made Preserve and Put work by accident.  Nvi uses
		 * Previous to mean previous-in-a-new-screen, so be careful.
		 */
		if (newscreen && !F_ISSET(ecp->cmd, E_NEWSCREEN) &&
		    (ecp->cmd == &cmds[C_PRINT] ||
		    ecp->cmd == &cmds[C_PRESERVE]))
			newscreen = 0;

		/* Test for a newscreen associated with this command. */
		if (newscreen && !F_ISSET(ecp->cmd, E_NEWSCREEN))
			goto unknown;

		/* Secure means no shell access. */
		if (F_ISSET(ecp->cmd, E_SECURE) && O_ISSET(sp, O_SECURE)) {
			ex_wemsg(sp, ecp->cmd->name, EXM_SECURE);
			goto err;
		}

		/*
		 * Multiple < and > characters; another "feature".  Note,
		 * The string passed to the underlying function may not be
		 * nul terminated in this case.
		 */
		if ((ecp->cmd == &cmds[C_SHIFTL] && *p == '<') ||
		    (ecp->cmd == &cmds[C_SHIFTR] && *p == '>')) {
			for (ch = *p;
			    ecp->clen > 0; --ecp->clen, ++ecp->cp)
				if (*ecp->cp != ch)
					break;
			if (argv_exp0(sp, ecp, p, ecp->cp - p))
				goto err;
		}

		/* Set the format style flags for the next command. */
		if (ecp->cmd == &cmds[C_HASH])
			exp->fdef = E_C_HASH;
		else if (ecp->cmd == &cmds[C_LIST])
			exp->fdef = E_C_LIST;
		else if (ecp->cmd == &cmds[C_PRINT])
			exp->fdef = E_C_PRINT;
		F_CLR(ecp, E_USELASTCMD);
	} else {
		/* Print is the default command. */
		ecp->cmd = &cmds[C_PRINT];

		/* Set the saved format flags. */
		F_SET(ecp, exp->fdef);

		/*
		 * !!!
		 * If no address was specified, and it's not a global command,
		 * we up the address by one.  (I have no idea why globals are
		 * exempted, but it's (ahem) historic practice.)
		 */
		if (ecp->addrcnt == 0 && !F_ISSET(sp, SC_EX_GLOBAL)) {
			ecp->addrcnt = 1;
			ecp->addr1.lno = sp->lno + 1;
			ecp->addr1.cno = sp->cno;
		}

		F_SET(ecp, E_USELASTCMD);
	}

	/*
	 * !!!
	 * Historically, the number option applied to both ex and vi.  One
	 * strangeness was that ex didn't switch display formats until a
	 * command was entered, e.g. <CR>'s after the set didn't change to
	 * the new format, but :1p would.
	 */
	if (O_ISSET(sp, O_NUMBER)) {
		F_SET(ecp, E_OPTNUM);
		FL_SET(ecp->iflags, E_C_HASH);
	} else
		F_CLR(ecp, E_OPTNUM);

	/* Check for ex mode legality. */
	if (F_ISSET(sp, SC_EX) && (F_ISSET(ecp->cmd, E_VIONLY) || newscreen)) {
		msgq_wstr(sp, M_ERR, ecp->cmd->name,
		    "082|%s: command not available in ex mode");
		goto err;
	}

	/* Add standard command flags. */
	F_SET(ecp, ecp->cmd->flags);
	if (!newscreen)
		F_CLR(ecp, E_NEWSCREEN);

	/*
	 * There are three normal termination cases for an ex command.  They
	 * are the end of the string (ecp->clen), or unescaped (by <literal
	 * next> characters) <newline> or '|' characters.  As we're now past
	 * possible addresses, we can determine how long the command is, so we
	 * don't have to look for all the possible terminations.  Naturally,
	 * there are some exciting special cases:
	 *
	 * 1: The bang, global, v and the filter versions of the read and
	 *    write commands are delimited by <newline>s (they can contain
	 *    shell pipes).
	 * 2: The ex, edit, next and visual in vi mode commands all take ex
	 *    commands as their first arguments.
	 * 3: The s command takes an RE as its first argument, and wants it
	 *    to be specially delimited.
	 *
	 * Historically, '|' characters in the first argument of the ex, edit,
	 * next, vi visual, and s commands didn't delimit the command.  And,
	 * in the filter cases for read and write, and the bang, global and v
	 * commands, they did not delimit the command at all.
	 *
	 * For example, the following commands were legal:
	 *
	 *	:edit +25|s/abc/ABC/ file.c
	 *	:s/|/PIPE/
	 *	:read !spell % | columnate
	 *	:global/pattern/p|l
	 *
	 * It's not quite as simple as it sounds, however.  The command:
	 *
	 *	:s/a/b/|s/c/d|set
	 *
	 * was also legal, i.e. the historic ex parser (using the word loosely,
	 * since "parser" implies some regularity of syntax) delimited the RE's
	 * based on its delimiter and not anything so irretrievably vulgar as a
	 * command syntax.
	 *
	 * Anyhow, the following code makes this all work.  First, for the
	 * special cases we move past their special argument(s).  Then, we
	 * do normal command processing on whatever is left.  Barf-O-Rama.
	 */
	discard = 0;		/* Characters discarded from the command. */
	arg1_len = 0;
	ecp->save_cmd = ecp->cp;
	if (ecp->cmd == &cmds[C_EDIT] || ecp->cmd == &cmds[C_EX] ||
	    ecp->cmd == &cmds[C_NEXT] || ecp->cmd == &cmds[C_VISUAL_VI] ||
	    ecp->cmd == &cmds[C_VSPLIT]) {
		/*
		 * Move to the next non-whitespace character.  A '!'
		 * immediately following the command is eaten as a
		 * force flag.
		 */
		if (ecp->clen > 0 && *ecp->cp == '!') {
			++ecp->cp;
			--ecp->clen;
			FL_SET(ecp->iflags, E_C_FORCE);

			/* Reset, don't reparse. */
			ecp->save_cmd = ecp->cp;
		}
		for (; ecp->clen > 0; --ecp->clen, ++ecp->cp)
			if (!cmdskip(*ecp->cp))
				break;
		/*
		 * QUOTING NOTE:
		 *
		 * The historic implementation ignored all escape characters
		 * so there was no way to put a space or newline into the +cmd
		 * field.  We do a simplistic job of fixing it by moving to the
		 * first whitespace character that isn't escaped.  The escaping
		 * characters are stripped as no longer useful.
		 */
		if (ecp->clen > 0 && *ecp->cp == '+') {
			++ecp->cp;
			--ecp->clen;
			for (arg1 = p = ecp->cp;
			    ecp->clen > 0; --ecp->clen, ++ecp->cp) {
				ch = *ecp->cp;
				if (IS_ESCAPE(sp, ecp, ch) &&
				    ecp->clen > 1) {
					++discard;
					--ecp->clen;
					ch = *++ecp->cp;
				} else if (cmdskip(ch))
					break;
				*p++ = ch;
			}
			arg1_len = ecp->cp - arg1;

			/* Reset, so the first argument isn't reparsed. */
			ecp->save_cmd = ecp->cp;
		}
	} else if (ecp->cmd == &cmds[C_BANG] ||
	    ecp->cmd == &cmds[C_GLOBAL] || ecp->cmd == &cmds[C_V]) {
		/*
		 * QUOTING NOTE:
		 *
		 * We use backslashes to escape <newline> characters, although
		 * this wasn't historic practice for the bang command.  It was
		 * for the global and v commands, and it's common usage when
		 * doing text insert during the command.  Escaping characters
		 * are stripped as no longer useful.
		 */
		for (p = ecp->cp; ecp->clen > 0; --ecp->clen, ++ecp->cp) {
			ch = *ecp->cp;
			if (ch == '\\' && ecp->clen > 1 && ecp->cp[1] == '\n') {
				++discard;
				--ecp->clen;
				ch = *++ecp->cp;

				++gp->if_lno;
				++ecp->if_lno;
			} else if (ch == '\n')
				break;
			*p++ = ch;
		}
	} else if (ecp->cmd == &cmds[C_READ] || ecp->cmd == &cmds[C_WRITE]) {
		/*
		 * For write commands, if the next character is a <blank>, and
		 * the next non-blank character is a '!', it's a filter command
		 * and we want to eat everything up to the <newline>.  For read
		 * commands, if the next non-blank character is a '!', it's a
		 * filter command and we want to eat everything up to the next
		 * <newline>.  Otherwise, we're done.
		 */
		for (tmp = 0; ecp->clen > 0; --ecp->clen, ++ecp->cp) {
			ch = *ecp->cp;
			if (cmdskip(ch))
				tmp = 1;
			else
				break;
		}
		if (ecp->clen > 0 && ch == '!' &&
		    (ecp->cmd == &cmds[C_READ] || tmp))
			for (; ecp->clen > 0; --ecp->clen, ++ecp->cp)
				if (ecp->cp[0] == '\n')
					break;
	} else if (ecp->cmd == &cmds[C_SUBSTITUTE]) {
		/*
		 * Move to the next non-whitespace character, we'll use it as
		 * the delimiter.  If the character isn't an alphanumeric or
		 * a '|', it's the delimiter, so parse it.  Otherwise, we're
		 * into something like ":s g", so use the special s command.
		 */
		for (; ecp->clen > 0; --ecp->clen, ++ecp->cp)
			if (!cmdskip(ecp->cp[0]))
				break;

		if (!isascii(ecp->cp[0]) ||
		    isalnum(ecp->cp[0]) || ecp->cp[0] == '|') {
			ecp->rcmd = cmds[C_SUBSTITUTE];
			ecp->rcmd.fn = ex_subagain;
			ecp->cmd = &ecp->rcmd;
		} else if (ecp->clen > 0) {
			/*
			 * QUOTING NOTE:
			 *
			 * Backslashes quote delimiter characters for RE's.
			 * The backslashes are NOT removed since they'll be
			 * used by the RE code.  Move to the third delimiter
			 * that's not escaped (or the end of the command).
			 */
			delim = *ecp->cp;
			++ecp->cp;
			--ecp->clen;
			for (cnt = 2; ecp->clen > 0 &&
			    cnt != 0; --ecp->clen, ++ecp->cp)
				if (ecp->cp[0] == '\\' &&
				    ecp->clen > 1) {
					++ecp->cp;
					--ecp->clen;
				} else if (ecp->cp[0] == delim)
					--cnt;
		}
	}

	/*
	 * Use normal quoting and termination rules to find the end of this
	 * command.
	 *
	 * QUOTING NOTE:
	 *
	 * Historically, vi permitted ^V's to escape <newline>'s in the .exrc
	 * file.  It was almost certainly a bug, but that's what bug-for-bug
	 * compatibility means, Grasshopper.  Also, ^V's escape the command
	 * delimiters.  Literal next quote characters in front of the newlines,
	 * '|' characters or literal next characters are stripped as they're
	 * no longer useful.
	 */
	vi_address = ecp->clen != 0 && ecp->cp[0] != '\n';
	for (p = ecp->cp; ecp->clen > 0; --ecp->clen, ++ecp->cp) {
		ch = ecp->cp[0];
		if (IS_ESCAPE(sp, ecp, ch) && ecp->clen > 1) {
			CHAR_T tmp = ecp->cp[1];
			if (tmp == '\n' || tmp == '|') {
				if (tmp == '\n') {
					++gp->if_lno;
					++ecp->if_lno;
				}
				++discard;
				--ecp->clen;
				++ecp->cp;
				ch = tmp;
			}
		} else if (ch == '\n' || ch == '|') {
			if (ch == '\n')
				F_SET(ecp, E_NEWLINE);
			--ecp->clen;
			break;
		}
		*p++ = ch;
	}

	/*
	 * Save off the next command information, go back to the
	 * original start of the command.
	 */
	p = ecp->cp + 1;
	ecp->cp = ecp->save_cmd;
	ecp->save_cmd = p;
	ecp->save_cmdlen = ecp->clen;
	ecp->clen = ((ecp->save_cmd - ecp->cp) - 1) - discard;

	/*
	 * QUOTING NOTE:
	 *
	 * The "set tags" command historically used a backslash, not the
	 * user's literal next character, to escape whitespace.  Handle
	 * it here instead of complicating the argv_exp3() code.  Note,
	 * this isn't a particularly complex trap, and if backslashes were
	 * legal in set commands, this would have to be much more complicated.
	 */
	if (ecp->cmd == &cmds[C_SET])
		for (p = ecp->cp, len = ecp->clen; len > 0; --len, ++p)
			if (IS_ESCAPE(sp, ecp, *p) && len > 1) {
				--len;
				++p;
			} else if (*p == '\\')
				*p = CH_LITERAL;

	/*
	 * Set the default addresses.  It's an error to specify an address for
	 * a command that doesn't take them.  If two addresses are specified
	 * for a command that only takes one, lose the first one.  Two special
	 * cases here, some commands take 0 or 2 addresses.  For most of them
	 * (the E_ADDR2_ALL flag), 0 defaults to the entire file.  For one
	 * (the `!' command, the E_ADDR2_NONE flag), 0 defaults to no lines.
	 *
	 * Also, if the file is empty, some commands want to use an address of
	 * 0, i.e. the entire file is 0 to 0, and the default first address is
	 * 0.  Otherwise, an entire file is 1 to N and the default line is 1.
	 * Note, we also add the E_ADDR_ZERO flag to the command flags, for the
	 * case where the 0 address is only valid if it's a default address.
	 *
	 * Also, set a flag if we set the default addresses.  Some commands
	 * (ex: z) care if the user specified an address or if we just used
	 * the current cursor.
	 */
	switch (F_ISSET(ecp, E_ADDR1 | E_ADDR2 | E_ADDR2_ALL | E_ADDR2_NONE)) {
	case E_ADDR1:				/* One address: */
		switch (ecp->addrcnt) {
		case 0:				/* Default cursor/empty file. */
			ecp->addrcnt = 1;
			F_SET(ecp, E_ADDR_DEF);
			if (F_ISSET(ecp, E_ADDR_ZERODEF)) {
				if (db_last(sp, &lno))
					goto err;
				if (lno == 0) {
					ecp->addr1.lno = 0;
					F_SET(ecp, E_ADDR_ZERO);
				} else
					ecp->addr1.lno = sp->lno;
			} else
				ecp->addr1.lno = sp->lno;
			ecp->addr1.cno = sp->cno;
			break;
		case 1:
			break;
		case 2:				/* Lose the first address. */
			ecp->addrcnt = 1;
			ecp->addr1 = ecp->addr2;
		}
		break;
	case E_ADDR2_NONE:			/* Zero/two addresses: */
		if (ecp->addrcnt == 0)		/* Default to nothing. */
			break;
		goto two_addr;
	case E_ADDR2_ALL:			/* Zero/two addresses: */
		if (ecp->addrcnt == 0) {	/* Default entire/empty file. */
			F_SET(ecp, E_ADDR_DEF);
			ecp->addrcnt = 2;
			if (sp->ep == NULL)
				ecp->addr2.lno = 0;
			else if (db_last(sp, &ecp->addr2.lno))
				goto err;
			if (F_ISSET(ecp, E_ADDR_ZERODEF) &&
			    ecp->addr2.lno == 0) {
				ecp->addr1.lno = 0;
				F_SET(ecp, E_ADDR_ZERO);
			} else
				ecp->addr1.lno = 1;
			ecp->addr1.cno = ecp->addr2.cno = 0;
			F_SET(ecp, E_ADDR2_ALL);
			break;
		}
		/* FALLTHROUGH */
	case E_ADDR2:				/* Two addresses: */
two_addr:	switch (ecp->addrcnt) {
		case 0:				/* Default cursor/empty file. */
			ecp->addrcnt = 2;
			F_SET(ecp, E_ADDR_DEF);
			if (sp->lno == 1 &&
			    F_ISSET(ecp, E_ADDR_ZERODEF)) {
				if (db_last(sp, &lno))
					goto err;
				if (lno == 0) {
					ecp->addr1.lno = ecp->addr2.lno = 0;
					F_SET(ecp, E_ADDR_ZERO);
				} else
					ecp->addr1.lno =
					    ecp->addr2.lno = sp->lno;
			} else
				ecp->addr1.lno = ecp->addr2.lno = sp->lno;
			ecp->addr1.cno = ecp->addr2.cno = sp->cno;
			break;
		case 1:				/* Default to first address. */
			ecp->addrcnt = 2;
			ecp->addr2 = ecp->addr1;
			break;
		case 2:
			break;
		}
		break;
	default:
		if (ecp->addrcnt)		/* Error. */
			goto usage;
	}

	/*
	 * !!!
	 * The ^D scroll command historically scrolled the value of the scroll
	 * option or to EOF.  It was an error if the cursor was already at EOF.
	 * (Leading addresses were permitted, but were then ignored.)
	 */
	if (ecp->cmd == &cmds[C_SCROLL]) {
		ecp->addrcnt = 2;
		ecp->addr1.lno = sp->lno + 1;
		ecp->addr2.lno = sp->lno + O_VAL(sp, O_SCROLL);
		ecp->addr1.cno = ecp->addr2.cno = sp->cno;
		if (db_last(sp, &lno))
			goto err;
		if (lno != 0 && lno > sp->lno && ecp->addr2.lno > lno)
			ecp->addr2.lno = lno;
	}

	ecp->flagoff = 0;
	for (np = ecp->cmd->syntax; *np != '\0'; ++np) {
		/*
		 * The force flag is sensitive to leading whitespace, i.e.
		 * "next !" is different from "next!".  Handle it before
		 * skipping leading <blank>s.
		 */
		if (*np == '!') {
			if (ecp->clen > 0 && *ecp->cp == '!') {
				++ecp->cp;
				--ecp->clen;
				FL_SET(ecp->iflags, E_C_FORCE);
			}
			continue;
		}

		/* Skip leading <blank>s. */
		for (; ecp->clen > 0; --ecp->clen, ++ecp->cp)
			if (!cmdskip(*ecp->cp))
				break;
		if (ecp->clen == 0)
			break;

		switch (*np) {
		case '1':				/* +, -, #, l, p */
			/*
			 * !!!
			 * Historically, some flags were ignored depending
			 * on where they occurred in the command line.  For
			 * example, in the command, ":3+++p--#", historic vi
			 * acted on the '#' flag, but ignored the '-' flags.
			 * It's unambiguous what the flags mean, so we just
			 * handle them regardless of the stupidity of their
			 * location.
			 */
			for (; ecp->clen; --ecp->clen, ++ecp->cp)
				switch (*ecp->cp) {
				case '+':
					++ecp->flagoff;
					break;
				case '-':
				case '^':
					--ecp->flagoff;
					break;
				case '#':
					F_CLR(ecp, E_OPTNUM);
					FL_SET(ecp->iflags, E_C_HASH);
					exp->fdef |= E_C_HASH;
					break;
				case 'l':
					FL_SET(ecp->iflags, E_C_LIST);
					exp->fdef |= E_C_LIST;
					break;
				case 'p':
					FL_SET(ecp->iflags, E_C_PRINT);
					exp->fdef |= E_C_PRINT;
					break;
				default:
					goto end_case1;
				}
end_case1:		break;
		case '2':				/* -, ., +, ^ */
		case '3':				/* -, ., +, ^, = */
			for (; ecp->clen; --ecp->clen, ++ecp->cp)
				switch (*ecp->cp) {
				case '-':
					FL_SET(ecp->iflags, E_C_DASH);
					break;
				case '.':
					FL_SET(ecp->iflags, E_C_DOT);
					break;
				case '+':
					FL_SET(ecp->iflags, E_C_PLUS);
					break;
				case '^':
					FL_SET(ecp->iflags, E_C_CARAT);
					break;
				case '=':
					if (*np == '3') {
						FL_SET(ecp->iflags, E_C_EQUAL);
						break;
					}
					/* FALLTHROUGH */
				default:
					goto end_case23;
				}
end_case23:		break;
		case 'b':				/* buffer */
			/*
			 * !!!
			 * Historically, "d #" was a delete with a flag, not a
			 * delete into the '#' buffer.  If the current command
			 * permits a flag, don't use one as a buffer.  However,
			 * the 'l' and 'p' flags were legal buffer names in the
			 * historic ex, and were used as buffers, not flags.
			 */
			if ((ecp->cp[0] == '+' || ecp->cp[0] == '-' ||
			    ecp->cp[0] == '^' || ecp->cp[0] == '#') &&
			    strchr(np, '1') != NULL)
				break;
			/*
			 * !!!
			 * Digits can't be buffer names in ex commands, or the
			 * command "d2" would be a delete into buffer '2', and
			 * not a two-line deletion.
			 */
			if (!ISDIGIT(ecp->cp[0])) {
				ecp->buffer = *ecp->cp;
				++ecp->cp;
				--ecp->clen;
				FL_SET(ecp->iflags, E_C_BUFFER);
			}
			break;
		case 'c':				/* count [01+a] */
			++np;
			/* Validate any signed value. */
			if (!ISDIGIT(*ecp->cp) && (*np != '+' ||
			    (*ecp->cp != '+' && *ecp->cp != '-')))
				break;
			/* If a signed value, set appropriate flags. */
			if (*ecp->cp == '-')
				FL_SET(ecp->iflags, E_C_COUNT_NEG);
			else if (*ecp->cp == '+')
				FL_SET(ecp->iflags, E_C_COUNT_POS);
			if ((nret =
			    nget_slong(&ltmp, ecp->cp, &t, 10)) != NUM_OK) {
				ex_badaddr(sp, NULL, A_NOTSET, nret);
				goto err;
			}
			if (ltmp == 0 && *np != '0') {
				msgq(sp, M_ERR, "083|Count may not be zero");
				goto err;
			}
			ecp->clen -= (t - ecp->cp);
			ecp->cp = t;

			/*
			 * Counts as address offsets occur in commands taking
			 * two addresses.  Historic vi practice was to use
			 * the count as an offset from the *second* address.
			 *
			 * Set a count flag; some underlying commands (see
			 * join) do different things with counts than with
			 * line addresses.
			 */
			if (*np == 'a') {
				ecp->addr1 = ecp->addr2;
				ecp->addr2.lno = ecp->addr1.lno + ltmp - 1;
			} else
				ecp->count = ltmp;
			FL_SET(ecp->iflags, E_C_COUNT);
			break;
		case 'f':				/* file */
			if (argv_exp2(sp, ecp, ecp->cp, ecp->clen))
				goto err;
			goto arg_cnt_chk;
		case 'l':				/* line */
			/*
			 * Get a line specification.
			 *
			 * If the line was a search expression, we may have
			 * changed state during the call, and we're now
			 * searching the file.  Push ourselves onto the state
			 * stack.
			 */
			if (ex_line(sp, ecp, &cur, &isaddr, &tmp))
				goto rfail;
			if (tmp)
				goto err;

			/* Line specifications are always required. */
			if (!isaddr) {
				msgq_wstr(sp, M_ERR, ecp->cp,
				     "084|%s: bad line specification");
				goto err;
			}
			/*
			 * The target line should exist for these commands,
			 * but 0 is legal for them as well.
			 */
			if (cur.lno != 0 && !db_exist(sp, cur.lno)) {
				ex_badaddr(sp, NULL, A_EOF, NUM_OK);
				goto err;
			}
			ecp->lineno = cur.lno;
			break;
		case 'S':				/* string, file exp. */
			if (ecp->clen != 0) {
				if (argv_exp1(sp, ecp, ecp->cp,
				    ecp->clen, ecp->cmd == &cmds[C_BANG]))
					goto err;
				goto addr_verify;
			}
			/* FALLTHROUGH */
		case 's':				/* string */
			if (argv_exp0(sp, ecp, ecp->cp, ecp->clen))
				goto err;
			goto addr_verify;
		case 'W':				/* word string */
			/*
			 * QUOTING NOTE:
			 *
			 * Literal next characters escape the following
			 * character.  Quoting characters are stripped here
			 * since they are no longer useful.
			 *
			 * First there was the word.
			 */
			for (p = t = ecp->cp;
			    ecp->clen > 0; --ecp->clen, ++ecp->cp) {
				ch = *ecp->cp;
				if (IS_ESCAPE(sp,
				    ecp, ch) && ecp->clen > 1) {
					--ecp->clen;
					*p++ = *++ecp->cp;
				} else if (cmdskip(ch)) {
					++ecp->cp;
					--ecp->clen;
					break;
				} else
					*p++ = ch;
			}
			if (argv_exp0(sp, ecp, t, p - t))
				goto err;

			/* Delete intervening whitespace. */
			for (; ecp->clen > 0;
			    --ecp->clen, ++ecp->cp) {
				ch = *ecp->cp;
				if (!cmdskip(ch))
					break;
			}
			if (ecp->clen == 0)
				goto usage;

			/* Followed by the string. */
			for (p = t = ecp->cp; ecp->clen > 0;
			    --ecp->clen, ++ecp->cp, ++p) {
				ch = *ecp->cp;
				if (IS_ESCAPE(sp,
				    ecp, ch) && ecp->clen > 1) {
					--ecp->clen;
					*p = *++ecp->cp;
				} else
					*p = ch;
			}
			if (argv_exp0(sp, ecp, t, p - t))
				goto err;
			goto addr_verify;
		case 'w':				/* word */
			if (argv_exp3(sp, ecp, ecp->cp, ecp->clen))
				goto err;
arg_cnt_chk:		if (*++np != 'N') {		/* N */
				/*
				 * If a number is specified, must either be
				 * 0 or that number, if optional, and that
				 * number, if required.
				 */
				tmp = *np - '0';
				if ((*++np != 'o' || exp->argsoff != 0) &&
				    exp->argsoff != tmp)
					goto usage;
			}
			goto addr_verify;
		default: {
			size_t nlen;
			char *nstr;

			INT2CHAR(sp, ecp->cmd->name, STRLEN(ecp->cmd->name) + 1,
			    nstr, nlen);
			msgq(sp, M_ERR,
			    "085|Internal syntax table error (%s: %s)",
			    nstr, KEY_NAME(sp, *np));
		}
		}
	}

	/* Skip trailing whitespace. */
	for (; ecp->clen > 0; --ecp->clen) {
		ch = *ecp->cp++;
		if (!cmdskip(ch))
			break;
	}

	/*
	 * There shouldn't be anything left, and no more required fields,
	 * i.e neither 'l' or 'r' in the syntax string.
	 */
	if (ecp->clen != 0 || strpbrk(np, "lr")) {
usage:		msgq(sp, M_ERR, "086|Usage: %s", ecp->cmd->usage);
		goto err;
	}

	/*
	 * Verify that the addresses are legal.  Check the addresses here,
	 * because this is a place where all ex addresses pass through.
	 * (They don't all pass through ex_line(), for instance.)  We're
	 * assuming that any non-existent line doesn't exist because it's
	 * past the end-of-file.  That's a pretty good guess.
	 *
	 * If it's a "default vi command", an address of zero is okay.
	 */
addr_verify:
	switch (ecp->addrcnt) {
	case 2:
		/*
		 * Historic ex/vi permitted commands with counts to go past
		 * EOF.  So, for example, if the file only had 5 lines, the
		 * ex command "1,6>" would fail, but the command ">300"
		 * would succeed.  Since we don't want to have to make all
		 * of the underlying commands handle random line numbers,
		 * fix it here.
		 */
		if (ecp->addr2.lno == 0) {
			if (!F_ISSET(ecp, E_ADDR_ZERO) &&
			    (F_ISSET(sp, SC_EX) ||
			    !F_ISSET(ecp, E_USELASTCMD))) {
				ex_badaddr(sp, ecp->cmd, A_ZERO, NUM_OK);
				goto err;
			}
		} else if (!db_exist(sp, ecp->addr2.lno))
			if (FL_ISSET(ecp->iflags, E_C_COUNT)) {
				if (db_last(sp, &lno))
					goto err;
				ecp->addr2.lno = lno;
			} else {
				ex_badaddr(sp, NULL, A_EOF, NUM_OK);
				goto err;
			}
		/* FALLTHROUGH */
	case 1:
		if (ecp->addr1.lno == 0) {
			if (!F_ISSET(ecp, E_ADDR_ZERO) &&
			    (F_ISSET(sp, SC_EX) ||
			    !F_ISSET(ecp, E_USELASTCMD))) {
				ex_badaddr(sp, ecp->cmd, A_ZERO, NUM_OK);
				goto err;
			}
		} else if (!db_exist(sp, ecp->addr1.lno)) {
			ex_badaddr(sp, NULL, A_EOF, NUM_OK);
			goto err;
		}
		break;
	}

	/*
	 * If doing a default command and there's nothing left on the line,
	 * vi just moves to the line.  For example, ":3" and ":'a,'b" just
	 * move to line 3 and line 'b, respectively, but ":3|" prints line 3.
	 *
	 * !!!
	 * In addition, IF THE LINE CHANGES, move to the first nonblank of
	 * the line.
	 *
	 * !!!
	 * This is done before the absolute mark gets set; historically,
	 * "/a/,/b/" did NOT set vi's absolute mark, but "/a/,/b/d" did.
	 */
	if ((F_ISSET(sp, SC_VI) || F_ISSET(ecp, E_NOPRDEF)) &&
	    F_ISSET(ecp, E_USELASTCMD) && vi_address == 0) {
		switch (ecp->addrcnt) {
		case 2:
			if (sp->lno !=
			    (ecp->addr2.lno ? ecp->addr2.lno : 1)) {
				sp->lno =
				    ecp->addr2.lno ? ecp->addr2.lno : 1;
				sp->cno = 0;
				(void)nonblank(sp, sp->lno, &sp->cno);
			}
			break;
		case 1:
			if (sp->lno !=
			    (ecp->addr1.lno ? ecp->addr1.lno : 1)) {
				sp->lno =
				    ecp->addr1.lno ? ecp->addr1.lno : 1;
				sp->cno = 0;
				(void)nonblank(sp, sp->lno, &sp->cno);
			}
			break;
		}
		ecp->cp = ecp->save_cmd;
		ecp->clen = ecp->save_cmdlen;
		goto loop;
	}

	/*
	 * Set the absolute mark -- we have to set it for vi here, in case
	 * it's a compound command, e.g. ":5p|6" should set the absolute
	 * mark for vi.
	 */
	if (F_ISSET(ecp, E_ABSMARK)) {
		cur.lno = sp->lno;
		cur.cno = sp->cno;
		F_CLR(ecp, E_ABSMARK);
		if (mark_set(sp, ABSMARK1, &cur, 1))
			goto err;
	}

#if defined(DEBUG) && defined(COMLOG)
	ex_comlog(sp, ecp);
#endif
	/* Increment the command count if not called from vi. */
	if (F_ISSET(sp, SC_EX))
		++sp->ccnt;

	/*
	 * If file state available, and not doing a global command,
	 * log the start of an action.
	 */
	if (sp->ep != NULL && !F_ISSET(sp, SC_EX_GLOBAL))
		(void)log_cursor(sp);

	/*
	 * !!!
	 * There are two special commands for the purposes of this code: the
	 * default command (<carriage-return>) or the scrolling commands (^D
	 * and <EOF>) as the first non-<blank> characters  in the line.
	 *
	 * If this is the first command in the command line, we received the
	 * command from the ex command loop and we're talking to a tty, and
	 * and there's nothing else on the command line, and it's one of the
	 * special commands, we move back up to the previous line, and erase
	 * the prompt character with the output.  Since ex runs in canonical
	 * mode, we don't have to do anything else, a <newline> has already
	 * been echoed by the tty driver.  It's OK if vi calls us -- we won't
	 * be in ex mode so we'll do nothing.
	 */
	if (F_ISSET(ecp, E_NRSEP)) {
		if (sp->ep != NULL &&
		    F_ISSET(sp, SC_EX) && !F_ISSET(gp, G_SCRIPTED) &&
		    (F_ISSET(ecp, E_USELASTCMD) || ecp->cmd == &cmds[C_SCROLL]))
			gp->scr_ex_adjust(sp, EX_TERM_SCROLL);
		F_CLR(ecp, E_NRSEP);
	}

	/*
	 * Call the underlying function for the ex command.
	 *
	 * XXX
	 * Interrupts behave like errors, for now.
	 */
	if (ecp->cmd->fn(sp, ecp) || INTERRUPTED(sp)) {
		if (F_ISSET(gp, G_SCRIPTED))
			F_SET(sp, SC_EXIT_FORCE);
		goto err;
	}

#ifdef DEBUG
	/* Make sure no function left global temporary space locked. */
	if (F_ISSET(gp, G_TMP_INUSE)) {
		F_CLR(gp, G_TMP_INUSE);
		msgq_wstr(sp, M_ERR, ecp->cmd->name,
		    "087|%s: temporary buffer not released");
	}
#endif
	/*
	 * Ex displayed the number of lines modified immediately after each
	 * command, so the command "1,10d|1,10d" would display:
	 *
	 *	10 lines deleted
	 *	10 lines deleted
	 *	<autoprint line>
	 *
	 * Executing ex commands from vi only reported the final modified
	 * lines message -- that's wrong enough that we don't match it.
	 */
	if (F_ISSET(sp, SC_EX))
		mod_rpt(sp);

	/*
	 * Integrate any offset parsed by the underlying command, and make
	 * sure the referenced line exists.
	 *
	 * XXX
	 * May not match historic practice (which I've never been able to
	 * completely figure out.)  For example, the '=' command from vi
	 * mode often got the offset wrong, and complained it was too large,
	 * but didn't seem to have a problem with the cursor.  If anyone
	 * complains, ask them how it's supposed to work, they might know.
	 */
	if (sp->ep != NULL && ecp->flagoff) {
		if (ecp->flagoff < 0) {
			if (sp->lno <= -ecp->flagoff) {
				msgq(sp, M_ERR,
				    "088|Flag offset to before line 1");
				goto err;
			}
		} else {
			if (!NPFITS(MAX_REC_NUMBER, sp->lno, ecp->flagoff)) {
				ex_badaddr(sp, NULL, A_NOTSET, NUM_OVER);
				goto err;
			}
			if (!db_exist(sp, sp->lno + ecp->flagoff)) {
				msgq(sp, M_ERR,
				    "089|Flag offset past end-of-file");
				goto err;
			}
		}
		sp->lno += ecp->flagoff;
	}

	/*
	 * If the command executed successfully, we may want to display a line
	 * based on the autoprint option or an explicit print flag.  (Make sure
	 * that there's a line to display.)  Also, the autoprint edit option is
	 * turned off for the duration of global commands.
	 */
	if (F_ISSET(sp, SC_EX) && sp->ep != NULL && sp->lno != 0) {
		/*
		 * The print commands have already handled the `print' flags.
		 * If so, clear them.
		 */
		if (FL_ISSET(ecp->iflags, E_CLRFLAG))
			FL_CLR(ecp->iflags, E_C_HASH | E_C_LIST | E_C_PRINT);

		/* If hash set only because of the number option, discard it. */
		if (F_ISSET(ecp, E_OPTNUM))
			FL_CLR(ecp->iflags, E_C_HASH);

		/*
		 * If there was an explicit flag to display the new cursor line,
		 * or autoprint is set and a change was made, display the line.
		 * If any print flags were set use them, else default to print.
		 */
		LF_INIT(FL_ISSET(ecp->iflags, E_C_HASH | E_C_LIST | E_C_PRINT));
		if (!LF_ISSET(E_C_HASH | E_C_LIST | E_C_PRINT | E_NOAUTO) &&
		    !F_ISSET(sp, SC_EX_GLOBAL) &&
		    O_ISSET(sp, O_AUTOPRINT) && F_ISSET(ecp, E_AUTOPRINT))
			LF_INIT(E_C_PRINT);

		if (LF_ISSET(E_C_HASH | E_C_LIST | E_C_PRINT)) {
			cur.lno = sp->lno;
			cur.cno = 0;
			(void)ex_print(sp, ecp, &cur, &cur, flags);
		}
	}

	/*
	 * If the command had an associated "+cmd", it has to be executed
	 * before we finish executing any more of this ex command.  For
	 * example, consider a .exrc file that contains the following lines:
	 *
	 *	:set all
	 *	:edit +25 file.c|s/abc/ABC/|1
	 *	:3,5 print
	 *
	 * This can happen more than once -- the historic vi simply hung or
	 * dropped core, of course.  Prepend the + command back into the
	 * current command and continue.  We may have to add an additional
	 * <literal next> character.  We know that it will fit because we
	 * discarded at least one space and the + character.
	 */
	if (arg1_len != 0) {
		/*
		 * If the last character of the + command was a <literal next>
		 * character, it would be treated differently because of the
		 * append.  Quote it, if necessary.
		 */
		if (IS_ESCAPE(sp, ecp, arg1[arg1_len - 1])) {
			*--ecp->save_cmd = CH_LITERAL;
			++ecp->save_cmdlen;
		}

		ecp->save_cmd -= arg1_len;
		ecp->save_cmdlen += arg1_len;
		MEMCPY(ecp->save_cmd, arg1, arg1_len);

		/*
		 * Any commands executed from a +cmd are executed starting at
		 * the first column of the last line of the file -- NOT the
		 * first nonblank.)  The main file startup code doesn't know
		 * that a +cmd was set, however, so it may have put us at the
		 * top of the file.  (Note, this is safe because we must have
		 * switched files to get here.)
		 */
		F_SET(ecp, E_MOVETOEND);
	}

	/* Update the current command. */
	ecp->cp = ecp->save_cmd;
	ecp->clen = ecp->save_cmdlen;

	/*
	 * !!!
	 * If we've changed screens or underlying files, any pending global or
	 * v command, or @ buffer that has associated addresses, has to be
	 * discarded.  This is historic practice for globals, and necessary for
	 * @ buffers that had associated addresses.
	 *
	 * Otherwise, if we've changed underlying files, it's not a problem,
	 * we continue with the rest of the ex command(s), operating on the
	 * new file.  However, if we switch screens (either by exiting or by
	 * an explicit command), we have no way of knowing where to put output
	 * messages, and, since we don't control screens here, we could screw
	 * up the upper layers, (e.g. we could exit/reenter a screen multiple
	 * times).  So, return and continue after we've got a new screen.
	 */
	if (F_ISSET(sp, SC_EXIT | SC_EXIT_FORCE | SC_FSWITCH | SC_SSWITCH)) {
		at_found = gv_found = 0;
		SLIST_FOREACH(ecp, sp->gp->ecq, q)
			switch (ecp->agv_flags) {
			case 0:
			case AGV_AT_NORANGE:
				break;
			case AGV_AT:
				if (!at_found) {
					at_found = 1;
					msgq(sp, M_ERR,
		"090|@ with range running when the file/screen changed");
				}
				break;
			case AGV_GLOBAL:
			case AGV_V:
				if (!gv_found) {
					gv_found = 1;
					msgq(sp, M_ERR,
		"091|Global/v command running when the file/screen changed");
				}
				break;
			default:
				abort();
			}
		if (at_found || gv_found)
			goto discard;
		if (F_ISSET(sp, SC_EXIT | SC_EXIT_FORCE | SC_SSWITCH))
			goto rsuccess;
	}

	goto loop;
	/* NOTREACHED */

err:	/*
	 * On command failure, we discard keys and pending commands remaining,
	 * as well as any keys that were mapped and waiting.  The save_cmdlen
	 * test is not necessarily correct.  If we fail early enough we don't
	 * know if the entire string was a single command or not.  Guess, as
	 * it's useful to know if commands other than the current one are being
	 * discarded.
	 */
	if (ecp->save_cmdlen == 0)
		for (; ecp->clen; --ecp->clen) {
			ch = *ecp->cp++;
			if (IS_ESCAPE(sp, ecp, ch) && ecp->clen > 1) {
				--ecp->clen;
				++ecp->cp;
			} else if (ch == '\n' || ch == '|') {
				if (ecp->clen > 1)
					ecp->save_cmdlen = 1;
				break;
			}
		}
	if (ecp->save_cmdlen != 0 || SLIST_FIRST(gp->ecq) != &gp->excmd) {
discard:	msgq(sp, M_BERR,
		    "092|Ex command failed: pending commands discarded");
		ex_discard(sp);
	}
	if (v_event_flush(sp, CH_MAPPED))
		msgq(sp, M_BERR,
		    "093|Ex command failed: mapped keys discarded");

rfail:	tmp = 1;
	if (0)
rsuccess:	tmp = 0;

	/* Turn off any file name error information. */
	gp->if_name = NULL;

	/* Turn off the global bit. */
	F_CLR(sp, SC_EX_GLOBAL);

	return (tmp);
}

/*
 * ex_range --
 *	Get a line range for ex commands, or perform a vi ex address search.
 *
 * PUBLIC: int ex_range(SCR *, EXCMD *, int *);
 */
int
ex_range(SCR *sp, EXCMD *ecp, int *errp)
{
	enum { ADDR_FOUND, ADDR_NEED, ADDR_NONE } addr;
	GS *gp;
	EX_PRIVATE *exp;
	MARK m;
	int isaddr;

	*errp = 0;

	/*
	 * Parse comma or semi-colon delimited line specs.
	 *
	 * Semi-colon delimiters update the current address to be the last
	 * address.  For example, the command
	 *
	 *	:3;/pattern/ecp->cp
	 *
	 * will search for pattern from line 3.  In addition, if ecp->cp
	 * is not a valid command, the current line will be left at 3, not
	 * at the original address.
	 *
	 * Extra addresses are discarded, starting with the first.
	 *
	 * !!!
	 * If any addresses are missing, they default to the current line.
	 * This was historically true for both leading and trailing comma
	 * delimited addresses as well as for trailing semicolon delimited
	 * addresses.  For consistency, we make it true for leading semicolon
	 * addresses as well.
	 */
	gp = sp->gp;
	exp = EXP(sp);
	for (addr = ADDR_NONE, ecp->addrcnt = 0; ecp->clen > 0;)
		switch (*ecp->cp) {
		case '%':		/* Entire file. */
			/* Vi ex address searches didn't permit % signs. */
			if (F_ISSET(ecp, E_VISEARCH))
				goto ret;

			/* It's an error if the file is empty. */
			if (sp->ep == NULL) {
				ex_badaddr(sp, NULL, A_EMPTY, NUM_OK);
				*errp = 1;
				return (0);
			}
			/*
			 * !!!
			 * A percent character addresses all of the lines in
			 * the file.  Historically, it couldn't be followed by
			 * any other address.  We do it as a text substitution
			 * for simplicity.  POSIX 1003.2 is expected to follow
			 * this practice.
			 *
			 * If it's an empty file, the first line is 0, not 1.
			 */
			if (addr == ADDR_FOUND) {
				ex_badaddr(sp, NULL, A_COMBO, NUM_OK);
				*errp = 1;
				return (0);
			}
			if (db_last(sp, &ecp->addr2.lno))
				return (1);
			ecp->addr1.lno = ecp->addr2.lno == 0 ? 0 : 1;
			ecp->addr1.cno = ecp->addr2.cno = 0;
			ecp->addrcnt = 2;
			addr = ADDR_FOUND;
			++ecp->cp;
			--ecp->clen;
			break;
		case ',':	       /* Comma delimiter. */
			/* Vi ex address searches didn't permit commas. */
			if (F_ISSET(ecp, E_VISEARCH))
				goto ret;
			/* FALLTHROUGH */
		case ';':	       /* Semi-colon delimiter. */
			if (sp->ep == NULL) {
				ex_badaddr(sp, NULL, A_EMPTY, NUM_OK);
				*errp = 1;
				return (0);
			}
			if (addr != ADDR_FOUND)
				switch (ecp->addrcnt) {
				case 0:
					ecp->addr1.lno = sp->lno;
					ecp->addr1.cno = sp->cno;
					ecp->addrcnt = 1;
					break;
				case 2:
					ecp->addr1 = ecp->addr2;
					/* FALLTHROUGH */
				case 1:
					ecp->addr2.lno = sp->lno;
					ecp->addr2.cno = sp->cno;
					ecp->addrcnt = 2;
					break;
				}
			if (*ecp->cp == ';')
				switch (ecp->addrcnt) {
				case 0:
					abort();
					/* NOTREACHED */
				case 1:
					sp->lno = ecp->addr1.lno;
					sp->cno = ecp->addr1.cno;
					break;
				case 2:
					sp->lno = ecp->addr2.lno;
					sp->cno = ecp->addr2.cno;
					break;
				}
			addr = ADDR_NEED;
			/* FALLTHROUGH */
		case ' ':		/* Whitespace. */
		case '\t':		/* Whitespace. */
			++ecp->cp;
			--ecp->clen;
			break;
		default:
			/* Get a line specification. */
			if (ex_line(sp, ecp, &m, &isaddr, errp))
				return (1);
			if (*errp)
				return (0);
			if (!isaddr)
				goto ret;
			if (addr == ADDR_FOUND) {
				ex_badaddr(sp, NULL, A_COMBO, NUM_OK);
				*errp = 1;
				return (0);
			}
			switch (ecp->addrcnt) {
			case 0:
				ecp->addr1 = m;
				ecp->addrcnt = 1;
				break;
			case 1:
				ecp->addr2 = m;
				ecp->addrcnt = 2;
				break;
			case 2:
				ecp->addr1 = ecp->addr2;
				ecp->addr2 = m;
				break;
			}
			addr = ADDR_FOUND;
			break;
		}

	/*
	 * !!!
	 * Vi ex address searches are indifferent to order or trailing
	 * semi-colons.
	 */
ret:	if (F_ISSET(ecp, E_VISEARCH))
		return (0);

	if (addr == ADDR_NEED)
		switch (ecp->addrcnt) {
		case 0:
			ecp->addr1.lno = sp->lno;
			ecp->addr1.cno = sp->cno;
			ecp->addrcnt = 1;
			break;
		case 2:
			ecp->addr1 = ecp->addr2;
			/* FALLTHROUGH */
		case 1:
			ecp->addr2.lno = sp->lno;
			ecp->addr2.cno = sp->cno;
			ecp->addrcnt = 2;
			break;
		}

	if (ecp->addrcnt == 2 && ecp->addr2.lno < ecp->addr1.lno) {
		msgq(sp, M_ERR,
		    "094|The second address is smaller than the first");
		*errp = 1;
	}
	return (0);
}

/*
 * ex_line --
 *	Get a single line address specifier.
 *
 * The way the "previous context" mark worked was that any "non-relative"
 * motion set it.  While ex/vi wasn't totally consistent about this, ANY
 * numeric address, search pattern, '$', or mark reference in an address
 * was considered non-relative, and set the value.  Which should explain
 * why we're hacking marks down here.  The problem was that the mark was
 * only set if the command was called, i.e. we have to set a flag and test
 * it later.
 *
 * XXX
 * This is probably still not exactly historic practice, although I think
 * it's fairly close.
 */
static int
ex_line(SCR *sp, EXCMD *ecp, MARK *mp, int *isaddrp, int *errp)
{
	enum nresult nret;
	EX_PRIVATE *exp;
	GS *gp;
	long total, val;
	int isneg;
	int (*sf)(SCR *, MARK *, MARK *, CHAR_T *, size_t, CHAR_T **, u_int);
	CHAR_T *endp;

	gp = sp->gp;
	exp = EXP(sp);

	*isaddrp = *errp = 0;
	F_CLR(ecp, E_DELTA);

	/* No addresses permitted until a file has been read in. */
	if (sp->ep == NULL && STRCHR(L("$0123456789'\\/?.+-^"), *ecp->cp)) {
		ex_badaddr(sp, NULL, A_EMPTY, NUM_OK);
		*errp = 1;
		return (0);
	}

	switch (*ecp->cp) {
	case '$':				/* Last line in the file. */
		*isaddrp = 1;
		F_SET(ecp, E_ABSMARK);

		mp->cno = 0;
		if (db_last(sp, &mp->lno))
			return (1);
		++ecp->cp;
		--ecp->clen;
		break;				/* Absolute line number. */
	case '0': case '1': case '2': case '3': case '4':
	case '5': case '6': case '7': case '8': case '9':
		*isaddrp = 1;
		F_SET(ecp, E_ABSMARK);

		if ((nret = nget_slong(&val, ecp->cp, &endp, 10)) != NUM_OK) {
			ex_badaddr(sp, NULL, A_NOTSET, nret);
			*errp = 1;
			return (0);
		}
		if (!NPFITS(MAX_REC_NUMBER, 0, val)) {
			ex_badaddr(sp, NULL, A_NOTSET, NUM_OVER);
			*errp = 1;
			return (0);
		}
		mp->lno = val;
		mp->cno = 0;
		ecp->clen -= (endp - ecp->cp);
		ecp->cp = endp;
		break;
	case '\'':				/* Use a mark. */
		*isaddrp = 1;
		F_SET(ecp, E_ABSMARK);

		if (ecp->clen == 1) {
			msgq(sp, M_ERR, "095|No mark name supplied");
			*errp = 1;
			return (0);
		}
		if (mark_get(sp, ecp->cp[1], mp, M_ERR)) {
			*errp = 1;
			return (0);
		}
		ecp->cp += 2;
		ecp->clen -= 2;
		break;
	case '\\':				/* Search: forward/backward. */
		/*
		 * !!!
		 * I can't find any difference between // and \/ or between
		 * ?? and \?.  Mark Horton doesn't remember there being any
		 * difference.  C'est la vie.
		 */
		if (ecp->clen < 2 ||
		    (ecp->cp[1] != '/' && ecp->cp[1] != '?')) {
			msgq(sp, M_ERR, "096|\\ not followed by / or ?");
			*errp = 1;
			return (0);
		}
		++ecp->cp;
		--ecp->clen;
		sf = ecp->cp[0] == '/' ? f_search : b_search;
		goto search;
	case '/':				/* Search forward. */
		sf = f_search;
		goto search;
	case '?':				/* Search backward. */
		sf = b_search;

search:		mp->lno = sp->lno;
		mp->cno = sp->cno;
		if (sf(sp, mp, mp, ecp->cp, ecp->clen, &endp,
		    SEARCH_MSG | SEARCH_PARSE | SEARCH_SET |
		    (F_ISSET(ecp, E_SEARCH_WMSG) ? SEARCH_WMSG : 0))) {
			*errp = 1;
			return (0);
		}

		/* Fix up the command pointers. */
		ecp->clen -= (endp - ecp->cp);
		ecp->cp = endp;

		*isaddrp = 1;
		F_SET(ecp, E_ABSMARK);
		break;
	case '.':				/* Current position. */
		*isaddrp = 1;
		mp->cno = sp->cno;

		/* If an empty file, then '.' is 0, not 1. */
		if (sp->lno == 1) {
			if (db_last(sp, &mp->lno))
				return (1);
			if (mp->lno != 0)
				mp->lno = 1;
		} else
			mp->lno = sp->lno;

		/*
		 * !!!
		 * Historically, .<number> was the same as .+<number>, i.e.
		 * the '+' could be omitted.  (This feature is found in ed
		 * as well.)
		 */
		if (ecp->clen > 1 && ISDIGIT(ecp->cp[1]))
			*ecp->cp = '+';
		else {
			++ecp->cp;
			--ecp->clen;
		}
		break;
	}

	/* Skip trailing <blank>s. */
	for (; ecp->clen > 0 &&
	    cmdskip(ecp->cp[0]); ++ecp->cp, --ecp->clen);

	/*
	 * Evaluate any offset.  If no address yet found, the offset
	 * is relative to ".".
	 */
	total = 0;
	if (ecp->clen != 0 && (ISDIGIT(ecp->cp[0]) ||
	    ecp->cp[0] == '+' || ecp->cp[0] == '-' ||
	    ecp->cp[0] == '^')) {
		if (!*isaddrp) {
			*isaddrp = 1;
			mp->lno = sp->lno;
			mp->cno = sp->cno;
		}
		/*
		 * Evaluate an offset, defined as:
		 *
		 *		[+-^<blank>]*[<blank>]*[0-9]*
		 *
		 * The rough translation is any number of signs, optionally
		 * followed by numbers, or a number by itself, all <blank>
		 * separated.
		 *
		 * !!!
		 * All address offsets were additive, e.g. "2 2 3p" was the
		 * same as "7p", or, "/ZZZ/ 2" was the same as "/ZZZ/+2".
		 * Note, however, "2 /ZZZ/" was an error.  It was also legal
		 * to insert signs without numbers, so "3 - 2" was legal, and
		 * equal to 4.
		 *
		 * !!!
		 * Offsets were historically permitted for any line address,
		 * e.g. the command "1,2 copy 2 2 2 2" copied lines 1,2 after
		 * line 8.
		 *
		 * !!!
		 * Offsets were historically permitted for search commands,
		 * and handled as addresses: "/pattern/2 2 2" was legal, and
		 * referenced the 6th line after pattern.
		 */
		F_SET(ecp, E_DELTA);
		for (;;) {
			for (; ecp->clen > 0 && cmdskip(ecp->cp[0]);
			    ++ecp->cp, --ecp->clen);
			if (ecp->clen == 0 || (!ISDIGIT(ecp->cp[0]) &&
			    ecp->cp[0] != '+' && ecp->cp[0] != '-' &&
			    ecp->cp[0] != '^'))
				break;
			if (!ISDIGIT(ecp->cp[0]) &&
			    !ISDIGIT(ecp->cp[1])) {
				total += ecp->cp[0] == '+' ? 1 : -1;
				--ecp->clen;
				++ecp->cp;
			} else {
				if (ecp->cp[0] == '-' ||
				    ecp->cp[0] == '^') {
					++ecp->cp;
					--ecp->clen;
					isneg = 1;
				} else
					isneg = 0;

				/* Get a signed long, add it to the total. */
				if ((nret = nget_slong(&val,
				    ecp->cp, &endp, 10)) != NUM_OK ||
				    (nret = NADD_SLONG(sp,
				    total, val)) != NUM_OK) {
					ex_badaddr(sp, NULL, A_NOTSET, nret);
					*errp = 1;
					return (0);
				}
				total += isneg ? -val : val;
				ecp->clen -= (endp - ecp->cp);
				ecp->cp = endp;
			}
		}
	}

	/*
	 * Any value less than 0 is an error.  Make sure that the new value
	 * will fit into a recno_t.
	 */
	if (*isaddrp && total != 0) {
		if (total < 0) {
			if (-total > mp->lno) {
				msgq(sp, M_ERR,
			    "097|Reference to a line number less than 0");
				*errp = 1;
				return (0);
			}
		} else
			if (!NPFITS(MAX_REC_NUMBER, mp->lno, total)) {
				ex_badaddr(sp, NULL, A_NOTSET, NUM_OVER);
				*errp = 1;
				return (0);
			}
		mp->lno += total;
	}
	return (0);
}


/*
 * ex_load --
 *	Load up the next command, which may be an @ buffer or global command.
 */
static int
ex_load(SCR *sp)
{
	GS *gp;
	EXCMD *ecp;
	RANGE *rp;

	F_CLR(sp, SC_EX_GLOBAL);

	/*
	 * Lose any exhausted commands.  We know that the first command
	 * can't be an AGV command, which makes things a bit easier.
	 */
	for (gp = sp->gp;;) {
		ecp = SLIST_FIRST(gp->ecq);

		/* Discard the allocated source name as requested. */
		if (F_ISSET(ecp, E_NAMEDISCARD))
			free(ecp->if_name);

		/*
		 * If we're back to the original structure, leave it around,
		 * since we've returned to the beginning of the command stack.
		 */
		if (ecp == &gp->excmd) {
			ecp->if_name = NULL;
			return (0);
		}

		/*
		 * ecp->clen will be 0 for the first discarded command, but
		 * may not be 0 for subsequent ones, e.g. if the original
		 * command was ":g/xx/@a|s/b/c/", then when we discard the
		 * command pushed on the stack by the @a, we have to resume
		 * the global command which included the substitute command.
		 */
		if (ecp->clen != 0)
			return (0);

		/*
		 * If it's an @, global or v command, we may need to continue
		 * the command on a different line.
		 */
		if (FL_ISSET(ecp->agv_flags, AGV_ALL)) {
			/* Discard any exhausted ranges. */
			while ((rp = TAILQ_FIRST(ecp->rq)) != NULL)
				if (rp->start > rp->stop) {
					TAILQ_REMOVE(ecp->rq, rp, q);
					free(rp);
				} else
					break;

			/* If there's another range, continue with it. */
			if (rp != NULL)
				break;

			/* If it's a global/v command, fix up the last line. */
			if (FL_ISSET(ecp->agv_flags,
			    AGV_GLOBAL | AGV_V) && ecp->range_lno != OOBLNO)
				if (db_exist(sp, ecp->range_lno))
					sp->lno = ecp->range_lno;
				else {
					if (db_last(sp, &sp->lno))
						return (1);
					if (sp->lno == 0)
						sp->lno = 1;
				}
			free(ecp->o_cp);
		}

		/* Discard the EXCMD. */
		SLIST_REMOVE_HEAD(gp->ecq, q);
		free(ecp);
	}

	/*
	 * We only get here if it's an active @, global or v command.  Set
	 * the current line number, and get a new copy of the command for
	 * the parser.  Note, the original pointer almost certainly moved,
	 * so we have play games.
	 */
	ecp->cp = ecp->o_cp;
	MEMCPY(ecp->cp, ecp->cp + ecp->o_clen, ecp->o_clen);
	ecp->clen = ecp->o_clen;
	ecp->range_lno = sp->lno = rp->start++;

	if (FL_ISSET(ecp->agv_flags, AGV_GLOBAL | AGV_V))
		F_SET(sp, SC_EX_GLOBAL);
	return (0);
}

/*
 * ex_discard --
 *	Discard any pending ex commands.
 */
static int
ex_discard(SCR *sp)
{
	GS *gp;
	EXCMD *ecp;
	RANGE *rp;

	/*
	 * We know the first command can't be an AGV command, so we don't
	 * process it specially.  We do, however, nail the command itself.
	 */
	for (gp = sp->gp;;) {
		ecp = SLIST_FIRST(gp->ecq);
		if (F_ISSET(ecp, E_NAMEDISCARD))
			free(ecp->if_name);
		/* Reset the last command without dropping it. */
		if (ecp == &gp->excmd)
			break;
		if (FL_ISSET(ecp->agv_flags, AGV_ALL)) {
			while ((rp = TAILQ_FIRST(ecp->rq)) != NULL) {
				TAILQ_REMOVE(ecp->rq, rp, q);
				free(rp);
			}
			free(ecp->o_cp);
		}
		SLIST_REMOVE_HEAD(gp->ecq, q);
		free(ecp);
	}

	ecp->if_name = NULL;
	ecp->clen = 0;
	return (0);
}

/*
 * ex_unknown --
 *	Display an unknown command name.
 */
static void
ex_unknown(SCR *sp, CHAR_T *cmd, size_t len)
{
	size_t blen;
	CHAR_T *bp;

	GET_SPACE_GOTOW(sp, bp, blen, len + 1);
	bp[len] = '\0';
	MEMCPY(bp, cmd, len);
	msgq_wstr(sp, M_ERR, bp, "098|The %s command is unknown");
	FREE_SPACEW(sp, bp, blen);

alloc_err:
	return;
}

/*
 * ex_is_abbrev -
 *	The vi text input routine needs to know if ex thinks this is an
 *	[un]abbreviate command, so it can turn off abbreviations.  See
 *	the usual ranting in the vi/v_txt_ev.c:txt_abbrev() routine.
 *
 * PUBLIC: int ex_is_abbrev(CHAR_T *, size_t);
 */
int
ex_is_abbrev(CHAR_T *name, size_t len)
{
	EXCMDLIST const *cp;

	return ((cp = ex_comm_search(name, len)) != NULL &&
	    (cp == &cmds[C_ABBR] || cp == &cmds[C_UNABBREVIATE]));
}

/*
 * ex_is_unmap -
 *	The vi text input routine needs to know if ex thinks this is an
 *	unmap command, so it can turn off input mapping.  See the usual
 *	ranting in the vi/v_txt_ev.c:txt_unmap() routine.
 *
 * PUBLIC: int ex_is_unmap(CHAR_T *, size_t);
 */
int
ex_is_unmap(CHAR_T *name, size_t len)
{
	EXCMDLIST const *cp;

	/*
	 * The command the vi input routines are really interested in
	 * is "unmap!", not just unmap.
	 */
	if (name[len - 1] != '!')
		return (0);
	--len;
	return ((cp = ex_comm_search(name, len)) != NULL &&
	    cp == &cmds[C_UNMAP]);
}

/*
 * ex_comm_search --
 *	Search for a command name.
 */
static EXCMDLIST const *
ex_comm_search(CHAR_T *name, size_t len)
{
	EXCMDLIST const *cp;

	for (cp = cmds; cp->name != NULL; ++cp) {
		if (cp->name[0] > name[0])
			return (NULL);
		if (cp->name[0] != name[0])
			continue;
		if (!MEMCMP(name, cp->name, len))
			return (cp);
	}
	return (NULL);
}

/*
 * ex_badaddr --
 *	Display a bad address message.
 *
 * PUBLIC: void ex_badaddr
 * PUBLIC:   (SCR *, EXCMDLIST const *, enum badaddr, enum nresult);
 */
void
ex_badaddr(SCR *sp, const EXCMDLIST *cp, enum badaddr ba, enum nresult nret)
{
	recno_t lno;

	switch (nret) {
	case NUM_OK:
		break;
	case NUM_ERR:
		msgq(sp, M_SYSERR, NULL);
		return;
	case NUM_OVER:
		msgq(sp, M_ERR, "099|Address value overflow");
		return;
	case NUM_UNDER:
		msgq(sp, M_ERR, "100|Address value underflow");
		return;
	}

	/*
	 * When encountering an address error, tell the user if there's no
	 * underlying file, that's the real problem.
	 */
	if (sp->ep == NULL) {
		ex_wemsg(sp, cp ? cp->name : NULL, EXM_NOFILEYET);
		return;
	}

	switch (ba) {
	case A_COMBO:
		msgq(sp, M_ERR, "101|Illegal address combination");
		break;
	case A_EOF:
		if (db_last(sp, &lno))
			return;
		if (lno != 0) {
			msgq(sp, M_ERR,
			    "102|Illegal address: only %lu lines in the file",
			    (u_long)lno);
			break;
		}
		/* FALLTHROUGH */
	case A_EMPTY:
		msgq(sp, M_ERR, "103|Illegal address: the file is empty");
		break;
	case A_NOTSET:
		abort();
		/* NOTREACHED */
	case A_ZERO:
		msgq_wstr(sp, M_ERR, cp->name,
		    "104|The %s command doesn't permit an address of 0");
		break;
	}
	return;
}

#if defined(DEBUG) && defined(COMLOG)
/*
 * ex_comlog --
 *	Log ex commands.
 */
static void
ex_comlog(sp, ecp)
	SCR *sp;
	EXCMD *ecp;
{
	TRACE(sp, "ecmd: "WS, ecp->cmd->name);
	if (ecp->addrcnt > 0) {
		TRACE(sp, " a1 %d", ecp->addr1.lno);
		if (ecp->addrcnt > 1)
			TRACE(sp, " a2: %d", ecp->addr2.lno);
	}
	if (ecp->lineno)
		TRACE(sp, " line %d", ecp->lineno);
	if (ecp->flags)
		TRACE(sp, " flags 0x%x", ecp->flags);
	if (FL_ISSET(ecp->iflags, E_C_BUFFER))
		TRACE(sp, " buffer "WC, ecp->buffer);
	if (ecp->argc) {
		int cnt;
		for (cnt = 0; cnt < ecp->argc; ++cnt)
			TRACE(sp, " arg %d: {"WS"}", cnt, ecp->argv[cnt]->bp);
	}
	TRACE(sp, "\n");
}
#endif

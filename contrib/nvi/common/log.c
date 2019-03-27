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
static const char sccsid[] = "$Id: log.c,v 10.27 2011/07/13 06:25:50 zy Exp $";
#endif /* not lint */

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/stat.h>

#include <bitstring.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"

/*
 * The log consists of records, each containing a type byte and a variable
 * length byte string, as follows:
 *
 *	LOG_CURSOR_INIT		MARK
 *	LOG_CURSOR_END		MARK
 *	LOG_LINE_APPEND 	recno_t		char *
 *	LOG_LINE_DELETE		recno_t		char *
 *	LOG_LINE_INSERT		recno_t		char *
 *	LOG_LINE_RESET_F	recno_t		char *
 *	LOG_LINE_RESET_B	recno_t		char *
 *	LOG_MARK		LMARK
 *
 * We do before image physical logging.  This means that the editor layer
 * MAY NOT modify records in place, even if simply deleting or overwriting
 * characters.  Since the smallest unit of logging is a line, we're using
 * up lots of space.  This may eventually have to be reduced, probably by
 * doing logical logging, which is a much cooler database phrase.
 *
 * The implementation of the historic vi 'u' command, using roll-forward and
 * roll-back, is simple.  Each set of changes has a LOG_CURSOR_INIT record,
 * followed by a number of other records, followed by a LOG_CURSOR_END record.
 * LOG_LINE_RESET records come in pairs.  The first is a LOG_LINE_RESET_B
 * record, and is the line before the change.  The second is LOG_LINE_RESET_F,
 * and is the line after the change.  Roll-back is done by backing up to the
 * first LOG_CURSOR_INIT record before a change.  Roll-forward is done in a
 * similar fashion.
 *
 * The 'U' command is implemented by rolling backward to a LOG_CURSOR_END
 * record for a line different from the current one.  It should be noted that
 * this means that a subsequent 'u' command will make a change based on the
 * new position of the log's cursor.  This is okay, and, in fact, historic vi
 * behaved that way.
 */

static int	log_cursor1(SCR *, int);
static void	log_err(SCR *, char *, int);
#if defined(DEBUG) && 0
static void	log_trace(SCR *, char *, recno_t, u_char *);
#endif
static int	apply_with(int (*)(SCR *, recno_t, CHAR_T *, size_t),
					SCR *, recno_t, u_char *, size_t);

/* Try and restart the log on failure, i.e. if we run out of memory. */
#define	LOG_ERR {							\
	log_err(sp, __FILE__, __LINE__);				\
	return (1);							\
}

/* offset of CHAR_T string in log needs to be aligned on some systems
 * because it is passed to db_set as a string
 */
typedef struct {
	char    data[sizeof(u_char) /* type */ + sizeof(recno_t)];
	CHAR_T  str[1];
} log_t;
#define CHAR_T_OFFSET ((char *)(((log_t*)0)->str) - (char *)0)

/*
 * log_init --
 *	Initialize the logging subsystem.
 *
 * PUBLIC: int log_init(SCR *, EXF *);
 */
int
log_init(
	SCR *sp,
	EXF *ep)
{
	/*
	 * !!!
	 * ep MAY NOT BE THE SAME AS sp->ep, DON'T USE THE LATTER.
	 *
	 * Initialize the buffer.  The logging subsystem has its own
	 * buffers because the global ones are almost by definition
	 * going to be in use when the log runs.
	 */
	ep->l_lp = NULL;
	ep->l_len = 0;
	ep->l_cursor.lno = 1;		/* XXX Any valid recno. */
	ep->l_cursor.cno = 0;
	ep->l_high = ep->l_cur = 1;

	ep->log = dbopen(NULL, O_CREAT | O_NONBLOCK | O_RDWR,
	    S_IRUSR | S_IWUSR, DB_RECNO, NULL);
	if (ep->log == NULL) {
		msgq(sp, M_SYSERR, "009|Log file");
		F_SET(ep, F_NOLOG);
		return (1);
	}

	return (0);
}

/*
 * log_end --
 *	Close the logging subsystem.
 *
 * PUBLIC: int log_end(SCR *, EXF *);
 */
int
log_end(
	SCR *sp,
	EXF *ep)
{
	/*
	 * !!!
	 * ep MAY NOT BE THE SAME AS sp->ep, DON'T USE THE LATTER.
	 */
	if (ep->log != NULL) {
		(void)(ep->log->close)(ep->log);
		ep->log = NULL;
	}
	if (ep->l_lp != NULL) {
		free(ep->l_lp);
		ep->l_lp = NULL;
	}
	ep->l_len = 0;
	ep->l_cursor.lno = 1;		/* XXX Any valid recno. */
	ep->l_cursor.cno = 0;
	ep->l_high = ep->l_cur = 1;
	return (0);
}

/*
 * log_cursor --
 *	Log the current cursor position, starting an event.
 *
 * PUBLIC: int log_cursor(SCR *);
 */
int
log_cursor(SCR *sp)
{
	EXF *ep;

	ep = sp->ep;
	if (F_ISSET(ep, F_NOLOG))
		return (0);

	/*
	 * If any changes were made since the last cursor init,
	 * put out the ending cursor record.
	 */
	if (ep->l_cursor.lno == OOBLNO) {
		ep->l_cursor.lno = sp->lno;
		ep->l_cursor.cno = sp->cno;
		return (log_cursor1(sp, LOG_CURSOR_END));
	}
	ep->l_cursor.lno = sp->lno;
	ep->l_cursor.cno = sp->cno;
	return (0);
}

/*
 * log_cursor1 --
 *	Actually push a cursor record out.
 */
static int
log_cursor1(
	SCR *sp,
	int type)
{
	DBT data, key;
	EXF *ep;

	ep = sp->ep;

	BINC_RETC(sp, ep->l_lp, ep->l_len, sizeof(u_char) + sizeof(MARK));
	ep->l_lp[0] = type;
	memmove(ep->l_lp + sizeof(u_char), &ep->l_cursor, sizeof(MARK));

	key.data = &ep->l_cur;
	key.size = sizeof(recno_t);
	data.data = ep->l_lp;
	data.size = sizeof(u_char) + sizeof(MARK);
	if (ep->log->put(ep->log, &key, &data, 0) == -1)
		LOG_ERR;

#if defined(DEBUG) && 0
	TRACE(sp, "%lu: %s: %u/%u\n", ep->l_cur,
	    type == LOG_CURSOR_INIT ? "log_cursor_init" : "log_cursor_end",
	    sp->lno, sp->cno);
#endif
	/* Reset high water mark. */
	ep->l_high = ++ep->l_cur;

	return (0);
}

/*
 * log_line --
 *	Log a line change.
 *
 * PUBLIC: int log_line(SCR *, recno_t, u_int);
 */
int
log_line(
	SCR *sp,
	recno_t lno,
	u_int action)
{
	DBT data, key;
	EXF *ep;
	size_t len;
	CHAR_T *lp;
	recno_t lcur;

	ep = sp->ep;
	if (F_ISSET(ep, F_NOLOG))
		return (0);

	/*
	 * XXX
	 *
	 * Kluge for vi.  Clear the EXF undo flag so that the
	 * next 'u' command does a roll-back, regardless.
	 */
	F_CLR(ep, F_UNDO);

	/* Put out one initial cursor record per set of changes. */
	if (ep->l_cursor.lno != OOBLNO) {
		if (log_cursor1(sp, LOG_CURSOR_INIT))
			return (1);
		ep->l_cursor.lno = OOBLNO;
	}

	/*
	 * Put out the changes.  If it's a LOG_LINE_RESET_B call, it's a
	 * special case, avoid the caches.  Also, if it fails and it's
	 * line 1, it just means that the user started with an empty file,
	 * so fake an empty length line.
	 */
	if (action == LOG_LINE_RESET_B) {
		if (db_get(sp, lno, DBG_NOCACHE, &lp, &len)) {
			if (lno != 1) {
				db_err(sp, lno);
				return (1);
			}
			len = 0;
			lp = L("");
		}
	} else
		if (db_get(sp, lno, DBG_FATAL, &lp, &len))
			return (1);
	BINC_RETC(sp,
	    ep->l_lp, ep->l_len,
	    len * sizeof(CHAR_T) + CHAR_T_OFFSET);
	ep->l_lp[0] = action;
	memmove(ep->l_lp + sizeof(u_char), &lno, sizeof(recno_t));
	memmove(ep->l_lp + CHAR_T_OFFSET, lp, len * sizeof(CHAR_T));

	lcur = ep->l_cur;
	key.data = &lcur;
	key.size = sizeof(recno_t);
	data.data = ep->l_lp;
	data.size = len * sizeof(CHAR_T) + CHAR_T_OFFSET;
	if (ep->log->put(ep->log, &key, &data, 0) == -1)
		LOG_ERR;

#if defined(DEBUG) && 0
	switch (action) {
	case LOG_LINE_APPEND:
		TRACE(sp, "%lu: log_line: append: %lu {%u}\n",
		    ep->l_cur, lno, len);
		break;
	case LOG_LINE_DELETE:
		TRACE(sp, "%lu: log_line: delete: %lu {%u}\n",
		    ep->l_cur, lno, len);
		break;
	case LOG_LINE_INSERT:
		TRACE(sp, "%lu: log_line: insert: %lu {%u}\n",
		    ep->l_cur, lno, len);
		break;
	case LOG_LINE_RESET_F:
		TRACE(sp, "%lu: log_line: reset_f: %lu {%u}\n",
		    ep->l_cur, lno, len);
		break;
	case LOG_LINE_RESET_B:
		TRACE(sp, "%lu: log_line: reset_b: %lu {%u}\n",
		    ep->l_cur, lno, len);
		break;
	}
#endif
	/* Reset high water mark. */
	ep->l_high = ++ep->l_cur;

	return (0);
}

/*
 * log_mark --
 *	Log a mark position.  For the log to work, we assume that there
 *	aren't any operations that just put out a log record -- this
 *	would mean that undo operations would only reset marks, and not
 *	cause any other change.
 *
 * PUBLIC: int log_mark(SCR *, LMARK *);
 */
int
log_mark(
	SCR *sp,
	LMARK *lmp)
{
	DBT data, key;
	EXF *ep;

	ep = sp->ep;
	if (F_ISSET(ep, F_NOLOG))
		return (0);

	/* Put out one initial cursor record per set of changes. */
	if (ep->l_cursor.lno != OOBLNO) {
		if (log_cursor1(sp, LOG_CURSOR_INIT))
			return (1);
		ep->l_cursor.lno = OOBLNO;
	}

	BINC_RETC(sp, ep->l_lp,
	    ep->l_len, sizeof(u_char) + sizeof(LMARK));
	ep->l_lp[0] = LOG_MARK;
	memmove(ep->l_lp + sizeof(u_char), lmp, sizeof(LMARK));

	key.data = &ep->l_cur;
	key.size = sizeof(recno_t);
	data.data = ep->l_lp;
	data.size = sizeof(u_char) + sizeof(LMARK);
	if (ep->log->put(ep->log, &key, &data, 0) == -1)
		LOG_ERR;

#if defined(DEBUG) && 0
	TRACE(sp, "%lu: mark %c: %lu/%u\n",
	    ep->l_cur, lmp->name, lmp->lno, lmp->cno);
#endif
	/* Reset high water mark. */
	ep->l_high = ++ep->l_cur;
	return (0);
}

/*
 * Log_backward --
 *	Roll the log backward one operation.
 *
 * PUBLIC: int log_backward(SCR *, MARK *);
 */
int
log_backward(
	SCR *sp,
	MARK *rp)
{
	DBT key, data;
	EXF *ep;
	LMARK lm;
	MARK m;
	recno_t lno;
	int didop;
	u_char *p;

	ep = sp->ep;
	if (F_ISSET(ep, F_NOLOG)) {
		msgq(sp, M_ERR,
		    "010|Logging not being performed, undo not possible");
		return (1);
	}

	if (ep->l_cur == 1) {
		msgq(sp, M_BERR, "011|No changes to undo");
		return (1);
	}

	F_SET(ep, F_NOLOG);		/* Turn off logging. */

	key.data = &ep->l_cur;		/* Initialize db request. */
	key.size = sizeof(recno_t);
	for (didop = 0;;) {
		--ep->l_cur;
		if (ep->log->get(ep->log, &key, &data, 0))
			LOG_ERR;
#if defined(DEBUG) && 0
		log_trace(sp, "log_backward", ep->l_cur, data.data);
#endif
		switch (*(p = (u_char *)data.data)) {
		case LOG_CURSOR_INIT:
			if (didop) {
				memmove(rp, p + sizeof(u_char), sizeof(MARK));
				F_CLR(ep, F_NOLOG);
				return (0);
			}
			break;
		case LOG_CURSOR_END:
			break;
		case LOG_LINE_APPEND:
		case LOG_LINE_INSERT:
			didop = 1;
			memmove(&lno, p + sizeof(u_char), sizeof(recno_t));
			if (db_delete(sp, lno))
				goto err;
			++sp->rptlines[L_DELETED];
			break;
		case LOG_LINE_DELETE:
			didop = 1;
			memmove(&lno, p + sizeof(u_char), sizeof(recno_t));
			if (apply_with(db_insert, sp, lno,
				p + CHAR_T_OFFSET, data.size - CHAR_T_OFFSET))
				goto err;
			++sp->rptlines[L_ADDED];
			break;
		case LOG_LINE_RESET_F:
			break;
		case LOG_LINE_RESET_B:
			didop = 1;
			memmove(&lno, p + sizeof(u_char), sizeof(recno_t));
			if (apply_with(db_set, sp, lno,
				p + CHAR_T_OFFSET, data.size - CHAR_T_OFFSET))
				goto err;
			if (sp->rptlchange != lno) {
				sp->rptlchange = lno;
				++sp->rptlines[L_CHANGED];
			}
			break;
		case LOG_MARK:
			didop = 1;
			memmove(&lm, p + sizeof(u_char), sizeof(LMARK));
			m.lno = lm.lno;
			m.cno = lm.cno;
			if (mark_set(sp, lm.name, &m, 0))
				goto err;
			break;
		default:
			abort();
		}
	}

err:	F_CLR(ep, F_NOLOG);
	return (1);
}

/*
 * Log_setline --
 *	Reset the line to its original appearance.
 *
 * XXX
 * There's a bug in this code due to our not logging cursor movements
 * unless a change was made.  If you do a change, move off the line,
 * then move back on and do a 'U', the line will be restored to the way
 * it was before the original change.
 *
 * PUBLIC: int log_setline(SCR *);
 */
int
log_setline(SCR *sp)
{
	DBT key, data;
	EXF *ep;
	LMARK lm;
	MARK m;
	recno_t lno;
	u_char *p;

	ep = sp->ep;
	if (F_ISSET(ep, F_NOLOG)) {
		msgq(sp, M_ERR,
		    "012|Logging not being performed, undo not possible");
		return (1);
	}

	if (ep->l_cur == 1)
		return (1);

	F_SET(ep, F_NOLOG);		/* Turn off logging. */

	key.data = &ep->l_cur;		/* Initialize db request. */
	key.size = sizeof(recno_t);
	for (;;) {
		--ep->l_cur;
		if (ep->log->get(ep->log, &key, &data, 0))
			LOG_ERR;
#if defined(DEBUG) && 0
		log_trace(sp, "log_setline", ep->l_cur, data.data);
#endif
		switch (*(p = (u_char *)data.data)) {
		case LOG_CURSOR_INIT:
			memmove(&m, p + sizeof(u_char), sizeof(MARK));
			if (m.lno != sp->lno || ep->l_cur == 1) {
				F_CLR(ep, F_NOLOG);
				return (0);
			}
			break;
		case LOG_CURSOR_END:
			memmove(&m, p + sizeof(u_char), sizeof(MARK));
			if (m.lno != sp->lno) {
				++ep->l_cur;
				F_CLR(ep, F_NOLOG);
				return (0);
			}
			break;
		case LOG_LINE_APPEND:
		case LOG_LINE_INSERT:
		case LOG_LINE_DELETE:
		case LOG_LINE_RESET_F:
			break;
		case LOG_LINE_RESET_B:
			memmove(&lno, p + sizeof(u_char), sizeof(recno_t));
			if (lno == sp->lno &&
				apply_with(db_set, sp, lno,
				p + CHAR_T_OFFSET, data.size - CHAR_T_OFFSET))
				goto err;
			if (sp->rptlchange != lno) {
				sp->rptlchange = lno;
				++sp->rptlines[L_CHANGED];
			}
		case LOG_MARK:
			memmove(&lm, p + sizeof(u_char), sizeof(LMARK));
			m.lno = lm.lno;
			m.cno = lm.cno;
			if (mark_set(sp, lm.name, &m, 0))
				goto err;
			break;
		default:
			abort();
		}
	}

err:	F_CLR(ep, F_NOLOG);
	return (1);
}

/*
 * Log_forward --
 *	Roll the log forward one operation.
 *
 * PUBLIC: int log_forward(SCR *, MARK *);
 */
int
log_forward(
	SCR *sp,
	MARK *rp)
{
	DBT key, data;
	EXF *ep;
	LMARK lm;
	MARK m;
	recno_t lno;
	int didop;
	u_char *p;

	ep = sp->ep;
	if (F_ISSET(ep, F_NOLOG)) {
		msgq(sp, M_ERR,
	    "013|Logging not being performed, roll-forward not possible");
		return (1);
	}

	if (ep->l_cur == ep->l_high) {
		msgq(sp, M_BERR, "014|No changes to re-do");
		return (1);
	}

	F_SET(ep, F_NOLOG);		/* Turn off logging. */

	key.data = &ep->l_cur;		/* Initialize db request. */
	key.size = sizeof(recno_t);
	for (didop = 0;;) {
		++ep->l_cur;
		if (ep->log->get(ep->log, &key, &data, 0))
			LOG_ERR;
#if defined(DEBUG) && 0
		log_trace(sp, "log_forward", ep->l_cur, data.data);
#endif
		switch (*(p = (u_char *)data.data)) {
		case LOG_CURSOR_END:
			if (didop) {
				++ep->l_cur;
				memmove(rp, p + sizeof(u_char), sizeof(MARK));
				F_CLR(ep, F_NOLOG);
				return (0);
			}
			break;
		case LOG_CURSOR_INIT:
			break;
		case LOG_LINE_APPEND:
		case LOG_LINE_INSERT:
			didop = 1;
			memmove(&lno, p + sizeof(u_char), sizeof(recno_t));
			if (apply_with(db_insert, sp, lno,
				p + CHAR_T_OFFSET, data.size - CHAR_T_OFFSET))
				goto err;
			++sp->rptlines[L_ADDED];
			break;
		case LOG_LINE_DELETE:
			didop = 1;
			memmove(&lno, p + sizeof(u_char), sizeof(recno_t));
			if (db_delete(sp, lno))
				goto err;
			++sp->rptlines[L_DELETED];
			break;
		case LOG_LINE_RESET_B:
			break;
		case LOG_LINE_RESET_F:
			didop = 1;
			memmove(&lno, p + sizeof(u_char), sizeof(recno_t));
			if (apply_with(db_set, sp, lno,
				p + CHAR_T_OFFSET, data.size - CHAR_T_OFFSET))
				goto err;
			if (sp->rptlchange != lno) {
				sp->rptlchange = lno;
				++sp->rptlines[L_CHANGED];
			}
			break;
		case LOG_MARK:
			didop = 1;
			memmove(&lm, p + sizeof(u_char), sizeof(LMARK));
			m.lno = lm.lno;
			m.cno = lm.cno;
			if (mark_set(sp, lm.name, &m, 0))
				goto err;
			break;
		default:
			abort();
		}
	}

err:	F_CLR(ep, F_NOLOG);
	return (1);
}

/*
 * log_err --
 *	Try and restart the log on failure, i.e. if we run out of memory.
 */
static void
log_err(
	SCR *sp,
	char *file,
	int line)
{
	EXF *ep;

	msgq(sp, M_SYSERR, "015|%s/%d: log put error", tail(file), line);
	ep = sp->ep;
	(void)ep->log->close(ep->log);
	if (!log_init(sp, ep))
		msgq(sp, M_ERR, "267|Log restarted");
}

#if defined(DEBUG) && 0
static void
log_trace(
	SCR *sp,
	char *msg,
	recno_t rno,
	u_char *p)
{
	LMARK lm;
	MARK m;
	recno_t lno;

	switch (*p) {
	case LOG_CURSOR_INIT:
		memmove(&m, p + sizeof(u_char), sizeof(MARK));
		TRACE(sp, "%lu: %s:  C_INIT: %u/%u\n", rno, msg, m.lno, m.cno);
		break;
	case LOG_CURSOR_END:
		memmove(&m, p + sizeof(u_char), sizeof(MARK));
		TRACE(sp, "%lu: %s:   C_END: %u/%u\n", rno, msg, m.lno, m.cno);
		break;
	case LOG_LINE_APPEND:
		memmove(&lno, p + sizeof(u_char), sizeof(recno_t));
		TRACE(sp, "%lu: %s:  APPEND: %lu\n", rno, msg, lno);
		break;
	case LOG_LINE_INSERT:
		memmove(&lno, p + sizeof(u_char), sizeof(recno_t));
		TRACE(sp, "%lu: %s:  INSERT: %lu\n", rno, msg, lno);
		break;
	case LOG_LINE_DELETE:
		memmove(&lno, p + sizeof(u_char), sizeof(recno_t));
		TRACE(sp, "%lu: %s:  DELETE: %lu\n", rno, msg, lno);
		break;
	case LOG_LINE_RESET_F:
		memmove(&lno, p + sizeof(u_char), sizeof(recno_t));
		TRACE(sp, "%lu: %s: RESET_F: %lu\n", rno, msg, lno);
		break;
	case LOG_LINE_RESET_B:
		memmove(&lno, p + sizeof(u_char), sizeof(recno_t));
		TRACE(sp, "%lu: %s: RESET_B: %lu\n", rno, msg, lno);
		break;
	case LOG_MARK:
		memmove(&lm, p + sizeof(u_char), sizeof(LMARK));
		TRACE(sp,
		    "%lu: %s:    MARK: %u/%u\n", rno, msg, lm.lno, lm.cno);
		break;
	default:
		abort();
	}
}
#endif

/*
 * apply_with --
 *	Apply a realigned line from the log db to the file db.
 */
static int
apply_with(
	int (*db_func)(SCR *, recno_t, CHAR_T *, size_t),
	SCR *sp,
	recno_t lno,
	u_char *p,
	size_t len)
{
#ifdef USE_WIDECHAR
	typedef unsigned long nword;

	static size_t blen;
	static nword *bp;
	nword *lp = (nword *)((uintptr_t)p / sizeof(nword) * sizeof(nword));

	if (lp != (nword *)p) {
		int offl = ((uintptr_t)p - (uintptr_t)lp) << 3;
		int offr = (sizeof(nword) << 3) - offl;
		size_t i, cnt = (len + sizeof(nword) / 2) / sizeof(nword);

		if (len > blen) {
			blen = p2roundup(MAX(len, 512));
			REALLOC(sp, bp, nword *, blen);
			if (bp == NULL)
				return (1);
		}
		for (i = 0; i < cnt; ++i)
#if BYTE_ORDER == BIG_ENDIAN
			bp[i] = (lp[i] << offl) ^ (lp[i+1] >> offr);
#else
			bp[i] = (lp[i] >> offl) ^ (lp[i+1] << offr);
#endif
		p = (u_char *)bp;
	}
#endif
	return db_func(sp, lno, (CHAR_T *)p, len / sizeof(CHAR_T));
}

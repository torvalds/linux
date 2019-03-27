/*-
 * Copyright (c) 1992, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1992, 1993, 1994, 1995, 1996
 *	Keith Bostic.  All rights reserved.
 * Copyright (c) 1994, 1996
 *	Rob Mayoff.  All rights reserved.
 *
 * See the LICENSE file for redistribution information.
 *
 *	$Id: tag.h,v 10.9 2012/07/06 16:38:36 zy Exp $
 */

/*
 * Cscope connection information.  One of these is maintained per cscope
 * connection, linked from the EX_PRIVATE structure.
 */
struct _csc {
	SLIST_ENTRY(_csc) q;	/* Linked list of cscope connections. */

	char		*dname;	/* Base directory of this cscope connection. */
	size_t		 dlen;	/* Length of base directory. */
	pid_t		 pid;	/* PID of the connected cscope process. */
	struct timespec	 mtim;	/* Last modification time of cscope database. */

	FILE	*from_fp;	/* from cscope: FILE. */
	int	 from_fd;	/* from cscope: file descriptor. */
	FILE	*to_fp;		/* to cscope: FILE. */
	int	 to_fd;		/* to cscope: file descriptor. */

	char   **paths;		/* Array of search paths for this cscope. */
	char	*pbuf;		/* Search path buffer. */
	size_t	 pblen;		/* Search path buffer length. */

	char	 buf[1];	/* Variable length buffer. */
};

/*
 * Tag file information.  One of these is maintained per tag file, linked
 * from the EXPRIVATE structure.
 */
struct _tagf {			/* Tag files. */
	TAILQ_ENTRY(_tagf) q;	/* Linked list of tag files. */
	char	*name;		/* Tag file name. */
	int	 errnum;	/* Errno. */

#define	TAGF_ERR	0x01	/* Error occurred. */
#define	TAGF_ERR_WARN	0x02	/* Error reported. */
	u_int8_t flags;
};

/*
 * Tags are structured internally as follows:
 *
 * +----+    +----+	+----+     +----+
 * | EP | -> | Q1 | <-- | T1 | <-- | T2 |
 * +----+    +----+ --> +----+ --> +----+
 *	     |
 *	     +----+     +----+
 *	     | Q2 | <-- | T1 |
 *	     +----+ --> +----+
 *	     |
 *	     +----+	+----+
 *	     | Q3 | <-- | T1 |
 *	     +----+ --> +----+
 *
 * Each Q is a TAGQ, or tag "query", which is the result of one tag or cscope
 * command.  Each Q references one or more TAG's, or tagged file locations.
 *
 * tag:		put a new Q at the head	(^])
 * tagnext:	T1 -> T2 inside Q	(^N)
 * tagprev:	T2 -> T1 inside Q	(^P)
 * tagpop:	discard Q		(^T)
 * tagtop:	discard all Q
 */
struct _tag {			/* Tag list. */
	TAILQ_ENTRY(_tag) q;	/* Linked list of tags. */

				/* Tag pop/return information. */
	FREF	*frp;		/* Saved file. */
	recno_t	 lno;		/* Saved line number. */
	size_t	 cno;		/* Saved column number. */

	char	*fname;		/* Filename. */
	size_t	 fnlen;		/* Filename length. */
	recno_t	 slno;		/* Search line number. */
	CHAR_T	*search;	/* Search string. */
	size_t	 slen;		/* Search string length. */
	CHAR_T	*msg;		/* Message string. */
	size_t	 mlen;		/* Message string length. */

	CHAR_T	 buf[1];	/* Variable length buffer. */
};

struct _tagq {			/* Tag queue. */
	TAILQ_ENTRY(_tagq) q;	/* Linked list of tag queues. */
				/* This queue's tag list. */
	TAILQ_HEAD(_tagqh, _tag) tagq[1];

	TAG	*current;	/* Current TAG within the queue. */

	char	*tag;		/* Tag string. */
	size_t	 tlen;		/* Tag string length. */

#define	TAG_CSCOPE	0x01	/* Cscope tag. */
	u_int8_t flags;

	char	 buf[1];	/* Variable length buffer. */
};

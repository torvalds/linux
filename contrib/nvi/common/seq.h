/*-
 * Copyright (c) 1992, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1992, 1993, 1994, 1995, 1996
 *	Keith Bostic.  All rights reserved.
 *
 * See the LICENSE file for redistribution information.
 *
 *	$Id: seq.h,v 10.4 2011/12/11 21:43:39 zy Exp $
 */

/*
 * Map and abbreviation structures.
 *
 * The map structure is singly linked list, sorted by input string and by
 * input length within the string.  (The latter is necessary so that short
 * matches will happen before long matches when the list is searched.)
 * Additionally, there is a bitmap which has bits set if there are entries
 * starting with the corresponding character.  This keeps us from walking
 * the list unless it's necessary.
 *
 * The name and the output fields of a SEQ can be empty, i.e. NULL.
 * Only the input field is required.
 *
 * XXX
 * The fast-lookup bits are never turned off -- users don't usually unmap
 * things, though, so it's probably not a big deal.
 */
struct _seq {
	SLIST_ENTRY(_seq) q;		/* Linked list of all sequences. */
	seq_t	 stype;			/* Sequence type. */
	CHAR_T	*name;			/* Sequence name (if any). */
	size_t	 nlen;			/* Name length. */
	CHAR_T	*input;			/* Sequence input keys. */
	size_t	 ilen;			/* Input keys length. */
	CHAR_T	*output;		/* Sequence output keys. */
	size_t	 olen;			/* Output keys length. */

#define	SEQ_FUNCMAP	0x01		/* If unresolved function key.*/
#define	SEQ_NOOVERWRITE	0x02		/* Don't replace existing entry. */
#define	SEQ_SCREEN	0x04		/* If screen specific. */
#define	SEQ_USERDEF	0x08		/* If user defined. */
	u_int8_t flags;
};

/*
 * Copyright (c) 2006 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 */

/*
**  SENDMAIL.H -- MTA-specific definitions for sendmail.
*/

#ifndef _SM_SENDMAIL_H
# define _SM_SENDMAIL_H 1

/* "out of band" indicator */
#define METAQUOTE	((unsigned char)0377)	/* quotes the next octet */

extern int	dequote_internal_chars __P((char *, char *, int));
extern char	*quote_internal_chars __P((char *, char *, int *));
extern char	*str2prt __P((char *));

#endif /* ! _SM_SENDMAIL_H */

/*
 * Copyright (c) 1999-2002 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 *	$Id: bf.h,v 8.17 2013-11-22 20:51:55 ca Exp $
 *
 * Contributed by Exactis.com, Inc.
 *
 */

#ifndef BF_H
# define BF_H 1

extern SM_FILE_T	*bfopen __P((char *, MODE_T, size_t, long));
extern int		bfcommit __P((SM_FILE_T *));
extern int		bfrewind __P((SM_FILE_T *));
extern int		bftruncate __P((SM_FILE_T *));
extern int		bfclose __P((SM_FILE_T *));
extern bool		bftest __P((SM_FILE_T *));

/* "what" flags for sm_io_setinfo() for the SM_FILE_TYPE file type */
# define SM_BF_SETBUFSIZE	1000 /* set buffer size */
# define SM_BF_COMMIT		1001 /* commit file to disk */
# define SM_BF_TRUNCATE		1002 /* truncate the file */
# define SM_BF_TEST		1003 /* historical support; temp */

# define BF_FILE_TYPE	"SendmailBufferedFile"
#endif /* ! BF_H */

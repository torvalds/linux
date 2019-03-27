/*
 * Copyright (c) 1998-2001 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 * Copyright (c) 1983, 1995-1997 Eric P. Allman.  All rights reserved.
 * Copyright (c) 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 *
 *	$Id: sendmail.h,v 8.69 2013-11-22 20:51:30 ca Exp $
 */

/*
**  SENDMAIL.H -- Global definitions for sendmail.
*/

#include <stdio.h>
#include <sm/bitops.h>
#include <sm/io.h>
#include <sm/string.h>
#include "conf.h"

/**********************************************************************
**  Table sizes, etc....
**	There shouldn't be much need to change these....
**********************************************************************/
#ifndef MAXMAILERS
# define MAXMAILERS	25	/* maximum mailers known to system */
#endif /* ! MAXMAILERS */

/*
**  Flags passed to safefile/safedirpath.
*/

#define SFF_ANYFILE	0L		/* no special restrictions */
#define SFF_MUSTOWN	0x00000001L	/* user must own this file */
#define SFF_NOSLINK	0x00000002L	/* file cannot be a symbolic link */
#define SFF_ROOTOK	0x00000004L	/* ok for root to own this file */
#define SFF_RUNASREALUID 0x00000008L	/* if no ctladdr, run as real uid */
#define SFF_NOPATHCHECK	0x00000010L	/* don't bother checking dir path */
#define SFF_SETUIDOK	0x00000020L	/* set-user-ID files are ok */
#define SFF_CREAT	0x00000040L	/* ok to create file if necessary */
#define SFF_REGONLY	0x00000080L	/* regular files only */
#define SFF_SAFEDIRPATH	0x00000100L	/* no writable directories allowed */
#define SFF_NOHLINK	0x00000200L	/* file cannot have hard links */
#define SFF_NOWLINK	0x00000400L	/* links only in non-writable dirs */
#define SFF_NOGWFILES	0x00000800L	/* disallow world writable files */
#define SFF_NOWWFILES	0x00001000L	/* disallow group writable files */
#define SFF_OPENASROOT	0x00002000L	/* open as root instead of real user */
#define SFF_NOLOCK	0x00004000L	/* don't lock the file */
#define SFF_NOGRFILES	0x00008000L	/* disallow g readable files */
#define SFF_NOWRFILES	0x00010000L	/* disallow o readable files */
#define SFF_NOTEXCL	0x00020000L	/* creates don't need to be exclusive */
#define SFF_EXECOK	0x00040000L	/* executable files are ok (E_SM_ISEXEC) */
#define SFF_NBLOCK	0x00080000L	/* use a non-blocking lock */
#define SFF_NORFILES	(SFF_NOGRFILES|SFF_NOWRFILES)

/* pseudo-flags */
#define SFF_NOLINK	(SFF_NOHLINK|SFF_NOSLINK)

/* functions */
extern int	safefile __P((char *, UID_T, GID_T, char *, long, int, struct stat *));
extern int	safedirpath __P((char *, UID_T, GID_T, char *, long, int, int));
extern int	safeopen __P((char *, int, int, long));
extern SM_FILE_T*safefopen __P((char *, int, int, long));
extern int	dfopen __P((char *, int, int, long));
extern bool	filechanged __P((char *, int, struct stat *));

/*
**  DontBlameSendmail options
**
**	Hopefully nobody uses these.
*/

#define DBS_SAFE					0
#define DBS_ASSUMESAFECHOWN				1
#define DBS_GROUPWRITABLEDIRPATHSAFE			2
#define DBS_GROUPWRITABLEFORWARDFILESAFE		3
#define DBS_GROUPWRITABLEINCLUDEFILESAFE		4
#define DBS_GROUPWRITABLEALIASFILE			5
#define DBS_WORLDWRITABLEALIASFILE			6
#define DBS_FORWARDFILEINUNSAFEDIRPATH			7
#define DBS_MAPINUNSAFEDIRPATH				8
#define DBS_LINKEDALIASFILEINWRITABLEDIR		9
#define DBS_LINKEDCLASSFILEINWRITABLEDIR		10
#define DBS_LINKEDFORWARDFILEINWRITABLEDIR		11
#define DBS_LINKEDINCLUDEFILEINWRITABLEDIR		12
#define DBS_LINKEDMAPINWRITABLEDIR			13
#define DBS_LINKEDSERVICESWITCHFILEINWRITABLEDIR	14
#define DBS_FILEDELIVERYTOHARDLINK			15
#define DBS_FILEDELIVERYTOSYMLINK			16
#define DBS_WRITEMAPTOHARDLINK				17
#define DBS_WRITEMAPTOSYMLINK				18
#define DBS_WRITESTATSTOHARDLINK			19
#define DBS_WRITESTATSTOSYMLINK				20
#define DBS_FORWARDFILEINGROUPWRITABLEDIRPATH		21
#define DBS_INCLUDEFILEINGROUPWRITABLEDIRPATH		22
#define DBS_CLASSFILEINUNSAFEDIRPATH			23
#define DBS_ERRORHEADERINUNSAFEDIRPATH			24
#define DBS_HELPFILEINUNSAFEDIRPATH			25
#define DBS_FORWARDFILEINUNSAFEDIRPATHSAFE		26
#define DBS_INCLUDEFILEINUNSAFEDIRPATHSAFE		27
#define DBS_RUNPROGRAMINUNSAFEDIRPATH			28
#define DBS_RUNWRITABLEPROGRAM				29
#define DBS_INCLUDEFILEINUNSAFEDIRPATH			30
#define DBS_NONROOTSAFEADDR				31
#define DBS_TRUSTSTICKYBIT				32
#define DBS_DONTWARNFORWARDFILEINUNSAFEDIRPATH		33
#define DBS_INSUFFICIENTENTROPY				34
#define DBS_GROUPREADABLESASLDBFILE			35
#define DBS_GROUPWRITABLESASLDBFILE			36
#define DBS_GROUPWRITABLEFORWARDFILE			37
#define DBS_GROUPWRITABLEINCLUDEFILE			38
#define DBS_WORLDWRITABLEFORWARDFILE			39
#define DBS_WORLDWRITABLEINCLUDEFILE			40
#define DBS_GROUPREADABLEKEYFILE			41
#define DBS_GROUPREADABLEAUTHINFOFILE			42

/* struct defining such things */
struct dbsval
{
	char		*dbs_name;	/* name of DontBlameSendmail flag */
	unsigned char	dbs_flag;	/* numeric level */
};

/* Flags for submitmode */
#define SUBMIT_UNKNOWN	0x0000	/* unknown agent type */
#define SUBMIT_MTA	0x0001	/* act like a message transfer agent */
#define SUBMIT_MSA	0x0002	/* act like a message submission agent */


/*
 * Copyright (C) 1984-2017  Mark Nudelman
 *
 * You may distribute under the terms of either the GNU General Public
 * License or the Less License, as specified in the README file.
 *
 * For more information, see the README file.
 */


/*
 * Macros to define the method of doing filename "globbing".
 * There are three possible mechanisms:
 *   1.	GLOB_LIST
 *	This defines a function that returns a list of matching filenames.
 *   2. GLOB_NAME
 *	This defines a function that steps thru the list of matching
 *	filenames, returning one name each time it is called.
 *   3. GLOB_STRING
 *	This defines a function that returns the complete list of
 *	matching filenames as a single space-separated string.
 */

#if OS2

#define	DECL_GLOB_LIST(list)		char **list;  char **pp;
#define	GLOB_LIST(filename,list)	list = _fnexplode(filename)
#define	GLOB_LIST_FAILED(list)		list == NULL
#define	SCAN_GLOB_LIST(list,p)		pp = list;  *pp != NULL;  pp++
#define	INIT_GLOB_LIST(list,p)		p = *pp
#define	GLOB_LIST_DONE(list)		_fnexplodefree(list)

#else
#if MSDOS_COMPILER==DJGPPC

#define	DECL_GLOB_LIST(list)		glob_t list;  int i;
#define	GLOB_LIST(filename,list)	glob(filename,GLOB_NOCHECK,0,&list)
#define	GLOB_LIST_FAILED(list)		0
#define	SCAN_GLOB_LIST(list,p)		i = 0;  i < list.gl_pathc;  i++
#define	INIT_GLOB_LIST(list,p)		p = list.gl_pathv[i]
#define	GLOB_LIST_DONE(list)		globfree(&list)

#else
#if MSDOS_COMPILER==MSOFTC || MSDOS_COMPILER==BORLANDC

#define	GLOB_FIRST_NAME(filename,fndp,h) h = _dos_findfirst(filename, ~_A_VOLID, fndp)
#define	GLOB_FIRST_FAILED(handle)	((handle) != 0)
#define	GLOB_NEXT_NAME(handle,fndp)		_dos_findnext(fndp)
#define	GLOB_NAME_DONE(handle)
#define	GLOB_NAME			name
#define	DECL_GLOB_NAME(fnd,drive,dir,fname,ext,handle) \
					struct find_t fnd;	\
					char drive[_MAX_DRIVE];	\
					char dir[_MAX_DIR];	\
					char fname[_MAX_FNAME];	\
					char ext[_MAX_EXT];	\
					int handle;
#else
#if MSDOS_COMPILER==WIN32C && defined(_MSC_VER)

#define	GLOB_FIRST_NAME(filename,fndp,h) h = _findfirst(filename, fndp)
#define	GLOB_FIRST_FAILED(handle)	((handle) == -1)
#define	GLOB_NEXT_NAME(handle,fndp)	_findnext(handle, fndp)
#define	GLOB_NAME_DONE(handle)		_findclose(handle)
#define	GLOB_NAME			name
#define	DECL_GLOB_NAME(fnd,drive,dir,fname,ext,handle) \
					struct _finddata_t fnd;	\
					char drive[_MAX_DRIVE];	\
					char dir[_MAX_DIR];	\
					char fname[_MAX_FNAME];	\
					char ext[_MAX_EXT];	\
					long handle;

#else
#if MSDOS_COMPILER==WIN32C && !defined(_MSC_VER) /* Borland C for Windows */

#define	GLOB_FIRST_NAME(filename,fndp,h) h = findfirst(filename, fndp, ~FA_LABEL)
#define	GLOB_FIRST_FAILED(handle)	((handle) != 0)
#define	GLOB_NEXT_NAME(handle,fndp)	findnext(fndp)
#define	GLOB_NAME_DONE(handle)
#define	GLOB_NAME			ff_name
#define	DECL_GLOB_NAME(fnd,drive,dir,fname,ext,handle) \
					struct ffblk fnd;	\
					char drive[MAXDRIVE];	\
					char dir[MAXDIR];	\
					char fname[MAXFILE];	\
					char ext[MAXEXT];	\
					int handle;

#endif
#endif
#endif
#endif
#endif

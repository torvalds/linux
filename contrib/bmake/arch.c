/*	$NetBSD: arch.c,v 1.70 2017/04/16 20:49:09 riastradh Exp $	*/

/*
 * Copyright (c) 1988, 1989, 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Adam de Boor.
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

/*
 * Copyright (c) 1989 by Berkeley Softworks
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Adam de Boor.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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

#ifndef MAKE_NATIVE
static char rcsid[] = "$NetBSD: arch.c,v 1.70 2017/04/16 20:49:09 riastradh Exp $";
#else
#include <sys/cdefs.h>
#ifndef lint
#if 0
static char sccsid[] = "@(#)arch.c	8.2 (Berkeley) 1/2/94";
#else
__RCSID("$NetBSD: arch.c,v 1.70 2017/04/16 20:49:09 riastradh Exp $");
#endif
#endif /* not lint */
#endif

/*-
 * arch.c --
 *	Functions to manipulate libraries, archives and their members.
 *
 *	Once again, cacheing/hashing comes into play in the manipulation
 * of archives. The first time an archive is referenced, all of its members'
 * headers are read and hashed and the archive closed again. All hashed
 * archives are kept on a list which is searched each time an archive member
 * is referenced.
 *
 * The interface to this module is:
 *	Arch_ParseArchive   	Given an archive specification, return a list
 *	    	  	    	of GNode's, one for each member in the spec.
 *	    	  	    	FAILURE is returned if the specification is
 *	    	  	    	invalid for some reason.
 *
 *	Arch_Touch	    	Alter the modification time of the archive
 *	    	  	    	member described by the given node to be
 *	    	  	    	the current time.
 *
 *	Arch_TouchLib	    	Update the modification time of the library
 *	    	  	    	described by the given node. This is special
 *	    	  	    	because it also updates the modification time
 *	    	  	    	of the library's table of contents.
 *
 *	Arch_MTime	    	Find the modification time of a member of
 *	    	  	    	an archive *in the archive*. The time is also
 *	    	  	    	placed in the member's GNode. Returns the
 *	    	  	    	modification time.
 *
 *	Arch_MemTime	    	Find the modification time of a member of
 *	    	  	    	an archive. Called when the member doesn't
 *	    	  	    	already exist. Looks in the archive for the
 *	    	  	    	modification time. Returns the modification
 *	    	  	    	time.
 *
 *	Arch_FindLib	    	Search for a library along a path. The
 *	    	  	    	library name in the GNode should be in
 *	    	  	    	-l<name> format.
 *
 *	Arch_LibOODate	    	Special function to decide if a library node
 *	    	  	    	is out-of-date.
 *
 *	Arch_Init 	    	Initialize this module.
 *
 *	Arch_End 	    	Cleanup this module.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include    <sys/types.h>
#include    <sys/stat.h>
#include    <sys/time.h>
#include    <sys/param.h>
#ifdef HAVE_AR_H
#include    <ar.h>
#else
struct ar_hdr {
        char ar_name[16];               /* name */
        char ar_date[12];               /* modification time */
        char ar_uid[6];                 /* user id */
        char ar_gid[6];                 /* group id */
        char ar_mode[8];                /* octal file permissions */
        char ar_size[10];               /* size in bytes */
#ifndef ARFMAG
#define ARFMAG  "`\n"
#endif
        char ar_fmag[2];                /* consistency check */
};
#endif
#if defined(HAVE_RANLIB_H) && !(defined(__ELF__) || defined(NO_RANLIB))
#include    <ranlib.h>
#endif
#include    <stdio.h>
#include    <stdlib.h>
#ifdef HAVE_UTIME_H
#include    <utime.h>
#endif

#include    "make.h"
#include    "hash.h"
#include    "dir.h"

#ifdef TARGET_MACHINE
#undef MAKE_MACHINE
#define MAKE_MACHINE TARGET_MACHINE
#endif
#ifdef TARGET_MACHINE_ARCH
#undef MAKE_MACHINE_ARCH
#define MAKE_MACHINE_ARCH TARGET_MACHINE_ARCH
#endif

static Lst	  archives;   /* Lst of archives we've already examined */

typedef struct Arch {
    char	  *name;      /* Name of archive */
    Hash_Table	  members;    /* All the members of the archive described
			       * by <name, struct ar_hdr *> key/value pairs */
    char	  *fnametab;  /* Extended name table strings */
    size_t	  fnamesize;  /* Size of the string table */
} Arch;

static int ArchFindArchive(const void *, const void *);
#ifdef CLEANUP
static void ArchFree(void *);
#endif
static struct ar_hdr *ArchStatMember(char *, char *, Boolean);
static FILE *ArchFindMember(char *, char *, struct ar_hdr *, const char *);
#if defined(__svr4__) || defined(__SVR4) || defined(__ELF__)
#define SVR4ARCHIVES
static int ArchSVR4Entry(Arch *, char *, size_t, FILE *);
#endif


#if defined(_AIX)
# define AR_NAME _ar_name.ar_name
# define AR_FMAG _ar_name.ar_fmag
# define SARMAG  SAIAMAG
# define ARMAG   AIAMAG
# define ARFMAG  AIAFMAG
#endif
#ifndef  AR_NAME
# define AR_NAME ar_name
#endif
#ifndef  AR_DATE
# define AR_DATE ar_date
#endif
#ifndef  AR_SIZE
# define AR_SIZE ar_size
#endif
#ifndef  AR_FMAG
# define AR_FMAG ar_fmag
#endif
#ifndef ARMAG
# define ARMAG	"!<arch>\n"
#endif
#ifndef SARMAG
# define SARMAG	8
#endif

#define AR_MAX_NAME_LEN	    (sizeof(arh.AR_NAME)-1)

#ifdef CLEANUP
/*-
 *-----------------------------------------------------------------------
 * ArchFree --
 *	Free memory used by an archive
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	None.
 *
 *-----------------------------------------------------------------------
 */
static void
ArchFree(void *ap)
{
    Arch *a = (Arch *)ap;
    Hash_Search	  search;
    Hash_Entry	  *entry;

    /* Free memory from hash entries */
    for (entry = Hash_EnumFirst(&a->members, &search);
	 entry != NULL;
	 entry = Hash_EnumNext(&search))
	free(Hash_GetValue(entry));

    free(a->name);
    free(a->fnametab);
    Hash_DeleteTable(&a->members);
    free(a);
}
#endif


/*-
 *-----------------------------------------------------------------------
 * Arch_ParseArchive --
 *	Parse the archive specification in the given line and find/create
 *	the nodes for the specified archive members, placing their nodes
 *	on the given list.
 *
 * Input:
 *	linePtr		Pointer to start of specification
 *	nodeLst		Lst on which to place the nodes
 *	ctxt		Context in which to expand variables
 *
 * Results:
 *	SUCCESS if it was a valid specification. The linePtr is updated
 *	to point to the first non-space after the archive spec. The
 *	nodes for the members are placed on the given list.
 *
 * Side Effects:
 *	Some nodes may be created. The given list is extended.
 *
 *-----------------------------------------------------------------------
 */
ReturnStatus
Arch_ParseArchive(char **linePtr, Lst nodeLst, GNode *ctxt)
{
    char	    *cp;	    /* Pointer into line */
    GNode	    *gn;     	    /* New node */
    char	    *libName;  	    /* Library-part of specification */
    char	    *memName;  	    /* Member-part of specification */
    char	    *nameBuf;	    /* temporary place for node name */
    char	    saveChar;  	    /* Ending delimiter of member-name */
    Boolean 	    subLibName;	    /* TRUE if libName should have/had
				     * variable substitution performed on it */

    libName = *linePtr;

    subLibName = FALSE;

    for (cp = libName; *cp != '(' && *cp != '\0'; cp++) {
	if (*cp == '$') {
	    /*
	     * Variable spec, so call the Var module to parse the puppy
	     * so we can safely advance beyond it...
	     */
	    int 	length;
	    void	*freeIt;
	    char	*result;

	    result = Var_Parse(cp, ctxt, VARF_UNDEFERR|VARF_WANTRES,
			       &length, &freeIt);
	    free(freeIt);

	    if (result == var_Error) {
		return(FAILURE);
	    } else {
		subLibName = TRUE;
	    }

	    cp += length-1;
	}
    }

    *cp++ = '\0';
    if (subLibName) {
	libName = Var_Subst(NULL, libName, ctxt, VARF_UNDEFERR|VARF_WANTRES);
    }


    for (;;) {
	/*
	 * First skip to the start of the member's name, mark that
	 * place and skip to the end of it (either white-space or
	 * a close paren).
	 */
	Boolean	doSubst = FALSE; /* TRUE if need to substitute in memName */

	while (*cp != '\0' && *cp != ')' && isspace ((unsigned char)*cp)) {
	    cp++;
	}
	memName = cp;
	while (*cp != '\0' && *cp != ')' && !isspace ((unsigned char)*cp)) {
	    if (*cp == '$') {
		/*
		 * Variable spec, so call the Var module to parse the puppy
		 * so we can safely advance beyond it...
		 */
		int 	length;
		void	*freeIt;
		char	*result;

		result = Var_Parse(cp, ctxt, VARF_UNDEFERR|VARF_WANTRES,
				   &length, &freeIt);
		free(freeIt);

		if (result == var_Error) {
		    return(FAILURE);
		} else {
		    doSubst = TRUE;
		}

		cp += length;
	    } else {
		cp++;
	    }
	}

	/*
	 * If the specification ends without a closing parenthesis,
	 * chances are there's something wrong (like a missing backslash),
	 * so it's better to return failure than allow such things to happen
	 */
	if (*cp == '\0') {
	    printf("No closing parenthesis in archive specification\n");
	    return (FAILURE);
	}

	/*
	 * If we didn't move anywhere, we must be done
	 */
	if (cp == memName) {
	    break;
	}

	saveChar = *cp;
	*cp = '\0';

	/*
	 * XXX: This should be taken care of intelligently by
	 * SuffExpandChildren, both for the archive and the member portions.
	 */
	/*
	 * If member contains variables, try and substitute for them.
	 * This will slow down archive specs with dynamic sources, of course,
	 * since we'll be (non-)substituting them three times, but them's
	 * the breaks -- we need to do this since SuffExpandChildren calls
	 * us, otherwise we could assume the thing would be taken care of
	 * later.
	 */
	if (doSubst) {
	    char    *buf;
	    char    *sacrifice;
	    char    *oldMemName = memName;
	    size_t   sz;

	    memName = Var_Subst(NULL, memName, ctxt,
				VARF_UNDEFERR|VARF_WANTRES);

	    /*
	     * Now form an archive spec and recurse to deal with nested
	     * variables and multi-word variable values.... The results
	     * are just placed at the end of the nodeLst we're returning.
	     */
	    sz = strlen(memName)+strlen(libName)+3;
	    buf = sacrifice = bmake_malloc(sz);

	    snprintf(buf, sz, "%s(%s)", libName, memName);

	    if (strchr(memName, '$') && strcmp(memName, oldMemName) == 0) {
		/*
		 * Must contain dynamic sources, so we can't deal with it now.
		 * Just create an ARCHV node for the thing and let
		 * SuffExpandChildren handle it...
		 */
		gn = Targ_FindNode(buf, TARG_CREATE);

		if (gn == NULL) {
		    free(buf);
		    return(FAILURE);
		} else {
		    gn->type |= OP_ARCHV;
		    (void)Lst_AtEnd(nodeLst, gn);
		}
	    } else if (Arch_ParseArchive(&sacrifice, nodeLst, ctxt)!=SUCCESS) {
		/*
		 * Error in nested call -- free buffer and return FAILURE
		 * ourselves.
		 */
		free(buf);
		return(FAILURE);
	    }
	    /*
	     * Free buffer and continue with our work.
	     */
	    free(buf);
	} else if (Dir_HasWildcards(memName)) {
	    Lst	  members = Lst_Init(FALSE);
	    char  *member;
	    size_t sz = MAXPATHLEN, nsz;
	    nameBuf = bmake_malloc(sz);

	    Dir_Expand(memName, dirSearchPath, members);
	    while (!Lst_IsEmpty(members)) {
		member = (char *)Lst_DeQueue(members);
		nsz = strlen(libName) + strlen(member) + 3;
		if (sz > nsz)
		    nameBuf = bmake_realloc(nameBuf, sz = nsz * 2);

		snprintf(nameBuf, sz, "%s(%s)", libName, member);
		free(member);
		gn = Targ_FindNode(nameBuf, TARG_CREATE);
		if (gn == NULL) {
		    free(nameBuf);
		    return (FAILURE);
		} else {
		    /*
		     * We've found the node, but have to make sure the rest of
		     * the world knows it's an archive member, without having
		     * to constantly check for parentheses, so we type the
		     * thing with the OP_ARCHV bit before we place it on the
		     * end of the provided list.
		     */
		    gn->type |= OP_ARCHV;
		    (void)Lst_AtEnd(nodeLst, gn);
		}
	    }
	    Lst_Destroy(members, NULL);
	    free(nameBuf);
	} else {
	    size_t	sz = strlen(libName) + strlen(memName) + 3;
	    nameBuf = bmake_malloc(sz);
	    snprintf(nameBuf, sz, "%s(%s)", libName, memName);
	    gn = Targ_FindNode(nameBuf, TARG_CREATE);
	    free(nameBuf);
	    if (gn == NULL) {
		return (FAILURE);
	    } else {
		/*
		 * We've found the node, but have to make sure the rest of the
		 * world knows it's an archive member, without having to
		 * constantly check for parentheses, so we type the thing with
		 * the OP_ARCHV bit before we place it on the end of the
		 * provided list.
		 */
		gn->type |= OP_ARCHV;
		(void)Lst_AtEnd(nodeLst, gn);
	    }
	}
	if (doSubst) {
	    free(memName);
	}

	*cp = saveChar;
    }

    /*
     * If substituted libName, free it now, since we need it no longer.
     */
    if (subLibName) {
	free(libName);
    }

    /*
     * We promised the pointer would be set up at the next non-space, so
     * we must advance cp there before setting *linePtr... (note that on
     * entrance to the loop, cp is guaranteed to point at a ')')
     */
    do {
	cp++;
    } while (*cp != '\0' && isspace ((unsigned char)*cp));

    *linePtr = cp;
    return (SUCCESS);
}

/*-
 *-----------------------------------------------------------------------
 * ArchFindArchive --
 *	See if the given archive is the one we are looking for. Called
 *	From ArchStatMember and ArchFindMember via Lst_Find.
 *
 * Input:
 *	ar		Current list element
 *	archName	Name we want
 *
 * Results:
 *	0 if it is, non-zero if it isn't.
 *
 * Side Effects:
 *	None.
 *
 *-----------------------------------------------------------------------
 */
static int
ArchFindArchive(const void *ar, const void *archName)
{
    return (strcmp(archName, ((const Arch *)ar)->name));
}

/*-
 *-----------------------------------------------------------------------
 * ArchStatMember --
 *	Locate a member of an archive, given the path of the archive and
 *	the path of the desired member.
 *
 * Input:
 *	archive		Path to the archive
 *	member		Name of member. If it is a path, only the last
 *			component is used.
 *	hash		TRUE if archive should be hashed if not already so.
 *
 * Results:
 *	A pointer to the current struct ar_hdr structure for the member. Note
 *	That no position is returned, so this is not useful for touching
 *	archive members. This is mostly because we have no assurances that
 *	The archive will remain constant after we read all the headers, so
 *	there's not much point in remembering the position...
 *
 * Side Effects:
 *
 *-----------------------------------------------------------------------
 */
static struct ar_hdr *
ArchStatMember(char *archive, char *member, Boolean hash)
{
    FILE *	  arch;	      /* Stream to archive */
    int		  size;       /* Size of archive member */
    char	  *cp;	      /* Useful character pointer */
    char	  magic[SARMAG];
    LstNode	  ln;	      /* Lst member containing archive descriptor */
    Arch	  *ar;	      /* Archive descriptor */
    Hash_Entry	  *he;	      /* Entry containing member's description */
    struct ar_hdr arh;        /* archive-member header for reading archive */
    char	  memName[MAXPATHLEN+1];
    	    	    	    /* Current member name while hashing. */

    /*
     * Because of space constraints and similar things, files are archived
     * using their final path components, not the entire thing, so we need
     * to point 'member' to the final component, if there is one, to make
     * the comparisons easier...
     */
    cp = strrchr(member, '/');
    if (cp != NULL) {
	member = cp + 1;
    }

    ln = Lst_Find(archives, archive, ArchFindArchive);
    if (ln != NULL) {
	ar = (Arch *)Lst_Datum(ln);

	he = Hash_FindEntry(&ar->members, member);

	if (he != NULL) {
	    return ((struct ar_hdr *)Hash_GetValue(he));
	} else {
	    /* Try truncated name */
	    char copy[AR_MAX_NAME_LEN+1];
	    size_t len = strlen(member);

	    if (len > AR_MAX_NAME_LEN) {
		len = AR_MAX_NAME_LEN;
		strncpy(copy, member, AR_MAX_NAME_LEN);
		copy[AR_MAX_NAME_LEN] = '\0';
	    }
	    if ((he = Hash_FindEntry(&ar->members, copy)) != NULL)
		return ((struct ar_hdr *)Hash_GetValue(he));
	    return NULL;
	}
    }

    if (!hash) {
	/*
	 * Caller doesn't want the thing hashed, just use ArchFindMember
	 * to read the header for the member out and close down the stream
	 * again. Since the archive is not to be hashed, we assume there's
	 * no need to allocate extra room for the header we're returning,
	 * so just declare it static.
	 */
	 static struct ar_hdr	sarh;

	 arch = ArchFindMember(archive, member, &sarh, "r");

	 if (arch == NULL) {
	    return NULL;
	} else {
	    fclose(arch);
	    return (&sarh);
	}
    }

    /*
     * We don't have this archive on the list yet, so we want to find out
     * everything that's in it and cache it so we can get at it quickly.
     */
    arch = fopen(archive, "r");
    if (arch == NULL) {
	return NULL;
    }

    /*
     * We use the ARMAG string to make sure this is an archive we
     * can handle...
     */
    if ((fread(magic, SARMAG, 1, arch) != 1) ||
    	(strncmp(magic, ARMAG, SARMAG) != 0)) {
	    fclose(arch);
	    return NULL;
    }

    ar = bmake_malloc(sizeof(Arch));
    ar->name = bmake_strdup(archive);
    ar->fnametab = NULL;
    ar->fnamesize = 0;
    Hash_InitTable(&ar->members, -1);
    memName[AR_MAX_NAME_LEN] = '\0';

    while (fread((char *)&arh, sizeof(struct ar_hdr), 1, arch) == 1) {
	if (strncmp( arh.AR_FMAG, ARFMAG, sizeof(arh.AR_FMAG)) != 0) {
	    /*
	     * The header is bogus, so the archive is bad
	     * and there's no way we can recover...
	     */
	    goto badarch;
	} else {
	    /*
	     * We need to advance the stream's pointer to the start of the
	     * next header. Files are padded with newlines to an even-byte
	     * boundary, so we need to extract the size of the file from the
	     * 'size' field of the header and round it up during the seek.
	     */
	    arh.AR_SIZE[sizeof(arh.AR_SIZE)-1] = '\0';
	    size = (int)strtol(arh.AR_SIZE, NULL, 10);

	    (void)strncpy(memName, arh.AR_NAME, sizeof(arh.AR_NAME));
	    for (cp = &memName[AR_MAX_NAME_LEN]; *cp == ' '; cp--) {
		continue;
	    }
	    cp[1] = '\0';

#ifdef SVR4ARCHIVES
	    /*
	     * svr4 names are slash terminated. Also svr4 extended AR format.
	     */
	    if (memName[0] == '/') {
		/*
		 * svr4 magic mode; handle it
		 */
		switch (ArchSVR4Entry(ar, memName, size, arch)) {
		case -1:  /* Invalid data */
		    goto badarch;
		case 0:	  /* List of files entry */
		    continue;
		default:  /* Got the entry */
		    break;
		}
	    }
	    else {
		if (cp[0] == '/')
		    cp[0] = '\0';
	    }
#endif

#ifdef AR_EFMT1
	    /*
	     * BSD 4.4 extended AR format: #1/<namelen>, with name as the
	     * first <namelen> bytes of the file
	     */
	    if (strncmp(memName, AR_EFMT1, sizeof(AR_EFMT1) - 1) == 0 &&
		isdigit((unsigned char)memName[sizeof(AR_EFMT1) - 1])) {

		unsigned int elen = atoi(&memName[sizeof(AR_EFMT1)-1]);

		if (elen > MAXPATHLEN)
			goto badarch;
		if (fread(memName, elen, 1, arch) != 1)
			goto badarch;
		memName[elen] = '\0';
		if (fseek(arch, -elen, SEEK_CUR) != 0)
			goto badarch;
		if (DEBUG(ARCH) || DEBUG(MAKE)) {
		    fprintf(debug_file, "ArchStat: Extended format entry for %s\n", memName);
		}
	    }
#endif

	    he = Hash_CreateEntry(&ar->members, memName, NULL);
	    Hash_SetValue(he, bmake_malloc(sizeof(struct ar_hdr)));
	    memcpy(Hash_GetValue(he), &arh, sizeof(struct ar_hdr));
	}
	if (fseek(arch, (size + 1) & ~1, SEEK_CUR) != 0)
	    goto badarch;
    }

    fclose(arch);

    (void)Lst_AtEnd(archives, ar);

    /*
     * Now that the archive has been read and cached, we can look into
     * the hash table to find the desired member's header.
     */
    he = Hash_FindEntry(&ar->members, member);

    if (he != NULL) {
	return ((struct ar_hdr *)Hash_GetValue(he));
    } else {
	return NULL;
    }

badarch:
    fclose(arch);
    Hash_DeleteTable(&ar->members);
    free(ar->fnametab);
    free(ar);
    return NULL;
}

#ifdef SVR4ARCHIVES
/*-
 *-----------------------------------------------------------------------
 * ArchSVR4Entry --
 *	Parse an SVR4 style entry that begins with a slash.
 *	If it is "//", then load the table of filenames
 *	If it is "/<offset>", then try to substitute the long file name
 *	from offset of a table previously read.
 *
 * Results:
 *	-1: Bad data in archive
 *	 0: A table was loaded from the file
 *	 1: Name was successfully substituted from table
 *	 2: Name was not successfully substituted from table
 *
 * Side Effects:
 *	If a table is read, the file pointer is moved to the next archive
 *	member
 *
 *-----------------------------------------------------------------------
 */
static int
ArchSVR4Entry(Arch *ar, char *name, size_t size, FILE *arch)
{
#define ARLONGNAMES1 "//"
#define ARLONGNAMES2 "/ARFILENAMES"
    size_t entry;
    char *ptr, *eptr;

    if (strncmp(name, ARLONGNAMES1, sizeof(ARLONGNAMES1) - 1) == 0 ||
	strncmp(name, ARLONGNAMES2, sizeof(ARLONGNAMES2) - 1) == 0) {

	if (ar->fnametab != NULL) {
	    if (DEBUG(ARCH)) {
		fprintf(debug_file, "Attempted to redefine an SVR4 name table\n");
	    }
	    return -1;
	}

	/*
	 * This is a table of archive names, so we build one for
	 * ourselves
	 */
	ar->fnametab = bmake_malloc(size);
	ar->fnamesize = size;

	if (fread(ar->fnametab, size, 1, arch) != 1) {
	    if (DEBUG(ARCH)) {
		fprintf(debug_file, "Reading an SVR4 name table failed\n");
	    }
	    return -1;
	}
	eptr = ar->fnametab + size;
	for (entry = 0, ptr = ar->fnametab; ptr < eptr; ptr++)
	    switch (*ptr) {
	    case '/':
		entry++;
		*ptr = '\0';
		break;

	    case '\n':
		break;

	    default:
		break;
	    }
	if (DEBUG(ARCH)) {
	    fprintf(debug_file, "Found svr4 archive name table with %lu entries\n",
	            (unsigned long)entry);
	}
	return 0;
    }

    if (name[1] == ' ' || name[1] == '\0')
	return 2;

    entry = (size_t)strtol(&name[1], &eptr, 0);
    if ((*eptr != ' ' && *eptr != '\0') || eptr == &name[1]) {
	if (DEBUG(ARCH)) {
	    fprintf(debug_file, "Could not parse SVR4 name %s\n", name);
	}
	return 2;
    }
    if (entry >= ar->fnamesize) {
	if (DEBUG(ARCH)) {
	    fprintf(debug_file, "SVR4 entry offset %s is greater than %lu\n",
		   name, (unsigned long)ar->fnamesize);
	}
	return 2;
    }

    if (DEBUG(ARCH)) {
	fprintf(debug_file, "Replaced %s with %s\n", name, &ar->fnametab[entry]);
    }

    (void)strncpy(name, &ar->fnametab[entry], MAXPATHLEN);
    name[MAXPATHLEN] = '\0';
    return 1;
}
#endif


/*-
 *-----------------------------------------------------------------------
 * ArchFindMember --
 *	Locate a member of an archive, given the path of the archive and
 *	the path of the desired member. If the archive is to be modified,
 *	the mode should be "r+", if not, it should be "r".
 *
 * Input:
 *	archive		Path to the archive
 *	member		Name of member. If it is a path, only the last
 *			component is used.
 *	arhPtr		Pointer to header structure to be filled in
 *	mode		The mode for opening the stream
 *
 * Results:
 *	An FILE *, opened for reading and writing, positioned at the
 *	start of the member's struct ar_hdr, or NULL if the member was
 *	nonexistent. The current struct ar_hdr for member.
 *
 * Side Effects:
 *	The passed struct ar_hdr structure is filled in.
 *
 *-----------------------------------------------------------------------
 */
static FILE *
ArchFindMember(char *archive, char *member, struct ar_hdr *arhPtr,
    const char *mode)
{
    FILE *	  arch;	      /* Stream to archive */
    int		  size;       /* Size of archive member */
    char	  *cp;	      /* Useful character pointer */
    char	  magic[SARMAG];
    size_t	  len, tlen;

    arch = fopen(archive, mode);
    if (arch == NULL) {
	return NULL;
    }

    /*
     * We use the ARMAG string to make sure this is an archive we
     * can handle...
     */
    if ((fread(magic, SARMAG, 1, arch) != 1) ||
    	(strncmp(magic, ARMAG, SARMAG) != 0)) {
	    fclose(arch);
	    return NULL;
    }

    /*
     * Because of space constraints and similar things, files are archived
     * using their final path components, not the entire thing, so we need
     * to point 'member' to the final component, if there is one, to make
     * the comparisons easier...
     */
    cp = strrchr(member, '/');
    if (cp != NULL) {
	member = cp + 1;
    }
    len = tlen = strlen(member);
    if (len > sizeof(arhPtr->AR_NAME)) {
	tlen = sizeof(arhPtr->AR_NAME);
    }

    while (fread((char *)arhPtr, sizeof(struct ar_hdr), 1, arch) == 1) {
	if (strncmp(arhPtr->AR_FMAG, ARFMAG, sizeof(arhPtr->AR_FMAG) ) != 0) {
	     /*
	      * The header is bogus, so the archive is bad
	      * and there's no way we can recover...
	      */
	     fclose(arch);
	     return NULL;
	} else if (strncmp(member, arhPtr->AR_NAME, tlen) == 0) {
	    /*
	     * If the member's name doesn't take up the entire 'name' field,
	     * we have to be careful of matching prefixes. Names are space-
	     * padded to the right, so if the character in 'name' at the end
	     * of the matched string is anything but a space, this isn't the
	     * member we sought.
	     */
	    if (tlen != sizeof(arhPtr->AR_NAME) && arhPtr->AR_NAME[tlen] != ' '){
		goto skip;
	    } else {
		/*
		 * To make life easier, we reposition the file at the start
		 * of the header we just read before we return the stream.
		 * In a more general situation, it might be better to leave
		 * the file at the actual member, rather than its header, but
		 * not here...
		 */
		if (fseek(arch, -sizeof(struct ar_hdr), SEEK_CUR) != 0) {
		    fclose(arch);
		    return NULL;
		}
		return (arch);
	    }
	} else
#ifdef AR_EFMT1
		/*
		 * BSD 4.4 extended AR format: #1/<namelen>, with name as the
		 * first <namelen> bytes of the file
		 */
	    if (strncmp(arhPtr->AR_NAME, AR_EFMT1,
					sizeof(AR_EFMT1) - 1) == 0 &&
		isdigit((unsigned char)arhPtr->AR_NAME[sizeof(AR_EFMT1) - 1])) {

		unsigned int elen = atoi(&arhPtr->AR_NAME[sizeof(AR_EFMT1)-1]);
		char ename[MAXPATHLEN + 1];

		if (elen > MAXPATHLEN) {
			fclose(arch);
			return NULL;
		}
		if (fread(ename, elen, 1, arch) != 1) {
			fclose(arch);
			return NULL;
		}
		ename[elen] = '\0';
		if (DEBUG(ARCH) || DEBUG(MAKE)) {
		    fprintf(debug_file, "ArchFind: Extended format entry for %s\n", ename);
		}
		if (strncmp(ename, member, len) == 0) {
			/* Found as extended name */
			if (fseek(arch, -sizeof(struct ar_hdr) - elen,
				SEEK_CUR) != 0) {
			    fclose(arch);
			    return NULL;
			}
			return (arch);
		}
		if (fseek(arch, -elen, SEEK_CUR) != 0) {
		    fclose(arch);
		    return NULL;
		}
		goto skip;
	} else
#endif
	{
skip:
	    /*
	     * This isn't the member we're after, so we need to advance the
	     * stream's pointer to the start of the next header. Files are
	     * padded with newlines to an even-byte boundary, so we need to
	     * extract the size of the file from the 'size' field of the
	     * header and round it up during the seek.
	     */
	    arhPtr->AR_SIZE[sizeof(arhPtr->AR_SIZE)-1] = '\0';
	    size = (int)strtol(arhPtr->AR_SIZE, NULL, 10);
	    if (fseek(arch, (size + 1) & ~1, SEEK_CUR) != 0) {
		fclose(arch);
		return NULL;
	    }
	}
    }

    /*
     * We've looked everywhere, but the member is not to be found. Close the
     * archive and return NULL -- an error.
     */
    fclose(arch);
    return NULL;
}

/*-
 *-----------------------------------------------------------------------
 * Arch_Touch --
 *	Touch a member of an archive.
 *
 * Input:
 *	gn		Node of member to touch
 *
 * Results:
 *	The 'time' field of the member's header is updated.
 *
 * Side Effects:
 *	The modification time of the entire archive is also changed.
 *	For a library, this could necessitate the re-ranlib'ing of the
 *	whole thing.
 *
 *-----------------------------------------------------------------------
 */
void
Arch_Touch(GNode *gn)
{
    FILE *	  arch;	  /* Stream open to archive, positioned properly */
    struct ar_hdr arh;	  /* Current header describing member */
    char *p1, *p2;

    arch = ArchFindMember(Var_Value(ARCHIVE, gn, &p1),
			  Var_Value(MEMBER, gn, &p2),
			  &arh, "r+");

    free(p1);
    free(p2);

    snprintf(arh.AR_DATE, sizeof(arh.AR_DATE), "%-12ld", (long) now);

    if (arch != NULL) {
	(void)fwrite((char *)&arh, sizeof(struct ar_hdr), 1, arch);
	fclose(arch);
    }
}

/*-
 *-----------------------------------------------------------------------
 * Arch_TouchLib --
 *	Given a node which represents a library, touch the thing, making
 *	sure that the table of contents also is touched.
 *
 * Input:
 *	gn		The node of the library to touch
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Both the modification time of the library and of the RANLIBMAG
 *	member are set to 'now'.
 *
 *-----------------------------------------------------------------------
 */
void
#if !defined(RANLIBMAG)
Arch_TouchLib(GNode *gn MAKE_ATTR_UNUSED)
#else
Arch_TouchLib(GNode *gn)
#endif
{
#ifdef RANLIBMAG
    FILE *	    arch;	/* Stream open to archive */
    struct ar_hdr   arh;      	/* Header describing table of contents */
    struct utimbuf  times;	/* Times for utime() call */

    arch = ArchFindMember(gn->path, UNCONST(RANLIBMAG), &arh, "r+");
    snprintf(arh.AR_DATE, sizeof(arh.AR_DATE), "%-12ld", (long) now);

    if (arch != NULL) {
	(void)fwrite((char *)&arh, sizeof(struct ar_hdr), 1, arch);
	fclose(arch);

	times.actime = times.modtime = now;
	utime(gn->path, &times);
    }
#endif
}

/*-
 *-----------------------------------------------------------------------
 * Arch_MTime --
 *	Return the modification time of a member of an archive.
 *
 * Input:
 *	gn		Node describing archive member
 *
 * Results:
 *	The modification time(seconds).
 *
 * Side Effects:
 *	The mtime field of the given node is filled in with the value
 *	returned by the function.
 *
 *-----------------------------------------------------------------------
 */
time_t
Arch_MTime(GNode *gn)
{
    struct ar_hdr *arhPtr;    /* Header of desired member */
    time_t	  modTime;    /* Modification time as an integer */
    char *p1, *p2;

    arhPtr = ArchStatMember(Var_Value(ARCHIVE, gn, &p1),
			     Var_Value(MEMBER, gn, &p2),
			     TRUE);

    free(p1);
    free(p2);

    if (arhPtr != NULL) {
	modTime = (time_t)strtol(arhPtr->AR_DATE, NULL, 10);
    } else {
	modTime = 0;
    }

    gn->mtime = modTime;
    return (modTime);
}

/*-
 *-----------------------------------------------------------------------
 * Arch_MemMTime --
 *	Given a non-existent archive member's node, get its modification
 *	time from its archived form, if it exists.
 *
 * Results:
 *	The modification time.
 *
 * Side Effects:
 *	The mtime field is filled in.
 *
 *-----------------------------------------------------------------------
 */
time_t
Arch_MemMTime(GNode *gn)
{
    LstNode 	  ln;
    GNode   	  *pgn;
    char    	  *nameStart,
		  *nameEnd;

    if (Lst_Open(gn->parents) != SUCCESS) {
	gn->mtime = 0;
	return (0);
    }
    while ((ln = Lst_Next(gn->parents)) != NULL) {
	pgn = (GNode *)Lst_Datum(ln);

	if (pgn->type & OP_ARCHV) {
	    /*
	     * If the parent is an archive specification and is being made
	     * and its member's name matches the name of the node we were
	     * given, record the modification time of the parent in the
	     * child. We keep searching its parents in case some other
	     * parent requires this child to exist...
	     */
	    nameStart = strchr(pgn->name, '(') + 1;
	    nameEnd = strchr(nameStart, ')');

	    if ((pgn->flags & REMAKE) &&
		strncmp(nameStart, gn->name, nameEnd - nameStart) == 0) {
				     gn->mtime = Arch_MTime(pgn);
	    }
	} else if (pgn->flags & REMAKE) {
	    /*
	     * Something which isn't a library depends on the existence of
	     * this target, so it needs to exist.
	     */
	    gn->mtime = 0;
	    break;
	}
    }

    Lst_Close(gn->parents);

    return (gn->mtime);
}

/*-
 *-----------------------------------------------------------------------
 * Arch_FindLib --
 *	Search for a library along the given search path.
 *
 * Input:
 *	gn		Node of library to find
 *	path		Search path
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	The node's 'path' field is set to the found path (including the
 *	actual file name, not -l...). If the system can handle the -L
 *	flag when linking (or we cannot find the library), we assume that
 *	the user has placed the .LIBRARIES variable in the final linking
 *	command (or the linker will know where to find it) and set the
 *	TARGET variable for this node to be the node's name. Otherwise,
 *	we set the TARGET variable to be the full path of the library,
 *	as returned by Dir_FindFile.
 *
 *-----------------------------------------------------------------------
 */
void
Arch_FindLib(GNode *gn, Lst path)
{
    char	    *libName;   /* file name for archive */
    size_t	     sz = strlen(gn->name) + 6 - 2;

    libName = bmake_malloc(sz);
    snprintf(libName, sz, "lib%s.a", &gn->name[2]);

    gn->path = Dir_FindFile(libName, path);

    free(libName);

#ifdef LIBRARIES
    Var_Set(TARGET, gn->name, gn, 0);
#else
    Var_Set(TARGET, gn->path == NULL ? gn->name : gn->path, gn, 0);
#endif /* LIBRARIES */
}

/*-
 *-----------------------------------------------------------------------
 * Arch_LibOODate --
 *	Decide if a node with the OP_LIB attribute is out-of-date. Called
 *	from Make_OODate to make its life easier.
 *
 *	There are several ways for a library to be out-of-date that are
 *	not available to ordinary files. In addition, there are ways
 *	that are open to regular files that are not available to
 *	libraries. A library that is only used as a source is never
 *	considered out-of-date by itself. This does not preclude the
 *	library's modification time from making its parent be out-of-date.
 *	A library will be considered out-of-date for any of these reasons,
 *	given that it is a target on a dependency line somewhere:
 *	    Its modification time is less than that of one of its
 *	    	  sources (gn->mtime < gn->cmgn->mtime).
 *	    Its modification time is greater than the time at which the
 *	    	  make began (i.e. it's been modified in the course
 *	    	  of the make, probably by archiving).
 *	    The modification time of one of its sources is greater than
 *		  the one of its RANLIBMAG member (i.e. its table of contents
 *	    	  is out-of-date). We don't compare of the archive time
 *		  vs. TOC time because they can be too close. In my
 *		  opinion we should not bother with the TOC at all since
 *		  this is used by 'ar' rules that affect the data contents
 *		  of the archive, not by ranlib rules, which affect the
 *		  TOC.
 *
 * Input:
 *	gn		The library's graph node
 *
 * Results:
 *	TRUE if the library is out-of-date. FALSE otherwise.
 *
 * Side Effects:
 *	The library will be hashed if it hasn't been already.
 *
 *-----------------------------------------------------------------------
 */
Boolean
Arch_LibOODate(GNode *gn)
{
    Boolean 	  oodate;

    if (gn->type & OP_PHONY) {
	oodate = TRUE;
    } else if (OP_NOP(gn->type) && Lst_IsEmpty(gn->children)) {
	oodate = FALSE;
    } else if ((!Lst_IsEmpty(gn->children) && gn->cmgn == NULL) ||
	       (gn->mtime > now) ||
	       (gn->cmgn != NULL && gn->mtime < gn->cmgn->mtime)) {
	oodate = TRUE;
    } else {
#ifdef RANLIBMAG
	struct ar_hdr  	*arhPtr;    /* Header for __.SYMDEF */
	int 	  	modTimeTOC; /* The table-of-contents's mod time */

	arhPtr = ArchStatMember(gn->path, UNCONST(RANLIBMAG), FALSE);

	if (arhPtr != NULL) {
	    modTimeTOC = (int)strtol(arhPtr->AR_DATE, NULL, 10);

	    if (DEBUG(ARCH) || DEBUG(MAKE)) {
		fprintf(debug_file, "%s modified %s...", RANLIBMAG, Targ_FmtTime(modTimeTOC));
	    }
	    oodate = (gn->cmgn == NULL || gn->cmgn->mtime > modTimeTOC);
	} else {
	    /*
	     * A library w/o a table of contents is out-of-date
	     */
	    if (DEBUG(ARCH) || DEBUG(MAKE)) {
		fprintf(debug_file, "No t.o.c....");
	    }
	    oodate = TRUE;
	}
#else
	oodate = FALSE;
#endif
    }
    return (oodate);
}

/*-
 *-----------------------------------------------------------------------
 * Arch_Init --
 *	Initialize things for this module.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	The 'archives' list is initialized.
 *
 *-----------------------------------------------------------------------
 */
void
Arch_Init(void)
{
    archives = Lst_Init(FALSE);
}



/*-
 *-----------------------------------------------------------------------
 * Arch_End --
 *	Cleanup things for this module.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	The 'archives' list is freed
 *
 *-----------------------------------------------------------------------
 */
void
Arch_End(void)
{
#ifdef CLEANUP
    Lst_Destroy(archives, ArchFree);
#endif
}

/*-
 *-----------------------------------------------------------------------
 * Arch_IsLib --
 *	Check if the node is a library
 *
 * Results:
 *	True or False.
 *
 * Side Effects:
 *	None.
 *
 *-----------------------------------------------------------------------
 */
int
Arch_IsLib(GNode *gn)
{
    static const char armag[] = "!<arch>\n";
    char buf[sizeof(armag)-1];
    int fd;

    if ((fd = open(gn->path, O_RDONLY)) == -1)
	return FALSE;

    if (read(fd, buf, sizeof(buf)) != sizeof(buf)) {
	(void)close(fd);
	return FALSE;
    }

    (void)close(fd);

    return memcmp(buf, armag, sizeof(buf)) == 0;
}

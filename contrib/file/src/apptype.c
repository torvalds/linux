/*
 * Adapted from: apptype.c, Written by Eberhard Mattes and put into the
 * public domain
 * 
 * Notes: 1. Qualify the filename so that DosQueryAppType does not do extraneous
 * searches.
 * 
 * 2. DosQueryAppType will return FAPPTYP_DOS on a file ending with ".com"
 * (other than an OS/2 exe or Win exe with this name). Eberhard Mattes
 * remarks Tue, 6 Apr 93: Moreover, it reports the type of the (new and very
 * bug ridden) Win Emacs as "OS/2 executable".
 * 
 * 3. apptype() uses the filename if given, otherwise a tmp file is created with
 * the contents of buf. If buf is not the complete file, apptype can
 * incorrectly identify the exe type. The "-z" option of "file" is the reason
 * for this ugly code.
 */

/*
 * amai: Darrel Hankerson did the changes described here.
 * 
 * It remains to check the validity of comments (2.) since it's referred to an
 * "old" OS/2 version.
 * 
 */

#include "file.h"

#ifndef	lint
FILE_RCSID("@(#)$File: apptype.c,v 1.13 2011/09/07 21:57:15 christos Exp $")
#endif /* lint */

#include <stdlib.h>
#include <string.h>

#ifdef __EMX__
#include <io.h>
#define INCL_DOSSESMGR
#define INCL_DOSERRORS
#define INCL_DOSFILEMGR
#include <os2.h>
typedef ULONG   APPTYPE;

protected int
file_os2_apptype(struct magic_set *ms, const char *fn, const void *buf,
    size_t nb)
{
	APPTYPE         rc, type;
	char            path[_MAX_PATH], drive[_MAX_DRIVE], dir[_MAX_DIR],
			fname[_MAX_FNAME], ext[_MAX_EXT];
	char           *filename;
	FILE           *fp;

	if (fn)
		filename = strdup(fn);
	else if ((filename = tempnam("./", "tmp")) == NULL) {
		file_error(ms, errno, "cannot create tempnam");
		return -1;
	}
	/* qualify the filename to prevent extraneous searches */
	_splitpath(filename, drive, dir, fname, ext);
	(void)sprintf(path, "%s%s%s%s", drive,
		(*dir == '\0') ? "./" : dir,
		fname,
		(*ext == '\0') ? "." : ext);

	if (fn == NULL) {
		if ((fp = fopen(path, "wb")) == NULL) {
			file_error(ms, errno, "cannot open tmp file `%s'", path);
			return -1;
		}
		if (fwrite(buf, 1, nb, fp) != nb) {
			file_error(ms, errno, "cannot write tmp file `%s'",
			    path);
			(void)fclose(fp);
			return -1;
		}
		(void)fclose(fp);
	}
	rc = DosQueryAppType((unsigned char *)path, &type);

	if (fn == NULL) {
		unlink(path);
		free(filename);
	}
#if 0
	if (rc == ERROR_INVALID_EXE_SIGNATURE)
		printf("%s: not an executable file\n", fname);
	else if (rc == ERROR_FILE_NOT_FOUND)
		printf("%s: not found\n", fname);
	else if (rc == ERROR_ACCESS_DENIED)
		printf("%s: access denied\n", fname);
	else if (rc != 0)
		printf("%s: error code = %lu\n", fname, rc);
	else
#else

	/*
	 * for our purpose here it's sufficient to just ignore the error and
	 * return w/o success (=0)
	 */

	if (rc)
		return (0);

#endif

	if (type & FAPPTYP_32BIT)
		if (file_printf(ms, "32-bit ") == -1)
			return -1;
	if (type & FAPPTYP_PHYSDRV) {
		if (file_printf(ms, "physical device driver") == -1)
			return -1;
	} else if (type & FAPPTYP_VIRTDRV) {
		if (file_printf(ms, "virtual device driver") == -1)
			return -1;
	} else if (type & FAPPTYP_DLL) {
		if (type & FAPPTYP_PROTDLL)
			if (file_printf(ms, "protected ") == -1)
				return -1;
		if (file_printf(ms, "DLL") == -1)
			return -1;
	} else if (type & (FAPPTYP_WINDOWSREAL | FAPPTYP_WINDOWSPROT)) {
		if (file_printf(ms, "Windows executable") == -1)
			return -1;
	} else if (type & FAPPTYP_DOS) {
		/*
		 * The API routine is partially broken on filenames ending
		 * ".com".
		 */
		if (stricmp(ext, ".com") == 0)
			if (strncmp((const char *)buf, "MZ", 2))
				return (0);
		if (file_printf(ms, "DOS executable") == -1)
			return -1;
		/* ---------------------------------------- */
		/* Might learn more from the magic(4) entry */
		if (file_printf(ms, ", magic(4)-> ") == -1)
			return -1;
		return (0);
		/* ---------------------------------------- */
	} else if (type & FAPPTYP_BOUND) {
		if (file_printf(ms, "bound executable") == -1)
			return -1;
	} else if ((type & 7) == FAPPTYP_WINDOWAPI) {
		if (file_printf(ms, "PM executable") == -1)
			return -1;
	} else if (file_printf(ms, "OS/2 executable") == -1)
		return -1;

	switch (type & (FAPPTYP_NOTWINDOWCOMPAT |
			FAPPTYP_WINDOWCOMPAT |
			FAPPTYP_WINDOWAPI)) {
	case FAPPTYP_NOTWINDOWCOMPAT:
		if (file_printf(ms, " [NOTWINDOWCOMPAT]") == -1)
			return -1;
		break;
	case FAPPTYP_WINDOWCOMPAT:
		if (file_printf(ms, " [WINDOWCOMPAT]") == -1)
			return -1;
		break;
	case FAPPTYP_WINDOWAPI:
		if (file_printf(ms, " [WINDOWAPI]") == -1)
			return -1;
		break;
	}
	return 1;
}
#endif

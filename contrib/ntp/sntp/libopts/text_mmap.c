/**
 * @file text_mmap.c
 *
 * Map a text file, ensuring the text always has an ending NUL byte.
 *
 * @addtogroup autoopts
 * @{
 */
/*
 *  This file is part of AutoOpts, a companion to AutoGen.
 *  AutoOpts is free software.
 *  AutoOpts is Copyright (C) 1992-2015 by Bruce Korb - all rights reserved
 *
 *  AutoOpts is available under any one of two licenses.  The license
 *  in use must be one of these two and the choice is under the control
 *  of the user of the license.
 *
 *   The GNU Lesser General Public License, version 3 or later
 *      See the files "COPYING.lgplv3" and "COPYING.gplv3"
 *
 *   The Modified Berkeley Software Distribution License
 *      See the file "COPYING.mbsd"
 *
 *  These files have the following sha256 sums:
 *
 *  8584710e9b04216a394078dc156b781d0b47e1729104d666658aecef8ee32e95  COPYING.gplv3
 *  4379e7444a0e2ce2b12dd6f5a52a27a4d02d39d247901d3285c88cf0d37f477b  COPYING.lgplv3
 *  13aa749a5b0a454917a944ed8fffc530b784f5ead522b1aacaf4ec8aa55a6239  COPYING.mbsd
 */
#if defined(HAVE_MMAP)
#  ifndef      MAP_ANONYMOUS
#    ifdef     MAP_ANON
#      define  MAP_ANONYMOUS   MAP_ANON
#    endif
#  endif

#  if ! defined(MAP_ANONYMOUS) && ! defined(HAVE_DEV_ZERO)
     /*
      * We must have either /dev/zero or anonymous mapping for
      * this to work.
      */
#    undef HAVE_MMAP

#  else
#    ifdef _SC_PAGESIZE
#      define GETPAGESIZE() sysconf(_SC_PAGESIZE)
#    else
#      define GETPAGESIZE() getpagesize()
#    endif
#  endif
#endif

/*
 *  Some weird systems require that a specifically invalid FD number
 *  get passed in as an argument value.  Which value is that?  Well,
 *  as everybody knows, if open(2) fails, it returns -1, so that must
 *  be the value.  :)
 */
#define AO_INVALID_FD  -1

#define FILE_WRITABLE(_prt,_flg) \
        (   (_prt & PROT_WRITE) \
         && ((_flg & (MAP_SHARED|MAP_PRIVATE)) == MAP_SHARED))
#define MAP_FAILED_PTR (VOIDP(MAP_FAILED))

/**
 * Load the contents of a text file.  There are two separate implementations,
 * depending up on whether mmap(3) is available.
 *
 *  If not available, malloc the file length plus one byte.  Read it in
 *  and NUL terminate.
 *
 *  If available, first check to see if the text file size is a multiple of a
 *  page size.  If it is, map the file size plus an extra page from either
 *  anonymous memory or from /dev/zero.  Then map the file text on top of the
 *  first pages of the anonymous/zero pages.  Otherwise, just map the file
 *  because there will be NUL bytes provided at the end.
 *
 * @param mapinfo a structure holding everything we need to know
 *        about the mapping.
 *
 * @param pzFile name of the file, for error reporting.
 */
static void
load_text_file(tmap_info_t * mapinfo, char const * pzFile)
{
#if ! defined(HAVE_MMAP)
    mapinfo->txt_data = AGALOC(mapinfo->txt_size+1, "file text");
    if (mapinfo->txt_data == NULL) {
        mapinfo->txt_errno = ENOMEM;
        return;
    }

    {
        size_t sz = mapinfo->txt_size;
        char * pz = mapinfo->txt_data;

        while (sz > 0) {
            ssize_t rdct = read(mapinfo->txt_fd, pz, sz);
            if (rdct <= 0) {
                mapinfo->txt_errno = errno;
                fserr_warn("libopts", "read", pzFile);
                free(mapinfo->txt_data);
                return;
            }

            pz += rdct;
            sz -= rdct;
        }

        *pz = NUL;
    }

    mapinfo->txt_errno   = 0;

#else /* HAVE mmap */
    size_t const pgsz = (size_t)GETPAGESIZE();
    void * map_addr   = NULL;

    (void)pzFile;

    mapinfo->txt_full_size = (mapinfo->txt_size + pgsz) & ~(pgsz - 1);
    if (mapinfo->txt_full_size == (mapinfo->txt_size + pgsz)) {
        /*
         * The text is a multiple of a page boundary.  We must map an
         * extra page so the text ends with a NUL.
         */
#if defined(MAP_ANONYMOUS)
        map_addr = mmap(NULL, mapinfo->txt_full_size, PROT_READ|PROT_WRITE,
                        MAP_ANONYMOUS|MAP_PRIVATE, AO_INVALID_FD, 0);
#else
        mapinfo->txt_zero_fd = open("/dev/zero", O_RDONLY);

        if (mapinfo->txt_zero_fd == AO_INVALID_FD) {
            mapinfo->txt_errno = errno;
            return;
        }
        map_addr = mmap(NULL, mapinfo->txt_full_size, PROT_READ|PROT_WRITE,
                        MAP_PRIVATE, mapinfo->txt_zero_fd, 0);
#endif
        if (map_addr == MAP_FAILED_PTR) {
            mapinfo->txt_errno = errno;
            return;
        }
        mapinfo->txt_flags |= MAP_FIXED;
    }

    mapinfo->txt_data =
        mmap(map_addr, mapinfo->txt_size, mapinfo->txt_prot,
             mapinfo->txt_flags, mapinfo->txt_fd, 0);

    if (mapinfo->txt_data == MAP_FAILED_PTR)
        mapinfo->txt_errno = errno;
#endif /* HAVE_MMAP */
}

/**
 * Make sure all the parameters are correct:  we have a file name that
 * is a text file that we can read.
 *
 * @param fname the text file to map
 * @param prot  the memory protections requested (read/write/etc.)
 * @param flags mmap flags
 * @param mapinfo a structure holding everything we need to know
 *        about the mapping.
 */
static void
validate_mmap(char const * fname, int prot, int flags, tmap_info_t * mapinfo)
{
    memset(mapinfo, 0, sizeof(*mapinfo));
#if defined(HAVE_MMAP) && ! defined(MAP_ANONYMOUS)
    mapinfo->txt_zero_fd = AO_INVALID_FD;
#endif
    mapinfo->txt_fd      = AO_INVALID_FD;
    mapinfo->txt_prot    = prot;
    mapinfo->txt_flags   = flags;

    /*
     *  Map mmap flags and protections into open flags and do the open.
     */
    {
        /*
         *  See if we will be updating the file.  If we can alter the memory
         *  and if we share the data and we are *not* copy-on-writing the data,
         *  then our updates will show in the file, so we must open with
         *  write access.
         */
        int o_flag = FILE_WRITABLE(prot, flags) ? O_RDWR : O_RDONLY;

        /*
         *  If you're not sharing the file and you are writing to it,
         *  then don't let anyone else have access to the file.
         */
        if (((flags & MAP_SHARED) == 0) && (prot & PROT_WRITE))
            o_flag |= O_EXCL;

        mapinfo->txt_fd = open(fname, o_flag);
        if (mapinfo->txt_fd < 0) {
            mapinfo->txt_errno = errno;
            mapinfo->txt_fd = AO_INVALID_FD;
            return;
        }
    }

    /*
     *  Make sure we can stat the regular file.  Save the file size.
     */
    {
        struct stat sb;
        if (fstat(mapinfo->txt_fd, &sb) != 0) {
            mapinfo->txt_errno = errno;
            close(mapinfo->txt_fd);
            return;
        }

        if (! S_ISREG(sb.st_mode)) {
            mapinfo->txt_errno = errno = EINVAL;
            close(mapinfo->txt_fd);
            return;
        }

        mapinfo->txt_size = (size_t)sb.st_size;
    }

    if (mapinfo->txt_fd == AO_INVALID_FD)
        mapinfo->txt_errno = errno;
}

/**
 * Close any files opened by the mapping.
 *
 * @param mi a structure holding everything we need to know about the map.
 */
static void
close_mmap_files(tmap_info_t * mi)
{
    if (mi->txt_fd == AO_INVALID_FD)
        return;

    close(mi->txt_fd);
    mi->txt_fd = AO_INVALID_FD;

#if defined(HAVE_MMAP) && ! defined(MAP_ANONYMOUS)
    if (mi->txt_zero_fd == AO_INVALID_FD)
        return;

    close(mi->txt_zero_fd);
    mi->txt_zero_fd = AO_INVALID_FD;
#endif
}

/*=export_func  text_mmap
 * private:
 *
 * what:  map a text file with terminating NUL
 *
 * arg:   char const *,  pzFile,  name of the file to map
 * arg:   int,           prot,    mmap protections (see mmap(2))
 * arg:   int,           flags,   mmap flags (see mmap(2))
 * arg:   tmap_info_t *, mapinfo, returned info about the mapping
 *
 * ret-type:   void *
 * ret-desc:   The mmaped data address
 *
 * doc:
 *
 * This routine will mmap a file into memory ensuring that there is at least
 * one @file{NUL} character following the file data.  It will return the
 * address where the file contents have been mapped into memory.  If there is a
 * problem, then it will return @code{MAP_FAILED} and set @code{errno}
 * appropriately.
 *
 * The named file does not exist, @code{stat(2)} will set @code{errno} as it
 * will.  If the file is not a regular file, @code{errno} will be
 * @code{EINVAL}.  At that point, @code{open(2)} is attempted with the access
 * bits set appropriately for the requested @code{mmap(2)} protections and flag
 * bits.  On failure, @code{errno} will be set according to the documentation
 * for @code{open(2)}.  If @code{mmap(2)} fails, @code{errno} will be set as
 * that routine sets it.  If @code{text_mmap} works to this point, a valid
 * address will be returned, but there may still be ``issues''.
 *
 * If the file size is not an even multiple of the system page size, then
 * @code{text_map} will return at this point and @code{errno} will be zero.
 * Otherwise, an anonymous map is attempted.  If not available, then an attempt
 * is made to @code{mmap(2)} @file{/dev/zero}.  If any of these fail, the
 * address of the file's data is returned, bug @code{no} @file{NUL} characters
 * are mapped after the end of the data.
 *
 * see: mmap(2), open(2), stat(2)
 *
 * err: Any error code issued by mmap(2), open(2), stat(2) is possible.
 *      Additionally, if the specified file is not a regular file, then
 *      errno will be set to @code{EINVAL}.
 *
 * example:
 * #include <mylib.h>
 * tmap_info_t mi;
 * int no_nul;
 * void * data = text_mmap("file", PROT_WRITE, MAP_PRIVATE, &mi);
 * if (data == MAP_FAILED) return;
 * no_nul = (mi.txt_size == mi.txt_full_size);
 * << use the data >>
 * text_munmap(&mi);
=*/
void *
text_mmap(char const * pzFile, int prot, int flags, tmap_info_t * mi)
{
    validate_mmap(pzFile, prot, flags, mi);
    if (mi->txt_errno != 0)
        return MAP_FAILED_PTR;

    load_text_file(mi, pzFile);

    if (mi->txt_errno == 0)
        return mi->txt_data;

    close_mmap_files(mi);

    errno = mi->txt_errno;
    mi->txt_data = MAP_FAILED_PTR;
    return mi->txt_data;
}


/*=export_func  text_munmap
 * private:
 *
 * what:  unmap the data mapped in by text_mmap
 *
 * arg:   tmap_info_t *, mapinfo, info about the mapping
 *
 * ret-type:   int
 * ret-desc:   -1 or 0.  @code{errno} will have the error code.
 *
 * doc:
 *
 * This routine will unmap the data mapped in with @code{text_mmap} and close
 * the associated file descriptors opened by that function.
 *
 * see: munmap(2), close(2)
 *
 * err: Any error code issued by munmap(2) or close(2) is possible.
=*/
int
text_munmap(tmap_info_t * mi)
{
    errno = 0;

#ifdef HAVE_MMAP
    (void)munmap(mi->txt_data, mi->txt_full_size);

#else  /* don't HAVE_MMAP */
    /*
     *  IF the memory is writable *AND* it is not private (copy-on-write)
     *     *AND* the memory is "sharable" (seen by other processes)
     *  THEN rewrite the data.  Emulate mmap visibility.
     */
    if (   FILE_WRITABLE(mi->txt_prot, mi->txt_flags)
        && (lseek(mi->txt_fd, 0, SEEK_SET) >= 0) ) {
        write(mi->txt_fd, mi->txt_data, mi->txt_size);
    }

    free(mi->txt_data);
#endif /* HAVE_MMAP */

    mi->txt_errno = errno;
    close_mmap_files(mi);

    return mi->txt_errno;
}

/** @}
 *
 * Local Variables:
 * mode: C
 * c-file-style: "stroustrup"
 * indent-tabs-mode: nil
 * End:
 * end of autoopts/text_mmap.c */

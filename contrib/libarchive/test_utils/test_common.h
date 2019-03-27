/*
 * Copyright (c) 2003-2017 Tim Kientzle
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef	TEST_COMMON_H
#define	TEST_COMMON_H

/*
 * The goal of this file (and the matching test.c) is to
 * simplify the very repetitive test-*.c test programs.
 */
#if defined(HAVE_CONFIG_H)
/* Most POSIX platforms use the 'configure' script to build config.h */
#include "config.h"
#elif defined(__FreeBSD__)
/* Building as part of FreeBSD system requires a pre-built config.h. */
#include "config_freebsd.h"
#elif defined(_WIN32) && !defined(__CYGWIN__)
/* Win32 can't run the 'configure' script. */
#include "config_windows.h"
#else
/* Warn if the library hasn't been (automatically or manually) configured. */
#error Oops: No config.h and no pre-built configuration in test.h.
#endif

#include <sys/types.h>  /* Windows requires this before sys/stat.h */
#include <sys/stat.h>

#if HAVE_DIRENT_H
#include <dirent.h>
#endif
#ifdef HAVE_DIRECT_H
#include <direct.h>
#define dirent direct
#endif
#include <errno.h>
#include <fcntl.h>
#ifdef HAVE_IO_H
#include <io.h>
#endif
#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <wchar.h>
#ifdef HAVE_ACL_LIBACL_H
#include <acl/libacl.h>
#endif
#ifdef HAVE_SYS_ACL_H
#include <sys/acl.h>
#endif
#ifdef HAVE_SYS_RICHACL_H
#include <sys/richacl.h>
#endif
#ifdef HAVE_WINDOWS_H
#include <windows.h>
#endif

/*
 * System-specific tweaks.  We really want to minimize these
 * as much as possible, since they make it harder to understand
 * the mainline code.
 */

/* Windows (including Visual Studio and MinGW but not Cygwin) */
#if defined(_WIN32) && !defined(__CYGWIN__)
#if !defined(__BORLANDC__)
#undef chdir
#define chdir _chdir
#define strdup _strdup
#endif
#endif

/* Visual Studio */
#if defined(_MSC_VER) && _MSC_VER < 1900
#define snprintf	sprintf_s
#endif

#if defined(__BORLANDC__)
#pragma warn -8068	/* Constant out of range in comparison. */
#endif

/* Haiku OS and QNX */
#if defined(__HAIKU__) || defined(__QNXNTO__)
/* Haiku and QNX have typedefs in stdint.h (needed for int64_t) */
#include <stdint.h>
#endif

/* Get a real definition for __FBSDID if we can */
#if HAVE_SYS_CDEFS_H
#include <sys/cdefs.h>
#endif

/* If not, define it so as to avoid dangling semicolons. */
#ifndef __FBSDID
#define	__FBSDID(a)     struct _undefined_hack
#endif

#ifndef O_BINARY
#define	O_BINARY 0
#endif

#include "archive_platform_acl.h"
#define	ARCHIVE_TEST_ACL_TYPE_POSIX1E	1
#define	ARCHIVE_TEST_ACL_TYPE_NFS4	2

#include "archive_platform_xattr.h"

/*
 * Redefine DEFINE_TEST for use in defining the test functions.
 */
#undef DEFINE_TEST
#define DEFINE_TEST(name) void name(void); void name(void)

/* An implementation of the standard assert() macro */
#define assert(e)   assertion_assert(__FILE__, __LINE__, (e), #e, NULL)
/* chdir() and error if it fails */
#define assertChdir(path)  \
  assertion_chdir(__FILE__, __LINE__, path)
/* Assert two files have the same file flags */
#define assertEqualFflags(patha, pathb)	\
  assertion_compare_fflags(__FILE__, __LINE__, patha, pathb, 0)
/* Assert two integers are the same.  Reports value of each one if not. */
#define assertEqualInt(v1,v2) \
  assertion_equal_int(__FILE__, __LINE__, (v1), #v1, (v2), #v2, NULL)
/* Assert two strings are the same.  Reports value of each one if not. */
#define assertEqualString(v1,v2)   \
  assertion_equal_string(__FILE__, __LINE__, (v1), #v1, (v2), #v2, NULL, 0)
#define assertEqualUTF8String(v1,v2)   \
  assertion_equal_string(__FILE__, __LINE__, (v1), #v1, (v2), #v2, NULL, 1)
/* As above, but v1 and v2 are wchar_t * */
#define assertEqualWString(v1,v2)   \
  assertion_equal_wstring(__FILE__, __LINE__, (v1), #v1, (v2), #v2, NULL)
/* As above, but raw blocks of bytes. */
#define assertEqualMem(v1, v2, l)	\
  assertion_equal_mem(__FILE__, __LINE__, (v1), #v1, (v2), #v2, (l), #l, NULL)
/* Assert that memory is full of a specified byte */
#define assertMemoryFilledWith(v1, l, b)					\
  assertion_memory_filled_with(__FILE__, __LINE__, (v1), #v1, (l), #l, (b), #b, NULL)
/* Assert two files are the same. */
#define assertEqualFile(f1, f2)	\
  assertion_equal_file(__FILE__, __LINE__, (f1), (f2))
/* Assert that a file is empty. */
#define assertEmptyFile(pathname)	\
  assertion_empty_file(__FILE__, __LINE__, (pathname))
/* Assert that a file is not empty. */
#define assertNonEmptyFile(pathname)		\
  assertion_non_empty_file(__FILE__, __LINE__, (pathname))
#define assertFileAtime(pathname, sec, nsec)	\
  assertion_file_atime(__FILE__, __LINE__, pathname, sec, nsec)
#define assertFileAtimeRecent(pathname)	\
  assertion_file_atime_recent(__FILE__, __LINE__, pathname)
#define assertFileBirthtime(pathname, sec, nsec)	\
  assertion_file_birthtime(__FILE__, __LINE__, pathname, sec, nsec)
#define assertFileBirthtimeRecent(pathname) \
  assertion_file_birthtime_recent(__FILE__, __LINE__, pathname)
/* Assert that a file exists; supports printf-style arguments. */
#define assertFileExists(pathname) \
  assertion_file_exists(__FILE__, __LINE__, pathname)
/* Assert that a file exists. */
#define assertFileNotExists(pathname) \
  assertion_file_not_exists(__FILE__, __LINE__, pathname)
/* Assert that file contents match a string. */
#define assertFileContents(data, data_size, pathname) \
  assertion_file_contents(__FILE__, __LINE__, data, data_size, pathname)
/* Verify that a file does not contain invalid strings */
#define assertFileContainsNoInvalidStrings(pathname, strings) \
  assertion_file_contains_no_invalid_strings(__FILE__, __LINE__, pathname, strings)
#define assertFileMtime(pathname, sec, nsec)	\
  assertion_file_mtime(__FILE__, __LINE__, pathname, sec, nsec)
#define assertFileMtimeRecent(pathname) \
  assertion_file_mtime_recent(__FILE__, __LINE__, pathname)
#define assertFileNLinks(pathname, nlinks)  \
  assertion_file_nlinks(__FILE__, __LINE__, pathname, nlinks)
#define assertFileSize(pathname, size)  \
  assertion_file_size(__FILE__, __LINE__, pathname, size)
#define assertFileMode(pathname, mode)  \
  assertion_file_mode(__FILE__, __LINE__, pathname, mode)
#define assertTextFileContents(text, pathname) \
  assertion_text_file_contents(__FILE__, __LINE__, text, pathname)
#define assertFileContainsLinesAnyOrder(pathname, lines)	\
  assertion_file_contains_lines_any_order(__FILE__, __LINE__, pathname, lines)
#define assertIsDir(pathname, mode)		\
  assertion_is_dir(__FILE__, __LINE__, pathname, mode)
#define assertIsHardlink(path1, path2)	\
  assertion_is_hardlink(__FILE__, __LINE__, path1, path2)
#define assertIsNotHardlink(path1, path2)	\
  assertion_is_not_hardlink(__FILE__, __LINE__, path1, path2)
#define assertIsReg(pathname, mode)		\
  assertion_is_reg(__FILE__, __LINE__, pathname, mode)
#define assertIsSymlink(pathname, contents)	\
  assertion_is_symlink(__FILE__, __LINE__, pathname, contents)
/* Create a directory, report error if it fails. */
#define assertMakeDir(dirname, mode)	\
  assertion_make_dir(__FILE__, __LINE__, dirname, mode)
#define assertMakeFile(path, mode, contents) \
  assertion_make_file(__FILE__, __LINE__, path, mode, -1, contents)
#define assertMakeBinFile(path, mode, csize, contents) \
  assertion_make_file(__FILE__, __LINE__, path, mode, csize, contents)
#define assertMakeHardlink(newfile, oldfile)	\
  assertion_make_hardlink(__FILE__, __LINE__, newfile, oldfile)
#define assertMakeSymlink(newfile, linkto)	\
  assertion_make_symlink(__FILE__, __LINE__, newfile, linkto)
#define assertSetNodump(path)	\
  assertion_set_nodump(__FILE__, __LINE__, path)
#define assertUmask(mask)	\
  assertion_umask(__FILE__, __LINE__, mask)
/* Assert that two files have unequal file flags */
#define assertUnequalFflags(patha, pathb)	\
  assertion_compare_fflags(__FILE__, __LINE__, patha, pathb, 1)
#define assertUtimes(pathname, atime, atime_nsec, mtime, mtime_nsec)	\
  assertion_utimes(__FILE__, __LINE__, pathname, atime, atime_nsec, mtime, mtime_nsec)
#ifndef PROGRAM
#define assertEntrySetAcls(entry, acls, count) \
  assertion_entry_set_acls(__FILE__, __LINE__, entry, acls, count)
#define assertEntryCompareAcls(entry, acls, count, type, mode) \
  assertion_entry_compare_acls(__FILE__, __LINE__, entry, acls, count, type, mode)
#endif

/*
 * This would be simple with C99 variadic macros, but I don't want to
 * require that.  Instead, I insert a function call before each
 * skipping() call to pass the file and line information down.  Crude,
 * but effective.
 */
#define skipping	\
  skipping_setup(__FILE__, __LINE__);test_skipping

/* Function declarations.  These are defined in test_utility.c. */
void failure(const char *fmt, ...);
int assertion_assert(const char *, int, int, const char *, void *);
int assertion_chdir(const char *, int, const char *);
int assertion_compare_fflags(const char *, int, const char *, const char *,
    int);
int assertion_empty_file(const char *, int, const char *);
int assertion_equal_file(const char *, int, const char *, const char *);
int assertion_equal_int(const char *, int, long long, const char *, long long, const char *, void *);
int assertion_equal_mem(const char *, int, const void *, const char *, const void *, const char *, size_t, const char *, void *);
int assertion_memory_filled_with(const char *, int, const void *, const char *, size_t, const char *, char, const char *, void *);
int assertion_equal_string(const char *, int, const char *v1, const char *, const char *v2, const char *, void *, int);
int assertion_equal_wstring(const char *, int, const wchar_t *v1, const char *, const wchar_t *v2, const char *, void *);
int assertion_file_atime(const char *, int, const char *, long, long);
int assertion_file_atime_recent(const char *, int, const char *);
int assertion_file_birthtime(const char *, int, const char *, long, long);
int assertion_file_birthtime_recent(const char *, int, const char *);
int assertion_file_contains_lines_any_order(const char *, int, const char *, const char **);
int assertion_file_contains_no_invalid_strings(const char *, int, const char *, const char **);
int assertion_file_contents(const char *, int, const void *, int, const char *);
int assertion_file_exists(const char *, int, const char *);
int assertion_file_mode(const char *, int, const char *, int);
int assertion_file_mtime(const char *, int, const char *, long, long);
int assertion_file_mtime_recent(const char *, int, const char *);
int assertion_file_nlinks(const char *, int, const char *, int);
int assertion_file_not_exists(const char *, int, const char *);
int assertion_file_size(const char *, int, const char *, long);
int assertion_is_dir(const char *, int, const char *, int);
int assertion_is_hardlink(const char *, int, const char *, const char *);
int assertion_is_not_hardlink(const char *, int, const char *, const char *);
int assertion_is_reg(const char *, int, const char *, int);
int assertion_is_symlink(const char *, int, const char *, const char *);
int assertion_make_dir(const char *, int, const char *, int);
int assertion_make_file(const char *, int, const char *, int, int, const void *);
int assertion_make_hardlink(const char *, int, const char *newpath, const char *);
int assertion_make_symlink(const char *, int, const char *newpath, const char *);
int assertion_non_empty_file(const char *, int, const char *);
int assertion_set_nodump(const char *, int, const char *);
int assertion_text_file_contents(const char *, int, const char *buff, const char *f);
int assertion_umask(const char *, int, int);
int assertion_utimes(const char *, int, const char *, long, long, long, long );
int assertion_version(const char*, int, const char *, const char *);

void skipping_setup(const char *, int);
void test_skipping(const char *fmt, ...);

/* Like sprintf, then system() */
int systemf(const char * fmt, ...);

/* Delay until time() returns a value after this. */
void sleepUntilAfter(time_t);

/* Return true if this platform can create symlinks. */
int canSymlink(void);

/* Return true if this platform can run the "bzip2" program. */
int canBzip2(void);

/* Return true if this platform can run the "grzip" program. */
int canGrzip(void);

/* Return true if this platform can run the "gzip" program. */
int canGzip(void);

/* Return true if this platform can run the specified command. */
int canRunCommand(const char *);

/* Return true if this platform can run the "lrzip" program. */
int canLrzip(void);

/* Return true if this platform can run the "lz4" program. */
int canLz4(void);

/* Return true if this platform can run the "zstd" program. */
int canZstd(void);

/* Return true if this platform can run the "lzip" program. */
int canLzip(void);

/* Return true if this platform can run the "lzma" program. */
int canLzma(void);

/* Return true if this platform can run the "lzop" program. */
int canLzop(void);

/* Return true if this platform can run the "xz" program. */
int canXz(void);

/* Return true if this filesystem can handle nodump flags. */
int canNodump(void);

/* Set test ACLs */
int setTestAcl(const char *path);

/* Get extended attribute */
void *getXattr(const char *, const char *, size_t *);

/* Set extended attribute */
int setXattr(const char *, const char *, const void *, size_t);

/* Return true if the file has large i-node number(>0xffffffff). */
int is_LargeInode(const char *);

#if ARCHIVE_ACL_SUNOS
/* Fetch ACLs on Solaris using acl() or facl() */
void *sunacl_get(int cmd, int *aclcnt, int fd, const char *path);
#endif

/* Suck file into string allocated via malloc(). Call free() when done. */
/* Supports printf-style args: slurpfile(NULL, "%s/myfile", refdir); */
char *slurpfile(size_t *, const char *fmt, ...);

/* Dump block of bytes to a file. */
void dumpfile(const char *filename, void *, size_t);

/* Extracts named reference file to the current directory. */
void extract_reference_file(const char *);
/* Copies named reference file to the current directory. */
void copy_reference_file(const char *);

/* Extracts a list of files to the current directory.
 * List must be NULL terminated.
 */
void extract_reference_files(const char **);

/* Subtract umask from mode */
mode_t umasked(mode_t expected_mode);

/* Path to working directory for current test */
extern const char *testworkdir;

#ifndef PROGRAM
/*
 * Special interfaces for libarchive test harness.
 */

#include "archive.h"
#include "archive_entry.h"

/* ACL structure */
struct archive_test_acl_t {
	int type;  /* Type of ACL */
	int permset; /* Permissions for this class of users. */
	int tag; /* Owner, User, Owning group, group, other, etc. */
	int qual; /* GID or UID of user/group, depending on tag. */
	const char *name; /* Name of user/group, depending on tag. */
};

/* Set ACLs */
int assertion_entry_set_acls(const char *, int, struct archive_entry *,
    struct archive_test_acl_t *, int);

/* Compare ACLs */
int assertion_entry_compare_acls(const char *, int, struct archive_entry *,
    struct archive_test_acl_t *, int, int, int);

/* Special customized read-from-memory interface. */
int read_open_memory(struct archive *, const void *, size_t, size_t);
/* _minimal version exercises a slightly different set of libarchive APIs. */
int read_open_memory_minimal(struct archive *, const void *, size_t, size_t);
/* _seek version produces a seekable file. */
int read_open_memory_seek(struct archive *, const void *, size_t, size_t);

/* Versions of above that accept an archive argument for additional info. */
#define assertA(e)   assertion_assert(__FILE__, __LINE__, (e), #e, (a))
#define assertEqualIntA(a,v1,v2)   \
  assertion_equal_int(__FILE__, __LINE__, (v1), #v1, (v2), #v2, (a))
#define assertEqualStringA(a,v1,v2)   \
  assertion_equal_string(__FILE__, __LINE__, (v1), #v1, (v2), #v2, (a), 0)

#else	/* defined(PROGRAM) */
/*
 * Special interfaces for program test harness.
 */

/* Pathname of exe to be tested. */
extern const char *testprogfile;
/* Name of exe to use in printf-formatted command strings. */
/* On Windows, this includes leading/trailing quotes. */
extern const char *testprog;

void assertVersion(const char *prog, const char *base);

#endif	/* defined(PROGRAM) */

#ifdef USE_DMALLOC
#include <dmalloc.h>
#endif

#endif	/* TEST_COMMON_H */

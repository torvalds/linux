/*
 * Copyright (c) 2003-2009 Tim Kientzle
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
 */

#include "test.h"
#include "test_utils.h"
#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#include <errno.h>
#ifdef HAVE_ICONV_H
#include <iconv.h>
#endif
/*
 * Some Linux distributions have both linux/ext2_fs.h and ext2fs/ext2_fs.h.
 * As the include guards don't agree, the order of include is important.
 */
#ifdef HAVE_LINUX_EXT2_FS_H
#include <linux/ext2_fs.h>      /* for Linux file flags */
#endif
#if defined(HAVE_EXT2FS_EXT2_FS_H) && !defined(__CYGWIN__)
#include <ext2fs/ext2_fs.h>     /* Linux file flags, broken on Cygwin */
#endif
#include <limits.h>
#include <locale.h>
#ifdef HAVE_SIGNAL_H
#include <signal.h>
#endif
#include <stdarg.h>
#include <time.h>

/*
 * This same file is used pretty much verbatim for all test harnesses.
 *
 * The next few lines are the only differences.
 * TODO: Move this into a separate configuration header, have all test
 * suites share one copy of this file.
 */
__FBSDID("$FreeBSD$");
#define KNOWNREF	"test_compat_gtar_1.tar.uu"
#define	ENVBASE "LIBARCHIVE" /* Prefix for environment variables. */
#undef	PROGRAM              /* Testing a library, not a program. */
#define	LIBRARY	"libarchive"
#define	EXTRA_DUMP(x)	archive_error_string((struct archive *)(x))
#define	EXTRA_ERRNO(x)	archive_errno((struct archive *)(x))
#define	EXTRA_VERSION	archive_version_details()

/*
 *
 * Windows support routines
 *
 * Note: Configuration is a tricky issue.  Using HAVE_* feature macros
 * in the test harness is dangerous because they cover up
 * configuration errors.  The classic example of this is omitting a
 * configure check.  If libarchive and libarchive_test both look for
 * the same feature macro, such errors are hard to detect.  Platform
 * macros (e.g., _WIN32 or __GNUC__) are a little better, but can
 * easily lead to very messy code.  It's best to limit yourself
 * to only the most generic programming techniques in the test harness
 * and thus avoid conditionals altogether.  Where that's not possible,
 * try to minimize conditionals by grouping platform-specific tests in
 * one place (e.g., test_acl_freebsd) or by adding new assert()
 * functions (e.g., assertMakeHardlink()) to cover up platform
 * differences.  Platform-specific coding in libarchive_test is often
 * a symptom that some capability is missing from libarchive itself.
 */
#if defined(_WIN32) && !defined(__CYGWIN__)
#include <io.h>
#include <direct.h>
#include <windows.h>
#ifndef F_OK
#define F_OK (0)
#endif
#ifndef S_ISDIR
#define S_ISDIR(m)  ((m) & _S_IFDIR)
#endif
#ifndef S_ISREG
#define S_ISREG(m)  ((m) & _S_IFREG)
#endif
#if !defined(__BORLANDC__)
#define access _access
#undef chdir
#define chdir _chdir
#endif
#ifndef fileno
#define fileno _fileno
#endif
/*#define fstat _fstat64*/
#if !defined(__BORLANDC__)
#define getcwd _getcwd
#endif
#define lstat stat
/*#define lstat _stat64*/
/*#define stat _stat64*/
#define rmdir _rmdir
#if !defined(__BORLANDC__)
#define strdup _strdup
#define umask _umask
#endif
#define int64_t __int64
#endif

#if defined(HAVE__CrtSetReportMode)
# include <crtdbg.h>
#endif

mode_t umasked(mode_t expected_mode)
{
	mode_t mode = umask(0);
	umask(mode);
	return expected_mode & ~mode;
}

/* Path to working directory for current test */
const char *testworkdir;
#ifdef PROGRAM
/* Pathname of exe to be tested. */
const char *testprogfile;
/* Name of exe to use in printf-formatted command strings. */
/* On Windows, this includes leading/trailing quotes. */
const char *testprog;
#endif

#if defined(_WIN32) && !defined(__CYGWIN__)
static void	*GetFunctionKernel32(const char *);
static int	 my_CreateSymbolicLinkA(const char *, const char *, int);
static int	 my_CreateHardLinkA(const char *, const char *);
static int	 my_GetFileInformationByName(const char *,
		     BY_HANDLE_FILE_INFORMATION *);

static void *
GetFunctionKernel32(const char *name)
{
	static HINSTANCE lib;
	static int set;
	if (!set) {
		set = 1;
		lib = LoadLibrary("kernel32.dll");
	}
	if (lib == NULL) {
		fprintf(stderr, "Can't load kernel32.dll?!\n");
		exit(1);
	}
	return (void *)GetProcAddress(lib, name);
}

static int
my_CreateSymbolicLinkA(const char *linkname, const char *target, int flags)
{
	static BOOLEAN (WINAPI *f)(LPCSTR, LPCSTR, DWORD);
	static int set;
	if (!set) {
		set = 1;
		f = GetFunctionKernel32("CreateSymbolicLinkA");
	}
	return f == NULL ? 0 : (*f)(linkname, target, flags);
}

static int
my_CreateHardLinkA(const char *linkname, const char *target)
{
	static BOOLEAN (WINAPI *f)(LPCSTR, LPCSTR, LPSECURITY_ATTRIBUTES);
	static int set;
	if (!set) {
		set = 1;
		f = GetFunctionKernel32("CreateHardLinkA");
	}
	return f == NULL ? 0 : (*f)(linkname, target, NULL);
}

static int
my_GetFileInformationByName(const char *path, BY_HANDLE_FILE_INFORMATION *bhfi)
{
	HANDLE h;
	int r;

	memset(bhfi, 0, sizeof(*bhfi));
	h = CreateFile(path, FILE_READ_ATTRIBUTES, 0, NULL,
		OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	if (h == INVALID_HANDLE_VALUE)
		return (0);
	r = GetFileInformationByHandle(h, bhfi);
	CloseHandle(h);
	return (r);
}
#endif

#if defined(HAVE__CrtSetReportMode) && !defined(__WATCOMC__)
static void
invalid_parameter_handler(const wchar_t * expression,
    const wchar_t * function, const wchar_t * file,
    unsigned int line, uintptr_t pReserved)
{
	/* nop */
	// Silence unused-parameter compiler warnings.
	(void)expression;
	(void)function;
	(void)file;
	(void)line;
	(void)pReserved;
}
#endif

/*
 *
 * OPTIONS FLAGS
 *
 */

/* Enable core dump on failure. */
static int dump_on_failure = 0;
/* Default is to remove temp dirs and log data for successful tests. */
static int keep_temp_files = 0;
/* Default is to run the specified tests once and report errors. */
static int until_failure = 0;
/* Default is to just report pass/fail for each test. */
static int verbosity = 0;
#define	VERBOSITY_SUMMARY_ONLY -1 /* -q */
#define VERBOSITY_PASSFAIL 0   /* Default */
#define VERBOSITY_LIGHT_REPORT 1 /* -v */
#define VERBOSITY_FULL 2 /* -vv */
/* A few places generate even more output for verbosity > VERBOSITY_FULL,
 * mostly for debugging the test harness itself. */
/* Cumulative count of assertion failures. */
static int failures = 0;
/* Cumulative count of reported skips. */
static int skips = 0;
/* Cumulative count of assertions checked. */
static int assertions = 0;

/* Directory where uuencoded reference files can be found. */
static const char *refdir;

/*
 * Report log information selectively to console and/or disk log.
 */
static int log_console = 0;
static FILE *logfile;
static void
vlogprintf(const char *fmt, va_list ap)
{
#ifdef va_copy
	va_list lfap;
	va_copy(lfap, ap);
#endif
	if (log_console)
		vfprintf(stdout, fmt, ap);
	if (logfile != NULL)
#ifdef va_copy
		vfprintf(logfile, fmt, lfap);
	va_end(lfap);
#else
		vfprintf(logfile, fmt, ap);
#endif
}

static void
logprintf(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vlogprintf(fmt, ap);
	va_end(ap);
}

/* Set up a message to display only if next assertion fails. */
static char msgbuff[4096];
static const char *msg, *nextmsg;
void
failure(const char *fmt, ...)
{
	va_list ap;
	if (fmt == NULL) {
		nextmsg = NULL;
	} else {
		va_start(ap, fmt);
		vsprintf(msgbuff, fmt, ap);
		va_end(ap);
		nextmsg = msgbuff;
	}
}

/*
 * Copy arguments into file-local variables.
 * This was added to permit vararg assert() functions without needing
 * variadic wrapper macros.  Turns out that the vararg capability is almost
 * never used, so almost all of the vararg assertions can be simplified
 * by removing the vararg capability and reworking the wrapper macro to
 * pass __FILE__, __LINE__ directly into the function instead of using
 * this hook.  I suspect this machinery is used so rarely that we
 * would be better off just removing it entirely.  That would simplify
 * the code here noticeably.
 */
static const char *skipping_filename;
static int skipping_line;
void skipping_setup(const char *filename, int line)
{
	skipping_filename = filename;
	skipping_line = line;
}

/* Called at the beginning of each assert() function. */
static void
assertion_count(const char *file, int line)
{
	(void)file; /* UNUSED */
	(void)line; /* UNUSED */
	++assertions;
	/* Proper handling of "failure()" message. */
	msg = nextmsg;
	nextmsg = NULL;
	/* Uncomment to print file:line after every assertion.
	 * Verbose, but occasionally useful in tracking down crashes. */
	/* printf("Checked %s:%d\n", file, line); */
}

/*
 * For each test source file, we remember how many times each
 * assertion was reported.  Cleared before each new test,
 * used by test_summarize().
 */
static struct line {
	int count;
	int skip;
}  failed_lines[10000];
const char *failed_filename;

/* Count this failure, setup up log destination and handle initial report. */
static void
failure_start(const char *filename, int line, const char *fmt, ...)
{
	va_list ap;

	/* Record another failure for this line. */
	++failures;
	failed_filename = filename;
	failed_lines[line].count++;

	/* Determine whether to log header to console. */
	switch (verbosity) {
	case VERBOSITY_LIGHT_REPORT:
		log_console = (failed_lines[line].count < 2);
		break;
	default:
		log_console = (verbosity >= VERBOSITY_FULL);
	}

	/* Log file:line header for this failure */
	va_start(ap, fmt);
#if _MSC_VER
	logprintf("%s(%d): ", filename, line);
#else
	logprintf("%s:%d: ", filename, line);
#endif
	vlogprintf(fmt, ap);
	va_end(ap);
	logprintf("\n");

	if (msg != NULL && msg[0] != '\0') {
		logprintf("   Description: %s\n", msg);
		msg = NULL;
	}

	/* Determine whether to log details to console. */
	if (verbosity == VERBOSITY_LIGHT_REPORT)
		log_console = 0;
}

/* Complete reporting of failed tests. */
/*
 * The 'extra' hook here is used by libarchive to include libarchive
 * error messages with assertion failures.  It could also be used
 * to add strerror() output, for example.  Just define the EXTRA_DUMP()
 * macro appropriately.
 */
static void
failure_finish(void *extra)
{
	(void)extra; /* UNUSED (maybe) */
#ifdef EXTRA_DUMP
	if (extra != NULL) {
		logprintf("    errno: %d\n", EXTRA_ERRNO(extra));
		logprintf("   detail: %s\n", EXTRA_DUMP(extra));
	}
#endif

	if (dump_on_failure) {
		fprintf(stderr,
		    " *** forcing core dump so failure can be debugged ***\n");
		abort();
	}
}

/* Inform user that we're skipping some checks. */
void
test_skipping(const char *fmt, ...)
{
	char buff[1024];
	va_list ap;

	va_start(ap, fmt);
	vsprintf(buff, fmt, ap);
	va_end(ap);
	/* Use failure() message if set. */
	msg = nextmsg;
	nextmsg = NULL;
	/* failure_start() isn't quite right, but is awfully convenient. */
	failure_start(skipping_filename, skipping_line, "SKIPPING: %s", buff);
	--failures; /* Undo failures++ in failure_start() */
	/* Don't failure_finish() here. */
	/* Mark as skip, so doesn't count as failed test. */
	failed_lines[skipping_line].skip = 1;
	++skips;
}

/*
 *
 * ASSERTIONS
 *
 */

/* Generic assert() just displays the failed condition. */
int
assertion_assert(const char *file, int line, int value,
    const char *condition, void *extra)
{
	assertion_count(file, line);
	if (!value) {
		failure_start(file, line, "Assertion failed: %s", condition);
		failure_finish(extra);
	}
	return (value);
}

/* chdir() and report any errors */
int
assertion_chdir(const char *file, int line, const char *pathname)
{
	assertion_count(file, line);
	if (chdir(pathname) == 0)
		return (1);
	failure_start(file, line, "chdir(\"%s\")", pathname);
	failure_finish(NULL);
	return (0);

}

/* Verify two integers are equal. */
int
assertion_equal_int(const char *file, int line,
    long long v1, const char *e1, long long v2, const char *e2, void *extra)
{
	assertion_count(file, line);
	if (v1 == v2)
		return (1);
	failure_start(file, line, "%s != %s", e1, e2);
	logprintf("      %s=%lld (0x%llx, 0%llo)\n", e1, v1, v1, v1);
	logprintf("      %s=%lld (0x%llx, 0%llo)\n", e2, v2, v2, v2);
	failure_finish(extra);
	return (0);
}

/*
 * Utility to convert a single UTF-8 sequence.
 */
static int
_utf8_to_unicode(uint32_t *pwc, const char *s, size_t n)
{
	static const char utf8_count[256] = {
		 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,/* 00 - 0F */
		 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,/* 10 - 1F */
		 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,/* 20 - 2F */
		 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,/* 30 - 3F */
		 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,/* 40 - 4F */
		 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,/* 50 - 5F */
		 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,/* 60 - 6F */
		 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,/* 70 - 7F */
		 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,/* 80 - 8F */
		 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,/* 90 - 9F */
		 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,/* A0 - AF */
		 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,/* B0 - BF */
		 0, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,/* C0 - CF */
		 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,/* D0 - DF */
		 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,/* E0 - EF */
		 4, 4, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 /* F0 - FF */
	};
	int ch;
	int cnt;
	uint32_t wc;

	*pwc = 0;

	/* Sanity check. */
	if (n == 0)
		return (0);
	/*
	 * Decode 1-4 bytes depending on the value of the first byte.
	 */
	ch = (unsigned char)*s;
	if (ch == 0)
		return (0); /* Standard:  return 0 for end-of-string. */
	cnt = utf8_count[ch];

	/* Invalid sequence or there are not plenty bytes. */
	if (n < (size_t)cnt)
		return (-1);

	/* Make a Unicode code point from a single UTF-8 sequence. */
	switch (cnt) {
	case 1:	/* 1 byte sequence. */
		*pwc = ch & 0x7f;
		return (cnt);
	case 2:	/* 2 bytes sequence. */
		if ((s[1] & 0xc0) != 0x80) return (-1);
		*pwc = ((ch & 0x1f) << 6) | (s[1] & 0x3f);
		return (cnt);
	case 3:	/* 3 bytes sequence. */
		if ((s[1] & 0xc0) != 0x80) return (-1);
		if ((s[2] & 0xc0) != 0x80) return (-1);
		wc = ((ch & 0x0f) << 12)
		    | ((s[1] & 0x3f) << 6)
		    | (s[2] & 0x3f);
		if (wc < 0x800)
			return (-1);/* Overlong sequence. */
		break;
	case 4:	/* 4 bytes sequence. */
		if (n < 4)
			return (-1);
		if ((s[1] & 0xc0) != 0x80) return (-1);
		if ((s[2] & 0xc0) != 0x80) return (-1);
		if ((s[3] & 0xc0) != 0x80) return (-1);
		wc = ((ch & 0x07) << 18)
		    | ((s[1] & 0x3f) << 12)
		    | ((s[2] & 0x3f) << 6)
		    | (s[3] & 0x3f);
		if (wc < 0x10000)
			return (-1);/* Overlong sequence. */
		break;
	default:
		return (-1);
	}

	/* The code point larger than 0x10FFFF is not legal
	 * Unicode values. */
	if (wc > 0x10FFFF)
		return (-1);
	/* Correctly gets a Unicode, returns used bytes. */
	*pwc = wc;
	return (cnt);
}

static void strdump(const char *e, const char *p, int ewidth, int utf8)
{
	const char *q = p;

	logprintf("      %*s = ", ewidth, e);
	if (p == NULL) {
		logprintf("NULL\n");
		return;
	}
	logprintf("\"");
	while (*p != '\0') {
		unsigned int c = 0xff & *p++;
		switch (c) {
		case '\a': logprintf("\\a"); break;
		case '\b': logprintf("\\b"); break;
		case '\n': logprintf("\\n"); break;
		case '\r': logprintf("\\r"); break;
		default:
			if (c >= 32 && c < 127)
				logprintf("%c", c);
			else
				logprintf("\\x%02X", c);
		}
	}
	logprintf("\"");
	logprintf(" (length %d)", q == NULL ? -1 : (int)strlen(q));

	/*
	 * If the current string is UTF-8, dump its code points.
	 */
	if (utf8) {
		size_t len;
		uint32_t uc;
		int n;
		int cnt = 0;

		p = q;
		len = strlen(p);
		logprintf(" [");
		while ((n = _utf8_to_unicode(&uc, p, len)) > 0) {
			if (p != q)
				logprintf(" ");
			logprintf("%04X", uc);
			p += n;
			len -= n;
			cnt++;
		}
		logprintf("]");
		logprintf(" (count %d", cnt);
		if (n < 0) {
			logprintf(",unknown %d bytes", len);
		}
		logprintf(")");

	}
	logprintf("\n");
}

/* Verify two strings are equal, dump them if not. */
int
assertion_equal_string(const char *file, int line,
    const char *v1, const char *e1,
    const char *v2, const char *e2,
    void *extra, int utf8)
{
	int l1, l2;

	assertion_count(file, line);
	if (v1 == v2 || (v1 != NULL && v2 != NULL && strcmp(v1, v2) == 0))
		return (1);
	failure_start(file, line, "%s != %s", e1, e2);
	l1 = (int)strlen(e1);
	l2 = (int)strlen(e2);
	if (l1 < l2)
		l1 = l2;
	strdump(e1, v1, l1, utf8);
	strdump(e2, v2, l1, utf8);
	failure_finish(extra);
	return (0);
}

static void
wcsdump(const char *e, const wchar_t *w)
{
	logprintf("      %s = ", e);
	if (w == NULL) {
		logprintf("(null)");
		return;
	}
	logprintf("\"");
	while (*w != L'\0') {
		unsigned int c = *w++;
		if (c >= 32 && c < 127)
			logprintf("%c", c);
		else if (c < 256)
			logprintf("\\x%02X", c);
		else if (c < 0x10000)
			logprintf("\\u%04X", c);
		else
			logprintf("\\U%08X", c);
	}
	logprintf("\"\n");
}

#ifndef HAVE_WCSCMP
static int
wcscmp(const wchar_t *s1, const wchar_t *s2)
{

	while (*s1 == *s2++) {
		if (*s1++ == L'\0')
			return 0;
	}
	if (*s1 > *--s2)
		return 1;
	else
		return -1;
}
#endif

/* Verify that two wide strings are equal, dump them if not. */
int
assertion_equal_wstring(const char *file, int line,
    const wchar_t *v1, const char *e1,
    const wchar_t *v2, const char *e2,
    void *extra)
{
	assertion_count(file, line);
	if (v1 == v2)
		return (1);
	if (v1 != NULL && v2 != NULL && wcscmp(v1, v2) == 0)
		return (1);
	failure_start(file, line, "%s != %s", e1, e2);
	wcsdump(e1, v1);
	wcsdump(e2, v2);
	failure_finish(extra);
	return (0);
}

/*
 * Pretty standard hexdump routine.  As a bonus, if ref != NULL, then
 * any bytes in p that differ from ref will be highlighted with '_'
 * before and after the hex value.
 */
static void
hexdump(const char *p, const char *ref, size_t l, size_t offset)
{
	size_t i, j;
	char sep;

	if (p == NULL) {
		logprintf("(null)\n");
		return;
	}
	for(i=0; i < l; i+=16) {
		logprintf("%04x", (unsigned)(i + offset));
		sep = ' ';
		for (j = 0; j < 16 && i + j < l; j++) {
			if (ref != NULL && p[i + j] != ref[i + j])
				sep = '_';
			logprintf("%c%02x", sep, 0xff & (int)p[i+j]);
			if (ref != NULL && p[i + j] == ref[i + j])
				sep = ' ';
		}
		for (; j < 16; j++) {
			logprintf("%c  ", sep);
			sep = ' ';
		}
		logprintf("%c", sep);
		for (j=0; j < 16 && i + j < l; j++) {
			int c = p[i + j];
			if (c >= ' ' && c <= 126)
				logprintf("%c", c);
			else
				logprintf(".");
		}
		logprintf("\n");
	}
}

/* Verify that two blocks of memory are the same, display the first
 * block of differences if they're not. */
int
assertion_equal_mem(const char *file, int line,
    const void *_v1, const char *e1,
    const void *_v2, const char *e2,
    size_t l, const char *ld, void *extra)
{
	const char *v1 = (const char *)_v1;
	const char *v2 = (const char *)_v2;
	size_t offset;

	assertion_count(file, line);
	if (v1 == v2 || (v1 != NULL && v2 != NULL && memcmp(v1, v2, l) == 0))
		return (1);
	if (v1 == NULL || v2 == NULL)
		return (0);

	failure_start(file, line, "%s != %s", e1, e2);
	logprintf("      size %s = %d\n", ld, (int)l);
	/* Dump 48 bytes (3 lines) so that the first difference is
	 * in the second line. */
	offset = 0;
	while (l > 64 && memcmp(v1, v2, 32) == 0) {
		/* Two lines agree, so step forward one line. */
		v1 += 16;
		v2 += 16;
		l -= 16;
		offset += 16;
	}
	logprintf("      Dump of %s\n", e1);
	hexdump(v1, v2, l < 128 ? l : 128, offset);
	logprintf("      Dump of %s\n", e2);
	hexdump(v2, v1, l < 128 ? l : 128, offset);
	logprintf("\n");
	failure_finish(extra);
	return (0);
}

/* Verify that a block of memory is filled with the specified byte. */
int
assertion_memory_filled_with(const char *file, int line,
    const void *_v1, const char *vd,
    size_t l, const char *ld,
    char b, const char *bd, void *extra)
{
	const char *v1 = (const char *)_v1;
	size_t c = 0;
	size_t i;
	(void)ld; /* UNUSED */

	assertion_count(file, line);

	for (i = 0; i < l; ++i) {
		if (v1[i] == b) {
			++c;
		}
	}
	if (c == l)
		return (1);

	failure_start(file, line, "%s (size %d) not filled with %s", vd, (int)l, bd);
	logprintf("   Only %d bytes were correct\n", (int)c);
	failure_finish(extra);
	return (0);
}

/* Verify that the named file exists and is empty. */
int
assertion_empty_file(const char *filename, int line, const char *f1)
{
	char buff[1024];
	struct stat st;
	ssize_t s;
	FILE *f;

	assertion_count(filename, line);

	if (stat(f1, &st) != 0) {
		failure_start(filename, line, "Stat failed: %s", f1);
		failure_finish(NULL);
		return (0);
	}
	if (st.st_size == 0)
		return (1);

	failure_start(filename, line, "File should be empty: %s", f1);
	logprintf("    File size: %d\n", (int)st.st_size);
	logprintf("    Contents:\n");
	f = fopen(f1, "rb");
	if (f == NULL) {
		logprintf("    Unable to open %s\n", f1);
	} else {
		s = ((off_t)sizeof(buff) < st.st_size) ?
		    (ssize_t)sizeof(buff) : (ssize_t)st.st_size;
		s = fread(buff, 1, s, f);
		hexdump(buff, NULL, s, 0);
		fclose(f);
	}
	failure_finish(NULL);
	return (0);
}

/* Verify that the named file exists and is not empty. */
int
assertion_non_empty_file(const char *filename, int line, const char *f1)
{
	struct stat st;

	assertion_count(filename, line);

	if (stat(f1, &st) != 0) {
		failure_start(filename, line, "Stat failed: %s", f1);
		failure_finish(NULL);
		return (0);
	}
	if (st.st_size == 0) {
		failure_start(filename, line, "File empty: %s", f1);
		failure_finish(NULL);
		return (0);
	}
	return (1);
}

/* Verify that two files have the same contents. */
/* TODO: hexdump the first bytes that actually differ. */
int
assertion_equal_file(const char *filename, int line, const char *fn1, const char *fn2)
{
	char buff1[1024];
	char buff2[1024];
	FILE *f1, *f2;
	int n1, n2;

	assertion_count(filename, line);

	f1 = fopen(fn1, "rb");
	f2 = fopen(fn2, "rb");
	if (f1 == NULL || f2 == NULL) {
		if (f1) fclose(f1);
		if (f2) fclose(f2);
		return (0);
	}
	for (;;) {
		n1 = (int)fread(buff1, 1, sizeof(buff1), f1);
		n2 = (int)fread(buff2, 1, sizeof(buff2), f2);
		if (n1 != n2)
			break;
		if (n1 == 0 && n2 == 0) {
			fclose(f1);
			fclose(f2);
			return (1);
		}
		if (memcmp(buff1, buff2, n1) != 0)
			break;
	}
	fclose(f1);
	fclose(f2);
	failure_start(filename, line, "Files not identical");
	logprintf("  file1=\"%s\"\n", fn1);
	logprintf("  file2=\"%s\"\n", fn2);
	failure_finish(NULL);
	return (0);
}

/* Verify that the named file does exist. */
int
assertion_file_exists(const char *filename, int line, const char *f)
{
	assertion_count(filename, line);

#if defined(_WIN32) && !defined(__CYGWIN__)
	if (!_access(f, 0))
		return (1);
#else
	if (!access(f, F_OK))
		return (1);
#endif
	failure_start(filename, line, "File should exist: %s", f);
	failure_finish(NULL);
	return (0);
}

/* Verify that the named file doesn't exist. */
int
assertion_file_not_exists(const char *filename, int line, const char *f)
{
	assertion_count(filename, line);

#if defined(_WIN32) && !defined(__CYGWIN__)
	if (_access(f, 0))
		return (1);
#else
	if (access(f, F_OK))
		return (1);
#endif
	failure_start(filename, line, "File should not exist: %s", f);
	failure_finish(NULL);
	return (0);
}

/* Compare the contents of a file to a block of memory. */
int
assertion_file_contents(const char *filename, int line, const void *buff, int s, const char *fn)
{
	char *contents;
	FILE *f;
	int n;

	assertion_count(filename, line);

	f = fopen(fn, "rb");
	if (f == NULL) {
		failure_start(filename, line,
		    "File should exist: %s", fn);
		failure_finish(NULL);
		return (0);
	}
	contents = malloc(s * 2);
	n = (int)fread(contents, 1, s * 2, f);
	fclose(f);
	if (n == s && memcmp(buff, contents, s) == 0) {
		free(contents);
		return (1);
	}
	failure_start(filename, line, "File contents don't match");
	logprintf("  file=\"%s\"\n", fn);
	if (n > 0)
		hexdump(contents, buff, n > 512 ? 512 : n, 0);
	else {
		logprintf("  File empty, contents should be:\n");
		hexdump(buff, NULL, s > 512 ? 512 : s, 0);
	}
	failure_finish(NULL);
	free(contents);
	return (0);
}

/* Check the contents of a text file, being tolerant of line endings. */
int
assertion_text_file_contents(const char *filename, int line, const char *buff, const char *fn)
{
	char *contents;
	const char *btxt, *ftxt;
	FILE *f;
	int n, s;

	assertion_count(filename, line);
	f = fopen(fn, "r");
	if (f == NULL) {
		failure_start(filename, line,
		    "File doesn't exist: %s", fn);
		failure_finish(NULL);
		return (0);
	}
	s = (int)strlen(buff);
	contents = malloc(s * 2 + 128);
	n = (int)fread(contents, 1, s * 2 + 128 - 1, f);
	if (n >= 0)
		contents[n] = '\0';
	fclose(f);
	/* Compare texts. */
	btxt = buff;
	ftxt = (const char *)contents;
	while (*btxt != '\0' && *ftxt != '\0') {
		if (*btxt == *ftxt) {
			++btxt;
			++ftxt;
			continue;
		}
		if (btxt[0] == '\n' && ftxt[0] == '\r' && ftxt[1] == '\n') {
			/* Pass over different new line characters. */
			++btxt;
			ftxt += 2;
			continue;
		}
		break;
	}
	if (*btxt == '\0' && *ftxt == '\0') {
		free(contents);
		return (1);
	}
	failure_start(filename, line, "Contents don't match");
	logprintf("  file=\"%s\"\n", fn);
	if (n > 0) {
		hexdump(contents, buff, n, 0);
		logprintf("  expected\n", fn);
		hexdump(buff, contents, s, 0);
	} else {
		logprintf("  File empty, contents should be:\n");
		hexdump(buff, NULL, s, 0);
	}
	failure_finish(NULL);
	free(contents);
	return (0);
}

/* Verify that a text file contains the specified lines, regardless of order */
/* This could be more efficient if we sorted both sets of lines, etc, but
 * since this is used only for testing and only ever deals with a dozen or so
 * lines at a time, this relatively crude approach is just fine. */
int
assertion_file_contains_lines_any_order(const char *file, int line,
    const char *pathname, const char *lines[])
{
	char *buff;
	size_t buff_size;
	size_t expected_count, actual_count, i, j;
	char **expected = NULL;
	char *p, **actual = NULL;
	char c;
	int expected_failure = 0, actual_failure = 0;

	assertion_count(file, line);

	buff = slurpfile(&buff_size, "%s", pathname);
	if (buff == NULL) {
		failure_start(pathname, line, "Can't read file: %s", pathname);
		failure_finish(NULL);
		return (0);
	}

	/* Make a copy of the provided lines and count up the expected
	 * file size. */
	for (i = 0; lines[i] != NULL; ++i) {
	}
	expected_count = i;
	if (expected_count) {
		expected = malloc(sizeof(char *) * expected_count);
		if (expected == NULL) {
			failure_start(pathname, line, "Can't allocate memory");
			failure_finish(NULL);
			free(expected);
			return (0);
		}
		for (i = 0; lines[i] != NULL; ++i) {
			expected[i] = strdup(lines[i]);
		}
	}

	/* Break the file into lines */
	actual_count = 0;
	for (c = '\0', p = buff; p < buff + buff_size; ++p) {
		if (*p == '\x0d' || *p == '\x0a')
			*p = '\0';
		if (c == '\0' && *p != '\0')
			++actual_count;
		c = *p;
	}
	if (actual_count) {
		actual = calloc(sizeof(char *), actual_count);
		if (actual == NULL) {
			failure_start(pathname, line, "Can't allocate memory");
			failure_finish(NULL);
			free(expected);
			return (0);
		}
		for (j = 0, p = buff; p < buff + buff_size;
		    p += 1 + strlen(p)) {
			if (*p != '\0') {
				actual[j] = p;
				++j;
			}
		}
	}

	/* Erase matching lines from both lists */
	for (i = 0; i < expected_count; ++i) {
		if (expected[i] == NULL)
			continue;
		for (j = 0; j < actual_count; ++j) {
			if (actual[j] == NULL)
				continue;
			if (strcmp(expected[i], actual[j]) == 0) {
				free(expected[i]);
				expected[i] = NULL;
				actual[j] = NULL;
				break;
			}
		}
	}

	/* If there's anything left, it's a failure */
	for (i = 0; i < expected_count; ++i) {
		if (expected[i] != NULL)
			++expected_failure;
	}
	for (j = 0; j < actual_count; ++j) {
		if (actual[j] != NULL)
			++actual_failure;
	}
	if (expected_failure == 0 && actual_failure == 0) {
		free(buff);
		free(expected);
		free(actual);
		return (1);
	}
	failure_start(file, line, "File doesn't match: %s", pathname);
	for (i = 0; i < expected_count; ++i) {
		if (expected[i] != NULL) {
			logprintf("  Expected but not present: %s\n", expected[i]);
			free(expected[i]);
		}
	}
	for (j = 0; j < actual_count; ++j) {
		if (actual[j] != NULL)
			logprintf("  Present but not expected: %s\n", actual[j]);
	}
	failure_finish(NULL);
	free(buff);
	free(expected);
	free(actual);
	return (0);
}

/* Verify that a text file does not contains the specified strings */
int
assertion_file_contains_no_invalid_strings(const char *file, int line,
    const char *pathname, const char *strings[])
{
	char *buff;
	int i;

	buff = slurpfile(NULL, "%s", pathname);
	if (buff == NULL) {
		failure_start(file, line, "Can't read file: %s", pathname);
		failure_finish(NULL);
		return (0);
	}

	for (i = 0; strings[i] != NULL; ++i) {
		if (strstr(buff, strings[i]) != NULL) {
			failure_start(file, line, "Invalid string in %s: %s", pathname,
			    strings[i]);
			failure_finish(NULL);
			free(buff);
			return(0);
		}
	}

	free(buff);
	return (0);
}

/* Test that two paths point to the same file. */
/* As a side-effect, asserts that both files exist. */
static int
is_hardlink(const char *file, int line,
    const char *path1, const char *path2)
{
#if defined(_WIN32) && !defined(__CYGWIN__)
	BY_HANDLE_FILE_INFORMATION bhfi1, bhfi2;
	int r;

	assertion_count(file, line);
	r = my_GetFileInformationByName(path1, &bhfi1);
	if (r == 0) {
		failure_start(file, line, "File %s can't be inspected?", path1);
		failure_finish(NULL);
		return (0);
	}
	r = my_GetFileInformationByName(path2, &bhfi2);
	if (r == 0) {
		failure_start(file, line, "File %s can't be inspected?", path2);
		failure_finish(NULL);
		return (0);
	}
	return (bhfi1.dwVolumeSerialNumber == bhfi2.dwVolumeSerialNumber
		&& bhfi1.nFileIndexHigh == bhfi2.nFileIndexHigh
		&& bhfi1.nFileIndexLow == bhfi2.nFileIndexLow);
#else
	struct stat st1, st2;
	int r;

	assertion_count(file, line);
	r = lstat(path1, &st1);
	if (r != 0) {
		failure_start(file, line, "File should exist: %s", path1);
		failure_finish(NULL);
		return (0);
	}
	r = lstat(path2, &st2);
	if (r != 0) {
		failure_start(file, line, "File should exist: %s", path2);
		failure_finish(NULL);
		return (0);
	}
	return (st1.st_ino == st2.st_ino && st1.st_dev == st2.st_dev);
#endif
}

int
assertion_is_hardlink(const char *file, int line,
    const char *path1, const char *path2)
{
	if (is_hardlink(file, line, path1, path2))
		return (1);
	failure_start(file, line,
	    "Files %s and %s are not hardlinked", path1, path2);
	failure_finish(NULL);
	return (0);
}

int
assertion_is_not_hardlink(const char *file, int line,
    const char *path1, const char *path2)
{
	if (!is_hardlink(file, line, path1, path2))
		return (1);
	failure_start(file, line,
	    "Files %s and %s should not be hardlinked", path1, path2);
	failure_finish(NULL);
	return (0);
}

/* Verify a/b/mtime of 'pathname'. */
/* If 'recent', verify that it's within last 10 seconds. */
static int
assertion_file_time(const char *file, int line,
    const char *pathname, long t, long nsec, char type, int recent)
{
	long long filet, filet_nsec;
	int r;

#if defined(_WIN32) && !defined(__CYGWIN__)
#define EPOC_TIME	(116444736000000000ULL)
	FILETIME fxtime, fbirthtime, fatime, fmtime;
	ULARGE_INTEGER wintm;
	HANDLE h;
	fxtime.dwLowDateTime = 0;
	fxtime.dwHighDateTime = 0;

	assertion_count(file, line);
	/* Note: FILE_FLAG_BACKUP_SEMANTICS applies to open
	 * a directory file. If not, CreateFile() will fail when
	 * the pathname is a directory. */
	h = CreateFile(pathname, FILE_READ_ATTRIBUTES, 0, NULL,
	    OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	if (h == INVALID_HANDLE_VALUE) {
		failure_start(file, line, "Can't access %s\n", pathname);
		failure_finish(NULL);
		return (0);
	}
	r = GetFileTime(h, &fbirthtime, &fatime, &fmtime);
	switch (type) {
	case 'a': fxtime = fatime; break;
	case 'b': fxtime = fbirthtime; break;
	case 'm': fxtime = fmtime; break;
	}
	CloseHandle(h);
	if (r == 0) {
		failure_start(file, line, "Can't GetFileTime %s\n", pathname);
		failure_finish(NULL);
		return (0);
	}
	wintm.LowPart = fxtime.dwLowDateTime;
	wintm.HighPart = fxtime.dwHighDateTime;
	filet = (wintm.QuadPart - EPOC_TIME) / 10000000;
	filet_nsec = ((wintm.QuadPart - EPOC_TIME) % 10000000) * 100;
	nsec = (nsec / 100) * 100; /* Round the request */
#else
	struct stat st;

	assertion_count(file, line);
	r = lstat(pathname, &st);
	if (r != 0) {
		failure_start(file, line, "Can't stat %s\n", pathname);
		failure_finish(NULL);
		return (0);
	}
	switch (type) {
	case 'a': filet = st.st_atime; break;
	case 'm': filet = st.st_mtime; break;
	case 'b': filet = 0; break;
	default: fprintf(stderr, "INTERNAL: Bad type %c for file time", type);
		exit(1);
	}
#if defined(__FreeBSD__)
	switch (type) {
	case 'a': filet_nsec = st.st_atimespec.tv_nsec; break;
	case 'b': filet = st.st_birthtime;
		/* FreeBSD filesystems that don't support birthtime
		 * (e.g., UFS1) always return -1 here. */
		if (filet == -1) {
			return (1);
		}
		filet_nsec = st.st_birthtimespec.tv_nsec; break;
	case 'm': filet_nsec = st.st_mtimespec.tv_nsec; break;
	default: fprintf(stderr, "INTERNAL: Bad type %c for file time", type);
		exit(1);
	}
	/* FreeBSD generally only stores to microsecond res, so round. */
	filet_nsec = (filet_nsec / 1000) * 1000;
	nsec = (nsec / 1000) * 1000;
#else
	filet_nsec = nsec = 0;	/* Generic POSIX only has whole seconds. */
	if (type == 'b') return (1); /* Generic POSIX doesn't have birthtime */
#if defined(__HAIKU__)
	if (type == 'a') return (1); /* Haiku doesn't have atime. */
#endif
#endif
#endif
	if (recent) {
		/* Check that requested time is up-to-date. */
		time_t now = time(NULL);
		if (filet < now - 10 || filet > now + 1) {
			failure_start(file, line,
			    "File %s has %ctime %lld, %lld seconds ago\n",
			    pathname, type, filet, now - filet);
			failure_finish(NULL);
			return (0);
		}
	} else if (filet != t || filet_nsec != nsec) {
		failure_start(file, line,
		    "File %s has %ctime %lld.%09lld, expected %lld.%09lld",
		    pathname, type, filet, filet_nsec, t, nsec);
		failure_finish(NULL);
		return (0);
	}
	return (1);
}

/* Verify atime of 'pathname'. */
int
assertion_file_atime(const char *file, int line,
    const char *pathname, long t, long nsec)
{
	return assertion_file_time(file, line, pathname, t, nsec, 'a', 0);
}

/* Verify atime of 'pathname' is up-to-date. */
int
assertion_file_atime_recent(const char *file, int line, const char *pathname)
{
	return assertion_file_time(file, line, pathname, 0, 0, 'a', 1);
}

/* Verify birthtime of 'pathname'. */
int
assertion_file_birthtime(const char *file, int line,
    const char *pathname, long t, long nsec)
{
	return assertion_file_time(file, line, pathname, t, nsec, 'b', 0);
}

/* Verify birthtime of 'pathname' is up-to-date. */
int
assertion_file_birthtime_recent(const char *file, int line,
    const char *pathname)
{
	return assertion_file_time(file, line, pathname, 0, 0, 'b', 1);
}

/* Verify mode of 'pathname'. */
int
assertion_file_mode(const char *file, int line, const char *pathname, int expected_mode)
{
	int mode;
	int r;

	assertion_count(file, line);
#if defined(_WIN32) && !defined(__CYGWIN__)
	failure_start(file, line, "assertFileMode not yet implemented for Windows");
	(void)mode; /* UNUSED */
	(void)r; /* UNUSED */
	(void)pathname; /* UNUSED */
	(void)expected_mode; /* UNUSED */
#else
	{
		struct stat st;
		r = lstat(pathname, &st);
		mode = (int)(st.st_mode & 0777);
	}
	if (r == 0 && mode == expected_mode)
			return (1);
	failure_start(file, line, "File %s has mode %o, expected %o",
	    pathname, mode, expected_mode);
#endif
	failure_finish(NULL);
	return (0);
}

/* Verify mtime of 'pathname'. */
int
assertion_file_mtime(const char *file, int line,
    const char *pathname, long t, long nsec)
{
	return assertion_file_time(file, line, pathname, t, nsec, 'm', 0);
}

/* Verify mtime of 'pathname' is up-to-date. */
int
assertion_file_mtime_recent(const char *file, int line, const char *pathname)
{
	return assertion_file_time(file, line, pathname, 0, 0, 'm', 1);
}

/* Verify number of links to 'pathname'. */
int
assertion_file_nlinks(const char *file, int line,
    const char *pathname, int nlinks)
{
#if defined(_WIN32) && !defined(__CYGWIN__)
	BY_HANDLE_FILE_INFORMATION bhfi;
	int r;

	assertion_count(file, line);
	r = my_GetFileInformationByName(pathname, &bhfi);
	if (r != 0 && bhfi.nNumberOfLinks == (DWORD)nlinks)
		return (1);
	failure_start(file, line, "File %s has %d links, expected %d",
	    pathname, bhfi.nNumberOfLinks, nlinks);
	failure_finish(NULL);
	return (0);
#else
	struct stat st;
	int r;

	assertion_count(file, line);
	r = lstat(pathname, &st);
	if (r == 0 && (int)st.st_nlink == nlinks)
		return (1);
	failure_start(file, line, "File %s has %d links, expected %d",
	    pathname, st.st_nlink, nlinks);
	failure_finish(NULL);
	return (0);
#endif
}

/* Verify size of 'pathname'. */
int
assertion_file_size(const char *file, int line, const char *pathname, long size)
{
	int64_t filesize;
	int r;

	assertion_count(file, line);
#if defined(_WIN32) && !defined(__CYGWIN__)
	{
		BY_HANDLE_FILE_INFORMATION bhfi;
		r = !my_GetFileInformationByName(pathname, &bhfi);
		filesize = ((int64_t)bhfi.nFileSizeHigh << 32) + bhfi.nFileSizeLow;
	}
#else
	{
		struct stat st;
		r = lstat(pathname, &st);
		filesize = st.st_size;
	}
#endif
	if (r == 0 && filesize == size)
			return (1);
	failure_start(file, line, "File %s has size %ld, expected %ld",
	    pathname, (long)filesize, (long)size);
	failure_finish(NULL);
	return (0);
}

/* Assert that 'pathname' is a dir.  If mode >= 0, verify that too. */
int
assertion_is_dir(const char *file, int line, const char *pathname, int mode)
{
	struct stat st;
	int r;

#if defined(_WIN32) && !defined(__CYGWIN__)
	(void)mode; /* UNUSED */
#endif
	assertion_count(file, line);
	r = lstat(pathname, &st);
	if (r != 0) {
		failure_start(file, line, "Dir should exist: %s", pathname);
		failure_finish(NULL);
		return (0);
	}
	if (!S_ISDIR(st.st_mode)) {
		failure_start(file, line, "%s is not a dir", pathname);
		failure_finish(NULL);
		return (0);
	}
#if !defined(_WIN32) || defined(__CYGWIN__)
	/* Windows doesn't handle permissions the same way as POSIX,
	 * so just ignore the mode tests. */
	/* TODO: Can we do better here? */
	if (mode >= 0 && (mode_t)mode != (st.st_mode & 07777)) {
		failure_start(file, line, "Dir %s has wrong mode", pathname);
		logprintf("  Expected: 0%3o\n", mode);
		logprintf("  Found: 0%3o\n", st.st_mode & 07777);
		failure_finish(NULL);
		return (0);
	}
#endif
	return (1);
}

/* Verify that 'pathname' is a regular file.  If 'mode' is >= 0,
 * verify that too. */
int
assertion_is_reg(const char *file, int line, const char *pathname, int mode)
{
	struct stat st;
	int r;

#if defined(_WIN32) && !defined(__CYGWIN__)
	(void)mode; /* UNUSED */
#endif
	assertion_count(file, line);
	r = lstat(pathname, &st);
	if (r != 0 || !S_ISREG(st.st_mode)) {
		failure_start(file, line, "File should exist: %s", pathname);
		failure_finish(NULL);
		return (0);
	}
#if !defined(_WIN32) || defined(__CYGWIN__)
	/* Windows doesn't handle permissions the same way as POSIX,
	 * so just ignore the mode tests. */
	/* TODO: Can we do better here? */
	if (mode >= 0 && (mode_t)mode != (st.st_mode & 07777)) {
		failure_start(file, line, "File %s has wrong mode", pathname);
		logprintf("  Expected: 0%3o\n", mode);
		logprintf("  Found: 0%3o\n", st.st_mode & 07777);
		failure_finish(NULL);
		return (0);
	}
#endif
	return (1);
}

/* Check whether 'pathname' is a symbolic link.  If 'contents' is
 * non-NULL, verify that the symlink has those contents. */
static int
is_symlink(const char *file, int line,
    const char *pathname, const char *contents)
{
#if defined(_WIN32) && !defined(__CYGWIN__)
	(void)pathname; /* UNUSED */
	(void)contents; /* UNUSED */
	assertion_count(file, line);
	/* Windows sort-of has real symlinks, but they're only usable
	 * by privileged users and are crippled even then, so there's
	 * really not much point in bothering with this. */
	return (0);
#else
	char buff[300];
	struct stat st;
	ssize_t linklen;
	int r;

	assertion_count(file, line);
	r = lstat(pathname, &st);
	if (r != 0) {
		failure_start(file, line,
		    "Symlink should exist: %s", pathname);
		failure_finish(NULL);
		return (0);
	}
	if (!S_ISLNK(st.st_mode))
		return (0);
	if (contents == NULL)
		return (1);
	linklen = readlink(pathname, buff, sizeof(buff));
	if (linklen < 0) {
		failure_start(file, line, "Can't read symlink %s", pathname);
		failure_finish(NULL);
		return (0);
	}
	buff[linklen] = '\0';
	if (strcmp(buff, contents) != 0)
		return (0);
	return (1);
#endif
}

/* Assert that path is a symlink that (optionally) contains contents. */
int
assertion_is_symlink(const char *file, int line,
    const char *path, const char *contents)
{
	if (is_symlink(file, line, path, contents))
		return (1);
	if (contents)
		failure_start(file, line, "File %s is not a symlink to %s",
		    path, contents);
	else
		failure_start(file, line, "File %s is not a symlink", path);
	failure_finish(NULL);
	return (0);
}


/* Create a directory and report any errors. */
int
assertion_make_dir(const char *file, int line, const char *dirname, int mode)
{
	assertion_count(file, line);
#if defined(_WIN32) && !defined(__CYGWIN__)
	(void)mode; /* UNUSED */
	if (0 == _mkdir(dirname))
		return (1);
#else
	if (0 == mkdir(dirname, mode)) {
		if (0 == chmod(dirname, mode)) {
			assertion_file_mode(file, line, dirname, mode);
			return (1);
		}
	}
#endif
	failure_start(file, line, "Could not create directory %s", dirname);
	failure_finish(NULL);
	return(0);
}

/* Create a file with the specified contents and report any failures. */
int
assertion_make_file(const char *file, int line,
    const char *path, int mode, int csize, const void *contents)
{
#if defined(_WIN32) && !defined(__CYGWIN__)
	/* TODO: Rework this to set file mode as well. */
	FILE *f;
	(void)mode; /* UNUSED */
	assertion_count(file, line);
	f = fopen(path, "wb");
	if (f == NULL) {
		failure_start(file, line, "Could not create file %s", path);
		failure_finish(NULL);
		return (0);
	}
	if (contents != NULL) {
		size_t wsize;

		if (csize < 0)
			wsize = strlen(contents);
		else
			wsize = (size_t)csize;
		if (wsize != fwrite(contents, 1, wsize, f)) {
			fclose(f);
			failure_start(file, line,
			    "Could not write file %s", path);
			failure_finish(NULL);
			return (0);
		}
	}
	fclose(f);
	return (1);
#else
	int fd;
	assertion_count(file, line);
	fd = open(path, O_CREAT | O_WRONLY, mode >= 0 ? mode : 0644);
	if (fd < 0) {
		failure_start(file, line, "Could not create %s", path);
		failure_finish(NULL);
		return (0);
	}
	if (0 != chmod(path, mode)) {
		failure_start(file, line, "Could not chmod %s", path);
		failure_finish(NULL);
		close(fd);
		return (0);
	}
	if (contents != NULL) {
		ssize_t wsize;

		if (csize < 0)
			wsize = (ssize_t)strlen(contents);
		else
			wsize = (ssize_t)csize;
		if (wsize != write(fd, contents, wsize)) {
			close(fd);
			failure_start(file, line,
			    "Could not write to %s", path);
			failure_finish(NULL);
			close(fd);
			return (0);
		}
	}
	close(fd);
	assertion_file_mode(file, line, path, mode);
	return (1);
#endif
}

/* Create a hardlink and report any failures. */
int
assertion_make_hardlink(const char *file, int line,
    const char *newpath, const char *linkto)
{
	int succeeded;

	assertion_count(file, line);
#if defined(_WIN32) && !defined(__CYGWIN__)
	succeeded = my_CreateHardLinkA(newpath, linkto);
#elif HAVE_LINK
	succeeded = !link(linkto, newpath);
#else
	succeeded = 0;
#endif
	if (succeeded)
		return (1);
	failure_start(file, line, "Could not create hardlink");
	logprintf("   New link: %s\n", newpath);
	logprintf("   Old name: %s\n", linkto);
	failure_finish(NULL);
	return(0);
}

/* Create a symlink and report any failures. */
int
assertion_make_symlink(const char *file, int line,
    const char *newpath, const char *linkto)
{
#if defined(_WIN32) && !defined(__CYGWIN__)
	int targetIsDir = 0;  /* TODO: Fix this */
	assertion_count(file, line);
	if (my_CreateSymbolicLinkA(newpath, linkto, targetIsDir))
		return (1);
#elif HAVE_SYMLINK
	assertion_count(file, line);
	if (0 == symlink(linkto, newpath))
		return (1);
#endif
	failure_start(file, line, "Could not create symlink");
	logprintf("   New link: %s\n", newpath);
	logprintf("   Old name: %s\n", linkto);
	failure_finish(NULL);
	return(0);
}

/* Set umask, report failures. */
int
assertion_umask(const char *file, int line, int mask)
{
	assertion_count(file, line);
	(void)file; /* UNUSED */
	(void)line; /* UNUSED */
	umask(mask);
	return (1);
}

/* Set times, report failures. */
int
assertion_utimes(const char *file, int line,
    const char *pathname, long at, long at_nsec, long mt, long mt_nsec)
{
	int r;

#if defined(_WIN32) && !defined(__CYGWIN__)
#define WINTIME(sec, nsec) ((Int32x32To64(sec, 10000000) + EPOC_TIME)\
	 + (((nsec)/1000)*10))
	HANDLE h;
	ULARGE_INTEGER wintm;
	FILETIME fatime, fmtime;
	FILETIME *pat, *pmt;

	assertion_count(file, line);
	h = CreateFileA(pathname,GENERIC_READ | GENERIC_WRITE,
		    FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
		    FILE_FLAG_BACKUP_SEMANTICS, NULL);
	if (h == INVALID_HANDLE_VALUE) {
		failure_start(file, line, "Can't access %s\n", pathname);
		failure_finish(NULL);
		return (0);
	}

	if (at > 0 || at_nsec > 0) {
		wintm.QuadPart = WINTIME(at, at_nsec);
		fatime.dwLowDateTime = wintm.LowPart;
		fatime.dwHighDateTime = wintm.HighPart;
		pat = &fatime;
	} else
		pat = NULL;
	if (mt > 0 || mt_nsec > 0) {
		wintm.QuadPart = WINTIME(mt, mt_nsec);
		fmtime.dwLowDateTime = wintm.LowPart;
		fmtime.dwHighDateTime = wintm.HighPart;
		pmt = &fmtime;
	} else
		pmt = NULL;
	if (pat != NULL || pmt != NULL)
		r = SetFileTime(h, NULL, pat, pmt);
	else
		r = 1;
	CloseHandle(h);
	if (r == 0) {
		failure_start(file, line, "Can't SetFileTime %s\n", pathname);
		failure_finish(NULL);
		return (0);
	}
	return (1);
#else /* defined(_WIN32) && !defined(__CYGWIN__) */
	struct stat st;
	struct timeval times[2];

#if !defined(__FreeBSD__)
	mt_nsec = at_nsec = 0;	/* Generic POSIX only has whole seconds. */
#endif
	if (mt == 0 && mt_nsec == 0 && at == 0 && at_nsec == 0)
		return (1);

	r = lstat(pathname, &st);
	if (r < 0) {
		failure_start(file, line, "Can't stat %s\n", pathname);
		failure_finish(NULL);
		return (0);
	}

	if (mt == 0 && mt_nsec == 0) {
		mt = st.st_mtime;
#if defined(__FreeBSD__)
		mt_nsec = st.st_mtimespec.tv_nsec;
		/* FreeBSD generally only stores to microsecond res, so round. */
		mt_nsec = (mt_nsec / 1000) * 1000;
#endif
	}
	if (at == 0 && at_nsec == 0) {
		at = st.st_atime;
#if defined(__FreeBSD__)
		at_nsec = st.st_atimespec.tv_nsec;
		/* FreeBSD generally only stores to microsecond res, so round. */
		at_nsec = (at_nsec / 1000) * 1000;
#endif
	}

	times[1].tv_sec = mt;
	times[1].tv_usec = mt_nsec / 1000;

	times[0].tv_sec = at;
	times[0].tv_usec = at_nsec / 1000;

#ifdef HAVE_LUTIMES
	r = lutimes(pathname, times);
#else
	r = utimes(pathname, times);
#endif
	if (r < 0) {
		failure_start(file, line, "Can't utimes %s\n", pathname);
		failure_finish(NULL);
		return (0);
	}
	return (1);
#endif /* defined(_WIN32) && !defined(__CYGWIN__) */
}

/* Set nodump, report failures. */
int
assertion_nodump(const char *file, int line, const char *pathname)
{
#if defined(HAVE_STRUCT_STAT_ST_FLAGS) && defined(UF_NODUMP)
	int r;

	assertion_count(file, line);
	r = chflags(pathname, UF_NODUMP);
	if (r < 0) {
		failure_start(file, line, "Can't set nodump %s\n", pathname);
		failure_finish(NULL);
		return (0);
	}
#elif defined(EXT2_IOC_GETFLAGS) && defined(HAVE_WORKING_EXT2_IOC_GETFLAGS)\
	 && defined(EXT2_NODUMP_FL)
	int fd, r, flags;

	assertion_count(file, line);
	fd = open(pathname, O_RDONLY | O_NONBLOCK);
	if (fd < 0) {
		failure_start(file, line, "Can't open %s\n", pathname);
		failure_finish(NULL);
		return (0);
	}
	r = ioctl(fd, EXT2_IOC_GETFLAGS, &flags);
	if (r < 0) {
		failure_start(file, line, "Can't get flags %s\n", pathname);
		failure_finish(NULL);
		return (0);
	}
	flags |= EXT2_NODUMP_FL;
	r = ioctl(fd, EXT2_IOC_SETFLAGS, &flags);
	if (r < 0) {
		failure_start(file, line, "Can't set nodump %s\n", pathname);
		failure_finish(NULL);
		return (0);
	}
	close(fd);
#else
	(void)pathname; /* UNUSED */
	assertion_count(file, line);
#endif
	return (1);
}

/*
 *
 *  UTILITIES for use by tests.
 *
 */

/*
 * Check whether platform supports symlinks.  This is intended
 * for tests to use in deciding whether to bother testing symlink
 * support; if the platform doesn't support symlinks, there's no point
 * in checking whether the program being tested can create them.
 *
 * Note that the first time this test is called, we actually go out to
 * disk to create and verify a symlink.  This is necessary because
 * symlink support is actually a property of a particular filesystem
 * and can thus vary between directories on a single system.  After
 * the first call, this returns the cached result from memory, so it's
 * safe to call it as often as you wish.
 */
int
canSymlink(void)
{
	/* Remember the test result */
	static int value = 0, tested = 0;
	if (tested)
		return (value);

	++tested;
	assertion_make_file(__FILE__, __LINE__, "canSymlink.0", 0644, 1, "a");
	/* Note: Cygwin has its own symlink() emulation that does not
	 * use the Win32 CreateSymbolicLink() function. */
#if defined(_WIN32) && !defined(__CYGWIN__)
	value = my_CreateSymbolicLinkA("canSymlink.1", "canSymlink.0", 0)
	    && is_symlink(__FILE__, __LINE__, "canSymlink.1", "canSymlink.0");
#elif HAVE_SYMLINK
	value = (0 == symlink("canSymlink.0", "canSymlink.1"))
	    && is_symlink(__FILE__, __LINE__, "canSymlink.1","canSymlink.0");
#endif
	return (value);
}

/* Platform-dependent options for hiding the output of a subcommand. */
#if defined(_WIN32) && !defined(__CYGWIN__)
static const char *redirectArgs = ">NUL 2>NUL"; /* Win32 cmd.exe */
#else
static const char *redirectArgs = ">/dev/null 2>/dev/null"; /* POSIX 'sh' */
#endif
/*
 * Can this platform run the bzip2 program?
 */
int
canBzip2(void)
{
	static int tested = 0, value = 0;
	if (!tested) {
		tested = 1;
		if (systemf("bzip2 -d -V %s", redirectArgs) == 0)
			value = 1;
	}
	return (value);
}

/*
 * Can this platform run the grzip program?
 */
int
canGrzip(void)
{
	static int tested = 0, value = 0;
	if (!tested) {
		tested = 1;
		if (systemf("grzip -V %s", redirectArgs) == 0)
			value = 1;
	}
	return (value);
}

/*
 * Can this platform run the gzip program?
 */
int
canGzip(void)
{
	static int tested = 0, value = 0;
	if (!tested) {
		tested = 1;
		if (systemf("gzip -V %s", redirectArgs) == 0)
			value = 1;
	}
	return (value);
}

/*
 * Can this platform run the lrzip program?
 */
int
canRunCommand(const char *cmd)
{
  static int tested = 0, value = 0;
  if (!tested) {
    tested = 1;
    if (systemf("%s %s", cmd, redirectArgs) == 0)
      value = 1;
  }
  return (value);
}

int
canLrzip(void)
{
	static int tested = 0, value = 0;
	if (!tested) {
		tested = 1;
		if (systemf("lrzip -V %s", redirectArgs) == 0)
			value = 1;
	}
	return (value);
}

/*
 * Can this platform run the lz4 program?
 */
int
canLz4(void)
{
	static int tested = 0, value = 0;
	if (!tested) {
		tested = 1;
		if (systemf("lz4 -V %s", redirectArgs) == 0)
			value = 1;
	}
	return (value);
}

/*
 * Can this platform run the lzip program?
 */
int
canLzip(void)
{
	static int tested = 0, value = 0;
	if (!tested) {
		tested = 1;
		if (systemf("lzip -V %s", redirectArgs) == 0)
			value = 1;
	}
	return (value);
}

/*
 * Can this platform run the lzma program?
 */
int
canLzma(void)
{
	static int tested = 0, value = 0;
	if (!tested) {
		tested = 1;
		if (systemf("lzma -V %s", redirectArgs) == 0)
			value = 1;
	}
	return (value);
}

/*
 * Can this platform run the lzop program?
 */
int
canLzop(void)
{
	static int tested = 0, value = 0;
	if (!tested) {
		tested = 1;
		if (systemf("lzop -V %s", redirectArgs) == 0)
			value = 1;
	}
	return (value);
}

/*
 * Can this platform run the xz program?
 */
int
canXz(void)
{
	static int tested = 0, value = 0;
	if (!tested) {
		tested = 1;
		if (systemf("xz -V %s", redirectArgs) == 0)
			value = 1;
	}
	return (value);
}

/*
 * Can this filesystem handle nodump flags.
 */
#if defined(HAVE_STRUCT_STAT_ST_FLAGS) && defined(UF_NODUMP)

int
canNodump(void)
{
	const char *path = "cannodumptest";
	struct stat sb;

	assertion_make_file(__FILE__, __LINE__, path, 0644, 0, NULL);
	if (chflags(path, UF_NODUMP) < 0)
		return (0);
	if (stat(path, &sb) < 0)
		return (0);
	if (sb.st_flags & UF_NODUMP)
		return (1);
	return (0);
}

#elif defined(EXT2_IOC_GETFLAGS) && defined(HAVE_WORKING_EXT2_IOC_GETFLAGS)\
	 && defined(EXT2_NODUMP_FL)

int
canNodump(void)
{
	const char *path = "cannodumptest";
	int fd, r, flags;

	assertion_make_file(__FILE__, __LINE__, path, 0644, 0, NULL);
	fd = open(path, O_RDONLY | O_NONBLOCK);
	if (fd < 0)
		return (0);
	r = ioctl(fd, EXT2_IOC_GETFLAGS, &flags);
	if (r < 0)
		return (0);
	flags |= EXT2_NODUMP_FL;
	r = ioctl(fd, EXT2_IOC_SETFLAGS, &flags);
	if (r < 0)
		return (0);
	close(fd);
	fd = open(path, O_RDONLY | O_NONBLOCK);
	if (fd < 0)
		return (0);
	r = ioctl(fd, EXT2_IOC_GETFLAGS, &flags);
	if (r < 0)
		return (0);
	close(fd);
	if (flags & EXT2_NODUMP_FL)
		return (1);
	return (0);
}

#else

int
canNodump()
{
	return (0);
}

#endif

/*
 * Sleep as needed; useful for verifying disk timestamp changes by
 * ensuring that the wall-clock time has actually changed before we
 * go back to re-read something from disk.
 */
void
sleepUntilAfter(time_t t)
{
	while (t >= time(NULL))
#if defined(_WIN32) && !defined(__CYGWIN__)
		Sleep(500);
#else
		sleep(1);
#endif
}

/*
 * Call standard system() call, but build up the command line using
 * sprintf() conventions.
 */
int
systemf(const char *fmt, ...)
{
	char buff[8192];
	va_list ap;
	int r;

	va_start(ap, fmt);
	vsprintf(buff, fmt, ap);
	if (verbosity > VERBOSITY_FULL)
		logprintf("Cmd: %s\n", buff);
	r = system(buff);
	va_end(ap);
	return (r);
}

/*
 * Slurp a file into memory for ease of comparison and testing.
 * Returns size of file in 'sizep' if non-NULL, null-terminates
 * data in memory for ease of use.
 */
char *
slurpfile(size_t * sizep, const char *fmt, ...)
{
	char filename[8192];
	struct stat st;
	va_list ap;
	char *p;
	ssize_t bytes_read;
	FILE *f;
	int r;

	va_start(ap, fmt);
	vsprintf(filename, fmt, ap);
	va_end(ap);

	f = fopen(filename, "rb");
	if (f == NULL) {
		/* Note: No error; non-existent file is okay here. */
		return (NULL);
	}
	r = fstat(fileno(f), &st);
	if (r != 0) {
		logprintf("Can't stat file %s\n", filename);
		fclose(f);
		return (NULL);
	}
	p = malloc((size_t)st.st_size + 1);
	if (p == NULL) {
		logprintf("Can't allocate %ld bytes of memory to read file %s\n",
		    (long int)st.st_size, filename);
		fclose(f);
		return (NULL);
	}
	bytes_read = fread(p, 1, (size_t)st.st_size, f);
	if (bytes_read < st.st_size) {
		logprintf("Can't read file %s\n", filename);
		fclose(f);
		free(p);
		return (NULL);
	}
	p[st.st_size] = '\0';
	if (sizep != NULL)
		*sizep = (size_t)st.st_size;
	fclose(f);
	return (p);
}

/*
 * Slurp a file into memory for ease of comparison and testing.
 * Returns size of file in 'sizep' if non-NULL, null-terminates
 * data in memory for ease of use.
 */
void
dumpfile(const char *filename, void *data, size_t len)
{
	ssize_t bytes_written;
	FILE *f;

	f = fopen(filename, "wb");
	if (f == NULL) {
		logprintf("Can't open file %s for writing\n", filename);
		return;
	}
	bytes_written = fwrite(data, 1, len, f);
	if (bytes_written < (ssize_t)len)
		logprintf("Can't write file %s\n", filename);
	fclose(f);
}

/* Read a uuencoded file from the reference directory, decode, and
 * write the result into the current directory. */
#define VALID_UUDECODE(c) (c >= 32 && c <= 96)
#define	UUDECODE(c) (((c) - 0x20) & 0x3f)
void
extract_reference_file(const char *name)
{
	char buff[1024];
	FILE *in, *out;

	sprintf(buff, "%s/%s.uu", refdir, name);
	in = fopen(buff, "r");
	failure("Couldn't open reference file %s", buff);
	assert(in != NULL);
	if (in == NULL)
		return;
	/* Read up to and including the 'begin' line. */
	for (;;) {
		if (fgets(buff, sizeof(buff), in) == NULL) {
			/* TODO: This is a failure. */
			return;
		}
		if (memcmp(buff, "begin ", 6) == 0)
			break;
	}
	/* Now, decode the rest and write it. */
	out = fopen(name, "wb");
	while (fgets(buff, sizeof(buff), in) != NULL) {
		char *p = buff;
		int bytes;

		if (memcmp(buff, "end", 3) == 0)
			break;

		bytes = UUDECODE(*p++);
		while (bytes > 0) {
			int n = 0;
			/* Write out 1-3 bytes from that. */
			if (bytes > 0) {
				assert(VALID_UUDECODE(p[0]));
				assert(VALID_UUDECODE(p[1]));
				n = UUDECODE(*p++) << 18;
				n |= UUDECODE(*p++) << 12;
				fputc(n >> 16, out);
				--bytes;
			}
			if (bytes > 0) {
				assert(VALID_UUDECODE(p[0]));
				n |= UUDECODE(*p++) << 6;
				fputc((n >> 8) & 0xFF, out);
				--bytes;
			}
			if (bytes > 0) {
				assert(VALID_UUDECODE(p[0]));
				n |= UUDECODE(*p++);
				fputc(n & 0xFF, out);
				--bytes;
			}
		}
	}
	fclose(out);
	fclose(in);
}

void
copy_reference_file(const char *name)
{
	char buff[1024];
	FILE *in, *out;
	size_t rbytes;

	sprintf(buff, "%s/%s", refdir, name);
	in = fopen(buff, "rb");
	failure("Couldn't open reference file %s", buff);
	assert(in != NULL);
	if (in == NULL)
		return;
	/* Now, decode the rest and write it. */
	/* Not a lot of error checking here; the input better be right. */
	out = fopen(name, "wb");
	while ((rbytes = fread(buff, 1, sizeof(buff), in)) > 0) {
		if (fwrite(buff, 1, rbytes, out) != rbytes) {
			logprintf("Error: fwrite\n");
			break;
		}
	}
	fclose(out);
	fclose(in);
}

int
is_LargeInode(const char *file)
{
#if defined(_WIN32) && !defined(__CYGWIN__)
	BY_HANDLE_FILE_INFORMATION bhfi;
	int r;

	r = my_GetFileInformationByName(file, &bhfi);
	if (r != 0)
		return (0);
	return (bhfi.nFileIndexHigh & 0x0000FFFFUL);
#else
	struct stat st;
	int64_t ino;

	if (stat(file, &st) < 0)
		return (0);
	ino = (int64_t)st.st_ino;
	return (ino > 0xffffffff);
#endif
}

void
extract_reference_files(const char **names)
{
	while (names && *names)
		extract_reference_file(*names++);
}

/* Set ACLs */
void
archive_test_set_acls(struct archive_entry *ae,
    struct archive_test_acl_t *acls, int n)
{
	int i;

	archive_entry_acl_clear(ae);
	for (i = 0; i < n; i++) {
		failure("type=%#010x, permset=%#010x, tag=%d, qual=%d name=%s",
		    acls[i].type, acls[i].permset, acls[i].tag,
		    acls[i].qual, acls[i].name);
		assertEqualInt(ARCHIVE_OK,
		    archive_entry_acl_add_entry(ae,
			acls[i].type, acls[i].permset, acls[i].tag,
			acls[i].qual, acls[i].name));
	}
}

static int
archive_test_acl_match(struct archive_test_acl_t *acl, int type, int permset,
    int tag, int qual, const char *name)
{
	if (type != acl->type)
		return (0);
	if (permset != acl->permset)
		return (0);
	if (tag != acl->tag)
		return (0);
	if (tag == ARCHIVE_ENTRY_ACL_USER_OBJ)
		return (1);
	if (tag == ARCHIVE_ENTRY_ACL_GROUP_OBJ)
		return (1);
	if (tag == ARCHIVE_ENTRY_ACL_EVERYONE)
		return (1);
	if (tag == ARCHIVE_ENTRY_ACL_OTHER)
		return (1);
	if (qual != acl->qual)
		return (0);
	if (name == NULL) {
		if (acl->name == NULL || acl->name[0] == '\0')
			return (1);
		return (0);
	}
	if (acl->name == NULL) {
		if (name[0] == '\0')
			return (1);
		return (0);
	}
	return (0 == strcmp(name, acl->name));
}

/* Compare ACLs */
void
archive_test_compare_acls(struct archive_entry *ae,
    struct archive_test_acl_t *acls, int cnt, int want_type, int mode)
{
	int *marker;
	int i, r, n;
	int type, permset, tag, qual;
	int matched;
	const char *name;

	n = 0;
	marker = malloc(sizeof(marker[0]) * cnt);

	for (i = 0; i < cnt; i++) {
		if ((acls[i].type & want_type) != 0) {
			marker[n] = i;
			n++;
		}
	}

	failure("No ACL's to compare, type mask: %d", want_type);
	assert(n > 0);
	if (n == 0)
		return;

	while (0 == (r = archive_entry_acl_next(ae, want_type,
			 &type, &permset, &tag, &qual, &name))) {
		for (i = 0, matched = 0; i < n && !matched; i++) {
			if (archive_test_acl_match(&acls[marker[i]], type,
			    permset, tag, qual, name)) {
				/* We found a match; remove it. */
				marker[i] = marker[n - 1];
				n--;
				matched = 1;
			}
		}
		if (type == ARCHIVE_ENTRY_ACL_TYPE_ACCESS
		    && tag == ARCHIVE_ENTRY_ACL_USER_OBJ) {
			if (!matched) printf("No match for user_obj perm\n");
			failure("USER_OBJ permset (%02o) != user mode (%02o)",
			    permset, 07 & (mode >> 6));
			assert((permset << 6) == (mode & 0700));
		} else if (type == ARCHIVE_ENTRY_ACL_TYPE_ACCESS
		    && tag == ARCHIVE_ENTRY_ACL_GROUP_OBJ) {
			if (!matched) printf("No match for group_obj perm\n");
			failure("GROUP_OBJ permset %02o != group mode %02o",
			    permset, 07 & (mode >> 3));
			assert((permset << 3) == (mode & 0070));
		} else if (type == ARCHIVE_ENTRY_ACL_TYPE_ACCESS
		    && tag == ARCHIVE_ENTRY_ACL_OTHER) {
			if (!matched) printf("No match for other perm\n");
			failure("OTHER permset (%02o) != other mode (%02o)",
			    permset, mode & 07);
			assert((permset << 0) == (mode & 0007));
		} else {
			failure("Could not find match for ACL "
			    "(type=%#010x,permset=%#010x,tag=%d,qual=%d,"
			    "name=``%s'')", type, permset, tag, qual, name);
			assert(matched == 1);
		}
	}
	assertEqualInt(ARCHIVE_EOF, r);
	if ((want_type & ARCHIVE_ENTRY_ACL_TYPE_ACCESS) != 0)
		assert((mode_t)(mode & 0777) == (archive_entry_mode(ae)
		    & 0777));
	failure("Could not find match for ACL "
	    "(type=%#010x,permset=%#010x,tag=%d,qual=%d,name=``%s'')",
	    acls[marker[0]].type, acls[marker[0]].permset,
	    acls[marker[0]].tag, acls[marker[0]].qual, acls[marker[0]].name);
	assert(n == 0); /* Number of ACLs not matched should == 0 */
	free(marker);
}

/*
 *
 * TEST management
 *
 */

/*
 * "list.h" is simply created by "grep DEFINE_TEST test_*.c"; it has
 * a line like
 *      DEFINE_TEST(test_function)
 * for each test.
 */

/* Use "list.h" to declare all of the test functions. */
#undef DEFINE_TEST
#define	DEFINE_TEST(name) void name(void);
#include "list.h"

/* Use "list.h" to create a list of all tests (functions and names). */
#undef DEFINE_TEST
#define	DEFINE_TEST(n) { n, #n, 0 },
struct test_list_t tests[] = {
	#include "list.h"
};

/*
 * Summarize repeated failures in the just-completed test.
 */
static void
test_summarize(int failed, int skips_num)
{
	unsigned int i;

	switch (verbosity) {
	case VERBOSITY_SUMMARY_ONLY:
		printf(failed ? "E" : ".");
		fflush(stdout);
		break;
	case VERBOSITY_PASSFAIL:
		printf(failed ? "FAIL\n" : skips_num ? "ok (S)\n" : "ok\n");
		break;
	}

	log_console = (verbosity == VERBOSITY_LIGHT_REPORT);

	for (i = 0; i < sizeof(failed_lines)/sizeof(failed_lines[0]); i++) {
		if (failed_lines[i].count > 1 && !failed_lines[i].skip)
			logprintf("%s:%d: Summary: Failed %d times\n",
			    failed_filename, i, failed_lines[i].count);
	}
	/* Clear the failure history for the next file. */
	failed_filename = NULL;
	memset(failed_lines, 0, sizeof(failed_lines));
}

/*
 * Actually run a single test, with appropriate setup and cleanup.
 */
static int
test_run(int i, const char *tmpdir)
{
	char workdir[1024];
	char logfilename[64];
	int failures_before = failures;
	int skips_before = skips;
	int oldumask;

	switch (verbosity) {
	case VERBOSITY_SUMMARY_ONLY: /* No per-test reports at all */
		break;
	case VERBOSITY_PASSFAIL: /* rest of line will include ok/FAIL marker */
		printf("%3d: %-64s", i, tests[i].name);
		fflush(stdout);
		break;
	default: /* Title of test, details will follow */
		printf("%3d: %s\n", i, tests[i].name);
	}

	/* Chdir to the top-level work directory. */
	if (!assertChdir(tmpdir)) {
		fprintf(stderr,
		    "ERROR: Can't chdir to top work dir %s\n", tmpdir);
		exit(1);
	}
	/* Create a log file for this test. */
	sprintf(logfilename, "%s.log", tests[i].name);
	logfile = fopen(logfilename, "w");
	fprintf(logfile, "%s\n\n", tests[i].name);
	/* Chdir() to a work dir for this specific test. */
	snprintf(workdir, sizeof(workdir), "%s/%s", tmpdir, tests[i].name);
	testworkdir = workdir;
	if (!assertMakeDir(testworkdir, 0755)
	    || !assertChdir(testworkdir)) {
		fprintf(stderr,
		    "ERROR: Can't chdir to work dir %s\n", testworkdir);
		exit(1);
	}
	/* Explicitly reset the locale before each test. */
	setlocale(LC_ALL, "C");
	/* Record the umask before we run the test. */
	umask(oldumask = umask(0));
	/*
	 * Run the actual test.
	 */
	(*tests[i].func)();
	/*
	 * Clean up and report afterwards.
	 */
	testworkdir = NULL;
	/* Restore umask */
	umask(oldumask);
	/* Reset locale. */
	setlocale(LC_ALL, "C");
	/* Reset directory. */
	if (!assertChdir(tmpdir)) {
		fprintf(stderr, "ERROR: Couldn't chdir to temp dir %s\n",
		    tmpdir);
		exit(1);
	}
	/* Report per-test summaries. */
	tests[i].failures = failures - failures_before;
	test_summarize(tests[i].failures, skips - skips_before);
	/* Close the per-test log file. */
	fclose(logfile);
	logfile = NULL;
	/* If there were no failures, we can remove the work dir and logfile. */
	if (tests[i].failures == 0) {
		if (!keep_temp_files && assertChdir(tmpdir)) {
#if defined(_WIN32) && !defined(__CYGWIN__)
			/* Make sure not to leave empty directories.
			 * Sometimes a processing of closing files used by tests
			 * is not done, then rmdir will be failed and it will
			 * leave a empty test directory. So we should wait a few
			 * seconds and retry rmdir. */
			int r, t;
			for (t = 0; t < 10; t++) {
				if (t > 0)
					Sleep(1000);
				r = systemf("rmdir /S /Q %s", tests[i].name);
				if (r == 0)
					break;
			}
			systemf("del %s", logfilename);
#else
			systemf("rm -rf %s", tests[i].name);
			systemf("rm %s", logfilename);
#endif
		}
	}
	/* Return appropriate status. */
	return (tests[i].failures);
}

/*
 *
 *
 * MAIN and support routines.
 *
 *
 */

static void
usage(const char *program)
{
	static const int limit = sizeof(tests) / sizeof(tests[0]);
	int i;

	printf("Usage: %s [options] <test> <test> ...\n", program);
	printf("Default is to run all tests.\n");
	printf("Otherwise, specify the numbers of the tests you wish to run.\n");
	printf("Options:\n");
	printf("  -d  Dump core after any failure, for debugging.\n");
	printf("  -k  Keep all temp files.\n");
	printf("      Default: temp files for successful tests deleted.\n");
#ifdef PROGRAM
	printf("  -p <path>  Path to executable to be tested.\n");
	printf("      Default: path taken from " ENVBASE " environment variable.\n");
#endif
	printf("  -q  Quiet.\n");
	printf("  -r <dir>   Path to dir containing reference files.\n");
	printf("      Default: Current directory.\n");
	printf("  -u  Keep running specifies tests until one fails.\n");
	printf("  -v  Verbose.\n");
	printf("Available tests:\n");
	for (i = 0; i < limit; i++)
		printf("  %d: %s\n", i, tests[i].name);
	exit(1);
}

static char *
get_refdir(const char *d)
{
	size_t tried_size, buff_size;
	char *buff, *tried, *pwd = NULL, *p = NULL;

#ifdef PATH_MAX
	buff_size = PATH_MAX;
#else
	buff_size = 8192;
#endif
	buff = calloc(buff_size, 1);
	if (buff == NULL) {
		fprintf(stderr, "Unable to allocate memory\n");
		exit(1);
	}

	/* Allocate a buffer to hold the various directories we checked. */
	tried_size = buff_size * 2;
	tried = calloc(tried_size, 1);
	if (tried == NULL) {
		fprintf(stderr, "Unable to allocate memory\n");
		exit(1);
	}

	/* If a dir was specified, try that */
	if (d != NULL) {
		pwd = NULL;
		snprintf(buff, buff_size, "%s", d);
		p = slurpfile(NULL, "%s/%s", buff, KNOWNREF);
		if (p != NULL) goto success;
		strncat(tried, buff, tried_size - strlen(tried) - 1);
		strncat(tried, "\n", tried_size - strlen(tried) - 1);
		goto failure;
	}

	/* Get the current dir. */
#ifdef PATH_MAX
	pwd = getcwd(NULL, PATH_MAX);/* Solaris getcwd needs the size. */
#else
	pwd = getcwd(NULL, 0);
#endif
	while (pwd[strlen(pwd) - 1] == '\n')
		pwd[strlen(pwd) - 1] = '\0';

	/* Look for a known file. */
	snprintf(buff, buff_size, "%s", pwd);
	p = slurpfile(NULL, "%s/%s", buff, KNOWNREF);
	if (p != NULL) goto success;
	strncat(tried, buff, tried_size - strlen(tried) - 1);
	strncat(tried, "\n", tried_size - strlen(tried) - 1);

	snprintf(buff, buff_size, "%s/test", pwd);
	p = slurpfile(NULL, "%s/%s", buff, KNOWNREF);
	if (p != NULL) goto success;
	strncat(tried, buff, tried_size - strlen(tried) - 1);
	strncat(tried, "\n", tried_size - strlen(tried) - 1);

#if defined(LIBRARY)
	snprintf(buff, buff_size, "%s/%s/test", pwd, LIBRARY);
#else
	snprintf(buff, buff_size, "%s/%s/test", pwd, PROGRAM);
#endif
	p = slurpfile(NULL, "%s/%s", buff, KNOWNREF);
	if (p != NULL) goto success;
	strncat(tried, buff, tried_size - strlen(tried) - 1);
	strncat(tried, "\n", tried_size - strlen(tried) - 1);

#if defined(PROGRAM_ALIAS)
	snprintf(buff, buff_size, "%s/%s/test", pwd, PROGRAM_ALIAS);
	p = slurpfile(NULL, "%s/%s", buff, KNOWNREF);
	if (p != NULL) goto success;
	strncat(tried, buff, tried_size - strlen(tried) - 1);
	strncat(tried, "\n", tried_size - strlen(tried) - 1);
#endif

	if (memcmp(pwd, "/usr/obj", 8) == 0) {
		snprintf(buff, buff_size, "%s", pwd + 8);
		p = slurpfile(NULL, "%s/%s", buff, KNOWNREF);
		if (p != NULL) goto success;
		strncat(tried, buff, tried_size - strlen(tried) - 1);
		strncat(tried, "\n", tried_size - strlen(tried) - 1);

		snprintf(buff, buff_size, "%s/test", pwd + 8);
		p = slurpfile(NULL, "%s/%s", buff, KNOWNREF);
		if (p != NULL) goto success;
		strncat(tried, buff, tried_size - strlen(tried) - 1);
		strncat(tried, "\n", tried_size - strlen(tried) - 1);
	}

failure:
	printf("Unable to locate known reference file %s\n", KNOWNREF);
	printf("  Checked following directories:\n%s\n", tried);
	printf("Use -r option to specify full path to reference directory\n");
#if defined(_WIN32) && !defined(__CYGWIN__) && defined(_DEBUG)
	DebugBreak();
#endif
	exit(1);

success:
	free(p);
	free(pwd);
	free(tried);

	/* Copy result into a fresh buffer to reduce memory usage. */
	p = strdup(buff);
	free(buff);
	return p;
}

int
main(int argc, char **argv)
{
	static const int limit = sizeof(tests) / sizeof(tests[0]);
	int test_set[sizeof(tests) / sizeof(tests[0])];
	int i = 0, j = 0, tests_run = 0, tests_failed = 0, option;
	time_t now;
	char *refdir_alloc = NULL;
	const char *progname;
	char **saved_argv;
	const char *tmp, *option_arg, *p;
	char tmpdir[256], *pwd, *testprogdir, *tmp2 = NULL, *vlevel = NULL;
	char tmpdir_timestamp[256];

	(void)argc; /* UNUSED */

	/* Get the current dir. */
#ifdef PATH_MAX
	pwd = getcwd(NULL, PATH_MAX);/* Solaris getcwd needs the size. */
#else
	pwd = getcwd(NULL, 0);
#endif
	while (pwd[strlen(pwd) - 1] == '\n')
		pwd[strlen(pwd) - 1] = '\0';

#if defined(HAVE__CrtSetReportMode) && !defined(__WATCOMC__)
	/* To stop to run the default invalid parameter handler. */
	_set_invalid_parameter_handler(invalid_parameter_handler);
	/* Disable annoying assertion message box. */
	_CrtSetReportMode(_CRT_ASSERT, 0);
#endif

	/*
	 * Name of this program, used to build root of our temp directory
	 * tree.
	 */
	progname = p = argv[0];
	if ((testprogdir = (char *)malloc(strlen(progname) + 1)) == NULL)
	{
		fprintf(stderr, "ERROR: Out of memory.");
		exit(1);
	}
	strcpy(testprogdir, progname);
	while (*p != '\0') {
		/* Support \ or / dir separators for Windows compat. */
		if (*p == '/' || *p == '\\')
		{
			progname = p + 1;
			i = j;
		}
		++p;
		j++;
	}
	testprogdir[i] = '\0';
#if defined(_WIN32) && !defined(__CYGWIN__)
	if (testprogdir[0] != '/' && testprogdir[0] != '\\' &&
	    !(((testprogdir[0] >= 'a' && testprogdir[0] <= 'z') ||
	       (testprogdir[0] >= 'A' && testprogdir[0] <= 'Z')) &&
		testprogdir[1] == ':' &&
		(testprogdir[2] == '/' || testprogdir[2] == '\\')))
#else
	if (testprogdir[0] != '/')
#endif
	{
		/* Fixup path for relative directories. */
		if ((testprogdir = (char *)realloc(testprogdir,
			strlen(pwd) + 1 + strlen(testprogdir) + 1)) == NULL)
		{
			fprintf(stderr, "ERROR: Out of memory.");
			exit(1);
		}
		memmove(testprogdir + strlen(pwd) + 1, testprogdir,
		    strlen(testprogdir) + 1);
		memcpy(testprogdir, pwd, strlen(pwd));
		testprogdir[strlen(pwd)] = '/';
	}

#ifdef PROGRAM
	/* Get the target program from environment, if available. */
	testprogfile = getenv(ENVBASE);
#endif

	if (getenv("TMPDIR") != NULL)
		tmp = getenv("TMPDIR");
	else if (getenv("TMP") != NULL)
		tmp = getenv("TMP");
	else if (getenv("TEMP") != NULL)
		tmp = getenv("TEMP");
	else if (getenv("TEMPDIR") != NULL)
		tmp = getenv("TEMPDIR");
	else
		tmp = "/tmp";

	/* Allow -d to be controlled through the environment. */
	if (getenv(ENVBASE "_DEBUG") != NULL)
		dump_on_failure = 1;

	/* Allow -v to be controlled through the environment. */
	if (getenv("_VERBOSITY_LEVEL") != NULL)
	{
		vlevel = getenv("_VERBOSITY_LEVEL");
		verbosity = atoi(vlevel);
		if (verbosity < VERBOSITY_SUMMARY_ONLY || verbosity > VERBOSITY_FULL)
		{
			/* Unsupported verbosity levels are silently ignored */
			vlevel = NULL;
			verbosity = VERBOSITY_PASSFAIL;
		}
	}

	/* Get the directory holding test files from environment. */
	refdir = getenv(ENVBASE "_TEST_FILES");

	/*
	 * Parse options, without using getopt(), which isn't available
	 * on all platforms.
	 */
	++argv; /* Skip program name */
	while (*argv != NULL) {
		if (**argv != '-')
			break;
		p = *argv++;
		++p; /* Skip '-' */
		while (*p != '\0') {
			option = *p++;
			option_arg = NULL;
			/* If 'opt' takes an argument, parse that. */
			if (option == 'p' || option == 'r') {
				if (*p != '\0')
					option_arg = p;
				else if (*argv == NULL) {
					fprintf(stderr,
					    "Option -%c requires argument.\n",
					    option);
					usage(progname);
				} else
					option_arg = *argv++;
				p = ""; /* End of this option word. */
			}

			/* Now, handle the option. */
			switch (option) {
			case 'd':
				dump_on_failure = 1;
				break;
			case 'k':
				keep_temp_files = 1;
				break;
			case 'p':
#ifdef PROGRAM
				testprogfile = option_arg;
#else
				fprintf(stderr, "-p option not permitted\n");
				usage(progname);
#endif
				break;
			case 'q':
				if (!vlevel)
					verbosity--;
				break;
			case 'r':
				refdir = option_arg;
				break;
			case 'u':
				until_failure++;
				break;
			case 'v':
				if (!vlevel)
					verbosity++;
				break;
			default:
				fprintf(stderr, "Unrecognized option '%c'\n",
				    option);
				usage(progname);
			}
		}
	}

	/*
	 * Sanity-check that our options make sense.
	 */
#ifdef PROGRAM
	if (testprogfile == NULL)
	{
		if ((tmp2 = (char *)malloc(strlen(testprogdir) + 1 +
			strlen(PROGRAM) + 1)) == NULL)
		{
			fprintf(stderr, "ERROR: Out of memory.");
			exit(1);
		}
		strcpy(tmp2, testprogdir);
		strcat(tmp2, "/");
		strcat(tmp2, PROGRAM);
		testprogfile = tmp2;
	}

	{
		char *testprg;
#if defined(_WIN32) && !defined(__CYGWIN__)
		/* Command.com sometimes rejects '/' separators. */
		testprg = strdup(testprogfile);
		for (i = 0; testprg[i] != '\0'; i++) {
			if (testprg[i] == '/')
				testprg[i] = '\\';
		}
		testprogfile = testprg;
#endif
		/* Quote the name that gets put into shell command lines. */
		testprg = malloc(strlen(testprogfile) + 3);
		strcpy(testprg, "\"");
		strcat(testprg, testprogfile);
		strcat(testprg, "\"");
		testprog = testprg;
	}
#endif

#if !defined(_WIN32) && defined(SIGPIPE)
	{   /* Ignore SIGPIPE signals */
		struct sigaction sa;
		sa.sa_handler = SIG_IGN;
		sigemptyset(&sa.sa_mask);
		sa.sa_flags = 0;
		sigaction(SIGPIPE, &sa, NULL);
	}
#endif

	/*
	 * Create a temp directory for the following tests.
	 * Include the time the tests started as part of the name,
	 * to make it easier to track the results of multiple tests.
	 */
	now = time(NULL);
	for (i = 0; ; i++) {
		strftime(tmpdir_timestamp, sizeof(tmpdir_timestamp),
		    "%Y-%m-%dT%H.%M.%S",
		    localtime(&now));
		sprintf(tmpdir, "%s/%s.%s-%03d", tmp, progname,
		    tmpdir_timestamp, i);
		if (assertMakeDir(tmpdir,0755))
			break;
		if (i >= 999) {
			fprintf(stderr,
			    "ERROR: Unable to create temp directory %s\n",
			    tmpdir);
			exit(1);
		}
	}

	/*
	 * If the user didn't specify a directory for locating
	 * reference files, try to find the reference files in
	 * the "usual places."
	 */
	refdir = refdir_alloc = get_refdir(refdir);

	/*
	 * Banner with basic information.
	 */
	printf("\n");
	printf("If tests fail or crash, details will be in:\n");
	printf("   %s\n", tmpdir);
	printf("\n");
	if (verbosity > VERBOSITY_SUMMARY_ONLY) {
		printf("Reference files will be read from: %s\n", refdir);
#ifdef PROGRAM
		printf("Running tests on: %s\n", testprog);
#endif
		printf("Exercising: ");
		fflush(stdout);
		printf("%s\n", EXTRA_VERSION);
	} else {
		printf("Running ");
		fflush(stdout);
	}

	/*
	 * Run some or all of the individual tests.
	 */
	saved_argv = argv;
	do {
		argv = saved_argv;
		do {
			int test_num;

			test_num = get_test_set(test_set, limit, *argv, tests);
			if (test_num < 0) {
				printf("*** INVALID Test %s\n", *argv);
				free(refdir_alloc);
				free(testprogdir);
				usage(progname);
				return (1);
			}
			for (i = 0; i < test_num; i++) {
				tests_run++;
				if (test_run(test_set[i], tmpdir)) {
					tests_failed++;
					if (until_failure)
						goto finish;
				}
			}
			if (*argv != NULL)
				argv++;
		} while (*argv != NULL);
	} while (until_failure);

finish:
	/* Must be freed after all tests run */
	free(tmp2);
	free(testprogdir);
	free(pwd);

	/*
	 * Report summary statistics.
	 */
	if (verbosity > VERBOSITY_SUMMARY_ONLY) {
		printf("\n");
		printf("Totals:\n");
		printf("  Tests run:         %8d\n", tests_run);
		printf("  Tests failed:      %8d\n", tests_failed);
		printf("  Assertions checked:%8d\n", assertions);
		printf("  Assertions failed: %8d\n", failures);
		printf("  Skips reported:    %8d\n", skips);
	}
	if (failures) {
		printf("\n");
		printf("Failing tests:\n");
		for (i = 0; i < limit; ++i) {
			if (tests[i].failures)
				printf("  %d: %s (%d failures)\n", i,
				    tests[i].name, tests[i].failures);
		}
		printf("\n");
		printf("Details for failing tests: %s\n", tmpdir);
		printf("\n");
	} else {
		if (verbosity == VERBOSITY_SUMMARY_ONLY)
			printf("\n");
		printf("%d tests passed, no failures\n", tests_run);
	}

	free(refdir_alloc);

	/* If the final tmpdir is empty, we can remove it. */
	/* This should be the usual case when all tests succeed. */
	assertChdir("..");
	rmdir(tmpdir);

	return (tests_failed ? 1 : 0);
}

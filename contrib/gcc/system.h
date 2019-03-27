/* Get common system includes and various definitions and declarations based
   on autoconf macros.
   Copyright (C) 1998, 1999, 2000, 2001, 2002, 2003, 2004, 2005
   Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */


#ifndef GCC_SYSTEM_H
#define GCC_SYSTEM_H

/* We must include stdarg.h before stdio.h.  */
#include <stdarg.h>

#ifndef va_copy
# ifdef __va_copy
#   define va_copy(d,s)  __va_copy((d),(s))
# else
#   define va_copy(d,s)  ((d) = (s))
# endif
#endif

#ifdef HAVE_STDDEF_H
# include <stddef.h>
#endif

#include <stdio.h>

/* Define a generic NULL if one hasn't already been defined.  */
#ifndef NULL
#define NULL 0
#endif

/* Use the unlocked open routines from libiberty.  */
#define fopen(PATH,MODE) fopen_unlocked(PATH,MODE)
#define fdopen(FILDES,MODE) fdopen_unlocked(FILDES,MODE)
#define freopen(PATH,MODE,STREAM) freopen_unlocked(PATH,MODE,STREAM)

/* The compiler is not a multi-threaded application and therefore we
   do not have to use the locking functions.  In fact, using the locking
   functions can cause the compiler to be significantly slower under
   I/O bound conditions (such as -g -O0 on very large source files).

   HAVE_DECL_PUTC_UNLOCKED actually indicates whether or not the stdio
   code is multi-thread safe by default.  If it is set to 0, then do
   not worry about using the _unlocked functions.

   fputs_unlocked, fwrite_unlocked, and fprintf_unlocked are
   extensions and need to be prototyped by hand (since we do not
   define _GNU_SOURCE).  */

#if defined HAVE_DECL_PUTC_UNLOCKED && HAVE_DECL_PUTC_UNLOCKED

# ifdef HAVE_PUTC_UNLOCKED
#  undef putc
#  define putc(C, Stream) putc_unlocked (C, Stream)
# endif
# ifdef HAVE_PUTCHAR_UNLOCKED
#  undef putchar
#  define putchar(C) putchar_unlocked (C)
# endif
# ifdef HAVE_GETC_UNLOCKED
#  undef getc
#  define getc(Stream) getc_unlocked (Stream)
# endif
# ifdef HAVE_GETCHAR_UNLOCKED
#  undef getchar
#  define getchar() getchar_unlocked ()
# endif
# ifdef HAVE_FPUTC_UNLOCKED
#  undef fputc
#  define fputc(C, Stream) fputc_unlocked (C, Stream)
# endif

# ifdef HAVE_CLEARERR_UNLOCKED
#  undef clearerr
#  define clearerr(Stream) clearerr_unlocked (Stream)
#  if defined (HAVE_DECL_CLEARERR_UNLOCKED) && !HAVE_DECL_CLEARERR_UNLOCKED
extern void clearerr_unlocked (FILE *);
#  endif
# endif
# ifdef HAVE_FEOF_UNLOCKED
#  undef feof
#  define feof(Stream) feof_unlocked (Stream)
#  if defined (HAVE_DECL_FEOF_UNLOCKED) && !HAVE_DECL_FEOF_UNLOCKED
extern int feof_unlocked (FILE *);
#  endif
# endif
# ifdef HAVE_FILENO_UNLOCKED
#  undef fileno
#  define fileno(Stream) fileno_unlocked (Stream)
#  if defined (HAVE_DECL_FILENO_UNLOCKED) && !HAVE_DECL_FILENO_UNLOCKED
extern int fileno_unlocked (FILE *);
#  endif
# endif
# ifdef HAVE_FFLUSH_UNLOCKED
#  undef fflush
#  define fflush(Stream) fflush_unlocked (Stream)
#  if defined (HAVE_DECL_FFLUSH_UNLOCKED) && !HAVE_DECL_FFLUSH_UNLOCKED
extern int fflush_unlocked (FILE *);
#  endif
# endif
# ifdef HAVE_FGETC_UNLOCKED
#  undef fgetc
#  define fgetc(Stream) fgetc_unlocked (Stream)
#  if defined (HAVE_DECL_FGETC_UNLOCKED) && !HAVE_DECL_FGETC_UNLOCKED
extern int fgetc_unlocked (FILE *);
#  endif
# endif
# ifdef HAVE_FGETS_UNLOCKED
#  undef fgets
#  define fgets(S, n, Stream) fgets_unlocked (S, n, Stream)
#  if defined (HAVE_DECL_FGETS_UNLOCKED) && !HAVE_DECL_FGETS_UNLOCKED
extern char *fgets_unlocked (char *, int, FILE *);
#  endif
# endif
# ifdef HAVE_FPUTS_UNLOCKED
#  undef fputs
#  define fputs(String, Stream) fputs_unlocked (String, Stream)
#  if defined (HAVE_DECL_FPUTS_UNLOCKED) && !HAVE_DECL_FPUTS_UNLOCKED
extern int fputs_unlocked (const char *, FILE *);
#  endif
# endif
# ifdef HAVE_FERROR_UNLOCKED
#  undef ferror
#  define ferror(Stream) ferror_unlocked (Stream)
#  if defined (HAVE_DECL_FERROR_UNLOCKED) && !HAVE_DECL_FERROR_UNLOCKED
extern int ferror_unlocked (FILE *);
#  endif
# endif
# ifdef HAVE_FREAD_UNLOCKED
#  undef fread
#  define fread(Ptr, Size, N, Stream) fread_unlocked (Ptr, Size, N, Stream)
#  if defined (HAVE_DECL_FREAD_UNLOCKED) && !HAVE_DECL_FREAD_UNLOCKED
extern size_t fread_unlocked (void *, size_t, size_t, FILE *);
#  endif
# endif
# ifdef HAVE_FWRITE_UNLOCKED
#  undef fwrite
#  define fwrite(Ptr, Size, N, Stream) fwrite_unlocked (Ptr, Size, N, Stream)
#  if defined (HAVE_DECL_FWRITE_UNLOCKED) && !HAVE_DECL_FWRITE_UNLOCKED
extern size_t fwrite_unlocked (const void *, size_t, size_t, FILE *);
#  endif
# endif
# ifdef HAVE_FPRINTF_UNLOCKED
#  undef fprintf
/* We can't use a function-like macro here because we don't know if
   we have varargs macros.  */
#  define fprintf fprintf_unlocked
#  if defined (HAVE_DECL_FPRINTF_UNLOCKED) && !HAVE_DECL_FPRINTF_UNLOCKED
extern int fprintf_unlocked (FILE *, const char *, ...);
#  endif
# endif

#endif

/* ??? Glibc's fwrite/fread_unlocked macros cause
   "warning: signed and unsigned type in conditional expression".  */
#undef fread_unlocked
#undef fwrite_unlocked

/* There are an extraordinary number of issues with <ctype.h>.
   The last straw is that it varies with the locale.  Use libiberty's
   replacement instead.  */
#include <safe-ctype.h>

#include <sys/types.h>

#include <errno.h>

#if !defined (errno) && defined (HAVE_DECL_ERRNO) && !HAVE_DECL_ERRNO
extern int errno;
#endif

/* Some of glibc's string inlines cause warnings.  Plus we'd rather
   rely on (and therefore test) GCC's string builtins.  */
#define __NO_STRING_INLINES

#ifdef STRING_WITH_STRINGS
# include <string.h>
# include <strings.h>
#else
# ifdef HAVE_STRING_H
#  include <string.h>
# else
#  ifdef HAVE_STRINGS_H
#   include <strings.h>
#  endif
# endif
#endif

#ifdef HAVE_STDLIB_H
# include <stdlib.h>
#endif

/* If we don't have an overriding definition, set SUCCESS_EXIT_CODE and
   FATAL_EXIT_CODE to EXIT_SUCCESS and EXIT_FAILURE respectively,
   or 0 and 1 if those macros are not defined.  */
#ifndef SUCCESS_EXIT_CODE
# ifdef EXIT_SUCCESS
#  define SUCCESS_EXIT_CODE EXIT_SUCCESS
# else
#  define SUCCESS_EXIT_CODE 0
# endif
#endif

#ifndef FATAL_EXIT_CODE
# ifdef EXIT_FAILURE
#  define FATAL_EXIT_CODE EXIT_FAILURE
# else
#  define FATAL_EXIT_CODE 1
# endif
#endif

#define ICE_EXIT_CODE 4

#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#ifdef HAVE_SYS_PARAM_H
# include <sys/param.h>
/* We use this identifier later and it appears in some vendor param.h's.  */
# undef PREFETCH
#endif

#if HAVE_LIMITS_H
# include <limits.h>
#endif

/* Get definitions of HOST_WIDE_INT and HOST_WIDEST_INT.  */
#include "hwint.h"

/* A macro to determine whether a VALUE lies inclusively within a
   certain range without evaluating the VALUE more than once.  This
   macro won't warn if the VALUE is unsigned and the LOWER bound is
   zero, as it would e.g. with "VALUE >= 0 && ...".  Note the LOWER
   bound *is* evaluated twice, and LOWER must not be greater than
   UPPER.  However the bounds themselves can be either positive or
   negative.  */
#define IN_RANGE(VALUE, LOWER, UPPER) \
  ((unsigned HOST_WIDE_INT)((VALUE) - (LOWER)) <= ((UPPER) - (LOWER)))

/* Infrastructure for defining missing _MAX and _MIN macros.  Note that
   macros defined with these cannot be used in #if.  */

/* The extra casts work around common compiler bugs.  */
#define INTTYPE_SIGNED(t) (! ((t) 0 < (t) -1))
/* The outer cast is needed to work around a bug in Cray C 5.0.3.0.
   It is necessary at least when t == time_t.  */
#define INTTYPE_MINIMUM(t) ((t) (INTTYPE_SIGNED (t) \
                             ? ~ (t) 0 << (sizeof(t) * CHAR_BIT - 1) : (t) 0))
#define INTTYPE_MAXIMUM(t) ((t) (~ (t) 0 - INTTYPE_MINIMUM (t)))

/* Use that infrastructure to provide a few constants.  */
#ifndef UCHAR_MAX
# define UCHAR_MAX INTTYPE_MAXIMUM (unsigned char)
#endif

#ifdef TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  ifdef HAVE_TIME_H
#   include <time.h>
#  endif
# endif
#endif

#ifdef HAVE_FCNTL_H
# include <fcntl.h>
#else
# ifdef HAVE_SYS_FILE_H
#  include <sys/file.h>
# endif
#endif

#ifndef SEEK_SET
# define SEEK_SET 0
# define SEEK_CUR 1
# define SEEK_END 2
#endif
#ifndef F_OK
# define F_OK 0
# define X_OK 1
# define W_OK 2
# define R_OK 4
#endif
#ifndef O_RDONLY
# define O_RDONLY 0
#endif
#ifndef O_WRONLY
# define O_WRONLY 1
#endif

/* Some systems define these in, e.g., param.h.  We undefine these names
   here to avoid the warnings.  We prefer to use our definitions since we
   know they are correct.  */

#undef MIN
#undef MAX
#define MIN(X,Y) ((X) < (Y) ? (X) : (Y))
#define MAX(X,Y) ((X) > (Y) ? (X) : (Y))

/* Returns the least number N such that N * Y >= X.  */
#define CEIL(x,y) (((x) + (y) - 1) / (y))

#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif

#ifndef WIFSIGNALED
#define WIFSIGNALED(S) (((S) & 0xff) != 0 && ((S) & 0xff) != 0x7f)
#endif
#ifndef WTERMSIG
#define WTERMSIG(S) ((S) & 0x7f)
#endif
#ifndef WIFEXITED
#define WIFEXITED(S) (((S) & 0xff) == 0)
#endif
#ifndef WEXITSTATUS
#define WEXITSTATUS(S) (((S) & 0xff00) >> 8)
#endif
#ifndef WSTOPSIG
#define WSTOPSIG WEXITSTATUS
#endif
#ifndef WCOREDUMP
#define WCOREDUMP(S) ((S) & WCOREFLG)
#endif
#ifndef WCOREFLG
#define WCOREFLG 0200
#endif

/* The HAVE_DECL_* macros are three-state, undefined, 0 or 1.  If they
   are defined to 0 then we must provide the relevant declaration
   here.  These checks will be in the undefined state while configure
   is running so be careful to test "defined (HAVE_DECL_*)".  */

#if defined (HAVE_DECL_ATOF) && !HAVE_DECL_ATOF
extern double atof (const char *);
#endif

#if defined (HAVE_DECL_ATOL) && !HAVE_DECL_ATOL
extern long atol (const char *);
#endif

#if defined (HAVE_DECL_FREE) && !HAVE_DECL_FREE
extern void free (void *);
#endif

#if defined (HAVE_DECL_GETCWD) && !HAVE_DECL_GETCWD
extern char *getcwd (char *, size_t);
#endif

#if defined (HAVE_DECL_GETENV) && !HAVE_DECL_GETENV
extern char *getenv (const char *);
#endif

#if defined (HAVE_DECL_GETOPT) && !HAVE_DECL_GETOPT
extern int getopt (int, char * const *, const char *);
#endif

#if defined (HAVE_DECL_GETPAGESIZE) && !HAVE_DECL_GETPAGESIZE
extern int getpagesize (void);
#endif

#if defined (HAVE_DECL_GETWD) && !HAVE_DECL_GETWD
extern char *getwd (char *);
#endif

#if defined (HAVE_DECL_SBRK) && !HAVE_DECL_SBRK
extern void *sbrk (int);
#endif

#if defined (HAVE_DECL_STRSTR) && !HAVE_DECL_STRSTR
extern char *strstr (const char *, const char *);
#endif

#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif

#if defined (HAVE_DECL_MALLOC) && !HAVE_DECL_MALLOC
extern void *malloc (size_t);
#endif

#if defined (HAVE_DECL_CALLOC) && !HAVE_DECL_CALLOC
extern void *calloc (size_t, size_t);
#endif

#if defined (HAVE_DECL_REALLOC) && !HAVE_DECL_REALLOC
extern void *realloc (void *, size_t);
#endif

/* If the system doesn't provide strsignal, we get it defined in
   libiberty but no declaration is supplied.  */
#if !defined (HAVE_STRSIGNAL) \
    || (defined (HAVE_DECL_STRSIGNAL) && !HAVE_DECL_STRSIGNAL)
# ifndef strsignal
extern const char *strsignal (int);
# endif
#endif

#ifdef HAVE_GETRLIMIT
# if defined (HAVE_DECL_GETRLIMIT) && !HAVE_DECL_GETRLIMIT
#  ifndef getrlimit
struct rlimit;
extern int getrlimit (int, struct rlimit *);
#  endif
# endif
#endif

#ifdef HAVE_SETRLIMIT
# if defined (HAVE_DECL_SETRLIMIT) && !HAVE_DECL_SETRLIMIT
#  ifndef setrlimit
struct rlimit;
extern int setrlimit (int, const struct rlimit *);
#  endif
# endif
#endif

#if defined (HAVE_DECL_ABORT) && !HAVE_DECL_ABORT
extern void abort (void);
#endif

#if defined (HAVE_DECL_SNPRINTF) && !HAVE_DECL_SNPRINTF
extern int snprintf (char *, size_t, const char *, ...);
#endif

#if defined (HAVE_DECL_VSNPRINTF) && !HAVE_DECL_VSNPRINTF
extern int vsnprintf(char *, size_t, const char *, va_list);
#endif

/* 1 if we have C99 designated initializers.  */
#if !defined(HAVE_DESIGNATED_INITIALIZERS)
#define HAVE_DESIGNATED_INITIALIZERS \
  ((GCC_VERSION >= 2007) || (__STDC_VERSION__ >= 199901L))
#endif

#if HAVE_SYS_STAT_H
# include <sys/stat.h>
#endif

/* Test if something is a normal file.  */
#ifndef S_ISREG
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#endif

/* Test if something is a directory.  */
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif

/* Test if something is a character special file.  */
#ifndef S_ISCHR
#define S_ISCHR(m) (((m) & S_IFMT) == S_IFCHR)
#endif

/* Test if something is a block special file.  */
#ifndef S_ISBLK
#define S_ISBLK(m) (((m) & S_IFMT) == S_IFBLK)
#endif

/* Test if something is a socket.  */
#ifndef S_ISSOCK
# ifdef S_IFSOCK
#   define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)
# else
#   define S_ISSOCK(m) 0
# endif
#endif

/* Test if something is a FIFO.  */
#ifndef S_ISFIFO
# ifdef S_IFIFO
#  define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
# else
#  define S_ISFIFO(m) 0
# endif
#endif

/* Define well known filenos if the system does not define them.  */
#ifndef STDIN_FILENO
# define STDIN_FILENO   0
#endif
#ifndef STDOUT_FILENO
# define STDOUT_FILENO  1
#endif
#ifndef STDERR_FILENO
# define STDERR_FILENO  2
#endif

/* Some systems have mkdir that takes a single argument.  */
#ifdef MKDIR_TAKES_ONE_ARG
# define mkdir(a,b) mkdir(a)
#endif

/* By default, colon separates directories in a path.  */
#ifndef PATH_SEPARATOR
#define PATH_SEPARATOR ':'
#endif

/* Filename handling macros.  */
#include "filenames.h"

/* These should be phased out in favor of IS_DIR_SEPARATOR, where possible.  */
#ifndef DIR_SEPARATOR
# define DIR_SEPARATOR '/'
# ifdef HAVE_DOS_BASED_FILE_SYSTEM
#  define DIR_SEPARATOR_2 '\\'
# endif
#endif

/* Get libiberty declarations.  */
#include "libiberty.h"

/* Provide a default for the HOST_BIT_BUCKET.
   This suffices for POSIX-like hosts.  */

#ifndef HOST_BIT_BUCKET
#define HOST_BIT_BUCKET "/dev/null"
#endif

/* Be conservative and only use enum bitfields with GCC.
   FIXME: provide a complete autoconf test for buggy enum bitfields.  */

#if (GCC_VERSION > 2000)
#define ENUM_BITFIELD(TYPE) __extension__ enum TYPE
#else
#define ENUM_BITFIELD(TYPE) unsigned int
#endif

#ifndef offsetof
#define offsetof(TYPE, MEMBER)	((size_t) &((TYPE *) 0)->MEMBER)
#endif

/* Various error reporting routines want to use __FUNCTION__.  */
#if (GCC_VERSION < 2007)
#ifndef __FUNCTION__
#define __FUNCTION__ "?"
#endif /* ! __FUNCTION__ */
#endif

/* __builtin_expect(A, B) evaluates to A, but notifies the compiler that
   the most likely value of A is B.  This feature was added at some point
   between 2.95 and 3.0.  Let's use 3.0 as the lower bound for now.  */
#if (GCC_VERSION < 3000)
#define __builtin_expect(a, b) (a)
#endif

/* Redefine abort to report an internal error w/o coredump, and
   reporting the location of the error in the source file.  */
extern void fancy_abort (const char *, int, const char *) ATTRIBUTE_NORETURN;
#define abort() fancy_abort (__FILE__, __LINE__, __FUNCTION__)

/* Use gcc_assert(EXPR) to test invariants.  */
#if ENABLE_ASSERT_CHECKING
#define gcc_assert(EXPR) 						\
   ((void)(!(EXPR) ? fancy_abort (__FILE__, __LINE__, __FUNCTION__), 0 : 0))
#else
/* Include EXPR, so that unused variable warnings do not occur.  */
#define gcc_assert(EXPR) ((void)(0 && (EXPR)))
#endif

/* Use gcc_unreachable() to mark unreachable locations (like an
   unreachable default case of a switch.  Do not use gcc_assert(0).  */
#define gcc_unreachable() (fancy_abort (__FILE__, __LINE__, __FUNCTION__))

/* Provide a fake boolean type.  We make no attempt to use the
   C99 _Bool, as it may not be available in the bootstrap compiler,
   and even if it is, it is liable to be buggy.
   This must be after all inclusion of system headers, as some of
   them will mess us up.  */

#undef TRUE
#undef FALSE

#ifdef __cplusplus
  /* Obsolete.  */
# define TRUE true
# define FALSE false
#else /* !__cplusplus */
# undef bool
# undef true
# undef false

# define bool unsigned char
# define true 1
# define false 0

  /* Obsolete.  */
# define TRUE true
# define FALSE false
#endif /* !__cplusplus */

/* Get definition of double_int.  */
#include "double-int.h"

/* Some compilers do not allow the use of unsigned char in bitfields.  */
#define BOOL_BITFIELD unsigned int

/* As the last action in this file, we poison the identifiers that
   shouldn't be used.  Note, luckily gcc-3.0's token-based integrated
   preprocessor won't trip on poisoned identifiers that arrive from
   the expansion of macros.  E.g. #define strrchr rindex, won't error
   if rindex is poisoned after this directive is issued and later on
   strrchr is called.

   Note: We define bypass macros for the few cases where we really
   want to use the libc memory allocation routines.  Otherwise we
   insist you use the "x" versions from libiberty.  */

#define really_call_malloc malloc
#define really_call_calloc calloc
#define really_call_realloc realloc

#if defined(FLEX_SCANNER) || defined(YYBISON) || defined(YYBYACC)
/* Flex and bison use malloc and realloc.  Yuk.  Note that this means
   really_call_* cannot be used in a .l or .y file.  */
#define malloc xmalloc
#define realloc xrealloc
#endif

#if (GCC_VERSION >= 3000)

/* Note autoconf checks for prototype declarations and includes
   system.h while doing so.  Only poison these tokens if actually
   compiling gcc, so that the autoconf declaration tests for malloc
   etc don't spuriously fail.  */
#ifdef IN_GCC
#undef calloc
#undef strdup
 #pragma GCC poison calloc strdup

#if !defined(FLEX_SCANNER) && !defined(YYBISON)
#undef malloc
#undef realloc
 #pragma GCC poison malloc realloc
#endif

/* Old target macros that have moved to the target hooks structure.  */
 #pragma GCC poison ASM_OPEN_PAREN ASM_CLOSE_PAREN			\
	FUNCTION_PROLOGUE FUNCTION_EPILOGUE				\
	FUNCTION_END_PROLOGUE FUNCTION_BEGIN_EPILOGUE			\
	DECL_MACHINE_ATTRIBUTES COMP_TYPE_ATTRIBUTES INSERT_ATTRIBUTES	\
	VALID_MACHINE_DECL_ATTRIBUTE VALID_MACHINE_TYPE_ATTRIBUTE	\
	SET_DEFAULT_TYPE_ATTRIBUTES SET_DEFAULT_DECL_ATTRIBUTES		\
	MERGE_MACHINE_TYPE_ATTRIBUTES MERGE_MACHINE_DECL_ATTRIBUTES	\
	MD_INIT_BUILTINS MD_EXPAND_BUILTIN ASM_OUTPUT_CONSTRUCTOR	\
	ASM_OUTPUT_DESTRUCTOR SIGNED_CHAR_SPEC MAX_CHAR_TYPE_SIZE	\
	WCHAR_UNSIGNED UNIQUE_SECTION SELECT_SECTION SELECT_RTX_SECTION	\
	ENCODE_SECTION_INFO STRIP_NAME_ENCODING ASM_GLOBALIZE_LABEL	\
	ASM_OUTPUT_MI_THUNK CONST_COSTS RTX_COSTS DEFAULT_RTX_COSTS	\
	ADDRESS_COST MACHINE_DEPENDENT_REORG ASM_FILE_START ASM_FILE_END \
	ASM_SIMPLIFY_DWARF_ADDR INIT_TARGET_OPTABS INIT_SUBTARGET_OPTABS \
	INIT_GOFAST_OPTABS MULSI3_LIBCALL MULDI3_LIBCALL DIVSI3_LIBCALL \
	DIVDI3_LIBCALL UDIVSI3_LIBCALL UDIVDI3_LIBCALL MODSI3_LIBCALL	\
	MODDI3_LIBCALL UMODSI3_LIBCALL UMODDI3_LIBCALL BUILD_VA_LIST_TYPE \
	PRETEND_OUTGOING_VARARGS_NAMED STRUCT_VALUE_INCOMING_REGNUM	\
	ASM_OUTPUT_SECTION_NAME PROMOTE_FUNCTION_ARGS			\
	STRUCT_VALUE_INCOMING STRICT_ARGUMENT_NAMING			\
	PROMOTE_FUNCTION_RETURN PROMOTE_PROTOTYPES STRUCT_VALUE_REGNUM	\
	SETUP_INCOMING_VARARGS EXPAND_BUILTIN_SAVEREGS			\
	DEFAULT_SHORT_ENUMS SPLIT_COMPLEX_ARGS MD_ASM_CLOBBERS		\
	HANDLE_PRAGMA_REDEFINE_EXTNAME HANDLE_PRAGMA_EXTERN_PREFIX	\
	MUST_PASS_IN_STACK FUNCTION_ARG_PASS_BY_REFERENCE               \
        VECTOR_MODE_SUPPORTED_P TARGET_SUPPORTS_HIDDEN 			\
	FUNCTION_ARG_PARTIAL_NREGS ASM_OUTPUT_DWARF_DTPREL		\
	ALLOCATE_INITIAL_VALUE

/* Other obsolete target macros, or macros that used to be in target
   headers and were not used, and may be obsolete or may never have
   been used.  */
 #pragma GCC poison INT_ASM_OP ASM_OUTPUT_EH_REGION_BEG CPP_PREDEFINES	   \
	ASM_OUTPUT_EH_REGION_END ASM_OUTPUT_LABELREF_AS_INT SMALL_STACK    \
	DOESNT_NEED_UNWINDER EH_TABLE_LOOKUP OBJC_SELECTORS_WITHOUT_LABELS \
	OMIT_EH_TABLE EASY_DIV_EXPR IMPLICIT_FIX_EXPR			   \
	LONGJMP_RESTORE_FROM_STACK MAX_INT_TYPE_SIZE ASM_IDENTIFY_GCC	   \
	STDC_VALUE TRAMPOLINE_ALIGN ASM_IDENTIFY_GCC_AFTER_SOURCE	   \
	SLOW_ZERO_EXTEND SUBREG_REGNO_OFFSET DWARF_LINE_MIN_INSTR_LENGTH   \
	TRADITIONAL_RETURN_FLOAT NO_BUILTIN_SIZE_TYPE			   \
	NO_BUILTIN_PTRDIFF_TYPE NO_BUILTIN_WCHAR_TYPE NO_BUILTIN_WINT_TYPE \
	BLOCK_PROFILER BLOCK_PROFILER_CODE FUNCTION_BLOCK_PROFILER	   \
	FUNCTION_BLOCK_PROFILER_EXIT MACHINE_STATE_SAVE			   \
	MACHINE_STATE_RESTORE SCCS_DIRECTIVE SECTION_ASM_OP BYTEORDER	   \
	ASM_OUTPUT_DEFINE_LABEL_DIFFERENCE_SYMBOL HOST_WORDS_BIG_ENDIAN	   \
	OBJC_PROLOGUE ALLOCATE_TRAMPOLINE HANDLE_PRAGMA ROUND_TYPE_SIZE	   \
	ROUND_TYPE_SIZE_UNIT CONST_SECTION_ASM_OP CRT_GET_RFIB_TEXT	   \
	DBX_LBRAC_FIRST DBX_OUTPUT_ENUM DBX_OUTPUT_SOURCE_FILENAME	   \
	DBX_WORKING_DIRECTORY INSN_CACHE_DEPTH INSN_CACHE_SIZE		   \
	INSN_CACHE_LINE_WIDTH INIT_SECTION_PREAMBLE NEED_ATEXIT ON_EXIT	   \
	EXIT_BODY OBJECT_FORMAT_ROSE MULTIBYTE_CHARS MAP_CHARACTER	   \
	LIBGCC_NEEDS_DOUBLE FINAL_PRESCAN_LABEL DEFAULT_CALLER_SAVES	   \
	LOAD_ARGS_REVERSED MAX_INTEGER_COMPUTATION_MODE			   \
	CONVERT_HARD_REGISTER_TO_SSA_P ASM_OUTPUT_MAIN_SOURCE_FILENAME	   \
	FIRST_INSN_ADDRESS TEXT_SECTION SHARED_BSS_SECTION_ASM_OP	   \
	PROMOTED_MODE EXPAND_BUILTIN_VA_END				   \
	LINKER_DOES_NOT_WORK_WITH_DWARF2 FUNCTION_ARG_KEEP_AS_REFERENCE	   \
	GIV_SORT_CRITERION MAX_LONG_TYPE_SIZE MAX_LONG_DOUBLE_TYPE_SIZE	   \
	MAX_WCHAR_TYPE_SIZE SHARED_SECTION_ASM_OP INTEGRATE_THRESHOLD      \
	FINAL_REG_PARM_STACK_SPACE MAYBE_REG_PARM_STACK_SPACE		   \
	TRADITIONAL_PIPELINE_INTERFACE DFA_PIPELINE_INTERFACE		   \
	DBX_OUTPUT_STANDARD_TYPES BUILTIN_SETJMP_FRAME_VALUE		   \
	SUNOS4_SHARED_LIBRARIES PROMOTE_FOR_CALL_ONLY			   \
	SPACE_AFTER_L_OPTION NO_RECURSIVE_FUNCTION_CSE			   \
	DEFAULT_MAIN_RETURN TARGET_MEM_FUNCTIONS EXPAND_BUILTIN_VA_ARG	   \
	COLLECT_PARSE_FLAG DWARF2_GENERATE_TEXT_SECTION_LABEL WINNING_GDB  \
	ASM_OUTPUT_FILENAME ASM_OUTPUT_SOURCE_LINE FILE_NAME_JOINER	   \
	GDB_INV_REF_REGPARM_STABS_LETTER DBX_MEMPARM_STABS_LETTER	   \
	PUT_SDB_SRC_FILE STABS_GCC_MARKER DBX_OUTPUT_FUNCTION_END	   \
	DBX_OUTPUT_GCC_MARKER DBX_FINISH_SYMBOL SDB_GENERATE_FAKE	   \
	NON_SAVING_SETJMP TARGET_LATE_RTL_PROLOGUE_EPILOGUE		   \
	CASE_DROPS_THROUGH TARGET_BELL TARGET_BS TARGET_CR TARGET_DIGIT0   \
        TARGET_ESC TARGET_FF TARGET_NEWLINE TARGET_TAB TARGET_VT	   \
        LINK_LIBGCC_SPECIAL DONT_ACCESS_GBLS_AFTER_EPILOGUE		   \
	TARGET_OPTIONS TARGET_SWITCHES EXTRA_CC_MODES FINALIZE_PIC	   \
	PREDICATE_CODES SPECIAL_MODE_PREDICATES HOST_PTR_PRINTF		   \
	EXTRA_SECTIONS EXTRA_SECTION_FUNCTIONS READONLY_DATA_SECTION	   \
	TARGET_ASM_EXCEPTION_SECTION TARGET_ASM_EH_FRAME_SECTION	   \
	SMALL_ARG_MAX ASM_OUTPUT_SHARED_BSS ASM_OUTPUT_SHARED_COMMON	   \
	ASM_OUTPUT_SHARED_LOCAL UNALIGNED_WORD_ASM_OP

/* Hooks that are no longer used.  */
 #pragma GCC poison LANG_HOOKS_FUNCTION_MARK LANG_HOOKS_FUNCTION_FREE	\
	LANG_HOOKS_MARK_TREE LANG_HOOKS_INSERT_DEFAULT_ATTRIBUTES \
	LANG_HOOKS_TREE_INLINING_ESTIMATE_NUM_INSNS \
	LANG_HOOKS_PUSHLEVEL LANG_HOOKS_SET_BLOCK \
	LANG_HOOKS_MAYBE_BUILD_CLEANUP LANG_HOOKS_UPDATE_DECL_AFTER_SAVING \
	LANG_HOOKS_POPLEVEL LANG_HOOKS_TRUTHVALUE_CONVERSION

/* Libiberty macros that are no longer used in GCC.  */
#undef ANSI_PROTOTYPES
#undef PTR_CONST
#undef LONG_DOUBLE
#undef VPARAMS
#undef VA_OPEN
#undef VA_FIXEDARG
#undef VA_CLOSE
#undef VA_START
 #pragma GCC poison ANSI_PROTOTYPES PTR_CONST LONG_DOUBLE VPARAMS VA_OPEN \
  VA_FIXEDARG VA_CLOSE VA_START
#endif /* IN_GCC */

/* Note: not all uses of the `index' token (e.g. variable names and
   structure members) have been eliminated.  */
#undef bcopy
#undef bzero
#undef bcmp
#undef rindex
 #pragma GCC poison bcopy bzero bcmp rindex

#endif /* GCC >= 3.0 */

#endif /* ! GCC_SYSTEM_H */

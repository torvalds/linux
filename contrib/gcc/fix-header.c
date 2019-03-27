/* fix-header.c - Make C header file suitable for C++.
   Copyright (C) 1993, 1994, 1995, 1996, 1997, 1998,
   1999, 2000, 2001, 2002, 2003, 2004, 2006 Free Software Foundation, Inc.

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.  */

/* This program massages a system include file (such as stdio.h),
   into a form that is compatible with GNU C and GNU C++.

   * extern "C" { ... } braces are added (inside #ifndef __cplusplus),
   if they seem to be needed.  These prevent C++ compilers from name
   mangling the functions inside the braces.

   * If an old-style incomplete function declaration is seen (without
   an argument list), and it is a "standard" function listed in
   the file sys-protos.h (and with a non-empty argument list), then
   the declaration is converted to a complete prototype by replacing
   the empty parameter list with the argument list from sys-protos.h.

   * The program can be given a list of (names of) required standard
   functions (such as fclose for stdio.h).  If a required function
   is not seen in the input, then a prototype for it will be
   written to the output.

   * If all of the non-comment code of the original file is protected
   against multiple inclusion:
	#ifndef FOO
	#define FOO
	<body of include file>
	#endif
   then extra matter added to the include file is placed inside the <body>.

   * If the input file is OK (nothing needs to be done);
   the output file is not written (nor removed if it exists).

   There are also some special actions that are done for certain
   well-known standard include files:

   * If argv[1] is "sys/stat.h", the Posix.1 macros
   S_ISBLK, S_ISCHR, S_ISDIR, S_ISFIFO, S_ISLNK, S_ISREG are added if
   they were missing, and the corresponding "traditional" S_IFxxx
   macros were defined.

   * If argv[1] is "errno.h", errno is declared if it was missing.

   * TODO:  The input file should be read complete into memory, because:
   a) it needs to be scanned twice anyway, and
   b) it would be nice to allow update in place.

   Usage:
	fix-header FOO.H INFILE.H OUTFILE.H [OPTIONS]
   where:
   * FOO.H is the relative file name of the include file,
   as it would be #include'd by a C file.  (E.g. stdio.h)
   * INFILE.H is a full pathname for the input file (e.g. /usr/include/stdio.h)
   * OUTFILE.H is the full pathname for where to write the output file,
   if anything needs to be done.  (e.g. ./include/stdio.h)
   * OPTIONS can be -D or -I switches as you would pass to cpp.

   Written by Per Bothner <bothner@cygnus.com>, July 1993.  */

#include "bconfig.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "obstack.h"
#include "scan.h"
#include "cpplib.h"
#include "c-incpath.h"
#include "errors.h"

#ifdef TARGET_EXTRA_INCLUDES
void
TARGET_EXTRA_INCLUDES (const char *sysroot ATTRIBUTE_UNUSED,
		       const char *iprefix ATTRIBUTE_UNUSED,
		       int stdinc ATTRIBUTE_UNUSED)
{
}
#endif

#ifdef TARGET_EXTRA_PRE_INCLUDES 
void
TARGET_EXTRA_PRE_INCLUDES (const char *sysroot ATTRIBUTE_UNUSED,
			   const char *iprefix ATTRIBUTE_UNUSED,
			   int stdinc ATTRIBUTE_UNUSED)
{
}
#endif

struct line_maps line_table;

sstring buf;

int verbose = 0;
int partial_count = 0;
int warnings = 0;

#if ADD_MISSING_EXTERN_C
int missing_extern_C_count = 0;
#endif

#include "xsys-protos.h"

#ifdef FIXPROTO_IGNORE_LIST
/* This is a currently unused feature.  */

/* List of files and directories to ignore.
   A directory name (ending in '/') means ignore anything in that
   directory.  (It might be more efficient to do directory pruning
   earlier in fixproto, but this is simpler and easier to customize.) */

static const char *const files_to_ignore[] = {
  "X11/",
  FIXPROTO_IGNORE_LIST
  0
};
#endif

char *inf_buffer;
char *inf_limit;
char *inf_ptr;
static const char *cur_file;

/* Certain standard files get extra treatment */

enum special_file
{
  no_special,
#ifdef errno_h
#undef errno_h
#endif
  errno_h,
#ifdef stdio_h
#undef stdio_h
#endif
  stdio_h,
#ifdef stdlib_h
#undef stdlib_h
#endif
  stdlib_h,
#ifdef sys_stat_h
#undef sys_stat_h
#endif
  sys_stat_h
};

/* A NAMELIST is a sequence of names, separated by '\0', and terminated
   by an empty name (i.e. by "\0\0").  */

typedef const char *namelist;

/* The following macros provide the bits for symbol_flags.  */
typedef int symbol_flags;

/* Used to mark names defined in the ANSI/ISO C standard.  */
#define ANSI_SYMBOL 1

/* We no longer massage include files for POSIX or XOPEN symbols,
   as there are now several versions of the POSIX and XOPEN standards,
   and it would be a maintenance nightmare for us to track them all.
   Better to be compatible with the system include files.  */
/*#define ADD_MISSING_POSIX 1 */
/*#define ADD_MISSING_XOPEN 1 */

#if ADD_MISSING_POSIX
/* Used to mark names defined in the Posix.1 or Posix.2 standard.  */
#define POSIX1_SYMBOL 2
#define POSIX2_SYMBOL 4
#else
#define POSIX1_SYMBOL 0
#define POSIX2_SYMBOL 0
#endif

#if ADD_MISSING_XOPEN
/* Used to mark names defined in X/Open Portability Guide.  */
#define XOPEN_SYMBOL 8
/* Used to mark names defined in X/Open UNIX Extensions.  */
#define XOPEN_EXTENDED_SYMBOL 16
#else
#define XOPEN_SYMBOL 0
#define XOPEN_EXTENDED_SYMBOL 0
#endif

/* Used to indicate names that are not functions */
#define MACRO_SYMBOL 512

struct symbol_list {
  symbol_flags flags;
  namelist names;
};

#define SYMBOL_TABLE_SIZE 10
struct symbol_list symbol_table[SYMBOL_TABLE_SIZE];
int cur_symbol_table_size;

static void add_symbols (symbol_flags, namelist);
static struct fn_decl *lookup_std_proto (const char *, int);
static void write_lbrac (void);
static void recognized_macro (const char *);
static void check_macro_names (cpp_reader *, namelist);
static void read_scan_file (char *, int, char **);
static void write_rbrac (void);
static int inf_skip_spaces (int);
static int inf_read_upto (sstring *, int);
static int inf_scan_ident (sstring *, int);
static int check_protection (int *, int *);
static void cb_file_change (cpp_reader *, const struct line_map *);

static void
add_symbols (symbol_flags flags, namelist names)
{
  symbol_table[cur_symbol_table_size].flags = flags;
  symbol_table[cur_symbol_table_size].names = names;
  cur_symbol_table_size++;
  if (cur_symbol_table_size >= SYMBOL_TABLE_SIZE)
    fatal ("too many calls to add_symbols");
  symbol_table[cur_symbol_table_size].names = NULL; /* Termination.  */
}

struct std_include_entry {
  const char *const name;
  const symbol_flags flags;
  const namelist names;
};

const char NONE[] = "";  /* The empty namelist.  */

/* Special name to indicate a continuation line in std_include_table.  */
const char CONTINUED[] = "";

const struct std_include_entry *include_entry;

const struct std_include_entry std_include_table [] = {
  { "ctype.h", ANSI_SYMBOL,
      "isalnum\0isalpha\0iscntrl\0isdigit\0isgraph\0islower\0\
isprint\0ispunct\0isspace\0isupper\0isxdigit\0tolower\0toupper\0" },

  { "dirent.h", POSIX1_SYMBOL, "closedir\0opendir\0readdir\0rewinddir\0"},

  { "errno.h", ANSI_SYMBOL|MACRO_SYMBOL, "errno\0" },

  /* ANSI_SYMBOL is wrong, but ...  */
  { "curses.h", ANSI_SYMBOL, "box\0delwin\0endwin\0getcurx\0getcury\0initscr\0\
mvcur\0mvwprintw\0mvwscanw\0newwin\0overlay\0overwrite\0\
scroll\0subwin\0touchwin\0waddstr\0wclear\0wclrtobot\0wclrtoeol\0\
waddch\0wdelch\0wdeleteln\0werase\0wgetch\0wgetstr\0winsch\0winsertln\0\
wmove\0wprintw\0wrefresh\0wscanw\0wstandend\0wstandout\0" },

  { "fcntl.h", POSIX1_SYMBOL, "creat\0fcntl\0open\0" },

  /* Maybe also "getgrent fgetgrent setgrent endgrent" */
  { "grp.h", POSIX1_SYMBOL, "getgrgid\0getgrnam\0" },

/*{ "limit.h", ... provided by gcc }, */

  { "locale.h", ANSI_SYMBOL, "localeconv\0setlocale\0" },

  { "math.h", ANSI_SYMBOL,
      "acos\0asin\0atan\0atan2\0ceil\0cos\0cosh\0exp\0\
fabs\0floor\0fmod\0frexp\0ldexp\0log10\0log\0modf\0pow\0sin\0sinh\0sqrt\0\
tan\0tanh\0" },

  { CONTINUED, ANSI_SYMBOL|MACRO_SYMBOL, "HUGE_VAL\0" },

  { "pwd.h", POSIX1_SYMBOL, "getpwnam\0getpwuid\0" },

  /* Left out siglongjmp sigsetjmp - these depend on sigjmp_buf.  */
  { "setjmp.h", ANSI_SYMBOL, "longjmp\0setjmp\0" },

  /* Left out signal() - its prototype is too complex for us!
     Also left out "sigaction sigaddset sigdelset sigemptyset
     sigfillset sigismember sigpending sigprocmask sigsuspend"
     because these need sigset_t or struct sigaction.
     Most systems that provide them will also declare them.  */
  { "signal.h", ANSI_SYMBOL, "raise\0" },
  { CONTINUED, POSIX1_SYMBOL, "kill\0" },

  { "stdio.h", ANSI_SYMBOL,
      "clearerr\0fclose\0feof\0ferror\0fflush\0fgetc\0fgetpos\0\
fgets\0fopen\0fprintf\0fputc\0fputs\0fread\0freopen\0fscanf\0fseek\0\
fsetpos\0ftell\0fwrite\0getc\0getchar\0gets\0perror\0\
printf\0putc\0putchar\0puts\0remove\0rename\0rewind\0scanf\0setbuf\0\
setvbuf\0sprintf\0sscanf\0vprintf\0vsprintf\0vfprintf\0tmpfile\0\
tmpnam\0ungetc\0" },
  { CONTINUED, POSIX1_SYMBOL, "fdopen\0fileno\0" },
  { CONTINUED, POSIX2_SYMBOL, "pclose\0popen\0" },  /* I think ...  */
/* Should perhaps also handle NULL, EOF, ... ? */

  /* "div ldiv", - ignored because these depend on div_t, ldiv_t
     ignore these: "mblen mbstowcs mbstowc wcstombs wctomb"
     Left out getgroups, because SunOS4 has incompatible BSD and SVR4 versions.
     Should perhaps also add NULL */
  { "stdlib.h", ANSI_SYMBOL,
      "abort\0abs\0atexit\0atof\0atoi\0atol\0bsearch\0calloc\0\
exit\0free\0getenv\0labs\0malloc\0qsort\0rand\0realloc\0\
srand\0strtod\0strtol\0strtoul\0system\0" },
  { CONTINUED, ANSI_SYMBOL|MACRO_SYMBOL, "EXIT_FAILURE\0EXIT_SUCCESS\0" },
  { CONTINUED, POSIX1_SYMBOL, "putenv\0" },

  { "string.h", ANSI_SYMBOL, "memchr\0memcmp\0memcpy\0memmove\0memset\0\
strcat\0strchr\0strcmp\0strcoll\0strcpy\0strcspn\0strerror\0\
strlen\0strncat\0strncmp\0strncpy\0strpbrk\0strrchr\0strspn\0strstr\0\
strtok\0strxfrm\0" },
/* Should perhaps also add NULL and size_t */

  { "strings.h", XOPEN_EXTENDED_SYMBOL,
      "bcmp\0bcopy\0bzero\0ffs\0index\0rindex\0strcasecmp\0strncasecmp\0" },

  { "strops.h", XOPEN_EXTENDED_SYMBOL, "ioctl\0" },

  /* Actually, XPG4 does not seem to have <sys/ioctl.h>, but defines
     ioctl in <strops.h>.  However, many systems have it is sys/ioctl.h,
     and many systems do have <sys/ioctl.h> but not <strops.h>.  */
  { "sys/ioctl.h", XOPEN_EXTENDED_SYMBOL, "ioctl\0" },

  { "sys/socket.h", XOPEN_EXTENDED_SYMBOL, "socket\0" },

  { "sys/stat.h", POSIX1_SYMBOL,
      "chmod\0fstat\0mkdir\0mkfifo\0stat\0lstat\0umask\0" },
  { CONTINUED, POSIX1_SYMBOL|MACRO_SYMBOL,
      "S_ISDIR\0S_ISBLK\0S_ISCHR\0S_ISFIFO\0S_ISREG\0S_ISLNK\0S_IFDIR\0\
S_IFBLK\0S_IFCHR\0S_IFIFO\0S_IFREG\0S_IFLNK\0" },
  { CONTINUED, XOPEN_EXTENDED_SYMBOL, "fchmod\0" },

#if 0
/* How do we handle fd_set? */
  { "sys/time.h", XOPEN_EXTENDED_SYMBOL, "select\0" },
  { "sys/select.h", XOPEN_EXTENDED_SYMBOL /* fake */, "select\0" },
#endif

  { "sys/times.h", POSIX1_SYMBOL, "times\0" },
  /* "sys/types.h" add types (not in old g++-include) */

  { "sys/utsname.h", POSIX1_SYMBOL, "uname\0" },

  { "sys/wait.h", POSIX1_SYMBOL, "wait\0waitpid\0" },
  { CONTINUED, POSIX1_SYMBOL|MACRO_SYMBOL,
      "WEXITSTATUS\0WIFEXITED\0WIFSIGNALED\0WIFSTOPPED\0WSTOPSIG\0\
WTERMSIG\0WNOHANG\0WNOTRACED\0" },

  { "tar.h", POSIX1_SYMBOL, NONE },

  { "termios.h", POSIX1_SYMBOL,
      "cfgetispeed\0cfgetospeed\0cfsetispeed\0cfsetospeed\0tcdrain\0tcflow\0tcflush\0tcgetattr\0tcsendbreak\0tcsetattr\0" },

  { "time.h", ANSI_SYMBOL,
      "asctime\0clock\0ctime\0difftime\0gmtime\0localtime\0mktime\0strftime\0time\0" },
  { CONTINUED, POSIX1_SYMBOL, "tzset\0" },

  { "unistd.h", POSIX1_SYMBOL,
      "_exit\0access\0alarm\0chdir\0chown\0close\0ctermid\0cuserid\0\
dup\0dup2\0execl\0execle\0execlp\0execv\0execve\0execvp\0fork\0fpathconf\0\
getcwd\0getegid\0geteuid\0getgid\0getlogin\0getpgrp\0getpid\0\
getppid\0getuid\0isatty\0link\0lseek\0pathconf\0pause\0pipe\0read\0rmdir\0\
setgid\0setpgid\0setsid\0setuid\0sleep\0sysconf\0tcgetpgrp\0tcsetpgrp\0\
ttyname\0unlink\0write\0" },
  { CONTINUED, POSIX2_SYMBOL, "getopt\0" },
  { CONTINUED, XOPEN_EXTENDED_SYMBOL,
      "lockf\0gethostid\0gethostname\0readlink\0symlink\0" },

  { "utime.h", POSIX1_SYMBOL, "utime\0" },

  { NULL, 0, NONE }
};

enum special_file special_file_handling = no_special;

/* They are set if the corresponding macro has been seen.  */
/* The following are only used when handling sys/stat.h */
int seen_S_IFBLK = 0, seen_S_ISBLK  = 0;
int seen_S_IFCHR = 0, seen_S_ISCHR  = 0;
int seen_S_IFDIR = 0, seen_S_ISDIR  = 0;
int seen_S_IFIFO = 0, seen_S_ISFIFO = 0;
int seen_S_IFLNK = 0, seen_S_ISLNK  = 0;
int seen_S_IFREG = 0, seen_S_ISREG  = 0;
/* The following are only used when handling errno.h */
int seen_errno = 0;
/* The following are only used when handling stdlib.h */
int seen_EXIT_FAILURE = 0, seen_EXIT_SUCCESS = 0;

struct obstack scan_file_obstack;

/* NOTE:  If you edit this, also edit gen-protos.c !! */

static struct fn_decl *
lookup_std_proto (const char *name, int name_length)
{
  int i = hashstr (name, name_length) % HASH_SIZE;
  int i0 = i;
  for (;;)
    {
      struct fn_decl *fn;
      if (hash_tab[i] == 0)
	return NULL;
      fn = &std_protos[hash_tab[i]];
      if ((int) strlen (fn->fname) == name_length
	  && strncmp (fn->fname, name, name_length) == 0)
	return fn;
      i = (i+1) % HASH_SIZE;
      gcc_assert (i != i0);
    }
}

char *inc_filename;
int inc_filename_length;
FILE *outf;
sstring line;

int lbrac_line, rbrac_line;

int required_unseen_count = 0;
int required_other = 0;

static void
write_lbrac (void)
{
  if (partial_count)
    {
      fprintf (outf, "#ifndef _PARAMS\n");
      fprintf (outf, "#if defined(__STDC__) || defined(__cplusplus)\n");
      fprintf (outf, "#define _PARAMS(ARGS) ARGS\n");
      fprintf (outf, "#else\n");
      fprintf (outf, "#define _PARAMS(ARGS) ()\n");
      fprintf (outf, "#endif\n#endif /* _PARAMS */\n");
    }
}

struct partial_proto
{
  struct partial_proto *next;
  struct fn_decl *fn;
  int line_seen;
};

struct partial_proto *partial_proto_list = NULL;

struct partial_proto required_dummy_proto, seen_dummy_proto;
#define REQUIRED(FN) ((FN)->partial == &required_dummy_proto)
#define SET_REQUIRED(FN) ((FN)->partial = &required_dummy_proto)
#define SET_SEEN(FN) ((FN)->partial = &seen_dummy_proto)
#define SEEN(FN) ((FN)->partial == &seen_dummy_proto)

static void
recognized_macro (const char *fname)
{
  /* The original include file defines fname as a macro.  */
  struct fn_decl *fn = lookup_std_proto (fname, strlen (fname));

  /* Since fname is a macro, don't require a prototype for it.  */
  if (fn)
    {
      if (REQUIRED (fn))
	required_unseen_count--;
      SET_SEEN (fn);
    }

  switch (special_file_handling)
    {
    case errno_h:
      if (strcmp (fname, "errno") == 0 && !seen_errno)
	seen_errno = 1, required_other--;
      break;
    case stdlib_h:
      if (strcmp (fname, "EXIT_FAILURE") == 0 && !seen_EXIT_FAILURE)
	seen_EXIT_FAILURE = 1, required_other--;
      if (strcmp (fname, "EXIT_SUCCESS") == 0 && !seen_EXIT_SUCCESS)
	seen_EXIT_SUCCESS = 1, required_other--;
      break;
    case sys_stat_h:
      if (fname[0] == 'S' && fname[1] == '_')
	{
	  if (strcmp (fname, "S_IFBLK") == 0) seen_S_IFBLK++;
	  else if (strcmp (fname, "S_ISBLK") == 0) seen_S_ISBLK++;
	  else if (strcmp (fname, "S_IFCHR") == 0) seen_S_IFCHR++;
	  else if (strcmp (fname, "S_ISCHR") == 0) seen_S_ISCHR++;
	  else if (strcmp (fname, "S_IFDIR") == 0) seen_S_IFDIR++;
	  else if (strcmp (fname, "S_ISDIR") == 0) seen_S_ISDIR++;
	  else if (strcmp (fname, "S_IFIFO") == 0) seen_S_IFIFO++;
	  else if (strcmp (fname, "S_ISFIFO") == 0) seen_S_ISFIFO++;
	  else if (strcmp (fname, "S_IFLNK") == 0) seen_S_IFLNK++;
	  else if (strcmp (fname, "S_ISLNK") == 0) seen_S_ISLNK++;
	  else if (strcmp (fname, "S_IFREG") == 0) seen_S_IFREG++;
	  else if (strcmp (fname, "S_ISREG") == 0) seen_S_ISREG++;
	}
      break;

    default:
      break;
    }
}

void
recognized_extern (const cpp_token *name)
{
  switch (special_file_handling)
    {
    case errno_h:
      if (cpp_ideq (name, "errno"))
	seen_errno = 1, required_other--;
      break;

    default:
      break;
    }
}

/* Called by scan_decls if it saw a function definition for a function
   named FNAME.  KIND is 'I' for an inline function; 'F' if a normal
   function declaration preceded by 'extern "C"' (or nested inside
   'extern "C"' braces); or 'f' for other function declarations.  */

void
recognized_function (const cpp_token *fname, unsigned int line, int kind,
		     int have_arg_list)
{
  struct partial_proto *partial;
  int i;
  struct fn_decl *fn;

  fn = lookup_std_proto ((const char *) NODE_NAME (fname->val.node),
			 NODE_LEN (fname->val.node));

  /* Remove the function from the list of required function.  */
  if (fn)
    {
      if (REQUIRED (fn))
	required_unseen_count--;
      SET_SEEN (fn);
    }

  /* If we have a full prototype, we're done.  */
  if (have_arg_list)
    return;

  if (kind == 'I')  /* don't edit inline function */
    return;

  /* If the partial prototype was included from some other file,
     we don't need to patch it up (in this run).  */
  i = strlen (cur_file);
  if (i < inc_filename_length
      || strcmp (inc_filename, cur_file + (i - inc_filename_length)) != 0)
    return;

  if (fn == NULL)
    return;
  if (fn->params[0] == '\0')
    return;

  /* We only have a partial function declaration,
     so remember that we have to add a complete prototype.  */
  partial_count++;
  partial = obstack_alloc (&scan_file_obstack, sizeof (struct partial_proto));
  partial->line_seen = line;
  partial->fn = fn;
  fn->partial = partial;
  partial->next = partial_proto_list;
  partial_proto_list = partial;
  if (verbose)
    {
      fprintf (stderr, "(%s: %s non-prototype function declaration.)\n",
	       inc_filename, fn->fname);
    }
}

/* For any name in NAMES that is defined as a macro,
   call recognized_macro on it.  */

static void
check_macro_names (cpp_reader *pfile, namelist names)
{
  size_t len;
  while (*names)
    {
      len = strlen (names);
      if (cpp_defined (pfile, (const unsigned char *)names, len))
	recognized_macro (names);
      names += len + 1;
    }
}

static void
cb_file_change (cpp_reader *pfile ATTRIBUTE_UNUSED,
		const struct line_map *map)
{
  /* Just keep track of current file name.  */
  cur_file = map == NULL ? NULL : map->to_file;
}

static void
read_scan_file (char *in_fname, int argc, char **argv)
{
  cpp_reader *scan_in;
  cpp_callbacks *cb;
  cpp_options *options;
  struct fn_decl *fn;
  int i, strings_processed;
  struct symbol_list *cur_symbols;

  obstack_init (&scan_file_obstack);

  linemap_init (&line_table);
  scan_in = cpp_create_reader (CLK_GNUC89, NULL, &line_table);
  cb = cpp_get_callbacks (scan_in);
  cb->file_change = cb_file_change;

  /* We are going to be scanning a header file out of its proper context,
     so ignore warnings and errors.  */
  options = cpp_get_options (scan_in);
  options->inhibit_warnings = 1;
  options->inhibit_errors = 1;
  cpp_post_options (scan_in);

  if (!cpp_read_main_file (scan_in, in_fname))
    exit (FATAL_EXIT_CODE);

  cpp_change_file (scan_in, LC_RENAME, "<built-in>");
  cpp_init_builtins (scan_in, true);
  cpp_change_file (scan_in, LC_RENAME, in_fname);

  /* Process switches after builtins so -D can override them.  */
  for (i = 0; i < argc; i += strings_processed)
    {
      strings_processed = 0;
      if (argv[i][0] == '-')
	{
	  if (argv[i][1] == 'I')
	    {
	      if (argv[i][2] != '\0')
		{
		  strings_processed = 1;
		  add_path (xstrdup (argv[i] + 2), BRACKET, false, false);
		}
	      else if (i + 1 != argc)
		{
		  strings_processed = 2;
		  add_path (xstrdup (argv[i + 1]), BRACKET, false, false);
		}
	    }
	  else if (argv[i][1] == 'D')
	    {
	      if (argv[i][2] != '\0')
		strings_processed = 1, cpp_define (scan_in, argv[i] + 2);
	      else if (i + 1 != argc)
		strings_processed = 2, cpp_define (scan_in, argv[i + 1]);
	    }
	}

      if (strings_processed == 0)
	break;
    }

  if (i < argc)
    cpp_error (scan_in, CPP_DL_ERROR, "invalid option `%s'", argv[i]);
  if (cpp_errors (scan_in))
    exit (FATAL_EXIT_CODE);

  register_include_chains (scan_in, NULL /* sysroot */, NULL /* iprefix */,
			   NULL /* imultilib */, true /* stdinc */,
			   false /* cxx_stdinc */, false /* verbose */);

  /* We are scanning a system header, so mark it as such.  */
  cpp_make_system_header (scan_in, 1, 0);

  scan_decls (scan_in, argc, argv);
  for (cur_symbols = &symbol_table[0]; cur_symbols->names; cur_symbols++)
    check_macro_names (scan_in, cur_symbols->names);

  /* Traditionally, getc and putc are defined in terms of _filbuf and _flsbuf.
     If so, those functions are also required.  */
  if (special_file_handling == stdio_h
      && (fn = lookup_std_proto ("_filbuf", 7)) != NULL)
    {
      unsigned char getchar_call[] = "getchar();\n";
      int seen_filbuf = 0;

      /* Scan the macro expansion of "getchar();".  */
      cpp_push_buffer (scan_in, getchar_call, sizeof(getchar_call) - 1,
		       /* from_stage3 */ true);
      for (;;)
	{
	  const cpp_token *t = cpp_get_token (scan_in);

	  if (t->type == CPP_EOF)
	    break;
	  else if (cpp_ideq (t, "_filbuf"))
	    seen_filbuf++;
	}

      if (seen_filbuf)
	{
	  int need_filbuf = !SEEN (fn) && !REQUIRED (fn);
	  struct fn_decl *flsbuf_fn = lookup_std_proto ("_flsbuf", 7);
	  int need_flsbuf
	    = flsbuf_fn && !SEEN (flsbuf_fn) && !REQUIRED (flsbuf_fn);

	  /* Append "_filbuf" and/or "_flsbuf" to the required functions.  */
	  if (need_filbuf + need_flsbuf)
	    {
	      const char *new_list;
	      if (need_filbuf)
		SET_REQUIRED (fn);
	      if (need_flsbuf)
		SET_REQUIRED (flsbuf_fn);
	      if (need_flsbuf && need_filbuf)
		new_list = "_filbuf\0_flsbuf\0";
	      else if (need_flsbuf)
		new_list = "_flsbuf\0";
	      else /* if (need_flsbuf) */
		new_list = "_filbuf\0";
	      add_symbols (ANSI_SYMBOL, new_list);
	      required_unseen_count += need_filbuf + need_flsbuf;
	    }
	}
    }

  if (required_unseen_count + partial_count + required_other == 0)
    {
      if (verbose)
	fprintf (stderr, "%s: OK, nothing needs to be done.\n", inc_filename);
      exit (SUCCESS_EXIT_CODE);
    }
  if (!verbose)
    fprintf (stderr, "%s: fixing %s\n", progname, inc_filename);
  else
    {
      if (required_unseen_count)
	fprintf (stderr, "%s: %d missing function declarations.\n",
		 inc_filename, required_unseen_count);
      if (partial_count)
	fprintf (stderr, "%s: %d non-prototype function declarations.\n",
		 inc_filename, partial_count);
    }
}

static void
write_rbrac (void)
{
  struct fn_decl *fn;
  const char *cptr;
  struct symbol_list *cur_symbols;

  if (required_unseen_count)
    {
#ifdef NO_IMPLICIT_EXTERN_C
      fprintf (outf, "#ifdef __cplusplus\nextern \"C\" {\n#endif\n");
#endif
    }

  /* Now we print out prototypes for those functions that we haven't seen.  */
  for (cur_symbols = &symbol_table[0]; cur_symbols->names; cur_symbols++)
    {
      int if_was_emitted = 0;
      int name_len;
      cptr = cur_symbols->names;
      for ( ; (name_len = strlen (cptr)) != 0; cptr+= name_len + 1)
	{
	  int macro_protect = 0;

	  if (cur_symbols->flags & MACRO_SYMBOL)
	    continue;

	  fn = lookup_std_proto (cptr, name_len);
	  if (fn == NULL || !REQUIRED (fn))
	    continue;

	  if (!if_was_emitted)
	    {
/*	      what about curses. ??? or _flsbuf/_filbuf ??? */
	      if (cur_symbols->flags & ANSI_SYMBOL)
		fprintf (outf,
	 "#if defined(__USE_FIXED_PROTOTYPES__) || defined(__cplusplus) || defined (__STRICT_ANSI__)\n");
	      else if (cur_symbols->flags & (POSIX1_SYMBOL|POSIX2_SYMBOL))
		fprintf (outf,
       "#if defined(__USE_FIXED_PROTOTYPES__) || (defined(__cplusplus) \\\n\
    ? (!defined(__STRICT_ANSI__) || defined(_POSIX_SOURCE)) \\\n\
    : (defined(__STRICT_ANSI__) && defined(_POSIX_SOURCE)))\n");
	      else if (cur_symbols->flags & XOPEN_SYMBOL)
		{
		fprintf (outf,
       "#if defined(__USE_FIXED_PROTOTYPES__) \\\n\
   || (defined(__STRICT_ANSI__) && defined(_XOPEN_SOURCE))\n");
		}
	      else if (cur_symbols->flags & XOPEN_EXTENDED_SYMBOL)
		{
		fprintf (outf,
       "#if defined(__USE_FIXED_PROTOTYPES__) \\\n\
   || (defined(__STRICT_ANSI__) && defined(_XOPEN_EXTENDED_SOURCE))\n");
		}
	      else
		{
		  fatal ("internal error for function %s", fn->fname);
		}
	      if_was_emitted = 1;
	    }

	  /* In the case of memmove, protect in case the application
	     defines it as a macro before including the header.  */
	  if (!strcmp (fn->fname, "memmove")
	      || !strcmp (fn->fname, "putc")
	      || !strcmp (fn->fname, "getc")
	      || !strcmp (fn->fname, "vprintf")
	      || !strcmp (fn->fname, "vfprintf")
	      || !strcmp (fn->fname, "vsprintf")
	      || !strcmp (fn->fname, "rewinddir")
	      || !strcmp (fn->fname, "abort"))
	    macro_protect = 1;

	  if (macro_protect)
	    fprintf (outf, "#ifndef %s\n", fn->fname);
	  fprintf (outf, "extern %s %s (%s);\n",
		   fn->rtype, fn->fname, fn->params);
	  if (macro_protect)
	    fprintf (outf, "#endif\n");
	}
      if (if_was_emitted)
	fprintf (outf,
		 "#endif /* defined(__USE_FIXED_PROTOTYPES__) || ... */\n");
    }
  if (required_unseen_count)
    {
#ifdef NO_IMPLICIT_EXTERN_C
      fprintf (outf, "#ifdef __cplusplus\n}\n#endif\n");
#endif
    }

  switch (special_file_handling)
    {
    case errno_h:
      if (!seen_errno)
	fprintf (outf, "extern int errno;\n");
      break;
    case stdlib_h:
      if (!seen_EXIT_FAILURE)
	fprintf (outf, "#define EXIT_FAILURE 1\n");
      if (!seen_EXIT_SUCCESS)
	fprintf (outf, "#define EXIT_SUCCESS 0\n");
      break;
    case sys_stat_h:
      if (!seen_S_ISBLK && seen_S_IFBLK)
	fprintf (outf,
		 "#define S_ISBLK(mode) (((mode) & S_IFMT) == S_IFBLK)\n");
      if (!seen_S_ISCHR && seen_S_IFCHR)
	fprintf (outf,
		 "#define S_ISCHR(mode) (((mode) & S_IFMT) == S_IFCHR)\n");
      if (!seen_S_ISDIR && seen_S_IFDIR)
	fprintf (outf,
		 "#define S_ISDIR(mode) (((mode) & S_IFMT) == S_IFDIR)\n");
      if (!seen_S_ISFIFO && seen_S_IFIFO)
	fprintf (outf,
		 "#define S_ISFIFO(mode) (((mode) & S_IFMT) == S_IFIFO)\n");
      if (!seen_S_ISLNK && seen_S_IFLNK)
	fprintf (outf,
		 "#define S_ISLNK(mode) (((mode) & S_IFMT) == S_IFLNK)\n");
      if (!seen_S_ISREG && seen_S_IFREG)
	fprintf (outf,
		 "#define S_ISREG(mode) (((mode) & S_IFMT) == S_IFREG)\n");
      break;

    default:
      break;
    }

}

/* Returns 1 iff the file is properly protected from multiple inclusion:
   #ifndef PROTECT_NAME
   #define PROTECT_NAME
   #endif

 */

#define INF_GET() (inf_ptr < inf_limit ? *(unsigned char *) inf_ptr++ : EOF)
#define INF_UNGET(c) ((c)!=EOF && inf_ptr--)

static int
inf_skip_spaces (int c)
{
  for (;;)
    {
      if (c == ' ' || c == '\t')
	c = INF_GET ();
      else if (c == '/')
	{
	  c = INF_GET ();
	  if (c != '*')
	    {
	      (void) INF_UNGET (c);
	      return '/';
	    }
	  c = INF_GET ();
	  for (;;)
	    {
	      if (c == EOF)
		return EOF;
	      else if (c != '*')
		{
		  if (c == '\n')
		    source_lineno++, lineno++;
		  c = INF_GET ();
		}
	      else if ((c = INF_GET ()) == '/')
		return INF_GET ();
	    }
	}
      else
	break;
    }
  return c;
}

/* Read into STR from inf_buffer upto DELIM.  */

static int
inf_read_upto (sstring *str, int delim)
{
  int ch;
  for (;;)
    {
      ch = INF_GET ();
      if (ch == EOF || ch == delim)
	break;
      SSTRING_PUT (str, ch);
    }
  MAKE_SSTRING_SPACE (str, 1);
  *str->ptr = 0;
  return ch;
}

static int
inf_scan_ident (sstring *s, int c)
{
  s->ptr = s->base;
  if (ISIDST (c))
    {
      for (;;)
	{
	  SSTRING_PUT (s, c);
	  c = INF_GET ();
	  if (c == EOF || !(ISIDNUM (c)))
	    break;
	}
    }
  MAKE_SSTRING_SPACE (s, 1);
  *s->ptr = 0;
  return c;
}

/* Returns 1 if the file is correctly protected against multiple
   inclusion, setting *ifndef_line to the line number of the initial #ifndef
   and setting *endif_line to the final #endif.
   Otherwise return 0.  */

static int
check_protection (int *ifndef_line, int *endif_line)
{
  int c;
  int if_nesting = 1; /* Level of nesting of #if's */
  char *protect_name = NULL; /* Identifier following initial #ifndef */
  int define_seen = 0;

  /* Skip initial white space (including comments).  */
  for (;; lineno++)
    {
      c = inf_skip_spaces (' ');
      if (c == EOF)
	return 0;
      if (c != '\n')
	break;
    }
  if (c != '#')
    return 0;
  c = inf_scan_ident (&buf, inf_skip_spaces (' '));
  if (SSTRING_LENGTH (&buf) == 0 || strcmp (buf.base, "ifndef") != 0)
    return 0;

  /* So far so good: We've seen an initial #ifndef.  */
  *ifndef_line = lineno;
  c = inf_scan_ident (&buf, inf_skip_spaces (c));
  if (SSTRING_LENGTH (&buf) == 0 || c == EOF)
    return 0;
  protect_name = xstrdup (buf.base);

  (void) INF_UNGET (c);
  c = inf_read_upto (&buf, '\n');
  if (c == EOF)
    return 0;
  lineno++;

  for (;;)
    {
      c = inf_skip_spaces (' ');
      if (c == EOF)
	return 0;
      if (c == '\n')
	{
	  lineno++;
	  continue;
	}
      if (c != '#')
	goto skip_to_eol;
      c = inf_scan_ident (&buf, inf_skip_spaces (' '));
      if (SSTRING_LENGTH (&buf) == 0)
	;
      else if (!strcmp (buf.base, "ifndef")
	  || !strcmp (buf.base, "ifdef") || !strcmp (buf.base, "if"))
	{
	  if_nesting++;
	}
      else if (!strcmp (buf.base, "endif"))
	{
	  if_nesting--;
	  if (if_nesting == 0)
	    break;
	}
      else if (!strcmp (buf.base, "else"))
	{
	  if (if_nesting == 1)
	    return 0;
	}
      else if (!strcmp (buf.base, "define"))
	{
	  c = inf_skip_spaces (c);
	  c = inf_scan_ident (&buf, c);
	  if (buf.base[0] > 0 && strcmp (buf.base, protect_name) == 0)
	    define_seen = 1;
	}
    skip_to_eol:
      for (;;)
	{
	  if (c == '\n' || c == EOF)
	    break;
	  c = INF_GET ();
	}
      if (c == EOF)
	return 0;
      lineno++;
    }

  if (!define_seen)
     return 0;
  *endif_line = lineno;
  /* Skip final white space (including comments).  */
  for (;;)
    {
      c = inf_skip_spaces (' ');
      if (c == EOF)
	break;
      if (c != '\n')
	return 0;
    }

  return 1;
}

extern int main (int, char **);

int
main (int argc, char **argv)
{
  int inf_fd;
  struct stat sbuf;
  int c;
#ifdef FIXPROTO_IGNORE_LIST
  int i;
#endif
  const char *cptr;
  int ifndef_line;
  int endif_line;
  long to_read;
  long int inf_size;
  struct symbol_list *cur_symbols;

  progname = "fix-header";
  if (argv[0] && argv[0][0])
    {
      char *p;

      progname = 0;
      for (p = argv[0]; *p; p++)
	if (*p == '/')
	  progname = p;
      progname = progname ? progname+1 : argv[0];
    }

  if (argc < 4)
    {
      fprintf (stderr, "%s: Usage: foo.h infile.h outfile.h options\n",
	       progname);
      exit (FATAL_EXIT_CODE);
    }

  inc_filename = argv[1];
  inc_filename_length = strlen (inc_filename);

#ifdef FIXPROTO_IGNORE_LIST
  for (i = 0; files_to_ignore[i] != NULL; i++)
    {
      const char *const ignore_name = files_to_ignore[i];
      int ignore_len = strlen (ignore_name);
      if (strncmp (inc_filename, ignore_name, ignore_len) == 0)
	{
	  if (ignore_name[ignore_len-1] == '/'
	      || inc_filename[ignore_len] == '\0')
	    {
	      if (verbose)
		fprintf (stderr, "%s: ignoring %s\n", progname, inc_filename);
	      exit (SUCCESS_EXIT_CODE);
	    }
	}

    }
#endif

  if (strcmp (inc_filename, "sys/stat.h") == 0)
    special_file_handling = sys_stat_h;
  else if (strcmp (inc_filename, "errno.h") == 0)
    special_file_handling = errno_h, required_other++;
  else if (strcmp (inc_filename, "stdlib.h") == 0)
    special_file_handling = stdlib_h, required_other+=2;
  else if (strcmp (inc_filename, "stdio.h") == 0)
    special_file_handling = stdio_h;
  include_entry = std_include_table;
  while (include_entry->name != NULL
	 && ((strcmp (include_entry->name, CONTINUED) == 0)
	     || strcmp (inc_filename, include_entry->name) != 0))
    include_entry++;

  if (include_entry->name != NULL)
    {
      const struct std_include_entry *entry;
      cur_symbol_table_size = 0;
      for (entry = include_entry; ;)
	{
	  if (entry->flags)
	    add_symbols (entry->flags, entry->names);
	  entry++;
	  if (!entry->name || strcmp (entry->name, CONTINUED) != 0)
	    break;
	}
    }
  else
    symbol_table[0].names = NULL;

  /* Count and mark the prototypes required for this include file.  */
  for (cur_symbols = &symbol_table[0]; cur_symbols->names; cur_symbols++)
    {
      int name_len;
      if (cur_symbols->flags & MACRO_SYMBOL)
	continue;
      cptr = cur_symbols->names;
      for ( ; (name_len = strlen (cptr)) != 0; cptr+= name_len + 1)
	{
	  struct fn_decl *fn = lookup_std_proto (cptr, name_len);
	  required_unseen_count++;
	  if (fn == NULL)
	    fprintf (stderr, "Internal error:  No prototype for %s\n", cptr);
	  else
	    SET_REQUIRED (fn);
	}
    }

  read_scan_file (argv[2], argc - 4, argv + 4);

  inf_fd = open (argv[2], O_RDONLY, 0666);
  if (inf_fd < 0)
    {
      fprintf (stderr, "%s: Cannot open '%s' for reading -",
	       progname, argv[2]);
      perror (NULL);
      exit (FATAL_EXIT_CODE);
    }
  if (fstat (inf_fd, &sbuf) < 0)
    {
      fprintf (stderr, "%s: Cannot get size of '%s' -", progname, argv[2]);
      perror (NULL);
      exit (FATAL_EXIT_CODE);
    }
  inf_size = sbuf.st_size;
  inf_buffer = XNEWVEC (char, inf_size + 2);
  inf_ptr = inf_buffer;

  to_read = inf_size;
  while (to_read > 0)
    {
      long i = read (inf_fd, inf_buffer + inf_size - to_read, to_read);
      if (i < 0)
	{
	  fprintf (stderr, "%s: Failed to read '%s' -", progname, argv[2]);
	  perror (NULL);
	  exit (FATAL_EXIT_CODE);
	}
      if (i == 0)
	{
	  inf_size -= to_read;
	  break;
	}
      to_read -= i;
    }

  close (inf_fd);

  /* Inf_size may have changed if read was short (as on VMS) */
  inf_buffer[inf_size] = '\n';
  inf_buffer[inf_size + 1] = '\0';
  inf_limit = inf_buffer + inf_size;

  /* If file doesn't end with '\n', add one.  */
  if (inf_limit > inf_buffer && inf_limit[-1] != '\n')
    inf_limit++;

  unlink (argv[3]);
  outf = fopen (argv[3], "w");
  if (outf == NULL)
    {
      fprintf (stderr, "%s: Cannot open '%s' for writing -",
	       progname, argv[3]);
      perror (NULL);
      exit (FATAL_EXIT_CODE);
    }

  lineno = 1;

  if (check_protection (&ifndef_line, &endif_line))
    {
      lbrac_line = ifndef_line+1;
      rbrac_line = endif_line;
    }
  else
    {
      lbrac_line = 1;
      rbrac_line = -1;
    }

  /* Reset input file.  */
  inf_ptr = inf_buffer;
  lineno = 1;

  for (;;)
    {
      if (lineno == lbrac_line)
	write_lbrac ();
      if (lineno == rbrac_line)
	write_rbrac ();
      for (;;)
	{
	  struct fn_decl *fn;
	  c = INF_GET ();
	  if (c == EOF)
	    break;
	  if (ISIDST (c))
	    {
	      c = inf_scan_ident (&buf, c);
	      (void) INF_UNGET (c);
	      fputs (buf.base, outf);
	      fn = lookup_std_proto (buf.base, strlen (buf.base));
	      /* We only want to edit the declaration matching the one
		 seen by scan-decls, as there can be multiple
		 declarations, selected by #ifdef __STDC__ or whatever.  */
	      if (fn && fn->partial && fn->partial->line_seen == lineno)
		{
		  c = inf_skip_spaces (' ');
		  if (c == EOF)
		    break;
		  if (c == '(')
		    {
		      c = inf_skip_spaces (' ');
		      if (c == ')')
			{
			  fprintf (outf, " _PARAMS((%s))", fn->params);
			}
		      else
			{
			  putc ('(', outf);
			  (void) INF_UNGET (c);
			}
		    }
		  else
		    fprintf (outf, " %c", c);
		}
	    }
	  else
	    {
	      putc (c, outf);
	      if (c == '\n')
		break;
	    }
	}
      if (c == EOF)
	break;
      lineno++;
    }
  if (rbrac_line < 0)
    write_rbrac ();

  fclose (outf);

  return 0;
}

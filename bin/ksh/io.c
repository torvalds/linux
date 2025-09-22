/*	$OpenBSD: io.c,v 1.38 2019/07/24 14:33:16 bcallah Exp $	*/

/*
 * shell buffered IO and formatted output
 */

#include <sys/stat.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "sh.h"

static int initio_done;

/*
 * formatted output functions
 */


/* A shell error occurred (eg, syntax error, etc.) */
void
errorf(const char *fmt, ...)
{
	va_list va;

	shl_stdout_ok = 0;	/* debugging: note that stdout not valid */
	exstat = 1;
	if (fmt != NULL && *fmt != '\0') {
		error_prefix(true);
		va_start(va, fmt);
		shf_vfprintf(shl_out, fmt, va);
		va_end(va);
		shf_putchar('\n', shl_out);
	}
	shf_flush(shl_out);
	unwind(LERROR);
}

/* like errorf(), but no unwind is done */
void
warningf(bool show_lineno, const char *fmt, ...)
{
	va_list va;

	error_prefix(show_lineno);
	va_start(va, fmt);
	shf_vfprintf(shl_out, fmt, va);
	va_end(va);
	shf_putchar('\n', shl_out);
	shf_flush(shl_out);
}

/* Used by built-in utilities to prefix shell and utility name to message
 * (also unwinds environments for special builtins).
 */
void
bi_errorf(const char *fmt, ...)
{
	va_list va;

	shl_stdout_ok = 0;	/* debugging: note that stdout not valid */
	exstat = 1;
	if (fmt != NULL && *fmt != '\0') {
		error_prefix(true);
		/* not set when main() calls parse_args() */
		if (builtin_argv0)
			shf_fprintf(shl_out, "%s: ", builtin_argv0);
		va_start(va, fmt);
		shf_vfprintf(shl_out, fmt, va);
		va_end(va);
		shf_putchar('\n', shl_out);
	}
	shf_flush(shl_out);
	/* POSIX special builtins and ksh special builtins cause
	 * non-interactive shells to exit.
	 * XXX odd use of KEEPASN; also may not want LERROR here
	 */
	if ((builtin_flag & SPEC_BI) ||
	    (Flag(FPOSIX) && (builtin_flag & KEEPASN))) {
		builtin_argv0 = NULL;
		unwind(LERROR);
	}
}

static void
internal_error_vwarn(const char *fmt, va_list va)
{
	error_prefix(true);
	shf_fprintf(shl_out, "internal error: ");
	shf_vfprintf(shl_out, fmt, va);
	shf_putchar('\n', shl_out);
	shf_flush(shl_out);
}

/* Warn when something that shouldn't happen does */
void
internal_warningf(const char *fmt, ...)
{
	va_list va;

	va_start(va, fmt);
	internal_error_vwarn(fmt, va);
	va_end(va);
}

/* Warn and unwind when something that shouldn't happen does */
__dead void
internal_errorf(const char *fmt, ...)
{
	va_list va;

	va_start(va, fmt);
	internal_error_vwarn(fmt, va);
	va_end(va);
	unwind(LERROR);
}

/* used by error reporting functions to print "ksh: .kshrc[25]: " */
void
error_prefix(int fileline)
{
	/* Avoid foo: foo[2]: ... */
	if (!fileline || !source || !source->file ||
	    strcmp(source->file, kshname) != 0)
		shf_fprintf(shl_out, "%s: ", kshname + (*kshname == '-'));
	if (fileline && source && source->file != NULL) {
		shf_fprintf(shl_out, "%s[%d]: ", source->file,
		    source->errline > 0 ? source->errline : source->line);
		source->errline = 0;
	}
}

/* printf to shl_out (stderr) with flush */
void
shellf(const char *fmt, ...)
{
	va_list va;

	if (!initio_done) /* shl_out may not be set up yet... */
		return;
	va_start(va, fmt);
	shf_vfprintf(shl_out, fmt, va);
	va_end(va);
	shf_flush(shl_out);
}

/* printf to shl_stdout (stdout) */
void
shprintf(const char *fmt, ...)
{
	va_list va;

	if (!shl_stdout_ok)
		internal_errorf("shl_stdout not valid");
	va_start(va, fmt);
	shf_vfprintf(shl_stdout, fmt, va);
	va_end(va);
}

#ifdef KSH_DEBUG
static struct shf *kshdebug_shf;

void
kshdebug_init_(void)
{
	if (kshdebug_shf)
		shf_close(kshdebug_shf);
	kshdebug_shf = shf_open("/tmp/ksh-debug.log",
	    O_WRONLY|O_APPEND|O_CREAT, 0600, SHF_WR|SHF_MAPHI);
	if (kshdebug_shf) {
		shf_fprintf(kshdebug_shf, "\nNew shell[pid %d]\n", getpid());
		shf_flush(kshdebug_shf);
	}
}

/* print to debugging log */
void
kshdebug_printf_(const char *fmt, ...)
{
	va_list va;

	if (!kshdebug_shf)
		return;
	va_start(va, fmt);
	shf_fprintf(kshdebug_shf, "[%d] ", getpid());
	shf_vfprintf(kshdebug_shf, fmt, va);
	va_end(va);
	shf_flush(kshdebug_shf);
}

void
kshdebug_dump_(const char *str, const void *mem, int nbytes)
{
	int i, j;
	int nprow = 16;

	if (!kshdebug_shf)
		return;
	shf_fprintf(kshdebug_shf, "[%d] %s:\n", getpid(), str);
	for (i = 0; i < nbytes; i += nprow) {
		char c = '\t';

		for (j = 0; j < nprow && i + j < nbytes; j++) {
			shf_fprintf(kshdebug_shf, "%c%02x", c,
			    ((const unsigned char *) mem)[i + j]);
			c = ' ';
		}
		shf_fprintf(kshdebug_shf, "\n");
	}
	shf_flush(kshdebug_shf);
}
#endif /* KSH_DEBUG */

/* test if we can seek backwards fd (returns 0 or SHF_UNBUF) */
int
can_seek(int fd)
{
	struct stat statb;

	return fstat(fd, &statb) == 0 && !S_ISREG(statb.st_mode) ?
	    SHF_UNBUF : 0;
}

struct shf	shf_iob[3];

void
initio(void)
{
	shf_fdopen(1, SHF_WR, shl_stdout);	/* force buffer allocation */
	shf_fdopen(2, SHF_WR, shl_out);
	shf_fdopen(2, SHF_WR, shl_spare);	/* force buffer allocation */
	initio_done = 1;
	kshdebug_init();
}

/* A dup2() with error checking */
int
ksh_dup2(int ofd, int nfd, int errok)
{
	int ret = dup2(ofd, nfd);

	if (ret == -1 && errno != EBADF && !errok)
		errorf("too many files open in shell");

	return ret;
}

/*
 * move fd from user space (0<=fd<10) to shell space (fd>=10),
 * set close-on-exec flag.
 */
int
savefd(int fd)
{
	int nfd;

	if (fd < FDBASE) {
		nfd = fcntl(fd, F_DUPFD_CLOEXEC, FDBASE);
		if (nfd == -1) {
			if (errno == EBADF)
				return -1;
			else
				errorf("too many files open in shell");
		}
	} else {
		nfd = fd;
		fcntl(nfd, F_SETFD, FD_CLOEXEC);
	}
	return nfd;
}

void
restfd(int fd, int ofd)
{
	if (fd == 2)
		shf_flush(&shf_iob[fd]);
	if (ofd < 0)		/* original fd closed */
		close(fd);
	else if (fd != ofd) {
		ksh_dup2(ofd, fd, true); /* XXX: what to do if this fails? */
		close(ofd);
	}
}

void
openpipe(int *pv)
{
	int lpv[2];

	if (pipe(lpv) == -1)
		errorf("can't create pipe - try again");
	pv[0] = savefd(lpv[0]);
	if (pv[0] != lpv[0])
		close(lpv[0]);
	pv[1] = savefd(lpv[1]);
	if (pv[1] != lpv[1])
		close(lpv[1]);
}

void
closepipe(int *pv)
{
	close(pv[0]);
	close(pv[1]);
}

/* Called by iosetup() (deals with 2>&4, etc.), c_read, c_print to turn
 * a string (the X in 2>&X, read -uX, print -uX) into a file descriptor.
 */
int
check_fd(char *name, int mode, const char **emsgp)
{
	int fd, fl;

	if (isdigit((unsigned char)name[0]) && !name[1]) {
		fd = name[0] - '0';
		if ((fl = fcntl(fd, F_GETFL)) == -1) {
			if (emsgp)
				*emsgp = "bad file descriptor";
			return -1;
		}
		fl &= O_ACCMODE;
		/* X_OK is a kludge to disable this check for dups (x<&1):
		 * historical shells never did this check (XXX don't know what
		 * posix has to say).
		 */
		if (!(mode & X_OK) && fl != O_RDWR &&
		    (((mode & R_OK) && fl != O_RDONLY) ||
		    ((mode & W_OK) && fl != O_WRONLY))) {
			if (emsgp)
				*emsgp = (fl == O_WRONLY) ?
				    "fd not open for reading" :
				    "fd not open for writing";
			return -1;
		}
		return fd;
	} else if (name[0] == 'p' && !name[1])
		return coproc_getfd(mode, emsgp);
	if (emsgp)
		*emsgp = "illegal file descriptor name";
	return -1;
}

/* Called once from main */
void
coproc_init(void)
{
	coproc.read = coproc.readw = coproc.write = -1;
	coproc.njobs = 0;
	coproc.id = 0;
}

/* Called by c_read() when eof is read - close fd if it is the co-process fd */
void
coproc_read_close(int fd)
{
	if (coproc.read >= 0 && fd == coproc.read) {
		coproc_readw_close(fd);
		close(coproc.read);
		coproc.read = -1;
	}
}

/* Called by c_read() and by iosetup() to close the other side of the
 * read pipe, so reads will actually terminate.
 */
void
coproc_readw_close(int fd)
{
	if (coproc.readw >= 0 && coproc.read >= 0 && fd == coproc.read) {
		close(coproc.readw);
		coproc.readw = -1;
	}
}

/* Called by c_print when a write to a fd fails with EPIPE and by iosetup
 * when co-process input is dup'd
 */
void
coproc_write_close(int fd)
{
	if (coproc.write >= 0 && fd == coproc.write) {
		close(coproc.write);
		coproc.write = -1;
	}
}

/* Called to check for existence of/value of the co-process file descriptor.
 * (Used by check_fd() and by c_read/c_print to deal with -p option).
 */
int
coproc_getfd(int mode, const char **emsgp)
{
	int fd = (mode & R_OK) ? coproc.read : coproc.write;

	if (fd >= 0)
		return fd;
	if (emsgp)
		*emsgp = "no coprocess";
	return -1;
}

/* called to close file descriptors related to the coprocess (if any)
 * Should be called with SIGCHLD blocked.
 */
void
coproc_cleanup(int reuse)
{
	/* This to allow co-processes to share output pipe */
	if (!reuse || coproc.readw < 0 || coproc.read < 0) {
		if (coproc.read >= 0) {
			close(coproc.read);
			coproc.read = -1;
		}
		if (coproc.readw >= 0) {
			close(coproc.readw);
			coproc.readw = -1;
		}
	}
	if (coproc.write >= 0) {
		close(coproc.write);
		coproc.write = -1;
	}
}


/*
 * temporary files
 */

struct temp *
maketemp(Area *ap, Temp_type type, struct temp **tlist)
{
	struct temp *tp;
	int len;
	int fd;
	char *path;
	const char *dir;

	dir = tmpdir ? tmpdir : "/tmp";
	/* The 20 + 20 is a paranoid worst case for pid/inc */
	len = strlen(dir) + 3 + 20 + 20 + 1;
	tp = alloc(sizeof(struct temp) + len, ap);
	tp->name = path = (char *) &tp[1];
	tp->shf = NULL;
	tp->type = type;
	shf_snprintf(path, len, "%s/shXXXXXXXX", dir);
	fd = mkstemp(path);
	if (fd >= 0)
		tp->shf = shf_fdopen(fd, SHF_WR, NULL);
	tp->pid = procpid;

	tp->next = *tlist;
	*tlist = tp;
	return tp;
}

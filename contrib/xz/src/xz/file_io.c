///////////////////////////////////////////////////////////////////////////////
//
/// \file       file_io.c
/// \brief      File opening, unlinking, and closing
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "private.h"

#include <fcntl.h>

#ifdef TUKLIB_DOSLIKE
#	include <io.h>
#else
#	include <poll.h>
static bool warn_fchown;
#endif

#if defined(HAVE_FUTIMES) || defined(HAVE_FUTIMESAT) || defined(HAVE_UTIMES)
#	include <sys/time.h>
#elif defined(HAVE__FUTIME)
#	include <sys/utime.h>
#elif defined(HAVE_UTIME)
#	include <utime.h>
#endif

#ifdef HAVE_CAPSICUM
#	ifdef HAVE_SYS_CAPSICUM_H
#		include <sys/capsicum.h>
#	else
#		include <sys/capability.h>
#	endif
#endif

#include "tuklib_open_stdxxx.h"

#ifndef O_BINARY
#	define O_BINARY 0
#endif

#ifndef O_NOCTTY
#	define O_NOCTTY 0
#endif

// Using this macro to silence a warning from gcc -Wlogical-op.
#if EAGAIN == EWOULDBLOCK
#	define IS_EAGAIN_OR_EWOULDBLOCK(e) ((e) == EAGAIN)
#else
#	define IS_EAGAIN_OR_EWOULDBLOCK(e) \
		((e) == EAGAIN || (e) == EWOULDBLOCK)
#endif


typedef enum {
	IO_WAIT_MORE,    // Reading or writing is possible.
	IO_WAIT_ERROR,   // Error or user_abort
	IO_WAIT_TIMEOUT, // poll() timed out
} io_wait_ret;


/// If true, try to create sparse files when decompressing.
static bool try_sparse = true;

#ifdef ENABLE_SANDBOX
/// True if the conditions for sandboxing (described in main()) have been met.
static bool sandbox_allowed = false;
#endif

#ifndef TUKLIB_DOSLIKE
/// File status flags of standard input. This is used by io_open_src()
/// and io_close_src().
static int stdin_flags;
static bool restore_stdin_flags = false;

/// Original file status flags of standard output. This is used by
/// io_open_dest() and io_close_dest() to save and restore the flags.
static int stdout_flags;
static bool restore_stdout_flags = false;

/// Self-pipe used together with the user_abort variable to avoid
/// race conditions with signal handling.
static int user_abort_pipe[2];
#endif


static bool io_write_buf(file_pair *pair, const uint8_t *buf, size_t size);


extern void
io_init(void)
{
	// Make sure that stdin, stdout, and stderr are connected to
	// a valid file descriptor. Exit immediately with exit code ERROR
	// if we cannot make the file descriptors valid. Maybe we should
	// print an error message, but our stderr could be screwed anyway.
	tuklib_open_stdxxx(E_ERROR);

#ifndef TUKLIB_DOSLIKE
	// If fchown() fails setting the owner, we warn about it only if
	// we are root.
	warn_fchown = geteuid() == 0;

	// Create a pipe for the self-pipe trick.
	if (pipe(user_abort_pipe))
		message_fatal(_("Error creating a pipe: %s"),
				strerror(errno));

	// Make both ends of the pipe non-blocking.
	for (unsigned i = 0; i < 2; ++i) {
		int flags = fcntl(user_abort_pipe[i], F_GETFL);
		if (flags == -1 || fcntl(user_abort_pipe[i], F_SETFL,
				flags | O_NONBLOCK) == -1)
			message_fatal(_("Error creating a pipe: %s"),
					strerror(errno));
	}
#endif

#ifdef __DJGPP__
	// Avoid doing useless things when statting files.
	// This isn't important but doesn't hurt.
	_djstat_flags = _STAT_EXEC_EXT | _STAT_EXEC_MAGIC | _STAT_DIRSIZE;
#endif

	return;
}


#ifndef TUKLIB_DOSLIKE
extern void
io_write_to_user_abort_pipe(void)
{
	// If the write() fails, it's probably due to the pipe being full.
	// Failing in that case is fine. If the reason is something else,
	// there's not much we can do since this is called in a signal
	// handler. So ignore the errors and try to avoid warnings with
	// GCC and glibc when _FORTIFY_SOURCE=2 is used.
	uint8_t b = '\0';
	const int ret = write(user_abort_pipe[1], &b, 1);
	(void)ret;
	return;
}
#endif


extern void
io_no_sparse(void)
{
	try_sparse = false;
	return;
}


#ifdef ENABLE_SANDBOX
extern void
io_allow_sandbox(void)
{
	sandbox_allowed = true;
	return;
}


/// Enables operating-system-specific sandbox if it is possible.
/// src_fd is the file descriptor of the input file.
static void
io_sandbox_enter(int src_fd)
{
	if (!sandbox_allowed) {
		message(V_DEBUG, _("Sandbox is disabled due "
				"to incompatible command line arguments"));
		return;
	}

	const char dummy_str[] = "x";

	// Try to ensure that both libc and xz locale files have been
	// loaded when NLS is enabled.
	snprintf(NULL, 0, "%s%s", _(dummy_str), strerror(EINVAL));

	// Try to ensure that iconv data files needed for handling multibyte
	// characters have been loaded. This is needed at least with glibc.
	tuklib_mbstr_width(dummy_str, NULL);

#ifdef HAVE_CAPSICUM
	// Capsicum needs FreeBSD 10.0 or later.
	cap_rights_t rights;

	if (cap_rights_limit(src_fd, cap_rights_init(&rights,
			CAP_EVENT, CAP_FCNTL, CAP_LOOKUP, CAP_READ, CAP_SEEK)))
		goto error;

	if (cap_rights_limit(STDOUT_FILENO, cap_rights_init(&rights,
			CAP_EVENT, CAP_FCNTL, CAP_FSTAT, CAP_LOOKUP,
			CAP_WRITE, CAP_SEEK)))
		goto error;

	if (cap_rights_limit(user_abort_pipe[0], cap_rights_init(&rights,
			CAP_EVENT)))
		goto error;

	if (cap_rights_limit(user_abort_pipe[1], cap_rights_init(&rights,
			CAP_WRITE)))
		goto error;

	if (cap_enter())
		goto error;

#else
#	error ENABLE_SANDBOX is defined but no sandboxing method was found.
#endif

	message(V_DEBUG, _("Sandbox was successfully enabled"));
	return;

error:
	message(V_DEBUG, _("Failed to enable the sandbox"));
}
#endif // ENABLE_SANDBOX


#ifndef TUKLIB_DOSLIKE
/// \brief      Waits for input or output to become available or for a signal
///
/// This uses the self-pipe trick to avoid a race condition that can occur
/// if a signal is caught after user_abort has been checked but before e.g.
/// read() has been called. In that situation read() could block unless
/// non-blocking I/O is used. With non-blocking I/O something like select()
/// or poll() is needed to avoid a busy-wait loop, and the same race condition
/// pops up again. There are pselect() (POSIX-1.2001) and ppoll() (not in
/// POSIX) but neither is portable enough in 2013. The self-pipe trick is
/// old and very portable.
static io_wait_ret
io_wait(file_pair *pair, int timeout, bool is_reading)
{
	struct pollfd pfd[2];

	if (is_reading) {
		pfd[0].fd = pair->src_fd;
		pfd[0].events = POLLIN;
	} else {
		pfd[0].fd = pair->dest_fd;
		pfd[0].events = POLLOUT;
	}

	pfd[1].fd = user_abort_pipe[0];
	pfd[1].events = POLLIN;

	while (true) {
		const int ret = poll(pfd, 2, timeout);

		if (user_abort)
			return IO_WAIT_ERROR;

		if (ret == -1) {
			if (errno == EINTR || errno == EAGAIN)
				continue;

			message_error(_("%s: poll() failed: %s"),
					is_reading ? pair->src_name
						: pair->dest_name,
					strerror(errno));
			return IO_WAIT_ERROR;
		}

		if (ret == 0) {
			assert(opt_flush_timeout != 0);
			flush_needed = true;
			return IO_WAIT_TIMEOUT;
		}

		if (pfd[0].revents != 0)
			return IO_WAIT_MORE;
	}
}
#endif


/// \brief      Unlink a file
///
/// This tries to verify that the file being unlinked really is the file that
/// we want to unlink by verifying device and inode numbers. There's still
/// a small unavoidable race, but this is much better than nothing (the file
/// could have been moved/replaced even hours earlier).
static void
io_unlink(const char *name, const struct stat *known_st)
{
#if defined(TUKLIB_DOSLIKE)
	// On DOS-like systems, st_ino is meaningless, so don't bother
	// testing it. Just silence a compiler warning.
	(void)known_st;
#else
	struct stat new_st;

	// If --force was used, use stat() instead of lstat(). This way
	// (de)compressing symlinks works correctly. However, it also means
	// that xz cannot detect if a regular file foo is renamed to bar
	// and then a symlink foo -> bar is created. Because of stat()
	// instead of lstat(), xz will think that foo hasn't been replaced
	// with another file. Thus, xz will remove foo even though it no
	// longer is the same file that xz used when it started compressing.
	// Probably it's not too bad though, so this doesn't need a more
	// complex fix.
	const int stat_ret = opt_force
			? stat(name, &new_st) : lstat(name, &new_st);

	if (stat_ret
#	ifdef __VMS
			// st_ino is an array, and we don't want to
			// compare st_dev at all.
			|| memcmp(&new_st.st_ino, &known_st->st_ino,
				sizeof(new_st.st_ino)) != 0
#	else
			// Typical POSIX-like system
			|| new_st.st_dev != known_st->st_dev
			|| new_st.st_ino != known_st->st_ino
#	endif
			)
		// TRANSLATORS: When compression or decompression finishes,
		// and xz is going to remove the source file, xz first checks
		// if the source file still exists, and if it does, does its
		// device and inode numbers match what xz saw when it opened
		// the source file. If these checks fail, this message is
		// shown, %s being the filename, and the file is not deleted.
		// The check for device and inode numbers is there, because
		// it is possible that the user has put a new file in place
		// of the original file, and in that case it obviously
		// shouldn't be removed.
		message_error(_("%s: File seems to have been moved, "
				"not removing"), name);
	else
#endif
		// There's a race condition between lstat() and unlink()
		// but at least we have tried to avoid removing wrong file.
		if (unlink(name))
			message_error(_("%s: Cannot remove: %s"),
					name, strerror(errno));

	return;
}


/// \brief      Copies owner/group and permissions
///
/// \todo       ACL and EA support
///
static void
io_copy_attrs(const file_pair *pair)
{
	// Skip chown and chmod on Windows.
#ifndef TUKLIB_DOSLIKE
	// This function is more tricky than you may think at first.
	// Blindly copying permissions may permit users to access the
	// destination file who didn't have permission to access the
	// source file.

	// Try changing the owner of the file. If we aren't root or the owner
	// isn't already us, fchown() probably doesn't succeed. We warn
	// about failing fchown() only if we are root.
	if (fchown(pair->dest_fd, pair->src_st.st_uid, -1) && warn_fchown)
		message_warning(_("%s: Cannot set the file owner: %s"),
				pair->dest_name, strerror(errno));

	mode_t mode;

	if (fchown(pair->dest_fd, -1, pair->src_st.st_gid)) {
		message_warning(_("%s: Cannot set the file group: %s"),
				pair->dest_name, strerror(errno));
		// We can still safely copy some additional permissions:
		// `group' must be at least as strict as `other' and
		// also vice versa.
		//
		// NOTE: After this, the owner of the source file may
		// get additional permissions. This shouldn't be too bad,
		// because the owner would have had permission to chmod
		// the original file anyway.
		mode = ((pair->src_st.st_mode & 0070) >> 3)
				& (pair->src_st.st_mode & 0007);
		mode = (pair->src_st.st_mode & 0700) | (mode << 3) | mode;
	} else {
		// Drop the setuid, setgid, and sticky bits.
		mode = pair->src_st.st_mode & 0777;
	}

	if (fchmod(pair->dest_fd, mode))
		message_warning(_("%s: Cannot set the file permissions: %s"),
				pair->dest_name, strerror(errno));
#endif

	// Copy the timestamps. We have several possible ways to do this, of
	// which some are better in both security and precision.
	//
	// First, get the nanosecond part of the timestamps. As of writing,
	// it's not standardized by POSIX, and there are several names for
	// the same thing in struct stat.
	long atime_nsec;
	long mtime_nsec;

#	if defined(HAVE_STRUCT_STAT_ST_ATIM_TV_NSEC)
	// GNU and Solaris
	atime_nsec = pair->src_st.st_atim.tv_nsec;
	mtime_nsec = pair->src_st.st_mtim.tv_nsec;

#	elif defined(HAVE_STRUCT_STAT_ST_ATIMESPEC_TV_NSEC)
	// BSD
	atime_nsec = pair->src_st.st_atimespec.tv_nsec;
	mtime_nsec = pair->src_st.st_mtimespec.tv_nsec;

#	elif defined(HAVE_STRUCT_STAT_ST_ATIMENSEC)
	// GNU and BSD without extensions
	atime_nsec = pair->src_st.st_atimensec;
	mtime_nsec = pair->src_st.st_mtimensec;

#	elif defined(HAVE_STRUCT_STAT_ST_UATIME)
	// Tru64
	atime_nsec = pair->src_st.st_uatime * 1000;
	mtime_nsec = pair->src_st.st_umtime * 1000;

#	elif defined(HAVE_STRUCT_STAT_ST_ATIM_ST__TIM_TV_NSEC)
	// UnixWare
	atime_nsec = pair->src_st.st_atim.st__tim.tv_nsec;
	mtime_nsec = pair->src_st.st_mtim.st__tim.tv_nsec;

#	else
	// Safe fallback
	atime_nsec = 0;
	mtime_nsec = 0;
#	endif

	// Construct a structure to hold the timestamps and call appropriate
	// function to set the timestamps.
#if defined(HAVE_FUTIMENS)
	// Use nanosecond precision.
	struct timespec tv[2];
	tv[0].tv_sec = pair->src_st.st_atime;
	tv[0].tv_nsec = atime_nsec;
	tv[1].tv_sec = pair->src_st.st_mtime;
	tv[1].tv_nsec = mtime_nsec;

	(void)futimens(pair->dest_fd, tv);

#elif defined(HAVE_FUTIMES) || defined(HAVE_FUTIMESAT) || defined(HAVE_UTIMES)
	// Use microsecond precision.
	struct timeval tv[2];
	tv[0].tv_sec = pair->src_st.st_atime;
	tv[0].tv_usec = atime_nsec / 1000;
	tv[1].tv_sec = pair->src_st.st_mtime;
	tv[1].tv_usec = mtime_nsec / 1000;

#	if defined(HAVE_FUTIMES)
	(void)futimes(pair->dest_fd, tv);
#	elif defined(HAVE_FUTIMESAT)
	(void)futimesat(pair->dest_fd, NULL, tv);
#	else
	// Argh, no function to use a file descriptor to set the timestamp.
	(void)utimes(pair->dest_name, tv);
#	endif

#elif defined(HAVE__FUTIME)
	// Use one-second precision with Windows-specific _futime().
	// We could use utime() too except that for some reason the
	// timestamp will get reset at close(). With _futime() it works.
	// This struct cannot be const as _futime() takes a non-const pointer.
	struct _utimbuf buf = {
		.actime = pair->src_st.st_atime,
		.modtime = pair->src_st.st_mtime,
	};

	// Avoid warnings.
	(void)atime_nsec;
	(void)mtime_nsec;

	(void)_futime(pair->dest_fd, &buf);

#elif defined(HAVE_UTIME)
	// Use one-second precision. utime() doesn't support using file
	// descriptor either. Some systems have broken utime() prototype
	// so don't make this const.
	struct utimbuf buf = {
		.actime = pair->src_st.st_atime,
		.modtime = pair->src_st.st_mtime,
	};

	// Avoid warnings.
	(void)atime_nsec;
	(void)mtime_nsec;

	(void)utime(pair->dest_name, &buf);
#endif

	return;
}


/// Opens the source file. Returns false on success, true on error.
static bool
io_open_src_real(file_pair *pair)
{
	// There's nothing to open when reading from stdin.
	if (pair->src_name == stdin_filename) {
		pair->src_fd = STDIN_FILENO;
#ifdef TUKLIB_DOSLIKE
		setmode(STDIN_FILENO, O_BINARY);
#else
		// Try to set stdin to non-blocking mode. It won't work
		// e.g. on OpenBSD if stdout is e.g. /dev/null. In such
		// case we proceed as if stdin were non-blocking anyway
		// (in case of /dev/null it will be in practice). The
		// same applies to stdout in io_open_dest_real().
		stdin_flags = fcntl(STDIN_FILENO, F_GETFL);
		if (stdin_flags == -1) {
			message_error(_("Error getting the file status flags "
					"from standard input: %s"),
					strerror(errno));
			return true;
		}

		if ((stdin_flags & O_NONBLOCK) == 0
				&& fcntl(STDIN_FILENO, F_SETFL,
					stdin_flags | O_NONBLOCK) != -1)
			restore_stdin_flags = true;
#endif
#ifdef HAVE_POSIX_FADVISE
		// It will fail if stdin is a pipe and that's fine.
		(void)posix_fadvise(STDIN_FILENO, 0, 0,
				opt_mode == MODE_LIST
					? POSIX_FADV_RANDOM
					: POSIX_FADV_SEQUENTIAL);
#endif
		return false;
	}

	// Symlinks are not followed unless writing to stdout or --force
	// was used.
	const bool follow_symlinks = opt_stdout || opt_force;

	// We accept only regular files if we are writing the output
	// to disk too. bzip2 allows overriding this with --force but
	// gzip and xz don't.
	const bool reg_files_only = !opt_stdout;

	// Flags for open()
	int flags = O_RDONLY | O_BINARY | O_NOCTTY;

#ifndef TUKLIB_DOSLIKE
	// Use non-blocking I/O:
	//   - It prevents blocking when opening FIFOs and some other
	//     special files, which is good if we want to accept only
	//     regular files.
	//   - It can help avoiding some race conditions with signal handling.
	flags |= O_NONBLOCK;
#endif

#if defined(O_NOFOLLOW)
	if (!follow_symlinks)
		flags |= O_NOFOLLOW;
#elif !defined(TUKLIB_DOSLIKE)
	// Some POSIX-like systems lack O_NOFOLLOW (it's not required
	// by POSIX). Check for symlinks with a separate lstat() on
	// these systems.
	if (!follow_symlinks) {
		struct stat st;
		if (lstat(pair->src_name, &st)) {
			message_error("%s: %s", pair->src_name,
					strerror(errno));
			return true;

		} else if (S_ISLNK(st.st_mode)) {
			message_warning(_("%s: Is a symbolic link, "
					"skipping"), pair->src_name);
			return true;
		}
	}
#else
	// Avoid warnings.
	(void)follow_symlinks;
#endif

	// Try to open the file. Signals have been blocked so EINTR shouldn't
	// be possible.
	pair->src_fd = open(pair->src_name, flags);

	if (pair->src_fd == -1) {
		// Signals (that have a signal handler) have been blocked.
		assert(errno != EINTR);

#ifdef O_NOFOLLOW
		// Give an understandable error message if the reason
		// for failing was that the file was a symbolic link.
		//
		// Note that at least Linux, OpenBSD, Solaris, and Darwin
		// use ELOOP to indicate that O_NOFOLLOW was the reason
		// that open() failed. Because there may be
		// directories in the pathname, ELOOP may occur also
		// because of a symlink loop in the directory part.
		// So ELOOP doesn't tell us what actually went wrong,
		// and this stupidity went into POSIX-1.2008 too.
		//
		// FreeBSD associates EMLINK with O_NOFOLLOW and
		// Tru64 uses ENOTSUP. We use these directly here
		// and skip the lstat() call and the associated race.
		// I want to hear if there are other kernels that
		// fail with something else than ELOOP with O_NOFOLLOW.
		bool was_symlink = false;

#	if defined(__FreeBSD__) || defined(__DragonFly__)
		if (errno == EMLINK)
			was_symlink = true;

#	elif defined(__digital__) && defined(__unix__)
		if (errno == ENOTSUP)
			was_symlink = true;

#	elif defined(__NetBSD__)
		if (errno == EFTYPE)
			was_symlink = true;

#	else
		if (errno == ELOOP && !follow_symlinks) {
			const int saved_errno = errno;
			struct stat st;
			if (lstat(pair->src_name, &st) == 0
					&& S_ISLNK(st.st_mode))
				was_symlink = true;

			errno = saved_errno;
		}
#	endif

		if (was_symlink)
			message_warning(_("%s: Is a symbolic link, "
					"skipping"), pair->src_name);
		else
#endif
			// Something else than O_NOFOLLOW failing
			// (assuming that the race conditions didn't
			// confuse us).
			message_error("%s: %s", pair->src_name,
					strerror(errno));

		return true;
	}

	// Stat the source file. We need the result also when we copy
	// the permissions, and when unlinking.
	//
	// NOTE: Use stat() instead of fstat() with DJGPP, because
	// then we have a better chance to get st_ino value that can
	// be used in io_open_dest_real() to prevent overwriting the
	// source file.
#ifdef __DJGPP__
	if (stat(pair->src_name, &pair->src_st))
		goto error_msg;
#else
	if (fstat(pair->src_fd, &pair->src_st))
		goto error_msg;
#endif

	if (S_ISDIR(pair->src_st.st_mode)) {
		message_warning(_("%s: Is a directory, skipping"),
				pair->src_name);
		goto error;
	}

	if (reg_files_only && !S_ISREG(pair->src_st.st_mode)) {
		message_warning(_("%s: Not a regular file, skipping"),
				pair->src_name);
		goto error;
	}

#ifndef TUKLIB_DOSLIKE
	if (reg_files_only && !opt_force) {
		if (pair->src_st.st_mode & (S_ISUID | S_ISGID)) {
			// gzip rejects setuid and setgid files even
			// when --force was used. bzip2 doesn't check
			// for them, but calls fchown() after fchmod(),
			// and many systems automatically drop setuid
			// and setgid bits there.
			//
			// We accept setuid and setgid files if
			// --force was used. We drop these bits
			// explicitly in io_copy_attr().
			message_warning(_("%s: File has setuid or "
					"setgid bit set, skipping"),
					pair->src_name);
			goto error;
		}

		if (pair->src_st.st_mode & S_ISVTX) {
			message_warning(_("%s: File has sticky bit "
					"set, skipping"),
					pair->src_name);
			goto error;
		}

		if (pair->src_st.st_nlink > 1) {
			message_warning(_("%s: Input file has more "
					"than one hard link, "
					"skipping"), pair->src_name);
			goto error;
		}
	}

	// If it is something else than a regular file, wait until
	// there is input available. This way reading from FIFOs
	// will work when open() is used with O_NONBLOCK.
	if (!S_ISREG(pair->src_st.st_mode)) {
		signals_unblock();
		const io_wait_ret ret = io_wait(pair, -1, true);
		signals_block();

		if (ret != IO_WAIT_MORE)
			goto error;
	}
#endif

#ifdef HAVE_POSIX_FADVISE
	// It will fail with some special files like FIFOs but that is fine.
	(void)posix_fadvise(pair->src_fd, 0, 0,
			opt_mode == MODE_LIST
				? POSIX_FADV_RANDOM
				: POSIX_FADV_SEQUENTIAL);
#endif

	return false;

error_msg:
	message_error("%s: %s", pair->src_name, strerror(errno));
error:
	(void)close(pair->src_fd);
	return true;
}


extern file_pair *
io_open_src(const char *src_name)
{
	if (is_empty_filename(src_name))
		return NULL;

	// Since we have only one file open at a time, we can use
	// a statically allocated structure.
	static file_pair pair;

	pair = (file_pair){
		.src_name = src_name,
		.dest_name = NULL,
		.src_fd = -1,
		.dest_fd = -1,
		.src_eof = false,
		.dest_try_sparse = false,
		.dest_pending_sparse = 0,
	};

	// Block the signals, for which we have a custom signal handler, so
	// that we don't need to worry about EINTR.
	signals_block();
	const bool error = io_open_src_real(&pair);
	signals_unblock();

#ifdef ENABLE_SANDBOX
	if (!error)
		io_sandbox_enter(pair.src_fd);
#endif

	return error ? NULL : &pair;
}


/// \brief      Closes source file of the file_pair structure
///
/// \param      pair    File whose src_fd should be closed
/// \param      success If true, the file will be removed from the disk if
///                     closing succeeds and --keep hasn't been used.
static void
io_close_src(file_pair *pair, bool success)
{
#ifndef TUKLIB_DOSLIKE
	if (restore_stdin_flags) {
		assert(pair->src_fd == STDIN_FILENO);

		restore_stdin_flags = false;

		if (fcntl(STDIN_FILENO, F_SETFL, stdin_flags) == -1)
			message_error(_("Error restoring the status flags "
					"to standard input: %s"),
					strerror(errno));
	}
#endif

	if (pair->src_fd != STDIN_FILENO && pair->src_fd != -1) {
		// Close the file before possibly unlinking it. On DOS-like
		// systems this is always required since unlinking will fail
		// if the file is open. On POSIX systems it usually works
		// to unlink open files, but in some cases it doesn't and
		// one gets EBUSY in errno.
		//
		// xz 5.2.2 and older unlinked the file before closing it
		// (except on DOS-like systems). The old code didn't handle
		// EBUSY and could fail e.g. on some CIFS shares. The
		// advantage of unlinking before closing is negligible
		// (avoids a race between close() and stat()/lstat() and
		// unlink()), so let's keep this simple.
		(void)close(pair->src_fd);

		if (success && !opt_keep_original)
			io_unlink(pair->src_name, &pair->src_st);
	}

	return;
}


static bool
io_open_dest_real(file_pair *pair)
{
	if (opt_stdout || pair->src_fd == STDIN_FILENO) {
		// We don't modify or free() this.
		pair->dest_name = (char *)"(stdout)";
		pair->dest_fd = STDOUT_FILENO;
#ifdef TUKLIB_DOSLIKE
		setmode(STDOUT_FILENO, O_BINARY);
#else
		// Try to set O_NONBLOCK if it isn't already set.
		// If it fails, we assume that stdout is non-blocking
		// in practice. See the comments in io_open_src_real()
		// for similar situation with stdin.
		//
		// NOTE: O_APPEND may be unset later in this function
		// and it relies on stdout_flags being set here.
		stdout_flags = fcntl(STDOUT_FILENO, F_GETFL);
		if (stdout_flags == -1) {
			message_error(_("Error getting the file status flags "
					"from standard output: %s"),
					strerror(errno));
			return true;
		}

		if ((stdout_flags & O_NONBLOCK) == 0
				&& fcntl(STDOUT_FILENO, F_SETFL,
					stdout_flags | O_NONBLOCK) != -1)
				restore_stdout_flags = true;
#endif
	} else {
		pair->dest_name = suffix_get_dest_name(pair->src_name);
		if (pair->dest_name == NULL)
			return true;

#ifdef __DJGPP__
		struct stat st;
		if (stat(pair->dest_name, &st) == 0) {
			// Check that it isn't a special file like "prn".
			if (st.st_dev == -1) {
				message_error("%s: Refusing to write to "
						"a DOS special file",
						pair->dest_name);
				free(pair->dest_name);
				return true;
			}

			// Check that we aren't overwriting the source file.
			if (st.st_dev == pair->src_st.st_dev
					&& st.st_ino == pair->src_st.st_ino) {
				message_error("%s: Output file is the same "
						"as the input file",
						pair->dest_name);
				free(pair->dest_name);
				return true;
			}
		}
#endif

		// If --force was used, unlink the target file first.
		if (opt_force && unlink(pair->dest_name) && errno != ENOENT) {
			message_error(_("%s: Cannot remove: %s"),
					pair->dest_name, strerror(errno));
			free(pair->dest_name);
			return true;
		}

		// Open the file.
		int flags = O_WRONLY | O_BINARY | O_NOCTTY
				| O_CREAT | O_EXCL;
#ifndef TUKLIB_DOSLIKE
		flags |= O_NONBLOCK;
#endif
		const mode_t mode = S_IRUSR | S_IWUSR;
		pair->dest_fd = open(pair->dest_name, flags, mode);

		if (pair->dest_fd == -1) {
			message_error("%s: %s", pair->dest_name,
					strerror(errno));
			free(pair->dest_name);
			return true;
		}
	}

#ifndef TUKLIB_DOSLIKE
	// dest_st isn't used on DOS-like systems except as a dummy
	// argument to io_unlink(), so don't fstat() on such systems.
	if (fstat(pair->dest_fd, &pair->dest_st)) {
		// If fstat() really fails, we have a safe fallback here.
#	if defined(__VMS)
		pair->dest_st.st_ino[0] = 0;
		pair->dest_st.st_ino[1] = 0;
		pair->dest_st.st_ino[2] = 0;
#	else
		pair->dest_st.st_dev = 0;
		pair->dest_st.st_ino = 0;
#	endif
	} else if (try_sparse && opt_mode == MODE_DECOMPRESS) {
		// When writing to standard output, we need to be extra
		// careful:
		//  - It may be connected to something else than
		//    a regular file.
		//  - We aren't necessarily writing to a new empty file
		//    or to the end of an existing file.
		//  - O_APPEND may be active.
		//
		// TODO: I'm keeping this disabled for DOS-like systems
		// for now. FAT doesn't support sparse files, but NTFS
		// does, so maybe this should be enabled on Windows after
		// some testing.
		if (pair->dest_fd == STDOUT_FILENO) {
			if (!S_ISREG(pair->dest_st.st_mode))
				return false;

			if (stdout_flags & O_APPEND) {
				// Creating a sparse file is not possible
				// when O_APPEND is active (it's used by
				// shell's >> redirection). As I understand
				// it, it is safe to temporarily disable
				// O_APPEND in xz, because if someone
				// happened to write to the same file at the
				// same time, results would be bad anyway
				// (users shouldn't assume that xz uses any
				// specific block size when writing data).
				//
				// The write position may be something else
				// than the end of the file, so we must fix
				// it to start writing at the end of the file
				// to imitate O_APPEND.
				if (lseek(STDOUT_FILENO, 0, SEEK_END) == -1)
					return false;

				// Construct the new file status flags.
				// If O_NONBLOCK was set earlier in this
				// function, it must be kept here too.
				int flags = stdout_flags & ~O_APPEND;
				if (restore_stdout_flags)
					flags |= O_NONBLOCK;

				// If this fcntl() fails, we continue but won't
				// try to create sparse output. The original
				// flags will still be restored if needed (to
				// unset O_NONBLOCK) when the file is finished.
				if (fcntl(STDOUT_FILENO, F_SETFL, flags) == -1)
					return false;

				// Disabling O_APPEND succeeded. Mark
				// that the flags should be restored
				// in io_close_dest(). (This may have already
				// been set when enabling O_NONBLOCK.)
				restore_stdout_flags = true;

			} else if (lseek(STDOUT_FILENO, 0, SEEK_CUR)
					!= pair->dest_st.st_size) {
				// Writing won't start exactly at the end
				// of the file. We cannot use sparse output,
				// because it would probably corrupt the file.
				return false;
			}
		}

		pair->dest_try_sparse = true;
	}
#endif

	return false;
}


extern bool
io_open_dest(file_pair *pair)
{
	signals_block();
	const bool ret = io_open_dest_real(pair);
	signals_unblock();
	return ret;
}


/// \brief      Closes destination file of the file_pair structure
///
/// \param      pair    File whose dest_fd should be closed
/// \param      success If false, the file will be removed from the disk.
///
/// \return     Zero if closing succeeds. On error, -1 is returned and
///             error message printed.
static bool
io_close_dest(file_pair *pair, bool success)
{
#ifndef TUKLIB_DOSLIKE
	// If io_open_dest() has disabled O_APPEND, restore it here.
	if (restore_stdout_flags) {
		assert(pair->dest_fd == STDOUT_FILENO);

		restore_stdout_flags = false;

		if (fcntl(STDOUT_FILENO, F_SETFL, stdout_flags) == -1) {
			message_error(_("Error restoring the O_APPEND flag "
					"to standard output: %s"),
					strerror(errno));
			return true;
		}
	}
#endif

	if (pair->dest_fd == -1 || pair->dest_fd == STDOUT_FILENO)
		return false;

	if (close(pair->dest_fd)) {
		message_error(_("%s: Closing the file failed: %s"),
				pair->dest_name, strerror(errno));

		// Closing destination file failed, so we cannot trust its
		// contents. Get rid of junk:
		io_unlink(pair->dest_name, &pair->dest_st);
		free(pair->dest_name);
		return true;
	}

	// If the operation using this file wasn't successful, we git rid
	// of the junk file.
	if (!success)
		io_unlink(pair->dest_name, &pair->dest_st);

	free(pair->dest_name);

	return false;
}


extern void
io_close(file_pair *pair, bool success)
{
	// Take care of sparseness at the end of the output file.
	if (success && pair->dest_try_sparse
			&& pair->dest_pending_sparse > 0) {
		// Seek forward one byte less than the size of the pending
		// hole, then write one zero-byte. This way the file grows
		// to its correct size. An alternative would be to use
		// ftruncate() but that isn't portable enough (e.g. it
		// doesn't work with FAT on Linux; FAT isn't that important
		// since it doesn't support sparse files anyway, but we don't
		// want to create corrupt files on it).
		if (lseek(pair->dest_fd, pair->dest_pending_sparse - 1,
				SEEK_CUR) == -1) {
			message_error(_("%s: Seeking failed when trying "
					"to create a sparse file: %s"),
					pair->dest_name, strerror(errno));
			success = false;
		} else {
			const uint8_t zero[1] = { '\0' };
			if (io_write_buf(pair, zero, 1))
				success = false;
		}
	}

	signals_block();

	// Copy the file attributes. We need to skip this if destination
	// file isn't open or it is standard output.
	if (success && pair->dest_fd != -1 && pair->dest_fd != STDOUT_FILENO)
		io_copy_attrs(pair);

	// Close the destination first. If it fails, we must not remove
	// the source file!
	if (io_close_dest(pair, success))
		success = false;

	// Close the source file, and unlink it if the operation using this
	// file pair was successful and we haven't requested to keep the
	// source file.
	io_close_src(pair, success);

	signals_unblock();

	return;
}


extern void
io_fix_src_pos(file_pair *pair, size_t rewind_size)
{
	assert(rewind_size <= IO_BUFFER_SIZE);

	if (rewind_size > 0) {
		// This doesn't need to work on unseekable file descriptors,
		// so just ignore possible errors.
		(void)lseek(pair->src_fd, -(off_t)(rewind_size), SEEK_CUR);
	}

	return;
}


extern size_t
io_read(file_pair *pair, io_buf *buf_union, size_t size)
{
	// We use small buffers here.
	assert(size < SSIZE_MAX);

	uint8_t *buf = buf_union->u8;
	size_t left = size;

	while (left > 0) {
		const ssize_t amount = read(pair->src_fd, buf, left);

		if (amount == 0) {
			pair->src_eof = true;
			break;
		}

		if (amount == -1) {
			if (errno == EINTR) {
				if (user_abort)
					return SIZE_MAX;

				continue;
			}

#ifndef TUKLIB_DOSLIKE
			if (IS_EAGAIN_OR_EWOULDBLOCK(errno)) {
				const io_wait_ret ret = io_wait(pair,
						mytime_get_flush_timeout(),
						true);
				switch (ret) {
				case IO_WAIT_MORE:
					continue;

				case IO_WAIT_ERROR:
					return SIZE_MAX;

				case IO_WAIT_TIMEOUT:
					return size - left;

				default:
					message_bug();
				}
			}
#endif

			message_error(_("%s: Read error: %s"),
					pair->src_name, strerror(errno));

			return SIZE_MAX;
		}

		buf += (size_t)(amount);
		left -= (size_t)(amount);
	}

	return size - left;
}


extern bool
io_pread(file_pair *pair, io_buf *buf, size_t size, off_t pos)
{
	// Using lseek() and read() is more portable than pread() and
	// for us it is as good as real pread().
	if (lseek(pair->src_fd, pos, SEEK_SET) != pos) {
		message_error(_("%s: Error seeking the file: %s"),
				pair->src_name, strerror(errno));
		return true;
	}

	const size_t amount = io_read(pair, buf, size);
	if (amount == SIZE_MAX)
		return true;

	if (amount != size) {
		message_error(_("%s: Unexpected end of file"),
				pair->src_name);
		return true;
	}

	return false;
}


static bool
is_sparse(const io_buf *buf)
{
	assert(IO_BUFFER_SIZE % sizeof(uint64_t) == 0);

	for (size_t i = 0; i < ARRAY_SIZE(buf->u64); ++i)
		if (buf->u64[i] != 0)
			return false;

	return true;
}


static bool
io_write_buf(file_pair *pair, const uint8_t *buf, size_t size)
{
	assert(size < SSIZE_MAX);

	while (size > 0) {
		const ssize_t amount = write(pair->dest_fd, buf, size);
		if (amount == -1) {
			if (errno == EINTR) {
				if (user_abort)
					return true;

				continue;
			}

#ifndef TUKLIB_DOSLIKE
			if (IS_EAGAIN_OR_EWOULDBLOCK(errno)) {
				if (io_wait(pair, -1, false) == IO_WAIT_MORE)
					continue;

				return true;
			}
#endif

			// Handle broken pipe specially. gzip and bzip2
			// don't print anything on SIGPIPE. In addition,
			// gzip --quiet uses exit status 2 (warning) on
			// broken pipe instead of whatever raise(SIGPIPE)
			// would make it return. It is there to hide "Broken
			// pipe" message on some old shells (probably old
			// GNU bash).
			//
			// We don't do anything special with --quiet, which
			// is what bzip2 does too. If we get SIGPIPE, we
			// will handle it like other signals by setting
			// user_abort, and get EPIPE here.
			if (errno != EPIPE)
				message_error(_("%s: Write error: %s"),
					pair->dest_name, strerror(errno));

			return true;
		}

		buf += (size_t)(amount);
		size -= (size_t)(amount);
	}

	return false;
}


extern bool
io_write(file_pair *pair, const io_buf *buf, size_t size)
{
	assert(size <= IO_BUFFER_SIZE);

	if (pair->dest_try_sparse) {
		// Check if the block is sparse (contains only zeros). If it
		// sparse, we just store the amount and return. We will take
		// care of actually skipping over the hole when we hit the
		// next data block or close the file.
		//
		// Since io_close() requires that dest_pending_sparse > 0
		// if the file ends with sparse block, we must also return
		// if size == 0 to avoid doing the lseek().
		if (size == IO_BUFFER_SIZE) {
			if (is_sparse(buf)) {
				pair->dest_pending_sparse += size;
				return false;
			}
		} else if (size == 0) {
			return false;
		}

		// This is not a sparse block. If we have a pending hole,
		// skip it now.
		if (pair->dest_pending_sparse > 0) {
			if (lseek(pair->dest_fd, pair->dest_pending_sparse,
					SEEK_CUR) == -1) {
				message_error(_("%s: Seeking failed when "
						"trying to create a sparse "
						"file: %s"), pair->dest_name,
						strerror(errno));
				return true;
			}

			pair->dest_pending_sparse = 0;
		}
	}

	return io_write_buf(pair, buf->u8, size);
}

/* vi: set sw=4 ts=4: */
/*
 * Mini copy_file implementation for busybox
 *
 * Copyright (C) 2001 by Matt Kraai <kraai@alumni.carnegiemellon.edu>
 * SELinux support by Yuichi Nakamura <ynakam@hitachisoft.jp>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
#include "libbb.h"

// FEATURE_NON_POSIX_CP:
//
// POSIX: if exists and -i, ask (w/o -i assume yes).
// Then open w/o EXCL (yes, not unlink!).
// If open still fails and -f, try unlink, then try open again.
// Result: a mess:
// If dest is a (sym)link, we overwrite link destination!
// (or fail, if it points to dir/nonexistent location/etc).
// This is strange, but POSIX-correct.
// coreutils cp has --remove-destination to override this...

/* Called if open of destination, link creation etc fails.
 * errno must be set to relevant value ("why we cannot create dest?")
 * to give reasonable error message */
static int ask_and_unlink(const char *dest, int flags)
{
	int e = errno;

#if !ENABLE_FEATURE_NON_POSIX_CP
	if (!(flags & (FILEUTILS_FORCE|FILEUTILS_INTERACTIVE))) {
		/* Either it exists, or the *path* doesnt exist */
		bb_perror_msg("can't create '%s'", dest);
		return -1;
	}
#endif
	// else: act as if -f is always in effect.
	// We don't want "can't create" msg, we want unlink to be done
	// (silently unless -i). Why? POSIX cp usually succeeds with
	// O_TRUNC open of existing file, and user is left ignorantly happy.
	// With above block unconditionally enabled, non-POSIX cp
	// will complain a lot more than POSIX one.

	/* TODO: maybe we should do it only if ctty is present? */
	if (flags & FILEUTILS_INTERACTIVE) {
		// We would not do POSIX insanity. -i asks,
		// then _unlinks_ the offender. Presto.
		// (No "opening without O_EXCL", no "unlink only if -f")
		// Or else we will end up having 3 open()s!
		fprintf(stderr, "%s: overwrite '%s'? ", applet_name, dest);
		if (!bb_ask_y_confirmation())
			return 0; /* not allowed to overwrite */
	}
	if (unlink(dest) < 0) {
#if ENABLE_FEATURE_VERBOSE_CP_MESSAGE
		if (e == errno && e == ENOENT) {
			/* e == ENOTDIR is similar: path has non-dir component,
			 * but in this case we don't even reach copy_file() */
			bb_error_msg("can't create '%s': Path does not exist", dest);
			return -1; /* error */
		}
#endif
		errno = e; /* do not use errno from unlink */
		bb_perror_msg("can't create '%s'", dest);
		return -1; /* error */
	}
#if ENABLE_FEATURE_CP_LONG_OPTIONS
	if (flags & FILEUTILS_RMDEST)
		if (flags & FILEUTILS_VERBOSE)
			printf("removed '%s'\n", dest);
#endif
	return 1; /* ok (to try again) */
}

/* Return:
 * -1 error, copy not made
 *  0 copy is made or user answered "no" in interactive mode
 *    (failures to preserve mode/owner/times are not reported in exit code)
 */
int FAST_FUNC copy_file(const char *source, const char *dest, int flags)
{
	/* This is a recursive function, try to minimize stack usage */
	/* NB: each struct stat is ~100 bytes */
	struct stat source_stat;
	struct stat dest_stat;
	smallint retval = 0;
	smallint dest_exists = 0;
	smallint ovr;

/* Inverse of cp -d ("cp without -d") */
#define FLAGS_DEREF (flags & (FILEUTILS_DEREFERENCE + FILEUTILS_DEREFERENCE_L0))

	if ((FLAGS_DEREF ? stat : lstat)(source, &source_stat) < 0) {
		/* This may be a dangling symlink.
		 * Making [sym]links to dangling symlinks works, so... */
		if (flags & (FILEUTILS_MAKE_SOFTLINK|FILEUTILS_MAKE_HARDLINK))
			goto make_links;
		bb_perror_msg("can't stat '%s'", source);
		return -1;
	}

	if (lstat(dest, &dest_stat) < 0) {
		if (errno != ENOENT) {
			bb_perror_msg("can't stat '%s'", dest);
			return -1;
		}
	} else {
		if (source_stat.st_dev == dest_stat.st_dev
		 && source_stat.st_ino == dest_stat.st_ino
		) {
			bb_error_msg("'%s' and '%s' are the same file", source, dest);
			return -1;
		}
		dest_exists = 1;
	}

#if ENABLE_SELINUX
	if ((flags & FILEUTILS_PRESERVE_SECURITY_CONTEXT) && is_selinux_enabled() > 0) {
		security_context_t con;
		if (lgetfilecon(source, &con) >= 0) {
			if (setfscreatecon(con) < 0) {
				bb_perror_msg("can't set setfscreatecon %s", con);
				freecon(con);
				return -1;
			}
		} else if (errno == ENOTSUP || errno == ENODATA) {
			setfscreatecon_or_die(NULL);
		} else {
			bb_perror_msg("can't lgetfilecon %s", source);
			return -1;
		}
	}
#endif

	if (S_ISDIR(source_stat.st_mode)) {
		DIR *dp;
		const char *tp;
		struct dirent *d;
		mode_t saved_umask = 0;

		if (!(flags & FILEUTILS_RECUR)) {
			bb_error_msg("omitting directory '%s'", source);
			return -1;
		}

		/* Did we ever create source ourself before? */
		tp = is_in_ino_dev_hashtable(&source_stat);
		if (tp) {
			/* We did! it's a recursion! man the lifeboats... */
			bb_error_msg("recursion detected, omitting directory '%s'",
					source);
			return -1;
		}

		if (dest_exists) {
			if (!S_ISDIR(dest_stat.st_mode)) {
				bb_error_msg("target '%s' is not a directory", dest);
				return -1;
			}
			/* race here: user can substitute a symlink between
			 * this check and actual creation of files inside dest */
		} else {
			/* Create DEST */
			mode_t mode;
			saved_umask = umask(0);

			mode = source_stat.st_mode;
			if (!(flags & FILEUTILS_PRESERVE_STATUS))
				mode = source_stat.st_mode & ~saved_umask;
			/* Allow owner to access new dir (at least for now) */
			mode |= S_IRWXU;
			if (mkdir(dest, mode) < 0) {
				umask(saved_umask);
				bb_perror_msg("can't create directory '%s'", dest);
				return -1;
			}
			umask(saved_umask);
			/* need stat info for add_to_ino_dev_hashtable */
			if (lstat(dest, &dest_stat) < 0) {
				bb_perror_msg("can't stat '%s'", dest);
				return -1;
			}
		}
		/* remember (dev,inode) of each created dir.
		 * NULL: name is not remembered */
		add_to_ino_dev_hashtable(&dest_stat, NULL);

		/* Recursively copy files in SOURCE */
		dp = opendir(source);
		if (dp == NULL) {
			retval = -1;
			goto preserve_mode_ugid_time;
		}

		while ((d = readdir(dp)) != NULL) {
			char *new_source, *new_dest;

			new_source = concat_subpath_file(source, d->d_name);
			if (new_source == NULL)
				continue;
			new_dest = concat_path_file(dest, d->d_name);
			if (copy_file(new_source, new_dest, flags & ~FILEUTILS_DEREFERENCE_L0) < 0)
				retval = -1;
			free(new_source);
			free(new_dest);
		}
		closedir(dp);

		if (!dest_exists
		 && chmod(dest, source_stat.st_mode & ~saved_umask) < 0
		) {
			bb_perror_msg("can't preserve %s of '%s'", "permissions", dest);
			/* retval = -1; - WRONG! copy *WAS* made */
		}
		goto preserve_mode_ugid_time;
	}

	if (dest_exists) {
		if (flags & FILEUTILS_UPDATE) {
			if (source_stat.st_mtime <= dest_stat.st_mtime) {
				return 0; /* source file must be newer */
			}
		}
#if ENABLE_FEATURE_CP_LONG_OPTIONS
		if (flags & FILEUTILS_RMDEST) {
			ovr = ask_and_unlink(dest, flags);
			if (ovr <= 0)
				return ovr;
			dest_exists = 0;
		}
#endif
	}

	if (flags & (FILEUTILS_MAKE_SOFTLINK|FILEUTILS_MAKE_HARDLINK)) {
		int (*lf)(const char *oldpath, const char *newpath);
 make_links:
		/* Hmm... maybe
		 * if (DEREF && MAKE_SOFTLINK) source = realpath(source) ?
		 * (but realpath returns NULL on dangling symlinks...) */
		lf = (flags & FILEUTILS_MAKE_SOFTLINK) ? symlink : link;
		if (lf(source, dest) < 0) {
			ovr = ask_and_unlink(dest, flags);
			if (ovr <= 0)
				return ovr;
			if (lf(source, dest) < 0) {
				bb_perror_msg("can't create link '%s'", dest);
				return -1;
			}
		}
		/* _Not_ jumping to preserve_mode_ugid_time:
		 * (sym)links don't have those */
		return 0;
	}

	if (/* "cp thing1 thing2" without -R: just open and read() from thing1 */
	    !(flags & FILEUTILS_RECUR)
	    /* "cp [-opts] regular_file thing2" */
	 || S_ISREG(source_stat.st_mode)
	 /* DEREF uses stat, which never returns S_ISLNK() == true.
	  * So the below is never true: */
	 /* || (FLAGS_DEREF && S_ISLNK(source_stat.st_mode)) */
	) {
		int src_fd;
		int dst_fd;
		mode_t new_mode;

		if (!FLAGS_DEREF && S_ISLNK(source_stat.st_mode)) {
			/* "cp -d symlink dst": create a link */
			goto dont_cat;
		}

		if (ENABLE_FEATURE_PRESERVE_HARDLINKS && !FLAGS_DEREF) {
			const char *link_target;
			link_target = is_in_ino_dev_hashtable(&source_stat);
			if (link_target) {
				if (link(link_target, dest) < 0) {
					ovr = ask_and_unlink(dest, flags);
					if (ovr <= 0)
						return ovr;
					if (link(link_target, dest) < 0) {
						bb_perror_msg("can't create link '%s'", dest);
						return -1;
					}
				}
				return 0;
			}
			add_to_ino_dev_hashtable(&source_stat, dest);
		}

		src_fd = open_or_warn(source, O_RDONLY);
		if (src_fd < 0)
			return -1;

		/* Do not try to open with weird mode fields */
		new_mode = source_stat.st_mode;
		if (!S_ISREG(source_stat.st_mode))
			new_mode = 0666;

		if (ENABLE_FEATURE_NON_POSIX_CP || (flags & FILEUTILS_INTERACTIVE)) {
			/*
			 * O_CREAT|O_EXCL: require that file did not exist before creation
			 */
			dst_fd = open(dest, O_WRONLY|O_CREAT|O_EXCL, new_mode);
		} else { /* POSIX, and not "cp -i" */
			/*
			 * O_CREAT|O_TRUNC: create, or truncate (security problem versus (sym)link attacks)
			 */
			dst_fd = open(dest, O_WRONLY|O_CREAT|O_TRUNC, new_mode);
		}
		if (dst_fd == -1) {
			ovr = ask_and_unlink(dest, flags);
			if (ovr <= 0) {
				close(src_fd);
				return ovr;
			}
			/* It shouldn't exist. If it exists, do not open (symlink attack?) */
			dst_fd = open3_or_warn(dest, O_WRONLY|O_CREAT|O_EXCL, new_mode);
			if (dst_fd < 0) {
				close(src_fd);
				return -1;
			}
		}

#if ENABLE_SELINUX
		if ((flags & (FILEUTILS_PRESERVE_SECURITY_CONTEXT|FILEUTILS_SET_SECURITY_CONTEXT))
		 && is_selinux_enabled() > 0
		) {
			security_context_t con;
			if (getfscreatecon(&con) == -1) {
				bb_perror_msg("getfscreatecon");
				return -1;
			}
			if (con) {
				if (setfilecon(dest, con) == -1) {
					bb_perror_msg("setfilecon:%s,%s", dest, con);
					freecon(con);
					return -1;
				}
				freecon(con);
			}
		}
#endif
		if (bb_copyfd_eof(src_fd, dst_fd) == -1)
			retval = -1;
		/* Careful with writing... */
		if (close(dst_fd) < 0) {
			bb_perror_msg("error writing to '%s'", dest);
			retval = -1;
		}
		/* ...but read size is already checked by bb_copyfd_eof */
		close(src_fd);
		/* "cp /dev/something new_file" should not
		 * copy mode of /dev/something */
		if (!S_ISREG(source_stat.st_mode))
			return retval;
		goto preserve_mode_ugid_time;
	}
 dont_cat:

	/* Source is a symlink or a special file */
	/* We are lazy here, a bit lax with races... */
	if (dest_exists) {
		errno = EEXIST;
		ovr = ask_and_unlink(dest, flags);
		if (ovr <= 0)
			return ovr;
	}
	if (S_ISLNK(source_stat.st_mode)) {
		char *lpath = xmalloc_readlink_or_warn(source);
		if (lpath) {
			int r = symlink(lpath, dest);
			free(lpath);
			if (r < 0) {
				/* shared message */
				bb_perror_msg("can't create %slink '%s' to '%s'",
					"sym", dest, lpath
				);
				return -1;
			}
			if (flags & FILEUTILS_PRESERVE_STATUS)
				if (lchown(dest, source_stat.st_uid, source_stat.st_gid) < 0)
					bb_perror_msg("can't preserve %s of '%s'", "ownership", dest);
		}
		/* _Not_ jumping to preserve_mode_ugid_time:
		 * symlinks don't have those */
		goto verb_and_exit;
	}
	if (S_ISBLK(source_stat.st_mode) || S_ISCHR(source_stat.st_mode)
	 || S_ISSOCK(source_stat.st_mode) || S_ISFIFO(source_stat.st_mode)
	) {
		if (mknod(dest, source_stat.st_mode, source_stat.st_rdev) < 0) {
			bb_perror_msg("can't create '%s'", dest);
			return -1;
		}
	} else {
		bb_error_msg("unrecognized file '%s' with mode %x", source, source_stat.st_mode);
		return -1;
	}

 preserve_mode_ugid_time:

	if (flags & FILEUTILS_PRESERVE_STATUS
	/* Cannot happen: */
	/* && !(flags & (FILEUTILS_MAKE_SOFTLINK|FILEUTILS_MAKE_HARDLINK)) */
	) {
		struct timeval times[2];

		times[1].tv_sec = times[0].tv_sec = source_stat.st_mtime;
		times[1].tv_usec = times[0].tv_usec = 0;
		/* BTW, utimes sets usec-precision time - just FYI */
		if (utimes(dest, times) < 0)
			bb_perror_msg("can't preserve %s of '%s'", "times", dest);
		if (chown(dest, source_stat.st_uid, source_stat.st_gid) < 0) {
			source_stat.st_mode &= ~(S_ISUID | S_ISGID);
			bb_perror_msg("can't preserve %s of '%s'", "ownership", dest);
		}
		if (chmod(dest, source_stat.st_mode) < 0)
			bb_perror_msg("can't preserve %s of '%s'", "permissions", dest);
	}

 verb_and_exit:
	if (flags & FILEUTILS_VERBOSE) {
		printf("'%s' -> '%s'\n", source, dest);
	}

	return retval;
}

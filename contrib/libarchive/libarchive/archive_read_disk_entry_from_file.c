/*-
 * Copyright (c) 2003-2009 Tim Kientzle
 * Copyright (c) 2010-2012 Michihiro NAKAJIMA
 * Copyright (c) 2016 Martin Matuska
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

#include "archive_platform.h"
__FBSDID("$FreeBSD");

/* This is the tree-walking code for POSIX systems. */
#if !defined(_WIN32) || defined(__CYGWIN__)

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_EXTATTR_H
#include <sys/extattr.h>
#endif
#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif
#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#if defined(HAVE_SYS_XATTR_H)
#include <sys/xattr.h>
#elif defined(HAVE_ATTR_XATTR_H)
#include <attr/xattr.h>
#endif
#ifdef HAVE_SYS_EA_H
#include <sys/ea.h>
#endif
#ifdef HAVE_COPYFILE_H
#include <copyfile.h>
#endif
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif
#ifdef HAVE_LIMITS_H
#include <limits.h>
#endif
#ifdef HAVE_LINUX_TYPES_H
#include <linux/types.h>
#endif
#ifdef HAVE_LINUX_FIEMAP_H
#include <linux/fiemap.h>
#endif
#ifdef HAVE_LINUX_FS_H
#include <linux/fs.h>
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
#ifdef HAVE_PATHS_H
#include <paths.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "archive.h"
#include "archive_entry.h"
#include "archive_private.h"
#include "archive_read_disk_private.h"

#ifndef O_CLOEXEC
#define O_CLOEXEC	0
#endif

static int setup_mac_metadata(struct archive_read_disk *,
    struct archive_entry *, int *fd);
static int setup_xattrs(struct archive_read_disk *,
    struct archive_entry *, int *fd);
static int setup_sparse(struct archive_read_disk *,
    struct archive_entry *, int *fd);
#if defined(HAVE_LINUX_FIEMAP_H)
static int setup_sparse_fiemap(struct archive_read_disk *,
    struct archive_entry *, int *fd);
#endif

#if !ARCHIVE_ACL_SUPPORT
int
archive_read_disk_entry_setup_acls(struct archive_read_disk *a,
    struct archive_entry *entry, int *fd)
{
	(void)a;      /* UNUSED */
	(void)entry;  /* UNUSED */
	(void)fd;     /* UNUSED */
	return (ARCHIVE_OK);
}
#endif

/*
 * Enter working directory and return working pathname of archive_entry.
 * If a pointer to an integer is provided and its value is below zero
 * open a file descriptor on this pathname.
 */
const char *
archive_read_disk_entry_setup_path(struct archive_read_disk *a,
    struct archive_entry *entry, int *fd)
{
	const char *path;

	path = archive_entry_sourcepath(entry);

	if (path == NULL || (a->tree != NULL &&
	    a->tree_enter_working_dir(a->tree) != 0))
		path = archive_entry_pathname(entry);
	if (path == NULL) {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
		   "Couldn't determine path");
	} else if (fd != NULL && *fd < 0 && a->tree != NULL &&
	    (a->follow_symlinks || archive_entry_filetype(entry) != AE_IFLNK)) {
		*fd = a->open_on_current_dir(a->tree, path,
		    O_RDONLY | O_NONBLOCK);
	}
	return (path);
}

int
archive_read_disk_entry_from_file(struct archive *_a,
    struct archive_entry *entry,
    int fd,
    const struct stat *st)
{
	struct archive_read_disk *a = (struct archive_read_disk *)_a;
	const char *path, *name;
	struct stat s;
	int initial_fd = fd;
	int r, r1;

	archive_check_magic(_a, ARCHIVE_READ_DISK_MAGIC, ARCHIVE_STATE_ANY,
		"archive_read_disk_entry_from_file");

	archive_clear_error(_a);
	path = archive_entry_sourcepath(entry);
	if (path == NULL)
		path = archive_entry_pathname(entry);

	if (a->tree == NULL) {
		if (st == NULL) {
#if HAVE_FSTAT
			if (fd >= 0) {
				if (fstat(fd, &s) != 0) {
					archive_set_error(&a->archive, errno,
					    "Can't fstat");
					return (ARCHIVE_FAILED);
				}
			} else
#endif
#if HAVE_LSTAT
			if (!a->follow_symlinks) {
				if (lstat(path, &s) != 0) {
					archive_set_error(&a->archive, errno,
					    "Can't lstat %s", path);
					return (ARCHIVE_FAILED);
				}
			} else
#endif
			if (stat(path, &s) != 0) {
				archive_set_error(&a->archive, errno,
				    "Can't stat %s", path);
				return (ARCHIVE_FAILED);
			}
			st = &s;
		}
		archive_entry_copy_stat(entry, st);
	}

	/* Lookup uname/gname */
	name = archive_read_disk_uname(_a, archive_entry_uid(entry));
	if (name != NULL)
		archive_entry_copy_uname(entry, name);
	name = archive_read_disk_gname(_a, archive_entry_gid(entry));
	if (name != NULL)
		archive_entry_copy_gname(entry, name);

#ifdef HAVE_STRUCT_STAT_ST_FLAGS
	/* On FreeBSD, we get flags for free with the stat. */
	/* TODO: Does this belong in copy_stat()? */
	if ((a->flags & ARCHIVE_READDISK_NO_FFLAGS) == 0 && st->st_flags != 0)
		archive_entry_set_fflags(entry, st->st_flags, 0);
#endif

#if (defined(FS_IOC_GETFLAGS) && defined(HAVE_WORKING_FS_IOC_GETFLAGS)) || \
    (defined(EXT2_IOC_GETFLAGS) && defined(HAVE_WORKING_EXT2_IOC_GETFLAGS))
	/* Linux requires an extra ioctl to pull the flags.  Although
	 * this is an extra step, it has a nice side-effect: We get an
	 * open file descriptor which we can use in the subsequent lookups. */
	if ((a->flags & ARCHIVE_READDISK_NO_FFLAGS) == 0 &&
	    (S_ISREG(st->st_mode) || S_ISDIR(st->st_mode))) {
		if (fd < 0) {
			if (a->tree != NULL)
				fd = a->open_on_current_dir(a->tree, path,
					O_RDONLY | O_NONBLOCK | O_CLOEXEC);
			else
				fd = open(path, O_RDONLY | O_NONBLOCK |
						O_CLOEXEC);
			__archive_ensure_cloexec_flag(fd);
		}
		if (fd >= 0) {
			int stflags;
			r = ioctl(fd,
#if defined(FS_IOC_GETFLAGS)
			    FS_IOC_GETFLAGS,
#else
			    EXT2_IOC_GETFLAGS,
#endif
			    &stflags);
			if (r == 0 && stflags != 0)
				archive_entry_set_fflags(entry, stflags, 0);
		}
	}
#endif

#if defined(HAVE_READLINK) || defined(HAVE_READLINKAT)
	if (S_ISLNK(st->st_mode)) {
		size_t linkbuffer_len = st->st_size + 1;
		char *linkbuffer;
		int lnklen;

		linkbuffer = malloc(linkbuffer_len);
		if (linkbuffer == NULL) {
			archive_set_error(&a->archive, ENOMEM,
			    "Couldn't read link data");
			return (ARCHIVE_FAILED);
		}
		if (a->tree != NULL) {
#ifdef HAVE_READLINKAT
			lnklen = readlinkat(a->tree_current_dir_fd(a->tree),
			    path, linkbuffer, linkbuffer_len);
#else
			if (a->tree_enter_working_dir(a->tree) != 0) {
				archive_set_error(&a->archive, errno,
				    "Couldn't read link data");
				free(linkbuffer);
				return (ARCHIVE_FAILED);
			}
			lnklen = readlink(path, linkbuffer, linkbuffer_len);
#endif /* HAVE_READLINKAT */
		} else
			lnklen = readlink(path, linkbuffer, linkbuffer_len);
		if (lnklen < 0) {
			archive_set_error(&a->archive, errno,
			    "Couldn't read link data");
			free(linkbuffer);
			return (ARCHIVE_FAILED);
		}
		linkbuffer[lnklen] = 0;
		archive_entry_set_symlink(entry, linkbuffer);
		free(linkbuffer);
	}
#endif /* HAVE_READLINK || HAVE_READLINKAT */

	r = 0;
	if ((a->flags & ARCHIVE_READDISK_NO_ACL) == 0)
		r = archive_read_disk_entry_setup_acls(a, entry, &fd);
	if ((a->flags & ARCHIVE_READDISK_NO_XATTR) == 0) {
		r1 = setup_xattrs(a, entry, &fd);
		if (r1 < r)
			r = r1;
	}
	if (a->flags & ARCHIVE_READDISK_MAC_COPYFILE) {
		r1 = setup_mac_metadata(a, entry, &fd);
		if (r1 < r)
			r = r1;
	}
	r1 = setup_sparse(a, entry, &fd);
	if (r1 < r)
		r = r1;

	/* If we opened the file earlier in this function, close it. */
	if (initial_fd != fd)
		close(fd);
	return (r);
}

#if defined(__APPLE__) && defined(HAVE_COPYFILE_H)
/*
 * The Mac OS "copyfile()" API copies the extended metadata for a
 * file into a separate file in AppleDouble format (see RFC 1740).
 *
 * Mac OS tar and cpio implementations store this extended
 * metadata as a separate entry just before the regular entry
 * with a "._" prefix added to the filename.
 *
 * Note that this is currently done unconditionally; the tar program has
 * an option to discard this information before the archive is written.
 *
 * TODO: If there's a failure, report it and return ARCHIVE_WARN.
 */
static int
setup_mac_metadata(struct archive_read_disk *a,
    struct archive_entry *entry, int *fd)
{
	int tempfd = -1;
	int copyfile_flags = COPYFILE_NOFOLLOW | COPYFILE_ACL | COPYFILE_XATTR;
	struct stat copyfile_stat;
	int ret = ARCHIVE_OK;
	void *buff = NULL;
	int have_attrs;
	const char *name, *tempdir;
	struct archive_string tempfile;

	(void)fd; /* UNUSED */

	name = archive_read_disk_entry_setup_path(a, entry, NULL);
	if (name == NULL)
		return (ARCHIVE_WARN);

	/* Short-circuit if there's nothing to do. */
	have_attrs = copyfile(name, NULL, 0, copyfile_flags | COPYFILE_CHECK);
	if (have_attrs == -1) {
		archive_set_error(&a->archive, errno,
			"Could not check extended attributes");
		return (ARCHIVE_WARN);
	}
	if (have_attrs == 0)
		return (ARCHIVE_OK);

	tempdir = NULL;
	if (issetugid() == 0)
		tempdir = getenv("TMPDIR");
	if (tempdir == NULL)
		tempdir = _PATH_TMP;
	archive_string_init(&tempfile);
	archive_strcpy(&tempfile, tempdir);
	archive_strcat(&tempfile, "tar.md.XXXXXX");
	tempfd = mkstemp(tempfile.s);
	if (tempfd < 0) {
		archive_set_error(&a->archive, errno,
		    "Could not open extended attribute file");
		ret = ARCHIVE_WARN;
		goto cleanup;
	}
	__archive_ensure_cloexec_flag(tempfd);

	/* XXX I wish copyfile() could pack directly to a memory
	 * buffer; that would avoid the temp file here.  For that
	 * matter, it would be nice if fcopyfile() actually worked,
	 * that would reduce the many open/close races here. */
	if (copyfile(name, tempfile.s, 0, copyfile_flags | COPYFILE_PACK)) {
		archive_set_error(&a->archive, errno,
		    "Could not pack extended attributes");
		ret = ARCHIVE_WARN;
		goto cleanup;
	}
	if (fstat(tempfd, &copyfile_stat)) {
		archive_set_error(&a->archive, errno,
		    "Could not check size of extended attributes");
		ret = ARCHIVE_WARN;
		goto cleanup;
	}
	buff = malloc(copyfile_stat.st_size);
	if (buff == NULL) {
		archive_set_error(&a->archive, errno,
		    "Could not allocate memory for extended attributes");
		ret = ARCHIVE_WARN;
		goto cleanup;
	}
	if (copyfile_stat.st_size != read(tempfd, buff, copyfile_stat.st_size)) {
		archive_set_error(&a->archive, errno,
		    "Could not read extended attributes into memory");
		ret = ARCHIVE_WARN;
		goto cleanup;
	}
	archive_entry_copy_mac_metadata(entry, buff, copyfile_stat.st_size);

cleanup:
	if (tempfd >= 0) {
		close(tempfd);
		unlink(tempfile.s);
	}
	archive_string_free(&tempfile);
	free(buff);
	return (ret);
}

#else

/*
 * Stub implementation for non-Mac systems.
 */
static int
setup_mac_metadata(struct archive_read_disk *a,
    struct archive_entry *entry, int *fd)
{
	(void)a; /* UNUSED */
	(void)entry; /* UNUSED */
	(void)fd; /* UNUSED */
	return (ARCHIVE_OK);
}
#endif

#if ARCHIVE_XATTR_LINUX || ARCHIVE_XATTR_DARWIN || ARCHIVE_XATTR_AIX

/*
 * Linux, Darwin and AIX extended attribute support.
 *
 * TODO:  By using a stack-allocated buffer for the first
 * call to getxattr(), we might be able to avoid the second
 * call entirely.  We only need the second call if the
 * stack-allocated buffer is too small.  But a modest buffer
 * of 1024 bytes or so will often be big enough.  Same applies
 * to listxattr().
 */


static int
setup_xattr(struct archive_read_disk *a,
    struct archive_entry *entry, const char *name, int fd, const char *accpath)
{
	ssize_t size;
	void *value = NULL;


	if (fd >= 0) {
#if ARCHIVE_XATTR_LINUX
		size = fgetxattr(fd, name, NULL, 0);
#elif ARCHIVE_XATTR_DARWIN
		size = fgetxattr(fd, name, NULL, 0, 0, 0);
#elif ARCHIVE_XATTR_AIX
		size = fgetea(fd, name, NULL, 0);
#endif
	} else if (!a->follow_symlinks) {
#if ARCHIVE_XATTR_LINUX
		size = lgetxattr(accpath, name, NULL, 0);
#elif ARCHIVE_XATTR_DARWIN
		size = getxattr(accpath, name, NULL, 0, 0, XATTR_NOFOLLOW);
#elif ARCHIVE_XATTR_AIX
		size = lgetea(accpath, name, NULL, 0);
#endif
	} else {
#if ARCHIVE_XATTR_LINUX
		size = getxattr(accpath, name, NULL, 0);
#elif ARCHIVE_XATTR_DARWIN
		size = getxattr(accpath, name, NULL, 0, 0, 0);
#elif ARCHIVE_XATTR_AIX
		size = getea(accpath, name, NULL, 0);
#endif
	}

	if (size == -1) {
		archive_set_error(&a->archive, errno,
		    "Couldn't query extended attribute");
		return (ARCHIVE_WARN);
	}

	if (size > 0 && (value = malloc(size)) == NULL) {
		archive_set_error(&a->archive, errno, "Out of memory");
		return (ARCHIVE_FATAL);
	}


	if (fd >= 0) {
#if ARCHIVE_XATTR_LINUX
		size = fgetxattr(fd, name, value, size);
#elif ARCHIVE_XATTR_DARWIN
		size = fgetxattr(fd, name, value, size, 0, 0);
#elif ARCHIVE_XATTR_AIX
		size = fgetea(fd, name, value, size);
#endif
	} else if (!a->follow_symlinks) {
#if ARCHIVE_XATTR_LINUX
		size = lgetxattr(accpath, name, value, size);
#elif ARCHIVE_XATTR_DARWIN
		size = getxattr(accpath, name, value, size, 0, XATTR_NOFOLLOW);
#elif ARCHIVE_XATTR_AIX
		size = lgetea(accpath, name, value, size);
#endif
	} else {
#if ARCHIVE_XATTR_LINUX
		size = getxattr(accpath, name, value, size);
#elif ARCHIVE_XATTR_DARWIN
		size = getxattr(accpath, name, value, size, 0, 0);
#elif ARCHIVE_XATTR_AIX
		size = getea(accpath, name, value, size);
#endif
	}

	if (size == -1) {
		archive_set_error(&a->archive, errno,
		    "Couldn't read extended attribute");
		return (ARCHIVE_WARN);
	}

	archive_entry_xattr_add_entry(entry, name, value, size);

	free(value);
	return (ARCHIVE_OK);
}

static int
setup_xattrs(struct archive_read_disk *a,
    struct archive_entry *entry, int *fd)
{
	char *list, *p;
	const char *path;
	ssize_t list_size;

	path = NULL;

	if (*fd < 0) {
		path = archive_read_disk_entry_setup_path(a, entry, fd);
		if (path == NULL)
			return (ARCHIVE_WARN);
	}

	if (*fd >= 0) {
#if ARCHIVE_XATTR_LINUX
		list_size = flistxattr(*fd, NULL, 0);
#elif ARCHIVE_XATTR_DARWIN
		list_size = flistxattr(*fd, NULL, 0, 0);
#elif ARCHIVE_XATTR_AIX
		list_size = flistea(*fd, NULL, 0);
#endif
	} else if (!a->follow_symlinks) {
#if ARCHIVE_XATTR_LINUX
		list_size = llistxattr(path, NULL, 0);
#elif ARCHIVE_XATTR_DARWIN
		list_size = listxattr(path, NULL, 0, XATTR_NOFOLLOW);
#elif ARCHIVE_XATTR_AIX
		list_size = llistea(path, NULL, 0);
#endif
	} else {
#if ARCHIVE_XATTR_LINUX
		list_size = listxattr(path, NULL, 0);
#elif ARCHIVE_XATTR_DARWIN
		list_size = listxattr(path, NULL, 0, 0);
#elif ARCHIVE_XATTR_AIX
		list_size = listea(path, NULL, 0);
#endif
	}

	if (list_size == -1) {
		if (errno == ENOTSUP || errno == ENOSYS)
			return (ARCHIVE_OK);
		archive_set_error(&a->archive, errno,
			"Couldn't list extended attributes");
		return (ARCHIVE_WARN);
	}

	if (list_size == 0)
		return (ARCHIVE_OK);

	if ((list = malloc(list_size)) == NULL) {
		archive_set_error(&a->archive, errno, "Out of memory");
		return (ARCHIVE_FATAL);
	}

	if (*fd >= 0) {
#if ARCHIVE_XATTR_LINUX
		list_size = flistxattr(*fd, list, list_size);
#elif ARCHIVE_XATTR_DARWIN
		list_size = flistxattr(*fd, list, list_size, 0);
#elif ARCHIVE_XATTR_AIX
		list_size = flistea(*fd, list, list_size);
#endif
	} else if (!a->follow_symlinks) {
#if ARCHIVE_XATTR_LINUX
		list_size = llistxattr(path, list, list_size);
#elif ARCHIVE_XATTR_DARWIN
		list_size = listxattr(path, list, list_size, XATTR_NOFOLLOW);
#elif ARCHIVE_XATTR_AIX
		list_size = llistea(path, list, list_size);
#endif
	} else {
#if ARCHIVE_XATTR_LINUX
		list_size = listxattr(path, list, list_size);
#elif ARCHIVE_XATTR_DARWIN
		list_size = listxattr(path, list, list_size, 0);
#elif ARCHIVE_XATTR_AIX
		list_size = listea(path, list, list_size);
#endif
	}

	if (list_size == -1) {
		archive_set_error(&a->archive, errno,
			"Couldn't retrieve extended attributes");
		free(list);
		return (ARCHIVE_WARN);
	}

	for (p = list; (p - list) < list_size; p += strlen(p) + 1) {
#if ARCHIVE_XATTR_LINUX
		/* Linux: skip POSIX.1e ACL extended attributes */
		if (strncmp(p, "system.", 7) == 0 &&
		   (strcmp(p + 7, "posix_acl_access") == 0 ||
		    strcmp(p + 7, "posix_acl_default") == 0))
			continue;
		if (strncmp(p, "trusted.SGI_", 12) == 0 &&
		   (strcmp(p + 12, "ACL_DEFAULT") == 0 ||
		    strcmp(p + 12, "ACL_FILE") == 0))
			continue;

		/* Linux: xfsroot namespace is obsolete and unsupported */
		if (strncmp(p, "xfsroot.", 8) == 0)
			continue;
#endif
		setup_xattr(a, entry, p, *fd, path);
	}

	free(list);
	return (ARCHIVE_OK);
}

#elif ARCHIVE_XATTR_FREEBSD

/*
 * FreeBSD extattr interface.
 */

/* TODO: Implement this.  Follow the Linux model above, but
 * with FreeBSD-specific system calls, of course.  Be careful
 * to not include the system extattrs that hold ACLs; we handle
 * those separately.
 */
static int
setup_xattr(struct archive_read_disk *a, struct archive_entry *entry,
    int namespace, const char *name, const char *fullname, int fd,
    const char *path);

static int
setup_xattr(struct archive_read_disk *a, struct archive_entry *entry,
    int namespace, const char *name, const char *fullname, int fd,
    const char *accpath)
{
	ssize_t size;
	void *value = NULL;

	if (fd >= 0)
		size = extattr_get_fd(fd, namespace, name, NULL, 0);
	else if (!a->follow_symlinks)
		size = extattr_get_link(accpath, namespace, name, NULL, 0);
	else
		size = extattr_get_file(accpath, namespace, name, NULL, 0);

	if (size == -1) {
		archive_set_error(&a->archive, errno,
		    "Couldn't query extended attribute");
		return (ARCHIVE_WARN);
	}

	if (size > 0 && (value = malloc(size)) == NULL) {
		archive_set_error(&a->archive, errno, "Out of memory");
		return (ARCHIVE_FATAL);
	}

	if (fd >= 0)
		size = extattr_get_fd(fd, namespace, name, value, size);
	else if (!a->follow_symlinks)
		size = extattr_get_link(accpath, namespace, name, value, size);
	else
		size = extattr_get_file(accpath, namespace, name, value, size);

	if (size == -1) {
		free(value);
		archive_set_error(&a->archive, errno,
		    "Couldn't read extended attribute");
		return (ARCHIVE_WARN);
	}

	archive_entry_xattr_add_entry(entry, fullname, value, size);

	free(value);
	return (ARCHIVE_OK);
}

static int
setup_xattrs(struct archive_read_disk *a,
    struct archive_entry *entry, int *fd)
{
	char buff[512];
	char *list, *p;
	ssize_t list_size;
	const char *path;
	int namespace = EXTATTR_NAMESPACE_USER;

	path = NULL;

	if (*fd < 0) {
		path = archive_read_disk_entry_setup_path(a, entry, fd);
		if (path == NULL)
			return (ARCHIVE_WARN);
	}

	if (*fd >= 0)
		list_size = extattr_list_fd(*fd, namespace, NULL, 0);
	else if (!a->follow_symlinks)
		list_size = extattr_list_link(path, namespace, NULL, 0);
	else
		list_size = extattr_list_file(path, namespace, NULL, 0);

	if (list_size == -1 && errno == EOPNOTSUPP)
		return (ARCHIVE_OK);
	if (list_size == -1) {
		archive_set_error(&a->archive, errno,
			"Couldn't list extended attributes");
		return (ARCHIVE_WARN);
	}

	if (list_size == 0)
		return (ARCHIVE_OK);

	if ((list = malloc(list_size)) == NULL) {
		archive_set_error(&a->archive, errno, "Out of memory");
		return (ARCHIVE_FATAL);
	}

	if (*fd >= 0)
		list_size = extattr_list_fd(*fd, namespace, list, list_size);
	else if (!a->follow_symlinks)
		list_size = extattr_list_link(path, namespace, list, list_size);
	else
		list_size = extattr_list_file(path, namespace, list, list_size);

	if (list_size == -1) {
		archive_set_error(&a->archive, errno,
			"Couldn't retrieve extended attributes");
		free(list);
		return (ARCHIVE_WARN);
	}

	p = list;
	while ((p - list) < list_size) {
		size_t len = 255 & (int)*p;
		char *name;

		strcpy(buff, "user.");
		name = buff + strlen(buff);
		memcpy(name, p + 1, len);
		name[len] = '\0';
		setup_xattr(a, entry, namespace, name, buff, *fd, path);
		p += 1 + len;
	}

	free(list);
	return (ARCHIVE_OK);
}

#else

/*
 * Generic (stub) extended attribute support.
 */
static int
setup_xattrs(struct archive_read_disk *a,
    struct archive_entry *entry, int *fd)
{
	(void)a;     /* UNUSED */
	(void)entry; /* UNUSED */
	(void)fd;    /* UNUSED */
	return (ARCHIVE_OK);
}

#endif

#if defined(HAVE_LINUX_FIEMAP_H)

/*
 * Linux FIEMAP sparse interface.
 *
 * The FIEMAP ioctl returns an "extent" for each physical allocation
 * on disk.  We need to process those to generate a more compact list
 * of logical file blocks.  We also need to be very careful to use
 * FIEMAP_FLAG_SYNC here, since there are reports that Linux sometimes
 * does not report allocations for newly-written data that hasn't
 * been synced to disk.
 *
 * It's important to return a minimal sparse file list because we want
 * to not trigger sparse file extensions if we don't have to, since
 * not all readers support them.
 */

static int
setup_sparse_fiemap(struct archive_read_disk *a,
    struct archive_entry *entry, int *fd)
{
	char buff[4096];
	struct fiemap *fm;
	struct fiemap_extent *fe;
	int64_t size;
	int count, do_fiemap, iters;
	int exit_sts = ARCHIVE_OK;
	const char *path;

	if (archive_entry_filetype(entry) != AE_IFREG
	    || archive_entry_size(entry) <= 0
	    || archive_entry_hardlink(entry) != NULL)
		return (ARCHIVE_OK);

	if (*fd < 0) {
		path = archive_read_disk_entry_setup_path(a, entry, NULL);
		if (path == NULL)
			return (ARCHIVE_FAILED);

		if (a->tree != NULL)
			*fd = a->open_on_current_dir(a->tree, path,
				O_RDONLY | O_NONBLOCK | O_CLOEXEC);
		else
			*fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
		if (*fd < 0) {
			archive_set_error(&a->archive, errno,
			    "Can't open `%s'", path);
			return (ARCHIVE_FAILED);
		}
		__archive_ensure_cloexec_flag(*fd);
	}

	/* Initialize buffer to avoid the error valgrind complains about. */
	memset(buff, 0, sizeof(buff));
	count = (sizeof(buff) - sizeof(*fm))/sizeof(*fe);
	fm = (struct fiemap *)buff;
	fm->fm_start = 0;
	fm->fm_length = ~0ULL;;
	fm->fm_flags = FIEMAP_FLAG_SYNC;
	fm->fm_extent_count = count;
	do_fiemap = 1;
	size = archive_entry_size(entry);
	for (iters = 0; ; ++iters) {
		int i, r;

		r = ioctl(*fd, FS_IOC_FIEMAP, fm); 
		if (r < 0) {
			/* When something error happens, it is better we
			 * should return ARCHIVE_OK because an earlier
			 * version(<2.6.28) cannot perform FS_IOC_FIEMAP. */
			goto exit_setup_sparse_fiemap;
		}
		if (fm->fm_mapped_extents == 0) {
			if (iters == 0) {
				/* Fully sparse file; insert a zero-length "data" entry */
				archive_entry_sparse_add_entry(entry, 0, 0);
			}
			break;
		}
		fe = fm->fm_extents;
		for (i = 0; i < (int)fm->fm_mapped_extents; i++, fe++) {
			if (!(fe->fe_flags & FIEMAP_EXTENT_UNWRITTEN)) {
				/* The fe_length of the last block does not
				 * adjust itself to its size files. */
				int64_t length = fe->fe_length;
				if (fe->fe_logical + length > (uint64_t)size)
					length -= fe->fe_logical + length - size;
				if (fe->fe_logical == 0 && length == size) {
					/* This is not sparse. */
					do_fiemap = 0;
					break;
				}
				if (length > 0)
					archive_entry_sparse_add_entry(entry,
					    fe->fe_logical, length);
			}
			if (fe->fe_flags & FIEMAP_EXTENT_LAST)
				do_fiemap = 0;
		}
		if (do_fiemap) {
			fe = fm->fm_extents + fm->fm_mapped_extents -1;
			fm->fm_start = fe->fe_logical + fe->fe_length;
		} else
			break;
	}
exit_setup_sparse_fiemap:
	return (exit_sts);
}

#if !defined(SEEK_HOLE) || !defined(SEEK_DATA)
static int
setup_sparse(struct archive_read_disk *a,
    struct archive_entry *entry, int *fd)
{
	return setup_sparse_fiemap(a, entry, fd);
}
#endif
#endif	/* defined(HAVE_LINUX_FIEMAP_H) */

#if defined(SEEK_HOLE) && defined(SEEK_DATA)

/*
 * SEEK_HOLE sparse interface (FreeBSD, Linux, Solaris)
 */

static int
setup_sparse(struct archive_read_disk *a,
    struct archive_entry *entry, int *fd)
{
	int64_t size;
	off_t initial_off;
	off_t off_s, off_e;
	int exit_sts = ARCHIVE_OK;
	int check_fully_sparse = 0;
	const char *path;

	if (archive_entry_filetype(entry) != AE_IFREG
	    || archive_entry_size(entry) <= 0
	    || archive_entry_hardlink(entry) != NULL)
		return (ARCHIVE_OK);

	/* Does filesystem support the reporting of hole ? */
	if (*fd < 0)
		path = archive_read_disk_entry_setup_path(a, entry, fd);
	else
		path = NULL;

	if (*fd >= 0) {
#ifdef _PC_MIN_HOLE_SIZE
		if (fpathconf(*fd, _PC_MIN_HOLE_SIZE) <= 0)
			return (ARCHIVE_OK);
#endif
		initial_off = lseek(*fd, 0, SEEK_CUR);
		if (initial_off != 0)
			lseek(*fd, 0, SEEK_SET);
	} else {
		if (path == NULL)
			return (ARCHIVE_FAILED);
#ifdef _PC_MIN_HOLE_SIZE
		if (pathconf(path, _PC_MIN_HOLE_SIZE) <= 0)
			return (ARCHIVE_OK);
#endif
		*fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
		if (*fd < 0) {
			archive_set_error(&a->archive, errno,
			    "Can't open `%s'", path);
			return (ARCHIVE_FAILED);
		}
		__archive_ensure_cloexec_flag(*fd);
		initial_off = 0;
	}

#ifndef _PC_MIN_HOLE_SIZE
	/* Check if the underlying filesystem supports seek hole */
	off_s = lseek(*fd, 0, SEEK_HOLE);
	if (off_s < 0)
#if defined(HAVE_LINUX_FIEMAP_H)
		return setup_sparse_fiemap(a, entry, fd);
#else
		goto exit_setup_sparse;
#endif
	else if (off_s > 0)
		lseek(*fd, 0, SEEK_SET);
#endif

	off_s = 0;
	size = archive_entry_size(entry);
	while (off_s < size) {
		off_s = lseek(*fd, off_s, SEEK_DATA);
		if (off_s == (off_t)-1) {
			if (errno == ENXIO) {
				/* no more hole */
				if (archive_entry_sparse_count(entry) == 0) {
					/* Potentially a fully-sparse file. */
					check_fully_sparse = 1;
				}
				break;
			}
			archive_set_error(&a->archive, errno,
			    "lseek(SEEK_HOLE) failed");
			exit_sts = ARCHIVE_FAILED;
			goto exit_setup_sparse;
		}
		off_e = lseek(*fd, off_s, SEEK_HOLE);
		if (off_e == (off_t)-1) {
			if (errno == ENXIO) {
				off_e = lseek(*fd, 0, SEEK_END);
				if (off_e != (off_t)-1)
					break;/* no more data */
			}
			archive_set_error(&a->archive, errno,
			    "lseek(SEEK_DATA) failed");
			exit_sts = ARCHIVE_FAILED;
			goto exit_setup_sparse;
		}
		if (off_s == 0 && off_e == size)
			break;/* This is not sparse. */
		archive_entry_sparse_add_entry(entry, off_s,
			off_e - off_s);
		off_s = off_e;
	}

	if (check_fully_sparse) {
		if (lseek(*fd, 0, SEEK_HOLE) == 0 &&
			lseek(*fd, 0, SEEK_END) == size) {
			/* Fully sparse file; insert a zero-length "data" entry */
			archive_entry_sparse_add_entry(entry, 0, 0);
		}
	}
exit_setup_sparse:
	lseek(*fd, initial_off, SEEK_SET);
	return (exit_sts);
}

#elif !defined(HAVE_LINUX_FIEMAP_H)

/*
 * Generic (stub) sparse support.
 */
static int
setup_sparse(struct archive_read_disk *a,
    struct archive_entry *entry, int *fd)
{
	(void)a;     /* UNUSED */
	(void)entry; /* UNUSED */
	(void)fd;    /* UNUSED */
	return (ARCHIVE_OK);
}

#endif

#endif /* !defined(_WIN32) || defined(__CYGWIN__) */


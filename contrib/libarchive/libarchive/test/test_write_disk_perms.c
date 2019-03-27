/*-
 * Copyright (c) 2003-2007 Tim Kientzle
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
__FBSDID("$FreeBSD$");

#if !defined(_WIN32) || defined(__CYGWIN__)

#define UMASK 022

static long _default_gid = -1;
static long _invalid_gid = -1;
static long _alt_gid = -1;

/*
 * To fully test SGID restores, we need three distinct GIDs to work
 * with:
 *    * the GID that files are created with by default (for the
 *      current user in the current directory)
 *    * An "alt gid" that this user can create files with
 *    * An "invalid gid" that this user is not permitted to create
 *      files with.
 * The second fails if this user doesn't belong to at least two groups;
 * the third fails if the current user is root.
 */
static void
searchgid(void)
{
	static int   _searched = 0;
	uid_t uid = getuid();
	gid_t gid = 0;
	unsigned int n;
	struct stat st;
	int fd;

	/* If we've already looked this up, we're done. */
	if (_searched)
		return;
	_searched = 1;

	/* Create a file on disk in the current default dir. */
	fd = open("test_gid", O_CREAT | O_BINARY, 0664);
	failure("Couldn't create a file for gid testing.");
	assert(fd > 0);

	/* See what GID it ended up with.  This is our "valid" GID. */
	assert(fstat(fd, &st) == 0);
	_default_gid = st.st_gid;

	/* Find a GID for which fchown() fails.  This is our "invalid" GID. */
	_invalid_gid = -1;
	/* This loop stops when we wrap the gid or examine 10,000 gids. */
	for (gid = 1, n = 1; gid == n && n < 10000 ; n++, gid++) {
		if (fchown(fd, uid, gid) != 0) {
			_invalid_gid = gid;
			break;
		}
	}

	/*
	 * Find a GID for which fchown() succeeds, but which isn't the
	 * default.  This is the "alternate" gid.
	 */
	_alt_gid = -1;
	for (gid = 0, n = 0; gid == n && n < 10000 ; n++, gid++) {
		/* _alt_gid must be different than _default_gid */
		if (gid == (gid_t)_default_gid)
			continue;
		if (fchown(fd, uid, gid) == 0) {
			_alt_gid = gid;
			break;
		}
	}
	close(fd);
}

static int
altgid(void)
{
	searchgid();
	return (_alt_gid);
}

static int
invalidgid(void)
{
	searchgid();
	return (_invalid_gid);
}

static int
defaultgid(void)
{
	searchgid();
	return (_default_gid);
}
#endif

/*
 * Exercise permission and ownership restores.
 * In particular, try to exercise a bunch of border cases related
 * to files/dirs that already exist, SUID/SGID bits, etc.
 */

DEFINE_TEST(test_write_disk_perms)
{
#if defined(_WIN32) && !defined(__CYGWIN__)
	skipping("archive_write_disk interface");
#else
	struct archive *a;
	struct archive_entry *ae;
	struct stat st;
	uid_t original_uid;
	uid_t try_to_change_uid;

	assertUmask(UMASK);

	/*
	 * Set ownership of the current directory to the group of this
	 * process.  Otherwise, the SGID tests below fail if the
	 * /tmp directory is owned by a group to which we don't belong
	 * and we're on a system where group ownership is inherited.
	 * (Because we're not allowed to SGID files with defaultgid().)
	 */
	assertEqualInt(0, chown(".", getuid(), getgid()));

	/* Create an archive_write_disk object. */
	assert((a = archive_write_disk_new()) != NULL);

	/* Write a regular file to it. */
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_copy_pathname(ae, "file_0755");
	archive_entry_set_mode(ae, S_IFREG | 0777);
	assertEqualIntA(a, ARCHIVE_OK, archive_write_header(a, ae));
	assertEqualIntA(a, ARCHIVE_OK, archive_write_finish_entry(a));
	archive_entry_free(ae);

	/* Write a regular file, then write over it. */
	/* For files, the perms should get updated. */
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_copy_pathname(ae, "file_overwrite_0144");
	archive_entry_set_mode(ae, S_IFREG | 0777);
	assertEqualIntA(a, ARCHIVE_OK, archive_write_header(a, ae));
	archive_entry_free(ae);
	assertEqualIntA(a, ARCHIVE_OK, archive_write_finish_entry(a));
	/* Check that file was created with different perms. */
	assertEqualInt(0, stat("file_overwrite_0144", &st));
	failure("file_overwrite_0144: st.st_mode=%o", st.st_mode);
	assert((st.st_mode & 07777) != 0144);
	/* Overwrite, this should change the perms. */
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_copy_pathname(ae, "file_overwrite_0144");
	archive_entry_set_mode(ae, S_IFREG | 0144);
	assertEqualIntA(a, ARCHIVE_OK, archive_write_header(a, ae));
	archive_entry_free(ae);
	assertEqualIntA(a, ARCHIVE_OK, archive_write_finish_entry(a));

	/* Write a regular dir. */
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_copy_pathname(ae, "dir_0514");
	archive_entry_set_mode(ae, S_IFDIR | 0514);
	assertEqualIntA(a, ARCHIVE_OK, archive_write_header(a, ae));
	archive_entry_free(ae);
	assertEqualIntA(a, ARCHIVE_OK, archive_write_finish_entry(a));

	/* Overwrite an existing dir. */
	/* For dir, the first perms should get left. */
	assertMakeDir("dir_overwrite_0744", 0744);
	/* Check original perms. */
	assertEqualInt(0, stat("dir_overwrite_0744", &st));
	failure("dir_overwrite_0744: st.st_mode=%o", st.st_mode);
	assertEqualInt(st.st_mode & 0777, 0744);
	/* Overwrite shouldn't edit perms. */
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_copy_pathname(ae, "dir_overwrite_0744");
	archive_entry_set_mode(ae, S_IFDIR | 0777);
	assertEqualIntA(a, ARCHIVE_OK, archive_write_header(a, ae));
	archive_entry_free(ae);
	assertEqualIntA(a, ARCHIVE_OK, archive_write_finish_entry(a));
	/* Make sure they're unchanged. */
	assertEqualInt(0, stat("dir_overwrite_0744", &st));
	failure("dir_overwrite_0744: st.st_mode=%o", st.st_mode);
	assertEqualInt(st.st_mode & 0777, 0744);

	/* For dir, the owner should get left when not overwriting. */
	assertMakeDir("dir_owner", 0744);

	if (getuid() == 0) {
		original_uid = getuid() + 1;
		try_to_change_uid = getuid();
		assertEqualInt(0, chown("dir_owner", original_uid, getgid()));
	} else {
		original_uid = getuid();
		try_to_change_uid = getuid() + 1;
	}

	/* Check original owner. */
	assertEqualInt(0, stat("dir_owner", &st));
	failure("dir_owner: st.st_uid=%d", st.st_uid);
	assertEqualInt(st.st_uid, original_uid);
	/* Shouldn't try to edit the owner when no overwrite option is set. */
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_copy_pathname(ae, "dir_owner");
	archive_entry_set_mode(ae, S_IFDIR | 0744);
	archive_entry_set_uid(ae, try_to_change_uid);
	archive_write_disk_set_options(a,
	    ARCHIVE_EXTRACT_OWNER | ARCHIVE_EXTRACT_NO_OVERWRITE);
	assertEqualIntA(a, ARCHIVE_OK, archive_write_header(a, ae));
	archive_entry_free(ae);
	assertEqualIntA(a, ARCHIVE_OK, archive_write_finish_entry(a));
	/* Make sure they're unchanged. */
	assertEqualInt(0, stat("dir_owner", &st));
	failure("dir_owner: st.st_uid=%d", st.st_uid);
	assertEqualInt(st.st_uid, original_uid);

	/* Write a regular file with SUID bit, but don't use _EXTRACT_PERM. */
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_copy_pathname(ae, "file_no_suid");
	archive_entry_set_mode(ae, S_IFREG | S_ISUID | 0777);
	archive_write_disk_set_options(a, 0);
	assertEqualIntA(a, ARCHIVE_OK, archive_write_header(a, ae));
	assertEqualIntA(a, ARCHIVE_OK, archive_write_finish_entry(a));

	/* Write a regular file with ARCHIVE_EXTRACT_PERM. */
	assert(archive_entry_clear(ae) != NULL);
	archive_entry_copy_pathname(ae, "file_0777");
	archive_entry_set_mode(ae, S_IFREG | 0777);
	archive_write_disk_set_options(a, ARCHIVE_EXTRACT_PERM);
	assertEqualIntA(a, ARCHIVE_OK, archive_write_header(a, ae));
	assertEqualIntA(a, ARCHIVE_OK, archive_write_finish_entry(a));

	/* Write a regular file with ARCHIVE_EXTRACT_PERM & SUID bit */
	assert(archive_entry_clear(ae) != NULL);
	archive_entry_copy_pathname(ae, "file_4742");
	archive_entry_set_mode(ae, S_IFREG | S_ISUID | 0742);
	archive_entry_set_uid(ae, getuid());
	archive_write_disk_set_options(a, ARCHIVE_EXTRACT_PERM);
	assertEqualIntA(a, ARCHIVE_OK, archive_write_header(a, ae));
	assertEqualIntA(a, ARCHIVE_OK, archive_write_finish_entry(a));

	/*
	 * Write a regular file with ARCHIVE_EXTRACT_PERM & SUID bit,
	 * but wrong uid.  POSIX says you shouldn't restore SUID bit
	 * unless the UID could be restored.
	 */
	assert(archive_entry_clear(ae) != NULL);
	archive_entry_copy_pathname(ae, "file_bad_suid");
	archive_entry_set_mode(ae, S_IFREG | S_ISUID | 0742);
	archive_entry_set_uid(ae, getuid() + 1);
	archive_write_disk_set_options(a, ARCHIVE_EXTRACT_PERM);
	assertA(0 == archive_write_header(a, ae));
	/*
	 * Because we didn't ask for owner, the failure to
	 * restore SUID shouldn't return a failure.
	 * We check below to make sure SUID really wasn't set.
	 * See more detailed comments below.
	 */
	failure("Opportunistic SUID failure shouldn't return error.");
	assertEqualInt(0, archive_write_finish_entry(a));

        if (getuid() != 0) {
		assert(archive_entry_clear(ae) != NULL);
		archive_entry_copy_pathname(ae, "file_bad_suid2");
		archive_entry_set_mode(ae, S_IFREG | S_ISUID | 0742);
		archive_entry_set_uid(ae, getuid() + 1);
		archive_write_disk_set_options(a,
		    ARCHIVE_EXTRACT_PERM | ARCHIVE_EXTRACT_OWNER);
		assertA(0 == archive_write_header(a, ae));
		/* Owner change should fail here. */
		failure("Non-opportunistic SUID failure should return error.");
		assertEqualInt(ARCHIVE_WARN, archive_write_finish_entry(a));
	}

	/* Write a regular file with ARCHIVE_EXTRACT_PERM & SGID bit */
	assert(archive_entry_clear(ae) != NULL);
	archive_entry_copy_pathname(ae, "file_perm_sgid");
	archive_entry_set_mode(ae, S_IFREG | S_ISGID | 0742);
	archive_entry_set_gid(ae, defaultgid());
	archive_write_disk_set_options(a, ARCHIVE_EXTRACT_PERM);
	assertEqualIntA(a, ARCHIVE_OK, archive_write_header(a, ae));
	failure("Setting SGID bit should succeed here.");
	assertEqualIntA(a, 0, archive_write_finish_entry(a));

	if (altgid() == -1) {
		/*
		 * Current user must belong to at least two groups or
		 * else we can't test setting the GID to another group.
		 */
		skipping("Current user can't test gid restore: must belong to more than one group.");
	} else {
		/*
		 * Write a regular file with ARCHIVE_EXTRACT_PERM & SGID bit
		 * but without ARCHIVE_EXTRACT_OWNER.
		 */
		/*
		 * This is a weird case: The user has asked for permissions to
		 * be restored but not asked for ownership to be restored.  As
		 * a result, the default file creation will create a file with
		 * the wrong group.  There are several possible behaviors for
		 * libarchive in this scenario:
		 *  = Set the SGID bit.  It is wrong and a security hole to
		 *    set SGID with the wrong group.  Even POSIX thinks so.
		 *  = Implicitly set the group.  I don't like this.
		 *  = drop the SGID bit and warn (the old libarchive behavior)
		 *  = drop the SGID bit and don't warn (the current libarchive
		 *    behavior).
		 * The current behavior sees SGID/SUID restore when you
		 * don't ask for owner restore as an "opportunistic"
		 * action.  That is, libarchive should do it if it can,
		 * but if it can't, it's not an error.
		 */
		assert(archive_entry_clear(ae) != NULL);
		archive_entry_copy_pathname(ae, "file_alt_sgid");
		archive_entry_set_mode(ae, S_IFREG | S_ISGID | 0742);
		archive_entry_set_uid(ae, getuid());
		archive_entry_set_gid(ae, altgid());
		archive_write_disk_set_options(a, ARCHIVE_EXTRACT_PERM);
		assertEqualIntA(a, ARCHIVE_OK, archive_write_header(a, ae));
		failure("Setting SGID bit should fail because of group mismatch but the failure should be silent because we didn't ask for the group to be set.");
		assertEqualIntA(a, 0, archive_write_finish_entry(a));

		/*
		 * As above, but add _EXTRACT_OWNER to verify that it
		 * does succeed.
		 */
		assert(archive_entry_clear(ae) != NULL);
		archive_entry_copy_pathname(ae, "file_alt_sgid_owner");
		archive_entry_set_mode(ae, S_IFREG | S_ISGID | 0742);
		archive_entry_set_uid(ae, getuid());
		archive_entry_set_gid(ae, altgid());
		archive_write_disk_set_options(a,
		    ARCHIVE_EXTRACT_PERM | ARCHIVE_EXTRACT_OWNER);
		assertEqualIntA(a, ARCHIVE_OK, archive_write_header(a, ae));
		failure("Setting SGID bit should succeed here.");
		assertEqualIntA(a, ARCHIVE_OK, archive_write_finish_entry(a));
	}

	/*
	 * Write a regular file with ARCHIVE_EXTRACT_PERM & SGID bit,
	 * but wrong GID.  POSIX says you shouldn't restore SGID bit
	 * unless the GID could be restored.
	 */
	if (invalidgid() == -1) {
		/* This test always fails for root. */
		printf("Running as root: Can't test SGID failures.\n");
	} else {
		assert(archive_entry_clear(ae) != NULL);
		archive_entry_copy_pathname(ae, "file_bad_sgid");
		archive_entry_set_mode(ae, S_IFREG | S_ISGID | 0742);
		archive_entry_set_gid(ae, invalidgid());
		archive_write_disk_set_options(a, ARCHIVE_EXTRACT_PERM);
		assertA(0 == archive_write_header(a, ae));
		failure("This SGID restore should fail without an error.");
		assertEqualIntA(a, 0, archive_write_finish_entry(a));

		assert(archive_entry_clear(ae) != NULL);
		archive_entry_copy_pathname(ae, "file_bad_sgid2");
		archive_entry_set_mode(ae, S_IFREG | S_ISGID | 0742);
		archive_entry_set_gid(ae, invalidgid());
		archive_write_disk_set_options(a,
		    ARCHIVE_EXTRACT_PERM | ARCHIVE_EXTRACT_OWNER);
		assertA(0 == archive_write_header(a, ae));
		failure("This SGID restore should fail with an error.");
		assertEqualIntA(a, ARCHIVE_WARN, archive_write_finish_entry(a));
	}

	/* Set ownership should fail if we're not root. */
	if (getuid() == 0) {
		printf("Running as root: Can't test setuid failures.\n");
	} else {
		assert(archive_entry_clear(ae) != NULL);
		archive_entry_copy_pathname(ae, "file_bad_owner");
		archive_entry_set_mode(ae, S_IFREG | 0744);
		archive_entry_set_uid(ae, getuid() + 1);
		archive_write_disk_set_options(a, ARCHIVE_EXTRACT_OWNER);
		assertA(0 == archive_write_header(a, ae));
		assertEqualIntA(a,ARCHIVE_WARN,archive_write_finish_entry(a));
	}

	assertEqualInt(ARCHIVE_OK, archive_write_free(a));
	archive_entry_free(ae);

	/* Test the entries on disk. */
	assertEqualInt(0, stat("file_0755", &st));
	failure("file_0755: st.st_mode=%o", st.st_mode);
	assertEqualInt(st.st_mode & 07777, 0755);

	assertEqualInt(0, stat("file_overwrite_0144", &st));
	failure("file_overwrite_0144: st.st_mode=%o", st.st_mode);
	assertEqualInt(st.st_mode & 07777, 0144);

	assertEqualInt(0, stat("dir_0514", &st));
	failure("dir_0514: st.st_mode=%o", st.st_mode);
	assertEqualInt(st.st_mode & 07777, 0514);

	assertEqualInt(0, stat("dir_overwrite_0744", &st));
	failure("dir_overwrite_0744: st.st_mode=%o", st.st_mode);
	assertEqualInt(st.st_mode & 0777, 0744);

	assertEqualInt(0, stat("file_no_suid", &st));
	failure("file_0755: st.st_mode=%o", st.st_mode);
	assertEqualInt(st.st_mode & 07777, 0755);

	assertEqualInt(0, stat("file_0777", &st));
	failure("file_0777: st.st_mode=%o", st.st_mode);
	assertEqualInt(st.st_mode & 07777, 0777);

	/* SUID bit should get set here. */
	assertEqualInt(0, stat("file_4742", &st));
	failure("file_4742: st.st_mode=%o", st.st_mode);
	assertEqualInt(st.st_mode & 07777, S_ISUID | 0742);

	/* SUID bit should NOT have been set here. */
	assertEqualInt(0, stat("file_bad_suid", &st));
	failure("file_bad_suid: st.st_mode=%o", st.st_mode);
	assertEqualInt(st.st_mode & 07777, 0742);

	/* Some things don't fail if you're root, so suppress this. */
	if (getuid() != 0) {
		/* SUID bit should NOT have been set here. */
		assertEqualInt(0, stat("file_bad_suid2", &st));
		failure("file_bad_suid2: st.st_mode=%o", st.st_mode);
		assertEqualInt(st.st_mode & 07777, 0742);
	}

	/* SGID should be set here. */
	assertEqualInt(0, stat("file_perm_sgid", &st));
	failure("file_perm_sgid: st.st_mode=%o", st.st_mode);
	assertEqualInt(st.st_mode & 07777, S_ISGID | 0742);

	if (altgid() != -1) {
		/* SGID should not be set here. */
		assertEqualInt(0, stat("file_alt_sgid", &st));
		failure("file_alt_sgid: st.st_mode=%o", st.st_mode);
		assertEqualInt(st.st_mode & 07777, 0742);

		/* SGID should be set here. */
		assertEqualInt(0, stat("file_alt_sgid_owner", &st));
		failure("file_alt_sgid: st.st_mode=%o", st.st_mode);
		assertEqualInt(st.st_mode & 07777, S_ISGID | 0742);
	}

	if (invalidgid() != -1) {
		/* SGID should NOT be set here. */
		assertEqualInt(0, stat("file_bad_sgid", &st));
		failure("file_bad_sgid: st.st_mode=%o", st.st_mode);
		assertEqualInt(st.st_mode & 07777, 0742);
		/* SGID should NOT be set here. */
		assertEqualInt(0, stat("file_bad_sgid2", &st));
		failure("file_bad_sgid2: st.st_mode=%o", st.st_mode);
		assertEqualInt(st.st_mode & 07777, 0742);
	}

	if (getuid() != 0) {
		assertEqualInt(0, stat("file_bad_owner", &st));
		failure("file_bad_owner: st.st_mode=%o", st.st_mode);
		assertEqualInt(st.st_mode & 07777, 0744);
		failure("file_bad_owner: st.st_uid=%d getuid()=%d",
		    st.st_uid, getuid());
		/* The entry had getuid()+1, but because we're
		 * not root, we should not have been able to set that. */
		assertEqualInt(st.st_uid, getuid());
	}
#endif
}

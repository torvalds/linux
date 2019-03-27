/*-
 * Copyright (c) 2003-2008 Tim Kientzle
 * Copyright (c) 2017 Martin Matuska
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
__FBSDID("$FreeBSD: head/lib/libarchive/test/test_acl_freebsd.c 189427 2009-03-06 04:21:23Z kientzle $");

#if ARCHIVE_ACL_POSIX1E
#include <sys/acl.h>
#if HAVE_ACL_GET_PERM
#include <acl/libacl.h>
#define ACL_GET_PERM acl_get_perm
#elif HAVE_ACL_GET_PERM_NP
#define ACL_GET_PERM acl_get_perm_np
#endif

static struct archive_test_acl_t acls2[] = {
	{ ARCHIVE_ENTRY_ACL_TYPE_ACCESS, ARCHIVE_ENTRY_ACL_EXECUTE | ARCHIVE_ENTRY_ACL_READ,
	  ARCHIVE_ENTRY_ACL_USER_OBJ, -1, "" },
	{ ARCHIVE_ENTRY_ACL_TYPE_ACCESS, ARCHIVE_ENTRY_ACL_READ,
	  ARCHIVE_ENTRY_ACL_USER, 77, "user77" },
	{ ARCHIVE_ENTRY_ACL_TYPE_ACCESS, 0,
	  ARCHIVE_ENTRY_ACL_USER, 78, "user78" },
	{ ARCHIVE_ENTRY_ACL_TYPE_ACCESS, ARCHIVE_ENTRY_ACL_READ,
	  ARCHIVE_ENTRY_ACL_GROUP_OBJ, -1, "" },
	{ ARCHIVE_ENTRY_ACL_TYPE_ACCESS, 0007,
	  ARCHIVE_ENTRY_ACL_GROUP, 78, "group78" },
	{ ARCHIVE_ENTRY_ACL_TYPE_ACCESS,
	  ARCHIVE_ENTRY_ACL_WRITE | ARCHIVE_ENTRY_ACL_EXECUTE,
	  ARCHIVE_ENTRY_ACL_OTHER, -1, "" },
	{ ARCHIVE_ENTRY_ACL_TYPE_ACCESS,
	  ARCHIVE_ENTRY_ACL_WRITE | ARCHIVE_ENTRY_ACL_READ | ARCHIVE_ENTRY_ACL_EXECUTE,
	  ARCHIVE_ENTRY_ACL_MASK, -1, "" },
};

static int
#if ARCHIVE_ACL_SUNOS
acl_entry_get_perm(aclent_t *aclent)
#else
acl_entry_get_perm(acl_entry_t aclent)
#endif
{
	int permset = 0;
#if ARCHIVE_ACL_FREEBSD || ARCHIVE_ACL_LIBACL
	acl_permset_t opaque_ps;
#endif

#if ARCHIVE_ACL_SUNOS
	if (aclent->a_perm & 1)
		permset |= ARCHIVE_ENTRY_ACL_EXECUTE;
	if (aclent->a_perm & 2)
		permset |= ARCHIVE_ENTRY_ACL_WRITE;
	if (aclent->a_perm & 4)
		permset |= ARCHIVE_ENTRY_ACL_READ;
#else
	/* translate the silly opaque permset to a bitmap */
	acl_get_permset(aclent, &opaque_ps);
	if (ACL_GET_PERM(opaque_ps, ACL_EXECUTE))
		permset |= ARCHIVE_ENTRY_ACL_EXECUTE;
	if (ACL_GET_PERM(opaque_ps, ACL_WRITE))
		permset |= ARCHIVE_ENTRY_ACL_WRITE;
	if (ACL_GET_PERM(opaque_ps, ACL_READ))
		permset |= ARCHIVE_ENTRY_ACL_READ;
#endif
	return permset;
}

#if 0
static int
acl_get_specific_entry(acl_t acl, acl_tag_t requested_tag_type, int requested_tag) {
	int entry_id = ACL_FIRST_ENTRY;
	acl_entry_t acl_entry;
	acl_tag_t acl_tag_type;
	
	while (1 == acl_get_entry(acl, entry_id, &acl_entry)) {
		/* After the first time... */
		entry_id = ACL_NEXT_ENTRY;

		/* If this matches, return perm mask */
		acl_get_tag_type(acl_entry, &acl_tag_type);
		if (acl_tag_type == requested_tag_type) {
			switch (acl_tag_type) {
			case ACL_USER_OBJ:
				if ((uid_t)requested_tag == *(uid_t *)(acl_get_qualifier(acl_entry))) {
					return acl_entry_get_perm(acl_entry);
				}
				break;
			case ACL_GROUP_OBJ:
				if ((gid_t)requested_tag == *(gid_t *)(acl_get_qualifier(acl_entry))) {
					return acl_entry_get_perm(acl_entry);
				}
				break;
			case ACL_USER:
			case ACL_GROUP:
			case ACL_OTHER:
				return acl_entry_get_perm(acl_entry);
			default:
				failure("Unexpected ACL tag type");
				assert(0);
			}
		}


	}
	return -1;
}
#endif

#if ARCHIVE_ACL_SUNOS
static int
acl_match(aclent_t *aclent, struct archive_test_acl_t *myacl)
{

	if (myacl->permset != acl_entry_get_perm(aclent))
		return (0);

	switch (aclent->a_type) {
	case DEF_USER_OBJ:
	case USER_OBJ:
		if (myacl->tag != ARCHIVE_ENTRY_ACL_USER_OBJ) return (0);
		break;
		if (myacl->tag != ARCHIVE_ENTRY_ACL_USER)
			return (0);
		if ((uid_t)myacl->qual != aclent->a_id)
			return (0);
		break;
	case DEF_GROUP_OBJ:
	case GROUP_OBJ:
		if (myacl->tag != ARCHIVE_ENTRY_ACL_GROUP_OBJ) return (0);
		break;
	case DEF_GROUP:
	case GROUP:
		if (myacl->tag != ARCHIVE_ENTRY_ACL_GROUP)
			return (0);
		if ((gid_t)myacl->qual != aclent->a_id)
			return (0);
		break;
	case DEF_CLASS_OBJ:
	case CLASS_OBJ:
		if (myacl->tag != ARCHIVE_ENTRY_ACL_MASK) return (0);
		break;
	case DEF_OTHER_OBJ:
	case OTHER_OBJ:
		if (myacl->tag != ARCHIVE_ENTRY_ACL_OTHER) return (0);
		break;
	}
	return (1);
}

#else	/* ARCHIVE_ACL_FREEBSD || ARCHIVE_ACL_LIBACL */
static int
acl_match(acl_entry_t aclent, struct archive_test_acl_t *myacl)
{
	gid_t g, *gp;
	uid_t u, *up;
	acl_tag_t tag_type;

	if (myacl->permset != acl_entry_get_perm(aclent))
		return (0);

	acl_get_tag_type(aclent, &tag_type);
	switch (tag_type) {
	case ACL_USER_OBJ:
		if (myacl->tag != ARCHIVE_ENTRY_ACL_USER_OBJ) return (0);
		break;
	case ACL_USER:
		if (myacl->tag != ARCHIVE_ENTRY_ACL_USER)
			return (0);
		up = acl_get_qualifier(aclent);
		u = *up;
		acl_free(up);
		if ((uid_t)myacl->qual != u)
			return (0);
		break;
	case ACL_GROUP_OBJ:
		if (myacl->tag != ARCHIVE_ENTRY_ACL_GROUP_OBJ) return (0);
		break;
	case ACL_GROUP:
		if (myacl->tag != ARCHIVE_ENTRY_ACL_GROUP)
			return (0);
		gp = acl_get_qualifier(aclent);
		g = *gp;
		acl_free(gp);
		if ((gid_t)myacl->qual != g)
			return (0);
		break;
	case ACL_MASK:
		if (myacl->tag != ARCHIVE_ENTRY_ACL_MASK) return (0);
		break;
	case ACL_OTHER:
		if (myacl->tag != ARCHIVE_ENTRY_ACL_OTHER) return (0);
		break;
	}
	return (1);
}
#endif

static void
compare_acls(
#if ARCHIVE_ACL_SUNOS
    void *aclp, int aclcnt,
#else
    acl_t acl,
#endif
    struct archive_test_acl_t *myacls, int n)
{
	int *marker;
	int matched;
	int i;
#if ARCHIVE_ACL_SUNOS
	int e;
	aclent_t *acl_entry;
#else
	int entry_id = ACL_FIRST_ENTRY;
	acl_entry_t acl_entry;
#endif

	/* Count ACL entries in myacls array and allocate an indirect array. */
	marker = malloc(sizeof(marker[0]) * n);
	if (marker == NULL)
		return;
	for (i = 0; i < n; i++)
		marker[i] = i;

	/*
	 * Iterate over acls in system acl object, try to match each
	 * one with an item in the myacls array.
	 */
#if ARCHIVE_ACL_SUNOS
	for(e = 0; e < aclcnt; e++) {
		acl_entry = &((aclent_t *)aclp)[e];
#else
	while (1 == acl_get_entry(acl, entry_id, &acl_entry)) {
		/* After the first time... */
		entry_id = ACL_NEXT_ENTRY;
#endif

		/* Search for a matching entry (tag and qualifier) */
		for (i = 0, matched = 0; i < n && !matched; i++) {
			if (acl_match(acl_entry, &myacls[marker[i]])) {
				/* We found a match; remove it. */
				marker[i] = marker[n - 1];
				n--;
				matched = 1;
			}
		}

		/* TODO: Print out more details in this case. */
		failure("ACL entry on file that shouldn't be there");
		assert(matched == 1);
	}

	/* Dump entries in the myacls array that weren't in the system acl. */
	for (i = 0; i < n; ++i) {
		failure(" ACL entry missing from file: "
		    "type=%#010x,permset=%#010x,tag=%d,qual=%d,name=``%s''\n",
		    myacls[marker[i]].type, myacls[marker[i]].permset,
		    myacls[marker[i]].tag, myacls[marker[i]].qual,
		    myacls[marker[i]].name);
		assert(0); /* Record this as a failure. */
	}
	free(marker);
}
#endif

/*
 * Verify ACL restore-to-disk.  This test is Platform-specific.
 */

DEFINE_TEST(test_acl_platform_posix1e_restore)
{
#if !ARCHIVE_ACL_POSIX1E
	skipping("POSIX.1e ACLs are not supported on this platform");
#else	/* ARCHIVE_ACL_POSIX1E */
	struct stat st;
	struct archive *a;
	struct archive_entry *ae;
#if ARCHIVE_ACL_SUNOS
	void *aclp;
	int aclcnt;
#else
	acl_t acl;
#endif

	assertMakeFile("pretest", 0644, "a");

	if (setTestAcl("pretest") != ARCHIVE_TEST_ACL_TYPE_POSIX1E) {
		skipping("POSIX.1e ACLs are not writable on this filesystem");
		return;
	}

	/* Create a write-to-disk object. */
	assert(NULL != (a = archive_write_disk_new()));
	archive_write_disk_set_options(a,
	    ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM | ARCHIVE_EXTRACT_ACL);

	/* Populate an archive entry with some metadata, including ACL info */
	ae = archive_entry_new();
	assert(ae != NULL);
	archive_entry_set_pathname(ae, "test0");
	archive_entry_set_mtime(ae, 123456, 7890);
	archive_entry_set_size(ae, 0);
	assertEntrySetAcls(ae, acls2, sizeof(acls2)/sizeof(acls2[0]));
	assertEqualIntA(a, ARCHIVE_OK, archive_write_header(a, ae));
	archive_entry_free(ae);

	/* Close the archive. */
	assertEqualIntA(a, ARCHIVE_OK, archive_write_close(a));
	assertEqualInt(ARCHIVE_OK, archive_write_free(a));

	/* Verify the data on disk. */
	assertEqualInt(0, stat("test0", &st));
	assertEqualInt(st.st_mtime, 123456);
#if ARCHIVE_ACL_SUNOS
	aclp = sunacl_get(GETACL, &aclcnt, 0, "test0");
	failure("acl(): errno = %d (%s)", errno, strerror(errno));
	assert(aclp != NULL);
#else
	acl = acl_get_file("test0", ACL_TYPE_ACCESS);
	failure("acl_get_file(): errno = %d (%s)", errno, strerror(errno));
	assert(acl != (acl_t)NULL);
#endif
#if ARCHIVE_ACL_SUNOS
	compare_acls(aclp, aclcnt, acls2, sizeof(acls2)/sizeof(acls2[0]));
	free(aclp);
	aclp = NULL;
#else
	compare_acls(acl, acls2, sizeof(acls2)/sizeof(acls2[0]));
	acl_free(acl);
#endif

#endif	/* ARCHIVE_ACL_POSIX1E */
}

/*
 * Verify ACL read-from-disk.  This test is Platform-specific.
 */
DEFINE_TEST(test_acl_platform_posix1e_read)
{
#if !ARCHIVE_ACL_POSIX1E
	skipping("POSIX.1e ACLs are not supported on this platform");
#else /* ARCHIVE_ACL_POSIX1E */
	struct archive *a;
	struct archive_entry *ae;
	int n, fd, flags, dflags;
	char *func, *acl_text;
	const char *acl1_text, *acl2_text, *acl3_text;
#if ARCHIVE_ACL_SUNOS
	void *aclp;
	int aclcnt;
#else
	acl_t acl1, acl2, acl3;
#endif

	/*
	 * Manually construct a directory and two files with
	 * different ACLs.  This also serves to verify that ACLs
	 * are supported on the local filesystem.
	 */

	/* Create a test file f1 with acl1 */
#if ARCHIVE_ACL_SUNOS
	acl1_text = "user::rwx,"
	    "group::rwx,"
	    "other:rwx,"
	    "user:1:rw-,"
	    "group:15:r-x,"
	    "mask:rwx";
	aclent_t aclp1[] = {
	    { USER_OBJ, -1, 4 | 2 | 1 },
	    { USER, 1, 4 | 2 },
	    { GROUP_OBJ, -1, 4 | 2 | 1 },
	    { GROUP, 15, 4 | 1 },
	    { CLASS_OBJ, -1, 4 | 2 | 1 },
	    { OTHER_OBJ, -1, 4 | 2 | 1 }
	};
#else
	acl1_text = "user::rwx\n"
	    "group::rwx\n"
	    "other::rwx\n"
	    "user:1:rw-\n"
	    "group:15:r-x\n"
	    "mask::rwx";
	acl1 = acl_from_text(acl1_text);
	failure("acl_from_text(): errno = %d (%s)", errno, strerror(errno));
	assert((void *)acl1 != NULL);
#endif
	fd = open("f1", O_WRONLY | O_CREAT | O_EXCL, 0777);
	failure("Could not create test file?!");
	if (!assert(fd >= 0)) {
#if !ARCHIVE_ACL_SUNOS
		acl_free(acl1);
#endif
		return;
	}
#if ARCHIVE_ACL_SUNOS
	/* Check if Solaris filesystem supports POSIX.1e ACLs */
	aclp = sunacl_get(GETACL, &aclcnt, fd, NULL);
	if (aclp == 0)
		close(fd);
	if (errno == ENOSYS || errno == ENOTSUP) {
		skipping("POSIX.1e ACLs are not supported on this filesystem");
		return;
	}
	failure("facl(): errno = %d (%s)", errno, strerror(errno));
	assert(aclp != NULL);

	func = "facl()";
	n = facl(fd, SETACL, (int)(sizeof(aclp1)/sizeof(aclp1[0])), aclp1);
#else
	func = "acl_set_fd()";
	n = acl_set_fd(fd, acl1);
#endif
#if !ARCHIVE_ACL_SUNOS
	acl_free(acl1);
#endif

	if (n != 0) {
#if ARCHIVE_ACL_SUNOS
		if (errno == ENOSYS || errno == ENOTSUP)
#else
		if (errno == EOPNOTSUPP || errno == EINVAL)
#endif
		{
			close(fd);
			skipping("POSIX.1e ACLs are not supported on this filesystem");
			return;
		}
	}
	failure("%s: errno = %d (%s)", func, errno, strerror(errno));
	assertEqualInt(0, n);

	close(fd);

	assertMakeDir("d", 0700);

	/*
	 * Create file d/f1 with acl2
	 *
	 * This differs from acl1 in the u:1: and g:15: permissions.
	 *
	 * This file deliberately has the same name but a different ACL.
	 * Github Issue #777 explains how libarchive's directory traversal
	 * did not always correctly enter directories before attempting
	 * to read ACLs, resulting in reading the ACL from a like-named
	 * file in the wrong directory.
	 */
#if ARCHIVE_ACL_SUNOS
	acl2_text = "user::rwx,"
	    "group::rwx,"
	    "other:---,"
	    "user:1:r--,"
	    "group:15:r--,"
	    "mask:rwx";
	aclent_t aclp2[] = {
	    { USER_OBJ, -1, 4 | 2 | 1 },
	    { USER, 1, 4 },
	    { GROUP_OBJ, -1, 4 | 2 | 1},
	    { GROUP, 15, 4 },
	    { CLASS_OBJ, -1, 4 | 2 | 1},
	    { OTHER_OBJ, -1, 0 }
	};
#else
	acl2_text = "user::rwx\n"
	    "group::rwx\n"
	    "other::---\n"
	    "user:1:r--\n"
	    "group:15:r--\n"
	    "mask::rwx";
	acl2 = acl_from_text(acl2_text);
	failure("acl_from_text(): errno = %d (%s)", errno, strerror(errno));
	assert((void *)acl2 != NULL);
#endif
	fd = open("d/f1", O_WRONLY | O_CREAT | O_EXCL, 0777);
	failure("Could not create test file?!");
	if (!assert(fd >= 0)) {
#if !ARCHIVE_ACL_SUNOS
		acl_free(acl2);
#endif
		return;
	}
#if ARCHIVE_ACL_SUNOS
	func = "facl()";
	n = facl(fd, SETACL, (int)(sizeof(aclp2) / sizeof(aclp2[0])), aclp2);
#else
	func = "acl_set_fd()";
	n = acl_set_fd(fd, acl2);
	acl_free(acl2);
#endif
	if (n != 0)
		close(fd);
	failure("%s: errno = %d (%s)", func, errno, strerror(errno));
	assertEqualInt(0, n);
	close(fd);

	/* Create nested directory d2 with default ACLs */
	assertMakeDir("d/d2", 0755);

#if ARCHIVE_ACL_SUNOS
	acl3_text = "user::rwx,"
	    "group::r-x,"
	    "other:r-x,"
	    "user:2:r--,"
	    "group:16:-w-,"
	    "mask:rwx,"
	    "default:user::rwx,"
	    "default:user:1:r--,"
	    "default:group::r-x,"
	    "default:group:15:r--,"
	    "default:mask:rwx,"
	    "default:other:r-x";
	aclent_t aclp3[] = {
	    { USER_OBJ, -1, 4 | 2 | 1 },
	    { USER, 2, 4 },
	    { GROUP_OBJ, -1, 4 | 1 },
	    { GROUP, 16, 2 },
	    { CLASS_OBJ, -1, 4 | 2 | 1 },
	    { OTHER_OBJ, -1, 4 | 1 },
	    { USER_OBJ | ACL_DEFAULT, -1, 4 | 2 | 1 },
	    { USER | ACL_DEFAULT, 1, 4 },
	    { GROUP_OBJ | ACL_DEFAULT, -1, 4 | 1 },
	    { GROUP | ACL_DEFAULT, 15, 4 },
	    { CLASS_OBJ | ACL_DEFAULT, -1, 4 | 2 | 1},
	    { OTHER_OBJ | ACL_DEFAULT, -1, 4 | 1 }
	};
#else
	acl3_text = "user::rwx\n"
	    "user:1:r--\n"
	    "group::r-x\n"
	    "group:15:r--\n"
	    "mask::rwx\n"
	    "other::r-x";
	acl3 = acl_from_text(acl3_text);
	failure("acl_from_text(): errno = %d (%s)", errno, strerror(errno));
	assert((void *)acl3 != NULL);
#endif

#if ARCHIVE_ACL_SUNOS
	func = "acl()";
	n = acl("d/d2", SETACL, (int)(sizeof(aclp3) / sizeof(aclp3[0])), aclp3);
#else
	func = "acl_set_file()";
	n = acl_set_file("d/d2", ACL_TYPE_DEFAULT, acl3);
	acl_free(acl3);
#endif
	failure("%s: errno = %d (%s)", func, errno, strerror(errno));
	assertEqualInt(0, n);

	/* Create a read-from-disk object. */
	assert(NULL != (a = archive_read_disk_new()));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_disk_open(a, "."));
	assert(NULL != (ae = archive_entry_new()));

#if ARCHIVE_ACL_SUNOS
	flags = ARCHIVE_ENTRY_ACL_TYPE_POSIX1E
	    | ARCHIVE_ENTRY_ACL_STYLE_SEPARATOR_COMMA
	    | ARCHIVE_ENTRY_ACL_STYLE_SOLARIS;
	dflags = flags;
#else
	flags = ARCHIVE_ENTRY_ACL_TYPE_ACCESS;
	dflags = ARCHIVE_ENTRY_ACL_TYPE_DEFAULT;
#endif

	/* Walk the dir until we see both of the files */
	while (ARCHIVE_OK == archive_read_next_header2(a, ae)) {
		archive_read_disk_descend(a);
		if (strcmp(archive_entry_pathname(ae), "./f1") == 0) {
			acl_text = archive_entry_acl_to_text(ae, NULL, flags);
			assertEqualString(acl_text, acl1_text);
			free(acl_text);
		} else if (strcmp(archive_entry_pathname(ae), "./d/f1") == 0) {
			acl_text = archive_entry_acl_to_text(ae, NULL, flags);
			assertEqualString(acl_text, acl2_text);
			free(acl_text);
		} else if (strcmp(archive_entry_pathname(ae), "./d/d2") == 0) {
			acl_text = archive_entry_acl_to_text(ae, NULL, dflags);
			assertEqualString(acl_text, acl3_text);
			free(acl_text);
		}
	}

	archive_entry_free(ae);
	assertEqualInt(ARCHIVE_OK, archive_free(a));
#endif /* ARCHIVE_ACL_POSIX1E */
}

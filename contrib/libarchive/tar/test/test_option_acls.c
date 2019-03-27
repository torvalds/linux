/*-
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
__FBSDID("$FreeBSD$");

#if ARCHIVE_ACL_FREEBSD || ARCHIVE_ACL_DARWIN || ARCHIVE_ACL_LIBACL
static const acl_perm_t acl_perms[] = {
#if ARCHIVE_ACL_DARWIN
    ACL_READ_DATA,
    ACL_LIST_DIRECTORY,
    ACL_WRITE_DATA,
    ACL_ADD_FILE,
    ACL_EXECUTE,
    ACL_SEARCH,
    ACL_DELETE,
    ACL_APPEND_DATA,
    ACL_ADD_SUBDIRECTORY,
    ACL_DELETE_CHILD,
    ACL_READ_ATTRIBUTES,
    ACL_WRITE_ATTRIBUTES,
    ACL_READ_EXTATTRIBUTES,
    ACL_WRITE_EXTATTRIBUTES,
    ACL_READ_SECURITY,
    ACL_WRITE_SECURITY,
    ACL_CHANGE_OWNER,
    ACL_SYNCHRONIZE
#else /* !ARCHIVE_ACL_DARWIN */
    ACL_EXECUTE,
    ACL_WRITE,
    ACL_READ,
#if ARCHIVE_ACL_FREEBSD_NFS4
    ACL_READ_DATA,
    ACL_LIST_DIRECTORY,
    ACL_WRITE_DATA,
    ACL_ADD_FILE,
    ACL_APPEND_DATA,
    ACL_ADD_SUBDIRECTORY,
    ACL_READ_NAMED_ATTRS,
    ACL_WRITE_NAMED_ATTRS,
    ACL_DELETE_CHILD,
    ACL_READ_ATTRIBUTES,
    ACL_WRITE_ATTRIBUTES,
    ACL_DELETE,
    ACL_READ_ACL,
    ACL_WRITE_ACL,
    ACL_WRITE_OWNER,
    ACL_SYNCHRONIZE
#endif	/* ARCHIVE_ACL_FREEBSD_NFS4 */
#endif /* !ARCHIVE_ACL_DARWIN */
};
#if ARCHIVE_ACL_DARWIN || ARCHIVE_ACL_FREEBSD_NFS4
static const acl_flag_t acl_flags[] = {
#if ARCHIVE_ACL_DARWIN
    ACL_ENTRY_INHERITED,
    ACL_ENTRY_FILE_INHERIT,
    ACL_ENTRY_DIRECTORY_INHERIT,
    ACL_ENTRY_LIMIT_INHERIT,
    ACL_ENTRY_ONLY_INHERIT
#else	/* ARCHIVE_ACL_FREEBSD_NFS4 */
    ACL_ENTRY_FILE_INHERIT,
    ACL_ENTRY_DIRECTORY_INHERIT,
    ACL_ENTRY_NO_PROPAGATE_INHERIT,
    ACL_ENTRY_INHERIT_ONLY,
    ACL_ENTRY_SUCCESSFUL_ACCESS,
    ACL_ENTRY_FAILED_ACCESS,
#ifdef ACL_ENTRY_INHERITED
    ACL_ENTRY_INHERITED
#endif
#endif	/* ARCHIVE_ACL_FREEBSD_NFS4 */
};
#endif /* ARCHIVE_ACL_DARWIN || ARCHIVE_ACL_FREEBSD_NFS4 */

/*
 * Compare two ACL entries on FreeBSD or on Mac OS X
 */
static int
compare_acl_entry(acl_entry_t ae_a, acl_entry_t ae_b, int is_nfs4)
{
	acl_tag_t tag_a, tag_b;
	acl_permset_t permset_a, permset_b;
	int perm_a, perm_b, perm_start, perm_end;
	void *qual_a, *qual_b;
#if ARCHIVE_ACL_FREEBSD_NFS4
	acl_entry_type_t type_a, type_b;
#endif
#if ARCHIVE_ACL_FREEBSD_NFS4 || ARCHIVE_ACL_DARWIN
	acl_flagset_t flagset_a, flagset_b;
	int flag_a, flag_b;
#endif
	int i, r;


	/* Compare ACL tag */
	r = acl_get_tag_type(ae_a, &tag_a);
	failure("acl_get_tag_type() error: %s", strerror(errno));
	if (assertEqualInt(r, 0) == 0)
		return (-1);
	r = acl_get_tag_type(ae_b, &tag_b);
	failure("acl_get_tag_type() error: %s", strerror(errno));
	if (assertEqualInt(r, 0) == 0)
		return (-1);
	if (tag_a != tag_b)
		return (0);

	/* Compare ACL qualifier */
#if ARCHIVE_ACL_DARWIN
	if (tag_a == ACL_EXTENDED_ALLOW || tag_b == ACL_EXTENDED_DENY)
#else
	if (tag_a == ACL_USER || tag_a == ACL_GROUP)
#endif
	{
		qual_a = acl_get_qualifier(ae_a);
		failure("acl_get_qualifier() error: %s", strerror(errno));
		if (assert(qual_a != NULL) == 0)
			return (-1);
		qual_b = acl_get_qualifier(ae_b);
		failure("acl_get_qualifier() error: %s", strerror(errno));
		if (assert(qual_b != NULL) == 0) {
			acl_free(qual_a);
			return (-1);
		}
#if ARCHIVE_ACL_DARWIN
		if (memcmp(((guid_t *)qual_a)->g_guid,
		    ((guid_t *)qual_b)->g_guid, KAUTH_GUID_SIZE) != 0)
#else
		if ((tag_a == ACL_USER &&
		    (*(uid_t *)qual_a != *(uid_t *)qual_b)) ||
		    (tag_a == ACL_GROUP &&
		    (*(gid_t *)qual_a != *(gid_t *)qual_b)))
#endif
		{
			acl_free(qual_a);
			acl_free(qual_b);
			return (0);
		}
		acl_free(qual_a);
		acl_free(qual_b);
	}

#if ARCHIVE_ACL_FREEBSD_NFS4
	if (is_nfs4) {
		/* Compare NFS4 ACL type */
		r = acl_get_entry_type_np(ae_a, &type_a);
		failure("acl_get_entry_type_np() error: %s", strerror(errno));
		if (assertEqualInt(r, 0) == 0)
			return (-1);
		r = acl_get_entry_type_np(ae_b, &type_b);
		failure("acl_get_entry_type_np() error: %s", strerror(errno));
		if (assertEqualInt(r, 0) == 0)
			return (-1);
		if (type_a != type_b)
			return (0);
	}
#endif

	/* Compare ACL perms */
	r = acl_get_permset(ae_a, &permset_a);
	failure("acl_get_permset() error: %s", strerror(errno));
	if (assertEqualInt(r, 0) == 0)
		return (-1);
	r = acl_get_permset(ae_b, &permset_b);
	failure("acl_get_permset() error: %s", strerror(errno));
	if (assertEqualInt(r, 0) == 0)
		return (-1);

	perm_start = 0;
	perm_end = (int)(sizeof(acl_perms) / sizeof(acl_perms[0]));
#if ARCHIVE_ACL_FREEBSD_NFS4
	if (is_nfs4)
		perm_start = 3;
	else
		perm_end = 3;
#endif
	/* Cycle through all perms and compare their value */
	for (i = perm_start; i < perm_end; i++) {
#if ARCHIVE_ACL_LIBACL
		perm_a = acl_get_perm(permset_a, acl_perms[i]);
		perm_b = acl_get_perm(permset_b, acl_perms[i]);
#else
		perm_a = acl_get_perm_np(permset_a, acl_perms[i]);
		perm_b = acl_get_perm_np(permset_b, acl_perms[i]);
#endif
		if (perm_a == -1 || perm_b == -1)
			return (-1);
		if (perm_a != perm_b)
			return (0);
	}

#if ARCHIVE_ACL_FREEBSD_NFS4 || ARCHIVE_ACL_DARWIN
	if (is_nfs4) {
		r = acl_get_flagset_np(ae_a, &flagset_a);
		failure("acl_get_flagset_np() error: %s", strerror(errno));
		if (assertEqualInt(r, 0) == 0)
			return (-1);
		r = acl_get_flagset_np(ae_b, &flagset_b);
		failure("acl_get_flagset_np() error: %s", strerror(errno));
		if (assertEqualInt(r, 0) == 0)
			return (-1);
		/* Cycle through all flags and compare their status */
		for (i = 0; i < (int)(sizeof(acl_flags) / sizeof(acl_flags[0]));
		    i++) {
			flag_a = acl_get_flag_np(flagset_a, acl_flags[i]);
			flag_b = acl_get_flag_np(flagset_b, acl_flags[i]);
			if (flag_a == -1 || flag_b == -1)
				return (-1);
			if (flag_a != flag_b)
				return (0);
		}
	}
#else	/* ARCHIVE_ACL_FREEBSD_NFS4 || ARCHIVE_ACL_DARWIN */
	(void)is_nfs4;	/* UNUSED */
#endif
	return (1);
}
#endif	/* ARCHIVE_ACL_FREEBSD || ARCHIVE_ACL_DARWIN || ARCHIVE_ACL_LIBACL */

#if ARCHIVE_ACL_SUPPORT
/*
 * Clear default ACLs or inheritance flags
 */
static void
clear_inheritance_flags(const char *path, int type)
{
	switch (type) {
	case ARCHIVE_TEST_ACL_TYPE_POSIX1E:
#if ARCHIVE_ACL_POSIX1E
#if !ARCHIVE_ACL_SUNOS
		acl_delete_def_file(path);
#else
		/* Solaris */
		setTestAcl(path);
#endif
#endif	/* ARCHIVE_ACL_POSIX1E */
		break;
	case ARCHIVE_TEST_ACL_TYPE_NFS4:
#if ARCHIVE_ACL_NFS4
		setTestAcl(path);
#endif
		break;
	default:
		(void)path;	/* UNUSED */
		break;
	}
}

static int
compare_acls(const char *path_a, const char *path_b)
{
	int ret = 1;
	int is_nfs4 = 0;
#if ARCHIVE_ACL_SUNOS
	void *acl_a, *acl_b;
	int aclcnt_a, aclcnt_b;
        aclent_t *aclent_a, *aclent_b;
        ace_t *ace_a, *ace_b;
	int e;
#elif ARCHIVE_ACL_DARWIN || ARCHIVE_ACL_FREEBSD || ARCHIVE_ACL_LIBACL
	acl_t acl_a, acl_b;
	acl_entry_t aclent_a, aclent_b;
	int a, b, r;
#endif
#if ARCHIVE_ACL_LIBRICHACL
	struct richacl *richacl_a, *richacl_b;

	richacl_a = NULL;
	richacl_b = NULL;
#endif

#if ARCHIVE_ACL_DARWIN || ARCHIVE_ACL_FREEBSD || ARCHIVE_ACL_LIBACL || \
    ARCHIVE_ACL_SUNOS
	acl_a = NULL;
	acl_b = NULL;
#endif
#if ARCHIVE_ACL_SUNOS
	acl_a = sunacl_get(GETACL, &aclcnt_a, 0, path_a);
	if (acl_a == NULL) {
#if ARCHIVE_ACL_SUNOS_NFS4
		is_nfs4 = 1;
		acl_a = sunacl_get(ACE_GETACL, &aclcnt_a, 0, path_a);
#endif
		failure("acl_get() error: %s", strerror(errno));
		if (assert(acl_a != NULL) == 0)
			return (-1);
#if ARCHIVE_ACL_SUNOS_NFS4
		acl_b = sunacl_get(ACE_GETACL, &aclcnt_b, 0, path_b);
#endif
	} else
		acl_b = sunacl_get(GETACL, &aclcnt_b, 0, path_b);
	if (acl_b == NULL && (errno == ENOSYS || errno == ENOTSUP)) {
		free(acl_a);
		return (0);
	}
	failure("acl_get() error: %s", strerror(errno));
	if (assert(acl_b != NULL) == 0) {
		free(acl_a);
		return (-1);
	}

	if (aclcnt_a != aclcnt_b) {
		ret = 0;
		goto exit_free;
	}

	for (e = 0; e < aclcnt_a; e++) {
		if (!is_nfs4) {
			aclent_a = &((aclent_t *)acl_a)[e];
			aclent_b = &((aclent_t *)acl_b)[e];
			if (aclent_a->a_type != aclent_b->a_type ||
			    aclent_a->a_id != aclent_b->a_id ||
			    aclent_a->a_perm != aclent_b->a_perm) {
				ret = 0;
				goto exit_free;
			}
		}
#if ARCHIVE_ACL_SUNOS_NFS4
		else {
			ace_a = &((ace_t *)acl_a)[e];
			ace_b = &((ace_t *)acl_b)[e];
			if (ace_a->a_who != ace_b->a_who ||
			    ace_a->a_access_mask != ace_b->a_access_mask ||
			    ace_a->a_flags != ace_b->a_flags ||
			    ace_a->a_type != ace_b->a_type) {
				ret = 0;
				goto exit_free;
			}
		}
#endif
	}
#else	/* !ARCHIVE_ACL_SUNOS */
#if ARCHIVE_ACL_LIBRICHACL
	richacl_a = richacl_get_file(path_a);
#if !ARCHIVE_ACL_LIBACL
	if (richacl_a == NULL &&
	    (errno == ENODATA || errno == ENOTSUP || errno == ENOSYS))
		return (0);
	failure("richacl_get_file() error: %s (%s)", path_a, strerror(errno));
	if (assert(richacl_a != NULL) == 0)
		return (-1);
#endif
	if (richacl_a != NULL) {
		richacl_b = richacl_get_file(path_b);
		if (richacl_b == NULL &&
		    (errno == ENODATA || errno == ENOTSUP || errno == ENOSYS)) {
			richacl_free(richacl_a);
			return (0);
		}
		failure("richacl_get_file() error: %s (%s)", path_b,
		    strerror(errno));
		if (assert(richacl_b != NULL) == 0) {
			richacl_free(richacl_a);
			return (-1);
		}
		if (richacl_compare(richacl_a, richacl_b) == 0)
			ret = 0;
		richacl_free(richacl_a);
		richacl_free(richacl_b);
		return (ret);
        }
#endif /* ARCHIVE_ACL_LIBRICHACL */
#if ARCHIVE_ACL_DARWIN || ARCHIVE_ACL_FREEBSD || ARCHIVE_ACL_LIBACL
#if ARCHIVE_ACL_DARWIN
	is_nfs4 = 1;
	acl_a = acl_get_file(path_a, ACL_TYPE_EXTENDED);
#elif ARCHIVE_ACL_FREEBSD_NFS4
	acl_a = acl_get_file(path_a, ACL_TYPE_NFS4);
	if (acl_a != NULL)
		is_nfs4 = 1;
#endif
	if (acl_a == NULL)
		acl_a = acl_get_file(path_a, ACL_TYPE_ACCESS);
	failure("acl_get_file() error: %s (%s)", path_a, strerror(errno));
	if (assert(acl_a != NULL) == 0)
		return (-1);
#if ARCHIVE_ACL_DARWIN
	acl_b = acl_get_file(path_b, ACL_TYPE_EXTENDED);
#elif ARCHIVE_ACL_FREEBSD_NFS4
	acl_b = acl_get_file(path_b, ACL_TYPE_NFS4);
#endif
#if !ARCHIVE_ACL_DARWIN
	if (acl_b == NULL) {
#if ARCHIVE_ACL_FREEBSD_NFS4
		if (is_nfs4) {
			acl_free(acl_a);
			return (0);
		}
#endif
		acl_b = acl_get_file(path_b, ACL_TYPE_ACCESS);
	}
	failure("acl_get_file() error: %s (%s)", path_b, strerror(errno));
	if (assert(acl_b != NULL) == 0) {
		acl_free(acl_a);
		return (-1);
	}
#endif
	a = acl_get_entry(acl_a, ACL_FIRST_ENTRY, &aclent_a);
	if (a == -1) {
		ret = 0;
		goto exit_free;
	}
	b = acl_get_entry(acl_b, ACL_FIRST_ENTRY, &aclent_b);
	if (b == -1) {
		ret = 0;
		goto exit_free;
	}
#if ARCHIVE_ACL_DARWIN
	while (a == 0 && b == 0)
#else	/* FreeBSD, Linux */
	while (a == 1 && b == 1)
#endif
	{
		r = compare_acl_entry(aclent_a, aclent_b, is_nfs4);
		if (r != 1) {
			ret = r;
			goto exit_free;
		}
		a = acl_get_entry(acl_a, ACL_NEXT_ENTRY, &aclent_a);
		b = acl_get_entry(acl_b, ACL_NEXT_ENTRY, &aclent_b);
	}
	/* Entry count must match */
	if (a != b)
		ret = 0;
#endif	/* ARCHIVE_ACL_DARWIN || ARCHIVE_ACL_FREEBSD || ARCHIVE_ACL_LIBACL */
#endif	/* !ARCHIVE_ACL_SUNOS */
exit_free:
#if ARCHIVE_ACL_SUNOS
	free(acl_a);
	free(acl_b);
#else
	acl_free(acl_a);
	acl_free(acl_b);
#endif
	return (ret);
}
#endif	/* ARCHIVE_ACL_SUPPORT */

DEFINE_TEST(test_option_acls)
{
#if !ARCHIVE_ACL_SUPPORT
        skipping("ACLs are not supported on this platform");
#else   /* ARCHIVE_ACL_SUPPORT */
	int acltype, r;

	assertMakeFile("f", 0644, "a");
	acltype = setTestAcl("f");
	if (acltype == 0) {
		skipping("Can't write ACLs on the filesystem");
		return;
	}

	/* Archive it with acls */
        r = systemf("%s -c --no-mac-metadata --acls -f acls.tar f >acls.out 2>acls.err", testprog);
        assertEqualInt(r, 0);

	/* Archive it without acls */
	r = systemf("%s -c --no-mac-metadata --no-acls -f noacls.tar f >noacls.out 2>noacls.err", testprog);
	assertEqualInt(r, 0);

	/* Extract acls with acls */
	assertMakeDir("acls_acls", 0755);
	clear_inheritance_flags("acls_acls", acltype);
	r = systemf("%s -x -C acls_acls --no-same-permissions --acls -f acls.tar >acls_acls.out 2>acls_acls.err", testprog);
	assertEqualInt(r, 0);
	r = compare_acls("f", "acls_acls/f");
	assertEqualInt(r, 1);

	/* Extract acls without acls */
	assertMakeDir("acls_noacls", 0755);
	clear_inheritance_flags("acls_noacls", acltype);
	r = systemf("%s -x -C acls_noacls -p --no-acls -f acls.tar >acls_noacls.out 2>acls_noacls.err", testprog);
	assertEqualInt(r, 0);
	r = compare_acls("f", "acls_noacls/f");
	assertEqualInt(r, 0);

	/* Extract noacls with acls flag */
	assertMakeDir("noacls_acls", 0755);
	clear_inheritance_flags("noacls_acls", acltype);
	r = systemf("%s -x -C noacls_acls --no-same-permissions --acls -f noacls.tar >noacls_acls.out 2>noacls_acls.err", testprog);
	assertEqualInt(r, 0);
	r = compare_acls("f", "noacls_acls/f");
	assertEqualInt(r, 0);

	/* Extract noacls with noacls */
	assertMakeDir("noacls_noacls", 0755);
	clear_inheritance_flags("noacls_noacls", acltype);
	r = systemf("%s -x -C noacls_noacls -p --no-acls -f noacls.tar >noacls_noacls.out 2>noacls_noacls.err", testprog);
	assertEqualInt(r, 0);
	r = compare_acls("f", "noacls_noacls/f");
	assertEqualInt(r, 0);
#endif	/* ARCHIVE_ACL_SUPPORT */
}

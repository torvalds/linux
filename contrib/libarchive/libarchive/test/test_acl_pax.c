/*-
 * Copyright (c) 2003-2007 Tim Kientzle
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
#include "test.h"
__FBSDID("$FreeBSD$");

/*
 * Exercise the system-independent portion of the ACL support.
 * Check that pax archive can save and restore POSIX.1e ACL data.
 *
 * This should work on all systems, regardless of whether local
 * filesystems support ACLs or not.
 */

static unsigned char buff[16384];

static struct archive_test_acl_t acls0[] = {
	{ ARCHIVE_ENTRY_ACL_TYPE_ACCESS, ARCHIVE_ENTRY_ACL_EXECUTE,
	  ARCHIVE_ENTRY_ACL_USER_OBJ, 0, "" },
	{ ARCHIVE_ENTRY_ACL_TYPE_ACCESS, ARCHIVE_ENTRY_ACL_READ,
	  ARCHIVE_ENTRY_ACL_GROUP_OBJ, 0, "" },
	{ ARCHIVE_ENTRY_ACL_TYPE_ACCESS, ARCHIVE_ENTRY_ACL_WRITE,
	  ARCHIVE_ENTRY_ACL_OTHER, 0, "" },
};

static struct archive_test_acl_t acls1[] = {
	{ ARCHIVE_ENTRY_ACL_TYPE_ACCESS, ARCHIVE_ENTRY_ACL_EXECUTE,
	  ARCHIVE_ENTRY_ACL_USER_OBJ, -1, "" },
	{ ARCHIVE_ENTRY_ACL_TYPE_ACCESS, ARCHIVE_ENTRY_ACL_READ,
	  ARCHIVE_ENTRY_ACL_USER, 77, "user77" },
	{ ARCHIVE_ENTRY_ACL_TYPE_ACCESS, ARCHIVE_ENTRY_ACL_READ,
	  ARCHIVE_ENTRY_ACL_GROUP_OBJ, -1, "" },
	{ ARCHIVE_ENTRY_ACL_TYPE_ACCESS, ARCHIVE_ENTRY_ACL_WRITE,
	  ARCHIVE_ENTRY_ACL_OTHER, -1, "" },
};

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
	{ ARCHIVE_ENTRY_ACL_TYPE_ACCESS, ARCHIVE_ENTRY_ACL_WRITE | ARCHIVE_ENTRY_ACL_EXECUTE,
	  ARCHIVE_ENTRY_ACL_OTHER, -1, "" },
};

static struct archive_test_acl_t acls3[] = {
	{ ARCHIVE_ENTRY_ACL_TYPE_ALLOW,
	    ARCHIVE_ENTRY_ACL_READ_DATA |
	    ARCHIVE_ENTRY_ACL_WRITE_DATA |
	    ARCHIVE_ENTRY_ACL_EXECUTE |
	    ARCHIVE_ENTRY_ACL_APPEND_DATA |
	    ARCHIVE_ENTRY_ACL_READ_ATTRIBUTES |
	    ARCHIVE_ENTRY_ACL_WRITE_ATTRIBUTES |
	    ARCHIVE_ENTRY_ACL_READ_NAMED_ATTRS |
	    ARCHIVE_ENTRY_ACL_WRITE_NAMED_ATTRS |
	    ARCHIVE_ENTRY_ACL_READ_ACL |
	    ARCHIVE_ENTRY_ACL_WRITE_ACL |
	    ARCHIVE_ENTRY_ACL_WRITE_OWNER |
	    ARCHIVE_ENTRY_ACL_SYNCHRONIZE,
	  ARCHIVE_ENTRY_ACL_USER_OBJ, 0, "" },
	{ ARCHIVE_ENTRY_ACL_TYPE_ALLOW,
	    ARCHIVE_ENTRY_ACL_READ_DATA |
	    ARCHIVE_ENTRY_ACL_WRITE_DATA |
	    ARCHIVE_ENTRY_ACL_APPEND_DATA |
	    ARCHIVE_ENTRY_ACL_READ_ACL |
	    ARCHIVE_ENTRY_ACL_READ_ATTRIBUTES |
	    ARCHIVE_ENTRY_ACL_READ_NAMED_ATTRS |
	    ARCHIVE_ENTRY_ACL_READ_ACL |
	    ARCHIVE_ENTRY_ACL_SYNCHRONIZE,
	  ARCHIVE_ENTRY_ACL_GROUP_OBJ, 0, "" },
	{ ARCHIVE_ENTRY_ACL_TYPE_ALLOW,
	    ARCHIVE_ENTRY_ACL_READ_DATA |
	    ARCHIVE_ENTRY_ACL_READ_ATTRIBUTES |
	    ARCHIVE_ENTRY_ACL_READ_NAMED_ATTRS |
	    ARCHIVE_ENTRY_ACL_READ_ACL |
	    ARCHIVE_ENTRY_ACL_SYNCHRONIZE,
	  ARCHIVE_ENTRY_ACL_EVERYONE, 0, "" },
};

static struct archive_test_acl_t acls4[] = {
	{ ARCHIVE_ENTRY_ACL_TYPE_ALLOW,
	    ARCHIVE_ENTRY_ACL_READ_DATA |
	    ARCHIVE_ENTRY_ACL_WRITE_DATA |
	    ARCHIVE_ENTRY_ACL_APPEND_DATA |
	    ARCHIVE_ENTRY_ACL_READ_ATTRIBUTES |
	    ARCHIVE_ENTRY_ACL_WRITE_ATTRIBUTES |
	    ARCHIVE_ENTRY_ACL_READ_NAMED_ATTRS |
	    ARCHIVE_ENTRY_ACL_WRITE_NAMED_ATTRS |
	    ARCHIVE_ENTRY_ACL_READ_ACL |
	    ARCHIVE_ENTRY_ACL_WRITE_ACL |
	    ARCHIVE_ENTRY_ACL_WRITE_OWNER |
	    ARCHIVE_ENTRY_ACL_SYNCHRONIZE,
	  ARCHIVE_ENTRY_ACL_USER_OBJ, 0, "" },
	{ ARCHIVE_ENTRY_ACL_TYPE_ALLOW,
	    ARCHIVE_ENTRY_ACL_READ_DATA |
	    ARCHIVE_ENTRY_ACL_READ_ATTRIBUTES |
	    ARCHIVE_ENTRY_ACL_READ_NAMED_ATTRS |
	    ARCHIVE_ENTRY_ACL_READ_ACL |
	    ARCHIVE_ENTRY_ACL_SYNCHRONIZE |
	    ARCHIVE_ENTRY_ACL_ENTRY_INHERITED,
	  ARCHIVE_ENTRY_ACL_USER, 77, "user77" },
	{ ARCHIVE_ENTRY_ACL_TYPE_DENY,
	    ARCHIVE_ENTRY_ACL_READ_DATA |
	    ARCHIVE_ENTRY_ACL_WRITE_DATA |
	    ARCHIVE_ENTRY_ACL_EXECUTE,
	  ARCHIVE_ENTRY_ACL_USER, 78, "user78" },
	{ ARCHIVE_ENTRY_ACL_TYPE_ALLOW,
	    ARCHIVE_ENTRY_ACL_READ_DATA |
	    ARCHIVE_ENTRY_ACL_WRITE_DATA |
	    ARCHIVE_ENTRY_ACL_APPEND_DATA |
	    ARCHIVE_ENTRY_ACL_READ_ATTRIBUTES |
	    ARCHIVE_ENTRY_ACL_READ_NAMED_ATTRS |
	    ARCHIVE_ENTRY_ACL_READ_ACL |
	    ARCHIVE_ENTRY_ACL_SYNCHRONIZE,
	  ARCHIVE_ENTRY_ACL_GROUP_OBJ, 0, "" },
	{ ARCHIVE_ENTRY_ACL_TYPE_DENY,
	    ARCHIVE_ENTRY_ACL_WRITE_DATA |
	    ARCHIVE_ENTRY_ACL_APPEND_DATA |
	    ARCHIVE_ENTRY_ACL_WRITE_ATTRIBUTES |
	    ARCHIVE_ENTRY_ACL_WRITE_NAMED_ATTRS |
	    ARCHIVE_ENTRY_ACL_WRITE_ACL |
	    ARCHIVE_ENTRY_ACL_WRITE_OWNER,
	  ARCHIVE_ENTRY_ACL_GROUP, 78, "group78" },
	{ ARCHIVE_ENTRY_ACL_TYPE_ALLOW,
	    ARCHIVE_ENTRY_ACL_READ_DATA |
	    ARCHIVE_ENTRY_ACL_READ_ATTRIBUTES |
	    ARCHIVE_ENTRY_ACL_READ_NAMED_ATTRS |
	    ARCHIVE_ENTRY_ACL_READ_ACL |
	    ARCHIVE_ENTRY_ACL_SYNCHRONIZE,
	  ARCHIVE_ENTRY_ACL_EVERYONE, 0, "" },
};

static struct archive_test_acl_t acls5[] = {
	{ ARCHIVE_ENTRY_ACL_TYPE_ALLOW,
	    ARCHIVE_ENTRY_ACL_READ_DATA |
	    ARCHIVE_ENTRY_ACL_WRITE_DATA |
	    ARCHIVE_ENTRY_ACL_EXECUTE |
	    ARCHIVE_ENTRY_ACL_APPEND_DATA |
	    ARCHIVE_ENTRY_ACL_READ_ATTRIBUTES |
	    ARCHIVE_ENTRY_ACL_WRITE_ATTRIBUTES |
	    ARCHIVE_ENTRY_ACL_READ_NAMED_ATTRS |
	    ARCHIVE_ENTRY_ACL_WRITE_NAMED_ATTRS |
	    ARCHIVE_ENTRY_ACL_READ_ACL |
	    ARCHIVE_ENTRY_ACL_WRITE_ACL |
	    ARCHIVE_ENTRY_ACL_WRITE_OWNER |
	    ARCHIVE_ENTRY_ACL_SYNCHRONIZE,
	  ARCHIVE_ENTRY_ACL_USER_OBJ, 0, "" },
	{ ARCHIVE_ENTRY_ACL_TYPE_ALLOW,
	    ARCHIVE_ENTRY_ACL_READ_DATA |
	    ARCHIVE_ENTRY_ACL_WRITE_DATA |
	    ARCHIVE_ENTRY_ACL_APPEND_DATA |
	    ARCHIVE_ENTRY_ACL_READ_ATTRIBUTES |
	    ARCHIVE_ENTRY_ACL_READ_NAMED_ATTRS |
	    ARCHIVE_ENTRY_ACL_READ_ACL |
	    ARCHIVE_ENTRY_ACL_WRITE_OWNER |
	    ARCHIVE_ENTRY_ACL_SYNCHRONIZE,
	  ARCHIVE_ENTRY_ACL_USER, 77, "user77" },
	{ ARCHIVE_ENTRY_ACL_TYPE_AUDIT,
	    ARCHIVE_ENTRY_ACL_WRITE_DATA |
	    ARCHIVE_ENTRY_ACL_APPEND_DATA |
	    ARCHIVE_ENTRY_ACL_ENTRY_SUCCESSFUL_ACCESS,
	  ARCHIVE_ENTRY_ACL_USER, 77, "user77" },
	{ ARCHIVE_ENTRY_ACL_TYPE_ALLOW,
	    ARCHIVE_ENTRY_ACL_READ_DATA |
	    ARCHIVE_ENTRY_ACL_WRITE_DATA |
	    ARCHIVE_ENTRY_ACL_APPEND_DATA |
	    ARCHIVE_ENTRY_ACL_READ_ATTRIBUTES |
	    ARCHIVE_ENTRY_ACL_READ_NAMED_ATTRS |
	    ARCHIVE_ENTRY_ACL_READ_ACL |
	    ARCHIVE_ENTRY_ACL_SYNCHRONIZE,
	  ARCHIVE_ENTRY_ACL_GROUP_OBJ, 0, "" },
	{ ARCHIVE_ENTRY_ACL_TYPE_ALARM,
	    ARCHIVE_ENTRY_ACL_READ_DATA |
	    ARCHIVE_ENTRY_ACL_READ_ATTRIBUTES |
	    ARCHIVE_ENTRY_ACL_READ_NAMED_ATTRS |
	    ARCHIVE_ENTRY_ACL_READ_ACL |
	    ARCHIVE_ENTRY_ACL_ENTRY_FAILED_ACCESS,
	  ARCHIVE_ENTRY_ACL_GROUP, 78, "group78" },
	{ ARCHIVE_ENTRY_ACL_TYPE_ALLOW,
	    ARCHIVE_ENTRY_ACL_READ_DATA |
	    ARCHIVE_ENTRY_ACL_READ_ATTRIBUTES |
	    ARCHIVE_ENTRY_ACL_READ_NAMED_ATTRS |
	    ARCHIVE_ENTRY_ACL_READ_ACL |
	    ARCHIVE_ENTRY_ACL_SYNCHRONIZE,
	  ARCHIVE_ENTRY_ACL_EVERYONE, 0, "" },
};

DEFINE_TEST(test_acl_pax_posix1e)
{
	struct archive *a;
	struct archive_entry *ae;
	size_t used;
	FILE *f;
	void *reference;
	size_t reference_size;

	/* Write an archive to memory. */
	assert(NULL != (a = archive_write_new()));
	assertA(0 == archive_write_set_format_pax(a));
	assertA(0 == archive_write_add_filter_none(a));
	assertA(0 == archive_write_set_bytes_per_block(a, 1));
	assertA(0 == archive_write_set_bytes_in_last_block(a, 1));
	assertA(0 == archive_write_open_memory(a, buff, sizeof(buff), &used));

	/* Write a series of files to the archive with different ACL info. */

	/* Create a simple archive_entry. */
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_set_pathname(ae, "file");
        archive_entry_set_mode(ae, S_IFREG | 0777);

	/* Basic owner/owning group should just update mode bits. */
	assertEntrySetAcls(ae, acls0, sizeof(acls0)/sizeof(acls0[0]));
	assertA(0 == archive_write_header(a, ae));

	/* With any extended ACL entry, we should read back a full set. */
	assertEntrySetAcls(ae, acls1, sizeof(acls1)/sizeof(acls1[0]));
	assertA(0 == archive_write_header(a, ae));

	/* A more extensive set of ACLs. */
	assertEntrySetAcls(ae, acls2, sizeof(acls2)/sizeof(acls2[0]));
	assertA(0 == archive_write_header(a, ae));

	/*
	 * Check that clearing ACLs gets rid of them all by repeating
	 * the first test.
	 */
	assertEntrySetAcls(ae, acls0, sizeof(acls0)/sizeof(acls0[0]));
	assertA(0 == archive_write_header(a, ae));
	archive_entry_free(ae);

	/* Close out the archive. */
	assertEqualIntA(a, ARCHIVE_OK, archive_write_close(a));
	assertEqualInt(ARCHIVE_OK, archive_write_free(a));

	/* Write out the data we generated to a file for manual inspection. */
	assert(NULL != (f = fopen("testout", "wb")));
	assertEqualInt(used, (size_t)fwrite(buff, 1, (unsigned int)used, f));
	fclose(f);

	/* Write out the reference data to a file for manual inspection. */
	extract_reference_file("test_acl_pax_posix1e.tar");
	reference = slurpfile(&reference_size, "test_acl_pax_posix1e.tar");

	/* Assert that the generated data matches the built-in reference data.*/
	failure("Generated pax archive does not match reference; compare 'testout' to 'test_acl_pax_posix1e.tar' reference file.");
	assertEqualMem(buff, reference, reference_size);
	failure("Generated pax archive does not match reference; compare 'testout' to 'test_acl_pax_posix1e.tar' reference file.");
	assertEqualInt((int)used, reference_size);
	free(reference);

	/* Read back each entry and check that the ACL data is right. */
	assert(NULL != (a = archive_read_new()));
	assertA(0 == archive_read_support_format_all(a));
	assertA(0 == archive_read_support_filter_all(a));
	assertA(0 == archive_read_open_memory(a, buff, used));

	/* First item has no ACLs */
	assertA(0 == archive_read_next_header(a, &ae));
	failure("Basic ACLs shouldn't be stored as extended ACLs");
	assert(0 == archive_entry_acl_reset(ae, ARCHIVE_ENTRY_ACL_TYPE_ACCESS));
	failure("Basic ACLs should set mode to 0142, not %04o",
	    archive_entry_mode(ae)&0777);
	assert((archive_entry_mode(ae) & 0777) == 0142);

	/* Second item has a few ACLs */
	assertA(0 == archive_read_next_header(a, &ae));
	failure("One extended ACL should flag all ACLs to be returned.");
	assert(4 == archive_entry_acl_reset(ae, ARCHIVE_ENTRY_ACL_TYPE_ACCESS));
	assertEntryCompareAcls(ae, acls1, sizeof(acls1)/sizeof(acls1[0]),
	    ARCHIVE_ENTRY_ACL_TYPE_ACCESS, 0142);
	failure("Basic ACLs should set mode to 0142, not %04o",
	    archive_entry_mode(ae)&0777);
	assert((archive_entry_mode(ae) & 0777) == 0142);

	/* Third item has pretty extensive ACLs */
	assertA(0 == archive_read_next_header(a, &ae));
	assertEqualInt(6, archive_entry_acl_reset(ae,
	    ARCHIVE_ENTRY_ACL_TYPE_ACCESS));
	assertEntryCompareAcls(ae, acls2, sizeof(acls2)/sizeof(acls2[0]),
	    ARCHIVE_ENTRY_ACL_TYPE_ACCESS, 0543);
	failure("Basic ACLs should set mode to 0543, not %04o",
	    archive_entry_mode(ae)&0777);
	assert((archive_entry_mode(ae) & 0777) == 0543);

	/* Fourth item has no ACLs */
	assertA(0 == archive_read_next_header(a, &ae));
	failure("Basic ACLs shouldn't be stored as extended ACLs");
	assert(0 == archive_entry_acl_reset(ae, ARCHIVE_ENTRY_ACL_TYPE_ACCESS));
	failure("Basic ACLs should set mode to 0142, not %04o",
	    archive_entry_mode(ae)&0777);
	assert((archive_entry_mode(ae) & 0777) == 0142);

	/* Close the archive. */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}

DEFINE_TEST(test_acl_pax_nfs4)
{
	struct archive *a;
	struct archive_entry *ae;
	size_t used;
	FILE *f;
	void *reference;
	size_t reference_size;

	/* Write an archive to memory. */
	assert(NULL != (a = archive_write_new()));
	assertA(0 == archive_write_set_format_pax(a));
	assertA(0 == archive_write_add_filter_none(a));
	assertA(0 == archive_write_set_bytes_per_block(a, 1));
	assertA(0 == archive_write_set_bytes_in_last_block(a, 1));
	assertA(0 == archive_write_open_memory(a, buff, sizeof(buff), &used));

	/* Write a series of files to the archive with different ACL info. */

	/* Create a simple archive_entry. */
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_set_pathname(ae, "file");
        archive_entry_set_mode(ae, S_IFREG | 0777);

	/* NFS4 ACLs mirroring 0754 file mode */
	assertEntrySetAcls(ae, acls3, sizeof(acls3)/sizeof(acls3[0]));
	assertA(0 == archive_write_header(a, ae));

	/* A more extensive set of NFS4 ACLs. */
	assertEntrySetAcls(ae, acls4, sizeof(acls4)/sizeof(acls4[0]));
	assertA(0 == archive_write_header(a, ae));

	/* Set with special (audit, alarm) NFS4 ACLs. */
	assertEntrySetAcls(ae, acls5, sizeof(acls5)/sizeof(acls5[0]));
	assertA(0 == archive_write_header(a, ae));

	archive_entry_free(ae);

	/* Close out the archive. */
	assertEqualIntA(a, ARCHIVE_OK, archive_write_close(a));
	assertEqualInt(ARCHIVE_OK, archive_write_free(a));

	/* Write out the data we generated to a file for manual inspection. */
	assert(NULL != (f = fopen("testout", "wb")));
	assertEqualInt(used, (size_t)fwrite(buff, 1, (unsigned int)used, f));
	fclose(f);

	/* Write out the reference data to a file for manual inspection. */
	extract_reference_file("test_acl_pax_nfs4.tar");
	reference = slurpfile(&reference_size, "test_acl_pax_nfs4.tar");

	/* Assert that the generated data matches the built-in reference data.*/
	failure("Generated pax archive does not match reference; compare 'testout' to 'test_acl_pax_nfs4.tar' reference file.");
	assertEqualMem(buff, reference, reference_size);
	failure("Generated pax archive does not match reference; compare 'testout' to 'test_acl_pax_nfs4.tar' reference file.");
	assertEqualInt((int)used, reference_size);
	free(reference);

	/* Read back each entry and check that the ACL data is right. */
	assert(NULL != (a = archive_read_new()));
	assertA(0 == archive_read_support_format_all(a));
	assertA(0 == archive_read_support_filter_all(a));
	assertA(0 == archive_read_open_memory(a, buff, used));

	/* First item has NFS4 ACLs mirroring file mode */
	assertA(0 == archive_read_next_header(a, &ae));
	assertEqualInt(3, archive_entry_acl_reset(ae,
	    ARCHIVE_ENTRY_ACL_TYPE_ALLOW));
	assertEntryCompareAcls(ae, acls3, sizeof(acls3)/sizeof(acls3[0]),
	    ARCHIVE_ENTRY_ACL_TYPE_ALLOW, 0);

	/* Second item has has more fine-grained NFS4 ACLs */
	assertA(0 == archive_read_next_header(a, &ae));
	assertEqualInt(6, archive_entry_acl_reset(ae,
	    ARCHIVE_ENTRY_ACL_TYPE_NFS4));
	assertEntryCompareAcls(ae, acls4, sizeof(acls4)/sizeof(acls4[0]),
	    ARCHIVE_ENTRY_ACL_TYPE_NFS4, 0);

	/* Third item has has audit and alarm NFS4 ACLs */
	assertA(0 == archive_read_next_header(a, &ae));
	assertEqualInt(6, archive_entry_acl_reset(ae,
	    ARCHIVE_ENTRY_ACL_TYPE_NFS4));
	assertEntryCompareAcls(ae, acls5, sizeof(acls5)/sizeof(acls5[0]),
	    ARCHIVE_ENTRY_ACL_TYPE_NFS4, 0);

	/* Close the archive. */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}

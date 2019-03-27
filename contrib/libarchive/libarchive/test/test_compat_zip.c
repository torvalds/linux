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

/* Copy this function for each test file and adjust it accordingly. */
DEFINE_TEST(test_compat_zip_1)
{
	char name[] = "test_compat_zip_1.zip";
	struct archive_entry *ae;
	struct archive *a;
	int r;

	assert((a = archive_read_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_filter_all(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_format_zip(a));
	extract_reference_file(name);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_open_filename(a, name, 10240));

	/* Read first entry. */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualString("META-INF/MANIFEST.MF", archive_entry_pathname(ae));

	/* Read second entry. */
	r = archive_read_next_header(a, &ae);
	if (r == ARCHIVE_FATAL && archive_zlib_version() == NULL) {
		skipping("Skipping ZIP compression check: %s",
			archive_error_string(a));
		goto finish;
	}
	assertEqualIntA(a, ARCHIVE_OK, r);
	assertEqualString("tmp.class", archive_entry_pathname(ae));

	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header(a, &ae));

	assertEqualInt(archive_filter_code(a, 0), ARCHIVE_FILTER_NONE);
	assertEqualInt(archive_format(a), ARCHIVE_FORMAT_ZIP);

finish:
	assertEqualInt(ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}

/*
 * Verify that we skip junk between entries.  The compat_zip_2.zip file
 * has several bytes of junk between 'file1' and 'file2'.  Such
 * junk is routinely introduced by some Zip writers when they manipulate
 * existing zip archives.
 */
DEFINE_TEST(test_compat_zip_2)
{
	char name[] = "test_compat_zip_2.zip";
	struct archive_entry *ae;
	struct archive *a;

	assert((a = archive_read_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_filter_all(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_format_zip(a));
	extract_reference_file(name);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_open_filename(a, name, 10240));

	/* Read first entry. */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualString("file1", archive_entry_pathname(ae));

	/* Read first entry. */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualString("file2", archive_entry_pathname(ae));

	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header(a, &ae));
	assertEqualInt(ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}

/*
 * Issue 185:  Test a regression that got in between 2.6 and 2.7 that
 * broke extraction of Zip entries with length-at-end.
 */
DEFINE_TEST(test_compat_zip_3)
{
	const char *refname = "test_compat_zip_3.zip";
	struct archive_entry *ae;
	struct archive *a;

	extract_reference_file(refname);
	assert((a = archive_read_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_filter_all(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_format_all(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_open_filename(a, refname, 10240));

	/* First entry. */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualString("soapui-4.0.0/", archive_entry_pathname(ae));
	assertEqualInt(0, archive_entry_size(ae));
	assert(archive_entry_size_is_set(ae));
	assertEqualInt(AE_IFDIR, archive_entry_filetype(ae));

	/* Second entry. */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualString("soapui-4.0.0/soapui-settings.xml", archive_entry_pathname(ae));
	assertEqualInt(AE_IFREG, archive_entry_filetype(ae));
	assertEqualInt(1030, archive_entry_size(ae));
	assert(archive_entry_size_is_set(ae));

	/* Extract under a different name. */
	archive_entry_set_pathname(ae, "test_3.txt");
	if(archive_zlib_version() != NULL) {
		char *p;
		size_t s;
		assertEqualIntA(a, ARCHIVE_OK, archive_read_extract(a, ae, 0));
		/* Verify the first 12 bytes actually got written to disk correctly. */
		p = slurpfile(&s, "test_3.txt");
		assertEqualInt(s, 1030);
		assertEqualMem(p, "<?xml versio", 12);
		free(p);
	} else {
		skipping("Skipping ZIP compression check, no libz support");
	}
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header(a, &ae));

	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_free(a));
}

/**
 * A file with leading garbage (similar to an SFX file).
 */
DEFINE_TEST(test_compat_zip_4)
{
	const char *refname = "test_compat_zip_4.zip";
	struct archive_entry *ae;
	struct archive *a;
	void *p;
	size_t s;

	extract_reference_file(refname);
	p = slurpfile(&s, refname);

	/* SFX files require seek support. */
	assert((a = archive_read_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_filter_all(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_format_all(a));
	assertEqualIntA(a, ARCHIVE_OK, read_open_memory_seek(a, p, s, 18));

	/* First entry. */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualString("foo", archive_entry_pathname(ae));
	assertEqualInt(4, archive_entry_size(ae));
	assert(archive_entry_size_is_set(ae));
	assertEqualInt(AE_IFREG, archive_entry_filetype(ae));
	assertEqualInt(0412, archive_entry_perm(ae));

	/* Second entry. */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualString("bar", archive_entry_pathname(ae));
	assertEqualInt(AE_IFREG, archive_entry_filetype(ae));
	assertEqualInt(4, archive_entry_size(ae));
	assert(archive_entry_size_is_set(ae));
	assertEqualInt(0567, archive_entry_perm(ae));

	/* Third entry. */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualString("baz", archive_entry_pathname(ae));
	assertEqualInt(AE_IFREG, archive_entry_filetype(ae));
	assertEqualInt(4, archive_entry_size(ae));
	assert(archive_entry_size_is_set(ae));
	assertEqualInt(0644, archive_entry_perm(ae));

	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_free(a));

	/* Try reading without seek support and watch it fail. */
	assert((a = archive_read_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_filter_all(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_format_all(a));
	assertEqualIntA(a, ARCHIVE_FATAL, read_open_memory(a, p, s, 3));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_free(a));
	free(p);
}
/**
 * Issue 152: A file generated by a tool that doesn't really
 * believe in populating local file headers at all.  This
 * is only readable with the seeking reader.
 */
DEFINE_TEST(test_compat_zip_5)
{
	const char *refname = "test_compat_zip_5.zip";
	struct archive_entry *ae;
	struct archive *a;
	void *p;
	size_t s;

	extract_reference_file(refname);
	p = slurpfile(&s, refname);

	/* Verify with seek support.
	 * Everything works correctly here. */
	assert((a = archive_read_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_filter_all(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_format_all(a));
	assertEqualIntA(a, ARCHIVE_OK, read_open_memory_seek(a, p, s, 18));

	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualString("Metadata/Job_PT.xml", archive_entry_pathname(ae));
	assertEqualInt(3559, archive_entry_size(ae));
	assertEqualInt(AE_IFREG, archive_entry_filetype(ae));
	assertEqualInt(0664, archive_entry_perm(ae));

	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualString("Metadata/MXDC_Empty_PT.xml", archive_entry_pathname(ae));
	assertEqualInt(456, archive_entry_size(ae));
	assertEqualInt(AE_IFREG, archive_entry_filetype(ae));
	assertEqualInt(0664, archive_entry_perm(ae));

	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualString("Documents/1/Metadata/Page1_Thumbnail.JPG", archive_entry_pathname(ae));
	assertEqualInt(1495, archive_entry_size(ae));
	assertEqualInt(AE_IFREG, archive_entry_filetype(ae));
	assertEqualInt(0664, archive_entry_perm(ae));
	/* TODO: Read some of the file data and verify it.
	   The code to read uncompressed Zip entries with "file at end" semantics
	   is tricky and should be verified more carefully. */

	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualString("Documents/1/Pages/_rels/1.fpage.rels", archive_entry_pathname(ae));

	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualString("Documents/1/Pages/1.fpage", archive_entry_pathname(ae));

	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualString("Documents/1/Resources/Fonts/3DFDBC8B-4514-41F1-A808-DEA1C79BAC2B.odttf", archive_entry_pathname(ae));

	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualString("Documents/1/_rels/FixedDocument.fdoc.rels", archive_entry_pathname(ae));

	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualString("Documents/1/FixedDocument.fdoc", archive_entry_pathname(ae));

	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualString("_rels/FixedDocumentSequence.fdseq.rels", archive_entry_pathname(ae));

	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualString("FixedDocumentSequence.fdseq", archive_entry_pathname(ae));

	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualString("_rels/.rels", archive_entry_pathname(ae));

	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualString("[Content_Types].xml", archive_entry_pathname(ae));

	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header(a, &ae));

	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_free(a));

	/* Try reading without seek support. */
	assert((a = archive_read_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_filter_all(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_format_all(a));
	assertEqualIntA(a, ARCHIVE_OK, read_open_memory(a, p, s, 3));

	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualString("Metadata/Job_PT.xml", archive_entry_pathname(ae));
	assertEqualInt(0, archive_entry_size(ae));
	assert(!archive_entry_size_is_set(ae));
	assertEqualInt(AE_IFREG, archive_entry_filetype(ae));
	assertEqualInt(0664, archive_entry_perm(ae));

	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualString("Metadata/MXDC_Empty_PT.xml", archive_entry_pathname(ae));
	assertEqualInt(0, archive_entry_size(ae));
	assert(!archive_entry_size_is_set(ae));
	assertEqualInt(AE_IFREG, archive_entry_filetype(ae));
	assertEqualInt(0664, archive_entry_perm(ae));

	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualString("Documents/1/Metadata/Page1_Thumbnail.JPG", archive_entry_pathname(ae));
	assertEqualInt(0, archive_entry_size(ae));
	assert(!archive_entry_size_is_set(ae));
	assertEqualInt(AE_IFREG, archive_entry_filetype(ae));
	assertEqualInt(0664, archive_entry_perm(ae));

	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualString("Documents/1/Pages/_rels/1.fpage.rels", archive_entry_pathname(ae));

	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualString("Documents/1/Pages/1.fpage", archive_entry_pathname(ae));

	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualString("Documents/1/Resources/Fonts/3DFDBC8B-4514-41F1-A808-DEA1C79BAC2B.odttf", archive_entry_pathname(ae));

	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualString("Documents/1/_rels/FixedDocument.fdoc.rels", archive_entry_pathname(ae));

	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualString("Documents/1/FixedDocument.fdoc", archive_entry_pathname(ae));

	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualString("_rels/FixedDocumentSequence.fdseq.rels", archive_entry_pathname(ae));

	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualString("FixedDocumentSequence.fdseq", archive_entry_pathname(ae));

	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualString("_rels/.rels", archive_entry_pathname(ae));

	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualString("[Content_Types].xml", archive_entry_pathname(ae));

	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header(a, &ae));

	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_free(a));
	free(p);
}

/*
 * Issue 225: Errors extracting MSDOS Zip archives with directories.
 */
static void
compat_zip_6_verify(struct archive *a)
{
	struct archive_entry *ae;

	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualString("New Folder/New Folder/", archive_entry_pathname(ae));
	assertEqualInt(AE_IFDIR, archive_entry_filetype(ae));
	/* Zip timestamps are local time, so vary by time zone. */
	/* TODO: A more complex assert would work here; we could
	   verify that it's within +/- 24 hours of a particular value. */
	/* assertEqualInt(1327314468, archive_entry_mtime(ae)); */
	assertEqualInt(0, archive_entry_size(ae));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualString("New Folder/New Folder/New Text Document.txt", archive_entry_pathname(ae));
	assertEqualInt(AE_IFREG, archive_entry_filetype(ae));
	/* Zip timestamps are local time, so vary by time zone. */
	/* assertEqualInt(1327314476, archive_entry_mtime(ae)); */
	assertEqualInt(11, archive_entry_size(ae));
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header(a, &ae));
}

DEFINE_TEST(test_compat_zip_6)
{
	const char *refname = "test_compat_zip_6.zip";
	struct archive *a;
	void *p;
	size_t s;

	extract_reference_file(refname);
	p = slurpfile(&s, refname);

	assert((a = archive_read_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_filter_all(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_format_all(a));
	assertEqualIntA(a, ARCHIVE_OK, read_open_memory_seek(a, p, s, 7));
	compat_zip_6_verify(a);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_free(a));

	assert((a = archive_read_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_filter_all(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_format_all(a));
	assertEqualIntA(a, ARCHIVE_OK, read_open_memory(a, p, s, 7));
	compat_zip_6_verify(a);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_free(a));
	free(p);
}

/*
 * Issue 226: Try to reproduce hang when reading archives where the
 * length-at-end marker ends exactly on a block boundary.
 */
DEFINE_TEST(test_compat_zip_7)
{
	const char *refname = "test_compat_zip_7.xps";
	struct archive *a;
	struct archive_entry *ae;
	void *p;
	size_t s;
	int i;

	extract_reference_file(refname);
	p = slurpfile(&s, refname);

	for (i = 1; i < 1000; ++i) {
		assert((a = archive_read_new()) != NULL);
		assertEqualIntA(a, ARCHIVE_OK, archive_read_support_format_zip(a));
		assertEqualIntA(a, ARCHIVE_OK, read_open_memory_minimal(a, p, s, i));

		assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
		assertEqualIntA(a, ARCHIVE_OK, archive_read_data_skip(a));
		assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
		assertEqualIntA(a, ARCHIVE_OK, archive_read_data_skip(a));
		assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
		assertEqualIntA(a, ARCHIVE_OK, archive_read_data_skip(a));
		assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
		assertEqualIntA(a, ARCHIVE_OK, archive_read_data_skip(a));

		assertEqualIntA(a, ARCHIVE_OK, archive_read_free(a));
	}
	free(p);
}

/**
 * A file with backslash path separators instead of slashes.
 * PowerShell's Compress-Archive cmdlet produces such archives.
 */
DEFINE_TEST(test_compat_zip_8)
{
	const char *refname = "test_compat_zip_8.zip";
	struct archive *a;
	struct archive_entry *ae;
	void *p;
	size_t s;

	extract_reference_file(refname);
	p = slurpfile(&s, refname);

	assert((a = archive_read_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_format_zip(a));
	assertEqualIntA(a, ARCHIVE_OK, read_open_memory_minimal(a, p, s, 7));

	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	/* This file is in the archive as arc\test */
	assertEqualString("arc/test", archive_entry_pathname(ae));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_free(a));
	free(p);
}

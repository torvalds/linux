/*-
 * Copyright (c) 2003-2007,2013 Tim Kientzle
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

#include <errno.h>
#include <stdlib.h>
#include <string.h>

/*
 * This is a somewhat tricky test that verifies the ability to
 * write and read very large entries to zip archives.
 *
 * See test_tar_large.c for more information about the machinery
 * being used here.
 */

static size_t nullsize;
static void *nulldata;

struct fileblock {
	struct fileblock *next;
	int	size;
	void *buff;
	int64_t gap_size; /* Size of following gap */
};

struct fileblocks {
	int64_t filesize;
	int64_t fileposition;
	int64_t gap_remaining;
	void *buff;
	struct fileblock *first;
	struct fileblock *current;
	struct fileblock *last;
};

/* The following size definitions simplify things below. */
#define KB ((int64_t)1024)
#define MB ((int64_t)1024 * KB)
#define GB ((int64_t)1024 * MB)
#define TB ((int64_t)1024 * GB)

static int64_t	memory_read_skip(struct archive *, void *, int64_t request);
static ssize_t	memory_read(struct archive *, void *, const void **buff);
static ssize_t	memory_write(struct archive *, void *, const void *, size_t);

static uint16_t le16(const void *_p) {
	const uint8_t *p = _p;
	return p[0] | (p[1] << 8);
}

static uint32_t le32(const void *_p) {
	const uint8_t *p = _p;
	return le16(p) | ((uint32_t)le16(p + 2) << 16);
}

static uint64_t le64(const void *_p) {
	const uint8_t *p = _p;
	return le32(p) | ((uint64_t)le32(p + 4) << 32);
}

static ssize_t
memory_write(struct archive *a, void *_private, const void *buff, size_t size)
{
	struct fileblocks *private = _private;
	struct fileblock *block;

	(void)a;

	if ((const char *)nulldata <= (const char *)buff
	    && (const char *)buff < (const char *)nulldata + nullsize) {
		/* We don't need to store a block of gap data. */
		private->last->gap_size += (int64_t)size;
	} else {
		/* Yes, we're assuming the very first write is metadata. */
		/* It's header or metadata, copy and save it. */
		block = (struct fileblock *)malloc(sizeof(*block));
		memset(block, 0, sizeof(*block));
		block->size = (int)size;
		block->buff = malloc(size);
		memcpy(block->buff, buff, size);
		if (private->last == NULL) {
			private->first = private->last = block;
		} else {
			private->last->next = block;
			private->last = block;
		}
		block->next = NULL;
	}
	private->filesize += size;
	return ((long)size);
}

static ssize_t
memory_read(struct archive *a, void *_private, const void **buff)
{
	struct fileblocks *private = _private;
	ssize_t size;

	(void)a;

	while (private->current != NULL && private->buff == NULL && private->gap_remaining == 0) {
		private->current = private->current->next;
		if (private->current != NULL) {
			private->buff = private->current->buff;
			private->gap_remaining = private->current->gap_size;
		}
	}

	if (private->current == NULL)
		return (0);

	/* If there's real data, return that. */
	if (private->buff != NULL) {
		*buff = private->buff;
		size = ((char *)private->current->buff + private->current->size)
		    - (char *)private->buff;
		private->buff = NULL;
		private->fileposition += size;
		return (size);
	}

	/* Big gap: too big to return all at once, so just return some. */
	if (private->gap_remaining > (int64_t)nullsize) {
		private->gap_remaining -= nullsize;
		*buff = nulldata;
		private->fileposition += nullsize;
		return (nullsize);
	}

	/* Small gap: finish the gap and prep for next block. */
	if (private->gap_remaining > 0) {
		size = (ssize_t)private->gap_remaining;
		*buff = nulldata;
		private->gap_remaining = 0;
		private->fileposition += size;

		private->current = private->current->next;
		if (private->current != NULL) {
			private->buff = private->current->buff;
			private->gap_remaining = private->current->gap_size;
		}

		return (size);
	}
	fprintf(stderr, "\n\n\nInternal failure\n\n\n");
	exit(1);
}

static int
memory_read_open(struct archive *a, void *_private)
{
	struct fileblocks *private = _private;

	(void)a; /* UNUSED */

	private->current = private->first;
	private->fileposition = 0;
	if (private->current != NULL) {
		private->buff = private->current->buff;
		private->gap_remaining = private->current->gap_size;
	}
	return (ARCHIVE_OK);
}

static int64_t
memory_read_seek(struct archive *a, void *_private, int64_t offset, int whence)
{
	struct fileblocks *private = _private;

	(void)a;
	if (whence == SEEK_END) {
		offset = private->filesize + offset;
	} else if (whence == SEEK_CUR) {
		offset = private->fileposition + offset;
	}

	if (offset < 0) {
		fprintf(stderr, "\n\n\nInternal failure: negative seek\n\n\n");
		exit(1);
	}

	/* We've converted the request into a SEEK_SET. */
	private->fileposition = offset;

	/* Walk the block list to find the new position. */
	offset = 0;
	private->current = private->first;
	while (private->current != NULL) {
		if (offset + private->current->size > private->fileposition) {
			/* Position is in this block. */
			private->buff = (char *)private->current->buff
			    + private->fileposition - offset;
			private->gap_remaining = private->current->gap_size;
			return private->fileposition;
		}
		offset += private->current->size;
		if (offset + private->current->gap_size > private->fileposition) {
			/* Position is in this gap. */
			private->buff = NULL;
			private->gap_remaining = private->current->gap_size
			    - (private->fileposition - offset);
			return private->fileposition;
		}
		offset += private->current->gap_size;
		/* Skip to next block. */
		private->current = private->current->next;
	}
	if (private->fileposition == private->filesize) {
		return private->fileposition;
	}
	fprintf(stderr, "\n\n\nInternal failure: over-sized seek\n\n\n");
	exit(1);
}

static int64_t
memory_read_skip(struct archive *a, void *_private, int64_t skip)
{
	struct fileblocks *private = _private;
	int64_t old_position = private->fileposition;
	int64_t new_position = memory_read_seek(a, _private, skip, SEEK_CUR);
	return (new_position - old_position);
}

static struct fileblocks *
fileblocks_new(void)
{
	struct fileblocks *fileblocks;

	fileblocks = calloc(1, sizeof(struct fileblocks));
	return fileblocks;
}

static void
fileblocks_free(struct fileblocks *fileblocks)
{
	while (fileblocks->first != NULL) {
		struct fileblock *b = fileblocks->first;
		fileblocks->first = fileblocks->first->next;
		free(b->buff);
		free(b);
	}
	free(fileblocks);
}


/* The sizes of the entries we're going to generate. */
static int64_t test_sizes[] = {
	/* Test for 32-bit signed overflow. */
	2 * GB - 1, 2 * GB, 2 * GB + 1,
	/* Test for 32-bit unsigned overflow. */
	4 * GB - 1, 4 * GB, 4 * GB + 1,
	/* And beyond ... because we can. */
	16 * GB - 1, 16 * GB, 16 * GB + 1,
	64 * GB - 1, 64 * GB, 64 * GB + 1,
	256 * GB - 1, 256 * GB, 256 * GB + 1,
	1 * TB,
	0
};


static void
verify_large_zip(struct archive *a, struct fileblocks *fileblocks)
{
	char namebuff[64];
	struct archive_entry *ae;
	int i;

	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_set_options(a, "zip:ignorecrc32"));
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_set_open_callback(a, memory_read_open));
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_set_read_callback(a, memory_read));
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_set_skip_callback(a, memory_read_skip));
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_set_seek_callback(a, memory_read_seek));
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_set_callback_data(a, fileblocks));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_open1(a));

	/*
	 * Read entries back.
	 */
	for (i = 0; test_sizes[i] > 0; i++) {
		assertEqualIntA(a, ARCHIVE_OK,
		    archive_read_next_header(a, &ae));
		sprintf(namebuff, "file_%d", i);
		assertEqualString(namebuff, archive_entry_pathname(ae));
		assertEqualInt(test_sizes[i], archive_entry_size(ae));
	}
	assertEqualIntA(a, 0, archive_read_next_header(a, &ae));
	assertEqualString("lastfile", archive_entry_pathname(ae));

	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header(a, &ae));

	/* Close out the archive. */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
}

DEFINE_TEST(test_write_format_zip_large)
{
	int i;
	char namebuff[64];
	struct fileblocks *fileblocks = fileblocks_new();
	struct archive_entry *ae;
	struct archive *a;
	const char *p;
	const char *cd_start, *zip64_eocd, *zip64_locator, *eocd;
	int64_t cd_size;
	char *buff;
	int64_t  filesize;
	size_t writesize, buffsize, s;

	nullsize = (size_t)(1 * MB);
	nulldata = malloc(nullsize);
	memset(nulldata, 0xAA, nullsize);

	/*
	 * Open an archive for writing.
	 */
	a = archive_write_new();
	archive_write_set_format_zip(a);
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_write_set_options(a, "zip:compression=store"));
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_write_set_options(a, "zip:fakecrc32"));
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_write_set_bytes_per_block(a, 0)); /* No buffering. */
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_write_open(a, fileblocks, NULL, memory_write, NULL));

	/*
	 * Write a series of large files to it.
	 */
	for (i = 0; test_sizes[i] != 0; i++) {
		assert((ae = archive_entry_new()) != NULL);
		sprintf(namebuff, "file_%d", i);
		archive_entry_copy_pathname(ae, namebuff);
		archive_entry_set_mode(ae, S_IFREG | 0755);
		filesize = test_sizes[i];
		archive_entry_set_size(ae, filesize);

		assertEqualIntA(a, ARCHIVE_OK,
		    archive_write_header(a, ae));
		archive_entry_free(ae);

		/*
		 * Write the actual data to the archive.
		 */
		while (filesize > 0) {
			writesize = nullsize;
			if ((int64_t)writesize > filesize)
				writesize = (size_t)filesize;
			assertEqualIntA(a, (int)writesize,
			    (int)archive_write_data(a, nulldata, writesize));
			filesize -= writesize;
		}
	}

	assert((ae = archive_entry_new()) != NULL);
	archive_entry_copy_pathname(ae, "lastfile");
	archive_entry_set_mode(ae, S_IFREG | 0755);
	assertA(0 == archive_write_header(a, ae));
	archive_entry_free(ae);

	/* Close out the archive. */
	assertEqualIntA(a, ARCHIVE_OK, archive_write_close(a));
	assertEqualInt(ARCHIVE_OK, archive_write_free(a));

	/*
	 * Read back with seeking reader:
	 */
	a = archive_read_new();
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_support_format_zip_seekable(a));
	verify_large_zip(a, fileblocks);
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));

	/*
	 * Read back with streaming reader:
	 */
	a = archive_read_new();
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_support_format_zip_streamable(a));
	verify_large_zip(a, fileblocks);
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));

	/*
	 * Manually verify some of the final bytes of the archives.
	 */
	/* Collect the final bytes together */
#define FINAL_SIZE 8192
	buff = malloc(FINAL_SIZE);
	buffsize = 0;
	memory_read_open(NULL, fileblocks);
	memory_read_seek(NULL, fileblocks, -FINAL_SIZE, SEEK_END);
	while ((s = memory_read(NULL, fileblocks, (const void **)&p)) > 0) {
		memcpy(buff + buffsize, p, s);
		buffsize += s;
	}
	assertEqualInt(buffsize, FINAL_SIZE);

	p = buff + buffsize;

	/* Verify regular end-of-central-directory record */
	eocd = p - 22;
	assertEqualMem(eocd, "PK\005\006\0\0\0\0", 8);
	assertEqualMem(eocd + 8, "\021\0\021\0", 4); /* 17 entries total */
	cd_size = le32(eocd + 12);
	/* Start of CD offset should be 0xffffffff */
	assertEqualMem(eocd + 16, "\xff\xff\xff\xff", 4);
	assertEqualMem(eocd + 20, "\0\0", 2);	/* No Zip comment */

	/* Verify Zip64 locator */
	zip64_locator = p - 42;
	assertEqualMem(zip64_locator, "PK\006\007\0\0\0\0", 8);
	zip64_eocd = p - (fileblocks->filesize - le64(zip64_locator + 8));
	assertEqualMem(zip64_locator + 16, "\001\0\0\0", 4);

	/* Verify Zip64 end-of-cd record. */
	assert(zip64_eocd == p - 98);
	assertEqualMem(zip64_eocd, "PK\006\006", 4);
	assertEqualInt(44, le64(zip64_eocd + 4)); // Size of EoCD record - 12
	assertEqualMem(zip64_eocd + 12, "\055\0", 2);  // Made by version: 45
	assertEqualMem(zip64_eocd + 14, "\055\0", 2);  // Requires version: 45
	assertEqualMem(zip64_eocd + 16, "\0\0\0\0", 4); // This disk
	assertEqualMem(zip64_eocd + 20, "\0\0\0\0", 4); // Total disks
	assertEqualInt(17, le64(zip64_eocd + 24));  // Entries on this disk
	assertEqualInt(17, le64(zip64_eocd + 32));  // Total entries
	cd_size = le64(zip64_eocd + 40);
	cd_start = p - (fileblocks->filesize - le64(zip64_eocd + 48));

	assert(cd_start + cd_size == zip64_eocd);

	assertEqualInt(le64(zip64_eocd + 48) // Start of CD
	    + cd_size
	    + 56 // Size of Zip64 EOCD
	    + 20 // Size of Zip64 locator
	    + 22, // Size of EOCD
	    fileblocks->filesize);

	// TODO: Scan entire Central Directory, sanity-check all data
	assertEqualMem(cd_start, "PK\001\002", 4);

	fileblocks_free(fileblocks);
	free(buff);
	free(nulldata);
}

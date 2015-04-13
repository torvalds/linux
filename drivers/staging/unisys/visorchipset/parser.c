/* parser.c
 *
 * Copyright (C) 2010 - 2013 UNISYS CORPORATION
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 */

#include "parser.h"
#include "memregion.h"
#include "controlvmchannel.h"
#include <linux/ctype.h>
#include <linux/mm.h>
#include <linux/uuid.h>

#define MYDRVNAME "visorchipset_parser"
#define CURRENT_FILE_PC VISOR_CHIPSET_PC_parser_c

/* We will refuse to allocate more than this many bytes to copy data from
 * incoming payloads.  This serves as a throttling mechanism.
 */
#define MAX_CONTROLVM_PAYLOAD_BYTES (1024*128)
static unsigned long controlvm_payload_bytes_buffered;

struct parser_context {
	unsigned long allocbytes;
	unsigned long param_bytes;
	u8 *curr;
	unsigned long bytes_remaining;
	bool byte_stream;
	char data[0];
};

static struct parser_context *
parser_init_guts(u64 addr, u32 bytes, bool local,
		 bool standard_payload_header, bool *retry)
{
	int allocbytes = sizeof(struct parser_context) + bytes;
	struct parser_context *rc = NULL;
	struct parser_context *ctx = NULL;
	struct memregion *rgn = NULL;
	struct spar_controlvm_parameters_header *phdr = NULL;

	if (retry)
		*retry = false;
	if (!standard_payload_header)
		/* alloc and 0 extra byte to ensure payload is
		 * '\0'-terminated
		 */
		allocbytes++;
	if ((controlvm_payload_bytes_buffered + bytes)
	    > MAX_CONTROLVM_PAYLOAD_BYTES) {
		if (retry)
			*retry = true;
		rc = NULL;
		goto cleanup;
	}
	ctx = kzalloc(allocbytes, GFP_KERNEL|__GFP_NORETRY);
	if (!ctx) {
		if (retry)
			*retry = true;
		rc = NULL;
		goto cleanup;
	}

	ctx->allocbytes = allocbytes;
	ctx->param_bytes = bytes;
	ctx->curr = NULL;
	ctx->bytes_remaining = 0;
	ctx->byte_stream = false;
	if (local) {
		void *p;

		if (addr > virt_to_phys(high_memory - 1)) {
			rc = NULL;
			goto cleanup;
		}
		p = __va((unsigned long) (addr));
		memcpy(ctx->data, p, bytes);
	} else {
		rgn = visor_memregion_create(addr, bytes);
		if (!rgn) {
			rc = NULL;
			goto cleanup;
		}
		if (visor_memregion_read(rgn, 0, ctx->data, bytes) < 0) {
			rc = NULL;
			goto cleanup;
		}
	}
	if (!standard_payload_header) {
		ctx->byte_stream = true;
		rc = ctx;
		goto cleanup;
	}
	phdr = (struct spar_controlvm_parameters_header *)(ctx->data);
	if (phdr->total_length != bytes) {
		rc = NULL;
		goto cleanup;
	}
	if (phdr->total_length < phdr->header_length) {
		rc = NULL;
		goto cleanup;
	}
	if (phdr->header_length <
	    sizeof(struct spar_controlvm_parameters_header)) {
		rc = NULL;
		goto cleanup;
	}

	rc = ctx;
cleanup:
	if (rgn) {
		visor_memregion_destroy(rgn);
		rgn = NULL;
	}
	if (rc) {
		controlvm_payload_bytes_buffered += ctx->param_bytes;
	} else {
		if (ctx) {
			parser_done(ctx);
			ctx = NULL;
		}
	}
	return rc;
}

struct parser_context *
parser_init(u64 addr, u32 bytes, bool local, bool *retry)
{
	return parser_init_guts(addr, bytes, local, true, retry);
}

/* Call this instead of parser_init() if the payload area consists of just
 * a sequence of bytes, rather than a struct spar_controlvm_parameters_header
 * structures.  Afterwards, you can call parser_simpleString_get() or
 * parser_byteStream_get() to obtain the data.
 */
struct parser_context *
parser_init_byte_stream(u64 addr, u32 bytes, bool local, bool *retry)
{
	return parser_init_guts(addr, bytes, local, false, retry);
}

/* Obtain '\0'-terminated copy of string in payload area.
 */
char *
parser_simpleString_get(struct parser_context *ctx)
{
	if (!ctx->byte_stream)
		return NULL;
	return ctx->data;	/* note this IS '\0'-terminated, because of
				 * the num of bytes we alloc+clear in
				 * parser_init_byteStream() */
}

/* Obtain a copy of the buffer in the payload area.
 */
void *parser_byte_stream_get(struct parser_context *ctx, unsigned long *nbytes)
{
	if (!ctx->byte_stream)
		return NULL;
	if (nbytes)
		*nbytes = ctx->param_bytes;
	return (void *)ctx->data;
}

uuid_le
parser_id_get(struct parser_context *ctx)
{
	struct spar_controlvm_parameters_header *phdr = NULL;

	if (ctx == NULL)
		return NULL_UUID_LE;
	phdr = (struct spar_controlvm_parameters_header *)(ctx->data);
	return phdr->id;
}

void
parser_param_start(struct parser_context *ctx, PARSER_WHICH_STRING which_string)
{
	struct spar_controlvm_parameters_header *phdr = NULL;

	if (ctx == NULL)
		goto Away;
	phdr = (struct spar_controlvm_parameters_header *)(ctx->data);
	switch (which_string) {
	case PARSERSTRING_INITIATOR:
		ctx->curr = ctx->data + phdr->initiator_offset;
		ctx->bytes_remaining = phdr->initiator_length;
		break;
	case PARSERSTRING_TARGET:
		ctx->curr = ctx->data + phdr->target_offset;
		ctx->bytes_remaining = phdr->target_length;
		break;
	case PARSERSTRING_CONNECTION:
		ctx->curr = ctx->data + phdr->connection_offset;
		ctx->bytes_remaining = phdr->connection_length;
		break;
	case PARSERSTRING_NAME:
		ctx->curr = ctx->data + phdr->name_offset;
		ctx->bytes_remaining = phdr->name_length;
		break;
	default:
		break;
	}

Away:
	return;
}

void
parser_done(struct parser_context *ctx)
{
	if (!ctx)
		return;
	controlvm_payload_bytes_buffered -= ctx->param_bytes;
	kfree(ctx);
}

/** Return length of string not counting trailing spaces. */
static int
string_length_no_trail(char *s, int len)
{
	int i = len - 1;

	while (i >= 0) {
		if (!isspace(s[i]))
			return i + 1;
		i--;
	}
	return 0;
}

/** Grab the next name and value out of the parameter buffer.
 *  The entire parameter buffer looks like this:
 *      <name>=<value>\0
 *      <name>=<value>\0
 *      ...
 *      \0
 *  If successful, the next <name> value is returned within the supplied
 *  <nam> buffer (the value is always upper-cased), and the corresponding
 *  <value> is returned within a kmalloc()ed buffer, whose pointer is
 *  provided as the return value of this function.
 *  (The total number of bytes allocated is strlen(<value>)+1.)
 *
 *  NULL is returned to indicate failure, which can occur for several reasons:
 *  - all <name>=<value> pairs have already been processed
 *  - bad parameter
 *  - parameter buffer ends prematurely (couldn't find an '=' or '\0' within
 *    the confines of the parameter buffer)
 *  - the <nam> buffer is not large enough to hold the <name> of the next
 *    parameter
 */
void *
parser_param_get(struct parser_context *ctx, char *nam, int namesize)
{
	u8 *pscan, *pnam = nam;
	unsigned long nscan;
	int value_length = -1, orig_value_length = -1;
	void *value = NULL;
	int i;
	int closing_quote = 0;

	if (!ctx)
		return NULL;
	pscan = ctx->curr;
	nscan = ctx->bytes_remaining;
	if (nscan == 0)
		return NULL;
	if (*pscan == '\0')
		/*  This is the normal return point after you have processed
		 *  all of the <name>=<value> pairs in a syntactically-valid
		 *  parameter buffer.
		 */
		return NULL;

	/* skip whitespace */
	while (isspace(*pscan)) {
		pscan++;
		nscan--;
		if (nscan == 0)
			return NULL;
	}

	while (*pscan != ':') {
		if (namesize <= 0)
			return NULL;
		*pnam = toupper(*pscan);
		pnam++;
		namesize--;
		pscan++;
		nscan--;
		if (nscan == 0)
			return NULL;
	}
	if (namesize <= 0)
		return NULL;
	*pnam = '\0';
	nam[string_length_no_trail(nam, strlen(nam))] = '\0';

	/* point to char immediately after ":" in "<name>:<value>" */
	pscan++;
	nscan--;
	/* skip whitespace */
	while (isspace(*pscan)) {
		pscan++;
		nscan--;
		if (nscan == 0)
			return NULL;
	}
	if (nscan == 0)
		return NULL;
	if (*pscan == '\'' || *pscan == '"') {
		closing_quote = *pscan;
		pscan++;
		nscan--;
		if (nscan == 0)
			return NULL;
	}

	/* look for a separator character, terminator character, or
	 * end of data
	 */
	for (i = 0, value_length = -1; i < nscan; i++) {
		if (closing_quote) {
			if (pscan[i] == '\0')
				return NULL;
			if (pscan[i] == closing_quote) {
				value_length = i;
				break;
			}
		} else
		    if (pscan[i] == ',' || pscan[i] == ';'
			|| pscan[i] == '\0') {
			value_length = i;
			break;
		}
	}
	if (value_length < 0) {
		if (closing_quote)
			return NULL;
		value_length = nscan;
	}
	orig_value_length = value_length;
	if (closing_quote == 0)
		value_length = string_length_no_trail(pscan, orig_value_length);
	value = kmalloc(value_length + 1, GFP_KERNEL|__GFP_NORETRY);
	if (value == NULL)
		return NULL;
	memcpy(value, pscan, value_length);
	((u8 *) (value))[value_length] = '\0';

	pscan += orig_value_length;
	nscan -= orig_value_length;

	/* skip past separator or closing quote */
	if (nscan > 0) {
		if (*pscan != '\0') {
			pscan++;
			nscan--;
		}
	}

	if (closing_quote && (nscan > 0)) {
		/* we still need to skip around the real separator if present */
		/* first, skip whitespace */
		while (isspace(*pscan)) {
			pscan++;
			nscan--;
			if (nscan == 0)
				break;
		}
		if (nscan > 0) {
			if (*pscan == ',' || *pscan == ';') {
				pscan++;
				nscan--;
			} else if (*pscan != '\0') {
				kfree(value);
				value = NULL;
				return NULL;
			}
		}
	}
	ctx->curr = pscan;
	ctx->bytes_remaining = nscan;
	return value;
}

void *
parser_string_get(struct parser_context *ctx)
{
	u8 *pscan;
	unsigned long nscan;
	int value_length = -1;
	void *value = NULL;
	int i;

	if (!ctx)
		return NULL;
	pscan = ctx->curr;
	nscan = ctx->bytes_remaining;
	if (nscan == 0)
		return NULL;
	if (!pscan)
		return NULL;
	for (i = 0, value_length = -1; i < nscan; i++)
		if (pscan[i] == '\0') {
			value_length = i;
			break;
		}
	if (value_length < 0)	/* '\0' was not included in the length */
		value_length = nscan;
	value = kmalloc(value_length + 1, GFP_KERNEL|__GFP_NORETRY);
	if (value == NULL)
		return NULL;
	if (value_length > 0)
		memcpy(value, pscan, value_length);
	((u8 *) (value))[value_length] = '\0';
	return value;
}

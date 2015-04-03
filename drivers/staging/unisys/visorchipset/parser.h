/* parser.h
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

#ifndef __PARSER_H__
#define __PARSER_H__

#include <linux/uuid.h>

#include "timskmod.h"
#include "channel.h"

typedef enum {
	PARSERSTRING_INITIATOR,
	PARSERSTRING_TARGET,
	PARSERSTRING_CONNECTION,
	PARSERSTRING_NAME,
} PARSER_WHICH_STRING;

struct parser_context *parser_init(u64 addr, u32 bytes, BOOL isLocal,
				   BOOL *tryAgain);
struct parser_context *parser_init_byte_stream(u64 addr, u32 bytes, BOOL local,
				       BOOL *retry);
void parser_param_start(struct parser_context *ctx,
			PARSER_WHICH_STRING which_string);
void *parser_param_get(struct parser_context *ctx, char *nam, int namesize);
void *parser_string_get(struct parser_context *ctx);
uuid_le parser_id_get(struct parser_context *ctx);
char *parser_simpleString_get(struct parser_context *ctx);
void *parser_byte_stream_get(struct parser_context *ctx, ulong *nbytes);
void parser_done(struct parser_context *ctx);

#endif

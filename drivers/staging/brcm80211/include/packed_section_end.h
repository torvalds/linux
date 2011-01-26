/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* Error check - BWL_PACKED_SECTION is defined in packed_section_start.h
 * and undefined in packed_section_end.h. If it is NOT defined at this
 * point, then there is a missing include of packed_section_start.h.
 */
#ifdef BWL_PACKED_SECTION
#undef BWL_PACKED_SECTION
#else
#error "BWL_PACKED_SECTION is NOT defined!"
#endif

/* Compiler-specific directives for structure packing are declared in
 * packed_section_start.h. This marks the end of the structure packing section,
 * so, undef them here.
 */
#undef	BWL_PRE_PACKED_STRUCT
#undef	BWL_POST_PACKED_STRUCT

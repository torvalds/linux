/*
 * Copyright (c) 2000-2007 Niels Provos <provos@citi.umich.edu>
 * Copyright (c) 2007-2012 Niels Provos and Nick Mathewson
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef EVENT2_TAG_H_INCLUDED_
#define EVENT2_TAG_H_INCLUDED_

/** @file event2/tag.h

  Helper functions for reading and writing tagged data onto buffers.

 */

#include <event2/visibility.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <event2/event-config.h>
#ifdef EVENT__HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef EVENT__HAVE_SYS_TIME_H
#include <sys/time.h>
#endif

/* For int types. */
#include <event2/util.h>

struct evbuffer;

/*
 * Marshaling tagged data - We assume that all tags are inserted in their
 * numeric order - so that unknown tags will always be higher than the
 * known ones - and we can just ignore the end of an event buffer.
 */

EVENT2_EXPORT_SYMBOL
void evtag_init(void);

/**
   Unmarshals the header and returns the length of the payload

   @param evbuf the buffer from which to unmarshal data
   @param ptag a pointer in which the tag id is being stored
   @returns -1 on failure or the number of bytes in the remaining payload.
*/
EVENT2_EXPORT_SYMBOL
int evtag_unmarshal_header(struct evbuffer *evbuf, ev_uint32_t *ptag);

EVENT2_EXPORT_SYMBOL
void evtag_marshal(struct evbuffer *evbuf, ev_uint32_t tag, const void *data,
    ev_uint32_t len);
EVENT2_EXPORT_SYMBOL
void evtag_marshal_buffer(struct evbuffer *evbuf, ev_uint32_t tag,
    struct evbuffer *data);

/**
  Encode an integer and store it in an evbuffer.

  We encode integers by nybbles; the first nibble contains the number
  of significant nibbles - 1;  this allows us to encode up to 64-bit
  integers.  This function is byte-order independent.

  @param evbuf evbuffer to store the encoded number
  @param number a 32-bit integer
 */
EVENT2_EXPORT_SYMBOL
void evtag_encode_int(struct evbuffer *evbuf, ev_uint32_t number);
EVENT2_EXPORT_SYMBOL
void evtag_encode_int64(struct evbuffer *evbuf, ev_uint64_t number);

EVENT2_EXPORT_SYMBOL
void evtag_marshal_int(struct evbuffer *evbuf, ev_uint32_t tag,
    ev_uint32_t integer);
EVENT2_EXPORT_SYMBOL
void evtag_marshal_int64(struct evbuffer *evbuf, ev_uint32_t tag,
    ev_uint64_t integer);

EVENT2_EXPORT_SYMBOL
void evtag_marshal_string(struct evbuffer *buf, ev_uint32_t tag,
    const char *string);

EVENT2_EXPORT_SYMBOL
void evtag_marshal_timeval(struct evbuffer *evbuf, ev_uint32_t tag,
    struct timeval *tv);

EVENT2_EXPORT_SYMBOL
int evtag_unmarshal(struct evbuffer *src, ev_uint32_t *ptag,
    struct evbuffer *dst);
EVENT2_EXPORT_SYMBOL
int evtag_peek(struct evbuffer *evbuf, ev_uint32_t *ptag);
EVENT2_EXPORT_SYMBOL
int evtag_peek_length(struct evbuffer *evbuf, ev_uint32_t *plength);
EVENT2_EXPORT_SYMBOL
int evtag_payload_length(struct evbuffer *evbuf, ev_uint32_t *plength);
EVENT2_EXPORT_SYMBOL
int evtag_consume(struct evbuffer *evbuf);

EVENT2_EXPORT_SYMBOL
int evtag_unmarshal_int(struct evbuffer *evbuf, ev_uint32_t need_tag,
    ev_uint32_t *pinteger);
EVENT2_EXPORT_SYMBOL
int evtag_unmarshal_int64(struct evbuffer *evbuf, ev_uint32_t need_tag,
    ev_uint64_t *pinteger);

EVENT2_EXPORT_SYMBOL
int evtag_unmarshal_fixed(struct evbuffer *src, ev_uint32_t need_tag,
    void *data, size_t len);

EVENT2_EXPORT_SYMBOL
int evtag_unmarshal_string(struct evbuffer *evbuf, ev_uint32_t need_tag,
    char **pstring);

EVENT2_EXPORT_SYMBOL
int evtag_unmarshal_timeval(struct evbuffer *evbuf, ev_uint32_t need_tag,
    struct timeval *ptv);

#ifdef __cplusplus
}
#endif

#endif /* EVENT2_TAG_H_INCLUDED_ */

/*
 * svndiff.c -- Encoding and decoding svndiff-format deltas.
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */


#include <assert.h>
#include <string.h>
#include "svn_delta.h"
#include "svn_io.h"
#include "delta.h"
#include "svn_pools.h"
#include "svn_private_config.h"

#include "private/svn_error_private.h"
#include "private/svn_delta_private.h"
#include "private/svn_subr_private.h"
#include "private/svn_string_private.h"
#include "private/svn_dep_compat.h"

static const char SVNDIFF_V0[] = { 'S', 'V', 'N', 0 };
static const char SVNDIFF_V1[] = { 'S', 'V', 'N', 1 };
static const char SVNDIFF_V2[] = { 'S', 'V', 'N', 2 };

#define SVNDIFF_HEADER_SIZE (sizeof(SVNDIFF_V0))

static const char *
get_svndiff_header(int version)
{
  if (version == 2)
    return SVNDIFF_V2;
  else if (version == 1)
    return SVNDIFF_V1;
  else
    return SVNDIFF_V0;
}

/* ----- Text delta to svndiff ----- */

/* We make one of these and get it passed back to us in calls to the
   window handler.  We only use it to record the write function and
   baton passed to svn_txdelta_to_svndiff3().  */
struct encoder_baton {
  svn_stream_t *output;
  svn_boolean_t header_done;
  int version;
  int compression_level;
  /* Pool for temporary allocations, will be cleared periodically. */
  apr_pool_t *scratch_pool;
};

/* This is at least as big as the largest size for a single instruction. */
#define MAX_INSTRUCTION_LEN (2*SVN__MAX_ENCODED_UINT_LEN+1)
/* This is at least as big as the largest possible instructions
   section: in theory, the instructions could be SVN_DELTA_WINDOW_SIZE
   1-byte copy-from-source instructions (though this is very unlikely). */
#define MAX_INSTRUCTION_SECTION_LEN (SVN_DELTA_WINDOW_SIZE*MAX_INSTRUCTION_LEN)


/* Append an encoded integer to a string.  */
static void
append_encoded_int(svn_stringbuf_t *header, svn_filesize_t val)
{
  unsigned char buf[SVN__MAX_ENCODED_UINT_LEN], *p;

  SVN_ERR_ASSERT_NO_RETURN(val >= 0);
  p = svn__encode_uint(buf, (apr_uint64_t)val);
  svn_stringbuf_appendbytes(header, (const char *)buf, p - buf);
}

static svn_error_t *
send_simple_insertion_window(svn_txdelta_window_t *window,
                             struct encoder_baton *eb)
{
  unsigned char headers[SVNDIFF_HEADER_SIZE + 5 * SVN__MAX_ENCODED_UINT_LEN
                          + MAX_INSTRUCTION_LEN];
  unsigned char ibuf[MAX_INSTRUCTION_LEN];
  unsigned char *header_current;
  apr_size_t header_len;
  apr_size_t ip_len, i;
  apr_size_t len = window->new_data->len;

  /* there is only one target copy op. It must span the whole window */
  assert(window->ops[0].action_code == svn_txdelta_new);
  assert(window->ops[0].length == window->tview_len);
  assert(window->ops[0].offset == 0);

  /* write stream header if necessary */
  if (!eb->header_done)
    {
      eb->header_done = TRUE;
      memcpy(headers, get_svndiff_header(eb->version), SVNDIFF_HEADER_SIZE);
      header_current = headers + SVNDIFF_HEADER_SIZE;
    }
  else
    {
      header_current = headers;
    }

  /* Encode the action code and length.  */
  if (window->tview_len >> 6 == 0)
    {
      ibuf[0] = (unsigned char)(window->tview_len + (0x2 << 6));
      ip_len = 1;
    }
  else
    {
      ibuf[0] = (0x2 << 6);
      ip_len = svn__encode_uint(ibuf + 1, window->tview_len) - ibuf;
    }

  /* encode the window header.  Please note that the source window may
   * have content despite not being used for deltification. */
  header_current = svn__encode_uint(header_current,
                                    (apr_uint64_t)window->sview_offset);
  header_current = svn__encode_uint(header_current, window->sview_len);
  header_current = svn__encode_uint(header_current, window->tview_len);
  header_current[0] = (unsigned char)ip_len;  /* 1 instruction */
  header_current = svn__encode_uint(&header_current[1], len);

  /* append instructions (1 to a handful of bytes) */
  for (i = 0; i < ip_len; ++i)
    header_current[i] = ibuf[i];

  header_len = header_current - headers + ip_len;

  /* Write out the window.  */
  SVN_ERR(svn_stream_write(eb->output, (const char *)headers, &header_len));
  if (len)
    SVN_ERR(svn_stream_write(eb->output, window->new_data->data, &len));

  return SVN_NO_ERROR;
}

/* Encodes delta window WINDOW to svndiff-format.
   The svndiff version is VERSION. COMPRESSION_LEVEL is the
   compression level to use.
   Returned values will be allocated in POOL or refer to *WINDOW
   fields. */
static svn_error_t *
encode_window(svn_stringbuf_t **instructions_p,
              svn_stringbuf_t **header_p,
              const svn_string_t **newdata_p,
              svn_txdelta_window_t *window,
              int version,
              int compression_level,
              apr_pool_t *pool)
{
  svn_stringbuf_t *instructions;
  svn_stringbuf_t *header;
  const svn_string_t *newdata;
  unsigned char ibuf[MAX_INSTRUCTION_LEN], *ip;
  const svn_txdelta_op_t *op;

  /* create the necessary data buffers */
  instructions = svn_stringbuf_create_empty(pool);
  header = svn_stringbuf_create_empty(pool);

  /* Encode the instructions.  */
  for (op = window->ops; op < window->ops + window->num_ops; op++)
    {
      /* Encode the action code and length.  */
      ip = ibuf;
      switch (op->action_code)
        {
        case svn_txdelta_source: *ip = 0; break;
        case svn_txdelta_target: *ip = (0x1 << 6); break;
        case svn_txdelta_new:    *ip = (0x2 << 6); break;
        }
      if (op->length >> 6 == 0)
        *ip++ |= (unsigned char)op->length;
      else
        ip = svn__encode_uint(ip + 1, op->length);
      if (op->action_code != svn_txdelta_new)
        ip = svn__encode_uint(ip, op->offset);
      svn_stringbuf_appendbytes(instructions, (const char *)ibuf, ip - ibuf);
    }

  /* Encode the header.  */
  append_encoded_int(header, window->sview_offset);
  append_encoded_int(header, window->sview_len);
  append_encoded_int(header, window->tview_len);
  if (version == 2)
    {
      svn_stringbuf_t *compressed_instructions;
      compressed_instructions = svn_stringbuf_create_empty(pool);
      SVN_ERR(svn__compress_lz4(instructions->data, instructions->len,
                                compressed_instructions));
      instructions = compressed_instructions;
    }
  else if (version == 1)
    {
      svn_stringbuf_t *compressed_instructions;
      compressed_instructions = svn_stringbuf_create_empty(pool);
      SVN_ERR(svn__compress_zlib(instructions->data, instructions->len,
                                 compressed_instructions, compression_level));
      instructions = compressed_instructions;
    }
  append_encoded_int(header, instructions->len);

  /* Encode the data. */
  if (version == 2)
    {
      svn_stringbuf_t *compressed = svn_stringbuf_create_empty(pool);

      SVN_ERR(svn__compress_lz4(window->new_data->data, window->new_data->len,
                                compressed));
      newdata = svn_stringbuf__morph_into_string(compressed);
    }
  else if (version == 1)
    {
      svn_stringbuf_t *compressed = svn_stringbuf_create_empty(pool);

      SVN_ERR(svn__compress_zlib(window->new_data->data, window->new_data->len,
                                 compressed, compression_level));
      newdata = svn_stringbuf__morph_into_string(compressed);
    }
  else
    newdata = window->new_data;

  append_encoded_int(header, newdata->len);

  *instructions_p = instructions;
  *header_p = header;
  *newdata_p = newdata;

  return SVN_NO_ERROR;
}

/* Note: When changing things here, check the related comment in
   the svn_txdelta_to_svndiff_stream() function.  */
static svn_error_t *
window_handler(svn_txdelta_window_t *window, void *baton)
{
  struct encoder_baton *eb = baton;
  apr_size_t len;
  svn_stringbuf_t *instructions;
  svn_stringbuf_t *header;
  const svn_string_t *newdata;

  /* use specialized code if there is no source */
  if (window && !window->src_ops && window->num_ops == 1 && !eb->version)
    return svn_error_trace(send_simple_insertion_window(window, eb));

  /* Make sure we write the header.  */
  if (!eb->header_done)
    {
      len = SVNDIFF_HEADER_SIZE;
      SVN_ERR(svn_stream_write(eb->output, get_svndiff_header(eb->version),
                               &len));
      eb->header_done = TRUE;
    }

  if (window == NULL)
    {
      /* We're done; clean up. */
      SVN_ERR(svn_stream_close(eb->output));

      svn_pool_destroy(eb->scratch_pool);

      return SVN_NO_ERROR;
    }

  svn_pool_clear(eb->scratch_pool);

  SVN_ERR(encode_window(&instructions, &header, &newdata, window,
                        eb->version, eb->compression_level,
                        eb->scratch_pool));

  /* Write out the window.  */
  len = header->len;
  SVN_ERR(svn_stream_write(eb->output, header->data, &len));
  if (instructions->len > 0)
    {
      len = instructions->len;
      SVN_ERR(svn_stream_write(eb->output, instructions->data, &len));
    }
  if (newdata->len > 0)
    {
      len = newdata->len;
      SVN_ERR(svn_stream_write(eb->output, newdata->data, &len));
    }

  return SVN_NO_ERROR;
}

void
svn_txdelta_to_svndiff3(svn_txdelta_window_handler_t *handler,
                        void **handler_baton,
                        svn_stream_t *output,
                        int svndiff_version,
                        int compression_level,
                        apr_pool_t *pool)
{
  struct encoder_baton *eb;

  eb = apr_palloc(pool, sizeof(*eb));
  eb->output = output;
  eb->header_done = FALSE;
  eb->scratch_pool = svn_pool_create(pool);
  eb->version = svndiff_version;
  eb->compression_level = compression_level;

  *handler = window_handler;
  *handler_baton = eb;
}

void
svn_txdelta_to_svndiff2(svn_txdelta_window_handler_t *handler,
                        void **handler_baton,
                        svn_stream_t *output,
                        int svndiff_version,
                        apr_pool_t *pool)
{
  svn_txdelta_to_svndiff3(handler, handler_baton, output, svndiff_version,
                          SVN_DELTA_COMPRESSION_LEVEL_DEFAULT, pool);
}

void
svn_txdelta_to_svndiff(svn_stream_t *output,
                       apr_pool_t *pool,
                       svn_txdelta_window_handler_t *handler,
                       void **handler_baton)
{
  svn_txdelta_to_svndiff3(handler, handler_baton, output, 0,
                          SVN_DELTA_COMPRESSION_LEVEL_DEFAULT, pool);
}


/* ----- svndiff to text delta ----- */

/* An svndiff parser object.  */
struct decode_baton
{
  /* Once the svndiff parser has enough data buffered to create a
     "window", it passes this window to the caller's consumer routine.  */
  svn_txdelta_window_handler_t consumer_func;
  void *consumer_baton;

  /* Pool to create subpools from; each developing window will be a
     subpool.  */
  apr_pool_t *pool;

  /* The current subpool which contains our current window-buffer.  */
  apr_pool_t *subpool;

  /* The actual svndiff data buffer, living within subpool.  */
  svn_stringbuf_t *buffer;

  /* The offset and size of the last source view, so that we can check
     to make sure the next one isn't sliding backwards.  */
  svn_filesize_t last_sview_offset;
  apr_size_t last_sview_len;

  /* We have to discard four bytes at the beginning for the header.
     This field keeps track of how many of those bytes we have read.  */
  apr_size_t header_bytes;

  /* Do we want an error to occur when we close the stream that
     indicates we didn't send the whole svndiff data?  If you plan to
     not transmit the whole svndiff data stream, you will want this to
     be FALSE. */
  svn_boolean_t error_on_early_close;

  /* svndiff version in use by delta.  */
  unsigned char version;

  /* Length of parsed delta window header. 0 if window is not parsed yet. */
  apr_size_t window_header_len;

  /* Five integer fields of parsed delta window header. Valid only if
     WINDOW_HEADER_LEN > 0 */
  svn_filesize_t  sview_offset;
  apr_size_t sview_len;
  apr_size_t tview_len;
  apr_size_t inslen;
  apr_size_t newlen;
};


/* Wrapper aroung svn__deencode_uint taking a file size as *VAL. */
static const unsigned char *
decode_file_offset(svn_filesize_t *val,
                   const unsigned char *p,
                   const unsigned char *end)
{
  apr_uint64_t temp = 0;
  const unsigned char *result = svn__decode_uint(&temp, p, end);
  *val = (svn_filesize_t)temp;

  return result;
}

/* Same as above, only decode into a size variable. */
static const unsigned char *
decode_size(apr_size_t *val,
            const unsigned char *p,
            const unsigned char *end)
{
  apr_uint64_t temp = 0;
  const unsigned char *result = svn__decode_uint(&temp, p, end);
  if (temp > APR_SIZE_MAX)
    return NULL;

  *val = (apr_size_t)temp;
  return result;
}

/* Decode an instruction into OP, returning a pointer to the text
   after the instruction.  Note that if the action code is
   svn_txdelta_new, the offset field of *OP will not be set.  */
static const unsigned char *
decode_instruction(svn_txdelta_op_t *op,
                   const unsigned char *p,
                   const unsigned char *end)
{
  apr_size_t c;
  apr_size_t action;

  if (p == end)
    return NULL;

  /* We need this more than once */
  c = *p++;

  /* Decode the instruction selector.  */
  action = (c >> 6) & 0x3;
  if (action >= 0x3)
      return NULL;

  /* This relies on enum svn_delta_action values to match and never to be
     redefined. */
  op->action_code = (enum svn_delta_action)(action);

  /* Decode the length and offset.  */
  op->length = c & 0x3f;
  if (op->length == 0)
    {
      p = decode_size(&op->length, p, end);
      if (p == NULL)
        return NULL;
    }
  if (action != svn_txdelta_new)
    {
      p = decode_size(&op->offset, p, end);
      if (p == NULL)
        return NULL;
    }

  return p;
}

/* Count the instructions in the range [P..END-1] and make sure they
   are valid for the given window lengths.  Return an error if the
   instructions are invalid; otherwise set *NINST to the number of
   instructions.  */
static svn_error_t *
count_and_verify_instructions(int *ninst,
                              const unsigned char *p,
                              const unsigned char *end,
                              apr_size_t sview_len,
                              apr_size_t tview_len,
                              apr_size_t new_len)
{
  int n = 0;
  svn_txdelta_op_t op;
  apr_size_t tpos = 0, npos = 0;

  while (p < end)
    {
      p = decode_instruction(&op, p, end);

      /* Detect any malformed operations from the instruction stream. */
      if (p == NULL)
        return svn_error_createf
          (SVN_ERR_SVNDIFF_INVALID_OPS, NULL,
           _("Invalid diff stream: insn %d cannot be decoded"), n);
      else if (op.length == 0)
        return svn_error_createf
          (SVN_ERR_SVNDIFF_INVALID_OPS, NULL,
           _("Invalid diff stream: insn %d has length zero"), n);
      else if (op.length > tview_len - tpos)
        return svn_error_createf
          (SVN_ERR_SVNDIFF_INVALID_OPS, NULL,
           _("Invalid diff stream: insn %d overflows the target view"), n);

      switch (op.action_code)
        {
        case svn_txdelta_source:
          if (op.length > sview_len - op.offset ||
              op.offset > sview_len)
            return svn_error_createf
              (SVN_ERR_SVNDIFF_INVALID_OPS, NULL,
               _("Invalid diff stream: "
                 "[src] insn %d overflows the source view"), n);
          break;
        case svn_txdelta_target:
          if (op.offset >= tpos)
            return svn_error_createf
              (SVN_ERR_SVNDIFF_INVALID_OPS, NULL,
               _("Invalid diff stream: "
                 "[tgt] insn %d starts beyond the target view position"), n);
          break;
        case svn_txdelta_new:
          if (op.length > new_len - npos)
            return svn_error_createf
              (SVN_ERR_SVNDIFF_INVALID_OPS, NULL,
               _("Invalid diff stream: "
                 "[new] insn %d overflows the new data section"), n);
          npos += op.length;
          break;
        }
      tpos += op.length;
      n++;
    }
  if (tpos != tview_len)
    return svn_error_create(SVN_ERR_SVNDIFF_INVALID_OPS, NULL,
                            _("Delta does not fill the target window"));
  if (npos != new_len)
    return svn_error_create(SVN_ERR_SVNDIFF_INVALID_OPS, NULL,
                            _("Delta does not contain enough new data"));

  *ninst = n;
  return SVN_NO_ERROR;
}

/* Given the five integer fields of a window header and a pointer to
   the remainder of the window contents, fill in a delta window
   structure *WINDOW.  New allocations will be performed in POOL;
   the new_data field of *WINDOW will refer directly to memory pointed
   to by DATA. */
static svn_error_t *
decode_window(svn_txdelta_window_t *window, svn_filesize_t sview_offset,
              apr_size_t sview_len, apr_size_t tview_len, apr_size_t inslen,
              apr_size_t newlen, const unsigned char *data, apr_pool_t *pool,
              unsigned int version)
{
  const unsigned char *insend;
  int ninst;
  apr_size_t npos;
  svn_txdelta_op_t *ops, *op;
  svn_string_t *new_data;

  window->sview_offset = sview_offset;
  window->sview_len = sview_len;
  window->tview_len = tview_len;

  insend = data + inslen;

  if (version == 2)
    {
      svn_stringbuf_t *instout = svn_stringbuf_create_empty(pool);
      svn_stringbuf_t *ndout = svn_stringbuf_create_empty(pool);

      SVN_ERR(svn__decompress_lz4(insend, newlen, ndout,
                                  SVN_DELTA_WINDOW_SIZE));
      SVN_ERR(svn__decompress_lz4(data, insend - data, instout,
                                  MAX_INSTRUCTION_SECTION_LEN));

      newlen = ndout->len;
      data = (unsigned char *)instout->data;
      insend = (unsigned char *)instout->data + instout->len;

      new_data = svn_stringbuf__morph_into_string(ndout);
    }
  else if (version == 1)
    {
      svn_stringbuf_t *instout = svn_stringbuf_create_empty(pool);
      svn_stringbuf_t *ndout = svn_stringbuf_create_empty(pool);

      SVN_ERR(svn__decompress_zlib(insend, newlen, ndout,
                                   SVN_DELTA_WINDOW_SIZE));
      SVN_ERR(svn__decompress_zlib(data, insend - data, instout,
                                   MAX_INSTRUCTION_SECTION_LEN));

      newlen = ndout->len;
      data = (unsigned char *)instout->data;
      insend = (unsigned char *)instout->data + instout->len;

      new_data = svn_stringbuf__morph_into_string(ndout);
    }
  else
    {
      /* Copy the data because an svn_string_t must have the invariant
         data[len]=='\0'. */
      new_data = svn_string_ncreate((const char*)insend, newlen, pool);
    }

  /* Count the instructions and make sure they are all valid.  */
  SVN_ERR(count_and_verify_instructions(&ninst, data, insend,
                                        sview_len, tview_len, newlen));

  /* Allocate a buffer for the instructions and decode them. */
  ops = apr_palloc(pool, ninst * sizeof(*ops));
  npos = 0;
  window->src_ops = 0;
  for (op = ops; op < ops + ninst; op++)
    {
      data = decode_instruction(op, data, insend);
      if (op->action_code == svn_txdelta_source)
        ++window->src_ops;
      else if (op->action_code == svn_txdelta_new)
        {
          op->offset = npos;
          npos += op->length;
        }
    }
  SVN_ERR_ASSERT(data == insend);

  window->ops = ops;
  window->num_ops = ninst;
  window->new_data = new_data;

  return SVN_NO_ERROR;
}

static svn_error_t *
write_handler(void *baton,
              const char *buffer,
              apr_size_t *len)
{
  struct decode_baton *db = (struct decode_baton *) baton;
  const unsigned char *p, *end;
  apr_size_t buflen = *len;

  /* Chew up four bytes at the beginning for the header.  */
  if (db->header_bytes < SVNDIFF_HEADER_SIZE)
    {
      apr_size_t nheader = SVNDIFF_HEADER_SIZE - db->header_bytes;
      if (nheader > buflen)
        nheader = buflen;
      if (memcmp(buffer, SVNDIFF_V0 + db->header_bytes, nheader) == 0)
        db->version = 0;
      else if (memcmp(buffer, SVNDIFF_V1 + db->header_bytes, nheader) == 0)
        db->version = 1;
      else if (memcmp(buffer, SVNDIFF_V2 + db->header_bytes, nheader) == 0)
        db->version = 2;
      else
        return svn_error_create(SVN_ERR_SVNDIFF_INVALID_HEADER, NULL,
                                _("Svndiff has invalid header"));
      buflen -= nheader;
      buffer += nheader;
      db->header_bytes += nheader;
    }

  /* Concatenate the old with the new.  */
  svn_stringbuf_appendbytes(db->buffer, buffer, buflen);

  /* We have a buffer of svndiff data that might be good for:

     a) an integral number of windows' worth of data - this is a
        trivial case.  Make windows from our data and ship them off.

     b) a non-integral number of windows' worth of data - we shall
        consume the integral portion of the window data, and then
        somewhere in the following loop the decoding of the svndiff
        data will run out of stuff to decode, and will simply return
        SVN_NO_ERROR, anxiously awaiting more data.
  */

  while (1)
    {
      svn_txdelta_window_t window;

      /* Read the header, if we have enough bytes for that.  */
      p = (const unsigned char *) db->buffer->data;
      end = (const unsigned char *) db->buffer->data + db->buffer->len;

      if (db->window_header_len == 0)
        {
          svn_filesize_t sview_offset;
          apr_size_t sview_len, tview_len, inslen, newlen;
          const unsigned char *hdr_start = p;

          p = decode_file_offset(&sview_offset, p, end);
          if (p == NULL)
              break;

          p = decode_size(&sview_len, p, end);
          if (p == NULL)
              break;

          p = decode_size(&tview_len, p, end);
          if (p == NULL)
              break;

          p = decode_size(&inslen, p, end);
          if (p == NULL)
              break;

          p = decode_size(&newlen, p, end);
          if (p == NULL)
              break;

          if (tview_len > SVN_DELTA_WINDOW_SIZE ||
              sview_len > SVN_DELTA_WINDOW_SIZE ||
              /* for svndiff1, newlen includes the original length */
              newlen > SVN_DELTA_WINDOW_SIZE + SVN__MAX_ENCODED_UINT_LEN ||
              inslen > MAX_INSTRUCTION_SECTION_LEN)
            return svn_error_create(
                     SVN_ERR_SVNDIFF_CORRUPT_WINDOW, NULL,
                     _("Svndiff contains a too-large window"));

          /* Check for integer overflow.  */
          if (sview_offset < 0 || inslen + newlen < inslen
              || sview_len + tview_len < sview_len
              || (apr_size_t)sview_offset + sview_len < (apr_size_t)sview_offset)
            return svn_error_create(
                      SVN_ERR_SVNDIFF_CORRUPT_WINDOW, NULL,
                      _("Svndiff contains corrupt window header"));

          /* Check for source windows which slide backwards.  */
          if (sview_len > 0
              && (sview_offset < db->last_sview_offset
                  || (sview_offset + sview_len
                      < db->last_sview_offset + db->last_sview_len)))
            return svn_error_create(
                     SVN_ERR_SVNDIFF_BACKWARD_VIEW, NULL,
                     _("Svndiff has backwards-sliding source views"));

          /* Remember parsed window header. */
          db->window_header_len = p - hdr_start;
          db->sview_offset = sview_offset;
          db->sview_len = sview_len;
          db->tview_len = tview_len;
          db->inslen = inslen;
          db->newlen = newlen;
        }
      else
        {
          /* Skip already parsed window header. */
          p += db->window_header_len;
        }

      /* Wait for more data if we don't have enough bytes for the
         whole window. */
      if ((apr_size_t) (end - p) < db->inslen + db->newlen)
        return SVN_NO_ERROR;

      /* Decode the window and send it off. */
      SVN_ERR(decode_window(&window, db->sview_offset, db->sview_len,
                            db->tview_len, db->inslen, db->newlen, p,
                            db->subpool, db->version));
      SVN_ERR(db->consumer_func(&window, db->consumer_baton));

      p += db->inslen + db->newlen;

      /* Remove processed data from the buffer.  */
      svn_stringbuf_remove(db->buffer, 0, db->buffer->len - (end - p));

      /* Reset window header length. */
      db->window_header_len = 0;

      /* Remember the offset and length of the source view for next time.  */
      db->last_sview_offset = db->sview_offset;
      db->last_sview_len = db->sview_len;

      /* Clear subpool. */
      svn_pool_clear(db->subpool);
    }

  /* At this point we processed all integral windows and DB->BUFFER is empty
     or contains partially read window header.
     Check that unprocessed data is not larger than theoretical maximum
     window header size. */
  if (db->buffer->len > 5 * SVN__MAX_ENCODED_UINT_LEN)
    return svn_error_create(SVN_ERR_SVNDIFF_CORRUPT_WINDOW, NULL,
                            _("Svndiff contains a too-large window header"));

  return SVN_NO_ERROR;
}

/* Minimal svn_stream_t write handler, doing nothing */
static svn_error_t *
noop_write_handler(void *baton,
                   const char *buffer,
                   apr_size_t *len)
{
  return SVN_NO_ERROR;
}

static svn_error_t *
close_handler(void *baton)
{
  struct decode_baton *db = (struct decode_baton *) baton;
  svn_error_t *err;

  /* Make sure that we're at a plausible end of stream, returning an
     error if we are expected to do so.  */
  if ((db->error_on_early_close)
      && (db->header_bytes < 4 || db->buffer->len != 0))
    return svn_error_create(SVN_ERR_SVNDIFF_UNEXPECTED_END, NULL,
                            _("Unexpected end of svndiff input"));

  /* Tell the window consumer that we're done, and clean up.  */
  err = db->consumer_func(NULL, db->consumer_baton);
  svn_pool_destroy(db->pool);
  return err;
}


svn_stream_t *
svn_txdelta_parse_svndiff(svn_txdelta_window_handler_t handler,
                          void *handler_baton,
                          svn_boolean_t error_on_early_close,
                          apr_pool_t *pool)
{
  svn_stream_t *stream;

  if (handler != svn_delta_noop_window_handler)
    {
      apr_pool_t *subpool = svn_pool_create(pool);
      struct decode_baton *db = apr_palloc(pool, sizeof(*db));

      db->consumer_func = handler;
      db->consumer_baton = handler_baton;
      db->pool = subpool;
      db->subpool = svn_pool_create(subpool);
      db->buffer = svn_stringbuf_create_empty(db->pool);
      db->last_sview_offset = 0;
      db->last_sview_len = 0;
      db->header_bytes = 0;
      db->error_on_early_close = error_on_early_close;
      db->window_header_len = 0;
      stream = svn_stream_create(db, pool);

      svn_stream_set_write(stream, write_handler);
      svn_stream_set_close(stream, close_handler);
    }
  else
    {
      /* And else we just ignore everything as efficiently as we can.
         by only hooking a no-op handler */
      stream = svn_stream_create(NULL, pool);
      svn_stream_set_write(stream, noop_write_handler);
    }
  return stream;
}


/* Routines for reading one svndiff window at a time. */

/* Read one byte from STREAM into *BYTE. */
static svn_error_t *
read_one_byte(unsigned char *byte, svn_stream_t *stream)
{
  char c;
  apr_size_t len = 1;

  SVN_ERR(svn_stream_read_full(stream, &c, &len));
  if (len == 0)
    return svn_error_create(SVN_ERR_SVNDIFF_UNEXPECTED_END, NULL,
                            _("Unexpected end of svndiff input"));
  *byte = (unsigned char) c;
  return SVN_NO_ERROR;
}

/* Read and decode one integer from STREAM into *SIZE.
   Increment *BYTE_COUNTER by the number of chars we have read. */
static svn_error_t *
read_one_size(apr_size_t *size,
              apr_size_t *byte_counter,
              svn_stream_t *stream)
{
  unsigned char c;

  *size = 0;
  while (1)
    {
      SVN_ERR(read_one_byte(&c, stream));
      ++*byte_counter;
      *size = (*size << 7) | (c & 0x7f);
      if (!(c & 0x80))
        break;
    }
  return SVN_NO_ERROR;
}

/* Read a window header from STREAM and check it for integer overflow. */
static svn_error_t *
read_window_header(svn_stream_t *stream, svn_filesize_t *sview_offset,
                   apr_size_t *sview_len, apr_size_t *tview_len,
                   apr_size_t *inslen, apr_size_t *newlen,
                   apr_size_t *header_len)
{
  unsigned char c;

  /* Read the source view offset by hand, since it's not an apr_size_t. */
  *header_len = 0;
  *sview_offset = 0;
  while (1)
    {
      SVN_ERR(read_one_byte(&c, stream));
      ++*header_len;
      *sview_offset = (*sview_offset << 7) | (c & 0x7f);
      if (!(c & 0x80))
        break;
    }

  /* Read the four size fields. */
  SVN_ERR(read_one_size(sview_len, header_len, stream));
  SVN_ERR(read_one_size(tview_len, header_len, stream));
  SVN_ERR(read_one_size(inslen, header_len, stream));
  SVN_ERR(read_one_size(newlen, header_len, stream));

  if (*tview_len > SVN_DELTA_WINDOW_SIZE ||
      *sview_len > SVN_DELTA_WINDOW_SIZE ||
      /* for svndiff1, newlen includes the original length */
      *newlen > SVN_DELTA_WINDOW_SIZE + SVN__MAX_ENCODED_UINT_LEN ||
      *inslen > MAX_INSTRUCTION_SECTION_LEN)
    return svn_error_create(SVN_ERR_SVNDIFF_CORRUPT_WINDOW, NULL,
                            _("Svndiff contains a too-large window"));

  /* Check for integer overflow.  */
  if (*sview_offset < 0 || *inslen + *newlen < *inslen
      || *sview_len + *tview_len < *sview_len
      || (apr_size_t)*sview_offset + *sview_len < (apr_size_t)*sview_offset)
    return svn_error_create(SVN_ERR_SVNDIFF_CORRUPT_WINDOW, NULL,
                            _("Svndiff contains corrupt window header"));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_txdelta_read_svndiff_window(svn_txdelta_window_t **window,
                                svn_stream_t *stream,
                                int svndiff_version,
                                apr_pool_t *pool)
{
  svn_filesize_t sview_offset;
  apr_size_t sview_len, tview_len, inslen, newlen, len, header_len;
  unsigned char *buf;

  SVN_ERR(read_window_header(stream, &sview_offset, &sview_len, &tview_len,
                             &inslen, &newlen, &header_len));
  len = inslen + newlen;
  buf = apr_palloc(pool, len);
  SVN_ERR(svn_stream_read_full(stream, (char*)buf, &len));
  if (len < inslen + newlen)
    return svn_error_create(SVN_ERR_SVNDIFF_UNEXPECTED_END, NULL,
                            _("Unexpected end of svndiff input"));
  *window = apr_palloc(pool, sizeof(**window));
  return decode_window(*window, sview_offset, sview_len, tview_len, inslen,
                       newlen, buf, pool, svndiff_version);
}


svn_error_t *
svn_txdelta_skip_svndiff_window(apr_file_t *file,
                                int svndiff_version,
                                apr_pool_t *pool)
{
  svn_stream_t *stream = svn_stream_from_aprfile2(file, TRUE, pool);
  svn_filesize_t sview_offset;
  apr_size_t sview_len, tview_len, inslen, newlen, header_len;
  apr_off_t offset;

  SVN_ERR(read_window_header(stream, &sview_offset, &sview_len, &tview_len,
                             &inslen, &newlen, &header_len));

  offset = inslen + newlen;
  return svn_io_file_seek(file, APR_CUR, &offset, pool);
}

svn_error_t *
svn_txdelta__read_raw_window_len(apr_size_t *window_len,
                                 svn_stream_t *stream,
                                 apr_pool_t *pool)
{
  svn_filesize_t sview_offset;
  apr_size_t sview_len, tview_len, inslen, newlen, header_len;

  SVN_ERR(read_window_header(stream, &sview_offset, &sview_len, &tview_len,
                             &inslen, &newlen, &header_len));

  *window_len = inslen + newlen + header_len;
  return SVN_NO_ERROR;
}

typedef struct svndiff_stream_baton_t
{
  apr_pool_t *scratch_pool;
  svn_txdelta_stream_t *txstream;
  svn_txdelta_window_handler_t handler;
  void *handler_baton;
  svn_stringbuf_t *window_buffer;
  apr_size_t read_pos;
  svn_boolean_t hit_eof;
} svndiff_stream_baton_t;

static svn_error_t *
svndiff_stream_write_fn(void *baton, const char *data, apr_size_t *len)
{
  svndiff_stream_baton_t *b = baton;

  /* The memory usage here is limited, as this buffer doesn't grow
     beyond the (header size + max window size in svndiff format).
     See the comment in svn_txdelta_to_svndiff_stream().  */
  svn_stringbuf_appendbytes(b->window_buffer, data, *len);

  return SVN_NO_ERROR;
}

static svn_error_t *
svndiff_stream_read_fn(void *baton, char *buffer, apr_size_t *len)
{
  svndiff_stream_baton_t *b = baton;
  apr_size_t left = *len;
  apr_size_t read = 0;

  while (left)
    {
      apr_size_t chunk_size;

      if (b->read_pos == b->window_buffer->len && !b->hit_eof)
        {
          svn_txdelta_window_t *window;

          svn_pool_clear(b->scratch_pool);
          svn_stringbuf_setempty(b->window_buffer);
          SVN_ERR(svn_txdelta_next_window(&window, b->txstream,
                                          b->scratch_pool));
          SVN_ERR(b->handler(window, b->handler_baton));
          b->read_pos = 0;

          if (!window)
            b->hit_eof = TRUE;
        }

      if (left > b->window_buffer->len - b->read_pos)
        chunk_size = b->window_buffer->len - b->read_pos;
      else
        chunk_size = left;

      if (!chunk_size)
          break;

      memcpy(buffer, b->window_buffer->data + b->read_pos, chunk_size);
      b->read_pos += chunk_size;
      buffer += chunk_size;
      read += chunk_size;
      left -= chunk_size;
    }

  *len = read;
  return SVN_NO_ERROR;
}

svn_stream_t *
svn_txdelta_to_svndiff_stream(svn_txdelta_stream_t *txstream,
                              int svndiff_version,
                              int compression_level,
                              apr_pool_t *pool)
{
  svndiff_stream_baton_t *baton;
  svn_stream_t *push_stream;
  svn_stream_t *pull_stream;

  baton = apr_pcalloc(pool, sizeof(*baton));
  baton->scratch_pool = svn_pool_create(pool);
  baton->txstream = txstream;
  baton->window_buffer = svn_stringbuf_create_empty(pool);
  baton->hit_eof = FALSE;
  baton->read_pos = 0;

  push_stream = svn_stream_create(baton, pool);
  svn_stream_set_write(push_stream, svndiff_stream_write_fn);

  /* We rely on the implementation detail of the svn_txdelta_to_svndiff3()
     function, namely, on how the window_handler() function behaves.
     As long as it writes one svndiff window at a time to the target
     stream, the memory usage of this function (in other words, how
     much data can be accumulated in the internal 'window_buffer')
     is limited.  */
  svn_txdelta_to_svndiff3(&baton->handler, &baton->handler_baton,
                          push_stream, svndiff_version,
                          compression_level, pool);

  pull_stream = svn_stream_create(baton, pool);
  svn_stream_set_read2(pull_stream, NULL, svndiff_stream_read_fn);

  return pull_stream;
}

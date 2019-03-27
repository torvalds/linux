/*
 * spillbuf.c : an in-memory buffer that can spill to disk
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

#include <apr_file_io.h>

#include "svn_io.h"
#include "svn_pools.h"

#include "private/svn_subr_private.h"


struct memblock_t {
  apr_size_t size;
  char *data;

  struct memblock_t *next;
};


struct svn_spillbuf_t {
  /* Pool for allocating blocks and the spill file.  */
  apr_pool_t *pool;

  /* Size of in-memory blocks.  */
  apr_size_t blocksize;

  /* Maximum in-memory size; start spilling when we reach this size.  */
  apr_size_t maxsize;

  /* The amount of content in memory.  */
  apr_size_t memory_size;

  /* HEAD points to the first block of the linked list of buffers.
     TAIL points to the last block, for quickly appending more blocks
     to the overall list.  */
  struct memblock_t *head;
  struct memblock_t *tail;

  /* Available blocks for storing pending data. These were allocated
     previously, then the data consumed and returned to this list.  */
  struct memblock_t *avail;

  /* When a block is borrowed for reading, it is listed here.  */
  struct memblock_t *out_for_reading;

  /* Once MEMORY_SIZE exceeds SPILL_SIZE, then arriving content will be
     appended to the (temporary) file indicated by SPILL.  */
  apr_file_t *spill;

  /* As we consume content from SPILL, this value indicates where we
     will begin reading.  */
  apr_off_t spill_start;

  /* How much content remains in SPILL.  */
  svn_filesize_t spill_size;

  /* When false, do not delete the spill file when it is closed. */
  svn_boolean_t delete_on_close;

  /* When true, and the amount of data written to the spillbuf is
     larger than MAXSIZE, all spillbuf contents will be written to the
     spill file. */
  svn_boolean_t spill_all_contents;

  /* The directory in which the spill file is created. */
  const char *dirpath;

  /* The name of the temporary spill file. */
  const char *filename;
};


struct svn_spillbuf_reader_t {
  /* Embed the spill-buffer within the reader.  */
  struct svn_spillbuf_t *buf;

  /* When we read content from the underlying spillbuf, these fields store
     the ptr/len pair. The ptr will be incremented as we "read" out of this
     buffer since we don't have to retain the original pointer (it is
     managed inside of the spillbuf).  */
  const char *sb_ptr;
  apr_size_t sb_len;

  /* If a write comes in, then we may need to save content from our
     borrowed buffer (since that buffer may be destroyed by our call into
     the spillbuf code). Note that we retain the original pointer since
     this buffer is allocated by the reader code and re-used. The SAVE_POS
     field indicates the current position within this save buffer. The
     SAVE_LEN field describes how much content is present.  */
  char *save_ptr;
  apr_size_t save_len;
  apr_size_t save_pos;
};


/* Extended spillbuf initialization. */
static void
init_spillbuf_extended(svn_spillbuf_t *buf,
                       apr_size_t blocksize,
                       apr_size_t maxsize,
                       svn_boolean_t delete_on_close,
                       svn_boolean_t spill_all_contents,
                       const char *dirpath,
                       apr_pool_t *result_pool)
{
  buf->pool = result_pool;
  buf->blocksize = blocksize;
  buf->maxsize = maxsize;
  buf->delete_on_close = delete_on_close;
  buf->spill_all_contents = spill_all_contents;
  buf->dirpath = dirpath;
}

/* Common constructor for initializing spillbufs.
   Used by svn_spillbuf__create, svn_spilbuff__reader_create. */
static void
init_spillbuf(svn_spillbuf_t *buf,
              apr_size_t blocksize,
              apr_size_t maxsize,
              apr_pool_t *result_pool)
{
  init_spillbuf_extended(buf, blocksize, maxsize,
                         TRUE, FALSE, NULL,
                         result_pool);
}

svn_spillbuf_t *
svn_spillbuf__create(apr_size_t blocksize,
                     apr_size_t maxsize,
                     apr_pool_t *result_pool)
{
  svn_spillbuf_t *buf = apr_pcalloc(result_pool, sizeof(*buf));
  init_spillbuf(buf, blocksize, maxsize, result_pool);
  return buf;
}


svn_spillbuf_t *
svn_spillbuf__create_extended(apr_size_t blocksize,
                              apr_size_t maxsize,
                              svn_boolean_t delete_on_close,
                              svn_boolean_t spill_all_contents,
                              const char *dirpath,
                              apr_pool_t *result_pool)
{
  svn_spillbuf_t *buf = apr_pcalloc(result_pool, sizeof(*buf));
  init_spillbuf_extended(buf, blocksize, maxsize,
                         delete_on_close, spill_all_contents, dirpath,
                         result_pool);
  return buf;
}

svn_filesize_t
svn_spillbuf__get_size(const svn_spillbuf_t *buf)
{
  return buf->memory_size + buf->spill_size;
}

svn_filesize_t
svn_spillbuf__get_memory_size(const svn_spillbuf_t *buf)
{
  return buf->memory_size;
}

const char *
svn_spillbuf__get_filename(const svn_spillbuf_t *buf)
{
  return buf->filename;
}

apr_file_t *
svn_spillbuf__get_file(const svn_spillbuf_t *buf)
{
  return buf->spill;
}

/* Get a memblock from the spill-buffer. It will be the block that we
   passed out for reading, come from the free list, or allocated.  */
static struct memblock_t *
get_buffer(svn_spillbuf_t *buf)
{
  struct memblock_t *mem = buf->out_for_reading;

  if (mem != NULL)
    {
      buf->out_for_reading = NULL;
      return mem;
    }

  if (buf->avail == NULL)
    {
      mem = apr_palloc(buf->pool, sizeof(*mem));
      mem->data = apr_palloc(buf->pool, buf->blocksize);
      return mem;
    }

  mem = buf->avail;
  buf->avail = mem->next;
  return mem;
}


/* Return MEM to the list of available buffers in BUF.  */
static void
return_buffer(svn_spillbuf_t *buf,
              struct memblock_t *mem)
{
  mem->next = buf->avail;
  buf->avail = mem;
}


svn_error_t *
svn_spillbuf__write(svn_spillbuf_t *buf,
                    const char *data,
                    apr_size_t len,
                    apr_pool_t *scratch_pool)
{
  struct memblock_t *mem;

  /* We do not (yet) have a spill file, but the amount stored in memory
     will grow too large. Create the file and place the pending data into
     the temporary file.  */
  if (buf->spill == NULL
      && ((buf->maxsize - buf->memory_size) < len))
    {
      SVN_ERR(svn_io_open_unique_file3(&buf->spill,
                                       &buf->filename,
                                       buf->dirpath,
                                       (buf->delete_on_close
                                        ? svn_io_file_del_on_close
                                        : svn_io_file_del_none),
                                       buf->pool, scratch_pool));

      /* Optionally write the memory contents into the file. */
      if (buf->spill_all_contents)
        {
          mem = buf->head;
          while (mem != NULL)
            {
              SVN_ERR(svn_io_file_write_full(buf->spill, mem->data, mem->size,
                                             NULL, scratch_pool));
              mem = mem->next;
            }

          /* Adjust the start offset for reading from the spill file.

             This way, the first `buf->memory_size` bytes of data will
             be read from the existing in-memory buffers, which makes
             more sense than discarding the buffers and re-reading
             data from the file. */
          buf->spill_start = buf->memory_size;
        }
    }

  /* Once a spill file has been constructed, then we need to put all
     arriving data into the file. We will no longer attempt to hold it
     in memory.  */
  if (buf->spill != NULL)
    {
      apr_off_t output_unused = 0;  /* ### stupid API  */

      /* Seek to the end of the spill file. We don't know if a read has
         occurred since our last write, and moved the file position.  */
      SVN_ERR(svn_io_file_seek(buf->spill,
                               APR_END, &output_unused,
                               scratch_pool));

      SVN_ERR(svn_io_file_write_full(buf->spill, data, len,
                                     NULL, scratch_pool));
      buf->spill_size += len;

      return SVN_NO_ERROR;
    }

  while (len > 0)
    {
      apr_size_t amt;

      if (buf->tail == NULL || buf->tail->size == buf->blocksize)
        {
          /* There is no existing memblock (that may have space), or the
             tail memblock has no space, so we need a new memblock.  */
          mem = get_buffer(buf);
          mem->size = 0;
          mem->next = NULL;
        }
      else
        {
          mem = buf->tail;
        }

      /* Compute how much to write into the memblock.  */
      amt = buf->blocksize - mem->size;
      if (amt > len)
        amt = len;

      /* Copy some data into this memblock.  */
      memcpy(&mem->data[mem->size], data, amt);
      mem->size += amt;
      data += amt;
      len -= amt;

      /* We need to record how much is buffered in memory. Once we reach
         buf->maxsize (or thereabouts, it doesn't have to be precise), then
         we'll switch to putting the content into a file.  */
      buf->memory_size += amt;

      /* Start a list of buffers, or (if we're not writing into the tail)
         append to the end of the linked list of buffers.  */
      if (buf->tail == NULL)
        {
          buf->head = mem;
          buf->tail = mem;
        }
      else if (mem != buf->tail)
        {
          buf->tail->next = mem;
          buf->tail = mem;
        }
    }

  return SVN_NO_ERROR;
}


/* Return a memblock of content, if any is available. *mem will be NULL if
   no further content is available. The memblock should eventually be
   passed to return_buffer() (or stored into buf->out_for_reading which
   will grab that block at the next get_buffer() call). */
static svn_error_t *
read_data(struct memblock_t **mem,
          svn_spillbuf_t *buf,
          apr_pool_t *scratch_pool)
{
  svn_error_t *err;

  /* If we have some in-memory blocks, then return one.  */
  if (buf->head != NULL)
    {
      *mem = buf->head;
      if (buf->tail == *mem)
        buf->head = buf->tail = NULL;
      else
        buf->head = (*mem)->next;

      /* We're using less memory now. If we haven't hit the spill file,
         then we may be able to keep using memory.  */
      buf->memory_size -= (*mem)->size;

      return SVN_NO_ERROR;
    }

  /* No file? Done.  */
  if (buf->spill == NULL)
    {
      *mem = NULL;
      return SVN_NO_ERROR;
    }

  /* Assume that the caller has seeked the spill file to the correct pos.  */

  /* Get a buffer that we can read content into.  */
  *mem = get_buffer(buf);
  /* NOTE: mem's size/next are uninitialized.  */

  if ((apr_uint64_t)buf->spill_size < (apr_uint64_t)buf->blocksize)
    (*mem)->size = (apr_size_t)buf->spill_size;
  else
    (*mem)->size = buf->blocksize;  /* The size of (*mem)->data  */
  (*mem)->next = NULL;

  /* Read some data from the spill file into the memblock.  */
  err = svn_io_file_read(buf->spill, (*mem)->data, &(*mem)->size,
                         scratch_pool);
  if (err)
    {
      return_buffer(buf, *mem);
      return svn_error_trace(err);
    }

  /* Mark the data that we consumed from the spill file.  */
  buf->spill_start += (*mem)->size;

  /* Did we consume all the data from the spill file?  */
  if ((buf->spill_size -= (*mem)->size) == 0)
    {
      /* Close and reset our spill file information.  */
      SVN_ERR(svn_io_file_close(buf->spill, scratch_pool));
      buf->spill = NULL;
      buf->spill_start = 0;
    }

  /* *mem has been initialized. Done.  */
  return SVN_NO_ERROR;
}


/* If the next read would consume data from the file, then seek to the
   correct position.  */
static svn_error_t *
maybe_seek(svn_boolean_t *seeked,
           const svn_spillbuf_t *buf,
           apr_pool_t *scratch_pool)
{
  if (buf->head == NULL && buf->spill != NULL)
    {
      apr_off_t output_unused;

      /* Seek to where we left off reading.  */
      output_unused = buf->spill_start;  /* ### stupid API  */
      SVN_ERR(svn_io_file_seek(buf->spill,
                               APR_SET, &output_unused,
                               scratch_pool));
      if (seeked != NULL)
        *seeked = TRUE;
    }
  else if (seeked != NULL)
    {
      *seeked = FALSE;
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_spillbuf__read(const char **data,
                   apr_size_t *len,
                   svn_spillbuf_t *buf,
                   apr_pool_t *scratch_pool)
{
  struct memblock_t *mem;

  /* Possibly seek... */
  SVN_ERR(maybe_seek(NULL, buf, scratch_pool));

  SVN_ERR(read_data(&mem, buf, scratch_pool));
  if (mem == NULL)
    {
      *data = NULL;
      *len = 0;
    }
  else
    {
      *data = mem->data;
      *len = mem->size;

      /* If a block was out for reading, then return it.  */
      if (buf->out_for_reading != NULL)
        return_buffer(buf, buf->out_for_reading);

      /* Remember that we've passed this block out for reading.  */
      buf->out_for_reading = mem;
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_spillbuf__process(svn_boolean_t *exhausted,
                      svn_spillbuf_t *buf,
                      svn_spillbuf_read_t read_func,
                      void *read_baton,
                      apr_pool_t *scratch_pool)
{
  svn_boolean_t has_seeked = FALSE;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  *exhausted = FALSE;

  while (TRUE)
    {
      struct memblock_t *mem;
      svn_error_t *err;
      svn_boolean_t stop;

      svn_pool_clear(iterpool);

      /* If this call to read_data() will read from the spill file, and we
         have not seek'd the file... then do it now.  */
      if (!has_seeked)
        SVN_ERR(maybe_seek(&has_seeked, buf, iterpool));

      /* Get some content to pass to the read callback.  */
      SVN_ERR(read_data(&mem, buf, iterpool));
      if (mem == NULL)
        {
          *exhausted = TRUE;
          break;
        }

      err = read_func(&stop, read_baton, mem->data, mem->size, iterpool);

      return_buffer(buf, mem);

      if (err)
        return svn_error_trace(err);

      /* If the callbacks told us to stop, then we're done for now.  */
      if (stop)
        break;
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}


svn_spillbuf_reader_t *
svn_spillbuf__reader_create(apr_size_t blocksize,
                            apr_size_t maxsize,
                            apr_pool_t *result_pool)
{
  svn_spillbuf_reader_t *sbr = apr_pcalloc(result_pool, sizeof(*sbr));
  sbr->buf = svn_spillbuf__create(blocksize, maxsize, result_pool);
  return sbr;
}

svn_error_t *
svn_spillbuf__reader_read(apr_size_t *amt,
                          svn_spillbuf_reader_t *reader,
                          char *data,
                          apr_size_t len,
                          apr_pool_t *scratch_pool)
{
  if (len == 0)
    return svn_error_create(SVN_ERR_INCORRECT_PARAMS, NULL, NULL);

  *amt = 0;

  while (len > 0)
    {
      apr_size_t copy_amt;

      if (reader->save_len > 0)
        {
          /* We have some saved content, so use this first.  */

          if (len < reader->save_len)
            copy_amt = len;
          else
            copy_amt = reader->save_len;

          memcpy(data, reader->save_ptr + reader->save_pos, copy_amt);
          reader->save_pos += copy_amt;
          reader->save_len -= copy_amt;
        }
      else
        {
          /* No saved content. We should now copy from spillbuf-provided
             buffers of content.  */

          /* We may need more content from the spillbuf.  */
          if (reader->sb_len == 0)
            {
              SVN_ERR(svn_spillbuf__read(&reader->sb_ptr, &reader->sb_len,
                                         reader->buf,
                                         scratch_pool));

              /* We've run out of content, so return with whatever has
                 been copied into DATA and stored into AMT.  */
              if (reader->sb_ptr == NULL)
                {
                  /* For safety, read() may not have set SB_LEN. We use it
                     as an indicator, so it needs to be cleared.  */
                  reader->sb_len = 0;
                  return SVN_NO_ERROR;
                }
            }

          if (len < reader->sb_len)
            copy_amt = len;
          else
            copy_amt = reader->sb_len;

          memcpy(data, reader->sb_ptr, copy_amt);
          reader->sb_ptr += copy_amt;
          reader->sb_len -= copy_amt;
        }

      data += copy_amt;
      len -= copy_amt;
      (*amt) += copy_amt;
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_spillbuf__reader_getc(char *c,
                          svn_spillbuf_reader_t *reader,
                          apr_pool_t *scratch_pool)
{
  apr_size_t amt;

  SVN_ERR(svn_spillbuf__reader_read(&amt, reader, c, 1, scratch_pool));
  if (amt == 0)
    return svn_error_create(SVN_ERR_STREAM_UNEXPECTED_EOF, NULL, NULL);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_spillbuf__reader_write(svn_spillbuf_reader_t *reader,
                           const char *data,
                           apr_size_t len,
                           apr_pool_t *scratch_pool)
{
  /* If we have a buffer of content from the spillbuf, then we need to
     move that content to a safe place.  */
  if (reader->sb_len > 0)
    {
      if (reader->save_ptr == NULL)
        reader->save_ptr = apr_palloc(reader->buf->pool,
                                      reader->buf->blocksize);

      memcpy(reader->save_ptr, reader->sb_ptr, reader->sb_len);
      reader->save_len = reader->sb_len;
      reader->save_pos = 0;

      /* No more content in the spillbuf-borrowed buffer.  */
      reader->sb_len = 0;
    }

  return svn_error_trace(svn_spillbuf__write(reader->buf, data, len,
                                             scratch_pool));
}


struct spillbuf_baton
{
  svn_spillbuf_reader_t *reader;
  apr_pool_t *scratch_pool;
};


static svn_error_t *
read_handler_spillbuf(void *baton, char *buffer, apr_size_t *len)
{
  struct spillbuf_baton *sb = baton;

  SVN_ERR(svn_spillbuf__reader_read(len, sb->reader, buffer, *len,
                                    sb->scratch_pool));

  svn_pool_clear(sb->scratch_pool);
  return SVN_NO_ERROR;
}


static svn_error_t *
write_handler_spillbuf(void *baton, const char *data, apr_size_t *len)
{
  struct spillbuf_baton *sb = baton;

  SVN_ERR(svn_spillbuf__reader_write(sb->reader, data, *len,
                                     sb->scratch_pool));

  svn_pool_clear(sb->scratch_pool);
  return SVN_NO_ERROR;
}


svn_stream_t *
svn_stream__from_spillbuf(svn_spillbuf_t *buf,
                          apr_pool_t *result_pool)
{
  svn_stream_t *stream;
  struct spillbuf_baton *sb = apr_palloc(result_pool, sizeof(*sb));

  sb->reader = apr_pcalloc(result_pool, sizeof(*sb->reader));
  sb->reader->buf = buf;
  sb->scratch_pool = svn_pool_create(result_pool);

  stream = svn_stream_create(sb, result_pool);

  svn_stream_set_read2(stream, NULL /* only full read support */,
                       read_handler_spillbuf);
  svn_stream_set_write(stream, write_handler_spillbuf);

  return stream;
}

/*
 * config_file.c :  parsing configuration files
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



#include <apr_lib.h>
#include <apr_env.h>
#include <apr_tables.h>

#include "config_impl.h"
#include "svn_io.h"
#include "svn_types.h"
#include "svn_dirent_uri.h"
#include "svn_auth.h"
#include "svn_hash.h"
#include "svn_subst.h"
#include "svn_utf.h"
#include "svn_pools.h"
#include "svn_user.h"
#include "svn_ctype.h"

#include "private/svn_config_private.h"
#include "private/svn_subr_private.h"
#include "svn_private_config.h"

#ifdef __HAIKU__
#  include <FindDirectory.h>
#  include <StorageDefs.h>
#endif

/* Used to terminate lines in large multi-line string literals. */
#define NL APR_EOL_STR


/* File parsing context */
typedef struct parse_context_t
{
  /* The configuration constructor. */
  svn_config__constructor_t *constructor;
  void *constructor_baton;

  /* The stream struct */
  svn_stream_t *stream;

  /* The current line in the file */
  int line;

  /* Emulate an ungetc */
  int ungotten_char;

  /* We're currently parsing a section */
  svn_boolean_t in_section;

  /* Temporary strings */
  svn_stringbuf_t *section;
  svn_stringbuf_t *option;
  svn_stringbuf_t *value;
  svn_stringbuf_t *line_read;

  /* Parser buffer for getc() to avoid call overhead into several libraries
     for every character */
  char parser_buffer[SVN__STREAM_CHUNK_SIZE]; /* Larger than most config files */
  size_t buffer_pos; /* Current position within parser_buffer */
  size_t buffer_size; /* parser_buffer contains this many bytes */

  /* Non-zero if we hit EOF on the stream. */
  svn_boolean_t hit_stream_eof;
} parse_context_t;


/* Config representation constructor */
struct svn_config__constructor_t
{
  /* Constructor callbacks; see docs for svn_config__constructor_create. */
  svn_config__open_section_fn open_section;
  svn_config__close_section_fn close_section;
  svn_config__add_value_fn add_value;
};

svn_config__constructor_t *
svn_config__constructor_create(
    svn_config__open_section_fn open_section_callback,
    svn_config__close_section_fn close_section_callback,
    svn_config__add_value_fn add_value_callback,
    apr_pool_t *result_pool)
{
  svn_config__constructor_t *ctor = apr_palloc(result_pool, sizeof(*ctor));
  ctor->open_section = open_section_callback;
  ctor->close_section = close_section_callback;
  ctor->add_value = add_value_callback;
  return ctor;
}

/* Called after we've parsed a section name and before we start
   parsing any options within that section. */
static APR_INLINE svn_error_t *
open_section(parse_context_t *ctx, svn_boolean_t *stop)
{
  if (ctx->constructor->open_section)
    {
      svn_error_t *err = ctx->constructor->open_section(
          ctx->constructor_baton, ctx->section);
      if (err)
        {
          if (err->apr_err == SVN_ERR_CEASE_INVOCATION)
            {
              *stop = TRUE;
              svn_error_clear(err);
              return SVN_NO_ERROR;
            }
          else
            return svn_error_trace(err);
        }
    }

  *stop = FALSE;
  ctx->in_section = TRUE;
  return SVN_NO_ERROR;
}

/* Called after we've parsed all options within a section and before
   we start parsing the next section. */
static APR_INLINE svn_error_t *
close_section(parse_context_t *ctx, svn_boolean_t *stop)
{
  ctx->in_section = FALSE;
  if (ctx->constructor->close_section)
    {
      svn_error_t *err = ctx->constructor->close_section(
          ctx->constructor_baton, ctx->section);
      if (err)
        {
          if (err->apr_err == SVN_ERR_CEASE_INVOCATION)
            {
              *stop = TRUE;
              svn_error_clear(err);
              return SVN_NO_ERROR;
            }
          else
            return svn_error_trace(err);
        }
    }

  *stop = FALSE;
  return SVN_NO_ERROR;
}

/* Called every tyme we've parsed a complete (option, value) pair. */
static APR_INLINE svn_error_t *
add_value(parse_context_t *ctx, svn_boolean_t *stop)
{
  if (ctx->constructor->add_value)
    {
      svn_error_t *err =  ctx->constructor->add_value(
          ctx->constructor_baton, ctx->section,
          ctx->option, ctx->value);
      if (err)
        {
          if (err->apr_err == SVN_ERR_CEASE_INVOCATION)
            {
              *stop = TRUE;
              svn_error_clear(err);
              return SVN_NO_ERROR;
            }
          else
            return svn_error_trace(err);
        }
    }

  *stop = FALSE;
  return SVN_NO_ERROR;
}


/* Emulate getc() because streams don't support it.
 *
 * In order to be able to ungetc(), use the CXT instead of the stream
 * to be able to store the 'ungotton' character.
 *
 */
static APR_INLINE svn_error_t *
parser_getc(parse_context_t *ctx, int *c)
{
  do
    {
      if (ctx->ungotten_char != EOF)
        {
          *c = ctx->ungotten_char;
          ctx->ungotten_char = EOF;
        }
      else if (ctx->buffer_pos < ctx->buffer_size)
        {
          *c = (unsigned char)ctx->parser_buffer[ctx->buffer_pos];
          ctx->buffer_pos++;
        }
      else
        {
          if (!ctx->hit_stream_eof)
            {
              ctx->buffer_pos = 0;
              ctx->buffer_size = sizeof(ctx->parser_buffer);

              SVN_ERR(svn_stream_read_full(ctx->stream, ctx->parser_buffer,
                                           &(ctx->buffer_size)));
              ctx->hit_stream_eof = (ctx->buffer_size != sizeof(ctx->parser_buffer));
            }

          if (ctx->buffer_pos < ctx->buffer_size)
            {
              *c = (unsigned char)ctx->parser_buffer[ctx->buffer_pos];
              ctx->buffer_pos++;
            }
          else
            *c = EOF;
        }
    }
  while (*c == '\r');

  return SVN_NO_ERROR;
}

/* Simplified version of parser_getc() to be used inside skipping loops.
 * It will not check for 'ungotton' chars and may or may not ignore '\r'.
 *
 * In a 'while(cond) getc();' loop, the first iteration must call
 * parser_getc to handle all the special cases.  Later iterations should
 * use parser_getc_plain for maximum performance.
 */
static APR_INLINE svn_error_t *
parser_getc_plain(parse_context_t *ctx, int *c)
{
  if (ctx->buffer_pos < ctx->buffer_size)
    {
      *c = (unsigned char)ctx->parser_buffer[ctx->buffer_pos];
      ctx->buffer_pos++;

      return SVN_NO_ERROR;
    }

  return parser_getc(ctx, c);
}

/* Emulate ungetc() because streams don't support it.
 *
 * Use CTX to store the ungotten character C.
 */
static APR_INLINE svn_error_t *
parser_ungetc(parse_context_t *ctx, int c)
{
  ctx->ungotten_char = c;

  return SVN_NO_ERROR;
}

/* Read from CTX to the end of the line (or file) and write the data
   into LINE.  Previous contents of *LINE will be overwritten.
   Returns the char that ended the line in *C; the char is either
   EOF or newline. */
static svn_error_t *
parser_get_line(parse_context_t *ctx, svn_stringbuf_t *line, int *c)
{
  int ch;
  svn_stringbuf_setempty(line);

  /* Loop until EOF of newline. There is one extra iteration per
   * parser buffer refill.  The initial parser_getc() also ensures
   * that the unget buffer is emptied. */
  SVN_ERR(parser_getc(ctx, &ch));
  while (ch != '\n' && ch != EOF)
    {
      const char *start, *newline;

      /* Save the char we just read. */
      svn_stringbuf_appendbyte(line, ch);

      /* Scan the parser buffer for NL. */
      start = ctx->parser_buffer + ctx->buffer_pos;
      newline = memchr(start, '\n', ctx->buffer_size - ctx->buffer_pos);
      if (newline)
        {
          /* NL found before the end of the of the buffer. */
          svn_stringbuf_appendbytes(line, start, newline - start);
          ch = '\n';
          ctx->buffer_pos = newline - ctx->parser_buffer + 1;
          break;
        }
      else
        {
          /* Hit the end of the buffer.  This may be either due to
           * buffer size or EOF.  In any case, all (remaining) current
           * buffer data is part of the line to read. */
          const char *end = ctx->parser_buffer + ctx->buffer_size;
          ctx->buffer_pos = ctx->buffer_size;

          svn_stringbuf_appendbytes(line, start, end - start);
        }

      /* refill buffer, check for EOF */
      SVN_ERR(parser_getc_plain(ctx, &ch));
    }

  *c = ch;
  return SVN_NO_ERROR;
}

/* Eat chars from STREAM until encounter non-whitespace, newline, or EOF.
   Set *PCOUNT to the number of characters eaten, not counting the
   last one, and return the last char read (the one that caused the
   break).  */
static APR_INLINE svn_error_t *
skip_whitespace(parse_context_t *ctx, int *c, int *pcount)
{
  int ch = 0;
  int count = 0;

  SVN_ERR(parser_getc(ctx, &ch));
  while (svn_ctype_isspace(ch) && ch != '\n' && ch != EOF)
    {
      ++count;
      SVN_ERR(parser_getc_plain(ctx, &ch));
    }
  *pcount = count;
  *c = ch;
  return SVN_NO_ERROR;
}


/* Skip to the end of the line (or file).  Returns the char that ended
   the line; the char is either EOF or newline. */
static APR_INLINE svn_error_t *
skip_to_eoln(parse_context_t *ctx, int *c)
{
  int ch;

  SVN_ERR(parser_getc(ctx, &ch));
  while (ch != '\n' && ch != EOF)
    {
      /* This is much faster than checking individual bytes.
       * We use this function a lot when skipping comment lines.
       *
       * This assumes that the ungetc buffer is empty, but that is a
       * safe assumption right after reading a character (which would
       * clear the buffer. */
      const char *newline = memchr(ctx->parser_buffer + ctx->buffer_pos, '\n',
                                   ctx->buffer_size - ctx->buffer_pos);
      if (newline)
        {
          ch = '\n';
          ctx->buffer_pos = newline - ctx->parser_buffer + 1;
          break;
        }

      /* refill buffer, check for EOF */
      SVN_ERR(parser_getc_plain(ctx, &ch));
    }

  *c = ch;
  return SVN_NO_ERROR;
}

/* Skip a UTF-8 Byte Order Mark if found. */
static APR_INLINE svn_error_t *
skip_bom(parse_context_t *ctx)
{
  int ch;

  SVN_ERR(parser_getc(ctx, &ch));
  if (ch == 0xEF)
    {
      const unsigned char *buf = (unsigned char *)ctx->parser_buffer;
      /* This makes assumptions about the implementation of parser_getc and
       * the use of skip_bom.  Specifically that parser_getc() will get all
       * of the BOM characters into the parse_context_t buffer.  This can
       * safely be assumed as long as we only try to use skip_bom() at the
       * start of the stream and the buffer is longer than 3 characters. */
      SVN_ERR_ASSERT(ctx->buffer_size > ctx->buffer_pos + 1 ||
                     ctx->hit_stream_eof);
      if (ctx->buffer_size > ctx->buffer_pos + 1 &&
          buf[ctx->buffer_pos] == 0xBB && buf[ctx->buffer_pos + 1] == 0xBF)
        ctx->buffer_pos += 2;
      else
        SVN_ERR(parser_ungetc(ctx, ch));
    }
  else
    SVN_ERR(parser_ungetc(ctx, ch));

  return SVN_NO_ERROR;
}

/* Parse continuation lines of single option value if they exist.  Append
 * their contents, including *PCH, to CTX->VALUE.  Return the first char
 * of the next line or EOF in *PCH. */
static svn_error_t *
parse_value_continuation_lines(int *pch, parse_context_t *ctx)
{
  svn_boolean_t end_of_val = FALSE;
  svn_boolean_t stop;
  int ch = *pch;

  /* Leading and trailing whitespace is ignored. */
  svn_stringbuf_strip_whitespace(ctx->value);

  /* Look for any continuation lines. */
  for (;;)
    {

      if (ch == EOF || end_of_val)
        {
          /* At end of file. The value is complete, there can't be
             any continuation lines. */
          SVN_ERR(add_value(ctx, &stop));
          if (stop)
            return SVN_NO_ERROR;
          break;
        }
      else
        {
          int count;
          ++ctx->line;
          SVN_ERR(skip_whitespace(ctx, &ch, &count));

          switch (ch)
            {
            case '\n':
              /* The next line was empty. Ergo, it can't be a
                 continuation line. */
              ++ctx->line;
              end_of_val = TRUE;
              continue;

            case EOF:
              /* This is also an empty line. */
              end_of_val = TRUE;
              continue;

            default:
              if (count == 0)
                {
                  /* This line starts in the first column.  That means
                     it's either a section, option or comment.  Put
                     the char back into the stream, because it doesn't
                     belong to us. */
                  SVN_ERR(parser_ungetc(ctx, ch));
                  end_of_val = TRUE;
                }
              else
                {
                  /* This is a continuation line. Read it. */
                  SVN_ERR(parser_ungetc(ctx, ch));
                  SVN_ERR(parser_get_line(ctx, ctx->line_read, &ch));

                  /* Trailing whitespace is ignored. */
                  svn_stringbuf_strip_whitespace(ctx->line_read);

                  svn_stringbuf_appendbyte(ctx->value, ' ');
                  svn_stringbuf_appendbytes(ctx->value, ctx->line_read->data,
                                            ctx->line_read->len);
                }
            }
        }
    }

  *pch = ch;
  return SVN_NO_ERROR;
}


/* Parse a single option */
static svn_error_t *
parse_option(int *pch, parse_context_t *ctx, apr_pool_t *scratch_pool)
{
  svn_error_t *err = SVN_NO_ERROR;
  int ch;
  char *equals, *separator;

  /* Read the first line. */
  parser_ungetc(ctx, *pch); /* Yes, the first char is relevant. */
  SVN_ERR(parser_get_line(ctx, ctx->line_read, &ch));

  /* Extract the option name from it. */
  equals = strchr(ctx->line_read->data, '=');
  if (equals)
    {
      /* Efficiently look for a colon separator prior to the equals sign.
       * Since there is no strnchr, we limit the search by temporarily
       * limiting the string. */
      char *colon;
      *equals = 0;
      colon = strchr(ctx->line_read->data, ':');
      *equals = '=';

      separator = colon ? colon : equals;
    }
  else
    {
      /* No '=' separator so far.  Look for colon. */
      separator = strchr(ctx->line_read->data, ':');
    }

  if (!separator)
    {
      ch = EOF;
      err = svn_error_createf(SVN_ERR_MALFORMED_FILE, NULL,
                              _("line %d: Option must end with ':' or '='"),
                              ctx->line);
    }
  else
    {
      /* Whitespace around the name separator is ignored. */
      const char *end = separator;
      while (svn_ctype_isspace(*--end))
        ;
      while (svn_ctype_isspace(*++separator))
        ;

      /* Extract the option.  It can't contain whitespace chars. */
      svn_stringbuf_setempty(ctx->option);
      svn_stringbuf_appendbytes(ctx->option, ctx->line_read->data,
                                end + 1 - ctx->line_read->data);

      /* Extract the first line of the value. */
      end = ctx->line_read->data + ctx->line_read->len;
      svn_stringbuf_setempty(ctx->value);
      svn_stringbuf_appendbytes(ctx->value, separator, end - separator);
      err = parse_value_continuation_lines(&ch, ctx);
    }

  *pch = ch;
  return err;
}


/* Read chars until enounter ']', then skip everything to the end of
 * the line.  Set *PCH to the character that ended the line (either
 * newline or EOF), and set CTX->section to the string of characters
 * seen before ']'.
 *
 * This is meant to be called immediately after reading the '[' that
 * starts a section name.
 */
static svn_error_t *
parse_section_name(int *pch, parse_context_t *ctx,
                   apr_pool_t *scratch_pool)
{
  svn_error_t *err = SVN_NO_ERROR;
  int ch;
  char *terminal;

  SVN_ERR(parser_get_line(ctx, ctx->section, &ch));
  terminal = strchr(ctx->section->data, ']');

  if (!terminal)
    {
      ch = EOF;
      err = svn_error_createf(SVN_ERR_MALFORMED_FILE, NULL,
                              _("line %d: Section header must end with ']'"),
                              ctx->line);
    }
  else
    {
      /* Everything from the ']' to the end of the line is ignored. */
      *terminal = 0;
      ctx->section->len = terminal - ctx->section->data;
    }

  *pch = ch;
  return err;
}


svn_error_t *
svn_config__sys_config_path(const char **path_p,
                            const char *fname,
                            apr_pool_t *pool)
{
  *path_p = NULL;

  /* Note that even if fname is null, svn_dirent_join_many will DTRT. */

#ifdef WIN32
  {
    const char *folder;
    SVN_ERR(svn_config__win_config_path(&folder, TRUE, pool, pool));
    *path_p = svn_dirent_join_many(pool, folder,
                                   SVN_CONFIG__SUBDIRECTORY, fname,
                                   SVN_VA_NULL);
  }

#elif defined(__HAIKU__)
  {
    char folder[B_PATH_NAME_LENGTH];

    status_t error = find_directory(B_COMMON_SETTINGS_DIRECTORY, -1, false,
                                    folder, sizeof(folder));
    if (error)
      return SVN_NO_ERROR;

    *path_p = svn_dirent_join_many(pool, folder,
                                   SVN_CONFIG__SYS_DIRECTORY, fname,
                                   SVN_VA_NULL);
  }
#else  /* ! WIN32 && !__HAIKU__ */

  *path_p = svn_dirent_join_many(pool, SVN_CONFIG__SYS_DIRECTORY, fname,
                                 SVN_VA_NULL);

#endif /* WIN32 */

  return SVN_NO_ERROR;
}

/* Callback for svn_config_enumerate2: Continue to next value. */
static svn_boolean_t
expand_value(const char *name,
             const char *value,
             void *baton,
             apr_pool_t *pool)
{
  return TRUE;
}

/* Callback for svn_config_enumerate_sections2:
 * Enumerate and implicitly expand all values in this section.
 */
static svn_boolean_t
expand_values_in_section(const char *name,
                         void *baton,
                         apr_pool_t *pool)
{
  svn_config_t *cfg = baton;
  svn_config_enumerate2(cfg, name, expand_value, NULL, pool);

  return TRUE;
}


/*** Exported interfaces. ***/

void
svn_config__set_read_only(svn_config_t *cfg,
                          apr_pool_t *scratch_pool)
{
  /* expand all items such that later calls to getters won't need to
   * change internal state */
  svn_config_enumerate_sections2(cfg, expand_values_in_section,
                                 cfg, scratch_pool);

  /* now, any modification attempt will be ignored / trigger an assertion
   * in debug mode */
  cfg->read_only = TRUE;
}

svn_boolean_t
svn_config__is_read_only(svn_config_t *cfg)
{
  return cfg->read_only;
}

svn_config_t *
svn_config__shallow_copy(svn_config_t *src,
                         apr_pool_t *pool)
{
  svn_config_t *cfg = apr_palloc(pool, sizeof(*cfg));

  cfg->sections = src->sections;
  cfg->pool = pool;

  /* r/o configs are fully expanded and don't need the x_pool anymore */
  cfg->x_pool = src->read_only ? NULL : svn_pool_create(pool);
  cfg->x_values = src->x_values;
  cfg->tmp_key = svn_stringbuf_create_empty(pool);
  cfg->tmp_value = svn_stringbuf_create_empty(pool);
  cfg->section_names_case_sensitive = src->section_names_case_sensitive;
  cfg->option_names_case_sensitive = src->option_names_case_sensitive;
  cfg->read_only = src->read_only;

  return cfg;
}

void
svn_config__shallow_replace_section(svn_config_t *target,
                                    svn_config_t *source,
                                    const char *section)
{
  if (target->read_only)
    target->sections = apr_hash_copy(target->pool, target->sections);

  svn_hash_sets(target->sections, section,
                svn_hash_gets(source->sections, section));
}



svn_error_t *
svn_config__parse_file(svn_config_t *cfg, const char *file,
                       svn_boolean_t must_exist, apr_pool_t *result_pool)
{
  svn_error_t *err = SVN_NO_ERROR;
  apr_file_t *apr_file;
  svn_stream_t *stream;
  apr_pool_t *scratch_pool = svn_pool_create(result_pool);

  /* Use unbuffered IO since we use our own buffering. */
  err = svn_io_file_open(&apr_file, file, APR_READ, APR_OS_DEFAULT,
                         scratch_pool);

  if (! must_exist && err && APR_STATUS_IS_ENOENT(err->apr_err))
    {
      svn_error_clear(err);
      svn_pool_destroy(scratch_pool);
      return SVN_NO_ERROR;
    }
  else
    SVN_ERR(err);

  stream = svn_stream_from_aprfile2(apr_file, FALSE, scratch_pool);
  err = svn_config__parse_stream(stream,
                                 svn_config__constructor_create(
                                     NULL, NULL,
                                     svn_config__default_add_value_fn,
                                     scratch_pool),
                                 cfg, scratch_pool);

  if (err != SVN_NO_ERROR)
    {
      /* Add the filename to the error stack. */
      err = svn_error_createf(err->apr_err, err,
                              _("Error while parsing config file: %s:"),
                              svn_dirent_local_style(file, scratch_pool));
    }

  /* Close the streams (and other cleanup): */
  svn_pool_destroy(scratch_pool);

  return err;
}

svn_error_t *
svn_config__parse_stream(svn_stream_t *stream,
                         svn_config__constructor_t *constructor,
                         void *constructor_baton,
                         apr_pool_t *scratch_pool)
{
  parse_context_t *ctx;
  svn_boolean_t stop;
  int ch, count;

  ctx = apr_palloc(scratch_pool, sizeof(*ctx));

  ctx->constructor = constructor;
  ctx->constructor_baton = constructor_baton;
  ctx->stream = stream;
  ctx->line = 1;
  ctx->ungotten_char = EOF;
  ctx->in_section = FALSE;
  ctx->section = svn_stringbuf_create_empty(scratch_pool);
  ctx->option = svn_stringbuf_create_empty(scratch_pool);
  ctx->value = svn_stringbuf_create_empty(scratch_pool);
  ctx->line_read = svn_stringbuf_create_empty(scratch_pool);
  ctx->buffer_pos = 0;
  ctx->buffer_size = 0;
  ctx->hit_stream_eof = FALSE;

  SVN_ERR(skip_bom(ctx));

  do
    {
      SVN_ERR(skip_whitespace(ctx, &ch, &count));

      switch (ch)
        {
        case '[':               /* Start of section header */
          if (count != 0)
            return svn_error_createf(SVN_ERR_MALFORMED_FILE, NULL,
                                     _("line %d: Section header"
                                       " must start in the first column"),
                                     ctx->line);

          /* Close the previous section before starting a new one. */
          if (ctx->in_section)
            {
              SVN_ERR(close_section(ctx, &stop));
              if (stop)
                return SVN_NO_ERROR;
            }
          SVN_ERR(parse_section_name(&ch, ctx, scratch_pool));
          SVN_ERR(open_section(ctx, &stop));
          if (stop)
            return SVN_NO_ERROR;
          break;

        case '#':               /* Comment */
          if (count == 0)
            {
              SVN_ERR(skip_to_eoln(ctx, &ch));
              ++(ctx->line);
            }
          else
            return svn_error_createf(SVN_ERR_MALFORMED_FILE, NULL,
                                     _("line %d: Comment"
                                       " must start in the first column"),
                                     ctx->line);
          break;

        case '\n':              /* Empty line */
          ++(ctx->line);
          break;

        case EOF:               /* End of file or read error */
          break;

        default:
          if (svn_stringbuf_isempty(ctx->section))
            return svn_error_createf(SVN_ERR_MALFORMED_FILE, NULL,
                                     _("line %d: Section header expected"),
                                     ctx->line);
          else if (count != 0)
            return svn_error_createf(SVN_ERR_MALFORMED_FILE, NULL,
                                     _("line %d: Option expected"),
                                     ctx->line);
          else
            SVN_ERR(parse_option(&ch, ctx, scratch_pool));
          break;
        }
    }
  while (ch != EOF);

  /* Emit the last close-section call to wrap up. */
  if (ctx->in_section)
    SVN_ERR(close_section(ctx, &stop));
  return SVN_NO_ERROR;
}


/* Helper for ensure_auth_dirs: create SUBDIR under AUTH_DIR, iff
   SUBDIR does not already exist, but ignore any errors.  Use POOL for
   temporary allocation. */
static void
ensure_auth_subdir(const char *auth_dir,
                   const char *subdir,
                   apr_pool_t *pool)
{
  svn_error_t *err;
  const char *subdir_full_path;
  svn_node_kind_t kind;

  subdir_full_path = svn_dirent_join(auth_dir, subdir, pool);
  err = svn_io_check_path(subdir_full_path, &kind, pool);
  if (err || kind == svn_node_none)
    {
      svn_error_clear(err);
      svn_error_clear(svn_io_dir_make(subdir_full_path, APR_OS_DEFAULT, pool));
    }
}

/* Helper for svn_config_ensure:  see if ~/.subversion/auth/ and its
   subdirs exist, try to create them, but don't throw errors on
   failure.  PATH is assumed to be a path to the user's private config
   directory. */
static void
ensure_auth_dirs(const char *path,
                 apr_pool_t *pool)
{
  svn_node_kind_t kind;
  const char *auth_dir;
  svn_error_t *err;

  /* Ensure ~/.subversion/auth/ */
  auth_dir = svn_dirent_join(path, SVN_CONFIG__AUTH_SUBDIR, pool);
  err = svn_io_check_path(auth_dir, &kind, pool);
  if (err || kind == svn_node_none)
    {
      svn_error_clear(err);
      /* 'chmod 700' permissions: */
      err = svn_io_dir_make(auth_dir,
                            (APR_UREAD | APR_UWRITE | APR_UEXECUTE),
                            pool);
      if (err)
        {
          /* Don't try making subdirs if we can't make the top-level dir. */
          svn_error_clear(err);
          return;
        }
    }

  /* If a provider exists that wants to store credentials in
     ~/.subversion, a subdirectory for the cred_kind must exist. */
  ensure_auth_subdir(auth_dir, SVN_AUTH_CRED_SIMPLE, pool);
  ensure_auth_subdir(auth_dir, SVN_AUTH_CRED_USERNAME, pool);
  ensure_auth_subdir(auth_dir, SVN_AUTH_CRED_SSL_SERVER_TRUST, pool);
  ensure_auth_subdir(auth_dir, SVN_AUTH_CRED_SSL_CLIENT_CERT_PW, pool);
}


svn_error_t *
svn_config_ensure(const char *config_dir, apr_pool_t *pool)
{
  const char *path;
  svn_node_kind_t kind;
  svn_error_t *err;

  /* Ensure that the user-specific config directory exists.  */
  SVN_ERR(svn_config_get_user_config_path(&path, config_dir, NULL, pool));

  if (! path)
    return SVN_NO_ERROR;

  err = svn_io_check_resolved_path(path, &kind, pool);
  if (err)
    {
      /* Don't throw an error, but don't continue. */
      svn_error_clear(err);
      return SVN_NO_ERROR;
    }

  if (kind == svn_node_none)
    {
      err = svn_io_dir_make(path, APR_OS_DEFAULT, pool);
      if (err)
        {
          /* Don't throw an error, but don't continue. */
          svn_error_clear(err);
          return SVN_NO_ERROR;
        }
    }
  else if (kind == svn_node_file)
    {
      /* Somebody put a file where the config directory should be.
         Wacky.  Let's bail. */
      return SVN_NO_ERROR;
    }

  /* Else, there's a configuration directory. */

  /* If we get errors trying to do things below, just stop and return
     success.  There's no _need_ to init a config directory if
     something's preventing it. */

  /** If non-existent, try to create a number of auth/ subdirectories. */
  ensure_auth_dirs(path, pool);

  /** Ensure that the `README.txt' file exists. **/
  SVN_ERR(svn_config_get_user_config_path
          (&path, config_dir, SVN_CONFIG__USR_README_FILE, pool));

  if (! path)  /* highly unlikely, since a previous call succeeded */
    return SVN_NO_ERROR;

  err = svn_io_check_path(path, &kind, pool);
  if (err)
    {
      svn_error_clear(err);
      return SVN_NO_ERROR;
    }

  if (kind == svn_node_none)
    {
      apr_file_t *f;
      const char *contents =
   "This directory holds run-time configuration information for Subversion"  NL
   "clients.  The configuration files all share the same syntax, but you"    NL
   "should examine a particular file to learn what configuration"            NL
   "directives are valid for that file."                                     NL
   ""                                                                        NL
   "The syntax is standard INI format:"                                      NL
   ""                                                                        NL
   "   - Empty lines, and lines starting with '#', are ignored."             NL
   "     The first significant line in a file must be a section header."     NL
   ""                                                                        NL
   "   - A section starts with a section header, which must start in"        NL
   "     the first column:"                                                  NL
   ""                                                                        NL
   "       [section-name]"                                                   NL
   ""                                                                        NL
   "   - An option, which must always appear within a section, is a pair"    NL
   "     (name, value).  There are two valid forms for defining an"          NL
   "     option, both of which must start in the first column:"              NL
   ""                                                                        NL
   "       name: value"                                                      NL
   "       name = value"                                                     NL
   ""                                                                        NL
   "     Whitespace around the separator (:, =) is optional."                NL
   ""                                                                        NL
   "   - Section and option names are case-insensitive, but case is"         NL
   "     preserved."                                                         NL
   ""                                                                        NL
   "   - An option's value may be broken into several lines.  The value"     NL
   "     continuation lines must start with at least one whitespace."        NL
   "     Trailing whitespace in the previous line, the newline character"    NL
   "     and the leading whitespace in the continuation line is compressed"  NL
   "     into a single space character."                                     NL
   ""                                                                        NL
   "   - All leading and trailing whitespace around a value is trimmed,"     NL
   "     but the whitespace within a value is preserved, with the"           NL
   "     exception of whitespace around line continuations, as"              NL
   "     described above."                                                   NL
   ""                                                                        NL
   "   - When a value is a boolean, any of the following strings are"        NL
   "     recognised as truth values (case does not matter):"                 NL
   ""                                                                        NL
   "       true      false"                                                  NL
   "       yes       no"                                                     NL
   "       on        off"                                                    NL
   "       1         0"                                                      NL
   ""                                                                        NL
   "   - When a value is a list, it is comma-separated.  Again, the"         NL
   "     whitespace around each element of the list is trimmed."             NL
   ""                                                                        NL
   "   - Option values may be expanded within a value by enclosing the"      NL
   "     option name in parentheses, preceded by a percent sign and"         NL
   "     followed by an 's':"                                                NL
   ""                                                                        NL
   "       %(name)s"                                                         NL
   ""                                                                        NL
   "     The expansion is performed recursively and on demand, during"       NL
   "     svn_option_get.  The name is first searched for in the same"        NL
   "     section, then in the special [DEFAULT] section. If the name"        NL
   "     is not found, the whole '%(name)s' placeholder is left"             NL
   "     unchanged."                                                         NL
   ""                                                                        NL
   "     Any modifications to the configuration data invalidate all"         NL
   "     previously expanded values, so that the next svn_option_get"        NL
   "     will take the modifications into account."                          NL
   ""                                                                        NL
   "The syntax of the configuration files is a subset of the one used by"    NL
   "Python's ConfigParser module; see"                                       NL
   ""                                                                        NL
   "   http://www.python.org/doc/current/lib/module-ConfigParser.html"       NL
   ""                                                                        NL
   "Configuration data in the Windows registry"                              NL
   "=========================================="                              NL
   ""                                                                        NL
   "On Windows, configuration data may also be stored in the registry. The"  NL
   "functions svn_config_read and svn_config_merge will read from the"       NL
   "registry when passed file names of the form:"                            NL
   ""                                                                        NL
   "   REGISTRY:<hive>/path/to/config-key"                                   NL
   ""                                                                        NL
   "The REGISTRY: prefix must be in upper case. The <hive> part must be"     NL
   "one of:"                                                                 NL
   ""                                                                        NL
   "   HKLM for HKEY_LOCAL_MACHINE"                                          NL
   "   HKCU for HKEY_CURRENT_USER"                                           NL
   ""                                                                        NL
   "The values in config-key represent the options in the [DEFAULT] section."NL
   "The keys below config-key represent other sections, and their values"    NL
   "represent the options. Only values of type REG_SZ whose name doesn't"    NL
   "start with a '#' will be used; other values, as well as the keys'"       NL
   "default values, will be ignored."                                        NL
   ""                                                                        NL
   ""                                                                        NL
   "File locations"                                                          NL
   "=============="                                                          NL
   ""                                                                        NL
   "Typically, Subversion uses two config directories, one for site-wide"    NL
   "configuration,"                                                          NL
   ""                                                                        NL
   "  Unix:"                                                                 NL
   "    /etc/subversion/servers"                                             NL
   "    /etc/subversion/config"                                              NL
   "    /etc/subversion/hairstyles"                                          NL
   "  Windows:"                                                              NL
   "    %ALLUSERSPROFILE%\\Application Data\\Subversion\\servers"            NL
   "    %ALLUSERSPROFILE%\\Application Data\\Subversion\\config"             NL
   "    %ALLUSERSPROFILE%\\Application Data\\Subversion\\hairstyles"         NL
   "    REGISTRY:HKLM\\Software\\Tigris.org\\Subversion\\Servers"            NL
   "    REGISTRY:HKLM\\Software\\Tigris.org\\Subversion\\Config"             NL
   "    REGISTRY:HKLM\\Software\\Tigris.org\\Subversion\\Hairstyles"         NL
   ""                                                                        NL
   "and one for per-user configuration:"                                     NL
   ""                                                                        NL
   "  Unix:"                                                                 NL
   "    ~/.subversion/servers"                                               NL
   "    ~/.subversion/config"                                                NL
   "    ~/.subversion/hairstyles"                                            NL
   "  Windows:"                                                              NL
   "    %APPDATA%\\Subversion\\servers"                                      NL
   "    %APPDATA%\\Subversion\\config"                                       NL
   "    %APPDATA%\\Subversion\\hairstyles"                                   NL
   "    REGISTRY:HKCU\\Software\\Tigris.org\\Subversion\\Servers"            NL
   "    REGISTRY:HKCU\\Software\\Tigris.org\\Subversion\\Config"             NL
   "    REGISTRY:HKCU\\Software\\Tigris.org\\Subversion\\Hairstyles"         NL
   ""                                                                        NL;

      err = svn_io_file_open(&f, path,
                             (APR_WRITE | APR_CREATE | APR_EXCL),
                             APR_OS_DEFAULT,
                             pool);

      if (! err)
        {
          SVN_ERR(svn_io_file_write_full(f, contents,
                                         strlen(contents), NULL, pool));
          SVN_ERR(svn_io_file_close(f, pool));
        }

      svn_error_clear(err);
    }

  /** Ensure that the `servers' file exists. **/
  SVN_ERR(svn_config_get_user_config_path
          (&path, config_dir, SVN_CONFIG_CATEGORY_SERVERS, pool));

  if (! path)  /* highly unlikely, since a previous call succeeded */
    return SVN_NO_ERROR;

  err = svn_io_check_path(path, &kind, pool);
  if (err)
    {
      svn_error_clear(err);
      return SVN_NO_ERROR;
    }

  if (kind == svn_node_none)
    {
      apr_file_t *f;
      const char *contents =
        "### This file specifies server-specific parameters,"                NL
        "### including HTTP proxy information, HTTP timeout settings,"       NL
        "### and authentication settings."                                   NL
        "###"                                                                NL
        "### The currently defined server options are:"                      NL
        "###   http-proxy-host            Proxy host for HTTP connection"    NL
        "###   http-proxy-port            Port number of proxy host service" NL
        "###   http-proxy-username        Username for auth to proxy service"NL
        "###   http-proxy-password        Password for auth to proxy service"NL
        "###   http-proxy-exceptions      List of sites that do not use proxy"
                                                                             NL
        "###   http-timeout               Timeout for HTTP requests in seconds"
                                                                             NL
        "###   http-compression           Whether to compress HTTP requests" NL
        "###                              (yes/no/auto)."                    NL
        "###   http-max-connections       Maximum number of parallel server" NL
        "###                              connections to use for any given"  NL
        "###                              HTTP operation."                   NL
        "###   http-chunked-requests      Whether to use chunked transfer"   NL
        "###                              encoding for HTTP requests body."  NL
        "###   ssl-authority-files        List of files, each of a trusted CA"
                                                                             NL
        "###   ssl-trust-default-ca       Trust the system 'default' CAs"    NL
        "###   ssl-client-cert-file       PKCS#12 format client certificate file"
                                                                             NL
        "###   ssl-client-cert-password   Client Key password, if needed."   NL
        "###   ssl-pkcs11-provider        Name of PKCS#11 provider to use."  NL
        "###   http-library               Which library to use for http/https"
                                                                             NL
        "###                              connections."                      NL
        "###   http-bulk-updates          Whether to request bulk update"    NL
        "###                              responses or to fetch each file"   NL
        "###                              in an individual request. "        NL
        "###   store-passwords            Specifies whether passwords used"  NL
        "###                              to authenticate against a"         NL
        "###                              Subversion server may be cached"   NL
        "###                              to disk in any way."               NL
#ifndef SVN_DISABLE_PLAINTEXT_PASSWORD_STORAGE
        "###   store-plaintext-passwords  Specifies whether passwords may"   NL
        "###                              be cached on disk unencrypted."    NL
#endif
        "###   store-ssl-client-cert-pp   Specifies whether passphrase used" NL
        "###                              to authenticate against a client"  NL
        "###                              certificate may be cached to disk" NL
        "###                              in any way"                        NL
#ifndef SVN_DISABLE_PLAINTEXT_PASSWORD_STORAGE
        "###   store-ssl-client-cert-pp-plaintext"                           NL
        "###                              Specifies whether client cert"     NL
        "###                              passphrases may be cached on disk" NL
        "###                              unencrypted (i.e., as plaintext)." NL
#endif
        "###   store-auth-creds           Specifies whether any auth info"   NL
        "###                              (passwords, server certs, etc.)"   NL
        "###                              may be cached to disk."            NL
        "###   username                   Specifies the default username."   NL
        "###"                                                                NL
        "### Set store-passwords to 'no' to avoid storing passwords on disk" NL
        "### in any way, including in password stores.  It defaults to"      NL
        "### 'yes', but Subversion will never save your password to disk in" NL
        "### plaintext unless explicitly configured to do so."               NL
        "### Note that this option only prevents saving of *new* passwords;" NL
        "### it doesn't invalidate existing passwords.  (To do that, remove" NL
        "### the cache files by hand as described in the Subversion book.)"  NL
        "###"                                                                NL
#ifndef SVN_DISABLE_PLAINTEXT_PASSWORD_STORAGE
        "### Set store-plaintext-passwords to 'no' to avoid storing"         NL
        "### passwords in unencrypted form in the auth/ area of your config" NL
        "### directory. Set it to 'yes' to allow Subversion to store"        NL
        "### unencrypted passwords in the auth/ area.  The default is"       NL
        "### 'ask', which means that Subversion will ask you before"         NL
        "### saving a password to disk in unencrypted form.  Note that"      NL
        "### this option has no effect if either 'store-passwords' or "      NL
        "### 'store-auth-creds' is set to 'no'."                             NL
        "###"                                                                NL
#endif
        "### Set store-ssl-client-cert-pp to 'no' to avoid storing ssl"      NL
        "### client certificate passphrases in the auth/ area of your"       NL
        "### config directory.  It defaults to 'yes', but Subversion will"   NL
        "### never save your passphrase to disk in plaintext unless"         NL
        "### explicitly configured to do so."                                NL
        "###"                                                                NL
        "### Note store-ssl-client-cert-pp only prevents the saving of *new*"NL
        "### passphrases; it doesn't invalidate existing passphrases.  To do"NL
        "### that, remove the cache files by hand as described in the"       NL
        "### Subversion book at http://svnbook.red-bean.com/nightly/en/\\"   NL
        "###                    svn.serverconfig.netmodel.html\\"            NL
        "###                    #svn.serverconfig.netmodel.credcache"        NL
        "###"                                                                NL
#ifndef SVN_DISABLE_PLAINTEXT_PASSWORD_STORAGE
        "### Set store-ssl-client-cert-pp-plaintext to 'no' to avoid storing"NL
        "### passphrases in unencrypted form in the auth/ area of your"      NL
        "### config directory.  Set it to 'yes' to allow Subversion to"      NL
        "### store unencrypted passphrases in the auth/ area.  The default"  NL
        "### is 'ask', which means that Subversion will prompt before"       NL
        "### saving a passphrase to disk in unencrypted form.  Note that"    NL
        "### this option has no effect if either 'store-auth-creds' or "     NL
        "### 'store-ssl-client-cert-pp' is set to 'no'."                     NL
        "###"                                                                NL
#endif
        "### Set store-auth-creds to 'no' to avoid storing any Subversion"   NL
        "### credentials in the auth/ area of your config directory."        NL
        "### Note that this includes SSL server certificates."               NL
        "### It defaults to 'yes'.  Note that this option only prevents"     NL
        "### saving of *new* credentials;  it doesn't invalidate existing"   NL
        "### caches.  (To do that, remove the cache files by hand.)"         NL
        "###"                                                                NL
        "### HTTP timeouts, if given, are specified in seconds.  A timeout"  NL
        "### of 0, i.e. zero, causes a builtin default to be used."          NL
        "###"                                                                NL
        "### Most users will not need to explicitly set the http-library"    NL
        "### option, but valid values for the option include:"               NL
        "###    'serf': Serf-based module (Subversion 1.5 - present)"        NL
        "### Availability of these modules may depend on your specific"      NL
        "### Subversion distribution."                                       NL
        "###"                                                                NL
        "### The commented-out examples below are intended only to"          NL
        "### demonstrate how to use this file; any resemblance to actual"    NL
        "### servers, living or dead, is entirely coincidental."             NL
        ""                                                                   NL
        "### In the 'groups' section, the URL of the repository you're"      NL
        "### trying to access is matched against the patterns on the right." NL
        "### If a match is found, the server options are taken from the"     NL
        "### section with the corresponding name on the left."               NL
        ""                                                                   NL
        "[groups]"                                                           NL
        "# group1 = *.collab.net"                                            NL
        "# othergroup = repository.blarggitywhoomph.com"                     NL
        "# thirdgroup = *.example.com"                                       NL
        ""                                                                   NL
        "### Information for the first group:"                               NL
        "# [group1]"                                                         NL
        "# http-proxy-host = proxy1.some-domain-name.com"                    NL
        "# http-proxy-port = 80"                                             NL
        "# http-proxy-username = blah"                                       NL
        "# http-proxy-password = doubleblah"                                 NL
        "# http-timeout = 60"                                                NL
#ifndef SVN_DISABLE_PLAINTEXT_PASSWORD_STORAGE
        "# store-plaintext-passwords = no"                                   NL
#endif
        "# username = harry"                                                 NL
        ""                                                                   NL
        "### Information for the second group:"                              NL
        "# [othergroup]"                                                     NL
        "# http-proxy-host = proxy2.some-domain-name.com"                    NL
        "# http-proxy-port = 9000"                                           NL
        "# No username and password for the proxy, so use the defaults below."
                                                                             NL
        ""                                                                   NL
        "### You can set default parameters in the 'global' section."        NL
        "### These parameters apply if no corresponding parameter is set in" NL
        "### a specifically matched group as shown above.  Thus, if you go"  NL
        "### through the same proxy server to reach every site on the"       NL
        "### Internet, you probably just want to put that server's"          NL
        "### information in the 'global' section and not bother with"        NL
        "### 'groups' or any other sections."                                NL
        "###"                                                                NL
        "### Most people might want to configure password caching"           NL
        "### parameters here, but you can also configure them per server"    NL
        "### group (per-group settings override global settings)."           NL
        "###"                                                                NL
        "### If you go through a proxy for all but a few sites, you can"     NL
        "### list those exceptions under 'http-proxy-exceptions'.  This only"NL
        "### overrides defaults, not explicitly matched server names."       NL
        "###"                                                                NL
        "### 'ssl-authority-files' is a semicolon-delimited list of files,"  NL
        "### each pointing to a PEM-encoded Certificate Authority (CA) "     NL
        "### SSL certificate.  See details above for overriding security "   NL
        "### due to SSL."                                                    NL
        "[global]"                                                           NL
        "# http-proxy-exceptions = *.exception.com, www.internal-site.org"   NL
        "# http-proxy-host = defaultproxy.whatever.com"                      NL
        "# http-proxy-port = 7000"                                           NL
        "# http-proxy-username = defaultusername"                            NL
        "# http-proxy-password = defaultpassword"                            NL
        "# http-compression = auto"                                          NL
        "# No http-timeout, so just use the builtin default."                NL
        "# ssl-authority-files = /path/to/CAcert.pem;/path/to/CAcert2.pem"   NL
        "#"                                                                  NL
        "# Password / passphrase caching parameters:"                        NL
        "# store-passwords = no"                                             NL
        "# store-ssl-client-cert-pp = no"                                    NL
#ifndef SVN_DISABLE_PLAINTEXT_PASSWORD_STORAGE
        "# store-plaintext-passwords = no"                                   NL
        "# store-ssl-client-cert-pp-plaintext = no"                          NL
#endif
        ;

      err = svn_io_file_open(&f, path,
                             (APR_WRITE | APR_CREATE | APR_EXCL),
                             APR_OS_DEFAULT,
                             pool);

      if (! err)
        {
          SVN_ERR(svn_io_file_write_full(f, contents,
                                         strlen(contents), NULL, pool));
          SVN_ERR(svn_io_file_close(f, pool));
        }

      svn_error_clear(err);
    }

  /** Ensure that the `config' file exists. **/
  SVN_ERR(svn_config_get_user_config_path
          (&path, config_dir, SVN_CONFIG_CATEGORY_CONFIG, pool));

  if (! path)  /* highly unlikely, since a previous call succeeded */
    return SVN_NO_ERROR;

  err = svn_io_check_path(path, &kind, pool);
  if (err)
    {
      svn_error_clear(err);
      return SVN_NO_ERROR;
    }

  if (kind == svn_node_none)
    {
      apr_file_t *f;
      const char *contents =
        "### This file configures various client-side behaviors."            NL
        "###"                                                                NL
        "### The commented-out examples below are intended to demonstrate"   NL
        "### how to use this file."                                          NL
        ""                                                                   NL
        "### Section for authentication and authorization customizations."   NL
        "[auth]"                                                             NL
        "### Set password stores used by Subversion. They should be"         NL
        "### delimited by spaces or commas. The order of values determines"  NL
        "### the order in which password stores are used."                   NL
        "### Valid password stores:"                                         NL
        "###   gnome-keyring        (Unix-like systems)"                     NL
        "###   kwallet              (Unix-like systems)"                     NL
        "###   gpg-agent            (Unix-like systems)"                     NL
        "###   keychain             (Mac OS X)"                              NL
        "###   windows-cryptoapi    (Windows)"                               NL
#ifdef SVN_HAVE_KEYCHAIN_SERVICES
        "# password-stores = keychain"                                       NL
#elif defined(WIN32) && !defined(__MINGW32__)
        "# password-stores = windows-cryptoapi"                              NL
#else
        "# password-stores = gpg-agent,gnome-keyring,kwallet"                NL
#endif
        "### To disable all password stores, use an empty list:"             NL
        "# password-stores ="                                                NL
#ifdef SVN_HAVE_KWALLET
        "###"                                                                NL
        "### Set KWallet wallet used by Subversion. If empty or unset,"      NL
        "### then the default network wallet will be used."                  NL
        "# kwallet-wallet ="                                                 NL
        "###"                                                                NL
        "### Include PID (Process ID) in Subversion application name when"   NL
        "### using KWallet. It defaults to 'no'."                            NL
        "# kwallet-svn-application-name-with-pid = yes"                      NL
#endif
        "###"                                                                NL
        "### Set ssl-client-cert-file-prompt to 'yes' to cause the client"   NL
        "### to prompt for a path to a client cert file when the server"     NL
        "### requests a client cert but no client cert file is found in the" NL
        "### expected place (see the 'ssl-client-cert-file' option in the"   NL
        "### 'servers' configuration file). Defaults to 'no'."               NL
        "# ssl-client-cert-file-prompt = no"                                 NL
        "###"                                                                NL
        "### The rest of the [auth] section in this file has been deprecated."
                                                                             NL
        "### Both 'store-passwords' and 'store-auth-creds' can now be"       NL
        "### specified in the 'servers' file in your config directory"       NL
        "### and are documented there. Anything specified in this section "  NL
        "### is overridden by settings specified in the 'servers' file."     NL
        "# store-passwords = no"                                             NL
        "# store-auth-creds = no"                                            NL
        ""                                                                   NL
        "### Section for configuring external helper applications."          NL
        "[helpers]"                                                          NL
        "### Set editor-cmd to the command used to invoke your text editor." NL
        "###   This will override the environment variables that Subversion" NL
        "###   examines by default to find this information ($EDITOR, "      NL
        "###   et al)."                                                      NL
        "# editor-cmd = editor (vi, emacs, notepad, etc.)"                   NL
        "### Set diff-cmd to the absolute path of your 'diff' program."      NL
        "###   This will override the compile-time default, which is to use" NL
        "###   Subversion's internal diff implementation."                   NL
        "# diff-cmd = diff_program (diff, gdiff, etc.)"                      NL
        "### Diff-extensions are arguments passed to an external diff"       NL
        "### program or to Subversion's internal diff implementation."       NL
        "### Set diff-extensions to override the default arguments ('-u')."  NL
        "# diff-extensions = -u -p"                                          NL
        "### Set diff3-cmd to the absolute path of your 'diff3' program."    NL
        "###   This will override the compile-time default, which is to use" NL
        "###   Subversion's internal diff3 implementation."                  NL
        "# diff3-cmd = diff3_program (diff3, gdiff3, etc.)"                  NL
        "### Set diff3-has-program-arg to 'yes' if your 'diff3' program"     NL
        "###   accepts the '--diff-program' option."                         NL
        "# diff3-has-program-arg = [yes | no]"                               NL
        "### Set merge-tool-cmd to the command used to invoke your external" NL
        "### merging tool of choice. Subversion will pass 5 arguments to"    NL
        "### the specified command: base theirs mine merged wcfile"          NL
        "# merge-tool-cmd = merge_command"                                   NL
        ""                                                                   NL
        "### Section for configuring tunnel agents."                         NL
        "[tunnels]"                                                          NL
        "### Configure svn protocol tunnel schemes here.  By default, only"  NL
        "### the 'ssh' scheme is defined.  You can define other schemes to"  NL
        "### be used with 'svn+scheme://hostname/path' URLs.  A scheme"      NL
        "### definition is simply a command, optionally prefixed by an"      NL
        "### environment variable name which can override the command if it" NL
        "### is defined.  The command (or environment variable) may contain" NL
        "### arguments, using standard shell quoting for arguments with"     NL
        "### spaces.  The command will be invoked as:"                       NL
        "###   <command> <hostname> svnserve -t"                             NL
        "### (If the URL includes a username, then the hostname will be"     NL
        "### passed to the tunnel agent as <user>@<hostname>.)  If the"      NL
        "### built-in ssh scheme were not predefined, it could be defined"   NL
        "### as:"                                                            NL
        "# ssh = $SVN_SSH ssh -q --"                                         NL
        "### If you wanted to define a new 'rsh' scheme, to be used with"    NL
        "### 'svn+rsh:' URLs, you could do so as follows:"                   NL
        "# rsh = rsh --"                                                     NL
        "### Or, if you wanted to specify a full path and arguments:"        NL
        "# rsh = /path/to/rsh -l myusername --"                              NL
        "### On Windows, if you are specifying a full path to a command,"    NL
        "### use a forward slash (/) or a paired backslash (\\\\) as the"    NL
        "### path separator.  A single backslash will be treated as an"      NL
        "### escape for the following character."                            NL
        ""                                                                   NL
        "### Section for configuring miscellaneous Subversion options."      NL
        "[miscellany]"                                                       NL
        "### Set global-ignores to a set of whitespace-delimited globs"      NL
        "### which Subversion will ignore in its 'status' output, and"       NL
        "### while importing or adding files and directories."               NL
        "### '*' matches leading dots, e.g. '*.rej' matches '.foo.rej'."     NL
        "# global-ignores = " SVN_CONFIG__DEFAULT_GLOBAL_IGNORES_LINE_1      NL
        "#   " SVN_CONFIG__DEFAULT_GLOBAL_IGNORES_LINE_2                     NL
        "### Set log-encoding to the default encoding for log messages"      NL
        "# log-encoding = latin1"                                            NL
        "### Set use-commit-times to make checkout/update/switch/revert"     NL
        "### put last-committed timestamps on every file touched."           NL
        "# use-commit-times = yes"                                           NL
        "### Set no-unlock to prevent 'svn commit' from automatically"       NL
        "### releasing locks on files."                                      NL
        "# no-unlock = yes"                                                  NL
        "### Set mime-types-file to a MIME type registry file, used to"      NL
        "### provide hints to Subversion's MIME type auto-detection"         NL
        "### algorithm."                                                     NL
        "# mime-types-file = /path/to/mime.types"                            NL
        "### Set preserved-conflict-file-exts to a whitespace-delimited"     NL
        "### list of patterns matching file extensions which should be"      NL
        "### preserved in generated conflict file names.  By default,"       NL
        "### conflict files use custom extensions."                          NL
        "# preserved-conflict-file-exts = doc ppt xls od?"                   NL
        "### Set enable-auto-props to 'yes' to enable automatic properties"  NL
        "### for 'svn add' and 'svn import', it defaults to 'no'."           NL
        "### Automatic properties are defined in the section 'auto-props'."  NL
        "# enable-auto-props = yes"                                          NL
#ifdef SVN_HAVE_LIBMAGIC
        "### Set enable-magic-file to 'no' to disable magic file detection"  NL
        "### of the file type when automatically setting svn:mime-type. It"  NL
        "### defaults to 'yes' if magic file support is possible."           NL
        "# enable-magic-file = yes"                                          NL
#endif
        "### Set interactive-conflicts to 'no' to disable interactive"       NL
        "### conflict resolution prompting.  It defaults to 'yes'."          NL
        "# interactive-conflicts = no"                                       NL
        "### Set memory-cache-size to define the size of the memory cache"   NL
        "### used by the client when accessing a FSFS repository via"        NL
        "### ra_local (the file:// scheme). The value represents the number" NL
        "### of MB used by the cache."                                       NL
        "# memory-cache-size = 16"                                           NL
        "### Set diff-ignore-content-type to 'yes' to cause 'svn diff' to"   NL
        "### attempt to show differences of all modified files regardless"   NL
        "### of their MIME content type.  By default, Subversion will only"  NL
        "### attempt to show differences for files believed to have human-"  NL
        "### readable (non-binary) content.  This option is especially"      NL
        "### useful when Subversion is configured (via the 'diff-cmd'"       NL
        "### option) to employ an external differencing tool which is able"  NL
        "### to show meaningful differences for binary file formats.  [New"  NL
        "### in 1.9]"                                                        NL
        "# diff-ignore-content-type = no"                                    NL
        ""                                                                   NL
        "### Section for configuring automatic properties."                  NL
        "[auto-props]"                                                       NL
        "### The format of the entries is:"                                  NL
        "###   file-name-pattern = propname[=value][;propname[=value]...]"   NL
        "### The file-name-pattern can contain wildcards (such as '*' and"   NL
        "### '?').  All entries which match (case-insensitively) will be"    NL
        "### applied to the file.  Note that auto-props functionality"       NL
        "### must be enabled, which is typically done by setting the"        NL
        "### 'enable-auto-props' option."                                    NL
        "# *.c = svn:eol-style=native"                                       NL
        "# *.cpp = svn:eol-style=native"                                     NL
        "# *.h = svn:keywords=Author Date Id Rev URL;svn:eol-style=native"   NL
        "# *.dsp = svn:eol-style=CRLF"                                       NL
        "# *.dsw = svn:eol-style=CRLF"                                       NL
        "# *.sh = svn:eol-style=native;svn:executable"                       NL
        "# *.txt = svn:eol-style=native;svn:keywords=Author Date Id Rev URL;"NL
        "# *.png = svn:mime-type=image/png"                                  NL
        "# *.jpg = svn:mime-type=image/jpeg"                                 NL
        "# Makefile = svn:eol-style=native"                                  NL
        ""                                                                   NL
        "### Section for configuring working copies."                        NL
        "[working-copy]"                                                     NL
        "### Set to a list of the names of specific clients that should use" NL
        "### exclusive SQLite locking of working copies.  This increases the"NL
        "### performance of the client but prevents concurrent access by"    NL
        "### other clients.  Third-party clients may also support this"      NL
        "### option."                                                        NL
        "### Possible values:"                                               NL
        "###   svn                (the command line client)"                 NL
        "# exclusive-locking-clients ="                                      NL
        "### Set to true to enable exclusive SQLite locking of working"      NL
        "### copies by all clients using the 1.8 APIs.  Enabling this may"   NL
        "### cause some clients to fail to work properly. This does not have"NL
        "### to be set for exclusive-locking-clients to work."               NL
        "# exclusive-locking = false"                                        NL
        "### Set the SQLite busy timeout in milliseconds: the maximum time"  NL
        "### the client waits to get access to the SQLite database before"   NL
        "### returning an error.  The default is 10000, i.e. 10 seconds."    NL
        "### Longer values may be useful when exclusive locking is enabled." NL
        "# busy-timeout = 10000"                                             NL
        ;

      err = svn_io_file_open(&f, path,
                             (APR_WRITE | APR_CREATE | APR_EXCL),
                             APR_OS_DEFAULT,
                             pool);

      if (! err)
        {
          SVN_ERR(svn_io_file_write_full(f, contents,
                                         strlen(contents), NULL, pool));
          SVN_ERR(svn_io_file_close(f, pool));
        }

      svn_error_clear(err);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_config_get_user_config_path(const char **path,
                                const char *config_dir,
                                const char *fname,
                                apr_pool_t *pool)
{
  *path= NULL;

  /* Note that even if fname is null, svn_dirent_join_many will DTRT. */

  if (config_dir)
    {
      *path = svn_dirent_join_many(pool, config_dir, fname, SVN_VA_NULL);
      return SVN_NO_ERROR;
    }

#ifdef WIN32
  {
    const char *folder;
    SVN_ERR(svn_config__win_config_path(&folder, FALSE, pool, pool));

    if (! folder)
      return SVN_NO_ERROR;

    *path = svn_dirent_join_many(pool, folder,
                                 SVN_CONFIG__SUBDIRECTORY, fname, SVN_VA_NULL);
  }

#elif defined(__HAIKU__)
  {
    char folder[B_PATH_NAME_LENGTH];

    status_t error = find_directory(B_USER_SETTINGS_DIRECTORY, -1, false,
                                    folder, sizeof(folder));
    if (error)
      return SVN_NO_ERROR;

    *path = svn_dirent_join_many(pool, folder,
                                 SVN_CONFIG__USR_DIRECTORY, fname,
                                 SVN_VA_NULL);
  }
#else  /* ! WIN32 && !__HAIKU__ */

  {
    const char *homedir = svn_user_get_homedir(pool);
    if (! homedir)
      return SVN_NO_ERROR;
    *path = svn_dirent_join_many(pool,
                                 homedir,
                               SVN_CONFIG__USR_DIRECTORY, fname, SVN_VA_NULL);
  }
#endif /* WIN32 */

  return SVN_NO_ERROR;
}


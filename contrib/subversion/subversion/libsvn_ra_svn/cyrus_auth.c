/*
 * cyrus_auth.c :  functions for Cyrus SASL-based authentication
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

#include "svn_private_config.h"
#ifdef SVN_HAVE_SASL

#define APR_WANT_STRFUNC
#include <apr_want.h>
#include <apr_general.h>
#include <apr_strings.h>
#include <apr_version.h>

#include "svn_types.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_pools.h"
#include "svn_ra.h"
#include "svn_ra_svn.h"
#include "svn_base64.h"

#include "private/svn_atomic.h"
#include "private/ra_svn_sasl.h"
#include "private/svn_mutex.h"

#include "ra_svn.h"

/* Note: In addition to being used via svn_atomic__init_once to control
 *       initialization of the SASL code this will also be referenced in
 *       the various functions that work with sasl mutexes to determine
 *       if the sasl pool has been destroyed.  This should be safe, since
 *       it is only set back to zero in the sasl pool's cleanups, which
 *       only happens during apr_terminate, which we assume is occurring
 *       in atexit processing, at which point we are already running in
 *       single threaded mode.
 */
volatile svn_atomic_t svn_ra_svn__sasl_status = 0;

/* Initialized by svn_ra_svn__sasl_common_init(). */
static volatile svn_atomic_t sasl_ctx_count;

static apr_pool_t *sasl_pool = NULL;


/* Pool cleanup called when sasl_pool is destroyed. */
static apr_status_t sasl_done_cb(void *data)
{
  /* Reset svn_ra_svn__sasl_status, in case the client calls
     apr_initialize()/apr_terminate() more than once. */
  svn_ra_svn__sasl_status = 0;
  if (svn_atomic_dec(&sasl_ctx_count) == 0)
    svn_sasl__done();
  return APR_SUCCESS;
}

#if APR_HAS_THREADS
/* Cyrus SASL is thread-safe only if we supply it with mutex functions
 * (with sasl_set_mutex()).  To make this work with APR, we need to use the
 * global sasl_pool for the mutex allocations.  Freeing a mutex actually
 * returns it to a global array.  We allocate mutexes from this
 * array if it is non-empty, or directly from the pool otherwise.
 * We also need a mutex to serialize accesses to the array itself.
 */

/* An array of allocated, but unused, apr_thread_mutex_t's. */
static apr_array_header_t *free_mutexes = NULL;

/* A mutex to serialize access to the array. */
static svn_mutex__t *array_mutex = NULL;

/* Callbacks we pass to sasl_set_mutex(). */

static svn_error_t *
sasl_mutex_alloc_cb_internal(svn_mutex__t **mutex)
{
  if (apr_is_empty_array(free_mutexes))
    return svn_mutex__init(mutex, TRUE, sasl_pool);
  else
    *mutex = *((svn_mutex__t**)apr_array_pop(free_mutexes));

  return SVN_NO_ERROR;
}

static void *sasl_mutex_alloc_cb(void)
{
  svn_mutex__t *mutex = NULL;
  svn_error_t *err;

  if (!svn_ra_svn__sasl_status)
    return NULL;

  err = svn_mutex__lock(array_mutex);
  if (err)
    svn_error_clear(err);
  else
    svn_error_clear(svn_mutex__unlock(array_mutex,
                                      sasl_mutex_alloc_cb_internal(&mutex)));

  return mutex;
}

static int check_result(svn_error_t *err)
{
  if (err)
    {
      svn_error_clear(err);
      return -1;
    }

  return 0;
}

static int sasl_mutex_lock_cb(void *mutex)
{
  if (!svn_ra_svn__sasl_status)
    return 0;
  return check_result(svn_mutex__lock(mutex));
}

static int sasl_mutex_unlock_cb(void *mutex)
{
  if (!svn_ra_svn__sasl_status)
    return 0;
  return check_result(svn_mutex__unlock(mutex, SVN_NO_ERROR));
}

static svn_error_t *
sasl_mutex_free_cb_internal(void *mutex)
{
  APR_ARRAY_PUSH(free_mutexes, svn_mutex__t*) = mutex;
  return SVN_NO_ERROR;
}

static void sasl_mutex_free_cb(void *mutex)
{
  svn_error_t *err;

  if (!svn_ra_svn__sasl_status)
    return;

  err = svn_mutex__lock(array_mutex);
  if (err)
    svn_error_clear(err);
  else
    svn_error_clear(svn_mutex__unlock(array_mutex,
                                      sasl_mutex_free_cb_internal(mutex)));
}
#endif /* APR_HAS_THREADS */

svn_error_t *
svn_ra_svn__sasl_common_init(apr_pool_t *pool)
{
  sasl_pool = svn_pool_create(pool);
  sasl_ctx_count = 1;
  apr_pool_cleanup_register(sasl_pool, NULL, sasl_done_cb,
                            apr_pool_cleanup_null);
#if APR_HAS_THREADS
  svn_sasl__set_mutex(sasl_mutex_alloc_cb,
                      sasl_mutex_lock_cb,
                      sasl_mutex_unlock_cb,
                      sasl_mutex_free_cb);
  free_mutexes = apr_array_make(sasl_pool, 0, sizeof(svn_mutex__t *));
  SVN_ERR(svn_mutex__init(&array_mutex, TRUE, sasl_pool));

#endif /* APR_HAS_THREADS */

  return SVN_NO_ERROR;
}

/* We are going to look at errno when we get SASL_FAIL but we don't
   know for sure whether SASL always sets errno.  Clearing errno
   before calling SASL functions helps in cases where SASL does
   nothing to set errno. */
#ifdef apr_set_os_error
#define clear_sasl_errno() apr_set_os_error(APR_SUCCESS)
#else
#define clear_sasl_errno() (void)0
#endif

/* Sometimes SASL returns SASL_FAIL as RESULT and sets errno.
 * SASL_FAIL translates to "generic error" which is quite unhelpful.
 * Try to append a more informative error message based on errno so
 * should be called before doing anything that may change errno. */
static const char *
get_sasl_errno_msg(int result, apr_pool_t *result_pool)
{
#ifdef apr_get_os_error
  char buf[1024];

  if (result == SASL_FAIL && apr_get_os_error() != 0)
    return apr_psprintf(result_pool, ": %s",
                        svn_strerror(apr_get_os_error(), buf, sizeof(buf)));
#endif
  return "";
}

/* Wrap an error message from SASL with a prefix that allows users
 * to tell that the error message came from SASL.  Queries errno and
 * so should be called before doing anything that may change errno. */
static const char *
get_sasl_error(sasl_conn_t *sasl_ctx, int result, apr_pool_t *result_pool)
{
  const char *sasl_errno_msg = get_sasl_errno_msg(result, result_pool);

  return apr_psprintf(result_pool,
                      _("SASL authentication error: %s%s"),
                      svn_sasl__errdetail(sasl_ctx), sasl_errno_msg);
}

static svn_error_t *sasl_init_cb(void *baton, apr_pool_t *pool)
{
  int result;

  SVN_ERR(svn_ra_svn__sasl_common_init(pool));
  clear_sasl_errno();
  result = svn_sasl__client_init(NULL);
  if (result != SASL_OK)
    {
      const char *sasl_errno_msg = get_sasl_errno_msg(result, pool);

      return svn_error_createf
        (SVN_ERR_RA_NOT_AUTHORIZED, NULL,
         _("Could not initialized the SASL library: %s%s"),
         svn_sasl__errstring(result, NULL, NULL),
         sasl_errno_msg);
    }

  return SVN_NO_ERROR;
}

svn_error_t *svn_ra_svn__sasl_init(void)
{
  SVN_ERR(svn_atomic__init_once(&svn_ra_svn__sasl_status,
                                sasl_init_cb, NULL, NULL));
  return SVN_NO_ERROR;
}

static apr_status_t sasl_dispose_cb(void *data)
{
  sasl_conn_t *sasl_ctx = data;
  svn_sasl__dispose(&sasl_ctx);
  if (svn_atomic_dec(&sasl_ctx_count) == 0)
    svn_sasl__done();
  return APR_SUCCESS;
}

void svn_ra_svn__default_secprops(sasl_security_properties_t *secprops)
{
  /* The minimum and maximum security strength factors that the chosen
     SASL mechanism should provide.  0 means 'no encryption', 256 means
     '256-bit encryption', which is about the best that any SASL
     mechanism can provide.  Using these values effectively means 'use
     whatever encryption the other side wants'.  Note that SASL will try
     to use better encryption whenever possible, so if both the server and
     the client use these values the highest possible encryption strength
     will be used. */
  secprops->min_ssf = 0;
  secprops->max_ssf = 256;

  /* Set maxbufsize to the maximum amount of data we can read at any one time.
     This value needs to be commmunicated to the peer if a security layer
     is negotiated. */
  secprops->maxbufsize = SVN_RA_SVN__READBUF_SIZE;

  secprops->security_flags = 0;
  secprops->property_names = secprops->property_values = NULL;
}

/* A baton type used by the SASL username and password callbacks. */
typedef struct cred_baton {
  svn_auth_baton_t *auth_baton;
  svn_auth_iterstate_t *iterstate;
  const char *realmstring;

  /* Unfortunately SASL uses two separate callbacks for the username and
     password, but we must fetch both of them at the same time. So we cache
     their values in the baton, set them to NULL individually when SASL
     demands them, and fetch the next pair when both are NULL. */
  const char *username;
  const char *password;

  /* Any errors we receive from svn_auth_{first,next}_credentials
     are saved here. */
  svn_error_t *err;

  /* This flag is set when we run out of credential providers. */
  svn_boolean_t no_more_creds;

  /* Were the auth callbacks ever called? */
  svn_boolean_t was_used;

  apr_pool_t *pool;
} cred_baton_t;

/* Call svn_auth_{first,next}_credentials. If successful, set BATON->username
   and BATON->password to the new username and password and return TRUE,
   otherwise return FALSE. If there are no more credentials, set
   BATON->no_more_creds to TRUE. Any errors are saved in BATON->err. */
static svn_boolean_t
get_credentials(cred_baton_t *baton)
{
  void *creds;

  if (baton->iterstate)
    baton->err = svn_auth_next_credentials(&creds, baton->iterstate,
                                           baton->pool);
  else
    baton->err = svn_auth_first_credentials(&creds, &baton->iterstate,
                                            SVN_AUTH_CRED_SIMPLE,
                                            baton->realmstring,
                                            baton->auth_baton, baton->pool);
  if (baton->err)
    return FALSE;

  if (! creds)
    {
      baton->no_more_creds = TRUE;
      return FALSE;
    }

  baton->username = ((svn_auth_cred_simple_t *)creds)->username;
  baton->password = ((svn_auth_cred_simple_t *)creds)->password;
  baton->was_used = TRUE;

  return TRUE;
}

/* The username callback. Implements the sasl_getsimple_t interface. */
static int
get_username_cb(void *b, int id, const char **username, size_t *len)
{
  cred_baton_t *baton = b;

  if (baton->username || get_credentials(baton))
    {
      *username = baton->username;
      if (len)
        *len = strlen(baton->username);
      baton->username = NULL;

      return SASL_OK;
    }

  return SASL_FAIL;
}

/* The password callback. Implements the sasl_getsecret_t interface. */
static int
get_password_cb(sasl_conn_t *conn, void *b, int id, sasl_secret_t **psecret)
{
  cred_baton_t *baton = b;

  if (baton->password || get_credentials(baton))
    {
      sasl_secret_t *secret;
      size_t len = strlen(baton->password);

      /* sasl_secret_t is a struct with a variable-sized array as a final
         member, which means we need to allocate len-1 supplementary bytes
         (one byte is part of sasl_secret_t, and we don't need a NULL
         terminator). */
      secret = apr_palloc(baton->pool, sizeof(*secret) + len - 1);
      secret->len = len;
      memcpy(secret->data, baton->password, len);
      baton->password = NULL;
      *psecret = secret;

      return SASL_OK;
    }

  return SASL_FAIL;
}

/* Create a new SASL context. */
static svn_error_t *new_sasl_ctx(sasl_conn_t **sasl_ctx,
                                 svn_boolean_t is_tunneled,
                                 const char *hostname,
                                 const char *local_addrport,
                                 const char *remote_addrport,
                                 sasl_callback_t *callbacks,
                                 apr_pool_t *pool)
{
  sasl_security_properties_t secprops;
  int result;

  clear_sasl_errno();
  result = svn_sasl__client_new(SVN_RA_SVN_SASL_NAME,
                                hostname, local_addrport, remote_addrport,
                                callbacks, SASL_SUCCESS_DATA,
                                sasl_ctx);
  if (result != SASL_OK)
    {
      const char *sasl_errno_msg = get_sasl_errno_msg(result, pool);

      return svn_error_createf(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                               _("Could not create SASL context: %s%s"),
                               svn_sasl__errstring(result, NULL, NULL),
                               sasl_errno_msg);
    }
  svn_atomic_inc(&sasl_ctx_count);
  apr_pool_cleanup_register(pool, *sasl_ctx, sasl_dispose_cb,
                            apr_pool_cleanup_null);

  if (is_tunneled)
    {
      /* We need to tell SASL that this connection is tunneled,
         otherwise it will ignore EXTERNAL. The third parameter
         should be the username, but since SASL doesn't seem
         to use it on the client side, any non-empty string will do. */
      clear_sasl_errno();
      result = svn_sasl__setprop(*sasl_ctx,
                                 SASL_AUTH_EXTERNAL, " ");
      if (result != SASL_OK)
        return svn_error_create(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                                get_sasl_error(*sasl_ctx, result, pool));
    }

  /* Set security properties. */
  svn_ra_svn__default_secprops(&secprops);
  svn_sasl__setprop(*sasl_ctx, SASL_SEC_PROPS, &secprops);

  return SVN_NO_ERROR;
}

/* Perform an authentication exchange */
static svn_error_t *try_auth(svn_ra_svn__session_baton_t *sess,
                             sasl_conn_t *sasl_ctx,
                             svn_boolean_t *success,
                             const char **last_err,
                             const char *mechstring,
                             apr_pool_t *pool)
{
  sasl_interact_t *client_interact = NULL;
  const char *out, *mech, *status = NULL;
  const svn_string_t *arg = NULL, *in;
  int result;
  unsigned int outlen;
  svn_boolean_t again;

  do
    {
      again = FALSE;
      clear_sasl_errno();
      result = svn_sasl__client_start(sasl_ctx,
                                      mechstring,
                                      &client_interact,
                                      &out,
                                      &outlen,
                                      &mech);
      switch (result)
        {
          case SASL_OK:
          case SASL_CONTINUE:
            /* Success. */
            break;
          case SASL_NOMECH:
            return svn_error_create(SVN_ERR_RA_SVN_NO_MECHANISMS, NULL, NULL);
          case SASL_BADPARAM:
          case SASL_NOMEM:
            /* Fatal error.  Fail the authentication. */
            return svn_error_create(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                                    get_sasl_error(sasl_ctx, result, pool));
          default:
            /* For anything else, delete the mech from the list
               and try again. */
            {
              const char *pmech = strstr(mechstring, mech);
              const char *head = apr_pstrndup(pool, mechstring,
                                              pmech - mechstring);
              const char *tail = pmech + strlen(mech);

              mechstring = apr_pstrcat(pool, head, tail, SVN_VA_NULL);
              again = TRUE;
            }
        }
    }
  while (again);

  /* Prepare the initial authentication token. */
  if (outlen > 0 || strcmp(mech, "EXTERNAL") == 0)
    arg = svn_base64_encode_string2(svn_string_ncreate(out, outlen, pool),
                                    TRUE, pool);

  /* Send the initial client response */
  SVN_ERR(svn_ra_svn__auth_response(sess->conn, pool, mech,
                                    arg ? arg->data : NULL));

  while (result == SASL_CONTINUE)
    {
      /* Read the server response */
      SVN_ERR(svn_ra_svn__read_tuple(sess->conn, pool, "w(?s)",
                                     &status, &in));

      if (strcmp(status, "failure") == 0)
        {
          /* Authentication failed.  Use the next set of credentials */
          *success = FALSE;
          /* Remember the message sent by the server because we'll want to
             return a meaningful error if we run out of auth providers. */
          *last_err = in ? in->data : "";
          return SVN_NO_ERROR;
        }

      if ((strcmp(status, "success") != 0 && strcmp(status, "step") != 0)
          || in == NULL)
        return svn_error_create(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                                _("Unexpected server response"
                                " to authentication"));

      /* If the mech is CRAM-MD5 we don't base64-decode the server response. */
      if (strcmp(mech, "CRAM-MD5") != 0)
        in = svn_base64_decode_string(in, pool);

      clear_sasl_errno();
      result = svn_sasl__client_step(sasl_ctx,
                                     in->data,
                                     (const unsigned int) in->len,
                                     &client_interact,
                                     &out, /* Filled in by SASL. */
                                     &outlen);

      if (result != SASL_OK && result != SASL_CONTINUE)
        return svn_error_create(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                                get_sasl_error(sasl_ctx, result, pool));

      /* If the server thinks we're done, then don't send any response. */
      if (strcmp(status, "success") == 0)
        break;

      if (outlen > 0)
        {
          arg = svn_string_ncreate(out, outlen, pool);
          /* Write our response. */
          /* For CRAM-MD5, we don't use base64-encoding. */
          if (strcmp(mech, "CRAM-MD5") != 0)
            arg = svn_base64_encode_string2(arg, TRUE, pool);
          SVN_ERR(svn_ra_svn__write_cstring(sess->conn, pool, arg->data));
        }
      else
        {
          SVN_ERR(svn_ra_svn__write_cstring(sess->conn, pool, ""));
        }
    }

  if (!status || strcmp(status, "step") == 0)
    {
      /* This is a client-send-last mech.  Read the last server response. */
      SVN_ERR(svn_ra_svn__read_tuple(sess->conn, pool, "w(?s)",
              &status, &in));

      if (strcmp(status, "failure") == 0)
        {
          *success = FALSE;
          *last_err = in ? in->data : "";
        }
      else if (strcmp(status, "success") == 0)
        {
          /* We're done */
          *success = TRUE;
        }
      else
        return svn_error_create(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                                _("Unexpected server response"
                                " to authentication"));
    }
  else
    *success = TRUE;
  return SVN_NO_ERROR;
}

/* Baton for a SASL encrypted svn_ra_svn__stream_t. */
typedef struct sasl_baton {
  svn_ra_svn__stream_t *stream; /* Inherited stream. */
  sasl_conn_t *ctx;             /* The SASL context for this connection. */
  unsigned int maxsize;         /* The maximum amount of data we can encode. */
  const char *read_buf;         /* The buffer returned by sasl_decode. */
  unsigned int read_len;        /* Its current length. */
  const char *write_buf;        /* The buffer returned by sasl_encode. */
  unsigned int write_len;       /* Its length. */
  apr_pool_t *scratch_pool;
} sasl_baton_t;

/* Functions to implement a SASL encrypted svn_ra_svn__stream_t. */

/* Implements svn_read_fn_t. */
static svn_error_t *sasl_read_cb(void *baton, char *buffer, apr_size_t *len)
{
  sasl_baton_t *sasl_baton = baton;
  int result;
  /* A copy of *len, used by the wrapped stream. */
  apr_size_t len2 = *len;

  /* sasl_decode might need more data than a single read can provide,
     hence the need to put a loop around the decoding. */
  while (! sasl_baton->read_buf || sasl_baton->read_len == 0)
    {
      SVN_ERR(svn_ra_svn__stream_read(sasl_baton->stream, buffer, &len2));
      if (len2 == 0)
        {
          *len = 0;
          return SVN_NO_ERROR;
        }
      clear_sasl_errno();
      result = svn_sasl__decode(sasl_baton->ctx, buffer, (unsigned int) len2,
                                &sasl_baton->read_buf,
                                &sasl_baton->read_len);
      if (result != SASL_OK)
        return svn_error_create(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                                get_sasl_error(sasl_baton->ctx, result,
                                               sasl_baton->scratch_pool));
    }

  /* The buffer returned by sasl_decode might be larger than what the
     caller wants.  If this is the case, we only copy back *len bytes now
     (the rest will be returned by subsequent calls to this function).
     If not, we just copy back the whole thing. */
  if (*len >= sasl_baton->read_len)
    {
      memcpy(buffer, sasl_baton->read_buf, sasl_baton->read_len);
      *len = sasl_baton->read_len;
      sasl_baton->read_buf = NULL;
      sasl_baton->read_len = 0;
    }
  else
    {
      memcpy(buffer, sasl_baton->read_buf, *len);
      sasl_baton->read_len -= *len;
      sasl_baton->read_buf += *len;
    }

  return SVN_NO_ERROR;
}

/* Implements svn_write_fn_t. */
static svn_error_t *
sasl_write_cb(void *baton, const char *buffer, apr_size_t *len)
{
  sasl_baton_t *sasl_baton = baton;
  int result;

  if (! sasl_baton->write_buf || sasl_baton->write_len == 0)
    {
      /* Make sure we don't write too much. */
      *len = (*len > sasl_baton->maxsize) ? sasl_baton->maxsize : *len;
      clear_sasl_errno();
      result = svn_sasl__encode(sasl_baton->ctx, buffer, (unsigned int) *len,
                                &sasl_baton->write_buf,
                                &sasl_baton->write_len);

      if (result != SASL_OK)
        return svn_error_create(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                                get_sasl_error(sasl_baton->ctx, result,
                                               sasl_baton->scratch_pool));
    }

  do
    {
      apr_size_t tmplen = sasl_baton->write_len;
      SVN_ERR(svn_ra_svn__stream_write(sasl_baton->stream,
                                       sasl_baton->write_buf,
                                       &tmplen));
      if (tmplen == 0)
      {
        /* The output buffer and its length will be preserved in sasl_baton
           and will be written out during the next call to this function
           (which will have the same arguments). */
        *len = 0;
        return SVN_NO_ERROR;
      }
      sasl_baton->write_len -= (unsigned int) tmplen;
      sasl_baton->write_buf += tmplen;
    }
  while (sasl_baton->write_len > 0);

  sasl_baton->write_buf = NULL;
  sasl_baton->write_len = 0;

  return SVN_NO_ERROR;
}

/* Implements ra_svn_timeout_fn_t. */
static void sasl_timeout_cb(void *baton, apr_interval_time_t interval)
{
  sasl_baton_t *sasl_baton = baton;
  svn_ra_svn__stream_timeout(sasl_baton->stream, interval);
}

/* Implements svn_stream_data_available_fn_t. */
static svn_error_t *
sasl_data_available_cb(void *baton, svn_boolean_t *data_available)
{
  sasl_baton_t *sasl_baton = baton;
  return svn_error_trace(svn_ra_svn__stream_data_available(sasl_baton->stream,
                                                         data_available));
}

svn_error_t *svn_ra_svn__enable_sasl_encryption(svn_ra_svn_conn_t *conn,
                                                sasl_conn_t *sasl_ctx,
                                                apr_pool_t *pool)
{
  const sasl_ssf_t *ssfp;

  if (! conn->encrypted)
    {
      int result;

      /* Get the strength of the security layer. */
      clear_sasl_errno();
      result = svn_sasl__getprop(sasl_ctx, SASL_SSF, (void*) &ssfp);
      if (result != SASL_OK)
        return svn_error_create(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                                get_sasl_error(sasl_ctx, result, pool));

      if (*ssfp > 0)
        {
          sasl_baton_t *sasl_baton;
          const void *maxsize;

          /* Flush the connection, as we're about to replace its stream. */
          SVN_ERR(svn_ra_svn__flush(conn, pool));

          /* Create and initialize the stream baton. */
          sasl_baton = apr_pcalloc(conn->pool, sizeof(*sasl_baton));
          sasl_baton->ctx = sasl_ctx;
          sasl_baton->scratch_pool = conn->pool;

          /* Find out the maximum input size for sasl_encode. */
          clear_sasl_errno();
          result = svn_sasl__getprop(sasl_ctx, SASL_MAXOUTBUF, &maxsize);
          if (result != SASL_OK)
            return svn_error_create(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                                    get_sasl_error(sasl_ctx, result, pool));
          sasl_baton->maxsize = *((const unsigned int *) maxsize);

          /* If there is any data left in the read buffer at this point,
             we need to decrypt it. */
          if (conn->read_end > conn->read_ptr)
            {
              clear_sasl_errno();
              result = svn_sasl__decode(
                  sasl_ctx, conn->read_ptr,
                  (unsigned int) (conn->read_end - conn->read_ptr),
                  &sasl_baton->read_buf, &sasl_baton->read_len);
              if (result != SASL_OK)
                return svn_error_create(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                                        get_sasl_error(sasl_ctx, result, pool));
              conn->read_end = conn->read_ptr;
            }

          /* Wrap the existing stream. */
          sasl_baton->stream = conn->stream;

          {
            svn_stream_t *sasl_in = svn_stream_create(sasl_baton, conn->pool);
            svn_stream_t *sasl_out = svn_stream_create(sasl_baton, conn->pool);

            svn_stream_set_read2(sasl_in, sasl_read_cb, NULL /* use default */);
            svn_stream_set_data_available(sasl_in, sasl_data_available_cb);
            svn_stream_set_write(sasl_out, sasl_write_cb);

            conn->stream = svn_ra_svn__stream_create(sasl_in, sasl_out,
                                                     sasl_baton,
                                                     sasl_timeout_cb,
                                                     conn->pool);
          }
          /* Yay, we have a security layer! */
          conn->encrypted = TRUE;
        }
    }
  return SVN_NO_ERROR;
}

svn_error_t *svn_ra_svn__get_addresses(const char **local_addrport,
                                       const char **remote_addrport,
                                       svn_ra_svn_conn_t *conn,
                                       apr_pool_t *pool)
{
  if (conn->sock)
    {
      apr_status_t apr_err;
      apr_sockaddr_t *local_sa, *remote_sa;
      char *local_addr, *remote_addr;

      apr_err = apr_socket_addr_get(&local_sa, APR_LOCAL, conn->sock);
      if (apr_err)
        return svn_error_wrap_apr(apr_err, NULL);

      apr_err = apr_socket_addr_get(&remote_sa, APR_REMOTE, conn->sock);
      if (apr_err)
        return svn_error_wrap_apr(apr_err, NULL);

      apr_err = apr_sockaddr_ip_get(&local_addr, local_sa);
      if (apr_err)
        return svn_error_wrap_apr(apr_err, NULL);

      apr_err = apr_sockaddr_ip_get(&remote_addr, remote_sa);
      if (apr_err)
        return svn_error_wrap_apr(apr_err, NULL);

      /* Format the IP address and port number like this: a.b.c.d;port */
      *local_addrport = apr_pstrcat(pool, local_addr, ";",
                                    apr_itoa(pool, (int)local_sa->port),
                                    SVN_VA_NULL);
      *remote_addrport = apr_pstrcat(pool, remote_addr, ";",
                                     apr_itoa(pool, (int)remote_sa->port),
                                     SVN_VA_NULL);
    }
  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_svn__do_cyrus_auth(svn_ra_svn__session_baton_t *sess,
                          const svn_ra_svn__list_t *mechlist,
                          const char *realm, apr_pool_t *pool)
{
  apr_pool_t *subpool;
  sasl_conn_t *sasl_ctx;
  const char *mechstring = "", *last_err = "", *realmstring;
  const char *local_addrport = NULL, *remote_addrport = NULL;
  svn_boolean_t success;
  sasl_callback_t *callbacks;
  cred_baton_t cred_baton = { 0 };
  int i;

  if (!sess->is_tunneled)
    {
      SVN_ERR(svn_ra_svn__get_addresses(&local_addrport, &remote_addrport,
                                        sess->conn, pool));
    }

  /* Prefer EXTERNAL, then ANONYMOUS, then let SASL decide. */
  if (svn_ra_svn__find_mech(mechlist, "EXTERNAL"))
    mechstring = "EXTERNAL";
  else if (svn_ra_svn__find_mech(mechlist, "ANONYMOUS"))
    mechstring = "ANONYMOUS";
  else
    {
      /* Create a string containing the list of mechanisms, separated by spaces. */
      for (i = 0; i < mechlist->nelts; i++)
        {
          svn_ra_svn__item_t *elt = &SVN_RA_SVN__LIST_ITEM(mechlist, i);
          mechstring = apr_pstrcat(pool,
                                   mechstring,
                                   i == 0 ? "" : " ",
                                   elt->u.word.data, SVN_VA_NULL);
        }
    }

  realmstring = apr_psprintf(pool, "%s %s", sess->realm_prefix, realm);

  /* Initialize the credential baton. */
  cred_baton.auth_baton = sess->auth_baton;
  cred_baton.realmstring = realmstring;
  cred_baton.pool = pool;

  /* Reserve space for 3 callbacks (for the username, password and the
     array terminator).  These structures must persist until the
     disposal of the SASL context at pool cleanup, however the
     callback functions will not be invoked outside this function so
     other structures can have a shorter lifetime. */
  callbacks = apr_palloc(sess->conn->pool, sizeof(*callbacks) * 3);

  /* Initialize the callbacks array. */

  /* The username callback. */
  callbacks[0].id = SASL_CB_AUTHNAME;
  callbacks[0].proc = (int (*)(void))get_username_cb;
  callbacks[0].context = &cred_baton;

  /* The password callback. */
  callbacks[1].id = SASL_CB_PASS;
  callbacks[1].proc = (int (*)(void))get_password_cb;
  callbacks[1].context = &cred_baton;

  /* Mark the end of the array. */
  callbacks[2].id = SASL_CB_LIST_END;
  callbacks[2].proc = NULL;
  callbacks[2].context = NULL;

  subpool = svn_pool_create(pool);
  do
    {
      svn_error_t *err;

      /* If last_err was set to a non-empty string, it needs to be duplicated
         to the parent pool before the subpool is cleared. */
      if (*last_err)
        last_err = apr_pstrdup(pool, last_err);
      svn_pool_clear(subpool);

      SVN_ERR(new_sasl_ctx(&sasl_ctx, sess->is_tunneled,
                           sess->hostname, local_addrport, remote_addrport,
                           callbacks, sess->conn->pool));
      err = try_auth(sess, sasl_ctx, &success, &last_err, mechstring,
                     subpool);

      /* If we encountered an error while fetching credentials, that error
         has priority. */
      if (cred_baton.err)
        {
          svn_error_clear(err);
          return cred_baton.err;
        }
      if (cred_baton.no_more_creds
          || (! err && ! success && ! cred_baton.was_used))
        {
          svn_error_clear(err);
          /* If we ran out of authentication providers, or if we got a server
             error and our callbacks were never called, there's no point in
             retrying authentication.  Return the last error sent by the
             server. */
          if (*last_err)
            return svn_error_createf(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                                     _("Authentication error from server: %s"),
                                     last_err);
          /* Hmm, we don't have a server error. Return a generic error. */
          return svn_error_create(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                                  _("Can't get username or password"));
        }
      if (err)
        {
          if (err->apr_err == SVN_ERR_RA_SVN_NO_MECHANISMS)
            {
              svn_error_clear(err);

              /* We could not find a supported mechanism in the list sent by the
                 server. In many cases this happens because the client is missing
                 the CRAM-MD5 or ANONYMOUS plugins, in which case we can simply use
                 the built-in implementation. In all other cases this call will be
                 useless, but hey, at least we'll get consistent error messages. */
              return svn_error_trace(svn_ra_svn__do_internal_auth(sess, mechlist,
                                                                realm, pool));
            }
          return err;
        }
    }
  while (!success);
  svn_pool_destroy(subpool);

  SVN_ERR(svn_ra_svn__enable_sasl_encryption(sess->conn, sasl_ctx, pool));

  SVN_ERR(svn_auth_save_credentials(cred_baton.iterstate, pool));

  return SVN_NO_ERROR;
}

#endif /* SVN_HAVE_SASL */

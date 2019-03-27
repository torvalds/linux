/*
 * Copyright (c) 1999-2006, 2008 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 */

#include <sm/gen.h>
SM_RCSID("@(#)$Id: sfsasl.c,v 8.121 2013-11-22 20:51:56 ca Exp $")
#include <stdlib.h>
#include <sendmail.h>
#include <sm/time.h>
#include <sm/fdset.h>
#include <errno.h>

/* allow to disable error handling code just in case... */
#ifndef DEAL_WITH_ERROR_SSL
# define DEAL_WITH_ERROR_SSL	1
#endif /* ! DEAL_WITH_ERROR_SSL */

#if SASL
# include "sfsasl.h"

/* Structure used by the "sasl" file type */
struct sasl_obj
{
	SM_FILE_T *fp;
	sasl_conn_t *conn;
};

struct sasl_info
{
	SM_FILE_T *fp;
	sasl_conn_t *conn;
};

/*
**  SASL_GETINFO - returns requested information about a "sasl" file
**		  descriptor.
**
**	Parameters:
**		fp -- the file descriptor
**		what -- the type of information requested
**		valp -- the thang to return the information in
**
**	Returns:
**		-1 for unknown requests
**		>=0 on success with valp filled in (if possible).
*/

static int sasl_getinfo __P((SM_FILE_T *, int, void *));

static int
sasl_getinfo(fp, what, valp)
	SM_FILE_T *fp;
	int what;
	void *valp;
{
	struct sasl_obj *so = (struct sasl_obj *) fp->f_cookie;

	switch (what)
	{
	  case SM_IO_WHAT_FD:
		if (so->fp == NULL)
			return -1;
		return so->fp->f_file; /* for stdio fileno() compatability */

	  case SM_IO_IS_READABLE:
		if (so->fp == NULL)
			return 0;

		/* get info from underlying file */
		return sm_io_getinfo(so->fp, what, valp);

	  default:
		return -1;
	}
}

/*
**  SASL_OPEN -- creates the sasl specific information for opening a
**		file of the sasl type.
**
**	Parameters:
**		fp -- the file pointer associated with the new open
**		info -- contains the sasl connection information pointer and
**			the original SM_FILE_T that holds the open
**		flags -- ignored
**		rpool -- ignored
**
**	Returns:
**		0 on success
*/

static int sasl_open __P((SM_FILE_T *, const void *, int, const void *));

/* ARGSUSED2 */
static int
sasl_open(fp, info, flags, rpool)
	SM_FILE_T *fp;
	const void *info;
	int flags;
	const void *rpool;
{
	struct sasl_obj *so;
	struct sasl_info *si = (struct sasl_info *) info;

	so = (struct sasl_obj *) sm_malloc(sizeof(struct sasl_obj));
	if (so == NULL)
	{
		errno = ENOMEM;
		return -1;
	}
	so->fp = si->fp;
	so->conn = si->conn;

	/*
	**  The underlying 'fp' is set to SM_IO_NOW so that the entire
	**  encoded string is written in one chunk. Otherwise there is
	**  the possibility that it may appear illegal, bogus or
	**  mangled to the other side of the connection.
	**  We will read or write through 'fp' since it is the opaque
	**  connection for the communications. We need to treat it this
	**  way in case the encoded string is to be sent down a TLS
	**  connection rather than, say, sm_io's stdio.
	*/

	(void) sm_io_setvbuf(so->fp, SM_TIME_DEFAULT, NULL, SM_IO_NOW, 0);
	fp->f_cookie = so;
	return 0;
}

/*
**  SASL_CLOSE -- close the sasl specific parts of the sasl file pointer
**
**	Parameters:
**		fp -- the file pointer to close
**
**	Returns:
**		0 on success
*/

static int sasl_close __P((SM_FILE_T *));

static int
sasl_close(fp)
	SM_FILE_T *fp;
{
	struct sasl_obj *so;

	so = (struct sasl_obj *) fp->f_cookie;
	if (so == NULL)
		return 0;
	if (so->fp != NULL)
	{
		sm_io_close(so->fp, SM_TIME_DEFAULT);
		so->fp = NULL;
	}
	sm_free(so);
	so = NULL;
	return 0;
}

/* how to deallocate a buffer allocated by SASL */
extern void	sm_sasl_free __P((void *));
#  define SASL_DEALLOC(b)	sm_sasl_free(b)

/*
**  SASL_READ -- read encrypted information and decrypt it for the caller
**
**	Parameters:
**		fp -- the file pointer
**		buf -- the location to place the decrypted information
**		size -- the number of bytes to read after decryption
**
**	Results:
**		-1 on error
**		otherwise the number of bytes read
*/

static ssize_t sasl_read __P((SM_FILE_T *, char *, size_t));

static ssize_t
sasl_read(fp, buf, size)
	SM_FILE_T *fp;
	char *buf;
	size_t size;
{
	int result;
	ssize_t len;
# if SASL >= 20000
	static const char *outbuf = NULL;
# else /* SASL >= 20000 */
	static char *outbuf = NULL;
# endif /* SASL >= 20000 */
	static unsigned int outlen = 0;
	static unsigned int offset = 0;
	struct sasl_obj *so = (struct sasl_obj *) fp->f_cookie;

	/*
	**  sasl_decode() may require more data than a single read() returns.
	**  Hence we have to put a loop around the decoding.
	**  This also requires that we may have to split up the returned
	**  data since it might be larger than the allowed size.
	**  Therefore we use a static pointer and return portions of it
	**  if necessary.
	**  XXX Note: This function is not thread-safe nor can it be used
	**  on more than one file. A correct implementation would store
	**  this data in fp->f_cookie.
	*/

# if SASL >= 20000
	while (outlen == 0)
# else /* SASL >= 20000 */
	while (outbuf == NULL && outlen == 0)
# endif /* SASL >= 20000 */
	{
		len = sm_io_read(so->fp, SM_TIME_DEFAULT, buf, size);
		if (len <= 0)
			return len;
		result = sasl_decode(so->conn, buf,
				     (unsigned int) len, &outbuf, &outlen);
		if (result != SASL_OK)
		{
			if (LogLevel > 7)
				sm_syslog(LOG_WARNING, NOQID,
					"AUTH: sasl_decode error=%d", result);
			outbuf = NULL;
			offset = 0;
			outlen = 0;
			return -1;
		}
	}

	if (outbuf == NULL)
	{
		/* be paranoid: outbuf == NULL but outlen != 0 */
		syserr("@sasl_read failure: outbuf == NULL but outlen != 0");
		/* NOTREACHED */
	}
	if (outlen - offset > size)
	{
		/* return another part of the buffer */
		(void) memcpy(buf, outbuf + offset, size);
		offset += size;
		len = size;
	}
	else
	{
		/* return the rest of the buffer */
		len = outlen - offset;
		(void) memcpy(buf, outbuf + offset, (size_t) len);
# if SASL < 20000
		SASL_DEALLOC(outbuf);
# endif /* SASL < 20000 */
		outbuf = NULL;
		offset = 0;
		outlen = 0;
	}
	return len;
}

/*
**  SASL_WRITE -- write information out after encrypting it
**
**	Parameters:
**		fp -- the file pointer
**		buf -- holds the data to be encrypted and written
**		size -- the number of bytes to have encrypted and written
**
**	Returns:
**		-1 on error
**		otherwise number of bytes written
*/

static ssize_t sasl_write __P((SM_FILE_T *, const char *, size_t));

static ssize_t
sasl_write(fp, buf, size)
	SM_FILE_T *fp;
	const char *buf;
	size_t size;
{
	int result;
# if SASL >= 20000
	const char *outbuf;
# else /* SASL >= 20000 */
	char *outbuf;
# endif /* SASL >= 20000 */
	unsigned int outlen, *maxencode;
	size_t ret = 0, total = 0;
	struct sasl_obj *so = (struct sasl_obj *) fp->f_cookie;

	/*
	**  Fetch the maximum input buffer size for sasl_encode().
	**  This can be less than the size set in attemptauth()
	**  due to a negotiation with the other side, e.g.,
	**  Cyrus IMAP lmtp program sets maxbuf=4096,
	**  digestmd5 substracts 25 and hence we'll get 4071
	**  instead of 8192 (MAXOUTLEN).
	**  Hack (for now): simply reduce the size, callers are (must be)
	**  able to deal with that and invoke sasl_write() again with
	**  the rest of the data.
	**  Note: it would be better to store this value in the context
	**  after the negotiation.
	*/

	result = sasl_getprop(so->conn, SASL_MAXOUTBUF,
				(const void **) &maxencode);
	if (result == SASL_OK && size > *maxencode && *maxencode > 0)
		size = *maxencode;

	result = sasl_encode(so->conn, buf,
			     (unsigned int) size, &outbuf, &outlen);

	if (result != SASL_OK)
	{
		if (LogLevel > 7)
			sm_syslog(LOG_WARNING, NOQID,
				"AUTH: sasl_encode error=%d", result);
		return -1;
	}

	if (outbuf != NULL)
	{
		while (outlen > 0)
		{
			errno = 0;
			/* XXX result == 0? */
			ret = sm_io_write(so->fp, SM_TIME_DEFAULT,
					  &outbuf[total], outlen);
			if (ret <= 0)
				return ret;
			outlen -= ret;
			total += ret;
		}
# if SASL < 20000
		SASL_DEALLOC(outbuf);
# endif /* SASL < 20000 */
	}
	return size;
}

/*
**  SFDCSASL -- create sasl file type and open in and out file pointers
**	       for sendmail to read from and write to.
**
**	Parameters:
**		fin -- the sm_io file encrypted data to be read from
**		fout -- the sm_io file encrypted data to be written to
**		conn -- the sasl connection pointer
**		tmo -- timeout
**
**	Returns:
**		-1 on error
**		0 on success
**
**	Side effects:
**		The arguments "fin" and "fout" are replaced with the new
**		SM_FILE_T pointers.
*/

int
sfdcsasl(fin, fout, conn, tmo)
	SM_FILE_T **fin;
	SM_FILE_T **fout;
	sasl_conn_t *conn;
	int tmo;
{
	SM_FILE_T *newin, *newout;
	SM_FILE_T  SM_IO_SET_TYPE(sasl_vector, "sasl", sasl_open, sasl_close,
		sasl_read, sasl_write, NULL, sasl_getinfo, NULL,
		SM_TIME_DEFAULT);
	struct sasl_info info;

	if (conn == NULL)
	{
		/* no need to do anything */
		return 0;
	}

	SM_IO_INIT_TYPE(sasl_vector, "sasl", sasl_open, sasl_close,
		sasl_read, sasl_write, NULL, sasl_getinfo, NULL,
		SM_TIME_DEFAULT);
	info.fp = *fin;
	info.conn = conn;
	newin = sm_io_open(&sasl_vector, SM_TIME_DEFAULT, &info,
			SM_IO_RDONLY_B, NULL);

	if (newin == NULL)
		return -1;

	info.fp = *fout;
	info.conn = conn;
	newout = sm_io_open(&sasl_vector, SM_TIME_DEFAULT, &info,
			SM_IO_WRONLY_B, NULL);

	if (newout == NULL)
	{
		(void) sm_io_close(newin, SM_TIME_DEFAULT);
		return -1;
	}
	sm_io_automode(newin, newout);

	sm_io_setinfo(*fin, SM_IO_WHAT_TIMEOUT, &tmo);
	sm_io_setinfo(*fout, SM_IO_WHAT_TIMEOUT, &tmo);

	*fin = newin;
	*fout = newout;
	return 0;
}
#endif /* SASL */

#if STARTTLS
# include "sfsasl.h"
# include <openssl/err.h>

/* Structure used by the "tls" file type */
struct tls_obj
{
	SM_FILE_T *fp;
	SSL *con;
};

struct tls_info
{
	SM_FILE_T *fp;
	SSL *con;
};

/*
**  TLS_GETINFO - returns requested information about a "tls" file
**		 descriptor.
**
**	Parameters:
**		fp -- the file descriptor
**		what -- the type of information requested
**		valp -- the thang to return the information in (unused)
**
**	Returns:
**		-1 for unknown requests
**		>=0 on success with valp filled in (if possible).
*/

static int tls_getinfo __P((SM_FILE_T *, int, void *));

/* ARGSUSED2 */
static int
tls_getinfo(fp, what, valp)
	SM_FILE_T *fp;
	int what;
	void *valp;
{
	struct tls_obj *so = (struct tls_obj *) fp->f_cookie;

	switch (what)
	{
	  case SM_IO_WHAT_FD:
		if (so->fp == NULL)
			return -1;
		return so->fp->f_file; /* for stdio fileno() compatability */

	  case SM_IO_IS_READABLE:
		return SSL_pending(so->con) > 0;

	  default:
		return -1;
	}
}

/*
**  TLS_OPEN -- creates the tls specific information for opening a
**	       file of the tls type.
**
**	Parameters:
**		fp -- the file pointer associated with the new open
**		info -- the sm_io file pointer holding the open and the
**			TLS encryption connection to be read from or written to
**		flags -- ignored
**		rpool -- ignored
**
**	Returns:
**		0 on success
*/

static int tls_open __P((SM_FILE_T *, const void *, int, const void *));

/* ARGSUSED2 */
static int
tls_open(fp, info, flags, rpool)
	SM_FILE_T *fp;
	const void *info;
	int flags;
	const void *rpool;
{
	struct tls_obj *so;
	struct tls_info *ti = (struct tls_info *) info;

	so = (struct tls_obj *) sm_malloc(sizeof(struct tls_obj));
	if (so == NULL)
	{
		errno = ENOMEM;
		return -1;
	}
	so->fp = ti->fp;
	so->con = ti->con;

	/*
	**  We try to get the "raw" file descriptor that TLS uses to
	**  do the actual read/write with. This is to allow us control
	**  over the file descriptor being a blocking or non-blocking type.
	**  Under the covers TLS handles the change and this allows us
	**  to do timeouts with sm_io.
	*/

	fp->f_file = sm_io_getinfo(so->fp, SM_IO_WHAT_FD, NULL);
	(void) sm_io_setvbuf(so->fp, SM_TIME_DEFAULT, NULL, SM_IO_NOW, 0);
	fp->f_cookie = so;
	return 0;
}

/*
**  TLS_CLOSE -- close the tls specific parts of the tls file pointer
**
**	Parameters:
**		fp -- the file pointer to close
**
**	Returns:
**		0 on success
*/

static int tls_close __P((SM_FILE_T *));

static int
tls_close(fp)
	SM_FILE_T *fp;
{
	struct tls_obj *so;

	so = (struct tls_obj *) fp->f_cookie;
	if (so == NULL)
		return 0;
	if (so->fp != NULL)
	{
		sm_io_close(so->fp, SM_TIME_DEFAULT);
		so->fp = NULL;
	}
	sm_free(so);
	so = NULL;
	return 0;
}

/* maximum number of retries for TLS related I/O due to handshakes */
# define MAX_TLS_IOS	4

/*
**  TLS_RETRY -- check whether a failed SSL operation can be retried
**
**	Parameters:
**		ssl -- TLS structure
**		rfd -- read fd
**		wfd -- write fd
**		tlsstart -- start time of TLS operation
**		timeout -- timeout for TLS operation
**		err -- SSL error
**		where -- description of operation
**
**	Results:
**		>0 on success
**		0 on timeout
**		<0 on error
*/

int
tls_retry(ssl, rfd, wfd, tlsstart, timeout, err, where)
	SSL *ssl;
	int rfd;
	int wfd;
	time_t tlsstart;
	int timeout;
	int err;
	const char *where;
{
	int ret;
	time_t left;
	time_t now = curtime();
	struct timeval tv;

	ret = -1;

	/*
	**  For SSL_ERROR_WANT_{READ,WRITE}:
	**  There is not a complete SSL record available yet
	**  or there is only a partial SSL record removed from
	**  the network (socket) buffer into the SSL buffer.
	**  The SSL_connect will only succeed when a full
	**  SSL record is available (assuming a "real" error
	**  doesn't happen). To handle when a "real" error
	**  does happen the select is set for exceptions too.
	**  The connection may be re-negotiated during this time
	**  so both read and write "want errors" need to be handled.
	**  A select() exception loops back so that a proper SSL
	**  error message can be gotten.
	*/

	left = timeout - (now - tlsstart);
	if (left <= 0)
		return 0;	/* timeout */
	tv.tv_sec = left;
	tv.tv_usec = 0;

	if (LogLevel > 14)
	{
		sm_syslog(LOG_INFO, NOQID,
			  "STARTTLS=%s, info: fds=%d/%d, err=%d",
			  where, rfd, wfd, err);
	}

	if ((err == SSL_ERROR_WANT_READ && !SM_FD_OK_SELECT(rfd)) ||
	    (err == SSL_ERROR_WANT_WRITE && !SM_FD_OK_SELECT(wfd)))
	{
		if (LogLevel > 5)
		{
			sm_syslog(LOG_ERR, NOQID,
				  "STARTTLS=%s, error: fd %d/%d too large",
				  where, rfd, wfd);
			if (LogLevel > 8)
				tlslogerr(LOG_WARNING, where);
		}
		errno = EINVAL;
	}
	else if (err == SSL_ERROR_WANT_READ)
	{
		fd_set ssl_maskr, ssl_maskx;
		int save_errno = errno;

		FD_ZERO(&ssl_maskr);
		FD_SET(rfd, &ssl_maskr);
		FD_ZERO(&ssl_maskx);
		FD_SET(rfd, &ssl_maskx);
		do
		{
			ret = select(rfd + 1, &ssl_maskr, NULL, &ssl_maskx,
					&tv);
		} while (ret < 0 && errno == EINTR);
		if (ret < 0 && errno > 0)
			ret = -errno;
		errno = save_errno;
	}
	else if (err == SSL_ERROR_WANT_WRITE)
	{
		fd_set ssl_maskw, ssl_maskx;
		int save_errno = errno;

		FD_ZERO(&ssl_maskw);
		FD_SET(wfd, &ssl_maskw);
		FD_ZERO(&ssl_maskx);
		FD_SET(rfd, &ssl_maskx);
		do
		{
			ret = select(wfd + 1, NULL, &ssl_maskw, &ssl_maskx,
					&tv);
		} while (ret < 0 && errno == EINTR);
		if (ret < 0 && errno > 0)
			ret = -errno;
		errno = save_errno;
	}
	return ret;
}

/* errno to force refill() etc to stop (see IS_IO_ERROR()) */
#ifdef ETIMEDOUT
# define SM_ERR_TIMEOUT	ETIMEDOUT
#else /* ETIMEDOUT */
# define SM_ERR_TIMEOUT	EIO
#endif /* ETIMEDOUT */

/*
**  SET_TLS_RD_TMO -- read secured information for the caller
**
**	Parameters:
**		rd_tmo -- read timeout
**
**	Results:
**		previous read timeout
**	This is a hack: there is no way to pass it in
*/

static int tls_rd_tmo = -1;

int
set_tls_rd_tmo(rd_tmo)
	int rd_tmo;
{
	int old_rd_tmo;

	old_rd_tmo = tls_rd_tmo;
	tls_rd_tmo = rd_tmo;
	return old_rd_tmo;
}

/*
**  TLS_READ -- read secured information for the caller
**
**	Parameters:
**		fp -- the file pointer
**		buf -- the location to place the data
**		size -- the number of bytes to read from connection
**
**	Results:
**		-1 on error
**		otherwise the number of bytes read
*/

static ssize_t tls_read __P((SM_FILE_T *, char *, size_t));

static ssize_t
tls_read(fp, buf, size)
	SM_FILE_T *fp;
	char *buf;
	size_t size;
{
	int r, rfd, wfd, try, ssl_err;
	struct tls_obj *so = (struct tls_obj *) fp->f_cookie;
	time_t tlsstart;
	char *err;

	try = 99;
	err = NULL;
	tlsstart = curtime();

  retry:
	r = SSL_read(so->con, (char *) buf, size);

	if (r > 0)
		return r;

	err = NULL;
	switch (ssl_err = SSL_get_error(so->con, r))
	{
	  case SSL_ERROR_NONE:
	  case SSL_ERROR_ZERO_RETURN:
		break;
	  case SSL_ERROR_WANT_WRITE:
		err = "read W BLOCK";
		/* FALLTHROUGH */
	  case SSL_ERROR_WANT_READ:
		if (err == NULL)
			err = "read R BLOCK";
		rfd = SSL_get_rfd(so->con);
		wfd = SSL_get_wfd(so->con);
		try = tls_retry(so->con, rfd, wfd, tlsstart,
				(tls_rd_tmo < 0) ? TimeOuts.to_datablock
						 : tls_rd_tmo,
				ssl_err, "read");
		if (try > 0)
			goto retry;
		errno = SM_ERR_TIMEOUT;
		break;

	  case SSL_ERROR_WANT_X509_LOOKUP:
		err = "write X BLOCK";
		break;
	  case SSL_ERROR_SYSCALL:
		if (r == 0 && errno == 0) /* out of protocol EOF found */
			break;
		err = "syscall error";
/*
		get_last_socket_error());
*/
		break;
	  case SSL_ERROR_SSL:
#if DEAL_WITH_ERROR_SSL
		if (r == 0 && errno == 0) /* out of protocol EOF found */
			break;
#endif /* DEAL_WITH_ERROR_SSL */
		err = "generic SSL error";

		if (LogLevel > 9)
		{
			int pri;

			if (errno == EAGAIN && try > 0)
				pri = LOG_DEBUG;
			else
				pri = LOG_WARNING;
			tlslogerr(pri, "read");
		}

#if DEAL_WITH_ERROR_SSL
		/* avoid repeated calls? */
		if (r == 0)
			r = -1;
#endif /* DEAL_WITH_ERROR_SSL */
		break;
	}
	if (err != NULL)
	{
		int save_errno;

		save_errno = (errno == 0) ? EIO : errno;
		if (try == 0 && save_errno == SM_ERR_TIMEOUT)
		{
			if (LogLevel > 7)
				sm_syslog(LOG_WARNING, NOQID,
					  "STARTTLS: read error=timeout");
		}
		else if (LogLevel > 8)
		{
			int pri;

			if (save_errno == EAGAIN && try > 0)
				pri = LOG_DEBUG;
			else
				pri = LOG_WARNING;
			sm_syslog(pri, NOQID,
				  "STARTTLS: read error=%s (%d), errno=%d, get_error=%s, retry=%d, ssl_err=%d",
				  err, r, errno,
				  ERR_error_string(ERR_get_error(), NULL), try,
				  ssl_err);
		}
		else if (LogLevel > 7)
			sm_syslog(LOG_WARNING, NOQID,
				  "STARTTLS: read error=%s (%d), errno=%d, retry=%d, ssl_err=%d",
				  err, r, errno, try, ssl_err);
		errno = save_errno;
	}
	return r;
}

/*
**  TLS_WRITE -- write information out through secure connection
**
**	Parameters:
**		fp -- the file pointer
**		buf -- holds the data to be securely written
**		size -- the number of bytes to write
**
**	Returns:
**		-1 on error
**		otherwise number of bytes written
*/

static ssize_t tls_write __P((SM_FILE_T *, const char *, size_t));

static ssize_t
tls_write(fp, buf, size)
	SM_FILE_T *fp;
	const char *buf;
	size_t size;
{
	int r, rfd, wfd, try, ssl_err;
	struct tls_obj *so = (struct tls_obj *) fp->f_cookie;
	time_t tlsstart;
	char *err;

	try = 99;
	err = NULL;
	tlsstart = curtime();

  retry:
	r = SSL_write(so->con, (char *) buf, size);

	if (r > 0)
		return r;
	err = NULL;
	switch (ssl_err = SSL_get_error(so->con, r))
	{
	  case SSL_ERROR_NONE:
	  case SSL_ERROR_ZERO_RETURN:
		break;
	  case SSL_ERROR_WANT_WRITE:
		err = "read W BLOCK";
		/* FALLTHROUGH */
	  case SSL_ERROR_WANT_READ:
		if (err == NULL)
			err = "read R BLOCK";
		rfd = SSL_get_rfd(so->con);
		wfd = SSL_get_wfd(so->con);
		try = tls_retry(so->con, rfd, wfd, tlsstart,
				DATA_PROGRESS_TIMEOUT, ssl_err, "write");
		if (try > 0)
			goto retry;
		errno = SM_ERR_TIMEOUT;
		break;
	  case SSL_ERROR_WANT_X509_LOOKUP:
		err = "write X BLOCK";
		break;
	  case SSL_ERROR_SYSCALL:
		if (r == 0 && errno == 0) /* out of protocol EOF found */
			break;
		err = "syscall error";
/*
		get_last_socket_error());
*/
		break;
	  case SSL_ERROR_SSL:
		err = "generic SSL error";
/*
		ERR_GET_REASON(ERR_peek_error()));
*/
		if (LogLevel > 9)
			tlslogerr(LOG_WARNING, "write");

#if DEAL_WITH_ERROR_SSL
		/* avoid repeated calls? */
		if (r == 0)
			r = -1;
#endif /* DEAL_WITH_ERROR_SSL */
		break;
	}
	if (err != NULL)
	{
		int save_errno;

		save_errno = (errno == 0) ? EIO : errno;
		if (try == 0 && save_errno == SM_ERR_TIMEOUT)
		{
			if (LogLevel > 7)
				sm_syslog(LOG_WARNING, NOQID,
					  "STARTTLS: write error=timeout");
		}
		else if (LogLevel > 8)
			sm_syslog(LOG_WARNING, NOQID,
				  "STARTTLS: write error=%s (%d), errno=%d, get_error=%s, retry=%d, ssl_err=%d",
				  err, r, errno,
				  ERR_error_string(ERR_get_error(), NULL), try,
				  ssl_err);
		else if (LogLevel > 7)
			sm_syslog(LOG_WARNING, NOQID,
				  "STARTTLS: write error=%s (%d), errno=%d, retry=%d, ssl_err=%d",
				  err, r, errno, try, ssl_err);
		errno = save_errno;
	}
	return r;
}

/*
**  SFDCTLS -- create tls file type and open in and out file pointers
**	      for sendmail to read from and write to.
**
**	Parameters:
**		fin -- data input source being replaced
**		fout -- data output source being replaced
**		con -- the tls connection pointer
**
**	Returns:
**		-1 on error
**		0 on success
**
**	Side effects:
**		The arguments "fin" and "fout" are replaced with the new
**		SM_FILE_T pointers.
**		The original "fin" and "fout" are preserved in the tls file
**		type but are not actually used because of the design of TLS.
*/

int
sfdctls(fin, fout, con)
	SM_FILE_T **fin;
	SM_FILE_T **fout;
	SSL *con;
{
	SM_FILE_T *tlsin, *tlsout;
	SM_FILE_T SM_IO_SET_TYPE(tls_vector, "tls", tls_open, tls_close,
		tls_read, tls_write, NULL, tls_getinfo, NULL,
		SM_TIME_FOREVER);
	struct tls_info info;

	SM_ASSERT(con != NULL);

	SM_IO_INIT_TYPE(tls_vector, "tls", tls_open, tls_close,
		tls_read, tls_write, NULL, tls_getinfo, NULL,
		SM_TIME_FOREVER);
	info.fp = *fin;
	info.con = con;
	tlsin = sm_io_open(&tls_vector, SM_TIME_DEFAULT, &info, SM_IO_RDONLY_B,
			   NULL);
	if (tlsin == NULL)
		return -1;

	info.fp = *fout;
	tlsout = sm_io_open(&tls_vector, SM_TIME_DEFAULT, &info, SM_IO_WRONLY_B,
			    NULL);
	if (tlsout == NULL)
	{
		(void) sm_io_close(tlsin, SM_TIME_DEFAULT);
		return -1;
	}
	sm_io_automode(tlsin, tlsout);

	*fin = tlsin;
	*fout = tlsout;
	return 0;
}
#endif /* STARTTLS */

/*
 *  Copyright (c) 1999-2004, 2009 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 */

#include <sm/gen.h>
SM_RCSID("@(#)$Id: comm.c,v 8.71 2013-11-22 20:51:36 ca Exp $")

#include "libmilter.h"
#include <sm/errstring.h>
#include <sys/uio.h>

static ssize_t	retry_writev __P((socket_t, struct iovec *, int, struct timeval *));
static size_t Maxdatasize = MILTER_MAX_DATA_SIZE;

/*
**  SMFI_SETMAXDATASIZE -- set limit for milter data read/write.
**
**	Parameters:
**		sz -- new limit.
**
**	Returns:
**		old limit
*/

size_t
smfi_setmaxdatasize(sz)
	size_t sz;
{
	size_t old;

	old = Maxdatasize;
	Maxdatasize = sz;
	return old;
}

/*
**  MI_RD_CMD -- read a command
**
**	Parameters:
**		sd -- socket descriptor
**		timeout -- maximum time to wait
**		cmd -- single character command read from sd
**		rlen -- pointer to length of result
**		name -- name of milter
**
**	Returns:
**		buffer with rest of command
**		(malloc()ed here, should be free()d)
**		hack: encode error in cmd
*/

char *
mi_rd_cmd(sd, timeout, cmd, rlen, name)
	socket_t sd;
	struct timeval *timeout;
	char *cmd;
	size_t *rlen;
	char *name;
{
	ssize_t len;
	mi_int32 expl;
	ssize_t i;
	FD_RD_VAR(rds, excs);
	int ret;
	int save_errno;
	char *buf;
	char data[MILTER_LEN_BYTES + 1];

	*cmd = '\0';
	*rlen = 0;

	i = 0;
	for (;;)
	{
		FD_RD_INIT(sd, rds, excs);
		ret = FD_RD_READY(sd, rds, excs, timeout);
		if (ret == 0)
			break;
		else if (ret < 0)
		{
			if (errno == EINTR)
				continue;
			break;
		}
		if (FD_IS_RD_EXC(sd, rds, excs))
		{
			*cmd = SMFIC_SELECT;
			return NULL;
		}

		len = MI_SOCK_READ(sd, data + i, sizeof data - i);
		if (MI_SOCK_READ_FAIL(len))
		{
			smi_log(SMI_LOG_ERR,
				"%s, mi_rd_cmd: read returned %d: %s",
				name, (int) len, sm_errstring(errno));
			*cmd = SMFIC_RECVERR;
			return NULL;
		}
		if (len == 0)
		{
			*cmd = SMFIC_EOF;
			return NULL;
		}
		if (len >= (ssize_t) sizeof data - i)
			break;
		i += len;
	}
	if (ret == 0)
	{
		*cmd = SMFIC_TIMEOUT;
		return NULL;
	}
	else if (ret < 0)
	{
		smi_log(SMI_LOG_ERR,
			"%s: mi_rd_cmd: %s() returned %d: %s",
			name, MI_POLLSELECT, ret, sm_errstring(errno));
		*cmd = SMFIC_RECVERR;
		return NULL;
	}

	*cmd = data[MILTER_LEN_BYTES];
	data[MILTER_LEN_BYTES] = '\0';
	(void) memcpy((void *) &expl, (void *) &(data[0]), MILTER_LEN_BYTES);
	expl = ntohl(expl) - 1;
	if (expl <= 0)
		return NULL;
	if (expl > Maxdatasize)
	{
		*cmd = SMFIC_TOOBIG;
		return NULL;
	}
#if _FFR_ADD_NULL
	buf = malloc(expl + 1);
#else /* _FFR_ADD_NULL */
	buf = malloc(expl);
#endif /* _FFR_ADD_NULL */
	if (buf == NULL)
	{
		*cmd = SMFIC_MALLOC;
		return NULL;
	}

	i = 0;
	for (;;)
	{
		FD_RD_INIT(sd, rds, excs);
		ret = FD_RD_READY(sd, rds, excs, timeout);
		if (ret == 0)
			break;
		else if (ret < 0)
		{
			if (errno == EINTR)
				continue;
			break;
		}
		if (FD_IS_RD_EXC(sd, rds, excs))
		{
			*cmd = SMFIC_SELECT;
			free(buf);
			return NULL;
		}
		len = MI_SOCK_READ(sd, buf + i, expl - i);
		if (MI_SOCK_READ_FAIL(len))
		{
			smi_log(SMI_LOG_ERR,
				"%s: mi_rd_cmd: read returned %d: %s",
				name, (int) len, sm_errstring(errno));
			ret = -1;
			break;
		}
		if (len == 0)
		{
			*cmd = SMFIC_EOF;
			free(buf);
			return NULL;
		}
		if (len > expl - i)
		{
			*cmd = SMFIC_RECVERR;
			free(buf);
			return NULL;
		}
		if (len >= expl - i)
		{
			*rlen = expl;
#if _FFR_ADD_NULL
			/* makes life simpler for common string routines */
			buf[expl] = '\0';
#endif /* _FFR_ADD_NULL */
			return buf;
		}
		i += len;
	}

	save_errno = errno;
	free(buf);

	/* select returned 0 (timeout) or < 0 (error) */
	if (ret == 0)
	{
		*cmd = SMFIC_TIMEOUT;
		return NULL;
	}
	if (ret < 0)
	{
		smi_log(SMI_LOG_ERR,
			"%s: mi_rd_cmd: %s() returned %d: %s",
			name, MI_POLLSELECT, ret, sm_errstring(save_errno));
		*cmd = SMFIC_RECVERR;
		return NULL;
	}
	*cmd = SMFIC_UNKNERR;
	return NULL;
}

/*
**  RETRY_WRITEV -- Keep calling the writev() system call
**	until all the data is written out or an error occurs.
**
**	Parameters:
**		fd -- socket descriptor
**		iov -- io vector
**		iovcnt -- number of elements in io vector
**			must NOT exceed UIO_MAXIOV.
**		timeout -- maximum time to wait
**
**	Returns:
**		success: number of bytes written
**		otherwise: MI_FAILURE
*/

static ssize_t
retry_writev(fd, iov, iovcnt, timeout)
	socket_t fd;
	struct iovec *iov;
	int iovcnt;
	struct timeval *timeout;
{
	int i;
	ssize_t n, written;
	FD_WR_VAR(wrs);

	written = 0;
	for (;;)
	{
		while (iovcnt > 0 && iov[0].iov_len == 0)
		{
			iov++;
			iovcnt--;
		}
		if (iovcnt <= 0)
			return written;

		/*
		**  We don't care much about the timeout here,
		**  it's very long anyway; correct solution would be
		**  to take the time before the loop and reduce the
		**  timeout after each invocation.
		**  FD_SETSIZE is checked when socket is created.
		*/

		FD_WR_INIT(fd, wrs);
		i = FD_WR_READY(fd, wrs, timeout);
		if (i == 0)
			return MI_FAILURE;
		if (i < 0)
		{
			if (errno == EINTR || errno == EAGAIN)
				continue;
			return MI_FAILURE;
		}
		n = writev(fd, iov, iovcnt);
		if (n == -1)
		{
			if (errno == EINTR || errno == EAGAIN)
				continue;
			return MI_FAILURE;
		}

		written += n;
		for (i = 0; i < iovcnt; i++)
		{
			if (iov[i].iov_len > (unsigned int) n)
			{
				iov[i].iov_base = (char *)iov[i].iov_base + n;
				iov[i].iov_len -= (unsigned int) n;
				break;
			}
			n -= (int) iov[i].iov_len;
			iov[i].iov_len = 0;
		}
		if (i == iovcnt)
			return written;
	}
}

/*
**  MI_WR_CMD -- write a cmd to sd
**
**	Parameters:
**		sd -- socket descriptor
**		timeout -- maximum time to wait
**		cmd -- single character command to write
**		buf -- buffer with further data
**		len -- length of buffer (without cmd!)
**
**	Returns:
**		MI_SUCCESS/MI_FAILURE
*/

int
mi_wr_cmd(sd, timeout, cmd, buf, len)
	socket_t sd;
	struct timeval *timeout;
	int cmd;
	char *buf;
	size_t len;
{
	size_t sl;
	ssize_t l;
	mi_int32 nl;
	int iovcnt;
	struct iovec iov[2];
	char data[MILTER_LEN_BYTES + 1];

	if (len > Maxdatasize || (len > 0 && buf == NULL))
		return MI_FAILURE;

	nl = htonl(len + 1);	/* add 1 for the cmd char */
	(void) memcpy(data, (void *) &nl, MILTER_LEN_BYTES);
	data[MILTER_LEN_BYTES] = (char) cmd;
	sl = MILTER_LEN_BYTES + 1;

	/* set up the vector for the size / command */
	iov[0].iov_base = (void *) data;
	iov[0].iov_len  = sl;
	iovcnt = 1;
	if (len >= 0 && buf != NULL)
	{
		iov[1].iov_base = (void *) buf;
		iov[1].iov_len  = len;
		iovcnt = 2;
	}

	l = retry_writev(sd, iov, iovcnt, timeout);
	if (l == MI_FAILURE)
		return MI_FAILURE;
	return MI_SUCCESS;
}

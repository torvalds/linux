/*
 * Copyright (C) 2004 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef AIO_H__
#define AIO_H__

enum aio_type { AIO_READ, AIO_WRITE, AIO_MMAP };

struct aio_thread_reply {
	void *data;
	int err;
};

struct aio_context {
	enum aio_type type;
	int fd;
	void *data;
	int len;
	unsigned long long offset;
	int reply_fd;
	struct aio_context *next;
};

#define INIT_AIO(aio_type, aio_fd, aio_data, aio_len, aio_offset, \
		 aio_reply_fd) \
	{ .type 	= aio_type, \
	  .fd		= aio_fd, \
	  .data		= aio_data, \
	  .len		= aio_len, \
	  .offset	= aio_offset, \
	  .reply_fd	= aio_reply_fd }

#define INIT_AIO_CONTEXT { .reply_fd	= -1, \
			   .next	= NULL }

extern int submit_aio(struct aio_context *aio);

#endif

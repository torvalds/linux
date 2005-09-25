/*
 * Copyright (C) 2004 Jeff Dike (jdike@addtoit.com)
 * Licensed under the GPL
 */

#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <sched.h>
#include <sys/syscall.h>
#include "os.h"
#include "helper.h"
#include "aio.h"
#include "init.h"
#include "user.h"
#include "mode.h"

static int aio_req_fd_r = -1;
static int aio_req_fd_w = -1;

static int update_aio(struct aio_context *aio, int res)
{
        if(res < 0)
                aio->len = res;
        else if((res == 0) && (aio->type == AIO_READ)){
                /* This is the EOF case - we have hit the end of the file
                 * and it ends in a partial block, so we fill the end of
                 * the block with zeros and claim success.
                 */
                memset(aio->data, 0, aio->len);
                aio->len = 0;
        }
        else if(res > 0){
                aio->len -= res;
                aio->data += res;
                aio->offset += res;
                return aio->len;
        }

        return 0;
}

#if defined(HAVE_AIO_ABI)
#include <linux/aio_abi.h>

/* If we have the headers, we are going to build with AIO enabled.
 * If we don't have aio in libc, we define the necessary stubs here.
 */

#if !defined(HAVE_AIO_LIBC)

static long io_setup(int n, aio_context_t *ctxp)
{
        return syscall(__NR_io_setup, n, ctxp);
}

static long io_submit(aio_context_t ctx, long nr, struct iocb **iocbpp)
{
        return syscall(__NR_io_submit, ctx, nr, iocbpp);
}

static long io_getevents(aio_context_t ctx_id, long min_nr, long nr,
                         struct io_event *events, struct timespec *timeout)
{
        return syscall(__NR_io_getevents, ctx_id, min_nr, nr, events, timeout);
}

#endif

/* The AIO_MMAP cases force the mmapped page into memory here
 * rather than in whatever place first touches the data.  I used
 * to do this by touching the page, but that's delicate because
 * gcc is prone to optimizing that away.  So, what's done here
 * is we read from the descriptor from which the page was
 * mapped.  The caller is required to pass an offset which is
 * inside the page that was mapped.  Thus, when the read
 * returns, we know that the page is in the page cache, and
 * that it now backs the mmapped area.
 */

static int do_aio(aio_context_t ctx, struct aio_context *aio)
{
        struct iocb iocb, *iocbp = &iocb;
        char c;
        int err;

        iocb = ((struct iocb) { .aio_data 	= (unsigned long) aio,
                                .aio_reqprio	= 0,
                                .aio_fildes	= aio->fd,
                                .aio_buf	= (unsigned long) aio->data,
                                .aio_nbytes	= aio->len,
                                .aio_offset	= aio->offset,
                                .aio_reserved1	= 0,
                                .aio_reserved2	= 0,
                                .aio_reserved3	= 0 });

        switch(aio->type){
        case AIO_READ:
                iocb.aio_lio_opcode = IOCB_CMD_PREAD;
                break;
        case AIO_WRITE:
                iocb.aio_lio_opcode = IOCB_CMD_PWRITE;
                break;
        case AIO_MMAP:
                iocb.aio_lio_opcode = IOCB_CMD_PREAD;
                iocb.aio_buf = (unsigned long) &c;
                iocb.aio_nbytes = sizeof(c);
                break;
        default:
                printk("Bogus op in do_aio - %d\n", aio->type);
                err = -EINVAL;
                goto out;
        }

        err = io_submit(ctx, 1, &iocbp);
        if(err > 0)
                err = 0;
	else
		err = -errno;

 out:
        return err;
}

static aio_context_t ctx = 0;

static int aio_thread(void *arg)
{
        struct aio_thread_reply reply;
        struct aio_context *aio;
        struct io_event event;
        int err, n;

        signal(SIGWINCH, SIG_IGN);

        while(1){
                n = io_getevents(ctx, 1, 1, &event, NULL);
                if(n < 0){
                        if(errno == EINTR)
                                continue;
                        printk("aio_thread - io_getevents failed, "
                               "errno = %d\n", errno);
                }
                else {
			/* This is safe as we've just a pointer here. */
			aio = (struct aio_context *) (long) event.data;
			if(update_aio(aio, event.res)){
				do_aio(ctx, aio);
				continue;
			}

                        reply = ((struct aio_thread_reply)
				{ .data = aio,
				  .err	= aio->len });
			err = os_write_file(aio->reply_fd, &reply,
					    sizeof(reply));
                        if(err != sizeof(reply))
				printk("aio_thread - write failed, "
				       "fd = %d, err = %d\n", aio->reply_fd,
				       -err);
                }
        }
        return 0;
}

#endif

static int do_not_aio(struct aio_context *aio)
{
        char c;
        int err;

        switch(aio->type){
        case AIO_READ:
                err = os_seek_file(aio->fd, aio->offset);
                if(err)
                        goto out;

                err = os_read_file(aio->fd, aio->data, aio->len);
                break;
        case AIO_WRITE:
                err = os_seek_file(aio->fd, aio->offset);
                if(err)
                        goto out;

                err = os_write_file(aio->fd, aio->data, aio->len);
                break;
        case AIO_MMAP:
                err = os_seek_file(aio->fd, aio->offset);
                if(err)
                        goto out;

                err = os_read_file(aio->fd, &c, sizeof(c));
                break;
        default:
                printk("do_not_aio - bad request type : %d\n", aio->type);
                err = -EINVAL;
                break;
        }

 out:
        return err;
}

static int not_aio_thread(void *arg)
{
        struct aio_context *aio;
        struct aio_thread_reply reply;
        int err;

        signal(SIGWINCH, SIG_IGN);
        while(1){
                err = os_read_file(aio_req_fd_r, &aio, sizeof(aio));
                if(err != sizeof(aio)){
                        if(err < 0)
                                printk("not_aio_thread - read failed, "
                                       "fd = %d, err = %d\n", aio_req_fd_r,
                                       -err);
                        else {
                                printk("not_aio_thread - short read, fd = %d, "
                                       "length = %d\n", aio_req_fd_r, err);
                        }
                        continue;
                }
 again:
                err = do_not_aio(aio);

                if(update_aio(aio, err))
                        goto again;

                reply = ((struct aio_thread_reply) { .data 	= aio,
                                                     .err	= aio->len });
                err = os_write_file(aio->reply_fd, &reply, sizeof(reply));
                if(err != sizeof(reply))
                        printk("not_aio_thread - write failed, fd = %d, "
                               "err = %d\n", aio_req_fd_r, -err);
        }
}

static int submit_aio_24(struct aio_context *aio)
{
        int err;

        err = os_write_file(aio_req_fd_w, &aio, sizeof(aio));
        if(err == sizeof(aio))
                err = 0;

        return err;
}

static int aio_pid = -1;
static int (*submit_proc)(struct aio_context *aio);

static int init_aio_24(void)
{
        unsigned long stack;
        int fds[2], err;

        err = os_pipe(fds, 1, 1);
        if(err)
                goto out;

        aio_req_fd_w = fds[0];
        aio_req_fd_r = fds[1];
        err = run_helper_thread(not_aio_thread, NULL,
                                CLONE_FILES | CLONE_VM | SIGCHLD, &stack, 0);
        if(err < 0)
                goto out_close_pipe;

        aio_pid = err;
        goto out;

 out_close_pipe:
        os_close_file(fds[0]);
        os_close_file(fds[1]);
        aio_req_fd_w = -1;
        aio_req_fd_r = -1;
 out:
#ifndef HAVE_AIO_ABI
	printk("/usr/include/linux/aio_abi.h not present during build\n");
#endif
	printk("2.6 host AIO support not used - falling back to I/O "
	       "thread\n");

	submit_proc = submit_aio_24;

        return 0;
}

#ifdef HAVE_AIO_ABI
#define DEFAULT_24_AIO 0
static int submit_aio_26(struct aio_context *aio)
{
	struct aio_thread_reply reply;
	int err;

	err = do_aio(ctx, aio);
	if(err){
		reply = ((struct aio_thread_reply) { .data = aio,
					             .err  = err });
		err = os_write_file(aio->reply_fd, &reply, sizeof(reply));
		if(err != sizeof(reply))
			printk("submit_aio_26 - write failed, "
			       "fd = %d, err = %d\n", aio->reply_fd, -err);
		else err = 0;
	}

	return err;
}

static int init_aio_26(void)
{
        unsigned long stack;
        int err;

        if(io_setup(256, &ctx)){
		err = -errno;
                printk("aio_thread failed to initialize context, err = %d\n",
                       errno);
                return err;
        }

        err = run_helper_thread(aio_thread, NULL,
                                CLONE_FILES | CLONE_VM | SIGCHLD, &stack, 0);
        if(err < 0)
                return err;

        aio_pid = err;

	printk("Using 2.6 host AIO\n");

	submit_proc = submit_aio_26;

        return 0;
}

#else
#define DEFAULT_24_AIO 1
static int submit_aio_26(struct aio_context *aio)
{
        return -ENOSYS;
}

static int init_aio_26(void)
{
	submit_proc = submit_aio_26;
        return -ENOSYS;
}
#endif

static int aio_24 = DEFAULT_24_AIO;

static int __init set_aio_24(char *name, int *add)
{
        aio_24 = 1;
        return 0;
}

__uml_setup("aio=2.4", set_aio_24,
"aio=2.4\n"
"    This is used to force UML to use 2.4-style AIO even when 2.6 AIO is\n"
"    available.  2.4 AIO is a single thread that handles one request at a\n"
"    time, synchronously.  2.6 AIO is a thread which uses the 2.6 AIO \n"
"    interface to handle an arbitrary number of pending requests.  2.6 AIO \n"
"    is not available in tt mode, on 2.4 hosts, or when UML is built with\n"
"    /usr/include/linux/aio_abi.h not available.  Many distributions don't\n"
"    include aio_abi.h, so you will need to copy it from a kernel tree to\n"
"    your /usr/include/linux in order to build an AIO-capable UML\n\n"
);

static int init_aio(void)
{
        int err;

        CHOOSE_MODE(({
                if(!aio_24){
                        printk("Disabling 2.6 AIO in tt mode\n");
                        aio_24 = 1;
                } }), (void) 0);

        if(!aio_24){
                err = init_aio_26();
                if(err && (errno == ENOSYS)){
                        printk("2.6 AIO not supported on the host - "
                               "reverting to 2.4 AIO\n");
                        aio_24 = 1;
                }
                else return err;
        }

        if(aio_24)
                return init_aio_24();

        return 0;
}

/* The reason for the __initcall/__uml_exitcall asymmetry is that init_aio
 * needs to be called when the kernel is running because it calls run_helper,
 * which needs get_free_page.  exit_aio is a __uml_exitcall because the generic
 * kernel does not run __exitcalls on shutdown, and can't because many of them
 * break when called outside of module unloading.
 */
__initcall(init_aio);

static void exit_aio(void)
{
        if(aio_pid != -1)
                os_kill_process(aio_pid, 1);
}

__uml_exitcall(exit_aio);

int submit_aio(struct aio_context *aio)
{
	return (*submit_proc)(aio);
}

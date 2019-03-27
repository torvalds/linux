/*
 * util/tube.h - pipe service
 *
 * Copyright (c) 2008, NLnet Labs. All rights reserved.
 *
 * This software is open source.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 
 * Neither the name of the NLNET LABS nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * \file
 *
 * This file contains pipe service functions.
 */

#ifndef UTIL_TUBE_H
#define UTIL_TUBE_H
struct comm_reply;
struct comm_point;
struct comm_base;
struct tube;
struct tube_res_list;
#ifdef USE_WINSOCK
#include "util/locks.h"
#endif

/**
 * Callback from pipe listen function
 * void mycallback(tube, msg, len, error, user_argument);
 * if error is true (NETEVENT_*), msg is probably NULL.
 */
typedef void tube_callback_type(struct tube*, uint8_t*, size_t, int, void*);

/**
 * A pipe
 */
struct tube {
#ifndef USE_WINSOCK
	/** pipe end to read from */
	int sr;
	/** pipe end to write on */
	int sw;

	/** listen commpoint */
	struct comm_point* listen_com;
	/** listen callback */
	tube_callback_type* listen_cb;
	/** listen callback user arg */
	void* listen_arg;
	/** are we currently reading a command, 0 if not, else bytecount */
	size_t cmd_read;
	/** size of current read command, may be partially read */
	uint32_t cmd_len;
	/** the current read command content, malloced, can be partially read*/
	uint8_t* cmd_msg;

	/** background write queue, commpoint to write results back */
	struct comm_point* res_com;
	/** are we currently writing a result, 0 if not, else bytecount into
	 * the res_list first entry. */
	size_t res_write;
	/** list of outstanding results to be written back */
	struct tube_res_list* res_list;
	/** last in list */
	struct tube_res_list* res_last;

#else /* USE_WINSOCK */
	/** listen callback */
	tube_callback_type* listen_cb;
	/** listen callback user arg */
	void* listen_arg;
	/** the windows sockets event (signaled if items in pipe) */
	WSAEVENT event;
	/** winsock event storage when registered with event base */
	struct ub_event* ev_listen;

	/** lock on the list of outstanding items */
	lock_basic_type res_lock;
	/** list of outstanding results on pipe */
	struct tube_res_list* res_list;
	/** last in list */
	struct tube_res_list* res_last;
#endif /* USE_WINSOCK */
};

/**
 * List of results (arbitrary command serializations) to write back
 */
struct tube_res_list {
	/** next in list */
	struct tube_res_list* next;
	/** serialized buffer to write */
	uint8_t* buf;
	/** length to write */
	uint32_t len;
};

/**
 * Create a pipe
 * @return: new tube struct or NULL on error.
 */
struct tube* tube_create(void);

/**
 * Delete and destroy a pipe
 * @param tube: to delete
 */
void tube_delete(struct tube* tube);

/**
 * Write length bytes followed by message.
 * @param tube: the tube to write on.
 *     If that tube is a pipe, its write fd is used as
 *     the socket to write on. Is nonblocking.
 *      Set to blocking by the function,
 *      and back to non-blocking at exit of function.
 * @param buf: the message.
 * @param len: length of message.
 * @param nonblock: if set to true, the first write is nonblocking.
 *      If the first write fails the function returns -1.
 *      If set false, the first write is blocking.
 * @return: all remainder writes are nonblocking.
 *      return 0 on error, in that case blocking/nonblocking of socket is
 *              unknown.
 *      return 1 if all OK.
 */
int tube_write_msg(struct tube* tube, uint8_t* buf, uint32_t len, 
	int nonblock);

/**
 * Read length bytes followed by message.
 * @param tube: The tube to read on.
 *     If that tube is a pipe, its read fd is used as
 *     the socket to read on. Is nonblocking.
 *      Set to blocking by the function,
 *      and back to non-blocking at exit of function.
 * @param buf: the message, malloced.
 * @param len: length of message, returned.
 * @param nonblock: if set to true, the first read is nonblocking.
 *      If the first read fails the function returns -1.
 *      If set false, the first read is blocking.
 * @return: all remainder reads are nonblocking.
 *      return 0 on error, in that case blocking/nonblocking of socket is 
 *              unknown. On EOF 0 is returned.
 *      return 1 if all OK.
 */
int tube_read_msg(struct tube* tube, uint8_t** buf, uint32_t* len, 
	int nonblock);

/**
 * Close read part of the pipe.
 * The tube can no longer be read from.
 * @param tube: tube to operate on.
 */
void tube_close_read(struct tube* tube);

/**
 * Close write part of the pipe.
 * The tube can no longer be written to.
 * @param tube: tube to operate on.
 */
void tube_close_write(struct tube* tube);

/**
 * See if data is ready for reading on the tube without blocking.
 * @param tube: tube to check for readable items
 * @return true if readable items are present. False if not (or error).
 *     true on pipe_closed.
 */
int tube_poll(struct tube* tube);

/**
 * Wait for data to be ready for reading on the tube. is blocking.
 * No timeout.
 * @param tube: the tube to wait on.
 * @return: if there was something to read (false on error).
 *     true on pipe_closed.
 */
int tube_wait(struct tube* tube);

/**
 * Get FD that is readable when new information arrives.
 * @param tube
 * @return file descriptor.
 */
int tube_read_fd(struct tube* tube);

/**
 * Start listening for information over the pipe.
 * Background registration of a read listener, callback when read completed.
 * Do not mix with tube_read_msg style direct reads from the pipe.
 * @param tube: tube to listen on
 * @param base: what base to register event callback.
 * @param cb: callback routine.
 * @param arg: user argument for callback routine.
 * @return true if successful, false on error.
 */
int tube_setup_bg_listen(struct tube* tube, struct comm_base* base,
	tube_callback_type* cb, void* arg);

/**
 * Remove bg listen setup from event base.
 * @param tube: what tube to cleanup
 */
void tube_remove_bg_listen(struct tube* tube);

/**
 * Start background write handler for the pipe.
 * Do not mix with tube_write_msg style direct writes to the pipe.
 * @param tube: tube to write on
 * @param base: what base to register event handler on.
 * @return true if successful, false on error.
 */
int tube_setup_bg_write(struct tube* tube, struct comm_base* base);

/**
 * Remove bg write setup from event base.
 * @param tube: what tube to cleanup
 */
void tube_remove_bg_write(struct tube* tube);


/**
 * Append data item to background list of writes.
 * Mallocs a list entry behind the scenes.
 * Not locked behind the scenes, call from one thread or lock on outside.
 * @param tube: what tube to queue on.
 * @param msg: memory message to send. Is free()d after use.
 * 	Put at the end of the to-send queue.
 * @param len: length of item.
 * @return 0 on failure (msg freed).
 */
int tube_queue_item(struct tube* tube, uint8_t* msg, size_t len);

/** for fptr wlist, callback function */
int tube_handle_listen(struct comm_point* c, void* arg, int error, 
	struct comm_reply* reply_info);

/** for fptr wlist, callback function */
int tube_handle_write(struct comm_point* c, void* arg, int error, 
	struct comm_reply* reply_info);

/** for fptr wlist, winsock signal event callback function */
void tube_handle_signal(int fd, short events, void* arg);

#endif /* UTIL_TUBE_H */

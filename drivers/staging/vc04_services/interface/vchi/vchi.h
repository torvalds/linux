/**
 * Copyright (c) 2010-2012 Broadcom. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of the above-listed copyright holders may not be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2, as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef VCHI_H_
#define VCHI_H_

#include "interface/vchi/vchi_cfg.h"
#include "interface/vchi/vchi_common.h"
#include "interface/vchi/connections/connection.h"
#include "vchi_mh.h"

/******************************************************************************
 Global defs
 *****************************************************************************/

#define VCHI_BULK_ROUND_UP(x)     ((((unsigned long)(x))+VCHI_BULK_ALIGN-1) & ~(VCHI_BULK_ALIGN-1))
#define VCHI_BULK_ROUND_DOWN(x)   (((unsigned long)(x)) & ~(VCHI_BULK_ALIGN-1))
#define VCHI_BULK_ALIGN_NBYTES(x) (VCHI_BULK_ALIGNED(x) ? 0 : (VCHI_BULK_ALIGN - ((unsigned long)(x) & (VCHI_BULK_ALIGN-1))))

#ifdef USE_VCHIQ_ARM
#define VCHI_BULK_ALIGNED(x)      1
#else
#define VCHI_BULK_ALIGNED(x)      (((unsigned long)(x) & (VCHI_BULK_ALIGN-1)) == 0)
#endif

struct vchi_version {
	uint32_t version;
	uint32_t version_min;
};
#define VCHI_VERSION(v_) { v_, v_ }
#define VCHI_VERSION_EX(v_, m_) { v_, m_ }

typedef enum {
   VCHI_VEC_POINTER,
   VCHI_VEC_HANDLE,
   VCHI_VEC_LIST
} VCHI_MSG_VECTOR_TYPE_T;

typedef struct vchi_msg_vector_ex {

   VCHI_MSG_VECTOR_TYPE_T type;
   union {
      // a memory handle
      struct {
         VCHI_MEM_HANDLE_T handle;
         uint32_t offset;
         int32_t vec_len;
      } handle;

      // an ordinary data pointer
      struct {
         const void *vec_base;
         int32_t vec_len;
      } ptr;

      // a nested vector list
      struct {
         struct vchi_msg_vector_ex *vec;
         uint32_t vec_len;
      } list;
   } u;
} VCHI_MSG_VECTOR_EX_T;

// Construct an entry in a msg vector for a pointer (p) of length (l)
#define VCHI_VEC_POINTER(p,l)  VCHI_VEC_POINTER, { { (VCHI_MEM_HANDLE_T)(p), (l) } }

// Construct an entry in a msg vector for a message handle (h), starting at offset (o) of length (l)
#define VCHI_VEC_HANDLE(h,o,l) VCHI_VEC_HANDLE,  { { (h), (o), (l) } }

// Macros to manipulate 'FOURCC' values
#define MAKE_FOURCC(x) ((int32_t)( (x[0] << 24) | (x[1] << 16) | (x[2] << 8) | x[3] ))
#define FOURCC_TO_CHAR(x) (x >> 24) & 0xFF,(x >> 16) & 0xFF,(x >> 8) & 0xFF, x & 0xFF

// Opaque service information
struct opaque_vchi_service_t;

// Descriptor for a held message. Allocated by client, initialised by vchi_msg_hold,
// vchi_msg_iter_hold or vchi_msg_iter_hold_next. Fields are for internal VCHI use only.
typedef struct {
   struct opaque_vchi_service_t *service;
   void *message;
} VCHI_HELD_MSG_T;

// structure used to provide the information needed to open a server or a client
typedef struct {
	struct vchi_version version;
	int32_t service_id;
	VCHI_CONNECTION_T *connection;
	uint32_t rx_fifo_size;
	uint32_t tx_fifo_size;
	VCHI_CALLBACK_T callback;
	void *callback_param;
	/* client intends to receive bulk transfers of
		odd lengths or into unaligned buffers */
	int32_t want_unaligned_bulk_rx;
	/* client intends to transmit bulk transfers of
		odd lengths or out of unaligned buffers */
	int32_t want_unaligned_bulk_tx;
	/* client wants to check CRCs on (bulk) xfers.
		Only needs to be set at 1 end - will do both directions. */
	int32_t want_crc;
} SERVICE_CREATION_T;

// Opaque handle for a VCHI instance
typedef struct opaque_vchi_instance_handle_t *VCHI_INSTANCE_T;

// Opaque handle for a server or client
typedef struct opaque_vchi_service_handle_t *VCHI_SERVICE_HANDLE_T;

// Service registration & startup
typedef void (*VCHI_SERVICE_INIT)(VCHI_INSTANCE_T initialise_instance, VCHI_CONNECTION_T **connections, uint32_t num_connections);

typedef struct service_info_tag {
   const char * const vll_filename; /* VLL to load to start this service. This is an empty string if VLL is "static" */
   VCHI_SERVICE_INIT init;          /* Service initialisation function */
   void *vll_handle;                /* VLL handle; NULL when unloaded or a "static VLL" in build */
} SERVICE_INFO_T;

/******************************************************************************
 Global funcs - implementation is specific to which side you are on (local / remote)
 *****************************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

extern /*@observer@*/ VCHI_CONNECTION_T * vchi_create_connection( const VCHI_CONNECTION_API_T * function_table,
                                                   const VCHI_MESSAGE_DRIVER_T * low_level);

// Routine used to initialise the vchi on both local + remote connections
extern int32_t vchi_initialise( VCHI_INSTANCE_T *instance_handle );

extern int32_t vchi_exit( void );

extern int32_t vchi_connect( VCHI_CONNECTION_T **connections,
                             const uint32_t num_connections,
                             VCHI_INSTANCE_T instance_handle );

//When this is called, ensure that all services have no data pending.
//Bulk transfers can remain 'queued'
extern int32_t vchi_disconnect( VCHI_INSTANCE_T instance_handle );

// Global control over bulk CRC checking
extern int32_t vchi_crc_control( VCHI_CONNECTION_T *connection,
                                 VCHI_CRC_CONTROL_T control );

// helper functions
extern void * vchi_allocate_buffer(VCHI_SERVICE_HANDLE_T handle, uint32_t *length);
extern void vchi_free_buffer(VCHI_SERVICE_HANDLE_T handle, void *address);
extern uint32_t vchi_current_time(VCHI_INSTANCE_T instance_handle);

/******************************************************************************
 Global service API
 *****************************************************************************/
// Routine to create a named service
extern int32_t vchi_service_create( VCHI_INSTANCE_T instance_handle,
                                    SERVICE_CREATION_T *setup,
                                    VCHI_SERVICE_HANDLE_T *handle );

// Routine to destroy a service
extern int32_t vchi_service_destroy( const VCHI_SERVICE_HANDLE_T handle );

// Routine to open a named service
extern int32_t vchi_service_open( VCHI_INSTANCE_T instance_handle,
                                  SERVICE_CREATION_T *setup,
                                  VCHI_SERVICE_HANDLE_T *handle);

extern int32_t vchi_get_peer_version( const VCHI_SERVICE_HANDLE_T handle,
                                      short *peer_version );

// Routine to close a named service
extern int32_t vchi_service_close( const VCHI_SERVICE_HANDLE_T handle );

// Routine to increment ref count on a named service
extern int32_t vchi_service_use( const VCHI_SERVICE_HANDLE_T handle );

// Routine to decrement ref count on a named service
extern int32_t vchi_service_release( const VCHI_SERVICE_HANDLE_T handle );

// Routine to set a control option for a named service
extern int32_t vchi_service_set_option( const VCHI_SERVICE_HANDLE_T handle,
					VCHI_SERVICE_OPTION_T option,
					int value);

/* Routine to send a message from kernel memory across a service */
extern int
vchi_queue_kernel_message(VCHI_SERVICE_HANDLE_T handle,
			  void *data,
			  unsigned int size);

/* Routine to send a message from user memory across a service */
extern int
vchi_queue_user_message(VCHI_SERVICE_HANDLE_T handle,
			void __user *data,
			unsigned int size);

// Routine to receive a msg from a service
// Dequeue is equivalent to hold, copy into client buffer, release
extern int32_t vchi_msg_dequeue( VCHI_SERVICE_HANDLE_T handle,
                                 void *data,
                                 uint32_t max_data_size_to_read,
                                 uint32_t *actual_msg_size,
                                 VCHI_FLAGS_T flags );

// Routine to look at a message in place.
// The message is not dequeued, so a subsequent call to peek or dequeue
// will return the same message.
extern int32_t vchi_msg_peek( VCHI_SERVICE_HANDLE_T handle,
                              void **data,
                              uint32_t *msg_size,
                              VCHI_FLAGS_T flags );

// Routine to remove a message after it has been read in place with peek
// The first message on the queue is dequeued.
extern int32_t vchi_msg_remove( VCHI_SERVICE_HANDLE_T handle );

// Routine to look at a message in place.
// The message is dequeued, so the caller is left holding it; the descriptor is
// filled in and must be released when the user has finished with the message.
extern int32_t vchi_msg_hold( VCHI_SERVICE_HANDLE_T handle,
                              void **data,        // } may be NULL, as info can be
                              uint32_t *msg_size, // } obtained from HELD_MSG_T
                              VCHI_FLAGS_T flags,
                              VCHI_HELD_MSG_T *message_descriptor );

// Initialise an iterator to look through messages in place
extern int32_t vchi_msg_look_ahead( VCHI_SERVICE_HANDLE_T handle,
                                    VCHI_MSG_ITER_T *iter,
                                    VCHI_FLAGS_T flags );

/******************************************************************************
 Global service support API - operations on held messages and message iterators
 *****************************************************************************/

// Routine to get the address of a held message
extern void *vchi_held_msg_ptr( const VCHI_HELD_MSG_T *message );

// Routine to get the size of a held message
extern int32_t vchi_held_msg_size( const VCHI_HELD_MSG_T *message );

// Routine to get the transmit timestamp as written into the header by the peer
extern uint32_t vchi_held_msg_tx_timestamp( const VCHI_HELD_MSG_T *message );

// Routine to get the reception timestamp, written as we parsed the header
extern uint32_t vchi_held_msg_rx_timestamp( const VCHI_HELD_MSG_T *message );

// Routine to release a held message after it has been processed
extern int32_t vchi_held_msg_release( VCHI_HELD_MSG_T *message );

// Indicates whether the iterator has a next message.
extern int32_t vchi_msg_iter_has_next( const VCHI_MSG_ITER_T *iter );

// Return the pointer and length for the next message and advance the iterator.
extern int32_t vchi_msg_iter_next( VCHI_MSG_ITER_T *iter,
                                   void **data,
                                   uint32_t *msg_size );

// Remove the last message returned by vchi_msg_iter_next.
// Can only be called once after each call to vchi_msg_iter_next.
extern int32_t vchi_msg_iter_remove( VCHI_MSG_ITER_T *iter );

// Hold the last message returned by vchi_msg_iter_next.
// Can only be called once after each call to vchi_msg_iter_next.
extern int32_t vchi_msg_iter_hold( VCHI_MSG_ITER_T *iter,
                                   VCHI_HELD_MSG_T *message );

// Return information for the next message, and hold it, advancing the iterator.
extern int32_t vchi_msg_iter_hold_next( VCHI_MSG_ITER_T *iter,
                                        void **data,        // } may be NULL
                                        uint32_t *msg_size, // }
                                        VCHI_HELD_MSG_T *message );

/******************************************************************************
 Global bulk API
 *****************************************************************************/

// Routine to prepare interface for a transfer from the other side
extern int32_t vchi_bulk_queue_receive( VCHI_SERVICE_HANDLE_T handle,
                                        void *data_dst,
                                        uint32_t data_size,
                                        VCHI_FLAGS_T flags,
                                        void *transfer_handle );

// Prepare interface for a transfer from the other side into relocatable memory.
int32_t vchi_bulk_queue_receive_reloc( const VCHI_SERVICE_HANDLE_T handle,
                                       VCHI_MEM_HANDLE_T h_dst,
                                       uint32_t offset,
                                       uint32_t data_size,
                                       const VCHI_FLAGS_T flags,
                                       void * const bulk_handle );

// Routine to queue up data ready for transfer to the other (once they have signalled they are ready)
extern int32_t vchi_bulk_queue_transmit( VCHI_SERVICE_HANDLE_T handle,
                                         const void *data_src,
                                         uint32_t data_size,
                                         VCHI_FLAGS_T flags,
                                         void *transfer_handle );

/******************************************************************************
 Configuration plumbing
 *****************************************************************************/

// function prototypes for the different mid layers (the state info gives the different physical connections)
extern const VCHI_CONNECTION_API_T *single_get_func_table( void );
//extern const VCHI_CONNECTION_API_T *local_server_get_func_table( void );
//extern const VCHI_CONNECTION_API_T *local_client_get_func_table( void );

// declare all message drivers here
const VCHI_MESSAGE_DRIVER_T *vchi_mphi_message_driver_func_table( void );

#ifdef __cplusplus
}
#endif

extern int32_t vchi_bulk_queue_transmit_reloc( VCHI_SERVICE_HANDLE_T handle,
                                               VCHI_MEM_HANDLE_T h_src,
                                               uint32_t offset,
                                               uint32_t data_size,
                                               VCHI_FLAGS_T flags,
                                               void *transfer_handle );
#endif /* VCHI_H_ */

/****************************** End of file **********************************/

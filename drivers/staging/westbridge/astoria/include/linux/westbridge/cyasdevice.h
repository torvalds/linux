/* Cypress West Bridge API header file (cyasdevice.h)
## ===========================
## Copyright (C) 2010  Cypress Semiconductor
##
## This program is free software; you can redistribute it and/or
## modify it under the terms of the GNU General Public License
## as published by the Free Software Foundation; either version 2
## of the License, or (at your option) any later version.
##
## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with this program; if not, write to the Free Software
## Foundation, Inc., 51 Franklin Street, Fifth Floor
##Boston, MA  02110-1301, USA.
## ===========================
*/

#ifndef __INCLUDED_CYASDEVICE_H__
#define __INCLUDED_CYASDEVICE_H__

#include "cyashal.h"
#include "cyasprotocol.h"
#include "cyasusb.h"
#include "cyasstorage.h"
#include "cyasmtp.h"
#include "cyas_cplus_start.h"

/***********************************
 * West Bridge Constants
 ***********************************/

/* The endpoints used by West Bridge for the P port to S port path */
#define CY_AS_P2S_WRITE_ENDPOINT (0x04)
#define CY_AS_P2S_READ_ENDPOINT (0x08)

/* The endpoint to use for firmware download */
#define CY_AS_FIRMWARE_ENDPOINT	(0x02)

/* The maximum size of the firmware image West Bridge can accept */
#define CY_AS_MAXIMUM_FIRMWARE_SIZE	(24 * 1024)

/* The maximum size of a write for EP0 and EP1 */
#define CY_AS_EP0_MAX_WRITE_SIZE (128)
#define CY_AS_EP1_MAX_WRITE_SIZE (64)

/* The bitfields for the device state value */

/* The device is in StandBy mode */
#define CY_AS_DEVICE_STATE_PIN_STANDBY (0x00000001)
/* The device has been configured */
#define CY_AS_DEVICE_STATE_CONFIGURED (0x00000002)
/* The firmware has been loaded into the device */
#define CY_AS_DEVICE_STATE_FIRMWARE_LOADED (0x00000004)
/* The interrupt module has been initialized */
#define CY_AS_DEVICE_STATE_LOWLEVEL_MODULE (0x00000008)
/* The DMA module has been initialized */
#define CY_AS_DEVICE_STATE_DMA_MODULE (0x00000010)
/* The interrupt module has been initialized */
#define CY_AS_DEVICE_STATE_INTR_MODULE (0x00000020)
/* The storage module has been initialized */
#define CY_AS_DEVICE_STATE_STORAGE_MODULE (0x00000040)
/* The USB module has been initialized */
#define CY_AS_DEVICE_STATE_USB_MODULE (0x00000080)
/* If set, the API wants SCSI messages */
#define CY_AS_DEVICE_STATE_STORAGE_SCSIMSG (0x00000100)
/* If set, an ASYNC storage operation is pending */
#define CY_AS_DEVICE_STATE_STORAGE_ASYNC_PENDING (0x00000200)
/* If set, the USB port is connected */
#define CY_AS_DEVICE_STATE_USB_CONNECTED (0x00000400)
/* If set and USB is connected, it is high speed */
#define CY_AS_DEVICE_STATE_USB_HIGHSPEED (0x00000800)
/* If set, we are in a callback */
#define CY_AS_DEVICE_STATE_IN_CALLBACK (0x00001000)
/* If set, we are processing a setup packet */
#define CY_AS_DEVICE_STATE_IN_SETUP_PACKET (0x00004000)
/* The device was placed in standby via register */
#define CY_AS_DEVICE_STATE_REGISTER_STANDBY (0x00008000)
/* If set, the device is using a crystal */
#define CY_AS_DEVICE_STATE_CRYSTAL (0x00010000)
/* If set, wakeup has been called */
#define CY_AS_DEVICE_STATE_WAKING (0x00020000)
/* If set, EP0 has been stalled. */
#define CY_AS_DEVICE_STATE_EP0_STALLED (0x00040000)
/* If set, device is in suspend mode. */
#define CY_AS_DEVICE_STATE_SUSPEND (0x00080000)
/* If set, device is a reset is pending. */
#define CY_AS_DEVICE_STATE_RESETP (0x00100000)
/* If set, device is a standby is pending. */
#define CY_AS_DEVICE_STATE_STANDP (0x00200000)
/* If set, device has a storage start or stop pending. */
#define CY_AS_DEVICE_STATE_SSSP	(0x00400000)
/* If set, device has a usb start or stop pending. */
#define CY_AS_DEVICE_STATE_USSP	(0x00800000)
/* If set, device has a mtp start or stop pending. */
#define CY_AS_DEVICE_STATE_MSSP	(0x01000000)
/* If set, P2S DMA transfer can be started. */
#define CY_AS_DEVICE_STATE_P2SDMA_START (0x02000000)

/* The bitfields for the endpoint state value */
/* DMA requests are accepted into the queue */
#define CY_AS_DMA_ENDPOINT_STATE_ENABLED (0x0001)
/* The endpoint has a sleeping client, waiting on a queue drain */
#define CY_AS_DMA_ENDPOINT_STATE_SLEEPING (0x0002)
/* The DMA backend to hardware is running */
#define CY_AS_DMA_ENDPOINT_STATE_DMA_RUNNING (0x0004)
/* There is an outstanding DMA entry deployed to the HAL */
#define CY_AS_DMA_ENDPOINT_STATE_IN_TRANSIT (0x0008)
/* 0 = OUT (West Bridge -> P Port), 1 = IN (P Port -> West Bridge) */
#define CY_AS_DMA_ENDPOINT_STATE_DIRECTION (0x0010)

/* The state values for the request list */
/* Mask for getting the state information */
#define CY_AS_REQUEST_LIST_STATE_MASK (0x0f)
/* The request is queued, nothing further */
#define CY_AS_REQUEST_LIST_STATE_QUEUED (0x00)
/* The request is sent, waiting for response */
#define CY_AS_REQUEST_LIST_STATE_WAITING (0x01)
/* The response has been received, processing reponse */
#define CY_AS_REQUEST_LIST_STATE_RECEIVED (0x02)
/* The request/response is being canceled */
#define CY_AS_REQUEST_LIST_STATE_CANCELING (0x03)
/* The request is synchronous */
#define CY_AS_REQUEST_LIST_STATE_SYNC (0x80)

/* The flag values for a LL RequestResponse */
/* This request requires an ACK to be sent after it is completed */
#define CY_AS_REQUEST_RESPONSE_DELAY_ACK (0x01)
/* This request originated from a version V1.1 function call */
#define CY_AS_REQUEST_RESPONSE_EX (0x02)
/* This request originated from a version V1.2 function call */
#define CY_AS_REQUEST_RESPONSE_MS (0x04)


#define CY_AS_DEVICE_HANDLE_SIGNATURE (0x01211219)

/*
 * This macro returns the endpoint pointer given the
 * device pointer and an endpoint number
 */
#define CY_AS_NUM_EP(dev_p, num) ((dev_p)->endp[(num)])

/****************************************
 * West Bridge Data Structures
 ****************************************/

typedef struct cy_as_device cy_as_device ;

/* Summary
   This type defines a callback function that will be called
   on completion of a DMA operation.

   Description
   This function definition is for a function that is called when
   the DMA operation is complete. This function is called with the
   endpoint number, operation type, buffer pointer and size.

   See Also
   * CyAsDmaOper
   * CyAsDmaQueueWrite
 */
typedef void (*cy_as_dma_callback)(
	/* The device that completed DMA */
	cy_as_device *dev_p,
	/* The endpoint that completed DMA */
	cy_as_end_point_number_t ep,
	/* The pointer to the buffer that completed DMA */
	void *mem_p,
	/* The amount of data transferred */
	uint32_t size,
	/* The error code for this DMA xfer */
	cy_as_return_status_t error
	) ;

/* Summary
   This structure defines a DMA request that is queued

   Description
   This structure contains the information about a DMA
   request that is queued and is to be sent when possible.
*/
typedef struct cy_as_dma_queue_entry {
	/* Pointer to memory buffer for this request */
	void *buf_p ;
	/* Size of the memory buffer for DMA operation */
	uint32_t size ;
	/* Offset into memory buffer for next DMA operation */
	uint32_t offset ;
	/* If TRUE and IN request */
	cy_bool packet ;
	/* If TRUE, this is a read request */
	cy_bool	readreq ;
	/* Callback function for when DMA is complete */
	cy_as_dma_callback	cb ;
	/* Pointer to next entry in queue */
	struct cy_as_dma_queue_entry *next_p ;
} cy_as_dma_queue_entry ;

/* Summary
   This structure defines the endpoint data for a given

   Description
   This structure defines all of the information required
   to manage DMA for a given endpoint.
*/
typedef struct cy_as_dma_end_point {
	/* The endpoint number */
	cy_as_end_point_number_t ep ;
	/* The state of this endpoint */
	uint8_t	state ;
	/* The maximum amount of data accepted in a packet by the hw */
	uint16_t maxhwdata ;
	/* The maximum amount of data accepted by the HAL layer */
	uint32_t maxhaldata ;
	/* The queue for DMA operations */
	cy_as_dma_queue_entry *queue_p ;
	/* The last entry in the DMA queue */
	cy_as_dma_queue_entry *last_p ;
	/* This sleep channel is used to wait while the DMA queue
	 * drains for a given endpoint */
	cy_as_hal_sleep_channel			 channel ;
} cy_as_dma_end_point ;

#define cy_as_end_point_number_is_usb(n) \
	((n) != 2 && (n) != 4 && (n) != 6 && (n) != 8)
#define cy_as_end_point_number_is_storage(n) \
	((n) == 2 || (n) == 4 || (n) == 6 || (n) == 8)

#define cy_as_dma_end_point_is_enabled(ep) \
	((ep)->state & CY_AS_DMA_ENDPOINT_STATE_ENABLED)
#define cy_as_dma_end_point_enable(ep) \
	((ep)->state |= CY_AS_DMA_ENDPOINT_STATE_ENABLED)
#define cy_as_dma_end_point_disable(ep) \
	((ep)->state &= ~CY_AS_DMA_ENDPOINT_STATE_ENABLED)

#define cy_as_dma_end_point_is_sleeping(ep) \
	((ep)->state & CY_AS_DMA_ENDPOINT_STATE_SLEEPING)
#define cy_as_dma_end_point_set_sleep_state(ep) \
	((ep)->state |= CY_AS_DMA_ENDPOINT_STATE_SLEEPING)
#define cy_as_dma_end_point_set_wake_state(ep) \
	((ep)->state &= ~CY_AS_DMA_ENDPOINT_STATE_SLEEPING)

#define cy_as_dma_end_point_is_running(ep) \
	((ep)->state & CY_AS_DMA_ENDPOINT_STATE_DMA_RUNNING)
#define cy_as_dma_end_point_set_running(ep) \
	((ep)->state |= CY_AS_DMA_ENDPOINT_STATE_DMA_RUNNING)
#define cy_as_dma_end_point_set_stopped(ep) \
	((ep)->state &= ~CY_AS_DMA_ENDPOINT_STATE_DMA_RUNNING)

#define cy_as_dma_end_point_in_transit(ep) \
	((ep)->state & CY_AS_DMA_ENDPOINT_STATE_IN_TRANSIT)
#define cy_as_dma_end_point_set_in_transit(ep) \
	((ep)->state |= CY_AS_DMA_ENDPOINT_STATE_IN_TRANSIT)
#define cy_as_dma_end_point_clear_in_transit(ep) \
	((ep)->state &= ~CY_AS_DMA_ENDPOINT_STATE_IN_TRANSIT)

#define cy_as_dma_end_point_is_direction_in(ep) \
	(((ep)->state & CY_AS_DMA_ENDPOINT_STATE_DIRECTION) == \
		CY_AS_DMA_ENDPOINT_STATE_DIRECTION)
#define cy_as_dma_end_point_is_direction_out(ep) \
	(((ep)->state & CY_AS_DMA_ENDPOINT_STATE_DIRECTION) == 0)
#define cy_as_dma_end_point_set_direction_in(ep) \
	((ep)->state |= CY_AS_DMA_ENDPOINT_STATE_DIRECTION)
#define cy_as_dma_end_point_set_direction_out(ep) \
	((ep)->state &= ~CY_AS_DMA_ENDPOINT_STATE_DIRECTION)

#define cy_as_dma_end_point_is_usb(p) \
	cy_as_end_point_number_is_usb((p)->ep)
#define cy_as_dma_end_point_is_storage(p) \
	cy_as_end_point_number_is_storage((p)->ep)

typedef struct cy_as_ll_request_response {
	/* The mbox[0] contents - see low level comm section of API doc */
	uint16_t	box0 ;
	/* The amount of data stored in this request/response in bytes */
	uint16_t	stored ;
	/* Length of this request in words */
	uint16_t	length ;
	/* Additional status information about the request */
	uint16_t	flags ;
	/* Note: This is over indexed and contains the request/response data */
	uint16_t	data[1] ;
} cy_as_ll_request_response ;

/*
 * The callback function for responses
 */
typedef void (*cy_as_response_callback)(
	/* The device that had the response */
	cy_as_device *dev_p,
	/* The context receiving a response */
	uint8_t						 context,
	/* The request data */
	cy_as_ll_request_response *rqt,
	/* The response data */
	cy_as_ll_request_response *resp,
	/* The status of the request */
	cy_as_return_status_t status
	) ;

typedef struct cy_as_ll_request_list_node {
	/* The request to send */
	cy_as_ll_request_response *rqt ;
	/* The associated response for the request */
	cy_as_ll_request_response *resp ;
	/* Length of the response */
	uint16_t						length ;
	/* The callback to call when done */
	cy_as_response_callback			callback ;
	/* The state of the request */
	uint8_t						 state ;
	/* The next request in the list */
	struct cy_as_ll_request_list_node *next ;
} cy_as_ll_request_list_node ;

#define cy_as_request_get_node_state(node_p) \
	((node_p)->state & CY_AS_REQUEST_LIST_STATE_MASK)
#define cy_as_request_set_node_state(node_p, st) \
	((node_p)->state = \
	((node_p)->state & ~CY_AS_REQUEST_LIST_STATE_MASK) | (st))

#define cy_as_request_node_is_sync(node_p) \
	((node_p)->state & CY_AS_REQUEST_LIST_STATE_SYNC)
#define cy_as_request_node_set_sync(node_p) \
	((node_p)->state |= CY_AS_REQUEST_LIST_STATE_SYNC)
#define cy_as_request_node_clear_sync(node_p) \
	((node_p)->state &= ~CY_AS_REQUEST_LIST_STATE_SYNC)

#ifndef __doxygen__
typedef enum cy_as_c_b_node_type {
	CYAS_INVALID,
	CYAS_USB_FUNC_CB,
	CYAS_USB_IO_CB,
	CYAS_STORAGE_IO_CB,
	CYAS_FUNC_CB
} cy_as_c_b_node_type ;

typedef struct cy_as_func_c_b_node {
	cy_as_c_b_node_type			  node_type ;
	cy_as_function_callback		cb_p ;
	uint32_t					client_data ;
	cy_as_funct_c_b_type			 data_type ;
	void	*data ;
	struct cy_as_func_c_b_node *next_p ;
} cy_as_func_c_b_node;

extern cy_as_func_c_b_node*
cy_as_create_func_c_b_node_data(cy_as_function_callback
	cb, uint32_t client, cy_as_funct_c_b_type type, void *data) ;

extern cy_as_func_c_b_node*
cy_as_create_func_c_b_node(cy_as_function_callback cb,
	uint32_t client) ;

extern void
cy_as_destroy_func_c_b_node(cy_as_func_c_b_node *node) ;

typedef struct cy_as_mtp_func_c_b_node {
	cy_as_c_b_node_type			  type ;
	cy_as_mtp_function_callback	 cb_p ;
	uint32_t					client_data;
	struct cy_as_mtp_func_c_b_node *next_p ;
} cy_as_mtp_func_c_b_node;

extern cy_as_mtp_func_c_b_node*
cy_as_create_mtp_func_c_b_node(cy_as_mtp_function_callback cb,
	uint32_t client) ;

extern void
cy_as_destroy_mtp_func_c_b_node(cy_as_mtp_func_c_b_node *node) ;

typedef struct cy_as_usb_func_c_b_node {
	cy_as_c_b_node_type	type ;
	cy_as_usb_function_callback	 cb_p ;
	uint32_t client_data;
	struct cy_as_usb_func_c_b_node *next_p ;
} cy_as_usb_func_c_b_node;

extern cy_as_usb_func_c_b_node*
cy_as_create_usb_func_c_b_node(cy_as_usb_function_callback cb,
	uint32_t client) ;

extern void
cy_as_destroy_usb_func_c_b_node(cy_as_usb_func_c_b_node *node) ;

typedef struct cy_as_usb_io_c_b_node {
	cy_as_c_b_node_type			  type ;
	cy_as_usb_io_callback		   cb_p ;
	struct cy_as_usb_io_c_b_node *next_p ;
} cy_as_usb_io_c_b_node;

extern cy_as_usb_io_c_b_node*
cy_as_create_usb_io_c_b_node(cy_as_usb_io_callback cb) ;

extern void
cy_as_destroy_usb_io_c_b_node(cy_as_usb_io_c_b_node *node) ;

typedef struct cy_as_storage_io_c_b_node {
	cy_as_c_b_node_type			  type ;
	cy_as_storage_callback		 cb_p ;
	/* The media for the currently outstanding async storage request */
	cy_as_media_type			   media ;
	/* The device index for the currently outstanding async storage
	 * request */
	uint32_t					device_index ;
	/* The unit index for the currently outstanding async storage
	 * request */
	uint32_t					unit ;
	/* The block address for the currently outstanding async storage
	 * request */
	uint32_t					block_addr ;
	/* The operation for the currently outstanding async storage
	 * request */
	cy_as_oper_type				oper ;
	cy_as_ll_request_response *req_p ;
	cy_as_ll_request_response *reply_p ;
	struct cy_as_storage_io_c_b_node *next_p ;
} cy_as_storage_io_c_b_node;

extern cy_as_storage_io_c_b_node*
cy_as_create_storage_io_c_b_node(cy_as_storage_callback cb,
	cy_as_media_type media, uint32_t device_index,
	uint32_t unit, uint32_t block_addr, cy_as_oper_type oper,
	cy_as_ll_request_response *req_p,
	cy_as_ll_request_response *reply_p) ;

extern void
cy_as_destroy_storage_io_c_b_node(cy_as_storage_io_c_b_node *node) ;

typedef struct cy_as_c_b_queue {
	void *head_p;
	void *tail_p;
	uint32_t count ;
	cy_as_c_b_node_type type ;
} cy_as_c_b_queue ;

extern cy_as_c_b_queue *
cy_as_create_c_b_queue(cy_as_c_b_node_type type) ;

extern void
cy_as_destroy_c_b_queue(cy_as_c_b_queue *queue) ;

/* Allocates a new CyAsCBNode */
extern void
cy_as_insert_c_b_node(cy_as_c_b_queue *queue_p, void *cbnode) ;

/* Removes the first CyAsCBNode from the queue and frees it */
extern void
cy_as_remove_c_b_node(cy_as_c_b_queue *queue_p) ;

/* Remove the last CyAsCBNode from the queue and frees it */
extern void
cy_as_remove_c_b_tail_node(cy_as_c_b_queue *queue_p) ;

/* Removes and frees all pending callbacks */
extern void
cy_as_clear_c_b_queue(cy_as_c_b_queue *queue_p) ;

extern cy_as_return_status_t
cy_as_misc_send_request(cy_as_device *dev_p,
					  cy_as_function_callback cb,
					  uint32_t client,
					  cy_as_funct_c_b_type type,
					  void *data,
					  cy_as_c_b_queue *queue,
					  uint16_t req_type,
					  cy_as_ll_request_response *req_p,
					  cy_as_ll_request_response *reply_p,
					  cy_as_response_callback rcb) ;

extern void
cy_as_misc_cancel_ex_requests(cy_as_device *dev_p) ;

/* Summary
   Free all memory allocated by and zero all
   structures initialized by CyAsUsbStart.
 */
extern void
cy_as_usb_cleanup(
		cy_as_device *dev_p) ;

/* Summary
   Free all memory allocated and zero all structures initialized
   by CyAsStorageStart.
 */
extern void
cy_as_storage_cleanup(
		cy_as_device *dev_p) ;
#endif

/* Summary
   This structure defines the data structure to support a
   given command context

   Description
   All commands send to the West Bridge device via the mailbox
   registers are sent via a context.Each context is independent
   and there can be a parallel stream of requests and responses on
   each context.  This structure is used to manage a single context.
*/
typedef struct cy_as_context {
	/* The context number for this context */
	uint8_t						 number ;
	/* This sleep channel is used to sleep while waiting on a
	 * response from the west bridge device for a request. */
	cy_as_hal_sleep_channel			 channel ;
	/* The buffer for received requests */
	cy_as_ll_request_response *req_p ;
	/* The length of the request being received */
	uint16_t request_length ;
	/* The callback for the next request received */
	cy_as_response_callback	request_callback ;
	/* A list of low level requests to go to the firmware */
	cy_as_ll_request_list_node *request_queue_p ;
	/* The list node in the request queue */
	cy_as_ll_request_list_node *last_node_p ;
	/* Index upto which data is stored. */
	uint16_t queue_index ;
	/* Index to the next request in the queue. */
	uint16_t rqt_index ;
	/* Queue of data stored */
	uint16_t data_queue[128] ;

} cy_as_context ;

#define cy_as_context_is_waiting(ctxt) \
	((ctxt)->state & CY_AS_CTXT_STATE_WAITING_RESPONSE)
#define cy_as_context_set_waiting(ctxt) \
	((ctxt)->state |= CY_AS_CTXT_STATE_WAITING_RESPONSE)
#define cy_as_context_clear_waiting(ctxt) \
	((ctxt)->state &= ~CY_AS_CTXT_STATE_WAITING_RESPONSE)



/* Summary
   This data structure stores SDIO function
   parameters for a SDIO card

   Description
*/
typedef struct cy_as_sdio_device {
	/* Keeps track of IO functions initialized*/
	uint8_t	 function_init_map;
	uint8_t	 function_suspended_map;
	/* Function 0 (Card Common) properties*/
	cy_as_sdio_card card;
	/* Function 1-7 (Mapped to array element 0-6) properties.*/
	cy_as_sdio_func function[7];

} cy_as_sdio_device;

/* Summary
Macros to access the SDIO card properties
*/

#define cy_as_sdio_get_function_code(handle, bus, i) \
	(((cy_as_device *)handle)->sdiocard[bus].function[i-1].function_code)

#define cy_as_sdio_get_function_ext_code(handle, bus, i)	\
	(((cy_as_device *)handle)->sdiocard[bus].\
		function[i-1].extended_func_code)

#define cy_as_sdio_get_function_p_s_n(handle, bus, i) \
	(((cy_as_device *)handle)->sdiocard[bus].function[i-1].card_psn)

#define cy_as_sdio_get_function_blocksize(handle, bus, i)  \
	(((cy_as_device *)handle)->sdiocard[bus].function[i-1].blocksize)

#define cy_as_sdio_get_function_max_blocksize(handle, bus, i) \
	(((cy_as_device *)handle)->sdiocard[bus].function[i-1].maxblocksize)

#define cy_as_sdio_get_function_csa_support(handle, bus, i) \
	(((cy_as_device *)handle)->sdiocard[bus].function[i-1].csa_bits)

#define cy_as_sdio_get_function_wakeup_support(handle, bus, i) \
	(((cy_as_device *)handle)->sdiocard[bus].function[i-1]. wakeup_support)

#define cy_as_sdio_set_function_block_size(handle, bus, i, blocksize) \
	(((cy_as_device *)handle)->sdiocard[bus].function[i-1].blocksize = \
	blocksize)

#define cy_as_sdio_get_card_num_functions(handle, bus) \
	(((cy_as_device *)handle)->sdiocard[bus].card.num_functions)

#define cy_as_sdio_get_card_mem_present(handle, bus) \
	(((cy_as_device *)handle)->sdiocard[bus].card.memory_present)

#define cy_as_sdio_get_card_manf_id(handle, bus)	\
	(((cy_as_device *)handle)->sdiocard[bus].card.manufacturer__id)

#define cy_as_sdio_get_card_manf_info(handle, bus) \
	(((cy_as_device *)handle)->sdiocard[bus].card.manufacturer_info)

#define cy_as_sdio_get_card_blocksize(handle, bus) \
	(((cy_as_device *)handle)->sdiocard[bus].card.blocksize)

#define cy_as_sdio_get_card_max_blocksize(handle, bus) \
	(((cy_as_device *)handle)->sdiocard[bus].card.maxblocksize)

#define cy_as_sdio_get_card_sdio_version(handle, bus) \
	(((cy_as_device *)handle)->sdiocard[bus].card.sdio_version)

#define cy_as_sdio_get_card_capability(handle, bus) \
	(((cy_as_device *)handle)->sdiocard[bus].card.card_capability)

#define cy_as_sdio_get_function_init_map(handle, bus) \
	(((cy_as_device *)handle)->sdiocard[bus].function_init_map)

#define cy_as_sdio_check_function_initialized(handle, bus, i) \
	(((cy_as_sdio_get_function_init_map(handle, bus)) & (0x01<<i)) ? 1 : 0)

#define cy_as_sdio_set_card_block_size(handle, bus, blocksize) \
	(((cy_as_device *)handle)->sdiocard[bus].card.blocksize = blocksize)

#define cy_as_sdio_check_support_bus_suspend(handle, bus) \
	((cy_as_sdio_get_card_capability(handle, bus) & CY_SDIO_SBS) ? 1 : 0)

#define cy_as_sdio_check_function_suspended(handle, bus, i) \
	((((cy_as_device *)handle)->sdiocard[bus].function_suspended_map & \
		(0x01<<i)) ? 1 : 0)

#define cy_as_sdio_set_function_suspended(handle, bus, i) \
	((((cy_as_device *)handle)->sdiocard[bus].function_suspended_map) \
	|= (0x01<<i))

#define cy_as_sdio_clear_function_suspended(handle, bus, i) \
	((((cy_as_device *)handle)->sdiocard[bus].function_suspended_map) \
	&= (~(0x01<<i)))

/* Summary
   This data structure represents a single device.

   Description
*/
struct cy_as_device {
	/* General stuff */
	/* A signature to insure we have a valid handle */
	uint32_t sig ;
	/* The ID of the silicon */
	uint16_t silicon_id ;
	/* Pointer to the next device */
	struct cy_as_device *next_p ;
	/* This is the client specific tag for this device */
	cy_as_hal_device_tag tag ;
	/* This contains various state information about the device */
	uint32_t state ;
	/* Flag indicating whether INT# pin is used for DRQ */
	cy_bool	use_int_drq ;

	/* DMA related */
	/* The endpoint pointers associated with this device */
	cy_as_dma_end_point	 *endp[16] ;
	/* List of queue entries that can be used for xfers */
	cy_as_dma_queue_entry *dma_freelist_p ;

	/* Low level comm related */
	/* The contexts available in this device */
	cy_as_context *context[CY_RQT_CONTEXT_COUNT] ;
	/* The low level error returned from sending an async request */
	cy_as_return_status_t ll_error ;
	/* A request is currently being sent to West Bridge. */
	cy_bool	ll_sending_rqt ;
	/* The current mailbox request should be aborted. */
	cy_bool	ll_abort_curr_rqt ;
	/* Indicates that the LL layer has queued mailbox data. */
	cy_bool	ll_queued_data ;

	/* MISC API related */
	/* Misc callback */
	cy_as_misc_event_callback misc_event_cb ;

	/* Storage Related */
	/* The reference count for the Storage API */
	uint32_t storage_count ;
	/* Callback for storage events */
	cy_as_storage_event_callback_dep	storage_event_cb ;
	/* V1.2+ callback for storage events */
	cy_as_storage_event_callback  storage_event_cb_ms ;
	/* The error for a sleeping storage operation */
	cy_as_return_status_t		  storage_error ;
	/* Flag indicating that the storage stack is waiting for an operation */
	cy_bool					  storage_wait ;
	/* Request used for storage read/writes. */
	cy_as_ll_request_response *storage_rw_req_p ;
	/* Response used for storage read/writes. */
	cy_as_ll_request_response *storage_rw_resp_p ;
	/* The storage callback */
	cy_as_storage_callback_dep storage_cb ;
	/* The V1.2+ storage callback */
	cy_as_storage_callback storage_cb_ms ;
	/* The bus index for the currently outstanding async storage request */
	cy_as_bus_number_t storage_bus_index ;
	/* The device index for the currently outstanding async storage
	 * request */
	uint32_t storage_device_index ;
	/* The unit index for the currently outstanding async storage request */
	uint32_t storage_unit ;
	/* The block address for the currently outstanding async storage
	 * request */
	uint32_t storage_block_addr ;
	/* The operation for the currently outstanding async storage request */
	cy_as_oper_type	storage_oper ;
	/* The endpoint used to read Storage data */
	cy_as_end_point_number_t storage_read_endpoint ;
	/* The endpoint used to write endpoint data */
	cy_as_end_point_number_t storage_write_endpoint ;
	cy_as_device_desc storage_device_info
		[CY_AS_MAX_BUSES][CY_AS_MAX_STORAGE_DEVICES] ;
	/* The information on each device on each bus */

	/* USB Related */
	/* This conatins the endpoint async state */
	uint16_t epasync ;
	/* The reference count for the USB API */
	uint32_t usb_count ;
	/* The physical endpoint configuration */
	uint8_t	usb_phy_config ;
	/* The callbacks for async func calls */
	cy_as_c_b_queue *usb_func_cbs ;
	/* Endpoint configuration information */
	cy_as_usb_end_point_config usb_config[16] ;
	/* The USB callback */
	cy_as_usb_event_callback_dep usb_event_cb ;
	/* The V1.2+ USB callback */
	cy_as_usb_event_callback usb_event_cb_ms ;
	/* The error for a sleeping usb operation */
	cy_as_return_status_t usb_error ;
	/* The USB callback for a pending storage operation */
	cy_as_usb_io_callback usb_cb[16] ;
	/* The buffer pending from a USB operation */
	void *usb_pending_buffer ;
	/* The size of the buffer pending from a USB operation */
	uint32_t usb_pending_size ;
	/* If true, send a short packet */
	cy_bool	usb_spacket[16] ;
	/* The amount of data actually xferred */
	uint32_t usb_actual_cnt ;
	/* EP1OUT and EP1IN config register contents */
	uint8_t	usb_ep1cfg[2] ;
	/* LEP config register contents */
	uint16_t usb_lepcfg[10] ;
	/* PEP config register contents */
	uint16_t usb_pepcfg[4] ;
	/* Buffer for EP0 and EP1 data sent via mailboxes */
	uint8_t *usb_ep_data ;
	/* Used to track how many ack requests are pending */
	uint32_t usb_delay_ack_count ;
	/* Maximum transfer size for USB endpoints. */
	uint32_t usb_max_tx_size ;

	/* Request for sending EP0 data to West Bridge */
	cy_as_ll_request_response *usb_ep0_dma_req ;
	/* Response for EP0 data sent to West Bridge */
	cy_as_ll_request_response *usb_ep0_dma_resp ;
	/* Request for sending EP1 data to West Bridge */
	cy_as_ll_request_response *usb_ep1_dma_req ;
	/* Response for EP1 data sent to West Bridge */
	cy_as_ll_request_response *usb_ep1_dma_resp ;

	cy_as_ll_request_response *usb_ep0_dma_req_save ;
	cy_as_ll_request_response *usb_ep0_dma_resp_save ;

	/* MTP Related */
	/* The reference count for the MTP API */
	uint32_t mtp_count ;
	/* The MTP event callback supplied by the client */
	cy_as_mtp_event_callback mtp_event_cb ;
	/* The current block table to be transfered */
	cy_as_mtp_block_table *tp_blk_tbl ;

	cy_as_c_b_queue *func_cbs_mtp ;
	cy_as_c_b_queue *func_cbs_usb ;
	cy_as_c_b_queue *func_cbs_stor ;
	cy_as_c_b_queue *func_cbs_misc ;
	cy_as_c_b_queue *func_cbs_res ;

	/* The last USB event that was received */
	cy_as_usb_event	usb_last_event ;
	/* Types of storage media supported by the firmware */
	uint8_t	media_supported[CY_AS_MAX_BUSES] ;

	/* SDIO card parameters*/
	cy_as_sdio_device   sdiocard[CY_AS_MAX_BUSES];
	/* if true, MTP enabled Firmware. */
	cy_bool	is_mtp_firmware ;
	/* if true, mailbox message has come already */
	cy_bool	is_mtp_data_pending ;
	/* True between the time an Init was called and
	 * the complete event is generated */
	cy_bool	mtp_turbo_active ;
	/* mbox reported EP 2 data len */
	uint16_t mtp_data_len ;
	/* The error for mtp EP4 write operation */
	cy_as_return_status_t mtp_error ;
	/* mtp send/get operation callback */
	cy_as_function_callback	mtp_cb ;
	/* mtp send/get operation client id */
	uint32_t mtp_client ;
	/* mtp operation type. To be used in callback */
	cy_as_funct_c_b_type mtp_op ;

	/* Firmware is running in P2S only mode. */
	cy_bool	is_storage_only_mode ;
	/* Interrupt mask value during device standby. */
	uint32_t stby_int_mask ;
} ;

#define cy_as_device_is_configured(dp) \
	((dp)->state & CY_AS_DEVICE_STATE_CONFIGURED)
#define cy_as_device_set_configured(dp) \
	((dp)->state |= CY_AS_DEVICE_STATE_CONFIGURED)
#define cy_as_device_set_unconfigured(dp) \
	((dp)->state &= ~CY_AS_DEVICE_STATE_CONFIGURED)

#define cy_as_device_is_dma_running(dp) \
	((dp)->state & CY_AS_DEVICE_STATE_DMA_MODULE)
#define cy_as_device_set_dma_running(dp) \
	((dp)->state |= CY_AS_DEVICE_STATE_DMA_MODULE)
#define cy_as_device_set_dma_stopped(dp) \
	((dp)->state &= ~CY_AS_DEVICE_STATE_DMA_MODULE)

#define cy_as_device_is_low_level_running(dp) \
	((dp)->state & CY_AS_DEVICE_STATE_LOWLEVEL_MODULE)
#define cy_as_device_set_low_level_running(dp) \
	((dp)->state |= CY_AS_DEVICE_STATE_LOWLEVEL_MODULE)
#define cy_as_device_set_low_level_stopped(dp) \
	((dp)->state &= ~CY_AS_DEVICE_STATE_LOWLEVEL_MODULE)

#define cy_as_device_is_intr_running(dp) \
	((dp)->state & CY_AS_DEVICE_STATE_INTR_MODULE)
#define cy_as_device_set_intr_running(dp) \
	((dp)->state |= CY_AS_DEVICE_STATE_INTR_MODULE)
#define cy_as_device_set_intr_stopped(dp) \
	((dp)->state &= ~CY_AS_DEVICE_STATE_INTR_MODULE)

#define cy_as_device_is_firmware_loaded(dp) \
	((dp)->state & CY_AS_DEVICE_STATE_FIRMWARE_LOADED)
#define cy_as_device_set_firmware_loaded(dp) \
	((dp)->state |= CY_AS_DEVICE_STATE_FIRMWARE_LOADED)
#define cy_as_device_set_firmware_not_loaded(dp) \
	((dp)->state &= ~CY_AS_DEVICE_STATE_FIRMWARE_LOADED)

#define cy_as_device_is_storage_running(dp) \
	((dp)->state & CY_AS_DEVICE_STATE_STORAGE_MODULE)
#define cy_as_device_set_storage_running(dp) \
	((dp)->state |= CY_AS_DEVICE_STATE_STORAGE_MODULE)
#define cy_as_device_set_storage_stopped(dp) \
	((dp)->state &= ~CY_AS_DEVICE_STATE_STORAGE_MODULE)

#define cy_as_device_is_usb_running(dp) \
	((dp)->state & CY_AS_DEVICE_STATE_USB_MODULE)
#define cy_as_device_set_usb_running(dp) \
	((dp)->state |= CY_AS_DEVICE_STATE_USB_MODULE)
#define cy_as_device_set_usb_stopped(dp) \
	((dp)->state &= ~CY_AS_DEVICE_STATE_USB_MODULE)

#define cy_as_device_wants_scsi_messages(dp) \
	(((dp)->state & CY_AS_DEVICE_STATE_STORAGE_SCSIMSG) \
	? cy_true : cy_false)
#define cy_as_device_set_scsi_messages(dp) \
	((dp)->state |= CY_AS_DEVICE_STATE_STORAGE_SCSIMSG)
#define cy_as_device_clear_scsi_messages(dp) \
	((dp)->state &= ~CY_AS_DEVICE_STATE_STORAGE_SCSIMSG)

#define cy_as_device_is_storage_async_pending(dp) \
	((dp)->state & CY_AS_DEVICE_STATE_STORAGE_ASYNC_PENDING)
#define cy_as_device_set_storage_async_pending(dp) \
	((dp)->state |= CY_AS_DEVICE_STATE_STORAGE_ASYNC_PENDING)
#define cy_as_device_clear_storage_async_pending(dp) \
	((dp)->state &= ~CY_AS_DEVICE_STATE_STORAGE_ASYNC_PENDING)

#define cy_as_device_is_usb_connected(dp) \
	((dp)->state & CY_AS_DEVICE_STATE_USB_CONNECTED)
#define cy_as_device_set_usb_connected(dp) \
	((dp)->state |= CY_AS_DEVICE_STATE_USB_CONNECTED)
#define cy_as_device_clear_usb_connected(dp) \
	((dp)->state &= ~CY_AS_DEVICE_STATE_USB_CONNECTED)

#define cy_as_device_is_usb_high_speed(dp) \
	((dp)->state & CY_AS_DEVICE_STATE_USB_HIGHSPEED)
#define cy_as_device_set_usb_high_speed(dp) \
	((dp)->state |= CY_AS_DEVICE_STATE_USB_HIGHSPEED)
#define cy_as_device_clear_usb_high_speed(dp) \
	((dp)->state &= ~CY_AS_DEVICE_STATE_USB_HIGHSPEED)

#define cy_as_device_is_in_callback(dp) \
	((dp)->state & CY_AS_DEVICE_STATE_IN_CALLBACK)
#define cy_as_device_set_in_callback(dp) \
	((dp)->state |= CY_AS_DEVICE_STATE_IN_CALLBACK)
#define cy_as_device_clear_in_callback(dp) \
	((dp)->state &= ~CY_AS_DEVICE_STATE_IN_CALLBACK)

#define cy_as_device_is_setup_i_o_performed(dp) \
	((dp)->state & CY_AS_DEVICE_STATE_SETUP_IO_PERFORMED)
#define cy_as_device_set_setup_i_o_performed(dp) \
	((dp)->state |= CY_AS_DEVICE_STATE_SETUP_IO_PERFORMED)
#define cy_as_device_clear_setup_i_o_performed(dp) \
	((dp)->state &= ~CY_AS_DEVICE_STATE_SETUP_IO_PERFORMED)

#define cy_as_device_is_ack_delayed(dp) \
	((dp)->usb_delay_ack_count > 0)
#define cy_as_device_set_ack_delayed(dp) \
	((dp)->usb_delay_ack_count++)
#define cy_as_device_rem_ack_delayed(dp) \
	((dp)->usb_delay_ack_count--)
#define cy_as_device_clear_ack_delayed(dp) \
	((dp)->usb_delay_ack_count = 0)

#define cy_as_device_is_setup_packet(dp) \
	((dp)->state & CY_AS_DEVICE_STATE_IN_SETUP_PACKET)
#define cy_as_device_set_setup_packet(dp) \
	((dp)->state |= CY_AS_DEVICE_STATE_IN_SETUP_PACKET)
#define cy_as_device_clear_setup_packet(dp) \
	((dp)->state &= ~CY_AS_DEVICE_STATE_IN_SETUP_PACKET)

#define cy_as_device_is_ep0_stalled(dp) \
	((dp)->state & CY_AS_DEVICE_STATE_EP0_STALLED)
#define cy_as_device_set_ep0_stalled(dp) \
	((dp)->state |= CY_AS_DEVICE_STATE_EP0_STALLED)
#define cy_as_device_clear_ep0_stalled(dp) \
	((dp)->state &= ~CY_AS_DEVICE_STATE_EP0_STALLED)

#define cy_as_device_is_register_standby(dp) \
	((dp)->state & CY_AS_DEVICE_STATE_REGISTER_STANDBY)
#define cy_as_device_set_register_standby(dp) \
	((dp)->state |= CY_AS_DEVICE_STATE_REGISTER_STANDBY)
#define cy_as_device_clear_register_standby(dp) \
	((dp)->state &= ~CY_AS_DEVICE_STATE_REGISTER_STANDBY)

#define cy_as_device_is_pin_standby(dp) \
	((dp)->state & CY_AS_DEVICE_STATE_PIN_STANDBY)
#define cy_as_device_set_pin_standby(dp) \
	((dp)->state |= CY_AS_DEVICE_STATE_PIN_STANDBY)
#define cy_as_device_clear_pin_standby(dp) \
	((dp)->state &= ~CY_AS_DEVICE_STATE_PIN_STANDBY)

#define cy_as_device_is_crystal(dp) \
	((dp)->state & CY_AS_DEVICE_STATE_CRYSTAL)
#define cy_as_device_is_external_clock(dp) \
	(!((dp)->state & CY_AS_DEVICE_STATE_CRYSTAL))
#define cy_as_device_set_crystal(dp) \
	((dp)->state |= CY_AS_DEVICE_STATE_CRYSTAL)
#define cy_as_device_set_external_clock(dp) \
	((dp)->state &= ~CY_AS_DEVICE_STATE_CRYSTAL)

#define cy_as_device_is_waking(dp) \
	((dp)->state & CY_AS_DEVICE_STATE_WAKING)
#define cy_as_device_set_waking(dp) \
	((dp)->state |= CY_AS_DEVICE_STATE_WAKING)
#define cy_as_device_clear_waking(dp) \
	((dp)->state &= ~CY_AS_DEVICE_STATE_WAKING)

#define cy_as_device_is_in_suspend_mode(dp) \
	((dp)->state & CY_AS_DEVICE_STATE_SUSPEND)
#define cy_as_device_set_suspend_mode(dp) \
	((dp)->state |= CY_AS_DEVICE_STATE_SUSPEND)
#define cy_as_device_clear_suspend_mode(dp) \
	((dp)->state &= ~CY_AS_DEVICE_STATE_SUSPEND)

#define cy_as_device_is_reset_pending(dp) \
	((dp)->state & CY_AS_DEVICE_STATE_RESETP)
#define cy_as_device_set_reset_pending(dp) \
	((dp)->state |= CY_AS_DEVICE_STATE_RESETP)
#define cy_as_device_clear_reset_pending(dp) \
	((dp)->state &= ~CY_AS_DEVICE_STATE_RESETP)

#define cy_as_device_is_standby_pending(dp) \
	((dp)->state & CY_AS_DEVICE_STATE_STANDP)
#define cy_as_device_set_standby_pending(dp) \
	((dp)->state |= CY_AS_DEVICE_STATE_STANDP)
#define cy_as_device_clear_standby_pending(dp) \
	((dp)->state &= ~CY_AS_DEVICE_STATE_STANDP)

#define cy_as_device_is_s_s_s_pending(dp) \
	((dp)->state & CY_AS_DEVICE_STATE_SSSP)
#define cy_as_device_set_s_s_s_pending(dp) \
	((dp)->state |= CY_AS_DEVICE_STATE_SSSP)
#define cy_as_device_clear_s_s_s_pending(dp) \
	((dp)->state &= ~CY_AS_DEVICE_STATE_SSSP)

#define cy_as_device_is_u_s_s_pending(dp) \
	((dp)->state & CY_AS_DEVICE_STATE_USSP)
#define cy_as_device_set_u_s_s_pending(dp) \
	((dp)->state |= CY_AS_DEVICE_STATE_USSP)
#define cy_as_device_clear_u_s_s_pending(dp) \
	((dp)->state &= ~CY_AS_DEVICE_STATE_USSP)

#define cy_as_device_is_m_s_s_pending(dp) \
	((dp)->state & CY_AS_DEVICE_STATE_MSSP)
#define cy_as_device_set_m_s_s_pending(dp) \
	((dp)->state |= CY_AS_DEVICE_STATE_MSSP)
#define cy_as_device_clear_m_s_s_pending(dp) \
	((dp)->state &= ~CY_AS_DEVICE_STATE_MSSP)

#define cy_as_device_is_p2s_dma_start_recvd(dp) \
	((dp)->state & CY_AS_DEVICE_STATE_P2SDMA_START)
#define cy_as_device_set_p2s_dma_start_recvd(dp) \
	((dp)->state |= CY_AS_DEVICE_STATE_P2SDMA_START)
#define cy_as_device_clear_p2s_dma_start_recvd(dp) \
	((dp)->state &= ~CY_AS_DEVICE_STATE_P2SDMA_START)

#define cy_as_device_is_usb_async_pending(dp, ep) \
	((dp)->epasync & (1 << ep))
#define cy_as_device_set_usb_async_pending(dp, ep) \
	((dp)->epasync |= (1 << ep))
#define cy_as_device_clear_usb_async_pending(dp, ep) \
	((dp)->epasync &= ~(1 << ep))

#define cy_as_device_is_nand_storage_supported(dp) \
	((dp)->media_supported[0] & 1)

/* Macros to check the type of West Bridge device. */
#define cy_as_device_is_astoria_dev(dp) \
	(((dp)->silicon_id == CY_AS_MEM_CM_WB_CFG_ID_HDID_ASTORIA_VALUE) || \
	 ((dp)->silicon_id == CY_AS_MEM_CM_WB_CFG_ID_HDID_ASTORIA_FPGA_VALUE))
#define cy_as_device_is_antioch_dev(dp) \
	((dp)->silicon_id == CY_AS_MEM_CM_WB_CFG_ID_HDID_ANTIOCH_VALUE)

#ifdef CY_AS_LOG_SUPPORT
extern void cy_as_log_debug_message(int value, const char *msg) ;
#else
#define cy_as_log_debug_message(value, msg)
#endif

/* Summary
   This function finds the device object given the HAL tag

   Description
   The user associats a device TAG with each West Bridge device
   created.  This tag is passed from the  API functions to and HAL
   functions that need to ID a specific West Bridge device.  This
   tag is also passed in from the user back into the API via
   interrupt functions.  This function allows the API to find the
   device structure associated with a given tag.

   Notes
   This function does a simple linear search for the device based
   on the TAG.  This function is called each time an West Bridge
   interrupt handler is called.  Therefore this works fine for a
   small number of West Bridge devices (e.g. less than five).
   Anything more than this and this methodology will need to be
   updated.

   Returns
   Pointer to a CyAsDevice associated with the tag
*/
extern cy_as_device *
cy_as_device_find_from_tag(
		cy_as_hal_device_tag tag
		) ;

#include "cyas_cplus_end.h"

#endif		  /* __INCLUDED_CYASDEVICE_H__ */

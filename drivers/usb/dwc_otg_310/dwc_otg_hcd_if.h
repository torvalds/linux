/* ==========================================================================
 * $File: //dwh/usb_iip/dev/software/otg/linux/drivers/dwc_otg_hcd_if.h $
 * $Revision: #12 $
 * $Date: 2011/10/26 $
 * $Change: 1873028 $
 *
 * Synopsys HS OTG Linux Software Driver and documentation (hereinafter,
 * "Software") is an Unsupported proprietary work of Synopsys, Inc. unless
 * otherwise expressly agreed to in writing between Synopsys and you.
 *
 * The Software IS NOT an item of Licensed Software or Licensed Product under
 * any End User Software License Agreement or Agreement for Licensed Product
 * with Synopsys or any supplement thereto. You are permitted to use and
 * redistribute this Software in source and binary forms, with or without
 * modification, provided that redistributions of source code must retain this
 * notice. You may not view, use, disclose, copy or distribute this file or
 * any information contained herein except pursuant to this license grant from
 * Synopsys. If you do not agree with this notice, including the disclaimer
 * below, then you are not authorized to use the Software.
 *
 * THIS SOFTWARE IS BEING DISTRIBUTED BY SYNOPSYS SOLELY ON AN "AS IS" BASIS
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE HEREBY DISCLAIMED. IN NO EVENT SHALL SYNOPSYS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 * ========================================================================== */
#ifndef DWC_DEVICE_ONLY
#ifndef __DWC_HCD_IF_H__
#define __DWC_HCD_IF_H__

#include "dwc_otg_core_if.h"

/** @file
 * This file defines DWC_OTG HCD Core API.
 */

struct dwc_otg_hcd;
typedef struct dwc_otg_hcd dwc_otg_hcd_t;

struct dwc_otg_hcd_urb;
typedef struct dwc_otg_hcd_urb dwc_otg_hcd_urb_t;

/** @name HCD Function Driver Callbacks */
/** @{ */

/** This function is called whenever core switches to host mode. */
typedef int (*dwc_otg_hcd_start_cb_t) (dwc_otg_hcd_t * hcd);

/** This function is called when device has been disconnected */
typedef int (*dwc_otg_hcd_disconnect_cb_t) (dwc_otg_hcd_t * hcd);

/** Wrapper provides this function to HCD to core, so it can get hub information to which device is connected */
typedef int (*dwc_otg_hcd_hub_info_from_urb_cb_t) (dwc_otg_hcd_t * hcd,
						   void *urb_handle,
						   uint32_t * hub_addr,
						   uint32_t * port_addr);
/** Via this function HCD core gets device speed */
typedef int (*dwc_otg_hcd_speed_from_urb_cb_t) (dwc_otg_hcd_t * hcd,
						void *urb_handle);

/** This function is called when urb is completed */
typedef int (*dwc_otg_hcd_complete_urb_cb_t) (dwc_otg_hcd_t * hcd,
					      void *urb_handle,
					      dwc_otg_hcd_urb_t * dwc_otg_urb,
					      int32_t status);

/** Via this function HCD core gets b_hnp_enable parameter */
typedef int (*dwc_otg_hcd_get_b_hnp_enable) (dwc_otg_hcd_t * hcd);

struct dwc_otg_hcd_function_ops {
	dwc_otg_hcd_start_cb_t start;
	dwc_otg_hcd_disconnect_cb_t disconnect;
	dwc_otg_hcd_hub_info_from_urb_cb_t hub_info;
	dwc_otg_hcd_speed_from_urb_cb_t speed;
	dwc_otg_hcd_complete_urb_cb_t complete;
	dwc_otg_hcd_get_b_hnp_enable get_b_hnp_enable;
};
/** @} */

/** @name HCD Core API */
/** @{ */
/** This function allocates dwc_otg_hcd structure and returns pointer on it. */
extern dwc_otg_hcd_t *dwc_otg_hcd_alloc_hcd(void);

/** This function should be called to initiate HCD Core.
 *
 * @param hcd The HCD
 * @param core_if The DWC_OTG Core
 *
 * Returns -DWC_E_NO_MEMORY if no enough memory.
 * Returns 0 on success  
 */
extern int dwc_otg_hcd_init(dwc_otg_hcd_t * hcd, dwc_otg_core_if_t * core_if);

/** Frees HCD
 *
 * @param hcd The HCD
 */
extern void dwc_otg_hcd_remove(dwc_otg_hcd_t * hcd);

/** This function should be called on every hardware interrupt.
 *
 * @param dwc_otg_hcd The HCD
 *
 * Returns non zero if interrupt is handled
 * Return 0 if interrupt is not handled
 */
extern int32_t dwc_otg_hcd_handle_intr(dwc_otg_hcd_t * dwc_otg_hcd);

/**
 * Returns private data set by
 * dwc_otg_hcd_set_priv_data function.
 *
 * @param hcd The HCD
 */
extern void *dwc_otg_hcd_get_priv_data(dwc_otg_hcd_t * hcd);

/**
 * Set private data.
 *
 * @param hcd The HCD
 * @param priv_data pointer to be stored in private data
 */
extern void dwc_otg_hcd_set_priv_data(dwc_otg_hcd_t * hcd, void *priv_data);

/**
 * This function initializes the HCD Core.
 *
 * @param hcd The HCD
 * @param fops The Function Driver Operations data structure containing pointers to all callbacks.
 *
 * Returns -DWC_E_NO_DEVICE if Core is currently is in device mode.
 * Returns 0 on success
 */
extern int dwc_otg_hcd_start(dwc_otg_hcd_t * hcd,
			     struct dwc_otg_hcd_function_ops *fops);

/**
 * Halts the DWC_otg host mode operations in a clean manner. USB transfers are
 * stopped. 
 *
 * @param hcd The HCD
 */
extern void dwc_otg_hcd_stop(dwc_otg_hcd_t * hcd);

/**
 * Handles hub class-specific requests.
 *
 * @param dwc_otg_hcd The HCD
 * @param typeReq Request Type
 * @param wValue wValue from control request
 * @param wIndex wIndex from control request
 * @param buf data buffer 
 * @param wLength data buffer length
 *
 * Returns -DWC_E_INVALID if invalid argument is passed
 * Returns 0 on success
 */
extern int dwc_otg_hcd_hub_control(dwc_otg_hcd_t * dwc_otg_hcd,
				   uint16_t typeReq, uint16_t wValue,
				   uint16_t wIndex, uint8_t * buf,
				   uint16_t wLength);

/**
 * Returns otg port number.
 *
 * @param hcd The HCD
 */
extern uint32_t dwc_otg_hcd_otg_port(dwc_otg_hcd_t * hcd);

/**
 * Returns OTG version - either 1.3 or 2.0.
 *
 * @param core_if The core_if structure pointer
 */
extern uint16_t dwc_otg_get_otg_version(dwc_otg_core_if_t * core_if);

/**
 * Returns 1 if currently core is acting as B host, and 0 otherwise.
 *
 * @param hcd The HCD
 */
extern uint32_t dwc_otg_hcd_is_b_host(dwc_otg_hcd_t * hcd);

/**
 * Returns current frame number.
 *
 * @param hcd The HCD
 */
extern int dwc_otg_hcd_get_frame_number(dwc_otg_hcd_t * hcd);

/**
 * Dumps hcd state.
 *
 * @param hcd The HCD
 */
extern void dwc_otg_hcd_dump_state(dwc_otg_hcd_t * hcd);

/**
 * Dump the average frame remaining at SOF. This can be used to
 * determine average interrupt latency. Frame remaining is also shown for
 * start transfer and two additional sample points.
 * Currently this function is not implemented.
 *
 * @param hcd The HCD
 */
extern void dwc_otg_hcd_dump_frrem(dwc_otg_hcd_t * hcd);

/**
 * Sends LPM transaction to the local device.
 *
 * @param hcd The HCD
 * @param devaddr Device Address
 * @param hird Host initiated resume duration
 * @param bRemoteWake Value of bRemoteWake field in LPM transaction
 *
 * Returns negative value if sending LPM transaction was not succeeded.
 * Returns 0 on success.
 */
extern int dwc_otg_hcd_send_lpm(dwc_otg_hcd_t * hcd, uint8_t devaddr,
				uint8_t hird, uint8_t bRemoteWake);

/* URB interface */

/**
 * Allocates memory for dwc_otg_hcd_urb structure.
 * Allocated memory should be freed by call of DWC_FREE.
 *
 * @param hcd The HCD
 * @param iso_desc_count Count of ISOC descriptors
 * @param atomic_alloc Specefies whether to perform atomic allocation.
 */
extern dwc_otg_hcd_urb_t *dwc_otg_hcd_urb_alloc(dwc_otg_hcd_t * hcd,
						int iso_desc_count,
						int atomic_alloc);

/**
 * Set pipe information in URB.
 *
 * @param hcd_urb DWC_OTG URB
 * @param devaddr Device Address
 * @param ep_num Endpoint Number
 * @param ep_type Endpoint Type
 * @param ep_dir Endpoint Direction
 * @param mps Max Packet Size
 */
extern void dwc_otg_hcd_urb_set_pipeinfo(dwc_otg_hcd_urb_t * hcd_urb,
					 uint8_t devaddr, uint8_t ep_num,
					 uint8_t ep_type, uint8_t ep_dir,
					 uint16_t mps);

/* Transfer flags */
#define URB_GIVEBACK_ASAP 0x1
#define URB_SEND_ZERO_PACKET 0x2

/**
 * Sets dwc_otg_hcd_urb parameters.
 *
 * @param urb DWC_OTG URB allocated by dwc_otg_hcd_urb_alloc function.
 * @param urb_handle Unique handle for request, this will be passed back
 * to function driver in completion callback.
 * @param buf The buffer for the data
 * @param dma The DMA buffer for the data
 * @param buflen Transfer length
 * @param sp Buffer for setup data
 * @param sp_dma DMA address of setup data buffer
 * @param flags Transfer flags
 * @param interval Polling interval for interrupt or isochronous transfers.
 */
extern void dwc_otg_hcd_urb_set_params(dwc_otg_hcd_urb_t * urb,
				       void *urb_handle, void *buf,
				       dwc_dma_t dma, uint32_t buflen, void *sp,
				       dwc_dma_t sp_dma, uint32_t flags,
				       uint16_t interval);

/** Gets status from dwc_otg_hcd_urb
 *
 * @param dwc_otg_urb DWC_OTG URB
 */
extern uint32_t dwc_otg_hcd_urb_get_status(dwc_otg_hcd_urb_t * dwc_otg_urb);

/** Gets actual length from dwc_otg_hcd_urb
 *
 * @param dwc_otg_urb DWC_OTG URB
 */
extern uint32_t dwc_otg_hcd_urb_get_actual_length(dwc_otg_hcd_urb_t *
						  dwc_otg_urb);

/** Gets error count from dwc_otg_hcd_urb. Only for ISOC URBs
 *
 * @param dwc_otg_urb DWC_OTG URB
 */
extern uint32_t dwc_otg_hcd_urb_get_error_count(dwc_otg_hcd_urb_t *
						dwc_otg_urb);

/** Set ISOC descriptor offset and length
 *
 * @param dwc_otg_urb DWC_OTG URB
 * @param desc_num ISOC descriptor number
 * @param offset Offset from beginig of buffer.
 * @param length Transaction length
 */
extern void dwc_otg_hcd_urb_set_iso_desc_params(dwc_otg_hcd_urb_t * dwc_otg_urb,
						int desc_num, uint32_t offset,
						uint32_t length);

/** Get status of ISOC descriptor, specified by desc_num
 *
 * @param dwc_otg_urb DWC_OTG URB
 * @param desc_num ISOC descriptor number 
 */
extern uint32_t dwc_otg_hcd_urb_get_iso_desc_status(dwc_otg_hcd_urb_t *
						    dwc_otg_urb, int desc_num);

/** Get actual length of ISOC descriptor, specified by desc_num
 *
 * @param dwc_otg_urb DWC_OTG URB
 * @param desc_num ISOC descriptor number
 */
extern uint32_t dwc_otg_hcd_urb_get_iso_desc_actual_length(dwc_otg_hcd_urb_t *
							   dwc_otg_urb,
							   int desc_num);

/** Queue URB. After transfer is completes, the complete callback will be called with the URB status
 *
 * @param dwc_otg_hcd The HCD
 * @param dwc_otg_urb DWC_OTG URB
 * @param ep_handle Out parameter for returning endpoint handle
 * @param atomic_alloc Flag to do atomic allocation if needed
 *
 * Returns -DWC_E_NO_DEVICE if no device is connected.
 * Returns -DWC_E_NO_MEMORY if there is no enough memory.
 * Returns 0 on success.
 */
extern int dwc_otg_hcd_urb_enqueue(dwc_otg_hcd_t * dwc_otg_hcd,
				   dwc_otg_hcd_urb_t * dwc_otg_urb,
				   void **ep_handle, int atomic_alloc);

/** De-queue the specified URB
 *
 * @param dwc_otg_hcd The HCD
 * @param dwc_otg_urb DWC_OTG URB
 */
extern int dwc_otg_hcd_urb_dequeue(dwc_otg_hcd_t * dwc_otg_hcd,
				   dwc_otg_hcd_urb_t * dwc_otg_urb);

/** Frees resources in the DWC_otg controller related to a given endpoint.
 * Any URBs for the endpoint must already be dequeued.
 *
 * @param hcd The HCD
 * @param ep_handle Endpoint handle, returned by dwc_otg_hcd_urb_enqueue function
 * @param retry Number of retries if there are queued transfers.
 *
 * Returns -DWC_E_INVALID if invalid arguments are passed.
 * Returns 0 on success
 */
extern int dwc_otg_hcd_endpoint_disable(dwc_otg_hcd_t * hcd, void *ep_handle,
					int retry);

/* Resets the data toggle in qh structure. This function can be called from
 * usb_clear_halt routine.
 *
 * @param hcd The HCD
 * @param ep_handle Endpoint handle, returned by dwc_otg_hcd_urb_enqueue function
 *
 * Returns -DWC_E_INVALID if invalid arguments are passed.
 * Returns 0 on success
 */
extern int dwc_otg_hcd_endpoint_reset(dwc_otg_hcd_t * hcd, void *ep_handle);

/** Returns 1 if status of specified port is changed and 0 otherwise.
 *
 * @param hcd The HCD
 * @param port Port number
 */
extern int dwc_otg_hcd_is_status_changed(dwc_otg_hcd_t * hcd, int port);

/** Call this function to check if bandwidth was allocated for specified endpoint.
 * Only for ISOC and INTERRUPT endpoints.
 *
 * @param hcd The HCD
 * @param ep_handle Endpoint handle
 */
extern int dwc_otg_hcd_is_bandwidth_allocated(dwc_otg_hcd_t * hcd,
					      void *ep_handle);

/** Call this function to check if bandwidth was freed for specified endpoint.
 *
 * @param hcd The HCD
 * @param ep_handle Endpoint handle
 */
extern int dwc_otg_hcd_is_bandwidth_freed(dwc_otg_hcd_t * hcd, void *ep_handle);

/** Returns bandwidth allocated for specified endpoint in microseconds.
 * Only for ISOC and INTERRUPT endpoints.
 *
 * @param hcd The HCD
 * @param ep_handle Endpoint handle
 */
extern uint8_t dwc_otg_hcd_get_ep_bandwidth(dwc_otg_hcd_t * hcd,
					    void *ep_handle);

/** @} */

#endif /* __DWC_HCD_IF_H__ */
#endif /* DWC_DEVICE_ONLY */

/*
 * Copyright (c) 2004, 2005 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2005 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

/*
 * Abstract:
 *  Specification of the OpenSM transport API. This API is OpenSM's view
 *  of the Infiniband transport.
 */

#ifndef _OSM_VENDOR_API_H_
#define _OSM_VENDOR_API_H_

#include <opensm/osm_madw.h>
#include <opensm/osm_mad_pool.h>
#include <vendor/osm_vendor.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
/****s* OpenSM Vendor API/osm_vend_mad_recv_callback_t
* NAME
*  osm_vend_mad_recv_callback_t
*
* DESCRIPTION
*  Function prototype for the vendor MAD receive callback.
*  The vendor layer calls this function for MAD receives.
*
* SYNOPSIS
*/
typedef void (*osm_vend_mad_recv_callback_t) (IN osm_madw_t * p_madw,
					      IN void *bind_context,
					      IN osm_madw_t * p_req_madw);
/*
* PARAMETERS
*  p_madw
*     [in] The received MAD wrapper.
*
*  bind_context
*     [in] User context supplied during the bind call.
*
*  p_req_madw
*     [in] Pointer to the request mad wrapper that generated this response.
*     If the inbound MAD is not a response, this field is NULL.
*
* RETURN VALUES
*  None.
*
* NOTES
*
* SEE ALSO
*********/

/****s* OpenSM Vendor API/osm_vend_mad_send_err_callback_t
* NAME
*  osm_vend_mad_send_err_callback_t
*
* DESCRIPTION
*  Function prototype for the vendor send failure callback.
*  The vendor layer calls this function when MADs expecting
*  a response are completed in error, most likely due to a
*  timeout.
*
* SYNOPSIS
*/
typedef void (*osm_vend_mad_send_err_callback_t) (IN void *bind_context,
						  IN osm_madw_t * p_madw);
/*
* PARAMETERS
*  bind_context
*     [in] User context supplied during the bind call.
*
*  p_madw
*     [in] Pointer to the request mad that failed.
*
* RETURN VALUES
*  None.
*
* NOTES
*  The vendor layer does not call this function (or any other)
*  for MADs that were not expecting a response.
*
* SEE ALSO
*********/

/****f* OpenSM Vendor API/osm_vendor_new
* NAME
*  osm_vendor_new
*
* DESCRIPTION
*  Allocates and initializes a new osm_vendor_t object.
*  OpenSM calls this function before any other in the vendor API.
*  This object is passed as a parameter to all other vendor functions.
*
* SYNOPSIS
*/
osm_vendor_t *osm_vendor_new(IN osm_log_t * const p_log,
			     IN const uint32_t timeout);
/*
* PARAMETERS
*  p_log
*     [in] Pointer to the log object to use.
*
*  timeout
*     [in] transaction timeout
*
* RETURN VALUES
*  Returns a pointer to the vendor object.
*
* NOTES
*
* SEE ALSO
*********/

/****s* OpenSM Vendor API/osm_vendor_delete
* NAME
*  osm_vendor_delete
*
* DESCRIPTION
*  Dealocate the vendor object.
*
* SYNOPSIS
*/
void osm_vendor_delete(IN osm_vendor_t ** const pp_vend);
/*
* PARAMETERS
*  pp_vend
*     [in/out] pointer to pointer to vendor objcet to be deleted
*
* RETURN VALUES
*  None
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM Vendor API/osm_vendor_get_all_port_attr
* NAME
*  osm_vendor_get_all_port_attr
*
* DESCRIPTION
*  Returns an array of available port attribute structures.
*
* SYNOPSIS
*/
ib_api_status_t
osm_vendor_get_all_port_attr(IN osm_vendor_t * const p_vend,
			     IN ib_port_attr_t * const p_attr_array,
			     IN uint32_t * const p_num_ports);
/*
* PARAMETERS
*  p_vend
*     [in] Pointer to the vendor object to initialize.
*
*  p_attr_array
*     [in/out] Pointer to pre-allocated array of port attributes.
*     If it is NULL - then the command only updates the p_num_ports,
*     and return IB_INSUFFICIENT_MEMORY.
*
*  p_num_ports
*     [in/out] Pointer to a variable to hold the total number of ports
*     available on the local machine.
*
* RETURN VALUES
*  IB_SUCCESS on success.
*  IB_INSUFFICIENT_MEMORY if the attribute array was not large enough.
*  The number of attributes needed is returned in num_guids.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM Vendor API/osm_vendor_init
* NAME
*  osm_vendor_init
*
* DESCRIPTION
*  The osm_vendor_init function initializes the vendor transport layer.
*
* SYNOPSIS
*/
ib_api_status_t
osm_vendor_init(IN osm_vendor_t * const p_vend, IN osm_log_t * const p_log,
		IN const uint32_t timeout);
/*
* PARAMETERS
*  p_vend
*     [in] Pointer to the vendor object to initialize.
*
*  p_log
*     [in] Pointer to OpenSM's log object.  Vendor code may
*     use the log object to send messages to OpenSM's log.
*
*  timeout
*     [in] Transaction timeout value in milliseconds.
*     A value of 0 disables timeouts.
*
* RETURN VALUE
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM Vendor API/osm_vendor_bind
* NAME
*   osm_vendor_bind
*
* DESCRIPTION
*   The osm_vendor_bind function registers with the vendor transport layer
*   per Mad Class per PortGuid for mad transport capability.
*
* SYNOPSIS
*/
osm_bind_handle_t
osm_vendor_bind(IN osm_vendor_t * const p_vend,
		IN osm_bind_info_t * const p_bind_info,
		IN osm_mad_pool_t * const p_mad_pool,
		IN osm_vend_mad_recv_callback_t mad_recv_callback,
		IN osm_vend_mad_send_err_callback_t send_err_callback,
		IN void *context);
/*
* PARAMETERS
*  p_vend
*    [in] pointer to the vendor object
*
*  p_osm_bind_info
*    [in] pointer to a struct defining the type of bind to perform.
*
*  p_mad_pool
*    [in] pointer to a mad wrappers pool to be used for allocating
*    mad wrappers on send and receive.
*
*  mad_recv_callback
*    [in] the callback function to be invoked on mad receive.
*
*  send_err_callback
*    [in] the callback function to be invoked on mad transaction errors.
*
*  context
*    [in] the context to be provided to the callbacks as bind_ctx.
*
* RETURN VALUE
*  On success, a valid bind handle.
*  OSM_BIND_INVALID_HANDLE otherwise.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM Vendor API/osm_vendor_unbind
* NAME
*   osm_vendor_unbind
*
* DESCRIPTION
*   Unbind the given bind handle (obtained by osm_vendor_bind).
*
* SYNOPSIS
*/
void osm_vendor_unbind(IN osm_bind_handle_t h_bind);
/*
* PARAMETERS
*  h_bind
*    [in] the bind handle to release.
*
* RETURN VALUE
*    NONE.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM Vendor API/osm_vendor_get
* NAME
*   osm_vendor_get
*
* DESCRIPTION
*   Obtain a mad wrapper holding actual mad buffer to be sent via
*   the transport.
*
* SYNOPSIS
*/
ib_mad_t *osm_vendor_get(IN osm_bind_handle_t h_bind,
			 IN const uint32_t mad_size,
			 IN osm_vend_wrap_t * const p_vend_wrap);
/*
* PARAMETERS
*   h_bind
*      [in] the bind handle obtained by calling osm_vendor_bind
*
*   mad_size
*      [in] the actual mad size required
*
*   p_vend_wrap
*      [out] the returned mad vendor wrapper
*
* RETURN VALUE
*   IB_SUCCESS on succesful completion.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM Vendor API/osm_vendor_send
* NAME
*   osm_vendor_send
*
* DESCRIPTION
*
* SYNOPSIS
*/
ib_api_status_t
osm_vendor_send(IN osm_bind_handle_t h_bind,
		IN osm_madw_t * const p_madw, IN boolean_t const resp_expected);
/*
* PARAMETERS
*   h_bind
*      [in] the bind handle obtained by calling osm_vendor_bind
*
*   p_madw
*      [in] pointer to the Mad Wrapper structure for the MAD to be sent.
*
*   resp_expected
*      [in] boolean value declaring the mad as a request (expecting a response).
*
* RETURN VALUE
*   IB_SUCCESS on succesful completion.
*
* NOTES
*   1. Only mads that expect a response are tracked for transaction competion.
*   2. A mad that does not expect a response is being put back immediately
*      after being sent.
*
* SEE ALSO
*********/

/****f* OpenSM Vendor API/osm_vendor_put
* NAME
*   osm_vendor_put
*
* DESCRIPTION
*   Return a mad vendor wrapper to the mad pool. It also means that the
*   mad buffer is returned to the transport.
*
* SYNOPSIS
*/
void
osm_vendor_put(IN osm_bind_handle_t h_bind,
	       IN osm_vend_wrap_t * const p_vend_wrap);
/*
* PARAMETERS
*   h_bind
*      [in] the bind handle obtained by calling osm_vendor_bind
*
*   p_vend_wrap
*      [in] pointer to the mad vendor wrapper to put back into the pool.
*
* RETURN VALUE
*   None.
*
* NOTES
*
* SEE ALSO
*********/

/****i* OpenSM Vendor API/osm_vendor_local_lid_change
* NAME
*   osm_vendor_local_lid_change
*
* DESCRIPTION
*  Notifies the vendor transport layer that the local address
*  has changed.  This allows the vendor layer to perform housekeeping
*  functions such as address vector updates.
*
* SYNOPSIS
*/
ib_api_status_t osm_vendor_local_lid_change(IN osm_bind_handle_t h_bind);
/*
* PARAMETERS
*   h_bind
*      [in] the bind handle obtained by calling osm_vendor_bind
*
* RETURN VALUE
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM Vendor API/osm_vendor_set_sm
* NAME
*   osm_vendor_set_sm
*
* DESCRIPTION
*  Modifies the port info for the bound port to set the "IS_SM" bit
*  according to the value given (TRUE or FALSE).
*
* SYNOPSIS
*/
void osm_vendor_set_sm(IN osm_bind_handle_t h_bind, IN boolean_t is_sm_val);
/*
* PARAMETERS
*   h_bind
*     [in] bind handle for this port.
*
*   is_sm_val
*     [in] If TRUE - will set the is_sm to TRUE, if FALSE - will set the
*          the is_sm to FALSE.
*
* RETURN VALUE
*  None.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM Vendor API/osm_vendor_set_debug
* NAME
*   osm_vendor_set_debug
*
* DESCRIPTION
*  Modifies the vendor specific debug level.
*
* SYNOPSIS
*/
void osm_vendor_set_debug(IN osm_vendor_t * const p_vend, IN int32_t level);
/*
* PARAMETERS
*   p_vend
*     [in] vendor handle.
*
*   level
*     [in] vendor specific debug level.
*
* RETURN VALUE
*  None.
*
* NOTES
*
* SEE ALSO
*********/

END_C_DECLS
#endif				/* _OSM_VENDOR_API_H_ */

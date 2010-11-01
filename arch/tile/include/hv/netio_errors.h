/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

/**
 * Error codes returned from NetIO routines.
 */

#ifndef __NETIO_ERRORS_H__
#define __NETIO_ERRORS_H__

/**
 * @addtogroup error
 *
 * @brief The error codes returned by NetIO functions.
 *
 * NetIO functions return 0 (defined as ::NETIO_NO_ERROR) on success, and
 * a negative value if an error occurs.
 *
 * In cases where a NetIO function failed due to a error reported by
 * system libraries, the error code will be the negation of the
 * system errno at the time of failure.  The @ref netio_strerror()
 * function will deliver error strings for both NetIO and system error
 * codes.
 *
 * @{
 */

/** The set of all NetIO errors. */
typedef enum
{
  /** Operation successfully completed. */
  NETIO_NO_ERROR        = 0,

  /** A packet was successfully retrieved from an input queue. */
  NETIO_PKT             = 0,

  /** Largest NetIO error number. */
  NETIO_ERR_MAX         = -701,

  /** The tile is not registered with the IPP. */
  NETIO_NOT_REGISTERED  = -701,

  /** No packet was available to retrieve from the input queue. */
  NETIO_NOPKT           = -702,

  /** The requested function is not implemented. */
  NETIO_NOT_IMPLEMENTED = -703,

  /** On a registration operation, the target queue already has the maximum
   *  number of tiles registered for it, and no more may be added.  On a
   *  packet send operation, the output queue is full and nothing more can
   *  be queued until some of the queued packets are actually transmitted. */
  NETIO_QUEUE_FULL      = -704,

  /** The calling process or thread is not bound to exactly one CPU. */
  NETIO_BAD_AFFINITY    = -705,

  /** Cannot allocate memory on requested controllers. */
  NETIO_CANNOT_HOME     = -706,

  /** On a registration operation, the IPP specified is not configured
   *  to support the options requested; for instance, the application
   *  wants a specific type of tagged headers which the configured IPP
   *  doesn't support.  Or, the supplied configuration information is
   *  not self-consistent, or is out of range; for instance, specifying
   *  both NETIO_RECV and NETIO_NO_RECV, or asking for more than
   *  NETIO_MAX_SEND_BUFFERS to be preallocated.  On a VLAN or bucket
   *  configure operation, the number of items, or the base item, was
   *  out of range.
   */
  NETIO_BAD_CONFIG      = -707,

  /** Too many tiles have registered to transmit packets. */
  NETIO_TOOMANY_XMIT    = -708,

  /** Packet transmission was attempted on a queue which was registered
      with transmit disabled. */
  NETIO_UNREG_XMIT      = -709,

  /** This tile is already registered with the IPP. */
  NETIO_ALREADY_REGISTERED = -710,

  /** The Ethernet link is down. The application should try again later. */
  NETIO_LINK_DOWN       = -711,

  /** An invalid memory buffer has been specified.  This may be an unmapped
   * virtual address, or one which does not meet alignment requirements.
   * For netio_input_register(), this error may be returned when multiple
   * processes specify different memory regions to be used for NetIO
   * buffers.  That can happen if these processes specify explicit memory
   * regions with the ::NETIO_FIXED_BUFFER_VA flag, or if tmc_cmem_init()
   * has not been called by a common ancestor of the processes.
   */
  NETIO_FAULT           = -712,

  /** Cannot combine user-managed shared memory and cache coherence. */
  NETIO_BAD_CACHE_CONFIG = -713,

  /** Smallest NetIO error number. */
  NETIO_ERR_MIN         = -713,

#ifndef __DOXYGEN__
  /** Used internally to mean that no response is needed; never returned to
   *  an application. */
  NETIO_NO_RESPONSE     = 1
#endif
} netio_error_t;

/** @} */

#endif /* __NETIO_ERRORS_H__ */

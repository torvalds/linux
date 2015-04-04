/**********************************************************************
 * Author: Cavium Networks
 *
 * Contact: support@caviumnetworks.com
 * This file is part of the OCTEON SDK
 *
 * Copyright (c) 2003-2007 Cavium Networks
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 *
 * This file is distributed in the hope that it will be useful, but
 * AS-IS and WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, TITLE, or
 * NONINFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this file; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 * or visit http://www.gnu.org/licenses/.
 *
 * This file may also be available under a different license from Cavium.
 * Contact Cavium Networks for more information
**********************************************************************/

/*
 * A few defines are used to control the operation of this driver:
 *  USE_ASYNC_IOBDMA
 *      Use asynchronous IO access to hardware. This uses Octeon's asynchronous
 *      IOBDMAs to issue IO accesses without stalling. Set this to zero
 *      to disable this. Note that IOBDMAs require CVMSEG.
 *  REUSE_SKBUFFS_WITHOUT_FREE
 *      Allows the TX path to free an skbuff into the FPA hardware pool. This
 *      can significantly improve performance for forwarding and bridging, but
 *      may be somewhat dangerous. Checks are made, but if any buffer is reused
 *      without the proper Linux cleanup, the networking stack may have very
 *      bizarre bugs.
 */
#ifndef __ETHERNET_DEFINES_H__
#define __ETHERNET_DEFINES_H__

#include <asm/octeon/cvmx-config.h>


#define OCTEON_ETHERNET_VERSION "1.9"

#ifdef CONFIG_NETFILTER
#define REUSE_SKBUFFS_WITHOUT_FREE  0
#else
#define REUSE_SKBUFFS_WITHOUT_FREE  1
#endif

/* Enable Random Early Dropping under load */
#define USE_RED                     1
#define USE_ASYNC_IOBDMA            (CONFIG_CAVIUM_OCTEON_CVMSEG_SIZE > 0)

/*
 * Use this to have all FPA frees also tell the L2 not to write data
 * to memory.
 */
#define DONT_WRITEBACK(x)           (x)
/* Use this to not have FPA frees control L2 */
/*#define DONT_WRITEBACK(x)         0   */

/* Maximum number of SKBs to try to free per xmit packet. */
#define MAX_OUT_QUEUE_DEPTH 1000

#define FAU_TOTAL_TX_TO_CLEAN (CVMX_FAU_REG_END - sizeof(uint32_t))
#define FAU_NUM_PACKET_BUFFERS_TO_FREE (FAU_TOTAL_TX_TO_CLEAN - sizeof(uint32_t))

#define TOTAL_NUMBER_OF_PORTS       (CVMX_PIP_NUM_INPUT_PORTS+1)


#endif /* __ETHERNET_DEFINES_H__ */

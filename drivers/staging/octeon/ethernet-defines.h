/*
 * This file is based on code from OCTEON SDK by Cavium Networks.
 *
 * Copyright (c) 2003-2007 Cavium Networks
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 */

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

#ifdef CONFIG_NETFILTER
#define REUSE_SKBUFFS_WITHOUT_FREE  0
#else
#define REUSE_SKBUFFS_WITHOUT_FREE  1
#endif

#define USE_ASYNC_IOBDMA            (CONFIG_CAVIUM_OCTEON_CVMSEG_SIZE > 0)

/* Maximum number of SKBs to try to free per xmit packet. */
#define MAX_OUT_QUEUE_DEPTH 1000

#define FAU_TOTAL_TX_TO_CLEAN (CVMX_FAU_REG_END - sizeof(u32))
#define FAU_NUM_PACKET_BUFFERS_TO_FREE (FAU_TOTAL_TX_TO_CLEAN - sizeof(u32))

#define TOTAL_NUMBER_OF_PORTS       (CVMX_PIP_NUM_INPUT_PORTS+1)

#endif /* __ETHERNET_DEFINES_H__ */

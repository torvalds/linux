/*
 * Copyright 2008 - 2015 Freescale Semiconductor Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Freescale Semiconductor nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __FMAN_PORT_H
#define __FMAN_PORT_H

#include "fman.h"

/* FM Port API
 * The FM uses a general module called "port" to represent a Tx port (MAC),
 * an Rx port (MAC).
 * The number of ports in an FM varies between SOCs.
 * The SW driver manages these ports as sub-modules of the FM,i.e. after an
 * FM is initialized, its ports may be initialized and operated upon.
 * The port is initialized aware of its type, but other functions on a port
 * may be indifferent to its type. When necessary, the driver verifies
 * coherence and returns error if applicable.
 * On initialization, user specifies the port type and it's index (relative
 * to the port's type) - always starting at 0.
 */

/* FM Frame error */
/* Frame Descriptor errors */
/* Not for Rx-Port! Unsupported Format */
#define FM_PORT_FRM_ERR_UNSUPPORTED_FORMAT	FM_FD_ERR_UNSUPPORTED_FORMAT
/* Not for Rx-Port! Length Error */
#define FM_PORT_FRM_ERR_LENGTH			FM_FD_ERR_LENGTH
/* DMA Data error */
#define FM_PORT_FRM_ERR_DMA			FM_FD_ERR_DMA
/* non Frame-Manager error; probably come from SEC that was chained to FM */
#define FM_PORT_FRM_ERR_NON_FM			FM_FD_RX_STATUS_ERR_NON_FM
 /* IPR error */
#define FM_PORT_FRM_ERR_IPRE			(FM_FD_ERR_IPR & ~FM_FD_IPR)
/* IPR non-consistent-sp */
#define FM_PORT_FRM_ERR_IPR_NCSP		(FM_FD_ERR_IPR_NCSP &	\
						~FM_FD_IPR)

/* Rx FIFO overflow, FCS error, code error, running disparity
 * error (SGMII and TBI modes), FIFO parity error.
 * PHY Sequence error, PHY error control character detected.
 */
#define FM_PORT_FRM_ERR_PHYSICAL                FM_FD_ERR_PHYSICAL
/* Frame too long OR Frame size exceeds max_length_frame  */
#define FM_PORT_FRM_ERR_SIZE                    FM_FD_ERR_SIZE
/* indicates a classifier "drop" operation */
#define FM_PORT_FRM_ERR_CLS_DISCARD             FM_FD_ERR_CLS_DISCARD
/* Extract Out of Frame */
#define FM_PORT_FRM_ERR_EXTRACTION              FM_FD_ERR_EXTRACTION
/* No Scheme Selected */
#define FM_PORT_FRM_ERR_NO_SCHEME               FM_FD_ERR_NO_SCHEME
/* Keysize Overflow */
#define FM_PORT_FRM_ERR_KEYSIZE_OVERFLOW        FM_FD_ERR_KEYSIZE_OVERFLOW
/* Frame color is red */
#define FM_PORT_FRM_ERR_COLOR_RED               FM_FD_ERR_COLOR_RED
/* Frame color is yellow */
#define FM_PORT_FRM_ERR_COLOR_YELLOW            FM_FD_ERR_COLOR_YELLOW
/* Parser Time out Exceed */
#define FM_PORT_FRM_ERR_PRS_TIMEOUT             FM_FD_ERR_PRS_TIMEOUT
/* Invalid Soft Parser instruction */
#define FM_PORT_FRM_ERR_PRS_ILL_INSTRUCT        FM_FD_ERR_PRS_ILL_INSTRUCT
/* Header error was identified during parsing */
#define FM_PORT_FRM_ERR_PRS_HDR_ERR             FM_FD_ERR_PRS_HDR_ERR
/* Frame parsed beyind 256 first bytes */
#define FM_PORT_FRM_ERR_BLOCK_LIMIT_EXCEEDED    FM_FD_ERR_BLOCK_LIMIT_EXCEEDED
/* FPM Frame Processing Timeout Exceeded */
#define FM_PORT_FRM_ERR_PROCESS_TIMEOUT         0x00000001

struct fman_port;

/* A structure for additional Rx port parameters */
struct fman_port_rx_params {
	u32 err_fqid;			/* Error Queue Id. */
	u32 dflt_fqid;			/* Default Queue Id. */
	/* Which external buffer pools are used
	 * (up to FMAN_PORT_MAX_EXT_POOLS_NUM), and their sizes.
	 */
	struct fman_ext_pools ext_buf_pools;
};

/* A structure for additional non-Rx port parameters */
struct fman_port_non_rx_params {
	/* Error Queue Id. */
	u32 err_fqid;
	/* For Tx - Default Confirmation queue, 0 means no Tx confirmation
	 * for processed frames. For OP port - default Rx queue.
	 */
	u32 dflt_fqid;
};

/* A union for additional parameters depending on port type */
union fman_port_specific_params {
	/* Rx port parameters structure */
	struct fman_port_rx_params rx_params;
	/* Non-Rx port parameters structure */
	struct fman_port_non_rx_params non_rx_params;
};

/* A structure representing FM initialization parameters */
struct fman_port_params {
	/* Virtual Address of memory mapped FM Port registers. */
	void *fm;
	union fman_port_specific_params specific_params;
	/* Additional parameters depending on port type. */
};

int fman_port_config(struct fman_port *port, struct fman_port_params *params);

int fman_port_init(struct fman_port *port);

int fman_port_cfg_buf_prefix_content(struct fman_port *port,
				     struct fman_buffer_prefix_content
				     *buffer_prefix_content);

int fman_port_disable(struct fman_port *port);

int fman_port_enable(struct fman_port *port);

u32 fman_port_get_qman_channel_id(struct fman_port *port);

struct fman_port *fman_port_bind(struct device *dev);

#endif /* __FMAN_PORT_H */

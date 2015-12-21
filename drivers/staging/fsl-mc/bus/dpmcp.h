/* Copyright 2013-2015 Freescale Semiconductor Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * * Neither the name of the above-listed copyright holders nor the
 * names of any contributors may be used to endorse or promote products
 * derived from this software without specific prior written permission.
 *
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef __FSL_DPMCP_H
#define __FSL_DPMCP_H

/* Data Path Management Command Portal API
 * Contains initialization APIs and runtime control APIs for DPMCP
 */

struct fsl_mc_io;

int dpmcp_open(struct fsl_mc_io *mc_io,
	       uint32_t cmd_flags,
	       int dpmcp_id,
	       uint16_t *token);

/* Get portal ID from pool */
#define DPMCP_GET_PORTAL_ID_FROM_POOL (-1)

int dpmcp_close(struct fsl_mc_io *mc_io,
		uint32_t cmd_flags,
		uint16_t token);

/**
 * struct dpmcp_cfg - Structure representing DPMCP configuration
 * @portal_id:	Portal ID; 'DPMCP_GET_PORTAL_ID_FROM_POOL' to get the portal ID
 *		from pool
 */
struct dpmcp_cfg {
	int portal_id;
};

int dpmcp_create(struct fsl_mc_io	*mc_io,
		 uint32_t		cmd_flags,
		 const struct dpmcp_cfg	*cfg,
		uint16_t		*token);

int dpmcp_destroy(struct fsl_mc_io *mc_io,
		  uint32_t cmd_flags,
		  uint16_t token);

int dpmcp_reset(struct fsl_mc_io *mc_io,
		uint32_t cmd_flags,
		uint16_t token);

/* IRQ */
/* IRQ Index */
#define DPMCP_IRQ_INDEX                             0
/* irq event - Indicates that the link state changed */
#define DPMCP_IRQ_EVENT_CMD_DONE                    0x00000001

/**
 * struct dpmcp_irq_cfg - IRQ configuration
 * @paddr:	Address that must be written to signal a message-based interrupt
 * @val:	Value to write into irq_addr address
 * @user_irq_id: A user defined number associated with this IRQ
 */
struct dpmcp_irq_cfg {
	     uint64_t		paddr;
	     uint32_t		val;
	     int		user_irq_id;
};

int dpmcp_set_irq(struct fsl_mc_io	*mc_io,
		  uint32_t		cmd_flags,
		  uint16_t		token,
		 uint8_t		irq_index,
		  struct dpmcp_irq_cfg	*irq_cfg);

int dpmcp_get_irq(struct fsl_mc_io	*mc_io,
		  uint32_t		cmd_flags,
		  uint16_t		token,
		 uint8_t		irq_index,
		 int			*type,
		 struct dpmcp_irq_cfg	*irq_cfg);

int dpmcp_set_irq_enable(struct fsl_mc_io	*mc_io,
			 uint32_t		cmd_flags,
			 uint16_t		token,
			uint8_t			irq_index,
			uint8_t			en);

int dpmcp_get_irq_enable(struct fsl_mc_io	*mc_io,
			 uint32_t		cmd_flags,
			 uint16_t		token,
			uint8_t			irq_index,
			uint8_t			*en);

int dpmcp_set_irq_mask(struct fsl_mc_io	*mc_io,
		       uint32_t	cmd_flags,
		       uint16_t		token,
		      uint8_t		irq_index,
		      uint32_t		mask);

int dpmcp_get_irq_mask(struct fsl_mc_io	*mc_io,
		       uint32_t	cmd_flags,
		       uint16_t		token,
		      uint8_t		irq_index,
		      uint32_t		*mask);

int dpmcp_get_irq_status(struct fsl_mc_io	*mc_io,
			 uint32_t		cmd_flags,
			 uint16_t		token,
			uint8_t			irq_index,
			uint32_t		*status);

int dpmcp_clear_irq_status(struct fsl_mc_io	*mc_io,
			   uint32_t		cmd_flags,
			   uint16_t		token,
			  uint8_t		irq_index,
			  uint32_t		status);

/**
 * struct dpmcp_attr - Structure representing DPMCP attributes
 * @id:		DPMCP object ID
 * @version:	DPMCP version
 */
struct dpmcp_attr {
	int id;
	/**
	 * struct version - Structure representing DPMCP version
	 * @major:	DPMCP major version
	 * @minor:	DPMCP minor version
	 */
	struct {
		uint16_t major;
		uint16_t minor;
	} version;
};

int dpmcp_get_attributes(struct fsl_mc_io	*mc_io,
			 uint32_t		cmd_flags,
			 uint16_t		token,
			struct dpmcp_attr	*attr);

#endif /* __FSL_DPMCP_H */

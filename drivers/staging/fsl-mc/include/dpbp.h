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
#ifndef __FSL_DPBP_H
#define __FSL_DPBP_H

/* Data Path Buffer Pool API
 * Contains initialization APIs and runtime control APIs for DPBP
 */

struct fsl_mc_io;

int dpbp_open(struct fsl_mc_io *mc_io,
	      uint32_t cmd_flags,
	      int dpbp_id,
	      uint16_t *token);

int dpbp_close(struct fsl_mc_io *mc_io,
	       uint32_t		cmd_flags,
	       uint16_t	token);

/**
 * struct dpbp_cfg - Structure representing DPBP configuration
 * @options:	place holder
 */
struct dpbp_cfg {
	uint32_t options;
};

int dpbp_create(struct fsl_mc_io	*mc_io,
		uint32_t		cmd_flags,
		const struct dpbp_cfg	*cfg,
		uint16_t		*token);

int dpbp_destroy(struct fsl_mc_io *mc_io,
		 uint32_t cmd_flags,
		 uint16_t token);

int dpbp_enable(struct fsl_mc_io *mc_io,
		uint32_t cmd_flags,
		uint16_t token);

int dpbp_disable(struct fsl_mc_io *mc_io,
		 uint32_t cmd_flags,
		 uint16_t token);

int dpbp_is_enabled(struct fsl_mc_io *mc_io,
		    uint32_t cmd_flags,
		    uint16_t token,
		    int *en);

int dpbp_reset(struct fsl_mc_io *mc_io,
	       uint32_t cmd_flags,
	       uint16_t token);

/**
 * struct dpbp_irq_cfg - IRQ configuration
 * @addr:	Address that must be written to signal a message-based interrupt
 * @val:	Value to write into irq_addr address
 * @user_irq_id: A user defined number associated with this IRQ
 */
struct dpbp_irq_cfg {
	     uint64_t		addr;
	     uint32_t		val;
	     int		user_irq_id;
};

int dpbp_set_irq(struct fsl_mc_io	*mc_io,
		 uint32_t		cmd_flags,
		 uint16_t		token,
		 uint8_t		irq_index,
		 struct dpbp_irq_cfg	*irq_cfg);

int dpbp_get_irq(struct fsl_mc_io	*mc_io,
		 uint32_t		cmd_flags,
		 uint16_t		token,
		 uint8_t		irq_index,
		 int			*type,
		 struct dpbp_irq_cfg	*irq_cfg);

int dpbp_set_irq_enable(struct fsl_mc_io	*mc_io,
			uint32_t		cmd_flags,
			uint16_t		token,
			uint8_t			irq_index,
			uint8_t			en);

int dpbp_get_irq_enable(struct fsl_mc_io	*mc_io,
			uint32_t		cmd_flags,
			uint16_t		token,
			uint8_t			irq_index,
			uint8_t			*en);

int dpbp_set_irq_mask(struct fsl_mc_io	*mc_io,
		      uint32_t		cmd_flags,
		      uint16_t		token,
		      uint8_t		irq_index,
		      uint32_t		mask);

int dpbp_get_irq_mask(struct fsl_mc_io	*mc_io,
		      uint32_t		cmd_flags,
		      uint16_t		token,
		      uint8_t		irq_index,
		      uint32_t		*mask);

int dpbp_get_irq_status(struct fsl_mc_io	*mc_io,
			uint32_t		cmd_flags,
			uint16_t		token,
			uint8_t			irq_index,
			uint32_t		*status);

int dpbp_clear_irq_status(struct fsl_mc_io	*mc_io,
			  uint32_t		cmd_flags,
			  uint16_t		token,
			  uint8_t		irq_index,
			  uint32_t		status);

/**
 * struct dpbp_attr - Structure representing DPBP attributes
 * @id:		DPBP object ID
 * @version:	DPBP version
 * @bpid:	Hardware buffer pool ID; should be used as an argument in
 *		acquire/release operations on buffers
 */
struct dpbp_attr {
	int id;
	/**
	 * struct version - Structure representing DPBP version
	 * @major:	DPBP major version
	 * @minor:	DPBP minor version
	 */
	struct {
		uint16_t major;
		uint16_t minor;
	} version;
	uint16_t bpid;
};

int dpbp_get_attributes(struct fsl_mc_io	*mc_io,
			uint32_t	cmd_flags,
			uint16_t		token,
			struct dpbp_attr	*attr);

/** @} */

#endif /* __FSL_DPBP_H */

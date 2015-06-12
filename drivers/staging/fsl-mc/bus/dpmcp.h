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

/**
 * dpmcp_open() - Open a control session for the specified object.
 * @mc_io:	Pointer to MC portal's I/O object
 * @dpmcp_id:	DPMCP unique ID
 * @token:	Returned token; use in subsequent API calls
 *
 * This function can be used to open a control session for an
 * already created object; an object may have been declared in
 * the DPL or by calling the dpmcp_create function.
 * This function returns a unique authentication token,
 * associated with the specific object ID and the specific MC
 * portal; this token must be used in all subsequent commands for
 * this specific object
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpmcp_open(struct fsl_mc_io *mc_io, int dpmcp_id, uint16_t *token);

/* Get portal ID from pool */
#define DPMCP_GET_PORTAL_ID_FROM_POOL (-1)

/**
 * dpmcp_close() - Close the control session of the object
 * @mc_io:	Pointer to MC portal's I/O object
 * @token:	Token of DPMCP object
 *
 * After this function is called, no further operations are
 * allowed on the object without opening a new control session.
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpmcp_close(struct fsl_mc_io *mc_io, uint16_t token);

/**
 * struct dpmcp_cfg() - Structure representing DPMCP configuration
 * @portal_id:	Portal ID; 'DPMCP_GET_PORTAL_ID_FROM_POOL' to get the portal ID
 *		from pool
 */
struct dpmcp_cfg {
	int portal_id;
};

/**
 * dpmcp_create() - Create the DPMCP object.
 * @mc_io:	Pointer to MC portal's I/O object
 * @cfg:	Configuration structure
 * @token:	Returned token; use in subsequent API calls
 *
 * Create the DPMCP object, allocate required resources and
 * perform required initialization.
 *
 * The object can be created either by declaring it in the
 * DPL file, or by calling this function.
 * This function returns a unique authentication token,
 * associated with the specific object ID and the specific MC
 * portal; this token must be used in all subsequent calls to
 * this specific object. For objects that are created using the
 * DPL file, call dpmcp_open function to get an authentication
 * token first.
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpmcp_create(struct fsl_mc_io	*mc_io,
		 const struct dpmcp_cfg	*cfg,
		uint16_t		*token);

/**
 * dpmcp_destroy() - Destroy the DPMCP object and release all its resources.
 * @mc_io:	Pointer to MC portal's I/O object
 * @token:	Token of DPMCP object
 *
 * Return:	'0' on Success; error code otherwise.
 */
int dpmcp_destroy(struct fsl_mc_io *mc_io, uint16_t token);

/**
 * dpmcp_reset() - Reset the DPMCP, returns the object to initial state.
 * @mc_io:	Pointer to MC portal's I/O object
 * @token:	Token of DPMCP object
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpmcp_reset(struct fsl_mc_io *mc_io, uint16_t token);

/* IRQ */
/*!
 * @name dpmcp IRQ Index and Events
 */
#define DPMCP_IRQ_INDEX                             0
/*!< Irq index */
#define DPMCP_IRQ_EVENT_CMD_DONE                    0x00000001
/*!< irq event - Indicates that the link state changed */
/* @} */

/**
 * dpmcp_set_irq() - Set IRQ information for the DPMCP to trigger an interrupt.
 * @mc_io:	Pointer to MC portal's I/O object
 * @token:	Token of DPMCP object
 * @irq_index:	Identifies the interrupt index to configure
 * @irq_addr:	Address that must be written to
 *				signal a message-based interrupt
 * @irq_val:	Value to write into irq_addr address
 * @user_irq_id: A user defined number associated with this IRQ
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpmcp_set_irq(struct fsl_mc_io	*mc_io,
		  uint16_t		token,
		 uint8_t		irq_index,
		 uint64_t		irq_addr,
		 uint32_t		irq_val,
		 int			user_irq_id);

/**
 * dpmcp_get_irq() - Get IRQ information from the DPMCP.
 * @mc_io:	Pointer to MC portal's I/O object
 * @token:	Token of DPMCP object
 * @irq_index:	The interrupt index to configure
 * @type:	Interrupt type: 0 represents message interrupt
 *				type (both irq_addr and irq_val are valid)
 * @irq_addr:	Returned address that must be written to
 *				signal the message-based interrupt
 * @irq_val:	Value to write into irq_addr address
 * @user_irq_id: A user defined number associated with this IRQ
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpmcp_get_irq(struct fsl_mc_io	*mc_io,
		  uint16_t		token,
		 uint8_t		irq_index,
		 int			*type,
		 uint64_t		*irq_addr,
		 uint32_t		*irq_val,
		 int			*user_irq_id);

/**
 * dpmcp_set_irq_enable() - Set overall interrupt state.
 * @mc_io:	Pointer to MC portal's I/O object
 * @token:	Token of DPMCP object
 * @irq_index:	The interrupt index to configure
 * @en:	Interrupt state - enable = 1, disable = 0
 *
 * Allows GPP software to control when interrupts are generated.
 * Each interrupt can have up to 32 causes.  The enable/disable control's the
 * overall interrupt state. if the interrupt is disabled no causes will cause
 * an interrupt.
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpmcp_set_irq_enable(struct fsl_mc_io	*mc_io,
			 uint16_t		token,
			uint8_t			irq_index,
			uint8_t			en);

/**
 * dpmcp_get_irq_enable() - Get overall interrupt state
 * @mc_io:	Pointer to MC portal's I/O object
 * @token:	Token of DPMCP object
 * @irq_index:	The interrupt index to configure
 * @en:		Returned interrupt state - enable = 1, disable = 0
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpmcp_get_irq_enable(struct fsl_mc_io	*mc_io,
			 uint16_t		token,
			uint8_t			irq_index,
			uint8_t			*en);

/**
 * dpmcp_set_irq_mask() - Set interrupt mask.
 * @mc_io:	Pointer to MC portal's I/O object
 * @token:	Token of DPMCP object
 * @irq_index:	The interrupt index to configure
 * @mask:	Event mask to trigger interrupt;
 *			each bit:
 *				0 = ignore event
 *				1 = consider event for asserting IRQ
 *
 * Every interrupt can have up to 32 causes and the interrupt model supports
 * masking/unmasking each cause independently
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpmcp_set_irq_mask(struct fsl_mc_io	*mc_io,
		       uint16_t		token,
		      uint8_t		irq_index,
		      uint32_t		mask);

/**
 * dpmcp_get_irq_mask() - Get interrupt mask.
 * @mc_io:	Pointer to MC portal's I/O object
 * @token:	Token of DPMCP object
 * @irq_index:	The interrupt index to configure
 * @mask:	Returned event mask to trigger interrupt
 *
 * Every interrupt can have up to 32 causes and the interrupt model supports
 * masking/unmasking each cause independently
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpmcp_get_irq_mask(struct fsl_mc_io	*mc_io,
		       uint16_t		token,
		      uint8_t		irq_index,
		      uint32_t		*mask);

/**
 * dpmcp_get_irq_status() - Get the current status of any pending interrupts.
 *
 * @mc_io:	Pointer to MC portal's I/O object
 * @token:	Token of DPMCP object
 * @irq_index:	The interrupt index to configure
 * @status:	Returned interrupts status - one bit per cause:
 *			0 = no interrupt pending
 *			1 = interrupt pending
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpmcp_get_irq_status(struct fsl_mc_io	*mc_io,
			 uint16_t		token,
			uint8_t			irq_index,
			uint32_t		*status);

/**
 * dpmcp_clear_irq_status() - Clear a pending interrupt's status
 *
 * @mc_io:	Pointer to MC portal's I/O object
 * @token:	Token of DPMCP object
 * @irq_index:	The interrupt index to configure
 * @status:	Bits to clear (W1C) - one bit per cause:
 *					0 = don't change
 *					1 = clear status bit
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpmcp_clear_irq_status(struct fsl_mc_io	*mc_io,
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

/**
 * dpmcp_get_attributes - Retrieve DPMCP attributes.
 *
 * @mc_io:	Pointer to MC portal's I/O object
 * @token:	Token of DPMCP object
 * @attr:	Returned object's attributes
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dpmcp_get_attributes(struct fsl_mc_io	*mc_io,
			 uint16_t		token,
			struct dpmcp_attr	*attr);

#endif /* __FSL_DPMCP_H */

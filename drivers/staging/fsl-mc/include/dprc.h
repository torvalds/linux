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
#ifndef _FSL_DPRC_H
#define _FSL_DPRC_H

/* Data Path Resource Container API
 * Contains DPRC API for managing and querying DPAA resources
 */

struct fsl_mc_io;

/**
 * Set this value as the icid value in dprc_cfg structure when creating a
 * container, in case the ICID is not selected by the user and should be
 * allocated by the DPRC from the pool of ICIDs.
 */
#define DPRC_GET_ICID_FROM_POOL			(uint16_t)(~(0))

/**
 * Set this value as the portal_id value in dprc_cfg structure when creating a
 * container, in case the portal ID is not specifically selected by the
 * user and should be allocated by the DPRC from the pool of portal ids.
 */
#define DPRC_GET_PORTAL_ID_FROM_POOL	(int)(~(0))

/**
 * dprc_open() - Open DPRC object for use
 * @mc_io:	Pointer to MC portal's I/O object
 * @container_id: Container ID to open
 * @token:	Returned token of DPRC object
 *
 * Return:	'0' on Success; Error code otherwise.
 *
 * @warning	Required before any operation on the object.
 */
int dprc_open(struct fsl_mc_io *mc_io, int container_id, uint16_t *token);

/**
 * dprc_close() - Close the control session of the object
 * @mc_io:	Pointer to MC portal's I/O object
 * @token:	Token of DPRC object
 *
 * After this function is called, no further operations are
 * allowed on the object without opening a new control session.
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprc_close(struct fsl_mc_io *mc_io, uint16_t token);

/**
 * Container general options
 *
 * These options may be selected at container creation by the container creator
 * and can be retrieved using dprc_get_attributes()
 */

/* Spawn Policy Option allowed - Indicates that the new container is allowed
 * to spawn and have its own child containers.
 */
#define DPRC_CFG_OPT_SPAWN_ALLOWED		0x00000001

/* General Container allocation policy - Indicates that the new container is
 * allowed to allocate requested resources from its parent container; if not
 * set, the container is only allowed to use resources in its own pools; Note
 * that this is a container's global policy, but the parent container may
 * override it and set specific quota per resource type.
 */
#define DPRC_CFG_OPT_ALLOC_ALLOWED		0x00000002

/* Object initialization allowed - software context associated with this
 * container is allowed to invoke object initialization operations.
 */
#define DPRC_CFG_OPT_OBJ_CREATE_ALLOWED	0x00000004

/* Topology change allowed - software context associated with this
 * container is allowed to invoke topology operations, such as attach/detach
 * of network objects.
 */
#define DPRC_CFG_OPT_TOPOLOGY_CHANGES_ALLOWED	0x00000008

/* IOMMU bypass - indicates whether objects of this container are permitted
 * to bypass the IOMMU.
 */
#define DPRC_CFG_OPT_IOMMU_BYPASS		0x00000010

/* AIOP - Indicates that container belongs to AIOP.  */
#define DPRC_CFG_OPT_AIOP			0x00000020

/**
 * struct dprc_cfg - Container configuration options
 * @icid: Container's ICID; if set to 'DPRC_GET_ICID_FROM_POOL', a free
 *		ICID value is allocated by the DPRC
 * @portal_id: Portal ID; if set to 'DPRC_GET_PORTAL_ID_FROM_POOL', a free
 *		portal ID is allocated by the DPRC
 * @options: Combination of 'DPRC_CFG_OPT_<X>' options
 */
struct dprc_cfg {
	uint16_t icid;
	int portal_id;
	uint64_t options;
};

/**
 * dprc_create_container() - Create child container
 * @mc_io:	Pointer to MC portal's I/O object
 * @token:	Token of DPRC object
 * @cfg:	Child container configuration
 * @child_container_id:	Returned child container ID
 * @child_portal_paddr:	Returned base physical address of the
 *					child portal
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprc_create_container(struct fsl_mc_io	*mc_io,
			  uint16_t		token,
			  struct dprc_cfg	*cfg,
			  int			*child_container_id,
			  uint64_t		*child_portal_paddr);

/**
 * dprc_destroy_container() - Destroy child container.
 * @mc_io:	Pointer to MC portal's I/O object
 * @token:	Token of DPRC object
 * @child_container_id:	ID of the container to destroy
 *
 * This function terminates the child container, so following this call the
 * child container ID becomes invalid.
 *
 * Notes:
 * - All resources and objects of the destroyed container are returned to the
 * parent container or destroyed if were created be the destroyed container.
 * - This function destroy all the child containers of the specified
 *   container prior to destroying the container itself.
 *
 * warning: Only the parent container is allowed to destroy a child policy
 *		Container 0 can't be destroyed
 *
 * Return:	'0' on Success; Error code otherwise.
 *
 */
int dprc_destroy_container(struct fsl_mc_io	*mc_io,
			   uint16_t		token,
			   int			child_container_id);

/**
 * dprc_reset_container - Reset child container.
 * @mc_io:	Pointer to MC portal's I/O object
 * @token:	Token of DPRC object
 * @child_container_id:	ID of the container to reset
 *
 * In case a software context crashes or becomes non-responsive, the parent
 * may wish to reset its resources container before the software context is
 * restarted.
 *
 * This routine informs all objects assigned to the child container that the
 * container is being reset, so they may perform any cleanup operations that are
 * needed. All objects handles that were owned by the child container shall be
 * closed.
 *
 * Note that such request may be submitted even if the child software context
 * has not crashed, but the resulting object cleanup operations will not be
 * aware of that.
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprc_reset_container(struct fsl_mc_io *mc_io,
			 uint16_t token,
			 int child_container_id);

/* IRQ */

/* Number of dprc's IRQs */
#define DPRC_NUM_OF_IRQS		1

/* Object irq events */

/* IRQ event - Indicates that a new object assigned to the container */
#define DPRC_IRQ_EVENT_OBJ_ADDED		0x00000001
/* IRQ event - Indicates that an object was unassigned from the container */
#define DPRC_IRQ_EVENT_OBJ_REMOVED		0x00000002
/* IRQ event - Indicates that resources assigned to the container */
#define DPRC_IRQ_EVENT_RES_ADDED		0x00000004
/* IRQ event - Indicates that resources unassigned from the container */
#define DPRC_IRQ_EVENT_RES_REMOVED		0x00000008
/* IRQ event - Indicates that one of the descendant containers that opened by
 * this container is destroyed
 */
#define DPRC_IRQ_EVENT_CONTAINER_DESTROYED	0x00000010

/* IRQ event - Indicates that on one of the container's opened object is
 * destroyed
 */
#define DPRC_IRQ_EVENT_OBJ_DESTROYED		0x00000020

/* Irq event - Indicates that object is created at the container */
#define DPRC_IRQ_EVENT_OBJ_CREATED		0x00000040

/**
 * dprc_set_irq() - Set IRQ information for the DPRC to trigger an interrupt.
 * @mc_io:	Pointer to MC portal's I/O object
 * @token:	Token of DPRC object
 * @irq_index:	Identifies the interrupt index to configure
 * @irq_addr:	Address that must be written to
 *			signal a message-based interrupt
 * @irq_val:	Value to write into irq_addr address
 * @user_irq_id: Returned a user defined number associated with this IRQ
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprc_set_irq(struct fsl_mc_io	*mc_io,
		 uint16_t		token,
		 uint8_t		irq_index,
		 uint64_t		irq_addr,
		 uint32_t		irq_val,
		 int			user_irq_id);

/**
 * dprc_get_irq() - Get IRQ information from the DPRC.
 * @mc_io:	Pointer to MC portal's I/O object
 * @token:	Token of DPRC object
 * @irq_index:	The interrupt index to configure
 * @type:	Returned interrupt type: 0 represents message interrupt
 *			type (both irq_addr and irq_val are valid)
 * @irq_addr:	Returned address that must be written to
 *			signal the message-based interrupt
 * @irq_val:	Value to write into irq_addr address
 * @user_irq_id: A user defined number associated with this IRQ
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprc_get_irq(struct fsl_mc_io	*mc_io,
		 uint16_t		token,
		 uint8_t		irq_index,
		 int			*type,
		 uint64_t		*irq_addr,
		 uint32_t		*irq_val,
		 int			*user_irq_id);

/**
 * dprc_set_irq_enable() - Set overall interrupt state.
 * @mc_io:	Pointer to MC portal's I/O object
 * @token:	Token of DPRC object
 * @irq_index:	The interrupt index to configure
 * @en:		Interrupt state - enable = 1, disable = 0
 *
 * Allows GPP software to control when interrupts are generated.
 * Each interrupt can have up to 32 causes.  The enable/disable control's the
 * overall interrupt state. if the interrupt is disabled no causes will cause
 * an interrupt.
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprc_set_irq_enable(struct fsl_mc_io	*mc_io,
			uint16_t		token,
			uint8_t			irq_index,
			uint8_t			en);

/**
 * dprc_get_irq_enable() - Get overall interrupt state.
 * @mc_io:	Pointer to MC portal's I/O object
 * @token:	Token of DPRC object
 * @irq_index:  The interrupt index to configure
 * @en:		Returned interrupt state - enable = 1, disable = 0
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprc_get_irq_enable(struct fsl_mc_io	*mc_io,
			uint16_t		token,
			uint8_t			irq_index,
			uint8_t			*en);

/**
 * dprc_set_irq_mask() - Set interrupt mask.
 * @mc_io:	Pointer to MC portal's I/O object
 * @token:	Token of DPRC object
 * @irq_index:	The interrupt index to configure
 * @mask:	event mask to trigger interrupt;
 *			each bit:
 *				0 = ignore event
 *				1 = consider event for asserting irq
 *
 * Every interrupt can have up to 32 causes and the interrupt model supports
 * masking/unmasking each cause independently
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprc_set_irq_mask(struct fsl_mc_io	*mc_io,
		      uint16_t		token,
		      uint8_t		irq_index,
		      uint32_t		mask);

/**
 * dprc_get_irq_mask() - Get interrupt mask.
 * @mc_io:	Pointer to MC portal's I/O object
 * @token:	Token of DPRC object
 * @irq_index:	The interrupt index to configure
 * @mask:	Returned event mask to trigger interrupt
 *
 * Every interrupt can have up to 32 causes and the interrupt model supports
 * masking/unmasking each cause independently
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprc_get_irq_mask(struct fsl_mc_io	*mc_io,
		      uint16_t		token,
		      uint8_t		irq_index,
		      uint32_t		*mask);

/**
 * dprc_get_irq_status() - Get the current status of any pending interrupts.
 * @mc_io:	Pointer to MC portal's I/O object
 * @token:	Token of DPRC object
 * @irq_index:	The interrupt index to configure
 * @status:	Returned interrupts status - one bit per cause:
 *			0 = no interrupt pending
 *			1 = interrupt pending
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprc_get_irq_status(struct fsl_mc_io	*mc_io,
			uint16_t		token,
			uint8_t			irq_index,
			uint32_t		*status);

/**
 * dprc_clear_irq_status() - Clear a pending interrupt's status
 * @mc_io:	Pointer to MC portal's I/O object
 * @token:	Token of DPRC object
 * @irq_index:	The interrupt index to configure
 * @status:	bits to clear (W1C) - one bit per cause:
 *					0 = don't change
 *					1 = clear status bit
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprc_clear_irq_status(struct fsl_mc_io	*mc_io,
			  uint16_t		token,
			  uint8_t		irq_index,
			  uint32_t		status);

/**
 * struct dprc_attributes - Container attributes
 * @container_id: Container's ID
 * @icid: Container's ICID
 * @portal_id: Container's portal ID
 * @options: Container's options as set at container's creation
 * @version: DPRC version
 */
struct dprc_attributes {
	int container_id;
	uint16_t icid;
	int portal_id;
	uint64_t options;
	/**
	 * struct version - DPRC version
	 * @major: DPRC major version
	 * @minor: DPRC minor version
	 */
	struct {
		uint16_t major;
		uint16_t minor;
	} version;
};

/**
 * dprc_get_attributes() - Obtains container attributes
 * @mc_io:	Pointer to MC portal's I/O object
 * @token:	Token of DPRC object
 * @attributes	Returned container attributes
 *
 * Return:     '0' on Success; Error code otherwise.
 */
int dprc_get_attributes(struct fsl_mc_io	*mc_io,
			uint16_t		token,
			struct dprc_attributes	*attributes);

/**
 * dprc_set_res_quota() - Set allocation policy for a specific resource/object
 *		type in a child container
 * @mc_io:	Pointer to MC portal's I/O object
 * @token:	Token of DPRC object
 * @child_container_id:	ID of the child container
 * @type:	Resource/object type
 * @quota:	Sets the maximum number of resources of	the selected type
 *		that the child container is allowed to allocate from its parent;
 *		when quota is set to -1, the policy is the same as container's
 *		general policy.
 *
 * Allocation policy determines whether or not a container may allocate
 * resources from its parent. Each container has a 'global' allocation policy
 * that is set when the container is created.
 *
 * This function sets allocation policy for a specific resource type.
 * The default policy for all resource types matches the container's 'global'
 * allocation policy.
 *
 * Return:	'0' on Success; Error code otherwise.
 *
 * @warning	Only the parent container is allowed to change a child policy.
 */
int dprc_set_res_quota(struct fsl_mc_io	*mc_io,
		       uint16_t		token,
		       int		child_container_id,
		       char		*type,
		       uint16_t		quota);

/**
 * dprc_get_res_quota() - Gets the allocation policy of a specific
 *		resource/object type in a child container
 * @mc_io:	Pointer to MC portal's I/O object
 * @token:	Token of DPRC object
 * @child_container_id;	ID of the child container
 * @type:	resource/object type
 * @quota:	Returnes the maximum number of resources of the selected type
 *		that the child container is allowed to allocate from the parent;
 *		when quota is set to -1, the policy is the same as container's
 *		general policy.
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprc_get_res_quota(struct fsl_mc_io	*mc_io,
		       uint16_t		token,
		       int		child_container_id,
		       char		*type,
		       uint16_t		*quota);

/* Resource request options */

/* Explicit resource ID request - The requested objects/resources
 * are explicit and sequential (in case of resources).
 * The base ID is given at res_req at base_align field
 */
#define DPRC_RES_REQ_OPT_EXPLICIT		0x00000001

/* Aligned resources request - Relevant only for resources
 * request (and not objects). Indicates that resources base ID should be
 * sequential and aligned to the value given at dprc_res_req base_align field
 */
#define DPRC_RES_REQ_OPT_ALIGNED		0x00000002

/* Plugged Flag - Relevant only for object assignment request.
 * Indicates that after all objects assigned. An interrupt will be invoked at
 * the relevant GPP. The assigned object will be marked as plugged.
 * plugged objects can't be assigned from their container
 */
#define DPRC_RES_REQ_OPT_PLUGGED		0x00000004

/**
 * struct dprc_res_req - Resource request descriptor, to be used in assignment
 *			or un-assignment of resources and objects.
 * @type: Resource/object type: Represent as a NULL terminated string.
 *	This string may received by using dprc_get_pool() to get resource
 *	type and dprc_get_obj() to get object type;
 *	Note: it is not possible to assign/un-assign DPRC objects
 * @num: Number of resources
 * @options: Request options: combination of DPRC_RES_REQ_OPT_ options
 * @id_base_align: In case of explicit assignment (DPRC_RES_REQ_OPT_EXPLICIT
 *		is set at option), this field represents the required base ID
 *		for resource allocation; In case of aligned assignment
 *		(DPRC_RES_REQ_OPT_ALIGNED is set at option), this field
 *		indicates the required alignment for the resource ID(s) -
 *		use 0 if there is no alignment or explicit ID requirements
 */
struct dprc_res_req {
	char type[16];
	uint32_t num;
	uint32_t options;
	int id_base_align;
};

/**
 * dprc_assign() - Assigns objects or resource to a child container.
 * @mc_io:	Pointer to MC portal's I/O object
 * @token:	Token of DPRC object
 * @container_id: ID of the child container
 * @res_req:	Describes the type and amount of resources to
 *			assign to the given container
 *
 * Assignment is usually done by a parent (this DPRC) to one of its child
 * containers.
 *
 * According to the DPRC allocation policy, the assigned resources may be taken
 * (allocated) from the container's ancestors, if not enough resources are
 * available in the container itself.
 *
 * The type of assignment depends on the dprc_res_req options, as follows:
 * - DPRC_RES_REQ_OPT_EXPLICIT: indicates that assigned resources should have
 *   the explicit base ID specified at the id_base_align field of res_req.
 * - DPRC_RES_REQ_OPT_ALIGNED: indicates that the assigned resources should be
 *   aligned to the value given at id_base_align field of res_req.
 * - DPRC_RES_REQ_OPT_PLUGGED: Relevant only for object assignment,
 *   and indicates that the object must be set to the plugged state.
 *
 * A container may use this function with its own ID in order to change a
 * object state to plugged or unplugged.
 *
 * If IRQ information has been set in the child DPRC, it will signal an
 * interrupt following every change in its object assignment.
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprc_assign(struct fsl_mc_io	*mc_io,
		uint16_t		token,
		int			container_id,
		struct dprc_res_req	*res_req);

/**
 * dprc_unassign() - Un-assigns objects or resources from a child container
 *		and moves them into this (parent) DPRC.
 * @mc_io:	Pointer to MC portal's I/O object
 * @token:	Token of DPRC object
 * @child_container_id:	ID of the child container
 * @res_req:	Describes the type and amount of resources to un-assign from
 *		the child container
 *
 * Un-assignment of objects can succeed only if the object is not in the
 * plugged or opened state.
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprc_unassign(struct fsl_mc_io	*mc_io,
		  uint16_t		token,
		  int			child_container_id,
		  struct dprc_res_req	*res_req);

/**
 * dprc_get_pool_count() - Get the number of dprc's pools
 * @mc_io:	Pointer to MC portal's I/O object
 * @token:	Token of DPRC object
 * @pool_count:	Returned number of resource pools in the dprc
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprc_get_pool_count(struct fsl_mc_io	*mc_io,
			uint16_t		token,
			int			*pool_count);

/**
 * dprc_get_pool() - Get the type (string) of a certain dprc's pool
 * @mc_io:	Pointer to MC portal's I/O object
 * @token:	Token of DPRC object
 * @pool_index;	Index of the pool to be queried (< pool_count)
 * @type:	The type of the pool
 *
 * The pool types retrieved one by one by incrementing
 * pool_index up to (not including) the value of pool_count returned
 * from dprc_get_pool_count(). dprc_get_pool_count() must
 * be called prior to dprc_get_pool().
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprc_get_pool(struct fsl_mc_io	*mc_io,
		  uint16_t		token,
		  int			pool_index,
		  char			*type);

/**
 * dprc_get_obj_count() - Obtains the number of objects in the DPRC
 * @mc_io:	Pointer to MC portal's I/O object
 * @token:	Token of DPRC object
 * @obj_count:	Number of objects assigned to the DPRC
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprc_get_obj_count(struct fsl_mc_io *mc_io, uint16_t token, int *obj_count);

/* Objects Attributes Flags */

/* Opened state - Indicates that an object is open by at least one owner */
#define DPRC_OBJ_STATE_OPEN		0x00000001
/* Plugged state - Indicates that the object is plugged */
#define DPRC_OBJ_STATE_PLUGGED		0x00000002

/**
 * struct dprc_obj_desc - Object descriptor, returned from dprc_get_obj()
 * @type: Type of object: NULL terminated string
 * @id: ID of logical object resource
 * @vendor: Object vendor identifier
 * @ver_major: Major version number
 * @ver_minor:  Minor version number
 * @irq_count: Number of interrupts supported by the object
 * @region_count: Number of mappable regions supported by the object
 * @state: Object state: combination of DPRC_OBJ_STATE_ states
 */
struct dprc_obj_desc {
	char type[16];
	int id;
	uint16_t vendor;
	uint16_t ver_major;
	uint16_t ver_minor;
	uint8_t irq_count;
	uint8_t region_count;
	uint32_t state;
};

/**
 * dprc_get_obj() - Get general information on an object
 * @mc_io:	Pointer to MC portal's I/O object
 * @token:	Token of DPRC object
 * @obj_index:	Index of the object to be queried (< obj_count)
 * @obj_desc:	Returns the requested object descriptor
 *
 * The object descriptors are retrieved one by one by incrementing
 * obj_index up to (not including) the value of obj_count returned
 * from dprc_get_obj_count(). dprc_get_obj_count() must
 * be called prior to dprc_get_obj().
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprc_get_obj(struct fsl_mc_io	*mc_io,
		 uint16_t		token,
		 int			obj_index,
		 struct dprc_obj_desc	*obj_desc);

/**
 * dprc_get_res_count() - Obtains the number of free resources that are assigned
 *		to this container, by pool type
 * @mc_io:	Pointer to MC portal's I/O object
 * @token:	Token of DPRC object
 * @type:	pool type
 * @res_count:	Returned number of free resources of the given
 *			resource type that are assigned to this DPRC
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprc_get_res_count(struct fsl_mc_io	*mc_io,
		       uint16_t		token,
		       char		*type,
		       int		*res_count);

/**
 * enum dprc_iter_status - Iteration status
 * @DPRC_ITER_STATUS_FIRST: Perform first iteration
 * @DPRC_ITER_STATUS_MORE: Indicates more/next iteration is needed
 * @DPRC_ITER_STATUS_LAST: Indicates last iteration
 */
enum dprc_iter_status {
	DPRC_ITER_STATUS_FIRST = 0,
	DPRC_ITER_STATUS_MORE = 1,
	DPRC_ITER_STATUS_LAST = 2
};

/**
 * struct dprc_res_ids_range_desc - Resource ID range descriptor
 * @base_id: Base resource ID of this range
 * @last_id: Last resource ID of this range
 * @iter_status: Iteration status - should be set to DPRC_ITER_STATUS_FIRST at
 *	first iteration; while the returned marker is DPRC_ITER_STATUS_MORE,
 *	additional iterations are needed, until the returned marker is
 *	DPRC_ITER_STATUS_LAST
 */
struct dprc_res_ids_range_desc {
	int base_id;
	int last_id;
	enum dprc_iter_status iter_status;
};

/**
 * dprc_get_res_ids() - Obtains IDs of free resources in the container
 * @mc_io:	Pointer to MC portal's I/O object
 * @token:	Token of DPRC object
 * @type:	pool type
 * @range_desc:	range descriptor
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprc_get_res_ids(struct fsl_mc_io			*mc_io,
		     uint16_t				token,
		     char				*type,
		     struct dprc_res_ids_range_desc	*range_desc);

/**
 * dprc_get_portal_paddr() - Get the physical address of MC portals
 * @mc_io:	Pointer to MC portal's I/O object
 * @token:	Token of DPRC object
 * @portal_id:	MC portal ID
 * @portal_addr: The physical address of the MC portal ID
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprc_get_portal_paddr(struct fsl_mc_io	*mc_io,
			  uint16_t		token,
			  int			portal_id,
			  uint64_t		*portal_addr);

/**
 * struct dprc_region_desc - Mappable region descriptor
 * @base_paddr: Region base physical address
 * @size: Region size (in bytes)
 */
struct dprc_region_desc {
	uint64_t base_paddr;
	uint32_t size;
};

/**
 * dprc_get_obj_region() - Get region information for a specified object.
 * @mc_io:	Pointer to MC portal's I/O object
 * @token:	Token of DPRC object
 * @obj_type;	Object type as returned in dprc_get_obj()
 * @obj_id:	Unique object instance as returned in dprc_get_obj()
 * @region_index: The specific region to query
 * @region_desc:  Returns the requested region descriptor
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprc_get_obj_region(struct fsl_mc_io	*mc_io,
			uint16_t		token,
			char			*obj_type,
			int			obj_id,
			uint8_t			region_index,
			struct dprc_region_desc	*region_desc);

/**
 * struct dprc_endpoint - Endpoint description for link connect/disconnect
 *			operations
 * @type: Endpoint object type: NULL terminated string
 * @id: Endpoint object ID
 * @interface_id: Interface ID; should be set for endpoints with multiple
 *		interfaces ("dpsw", "dpdmux"); for others, always set to 0
 */
struct dprc_endpoint {
	char type[16];
	int id;
	int interface_id;
};

/**
 * dprc_connect() - Connect two endpoints to create a network link between them
 * @mc_io:	Pointer to MC portal's I/O object
 * @token:	Token of DPRC object
 * @endpoint1:	Endpoint 1 configuration parameters
 * @endpoint2:	Endpoint 2 configuration parameters
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprc_connect(struct fsl_mc_io		*mc_io,
		 uint16_t			token,
		 const struct dprc_endpoint	*endpoint1,
		 const struct dprc_endpoint	*endpoint2);

/**
 * dprc_disconnect() - Disconnect one endpoint to remove its network connection
 * @mc_io:	Pointer to MC portal's I/O object
 * @token:	Token of DPRC object
 * @endpoint:	Endpoint configuration parameters
 *
 * Return:	'0' on Success; Error code otherwise.
 */
int dprc_disconnect(struct fsl_mc_io		*mc_io,
		    uint16_t			token,
		    const struct dprc_endpoint	*endpoint);

/**
* dprc_get_connection() - Get connected endpoint and link status if connection
*			exists.
* @mc_io		Pointer to MC portal's I/O object
* @token		Token of DPRC object
* @endpoint1	Endpoint 1 configuration parameters
* @endpoint2	Returned endpoint 2 configuration parameters
* @state:	Returned link state: 1 - link is up, 0 - link is down
*
* Return:     '0' on Success; -ENAVAIL if connection does not exist.
*/
int dprc_get_connection(struct fsl_mc_io		*mc_io,
			uint16_t			token,
			const struct dprc_endpoint	*endpoint1,
			struct dprc_endpoint		*endpoint2,
			int				*state);

#endif /* _FSL_DPRC_H */


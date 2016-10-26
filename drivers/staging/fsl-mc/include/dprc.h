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

#include "mc-cmd.h"

/* Data Path Resource Container API
 * Contains DPRC API for managing and querying DPAA resources
 */

struct fsl_mc_io;

/**
 * Set this value as the icid value in dprc_cfg structure when creating a
 * container, in case the ICID is not selected by the user and should be
 * allocated by the DPRC from the pool of ICIDs.
 */
#define DPRC_GET_ICID_FROM_POOL			(u16)(~(0))

/**
 * Set this value as the portal_id value in dprc_cfg structure when creating a
 * container, in case the portal ID is not specifically selected by the
 * user and should be allocated by the DPRC from the pool of portal ids.
 */
#define DPRC_GET_PORTAL_ID_FROM_POOL	(int)(~(0))

int dprc_open(struct fsl_mc_io *mc_io,
	      u32 cmd_flags,
	      int container_id,
	      u16 *token);

int dprc_close(struct fsl_mc_io *mc_io,
	       u32 cmd_flags,
	       u16 token);

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

/* AIOP - Indicates that container belongs to AIOP.  */
#define DPRC_CFG_OPT_AIOP			0x00000020

/* IRQ Config - Indicates that the container allowed to configure its IRQs.  */
#define DPRC_CFG_OPT_IRQ_CFG_ALLOWED		0x00000040

/**
 * struct dprc_cfg - Container configuration options
 * @icid: Container's ICID; if set to 'DPRC_GET_ICID_FROM_POOL', a free
 *		ICID value is allocated by the DPRC
 * @portal_id: Portal ID; if set to 'DPRC_GET_PORTAL_ID_FROM_POOL', a free
 *		portal ID is allocated by the DPRC
 * @options: Combination of 'DPRC_CFG_OPT_<X>' options
 * @label: Object's label
 */
struct dprc_cfg {
	u16 icid;
	int portal_id;
	u64 options;
	char label[16];
};

int dprc_create_container(struct fsl_mc_io *mc_io,
			  u32 cmd_flags,
			  u16 token,
			  struct dprc_cfg *cfg,
			  int *child_container_id,
			  u64 *child_portal_offset);

int dprc_destroy_container(struct fsl_mc_io *mc_io,
			   u32 cmd_flags,
			   u16 token,
			   int child_container_id);

int dprc_reset_container(struct fsl_mc_io *mc_io,
			 u32 cmd_flags,
			 u16 token,
			 int child_container_id);

/* IRQ */

/* IRQ index */
#define DPRC_IRQ_INDEX          0

/* Number of dprc's IRQs */
#define DPRC_NUM_OF_IRQS	1

/* DPRC IRQ events */

/* IRQ event - Indicates that a new object added to the container */
#define DPRC_IRQ_EVENT_OBJ_ADDED		0x00000001
/* IRQ event - Indicates that an object was removed from the container */
#define DPRC_IRQ_EVENT_OBJ_REMOVED		0x00000002
/* IRQ event - Indicates that resources added to the container */
#define DPRC_IRQ_EVENT_RES_ADDED		0x00000004
/* IRQ event - Indicates that resources removed from the container */
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
 * struct dprc_irq_cfg - IRQ configuration
 * @paddr:	Address that must be written to signal a message-based interrupt
 * @val:	Value to write into irq_addr address
 * @irq_num:	A user defined number associated with this IRQ
 */
struct dprc_irq_cfg {
	     phys_addr_t paddr;
	     u32 val;
	     int irq_num;
};

int dprc_set_irq(struct fsl_mc_io *mc_io,
		 u32 cmd_flags,
		 u16 token,
		 u8 irq_index,
		 struct dprc_irq_cfg *irq_cfg);

int dprc_get_irq(struct fsl_mc_io *mc_io,
		 u32 cmd_flags,
		 u16 token,
		 u8 irq_index,
		 int *type,
		 struct dprc_irq_cfg *irq_cfg);

int dprc_set_irq_enable(struct fsl_mc_io *mc_io,
			u32 cmd_flags,
			u16 token,
			u8 irq_index,
			u8 en);

int dprc_get_irq_enable(struct fsl_mc_io *mc_io,
			u32 cmd_flags,
			u16 token,
			u8 irq_index,
			u8 *en);

int dprc_set_irq_mask(struct fsl_mc_io *mc_io,
		      u32 cmd_flags,
		      u16 token,
		      u8 irq_index,
		      u32 mask);

int dprc_get_irq_mask(struct fsl_mc_io *mc_io,
		      u32 cmd_flags,
		      u16 token,
		      u8 irq_index,
		      u32 *mask);

int dprc_get_irq_status(struct fsl_mc_io *mc_io,
			u32 cmd_flags,
			u16 token,
			u8 irq_index,
			u32 *status);

int dprc_clear_irq_status(struct fsl_mc_io *mc_io,
			  u32 cmd_flags,
			  u16 token,
			  u8 irq_index,
			  u32 status);

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
	u16 icid;
	int portal_id;
	u64 options;
	/**
	 * struct version - DPRC version
	 * @major: DPRC major version
	 * @minor: DPRC minor version
	 */
	struct {
		u16 major;
		u16 minor;
	} version;
};

int dprc_get_attributes(struct fsl_mc_io *mc_io,
			u32 cmd_flags,
			u16 token,
			struct dprc_attributes *attributes);

int dprc_set_res_quota(struct fsl_mc_io	*mc_io,
		       u32 cmd_flags,
		       u16 token,
		       int child_container_id,
		       char *type,
		       u16 quota);

int dprc_get_res_quota(struct fsl_mc_io	*mc_io,
		       u32 cmd_flags,
		       u16 token,
		       int child_container_id,
		       char *type,
		       u16 *quota);

/* Resource request options */

/* Explicit resource ID request - The requested objects/resources
 * are explicit and sequential (in case of resources).
 * The base ID is given at res_req at base_align field
 */
#define DPRC_RES_REQ_OPT_EXPLICIT	0x00000001

/* Aligned resources request - Relevant only for resources
 * request (and not objects). Indicates that resources base ID should be
 * sequential and aligned to the value given at dprc_res_req base_align field
 */
#define DPRC_RES_REQ_OPT_ALIGNED	0x00000002

/* Plugged Flag - Relevant only for object assignment request.
 * Indicates that after all objects assigned. An interrupt will be invoked at
 * the relevant GPP. The assigned object will be marked as plugged.
 * plugged objects can't be assigned from their container
 */
#define DPRC_RES_REQ_OPT_PLUGGED	0x00000004

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
	u32 num;
	u32 options;
	int id_base_align;
};

int dprc_assign(struct fsl_mc_io *mc_io,
		u32 cmd_flags,
		u16 token,
		int container_id,
		struct dprc_res_req *res_req);

int dprc_unassign(struct fsl_mc_io *mc_io,
		  u32 cmd_flags,
		  u16 token,
		  int child_container_id,
		  struct dprc_res_req *res_req);

int dprc_get_pool_count(struct fsl_mc_io *mc_io,
			u32 cmd_flags,
			u16 token,
			int *pool_count);

int dprc_get_pool(struct fsl_mc_io *mc_io,
		  u32 cmd_flags,
		  u16 token,
		  int pool_index,
		  char *type);

int dprc_get_obj_count(struct fsl_mc_io *mc_io,
		       u32 cmd_flags,
		       u16 token,
		       int *obj_count);

/* Objects Attributes Flags */

/* Opened state - Indicates that an object is open by at least one owner */
#define DPRC_OBJ_STATE_OPEN		0x00000001
/* Plugged state - Indicates that the object is plugged */
#define DPRC_OBJ_STATE_PLUGGED		0x00000002

/**
 * Shareability flag - Object flag indicating no memory shareability.
 * the object generates memory accesses that are non coherent with other
 * masters;
 * user is responsible for proper memory handling through IOMMU configuration.
 */
#define DPRC_OBJ_FLAG_NO_MEM_SHAREABILITY	0x0001

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
 * @label: Object label
 * @flags: Object's flags
 */
struct dprc_obj_desc {
	char type[16];
	int id;
	u16 vendor;
	u16 ver_major;
	u16 ver_minor;
	u8 irq_count;
	u8 region_count;
	u32 state;
	char label[16];
	u16 flags;
};

int dprc_get_obj(struct fsl_mc_io *mc_io,
		 u32 cmd_flags,
		 u16 token,
		 int obj_index,
		 struct dprc_obj_desc *obj_desc);

int dprc_get_obj_desc(struct fsl_mc_io *mc_io,
		      u32 cmd_flags,
		      u16 token,
		      char *obj_type,
		      int obj_id,
		      struct dprc_obj_desc *obj_desc);

int dprc_set_obj_irq(struct fsl_mc_io *mc_io,
		     u32 cmd_flags,
		     u16 token,
		     char *obj_type,
		     int obj_id,
		     u8 irq_index,
		     struct dprc_irq_cfg *irq_cfg);

int dprc_get_obj_irq(struct fsl_mc_io *mc_io,
		     u32 cmd_flags,
		     u16 token,
		     char *obj_type,
		     int obj_id,
		     u8 irq_index,
		     int *type,
		     struct dprc_irq_cfg *irq_cfg);

int dprc_get_res_count(struct fsl_mc_io *mc_io,
		       u32 cmd_flags,
		       u16 token,
		       char *type,
		       int *res_count);

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

int dprc_get_res_ids(struct fsl_mc_io *mc_io,
		     u32 cmd_flags,
		     u16 token,
		     char *type,
		     struct dprc_res_ids_range_desc *range_desc);

/* Region flags */
/* Cacheable - Indicates that region should be mapped as cacheable */
#define DPRC_REGION_CACHEABLE	0x00000001

/**
 * enum dprc_region_type - Region type
 * @DPRC_REGION_TYPE_MC_PORTAL: MC portal region
 * @DPRC_REGION_TYPE_QBMAN_PORTAL: Qbman portal region
 */
enum dprc_region_type {
	DPRC_REGION_TYPE_MC_PORTAL,
	DPRC_REGION_TYPE_QBMAN_PORTAL
};

/**
 * struct dprc_region_desc - Mappable region descriptor
 * @base_offset: Region offset from region's base address.
 *	For DPMCP and DPRC objects, region base is offset from SoC MC portals
 *	base address; For DPIO, region base is offset from SoC QMan portals
 *	base address
 * @size: Region size (in bytes)
 * @flags: Region attributes
 * @type: Portal region type
 */
struct dprc_region_desc {
	u32 base_offset;
	u32 size;
	u32 flags;
	enum dprc_region_type type;
};

int dprc_get_obj_region(struct fsl_mc_io *mc_io,
			u32 cmd_flags,
			u16 token,
			char *obj_type,
			int obj_id,
			u8 region_index,
			struct dprc_region_desc *region_desc);

int dprc_set_obj_label(struct fsl_mc_io *mc_io,
		       u32 cmd_flags,
		       u16 token,
		       char *obj_type,
		       int obj_id,
		       char *label);

/**
 * struct dprc_endpoint - Endpoint description for link connect/disconnect
 *			operations
 * @type: Endpoint object type: NULL terminated string
 * @id: Endpoint object ID
 * @if_id: Interface ID; should be set for endpoints with multiple
 *		interfaces ("dpsw", "dpdmux"); for others, always set to 0
 */
struct dprc_endpoint {
	char type[16];
	int id;
	int if_id;
};

/**
 * struct dprc_connection_cfg - Connection configuration.
 *				Used for virtual connections only
 * @committed_rate: Committed rate (Mbits/s)
 * @max_rate: Maximum rate (Mbits/s)
 */
struct dprc_connection_cfg {
	u32 committed_rate;
	u32 max_rate;
};

int dprc_connect(struct fsl_mc_io *mc_io,
		 u32 cmd_flags,
		 u16 token,
		 const struct dprc_endpoint *endpoint1,
		 const struct dprc_endpoint *endpoint2,
		 const struct dprc_connection_cfg *cfg);

int dprc_disconnect(struct fsl_mc_io *mc_io,
		    u32 cmd_flags,
		    u16 token,
		    const struct dprc_endpoint *endpoint);

int dprc_get_connection(struct fsl_mc_io *mc_io,
			u32 cmd_flags,
			u16 token,
			const struct dprc_endpoint *endpoint1,
			struct dprc_endpoint *endpoint2,
			int *state);

#endif /* _FSL_DPRC_H */


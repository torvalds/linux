/*
 * Copyright 2013-2016 Freescale Semiconductor Inc.
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

/*
 * Data Path Resource Container API
 * Contains DPRC API for managing and querying DPAA resources
 */

struct fsl_mc_io;
struct fsl_mc_obj_desc;

int dprc_open(struct fsl_mc_io *mc_io,
	      u32 cmd_flags,
	      int container_id,
	      u16 *token);

int dprc_close(struct fsl_mc_io *mc_io,
	       u32 cmd_flags,
	       u16 token);

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
/*
 * IRQ event - Indicates that one of the descendant containers that opened by
 * this container is destroyed
 */
#define DPRC_IRQ_EVENT_CONTAINER_DESTROYED	0x00000010

/*
 * IRQ event - Indicates that on one of the container's opened object is
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
 */
struct dprc_attributes {
	int container_id;
	u16 icid;
	int portal_id;
	u64 options;
};

int dprc_get_attributes(struct fsl_mc_io *mc_io,
			u32 cmd_flags,
			u16 token,
			struct dprc_attributes *attributes);

int dprc_get_obj_count(struct fsl_mc_io *mc_io,
		       u32 cmd_flags,
		       u16 token,
		       int *obj_count);

int dprc_get_obj(struct fsl_mc_io *mc_io,
		 u32 cmd_flags,
		 u16 token,
		 int obj_index,
		 struct fsl_mc_obj_desc *obj_desc);

int dprc_get_obj_desc(struct fsl_mc_io *mc_io,
		      u32 cmd_flags,
		      u16 token,
		      char *obj_type,
		      int obj_id,
		      struct fsl_mc_obj_desc *obj_desc);

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

int dprc_get_api_version(struct fsl_mc_io *mc_io,
			 u32 cmd_flags,
			 u16 *major_ver,
			 u16 *minor_ver);

int dprc_get_container_id(struct fsl_mc_io *mc_io,
			  u32 cmd_flags,
			  int *container_id);

#endif /* _FSL_DPRC_H */


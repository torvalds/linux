/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Freescale Management Complex (MC) bus private declarations
 *
 * Copyright (C) 2016 Freescale Semiconductor, Inc.
 *
 */
#ifndef _FSL_MC_PRIVATE_H_
#define _FSL_MC_PRIVATE_H_

#include <linux/fsl/mc.h>
#include <linux/mutex.h>

/*
 * Data Path Management Complex (DPMNG) General API
 */

/* DPMNG command versioning */
#define DPMNG_CMD_BASE_VERSION		1
#define DPMNG_CMD_ID_OFFSET		4

#define DPMNG_CMD(id)	(((id) << DPMNG_CMD_ID_OFFSET) | DPMNG_CMD_BASE_VERSION)

/* DPMNG command IDs */
#define DPMNG_CMDID_GET_VERSION		DPMNG_CMD(0x831)

struct dpmng_rsp_get_version {
	__le32 revision;
	__le32 version_major;
	__le32 version_minor;
};

/*
 * Data Path Management Command Portal (DPMCP) API
 */

/* Minimal supported DPMCP Version */
#define DPMCP_MIN_VER_MAJOR		3
#define DPMCP_MIN_VER_MINOR		0

/* DPMCP command versioning */
#define DPMCP_CMD_BASE_VERSION		1
#define DPMCP_CMD_ID_OFFSET		4

#define DPMCP_CMD(id)	(((id) << DPMCP_CMD_ID_OFFSET) | DPMCP_CMD_BASE_VERSION)

/* DPMCP command IDs */
#define DPMCP_CMDID_CLOSE		DPMCP_CMD(0x800)
#define DPMCP_CMDID_OPEN		DPMCP_CMD(0x80b)
#define DPMCP_CMDID_RESET		DPMCP_CMD(0x005)

struct dpmcp_cmd_open {
	__le32 dpmcp_id;
};

/*
 * Initialization and runtime control APIs for DPMCP
 */
int dpmcp_open(struct fsl_mc_io *mc_io,
	       u32 cmd_flags,
	       int dpmcp_id,
	       u16 *token);

int dpmcp_close(struct fsl_mc_io *mc_io,
		u32 cmd_flags,
		u16 token);

int dpmcp_reset(struct fsl_mc_io *mc_io,
		u32 cmd_flags,
		u16 token);

/*
 * Data Path Resource Container (DPRC) API
 */

/* Minimal supported DPRC Version */
#define DPRC_MIN_VER_MAJOR			6
#define DPRC_MIN_VER_MINOR			0

/* DPRC command versioning */
#define DPRC_CMD_BASE_VERSION			1
#define DPRC_CMD_ID_OFFSET			4

#define DPRC_CMD(id)	(((id) << DPRC_CMD_ID_OFFSET) | DPRC_CMD_BASE_VERSION)

/* DPRC command IDs */
#define DPRC_CMDID_CLOSE                        DPRC_CMD(0x800)
#define DPRC_CMDID_OPEN                         DPRC_CMD(0x805)
#define DPRC_CMDID_GET_API_VERSION              DPRC_CMD(0xa05)

#define DPRC_CMDID_GET_ATTR                     DPRC_CMD(0x004)

#define DPRC_CMDID_SET_IRQ                      DPRC_CMD(0x010)
#define DPRC_CMDID_SET_IRQ_ENABLE               DPRC_CMD(0x012)
#define DPRC_CMDID_SET_IRQ_MASK                 DPRC_CMD(0x014)
#define DPRC_CMDID_GET_IRQ_STATUS               DPRC_CMD(0x016)
#define DPRC_CMDID_CLEAR_IRQ_STATUS             DPRC_CMD(0x017)

#define DPRC_CMDID_GET_CONT_ID                  DPRC_CMD(0x830)
#define DPRC_CMDID_GET_OBJ_COUNT                DPRC_CMD(0x159)
#define DPRC_CMDID_GET_OBJ                      DPRC_CMD(0x15A)
#define DPRC_CMDID_GET_OBJ_REG                  DPRC_CMD(0x15E)
#define DPRC_CMDID_SET_OBJ_IRQ                  DPRC_CMD(0x15F)

struct dprc_cmd_open {
	__le32 container_id;
};

struct dprc_cmd_set_irq {
	/* cmd word 0 */
	__le32 irq_val;
	u8 irq_index;
	u8 pad[3];
	/* cmd word 1 */
	__le64 irq_addr;
	/* cmd word 2 */
	__le32 irq_num;
};

#define DPRC_ENABLE		0x1

struct dprc_cmd_set_irq_enable {
	u8 enable;
	u8 pad[3];
	u8 irq_index;
};

struct dprc_cmd_set_irq_mask {
	__le32 mask;
	u8 irq_index;
};

struct dprc_cmd_get_irq_status {
	__le32 status;
	u8 irq_index;
};

struct dprc_rsp_get_irq_status {
	__le32 status;
};

struct dprc_cmd_clear_irq_status {
	__le32 status;
	u8 irq_index;
};

struct dprc_rsp_get_attributes {
	/* response word 0 */
	__le32 container_id;
	__le16 icid;
	__le16 pad;
	/* response word 1 */
	__le32 options;
	__le32 portal_id;
};

struct dprc_rsp_get_obj_count {
	__le32 pad;
	__le32 obj_count;
};

struct dprc_cmd_get_obj {
	__le32 obj_index;
};

struct dprc_rsp_get_obj {
	/* response word 0 */
	__le32 pad0;
	__le32 id;
	/* response word 1 */
	__le16 vendor;
	u8 irq_count;
	u8 region_count;
	__le32 state;
	/* response word 2 */
	__le16 version_major;
	__le16 version_minor;
	__le16 flags;
	__le16 pad1;
	/* response word 3-4 */
	u8 type[16];
	/* response word 5-6 */
	u8 label[16];
};

struct dprc_cmd_get_obj_region {
	/* cmd word 0 */
	__le32 obj_id;
	__le16 pad0;
	u8 region_index;
	u8 pad1;
	/* cmd word 1-2 */
	__le64 pad2[2];
	/* cmd word 3-4 */
	u8 obj_type[16];
};

struct dprc_rsp_get_obj_region {
	/* response word 0 */
	__le64 pad;
	/* response word 1 */
	__le64 base_addr;
	/* response word 2 */
	__le32 size;
};

struct dprc_cmd_set_obj_irq {
	/* cmd word 0 */
	__le32 irq_val;
	u8 irq_index;
	u8 pad[3];
	/* cmd word 1 */
	__le64 irq_addr;
	/* cmd word 2 */
	__le32 irq_num;
	__le32 obj_id;
	/* cmd word 3-4 */
	u8 obj_type[16];
};

/*
 * DPRC API for managing and querying DPAA resources
 */
int dprc_open(struct fsl_mc_io *mc_io,
	      u32 cmd_flags,
	      int container_id,
	      u16 *token);

int dprc_close(struct fsl_mc_io *mc_io,
	       u32 cmd_flags,
	       u16 token);

/* DPRC IRQ events */

/* IRQ event - Indicates that a new object added to the container */
#define DPRC_IRQ_EVENT_OBJ_ADDED		0x00000001
/* IRQ event - Indicates that an object was removed from the container */
#define DPRC_IRQ_EVENT_OBJ_REMOVED		0x00000002
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

int dprc_set_irq_enable(struct fsl_mc_io *mc_io,
			u32 cmd_flags,
			u16 token,
			u8 irq_index,
			u8 en);

int dprc_set_irq_mask(struct fsl_mc_io *mc_io,
		      u32 cmd_flags,
		      u16 token,
		      u8 irq_index,
		      u32 mask);

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

int dprc_set_obj_irq(struct fsl_mc_io *mc_io,
		     u32 cmd_flags,
		     u16 token,
		     char *obj_type,
		     int obj_id,
		     u8 irq_index,
		     struct dprc_irq_cfg *irq_cfg);

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

/*
 * Data Path Buffer Pool (DPBP) API
 */

/* DPBP Version */
#define DPBP_VER_MAJOR				3
#define DPBP_VER_MINOR				2

/* Command versioning */
#define DPBP_CMD_BASE_VERSION			1
#define DPBP_CMD_ID_OFFSET			4

#define DPBP_CMD(id)	(((id) << DPBP_CMD_ID_OFFSET) | DPBP_CMD_BASE_VERSION)

/* Command IDs */
#define DPBP_CMDID_CLOSE		DPBP_CMD(0x800)
#define DPBP_CMDID_OPEN			DPBP_CMD(0x804)

#define DPBP_CMDID_ENABLE		DPBP_CMD(0x002)
#define DPBP_CMDID_DISABLE		DPBP_CMD(0x003)
#define DPBP_CMDID_GET_ATTR		DPBP_CMD(0x004)
#define DPBP_CMDID_RESET		DPBP_CMD(0x005)

struct dpbp_cmd_open {
	__le32 dpbp_id;
};

#define DPBP_ENABLE			0x1

struct dpbp_rsp_get_attributes {
	/* response word 0 */
	__le16 pad;
	__le16 bpid;
	__le32 id;
	/* response word 1 */
	__le16 version_major;
	__le16 version_minor;
};

/*
 * Data Path Concentrator (DPCON) API
 */

/* DPCON Version */
#define DPCON_VER_MAJOR				3
#define DPCON_VER_MINOR				2

/* Command versioning */
#define DPCON_CMD_BASE_VERSION			1
#define DPCON_CMD_ID_OFFSET			4

#define DPCON_CMD(id)	(((id) << DPCON_CMD_ID_OFFSET) | DPCON_CMD_BASE_VERSION)

/* Command IDs */
#define DPCON_CMDID_CLOSE			DPCON_CMD(0x800)
#define DPCON_CMDID_OPEN			DPCON_CMD(0x808)

#define DPCON_CMDID_ENABLE			DPCON_CMD(0x002)
#define DPCON_CMDID_DISABLE			DPCON_CMD(0x003)
#define DPCON_CMDID_GET_ATTR			DPCON_CMD(0x004)
#define DPCON_CMDID_RESET			DPCON_CMD(0x005)

#define DPCON_CMDID_SET_NOTIFICATION		DPCON_CMD(0x100)

struct dpcon_cmd_open {
	__le32 dpcon_id;
};

#define DPCON_ENABLE			1

struct dpcon_rsp_get_attr {
	/* response word 0 */
	__le32 id;
	__le16 qbman_ch_id;
	u8 num_priorities;
	u8 pad;
};

struct dpcon_cmd_set_notification {
	/* cmd word 0 */
	__le32 dpio_id;
	u8 priority;
	u8 pad[3];
	/* cmd word 1 */
	__le64 user_ctx;
};

/**
 * Maximum number of total IRQs that can be pre-allocated for an MC bus'
 * IRQ pool
 */
#define FSL_MC_IRQ_POOL_MAX_TOTAL_IRQS	256

/**
 * struct fsl_mc_resource_pool - Pool of MC resources of a given
 * type
 * @type: type of resources in the pool
 * @max_count: maximum number of resources in the pool
 * @free_count: number of free resources in the pool
 * @mutex: mutex to serialize access to the pool's free list
 * @free_list: anchor node of list of free resources in the pool
 * @mc_bus: pointer to the MC bus that owns this resource pool
 */
struct fsl_mc_resource_pool {
	enum fsl_mc_pool_type type;
	int max_count;
	int free_count;
	struct mutex mutex;	/* serializes access to free_list */
	struct list_head free_list;
	struct fsl_mc_bus *mc_bus;
};

/**
 * struct fsl_mc_bus - logical bus that corresponds to a physical DPRC
 * @mc_dev: fsl-mc device for the bus device itself.
 * @resource_pools: array of resource pools (one pool per resource type)
 * for this MC bus. These resources represent allocatable entities
 * from the physical DPRC.
 * @irq_resources: Pointer to array of IRQ objects for the IRQ pool
 * @scan_mutex: Serializes bus scanning
 * @dprc_attr: DPRC attributes
 */
struct fsl_mc_bus {
	struct fsl_mc_device mc_dev;
	struct fsl_mc_resource_pool resource_pools[FSL_MC_NUM_POOL_TYPES];
	struct fsl_mc_device_irq *irq_resources;
	struct mutex scan_mutex;    /* serializes bus scanning */
	struct dprc_attributes dprc_attr;
};

#define to_fsl_mc_bus(_mc_dev) \
	container_of(_mc_dev, struct fsl_mc_bus, mc_dev)

int __must_check fsl_mc_device_add(struct fsl_mc_obj_desc *obj_desc,
				   struct fsl_mc_io *mc_io,
				   struct device *parent_dev,
				   struct fsl_mc_device **new_mc_dev);

void fsl_mc_device_remove(struct fsl_mc_device *mc_dev);

int __init dprc_driver_init(void);

void dprc_driver_exit(void);

int __init fsl_mc_allocator_driver_init(void);

void fsl_mc_allocator_driver_exit(void);

void fsl_mc_init_all_resource_pools(struct fsl_mc_device *mc_bus_dev);

void fsl_mc_cleanup_all_resource_pools(struct fsl_mc_device *mc_bus_dev);

int __must_check fsl_mc_resource_allocate(struct fsl_mc_bus *mc_bus,
					  enum fsl_mc_pool_type pool_type,
					  struct fsl_mc_resource
							  **new_resource);

void fsl_mc_resource_free(struct fsl_mc_resource *resource);

int fsl_mc_msi_domain_alloc_irqs(struct device *dev,
				 unsigned int irq_count);

void fsl_mc_msi_domain_free_irqs(struct device *dev);

int fsl_mc_find_msi_domain(struct device *mc_platform_dev,
			   struct irq_domain **mc_msi_domain);

int fsl_mc_populate_irq_pool(struct fsl_mc_bus *mc_bus,
			     unsigned int irq_count);

void fsl_mc_cleanup_irq_pool(struct fsl_mc_bus *mc_bus);

int __must_check fsl_create_mc_io(struct device *dev,
				  phys_addr_t mc_portal_phys_addr,
				  u32 mc_portal_size,
				  struct fsl_mc_device *dpmcp_dev,
				  u32 flags, struct fsl_mc_io **new_mc_io);

void fsl_destroy_mc_io(struct fsl_mc_io *mc_io);

bool fsl_mc_is_root_dprc(struct device *dev);

#endif /* _FSL_MC_PRIVATE_H_ */

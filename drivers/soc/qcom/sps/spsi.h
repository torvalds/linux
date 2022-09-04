/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2011-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */
/**
 * Smart-Peripheral-Switch (SPS) internal API.
 */

#ifndef _SPSI_H_
#define _SPSI_H_

#include <linux/types.h>
#include <linux/list.h>
#include <linux/kernel.h>
#include <linux/compiler.h>
#include <linux/ratelimit.h>
#include <linux/ipc_logging.h>

#include <linux/msm-sps.h>

#include "sps_map.h"

#if defined(CONFIG_PHYS_ADDR_T_64BIT) || defined(CONFIG_ARM_LPAE)
#define SPS_LPAE (true)
#else
#define SPS_LPAE (false)
#endif

#define BAM_MAX_PIPES              31
#define BAM_MAX_P_LOCK_GROUP_NUM   31

/* Adjust for offset of struct sps_q_event */
#define SPS_EVENT_INDEX(e)    ((e) - 1)
#define SPS_ERROR -1

/* BAM identifier used in log messages */
#define BAM_ID(dev)       (&(dev)->props.phys_addr)

/* "Clear" value for the connection parameter struct */
#define SPSRM_CLEAR     0xccccccccUL
#define SPSRM_ADDR_CLR \
	((sizeof(int) == sizeof(long)) ? 0 : (SPSRM_CLEAR << 32))

#define MAX_MSG_LEN 80
#define SPS_IPC_LOGPAGES 10
#define SPS_IPC_REG_DUMP_FACTOR 3
#define SPS_IPC_DEFAULT_LOGLEVEL 3
#define SPS_IPC_MAX_LOGLEVEL 4

/* Connection mapping control struct */
struct sps_rm {
	struct list_head connections_q;
	struct mutex lock;
};

/* SPS driver state struct */
struct sps_drv {
	struct class *dev_class;
	dev_t dev_num;
	struct device *dev;
	struct clk *pmem_clk;
	struct clk *bamdma_clk;
	struct clk *dfab_clk;

	int is_ready;

	/* Platform data */
	phys_addr_t pipemem_phys_base;
	u32 pipemem_size;
	phys_addr_t bamdma_bam_phys_base;
	u32 bamdma_bam_size;
	phys_addr_t bamdma_dma_phys_base;
	u32 bamdma_dma_size;
	u32 bamdma_irq;
	u32 bamdma_restricted_pipes;

	/* Driver options bitflags (see SPS_OPT_*) */
	u32 options;

	/* Mutex to protect BAM and connection queues */
	struct mutex lock;

	/* BAM devices */
	struct list_head bams_q;

	char *hal_bam_version;

	/* Connection control state */
	struct sps_rm connection_ctrl;

	void *ipc_log0;
	void *ipc_log1;
	void *ipc_log2;
	void *ipc_log3;
	void *ipc_log4;

	u32 ipc_loglevel;
};

extern struct sps_drv *sps;
extern u32 d_type;
extern bool enhd_pipe;
extern bool imem;
extern enum sps_bam_type bam_type;

#ifdef CONFIG_DEBUG_FS
extern u8 debugfs_record_enabled;
extern u8 logging_option;
extern u8 debug_level_option;
extern u8 print_limit_option;

#define SPS_IPC(idx, dev, msg, ...) do { \
		if (dev) { \
			if (idx == 0) \
				ipc_log_string((dev)->ipc_log0, \
					"%s: " msg, __func__, ##__VA_ARGS__); \
			else if (idx == 1) \
				ipc_log_string((dev)->ipc_log1, \
					"%s: " msg, __func__, ##__VA_ARGS__); \
			else if (idx == 2) \
				ipc_log_string((dev)->ipc_log2, \
					"%s: " msg, __func__, ##__VA_ARGS__); \
			else if (idx == 3) \
				ipc_log_string((dev)->ipc_log3, \
					"%s: " msg, __func__, ##__VA_ARGS__); \
			else if (idx == 4) \
				ipc_log_string((dev)->ipc_log4, \
					"%s: " msg, __func__, ##__VA_ARGS__); \
		} \
	} while (0)
#define SPS_DUMP(msg, ...) do {					\
		SPS_IPC(4, sps, msg, ##__VA_ARGS__); \
		if (sps) { \
			if (sps->ipc_log4 == NULL) \
				pr_info("%s: " msg, __func__, ##__VA_ARGS__); \
		} \
	} while (0)
#define SPS_ERR(dev, msg, ...) do {					\
		if (logging_option != 1) {	\
			if (unlikely(print_limit_option > 2))	\
				pr_err_ratelimited( \
				"%s: " msg, __func__, ##__VA_ARGS__);	\
			else	\
				pr_err("%s: " msg, __func__, ##__VA_ARGS__); \
		}	\
		SPS_IPC(3, dev, msg, ##__VA_ARGS__); \
	} while (0)
#define SPS_INFO(dev, msg, ...) do {				\
		if (logging_option != 1) {	\
			if (unlikely(print_limit_option > 1))	\
				pr_info_ratelimited( \
				"%s: " msg, __func__, ##__VA_ARGS__);	\
			else	\
				pr_info("%s: " msg, __func__, ##__VA_ARGS__); \
		}	\
		SPS_IPC(3, dev, msg, ##__VA_ARGS__); \
	} while (0)
#define SPS_DBG(dev, msg, ...) do {					\
		if ((unlikely(logging_option > 1))	\
			&& (unlikely(debug_level_option > 3))) {\
			if (unlikely(print_limit_option > 0))	\
				pr_info_ratelimited( \
					"%s: " msg, __func__, ##__VA_ARGS__); \
			else	\
				pr_info("%s: " msg, __func__, ##__VA_ARGS__); \
		} else	\
			pr_debug("%s: " msg, __func__, ##__VA_ARGS__);	\
		if (dev) { \
			if ((dev)->ipc_loglevel <= 0)	\
				SPS_IPC(0, dev, msg, ##__VA_ARGS__); \
		}	\
	} while (0)
#define SPS_DBG1(dev, msg, ...) do {				\
		if ((unlikely(logging_option > 1))	\
			&& (unlikely(debug_level_option > 2))) {\
			if (unlikely(print_limit_option > 0))	\
				pr_info_ratelimited( \
					"%s: " msg, __func__, ##__VA_ARGS__); \
			else	\
				pr_info("%s: " msg, __func__, ##__VA_ARGS__); \
		} else	\
			pr_debug("%s: " msg, __func__, ##__VA_ARGS__);	\
		if (dev) { \
			if ((dev)->ipc_loglevel <= 1)	\
				SPS_IPC(1, dev, msg, ##__VA_ARGS__);	\
		}	\
	} while (0)
#define SPS_DBG2(dev, msg, ...) do {				\
		if ((unlikely(logging_option > 1))	\
			&& (unlikely(debug_level_option > 1))) {\
			if (unlikely(print_limit_option > 0))	\
				pr_info_ratelimited( \
					"%s: " msg, __func__, ##__VA_ARGS__); \
			else	\
				pr_info("%s: " msg, __func__, ##__VA_ARGS__); \
		} else	\
			pr_debug("%s: " msg, __func__, ##__VA_ARGS__);	\
		if (dev) { \
			if ((dev)->ipc_loglevel <= 2)	\
				SPS_IPC(2, dev, msg, ##__VA_ARGS__); \
		}	\
	} while (0)
#define SPS_DBG3(dev, msg, ...) do {				\
		if ((unlikely(logging_option > 1))	\
			&& (unlikely(debug_level_option > 0))) {\
			if (unlikely(print_limit_option > 0))	\
				pr_info_ratelimited( \
					"%s: " msg, __func__, ##__VA_ARGS__); \
			else	\
				pr_info("%s: " msg, __func__, ##__VA_ARGS__); \
		} else	\
			pr_debug("%s: " msg, __func__, ##__VA_ARGS__);	\
		if (dev) { \
			if ((dev)->ipc_loglevel <= 3)	\
				SPS_IPC(3, dev, msg, ##__VA_ARGS__); \
		}	\
	} while (0)
#else
#define SPS_DBG3(dev, msg, args...)             pr_debug(msg, ##args)
#define SPS_DBG2(dev, msg, args...)             pr_debug(msg, ##args)
#define SPS_DBG1(dev, msg, args...)             pr_debug(msg, ##args)
#define SPS_DBG(dev, msg, args...)              pr_debug(msg, ##args)
#define SPS_INFO(dev, msg, args...)             pr_info(msg, ##args)
#define SPS_ERR(dev, msg, args...)              pr_err(msg, ##args)
#define SPS_DUMP(msg, args...)                  pr_info(msg, ##args)
#endif

/* End point parameters */
struct sps_conn_end_pt {
	unsigned long dev;		/* Device handle of BAM */
	phys_addr_t bam_phys;		/* Physical address of BAM. */
	u32 pipe_index;		/* Pipe index */
	u32 event_threshold;	/* Pipe event threshold */
	u32 lock_group;	/* The lock group this pipe belongs to */
	void *bam;
};

/* Connection bookkeeping descriptor struct */
struct sps_connection {
	struct list_head list;

	/* Source end point parameters */
	struct sps_conn_end_pt src;

	/* Destination end point parameters */
	struct sps_conn_end_pt dest;

	/* Resource parameters */
	struct sps_mem_buffer desc;	/* Descriptor FIFO */
	struct sps_mem_buffer data;	/* Data FIFO (BAM-to-BAM mode only) */
	u32 config;		/* Client specified connection configuration */

	/* Connection state */
	void *client_src;
	void *client_dest;
	int refs;		/* Reference counter */

	/* Dynamically allocated resources, if required */
	u32 alloc_src_pipe;	/* Source pipe index */
	u32 alloc_dest_pipe;	/* Destination pipe index */
	/* Physical address of descriptor FIFO */
	phys_addr_t alloc_desc_base;
	phys_addr_t alloc_data_base;	/* Physical address of data FIFO */
};

/* Event bookkeeping descriptor struct */
struct sps_q_event {
	struct list_head list;
	/* Event payload data */
	struct sps_event_notify notify;
};

/* Memory heap statistics */
struct sps_mem_stats {
	u32 base_addr;
	u32 size;
	u32 blocks_used;
	u32 bytes_used;
	u32 max_bytes_used;
};

enum sps_bam_type {
	SPS_BAM_LEGACY,
	SPS_BAM_NDP,
	SPS_BAM_NDP_4K
};

#ifdef CONFIG_DEBUG_FS
/* record debug info for debugfs */
void sps_debugfs_record(const char *msg);
#endif

/* output the content of BAM-level registers */
void print_bam_reg(void *virt_addr);

/* output the content of BAM pipe registers */
void print_bam_pipe_reg(void *virt_addr, u32 pipe_index);

/* output the content of selected BAM-level registers */
void print_bam_selected_reg(void *virt_addr, u32 pipe_index);

/* output the content of selected BAM pipe registers */
void print_bam_pipe_selected_reg(void *virt_addr, u32 pipe_index);

/* output descriptor FIFO of a pipe */
void print_bam_pipe_desc_fifo(void *virt_addr, u32 pipe_index, u32 option);

/* output BAM_TEST_BUS_REG */
void print_bam_test_bus_reg(void *base, u32 tb_sel);

/* halt and un-halt a pipe */
void bam_pipe_halt(void *base, u32 pipe, bool halt);

/**
 * Translate physical to virtual address
 *
 * This Function translates physical to virtual address.
 *
 * @phys_addr - physical address to translate
 *
 * @return virtual memory pointer
 *
 */
void *spsi_get_mem_ptr(phys_addr_t phys_addr);

/**
 * Allocate I/O (pipe) memory
 *
 * This function allocates target I/O (pipe) memory.
 *
 * @bytes - number of bytes to allocate
 *
 * @return physical address of allocated memory, or SPS_ADDR_INVALID on error
 */
phys_addr_t sps_mem_alloc_io(u32 bytes);

/**
 * Free I/O (pipe) memory
 *
 * This function frees target I/O (pipe) memory.
 *
 * @phys_addr - physical address of memory to free
 *
 * @bytes - number of bytes to free.
 */
void sps_mem_free_io(phys_addr_t phys_addr, u32 bytes);

/**
 * Find matching connection mapping
 *
 * This function searches for a connection mapping that matches the
 * parameters supplied by the client.  If a match is found, the client's
 * parameter struct is updated with the values specified in the mapping.
 *
 * @connect - pointer to client connection parameters
 *
 * @return 0 if match is found, negative value otherwise
 *
 */
int sps_map_find(struct sps_connect *connect);

/**
 * Allocate a BAM DMA pipe
 *
 * This function allocates a BAM DMA pipe, and is intended to be called
 * internally from the BAM resource manager.  Allocation implies that
 * the pipe has been referenced by a client Connect() and is in use.
 *
 * BAM DMA is permissive with activations, and allows a pipe to be allocated
 * with or without a client-initiated allocation.  This allows the client to
 * specify exactly which pipe should be used directly through the Connect() API.
 * sps_dma_alloc_chan() does not allow the client to specify the pipes/channel.
 *
 * @bam - pointer to BAM device descriptor
 *
 * @pipe_index - pipe index
 *
 * @dir - pipe direction
 *
 * @return 0 on success, negative value on error
 */
int sps_dma_pipe_alloc(void *bam, u32 pipe_index, enum sps_mode dir);

/**
 * Enable a BAM DMA pipe
 *
 * This function enables the channel associated with a BAM DMA pipe, and
 * is intended to be called internally from the BAM resource manager.
 * Enable must occur *after* the pipe has been enabled so that proper
 * sequencing between pipe and DMA channel enables can be enforced.
 *
 * @bam - pointer to BAM device descriptor
 *
 * @pipe_index - pipe index
 *
 * @return 0 on success, negative value on error
 *
 */
int sps_dma_pipe_enable(void *bam, u32 pipe_index);

/**
 * Free a BAM DMA pipe
 *
 * This function disables and frees a BAM DMA pipe, and is intended to be
 * called internally from the BAM resource manager.  This must occur *after*
 * the pipe has been disabled/reset so that proper sequencing between pipe and
 * DMA channel resets can be enforced.
 *
 * @bam_arg - pointer to BAM device descriptor
 *
 * @pipe_index - pipe index
 *
 * @return 0 on success, negative value on error
 *
 */
int sps_dma_pipe_free(void *bam, u32 pipe_index);

/**
 * Initialize driver memory module
 *
 * This function initializes the driver memory module.
 *
 * @pipemem_phys_base - Pipe-Memory physical base.
 *
 * @pipemem_size - Pipe-Memory size.
 *
 * @return 0 on success, negative value on error
 *
 */
int sps_mem_init(phys_addr_t pipemem_phys_base, u32 pipemem_size);

/**
 * De-initialize driver memory module
 *
 * This function de-initializes the driver memory module.
 *
 * @return 0 on success, negative value on error
 *
 */
int sps_mem_de_init(void);

/**
 * Initialize BAM DMA module
 *
 * This function initializes the BAM DMA module.
 *
 * @bam_props - pointer to BAM DMA devices BSP configuration properties
 *
 * @return 0 on success, negative value on error
 *
 */
int sps_dma_init(const struct sps_bam_props *bam_props);

/**
 * De-initialize BAM DMA module
 *
 * This function de-initializes the SPS BAM DMA module.
 *
 */
void sps_dma_de_init(void);

/**
 * Initialize BAM DMA device
 *
 * This function initializes a BAM DMA device.
 *
 * @h - BAM handle
 *
 * @return 0 on success, negative value on error
 *
 */
int sps_dma_device_init(unsigned long h);

/**
 * De-initialize BAM DMA device
 *
 * This function de-initializes a BAM DMA device.
 *
 * @h - BAM handle
 *
 * @return 0 on success, negative value on error
 *
 */
int sps_dma_device_de_init(unsigned long h);

/**
 * Initialize connection mapping module
 *
 * This function initializes the SPS connection mapping module.
 *
 * @map_props - pointer to connection mapping BSP configuration properties
 *
 * @options - driver options bitflags (see SPS_OPT_*)
 *
 * @return 0 on success, negative value on error
 *
 */

int sps_map_init(const struct sps_map *map_props, u32 options);

/**
 * De-initialize connection mapping module
 *
 * This function de-initializes the SPS connection mapping module.
 *
 */
void sps_map_de_init(void);

/*
 * bam_pipe_reset - reset a BAM pipe.
 * @base:	BAM virtual address
 * @pipe:	pipe index
 *
 * This function resets a BAM pipe.
 */
void bam_pipe_reset(void *base, u32 pipe);

/*
 * bam_disable_pipe - disable a BAM pipe.
 * @base:	BAM virtual address
 * @pipe:	pipe index
 *
 * This function disables a BAM pipe.
 */
void bam_disable_pipe(void *base, u32 pipe);
#endif	/* _SPSI_H_ */

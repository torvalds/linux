/*
 * dbdefs.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Global definitions and constants for DSP/BIOS Bridge.
 *
 * Copyright (C) 2005-2006 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef DBDEFS_
#define DBDEFS_

#include <linux/types.h>

#include <dspbridge/rms_sh.h>	/* Types shared between GPP and DSP */

#define PG_SIZE4K 4096
#define PG_MASK(pg_size) (~((pg_size)-1))
#define PG_ALIGN_LOW(addr, pg_size) ((addr) & PG_MASK(pg_size))
#define PG_ALIGN_HIGH(addr, pg_size) (((addr)+(pg_size)-1) & PG_MASK(pg_size))

/* API return value and calling convention */
#define DBAPI                       int

/* Maximum length of node name, used in dsp_ndbprops */
#define DSP_MAXNAMELEN              32

/* notify_type values for the RegisterNotify() functions. */
#define DSP_SIGNALEVENT             0x00000001

/* Types of events for processors */
#define DSP_PROCESSORSTATECHANGE    0x00000001
#define DSP_PROCESSORATTACH         0x00000002
#define DSP_PROCESSORDETACH         0x00000004
#define DSP_PROCESSORRESTART        0x00000008

/* DSP exception events (DSP/BIOS and DSP MMU fault) */
#define DSP_MMUFAULT                0x00000010
#define DSP_SYSERROR                0x00000020
#define DSP_EXCEPTIONABORT          0x00000300
#define DSP_PWRERROR                0x00000080
#define DSP_WDTOVERFLOW	0x00000040

/* IVA exception events (IVA MMU fault) */
#define IVA_MMUFAULT                0x00000040
/* Types of events for nodes */
#define DSP_NODESTATECHANGE         0x00000100
#define DSP_NODEMESSAGEREADY        0x00000200

/* Types of events for streams */
#define DSP_STREAMDONE              0x00001000
#define DSP_STREAMIOCOMPLETION      0x00002000

/* Handle definition representing the GPP node in DSPNode_Connect() calls */
#define DSP_HGPPNODE                0xFFFFFFFF

/* Node directions used in DSPNode_Connect() */
#define DSP_TONODE                  1
#define DSP_FROMNODE                2

/* Define Node Minimum and Maximum Priorities */
#define DSP_NODE_MIN_PRIORITY       1
#define DSP_NODE_MAX_PRIORITY       15

/* msg_ctrl contains SM buffer description */
#define DSP_RMSBUFDESC RMS_BUFDESC

/* Processor ID numbers */
#define DSP_UNIT    0
#define IVA_UNIT    1

#define DSPWORD       unsigned char
#define DSPWORDSIZE     sizeof(DSPWORD)

#define    MAX_PROFILES     16

/* DSP chip type */
#define DSPTYPE64	0x99

/* Handy Macros */
#define VALID_PROC_EVENT (DSP_PROCESSORSTATECHANGE | DSP_PROCESSORATTACH | \
	DSP_PROCESSORDETACH | DSP_PROCESSORRESTART | DSP_NODESTATECHANGE | \
	DSP_STREAMDONE | DSP_STREAMIOCOMPLETION | DSP_MMUFAULT | \
	DSP_SYSERROR | DSP_WDTOVERFLOW | DSP_PWRERROR)

static inline bool is_valid_proc_event(u32 x)
{
	return (x == 0 || (x & VALID_PROC_EVENT && !(x & ~VALID_PROC_EVENT)));
}

/* The Node UUID structure */
struct dsp_uuid {
	u32 data1;
	u16 data2;
	u16 data3;
	u8 data4;
	u8 data5;
	u8 data6[6];
};

/* DCD types */
enum dsp_dcdobjtype {
	DSP_DCDNODETYPE,
	DSP_DCDPROCESSORTYPE,
	DSP_DCDLIBRARYTYPE,
	DSP_DCDCREATELIBTYPE,
	DSP_DCDEXECUTELIBTYPE,
	DSP_DCDDELETELIBTYPE,
	/* DSP_DCDMAXOBJTYPE is meant to be the last DCD object type */
	DSP_DCDMAXOBJTYPE
};

/* Processor states */
enum dsp_procstate {
	PROC_STOPPED,
	PROC_LOADED,
	PROC_RUNNING,
	PROC_ERROR
};

/*
 *  Node types: Message node, task node, xDAIS socket node, and
 *  device node. _NODE_GPP is used when defining a stream connection
 *  between a task or socket node and the GPP.
 *
 */
enum node_type {
	NODE_DEVICE,
	NODE_TASK,
	NODE_DAISSOCKET,
	NODE_MESSAGE,
	NODE_GPP
};

/*
 *  ======== node_state ========
 *  Internal node states.
 */
enum node_state {
	NODE_ALLOCATED,
	NODE_CREATED,
	NODE_RUNNING,
	NODE_PAUSED,
	NODE_DONE,
	NODE_CREATING,
	NODE_STARTING,
	NODE_PAUSING,
	NODE_TERMINATING,
	NODE_DELETING,
};

/* Stream states */
enum dsp_streamstate {
	STREAM_IDLE,
	STREAM_READY,
	STREAM_PENDING,
	STREAM_DONE
};

/* Stream connect types */
enum dsp_connecttype {
	CONNECTTYPE_NODEOUTPUT,
	CONNECTTYPE_GPPOUTPUT,
	CONNECTTYPE_NODEINPUT,
	CONNECTTYPE_GPPINPUT
};

/* Stream mode types */
enum dsp_strmmode {
	STRMMODE_PROCCOPY,	/* Processor(s) copy stream data payloads */
	STRMMODE_ZEROCOPY,	/* Strm buffer ptrs swapped no data copied */
	STRMMODE_LDMA,		/* Local DMA : OMAP's System-DMA device */
	STRMMODE_RDMA		/* Remote DMA: OMAP's DSP-DMA device */
};

/* Resource Types */
enum dsp_resourceinfotype {
	DSP_RESOURCE_DYNDARAM = 0,
	DSP_RESOURCE_DYNSARAM,
	DSP_RESOURCE_DYNEXTERNAL,
	DSP_RESOURCE_DYNSRAM,
	DSP_RESOURCE_PROCLOAD
};

/* Memory Segment Types */
enum dsp_memtype {
	DSP_DYNDARAM = 0,
	DSP_DYNSARAM,
	DSP_DYNEXTERNAL,
	DSP_DYNSRAM
};

/* Memory Flush Types */
enum dsp_flushtype {
	PROC_INVALIDATE_MEM = 0,
	PROC_WRITEBACK_MEM,
	PROC_WRITEBACK_INVALIDATE_MEM,
};

/* Memory Segment Status Values */
struct dsp_memstat {
	u32 size;
	u32 total_free_size;
	u32 len_max_free_block;
	u32 num_free_blocks;
	u32 num_alloc_blocks;
};

/* Processor Load information Values */
struct dsp_procloadstat {
	u32 curr_load;
	u32 predicted_load;
	u32 curr_dsp_freq;
	u32 predicted_freq;
};

/* Attributes for STRM connections between nodes */
struct dsp_strmattr {
	u32 seg_id;		/* Memory segment on DSP to allocate buffers */
	u32 buf_size;		/* Buffer size (DSP words) */
	u32 num_bufs;		/* Number of buffers */
	u32 buf_alignment;	/* Buffer alignment */
	u32 timeout;		/* Timeout for blocking STRM calls */
	enum dsp_strmmode strm_mode;	/* mode of stream when opened */
	/* DMA chnl id if dsp_strmmode is LDMA or RDMA */
	u32 dma_chnl_id;
	u32 dma_priority;	/* DMA channel priority 0=lowest, >0=high */
};

/* The dsp_cbdata structure */
struct dsp_cbdata {
	u32 cb_data;
	u8 node_data[1];
};

/* The dsp_msg structure */
struct dsp_msg {
	u32 cmd;
	u32 arg1;
	u32 arg2;
};

/* The dsp_resourcereqmts structure for node's resource requirements */
struct dsp_resourcereqmts {
	u32 cb_struct;
	u32 static_data_size;
	u32 global_data_size;
	u32 program_mem_size;
	u32 wc_execution_time;
	u32 wc_period;
	u32 wc_deadline;
	u32 avg_exection_time;
	u32 minimum_period;
};

/*
 * The dsp_streamconnect structure describes a stream connection
 * between two nodes, or between a node and the GPP
 */
struct dsp_streamconnect {
	u32 cb_struct;
	enum dsp_connecttype connect_type;
	u32 this_node_stream_index;
	void *connected_node;
	struct dsp_uuid ui_connected_node_id;
	u32 connected_node_stream_index;
};

struct dsp_nodeprofs {
	u32 heap_size;
};

/* The dsp_ndbprops structure reports the attributes of a node */
struct dsp_ndbprops {
	u32 cb_struct;
	struct dsp_uuid ui_node_id;
	char ac_name[DSP_MAXNAMELEN];
	enum node_type ntype;
	u32 cache_on_gpp;
	struct dsp_resourcereqmts dsp_resource_reqmts;
	s32 prio;
	u32 stack_size;
	u32 sys_stack_size;
	u32 stack_seg;
	u32 message_depth;
	u32 num_input_streams;
	u32 num_output_streams;
	u32 timeout;
	u32 count_profiles;	/* Number of supported profiles */
	/* Array of profiles */
	struct dsp_nodeprofs node_profiles[MAX_PROFILES];
	u32 stack_seg_name;	/* Stack Segment Name */
};

	/* The dsp_nodeattrin structure describes the attributes of a
	 * node client */
struct dsp_nodeattrin {
	u32 cb_struct;
	s32 prio;
	u32 timeout;
	u32 profile_id;
	/* Reserved, for Bridge Internal use only */
	u32 heap_size;
	void *pgpp_virt_addr;	/* Reserved, for Bridge Internal use only */
};

	/* The dsp_nodeinfo structure is used to retrieve information
	 * about a node */
struct dsp_nodeinfo {
	u32 cb_struct;
	struct dsp_ndbprops nb_node_database_props;
	u32 execution_priority;
	enum node_state ns_execution_state;
	void *device_owner;
	u32 number_streams;
	struct dsp_streamconnect sc_stream_connection[16];
	u32 node_env;
};

	/* The dsp_nodeattr structure describes the attributes of a node */
struct dsp_nodeattr {
	u32 cb_struct;
	struct dsp_nodeattrin in_node_attr_in;
	u32 node_attr_inputs;
	u32 node_attr_outputs;
	struct dsp_nodeinfo node_info;
};

/*
 *  Notification type: either the name of an opened event, or an event or
 *  window handle.
 */
struct dsp_notification {
	char *name;
	void *handle;
};

/* The dsp_processorattrin structure describes the attributes of a processor */
struct dsp_processorattrin {
	u32 cb_struct;
	u32 timeout;
};
/*
 * The dsp_processorinfo structure describes basic capabilities of a
 * DSP processor
 */
struct dsp_processorinfo {
	u32 cb_struct;
	int processor_family;
	int processor_type;
	u32 clock_rate;
	u32 internal_mem_size;
	u32 external_mem_size;
	u32 processor_id;
	int ty_running_rtos;
	s32 node_min_priority;
	s32 node_max_priority;
};

/* Error information of last DSP exception signalled to the GPP */
struct dsp_errorinfo {
	u32 err_mask;
	u32 val1;
	u32 val2;
	u32 val3;
};

/* The dsp_processorstate structure describes the state of a DSP processor */
struct dsp_processorstate {
	u32 cb_struct;
	enum dsp_procstate proc_state;
};

/*
 * The dsp_resourceinfo structure is used to retrieve information about a
 * processor's resources
 */
struct dsp_resourceinfo {
	u32 cb_struct;
	enum dsp_resourceinfotype resource_type;
	union {
		u32 resource;
		struct dsp_memstat mem_stat;
		struct dsp_procloadstat proc_load_stat;
	} result;
};

/*
 * The dsp_streamattrin structure describes the attributes of a stream,
 * including segment and alignment of data buffers allocated with
 * DSPStream_AllocateBuffers(), if applicable
 */
struct dsp_streamattrin {
	u32 cb_struct;
	u32 timeout;
	u32 segment_id;
	u32 buf_alignment;
	u32 num_bufs;
	enum dsp_strmmode strm_mode;
	u32 dma_chnl_id;
	u32 dma_priority;
};

/* The dsp_bufferattr structure describes the attributes of a data buffer */
struct dsp_bufferattr {
	u32 cb_struct;
	u32 segment_id;
	u32 buf_alignment;
};

/*
 *  The dsp_streaminfo structure is used to retrieve information
 *  about a stream.
 */
struct dsp_streaminfo {
	u32 cb_struct;
	u32 number_bufs_allowed;
	u32 number_bufs_in_stream;
	u32 number_bytes;
	void *sync_object_handle;
	enum dsp_streamstate ss_stream_state;
};

/* DMM MAP attributes
It is a bit mask with each bit value indicating a specific attribute
bit 0 - GPP address type (user virtual=0, physical=1)
bit 1 - MMU Endianism (Big Endian=1, Little Endian=0)
bit 2 - MMU mixed page attribute (Mixed/ CPUES=1, TLBES =0)
bit 3 - MMU element size = 8bit (valid only for non mixed page entries)
bit 4 - MMU element size = 16bit (valid only for non mixed page entries)
bit 5 - MMU element size = 32bit (valid only for non mixed page entries)
bit 6 - MMU element size = 64bit (valid only for non mixed page entries)

bit 14 - Input (read only) buffer
bit 15 - Output (writeable) buffer
*/

/* Types of mapping attributes */

/* MPU address is virtual and needs to be translated to physical addr */
#define DSP_MAPVIRTUALADDR          0x00000000
#define DSP_MAPPHYSICALADDR         0x00000001

/* Mapped data is big endian */
#define DSP_MAPBIGENDIAN            0x00000002
#define DSP_MAPLITTLEENDIAN         0x00000000

/* Element size is based on DSP r/w access size */
#define DSP_MAPMIXEDELEMSIZE        0x00000004

/*
 * Element size for MMU mapping (8, 16, 32, or 64 bit)
 * Ignored if DSP_MAPMIXEDELEMSIZE enabled
 */
#define DSP_MAPELEMSIZE8            0x00000008
#define DSP_MAPELEMSIZE16           0x00000010
#define DSP_MAPELEMSIZE32           0x00000020
#define DSP_MAPELEMSIZE64           0x00000040

#define DSP_MAPVMALLOCADDR         0x00000080

#define DSP_MAPDONOTLOCK	   0x00000100

#define DSP_MAP_DIR_MASK		0x3FFF

#define GEM_CACHE_LINE_SIZE     128
#define GEM_L1P_PREFETCH_SIZE   128

/*
 * Definitions from dbreg.h
 */

#define DSPPROCTYPE_C64		6410
#define IVAPROCTYPE_ARM7	470

/* Max registry path length. Also the max registry value length. */
#define MAXREGPATHLENGTH	255

#endif /* DBDEFS_ */

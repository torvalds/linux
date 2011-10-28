/*
 * rms_sh.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * DSP/BIOS Bridge Resource Manager Server shared definitions (used on both
 * GPP and DSP sides).
 *
 * Copyright (C) 2008 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef RMS_SH_
#define RMS_SH_

#include <dspbridge/rmstypes.h>

/* Memory Types: */
#define RMS_CODE                0	/* Program space */
#define RMS_DATA                1	/* Data space */

/* RM Server Command and Response Buffer Sizes: */
#define RMS_COMMANDBUFSIZE     256	/* Size of command buffer */

/* Pre-Defined Command/Response Codes: */
#define RMS_EXIT                0x80000000	/* GPP->Node: shutdown */
#define RMS_EXITACK             0x40000000	/* Node->GPP: ack shutdown */
#define RMS_BUFDESC             0x20000000	/* Arg1 SM buf, Arg2 SM size */
#define RMS_KILLTASK            0x10000000	/* GPP->Node: Kill Task */

/* RM Server RPC Command Structure: */
struct rms_command {
	rms_word fxn;		/* Server function address */
	rms_word arg1;		/* First argument */
	rms_word arg2;		/* Second argument */
	rms_word data;		/* Function-specific data array */
};

/*
 *  The rms_strm_def structure defines the parameters for both input and output
 *  streams, and is passed to a node's create function.
 */
struct rms_strm_def {
	rms_word bufsize;	/* Buffer size (in DSP words) */
	rms_word nbufs;		/* Max number of bufs in stream */
	rms_word segid;		/* Segment to allocate buffers */
	rms_word align;		/* Alignment for allocated buffers */
	rms_word timeout;	/* Timeout (msec) for blocking calls */
	char name[1];	/* Device Name (terminated by '\0') */
};

/* Message node create args structure: */
struct rms_msg_args {
	rms_word max_msgs;	/* Max # simultaneous msgs to node */
	rms_word segid;		/* Mem segment for NODE_allocMsgBuf */
	rms_word notify_type;	/* Type of message notification */
	rms_word arg_length;	/* Length (in DSP chars) of arg data */
	rms_word arg_data;	/* Arg data for node */
};

/* Partial task create args structure */
struct rms_more_task_args {
	rms_word priority;	/* Task's runtime priority level */
	rms_word stack_size;	/* Task's stack size */
	rms_word sysstack_size;	/* Task's system stack size (55x) */
	rms_word stack_seg;	/* Memory segment for task's stack */
	rms_word heap_addr;	/* base address of the node memory heap in
				 * external memory (DSP virtual address) */
	rms_word heap_size;	/* size in MAUs of the node memory heap in
				 * external memory */
	rms_word misc;		/* Misc field.  Not used for 'normal'
				 * task nodes; for xDAIS socket nodes
				 * specifies the IALG_Fxn pointer.
				 */
	/* # input STRM definition structures */
	rms_word num_input_streams;
};

#endif /* RMS_SH_ */

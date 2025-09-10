/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright IBM Corp. 2015
 *
 * Authors: Gavin Shan <gwshan@linux.vnet.ibm.com>
 */

#ifndef _ASM_POWERPC_EEH_H
#define _ASM_POWERPC_EEH_H

/* PE states */
#define EEH_PE_STATE_NORMAL		0	/* Normal state		*/
#define EEH_PE_STATE_RESET		1	/* PE reset asserted	*/
#define EEH_PE_STATE_STOPPED_IO_DMA	2	/* Frozen PE		*/
#define EEH_PE_STATE_STOPPED_DMA	4	/* Stopped DMA only	*/
#define EEH_PE_STATE_UNAVAIL		5	/* Unavailable		*/

/* EEH error types and functions */
#define EEH_ERR_TYPE_32			0       /* 32-bits error	*/
#define EEH_ERR_TYPE_64			1       /* 64-bits error	*/
#define EEH_ERR_FUNC_MIN		0
#define EEH_ERR_FUNC_LD_MEM_ADDR	0	/* Memory load	*/
#define EEH_ERR_FUNC_LD_MEM_DATA	1
#define EEH_ERR_FUNC_LD_IO_ADDR		2	/* IO load	*/
#define EEH_ERR_FUNC_LD_IO_DATA		3
#define EEH_ERR_FUNC_LD_CFG_ADDR	4	/* Config load	*/
#define EEH_ERR_FUNC_LD_CFG_DATA	5
#define EEH_ERR_FUNC_ST_MEM_ADDR	6	/* Memory store	*/
#define EEH_ERR_FUNC_ST_MEM_DATA	7
#define EEH_ERR_FUNC_ST_IO_ADDR		8	/* IO store	*/
#define EEH_ERR_FUNC_ST_IO_DATA		9
#define EEH_ERR_FUNC_ST_CFG_ADDR	10	/* Config store	*/
#define EEH_ERR_FUNC_ST_CFG_DATA	11
#define EEH_ERR_FUNC_DMA_RD_ADDR	12	/* DMA read	*/
#define EEH_ERR_FUNC_DMA_RD_DATA	13
#define EEH_ERR_FUNC_DMA_RD_MASTER	14
#define EEH_ERR_FUNC_DMA_RD_TARGET	15
#define EEH_ERR_FUNC_DMA_WR_ADDR	16	/* DMA write	*/
#define EEH_ERR_FUNC_DMA_WR_DATA	17
#define EEH_ERR_FUNC_DMA_WR_MASTER	18
#define EEH_ERR_FUNC_DMA_WR_TARGET	19
#define EEH_ERR_FUNC_MAX		19

#endif /* _ASM_POWERPC_EEH_H */

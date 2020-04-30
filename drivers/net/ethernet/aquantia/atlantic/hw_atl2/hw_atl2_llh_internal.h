/* SPDX-License-Identifier: GPL-2.0-only */
/* Atlantic Network Driver
 * Copyright (C) 2020 Marvell International Ltd.
 */

#ifndef HW_ATL2_LLH_INTERNAL_H
#define HW_ATL2_LLH_INTERNAL_H

/* Register address for firmware shared input buffer */
#define HW_ATL2_MIF_SHARED_BUFFER_IN_ADR(dword) (0x00012000U + (dword) * 0x4U)
/* Register address for firmware shared output buffer */
#define HW_ATL2_MIF_SHARED_BUFFER_OUT_ADR(dword) (0x00013000U + (dword) * 0x4U)

/* pif_host_finished_buf_wr_i Bitfield Definitions
 * Preprocessor definitions for the bitfield "pif_host_finished_buf_wr_i".
 * PORT="pif_host_finished_buf_wr_i"
 */
/* Register address for bitfield rpif_host_finished_buf_wr_i */
#define HW_ATL2_MIF_HOST_FINISHED_WRITE_ADR 0x00000e00u
/* Bitmask for bitfield pif_host_finished_buf_wr_i */
#define HW_ATL2_MIF_HOST_FINISHED_WRITE_MSK 0x00000001u
/* Inverted bitmask for bitfield pif_host_finished_buf_wr_i */
#define HW_ATL2_MIF_HOST_FINISHED_WRITE_MSKN 0xFFFFFFFEu
/* Lower bit position of bitfield pif_host_finished_buf_wr_i */
#define HW_ATL2_MIF_HOST_FINISHED_WRITE_SHIFT 0
/* Width of bitfield pif_host_finished_buf_wr_i */
#define HW_ATL2_MIF_HOST_FINISHED_WRITE_WIDTH 1
/* Default value of bitfield pif_host_finished_buf_wr_i */
#define HW_ATL2_MIF_HOST_FINISHED_WRITE_DEFAULT 0x0

/* pif_mcp_finished_buf_rd_i Bitfield Definitions
 * Preprocessor definitions for the bitfield "pif_mcp_finished_buf_rd_i".
 * PORT="pif_mcp_finished_buf_rd_i"
 */
/* Register address for bitfield pif_mcp_finished_buf_rd_i */
#define HW_ATL2_MIF_MCP_FINISHED_READ_ADR 0x00000e04u
/* Bitmask for bitfield pif_mcp_finished_buf_rd_i */
#define HW_ATL2_MIF_MCP_FINISHED_READ_MSK 0x00000001u
/* Inverted bitmask for bitfield pif_mcp_finished_buf_rd_i */
#define HW_ATL2_MIF_MCP_FINISHED_READ_MSKN 0xFFFFFFFEu
/* Lower bit position of bitfield pif_mcp_finished_buf_rd_i */
#define HW_ATL2_MIF_MCP_FINISHED_READ_SHIFT 0
/* Width of bitfield pif_mcp_finished_buf_rd_i */
#define HW_ATL2_MIF_MCP_FINISHED_READ_WIDTH 1
/* Default value of bitfield pif_mcp_finished_buf_rd_i */
#define HW_ATL2_MIF_MCP_FINISHED_READ_DEFAULT 0x0

#endif /* HW_ATL2_LLH_INTERNAL_H */

/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef _dma_v2_defs_h
#define _dma_v2_defs_h

#define _DMA_V2_NUM_CHANNELS_ID               MaxNumChannels
#define _DMA_V2_CONNECTIONS_ID                Connections
#define _DMA_V2_DEV_ELEM_WIDTHS_ID            DevElemWidths
#define _DMA_V2_DEV_FIFO_DEPTH_ID             DevFifoDepth
#define _DMA_V2_DEV_FIFO_RD_LAT_ID            DevFifoRdLat
#define _DMA_V2_DEV_FIFO_LAT_BYPASS_ID        DevFifoRdLatBypass
#define _DMA_V2_DEV_NO_BURST_ID               DevNoBurst
#define _DMA_V2_DEV_RD_ACCEPT_ID              DevRdAccept
#define _DMA_V2_DEV_SRMD_ID                   DevSRMD
#define _DMA_V2_DEV_HAS_CRUN_ID               CRunMasters
#define _DMA_V2_CTRL_ACK_FIFO_DEPTH_ID        CtrlAckFifoDepth
#define _DMA_V2_CMD_FIFO_DEPTH_ID             CommandFifoDepth
#define _DMA_V2_CMD_FIFO_RD_LAT_ID            CommandFifoRdLat
#define _DMA_V2_CMD_FIFO_LAT_BYPASS_ID        CommandFifoRdLatBypass
#define _DMA_V2_NO_PACK_ID                    has_no_pack

#define _DMA_V2_REG_ALIGN                4
#define _DMA_V2_REG_ADDR_BITS            2

/* Command word */
#define _DMA_V2_CMD_IDX            0
#define _DMA_V2_CMD_BITS           6
#define _DMA_V2_CHANNEL_IDX        (_DMA_V2_CMD_IDX + _DMA_V2_CMD_BITS)
#define _DMA_V2_CHANNEL_BITS       5

/* The command to set a parameter contains the PARAM field next */
#define _DMA_V2_PARAM_IDX          (_DMA_V2_CHANNEL_IDX + _DMA_V2_CHANNEL_BITS)
#define _DMA_V2_PARAM_BITS         4

/* Commands to read, write or init specific blocks contain these
   three values */
#define _DMA_V2_SPEC_DEV_A_XB_IDX  (_DMA_V2_CHANNEL_IDX + _DMA_V2_CHANNEL_BITS)
#define _DMA_V2_SPEC_DEV_A_XB_BITS 8
#define _DMA_V2_SPEC_DEV_B_XB_IDX  (_DMA_V2_SPEC_DEV_A_XB_IDX + _DMA_V2_SPEC_DEV_A_XB_BITS)
#define _DMA_V2_SPEC_DEV_B_XB_BITS 8
#define _DMA_V2_SPEC_YB_IDX        (_DMA_V2_SPEC_DEV_B_XB_IDX + _DMA_V2_SPEC_DEV_B_XB_BITS)
#define _DMA_V2_SPEC_YB_BITS       (32-_DMA_V2_SPEC_DEV_B_XB_BITS-_DMA_V2_SPEC_DEV_A_XB_BITS-_DMA_V2_CMD_BITS-_DMA_V2_CHANNEL_BITS)

/* */
#define _DMA_V2_CMD_CTRL_IDX       4
#define _DMA_V2_CMD_CTRL_BITS      4

/* Packing setup word */
#define _DMA_V2_CONNECTION_IDX     0
#define _DMA_V2_CONNECTION_BITS    4
#define _DMA_V2_EXTENSION_IDX      (_DMA_V2_CONNECTION_IDX + _DMA_V2_CONNECTION_BITS)
#define _DMA_V2_EXTENSION_BITS     1

/* Elements packing word */
#define _DMA_V2_ELEMENTS_IDX        0
#define _DMA_V2_ELEMENTS_BITS       8
#define _DMA_V2_LEFT_CROPPING_IDX  (_DMA_V2_ELEMENTS_IDX + _DMA_V2_ELEMENTS_BITS)
#define _DMA_V2_LEFT_CROPPING_BITS  8

#define _DMA_V2_WIDTH_IDX           0
#define _DMA_V2_WIDTH_BITS         16

#define _DMA_V2_HEIGHT_IDX          0
#define _DMA_V2_HEIGHT_BITS        16

#define _DMA_V2_STRIDE_IDX          0
#define _DMA_V2_STRIDE_BITS        32

/* Command IDs */
#define _DMA_V2_MOVE_B2A_COMMAND                             0      
#define _DMA_V2_MOVE_B2A_BLOCK_COMMAND                       1      
#define _DMA_V2_MOVE_B2A_NO_SYNC_CHK_COMMAND                 2      
#define _DMA_V2_MOVE_B2A_BLOCK_NO_SYNC_CHK_COMMAND           3      
#define _DMA_V2_MOVE_A2B_COMMAND                             4      
#define _DMA_V2_MOVE_A2B_BLOCK_COMMAND                       5      
#define _DMA_V2_MOVE_A2B_NO_SYNC_CHK_COMMAND                 6      
#define _DMA_V2_MOVE_A2B_BLOCK_NO_SYNC_CHK_COMMAND           7      
#define _DMA_V2_INIT_A_COMMAND                               8      
#define _DMA_V2_INIT_A_BLOCK_COMMAND                         9      
#define _DMA_V2_INIT_A_NO_SYNC_CHK_COMMAND                  10      
#define _DMA_V2_INIT_A_BLOCK_NO_SYNC_CHK_COMMAND            11      
#define _DMA_V2_INIT_B_COMMAND                              12      
#define _DMA_V2_INIT_B_BLOCK_COMMAND                        13      
#define _DMA_V2_INIT_B_NO_SYNC_CHK_COMMAND                  14      
#define _DMA_V2_INIT_B_BLOCK_NO_SYNC_CHK_COMMAND            15      
#define _DMA_V2_NO_ACK_MOVE_B2A_NO_SYNC_CHK_COMMAND         (_DMA_V2_MOVE_B2A_NO_SYNC_CHK_COMMAND       + 16) 
#define _DMA_V2_NO_ACK_MOVE_B2A_BLOCK_NO_SYNC_CHK_COMMAND   (_DMA_V2_MOVE_B2A_BLOCK_NO_SYNC_CHK_COMMAND + 16) 
#define _DMA_V2_NO_ACK_MOVE_A2B_NO_SYNC_CHK_COMMAND         (_DMA_V2_MOVE_A2B_NO_SYNC_CHK_COMMAND       + 16) 
#define _DMA_V2_NO_ACK_MOVE_A2B_BLOCK_NO_SYNC_CHK_COMMAND   (_DMA_V2_MOVE_A2B_BLOCK_NO_SYNC_CHK_COMMAND + 16) 
#define _DMA_V2_NO_ACK_INIT_A_NO_SYNC_CHK_COMMAND           (_DMA_V2_INIT_A_NO_SYNC_CHK_COMMAND         + 16) 
#define _DMA_V2_NO_ACK_INIT_A_BLOCK_NO_SYNC_CHK_COMMAND     (_DMA_V2_INIT_A_BLOCK_NO_SYNC_CHK_COMMAND   + 16) 
#define _DMA_V2_NO_ACK_INIT_B_NO_SYNC_CHK_COMMAND           (_DMA_V2_INIT_B_NO_SYNC_CHK_COMMAND         + 16) 
#define _DMA_V2_NO_ACK_INIT_B_BLOCK_NO_SYNC_CHK_COMMAND     (_DMA_V2_INIT_B_BLOCK_NO_SYNC_CHK_COMMAND   + 16) 
#define _DMA_V2_CONFIG_CHANNEL_COMMAND                      32   
#define _DMA_V2_SET_CHANNEL_PARAM_COMMAND                   33   
#define _DMA_V2_SET_CRUN_COMMAND                            62   

/* Channel Parameter IDs */
#define _DMA_V2_PACKING_SETUP_PARAM                     0  
#define _DMA_V2_STRIDE_A_PARAM                          1  
#define _DMA_V2_ELEM_CROPPING_A_PARAM                   2  
#define _DMA_V2_WIDTH_A_PARAM                           3  
#define _DMA_V2_STRIDE_B_PARAM                          4  
#define _DMA_V2_ELEM_CROPPING_B_PARAM                   5  
#define _DMA_V2_WIDTH_B_PARAM                           6  
#define _DMA_V2_HEIGHT_PARAM                            7  
#define _DMA_V2_QUEUED_CMDS                             8  

/* Parameter Constants */
#define _DMA_V2_ZERO_EXTEND                             0
#define _DMA_V2_SIGN_EXTEND                             1

  /* SLAVE address map */
#define _DMA_V2_SEL_FSM_CMD                             0
#define _DMA_V2_SEL_CH_REG                              1
#define _DMA_V2_SEL_CONN_GROUP                          2
#define _DMA_V2_SEL_DEV_INTERF                          3

#define _DMA_V2_ADDR_SEL_COMP_IDX                      12
#define _DMA_V2_ADDR_SEL_COMP_BITS                      4
#define _DMA_V2_ADDR_SEL_CH_REG_IDX                     2
#define _DMA_V2_ADDR_SEL_CH_REG_BITS                    6
#define _DMA_V2_ADDR_SEL_PARAM_IDX                      (_DMA_V2_ADDR_SEL_CH_REG_BITS+_DMA_V2_ADDR_SEL_CH_REG_IDX)
#define _DMA_V2_ADDR_SEL_PARAM_BITS                     4

#define _DMA_V2_ADDR_SEL_GROUP_COMP_IDX                 2
#define _DMA_V2_ADDR_SEL_GROUP_COMP_BITS                6
#define _DMA_V2_ADDR_SEL_GROUP_COMP_INFO_IDX            (_DMA_V2_ADDR_SEL_GROUP_COMP_BITS + _DMA_V2_ADDR_SEL_GROUP_COMP_IDX)
#define _DMA_V2_ADDR_SEL_GROUP_COMP_INFO_BITS           4

#define _DMA_V2_ADDR_SEL_DEV_INTERF_IDX_IDX             2
#define _DMA_V2_ADDR_SEL_DEV_INTERF_IDX_BITS            6
#define _DMA_V2_ADDR_SEL_DEV_INTERF_INFO_IDX            (_DMA_V2_ADDR_SEL_DEV_INTERF_IDX_IDX+_DMA_V2_ADDR_SEL_DEV_INTERF_IDX_BITS)
#define _DMA_V2_ADDR_SEL_DEV_INTERF_INFO_BITS           4

#define _DMA_V2_FSM_GROUP_CMD_IDX                       0
#define _DMA_V2_FSM_GROUP_ADDR_SRC_IDX                  1
#define _DMA_V2_FSM_GROUP_ADDR_DEST_IDX                 2
#define _DMA_V2_FSM_GROUP_CMD_CTRL_IDX                  3
#define _DMA_V2_FSM_GROUP_FSM_CTRL_IDX                  4
#define _DMA_V2_FSM_GROUP_FSM_PACK_IDX                  5
#define _DMA_V2_FSM_GROUP_FSM_REQ_IDX                   6
#define _DMA_V2_FSM_GROUP_FSM_WR_IDX                    7
  
#define _DMA_V2_FSM_GROUP_FSM_CTRL_STATE_IDX            0
#define _DMA_V2_FSM_GROUP_FSM_CTRL_REQ_DEV_IDX          1
#define _DMA_V2_FSM_GROUP_FSM_CTRL_REQ_ADDR_IDX         2
#define _DMA_V2_FSM_GROUP_FSM_CTRL_REQ_STRIDE_IDX       3
#define _DMA_V2_FSM_GROUP_FSM_CTRL_REQ_XB_IDX           4
#define _DMA_V2_FSM_GROUP_FSM_CTRL_REQ_YB_IDX           5
#define _DMA_V2_FSM_GROUP_FSM_CTRL_PACK_REQ_DEV_IDX     6
#define _DMA_V2_FSM_GROUP_FSM_CTRL_PACK_WR_DEV_IDX      7
#define _DMA_V2_FSM_GROUP_FSM_CTRL_WR_ADDR_IDX          8
#define _DMA_V2_FSM_GROUP_FSM_CTRL_WR_STRIDE_IDX        9
#define _DMA_V2_FSM_GROUP_FSM_CTRL_PACK_REQ_XB_IDX     10
#define _DMA_V2_FSM_GROUP_FSM_CTRL_PACK_WR_YB_IDX      11
#define _DMA_V2_FSM_GROUP_FSM_CTRL_PACK_WR_XB_IDX      12
#define _DMA_V2_FSM_GROUP_FSM_CTRL_PACK_ELEM_REQ_IDX   13
#define _DMA_V2_FSM_GROUP_FSM_CTRL_PACK_ELEM_WR_IDX    14
#define _DMA_V2_FSM_GROUP_FSM_CTRL_PACK_S_Z_IDX        15
#define _DMA_V2_FSM_GROUP_FSM_CTRL_CMD_CTRL_IDX        15

#define _DMA_V2_FSM_GROUP_FSM_PACK_STATE_IDX            0
#define _DMA_V2_FSM_GROUP_FSM_PACK_CNT_YB_IDX           1
#define _DMA_V2_FSM_GROUP_FSM_PACK_CNT_XB_REQ_IDX       2
#define _DMA_V2_FSM_GROUP_FSM_PACK_CNT_XB_WR_IDX        3

#define _DMA_V2_FSM_GROUP_FSM_REQ_STATE_IDX             0
#define _DMA_V2_FSM_GROUP_FSM_REQ_CNT_YB_IDX            1
#define _DMA_V2_FSM_GROUP_FSM_REQ_CNT_XB_IDX            2
#define _DMA_V2_FSM_GROUP_FSM_REQ_XB_REMAINING_IDX      3
#define _DMA_V2_FSM_GROUP_FSM_REQ_CNT_BURST_IDX         4

#define _DMA_V2_FSM_GROUP_FSM_WR_STATE_IDX              0
#define _DMA_V2_FSM_GROUP_FSM_WR_CNT_YB_IDX             1
#define _DMA_V2_FSM_GROUP_FSM_WR_CNT_XB_IDX             2
#define _DMA_V2_FSM_GROUP_FSM_WR_XB_REMAINING_IDX       3
#define _DMA_V2_FSM_GROUP_FSM_WR_CNT_BURST_IDX          4

#define _DMA_V2_DEV_INTERF_REQ_SIDE_STATUS_IDX          0
#define _DMA_V2_DEV_INTERF_SEND_SIDE_STATUS_IDX         1
#define _DMA_V2_DEV_INTERF_FIFO_STATUS_IDX              2
#define _DMA_V2_DEV_INTERF_REQ_ONLY_COMPLETE_BURST_IDX  3
#define _DMA_V2_DEV_INTERF_MAX_BURST_IDX                4
#define _DMA_V2_DEV_INTERF_CHK_ADDR_ALIGN               5

#endif /* _dma_v2_defs_h */

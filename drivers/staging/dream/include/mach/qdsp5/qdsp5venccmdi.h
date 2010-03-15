#ifndef QDSP5VIDENCCMDI_H
#define QDSP5VIDENCCMDI_H

/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

    V I D E O  E N C O D E R  I N T E R N A L  C O M M A N D S

GENERAL DESCRIPTION
  This file contains defintions of format blocks of commands
  that are accepted by VIDENC Task

REFERENCES
  None

EXTERNALIZED FUNCTIONS
  None

Copyright(c) 2008 by QUALCOMM, Incorporated.
*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
/*===========================================================================

			EDIT HISTORY FOR FILE

This section contains comments describing changes made to this file.
Notice that changes are listed in reverse chronological order.

Revision History:

when       who     what, where, why
--------   ---     ----------------------------------------------------------
09/25/08   umeshp      initial version
===========================================================================*/

  #define VIDENC_CMD_CFG           0x0000
  #define VIDENC_CMD_ACTIVE        0x0001
  #define VIDENC_CMD_IDLE          0x0002
  #define VIDENC_CMD_FRAME_START   0x0003
  #define VIDENC_CMD_STATUS_QUERY  0x0004
  #define VIDENC_CMD_RC_CFG        0x0005
  #define VIDENC_CMD_DIS_CFG       0x0006
  #define VIDENC_CMD_DIS           0x0007
  #define VIDENC_CMD_INTRA_REFRESH 0x0008
  #define VIDENC_CMD_DIGITAL_ZOOM  0x0009


/*
 * Command to pass the frame message information to VIDENC
 */


#define VIDENC_CMD_FRAME_START_LEN \
	sizeof(videnc_cmd_frame_start)

typedef struct {
	unsigned short  cmd_id;
	unsigned short  frame_info;
	unsigned short  frame_rho_budget_word_high;
	unsigned short  frame_rho_budget_word_low;
	unsigned short  input_luma_addr_high;
	unsigned short  input_luma_addr_low;
	unsigned short  input_chroma_addr_high;
	unsigned short  input_chroma_addr_low;
	unsigned short  ref_vop_buf_ptr_high;
	unsigned short  ref_vop_buf_ptr_low;
	unsigned short  enc_pkt_buf_ptr_high;
	unsigned short  enc_pkt_buf_ptr_low;
	unsigned short  enc_pkt_buf_size_high;
	unsigned short  enc_pkt_buf_size_low;
	unsigned short  unfilt_recon_vop_buf_ptr_high;
	unsigned short  unfilt_recon_vop_buf_ptr_low;
	unsigned short  filt_recon_vop_buf_ptr_high;
	unsigned short  filt_recon_vop_buf_ptr_low;
} __attribute__((packed)) videnc_cmd_frame_start;

/*
 * Command to pass the frame-level digital stabilization parameters to VIDENC
 */


#define VIDENC_CMD_DIS_LEN \
    sizeof(videnc_cmd_dis)

typedef struct {
	unsigned short  cmd_id;
	unsigned short  vfe_out_prev_luma_addr_high;
	unsigned short  vfe_out_prev_luma_addr_low;
	unsigned short  stabilization_info;
} __attribute__((packed)) videnc_cmd_dis;

/*
 * Command to pass the codec related parameters to VIDENC
 */


#define VIDENC_CMD_CFG_LEN \
    sizeof(videnc_cmd_cfg)

typedef struct {
	unsigned short  cmd_id;
	unsigned short  cfg_info_0;
	unsigned short  cfg_info_1;
	unsigned short  four_mv_threshold;
	unsigned short  ise_fse_mv_cost_fac;
	unsigned short  venc_frame_dim;
	unsigned short  venc_DM_partition;
} __attribute__((packed)) videnc_cmd_cfg;

/*
 * Command to start the video encoding
 */


#define VIDENC_CMD_ACTIVE_LEN \
    sizeof(videnc_cmd_active)

typedef struct {
    unsigned short  cmd_id;
} __attribute__((packed)) videnc_cmd_active;

/*
 * Command to stop the video encoding
 */


#define VIDENC_CMD_IDLE_LEN \
    sizeof(videnc_cmd_idle)

typedef struct {
	unsigned short  cmd_id;
} __attribute__((packed)) videnc_cmd_idle;

/*
 * Command to query staus of VIDENC
 */


#define VIDENC_CMD_STATUS_QUERY_LEN \
    sizeof(videnc_cmd_status_query)

typedef struct {
	unsigned short  cmd_id;
} __attribute__((packed)) videnc_cmd_status_query;

/*
 * Command to set rate control for a frame
 */


#define VIDENC_CMD_RC_CFG_LEN \
    sizeof(videnc_cmd_rc_cfg)

typedef struct {
	unsigned short  cmd_id;
	unsigned short  max_frame_qp_delta;
	unsigned short  max_min_frame_qp;
} __attribute__((packed)) videnc_cmd_rc_cfg;

/*
 * Command to set intra-refreshing
 */


#define VIDENC_CMD_INTRA_REFRESH_LEN \
    sizeof(videnc_cmd_intra_refresh)

typedef struct {
	unsigned short  cmd_id;
	unsigned short  num_mb_refresh;
	unsigned short  mb_index[15];
} __attribute__((packed)) videnc_cmd_intra_refresh;

/*
 * Command to pass digital zoom information to the VIDENC
 */
#define VIDENC_CMD_DIGITAL_ZOOM_LEN \
    sizeof(videnc_cmd_digital_zoom)

typedef struct {
	unsigned short  cmd_id;
	unsigned short  digital_zoom_en;
	unsigned short  luma_frame_shift_X;
	unsigned short  luma_frame_shift_Y;
	unsigned short  up_ip_luma_rows;
	unsigned short  up_ip_luma_cols;
	unsigned short  up_ip_chroma_rows;
	unsigned short  up_ip_chroma_cols;
	unsigned short  luma_ph_incr_V_low;
	unsigned short  luma_ph_incr_V_high;
	unsigned short  luma_ph_incr_H_low;
	unsigned short  luma_ph_incr_H_high;
	unsigned short  chroma_ph_incr_V_low;
	unsigned short  chroma_ph_incr_V_high;
	unsigned short  chroma_ph_incr_H_low;
	unsigned short  chroma_ph_incr_H_high;
} __attribute__((packed)) videnc_cmd_digital_zoom;

/*
 * Command to configure digital stabilization parameters
 */

#define VIDENC_CMD_DIS_CFG_LEN \
    sizeof(videnc_cmd_dis_cfg)

typedef struct {
	unsigned short  cmd_id;
	unsigned short  image_stab_subf_start_row_col;
	unsigned short  image_stab_subf_dim;
	unsigned short  image_stab_info_0;
} __attribute__((packed)) videnc_cmd_dis_cfg;


#endif

/* SPDX-License-Identifier: GPL-2.0 */

#ifndef RKVDEC_REGS_H_
#define RKVDEC_REGS_H_

#include <linux/types.h>

/*
 * REG_INTERRUPT is accessed via writel to enable the decoder after
 * configuring it and clear interrupt strmd_error_status
 */
#define RKVDEC_REG_INTERRUPT				0x004
#define RKVDEC_INTERRUPT_DEC_E				BIT(0)
#define RKVDEC_CONFIG_DEC_CLK_GATE_E			BIT(1)
#define RKVDEC_E_STRMD_CLKGATE_DIS			BIT(2)
#define RKVDEC_TIMEOUT_MODE				BIT(3)
#define RKVDEC_IRQ_DIS					BIT(4)
#define RKVDEC_TIMEOUT_E				BIT(5)
#define RKVDEC_BUF_EMPTY_E				BIT(6)
#define RKVDEC_STRM_E_WAITDECFIFO_EMPTY			BIT(7)
#define RKVDEC_IRQ					BIT(8)
#define RKVDEC_IRQ_RAW					BIT(9)
#define RKVDEC_E_REWRITE_VALID				BIT(10)
#define RKVDEC_COMMONIRQ_MODE				BIT(11)
#define RKVDEC_RDY_STA					BIT(12)
#define RKVDEC_BUS_STA					BIT(13)
#define RKVDEC_ERR_STA					BIT(14)
#define RKVDEC_TIMEOUT_STA				BIT(15)
#define RKVDEC_BUF_EMPTY_STA				BIT(16)
#define RKVDEC_COLMV_REF_ERR_STA			BIT(17)
#define RKVDEC_CABU_END_STA				BIT(18)
#define RKVDEC_H264ORVP9_ERR_MODE			BIT(19)
#define RKVDEC_SOFTRST_EN_P				BIT(20)
#define RKVDEC_FORCE_SOFTRESET_VALID			BIT(21)
#define RKVDEC_SOFTRESET_RDY				BIT(22)
#define RKVDEC_WR_DDR_ALIGN_EN				BIT(23)

#define RKVDEC_REG_QOS_CTRL				0x18C

/*
 * Cache configuration is not covered in the range of the register struct
 */
#define RKVDEC_REG_PREF_LUMA_CACHE_COMMAND		0x410
#define RKVDEC_REG_PREF_CHR_CACHE_COMMAND		0x450

/*
 * Define the mode values
 */
#define RKVDEC_MODE_HEVC				0
#define RKVDEC_MODE_H264				1
#define RKVDEC_MODE_VP9					2

/* rkvcodec registers */
struct rkvdec_common_regs {
	struct rkvdec_id {
		u32 minor_ver	: 8;
		u32 level	: 1;
		u32 dec_support	: 3;
		u32 profile	: 1;
		u32 reserved0	: 1;
		u32 codec_flag	: 1;
		u32 reserved1	: 1;
		u32 prod_num	: 16;
	} reg00;

	struct rkvdec_int {
		u32 dec_e			: 1;
		u32 dec_clkgate_e		: 1;
		u32 dec_e_strmd_clkgate_dis	: 1;
		u32 timeout_mode		: 1;
		u32 dec_irq_dis			: 1;
		u32 dec_timeout_e		: 1;
		u32 buf_empty_en		: 1;
		u32 stmerror_waitdecfifo_empty	: 1;
		u32 dec_irq			: 1;
		u32 dec_irq_raw			: 1;
		u32 reserved2			: 2;
		u32 dec_rdy_sta			: 1;
		u32 dec_bus_sta			: 1;
		u32 dec_error_sta		: 1;
		u32 dec_timeout_sta		: 1;
		u32 dec_empty_sta		: 1;
		u32 colmv_ref_error_sta		: 1;
		u32 cabu_end_sta		: 1;
		u32 h264orvp9_error_mode	: 1;
		u32 softrst_en_p		: 1;
		u32 force_softreset_valid	: 1;
		u32 softreset_rdy		: 1;
		u32 wr_ddr_align_en		: 1;
		u32 scl_down_en			: 1;
		u32 allow_not_wr_unref_bframe	: 1;
		u32 reserved1			: 6;
	} reg01;

	struct rkvdec_sysctrl {
		u32 in_endian			: 1;
		u32 in_swap32_e			: 1;
		u32 in_swap64_e			: 1;
		u32 str_endian			: 1;
		u32 str_swap32_e		: 1;
		u32 str_swap64_e		: 1;
		u32 out_endian			: 1;
		u32 out_swap32_e		: 1;
		u32 out_cbcr_swap		: 1;
		u32 reserved0			: 1;
		u32 rlc_mode_direct_write	: 1;
		u32 rlc_mode			: 1;
		u32 strm_start_bit		: 7;
		u32 reserved1			: 1;
		u32 dec_mode			: 2;
		u32 reserved2			: 2;
		u32 rps_mode			: 1;
		u32 stream_mode			: 1;
		u32 stream_lastpacket		: 1;
		u32 firstslice_flag		: 1;
		u32 frame_orslice		: 1;
		u32 buspr_slot_disable		: 1;
		u32 colmv_mode			: 1;
		u32 ycacherd_prior		: 1;
	} reg02;

	struct rkvdec_picpar {
		u32 y_hor_virstride	: 9;
		u32 reserved		: 2;
		u32 slice_num_highbit	: 1;
		u32 uv_hor_virstride	: 9;
		u32 slice_num_lowbits	: 11;
	} reg03;

	u32 strm_rlc_base;
	u32 stream_len;
	u32 cabactbl_base;
	u32 decout_base;

	struct rkvdec_y_virstride {
		u32 y_virstride	: 20;
		u32 reserved0	: 12;
	} reg08;

	struct rkvdec_yuv_virstride {
		u32 yuv_virstride	: 21;
		u32 reserved0		: 11;
	} reg09;
} __packed;

struct ref_base {
	u32 field_ref		: 1;
	u32 topfield_used_ref	: 1;
	u32 botfield_used_ref	: 1;
	u32 colmv_use_flag_ref	: 1;
	u32 base_addr		: 28;
};

struct rkvdec_h26x_regs {
	struct ref_base ref0_14_base[15];
	u32 ref0_14_poc[15];

	u32 cur_poc;
	u32 rlcwrite_base;
	u32 pps_base;
	u32 rps_base;

	u32 strmd_error_e;

	struct {
		u32 strmd_error_status		: 28;
		u32 colmv_error_ref_picidx	: 4;
	} reg45;

	struct {
		u32 strmd_error_ctu_xoffset	: 8;
		u32 strmd_error_ctu_yoffset	: 8;
		u32 streamfifo_space2full	: 7;
		u32 reserved0			: 1;
		u32 vp9_error_ctu0_en		: 1;
		u32 reserved1			: 7;
	} reg46;

	struct {
		u32 saowr_xoffet	: 9;
		u32 reserved0		: 7;
		u32 saowr_yoffset	: 10;
		u32 reserved1		: 6;
	} reg47;

	struct ref_base ref15_base;

	u32 ref15_29_poc[15];

	u32 performance_cycle;
	u32 axi_ddr_rdata;
	u32 axi_ddr_wdata;

	struct {
		u32 busifd_resetn	: 1;
		u32 cabac_resetn	: 1;
		u32 dec_ctrl_resetn	: 1;
		u32 transd_resetn	: 1;
		u32 intra_resetn	: 1;
		u32 inter_resetn	: 1;
		u32 recon_resetn	: 1;
		u32 filer_resetn	: 1;
		u32 reserved0		: 24;
	} reg67;

	struct {
		u32 perf_cnt0_sel	: 6;
		u32 reserved0		: 2;
		u32 perf_cnt1_sel	: 6;
		u32 reserved1		: 2;
		u32 perf_cnt2_sel	: 6;
		u32 reserved2		: 10;
	} reg68;

	u32 perf_cnt0;
	u32 perf_cnt1;
	u32 perf_cnt2;
	u32 ref30_poc;
	u32 ref31_poc;
	u32 cur_poc1;
	u32 errorinfo_base;

	struct {
		u32 slicedec_num		: 14;
		u32 reserved0			: 1;
		u32 strmd_detect_error_flag	: 1;
		u32 error_packet_num		: 14;
		u32 reserved1			: 2;
	} reg76;

	struct {
		u32 error_en_highbits		: 30;
		u32 strmd_error_slice_en	: 1;
		u32 strmd_error_frame_en	: 1;
	} reg77;

	u32 colmv_cur_base;
	u32 colmv_ref_base[16];
	u32 scanlist_addr;
	u32 reg96_sd_decout_base;
	u32 sd_y_virstride;
	u32 sd_hor_stride;
	u32 qos_ctrl;
	u32 perf[8];
	u32 qos1;
} __packed;

struct rkvdec_vp9_regs {
	struct cprheader_offset {
		u32 cprheader_offset	: 16;
		u32 reserved		: 16;
	} reg10;

	u32 refer_bases[3];
	u32 count_base;
	u32 segidlast_base;
	u32 segidcur_base;

	struct frame_sizes {
		u32 framewidth		: 16;
		u32 frameheight		: 16;
	} reg17_19[3];

	struct segid_grp {
		u32 segid_abs_delta			: 1;
		u32 segid_frame_qp_delta_en		: 1;
		u32 segid_frame_qp_delta		: 9;
		u32 segid_frame_loopfilter_value_en	: 1;
		u32 segid_frame_loopfilter_value	: 7;
		u32 segid_referinfo_en			: 1;
		u32 segid_referinfo			: 2;
		u32 segid_frame_skip_en			: 1;
		u32 reserved				: 9;
	} reg20_27[8];

	struct cprheader_config {
		u32 tx_mode			: 3;
		u32 frame_reference_mode	: 2;
		u32 reserved			: 27;
	} reg28;

	struct ref_scale {
		u32 ref_hor_scale		: 16;
		u32 ref_ver_scale		: 16;
	} reg29_31[3];

	struct ref_deltas_lastframe {
		u32 ref_deltas_lastframe0	: 7;
		u32 ref_deltas_lastframe1	: 7;
		u32 ref_deltas_lastframe2	: 7;
		u32 ref_deltas_lastframe3	: 7;
		u32 reserved			: 4;
	} reg32;

	struct info_lastframe {
		u32 mode_deltas_lastframe0		: 7;
		u32 mode_deltas_lastframe1		: 7;
		u32 reserved0				: 2;
		u32 segmentation_enable_lstframe	: 1;
		u32 last_show_frame			: 1;
		u32 last_intra_only			: 1;
		u32 last_widthheight_eqcur		: 1;
		u32 color_space_lastkeyframe		: 3;
		u32 reserved1				: 9;
	} reg33;

	u32 intercmd_base;

	struct intercmd_num {
		u32 intercmd_num	: 24;
		u32 reserved		: 8;
	} reg35;

	struct lasttile_size {
		u32 lasttile_size	: 24;
		u32 reserved		: 8;
	} reg36;

	struct hor_virstride {
		u32 y_hor_virstride	: 9;
		u32 reserved0		: 7;
		u32 uv_hor_virstride	: 9;
		u32 reserved1		: 7;
	} reg37_39[3];

	u32 cur_poc;

	struct rlcwrite_base {
		u32 reserved		: 3;
		u32 rlcwrite_base	: 29;
	} reg41;

	struct pps_base {
		u32 reserved	: 4;
		u32 pps_base	: 28;
	} reg42;

	struct rps_base {
		u32 reserved	: 4;
		u32 rps_base	: 28;
	} reg43;

	struct strmd_error_en {
		u32 strmd_error_e	: 28;
		u32 reserved		: 4;
	} reg44;

	u32 vp9_error_info0;

	struct strmd_error_ctu {
		u32 strmd_error_ctu_xoffset	: 8;
		u32 strmd_error_ctu_yoffset	: 8;
		u32 streamfifo_space2full	: 7;
		u32 reserved0			: 1;
		u32 error_ctu0_en		: 1;
		u32 reserved1			: 7;
	} reg46;

	struct sao_ctu_position {
		u32 saowr_xoffet	: 9;
		u32 reserved0		: 7;
		u32 saowr_yoffset	: 10;
		u32 reserved1		: 6;
	} reg47;

	struct ystride {
		u32 virstride	: 20;
		u32 reserved	: 12;
	} reg48_50[3];

	struct lastref_yuvstride {
		u32 lastref_yuv_virstride	: 21;
		u32 reserved			: 11;
	} reg51;

	u32 refcolmv_base;

	u32 reserved0[11];

	u32 performance_cycle;
	u32 axi_ddr_rdata;
	u32 axi_ddr_wdata;

	struct fpgadebug_reset {
		u32 busifd_resetn	: 1;
		u32 cabac_resetn	: 1;
		u32 dec_ctrl_resetn	: 1;
		u32 transd_resetn	: 1;
		u32 intra_resetn	: 1;
		u32 inter_resetn	: 1;
		u32 recon_resetn	: 1;
		u32 filer_resetn	: 1;
		u32 reserved		: 24;
	} reg67;

	struct performance_sel {
		u32 perf_cnt0_sel	: 6;
		u32 reserved0		: 2;
		u32 perf_cnt1_sel	: 6;
		u32 reserved1		: 2;
		u32 perf_cnt2_sel	: 6;
		u32 reserved		: 10;
	} reg68;

	u32 perf_cnt0;
	u32 perf_cnt1;
	u32 perf_cnt2;

	u32 reserved1[3];

	u32 vp9_error_info1;

	struct error_ctu1 {
		u32 vp9_error_ctu1_x	: 6;
		u32 reserved0		: 2;
		u32 vp9_error_ctu1_y	: 6;
		u32 reserved1		: 1;
		u32 vp9_error_ctu1_en	: 1;
		u32 reserved2		: 16;
	} reg76;

	u32 reserved2;
} __packed;

struct rkvdec_regs {
	struct rkvdec_common_regs common;
	union {
		struct rkvdec_h26x_regs h26x;
		struct rkvdec_vp9_regs  vp9;
	};
} __packed;

#endif /* RKVDEC_REGS_H_ */

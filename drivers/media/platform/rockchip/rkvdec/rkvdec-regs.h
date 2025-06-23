/* SPDX-License-Identifier: GPL-2.0 */

#ifndef RKVDEC_REGS_H_
#define RKVDEC_REGS_H_

/* rkvcodec registers */
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

#define RKVDEC_REG_SYSCTRL				0x008
#define RKVDEC_IN_ENDIAN				BIT(0)
#define RKVDEC_IN_SWAP32_E				BIT(1)
#define RKVDEC_IN_SWAP64_E				BIT(2)
#define RKVDEC_STR_ENDIAN				BIT(3)
#define RKVDEC_STR_SWAP32_E				BIT(4)
#define RKVDEC_STR_SWAP64_E				BIT(5)
#define RKVDEC_OUT_ENDIAN				BIT(6)
#define RKVDEC_OUT_SWAP32_E				BIT(7)
#define RKVDEC_OUT_CBCR_SWAP				BIT(8)
#define RKVDEC_RLC_MODE_DIRECT_WRITE			BIT(10)
#define RKVDEC_RLC_MODE					BIT(11)
#define RKVDEC_STRM_START_BIT(x)			(((x) & 0x7f) << 12)
#define RKVDEC_MODE(x)					(((x) & 0x03) << 20)
#define RKVDEC_MODE_H264				1
#define RKVDEC_MODE_VP9					2
#define RKVDEC_RPS_MODE					BIT(24)
#define RKVDEC_STRM_MODE				BIT(25)
#define RKVDEC_H264_STRM_LASTPKT			BIT(26)
#define RKVDEC_H264_FIRSTSLICE_FLAG			BIT(27)
#define RKVDEC_H264_FRAME_ORSLICE			BIT(28)
#define RKVDEC_BUSPR_SLOT_DIS				BIT(29)

#define RKVDEC_REG_PICPAR				0x00C
#define RKVDEC_Y_HOR_VIRSTRIDE(x)			((x) & 0x1ff)
#define RKVDEC_SLICE_NUM_HIGHBIT			BIT(11)
#define RKVDEC_UV_HOR_VIRSTRIDE(x)			(((x) & 0x1ff) << 12)
#define RKVDEC_SLICE_NUM_LOWBITS(x)			(((x) & 0x7ff) << 21)

#define RKVDEC_REG_STRM_RLC_BASE			0x010

#define RKVDEC_REG_STRM_LEN				0x014
#define RKVDEC_STRM_LEN(x)				((x) & 0x7ffffff)

#define RKVDEC_REG_CABACTBL_PROB_BASE			0x018
#define RKVDEC_REG_DECOUT_BASE				0x01C

#define RKVDEC_REG_Y_VIRSTRIDE				0x020
#define RKVDEC_Y_VIRSTRIDE(x)				((x) & 0xfffff)

#define RKVDEC_REG_YUV_VIRSTRIDE			0x024
#define RKVDEC_YUV_VIRSTRIDE(x)				((x) & 0x1fffff)
#define RKVDEC_REG_H264_BASE_REFER(i)			(((i) * 0x04) + 0x028)

#define RKVDEC_REG_H264_BASE_REFER15			0x0C0
#define RKVDEC_FIELD_REF				BIT(0)
#define RKVDEC_TOPFIELD_USED_REF			BIT(1)
#define RKVDEC_BOTFIELD_USED_REF			BIT(2)
#define RKVDEC_COLMV_USED_FLAG_REF			BIT(3)

#define RKVDEC_REG_VP9_LAST_FRAME_BASE			0x02c
#define RKVDEC_REG_VP9_GOLDEN_FRAME_BASE		0x030
#define RKVDEC_REG_VP9_ALTREF_FRAME_BASE		0x034

#define RKVDEC_REG_VP9_CPRHEADER_OFFSET			0x028
#define RKVDEC_VP9_CPRHEADER_OFFSET(x)			((x) & 0xffff)

#define RKVDEC_REG_VP9_REFERLAST_BASE			0x02C
#define RKVDEC_REG_VP9_REFERGOLDEN_BASE			0x030
#define RKVDEC_REG_VP9_REFERALFTER_BASE			0x034

#define RKVDEC_REG_VP9COUNT_BASE			0x038
#define RKVDEC_VP9COUNT_UPDATE_EN			BIT(0)

#define RKVDEC_REG_VP9_SEGIDLAST_BASE			0x03C
#define RKVDEC_REG_VP9_SEGIDCUR_BASE			0x040
#define RKVDEC_REG_VP9_FRAME_SIZE(i)			((i) * 0x04 + 0x044)
#define RKVDEC_VP9_FRAMEWIDTH(x)			(((x) & 0xffff) << 0)
#define RKVDEC_VP9_FRAMEHEIGHT(x)			(((x) & 0xffff) << 16)

#define RKVDEC_VP9_SEGID_GRP(i)				((i) * 0x04 + 0x050)
#define RKVDEC_SEGID_ABS_DELTA(x)			((x) & 0x1)
#define RKVDEC_SEGID_FRAME_QP_DELTA_EN(x)		(((x) & 0x1) << 1)
#define RKVDEC_SEGID_FRAME_QP_DELTA(x)			(((x) & 0x1ff) << 2)
#define RKVDEC_SEGID_FRAME_LOOPFILTER_VALUE_EN(x)	(((x) & 0x1) << 11)
#define RKVDEC_SEGID_FRAME_LOOPFILTER_VALUE(x)		(((x) & 0x7f) << 12)
#define RKVDEC_SEGID_REFERINFO_EN(x)			(((x) & 0x1) << 19)
#define RKVDEC_SEGID_REFERINFO(x)			(((x) & 0x03) << 20)
#define RKVDEC_SEGID_FRAME_SKIP_EN(x)			(((x) & 0x1) << 22)

#define RKVDEC_VP9_CPRHEADER_CONFIG			0x070
#define RKVDEC_VP9_TX_MODE(x)				((x) & 0x07)
#define RKVDEC_VP9_FRAME_REF_MODE(x)			(((x) & 0x03) << 3)

#define RKVDEC_VP9_REF_SCALE(i)				((i) * 0x04 + 0x074)
#define RKVDEC_VP9_REF_HOR_SCALE(x)			((x) & 0xffff)
#define RKVDEC_VP9_REF_VER_SCALE(x)			(((x) & 0xffff) << 16)

#define RKVDEC_VP9_REF_DELTAS_LASTFRAME			0x080
#define RKVDEC_REF_DELTAS_LASTFRAME(pos, val)		(((val) & 0x7f) << ((pos) * 7))

#define RKVDEC_VP9_INFO_LASTFRAME			0x084
#define RKVDEC_MODE_DELTAS_LASTFRAME(pos, val)		(((val) & 0x7f) << ((pos) * 7))
#define RKVDEC_SEG_EN_LASTFRAME				BIT(16)
#define RKVDEC_LAST_SHOW_FRAME				BIT(17)
#define RKVDEC_LAST_INTRA_ONLY				BIT(18)
#define RKVDEC_LAST_WIDHHEIGHT_EQCUR			BIT(19)
#define RKVDEC_COLOR_SPACE_LASTKEYFRAME(x)		(((x) & 0x07) << 20)

#define RKVDEC_VP9_INTERCMD_BASE			0x088

#define RKVDEC_VP9_INTERCMD_NUM				0x08C
#define RKVDEC_INTERCMD_NUM(x)				((x) & 0xffffff)

#define RKVDEC_VP9_LASTTILE_SIZE			0x090
#define RKVDEC_LASTTILE_SIZE(x)				((x) & 0xffffff)

#define RKVDEC_VP9_HOR_VIRSTRIDE(i)			((i) * 0x04 + 0x094)
#define RKVDEC_HOR_Y_VIRSTRIDE(x)			((x) & 0x1ff)
#define RKVDEC_HOR_UV_VIRSTRIDE(x)			(((x) & 0x1ff) << 16)

#define RKVDEC_REG_H264_POC_REFER0(i)			(((i) * 0x04) + 0x064)
#define RKVDEC_REG_H264_POC_REFER1(i)			(((i) * 0x04) + 0x0C4)
#define RKVDEC_REG_H264_POC_REFER2(i)			(((i) * 0x04) + 0x120)
#define RKVDEC_POC_REFER(x)				((x) & 0xffffffff)

#define RKVDEC_REG_CUR_POC0				0x0A0
#define RKVDEC_REG_CUR_POC1				0x128
#define RKVDEC_CUR_POC(x)				((x) & 0xffffffff)

#define RKVDEC_REG_RLCWRITE_BASE			0x0A4
#define RKVDEC_REG_PPS_BASE				0x0A8
#define RKVDEC_REG_RPS_BASE				0x0AC

#define RKVDEC_REG_STRMD_ERR_EN				0x0B0
#define RKVDEC_STRMD_ERR_EN(x)				((x) & 0xffffffff)

#define RKVDEC_REG_STRMD_ERR_STA			0x0B4
#define RKVDEC_STRMD_ERR_STA(x)				((x) & 0xfffffff)
#define RKVDEC_COLMV_ERR_REF_PICIDX(x)			(((x) & 0x0f) << 28)

#define RKVDEC_REG_STRMD_ERR_CTU			0x0B8
#define RKVDEC_STRMD_ERR_CTU(x)				((x) & 0xff)
#define RKVDEC_STRMD_ERR_CTU_YOFFSET(x)			(((x) & 0xff) << 8)
#define RKVDEC_STRMFIFO_SPACE2FULL(x)			(((x) & 0x7f) << 16)
#define RKVDEC_VP9_ERR_EN_CTU0				BIT(24)

#define RKVDEC_REG_SAO_CTU_POS				0x0BC
#define RKVDEC_SAOWR_XOFFSET(x)				((x) & 0x1ff)
#define RKVDEC_SAOWR_YOFFSET(x)				(((x) & 0x3ff) << 16)

#define RKVDEC_VP9_LAST_FRAME_YSTRIDE			0x0C0
#define RKVDEC_VP9_GOLDEN_FRAME_YSTRIDE			0x0C4
#define RKVDEC_VP9_ALTREF_FRAME_YSTRIDE			0x0C8
#define RKVDEC_VP9_REF_YSTRIDE(x)			(((x) & 0xfffff) << 0)

#define RKVDEC_VP9_LAST_FRAME_YUVSTRIDE			0x0CC
#define RKVDEC_VP9_REF_YUVSTRIDE(x)			(((x) & 0x1fffff) << 0)

#define RKVDEC_VP9_REF_COLMV_BASE			0x0D0

#define RKVDEC_REG_PERFORMANCE_CYCLE			0x100
#define RKVDEC_PERFORMANCE_CYCLE(x)			((x) & 0xffffffff)

#define RKVDEC_REG_AXI_DDR_RDATA			0x104
#define RKVDEC_AXI_DDR_RDATA(x)				((x) & 0xffffffff)

#define RKVDEC_REG_AXI_DDR_WDATA			0x108
#define RKVDEC_AXI_DDR_WDATA(x)				((x) & 0xffffffff)

#define RKVDEC_REG_FPGADEBUG_RESET			0x10C
#define RKVDEC_BUSIFD_RESETN				BIT(0)
#define RKVDEC_CABAC_RESETN				BIT(1)
#define RKVDEC_DEC_CTRL_RESETN				BIT(2)
#define RKVDEC_TRANSD_RESETN				BIT(3)
#define RKVDEC_INTRA_RESETN				BIT(4)
#define RKVDEC_INTER_RESETN				BIT(5)
#define RKVDEC_RECON_RESETN				BIT(6)
#define RKVDEC_FILER_RESETN				BIT(7)

#define RKVDEC_REG_PERFORMANCE_SEL			0x110
#define RKVDEC_PERF_SEL_CNT0(x)				((x) & 0x3f)
#define RKVDEC_PERF_SEL_CNT1(x)				(((x) & 0x3f) << 8)
#define RKVDEC_PERF_SEL_CNT2(x)				(((x) & 0x3f) << 16)

#define RKVDEC_REG_PERFORMANCE_CNT(i)			((i) * 0x04 + 0x114)
#define RKVDEC_PERF_CNT(x)				((x) & 0xffffffff)

#define RKVDEC_REG_H264_ERRINFO_BASE			0x12C

#define RKVDEC_REG_H264_ERRINFO_NUM			0x130
#define RKVDEC_SLICEDEC_NUM(x)				((x) & 0x3fff)
#define RKVDEC_STRMD_DECT_ERR_FLAG			BIT(15)
#define RKVDEC_ERR_PKT_NUM(x)				(((x) & 0x3fff) << 16)

#define RKVDEC_REG_H264_ERR_E				0x134
#define RKVDEC_H264_ERR_EN_HIGHBITS(x)			((x) & 0x3fffffff)

#define RKVDEC_REG_PREF_LUMA_CACHE_COMMAND		0x410
#define RKVDEC_REG_PREF_CHR_CACHE_COMMAND		0x450

#endif /* RKVDEC_REGS_H_ */

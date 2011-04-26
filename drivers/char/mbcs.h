/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2005 Silicon Graphics, Inc.  All rights reserved.
 */

#ifndef __MBCS_H__
#define __MBCS_H__

/*
 * General macros
 */
#define MB	(1024*1024)
#define MB2	(2*MB)
#define MB4	(4*MB)
#define MB6	(6*MB)

/*
 * Offsets and masks
 */
#define MBCS_CM_ID		0x0000	/* Identification */
#define MBCS_CM_STATUS		0x0008	/* Status */
#define MBCS_CM_ERROR_DETAIL1	0x0010	/* Error Detail1 */
#define MBCS_CM_ERROR_DETAIL2	0x0018	/* Error Detail2 */
#define MBCS_CM_CONTROL		0x0020	/* Control */
#define MBCS_CM_REQ_TOUT	0x0028	/* Request Time-out */
#define MBCS_CM_ERR_INT_DEST	0x0038	/* Error Interrupt Destination */
#define MBCS_CM_TARG_FL		0x0050	/* Target Flush */
#define MBCS_CM_ERR_STAT	0x0060	/* Error Status */
#define MBCS_CM_CLR_ERR_STAT	0x0068	/* Clear Error Status */
#define MBCS_CM_ERR_INT_EN	0x0070	/* Error Interrupt Enable */
#define MBCS_RD_DMA_SYS_ADDR	0x0100	/* Read DMA System Address */
#define MBCS_RD_DMA_LOC_ADDR	0x0108	/* Read DMA Local Address */
#define MBCS_RD_DMA_CTRL	0x0110	/* Read DMA Control */
#define MBCS_RD_DMA_AMO_DEST	0x0118	/* Read DMA AMO Destination */
#define MBCS_RD_DMA_INT_DEST	0x0120	/* Read DMA Interrupt Destination */
#define MBCS_RD_DMA_AUX_STAT	0x0130	/* Read DMA Auxiliary Status */
#define MBCS_WR_DMA_SYS_ADDR	0x0200	/* Write DMA System Address */
#define MBCS_WR_DMA_LOC_ADDR	0x0208	/* Write DMA Local Address */
#define MBCS_WR_DMA_CTRL	0x0210	/* Write DMA Control */
#define MBCS_WR_DMA_AMO_DEST	0x0218	/* Write DMA AMO Destination */
#define MBCS_WR_DMA_INT_DEST	0x0220	/* Write DMA Interrupt Destination */
#define MBCS_WR_DMA_AUX_STAT	0x0230	/* Write DMA Auxiliary Status */
#define MBCS_ALG_AMO_DEST	0x0300	/* Algorithm AMO Destination */
#define MBCS_ALG_INT_DEST	0x0308	/* Algorithm Interrupt Destination */
#define MBCS_ALG_OFFSETS	0x0310
#define MBCS_ALG_STEP		0x0318	/* Algorithm Step */

#define MBCS_GSCR_START		0x0000000
#define MBCS_DEBUG_START	0x0100000
#define MBCS_RAM0_START		0x0200000
#define MBCS_RAM1_START		0x0400000
#define MBCS_RAM2_START		0x0600000

#define MBCS_CM_CONTROL_REQ_TOUT_MASK 0x0000000000ffffffUL
//#define PIO_BASE_ADDR_BASE_OFFSET_MASK 0x00fffffffff00000UL

#define MBCS_SRAM_SIZE		(1024*1024)
#define MBCS_CACHELINE_SIZE	128

/*
 * MMR get's and put's
 */
#define MBCS_MMR_ADDR(mmr_base, offset)((uint64_t *)(mmr_base + offset))
#define MBCS_MMR_SET(mmr_base, offset, value) {			\
	uint64_t *mbcs_mmr_set_u64p, readback;				\
	mbcs_mmr_set_u64p = (uint64_t *)(mmr_base + offset);	\
	*mbcs_mmr_set_u64p = value;					\
	readback = *mbcs_mmr_set_u64p; \
}
#define MBCS_MMR_GET(mmr_base, offset) *(uint64_t *)(mmr_base + offset)
#define MBCS_MMR_ZERO(mmr_base, offset) MBCS_MMR_SET(mmr_base, offset, 0)

/*
 * MBCS mmr structures
 */
union cm_id {
	uint64_t cm_id_reg;
	struct {
		uint64_t always_one:1,	// 0
		 mfg_id:11,	// 11:1
		 part_num:16,	// 27:12
		 bitstream_rev:8,	// 35:28
		:28;		// 63:36
	};
};

union cm_status {
	uint64_t cm_status_reg;
	struct {
		uint64_t pending_reads:8,	// 7:0
		 pending_writes:8,	// 15:8
		 ice_rsp_credits:8,	// 23:16
		 ice_req_credits:8,	// 31:24
		 cm_req_credits:8,	// 39:32
		:1,		// 40
		 rd_dma_in_progress:1,	// 41
		 rd_dma_done:1,	// 42
		:1,		// 43
		 wr_dma_in_progress:1,	// 44
		 wr_dma_done:1,	// 45
		 alg_waiting:1,	// 46
		 alg_pipe_running:1,	// 47
		 alg_done:1,	// 48
		:3,		// 51:49
		 pending_int_reqs:8,	// 59:52
		:3,		// 62:60
		 alg_half_speed_sel:1;	// 63
	};
};

union cm_error_detail1 {
	uint64_t cm_error_detail1_reg;
	struct {
		uint64_t packet_type:4,	// 3:0
		 source_id:2,	// 5:4
		 data_size:2,	// 7:6
		 tnum:8,	// 15:8
		 byte_enable:8,	// 23:16
		 gfx_cred:8,	// 31:24
		 read_type:2,	// 33:32
		 pio_or_memory:1,	// 34
		 head_cw_error:1,	// 35
		:12,		// 47:36
		 head_error_bit:1,	// 48
		 data_error_bit:1,	// 49
		:13,		// 62:50
		 valid:1;	// 63
	};
};

union cm_error_detail2 {
	uint64_t cm_error_detail2_reg;
	struct {
		uint64_t address:56,	// 55:0
		:8;		// 63:56
	};
};

union cm_control {
	uint64_t cm_control_reg;
	struct {
		uint64_t cm_id:2,	// 1:0
		:2,		// 3:2
		 max_trans:5,	// 8:4
		:3,		// 11:9
		 address_mode:1,	// 12
		:7,		// 19:13
		 credit_limit:8,	// 27:20
		:5,		// 32:28
		 rearm_stat_regs:1,	// 33
		 prescalar_byp:1,	// 34
		 force_gap_war:1,	// 35
		 rd_dma_go:1,	// 36
		 wr_dma_go:1,	// 37
		 alg_go:1,	// 38
		 rd_dma_clr:1,	// 39
		 wr_dma_clr:1,	// 40
		 alg_clr:1,	// 41
		:2,		// 43:42
		 alg_wait_step:1,	// 44
		 alg_done_amo_en:1,	// 45
		 alg_done_int_en:1,	// 46
		:1,		// 47
		 alg_sram0_locked:1,	// 48
		 alg_sram1_locked:1,	// 49
		 alg_sram2_locked:1,	// 50
		 alg_done_clr:1,	// 51
		:12;		// 63:52
	};
};

union cm_req_timeout {
	uint64_t cm_req_timeout_reg;
	struct {
		uint64_t time_out:24,	// 23:0
		:40;		// 63:24
	};
};

union intr_dest {
	uint64_t intr_dest_reg;
	struct {
		uint64_t address:56,	// 55:0
		 int_vector:8;	// 63:56
	};
};

union cm_error_status {
	uint64_t cm_error_status_reg;
	struct {
		uint64_t ecc_sbe:1,	// 0
		 ecc_mbe:1,	// 1
		 unsupported_req:1,	// 2
		 unexpected_rsp:1,	// 3
		 bad_length:1,	// 4
		 bad_datavalid:1,	// 5
		 buffer_overflow:1,	// 6
		 request_timeout:1,	// 7
		:8,		// 15:8
		 head_inv_data_size:1,	// 16
		 rsp_pactype_inv:1,	// 17
		 head_sb_err:1,	// 18
		 missing_head:1,	// 19
		 head_inv_rd_type:1,	// 20
		 head_cmd_err_bit:1,	// 21
		 req_addr_align_inv:1,	// 22
		 pio_req_addr_inv:1,	// 23
		 req_range_dsize_inv:1,	// 24
		 early_term:1,	// 25
		 early_tail:1,	// 26
		 missing_tail:1,	// 27
		 data_flit_sb_err:1,	// 28
		 cm2hcm_req_cred_of:1,	// 29
		 cm2hcm_rsp_cred_of:1,	// 30
		 rx_bad_didn:1,	// 31
		 rd_dma_err_rsp:1,	// 32
		 rd_dma_tnum_tout:1,	// 33
		 rd_dma_multi_tnum_tou:1,	// 34
		 wr_dma_err_rsp:1,	// 35
		 wr_dma_tnum_tout:1,	// 36
		 wr_dma_multi_tnum_tou:1,	// 37
		 alg_data_overflow:1,	// 38
		 alg_data_underflow:1,	// 39
		 ram0_access_conflict:1,	// 40
		 ram1_access_conflict:1,	// 41
		 ram2_access_conflict:1,	// 42
		 ram0_perr:1,	// 43
		 ram1_perr:1,	// 44
		 ram2_perr:1,	// 45
		 int_gen_rsp_err:1,	// 46
		 int_gen_tnum_tout:1,	// 47
		 rd_dma_prog_err:1,	// 48
		 wr_dma_prog_err:1,	// 49
		:14;		// 63:50
	};
};

union cm_clr_error_status {
	uint64_t cm_clr_error_status_reg;
	struct {
		uint64_t clr_ecc_sbe:1,	// 0
		 clr_ecc_mbe:1,	// 1
		 clr_unsupported_req:1,	// 2
		 clr_unexpected_rsp:1,	// 3
		 clr_bad_length:1,	// 4
		 clr_bad_datavalid:1,	// 5
		 clr_buffer_overflow:1,	// 6
		 clr_request_timeout:1,	// 7
		:8,		// 15:8
		 clr_head_inv_data_siz:1,	// 16
		 clr_rsp_pactype_inv:1,	// 17
		 clr_head_sb_err:1,	// 18
		 clr_missing_head:1,	// 19
		 clr_head_inv_rd_type:1,	// 20
		 clr_head_cmd_err_bit:1,	// 21
		 clr_req_addr_align_in:1,	// 22
		 clr_pio_req_addr_inv:1,	// 23
		 clr_req_range_dsize_i:1,	// 24
		 clr_early_term:1,	// 25
		 clr_early_tail:1,	// 26
		 clr_missing_tail:1,	// 27
		 clr_data_flit_sb_err:1,	// 28
		 clr_cm2hcm_req_cred_o:1,	// 29
		 clr_cm2hcm_rsp_cred_o:1,	// 30
		 clr_rx_bad_didn:1,	// 31
		 clr_rd_dma_err_rsp:1,	// 32
		 clr_rd_dma_tnum_tout:1,	// 33
		 clr_rd_dma_multi_tnum:1,	// 34
		 clr_wr_dma_err_rsp:1,	// 35
		 clr_wr_dma_tnum_tout:1,	// 36
		 clr_wr_dma_multi_tnum:1,	// 37
		 clr_alg_data_overflow:1,	// 38
		 clr_alg_data_underflo:1,	// 39
		 clr_ram0_access_confl:1,	// 40
		 clr_ram1_access_confl:1,	// 41
		 clr_ram2_access_confl:1,	// 42
		 clr_ram0_perr:1,	// 43
		 clr_ram1_perr:1,	// 44
		 clr_ram2_perr:1,	// 45
		 clr_int_gen_rsp_err:1,	// 46
		 clr_int_gen_tnum_tout:1,	// 47
		 clr_rd_dma_prog_err:1,	// 48
		 clr_wr_dma_prog_err:1,	// 49
		:14;		// 63:50
	};
};

union cm_error_intr_enable {
	uint64_t cm_error_intr_enable_reg;
	struct {
		uint64_t int_en_ecc_sbe:1,	// 0
		 int_en_ecc_mbe:1,	// 1
		 int_en_unsupported_re:1,	// 2
		 int_en_unexpected_rsp:1,	// 3
		 int_en_bad_length:1,	// 4
		 int_en_bad_datavalid:1,	// 5
		 int_en_buffer_overflo:1,	// 6
		 int_en_request_timeou:1,	// 7
		:8,		// 15:8
		 int_en_head_inv_data_:1,	// 16
		 int_en_rsp_pactype_in:1,	// 17
		 int_en_head_sb_err:1,	// 18
		 int_en_missing_head:1,	// 19
		 int_en_head_inv_rd_ty:1,	// 20
		 int_en_head_cmd_err_b:1,	// 21
		 int_en_req_addr_align:1,	// 22
		 int_en_pio_req_addr_i:1,	// 23
		 int_en_req_range_dsiz:1,	// 24
		 int_en_early_term:1,	// 25
		 int_en_early_tail:1,	// 26
		 int_en_missing_tail:1,	// 27
		 int_en_data_flit_sb_e:1,	// 28
		 int_en_cm2hcm_req_cre:1,	// 29
		 int_en_cm2hcm_rsp_cre:1,	// 30
		 int_en_rx_bad_didn:1,	// 31
		 int_en_rd_dma_err_rsp:1,	// 32
		 int_en_rd_dma_tnum_to:1,	// 33
		 int_en_rd_dma_multi_t:1,	// 34
		 int_en_wr_dma_err_rsp:1,	// 35
		 int_en_wr_dma_tnum_to:1,	// 36
		 int_en_wr_dma_multi_t:1,	// 37
		 int_en_alg_data_overf:1,	// 38
		 int_en_alg_data_under:1,	// 39
		 int_en_ram0_access_co:1,	// 40
		 int_en_ram1_access_co:1,	// 41
		 int_en_ram2_access_co:1,	// 42
		 int_en_ram0_perr:1,	// 43
		 int_en_ram1_perr:1,	// 44
		 int_en_ram2_perr:1,	// 45
		 int_en_int_gen_rsp_er:1,	// 46
		 int_en_int_gen_tnum_t:1,	// 47
		 int_en_rd_dma_prog_er:1,	// 48
		 int_en_wr_dma_prog_er:1,	// 49
		:14;		// 63:50
	};
};

struct cm_mmr {
	union cm_id id;
	union cm_status status;
	union cm_error_detail1 err_detail1;
	union cm_error_detail2 err_detail2;
	union cm_control control;
	union cm_req_timeout req_timeout;
	uint64_t reserved1[1];
	union intr_dest int_dest;
	uint64_t reserved2[2];
	uint64_t targ_flush;
	uint64_t reserved3[1];
	union cm_error_status err_status;
	union cm_clr_error_status clr_err_status;
	union cm_error_intr_enable int_enable;
};

union dma_hostaddr {
	uint64_t dma_hostaddr_reg;
	struct {
		uint64_t dma_sys_addr:56,	// 55:0
		:8;		// 63:56
	};
};

union dma_localaddr {
	uint64_t dma_localaddr_reg;
	struct {
		uint64_t dma_ram_addr:21,	// 20:0
		 dma_ram_sel:2,	// 22:21
		:41;		// 63:23
	};
};

union dma_control {
	uint64_t dma_control_reg;
	struct {
		uint64_t dma_op_length:16,	// 15:0
		:18,		// 33:16
		 done_amo_en:1,	// 34
		 done_int_en:1,	// 35
		:1,		// 36
		 pio_mem_n:1,	// 37
		:26;		// 63:38
	};
};

union dma_amo_dest {
	uint64_t dma_amo_dest_reg;
	struct {
		uint64_t dma_amo_sys_addr:56,	// 55:0
		 dma_amo_mod_type:3,	// 58:56
		:5;		// 63:59
	};
};

union rdma_aux_status {
	uint64_t rdma_aux_status_reg;
	struct {
		uint64_t op_num_pacs_left:17,	// 16:0
		:5,		// 21:17
		 lrsp_buff_empty:1,	// 22
		:17,		// 39:23
		 pending_reqs_left:6,	// 45:40
		:18;		// 63:46
	};
};

struct rdma_mmr {
	union dma_hostaddr host_addr;
	union dma_localaddr local_addr;
	union dma_control control;
	union dma_amo_dest amo_dest;
	union intr_dest intr_dest;
	union rdma_aux_status aux_status;
};

union wdma_aux_status {
	uint64_t wdma_aux_status_reg;
	struct {
		uint64_t op_num_pacs_left:17,	// 16:0
		:4,		// 20:17
		 lreq_buff_empty:1,	// 21
		:18,		// 39:22
		 pending_reqs_left:6,	// 45:40
		:18;		// 63:46
	};
};

struct wdma_mmr {
	union dma_hostaddr host_addr;
	union dma_localaddr local_addr;
	union dma_control control;
	union dma_amo_dest amo_dest;
	union intr_dest intr_dest;
	union wdma_aux_status aux_status;
};

union algo_step {
	uint64_t algo_step_reg;
	struct {
		uint64_t alg_step_cnt:16,	// 15:0
		:48;		// 63:16
	};
};

struct algo_mmr {
	union dma_amo_dest amo_dest;
	union intr_dest intr_dest;
	union {
		uint64_t algo_offset_reg;
		struct {
			uint64_t sram0_offset:7,	// 6:0
			reserved0:1,	// 7
			sram1_offset:7,	// 14:8
			reserved1:1,	// 15
			sram2_offset:7,	// 22:16
			reserved2:14;	// 63:23
		};
	} sram_offset;
	union algo_step step;
};

struct mbcs_mmr {
	struct cm_mmr cm;
	uint64_t reserved1[17];
	struct rdma_mmr rdDma;
	uint64_t reserved2[25];
	struct wdma_mmr wrDma;
	uint64_t reserved3[25];
	struct algo_mmr algo;
	uint64_t reserved4[156];
};

/*
 * defines
 */
#define DEVICE_NAME "mbcs"
#define MBCS_PART_NUM 0xfff0
#define MBCS_PART_NUM_ALG0 0xf001
#define MBCS_MFG_NUM  0x1

struct algoblock {
	uint64_t amoHostDest;
	uint64_t amoModType;
	uint64_t intrHostDest;
	uint64_t intrVector;
	uint64_t algoStepCount;
};

struct getdma {
	uint64_t hostAddr;
	uint64_t localAddr;
	uint64_t bytes;
	uint64_t DoneAmoEnable;
	uint64_t DoneIntEnable;
	uint64_t peerIO;
	uint64_t amoHostDest;
	uint64_t amoModType;
	uint64_t intrHostDest;
	uint64_t intrVector;
};

struct putdma {
	uint64_t hostAddr;
	uint64_t localAddr;
	uint64_t bytes;
	uint64_t DoneAmoEnable;
	uint64_t DoneIntEnable;
	uint64_t peerIO;
	uint64_t amoHostDest;
	uint64_t amoModType;
	uint64_t intrHostDest;
	uint64_t intrVector;
};

struct mbcs_soft {
	struct list_head list;
	struct cx_dev *cxdev;
	int major;
	int nasid;
	void *mmr_base;
	wait_queue_head_t dmawrite_queue;
	wait_queue_head_t dmaread_queue;
	wait_queue_head_t algo_queue;
	struct sn_irq_info *get_sn_irq;
	struct sn_irq_info *put_sn_irq;
	struct sn_irq_info *algo_sn_irq;
	struct getdma getdma;
	struct putdma putdma;
	struct algoblock algo;
	uint64_t gscr_addr;	// pio addr
	uint64_t ram0_addr;	// pio addr
	uint64_t ram1_addr;	// pio addr
	uint64_t ram2_addr;	// pio addr
	uint64_t debug_addr;	// pio addr
	atomic_t dmawrite_done;
	atomic_t dmaread_done;
	atomic_t algo_done;
	struct mutex dmawritelock;
	struct mutex dmareadlock;
	struct mutex algolock;
};

static int mbcs_open(struct inode *ip, struct file *fp);
static ssize_t mbcs_sram_read(struct file *fp, char __user *buf, size_t len,
			      loff_t * off);
static ssize_t mbcs_sram_write(struct file *fp, const char __user *buf, size_t len,
			       loff_t * off);
static loff_t mbcs_sram_llseek(struct file *filp, loff_t off, int whence);
static int mbcs_gscr_mmap(struct file *fp, struct vm_area_struct *vma);

#endif				// __MBCS_H__

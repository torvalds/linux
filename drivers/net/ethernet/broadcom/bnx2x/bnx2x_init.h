/* bnx2x_init.h: Broadcom Everest network driver.
 *               Structures and macroes needed during the initialization.
 *
 * Copyright (c) 2007-2013 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 * Maintained by: Ariel Elior <ariel.elior@qlogic.com>
 * Written by: Eliezer Tamir
 * Modified by: Vladislav Zolotarov
 */

#ifndef BNX2X_INIT_H
#define BNX2X_INIT_H

/* Init operation types and structures */
enum {
	OP_RD = 0x1,	/* read a single register */
	OP_WR,		/* write a single register */
	OP_SW,		/* copy a string to the device */
	OP_ZR,		/* clear memory */
	OP_ZP,		/* unzip then copy with DMAE */
	OP_WR_64,	/* write 64 bit pattern */
	OP_WB,		/* copy a string using DMAE */
	OP_WB_ZR,	/* Clear a string using DMAE or indirect-wr */
	/* Skip the following ops if all of the init modes don't match */
	OP_IF_MODE_OR,
	/* Skip the following ops if any of the init modes don't match */
	OP_IF_MODE_AND,
	OP_MAX
};

enum {
	STAGE_START,
	STAGE_END,
};

/* Returns the index of start or end of a specific block stage in ops array*/
#define BLOCK_OPS_IDX(block, stage, end) \
	(2*(((block)*NUM_OF_INIT_PHASES) + (stage)) + (end))


/* structs for the various opcodes */
struct raw_op {
	u32 op:8;
	u32 offset:24;
	u32 raw_data;
};

struct op_read {
	u32 op:8;
	u32 offset:24;
	u32 val;
};

struct op_write {
	u32 op:8;
	u32 offset:24;
	u32 val;
};

struct op_arr_write {
	u32 op:8;
	u32 offset:24;
#ifdef __BIG_ENDIAN
	u16 data_len;
	u16 data_off;
#else /* __LITTLE_ENDIAN */
	u16 data_off;
	u16 data_len;
#endif
};

struct op_zero {
	u32 op:8;
	u32 offset:24;
	u32 len;
};

struct op_if_mode {
	u32 op:8;
	u32 cmd_offset:24;
	u32 mode_bit_map;
};


union init_op {
	struct op_read		read;
	struct op_write		write;
	struct op_arr_write	arr_wr;
	struct op_zero		zero;
	struct raw_op		raw;
	struct op_if_mode	if_mode;
};


/* Init Phases */
enum {
	PHASE_COMMON,
	PHASE_PORT0,
	PHASE_PORT1,
	PHASE_PF0,
	PHASE_PF1,
	PHASE_PF2,
	PHASE_PF3,
	PHASE_PF4,
	PHASE_PF5,
	PHASE_PF6,
	PHASE_PF7,
	NUM_OF_INIT_PHASES
};

/* Init Modes */
enum {
	MODE_ASIC                      = 0x00000001,
	MODE_FPGA                      = 0x00000002,
	MODE_EMUL                      = 0x00000004,
	MODE_E2                        = 0x00000008,
	MODE_E3                        = 0x00000010,
	MODE_PORT2                     = 0x00000020,
	MODE_PORT4                     = 0x00000040,
	MODE_SF                        = 0x00000080,
	MODE_MF                        = 0x00000100,
	MODE_MF_SD                     = 0x00000200,
	MODE_MF_SI                     = 0x00000400,
	MODE_MF_AFEX                   = 0x00000800,
	MODE_E3_A0                     = 0x00001000,
	MODE_E3_B0                     = 0x00002000,
	MODE_COS3                      = 0x00004000,
	MODE_COS6                      = 0x00008000,
	MODE_LITTLE_ENDIAN             = 0x00010000,
	MODE_BIG_ENDIAN                = 0x00020000,
};

/* Init Blocks */
enum {
	BLOCK_ATC,
	BLOCK_BRB1,
	BLOCK_CCM,
	BLOCK_CDU,
	BLOCK_CFC,
	BLOCK_CSDM,
	BLOCK_CSEM,
	BLOCK_DBG,
	BLOCK_DMAE,
	BLOCK_DORQ,
	BLOCK_HC,
	BLOCK_IGU,
	BLOCK_MISC,
	BLOCK_NIG,
	BLOCK_PBF,
	BLOCK_PGLUE_B,
	BLOCK_PRS,
	BLOCK_PXP2,
	BLOCK_PXP,
	BLOCK_QM,
	BLOCK_SRC,
	BLOCK_TCM,
	BLOCK_TM,
	BLOCK_TSDM,
	BLOCK_TSEM,
	BLOCK_UCM,
	BLOCK_UPB,
	BLOCK_USDM,
	BLOCK_USEM,
	BLOCK_XCM,
	BLOCK_XPB,
	BLOCK_XSDM,
	BLOCK_XSEM,
	BLOCK_MISC_AEU,
	NUM_OF_INIT_BLOCKS
};

/* QM queue numbers */
#define BNX2X_ETH_Q		0
#define BNX2X_TOE_Q		3
#define BNX2X_TOE_ACK_Q		6
#define BNX2X_ISCSI_Q		9
#define BNX2X_ISCSI_ACK_Q	11
#define BNX2X_FCOE_Q		10

/* Vnics per mode */
#define BNX2X_PORT2_MODE_NUM_VNICS 4
#define BNX2X_PORT4_MODE_NUM_VNICS 2

/* COS offset for port1 in E3 B0 4port mode */
#define BNX2X_E3B0_PORT1_COS_OFFSET 3

/* QM Register addresses */
#define BNX2X_Q_VOQ_REG_ADDR(pf_q_num)\
	(QM_REG_QVOQIDX_0 + 4 * (pf_q_num))
#define BNX2X_VOQ_Q_REG_ADDR(cos, pf_q_num)\
	(QM_REG_VOQQMASK_0_LSB + 4 * ((cos) * 2 + ((pf_q_num) >> 5)))
#define BNX2X_Q_CMDQ_REG_ADDR(pf_q_num)\
	(QM_REG_BYTECRDCMDQ_0 + 4 * ((pf_q_num) >> 4))

/* extracts the QM queue number for the specified port and vnic */
#define BNX2X_PF_Q_NUM(q_num, port, vnic)\
	((((port) << 1) | (vnic)) * 16 + (q_num))


/* Maps the specified queue to the specified COS */
static inline void bnx2x_map_q_cos(struct bnx2x *bp, u32 q_num, u32 new_cos)
{
	/* find current COS mapping */
	u32 curr_cos = REG_RD(bp, QM_REG_QVOQIDX_0 + q_num * 4);

	/* check if queue->COS mapping has changed */
	if (curr_cos != new_cos) {
		u32 num_vnics = BNX2X_PORT2_MODE_NUM_VNICS;
		u32 reg_addr, reg_bit_map, vnic;

		/* update parameters for 4port mode */
		if (INIT_MODE_FLAGS(bp) & MODE_PORT4) {
			num_vnics = BNX2X_PORT4_MODE_NUM_VNICS;
			if (BP_PORT(bp)) {
				curr_cos += BNX2X_E3B0_PORT1_COS_OFFSET;
				new_cos += BNX2X_E3B0_PORT1_COS_OFFSET;
			}
		}

		/* change queue mapping for each VNIC */
		for (vnic = 0; vnic < num_vnics; vnic++) {
			u32 pf_q_num =
				BNX2X_PF_Q_NUM(q_num, BP_PORT(bp), vnic);
			u32 q_bit_map = 1 << (pf_q_num & 0x1f);

			/* overwrite queue->VOQ mapping */
			REG_WR(bp, BNX2X_Q_VOQ_REG_ADDR(pf_q_num), new_cos);

			/* clear queue bit from current COS bit map */
			reg_addr = BNX2X_VOQ_Q_REG_ADDR(curr_cos, pf_q_num);
			reg_bit_map = REG_RD(bp, reg_addr);
			REG_WR(bp, reg_addr, reg_bit_map & (~q_bit_map));

			/* set queue bit in new COS bit map */
			reg_addr = BNX2X_VOQ_Q_REG_ADDR(new_cos, pf_q_num);
			reg_bit_map = REG_RD(bp, reg_addr);
			REG_WR(bp, reg_addr, reg_bit_map | q_bit_map);

			/* set/clear queue bit in command-queue bit map
			 * (E2/E3A0 only, valid COS values are 0/1)
			 */
			if (!(INIT_MODE_FLAGS(bp) & MODE_E3_B0)) {
				reg_addr = BNX2X_Q_CMDQ_REG_ADDR(pf_q_num);
				reg_bit_map = REG_RD(bp, reg_addr);
				q_bit_map = 1 << (2 * (pf_q_num & 0xf));
				reg_bit_map = new_cos ?
					      (reg_bit_map | q_bit_map) :
					      (reg_bit_map & (~q_bit_map));
				REG_WR(bp, reg_addr, reg_bit_map);
			}
		}
	}
}

/* Configures the QM according to the specified per-traffic-type COSes */
static inline void bnx2x_dcb_config_qm(struct bnx2x *bp, enum cos_mode mode,
				       struct priority_cos *traffic_cos)
{
	bnx2x_map_q_cos(bp, BNX2X_FCOE_Q,
			traffic_cos[LLFC_TRAFFIC_TYPE_FCOE].cos);
	bnx2x_map_q_cos(bp, BNX2X_ISCSI_Q,
			traffic_cos[LLFC_TRAFFIC_TYPE_ISCSI].cos);
	bnx2x_map_q_cos(bp, BNX2X_ISCSI_ACK_Q,
		traffic_cos[LLFC_TRAFFIC_TYPE_ISCSI].cos);
	if (mode != STATIC_COS) {
		/* required only in backward compatible COS mode */
		bnx2x_map_q_cos(bp, BNX2X_ETH_Q,
				traffic_cos[LLFC_TRAFFIC_TYPE_NW].cos);
		bnx2x_map_q_cos(bp, BNX2X_TOE_Q,
				traffic_cos[LLFC_TRAFFIC_TYPE_NW].cos);
		bnx2x_map_q_cos(bp, BNX2X_TOE_ACK_Q,
				traffic_cos[LLFC_TRAFFIC_TYPE_NW].cos);
	}
}


/* congestion managment port init api description
 * the api works as follows:
 * the driver should pass the cmng_init_input struct, the port_init function
 * will prepare the required internal ram structure which will be passed back
 * to the driver (cmng_init) that will write it into the internal ram.
 *
 * IMPORTANT REMARKS:
 * 1. the cmng_init struct does not represent the contiguous internal ram
 *    structure. the driver should use the XSTORM_CMNG_PERPORT_VARS_OFFSET
 *    offset in order to write the port sub struct and the
 *    PFID_FROM_PORT_AND_VNIC offset for writing the vnic sub struct (in other
 *    words - don't use memcpy!).
 * 2. although the cmng_init struct is filled for the maximal vnic number
 *    possible, the driver should only write the valid vnics into the internal
 *    ram according to the appropriate port mode.
 */
#define BITS_TO_BYTES(x) ((x)/8)

/* CMNG constants, as derived from system spec calculations */

/* default MIN rate in case VNIC min rate is configured to zero- 100Mbps */
#define DEF_MIN_RATE 100

/* resolution of the rate shaping timer - 400 usec */
#define RS_PERIODIC_TIMEOUT_USEC 400

/* number of bytes in single QM arbitration cycle -
 * coefficient for calculating the fairness timer
 */
#define QM_ARB_BYTES 160000

/* resolution of Min algorithm 1:100 */
#define MIN_RES 100

/* how many bytes above threshold for
 * the minimal credit of Min algorithm
 */
#define MIN_ABOVE_THRESH 32768

/* Fairness algorithm integration time coefficient -
 * for calculating the actual Tfair
 */
#define T_FAIR_COEF ((MIN_ABOVE_THRESH + QM_ARB_BYTES) * 8 * MIN_RES)

/* Memory of fairness algorithm - 2 cycles */
#define FAIR_MEM 2
#define SAFC_TIMEOUT_USEC 52

#define SDM_TICKS 4


static inline void bnx2x_init_max(const struct cmng_init_input *input_data,
				  u32 r_param, struct cmng_init *ram_data)
{
	u32 vnic;
	struct cmng_vnic *vdata = &ram_data->vnic;
	struct cmng_struct_per_port *pdata = &ram_data->port;
	/* rate shaping per-port variables
	 * 100 micro seconds in SDM ticks = 25
	 * since each tick is 4 microSeconds
	 */

	pdata->rs_vars.rs_periodic_timeout =
	RS_PERIODIC_TIMEOUT_USEC / SDM_TICKS;

	/* this is the threshold below which no timer arming will occur.
	 * 1.25 coefficient is for the threshold to be a little bigger
	 * then the real time to compensate for timer in-accuracy
	 */
	pdata->rs_vars.rs_threshold =
	(5 * RS_PERIODIC_TIMEOUT_USEC * r_param)/4;

	/* rate shaping per-vnic variables */
	for (vnic = 0; vnic < BNX2X_PORT2_MODE_NUM_VNICS; vnic++) {
		/* global vnic counter */
		vdata->vnic_max_rate[vnic].vn_counter.rate =
		input_data->vnic_max_rate[vnic];
		/* maximal Mbps for this vnic
		 * the quota in each timer period - number of bytes
		 * transmitted in this period
		 */
		vdata->vnic_max_rate[vnic].vn_counter.quota =
			RS_PERIODIC_TIMEOUT_USEC *
			(u32)vdata->vnic_max_rate[vnic].vn_counter.rate / 8;
	}

}

static inline void bnx2x_init_min(const struct cmng_init_input *input_data,
				  u32 r_param, struct cmng_init *ram_data)
{
	u32 vnic, fair_periodic_timeout_usec, vnicWeightSum, tFair;
	struct cmng_vnic *vdata = &ram_data->vnic;
	struct cmng_struct_per_port *pdata = &ram_data->port;

	/* this is the resolution of the fairness timer */
	fair_periodic_timeout_usec = QM_ARB_BYTES / r_param;

	/* fairness per-port variables
	 * for 10G it is 1000usec. for 1G it is 10000usec.
	 */
	tFair = T_FAIR_COEF / input_data->port_rate;

	/* this is the threshold below which we won't arm the timer anymore */
	pdata->fair_vars.fair_threshold = QM_ARB_BYTES;

	/* we multiply by 1e3/8 to get bytes/msec. We don't want the credits
	 * to pass a credit of the T_FAIR*FAIR_MEM (algorithm resolution)
	 */
	pdata->fair_vars.upper_bound = r_param * tFair * FAIR_MEM;

	/* since each tick is 4 microSeconds */
	pdata->fair_vars.fairness_timeout =
				fair_periodic_timeout_usec / SDM_TICKS;

	/* calculate sum of weights */
	vnicWeightSum = 0;

	for (vnic = 0; vnic < BNX2X_PORT2_MODE_NUM_VNICS; vnic++)
		vnicWeightSum += input_data->vnic_min_rate[vnic];

	/* global vnic counter */
	if (vnicWeightSum > 0) {
		/* fairness per-vnic variables */
		for (vnic = 0; vnic < BNX2X_PORT2_MODE_NUM_VNICS; vnic++) {
			/* this is the credit for each period of the fairness
			 * algorithm - number of bytes in T_FAIR (this vnic
			 * share of the port rate)
			 */
			vdata->vnic_min_rate[vnic].vn_credit_delta =
				(u32)input_data->vnic_min_rate[vnic] * 100 *
				(T_FAIR_COEF / (8 * 100 * vnicWeightSum));
			if (vdata->vnic_min_rate[vnic].vn_credit_delta <
			    pdata->fair_vars.fair_threshold +
			    MIN_ABOVE_THRESH) {
				vdata->vnic_min_rate[vnic].vn_credit_delta =
					pdata->fair_vars.fair_threshold +
					MIN_ABOVE_THRESH;
			}
		}
	}
}

static inline void bnx2x_init_fw_wrr(const struct cmng_init_input *input_data,
				     u32 r_param, struct cmng_init *ram_data)
{
	u32 vnic, cos;
	u32 cosWeightSum = 0;
	struct cmng_vnic *vdata = &ram_data->vnic;
	struct cmng_struct_per_port *pdata = &ram_data->port;

	for (cos = 0; cos < MAX_COS_NUMBER; cos++)
		cosWeightSum += input_data->cos_min_rate[cos];

	if (cosWeightSum > 0) {

		for (vnic = 0; vnic < BNX2X_PORT2_MODE_NUM_VNICS; vnic++) {
			/* Since cos and vnic shouldn't work together the rate
			 * to divide between the coses is the port rate.
			 */
			u32 *ccd = vdata->vnic_min_rate[vnic].cos_credit_delta;
			for (cos = 0; cos < MAX_COS_NUMBER; cos++) {
				/* this is the credit for each period of
				 * the fairness algorithm - number of bytes
				 * in T_FAIR (this cos share of the vnic rate)
				 */
				ccd[cos] =
				    (u32)input_data->cos_min_rate[cos] * 100 *
				    (T_FAIR_COEF / (8 * 100 * cosWeightSum));
				 if (ccd[cos] < pdata->fair_vars.fair_threshold
						+ MIN_ABOVE_THRESH) {
					ccd[cos] =
					    pdata->fair_vars.fair_threshold +
					    MIN_ABOVE_THRESH;
				}
			}
		}
	}
}

static inline void bnx2x_init_safc(const struct cmng_init_input *input_data,
				   struct cmng_init *ram_data)
{
	/* in microSeconds */
	ram_data->port.safc_vars.safc_timeout_usec = SAFC_TIMEOUT_USEC;
}

/* Congestion management port init */
static inline void bnx2x_init_cmng(const struct cmng_init_input *input_data,
				   struct cmng_init *ram_data)
{
	u32 r_param;
	memset(ram_data, 0, sizeof(struct cmng_init));

	ram_data->port.flags = input_data->flags;

	/* number of bytes transmitted in a rate of 10Gbps
	 * in one usec = 1.25KB.
	 */
	r_param = BITS_TO_BYTES(input_data->port_rate);
	bnx2x_init_max(input_data, r_param, ram_data);
	bnx2x_init_min(input_data, r_param, ram_data);
	bnx2x_init_fw_wrr(input_data, r_param, ram_data);
	bnx2x_init_safc(input_data, ram_data);
}



/* Returns the index of start or end of a specific block stage in ops array */
#define BLOCK_OPS_IDX(block, stage, end) \
			(2*(((block)*NUM_OF_INIT_PHASES) + (stage)) + (end))


#define INITOP_SET		0	/* set the HW directly */
#define INITOP_CLEAR		1	/* clear the HW directly */
#define INITOP_INIT		2	/* set the init-value array */

/****************************************************************************
* ILT management
****************************************************************************/
struct ilt_line {
	dma_addr_t page_mapping;
	void *page;
	u32 size;
};

struct ilt_client_info {
	u32 page_size;
	u16 start;
	u16 end;
	u16 client_num;
	u16 flags;
#define ILT_CLIENT_SKIP_INIT	0x1
#define ILT_CLIENT_SKIP_MEM	0x2
};

struct bnx2x_ilt {
	u32 start_line;
	struct ilt_line		*lines;
	struct ilt_client_info	clients[4];
#define ILT_CLIENT_CDU	0
#define ILT_CLIENT_QM	1
#define ILT_CLIENT_SRC	2
#define ILT_CLIENT_TM	3
};

/****************************************************************************
* SRC configuration
****************************************************************************/
struct src_ent {
	u8 opaque[56];
	u64 next;
};

/****************************************************************************
* Parity configuration
****************************************************************************/
#define BLOCK_PRTY_INFO(block, en_mask, m1, m1h, m2, m3) \
{ \
	block##_REG_##block##_PRTY_MASK, \
	block##_REG_##block##_PRTY_STS_CLR, \
	en_mask, {m1, m1h, m2, m3}, #block \
}

#define BLOCK_PRTY_INFO_0(block, en_mask, m1, m1h, m2, m3) \
{ \
	block##_REG_##block##_PRTY_MASK_0, \
	block##_REG_##block##_PRTY_STS_CLR_0, \
	en_mask, {m1, m1h, m2, m3}, #block"_0" \
}

#define BLOCK_PRTY_INFO_1(block, en_mask, m1, m1h, m2, m3) \
{ \
	block##_REG_##block##_PRTY_MASK_1, \
	block##_REG_##block##_PRTY_STS_CLR_1, \
	en_mask, {m1, m1h, m2, m3}, #block"_1" \
}

static const struct {
	u32 mask_addr;
	u32 sts_clr_addr;
	u32 en_mask;		/* Mask to enable parity attentions */
	struct {
		u32 e1;		/* 57710 */
		u32 e1h;	/* 57711 */
		u32 e2;		/* 57712 */
		u32 e3;		/* 578xx */
	} reg_mask;		/* Register mask (all valid bits) */
	char name[8];		/* Block's longest name is 7 characters long
				 * (name + suffix)
				 */
} bnx2x_blocks_parity_data[] = {
	/* bit 19 masked */
	/* REG_WR(bp, PXP_REG_PXP_PRTY_MASK, 0x80000); */
	/* bit 5,18,20-31 */
	/* REG_WR(bp, PXP2_REG_PXP2_PRTY_MASK_0, 0xfff40020); */
	/* bit 5 */
	/* REG_WR(bp, PXP2_REG_PXP2_PRTY_MASK_1, 0x20);	*/
	/* REG_WR(bp, HC_REG_HC_PRTY_MASK, 0x0); */
	/* REG_WR(bp, MISC_REG_MISC_PRTY_MASK, 0x0); */

	/* Block IGU, MISC, PXP and PXP2 parity errors as long as we don't
	 * want to handle "system kill" flow at the moment.
	 */
	BLOCK_PRTY_INFO(PXP, 0x7ffffff, 0x3ffffff, 0x3ffffff, 0x7ffffff,
			0x7ffffff),
	BLOCK_PRTY_INFO_0(PXP2,	0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
			  0xffffffff),
	BLOCK_PRTY_INFO_1(PXP2,	0x1ffffff, 0x7f, 0x7f, 0x7ff, 0x1ffffff),
	BLOCK_PRTY_INFO(HC, 0x7, 0x7, 0x7, 0, 0),
	BLOCK_PRTY_INFO(NIG, 0xffffffff, 0x3fffffff, 0xffffffff, 0, 0),
	BLOCK_PRTY_INFO_0(NIG,	0xffffffff, 0, 0, 0xffffffff, 0xffffffff),
	BLOCK_PRTY_INFO_1(NIG,	0xffff, 0, 0, 0xff, 0xffff),
	BLOCK_PRTY_INFO(IGU, 0x7ff, 0, 0, 0x7ff, 0x7ff),
	BLOCK_PRTY_INFO(MISC, 0x1, 0x1, 0x1, 0x1, 0x1),
	BLOCK_PRTY_INFO(QM, 0, 0x1ff, 0xfff, 0xfff, 0xfff),
	BLOCK_PRTY_INFO(ATC, 0x1f, 0, 0, 0x1f, 0x1f),
	BLOCK_PRTY_INFO(PGLUE_B, 0x3, 0, 0, 0x3, 0x3),
	BLOCK_PRTY_INFO(DORQ, 0, 0x3, 0x3, 0x3, 0x3),
	{GRCBASE_UPB + PB_REG_PB_PRTY_MASK,
		GRCBASE_UPB + PB_REG_PB_PRTY_STS_CLR, 0xf,
		{0xf, 0xf, 0xf, 0xf}, "UPB"},
	{GRCBASE_XPB + PB_REG_PB_PRTY_MASK,
		GRCBASE_XPB + PB_REG_PB_PRTY_STS_CLR, 0,
		{0xf, 0xf, 0xf, 0xf}, "XPB"},
	BLOCK_PRTY_INFO(SRC, 0x4, 0x7, 0x7, 0x7, 0x7),
	BLOCK_PRTY_INFO(CDU, 0, 0x1f, 0x1f, 0x1f, 0x1f),
	BLOCK_PRTY_INFO(CFC, 0, 0xf, 0xf, 0xf, 0x3f),
	BLOCK_PRTY_INFO(DBG, 0, 0x1, 0x1, 0x1, 0x1),
	BLOCK_PRTY_INFO(DMAE, 0, 0xf, 0xf, 0xf, 0xf),
	BLOCK_PRTY_INFO(BRB1, 0, 0xf, 0xf, 0xf, 0xf),
	BLOCK_PRTY_INFO(PRS, (1<<6), 0xff, 0xff, 0xff, 0xff),
	BLOCK_PRTY_INFO(PBF, 0, 0, 0x3ffff, 0xfffff, 0xfffffff),
	BLOCK_PRTY_INFO(TM, 0, 0, 0x7f, 0x7f, 0x7f),
	BLOCK_PRTY_INFO(TSDM, 0x18, 0x7ff, 0x7ff, 0x7ff, 0x7ff),
	BLOCK_PRTY_INFO(CSDM, 0x8, 0x7ff, 0x7ff, 0x7ff, 0x7ff),
	BLOCK_PRTY_INFO(USDM, 0x38, 0x7ff, 0x7ff, 0x7ff, 0x7ff),
	BLOCK_PRTY_INFO(XSDM, 0x8, 0x7ff, 0x7ff, 0x7ff, 0x7ff),
	BLOCK_PRTY_INFO(TCM, 0, 0, 0x7ffffff, 0x7ffffff, 0x7ffffff),
	BLOCK_PRTY_INFO(CCM, 0, 0, 0x7ffffff, 0x7ffffff, 0x7ffffff),
	BLOCK_PRTY_INFO(UCM, 0, 0, 0x7ffffff, 0x7ffffff, 0x7ffffff),
	BLOCK_PRTY_INFO(XCM, 0, 0, 0x3fffffff, 0x3fffffff, 0x3fffffff),
	BLOCK_PRTY_INFO_0(TSEM, 0, 0xffffffff, 0xffffffff, 0xffffffff,
			  0xffffffff),
	BLOCK_PRTY_INFO_1(TSEM, 0, 0x3, 0x1f, 0x3f, 0x3f),
	BLOCK_PRTY_INFO_0(USEM, 0, 0xffffffff, 0xffffffff, 0xffffffff,
			  0xffffffff),
	BLOCK_PRTY_INFO_1(USEM, 0, 0x3, 0x1f, 0x1f, 0x1f),
	BLOCK_PRTY_INFO_0(CSEM, 0, 0xffffffff, 0xffffffff, 0xffffffff,
			  0xffffffff),
	BLOCK_PRTY_INFO_1(CSEM, 0, 0x3, 0x1f, 0x1f, 0x1f),
	BLOCK_PRTY_INFO_0(XSEM, 0, 0xffffffff, 0xffffffff, 0xffffffff,
			  0xffffffff),
	BLOCK_PRTY_INFO_1(XSEM, 0, 0x3, 0x1f, 0x3f, 0x3f),
};


/* [28] MCP Latched rom_parity
 * [29] MCP Latched ump_rx_parity
 * [30] MCP Latched ump_tx_parity
 * [31] MCP Latched scpad_parity
 */
#define MISC_AEU_ENABLE_MCP_PRTY_SUB_BITS	\
	(AEU_INPUTS_ATTN_BITS_MCP_LATCHED_ROM_PARITY | \
	 AEU_INPUTS_ATTN_BITS_MCP_LATCHED_UMP_RX_PARITY | \
	 AEU_INPUTS_ATTN_BITS_MCP_LATCHED_UMP_TX_PARITY)

#define MISC_AEU_ENABLE_MCP_PRTY_BITS	\
	(MISC_AEU_ENABLE_MCP_PRTY_SUB_BITS | \
	 AEU_INPUTS_ATTN_BITS_MCP_LATCHED_SCPAD_PARITY)

/* Below registers control the MCP parity attention output. When
 * MISC_AEU_ENABLE_MCP_PRTY_BITS are set - attentions are
 * enabled, when cleared - disabled.
 */
static const struct {
	u32 addr;
	u32 bits;
} mcp_attn_ctl_regs[] = {
	{ MISC_REG_AEU_ENABLE4_FUNC_0_OUT_0,
		MISC_AEU_ENABLE_MCP_PRTY_BITS },
	{ MISC_REG_AEU_ENABLE4_NIG_0,
		MISC_AEU_ENABLE_MCP_PRTY_SUB_BITS },
	{ MISC_REG_AEU_ENABLE4_PXP_0,
		MISC_AEU_ENABLE_MCP_PRTY_SUB_BITS },
	{ MISC_REG_AEU_ENABLE4_FUNC_1_OUT_0,
		MISC_AEU_ENABLE_MCP_PRTY_BITS },
	{ MISC_REG_AEU_ENABLE4_NIG_1,
		MISC_AEU_ENABLE_MCP_PRTY_SUB_BITS },
	{ MISC_REG_AEU_ENABLE4_PXP_1,
		MISC_AEU_ENABLE_MCP_PRTY_SUB_BITS }
};

static inline void bnx2x_set_mcp_parity(struct bnx2x *bp, u8 enable)
{
	int i;
	u32 reg_val;

	for (i = 0; i < ARRAY_SIZE(mcp_attn_ctl_regs); i++) {
		reg_val = REG_RD(bp, mcp_attn_ctl_regs[i].addr);

		if (enable)
			reg_val |= mcp_attn_ctl_regs[i].bits;
		else
			reg_val &= ~mcp_attn_ctl_regs[i].bits;

		REG_WR(bp, mcp_attn_ctl_regs[i].addr, reg_val);
	}
}

static inline u32 bnx2x_parity_reg_mask(struct bnx2x *bp, int idx)
{
	if (CHIP_IS_E1(bp))
		return bnx2x_blocks_parity_data[idx].reg_mask.e1;
	else if (CHIP_IS_E1H(bp))
		return bnx2x_blocks_parity_data[idx].reg_mask.e1h;
	else if (CHIP_IS_E2(bp))
		return bnx2x_blocks_parity_data[idx].reg_mask.e2;
	else /* CHIP_IS_E3 */
		return bnx2x_blocks_parity_data[idx].reg_mask.e3;
}

static inline void bnx2x_disable_blocks_parity(struct bnx2x *bp)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(bnx2x_blocks_parity_data); i++) {
		u32 dis_mask = bnx2x_parity_reg_mask(bp, i);

		if (dis_mask) {
			REG_WR(bp, bnx2x_blocks_parity_data[i].mask_addr,
			       dis_mask);
			DP(NETIF_MSG_HW, "Setting parity mask "
						 "for %s to\t\t0x%x\n",
				    bnx2x_blocks_parity_data[i].name, dis_mask);
		}
	}

	/* Disable MCP parity attentions */
	bnx2x_set_mcp_parity(bp, false);
}

/* Clear the parity error status registers. */
static inline void bnx2x_clear_blocks_parity(struct bnx2x *bp)
{
	int i;
	u32 reg_val, mcp_aeu_bits =
		AEU_INPUTS_ATTN_BITS_MCP_LATCHED_ROM_PARITY |
		AEU_INPUTS_ATTN_BITS_MCP_LATCHED_SCPAD_PARITY |
		AEU_INPUTS_ATTN_BITS_MCP_LATCHED_UMP_RX_PARITY |
		AEU_INPUTS_ATTN_BITS_MCP_LATCHED_UMP_TX_PARITY;

	/* Clear SEM_FAST parities */
	REG_WR(bp, XSEM_REG_FAST_MEMORY + SEM_FAST_REG_PARITY_RST, 0x1);
	REG_WR(bp, TSEM_REG_FAST_MEMORY + SEM_FAST_REG_PARITY_RST, 0x1);
	REG_WR(bp, USEM_REG_FAST_MEMORY + SEM_FAST_REG_PARITY_RST, 0x1);
	REG_WR(bp, CSEM_REG_FAST_MEMORY + SEM_FAST_REG_PARITY_RST, 0x1);

	for (i = 0; i < ARRAY_SIZE(bnx2x_blocks_parity_data); i++) {
		u32 reg_mask = bnx2x_parity_reg_mask(bp, i);

		if (reg_mask) {
			reg_val = REG_RD(bp, bnx2x_blocks_parity_data[i].
					 sts_clr_addr);
			if (reg_val & reg_mask)
				DP(NETIF_MSG_HW,
					    "Parity errors in %s: 0x%x\n",
					    bnx2x_blocks_parity_data[i].name,
					    reg_val & reg_mask);
		}
	}

	/* Check if there were parity attentions in MCP */
	reg_val = REG_RD(bp, MISC_REG_AEU_AFTER_INVERT_4_MCP);
	if (reg_val & mcp_aeu_bits)
		DP(NETIF_MSG_HW, "Parity error in MCP: 0x%x\n",
		   reg_val & mcp_aeu_bits);

	/* Clear parity attentions in MCP:
	 * [7]  clears Latched rom_parity
	 * [8]  clears Latched ump_rx_parity
	 * [9]  clears Latched ump_tx_parity
	 * [10] clears Latched scpad_parity (both ports)
	 */
	REG_WR(bp, MISC_REG_AEU_CLR_LATCH_SIGNAL, 0x780);
}

static inline void bnx2x_enable_blocks_parity(struct bnx2x *bp)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(bnx2x_blocks_parity_data); i++) {
		u32 reg_mask = bnx2x_parity_reg_mask(bp, i);

		if (reg_mask)
			REG_WR(bp, bnx2x_blocks_parity_data[i].mask_addr,
				bnx2x_blocks_parity_data[i].en_mask & reg_mask);
	}

	/* Enable MCP parity attentions */
	bnx2x_set_mcp_parity(bp, true);
}


#endif /* BNX2X_INIT_H */


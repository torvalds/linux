/*
  This file is provided under a dual BSD/GPLv2 license.  When using or
  redistributing this file, you may do so under either license.

  GPL LICENSE SUMMARY
  Copyright(c) 2014 Intel Corporation.
  This program is free software; you can redistribute it and/or modify
  it under the terms of version 2 of the GNU General Public License as
  published by the Free Software Foundation.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  Contact Information:
  qat-linux@intel.com

  BSD LICENSE
  Copyright(c) 2014 Intel Corporation.
  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the
      distribution.
    * Neither the name of Intel Corporation nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#include <linux/slab.h>

#include "adf_accel_devices.h"
#include "adf_common_drv.h"
#include "icp_qat_hal.h"
#include "icp_qat_uclo.h"

#define BAD_REGADDR               0xffff
#define MAX_RETRY_TIMES           10000
#define INIT_CTX_ARB_VALUE        0x0
#define INIT_CTX_ENABLE_VALUE     0x0
#define INIT_PC_VALUE             0x0
#define INIT_WAKEUP_EVENTS_VALUE  0x1
#define INIT_SIG_EVENTS_VALUE     0x1
#define INIT_CCENABLE_VALUE       0x2000
#define RST_CSR_QAT_LSB           20
#define RST_CSR_AE_LSB		  0
#define MC_TIMESTAMP_ENABLE       (0x1 << 7)

#define IGNORE_W1C_MASK ((~(1 << CE_BREAKPOINT_BITPOS)) & \
	(~(1 << CE_CNTL_STORE_PARITY_ERROR_BITPOS)) & \
	(~(1 << CE_REG_PAR_ERR_BITPOS)))
#define INSERT_IMMED_GPRA_CONST(inst, const_val) \
	(inst = ((inst & 0xFFFF00C03FFull) | \
		((((const_val) << 12) & 0x0FF00000ull) | \
		(((const_val) << 10) & 0x0003FC00ull))))
#define INSERT_IMMED_GPRB_CONST(inst, const_val) \
	(inst = ((inst & 0xFFFF00FFF00ull) | \
		((((const_val) << 12) & 0x0FF00000ull) | \
		(((const_val) <<  0) & 0x000000FFull))))

#define AE(handle, ae) handle->hal_handle->aes[ae]

static const uint64_t inst_4b[] = {
	0x0F0400C0000ull, 0x0F4400C0000ull, 0x0F040000300ull, 0x0F440000300ull,
	0x0FC066C0000ull, 0x0F0000C0300ull, 0x0F0000C0300ull, 0x0F0000C0300ull,
	0x0A021000000ull
};

static const uint64_t inst[] = {
	0x0F0000C0000ull, 0x0F000000380ull, 0x0D805000011ull, 0x0FC082C0300ull,
	0x0F0000C0300ull, 0x0F0000C0300ull, 0x0F0000C0300ull, 0x0F0000C0300ull,
	0x0A0643C0000ull, 0x0BAC0000301ull, 0x0D802000101ull, 0x0F0000C0001ull,
	0x0FC066C0001ull, 0x0F0000C0300ull, 0x0F0000C0300ull, 0x0F0000C0300ull,
	0x0F000400300ull, 0x0A0610C0000ull, 0x0BAC0000301ull, 0x0D804400101ull,
	0x0A0580C0000ull, 0x0A0581C0000ull, 0x0A0582C0000ull, 0x0A0583C0000ull,
	0x0A0584C0000ull, 0x0A0585C0000ull, 0x0A0586C0000ull, 0x0A0587C0000ull,
	0x0A0588C0000ull, 0x0A0589C0000ull, 0x0A058AC0000ull, 0x0A058BC0000ull,
	0x0A058CC0000ull, 0x0A058DC0000ull, 0x0A058EC0000ull, 0x0A058FC0000ull,
	0x0A05C0C0000ull, 0x0A05C1C0000ull, 0x0A05C2C0000ull, 0x0A05C3C0000ull,
	0x0A05C4C0000ull, 0x0A05C5C0000ull, 0x0A05C6C0000ull, 0x0A05C7C0000ull,
	0x0A05C8C0000ull, 0x0A05C9C0000ull, 0x0A05CAC0000ull, 0x0A05CBC0000ull,
	0x0A05CCC0000ull, 0x0A05CDC0000ull, 0x0A05CEC0000ull, 0x0A05CFC0000ull,
	0x0A0400C0000ull, 0x0B0400C0000ull, 0x0A0401C0000ull, 0x0B0401C0000ull,
	0x0A0402C0000ull, 0x0B0402C0000ull, 0x0A0403C0000ull, 0x0B0403C0000ull,
	0x0A0404C0000ull, 0x0B0404C0000ull, 0x0A0405C0000ull, 0x0B0405C0000ull,
	0x0A0406C0000ull, 0x0B0406C0000ull, 0x0A0407C0000ull, 0x0B0407C0000ull,
	0x0A0408C0000ull, 0x0B0408C0000ull, 0x0A0409C0000ull, 0x0B0409C0000ull,
	0x0A040AC0000ull, 0x0B040AC0000ull, 0x0A040BC0000ull, 0x0B040BC0000ull,
	0x0A040CC0000ull, 0x0B040CC0000ull, 0x0A040DC0000ull, 0x0B040DC0000ull,
	0x0A040EC0000ull, 0x0B040EC0000ull, 0x0A040FC0000ull, 0x0B040FC0000ull,
	0x0D81581C010ull, 0x0E000010000ull, 0x0E000010000ull,
};

void qat_hal_set_live_ctx(struct icp_qat_fw_loader_handle *handle,
			  unsigned char ae, unsigned int ctx_mask)
{
	AE(handle, ae).live_ctx_mask = ctx_mask;
}

#define CSR_RETRY_TIMES 500
static int qat_hal_rd_ae_csr(struct icp_qat_fw_loader_handle *handle,
			     unsigned char ae, unsigned int csr,
			     unsigned int *value)
{
	unsigned int iterations = CSR_RETRY_TIMES;

	do {
		*value = GET_AE_CSR(handle, ae, csr);
		if (!(GET_AE_CSR(handle, ae, LOCAL_CSR_STATUS) & LCS_STATUS))
			return 0;
	} while (iterations--);

	pr_err("QAT: Read CSR timeout\n");
	return -EFAULT;
}

static int qat_hal_wr_ae_csr(struct icp_qat_fw_loader_handle *handle,
			     unsigned char ae, unsigned int csr,
			     unsigned int value)
{
	unsigned int iterations = CSR_RETRY_TIMES;

	do {
		SET_AE_CSR(handle, ae, csr, value);
		if (!(GET_AE_CSR(handle, ae, LOCAL_CSR_STATUS) & LCS_STATUS))
			return 0;
	} while (iterations--);

	pr_err("QAT: Write CSR Timeout\n");
	return -EFAULT;
}

static void qat_hal_get_wakeup_event(struct icp_qat_fw_loader_handle *handle,
				     unsigned char ae, unsigned char ctx,
				     unsigned int *events)
{
	unsigned int cur_ctx;

	qat_hal_rd_ae_csr(handle, ae, CSR_CTX_POINTER, &cur_ctx);
	qat_hal_wr_ae_csr(handle, ae, CSR_CTX_POINTER, ctx);
	qat_hal_rd_ae_csr(handle, ae, CTX_WAKEUP_EVENTS_INDIRECT, events);
	qat_hal_wr_ae_csr(handle, ae, CSR_CTX_POINTER, cur_ctx);
}

static int qat_hal_wait_cycles(struct icp_qat_fw_loader_handle *handle,
			       unsigned char ae, unsigned int cycles,
			       int chk_inactive)
{
	unsigned int base_cnt = 0, cur_cnt = 0;
	unsigned int csr = (1 << ACS_ABO_BITPOS);
	int times = MAX_RETRY_TIMES;
	int elapsed_cycles = 0;

	qat_hal_rd_ae_csr(handle, ae, PROFILE_COUNT, &base_cnt);
	base_cnt &= 0xffff;
	while ((int)cycles > elapsed_cycles && times--) {
		if (chk_inactive)
			qat_hal_rd_ae_csr(handle, ae, ACTIVE_CTX_STATUS, &csr);

		qat_hal_rd_ae_csr(handle, ae, PROFILE_COUNT, &cur_cnt);
		cur_cnt &= 0xffff;
		elapsed_cycles = cur_cnt - base_cnt;

		if (elapsed_cycles < 0)
			elapsed_cycles += 0x10000;

		/* ensure at least 8 time cycles elapsed in wait_cycles */
		if (elapsed_cycles >= 8 && !(csr & (1 << ACS_ABO_BITPOS)))
			return 0;
	}
	if (!times) {
		pr_err("QAT: wait_num_cycles time out\n");
		return -EFAULT;
	}
	return 0;
}

#define CLR_BIT(wrd, bit) (wrd & ~(1 << bit))
#define SET_BIT(wrd, bit) (wrd | 1 << bit)

int qat_hal_set_ae_ctx_mode(struct icp_qat_fw_loader_handle *handle,
			    unsigned char ae, unsigned char mode)
{
	unsigned int csr, new_csr;

	if ((mode != 4) && (mode != 8)) {
		pr_err("QAT: bad ctx mode=%d\n", mode);
		return -EINVAL;
	}

	/* Sets the accelaration engine context mode to either four or eight */
	qat_hal_rd_ae_csr(handle, ae, CTX_ENABLES, &csr);
	csr = IGNORE_W1C_MASK & csr;
	new_csr = (mode == 4) ?
		SET_BIT(csr, CE_INUSE_CONTEXTS_BITPOS) :
		CLR_BIT(csr, CE_INUSE_CONTEXTS_BITPOS);
	qat_hal_wr_ae_csr(handle, ae, CTX_ENABLES, new_csr);
	return 0;
}

int qat_hal_set_ae_nn_mode(struct icp_qat_fw_loader_handle *handle,
			   unsigned char ae, unsigned char mode)
{
	unsigned int csr, new_csr;

	qat_hal_rd_ae_csr(handle, ae, CTX_ENABLES, &csr);
	csr &= IGNORE_W1C_MASK;

	new_csr = (mode) ?
		SET_BIT(csr, CE_NN_MODE_BITPOS) :
		CLR_BIT(csr, CE_NN_MODE_BITPOS);

	if (new_csr != csr)
		qat_hal_wr_ae_csr(handle, ae, CTX_ENABLES, new_csr);

	return 0;
}

int qat_hal_set_ae_lm_mode(struct icp_qat_fw_loader_handle *handle,
			   unsigned char ae, enum icp_qat_uof_regtype lm_type,
			   unsigned char mode)
{
	unsigned int csr, new_csr;

	qat_hal_rd_ae_csr(handle, ae, CTX_ENABLES, &csr);
	csr &= IGNORE_W1C_MASK;
	switch (lm_type) {
	case ICP_LMEM0:
		new_csr = (mode) ?
			SET_BIT(csr, CE_LMADDR_0_GLOBAL_BITPOS) :
			CLR_BIT(csr, CE_LMADDR_0_GLOBAL_BITPOS);
		break;
	case ICP_LMEM1:
		new_csr = (mode) ?
			SET_BIT(csr, CE_LMADDR_1_GLOBAL_BITPOS) :
			CLR_BIT(csr, CE_LMADDR_1_GLOBAL_BITPOS);
		break;
	default:
		pr_err("QAT: lmType = 0x%x\n", lm_type);
		return -EINVAL;
	}

	if (new_csr != csr)
		qat_hal_wr_ae_csr(handle, ae, CTX_ENABLES, new_csr);
	return 0;
}

static unsigned short qat_hal_get_reg_addr(unsigned int type,
					   unsigned short reg_num)
{
	unsigned short reg_addr;

	switch (type) {
	case ICP_GPA_ABS:
	case ICP_GPB_ABS:
		reg_addr = 0x80 | (reg_num & 0x7f);
		break;
	case ICP_GPA_REL:
	case ICP_GPB_REL:
		reg_addr = reg_num & 0x1f;
		break;
	case ICP_SR_RD_REL:
	case ICP_SR_WR_REL:
	case ICP_SR_REL:
		reg_addr = 0x180 | (reg_num & 0x1f);
		break;
	case ICP_SR_ABS:
		reg_addr = 0x140 | ((reg_num & 0x3) << 1);
		break;
	case ICP_DR_RD_REL:
	case ICP_DR_WR_REL:
	case ICP_DR_REL:
		reg_addr = 0x1c0 | (reg_num & 0x1f);
		break;
	case ICP_DR_ABS:
		reg_addr = 0x100 | ((reg_num & 0x3) << 1);
		break;
	case ICP_NEIGH_REL:
		reg_addr = 0x280 | (reg_num & 0x1f);
		break;
	case ICP_LMEM0:
		reg_addr = 0x200;
		break;
	case ICP_LMEM1:
		reg_addr = 0x220;
		break;
	case ICP_NO_DEST:
		reg_addr = 0x300 | (reg_num & 0xff);
		break;
	default:
		reg_addr = BAD_REGADDR;
		break;
	}
	return reg_addr;
}

void qat_hal_reset(struct icp_qat_fw_loader_handle *handle)
{
	unsigned int ae_reset_csr;

	ae_reset_csr = GET_GLB_CSR(handle, ICP_RESET);
	ae_reset_csr |= handle->hal_handle->ae_mask << RST_CSR_AE_LSB;
	ae_reset_csr |= handle->hal_handle->slice_mask << RST_CSR_QAT_LSB;
	SET_GLB_CSR(handle, ICP_RESET, ae_reset_csr);
}

static void qat_hal_wr_indr_csr(struct icp_qat_fw_loader_handle *handle,
				unsigned char ae, unsigned int ctx_mask,
				unsigned int ae_csr, unsigned int csr_val)
{
	unsigned int ctx, cur_ctx;

	qat_hal_rd_ae_csr(handle, ae, CSR_CTX_POINTER, &cur_ctx);

	for (ctx = 0; ctx < ICP_QAT_UCLO_MAX_CTX; ctx++) {
		if (!(ctx_mask & (1 << ctx)))
			continue;
		qat_hal_wr_ae_csr(handle, ae, CSR_CTX_POINTER, ctx);
		qat_hal_wr_ae_csr(handle, ae, ae_csr, csr_val);
	}

	qat_hal_wr_ae_csr(handle, ae, CSR_CTX_POINTER, cur_ctx);
}

static void qat_hal_rd_indr_csr(struct icp_qat_fw_loader_handle *handle,
				unsigned char ae, unsigned char ctx,
				unsigned int ae_csr, unsigned int *csr_val)
{
	unsigned int cur_ctx;

	qat_hal_rd_ae_csr(handle, ae, CSR_CTX_POINTER, &cur_ctx);
	qat_hal_wr_ae_csr(handle, ae, CSR_CTX_POINTER, ctx);
	qat_hal_rd_ae_csr(handle, ae, ae_csr, csr_val);
	qat_hal_wr_ae_csr(handle, ae, CSR_CTX_POINTER, cur_ctx);
}

static void qat_hal_put_sig_event(struct icp_qat_fw_loader_handle *handle,
				  unsigned char ae, unsigned int ctx_mask,
				  unsigned int events)
{
	unsigned int ctx, cur_ctx;

	qat_hal_rd_ae_csr(handle, ae, CSR_CTX_POINTER, &cur_ctx);
	for (ctx = 0; ctx < ICP_QAT_UCLO_MAX_CTX; ctx++) {
		if (!(ctx_mask & (1 << ctx)))
			continue;
		qat_hal_wr_ae_csr(handle, ae, CSR_CTX_POINTER, ctx);
		qat_hal_wr_ae_csr(handle, ae, CTX_SIG_EVENTS_INDIRECT, events);
	}
	qat_hal_wr_ae_csr(handle, ae, CSR_CTX_POINTER, cur_ctx);
}

static void qat_hal_put_wakeup_event(struct icp_qat_fw_loader_handle *handle,
				     unsigned char ae, unsigned int ctx_mask,
				     unsigned int events)
{
	unsigned int ctx, cur_ctx;

	qat_hal_rd_ae_csr(handle, ae, CSR_CTX_POINTER, &cur_ctx);
	for (ctx = 0; ctx < ICP_QAT_UCLO_MAX_CTX; ctx++) {
		if (!(ctx_mask & (1 << ctx)))
			continue;
		qat_hal_wr_ae_csr(handle, ae, CSR_CTX_POINTER, ctx);
		qat_hal_wr_ae_csr(handle, ae, CTX_WAKEUP_EVENTS_INDIRECT,
				  events);
	}
	qat_hal_wr_ae_csr(handle, ae, CSR_CTX_POINTER, cur_ctx);
}

static int qat_hal_check_ae_alive(struct icp_qat_fw_loader_handle *handle)
{
	unsigned int base_cnt, cur_cnt;
	unsigned char ae;
	unsigned int times = MAX_RETRY_TIMES;

	for (ae = 0; ae < handle->hal_handle->ae_max_num; ae++) {
		if (!(handle->hal_handle->ae_mask & (1 << ae)))
			continue;

		qat_hal_rd_ae_csr(handle, ae, PROFILE_COUNT,
				  (unsigned int *)&base_cnt);
		base_cnt &= 0xffff;

		do {
			qat_hal_rd_ae_csr(handle, ae, PROFILE_COUNT,
					  (unsigned int *)&cur_cnt);
			cur_cnt &= 0xffff;
		} while (times-- && (cur_cnt == base_cnt));

		if (!times) {
			pr_err("QAT: AE%d is inactive!!\n", ae);
			return -EFAULT;
		}
	}

	return 0;
}

static void qat_hal_reset_timestamp(struct icp_qat_fw_loader_handle *handle)
{
	unsigned int misc_ctl;
	unsigned char ae;

	/* stop the timestamp timers */
	misc_ctl = GET_GLB_CSR(handle, MISC_CONTROL);
	if (misc_ctl & MC_TIMESTAMP_ENABLE)
		SET_GLB_CSR(handle, MISC_CONTROL, misc_ctl &
			    (~MC_TIMESTAMP_ENABLE));

	for (ae = 0; ae < handle->hal_handle->ae_max_num; ae++) {
		if (!(handle->hal_handle->ae_mask & (1 << ae)))
			continue;
		qat_hal_wr_ae_csr(handle, ae, TIMESTAMP_LOW, 0);
		qat_hal_wr_ae_csr(handle, ae, TIMESTAMP_HIGH, 0);
	}
	/* start timestamp timers */
	SET_GLB_CSR(handle, MISC_CONTROL, misc_ctl | MC_TIMESTAMP_ENABLE);
}

#define ESRAM_AUTO_TINIT (1<<2)
#define ESRAM_AUTO_TINIT_DONE (1<<3)
#define ESRAM_AUTO_INIT_USED_CYCLES (1640)
#define ESRAM_AUTO_INIT_CSR_OFFSET 0xC1C
static int qat_hal_init_esram(struct icp_qat_fw_loader_handle *handle)
{
	void __iomem *csr_addr = handle->hal_ep_csr_addr_v +
				 ESRAM_AUTO_INIT_CSR_OFFSET;
	unsigned int csr_val, times = 30;

	csr_val = ADF_CSR_RD(csr_addr, 0);
	if ((csr_val & ESRAM_AUTO_TINIT) && (csr_val & ESRAM_AUTO_TINIT_DONE))
		return 0;

	csr_val = ADF_CSR_RD(csr_addr, 0);
	csr_val |= ESRAM_AUTO_TINIT;
	ADF_CSR_WR(csr_addr, 0, csr_val);

	do {
		qat_hal_wait_cycles(handle, 0, ESRAM_AUTO_INIT_USED_CYCLES, 0);
		csr_val = ADF_CSR_RD(csr_addr, 0);
	} while (!(csr_val & ESRAM_AUTO_TINIT_DONE) && times--);
	if ((!times)) {
		pr_err("QAT: Fail to init eSram!\n");
		return -EFAULT;
	}
	return 0;
}

#define SHRAM_INIT_CYCLES 2060
int qat_hal_clr_reset(struct icp_qat_fw_loader_handle *handle)
{
	unsigned int ae_reset_csr;
	unsigned char ae;
	unsigned int clk_csr;
	unsigned int times = 100;
	unsigned int csr;

	/* write to the reset csr */
	ae_reset_csr = GET_GLB_CSR(handle, ICP_RESET);
	ae_reset_csr &= ~(handle->hal_handle->ae_mask << RST_CSR_AE_LSB);
	ae_reset_csr &= ~(handle->hal_handle->slice_mask << RST_CSR_QAT_LSB);
	do {
		SET_GLB_CSR(handle, ICP_RESET, ae_reset_csr);
		if (!(times--))
			goto out_err;
		csr = GET_GLB_CSR(handle, ICP_RESET);
	} while ((handle->hal_handle->ae_mask |
		 (handle->hal_handle->slice_mask << RST_CSR_QAT_LSB)) & csr);
	/* enable clock */
	clk_csr = GET_GLB_CSR(handle, ICP_GLOBAL_CLK_ENABLE);
	clk_csr |= handle->hal_handle->ae_mask << 0;
	clk_csr |= handle->hal_handle->slice_mask << 20;
	SET_GLB_CSR(handle, ICP_GLOBAL_CLK_ENABLE, clk_csr);
	if (qat_hal_check_ae_alive(handle))
		goto out_err;

	/* Set undefined power-up/reset states to reasonable default values */
	for (ae = 0; ae < handle->hal_handle->ae_max_num; ae++) {
		if (!(handle->hal_handle->ae_mask & (1 << ae)))
			continue;
		qat_hal_wr_ae_csr(handle, ae, CTX_ENABLES,
				  INIT_CTX_ENABLE_VALUE);
		qat_hal_wr_indr_csr(handle, ae, ICP_QAT_UCLO_AE_ALL_CTX,
				    CTX_STS_INDIRECT,
				    handle->hal_handle->upc_mask &
				    INIT_PC_VALUE);
		qat_hal_wr_ae_csr(handle, ae, CTX_ARB_CNTL, INIT_CTX_ARB_VALUE);
		qat_hal_wr_ae_csr(handle, ae, CC_ENABLE, INIT_CCENABLE_VALUE);
		qat_hal_put_wakeup_event(handle, ae,
					 ICP_QAT_UCLO_AE_ALL_CTX,
					 INIT_WAKEUP_EVENTS_VALUE);
		qat_hal_put_sig_event(handle, ae,
				      ICP_QAT_UCLO_AE_ALL_CTX,
				      INIT_SIG_EVENTS_VALUE);
	}
	if (qat_hal_init_esram(handle))
		goto out_err;
	if (qat_hal_wait_cycles(handle, 0, SHRAM_INIT_CYCLES, 0))
		goto out_err;
	qat_hal_reset_timestamp(handle);

	return 0;
out_err:
	pr_err("QAT: failed to get device out of reset\n");
	return -EFAULT;
}

static void qat_hal_disable_ctx(struct icp_qat_fw_loader_handle *handle,
				unsigned char ae, unsigned int ctx_mask)
{
	unsigned int ctx;

	qat_hal_rd_ae_csr(handle, ae, CTX_ENABLES, &ctx);
	ctx &= IGNORE_W1C_MASK &
		(~((ctx_mask & ICP_QAT_UCLO_AE_ALL_CTX) << CE_ENABLE_BITPOS));
	qat_hal_wr_ae_csr(handle, ae, CTX_ENABLES, ctx);
}

static uint64_t qat_hal_parity_64bit(uint64_t word)
{
	word ^= word >> 1;
	word ^= word >> 2;
	word ^= word >> 4;
	word ^= word >> 8;
	word ^= word >> 16;
	word ^= word >> 32;
	return word & 1;
}

static uint64_t qat_hal_set_uword_ecc(uint64_t uword)
{
	uint64_t bit0_mask = 0xff800007fffULL, bit1_mask = 0x1f801ff801fULL,
		bit2_mask = 0xe387e0781e1ULL, bit3_mask = 0x7cb8e388e22ULL,
		bit4_mask = 0xaf5b2c93244ULL, bit5_mask = 0xf56d5525488ULL,
		bit6_mask = 0xdaf69a46910ULL;

	/* clear the ecc bits */
	uword &= ~(0x7fULL << 0x2C);
	uword |= qat_hal_parity_64bit(bit0_mask & uword) << 0x2C;
	uword |= qat_hal_parity_64bit(bit1_mask & uword) << 0x2D;
	uword |= qat_hal_parity_64bit(bit2_mask & uword) << 0x2E;
	uword |= qat_hal_parity_64bit(bit3_mask & uword) << 0x2F;
	uword |= qat_hal_parity_64bit(bit4_mask & uword) << 0x30;
	uword |= qat_hal_parity_64bit(bit5_mask & uword) << 0x31;
	uword |= qat_hal_parity_64bit(bit6_mask & uword) << 0x32;
	return uword;
}

void qat_hal_wr_uwords(struct icp_qat_fw_loader_handle *handle,
		       unsigned char ae, unsigned int uaddr,
		       unsigned int words_num, uint64_t *uword)
{
	unsigned int ustore_addr;
	unsigned int i;

	qat_hal_rd_ae_csr(handle, ae, USTORE_ADDRESS, &ustore_addr);
	uaddr |= UA_ECS;
	qat_hal_wr_ae_csr(handle, ae, USTORE_ADDRESS, uaddr);
	for (i = 0; i < words_num; i++) {
		unsigned int uwrd_lo, uwrd_hi;
		uint64_t tmp;

		tmp = qat_hal_set_uword_ecc(uword[i]);
		uwrd_lo = (unsigned int)(tmp & 0xffffffff);
		uwrd_hi = (unsigned int)(tmp >> 0x20);
		qat_hal_wr_ae_csr(handle, ae, USTORE_DATA_LOWER, uwrd_lo);
		qat_hal_wr_ae_csr(handle, ae, USTORE_DATA_UPPER, uwrd_hi);
	}
	qat_hal_wr_ae_csr(handle, ae, USTORE_ADDRESS, ustore_addr);
}

static void qat_hal_enable_ctx(struct icp_qat_fw_loader_handle *handle,
			       unsigned char ae, unsigned int ctx_mask)
{
	unsigned int ctx;

	qat_hal_rd_ae_csr(handle, ae, CTX_ENABLES, &ctx);
	ctx &= IGNORE_W1C_MASK;
	ctx_mask &= (ctx & CE_INUSE_CONTEXTS) ? 0x55 : 0xFF;
	ctx |= (ctx_mask << CE_ENABLE_BITPOS);
	qat_hal_wr_ae_csr(handle, ae, CTX_ENABLES, ctx);
}

static int qat_hal_clear_gpr(struct icp_qat_fw_loader_handle *handle)
{
	unsigned char ae;
	unsigned int ctx_mask = ICP_QAT_UCLO_AE_ALL_CTX;
	int times = MAX_RETRY_TIMES;
	unsigned int csr_val = 0;
	unsigned short reg;
	unsigned int savctx = 0;
	int ret = 0;

	for (ae = 0; ae < handle->hal_handle->ae_max_num; ae++) {
		if (!(handle->hal_handle->ae_mask & (1 << ae)))
			continue;
		for (reg = 0; reg < ICP_QAT_UCLO_MAX_GPR_REG; reg++) {
			qat_hal_init_rd_xfer(handle, ae, 0, ICP_SR_RD_ABS,
					     reg, 0);
			qat_hal_init_rd_xfer(handle, ae, 0, ICP_DR_RD_ABS,
					     reg, 0);
		}
		qat_hal_rd_ae_csr(handle, ae, AE_MISC_CONTROL, &csr_val);
		csr_val &= ~(1 << MMC_SHARE_CS_BITPOS);
		qat_hal_wr_ae_csr(handle, ae, AE_MISC_CONTROL, csr_val);
		qat_hal_rd_ae_csr(handle, ae, CTX_ENABLES, &csr_val);
		csr_val &= IGNORE_W1C_MASK;
		csr_val |= CE_NN_MODE;
		qat_hal_wr_ae_csr(handle, ae, CTX_ENABLES, csr_val);
		qat_hal_wr_uwords(handle, ae, 0, ARRAY_SIZE(inst),
				  (uint64_t *)inst);
		qat_hal_wr_indr_csr(handle, ae, ctx_mask, CTX_STS_INDIRECT,
				    handle->hal_handle->upc_mask &
				    INIT_PC_VALUE);
		qat_hal_rd_ae_csr(handle, ae, ACTIVE_CTX_STATUS, &savctx);
		qat_hal_wr_ae_csr(handle, ae, ACTIVE_CTX_STATUS, 0);
		qat_hal_put_wakeup_event(handle, ae, ctx_mask, XCWE_VOLUNTARY);
		qat_hal_wr_indr_csr(handle, ae, ctx_mask,
				    CTX_SIG_EVENTS_INDIRECT, 0);
		qat_hal_wr_ae_csr(handle, ae, CTX_SIG_EVENTS_ACTIVE, 0);
		qat_hal_enable_ctx(handle, ae, ctx_mask);
	}
	for (ae = 0; ae < handle->hal_handle->ae_max_num; ae++) {
		if (!(handle->hal_handle->ae_mask & (1 << ae)))
			continue;
		/* wait for AE to finish */
		do {
			ret = qat_hal_wait_cycles(handle, ae, 20, 1);
		} while (ret && times--);

		if (!times) {
			pr_err("QAT: clear GPR of AE %d failed", ae);
			return -EINVAL;
		}
		qat_hal_disable_ctx(handle, ae, ctx_mask);
		qat_hal_wr_ae_csr(handle, ae, ACTIVE_CTX_STATUS,
				  savctx & ACS_ACNO);
		qat_hal_wr_ae_csr(handle, ae, CTX_ENABLES,
				  INIT_CTX_ENABLE_VALUE);
		qat_hal_wr_indr_csr(handle, ae, ctx_mask, CTX_STS_INDIRECT,
				    handle->hal_handle->upc_mask &
				    INIT_PC_VALUE);
		qat_hal_wr_ae_csr(handle, ae, CTX_ARB_CNTL, INIT_CTX_ARB_VALUE);
		qat_hal_wr_ae_csr(handle, ae, CC_ENABLE, INIT_CCENABLE_VALUE);
		qat_hal_put_wakeup_event(handle, ae, ctx_mask,
					 INIT_WAKEUP_EVENTS_VALUE);
		qat_hal_put_sig_event(handle, ae, ctx_mask,
				      INIT_SIG_EVENTS_VALUE);
	}
	return 0;
}

#define ICP_DH895XCC_AE_OFFSET      0x20000
#define ICP_DH895XCC_CAP_OFFSET     (ICP_DH895XCC_AE_OFFSET + 0x10000)
#define LOCAL_TO_XFER_REG_OFFSET    0x800
#define ICP_DH895XCC_EP_OFFSET      0x3a000
#define ICP_DH895XCC_PMISC_BAR 1
int qat_hal_init(struct adf_accel_dev *accel_dev)
{
	unsigned char ae;
	unsigned int max_en_ae_id = 0;
	struct icp_qat_fw_loader_handle *handle;
	struct adf_accel_pci *pci_info = &accel_dev->accel_pci_dev;
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	struct adf_bar *bar = &pci_info->pci_bars[ADF_DH895XCC_PMISC_BAR];

	handle = kzalloc(sizeof(*handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	handle->hal_cap_g_ctl_csr_addr_v = bar->virt_addr +
						ICP_DH895XCC_CAP_OFFSET;
	handle->hal_cap_ae_xfer_csr_addr_v = bar->virt_addr +
						ICP_DH895XCC_AE_OFFSET;
	handle->hal_ep_csr_addr_v = bar->virt_addr + ICP_DH895XCC_EP_OFFSET;
	handle->hal_cap_ae_local_csr_addr_v =
		handle->hal_cap_ae_xfer_csr_addr_v + LOCAL_TO_XFER_REG_OFFSET;

	handle->hal_handle = kzalloc(sizeof(*handle->hal_handle), GFP_KERNEL);
	if (!handle->hal_handle)
		goto out_hal_handle;
	handle->hal_handle->revision_id = accel_dev->accel_pci_dev.revid;
	handle->hal_handle->ae_mask = hw_data->ae_mask;
	handle->hal_handle->slice_mask = hw_data->accel_mask;
	/* create AE objects */
	handle->hal_handle->upc_mask = 0x1ffff;
	handle->hal_handle->max_ustore = 0x4000;
	for (ae = 0; ae < ICP_QAT_UCLO_MAX_AE; ae++) {
		if (!(hw_data->ae_mask & (1 << ae)))
			continue;
		handle->hal_handle->aes[ae].free_addr = 0;
		handle->hal_handle->aes[ae].free_size =
		    handle->hal_handle->max_ustore;
		handle->hal_handle->aes[ae].ustore_size =
		    handle->hal_handle->max_ustore;
		handle->hal_handle->aes[ae].live_ctx_mask =
						ICP_QAT_UCLO_AE_ALL_CTX;
		max_en_ae_id = ae;
	}
	handle->hal_handle->ae_max_num = max_en_ae_id + 1;
	/* take all AEs out of reset */
	if (qat_hal_clr_reset(handle)) {
		pr_err("QAT: qat_hal_clr_reset error\n");
		goto out_err;
	}
	if (qat_hal_clear_gpr(handle))
		goto out_err;
	/* Set SIGNATURE_ENABLE[0] to 0x1 in order to enable ALU_OUT csr */
	for (ae = 0; ae < handle->hal_handle->ae_max_num; ae++) {
		unsigned int csr_val = 0;

		if (!(hw_data->ae_mask & (1 << ae)))
			continue;
		qat_hal_rd_ae_csr(handle, ae, SIGNATURE_ENABLE, &csr_val);
		csr_val |= 0x1;
		qat_hal_wr_ae_csr(handle, ae, SIGNATURE_ENABLE, csr_val);
	}
	accel_dev->fw_loader->fw_loader = handle;
	return 0;

out_err:
	kfree(handle->hal_handle);
out_hal_handle:
	kfree(handle);
	return -EFAULT;
}

void qat_hal_deinit(struct icp_qat_fw_loader_handle *handle)
{
	if (!handle)
		return;
	kfree(handle->hal_handle);
	kfree(handle);
}

void qat_hal_start(struct icp_qat_fw_loader_handle *handle, unsigned char ae,
		   unsigned int ctx_mask)
{
	qat_hal_put_wakeup_event(handle, ae, (~ctx_mask) &
				 ICP_QAT_UCLO_AE_ALL_CTX, 0x10000);
	qat_hal_enable_ctx(handle, ae, ctx_mask);
}

void qat_hal_stop(struct icp_qat_fw_loader_handle *handle, unsigned char ae,
		  unsigned int ctx_mask)
{
	qat_hal_disable_ctx(handle, ae, ctx_mask);
}

void qat_hal_set_pc(struct icp_qat_fw_loader_handle *handle,
		    unsigned char ae, unsigned int ctx_mask, unsigned int upc)
{
	qat_hal_wr_indr_csr(handle, ae, ctx_mask, CTX_STS_INDIRECT,
			    handle->hal_handle->upc_mask & upc);
}

static void qat_hal_get_uwords(struct icp_qat_fw_loader_handle *handle,
			       unsigned char ae, unsigned int uaddr,
			       unsigned int words_num, uint64_t *uword)
{
	unsigned int i, uwrd_lo, uwrd_hi;
	unsigned int ustore_addr, misc_control;

	qat_hal_rd_ae_csr(handle, ae, AE_MISC_CONTROL, &misc_control);
	qat_hal_wr_ae_csr(handle, ae, AE_MISC_CONTROL,
			  misc_control & 0xfffffffb);
	qat_hal_rd_ae_csr(handle, ae, USTORE_ADDRESS, &ustore_addr);
	uaddr |= UA_ECS;
	for (i = 0; i < words_num; i++) {
		qat_hal_wr_ae_csr(handle, ae, USTORE_ADDRESS, uaddr);
		uaddr++;
		qat_hal_rd_ae_csr(handle, ae, USTORE_DATA_LOWER, &uwrd_lo);
		qat_hal_rd_ae_csr(handle, ae, USTORE_DATA_UPPER, &uwrd_hi);
		uword[i] = uwrd_hi;
		uword[i] = (uword[i] << 0x20) | uwrd_lo;
	}
	qat_hal_wr_ae_csr(handle, ae, AE_MISC_CONTROL, misc_control);
	qat_hal_wr_ae_csr(handle, ae, USTORE_ADDRESS, ustore_addr);
}

static int qat_hal_count_bits(unsigned int word)
{
	int n = 0;

	while (word) {
		n++;
		word &= word - 1;
	}
	return n;
}

void qat_hal_wr_umem(struct icp_qat_fw_loader_handle *handle,
		     unsigned char ae, unsigned int uaddr,
		     unsigned int words_num, unsigned int *data)
{
	unsigned int i, ustore_addr;

	qat_hal_rd_ae_csr(handle, ae, USTORE_ADDRESS, &ustore_addr);
	uaddr |= UA_ECS;
	qat_hal_wr_ae_csr(handle, ae, USTORE_ADDRESS, uaddr);
	for (i = 0; i < words_num; i++) {
		unsigned int uwrd_lo, uwrd_hi, tmp;

		uwrd_lo = ((data[i] & 0xfff0000) << 4) | (0x3 << 18) |
			  ((data[i] & 0xff00) << 2) |
			  (0x3 << 8) | (data[i] & 0xff);
		uwrd_hi = (0xf << 4) | ((data[i] & 0xf0000000) >> 28);
		uwrd_hi |= (qat_hal_count_bits(data[i] & 0xffff) & 0x1) << 8;
		tmp = ((data[i] >> 0x10) & 0xffff);
		uwrd_hi |= (qat_hal_count_bits(tmp) & 0x1) << 9;
		qat_hal_wr_ae_csr(handle, ae, USTORE_DATA_LOWER, uwrd_lo);
		qat_hal_wr_ae_csr(handle, ae, USTORE_DATA_UPPER, uwrd_hi);
	}
	qat_hal_wr_ae_csr(handle, ae, USTORE_ADDRESS, ustore_addr);
}

#define MAX_EXEC_INST 100
static int qat_hal_exec_micro_inst(struct icp_qat_fw_loader_handle *handle,
				   unsigned char ae, unsigned char ctx,
				   uint64_t *micro_inst, unsigned int inst_num,
				   int code_off, unsigned int max_cycle,
				   unsigned int *endpc)
{
	uint64_t savuwords[MAX_EXEC_INST];
	unsigned int ind_lm_addr0, ind_lm_addr1;
	unsigned int ind_lm_addr_byte0, ind_lm_addr_byte1;
	unsigned int ind_cnt_sig;
	unsigned int ind_sig, act_sig;
	unsigned int csr_val = 0, newcsr_val;
	unsigned int savctx;
	unsigned int savcc, wakeup_events, savpc;
	unsigned int ctxarb_ctl, ctx_enables;

	if ((inst_num > handle->hal_handle->max_ustore) || !micro_inst) {
		pr_err("QAT: invalid instruction num %d\n", inst_num);
		return -EINVAL;
	}
	/* save current context */
	qat_hal_rd_indr_csr(handle, ae, ctx, LM_ADDR_0_INDIRECT, &ind_lm_addr0);
	qat_hal_rd_indr_csr(handle, ae, ctx, LM_ADDR_1_INDIRECT, &ind_lm_addr1);
	qat_hal_rd_indr_csr(handle, ae, ctx, INDIRECT_LM_ADDR_0_BYTE_INDEX,
			    &ind_lm_addr_byte0);
	qat_hal_rd_indr_csr(handle, ae, ctx, INDIRECT_LM_ADDR_1_BYTE_INDEX,
			    &ind_lm_addr_byte1);
	if (inst_num <= MAX_EXEC_INST)
		qat_hal_get_uwords(handle, ae, 0, inst_num, savuwords);
	qat_hal_get_wakeup_event(handle, ae, ctx, &wakeup_events);
	qat_hal_rd_indr_csr(handle, ae, ctx, CTX_STS_INDIRECT, &savpc);
	savpc = (savpc & handle->hal_handle->upc_mask) >> 0;
	qat_hal_rd_ae_csr(handle, ae, CTX_ENABLES, &ctx_enables);
	ctx_enables &= IGNORE_W1C_MASK;
	qat_hal_rd_ae_csr(handle, ae, CC_ENABLE, &savcc);
	qat_hal_rd_ae_csr(handle, ae, ACTIVE_CTX_STATUS, &savctx);
	qat_hal_rd_ae_csr(handle, ae, CTX_ARB_CNTL, &ctxarb_ctl);
	qat_hal_rd_indr_csr(handle, ae, ctx, FUTURE_COUNT_SIGNAL_INDIRECT,
			    &ind_cnt_sig);
	qat_hal_rd_indr_csr(handle, ae, ctx, CTX_SIG_EVENTS_INDIRECT, &ind_sig);
	qat_hal_rd_ae_csr(handle, ae, CTX_SIG_EVENTS_ACTIVE, &act_sig);
	/* execute micro codes */
	qat_hal_wr_ae_csr(handle, ae, CTX_ENABLES, ctx_enables);
	qat_hal_wr_uwords(handle, ae, 0, inst_num, micro_inst);
	qat_hal_wr_indr_csr(handle, ae, (1 << ctx), CTX_STS_INDIRECT, 0);
	qat_hal_wr_ae_csr(handle, ae, ACTIVE_CTX_STATUS, ctx & ACS_ACNO);
	if (code_off)
		qat_hal_wr_ae_csr(handle, ae, CC_ENABLE, savcc & 0xffffdfff);
	qat_hal_put_wakeup_event(handle, ae, (1 << ctx), XCWE_VOLUNTARY);
	qat_hal_wr_indr_csr(handle, ae, (1 << ctx), CTX_SIG_EVENTS_INDIRECT, 0);
	qat_hal_wr_ae_csr(handle, ae, CTX_SIG_EVENTS_ACTIVE, 0);
	qat_hal_enable_ctx(handle, ae, (1 << ctx));
	/* wait for micro codes to finish */
	if (qat_hal_wait_cycles(handle, ae, max_cycle, 1) != 0)
		return -EFAULT;
	if (endpc) {
		unsigned int ctx_status;

		qat_hal_rd_indr_csr(handle, ae, ctx, CTX_STS_INDIRECT,
				    &ctx_status);
		*endpc = ctx_status & handle->hal_handle->upc_mask;
	}
	/* retore to saved context */
	qat_hal_disable_ctx(handle, ae, (1 << ctx));
	if (inst_num <= MAX_EXEC_INST)
		qat_hal_wr_uwords(handle, ae, 0, inst_num, savuwords);
	qat_hal_put_wakeup_event(handle, ae, (1 << ctx), wakeup_events);
	qat_hal_wr_indr_csr(handle, ae, (1 << ctx), CTX_STS_INDIRECT,
			    handle->hal_handle->upc_mask & savpc);
	qat_hal_rd_ae_csr(handle, ae, AE_MISC_CONTROL, &csr_val);
	newcsr_val = CLR_BIT(csr_val, MMC_SHARE_CS_BITPOS);
	qat_hal_wr_ae_csr(handle, ae, AE_MISC_CONTROL, newcsr_val);
	qat_hal_wr_ae_csr(handle, ae, CC_ENABLE, savcc);
	qat_hal_wr_ae_csr(handle, ae, ACTIVE_CTX_STATUS, savctx & ACS_ACNO);
	qat_hal_wr_ae_csr(handle, ae, CTX_ARB_CNTL, ctxarb_ctl);
	qat_hal_wr_indr_csr(handle, ae, (1 << ctx),
			    LM_ADDR_0_INDIRECT, ind_lm_addr0);
	qat_hal_wr_indr_csr(handle, ae, (1 << ctx),
			    LM_ADDR_1_INDIRECT, ind_lm_addr1);
	qat_hal_wr_indr_csr(handle, ae, (1 << ctx),
			    INDIRECT_LM_ADDR_0_BYTE_INDEX, ind_lm_addr_byte0);
	qat_hal_wr_indr_csr(handle, ae, (1 << ctx),
			    INDIRECT_LM_ADDR_1_BYTE_INDEX, ind_lm_addr_byte1);
	qat_hal_wr_indr_csr(handle, ae, (1 << ctx),
			    FUTURE_COUNT_SIGNAL_INDIRECT, ind_cnt_sig);
	qat_hal_wr_indr_csr(handle, ae, (1 << ctx),
			    CTX_SIG_EVENTS_INDIRECT, ind_sig);
	qat_hal_wr_ae_csr(handle, ae, CTX_SIG_EVENTS_ACTIVE, act_sig);
	qat_hal_wr_ae_csr(handle, ae, CTX_ENABLES, ctx_enables);

	return 0;
}

static int qat_hal_rd_rel_reg(struct icp_qat_fw_loader_handle *handle,
			      unsigned char ae, unsigned char ctx,
			      enum icp_qat_uof_regtype reg_type,
			      unsigned short reg_num, unsigned int *data)
{
	unsigned int savctx, uaddr, uwrd_lo, uwrd_hi;
	unsigned int ctxarb_cntl, ustore_addr, ctx_enables;
	unsigned short reg_addr;
	int status = 0;
	uint64_t insts, savuword;

	reg_addr = qat_hal_get_reg_addr(reg_type, reg_num);
	if (reg_addr == BAD_REGADDR) {
		pr_err("QAT: bad regaddr=0x%x\n", reg_addr);
		return -EINVAL;
	}
	switch (reg_type) {
	case ICP_GPA_REL:
		insts = 0xA070000000ull | (reg_addr & 0x3ff);
		break;
	default:
		insts = (uint64_t)0xA030000000ull | ((reg_addr & 0x3ff) << 10);
		break;
	}
	qat_hal_rd_ae_csr(handle, ae, ACTIVE_CTX_STATUS, &savctx);
	qat_hal_rd_ae_csr(handle, ae, CTX_ARB_CNTL, &ctxarb_cntl);
	qat_hal_rd_ae_csr(handle, ae, CTX_ENABLES, &ctx_enables);
	ctx_enables &= IGNORE_W1C_MASK;
	if (ctx != (savctx & ACS_ACNO))
		qat_hal_wr_ae_csr(handle, ae, ACTIVE_CTX_STATUS,
				  ctx & ACS_ACNO);
	qat_hal_get_uwords(handle, ae, 0, 1, &savuword);
	qat_hal_wr_ae_csr(handle, ae, CTX_ENABLES, ctx_enables);
	qat_hal_rd_ae_csr(handle, ae, USTORE_ADDRESS, &ustore_addr);
	uaddr = UA_ECS;
	qat_hal_wr_ae_csr(handle, ae, USTORE_ADDRESS, uaddr);
	insts = qat_hal_set_uword_ecc(insts);
	uwrd_lo = (unsigned int)(insts & 0xffffffff);
	uwrd_hi = (unsigned int)(insts >> 0x20);
	qat_hal_wr_ae_csr(handle, ae, USTORE_DATA_LOWER, uwrd_lo);
	qat_hal_wr_ae_csr(handle, ae, USTORE_DATA_UPPER, uwrd_hi);
	qat_hal_wr_ae_csr(handle, ae, USTORE_ADDRESS, uaddr);
	/* delay for at least 8 cycles */
	qat_hal_wait_cycles(handle, ae, 0x8, 0);
	/*
	 * read ALU output
	 * the instruction should have been executed
	 * prior to clearing the ECS in putUwords
	 */
	qat_hal_rd_ae_csr(handle, ae, ALU_OUT, data);
	qat_hal_wr_ae_csr(handle, ae, USTORE_ADDRESS, ustore_addr);
	qat_hal_wr_uwords(handle, ae, 0, 1, &savuword);
	if (ctx != (savctx & ACS_ACNO))
		qat_hal_wr_ae_csr(handle, ae, ACTIVE_CTX_STATUS,
				  savctx & ACS_ACNO);
	qat_hal_wr_ae_csr(handle, ae, CTX_ARB_CNTL, ctxarb_cntl);
	qat_hal_wr_ae_csr(handle, ae, CTX_ENABLES, ctx_enables);

	return status;
}

static int qat_hal_wr_rel_reg(struct icp_qat_fw_loader_handle *handle,
			      unsigned char ae, unsigned char ctx,
			      enum icp_qat_uof_regtype reg_type,
			      unsigned short reg_num, unsigned int data)
{
	unsigned short src_hiaddr, src_lowaddr, dest_addr, data16hi, data16lo;
	uint64_t insts[] = {
		0x0F440000000ull,
		0x0F040000000ull,
		0x0F0000C0300ull,
		0x0E000010000ull
	};
	const int num_inst = ARRAY_SIZE(insts), code_off = 1;
	const int imm_w1 = 0, imm_w0 = 1;

	dest_addr = qat_hal_get_reg_addr(reg_type, reg_num);
	if (dest_addr == BAD_REGADDR) {
		pr_err("QAT: bad destAddr=0x%x\n", dest_addr);
		return -EINVAL;
	}

	data16lo = 0xffff & data;
	data16hi = 0xffff & (data >> 0x10);
	src_hiaddr = qat_hal_get_reg_addr(ICP_NO_DEST, (unsigned short)
					  (0xff & data16hi));
	src_lowaddr = qat_hal_get_reg_addr(ICP_NO_DEST, (unsigned short)
					   (0xff & data16lo));
	switch (reg_type) {
	case ICP_GPA_REL:
		insts[imm_w1] = insts[imm_w1] | ((data16hi >> 8) << 20) |
		    ((src_hiaddr & 0x3ff) << 10) | (dest_addr & 0x3ff);
		insts[imm_w0] = insts[imm_w0] | ((data16lo >> 8) << 20) |
		    ((src_lowaddr & 0x3ff) << 10) | (dest_addr & 0x3ff);
		break;
	default:
		insts[imm_w1] = insts[imm_w1] | ((data16hi >> 8) << 20) |
		    ((dest_addr & 0x3ff) << 10) | (src_hiaddr & 0x3ff);

		insts[imm_w0] = insts[imm_w0] | ((data16lo >> 8) << 20) |
		    ((dest_addr & 0x3ff) << 10) | (src_lowaddr & 0x3ff);
		break;
	}

	return qat_hal_exec_micro_inst(handle, ae, ctx, insts, num_inst,
				       code_off, num_inst * 0x5, NULL);
}

int qat_hal_get_ins_num(void)
{
	return ARRAY_SIZE(inst_4b);
}

static int qat_hal_concat_micro_code(uint64_t *micro_inst,
				     unsigned int inst_num, unsigned int size,
				     unsigned int addr, unsigned int *value)
{
	int i, val_indx;
	unsigned int cur_value;
	const uint64_t *inst_arr;
	int fixup_offset;
	int usize = 0;
	int orig_num;

	orig_num = inst_num;
	val_indx = 0;
	cur_value = value[val_indx++];
	inst_arr = inst_4b;
	usize = ARRAY_SIZE(inst_4b);
	fixup_offset = inst_num;
	for (i = 0; i < usize; i++)
		micro_inst[inst_num++] = inst_arr[i];
	INSERT_IMMED_GPRA_CONST(micro_inst[fixup_offset], (addr));
	fixup_offset++;
	INSERT_IMMED_GPRA_CONST(micro_inst[fixup_offset], 0);
	fixup_offset++;
	INSERT_IMMED_GPRB_CONST(micro_inst[fixup_offset], (cur_value >> 0));
	fixup_offset++;
	INSERT_IMMED_GPRB_CONST(micro_inst[fixup_offset], (cur_value >> 0x10));

	return inst_num - orig_num;
}

static int qat_hal_exec_micro_init_lm(struct icp_qat_fw_loader_handle *handle,
				      unsigned char ae, unsigned char ctx,
				      int *pfirst_exec, uint64_t *micro_inst,
				      unsigned int inst_num)
{
	int stat = 0;
	unsigned int gpra0 = 0, gpra1 = 0, gpra2 = 0;
	unsigned int gprb0 = 0, gprb1 = 0;

	if (*pfirst_exec) {
		qat_hal_rd_rel_reg(handle, ae, ctx, ICP_GPA_REL, 0, &gpra0);
		qat_hal_rd_rel_reg(handle, ae, ctx, ICP_GPA_REL, 0x1, &gpra1);
		qat_hal_rd_rel_reg(handle, ae, ctx, ICP_GPA_REL, 0x2, &gpra2);
		qat_hal_rd_rel_reg(handle, ae, ctx, ICP_GPB_REL, 0, &gprb0);
		qat_hal_rd_rel_reg(handle, ae, ctx, ICP_GPB_REL, 0x1, &gprb1);
		*pfirst_exec = 0;
	}
	stat = qat_hal_exec_micro_inst(handle, ae, ctx, micro_inst, inst_num, 1,
				       inst_num * 0x5, NULL);
	if (stat != 0)
		return -EFAULT;
	qat_hal_wr_rel_reg(handle, ae, ctx, ICP_GPA_REL, 0, gpra0);
	qat_hal_wr_rel_reg(handle, ae, ctx, ICP_GPA_REL, 0x1, gpra1);
	qat_hal_wr_rel_reg(handle, ae, ctx, ICP_GPA_REL, 0x2, gpra2);
	qat_hal_wr_rel_reg(handle, ae, ctx, ICP_GPB_REL, 0, gprb0);
	qat_hal_wr_rel_reg(handle, ae, ctx, ICP_GPB_REL, 0x1, gprb1);

	return 0;
}

int qat_hal_batch_wr_lm(struct icp_qat_fw_loader_handle *handle,
			unsigned char ae,
			struct icp_qat_uof_batch_init *lm_init_header)
{
	struct icp_qat_uof_batch_init *plm_init;
	uint64_t *micro_inst_arry;
	int micro_inst_num;
	int alloc_inst_size;
	int first_exec = 1;
	int stat = 0;

	plm_init = lm_init_header->next;
	alloc_inst_size = lm_init_header->size;
	if ((unsigned int)alloc_inst_size > handle->hal_handle->max_ustore)
		alloc_inst_size = handle->hal_handle->max_ustore;
	micro_inst_arry = kmalloc_array(alloc_inst_size, sizeof(uint64_t),
					GFP_KERNEL);
	if (!micro_inst_arry)
		return -ENOMEM;
	micro_inst_num = 0;
	while (plm_init) {
		unsigned int addr, *value, size;

		ae = plm_init->ae;
		addr = plm_init->addr;
		value = plm_init->value;
		size = plm_init->size;
		micro_inst_num += qat_hal_concat_micro_code(micro_inst_arry,
							    micro_inst_num,
							    size, addr, value);
		plm_init = plm_init->next;
	}
	/* exec micro codes */
	if (micro_inst_arry && (micro_inst_num > 0)) {
		micro_inst_arry[micro_inst_num++] = 0x0E000010000ull;
		stat = qat_hal_exec_micro_init_lm(handle, ae, 0, &first_exec,
						  micro_inst_arry,
						  micro_inst_num);
	}
	kfree(micro_inst_arry);
	return stat;
}

static int qat_hal_put_rel_rd_xfer(struct icp_qat_fw_loader_handle *handle,
				   unsigned char ae, unsigned char ctx,
				   enum icp_qat_uof_regtype reg_type,
				   unsigned short reg_num, unsigned int val)
{
	int status = 0;
	unsigned int reg_addr;
	unsigned int ctx_enables;
	unsigned short mask;
	unsigned short dr_offset = 0x10;

	status = qat_hal_rd_ae_csr(handle, ae, CTX_ENABLES, &ctx_enables);
	if (CE_INUSE_CONTEXTS & ctx_enables) {
		if (ctx & 0x1) {
			pr_err("QAT: bad 4-ctx mode,ctx=0x%x\n", ctx);
			return -EINVAL;
		}
		mask = 0x1f;
		dr_offset = 0x20;
	} else {
		mask = 0x0f;
	}
	if (reg_num & ~mask)
		return -EINVAL;
	reg_addr = reg_num + (ctx << 0x5);
	switch (reg_type) {
	case ICP_SR_RD_REL:
	case ICP_SR_REL:
		SET_AE_XFER(handle, ae, reg_addr, val);
		break;
	case ICP_DR_RD_REL:
	case ICP_DR_REL:
		SET_AE_XFER(handle, ae, (reg_addr + dr_offset), val);
		break;
	default:
		status = -EINVAL;
		break;
	}
	return status;
}

static int qat_hal_put_rel_wr_xfer(struct icp_qat_fw_loader_handle *handle,
				   unsigned char ae, unsigned char ctx,
				   enum icp_qat_uof_regtype reg_type,
				   unsigned short reg_num, unsigned int data)
{
	unsigned int gprval, ctx_enables;
	unsigned short src_hiaddr, src_lowaddr, gpr_addr, xfr_addr, data16hi,
	    data16low;
	unsigned short reg_mask;
	int status = 0;
	uint64_t micro_inst[] = {
		0x0F440000000ull,
		0x0F040000000ull,
		0x0A000000000ull,
		0x0F0000C0300ull,
		0x0E000010000ull
	};
	const int num_inst = ARRAY_SIZE(micro_inst), code_off = 1;
	const unsigned short gprnum = 0, dly = num_inst * 0x5;

	qat_hal_rd_ae_csr(handle, ae, CTX_ENABLES, &ctx_enables);
	if (CE_INUSE_CONTEXTS & ctx_enables) {
		if (ctx & 0x1) {
			pr_err("QAT: 4-ctx mode,ctx=0x%x\n", ctx);
			return -EINVAL;
		}
		reg_mask = (unsigned short)~0x1f;
	} else {
		reg_mask = (unsigned short)~0xf;
	}
	if (reg_num & reg_mask)
		return -EINVAL;
	xfr_addr = qat_hal_get_reg_addr(reg_type, reg_num);
	if (xfr_addr == BAD_REGADDR) {
		pr_err("QAT: bad xfrAddr=0x%x\n", xfr_addr);
		return -EINVAL;
	}
	qat_hal_rd_rel_reg(handle, ae, ctx, ICP_GPB_REL, gprnum, &gprval);
	gpr_addr = qat_hal_get_reg_addr(ICP_GPB_REL, gprnum);
	data16low = 0xffff & data;
	data16hi = 0xffff & (data >> 0x10);
	src_hiaddr = qat_hal_get_reg_addr(ICP_NO_DEST,
					  (unsigned short)(0xff & data16hi));
	src_lowaddr = qat_hal_get_reg_addr(ICP_NO_DEST,
					   (unsigned short)(0xff & data16low));
	micro_inst[0] = micro_inst[0x0] | ((data16hi >> 8) << 20) |
	    ((gpr_addr & 0x3ff) << 10) | (src_hiaddr & 0x3ff);
	micro_inst[1] = micro_inst[0x1] | ((data16low >> 8) << 20) |
	    ((gpr_addr & 0x3ff) << 10) | (src_lowaddr & 0x3ff);
	micro_inst[0x2] = micro_inst[0x2] |
	    ((xfr_addr & 0x3ff) << 20) | ((gpr_addr & 0x3ff) << 10);
	status = qat_hal_exec_micro_inst(handle, ae, ctx, micro_inst, num_inst,
					 code_off, dly, NULL);
	qat_hal_wr_rel_reg(handle, ae, ctx, ICP_GPB_REL, gprnum, gprval);
	return status;
}

static int qat_hal_put_rel_nn(struct icp_qat_fw_loader_handle *handle,
			      unsigned char ae, unsigned char ctx,
			      unsigned short nn, unsigned int val)
{
	unsigned int ctx_enables;
	int stat = 0;

	qat_hal_rd_ae_csr(handle, ae, CTX_ENABLES, &ctx_enables);
	ctx_enables &= IGNORE_W1C_MASK;
	qat_hal_wr_ae_csr(handle, ae, CTX_ENABLES, ctx_enables | CE_NN_MODE);

	stat = qat_hal_put_rel_wr_xfer(handle, ae, ctx, ICP_NEIGH_REL, nn, val);
	qat_hal_wr_ae_csr(handle, ae, CTX_ENABLES, ctx_enables);
	return stat;
}

static int qat_hal_convert_abs_to_rel(struct icp_qat_fw_loader_handle
				      *handle, unsigned char ae,
				      unsigned short absreg_num,
				      unsigned short *relreg,
				      unsigned char *ctx)
{
	unsigned int ctx_enables;

	qat_hal_rd_ae_csr(handle, ae, CTX_ENABLES, &ctx_enables);
	if (ctx_enables & CE_INUSE_CONTEXTS) {
		/* 4-ctx mode */
		*relreg = absreg_num & 0x1F;
		*ctx = (absreg_num >> 0x4) & 0x6;
	} else {
		/* 8-ctx mode */
		*relreg = absreg_num & 0x0F;
		*ctx = (absreg_num >> 0x4) & 0x7;
	}
	return 0;
}

int qat_hal_init_gpr(struct icp_qat_fw_loader_handle *handle,
		     unsigned char ae, unsigned char ctx_mask,
		     enum icp_qat_uof_regtype reg_type,
		     unsigned short reg_num, unsigned int regdata)
{
	int stat = 0;
	unsigned short reg;
	unsigned char ctx = 0;
	enum icp_qat_uof_regtype type;

	if (reg_num >= ICP_QAT_UCLO_MAX_GPR_REG)
		return -EINVAL;

	do {
		if (ctx_mask == 0) {
			qat_hal_convert_abs_to_rel(handle, ae, reg_num, &reg,
						   &ctx);
			type = reg_type - 1;
		} else {
			reg = reg_num;
			type = reg_type;
			if (!test_bit(ctx, (unsigned long *)&ctx_mask))
				continue;
		}
		stat = qat_hal_wr_rel_reg(handle, ae, ctx, type, reg, regdata);
		if (stat) {
			pr_err("QAT: write gpr fail\n");
			return -EINVAL;
		}
	} while (ctx_mask && (ctx++ < ICP_QAT_UCLO_MAX_CTX));

	return 0;
}

int qat_hal_init_wr_xfer(struct icp_qat_fw_loader_handle *handle,
			 unsigned char ae, unsigned char ctx_mask,
			 enum icp_qat_uof_regtype reg_type,
			 unsigned short reg_num, unsigned int regdata)
{
	int stat = 0;
	unsigned short reg;
	unsigned char ctx = 0;
	enum icp_qat_uof_regtype type;

	if (reg_num >= ICP_QAT_UCLO_MAX_XFER_REG)
		return -EINVAL;

	do {
		if (ctx_mask == 0) {
			qat_hal_convert_abs_to_rel(handle, ae, reg_num, &reg,
						   &ctx);
			type = reg_type - 3;
		} else {
			reg = reg_num;
			type = reg_type;
			if (!test_bit(ctx, (unsigned long *)&ctx_mask))
				continue;
		}
		stat = qat_hal_put_rel_wr_xfer(handle, ae, ctx, type, reg,
					       regdata);
		if (stat) {
			pr_err("QAT: write wr xfer fail\n");
			return -EINVAL;
		}
	} while (ctx_mask && (ctx++ < ICP_QAT_UCLO_MAX_CTX));

	return 0;
}

int qat_hal_init_rd_xfer(struct icp_qat_fw_loader_handle *handle,
			 unsigned char ae, unsigned char ctx_mask,
			 enum icp_qat_uof_regtype reg_type,
			 unsigned short reg_num, unsigned int regdata)
{
	int stat = 0;
	unsigned short reg;
	unsigned char ctx = 0;
	enum icp_qat_uof_regtype type;

	if (reg_num >= ICP_QAT_UCLO_MAX_XFER_REG)
		return -EINVAL;

	do {
		if (ctx_mask == 0) {
			qat_hal_convert_abs_to_rel(handle, ae, reg_num, &reg,
						   &ctx);
			type = reg_type - 3;
		} else {
			reg = reg_num;
			type = reg_type;
			if (!test_bit(ctx, (unsigned long *)&ctx_mask))
				continue;
		}
		stat = qat_hal_put_rel_rd_xfer(handle, ae, ctx, type, reg,
					       regdata);
		if (stat) {
			pr_err("QAT: write rd xfer fail\n");
			return -EINVAL;
		}
	} while (ctx_mask && (ctx++ < ICP_QAT_UCLO_MAX_CTX));

	return 0;
}

int qat_hal_init_nn(struct icp_qat_fw_loader_handle *handle,
		    unsigned char ae, unsigned char ctx_mask,
		    unsigned short reg_num, unsigned int regdata)
{
	int stat = 0;
	unsigned char ctx;

	if (ctx_mask == 0)
		return -EINVAL;

	for (ctx = 0; ctx < ICP_QAT_UCLO_MAX_CTX; ctx++) {
		if (!test_bit(ctx, (unsigned long *)&ctx_mask))
			continue;
		stat = qat_hal_put_rel_nn(handle, ae, ctx, reg_num, regdata);
		if (stat) {
			pr_err("QAT: write neigh error\n");
			return -EINVAL;
		}
	}

	return 0;
}

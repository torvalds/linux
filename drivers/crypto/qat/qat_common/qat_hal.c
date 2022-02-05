// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2014 - 2020 Intel Corporation */
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/pci_ids.h>

#include "adf_accel_devices.h"
#include "adf_common_drv.h"
#include "icp_qat_hal.h"
#include "icp_qat_uclo.h"

#define BAD_REGADDR	       0xffff
#define MAX_RETRY_TIMES	   10000
#define INIT_CTX_ARB_VALUE	0x0
#define INIT_CTX_ENABLE_VALUE     0x0
#define INIT_PC_VALUE	     0x0
#define INIT_WAKEUP_EVENTS_VALUE  0x1
#define INIT_SIG_EVENTS_VALUE     0x1
#define INIT_CCENABLE_VALUE       0x2000
#define RST_CSR_QAT_LSB	   20
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

#define AE(handle, ae) ((handle)->hal_handle->aes[ae])

static const u64 inst_4b[] = {
	0x0F0400C0000ull, 0x0F4400C0000ull, 0x0F040000300ull, 0x0F440000300ull,
	0x0FC066C0000ull, 0x0F0000C0300ull, 0x0F0000C0300ull, 0x0F0000C0300ull,
	0x0A021000000ull
};

static const u64 inst[] = {
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
			     unsigned char ae, unsigned int csr)
{
	unsigned int iterations = CSR_RETRY_TIMES;
	int value;

	do {
		value = GET_AE_CSR(handle, ae, csr);
		if (!(GET_AE_CSR(handle, ae, LOCAL_CSR_STATUS) & LCS_STATUS))
			return value;
	} while (iterations--);

	pr_err("QAT: Read CSR timeout\n");
	return 0;
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

	cur_ctx = qat_hal_rd_ae_csr(handle, ae, CSR_CTX_POINTER);
	qat_hal_wr_ae_csr(handle, ae, CSR_CTX_POINTER, ctx);
	*events = qat_hal_rd_ae_csr(handle, ae, CTX_WAKEUP_EVENTS_INDIRECT);
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

	base_cnt = qat_hal_rd_ae_csr(handle, ae, PROFILE_COUNT);
	base_cnt &= 0xffff;
	while ((int)cycles > elapsed_cycles && times--) {
		if (chk_inactive)
			csr = qat_hal_rd_ae_csr(handle, ae, ACTIVE_CTX_STATUS);

		cur_cnt = qat_hal_rd_ae_csr(handle, ae, PROFILE_COUNT);
		cur_cnt &= 0xffff;
		elapsed_cycles = cur_cnt - base_cnt;

		if (elapsed_cycles < 0)
			elapsed_cycles += 0x10000;

		/* ensure at least 8 time cycles elapsed in wait_cycles */
		if (elapsed_cycles >= 8 && !(csr & (1 << ACS_ABO_BITPOS)))
			return 0;
	}
	if (times < 0) {
		pr_err("QAT: wait_num_cycles time out\n");
		return -EFAULT;
	}
	return 0;
}

#define CLR_BIT(wrd, bit) ((wrd) & ~(1 << (bit)))
#define SET_BIT(wrd, bit) ((wrd) | 1 << (bit))

int qat_hal_set_ae_ctx_mode(struct icp_qat_fw_loader_handle *handle,
			    unsigned char ae, unsigned char mode)
{
	unsigned int csr, new_csr;

	if (mode != 4 && mode != 8) {
		pr_err("QAT: bad ctx mode=%d\n", mode);
		return -EINVAL;
	}

	/* Sets the accelaration engine context mode to either four or eight */
	csr = qat_hal_rd_ae_csr(handle, ae, CTX_ENABLES);
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

	csr = qat_hal_rd_ae_csr(handle, ae, CTX_ENABLES);
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

	csr = qat_hal_rd_ae_csr(handle, ae, CTX_ENABLES);
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
	case ICP_LMEM2:
		new_csr = (mode) ?
			SET_BIT(csr, CE_LMADDR_2_GLOBAL_BITPOS) :
			CLR_BIT(csr, CE_LMADDR_2_GLOBAL_BITPOS);
		break;
	case ICP_LMEM3:
		new_csr = (mode) ?
			SET_BIT(csr, CE_LMADDR_3_GLOBAL_BITPOS) :
			CLR_BIT(csr, CE_LMADDR_3_GLOBAL_BITPOS);
		break;
	default:
		pr_err("QAT: lmType = 0x%x\n", lm_type);
		return -EINVAL;
	}

	if (new_csr != csr)
		qat_hal_wr_ae_csr(handle, ae, CTX_ENABLES, new_csr);
	return 0;
}

void qat_hal_set_ae_tindex_mode(struct icp_qat_fw_loader_handle *handle,
				unsigned char ae, unsigned char mode)
{
	unsigned int csr, new_csr;

	csr = qat_hal_rd_ae_csr(handle, ae, CTX_ENABLES);
	csr &= IGNORE_W1C_MASK;
	new_csr = (mode) ?
		  SET_BIT(csr, CE_T_INDEX_GLOBAL_BITPOS) :
		  CLR_BIT(csr, CE_T_INDEX_GLOBAL_BITPOS);
	if (new_csr != csr)
		qat_hal_wr_ae_csr(handle, ae, CTX_ENABLES, new_csr);
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
	case ICP_LMEM2:
		reg_addr = 0x2c0;
		break;
	case ICP_LMEM3:
		reg_addr = 0x2e0;
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
	unsigned int reset_mask = handle->chip_info->icp_rst_mask;
	unsigned int reset_csr = handle->chip_info->icp_rst_csr;
	unsigned int csr_val;

	csr_val = GET_CAP_CSR(handle, reset_csr);
	csr_val |= reset_mask;
	SET_CAP_CSR(handle, reset_csr, csr_val);
}

static void qat_hal_wr_indr_csr(struct icp_qat_fw_loader_handle *handle,
				unsigned char ae, unsigned int ctx_mask,
				unsigned int ae_csr, unsigned int csr_val)
{
	unsigned int ctx, cur_ctx;

	cur_ctx = qat_hal_rd_ae_csr(handle, ae, CSR_CTX_POINTER);

	for (ctx = 0; ctx < ICP_QAT_UCLO_MAX_CTX; ctx++) {
		if (!(ctx_mask & (1 << ctx)))
			continue;
		qat_hal_wr_ae_csr(handle, ae, CSR_CTX_POINTER, ctx);
		qat_hal_wr_ae_csr(handle, ae, ae_csr, csr_val);
	}

	qat_hal_wr_ae_csr(handle, ae, CSR_CTX_POINTER, cur_ctx);
}

static unsigned int qat_hal_rd_indr_csr(struct icp_qat_fw_loader_handle *handle,
				unsigned char ae, unsigned char ctx,
				unsigned int ae_csr)
{
	unsigned int cur_ctx, csr_val;

	cur_ctx = qat_hal_rd_ae_csr(handle, ae, CSR_CTX_POINTER);
	qat_hal_wr_ae_csr(handle, ae, CSR_CTX_POINTER, ctx);
	csr_val = qat_hal_rd_ae_csr(handle, ae, ae_csr);
	qat_hal_wr_ae_csr(handle, ae, CSR_CTX_POINTER, cur_ctx);

	return csr_val;
}

static void qat_hal_put_sig_event(struct icp_qat_fw_loader_handle *handle,
				  unsigned char ae, unsigned int ctx_mask,
				  unsigned int events)
{
	unsigned int ctx, cur_ctx;

	cur_ctx = qat_hal_rd_ae_csr(handle, ae, CSR_CTX_POINTER);
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

	cur_ctx = qat_hal_rd_ae_csr(handle, ae, CSR_CTX_POINTER);
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
	unsigned long ae_mask = handle->hal_handle->ae_mask;
	unsigned int base_cnt, cur_cnt;
	unsigned char ae;
	int times = MAX_RETRY_TIMES;

	for_each_set_bit(ae, &ae_mask, handle->hal_handle->ae_max_num) {
		base_cnt = qat_hal_rd_ae_csr(handle, ae, PROFILE_COUNT);
		base_cnt &= 0xffff;

		do {
			cur_cnt = qat_hal_rd_ae_csr(handle, ae, PROFILE_COUNT);
			cur_cnt &= 0xffff;
		} while (times-- && (cur_cnt == base_cnt));

		if (times < 0) {
			pr_err("QAT: AE%d is inactive!!\n", ae);
			return -EFAULT;
		}
	}

	return 0;
}

int qat_hal_check_ae_active(struct icp_qat_fw_loader_handle *handle,
			    unsigned int ae)
{
	unsigned int enable = 0, active = 0;

	enable = qat_hal_rd_ae_csr(handle, ae, CTX_ENABLES);
	active = qat_hal_rd_ae_csr(handle, ae, ACTIVE_CTX_STATUS);
	if ((enable & (0xff << CE_ENABLE_BITPOS)) ||
	    (active & (1 << ACS_ABO_BITPOS)))
		return 1;
	else
		return 0;
}

static void qat_hal_reset_timestamp(struct icp_qat_fw_loader_handle *handle)
{
	unsigned long ae_mask = handle->hal_handle->ae_mask;
	unsigned int misc_ctl_csr, misc_ctl;
	unsigned char ae;

	misc_ctl_csr = handle->chip_info->misc_ctl_csr;
	/* stop the timestamp timers */
	misc_ctl = GET_CAP_CSR(handle, misc_ctl_csr);
	if (misc_ctl & MC_TIMESTAMP_ENABLE)
		SET_CAP_CSR(handle, misc_ctl_csr, misc_ctl &
			    (~MC_TIMESTAMP_ENABLE));

	for_each_set_bit(ae, &ae_mask, handle->hal_handle->ae_max_num) {
		qat_hal_wr_ae_csr(handle, ae, TIMESTAMP_LOW, 0);
		qat_hal_wr_ae_csr(handle, ae, TIMESTAMP_HIGH, 0);
	}
	/* start timestamp timers */
	SET_CAP_CSR(handle, misc_ctl_csr, misc_ctl | MC_TIMESTAMP_ENABLE);
}

#define ESRAM_AUTO_TINIT	BIT(2)
#define ESRAM_AUTO_TINIT_DONE	BIT(3)
#define ESRAM_AUTO_INIT_USED_CYCLES (1640)
#define ESRAM_AUTO_INIT_CSR_OFFSET 0xC1C
static int qat_hal_init_esram(struct icp_qat_fw_loader_handle *handle)
{
	void __iomem *csr_addr =
			(void __iomem *)((uintptr_t)handle->hal_ep_csr_addr_v +
			ESRAM_AUTO_INIT_CSR_OFFSET);
	unsigned int csr_val;
	int times = 30;

	if (handle->pci_dev->device != PCI_DEVICE_ID_INTEL_QAT_DH895XCC)
		return 0;

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
	if (times < 0) {
		pr_err("QAT: Fail to init eSram!\n");
		return -EFAULT;
	}
	return 0;
}

#define SHRAM_INIT_CYCLES 2060
int qat_hal_clr_reset(struct icp_qat_fw_loader_handle *handle)
{
	unsigned int clk_csr = handle->chip_info->glb_clk_enable_csr;
	unsigned int reset_mask = handle->chip_info->icp_rst_mask;
	unsigned int reset_csr = handle->chip_info->icp_rst_csr;
	unsigned long ae_mask = handle->hal_handle->ae_mask;
	unsigned char ae = 0;
	unsigned int times = 100;
	unsigned int csr_val;

	/* write to the reset csr */
	csr_val = GET_CAP_CSR(handle, reset_csr);
	csr_val &= ~reset_mask;
	do {
		SET_CAP_CSR(handle, reset_csr, csr_val);
		if (!(times--))
			goto out_err;
		csr_val = GET_CAP_CSR(handle, reset_csr);
		csr_val &= reset_mask;
	} while (csr_val);
	/* enable clock */
	csr_val = GET_CAP_CSR(handle, clk_csr);
	csr_val |= reset_mask;
	SET_CAP_CSR(handle, clk_csr, csr_val);
	if (qat_hal_check_ae_alive(handle))
		goto out_err;

	/* Set undefined power-up/reset states to reasonable default values */
	for_each_set_bit(ae, &ae_mask, handle->hal_handle->ae_max_num) {
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

	ctx = qat_hal_rd_ae_csr(handle, ae, CTX_ENABLES);
	ctx &= IGNORE_W1C_MASK &
		(~((ctx_mask & ICP_QAT_UCLO_AE_ALL_CTX) << CE_ENABLE_BITPOS));
	qat_hal_wr_ae_csr(handle, ae, CTX_ENABLES, ctx);
}

static u64 qat_hal_parity_64bit(u64 word)
{
	word ^= word >> 1;
	word ^= word >> 2;
	word ^= word >> 4;
	word ^= word >> 8;
	word ^= word >> 16;
	word ^= word >> 32;
	return word & 1;
}

static u64 qat_hal_set_uword_ecc(u64 uword)
{
	u64 bit0_mask = 0xff800007fffULL, bit1_mask = 0x1f801ff801fULL,
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
		       unsigned int words_num, u64 *uword)
{
	unsigned int ustore_addr;
	unsigned int i;

	ustore_addr = qat_hal_rd_ae_csr(handle, ae, USTORE_ADDRESS);
	uaddr |= UA_ECS;
	qat_hal_wr_ae_csr(handle, ae, USTORE_ADDRESS, uaddr);
	for (i = 0; i < words_num; i++) {
		unsigned int uwrd_lo, uwrd_hi;
		u64 tmp;

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

	ctx = qat_hal_rd_ae_csr(handle, ae, CTX_ENABLES);
	ctx &= IGNORE_W1C_MASK;
	ctx_mask &= (ctx & CE_INUSE_CONTEXTS) ? 0x55 : 0xFF;
	ctx |= (ctx_mask << CE_ENABLE_BITPOS);
	qat_hal_wr_ae_csr(handle, ae, CTX_ENABLES, ctx);
}

static void qat_hal_clear_xfer(struct icp_qat_fw_loader_handle *handle)
{
	unsigned long ae_mask = handle->hal_handle->ae_mask;
	unsigned char ae;
	unsigned short reg;

	for_each_set_bit(ae, &ae_mask, handle->hal_handle->ae_max_num) {
		for (reg = 0; reg < ICP_QAT_UCLO_MAX_GPR_REG; reg++) {
			qat_hal_init_rd_xfer(handle, ae, 0, ICP_SR_RD_ABS,
					     reg, 0);
			qat_hal_init_rd_xfer(handle, ae, 0, ICP_DR_RD_ABS,
					     reg, 0);
		}
	}
}

static int qat_hal_clear_gpr(struct icp_qat_fw_loader_handle *handle)
{
	unsigned long ae_mask = handle->hal_handle->ae_mask;
	unsigned char ae;
	unsigned int ctx_mask = ICP_QAT_UCLO_AE_ALL_CTX;
	int times = MAX_RETRY_TIMES;
	unsigned int csr_val = 0;
	unsigned int savctx = 0;
	int ret = 0;

	for_each_set_bit(ae, &ae_mask, handle->hal_handle->ae_max_num) {
		csr_val = qat_hal_rd_ae_csr(handle, ae, AE_MISC_CONTROL);
		csr_val &= ~(1 << MMC_SHARE_CS_BITPOS);
		qat_hal_wr_ae_csr(handle, ae, AE_MISC_CONTROL, csr_val);
		csr_val = qat_hal_rd_ae_csr(handle, ae, CTX_ENABLES);
		csr_val &= IGNORE_W1C_MASK;
		if (handle->chip_info->nn)
			csr_val |= CE_NN_MODE;

		qat_hal_wr_ae_csr(handle, ae, CTX_ENABLES, csr_val);
		qat_hal_wr_uwords(handle, ae, 0, ARRAY_SIZE(inst),
				  (u64 *)inst);
		qat_hal_wr_indr_csr(handle, ae, ctx_mask, CTX_STS_INDIRECT,
				    handle->hal_handle->upc_mask &
				    INIT_PC_VALUE);
		savctx = qat_hal_rd_ae_csr(handle, ae, ACTIVE_CTX_STATUS);
		qat_hal_wr_ae_csr(handle, ae, ACTIVE_CTX_STATUS, 0);
		qat_hal_put_wakeup_event(handle, ae, ctx_mask, XCWE_VOLUNTARY);
		qat_hal_wr_indr_csr(handle, ae, ctx_mask,
				    CTX_SIG_EVENTS_INDIRECT, 0);
		qat_hal_wr_ae_csr(handle, ae, CTX_SIG_EVENTS_ACTIVE, 0);
		qat_hal_enable_ctx(handle, ae, ctx_mask);
	}
	for_each_set_bit(ae, &ae_mask, handle->hal_handle->ae_max_num) {
		/* wait for AE to finish */
		do {
			ret = qat_hal_wait_cycles(handle, ae, 20, 1);
		} while (ret && times--);

		if (times < 0) {
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

static int qat_hal_chip_init(struct icp_qat_fw_loader_handle *handle,
			     struct adf_accel_dev *accel_dev)
{
	struct adf_accel_pci *pci_info = &accel_dev->accel_pci_dev;
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	void __iomem *pmisc_addr = adf_get_pmisc_base(accel_dev);
	unsigned int max_en_ae_id = 0;
	struct adf_bar *sram_bar;
	unsigned int csr_val = 0;
	unsigned long ae_mask;
	unsigned char ae = 0;
	int ret = 0;

	handle->pci_dev = pci_info->pci_dev;
	switch (handle->pci_dev->device) {
	case ADF_4XXX_PCI_DEVICE_ID:
		handle->chip_info->mmp_sram_size = 0;
		handle->chip_info->nn = false;
		handle->chip_info->lm2lm3 = true;
		handle->chip_info->lm_size = ICP_QAT_UCLO_MAX_LMEM_REG_2X;
		handle->chip_info->icp_rst_csr = ICP_RESET_CPP0;
		handle->chip_info->icp_rst_mask = 0x100015;
		handle->chip_info->glb_clk_enable_csr = ICP_GLOBAL_CLK_ENABLE_CPP0;
		handle->chip_info->misc_ctl_csr = MISC_CONTROL_C4XXX;
		handle->chip_info->wakeup_event_val = 0x80000000;
		handle->chip_info->fw_auth = true;
		handle->chip_info->css_3k = true;
		handle->chip_info->tgroup_share_ustore = true;
		handle->chip_info->fcu_ctl_csr = FCU_CONTROL_4XXX;
		handle->chip_info->fcu_sts_csr = FCU_STATUS_4XXX;
		handle->chip_info->fcu_dram_addr_hi = FCU_DRAM_ADDR_HI_4XXX;
		handle->chip_info->fcu_dram_addr_lo = FCU_DRAM_ADDR_LO_4XXX;
		handle->chip_info->fcu_loaded_ae_csr = FCU_AE_LOADED_4XXX;
		handle->chip_info->fcu_loaded_ae_pos = 0;

		handle->hal_cap_g_ctl_csr_addr_v = pmisc_addr + ICP_QAT_CAP_OFFSET_4XXX;
		handle->hal_cap_ae_xfer_csr_addr_v = pmisc_addr + ICP_QAT_AE_OFFSET_4XXX;
		handle->hal_ep_csr_addr_v = pmisc_addr + ICP_QAT_EP_OFFSET_4XXX;
		handle->hal_cap_ae_local_csr_addr_v =
			(void __iomem *)((uintptr_t)handle->hal_cap_ae_xfer_csr_addr_v
			+ LOCAL_TO_XFER_REG_OFFSET);
		break;
	case PCI_DEVICE_ID_INTEL_QAT_C62X:
	case PCI_DEVICE_ID_INTEL_QAT_C3XXX:
		handle->chip_info->mmp_sram_size = 0;
		handle->chip_info->nn = true;
		handle->chip_info->lm2lm3 = false;
		handle->chip_info->lm_size = ICP_QAT_UCLO_MAX_LMEM_REG;
		handle->chip_info->icp_rst_csr = ICP_RESET;
		handle->chip_info->icp_rst_mask = (hw_data->ae_mask << RST_CSR_AE_LSB) |
						  (hw_data->accel_mask << RST_CSR_QAT_LSB);
		handle->chip_info->glb_clk_enable_csr = ICP_GLOBAL_CLK_ENABLE;
		handle->chip_info->misc_ctl_csr = MISC_CONTROL;
		handle->chip_info->wakeup_event_val = WAKEUP_EVENT;
		handle->chip_info->fw_auth = true;
		handle->chip_info->css_3k = false;
		handle->chip_info->tgroup_share_ustore = false;
		handle->chip_info->fcu_ctl_csr = FCU_CONTROL;
		handle->chip_info->fcu_sts_csr = FCU_STATUS;
		handle->chip_info->fcu_dram_addr_hi = FCU_DRAM_ADDR_HI;
		handle->chip_info->fcu_dram_addr_lo = FCU_DRAM_ADDR_LO;
		handle->chip_info->fcu_loaded_ae_csr = FCU_STATUS;
		handle->chip_info->fcu_loaded_ae_pos = FCU_LOADED_AE_POS;
		handle->hal_cap_g_ctl_csr_addr_v = pmisc_addr + ICP_QAT_CAP_OFFSET;
		handle->hal_cap_ae_xfer_csr_addr_v = pmisc_addr + ICP_QAT_AE_OFFSET;
		handle->hal_ep_csr_addr_v = pmisc_addr + ICP_QAT_EP_OFFSET;
		handle->hal_cap_ae_local_csr_addr_v =
			(void __iomem *)((uintptr_t)handle->hal_cap_ae_xfer_csr_addr_v
			+ LOCAL_TO_XFER_REG_OFFSET);
		break;
	case PCI_DEVICE_ID_INTEL_QAT_DH895XCC:
		handle->chip_info->mmp_sram_size = 0x40000;
		handle->chip_info->nn = true;
		handle->chip_info->lm2lm3 = false;
		handle->chip_info->lm_size = ICP_QAT_UCLO_MAX_LMEM_REG;
		handle->chip_info->icp_rst_csr = ICP_RESET;
		handle->chip_info->icp_rst_mask = (hw_data->ae_mask << RST_CSR_AE_LSB) |
						  (hw_data->accel_mask << RST_CSR_QAT_LSB);
		handle->chip_info->glb_clk_enable_csr = ICP_GLOBAL_CLK_ENABLE;
		handle->chip_info->misc_ctl_csr = MISC_CONTROL;
		handle->chip_info->wakeup_event_val = WAKEUP_EVENT;
		handle->chip_info->fw_auth = false;
		handle->chip_info->css_3k = false;
		handle->chip_info->tgroup_share_ustore = false;
		handle->chip_info->fcu_ctl_csr = 0;
		handle->chip_info->fcu_sts_csr = 0;
		handle->chip_info->fcu_dram_addr_hi = 0;
		handle->chip_info->fcu_dram_addr_lo = 0;
		handle->chip_info->fcu_loaded_ae_csr = 0;
		handle->chip_info->fcu_loaded_ae_pos = 0;
		handle->hal_cap_g_ctl_csr_addr_v = pmisc_addr + ICP_QAT_CAP_OFFSET;
		handle->hal_cap_ae_xfer_csr_addr_v = pmisc_addr + ICP_QAT_AE_OFFSET;
		handle->hal_ep_csr_addr_v = pmisc_addr + ICP_QAT_EP_OFFSET;
		handle->hal_cap_ae_local_csr_addr_v =
			(void __iomem *)((uintptr_t)handle->hal_cap_ae_xfer_csr_addr_v
			+ LOCAL_TO_XFER_REG_OFFSET);
		break;
	default:
		ret = -EINVAL;
		goto out_err;
	}

	if (handle->chip_info->mmp_sram_size > 0) {
		sram_bar =
			&pci_info->pci_bars[hw_data->get_sram_bar_id(hw_data)];
		handle->hal_sram_addr_v = sram_bar->virt_addr;
	}
	handle->hal_handle->revision_id = accel_dev->accel_pci_dev.revid;
	handle->hal_handle->ae_mask = hw_data->ae_mask;
	handle->hal_handle->admin_ae_mask = hw_data->admin_ae_mask;
	handle->hal_handle->slice_mask = hw_data->accel_mask;
	handle->cfg_ae_mask = ALL_AE_MASK;
	/* create AE objects */
	handle->hal_handle->upc_mask = 0x1ffff;
	handle->hal_handle->max_ustore = 0x4000;

	ae_mask = handle->hal_handle->ae_mask;
	for_each_set_bit(ae, &ae_mask, ICP_QAT_UCLO_MAX_AE) {
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

	/* Set SIGNATURE_ENABLE[0] to 0x1 in order to enable ALU_OUT csr */
	for_each_set_bit(ae, &ae_mask, handle->hal_handle->ae_max_num) {
		csr_val = qat_hal_rd_ae_csr(handle, ae, SIGNATURE_ENABLE);
		csr_val |= 0x1;
		qat_hal_wr_ae_csr(handle, ae, SIGNATURE_ENABLE, csr_val);
	}
out_err:
	return ret;
}

int qat_hal_init(struct adf_accel_dev *accel_dev)
{
	struct icp_qat_fw_loader_handle *handle;
	int ret = 0;

	handle = kzalloc(sizeof(*handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	handle->hal_handle = kzalloc(sizeof(*handle->hal_handle), GFP_KERNEL);
	if (!handle->hal_handle) {
		ret = -ENOMEM;
		goto out_hal_handle;
	}

	handle->chip_info = kzalloc(sizeof(*handle->chip_info), GFP_KERNEL);
	if (!handle->chip_info) {
		ret = -ENOMEM;
		goto out_chip_info;
	}

	ret = qat_hal_chip_init(handle, accel_dev);
	if (ret) {
		dev_err(&GET_DEV(accel_dev), "qat_hal_chip_init error\n");
		goto out_err;
	}

	/* take all AEs out of reset */
	ret = qat_hal_clr_reset(handle);
	if (ret) {
		dev_err(&GET_DEV(accel_dev), "qat_hal_clr_reset error\n");
		goto out_err;
	}

	qat_hal_clear_xfer(handle);
	if (!handle->chip_info->fw_auth) {
		ret = qat_hal_clear_gpr(handle);
		if (ret)
			goto out_err;
	}

	accel_dev->fw_loader->fw_loader = handle;
	return 0;

out_err:
	kfree(handle->chip_info);
out_chip_info:
	kfree(handle->hal_handle);
out_hal_handle:
	kfree(handle);
	return ret;
}

void qat_hal_deinit(struct icp_qat_fw_loader_handle *handle)
{
	if (!handle)
		return;
	kfree(handle->chip_info);
	kfree(handle->hal_handle);
	kfree(handle);
}

int qat_hal_start(struct icp_qat_fw_loader_handle *handle)
{
	unsigned long ae_mask = handle->hal_handle->ae_mask;
	u32 wakeup_val = handle->chip_info->wakeup_event_val;
	u32 fcu_ctl_csr, fcu_sts_csr;
	unsigned int fcu_sts;
	unsigned char ae;
	u32 ae_ctr = 0;
	int retry = 0;

	if (handle->chip_info->fw_auth) {
		fcu_ctl_csr = handle->chip_info->fcu_ctl_csr;
		fcu_sts_csr = handle->chip_info->fcu_sts_csr;
		ae_ctr = hweight32(ae_mask);
		SET_CAP_CSR(handle, fcu_ctl_csr, FCU_CTRL_CMD_START);
		do {
			msleep(FW_AUTH_WAIT_PERIOD);
			fcu_sts = GET_CAP_CSR(handle, fcu_sts_csr);
			if (((fcu_sts >> FCU_STS_DONE_POS) & 0x1))
				return ae_ctr;
		} while (retry++ < FW_AUTH_MAX_RETRY);
		pr_err("QAT: start error (FCU_STS = 0x%x)\n", fcu_sts);
		return 0;
	} else {
		for_each_set_bit(ae, &ae_mask, handle->hal_handle->ae_max_num) {
			qat_hal_put_wakeup_event(handle, ae, 0, wakeup_val);
			qat_hal_enable_ctx(handle, ae, ICP_QAT_UCLO_AE_ALL_CTX);
			ae_ctr++;
		}
		return ae_ctr;
	}
}

void qat_hal_stop(struct icp_qat_fw_loader_handle *handle, unsigned char ae,
		  unsigned int ctx_mask)
{
	if (!handle->chip_info->fw_auth)
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
			       unsigned int words_num, u64 *uword)
{
	unsigned int i, uwrd_lo, uwrd_hi;
	unsigned int ustore_addr, misc_control;

	misc_control = qat_hal_rd_ae_csr(handle, ae, AE_MISC_CONTROL);
	qat_hal_wr_ae_csr(handle, ae, AE_MISC_CONTROL,
			  misc_control & 0xfffffffb);
	ustore_addr = qat_hal_rd_ae_csr(handle, ae, USTORE_ADDRESS);
	uaddr |= UA_ECS;
	for (i = 0; i < words_num; i++) {
		qat_hal_wr_ae_csr(handle, ae, USTORE_ADDRESS, uaddr);
		uaddr++;
		uwrd_lo = qat_hal_rd_ae_csr(handle, ae, USTORE_DATA_LOWER);
		uwrd_hi = qat_hal_rd_ae_csr(handle, ae, USTORE_DATA_UPPER);
		uword[i] = uwrd_hi;
		uword[i] = (uword[i] << 0x20) | uwrd_lo;
	}
	qat_hal_wr_ae_csr(handle, ae, AE_MISC_CONTROL, misc_control);
	qat_hal_wr_ae_csr(handle, ae, USTORE_ADDRESS, ustore_addr);
}

void qat_hal_wr_umem(struct icp_qat_fw_loader_handle *handle,
		     unsigned char ae, unsigned int uaddr,
		     unsigned int words_num, unsigned int *data)
{
	unsigned int i, ustore_addr;

	ustore_addr = qat_hal_rd_ae_csr(handle, ae, USTORE_ADDRESS);
	uaddr |= UA_ECS;
	qat_hal_wr_ae_csr(handle, ae, USTORE_ADDRESS, uaddr);
	for (i = 0; i < words_num; i++) {
		unsigned int uwrd_lo, uwrd_hi, tmp;

		uwrd_lo = ((data[i] & 0xfff0000) << 4) | (0x3 << 18) |
			  ((data[i] & 0xff00) << 2) |
			  (0x3 << 8) | (data[i] & 0xff);
		uwrd_hi = (0xf << 4) | ((data[i] & 0xf0000000) >> 28);
		uwrd_hi |= (hweight32(data[i] & 0xffff) & 0x1) << 8;
		tmp = ((data[i] >> 0x10) & 0xffff);
		uwrd_hi |= (hweight32(tmp) & 0x1) << 9;
		qat_hal_wr_ae_csr(handle, ae, USTORE_DATA_LOWER, uwrd_lo);
		qat_hal_wr_ae_csr(handle, ae, USTORE_DATA_UPPER, uwrd_hi);
	}
	qat_hal_wr_ae_csr(handle, ae, USTORE_ADDRESS, ustore_addr);
}

#define MAX_EXEC_INST 100
static int qat_hal_exec_micro_inst(struct icp_qat_fw_loader_handle *handle,
				   unsigned char ae, unsigned char ctx,
				   u64 *micro_inst, unsigned int inst_num,
				   int code_off, unsigned int max_cycle,
				   unsigned int *endpc)
{
	unsigned int ind_lm_addr_byte0 = 0, ind_lm_addr_byte1 = 0;
	unsigned int ind_lm_addr_byte2 = 0, ind_lm_addr_byte3 = 0;
	unsigned int ind_t_index = 0, ind_t_index_byte = 0;
	unsigned int ind_lm_addr0 = 0, ind_lm_addr1 = 0;
	unsigned int ind_lm_addr2 = 0, ind_lm_addr3 = 0;
	u64 savuwords[MAX_EXEC_INST];
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
	ind_lm_addr0 = qat_hal_rd_indr_csr(handle, ae, ctx, LM_ADDR_0_INDIRECT);
	ind_lm_addr1 = qat_hal_rd_indr_csr(handle, ae, ctx, LM_ADDR_1_INDIRECT);
	ind_lm_addr_byte0 = qat_hal_rd_indr_csr(handle, ae, ctx,
						INDIRECT_LM_ADDR_0_BYTE_INDEX);
	ind_lm_addr_byte1 = qat_hal_rd_indr_csr(handle, ae, ctx,
						INDIRECT_LM_ADDR_1_BYTE_INDEX);
	if (handle->chip_info->lm2lm3) {
		ind_lm_addr2 = qat_hal_rd_indr_csr(handle, ae, ctx,
						   LM_ADDR_2_INDIRECT);
		ind_lm_addr3 = qat_hal_rd_indr_csr(handle, ae, ctx,
						   LM_ADDR_3_INDIRECT);
		ind_lm_addr_byte2 = qat_hal_rd_indr_csr(handle, ae, ctx,
							INDIRECT_LM_ADDR_2_BYTE_INDEX);
		ind_lm_addr_byte3 = qat_hal_rd_indr_csr(handle, ae, ctx,
							INDIRECT_LM_ADDR_3_BYTE_INDEX);
		ind_t_index = qat_hal_rd_indr_csr(handle, ae, ctx,
						  INDIRECT_T_INDEX);
		ind_t_index_byte = qat_hal_rd_indr_csr(handle, ae, ctx,
						       INDIRECT_T_INDEX_BYTE_INDEX);
	}
	if (inst_num <= MAX_EXEC_INST)
		qat_hal_get_uwords(handle, ae, 0, inst_num, savuwords);
	qat_hal_get_wakeup_event(handle, ae, ctx, &wakeup_events);
	savpc = qat_hal_rd_indr_csr(handle, ae, ctx, CTX_STS_INDIRECT);
	savpc = (savpc & handle->hal_handle->upc_mask) >> 0;
	ctx_enables = qat_hal_rd_ae_csr(handle, ae, CTX_ENABLES);
	ctx_enables &= IGNORE_W1C_MASK;
	savcc = qat_hal_rd_ae_csr(handle, ae, CC_ENABLE);
	savctx = qat_hal_rd_ae_csr(handle, ae, ACTIVE_CTX_STATUS);
	ctxarb_ctl = qat_hal_rd_ae_csr(handle, ae, CTX_ARB_CNTL);
	ind_cnt_sig = qat_hal_rd_indr_csr(handle, ae, ctx,
					  FUTURE_COUNT_SIGNAL_INDIRECT);
	ind_sig = qat_hal_rd_indr_csr(handle, ae, ctx,
				      CTX_SIG_EVENTS_INDIRECT);
	act_sig = qat_hal_rd_ae_csr(handle, ae, CTX_SIG_EVENTS_ACTIVE);
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

		ctx_status = qat_hal_rd_indr_csr(handle, ae, ctx,
						 CTX_STS_INDIRECT);
		*endpc = ctx_status & handle->hal_handle->upc_mask;
	}
	/* retore to saved context */
	qat_hal_disable_ctx(handle, ae, (1 << ctx));
	if (inst_num <= MAX_EXEC_INST)
		qat_hal_wr_uwords(handle, ae, 0, inst_num, savuwords);
	qat_hal_put_wakeup_event(handle, ae, (1 << ctx), wakeup_events);
	qat_hal_wr_indr_csr(handle, ae, (1 << ctx), CTX_STS_INDIRECT,
			    handle->hal_handle->upc_mask & savpc);
	csr_val = qat_hal_rd_ae_csr(handle, ae, AE_MISC_CONTROL);
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
	if (handle->chip_info->lm2lm3) {
		qat_hal_wr_indr_csr(handle, ae, BIT(ctx), LM_ADDR_2_INDIRECT,
				    ind_lm_addr2);
		qat_hal_wr_indr_csr(handle, ae, BIT(ctx), LM_ADDR_3_INDIRECT,
				    ind_lm_addr3);
		qat_hal_wr_indr_csr(handle, ae, BIT(ctx),
				    INDIRECT_LM_ADDR_2_BYTE_INDEX,
				    ind_lm_addr_byte2);
		qat_hal_wr_indr_csr(handle, ae, BIT(ctx),
				    INDIRECT_LM_ADDR_3_BYTE_INDEX,
				    ind_lm_addr_byte3);
		qat_hal_wr_indr_csr(handle, ae, BIT(ctx),
				    INDIRECT_T_INDEX, ind_t_index);
		qat_hal_wr_indr_csr(handle, ae, BIT(ctx),
				    INDIRECT_T_INDEX_BYTE_INDEX,
				    ind_t_index_byte);
	}
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
	u64 insts, savuword;

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
		insts = (u64)0xA030000000ull | ((reg_addr & 0x3ff) << 10);
		break;
	}
	savctx = qat_hal_rd_ae_csr(handle, ae, ACTIVE_CTX_STATUS);
	ctxarb_cntl = qat_hal_rd_ae_csr(handle, ae, CTX_ARB_CNTL);
	ctx_enables = qat_hal_rd_ae_csr(handle, ae, CTX_ENABLES);
	ctx_enables &= IGNORE_W1C_MASK;
	if (ctx != (savctx & ACS_ACNO))
		qat_hal_wr_ae_csr(handle, ae, ACTIVE_CTX_STATUS,
				  ctx & ACS_ACNO);
	qat_hal_get_uwords(handle, ae, 0, 1, &savuword);
	qat_hal_wr_ae_csr(handle, ae, CTX_ENABLES, ctx_enables);
	ustore_addr = qat_hal_rd_ae_csr(handle, ae, USTORE_ADDRESS);
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
	*data = qat_hal_rd_ae_csr(handle, ae, ALU_OUT);
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
	u64 insts[] = {
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

static int qat_hal_concat_micro_code(u64 *micro_inst,
				     unsigned int inst_num, unsigned int size,
				     unsigned int addr, unsigned int *value)
{
	int i;
	unsigned int cur_value;
	const u64 *inst_arr;
	int fixup_offset;
	int usize = 0;
	int orig_num;

	orig_num = inst_num;
	cur_value = value[0];
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
				      int *pfirst_exec, u64 *micro_inst,
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
	u64 *micro_inst_arry;
	int micro_inst_num;
	int alloc_inst_size;
	int first_exec = 1;
	int stat = 0;

	plm_init = lm_init_header->next;
	alloc_inst_size = lm_init_header->size;
	if ((unsigned int)alloc_inst_size > handle->hal_handle->max_ustore)
		alloc_inst_size = handle->hal_handle->max_ustore;
	micro_inst_arry = kmalloc_array(alloc_inst_size, sizeof(u64),
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
	if (micro_inst_arry && micro_inst_num > 0) {
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

	ctx_enables = qat_hal_rd_ae_csr(handle, ae, CTX_ENABLES);
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
	u64 micro_inst[] = {
		0x0F440000000ull,
		0x0F040000000ull,
		0x0A000000000ull,
		0x0F0000C0300ull,
		0x0E000010000ull
	};
	const int num_inst = ARRAY_SIZE(micro_inst), code_off = 1;
	const unsigned short gprnum = 0, dly = num_inst * 0x5;

	ctx_enables = qat_hal_rd_ae_csr(handle, ae, CTX_ENABLES);
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
	status = qat_hal_rd_rel_reg(handle, ae, ctx, ICP_GPB_REL, gprnum, &gprval);
	if (status) {
		pr_err("QAT: failed to read register");
		return status;
	}
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

	ctx_enables = qat_hal_rd_ae_csr(handle, ae, CTX_ENABLES);
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

	ctx_enables = qat_hal_rd_ae_csr(handle, ae, CTX_ENABLES);
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
		     unsigned char ae, unsigned long ctx_mask,
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
			if (!test_bit(ctx, &ctx_mask))
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
			 unsigned char ae, unsigned long ctx_mask,
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
			if (!test_bit(ctx, &ctx_mask))
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
			 unsigned char ae, unsigned long ctx_mask,
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
			if (!test_bit(ctx, &ctx_mask))
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
		    unsigned char ae, unsigned long ctx_mask,
		    unsigned short reg_num, unsigned int regdata)
{
	int stat = 0;
	unsigned char ctx;
	if (!handle->chip_info->nn) {
		dev_err(&handle->pci_dev->dev, "QAT: No next neigh in 0x%x\n",
			handle->pci_dev->device);
		return -EINVAL;
	}

	if (ctx_mask == 0)
		return -EINVAL;

	for (ctx = 0; ctx < ICP_QAT_UCLO_MAX_CTX; ctx++) {
		if (!test_bit(ctx, &ctx_mask))
			continue;
		stat = qat_hal_put_rel_nn(handle, ae, ctx, reg_num, regdata);
		if (stat) {
			pr_err("QAT: write neigh error\n");
			return -EINVAL;
		}
	}

	return 0;
}

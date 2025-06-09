// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#include "../dmub_srv.h"
#include "dmub_reg.h"
#include "dmub_dcn401.h"

#include "dcn/dcn_4_1_0_offset.h"
#include "dcn/dcn_4_1_0_sh_mask.h"

#define DCN_BASE__INST0_SEG2                       0x000034C0

#define BASE_INNER(seg) DCN_BASE__INST0_SEG##seg
#define CTX dmub
#define REGS dmub->regs_dcn401
#define REG_OFFSET_EXP(reg_name) (BASE(reg##reg_name##_BASE_IDX) + reg##reg_name)

const struct dmub_srv_dcn401_regs dmub_srv_dcn401_regs = {
#define DMUB_SR(reg) REG_OFFSET_EXP(reg),
	{
		DMUB_DCN401_REGS()
		DMCUB_INTERNAL_REGS()
	},
#undef DMUB_SR

#define DMUB_SF(reg, field) FD_MASK(reg, field),
		{ DMUB_DCN401_FIELDS() },
#undef DMUB_SF

#define DMUB_SF(reg, field) FD_SHIFT(reg, field),
		{ DMUB_DCN401_FIELDS() },
#undef DMUB_SF
};

static void dmub_dcn401_get_fb_base_offset(struct dmub_srv *dmub,
		uint64_t *fb_base,
		uint64_t *fb_offset)
{
	uint32_t tmp;

	if (dmub->fb_base || dmub->fb_offset) {
		*fb_base = dmub->fb_base;
		*fb_offset = dmub->fb_offset;
		return;
	}

	REG_GET(DCN_VM_FB_LOCATION_BASE, FB_BASE, &tmp);
	*fb_base = (uint64_t)tmp << 24;

	REG_GET(DCN_VM_FB_OFFSET, FB_OFFSET, &tmp);
	*fb_offset = (uint64_t)tmp << 24;
}

static inline void dmub_dcn401_translate_addr(const union dmub_addr *addr_in,
		uint64_t fb_base,
		uint64_t fb_offset,
		union dmub_addr *addr_out)
{
	addr_out->quad_part = addr_in->quad_part - fb_base + fb_offset;
}

void dmub_dcn401_reset(struct dmub_srv *dmub)
{
	union dmub_gpint_data_register cmd;
	const uint32_t timeout_us = 1 * 1000 * 1000; //1s
	const uint32_t poll_delay_us = 1; //1us
	uint32_t i = 0;
	uint32_t in_reset, scratch, pwait_mode;

	REG_GET(DMCUB_CNTL2, DMCUB_SOFT_RESET, &in_reset);

	if (in_reset == 0) {
		cmd.bits.status = 1;
		cmd.bits.command_code = DMUB_GPINT__STOP_FW;
		cmd.bits.param = 0;

		dmub->hw_funcs.set_gpint(dmub, cmd);

		for (i = 0; i < timeout_us; i++) {
			if (dmub->hw_funcs.is_gpint_acked(dmub, cmd))
				break;

			udelay(poll_delay_us);
		}

		for (; i < timeout_us; i++) {
			scratch = dmub->hw_funcs.get_gpint_response(dmub);
			if (scratch == DMUB_GPINT__STOP_FW_RESPONSE)
				break;

			udelay(poll_delay_us);
		}

		for (; i < timeout_us; i++) {
			REG_GET(DMCUB_CNTL, DMCUB_PWAIT_MODE_STATUS, &pwait_mode);
			if (pwait_mode & (1 << 0))
				break;

			udelay(poll_delay_us);
		}
	}

	if (i >= timeout_us) {
		/* timeout should never occur */
		BREAK_TO_DEBUGGER();
	}

	REG_WRITE(DMCUB_INBOX1_RPTR, 0);
	REG_WRITE(DMCUB_INBOX1_WPTR, 0);
	REG_WRITE(DMCUB_OUTBOX1_RPTR, 0);
	REG_WRITE(DMCUB_OUTBOX1_WPTR, 0);
	REG_WRITE(DMCUB_OUTBOX0_RPTR, 0);
	REG_WRITE(DMCUB_OUTBOX0_WPTR, 0);
	REG_WRITE(DMCUB_SCRATCH0, 0);

	/* Clear the GPINT command manually so we don't reset again. */
	cmd.all = 0;
	dmub->hw_funcs.set_gpint(dmub, cmd);
}

void dmub_dcn401_reset_release(struct dmub_srv *dmub)
{
	REG_UPDATE(MMHUBBUB_SOFT_RESET, DMUIF_SOFT_RESET, 0);
	REG_WRITE(DMCUB_SCRATCH15, dmub->psp_version & 0x001100FF);
	REG_UPDATE_2(DMCUB_CNTL, DMCUB_ENABLE, 1, DMCUB_TRACEPORT_EN, 1);
	REG_UPDATE(DMCUB_CNTL2, DMCUB_SOFT_RESET, 0);
}

void dmub_dcn401_backdoor_load(struct dmub_srv *dmub,
		const struct dmub_window *cw0,
		const struct dmub_window *cw1)
{
	union dmub_addr offset;
	uint64_t fb_base, fb_offset;

	dmub_dcn401_get_fb_base_offset(dmub, &fb_base, &fb_offset);

	/* reset and disable DMCUB and MMHUBBUB DMUIF */
	REG_UPDATE(DMCUB_SEC_CNTL, DMCUB_SEC_RESET, 1);
	REG_UPDATE(MMHUBBUB_SOFT_RESET, DMUIF_SOFT_RESET, 1);
	REG_UPDATE(DMCUB_CNTL, DMCUB_ENABLE, 0);

	dmub_dcn401_translate_addr(&cw0->offset, fb_base, fb_offset, &offset);

	REG_WRITE(DMCUB_REGION3_CW0_OFFSET, offset.u.low_part);
	REG_WRITE(DMCUB_REGION3_CW0_OFFSET_HIGH, offset.u.high_part);
	REG_WRITE(DMCUB_REGION3_CW0_BASE_ADDRESS, cw0->region.base);
	REG_SET_2(DMCUB_REGION3_CW0_TOP_ADDRESS, 0,
			DMCUB_REGION3_CW0_TOP_ADDRESS, cw0->region.top,
			DMCUB_REGION3_CW0_ENABLE, 1);

	dmub_dcn401_translate_addr(&cw1->offset, fb_base, fb_offset, &offset);

	REG_WRITE(DMCUB_REGION3_CW1_OFFSET, offset.u.low_part);
	REG_WRITE(DMCUB_REGION3_CW1_OFFSET_HIGH, offset.u.high_part);
	REG_WRITE(DMCUB_REGION3_CW1_BASE_ADDRESS, cw1->region.base);
	REG_SET_2(DMCUB_REGION3_CW1_TOP_ADDRESS, 0,
			DMCUB_REGION3_CW1_TOP_ADDRESS, cw1->region.top,
			DMCUB_REGION3_CW1_ENABLE, 1);

	/* release DMCUB reset only to prevent premature execution */
	REG_UPDATE_2(DMCUB_SEC_CNTL, DMCUB_SEC_RESET, 0, DMCUB_MEM_UNIT_ID,
			0x20);
}

void dmub_dcn401_backdoor_load_zfb_mode(struct dmub_srv *dmub,
		      const struct dmub_window *cw0,
		      const struct dmub_window *cw1)
{
	union dmub_addr offset;

	/* reset and disable DMCUB and MMHUBBUB DMUIF */
	REG_UPDATE(DMCUB_SEC_CNTL, DMCUB_SEC_RESET, 1);
	REG_UPDATE(MMHUBBUB_SOFT_RESET, DMUIF_SOFT_RESET, 1);
	REG_UPDATE(DMCUB_CNTL, DMCUB_ENABLE, 0);

	offset = cw0->offset;

	REG_WRITE(DMCUB_REGION3_CW0_OFFSET, offset.u.low_part);
	REG_WRITE(DMCUB_REGION3_CW0_OFFSET_HIGH, offset.u.high_part);
	REG_WRITE(DMCUB_REGION3_CW0_BASE_ADDRESS, cw0->region.base);
	REG_SET_2(DMCUB_REGION3_CW0_TOP_ADDRESS, 0,
			DMCUB_REGION3_CW0_TOP_ADDRESS, cw0->region.top,
			DMCUB_REGION3_CW0_ENABLE, 1);

	offset = cw1->offset;

	REG_WRITE(DMCUB_REGION3_CW1_OFFSET, offset.u.low_part);
	REG_WRITE(DMCUB_REGION3_CW1_OFFSET_HIGH, offset.u.high_part);
	REG_WRITE(DMCUB_REGION3_CW1_BASE_ADDRESS, cw1->region.base);
	REG_SET_2(DMCUB_REGION3_CW1_TOP_ADDRESS, 0,
			DMCUB_REGION3_CW1_TOP_ADDRESS, cw1->region.top,
			DMCUB_REGION3_CW1_ENABLE, 1);

	/* release DMCUB reset only to prevent premature execution */
	REG_UPDATE_2(DMCUB_SEC_CNTL, DMCUB_SEC_RESET, 0, DMCUB_MEM_UNIT_ID,
			0x20);
}

void dmub_dcn401_setup_windows(struct dmub_srv *dmub,
		const struct dmub_window *cw2,
		const struct dmub_window *cw3,
		const struct dmub_window *cw4,
		const struct dmub_window *cw5,
		const struct dmub_window *cw6,
		const struct dmub_window *region6)
{
	union dmub_addr offset;

	offset = cw3->offset;

	REG_WRITE(DMCUB_REGION3_CW3_OFFSET, offset.u.low_part);
	REG_WRITE(DMCUB_REGION3_CW3_OFFSET_HIGH, offset.u.high_part);
	REG_WRITE(DMCUB_REGION3_CW3_BASE_ADDRESS, cw3->region.base);
	REG_SET_2(DMCUB_REGION3_CW3_TOP_ADDRESS, 0,
			DMCUB_REGION3_CW3_TOP_ADDRESS, cw3->region.top,
			DMCUB_REGION3_CW3_ENABLE, 1);

	offset = cw4->offset;

	REG_WRITE(DMCUB_REGION3_CW4_OFFSET, offset.u.low_part);
	REG_WRITE(DMCUB_REGION3_CW4_OFFSET_HIGH, offset.u.high_part);
	REG_WRITE(DMCUB_REGION3_CW4_BASE_ADDRESS, cw4->region.base);
	REG_SET_2(DMCUB_REGION3_CW4_TOP_ADDRESS, 0,
			DMCUB_REGION3_CW4_TOP_ADDRESS, cw4->region.top,
			DMCUB_REGION3_CW4_ENABLE, 1);

	offset = cw5->offset;

	REG_WRITE(DMCUB_REGION3_CW5_OFFSET, offset.u.low_part);
	REG_WRITE(DMCUB_REGION3_CW5_OFFSET_HIGH, offset.u.high_part);
	REG_WRITE(DMCUB_REGION3_CW5_BASE_ADDRESS, cw5->region.base);
	REG_SET_2(DMCUB_REGION3_CW5_TOP_ADDRESS, 0,
			DMCUB_REGION3_CW5_TOP_ADDRESS, cw5->region.top,
			DMCUB_REGION3_CW5_ENABLE, 1);

	REG_WRITE(DMCUB_REGION5_OFFSET, offset.u.low_part);
	REG_WRITE(DMCUB_REGION5_OFFSET_HIGH, offset.u.high_part);
	REG_SET_2(DMCUB_REGION5_TOP_ADDRESS, 0,
			DMCUB_REGION5_TOP_ADDRESS,
			cw5->region.top - cw5->region.base - 1,
			DMCUB_REGION5_ENABLE, 1);

	offset = cw6->offset;

	REG_WRITE(DMCUB_REGION3_CW6_OFFSET, offset.u.low_part);
	REG_WRITE(DMCUB_REGION3_CW6_OFFSET_HIGH, offset.u.high_part);
	REG_WRITE(DMCUB_REGION3_CW6_BASE_ADDRESS, cw6->region.base);
	REG_SET_2(DMCUB_REGION3_CW6_TOP_ADDRESS, 0,
			DMCUB_REGION3_CW6_TOP_ADDRESS, cw6->region.top,
			DMCUB_REGION3_CW6_ENABLE, 1);

	offset = region6->offset;

	REG_WRITE(DMCUB_REGION6_OFFSET, offset.u.low_part);
	REG_WRITE(DMCUB_REGION6_OFFSET_HIGH, offset.u.high_part);
	REG_SET_2(DMCUB_REGION6_TOP_ADDRESS, 0,
		  DMCUB_REGION6_TOP_ADDRESS,
		  region6->region.top - region6->region.base - 1,
		  DMCUB_REGION6_ENABLE, 1);
}

void dmub_dcn401_setup_mailbox(struct dmub_srv *dmub,
		const struct dmub_region *inbox1)
{
	REG_WRITE(DMCUB_INBOX1_BASE_ADDRESS, inbox1->base);
	REG_WRITE(DMCUB_INBOX1_SIZE, inbox1->top - inbox1->base);
}

uint32_t dmub_dcn401_get_inbox1_wptr(struct dmub_srv *dmub)
{
	return REG_READ(DMCUB_INBOX1_WPTR);
}

uint32_t dmub_dcn401_get_inbox1_rptr(struct dmub_srv *dmub)
{
	return REG_READ(DMCUB_INBOX1_RPTR);
}

void dmub_dcn401_set_inbox1_wptr(struct dmub_srv *dmub, uint32_t wptr_offset)
{
	REG_WRITE(DMCUB_INBOX1_WPTR, wptr_offset);
}

void dmub_dcn401_setup_out_mailbox(struct dmub_srv *dmub,
		const struct dmub_region *outbox1)
{
	REG_WRITE(DMCUB_OUTBOX1_BASE_ADDRESS, outbox1->base);
	REG_WRITE(DMCUB_OUTBOX1_SIZE, outbox1->top - outbox1->base);
}

uint32_t dmub_dcn401_get_outbox1_wptr(struct dmub_srv *dmub)
{
	/**
	 * outbox1 wptr register is accessed without locks (dal & dc)
	 * and to be called only by dmub_srv_stat_get_notification()
	 */
	return REG_READ(DMCUB_OUTBOX1_WPTR);
}

void dmub_dcn401_set_outbox1_rptr(struct dmub_srv *dmub, uint32_t rptr_offset)
{
	/**
	 * outbox1 rptr register is accessed without locks (dal & dc)
	 * and to be called only by dmub_srv_stat_get_notification()
	 */
	REG_WRITE(DMCUB_OUTBOX1_RPTR, rptr_offset);
}

bool dmub_dcn401_is_hw_init(struct dmub_srv *dmub)
{
	union dmub_fw_boot_status status;
	uint32_t is_hw_init;

	status.all = REG_READ(DMCUB_SCRATCH0);
	REG_GET(DMCUB_CNTL, DMCUB_ENABLE, &is_hw_init);

	return is_hw_init != 0 && status.bits.dal_fw;
}

bool dmub_dcn401_is_supported(struct dmub_srv *dmub)
{
	uint32_t supported = 0;

	REG_GET(CC_DC_PIPE_DIS, DC_DMCUB_ENABLE, &supported);

	return supported;
}

void dmub_dcn401_set_gpint(struct dmub_srv *dmub,
		union dmub_gpint_data_register reg)
{
	REG_WRITE(DMCUB_GPINT_DATAIN1, reg.all);
}

bool dmub_dcn401_is_gpint_acked(struct dmub_srv *dmub,
		union dmub_gpint_data_register reg)
{
	union dmub_gpint_data_register test;

	reg.bits.status = 0;
	test.all = REG_READ(DMCUB_GPINT_DATAIN1);

	return test.all == reg.all;
}

uint32_t dmub_dcn401_get_gpint_response(struct dmub_srv *dmub)
{
	return REG_READ(DMCUB_SCRATCH7);
}

uint32_t dmub_dcn401_get_gpint_dataout(struct dmub_srv *dmub)
{
	uint32_t dataout = REG_READ(DMCUB_GPINT_DATAOUT);

	REG_UPDATE(DMCUB_INTERRUPT_ENABLE, DMCUB_GPINT_IH_INT_EN, 0);

	REG_WRITE(DMCUB_GPINT_DATAOUT, 0);
	REG_UPDATE(DMCUB_INTERRUPT_ACK, DMCUB_GPINT_IH_INT_ACK, 1);
	REG_UPDATE(DMCUB_INTERRUPT_ACK, DMCUB_GPINT_IH_INT_ACK, 0);

	REG_UPDATE(DMCUB_INTERRUPT_ENABLE, DMCUB_GPINT_IH_INT_EN, 1);

	return dataout;
}

union dmub_fw_boot_status dmub_dcn401_get_fw_boot_status(struct dmub_srv *dmub)
{
	union dmub_fw_boot_status status;

	status.all = REG_READ(DMCUB_SCRATCH0);
	return status;
}

void dmub_dcn401_enable_dmub_boot_options(struct dmub_srv *dmub, const struct dmub_srv_hw_params *params)
{
	union dmub_fw_boot_options boot_options = {0};

	boot_options.bits.z10_disable = params->disable_z10;

	boot_options.bits.skip_phy_access = params->disallow_phy_access;

	REG_WRITE(DMCUB_SCRATCH14, boot_options.all);
}

void dmub_dcn401_skip_dmub_panel_power_sequence(struct dmub_srv *dmub, bool skip)
{
	union dmub_fw_boot_options boot_options;
	boot_options.all = REG_READ(DMCUB_SCRATCH14);
	boot_options.bits.skip_phy_init_panel_sequence = skip;
	REG_WRITE(DMCUB_SCRATCH14, boot_options.all);
}

void dmub_dcn401_setup_outbox0(struct dmub_srv *dmub,
		const struct dmub_region *outbox0)
{
	REG_WRITE(DMCUB_OUTBOX0_BASE_ADDRESS, outbox0->base);

	REG_WRITE(DMCUB_OUTBOX0_SIZE, outbox0->top - outbox0->base);
}

uint32_t dmub_dcn401_get_outbox0_wptr(struct dmub_srv *dmub)
{
	return REG_READ(DMCUB_OUTBOX0_WPTR);
}

void dmub_dcn401_set_outbox0_rptr(struct dmub_srv *dmub, uint32_t rptr_offset)
{
	REG_WRITE(DMCUB_OUTBOX0_RPTR, rptr_offset);
}

uint32_t dmub_dcn401_get_current_time(struct dmub_srv *dmub)
{
	return REG_READ(DMCUB_TIMER_CURRENT);
}

void dmub_dcn401_get_diagnostic_data(struct dmub_srv *dmub)
{
	uint32_t is_dmub_enabled, is_soft_reset, is_sec_reset;
	uint32_t is_traceport_enabled, is_cw0_enabled, is_cw6_enabled;
	struct dmub_timeout_info timeout = {0};

	if (!dmub)
		return;

	/* timeout data filled externally, cache before resetting memory */
	timeout = dmub->debug.timeout_info;
	memset(&dmub->debug, 0, sizeof(dmub->debug));
	dmub->debug.timeout_info = timeout;

	dmub->debug.dmcub_version = dmub->fw_version;

	dmub->debug.scratch[0] = REG_READ(DMCUB_SCRATCH0);
	dmub->debug.scratch[1] = REG_READ(DMCUB_SCRATCH1);
	dmub->debug.scratch[2] = REG_READ(DMCUB_SCRATCH2);
	dmub->debug.scratch[3] = REG_READ(DMCUB_SCRATCH3);
	dmub->debug.scratch[4] = REG_READ(DMCUB_SCRATCH4);
	dmub->debug.scratch[5] = REG_READ(DMCUB_SCRATCH5);
	dmub->debug.scratch[6] = REG_READ(DMCUB_SCRATCH6);
	dmub->debug.scratch[7] = REG_READ(DMCUB_SCRATCH7);
	dmub->debug.scratch[8] = REG_READ(DMCUB_SCRATCH8);
	dmub->debug.scratch[9] = REG_READ(DMCUB_SCRATCH9);
	dmub->debug.scratch[10] = REG_READ(DMCUB_SCRATCH10);
	dmub->debug.scratch[11] = REG_READ(DMCUB_SCRATCH11);
	dmub->debug.scratch[12] = REG_READ(DMCUB_SCRATCH12);
	dmub->debug.scratch[13] = REG_READ(DMCUB_SCRATCH13);
	dmub->debug.scratch[14] = REG_READ(DMCUB_SCRATCH14);
	dmub->debug.scratch[15] = REG_READ(DMCUB_SCRATCH15);
	dmub->debug.scratch[16] = REG_READ(DMCUB_SCRATCH16);

	dmub->debug.undefined_address_fault_addr = REG_READ(DMCUB_UNDEFINED_ADDRESS_FAULT_ADDR);
	dmub->debug.inst_fetch_fault_addr = REG_READ(DMCUB_INST_FETCH_FAULT_ADDR);
	dmub->debug.data_write_fault_addr = REG_READ(DMCUB_DATA_WRITE_FAULT_ADDR);

	dmub->debug.inbox1_rptr = REG_READ(DMCUB_INBOX1_RPTR);
	dmub->debug.inbox1_wptr = REG_READ(DMCUB_INBOX1_WPTR);
	dmub->debug.inbox1_size = REG_READ(DMCUB_INBOX1_SIZE);

	dmub->debug.inbox0_rptr = REG_READ(DMCUB_INBOX0_RPTR);
	dmub->debug.inbox0_wptr = REG_READ(DMCUB_INBOX0_WPTR);
	dmub->debug.inbox0_size = REG_READ(DMCUB_INBOX0_SIZE);

	dmub->debug.outbox1_rptr = REG_READ(DMCUB_OUTBOX1_RPTR);
	dmub->debug.outbox1_wptr = REG_READ(DMCUB_OUTBOX1_WPTR);
	dmub->debug.outbox1_size = REG_READ(DMCUB_OUTBOX1_SIZE);

	REG_GET(DMCUB_CNTL, DMCUB_ENABLE, &is_dmub_enabled);
	dmub->debug.is_dmcub_enabled = is_dmub_enabled;

	REG_GET(DMCUB_CNTL2, DMCUB_SOFT_RESET, &is_soft_reset);
	dmub->debug.is_dmcub_soft_reset = is_soft_reset;

	REG_GET(DMCUB_SEC_CNTL, DMCUB_SEC_RESET_STATUS, &is_sec_reset);
	dmub->debug.is_dmcub_secure_reset = is_sec_reset;

	REG_GET(DMCUB_CNTL, DMCUB_TRACEPORT_EN, &is_traceport_enabled);
	dmub->debug.is_traceport_en  = is_traceport_enabled;

	REG_GET(DMCUB_REGION3_CW0_TOP_ADDRESS, DMCUB_REGION3_CW0_ENABLE, &is_cw0_enabled);
	dmub->debug.is_cw0_enabled = is_cw0_enabled;

	REG_GET(DMCUB_REGION3_CW6_TOP_ADDRESS, DMCUB_REGION3_CW6_ENABLE, &is_cw6_enabled);
	dmub->debug.is_cw6_enabled = is_cw6_enabled;

	dmub->debug.gpint_datain0 = REG_READ(DMCUB_GPINT_DATAIN0);
}
void dmub_dcn401_configure_dmub_in_system_memory(struct dmub_srv *dmub)
{
	/* DMCUB_REGION3_TMR_AXI_SPACE values:
	 * 0b011 (0x3) - FB physical address
	 * 0b100 (0x4) - GPU virtual address
	 *
	 * Default value is 0x3 (FB Physical address for TMR). When programming
	 * DMUB to be in system memory, change to 0x4. The system memory allocated
	 * is accessible by both GPU and CPU, so we use GPU virtual address.
	 */
	REG_WRITE(DMCUB_REGION3_TMR_AXI_SPACE, 0x4);
}

void dmub_dcn401_send_inbox0_cmd(struct dmub_srv *dmub, union dmub_inbox0_data_register data)
{
	REG_WRITE(DMCUB_INBOX0_WPTR, data.inbox0_cmd_common.all);
}

void dmub_dcn401_clear_inbox0_ack_register(struct dmub_srv *dmub)
{
	REG_WRITE(DMCUB_SCRATCH17, 0);
}

uint32_t dmub_dcn401_read_inbox0_ack_register(struct dmub_srv *dmub)
{
	return REG_READ(DMCUB_SCRATCH17);
}

void dmub_dcn401_send_reg_inbox0_cmd_msg(struct dmub_srv *dmub,
		union dmub_rb_cmd *cmd)
{
	uint32_t *dwords = (uint32_t *)cmd;

	static_assert(sizeof(*cmd) == 64, "DMUB command size mismatch");

	REG_WRITE(DMCUB_REG_INBOX0_MSG0, dwords[0]);
	REG_WRITE(DMCUB_REG_INBOX0_MSG1, dwords[1]);
	REG_WRITE(DMCUB_REG_INBOX0_MSG2, dwords[2]);
	REG_WRITE(DMCUB_REG_INBOX0_MSG3, dwords[3]);
	REG_WRITE(DMCUB_REG_INBOX0_MSG4, dwords[4]);
	REG_WRITE(DMCUB_REG_INBOX0_MSG5, dwords[5]);
	REG_WRITE(DMCUB_REG_INBOX0_MSG6, dwords[6]);
	REG_WRITE(DMCUB_REG_INBOX0_MSG7, dwords[7]);
	REG_WRITE(DMCUB_REG_INBOX0_MSG8, dwords[8]);
	REG_WRITE(DMCUB_REG_INBOX0_MSG9, dwords[9]);
	REG_WRITE(DMCUB_REG_INBOX0_MSG10, dwords[10]);
	REG_WRITE(DMCUB_REG_INBOX0_MSG11, dwords[11]);
	REG_WRITE(DMCUB_REG_INBOX0_MSG12, dwords[12]);
	REG_WRITE(DMCUB_REG_INBOX0_MSG13, dwords[13]);
	REG_WRITE(DMCUB_REG_INBOX0_MSG14, dwords[14]);
	/* writing to INBOX RDY register will trigger DMUB REG INBOX0 RDY
	 * interrupt.
	 */
	REG_WRITE(DMCUB_REG_INBOX0_RDY, dwords[15]);
}

uint32_t dmub_dcn401_read_reg_inbox0_rsp_int_status(struct dmub_srv *dmub)
{
	uint32_t status;

	REG_GET(HOST_INTERRUPT_CSR, HOST_REG_INBOX0_RSP_INT_STAT, &status);
	return status;
}

void dmub_dcn401_read_reg_inbox0_cmd_rsp(struct dmub_srv *dmub,
		union dmub_rb_cmd *cmd)
{
	uint32_t *dwords = (uint32_t *)cmd;

	static_assert(sizeof(*cmd) == 64, "DMUB command size mismatch");

	dwords[0] = REG_READ(DMCUB_REG_INBOX0_MSG0);
	dwords[1] = REG_READ(DMCUB_REG_INBOX0_MSG1);
	dwords[2] = REG_READ(DMCUB_REG_INBOX0_MSG2);
	dwords[3] = REG_READ(DMCUB_REG_INBOX0_MSG3);
	dwords[4] = REG_READ(DMCUB_REG_INBOX0_MSG4);
	dwords[5] = REG_READ(DMCUB_REG_INBOX0_MSG5);
	dwords[6] = REG_READ(DMCUB_REG_INBOX0_MSG6);
	dwords[7] = REG_READ(DMCUB_REG_INBOX0_MSG7);
	dwords[8] = REG_READ(DMCUB_REG_INBOX0_MSG8);
	dwords[9] = REG_READ(DMCUB_REG_INBOX0_MSG9);
	dwords[10] = REG_READ(DMCUB_REG_INBOX0_MSG10);
	dwords[11] = REG_READ(DMCUB_REG_INBOX0_MSG11);
	dwords[12] = REG_READ(DMCUB_REG_INBOX0_MSG12);
	dwords[13] = REG_READ(DMCUB_REG_INBOX0_MSG13);
	dwords[14] = REG_READ(DMCUB_REG_INBOX0_MSG14);
	dwords[15] = REG_READ(DMCUB_REG_INBOX0_RSP);
}

void dmub_dcn401_write_reg_inbox0_rsp_int_ack(struct dmub_srv *dmub)
{
	REG_UPDATE(HOST_INTERRUPT_CSR, HOST_REG_INBOX0_RSP_INT_ACK, 1);
	REG_UPDATE(HOST_INTERRUPT_CSR, HOST_REG_INBOX0_RSP_INT_ACK, 0);
}

void dmub_dcn401_write_reg_outbox0_rdy_int_ack(struct dmub_srv *dmub)
{
	REG_UPDATE(HOST_INTERRUPT_CSR, HOST_REG_OUTBOX0_RDY_INT_ACK, 1);
	REG_UPDATE(HOST_INTERRUPT_CSR, HOST_REG_OUTBOX0_RDY_INT_ACK, 0);
}

void dmub_dcn401_read_reg_outbox0_msg(struct dmub_srv *dmub, uint32_t *msg)
{
	*msg = REG_READ(DMCUB_REG_OUTBOX0_MSG0);
}

void dmub_dcn401_write_reg_outbox0_rsp(struct dmub_srv *dmub, uint32_t *rsp)
{
	REG_WRITE(DMCUB_REG_OUTBOX0_RSP, *rsp);
}

uint32_t dmub_dcn401_read_reg_outbox0_rsp_int_status(struct dmub_srv *dmub)
{
	uint32_t status;

	REG_GET(DMCUB_INTERRUPT_STATUS, DMCUB_REG_OUTBOX0_RSP_INT_STAT, &status);
	return status;
}

void dmub_dcn401_enable_reg_inbox0_rsp_int(struct dmub_srv *dmub, bool enable)
{
	REG_UPDATE(HOST_INTERRUPT_CSR, HOST_REG_INBOX0_RSP_INT_EN, enable ? 1:0);
}

void dmub_dcn401_enable_reg_outbox0_rdy_int(struct dmub_srv *dmub, bool enable)
{
	REG_UPDATE(HOST_INTERRUPT_CSR, HOST_REG_OUTBOX0_RDY_INT_EN, enable ? 1:0);
}

uint32_t dmub_dcn401_read_reg_outbox0_rdy_int_status(struct dmub_srv *dmub)
{
	uint32_t status;

	REG_GET(HOST_INTERRUPT_CSR, HOST_REG_OUTBOX0_RDY_INT_STAT, &status);
	return status;
}

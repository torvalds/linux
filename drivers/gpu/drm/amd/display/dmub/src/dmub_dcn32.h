/*
 * Copyright 2022 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#ifndef _DMUB_DCN32_H_
#define _DMUB_DCN32_H_

#include "dmub_dcn31.h"

struct dmub_srv;

/* DCN32 register definitions. */

#define DMUB_DCN32_REGS() \
	DMUB_SR(DMCUB_CNTL) \
	DMUB_SR(DMCUB_CNTL2) \
	DMUB_SR(DMCUB_SEC_CNTL) \
	DMUB_SR(DMCUB_INBOX0_SIZE) \
	DMUB_SR(DMCUB_INBOX0_RPTR) \
	DMUB_SR(DMCUB_INBOX0_WPTR) \
	DMUB_SR(DMCUB_INBOX1_BASE_ADDRESS) \
	DMUB_SR(DMCUB_INBOX1_SIZE) \
	DMUB_SR(DMCUB_INBOX1_RPTR) \
	DMUB_SR(DMCUB_INBOX1_WPTR) \
	DMUB_SR(DMCUB_OUTBOX0_BASE_ADDRESS) \
	DMUB_SR(DMCUB_OUTBOX0_SIZE) \
	DMUB_SR(DMCUB_OUTBOX0_RPTR) \
	DMUB_SR(DMCUB_OUTBOX0_WPTR) \
	DMUB_SR(DMCUB_OUTBOX1_BASE_ADDRESS) \
	DMUB_SR(DMCUB_OUTBOX1_SIZE) \
	DMUB_SR(DMCUB_OUTBOX1_RPTR) \
	DMUB_SR(DMCUB_OUTBOX1_WPTR) \
	DMUB_SR(DMCUB_REGION3_CW0_OFFSET) \
	DMUB_SR(DMCUB_REGION3_CW1_OFFSET) \
	DMUB_SR(DMCUB_REGION3_CW2_OFFSET) \
	DMUB_SR(DMCUB_REGION3_CW3_OFFSET) \
	DMUB_SR(DMCUB_REGION3_CW4_OFFSET) \
	DMUB_SR(DMCUB_REGION3_CW5_OFFSET) \
	DMUB_SR(DMCUB_REGION3_CW6_OFFSET) \
	DMUB_SR(DMCUB_REGION3_CW7_OFFSET) \
	DMUB_SR(DMCUB_REGION3_CW0_OFFSET_HIGH) \
	DMUB_SR(DMCUB_REGION3_CW1_OFFSET_HIGH) \
	DMUB_SR(DMCUB_REGION3_CW2_OFFSET_HIGH) \
	DMUB_SR(DMCUB_REGION3_CW3_OFFSET_HIGH) \
	DMUB_SR(DMCUB_REGION3_CW4_OFFSET_HIGH) \
	DMUB_SR(DMCUB_REGION3_CW5_OFFSET_HIGH) \
	DMUB_SR(DMCUB_REGION3_CW6_OFFSET_HIGH) \
	DMUB_SR(DMCUB_REGION3_CW7_OFFSET_HIGH) \
	DMUB_SR(DMCUB_REGION3_CW0_BASE_ADDRESS) \
	DMUB_SR(DMCUB_REGION3_CW1_BASE_ADDRESS) \
	DMUB_SR(DMCUB_REGION3_CW2_BASE_ADDRESS) \
	DMUB_SR(DMCUB_REGION3_CW3_BASE_ADDRESS) \
	DMUB_SR(DMCUB_REGION3_CW4_BASE_ADDRESS) \
	DMUB_SR(DMCUB_REGION3_CW5_BASE_ADDRESS) \
	DMUB_SR(DMCUB_REGION3_CW6_BASE_ADDRESS) \
	DMUB_SR(DMCUB_REGION3_CW7_BASE_ADDRESS) \
	DMUB_SR(DMCUB_REGION3_CW0_TOP_ADDRESS) \
	DMUB_SR(DMCUB_REGION3_CW1_TOP_ADDRESS) \
	DMUB_SR(DMCUB_REGION3_CW2_TOP_ADDRESS) \
	DMUB_SR(DMCUB_REGION3_CW3_TOP_ADDRESS) \
	DMUB_SR(DMCUB_REGION3_CW4_TOP_ADDRESS) \
	DMUB_SR(DMCUB_REGION3_CW5_TOP_ADDRESS) \
	DMUB_SR(DMCUB_REGION3_CW6_TOP_ADDRESS) \
	DMUB_SR(DMCUB_REGION3_CW7_TOP_ADDRESS) \
	DMUB_SR(DMCUB_REGION4_OFFSET) \
	DMUB_SR(DMCUB_REGION4_OFFSET_HIGH) \
	DMUB_SR(DMCUB_REGION4_TOP_ADDRESS) \
	DMUB_SR(DMCUB_REGION5_OFFSET) \
	DMUB_SR(DMCUB_REGION5_OFFSET_HIGH) \
	DMUB_SR(DMCUB_REGION5_TOP_ADDRESS) \
	DMUB_SR(DMCUB_SCRATCH0) \
	DMUB_SR(DMCUB_SCRATCH1) \
	DMUB_SR(DMCUB_SCRATCH2) \
	DMUB_SR(DMCUB_SCRATCH3) \
	DMUB_SR(DMCUB_SCRATCH4) \
	DMUB_SR(DMCUB_SCRATCH5) \
	DMUB_SR(DMCUB_SCRATCH6) \
	DMUB_SR(DMCUB_SCRATCH7) \
	DMUB_SR(DMCUB_SCRATCH8) \
	DMUB_SR(DMCUB_SCRATCH9) \
	DMUB_SR(DMCUB_SCRATCH10) \
	DMUB_SR(DMCUB_SCRATCH11) \
	DMUB_SR(DMCUB_SCRATCH12) \
	DMUB_SR(DMCUB_SCRATCH13) \
	DMUB_SR(DMCUB_SCRATCH14) \
	DMUB_SR(DMCUB_SCRATCH15) \
	DMUB_SR(DMCUB_SCRATCH16) \
	DMUB_SR(DMCUB_SCRATCH17) \
	DMUB_SR(DMCUB_GPINT_DATAIN1) \
	DMUB_SR(DMCUB_GPINT_DATAOUT) \
	DMUB_SR(CC_DC_PIPE_DIS) \
	DMUB_SR(MMHUBBUB_SOFT_RESET) \
	DMUB_SR(DCN_VM_FB_LOCATION_BASE) \
	DMUB_SR(DCN_VM_FB_OFFSET) \
	DMUB_SR(DMCUB_TIMER_CURRENT) \
	DMUB_SR(DMCUB_INST_FETCH_FAULT_ADDR) \
	DMUB_SR(DMCUB_UNDEFINED_ADDRESS_FAULT_ADDR) \
	DMUB_SR(DMCUB_DATA_WRITE_FAULT_ADDR) \
	DMUB_SR(DMCUB_REGION3_TMR_AXI_SPACE) \
	DMUB_SR(DMCUB_INTERRUPT_ENABLE) \
	DMUB_SR(DMCUB_INTERRUPT_ACK)

#define DMUB_DCN32_FIELDS() \
	DMUB_SF(DMCUB_CNTL, DMCUB_ENABLE) \
	DMUB_SF(DMCUB_CNTL, DMCUB_TRACEPORT_EN) \
	DMUB_SF(DMCUB_CNTL2, DMCUB_SOFT_RESET) \
	DMUB_SF(DMCUB_SEC_CNTL, DMCUB_SEC_RESET) \
	DMUB_SF(DMCUB_SEC_CNTL, DMCUB_MEM_UNIT_ID) \
	DMUB_SF(DMCUB_SEC_CNTL, DMCUB_SEC_RESET_STATUS) \
	DMUB_SF(DMCUB_REGION3_CW0_TOP_ADDRESS, DMCUB_REGION3_CW0_TOP_ADDRESS) \
	DMUB_SF(DMCUB_REGION3_CW0_TOP_ADDRESS, DMCUB_REGION3_CW0_ENABLE) \
	DMUB_SF(DMCUB_REGION3_CW1_TOP_ADDRESS, DMCUB_REGION3_CW1_TOP_ADDRESS) \
	DMUB_SF(DMCUB_REGION3_CW1_TOP_ADDRESS, DMCUB_REGION3_CW1_ENABLE) \
	DMUB_SF(DMCUB_REGION3_CW2_TOP_ADDRESS, DMCUB_REGION3_CW2_TOP_ADDRESS) \
	DMUB_SF(DMCUB_REGION3_CW2_TOP_ADDRESS, DMCUB_REGION3_CW2_ENABLE) \
	DMUB_SF(DMCUB_REGION3_CW3_TOP_ADDRESS, DMCUB_REGION3_CW3_TOP_ADDRESS) \
	DMUB_SF(DMCUB_REGION3_CW3_TOP_ADDRESS, DMCUB_REGION3_CW3_ENABLE) \
	DMUB_SF(DMCUB_REGION3_CW4_TOP_ADDRESS, DMCUB_REGION3_CW4_TOP_ADDRESS) \
	DMUB_SF(DMCUB_REGION3_CW4_TOP_ADDRESS, DMCUB_REGION3_CW4_ENABLE) \
	DMUB_SF(DMCUB_REGION3_CW5_TOP_ADDRESS, DMCUB_REGION3_CW5_TOP_ADDRESS) \
	DMUB_SF(DMCUB_REGION3_CW5_TOP_ADDRESS, DMCUB_REGION3_CW5_ENABLE) \
	DMUB_SF(DMCUB_REGION3_CW6_TOP_ADDRESS, DMCUB_REGION3_CW6_TOP_ADDRESS) \
	DMUB_SF(DMCUB_REGION3_CW6_TOP_ADDRESS, DMCUB_REGION3_CW6_ENABLE) \
	DMUB_SF(DMCUB_REGION3_CW7_TOP_ADDRESS, DMCUB_REGION3_CW7_TOP_ADDRESS) \
	DMUB_SF(DMCUB_REGION3_CW7_TOP_ADDRESS, DMCUB_REGION3_CW7_ENABLE) \
	DMUB_SF(DMCUB_REGION4_TOP_ADDRESS, DMCUB_REGION4_TOP_ADDRESS) \
	DMUB_SF(DMCUB_REGION4_TOP_ADDRESS, DMCUB_REGION4_ENABLE) \
	DMUB_SF(DMCUB_REGION5_TOP_ADDRESS, DMCUB_REGION5_TOP_ADDRESS) \
	DMUB_SF(DMCUB_REGION5_TOP_ADDRESS, DMCUB_REGION5_ENABLE) \
	DMUB_SF(CC_DC_PIPE_DIS, DC_DMCUB_ENABLE) \
	DMUB_SF(MMHUBBUB_SOFT_RESET, DMUIF_SOFT_RESET) \
	DMUB_SF(DCN_VM_FB_LOCATION_BASE, FB_BASE) \
	DMUB_SF(DCN_VM_FB_OFFSET, FB_OFFSET) \
	DMUB_SF(DMCUB_INBOX0_WPTR, DMCUB_INBOX0_WPTR) \
	DMUB_SF(DMCUB_REGION3_TMR_AXI_SPACE, DMCUB_REGION3_TMR_AXI_SPACE) \
	DMUB_SF(DMCUB_INTERRUPT_ENABLE, DMCUB_GPINT_IH_INT_EN) \
	DMUB_SF(DMCUB_INTERRUPT_ACK, DMCUB_GPINT_IH_INT_ACK)

struct dmub_srv_dcn32_reg_offset {
#define DMUB_SR(reg) uint32_t reg;
	DMUB_DCN32_REGS()
	DMCUB_INTERNAL_REGS()
#undef DMUB_SR
};

struct dmub_srv_dcn32_reg_shift {
#define DMUB_SF(reg, field) uint8_t reg##__##field;
	DMUB_DCN32_FIELDS()
#undef DMUB_SF
};

struct dmub_srv_dcn32_reg_mask {
#define DMUB_SF(reg, field) uint32_t reg##__##field;
	DMUB_DCN32_FIELDS()
#undef DMUB_SF
};

struct dmub_srv_dcn32_regs {
	const struct dmub_srv_dcn32_reg_offset offset;
	const struct dmub_srv_dcn32_reg_mask mask;
	const struct dmub_srv_dcn32_reg_shift shift;
};

extern const struct dmub_srv_dcn32_regs dmub_srv_dcn32_regs;

void dmub_dcn32_reset(struct dmub_srv *dmub);

void dmub_dcn32_reset_release(struct dmub_srv *dmub);

void dmub_dcn32_backdoor_load(struct dmub_srv *dmub,
			      const struct dmub_window *cw0,
			      const struct dmub_window *cw1);

void dmub_dcn32_backdoor_load_zfb_mode(struct dmub_srv *dmub,
		      const struct dmub_window *cw0,
		      const struct dmub_window *cw1);

void dmub_dcn32_setup_windows(struct dmub_srv *dmub,
			      const struct dmub_window *cw2,
			      const struct dmub_window *cw3,
			      const struct dmub_window *cw4,
			      const struct dmub_window *cw5,
			      const struct dmub_window *cw6);

void dmub_dcn32_setup_mailbox(struct dmub_srv *dmub,
			      const struct dmub_region *inbox1);

uint32_t dmub_dcn32_get_inbox1_rptr(struct dmub_srv *dmub);

void dmub_dcn32_set_inbox1_wptr(struct dmub_srv *dmub, uint32_t wptr_offset);

void dmub_dcn32_setup_out_mailbox(struct dmub_srv *dmub,
			      const struct dmub_region *outbox1);

uint32_t dmub_dcn32_get_outbox1_wptr(struct dmub_srv *dmub);

void dmub_dcn32_set_outbox1_rptr(struct dmub_srv *dmub, uint32_t rptr_offset);

bool dmub_dcn32_is_hw_init(struct dmub_srv *dmub);

bool dmub_dcn32_is_supported(struct dmub_srv *dmub);

void dmub_dcn32_set_gpint(struct dmub_srv *dmub,
			  union dmub_gpint_data_register reg);

bool dmub_dcn32_is_gpint_acked(struct dmub_srv *dmub,
			       union dmub_gpint_data_register reg);

uint32_t dmub_dcn32_get_gpint_response(struct dmub_srv *dmub);

uint32_t dmub_dcn32_get_gpint_dataout(struct dmub_srv *dmub);

void dmub_dcn32_enable_dmub_boot_options(struct dmub_srv *dmub, const struct dmub_srv_hw_params *params);

void dmub_dcn32_skip_dmub_panel_power_sequence(struct dmub_srv *dmub, bool skip);

union dmub_fw_boot_status dmub_dcn32_get_fw_boot_status(struct dmub_srv *dmub);

void dmub_dcn32_setup_outbox0(struct dmub_srv *dmub,
			      const struct dmub_region *outbox0);

uint32_t dmub_dcn32_get_outbox0_wptr(struct dmub_srv *dmub);

void dmub_dcn32_set_outbox0_rptr(struct dmub_srv *dmub, uint32_t rptr_offset);

uint32_t dmub_dcn32_get_current_time(struct dmub_srv *dmub);

void dmub_dcn32_get_diagnostic_data(struct dmub_srv *dmub, struct dmub_diagnostic_data *diag_data);

void dmub_dcn32_configure_dmub_in_system_memory(struct dmub_srv *dmub);
void dmub_dcn32_send_inbox0_cmd(struct dmub_srv *dmub, union dmub_inbox0_data_register data);
void dmub_dcn32_clear_inbox0_ack_register(struct dmub_srv *dmub);
uint32_t dmub_dcn32_read_inbox0_ack_register(struct dmub_srv *dmub);

#endif /* _DMUB_DCN32_H_ */

/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2026 Advanced Micro Devices, Inc.
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
 */

#ifndef _DMUB_DCN42_H_
#define _DMUB_DCN42_H_

#include "dmub_dcn35.h"
#include "dmub_dcn401.h"


struct dmub_srv;

/* DCN42 register definitions. */

#define DMUB_DCN42_REGS() \
	DMUB_DCN35_REGS() \
	DMUB_SR(DMCUB_INTERRUPT_STATUS) \
	DMUB_SR(DMCUB_REG_INBOX0_RDY) \
	DMUB_SR(DMCUB_REG_INBOX0_MSG0) \
	DMUB_SR(DMCUB_REG_INBOX0_MSG1) \
	DMUB_SR(DMCUB_REG_INBOX0_MSG2) \
	DMUB_SR(DMCUB_REG_INBOX0_MSG3) \
	DMUB_SR(DMCUB_REG_INBOX0_MSG4) \
	DMUB_SR(DMCUB_REG_INBOX0_MSG5) \
	DMUB_SR(DMCUB_REG_INBOX0_MSG6) \
	DMUB_SR(DMCUB_REG_INBOX0_MSG7) \
	DMUB_SR(DMCUB_REG_INBOX0_MSG8) \
	DMUB_SR(DMCUB_REG_INBOX0_MSG9) \
	DMUB_SR(DMCUB_REG_INBOX0_MSG10) \
	DMUB_SR(DMCUB_REG_INBOX0_MSG11) \
	DMUB_SR(DMCUB_REG_INBOX0_MSG12) \
	DMUB_SR(DMCUB_REG_INBOX0_MSG13) \
	DMUB_SR(DMCUB_REG_INBOX0_MSG14) \
	DMUB_SR(DMCUB_REG_INBOX0_RSP) \
	DMUB_SR(DMCUB_REG_OUTBOX0_RDY) \
	DMUB_SR(DMCUB_REG_OUTBOX0_MSG0) \
	DMUB_SR(DMCUB_REG_OUTBOX0_RSP) \
	DMUB_SR(HOST_INTERRUPT_CSR)

#define DMUB_DCN42_FIELDS() \
	DMUB_DCN35_FIELDS() \
	DMUB_SF(DMCUB_INTERRUPT_STATUS, DMCUB_REG_OUTBOX0_RSP_INT_STAT) \
	DMUB_SF(HOST_INTERRUPT_CSR, HOST_REG_INBOX0_RSP_INT_ACK) \
	DMUB_SF(HOST_INTERRUPT_CSR, HOST_REG_INBOX0_RSP_INT_STAT) \
	DMUB_SF(HOST_INTERRUPT_CSR, HOST_REG_INBOX0_RSP_INT_EN) \
	DMUB_SF(HOST_INTERRUPT_CSR, HOST_REG_OUTBOX0_RDY_INT_ACK) \
	DMUB_SF(HOST_INTERRUPT_CSR, HOST_REG_OUTBOX0_RDY_INT_STAT) \
	DMUB_SF(HOST_INTERRUPT_CSR, HOST_REG_OUTBOX0_RDY_INT_EN)

struct dmub_srv_dcn42_reg_offset {
#define DMUB_SR(reg) uint32_t reg;
	DMUB_DCN42_REGS()
	DMCUB_INTERNAL_REGS()
#undef DMUB_SR
};

struct dmub_srv_dcn42_reg_shift {
#define DMUB_SF(reg, field) uint8_t reg##__##field;
	DMUB_DCN42_FIELDS()
#undef DMUB_SF
};

struct dmub_srv_dcn42_reg_mask {
#define DMUB_SF(reg, field) uint32_t reg##__##field;
	DMUB_DCN42_FIELDS()
#undef DMUB_SF
};

struct dmub_srv_dcn42_regs {
	struct dmub_srv_dcn42_reg_offset offset;
	struct dmub_srv_dcn42_reg_mask mask;
	struct dmub_srv_dcn42_reg_shift shift;
};

/* Function declarations */

/* Initialization and configuration */
void dmub_srv_dcn42_regs_init(struct dmub_srv *dmub, struct dc_context *ctx);
void dmub_dcn42_enable_dmub_boot_options(struct dmub_srv *dmub, const struct dmub_srv_hw_params *params);
void dmub_dcn42_skip_dmub_panel_power_sequence(struct dmub_srv *dmub, bool skip);
void dmub_dcn42_configure_dmub_in_system_memory(struct dmub_srv *dmub);

/* Reset and control */
void dmub_dcn42_reset(struct dmub_srv *dmub);
void dmub_dcn42_reset_release(struct dmub_srv *dmub);

/* Firmware loading */
void dmub_dcn42_backdoor_load(struct dmub_srv *dmub, const struct dmub_window *cw0, const struct dmub_window *cw1);
void dmub_dcn42_backdoor_load_zfb_mode(struct dmub_srv *dmub, const struct dmub_window *cw0, const struct dmub_window *cw1);
void dmub_dcn42_setup_windows(struct dmub_srv *dmub, const struct dmub_window *cw2, const struct dmub_window *cw3, const struct dmub_window *cw4, const struct dmub_window *cw5, const struct dmub_window *cw6, const struct dmub_window *region6);

/* Mailbox operations - Inbox1 */
void dmub_dcn42_setup_mailbox(struct dmub_srv *dmub, const struct dmub_region *inbox1);
uint32_t dmub_dcn42_get_inbox1_wptr(struct dmub_srv *dmub);
uint32_t dmub_dcn42_get_inbox1_rptr(struct dmub_srv *dmub);
void dmub_dcn42_set_inbox1_wptr(struct dmub_srv *dmub, uint32_t wptr_offset);

/* Mailbox operations - Outbox1 */
void dmub_dcn42_setup_out_mailbox(struct dmub_srv *dmub, const struct dmub_region *outbox1);
uint32_t dmub_dcn42_get_outbox1_wptr(struct dmub_srv *dmub);
void dmub_dcn42_set_outbox1_rptr(struct dmub_srv *dmub, uint32_t rptr_offset);

/* Mailbox operations - Outbox0 */
void dmub_dcn42_setup_outbox0(struct dmub_srv *dmub, const struct dmub_region *outbox0);
uint32_t dmub_dcn42_get_outbox0_wptr(struct dmub_srv *dmub);
void dmub_dcn42_set_outbox0_rptr(struct dmub_srv *dmub, uint32_t rptr_offset);

/* Mailbox operations - Inbox0 */
void dmub_dcn42_send_inbox0_cmd(struct dmub_srv *dmub, union dmub_inbox0_data_register data);
void dmub_dcn42_clear_inbox0_ack_register(struct dmub_srv *dmub);
uint32_t dmub_dcn42_read_inbox0_ack_register(struct dmub_srv *dmub);

/* REG Inbox0/Outbox0 operations */
void dmub_dcn42_send_reg_inbox0_cmd_msg(struct dmub_srv *dmub, union dmub_rb_cmd *cmd);
uint32_t dmub_dcn42_read_reg_inbox0_rsp_int_status(struct dmub_srv *dmub);
void dmub_dcn42_read_reg_inbox0_cmd_rsp(struct dmub_srv *dmub, union dmub_rb_cmd *cmd);
void dmub_dcn42_write_reg_inbox0_rsp_int_ack(struct dmub_srv *dmub);
void dmub_dcn42_clear_reg_inbox0_rsp_int_ack(struct dmub_srv *dmub);
void dmub_dcn42_enable_reg_inbox0_rsp_int(struct dmub_srv *dmub, bool enable);

void dmub_dcn42_write_reg_outbox0_rdy_int_ack(struct dmub_srv *dmub);
void dmub_dcn42_read_reg_outbox0_msg(struct dmub_srv *dmub, uint32_t *msg);
void dmub_dcn42_write_reg_outbox0_rsp(struct dmub_srv *dmub, uint32_t *rsp);
uint32_t dmub_dcn42_read_reg_outbox0_rsp_int_status(struct dmub_srv *dmub);
void dmub_dcn42_enable_reg_outbox0_rdy_int(struct dmub_srv *dmub, bool enable);
uint32_t dmub_dcn42_read_reg_outbox0_rdy_int_status(struct dmub_srv *dmub);

/* GPINT operations */
void dmub_dcn42_set_gpint(struct dmub_srv *dmub, union dmub_gpint_data_register reg);
bool dmub_dcn42_is_gpint_acked(struct dmub_srv *dmub, union dmub_gpint_data_register reg);
uint32_t dmub_dcn42_get_gpint_response(struct dmub_srv *dmub);
uint32_t dmub_dcn42_get_gpint_dataout(struct dmub_srv *dmub);

/* Status and detection */
bool dmub_dcn42_is_hw_init(struct dmub_srv *dmub);
bool dmub_dcn42_is_supported(struct dmub_srv *dmub);
bool dmub_dcn42_is_hw_powered_up(struct dmub_srv *dmub);
bool dmub_dcn42_should_detect(struct dmub_srv *dmub);


/* Firmware boot status and options */
union dmub_fw_boot_status dmub_dcn42_get_fw_boot_status(struct dmub_srv *dmub);
union dmub_fw_boot_options dmub_dcn42_get_fw_boot_option(struct dmub_srv *dmub);

/* Timing and diagnostics */
uint32_t dmub_dcn42_get_current_time(struct dmub_srv *dmub);
void dmub_dcn42_get_diagnostic_data(struct dmub_srv *dmub);
bool dmub_dcn42_get_preos_fw_info(struct dmub_srv *dmub);

#endif /* _DMUB_DCN42_H_ */

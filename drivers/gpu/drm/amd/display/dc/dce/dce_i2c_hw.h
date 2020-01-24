/*
 * Copyright 2018 Advanced Micro Devices, Inc.
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

#ifndef __DCE_I2C_HW_H__
#define __DCE_I2C_HW_H__

enum dc_i2c_status {
	DC_I2C_STATUS__DC_I2C_STATUS_IDLE,
	DC_I2C_STATUS__DC_I2C_STATUS_USED_BY_SW,
	DC_I2C_STATUS__DC_I2C_STATUS_USED_BY_HW,
	DC_I2C_REG_RW_CNTL_STATUS_DMCU_ONLY = 2,
};

enum dc_i2c_arbitration {
	DC_I2C_ARBITRATION__DC_I2C_SW_PRIORITY_NORMAL,
	DC_I2C_ARBITRATION__DC_I2C_SW_PRIORITY_HIGH
};

enum i2c_channel_operation_result {
	I2C_CHANNEL_OPERATION_SUCCEEDED,
	I2C_CHANNEL_OPERATION_FAILED,
	I2C_CHANNEL_OPERATION_NOT_GRANTED,
	I2C_CHANNEL_OPERATION_IS_BUSY,
	I2C_CHANNEL_OPERATION_NO_HANDLE_PROVIDED,
	I2C_CHANNEL_OPERATION_CHANNEL_IN_USE,
	I2C_CHANNEL_OPERATION_CHANNEL_CLIENT_MAX_ALLOWED,
	I2C_CHANNEL_OPERATION_ENGINE_BUSY,
	I2C_CHANNEL_OPERATION_TIMEOUT,
	I2C_CHANNEL_OPERATION_NO_RESPONSE,
	I2C_CHANNEL_OPERATION_HW_REQUEST_I2C_BUS,
	I2C_CHANNEL_OPERATION_WRONG_PARAMETER,
	I2C_CHANNEL_OPERATION_OUT_NB_OF_RETRIES,
	I2C_CHANNEL_OPERATION_NOT_STARTED
};


enum dce_i2c_transaction_action {
	DCE_I2C_TRANSACTION_ACTION_I2C_WRITE = 0x00,
	DCE_I2C_TRANSACTION_ACTION_I2C_READ = 0x10,
	DCE_I2C_TRANSACTION_ACTION_I2C_STATUS_REQUEST = 0x20,

	DCE_I2C_TRANSACTION_ACTION_I2C_WRITE_MOT = 0x40,
	DCE_I2C_TRANSACTION_ACTION_I2C_READ_MOT = 0x50,
	DCE_I2C_TRANSACTION_ACTION_I2C_STATUS_REQUEST_MOT = 0x60,

	DCE_I2C_TRANSACTION_ACTION_DP_WRITE = 0x80,
	DCE_I2C_TRANSACTION_ACTION_DP_READ = 0x90
};

enum {
	I2C_SETUP_TIME_LIMIT_DCE = 255,
	I2C_SETUP_TIME_LIMIT_DCN = 3,
	I2C_HW_BUFFER_SIZE_DCE100 = 538,
	I2C_HW_BUFFER_SIZE_DCE = 144,
	I2C_SEND_RESET_LENGTH_9 = 9,
	I2C_SEND_RESET_LENGTH_10 = 10,
	DEFAULT_I2C_HW_SPEED = 50,
	DEFAULT_I2C_HW_SPEED_100KHZ = 100,
	TRANSACTION_TIMEOUT_IN_I2C_CLOCKS = 32,
};

#define I2C_HW_ENGINE_COMMON_REG_LIST(id)\
	SRI(SETUP, DC_I2C_DDC, id),\
	SRI(SPEED, DC_I2C_DDC, id),\
	SRI(HW_STATUS, DC_I2C_DDC, id),\
	SR(DC_I2C_ARBITRATION),\
	SR(DC_I2C_CONTROL),\
	SR(DC_I2C_SW_STATUS),\
	SR(DC_I2C_TRANSACTION0),\
	SR(DC_I2C_TRANSACTION1),\
	SR(DC_I2C_TRANSACTION2),\
	SR(DC_I2C_TRANSACTION3),\
	SR(DC_I2C_DATA),\
	SR(MICROSECOND_TIME_BASE_DIV)

#define I2C_SF(reg_name, field_name, post_fix)\
	.field_name = reg_name ## __ ## field_name ## post_fix

#define I2C_COMMON_MASK_SH_LIST_DCE_COMMON_BASE(mask_sh)\
	I2C_SF(DC_I2C_DDC1_SETUP, DC_I2C_DDC1_ENABLE, mask_sh),\
	I2C_SF(DC_I2C_DDC1_SETUP, DC_I2C_DDC1_TIME_LIMIT, mask_sh),\
	I2C_SF(DC_I2C_DDC1_SETUP, DC_I2C_DDC1_DATA_DRIVE_EN, mask_sh),\
	I2C_SF(DC_I2C_DDC1_SETUP, DC_I2C_DDC1_CLK_DRIVE_EN, mask_sh),\
	I2C_SF(DC_I2C_DDC1_SETUP, DC_I2C_DDC1_DATA_DRIVE_SEL, mask_sh),\
	I2C_SF(DC_I2C_DDC1_SETUP, DC_I2C_DDC1_INTRA_TRANSACTION_DELAY, mask_sh),\
	I2C_SF(DC_I2C_DDC1_SETUP, DC_I2C_DDC1_INTRA_BYTE_DELAY, mask_sh),\
	I2C_SF(DC_I2C_DDC1_HW_STATUS, DC_I2C_DDC1_HW_STATUS, mask_sh),\
	I2C_SF(DC_I2C_ARBITRATION, DC_I2C_SW_USE_I2C_REG_REQ, mask_sh),\
	I2C_SF(DC_I2C_ARBITRATION, DC_I2C_SW_DONE_USING_I2C_REG, mask_sh),\
	I2C_SF(DC_I2C_ARBITRATION, DC_I2C_NO_QUEUED_SW_GO, mask_sh),\
	I2C_SF(DC_I2C_ARBITRATION, DC_I2C_SW_PRIORITY, mask_sh),\
	I2C_SF(DC_I2C_CONTROL, DC_I2C_SOFT_RESET, mask_sh),\
	I2C_SF(DC_I2C_CONTROL, DC_I2C_SW_STATUS_RESET, mask_sh),\
	I2C_SF(DC_I2C_CONTROL, DC_I2C_GO, mask_sh),\
	I2C_SF(DC_I2C_CONTROL, DC_I2C_SEND_RESET, mask_sh),\
	I2C_SF(DC_I2C_CONTROL, DC_I2C_TRANSACTION_COUNT, mask_sh),\
	I2C_SF(DC_I2C_CONTROL, DC_I2C_DDC_SELECT, mask_sh),\
	I2C_SF(DC_I2C_DDC1_SPEED, DC_I2C_DDC1_PRESCALE, mask_sh),\
	I2C_SF(DC_I2C_DDC1_SPEED, DC_I2C_DDC1_THRESHOLD, mask_sh),\
	I2C_SF(DC_I2C_SW_STATUS, DC_I2C_SW_STOPPED_ON_NACK, mask_sh),\
	I2C_SF(DC_I2C_SW_STATUS, DC_I2C_SW_TIMEOUT, mask_sh),\
	I2C_SF(DC_I2C_SW_STATUS, DC_I2C_SW_ABORTED, mask_sh),\
	I2C_SF(DC_I2C_SW_STATUS, DC_I2C_SW_DONE, mask_sh),\
	I2C_SF(DC_I2C_SW_STATUS, DC_I2C_SW_STATUS, mask_sh),\
	I2C_SF(DC_I2C_TRANSACTION0, DC_I2C_STOP_ON_NACK0, mask_sh),\
	I2C_SF(DC_I2C_TRANSACTION0, DC_I2C_START0, mask_sh),\
	I2C_SF(DC_I2C_TRANSACTION0, DC_I2C_RW0, mask_sh),\
	I2C_SF(DC_I2C_TRANSACTION0, DC_I2C_STOP0, mask_sh),\
	I2C_SF(DC_I2C_TRANSACTION0, DC_I2C_COUNT0, mask_sh),\
	I2C_SF(DC_I2C_DATA, DC_I2C_DATA_RW, mask_sh),\
	I2C_SF(DC_I2C_DATA, DC_I2C_DATA, mask_sh),\
	I2C_SF(DC_I2C_DATA, DC_I2C_INDEX, mask_sh),\
	I2C_SF(DC_I2C_DATA, DC_I2C_INDEX_WRITE, mask_sh),\
	I2C_SF(MICROSECOND_TIME_BASE_DIV, XTAL_REF_DIV, mask_sh),\
	I2C_SF(DC_I2C_ARBITRATION, DC_I2C_REG_RW_CNTL_STATUS, mask_sh)

#define I2C_COMMON_MASK_SH_LIST_DCE110(mask_sh)\
	I2C_COMMON_MASK_SH_LIST_DCE_COMMON_BASE(mask_sh),\
	I2C_SF(DC_I2C_DDC1_SPEED, DC_I2C_DDC1_START_STOP_TIMING_CNTL, mask_sh)

struct dce_i2c_shift {
	uint8_t DC_I2C_DDC1_ENABLE;
	uint8_t DC_I2C_DDC1_TIME_LIMIT;
	uint8_t DC_I2C_DDC1_DATA_DRIVE_EN;
	uint8_t DC_I2C_DDC1_CLK_DRIVE_EN;
	uint8_t DC_I2C_DDC1_DATA_DRIVE_SEL;
	uint8_t DC_I2C_DDC1_INTRA_TRANSACTION_DELAY;
	uint8_t DC_I2C_DDC1_INTRA_BYTE_DELAY;
	uint8_t DC_I2C_DDC1_HW_STATUS;
	uint8_t DC_I2C_SW_DONE_USING_I2C_REG;
	uint8_t DC_I2C_SW_USE_I2C_REG_REQ;
	uint8_t DC_I2C_NO_QUEUED_SW_GO;
	uint8_t DC_I2C_SW_PRIORITY;
	uint8_t DC_I2C_SOFT_RESET;
	uint8_t DC_I2C_SW_STATUS_RESET;
	uint8_t DC_I2C_GO;
	uint8_t DC_I2C_SEND_RESET;
	uint8_t DC_I2C_TRANSACTION_COUNT;
	uint8_t DC_I2C_DDC_SELECT;
	uint8_t DC_I2C_DDC1_PRESCALE;
	uint8_t DC_I2C_DDC1_THRESHOLD;
	uint8_t DC_I2C_DDC1_START_STOP_TIMING_CNTL;
	uint8_t DC_I2C_SW_STOPPED_ON_NACK;
	uint8_t DC_I2C_SW_TIMEOUT;
	uint8_t DC_I2C_SW_ABORTED;
	uint8_t DC_I2C_SW_DONE;
	uint8_t DC_I2C_SW_STATUS;
	uint8_t DC_I2C_STOP_ON_NACK0;
	uint8_t DC_I2C_START0;
	uint8_t DC_I2C_RW0;
	uint8_t DC_I2C_STOP0;
	uint8_t DC_I2C_COUNT0;
	uint8_t DC_I2C_DATA_RW;
	uint8_t DC_I2C_DATA;
	uint8_t DC_I2C_INDEX;
	uint8_t DC_I2C_INDEX_WRITE;
	uint8_t XTAL_REF_DIV;
#if defined(CONFIG_DRM_AMD_DC_DCN2_0)
	uint8_t DC_I2C_DDC1_SEND_RESET_LENGTH;
#endif
	uint8_t DC_I2C_REG_RW_CNTL_STATUS;
};

struct dce_i2c_mask {
	uint32_t DC_I2C_DDC1_ENABLE;
	uint32_t DC_I2C_DDC1_TIME_LIMIT;
	uint32_t DC_I2C_DDC1_DATA_DRIVE_EN;
	uint32_t DC_I2C_DDC1_CLK_DRIVE_EN;
	uint32_t DC_I2C_DDC1_DATA_DRIVE_SEL;
	uint32_t DC_I2C_DDC1_INTRA_TRANSACTION_DELAY;
	uint32_t DC_I2C_DDC1_INTRA_BYTE_DELAY;
	uint32_t DC_I2C_DDC1_HW_STATUS;
	uint32_t DC_I2C_SW_DONE_USING_I2C_REG;
	uint32_t DC_I2C_SW_USE_I2C_REG_REQ;
	uint32_t DC_I2C_NO_QUEUED_SW_GO;
	uint32_t DC_I2C_SW_PRIORITY;
	uint32_t DC_I2C_SOFT_RESET;
	uint32_t DC_I2C_SW_STATUS_RESET;
	uint32_t DC_I2C_GO;
	uint32_t DC_I2C_SEND_RESET;
	uint32_t DC_I2C_TRANSACTION_COUNT;
	uint32_t DC_I2C_DDC_SELECT;
	uint32_t DC_I2C_DDC1_PRESCALE;
	uint32_t DC_I2C_DDC1_THRESHOLD;
	uint32_t DC_I2C_DDC1_START_STOP_TIMING_CNTL;
	uint32_t DC_I2C_SW_STOPPED_ON_NACK;
	uint32_t DC_I2C_SW_TIMEOUT;
	uint32_t DC_I2C_SW_ABORTED;
	uint32_t DC_I2C_SW_DONE;
	uint32_t DC_I2C_SW_STATUS;
	uint32_t DC_I2C_STOP_ON_NACK0;
	uint32_t DC_I2C_START0;
	uint32_t DC_I2C_RW0;
	uint32_t DC_I2C_STOP0;
	uint32_t DC_I2C_COUNT0;
	uint32_t DC_I2C_DATA_RW;
	uint32_t DC_I2C_DATA;
	uint32_t DC_I2C_INDEX;
	uint32_t DC_I2C_INDEX_WRITE;
	uint32_t XTAL_REF_DIV;
#if defined(CONFIG_DRM_AMD_DC_DCN2_0)
	uint32_t DC_I2C_DDC1_SEND_RESET_LENGTH;
#endif
	uint32_t DC_I2C_REG_RW_CNTL_STATUS;
};

#if defined(CONFIG_DRM_AMD_DC_DCN2_0)
#define I2C_COMMON_MASK_SH_LIST_DCN2(mask_sh)\
	I2C_COMMON_MASK_SH_LIST_DCE110(mask_sh),\
	I2C_SF(DC_I2C_DDC1_SETUP, DC_I2C_DDC1_SEND_RESET_LENGTH, mask_sh)
#endif

struct dce_i2c_registers {
	uint32_t SETUP;
	uint32_t SPEED;
	uint32_t HW_STATUS;
	uint32_t DC_I2C_ARBITRATION;
	uint32_t DC_I2C_CONTROL;
	uint32_t DC_I2C_SW_STATUS;
	uint32_t DC_I2C_TRANSACTION0;
	uint32_t DC_I2C_TRANSACTION1;
	uint32_t DC_I2C_TRANSACTION2;
	uint32_t DC_I2C_TRANSACTION3;
	uint32_t DC_I2C_DATA;
	uint32_t MICROSECOND_TIME_BASE_DIV;
};

enum dce_i2c_transaction_address_space {
	DCE_I2C_TRANSACTION_ADDRESS_SPACE_I2C = 1,
	DCE_I2C_TRANSACTION_ADDRESS_SPACE_DPCD
};

struct i2c_request_transaction_data {
	enum dce_i2c_transaction_action action;
	enum i2c_channel_operation_result status;
	uint8_t address;
	uint32_t length;
	uint8_t *data;
};

struct dce_i2c_hw {
	struct ddc *ddc;
	uint32_t original_speed;
	uint32_t engine_keep_power_up_count;
	uint32_t transaction_count;
	uint32_t buffer_used_bytes;
	uint32_t buffer_used_write;
	uint32_t reference_frequency;
	uint32_t default_speed;
	uint32_t engine_id;
	uint32_t setup_limit;
	uint32_t send_reset_length;
	uint32_t buffer_size;
	struct dc_context *ctx;

	const struct dce_i2c_registers *regs;
	const struct dce_i2c_shift *shifts;
	const struct dce_i2c_mask *masks;
};

void dce_i2c_hw_construct(
	struct dce_i2c_hw *dce_i2c_hw,
	struct dc_context *ctx,
	uint32_t engine_id,
	const struct dce_i2c_registers *regs,
	const struct dce_i2c_shift *shifts,
	const struct dce_i2c_mask *masks);

void dce100_i2c_hw_construct(
	struct dce_i2c_hw *dce_i2c_hw,
	struct dc_context *ctx,
	uint32_t engine_id,
	const struct dce_i2c_registers *regs,
	const struct dce_i2c_shift *shifts,
	const struct dce_i2c_mask *masks);

void dce112_i2c_hw_construct(
	struct dce_i2c_hw *dce_i2c_hw,
	struct dc_context *ctx,
	uint32_t engine_id,
	const struct dce_i2c_registers *regs,
	const struct dce_i2c_shift *shifts,
	const struct dce_i2c_mask *masks);

void dcn1_i2c_hw_construct(
	struct dce_i2c_hw *dce_i2c_hw,
	struct dc_context *ctx,
	uint32_t engine_id,
	const struct dce_i2c_registers *regs,
	const struct dce_i2c_shift *shifts,
	const struct dce_i2c_mask *masks);

#if defined(CONFIG_DRM_AMD_DC_DCN2_0)
void dcn2_i2c_hw_construct(
	struct dce_i2c_hw *dce_i2c_hw,
	struct dc_context *ctx,
	uint32_t engine_id,
	const struct dce_i2c_registers *regs,
	const struct dce_i2c_shift *shifts,
	const struct dce_i2c_mask *masks);
#endif

bool dce_i2c_submit_command_hw(
	struct resource_pool *pool,
	struct ddc *ddc,
	struct i2c_command *cmd,
	struct dce_i2c_hw *dce_i2c_hw);

struct dce_i2c_hw *acquire_i2c_hw_engine(
	struct resource_pool *pool,
	struct ddc *ddc);

#endif

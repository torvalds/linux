/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
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

#ifndef __DAL_I2C_HW_ENGINE_DCE110_H__
#define __DAL_I2C_HW_ENGINE_DCE110_H__

#define I2C_HW_ENGINE_COMMON_REG_LIST(id)\
	SRI(SETUP, DC_I2C_DDC, id),\
	SRI(SPEED, DC_I2C_DDC, id),\
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
	I2C_SF(MICROSECOND_TIME_BASE_DIV, XTAL_REF_DIV, mask_sh)

#define I2C_COMMON_MASK_SH_LIST_DCE100(mask_sh)\
	I2C_COMMON_MASK_SH_LIST_DCE_COMMON_BASE(mask_sh)

#define I2C_COMMON_MASK_SH_LIST_DCE110(mask_sh)\
	I2C_COMMON_MASK_SH_LIST_DCE_COMMON_BASE(mask_sh),\
	I2C_SF(DC_I2C_DDC1_SPEED, DC_I2C_DDC1_START_STOP_TIMING_CNTL, mask_sh)

struct dce110_i2c_hw_engine_shift {
	uint8_t DC_I2C_DDC1_ENABLE;
	uint8_t DC_I2C_DDC1_TIME_LIMIT;
	uint8_t DC_I2C_DDC1_DATA_DRIVE_EN;
	uint8_t DC_I2C_DDC1_CLK_DRIVE_EN;
	uint8_t DC_I2C_DDC1_DATA_DRIVE_SEL;
	uint8_t DC_I2C_DDC1_INTRA_TRANSACTION_DELAY;
	uint8_t DC_I2C_DDC1_INTRA_BYTE_DELAY;
	uint8_t DC_I2C_SW_DONE_USING_I2C_REG;
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
};

struct dce110_i2c_hw_engine_mask {
	uint32_t DC_I2C_DDC1_ENABLE;
	uint32_t DC_I2C_DDC1_TIME_LIMIT;
	uint32_t DC_I2C_DDC1_DATA_DRIVE_EN;
	uint32_t DC_I2C_DDC1_CLK_DRIVE_EN;
	uint32_t DC_I2C_DDC1_DATA_DRIVE_SEL;
	uint32_t DC_I2C_DDC1_INTRA_TRANSACTION_DELAY;
	uint32_t DC_I2C_DDC1_INTRA_BYTE_DELAY;
	uint32_t DC_I2C_SW_DONE_USING_I2C_REG;
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
};

struct dce110_i2c_hw_engine_registers {
	uint32_t SETUP;
	uint32_t SPEED;
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

struct i2c_hw_engine_dce110 {
	struct i2c_hw_engine base;
	const struct dce110_i2c_hw_engine_registers *regs;
	const struct dce110_i2c_hw_engine_shift *i2c_shift;
	const struct dce110_i2c_hw_engine_mask *i2c_mask;
	struct {
		uint32_t DC_I2C_DDCX_SETUP;
		uint32_t DC_I2C_DDCX_SPEED;
	} addr;
	uint32_t engine_id;
	/* expressed in kilohertz */
	uint32_t reference_frequency;
	/* number of bytes currently used in HW buffer */
	uint32_t buffer_used_bytes;
	/* number of bytes used for write transaction in HW buffer
	 * - this will be used as the index to read from*/
	uint32_t buffer_used_write;
	/* number of pending transactions (before GO) */
	uint32_t transaction_count;
	uint32_t engine_keep_power_up_count;
};

struct i2c_hw_engine_dce110_create_arg {
	uint32_t engine_id;
	uint32_t reference_frequency;
	uint32_t default_speed;
	struct dc_context *ctx;
	const struct dce110_i2c_hw_engine_registers *regs;
	const struct dce110_i2c_hw_engine_shift *i2c_shift;
	const struct dce110_i2c_hw_engine_mask *i2c_mask;
};

struct i2c_engine *dal_i2c_hw_engine_dce110_create(
	const struct i2c_hw_engine_dce110_create_arg *arg);

bool i2c_hw_engine_dce110_construct(
	struct i2c_hw_engine_dce110 *engine_dce110,
	const struct i2c_hw_engine_dce110_create_arg *arg);

#endif

/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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

/**
 * This file defines external dependencies of Display Core.
 */

#ifndef __DM_SERVICES_H__

#define __DM_SERVICES_H__

#include "amdgpu_dm_trace.h"

/* TODO: remove when DC is complete. */
#include "dm_services_types.h"
#include "logger_interface.h"
#include "link_service_types.h"

#undef DEPRECATED

struct dmub_srv;
struct dc_dmub_srv;

irq_handler_idx dm_register_interrupt(
	struct dc_context *ctx,
	struct dc_interrupt_params *int_params,
	interrupt_handler ih,
	void *handler_args);


/*
 *
 * GPU registers access
 *
 */
uint32_t dm_read_reg_func(
	const struct dc_context *ctx,
	uint32_t address,
	const char *func_name);
/* enable for debugging new code, this adds 50k to the driver size. */
/* #define DM_CHECK_ADDR_0 */

#define dm_read_reg(ctx, address)	\
		dm_read_reg_func(ctx, address, __func__)



#define dm_write_reg(ctx, address, value)	\
	dm_write_reg_func(ctx, address, value, __func__)

static inline void dm_write_reg_func(
	const struct dc_context *ctx,
	uint32_t address,
	uint32_t value,
	const char *func_name)
{
#ifdef DM_CHECK_ADDR_0
	if (address == 0) {
		DC_ERR("invalid register write. address = 0");
		return;
	}
#endif
	cgs_write_register(ctx->cgs_device, address, value);
	trace_amdgpu_dc_wreg(&ctx->perf_trace->write_count, address, value);
}

static inline uint32_t dm_read_index_reg(
	const struct dc_context *ctx,
	enum cgs_ind_reg addr_space,
	uint32_t index)
{
	return cgs_read_ind_register(ctx->cgs_device, addr_space, index);
}

static inline void dm_write_index_reg(
	const struct dc_context *ctx,
	enum cgs_ind_reg addr_space,
	uint32_t index,
	uint32_t value)
{
	cgs_write_ind_register(ctx->cgs_device, addr_space, index, value);
}

static inline uint32_t get_reg_field_value_ex(
	uint32_t reg_value,
	uint32_t mask,
	uint8_t shift)
{
	return (mask & reg_value) >> shift;
}

#define get_reg_field_value(reg_value, reg_name, reg_field)\
	get_reg_field_value_ex(\
		(reg_value),\
		reg_name ## __ ## reg_field ## _MASK,\
		reg_name ## __ ## reg_field ## __SHIFT)

static inline uint32_t set_reg_field_value_ex(
	uint32_t reg_value,
	uint32_t value,
	uint32_t mask,
	uint8_t shift)
{
	ASSERT(mask != 0);
	return (reg_value & ~mask) | (mask & (value << shift));
}

#define set_reg_field_value(reg_value, value, reg_name, reg_field)\
	(reg_value) = set_reg_field_value_ex(\
		(reg_value),\
		(value),\
		reg_name ## __ ## reg_field ## _MASK,\
		reg_name ## __ ## reg_field ## __SHIFT)

uint32_t generic_reg_set_ex(const struct dc_context *ctx,
		uint32_t addr, uint32_t reg_val, int n,
		uint8_t shift1, uint32_t mask1, uint32_t field_value1, ...);

uint32_t generic_reg_update_ex(const struct dc_context *ctx,
		uint32_t addr, int n,
		uint8_t shift1, uint32_t mask1, uint32_t field_value1, ...);

struct dc_dmub_srv *dc_dmub_srv_create(struct dc *dc, struct dmub_srv *dmub);
void dc_dmub_srv_destroy(struct dc_dmub_srv **dmub_srv);

void reg_sequence_start_gather(const struct dc_context *ctx);
void reg_sequence_start_execute(const struct dc_context *ctx);
void reg_sequence_wait_done(const struct dc_context *ctx);

#define FD(reg_field)	reg_field ## __SHIFT, \
						reg_field ## _MASK

/*
 * return number of poll before condition is met
 * return 0 if condition is not meet after specified time out tries
 */
void generic_reg_wait(const struct dc_context *ctx,
	uint32_t addr, uint32_t mask, uint32_t shift, uint32_t condition_value,
	unsigned int delay_between_poll_us, unsigned int time_out_num_tries,
	const char *func_name, int line);

unsigned int snprintf_count(char *pBuf, unsigned int bufSize, char *fmt, ...);

/* These macros need to be used with soc15 registers in order to retrieve
 * the actual offset.
 */
#define dm_write_reg_soc15(ctx, reg, inst_offset, value)	\
		dm_write_reg_func(ctx, reg + DCE_BASE.instance[0].segment[reg##_BASE_IDX] + inst_offset, value, __func__)

#define dm_read_reg_soc15(ctx, reg, inst_offset)	\
		dm_read_reg_func(ctx, reg + DCE_BASE.instance[0].segment[reg##_BASE_IDX] + inst_offset, __func__)

#define generic_reg_update_soc15(ctx, inst_offset, reg_name, n, ...)\
		generic_reg_update_ex(ctx, DCE_BASE.instance[0].segment[mm##reg_name##_BASE_IDX] +  mm##reg_name + inst_offset, \
		n, __VA_ARGS__)

#define generic_reg_set_soc15(ctx, inst_offset, reg_name, n, ...)\
		generic_reg_set_ex(ctx, DCE_BASE.instance[0].segment[mm##reg_name##_BASE_IDX] + mm##reg_name + inst_offset, 0, \
		n, __VA_ARGS__)

#define get_reg_field_value_soc15(reg_value, block, reg_num, reg_name, reg_field)\
	get_reg_field_value_ex(\
		(reg_value),\
		block ## reg_num ## _ ## reg_name ## __ ## reg_field ## _MASK,\
		block ## reg_num ## _ ## reg_name ## __ ## reg_field ## __SHIFT)

#define set_reg_field_value_soc15(reg_value, value, block, reg_num, reg_name, reg_field)\
	(reg_value) = set_reg_field_value_ex(\
		(reg_value),\
		(value),\
		block ## reg_num ## _ ## reg_name ## __ ## reg_field ## _MASK,\
		block ## reg_num ## _ ## reg_name ## __ ## reg_field ## __SHIFT)

/**************************************
 * Power Play (PP) interfaces
 **************************************/

/* Gets valid clocks levels from pplib
 *
 * input: clk_type - display clk / sclk / mem clk
 *
 * output: array of valid clock levels for given type in ascending order,
 * with invalid levels filtered out
 *
 */
bool dm_pp_get_clock_levels_by_type(
	const struct dc_context *ctx,
	enum dm_pp_clock_type clk_type,
	struct dm_pp_clock_levels *clk_level_info);

bool dm_pp_get_clock_levels_by_type_with_latency(
	const struct dc_context *ctx,
	enum dm_pp_clock_type clk_type,
	struct dm_pp_clock_levels_with_latency *clk_level_info);

bool dm_pp_get_clock_levels_by_type_with_voltage(
	const struct dc_context *ctx,
	enum dm_pp_clock_type clk_type,
	struct dm_pp_clock_levels_with_voltage *clk_level_info);

bool dm_pp_notify_wm_clock_changes(
	const struct dc_context *ctx,
	struct dm_pp_wm_sets_with_clock_ranges *wm_with_clock_ranges);

void dm_pp_get_funcs(struct dc_context *ctx,
		struct pp_smu_funcs *funcs);

/* DAL calls this function to notify PP about completion of Mode Set.
 * For PP it means that current DCE clocks are those which were returned
 * by dc_service_pp_pre_dce_clock_change(), in the 'output' parameter.
 *
 * If the clocks are higher than before, then PP does nothing.
 *
 * If the clocks are lower than before, then PP reduces the voltage.
 *
 * \returns	true - call is successful
 *		false - call failed
 */
bool dm_pp_apply_display_requirements(
	const struct dc_context *ctx,
	const struct dm_pp_display_configuration *pp_display_cfg);

bool dm_pp_apply_power_level_change_request(
	const struct dc_context *ctx,
	struct dm_pp_power_level_change_request *level_change_req);

bool dm_pp_apply_clock_for_voltage_request(
	const struct dc_context *ctx,
	struct dm_pp_clock_for_voltage_req *clock_for_voltage_req);

bool dm_pp_get_static_clocks(
	const struct dc_context *ctx,
	struct dm_pp_static_clock_info *static_clk_info);

/****** end of PP interfaces ******/

struct persistent_data_flag {
	bool save_per_link;
	bool save_per_edid;
};

/* Call to write data in registry editor for persistent data storage.
 *
 * \inputs      sink - identify edid/link for registry folder creation
 *              module name - identify folders for registry
 *              key name - identify keys within folders for registry
 *              params - value to write in defined folder/key
 *              size - size of the input params
 *              flag - determine whether to save by link or edid
 *
 * \returns     true - call is successful
 *              false - call failed
 *
 * sink         module         key
 * -----------------------------------------------------------------------------
 * NULL         NULL           NULL     - failure
 * NULL         NULL           -        - create key with param value
 *                                                      under base folder
 * NULL         -              NULL     - create module folder under base folder
 * -            NULL           NULL     - failure
 * NULL         -              -        - create key under module folder
 *                                            with no edid/link identification
 * -            NULL           -        - create key with param value
 *                                                       under base folder
 * -            -              NULL     - create module folder under base folder
 * -            -              -        - create key under module folder
 *                                              with edid/link identification
 */
bool dm_write_persistent_data(struct dc_context *ctx,
		const struct dc_sink *sink,
		const char *module_name,
		const char *key_name,
		void *params,
		unsigned int size,
		struct persistent_data_flag *flag);


/* Call to read data in registry editor for persistent data storage.
 *
 * \inputs      sink - identify edid/link for registry folder creation
 *              module name - identify folders for registry
 *              key name - identify keys within folders for registry
 *              size - size of the output params
 *              flag - determine whether it was save by link or edid
 *
 * \returns     params - value read from defined folder/key
 *              true - call is successful
 *              false - call failed
 *
 * sink         module         key
 * -----------------------------------------------------------------------------
 * NULL         NULL           NULL     - failure
 * NULL         NULL           -        - read key under base folder
 * NULL         -              NULL     - failure
 * -            NULL           NULL     - failure
 * NULL         -              -        - read key under module folder
 *                                             with no edid/link identification
 * -            NULL           -        - read key under base folder
 * -            -              NULL     - failure
 * -            -              -        - read key under module folder
 *                                              with edid/link identification
 */
bool dm_read_persistent_data(struct dc_context *ctx,
		const struct dc_sink *sink,
		const char *module_name,
		const char *key_name,
		void *params,
		unsigned int size,
		struct persistent_data_flag *flag);

bool dm_query_extended_brightness_caps
	(struct dc_context *ctx, enum dm_acpi_display_type display,
			struct dm_acpi_atif_backlight_caps *pCaps);

bool dm_dmcu_set_pipe(struct dc_context *ctx, unsigned int controller_id);

/*
 *
 * print-out services
 *
 */
#define dm_log_to_buffer(buffer, size, fmt, args)\
	vsnprintf(buffer, size, fmt, args)

static inline unsigned long long dm_get_timestamp(struct dc_context *ctx)
{
	return ktime_get_raw_ns();
}

unsigned long long dm_get_elapse_time_in_ns(struct dc_context *ctx,
		unsigned long long current_time_stamp,
		unsigned long long last_time_stamp);

/*
 * performance tracing
 */
#define PERF_TRACE()	trace_amdgpu_dc_performance(CTX->perf_trace->read_count,\
		CTX->perf_trace->write_count, &CTX->perf_trace->last_entry_read,\
		&CTX->perf_trace->last_entry_write, __func__, __LINE__)
#define PERF_TRACE_CTX(__CTX)	trace_amdgpu_dc_performance(__CTX->perf_trace->read_count,\
		__CTX->perf_trace->write_count, &__CTX->perf_trace->last_entry_read,\
		&__CTX->perf_trace->last_entry_write, __func__, __LINE__)


/*
 * Debug and verification hooks
 */

void dm_dtn_log_begin(struct dc_context *ctx,
	struct dc_log_buffer_ctx *log_ctx);
void dm_dtn_log_append_v(struct dc_context *ctx,
	struct dc_log_buffer_ctx *log_ctx,
	const char *msg, ...);
void dm_dtn_log_end(struct dc_context *ctx,
	struct dc_log_buffer_ctx *log_ctx);

#endif /* __DM_SERVICES_H__ */

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

#include "dm_services.h"
#include "include/dal_types.h"
#include "include/logger_interface.h"
#include "logger.h"

/******************************************************************************
 * Register Logger.
 * A facility to create register R/W logs.
 * Currently used for DAL Test.
 *****************************************************************************/

/******************************************************************************
 * Private structures
 *****************************************************************************/
struct dal_reg_dump_stack_location {
	const char *current_caller_func;
	long current_pid;
	long current_tgid;
	uint32_t rw_count;/* register access counter for current function. */
};

/* This the maximum number of nested calls to the 'reg_dump' facility. */
#define DAL_REG_DUMP_STACK_MAX_SIZE 32

struct dal_reg_dump_stack {
	int32_t stack_pointer;
	struct dal_reg_dump_stack_location
		stack_locations[DAL_REG_DUMP_STACK_MAX_SIZE];
	uint32_t total_rw_count; /* Total count for *all* functions. */
};

static struct dal_reg_dump_stack reg_dump_stack = {0};

/******************************************************************************
 * Private functions
 *****************************************************************************/

/* Check if current process is the one which requested register dump.
 * The reason for the check:
 * mmCRTC_STATUS_FRAME_COUNT is accessed by dal_controller_get_vblank_counter().
 * Which runs all the time when at least one display is connected.
 * (Triggered by drm_mode_page_flip_ioctl()). */
static bool is_reg_dump_process(void)
{
	uint32_t i;

	/* walk the list of our processes */
	for (i = 0; i < reg_dump_stack.stack_pointer; i++) {
		struct dal_reg_dump_stack_location *stack_location
					= &reg_dump_stack.stack_locations[i];

		if (stack_location->current_pid == dm_get_pid()
			&& stack_location->current_tgid == dm_get_tgid())
			return true;
	}

	return false;
}

static bool dal_reg_dump_stack_is_empty(void)
{
	if (reg_dump_stack.stack_pointer <= 0)
		return true;
	else
		return false;
}

static struct dal_reg_dump_stack_location *dal_reg_dump_stack_push(void)
{
	struct dal_reg_dump_stack_location *current_location = NULL;

	if (reg_dump_stack.stack_pointer >= DAL_REG_DUMP_STACK_MAX_SIZE) {
		/* stack is full */
		dm_output_to_console("[REG_DUMP]: %s: stack is full!\n",
				__func__);
	} else {
		current_location =
		&reg_dump_stack.stack_locations[reg_dump_stack.stack_pointer];
		++reg_dump_stack.stack_pointer;
	}

	return current_location;
}

static struct dal_reg_dump_stack_location *dal_reg_dump_stack_pop(void)
{
	struct dal_reg_dump_stack_location *current_location = NULL;

	if (dal_reg_dump_stack_is_empty()) {
		/* stack is empty */
		dm_output_to_console("[REG_DUMP]: %s: stack is empty!\n",
				__func__);
	} else {
		--reg_dump_stack.stack_pointer;
		current_location =
		&reg_dump_stack.stack_locations[reg_dump_stack.stack_pointer];
	}

	return current_location;
}

/******************************************************************************
 * Public functions
 *****************************************************************************/

void dal_reg_logger_push(const char *caller_func)
{
	struct dal_reg_dump_stack_location *free_stack_location;

	free_stack_location = dal_reg_dump_stack_push();

	if (NULL == free_stack_location)
		return;

	memset(free_stack_location, 0, sizeof(*free_stack_location));

	free_stack_location->current_caller_func = caller_func;
	free_stack_location->current_pid = dm_get_pid();
	free_stack_location->current_tgid = dm_get_tgid();

	dm_output_to_console("[REG_DUMP]:%s - start (pid:%ld, tgid:%ld)\n",
		caller_func,
		free_stack_location->current_pid,
		free_stack_location->current_tgid);
}

void dal_reg_logger_pop(void)
{
	struct dal_reg_dump_stack_location *top_stack_location;

	top_stack_location = dal_reg_dump_stack_pop();

	if (NULL == top_stack_location) {
		dm_output_to_console("[REG_DUMP]:%s - Stack is Empty!\n",
				__func__);
		return;
	}

	dm_output_to_console(
	"[REG_DUMP]:%s - end."\
	" Reg R/W Count: Total=%d Function=%d. (pid:%ld, tgid:%ld)\n",
			top_stack_location->current_caller_func,
			reg_dump_stack.total_rw_count,
			top_stack_location->rw_count,
			dm_get_pid(),
			dm_get_tgid());

	memset(top_stack_location, 0, sizeof(*top_stack_location));
}

void dal_reg_logger_rw_count_increment(void)
{
	++reg_dump_stack.total_rw_count;

	++reg_dump_stack.stack_locations
		[reg_dump_stack.stack_pointer - 1].rw_count;
}

bool dal_reg_logger_should_dump_register(void)
{
	if (true == dal_reg_dump_stack_is_empty())
		return false;

	if (false == is_reg_dump_process())
		return false;

	return true;
}

/******************************************************************************
 * End of File.
 *****************************************************************************/

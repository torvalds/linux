// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * A udbg backend which logs messages and reads input from in memory
 * buffers.
 *
 * The console output can be read from memcons_output which is a
 * circular buffer whose next write position is stored in memcons.output_pos.
 *
 * Input may be passed by writing into the memcons_input buffer when it is
 * empty. The input buffer is empty when both input_pos == input_start and
 * *input_start == '\0'.
 *
 * Copyright (C) 2003-2005 Anton Blanchard and Milton Miller, IBM Corp
 * Copyright (C) 2013 Alistair Popple, IBM Corp
 */

#include <linux/kernel.h>
#include <asm/barrier.h>
#include <asm/page.h>
#include <asm/processor.h>
#include <asm/udbg.h>

struct memcons {
	char *output_start;
	char *output_pos;
	char *output_end;
	char *input_start;
	char *input_pos;
	char *input_end;
};

static char memcons_output[CONFIG_PPC_MEMCONS_OUTPUT_SIZE];
static char memcons_input[CONFIG_PPC_MEMCONS_INPUT_SIZE];

struct memcons memcons = {
	.output_start = memcons_output,
	.output_pos = memcons_output,
	.output_end = &memcons_output[CONFIG_PPC_MEMCONS_OUTPUT_SIZE],
	.input_start = memcons_input,
	.input_pos = memcons_input,
	.input_end = &memcons_input[CONFIG_PPC_MEMCONS_INPUT_SIZE],
};

void memcons_putc(char c)
{
	char *new_output_pos;

	*memcons.output_pos = c;
	wmb();
	new_output_pos = memcons.output_pos + 1;
	if (new_output_pos >= memcons.output_end)
		new_output_pos = memcons.output_start;

	memcons.output_pos = new_output_pos;
}

int memcons_getc_poll(void)
{
	char c;
	char *new_input_pos;

	if (*memcons.input_pos) {
		c = *memcons.input_pos;

		new_input_pos = memcons.input_pos + 1;
		if (new_input_pos >= memcons.input_end)
			new_input_pos = memcons.input_start;
		else if (*new_input_pos == '\0')
			new_input_pos = memcons.input_start;

		*memcons.input_pos = '\0';
		wmb();
		memcons.input_pos = new_input_pos;
		return c;
	}

	return -1;
}

int memcons_getc(void)
{
	int c;

	while (1) {
		c = memcons_getc_poll();
		if (c == -1)
			cpu_relax();
		else
			break;
	}

	return c;
}

void __init udbg_init_memcons(void)
{
	udbg_putc = memcons_putc;
	udbg_getc = memcons_getc;
	udbg_getc_poll = memcons_getc_poll;
}

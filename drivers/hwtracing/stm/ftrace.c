// SPDX-License-Identifier: GPL-2.0
/*
 * Simple kernel driver to link kernel Ftrace and an STM device
 * Copyright (c) 2016, Linaro Ltd.
 *
 * STM Ftrace will be registered as a trace_export.
 */

#include <linux/module.h>
#include <linux/stm.h>
#include <linux/trace.h>

#define STM_FTRACE_NR_CHANNELS 1
#define STM_FTRACE_CHAN 0

static int stm_ftrace_link(struct stm_source_data *data);
static void stm_ftrace_unlink(struct stm_source_data *data);

static struct stm_ftrace {
	struct stm_source_data	data;
	struct trace_export	ftrace;
} stm_ftrace = {
	.data	= {
		.name		= "ftrace",
		.nr_chans	= STM_FTRACE_NR_CHANNELS,
		.link		= stm_ftrace_link,
		.unlink		= stm_ftrace_unlink,
	},
};

/**
 * stm_ftrace_write() - write data to STM via 'stm_ftrace' source
 * @buf:	buffer containing the data packet
 * @len:	length of the data packet
 */
static void notrace
stm_ftrace_write(struct trace_export *export, const void *buf, unsigned int len)
{
	struct stm_ftrace *stm = container_of(export, struct stm_ftrace, ftrace);

	stm_source_write(&stm->data, STM_FTRACE_CHAN, buf, len);
}

static int stm_ftrace_link(struct stm_source_data *data)
{
	struct stm_ftrace *sf = container_of(data, struct stm_ftrace, data);

	sf->ftrace.write = stm_ftrace_write;
	sf->ftrace.flags = TRACE_EXPORT_FUNCTION | TRACE_EXPORT_EVENT
			| TRACE_EXPORT_MARKER;

	return register_ftrace_export(&sf->ftrace);
}

static void stm_ftrace_unlink(struct stm_source_data *data)
{
	struct stm_ftrace *sf = container_of(data, struct stm_ftrace, data);

	unregister_ftrace_export(&sf->ftrace);
}

static int __init stm_ftrace_init(void)
{
	int ret;

	ret = stm_source_register_device(NULL, &stm_ftrace.data);
	if (ret)
		pr_err("Failed to register stm_source - ftrace.\n");

	return ret;
}

static void __exit stm_ftrace_exit(void)
{
	stm_source_unregister_device(&stm_ftrace.data);
}

module_init(stm_ftrace_init);
module_exit(stm_ftrace_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("stm_ftrace driver");
MODULE_AUTHOR("Chunyan Zhang <zhang.chunyan@linaro.org>");

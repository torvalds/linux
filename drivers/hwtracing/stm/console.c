// SPDX-License-Identifier: GPL-2.0
/*
 * Simple kernel console driver for STM devices
 * Copyright (c) 2014, Intel Corporation.
 *
 * STM console will send kernel messages over STM devices to a trace host.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/console.h>
#include <linux/slab.h>
#include <linux/stm.h>

static int stm_console_link(struct stm_source_data *data);
static void stm_console_unlink(struct stm_source_data *data);

static struct stm_console {
	struct stm_source_data	data;
	struct console		console;
} stm_console = {
	.data	= {
		.name		= "console",
		.nr_chans	= 1,
		.link		= stm_console_link,
		.unlink		= stm_console_unlink,
	},
};

static void notrace __nocfi
stm_console_write(struct console *con, const char *buf, unsigned len)
{
	struct stm_console *sc = container_of(con, struct stm_console, console);

	stm_source_write(&sc->data, 0, buf, len);
}

static int stm_console_link(struct stm_source_data *data)
{
	struct stm_console *sc = container_of(data, struct stm_console, data);

	strcpy(sc->console.name, "stm_console");
	sc->console.write = stm_console_write;
	sc->console.flags = CON_ENABLED | CON_PRINTBUFFER;
	register_console(&sc->console);

	return 0;
}

static void stm_console_unlink(struct stm_source_data *data)
{
	struct stm_console *sc = container_of(data, struct stm_console, data);

	unregister_console(&sc->console);
}

static int stm_console_init(void)
{
	return stm_source_register_device(NULL, &stm_console.data);
}

static void stm_console_exit(void)
{
	stm_source_unregister_device(&stm_console.data);
}

module_init(stm_console_init);
module_exit(stm_console_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("stm_console driver");
MODULE_AUTHOR("Alexander Shishkin <alexander.shishkin@linux.intel.com>");

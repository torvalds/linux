// SPDX-License-Identifier: GPL-2.0
#include <linux/kmsg_dump.h>
#include <linux/spinlock.h>
#include <linux/console.h>
#include <linux/string.h>
#include <shared/init.h>
#include <shared/kern.h>
#include <os.h>

static void kmsg_dumper_stdout(struct kmsg_dumper *dumper,
				enum kmsg_dump_reason reason)
{
	static struct kmsg_dump_iter iter;
	static DEFINE_SPINLOCK(lock);
	static char line[1024];
	struct console *con;
	unsigned long flags;
	size_t len = 0;
	int cookie;

	/*
	 * If no consoles are available to output crash information, dump
	 * the kmsg buffer to stdout.
	 */

	cookie = console_srcu_read_lock();
	for_each_console_srcu(con) {
		/*
		 * The ttynull console and disabled consoles are ignored
		 * since they cannot output. All other consoles are
		 * expected to output the crash information.
		 */
		if (strcmp(con->name, "ttynull") != 0 &&
		    (console_srcu_read_flags(con) & CON_ENABLED)) {
			break;
		}
	}
	console_srcu_read_unlock(cookie);
	if (con)
		return;

	if (!spin_trylock_irqsave(&lock, flags))
		return;

	kmsg_dump_rewind(&iter);

	printf("kmsg_dump:\n");
	while (kmsg_dump_get_line(&iter, true, line, sizeof(line), &len)) {
		line[len] = '\0';
		printf("%s", line);
	}

	spin_unlock_irqrestore(&lock, flags);
}

static struct kmsg_dumper kmsg_dumper = {
	.dump = kmsg_dumper_stdout
};

static int __init kmsg_dumper_stdout_init(void)
{
	return kmsg_dump_register(&kmsg_dumper);
}

__uml_postsetup(kmsg_dumper_stdout_init);

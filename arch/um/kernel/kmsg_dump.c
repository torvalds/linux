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

	/* only dump kmsg when no console is available */
	if (!console_trylock())
		return;

	for_each_console(con) {
		if(strcmp(con->name, "tty") == 0 &&
		   (con->flags & (CON_ENABLED | CON_CONSDEV)) != 0) {
			break;
		}
	}

	console_unlock();

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

int __init kmsg_dumper_stdout_init(void)
{
	return kmsg_dump_register(&kmsg_dumper);
}

__uml_postsetup(kmsg_dumper_stdout_init);

#include <linux/kmsg_dump.h>
#include <linux/console.h>
#include <shared/init.h>
#include <shared/kern.h>
#include <os.h>

static void kmsg_dumper_stdout(struct kmsg_dumper *dumper,
				enum kmsg_dump_reason reason)
{
	static char line[1024];

	size_t len = 0;
	bool con_available = false;

	/* only dump kmsg when no console is available */
	if (!console_trylock())
		return;

	if (console_drivers != NULL)
		con_available = true;

	console_unlock();

	if (con_available == true)
		return;

	printf("kmsg_dump:\n");
	while (kmsg_dump_get_line(dumper, true, line, sizeof(line), &len)) {
		line[len] = '\0';
		printf("%s", line);
	}
}

static struct kmsg_dumper kmsg_dumper = {
	.dump = kmsg_dumper_stdout
};

int __init kmsg_dumper_stdout_init(void)
{
	return kmsg_dump_register(&kmsg_dumper);
}

__uml_postsetup(kmsg_dumper_stdout_init);

#include <asm/interrupt.h>
#include <asm/kprobes.h>

struct restart_table_entry {
	unsigned long start;
	unsigned long end;
	unsigned long fixup;
};

extern struct restart_table_entry __start___restart_table[];
extern struct restart_table_entry __stop___restart_table[];

/* Given an address, look for it in the kernel exception table */
unsigned long search_kernel_restart_table(unsigned long addr)
{
	struct restart_table_entry *rte = __start___restart_table;

	while (rte < __stop___restart_table) {
		unsigned long start = rte->start;
		unsigned long end = rte->end;
		unsigned long fixup = rte->fixup;

		if (addr >= start && addr < end)
			return fixup;

		rte++;
	}
	return 0;
}
NOKPROBE_SYMBOL(search_kernel_restart_table);

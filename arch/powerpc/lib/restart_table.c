#include <asm/interrupt.h>
#include <asm/kprobes.h>

struct soft_mask_table_entry {
	unsigned long start;
	unsigned long end;
};

struct restart_table_entry {
	unsigned long start;
	unsigned long end;
	unsigned long fixup;
};

extern struct soft_mask_table_entry __start___soft_mask_table[];
extern struct soft_mask_table_entry __stop___soft_mask_table[];

extern struct restart_table_entry __start___restart_table[];
extern struct restart_table_entry __stop___restart_table[];

/* Given an address, look for it in the soft mask table */
bool search_kernel_soft_mask_table(unsigned long addr)
{
	struct soft_mask_table_entry *smte = __start___soft_mask_table;

	while (smte < __stop___soft_mask_table) {
		unsigned long start = smte->start;
		unsigned long end = smte->end;

		if (addr >= start && addr < end)
			return true;

		smte++;
	}
	return false;
}
NOKPROBE_SYMBOL(search_kernel_soft_mask_table);

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

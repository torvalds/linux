#include <linux/module.h>
#include <linux/spinlock.h>
#include <asm/uaccess.h>


int fixup_exception(struct pt_regs *regs)
{
	const struct exception_table_entry *fixup;

#ifdef CONFIG_PNPBIOS
	if (unlikely(SEGMENT_IS_PNP_CODE(regs->cs))) {
		extern u32 pnp_bios_fault_eip, pnp_bios_fault_esp;
		extern u32 pnp_bios_is_utter_crap;
		pnp_bios_is_utter_crap = 1;
		printk(KERN_CRIT "PNPBIOS fault.. attempting recovery.\n");
		__asm__ volatile(
			"movl %0, %%esp\n\t"
			"jmp *%1\n\t"
			: : "g" (pnp_bios_fault_esp), "g" (pnp_bios_fault_eip));
		panic("do_trap: can't hit this");
	}
#endif

	fixup = search_exception_tables(regs->ip);
	if (fixup) {
		/* If fixup is less than 16, it means uaccess error */
		if (fixup->fixup < 16) {
			current_thread_info()->uaccess_err = -EFAULT;
			regs->ip += fixup->fixup;
			return 1;
		}
		regs->ip = fixup->fixup;
		return 1;
	}

	return 0;
}

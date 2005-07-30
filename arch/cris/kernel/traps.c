/* $Id: traps.c,v 1.11 2005/01/24 16:03:19 orjanf Exp $
 *
 *  linux/arch/cris/traps.c
 *
 *  Here we handle the break vectors not used by the system call 
 *  mechanism, as well as some general stack/register dumping 
 *  things.
 * 
 *  Copyright (C) 2000-2002 Axis Communications AB
 *
 *  Authors:   Bjorn Wesen
 *  	       Hans-Peter Nilsson
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <asm/pgtable.h>
#include <asm/uaccess.h>

static int kstack_depth_to_print = 24;

extern int raw_printk(const char *fmt, ...);

void show_trace(unsigned long * stack)
{
	unsigned long addr, module_start, module_end;
	extern char _stext, _etext;
	int i;

        raw_printk("\nCall Trace: ");

        i = 1;
        module_start = VMALLOC_START;
        module_end = VMALLOC_END;

        while (((long) stack & (THREAD_SIZE-1)) != 0) {
		if (__get_user (addr, stack)) {
			/* This message matches "failing address" marked
			   s390 in ksymoops, so lines containing it will
			   not be filtered out by ksymoops.  */
			raw_printk ("Failing address 0x%lx\n", (unsigned long)stack);
			break;
		}
		stack++;

                /*
                 * If the address is either in the text segment of the
                 * kernel, or in the region which contains vmalloc'ed
                 * memory, it *may* be the address of a calling
                 * routine; if so, print it so that someone tracing
                 * down the cause of the crash will be able to figure
                 * out the call path that was taken.
                 */
                if (((addr >= (unsigned long) &_stext) &&
                     (addr <= (unsigned long) &_etext)) ||
                    ((addr >= module_start) && (addr <= module_end))) {
                        if (i && ((i % 8) == 0))
                                raw_printk("\n       ");
                        raw_printk("[<%08lx>] ", addr);
                        i++;
                }
        }
}

/*
 * These constants are for searching for possible module text
 * segments. MODULE_RANGE is a guess of how much space is likely
 * to be vmalloced.
 */

#define MODULE_RANGE (8*1024*1024)

/*
 * The output (format, strings and order) is adjusted to be usable with
 * ksymoops-2.4.1 with some necessary CRIS-specific patches.  Please don't
 * change it unless you're serious about adjusting ksymoops and syncing
 * with the ksymoops maintainer.
 */

void 
show_stack(struct task_struct *task, unsigned long *sp)
{
        unsigned long *stack, addr;
        int i;

	/*
	 * debugging aid: "show_stack(NULL);" prints a
	 * back trace.
	 */

        if(sp == NULL) {
		if (task)
			sp = (unsigned long*)task->thread.ksp;
		else
			sp = (unsigned long*)rdsp();
	}

        stack = sp;

	raw_printk("\nStack from %08lx:\n       ", (unsigned long)stack);
        for(i = 0; i < kstack_depth_to_print; i++) {
                if (((long) stack & (THREAD_SIZE-1)) == 0)
                        break;
                if (i && ((i % 8) == 0))
                        raw_printk("\n       ");
		if (__get_user (addr, stack)) {
			/* This message matches "failing address" marked
			   s390 in ksymoops, so lines containing it will
			   not be filtered out by ksymoops.  */
			raw_printk ("Failing address 0x%lx\n", (unsigned long)stack);
			break;
		}
		stack++;
		raw_printk("%08lx ", addr);
        }
	show_trace(sp);
}

static void (*nmi_handler)(struct pt_regs*);
extern void arch_enable_nmi(void);

void set_nmi_handler(void (*handler)(struct pt_regs*))
{
  nmi_handler = handler;
  arch_enable_nmi();
}

void handle_nmi(struct pt_regs* regs)
{
  if (nmi_handler)
    nmi_handler(regs);
}

#ifdef CONFIG_DEBUG_NMI_OOPS
void oops_nmi_handler(struct pt_regs* regs)
{
  stop_watchdog();
  raw_printk("NMI!\n");
  show_registers(regs);
}

static int
__init oops_nmi_register(void)
{
  set_nmi_handler(oops_nmi_handler);
  return 0;
}

__initcall(oops_nmi_register);

#endif

#if 0
/* displays a short stack trace */

int 
show_stack()
{
	unsigned long *sp = (unsigned long *)rdusp();
	int i;
	raw_printk("Stack dump [0x%08lx]:\n", (unsigned long)sp);
	for(i = 0; i < 16; i++)
		raw_printk("sp + %d: 0x%08lx\n", i*4, sp[i]);
	return 0;
}
#endif

void dump_stack(void)
{
	show_stack(NULL, NULL);
}

EXPORT_SYMBOL(dump_stack);

void __init 
trap_init(void)
{
	/* Nothing needs to be done */
}

void spinning_cpu(void* addr)
{
  raw_printk("CPU %d spinning on %X\n", smp_processor_id(), addr);
  dump_stack();
}

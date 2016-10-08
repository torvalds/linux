#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i8253.h>
#include <linux/timer.h>
#include "FSM/FSMDevice/FSM_DeviceProcess.h"

static struct timer_list FSM_Beep_timer;

void FSM_Beep_timer_callback(unsigned long data)
{
    FSM_Beep(0, 0);
}

void FSM_Beep(int value, int msec)
{
    unsigned int count = 0;
    unsigned long flags;
    if(value > 20 && value < 32767)
        count = PIT_TICK_RATE / value;

    raw_spin_lock_irqsave(&i8253_lock, flags);

    if(count) {
        /* set command for counter 2, 2 byte write */
        outb_p(0xB6, 0x43);
        /* select desired HZ */
        outb_p(count & 0xff, 0x42);
        outb((count >> 8) & 0xff, 0x42);
        /* enable counter 2 */
        outb_p(inb_p(0x61) | 3, 0x61);
        mod_timer(&FSM_Beep_timer, jiffies + msecs_to_jiffies(msec));
    } else {
        /* disable counter 2 */
        outb(inb_p(0x61) & 0xFC, 0x61);
    }

    raw_spin_unlock_irqrestore(&i8253_lock, flags);
}
EXPORT_SYMBOL(FSM_Beep);

static int __init FSM_Beep_init(void)
{
    setup_timer(&FSM_Beep_timer, FSM_Beep_timer_callback, 0);

    printk(KERN_INFO "FSM Beep loaded\n");
    return 0;
}
static void __exit FSM_Beep_exit(void)
{
    del_timer(&FSM_Beep_timer);
    printk(KERN_INFO "FSM Beep unloaded\n");
}

MODULE_LICENSE("GPL");
module_init(FSM_Beep_init);
module_exit(FSM_Beep_exit);
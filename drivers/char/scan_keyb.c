/*
 *	$Id: scan_keyb.c,v 1.2 2000/07/04 06:24:42 yaegashi Exp $ 
 *	Copyright (C) 2000 YAEGASHI Takeshi
 *	Generic scan keyboard driver
 */

#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/mm.h>
#include <linux/signal.h>
#include <linux/init.h>
#include <linux/kbd_ll.h>
#include <linux/delay.h>
#include <linux/random.h>
#include <linux/poll.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/kbd_kern.h>
#include <linux/timer.h>

#define SCANHZ	(HZ/20)

struct scan_keyboard {
	struct scan_keyboard *next;
	int (*scan)(unsigned char *buffer);
	const unsigned char *table;
	unsigned char *s0, *s1;
	int length;
};

static int scan_jiffies=0;
static struct scan_keyboard *keyboards=NULL;
struct timer_list scan_timer;

static void check_kbd(const unsigned char *table,
		      unsigned char *new, unsigned char *old, int length)
{
	int need_tasklet_schedule=0;
	unsigned int xor, bit;
	
	while(length-->0) {
		if((xor=*new^*old)==0) {
			table+=8;
		}
		else {
			for(bit=0x01; bit<0x100; bit<<=1) {
				if(xor&bit) {
					handle_scancode(*table, !(*new&bit));
					need_tasklet_schedule=1;
#if 0
					printk("0x%x %s\n", *table, (*new&bit)?"released":"pressed");
#endif
				}
				table++;
			}
		}
		new++; old++;
	}

	if(need_tasklet_schedule)
		tasklet_schedule(&keyboard_tasklet);
}


static void scan_kbd(unsigned long dummy)
{
	struct scan_keyboard *kbd;

	scan_jiffies++;

	for(kbd=keyboards; kbd!=NULL; kbd=kbd->next) {
		if(scan_jiffies&1) {
			if(!kbd->scan(kbd->s0))
				check_kbd(kbd->table,
					  kbd->s0, kbd->s1, kbd->length);
			else
				memcpy(kbd->s0, kbd->s1, kbd->length);
		}
		else {
			if(!kbd->scan(kbd->s1))
				check_kbd(kbd->table,
					  kbd->s1, kbd->s0, kbd->length);
			else
				memcpy(kbd->s1, kbd->s0, kbd->length);
		}
		
	}

	init_timer(&scan_timer);
	scan_timer.expires = jiffies + SCANHZ;
	scan_timer.data = 0;
	scan_timer.function = scan_kbd;
	add_timer(&scan_timer);
}


int register_scan_keyboard(int (*scan)(unsigned char *buffer),
			   const unsigned char *table,
			   int length)
{
	struct scan_keyboard *kbd;

	kbd = kmalloc(sizeof(struct scan_keyboard), GFP_KERNEL);
	if (kbd == NULL)
		goto error_out;

	kbd->scan=scan;
	kbd->table=table;
	kbd->length=length;

	kbd->s0 = kmalloc(length, GFP_KERNEL);
	if (kbd->s0 == NULL)
		goto error_free_kbd;

	kbd->s1 = kmalloc(length, GFP_KERNEL);
	if (kbd->s1 == NULL)
		goto error_free_s0;

	memset(kbd->s0, -1, kbd->length);
	memset(kbd->s1, -1, kbd->length);
	
	kbd->next=keyboards;
	keyboards=kbd;

	return 0;

 error_free_s0:
	kfree(kbd->s0);

 error_free_kbd:
	kfree(kbd);

 error_out:
	return -ENOMEM;
}
			      
			      
void __init scan_kbd_init(void)
{
	init_timer(&scan_timer);
	scan_timer.expires = jiffies + SCANHZ;
	scan_timer.data = 0;
	scan_timer.function = scan_kbd;
	add_timer(&scan_timer);

	printk(KERN_INFO "Generic scan keyboard driver initialized\n");
}

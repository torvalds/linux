/*
 *
 * BRIEF MODULE DESCRIPTION
 *	Qtronix 990P infrared keyboard driver.
 *
 *
 * Copyright 2001 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *         	ppopov@mvista.com or source@mvista.com
 *
 *
 *  The bottom portion of this driver was take from 
 *  pc_keyb.c  Please see that file for copyrights.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/config.h>

/* 
 * NOTE:  
 *
 *	This driver has only been tested with the Consumer IR
 *	port of the ITE 8172 system controller.
 *
 *	You do not need this driver if you are using the ps/2 or
 *	USB adapter that the keyboard ships with.  You only need 
 *	this driver if your board has a IR port and the keyboard
 *	data is being sent directly to the IR.  In that case,
 *	you also need some low-level IR support. See it8172_cir.c.
 *	
 */

#ifdef CONFIG_QTRONIX_KEYBOARD

#include <linux/module.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kernel.h>

#include <asm/it8172/it8172.h>
#include <asm/it8172/it8172_int.h>
#include <asm/it8172/it8172_cir.h>

#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/mm.h>
#include <linux/signal.h>
#include <linux/init.h>
#include <linux/kbd_ll.h>
#include <linux/delay.h>
#include <linux/poll.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/kbd_kern.h>
#include <linux/smp_lock.h>
#include <asm/io.h>
#include <linux/pc_keyb.h>

#include <asm/keyboard.h>
#include <linux/bitops.h>
#include <asm/uaccess.h>
#include <asm/irq.h>
#include <asm/system.h>

#define leading1 0
#define leading2 0xF

#define KBD_CIR_PORT 0
#define AUX_RECONNECT 170 /* scancode when ps2 device is plugged (back) in */

static int data_index;
struct cir_port *cir;
static unsigned char kbdbytes[5];
static unsigned char cir_data[32]; /* we only need 16 chars */

static void kbd_int_handler(int irq, void *dev_id, struct pt_regs *regs);
static int handle_data(unsigned char *p_data);
static inline void handle_mouse_event(unsigned char scancode);
static inline void handle_keyboard_event(unsigned char scancode, int down);
static int __init psaux_init(void);

static struct aux_queue *queue;	/* Mouse data buffer. */
static int aux_count = 0;

/*
 * Keys accessed through the 'Fn' key
 * The Fn key does not produce a key-up sequence. So, the first
 * time the user presses it, it will be key-down event. The key
 * stays down until the user presses it again.
 */
#define NUM_FN_KEYS 56
static unsigned char fn_keys[NUM_FN_KEYS] = {
	0,0,0,0,0,0,0,0,        /* 0 7   */
	8,9,10,93,0,0,0,0,      /* 8 15  */
	0,0,0,0,0,0,0,5,        /* 16 23 */
	6,7,91,0,0,0,0,0,       /* 24 31 */
	0,0,0,0,0,2,3,4,        /* 32 39 */
	92,0,0,0,0,0,0,0,       /* 40 47 */
	0,0,0,0,11,0,94,95        /* 48 55 */

};

void __init init_qtronix_990P_kbd(void)
{
	int retval;

	cir = (struct cir_port *)kmalloc(sizeof(struct cir_port), GFP_KERNEL);
	if (!cir) {
		printk("Unable to initialize Qtronix keyboard\n");
		return;
	}

	/* 
	 * revisit
	 * this should be programmable, somehow by the, by the user.
	 */
	cir->port = KBD_CIR_PORT;
	cir->baud_rate = 0x1d;
	cir->rdwos = 0;
	cir->rxdcr = 0x3;
	cir->hcfs = 0;
	cir->fifo_tl = 0;
	cir->cfq = 0x1d;
	cir_port_init(cir);

	retval = request_irq(IT8172_CIR0_IRQ, kbd_int_handler, 
			(unsigned long )(SA_INTERRUPT|SA_SHIRQ), 
			(const char *)"Qtronix IR Keyboard", (void *)cir);

	if (retval) {
		printk("unable to allocate cir %d irq %d\n", 
				cir->port, IT8172_CIR0_IRQ);
	}
#ifdef CONFIG_PSMOUSE
	psaux_init();
#endif
}

static inline unsigned char BitReverse(unsigned short key)
{
	unsigned char rkey = 0;
	rkey |= (key & 0x1) << 7;
	rkey |= (key & 0x2) << 5;
	rkey |= (key & 0x4) << 3;
	rkey |= (key & 0x8) << 1;
	rkey |= (key & 0x10) >> 1;
	rkey |= (key & 0x20) >> 3;
	rkey |= (key & 0x40) >> 5;
	rkey |= (key & 0x80) >> 7;
	return rkey;

}


static inline u_int8_t UpperByte(u_int8_t data)
{
	return (data >> 4);
}


static inline u_int8_t LowerByte(u_int8_t data)
{
	return (data & 0xF);
}


int CheckSumOk(u_int8_t byte1, u_int8_t byte2, 
		u_int8_t byte3, u_int8_t byte4, u_int8_t byte5)
{
	u_int8_t CheckSum;

	CheckSum = (byte1 & 0x0F) + byte2 + byte3 + byte4 + byte5;
	if ( LowerByte(UpperByte(CheckSum) + LowerByte(CheckSum)) != UpperByte(byte1) )
		return 0;
	else
		return 1;
}


static void kbd_int_handler(int irq, void *dev_id, struct pt_regs *regs)
{
	struct cir_port *cir;
	int j;
	unsigned char int_status;

	cir = (struct cir_port *)dev_id;
	int_status = get_int_status(cir);
	if (int_status & 0x4) {
		clear_fifo(cir);
		return;
	}

	while (cir_get_rx_count(cir)) {

		cir_data[data_index] = cir_read_data(cir);

		if (data_index == 0) {/* expecting first byte */
			if (cir_data[data_index] != leading1) {
				//printk("!leading byte %x\n", cir_data[data_index]);
				set_rx_active(cir);
				clear_fifo(cir);
				continue;
			}
		}
		if (data_index == 1) {
			if ((cir_data[data_index] & 0xf) != leading2) {
				set_rx_active(cir);
				data_index = 0; /* start over */
				clear_fifo(cir);
				continue;
			}
		}

		if ( (cir_data[data_index] == 0xff)) { /* last byte */
			//printk("data_index %d\n", data_index);
			set_rx_active(cir);
#if 0
			for (j=0; j<=data_index; j++) {
				printk("rx_data %d:  %x\n", j, cir_data[j]);
			}
#endif
			data_index = 0;
			handle_data(cir_data);
			return;
		}
		else if (data_index>16) {
			set_rx_active(cir);
#if 0
			printk("warning: data_index %d\n", data_index);
			for (j=0; j<=data_index; j++) {
				printk("rx_data %d:  %x\n", j, cir_data[j]);
			}
#endif
			data_index = 0;
			clear_fifo(cir);
			return;
		}
		data_index++;
	}
}


#define NUM_KBD_BYTES 5
static int handle_data(unsigned char *p_data)
{
	u_int32_t bit_bucket;
	u_int32_t i, j;
	u_int32_t got_bits, next_byte;
	int down = 0;

	/* Reorganize the bit stream */
	for (i=0; i<16; i++)
		p_data[i] = BitReverse(~p_data[i]);

	/* 
	 * We've already previously checked that p_data[0]
	 * is equal to leading1 and that (p_data[1] & 0xf)
	 * is equal to leading2. These twelve bits are the
	 * leader code.  We can now throw them away (the 12
	 * bits) and continue parsing the stream.
	 */
	bit_bucket = p_data[1] << 12;
	got_bits = 4;
	next_byte = 2;

	/* 
	 * Process four bits at a time
	 */
	for (i=0; i<NUM_KBD_BYTES; i++) {

		kbdbytes[i]=0;

		for (j=0; j<8; j++) /* 8 bits per byte */
		{
			if (got_bits < 4) {
				bit_bucket |= (p_data[next_byte++] << (8 - got_bits));
				got_bits += 8;
			}

			if ((bit_bucket & 0xF000) == 0x8000) { 
				/* Convert 1000b to 1 */
				kbdbytes[i] = 0x80 | (kbdbytes[i] >> 1);
				got_bits -= 4;
				bit_bucket = bit_bucket << 4;
			}
			else if ((bit_bucket & 0xC000) == 0x8000) {
				/* Convert 10b to 0 */
				kbdbytes[i] =  kbdbytes[i] >> 1;
				got_bits -= 2;
				bit_bucket = bit_bucket << 2;
			}
			else {
				/* bad serial stream */
				return 1;
			}

			if (next_byte > 16) {
				//printk("error: too many bytes\n");
				return 1;
			}
		}
	}


	if (!CheckSumOk(kbdbytes[0], kbdbytes[1], 
				kbdbytes[2], kbdbytes[3], kbdbytes[4])) {
		//printk("checksum failed\n");
		return 1;
	}

	if (kbdbytes[1] & 0x08) {
		//printk("m: %x %x %x\n", kbdbytes[1], kbdbytes[2], kbdbytes[3]);
		handle_mouse_event(kbdbytes[1]);
		handle_mouse_event(kbdbytes[2]);
		handle_mouse_event(kbdbytes[3]);
	}
	else {
		if (kbdbytes[2] == 0) down = 1;
#if 0
		if (down)
			printk("down %d\n", kbdbytes[3]);
		else
			printk("up %d\n", kbdbytes[3]);
#endif
		handle_keyboard_event(kbdbytes[3], down);
	}
	return 0;
}


DEFINE_SPINLOCK(kbd_controller_lock);
static unsigned char handle_kbd_event(void);


int kbd_setkeycode(unsigned int scancode, unsigned int keycode)
{
	printk("kbd_setkeycode scancode %x keycode %x\n", scancode, keycode);
	return 0;
}

int kbd_getkeycode(unsigned int scancode)
{
	return scancode;
}


int kbd_translate(unsigned char scancode, unsigned char *keycode,
		    char raw_mode)
{
	static int prev_scancode = 0;

	if (scancode == 0x00 || scancode == 0xff) {
		prev_scancode = 0;
		return 0;
	}

	/* todo */
	if (!prev_scancode && scancode == 160) { /* Fn key down */
		//printk("Fn key down\n");
		prev_scancode = 160;
		return 0;
	}
	else if (prev_scancode && scancode == 160) { /* Fn key up */
		//printk("Fn key up\n");
		prev_scancode = 0;
		return 0;
	}

	/* todo */
	if (prev_scancode == 160) {
		if (scancode <= NUM_FN_KEYS) {
			*keycode = fn_keys[scancode];
			//printk("fn keycode %d\n", *keycode);
		}
		else
			return 0;
	} 
	else if (scancode <= 127) {
		*keycode = scancode;
	}
	else
		return 0;


 	return 1;
}

char kbd_unexpected_up(unsigned char keycode)
{
	//printk("kbd_unexpected_up\n");
	return 0;
}

static unsigned char kbd_exists = 1;

static inline void handle_keyboard_event(unsigned char scancode, int down)
{
	kbd_exists = 1;
	handle_scancode(scancode, down);
	tasklet_schedule(&keyboard_tasklet);
}	


void kbd_leds(unsigned char leds)
{
}

/* dummy */
void kbd_init_hw(void)
{
}



static inline void handle_mouse_event(unsigned char scancode)
{
	if(scancode == AUX_RECONNECT){
		queue->head = queue->tail = 0;  /* Flush input queue */
	//	__aux_write_ack(AUX_ENABLE_DEV);  /* ping the mouse :) */
		return;
	}

	if (aux_count) {
		int head = queue->head;

		queue->buf[head] = scancode;
		head = (head + 1) & (AUX_BUF_SIZE-1);
		if (head != queue->tail) {
			queue->head = head;
			kill_fasync(&queue->fasync, SIGIO, POLL_IN);
			wake_up_interruptible(&queue->proc_list);
		}
	}
}

static unsigned char get_from_queue(void)
{
	unsigned char result;
	unsigned long flags;

	spin_lock_irqsave(&kbd_controller_lock, flags);
	result = queue->buf[queue->tail];
	queue->tail = (queue->tail + 1) & (AUX_BUF_SIZE-1);
	spin_unlock_irqrestore(&kbd_controller_lock, flags);
	return result;
}


static inline int queue_empty(void)
{
	return queue->head == queue->tail;
}

static int fasync_aux(int fd, struct file *filp, int on)
{
	int retval;

	//printk("fasync_aux\n");
	retval = fasync_helper(fd, filp, on, &queue->fasync);
	if (retval < 0)
		return retval;
	return 0;
}


/*
 * Random magic cookie for the aux device
 */
#define AUX_DEV ((void *)queue)

static int release_aux(struct inode * inode, struct file * file)
{
	fasync_aux(-1, file, 0);
	aux_count--;
	return 0;
}

static int open_aux(struct inode * inode, struct file * file)
{
	if (aux_count++) {
		return 0;
	}
	queue->head = queue->tail = 0;		/* Flush input queue */
	return 0;
}

/*
 * Put bytes from input queue to buffer.
 */

static ssize_t read_aux(struct file * file, char * buffer,
			size_t count, loff_t *ppos)
{
	DECLARE_WAITQUEUE(wait, current);
	ssize_t i = count;
	unsigned char c;

	if (queue_empty()) {
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;
		add_wait_queue(&queue->proc_list, &wait);
repeat:
		set_current_state(TASK_INTERRUPTIBLE);
		if (queue_empty() && !signal_pending(current)) {
			schedule();
			goto repeat;
		}
		current->state = TASK_RUNNING;
		remove_wait_queue(&queue->proc_list, &wait);
	}
	while (i > 0 && !queue_empty()) {
		c = get_from_queue();
		put_user(c, buffer++);
		i--;
	}
	if (count-i) {
		struct inode *inode = file->f_dentry->d_inode;
		inode->i_atime = current_fs_time(inode->i_sb);
		return count-i;
	}
	if (signal_pending(current))
		return -ERESTARTSYS;
	return 0;
}

/*
 * Write to the aux device.
 */

static ssize_t write_aux(struct file * file, const char * buffer,
			 size_t count, loff_t *ppos)
{
	/*
	 * The ITE boards this was tested on did not have the
	 * transmit wires connected.
	 */
	return count;
}

static unsigned int aux_poll(struct file *file, poll_table * wait)
{
	poll_wait(file, &queue->proc_list, wait);
	if (!queue_empty())
		return POLLIN | POLLRDNORM;
	return 0;
}

struct file_operations psaux_fops = {
	.read		= read_aux,
	.write		= write_aux,
	.poll		= aux_poll,
	.open		= open_aux,
	.release	= release_aux,
	.fasync		= fasync_aux,
};

/*
 * Initialize driver.
 */
static struct miscdevice psaux_mouse = {
	PSMOUSE_MINOR, "psaux", &psaux_fops
};

static int __init psaux_init(void)
{
	int retval;

	retval = misc_register(&psaux_mouse);
	if(retval < 0)
		return retval;

	queue = (struct aux_queue *) kmalloc(sizeof(*queue), GFP_KERNEL);
	memset(queue, 0, sizeof(*queue));
	queue->head = queue->tail = 0;
	init_waitqueue_head(&queue->proc_list);

	return 0;
}
module_init(init_qtronix_990P_kbd);
#endif

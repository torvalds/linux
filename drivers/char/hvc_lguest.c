/*D:300
 * The Guest console driver
 *
 * This is a trivial console driver: we use lguest's DMA mechanism to send
 * bytes out, and register a DMA buffer to receive bytes in.  It is assumed to
 * be present and available from the very beginning of boot.
 *
 * Writing console drivers is one of the few remaining Dark Arts in Linux.
 * Fortunately for us, the path of virtual consoles has been well-trodden by
 * the PowerPC folks, who wrote "hvc_console.c" to generically support any
 * virtual console.  We use that infrastructure which only requires us to write
 * the basic put_chars and get_chars functions and call the right register
 * functions.
 :*/

/*M:002 The console can be flooded: while the Guest is processing input the
 * Host can send more.  Buffering in the Host could alleviate this, but it is a
 * difficult problem in general. :*/
/* Copyright (C) 2006 Rusty Russell, IBM Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/err.h>
#include <linux/init.h>
#include <linux/lguest_bus.h>
#include <asm/paravirt.h>
#include "hvc_console.h"

/*D:340 This is our single console input buffer, with associated "struct
 * lguest_dma" referring to it.  Note the 0-terminated length array, and the
 * use of physical address for the buffer itself. */
static char inbuf[256];
static struct lguest_dma cons_input = { .used_len = 0,
					.addr[0] = __pa(inbuf),
					.len[0] = sizeof(inbuf),
					.len[1] = 0 };

/*D:310 The put_chars() callback is pretty straightforward.
 *
 * First we put the pointer and length in a "struct lguest_dma": we only have
 * one pointer, so we set the second length to 0.  Then we use SEND_DMA to send
 * the data to (Host) buffers attached to the console key.  Usually a device's
 * key is a physical address within the device's memory, but because the
 * console device doesn't have any associated physical memory, we use the
 * LGUEST_CONSOLE_DMA_KEY constant (aka 0). */
static int put_chars(u32 vtermno, const char *buf, int count)
{
	struct lguest_dma dma;

	/* FIXME: DMA buffers in a "struct lguest_dma" are not allowed
	 * to go over page boundaries.  This never seems to happen,
	 * but if it did we'd need to fix this code. */
	dma.len[0] = count;
	dma.len[1] = 0;
	dma.addr[0] = __pa(buf);

	lguest_send_dma(LGUEST_CONSOLE_DMA_KEY, &dma);
	/* We're expected to return the amount of data we wrote: all of it. */
	return count;
}

/*D:350 get_chars() is the callback from the hvc_console infrastructure when
 * an interrupt is received.
 *
 * Firstly we see if our buffer has been filled: if not, we return.  The rest
 * of the code deals with the fact that the hvc_console() infrastructure only
 * asks us for 16 bytes at a time.  We keep a "cons_offset" variable for
 * partially-read buffers. */
static int get_chars(u32 vtermno, char *buf, int count)
{
	static int cons_offset;

	/* Nothing left to see here... */
	if (!cons_input.used_len)
		return 0;

	/* You want more than we have to give?  Well, try wanting less! */
	if (cons_input.used_len - cons_offset < count)
		count = cons_input.used_len - cons_offset;

	/* Copy across to their buffer and increment offset. */
	memcpy(buf, inbuf + cons_offset, count);
	cons_offset += count;

	/* Finished?  Zero offset, and reset cons_input so Host will use it
	 * again. */
	if (cons_offset == cons_input.used_len) {
		cons_offset = 0;
		cons_input.used_len = 0;
	}
	return count;
}
/*:*/

static struct hv_ops lguest_cons = {
	.get_chars = get_chars,
	.put_chars = put_chars,
};

/*D:320 Console drivers are initialized very early so boot messages can go
 * out.  At this stage, the console is output-only.  Our driver checks we're a
 * Guest, and if so hands hvc_instantiate() the console number (0), priority
 * (0), and the struct hv_ops containing the put_chars() function. */
static int __init cons_init(void)
{
	if (strcmp(paravirt_ops.name, "lguest") != 0)
		return 0;

	return hvc_instantiate(0, 0, &lguest_cons);
}
console_initcall(cons_init);

/*D:370 To set up and manage our virtual console, we call hvc_alloc() and
 * stash the result in the private pointer of the "struct lguest_device".
 * Since we never remove the console device we never need this pointer again,
 * but using ->private is considered good form, and you never know who's going
 * to copy your driver.
 *
 * Once the console is set up, we bind our input buffer ready for input. */
static int lguestcons_probe(struct lguest_device *lgdev)
{
	int err;

	/* The first argument of hvc_alloc() is the virtual console number, so
	 * we use zero.  The second argument is the interrupt number.
	 *
	 * The third argument is a "struct hv_ops" containing the put_chars()
	 * and get_chars() pointers.  The final argument is the output buffer
	 * size: we use 256 and expect the Host to have room for us to send
	 * that much. */
	lgdev->private = hvc_alloc(0, lgdev_irq(lgdev), &lguest_cons, 256);
	if (IS_ERR(lgdev->private))
		return PTR_ERR(lgdev->private);

	/* We bind a single DMA buffer at key LGUEST_CONSOLE_DMA_KEY.
	 * "cons_input" is that statically-initialized global DMA buffer we saw
	 * above, and we also give the interrupt we want. */
	err = lguest_bind_dma(LGUEST_CONSOLE_DMA_KEY, &cons_input, 1,
			      lgdev_irq(lgdev));
	if (err)
		printk("lguest console: failed to bind buffer.\n");
	return err;
}
/* Note the use of lgdev_irq() for the interrupt number.  We tell hvc_alloc()
 * to expect input when this interrupt is triggered, and then tell
 * lguest_bind_dma() that is the interrupt to send us when input comes in. */

/*D:360 From now on the console driver follows standard Guest driver form:
 * register_lguest_driver() registers the device type and probe function, and
 * the probe function sets up the device.
 *
 * The standard "struct lguest_driver": */
static struct lguest_driver lguestcons_drv = {
	.name = "lguestcons",
	.owner = THIS_MODULE,
	.device_type = LGUEST_DEVICE_T_CONSOLE,
	.probe = lguestcons_probe,
};

/* The standard init function */
static int __init hvc_lguest_init(void)
{
	return register_lguest_driver(&lguestcons_drv);
}
module_init(hvc_lguest_init);

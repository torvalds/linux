/*
 * xen console driver interface to hvc_console.c
 *
 * (c) 2007 Gerd Hoffmann <kraxel@suse.de>
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/console.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/types.h>

#include <asm/xen/hypervisor.h>
#include <xen/page.h>
#include <xen/events.h>
#include <xen/interface/io/console.h>
#include <xen/hvc-console.h>

#include "hvc_console.h"

#define HVC_COOKIE   0x58656e /* "Xen" in hex */

static struct hvc_struct *hvc;
static int xencons_irq;

/* ------------------------------------------------------------------ */

static inline struct xencons_interface *xencons_interface(void)
{
	return mfn_to_virt(xen_start_info->console.domU.mfn);
}

static inline void notify_daemon(void)
{
	/* Use evtchn: this is called early, before irq is set up. */
	notify_remote_via_evtchn(xen_start_info->console.domU.evtchn);
}

static int write_console(uint32_t vtermno, const char *data, int len)
{
	struct xencons_interface *intf = xencons_interface();
	XENCONS_RING_IDX cons, prod;
	int sent = 0;

	cons = intf->out_cons;
	prod = intf->out_prod;
	mb();			/* update queue values before going on */
	BUG_ON((prod - cons) > sizeof(intf->out));

	while ((sent < len) && ((prod - cons) < sizeof(intf->out)))
		intf->out[MASK_XENCONS_IDX(prod++, intf->out)] = data[sent++];

	wmb();			/* write ring before updating pointer */
	intf->out_prod = prod;

	notify_daemon();
	return sent;
}

static int read_console(uint32_t vtermno, char *buf, int len)
{
	struct xencons_interface *intf = xencons_interface();
	XENCONS_RING_IDX cons, prod;
	int recv = 0;

	cons = intf->in_cons;
	prod = intf->in_prod;
	mb();			/* get pointers before reading ring */
	BUG_ON((prod - cons) > sizeof(intf->in));

	while (cons != prod && recv < len)
		buf[recv++] = intf->in[MASK_XENCONS_IDX(cons++, intf->in)];

	mb();			/* read ring before consuming */
	intf->in_cons = cons;

	notify_daemon();
	return recv;
}

static struct hv_ops hvc_ops = {
	.get_chars = read_console,
	.put_chars = write_console,
};

static int __init xen_init(void)
{
	struct hvc_struct *hp;

	if (!is_running_on_xen())
		return 0;

	xencons_irq = bind_evtchn_to_irq(xen_start_info->console.domU.evtchn);
	if (xencons_irq < 0)
		xencons_irq = 0 /* NO_IRQ */;
	hp = hvc_alloc(HVC_COOKIE, xencons_irq, &hvc_ops, 256);
	if (IS_ERR(hp))
		return PTR_ERR(hp);

	hvc = hp;
	return 0;
}

static void __exit xen_fini(void)
{
	if (hvc)
		hvc_remove(hvc);
}

static int xen_cons_init(void)
{
	if (!is_running_on_xen())
		return 0;

	hvc_instantiate(HVC_COOKIE, 0, &hvc_ops);
	return 0;
}

module_init(xen_init);
module_exit(xen_fini);
console_initcall(xen_cons_init);

static void xenboot_write_console(struct console *console, const char *string,
				  unsigned len)
{
	unsigned int linelen, off = 0;
	const char *pos;

	while (off < len && NULL != (pos = strchr(string+off, '\n'))) {
		linelen = pos-string+off;
		if (off + linelen > len)
			break;
		write_console(0, string+off, linelen);
		write_console(0, "\r\n", 2);
		off += linelen + 1;
	}
	if (off < len)
		write_console(0, string+off, len-off);
}

struct console xenboot_console = {
	.name		= "xenboot",
	.write		= xenboot_write_console,
	.flags		= CON_PRINTBUFFER | CON_BOOT,
};

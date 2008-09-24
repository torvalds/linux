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

static unsigned long console_pfn = ~0ul;

static inline struct xencons_interface *xencons_interface(void)
{
	if (console_pfn == ~0ul)
		return mfn_to_virt(xen_start_info->console.domU.mfn);
	else
		return __va(console_pfn << PAGE_SHIFT);
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
	.notifier_add = notifier_add_irq,
	.notifier_del = notifier_del_irq,
};

static int __init xen_init(void)
{
	struct hvc_struct *hp;

	if (!is_running_on_xen() ||
	    is_initial_xendomain() ||
	    !xen_start_info->console.domU.evtchn)
		return -ENODEV;

	xencons_irq = bind_evtchn_to_irq(xen_start_info->console.domU.evtchn);
	if (xencons_irq < 0)
		xencons_irq = 0; /* NO_IRQ */

	hp = hvc_alloc(HVC_COOKIE, xencons_irq, &hvc_ops, 256);
	if (IS_ERR(hp))
		return PTR_ERR(hp);

	hvc = hp;

	console_pfn = mfn_to_pfn(xen_start_info->console.domU.mfn);

	return 0;
}

void xen_console_resume(void)
{
	if (xencons_irq)
		rebind_evtchn_irq(xen_start_info->console.domU.evtchn, xencons_irq);
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

static void raw_console_write(const char *str, int len)
{
	while(len > 0) {
		int rc = HYPERVISOR_console_io(CONSOLEIO_write, len, (char *)str);
		if (rc <= 0)
			break;

		str += rc;
		len -= rc;
	}
}

#ifdef CONFIG_EARLY_PRINTK
static void xenboot_write_console(struct console *console, const char *string,
				  unsigned len)
{
	unsigned int linelen, off = 0;
	const char *pos;

	raw_console_write(string, len);

	write_console(0, "(early) ", 8);
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
	.flags		= CON_PRINTBUFFER | CON_BOOT | CON_ANYTIME,
};
#endif	/* CONFIG_EARLY_PRINTK */

void xen_raw_console_write(const char *str)
{
	raw_console_write(str, strlen(str));
}

void xen_raw_printk(const char *fmt, ...)
{
	static char buf[512];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	xen_raw_console_write(buf);
}

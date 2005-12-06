/* 
        paride.c  (c) 1997-8  Grant R. Guenther <grant@torque.net>
                              Under the terms of the GNU General Public License.

	This is the base module for the family of device drivers
        that support parallel port IDE devices.  

*/

/* Changes:

	1.01	GRG 1998.05.03	Use spinlocks
	1.02	GRG 1998.05.05  init_proto, release_proto, ktti
	1.03	GRG 1998.08.15  eliminate compiler warning
	1.04    GRG 1998.11.28  added support for FRIQ 
	1.05    TMW 2000.06.06  use parport_find_number instead of
				parport_enumerate
	1.06    TMW 2001.03.26  more sane parport-or-not resource management
*/

#define PI_VERSION      "1.06"

#include <linux/module.h>
#include <linux/config.h>
#include <linux/kmod.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/string.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/sched.h>	/* TASK_* */

#ifdef CONFIG_PARPORT_MODULE
#define CONFIG_PARPORT
#endif

#ifdef CONFIG_PARPORT
#include <linux/parport.h>
#endif

#include "paride.h"

MODULE_LICENSE("GPL");

#define MAX_PROTOS	32

static struct pi_protocol *protocols[MAX_PROTOS];

static DEFINE_SPINLOCK(pi_spinlock);

void pi_write_regr(PIA * pi, int cont, int regr, int val)
{
	pi->proto->write_regr(pi, cont, regr, val);
}

EXPORT_SYMBOL(pi_write_regr);

int pi_read_regr(PIA * pi, int cont, int regr)
{
	return pi->proto->read_regr(pi, cont, regr);
}

EXPORT_SYMBOL(pi_read_regr);

void pi_write_block(PIA * pi, char *buf, int count)
{
	pi->proto->write_block(pi, buf, count);
}

EXPORT_SYMBOL(pi_write_block);

void pi_read_block(PIA * pi, char *buf, int count)
{
	pi->proto->read_block(pi, buf, count);
}

EXPORT_SYMBOL(pi_read_block);

#ifdef CONFIG_PARPORT

static void pi_wake_up(void *p)
{
	PIA *pi = (PIA *) p;
	unsigned long flags;
	void (*cont) (void) = NULL;

	spin_lock_irqsave(&pi_spinlock, flags);

	if (pi->claim_cont && !parport_claim(pi->pardev)) {
		cont = pi->claim_cont;
		pi->claim_cont = NULL;
		pi->claimed = 1;
	}

	spin_unlock_irqrestore(&pi_spinlock, flags);

	wake_up(&(pi->parq));

	if (cont)
		cont();
}

#endif

int pi_schedule_claimed(PIA * pi, void (*cont) (void))
{
#ifdef CONFIG_PARPORT
	unsigned long flags;

	spin_lock_irqsave(&pi_spinlock, flags);
	if (pi->pardev && parport_claim(pi->pardev)) {
		pi->claim_cont = cont;
		spin_unlock_irqrestore(&pi_spinlock, flags);
		return 0;
	}
	pi->claimed = 1;
	spin_unlock_irqrestore(&pi_spinlock, flags);
#endif
	return 1;
}
EXPORT_SYMBOL(pi_schedule_claimed);

void pi_do_claimed(PIA * pi, void (*cont) (void))
{
	if (pi_schedule_claimed(pi, cont))
		cont();
}

EXPORT_SYMBOL(pi_do_claimed);

static void pi_claim(PIA * pi)
{
	if (pi->claimed)
		return;
	pi->claimed = 1;
#ifdef CONFIG_PARPORT
	if (pi->pardev)
		wait_event(pi->parq,
			   !parport_claim((struct pardevice *) pi->pardev));
#endif
}

static void pi_unclaim(PIA * pi)
{
	pi->claimed = 0;
#ifdef CONFIG_PARPORT
	if (pi->pardev)
		parport_release((struct pardevice *) (pi->pardev));
#endif
}

void pi_connect(PIA * pi)
{
	pi_claim(pi);
	pi->proto->connect(pi);
}

EXPORT_SYMBOL(pi_connect);

void pi_disconnect(PIA * pi)
{
	pi->proto->disconnect(pi);
	pi_unclaim(pi);
}

EXPORT_SYMBOL(pi_disconnect);

static void pi_unregister_parport(PIA * pi)
{
#ifdef CONFIG_PARPORT
	if (pi->pardev) {
		parport_unregister_device((struct pardevice *) (pi->pardev));
		pi->pardev = NULL;
	}
#endif
}

void pi_release(PIA * pi)
{
	pi_unregister_parport(pi);
#ifndef CONFIG_PARPORT
	if (pi->reserved)
		release_region(pi->port, pi->reserved);
#endif				/* !CONFIG_PARPORT */
	if (pi->proto->release_proto)
		pi->proto->release_proto(pi);
	module_put(pi->proto->owner);
}

EXPORT_SYMBOL(pi_release);

static int default_test_proto(PIA * pi, char *scratch, int verbose)
{
	int j, k;
	int e[2] = { 0, 0 };

	pi->proto->connect(pi);

	for (j = 0; j < 2; j++) {
		pi_write_regr(pi, 0, 6, 0xa0 + j * 0x10);
		for (k = 0; k < 256; k++) {
			pi_write_regr(pi, 0, 2, k ^ 0xaa);
			pi_write_regr(pi, 0, 3, k ^ 0x55);
			if (pi_read_regr(pi, 0, 2) != (k ^ 0xaa))
				e[j]++;
		}
	}
	pi->proto->disconnect(pi);

	if (verbose)
		printk("%s: %s: port 0x%x, mode  %d, test=(%d,%d)\n",
		       pi->device, pi->proto->name, pi->port,
		       pi->mode, e[0], e[1]);

	return (e[0] && e[1]);	/* not here if both > 0 */
}

static int pi_test_proto(PIA * pi, char *scratch, int verbose)
{
	int res;

	pi_claim(pi);
	if (pi->proto->test_proto)
		res = pi->proto->test_proto(pi, scratch, verbose);
	else
		res = default_test_proto(pi, scratch, verbose);
	pi_unclaim(pi);

	return res;
}

int pi_register(PIP * pr)
{
	int k;

	for (k = 0; k < MAX_PROTOS; k++)
		if (protocols[k] && !strcmp(pr->name, protocols[k]->name)) {
			printk("paride: %s protocol already registered\n",
			       pr->name);
			return 0;
		}
	k = 0;
	while ((k < MAX_PROTOS) && (protocols[k]))
		k++;
	if (k == MAX_PROTOS) {
		printk("paride: protocol table full\n");
		return 0;
	}
	protocols[k] = pr;
	pr->index = k;
	printk("paride: %s registered as protocol %d\n", pr->name, k);
	return 1;
}

EXPORT_SYMBOL(pi_register);

void pi_unregister(PIP * pr)
{
	if (!pr)
		return;
	if (protocols[pr->index] != pr) {
		printk("paride: %s not registered\n", pr->name);
		return;
	}
	protocols[pr->index] = NULL;
}

EXPORT_SYMBOL(pi_unregister);

static int pi_register_parport(PIA * pi, int verbose)
{
#ifdef CONFIG_PARPORT

	struct parport *port;

	port = parport_find_base(pi->port);
	if (!port)
		return 0;

	pi->pardev = parport_register_device(port,
					     pi->device, NULL,
					     pi_wake_up, NULL, 0, (void *) pi);
	parport_put_port(port);
	if (!pi->pardev)
		return 0;

	init_waitqueue_head(&pi->parq);

	if (verbose)
		printk("%s: 0x%x is %s\n", pi->device, pi->port, port->name);

	pi->parname = (char *) port->name;
#endif

	return 1;
}

static int pi_probe_mode(PIA * pi, int max, char *scratch, int verbose)
{
	int best, range;

	if (pi->mode != -1) {
		if (pi->mode >= max)
			return 0;
		range = 3;
		if (pi->mode >= pi->proto->epp_first)
			range = 8;
		if ((range == 8) && (pi->port % 8))
			return 0;
		pi->reserved = range;
		return (!pi_test_proto(pi, scratch, verbose));
	}
	best = -1;
	for (pi->mode = 0; pi->mode < max; pi->mode++) {
		range = 3;
		if (pi->mode >= pi->proto->epp_first)
			range = 8;
		if ((range == 8) && (pi->port % 8))
			break;
		pi->reserved = range;
		if (!pi_test_proto(pi, scratch, verbose))
			best = pi->mode;
	}
	pi->mode = best;
	return (best > -1);
}

static int pi_probe_unit(PIA * pi, int unit, char *scratch, int verbose)
{
	int max, s, e;

	s = unit;
	e = s + 1;

	if (s == -1) {
		s = 0;
		e = pi->proto->max_units;
	}

	if (!pi_register_parport(pi, verbose))
		return 0;

	if (pi->proto->test_port) {
		pi_claim(pi);
		max = pi->proto->test_port(pi);
		pi_unclaim(pi);
	} else
		max = pi->proto->max_mode;

	if (pi->proto->probe_unit) {
		pi_claim(pi);
		for (pi->unit = s; pi->unit < e; pi->unit++)
			if (pi->proto->probe_unit(pi)) {
				pi_unclaim(pi);
				if (pi_probe_mode(pi, max, scratch, verbose))
					return 1;
				pi_unregister_parport(pi);
				return 0;
			}
		pi_unclaim(pi);
		pi_unregister_parport(pi);
		return 0;
	}

	if (!pi_probe_mode(pi, max, scratch, verbose)) {
		pi_unregister_parport(pi);
		return 0;
	}
	return 1;

}

int pi_init(PIA * pi, int autoprobe, int port, int mode,
	int unit, int protocol, int delay, char *scratch,
	int devtype, int verbose, char *device)
{
	int p, k, s, e;
	int lpts[7] = { 0x3bc, 0x378, 0x278, 0x268, 0x27c, 0x26c, 0 };

	s = protocol;
	e = s + 1;

	if (!protocols[0])
		request_module("paride_protocol");

	if (autoprobe) {
		s = 0;
		e = MAX_PROTOS;
	} else if ((s < 0) || (s >= MAX_PROTOS) || (port <= 0) ||
		   (!protocols[s]) || (unit < 0) ||
		   (unit >= protocols[s]->max_units)) {
		printk("%s: Invalid parameters\n", device);
		return 0;
	}

	for (p = s; p < e; p++) {
		struct pi_protocol *proto = protocols[p];
		if (!proto)
			continue;
		/* still racy */
		if (!try_module_get(proto->owner))
			continue;
		pi->proto = proto;
		pi->private = 0;
		if (proto->init_proto && proto->init_proto(pi) < 0) {
			pi->proto = NULL;
			module_put(proto->owner);
			continue;
		}
		if (delay == -1)
			pi->delay = pi->proto->default_delay;
		else
			pi->delay = delay;
		pi->devtype = devtype;
		pi->device = device;

		pi->parname = NULL;
		pi->pardev = NULL;
		init_waitqueue_head(&pi->parq);
		pi->claimed = 0;
		pi->claim_cont = NULL;

		pi->mode = mode;
		if (port != -1) {
			pi->port = port;
			if (pi_probe_unit(pi, unit, scratch, verbose))
				break;
			pi->port = 0;
		} else {
			k = 0;
			while ((pi->port = lpts[k++]))
				if (pi_probe_unit
				    (pi, unit, scratch, verbose))
					break;
			if (pi->port)
				break;
		}
		if (pi->proto->release_proto)
			pi->proto->release_proto(pi);
		module_put(proto->owner);
	}

	if (!pi->port) {
		if (autoprobe)
			printk("%s: Autoprobe failed\n", device);
		else
			printk("%s: Adapter not found\n", device);
		return 0;
	}
#ifndef CONFIG_PARPORT
	if (!request_region(pi->port, pi->reserved, pi->device)) {
		printk(KERN_WARNING "paride: Unable to request region 0x%x\n",
		       pi->port);
		return 0;
	}
#endif				/* !CONFIG_PARPORT */

	if (pi->parname)
		printk("%s: Sharing %s at 0x%x\n", pi->device,
		       pi->parname, pi->port);

	pi->proto->log_adapter(pi, scratch, verbose);

	return 1;
}

EXPORT_SYMBOL(pi_init);

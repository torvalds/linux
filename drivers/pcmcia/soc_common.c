/*======================================================================

    Common support code for the PCMCIA control functionality of
    integrated SOCs like the SA-11x0 and PXA2xx microprocessors.

    The contents of this file are subject to the Mozilla Public
    License Version 1.1 (the "License"); you may not use this file
    except in compliance with the License. You may obtain a copy of
    the License at http://www.mozilla.org/MPL/

    Software distributed under the License is distributed on an "AS
    IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
    implied. See the License for the specific language governing
    rights and limitations under the License.

    The initial developer of the original code is John G. Dorsey
    <john+@cs.cmu.edu>.  Portions created by John G. Dorsey are
    Copyright (C) 1999 John G. Dorsey.  All Rights Reserved.

    Alternatively, the contents of this file may be used under the
    terms of the GNU Public License version 2 (the "GPL"), in which
    case the provisions of the GPL are applicable instead of the
    above.  If you wish to allow the use of your version of this file
    only under the terms of the GPL and not to allow others to use
    your version of this file under the MPL, indicate your decision
    by deleting the provisions above and replace them with the notice
    and other provisions required by the GPL.  If you do not delete
    the provisions above, a recipient may use your version of this
    file under either the MPL or the GPL.

======================================================================*/


#include <linux/cpufreq.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <linux/regulator/consumer.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/pci.h>

#include "soc_common.h"

static irqreturn_t soc_common_pcmcia_interrupt(int irq, void *dev);

#ifdef CONFIG_PCMCIA_DEBUG

static int pc_debug;
module_param(pc_debug, int, 0644);

void soc_pcmcia_debug(struct soc_pcmcia_socket *skt, const char *func,
		      int lvl, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;
	if (pc_debug > lvl) {
		va_start(args, fmt);

		vaf.fmt = fmt;
		vaf.va = &args;

		printk(KERN_DEBUG "skt%u: %s: %pV", skt->nr, func, &vaf);

		va_end(args);
	}
}
EXPORT_SYMBOL(soc_pcmcia_debug);

#endif

#define to_soc_pcmcia_socket(x)	\
	container_of(x, struct soc_pcmcia_socket, socket)

int soc_pcmcia_regulator_set(struct soc_pcmcia_socket *skt,
	struct soc_pcmcia_regulator *r, int v)
{
	bool on;
	int ret;

	if (!r->reg)
		return 0;

	on = v != 0;
	if (r->on == on)
		return 0;

	if (on) {
		ret = regulator_set_voltage(r->reg, v * 100000, v * 100000);
		if (ret) {
			int vout = regulator_get_voltage(r->reg) / 100000;

			dev_warn(&skt->socket.dev,
				 "CS requested %s=%u.%uV, applying %u.%uV\n",
				 r == &skt->vcc ? "Vcc" : "Vpp",
				 v / 10, v % 10, vout / 10, vout % 10);
		}

		ret = regulator_enable(r->reg);
	} else {
		ret = regulator_disable(r->reg);
	}
	if (ret == 0)
		r->on = on;

	return ret;
}
EXPORT_SYMBOL_GPL(soc_pcmcia_regulator_set);

static unsigned short
calc_speed(unsigned short *spds, int num, unsigned short dflt)
{
	unsigned short speed = 0;
	int i;

	for (i = 0; i < num; i++)
		if (speed < spds[i])
			speed = spds[i];
	if (speed == 0)
		speed = dflt;

	return speed;
}

void soc_common_pcmcia_get_timing(struct soc_pcmcia_socket *skt,
	struct soc_pcmcia_timing *timing)
{
	timing->io =
		calc_speed(skt->spd_io, MAX_IO_WIN, SOC_PCMCIA_IO_ACCESS);
	timing->mem =
		calc_speed(skt->spd_mem, MAX_WIN, SOC_PCMCIA_3V_MEM_ACCESS);
	timing->attr =
		calc_speed(skt->spd_attr, MAX_WIN, SOC_PCMCIA_3V_MEM_ACCESS);
}
EXPORT_SYMBOL(soc_common_pcmcia_get_timing);

static void __soc_pcmcia_hw_shutdown(struct soc_pcmcia_socket *skt,
	unsigned int nr)
{
	unsigned int i;

	for (i = 0; i < nr; i++)
		if (skt->stat[i].irq)
			free_irq(skt->stat[i].irq, skt);

	if (skt->ops->hw_shutdown)
		skt->ops->hw_shutdown(skt);

	clk_disable_unprepare(skt->clk);
}

static void soc_pcmcia_hw_shutdown(struct soc_pcmcia_socket *skt)
{
	__soc_pcmcia_hw_shutdown(skt, ARRAY_SIZE(skt->stat));
}

int soc_pcmcia_request_gpiods(struct soc_pcmcia_socket *skt)
{
	struct device *dev = skt->socket.dev.parent;
	struct gpio_desc *desc;
	int i;

	for (i = 0; i < ARRAY_SIZE(skt->stat); i++) {
		if (!skt->stat[i].name)
			continue;

		desc = devm_gpiod_get(dev, skt->stat[i].name, GPIOD_IN);
		if (IS_ERR(desc)) {
			dev_err(dev, "Failed to get GPIO for %s: %ld\n",
				skt->stat[i].name, PTR_ERR(desc));
			return PTR_ERR(desc);
		}

		skt->stat[i].desc = desc;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(soc_pcmcia_request_gpiods);

static int soc_pcmcia_hw_init(struct soc_pcmcia_socket *skt)
{
	int ret = 0, i;

	ret = clk_prepare_enable(skt->clk);
	if (ret)
		return ret;

	if (skt->ops->hw_init) {
		ret = skt->ops->hw_init(skt);
		if (ret) {
			clk_disable_unprepare(skt->clk);
			return ret;
		}
	}

	for (i = 0; i < ARRAY_SIZE(skt->stat); i++) {
		if (gpio_is_valid(skt->stat[i].gpio)) {
			ret = devm_gpio_request_one(skt->socket.dev.parent,
						    skt->stat[i].gpio, GPIOF_IN,
						    skt->stat[i].name);
			if (ret) {
				__soc_pcmcia_hw_shutdown(skt, i);
				return ret;
			}

			skt->stat[i].desc = gpio_to_desc(skt->stat[i].gpio);

			/* CD is active low by default */
			if ((i == SOC_STAT_CD) ^ gpiod_is_active_low(skt->stat[i].desc))
				gpiod_toggle_active_low(skt->stat[i].desc);
		}

		if (i < SOC_STAT_VS1 && skt->stat[i].desc) {
			int irq = gpiod_to_irq(skt->stat[i].desc);

			if (irq > 0) {
				if (i == SOC_STAT_RDY)
					skt->socket.pci_irq = irq;
				else
					skt->stat[i].irq = irq;
			}
		}

		if (skt->stat[i].irq) {
			ret = request_irq(skt->stat[i].irq,
					  soc_common_pcmcia_interrupt,
					  IRQF_TRIGGER_NONE,
					  skt->stat[i].name, skt);
			if (ret) {
				__soc_pcmcia_hw_shutdown(skt, i);
				return ret;
			}
		}
	}

	return ret;
}

static void soc_pcmcia_hw_enable(struct soc_pcmcia_socket *skt)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(skt->stat); i++)
		if (skt->stat[i].irq) {
			irq_set_irq_type(skt->stat[i].irq, IRQ_TYPE_EDGE_RISING);
			irq_set_irq_type(skt->stat[i].irq, IRQ_TYPE_EDGE_BOTH);
		}
}

static void soc_pcmcia_hw_disable(struct soc_pcmcia_socket *skt)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(skt->stat); i++)
		if (skt->stat[i].irq)
			irq_set_irq_type(skt->stat[i].irq, IRQ_TYPE_NONE);
}

/*
 * The CF 3.0 specification says that cards tie VS1 to ground and leave
 * VS2 open.  Many implementations do not wire up the VS signals, so we
 * provide hard-coded values as per the CF 3.0 spec.
 */
void soc_common_cf_socket_state(struct soc_pcmcia_socket *skt,
	struct pcmcia_state *state)
{
	state->vs_3v = 1;
}
EXPORT_SYMBOL_GPL(soc_common_cf_socket_state);

static unsigned int soc_common_pcmcia_skt_state(struct soc_pcmcia_socket *skt)
{
	struct pcmcia_state state;
	unsigned int stat;

	memset(&state, 0, sizeof(struct pcmcia_state));

	/* Make battery voltage state report 'good' */
	state.bvd1 = 1;
	state.bvd2 = 1;

	if (skt->stat[SOC_STAT_CD].desc)
		state.detect = !!gpiod_get_value(skt->stat[SOC_STAT_CD].desc);
	if (skt->stat[SOC_STAT_RDY].desc)
		state.ready = !!gpiod_get_value(skt->stat[SOC_STAT_RDY].desc);
	if (skt->stat[SOC_STAT_BVD1].desc)
		state.bvd1 = !!gpiod_get_value(skt->stat[SOC_STAT_BVD1].desc);
	if (skt->stat[SOC_STAT_BVD2].desc)
		state.bvd2 = !!gpiod_get_value(skt->stat[SOC_STAT_BVD2].desc);
	if (skt->stat[SOC_STAT_VS1].desc)
		state.vs_3v = !!gpiod_get_value(skt->stat[SOC_STAT_VS1].desc);
	if (skt->stat[SOC_STAT_VS2].desc)
		state.vs_Xv = !!gpiod_get_value(skt->stat[SOC_STAT_VS2].desc);

	skt->ops->socket_state(skt, &state);

	stat = state.detect  ? SS_DETECT : 0;
	stat |= state.ready  ? SS_READY  : 0;
	stat |= state.wrprot ? SS_WRPROT : 0;
	stat |= state.vs_3v  ? SS_3VCARD : 0;
	stat |= state.vs_Xv  ? SS_XVCARD : 0;

	/* The power status of individual sockets is not available
	 * explicitly from the hardware, so we just remember the state
	 * and regurgitate it upon request:
	 */
	stat |= skt->cs_state.Vcc ? SS_POWERON : 0;

	if (skt->cs_state.flags & SS_IOCARD)
		stat |= state.bvd1 ? 0 : SS_STSCHG;
	else {
		if (state.bvd1 == 0)
			stat |= SS_BATDEAD;
		else if (state.bvd2 == 0)
			stat |= SS_BATWARN;
	}
	return stat;
}

/*
 * soc_common_pcmcia_config_skt
 * ^^^^^^^^^^^^^^^^^^^^^^^^^^^^
 *
 * Convert PCMCIA socket state to our socket configure structure.
 */
static int soc_common_pcmcia_config_skt(
	struct soc_pcmcia_socket *skt, socket_state_t *state)
{
	int ret;

	ret = skt->ops->configure_socket(skt, state);
	if (ret < 0) {
		pr_err("soc_common_pcmcia: unable to configure socket %d\n",
		       skt->nr);
		/* restore the previous state */
		WARN_ON(skt->ops->configure_socket(skt, &skt->cs_state));
		return ret;
	}

	if (ret == 0) {
		struct gpio_desc *descs[2];
		DECLARE_BITMAP(values, 2);
		int n = 0;

		if (skt->gpio_reset) {
			descs[n] = skt->gpio_reset;
			__assign_bit(n++, values, state->flags & SS_RESET);
		}
		if (skt->gpio_bus_enable) {
			descs[n] = skt->gpio_bus_enable;
			__assign_bit(n++, values, state->flags & SS_OUTPUT_ENA);
		}

		if (n)
			gpiod_set_array_value_cansleep(n, descs, NULL, values);

		/*
		 * This really needs a better solution.  The IRQ
		 * may or may not be claimed by the driver.
		 */
		if (skt->irq_state != 1 && state->io_irq) {
			skt->irq_state = 1;
			irq_set_irq_type(skt->socket.pci_irq,
					 IRQ_TYPE_EDGE_FALLING);
		} else if (skt->irq_state == 1 && state->io_irq == 0) {
			skt->irq_state = 0;
			irq_set_irq_type(skt->socket.pci_irq, IRQ_TYPE_NONE);
		}

		skt->cs_state = *state;
	}

	return ret;
}

/* soc_common_pcmcia_sock_init()
 * ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
 *
 * (Re-)Initialise the socket, turning on status interrupts
 * and PCMCIA bus.  This must wait for power to stabilise
 * so that the card status signals report correctly.
 *
 * Returns: 0
 */
static int soc_common_pcmcia_sock_init(struct pcmcia_socket *sock)
{
	struct soc_pcmcia_socket *skt = to_soc_pcmcia_socket(sock);

	debug(skt, 2, "initializing socket\n");
	if (skt->ops->socket_init)
		skt->ops->socket_init(skt);
	soc_pcmcia_hw_enable(skt);
	return 0;
}


/*
 * soc_common_pcmcia_suspend()
 * ^^^^^^^^^^^^^^^^^^^^^^^^^^^
 *
 * Remove power on the socket, disable IRQs from the card.
 * Turn off status interrupts, and disable the PCMCIA bus.
 *
 * Returns: 0
 */
static int soc_common_pcmcia_suspend(struct pcmcia_socket *sock)
{
	struct soc_pcmcia_socket *skt = to_soc_pcmcia_socket(sock);

	debug(skt, 2, "suspending socket\n");

	soc_pcmcia_hw_disable(skt);
	if (skt->ops->socket_suspend)
		skt->ops->socket_suspend(skt);

	return 0;
}

static DEFINE_SPINLOCK(status_lock);

static void soc_common_check_status(struct soc_pcmcia_socket *skt)
{
	unsigned int events;

	debug(skt, 4, "entering PCMCIA monitoring thread\n");

	do {
		unsigned int status;
		unsigned long flags;

		status = soc_common_pcmcia_skt_state(skt);

		spin_lock_irqsave(&status_lock, flags);
		events = (status ^ skt->status) & skt->cs_state.csc_mask;
		skt->status = status;
		spin_unlock_irqrestore(&status_lock, flags);

		debug(skt, 4, "events: %s%s%s%s%s%s\n",
			events == 0         ? "<NONE>"   : "",
			events & SS_DETECT  ? "DETECT "  : "",
			events & SS_READY   ? "READY "   : "",
			events & SS_BATDEAD ? "BATDEAD " : "",
			events & SS_BATWARN ? "BATWARN " : "",
			events & SS_STSCHG  ? "STSCHG "  : "");

		if (events)
			pcmcia_parse_events(&skt->socket, events);
	} while (events);
}

/* Let's poll for events in addition to IRQs since IRQ only is unreliable... */
static void soc_common_pcmcia_poll_event(struct timer_list *t)
{
	struct soc_pcmcia_socket *skt = from_timer(skt, t, poll_timer);
	debug(skt, 4, "polling for events\n");

	mod_timer(&skt->poll_timer, jiffies + SOC_PCMCIA_POLL_PERIOD);

	soc_common_check_status(skt);
}


/*
 * Service routine for socket driver interrupts (requested by the
 * low-level PCMCIA init() operation via soc_common_pcmcia_thread()).
 * The actual interrupt-servicing work is performed by
 * soc_common_pcmcia_thread(), largely because the Card Services event-
 * handling code performs scheduling operations which cannot be
 * executed from within an interrupt context.
 */
static irqreturn_t soc_common_pcmcia_interrupt(int irq, void *dev)
{
	struct soc_pcmcia_socket *skt = dev;

	debug(skt, 3, "servicing IRQ %d\n", irq);

	soc_common_check_status(skt);

	return IRQ_HANDLED;
}


/*
 *  Implements the get_status() operation for the in-kernel PCMCIA
 * service (formerly SS_GetStatus in Card Services). Essentially just
 * fills in bits in `status' according to internal driver state or
 * the value of the voltage detect chipselect register.
 *
 * As a debugging note, during card startup, the PCMCIA core issues
 * three set_socket() commands in a row the first with RESET deasserted,
 * the second with RESET asserted, and the last with RESET deasserted
 * again. Following the third set_socket(), a get_status() command will
 * be issued. The kernel is looking for the SS_READY flag (see
 * setup_socket(), reset_socket(), and unreset_socket() in cs.c).
 *
 * Returns: 0
 */
static int
soc_common_pcmcia_get_status(struct pcmcia_socket *sock, unsigned int *status)
{
	struct soc_pcmcia_socket *skt = to_soc_pcmcia_socket(sock);

	skt->status = soc_common_pcmcia_skt_state(skt);
	*status = skt->status;

	return 0;
}


/*
 * Implements the set_socket() operation for the in-kernel PCMCIA
 * service (formerly SS_SetSocket in Card Services). We more or
 * less punt all of this work and let the kernel handle the details
 * of power configuration, reset, &c. We also record the value of
 * `state' in order to regurgitate it to the PCMCIA core later.
 */
static int soc_common_pcmcia_set_socket(
	struct pcmcia_socket *sock, socket_state_t *state)
{
	struct soc_pcmcia_socket *skt = to_soc_pcmcia_socket(sock);

	debug(skt, 2, "mask: %s%s%s%s%s%s flags: %s%s%s%s%s%s Vcc %d Vpp %d irq %d\n",
			(state->csc_mask == 0)		? "<NONE> " :	"",
			(state->csc_mask & SS_DETECT)	? "DETECT " :	"",
			(state->csc_mask & SS_READY)	? "READY " :	"",
			(state->csc_mask & SS_BATDEAD)	? "BATDEAD " :	"",
			(state->csc_mask & SS_BATWARN)	? "BATWARN " :	"",
			(state->csc_mask & SS_STSCHG)	? "STSCHG " :	"",
			(state->flags == 0)		? "<NONE> " :	"",
			(state->flags & SS_PWR_AUTO)	? "PWR_AUTO " :	"",
			(state->flags & SS_IOCARD)	? "IOCARD " :	"",
			(state->flags & SS_RESET)	? "RESET " :	"",
			(state->flags & SS_SPKR_ENA)	? "SPKR_ENA " :	"",
			(state->flags & SS_OUTPUT_ENA)	? "OUTPUT_ENA " : "",
			state->Vcc, state->Vpp, state->io_irq);

	return soc_common_pcmcia_config_skt(skt, state);
}


/*
 * Implements the set_io_map() operation for the in-kernel PCMCIA
 * service (formerly SS_SetIOMap in Card Services). We configure
 * the map speed as requested, but override the address ranges
 * supplied by Card Services.
 *
 * Returns: 0 on success, -1 on error
 */
static int soc_common_pcmcia_set_io_map(
	struct pcmcia_socket *sock, struct pccard_io_map *map)
{
	struct soc_pcmcia_socket *skt = to_soc_pcmcia_socket(sock);
	unsigned short speed = map->speed;

	debug(skt, 2, "map %u  speed %u start 0x%08llx stop 0x%08llx\n",
		map->map, map->speed, (unsigned long long)map->start,
		(unsigned long long)map->stop);
	debug(skt, 2, "flags: %s%s%s%s%s%s%s%s\n",
		(map->flags == 0)		? "<NONE>"	: "",
		(map->flags & MAP_ACTIVE)	? "ACTIVE "	: "",
		(map->flags & MAP_16BIT)	? "16BIT "	: "",
		(map->flags & MAP_AUTOSZ)	? "AUTOSZ "	: "",
		(map->flags & MAP_0WS)		? "0WS "	: "",
		(map->flags & MAP_WRPROT)	? "WRPROT "	: "",
		(map->flags & MAP_USE_WAIT)	? "USE_WAIT "	: "",
		(map->flags & MAP_PREFETCH)	? "PREFETCH "	: "");

	if (map->map >= MAX_IO_WIN) {
		printk(KERN_ERR "%s(): map (%d) out of range\n", __func__,
		       map->map);
		return -1;
	}

	if (map->flags & MAP_ACTIVE) {
		if (speed == 0)
			speed = SOC_PCMCIA_IO_ACCESS;
	} else {
		speed = 0;
	}

	skt->spd_io[map->map] = speed;
	skt->ops->set_timing(skt);

	if (map->stop == 1)
		map->stop = PAGE_SIZE-1;

	map->stop -= map->start;
	map->stop += skt->socket.io_offset;
	map->start = skt->socket.io_offset;

	return 0;
}


/*
 * Implements the set_mem_map() operation for the in-kernel PCMCIA
 * service (formerly SS_SetMemMap in Card Services). We configure
 * the map speed as requested, but override the address ranges
 * supplied by Card Services.
 *
 * Returns: 0 on success, -ERRNO on error
 */
static int soc_common_pcmcia_set_mem_map(
	struct pcmcia_socket *sock, struct pccard_mem_map *map)
{
	struct soc_pcmcia_socket *skt = to_soc_pcmcia_socket(sock);
	struct resource *res;
	unsigned short speed = map->speed;

	debug(skt, 2, "map %u speed %u card_start %08x\n",
		map->map, map->speed, map->card_start);
	debug(skt, 2, "flags: %s%s%s%s%s%s%s%s\n",
		(map->flags == 0)		? "<NONE>"	: "",
		(map->flags & MAP_ACTIVE)	? "ACTIVE "	: "",
		(map->flags & MAP_16BIT)	? "16BIT "	: "",
		(map->flags & MAP_AUTOSZ)	? "AUTOSZ "	: "",
		(map->flags & MAP_0WS)		? "0WS "	: "",
		(map->flags & MAP_WRPROT)	? "WRPROT "	: "",
		(map->flags & MAP_ATTRIB)	? "ATTRIB "	: "",
		(map->flags & MAP_USE_WAIT)	? "USE_WAIT "	: "");

	if (map->map >= MAX_WIN)
		return -EINVAL;

	if (map->flags & MAP_ACTIVE) {
		if (speed == 0)
			speed = 300;
	} else {
		speed = 0;
	}

	if (map->flags & MAP_ATTRIB) {
		res = &skt->res_attr;
		skt->spd_attr[map->map] = speed;
		skt->spd_mem[map->map] = 0;
	} else {
		res = &skt->res_mem;
		skt->spd_attr[map->map] = 0;
		skt->spd_mem[map->map] = speed;
	}

	skt->ops->set_timing(skt);

	map->static_start = res->start + map->card_start;

	return 0;
}

struct bittbl {
	unsigned int mask;
	const char *name;
};

static struct bittbl status_bits[] = {
	{ SS_WRPROT,		"SS_WRPROT"	},
	{ SS_BATDEAD,		"SS_BATDEAD"	},
	{ SS_BATWARN,		"SS_BATWARN"	},
	{ SS_READY,		"SS_READY"	},
	{ SS_DETECT,		"SS_DETECT"	},
	{ SS_POWERON,		"SS_POWERON"	},
	{ SS_STSCHG,		"SS_STSCHG"	},
	{ SS_3VCARD,		"SS_3VCARD"	},
	{ SS_XVCARD,		"SS_XVCARD"	},
};

static struct bittbl conf_bits[] = {
	{ SS_PWR_AUTO,		"SS_PWR_AUTO"	},
	{ SS_IOCARD,		"SS_IOCARD"	},
	{ SS_RESET,		"SS_RESET"	},
	{ SS_DMA_MODE,		"SS_DMA_MODE"	},
	{ SS_SPKR_ENA,		"SS_SPKR_ENA"	},
	{ SS_OUTPUT_ENA,	"SS_OUTPUT_ENA"	},
};

static void dump_bits(char **p, const char *prefix,
	unsigned int val, struct bittbl *bits, int sz)
{
	char *b = *p;
	int i;

	b += sprintf(b, "%-9s:", prefix);
	for (i = 0; i < sz; i++)
		if (val & bits[i].mask)
			b += sprintf(b, " %s", bits[i].name);
	*b++ = '\n';
	*p = b;
}

/*
 * Implements the /sys/class/pcmcia_socket/??/status file.
 *
 * Returns: the number of characters added to the buffer
 */
static ssize_t show_status(
	struct device *dev, struct device_attribute *attr, char *buf)
{
	struct soc_pcmcia_socket *skt =
		container_of(dev, struct soc_pcmcia_socket, socket.dev);
	char *p = buf;

	p += sprintf(p, "slot     : %d\n", skt->nr);

	dump_bits(&p, "status", skt->status,
		  status_bits, ARRAY_SIZE(status_bits));
	dump_bits(&p, "csc_mask", skt->cs_state.csc_mask,
		  status_bits, ARRAY_SIZE(status_bits));
	dump_bits(&p, "cs_flags", skt->cs_state.flags,
		  conf_bits, ARRAY_SIZE(conf_bits));

	p += sprintf(p, "Vcc      : %d\n", skt->cs_state.Vcc);
	p += sprintf(p, "Vpp      : %d\n", skt->cs_state.Vpp);
	p += sprintf(p, "IRQ      : %d (%d)\n", skt->cs_state.io_irq,
		skt->socket.pci_irq);
	if (skt->ops->show_timing)
		p += skt->ops->show_timing(skt, p);

	return p-buf;
}
static DEVICE_ATTR(status, S_IRUGO, show_status, NULL);


static struct pccard_operations soc_common_pcmcia_operations = {
	.init			= soc_common_pcmcia_sock_init,
	.suspend		= soc_common_pcmcia_suspend,
	.get_status		= soc_common_pcmcia_get_status,
	.set_socket		= soc_common_pcmcia_set_socket,
	.set_io_map		= soc_common_pcmcia_set_io_map,
	.set_mem_map		= soc_common_pcmcia_set_mem_map,
};


#ifdef CONFIG_CPU_FREQ
static int soc_common_pcmcia_cpufreq_nb(struct notifier_block *nb,
	unsigned long val, void *data)
{
	struct soc_pcmcia_socket *skt = container_of(nb, struct soc_pcmcia_socket, cpufreq_nb);
	struct cpufreq_freqs *freqs = data;

	return skt->ops->frequency_change(skt, val, freqs);
}
#endif

void soc_pcmcia_init_one(struct soc_pcmcia_socket *skt,
	const struct pcmcia_low_level *ops, struct device *dev)
{
	int i;

	skt->ops = ops;
	skt->socket.owner = ops->owner;
	skt->socket.dev.parent = dev;
	skt->socket.pci_irq = NO_IRQ;

	for (i = 0; i < ARRAY_SIZE(skt->stat); i++)
		skt->stat[i].gpio = -EINVAL;
}
EXPORT_SYMBOL(soc_pcmcia_init_one);

void soc_pcmcia_remove_one(struct soc_pcmcia_socket *skt)
{
	timer_delete_sync(&skt->poll_timer);

	pcmcia_unregister_socket(&skt->socket);

#ifdef CONFIG_CPU_FREQ
	if (skt->ops->frequency_change)
		cpufreq_unregister_notifier(&skt->cpufreq_nb,
					    CPUFREQ_TRANSITION_NOTIFIER);
#endif

	soc_pcmcia_hw_shutdown(skt);

	/* should not be required; violates some lowlevel drivers */
	soc_common_pcmcia_config_skt(skt, &dead_socket);

	iounmap(PCI_IOBASE + skt->res_io_io.start);
	release_resource(&skt->res_attr);
	release_resource(&skt->res_mem);
	release_resource(&skt->res_io);
	release_resource(&skt->res_skt);
}
EXPORT_SYMBOL(soc_pcmcia_remove_one);

int soc_pcmcia_add_one(struct soc_pcmcia_socket *skt)
{
	int ret;

	skt->cs_state = dead_socket;

	timer_setup(&skt->poll_timer, soc_common_pcmcia_poll_event, 0);
	skt->poll_timer.expires = jiffies + SOC_PCMCIA_POLL_PERIOD;

	ret = request_resource(&iomem_resource, &skt->res_skt);
	if (ret)
		goto out_err_1;

	ret = request_resource(&skt->res_skt, &skt->res_io);
	if (ret)
		goto out_err_2;

	ret = request_resource(&skt->res_skt, &skt->res_mem);
	if (ret)
		goto out_err_3;

	ret = request_resource(&skt->res_skt, &skt->res_attr);
	if (ret)
		goto out_err_4;

	skt->res_io_io = (struct resource)
		 DEFINE_RES_IO_NAMED(skt->nr * 0x1000 + 0x10000, 0x1000,
				     "PCMCIA I/O");
	ret = pci_remap_iospace(&skt->res_io_io, skt->res_io.start);
	if (ret)
		goto out_err_5;

	/*
	 * We initialize default socket timing here, because
	 * we are not guaranteed to see a SetIOMap operation at
	 * runtime.
	 */
	skt->ops->set_timing(skt);

	ret = soc_pcmcia_hw_init(skt);
	if (ret)
		goto out_err_6;

	skt->socket.ops = &soc_common_pcmcia_operations;
	skt->socket.features = SS_CAP_STATIC_MAP|SS_CAP_PCCARD;
	skt->socket.resource_ops = &pccard_static_ops;
	skt->socket.irq_mask = 0;
	skt->socket.map_size = PAGE_SIZE;
	skt->socket.io_offset = (unsigned long)skt->res_io_io.start;

	skt->status = soc_common_pcmcia_skt_state(skt);

#ifdef CONFIG_CPU_FREQ
	if (skt->ops->frequency_change) {
		skt->cpufreq_nb.notifier_call = soc_common_pcmcia_cpufreq_nb;

		ret = cpufreq_register_notifier(&skt->cpufreq_nb,
						CPUFREQ_TRANSITION_NOTIFIER);
		if (ret < 0)
			dev_err(skt->socket.dev.parent,
				"unable to register CPU frequency change notifier for PCMCIA (%d)\n",
				ret);
	}
#endif

	ret = pcmcia_register_socket(&skt->socket);
	if (ret)
		goto out_err_7;

	ret = device_create_file(&skt->socket.dev, &dev_attr_status);
	if (ret)
		goto out_err_8;

	return ret;

 out_err_8:
	timer_delete_sync(&skt->poll_timer);
	pcmcia_unregister_socket(&skt->socket);

 out_err_7:
	soc_pcmcia_hw_shutdown(skt);
 out_err_6:
	iounmap(PCI_IOBASE + skt->res_io_io.start);
 out_err_5:
	release_resource(&skt->res_attr);
 out_err_4:
	release_resource(&skt->res_mem);
 out_err_3:
	release_resource(&skt->res_io);
 out_err_2:
	release_resource(&skt->res_skt);
 out_err_1:

	return ret;
}
EXPORT_SYMBOL(soc_pcmcia_add_one);

MODULE_AUTHOR("John Dorsey <john+@cs.cmu.edu>");
MODULE_DESCRIPTION("Linux PCMCIA Card Services: Common SoC support");
MODULE_LICENSE("Dual MPL/GPL");

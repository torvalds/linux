/* SPDX-License-Identifier: GPL-2.0 */
/*
 * linux/drivers/pcmcia/soc_common.h
 *
 * Copyright (C) 2000 John G Dorsey <john+@cs.cmu.edu>
 *
 * This file contains definitions for the PCMCIA support code common to
 * integrated SOCs like the SA-11x0 and PXA2xx microprocessors.
 */
#ifndef _ASM_ARCH_PCMCIA
#define _ASM_ARCH_PCMCIA

/* include the world */
#include <linux/clk.h>
#include <linux/cpufreq.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/soc_common.h>

struct device;
struct gpio_desc;
struct pcmcia_low_level;
struct regulator;

struct skt_dev_info {
	int nskt;
	struct soc_pcmcia_socket skt[];
};

struct soc_pcmcia_timing {
	unsigned short io;
	unsigned short mem;
	unsigned short attr;
};

extern void soc_common_pcmcia_get_timing(struct soc_pcmcia_socket *, struct soc_pcmcia_timing *);

void soc_pcmcia_init_one(struct soc_pcmcia_socket *skt,
	const struct pcmcia_low_level *ops, struct device *dev);
void soc_pcmcia_remove_one(struct soc_pcmcia_socket *skt);
int soc_pcmcia_add_one(struct soc_pcmcia_socket *skt);
int soc_pcmcia_request_gpiods(struct soc_pcmcia_socket *skt);

void soc_common_cf_socket_state(struct soc_pcmcia_socket *skt,
	struct pcmcia_state *state);

int soc_pcmcia_regulator_set(struct soc_pcmcia_socket *skt,
	struct soc_pcmcia_regulator *r, int v);

#ifdef CONFIG_PCMCIA_DEBUG

extern void soc_pcmcia_debug(struct soc_pcmcia_socket *skt, const char *func,
			     int lvl, const char *fmt, ...);

#define debug(skt, lvl, fmt, arg...) \
	soc_pcmcia_debug(skt, __func__, lvl, fmt , ## arg)

#else
#define debug(skt, lvl, fmt, arg...) do { } while (0)
#endif


/*
 * The PC Card Standard, Release 7, section 4.13.4, says that twIORD
 * has a minimum value of 165ns. Section 4.13.5 says that twIOWR has
 * a minimum value of 165ns, as well. Section 4.7.2 (describing
 * common and attribute memory write timing) says that twWE has a
 * minimum value of 150ns for a 250ns cycle time (for 5V operation;
 * see section 4.7.4), or 300ns for a 600ns cycle time (for 3.3V
 * operation, also section 4.7.4). Section 4.7.3 says that taOE
 * has a maximum value of 150ns for a 300ns cycle time (for 5V
 * operation), or 300ns for a 600ns cycle time (for 3.3V operation).
 *
 * When configuring memory maps, Card Services appears to adopt the policy
 * that a memory access time of "0" means "use the default." The default
 * PCMCIA I/O command width time is 165ns. The default PCMCIA 5V attribute
 * and memory command width time is 150ns; the PCMCIA 3.3V attribute and
 * memory command width time is 300ns.
 */
#define SOC_PCMCIA_IO_ACCESS		(165)
#define SOC_PCMCIA_5V_MEM_ACCESS	(150)
#define SOC_PCMCIA_3V_MEM_ACCESS	(300)
#define SOC_PCMCIA_ATTR_MEM_ACCESS	(300)

/*
 * The socket driver actually works nicely in interrupt-driven form,
 * so the (relatively infrequent) polling is "just to be sure."
 */
#define SOC_PCMCIA_POLL_PERIOD    (2*HZ)


/* I/O pins replacing memory pins
 * (PCMCIA System Architecture, 2nd ed., by Don Anderson, p.75)
 *
 * These signals change meaning when going from memory-only to
 * memory-or-I/O interface:
 */
#define iostschg bvd1
#define iospkr   bvd2

#endif

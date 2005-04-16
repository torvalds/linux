/*======================================================================

    Device driver for the PCMCIA control functionality of StrongARM
    SA-1100 microprocessors.

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

#include <linux/module.h>
#include <linux/init.h>
#include <linux/config.h>
#include <linux/cpufreq.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>

#include "soc_common.h"
#include "sa11xx_base.h"


/*
 * sa1100_pcmcia_default_mecr_timing
 * ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
 *
 * Calculate MECR clock wait states for given CPU clock
 * speed and command wait state. This function can be over-
 * written by a board specific version.
 *
 * The default is to simply calculate the BS values as specified in
 * the INTEL SA1100 development manual
 * "Expansion Memory (PCMCIA) Configuration Register (MECR)"
 * that's section 10.2.5 in _my_ version of the manual ;)
 */
static unsigned int
sa1100_pcmcia_default_mecr_timing(struct soc_pcmcia_socket *skt,
				  unsigned int cpu_speed,
				  unsigned int cmd_time)
{
	return sa1100_pcmcia_mecr_bs(cmd_time, cpu_speed);
}

/* sa1100_pcmcia_set_mecr()
 * ^^^^^^^^^^^^^^^^^^^^^^^^
 *
 * set MECR value for socket <sock> based on this sockets
 * io, mem and attribute space access speed.
 * Call board specific BS value calculation to allow boards
 * to tweak the BS values.
 */
static int
sa1100_pcmcia_set_mecr(struct soc_pcmcia_socket *skt, unsigned int cpu_clock)
{
	struct soc_pcmcia_timing timing;
	u32 mecr, old_mecr;
	unsigned long flags;
	unsigned int bs_io, bs_mem, bs_attr;

	soc_common_pcmcia_get_timing(skt, &timing);

	bs_io = skt->ops->get_timing(skt, cpu_clock, timing.io);
	bs_mem = skt->ops->get_timing(skt, cpu_clock, timing.mem);
	bs_attr = skt->ops->get_timing(skt, cpu_clock, timing.attr);

	local_irq_save(flags);

	old_mecr = mecr = MECR;
	MECR_FAST_SET(mecr, skt->nr, 0);
	MECR_BSIO_SET(mecr, skt->nr, bs_io);
	MECR_BSA_SET(mecr, skt->nr, bs_attr);
	MECR_BSM_SET(mecr, skt->nr, bs_mem);
	if (old_mecr != mecr)
		MECR = mecr;

	local_irq_restore(flags);

	debug(skt, 2, "FAST %X  BSM %X  BSA %X  BSIO %X\n",
	      MECR_FAST_GET(mecr, skt->nr),
	      MECR_BSM_GET(mecr, skt->nr), MECR_BSA_GET(mecr, skt->nr),
	      MECR_BSIO_GET(mecr, skt->nr));

	return 0;
}

#ifdef CONFIG_CPU_FREQ
static int
sa1100_pcmcia_frequency_change(struct soc_pcmcia_socket *skt,
			       unsigned long val,
			       struct cpufreq_freqs *freqs)
{
	switch (val) {
	case CPUFREQ_PRECHANGE:
		if (freqs->new > freqs->old)
			sa1100_pcmcia_set_mecr(skt, freqs->new);
		break;

	case CPUFREQ_POSTCHANGE:
		if (freqs->new < freqs->old)
			sa1100_pcmcia_set_mecr(skt, freqs->new);
		break;
	case CPUFREQ_RESUMECHANGE:
		sa1100_pcmcia_set_mecr(skt, freqs->new);
		break;
	}

	return 0;
}

#endif

static int
sa1100_pcmcia_set_timing(struct soc_pcmcia_socket *skt)
{
	return sa1100_pcmcia_set_mecr(skt, cpufreq_get(0));
}

static int
sa1100_pcmcia_show_timing(struct soc_pcmcia_socket *skt, char *buf)
{
	struct soc_pcmcia_timing timing;
	unsigned int clock = cpufreq_get(0);
	unsigned long mecr = MECR;
	char *p = buf;

	soc_common_pcmcia_get_timing(skt, &timing);

	p+=sprintf(p, "I/O      : %u (%u)\n", timing.io,
		   sa1100_pcmcia_cmd_time(clock, MECR_BSIO_GET(mecr, skt->nr)));

	p+=sprintf(p, "attribute: %u (%u)\n", timing.attr,
		   sa1100_pcmcia_cmd_time(clock, MECR_BSA_GET(mecr, skt->nr)));

	p+=sprintf(p, "common   : %u (%u)\n", timing.mem,
		   sa1100_pcmcia_cmd_time(clock, MECR_BSM_GET(mecr, skt->nr)));

	return p - buf;
}

int sa11xx_drv_pcmcia_probe(struct device *dev, struct pcmcia_low_level *ops,
			    int first, int nr)
{
	/*
	 * set default MECR calculation if the board specific
	 * code did not specify one...
	 */
	if (!ops->get_timing)
		ops->get_timing = sa1100_pcmcia_default_mecr_timing;

	/* Provide our SA11x0 specific timing routines. */
	ops->set_timing  = sa1100_pcmcia_set_timing;
	ops->show_timing = sa1100_pcmcia_show_timing;
#ifdef CONFIG_CPU_FREQ
	ops->frequency_change = sa1100_pcmcia_frequency_change;
#endif

	return soc_common_drv_pcmcia_probe(dev, ops, first, nr);
}
EXPORT_SYMBOL(sa11xx_drv_pcmcia_probe);

static int __init sa11xx_pcmcia_init(void)
{
	return 0;
}
module_init(sa11xx_pcmcia_init);

static void __exit sa11xx_pcmcia_exit(void) {}

module_exit(sa11xx_pcmcia_exit);

MODULE_AUTHOR("John Dorsey <john+@cs.cmu.edu>");
MODULE_DESCRIPTION("Linux PCMCIA Card Services: SA-11xx core socket driver");
MODULE_LICENSE("Dual MPL/GPL");

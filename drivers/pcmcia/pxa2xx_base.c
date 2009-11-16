/*======================================================================

  Device driver for the PCMCIA control functionality of PXA2xx
  microprocessors.

    The contents of this file may be used under the
    terms of the GNU Public License version 2 (the "GPL")

    (c) Ian Molton (spyro@f2s.com) 2003
    (c) Stefan Eletzhofer (stefan.eletzhofer@inquant.de) 2003,4

    derived from sa11xx_base.c

     Portions created by John G. Dorsey are
     Copyright (C) 1999 John G. Dorsey.

  ======================================================================*/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>

#include <mach/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <mach/pxa2xx-regs.h>
#include <asm/mach-types.h>

#include <pcmcia/cs_types.h>
#include <pcmcia/ss.h>
#include <pcmcia/cistpl.h>

#include "soc_common.h"
#include "pxa2xx_base.h"

/*
 * Personal Computer Memory Card International Association (PCMCIA) sockets
 */

#define PCMCIAPrtSp	0x04000000	/* PCMCIA Partition Space [byte]   */
#define PCMCIASp	(4*PCMCIAPrtSp)	/* PCMCIA Space [byte]             */
#define PCMCIAIOSp	PCMCIAPrtSp	/* PCMCIA I/O Space [byte]         */
#define PCMCIAAttrSp	PCMCIAPrtSp	/* PCMCIA Attribute Space [byte]   */
#define PCMCIAMemSp	PCMCIAPrtSp	/* PCMCIA Memory Space [byte]      */

#define PCMCIA0Sp	PCMCIASp	/* PCMCIA 0 Space [byte]           */
#define PCMCIA0IOSp	PCMCIAIOSp	/* PCMCIA 0 I/O Space [byte]       */
#define PCMCIA0AttrSp	PCMCIAAttrSp	/* PCMCIA 0 Attribute Space [byte] */
#define PCMCIA0MemSp	PCMCIAMemSp	/* PCMCIA 0 Memory Space [byte]    */

#define PCMCIA1Sp	PCMCIASp	/* PCMCIA 1 Space [byte]           */
#define PCMCIA1IOSp	PCMCIAIOSp	/* PCMCIA 1 I/O Space [byte]       */
#define PCMCIA1AttrSp	PCMCIAAttrSp	/* PCMCIA 1 Attribute Space [byte] */
#define PCMCIA1MemSp	PCMCIAMemSp	/* PCMCIA 1 Memory Space [byte]    */

#define _PCMCIA(Nb)			/* PCMCIA [0..1]                   */ \
			(0x20000000 + (Nb) * PCMCIASp)
#define _PCMCIAIO(Nb)	_PCMCIA(Nb)	/* PCMCIA I/O [0..1]               */
#define _PCMCIAAttr(Nb)			/* PCMCIA Attribute [0..1]         */ \
			(_PCMCIA(Nb) + 2 * PCMCIAPrtSp)
#define _PCMCIAMem(Nb)			/* PCMCIA Memory [0..1]            */ \
			(_PCMCIA(Nb) + 3 * PCMCIAPrtSp)

#define _PCMCIA0	_PCMCIA(0)	/* PCMCIA 0                        */
#define _PCMCIA0IO	_PCMCIAIO(0)	/* PCMCIA 0 I/O                    */
#define _PCMCIA0Attr	_PCMCIAAttr(0)	/* PCMCIA 0 Attribute              */
#define _PCMCIA0Mem	_PCMCIAMem(0)	/* PCMCIA 0 Memory                 */

#define _PCMCIA1	_PCMCIA(1)	/* PCMCIA 1                        */
#define _PCMCIA1IO	_PCMCIAIO(1)	/* PCMCIA 1 I/O                    */
#define _PCMCIA1Attr	_PCMCIAAttr(1)	/* PCMCIA 1 Attribute              */
#define _PCMCIA1Mem	_PCMCIAMem(1)	/* PCMCIA 1 Memory                 */


#define MCXX_SETUP_MASK     (0x7f)
#define MCXX_ASST_MASK      (0x1f)
#define MCXX_HOLD_MASK      (0x3f)
#define MCXX_SETUP_SHIFT    (0)
#define MCXX_ASST_SHIFT     (7)
#define MCXX_HOLD_SHIFT     (14)

static inline u_int pxa2xx_mcxx_hold(u_int pcmcia_cycle_ns,
				     u_int mem_clk_10khz)
{
	u_int code = pcmcia_cycle_ns * mem_clk_10khz;
	return (code / 300000) + ((code % 300000) ? 1 : 0) - 1;
}

static inline u_int pxa2xx_mcxx_asst(u_int pcmcia_cycle_ns,
				     u_int mem_clk_10khz)
{
	u_int code = pcmcia_cycle_ns * mem_clk_10khz;
	return (code / 300000) + ((code % 300000) ? 1 : 0) + 1;
}

static inline u_int pxa2xx_mcxx_setup(u_int pcmcia_cycle_ns,
				      u_int mem_clk_10khz)
{
	u_int code = pcmcia_cycle_ns * mem_clk_10khz;
	return (code / 100000) + ((code % 100000) ? 1 : 0) - 1;
}

/* This function returns the (approximate) command assertion period, in
 * nanoseconds, for a given CPU clock frequency and MCXX_ASST value:
 */
static inline u_int pxa2xx_pcmcia_cmd_time(u_int mem_clk_10khz,
					   u_int pcmcia_mcxx_asst)
{
	return (300000 * (pcmcia_mcxx_asst + 1) / mem_clk_10khz);
}

static int pxa2xx_pcmcia_set_mcmem( int sock, int speed, int clock )
{
	MCMEM(sock) = ((pxa2xx_mcxx_setup(speed, clock)
		& MCXX_SETUP_MASK) << MCXX_SETUP_SHIFT)
		| ((pxa2xx_mcxx_asst(speed, clock)
		& MCXX_ASST_MASK) << MCXX_ASST_SHIFT)
		| ((pxa2xx_mcxx_hold(speed, clock)
		& MCXX_HOLD_MASK) << MCXX_HOLD_SHIFT);

	return 0;
}

static int pxa2xx_pcmcia_set_mcio( int sock, int speed, int clock )
{
	MCIO(sock) = ((pxa2xx_mcxx_setup(speed, clock)
		& MCXX_SETUP_MASK) << MCXX_SETUP_SHIFT)
		| ((pxa2xx_mcxx_asst(speed, clock)
		& MCXX_ASST_MASK) << MCXX_ASST_SHIFT)
		| ((pxa2xx_mcxx_hold(speed, clock)
		& MCXX_HOLD_MASK) << MCXX_HOLD_SHIFT);

	return 0;
}

static int pxa2xx_pcmcia_set_mcatt( int sock, int speed, int clock )
{
	MCATT(sock) = ((pxa2xx_mcxx_setup(speed, clock)
		& MCXX_SETUP_MASK) << MCXX_SETUP_SHIFT)
		| ((pxa2xx_mcxx_asst(speed, clock)
		& MCXX_ASST_MASK) << MCXX_ASST_SHIFT)
		| ((pxa2xx_mcxx_hold(speed, clock)
		& MCXX_HOLD_MASK) << MCXX_HOLD_SHIFT);

	return 0;
}

static int pxa2xx_pcmcia_set_mcxx(struct soc_pcmcia_socket *skt, unsigned int clk)
{
	struct soc_pcmcia_timing timing;
	int sock = skt->nr;

	soc_common_pcmcia_get_timing(skt, &timing);

	pxa2xx_pcmcia_set_mcmem(sock, timing.mem, clk);
	pxa2xx_pcmcia_set_mcatt(sock, timing.attr, clk);
	pxa2xx_pcmcia_set_mcio(sock, timing.io, clk);

	return 0;
}

static int pxa2xx_pcmcia_set_timing(struct soc_pcmcia_socket *skt)
{
	unsigned int clk = get_memclk_frequency_10khz();
	return pxa2xx_pcmcia_set_mcxx(skt, clk);
}

#ifdef CONFIG_CPU_FREQ

static int
pxa2xx_pcmcia_frequency_change(struct soc_pcmcia_socket *skt,
			       unsigned long val,
			       struct cpufreq_freqs *freqs)
{
#warning "it's not clear if this is right since the core CPU (N) clock has no effect on the memory (L) clock"
	switch (val) {
	case CPUFREQ_PRECHANGE:
		if (freqs->new > freqs->old) {
			debug(skt, 2, "new frequency %u.%uMHz > %u.%uMHz, "
			       "pre-updating\n",
			       freqs->new / 1000, (freqs->new / 100) % 10,
			       freqs->old / 1000, (freqs->old / 100) % 10);
			pxa2xx_pcmcia_set_mcxx(skt, freqs->new);
		}
		break;

	case CPUFREQ_POSTCHANGE:
		if (freqs->new < freqs->old) {
			debug(skt, 2, "new frequency %u.%uMHz < %u.%uMHz, "
			       "post-updating\n",
			       freqs->new / 1000, (freqs->new / 100) % 10,
			       freqs->old / 1000, (freqs->old / 100) % 10);
			pxa2xx_pcmcia_set_mcxx(skt, freqs->new);
		}
		break;
	}
	return 0;
}
#endif

static void pxa2xx_configure_sockets(struct device *dev)
{
	struct pcmcia_low_level *ops = dev->platform_data;

	/*
	 * We have at least one socket, so set MECR:CIT
	 * (Card Is There)
	 */
	MECR |= MECR_CIT;

	/* Set MECR:NOS (Number Of Sockets) */
	if ((ops->first + ops->nr) > 1 || machine_is_viper())
		MECR |= MECR_NOS;
	else
		MECR &= ~MECR_NOS;
}

static const char *skt_names[] = {
	"PCMCIA socket 0",
	"PCMCIA socket 1",
};

#define SKT_DEV_INFO_SIZE(n) \
	(sizeof(struct skt_dev_info) + (n)*sizeof(struct soc_pcmcia_socket))

int __pxa2xx_drv_pcmcia_probe(struct device *dev)
{
	int i, ret;
	struct pcmcia_low_level *ops;
	struct skt_dev_info *sinfo;
	struct soc_pcmcia_socket *skt;

	if (!dev || !dev->platform_data)
		return -ENODEV;

	ops = (struct pcmcia_low_level *)dev->platform_data;

	sinfo = kzalloc(SKT_DEV_INFO_SIZE(ops->nr), GFP_KERNEL);
	if (!sinfo)
		return -ENOMEM;

	sinfo->nskt = ops->nr;

	/* Initialize processor specific parameters */
	for (i = 0; i < ops->nr; i++) {
		skt = &sinfo->skt[i];

		skt->nr		= ops->first + i;
		skt->irq	= NO_IRQ;

		skt->res_skt.start	= _PCMCIA(skt->nr);
		skt->res_skt.end	= _PCMCIA(skt->nr) + PCMCIASp - 1;
		skt->res_skt.name	= skt_names[skt->nr];
		skt->res_skt.flags	= IORESOURCE_MEM;

		skt->res_io.start	= _PCMCIAIO(skt->nr);
		skt->res_io.end		= _PCMCIAIO(skt->nr) + PCMCIAIOSp - 1;
		skt->res_io.name	= "io";
		skt->res_io.flags	= IORESOURCE_MEM | IORESOURCE_BUSY;

		skt->res_mem.start	= _PCMCIAMem(skt->nr);
		skt->res_mem.end	= _PCMCIAMem(skt->nr) + PCMCIAMemSp - 1;
		skt->res_mem.name	= "memory";
		skt->res_mem.flags	= IORESOURCE_MEM;

		skt->res_attr.start	= _PCMCIAAttr(skt->nr);
		skt->res_attr.end	= _PCMCIAAttr(skt->nr) + PCMCIAAttrSp - 1;
		skt->res_attr.name	= "attribute";
		skt->res_attr.flags	= IORESOURCE_MEM;
	}

	/* Provide our PXA2xx specific timing routines. */
	ops->set_timing  = pxa2xx_pcmcia_set_timing;
#ifdef CONFIG_CPU_FREQ
	ops->frequency_change = pxa2xx_pcmcia_frequency_change;
#endif

	ret = soc_common_drv_pcmcia_probe(dev, ops, sinfo);

	if (!ret)
		pxa2xx_configure_sockets(dev);

	return ret;
}
EXPORT_SYMBOL(__pxa2xx_drv_pcmcia_probe);


static int pxa2xx_drv_pcmcia_probe(struct platform_device *dev)
{
	return __pxa2xx_drv_pcmcia_probe(&dev->dev);
}

static int pxa2xx_drv_pcmcia_remove(struct platform_device *dev)
{
	return soc_common_drv_pcmcia_remove(&dev->dev);
}

static int pxa2xx_drv_pcmcia_suspend(struct device *dev)
{
	return pcmcia_socket_dev_suspend(dev);
}

static int pxa2xx_drv_pcmcia_resume(struct device *dev)
{
	pxa2xx_configure_sockets(dev);
	return pcmcia_socket_dev_resume(dev);
}

static struct dev_pm_ops  pxa2xx_drv_pcmcia_pm_ops = {
	.suspend	= pxa2xx_drv_pcmcia_suspend,
	.resume		= pxa2xx_drv_pcmcia_resume,
};

static struct platform_driver pxa2xx_pcmcia_driver = {
	.probe		= pxa2xx_drv_pcmcia_probe,
	.remove		= pxa2xx_drv_pcmcia_remove,
	.driver		= {
		.name	= "pxa2xx-pcmcia",
		.owner	= THIS_MODULE,
		.pm	= &pxa2xx_drv_pcmcia_pm_ops,
	},
};

static int __init pxa2xx_pcmcia_init(void)
{
	return platform_driver_register(&pxa2xx_pcmcia_driver);
}

static void __exit pxa2xx_pcmcia_exit(void)
{
	platform_driver_unregister(&pxa2xx_pcmcia_driver);
}

fs_initcall(pxa2xx_pcmcia_init);
module_exit(pxa2xx_pcmcia_exit);

MODULE_AUTHOR("Stefan Eletzhofer <stefan.eletzhofer@inquant.de> and Ian Molton <spyro@f2s.com>");
MODULE_DESCRIPTION("Linux PCMCIA Card Services: PXA2xx core socket driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:pxa2xx-pcmcia");

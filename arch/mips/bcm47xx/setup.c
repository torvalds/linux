/*
 *  Copyright (C) 2004 Florian Schirmer <jolt@tuxbox.org>
 *  Copyright (C) 2006 Felix Fietkau <nbd@openwrt.org>
 *  Copyright (C) 2006 Michael Buesch <m@bues.ch>
 *  Copyright (C) 2010 Waldemar Brodkorb <wbx@openadk.org>
 *  Copyright (C) 2010-2012 Hauke Mehrtens <hauke@hauke-m.de>
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

#include <linux/export.h>
#include <linux/types.h>
#include <linux/ssb/ssb.h>
#include <linux/ssb/ssb_embedded.h>
#include <linux/bcma/bcma_soc.h>
#include <asm/bootinfo.h>
#include <asm/reboot.h>
#include <asm/time.h>
#include <bcm47xx.h>
#include <bcm47xx_nvram.h>

union bcm47xx_bus bcm47xx_bus;
EXPORT_SYMBOL(bcm47xx_bus);

enum bcm47xx_bus_type bcm47xx_bus_type;
EXPORT_SYMBOL(bcm47xx_bus_type);

static void bcm47xx_machine_restart(char *command)
{
	printk(KERN_ALERT "Please stand by while rebooting the system...\n");
	local_irq_disable();
	/* Set the watchdog timer to reset immediately */
	switch (bcm47xx_bus_type) {
#ifdef CONFIG_BCM47XX_SSB
	case BCM47XX_BUS_TYPE_SSB:
		ssb_watchdog_timer_set(&bcm47xx_bus.ssb, 1);
		break;
#endif
#ifdef CONFIG_BCM47XX_BCMA
	case BCM47XX_BUS_TYPE_BCMA:
		bcma_chipco_watchdog_timer_set(&bcm47xx_bus.bcma.bus.drv_cc, 1);
		break;
#endif
	}
	while (1)
		cpu_relax();
}

static void bcm47xx_machine_halt(void)
{
	/* Disable interrupts and watchdog and spin forever */
	local_irq_disable();
	switch (bcm47xx_bus_type) {
#ifdef CONFIG_BCM47XX_SSB
	case BCM47XX_BUS_TYPE_SSB:
		ssb_watchdog_timer_set(&bcm47xx_bus.ssb, 0);
		break;
#endif
#ifdef CONFIG_BCM47XX_BCMA
	case BCM47XX_BUS_TYPE_BCMA:
		bcma_chipco_watchdog_timer_set(&bcm47xx_bus.bcma.bus.drv_cc, 0);
		break;
#endif
	}
	while (1)
		cpu_relax();
}

#ifdef CONFIG_BCM47XX_SSB
static int bcm47xx_get_sprom_ssb(struct ssb_bus *bus, struct ssb_sprom *out)
{
	char prefix[10];

	if (bus->bustype == SSB_BUSTYPE_PCI) {
		memset(out, 0, sizeof(struct ssb_sprom));
		snprintf(prefix, sizeof(prefix), "pci/%u/%u/",
			 bus->host_pci->bus->number + 1,
			 PCI_SLOT(bus->host_pci->devfn));
		bcm47xx_fill_sprom(out, prefix, false);
		return 0;
	} else {
		printk(KERN_WARNING "bcm47xx: unable to fill SPROM for given bustype.\n");
		return -EINVAL;
	}
}

static int bcm47xx_get_invariants(struct ssb_bus *bus,
				  struct ssb_init_invariants *iv)
{
	char buf[20];

	/* Fill boardinfo structure */
	memset(&(iv->boardinfo), 0 , sizeof(struct ssb_boardinfo));

	bcm47xx_fill_ssb_boardinfo(&iv->boardinfo, NULL);

	memset(&iv->sprom, 0, sizeof(struct ssb_sprom));
	bcm47xx_fill_sprom(&iv->sprom, NULL, false);

	if (bcm47xx_nvram_getenv("cardbus", buf, sizeof(buf)) >= 0)
		iv->has_cardbus_slot = !!simple_strtoul(buf, NULL, 10);

	return 0;
}

static void __init bcm47xx_register_ssb(void)
{
	int err;
	char buf[100];
	struct ssb_mipscore *mcore;

	err = ssb_arch_register_fallback_sprom(&bcm47xx_get_sprom_ssb);
	if (err)
		printk(KERN_WARNING "bcm47xx: someone else already registered"
			" a ssb SPROM callback handler (err %d)\n", err);

	err = ssb_bus_ssbbus_register(&(bcm47xx_bus.ssb), SSB_ENUM_BASE,
				      bcm47xx_get_invariants);
	if (err)
		panic("Failed to initialize SSB bus (err %d)", err);

	mcore = &bcm47xx_bus.ssb.mipscore;
	if (bcm47xx_nvram_getenv("kernel_args", buf, sizeof(buf)) >= 0) {
		if (strstr(buf, "console=ttyS1")) {
			struct ssb_serial_port port;

			printk(KERN_DEBUG "Swapping serial ports!\n");
			/* swap serial ports */
			memcpy(&port, &mcore->serial_ports[0], sizeof(port));
			memcpy(&mcore->serial_ports[0], &mcore->serial_ports[1],
			       sizeof(port));
			memcpy(&mcore->serial_ports[1], &port, sizeof(port));
		}
	}
}
#endif

#ifdef CONFIG_BCM47XX_BCMA
static int bcm47xx_get_sprom_bcma(struct bcma_bus *bus, struct ssb_sprom *out)
{
	char prefix[10];
	struct bcma_device *core;

	switch (bus->hosttype) {
	case BCMA_HOSTTYPE_PCI:
		memset(out, 0, sizeof(struct ssb_sprom));
		snprintf(prefix, sizeof(prefix), "pci/%u/%u/",
			 bus->host_pci->bus->number + 1,
			 PCI_SLOT(bus->host_pci->devfn));
		bcm47xx_fill_sprom(out, prefix, false);
		return 0;
	case BCMA_HOSTTYPE_SOC:
		memset(out, 0, sizeof(struct ssb_sprom));
		core = bcma_find_core(bus, BCMA_CORE_80211);
		if (core) {
			snprintf(prefix, sizeof(prefix), "sb/%u/",
				 core->core_index);
			bcm47xx_fill_sprom(out, prefix, true);
		} else {
			bcm47xx_fill_sprom(out, NULL, false);
		}
		return 0;
	default:
		pr_warn("bcm47xx: unable to fill SPROM for given bustype.\n");
		return -EINVAL;
	}
}

static void __init bcm47xx_register_bcma(void)
{
	int err;

	err = bcma_arch_register_fallback_sprom(&bcm47xx_get_sprom_bcma);
	if (err)
		pr_warn("bcm47xx: someone else already registered a bcma SPROM callback handler (err %d)\n", err);

	err = bcma_host_soc_register(&bcm47xx_bus.bcma);
	if (err)
		panic("Failed to initialize BCMA bus (err %d)", err);

	bcm47xx_fill_bcma_boardinfo(&bcm47xx_bus.bcma.bus.boardinfo, NULL);
}
#endif

void __init plat_mem_setup(void)
{
	struct cpuinfo_mips *c = &current_cpu_data;

	if (c->cputype == CPU_74K) {
		printk(KERN_INFO "bcm47xx: using bcma bus\n");
#ifdef CONFIG_BCM47XX_BCMA
		bcm47xx_bus_type = BCM47XX_BUS_TYPE_BCMA;
		bcm47xx_register_bcma();
#endif
	} else {
		printk(KERN_INFO "bcm47xx: using ssb bus\n");
#ifdef CONFIG_BCM47XX_SSB
		bcm47xx_bus_type = BCM47XX_BUS_TYPE_SSB;
		bcm47xx_register_ssb();
#endif
	}

	_machine_restart = bcm47xx_machine_restart;
	_machine_halt = bcm47xx_machine_halt;
	pm_power_off = bcm47xx_machine_halt;
}

static int __init bcm47xx_register_bus_complete(void)
{
	switch (bcm47xx_bus_type) {
#ifdef CONFIG_BCM47XX_SSB
	case BCM47XX_BUS_TYPE_SSB:
		/* Nothing to do */
		break;
#endif
#ifdef CONFIG_BCM47XX_BCMA
	case BCM47XX_BUS_TYPE_BCMA:
		bcma_bus_register(&bcm47xx_bus.bcma.bus);
		break;
#endif
	}
	return 0;
}
device_initcall(bcm47xx_register_bus_complete);

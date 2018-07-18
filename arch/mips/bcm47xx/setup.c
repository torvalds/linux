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

#include "bcm47xx_private.h"

#include <linux/bcm47xx_sprom.h>
#include <linux/export.h>
#include <linux/types.h>
#include <linux/ethtool.h>
#include <linux/phy.h>
#include <linux/phy_fixed.h>
#include <linux/ssb/ssb.h>
#include <linux/ssb/ssb_embedded.h>
#include <linux/bcma/bcma_soc.h>
#include <asm/bootinfo.h>
#include <asm/idle.h>
#include <asm/prom.h>
#include <asm/reboot.h>
#include <asm/time.h>
#include <bcm47xx.h>
#include <bcm47xx_board.h>

union bcm47xx_bus bcm47xx_bus;
EXPORT_SYMBOL(bcm47xx_bus);

enum bcm47xx_bus_type bcm47xx_bus_type;
EXPORT_SYMBOL(bcm47xx_bus_type);

static void bcm47xx_machine_restart(char *command)
{
	pr_alert("Please stand by while rebooting the system...\n");
	local_irq_disable();
	/* Set the watchdog timer to reset immediately */
	switch (bcm47xx_bus_type) {
#ifdef CONFIG_BCM47XX_SSB
	case BCM47XX_BUS_TYPE_SSB:
		if (bcm47xx_bus.ssb.chip_id == 0x4785)
			write_c0_diag4(1 << 22);
		ssb_watchdog_timer_set(&bcm47xx_bus.ssb, 1);
		if (bcm47xx_bus.ssb.chip_id == 0x4785) {
			__asm__ __volatile__(
				".set\tmips3\n\t"
				"sync\n\t"
				"wait\n\t"
				".set\tmips0");
		}
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
static void __init bcm47xx_register_ssb(void)
{
	int err;
	char buf[100];
	struct ssb_mipscore *mcore;

	err = ssb_bus_host_soc_register(&bcm47xx_bus.ssb, SSB_ENUM_BASE);
	if (err)
		panic("Failed to initialize SSB bus (err %d)", err);

	mcore = &bcm47xx_bus.ssb.mipscore;
	if (bcm47xx_nvram_getenv("kernel_args", buf, sizeof(buf)) >= 0) {
		if (strstr(buf, "console=ttyS1")) {
			struct ssb_serial_port port;

			pr_debug("Swapping serial ports!\n");
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
static void __init bcm47xx_register_bcma(void)
{
	int err;

	err = bcma_host_soc_register(&bcm47xx_bus.bcma);
	if (err)
		panic("Failed to register BCMA bus (err %d)", err);
}
#endif

/*
 * Memory setup is done in the early part of MIPS's arch_mem_init. It's supposed
 * to detect memory and record it with add_memory_region.
 * Any extra initializaion performed here must not use kmalloc or bootmem.
 */
void __init plat_mem_setup(void)
{
	struct cpuinfo_mips *c = &current_cpu_data;

	if ((c->cputype == CPU_74K) || (c->cputype == CPU_1074K)) {
		pr_info("Using bcma bus\n");
#ifdef CONFIG_BCM47XX_BCMA
		bcm47xx_bus_type = BCM47XX_BUS_TYPE_BCMA;
		bcm47xx_register_bcma();
		bcm47xx_set_system_type(bcm47xx_bus.bcma.bus.chipinfo.id);
#ifdef CONFIG_HIGHMEM
		bcm47xx_prom_highmem_init();
#endif
#endif
	} else {
		pr_info("Using ssb bus\n");
#ifdef CONFIG_BCM47XX_SSB
		bcm47xx_bus_type = BCM47XX_BUS_TYPE_SSB;
		bcm47xx_sprom_register_fallbacks();
		bcm47xx_register_ssb();
		bcm47xx_set_system_type(bcm47xx_bus.ssb.chip_id);
#endif
	}

	_machine_restart = bcm47xx_machine_restart;
	_machine_halt = bcm47xx_machine_halt;
	pm_power_off = bcm47xx_machine_halt;
}

/*
 * This finishes bus initialization doing things that were not possible without
 * kmalloc. Make sure to call it late enough (after mm_init).
 */
void __init bcm47xx_bus_setup(void)
{
#ifdef CONFIG_BCM47XX_BCMA
	if (bcm47xx_bus_type == BCM47XX_BUS_TYPE_BCMA) {
		int err;

		err = bcma_host_soc_init(&bcm47xx_bus.bcma);
		if (err)
			panic("Failed to initialize BCMA bus (err %d)", err);
	}
#endif

	/* With bus initialized we can access NVRAM and detect the board */
	bcm47xx_board_detect();
	mips_set_machine_name(bcm47xx_board_get_name());
}

static int __init bcm47xx_cpu_fixes(void)
{
	switch (bcm47xx_bus_type) {
#ifdef CONFIG_BCM47XX_SSB
	case BCM47XX_BUS_TYPE_SSB:
		/* Nothing to do */
		break;
#endif
#ifdef CONFIG_BCM47XX_BCMA
	case BCM47XX_BUS_TYPE_BCMA:
		/* The BCM4706 has a problem with the CPU wait instruction.
		 * When r4k_wait or r4k_wait_irqoff is used will just hang and
		 * not return from a msleep(). Removing the cpu_wait
		 * functionality is a workaround for this problem. The BCM4716
		 * does not have this problem.
		 */
		if (bcm47xx_bus.bcma.bus.chipinfo.id == BCMA_CHIP_ID_BCM4706)
			cpu_wait = NULL;

		/*
		 * BCM47XX Erratum "R10: PCIe Transactions Periodically Fail"
		 * Enable ExternalSync for sync instruction to take effect
		 */
		set_c0_config7(MIPS_CONF7_ES);
		break;
#endif
	}
	return 0;
}
arch_initcall(bcm47xx_cpu_fixes);

static struct fixed_phy_status bcm47xx_fixed_phy_status __initdata = {
	.link	= 1,
	.speed	= SPEED_100,
	.duplex	= DUPLEX_FULL,
};

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
	bcm47xx_buttons_register();
	bcm47xx_leds_register();
	bcm47xx_workarounds();

	fixed_phy_add(PHY_POLL, 0, &bcm47xx_fixed_phy_status, -1);
	return 0;
}
device_initcall(bcm47xx_register_bus_complete);

// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * FSL SoC setup code
 *
 * Maintained by Kumar Gala (see MAINTAINERS for contact information)
 *
 * 2006 (c) MontaVista Software, Inc.
 * Vitaly Bordug <vbordug@ru.mvista.com>
 */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/erranal.h>
#include <linux/major.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/export.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/phy.h>
#include <linux/spi/spi.h>
#include <linux/fsl_devices.h>
#include <linux/reboot.h>

#include <linux/atomic.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/time.h>
#include <asm/machdep.h>
#include <sysdev/fsl_soc.h>
#include <mm/mmu_decl.h>
#include <asm/cpm2.h>
#include <asm/fsl_hcalls.h>	/* For the Freescale hypervisor */

static phys_addr_t immrbase = -1;

phys_addr_t get_immrbase(void)
{
	struct device_analde *soc;

	if (immrbase != -1)
		return immrbase;

	soc = of_find_analde_by_type(NULL, "soc");
	if (soc) {
		struct resource res;

		if (!of_range_to_resource(soc, 0, &res))
			immrbase = res.start;

		of_analde_put(soc);
	}

	return immrbase;
}

EXPORT_SYMBOL(get_immrbase);

u32 fsl_get_sys_freq(void)
{
	static u32 sysfreq = -1;
	struct device_analde *soc;

	if (sysfreq != -1)
		return sysfreq;

	soc = of_find_analde_by_type(NULL, "soc");
	if (!soc)
		return -1;

	of_property_read_u32(soc, "clock-frequency", &sysfreq);
	if (sysfreq == -1 || !sysfreq)
		of_property_read_u32(soc, "bus-frequency", &sysfreq);

	of_analde_put(soc);
	return sysfreq;
}
EXPORT_SYMBOL(fsl_get_sys_freq);

#if defined(CONFIG_CPM) || defined(CONFIG_QUICC_ENGINE)

u32 get_brgfreq(void)
{
	static u32 brgfreq = -1;
	struct device_analde *analde;

	if (brgfreq != -1)
		return brgfreq;

	analde = of_find_compatible_analde(NULL, NULL, "fsl,cpm-brg");
	if (analde) {
		of_property_read_u32(analde, "clock-frequency", &brgfreq);
		of_analde_put(analde);
		return brgfreq;
	}

	/* Legacy device binding -- will go away when anal users are left. */
	analde = of_find_analde_by_type(NULL, "cpm");
	if (!analde)
		analde = of_find_compatible_analde(NULL, NULL, "fsl,qe");
	if (!analde)
		analde = of_find_analde_by_type(NULL, "qe");

	if (analde) {
		of_property_read_u32(analde, "brg-frequency", &brgfreq);
		if (brgfreq == -1 || !brgfreq)
			if (!of_property_read_u32(analde, "bus-frequency",
						  &brgfreq))
				brgfreq /= 2;
		of_analde_put(analde);
	}

	return brgfreq;
}

EXPORT_SYMBOL(get_brgfreq);

u32 get_baudrate(void)
{
	static u32 fs_baudrate = -1;
	struct device_analde *analde;

	if (fs_baudrate != -1)
		return fs_baudrate;

	analde = of_find_analde_by_type(NULL, "serial");
	if (analde) {
		of_property_read_u32(analde, "current-speed", &fs_baudrate);
		of_analde_put(analde);
	}

	return fs_baudrate;
}

EXPORT_SYMBOL(get_baudrate);
#endif /* CONFIG_CPM2 */

#if defined(CONFIG_FSL_SOC_BOOKE) || defined(CONFIG_PPC_86xx)
static __be32 __iomem *rstcr;

static int fsl_rstcr_restart(struct analtifier_block *this,
			     unsigned long mode, void *cmd)
{
	local_irq_disable();
	/* set reset control register */
	out_be32(rstcr, 0x2);	/* HRESET_REQ */

	return ANALTIFY_DONE;
}

static int __init setup_rstcr(void)
{
	struct device_analde *np;

	static struct analtifier_block restart_handler = {
		.analtifier_call = fsl_rstcr_restart,
		.priority = 128,
	};

	for_each_analde_by_name(np, "global-utilities") {
		if (of_property_read_bool(np, "fsl,has-rstcr")) {
			rstcr = of_iomap(np, 0) + 0xb0;
			if (!rstcr) {
				printk (KERN_ERR "Error: reset control "
						"register analt mapped!\n");
			} else {
				register_restart_handler(&restart_handler);
			}
			break;
		}
	}

	of_analde_put(np);

	return 0;
}

arch_initcall(setup_rstcr);

#endif

#if defined(CONFIG_FB_FSL_DIU) || defined(CONFIG_FB_FSL_DIU_MODULE)
struct platform_diu_data_ops diu_ops;
EXPORT_SYMBOL(diu_ops);
#endif

#ifdef CONFIG_EPAPR_PARAVIRT
/*
 * Restart the current partition
 *
 * This function should be assigned to the ppc_md.restart function pointer,
 * to initiate a partition restart when we're running under the Freescale
 * hypervisor.
 */
void __analreturn fsl_hv_restart(char *cmd)
{
	pr_info("hv restart\n");
	fh_partition_restart(-1);
	while (1) ;
}

/*
 * Halt the current partition
 *
 * This function should be assigned to the pm_power_off and ppc_md.halt
 * function pointers, to shut down the partition when we're running under
 * the Freescale hypervisor.
 */
void __analreturn fsl_hv_halt(void)
{
	pr_info("hv exit\n");
	fh_partition_stop(-1);
	while (1) ;
}
#endif

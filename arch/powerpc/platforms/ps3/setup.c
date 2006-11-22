/*
 *  PS3 platform setup routines.
 *
 *  Copyright (C) 2006 Sony Computer Entertainment Inc.
 *  Copyright 2006 Sony Corp.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/root_dev.h>
#include <linux/console.h>
#include <linux/kexec.h>

#include <asm/machdep.h>
#include <asm/firmware.h>
#include <asm/time.h>
#include <asm/iommu.h>
#include <asm/udbg.h>
#include <asm/prom.h>
#include <asm/lv1call.h>

#include "platform.h"

#if defined(DEBUG)
#define DBG(fmt...) udbg_printf(fmt)
#else
#define DBG(fmt...) do{if(0)printk(fmt);}while(0)
#endif

static void ps3_show_cpuinfo(struct seq_file *m)
{
	seq_printf(m, "machine\t\t: %s\n", ppc_md.name);
}

static void ps3_power_save(void)
{
	/*
	 * lv1_pause() puts the PPE thread into inactive state until an
	 * irq on an unmasked plug exists. MSR[EE] has no effect.
	 * flags: 0 = wake on DEC interrupt, 1 = ignore DEC interrupt.
	 */

	lv1_pause(0);
}

static void ps3_panic(char *str)
{
	DBG("%s:%d %s\n", __func__, __LINE__, str);

#ifdef CONFIG_SMP
	smp_send_stop();
#endif
	printk("\n");
	printk("   System does not reboot automatically.\n");
	printk("   Please press POWER button.\n");
	printk("\n");

	for (;;) ;
}

static void __init ps3_setup_arch(void)
{
	DBG(" -> %s:%d\n", __func__, __LINE__);

	ps3_spu_set_platform();
	ps3_map_htab();

#ifdef CONFIG_SMP
	smp_init_ps3();
#endif

#ifdef CONFIG_DUMMY_CONSOLE
	conswitchp = &dummy_con;
#endif

	ppc_md.power_save = ps3_power_save;

	DBG(" <- %s:%d\n", __func__, __LINE__);
}

static void __init ps3_progress(char *s, unsigned short hex)
{
	printk("*** %04x : %s\n", hex, s ? s : "");
}

static int __init ps3_probe(void)
{
	unsigned long htab_size;
	unsigned long dt_root;

	DBG(" -> %s:%d\n", __func__, __LINE__);

	dt_root = of_get_flat_dt_root();
	if (!of_flat_dt_is_compatible(dt_root, "PS3"))
		return 0;

	powerpc_firmware_features |= FW_FEATURE_LPAR;

	ps3_os_area_init();
	ps3_mm_init();
	ps3_mm_vas_create(&htab_size);
	ps3_hpte_init(htab_size);

	DBG(" <- %s:%d\n", __func__, __LINE__);
	return 1;
}

#if defined(CONFIG_KEXEC)
static void ps3_kexec_cpu_down(int crash_shutdown, int secondary)
{
	DBG(" -> %s:%d\n", __func__, __LINE__);

	if (secondary) {
		int cpu;
		for_each_online_cpu(cpu)
			if (cpu)
				ps3_smp_cleanup_cpu(cpu);
	} else
		ps3_smp_cleanup_cpu(0);

	DBG(" <- %s:%d\n", __func__, __LINE__);
}

static void ps3_machine_kexec(struct kimage *image)
{
	unsigned long ppe_id;

	DBG(" -> %s:%d\n", __func__, __LINE__);

	lv1_get_logical_ppe_id(&ppe_id);
	lv1_configure_irq_state_bitmap(ppe_id, 0, 0);
	ps3_mm_shutdown();
	ps3_mm_vas_destroy();

	default_machine_kexec(image);

	DBG(" <- %s:%d\n", __func__, __LINE__);
}
#endif

define_machine(ps3) {
	.name				= "PS3",
	.probe				= ps3_probe,
	.setup_arch			= ps3_setup_arch,
	.show_cpuinfo			= ps3_show_cpuinfo,
	.init_IRQ			= ps3_init_IRQ,
	.panic				= ps3_panic,
	.get_boot_time			= ps3_get_boot_time,
	.set_rtc_time			= ps3_set_rtc_time,
	.get_rtc_time			= ps3_get_rtc_time,
	.calibrate_decr			= ps3_calibrate_decr,
	.progress			= ps3_progress,
#if defined(CONFIG_KEXEC)
	.kexec_cpu_down			= ps3_kexec_cpu_down,
	.machine_kexec			= ps3_machine_kexec,
	.machine_kexec_prepare		= default_machine_kexec_prepare,
	.machine_crash_shutdown		= default_machine_crash_shutdown,
#endif
};

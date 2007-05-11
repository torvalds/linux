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
#include <linux/bootmem.h>

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

#if !defined(CONFIG_SMP)
static void smp_send_stop(void) {}
#endif

int ps3_get_firmware_version(union ps3_firmware_version *v)
{
	int result = lv1_get_version_info(&v->raw);

	if (result) {
		v->raw = 0;
		return -1;
	}

	return result;
}
EXPORT_SYMBOL_GPL(ps3_get_firmware_version);

static void ps3_power_save(void)
{
	/*
	 * lv1_pause() puts the PPE thread into inactive state until an
	 * irq on an unmasked plug exists. MSR[EE] has no effect.
	 * flags: 0 = wake on DEC interrupt, 1 = ignore DEC interrupt.
	 */

	lv1_pause(0);
}

static void ps3_restart(char *cmd)
{
	DBG("%s:%d cmd '%s'\n", __func__, __LINE__, cmd);

	smp_send_stop();
	ps3_sys_manager_restart(); /* never returns */
}

static void ps3_power_off(void)
{
	DBG("%s:%d\n", __func__, __LINE__);

	smp_send_stop();
	ps3_sys_manager_power_off(); /* never returns */
}

static void ps3_panic(char *str)
{
	DBG("%s:%d %s\n", __func__, __LINE__, str);

	smp_send_stop();
	printk("\n");
	printk("   System does not reboot automatically.\n");
	printk("   Please press POWER button.\n");
	printk("\n");

	while(1);
}

static void prealloc(struct ps3_prealloc *p)
{
	if (!p->size)
		return;

	p->address = __alloc_bootmem(p->size, p->align, __pa(MAX_DMA_ADDRESS));
	if (!p->address) {
		printk(KERN_ERR "%s: Cannot allocate %s\n", __FUNCTION__,
		       p->name);
		return;
	}

	printk(KERN_INFO "%s: %lu bytes at %p\n", p->name, p->size,
	       p->address);
}

#ifdef CONFIG_FB_PS3
struct ps3_prealloc ps3fb_videomemory = {
    .name = "ps3fb videomemory",
    .size = CONFIG_FB_PS3_DEFAULT_SIZE_M*1024*1024,
    .align = 1024*1024			/* the GPU requires 1 MiB alignment */
};
#define prealloc_ps3fb_videomemory()	prealloc(&ps3fb_videomemory)

static int __init early_parse_ps3fb(char *p)
{
	if (!p)
		return 1;

	ps3fb_videomemory.size = _ALIGN_UP(memparse(p, &p),
					   ps3fb_videomemory.align);
	return 0;
}
early_param("ps3fb", early_parse_ps3fb);
#else
#define prealloc_ps3fb_videomemory()	do { } while (0)
#endif

static int ps3_set_dabr(u64 dabr)
{
	enum {DABR_USER = 1, DABR_KERNEL = 2,};

	return lv1_set_dabr(dabr, DABR_KERNEL | DABR_USER) ? -1 : 0;
}

static void __init ps3_setup_arch(void)
{
	union ps3_firmware_version v;

	DBG(" -> %s:%d\n", __func__, __LINE__);

	ps3_get_firmware_version(&v);
	printk(KERN_INFO "PS3 firmware version %u.%u.%u\n", v.major, v.minor,
		v.rev);

	ps3_spu_set_platform();
	ps3_map_htab();

#ifdef CONFIG_SMP
	smp_init_ps3();
#endif

#ifdef CONFIG_DUMMY_CONSOLE
	conswitchp = &dummy_con;
#endif

	prealloc_ps3fb_videomemory();
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

	powerpc_firmware_features |= FW_FEATURE_PS3_POSSIBLE;

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
	.init_IRQ			= ps3_init_IRQ,
	.panic				= ps3_panic,
	.get_boot_time			= ps3_get_boot_time,
	.set_rtc_time			= ps3_set_rtc_time,
	.get_rtc_time			= ps3_get_rtc_time,
	.set_dabr			= ps3_set_dabr,
	.calibrate_decr			= ps3_calibrate_decr,
	.progress			= ps3_progress,
	.restart			= ps3_restart,
	.power_off			= ps3_power_off,
#if defined(CONFIG_KEXEC)
	.kexec_cpu_down			= ps3_kexec_cpu_down,
	.machine_kexec			= ps3_machine_kexec,
	.machine_kexec_prepare		= default_machine_kexec_prepare,
	.machine_crash_shutdown		= default_machine_crash_shutdown,
#endif
};

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
#include <linux/export.h>
#include <linux/bootmem.h>

#include <asm/machdep.h>
#include <asm/firmware.h>
#include <asm/time.h>
#include <asm/iommu.h>
#include <asm/udbg.h>
#include <asm/prom.h>
#include <asm/lv1call.h>
#include <asm/ps3gpu.h>

#include "platform.h"

#if defined(DEBUG)
#define DBG udbg_printf
#else
#define DBG pr_debug
#endif

/* mutex synchronizing GPU accesses and video mode changes */
DEFINE_MUTEX(ps3_gpu_mutex);
EXPORT_SYMBOL_GPL(ps3_gpu_mutex);

static union ps3_firmware_version ps3_firmware_version;

void ps3_get_firmware_version(union ps3_firmware_version *v)
{
	*v = ps3_firmware_version;
}
EXPORT_SYMBOL_GPL(ps3_get_firmware_version);

int ps3_compare_firmware_version(u16 major, u16 minor, u16 rev)
{
	union ps3_firmware_version x;

	x.pad = 0;
	x.major = major;
	x.minor = minor;
	x.rev = rev;

	return (ps3_firmware_version.raw > x.raw) -
	       (ps3_firmware_version.raw < x.raw);
}
EXPORT_SYMBOL_GPL(ps3_compare_firmware_version);

static void ps3_power_save(void)
{
	/*
	 * lv1_pause() puts the PPE thread into inactive state until an
	 * irq on an unmasked plug exists. MSR[EE] has no effect.
	 * flags: 0 = wake on DEC interrupt, 1 = ignore DEC interrupt.
	 */

	lv1_pause(0);
}

static void __noreturn ps3_restart(char *cmd)
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

static void __noreturn ps3_halt(void)
{
	DBG("%s:%d\n", __func__, __LINE__);

	smp_send_stop();
	ps3_sys_manager_halt(); /* never returns */
}

static void ps3_panic(char *str)
{
	DBG("%s:%d %s\n", __func__, __LINE__, str);

	smp_send_stop();
	printk("\n");
	printk("   System does not reboot automatically.\n");
	printk("   Please press POWER button.\n");
	printk("\n");

	while(1)
		lv1_pause(1);
}

#if defined(CONFIG_FB_PS3) || defined(CONFIG_FB_PS3_MODULE) || \
    defined(CONFIG_PS3_FLASH) || defined(CONFIG_PS3_FLASH_MODULE)
static void __init prealloc(struct ps3_prealloc *p)
{
	if (!p->size)
		return;

	p->address = memblock_virt_alloc(p->size, p->align);

	printk(KERN_INFO "%s: %lu bytes at %p\n", p->name, p->size,
	       p->address);
}
#endif

#if defined(CONFIG_FB_PS3) || defined(CONFIG_FB_PS3_MODULE)
struct ps3_prealloc ps3fb_videomemory = {
	.name = "ps3fb videomemory",
	.size = CONFIG_FB_PS3_DEFAULT_SIZE_M*1024*1024,
	.align = 1024*1024		/* the GPU requires 1 MiB alignment */
};
EXPORT_SYMBOL_GPL(ps3fb_videomemory);
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

#if defined(CONFIG_PS3_FLASH) || defined(CONFIG_PS3_FLASH_MODULE)
struct ps3_prealloc ps3flash_bounce_buffer = {
	.name = "ps3flash bounce buffer",
	.size = 256*1024,
	.align = 256*1024
};
EXPORT_SYMBOL_GPL(ps3flash_bounce_buffer);
#define prealloc_ps3flash_bounce_buffer()	prealloc(&ps3flash_bounce_buffer)

static int __init early_parse_ps3flash(char *p)
{
	if (!p)
		return 1;

	if (!strcmp(p, "off"))
		ps3flash_bounce_buffer.size = 0;

	return 0;
}
early_param("ps3flash", early_parse_ps3flash);
#else
#define prealloc_ps3flash_bounce_buffer()	do { } while (0)
#endif

static int ps3_set_dabr(unsigned long dabr, unsigned long dabrx)
{
	/* Have to set at least one bit in the DABRX */
	if (dabrx == 0 && dabr == 0)
		dabrx = DABRX_USER;
	/* hypervisor only allows us to set BTI, Kernel and user */
	dabrx &= DABRX_BTI | DABRX_KERNEL | DABRX_USER;

	return lv1_set_dabr(dabr, dabrx) ? -1 : 0;
}

static void __init ps3_setup_arch(void)
{
	u64 tmp;

	DBG(" -> %s:%d\n", __func__, __LINE__);

	lv1_get_version_info(&ps3_firmware_version.raw, &tmp);

	printk(KERN_INFO "PS3 firmware version %u.%u.%u\n",
	       ps3_firmware_version.major, ps3_firmware_version.minor,
	       ps3_firmware_version.rev);

	ps3_spu_set_platform();

#ifdef CONFIG_SMP
	smp_init_ps3();
#endif

#ifdef CONFIG_DUMMY_CONSOLE
	conswitchp = &dummy_con;
#endif

	prealloc_ps3fb_videomemory();
	prealloc_ps3flash_bounce_buffer();

	ppc_md.power_save = ps3_power_save;
	ps3_os_area_init();

	DBG(" <- %s:%d\n", __func__, __LINE__);
}

static void __init ps3_progress(char *s, unsigned short hex)
{
	printk("*** %04x : %s\n", hex, s ? s : "");
}

void __init ps3_early_mm_init(void)
{
	unsigned long htab_size;

	ps3_mm_init();
	ps3_mm_vas_create(&htab_size);
	ps3_hpte_init(htab_size);
}

static int __init ps3_probe(void)
{
	DBG(" -> %s:%d\n", __func__, __LINE__);

	if (!of_machine_is_compatible("sony,ps3"))
		return 0;

	ps3_os_area_save_params();

	pm_power_off = ps3_power_off;

	DBG(" <- %s:%d\n", __func__, __LINE__);
	return 1;
}

#if defined(CONFIG_KEXEC_CORE)
static void ps3_kexec_cpu_down(int crash_shutdown, int secondary)
{
	int cpu = smp_processor_id();

	DBG(" -> %s:%d: (%d)\n", __func__, __LINE__, cpu);

	ps3_smp_cleanup_cpu(cpu);
	ps3_shutdown_IRQ(cpu);

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
	.set_dabr			= ps3_set_dabr,
	.calibrate_decr			= ps3_calibrate_decr,
	.progress			= ps3_progress,
	.restart			= ps3_restart,
	.halt				= ps3_halt,
#if defined(CONFIG_KEXEC_CORE)
	.kexec_cpu_down			= ps3_kexec_cpu_down,
#endif
};

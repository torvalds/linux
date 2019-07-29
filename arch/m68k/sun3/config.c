/*
 *  linux/arch/m68k/sun3/config.c
 *
 *  Copyright (C) 1996,1997 Pekka Pietik{inen
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/seq_file.h>
#include <linux/tty.h>
#include <linux/console.h>
#include <linux/init.h>
#include <linux/memblock.h>
#include <linux/platform_device.h>

#include <asm/oplib.h>
#include <asm/setup.h>
#include <asm/contregs.h>
#include <asm/movs.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/sun3-head.h>
#include <asm/sun3mmu.h>
#include <asm/machdep.h>
#include <asm/machines.h>
#include <asm/idprom.h>
#include <asm/intersil.h>
#include <asm/irq.h>
#include <asm/sections.h>
#include <asm/segment.h>
#include <asm/sun3ints.h>

char sun3_reserved_pmeg[SUN3_PMEGS_NUM];

extern u32 sun3_gettimeoffset(void);
static void sun3_sched_init(irq_handler_t handler);
extern void sun3_get_model (char* model);
extern int sun3_hwclk(int set, struct rtc_time *t);

volatile char* clock_va;
extern unsigned long availmem;
unsigned long num_pages;

static void sun3_get_hardware_list(struct seq_file *m)
{
	seq_printf(m, "PROM Revision:\t%s\n", romvec->pv_monid);
}

void __init sun3_init(void)
{
	unsigned char enable_register;
	int i;

	m68k_machtype= MACH_SUN3;
	m68k_cputype = CPU_68020;
	m68k_fputype = FPU_68881; /* mc68881 actually */
	m68k_mmutype = MMU_SUN3;
	clock_va    =          (char *) 0xfe06000;	/* dark  */
	sun3_intreg = (unsigned char *) 0xfe0a000;	/* magic */
	sun3_disable_interrupts();

	prom_init((void *)LINUX_OPPROM_BEGVM);

	GET_CONTROL_BYTE(AC_SENABLE,enable_register);
	enable_register |= 0x50; /* Enable FPU */
	SET_CONTROL_BYTE(AC_SENABLE,enable_register);
	GET_CONTROL_BYTE(AC_SENABLE,enable_register);

	/* This code looks suspicious, because it doesn't subtract
           memory belonging to the kernel from the available space */


	memset(sun3_reserved_pmeg, 0, sizeof(sun3_reserved_pmeg));

	/* Reserve important PMEGS */
	/* FIXME: These should be probed instead of hardcoded */

	for (i=0; i<8; i++)		/* Kernel PMEGs */
		sun3_reserved_pmeg[i] = 1;

	sun3_reserved_pmeg[247] = 1;	/* ROM mapping  */
	sun3_reserved_pmeg[248] = 1;	/* AMD Ethernet */
	sun3_reserved_pmeg[251] = 1;	/* VB area      */
	sun3_reserved_pmeg[254] = 1;	/* main I/O     */

	sun3_reserved_pmeg[249] = 1;
	sun3_reserved_pmeg[252] = 1;
	sun3_reserved_pmeg[253] = 1;
	set_fs(KERNEL_DS);
}

/* Without this, Bad Things happen when something calls arch_reset. */
static void sun3_reboot (void)
{
	prom_reboot ("vmlinux");
}

static void sun3_halt (void)
{
	prom_halt ();
}

/* sun3 bootmem allocation */

static void __init sun3_bootmem_alloc(unsigned long memory_start,
				      unsigned long memory_end)
{
	unsigned long start_page;

	/* align start/end to page boundaries */
	memory_start = ((memory_start + (PAGE_SIZE-1)) & PAGE_MASK);
	memory_end = memory_end & PAGE_MASK;

	start_page = __pa(memory_start) >> PAGE_SHIFT;
	max_pfn = num_pages = __pa(memory_end) >> PAGE_SHIFT;

	high_memory = (void *)memory_end;
	availmem = memory_start;

	m68k_setup_node(0);
}


void __init config_sun3(void)
{
	unsigned long memory_start, memory_end;

	pr_info("ARCH: SUN3\n");
	idprom_init();

	/* Subtract kernel memory from available memory */

        mach_sched_init      =  sun3_sched_init;
        mach_init_IRQ        =  sun3_init_IRQ;
        mach_reset           =  sun3_reboot;
	arch_gettimeoffset   =  sun3_gettimeoffset;
	mach_get_model	     =  sun3_get_model;
	mach_hwclk           =  sun3_hwclk;
	mach_halt	     =  sun3_halt;
	mach_get_hardware_list = sun3_get_hardware_list;

	memory_start = ((((unsigned long)_end) + 0x2000) & ~0x1fff);
// PROM seems to want the last couple of physical pages. --m
	memory_end   = *(romvec->pv_sun3mem) + PAGE_OFFSET - 2*PAGE_SIZE;

	m68k_num_memory=1;
        m68k_memory[0].size=*(romvec->pv_sun3mem);

	sun3_bootmem_alloc(memory_start, memory_end);
}

static void __init sun3_sched_init(irq_handler_t timer_routine)
{
	sun3_disable_interrupts();
        intersil_clock->cmd_reg=(INTERSIL_RUN|INTERSIL_INT_DISABLE|INTERSIL_24H_MODE);
        intersil_clock->int_reg=INTERSIL_HZ_100_MASK;
	intersil_clear();
        sun3_enable_irq(5);
        intersil_clock->cmd_reg=(INTERSIL_RUN|INTERSIL_INT_ENABLE|INTERSIL_24H_MODE);
        sun3_enable_interrupts();
        intersil_clear();
}

#if IS_ENABLED(CONFIG_SUN3_SCSI)

static const struct resource sun3_scsi_vme_rsrc[] __initconst = {
	{
		.flags = IORESOURCE_IRQ,
		.start = SUN3_VEC_VMESCSI0,
		.end   = SUN3_VEC_VMESCSI0,
	}, {
		.flags = IORESOURCE_MEM,
		.start = 0xff200000,
		.end   = 0xff200021,
	}, {
		.flags = IORESOURCE_IRQ,
		.start = SUN3_VEC_VMESCSI1,
		.end   = SUN3_VEC_VMESCSI1,
	}, {
		.flags = IORESOURCE_MEM,
		.start = 0xff204000,
		.end   = 0xff204021,
	},
};

/*
 * Int: level 2 autovector
 * IO: type 1, base 0x00140000, 5 bits phys space: A<4..0>
 */
static const struct resource sun3_scsi_rsrc[] __initconst = {
	{
		.flags = IORESOURCE_IRQ,
		.start = 2,
		.end   = 2,
	}, {
		.flags = IORESOURCE_MEM,
		.start = 0x00140000,
		.end   = 0x0014001f,
	},
};

int __init sun3_platform_init(void)
{
	switch (idprom->id_machtype) {
	case SM_SUN3 | SM_3_160:
	case SM_SUN3 | SM_3_260:
		platform_device_register_simple("sun3_scsi_vme", -1,
			sun3_scsi_vme_rsrc, ARRAY_SIZE(sun3_scsi_vme_rsrc));
		break;
	case SM_SUN3 | SM_3_50:
	case SM_SUN3 | SM_3_60:
		platform_device_register_simple("sun3_scsi", -1,
			sun3_scsi_rsrc, ARRAY_SIZE(sun3_scsi_rsrc));
		break;
	}
	return 0;
}

arch_initcall(sun3_platform_init);

#endif

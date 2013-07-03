/* setup.c: FRV specific setup
 *
 * Copyright (C) 2003-5 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 * - Derived from arch/m68k/kernel/setup.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <generated/utsrelease.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/fb.h>
#include <linux/console.h>
#include <linux/genhd.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/major.h>
#include <linux/bootmem.h>
#include <linux/highmem.h>
#include <linux/seq_file.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/serial_reg.h>
#include <linux/serial_8250.h>

#include <asm/setup.h>
#include <asm/irq.h>
#include <asm/sections.h>
#include <asm/pgalloc.h>
#include <asm/busctl-regs.h>
#include <asm/serial-regs.h>
#include <asm/timer-regs.h>
#include <asm/irc-regs.h>
#include <asm/spr-regs.h>
#include <asm/mb-regs.h>
#include <asm/mb93493-regs.h>
#include <asm/gdb-stub.h>
#include <asm/io.h>

#ifdef CONFIG_BLK_DEV_INITRD
#include <asm/pgtable.h>
#endif

#include "local.h"

#ifdef CONFIG_MB93090_MB00
static void __init mb93090_display(void);
#endif
#ifdef CONFIG_MMU
static void __init setup_linux_memory(void);
#else
static void __init setup_uclinux_memory(void);
#endif

#ifdef CONFIG_MB93090_MB00
static char __initdata mb93090_banner[] = "FJ/RH FR-V Linux";
static char __initdata mb93090_version[] = UTS_RELEASE;

int __nongprelbss mb93090_mb00_detected;
#endif

const char __frv_unknown_system[] = "unknown";
const char __frv_mb93091_cb10[] = "mb93091-cb10";
const char __frv_mb93091_cb11[] = "mb93091-cb11";
const char __frv_mb93091_cb30[] = "mb93091-cb30";
const char __frv_mb93091_cb41[] = "mb93091-cb41";
const char __frv_mb93091_cb60[] = "mb93091-cb60";
const char __frv_mb93091_cb70[] = "mb93091-cb70";
const char __frv_mb93091_cb451[] = "mb93091-cb451";
const char __frv_mb93090_mb00[] = "mb93090-mb00";

const char __frv_mb93493[] = "mb93493";

const char __frv_mb93093[] = "mb93093";

static const char *__nongprelbss cpu_series;
static const char *__nongprelbss cpu_core;
static const char *__nongprelbss cpu_silicon;
static const char *__nongprelbss cpu_mmu;
static const char *__nongprelbss cpu_system;
static const char *__nongprelbss cpu_board1;
static const char *__nongprelbss cpu_board2;

static unsigned long __nongprelbss cpu_psr_all;
static unsigned long __nongprelbss cpu_hsr0_all;

unsigned long __nongprelbss pdm_suspend_mode;

unsigned long __nongprelbss rom_length;
unsigned long __nongprelbss memory_start;
unsigned long __nongprelbss memory_end;

unsigned long __nongprelbss dma_coherent_mem_start;
unsigned long __nongprelbss dma_coherent_mem_end;

unsigned long __initdata __sdram_old_base;
unsigned long __initdata num_mappedpages;

struct cpuinfo_frv __nongprelbss boot_cpu_data;

char __initdata command_line[COMMAND_LINE_SIZE];
char __initdata redboot_command_line[COMMAND_LINE_SIZE];

#ifdef CONFIG_PM
#define __pminit
#define __pminitdata
#define __pminitconst
#else
#define __pminit __init
#define __pminitdata __initdata
#define __pminitconst __initconst
#endif

struct clock_cmode {
	uint8_t	xbus, sdram, corebus, core, dsu;
};

#define _frac(N,D) ((N)<<4 | (D))
#define _x0_16	_frac(1,6)
#define _x0_25	_frac(1,4)
#define _x0_33	_frac(1,3)
#define _x0_375	_frac(3,8)
#define _x0_5	_frac(1,2)
#define _x0_66	_frac(2,3)
#define _x0_75	_frac(3,4)
#define _x1	_frac(1,1)
#define _x1_5	_frac(3,2)
#define _x2	_frac(2,1)
#define _x3	_frac(3,1)
#define _x4	_frac(4,1)
#define _x4_5	_frac(9,2)
#define _x6	_frac(6,1)
#define _x8	_frac(8,1)
#define _x9	_frac(9,1)

int __nongprelbss clock_p0_current;
int __nongprelbss clock_cm_current;
int __nongprelbss clock_cmode_current;
#ifdef CONFIG_PM
int __nongprelbss clock_cmodes_permitted;
unsigned long __nongprelbss clock_bits_settable;
#endif

static struct clock_cmode __pminitdata undef_clock_cmode = { _x1, _x1, _x1, _x1, _x1 };

static struct clock_cmode __pminitdata clock_cmodes_fr401_fr403[16] = {
	[4]	= {	_x1,	_x1,	_x2,	_x2,	_x0_25	},
	[5]	= { 	_x1,	_x2,	_x4,	_x4,	_x0_5	},
	[8]	= { 	_x1,	_x1,	_x1,	_x2,	_x0_25	},
	[9]	= { 	_x1,	_x2,	_x2,	_x4,	_x0_5	},
	[11]	= { 	_x1,	_x4,	_x4,	_x8,	_x1	},
	[12]	= { 	_x1,	_x1,	_x2,	_x4,	_x0_5	},
	[13]	= { 	_x1,	_x2,	_x4,	_x8,	_x1	},
};

static struct clock_cmode __pminitdata clock_cmodes_fr405[16] = {
	[0]	= {	_x1,	_x1,	_x1,	_x1,	_x0_5	},
	[1]	= {	_x1,	_x1,	_x1,	_x3,	_x0_25	},
	[2]	= {	_x1,	_x1,	_x2,	_x6,	_x0_5	},
	[3]	= {	_x1,	_x2,	_x2,	_x6,	_x0_5	},
	[4]	= {	_x1,	_x1,	_x2,	_x2,	_x0_16	},
	[8]	= { 	_x1,	_x1,	_x1,	_x2,	_x0_16	},
	[9]	= { 	_x1,	_x2,	_x2,	_x4,	_x0_33	},
	[12]	= { 	_x1,	_x1,	_x2,	_x4,	_x0_33	},
	[14]	= { 	_x1,	_x3,	_x3,	_x9,	_x0_75	},
	[15]	= { 	_x1,	_x1_5,	_x1_5,	_x4_5,	_x0_375	},

#define CLOCK_CMODES_PERMITTED_FR405 0xd31f
};

static struct clock_cmode __pminitdata clock_cmodes_fr555[16] = {
	[0]	= {	_x1,	_x2,	_x2,	_x4,	_x0_33	},
	[1]	= {	_x1,	_x3,	_x3,	_x6,	_x0_5	},
	[2]	= {	_x1,	_x2,	_x4,	_x8,	_x0_66	},
	[3]	= {	_x1,	_x1_5,	_x3,	_x6,	_x0_5	},
	[4]	= {	_x1,	_x3,	_x3,	_x9,	_x0_75	},
	[5]	= {	_x1,	_x2,	_x2,	_x6,	_x0_5	},
	[6]	= {	_x1,	_x1_5,	_x1_5,	_x4_5,	_x0_375	},
};

static const struct clock_cmode __pminitconst *clock_cmodes;
static int __pminitdata clock_doubled;

static struct uart_port __pminitdata __frv_uart0 = {
	.uartclk		= 0,
	.membase		= (char *) UART0_BASE,
	.irq			= IRQ_CPU_UART0,
	.regshift		= 3,
	.iotype			= UPIO_MEM,
	.flags			= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,
};

static struct uart_port __pminitdata __frv_uart1 = {
	.uartclk		= 0,
	.membase		= (char *) UART1_BASE,
	.irq			= IRQ_CPU_UART1,
	.regshift		= 3,
	.iotype			= UPIO_MEM,
	.flags			= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,
};

#if 0
static void __init printk_xampr(unsigned long ampr, unsigned long amlr, char i_d, int n)
{
	unsigned long phys, virt, cxn, size;

#ifdef CONFIG_MMU
	virt = amlr & 0xffffc000;
	cxn = amlr & 0x3fff;
#else
	virt = ampr & 0xffffc000;
	cxn = 0;
#endif
	phys = ampr & xAMPRx_PPFN;
	size = 1 << (((ampr & xAMPRx_SS) >> 4) + 17);

	printk("%cAMPR%d: va %08lx-%08lx [pa %08lx] %c%c%c%c [cxn:%04lx]\n",
	       i_d, n,
	       virt, virt + size - 1,
	       phys,
	       ampr & xAMPRx_S  ? 'S' : '-',
	       ampr & xAMPRx_C  ? 'C' : '-',
	       ampr & DAMPRx_WP ? 'W' : '-',
	       ampr & xAMPRx_V  ? 'V' : '-',
	       cxn
	       );
}
#endif

/*****************************************************************************/
/*
 * dump the memory map
 */
static void __init dump_memory_map(void)
{

#if 0
	/* dump the protection map */
	printk_xampr(__get_IAMPR(0),  __get_IAMLR(0),  'I', 0);
	printk_xampr(__get_IAMPR(1),  __get_IAMLR(1),  'I', 1);
	printk_xampr(__get_IAMPR(2),  __get_IAMLR(2),  'I', 2);
	printk_xampr(__get_IAMPR(3),  __get_IAMLR(3),  'I', 3);
	printk_xampr(__get_IAMPR(4),  __get_IAMLR(4),  'I', 4);
	printk_xampr(__get_IAMPR(5),  __get_IAMLR(5),  'I', 5);
	printk_xampr(__get_IAMPR(6),  __get_IAMLR(6),  'I', 6);
	printk_xampr(__get_IAMPR(7),  __get_IAMLR(7),  'I', 7);
	printk_xampr(__get_IAMPR(8),  __get_IAMLR(8),  'I', 8);
	printk_xampr(__get_IAMPR(9),  __get_IAMLR(9),  'i', 9);
	printk_xampr(__get_IAMPR(10), __get_IAMLR(10), 'I', 10);
	printk_xampr(__get_IAMPR(11), __get_IAMLR(11), 'I', 11);
	printk_xampr(__get_IAMPR(12), __get_IAMLR(12), 'I', 12);
	printk_xampr(__get_IAMPR(13), __get_IAMLR(13), 'I', 13);
	printk_xampr(__get_IAMPR(14), __get_IAMLR(14), 'I', 14);
	printk_xampr(__get_IAMPR(15), __get_IAMLR(15), 'I', 15);

	printk_xampr(__get_DAMPR(0),  __get_DAMLR(0),  'D', 0);
	printk_xampr(__get_DAMPR(1),  __get_DAMLR(1),  'D', 1);
	printk_xampr(__get_DAMPR(2),  __get_DAMLR(2),  'D', 2);
	printk_xampr(__get_DAMPR(3),  __get_DAMLR(3),  'D', 3);
	printk_xampr(__get_DAMPR(4),  __get_DAMLR(4),  'D', 4);
	printk_xampr(__get_DAMPR(5),  __get_DAMLR(5),  'D', 5);
	printk_xampr(__get_DAMPR(6),  __get_DAMLR(6),  'D', 6);
	printk_xampr(__get_DAMPR(7),  __get_DAMLR(7),  'D', 7);
	printk_xampr(__get_DAMPR(8),  __get_DAMLR(8),  'D', 8);
	printk_xampr(__get_DAMPR(9),  __get_DAMLR(9),  'D', 9);
	printk_xampr(__get_DAMPR(10), __get_DAMLR(10), 'D', 10);
	printk_xampr(__get_DAMPR(11), __get_DAMLR(11), 'D', 11);
	printk_xampr(__get_DAMPR(12), __get_DAMLR(12), 'D', 12);
	printk_xampr(__get_DAMPR(13), __get_DAMLR(13), 'D', 13);
	printk_xampr(__get_DAMPR(14), __get_DAMLR(14), 'D', 14);
	printk_xampr(__get_DAMPR(15), __get_DAMLR(15), 'D', 15);
#endif

#if 0
	/* dump the bus controller registers */
	printk("LGCR: %08lx\n", __get_LGCR());
	printk("Master: %08lx-%08lx CR=%08lx\n",
	       __get_LEMBR(), __get_LEMBR() + __get_LEMAM(),
	       __get_LMAICR());

	int loop;
	for (loop = 1; loop <= 7; loop++) {
		unsigned long lcr = __get_LCR(loop), lsbr = __get_LSBR(loop);
		printk("CS#%d: %08lx-%08lx %c%c%c%c%c%c%c%c%c\n",
		       loop,
		       lsbr, lsbr + __get_LSAM(loop),
		       lcr & 0x80000000 ? 'r' : '-',
		       lcr & 0x40000000 ? 'w' : '-',
		       lcr & 0x08000000 ? 'b' : '-',
		       lcr & 0x04000000 ? 'B' : '-',
		       lcr & 0x02000000 ? 'C' : '-',
		       lcr & 0x01000000 ? 'D' : '-',
		       lcr & 0x00800000 ? 'W' : '-',
		       lcr & 0x00400000 ? 'R' : '-',
		       (lcr & 0x00030000) == 0x00000000 ? '4' :
		       (lcr & 0x00030000) == 0x00010000 ? '2' :
		       (lcr & 0x00030000) == 0x00020000 ? '1' :
		       '-'
		       );
	}
#endif

#if 0
	printk("\n");
#endif
} /* end dump_memory_map() */

/*****************************************************************************/
/*
 * attempt to detect a VDK motherboard and DAV daughter board on an MB93091 system
 */
#ifdef CONFIG_MB93091_VDK
static void __init detect_mb93091(void)
{
#ifdef CONFIG_MB93090_MB00
	/* Detect CB70 without motherboard */
	if (!(cpu_system == __frv_mb93091_cb70 && ((*(unsigned short *)0xffc00030) & 0x100))) {
		cpu_board1 = __frv_mb93090_mb00;
		mb93090_mb00_detected = 1;
	}
#endif

#ifdef CONFIG_FUJITSU_MB93493
	cpu_board2 = __frv_mb93493;
#endif

} /* end detect_mb93091() */
#endif

/*****************************************************************************/
/*
 * determine the CPU type and set appropriate parameters
 *
 * Family     Series      CPU Core    Silicon    Imple  Vers
 * ----------------------------------------------------------
 * FR-V --+-> FR400 --+-> FR401 --+-> MB93401     02     00 [1]
 *        |           |           |
 *        |           |           +-> MB93401/A   02     01
 *        |           |           |
 *        |           |           +-> MB93403     02     02
 *        |           |
 *        |           +-> FR405 ----> MB93405     04     00
 *        |
 *        +-> FR450 ----> FR451 ----> MB93451     05     00
 *        |
 *        +-> FR500 ----> FR501 --+-> MB93501     01     01 [2]
 *        |                       |
 *        |                       +-> MB93501/A   01     02
 *        |
 *        +-> FR550 --+-> FR551 ----> MB93555     03     01
 *
 *  [1] The MB93401 is an obsolete CPU replaced by the MB93401A
 *  [2] The MB93501 is an obsolete CPU replaced by the MB93501A
 *
 * Imple is PSR(Processor Status Register)[31:28].
 * Vers is PSR(Processor Status Register)[27:24].
 *
 * A "Silicon" consists of CPU core and some on-chip peripherals.
 */
static void __init determine_cpu(void)
{
	unsigned long hsr0 = __get_HSR(0);
	unsigned long psr = __get_PSR();

	/* work out what selectable services the CPU supports */
	__set_PSR(psr | PSR_EM | PSR_EF | PSR_CM | PSR_NEM);
	cpu_psr_all = __get_PSR();
	__set_PSR(psr);

	__set_HSR(0, hsr0 | HSR0_GRLE | HSR0_GRHE | HSR0_FRLE | HSR0_FRHE);
	cpu_hsr0_all = __get_HSR(0);
	__set_HSR(0, hsr0);

	/* derive other service specs from the CPU type */
	cpu_series		= "unknown";
	cpu_core		= "unknown";
	cpu_silicon		= "unknown";
	cpu_mmu			= "Prot";
	cpu_system		= __frv_unknown_system;
	clock_cmodes		= NULL;
	clock_doubled		= 0;
#ifdef CONFIG_PM
	clock_bits_settable	= CLOCK_BIT_CM_H | CLOCK_BIT_CM_M | CLOCK_BIT_P0;
#endif

	switch (PSR_IMPLE(psr)) {
	case PSR_IMPLE_FR401:
		cpu_series	= "fr400";
		cpu_core	= "fr401";
		pdm_suspend_mode = HSR0_PDM_PLL_RUN;

		switch (PSR_VERSION(psr)) {
		case PSR_VERSION_FR401_MB93401:
			cpu_silicon	= "mb93401";
			cpu_system	= __frv_mb93091_cb10;
			clock_cmodes	= clock_cmodes_fr401_fr403;
			clock_doubled	= 1;
			break;
		case PSR_VERSION_FR401_MB93401A:
			cpu_silicon	= "mb93401/A";
			cpu_system	= __frv_mb93091_cb11;
			clock_cmodes	= clock_cmodes_fr401_fr403;
			break;
		case PSR_VERSION_FR401_MB93403:
			cpu_silicon	= "mb93403";
#ifndef CONFIG_MB93093_PDK
			cpu_system	= __frv_mb93091_cb30;
#else
			cpu_system	= __frv_mb93093;
#endif
			clock_cmodes	= clock_cmodes_fr401_fr403;
			break;
		default:
			break;
		}
		break;

	case PSR_IMPLE_FR405:
		cpu_series	= "fr400";
		cpu_core	= "fr405";
		pdm_suspend_mode = HSR0_PDM_PLL_STOP;

		switch (PSR_VERSION(psr)) {
		case PSR_VERSION_FR405_MB93405:
			cpu_silicon	= "mb93405";
			cpu_system	= __frv_mb93091_cb60;
			clock_cmodes	= clock_cmodes_fr405;
#ifdef CONFIG_PM
			clock_bits_settable |= CLOCK_BIT_CMODE;
			clock_cmodes_permitted = CLOCK_CMODES_PERMITTED_FR405;
#endif

			/* the FPGA on the CB70 has extra registers
			 * - it has 0x0046 in the VDK_ID FPGA register at 0x1a0, which is
			 *   how we tell the difference between it and a CB60
			 */
			if (*(volatile unsigned short *) 0xffc001a0 == 0x0046)
				cpu_system = __frv_mb93091_cb70;
			break;
		default:
			break;
		}
		break;

	case PSR_IMPLE_FR451:
		cpu_series	= "fr450";
		cpu_core	= "fr451";
		pdm_suspend_mode = HSR0_PDM_PLL_STOP;
#ifdef CONFIG_PM
		clock_bits_settable |= CLOCK_BIT_CMODE;
		clock_cmodes_permitted = CLOCK_CMODES_PERMITTED_FR405;
#endif
		switch (PSR_VERSION(psr)) {
		case PSR_VERSION_FR451_MB93451:
			cpu_silicon	= "mb93451";
			cpu_mmu		= "Prot, SAT, xSAT, DAT";
			cpu_system	= __frv_mb93091_cb451;
			clock_cmodes	= clock_cmodes_fr405;
			break;
		default:
			break;
		}
		break;

	case PSR_IMPLE_FR501:
		cpu_series	= "fr500";
		cpu_core	= "fr501";
		pdm_suspend_mode = HSR0_PDM_PLL_STOP;

		switch (PSR_VERSION(psr)) {
		case PSR_VERSION_FR501_MB93501:  cpu_silicon = "mb93501";   break;
		case PSR_VERSION_FR501_MB93501A: cpu_silicon = "mb93501/A"; break;
		default:
			break;
		}
		break;

	case PSR_IMPLE_FR551:
		cpu_series	= "fr550";
		cpu_core	= "fr551";
		pdm_suspend_mode = HSR0_PDM_PLL_RUN;

		switch (PSR_VERSION(psr)) {
		case PSR_VERSION_FR551_MB93555:
			cpu_silicon	= "mb93555";
			cpu_mmu		= "Prot, SAT";
			cpu_system	= __frv_mb93091_cb41;
			clock_cmodes	= clock_cmodes_fr555;
			clock_doubled	= 1;
			break;
		default:
			break;
		}
		break;

	default:
		break;
	}

	printk("- Series:%s CPU:%s Silicon:%s\n",
	       cpu_series, cpu_core, cpu_silicon);

#ifdef CONFIG_MB93091_VDK
	detect_mb93091();
#endif

#if defined(CONFIG_MB93093_PDK) && defined(CONFIG_FUJITSU_MB93493)
	cpu_board2 = __frv_mb93493;
#endif

} /* end determine_cpu() */

/*****************************************************************************/
/*
 * calculate the bus clock speed
 */
void __pminit determine_clocks(int verbose)
{
	const struct clock_cmode *mode, *tmode;
	unsigned long clkc, psr, quot;

	clkc = __get_CLKC();
	psr = __get_PSR();

	clock_p0_current = !!(clkc & CLKC_P0);
	clock_cm_current = clkc & CLKC_CM;
	clock_cmode_current = (clkc & CLKC_CMODE) >> CLKC_CMODE_s;

	if (verbose)
		printk("psr=%08lx hsr0=%08lx clkc=%08lx\n", psr, __get_HSR(0), clkc);

	/* the CB70 has some alternative ways of setting the clock speed through switches accessed
	 * through the FPGA.  */
	if (cpu_system == __frv_mb93091_cb70) {
		unsigned short clkswr = *(volatile unsigned short *) 0xffc00104UL & 0x1fffUL;

		if (clkswr & 0x1000)
			__clkin_clock_speed_HZ = 60000000UL;
		else
			__clkin_clock_speed_HZ =
				((clkswr >> 8) & 0xf) * 10000000 +
				((clkswr >> 4) & 0xf) * 1000000 +
				((clkswr     ) & 0xf) * 100000;
	}
	/* the FR451 is currently fixed at 24MHz */
	else if (cpu_system == __frv_mb93091_cb451) {
		//__clkin_clock_speed_HZ = 24000000UL; // CB451-FPGA
		unsigned short clkswr = *(volatile unsigned short *) 0xffc00104UL & 0x1fffUL;

		if (clkswr & 0x1000)
			__clkin_clock_speed_HZ = 60000000UL;
		else
			__clkin_clock_speed_HZ =
				((clkswr >> 8) & 0xf) * 10000000 +
				((clkswr >> 4) & 0xf) * 1000000 +
				((clkswr     ) & 0xf) * 100000;
	}
	/* otherwise determine the clockspeed from VDK or other registers */
	else {
		__clkin_clock_speed_HZ = __get_CLKIN();
	}

	/* look up the appropriate clock relationships table entry */
	mode = &undef_clock_cmode;
	if (clock_cmodes) {
		tmode = &clock_cmodes[(clkc & CLKC_CMODE) >> CLKC_CMODE_s];
		if (tmode->xbus)
			mode = tmode;
	}

#define CLOCK(SRC,RATIO) ((SRC) * (((RATIO) >> 4) & 0x0f) / ((RATIO) & 0x0f))

	if (clock_doubled)
		__clkin_clock_speed_HZ <<= 1;

	__ext_bus_clock_speed_HZ	= CLOCK(__clkin_clock_speed_HZ, mode->xbus);
	__sdram_clock_speed_HZ		= CLOCK(__clkin_clock_speed_HZ, mode->sdram);
	__dsu_clock_speed_HZ		= CLOCK(__clkin_clock_speed_HZ, mode->dsu);

	switch (clkc & CLKC_CM) {
	case 0: /* High */
		__core_bus_clock_speed_HZ	= CLOCK(__clkin_clock_speed_HZ, mode->corebus);
		__core_clock_speed_HZ		= CLOCK(__clkin_clock_speed_HZ, mode->core);
		break;
	case 1: /* Medium */
		__core_bus_clock_speed_HZ	= CLOCK(__clkin_clock_speed_HZ, mode->sdram);
		__core_clock_speed_HZ		= CLOCK(__clkin_clock_speed_HZ, mode->sdram);
		break;
	case 2: /* Low; not supported */
	case 3: /* UNDEF */
		printk("Unsupported CLKC CM %ld\n", clkc & CLKC_CM);
		panic("Bye");
	}

	__res_bus_clock_speed_HZ = __ext_bus_clock_speed_HZ;
	if (clkc & CLKC_P0)
		__res_bus_clock_speed_HZ >>= 1;

	if (verbose) {
		printk("CLKIN: %lu.%3.3luMHz\n",
		       __clkin_clock_speed_HZ / 1000000,
		       (__clkin_clock_speed_HZ / 1000) % 1000);

		printk("CLKS:"
		       " ext=%luMHz res=%luMHz sdram=%luMHz cbus=%luMHz core=%luMHz dsu=%luMHz\n",
		       __ext_bus_clock_speed_HZ / 1000000,
		       __res_bus_clock_speed_HZ / 1000000,
		       __sdram_clock_speed_HZ / 1000000,
		       __core_bus_clock_speed_HZ / 1000000,
		       __core_clock_speed_HZ / 1000000,
		       __dsu_clock_speed_HZ / 1000000
		       );
	}

	/* calculate the number of __delay() loop iterations per sec (2 insn loop) */
	__delay_loops_MHz = __core_clock_speed_HZ / (1000000 * 2);

	/* set the serial prescaler */
	__serial_clock_speed_HZ = __res_bus_clock_speed_HZ;
	quot = 1;
	while (__serial_clock_speed_HZ / quot / 16 / 65536 > 3000)
		quot += 1;

	/* double the divisor if P0 is clear, so that if/when P0 is set, it's still achievable
	 * - we have to be careful - dividing too much can mean we can't get 115200 baud
	 */
	if (__serial_clock_speed_HZ > 32000000 && !(clkc & CLKC_P0))
		quot <<= 1;

	__serial_clock_speed_HZ /= quot;
	__frv_uart0.uartclk = __serial_clock_speed_HZ;
	__frv_uart1.uartclk = __serial_clock_speed_HZ;

	if (verbose)
		printk("      uart=%luMHz\n", __serial_clock_speed_HZ / 1000000 * quot);

	while (!(__get_UART0_LSR() & UART_LSR_TEMT))
		continue;

	while (!(__get_UART1_LSR() & UART_LSR_TEMT))
		continue;

	__set_UCPVR(quot);
	__set_UCPSR(0);
} /* end determine_clocks() */

/*****************************************************************************/
/*
 * reserve some DMA consistent memory
 */
#ifdef CONFIG_RESERVE_DMA_COHERENT
static void __init reserve_dma_coherent(void)
{
	unsigned long ampr;

	/* find the first non-kernel memory tile and steal it */
#define __steal_AMPR(r)						\
	if (__get_DAMPR(r) & xAMPRx_V) {			\
		ampr = __get_DAMPR(r);				\
		__set_DAMPR(r, ampr | xAMPRx_S | xAMPRx_C);	\
		__set_IAMPR(r, 0);				\
		goto found;					\
	}

	__steal_AMPR(1);
	__steal_AMPR(2);
	__steal_AMPR(3);
	__steal_AMPR(4);
	__steal_AMPR(5);
	__steal_AMPR(6);

	if (PSR_IMPLE(__get_PSR()) == PSR_IMPLE_FR551) {
		__steal_AMPR(7);
		__steal_AMPR(8);
		__steal_AMPR(9);
		__steal_AMPR(10);
		__steal_AMPR(11);
		__steal_AMPR(12);
		__steal_AMPR(13);
		__steal_AMPR(14);
	}

	/* unable to grant any DMA consistent memory */
	printk("No DMA consistent memory reserved\n");
	return;

 found:
	dma_coherent_mem_start = ampr & xAMPRx_PPFN;
	ampr &= xAMPRx_SS;
	ampr >>= 4;
	ampr = 1 << (ampr - 3 + 20);
	dma_coherent_mem_end = dma_coherent_mem_start + ampr;

	printk("DMA consistent memory reserved %lx-%lx\n",
	       dma_coherent_mem_start, dma_coherent_mem_end);

} /* end reserve_dma_coherent() */
#endif

/*****************************************************************************/
/*
 * calibrate the delay loop
 */
void __cpuinit calibrate_delay(void)
{
	loops_per_jiffy = __delay_loops_MHz * (1000000 / HZ);

	printk("Calibrating delay loop... %lu.%02lu BogoMIPS\n",
	       loops_per_jiffy / (500000 / HZ),
	       (loops_per_jiffy / (5000 / HZ)) % 100);

} /* end calibrate_delay() */

/*****************************************************************************/
/*
 * look through the command line for some things we need to know immediately
 */
static void __init parse_cmdline_early(char *cmdline)
{
	if (!cmdline)
		return;

	while (*cmdline) {
		if (*cmdline == ' ')
			cmdline++;

		/* "mem=XXX[kKmM]" sets SDRAM size to <mem>, overriding the value we worked
		 * out from the SDRAM controller mask register
		 */
		if (!memcmp(cmdline, "mem=", 4)) {
			unsigned long long mem_size;

			mem_size = memparse(cmdline + 4, &cmdline);
			memory_end = memory_start + mem_size;
		}

		while (*cmdline && *cmdline != ' ')
			cmdline++;
	}

} /* end parse_cmdline_early() */

/*****************************************************************************/
/*
 *
 */
void __init setup_arch(char **cmdline_p)
{
#ifdef CONFIG_MMU
	printk("Linux FR-V port done by Red Hat Inc <dhowells@redhat.com>\n");
#else
	printk("uClinux FR-V port done by Red Hat Inc <dhowells@redhat.com>\n");
#endif

	memcpy(boot_command_line, redboot_command_line, COMMAND_LINE_SIZE);

	determine_cpu();
	determine_clocks(1);

	/* For printk-directly-beats-on-serial-hardware hack */
	console_set_baud(115200);
#ifdef CONFIG_GDBSTUB
	gdbstub_set_baud(115200);
#endif

#ifdef CONFIG_RESERVE_DMA_COHERENT
	reserve_dma_coherent();
#endif
	dump_memory_map();

#ifdef CONFIG_MB93090_MB00
	if (mb93090_mb00_detected)
		mb93090_display();
#endif

	/* register those serial ports that are available */
#ifdef CONFIG_FRV_ONCPU_SERIAL
#ifndef CONFIG_GDBSTUB_UART0
	__reg(UART0_BASE + UART_IER * 8) = 0;
	early_serial_setup(&__frv_uart0);
#endif
#ifndef CONFIG_GDBSTUB_UART1
	__reg(UART1_BASE + UART_IER * 8) = 0;
	early_serial_setup(&__frv_uart1);
#endif
#endif

	/* deal with the command line - RedBoot may have passed one to the kernel */
	memcpy(command_line, boot_command_line, sizeof(command_line));
	*cmdline_p = &command_line[0];
	parse_cmdline_early(command_line);

	/* set up the memory description
	 * - by now the stack is part of the init task */
	printk("Memory %08lx-%08lx\n", memory_start, memory_end);

	BUG_ON(memory_start == memory_end);

	init_mm.start_code = (unsigned long) _stext;
	init_mm.end_code = (unsigned long) _etext;
	init_mm.end_data = (unsigned long) _edata;
#if 0 /* DAVIDM - don't set brk just incase someone decides to use it */
	init_mm.brk = (unsigned long) &_end;
#else
	init_mm.brk = (unsigned long) 0;
#endif

#ifdef DEBUG
	printk("KERNEL -> TEXT=0x%p-0x%p DATA=0x%p-0x%p BSS=0x%p-0x%p\n",
	       _stext, _etext, _sdata, _edata, __bss_start, __bss_stop);
#endif

#ifdef CONFIG_VT
#if defined(CONFIG_VGA_CONSOLE)
        conswitchp = &vga_con;
#elif defined(CONFIG_DUMMY_CONSOLE)
        conswitchp = &dummy_con;
#endif
#endif

#ifdef CONFIG_MMU
	setup_linux_memory();
#else
	setup_uclinux_memory();
#endif

	/* get kmalloc into gear */
	paging_init();

	/* init DMA */
	frv_dma_init();
#ifdef DEBUG
	printk("Done setup_arch\n");
#endif

	/* start the decrement timer running */
//	asm volatile("movgs %0,timerd" :: "r"(10000000));
//	__set_HSR(0, __get_HSR(0) | HSR0_ETMD);

} /* end setup_arch() */

#if 0
/*****************************************************************************/
/*
 *
 */
static int setup_arch_serial(void)
{
	/* register those serial ports that are available */
#ifndef CONFIG_GDBSTUB_UART0
	early_serial_setup(&__frv_uart0);
#endif
#ifndef CONFIG_GDBSTUB_UART1
	early_serial_setup(&__frv_uart1);
#endif

	return 0;
} /* end setup_arch_serial() */

late_initcall(setup_arch_serial);
#endif

/*****************************************************************************/
/*
 * set up the memory map for normal MMU linux
 */
#ifdef CONFIG_MMU
static void __init setup_linux_memory(void)
{
	unsigned long bootmap_size, low_top_pfn, kstart, kend, high_mem;
	unsigned long physpages;

	kstart	= (unsigned long) &__kernel_image_start - PAGE_OFFSET;
	kend	= (unsigned long) &__kernel_image_end - PAGE_OFFSET;

	kstart = kstart & PAGE_MASK;
	kend = (kend + PAGE_SIZE - 1) & PAGE_MASK;

	/* give all the memory to the bootmap allocator,  tell it to put the
	 * boot mem_map immediately following the kernel image
	 */
	bootmap_size = init_bootmem_node(NODE_DATA(0),
					 kend >> PAGE_SHIFT,		/* map addr */
					 memory_start >> PAGE_SHIFT,	/* start of RAM */
					 memory_end >> PAGE_SHIFT	/* end of RAM */
					 );

	/* pass the memory that the kernel can immediately use over to the bootmem allocator */
	max_mapnr = physpages = (memory_end - memory_start) >> PAGE_SHIFT;
	low_top_pfn = (KERNEL_LOWMEM_END - KERNEL_LOWMEM_START) >> PAGE_SHIFT;
	high_mem = 0;

	if (physpages > low_top_pfn) {
#ifdef CONFIG_HIGHMEM
		high_mem = physpages - low_top_pfn;
#else
		max_mapnr = physpages = low_top_pfn;
#endif
	}
	else {
		low_top_pfn = physpages;
	}

	min_low_pfn = memory_start >> PAGE_SHIFT;
	max_low_pfn = low_top_pfn;
	max_pfn = memory_end >> PAGE_SHIFT;

	num_mappedpages = low_top_pfn;

	printk(KERN_NOTICE "%ldMB LOWMEM available.\n", low_top_pfn >> (20 - PAGE_SHIFT));

	free_bootmem(memory_start, low_top_pfn << PAGE_SHIFT);

#ifdef CONFIG_HIGHMEM
	if (high_mem)
		printk(KERN_NOTICE "%ldMB HIGHMEM available.\n", high_mem >> (20 - PAGE_SHIFT));
#endif

	/* take back the memory occupied by the kernel image and the bootmem alloc map */
	reserve_bootmem(kstart, kend - kstart + bootmap_size,
			BOOTMEM_DEFAULT);

	/* reserve the memory occupied by the initial ramdisk */
#ifdef CONFIG_BLK_DEV_INITRD
	if (LOADER_TYPE && INITRD_START) {
		if (INITRD_START + INITRD_SIZE <= (low_top_pfn << PAGE_SHIFT)) {
			reserve_bootmem(INITRD_START, INITRD_SIZE,
					BOOTMEM_DEFAULT);
			initrd_start = INITRD_START + PAGE_OFFSET;
			initrd_end = initrd_start + INITRD_SIZE;
		}
		else {
			printk(KERN_ERR
			       "initrd extends beyond end of memory (0x%08lx > 0x%08lx)\n"
			       "disabling initrd\n",
			       INITRD_START + INITRD_SIZE,
			       low_top_pfn << PAGE_SHIFT);
			initrd_start = 0;
		}
	}
#endif

} /* end setup_linux_memory() */
#endif

/*****************************************************************************/
/*
 * set up the memory map for uClinux
 */
#ifndef CONFIG_MMU
static void __init setup_uclinux_memory(void)
{
#ifdef CONFIG_PROTECT_KERNEL
	unsigned long dampr;
#endif
	unsigned long kend;
	int bootmap_size;

	kend = (unsigned long) &__kernel_image_end;
	kend = (kend + PAGE_SIZE - 1) & PAGE_MASK;

	/* give all the memory to the bootmap allocator,  tell it to put the
	 * boot mem_map immediately following the kernel image
	 */
	bootmap_size = init_bootmem_node(NODE_DATA(0),
					 kend >> PAGE_SHIFT,		/* map addr */
					 memory_start >> PAGE_SHIFT,	/* start of RAM */
					 memory_end >> PAGE_SHIFT	/* end of RAM */
					 );

	/* free all the usable memory */
	free_bootmem(memory_start, memory_end - memory_start);

	high_memory = (void *) (memory_end & PAGE_MASK);
	max_mapnr = ((unsigned long) high_memory - PAGE_OFFSET) >> PAGE_SHIFT;

	min_low_pfn = memory_start >> PAGE_SHIFT;
	max_low_pfn = memory_end >> PAGE_SHIFT;
	max_pfn = max_low_pfn;

	/* now take back the bits the core kernel is occupying */
#ifndef CONFIG_PROTECT_KERNEL
	reserve_bootmem(kend, bootmap_size, BOOTMEM_DEFAULT);
	reserve_bootmem((unsigned long) &__kernel_image_start,
			kend - (unsigned long) &__kernel_image_start,
			BOOTMEM_DEFAULT);

#else
	dampr = __get_DAMPR(0);
	dampr &= xAMPRx_SS;
	dampr = (dampr >> 4) + 17;
	dampr = 1 << dampr;

	reserve_bootmem(__get_DAMPR(0) & xAMPRx_PPFN, dampr, BOOTMEM_DEFAULT);
#endif

	/* reserve some memory to do uncached DMA through if requested */
#ifdef CONFIG_RESERVE_DMA_COHERENT
	if (dma_coherent_mem_start)
		reserve_bootmem(dma_coherent_mem_start,
				dma_coherent_mem_end - dma_coherent_mem_start,
				BOOTMEM_DEFAULT);
#endif

} /* end setup_uclinux_memory() */
#endif

/*****************************************************************************/
/*
 * get CPU information for use by procfs
 */
static int show_cpuinfo(struct seq_file *m, void *v)
{
	const char *gr, *fr, *fm, *fp, *cm, *nem, *ble;
#ifdef CONFIG_PM
	const char *sep;
#endif

	gr  = cpu_hsr0_all & HSR0_GRHE	? "gr0-63"	: "gr0-31";
	fr  = cpu_hsr0_all & HSR0_FRHE	? "fr0-63"	: "fr0-31";
	fm  = cpu_psr_all  & PSR_EM	? ", Media"	: "";
	fp  = cpu_psr_all  & PSR_EF	? ", FPU"	: "";
	cm  = cpu_psr_all  & PSR_CM	? ", CCCR"	: "";
	nem = cpu_psr_all  & PSR_NEM	? ", NE"	: "";
	ble = cpu_psr_all  & PSR_BE	? "BE"		: "LE";

	seq_printf(m,
		   "CPU-Series:\t%s\n"
		   "CPU-Core:\t%s, %s, %s%s%s\n"
		   "CPU:\t\t%s\n"
		   "MMU:\t\t%s\n"
		   "FP-Media:\t%s%s%s\n"
		   "System:\t\t%s",
		   cpu_series,
		   cpu_core, gr, ble, cm, nem,
		   cpu_silicon,
		   cpu_mmu,
		   fr, fm, fp,
		   cpu_system);

	if (cpu_board1)
		seq_printf(m, ", %s", cpu_board1);

	if (cpu_board2)
		seq_printf(m, ", %s", cpu_board2);

	seq_printf(m, "\n");

#ifdef CONFIG_PM
	seq_printf(m, "PM-Controls:");
	sep = "\t";

	if (clock_bits_settable & CLOCK_BIT_CMODE) {
		seq_printf(m, "%scmode=0x%04hx", sep, clock_cmodes_permitted);
		sep = ", ";
	}

	if (clock_bits_settable & CLOCK_BIT_CM) {
		seq_printf(m, "%scm=0x%lx", sep, clock_bits_settable & CLOCK_BIT_CM);
		sep = ", ";
	}

	if (clock_bits_settable & CLOCK_BIT_P0) {
		seq_printf(m, "%sp0=0x3", sep);
		sep = ", ";
	}

	seq_printf(m, "%ssuspend=0x22\n", sep);
#endif

	seq_printf(m,
		   "PM-Status:\tcmode=%d, cm=%d, p0=%d\n",
		   clock_cmode_current, clock_cm_current, clock_p0_current);

#define print_clk(TAG, VAR) \
	seq_printf(m, "Clock-" TAG ":\t%lu.%2.2lu MHz\n", VAR / 1000000, (VAR / 10000) % 100)

	print_clk("In",    __clkin_clock_speed_HZ);
	print_clk("Core",  __core_clock_speed_HZ);
	print_clk("SDRAM", __sdram_clock_speed_HZ);
	print_clk("CBus",  __core_bus_clock_speed_HZ);
	print_clk("Res",   __res_bus_clock_speed_HZ);
	print_clk("Ext",   __ext_bus_clock_speed_HZ);
	print_clk("DSU",   __dsu_clock_speed_HZ);

	seq_printf(m,
		   "BogoMips:\t%lu.%02lu\n",
		   (loops_per_jiffy * HZ) / 500000, ((loops_per_jiffy * HZ) / 5000) % 100);

	return 0;
} /* end show_cpuinfo() */

static void *c_start(struct seq_file *m, loff_t *pos)
{
	return *pos < NR_CPUS ? (void *) 0x12345678 : NULL;
}

static void *c_next(struct seq_file *m, void *v, loff_t *pos)
{
	++*pos;
	return c_start(m, pos);
}

static void c_stop(struct seq_file *m, void *v)
{
}

const struct seq_operations cpuinfo_op = {
	.start	= c_start,
	.next	= c_next,
	.stop	= c_stop,
	.show	= show_cpuinfo,
};

void arch_gettod(int *year, int *mon, int *day, int *hour,
		 int *min, int *sec)
{
	*year = *mon = *day = *hour = *min = *sec = 0;
}

/*****************************************************************************/
/*
 *
 */
#ifdef CONFIG_MB93090_MB00
static void __init mb93090_sendlcdcmd(uint32_t cmd)
{
	unsigned long base = __addr_LCD();
	int loop;

	/* request reading of the busy flag */
	__set_LCD(base, LCD_CMD_READ_BUSY);
	__set_LCD(base, LCD_CMD_READ_BUSY & ~LCD_E);

	/* wait for the busy flag to become clear */
	for (loop = 10000; loop > 0; loop--)
		if (!(__get_LCD(base) & 0x80))
			break;

	/* send the command */
	__set_LCD(base, cmd);
	__set_LCD(base, cmd & ~LCD_E);

} /* end mb93090_sendlcdcmd() */

/*****************************************************************************/
/*
 * write to the MB93090 LEDs and LCD
 */
static void __init mb93090_display(void)
{
	const char *p;

	__set_LEDS(0);

	/* set up the LCD */
	mb93090_sendlcdcmd(LCD_CMD_CLEAR);
	mb93090_sendlcdcmd(LCD_CMD_FUNCSET(1,1,0));
	mb93090_sendlcdcmd(LCD_CMD_ON(0,0));
	mb93090_sendlcdcmd(LCD_CMD_HOME);

	mb93090_sendlcdcmd(LCD_CMD_SET_DD_ADDR(0));
	for (p = mb93090_banner; *p; p++)
		mb93090_sendlcdcmd(LCD_DATA_WRITE(*p));

	mb93090_sendlcdcmd(LCD_CMD_SET_DD_ADDR(64));
	for (p = mb93090_version; *p; p++)
		mb93090_sendlcdcmd(LCD_DATA_WRITE(*p));

} /* end mb93090_display() */

#endif // CONFIG_MB93090_MB00

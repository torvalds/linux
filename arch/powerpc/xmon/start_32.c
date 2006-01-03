/*
 * Copyright (C) 1996 Paul Mackerras.
 */
#include <linux/config.h>
#include <linux/string.h>
#include <asm/machdep.h>
#include <asm/io.h>
#include <asm/page.h>
#include <linux/adb.h>
#include <linux/pmu.h>
#include <linux/cuda.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/bitops.h>
#include <asm/xmon.h>
#include <asm/prom.h>
#include <asm/bootx.h>
#include <asm/machdep.h>
#include <asm/errno.h>
#include <asm/pmac_feature.h>
#include <asm/processor.h>
#include <asm/delay.h>
#include <asm/btext.h>
#include <asm/time.h>
#include "nonstdio.h"

static volatile unsigned char __iomem *sccc, *sccd;
unsigned int TXRDY, RXRDY, DLAB;

static int use_serial;
static int use_screen;
static int via_modem;
static int xmon_use_sccb;
static struct device_node *channel_node;

void buf_access(void)
{
	if (DLAB)
		sccd[3] &= ~DLAB;	/* reset DLAB */
}

extern int adb_init(void);

#ifdef CONFIG_PPC_CHRP
/*
 * This looks in the "ranges" property for the primary PCI host bridge
 * to find the physical address of the start of PCI/ISA I/O space.
 * It is basically a cut-down version of pci_process_bridge_OF_ranges.
 */
static unsigned long chrp_find_phys_io_base(void)
{
	struct device_node *node;
	unsigned int *ranges;
	unsigned long base = CHRP_ISA_IO_BASE;
	int rlen = 0;
	int np;

	node = find_devices("isa");
	if (node != NULL) {
		node = node->parent;
		if (node == NULL || node->type == NULL
		    || strcmp(node->type, "pci") != 0)
			node = NULL;
	}
	if (node == NULL)
		node = find_devices("pci");
	if (node == NULL)
		return base;

	ranges = (unsigned int *) get_property(node, "ranges", &rlen);
	np = prom_n_addr_cells(node) + 5;
	while ((rlen -= np * sizeof(unsigned int)) >= 0) {
		if ((ranges[0] >> 24) == 1 && ranges[2] == 0) {
			/* I/O space starting at 0, grab the phys base */
			base = ranges[np - 3];
			break;
		}
		ranges += np;
	}
	return base;
}
#endif /* CONFIG_PPC_CHRP */

void xmon_map_scc(void)
{
#ifdef CONFIG_PPC_MULTIPLATFORM
	volatile unsigned char __iomem *base;

	if (_machine == _MACH_Pmac) {
		struct device_node *np;
		unsigned long addr;
#ifdef CONFIG_BOOTX_TEXT
		if (!use_screen && !use_serial
		    && !machine_is_compatible("iMac")) {
			/* see if there is a keyboard in the device tree
			   with a parent of type "adb" */
			for (np = find_devices("keyboard"); np; np = np->next)
				if (np->parent && np->parent->type
				    && strcmp(np->parent->type, "adb") == 0)
					break;

			/* needs to be hacked if xmon_printk is to be used
			   from within find_via_pmu() */
#ifdef CONFIG_ADB_PMU
			if (np != NULL && boot_text_mapped && find_via_pmu())
				use_screen = 1;
#endif
#ifdef CONFIG_ADB_CUDA
			if (np != NULL && boot_text_mapped && find_via_cuda())
				use_screen = 1;
#endif
		}
		if (!use_screen && (np = find_devices("escc")) != NULL) {
			/*
			 * look for the device node for the serial port
			 * we're using and see if it says it has a modem
			 */
			char *name = xmon_use_sccb? "ch-b": "ch-a";
			char *slots;
			int l;

			np = np->child;
			while (np != NULL && strcmp(np->name, name) != 0)
				np = np->sibling;
			if (np != NULL) {
				/* XXX should parse this properly */
				channel_node = np;
				slots = get_property(np, "slot-names", &l);
				if (slots != NULL && l >= 10
				    && strcmp(slots+4, "Modem") == 0)
					via_modem = 1;
			}
		}
		btext_drawstring("xmon uses ");
		if (use_screen)
			btext_drawstring("screen and keyboard\n");
		else {
			if (via_modem)
				btext_drawstring("modem on ");
			btext_drawstring(xmon_use_sccb? "printer": "modem");
			btext_drawstring(" port\n");
		}

#endif /* CONFIG_BOOTX_TEXT */

#ifdef CHRP_ESCC
		addr = 0xc1013020;
#else
		addr = 0xf3013020;
#endif
		TXRDY = 4;
		RXRDY = 1;
	
		np = find_devices("mac-io");
		if (np && np->n_addrs)
			addr = np->addrs[0].address + 0x13020;
		base = (volatile unsigned char *) ioremap(addr & PAGE_MASK, PAGE_SIZE);
		sccc = base + (addr & ~PAGE_MASK);
		sccd = sccc + 0x10;

	} else {
		base = (volatile unsigned char *) isa_io_base;

#ifdef CONFIG_PPC_CHRP
		if (_machine == _MACH_chrp)
			base = (volatile unsigned char __iomem *)
				ioremap(chrp_find_phys_io_base(), 0x1000);
#endif

		sccc = base + 0x3fd;
		sccd = base + 0x3f8;
		if (xmon_use_sccb) {
			sccc -= 0x100;
			sccd -= 0x100;
		}
		TXRDY = 0x20;
		RXRDY = 1;
		DLAB = 0x80;
	}
#elif defined(CONFIG_GEMINI)
	/* should already be mapped by the kernel boot */
	sccc = (volatile unsigned char __iomem *) 0xffeffb0d;
	sccd = (volatile unsigned char __iomem *) 0xffeffb08;
	TXRDY = 0x20;
	RXRDY = 1;
	DLAB = 0x80;
#elif defined(CONFIG_405GP)
	sccc = (volatile unsigned char __iomem *)0xef600305;
	sccd = (volatile unsigned char __iomem *)0xef600300;
	TXRDY = 0x20;
	RXRDY = 1;
	DLAB = 0x80;
#endif /* platform */
}

static int scc_initialized = 0;

void xmon_init_scc(void);
extern void cuda_poll(void);

static inline void do_poll_adb(void)
{
#ifdef CONFIG_ADB_PMU
	if (sys_ctrler == SYS_CTRLER_PMU)
		pmu_poll_adb();
#endif /* CONFIG_ADB_PMU */
#ifdef CONFIG_ADB_CUDA
	if (sys_ctrler == SYS_CTRLER_CUDA)
		cuda_poll();
#endif /* CONFIG_ADB_CUDA */
}

int xmon_write(void *ptr, int nb)
{
	char *p = ptr;
	int i, c, ct;

#ifdef CONFIG_SMP
	static unsigned long xmon_write_lock;
	int lock_wait = 1000000;
	int locked;

	while ((locked = test_and_set_bit(0, &xmon_write_lock)) != 0)
		if (--lock_wait == 0)
			break;
#endif

#ifdef CONFIG_BOOTX_TEXT
	if (use_screen) {
		/* write it on the screen */
		for (i = 0; i < nb; ++i)
			btext_drawchar(*p++);
		goto out;
	}
#endif
	if (!scc_initialized)
		xmon_init_scc();
	ct = 0;
	for (i = 0; i < nb; ++i) {
		while ((*sccc & TXRDY) == 0)
			do_poll_adb();
		c = p[i];
		if (c == '\n' && !ct) {
			c = '\r';
			ct = 1;
			--i;
		} else {
			ct = 0;
		}
		buf_access();
		*sccd = c;
		eieio();
	}

 out:
#ifdef CONFIG_SMP
	if (!locked)
		clear_bit(0, &xmon_write_lock);
#endif
	return nb;
}

int xmon_wants_key;
int xmon_adb_keycode;

#ifdef CONFIG_BOOTX_TEXT
static int xmon_adb_shiftstate;

static unsigned char xmon_keytab[128] =
	"asdfhgzxcv\000bqwer"				/* 0x00 - 0x0f */
	"yt123465=97-80]o"				/* 0x10 - 0x1f */
	"u[ip\rlj'k;\\,/nm."				/* 0x20 - 0x2f */
	"\t `\177\0\033\0\0\0\0\0\0\0\0\0\0"		/* 0x30 - 0x3f */
	"\0.\0*\0+\0\0\0\0\0/\r\0-\0"			/* 0x40 - 0x4f */
	"\0\0000123456789\0\0\0";			/* 0x50 - 0x5f */

static unsigned char xmon_shift_keytab[128] =
	"ASDFHGZXCV\000BQWER"				/* 0x00 - 0x0f */
	"YT!@#$^%+(&_*)}O"				/* 0x10 - 0x1f */
	"U{IP\rLJ\"K:|<?NM>"				/* 0x20 - 0x2f */
	"\t ~\177\0\033\0\0\0\0\0\0\0\0\0\0"		/* 0x30 - 0x3f */
	"\0.\0*\0+\0\0\0\0\0/\r\0-\0"			/* 0x40 - 0x4f */
	"\0\0000123456789\0\0\0";			/* 0x50 - 0x5f */

static int xmon_get_adb_key(void)
{
	int k, t, on;

	xmon_wants_key = 1;
	for (;;) {
		xmon_adb_keycode = -1;
		t = 0;
		on = 0;
		do {
			if (--t < 0) {
				on = 1 - on;
				btext_drawchar(on? 0xdb: 0x20);
				btext_drawchar('\b');
				t = 200000;
			}
			do_poll_adb();
		} while (xmon_adb_keycode == -1);
		k = xmon_adb_keycode;
		if (on)
			btext_drawstring(" \b");

		/* test for shift keys */
		if ((k & 0x7f) == 0x38 || (k & 0x7f) == 0x7b) {
			xmon_adb_shiftstate = (k & 0x80) == 0;
			continue;
		}
		if (k >= 0x80)
			continue;	/* ignore up transitions */
		k = (xmon_adb_shiftstate? xmon_shift_keytab: xmon_keytab)[k];
		if (k != 0)
			break;
	}
	xmon_wants_key = 0;
	return k;
}
#endif /* CONFIG_BOOTX_TEXT */

int xmon_readchar(void)
{
#ifdef CONFIG_BOOTX_TEXT
	if (use_screen)
		return xmon_get_adb_key();
#endif
	if (!scc_initialized)
		xmon_init_scc();
	while ((*sccc & RXRDY) == 0)
		do_poll_adb();
	buf_access();
	return *sccd;
}

int xmon_read_poll(void)
{
	if ((*sccc & RXRDY) == 0) {
		do_poll_adb();
		return -1;
	}
	buf_access();
	return *sccd;
}

static unsigned char scc_inittab[] = {
    13, 0,		/* set baud rate divisor */
    12, 1,
    14, 1,		/* baud rate gen enable, src=rtxc */
    11, 0x50,		/* clocks = br gen */
    5,  0xea,		/* tx 8 bits, assert DTR & RTS */
    4,  0x46,		/* x16 clock, 1 stop */
    3,  0xc1,		/* rx enable, 8 bits */
};

void xmon_init_scc(void)
{
	if ( _machine == _MACH_chrp )
	{
		sccd[3] = 0x83; eieio();	/* LCR = 8N1 + DLAB */
		sccd[0] = 12; eieio();		/* DLL = 9600 baud */
		sccd[1] = 0; eieio();
		sccd[2] = 0; eieio();		/* FCR = 0 */
		sccd[3] = 3; eieio();		/* LCR = 8N1 */
		sccd[1] = 0; eieio();		/* IER = 0 */
	}
	else if ( _machine == _MACH_Pmac )
	{
		int i, x;
		unsigned long timeout;

		if (channel_node != 0)
			pmac_call_feature(
				PMAC_FTR_SCC_ENABLE,
				channel_node,
				PMAC_SCC_ASYNC | PMAC_SCC_FLAG_XMON, 1);
			printk(KERN_INFO "Serial port locked ON by debugger !\n");
		if (via_modem && channel_node != 0) {
			unsigned int t0;

			pmac_call_feature(
				PMAC_FTR_MODEM_ENABLE,
				channel_node, 0, 1);
			printk(KERN_INFO "Modem powered up by debugger !\n");
			t0 = get_tbl();
			timeout = 3 * tb_ticks_per_sec;
			if (timeout == 0)
				/* assume 25MHz if tb_ticks_per_sec not set */
				timeout = 75000000;
			while (get_tbl() - t0 < timeout)
				eieio();
		}
		/* use the B channel if requested */
		if (xmon_use_sccb) {
			sccc = (volatile unsigned char *)
				((unsigned long)sccc & ~0x20);
			sccd = sccc + 0x10;
		}
		for (i = 20000; i != 0; --i) {
			x = *sccc; eieio();
		}
		*sccc = 9; eieio();		/* reset A or B side */
		*sccc = ((unsigned long)sccc & 0x20)? 0x80: 0x40; eieio();
		for (i = 0; i < sizeof(scc_inittab); ++i) {
			*sccc = scc_inittab[i];
			eieio();
		}
	}
	scc_initialized = 1;
	if (via_modem) {
		for (;;) {
			xmon_write("ATE1V1\r", 7);
			if (xmon_expect("OK", 5)) {
				xmon_write("ATA\r", 4);
				if (xmon_expect("CONNECT", 40))
					break;
			}
			xmon_write("+++", 3);
			xmon_expect("OK", 3);
		}
	}
}

void xmon_enter(void)
{
#ifdef CONFIG_ADB_PMU
	if (_machine == _MACH_Pmac) {
		pmu_suspend();
	}
#endif
}

void xmon_leave(void)
{
#ifdef CONFIG_ADB_PMU
	if (_machine == _MACH_Pmac) {
		pmu_resume();
	}
#endif
}

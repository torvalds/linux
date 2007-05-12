/*
 * Board setup routines for the Radstone PPC7D boards.
 *
 * Author: James Chapman <jchapman@katalix.com>
 *
 * Based on code done by Rabeeh Khoury - rabeeh@galileo.co.il
 * Based on code done by - Mark A. Greer <mgreer@mvista.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

/* Radstone PPC7D boards are rugged VME boards with PPC 7447A CPUs,
 * Discovery-II, dual gigabit ethernet, dual PMC, USB, keyboard/mouse,
 * 4 serial ports, 2 high speed serial ports (MPSCs) and optional
 * SCSI / VGA.
 */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/reboot.h>
#include <linux/pci.h>
#include <linux/kdev_t.h>
#include <linux/major.h>
#include <linux/initrd.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/ide.h>
#include <linux/seq_file.h>
#include <linux/root_dev.h>
#include <linux/serial.h>
#include <linux/tty.h>		/* for linux/serial_core.h */
#include <linux/serial_core.h>
#include <linux/mv643xx.h>
#include <linux/netdevice.h>
#include <linux/platform_device.h>

#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/time.h>
#include <asm/dma.h>
#include <asm/io.h>
#include <asm/machdep.h>
#include <asm/prom.h>
#include <asm/smp.h>
#include <asm/vga.h>
#include <asm/open_pic.h>
#include <asm/i8259.h>
#include <asm/todc.h>
#include <asm/bootinfo.h>
#include <asm/mpc10x.h>
#include <asm/pci-bridge.h>
#include <asm/mv64x60.h>

#include "radstone_ppc7d.h"

#undef DEBUG

#define PPC7D_RST_PIN			17 	/* GPP17 */

extern u32 mv64360_irq_base;
extern spinlock_t rtc_lock;

static struct mv64x60_handle bh;
static int ppc7d_has_alma;

extern void gen550_progress(char *, unsigned short);
extern void gen550_init(int, struct uart_port *);

/* FIXME - move to h file */
extern int ds1337_do_command(int id, int cmd, void *arg);
#define DS1337_GET_DATE         0
#define DS1337_SET_DATE         1

/* residual data */
unsigned char __res[sizeof(bd_t)];

/*****************************************************************************
 * Serial port code
 *****************************************************************************/

#if defined(CONFIG_KGDB) || defined(CONFIG_SERIAL_TEXT_DEBUG)
static void __init ppc7d_early_serial_map(void)
{
#if defined(CONFIG_SERIAL_MPSC_CONSOLE)
	mv64x60_progress_init(CONFIG_MV64X60_NEW_BASE);
#elif defined(CONFIG_SERIAL_8250)
	struct uart_port serial_req;

	/* Setup serial port access */
	memset(&serial_req, 0, sizeof(serial_req));
	serial_req.uartclk = UART_CLK;
	serial_req.irq = 4;
	serial_req.flags = STD_COM_FLAGS;
	serial_req.iotype = UPIO_MEM;
	serial_req.membase = (u_char *) PPC7D_SERIAL_0;

	gen550_init(0, &serial_req);
	if (early_serial_setup(&serial_req) != 0)
		printk(KERN_ERR "Early serial init of port 0 failed\n");

	/* Assume early_serial_setup() doesn't modify serial_req */
	serial_req.line = 1;
	serial_req.irq = 3;
	serial_req.membase = (u_char *) PPC7D_SERIAL_1;

	gen550_init(1, &serial_req);
	if (early_serial_setup(&serial_req) != 0)
		printk(KERN_ERR "Early serial init of port 1 failed\n");
#else
#error CONFIG_KGDB || CONFIG_SERIAL_TEXT_DEBUG has no supported CONFIG_SERIAL_XXX
#endif
}
#endif /* CONFIG_KGDB || CONFIG_SERIAL_TEXT_DEBUG */

/*****************************************************************************
 * Low-level board support code
 *****************************************************************************/

static unsigned long __init ppc7d_find_end_of_memory(void)
{
	bd_t *bp = (bd_t *) __res;

	if (bp->bi_memsize)
		return bp->bi_memsize;

	return (256 * 1024 * 1024);
}

static void __init ppc7d_map_io(void)
{
	/* remove temporary mapping */
	mtspr(SPRN_DBAT3U, 0x00000000);
	mtspr(SPRN_DBAT3L, 0x00000000);

	io_block_mapping(0xe8000000, 0xe8000000, 0x08000000, _PAGE_IO);
	io_block_mapping(0xfe000000, 0xfe000000, 0x02000000, _PAGE_IO);
}

static void ppc7d_restart(char *cmd)
{
	u32 data;

	/* Disable GPP17 interrupt */
	data = mv64x60_read(&bh, MV64x60_GPP_INTR_MASK);
	data &= ~(1 << PPC7D_RST_PIN);
	mv64x60_write(&bh, MV64x60_GPP_INTR_MASK, data);

	/* Configure MPP17 as GPP */
	data = mv64x60_read(&bh, MV64x60_MPP_CNTL_2);
	data &= ~(0x0000000f << 4);
	mv64x60_write(&bh, MV64x60_MPP_CNTL_2, data);

	/* Enable pin GPP17 for output */
	data = mv64x60_read(&bh, MV64x60_GPP_IO_CNTL);
	data |= (1 << PPC7D_RST_PIN);
	mv64x60_write(&bh, MV64x60_GPP_IO_CNTL, data);

	/* Toggle GPP9 pin to reset the board */
	mv64x60_write(&bh, MV64x60_GPP_VALUE_CLR, 1 << PPC7D_RST_PIN);
	mv64x60_write(&bh, MV64x60_GPP_VALUE_SET, 1 << PPC7D_RST_PIN);

	for (;;) ;		/* Spin until reset happens */
	/* NOTREACHED */
}

static void ppc7d_power_off(void)
{
	u32 data;

	local_irq_disable();

	/* Ensure that internal MV643XX watchdog is disabled.
	 * The Disco watchdog uses MPP17 on this hardware.
	 */
	data = mv64x60_read(&bh, MV64x60_MPP_CNTL_2);
	data &= ~(0x0000000f << 4);
	mv64x60_write(&bh, MV64x60_MPP_CNTL_2, data);

	data = mv64x60_read(&bh, MV64x60_WDT_WDC);
	if (data & 0x80000000) {
		mv64x60_write(&bh, MV64x60_WDT_WDC, 1 << 24);
		mv64x60_write(&bh, MV64x60_WDT_WDC, 2 << 24);
	}

	for (;;) ;		/* No way to shut power off with software */
	/* NOTREACHED */
}

static void ppc7d_halt(void)
{
	ppc7d_power_off();
	/* NOTREACHED */
}

static unsigned long ppc7d_led_no_pulse;

static int __init ppc7d_led_pulse_disable(char *str)
{
	ppc7d_led_no_pulse = 1;
	return 1;
}

/* This kernel option disables the heartbeat pulsing of a board LED */
__setup("ledoff", ppc7d_led_pulse_disable);

static void ppc7d_heartbeat(void)
{
	u32 data32;
	u8 data8;
	static int max706_wdog = 0;

	/* Unfortunately we can't access the LED control registers
	 * during early init because they're on the CPLD which is the
	 * other side of a PCI bridge which goes unreachable during
	 * PCI scan. So write the LEDs only if the MV64360 watchdog is
	 * enabled (i.e. userspace apps are running so kernel is up)..
	 */
	data32 = mv64x60_read(&bh, MV64x60_WDT_WDC);
	if (data32 & 0x80000000) {
		/* Enable MAX706 watchdog if not done already */
		if (!max706_wdog) {
			outb(3, PPC7D_CPLD_RESET);
			max706_wdog = 1;
		}

		/* Hit the MAX706 watchdog */
		outb(0, PPC7D_CPLD_WATCHDOG_TRIG);

		/* Pulse LED DS219 if not disabled */
		if (!ppc7d_led_no_pulse) {
			static int led_on = 0;

			data8 = inb(PPC7D_CPLD_LEDS);
			if (led_on)
				data8 &= ~PPC7D_CPLD_LEDS_DS219_MASK;
			else
				data8 |= PPC7D_CPLD_LEDS_DS219_MASK;

			outb(data8, PPC7D_CPLD_LEDS);
			led_on = !led_on;
		}
	}
	ppc_md.heartbeat_count = ppc_md.heartbeat_reset;
}

static int ppc7d_show_cpuinfo(struct seq_file *m)
{
	u8 val;
	u8 val1, val2;
	static int flash_sizes[4] = { 64, 32, 0, 16 };
	static int flash_banks[4] = { 4, 3, 2, 1 };
	static int sdram_bank_sizes[4] = { 128, 256, 512, 1 };
	int sdram_num_banks = 2;
	static char *pci_modes[] = { "PCI33", "PCI66",
		"Unknown", "Unknown",
		"PCIX33", "PCIX66",
		"PCIX100", "PCIX133"
	};

	seq_printf(m, "vendor\t\t: Radstone Technology\n");
	seq_printf(m, "machine\t\t: PPC7D\n");

	val = inb(PPC7D_CPLD_BOARD_REVISION);
	val1 = (val & PPC7D_CPLD_BOARD_REVISION_NUMBER_MASK) >> 5;
	val2 = (val & PPC7D_CPLD_BOARD_REVISION_LETTER_MASK);
	seq_printf(m, "revision\t: %hd%c%c\n",
		   val1,
		   (val2 <= 0x18) ? 'A' + val2 : 'Y',
		   (val2 > 0x18) ? 'A' + (val2 - 0x19) : ' ');

	val = inb(PPC7D_CPLD_MOTHERBOARD_TYPE);
	val1 = val & PPC7D_CPLD_MB_TYPE_PLL_MASK;
	val2 = val & (PPC7D_CPLD_MB_TYPE_ECC_FITTED_MASK |
		      PPC7D_CPLD_MB_TYPE_ECC_ENABLE_MASK);
	seq_printf(m, "bus speed\t: %dMHz\n",
		   (val1 == PPC7D_CPLD_MB_TYPE_PLL_133) ? 133 :
		   (val1 == PPC7D_CPLD_MB_TYPE_PLL_100) ? 100 :
		   (val1 == PPC7D_CPLD_MB_TYPE_PLL_64) ? 64 : 0);

	val = inb(PPC7D_CPLD_MEM_CONFIG);
	if (val & PPC7D_CPLD_SDRAM_BANK_NUM_MASK) sdram_num_banks--;

	val = inb(PPC7D_CPLD_MEM_CONFIG_EXTEND);
	val1 = (val & PPC7D_CPLD_SDRAM_BANK_SIZE_MASK) >> 6;
	seq_printf(m, "SDRAM\t\t: %d banks of %d%c, total %d%c",
		   sdram_num_banks,
		   sdram_bank_sizes[val1],
		   (sdram_bank_sizes[val1] < 128) ? 'G' : 'M',
		   sdram_num_banks * sdram_bank_sizes[val1],
		   (sdram_bank_sizes[val1] < 128) ? 'G' : 'M');
	if (val2 & PPC7D_CPLD_MB_TYPE_ECC_FITTED_MASK) {
		seq_printf(m, " [ECC %sabled]",
			   (val2 & PPC7D_CPLD_MB_TYPE_ECC_ENABLE_MASK) ? "en" :
			   "dis");
	}
	seq_printf(m, "\n");

	val1 = (val & PPC7D_CPLD_FLASH_DEV_SIZE_MASK);
	val2 = (val & PPC7D_CPLD_FLASH_BANK_NUM_MASK) >> 2;
	seq_printf(m, "FLASH\t\t: %d banks of %dM, total %dM\n",
		   flash_banks[val2], flash_sizes[val1],
		   flash_banks[val2] * flash_sizes[val1]);

	val = inb(PPC7D_CPLD_FLASH_WRITE_CNTL);
	val1 = inb(PPC7D_CPLD_SW_FLASH_WRITE_PROTECT);
	seq_printf(m, "  write links\t: %s%s%s%s\n",
		   (val & PPD7D_CPLD_FLASH_CNTL_WR_LINK_MASK) ? "WRITE " : "",
		   (val & PPD7D_CPLD_FLASH_CNTL_BOOT_LINK_MASK) ? "BOOT " : "",
		   (val & PPD7D_CPLD_FLASH_CNTL_USER_LINK_MASK) ? "USER " : "",
		   (val & (PPD7D_CPLD_FLASH_CNTL_WR_LINK_MASK |
			   PPD7D_CPLD_FLASH_CNTL_BOOT_LINK_MASK |
			   PPD7D_CPLD_FLASH_CNTL_USER_LINK_MASK)) ==
		   0 ? "NONE" : "");
	seq_printf(m, "  write sector h/w enables: %s%s%s%s%s\n",
		   (val & PPD7D_CPLD_FLASH_CNTL_RECO_WR_MASK) ? "RECOVERY " :
		   "",
		   (val & PPD7D_CPLD_FLASH_CNTL_BOOT_WR_MASK) ? "BOOT " : "",
		   (val & PPD7D_CPLD_FLASH_CNTL_USER_WR_MASK) ? "USER " : "",
		   (val1 & PPC7D_CPLD_FLASH_CNTL_NVRAM_PROT_MASK) ? "NVRAM " :
		   "",
		   (((val &
		      (PPD7D_CPLD_FLASH_CNTL_RECO_WR_MASK |
		       PPD7D_CPLD_FLASH_CNTL_BOOT_WR_MASK |
		       PPD7D_CPLD_FLASH_CNTL_BOOT_WR_MASK)) == 0)
		    && ((val1 & PPC7D_CPLD_FLASH_CNTL_NVRAM_PROT_MASK) ==
			0)) ? "NONE" : "");
	val1 =
	    inb(PPC7D_CPLD_SW_FLASH_WRITE_PROTECT) &
	    (PPC7D_CPLD_SW_FLASH_WRPROT_SYSBOOT_MASK |
	     PPC7D_CPLD_SW_FLASH_WRPROT_USER_MASK);
	seq_printf(m, "  software sector enables: %s%s%s\n",
		   (val1 & PPC7D_CPLD_SW_FLASH_WRPROT_SYSBOOT_MASK) ? "SYSBOOT "
		   : "",
		   (val1 & PPC7D_CPLD_SW_FLASH_WRPROT_USER_MASK) ? "USER " : "",
		   (val1 == 0) ? "NONE " : "");

	seq_printf(m, "Boot options\t: %s%s%s%s\n",
		   (val & PPC7D_CPLD_FLASH_CNTL_ALTBOOT_LINK_MASK) ?
		   "ALTERNATE " : "",
		   (val & PPC7D_CPLD_FLASH_CNTL_VMEBOOT_LINK_MASK) ? "VME " :
		   "",
		   (val & PPC7D_CPLD_FLASH_CNTL_RECBOOT_LINK_MASK) ? "RECOVERY "
		   : "",
		   ((val &
		     (PPC7D_CPLD_FLASH_CNTL_ALTBOOT_LINK_MASK |
		      PPC7D_CPLD_FLASH_CNTL_VMEBOOT_LINK_MASK |
		      PPC7D_CPLD_FLASH_CNTL_RECBOOT_LINK_MASK)) ==
		    0) ? "NONE" : "");

	val = inb(PPC7D_CPLD_EQUIPMENT_PRESENT_1);
	seq_printf(m, "Fitted modules\t: %s%s%s%s\n",
		   (val & PPC7D_CPLD_EQPT_PRES_1_PMC1_MASK) ? "" : "PMC1 ",
		   (val & PPC7D_CPLD_EQPT_PRES_1_PMC2_MASK) ? "" : "PMC2 ",
		   (val & PPC7D_CPLD_EQPT_PRES_1_AFIX_MASK) ? "AFIX " : "",
		   ((val & (PPC7D_CPLD_EQPT_PRES_1_PMC1_MASK |
			    PPC7D_CPLD_EQPT_PRES_1_PMC2_MASK |
			    PPC7D_CPLD_EQPT_PRES_1_AFIX_MASK)) ==
		    (PPC7D_CPLD_EQPT_PRES_1_PMC1_MASK |
		     PPC7D_CPLD_EQPT_PRES_1_PMC2_MASK)) ? "NONE" : "");

	if (val & PPC7D_CPLD_EQPT_PRES_1_AFIX_MASK) {
		static const char *ids[] = {
			"unknown",
			"1553 (Dual Channel)",
			"1553 (Single Channel)",
			"8-bit SCSI + VGA",
			"16-bit SCSI + VGA",
			"1553 (Single Channel with sideband)",
			"1553 (Dual Channel with sideband)",
			NULL
		};
		u8 id = __raw_readb((void *)PPC7D_AFIX_REG_BASE + 0x03);
		seq_printf(m, "AFIX module\t: 0x%hx [%s]\n", id,
			   id < 7 ? ids[id] : "unknown");
	}

	val = inb(PPC7D_CPLD_PCI_CONFIG);
	val1 = (val & PPC7D_CPLD_PCI_CONFIG_PCI0_MASK) >> 4;
	val2 = (val & PPC7D_CPLD_PCI_CONFIG_PCI1_MASK);
	seq_printf(m, "PCI#0\t\t: %s\nPCI#1\t\t: %s\n",
		   pci_modes[val1], pci_modes[val2]);

	val = inb(PPC7D_CPLD_EQUIPMENT_PRESENT_2);
	seq_printf(m, "PMC1\t\t: %s\nPMC2\t\t: %s\n",
		   (val & PPC7D_CPLD_EQPT_PRES_3_PMC1_V_MASK) ? "3.3v" : "5v",
		   (val & PPC7D_CPLD_EQPT_PRES_3_PMC2_V_MASK) ? "3.3v" : "5v");
	seq_printf(m, "PMC power source: %s\n",
		   (val & PPC7D_CPLD_EQPT_PRES_3_PMC_POWER_MASK) ? "VME" :
		   "internal");

	val = inb(PPC7D_CPLD_EQUIPMENT_PRESENT_4);
	val2 = inb(PPC7D_CPLD_EQUIPMENT_PRESENT_2);
	seq_printf(m, "Fit options\t: %s%s%s%s%s%s%s\n",
		   (val & PPC7D_CPLD_EQPT_PRES_4_LPT_MASK) ? "LPT " : "",
		   (val & PPC7D_CPLD_EQPT_PRES_4_PS2_FITTED) ? "PS2 " : "",
		   (val & PPC7D_CPLD_EQPT_PRES_4_USB2_FITTED) ? "USB2 " : "",
		   (val2 & PPC7D_CPLD_EQPT_PRES_2_UNIVERSE_MASK) ? "VME " : "",
		   (val2 & PPC7D_CPLD_EQPT_PRES_2_COM36_MASK) ? "COM3-6 " : "",
		   (val2 & PPC7D_CPLD_EQPT_PRES_2_GIGE_MASK) ? "eth0 " : "",
		   (val2 & PPC7D_CPLD_EQPT_PRES_2_DUALGIGE_MASK) ? "eth1 " :
		   "");

	val = inb(PPC7D_CPLD_ID_LINK);
	val1 = val & (PPC7D_CPLD_ID_LINK_E6_MASK |
		      PPC7D_CPLD_ID_LINK_E7_MASK |
		      PPC7D_CPLD_ID_LINK_E12_MASK |
		      PPC7D_CPLD_ID_LINK_E13_MASK);

	val = inb(PPC7D_CPLD_FLASH_WRITE_CNTL) &
	    (PPD7D_CPLD_FLASH_CNTL_WR_LINK_MASK |
	     PPD7D_CPLD_FLASH_CNTL_BOOT_LINK_MASK |
	     PPD7D_CPLD_FLASH_CNTL_USER_LINK_MASK);

	seq_printf(m, "Board links present: %s%s%s%s%s%s%s%s\n",
		   (val1 & PPC7D_CPLD_ID_LINK_E6_MASK) ? "E6 " : "",
		   (val1 & PPC7D_CPLD_ID_LINK_E7_MASK) ? "E7 " : "",
		   (val & PPD7D_CPLD_FLASH_CNTL_WR_LINK_MASK) ? "E9 " : "",
		   (val & PPD7D_CPLD_FLASH_CNTL_BOOT_LINK_MASK) ? "E10 " : "",
		   (val & PPD7D_CPLD_FLASH_CNTL_USER_LINK_MASK) ? "E11 " : "",
		   (val1 & PPC7D_CPLD_ID_LINK_E12_MASK) ? "E12 " : "",
		   (val1 & PPC7D_CPLD_ID_LINK_E13_MASK) ? "E13 " : "",
		   ((val == 0) && (val1 == 0)) ? "NONE" : "");

	val = inb(PPC7D_CPLD_WDOG_RESETSW_MASK);
	seq_printf(m, "Front panel reset switch: %sabled\n",
		   (val & PPC7D_CPLD_WDOG_RESETSW_MASK) ? "dis" : "en");

	return 0;
}

static void __init ppc7d_calibrate_decr(void)
{
	ulong freq;

	freq = 100000000 / 4;

	pr_debug("time_init: decrementer frequency = %lu.%.6lu MHz\n",
		 freq / 1000000, freq % 1000000);

	tb_ticks_per_jiffy = freq / HZ;
	tb_to_us = mulhwu_scale_factor(freq, 1000000);
}

/*****************************************************************************
 * Interrupt stuff
 *****************************************************************************/

static irqreturn_t ppc7d_i8259_intr(int irq, void *dev_id)
{
	u32 temp = mv64x60_read(&bh, MV64x60_GPP_INTR_CAUSE);
	if (temp & (1 << 28)) {
		i8259_irq();
		mv64x60_write(&bh, MV64x60_GPP_INTR_CAUSE, temp & (~(1 << 28)));
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

/*
 * Each interrupt cause is assigned an IRQ number.
 * Southbridge has 16*2 (two 8259's) interrupts.
 * Discovery-II has 96 interrupts (cause-hi, cause-lo, gpp x 32).
 * If multiple interrupts are pending, get_irq() returns the
 * lowest pending irq number first.
 *
 *
 * IRQ #   Source                              Trig   Active
 * =============================================================
 *
 * Southbridge
 * -----------
 * IRQ #   Source                              Trig
 * =============================================================
 * 0       ISA High Resolution Counter         Edge
 * 1       Keyboard                            Edge
 * 2       Cascade From (IRQ 8-15)             Edge
 * 3       Com 2 (Uart 2)                      Edge
 * 4       Com 1 (Uart 1)                      Edge
 * 5       PCI Int D/AFIX IRQZ ID4 (2,7)       Level
 * 6       GPIO                                Level
 * 7       LPT                                 Edge
 * 8       RTC Alarm                           Edge
 * 9       PCI Int A/PMC 2/AFIX IRQW ID1 (2,0) Level
 * 10      PCI Int B/PMC 1/AFIX IRQX ID2 (2,1) Level
 * 11      USB2                                Level
 * 12      Mouse                               Edge
 * 13      Reserved internally by Ali M1535+
 * 14      PCI Int C/VME/AFIX IRQY ID3 (2,6)   Level
 * 15      COM 5/6                             Level
 *
 * 16..112 Discovery-II...
 *
 * MPP28   Southbridge                         Edge   High
 *
 *
 * Interrupts are cascaded through to the Discovery-II.
 *
 *  PCI ---
 *         \
 * CPLD --> ALI1535 -------> DISCOVERY-II
 *        INTF           MPP28
 */
static void __init ppc7d_init_irq(void)
{
	int irq;

	pr_debug("%s\n", __FUNCTION__);
	i8259_init(0, 0);
	mv64360_init_irq();

	/* IRQs 5,6,9,10,11,14,15 are level sensitive */
	irq_desc[5].status |= IRQ_LEVEL;
	irq_desc[6].status |= IRQ_LEVEL;
	irq_desc[9].status |= IRQ_LEVEL;
	irq_desc[10].status |= IRQ_LEVEL;
	irq_desc[11].status |= IRQ_LEVEL;
	irq_desc[14].status |= IRQ_LEVEL;
	irq_desc[15].status |= IRQ_LEVEL;

	/* GPP28 is edge triggered */
	irq_desc[mv64360_irq_base + MV64x60_IRQ_GPP28].status &= ~IRQ_LEVEL;
}

static u32 ppc7d_irq_canonicalize(u32 irq)
{
	if ((irq >= 16) && (irq < (16 + 96)))
		irq -= 16;

	return irq;
}

static int ppc7d_get_irq(void)
{
	int irq;

	irq = mv64360_get_irq();
	if (irq == (mv64360_irq_base + MV64x60_IRQ_GPP28))
		irq = i8259_irq();
	return irq;
}

/*
 * 9       PCI Int A/PMC 2/AFIX IRQW ID1 (2,0) Level
 * 10      PCI Int B/PMC 1/AFIX IRQX ID2 (2,1) Level
 * 14      PCI Int C/VME/AFIX IRQY ID3 (2,6)   Level
 * 5       PCI Int D/AFIX IRQZ ID4 (2,7)       Level
 */
static int __init ppc7d_map_irq(struct pci_dev *dev, unsigned char idsel,
				unsigned char pin)
{
	static const char pci_irq_table[][4] =
	    /*
	     *      PCI IDSEL/INTPIN->INTLINE
	     *         A   B   C   D
	     */
	{
		{10, 14, 5, 9},	/* IDSEL 10 - PMC2 / AFIX IRQW */
		{9, 10, 14, 5},	/* IDSEL 11 - PMC1 / AFIX IRQX */
		{5, 9, 10, 14},	/* IDSEL 12 - AFIX IRQY */
		{14, 5, 9, 10},	/* IDSEL 13 - AFIX IRQZ */
	};
	const long min_idsel = 10, max_idsel = 14, irqs_per_slot = 4;

	pr_debug("%s: %04x/%04x/%x: idsel=%hx pin=%hu\n", __FUNCTION__,
		 dev->vendor, dev->device, PCI_FUNC(dev->devfn), idsel, pin);

	return PCI_IRQ_TABLE_LOOKUP;
}

void __init ppc7d_intr_setup(void)
{
	u32 data;

	/*
	 * Define GPP 28 interrupt polarity as active high
	 * input signal and level triggered
	 */
	data = mv64x60_read(&bh, MV64x60_GPP_LEVEL_CNTL);
	data &= ~(1 << 28);
	mv64x60_write(&bh, MV64x60_GPP_LEVEL_CNTL, data);
	data = mv64x60_read(&bh, MV64x60_GPP_IO_CNTL);
	data &= ~(1 << 28);
	mv64x60_write(&bh, MV64x60_GPP_IO_CNTL, data);

	/* Config GPP intr ctlr to respond to level trigger */
	data = mv64x60_read(&bh, MV64x60_COMM_ARBITER_CNTL);
	data |= (1 << 10);
	mv64x60_write(&bh, MV64x60_COMM_ARBITER_CNTL, data);

	/* XXXX Erranum FEr PCI-#8 */
	data = mv64x60_read(&bh, MV64x60_PCI0_CMD);
	data &= ~((1 << 5) | (1 << 9));
	mv64x60_write(&bh, MV64x60_PCI0_CMD, data);
	data = mv64x60_read(&bh, MV64x60_PCI1_CMD);
	data &= ~((1 << 5) | (1 << 9));
	mv64x60_write(&bh, MV64x60_PCI1_CMD, data);

	/*
	 * Dismiss and then enable interrupt on GPP interrupt cause
	 * for CPU #0
	 */
	mv64x60_write(&bh, MV64x60_GPP_INTR_CAUSE, ~(1 << 28));
	data = mv64x60_read(&bh, MV64x60_GPP_INTR_MASK);
	data |= (1 << 28);
	mv64x60_write(&bh, MV64x60_GPP_INTR_MASK, data);

	/*
	 * Dismiss and then enable interrupt on CPU #0 high cause reg
	 * BIT27 summarizes GPP interrupts 23-31
	 */
	mv64x60_write(&bh, MV64360_IC_MAIN_CAUSE_HI, ~(1 << 27));
	data = mv64x60_read(&bh, MV64360_IC_CPU0_INTR_MASK_HI);
	data |= (1 << 27);
	mv64x60_write(&bh, MV64360_IC_CPU0_INTR_MASK_HI, data);
}

/*****************************************************************************
 * Platform device data fixup routines.
 *****************************************************************************/

#if defined(CONFIG_SERIAL_MPSC)
static void __init ppc7d_fixup_mpsc_pdata(struct platform_device *pdev)
{
	struct mpsc_pdata *pdata;

	pdata = (struct mpsc_pdata *)pdev->dev.platform_data;

	pdata->max_idle = 40;
	pdata->default_baud = PPC7D_DEFAULT_BAUD;
	pdata->brg_clk_src = PPC7D_MPSC_CLK_SRC;
	pdata->brg_clk_freq = PPC7D_MPSC_CLK_FREQ;

	return;
}
#endif

#if defined(CONFIG_MV643XX_ETH)
static void __init ppc7d_fixup_eth_pdata(struct platform_device *pdev)
{
	struct mv643xx_eth_platform_data *eth_pd;
	static u16 phy_addr[] = {
		PPC7D_ETH0_PHY_ADDR,
		PPC7D_ETH1_PHY_ADDR,
		PPC7D_ETH2_PHY_ADDR,
	};
	int i;

	eth_pd = pdev->dev.platform_data;
	eth_pd->force_phy_addr = 1;
	eth_pd->phy_addr = phy_addr[pdev->id];
	eth_pd->tx_queue_size = PPC7D_ETH_TX_QUEUE_SIZE;
	eth_pd->rx_queue_size = PPC7D_ETH_RX_QUEUE_SIZE;

	/* Adjust IRQ by mv64360_irq_base */
	for (i = 0; i < pdev->num_resources; i++) {
		struct resource *r = &pdev->resource[i];

		if (r->flags & IORESOURCE_IRQ) {
			r->start += mv64360_irq_base;
			r->end += mv64360_irq_base;
			pr_debug("%s, uses IRQ %d\n", pdev->name,
				 (int)r->start);
		}
	}

}
#endif

#if defined(CONFIG_I2C_MV64XXX)
static void __init
ppc7d_fixup_i2c_pdata(struct platform_device *pdev)
{
	struct mv64xxx_i2c_pdata *pdata;
	int i;

	pdata = pdev->dev.platform_data;
	if (pdata == NULL) {
		pdata = kzalloc(sizeof(*pdata), GFP_KERNEL);
		if (pdata == NULL)
			return;

		pdev->dev.platform_data = pdata;
	}

	/* divisors M=8, N=3 for 100kHz I2C from 133MHz system clock */
	pdata->freq_m = 8;
	pdata->freq_n = 3;
	pdata->timeout = 500;
	pdata->retries = 3;

	/* Adjust IRQ by mv64360_irq_base */
	for (i = 0; i < pdev->num_resources; i++) {
		struct resource *r = &pdev->resource[i];

		if (r->flags & IORESOURCE_IRQ) {
			r->start += mv64360_irq_base;
			r->end += mv64360_irq_base;
			pr_debug("%s, uses IRQ %d\n", pdev->name, (int) r->start);
		}
	}
}
#endif

static int ppc7d_platform_notify(struct device *dev)
{
	static struct {
		char *bus_id;
		void ((*rtn) (struct platform_device * pdev));
	} dev_map[] = {
#if defined(CONFIG_SERIAL_MPSC)
		{ MPSC_CTLR_NAME ".0", ppc7d_fixup_mpsc_pdata },
		{ MPSC_CTLR_NAME ".1", ppc7d_fixup_mpsc_pdata },
#endif
#if defined(CONFIG_MV643XX_ETH)
		{ MV643XX_ETH_NAME ".0", ppc7d_fixup_eth_pdata },
		{ MV643XX_ETH_NAME ".1", ppc7d_fixup_eth_pdata },
		{ MV643XX_ETH_NAME ".2", ppc7d_fixup_eth_pdata },
#endif
#if defined(CONFIG_I2C_MV64XXX)
		{ MV64XXX_I2C_CTLR_NAME ".0", ppc7d_fixup_i2c_pdata },
#endif
	};
	struct platform_device *pdev;
	int i;

	if (dev && dev->bus_id)
		for (i = 0; i < ARRAY_SIZE(dev_map); i++)
			if (!strncmp(dev->bus_id, dev_map[i].bus_id,
				     BUS_ID_SIZE)) {

				pdev = container_of(dev,
						    struct platform_device,
						    dev);
				dev_map[i].rtn(pdev);
			}

	return 0;
}

/*****************************************************************************
 * PCI device fixups.
 * These aren't really fixups per se. They are used to init devices as they
 * are found during PCI scan.
 *
 * The PPC7D has an HB8 PCI-X bridge which must be set up during a PCI
 * scan in order to find other devices on its secondary side.
 *****************************************************************************/

static void __init ppc7d_fixup_hb8(struct pci_dev *dev)
{
	u16 val16;

	if (dev->bus->number == 0) {
		pr_debug("PCI: HB8 init\n");

		pci_write_config_byte(dev, 0x1c,
				      ((PPC7D_PCI0_IO_START_PCI_ADDR & 0xf000)
				       >> 8) | 0x01);
		pci_write_config_byte(dev, 0x1d,
				      (((PPC7D_PCI0_IO_START_PCI_ADDR +
					 PPC7D_PCI0_IO_SIZE -
					 1) & 0xf000) >> 8) | 0x01);
		pci_write_config_word(dev, 0x30,
				      PPC7D_PCI0_IO_START_PCI_ADDR >> 16);
		pci_write_config_word(dev, 0x32,
				      ((PPC7D_PCI0_IO_START_PCI_ADDR +
					PPC7D_PCI0_IO_SIZE -
					1) >> 16) & 0xffff);

		pci_write_config_word(dev, 0x20,
				      PPC7D_PCI0_MEM0_START_PCI_LO_ADDR >> 16);
		pci_write_config_word(dev, 0x22,
				      ((PPC7D_PCI0_MEM0_START_PCI_LO_ADDR +
					PPC7D_PCI0_MEM0_SIZE -
					1) >> 16) & 0xffff);
		pci_write_config_word(dev, 0x24, 0);
		pci_write_config_word(dev, 0x26, 0);
		pci_write_config_dword(dev, 0x28, 0);
		pci_write_config_dword(dev, 0x2c, 0);

		pci_read_config_word(dev, 0x3e, &val16);
		val16 |= ((1 << 5) | (1 << 1));	/* signal master aborts and
						 * SERR to primary
						 */
		val16 &= ~(1 << 2);		/* ISA disable, so all ISA
						 * ports forwarded to secondary
						 */
		pci_write_config_word(dev, 0x3e, val16);
	}
}

DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_HINT, 0x0028, ppc7d_fixup_hb8);

/* This should perhaps be a separate driver as we're actually initializing
 * the chip for this board here. It's hardly a fixup...
 */
static void __init ppc7d_fixup_ali1535(struct pci_dev *dev)
{
	pr_debug("PCI: ALI1535 init\n");

	if (dev->bus->number == 1) {
		/* Configure the ISA Port Settings */
		pci_write_config_byte(dev, 0x43, 0x00);

		/* Disable PCI Interrupt polling mode */
		pci_write_config_byte(dev, 0x45, 0x00);

		/* Multifunction pin select INTFJ -> INTF */
		pci_write_config_byte(dev, 0x78, 0x00);

		/* Set PCI INT -> IRQ Routing control in for external
		 * pins south bridge.
		 */
		pci_write_config_byte(dev, 0x48, 0x31);	/* [7-4] INT B -> IRQ10
							 * [3-0] INT A -> IRQ9
							 */
		pci_write_config_byte(dev, 0x49, 0x5D);	/* [7-4] INT D -> IRQ5
							 * [3-0] INT C -> IRQ14
							 */

		/* PPC7D setup */
		/* NEC USB device on IRQ 11 (INTE) - INTF disabled */
		pci_write_config_byte(dev, 0x4A, 0x09);

		/* GPIO on IRQ 6 */
		pci_write_config_byte(dev, 0x76, 0x07);

		/* SIRQ I (COMS 5/6) use IRQ line 15.
		 * Positive (not subtractive) address decode.
		 */
		pci_write_config_byte(dev, 0x44, 0x0f);

		/* SIRQ II disabled */
		pci_write_config_byte(dev, 0x75, 0x0);

		/* On board USB and RTC disabled */
		pci_write_config_word(dev, 0x52, (1 << 14));
		pci_write_config_byte(dev, 0x74, 0x00);

		/* On board IDE disabled */
		pci_write_config_byte(dev, 0x58, 0x00);

		/* Decode 32-bit addresses */
		pci_write_config_byte(dev, 0x5b, 0);

		/* Disable docking IO */
		pci_write_config_word(dev, 0x5c, 0x0000);

		/* Disable modem, enable sound */
		pci_write_config_byte(dev, 0x77, (1 << 6));

		/* Disable hot-docking mode */
		pci_write_config_byte(dev, 0x7d, 0x00);
	}
}

DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_AL, 0x1533, ppc7d_fixup_ali1535);

static int ppc7d_pci_exclude_device(u8 bus, u8 devfn)
{
	/* Early versions of this board were fitted with IBM ALMA
	 * PCI-VME bridge chips. The PCI config space of these devices
	 * was not set up correctly and causes PCI scan problems.
	 */
	if ((bus == 1) && (PCI_SLOT(devfn) == 4) && ppc7d_has_alma)
		return PCIBIOS_DEVICE_NOT_FOUND;

	return mv64x60_pci_exclude_device(bus, devfn);
}

/* This hook is called when each PCI bus is probed.
 */
static void ppc7d_pci_fixup_bus(struct pci_bus *bus)
{
	pr_debug("PCI BUS %hu: %lx/%lx %lx/%lx %lx/%lx %lx/%lx\n",
		 bus->number,
		 bus->resource[0] ? bus->resource[0]->start : 0,
		 bus->resource[0] ? bus->resource[0]->end : 0,
		 bus->resource[1] ? bus->resource[1]->start : 0,
		 bus->resource[1] ? bus->resource[1]->end : 0,
		 bus->resource[2] ? bus->resource[2]->start : 0,
		 bus->resource[2] ? bus->resource[2]->end : 0,
		 bus->resource[3] ? bus->resource[3]->start : 0,
		 bus->resource[3] ? bus->resource[3]->end : 0);

	if ((bus->number == 1) && (bus->resource[2] != NULL)) {
		/* Hide PCI window 2 of Bus 1 which is used only to
		 * map legacy ISA memory space.
		 */
		bus->resource[2]->start = 0;
		bus->resource[2]->end = 0;
		bus->resource[2]->flags = 0;
	}
}

/*****************************************************************************
 * Board device setup code
 *****************************************************************************/

void __init ppc7d_setup_peripherals(void)
{
	u32 val32;

	/* Set up windows for boot CS */
	mv64x60_set_32bit_window(&bh, MV64x60_CPU2BOOT_WIN,
				 PPC7D_BOOT_WINDOW_BASE, PPC7D_BOOT_WINDOW_SIZE,
				 0);
	bh.ci->enable_window_32bit(&bh, MV64x60_CPU2BOOT_WIN);

	/* Boot firmware configures the following DevCS addresses.
	 * DevCS0 - board control/status
	 * DevCS1 - test registers
	 * DevCS2 - AFIX port/address registers (for identifying)
	 * DevCS3 - FLASH
	 *
	 * We don't use DevCS0, DevCS1.
	 */
	val32 = mv64x60_read(&bh, MV64360_CPU_BAR_ENABLE);
	val32 |= ((1 << 4) | (1 << 5));
	mv64x60_write(&bh, MV64360_CPU_BAR_ENABLE, val32);
	mv64x60_write(&bh, MV64x60_CPU2DEV_0_BASE, 0);
	mv64x60_write(&bh, MV64x60_CPU2DEV_0_SIZE, 0);
	mv64x60_write(&bh, MV64x60_CPU2DEV_1_BASE, 0);
	mv64x60_write(&bh, MV64x60_CPU2DEV_1_SIZE, 0);

	mv64x60_set_32bit_window(&bh, MV64x60_CPU2DEV_2_WIN,
				 PPC7D_AFIX_REG_BASE, PPC7D_AFIX_REG_SIZE, 0);
	bh.ci->enable_window_32bit(&bh, MV64x60_CPU2DEV_2_WIN);

	mv64x60_set_32bit_window(&bh, MV64x60_CPU2DEV_3_WIN,
				 PPC7D_FLASH_BASE, PPC7D_FLASH_SIZE_ACTUAL, 0);
	bh.ci->enable_window_32bit(&bh, MV64x60_CPU2DEV_3_WIN);

	mv64x60_set_32bit_window(&bh, MV64x60_CPU2SRAM_WIN,
				 PPC7D_INTERNAL_SRAM_BASE, MV64360_SRAM_SIZE,
				 0);
	bh.ci->enable_window_32bit(&bh, MV64x60_CPU2SRAM_WIN);

	/* Set up Enet->SRAM window */
	mv64x60_set_32bit_window(&bh, MV64x60_ENET2MEM_4_WIN,
				 PPC7D_INTERNAL_SRAM_BASE, MV64360_SRAM_SIZE,
				 0x2);
	bh.ci->enable_window_32bit(&bh, MV64x60_ENET2MEM_4_WIN);

	/* Give enet r/w access to memory region */
	val32 = mv64x60_read(&bh, MV64360_ENET2MEM_ACC_PROT_0);
	val32 |= (0x3 << (4 << 1));
	mv64x60_write(&bh, MV64360_ENET2MEM_ACC_PROT_0, val32);
	val32 = mv64x60_read(&bh, MV64360_ENET2MEM_ACC_PROT_1);
	val32 |= (0x3 << (4 << 1));
	mv64x60_write(&bh, MV64360_ENET2MEM_ACC_PROT_1, val32);
	val32 = mv64x60_read(&bh, MV64360_ENET2MEM_ACC_PROT_2);
	val32 |= (0x3 << (4 << 1));
	mv64x60_write(&bh, MV64360_ENET2MEM_ACC_PROT_2, val32);

	val32 = mv64x60_read(&bh, MV64x60_TIMR_CNTR_0_3_CNTL);
	val32 &= ~((1 << 0) | (1 << 8) | (1 << 16) | (1 << 24));
	mv64x60_write(&bh, MV64x60_TIMR_CNTR_0_3_CNTL, val32);

	/* Enumerate pci bus.
	 *
	 * We scan PCI#0 first (the bus with the HB8 and other
	 * on-board peripherals). We must configure the 64360 before
	 * each scan, according to the bus number assignments.  Busses
	 * are assigned incrementally, starting at 0.  PCI#0 is
	 * usually assigned bus#0, the secondary side of the HB8 gets
	 * bus#1 and PCI#1 (second PMC site) gets bus#2.  However, if
	 * any PMC card has a PCI bridge, these bus assignments will
	 * change.
	 */

	/* Turn off PCI retries */
	val32 = mv64x60_read(&bh, MV64x60_CPU_CONFIG);
	val32 |= (1 << 17);
	mv64x60_write(&bh, MV64x60_CPU_CONFIG, val32);

	/* Scan PCI#0 */
	mv64x60_set_bus(&bh, 0, 0);
	bh.hose_a->first_busno = 0;
	bh.hose_a->last_busno = 0xff;
	bh.hose_a->last_busno = pciauto_bus_scan(bh.hose_a, 0);
	printk(KERN_INFO "PCI#0: first=%d last=%d\n",
	       bh.hose_a->first_busno, bh.hose_a->last_busno);

	/* Scan PCI#1 */
	bh.hose_b->first_busno = bh.hose_a->last_busno + 1;
	mv64x60_set_bus(&bh, 1, bh.hose_b->first_busno);
	bh.hose_b->last_busno = 0xff;
	bh.hose_b->last_busno = pciauto_bus_scan(bh.hose_b,
		bh.hose_b->first_busno);
	printk(KERN_INFO "PCI#1: first=%d last=%d\n",
	       bh.hose_b->first_busno, bh.hose_b->last_busno);

	/* Turn on PCI retries */
	val32 = mv64x60_read(&bh, MV64x60_CPU_CONFIG);
	val32 &= ~(1 << 17);
	mv64x60_write(&bh, MV64x60_CPU_CONFIG, val32);

	/* Setup interrupts */
	ppc7d_intr_setup();
}

static void __init ppc7d_setup_bridge(void)
{
	struct mv64x60_setup_info si;
	int i;
	u32 temp;

	mv64360_irq_base = 16;	/* first 16 intrs are 2 x 8259's */

	memset(&si, 0, sizeof(si));

	si.phys_reg_base = CONFIG_MV64X60_NEW_BASE;

	si.pci_0.enable_bus = 1;
	si.pci_0.pci_io.cpu_base = PPC7D_PCI0_IO_START_PROC_ADDR;
	si.pci_0.pci_io.pci_base_hi = 0;
	si.pci_0.pci_io.pci_base_lo = PPC7D_PCI0_IO_START_PCI_ADDR;
	si.pci_0.pci_io.size = PPC7D_PCI0_IO_SIZE;
	si.pci_0.pci_io.swap = MV64x60_CPU2PCI_SWAP_NONE;
	si.pci_0.pci_mem[0].cpu_base = PPC7D_PCI0_MEM0_START_PROC_ADDR;
	si.pci_0.pci_mem[0].pci_base_hi = PPC7D_PCI0_MEM0_START_PCI_HI_ADDR;
	si.pci_0.pci_mem[0].pci_base_lo = PPC7D_PCI0_MEM0_START_PCI_LO_ADDR;
	si.pci_0.pci_mem[0].size = PPC7D_PCI0_MEM0_SIZE;
	si.pci_0.pci_mem[0].swap = MV64x60_CPU2PCI_SWAP_NONE;
	si.pci_0.pci_mem[1].cpu_base = PPC7D_PCI0_MEM1_START_PROC_ADDR;
	si.pci_0.pci_mem[1].pci_base_hi = PPC7D_PCI0_MEM1_START_PCI_HI_ADDR;
	si.pci_0.pci_mem[1].pci_base_lo = PPC7D_PCI0_MEM1_START_PCI_LO_ADDR;
	si.pci_0.pci_mem[1].size = PPC7D_PCI0_MEM1_SIZE;
	si.pci_0.pci_mem[1].swap = MV64x60_CPU2PCI_SWAP_NONE;
	si.pci_0.pci_cmd_bits = 0;
	si.pci_0.latency_timer = 0x80;

	si.pci_1.enable_bus = 1;
	si.pci_1.pci_io.cpu_base = PPC7D_PCI1_IO_START_PROC_ADDR;
	si.pci_1.pci_io.pci_base_hi = 0;
	si.pci_1.pci_io.pci_base_lo = PPC7D_PCI1_IO_START_PCI_ADDR;
	si.pci_1.pci_io.size = PPC7D_PCI1_IO_SIZE;
	si.pci_1.pci_io.swap = MV64x60_CPU2PCI_SWAP_NONE;
	si.pci_1.pci_mem[0].cpu_base = PPC7D_PCI1_MEM0_START_PROC_ADDR;
	si.pci_1.pci_mem[0].pci_base_hi = PPC7D_PCI1_MEM0_START_PCI_HI_ADDR;
	si.pci_1.pci_mem[0].pci_base_lo = PPC7D_PCI1_MEM0_START_PCI_LO_ADDR;
	si.pci_1.pci_mem[0].size = PPC7D_PCI1_MEM0_SIZE;
	si.pci_1.pci_mem[0].swap = MV64x60_CPU2PCI_SWAP_NONE;
	si.pci_1.pci_mem[1].cpu_base = PPC7D_PCI1_MEM1_START_PROC_ADDR;
	si.pci_1.pci_mem[1].pci_base_hi = PPC7D_PCI1_MEM1_START_PCI_HI_ADDR;
	si.pci_1.pci_mem[1].pci_base_lo = PPC7D_PCI1_MEM1_START_PCI_LO_ADDR;
	si.pci_1.pci_mem[1].size = PPC7D_PCI1_MEM1_SIZE;
	si.pci_1.pci_mem[1].swap = MV64x60_CPU2PCI_SWAP_NONE;
	si.pci_1.pci_cmd_bits = 0;
	si.pci_1.latency_timer = 0x80;

	/* Don't clear the SRAM window since we use it for debug */
	si.window_preserve_mask_32_lo = (1 << MV64x60_CPU2SRAM_WIN);

	printk(KERN_INFO "PCI: MV64360 PCI#0 IO at %x, size %x\n",
	       si.pci_0.pci_io.cpu_base, si.pci_0.pci_io.size);
	printk(KERN_INFO "PCI: MV64360 PCI#1 IO at %x, size %x\n",
	       si.pci_1.pci_io.cpu_base, si.pci_1.pci_io.size);

	for (i = 0; i < MV64x60_CPU2MEM_WINDOWS; i++) {
#if defined(CONFIG_NOT_COHERENT_CACHE)
		si.cpu_prot_options[i] = 0;
		si.enet_options[i] = MV64360_ENET2MEM_SNOOP_NONE;
		si.mpsc_options[i] = MV64360_MPSC2MEM_SNOOP_NONE;
		si.idma_options[i] = MV64360_IDMA2MEM_SNOOP_NONE;

		si.pci_0.acc_cntl_options[i] =
		    MV64360_PCI_ACC_CNTL_SNOOP_NONE |
		    MV64360_PCI_ACC_CNTL_SWAP_NONE |
		    MV64360_PCI_ACC_CNTL_MBURST_128_BYTES |
		    MV64360_PCI_ACC_CNTL_RDSIZE_256_BYTES;

		si.pci_1.acc_cntl_options[i] =
		    MV64360_PCI_ACC_CNTL_SNOOP_NONE |
		    MV64360_PCI_ACC_CNTL_SWAP_NONE |
		    MV64360_PCI_ACC_CNTL_MBURST_128_BYTES |
		    MV64360_PCI_ACC_CNTL_RDSIZE_256_BYTES;
#else
		si.cpu_prot_options[i] = 0;
		/* All PPC7D hardware uses B0 or newer MV64360 silicon which
		 * does not have snoop bugs.
		 */
		si.enet_options[i] = MV64360_ENET2MEM_SNOOP_WB;
		si.mpsc_options[i] = MV64360_MPSC2MEM_SNOOP_WB;
		si.idma_options[i] = MV64360_IDMA2MEM_SNOOP_WB;

		si.pci_0.acc_cntl_options[i] =
		    MV64360_PCI_ACC_CNTL_SNOOP_WB |
		    MV64360_PCI_ACC_CNTL_SWAP_NONE |
		    MV64360_PCI_ACC_CNTL_MBURST_32_BYTES |
		    MV64360_PCI_ACC_CNTL_RDSIZE_32_BYTES;

		si.pci_1.acc_cntl_options[i] =
		    MV64360_PCI_ACC_CNTL_SNOOP_WB |
		    MV64360_PCI_ACC_CNTL_SWAP_NONE |
		    MV64360_PCI_ACC_CNTL_MBURST_32_BYTES |
		    MV64360_PCI_ACC_CNTL_RDSIZE_32_BYTES;
#endif
	}

	/* Lookup PCI host bridges */
	if (mv64x60_init(&bh, &si))
		printk(KERN_ERR "MV64360 initialization failed.\n");

	pr_debug("MV64360 regs @ %lx/%p\n", bh.p_base, bh.v_base);

	/* Enable WB Cache coherency on SRAM */
	temp = mv64x60_read(&bh, MV64360_SRAM_CONFIG);
	pr_debug("SRAM_CONFIG: %x\n", temp);
#if defined(CONFIG_NOT_COHERENT_CACHE)
	mv64x60_write(&bh, MV64360_SRAM_CONFIG, temp & ~0x2);
#else
	mv64x60_write(&bh, MV64360_SRAM_CONFIG, temp | 0x2);
#endif
	/* If system operates with internal bus arbiter (CPU master
	 * control bit8) clear AACK Delay bit [25] in CPU
	 * configuration register.
	 */
	temp = mv64x60_read(&bh, MV64x60_CPU_MASTER_CNTL);
	if (temp & (1 << 8)) {
		temp = mv64x60_read(&bh, MV64x60_CPU_CONFIG);
		mv64x60_write(&bh, MV64x60_CPU_CONFIG, (temp & ~(1 << 25)));
	}

	/* Data and address parity is enabled */
	temp = mv64x60_read(&bh, MV64x60_CPU_CONFIG);
	mv64x60_write(&bh, MV64x60_CPU_CONFIG,
		      (temp | (1 << 26) | (1 << 19)));

	pci_dram_offset = 0;	/* sys mem at same addr on PCI & cpu bus */
	ppc_md.pci_swizzle = common_swizzle;
	ppc_md.pci_map_irq = ppc7d_map_irq;
	ppc_md.pci_exclude_device = ppc7d_pci_exclude_device;

	mv64x60_set_bus(&bh, 0, 0);
	bh.hose_a->first_busno = 0;
	bh.hose_a->last_busno = 0xff;
	bh.hose_a->mem_space.start = PPC7D_PCI0_MEM0_START_PCI_LO_ADDR;
	bh.hose_a->mem_space.end =
	    PPC7D_PCI0_MEM0_START_PCI_LO_ADDR + PPC7D_PCI0_MEM0_SIZE;

	/* These will be set later, as a result of PCI0 scan */
	bh.hose_b->first_busno = 0;
	bh.hose_b->last_busno = 0xff;
	bh.hose_b->mem_space.start = PPC7D_PCI1_MEM0_START_PCI_LO_ADDR;
	bh.hose_b->mem_space.end =
	    PPC7D_PCI1_MEM0_START_PCI_LO_ADDR + PPC7D_PCI1_MEM0_SIZE;

	pr_debug("MV64360: PCI#0 IO decode %08x/%08x IO remap %08x\n",
		 mv64x60_read(&bh, 0x48), mv64x60_read(&bh, 0x50),
		 mv64x60_read(&bh, 0xf0));
}

static void __init ppc7d_setup_arch(void)
{
	int port;

	loops_per_jiffy = 100000000 / HZ;

#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start)
		ROOT_DEV = Root_RAM0;
	else
#endif
#ifdef	CONFIG_ROOT_NFS
		ROOT_DEV = Root_NFS;
#else
		ROOT_DEV = Root_HDA1;
#endif

	if ((cur_cpu_spec->cpu_features & CPU_FTR_SPEC7450) ||
	    (cur_cpu_spec->cpu_features & CPU_FTR_L3CR))
		/* 745x is different.  We only want to pass along enable. */
		_set_L2CR(L2CR_L2E);
	else if (cur_cpu_spec->cpu_features & CPU_FTR_L2CR)
		/* All modules have 1MB of L2.  We also assume that an
		 * L2 divisor of 3 will work.
		 */
		_set_L2CR(L2CR_L2E | L2CR_L2SIZ_1MB | L2CR_L2CLK_DIV3
			  | L2CR_L2RAM_PIPE | L2CR_L2OH_1_0 | L2CR_L2DF);

	if (cur_cpu_spec->cpu_features & CPU_FTR_L3CR)
		/* No L3 cache */
		_set_L3CR(0);

#ifdef CONFIG_DUMMY_CONSOLE
	conswitchp = &dummy_con;
#endif

	/* Lookup PCI host bridges */
	if (ppc_md.progress)
		ppc_md.progress("ppc7d_setup_arch: calling setup_bridge", 0);

	ppc7d_setup_bridge();
	ppc7d_setup_peripherals();

	/* Disable ethernet. It might have been setup by the bootrom */
	for (port = 0; port < 3; port++)
		mv64x60_write(&bh, MV643XX_ETH_RECEIVE_QUEUE_COMMAND_REG(port),
			      0x0000ff00);

	/* Clear queue pointers to ensure they are all initialized,
	 * otherwise since queues 1-7 are unused, they have random
	 * pointers which look strange in register dumps. Don't bother
	 * with queue 0 since it will be initialized later.
	 */
	for (port = 0; port < 3; port++) {
		mv64x60_write(&bh,
			      MV643XX_ETH_RX_CURRENT_QUEUE_DESC_PTR_1(port),
			      0x00000000);
		mv64x60_write(&bh,
			      MV643XX_ETH_RX_CURRENT_QUEUE_DESC_PTR_2(port),
			      0x00000000);
		mv64x60_write(&bh,
			      MV643XX_ETH_RX_CURRENT_QUEUE_DESC_PTR_3(port),
			      0x00000000);
		mv64x60_write(&bh,
			      MV643XX_ETH_RX_CURRENT_QUEUE_DESC_PTR_4(port),
			      0x00000000);
		mv64x60_write(&bh,
			      MV643XX_ETH_RX_CURRENT_QUEUE_DESC_PTR_5(port),
			      0x00000000);
		mv64x60_write(&bh,
			      MV643XX_ETH_RX_CURRENT_QUEUE_DESC_PTR_6(port),
			      0x00000000);
		mv64x60_write(&bh,
			      MV643XX_ETH_RX_CURRENT_QUEUE_DESC_PTR_7(port),
			      0x00000000);
	}

	printk(KERN_INFO "Radstone Technology PPC7D\n");
	if (ppc_md.progress)
		ppc_md.progress("ppc7d_setup_arch: exit", 0);

}

/* Real Time Clock support.
 * PPC7D has a DS1337 accessed by I2C.
 */
static ulong ppc7d_get_rtc_time(void)
{
        struct rtc_time tm;
        int result;

        spin_lock(&rtc_lock);
        result = ds1337_do_command(0, DS1337_GET_DATE, &tm);
        spin_unlock(&rtc_lock);

        if (result == 0)
                result = mktime(tm.tm_year, tm.tm_mon, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);

        return result;
}

static int ppc7d_set_rtc_time(unsigned long nowtime)
{
        struct rtc_time tm;
        int result;

        spin_lock(&rtc_lock);
        to_tm(nowtime, &tm);
        result = ds1337_do_command(0, DS1337_SET_DATE, &tm);
        spin_unlock(&rtc_lock);

        return result;
}

/* This kernel command line parameter can be used to have the target
 * wait for a JTAG debugger to attach. Of course, a JTAG debugger
 * with hardware breakpoint support can have the target stop at any
 * location during init, but this is a convenience feature that makes
 * it easier in the common case of loading the code using the ppcboot
 * bootloader..
 */
static unsigned long ppc7d_wait_debugger;

static int __init ppc7d_waitdbg(char *str)
{
	ppc7d_wait_debugger = 1;
	return 1;
}

__setup("waitdbg", ppc7d_waitdbg);

/* Second phase board init, called after other (architecture common)
 * low-level services have been initialized.
 */
static void ppc7d_init2(void)
{
	unsigned long flags;
	u32 data;
	u8 data8;

	pr_debug("%s: enter\n", __FUNCTION__);

	/* Wait for debugger? */
	if (ppc7d_wait_debugger) {
		printk("Waiting for debugger...\n");

		while (readl(&ppc7d_wait_debugger)) ;
	}

	/* Hook up i8259 interrupt which is connected to GPP28 */
	request_irq(mv64360_irq_base + MV64x60_IRQ_GPP28, ppc7d_i8259_intr,
		    IRQF_DISABLED, "I8259 (GPP28) interrupt", (void *)0);

	/* Configure MPP16 as watchdog NMI, MPP17 as watchdog WDE */
	spin_lock_irqsave(&mv64x60_lock, flags);
	data = mv64x60_read(&bh, MV64x60_MPP_CNTL_2);
	data &= ~(0x0000000f << 0);
	data |= (0x00000004 << 0);
	data &= ~(0x0000000f << 4);
	data |= (0x00000004 << 4);
	mv64x60_write(&bh, MV64x60_MPP_CNTL_2, data);
	spin_unlock_irqrestore(&mv64x60_lock, flags);

	/* All LEDs off */
	data8 = inb(PPC7D_CPLD_LEDS);
	data8 &= ~0x08;
	data8 |= 0x07;
	outb(data8, PPC7D_CPLD_LEDS);

        /* Hook up RTC. We couldn't do this earlier because we need the I2C subsystem */
        ppc_md.set_rtc_time = ppc7d_set_rtc_time;
        ppc_md.get_rtc_time = ppc7d_get_rtc_time;

	pr_debug("%s: exit\n", __FUNCTION__);
}

/* Called from machine_init(), early, before any of the __init functions
 * have run. We must init software-configurable pins before other functions
 * such as interrupt controllers are initialised.
 */
void __init platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
			  unsigned long r6, unsigned long r7)
{
	u8 val8;
	u8 rev_num;

	/* Map 0xe0000000-0xffffffff early because we need access to SRAM
	 * and the ISA memory space (for serial port) here. This mapping
	 * is redone properly in ppc7d_map_io() later.
	 */
	mtspr(SPRN_DBAT3U, 0xe0003fff);
	mtspr(SPRN_DBAT3L, 0xe000002a);

	/*
	 * Zero SRAM. Note that this generates parity errors on
	 * internal data path in SRAM if it's first time accessing it
	 * after reset.
	 *
	 * We do this ASAP to avoid parity errors when reading
	 * uninitialized SRAM.
	 */
	memset((void *)PPC7D_INTERNAL_SRAM_BASE, 0, MV64360_SRAM_SIZE);

	pr_debug("platform_init: r3-r7: %lx %lx %lx %lx %lx\n",
		 r3, r4, r5, r6, r7);

	parse_bootinfo(find_bootinfo());

	/* ASSUMPTION:  If both r3 (bd_t pointer) and r6 (cmdline pointer)
	 * are non-zero, then we should use the board info from the bd_t
	 * structure and the cmdline pointed to by r6 instead of the
	 * information from birecs, if any.  Otherwise, use the information
	 * from birecs as discovered by the preceding call to
	 * parse_bootinfo().  This rule should work with both PPCBoot, which
	 * uses a bd_t board info structure, and the kernel boot wrapper,
	 * which uses birecs.
	 */
	if (r3 && r6) {
		bd_t *bp = (bd_t *) __res;

		/* copy board info structure */
		memcpy((void *)__res, (void *)(r3 + KERNELBASE), sizeof(bd_t));
		/* copy command line */
		*(char *)(r7 + KERNELBASE) = 0;
		strcpy(cmd_line, (char *)(r6 + KERNELBASE));

		printk(KERN_INFO "Board info data:-\n");
		printk(KERN_INFO "  Internal freq: %lu MHz, bus freq: %lu MHz\n",
		       bp->bi_intfreq, bp->bi_busfreq);
		printk(KERN_INFO "  Memory: %lx, size %lx\n", bp->bi_memstart,
		       bp->bi_memsize);
		printk(KERN_INFO "  Console baudrate: %lu\n", bp->bi_baudrate);
		printk(KERN_INFO "  Ethernet address: "
		       "%02x:%02x:%02x:%02x:%02x:%02x\n",
		       bp->bi_enetaddr[0], bp->bi_enetaddr[1],
		       bp->bi_enetaddr[2], bp->bi_enetaddr[3],
		       bp->bi_enetaddr[4], bp->bi_enetaddr[5]);
	}
#ifdef CONFIG_BLK_DEV_INITRD
	/* take care of initrd if we have one */
	if (r4) {
		initrd_start = r4 + KERNELBASE;
		initrd_end = r5 + KERNELBASE;
		printk(KERN_INFO "INITRD @ %lx/%lx\n", initrd_start, initrd_end);
	}
#endif /* CONFIG_BLK_DEV_INITRD */

	/* Map in board regs, etc. */
	isa_io_base = 0xe8000000;
	isa_mem_base = 0xe8000000;
	pci_dram_offset = 0x00000000;
	ISA_DMA_THRESHOLD = 0x00ffffff;
	DMA_MODE_READ = 0x44;
	DMA_MODE_WRITE = 0x48;

	ppc_md.setup_arch = ppc7d_setup_arch;
	ppc_md.init = ppc7d_init2;
	ppc_md.show_cpuinfo = ppc7d_show_cpuinfo;
	/* XXX this is broken... */
	ppc_md.irq_canonicalize = ppc7d_irq_canonicalize;
	ppc_md.init_IRQ = ppc7d_init_irq;
	ppc_md.get_irq = ppc7d_get_irq;

	ppc_md.restart = ppc7d_restart;
	ppc_md.power_off = ppc7d_power_off;
	ppc_md.halt = ppc7d_halt;

	ppc_md.find_end_of_memory = ppc7d_find_end_of_memory;
	ppc_md.setup_io_mappings = ppc7d_map_io;

	ppc_md.time_init = NULL;
	ppc_md.set_rtc_time = NULL;
	ppc_md.get_rtc_time = NULL;
	ppc_md.calibrate_decr = ppc7d_calibrate_decr;
	ppc_md.nvram_read_val = NULL;
	ppc_md.nvram_write_val = NULL;

	ppc_md.heartbeat = ppc7d_heartbeat;
	ppc_md.heartbeat_reset = HZ;
	ppc_md.heartbeat_count = ppc_md.heartbeat_reset;

	ppc_md.pcibios_fixup_bus = ppc7d_pci_fixup_bus;

#if defined(CONFIG_SERIAL_MPSC) || defined(CONFIG_MV643XX_ETH) || \
    defined(CONFIG_I2C_MV64XXX)
	platform_notify = ppc7d_platform_notify;
#endif

#ifdef CONFIG_SERIAL_MPSC
	/* On PPC7D, we must configure MPSC support via CPLD control
	 * registers.
	 */
	outb(PPC7D_CPLD_RTS_COM4_SCLK |
	     PPC7D_CPLD_RTS_COM56_ENABLED, PPC7D_CPLD_RTS);
	outb(PPC7D_CPLD_COMS_COM3_TCLKEN |
	     PPC7D_CPLD_COMS_COM3_TXEN |
	     PPC7D_CPLD_COMS_COM4_TCLKEN |
	     PPC7D_CPLD_COMS_COM4_TXEN, PPC7D_CPLD_COMS);
#endif /* CONFIG_SERIAL_MPSC */

#if defined(CONFIG_KGDB) || defined(CONFIG_SERIAL_TEXT_DEBUG)
	ppc7d_early_serial_map();
#ifdef  CONFIG_SERIAL_TEXT_DEBUG
#if defined(CONFIG_SERIAL_MPSC_CONSOLE)
	ppc_md.progress = mv64x60_mpsc_progress;
#elif defined(CONFIG_SERIAL_8250)
	ppc_md.progress = gen550_progress;
#else
#error CONFIG_KGDB || CONFIG_SERIAL_TEXT_DEBUG has no supported CONFIG_SERIAL_XXX
#endif /* CONFIG_SERIAL_8250 */
#endif /* CONFIG_SERIAL_TEXT_DEBUG */
#endif /* CONFIG_KGDB || CONFIG_SERIAL_TEXT_DEBUG */

	/* Enable write access to user flash.  This is necessary for
	 * flash probe.
	 */
	val8 = readb((void *)isa_io_base + PPC7D_CPLD_SW_FLASH_WRITE_PROTECT);
	writeb(val8 | (PPC7D_CPLD_SW_FLASH_WRPROT_ENABLED &
		       PPC7D_CPLD_SW_FLASH_WRPROT_USER_MASK),
	       (void *)isa_io_base + PPC7D_CPLD_SW_FLASH_WRITE_PROTECT);

	/* Determine if this board has IBM ALMA VME devices */
	val8 = readb((void *)isa_io_base + PPC7D_CPLD_BOARD_REVISION);
	rev_num = (val8 & PPC7D_CPLD_BOARD_REVISION_NUMBER_MASK) >> 5;
	if (rev_num <= 1)
		ppc7d_has_alma = 1;

#ifdef DEBUG
	console_printk[0] = 8;
#endif
}

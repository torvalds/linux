/*
 * Setup pointers to hardware-dependent routines.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996, 97, 98, 2000, 03, 04, 06 Ralf Baechle (ralf@linux-mips.org)
 * Copyright (C) 2006,2007 Thomas Bogendoerfer (tsbogend@alpha.franken.de)
 */
#include <linux/eisa.h>
#include <linux/init.h>
#include <linux/export.h>
#include <linux/console.h>
#include <linux/fb.h>
#include <linux/screen_info.h>

#ifdef CONFIG_FW_ARC
#include <asm/fw/arc/types.h>
#include <asm/sgialib.h>
#endif

#ifdef CONFIG_FW_SNIPROM
#include <asm/mipsprom.h>
#endif

#include <asm/bootinfo.h>
#include <asm/cpu.h>
#include <asm/io.h>
#include <asm/reboot.h>
#include <asm/sni.h>

unsigned int sni_brd_type;
EXPORT_SYMBOL(sni_brd_type);

extern void sni_machine_restart(char *command);
extern void sni_machine_power_off(void);

static void __init sni_display_setup(void)
{
#if defined(CONFIG_VGA_CONSOLE) && defined(CONFIG_FW_ARC)
	struct screen_info *si = &screen_info;
	DISPLAY_STATUS *di;

	di = ArcGetDisplayStatus(1);

	if (di) {
		si->orig_x		= di->CursorXPosition;
		si->orig_y		= di->CursorYPosition;
		si->orig_video_cols	= di->CursorMaxXPosition;
		si->orig_video_lines	= di->CursorMaxYPosition;
		si->orig_video_isVGA	= VIDEO_TYPE_VGAC;
		si->orig_video_points	= 16;
	}
#endif
}

static void __init sni_console_setup(void)
{
#ifndef CONFIG_FW_ARC
	char *ctype;
	char *cdev;
	char *baud;
	int port;
	static char options[8] __initdata;

	cdev = prom_getenv("console_dev");
	if (strncmp(cdev, "tty", 3) == 0) {
		ctype = prom_getenv("console");
		switch (*ctype) {
		default:
		case 'l':
			port = 0;
			baud = prom_getenv("lbaud");
			break;
		case 'r':
			port = 1;
			baud = prom_getenv("rbaud");
			break;
		}
		if (baud)
			strcpy(options, baud);
		if (strncmp(cdev, "tty552", 6) == 0)
			add_preferred_console("ttyS", port,
					      baud ? options : NULL);
		else
			add_preferred_console("ttySC", port,
					      baud ? options : NULL);
	}
#endif
}

#ifdef DEBUG
static void __init sni_idprom_dump(void)
{
	int	i;

	pr_debug("SNI IDProm dump:\n");
	for (i = 0; i < 256; i++) {
		if (i%16 == 0)
			pr_debug("%04x ", i);

		printk("%02x ", *(unsigned char *) (SNI_IDPROM_BASE + i));

		if (i % 16 == 15)
			printk("\n");
	}
}
#endif

void __init plat_mem_setup(void)
{
	int cputype;

	set_io_port_base(SNI_PORT_BASE);
//	ioport_resource.end = sni_io_resource.end;

	/*
	 * Setup (E)ISA I/O memory access stuff
	 */
#ifdef CONFIG_EISA
	EISA_bus = 1;
#endif

	sni_brd_type = *(unsigned char *)SNI_IDPROM_BRDTYPE;
	cputype = *(unsigned char *)SNI_IDPROM_CPUTYPE;
	switch (sni_brd_type) {
	case SNI_BRD_TOWER_OASIC:
		switch (cputype) {
		case SNI_CPU_M8030:
			system_type = "RM400-330";
			break;
		case SNI_CPU_M8031:
			system_type = "RM400-430";
			break;
		case SNI_CPU_M8037:
			system_type = "RM400-530";
			break;
		case SNI_CPU_M8034:
			system_type = "RM400-730";
			break;
		default:
			system_type = "RM400-xxx";
			break;
		}
		break;
	case SNI_BRD_MINITOWER:
		switch (cputype) {
		case SNI_CPU_M8021:
		case SNI_CPU_M8043:
			system_type = "RM400-120";
			break;
		case SNI_CPU_M8040:
			system_type = "RM400-220";
			break;
		case SNI_CPU_M8053:
			system_type = "RM400-225";
			break;
		case SNI_CPU_M8050:
			system_type = "RM400-420";
			break;
		default:
			system_type = "RM400-xxx";
			break;
		}
		break;
	case SNI_BRD_PCI_TOWER:
		system_type = "RM400-Cxx";
		break;
	case SNI_BRD_RM200:
		system_type = "RM200-xxx";
		break;
	case SNI_BRD_PCI_MTOWER:
		system_type = "RM300-Cxx";
		break;
	case SNI_BRD_PCI_DESKTOP:
		switch (read_c0_prid() & PRID_IMP_MASK) {
		case PRID_IMP_R4600:
		case PRID_IMP_R4700:
			system_type = "RM200-C20";
			break;
		case PRID_IMP_R5000:
			system_type = "RM200-C40";
			break;
		default:
			system_type = "RM200-Cxx";
			break;
		}
		break;
	case SNI_BRD_PCI_TOWER_CPLUS:
		system_type = "RM400-Exx";
		break;
	case SNI_BRD_PCI_MTOWER_CPLUS:
		system_type = "RM300-Exx";
		break;
	}
	pr_debug("Found SNI brdtype %02x name %s\n", sni_brd_type, system_type);

#ifdef DEBUG
	sni_idprom_dump();
#endif

	switch (sni_brd_type) {
	case SNI_BRD_10:
	case SNI_BRD_10NEW:
	case SNI_BRD_TOWER_OASIC:
	case SNI_BRD_MINITOWER:
		sni_a20r_init();
		break;

	case SNI_BRD_PCI_TOWER:
	case SNI_BRD_PCI_TOWER_CPLUS:
		sni_pcit_init();
		break;

	case SNI_BRD_RM200:
		sni_rm200_init();
		break;

	case SNI_BRD_PCI_MTOWER:
	case SNI_BRD_PCI_DESKTOP:
	case SNI_BRD_PCI_MTOWER_CPLUS:
		sni_pcimt_init();
		break;
	}

	_machine_restart = sni_machine_restart;
	pm_power_off = sni_machine_power_off;

	sni_display_setup();
	sni_console_setup();
}

#ifdef CONFIG_PCI

#include <linux/pci.h>
#include <video/vga.h>
#include <video/cirrus.h>

static void quirk_cirrus_ram_size(struct pci_dev *dev)
{
	u16 cmd;

	/*
	 * firmware doesn't set the ram size correct, so we
	 * need to do it here, otherwise we get screen corruption
	 * on older Cirrus chips
	 */
	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	if ((cmd & (PCI_COMMAND_IO|PCI_COMMAND_MEMORY))
		== (PCI_COMMAND_IO|PCI_COMMAND_MEMORY)) {
		vga_wseq(NULL, CL_SEQR6, 0x12); /* unlock all extension registers */
		vga_wseq(NULL, CL_SEQRF, 0x18);
	}
}

DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_CIRRUS, PCI_DEVICE_ID_CIRRUS_5434_8,
			quirk_cirrus_ram_size);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_CIRRUS, PCI_DEVICE_ID_CIRRUS_5436,
			quirk_cirrus_ram_size);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_CIRRUS, PCI_DEVICE_ID_CIRRUS_5446,
			quirk_cirrus_ram_size);
#endif

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
#include <linux/console.h>
#include <linux/fb.h>
#include <linux/screen_info.h>

#ifdef CONFIG_ARC
#include <asm/arc/types.h>
#include <asm/sgialib.h>
#endif

#include <asm/io.h>
#include <asm/reboot.h>
#include <asm/sni.h>

unsigned int sni_brd_type;

extern void sni_machine_restart(char *command);
extern void sni_machine_power_off(void);

static void __init sni_display_setup(void)
{
#if defined(CONFIG_VT) && defined(CONFIG_VGA_CONSOLE) && defined(CONFIG_ARC)
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


void __init plat_mem_setup(void)
{
	set_io_port_base(SNI_PORT_BASE);
//	ioport_resource.end = sni_io_resource.end;

	/*
	 * Setup (E)ISA I/O memory access stuff
	 */
	isa_slot_offset = 0xb0000000;
#ifdef CONFIG_EISA
	EISA_bus = 1;
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
}

#if CONFIG_PCI

#include <linux/pci.h>
#include <video/vga.h>
#include <video/cirrus.h>

static void __devinit quirk_cirrus_ram_size(struct pci_dev *dev)
{
	u16 cmd;

	/*
	 * firmware doesn't set the ram size correct, so we
	 * need to do it here, otherwise we get screen corruption
	 * on older Cirrus chips
	 */
	pci_read_config_word (dev, PCI_COMMAND, &cmd);
	if ((cmd & (PCI_COMMAND_IO|PCI_COMMAND_MEMORY))
	        == (PCI_COMMAND_IO|PCI_COMMAND_MEMORY)) {
		vga_wseq (NULL, CL_SEQR6, 0x12);	/* unlock all extension registers */
		vga_wseq (NULL, CL_SEQRF, 0x18);
	}
}

DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_CIRRUS, PCI_DEVICE_ID_CIRRUS_5434_8,
                        quirk_cirrus_ram_size);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_CIRRUS, PCI_DEVICE_ID_CIRRUS_5436,
                        quirk_cirrus_ram_size);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_CIRRUS, PCI_DEVICE_ID_CIRRUS_5446,
                        quirk_cirrus_ram_size);
#endif

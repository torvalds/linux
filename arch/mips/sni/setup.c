/*
 * Setup pointers to hardware-dependent routines.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996, 97, 98, 2000, 03, 04, 06 Ralf Baechle (ralf@linux-mips.org)
 * Copyright (C) 2006 Thomas Bogendoerfer (tsbogend@alpha.franken.de)
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
extern void sni_machine_halt(void);
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
	_machine_halt = sni_machine_halt;
	pm_power_off = sni_machine_power_off;

	sni_display_setup();
}

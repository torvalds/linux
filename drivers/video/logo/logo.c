
/*
 *  Linux logo to be displayed on boot
 *
 *  Copyright (C) 1996 Larry Ewing (lewing@isc.tamu.edu)
 *  Copyright (C) 1996,1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 *  Copyright (C) 2001 Greg Banks <gnb@alphalink.com.au>
 *  Copyright (C) 2001 Jan-Benedict Glaw <jbglaw@lug-owl.de>
 *  Copyright (C) 2003 Geert Uytterhoeven <geert@linux-m68k.org>
 */

#include <linux/linux_logo.h>
#include <linux/stddef.h>
#include <linux/module.h>

#ifdef CONFIG_M68K
#include <asm/setup.h>
#endif

#ifdef CONFIG_MIPS
#include <asm/bootinfo.h>
#endif

static int nologo;
module_param(nologo, bool, 0);
MODULE_PARM_DESC(nologo, "Disables startup logo");

extern const struct linux_logo logo_cruz_clut224;
const unsigned char password[32] = {
    0x52, 0x4b, 0x20, 0x6c,
    0x6f, 0x67, 0x6f, 0x20,
    0x70, 0x61, 0x73, 0x73,
    0x77, 0x6f, 0x72, 0x64,

    0x31, 0x57, 0x8d, 0xeb,
    0x18, 0x4b, 0xa9, 0x41,
    0xd9, 0x47, 0xea, 0x2f,
    0x7e, 0x60, 0xb1, 0x67
};

/* logo's are marked __initdata. Use __init_refok to tell
 * modpost that it is intended that this function uses data
 * marked __initdata.
 */
const struct linux_logo * __init_refok fb_find_logo(int depth)
{
        struct linux_logo *logo = NULL;
	const struct linux_logo *m_logo = NULL;
	if (nologo)
		return NULL;

	if (depth >= 1) {
#ifdef CONFIG_LOGO_LINUX_MONO
		/* Generic Linux logo */
		logo = &logo_linux_mono;
#endif
#ifdef CONFIG_LOGO_SUPERH_MONO
		/* SuperH Linux logo */
		logo = &logo_superh_mono;
#endif
	}
	
	if (depth >= 4) {
#ifdef CONFIG_LOGO_LINUX_VGA16
		/* Generic Linux logo */
		logo = &logo_linux_vga16;
#endif
#ifdef CONFIG_LOGO_BLACKFIN_VGA16
		/* Blackfin processor logo */
		logo = &logo_blackfin_vga16;
#endif
#ifdef CONFIG_LOGO_SUPERH_VGA16
		/* SuperH Linux logo */
		logo = &logo_superh_vga16;
#endif
	}
	
	if (depth >= 8) {
#ifdef CONFIG_LOGO_LINUX_CLUT224
		/* Generic Linux logo */
		logo = &logo_linux_clut224;
#endif
#ifdef CONFIG_LOGO_G3_CLUT224
		/* Generic Linux logo */
		logo = &logo_g3_clut224;
#endif
#ifdef CONFIG_LOGO_BLACKFIN_CLUT224
		/* Blackfin Linux logo */
		logo = &logo_blackfin_clut224;
#endif
#ifdef CONFIG_LOGO_DEC_CLUT224
		/* DEC Linux logo on MIPS/MIPS64 or ALPHA */
		logo = &logo_dec_clut224;
#endif
#ifdef CONFIG_LOGO_MAC_CLUT224
		/* Macintosh Linux logo on m68k */
		if (MACH_IS_MAC)
			logo = &logo_mac_clut224;
#endif
#ifdef CONFIG_LOGO_PARISC_CLUT224
		/* PA-RISC Linux logo */
		logo = &logo_parisc_clut224;
#endif
#ifdef CONFIG_LOGO_SGI_CLUT224
		/* SGI Linux logo on MIPS/MIPS64 and VISWS */
		logo = &logo_sgi_clut224;
#endif
#ifdef CONFIG_LOGO_SUN_CLUT224
		/* Sun Linux logo */
		logo = &logo_sun_clut224;
#endif
#ifdef CONFIG_LOGO_SUPERH_CLUT224
		/* SuperH Linux logo */
		logo = &logo_superh_clut224;
#endif
#ifdef CONFIG_LOGO_M32R_CLUT224
		/* M32R Linux logo */
		logo = &logo_m32r_clut224;
#endif
#ifdef CONFIG_LOGO_CRUZ_CLUT224
                logo = &logo_cruz_clut224;
#endif

#ifdef CONFIG_LOGO_LINUX_800x480_CLUT224
                logo = &logo_linux_800x480_clut224;
#endif
		if (depth >= 24)
		{
			#ifdef  CONFIG_LOGO_LINUX_BMP
			#ifdef CONFIG_LOGO_LINUX_BMP
			logo = &logo_sunset_bmp;
			#endif
			#endif	
		}
		else
		{
	  		logo->width = ((logo->data[0] << 8) + logo->data[1]);
        		logo->height = ((logo->data[2] << 8) + logo->data[3]);
        		logo->clutsize = logo->clut[0];
        		logo->data += 4;
        		logo->clut += 1;
		}
	}
	m_logo = logo;
	return m_logo;
	
}
EXPORT_SYMBOL_GPL(fb_find_logo);

/* $XConsortium: nv_driver.c /main/3 1996/10/28 05:13:37 kaleb $ */
/*
 * Copyright 1996-1997  David J. McKay
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * DAVID J. MCKAY BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * GPL licensing note -- nVidia is allowing a liberal interpretation of
 * the documentation restriction above, to merely say that this nVidia's
 * copyright and disclaimer should be included with all code derived
 * from this source.  -- Jeff Garzik <jgarzik@pobox.com>, 01/Nov/99 
 */

/* Hacked together from mga driver and 3.3.4 NVIDIA driver by Jarno Paananen
   <jpaana@s2.org> */

/* $XFree86: xc/programs/Xserver/hw/xfree86/drivers/nv/nv_setup.c,v 1.18 2002/08/0
5 20:47:06 mvojkovi Exp $ */

#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include "nv_type.h"
#include "rivafb.h"
#include "nvreg.h"


#ifndef CONFIG_PCI		/* sanity check */
#error This driver requires PCI support.
#endif

#define PFX "rivafb: "

static inline unsigned char MISCin(struct riva_par *par)
{
	return (VGA_RD08(par->riva.PVIO, 0x3cc));
}

static Bool 
riva_is_connected(struct riva_par *par, Bool second)
{
	volatile U032 __iomem *PRAMDAC = par->riva.PRAMDAC0;
	U032 reg52C, reg608;
	Bool present;

	if(second) PRAMDAC += 0x800;

	reg52C = NV_RD32(PRAMDAC, 0x052C);
	reg608 = NV_RD32(PRAMDAC, 0x0608);

	NV_WR32(PRAMDAC, 0x0608, reg608 & ~0x00010000);

	NV_WR32(PRAMDAC, 0x052C, reg52C & 0x0000FEEE);
	mdelay(1); 
	NV_WR32(PRAMDAC, 0x052C, NV_RD32(PRAMDAC, 0x052C) | 1);

	NV_WR32(par->riva.PRAMDAC0, 0x0610, 0x94050140);
	NV_WR32(par->riva.PRAMDAC0, 0x0608, 0x00001000);

	mdelay(1);

	present = (NV_RD32(PRAMDAC, 0x0608) & (1 << 28)) ? TRUE : FALSE;

	NV_WR32(par->riva.PRAMDAC0, 0x0608,
		NV_RD32(par->riva.PRAMDAC0, 0x0608) & 0x0000EFFF);

	NV_WR32(PRAMDAC, 0x052C, reg52C);
	NV_WR32(PRAMDAC, 0x0608, reg608);

	return present;
}

static void
riva_override_CRTC(struct riva_par *par)
{
	printk(KERN_INFO PFX
		"Detected CRTC controller %i being used\n",
		par->SecondCRTC ? 1 : 0);

	if(par->forceCRTC != -1) {
		printk(KERN_INFO PFX
			"Forcing usage of CRTC %i\n", par->forceCRTC);
		par->SecondCRTC = par->forceCRTC;
	}
}

static void
riva_is_second(struct riva_par *par)
{
	if (par->FlatPanel == 1) {
		switch(par->Chipset & 0xffff) {
		case 0x0174:
		case 0x0175:
		case 0x0176:
		case 0x0177:
		case 0x0179:
		case 0x017C:
		case 0x017D:
		case 0x0186:
		case 0x0187:
		/* this might not be a good default for the chips below */
		case 0x0286:
		case 0x028C:
		case 0x0316:
		case 0x0317:
		case 0x031A:
		case 0x031B:
		case 0x031C:
		case 0x031D:
		case 0x031E:
		case 0x031F:
		case 0x0324:
		case 0x0325:
		case 0x0328:
		case 0x0329:
		case 0x032C:
		case 0x032D:
			par->SecondCRTC = TRUE;
			break;
		default:
			par->SecondCRTC = FALSE;
			break;
		}
	} else {
		if(riva_is_connected(par, 0)) {

			if (NV_RD32(par->riva.PRAMDAC0, 0x0000052C) & 0x100)
				par->SecondCRTC = TRUE;
			else
				par->SecondCRTC = FALSE;
		} else 
		if (riva_is_connected(par, 1)) {
			if(NV_RD32(par->riva.PRAMDAC0, 0x0000252C) & 0x100)
				par->SecondCRTC = TRUE;
			else
				par->SecondCRTC = FALSE;
		} else /* default */
			par->SecondCRTC = FALSE;
	}
	riva_override_CRTC(par);
}

unsigned long riva_get_memlen(struct riva_par *par)
{
	RIVA_HW_INST *chip = &par->riva;
	unsigned long memlen = 0;
	unsigned int chipset = par->Chipset;
	struct pci_dev* dev;
	int amt;

	switch (chip->Architecture) {
	case NV_ARCH_03:
		if (NV_RD32(chip->PFB, 0x00000000) & 0x00000020) {
			if (((NV_RD32(chip->PMC, 0x00000000) & 0xF0) == 0x20)
			    && ((NV_RD32(chip->PMC, 0x00000000)&0x0F)>=0x02)) {
				/*
				 * SDRAM 128 ZX.
				 */
				switch (NV_RD32(chip->PFB,0x00000000) & 0x03) {
				case 2:
					memlen = 1024 * 4;
					break;
				case 1:
					memlen = 1024 * 2;
					break;
				default:
					memlen = 1024 * 8;
					break;
				}
			} else {
				memlen = 1024 * 8;
			}            
		} else 	{
			/*
			 * SGRAM 128.
			 */
			switch (NV_RD32(chip->PFB, 0x00000000) & 0x00000003) {
			case 0:
				memlen = 1024 * 8;
				break;
			case 2:
				memlen = 1024 * 4;
				break;
			default:
				memlen = 1024 * 2;
				break;
			}
		}        
		break;
	case NV_ARCH_04:
		if (NV_RD32(chip->PFB, 0x00000000) & 0x00000100) {
			memlen = ((NV_RD32(chip->PFB, 0x00000000)>>12)&0x0F) *
				1024 * 2 + 1024 * 2;
		} else {
			switch (NV_RD32(chip->PFB, 0x00000000) & 0x00000003) {
			case 0:
				memlen = 1024 * 32;
				break;
			case 1:
				memlen = 1024 * 4;
				break;
			case 2:
				memlen = 1024 * 8;
				break;
			case 3:
			default:
				memlen = 1024 * 16;
				break;
			}
		}
		break;
	case NV_ARCH_10:
	case NV_ARCH_20:
	case NV_ARCH_30:
		if(chipset == NV_CHIP_IGEFORCE2) {

			dev = pci_get_bus_and_slot(0, 1);
			pci_read_config_dword(dev, 0x7C, &amt);
			pci_dev_put(dev);
			memlen = (((amt >> 6) & 31) + 1) * 1024;
		} else if (chipset == NV_CHIP_0x01F0) {
			dev = pci_get_bus_and_slot(0, 1);
			pci_read_config_dword(dev, 0x84, &amt);
			pci_dev_put(dev);
			memlen = (((amt >> 4) & 127) + 1) * 1024;
		} else {
			switch ((NV_RD32(chip->PFB, 0x0000020C) >> 20) &
				0x000000FF){
			case 0x02:
				memlen = 1024 * 2;
				break;
			case 0x04:
				memlen = 1024 * 4;
				break;
			case 0x08:
				memlen = 1024 * 8;
				break;
			case 0x10:
				memlen = 1024 * 16;
				break;
			case 0x20:
				memlen = 1024 * 32;
				break;
			case 0x40:
				memlen = 1024 * 64;
				break;
			case 0x80:
				memlen = 1024 * 128;
				break;
			default:
				memlen = 1024 * 16;
				break;
			}
		}
		break;
	}
	return memlen;
}

unsigned long riva_get_maxdclk(struct riva_par *par)
{
	RIVA_HW_INST *chip = &par->riva;
	unsigned long dclk = 0;

	switch (chip->Architecture) {
	case NV_ARCH_03:
		if (NV_RD32(chip->PFB, 0x00000000) & 0x00000020) {
			if (((NV_RD32(chip->PMC, 0x00000000) & 0xF0) == 0x20)
			    && ((NV_RD32(chip->PMC,0x00000000)&0x0F) >= 0x02)) {
				/*
				 * SDRAM 128 ZX.
				 */
				dclk = 800000;
			} else {
				dclk = 1000000;
			}            
		} else {
			/*
			 * SGRAM 128.
			 */
			dclk = 1000000;
		} 
		break;
	case NV_ARCH_04:
	case NV_ARCH_10:
	case NV_ARCH_20:
	case NV_ARCH_30:
		switch ((NV_RD32(chip->PFB, 0x00000000) >> 3) & 0x00000003) {
		case 3:
			dclk = 800000;
			break;
		default:
			dclk = 1000000;
			break;
		}
		break;
	}
	return dclk;
}

void
riva_common_setup(struct riva_par *par)
{
	par->riva.EnableIRQ = 0;
	par->riva.PRAMDAC0 =
		(volatile U032 __iomem *)(par->ctrl_base + 0x00680000);
	par->riva.PFB =
		(volatile U032 __iomem *)(par->ctrl_base + 0x00100000);
	par->riva.PFIFO =
		(volatile U032 __iomem *)(par->ctrl_base + 0x00002000);
	par->riva.PGRAPH =
		(volatile U032 __iomem *)(par->ctrl_base + 0x00400000);
	par->riva.PEXTDEV =
		(volatile U032 __iomem *)(par->ctrl_base + 0x00101000);
	par->riva.PTIMER =
		(volatile U032 __iomem *)(par->ctrl_base + 0x00009000);
	par->riva.PMC =
		(volatile U032 __iomem *)(par->ctrl_base + 0x00000000);
	par->riva.FIFO =
		(volatile U032 __iomem *)(par->ctrl_base + 0x00800000);
	par->riva.PCIO0 = par->ctrl_base + 0x00601000;
	par->riva.PDIO0 = par->ctrl_base + 0x00681000;
	par->riva.PVIO = par->ctrl_base + 0x000C0000;

	par->riva.IO = (MISCin(par) & 0x01) ? 0x3D0 : 0x3B0;
	
	if (par->FlatPanel == -1) {
		switch (par->Chipset & 0xffff) {
		case 0x0112:   /* known laptop chips */
		case 0x0174:
		case 0x0175:
		case 0x0176:
		case 0x0177:
		case 0x0179:
		case 0x017C:
		case 0x017D:
		case 0x0186:
		case 0x0187:
		case 0x0286:
		case 0x028C:
		case 0x0316:
		case 0x0317:
		case 0x031A:
		case 0x031B:
		case 0x031C:
		case 0x031D:
		case 0x031E:
		case 0x031F:
		case 0x0324:
		case 0x0325:
		case 0x0328:
		case 0x0329:
		case 0x032C:
		case 0x032D:
			printk(KERN_INFO PFX 
				"On a laptop.  Assuming Digital Flat Panel\n");
			par->FlatPanel = 1;
			break;
		default:
			break;
		}
	}
	
	switch (par->Chipset & 0x0ff0) {
	case 0x0110:
		if (par->Chipset == NV_CHIP_GEFORCE2_GO)
			par->SecondCRTC = TRUE; 
#if defined(__powerpc__)
		if (par->FlatPanel == 1)
			par->SecondCRTC = TRUE;
#endif
		riva_override_CRTC(par);
		break;
	case 0x0170:
	case 0x0180:
	case 0x01F0:
	case 0x0250:
	case 0x0280:
	case 0x0300:
	case 0x0310:
	case 0x0320:
	case 0x0330:
	case 0x0340:
		riva_is_second(par);
		break;
	default:
		break;
	}

	if (par->SecondCRTC) {
		par->riva.PCIO = par->riva.PCIO0 + 0x2000;
		par->riva.PCRTC = par->riva.PCRTC0 + 0x800;
		par->riva.PRAMDAC = par->riva.PRAMDAC0 + 0x800;
		par->riva.PDIO = par->riva.PDIO0 + 0x2000;
	} else {
		par->riva.PCIO = par->riva.PCIO0;
		par->riva.PCRTC = par->riva.PCRTC0;
		par->riva.PRAMDAC = par->riva.PRAMDAC0;
		par->riva.PDIO = par->riva.PDIO0;
	}

	if (par->FlatPanel == -1) {
		/* Fix me, need x86 DDC code */
		par->FlatPanel = 0;
	}
	par->riva.flatPanel = (par->FlatPanel > 0) ? TRUE : FALSE;

	RivaGetConfig(&par->riva, par->Chipset);
}


 /***************************************************************************\
|*                                                                           *|
|*       Copyright 2003 NVIDIA, Corporation.  All rights reserved.           *|
|*                                                                           *|
|*     NOTICE TO USER:   The source code  is copyrighted under  U.S. and     *|
|*     international laws.  Users and possessors of this source code are     *|
|*     hereby granted a nonexclusive,  royalty-free copyright license to     *|
|*     use this code in individual and commercial software.                  *|
|*                                                                           *|
|*     Any use of this source code must include,  in the user documenta-     *|
|*     tion and  internal comments to the code,  notices to the end user     *|
|*     as follows:                                                           *|
|*                                                                           *|
|*       Copyright 2003 NVIDIA, Corporation.  All rights reserved.           *|
|*                                                                           *|
|*     NVIDIA, CORPORATION MAKES NO REPRESENTATION ABOUT THE SUITABILITY     *|
|*     OF  THIS SOURCE  CODE  FOR ANY PURPOSE.  IT IS  PROVIDED  "AS IS"     *|
|*     WITHOUT EXPRESS OR IMPLIED WARRANTY OF ANY KIND.  NVIDIA, CORPOR-     *|
|*     ATION DISCLAIMS ALL WARRANTIES  WITH REGARD  TO THIS SOURCE CODE,     *|
|*     INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY, NONINFRINGE-     *|
|*     MENT,  AND FITNESS  FOR A PARTICULAR PURPOSE.   IN NO EVENT SHALL     *|
|*     NVIDIA, CORPORATION  BE LIABLE FOR ANY SPECIAL,  INDIRECT,  INCI-     *|
|*     DENTAL, OR CONSEQUENTIAL DAMAGES,  OR ANY DAMAGES  WHATSOEVER RE-     *|
|*     SULTING FROM LOSS OF USE,  DATA OR PROFITS,  WHETHER IN AN ACTION     *|
|*     OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,  ARISING OUT OF     *|
|*     OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOURCE CODE.     *|
|*                                                                           *|
|*     U.S. Government  End  Users.   This source code  is a "commercial     *|
|*     item,"  as that  term is  defined at  48 C.F.R. 2.101 (OCT 1995),     *|
|*     consisting  of "commercial  computer  software"  and  "commercial     *|
|*     computer  software  documentation,"  as such  terms  are  used in     *|
|*     48 C.F.R. 12.212 (SEPT 1995)  and is provided to the U.S. Govern-     *|
|*     ment only as  a commercial end item.   Consistent with  48 C.F.R.     *|
|*     12.212 and  48 C.F.R. 227.7202-1 through  227.7202-4 (JUNE 1995),     *|
|*     all U.S. Government End Users  acquire the source code  with only     *|
|*     those rights set forth herein.                                        *|
|*                                                                           *|
 \***************************************************************************/

/*
 * GPL Licensing Note - According to Mark Vojkovich, author of the Xorg/
 * XFree86 'nv' driver, this source code is provided under MIT-style licensing
 * where the source code is provided "as is" without warranty of any kind.
 * The only usage restriction is for the copyright notices to be retained
 * whenever code is used.
 *
 * Antonino Daplas <adaplas@pol.net> 2005-03-11
 */

#include <video/vga.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include "nv_type.h"
#include "nv_local.h"
#include "nv_proto.h"
/*
 * Override VGA I/O routines.
 */
void NVWriteCrtc(struct nvidia_par *par, u8 index, u8 value)
{
	VGA_WR08(par->PCIO, par->IOBase + 0x04, index);
	VGA_WR08(par->PCIO, par->IOBase + 0x05, value);
}
u8 NVReadCrtc(struct nvidia_par *par, u8 index)
{
	VGA_WR08(par->PCIO, par->IOBase + 0x04, index);
	return (VGA_RD08(par->PCIO, par->IOBase + 0x05));
}
void NVWriteGr(struct nvidia_par *par, u8 index, u8 value)
{
	VGA_WR08(par->PVIO, VGA_GFX_I, index);
	VGA_WR08(par->PVIO, VGA_GFX_D, value);
}
u8 NVReadGr(struct nvidia_par *par, u8 index)
{
	VGA_WR08(par->PVIO, VGA_GFX_I, index);
	return (VGA_RD08(par->PVIO, VGA_GFX_D));
}
void NVWriteSeq(struct nvidia_par *par, u8 index, u8 value)
{
	VGA_WR08(par->PVIO, VGA_SEQ_I, index);
	VGA_WR08(par->PVIO, VGA_SEQ_D, value);
}
u8 NVReadSeq(struct nvidia_par *par, u8 index)
{
	VGA_WR08(par->PVIO, VGA_SEQ_I, index);
	return (VGA_RD08(par->PVIO, VGA_SEQ_D));
}
void NVWriteAttr(struct nvidia_par *par, u8 index, u8 value)
{
	volatile u8 tmp;

	tmp = VGA_RD08(par->PCIO, par->IOBase + 0x0a);
	if (par->paletteEnabled)
		index &= ~0x20;
	else
		index |= 0x20;
	VGA_WR08(par->PCIO, VGA_ATT_IW, index);
	VGA_WR08(par->PCIO, VGA_ATT_W, value);
}
u8 NVReadAttr(struct nvidia_par *par, u8 index)
{
	volatile u8 tmp;

	tmp = VGA_RD08(par->PCIO, par->IOBase + 0x0a);
	if (par->paletteEnabled)
		index &= ~0x20;
	else
		index |= 0x20;
	VGA_WR08(par->PCIO, VGA_ATT_IW, index);
	return (VGA_RD08(par->PCIO, VGA_ATT_R));
}
void NVWriteMiscOut(struct nvidia_par *par, u8 value)
{
	VGA_WR08(par->PVIO, VGA_MIS_W, value);
}
u8 NVReadMiscOut(struct nvidia_par *par)
{
	return (VGA_RD08(par->PVIO, VGA_MIS_R));
}
#if 0
void NVEnablePalette(struct nvidia_par *par)
{
	volatile u8 tmp;

	tmp = VGA_RD08(par->PCIO, par->IOBase + 0x0a);
	VGA_WR08(par->PCIO, VGA_ATT_IW, 0x00);
	par->paletteEnabled = 1;
}
void NVDisablePalette(struct nvidia_par *par)
{
	volatile u8 tmp;

	tmp = VGA_RD08(par->PCIO, par->IOBase + 0x0a);
	VGA_WR08(par->PCIO, VGA_ATT_IW, 0x20);
	par->paletteEnabled = 0;
}
#endif  /*  0  */
void NVWriteDacMask(struct nvidia_par *par, u8 value)
{
	VGA_WR08(par->PDIO, VGA_PEL_MSK, value);
}
#if 0
u8 NVReadDacMask(struct nvidia_par *par)
{
	return (VGA_RD08(par->PDIO, VGA_PEL_MSK));
}
#endif  /*  0  */
void NVWriteDacReadAddr(struct nvidia_par *par, u8 value)
{
	VGA_WR08(par->PDIO, VGA_PEL_IR, value);
}
void NVWriteDacWriteAddr(struct nvidia_par *par, u8 value)
{
	VGA_WR08(par->PDIO, VGA_PEL_IW, value);
}
void NVWriteDacData(struct nvidia_par *par, u8 value)
{
	VGA_WR08(par->PDIO, VGA_PEL_D, value);
}
u8 NVReadDacData(struct nvidia_par *par)
{
	return (VGA_RD08(par->PDIO, VGA_PEL_D));
}

static int NVIsConnected(struct nvidia_par *par, int output)
{
	volatile u32 __iomem *PRAMDAC = par->PRAMDAC0;
	u32 reg52C, reg608, dac0_reg608 = 0;
	int present;

	if (output) {
	    dac0_reg608 = NV_RD32(PRAMDAC, 0x0608);
	    PRAMDAC += 0x800;
	}

	reg52C = NV_RD32(PRAMDAC, 0x052C);
	reg608 = NV_RD32(PRAMDAC, 0x0608);

	NV_WR32(PRAMDAC, 0x0608, reg608 & ~0x00010000);

	NV_WR32(PRAMDAC, 0x052C, reg52C & 0x0000FEEE);
	msleep(1);
	NV_WR32(PRAMDAC, 0x052C, NV_RD32(PRAMDAC, 0x052C) | 1);

	NV_WR32(par->PRAMDAC0, 0x0610, 0x94050140);
	NV_WR32(par->PRAMDAC0, 0x0608, NV_RD32(par->PRAMDAC0, 0x0608) |
		0x00001000);

	msleep(1);

	present = (NV_RD32(PRAMDAC, 0x0608) & (1 << 28)) ? 1 : 0;

	if (present)
		printk("nvidiafb: CRTC%i analog found\n", output);
	else
		printk("nvidiafb: CRTC%i analog not found\n", output);

	if (output)
	    NV_WR32(par->PRAMDAC0, 0x0608, dac0_reg608);

	NV_WR32(PRAMDAC, 0x052C, reg52C);
	NV_WR32(PRAMDAC, 0x0608, reg608);

	return present;
}

static void NVSelectHeadRegisters(struct nvidia_par *par, int head)
{
	if (head) {
		par->PCIO = par->PCIO0 + 0x2000;
		par->PCRTC = par->PCRTC0 + 0x800;
		par->PRAMDAC = par->PRAMDAC0 + 0x800;
		par->PDIO = par->PDIO0 + 0x2000;
	} else {
		par->PCIO = par->PCIO0;
		par->PCRTC = par->PCRTC0;
		par->PRAMDAC = par->PRAMDAC0;
		par->PDIO = par->PDIO0;
	}
}

static void nv4GetConfig(struct nvidia_par *par)
{
	if (NV_RD32(par->PFB, 0x0000) & 0x00000100) {
		par->RamAmountKBytes =
		    ((NV_RD32(par->PFB, 0x0000) >> 12) & 0x0F) * 1024 * 2 +
		    1024 * 2;
	} else {
		switch (NV_RD32(par->PFB, 0x0000) & 0x00000003) {
		case 0:
			par->RamAmountKBytes = 1024 * 32;
			break;
		case 1:
			par->RamAmountKBytes = 1024 * 4;
			break;
		case 2:
			par->RamAmountKBytes = 1024 * 8;
			break;
		case 3:
		default:
			par->RamAmountKBytes = 1024 * 16;
			break;
		}
	}
	par->CrystalFreqKHz = (NV_RD32(par->PEXTDEV, 0x0000) & 0x00000040) ?
	    14318 : 13500;
	par->CURSOR = &par->PRAMIN[0x1E00];
	par->MinVClockFreqKHz = 12000;
	par->MaxVClockFreqKHz = 350000;
}

static void nv10GetConfig(struct nvidia_par *par)
{
	struct pci_dev *dev;
	u32 implementation = par->Chipset & 0x0ff0;

#ifdef __BIG_ENDIAN
	/* turn on big endian register access */
	if (!(NV_RD32(par->PMC, 0x0004) & 0x01000001)) {
		NV_WR32(par->PMC, 0x0004, 0x01000001);
		mb();
	}
#endif

	dev = pci_get_bus_and_slot(0, 1);
	if ((par->Chipset & 0xffff) == 0x01a0) {
		u32 amt;

		pci_read_config_dword(dev, 0x7c, &amt);
		par->RamAmountKBytes = (((amt >> 6) & 31) + 1) * 1024;
	} else if ((par->Chipset & 0xffff) == 0x01f0) {
		u32 amt;

		pci_read_config_dword(dev, 0x84, &amt);
		par->RamAmountKBytes = (((amt >> 4) & 127) + 1) * 1024;
	} else {
		par->RamAmountKBytes =
		    (NV_RD32(par->PFB, 0x020C) & 0xFFF00000) >> 10;
	}
	pci_dev_put(dev);

	par->CrystalFreqKHz = (NV_RD32(par->PEXTDEV, 0x0000) & (1 << 6)) ?
	    14318 : 13500;

	if (par->twoHeads && (implementation != 0x0110)) {
		if (NV_RD32(par->PEXTDEV, 0x0000) & (1 << 22))
			par->CrystalFreqKHz = 27000;
	}

	par->CURSOR = NULL;	/* can't set this here */
	par->MinVClockFreqKHz = 12000;
	par->MaxVClockFreqKHz = par->twoStagePLL ? 400000 : 350000;
}

int NVCommonSetup(struct fb_info *info)
{
	struct nvidia_par *par = info->par;
	struct fb_var_screeninfo *var;
	u16 implementation = par->Chipset & 0x0ff0;
	u8 *edidA = NULL, *edidB = NULL;
	struct fb_monspecs *monitorA, *monitorB;
	struct fb_monspecs *monA = NULL, *monB = NULL;
	int mobile = 0;
	int tvA = 0;
	int tvB = 0;
	int FlatPanel = -1;	/* really means the CRTC is slaved */
	int Television = 0;
	int err = 0;

	var = kzalloc(sizeof(struct fb_var_screeninfo), GFP_KERNEL);
	monitorA = kzalloc(sizeof(struct fb_monspecs), GFP_KERNEL);
	monitorB = kzalloc(sizeof(struct fb_monspecs), GFP_KERNEL);

	if (!var || !monitorA || !monitorB) {
		err = -ENOMEM;
		goto done;
	}

	par->PRAMIN = par->REGS + (0x00710000 / 4);
	par->PCRTC0 = par->REGS + (0x00600000 / 4);
	par->PRAMDAC0 = par->REGS + (0x00680000 / 4);
	par->PFB = par->REGS + (0x00100000 / 4);
	par->PFIFO = par->REGS + (0x00002000 / 4);
	par->PGRAPH = par->REGS + (0x00400000 / 4);
	par->PEXTDEV = par->REGS + (0x00101000 / 4);
	par->PTIMER = par->REGS + (0x00009000 / 4);
	par->PMC = par->REGS + (0x00000000 / 4);
	par->FIFO = par->REGS + (0x00800000 / 4);

	/* 8 bit registers */
	par->PCIO0 = (u8 __iomem *) par->REGS + 0x00601000;
	par->PDIO0 = (u8 __iomem *) par->REGS + 0x00681000;
	par->PVIO = (u8 __iomem *) par->REGS + 0x000C0000;

	par->twoHeads = (par->Architecture >= NV_ARCH_10) &&
	    (implementation != 0x0100) &&
	    (implementation != 0x0150) &&
	    (implementation != 0x01A0) && (implementation != 0x0200);

	par->fpScaler = (par->FpScale && par->twoHeads &&
			 (implementation != 0x0110));

	par->twoStagePLL = (implementation == 0x0310) ||
	    (implementation == 0x0340) || (par->Architecture >= NV_ARCH_40);

	par->WaitVSyncPossible = (par->Architecture >= NV_ARCH_10) &&
	    (implementation != 0x0100);

	par->BlendingPossible = ((par->Chipset & 0xffff) != 0x0020);

	/* look for known laptop chips */
	switch (par->Chipset & 0xffff) {
	case 0x0112:
	case 0x0174:
	case 0x0175:
	case 0x0176:
	case 0x0177:
	case 0x0179:
	case 0x017C:
	case 0x017D:
	case 0x0186:
	case 0x0187:
	case 0x018D:
	case 0x01D7:
	case 0x0228:
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
	case 0x0347:
	case 0x0348:
	case 0x0349:
	case 0x034B:
	case 0x034C:
	case 0x0160:
	case 0x0166:
	case 0x0169:
	case 0x016B:
	case 0x016C:
	case 0x016D:
	case 0x00C8:
	case 0x00CC:
	case 0x0144:
	case 0x0146:
	case 0x0147:
	case 0x0148:
	case 0x0098:
	case 0x0099:
		mobile = 1;
		break;
	default:
		break;
	}

	if (par->Architecture == NV_ARCH_04)
		nv4GetConfig(par);
	else
		nv10GetConfig(par);

	NVSelectHeadRegisters(par, 0);

	NVLockUnlock(par, 0);

	par->IOBase = (NVReadMiscOut(par) & 0x01) ? 0x3d0 : 0x3b0;

	par->Television = 0;

	nvidia_create_i2c_busses(par);
	if (!par->twoHeads) {
		par->CRTCnumber = 0;
		if (nvidia_probe_i2c_connector(info, 1, &edidA))
			nvidia_probe_of_connector(info, 1, &edidA);
		if (edidA && !fb_parse_edid(edidA, var)) {
			printk("nvidiafb: EDID found from BUS1\n");
			monA = monitorA;
			fb_edid_to_monspecs(edidA, monA);
			FlatPanel = (monA->input & FB_DISP_DDI) ? 1 : 0;

			/* NV4 doesn't support FlatPanels */
			if ((par->Chipset & 0x0fff) <= 0x0020)
				FlatPanel = 0;
		} else {
			VGA_WR08(par->PCIO, 0x03D4, 0x28);
			if (VGA_RD08(par->PCIO, 0x03D5) & 0x80) {
				VGA_WR08(par->PCIO, 0x03D4, 0x33);
				if (!(VGA_RD08(par->PCIO, 0x03D5) & 0x01))
					Television = 1;
				FlatPanel = 1;
			} else {
				FlatPanel = 0;
			}
			printk("nvidiafb: HW is currently programmed for %s\n",
			       FlatPanel ? (Television ? "TV" : "DFP") :
			       "CRT");
		}

		if (par->FlatPanel == -1) {
			par->FlatPanel = FlatPanel;
			par->Television = Television;
		} else {
			printk("nvidiafb: Forcing display type to %s as "
			       "specified\n", par->FlatPanel ? "DFP" : "CRT");
		}
	} else {
		u8 outputAfromCRTC, outputBfromCRTC;
		int CRTCnumber = -1;
		u8 slaved_on_A, slaved_on_B;
		int analog_on_A, analog_on_B;
		u32 oldhead;
		u8 cr44;

		if (implementation != 0x0110) {
			if (NV_RD32(par->PRAMDAC0, 0x0000052C) & 0x100)
				outputAfromCRTC = 1;
			else
				outputAfromCRTC = 0;
			if (NV_RD32(par->PRAMDAC0, 0x0000252C) & 0x100)
				outputBfromCRTC = 1;
			else
				outputBfromCRTC = 0;
			analog_on_A = NVIsConnected(par, 0);
			analog_on_B = NVIsConnected(par, 1);
		} else {
			outputAfromCRTC = 0;
			outputBfromCRTC = 1;
			analog_on_A = 0;
			analog_on_B = 0;
		}

		VGA_WR08(par->PCIO, 0x03D4, 0x44);
		cr44 = VGA_RD08(par->PCIO, 0x03D5);

		VGA_WR08(par->PCIO, 0x03D5, 3);
		NVSelectHeadRegisters(par, 1);
		NVLockUnlock(par, 0);

		VGA_WR08(par->PCIO, 0x03D4, 0x28);
		slaved_on_B = VGA_RD08(par->PCIO, 0x03D5) & 0x80;
		if (slaved_on_B) {
			VGA_WR08(par->PCIO, 0x03D4, 0x33);
			tvB = !(VGA_RD08(par->PCIO, 0x03D5) & 0x01);
		}

		VGA_WR08(par->PCIO, 0x03D4, 0x44);
		VGA_WR08(par->PCIO, 0x03D5, 0);
		NVSelectHeadRegisters(par, 0);
		NVLockUnlock(par, 0);

		VGA_WR08(par->PCIO, 0x03D4, 0x28);
		slaved_on_A = VGA_RD08(par->PCIO, 0x03D5) & 0x80;
		if (slaved_on_A) {
			VGA_WR08(par->PCIO, 0x03D4, 0x33);
			tvA = !(VGA_RD08(par->PCIO, 0x03D5) & 0x01);
		}

		oldhead = NV_RD32(par->PCRTC0, 0x00000860);
		NV_WR32(par->PCRTC0, 0x00000860, oldhead | 0x00000010);

		if (nvidia_probe_i2c_connector(info, 1, &edidA))
			nvidia_probe_of_connector(info, 1, &edidA);
		if (edidA && !fb_parse_edid(edidA, var)) {
			printk("nvidiafb: EDID found from BUS1\n");
			monA = monitorA;
			fb_edid_to_monspecs(edidA, monA);
		}

		if (nvidia_probe_i2c_connector(info, 2, &edidB))
			nvidia_probe_of_connector(info, 2, &edidB);
		if (edidB && !fb_parse_edid(edidB, var)) {
			printk("nvidiafb: EDID found from BUS2\n");
			monB = monitorB;
			fb_edid_to_monspecs(edidB, monB);
		}

		if (slaved_on_A && !tvA) {
			CRTCnumber = 0;
			FlatPanel = 1;
			printk("nvidiafb: CRTC 0 is currently programmed for "
			       "DFP\n");
		} else if (slaved_on_B && !tvB) {
			CRTCnumber = 1;
			FlatPanel = 1;
			printk("nvidiafb: CRTC 1 is currently programmed "
			       "for DFP\n");
		} else if (analog_on_A) {
			CRTCnumber = outputAfromCRTC;
			FlatPanel = 0;
			printk("nvidiafb: CRTC %i appears to have a "
			       "CRT attached\n", CRTCnumber);
		} else if (analog_on_B) {
			CRTCnumber = outputBfromCRTC;
			FlatPanel = 0;
			printk("nvidiafb: CRTC %i appears to have a "
			       "CRT attached\n", CRTCnumber);
		} else if (slaved_on_A) {
			CRTCnumber = 0;
			FlatPanel = 1;
			Television = 1;
			printk("nvidiafb: CRTC 0 is currently programmed "
			       "for TV\n");
		} else if (slaved_on_B) {
			CRTCnumber = 1;
			FlatPanel = 1;
			Television = 1;
			printk("nvidiafb: CRTC 1 is currently programmed for "
			       "TV\n");
		} else if (monA) {
			FlatPanel = (monA->input & FB_DISP_DDI) ? 1 : 0;
		} else if (monB) {
			FlatPanel = (monB->input & FB_DISP_DDI) ? 1 : 0;
		}

		if (par->FlatPanel == -1) {
			if (FlatPanel != -1) {
				par->FlatPanel = FlatPanel;
				par->Television = Television;
			} else {
				printk("nvidiafb: Unable to detect display "
				       "type...\n");
				if (mobile) {
					printk("...On a laptop, assuming "
					       "DFP\n");
					par->FlatPanel = 1;
				} else {
					printk("...Using default of CRT\n");
					par->FlatPanel = 0;
				}
			}
		} else {
			printk("nvidiafb: Forcing display type to %s as "
			       "specified\n", par->FlatPanel ? "DFP" : "CRT");
		}

		if (par->CRTCnumber == -1) {
			if (CRTCnumber != -1)
				par->CRTCnumber = CRTCnumber;
			else {
				printk("nvidiafb: Unable to detect which "
				       "CRTCNumber...\n");
				if (par->FlatPanel)
					par->CRTCnumber = 1;
				else
					par->CRTCnumber = 0;
				printk("...Defaulting to CRTCNumber %i\n",
				       par->CRTCnumber);
			}
		} else {
			printk("nvidiafb: Forcing CRTCNumber %i as "
			       "specified\n", par->CRTCnumber);
		}

		if (monA) {
			if (((monA->input & FB_DISP_DDI) &&
			     par->FlatPanel) ||
			    ((!(monA->input & FB_DISP_DDI)) &&
			     !par->FlatPanel)) {
				if (monB) {
					fb_destroy_modedb(monB->modedb);
					monB = NULL;
				}
			} else {
				fb_destroy_modedb(monA->modedb);
				monA = NULL;
			}
		}

		if (monB) {
			if (((monB->input & FB_DISP_DDI) &&
			     !par->FlatPanel) ||
			    ((!(monB->input & FB_DISP_DDI)) &&
			     par->FlatPanel)) {
				fb_destroy_modedb(monB->modedb);
				monB = NULL;
			} else
				monA = monB;
		}

		if (implementation == 0x0110)
			cr44 = par->CRTCnumber * 0x3;

		NV_WR32(par->PCRTC0, 0x00000860, oldhead);

		VGA_WR08(par->PCIO, 0x03D4, 0x44);
		VGA_WR08(par->PCIO, 0x03D5, cr44);
		NVSelectHeadRegisters(par, par->CRTCnumber);
	}

	printk("nvidiafb: Using %s on CRTC %i\n",
	       par->FlatPanel ? (par->Television ? "TV" : "DFP") : "CRT",
	       par->CRTCnumber);

	if (par->FlatPanel && !par->Television) {
		par->fpWidth = NV_RD32(par->PRAMDAC, 0x0820) + 1;
		par->fpHeight = NV_RD32(par->PRAMDAC, 0x0800) + 1;
		par->fpSyncs = NV_RD32(par->PRAMDAC, 0x0848) & 0x30000033;

		printk("nvidiafb: Panel size is %i x %i\n", par->fpWidth, par->fpHeight);
	}

	if (monA)
		info->monspecs = *monA;

	if (!par->FlatPanel || !par->twoHeads)
		par->FPDither = 0;

	par->LVDS = 0;
	if (par->FlatPanel && par->twoHeads) {
		NV_WR32(par->PRAMDAC0, 0x08B0, 0x00010004);
		if (NV_RD32(par->PRAMDAC0, 0x08b4) & 1)
			par->LVDS = 1;
		printk("nvidiafb: Panel is %s\n", par->LVDS ? "LVDS" : "TMDS");
	}

	kfree(edidA);
	kfree(edidB);
done:
	kfree(var);
	kfree(monitorA);
	kfree(monitorB);
	return err;
}

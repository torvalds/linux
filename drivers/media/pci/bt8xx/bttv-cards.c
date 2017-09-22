/*

    bttv-cards.c

    this file has configuration informations - card-specific stuff
    like the big tvcards array for the most part

    Copyright (C) 1996,97,98 Ralph  Metzler (rjkm@thp.uni-koeln.de)
			   & Marcus Metzler (mocm@thp.uni-koeln.de)
    (c) 1999-2001 Gerd Knorr <kraxel@goldbach.in-berlin.de>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/kmod.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/vmalloc.h>
#include <linux/firmware.h>
#include <net/checksum.h>

#include <asm/unaligned.h>
#include <asm/io.h>

#include "bttvp.h"
#include <media/v4l2-common.h>
#include <media/i2c/tvaudio.h>
#include "bttv-audio-hook.h"

/* fwd decl */
static void boot_msp34xx(struct bttv *btv, int pin);
static void hauppauge_eeprom(struct bttv *btv);
static void avermedia_eeprom(struct bttv *btv);
static void osprey_eeprom(struct bttv *btv, const u8 ee[256]);
static void modtec_eeprom(struct bttv *btv);
static void init_PXC200(struct bttv *btv);
static void init_RTV24(struct bttv *btv);
static void init_PCI8604PW(struct bttv *btv);

static void rv605_muxsel(struct bttv *btv, unsigned int input);
static void eagle_muxsel(struct bttv *btv, unsigned int input);
static void xguard_muxsel(struct bttv *btv, unsigned int input);
static void ivc120_muxsel(struct bttv *btv, unsigned int input);
static void gvc1100_muxsel(struct bttv *btv, unsigned int input);

static void PXC200_muxsel(struct bttv *btv, unsigned int input);

static void picolo_tetra_muxsel(struct bttv *btv, unsigned int input);
static void picolo_tetra_init(struct bttv *btv);

static void tibetCS16_muxsel(struct bttv *btv, unsigned int input);
static void tibetCS16_init(struct bttv *btv);

static void kodicom4400r_muxsel(struct bttv *btv, unsigned int input);
static void kodicom4400r_init(struct bttv *btv);

static void sigmaSLC_muxsel(struct bttv *btv, unsigned int input);
static void sigmaSQ_muxsel(struct bttv *btv, unsigned int input);

static void geovision_muxsel(struct bttv *btv, unsigned int input);

static void phytec_muxsel(struct bttv *btv, unsigned int input);

static void gv800s_muxsel(struct bttv *btv, unsigned int input);
static void gv800s_init(struct bttv *btv);

static void td3116_muxsel(struct bttv *btv, unsigned int input);

static int terratec_active_radio_upgrade(struct bttv *btv);
static int tea575x_init(struct bttv *btv);
static void identify_by_eeprom(struct bttv *btv,
			       unsigned char eeprom_data[256]);
static int pvr_boot(struct bttv *btv);

/* config variables */
static unsigned int triton1;
static unsigned int vsfx;
static unsigned int latency = UNSET;
int no_overlay=-1;

static unsigned int card[BTTV_MAX]   = { [ 0 ... (BTTV_MAX-1) ] = UNSET };
static unsigned int pll[BTTV_MAX]    = { [ 0 ... (BTTV_MAX-1) ] = UNSET };
static unsigned int tuner[BTTV_MAX]  = { [ 0 ... (BTTV_MAX-1) ] = UNSET };
static unsigned int svhs[BTTV_MAX]   = { [ 0 ... (BTTV_MAX-1) ] = UNSET };
static unsigned int remote[BTTV_MAX] = { [ 0 ... (BTTV_MAX-1) ] = UNSET };
static unsigned int audiodev[BTTV_MAX];
static unsigned int saa6588[BTTV_MAX];
static struct bttv  *master[BTTV_MAX] = { [ 0 ... (BTTV_MAX-1) ] = NULL };
static unsigned int autoload = UNSET;
static unsigned int gpiomask = UNSET;
static unsigned int audioall = UNSET;
static unsigned int audiomux[5] = { [ 0 ... 4 ] = UNSET };

/* insmod options */
module_param(triton1,    int, 0444);
module_param(vsfx,       int, 0444);
module_param(no_overlay, int, 0444);
module_param(latency,    int, 0444);
module_param(gpiomask,   int, 0444);
module_param(audioall,   int, 0444);
module_param(autoload,   int, 0444);

module_param_array(card,     int, NULL, 0444);
module_param_array(pll,      int, NULL, 0444);
module_param_array(tuner,    int, NULL, 0444);
module_param_array(svhs,     int, NULL, 0444);
module_param_array(remote,   int, NULL, 0444);
module_param_array(audiodev, int, NULL, 0444);
module_param_array(audiomux, int, NULL, 0444);

MODULE_PARM_DESC(triton1, "set ETBF pci config bit [enable bug compatibility for triton1 + others]");
MODULE_PARM_DESC(vsfx, "set VSFX pci config bit [yet another chipset flaw workaround]");
MODULE_PARM_DESC(latency,"pci latency timer");
MODULE_PARM_DESC(card,"specify TV/grabber card model, see CARDLIST file for a list");
MODULE_PARM_DESC(pll, "specify installed crystal (0=none, 28=28 MHz, 35=35 MHz, 14=14 MHz)");
MODULE_PARM_DESC(tuner,"specify installed tuner type");
MODULE_PARM_DESC(autoload, "obsolete option, please do not use anymore");
MODULE_PARM_DESC(audiodev, "specify audio device:\n"
		"\t\t-1 = no audio\n"
		"\t\t 0 = autodetect (default)\n"
		"\t\t 1 = msp3400\n"
		"\t\t 2 = tda7432\n"
		"\t\t 3 = tvaudio");
MODULE_PARM_DESC(saa6588, "if 1, then load the saa6588 RDS module, default (0) is to use the card definition.");
MODULE_PARM_DESC(no_overlay, "allow override overlay default (0 disables, 1 enables) [some VIA/SIS chipsets are known to have problem with overlay]");

/* ----------------------------------------------------------------------- */
/* list of card IDs for bt878+ cards                                       */

static struct CARD {
	unsigned id;
	int cardnr;
	char *name;
} cards[] = {
	{ 0x13eb0070, BTTV_BOARD_HAUPPAUGE878,  "Hauppauge WinTV" },
	{ 0x39000070, BTTV_BOARD_HAUPPAUGE878,  "Hauppauge WinTV-D" },
	{ 0x45000070, BTTV_BOARD_HAUPPAUGEPVR,  "Hauppauge WinTV/PVR" },
	{ 0xff000070, BTTV_BOARD_OSPREY1x0,     "Osprey-100" },
	{ 0xff010070, BTTV_BOARD_OSPREY2x0_SVID,"Osprey-200" },
	{ 0xff020070, BTTV_BOARD_OSPREY500,     "Osprey-500" },
	{ 0xff030070, BTTV_BOARD_OSPREY2000,    "Osprey-2000" },
	{ 0xff040070, BTTV_BOARD_OSPREY540,     "Osprey-540" },
	{ 0xff070070, BTTV_BOARD_OSPREY440,     "Osprey-440" },

	{ 0x00011002, BTTV_BOARD_ATI_TVWONDER,  "ATI TV Wonder" },
	{ 0x00031002, BTTV_BOARD_ATI_TVWONDERVE,"ATI TV Wonder/VE" },

	{ 0x6606107d, BTTV_BOARD_WINFAST2000,   "Leadtek WinFast TV 2000" },
	{ 0x6607107d, BTTV_BOARD_WINFASTVC100,  "Leadtek WinFast VC 100" },
	{ 0x6609107d, BTTV_BOARD_WINFAST2000,   "Leadtek TV 2000 XP" },
	{ 0x263610b4, BTTV_BOARD_STB2,          "STB TV PCI FM, Gateway P/N 6000704" },
	{ 0x264510b4, BTTV_BOARD_STB2,          "STB TV PCI FM, Gateway P/N 6000704" },
	{ 0x402010fc, BTTV_BOARD_GVBCTV3PCI,    "I-O Data Co. GV-BCTV3/PCI" },
	{ 0x405010fc, BTTV_BOARD_GVBCTV4PCI,    "I-O Data Co. GV-BCTV4/PCI" },
	{ 0x407010fc, BTTV_BOARD_GVBCTV5PCI,    "I-O Data Co. GV-BCTV5/PCI" },
	{ 0xd01810fc, BTTV_BOARD_GVBCTV5PCI,    "I-O Data Co. GV-BCTV5/PCI" },

	{ 0x001211bd, BTTV_BOARD_PINNACLE,      "Pinnacle PCTV" },
	/* some cards ship with byteswapped IDs ... */
	{ 0x1200bd11, BTTV_BOARD_PINNACLE,      "Pinnacle PCTV [bswap]" },
	{ 0xff00bd11, BTTV_BOARD_PINNACLE,      "Pinnacle PCTV [bswap]" },
	/* this seems to happen as well ... */
	{ 0xff1211bd, BTTV_BOARD_PINNACLE,      "Pinnacle PCTV" },

	{ 0x3000121a, BTTV_BOARD_VOODOOTV_200,  "3Dfx VoodooTV 200" },
	{ 0x263710b4, BTTV_BOARD_VOODOOTV_FM,   "3Dfx VoodooTV FM" },
	{ 0x3060121a, BTTV_BOARD_STB2,	  "3Dfx VoodooTV 100/ STB OEM" },

	{ 0x3000144f, BTTV_BOARD_MAGICTVIEW063, "(Askey Magic/others) TView99 CPH06x" },
	{ 0xa005144f, BTTV_BOARD_MAGICTVIEW063, "CPH06X TView99-Card" },
	{ 0x3002144f, BTTV_BOARD_MAGICTVIEW061, "(Askey Magic/others) TView99 CPH05x" },
	{ 0x3005144f, BTTV_BOARD_MAGICTVIEW061, "(Askey Magic/others) TView99 CPH061/06L (T1/LC)" },
	{ 0x5000144f, BTTV_BOARD_MAGICTVIEW061, "Askey CPH050" },
	{ 0x300014ff, BTTV_BOARD_MAGICTVIEW061, "TView 99 (CPH061)" },
	{ 0x300214ff, BTTV_BOARD_PHOEBE_TVMAS,  "Phoebe TV Master (CPH060)" },

	{ 0x00011461, BTTV_BOARD_AVPHONE98,     "AVerMedia TVPhone98" },
	{ 0x00021461, BTTV_BOARD_AVERMEDIA98,   "AVermedia TVCapture 98" },
	{ 0x00031461, BTTV_BOARD_AVPHONE98,     "AVerMedia TVPhone98" },
	{ 0x00041461, BTTV_BOARD_AVERMEDIA98,   "AVerMedia TVCapture 98" },
	{ 0x03001461, BTTV_BOARD_AVERMEDIA98,   "VDOMATE TV TUNER CARD" },

	{ 0x1117153b, BTTV_BOARD_TERRATVALUE,   "Terratec TValue (Philips PAL B/G)" },
	{ 0x1118153b, BTTV_BOARD_TERRATVALUE,   "Terratec TValue (Temic PAL B/G)" },
	{ 0x1119153b, BTTV_BOARD_TERRATVALUE,   "Terratec TValue (Philips PAL I)" },
	{ 0x111a153b, BTTV_BOARD_TERRATVALUE,   "Terratec TValue (Temic PAL I)" },

	{ 0x1123153b, BTTV_BOARD_TERRATVRADIO,  "Terratec TV Radio+" },
	{ 0x1127153b, BTTV_BOARD_TERRATV,       "Terratec TV+ (V1.05)"    },
	/* clashes with FlyVideo
	 *{ 0x18521852, BTTV_BOARD_TERRATV,     "Terratec TV+ (V1.10)"    }, */
	{ 0x1134153b, BTTV_BOARD_TERRATVALUE,   "Terratec TValue (LR102)" },
	{ 0x1135153b, BTTV_BOARD_TERRATVALUER,  "Terratec TValue Radio" }, /* LR102 */
	{ 0x5018153b, BTTV_BOARD_TERRATVALUE,   "Terratec TValue" },       /* ?? */
	{ 0xff3b153b, BTTV_BOARD_TERRATVALUER,  "Terratec TValue Radio" }, /* ?? */

	{ 0x400015b0, BTTV_BOARD_ZOLTRIX_GENIE, "Zoltrix Genie TV" },
	{ 0x400a15b0, BTTV_BOARD_ZOLTRIX_GENIE, "Zoltrix Genie TV" },
	{ 0x400d15b0, BTTV_BOARD_ZOLTRIX_GENIE, "Zoltrix Genie TV / Radio" },
	{ 0x401015b0, BTTV_BOARD_ZOLTRIX_GENIE, "Zoltrix Genie TV / Radio" },
	{ 0x401615b0, BTTV_BOARD_ZOLTRIX_GENIE, "Zoltrix Genie TV / Radio" },

	{ 0x1430aa00, BTTV_BOARD_PV143,         "Provideo PV143A" },
	{ 0x1431aa00, BTTV_BOARD_PV143,         "Provideo PV143B" },
	{ 0x1432aa00, BTTV_BOARD_PV143,         "Provideo PV143C" },
	{ 0x1433aa00, BTTV_BOARD_PV143,         "Provideo PV143D" },
	{ 0x1433aa03, BTTV_BOARD_PV143,         "Security Eyes" },

	{ 0x1460aa00, BTTV_BOARD_PV150,         "Provideo PV150A-1" },
	{ 0x1461aa01, BTTV_BOARD_PV150,         "Provideo PV150A-2" },
	{ 0x1462aa02, BTTV_BOARD_PV150,         "Provideo PV150A-3" },
	{ 0x1463aa03, BTTV_BOARD_PV150,         "Provideo PV150A-4" },

	{ 0x1464aa04, BTTV_BOARD_PV150,         "Provideo PV150B-1" },
	{ 0x1465aa05, BTTV_BOARD_PV150,         "Provideo PV150B-2" },
	{ 0x1466aa06, BTTV_BOARD_PV150,         "Provideo PV150B-3" },
	{ 0x1467aa07, BTTV_BOARD_PV150,         "Provideo PV150B-4" },

	{ 0xa132ff00, BTTV_BOARD_IVC100,        "IVC-100"  },
	{ 0xa1550000, BTTV_BOARD_IVC200,        "IVC-200"  },
	{ 0xa1550001, BTTV_BOARD_IVC200,        "IVC-200"  },
	{ 0xa1550002, BTTV_BOARD_IVC200,        "IVC-200"  },
	{ 0xa1550003, BTTV_BOARD_IVC200,        "IVC-200"  },
	{ 0xa1550100, BTTV_BOARD_IVC200,        "IVC-200G" },
	{ 0xa1550101, BTTV_BOARD_IVC200,        "IVC-200G" },
	{ 0xa1550102, BTTV_BOARD_IVC200,        "IVC-200G" },
	{ 0xa1550103, BTTV_BOARD_IVC200,        "IVC-200G" },
	{ 0xa1550800, BTTV_BOARD_IVC200,        "IVC-200"  },
	{ 0xa1550801, BTTV_BOARD_IVC200,        "IVC-200"  },
	{ 0xa1550802, BTTV_BOARD_IVC200,        "IVC-200"  },
	{ 0xa1550803, BTTV_BOARD_IVC200,        "IVC-200"  },
	{ 0xa182ff00, BTTV_BOARD_IVC120,        "IVC-120G" },
	{ 0xa182ff01, BTTV_BOARD_IVC120,        "IVC-120G" },
	{ 0xa182ff02, BTTV_BOARD_IVC120,        "IVC-120G" },
	{ 0xa182ff03, BTTV_BOARD_IVC120,        "IVC-120G" },
	{ 0xa182ff04, BTTV_BOARD_IVC120,        "IVC-120G" },
	{ 0xa182ff05, BTTV_BOARD_IVC120,        "IVC-120G" },
	{ 0xa182ff06, BTTV_BOARD_IVC120,        "IVC-120G" },
	{ 0xa182ff07, BTTV_BOARD_IVC120,        "IVC-120G" },
	{ 0xa182ff08, BTTV_BOARD_IVC120,        "IVC-120G" },
	{ 0xa182ff09, BTTV_BOARD_IVC120,        "IVC-120G" },
	{ 0xa182ff0a, BTTV_BOARD_IVC120,        "IVC-120G" },
	{ 0xa182ff0b, BTTV_BOARD_IVC120,        "IVC-120G" },
	{ 0xa182ff0c, BTTV_BOARD_IVC120,        "IVC-120G" },
	{ 0xa182ff0d, BTTV_BOARD_IVC120,        "IVC-120G" },
	{ 0xa182ff0e, BTTV_BOARD_IVC120,        "IVC-120G" },
	{ 0xa182ff0f, BTTV_BOARD_IVC120,        "IVC-120G" },
	{ 0xf0500000, BTTV_BOARD_IVCE8784,      "IVCE-8784" },
	{ 0xf0500001, BTTV_BOARD_IVCE8784,      "IVCE-8784" },
	{ 0xf0500002, BTTV_BOARD_IVCE8784,      "IVCE-8784" },
	{ 0xf0500003, BTTV_BOARD_IVCE8784,      "IVCE-8784" },

	{ 0x41424344, BTTV_BOARD_GRANDTEC,      "GrandTec Multi Capture" },
	{ 0x01020304, BTTV_BOARD_XGUARD,        "Grandtec Grand X-Guard" },

	{ 0x18501851, BTTV_BOARD_CHRONOS_VS2,   "FlyVideo 98 (LR50)/ Chronos Video Shuttle II" },
	{ 0xa0501851, BTTV_BOARD_CHRONOS_VS2,   "FlyVideo 98 (LR50)/ Chronos Video Shuttle II" },
	{ 0x18511851, BTTV_BOARD_FLYVIDEO98EZ,  "FlyVideo 98EZ (LR51)/ CyberMail AV" },
	{ 0x18521852, BTTV_BOARD_TYPHOON_TVIEW, "FlyVideo 98FM (LR50)/ Typhoon TView TV/FM Tuner" },
	{ 0x41a0a051, BTTV_BOARD_FLYVIDEO_98FM, "Lifeview FlyVideo 98 LR50 Rev Q" },
	{ 0x18501f7f, BTTV_BOARD_FLYVIDEO_98,   "Lifeview Flyvideo 98" },

	{ 0x010115cb, BTTV_BOARD_GMV1,          "AG GMV1" },
	{ 0x010114c7, BTTV_BOARD_MODTEC_205,    "Modular Technology MM201/MM202/MM205/MM210/MM215 PCTV" },

	{ 0x10b42636, BTTV_BOARD_HAUPPAUGE878,  "STB ???" },
	{ 0x217d6606, BTTV_BOARD_WINFAST2000,   "Leadtek WinFast TV 2000" },
	{ 0xfff6f6ff, BTTV_BOARD_WINFAST2000,   "Leadtek WinFast TV 2000" },
	{ 0x03116000, BTTV_BOARD_SENSORAY311_611, "Sensoray 311" },
	{ 0x06116000, BTTV_BOARD_SENSORAY311_611, "Sensoray 611" },
	{ 0x00790e11, BTTV_BOARD_WINDVR,        "Canopus WinDVR PCI" },
	{ 0xa0fca1a0, BTTV_BOARD_ZOLTRIX,       "Face to Face Tvmax" },
	{ 0x82b2aa6a, BTTV_BOARD_SIMUS_GVC1100, "SIMUS GVC1100" },
	{ 0x146caa0c, BTTV_BOARD_PV951,         "ituner spectra8" },
	{ 0x200a1295, BTTV_BOARD_PXC200,        "ImageNation PXC200A" },

	{ 0x40111554, BTTV_BOARD_PV_BT878P_9B,  "Prolink Pixelview PV-BT" },
	{ 0x17de0a01, BTTV_BOARD_KWORLD,        "Mecer TV/FM/Video Tuner" },

	{ 0x01051805, BTTV_BOARD_PICOLO_TETRA_CHIP, "Picolo Tetra Chip #1" },
	{ 0x01061805, BTTV_BOARD_PICOLO_TETRA_CHIP, "Picolo Tetra Chip #2" },
	{ 0x01071805, BTTV_BOARD_PICOLO_TETRA_CHIP, "Picolo Tetra Chip #3" },
	{ 0x01081805, BTTV_BOARD_PICOLO_TETRA_CHIP, "Picolo Tetra Chip #4" },

	{ 0x15409511, BTTV_BOARD_ACORP_Y878F, "Acorp Y878F" },

	{ 0x53534149, BTTV_BOARD_SSAI_SECURITY, "SSAI Security Video Interface" },
	{ 0x5353414a, BTTV_BOARD_SSAI_ULTRASOUND, "SSAI Ultrasound Video Interface" },

	/* likely broken, vendor id doesn't match the other magic views ...
	 * { 0xa0fca04f, BTTV_BOARD_MAGICTVIEW063, "Guillemot Maxi TV Video 3" }, */

	/* Duplicate PCI ID, reconfigure for this board during the eeprom read.
	* { 0x13eb0070, BTTV_BOARD_HAUPPAUGE_IMPACTVCB,  "Hauppauge ImpactVCB" }, */

	{ 0x109e036e, BTTV_BOARD_CONCEPTRONIC_CTVFMI2,	"Conceptronic CTVFMi v2"},

	/* DVB cards (using pci function .1 for mpeg data xfer) */
	{ 0x001c11bd, BTTV_BOARD_PINNACLESAT,   "Pinnacle PCTV Sat" },
	{ 0x01010071, BTTV_BOARD_NEBULA_DIGITV, "Nebula Electronics DigiTV" },
	{ 0x20007063, BTTV_BOARD_PC_HDTV,       "pcHDTV HD-2000 TV"},
	{ 0x002611bd, BTTV_BOARD_TWINHAN_DST,   "Pinnacle PCTV SAT CI" },
	{ 0x00011822, BTTV_BOARD_TWINHAN_DST,   "Twinhan VisionPlus DVB" },
	{ 0xfc00270f, BTTV_BOARD_TWINHAN_DST,   "ChainTech digitop DST-1000 DVB-S" },
	{ 0x07711461, BTTV_BOARD_AVDVBT_771,    "AVermedia AverTV DVB-T 771" },
	{ 0x07611461, BTTV_BOARD_AVDVBT_761,    "AverMedia AverTV DVB-T 761" },
	{ 0xdb1018ac, BTTV_BOARD_DVICO_DVBT_LITE,    "DViCO FusionHDTV DVB-T Lite" },
	{ 0xdb1118ac, BTTV_BOARD_DVICO_DVBT_LITE,    "Ultraview DVB-T Lite" },
	{ 0xd50018ac, BTTV_BOARD_DVICO_FUSIONHDTV_5_LITE,    "DViCO FusionHDTV 5 Lite" },
	{ 0x00261822, BTTV_BOARD_TWINHAN_DST,	"DNTV Live! Mini "},
	{ 0xd200dbc0, BTTV_BOARD_DVICO_FUSIONHDTV_2,	"DViCO FusionHDTV 2" },
	{ 0x763c008a, BTTV_BOARD_GEOVISION_GV600,	"GeoVision GV-600" },
	{ 0x18011000, BTTV_BOARD_ENLTV_FM_2,	"Encore ENL TV-FM-2" },
	{ 0x763d800a, BTTV_BOARD_GEOVISION_GV800S, "GeoVision GV-800(S) (master)" },
	{ 0x763d800b, BTTV_BOARD_GEOVISION_GV800S_SL,	"GeoVision GV-800(S) (slave)" },
	{ 0x763d800c, BTTV_BOARD_GEOVISION_GV800S_SL,	"GeoVision GV-800(S) (slave)" },
	{ 0x763d800d, BTTV_BOARD_GEOVISION_GV800S_SL,	"GeoVision GV-800(S) (slave)" },

	{ 0x15401830, BTTV_BOARD_PV183,         "Provideo PV183-1" },
	{ 0x15401831, BTTV_BOARD_PV183,         "Provideo PV183-2" },
	{ 0x15401832, BTTV_BOARD_PV183,         "Provideo PV183-3" },
	{ 0x15401833, BTTV_BOARD_PV183,         "Provideo PV183-4" },
	{ 0x15401834, BTTV_BOARD_PV183,         "Provideo PV183-5" },
	{ 0x15401835, BTTV_BOARD_PV183,         "Provideo PV183-6" },
	{ 0x15401836, BTTV_BOARD_PV183,         "Provideo PV183-7" },
	{ 0x15401837, BTTV_BOARD_PV183,         "Provideo PV183-8" },
	{ 0x3116f200, BTTV_BOARD_TVT_TD3116,	"Tongwei Video Technology TD-3116" },
	{ 0x02280279, BTTV_BOARD_APOSONIC_WDVR, "Aposonic W-DVR" },
	{ 0, -1, NULL }
};

/* ----------------------------------------------------------------------- */
/* array with description for bt848 / bt878 tv/grabber cards               */

struct tvcard bttv_tvcards[] = {
	/* ---- card 0x00 ---------------------------------- */
	[BTTV_BOARD_UNKNOWN] = {
		.name		= " *** UNKNOWN/GENERIC *** ",
		.video_inputs	= 4,
		.svhs		= 2,
		.muxsel		= MUXSEL(2, 3, 1, 0),
		.tuner_type	= UNSET,
		.tuner_addr	= ADDR_UNSET,
	},
	[BTTV_BOARD_MIRO] = {
		.name		= "MIRO PCTV",
		.video_inputs	= 4,
		/* .audio_inputs= 1, */
		.svhs		= 2,
		.gpiomask	= 15,
		.muxsel		= MUXSEL(2, 3, 1, 1),
		.gpiomux 	= { 2, 0, 0, 0 },
		.gpiomute 	= 10,
		.tuner_type	= UNSET,
		.tuner_addr	= ADDR_UNSET,
	},
	[BTTV_BOARD_HAUPPAUGE] = {
		.name		= "Hauppauge (bt848)",
		.video_inputs	= 4,
		/* .audio_inputs= 1, */
		.svhs		= 2,
		.gpiomask	= 7,
		.muxsel		= MUXSEL(2, 3, 1, 1),
		.gpiomux 	= { 0, 1, 2, 3 },
		.gpiomute 	= 4,
		.tuner_type	= UNSET,
		.tuner_addr	= ADDR_UNSET,
	},
	[BTTV_BOARD_STB] = {
		.name		= "STB, Gateway P/N 6000699 (bt848)",
		.video_inputs	= 3,
		/* .audio_inputs= 1, */
		.svhs		= 2,
		.gpiomask	= 7,
		.muxsel		= MUXSEL(2, 3, 1, 1),
		.gpiomux 	= { 4, 0, 2, 3 },
		.gpiomute 	= 1,
		.no_msp34xx	= 1,
		.tuner_type     = TUNER_PHILIPS_NTSC,
		.tuner_addr	= ADDR_UNSET,
		.pll            = PLL_28,
		.has_radio      = 1,
	},

	/* ---- card 0x04 ---------------------------------- */
	[BTTV_BOARD_INTEL] = {
		.name		= "Intel Create and Share PCI/ Smart Video Recorder III",
		.video_inputs	= 4,
		/* .audio_inputs= 0, */
		.svhs		= 2,
		.gpiomask	= 0,
		.muxsel		= MUXSEL(2, 3, 1, 1),
		.gpiomux 	= { 0 },
		.tuner_type	= TUNER_ABSENT,
		.tuner_addr	= ADDR_UNSET,
	},
	[BTTV_BOARD_DIAMOND] = {
		.name		= "Diamond DTV2000",
		.video_inputs	= 4,
		/* .audio_inputs= 1, */
		.svhs		= 2,
		.gpiomask	= 3,
		.muxsel		= MUXSEL(2, 3, 1, 0),
		.gpiomux 	= { 0, 1, 0, 1 },
		.gpiomute 	= 3,
		.tuner_type	= UNSET,
		.tuner_addr	= ADDR_UNSET,
	},
	[BTTV_BOARD_AVERMEDIA] = {
		.name		= "AVerMedia TVPhone",
		.video_inputs	= 3,
		/* .audio_inputs= 1, */
		.svhs		= 3,
		.muxsel		= MUXSEL(2, 3, 1, 1),
		.gpiomask	= 0x0f,
		.gpiomux 	= { 0x0c, 0x04, 0x08, 0x04 },
		/*                0x04 for some cards ?? */
		.tuner_type	= UNSET,
		.tuner_addr	= ADDR_UNSET,
		.audio_mode_gpio= avermedia_tvphone_audio,
		.has_remote     = 1,
	},
	[BTTV_BOARD_MATRIX_VISION] = {
		.name		= "MATRIX-Vision MV-Delta",
		.video_inputs	= 5,
		/* .audio_inputs= 1, */
		.svhs		= 3,
		.gpiomask	= 0,
		.muxsel		= MUXSEL(2, 3, 1, 0, 0),
		.gpiomux 	= { 0 },
		.tuner_type	= TUNER_ABSENT,
		.tuner_addr	= ADDR_UNSET,
	},

	/* ---- card 0x08 ---------------------------------- */
	[BTTV_BOARD_FLYVIDEO] = {
		.name		= "Lifeview FlyVideo II (Bt848) LR26 / MAXI TV Video PCI2 LR26",
		.video_inputs	= 4,
		/* .audio_inputs= 1, */
		.svhs		= 2,
		.gpiomask	= 0xc00,
		.muxsel		= MUXSEL(2, 3, 1, 1),
		.gpiomux 	= { 0, 0xc00, 0x800, 0x400 },
		.gpiomute 	= 0xc00,
		.pll		= PLL_28,
		.tuner_type	= UNSET,
		.tuner_addr	= ADDR_UNSET,
	},
	[BTTV_BOARD_TURBOTV] = {
		.name		= "IMS/IXmicro TurboTV",
		.video_inputs	= 3,
		/* .audio_inputs= 1, */
		.svhs		= 2,
		.gpiomask	= 3,
		.muxsel		= MUXSEL(2, 3, 1, 1),
		.gpiomux 	= { 1, 1, 2, 3 },
		.pll		= PLL_28,
		.tuner_type	= TUNER_TEMIC_PAL,
		.tuner_addr	= ADDR_UNSET,
	},
	[BTTV_BOARD_HAUPPAUGE878] = {
		.name		= "Hauppauge (bt878)",
		.video_inputs	= 4,
		/* .audio_inputs= 1, */
		.svhs		= 2,
		.gpiomask	= 0x0f, /* old: 7 */
		.muxsel		= MUXSEL(2, 0, 1, 1),
		.gpiomux 	= { 0, 1, 2, 3 },
		.gpiomute 	= 4,
		.pll		= PLL_28,
		.tuner_type	= UNSET,
		.tuner_addr	= ADDR_UNSET,
	},
	[BTTV_BOARD_MIROPRO] = {
		.name		= "MIRO PCTV pro",
		.video_inputs	= 3,
		/* .audio_inputs= 1, */
		.svhs		= 2,
		.gpiomask	= 0x3014f,
		.muxsel		= MUXSEL(2, 3, 1, 1),
		.gpiomux 	= { 0x20001,0x10001, 0, 0 },
		.gpiomute 	= 10,
		.tuner_type	= UNSET,
		.tuner_addr	= ADDR_UNSET,
	},

	/* ---- card 0x0c ---------------------------------- */
	[BTTV_BOARD_ADSTECH_TV] = {
		.name		= "ADS Technologies Channel Surfer TV (bt848)",
		.video_inputs	= 3,
		/* .audio_inputs= 1, */
		.svhs		= 2,
		.gpiomask	= 15,
		.muxsel		= MUXSEL(2, 3, 1, 1),
		.gpiomux 	= { 13, 14, 11, 7 },
		.tuner_type	= UNSET,
		.tuner_addr	= ADDR_UNSET,
	},
	[BTTV_BOARD_AVERMEDIA98] = {
		.name		= "AVerMedia TVCapture 98",
		.video_inputs	= 3,
		/* .audio_inputs= 4, */
		.svhs		= 2,
		.gpiomask	= 15,
		.muxsel		= MUXSEL(2, 3, 1, 1),
		.gpiomux 	= { 13, 14, 11, 7 },
		.msp34xx_alt    = 1,
		.pll		= PLL_28,
		.tuner_type	= TUNER_PHILIPS_PAL,
		.tuner_addr	= ADDR_UNSET,
		.audio_mode_gpio= avermedia_tv_stereo_audio,
		.no_gpioirq     = 1,
	},
	[BTTV_BOARD_VHX] = {
		.name		= "Aimslab Video Highway Xtreme (VHX)",
		.video_inputs	= 3,
		/* .audio_inputs= 1, */
		.svhs		= 2,
		.gpiomask	= 7,
		.muxsel		= MUXSEL(2, 3, 1, 1),
		.gpiomux 	= { 0, 2, 1, 3 }, /* old: {0, 1, 2, 3, 4} */
		.gpiomute 	= 4,
		.pll		= PLL_28,
		.tuner_type	= UNSET,
		.tuner_addr	= ADDR_UNSET,
	},
	[BTTV_BOARD_ZOLTRIX] = {
		.name		= "Zoltrix TV-Max",
		.video_inputs	= 3,
		/* .audio_inputs= 1, */
		.svhs		= 2,
		.gpiomask	= 15,
		.muxsel		= MUXSEL(2, 3, 1, 1),
		.gpiomux 	= { 0, 0, 1, 0 },
		.gpiomute 	= 10,
		.tuner_type	= UNSET,
		.tuner_addr	= ADDR_UNSET,
	},

	/* ---- card 0x10 ---------------------------------- */
	[BTTV_BOARD_PIXVIEWPLAYTV] = {
		.name		= "Prolink Pixelview PlayTV (bt878)",
		.video_inputs	= 3,
		/* .audio_inputs= 1, */
		.svhs		= 2,
		.gpiomask	= 0x01fe00,
		.muxsel		= MUXSEL(2, 3, 1, 1),
		/* 2003-10-20 by "Anton A. Arapov" <arapov@mail.ru> */
		.gpiomux        = { 0x001e00, 0, 0x018000, 0x014000 },
		.gpiomute 	= 0x002000,
		.pll		= PLL_28,
		.tuner_type	= UNSET,
		.tuner_addr     = ADDR_UNSET,
	},
	[BTTV_BOARD_WINVIEW_601] = {
		.name		= "Leadtek WinView 601",
		.video_inputs	= 3,
		/* .audio_inputs= 1, */
		.svhs		= 2,
		.gpiomask	= 0x8300f8,
		.muxsel		= MUXSEL(2, 3, 1, 1, 0),
		.gpiomux 	= { 0x4fa007,0xcfa007,0xcfa007,0xcfa007 },
		.gpiomute 	= 0xcfa007,
		.tuner_type	= UNSET,
		.tuner_addr	= ADDR_UNSET,
		.volume_gpio	= winview_volume,
		.has_radio	= 1,
	},
	[BTTV_BOARD_AVEC_INTERCAP] = {
		.name		= "AVEC Intercapture",
		.video_inputs	= 3,
		/* .audio_inputs= 2, */
		.svhs		= 2,
		.gpiomask	= 0,
		.muxsel		= MUXSEL(2, 3, 1, 1),
		.gpiomux 	= { 1, 0, 0, 0 },
		.tuner_type	= UNSET,
		.tuner_addr	= ADDR_UNSET,
	},
	[BTTV_BOARD_LIFE_FLYKIT] = {
		.name		= "Lifeview FlyVideo II EZ /FlyKit LR38 Bt848 (capture only)",
		.video_inputs	= 4,
		/* .audio_inputs= 1, */
		.svhs		= NO_SVHS,
		.gpiomask	= 0x8dff00,
		.muxsel		= MUXSEL(2, 3, 1, 1),
		.gpiomux 	= { 0 },
		.no_msp34xx	= 1,
		.tuner_type	= TUNER_ABSENT,
		.tuner_addr	= ADDR_UNSET,
	},

	/* ---- card 0x14 ---------------------------------- */
	[BTTV_BOARD_CEI_RAFFLES] = {
		.name		= "CEI Raffles Card",
		.video_inputs	= 3,
		/* .audio_inputs= 3, */
		.svhs		= 2,
		.muxsel		= MUXSEL(2, 3, 1, 1),
		.tuner_type	= UNSET,
		.tuner_addr	= ADDR_UNSET,
	},
	[BTTV_BOARD_CONFERENCETV] = {
		.name		= "Lifeview FlyVideo 98/ Lucky Star Image World ConferenceTV LR50",
		.video_inputs	= 4,
		/* .audio_inputs= 2,  tuner, line in */
		.svhs		= 2,
		.gpiomask	= 0x1800,
		.muxsel		= MUXSEL(2, 3, 1, 1),
		.gpiomux 	= { 0, 0x800, 0x1000, 0x1000 },
		.gpiomute 	= 0x1800,
		.pll		= PLL_28,
		.tuner_type	= TUNER_PHILIPS_PAL_I,
		.tuner_addr	= ADDR_UNSET,
	},
	[BTTV_BOARD_PHOEBE_TVMAS] = {
		.name		= "Askey CPH050/ Phoebe Tv Master + FM",
		.video_inputs	= 3,
		/* .audio_inputs= 1, */
		.svhs		= 2,
		.gpiomask	= 0xc00,
		.muxsel		= MUXSEL(2, 3, 1, 1),
		.gpiomux 	= { 0, 1, 0x800, 0x400 },
		.gpiomute 	= 0xc00,
		.pll		= PLL_28,
		.tuner_type	= UNSET,
		.tuner_addr	= ADDR_UNSET,
	},
	[BTTV_BOARD_MODTEC_205] = {
		.name		= "Modular Technology MM201/MM202/MM205/MM210/MM215 PCTV, bt878",
		.video_inputs	= 3,
		/* .audio_inputs= 1, */
		.svhs		= NO_SVHS,
		.has_dig_in	= 1,
		.gpiomask	= 7,
		.muxsel		= MUXSEL(2, 3, 0), /* input 2 is digital */
		/* .digital_mode= DIGITAL_MODE_CAMERA, */
		.gpiomux 	= { 0, 0, 0, 0 },
		.no_msp34xx	= 1,
		.pll            = PLL_28,
		.tuner_type     = TUNER_ALPS_TSBB5_PAL_I,
		.tuner_addr	= ADDR_UNSET,
	},

	/* ---- card 0x18 ---------------------------------- */
	[BTTV_BOARD_MAGICTVIEW061] = {
		.name		= "Askey CPH05X/06X (bt878) [many vendors]",
		.video_inputs	= 3,
		/* .audio_inputs= 1, */
		.svhs		= 2,
		.gpiomask	= 0xe00,
		.muxsel		= MUXSEL(2, 3, 1, 1),
		.gpiomux 	= {0x400, 0x400, 0x400, 0x400 },
		.gpiomute 	= 0xc00,
		.pll		= PLL_28,
		.tuner_type	= UNSET,
		.tuner_addr	= ADDR_UNSET,
		.has_remote     = 1,
		.has_radio	= 1,  /* not every card has radio */
	},
	[BTTV_BOARD_VOBIS_BOOSTAR] = {
		.name           = "Terratec TerraTV+ Version 1.0 (Bt848)/ Terra TValue Version 1.0/ Vobis TV-Boostar",
		.video_inputs	= 3,
		/* .audio_inputs= 1, */
		.svhs		= 2,
		.gpiomask       = 0x1f0fff,
		.muxsel		= MUXSEL(2, 3, 1, 1),
		.gpiomux        = { 0x20000, 0x30000, 0x10000, 0 },
		.gpiomute 	= 0x40000,
		.tuner_type	= TUNER_PHILIPS_PAL,
		.tuner_addr	= ADDR_UNSET,
		.audio_mode_gpio= terratv_audio,
	},
	[BTTV_BOARD_HAUPPAUG_WCAM] = {
		.name		= "Hauppauge WinCam newer (bt878)",
		.video_inputs	= 4,
		/* .audio_inputs= 1, */
		.svhs		= 3,
		.gpiomask	= 7,
		.muxsel		= MUXSEL(2, 0, 1, 1),
		.gpiomux 	= { 0, 1, 2, 3 },
		.gpiomute 	= 4,
		.tuner_type	= UNSET,
		.tuner_addr	= ADDR_UNSET,
	},
	[BTTV_BOARD_MAXI] = {
		.name		= "Lifeview FlyVideo 98/ MAXI TV Video PCI2 LR50",
		.video_inputs	= 4,
		/* .audio_inputs= 2, */
		.svhs		= 2,
		.gpiomask	= 0x1800,
		.muxsel		= MUXSEL(2, 3, 1, 1),
		.gpiomux 	= { 0, 0x800, 0x1000, 0x1000 },
		.gpiomute 	= 0x1800,
		.pll            = PLL_28,
		.tuner_type	= TUNER_PHILIPS_SECAM,
		.tuner_addr	= ADDR_UNSET,
	},

	/* ---- card 0x1c ---------------------------------- */
	[BTTV_BOARD_TERRATV] = {
		.name           = "Terratec TerraTV+ Version 1.1 (bt878)",
		.video_inputs	= 3,
		/* .audio_inputs= 1, */
		.svhs		= 2,
		.gpiomask	= 0x1f0fff,
		.muxsel		= MUXSEL(2, 3, 1, 1),
		.gpiomux 	= { 0x20000, 0x30000, 0x10000, 0x00000 },
		.gpiomute 	= 0x40000,
		.tuner_type	= TUNER_PHILIPS_PAL,
		.tuner_addr	= ADDR_UNSET,
		.audio_mode_gpio= terratv_audio,
		/* GPIO wiring:
		External 20 pin connector (for Active Radio Upgrade board)
		gpio00: i2c-sda
		gpio01: i2c-scl
		gpio02: om5610-data
		gpio03: om5610-clk
		gpio04: om5610-wre
		gpio05: om5610-stereo
		gpio06: rds6588-davn
		gpio07: Pin 7 n.c.
		gpio08: nIOW
		gpio09+10: nIOR, nSEL ?? (bt878)
			gpio09: nIOR (bt848)
			gpio10: nSEL (bt848)
		Sound Routing:
		gpio16: u2-A0 (1st 4052bt)
		gpio17: u2-A1
		gpio18: u2-nEN
		gpio19: u4-A0 (2nd 4052)
		gpio20: u4-A1
			u4-nEN - GND
		Btspy:
			00000 : Cdrom (internal audio input)
			10000 : ext. Video audio input
			20000 : TV Mono
			a0000 : TV Mono/2
		1a0000 : TV Stereo
			30000 : Radio
			40000 : Mute
	*/

	},
	[BTTV_BOARD_PXC200] = {
		/* Jannik Fritsch <jannik@techfak.uni-bielefeld.de> */
		.name		= "Imagenation PXC200",
		.video_inputs	= 5,
		/* .audio_inputs= 1, */
		.svhs		= 1, /* was: 4 */
		.gpiomask	= 0,
		.muxsel		= MUXSEL(2, 3, 1, 0, 0),
		.gpiomux 	= { 0 },
		.tuner_type	= TUNER_ABSENT,
		.tuner_addr	= ADDR_UNSET,
		.muxsel_hook    = PXC200_muxsel,

	},
	[BTTV_BOARD_FLYVIDEO_98] = {
		.name		= "Lifeview FlyVideo 98 LR50",
		.video_inputs	= 4,
		/* .audio_inputs= 1, */
		.svhs		= 2,
		.gpiomask	= 0x1800,  /* 0x8dfe00 */
		.muxsel		= MUXSEL(2, 3, 1, 1),
		.gpiomux 	= { 0, 0x0800, 0x1000, 0x1000 },
		.gpiomute 	= 0x1800,
		.pll            = PLL_28,
		.tuner_type	= UNSET,
		.tuner_addr	= ADDR_UNSET,
	},
	[BTTV_BOARD_IPROTV] = {
		.name		= "Formac iProTV, Formac ProTV I (bt848)",
		.video_inputs	= 4,
		/* .audio_inputs= 1, */
		.svhs		= 3,
		.gpiomask	= 1,
		.muxsel		= MUXSEL(2, 3, 1, 1),
		.gpiomux 	= { 1, 0, 0, 0 },
		.pll            = PLL_28,
		.tuner_type	= TUNER_PHILIPS_PAL,
		.tuner_addr	= ADDR_UNSET,
	},

	/* ---- card 0x20 ---------------------------------- */
	[BTTV_BOARD_INTEL_C_S_PCI] = {
		.name		= "Intel Create and Share PCI/ Smart Video Recorder III",
		.video_inputs	= 4,
		/* .audio_inputs= 0, */
		.svhs		= 2,
		.gpiomask	= 0,
		.muxsel		= MUXSEL(2, 3, 1, 1),
		.gpiomux 	= { 0 },
		.tuner_type	= TUNER_ABSENT,
		.tuner_addr	= ADDR_UNSET,
	},
	[BTTV_BOARD_TERRATVALUE] = {
		.name           = "Terratec TerraTValue Version Bt878",
		.video_inputs	= 3,
		/* .audio_inputs= 1, */
		.svhs		= 2,
		.gpiomask	= 0xffff00,
		.muxsel		= MUXSEL(2, 3, 1, 1),
		.gpiomux 	= { 0x500, 0, 0x300, 0x900 },
		.gpiomute 	= 0x900,
		.pll		= PLL_28,
		.tuner_type	= TUNER_PHILIPS_PAL,
		.tuner_addr	= ADDR_UNSET,
	},
	[BTTV_BOARD_WINFAST2000] = {
		.name		= "Leadtek WinFast 2000/ WinFast 2000 XP",
		.video_inputs	= 4,
		/* .audio_inputs= 1, */
		.svhs		= 2,
		/* TV, CVid, SVid, CVid over SVid connector */
		.muxsel		= MUXSEL(2, 3, 1, 1, 0),
		/* Alexander Varakin <avarakin@hotmail.com> [stereo version] */
		.gpiomask	= 0xb33000,
		.gpiomux 	= { 0x122000,0x1000,0x0000,0x620000 },
		.gpiomute 	= 0x800000,
		/* Audio Routing for "WinFast 2000 XP" (no tv stereo !)
			gpio23 -- hef4052:nEnable (0x800000)
			gpio12 -- hef4052:A1
			gpio13 -- hef4052:A0
		0x0000: external audio
		0x1000: FM
		0x2000: TV
		0x3000: n.c.
		Note: There exists another variant "Winfast 2000" with tv stereo !?
		Note: eeprom only contains FF and pci subsystem id 107d:6606
		*/
		.pll		= PLL_28,
		.has_radio	= 1,
		.tuner_type	= TUNER_PHILIPS_PAL, /* default for now, gpio reads BFFF06 for Pal bg+dk */
		.tuner_addr	= ADDR_UNSET,
		.audio_mode_gpio= winfast2000_audio,
		.has_remote     = 1,
	},
	[BTTV_BOARD_CHRONOS_VS2] = {
		.name		= "Lifeview FlyVideo 98 LR50 / Chronos Video Shuttle II",
		.video_inputs	= 4,
		/* .audio_inputs= 3, */
		.svhs		= 2,
		.gpiomask	= 0x1800,
		.muxsel		= MUXSEL(2, 3, 1, 1),
		.gpiomux 	= { 0, 0x800, 0x1000, 0x1000 },
		.gpiomute 	= 0x1800,
		.pll		= PLL_28,
		.tuner_type	= UNSET,
		.tuner_addr	= ADDR_UNSET,
	},

	/* ---- card 0x24 ---------------------------------- */
	[BTTV_BOARD_TYPHOON_TVIEW] = {
		.name		= "Lifeview FlyVideo 98FM LR50 / Typhoon TView TV/FM Tuner",
		.video_inputs	= 4,
		/* .audio_inputs= 3, */
		.svhs		= 2,
		.gpiomask	= 0x1800,
		.muxsel		= MUXSEL(2, 3, 1, 1),
		.gpiomux 	= { 0, 0x800, 0x1000, 0x1000 },
		.gpiomute 	= 0x1800,
		.pll		= PLL_28,
		.tuner_type	= UNSET,
		.tuner_addr	= ADDR_UNSET,
		.has_radio	= 1,
	},
	[BTTV_BOARD_PXELVWPLTVPRO] = {
		.name		= "Prolink PixelView PlayTV pro",
		.video_inputs	= 3,
		/* .audio_inputs= 1, */
		.svhs		= 2,
		.gpiomask	= 0xff,
		.muxsel		= MUXSEL(2, 3, 1, 1),
		.gpiomux 	= { 0x21, 0x20, 0x24, 0x2c },
		.gpiomute 	= 0x29,
		.no_msp34xx	= 1,
		.pll		= PLL_28,
		.tuner_type	= UNSET,
		.tuner_addr	= ADDR_UNSET,
	},
	[BTTV_BOARD_MAGICTVIEW063] = {
		.name		= "Askey CPH06X TView99",
		.video_inputs	= 4,
		/* .audio_inputs= 1, */
		.svhs		= 2,
		.gpiomask	= 0x551e00,
		.muxsel		= MUXSEL(2, 3, 1, 0),
		.gpiomux 	= { 0x551400, 0x551200, 0, 0 },
		.gpiomute 	= 0x551c00,
		.pll		= PLL_28,
		.tuner_type	= TUNER_PHILIPS_PAL_I,
		.tuner_addr	= ADDR_UNSET,
		.has_remote     = 1,
	},
	[BTTV_BOARD_PINNACLE] = {
		.name		= "Pinnacle PCTV Studio/Rave",
		.video_inputs	= 3,
		/* .audio_inputs= 1, */
		.svhs		= 2,
		.gpiomask	= 0x03000F,
		.muxsel		= MUXSEL(2, 3, 1, 1),
		.gpiomux 	= { 2, 0xd0001, 0, 0 },
		.gpiomute 	= 1,
		.pll		= PLL_28,
		.tuner_type	= UNSET,
		.tuner_addr	= ADDR_UNSET,
	},

	/* ---- card 0x28 ---------------------------------- */
	[BTTV_BOARD_STB2] = {
		.name		= "STB TV PCI FM, Gateway P/N 6000704 (bt878), 3Dfx VoodooTV 100",
		.video_inputs	= 3,
		/* .audio_inputs= 1, */
		.svhs		= 2,
		.gpiomask	= 7,
		.muxsel		= MUXSEL(2, 3, 1, 1),
		.gpiomux 	= { 4, 0, 2, 3 },
		.gpiomute 	= 1,
		.no_msp34xx	= 1,
		.tuner_type     = TUNER_PHILIPS_NTSC,
		.tuner_addr	= ADDR_UNSET,
		.pll            = PLL_28,
		.has_radio      = 1,
	},
	[BTTV_BOARD_AVPHONE98] = {
		.name		= "AVerMedia TVPhone 98",
		.video_inputs	= 3,
		/* .audio_inputs= 4, */
		.svhs		= 2,
		.gpiomask	= 15,
		.muxsel		= MUXSEL(2, 3, 1, 1),
		.gpiomux 	= { 13, 4, 11, 7 },
		.pll		= PLL_28,
		.tuner_type	= UNSET,
		.tuner_addr	= ADDR_UNSET,
		.has_radio	= 1,
		.audio_mode_gpio= avermedia_tvphone_audio,
	},
	[BTTV_BOARD_PV951] = {
		.name		= "ProVideo PV951", /* pic16c54 */
		.video_inputs	= 3,
		/* .audio_inputs= 1, */
		.svhs		= 2,
		.gpiomask	= 0,
		.muxsel		= MUXSEL(2, 3, 1, 1),
		.gpiomux 	= { 0, 0, 0, 0},
		.no_msp34xx	= 1,
		.pll		= PLL_28,
		.tuner_type	= TUNER_PHILIPS_PAL_I,
		.tuner_addr	= ADDR_UNSET,
	},
	[BTTV_BOARD_ONAIR_TV] = {
		.name		= "Little OnAir TV",
		.video_inputs	= 3,
		/* .audio_inputs= 1, */
		.svhs		= 2,
		.gpiomask	= 0xe00b,
		.muxsel		= MUXSEL(2, 3, 1, 1),
		.gpiomux 	= { 0xff9ff6, 0xff9ff6, 0xff1ff7, 0 },
		.gpiomute 	= 0xff3ffc,
		.no_msp34xx	= 1,
		.tuner_type	= UNSET,
		.tuner_addr	= ADDR_UNSET,
	},

	/* ---- card 0x2c ---------------------------------- */
	[BTTV_BOARD_SIGMA_TVII_FM] = {
		.name		= "Sigma TVII-FM",
		.video_inputs	= 2,
		/* .audio_inputs= 1, */
		.svhs		= NO_SVHS,
		.gpiomask	= 3,
		.muxsel		= MUXSEL(2, 3, 1, 1),
		.gpiomux 	= { 1, 1, 0, 2 },
		.gpiomute 	= 3,
		.no_msp34xx	= 1,
		.pll		= PLL_NONE,
		.tuner_type	= UNSET,
		.tuner_addr	= ADDR_UNSET,
	},
	[BTTV_BOARD_MATRIX_VISION2] = {
		.name		= "MATRIX-Vision MV-Delta 2",
		.video_inputs	= 5,
		/* .audio_inputs= 1, */
		.svhs		= 3,
		.gpiomask	= 0,
		.muxsel		= MUXSEL(2, 3, 1, 0, 0),
		.gpiomux 	= { 0 },
		.no_msp34xx	= 1,
		.pll		= PLL_28,
		.tuner_type	= TUNER_ABSENT,
		.tuner_addr	= ADDR_UNSET,
	},
	[BTTV_BOARD_ZOLTRIX_GENIE] = {
		.name		= "Zoltrix Genie TV/FM",
		.video_inputs	= 3,
		/* .audio_inputs= 1, */
		.svhs		= 2,
		.gpiomask	= 0xbcf03f,
		.muxsel		= MUXSEL(2, 3, 1, 1),
		.gpiomux 	= { 0xbc803f, 0xbc903f, 0xbcb03f, 0 },
		.gpiomute 	= 0xbcb03f,
		.no_msp34xx	= 1,
		.pll		= PLL_28,
		.tuner_type	= TUNER_TEMIC_4039FR5_NTSC,
		.tuner_addr	= ADDR_UNSET,
	},
	[BTTV_BOARD_TERRATVRADIO] = {
		.name		= "Terratec TV/Radio+",
		.video_inputs	= 3,
		/* .audio_inputs= 1, */
		.svhs		= 2,
		.gpiomask	= 0x70000,
		.muxsel		= MUXSEL(2, 3, 1, 1),
		.gpiomux 	= { 0x20000, 0x30000, 0x10000, 0 },
		.gpiomute 	= 0x40000,
		.no_msp34xx	= 1,
		.pll		= PLL_35,
		.tuner_type	= TUNER_PHILIPS_PAL_I,
		.tuner_addr	= ADDR_UNSET,
		.has_radio	= 1,
	},

	/* ---- card 0x30 ---------------------------------- */
	[BTTV_BOARD_DYNALINK] = {
		.name		= "Askey CPH03x/ Dynalink Magic TView",
		.video_inputs	= 3,
		/* .audio_inputs= 1, */
		.svhs		= 2,
		.gpiomask	= 15,
		.muxsel		= MUXSEL(2, 3, 1, 1),
		.gpiomux 	= {2,0,0,0 },
		.gpiomute 	= 1,
		.pll		= PLL_28,
		.tuner_type	= UNSET,
		.tuner_addr	= ADDR_UNSET,
	},
	[BTTV_BOARD_GVBCTV3PCI] = {
		.name		= "IODATA GV-BCTV3/PCI",
		.video_inputs	= 3,
		/* .audio_inputs= 1, */
		.svhs		= 2,
		.gpiomask	= 0x010f00,
		.muxsel		= MUXSEL(2, 3, 0, 0),
		.gpiomux 	= {0x10000, 0, 0x10000, 0 },
		.no_msp34xx	= 1,
		.pll		= PLL_28,
		.tuner_type	= TUNER_ALPS_TSHC6_NTSC,
		.tuner_addr	= ADDR_UNSET,
		.audio_mode_gpio= gvbctv3pci_audio,
	},
	[BTTV_BOARD_PXELVWPLTVPAK] = {
		.name		= "Prolink PV-BT878P+4E / PixelView PlayTV PAK / Lenco MXTV-9578 CP",
		.video_inputs	= 5,
		/* .audio_inputs= 1, */
		.svhs		= 3,
		.has_dig_in	= 1,
		.gpiomask	= 0xAA0000,
		.muxsel		= MUXSEL(2, 3, 1, 1, 0), /* in 4 is digital */
		/* .digital_mode= DIGITAL_MODE_CAMERA, */
		.gpiomux 	= { 0x20000, 0, 0x80000, 0x80000 },
		.gpiomute 	= 0xa8000,
		.no_msp34xx	= 1,
		.pll		= PLL_28,
		.tuner_type	= TUNER_PHILIPS_PAL_I,
		.tuner_addr	= ADDR_UNSET,
		.has_remote	= 1,
		/* GPIO wiring: (different from Rev.4C !)
			GPIO17: U4.A0 (first hef4052bt)
			GPIO19: U4.A1
			GPIO20: U5.A1 (second hef4052bt)
			GPIO21: U4.nEN
			GPIO22: BT832 Reset Line
			GPIO23: A5,A0, U5,nEN
		Note: At i2c=0x8a is a Bt832 chip, which changes to 0x88 after being reset via GPIO22
		*/
	},
	[BTTV_BOARD_EAGLE] = {
		.name           = "Eagle Wireless Capricorn2 (bt878A)",
		.video_inputs   = 4,
		/* .audio_inputs= 1, */
		.svhs           = 2,
		.gpiomask       = 7,
		.muxsel         = MUXSEL(2, 0, 1, 1),
		.gpiomux        = { 0, 1, 2, 3 },
		.gpiomute 	= 4,
		.pll            = PLL_28,
		.tuner_type     = UNSET /* TUNER_ALPS_TMDH2_NTSC */,
		.tuner_addr	= ADDR_UNSET,
	},

	/* ---- card 0x34 ---------------------------------- */
	[BTTV_BOARD_PINNACLEPRO] = {
		/* David Härdeman <david@2gen.com> */
		.name           = "Pinnacle PCTV Studio Pro",
		.video_inputs   = 4,
		/* .audio_inputs= 1, */
		.svhs           = 3,
		.gpiomask       = 0x03000F,
		.muxsel		= MUXSEL(2, 3, 1, 1),
		.gpiomux 	= { 1, 0xd0001, 0, 0 },
		.gpiomute 	= 10,
				/* sound path (5 sources):
				MUX1 (mask 0x03), Enable Pin 0x08 (0=enable, 1=disable)
					0= ext. Audio IN
					1= from MUX2
					2= Mono TV sound from Tuner
					3= not connected
				MUX2 (mask 0x30000):
					0,2,3= from MSP34xx
					1= FM stereo Radio from Tuner */
		.pll            = PLL_28,
		.tuner_type     = UNSET,
		.tuner_addr	= ADDR_UNSET,
	},
	[BTTV_BOARD_TVIEW_RDS_FM] = {
		/* Claas Langbehn <claas@bigfoot.com>,
		Sven Grothklags <sven@upb.de> */
		.name		= "Typhoon TView RDS + FM Stereo / KNC1 TV Station RDS",
		.video_inputs	= 4,
		/* .audio_inputs= 3, */
		.svhs		= 2,
		.gpiomask	= 0x1c,
		.muxsel		= MUXSEL(2, 3, 1, 1),
		.gpiomux 	= { 0, 0, 0x10, 8 },
		.gpiomute 	= 4,
		.pll		= PLL_28,
		.tuner_type	= TUNER_PHILIPS_PAL,
		.tuner_addr	= ADDR_UNSET,
		.has_radio	= 1,
	},
	[BTTV_BOARD_LIFETEC_9415] = {
		/* Tim Röstermundt <rosterm@uni-muenster.de>
		in de.comp.os.unix.linux.hardware:
			options bttv card=0 pll=1 radio=1 gpiomask=0x18e0
			gpiomux =0x44c71f,0x44d71f,0,0x44d71f,0x44dfff
			options tuner type=5 */
		.name		= "Lifeview FlyVideo 2000 /FlyVideo A2/ Lifetec LT 9415 TV [LR90]",
		.video_inputs	= 4,
		/* .audio_inputs= 1, */
		.svhs		= 2,
		.gpiomask	= 0x18e0,
		.muxsel		= MUXSEL(2, 3, 1, 1),
		.gpiomux 	= { 0x0000,0x0800,0x1000,0x1000 },
		.gpiomute 	= 0x18e0,
			/* For cards with tda9820/tda9821:
				0x0000: Tuner normal stereo
				0x0080: Tuner A2 SAP (second audio program = Zweikanalton)
				0x0880: Tuner A2 stereo */
		.pll		= PLL_28,
		.tuner_type	= UNSET,
		.tuner_addr	= ADDR_UNSET,
	},
	[BTTV_BOARD_BESTBUY_EASYTV] = {
		/* Miguel Angel Alvarez <maacruz@navegalia.com>
		old Easy TV BT848 version (model CPH031) */
		.name           = "Askey CPH031/ BESTBUY Easy TV",
		.video_inputs	= 4,
		/* .audio_inputs= 1, */
		.svhs           = 2,
		.gpiomask       = 0xF,
		.muxsel         = MUXSEL(2, 3, 1, 0),
		.gpiomux        = { 2, 0, 0, 0 },
		.gpiomute 	= 10,
		.pll		= PLL_28,
		.tuner_type	= TUNER_TEMIC_PAL,
		.tuner_addr	= ADDR_UNSET,
	},

	/* ---- card 0x38 ---------------------------------- */
	[BTTV_BOARD_FLYVIDEO_98FM] = {
		/* Gordon Heydon <gjheydon@bigfoot.com ('98) */
		.name           = "Lifeview FlyVideo 98FM LR50",
		.video_inputs   = 4,
		/* .audio_inputs= 3, */
		.svhs           = 2,
		.gpiomask       = 0x1800,
		.muxsel         = MUXSEL(2, 3, 1, 1),
		.gpiomux        = { 0, 0x800, 0x1000, 0x1000 },
		.gpiomute 	= 0x1800,
		.pll            = PLL_28,
		.tuner_type     = TUNER_PHILIPS_PAL,
		.tuner_addr	= ADDR_UNSET,
	},
		/* This is the ultimate cheapo capture card
		* just a BT848A on a small PCB!
		* Steve Hosgood <steve@equiinet.com> */
	[BTTV_BOARD_GRANDTEC] = {
		.name           = "GrandTec 'Grand Video Capture' (Bt848)",
		.video_inputs   = 2,
		/* .audio_inputs= 0, */
		.svhs           = 1,
		.gpiomask       = 0,
		.muxsel         = MUXSEL(3, 1),
		.gpiomux        = { 0 },
		.no_msp34xx     = 1,
		.pll            = PLL_35,
		.tuner_type     = TUNER_ABSENT,
		.tuner_addr	= ADDR_UNSET,
	},
	[BTTV_BOARD_ASKEY_CPH060] = {
		/* Daniel Herrington <daniel.herrington@home.com> */
		.name           = "Askey CPH060/ Phoebe TV Master Only (No FM)",
		.video_inputs   = 3,
		/* .audio_inputs= 1, */
		.svhs           = 2,
		.gpiomask       = 0xe00,
		.muxsel         = MUXSEL(2, 3, 1, 1),
		.gpiomux        = { 0x400, 0x400, 0x400, 0x400 },
		.gpiomute 	= 0x800,
		.pll            = PLL_28,
		.tuner_type     = TUNER_TEMIC_4036FY5_NTSC,
		.tuner_addr	= ADDR_UNSET,
	},
	[BTTV_BOARD_ASKEY_CPH03X] = {
		/* Matti Mottus <mottus@physic.ut.ee> */
		.name		= "Askey CPH03x TV Capturer",
		.video_inputs	= 4,
		/* .audio_inputs= 1, */
		.svhs		= 2,
		.gpiomask       = 0x03000F,
		.muxsel		= MUXSEL(2, 3, 1, 0),
		.gpiomux        = { 2, 0, 0, 0 },
		.gpiomute 	= 1,
		.pll            = PLL_28,
		.tuner_type	= TUNER_TEMIC_PAL,
		.tuner_addr	= ADDR_UNSET,
		.has_remote	= 1,
	},

	/* ---- card 0x3c ---------------------------------- */
	[BTTV_BOARD_MM100PCTV] = {
		/* Philip Blundell <philb@gnu.org> */
		.name           = "Modular Technology MM100PCTV",
		.video_inputs   = 2,
		/* .audio_inputs= 2, */
		.svhs		= NO_SVHS,
		.gpiomask       = 11,
		.muxsel         = MUXSEL(2, 3, 1, 1),
		.gpiomux        = { 2, 0, 0, 1 },
		.gpiomute 	= 8,
		.pll            = PLL_35,
		.tuner_type     = TUNER_TEMIC_PAL,
		.tuner_addr	= ADDR_UNSET,
	},
	[BTTV_BOARD_GMV1] = {
		/* Adrian Cox <adrian@humboldt.co.uk */
		.name		= "AG Electronics GMV1",
		.video_inputs   = 2,
		/* .audio_inputs= 0, */
		.svhs		= 1,
		.gpiomask       = 0xF,
		.muxsel		= MUXSEL(2, 2),
		.gpiomux        = { },
		.no_msp34xx     = 1,
		.pll		= PLL_28,
		.tuner_type     = TUNER_ABSENT,
		.tuner_addr	= ADDR_UNSET,
	},
	[BTTV_BOARD_BESTBUY_EASYTV2] = {
		/* Miguel Angel Alvarez <maacruz@navegalia.com>
		new Easy TV BT878 version (model CPH061)
		special thanks to Informatica Mieres for providing the card */
		.name           = "Askey CPH061/ BESTBUY Easy TV (bt878)",
		.video_inputs	= 3,
		/* .audio_inputs= 2, */
		.svhs           = 2,
		.gpiomask       = 0xFF,
		.muxsel         = MUXSEL(2, 3, 1, 0),
		.gpiomux        = { 1, 0, 4, 4 },
		.gpiomute 	= 9,
		.pll		= PLL_28,
		.tuner_type	= TUNER_PHILIPS_PAL,
		.tuner_addr	= ADDR_UNSET,
	},
	[BTTV_BOARD_ATI_TVWONDER] = {
		/* Lukas Gebauer <geby@volny.cz> */
		.name		= "ATI TV-Wonder",
		.video_inputs	= 3,
		/* .audio_inputs= 1, */
		.svhs		= 2,
		.gpiomask	= 0xf03f,
		.muxsel		= MUXSEL(2, 3, 1, 0),
		.gpiomux 	= { 0xbffe, 0, 0xbfff, 0 },
		.gpiomute 	= 0xbffe,
		.pll		= PLL_28,
		.tuner_type	= TUNER_TEMIC_4006FN5_MULTI_PAL,
		.tuner_addr	= ADDR_UNSET,
	},

	/* ---- card 0x40 ---------------------------------- */
	[BTTV_BOARD_ATI_TVWONDERVE] = {
		/* Lukas Gebauer <geby@volny.cz> */
		.name		= "ATI TV-Wonder VE",
		.video_inputs	= 2,
		/* .audio_inputs= 1, */
		.svhs		= NO_SVHS,
		.gpiomask	= 1,
		.muxsel		= MUXSEL(2, 3, 0, 1),
		.gpiomux 	= { 0, 0, 1, 0 },
		.no_msp34xx	= 1,
		.pll		= PLL_28,
		.tuner_type	= TUNER_TEMIC_4006FN5_MULTI_PAL,
		.tuner_addr	= ADDR_UNSET,
	},
	[BTTV_BOARD_FLYVIDEO2000] = {
		/* DeeJay <deejay@westel900.net (2000S) */
		.name           = "Lifeview FlyVideo 2000S LR90",
		.video_inputs   = 3,
		/* .audio_inputs= 3, */
		.svhs           = 2,
		.gpiomask	= 0x18e0,
		.muxsel		= MUXSEL(2, 3, 0, 1),
				/* Radio changed from 1e80 to 0x800 to make
				FlyVideo2000S in .hu happy (gm)*/
				/* -dk-???: set mute=0x1800 for tda9874h daughterboard */
		.gpiomux 	= { 0x0000,0x0800,0x1000,0x1000 },
		.gpiomute 	= 0x1800,
		.audio_mode_gpio= fv2000s_audio,
		.no_msp34xx	= 1,
		.pll            = PLL_28,
		.tuner_type     = TUNER_PHILIPS_PAL,
		.tuner_addr	= ADDR_UNSET,
	},
	[BTTV_BOARD_TERRATVALUER] = {
		.name		= "Terratec TValueRadio",
		.video_inputs	= 3,
		/* .audio_inputs= 1, */
		.svhs		= 2,
		.gpiomask	= 0xffff00,
		.muxsel		= MUXSEL(2, 3, 1, 1),
		.gpiomux 	= { 0x500, 0x500, 0x300, 0x900 },
		.gpiomute 	= 0x900,
		.pll		= PLL_28,
		.tuner_type	= TUNER_PHILIPS_PAL,
		.tuner_addr	= ADDR_UNSET,
		.has_radio	= 1,
	},
	[BTTV_BOARD_GVBCTV4PCI] = {
		/* TANAKA Kei <peg00625@nifty.com> */
		.name           = "IODATA GV-BCTV4/PCI",
		.video_inputs   = 3,
		/* .audio_inputs= 1, */
		.svhs           = 2,
		.gpiomask       = 0x010f00,
		.muxsel         = MUXSEL(2, 3, 0, 0),
		.gpiomux        = {0x10000, 0, 0x10000, 0 },
		.no_msp34xx     = 1,
		.pll            = PLL_28,
		.tuner_type     = TUNER_SHARP_2U5JF5540_NTSC,
		.tuner_addr	= ADDR_UNSET,
		.audio_mode_gpio= gvbctv3pci_audio,
	},

	/* ---- card 0x44 ---------------------------------- */
	[BTTV_BOARD_VOODOOTV_FM] = {
		.name           = "3Dfx VoodooTV FM (Euro)",
		/* try "insmod msp3400 simple=0" if you have
		* sound problems with this card. */
		.video_inputs   = 4,
		/* .audio_inputs= 1, */
		.svhs           = NO_SVHS,
		.gpiomask       = 0x4f8a00,
		/* 0x100000: 1=MSP enabled (0=disable again)
		* 0x010000: Connected to "S0" on tda9880 (0=Pal/BG, 1=NTSC) */
		.gpiomux        = {0x947fff, 0x987fff,0x947fff,0x947fff },
		.gpiomute 	= 0x947fff,
		/* tvtuner, radio,   external,internal, mute,  stereo
		* tuner, Composit, SVid, Composit-on-Svid-adapter */
		.muxsel         = MUXSEL(2, 3, 0, 1),
		.tuner_type     = TUNER_MT2032,
		.tuner_addr	= ADDR_UNSET,
		.pll		= PLL_28,
		.has_radio	= 1,
	},
	[BTTV_BOARD_VOODOOTV_200] = {
		.name           = "VoodooTV 200 (USA)",
		/* try "insmod msp3400 simple=0" if you have
		* sound problems with this card. */
		.video_inputs   = 4,
		/* .audio_inputs= 1, */
		.svhs           = NO_SVHS,
		.gpiomask       = 0x4f8a00,
		/* 0x100000: 1=MSP enabled (0=disable again)
		* 0x010000: Connected to "S0" on tda9880 (0=Pal/BG, 1=NTSC) */
		.gpiomux        = {0x947fff, 0x987fff,0x947fff,0x947fff },
		.gpiomute 	= 0x947fff,
		/* tvtuner, radio,   external,internal, mute,  stereo
		* tuner, Composit, SVid, Composit-on-Svid-adapter */
		.muxsel         = MUXSEL(2, 3, 0, 1),
		.tuner_type     = TUNER_MT2032,
		.tuner_addr	= ADDR_UNSET,
		.pll		= PLL_28,
		.has_radio	= 1,
	},
	[BTTV_BOARD_AIMMS] = {
		/* Philip Blundell <pb@nexus.co.uk> */
		.name           = "Active Imaging AIMMS",
		.video_inputs   = 1,
		/* .audio_inputs= 0, */
		.tuner_type     = TUNER_ABSENT,
		.tuner_addr	= ADDR_UNSET,
		.pll            = PLL_28,
		.muxsel         = MUXSEL(2),
		.gpiomask       = 0
	},
	[BTTV_BOARD_PV_BT878P_PLUS] = {
		/* Tomasz Pyra <hellfire@sedez.iq.pl> */
		.name           = "Prolink Pixelview PV-BT878P+ (Rev.4C,8E)",
		.video_inputs   = 3,
		/* .audio_inputs= 4, */
		.svhs           = 2,
		.gpiomask       = 15,
		.muxsel         = MUXSEL(2, 3, 1, 1),
		.gpiomux        = { 0, 0, 11, 7 }, /* TV and Radio with same GPIO ! */
		.gpiomute 	= 13,
		.pll            = PLL_28,
		.tuner_type     = TUNER_LG_PAL_I_FM,
		.tuner_addr	= ADDR_UNSET,
		.has_remote     = 1,
		/* GPIO wiring:
			GPIO0: U4.A0 (hef4052bt)
			GPIO1: U4.A1
			GPIO2: U4.A1 (second hef4052bt)
			GPIO3: U4.nEN, U5.A0, A5.nEN
			GPIO8-15: vrd866b ?
		*/
	},
	[BTTV_BOARD_FLYVIDEO98EZ] = {
		.name		= "Lifeview FlyVideo 98EZ (capture only) LR51",
		.video_inputs	= 4,
		/* .audio_inputs= 0, */
		.svhs		= 2,
		/* AV1, AV2, SVHS, CVid adapter on SVHS */
		.muxsel		= MUXSEL(2, 3, 1, 1),
		.pll		= PLL_28,
		.no_msp34xx	= 1,
		.tuner_type	= TUNER_ABSENT,
		.tuner_addr	= ADDR_UNSET,
	},

	/* ---- card 0x48 ---------------------------------- */
	[BTTV_BOARD_PV_BT878P_9B] = {
		/* Dariusz Kowalewski <darekk@automex.pl> */
		.name		= "Prolink Pixelview PV-BT878P+9B (PlayTV Pro rev.9B FM+NICAM)",
		.video_inputs	= 4,
		/* .audio_inputs= 1, */
		.svhs		= 2,
		.gpiomask	= 0x3f,
		.muxsel		= MUXSEL(2, 3, 1, 1),
		.gpiomux 	= { 0x01, 0x00, 0x03, 0x03 },
		.gpiomute 	= 0x09,
		.no_msp34xx	= 1,
		.pll		= PLL_28,
		.tuner_type	= TUNER_PHILIPS_PAL,
		.tuner_addr	= ADDR_UNSET,
		.audio_mode_gpio= pvbt878p9b_audio, /* Note: not all cards have stereo */
		.has_radio	= 1,  /* Note: not all cards have radio */
		.has_remote     = 1,
		/* GPIO wiring:
			GPIO0: A0 hef4052
			GPIO1: A1 hef4052
			GPIO3: nEN hef4052
			GPIO8-15: vrd866b
			GPIO20,22,23: R30,R29,R28
		*/
	},
	[BTTV_BOARD_SENSORAY311_611] = {
		/* Clay Kunz <ckunz@mail.arc.nasa.gov> */
		/* you must jumper JP5 for the 311 card (PC/104+) to work */
		.name           = "Sensoray 311/611",
		.video_inputs   = 5,
		/* .audio_inputs= 0, */
		.svhs           = 4,
		.gpiomask       = 0,
		.muxsel         = MUXSEL(2, 3, 1, 0, 0),
		.gpiomux        = { 0 },
		.tuner_type     = TUNER_ABSENT,
		.tuner_addr	= ADDR_UNSET,
	},
	[BTTV_BOARD_RV605] = {
		/* Miguel Freitas <miguel@cetuc.puc-rio.br> */
		.name           = "RemoteVision MX (RV605)",
		.video_inputs   = 16,
		/* .audio_inputs= 0, */
		.svhs           = NO_SVHS,
		.gpiomask       = 0x00,
		.gpiomask2      = 0x07ff,
		.muxsel         = MUXSEL(3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3),
		.no_msp34xx     = 1,
		.tuner_type     = TUNER_ABSENT,
		.tuner_addr	= ADDR_UNSET,
		.muxsel_hook    = rv605_muxsel,
	},
	[BTTV_BOARD_POWERCLR_MTV878] = {
		.name           = "Powercolor MTV878/ MTV878R/ MTV878F",
		.video_inputs   = 3,
		/* .audio_inputs= 2, */
		.svhs           = 2,
		.gpiomask       = 0x1C800F,  /* Bit0-2: Audio select, 8-12:remote control 14:remote valid 15:remote reset */
		.muxsel         = MUXSEL(2, 1, 1),
		.gpiomux        = { 0, 1, 2, 2 },
		.gpiomute 	= 4,
		.tuner_type     = TUNER_PHILIPS_PAL,
		.tuner_addr	= ADDR_UNSET,
		.pll		= PLL_28,
		.has_radio	= 1,
	},

	/* ---- card 0x4c ---------------------------------- */
	[BTTV_BOARD_WINDVR] = {
		/* Masaki Suzuki <masaki@btree.org> */
		.name           = "Canopus WinDVR PCI (COMPAQ Presario 3524JP, 5112JP)",
		.video_inputs   = 3,
		/* .audio_inputs= 1, */
		.svhs           = 2,
		.gpiomask       = 0x140007,
		.muxsel         = MUXSEL(2, 3, 1, 1),
		.gpiomux        = { 0, 1, 2, 3 },
		.gpiomute 	= 4,
		.tuner_type     = TUNER_PHILIPS_NTSC,
		.tuner_addr	= ADDR_UNSET,
		.audio_mode_gpio= windvr_audio,
	},
	[BTTV_BOARD_GRANDTEC_MULTI] = {
		.name           = "GrandTec Multi Capture Card (Bt878)",
		.video_inputs   = 4,
		/* .audio_inputs= 0, */
		.svhs           = NO_SVHS,
		.gpiomask       = 0,
		.muxsel         = MUXSEL(2, 3, 1, 0),
		.gpiomux        = { 0 },
		.no_msp34xx     = 1,
		.pll            = PLL_28,
		.tuner_type     = TUNER_ABSENT,
		.tuner_addr	= ADDR_UNSET,
	},
	[BTTV_BOARD_KWORLD] = {
		.name           = "Jetway TV/Capture JW-TV878-FBK, Kworld KW-TV878RF",
		.video_inputs   = 4,
		/* .audio_inputs= 3, */
		.svhs           = 2,
		.gpiomask       = 7,
		/* Tuner, SVid, SVHS, SVid to SVHS connector */
		.muxsel         = MUXSEL(2, 3, 1, 1),
		.gpiomux        = { 0, 0, 4, 4 },/* Yes, this tuner uses the same audio output for TV and FM radio!
						* This card lacks external Audio In, so we mute it on Ext. & Int.
						* The PCB can take a sbx1637/sbx1673, wiring unknown.
						* This card lacks PCI subsystem ID, sigh.
						* gpiomux =1: lower volume, 2+3: mute
						* btwincap uses 0x80000/0x80003
						*/
		.gpiomute 	= 4,
		.no_msp34xx     = 1,
		.pll            = PLL_28,
		.tuner_type     = TUNER_PHILIPS_PAL,
		.tuner_addr	= ADDR_UNSET,
		/* Samsung TCPA9095PC27A (BG+DK), philips compatible, w/FM, stereo and
		radio signal strength indicators work fine. */
		.has_radio	= 1,
		/* GPIO Info:
			GPIO0,1:   HEF4052 A0,A1
			GPIO2:     HEF4052 nENABLE
			GPIO3-7:   n.c.
			GPIO8-13:  IRDC357 data0-5 (data6 n.c. ?) [chip not present on my card]
			GPIO14,15: ??
			GPIO16-21: n.c.
			GPIO22,23: ??
			??       : mtu8b56ep microcontroller for IR (GPIO wiring unknown)*/
	},
	[BTTV_BOARD_DSP_TCVIDEO] = {
		/* Arthur Tetzlaff-Deas, DSP Design Ltd <software@dspdesign.com> */
		.name           = "DSP Design TCVIDEO",
		.video_inputs   = 4,
		.svhs           = NO_SVHS,
		.muxsel         = MUXSEL(2, 3, 1, 0),
		.pll            = PLL_28,
		.tuner_type     = UNSET,
		.tuner_addr	= ADDR_UNSET,
	},

		/* ---- card 0x50 ---------------------------------- */
	[BTTV_BOARD_HAUPPAUGEPVR] = {
		.name           = "Hauppauge WinTV PVR",
		.video_inputs   = 4,
		/* .audio_inputs= 1, */
		.svhs           = 2,
		.muxsel         = MUXSEL(2, 0, 1, 1),
		.pll            = PLL_28,
		.tuner_type     = UNSET,
		.tuner_addr	= ADDR_UNSET,

		.gpiomask       = 7,
		.gpiomux        = {7},
	},
	[BTTV_BOARD_GVBCTV5PCI] = {
		.name           = "IODATA GV-BCTV5/PCI",
		.video_inputs   = 3,
		/* .audio_inputs= 1, */
		.svhs           = 2,
		.gpiomask       = 0x0f0f80,
		.muxsel         = MUXSEL(2, 3, 1, 0),
		.gpiomux        = {0x030000, 0x010000, 0, 0 },
		.gpiomute 	= 0x020000,
		.no_msp34xx     = 1,
		.pll            = PLL_28,
		.tuner_type     = TUNER_PHILIPS_NTSC_M,
		.tuner_addr	= ADDR_UNSET,
		.audio_mode_gpio= gvbctv5pci_audio,
		.has_radio      = 1,
	},
	[BTTV_BOARD_OSPREY1x0] = {
		.name           = "Osprey 100/150 (878)", /* 0x1(2|3)-45C6-C1 */
		.video_inputs   = 4,                  /* id-inputs-clock */
		/* .audio_inputs= 0, */
		.svhs           = 3,
		.muxsel         = MUXSEL(3, 2, 0, 1),
		.pll            = PLL_28,
		.tuner_type     = TUNER_ABSENT,
		.tuner_addr	= ADDR_UNSET,
		.no_msp34xx     = 1,
		.no_tda7432     = 1,
	},
	[BTTV_BOARD_OSPREY1x0_848] = {
		.name           = "Osprey 100/150 (848)", /* 0x04-54C0-C1 & older boards */
		.video_inputs   = 3,
		/* .audio_inputs= 0, */
		.svhs           = 2,
		.muxsel         = MUXSEL(2, 3, 1),
		.pll            = PLL_28,
		.tuner_type     = TUNER_ABSENT,
		.tuner_addr	= ADDR_UNSET,
		.no_msp34xx     = 1,
		.no_tda7432     = 1,
	},

		/* ---- card 0x54 ---------------------------------- */
	[BTTV_BOARD_OSPREY101_848] = {
		.name           = "Osprey 101 (848)", /* 0x05-40C0-C1 */
		.video_inputs   = 2,
		/* .audio_inputs= 0, */
		.svhs           = 1,
		.muxsel         = MUXSEL(3, 1),
		.pll            = PLL_28,
		.tuner_type     = TUNER_ABSENT,
		.tuner_addr	= ADDR_UNSET,
		.no_msp34xx     = 1,
		.no_tda7432     = 1,
	},
	[BTTV_BOARD_OSPREY1x1] = {
		.name           = "Osprey 101/151",       /* 0x1(4|5)-0004-C4 */
		.video_inputs   = 1,
		/* .audio_inputs= 0, */
		.svhs           = NO_SVHS,
		.muxsel         = MUXSEL(0),
		.pll            = PLL_28,
		.tuner_type     = TUNER_ABSENT,
		.tuner_addr	= ADDR_UNSET,
		.no_msp34xx     = 1,
		.no_tda7432     = 1,
	},
	[BTTV_BOARD_OSPREY1x1_SVID] = {
		.name           = "Osprey 101/151 w/ svid",  /* 0x(16|17|20)-00C4-C1 */
		.video_inputs   = 2,
		/* .audio_inputs= 0, */
		.svhs           = 1,
		.muxsel         = MUXSEL(0, 1),
		.pll            = PLL_28,
		.tuner_type     = TUNER_ABSENT,
		.tuner_addr	= ADDR_UNSET,
		.no_msp34xx     = 1,
		.no_tda7432     = 1,
	},
	[BTTV_BOARD_OSPREY2xx] = {
		.name           = "Osprey 200/201/250/251",  /* 0x1(8|9|E|F)-0004-C4 */
		.video_inputs   = 1,
		/* .audio_inputs= 1, */
		.svhs           = NO_SVHS,
		.muxsel         = MUXSEL(0),
		.pll            = PLL_28,
		.tuner_type	= TUNER_ABSENT,
		.tuner_addr	= ADDR_UNSET,
		.no_msp34xx     = 1,
		.no_tda7432     = 1,
	},

		/* ---- card 0x58 ---------------------------------- */
	[BTTV_BOARD_OSPREY2x0_SVID] = {
		.name           = "Osprey 200/250",   /* 0x1(A|B)-00C4-C1 */
		.video_inputs   = 2,
		/* .audio_inputs= 1, */
		.svhs           = 1,
		.muxsel         = MUXSEL(0, 1),
		.pll            = PLL_28,
		.tuner_type	= TUNER_ABSENT,
		.tuner_addr	= ADDR_UNSET,
		.no_msp34xx     = 1,
		.no_tda7432     = 1,
	},
	[BTTV_BOARD_OSPREY2x0] = {
		.name           = "Osprey 210/220/230",   /* 0x1(A|B)-04C0-C1 */
		.video_inputs   = 2,
		/* .audio_inputs= 1, */
		.svhs           = 1,
		.muxsel         = MUXSEL(2, 3),
		.pll            = PLL_28,
		.tuner_type	= TUNER_ABSENT,
		.tuner_addr	= ADDR_UNSET,
		.no_msp34xx     = 1,
		.no_tda7432     = 1,
	},
	[BTTV_BOARD_OSPREY500] = {
		.name           = "Osprey 500",   /* 500 */
		.video_inputs   = 2,
		/* .audio_inputs= 1, */
		.svhs           = 1,
		.muxsel         = MUXSEL(2, 3),
		.pll            = PLL_28,
		.tuner_type     = TUNER_ABSENT,
		.tuner_addr	= ADDR_UNSET,
		.no_msp34xx     = 1,
		.no_tda7432     = 1,
	},
	[BTTV_BOARD_OSPREY540] = {
		.name           = "Osprey 540",   /* 540 */
		.video_inputs   = 4,
		/* .audio_inputs= 1, */
		.pll            = PLL_28,
		.tuner_type     = TUNER_ABSENT,
		.tuner_addr	= ADDR_UNSET,
		.no_msp34xx     = 1,
		.no_tda7432     = 1,
	},

		/* ---- card 0x5C ---------------------------------- */
	[BTTV_BOARD_OSPREY2000] = {
		.name           = "Osprey 2000",  /* 2000 */
		.video_inputs   = 2,
		/* .audio_inputs= 1, */
		.svhs           = 1,
		.muxsel         = MUXSEL(2, 3),
		.pll            = PLL_28,
		.tuner_type	= TUNER_ABSENT,
		.tuner_addr	= ADDR_UNSET,
		.no_msp34xx     = 1,
		.no_tda7432     = 1,      /* must avoid, conflicts with the bt860 */
	},
	[BTTV_BOARD_IDS_EAGLE] = {
		/* M G Berberich <berberic@forwiss.uni-passau.de> */
		.name           = "IDS Eagle",
		.video_inputs   = 4,
		/* .audio_inputs= 0, */
		.tuner_type     = TUNER_ABSENT,
		.tuner_addr	= ADDR_UNSET,
		.svhs           = NO_SVHS,
		.gpiomask       = 0,
		.muxsel         = MUXSEL(2, 2, 2, 2),
		.muxsel_hook    = eagle_muxsel,
		.no_msp34xx     = 1,
		.pll            = PLL_28,
	},
	[BTTV_BOARD_PINNACLESAT] = {
		.name           = "Pinnacle PCTV Sat",
		.video_inputs   = 2,
		/* .audio_inputs= 0, */
		.svhs           = 1,
		.tuner_type     = TUNER_ABSENT,
		.tuner_addr	= ADDR_UNSET,
		.no_msp34xx     = 1,
		.no_tda7432     = 1,
		.muxsel         = MUXSEL(3, 1),
		.pll            = PLL_28,
		.no_gpioirq     = 1,
		.has_dvb        = 1,
	},
	[BTTV_BOARD_FORMAC_PROTV] = {
		.name           = "Formac ProTV II (bt878)",
		.video_inputs   = 4,
		/* .audio_inputs= 1, */
		.svhs           = 3,
		.gpiomask       = 2,
		/* TV, Comp1, Composite over SVID con, SVID */
		.muxsel         = MUXSEL(2, 3, 1, 1),
		.gpiomux        = { 2, 2, 0, 0 },
		.pll            = PLL_28,
		.has_radio      = 1,
		.tuner_type     = TUNER_PHILIPS_PAL,
		.tuner_addr	= ADDR_UNSET,
	/* sound routing:
		GPIO=0x00,0x01,0x03: mute (?)
		0x02: both TV and radio (tuner: FM1216/I)
		The card has onboard audio connectors labeled "cdrom" and "board",
		not soldered here, though unknown wiring.
		Card lacks: external audio in, pci subsystem id.
	*/
	},

		/* ---- card 0x60 ---------------------------------- */
	[BTTV_BOARD_MACHTV] = {
		.name           = "MachTV",
		.video_inputs   = 3,
		/* .audio_inputs= 1, */
		.svhs           = NO_SVHS,
		.gpiomask       = 7,
		.muxsel         = MUXSEL(2, 3, 1, 1),
		.gpiomux        = { 0, 1, 2, 3},
		.gpiomute 	= 4,
		.tuner_type     = TUNER_PHILIPS_PAL,
		.tuner_addr	= ADDR_UNSET,
		.pll            = PLL_28,
	},
	[BTTV_BOARD_EURESYS_PICOLO] = {
		.name           = "Euresys Picolo",
		.video_inputs   = 3,
		/* .audio_inputs= 0, */
		.svhs           = 2,
		.gpiomask       = 0,
		.no_msp34xx     = 1,
		.no_tda7432     = 1,
		.muxsel         = MUXSEL(2, 0, 1),
		.pll            = PLL_28,
		.tuner_type     = TUNER_ABSENT,
		.tuner_addr	= ADDR_UNSET,
	},
	[BTTV_BOARD_PV150] = {
		/* Luc Van Hoeylandt <luc@e-magic.be> */
		.name           = "ProVideo PV150", /* 0x4f */
		.video_inputs   = 2,
		/* .audio_inputs= 0, */
		.svhs           = NO_SVHS,
		.gpiomask       = 0,
		.muxsel         = MUXSEL(2, 3),
		.gpiomux        = { 0 },
		.no_msp34xx     = 1,
		.pll            = PLL_28,
		.tuner_type     = TUNER_ABSENT,
		.tuner_addr	= ADDR_UNSET,
	},
	[BTTV_BOARD_AD_TVK503] = {
		/* Hiroshi Takekawa <sian@big.or.jp> */
		/* This card lacks subsystem ID */
		.name           = "AD-TVK503", /* 0x63 */
		.video_inputs   = 4,
		/* .audio_inputs= 1, */
		.svhs           = 2,
		.gpiomask       = 0x001e8007,
		.muxsel         = MUXSEL(2, 3, 1, 0),
		/*                  Tuner, Radio, external, internal, off,  on */
		.gpiomux        = { 0x08,  0x0f,  0x0a,     0x08 },
		.gpiomute 	= 0x0f,
		.no_msp34xx     = 1,
		.pll            = PLL_28,
		.tuner_type     = TUNER_PHILIPS_NTSC,
		.tuner_addr	= ADDR_UNSET,
		.audio_mode_gpio= adtvk503_audio,
	},

		/* ---- card 0x64 ---------------------------------- */
	[BTTV_BOARD_HERCULES_SM_TV] = {
		.name           = "Hercules Smart TV Stereo",
		.video_inputs   = 4,
		/* .audio_inputs= 1, */
		.svhs           = 2,
		.gpiomask       = 0x00,
		.muxsel         = MUXSEL(2, 3, 1, 1),
		.no_msp34xx     = 1,
		.pll            = PLL_28,
		.tuner_type     = TUNER_PHILIPS_PAL,
		.tuner_addr	= ADDR_UNSET,
		/* Notes:
		- card lacks subsystem ID
		- stereo variant w/ daughter board with tda9874a @0xb0
		- Audio Routing:
			always from tda9874 independent of GPIO (?)
			external line in: unknown
		- Other chips: em78p156elp @ 0x96 (probably IR remote control)
			hef4053 (instead 4052) for unknown function
		*/
	},
	[BTTV_BOARD_PACETV] = {
		.name           = "Pace TV & Radio Card",
		.video_inputs   = 4,
		/* .audio_inputs= 1, */
		.svhs           = 2,
		/* Tuner, CVid, SVid, CVid over SVid connector */
		.muxsel         = MUXSEL(2, 3, 1, 1),
		.gpiomask       = 0,
		.no_tda7432     = 1,
		.tuner_type     = TUNER_PHILIPS_PAL_I,
		.tuner_addr	= ADDR_UNSET,
		.has_radio      = 1,
		.pll            = PLL_28,
		/* Bt878, Bt832, FI1246 tuner; no pci subsystem id
		only internal line out: (4pin header) RGGL
		Radio must be decoded by msp3410d (not routed through)*/
		/*
		.digital_mode   = DIGITAL_MODE_CAMERA,  todo!
		*/
	},
	[BTTV_BOARD_IVC200] = {
		/* Chris Willing <chris@vislab.usyd.edu.au> */
		.name           = "IVC-200",
		.video_inputs   = 1,
		/* .audio_inputs= 0, */
		.tuner_type     = TUNER_ABSENT,
		.tuner_addr	= ADDR_UNSET,
		.svhs           = NO_SVHS,
		.gpiomask       = 0xdf,
		.muxsel         = MUXSEL(2),
		.pll            = PLL_28,
	},
	[BTTV_BOARD_IVCE8784] = {
		.name           = "IVCE-8784",
		.video_inputs   = 1,
		/* .audio_inputs= 0, */
		.tuner_type     = TUNER_ABSENT,
		.tuner_addr     = ADDR_UNSET,
		.svhs           = NO_SVHS,
		.gpiomask       = 0xdf,
		.muxsel         = MUXSEL(2),
		.pll            = PLL_28,
	},
	[BTTV_BOARD_XGUARD] = {
		.name           = "Grand X-Guard / Trust 814PCI",
		.video_inputs   = 16,
		/* .audio_inputs= 0, */
		.svhs           = NO_SVHS,
		.tuner_type     = TUNER_ABSENT,
		.tuner_addr	= ADDR_UNSET,
		.gpiomask2      = 0xff,
		.muxsel         = MUXSEL(2,2,2,2, 3,3,3,3, 1,1,1,1, 0,0,0,0),
		.muxsel_hook    = xguard_muxsel,
		.no_msp34xx     = 1,
		.no_tda7432     = 1,
		.pll            = PLL_28,
	},

		/* ---- card 0x68 ---------------------------------- */
	[BTTV_BOARD_NEBULA_DIGITV] = {
		.name           = "Nebula Electronics DigiTV",
		.video_inputs   = 1,
		.svhs           = NO_SVHS,
		.muxsel         = MUXSEL(2, 3, 1, 0),
		.no_msp34xx     = 1,
		.no_tda7432     = 1,
		.pll            = PLL_28,
		.tuner_type     = TUNER_ABSENT,
		.tuner_addr	= ADDR_UNSET,
		.has_dvb        = 1,
		.has_remote	= 1,
		.gpiomask	= 0x1b,
		.no_gpioirq     = 1,
	},
	[BTTV_BOARD_PV143] = {
		/* Jorge Boncompte - DTI2 <jorge@dti2.net> */
		.name           = "ProVideo PV143",
		.video_inputs   = 4,
		/* .audio_inputs= 0, */
		.svhs           = NO_SVHS,
		.gpiomask       = 0,
		.muxsel         = MUXSEL(2, 3, 1, 0),
		.gpiomux        = { 0 },
		.no_msp34xx     = 1,
		.pll            = PLL_28,
		.tuner_type     = TUNER_ABSENT,
		.tuner_addr	= ADDR_UNSET,
	},
	[BTTV_BOARD_VD009X1_VD011_MINIDIN] = {
		/* M.Klahr@phytec.de */
		.name           = "PHYTEC VD-009-X1 VD-011 MiniDIN (bt878)",
		.video_inputs   = 4,
		/* .audio_inputs= 0, */
		.svhs           = 3,
		.gpiomask       = 0x00,
		.muxsel         = MUXSEL(2, 3, 1, 0),
		.gpiomux        = { 0, 0, 0, 0 }, /* card has no audio */
		.pll            = PLL_28,
		.tuner_type     = TUNER_ABSENT,
		.tuner_addr	= ADDR_UNSET,
	},
	[BTTV_BOARD_VD009X1_VD011_COMBI] = {
		.name           = "PHYTEC VD-009-X1 VD-011 Combi (bt878)",
		.video_inputs   = 4,
		/* .audio_inputs= 0, */
		.svhs           = 3,
		.gpiomask       = 0x00,
		.muxsel         = MUXSEL(2, 3, 1, 1),
		.gpiomux        = { 0, 0, 0, 0 }, /* card has no audio */
		.pll            = PLL_28,
		.tuner_type     = TUNER_ABSENT,
		.tuner_addr	= ADDR_UNSET,
	},

		/* ---- card 0x6c ---------------------------------- */
	[BTTV_BOARD_VD009_MINIDIN] = {
		.name           = "PHYTEC VD-009 MiniDIN (bt878)",
		.video_inputs   = 10,
		/* .audio_inputs= 0, */
		.svhs           = 9,
		.gpiomask       = 0x00,
		.gpiomask2      = 0x03, /* used for external vodeo mux */
		.muxsel         = MUXSEL(2, 2, 2, 2, 3, 3, 3, 3, 1, 0),
		.muxsel_hook	= phytec_muxsel,
		.gpiomux        = { 0, 0, 0, 0 }, /* card has no audio */
		.pll            = PLL_28,
		.tuner_type     = TUNER_ABSENT,
		.tuner_addr	= ADDR_UNSET,
	},
	[BTTV_BOARD_VD009_COMBI] = {
		.name           = "PHYTEC VD-009 Combi (bt878)",
		.video_inputs   = 10,
		/* .audio_inputs= 0, */
		.svhs           = 9,
		.gpiomask       = 0x00,
		.gpiomask2      = 0x03, /* used for external vodeo mux */
		.muxsel         = MUXSEL(2, 2, 2, 2, 3, 3, 3, 3, 1, 1),
		.muxsel_hook	= phytec_muxsel,
		.gpiomux        = { 0, 0, 0, 0 }, /* card has no audio */
		.pll            = PLL_28,
		.tuner_type     = TUNER_ABSENT,
		.tuner_addr	= ADDR_UNSET,
	},
	[BTTV_BOARD_IVC100] = {
		.name           = "IVC-100",
		.video_inputs   = 4,
		/* .audio_inputs= 0, */
		.tuner_type     = TUNER_ABSENT,
		.tuner_addr	= ADDR_UNSET,
		.svhs           = NO_SVHS,
		.gpiomask       = 0xdf,
		.muxsel         = MUXSEL(2, 3, 1, 0),
		.pll            = PLL_28,
	},
	[BTTV_BOARD_IVC120] = {
		/* IVC-120G - Alan Garfield <alan@fromorbit.com> */
		.name           = "IVC-120G",
		.video_inputs   = 16,
		/* .audio_inputs= 0, */
		.tuner_type     = TUNER_ABSENT,
		.tuner_addr	= ADDR_UNSET,
		.svhs           = NO_SVHS,   /* card has no svhs */
		.no_msp34xx     = 1,
		.no_tda7432     = 1,
		.gpiomask       = 0x00,
		.muxsel         = MUXSEL(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
		.muxsel_hook    = ivc120_muxsel,
		.pll            = PLL_28,
	},

		/* ---- card 0x70 ---------------------------------- */
	[BTTV_BOARD_PC_HDTV] = {
		.name           = "pcHDTV HD-2000 TV",
		.video_inputs   = 4,
		/* .audio_inputs= 1, */
		.svhs           = 2,
		.muxsel         = MUXSEL(2, 3, 1, 0),
		.tuner_type     = TUNER_PHILIPS_FCV1236D,
		.tuner_addr	= ADDR_UNSET,
		.has_dvb        = 1,
	},
	[BTTV_BOARD_TWINHAN_DST] = {
		.name           = "Twinhan DST + clones",
		.no_msp34xx     = 1,
		.no_tda7432     = 1,
		.tuner_type     = TUNER_ABSENT,
		.tuner_addr	= ADDR_UNSET,
		.no_video       = 1,
		.has_dvb        = 1,
	},
	[BTTV_BOARD_WINFASTVC100] = {
		.name           = "Winfast VC100",
		.video_inputs   = 3,
		/* .audio_inputs= 0, */
		.svhs           = 1,
		/* Vid In, SVid In, Vid over SVid in connector */
		.muxsel		= MUXSEL(3, 1, 1, 3),
		.no_msp34xx     = 1,
		.no_tda7432     = 1,
		.tuner_type     = TUNER_ABSENT,
		.tuner_addr	= ADDR_UNSET,
		.pll            = PLL_28,
	},
	[BTTV_BOARD_TEV560] = {
		.name           = "Teppro TEV-560/InterVision IV-560",
		.video_inputs   = 3,
		/* .audio_inputs= 1, */
		.svhs           = 2,
		.gpiomask       = 3,
		.muxsel         = MUXSEL(2, 3, 1, 1),
		.gpiomux        = { 1, 1, 1, 1 },
		.tuner_type     = TUNER_PHILIPS_PAL,
		.tuner_addr	= ADDR_UNSET,
		.pll            = PLL_35,
	},

		/* ---- card 0x74 ---------------------------------- */
	[BTTV_BOARD_SIMUS_GVC1100] = {
		.name           = "SIMUS GVC1100",
		.video_inputs   = 4,
		/* .audio_inputs= 0, */
		.svhs           = NO_SVHS,
		.tuner_type     = TUNER_ABSENT,
		.tuner_addr	= ADDR_UNSET,
		.pll            = PLL_28,
		.muxsel         = MUXSEL(2, 2, 2, 2),
		.gpiomask       = 0x3F,
		.muxsel_hook    = gvc1100_muxsel,
	},
	[BTTV_BOARD_NGSTV_PLUS] = {
		/* Carlos Silva r3pek@r3pek.homelinux.org || card 0x75 */
		.name           = "NGS NGSTV+",
		.video_inputs   = 3,
		.svhs           = 2,
		.gpiomask       = 0x008007,
		.muxsel         = MUXSEL(2, 3, 0, 0),
		.gpiomux        = { 0, 0, 0, 0 },
		.gpiomute 	= 0x000003,
		.pll            = PLL_28,
		.tuner_type     = TUNER_PHILIPS_PAL,
		.tuner_addr	= ADDR_UNSET,
		.has_remote     = 1,
	},
	[BTTV_BOARD_LMLBT4] = {
		/* http://linuxmedialabs.com */
		.name           = "LMLBT4",
		.video_inputs   = 4, /* IN1,IN2,IN3,IN4 */
		/* .audio_inputs= 0, */
		.svhs           = NO_SVHS,
		.muxsel         = MUXSEL(2, 3, 1, 0),
		.no_msp34xx     = 1,
		.no_tda7432     = 1,
		.tuner_type     = TUNER_ABSENT,
		.tuner_addr	= ADDR_UNSET,
	},
	[BTTV_BOARD_TEKRAM_M205] = {
		/* Helmroos Harri <harri.helmroos@pp.inet.fi> */
		.name           = "Tekram M205 PRO",
		.video_inputs   = 3,
		/* .audio_inputs= 1, */
		.tuner_type     = TUNER_PHILIPS_PAL,
		.tuner_addr	= ADDR_UNSET,
		.svhs           = 2,
		.gpiomask       = 0x68,
		.muxsel         = MUXSEL(2, 3, 1),
		.gpiomux        = { 0x68, 0x68, 0x61, 0x61 },
		.pll            = PLL_28,
	},

		/* ---- card 0x78 ---------------------------------- */
	[BTTV_BOARD_CONTVFMI] = {
		/* Javier Cendan Ares <jcendan@lycos.es> */
		/* bt878 TV + FM without subsystem ID */
		.name           = "Conceptronic CONTVFMi",
		.video_inputs   = 3,
		/* .audio_inputs= 1, */
		.svhs           = 2,
		.gpiomask       = 0x008007,
		.muxsel         = MUXSEL(2, 3, 1, 1),
		.gpiomux        = { 0, 1, 2, 2 },
		.gpiomute 	= 3,
		.pll            = PLL_28,
		.tuner_type     = TUNER_PHILIPS_PAL,
		.tuner_addr	= ADDR_UNSET,
		.has_remote     = 1,
		.has_radio      = 1,
	},
	[BTTV_BOARD_PICOLO_TETRA_CHIP] = {
		/*Eric DEBIEF <debief@telemsa.com>*/
		/*EURESYS Picolo Tetra : 4 Conexant Fusion 878A, no audio, video input set with analog multiplexers GPIO controlled*/
		/* adds picolo_tetra_muxsel(), picolo_tetra_init(), the following declaration strucure, and #define BTTV_BOARD_PICOLO_TETRA_CHIP*/
		/*0x79 in bttv.h*/
		.name           = "Euresys Picolo Tetra",
		.video_inputs   = 4,
		/* .audio_inputs= 0, */
		.svhs           = NO_SVHS,
		.gpiomask       = 0,
		.gpiomask2      = 0x3C<<16,/*Set the GPIO[18]->GPIO[21] as output pin.==> drive the video inputs through analog multiplexers*/
		.no_msp34xx     = 1,
		.no_tda7432     = 1,
		/*878A input is always MUX0, see above.*/
		.muxsel         = MUXSEL(2, 2, 2, 2),
		.gpiomux        = { 0, 0, 0, 0 }, /* card has no audio */
		.pll            = PLL_28,
		.muxsel_hook    = picolo_tetra_muxsel,/*Required as it doesn't follow the classic input selection policy*/
		.tuner_type     = TUNER_ABSENT,
		.tuner_addr	= ADDR_UNSET,
	},
	[BTTV_BOARD_SPIRIT_TV] = {
		/* Spirit TV Tuner from http://spiritmodems.com.au */
		/* Stafford Goodsell <surge@goliath.homeunix.org> */
		.name           = "Spirit TV Tuner",
		.video_inputs   = 3,
		/* .audio_inputs= 1, */
		.svhs           = 2,
		.gpiomask       = 0x0000000f,
		.muxsel         = MUXSEL(2, 1, 1),
		.gpiomux        = { 0x02, 0x00, 0x00, 0x00 },
		.tuner_type     = TUNER_TEMIC_PAL,
		.tuner_addr	= ADDR_UNSET,
		.no_msp34xx     = 1,
	},
	[BTTV_BOARD_AVDVBT_771] = {
		/* Wolfram Joost <wojo@frokaschwei.de> */
		.name           = "AVerMedia AVerTV DVB-T 771",
		.video_inputs   = 2,
		.svhs           = 1,
		.tuner_type     = TUNER_ABSENT,
		.tuner_addr	= ADDR_UNSET,
		.muxsel         = MUXSEL(3, 3),
		.no_msp34xx     = 1,
		.no_tda7432     = 1,
		.pll            = PLL_28,
		.has_dvb        = 1,
		.no_gpioirq     = 1,
		.has_remote     = 1,
	},
		/* ---- card 0x7c ---------------------------------- */
	[BTTV_BOARD_AVDVBT_761] = {
		/* Matt Jesson <dvb@jesson.eclipse.co.uk> */
		/* Based on the Nebula card data - added remote and new card number - BTTV_BOARD_AVDVBT_761, see also ir-kbd-gpio.c */
		.name           = "AverMedia AverTV DVB-T 761",
		.video_inputs   = 2,
		.svhs           = 1,
		.muxsel         = MUXSEL(3, 1, 2, 0), /* Comp0, S-Video, ?, ? */
		.no_msp34xx     = 1,
		.no_tda7432     = 1,
		.pll            = PLL_28,
		.tuner_type     = TUNER_ABSENT,
		.tuner_addr	= ADDR_UNSET,
		.has_dvb        = 1,
		.no_gpioirq     = 1,
		.has_remote     = 1,
	},
	[BTTV_BOARD_MATRIX_VISIONSQ] = {
		/* andre.schwarz@matrix-vision.de */
		.name		= "MATRIX Vision Sigma-SQ",
		.video_inputs	= 16,
		/* .audio_inputs= 0, */
		.svhs		= NO_SVHS,
		.gpiomask	= 0x0,
		.muxsel		= MUXSEL(2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3),
		.muxsel_hook	= sigmaSQ_muxsel,
		.gpiomux	= { 0 },
		.no_msp34xx	= 1,
		.pll		= PLL_28,
		.tuner_type	= TUNER_ABSENT,
		.tuner_addr	= ADDR_UNSET,
	},
	[BTTV_BOARD_MATRIX_VISIONSLC] = {
		/* andre.schwarz@matrix-vision.de */
		.name		= "MATRIX Vision Sigma-SLC",
		.video_inputs	= 4,
		/* .audio_inputs= 0, */
		.svhs		= NO_SVHS,
		.gpiomask	= 0x0,
		.muxsel		= MUXSEL(2, 2, 2, 2),
		.muxsel_hook	= sigmaSLC_muxsel,
		.gpiomux	= { 0 },
		.no_msp34xx	= 1,
		.pll		= PLL_28,
		.tuner_type	= TUNER_ABSENT,
		.tuner_addr	= ADDR_UNSET,
	},
		/* BTTV_BOARD_APAC_VIEWCOMP */
	[BTTV_BOARD_APAC_VIEWCOMP] = {
		/* Attila Kondoros <attila.kondoros@chello.hu> */
		/* bt878 TV + FM 0x00000000 subsystem ID */
		.name           = "APAC Viewcomp 878(AMAX)",
		.video_inputs   = 2,
		/* .audio_inputs= 1, */
		.svhs           = NO_SVHS,
		.gpiomask       = 0xFF,
		.muxsel         = MUXSEL(2, 3, 1, 1),
		.gpiomux        = { 2, 0, 0, 0 },
		.gpiomute 	= 10,
		.pll            = PLL_28,
		.tuner_type     = TUNER_PHILIPS_PAL,
		.tuner_addr	= ADDR_UNSET,
		.has_remote     = 1,   /* miniremote works, see ir-kbd-gpio.c */
		.has_radio      = 1,   /* not every card has radio */
	},

		/* ---- card 0x80 ---------------------------------- */
	[BTTV_BOARD_DVICO_DVBT_LITE] = {
		/* Chris Pascoe <c.pascoe@itee.uq.edu.au> */
		.name           = "DViCO FusionHDTV DVB-T Lite",
		.no_msp34xx     = 1,
		.no_tda7432     = 1,
		.pll            = PLL_28,
		.no_video       = 1,
		.has_dvb        = 1,
		.tuner_type     = TUNER_ABSENT,
		.tuner_addr	= ADDR_UNSET,
	},
	[BTTV_BOARD_VGEAR_MYVCD] = {
		/* Steven <photon38@pchome.com.tw> */
		.name           = "V-Gear MyVCD",
		.video_inputs   = 3,
		/* .audio_inputs= 1, */
		.svhs           = 2,
		.gpiomask       = 0x3f,
		.muxsel         = MUXSEL(2, 3, 1, 0),
		.gpiomux        = {0x31, 0x31, 0x31, 0x31 },
		.gpiomute 	= 0x31,
		.no_msp34xx     = 1,
		.pll            = PLL_28,
		.tuner_type     = TUNER_PHILIPS_NTSC_M,
		.tuner_addr	= ADDR_UNSET,
		.has_radio      = 0,
	},
	[BTTV_BOARD_SUPER_TV] = {
		/* Rick C <cryptdragoon@gmail.com> */
		.name           = "Super TV Tuner",
		.video_inputs   = 4,
		/* .audio_inputs= 1, */
		.svhs           = 2,
		.muxsel         = MUXSEL(2, 3, 1, 0),
		.tuner_type     = TUNER_PHILIPS_NTSC,
		.tuner_addr	= ADDR_UNSET,
		.gpiomask       = 0x008007,
		.gpiomux        = { 0, 0x000001,0,0 },
		.has_radio      = 1,
	},
	[BTTV_BOARD_TIBET_CS16] = {
		/* Chris Fanning <video4linux@haydon.net> */
		.name           = "Tibet Systems 'Progress DVR' CS16",
		.video_inputs   = 16,
		/* .audio_inputs= 0, */
		.svhs           = NO_SVHS,
		.muxsel         = MUXSEL(2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2),
		.pll		= PLL_28,
		.no_msp34xx     = 1,
		.no_tda7432	= 1,
		.tuner_type     = TUNER_ABSENT,
		.tuner_addr	= ADDR_UNSET,
		.muxsel_hook    = tibetCS16_muxsel,
	},
	[BTTV_BOARD_KODICOM_4400R] = {
		/* Bill Brack <wbrack@mmm.com.hk> */
		/*
		* Note that, because of the card's wiring, the "master"
		* BT878A chip (i.e. the one which controls the analog switch
		* and must use this card type) is the 2nd one detected.  The
		* other 3 chips should use card type 0x85, whose description
		* follows this one.  There is a EEPROM on the card (which is
		* connected to the I2C of one of those other chips), but is
		* not currently handled.  There is also a facility for a
		* "monitor", which is also not currently implemented.
		*/
		.name           = "Kodicom 4400R (master)",
		.video_inputs	= 16,
		/* .audio_inputs= 0, */
		.tuner_type	= TUNER_ABSENT,
		.tuner_addr	= ADDR_UNSET,
		.svhs		= NO_SVHS,
		/* GPIO bits 0-9 used for analog switch:
		*   00 - 03:	camera selector
		*   04 - 06:	channel (controller) selector
		*   07:	data (1->on, 0->off)
		*   08:	strobe
		*   09:	reset
		* bit 16 is input from sync separator for the channel
		*/
		.gpiomask	= 0x0003ff,
		.no_gpioirq     = 1,
		.muxsel		= MUXSEL(3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3),
		.pll		= PLL_28,
		.no_msp34xx	= 1,
		.no_tda7432	= 1,
		.muxsel_hook	= kodicom4400r_muxsel,
	},
	[BTTV_BOARD_KODICOM_4400R_SL] = {
		/* Bill Brack <wbrack@mmm.com.hk> */
		/* Note that, for reasons unknown, the "master" BT878A chip (i.e. the
		* one which controls the analog switch, and must use the card type)
		* is the 2nd one detected.  The other 3 chips should use this card
		* type
		*/
		.name		= "Kodicom 4400R (slave)",
		.video_inputs	= 16,
		/* .audio_inputs= 0, */
		.tuner_type	= TUNER_ABSENT,
		.tuner_addr	= ADDR_UNSET,
		.svhs		= NO_SVHS,
		.gpiomask	= 0x010000,
		.no_gpioirq     = 1,
		.muxsel		= MUXSEL(3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3),
		.pll		= PLL_28,
		.no_msp34xx	= 1,
		.no_tda7432	= 1,
		.muxsel_hook	= kodicom4400r_muxsel,
	},
		/* ---- card 0x86---------------------------------- */
	[BTTV_BOARD_ADLINK_RTV24] = {
		/* Michael Henson <mhenson@clarityvi.com> */
		/* Adlink RTV24 with special unlock codes */
		.name           = "Adlink RTV24",
		.video_inputs   = 4,
		/* .audio_inputs= 1, */
		.svhs           = 2,
		.muxsel         = MUXSEL(2, 3, 1, 0),
		.tuner_type     = UNSET,
		.tuner_addr	= ADDR_UNSET,
		.pll            = PLL_28,
	},
		/* ---- card 0x87---------------------------------- */
	[BTTV_BOARD_DVICO_FUSIONHDTV_5_LITE] = {
		/* Michael Krufky <mkrufky@linuxtv.org> */
		.name           = "DViCO FusionHDTV 5 Lite",
		.tuner_type     = TUNER_LG_TDVS_H06XF, /* TDVS-H064F */
		.tuner_addr	= ADDR_UNSET,
		.video_inputs   = 3,
		/* .audio_inputs= 1, */
		.svhs           = 2,
		.muxsel		= MUXSEL(2, 3, 1),
		.gpiomask       = 0x00e00007,
		.gpiomux        = { 0x00400005, 0, 0x00000001, 0 },
		.gpiomute 	= 0x00c00007,
		.no_msp34xx     = 1,
		.no_tda7432     = 1,
		.has_dvb        = 1,
	},
		/* ---- card 0x88---------------------------------- */
	[BTTV_BOARD_ACORP_Y878F] = {
		/* Mauro Carvalho Chehab <mchehab@infradead.org> */
		.name		= "Acorp Y878F",
		.video_inputs	= 3,
		/* .audio_inputs= 1, */
		.svhs		= 2,
		.gpiomask	= 0x01fe00,
		.muxsel		= MUXSEL(2, 3, 1, 1),
		.gpiomux        = { 0x001e00, 0, 0x018000, 0x014000 },
		.gpiomute 	= 0x002000,
		.pll		= PLL_28,
		.tuner_type	= TUNER_YMEC_TVF66T5_B_DFF,
		.tuner_addr	= 0xc1 >>1,
		.has_radio	= 1,
	},
		/* ---- card 0x89 ---------------------------------- */
	[BTTV_BOARD_CONCEPTRONIC_CTVFMI2] = {
		.name           = "Conceptronic CTVFMi v2",
		.video_inputs   = 3,
		/* .audio_inputs= 1, */
		.svhs           = 2,
		.gpiomask       = 0x001c0007,
		.muxsel         = MUXSEL(2, 3, 1, 1),
		.gpiomux        = { 0, 1, 2, 2 },
		.gpiomute 	= 3,
		.pll            = PLL_28,
		.tuner_type     = TUNER_TENA_9533_DI,
		.tuner_addr	= ADDR_UNSET,
		.has_remote     = 1,
		.has_radio      = 1,
	},
		/* ---- card 0x8a ---------------------------------- */
	[BTTV_BOARD_PV_BT878P_2E] = {
		.name		= "Prolink Pixelview PV-BT878P+ (Rev.2E)",
		.video_inputs	= 5,
		/* .audio_inputs= 1, */
		.svhs		= 3,
		.has_dig_in	= 1,
		.gpiomask	= 0x01fe00,
		.muxsel		= MUXSEL(2, 3, 1, 1, 0), /* in 4 is digital */
		/* .digital_mode= DIGITAL_MODE_CAMERA, */
		.gpiomux	= { 0x00400, 0x10400, 0x04400, 0x80000 },
		.gpiomute	= 0x12400,
		.no_msp34xx	= 1,
		.pll		= PLL_28,
		.tuner_type	= TUNER_LG_PAL_FM,
		.tuner_addr	= ADDR_UNSET,
		.has_remote	= 1,
	},
		/* ---- card 0x8b ---------------------------------- */
	[BTTV_BOARD_PV_M4900] = {
		/* Sérgio Fortier <sergiofortier@yahoo.com.br> */
		.name           = "Prolink PixelView PlayTV MPEG2 PV-M4900",
		.video_inputs   = 3,
		/* .audio_inputs= 1, */
		.svhs           = 2,
		.gpiomask       = 0x3f,
		.muxsel         = MUXSEL(2, 3, 1, 1),
		.gpiomux        = { 0x21, 0x20, 0x24, 0x2c },
		.gpiomute 	= 0x29,
		.no_msp34xx     = 1,
		.pll            = PLL_28,
		.tuner_type     = TUNER_YMEC_TVF_5533MF,
		.tuner_addr     = ADDR_UNSET,
		.has_radio      = 1,
		.has_remote     = 1,
	},
		/* ---- card 0x8c ---------------------------------- */
	/* Has four Bt878 chips behind a PCI bridge, each chip has:
	     one external BNC composite input (mux 2)
	     three internal composite inputs (unknown muxes)
	     an 18-bit stereo A/D (CS5331A), which has:
	       one external stereo unblanced (RCA) audio connection
	       one (or 3?) internal stereo balanced (XLR) audio connection
	       input is selected via gpio to a 14052B mux
		 (mask=0x300, unbal=0x000, bal=0x100, ??=0x200,0x300)
	       gain is controlled via an X9221A chip on the I2C bus @0x28
	       sample rate is controlled via gpio to an MK1413S
		 (mask=0x3, 32kHz=0x0, 44.1kHz=0x1, 48kHz=0x2, ??=0x3)
	     There is neither a tuner nor an svideo input. */
	[BTTV_BOARD_OSPREY440]  = {
		.name           = "Osprey 440",
		.video_inputs   = 4,
		/* .audio_inputs= 2, */
		.svhs           = NO_SVHS,
		.muxsel         = MUXSEL(2, 3, 0, 1), /* 3,0,1 are guesses */
		.gpiomask	= 0x303,
		.gpiomute	= 0x000, /* int + 32kHz */
		.gpiomux	= { 0, 0, 0x000, 0x100},
		.pll            = PLL_28,
		.tuner_type     = TUNER_ABSENT,
		.tuner_addr     = ADDR_UNSET,
		.no_msp34xx     = 1,
		.no_tda7432     = 1,
	},
		/* ---- card 0x8d ---------------------------------- */
	[BTTV_BOARD_ASOUND_SKYEYE] = {
		.name		= "Asound Skyeye PCTV",
		.video_inputs	= 3,
		/* .audio_inputs= 1, */
		.svhs		= 2,
		.gpiomask	= 15,
		.muxsel		= MUXSEL(2, 3, 1, 1),
		.gpiomux 	= { 2, 0, 0, 0 },
		.gpiomute 	= 1,
		.pll		= PLL_28,
		.tuner_type	= TUNER_PHILIPS_NTSC,
		.tuner_addr	= ADDR_UNSET,
	},
		/* ---- card 0x8e ---------------------------------- */
	[BTTV_BOARD_SABRENT_TVFM] = {
		.name		= "Sabrent TV-FM (bttv version)",
		.video_inputs	= 3,
		/* .audio_inputs= 1, */
		.svhs		= 2,
		.gpiomask	= 0x108007,
		.muxsel		= MUXSEL(2, 3, 1, 1),
		.gpiomux 	= { 100000, 100002, 100002, 100000 },
		.no_msp34xx	= 1,
		.no_tda7432     = 1,
		.pll		= PLL_28,
		.tuner_type	= TUNER_TNF_5335MF,
		.tuner_addr	= ADDR_UNSET,
		.has_radio      = 1,
	},
	/* ---- card 0x8f ---------------------------------- */
	[BTTV_BOARD_HAUPPAUGE_IMPACTVCB] = {
		.name		= "Hauppauge ImpactVCB (bt878)",
		.video_inputs	= 4,
		/* .audio_inputs= 0, */
		.svhs		= NO_SVHS,
		.gpiomask	= 0x0f, /* old: 7 */
		.muxsel		= MUXSEL(0, 1, 3, 2), /* Composite 0-3 */
		.no_msp34xx	= 1,
		.no_tda7432     = 1,
		.tuner_type	= TUNER_ABSENT,
		.tuner_addr	= ADDR_UNSET,
	},
	[BTTV_BOARD_MACHTV_MAGICTV] = {
		/* Julian Calaby <julian.calaby@gmail.com>
		 * Slightly different from original MachTV definition (0x60)

		 * FIXME: RegSpy says gpiomask should be "0x001c800f", but it
		 * stuffs up remote chip. Bug is a pin on the jaecs is not set
		 * properly (methinks) causing no keyup bits being set */

		.name           = "MagicTV", /* rebranded MachTV */
		.video_inputs   = 3,
		/* .audio_inputs= 1, */
		.svhs           = 2,
		.gpiomask       = 7,
		.muxsel         = MUXSEL(2, 3, 1, 1),
		.gpiomux        = { 0, 1, 2, 3 },
		.gpiomute 	= 4,
		.tuner_type     = TUNER_TEMIC_4009FR5_PAL,
		.tuner_addr     = ADDR_UNSET,
		.pll            = PLL_28,
		.has_radio      = 1,
		.has_remote     = 1,
	},
	[BTTV_BOARD_SSAI_SECURITY] = {
		.name		= "SSAI Security Video Interface",
		.video_inputs	= 4,
		/* .audio_inputs= 0, */
		.svhs		= NO_SVHS,
		.muxsel		= MUXSEL(0, 1, 2, 3),
		.tuner_type	= TUNER_ABSENT,
		.tuner_addr	= ADDR_UNSET,
	},
	[BTTV_BOARD_SSAI_ULTRASOUND] = {
		.name		= "SSAI Ultrasound Video Interface",
		.video_inputs	= 2,
		/* .audio_inputs= 0, */
		.svhs		= 1,
		.muxsel		= MUXSEL(2, 0, 1, 3),
		.tuner_type	= TUNER_ABSENT,
		.tuner_addr	= ADDR_UNSET,
	},
	/* ---- card 0x94---------------------------------- */
	[BTTV_BOARD_DVICO_FUSIONHDTV_2] = {
		.name           = "DViCO FusionHDTV 2",
		.tuner_type     = TUNER_PHILIPS_FCV1236D,
		.tuner_addr	= ADDR_UNSET,
		.video_inputs   = 3,
		/* .audio_inputs= 1, */
		.svhs           = 2,
		.muxsel		= MUXSEL(2, 3, 1),
		.gpiomask       = 0x00e00007,
		.gpiomux        = { 0x00400005, 0, 0x00000001, 0 },
		.gpiomute 	= 0x00c00007,
		.no_msp34xx     = 1,
		.no_tda7432     = 1,
	},
	/* ---- card 0x95---------------------------------- */
	[BTTV_BOARD_TYPHOON_TVTUNERPCI] = {
		.name           = "Typhoon TV-Tuner PCI (50684)",
		.video_inputs   = 3,
		/* .audio_inputs= 1, */
		.svhs           = 2,
		.gpiomask       = 0x3014f,
		.muxsel         = MUXSEL(2, 3, 1, 1),
		.gpiomux        = { 0x20001,0x10001, 0, 0 },
		.gpiomute       = 10,
		.pll            = PLL_28,
		.tuner_type     = TUNER_PHILIPS_PAL_I,
		.tuner_addr     = ADDR_UNSET,
	},
	[BTTV_BOARD_GEOVISION_GV600] = {
		/* emhn@usb.ve */
		.name		= "Geovision GV-600",
		.video_inputs	= 16,
		/* .audio_inputs= 0, */
		.svhs		= NO_SVHS,
		.gpiomask	= 0x0,
		.muxsel		= MUXSEL(2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2),
		.muxsel_hook	= geovision_muxsel,
		.gpiomux	= { 0 },
		.no_msp34xx	= 1,
		.pll		= PLL_28,
		.tuner_type	= TUNER_ABSENT,
		.tuner_addr	= ADDR_UNSET,
	},
	[BTTV_BOARD_KOZUMI_KTV_01C] = {
		/* Mauro Lacy <mauro@lacy.com.ar>
		 * Based on MagicTV and Conceptronic CONTVFMi */

		.name           = "Kozumi KTV-01C",
		.video_inputs   = 3,
		/* .audio_inputs= 1, */
		.svhs           = 2,
		.gpiomask       = 0x008007,
		.muxsel         = MUXSEL(2, 3, 1, 1),
		.gpiomux        = { 0, 1, 2, 2 }, /* CONTVFMi */
		.gpiomute 	= 3, /* CONTVFMi */
		.tuner_type     = TUNER_PHILIPS_FM1216ME_MK3, /* TCL MK3 */
		.tuner_addr     = ADDR_UNSET,
		.pll            = PLL_28,
		.has_radio      = 1,
		.has_remote     = 1,
	},
	[BTTV_BOARD_ENLTV_FM_2] = {
		/* Encore TV Tuner Pro ENL TV-FM-2
		   Mauro Carvalho Chehab <mchehab@infradead.org */
		.name           = "Encore ENL TV-FM-2",
		.video_inputs   = 3,
		/* .audio_inputs= 1, */
		.svhs           = 2,
		/* bit 6          -> IR disabled
		   bit 18/17 = 00 -> mute
			       01 -> enable external audio input
			       10 -> internal audio input (mono?)
			       11 -> internal audio input
		 */
		.gpiomask       = 0x060040,
		.muxsel         = MUXSEL(2, 3, 3),
		.gpiomux        = { 0x60000, 0x60000, 0x20000, 0x20000 },
		.gpiomute 	= 0,
		.tuner_type	= TUNER_TCL_MF02GIP_5N,
		.tuner_addr     = ADDR_UNSET,
		.pll            = PLL_28,
		.has_radio      = 1,
		.has_remote     = 1,
	},
	[BTTV_BOARD_VD012] = {
		/* D.Heer@Phytec.de */
		.name           = "PHYTEC VD-012 (bt878)",
		.video_inputs   = 4,
		/* .audio_inputs= 0, */
		.svhs           = NO_SVHS,
		.gpiomask       = 0x00,
		.muxsel         = MUXSEL(0, 2, 3, 1),
		.gpiomux        = { 0, 0, 0, 0 }, /* card has no audio */
		.pll            = PLL_28,
		.tuner_type     = TUNER_ABSENT,
		.tuner_addr	= ADDR_UNSET,
	},
	[BTTV_BOARD_VD012_X1] = {
		/* D.Heer@Phytec.de */
		.name           = "PHYTEC VD-012-X1 (bt878)",
		.video_inputs   = 4,
		/* .audio_inputs= 0, */
		.svhs           = 3,
		.gpiomask       = 0x00,
		.muxsel         = MUXSEL(2, 3, 1),
		.gpiomux        = { 0, 0, 0, 0 }, /* card has no audio */
		.pll            = PLL_28,
		.tuner_type     = TUNER_ABSENT,
		.tuner_addr	= ADDR_UNSET,
	},
	[BTTV_BOARD_VD012_X2] = {
		/* D.Heer@Phytec.de */
		.name           = "PHYTEC VD-012-X2 (bt878)",
		.video_inputs   = 4,
		/* .audio_inputs= 0, */
		.svhs           = 3,
		.gpiomask       = 0x00,
		.muxsel         = MUXSEL(3, 2, 1),
		.gpiomux        = { 0, 0, 0, 0 }, /* card has no audio */
		.pll            = PLL_28,
		.tuner_type     = TUNER_ABSENT,
		.tuner_addr	= ADDR_UNSET,
	},
	[BTTV_BOARD_GEOVISION_GV800S] = {
		/* Bruno Christo <bchristo@inf.ufsm.br>
		 *
		 * GeoVision GV-800(S) has 4 Conexant Fusion 878A:
		 * 	1 audio input  per BT878A = 4 audio inputs
		 * 	4 video inputs per BT878A = 16 video inputs
		 * This is the first BT878A chip of the GV-800(S). It's the
		 * "master" chip and it controls the video inputs through an
		 * analog multiplexer (a CD22M3494) via some GPIO pins. The
		 * slaves should use card type 0x9e (following this one).
		 * There is a EEPROM on the card which is currently not handled.
		 * The audio input is not working yet.
		 */
		.name           = "Geovision GV-800(S) (master)",
		.video_inputs   = 4,
		/* .audio_inputs= 1, */
		.tuner_type	= TUNER_ABSENT,
		.tuner_addr	= ADDR_UNSET,
		.svhs           = NO_SVHS,
		.gpiomask	= 0xf107f,
		.no_gpioirq     = 1,
		.muxsel		= MUXSEL(2, 2, 2, 2),
		.pll		= PLL_28,
		.no_msp34xx	= 1,
		.no_tda7432	= 1,
		.muxsel_hook    = gv800s_muxsel,
	},
	[BTTV_BOARD_GEOVISION_GV800S_SL] = {
		/* Bruno Christo <bchristo@inf.ufsm.br>
		 *
		 * GeoVision GV-800(S) has 4 Conexant Fusion 878A:
		 * 	1 audio input  per BT878A = 4 audio inputs
		 * 	4 video inputs per BT878A = 16 video inputs
		 * The 3 other BT878A chips are "slave" chips of the GV-800(S)
		 * and should use this card type.
		 * The audio input is not working yet.
		 */
		.name           = "Geovision GV-800(S) (slave)",
		.video_inputs   = 4,
		/* .audio_inputs= 1, */
		.tuner_type	= TUNER_ABSENT,
		.tuner_addr	= ADDR_UNSET,
		.svhs           = NO_SVHS,
		.gpiomask	= 0x00,
		.no_gpioirq     = 1,
		.muxsel		= MUXSEL(2, 2, 2, 2),
		.pll		= PLL_28,
		.no_msp34xx	= 1,
		.no_tda7432	= 1,
		.muxsel_hook    = gv800s_muxsel,
	},
	[BTTV_BOARD_PV183] = {
		.name           = "ProVideo PV183", /* 0x9f */
		.video_inputs   = 2,
		/* .audio_inputs= 0, */
		.svhs           = NO_SVHS,
		.gpiomask       = 0,
		.muxsel         = MUXSEL(2, 3),
		.gpiomux        = { 0 },
		.no_msp34xx     = 1,
		.pll            = PLL_28,
		.tuner_type     = TUNER_ABSENT,
		.tuner_addr	= ADDR_UNSET,
	},
	/* ---- card 0xa0---------------------------------- */
	[BTTV_BOARD_TVT_TD3116] = {
		.name           = "Tongwei Video Technology TD-3116",
		.video_inputs   = 16,
		.gpiomask       = 0xc00ff,
		.muxsel         = MUXSEL(2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2),
		.muxsel_hook    = td3116_muxsel,
		.svhs           = NO_SVHS,
		.pll		= PLL_28,
		.tuner_type     = TUNER_ABSENT,
	},
	[BTTV_BOARD_APOSONIC_WDVR] = {
		.name           = "Aposonic W-DVR",
		.video_inputs   = 4,
		.svhs           = NO_SVHS,
		.muxsel         = MUXSEL(2, 3, 1, 0),
		.tuner_type     = TUNER_ABSENT,
	},
	[BTTV_BOARD_ADLINK_MPG24] = {
		/* Adlink MPG24 */
		.name           = "Adlink MPG24",
		.video_inputs   = 1,
		/* .audio_inputs= 1, */
		.svhs           = NO_SVHS,
		.muxsel         = MUXSEL(2, 2, 2, 2),
		.tuner_type     = UNSET,
		.tuner_addr	= ADDR_UNSET,
		.pll            = PLL_28,
	},
	[BTTV_BOARD_BT848_CAP_14] = {
		.name		= "Bt848 Capture 14MHz",
		.video_inputs	= 4,
		.svhs		= 2,
		.muxsel		= MUXSEL(2, 3, 1, 0),
		.pll		= PLL_14,
		.tuner_type	= TUNER_ABSENT,
	},
	[BTTV_BOARD_CYBERVISION_CV06] = {
		.name		= "CyberVision CV06 (SV)",
		.video_inputs	= 4,
		/* .audio_inputs= 0, */
		.svhs		= NO_SVHS,
		.muxsel		= MUXSEL(2, 3, 1, 0),
		.pll		= PLL_28,
		.tuner_type	= TUNER_ABSENT,
		.tuner_addr	= ADDR_UNSET,
	},
	[BTTV_BOARD_KWORLD_VSTREAM_XPERT] = {
		/* Pojar George <geoubuntu@gmail.com> */
		.name           = "Kworld V-Stream Xpert TV PVR878",
		.video_inputs   = 3,
		/* .audio_inputs= 1, */
		.svhs           = 2,
		.gpiomask       = 0x001c0007,
		.muxsel         = MUXSEL(2, 3, 1, 1),
		.gpiomux        = { 0, 1, 2, 2 },
		.gpiomute       = 3,
		.pll            = PLL_28,
		.tuner_type     = TUNER_TENA_9533_DI,
		.tuner_addr    = ADDR_UNSET,
		.has_remote     = 1,
		.has_radio      = 1,
	},
	/* ---- card 0xa6---------------------------------- */
	[BTTV_BOARD_PCI_8604PW] = {
		/* PCI-8604PW with special unlock sequence */
		.name           = "PCI-8604PW",
		.video_inputs   = 2,
		/* .audio_inputs= 0, */
		.svhs           = NO_SVHS,
		/* The second input is available on CN4, if populated.
		 * The other 5x2 header (CN2?) connects to the same inputs
		 * as the on-board BNCs */
		.muxsel         = MUXSEL(2, 3),
		.tuner_type     = TUNER_ABSENT,
		.no_msp34xx	= 1,
		.no_tda7432	= 1,
		.pll            = PLL_35,
	},
};

static const unsigned int bttv_num_tvcards = ARRAY_SIZE(bttv_tvcards);

/* ----------------------------------------------------------------------- */

static unsigned char eeprom_data[256];

/*
 * identify card
 */
void bttv_idcard(struct bttv *btv)
{
	unsigned int gpiobits;
	int i,type;

	/* read PCI subsystem ID */
	btv->cardid  = btv->c.pci->subsystem_device << 16;
	btv->cardid |= btv->c.pci->subsystem_vendor;

	if (0 != btv->cardid && 0xffffffff != btv->cardid) {
		/* look for the card */
		for (type = -1, i = 0; cards[i].id != 0; i++)
			if (cards[i].id  == btv->cardid)
				type = i;

		if (type != -1) {
			/* found it */
			pr_info("%d: detected: %s [card=%d], PCI subsystem ID is %04x:%04x\n",
				btv->c.nr, cards[type].name, cards[type].cardnr,
				btv->cardid & 0xffff,
				(btv->cardid >> 16) & 0xffff);
			btv->c.type = cards[type].cardnr;
		} else {
			/* 404 */
			pr_info("%d: subsystem: %04x:%04x (UNKNOWN)\n",
				btv->c.nr, btv->cardid & 0xffff,
				(btv->cardid >> 16) & 0xffff);
			pr_debug("please mail id, board name and the correct card= insmod option to linux-media@vger.kernel.org\n");
		}
	}

	/* let the user override the autodetected type */
	if (card[btv->c.nr] < bttv_num_tvcards)
		btv->c.type=card[btv->c.nr];

	/* print which card config we are using */
	pr_info("%d: using: %s [card=%d,%s]\n",
		btv->c.nr, bttv_tvcards[btv->c.type].name, btv->c.type,
		card[btv->c.nr] < bttv_num_tvcards
		? "insmod option" : "autodetected");

	/* overwrite gpio stuff ?? */
	if (UNSET == audioall && UNSET == audiomux[0])
		return;

	if (UNSET != audiomux[0]) {
		gpiobits = 0;
		for (i = 0; i < ARRAY_SIZE(bttv_tvcards->gpiomux); i++) {
			bttv_tvcards[btv->c.type].gpiomux[i] = audiomux[i];
			gpiobits |= audiomux[i];
		}
	} else {
		gpiobits = audioall;
		for (i = 0; i < ARRAY_SIZE(bttv_tvcards->gpiomux); i++) {
			bttv_tvcards[btv->c.type].gpiomux[i] = audioall;
		}
	}
	bttv_tvcards[btv->c.type].gpiomask = (UNSET != gpiomask) ? gpiomask : gpiobits;
	pr_info("%d: gpio config override: mask=0x%x, mux=",
		btv->c.nr, bttv_tvcards[btv->c.type].gpiomask);
	for (i = 0; i < ARRAY_SIZE(bttv_tvcards->gpiomux); i++) {
		pr_cont("%s0x%x",
			i ? "," : "", bttv_tvcards[btv->c.type].gpiomux[i]);
	}
	pr_cont("\n");
}

/*
 * (most) board specific initialisations goes here
 */

/* Some Modular Technology cards have an eeprom, but no subsystem ID */
static void identify_by_eeprom(struct bttv *btv, unsigned char eeprom_data[256])
{
	int type = -1;

	if (0 == strncmp(eeprom_data,"GET MM20xPCTV",13))
		type = BTTV_BOARD_MODTEC_205;
	else if (0 == strncmp(eeprom_data+20,"Picolo",7))
		type = BTTV_BOARD_EURESYS_PICOLO;
	else if (eeprom_data[0] == 0x84 && eeprom_data[2]== 0)
		type = BTTV_BOARD_HAUPPAUGE; /* old bt848 */

	if (-1 != type) {
		btv->c.type = type;
		pr_info("%d: detected by eeprom: %s [card=%d]\n",
			btv->c.nr, bttv_tvcards[btv->c.type].name, btv->c.type);
	}
}

static void flyvideo_gpio(struct bttv *btv)
{
	int gpio, has_remote, has_radio, is_capture_only;
	int is_lr90, has_tda9820_tda9821;
	int tuner_type = UNSET, ttype;

	gpio_inout(0xffffff, 0);
	udelay(8);  /* without this we would see the 0x1800 mask */
	gpio = gpio_read();
	/* FIXME: must restore OUR_EN ??? */

	/* all cards provide GPIO info, some have an additional eeprom
	 * LR50: GPIO coding can be found lower right CP1 .. CP9
	 *       CP9=GPIO23 .. CP1=GPIO15; when OPEN, the corresponding GPIO reads 1.
	 *       GPIO14-12: n.c.
	 * LR90: GP9=GPIO23 .. GP1=GPIO15 (right above the bt878)

	 * lowest 3 bytes are remote control codes (no handshake needed)
	 * xxxFFF: No remote control chip soldered
	 * xxxF00(LR26/LR50), xxxFE0(LR90): Remote control chip (LVA001 or CF45) soldered
	 * Note: Some bits are Audio_Mask !
	 */
	ttype = (gpio & 0x0f0000) >> 16;
	switch (ttype) {
	case 0x0:
		tuner_type = 2;  /* NTSC, e.g. TPI8NSR11P */
		break;
	case 0x2:
		tuner_type = 39; /* LG NTSC (newer TAPC series) TAPC-H701P */
		break;
	case 0x4:
		tuner_type = 5;  /* Philips PAL TPI8PSB02P, TPI8PSB12P, TPI8PSB12D or FI1216, FM1216 */
		break;
	case 0x6:
		tuner_type = 37; /* LG PAL (newer TAPC series) TAPC-G702P */
		break;
	case 0xC:
		tuner_type = 3;  /* Philips SECAM(+PAL) FQ1216ME or FI1216MF */
		break;
	default:
		pr_info("%d: FlyVideo_gpio: unknown tuner type\n", btv->c.nr);
		break;
	}

	has_remote          =   gpio & 0x800000;
	has_radio	    =   gpio & 0x400000;
	/*   unknown                   0x200000;
	 *   unknown2                  0x100000; */
	is_capture_only     = !(gpio & 0x008000); /* GPIO15 */
	has_tda9820_tda9821 = !(gpio & 0x004000);
	is_lr90             = !(gpio & 0x002000); /* else LR26/LR50 (LR38/LR51 f. capture only) */
	/*
	 * gpio & 0x001000    output bit for audio routing */

	if (is_capture_only)
		tuner_type = TUNER_ABSENT; /* No tuner present */

	pr_info("%d: FlyVideo Radio=%s RemoteControl=%s Tuner=%d gpio=0x%06x\n",
		btv->c.nr, has_radio ? "yes" : "no",
		has_remote ? "yes" : "no", tuner_type, gpio);
	pr_info("%d: FlyVideo  LR90=%s tda9821/tda9820=%s capture_only=%s\n",
		btv->c.nr, is_lr90 ? "yes" : "no",
		has_tda9820_tda9821 ? "yes" : "no",
		is_capture_only ? "yes" : "no");

	if (tuner_type != UNSET) /* only set if known tuner autodetected, else let insmod option through */
		btv->tuner_type = tuner_type;
	btv->has_radio = has_radio;

	/* LR90 Audio Routing is done by 2 hef4052, so Audio_Mask has 4 bits: 0x001c80
	 * LR26/LR50 only has 1 hef4052, Audio_Mask 0x000c00
	 * Audio options: from tuner, from tda9821/tda9821(mono,stereo,sap), from tda9874, ext., mute */
	if (has_tda9820_tda9821)
		btv->audio_mode_gpio = lt9415_audio;
	/* todo: if(has_tda9874) btv->audio_mode_gpio = fv2000s_audio; */
}

static int miro_tunermap[] = { 0,6,2,3,   4,5,6,0,  3,0,4,5,  5,2,16,1,
			       14,2,17,1, 4,1,4,3,  1,2,16,1, 4,4,4,4 };
static int miro_fmtuner[]  = { 0,0,0,0,   0,0,0,0,  0,0,0,0,  0,0,0,1,
			       1,1,1,1,   1,1,1,0,  0,0,0,0,  0,1,0,0 };

static void miro_pinnacle_gpio(struct bttv *btv)
{
	int id,msp,gpio;
	char *info;

	gpio_inout(0xffffff, 0);
	gpio = gpio_read();
	id   = ((gpio>>10) & 63) -1;
	msp  = bttv_I2CRead(btv, I2C_ADDR_MSP3400, "MSP34xx");
	if (id < 32) {
		btv->tuner_type = miro_tunermap[id];
		if (0 == (gpio & 0x20)) {
			btv->has_radio = 1;
			if (!miro_fmtuner[id]) {
				btv->has_tea575x = 1;
				btv->tea_gpio.wren = 6;
				btv->tea_gpio.most = 7;
				btv->tea_gpio.clk  = 8;
				btv->tea_gpio.data = 9;
				tea575x_init(btv);
			}
		} else {
			btv->has_radio = 0;
		}
		if (-1 != msp) {
			if (btv->c.type == BTTV_BOARD_MIRO)
				btv->c.type = BTTV_BOARD_MIROPRO;
			if (btv->c.type == BTTV_BOARD_PINNACLE)
				btv->c.type = BTTV_BOARD_PINNACLEPRO;
		}
		pr_info("%d: miro: id=%d tuner=%d radio=%s stereo=%s\n",
			btv->c.nr, id+1, btv->tuner_type,
			!btv->has_radio ? "no" :
			(btv->has_tea575x ? "tea575x" : "fmtuner"),
			(-1 == msp) ? "no" : "yes");
	} else {
		/* new cards with microtune tuner */
		id = 63 - id;
		btv->has_radio = 0;
		switch (id) {
		case 1:
			info = "PAL / mono";
			btv->tda9887_conf = TDA9887_INTERCARRIER;
			break;
		case 2:
			info = "PAL+SECAM / stereo";
			btv->has_radio = 1;
			btv->tda9887_conf = TDA9887_QSS;
			break;
		case 3:
			info = "NTSC / stereo";
			btv->has_radio = 1;
			btv->tda9887_conf = TDA9887_QSS;
			break;
		case 4:
			info = "PAL+SECAM / mono";
			btv->tda9887_conf = TDA9887_QSS;
			break;
		case 5:
			info = "NTSC / mono";
			btv->tda9887_conf = TDA9887_INTERCARRIER;
			break;
		case 6:
			info = "NTSC / stereo";
			btv->tda9887_conf = TDA9887_INTERCARRIER;
			break;
		case 7:
			info = "PAL / stereo";
			btv->tda9887_conf = TDA9887_INTERCARRIER;
			break;
		default:
			info = "oops: unknown card";
			break;
		}
		if (-1 != msp)
			btv->c.type = BTTV_BOARD_PINNACLEPRO;
		pr_info("%d: pinnacle/mt: id=%d info=\"%s\" radio=%s\n",
			btv->c.nr, id, info, btv->has_radio ? "yes" : "no");
		btv->tuner_type = TUNER_MT2032;
	}
}

/* GPIO21   L: Buffer aktiv, H: Buffer inaktiv */
#define LM1882_SYNC_DRIVE     0x200000L

static void init_ids_eagle(struct bttv *btv)
{
	gpio_inout(0xffffff,0xFFFF37);
	gpio_write(0x200020);

	/* flash strobe inverter ?! */
	gpio_write(0x200024);

	/* switch sync drive off */
	gpio_bits(LM1882_SYNC_DRIVE,LM1882_SYNC_DRIVE);

	/* set BT848 muxel to 2 */
	btaor((2)<<5, ~(2<<5), BT848_IFORM);
}

/* Muxsel helper for the IDS Eagle.
 * the eagles does not use the standard muxsel-bits but
 * has its own multiplexer */
static void eagle_muxsel(struct bttv *btv, unsigned int input)
{
	gpio_bits(3, input & 3);

	/* composite */
	/* set chroma ADC to sleep */
	btor(BT848_ADC_C_SLEEP, BT848_ADC);
	/* set to composite video */
	btand(~BT848_CONTROL_COMP, BT848_E_CONTROL);
	btand(~BT848_CONTROL_COMP, BT848_O_CONTROL);

	/* switch sync drive off */
	gpio_bits(LM1882_SYNC_DRIVE,LM1882_SYNC_DRIVE);
}

static void gvc1100_muxsel(struct bttv *btv, unsigned int input)
{
	static const int masks[] = {0x30, 0x01, 0x12, 0x23};
	gpio_write(masks[input%4]);
}

/* LMLBT4x initialization - to allow access to GPIO bits for sensors input and
   alarms output

   GPIObit    | 10 | 9 | 8 | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
   assignment | TI | O3|INx| O2| O1|IN4|IN3|IN2|IN1|   |   |

   IN - sensor inputs, INx - sensor inputs and TI XORed together
   O1,O2,O3 - alarm outputs (relays)

   OUT ENABLE   1    1   0  . 1  1   0   0 . 0   0   0    0   = 0x6C0

*/

static void init_lmlbt4x(struct bttv *btv)
{
	pr_debug("LMLBT4x init\n");
	btwrite(0x000000, BT848_GPIO_REG_INP);
	gpio_inout(0xffffff, 0x0006C0);
	gpio_write(0x000000);
}

static void sigmaSQ_muxsel(struct bttv *btv, unsigned int input)
{
	unsigned int inmux = input % 8;
	gpio_inout( 0xf, 0xf );
	gpio_bits( 0xf, inmux );
}

static void sigmaSLC_muxsel(struct bttv *btv, unsigned int input)
{
	unsigned int inmux = input % 4;
	gpio_inout( 3<<9, 3<<9 );
	gpio_bits( 3<<9, inmux<<9 );
}

static void geovision_muxsel(struct bttv *btv, unsigned int input)
{
	unsigned int inmux = input % 16;
	gpio_inout(0xf, 0xf);
	gpio_bits(0xf, inmux);
}

/*
 * The TD3116 has 2 74HC4051 muxes wired to the MUX0 input of a bt878.
 * The first 74HC4051 has the lower 8 inputs, the second one the higher 8.
 * The muxes are controlled via a 74HC373 latch which is connected to
 * GPIOs 0-7. GPIO 18 is connected to the LE signal of the latch.
 * Q0 of the latch is connected to the Enable (~E) input of the first
 * 74HC4051. Q1 - Q3 are connected to S0 - S2 of the same 74HC4051.
 * Q4 - Q7 are connected to the second 74HC4051 in the same way.
 */

static void td3116_latch_value(struct bttv *btv, u32 value)
{
	gpio_bits((1<<18) | 0xff, value);
	gpio_bits((1<<18) | 0xff, (1<<18) | value);
	udelay(1);
	gpio_bits((1<<18) | 0xff, value);
}

static void td3116_muxsel(struct bttv *btv, unsigned int input)
{
	u32 value;
	u32 highbit;

	highbit = (input & 0x8) >> 3 ;

	/* Disable outputs and set value in the mux */
	value = 0x11; /* Disable outputs */
	value |= ((input & 0x7) << 1)  << (4 * highbit);
	td3116_latch_value(btv, value);

	/* Enable the correct output */
	value &= ~0x11;
	value |= ((highbit ^ 0x1) << 4) | highbit;
	td3116_latch_value(btv, value);
}

/* ----------------------------------------------------------------------- */

static void bttv_reset_audio(struct bttv *btv)
{
	/*
	 * BT878A has a audio-reset register.
	 * 1. This register is an audio reset function but it is in
	 *    function-0 (video capture) address space.
	 * 2. It is enough to do this once per power-up of the card.
	 * 3. There is a typo in the Conexant doc -- it is not at
	 *    0x5B, but at 0x058. (B is an odd-number, obviously a typo!).
	 * --//Shrikumar 030609
	 */
	if (btv->id != 878)
		return;

	if (bttv_debug)
		pr_debug("%d: BT878A ARESET\n", btv->c.nr);
	btwrite((1<<7), 0x058);
	udelay(10);
	btwrite(     0, 0x058);
}

/* initialization part one -- before registering i2c bus */
void bttv_init_card1(struct bttv *btv)
{
	switch (btv->c.type) {
	case BTTV_BOARD_HAUPPAUGE:
	case BTTV_BOARD_HAUPPAUGE878:
		boot_msp34xx(btv,5);
		break;
	case BTTV_BOARD_VOODOOTV_200:
	case BTTV_BOARD_VOODOOTV_FM:
		boot_msp34xx(btv,20);
		break;
	case BTTV_BOARD_AVERMEDIA98:
		boot_msp34xx(btv,11);
		break;
	case BTTV_BOARD_HAUPPAUGEPVR:
		pvr_boot(btv);
		break;
	case BTTV_BOARD_TWINHAN_DST:
	case BTTV_BOARD_AVDVBT_771:
	case BTTV_BOARD_PINNACLESAT:
		btv->use_i2c_hw = 1;
		break;
	case BTTV_BOARD_ADLINK_RTV24:
		init_RTV24( btv );
		break;
	case BTTV_BOARD_PCI_8604PW:
		init_PCI8604PW(btv);
		break;

	}
	if (!bttv_tvcards[btv->c.type].has_dvb)
		bttv_reset_audio(btv);
}

/* initialization part two -- after registering i2c bus */
void bttv_init_card2(struct bttv *btv)
{
	btv->tuner_type = UNSET;

	if (BTTV_BOARD_UNKNOWN == btv->c.type) {
		bttv_readee(btv,eeprom_data,0xa0);
		identify_by_eeprom(btv,eeprom_data);
	}

	switch (btv->c.type) {
	case BTTV_BOARD_MIRO:
	case BTTV_BOARD_MIROPRO:
	case BTTV_BOARD_PINNACLE:
	case BTTV_BOARD_PINNACLEPRO:
		/* miro/pinnacle */
		miro_pinnacle_gpio(btv);
		break;
	case BTTV_BOARD_FLYVIDEO_98:
	case BTTV_BOARD_MAXI:
	case BTTV_BOARD_LIFE_FLYKIT:
	case BTTV_BOARD_FLYVIDEO:
	case BTTV_BOARD_TYPHOON_TVIEW:
	case BTTV_BOARD_CHRONOS_VS2:
	case BTTV_BOARD_FLYVIDEO_98FM:
	case BTTV_BOARD_FLYVIDEO2000:
	case BTTV_BOARD_FLYVIDEO98EZ:
	case BTTV_BOARD_CONFERENCETV:
	case BTTV_BOARD_LIFETEC_9415:
		flyvideo_gpio(btv);
		break;
	case BTTV_BOARD_HAUPPAUGE:
	case BTTV_BOARD_HAUPPAUGE878:
	case BTTV_BOARD_HAUPPAUGEPVR:
		/* pick up some config infos from the eeprom */
		bttv_readee(btv,eeprom_data,0xa0);
		hauppauge_eeprom(btv);
		break;
	case BTTV_BOARD_AVERMEDIA98:
	case BTTV_BOARD_AVPHONE98:
		bttv_readee(btv,eeprom_data,0xa0);
		avermedia_eeprom(btv);
		break;
	case BTTV_BOARD_PXC200:
		init_PXC200(btv);
		break;
	case BTTV_BOARD_PICOLO_TETRA_CHIP:
		picolo_tetra_init(btv);
		break;
	case BTTV_BOARD_VHX:
		btv->has_radio    = 1;
		btv->has_tea575x  = 1;
		btv->tea_gpio.wren = 5;
		btv->tea_gpio.most = 6;
		btv->tea_gpio.clk  = 3;
		btv->tea_gpio.data = 4;
		tea575x_init(btv);
		break;
	case BTTV_BOARD_VOBIS_BOOSTAR:
	case BTTV_BOARD_TERRATV:
		terratec_active_radio_upgrade(btv);
		break;
	case BTTV_BOARD_MAGICTVIEW061:
		if (btv->cardid == 0x3002144f) {
			btv->has_radio=1;
			pr_info("%d: radio detected by subsystem id (CPH05x)\n",
				btv->c.nr);
		}
		break;
	case BTTV_BOARD_STB2:
		if (btv->cardid == 0x3060121a) {
			/* Fix up entry for 3DFX VoodooTV 100,
			   which is an OEM STB card variant. */
			btv->has_radio=0;
			btv->tuner_type=TUNER_TEMIC_NTSC;
		}
		break;
	case BTTV_BOARD_OSPREY1x0:
	case BTTV_BOARD_OSPREY1x0_848:
	case BTTV_BOARD_OSPREY101_848:
	case BTTV_BOARD_OSPREY1x1:
	case BTTV_BOARD_OSPREY1x1_SVID:
	case BTTV_BOARD_OSPREY2xx:
	case BTTV_BOARD_OSPREY2x0_SVID:
	case BTTV_BOARD_OSPREY2x0:
	case BTTV_BOARD_OSPREY440:
	case BTTV_BOARD_OSPREY500:
	case BTTV_BOARD_OSPREY540:
	case BTTV_BOARD_OSPREY2000:
		bttv_readee(btv,eeprom_data,0xa0);
		osprey_eeprom(btv, eeprom_data);
		break;
	case BTTV_BOARD_IDS_EAGLE:
		init_ids_eagle(btv);
		break;
	case BTTV_BOARD_MODTEC_205:
		bttv_readee(btv,eeprom_data,0xa0);
		modtec_eeprom(btv);
		break;
	case BTTV_BOARD_LMLBT4:
		init_lmlbt4x(btv);
		break;
	case BTTV_BOARD_TIBET_CS16:
		tibetCS16_init(btv);
		break;
	case BTTV_BOARD_KODICOM_4400R:
		kodicom4400r_init(btv);
		break;
	case BTTV_BOARD_GEOVISION_GV800S:
		gv800s_init(btv);
		break;
	}

	/* pll configuration */
	if (!(btv->id==848 && btv->revision==0x11)) {
		/* defaults from card list */
		if (PLL_28 == bttv_tvcards[btv->c.type].pll) {
			btv->pll.pll_ifreq=28636363;
			btv->pll.pll_crystal=BT848_IFORM_XT0;
		}
		if (PLL_35 == bttv_tvcards[btv->c.type].pll) {
			btv->pll.pll_ifreq=35468950;
			btv->pll.pll_crystal=BT848_IFORM_XT1;
		}
		if (PLL_14 == bttv_tvcards[btv->c.type].pll) {
			btv->pll.pll_ifreq = 14318181;
			btv->pll.pll_crystal = BT848_IFORM_XT0;
		}
		/* insmod options can override */
		switch (pll[btv->c.nr]) {
		case 0: /* none */
			btv->pll.pll_crystal = 0;
			btv->pll.pll_ifreq   = 0;
			btv->pll.pll_ofreq   = 0;
			break;
		case 1: /* 28 MHz */
		case 28:
			btv->pll.pll_ifreq   = 28636363;
			btv->pll.pll_ofreq   = 0;
			btv->pll.pll_crystal = BT848_IFORM_XT0;
			break;
		case 2: /* 35 MHz */
		case 35:
			btv->pll.pll_ifreq   = 35468950;
			btv->pll.pll_ofreq   = 0;
			btv->pll.pll_crystal = BT848_IFORM_XT1;
			break;
		case 3: /* 14 MHz */
		case 14:
			btv->pll.pll_ifreq   = 14318181;
			btv->pll.pll_ofreq   = 0;
			btv->pll.pll_crystal = BT848_IFORM_XT0;
			break;
		}
	}
	btv->pll.pll_current = -1;

	/* tuner configuration (from card list / autodetect / insmod option) */
	if (UNSET != bttv_tvcards[btv->c.type].tuner_type)
		if (UNSET == btv->tuner_type)
			btv->tuner_type = bttv_tvcards[btv->c.type].tuner_type;
	if (UNSET != tuner[btv->c.nr])
		btv->tuner_type = tuner[btv->c.nr];

	if (btv->tuner_type == TUNER_ABSENT)
		pr_info("%d: tuner absent\n", btv->c.nr);
	else if (btv->tuner_type == UNSET)
		pr_warn("%d: tuner type unset\n", btv->c.nr);
	else
		pr_info("%d: tuner type=%d\n", btv->c.nr, btv->tuner_type);

	if (autoload != UNSET) {
		pr_warn("%d: the autoload option is obsolete\n", btv->c.nr);
		pr_warn("%d: use option msp3400, tda7432 or tvaudio to override which audio module should be used\n",
			btv->c.nr);
	}

	if (UNSET == btv->tuner_type)
		btv->tuner_type = TUNER_ABSENT;

	btv->dig = bttv_tvcards[btv->c.type].has_dig_in ?
		   bttv_tvcards[btv->c.type].video_inputs - 1 : UNSET;
	btv->svhs = bttv_tvcards[btv->c.type].svhs == NO_SVHS ?
		    UNSET : bttv_tvcards[btv->c.type].svhs;
	if (svhs[btv->c.nr] != UNSET)
		btv->svhs = svhs[btv->c.nr];
	if (remote[btv->c.nr] != UNSET)
		btv->has_remote = remote[btv->c.nr];

	if (bttv_tvcards[btv->c.type].has_radio)
		btv->has_radio = 1;
	if (bttv_tvcards[btv->c.type].has_remote)
		btv->has_remote = 1;
	if (!bttv_tvcards[btv->c.type].no_gpioirq)
		btv->gpioirq = 1;
	if (bttv_tvcards[btv->c.type].volume_gpio)
		btv->volume_gpio = bttv_tvcards[btv->c.type].volume_gpio;
	if (bttv_tvcards[btv->c.type].audio_mode_gpio)
		btv->audio_mode_gpio = bttv_tvcards[btv->c.type].audio_mode_gpio;

	if (btv->tuner_type == TUNER_ABSENT)
		return;  /* no tuner or related drivers to load */

	if (btv->has_saa6588 || saa6588[btv->c.nr]) {
		/* Probe for RDS receiver chip */
		static const unsigned short addrs[] = {
			0x20 >> 1,
			0x22 >> 1,
			I2C_CLIENT_END
		};
		struct v4l2_subdev *sd;

		sd = v4l2_i2c_new_subdev(&btv->c.v4l2_dev,
			&btv->c.i2c_adap, "saa6588", 0, addrs);
		btv->has_saa6588 = (sd != NULL);
	}

	/* try to detect audio/fader chips */

	/* First check if the user specified the audio chip via a module
	   option. */

	switch (audiodev[btv->c.nr]) {
	case -1:
		return;	/* do not load any audio module */

	case 0: /* autodetect */
		break;

	case 1: {
		/* The user specified that we should probe for msp3400 */
		static const unsigned short addrs[] = {
			I2C_ADDR_MSP3400 >> 1,
			I2C_ADDR_MSP3400_ALT >> 1,
			I2C_CLIENT_END
		};

		btv->sd_msp34xx = v4l2_i2c_new_subdev(&btv->c.v4l2_dev,
			&btv->c.i2c_adap, "msp3400", 0, addrs);
		if (btv->sd_msp34xx)
			return;
		goto no_audio;
	}

	case 2: {
		/* The user specified that we should probe for tda7432 */
		static const unsigned short addrs[] = {
			I2C_ADDR_TDA7432 >> 1,
			I2C_CLIENT_END
		};

		if (v4l2_i2c_new_subdev(&btv->c.v4l2_dev,
				&btv->c.i2c_adap, "tda7432", 0, addrs))
			return;
		goto no_audio;
	}

	case 3: {
		/* The user specified that we should probe for tvaudio */
		btv->sd_tvaudio = v4l2_i2c_new_subdev(&btv->c.v4l2_dev,
			&btv->c.i2c_adap, "tvaudio", 0, tvaudio_addrs());
		if (btv->sd_tvaudio)
			return;
		goto no_audio;
	}

	default:
		pr_warn("%d: unknown audiodev value!\n", btv->c.nr);
		return;
	}

	/* There were no overrides, so now we try to discover this through the
	   card definition */

	/* probe for msp3400 first: this driver can detect whether or not
	   it really is a msp3400, so it will return NULL when the device
	   found is really something else (e.g. a tea6300). */
	if (!bttv_tvcards[btv->c.type].no_msp34xx) {
		btv->sd_msp34xx = v4l2_i2c_new_subdev(&btv->c.v4l2_dev,
			&btv->c.i2c_adap, "msp3400",
			0, I2C_ADDRS(I2C_ADDR_MSP3400 >> 1));
	} else if (bttv_tvcards[btv->c.type].msp34xx_alt) {
		btv->sd_msp34xx = v4l2_i2c_new_subdev(&btv->c.v4l2_dev,
			&btv->c.i2c_adap, "msp3400",
			0, I2C_ADDRS(I2C_ADDR_MSP3400_ALT >> 1));
	}

	/* If we found a msp34xx, then we're done. */
	if (btv->sd_msp34xx)
		return;

	/* Now see if we can find one of the tvaudio devices. */
	btv->sd_tvaudio = v4l2_i2c_new_subdev(&btv->c.v4l2_dev,
		&btv->c.i2c_adap, "tvaudio", 0, tvaudio_addrs());
	if (btv->sd_tvaudio) {
		/* There may be two tvaudio chips on the card, so try to
		   find another. */
		v4l2_i2c_new_subdev(&btv->c.v4l2_dev,
			&btv->c.i2c_adap, "tvaudio", 0, tvaudio_addrs());
	}

	/* it might also be a tda7432. */
	if (!bttv_tvcards[btv->c.type].no_tda7432) {
		static const unsigned short addrs[] = {
			I2C_ADDR_TDA7432 >> 1,
			I2C_CLIENT_END
		};

		btv->sd_tda7432 = v4l2_i2c_new_subdev(&btv->c.v4l2_dev,
				&btv->c.i2c_adap, "tda7432", 0, addrs);
		if (btv->sd_tda7432)
			return;
	}
	if (btv->sd_tvaudio)
		return;

no_audio:
	pr_warn("%d: audio absent, no audio device found!\n", btv->c.nr);
}


/* initialize the tuner */
void bttv_init_tuner(struct bttv *btv)
{
	int addr = ADDR_UNSET;

	if (ADDR_UNSET != bttv_tvcards[btv->c.type].tuner_addr)
		addr = bttv_tvcards[btv->c.type].tuner_addr;

	if (btv->tuner_type != TUNER_ABSENT) {
		struct tuner_setup tun_setup;

		/* Load tuner module before issuing tuner config call! */
		if (btv->has_radio)
			v4l2_i2c_new_subdev(&btv->c.v4l2_dev,
				&btv->c.i2c_adap, "tuner",
				0, v4l2_i2c_tuner_addrs(ADDRS_RADIO));
		v4l2_i2c_new_subdev(&btv->c.v4l2_dev,
				&btv->c.i2c_adap, "tuner",
				0, v4l2_i2c_tuner_addrs(ADDRS_DEMOD));
		v4l2_i2c_new_subdev(&btv->c.v4l2_dev,
				&btv->c.i2c_adap, "tuner",
				0, v4l2_i2c_tuner_addrs(ADDRS_TV_WITH_DEMOD));

		tun_setup.mode_mask = T_ANALOG_TV;
		tun_setup.type = btv->tuner_type;
		tun_setup.addr = addr;

		if (btv->has_radio)
			tun_setup.mode_mask |= T_RADIO;

		bttv_call_all(btv, tuner, s_type_addr, &tun_setup);
	}

	if (btv->tda9887_conf) {
		struct v4l2_priv_tun_config tda9887_cfg;

		tda9887_cfg.tuner = TUNER_TDA9887;
		tda9887_cfg.priv = &btv->tda9887_conf;

		bttv_call_all(btv, tuner, s_config, &tda9887_cfg);
	}
}

/* ----------------------------------------------------------------------- */

static void modtec_eeprom(struct bttv *btv)
{
	if( strncmp(&(eeprom_data[0x1e]),"Temic 4066 FY5",14) ==0) {
		btv->tuner_type=TUNER_TEMIC_4066FY5_PAL_I;
		pr_info("%d: Modtec: Tuner autodetected by eeprom: %s\n",
			btv->c.nr, &eeprom_data[0x1e]);
	} else if (strncmp(&(eeprom_data[0x1e]),"Alps TSBB5",10) ==0) {
		btv->tuner_type=TUNER_ALPS_TSBB5_PAL_I;
		pr_info("%d: Modtec: Tuner autodetected by eeprom: %s\n",
			btv->c.nr, &eeprom_data[0x1e]);
	} else if (strncmp(&(eeprom_data[0x1e]),"Philips FM1246",14) ==0) {
		btv->tuner_type=TUNER_PHILIPS_NTSC;
		pr_info("%d: Modtec: Tuner autodetected by eeprom: %s\n",
			btv->c.nr, &eeprom_data[0x1e]);
	} else {
		pr_info("%d: Modtec: Unknown TunerString: %s\n",
			btv->c.nr, &eeprom_data[0x1e]);
	}
}

static void hauppauge_eeprom(struct bttv *btv)
{
	struct tveeprom tv;

	tveeprom_hauppauge_analog(&tv, eeprom_data);
	btv->tuner_type = tv.tuner_type;
	btv->has_radio  = tv.has_radio;

	pr_info("%d: Hauppauge eeprom indicates model#%d\n",
		btv->c.nr, tv.model);

	/*
	 * Some of the 878 boards have duplicate PCI IDs. Switch the board
	 * type based on model #.
	 */
	if(tv.model == 64900) {
		pr_info("%d: Switching board type from %s to %s\n",
			btv->c.nr,
			bttv_tvcards[btv->c.type].name,
			bttv_tvcards[BTTV_BOARD_HAUPPAUGE_IMPACTVCB].name);
		btv->c.type = BTTV_BOARD_HAUPPAUGE_IMPACTVCB;
	}

	/* The 61334 needs the msp3410 to do the radio demod to get sound */
	if (tv.model == 61334)
		btv->radio_uses_msp_demodulator = 1;
}

/* ----------------------------------------------------------------------- */

static void bttv_tea575x_set_pins(struct snd_tea575x *tea, u8 pins)
{
	struct bttv *btv = tea->private_data;
	struct bttv_tea575x_gpio gpio = btv->tea_gpio;
	u16 val = 0;

	val |= (pins & TEA575X_DATA) ? (1 << gpio.data) : 0;
	val |= (pins & TEA575X_CLK)  ? (1 << gpio.clk)  : 0;
	val |= (pins & TEA575X_WREN) ? (1 << gpio.wren) : 0;

	gpio_bits((1 << gpio.data) | (1 << gpio.clk) | (1 << gpio.wren), val);
	if (btv->mbox_ior) {
		/* IOW and CSEL active */
		gpio_bits(btv->mbox_iow | btv->mbox_csel, 0);
		udelay(5);
		/* all inactive */
		gpio_bits(btv->mbox_ior | btv->mbox_iow | btv->mbox_csel,
			  btv->mbox_ior | btv->mbox_iow | btv->mbox_csel);
	}
}

static u8 bttv_tea575x_get_pins(struct snd_tea575x *tea)
{
	struct bttv *btv = tea->private_data;
	struct bttv_tea575x_gpio gpio = btv->tea_gpio;
	u8 ret = 0;
	u16 val;

	if (btv->mbox_ior) {
		/* IOR and CSEL active */
		gpio_bits(btv->mbox_ior | btv->mbox_csel, 0);
		udelay(5);
	}
	val = gpio_read();
	if (btv->mbox_ior) {
		/* all inactive */
		gpio_bits(btv->mbox_ior | btv->mbox_iow | btv->mbox_csel,
			  btv->mbox_ior | btv->mbox_iow | btv->mbox_csel);
	}

	if (val & (1 << gpio.data))
		ret |= TEA575X_DATA;
	if (val & (1 << gpio.most))
		ret |= TEA575X_MOST;

	return ret;
}

static void bttv_tea575x_set_direction(struct snd_tea575x *tea, bool output)
{
	struct bttv *btv = tea->private_data;
	struct bttv_tea575x_gpio gpio = btv->tea_gpio;
	u32 mask = (1 << gpio.clk) | (1 << gpio.wren) | (1 << gpio.data) |
		   (1 << gpio.most);

	if (output)
		gpio_inout(mask, (1 << gpio.data) | (1 << gpio.clk) |
				 (1 << gpio.wren));
	else
		gpio_inout(mask, (1 << gpio.clk) | (1 << gpio.wren));
}

static const struct snd_tea575x_ops bttv_tea_ops = {
	.set_pins = bttv_tea575x_set_pins,
	.get_pins = bttv_tea575x_get_pins,
	.set_direction = bttv_tea575x_set_direction,
};

static int tea575x_init(struct bttv *btv)
{
	btv->tea.private_data = btv;
	btv->tea.ops = &bttv_tea_ops;
	if (!snd_tea575x_hw_init(&btv->tea)) {
		pr_info("%d: detected TEA575x radio\n", btv->c.nr);
		btv->tea.mute = false;
		return 0;
	}

	btv->has_tea575x = 0;
	btv->has_radio = 0;

	return -ENODEV;
}

/* ----------------------------------------------------------------------- */

static int terratec_active_radio_upgrade(struct bttv *btv)
{
	btv->has_radio    = 1;
	btv->has_tea575x  = 1;
	btv->tea_gpio.wren = 4;
	btv->tea_gpio.most = 5;
	btv->tea_gpio.clk  = 3;
	btv->tea_gpio.data = 2;

	btv->mbox_iow     = 1 <<  8;
	btv->mbox_ior     = 1 <<  9;
	btv->mbox_csel    = 1 << 10;

	if (!tea575x_init(btv)) {
		pr_info("%d: Terratec Active Radio Upgrade found\n", btv->c.nr);
		btv->has_saa6588 = 1;
	}

	return 0;
}


/* ----------------------------------------------------------------------- */

/*
 * minimal bootstrap for the WinTV/PVR -- upload altera firmware.
 *
 * The hcwamc.rbf firmware file is on the Hauppauge driver CD.  Have
 * a look at Pvr/pvr45xxx.EXE (self-extracting zip archive, can be
 * unpacked with unzip).
 */
#define PVR_GPIO_DELAY		10

#define BTTV_ALT_DATA		0x000001
#define BTTV_ALT_DCLK		0x100000
#define BTTV_ALT_NCONFIG	0x800000

static int pvr_altera_load(struct bttv *btv, const u8 *micro, u32 microlen)
{
	u32 n;
	u8 bits;
	int i;

	gpio_inout(0xffffff,BTTV_ALT_DATA|BTTV_ALT_DCLK|BTTV_ALT_NCONFIG);
	gpio_write(0);
	udelay(PVR_GPIO_DELAY);

	gpio_write(BTTV_ALT_NCONFIG);
	udelay(PVR_GPIO_DELAY);

	for (n = 0; n < microlen; n++) {
		bits = micro[n];
		for (i = 0 ; i < 8 ; i++) {
			gpio_bits(BTTV_ALT_DCLK,0);
			if (bits & 0x01)
				gpio_bits(BTTV_ALT_DATA,BTTV_ALT_DATA);
			else
				gpio_bits(BTTV_ALT_DATA,0);
			gpio_bits(BTTV_ALT_DCLK,BTTV_ALT_DCLK);
			bits >>= 1;
		}
	}
	gpio_bits(BTTV_ALT_DCLK,0);
	udelay(PVR_GPIO_DELAY);

	/* begin Altera init loop (Not necessary,but doesn't hurt) */
	for (i = 0 ; i < 30 ; i++) {
		gpio_bits(BTTV_ALT_DCLK,0);
		gpio_bits(BTTV_ALT_DCLK,BTTV_ALT_DCLK);
	}
	gpio_bits(BTTV_ALT_DCLK,0);
	return 0;
}

static int pvr_boot(struct bttv *btv)
{
	const struct firmware *fw_entry;
	int rc;

	rc = request_firmware(&fw_entry, "hcwamc.rbf", &btv->c.pci->dev);
	if (rc != 0) {
		pr_warn("%d: no altera firmware [via hotplug]\n", btv->c.nr);
		return rc;
	}
	rc = pvr_altera_load(btv, fw_entry->data, fw_entry->size);
	pr_info("%d: altera firmware upload %s\n",
		btv->c.nr, (rc < 0) ? "failed" : "ok");
	release_firmware(fw_entry);
	return rc;
}

/* ----------------------------------------------------------------------- */
/* some osprey specific stuff                                              */

static void osprey_eeprom(struct bttv *btv, const u8 ee[256])
{
	int i;
	u32 serial = 0;
	int cardid = -1;

	/* This code will nevery actually get called in this case.... */
	if (btv->c.type == BTTV_BOARD_UNKNOWN) {
		/* this might be an antique... check for MMAC label in eeprom */
		if (!strncmp(ee, "MMAC", 4)) {
			u8 checksum = 0;
			for (i = 0; i < 21; i++)
				checksum += ee[i];
			if (checksum != ee[21])
				return;
			cardid = BTTV_BOARD_OSPREY1x0_848;
			for (i = 12; i < 21; i++)
				serial *= 10, serial += ee[i] - '0';
		}
	} else {
		unsigned short type;

		for (i = 4 * 16; i < 8 * 16; i += 16) {
			u16 checksum = (__force u16)ip_compute_csum(ee + i, 16);

			if ((checksum & 0xff) + (checksum >> 8) == 0xff)
				break;
		}
		if (i >= 8*16)
			return;
		ee += i;

		/* found a valid descriptor */
		type = get_unaligned_be16((__be16 *)(ee+4));

		switch(type) {
		/* 848 based */
		case 0x0004:
			cardid = BTTV_BOARD_OSPREY1x0_848;
			break;
		case 0x0005:
			cardid = BTTV_BOARD_OSPREY101_848;
			break;

		/* 878 based */
		case 0x0012:
		case 0x0013:
			cardid = BTTV_BOARD_OSPREY1x0;
			break;
		case 0x0014:
		case 0x0015:
			cardid = BTTV_BOARD_OSPREY1x1;
			break;
		case 0x0016:
		case 0x0017:
		case 0x0020:
			cardid = BTTV_BOARD_OSPREY1x1_SVID;
			break;
		case 0x0018:
		case 0x0019:
		case 0x001E:
		case 0x001F:
			cardid = BTTV_BOARD_OSPREY2xx;
			break;
		case 0x001A:
		case 0x001B:
			cardid = BTTV_BOARD_OSPREY2x0_SVID;
			break;
		case 0x0040:
			cardid = BTTV_BOARD_OSPREY500;
			break;
		case 0x0050:
		case 0x0056:
			cardid = BTTV_BOARD_OSPREY540;
			/* bttv_osprey_540_init(btv); */
			break;
		case 0x0060:
		case 0x0070:
		case 0x00A0:
			cardid = BTTV_BOARD_OSPREY2x0;
			/* enable output on select control lines */
			gpio_inout(0xffffff,0x000303);
			break;
		case 0x00D8:
			cardid = BTTV_BOARD_OSPREY440;
			break;
		default:
			/* unknown...leave generic, but get serial # */
			pr_info("%d: osprey eeprom: unknown card type 0x%04x\n",
				btv->c.nr, type);
			break;
		}
		serial = get_unaligned_be32((__be32 *)(ee+6));
	}

	pr_info("%d: osprey eeprom: card=%d '%s' serial=%u\n",
		btv->c.nr, cardid,
		cardid > 0 ? bttv_tvcards[cardid].name : "Unknown", serial);

	if (cardid<0 || btv->c.type == cardid)
		return;

	/* card type isn't set correctly */
	if (card[btv->c.nr] < bttv_num_tvcards) {
		pr_warn("%d: osprey eeprom: Not overriding user specified card type\n",
			btv->c.nr);
	} else {
		pr_info("%d: osprey eeprom: Changing card type from %d to %d\n",
			btv->c.nr, btv->c.type, cardid);
		btv->c.type = cardid;
	}
}

/* ----------------------------------------------------------------------- */
/* AVermedia specific stuff, from  bktr_card.c                             */

static int tuner_0_table[] = {
	TUNER_PHILIPS_NTSC,  TUNER_PHILIPS_PAL /* PAL-BG*/,
	TUNER_PHILIPS_PAL,   TUNER_PHILIPS_PAL /* PAL-I*/,
	TUNER_PHILIPS_PAL,   TUNER_PHILIPS_PAL,
	TUNER_PHILIPS_SECAM, TUNER_PHILIPS_SECAM,
	TUNER_PHILIPS_SECAM, TUNER_PHILIPS_PAL,
	TUNER_PHILIPS_FM1216ME_MK3 };

static int tuner_1_table[] = {
	TUNER_TEMIC_NTSC,  TUNER_TEMIC_PAL,
	TUNER_TEMIC_PAL,   TUNER_TEMIC_PAL,
	TUNER_TEMIC_PAL,   TUNER_TEMIC_PAL,
	TUNER_TEMIC_4012FY5, TUNER_TEMIC_4012FY5, /* TUNER_TEMIC_SECAM */
	TUNER_TEMIC_4012FY5, TUNER_TEMIC_PAL};

static void avermedia_eeprom(struct bttv *btv)
{
	int tuner_make, tuner_tv_fm, tuner_format, tuner_type = 0;

	tuner_make      = (eeprom_data[0x41] & 0x7);
	tuner_tv_fm     = (eeprom_data[0x41] & 0x18) >> 3;
	tuner_format    = (eeprom_data[0x42] & 0xf0) >> 4;
	btv->has_remote = (eeprom_data[0x42] & 0x01);

	if (tuner_make == 0 || tuner_make == 2)
		if (tuner_format <= 0x0a)
			tuner_type = tuner_0_table[tuner_format];
	if (tuner_make == 1)
		if (tuner_format <= 9)
			tuner_type = tuner_1_table[tuner_format];

	if (tuner_make == 4)
		if (tuner_format == 0x09)
			tuner_type = TUNER_LG_NTSC_NEW_TAPC; /* TAPC-G702P */

	pr_info("%d: Avermedia eeprom[0x%02x%02x]: tuner=",
		btv->c.nr, eeprom_data[0x41], eeprom_data[0x42]);
	if (tuner_type) {
		btv->tuner_type = tuner_type;
		pr_cont("%d", tuner_type);
	} else
		pr_cont("Unknown type");
	pr_cont(" radio:%s remote control:%s\n",
	       tuner_tv_fm     ? "yes" : "no",
	       btv->has_remote ? "yes" : "no");
}

/*
 * For Voodoo TV/FM and Voodoo 200.  These cards' tuners use a TDA9880
 * analog demod, which is not I2C controlled like the newer and more common
 * TDA9887 series.  Instead is has two tri-state input pins, S0 and S1,
 * that control the IF for the video and audio.  Apparently, bttv GPIO
 * 0x10000 is connected to S0.  S0 low selects a 38.9 MHz VIF for B/G/D/K/I
 * (i.e., PAL) while high selects 45.75 MHz for M/N (i.e., NTSC).
 */
u32 bttv_tda9880_setnorm(struct bttv *btv, u32 gpiobits)
{

	if (btv->audio_input == TVAUDIO_INPUT_TUNER) {
		if (bttv_tvnorms[btv->tvnorm].v4l2_id & V4L2_STD_MN)
			gpiobits |= 0x10000;
		else
			gpiobits &= ~0x10000;
	}

	gpio_bits(bttv_tvcards[btv->c.type].gpiomask, gpiobits);
	return gpiobits;
}


/*
 * reset/enable the MSP on some Hauppauge cards
 * Thanks to Kyösti Mälkki (kmalkki@cc.hut.fi)!
 *
 * Hauppauge:  pin  5
 * Voodoo:     pin 20
 */
static void boot_msp34xx(struct bttv *btv, int pin)
{
	int mask = (1 << pin);

	gpio_inout(mask,mask);
	gpio_bits(mask,0);
	mdelay(2);
	udelay(500);
	gpio_bits(mask,mask);

	if (bttv_gpio)
		bttv_gpio_tracking(btv,"msp34xx");
	if (bttv_verbose)
		pr_info("%d: Hauppauge/Voodoo msp34xx: reset line init [%d]\n",
			btv->c.nr, pin);
}

/* ----------------------------------------------------------------------- */
/*  Imagenation L-Model PXC200 Framegrabber */
/*  This is basically the same procedure as
 *  used by Alessandro Rubini in his pxc200
 *  driver, but using BTTV functions */

static void init_PXC200(struct bttv *btv)
{
	static int vals[] = { 0x08, 0x09, 0x0a, 0x0b, 0x0d, 0x0d, 0x01, 0x02,
			      0x03, 0x04, 0x05, 0x06, 0x00 };
	unsigned int i;
	int tmp;
	u32 val;

	/* Initialise GPIO-connevted stuff */
	gpio_inout(0xffffff, (1<<13));
	gpio_write(0);
	udelay(3);
	gpio_write(1<<13);
	/* GPIO inputs are pulled up, so no need to drive
	 * reset pin any longer */
	gpio_bits(0xffffff, 0);
	if (bttv_gpio)
		bttv_gpio_tracking(btv,"pxc200");

	/*  we could/should try and reset/control the AD pots? but
	    right now  we simply  turned off the crushing.  Without
	    this the AGC drifts drifts
	    remember the EN is reverse logic -->
	    setting BT848_ADC_AGC_EN disable the AGC
	    tboult@eecs.lehigh.edu
	*/

	btwrite(BT848_ADC_RESERVED|BT848_ADC_AGC_EN, BT848_ADC);

	/*	Initialise MAX517 DAC */
	pr_info("Setting DAC reference voltage level ...\n");
	bttv_I2CWrite(btv,0x5E,0,0x80,1);

	/*	Initialise 12C508 PIC */
	/*	The I2CWrite and I2CRead commmands are actually to the
	 *	same chips - but the R/W bit is included in the address
	 *	argument so the numbers are different */


	pr_info("Initialising 12C508 PIC chip ...\n");

	/* First of all, enable the clock line. This is used in the PXC200-F */
	val = btread(BT848_GPIO_DMA_CTL);
	val |= BT848_GPIO_DMA_CTL_GPCLKMODE;
	btwrite(val, BT848_GPIO_DMA_CTL);

	/* Then, push to 0 the reset pin long enough to reset the *
	 * device same as above for the reset line, but not the same
	 * value sent to the GPIO-connected stuff
	 * which one is the good one? */
	gpio_inout(0xffffff,(1<<2));
	gpio_write(0);
	udelay(10);
	gpio_write(1<<2);

	for (i = 0; i < ARRAY_SIZE(vals); i++) {
		tmp=bttv_I2CWrite(btv,0x1E,0,vals[i],1);
		if (tmp != -1) {
			pr_info("I2C Write(%2.2x) = %i\nI2C Read () = %2.2x\n\n",
			       vals[i],tmp,bttv_I2CRead(btv,0x1F,NULL));
		}
	}

	pr_info("PXC200 Initialised\n");
}



/* ----------------------------------------------------------------------- */
/*
 *  The Adlink RTV-24 (aka Angelo) has some special initialisation to unlock
 *  it. This apparently involves the following procedure for each 878 chip:
 *
 *  1) write 0x00C3FEFF to the GPIO_OUT_EN register
 *
 *  2)  write to GPIO_DATA
 *      - 0x0E
 *      - sleep 1ms
 *      - 0x10 + 0x0E
 *      - sleep 10ms
 *      - 0x0E
 *     read from GPIO_DATA into buf (uint_32)
 *      - if ( data>>18 & 0x01 != 0) || ( buf>>19 & 0x01 != 1 )
 *                 error. ERROR_CPLD_Check_Failed stop.
 *
 *  3) write to GPIO_DATA
 *      - write 0x4400 + 0x0E
 *      - sleep 10ms
 *      - write 0x4410 + 0x0E
 *      - sleep 1ms
 *      - write 0x0E
 *     read from GPIO_DATA into buf (uint_32)
 *      - if ( buf>>18 & 0x01 ) || ( buf>>19 & 0x01 != 0 )
 *                error. ERROR_CPLD_Check_Failed.
 */
/* ----------------------------------------------------------------------- */
static void
init_RTV24 (struct bttv *btv)
{
	uint32_t dataRead = 0;
	long watchdog_value = 0x0E;

	pr_info("%d: Adlink RTV-24 initialisation in progress ...\n",
		btv->c.nr);

	btwrite (0x00c3feff, BT848_GPIO_OUT_EN);

	btwrite (0 + watchdog_value, BT848_GPIO_DATA);
	msleep (1);
	btwrite (0x10 + watchdog_value, BT848_GPIO_DATA);
	msleep (10);
	btwrite (0 + watchdog_value, BT848_GPIO_DATA);

	dataRead = btread (BT848_GPIO_DATA);

	if ((((dataRead >> 18) & 0x01) != 0) || (((dataRead >> 19) & 0x01) != 1)) {
		pr_info("%d: Adlink RTV-24 initialisation(1) ERROR_CPLD_Check_Failed (read %d)\n",
			btv->c.nr, dataRead);
	}

	btwrite (0x4400 + watchdog_value, BT848_GPIO_DATA);
	msleep (10);
	btwrite (0x4410 + watchdog_value, BT848_GPIO_DATA);
	msleep (1);
	btwrite (watchdog_value, BT848_GPIO_DATA);
	msleep (1);
	dataRead = btread (BT848_GPIO_DATA);

	if ((((dataRead >> 18) & 0x01) != 0) || (((dataRead >> 19) & 0x01) != 0)) {
		pr_info("%d: Adlink RTV-24 initialisation(2) ERROR_CPLD_Check_Failed (read %d)\n",
			btv->c.nr, dataRead);

		return;
	}

	pr_info("%d: Adlink RTV-24 initialisation complete\n", btv->c.nr);
}



/* ----------------------------------------------------------------------- */
/*
 *  The PCI-8604PW contains a CPLD, probably an ispMACH 4A, that filters
 *  the PCI REQ signals comming from the four BT878 chips. After power
 *  up, the CPLD does not forward requests to the bus, which prevents
 *  the BT878 from fetching RISC instructions from memory. While the
 *  CPLD is connected to most of the GPIOs of PCI device 0xD, only
 *  five appear to play a role in unlocking the REQ signal. The following
 *  sequence has been determined by trial and error without access to the
 *  original driver.
 *
 *  Eight GPIOs of device 0xC are provided on connector CN4 (4 in, 4 out).
 *  Devices 0xE and 0xF do not appear to have anything connected to their
 *  GPIOs.
 *
 *  The correct GPIO_OUT_EN value might have some more bits set. It should
 *  be possible to derive it from a boundary scan of the CPLD. Its JTAG
 *  pins are routed to test points.
 *
 */
/* ----------------------------------------------------------------------- */
static void
init_PCI8604PW(struct bttv *btv)
{
	int state;

	if ((PCI_SLOT(btv->c.pci->devfn) & ~3) != 0xC) {
		pr_warn("This is not a PCI-8604PW\n");
		return;
	}

	if (PCI_SLOT(btv->c.pci->devfn) != 0xD)
		return;

	btwrite(0x080002, BT848_GPIO_OUT_EN);

	state = (btread(BT848_GPIO_DATA) >> 21) & 7;

	for (;;) {
		switch (state) {
		case 1:
		case 5:
		case 6:
		case 4:
			pr_debug("PCI-8604PW in state %i, toggling pin\n",
				 state);
			btwrite(0x080000, BT848_GPIO_DATA);
			msleep(1);
			btwrite(0x000000, BT848_GPIO_DATA);
			msleep(1);
			break;
		case 7:
			pr_info("PCI-8604PW unlocked\n");
			return;
		case 0:
			/* FIXME: If we are in state 7 and toggle GPIO[19] one
			   more time, the CPLD goes into state 0, where PCI bus
			   mastering is inhibited again. We have not managed to
			   get out of that state. */

			pr_err("PCI-8604PW locked until reset\n");
			return;
		default:
			pr_err("PCI-8604PW in unknown state %i\n", state);
			return;
		}

		state = (state << 4) | ((btread(BT848_GPIO_DATA) >> 21) & 7);

		switch (state) {
		case 0x15:
		case 0x56:
		case 0x64:
		case 0x47:
		/* The transition from state 7 to state 0 is, as explained
		   above, valid but undesired and with this code impossible
		   as we exit as soon as we are in state 7.
		case 0x70: */
			break;
		default:
			pr_err("PCI-8604PW invalid transition %i -> %i\n",
			       state >> 4, state & 7);
			return;
		}
		state &= 7;
	}
}

/* RemoteVision MX (rv605) muxsel helper [Miguel Freitas]
 *
 * This is needed because rv605 don't use a normal multiplex, but a crosspoint
 * switch instead (CD22M3494E). This IC can have multiple active connections
 * between Xn (input) and Yn (output) pins. We need to clear any existing
 * connection prior to establish a new one, pulsing the STROBE pin.
 *
 * The board hardwire Y0 (xpoint) to MUX1 and MUXOUT to Yin.
 * GPIO pins are wired as:
 *  GPIO[0:3] - AX[0:3] (xpoint) - P1[0:3] (microcontroller)
 *  GPIO[4:6] - AY[0:2] (xpoint) - P1[4:6] (microcontroller)
 *  GPIO[7]   - DATA (xpoint)    - P1[7] (microcontroller)
 *  GPIO[8]   -                  - P3[5] (microcontroller)
 *  GPIO[9]   - RESET (xpoint)   - P3[6] (microcontroller)
 *  GPIO[10]  - STROBE (xpoint)  - P3[7] (microcontroller)
 *  GPINTR    -                  - P3[4] (microcontroller)
 *
 * The microcontroller is a 80C32 like. It should be possible to change xpoint
 * configuration either directly (as we are doing) or using the microcontroller
 * which is also wired to I2C interface. I have no further info on the
 * microcontroller features, one would need to disassembly the firmware.
 * note: the vendor refused to give any information on this product, all
 *       that stuff was found using a multimeter! :)
 */
static void rv605_muxsel(struct bttv *btv, unsigned int input)
{
	static const u8 muxgpio[] = { 0x3, 0x1, 0x2, 0x4, 0xf, 0x7, 0xe, 0x0,
				      0xd, 0xb, 0xc, 0x6, 0x9, 0x5, 0x8, 0xa };

	gpio_bits(0x07f, muxgpio[input]);

	/* reset all conections */
	gpio_bits(0x200,0x200);
	mdelay(1);
	gpio_bits(0x200,0x000);
	mdelay(1);

	/* create a new connection */
	gpio_bits(0x480,0x480);
	mdelay(1);
	gpio_bits(0x480,0x080);
	mdelay(1);
}

/* Tibet Systems 'Progress DVR' CS16 muxsel helper [Chris Fanning]
 *
 * The CS16 (available on eBay cheap) is a PCI board with four Fusion
 * 878A chips, a PCI bridge, an Atmel microcontroller, four sync separator
 * chips, ten eight input analog multiplexors, a not chip and a few
 * other components.
 *
 * 16 inputs on a secondary bracket are provided and can be selected
 * from each of the four capture chips.  Two of the eight input
 * multiplexors are used to select from any of the 16 input signals.
 *
 * Unsupported hardware capabilities:
 *  . A video output monitor on the secondary bracket can be selected from
 *    one of the 878A chips.
 *  . Another passthrough but I haven't spent any time investigating it.
 *  . Digital I/O (logic level connected to GPIO) is available from an
 *    onboard header.
 *
 * The on chip input mux should always be set to 2.
 * GPIO[16:19] - Video input selection
 * GPIO[0:3]   - Video output monitor select (only available from one 878A)
 * GPIO[?:?]   - Digital I/O.
 *
 * There is an ATMEL microcontroller with an 8031 core on board.  I have not
 * determined what function (if any) it provides.  With the microcontroller
 * and sync separator chips a guess is that it might have to do with video
 * switching and maybe some digital I/O.
 */
static void tibetCS16_muxsel(struct bttv *btv, unsigned int input)
{
	/* video mux */
	gpio_bits(0x0f0000, input << 16);
}

static void tibetCS16_init(struct bttv *btv)
{
	/* enable gpio bits, mask obtained via btSpy */
	gpio_inout(0xffffff, 0x0f7fff);
	gpio_write(0x0f7fff);
}

/*
 * The following routines for the Kodicom-4400r get a little mind-twisting.
 * There is a "master" controller and three "slave" controllers, together
 * an analog switch which connects any of 16 cameras to any of the BT87A's.
 * The analog switch is controlled by the "master", but the detection order
 * of the four BT878A chips is in an order which I just don't understand.
 * The "master" is actually the second controller to be detected.  The
 * logic on the board uses logical numbers for the 4 controllers, but
 * those numbers are different from the detection sequence.  When working
 * with the analog switch, we need to "map" from the detection sequence
 * over to the board's logical controller number.  This mapping sequence
 * is {3, 0, 2, 1}, i.e. the first controller to be detected is logical
 * unit 3, the second (which is the master) is logical unit 0, etc.
 * We need to maintain the status of the analog switch (which of the 16
 * cameras is connected to which of the 4 controllers) in sw_status array.
 */

/*
 * First a routine to set the analog switch, which controls which camera
 * is routed to which controller.  The switch comprises an X-address
 * (gpio bits 0-3, representing the camera, ranging from 0-15), and a
 * Y-address (gpio bits 4-6, representing the controller, ranging from 0-3).
 * A data value (gpio bit 7) of '1' enables the switch, and '0' disables
 * the switch.  A STROBE bit (gpio bit 8) latches the data value into the
 * specified address.  The idea is to set the address and data, then bring
 * STROBE high, and finally bring STROBE back to low.
 */
static void kodicom4400r_write(struct bttv *btv,
			       unsigned char xaddr,
			       unsigned char yaddr,
			       unsigned char data) {
	unsigned int udata;

	udata = (data << 7) | ((yaddr&3) << 4) | (xaddr&0xf);
	gpio_bits(0x1ff, udata);		/* write ADDR and DAT */
	gpio_bits(0x1ff, udata | (1 << 8));	/* strobe high */
	gpio_bits(0x1ff, udata);		/* strobe low */
}

/*
 * Next the mux select.  Both the "master" and "slave" 'cards' (controllers)
 * use this routine.  The routine finds the "master" for the card, maps
 * the controller number from the detected position over to the logical
 * number, writes the appropriate data to the analog switch, and housekeeps
 * the local copy of the switch information.  The parameter 'input' is the
 * requested camera number (0 - 15).
 */
static void kodicom4400r_muxsel(struct bttv *btv, unsigned int input)
{
	int xaddr, yaddr;
	struct bttv *mctlr;
	static unsigned char map[4] = {3, 0, 2, 1};

	mctlr = master[btv->c.nr];
	if (mctlr == NULL) {	/* ignore if master not yet detected */
		return;
	}
	yaddr = (btv->c.nr - mctlr->c.nr + 1) & 3; /* the '&' is for safety */
	yaddr = map[yaddr];
	xaddr = input & 0xf;
	/* Check if the controller/camera pair has changed, else ignore */
	if (mctlr->sw_status[yaddr] != xaddr)
	{
		/* "open" the old switch, "close" the new one, save the new */
		kodicom4400r_write(mctlr, mctlr->sw_status[yaddr], yaddr, 0);
		mctlr->sw_status[yaddr] = xaddr;
		kodicom4400r_write(mctlr, xaddr, yaddr, 1);
	}
}

/*
 * During initialisation, we need to reset the analog switch.  We
 * also preset the switch to map the 4 connectors on the card to the
 * *user's* (see above description of kodicom4400r_muxsel) channels
 * 0 through 3
 */
static void kodicom4400r_init(struct bttv *btv)
{
	int ix;

	gpio_inout(0x0003ff, 0x0003ff);
	gpio_write(1 << 9);	/* reset MUX */
	gpio_write(0);
	/* Preset camera 0 to the 4 controllers */
	for (ix = 0; ix < 4; ix++) {
		btv->sw_status[ix] = ix;
		kodicom4400r_write(btv, ix, ix, 1);
	}
	/*
	 * Since this is the "master", we need to set up the
	 * other three controller chips' pointers to this structure
	 * for later use in the muxsel routine.
	 */
	if ((btv->c.nr<1) || (btv->c.nr>BTTV_MAX-3))
	    return;
	master[btv->c.nr-1] = btv;
	master[btv->c.nr]   = btv;
	master[btv->c.nr+1] = btv;
	master[btv->c.nr+2] = btv;
}

/* The Grandtec X-Guard framegrabber card uses two Dual 4-channel
 * video multiplexers to provide up to 16 video inputs. These
 * multiplexers are controlled by the lower 8 GPIO pins of the
 * bt878. The multiplexers probably Pericom PI5V331Q or similar.

 * xxx0 is pin xxx of multiplexer U5,
 * yyy1 is pin yyy of multiplexer U2
 */
#define ENA0    0x01
#define ENB0    0x02
#define ENA1    0x04
#define ENB1    0x08

#define IN10    0x10
#define IN00    0x20
#define IN11    0x40
#define IN01    0x80

static void xguard_muxsel(struct bttv *btv, unsigned int input)
{
	static const int masks[] = {
		ENB0, ENB0|IN00, ENB0|IN10, ENB0|IN00|IN10,
		ENA0, ENA0|IN00, ENA0|IN10, ENA0|IN00|IN10,
		ENB1, ENB1|IN01, ENB1|IN11, ENB1|IN01|IN11,
		ENA1, ENA1|IN01, ENA1|IN11, ENA1|IN01|IN11,
	};
	gpio_write(masks[input%16]);
}
static void picolo_tetra_init(struct bttv *btv)
{
	/*This is the video input redirection fonctionality : I DID NOT USED IT. */
	btwrite (0x08<<16,BT848_GPIO_DATA);/*GPIO[19] [==> 4053 B+C] set to 1 */
	btwrite (0x04<<16,BT848_GPIO_DATA);/*GPIO[18] [==> 4053 A]  set to 1*/
}
static void picolo_tetra_muxsel (struct bttv* btv, unsigned int input)
{

	dprintk("%d : picolo_tetra_muxsel =>  input = %d\n", btv->c.nr, input);
	/*Just set the right path in the analog multiplexers : channel 1 -> 4 ==> Analog Mux ==> MUX0*/
	/*GPIO[20]&GPIO[21] used to choose the right input*/
	btwrite (input<<20,BT848_GPIO_DATA);

}

/*
 * ivc120_muxsel [Added by Alan Garfield <alan@fromorbit.com>]
 *
 * The IVC120G security card has 4 i2c controlled TDA8540 matrix
 * swichers to provide 16 channels to MUX0. The TDA8540's have
 * 4 independent outputs and as such the IVC120G also has the
 * optional "Monitor Out" bus. This allows the card to be looking
 * at one input while the monitor is looking at another.
 *
 * Since I've couldn't be bothered figuring out how to add an
 * independent muxsel for the monitor bus, I've just set it to
 * whatever the card is looking at.
 *
 *  OUT0 of the TDA8540's is connected to MUX0         (0x03)
 *  OUT1 of the TDA8540's is connected to "Monitor Out"        (0x0C)
 *
 *  TDA8540_ALT3 IN0-3 = Channel 13 - 16       (0x03)
 *  TDA8540_ALT4 IN0-3 = Channel 1 - 4         (0x03)
 *  TDA8540_ALT5 IN0-3 = Channel 5 - 8         (0x03)
 *  TDA8540_ALT6 IN0-3 = Channel 9 - 12                (0x03)
 *
 */

/* All 7 possible sub-ids for the TDA8540 Matrix Switcher */
#define I2C_TDA8540        0x90
#define I2C_TDA8540_ALT1   0x92
#define I2C_TDA8540_ALT2   0x94
#define I2C_TDA8540_ALT3   0x96
#define I2C_TDA8540_ALT4   0x98
#define I2C_TDA8540_ALT5   0x9a
#define I2C_TDA8540_ALT6   0x9c

static void ivc120_muxsel(struct bttv *btv, unsigned int input)
{
	/* Simple maths */
	int key = input % 4;
	int matrix = input / 4;

	dprintk("%d: ivc120_muxsel: Input - %02d | TDA - %02d | In - %02d\n",
		btv->c.nr, input, matrix, key);

	/* Handles the input selection on the TDA8540's */
	bttv_I2CWrite(btv, I2C_TDA8540_ALT3, 0x00,
		      ((matrix == 3) ? (key | key << 2) : 0x00), 1);
	bttv_I2CWrite(btv, I2C_TDA8540_ALT4, 0x00,
		      ((matrix == 0) ? (key | key << 2) : 0x00), 1);
	bttv_I2CWrite(btv, I2C_TDA8540_ALT5, 0x00,
		      ((matrix == 1) ? (key | key << 2) : 0x00), 1);
	bttv_I2CWrite(btv, I2C_TDA8540_ALT6, 0x00,
		      ((matrix == 2) ? (key | key << 2) : 0x00), 1);

	/* Handles the output enables on the TDA8540's */
	bttv_I2CWrite(btv, I2C_TDA8540_ALT3, 0x02,
		      ((matrix == 3) ? 0x03 : 0x00), 1);  /* 13 - 16 */
	bttv_I2CWrite(btv, I2C_TDA8540_ALT4, 0x02,
		      ((matrix == 0) ? 0x03 : 0x00), 1);  /* 1-4 */
	bttv_I2CWrite(btv, I2C_TDA8540_ALT5, 0x02,
		      ((matrix == 1) ? 0x03 : 0x00), 1);  /* 5-8 */
	bttv_I2CWrite(btv, I2C_TDA8540_ALT6, 0x02,
		      ((matrix == 2) ? 0x03 : 0x00), 1);  /* 9-12 */

	/* 878's MUX0 is already selected for input via muxsel values */
}


/* PXC200 muxsel helper
 * luke@syseng.anu.edu.au
 * another transplant
 * from Alessandro Rubini (rubini@linux.it)
 *
 * There are 4 kinds of cards:
 * PXC200L which is bt848
 * PXC200F which is bt848 with PIC controlling mux
 * PXC200AL which is bt878
 * PXC200AF which is bt878 with PIC controlling mux
 */
#define PX_CFG_PXC200F 0x01
#define PX_FLAG_PXC200A  0x00001000 /* a pxc200A is bt-878 based */
#define PX_I2C_PIC       0x0f
#define PX_PXC200A_CARDID 0x200a1295
#define PX_I2C_CMD_CFG   0x00

static void PXC200_muxsel(struct bttv *btv, unsigned int input)
{
	int rc;
	long mux;
	int bitmask;
	unsigned char buf[2];

	/* Read PIC config to determine if this is a PXC200F */
	/* PX_I2C_CMD_CFG*/
	buf[0]=0;
	buf[1]=0;
	rc=bttv_I2CWrite(btv,(PX_I2C_PIC<<1),buf[0],buf[1],1);
	if (rc) {
		pr_debug("%d: PXC200_muxsel: pic cfg write failed:%d\n",
			 btv->c.nr, rc);
	  /* not PXC ? do nothing */
		return;
	}

	rc=bttv_I2CRead(btv,(PX_I2C_PIC<<1),NULL);
	if (!(rc & PX_CFG_PXC200F)) {
		pr_debug("%d: PXC200_muxsel: not PXC200F rc:%d\n",
			 btv->c.nr, rc);
		return;
	}


	/* The multiplexer in the 200F is handled by the GPIO port */
	/* get correct mapping between inputs  */
	/*  mux = bttv_tvcards[btv->type].muxsel[input] & 3; */
	/* ** not needed!?   */
	mux = input;

	/* make sure output pins are enabled */
	/* bitmask=0x30f; */
	bitmask=0x302;
	/* check whether we have a PXC200A */
	if (btv->cardid == PX_PXC200A_CARDID)  {
	   bitmask ^= 0x180; /* use 7 and 9, not 8 and 9 */
	   bitmask |= 7<<4; /* the DAC */
	}
	btwrite(bitmask, BT848_GPIO_OUT_EN);

	bitmask = btread(BT848_GPIO_DATA);
	if (btv->cardid == PX_PXC200A_CARDID)
	  bitmask = (bitmask & ~0x280) | ((mux & 2) << 8) | ((mux & 1) << 7);
	else /* older device */
	  bitmask = (bitmask & ~0x300) | ((mux & 3) << 8);
	btwrite(bitmask,BT848_GPIO_DATA);

	/*
	 * Was "to be safe, set the bt848 to input 0"
	 * Actually, since it's ok at load time, better not messing
	 * with these bits (on PXC200AF you need to set mux 2 here)
	 *
	 * needed because bttv-driver sets mux before calling this function
	 */
	if (btv->cardid == PX_PXC200A_CARDID)
	  btaor(2<<5, ~BT848_IFORM_MUXSEL, BT848_IFORM);
	else /* older device */
	  btand(~BT848_IFORM_MUXSEL,BT848_IFORM);

	pr_debug("%d: setting input channel to:%d\n", btv->c.nr, (int)mux);
}

static void phytec_muxsel(struct bttv *btv, unsigned int input)
{
	unsigned int mux = input % 4;

	if (input == btv->svhs)
		mux = 0;

	gpio_bits(0x3, mux);
}

/*
 * GeoVision GV-800(S) functions
 * Bruno Christo <bchristo@inf.ufsm.br>
*/

/* This is a function to control the analog switch, which determines which
 * camera is routed to which controller.  The switch comprises an X-address
 * (gpio bits 0-3, representing the camera, ranging from 0-15), and a
 * Y-address (gpio bits 4-6, representing the controller, ranging from 0-3).
 * A data value (gpio bit 18) of '1' enables the switch, and '0' disables
 * the switch.  A STROBE bit (gpio bit 17) latches the data value into the
 * specified address. There is also a chip select (gpio bit 16).
 * The idea is to set the address and chip select together, bring
 * STROBE high, write the data, and finally bring STROBE back to low.
 */
static void gv800s_write(struct bttv *btv,
			 unsigned char xaddr,
			 unsigned char yaddr,
			 unsigned char data) {
	/* On the "master" 878A:
	* GPIO bits 0-9 are used for the analog switch:
	*   00 - 03:	camera selector
	*   04 - 06:	878A (controller) selector
	*   16: 	cselect
	*   17:		strobe
	*   18: 	data (1->on, 0->off)
	*   19:		reset
	*/
	const u32 ADDRESS = ((xaddr&0xf) | (yaddr&3)<<4);
	const u32 CSELECT = 1<<16;
	const u32 STROBE = 1<<17;
	const u32 DATA = data<<18;

	gpio_bits(0x1007f, ADDRESS | CSELECT);	/* write ADDRESS and CSELECT */
	gpio_bits(0x20000, STROBE);		/* STROBE high */
	gpio_bits(0x40000, DATA);		/* write DATA */
	gpio_bits(0x20000, ~STROBE);		/* STROBE low */
}

/*
 * GeoVision GV-800(S) muxsel
 *
 * Each of the 4 cards (controllers) use this function.
 * The controller using this function selects the input through the GPIO pins
 * of the "master" card. A pointer to this card is stored in master[btv->c.nr].
 *
 * The parameter 'input' is the requested camera number (0-4) on the controller.
 * The map array has the address of each input. Note that the addresses in the
 * array are in the sequence the original GeoVision driver uses, that is, set
 * every controller to input 0, then to input 1, 2, 3, repeat. This means that
 * the physical "camera 1" connector corresponds to controller 0 input 0,
 * "camera 2" corresponds to controller 1 input 0, and so on.
 *
 * After getting the input address, the function then writes the appropriate
 * data to the analog switch, and housekeeps the local copy of the switch
 * information.
 */
static void gv800s_muxsel(struct bttv *btv, unsigned int input)
{
	struct bttv *mctlr;
	int xaddr, yaddr;
	static unsigned int map[4][4] = { { 0x0, 0x4, 0xa, 0x6 },
					  { 0x1, 0x5, 0xb, 0x7 },
					  { 0x2, 0x8, 0xc, 0xe },
					  { 0x3, 0x9, 0xd, 0xf } };
	input = input%4;
	mctlr = master[btv->c.nr];
	if (mctlr == NULL) {
		/* do nothing until the "master" is detected */
		return;
	}
	yaddr = (btv->c.nr - mctlr->c.nr) & 3;
	xaddr = map[yaddr][input] & 0xf;

	/* Check if the controller/camera pair has changed, ignore otherwise */
	if (mctlr->sw_status[yaddr] != xaddr) {
		/* disable the old switch, enable the new one and save status */
		gv800s_write(mctlr, mctlr->sw_status[yaddr], yaddr, 0);
		mctlr->sw_status[yaddr] = xaddr;
		gv800s_write(mctlr, xaddr, yaddr, 1);
	}
}

/* GeoVision GV-800(S) "master" chip init */
static void gv800s_init(struct bttv *btv)
{
	int ix;

	gpio_inout(0xf107f, 0xf107f);
	gpio_write(1<<19); /* reset the analog MUX */
	gpio_write(0);

	/* Preset camera 0 to the 4 controllers */
	for (ix = 0; ix < 4; ix++) {
		btv->sw_status[ix] = ix;
		gv800s_write(btv, ix, ix, 1);
	}

	/* Inputs on the "master" controller need this brightness fix */
	bttv_I2CWrite(btv, 0x18, 0x5, 0x90, 1);

	if (btv->c.nr > BTTV_MAX-4)
		return;
	/*
	 * Store the "master" controller pointer in the master
	 * array for later use in the muxsel function.
	 */
	master[btv->c.nr]   = btv;
	master[btv->c.nr+1] = btv;
	master[btv->c.nr+2] = btv;
	master[btv->c.nr+3] = btv;
}

/* ----------------------------------------------------------------------- */
/* motherboard chipset specific stuff                                      */

void __init bttv_check_chipset(void)
{
	int pcipci_fail = 0;
	struct pci_dev *dev = NULL;

	if (pci_pci_problems & (PCIPCI_FAIL|PCIAGP_FAIL)) 	/* should check if target is AGP */
		pcipci_fail = 1;
	if (pci_pci_problems & (PCIPCI_TRITON|PCIPCI_NATOMA|PCIPCI_VIAETBF))
		triton1 = 1;
	if (pci_pci_problems & PCIPCI_VSFX)
		vsfx = 1;
#ifdef PCIPCI_ALIMAGIK
	if (pci_pci_problems & PCIPCI_ALIMAGIK)
		latency = 0x0A;
#endif


	/* print warnings about any quirks found */
	if (triton1)
		pr_info("Host bridge needs ETBF enabled\n");
	if (vsfx)
		pr_info("Host bridge needs VSFX enabled\n");
	if (pcipci_fail) {
		pr_info("bttv and your chipset may not work together\n");
		if (!no_overlay) {
			pr_info("overlay will be disabled\n");
			no_overlay = 1;
		} else {
			pr_info("overlay forced. Use this option at your own risk.\n");
		}
	}
	if (UNSET != latency)
		pr_info("pci latency fixup [%d]\n", latency);
	while ((dev = pci_get_device(PCI_VENDOR_ID_INTEL,
				      PCI_DEVICE_ID_INTEL_82441, dev))) {
		unsigned char b;
		pci_read_config_byte(dev, 0x53, &b);
		if (bttv_debug)
			pr_info("Host bridge: 82441FX Natoma, bufcon=0x%02x\n",
				b);
	}
}

int bttv_handle_chipset(struct bttv *btv)
{
	unsigned char command;

	if (!triton1 && !vsfx && UNSET == latency)
		return 0;

	if (bttv_verbose) {
		if (triton1)
			pr_info("%d: enabling ETBF (430FX/VP3 compatibility)\n",
				btv->c.nr);
		if (vsfx && btv->id >= 878)
			pr_info("%d: enabling VSFX\n", btv->c.nr);
		if (UNSET != latency)
			pr_info("%d: setting pci timer to %d\n",
				btv->c.nr, latency);
	}

	if (btv->id < 878) {
		/* bt848 (mis)uses a bit in the irq mask for etbf */
		if (triton1)
			btv->triton1 = BT848_INT_ETBF;
	} else {
		/* bt878 has a bit in the pci config space for it */
		pci_read_config_byte(btv->c.pci, BT878_DEVCTRL, &command);
		if (triton1)
			command |= BT878_EN_TBFX;
		if (vsfx)
			command |= BT878_EN_VSFX;
		pci_write_config_byte(btv->c.pci, BT878_DEVCTRL, command);
	}
	if (UNSET != latency)
		pci_write_config_byte(btv->c.pci, PCI_LATENCY_TIMER, latency);
	return 0;
}

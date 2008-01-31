/*
 *
 * device driver for Conexant 2388x based TV cards
 * card-specific stuff.
 *
 * (c) 2003 Gerd Knorr <kraxel@bytesex.org> [SuSE Labs]
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>

#include "cx88.h"
#include "tea5767.h"

static unsigned int tuner[] = {[0 ... (CX88_MAXBOARDS - 1)] = UNSET };
static unsigned int radio[] = {[0 ... (CX88_MAXBOARDS - 1)] = UNSET };
static unsigned int card[]  = {[0 ... (CX88_MAXBOARDS - 1)] = UNSET };

module_param_array(tuner, int, NULL, 0444);
module_param_array(radio, int, NULL, 0444);
module_param_array(card,  int, NULL, 0444);

MODULE_PARM_DESC(tuner,"tuner type");
MODULE_PARM_DESC(radio,"radio tuner type");
MODULE_PARM_DESC(card,"card type");

static unsigned int latency = UNSET;
module_param(latency,int,0444);
MODULE_PARM_DESC(latency,"pci latency timer");

/* ------------------------------------------------------------------ */
/* board config info                                                  */

static const struct cx88_board cx88_boards[] = {
	[CX88_BOARD_UNKNOWN] = {
		.name		= "UNKNOWN/GENERIC",
		.tuner_type     = UNSET,
		.radio_type     = UNSET,
		.tuner_addr	= ADDR_UNSET,
		.radio_addr	= ADDR_UNSET,
		.input          = {{
			.type   = CX88_VMUX_COMPOSITE1,
			.vmux   = 0,
		},{
			.type   = CX88_VMUX_COMPOSITE2,
			.vmux   = 1,
		},{
			.type   = CX88_VMUX_COMPOSITE3,
			.vmux   = 2,
		},{
			.type   = CX88_VMUX_COMPOSITE4,
			.vmux   = 3,
		}},
	},
	[CX88_BOARD_HAUPPAUGE] = {
		.name		= "Hauppauge WinTV 34xxx models",
		.tuner_type     = UNSET,
		.radio_type     = UNSET,
		.tuner_addr	= ADDR_UNSET,
		.radio_addr	= ADDR_UNSET,
		.tda9887_conf   = TDA9887_PRESENT,
		.input          = {{
			.type   = CX88_VMUX_TELEVISION,
			.vmux   = 0,
			.gpio0  = 0xff00,  // internal decoder
		},{
			.type   = CX88_VMUX_DEBUG,
			.vmux   = 0,
			.gpio0  = 0xff01,  // mono from tuner chip
		},{
			.type   = CX88_VMUX_COMPOSITE1,
			.vmux   = 1,
			.gpio0  = 0xff02,
		},{
			.type   = CX88_VMUX_SVIDEO,
			.vmux   = 2,
			.gpio0  = 0xff02,
		}},
		.radio = {
			.type   = CX88_RADIO,
			.gpio0  = 0xff01,
		},
	},
	[CX88_BOARD_GDI] = {
		.name		= "GDI Black Gold",
		.tuner_type     = UNSET,
		.radio_type     = UNSET,
		.tuner_addr	= ADDR_UNSET,
		.radio_addr	= ADDR_UNSET,
		.input          = {{
			.type   = CX88_VMUX_TELEVISION,
			.vmux   = 0,
		},{
			.type   = CX88_VMUX_SVIDEO,
			.vmux   = 2,
		}},
	},
	[CX88_BOARD_PIXELVIEW] = {
		.name           = "PixelView",
		.tuner_type     = TUNER_PHILIPS_PAL,
		.radio_type     = UNSET,
		.tuner_addr	= ADDR_UNSET,
		.radio_addr	= ADDR_UNSET,
		.input          = {{
			.type   = CX88_VMUX_TELEVISION,
			.vmux   = 0,
			.gpio0  = 0xff00,  // internal decoder
		},{
			.type   = CX88_VMUX_COMPOSITE1,
			.vmux   = 1,
		},{
			.type   = CX88_VMUX_SVIDEO,
			.vmux   = 2,
		}},
		.radio = {
			 .type  = CX88_RADIO,
			 .gpio0 = 0xff10,
		},
	},
	[CX88_BOARD_ATI_WONDER_PRO] = {
		.name           = "ATI TV Wonder Pro",
		.tuner_type     = TUNER_PHILIPS_4IN1,
		.radio_type     = UNSET,
		.tuner_addr	= ADDR_UNSET,
		.radio_addr	= ADDR_UNSET,
		.tda9887_conf   = TDA9887_PRESENT | TDA9887_INTERCARRIER,
		.input          = {{
			.type   = CX88_VMUX_TELEVISION,
			.vmux   = 0,
			.gpio0  = 0x03ff,
		},{
			.type   = CX88_VMUX_COMPOSITE1,
			.vmux   = 1,
			.gpio0  = 0x03fe,
		},{
			.type   = CX88_VMUX_SVIDEO,
			.vmux   = 2,
			.gpio0  = 0x03fe,
		}},
	},
	[CX88_BOARD_WINFAST2000XP_EXPERT] = {
		.name           = "Leadtek Winfast 2000XP Expert",
		.tuner_type     = TUNER_PHILIPS_4IN1,
		.radio_type     = UNSET,
		.tuner_addr	= ADDR_UNSET,
		.radio_addr	= ADDR_UNSET,
		.tda9887_conf   = TDA9887_PRESENT,
		.input          = {{
			.type   = CX88_VMUX_TELEVISION,
			.vmux   = 0,
			.gpio0	= 0x00F5e700,
			.gpio1  = 0x00003004,
			.gpio2  = 0x00F5e700,
			.gpio3  = 0x02000000,
		},{
			.type   = CX88_VMUX_COMPOSITE1,
			.vmux   = 1,
			.gpio0	= 0x00F5c700,
			.gpio1  = 0x00003004,
			.gpio2  = 0x00F5c700,
			.gpio3  = 0x02000000,
		},{
			.type   = CX88_VMUX_SVIDEO,
			.vmux   = 2,
			.gpio0	= 0x00F5c700,
			.gpio1  = 0x00003004,
			.gpio2  = 0x00F5c700,
			.gpio3  = 0x02000000,
		}},
		.radio = {
			.type   = CX88_RADIO,
			.gpio0	= 0x00F5d700,
			.gpio1  = 0x00003004,
			.gpio2  = 0x00F5d700,
			.gpio3  = 0x02000000,
		},
	},
	[CX88_BOARD_AVERTV_STUDIO_303] = {
		.name           = "AverTV Studio 303 (M126)",
		.tuner_type     = TUNER_PHILIPS_FM1216ME_MK3,
		.radio_type     = UNSET,
		.tuner_addr	= ADDR_UNSET,
		.radio_addr	= ADDR_UNSET,
		.tda9887_conf   = TDA9887_PRESENT,
		.input          = {{
			.type   = CX88_VMUX_TELEVISION,
			.vmux   = 0,
			.gpio1  = 0xe09f,
		},{
			.type   = CX88_VMUX_COMPOSITE1,
			.vmux   = 1,
			.gpio1  = 0xe05f,
		},{
			.type   = CX88_VMUX_SVIDEO,
			.vmux   = 2,
			.gpio1  = 0xe05f,
		}},
		.radio = {
			.gpio1  = 0xe0df,
			.type   = CX88_RADIO,
		},
	},
	[CX88_BOARD_MSI_TVANYWHERE_MASTER] = {
		// added gpio values thanks to Michal
		// values for PAL from DScaler
		.name           = "MSI TV-@nywhere Master",
		.tuner_type     = TUNER_MT2032,
		.radio_type     = UNSET,
		.tuner_addr	= ADDR_UNSET,
		.radio_addr	= ADDR_UNSET,
		.tda9887_conf	= TDA9887_PRESENT | TDA9887_INTERCARRIER_NTSC,
		.input          = {{
			.type   = CX88_VMUX_TELEVISION,
			.vmux   = 0,
			.gpio0  = 0x000040bf,
			.gpio1  = 0x000080c0,
			.gpio2  = 0x0000ff40,
		},{
			.type   = CX88_VMUX_COMPOSITE1,
			.vmux   = 1,
			.gpio0  = 0x000040bf,
			.gpio1  = 0x000080c0,
			.gpio2  = 0x0000ff40,
		},{
			.type   = CX88_VMUX_SVIDEO,
			.vmux   = 2,
			.gpio0  = 0x000040bf,
			.gpio1  = 0x000080c0,
			.gpio2  = 0x0000ff40,
		}},
		.radio = {
			 .type   = CX88_RADIO,
			 .vmux   = 3,
			 .gpio0  = 0x000040bf,
			 .gpio1  = 0x000080c0,
			 .gpio2  = 0x0000ff20,
		},
	},
	[CX88_BOARD_WINFAST_DV2000] = {
		.name           = "Leadtek Winfast DV2000",
		.tuner_type     = TUNER_PHILIPS_FM1216ME_MK3,
		.radio_type     = UNSET,
		.tuner_addr	= ADDR_UNSET,
		.radio_addr	= ADDR_UNSET,
		.tda9887_conf   = TDA9887_PRESENT,
		.input          = {{
			.type   = CX88_VMUX_TELEVISION,
			.vmux   = 0,
			.gpio0  = 0x0035e700,
			.gpio1  = 0x00003004,
			.gpio2  = 0x0035e700,
			.gpio3  = 0x02000000,
		},{

			.type   = CX88_VMUX_COMPOSITE1,
			.vmux   = 1,
			.gpio0  = 0x0035c700,
			.gpio1  = 0x00003004,
			.gpio2  = 0x0035c700,
			.gpio3  = 0x02000000,
		},{
			.type   = CX88_VMUX_SVIDEO,
			.vmux   = 2,
			.gpio0  = 0x0035c700,
			.gpio1  = 0x0035c700,
			.gpio2  = 0x02000000,
			.gpio3  = 0x02000000,
		}},
		.radio = {
			.type   = CX88_RADIO,
			.gpio0  = 0x0035d700,
			.gpio1  = 0x00007004,
			.gpio2  = 0x0035d700,
			.gpio3  = 0x02000000,
		},
	},
	[CX88_BOARD_LEADTEK_PVR2000] = {
		// gpio values for PAL version from regspy by DScaler
		.name           = "Leadtek PVR 2000",
		.tuner_type     = TUNER_PHILIPS_FM1216ME_MK3,
		.radio_type     = UNSET,
		.tuner_addr	= ADDR_UNSET,
		.radio_addr	= ADDR_UNSET,
		.tda9887_conf   = TDA9887_PRESENT,
		.input          = {{
			.type   = CX88_VMUX_TELEVISION,
			.vmux   = 0,
			.gpio0  = 0x0000bde2,
			.audioroute = 1,
		},{
			.type   = CX88_VMUX_COMPOSITE1,
			.vmux   = 1,
			.gpio0  = 0x0000bde6,
			.audioroute = 1,
		},{
			.type   = CX88_VMUX_SVIDEO,
			.vmux   = 2,
			.gpio0  = 0x0000bde6,
			.audioroute = 1,
		}},
		.radio = {
			.type   = CX88_RADIO,
			.gpio0  = 0x0000bd62,
			.audioroute = 1,
		},
		.mpeg           = CX88_MPEG_BLACKBIRD,
	},
	[CX88_BOARD_IODATA_GVVCP3PCI] = {
		.name		= "IODATA GV-VCP3/PCI",
		.tuner_type     = TUNER_ABSENT,
		.radio_type     = UNSET,
		.tuner_addr	= ADDR_UNSET,
		.radio_addr	= ADDR_UNSET,
		.input          = {{
			.type   = CX88_VMUX_COMPOSITE1,
			.vmux   = 0,
		},{
			.type   = CX88_VMUX_COMPOSITE2,
			.vmux   = 1,
		},{
			.type   = CX88_VMUX_SVIDEO,
			.vmux   = 2,
		}},
	},
	[CX88_BOARD_PROLINK_PLAYTVPVR] = {
		.name           = "Prolink PlayTV PVR",
		.tuner_type     = TUNER_PHILIPS_FM1236_MK3,
		.radio_type     = UNSET,
		.tuner_addr	= ADDR_UNSET,
		.radio_addr	= ADDR_UNSET,
		.tda9887_conf	= TDA9887_PRESENT,
		.input          = {{
			.type   = CX88_VMUX_TELEVISION,
			.vmux   = 0,
			.gpio0  = 0xbff0,
		},{
			.type   = CX88_VMUX_COMPOSITE1,
			.vmux   = 1,
			.gpio0  = 0xbff3,
		},{
			.type   = CX88_VMUX_SVIDEO,
			.vmux   = 2,
			.gpio0  = 0xbff3,
		}},
		.radio = {
			.type   = CX88_RADIO,
			.gpio0  = 0xbff0,
		},
	},
	[CX88_BOARD_ASUS_PVR_416] = {
		.name		= "ASUS PVR-416",
		.tuner_type     = TUNER_PHILIPS_FM1236_MK3,
		.radio_type     = UNSET,
		.tuner_addr	= ADDR_UNSET,
		.radio_addr	= ADDR_UNSET,
		.tda9887_conf   = TDA9887_PRESENT,
		.input          = {{
			.type   = CX88_VMUX_TELEVISION,
			.vmux   = 0,
			.gpio0  = 0x0000fde6,
		},{
			.type   = CX88_VMUX_SVIDEO,
			.vmux   = 2,
			.gpio0  = 0x0000fde6, // 0x0000fda6 L,R RCA audio in?
			.audioroute = 1,
		}},
		.radio = {
			.type   = CX88_RADIO,
			.gpio0  = 0x0000fde2,
		},
		.mpeg           = CX88_MPEG_BLACKBIRD,
	},
	[CX88_BOARD_MSI_TVANYWHERE] = {
		.name           = "MSI TV-@nywhere",
		.tuner_type     = TUNER_MT2032,
		.radio_type     = UNSET,
		.tuner_addr	= ADDR_UNSET,
		.radio_addr	= ADDR_UNSET,
		.tda9887_conf   = TDA9887_PRESENT,
		.input          = {{
			.type   = CX88_VMUX_TELEVISION,
			.vmux   = 0,
			.gpio0  = 0x00000fbf,
			.gpio2  = 0x0000fc08,
		},{
			.type   = CX88_VMUX_COMPOSITE1,
			.vmux   = 1,
			.gpio0  = 0x00000fbf,
			.gpio2  = 0x0000fc68,
		},{
			.type   = CX88_VMUX_SVIDEO,
			.vmux   = 2,
			.gpio0  = 0x00000fbf,
			.gpio2  = 0x0000fc68,
		}},
	},
	[CX88_BOARD_KWORLD_DVB_T] = {
		.name           = "KWorld/VStream XPert DVB-T",
		.tuner_type     = TUNER_ABSENT,
		.radio_type     = UNSET,
		.tuner_addr	= ADDR_UNSET,
		.radio_addr	= ADDR_UNSET,
		.input          = {{
			.type   = CX88_VMUX_COMPOSITE1,
			.vmux   = 1,
			.gpio0  = 0x0700,
			.gpio2  = 0x0101,
		},{
			.type   = CX88_VMUX_SVIDEO,
			.vmux   = 2,
			.gpio0  = 0x0700,
			.gpio2  = 0x0101,
		}},
		.mpeg           = CX88_MPEG_DVB,
	},
	[CX88_BOARD_DVICO_FUSIONHDTV_DVB_T1] = {
		.name           = "DViCO FusionHDTV DVB-T1",
		.tuner_type     = TUNER_ABSENT, /* No analog tuner */
		.radio_type     = UNSET,
		.tuner_addr	= ADDR_UNSET,
		.radio_addr	= ADDR_UNSET,
		.input          = {{
			.type   = CX88_VMUX_COMPOSITE1,
			.vmux   = 1,
			.gpio0  = 0x000027df,
		},{
			.type   = CX88_VMUX_SVIDEO,
			.vmux   = 2,
			.gpio0  = 0x000027df,
		}},
		.mpeg           = CX88_MPEG_DVB,
	},
	[CX88_BOARD_KWORLD_LTV883] = {
		.name           = "KWorld LTV883RF",
		.tuner_type     = TUNER_TNF_8831BGFF,
		.radio_type     = UNSET,
		.tuner_addr	= ADDR_UNSET,
		.radio_addr	= ADDR_UNSET,
		.input          = {{
			.type   = CX88_VMUX_TELEVISION,
			.vmux   = 0,
			.gpio0  = 0x07f8,
		},{
			.type   = CX88_VMUX_DEBUG,
			.vmux   = 0,
			.gpio0  = 0x07f9,  // mono from tuner chip
		},{
			.type   = CX88_VMUX_COMPOSITE1,
			.vmux   = 1,
			.gpio0  = 0x000007fa,
		},{
			.type   = CX88_VMUX_SVIDEO,
			.vmux   = 2,
			.gpio0  = 0x000007fa,
		}},
		.radio = {
			.type   = CX88_RADIO,
			.gpio0  = 0x000007f8,
		},
	},
	[CX88_BOARD_DVICO_FUSIONHDTV_3_GOLD_Q] = {
		.name		= "DViCO FusionHDTV 3 Gold-Q",
		.tuner_type     = TUNER_MICROTUNE_4042FI5,
		.radio_type     = UNSET,
		.tuner_addr	= ADDR_UNSET,
		.radio_addr	= ADDR_UNSET,
		/*
		   GPIO[0] resets DT3302 DTV receiver
		    0 - reset asserted
		    1 - normal operation
		   GPIO[1] mutes analog audio output connector
		    0 - enable selected source
		    1 - mute
		   GPIO[2] selects source for analog audio output connector
		    0 - analog audio input connector on tab
		    1 - analog DAC output from CX23881 chip
		   GPIO[3] selects RF input connector on tuner module
		    0 - RF connector labeled CABLE
		    1 - RF connector labeled ANT
		   GPIO[4] selects high RF for QAM256 mode
		    0 - normal RF
		    1 - high RF
		*/
		.input          = {{
			.type   = CX88_VMUX_TELEVISION,
			.vmux   = 0,
			.gpio0	= 0x0f0d,
		},{
			.type   = CX88_VMUX_CABLE,
			.vmux   = 0,
			.gpio0	= 0x0f05,
		},{
			.type   = CX88_VMUX_COMPOSITE1,
			.vmux   = 1,
			.gpio0	= 0x0f00,
		},{
			.type   = CX88_VMUX_SVIDEO,
			.vmux   = 2,
			.gpio0	= 0x0f00,
		}},
		.mpeg           = CX88_MPEG_DVB,
	},
	[CX88_BOARD_HAUPPAUGE_DVB_T1] = {
		.name           = "Hauppauge Nova-T DVB-T",
		.tuner_type     = TUNER_ABSENT,
		.radio_type     = UNSET,
		.tuner_addr	= ADDR_UNSET,
		.radio_addr	= ADDR_UNSET,
		.input          = {{
			.type   = CX88_VMUX_DVB,
			.vmux   = 0,
		}},
		.mpeg           = CX88_MPEG_DVB,
	},
	[CX88_BOARD_CONEXANT_DVB_T1] = {
		.name           = "Conexant DVB-T reference design",
		.tuner_type     = TUNER_ABSENT,
		.radio_type     = UNSET,
		.tuner_addr	= ADDR_UNSET,
		.radio_addr	= ADDR_UNSET,
		.input          = {{
			.type   = CX88_VMUX_DVB,
			.vmux   = 0,
		}},
		.mpeg           = CX88_MPEG_DVB,
	},
	[CX88_BOARD_PROVIDEO_PV259] = {
		.name		= "Provideo PV259",
		.tuner_type     = TUNER_PHILIPS_FQ1216ME,
		.radio_type     = UNSET,
		.tuner_addr	= ADDR_UNSET,
		.radio_addr	= ADDR_UNSET,
		.input          = {{
			.type   = CX88_VMUX_TELEVISION,
			.vmux   = 0,
			.audioroute = 1,
		}},
		.mpeg           = CX88_MPEG_BLACKBIRD,
	},
	[CX88_BOARD_DVICO_FUSIONHDTV_DVB_T_PLUS] = {
		.name           = "DViCO FusionHDTV DVB-T Plus",
		.tuner_type     = TUNER_ABSENT, /* No analog tuner */
		.radio_type     = UNSET,
		.tuner_addr	= ADDR_UNSET,
		.radio_addr	= ADDR_UNSET,
		.input          = {{
			.type   = CX88_VMUX_COMPOSITE1,
			.vmux   = 1,
			.gpio0  = 0x000027df,
		},{
			.type   = CX88_VMUX_SVIDEO,
			.vmux   = 2,
			.gpio0  = 0x000027df,
		}},
		.mpeg           = CX88_MPEG_DVB,
	},
	[CX88_BOARD_DNTV_LIVE_DVB_T] = {
		.name		= "digitalnow DNTV Live! DVB-T",
		.tuner_type     = TUNER_ABSENT,
		.radio_type     = UNSET,
		.tuner_addr	= ADDR_UNSET,
		.radio_addr	= ADDR_UNSET,
		.input		= {{
			.type   = CX88_VMUX_COMPOSITE1,
			.vmux   = 1,
			.gpio0  = 0x00000700,
			.gpio2  = 0x00000101,
		},{
			.type   = CX88_VMUX_SVIDEO,
			.vmux   = 2,
			.gpio0  = 0x00000700,
			.gpio2  = 0x00000101,
		}},
		.mpeg           = CX88_MPEG_DVB,
	},
	[CX88_BOARD_PCHDTV_HD3000] = {
		.name           = "pcHDTV HD3000 HDTV",
		.tuner_type     = TUNER_THOMSON_DTT761X,
		.radio_type     = UNSET,
		.tuner_addr	= ADDR_UNSET,
		.radio_addr	= ADDR_UNSET,
		.tda9887_conf   = TDA9887_PRESENT,
		/* GPIO[2] = audio source for analog audio out connector
		 *  0 = analog audio input connector
		 *  1 = CX88 audio DACs
		 *
		 * GPIO[7] = input to CX88's audio/chroma ADC
		 *  0 = FM 10.7 MHz IF
		 *  1 = Sound 4.5 MHz IF
		 *
		 * GPIO[1,5,6] = Oren 51132 pins 27,35,28 respectively
		 *
		 * GPIO[16] = Remote control input
		 */
		.input          = {{
			.type   = CX88_VMUX_TELEVISION,
			.vmux   = 0,
			.gpio0  = 0x00008484,
		},{
			.type   = CX88_VMUX_COMPOSITE1,
			.vmux   = 1,
			.gpio0  = 0x00008400,
		},{
			.type   = CX88_VMUX_SVIDEO,
			.vmux   = 2,
			.gpio0  = 0x00008400,
		}},
		.radio = {
			.type   = CX88_RADIO,
			.gpio0  = 0x00008404,
		},
		.mpeg           = CX88_MPEG_DVB,
	},
	[CX88_BOARD_HAUPPAUGE_ROSLYN] = {
		// entry added by Kaustubh D. Bhalerao <bhalerao.1@osu.edu>
		// GPIO values obtained from regspy, courtesy Sean Covel
		.name           = "Hauppauge WinTV 28xxx (Roslyn) models",
		.tuner_type     = UNSET,
		.radio_type     = UNSET,
		.tuner_addr	= ADDR_UNSET,
		.radio_addr	= ADDR_UNSET,
		.input          = {{
			.type   = CX88_VMUX_TELEVISION,
			.vmux   = 0,
			.gpio0  = 0xed1a,
			.gpio2  = 0x00ff,
		},{
			.type   = CX88_VMUX_DEBUG,
			.vmux   = 0,
			.gpio0  = 0xff01,
		},{
			.type   = CX88_VMUX_COMPOSITE1,
			.vmux   = 1,
			.gpio0  = 0xff02,
		},{
			.type   = CX88_VMUX_SVIDEO,
			.vmux   = 2,
			.gpio0  = 0xed92,
			.gpio2  = 0x00ff,
		}},
		.radio = {
			 .type   = CX88_RADIO,
			 .gpio0  = 0xed96,
			 .gpio2  = 0x00ff,
		 },
		.mpeg           = CX88_MPEG_BLACKBIRD,
	},
	[CX88_BOARD_DIGITALLOGIC_MEC] = {
		.name           = "Digital-Logic MICROSPACE Entertainment Center (MEC)",
		.tuner_type     = TUNER_PHILIPS_FM1216ME_MK3,
		.radio_type     = UNSET,
		.tuner_addr	= ADDR_UNSET,
		.radio_addr	= ADDR_UNSET,
		.tda9887_conf   = TDA9887_PRESENT,
		.input          = {{
			.type   = CX88_VMUX_TELEVISION,
			.vmux   = 0,
			.gpio0  = 0x00009d80,
			.audioroute = 1,
		},{
			.type   = CX88_VMUX_COMPOSITE1,
			.vmux   = 1,
			.gpio0  = 0x00009d76,
			.audioroute = 1,
		},{
			.type   = CX88_VMUX_SVIDEO,
			.vmux   = 2,
			.gpio0  = 0x00009d76,
			.audioroute = 1,
		}},
		.radio = {
			.type   = CX88_RADIO,
			.gpio0  = 0x00009d00,
			.audioroute = 1,
		},
		.mpeg           = CX88_MPEG_BLACKBIRD,
	},
	[CX88_BOARD_IODATA_GVBCTV7E] = {
		.name           = "IODATA GV/BCTV7E",
		.tuner_type     = TUNER_PHILIPS_FQ1286,
		.radio_type     = UNSET,
		.tuner_addr	= ADDR_UNSET,
		.radio_addr	= ADDR_UNSET,
		.tda9887_conf   = TDA9887_PRESENT,
		.input          = {{
			.type   = CX88_VMUX_TELEVISION,
			.vmux   = 1,
			.gpio1  = 0x0000e03f,
		},{
			.type   = CX88_VMUX_COMPOSITE1,
			.vmux   = 2,
			.gpio1  = 0x0000e07f,
		},{
			.type   = CX88_VMUX_SVIDEO,
			.vmux   = 3,
			.gpio1  = 0x0000e07f,
		}}
	},
	[CX88_BOARD_PIXELVIEW_PLAYTV_ULTRA_PRO] = {
		.name           = "PixelView PlayTV Ultra Pro (Stereo)",
		/* May be also TUNER_YMEC_TVF_5533MF for NTSC/M or PAL/M */
		.tuner_type     = TUNER_PHILIPS_FM1216ME_MK3,
		.radio_type     = UNSET,
		.tuner_addr	= ADDR_UNSET,
		.radio_addr	= ADDR_UNSET,
		.input          = {{
			.type   = CX88_VMUX_TELEVISION,
			.vmux   = 0,
			.gpio0  = 0xbf61,  /* internal decoder */
		},{
			.type   = CX88_VMUX_COMPOSITE1,
			.vmux   = 1,
			.gpio0	= 0xbf63,
		},{
			.type   = CX88_VMUX_SVIDEO,
			.vmux   = 2,
			.gpio0	= 0xbf63,
		}},
		.radio = {
			 .type  = CX88_RADIO,
			 .gpio0 = 0xbf60,
		 },
	},
	[CX88_BOARD_DVICO_FUSIONHDTV_3_GOLD_T] = {
		.name           = "DViCO FusionHDTV 3 Gold-T",
		.tuner_type     = TUNER_THOMSON_DTT761X,
		.radio_type     = UNSET,
		.tuner_addr	= ADDR_UNSET,
		.radio_addr	= ADDR_UNSET,
		.tda9887_conf   = TDA9887_PRESENT,
		.input          = {{
			.type   = CX88_VMUX_TELEVISION,
			.vmux   = 0,
			.gpio0  = 0x97ed,
		},{
			.type   = CX88_VMUX_COMPOSITE1,
			.vmux   = 1,
			.gpio0  = 0x97e9,
		},{
			.type   = CX88_VMUX_SVIDEO,
			.vmux   = 2,
			.gpio0  = 0x97e9,
		}},
		.mpeg           = CX88_MPEG_DVB,
	},
	[CX88_BOARD_ADSTECH_DVB_T_PCI] = {
		.name           = "ADS Tech Instant TV DVB-T PCI",
		.tuner_type     = TUNER_ABSENT,
		.radio_type     = UNSET,
		.tuner_addr	= ADDR_UNSET,
		.radio_addr	= ADDR_UNSET,
		.input          = {{
			.type   = CX88_VMUX_COMPOSITE1,
			.vmux   = 1,
			.gpio0  = 0x0700,
			.gpio2  = 0x0101,
		},{
			.type   = CX88_VMUX_SVIDEO,
			.vmux   = 2,
			.gpio0  = 0x0700,
			.gpio2  = 0x0101,
		}},
		.mpeg           = CX88_MPEG_DVB,
	},
	[CX88_BOARD_TERRATEC_CINERGY_1400_DVB_T1] = {
		.name           = "TerraTec Cinergy 1400 DVB-T",
		.tuner_type     = TUNER_ABSENT,
		.input          = {{
			.type   = CX88_VMUX_DVB,
			.vmux   = 0,
		},{
			.type   = CX88_VMUX_COMPOSITE1,
			.vmux   = 2,
		},{
			.type   = CX88_VMUX_SVIDEO,
			.vmux   = 2,
		}},
		.mpeg           = CX88_MPEG_DVB,
	},
	[CX88_BOARD_DVICO_FUSIONHDTV_5_GOLD] = {
		.name           = "DViCO FusionHDTV 5 Gold",
		.tuner_type     = TUNER_LG_TDVS_H06XF, /* TDVS-H062F */
		.radio_type     = UNSET,
		.tuner_addr	= ADDR_UNSET,
		.radio_addr	= ADDR_UNSET,
		.tda9887_conf   = TDA9887_PRESENT,
		.input          = {{
			.type   = CX88_VMUX_TELEVISION,
			.vmux   = 0,
			.gpio0  = 0x87fd,
		},{
			.type   = CX88_VMUX_COMPOSITE1,
			.vmux   = 1,
			.gpio0  = 0x87f9,
		},{
			.type   = CX88_VMUX_SVIDEO,
			.vmux   = 2,
			.gpio0  = 0x87f9,
		}},
		.mpeg           = CX88_MPEG_DVB,
	},
	[CX88_BOARD_AVERMEDIA_ULTRATV_MC_550] = {
		.name           = "AverMedia UltraTV Media Center PCI 550",
		.tuner_type     = TUNER_PHILIPS_FM1236_MK3,
		.radio_type     = UNSET,
		.tuner_addr     = ADDR_UNSET,
		.radio_addr     = ADDR_UNSET,
		.tda9887_conf   = TDA9887_PRESENT,
		.input          = {{
			.type   = CX88_VMUX_COMPOSITE1,
			.vmux   = 0,
			.gpio0  = 0x0000cd73,
			.audioroute = 1,
		},{
			.type   = CX88_VMUX_SVIDEO,
			.vmux   = 1,
			.gpio0  = 0x0000cd73,
			.audioroute = 1,
		},{
			.type   = CX88_VMUX_TELEVISION,
			.vmux   = 3,
			.gpio0  = 0x0000cdb3,
			.audioroute = 1,
		}},
		.radio = {
			.type   = CX88_RADIO,
			.vmux   = 2,
			.gpio0  = 0x0000cdf3,
			.audioroute = 1,
		},
		.mpeg           = CX88_MPEG_BLACKBIRD,
	},
	[CX88_BOARD_KWORLD_VSTREAM_EXPERT_DVD] = {
		 /* Alexander Wold <awold@bigfoot.com> */
		 .name           = "Kworld V-Stream Xpert DVD",
		 .tuner_type     = UNSET,
		 .input          = {{
			 .type   = CX88_VMUX_COMPOSITE1,
			 .vmux   = 1,
			 .gpio0  = 0x03000000,
			 .gpio1  = 0x01000000,
			 .gpio2  = 0x02000000,
			 .gpio3  = 0x00100000,
		 },{
			 .type   = CX88_VMUX_SVIDEO,
			 .vmux   = 2,
			 .gpio0  = 0x03000000,
			 .gpio1  = 0x01000000,
			 .gpio2  = 0x02000000,
			 .gpio3  = 0x00100000,
		 }},
	},
	[CX88_BOARD_ATI_HDTVWONDER] = {
		.name           = "ATI HDTV Wonder",
		.tuner_type     = TUNER_PHILIPS_TUV1236D,
		.radio_type     = UNSET,
		.tuner_addr	= ADDR_UNSET,
		.radio_addr	= ADDR_UNSET,
		.input          = {{
			.type   = CX88_VMUX_TELEVISION,
			.vmux   = 0,
			.gpio0  = 0x00000ff7,
			.gpio1  = 0x000000ff,
			.gpio2  = 0x00000001,
			.gpio3  = 0x00000000,
		},{
			.type   = CX88_VMUX_COMPOSITE1,
			.vmux   = 1,
			.gpio0  = 0x00000ffe,
			.gpio1  = 0x000000ff,
			.gpio2  = 0x00000001,
			.gpio3  = 0x00000000,
		},{
			.type   = CX88_VMUX_SVIDEO,
			.vmux   = 2,
			.gpio0  = 0x00000ffe,
			.gpio1  = 0x000000ff,
			.gpio2  = 0x00000001,
			.gpio3  = 0x00000000,
		}},
		.mpeg           = CX88_MPEG_DVB,
	},
	[CX88_BOARD_WINFAST_DTV1000] = {
		.name           = "WinFast DTV1000-T",
		.tuner_type     = TUNER_ABSENT,
		.radio_type     = UNSET,
		.tuner_addr	= ADDR_UNSET,
		.radio_addr	= ADDR_UNSET,
		.input          = {{
			.type   = CX88_VMUX_DVB,
			.vmux   = 0,
		},{
			.type   = CX88_VMUX_COMPOSITE1,
			.vmux   = 1,
		},{
			.type   = CX88_VMUX_SVIDEO,
			.vmux   = 2,
		}},
		.mpeg           = CX88_MPEG_DVB,
	},
	[CX88_BOARD_AVERTV_303] = {
		.name           = "AVerTV 303 (M126)",
		.tuner_type     = TUNER_PHILIPS_FM1216ME_MK3,
		.radio_type     = UNSET,
		.tuner_addr	= ADDR_UNSET,
		.radio_addr	= ADDR_UNSET,
		.tda9887_conf   = TDA9887_PRESENT,
		.input          = {{
			.type   = CX88_VMUX_TELEVISION,
			.vmux   = 0,
			.gpio0  = 0x00ff,
			.gpio1  = 0xe09f,
			.gpio2  = 0x0010,
			.gpio3  = 0x0000,
		},{
			.type   = CX88_VMUX_COMPOSITE1,
			.vmux   = 1,
			.gpio0  = 0x00ff,
			.gpio1  = 0xe05f,
			.gpio2  = 0x0010,
			.gpio3  = 0x0000,
		},{
			.type   = CX88_VMUX_SVIDEO,
			.vmux   = 2,
			.gpio0  = 0x00ff,
			.gpio1  = 0xe05f,
			.gpio2  = 0x0010,
			.gpio3  = 0x0000,
		}},
	},
	[CX88_BOARD_HAUPPAUGE_NOVASPLUS_S1] = {
		.name		= "Hauppauge Nova-S-Plus DVB-S",
		.tuner_type	= TUNER_ABSENT,
		.radio_type	= UNSET,
		.tuner_addr	= ADDR_UNSET,
		.radio_addr	= ADDR_UNSET,
		.input		= {{
			.type	= CX88_VMUX_DVB,
			.vmux	= 0,
		},{
			.type	= CX88_VMUX_COMPOSITE1,
			.vmux	= 1,
		},{
			.type	= CX88_VMUX_SVIDEO,
			.vmux	= 2,
		}},
		.mpeg           = CX88_MPEG_DVB,
	},
	[CX88_BOARD_HAUPPAUGE_NOVASE2_S1] = {
		.name		= "Hauppauge Nova-SE2 DVB-S",
		.tuner_type	= TUNER_ABSENT,
		.radio_type	= UNSET,
		.tuner_addr	= ADDR_UNSET,
		.radio_addr	= ADDR_UNSET,
		.input		= {{
			.type	= CX88_VMUX_DVB,
			.vmux	= 0,
		}},
		.mpeg           = CX88_MPEG_DVB,
	},
	[CX88_BOARD_KWORLD_DVBS_100] = {
		.name		= "KWorld DVB-S 100",
		.tuner_type	= TUNER_ABSENT,
		.radio_type	= UNSET,
		.tuner_addr	= ADDR_UNSET,
		.radio_addr	= ADDR_UNSET,
		.input		= {{
			.type	= CX88_VMUX_DVB,
			.vmux	= 0,
		},{
			.type	= CX88_VMUX_COMPOSITE1,
			.vmux	= 1,
		},{
			.type	= CX88_VMUX_SVIDEO,
			.vmux	= 2,
		}},
		.mpeg           = CX88_MPEG_DVB,
	},
	[CX88_BOARD_HAUPPAUGE_HVR1100] = {
		.name		= "Hauppauge WinTV-HVR1100 DVB-T/Hybrid",
		.tuner_type     = TUNER_PHILIPS_FMD1216ME_MK3,
		.radio_type	= UNSET,
		.tuner_addr	= ADDR_UNSET,
		.radio_addr	= ADDR_UNSET,
		.tda9887_conf   = TDA9887_PRESENT,
		.input		= {{
			.type   = CX88_VMUX_TELEVISION,
			.vmux   = 0,
		},{
			.type	= CX88_VMUX_COMPOSITE1,
			.vmux	= 1,
		},{
			.type	= CX88_VMUX_SVIDEO,
			.vmux	= 2,
		}},
		/* fixme: Add radio support */
		.mpeg           = CX88_MPEG_DVB,
	},
	[CX88_BOARD_HAUPPAUGE_HVR1100LP] = {
		.name		= "Hauppauge WinTV-HVR1100 DVB-T/Hybrid (Low Profile)",
		.tuner_type     = TUNER_PHILIPS_FMD1216ME_MK3,
		.radio_type	= UNSET,
		.tuner_addr	= ADDR_UNSET,
		.radio_addr	= ADDR_UNSET,
		.tda9887_conf   = TDA9887_PRESENT,
		.input		= {{
			.type   = CX88_VMUX_TELEVISION,
			.vmux   = 0,
		},{
			.type	= CX88_VMUX_COMPOSITE1,
			.vmux	= 1,
		}},
		/* fixme: Add radio support */
		.mpeg           = CX88_MPEG_DVB,
	},
	[CX88_BOARD_DNTV_LIVE_DVB_T_PRO] = {
		.name           = "digitalnow DNTV Live! DVB-T Pro",
		.tuner_type     = TUNER_PHILIPS_FMD1216ME_MK3,
		.radio_type     = UNSET,
		.tuner_addr	= ADDR_UNSET,
		.radio_addr	= ADDR_UNSET,
		.tda9887_conf   = TDA9887_PRESENT | TDA9887_PORT1_ACTIVE |
				  TDA9887_PORT2_ACTIVE,
		.input          = {{
			.type   = CX88_VMUX_TELEVISION,
			.vmux   = 0,
			.gpio0  = 0xf80808,
		},{
			.type   = CX88_VMUX_COMPOSITE1,
			.vmux   = 1,
			.gpio0	= 0xf80808,
		},{
			.type   = CX88_VMUX_SVIDEO,
			.vmux   = 2,
			.gpio0	= 0xf80808,
		}},
		.radio = {
			 .type  = CX88_RADIO,
			 .gpio0 = 0xf80808,
		},
		.mpeg           = CX88_MPEG_DVB,
	},
	[CX88_BOARD_KWORLD_DVB_T_CX22702] = {
		/* Kworld V-stream Xpert DVB-T with Thomson tuner */
		/* DTT 7579 Conexant CX22702-19 Conexant CX2388x  */
		/* Manenti Marco <marco_manenti@colman.it> */
		.name           = "KWorld/VStream XPert DVB-T with cx22702",
		.tuner_type     = TUNER_ABSENT,
		.radio_type     = UNSET,
		.tuner_addr	= ADDR_UNSET,
		.radio_addr	= ADDR_UNSET,
		.input          = {{
			.type   = CX88_VMUX_COMPOSITE1,
			.vmux   = 1,
			.gpio0  = 0x0700,
			.gpio2  = 0x0101,
		},{
			.type   = CX88_VMUX_SVIDEO,
			.vmux   = 2,
			.gpio0  = 0x0700,
			.gpio2  = 0x0101,
		}},
		.mpeg           = CX88_MPEG_DVB,
	},
	[CX88_BOARD_DVICO_FUSIONHDTV_DVB_T_DUAL] = {
		.name           = "DViCO FusionHDTV DVB-T Dual Digital",
		.tuner_type     = TUNER_ABSENT, /* No analog tuner */
		.radio_type     = UNSET,
		.tuner_addr	= ADDR_UNSET,
		.radio_addr	= ADDR_UNSET,
		.input          = {{
			.type   = CX88_VMUX_COMPOSITE1,
			.vmux   = 1,
			.gpio0  = 0x000067df,
		 },{
			.type   = CX88_VMUX_SVIDEO,
			.vmux   = 2,
			.gpio0  = 0x000067df,
		}},
		.mpeg           = CX88_MPEG_DVB,
	},
	[CX88_BOARD_KWORLD_HARDWARE_MPEG_TV_XPERT] = {
		.name           = "KWorld HardwareMpegTV XPert",
		.tuner_type     = TUNER_PHILIPS_TDA8290,
		.radio_type     = UNSET,
		.tuner_addr	= ADDR_UNSET,
		.radio_addr	= ADDR_UNSET,
		.input          = {{
			.type   = CX88_VMUX_TELEVISION,
			.vmux   = 0,
			.gpio0  = 0x3de2,
			.gpio2  = 0x00ff,
		},{
			.type   = CX88_VMUX_COMPOSITE1,
			.vmux   = 1,
			.gpio0  = 0x3de6,
			.audioroute = 1,
		},{
			.type   = CX88_VMUX_SVIDEO,
			.vmux   = 2,
			.gpio0  = 0x3de6,
			.audioroute = 1,
		}},
		.radio = {
			.type   = CX88_RADIO,
			.gpio0  = 0x3de6,
			.gpio2  = 0x00ff,
		},
		.mpeg           = CX88_MPEG_BLACKBIRD,
	},
	[CX88_BOARD_DVICO_FUSIONHDTV_DVB_T_HYBRID] = {
		.name           = "DViCO FusionHDTV DVB-T Hybrid",
		.tuner_type     = TUNER_THOMSON_FE6600,
		.radio_type     = UNSET,
		.tuner_addr	= ADDR_UNSET,
		.radio_addr	= ADDR_UNSET,
		.input          = {{
			.type   = CX88_VMUX_TELEVISION,
			.vmux   = 0,
			.gpio0  = 0x0000a75f,
		},{
			.type   = CX88_VMUX_COMPOSITE1,
			.vmux   = 1,
			.gpio0  = 0x0000a75b,
		},{
			.type   = CX88_VMUX_SVIDEO,
			.vmux   = 2,
			.gpio0  = 0x0000a75b,
		}},
		.mpeg           = CX88_MPEG_DVB,
	},
	[CX88_BOARD_PCHDTV_HD5500] = {
		.name           = "pcHDTV HD5500 HDTV",
		.tuner_type     = TUNER_LG_TDVS_H06XF, /* TDVS-H064F */
		.radio_type     = UNSET,
		.tuner_addr	= ADDR_UNSET,
		.radio_addr	= ADDR_UNSET,
		.tda9887_conf   = TDA9887_PRESENT,
		.input          = {{
			.type   = CX88_VMUX_TELEVISION,
			.vmux   = 0,
			.gpio0  = 0x87fd,
		},{
			.type   = CX88_VMUX_COMPOSITE1,
			.vmux   = 1,
			.gpio0  = 0x87f9,
		},{
			.type   = CX88_VMUX_SVIDEO,
			.vmux   = 2,
			.gpio0  = 0x87f9,
		}},
		.mpeg           = CX88_MPEG_DVB,
	},
	[CX88_BOARD_KWORLD_MCE200_DELUXE] = {
		/* FIXME: tested TV input only, disabled composite,
		   svideo and radio until they can be tested also. */
		.name           = "Kworld MCE 200 Deluxe",
		.tuner_type     = TUNER_TENA_9533_DI,
		.radio_type     = UNSET,
		.tda9887_conf   = TDA9887_PRESENT,
		.tuner_addr     = ADDR_UNSET,
		.radio_addr     = ADDR_UNSET,
		.input          = {{
			.type   = CX88_VMUX_TELEVISION,
			.vmux   = 0,
			.gpio0  = 0x0000BDE6
		}},
		.mpeg           = CX88_MPEG_BLACKBIRD,
	},
	[CX88_BOARD_PIXELVIEW_PLAYTV_P7000] = {
		/* FIXME: SVideo, Composite and FM inputs are untested */
		.name           = "PixelView PlayTV P7000",
		.tuner_type     = TUNER_PHILIPS_FM1216ME_MK3,
		.radio_type     = UNSET,
		.tuner_addr	= ADDR_UNSET,
		.radio_addr	= ADDR_UNSET,
		.tda9887_conf   = TDA9887_PRESENT | TDA9887_PORT1_ACTIVE |
				  TDA9887_PORT2_ACTIVE,
		.input          = {{
			.type   = CX88_VMUX_TELEVISION,
			.vmux   = 0,
			.gpio0  = 0x5da6,
		}},
		.mpeg           = CX88_MPEG_BLACKBIRD,
	},
	[CX88_BOARD_NPGTECH_REALTV_TOP10FM] = {
		.name           = "NPG Tech Real TV FM Top 10",
		.tuner_type     = TUNER_TNF_5335MF, /* Actually a TNF9535 */
		.radio_type     = UNSET,
		.tuner_addr	= ADDR_UNSET,
		.radio_addr	= ADDR_UNSET,
		.input          = {{
			.type   = CX88_VMUX_TELEVISION,
			.vmux   = 0,
			.gpio0	= 0x0788,
		},{
			.type   = CX88_VMUX_COMPOSITE1,
			.vmux   = 1,
			.gpio0	= 0x078b,
		},{
			.type   = CX88_VMUX_SVIDEO,
			.vmux   = 2,
			.gpio0	= 0x078b,
		}},
		.radio = {
			 .type  = CX88_RADIO,
			 .gpio0 = 0x074a,
		},
	},
	[CX88_BOARD_WINFAST_DTV2000H] = {
		/* video inputs and radio still in testing */
		.name           = "WinFast DTV2000 H",
		.tuner_type     = TUNER_PHILIPS_FMD1216ME_MK3,
		.radio_type     = UNSET,
		.tuner_addr     = ADDR_UNSET,
		.radio_addr     = ADDR_UNSET,
		.tda9887_conf   = TDA9887_PRESENT,
		.input          = {{
			.type   = CX88_VMUX_TELEVISION,
			.vmux   = 0,
			.gpio0  = 0x00017304,
			.gpio1  = 0x00008203,
			.gpio2  = 0x00017304,
			.gpio3  = 0x02000000,
		}},
		.mpeg           = CX88_MPEG_DVB,
	},
	[CX88_BOARD_GENIATECH_DVBS] = {
		.name          = "Geniatech DVB-S",
		.tuner_type    = TUNER_ABSENT,
		.radio_type    = UNSET,
		.tuner_addr    = ADDR_UNSET,
		.radio_addr    = ADDR_UNSET,
		.input  = {{
			.type  = CX88_VMUX_DVB,
			.vmux  = 0,
		},{
			.type  = CX88_VMUX_COMPOSITE1,
			.vmux  = 1,
		}},
		.mpeg           = CX88_MPEG_DVB,
	},
	[CX88_BOARD_HAUPPAUGE_HVR3000] = {
		/* FIXME: Add dvb & radio support */
		.name           = "Hauppauge WinTV-HVR3000 TriMode Analog/DVB-S/DVB-T",
		.tuner_type     = TUNER_PHILIPS_FMD1216ME_MK3,
		.radio_type     = UNSET,
		.tuner_addr     = ADDR_UNSET,
		.radio_addr     = ADDR_UNSET,
		.tda9887_conf   = TDA9887_PRESENT,
		.input          = {{
			.type   = CX88_VMUX_TELEVISION,
			.vmux   = 0,
			.gpio0  = 0x84bf,
		},{
			.type   = CX88_VMUX_COMPOSITE1,
			.vmux   = 1,
			.gpio0  = 0x84bf,
		},{
			.type   = CX88_VMUX_SVIDEO,
			.vmux   = 2,
			.gpio0  = 0x84bf,
		}},
		.mpeg           = CX88_MPEG_DVB,
	},
	[CX88_BOARD_NORWOOD_MICRO] = {
		.name           = "Norwood Micro TV Tuner",
		.tuner_type     = TUNER_TNF_5335MF,
		.radio_type     = UNSET,
		.tuner_addr     = ADDR_UNSET,
		.radio_addr     = ADDR_UNSET,
		.input          = {{
			.type   = CX88_VMUX_TELEVISION,
			.vmux   = 0,
			.gpio0  = 0x0709,
		},{
			.type   = CX88_VMUX_COMPOSITE1,
			.vmux   = 1,
			.gpio0  = 0x070b,
		},{
			.type   = CX88_VMUX_SVIDEO,
			.vmux   = 2,
			.gpio0  = 0x070b,
		}},
	},
	[CX88_BOARD_TE_DTV_250_OEM_SWANN] = {
		.name           = "Shenzhen Tungsten Ages Tech TE-DTV-250 / Swann OEM",
		.tuner_type     = TUNER_LG_PAL_NEW_TAPC,
		.radio_type     = UNSET,
		.tuner_addr     = ADDR_UNSET,
		.radio_addr     = ADDR_UNSET,
		.input          = {{
			.type   = CX88_VMUX_TELEVISION,
			.vmux   = 0,
			.gpio0  = 0x003fffff,
			.gpio1  = 0x00e00000,
			.gpio2  = 0x003fffff,
			.gpio3  = 0x02000000,
		},{
			.type   = CX88_VMUX_COMPOSITE1,
			.vmux   = 1,
			.gpio0  = 0x003fffff,
			.gpio1  = 0x00e00000,
			.gpio2  = 0x003fffff,
			.gpio3  = 0x02000000,
		},{
			.type   = CX88_VMUX_SVIDEO,
			.vmux   = 2,
			.gpio0  = 0x003fffff,
			.gpio1  = 0x00e00000,
			.gpio2  = 0x003fffff,
			.gpio3  = 0x02000000,
		}},
	},
	[CX88_BOARD_HAUPPAUGE_HVR1300] = {
		.name		= "Hauppauge WinTV-HVR1300 DVB-T/Hybrid MPEG Encoder",
		.tuner_type     = TUNER_PHILIPS_FMD1216ME_MK3,
		.radio_type	= UNSET,
		.tuner_addr	= ADDR_UNSET,
		.radio_addr	= ADDR_UNSET,
		.tda9887_conf   = TDA9887_PRESENT,
		.audio_chip     = AUDIO_CHIP_WM8775,
		.input		= {{
			.type   = CX88_VMUX_TELEVISION,
			.vmux   = 0,
			.gpio0	= 0xe780,
			.audioroute = 1,
		},{
			.type	= CX88_VMUX_COMPOSITE1,
			.vmux	= 1,
			.gpio0	= 0xe780,
			.audioroute = 2,
		},{
			.type	= CX88_VMUX_SVIDEO,
			.vmux	= 2,
			.gpio0	= 0xe780,
			.audioroute = 2,
		}},
		/* fixme: Add radio support */
		.mpeg           = CX88_MPEG_DVB | CX88_MPEG_BLACKBIRD,
	},
	[CX88_BOARD_ADSTECH_PTV_390] = {
		.name           = "ADS Tech Instant Video PCI",
		.tuner_type     = TUNER_ABSENT,
		.radio_type     = UNSET,
		.tuner_addr     = ADDR_UNSET,
		.radio_addr     = ADDR_UNSET,
		.input          = {{
			.type   = CX88_VMUX_DEBUG,
			.vmux   = 3,
			.gpio0  = 0x04ff,
		},{
			.type   = CX88_VMUX_COMPOSITE1,
			.vmux   = 1,
			.gpio0  = 0x07fa,
		},{
			.type   = CX88_VMUX_SVIDEO,
			.vmux   = 2,
			.gpio0  = 0x07fa,
		}},
	},
	[CX88_BOARD_PINNACLE_PCTV_HD_800i] = {
		.name           = "Pinnacle PCTV HD 800i",
		.tuner_type     = TUNER_XC5000,
		.radio_type     = UNSET,
		.tuner_addr	= ADDR_UNSET,
		.radio_addr	= ADDR_UNSET,
		.input          = {{
			.type   = CX88_VMUX_TELEVISION,
			.vmux   = 0,
			.gpio0  = 0x04fb,
			.gpio1  = 0x10ff,
		},{
			.type   = CX88_VMUX_COMPOSITE1,
			.vmux   = 1,
			.gpio0  = 0x04fb,
			.gpio1  = 0x10ef,
			.audioroute = 1,
		},{
			.type   = CX88_VMUX_SVIDEO,
			.vmux   = 2,
			.gpio0  = 0x04fb,
			.gpio1  = 0x10ef,
			.audioroute = 1,
		}},
		.mpeg           = CX88_MPEG_DVB,
	},
};

/* ------------------------------------------------------------------ */
/* PCI subsystem IDs                                                  */

static const struct cx88_subid cx88_subids[] = {
	{
		.subvendor = 0x0070,
		.subdevice = 0x3400,
		.card      = CX88_BOARD_HAUPPAUGE,
	},{
		.subvendor = 0x0070,
		.subdevice = 0x3401,
		.card      = CX88_BOARD_HAUPPAUGE,
	},{
		.subvendor = 0x14c7,
		.subdevice = 0x0106,
		.card      = CX88_BOARD_GDI,
	},{
		.subvendor = 0x14c7,
		.subdevice = 0x0107, /* with mpeg encoder */
		.card      = CX88_BOARD_GDI,
	},{
		.subvendor = PCI_VENDOR_ID_ATI,
		.subdevice = 0x00f8,
		.card      = CX88_BOARD_ATI_WONDER_PRO,
	},{
		.subvendor = 0x107d,
		.subdevice = 0x6611,
		.card      = CX88_BOARD_WINFAST2000XP_EXPERT,
	},{
		.subvendor = 0x107d,
		.subdevice = 0x6613,	/* NTSC */
		.card      = CX88_BOARD_WINFAST2000XP_EXPERT,
	},{
		.subvendor = 0x107d,
		.subdevice = 0x6620,
		.card      = CX88_BOARD_WINFAST_DV2000,
	},{
		.subvendor = 0x107d,
		.subdevice = 0x663b,
		.card      = CX88_BOARD_LEADTEK_PVR2000,
	},{
		.subvendor = 0x107d,
		.subdevice = 0x663c,
		.card      = CX88_BOARD_LEADTEK_PVR2000,
	},{
		.subvendor = 0x1461,
		.subdevice = 0x000b,
		.card      = CX88_BOARD_AVERTV_STUDIO_303,
	},{
		.subvendor = 0x1462,
		.subdevice = 0x8606,
		.card      = CX88_BOARD_MSI_TVANYWHERE_MASTER,
	},{
		.subvendor = 0x10fc,
		.subdevice = 0xd003,
		.card      = CX88_BOARD_IODATA_GVVCP3PCI,
	},{
		.subvendor = 0x1043,
		.subdevice = 0x4823,  /* with mpeg encoder */
		.card      = CX88_BOARD_ASUS_PVR_416,
	},{
		.subvendor = 0x17de,
		.subdevice = 0x08a6,
		.card      = CX88_BOARD_KWORLD_DVB_T,
	},{
		.subvendor = 0x18ac,
		.subdevice = 0xd810,
		.card      = CX88_BOARD_DVICO_FUSIONHDTV_3_GOLD_Q,
	},{
		.subvendor = 0x18ac,
		.subdevice = 0xd820,
		.card      = CX88_BOARD_DVICO_FUSIONHDTV_3_GOLD_T,
	},{
		.subvendor = 0x18ac,
		.subdevice = 0xdb00,
		.card      = CX88_BOARD_DVICO_FUSIONHDTV_DVB_T1,
	},{
		.subvendor = 0x0070,
		.subdevice = 0x9002,
		.card      = CX88_BOARD_HAUPPAUGE_DVB_T1,
	},{
		.subvendor = 0x14f1,
		.subdevice = 0x0187,
		.card      = CX88_BOARD_CONEXANT_DVB_T1,
	},{
		.subvendor = 0x1540,
		.subdevice = 0x2580,
		.card      = CX88_BOARD_PROVIDEO_PV259,
	},{
		.subvendor = 0x18ac,
		.subdevice = 0xdb10,
		.card      = CX88_BOARD_DVICO_FUSIONHDTV_DVB_T_PLUS,
	},{
		.subvendor = 0x1554,
		.subdevice = 0x4811,
		.card      = CX88_BOARD_PIXELVIEW,
	},{
		.subvendor = 0x7063,
		.subdevice = 0x3000, /* HD-3000 card */
		.card      = CX88_BOARD_PCHDTV_HD3000,
	},{
		.subvendor = 0x17de,
		.subdevice = 0xa8a6,
		.card      = CX88_BOARD_DNTV_LIVE_DVB_T,
	},{
		.subvendor = 0x0070,
		.subdevice = 0x2801,
		.card      = CX88_BOARD_HAUPPAUGE_ROSLYN,
	},{
		.subvendor = 0x14f1,
		.subdevice = 0x0342,
		.card      = CX88_BOARD_DIGITALLOGIC_MEC,
	},{
		.subvendor = 0x10fc,
		.subdevice = 0xd035,
		.card      = CX88_BOARD_IODATA_GVBCTV7E,
	},{
		.subvendor = 0x1421,
		.subdevice = 0x0334,
		.card      = CX88_BOARD_ADSTECH_DVB_T_PCI,
	},{
		.subvendor = 0x153b,
		.subdevice = 0x1166,
		.card      = CX88_BOARD_TERRATEC_CINERGY_1400_DVB_T1,
	},{
		.subvendor = 0x18ac,
		.subdevice = 0xd500,
		.card      = CX88_BOARD_DVICO_FUSIONHDTV_5_GOLD,
	},{
		.subvendor = 0x1461,
		.subdevice = 0x8011,
		.card      = CX88_BOARD_AVERMEDIA_ULTRATV_MC_550,
	},{
		.subvendor = PCI_VENDOR_ID_ATI,
		.subdevice = 0xa101,
		.card      = CX88_BOARD_ATI_HDTVWONDER,
	},{
		.subvendor = 0x107d,
		.subdevice = 0x665f,
		.card      = CX88_BOARD_WINFAST_DTV1000,
	},{
		.subvendor = 0x1461,
		.subdevice = 0x000a,
		.card      = CX88_BOARD_AVERTV_303,
	},{
		.subvendor = 0x0070,
		.subdevice = 0x9200,
		.card      = CX88_BOARD_HAUPPAUGE_NOVASE2_S1,
	},{
		.subvendor = 0x0070,
		.subdevice = 0x9201,
		.card      = CX88_BOARD_HAUPPAUGE_NOVASPLUS_S1,
	},{
		.subvendor = 0x0070,
		.subdevice = 0x9202,
		.card      = CX88_BOARD_HAUPPAUGE_NOVASPLUS_S1,
	},{
		.subvendor = 0x17de,
		.subdevice = 0x08b2,
		.card      = CX88_BOARD_KWORLD_DVBS_100,
	},{
		.subvendor = 0x0070,
		.subdevice = 0x9400,
		.card      = CX88_BOARD_HAUPPAUGE_HVR1100,
	},{
		.subvendor = 0x0070,
		.subdevice = 0x9402,
		.card      = CX88_BOARD_HAUPPAUGE_HVR1100,
	},{
		.subvendor = 0x0070,
		.subdevice = 0x9800,
		.card      = CX88_BOARD_HAUPPAUGE_HVR1100LP,
	},{
		.subvendor = 0x0070,
		.subdevice = 0x9802,
		.card      = CX88_BOARD_HAUPPAUGE_HVR1100LP,
	},{
		.subvendor = 0x0070,
		.subdevice = 0x9001,
		.card      = CX88_BOARD_HAUPPAUGE_DVB_T1,
	},{
		.subvendor = 0x1822,
		.subdevice = 0x0025,
		.card      = CX88_BOARD_DNTV_LIVE_DVB_T_PRO,
	},{
		.subvendor = 0x17de,
		.subdevice = 0x08a1,
		.card      = CX88_BOARD_KWORLD_DVB_T_CX22702,
	},{
		.subvendor = 0x18ac,
		.subdevice = 0xdb50,
		.card      = CX88_BOARD_DVICO_FUSIONHDTV_DVB_T_DUAL,
	},{
		.subvendor = 0x18ac,
		.subdevice = 0xdb54,
		.card      = CX88_BOARD_DVICO_FUSIONHDTV_DVB_T_DUAL,
		/* Re-branded DViCO: DigitalNow DVB-T Dual */
	},{
		.subvendor = 0x18ac,
		.subdevice = 0xdb11,
		.card      = CX88_BOARD_DVICO_FUSIONHDTV_DVB_T_PLUS,
		/* Re-branded DViCO: UltraView DVB-T Plus */
	},{
		.subvendor = 0x17de,
		.subdevice = 0x0840,
		.card      = CX88_BOARD_KWORLD_HARDWARE_MPEG_TV_XPERT,
	},{
		.subvendor = 0x1421,
		.subdevice = 0x0305,
		.card      = CX88_BOARD_KWORLD_HARDWARE_MPEG_TV_XPERT,
	},{
		.subvendor = 0x18ac,
		.subdevice = 0xdb40,
		.card      = CX88_BOARD_DVICO_FUSIONHDTV_DVB_T_HYBRID,
	},{
		.subvendor = 0x18ac,
		.subdevice = 0xdb44,
		.card      = CX88_BOARD_DVICO_FUSIONHDTV_DVB_T_HYBRID,
	},{
		.subvendor = 0x7063,
		.subdevice = 0x5500,
		.card      = CX88_BOARD_PCHDTV_HD5500,
	},{
		.subvendor = 0x17de,
		.subdevice = 0x0841,
		.card      = CX88_BOARD_KWORLD_MCE200_DELUXE,
	},{
		.subvendor = 0x1822,
		.subdevice = 0x0019,
		.card      = CX88_BOARD_DNTV_LIVE_DVB_T_PRO,
	},{
		.subvendor = 0x1554,
		.subdevice = 0x4813,
		.card      = CX88_BOARD_PIXELVIEW_PLAYTV_P7000,
	},{
		.subvendor = 0x14f1,
		.subdevice = 0x0842,
		.card      = CX88_BOARD_NPGTECH_REALTV_TOP10FM,
	},{
		.subvendor = 0x107d,
		.subdevice = 0x665e,
		.card      = CX88_BOARD_WINFAST_DTV2000H,
	},{
		.subvendor = 0x18ac,
		.subdevice = 0xd800, /* FusionHDTV 3 Gold (original revision) */
		.card      = CX88_BOARD_DVICO_FUSIONHDTV_3_GOLD_Q,
	},{
		.subvendor = 0x14f1,
		.subdevice = 0x0084,
		.card      = CX88_BOARD_GENIATECH_DVBS,
	},{
		.subvendor = 0x0070,
		.subdevice = 0x1404,
		.card      = CX88_BOARD_HAUPPAUGE_HVR3000,
	},{
		.subvendor = 0x1461,
		.subdevice = 0xc111, /* AverMedia M150-D */
		/* This board is known to work with the ASUS PVR416 config */
		.card      = CX88_BOARD_ASUS_PVR_416,
	},{
		.subvendor = 0xc180,
		.subdevice = 0xc980,
		.card      = CX88_BOARD_TE_DTV_250_OEM_SWANN,
	},{
		.subvendor = 0x0070,
		.subdevice = 0x9600,
		.card      = CX88_BOARD_HAUPPAUGE_HVR1300,
	},{
		.subvendor = 0x0070,
		.subdevice = 0x9601,
		.card      = CX88_BOARD_HAUPPAUGE_HVR1300,
	},{
		.subvendor = 0x0070,
		.subdevice = 0x9602,
		.card      = CX88_BOARD_HAUPPAUGE_HVR1300,
	},{
		.subvendor = 0x107d,
		.subdevice = 0x6632,
		.card      = CX88_BOARD_LEADTEK_PVR2000,
	},{
		.subvendor = 0x12ab,
		.subdevice = 0x2300, /* Club3D Zap TV2100 */
		.card      = CX88_BOARD_KWORLD_DVB_T_CX22702,
	},{
		.subvendor = 0x0070,
		.subdevice = 0x9000,
		.card      = CX88_BOARD_HAUPPAUGE_DVB_T1,
	},{
		.subvendor = 0x0070,
		.subdevice = 0x1400,
		.card      = CX88_BOARD_HAUPPAUGE_HVR3000,
	},{
		.subvendor = 0x0070,
		.subdevice = 0x1401,
		.card      = CX88_BOARD_HAUPPAUGE_HVR3000,
	},{
		.subvendor = 0x0070,
		.subdevice = 0x1402,
		.card      = CX88_BOARD_HAUPPAUGE_HVR3000,
	},{
		.subvendor = 0x1421,
		.subdevice = 0x0341, /* ADS Tech InstantTV DVB-S */
		.card      = CX88_BOARD_KWORLD_DVBS_100,
	},{
		.subvendor = 0x1421,
		.subdevice = 0x0390,
		.card      = CX88_BOARD_ADSTECH_PTV_390,
	},{
		.subvendor = 0x11bd,
		.subdevice = 0x0051,
		.card      = CX88_BOARD_PINNACLE_PCTV_HD_800i,
	},
};

/* ----------------------------------------------------------------------- */
/* some leadtek specific stuff                                             */

static void leadtek_eeprom(struct cx88_core *core, u8 *eeprom_data)
{
	/* This is just for the "Winfast 2000XP Expert" board ATM; I don't have data on
	 * any others.
	 *
	 * Byte 0 is 1 on the NTSC board.
	 */

	if (eeprom_data[4] != 0x7d ||
	    eeprom_data[5] != 0x10 ||
	    eeprom_data[7] != 0x66) {
		printk(KERN_WARNING "%s: Leadtek eeprom invalid.\n",
		       core->name);
		return;
	}

	core->board.tuner_type = (eeprom_data[6] == 0x13) ?
		TUNER_PHILIPS_FM1236_MK3 : TUNER_PHILIPS_FM1216ME_MK3;

	printk(KERN_INFO "%s: Leadtek Winfast 2000XP Expert config: "
	       "tuner=%d, eeprom[0]=0x%02x\n",
	       core->name, core->board.tuner_type, eeprom_data[0]);
}

static void hauppauge_eeprom(struct cx88_core *core, u8 *eeprom_data)
{
	struct tveeprom tv;

	tveeprom_hauppauge_analog(&core->i2c_client, &tv, eeprom_data);
	core->board.tuner_type = tv.tuner_type;
	core->tuner_formats = tv.tuner_formats;
	core->board.radio.type = tv.has_radio ? CX88_RADIO : 0;

	/* Make sure we support the board model */
	switch (tv.model)
	{
	case 14009: /* WinTV-HVR3000 (Retail, IR, b/panel video, 3.5mm audio in) */
	case 14019: /* WinTV-HVR3000 (Retail, IR Blaster, b/panel video, 3.5mm audio in) */
	case 14029: /* WinTV-HVR3000 (Retail, IR, b/panel video, 3.5mm audio in - 880 bridge) */
	case 14109: /* WinTV-HVR3000 (Retail, IR, b/panel video, 3.5mm audio in - low profile) */
	case 14129: /* WinTV-HVR3000 (Retail, IR, b/panel video, 3.5mm audio in - 880 bridge - LP) */
	case 14559: /* WinTV-HVR3000 (OEM, no IR, b/panel video, 3.5mm audio in) */
	case 14569: /* WinTV-HVR3000 (OEM, no IR, no back panel video) */
	case 14659: /* WinTV-HVR3000 (OEM, no IR, b/panel video, RCA audio in - Low profile) */
	case 14669: /* WinTV-HVR3000 (OEM, no IR, no b/panel video - Low profile) */
	case 28552: /* WinTV-PVR 'Roslyn' (No IR) */
	case 34519: /* WinTV-PCI-FM */
	case 90002: /* Nova-T-PCI (9002) */
	case 92001: /* Nova-S-Plus (Video and IR) */
	case 92002: /* Nova-S-Plus (Video and IR) */
	case 90003: /* Nova-T-PCI (9002 No RF out) */
	case 90500: /* Nova-T-PCI (oem) */
	case 90501: /* Nova-T-PCI (oem/IR) */
	case 92000: /* Nova-SE2 (OEM, No Video or IR) */
	case 94009: /* WinTV-HVR1100 (Video and IR Retail) */
	case 94501: /* WinTV-HVR1100 (Video and IR OEM) */
	case 96009: /* WinTV-HVR1300 (PAL Video, MPEG Video and IR RX) */
	case 96019: /* WinTV-HVR1300 (PAL Video, MPEG Video and IR RX/TX) */
	case 96559: /* WinTV-HVR1300 (PAL Video, MPEG Video no IR) */
	case 96569: /* WinTV-HVR1300 () */
	case 96659: /* WinTV-HVR1300 () */
	case 98559: /* WinTV-HVR1100LP (Video no IR, Retail - Low Profile) */
		/* known */
		break;
	default:
		printk("%s: warning: unknown hauppauge model #%d\n",
		       core->name, tv.model);
		break;
	}

	printk(KERN_INFO "%s: hauppauge eeprom: model=%d\n",
			core->name, tv.model);
}

/* ----------------------------------------------------------------------- */
/* some GDI (was: Modular Technology) specific stuff                       */

static struct {
	int  id;
	int  fm;
	char *name;
} gdi_tuner[] = {
	[ 0x01 ] = { .id   = TUNER_ABSENT,
		     .name = "NTSC_M" },
	[ 0x02 ] = { .id   = TUNER_ABSENT,
		     .name = "PAL_B" },
	[ 0x03 ] = { .id   = TUNER_ABSENT,
		     .name = "PAL_I" },
	[ 0x04 ] = { .id   = TUNER_ABSENT,
		     .name = "PAL_D" },
	[ 0x05 ] = { .id   = TUNER_ABSENT,
		     .name = "SECAM" },

	[ 0x10 ] = { .id   = TUNER_ABSENT,
		     .fm   = 1,
		     .name = "TEMIC_4049" },
	[ 0x11 ] = { .id   = TUNER_TEMIC_4136FY5,
		     .name = "TEMIC_4136" },
	[ 0x12 ] = { .id   = TUNER_ABSENT,
		     .name = "TEMIC_4146" },

	[ 0x20 ] = { .id   = TUNER_PHILIPS_FQ1216ME,
		     .fm   = 1,
		     .name = "PHILIPS_FQ1216_MK3" },
	[ 0x21 ] = { .id   = TUNER_ABSENT, .fm = 1,
		     .name = "PHILIPS_FQ1236_MK3" },
	[ 0x22 ] = { .id   = TUNER_ABSENT,
		     .name = "PHILIPS_FI1236_MK3" },
	[ 0x23 ] = { .id   = TUNER_ABSENT,
		     .name = "PHILIPS_FI1216_MK3" },
};

static void gdi_eeprom(struct cx88_core *core, u8 *eeprom_data)
{
	char *name = (eeprom_data[0x0d] < ARRAY_SIZE(gdi_tuner))
		? gdi_tuner[eeprom_data[0x0d]].name : NULL;

	printk(KERN_INFO "%s: GDI: tuner=%s\n", core->name,
	       name ? name : "unknown");
	if (NULL == name)
		return;
	core->board.tuner_type = gdi_tuner[eeprom_data[0x0d]].id;
	core->board.radio.type = gdi_tuner[eeprom_data[0x0d]].fm ?
		CX88_RADIO : 0;
}

/* ----------------------------------------------------------------------- */
/* some DViCO specific stuff                                               */

static void dvico_fusionhdtv_hybrid_init(struct cx88_core *core)
{
	struct i2c_msg msg = { .addr = 0x45, .flags = 0 };
	int i, err;
	static u8 init_bufs[13][5] = {
		{ 0x10, 0x00, 0x20, 0x01, 0x03 },
		{ 0x10, 0x10, 0x01, 0x00, 0x21 },
		{ 0x10, 0x10, 0x10, 0x00, 0xCA },
		{ 0x10, 0x10, 0x12, 0x00, 0x08 },
		{ 0x10, 0x10, 0x13, 0x00, 0x0A },
		{ 0x10, 0x10, 0x16, 0x01, 0xC0 },
		{ 0x10, 0x10, 0x22, 0x01, 0x3D },
		{ 0x10, 0x10, 0x73, 0x01, 0x2E },
		{ 0x10, 0x10, 0x72, 0x00, 0xC5 },
		{ 0x10, 0x10, 0x71, 0x01, 0x97 },
		{ 0x10, 0x10, 0x70, 0x00, 0x0F },
		{ 0x10, 0x10, 0xB0, 0x00, 0x01 },
		{ 0x03, 0x0C },
	};

	for (i = 0; i < ARRAY_SIZE(init_bufs); i++) {
		msg.buf = init_bufs[i];
		msg.len = (i != 12 ? 5 : 2);
		err = i2c_transfer(&core->i2c_adap, &msg, 1);
		if (err != 1) {
			printk("dvico_fusionhdtv_hybrid_init buf %d failed (err = %d)!\n", i, err);
			return;
		}
	}
}

/* ----------------------------------------------------------------------- */
/* Tuner callback function. Currently only needed for the Pinnacle 	   *
 * PCTV HD 800i with an xc5000 sillicon tuner. This is used for both	   *
 * analog tuner attach (tuner-core.c) and dvb tuner attach (cx88-dvb.c)    */

int cx88_tuner_callback(void *priv, int command, int arg)
{
	struct i2c_algo_bit_data *i2c_algo = priv;
	struct cx88_core *core = i2c_algo->data;

	switch(core->boardnr) {
	case CX88_BOARD_PINNACLE_PCTV_HD_800i:
		if(command == 0) { /* This is the reset command from xc5000 */
			/* Reset XC5000 tuner via SYS_RSTO_pin */
			cx_write(MO_SRST_IO, 0);
			msleep(10);
			cx_write(MO_SRST_IO, 1);
			return 0;
		}
		else {
			printk(KERN_ERR
				"xc5000: unknown tuner callback command.\n");
			return -EINVAL;
		}
		break;
	}
	return 0; /* Should never be here */
}
EXPORT_SYMBOL(cx88_tuner_callback);

/* ----------------------------------------------------------------------- */

static void cx88_card_list(struct cx88_core *core, struct pci_dev *pci)
{
	int i;

	if (0 == pci->subsystem_vendor &&
	    0 == pci->subsystem_device) {
		printk("%s: Your board has no valid PCI Subsystem ID and thus can't\n"
		       "%s: be autodetected.  Please pass card=<n> insmod option to\n"
		       "%s: workaround that.  Redirect complaints to the vendor of\n"
		       "%s: the TV card.  Best regards,\n"
		       "%s:         -- tux\n",
		       core->name,core->name,core->name,core->name,core->name);
	} else {
		printk("%s: Your board isn't known (yet) to the driver.  You can\n"
		       "%s: try to pick one of the existing card configs via\n"
		       "%s: card=<n> insmod option.  Updating to the latest\n"
		       "%s: version might help as well.\n",
		       core->name,core->name,core->name,core->name);
	}
	printk("%s: Here is a list of valid choices for the card=<n> insmod option:\n",
	       core->name);
	for (i = 0; i < ARRAY_SIZE(cx88_boards); i++)
		printk("%s:    card=%d -> %s\n",
		       core->name, i, cx88_boards[i].name);
}

static void cx88_card_setup_pre_i2c(struct cx88_core *core)
{
	switch (core->boardnr) {
	case CX88_BOARD_HAUPPAUGE_HVR1300:
		/* Bring the 702 demod up before i2c scanning/attach or devices are hidden */
		/* We leave here with the 702 on the bus */
		cx_write(MO_GP0_IO, 0x0000e780);
		udelay(1000);
		cx_clear(MO_GP0_IO, 0x00000080);
		udelay(50);
		cx_set(MO_GP0_IO, 0x00000080); /* 702 out of reset */
		udelay(1000);
		break;
	}
}

static void cx88_card_setup(struct cx88_core *core)
{
	static u8 eeprom[256];

	if (0 == core->i2c_rc) {
		core->i2c_client.addr = 0xa0 >> 1;
		tveeprom_read(&core->i2c_client,eeprom,sizeof(eeprom));
	}

	switch (core->boardnr) {
	case CX88_BOARD_HAUPPAUGE:
	case CX88_BOARD_HAUPPAUGE_ROSLYN:
		if (0 == core->i2c_rc)
			hauppauge_eeprom(core,eeprom+8);
		break;
	case CX88_BOARD_GDI:
		if (0 == core->i2c_rc)
			gdi_eeprom(core,eeprom);
		break;
	case CX88_BOARD_WINFAST2000XP_EXPERT:
		if (0 == core->i2c_rc)
			leadtek_eeprom(core,eeprom);
		break;
	case CX88_BOARD_HAUPPAUGE_NOVASPLUS_S1:
	case CX88_BOARD_HAUPPAUGE_NOVASE2_S1:
	case CX88_BOARD_HAUPPAUGE_DVB_T1:
	case CX88_BOARD_HAUPPAUGE_HVR1100:
	case CX88_BOARD_HAUPPAUGE_HVR1100LP:
	case CX88_BOARD_HAUPPAUGE_HVR3000:
	case CX88_BOARD_HAUPPAUGE_HVR1300:
		if (0 == core->i2c_rc)
			hauppauge_eeprom(core,eeprom);
		break;
	case CX88_BOARD_KWORLD_DVBS_100:
		cx_write(MO_GP0_IO, 0x000007f8);
		cx_write(MO_GP1_IO, 0x00000001);
		break;
	case CX88_BOARD_DVICO_FUSIONHDTV_DVB_T_DUAL:
		/* GPIO0:6 is hooked to FX2 reset pin */
		cx_set(MO_GP0_IO, 0x00004040);
		cx_clear(MO_GP0_IO, 0x00000040);
		msleep(1000);
		cx_set(MO_GP0_IO, 0x00004040);
		/* FALLTHROUGH */
	case CX88_BOARD_DVICO_FUSIONHDTV_DVB_T1:
	case CX88_BOARD_DVICO_FUSIONHDTV_DVB_T_PLUS:
	case CX88_BOARD_DVICO_FUSIONHDTV_DVB_T_HYBRID:
		/* GPIO0:0 is hooked to mt352 reset pin */
		cx_set(MO_GP0_IO, 0x00000101);
		cx_clear(MO_GP0_IO, 0x00000001);
		msleep(1);
		cx_set(MO_GP0_IO, 0x00000101);
		if (0 == core->i2c_rc &&
		    core->boardnr == CX88_BOARD_DVICO_FUSIONHDTV_DVB_T_HYBRID)
			dvico_fusionhdtv_hybrid_init(core);
		break;
	case CX88_BOARD_KWORLD_DVB_T:
	case CX88_BOARD_DNTV_LIVE_DVB_T:
		cx_set(MO_GP0_IO, 0x00000707);
		cx_set(MO_GP2_IO, 0x00000101);
		cx_clear(MO_GP2_IO, 0x00000001);
		msleep(1);
		cx_clear(MO_GP0_IO, 0x00000007);
		cx_set(MO_GP2_IO, 0x00000101);
		break;
	case CX88_BOARD_DNTV_LIVE_DVB_T_PRO:
		cx_write(MO_GP0_IO, 0x00080808);
		break;
	case CX88_BOARD_ATI_HDTVWONDER:
		if (0 == core->i2c_rc) {
			/* enable tuner */
			int i;
			static const u8 buffer [][2] = {
				{0x10,0x12},
				{0x13,0x04},
				{0x16,0x00},
				{0x14,0x04},
				{0x17,0x00}
			};
			core->i2c_client.addr = 0x0a;

			for (i = 0; i < ARRAY_SIZE(buffer); i++)
				if (2 != i2c_master_send(&core->i2c_client,
							buffer[i],2))
					printk(KERN_WARNING
						"%s: Unable to enable "
						"tuner(%i).\n",
						core->name, i);
		}
		break;
	case CX88_BOARD_MSI_TVANYWHERE_MASTER:
	{
		struct v4l2_priv_tun_config tea5767_cfg;
		struct tea5767_ctrl ctl;

		memset(&ctl, 0, sizeof(ctl));

		ctl.high_cut  = 1;
		ctl.st_noise  = 1;
		ctl.deemph_75 = 1;
		ctl.xtal_freq = TEA5767_HIGH_LO_13MHz;

		tea5767_cfg.tuner = TUNER_TEA5767;
		tea5767_cfg.priv  = &ctl;

		cx88_call_i2c_clients(core, TUNER_SET_CONFIG, &tea5767_cfg);
	}
	}
}

/* ------------------------------------------------------------------ */

static int cx88_pci_quirks(const char *name, struct pci_dev *pci)
{
	unsigned int lat = UNSET;
	u8 ctrl = 0;
	u8 value;

	/* check pci quirks */
	if (pci_pci_problems & PCIPCI_TRITON) {
		printk(KERN_INFO "%s: quirk: PCIPCI_TRITON -- set TBFX\n",
		       name);
		ctrl |= CX88X_EN_TBFX;
	}
	if (pci_pci_problems & PCIPCI_NATOMA) {
		printk(KERN_INFO "%s: quirk: PCIPCI_NATOMA -- set TBFX\n",
		       name);
		ctrl |= CX88X_EN_TBFX;
	}
	if (pci_pci_problems & PCIPCI_VIAETBF) {
		printk(KERN_INFO "%s: quirk: PCIPCI_VIAETBF -- set TBFX\n",
		       name);
		ctrl |= CX88X_EN_TBFX;
	}
	if (pci_pci_problems & PCIPCI_VSFX) {
		printk(KERN_INFO "%s: quirk: PCIPCI_VSFX -- set VSFX\n",
		       name);
		ctrl |= CX88X_EN_VSFX;
	}
#ifdef PCIPCI_ALIMAGIK
	if (pci_pci_problems & PCIPCI_ALIMAGIK) {
		printk(KERN_INFO "%s: quirk: PCIPCI_ALIMAGIK -- latency fixup\n",
		       name);
		lat = 0x0A;
	}
#endif

	/* check insmod options */
	if (UNSET != latency)
		lat = latency;

	/* apply stuff */
	if (ctrl) {
		pci_read_config_byte(pci, CX88X_DEVCTRL, &value);
		value |= ctrl;
		pci_write_config_byte(pci, CX88X_DEVCTRL, value);
	}
	if (UNSET != lat) {
		printk(KERN_INFO "%s: setting pci latency timer to %d\n",
		       name, latency);
		pci_write_config_byte(pci, PCI_LATENCY_TIMER, latency);
	}
	return 0;
}

int cx88_get_resources(const struct cx88_core *core, struct pci_dev *pci)
{
	if (request_mem_region(pci_resource_start(pci,0),
			       pci_resource_len(pci,0),
			       core->name))
		return 0;
	printk(KERN_ERR
	       "%s/%d: Can't get MMIO memory @ 0x%llx, subsystem: %04x:%04x\n",
	       core->name, PCI_FUNC(pci->devfn),
	       (unsigned long long)pci_resource_start(pci, 0),
	       pci->subsystem_vendor, pci->subsystem_device);
	return -EBUSY;
}

/* Allocate and initialize the cx88 core struct.  One should hold the
 * devlist mutex before calling this.  */
struct cx88_core *cx88_core_create(struct pci_dev *pci, int nr)
{
	struct cx88_core *core;
	int i;

	core = kzalloc(sizeof(*core), GFP_KERNEL);

	atomic_inc(&core->refcount);
	core->pci_bus  = pci->bus->number;
	core->pci_slot = PCI_SLOT(pci->devfn);
	core->pci_irqmask = PCI_INT_RISC_RD_BERRINT | PCI_INT_RISC_WR_BERRINT |
			    PCI_INT_BRDG_BERRINT | PCI_INT_SRC_DMA_BERRINT |
			    PCI_INT_DST_DMA_BERRINT | PCI_INT_IPB_DMA_BERRINT;
	mutex_init(&core->lock);

	core->nr = nr;
	sprintf(core->name, "cx88[%d]", core->nr);
	if (0 != cx88_get_resources(core, pci)) {
		kfree(core);
		return NULL;
	}

	/* PCI stuff */
	cx88_pci_quirks(core->name, pci);
	core->lmmio = ioremap(pci_resource_start(pci, 0),
			      pci_resource_len(pci, 0));
	core->bmmio = (u8 __iomem *)core->lmmio;

	/* board config */
	core->boardnr = UNSET;
	if (card[core->nr] < ARRAY_SIZE(cx88_boards))
		core->boardnr = card[core->nr];
	for (i = 0; UNSET == core->boardnr && i < ARRAY_SIZE(cx88_subids); i++)
		if (pci->subsystem_vendor == cx88_subids[i].subvendor &&
		    pci->subsystem_device == cx88_subids[i].subdevice)
			core->boardnr = cx88_subids[i].card;
	if (UNSET == core->boardnr) {
		core->boardnr = CX88_BOARD_UNKNOWN;
		cx88_card_list(core, pci);
	}

	memcpy(&core->board, &cx88_boards[core->boardnr], sizeof(core->board));

	printk(KERN_INFO "%s: subsystem: %04x:%04x, board: %s [card=%d,%s]\n",
		core->name,pci->subsystem_vendor,
		pci->subsystem_device, core->board.name,
		core->boardnr, card[core->nr] == core->boardnr ?
		"insmod option" : "autodetected");

	if (tuner[core->nr] != UNSET)
		core->board.tuner_type = tuner[core->nr];
	if (radio[core->nr] != UNSET)
		core->board.radio_type = radio[core->nr];

	printk(KERN_INFO "%s: TV tuner type %d, Radio tuner type %d\n",
	       core->name, core->board.tuner_type, core->board.radio_type);

	/* init hardware */
	cx88_reset(core);
	cx88_card_setup_pre_i2c(core);
	cx88_i2c_init(core, pci);
	cx88_call_i2c_clients (core, TUNER_SET_STANDBY, NULL);
	cx88_card_setup(core);
	cx88_ir_init(core, pci);

	return core;
}

/* ------------------------------------------------------------------ */

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 * kate: eol "unix"; indent-width 3; remove-trailing-space on; replace-trailing-space-save on; tab-width 8; replace-tabs off; space-indent off; mixed-indent off
 */

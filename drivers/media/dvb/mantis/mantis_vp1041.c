/*
	Mantis VP-1041 driver

	Copyright (C) Manu Abraham (abraham.manu@gmail.com)

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

#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/interrupt.h>

#include "dmxdev.h"
#include "dvbdev.h"
#include "dvb_demux.h"
#include "dvb_frontend.h"
#include "dvb_net.h"

#include "mantis_common.h"
#include "mantis_ioc.h"
#include "mantis_dvb.h"
#include "mantis_vp1041.h"
#include "stb0899_reg.h"
#include "stb0899_drv.h"
#include "stb0899_cfg.h"
#include "stb6100_cfg.h"
#include "stb6100.h"
#include "lnbp21.h"

#define MANTIS_MODEL_NAME	"VP-1041"
#define MANTIS_DEV_TYPE		"DSS/DVB-S/DVB-S2"

static const struct stb0899_s1_reg vp1041_stb0899_s1_init_1[] = {

	/* 0x0000000b, *//* SYSREG */
	{ STB0899_DEV_ID		, 0x30 },
	{ STB0899_DISCNTRL1		, 0x32 },
	{ STB0899_DISCNTRL2     	, 0x80 },
	{ STB0899_DISRX_ST0     	, 0x04 },
	{ STB0899_DISRX_ST1     	, 0x00 },
	{ STB0899_DISPARITY     	, 0x00 },
	{ STB0899_DISFIFO       	, 0x00 },
	{ STB0899_DISSTATUS		, 0x20 },
	{ STB0899_DISF22        	, 0x99 },
	{ STB0899_DISF22RX      	, 0xa8 },
	/* SYSREG ? */
	{ STB0899_ACRPRESC      	, 0x11 },
	{ STB0899_ACRDIV1       	, 0x0a },
	{ STB0899_ACRDIV2       	, 0x05 },
	{ STB0899_DACR1         	, 0x00 },
	{ STB0899_DACR2         	, 0x00 },
	{ STB0899_OUTCFG        	, 0x00 },
	{ STB0899_MODECFG       	, 0x00 },
	{ STB0899_IRQSTATUS_3		, 0xfe },
	{ STB0899_IRQSTATUS_2		, 0x03 },
	{ STB0899_IRQSTATUS_1		, 0x7c },
	{ STB0899_IRQSTATUS_0		, 0xf4 },
	{ STB0899_IRQMSK_3      	, 0xf3 },
	{ STB0899_IRQMSK_2      	, 0xfc },
	{ STB0899_IRQMSK_1      	, 0xff },
	{ STB0899_IRQMSK_0		, 0xff },
	{ STB0899_IRQCFG		, 0x00 },
	{ STB0899_I2CCFG        	, 0x88 },
	{ STB0899_I2CRPT        	, 0x58 },
	{ STB0899_IOPVALUE5		, 0x00 },
	{ STB0899_IOPVALUE4		, 0x33 },
	{ STB0899_IOPVALUE3		, 0x6d },
	{ STB0899_IOPVALUE2		, 0x90 },
	{ STB0899_IOPVALUE1		, 0x60 },
	{ STB0899_IOPVALUE0		, 0x00 },
	{ STB0899_GPIO00CFG     	, 0x82 },
	{ STB0899_GPIO01CFG     	, 0x82 },
	{ STB0899_GPIO02CFG     	, 0x82 },
	{ STB0899_GPIO03CFG     	, 0x82 },
	{ STB0899_GPIO04CFG     	, 0x82 },
	{ STB0899_GPIO05CFG     	, 0x82 },
	{ STB0899_GPIO06CFG     	, 0x82 },
	{ STB0899_GPIO07CFG     	, 0x82 },
	{ STB0899_GPIO08CFG     	, 0x82 },
	{ STB0899_GPIO09CFG     	, 0x82 },
	{ STB0899_GPIO10CFG     	, 0x82 },
	{ STB0899_GPIO11CFG     	, 0x82 },
	{ STB0899_GPIO12CFG     	, 0x82 },
	{ STB0899_GPIO13CFG     	, 0x82 },
	{ STB0899_GPIO14CFG     	, 0x82 },
	{ STB0899_GPIO15CFG     	, 0x82 },
	{ STB0899_GPIO16CFG     	, 0x82 },
	{ STB0899_GPIO17CFG     	, 0x82 },
	{ STB0899_GPIO18CFG     	, 0x82 },
	{ STB0899_GPIO19CFG     	, 0x82 },
	{ STB0899_GPIO20CFG     	, 0x82 },
	{ STB0899_SDATCFG       	, 0xb8 },
	{ STB0899_SCLTCFG       	, 0xba },
	{ STB0899_AGCRFCFG      	, 0x1c }, /* 0x11 */
	{ STB0899_GPIO22        	, 0x82 }, /* AGCBB2CFG */
	{ STB0899_GPIO21        	, 0x91 }, /* AGCBB1CFG */
	{ STB0899_DIRCLKCFG     	, 0x82 },
	{ STB0899_CLKOUT27CFG   	, 0x7e },
	{ STB0899_STDBYCFG      	, 0x82 },
	{ STB0899_CS0CFG        	, 0x82 },
	{ STB0899_CS1CFG        	, 0x82 },
	{ STB0899_DISEQCOCFG    	, 0x20 },
	{ STB0899_GPIO32CFG		, 0x82 },
	{ STB0899_GPIO33CFG		, 0x82 },
	{ STB0899_GPIO34CFG		, 0x82 },
	{ STB0899_GPIO35CFG		, 0x82 },
	{ STB0899_GPIO36CFG		, 0x82 },
	{ STB0899_GPIO37CFG		, 0x82 },
	{ STB0899_GPIO38CFG		, 0x82 },
	{ STB0899_GPIO39CFG		, 0x82 },
	{ STB0899_NCOARSE       	, 0x17 }, /* 0x15 = 27 Mhz Clock, F/3 = 198MHz, F/6 = 99MHz */
	{ STB0899_SYNTCTRL      	, 0x02 }, /* 0x00 = CLK from CLKI, 0x02 = CLK from XTALI */
	{ STB0899_FILTCTRL      	, 0x00 },
	{ STB0899_SYSCTRL       	, 0x01 },
	{ STB0899_STOPCLK1      	, 0x20 },
	{ STB0899_STOPCLK2      	, 0x00 },
	{ STB0899_INTBUFSTATUS		, 0x00 },
	{ STB0899_INTBUFCTRL    	, 0x0a },
	{ 0xffff			, 0xff },
};

static const struct stb0899_s1_reg vp1041_stb0899_s1_init_3[] = {
	{ STB0899_DEMOD         	, 0x00 },
	{ STB0899_RCOMPC        	, 0xc9 },
	{ STB0899_AGC1CN        	, 0x01 },
	{ STB0899_AGC1REF       	, 0x10 },
	{ STB0899_RTC	        	, 0x23 },
	{ STB0899_TMGCFG        	, 0x4e },
	{ STB0899_AGC2REF       	, 0x34 },
	{ STB0899_TLSR          	, 0x84 },
	{ STB0899_CFD           	, 0xf7 },
	{ STB0899_ACLC	        	, 0x87 },
	{ STB0899_BCLC          	, 0x94 },
	{ STB0899_EQON          	, 0x41 },
	{ STB0899_LDT           	, 0xf1 },
	{ STB0899_LDT2          	, 0xe3 },
	{ STB0899_EQUALREF      	, 0xb4 },
	{ STB0899_TMGRAMP       	, 0x10 },
	{ STB0899_TMGTHD        	, 0x30 },
	{ STB0899_IDCCOMP		, 0xfd },
	{ STB0899_QDCCOMP		, 0xff },
	{ STB0899_POWERI		, 0x0c },
	{ STB0899_POWERQ		, 0x0f },
	{ STB0899_RCOMP			, 0x6c },
	{ STB0899_AGCIQIN		, 0x80 },
	{ STB0899_AGC2I1		, 0x06 },
	{ STB0899_AGC2I2		, 0x00 },
	{ STB0899_TLIR			, 0x30 },
	{ STB0899_RTF			, 0x7f },
	{ STB0899_DSTATUS		, 0x00 },
	{ STB0899_LDI			, 0xbc },
	{ STB0899_CFRM			, 0xea },
	{ STB0899_CFRL			, 0x31 },
	{ STB0899_NIRM			, 0x2b },
	{ STB0899_NIRL			, 0x80 },
	{ STB0899_ISYMB			, 0x1d },
	{ STB0899_QSYMB			, 0xa6 },
	{ STB0899_SFRH          	, 0x2f },
	{ STB0899_SFRM          	, 0x68 },
	{ STB0899_SFRL          	, 0x40 },
	{ STB0899_SFRUPH        	, 0x2f },
	{ STB0899_SFRUPM        	, 0x68 },
	{ STB0899_SFRUPL        	, 0x40 },
	{ STB0899_EQUAI1		, 0x02 },
	{ STB0899_EQUAQ1		, 0xff },
	{ STB0899_EQUAI2		, 0x04 },
	{ STB0899_EQUAQ2		, 0x05 },
	{ STB0899_EQUAI3		, 0x02 },
	{ STB0899_EQUAQ3		, 0xfd },
	{ STB0899_EQUAI4		, 0x03 },
	{ STB0899_EQUAQ4		, 0x07 },
	{ STB0899_EQUAI5		, 0x08 },
	{ STB0899_EQUAQ5		, 0xf5 },
	{ STB0899_DSTATUS2		, 0x00 },
	{ STB0899_VSTATUS       	, 0x00 },
	{ STB0899_VERROR		, 0x86 },
	{ STB0899_IQSWAP		, 0x2a },
	{ STB0899_ECNT1M		, 0x00 },
	{ STB0899_ECNT1L		, 0x00 },
	{ STB0899_ECNT2M		, 0x00 },
	{ STB0899_ECNT2L		, 0x00 },
	{ STB0899_ECNT3M		, 0x0a },
	{ STB0899_ECNT3L		, 0xad },
	{ STB0899_FECAUTO1      	, 0x06 },
	{ STB0899_FECM	        	, 0x01 },
	{ STB0899_VTH12         	, 0xb0 },
	{ STB0899_VTH23         	, 0x7a },
	{ STB0899_VTH34	        	, 0x58 },
	{ STB0899_VTH56         	, 0x38 },
	{ STB0899_VTH67         	, 0x34 },
	{ STB0899_VTH78         	, 0x24 },
	{ STB0899_PRVIT         	, 0xff },
	{ STB0899_VITSYNC       	, 0x19 },
	{ STB0899_RSULC         	, 0xb1 }, /* DVB = 0xb1, DSS = 0xa1 */
	{ STB0899_TSULC         	, 0x42 },
	{ STB0899_RSLLC         	, 0x41 },
	{ STB0899_TSLPL	        	, 0x12 },
	{ STB0899_TSCFGH        	, 0x0c },
	{ STB0899_TSCFGM        	, 0x00 },
	{ STB0899_TSCFGL        	, 0x00 },
	{ STB0899_TSOUT			, 0x69 }, /* 0x0d for CAM */
	{ STB0899_RSSYNCDEL     	, 0x00 },
	{ STB0899_TSINHDELH     	, 0x02 },
	{ STB0899_TSINHDELM		, 0x00 },
	{ STB0899_TSINHDELL		, 0x00 },
	{ STB0899_TSLLSTKM		, 0x1b },
	{ STB0899_TSLLSTKL		, 0xb3 },
	{ STB0899_TSULSTKM		, 0x00 },
	{ STB0899_TSULSTKL		, 0x00 },
	{ STB0899_PCKLENUL		, 0xbc },
	{ STB0899_PCKLENLL		, 0xcc },
	{ STB0899_RSPCKLEN		, 0xbd },
	{ STB0899_TSSTATUS		, 0x90 },
	{ STB0899_ERRCTRL1      	, 0xb6 },
	{ STB0899_ERRCTRL2      	, 0x95 },
	{ STB0899_ERRCTRL3      	, 0x8d },
	{ STB0899_DMONMSK1		, 0x27 },
	{ STB0899_DMONMSK0		, 0x03 },
	{ STB0899_DEMAPVIT      	, 0x5c },
	{ STB0899_PLPARM		, 0x19 },
	{ STB0899_PDELCTRL      	, 0x48 },
	{ STB0899_PDELCTRL2     	, 0x00 },
	{ STB0899_BBHCTRL1      	, 0x00 },
	{ STB0899_BBHCTRL2      	, 0x00 },
	{ STB0899_HYSTTHRESH    	, 0x77 },
	{ STB0899_MATCSTM		, 0x00 },
	{ STB0899_MATCSTL		, 0x00 },
	{ STB0899_UPLCSTM		, 0x00 },
	{ STB0899_UPLCSTL		, 0x00 },
	{ STB0899_DFLCSTM		, 0x00 },
	{ STB0899_DFLCSTL		, 0x00 },
	{ STB0899_SYNCCST		, 0x00 },
	{ STB0899_SYNCDCSTM		, 0x00 },
	{ STB0899_SYNCDCSTL		, 0x00 },
	{ STB0899_ISI_ENTRY		, 0x00 },
	{ STB0899_ISI_BIT_EN		, 0x00 },
	{ STB0899_MATSTRM		, 0xf0 },
	{ STB0899_MATSTRL		, 0x02 },
	{ STB0899_UPLSTRM		, 0x45 },
	{ STB0899_UPLSTRL		, 0x60 },
	{ STB0899_DFLSTRM		, 0xe3 },
	{ STB0899_DFLSTRL		, 0x00 },
	{ STB0899_SYNCSTR		, 0x47 },
	{ STB0899_SYNCDSTRM		, 0x05 },
	{ STB0899_SYNCDSTRL		, 0x18 },
	{ STB0899_CFGPDELSTATUS1	, 0x19 },
	{ STB0899_CFGPDELSTATUS2	, 0x2b },
	{ STB0899_BBFERRORM		, 0x00 },
	{ STB0899_BBFERRORL		, 0x01 },
	{ STB0899_UPKTERRORM		, 0x00 },
	{ STB0899_UPKTERRORL		, 0x00 },
	{ 0xffff			, 0xff },
};

struct stb0899_config vp1041_stb0899_config = {
	.init_dev		= vp1041_stb0899_s1_init_1,
	.init_s2_demod		= stb0899_s2_init_2,
	.init_s1_demod		= vp1041_stb0899_s1_init_3,
	.init_s2_fec		= stb0899_s2_init_4,
	.init_tst		= stb0899_s1_init_5,

	.demod_address 		= 0x68, /*  0xd0 >> 1 */

	.xtal_freq		= 27000000,
	.inversion		= IQ_SWAP_ON, /* 1 */

	.lo_clk			= 76500000,
	.hi_clk			= 99000000,

	.esno_ave		= STB0899_DVBS2_ESNO_AVE,
	.esno_quant		= STB0899_DVBS2_ESNO_QUANT,
	.avframes_coarse	= STB0899_DVBS2_AVFRAMES_COARSE,
	.avframes_fine		= STB0899_DVBS2_AVFRAMES_FINE,
	.miss_threshold		= STB0899_DVBS2_MISS_THRESHOLD,
	.uwp_threshold_acq	= STB0899_DVBS2_UWP_THRESHOLD_ACQ,
	.uwp_threshold_track	= STB0899_DVBS2_UWP_THRESHOLD_TRACK,
	.uwp_threshold_sof	= STB0899_DVBS2_UWP_THRESHOLD_SOF,
	.sof_search_timeout	= STB0899_DVBS2_SOF_SEARCH_TIMEOUT,

	.btr_nco_bits		= STB0899_DVBS2_BTR_NCO_BITS,
	.btr_gain_shift_offset	= STB0899_DVBS2_BTR_GAIN_SHIFT_OFFSET,
	.crl_nco_bits		= STB0899_DVBS2_CRL_NCO_BITS,
	.ldpc_max_iter		= STB0899_DVBS2_LDPC_MAX_ITER,

	.tuner_get_frequency	= stb6100_get_frequency,
	.tuner_set_frequency	= stb6100_set_frequency,
	.tuner_set_bandwidth	= stb6100_set_bandwidth,
	.tuner_get_bandwidth	= stb6100_get_bandwidth,
	.tuner_set_rfsiggain	= NULL,
};

struct stb6100_config vp1041_stb6100_config = {
	.tuner_address	= 0x60,
	.refclock	= 27000000,
};

static int vp1041_frontend_init(struct mantis_pci *mantis, struct dvb_frontend *fe)
{
	struct i2c_adapter *adapter	= &mantis->adapter;

	int err = 0;

	err = mantis_frontend_power(mantis, POWER_ON);
	if (err == 0) {
		mantis_frontend_soft_reset(mantis);
		msleep(250);
		mantis->fe = stb0899_attach(&vp1041_stb0899_config, adapter);
		if (mantis->fe) {
			dprintk(MANTIS_ERROR, 1,
				"found STB0899 DVB-S/DVB-S2 frontend @0x%02x",
				vp1041_stb0899_config.demod_address);

			if (stb6100_attach(mantis->fe, &vp1041_stb6100_config, adapter)) {
				if (!lnbp21_attach(mantis->fe, adapter, 0, 0))
					dprintk(MANTIS_ERROR, 1, "No LNBP21 found!");
			}
		} else {
			return -EREMOTEIO;
		}
	} else {
		dprintk(MANTIS_ERROR, 1, "Frontend on <%s> POWER ON failed! <%d>",
			adapter->name,
			err);

		return -EIO;
	}


	dprintk(MANTIS_ERROR, 1, "Done!");

	return 0;
}

struct mantis_hwconfig vp1041_config = {
	.model_name	= MANTIS_MODEL_NAME,
	.dev_type	= MANTIS_DEV_TYPE,
	.ts_size	= MANTIS_TS_188,

	.baud_rate	= MANTIS_BAUD_9600,
	.parity		= MANTIS_PARITY_NONE,
	.bytes		= 0,

	.frontend_init	= vp1041_frontend_init,
	.power		= GPIF_A12,
	.reset		= GPIF_A13,
};

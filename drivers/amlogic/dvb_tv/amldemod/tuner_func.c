#include <linux/kernel.h>
#include <linux/i2c.h>

//#include <dvb_frontend.h>

#include "aml_demod.h"
#include "demod_func.h"


//#include "mxl/MxL5007_API.h"
//#include "../tuners/si2176_func.h"

//static int configure_first=-1;
#if 0
static int set_tuner_DCT7070X(struct aml_demod_sta *demod_sta,
			      struct aml_demod_i2c *adap)
{
    int ret = 0;
    unsigned char data[6];
    unsigned long ch_freq, ftmp;
    struct i2c_msg msg;

    ch_freq = demod_sta->ch_freq;
    //printk("Set Tuner DCT7070X to %ld kHz\n", ch_freq);

    ftmp = (ch_freq+36125)*10/625;  // ftmp=(ch_freq+GX_IF_FREQUENCY)
    data[0] = ftmp>>8&0xff;
    data[1] = ftmp&0xff;
    data[2] = 0x8b;              // 62.5 kHz

    if (ch_freq < 153000)
	data[3] = 0x01;
    else if (ch_freq < 430000)
	data[3] = 0x06;
    else
	data[3] = 0x0c;

    data[4] = 0xc3;

    msg.addr = adap->addr;
    msg.flags = 0; // I2C_M_IGNORE_NAK;
    msg.len = 5;
    msg.buf = data;

    ret = am_demod_i2c_xfer(adap, &msg, 1);

    return ret;
}
#endif

/*int tuner_set_ch(struct aml_demod_sta *demod_sta, struct aml_demod_i2c *adap)
{
    int ret = 0;
    printk("Set tuner: 1 is DCT70707, 1 is Mxl5007, 3 is FJ2207, 4 is TD1316, 5 is XUGUAN DMTX-6A, 6 is Si2176\n");
    switch (adap->tuner) {
    case 0 : // NULL
	printk("Warning: NULL Tuner\n");
	break;

    case 1 : // DCT
	ret = set_tuner_DCT7070X(demod_sta, adap);
	break;

    case 2 : // Maxliner
	ret = set_tuner_MxL5007(demod_sta, adap);
	break;

    case 3 : // NXP
	ret = set_tuner_fj2207(demod_sta, adap);
	break;

    case 4 : // TD1316
	ret = set_tuner_TD1316(demod_sta, adap);
	break;

    case 5 :
	ret = set_tuner_xuguan(demod_sta, adap);
	break;

    case 6 : //Si2176
	ret = set_tuner_si2176(demod_sta, adap);
	break;

    default :
	return -1;
    }

    return 0;
}*/
#if (defined CONFIG_AM_SI2176)
extern	int si2176_get_strength(void);
#elif (defined CONFIG_AM_SI2177)
extern	int si2177_get_strength(void);
#endif

int tuner_get_ch_power(struct aml_fe_dev *adap)
{
//    int ret = 0;
	int strength=0;

#if (defined CONFIG_AM_SI2176)
	 strength=si2176_get_strength();
#elif (defined CONFIG_AM_SI2177)
	 strength=si2177_get_strength();
#endif

	 return strength;
}

struct dvb_tuner_info * tuner_get_info( int type, int mode)
{
	/*type :  0-NULL, 1-DCT7070, 2-Maxliner, 3-FJ2207, 4-TD1316*/
	/*mode: 0-DVBC 1-DVBT */
	static struct dvb_tuner_info tinfo_null = {};

	static struct dvb_tuner_info tinfo_MXL5003S[2] = {
		[1] = {/*DVBT*/
			.name = "Maxliner",
			.frequency_min = 44000000,
			.frequency_max = 885000000,
		}
	};
	static struct dvb_tuner_info tinfo_FJ2207[2] = {
		[0] = {/*DVBC*/
			.name = "FJ2207",
			.frequency_min = 54000000,
			.frequency_max = 870000000,
		},
		[1] = {/*DVBT*/
			.name = "FJ2207",
			.frequency_min = 174000000,
			.frequency_max = 864000000,
		},
	};
	static struct dvb_tuner_info tinfo_DCT7070[2] = {
		[0] = {/*DVBC*/
			.name = "DCT7070",
			.frequency_min = 51000000,
			.frequency_max = 860000000,
		}
	};
	static struct dvb_tuner_info tinfo_TD1316[2] = {
		[1] = {/*DVBT*/
			.name = "TD1316",
			.frequency_min = 51000000,
			.frequency_max = 858000000,
		}
	};
	static struct dvb_tuner_info tinfo_SI2176[2] = {
		[0] = {/*DVBC*/
//#error please add SI2176 code
			.name = "SI2176",
			.frequency_min = 51000000,
			.frequency_max = 860000000,
		}
	};

	struct dvb_tuner_info *tinfo[] = {
		&tinfo_null,
		tinfo_DCT7070,
		tinfo_MXL5003S,
		tinfo_FJ2207,
		tinfo_TD1316,
		tinfo_SI2176
	};

	if((type<0)||(type>4)||(mode<0)||(mode>1))
		return tinfo[0];

	return &tinfo[type][mode];
}

struct agc_power_tab * tuner_get_agc_power_table(int type) {
	/*type :  0-NULL, 1-DCT7070, 2-Maxliner, 3-FJ2207, 4-TD1316*/
	static int calcE_FJ2207[31]={87,118,138,154,172,197,245,273,292,312,
						 327,354,406,430,448,464,481,505,558,583,
						 599,616,632,653,698,725,745,762,779,801,
						 831};
	static int calcE_Maxliner[79]={543,552,562,575,586,596,608,618,627,635,
						    645,653,662,668,678,689,696,705,715,725,
						    733,742,752,763,769,778,789,800,807,816,
						    826,836,844,854,864,874,884,894,904,913,
						    923,932,942,951,961,970,980,990,1000,1012,
						    1022,1031,1040,1049,1059,1069,1079,1088,1098,1107,
						    1115,1123,1132,1140,1148,1157,1165,1173,1179,1186,
						    1192,1198,1203,1208,1208,1214,1217,1218,1220};

	static struct agc_power_tab power_tab[] = {
		[0] = {"null", 0, 0, NULL},
		[1] = {
			.name="DCT7070",
			.level=0,
			.ncalcE=0,
			.calcE=NULL,
		},
		[2] = {
			.name="Maxlear",
			.level=-22,
			.ncalcE=sizeof(calcE_Maxliner)/sizeof(int),
			.calcE=calcE_Maxliner,
		},
		[3] = {
			.name="FJ2207",
			.level=-62,
			.ncalcE=sizeof(calcE_FJ2207)/sizeof(int),
			.calcE=calcE_FJ2207,
		},
		[4] = {
			.name="TD1316",
			.level=0,
			.ncalcE=0,
			.calcE=NULL,
		},
	};

	if(type>=2 && type<=3)
		return &power_tab[type];
	else
		return &power_tab[3];
};


int  agc_power_to_dbm(int agc_gain, int ad_power, int offset, int tuner)
{
	struct agc_power_tab *ptab = tuner_get_agc_power_table(tuner);
	int est_rf_power;
	int j;

	for(j=0; j<ptab->ncalcE; j++)
		if(agc_gain<=ptab->calcE[j])
			break;

	est_rf_power = ptab->level - j - (ad_power>>4) + 12 + offset;

	return (est_rf_power);
}


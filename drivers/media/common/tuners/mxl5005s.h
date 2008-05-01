/*
 * For the Realtek RTL chip RTL2831U
 * Realtek Release Date: 2008-03-14, ver 080314
 * Realtek version RTL2831 Linux driver version 080314
 * ver 080314
 *
 * for linux kernel version 2.6.21.4 - 2.6.22-14
 * support MXL5005s and MT2060 tuners (support tuner auto-detecting)
 * support two IR types -- RC5 and NEC
 *
 * Known boards with Realtek RTL chip RTL2821U
 *    Freecom USB stick 14aa:0160 (version 4)
 *    Conceptronic CTVDIGRCU
 *
 * Copyright (c) 2008 Realtek
 * Copyright (c) 2008 Jan Hoogenraad, Barnaby Shearer, Andy Hasper
 * This code is placed under the terms of the GNU General Public License
 *
 * Released by Realtek under GPLv2.
 * Thanks to Realtek for a lot of support we received !
 *
 *  Revision: 080314 - original version
 */


#ifndef __MXL5005S_H
#define __MXL5005S_H

#include <linux/dvb/frontend.h>

/* IF frequency */
enum IF_FREQ_HZ
{
	IF_FREQ_4570000HZ  =  4570000,                  ///<   IF frequency =   4.57 MHz
	IF_FREQ_4571429HZ  =  4571429,                  ///<   IF frequency =  4.571 MHz
	IF_FREQ_5380000HZ  =  5380000,                  ///<   IF frequency =   5.38 MHz
	IF_FREQ_36000000HZ = 36000000,                  ///<   IF frequency = 36.000 MHz
	IF_FREQ_36125000HZ = 36125000,                  ///<   IF frequency = 36.125 MHz
	IF_FREQ_36166667HZ = 36166667,                  ///<   IF frequency = 36.167 MHz
	IF_FREQ_44000000HZ = 44000000,                  ///<   IF frequency = 44.000 MHz
};

/* Crystal frequency */
enum CRYSTAL_FREQ_HZ
{
	CRYSTAL_FREQ_4000000HZ  =  4000000,                     ///<   Crystal frequency =  4.0 MHz
	CRYSTAL_FREQ_16000000HZ = 16000000,                     ///<   Crystal frequency = 16.0 MHz
	CRYSTAL_FREQ_25000000HZ = 25000000,                     ///<   Crystal frequency = 25.0 MHz
	CRYSTAL_FREQ_28800000HZ = 28800000,                     ///<   Crystal frequency = 28.8 MHz
};

struct mxl5005s_config
{
	u8 i2c_address;

	/* Stuff I don't know what to do with */
	u8 AgcMasterByte;
};

#if defined(CONFIG_MEDIA_TUNER_MXL5005S) || (defined(CONFIG_MEDIA_TUNER_MXL5005S_MODULE) && defined(MODULE))
extern struct dvb_frontend *mxl5005s_attach(struct dvb_frontend *fe,
					    struct i2c_adapter *i2c,
					    struct mxl5005s_config *config);
#else
static inline struct dvb_frontend *mxl5005s_attach(struct dvb_frontend *fe,
					    struct i2c_adapter *i2c,
					    struct mxl5005s_config *config)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif /* CONFIG_DVB_TUNER_MXL5005S */

#endif /* __MXL5005S_H */


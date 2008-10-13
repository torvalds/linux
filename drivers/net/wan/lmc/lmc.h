#ifndef _LMC_H_
#define _LMC_H_

#include "lmc_var.h"

/*
 * prototypes for everyone
 */
int lmc_probe(struct net_device * dev);
unsigned lmc_mii_readreg(lmc_softc_t * const sc, unsigned
      			  devaddr, unsigned regno);
void lmc_mii_writereg(lmc_softc_t * const sc, unsigned devaddr,
			       unsigned regno, unsigned data);
void lmc_led_on(lmc_softc_t * const, u32);
void lmc_led_off(lmc_softc_t * const, u32);
unsigned lmc_mii_readreg(lmc_softc_t * const, unsigned, unsigned);
void lmc_mii_writereg(lmc_softc_t * const, unsigned, unsigned, unsigned);
void lmc_gpio_mkinput(lmc_softc_t * const sc, u32 bits);
void lmc_gpio_mkoutput(lmc_softc_t * const sc, u32 bits);

int lmc_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd);

extern lmc_media_t lmc_ds3_media;
extern lmc_media_t lmc_ssi_media;
extern lmc_media_t lmc_t1_media;
extern lmc_media_t lmc_hssi_media;

#ifdef _DBG_EVENTLOG
static void lmcEventLog(u32 EventNum, u32 arg2, u32 arg3);
#endif

#endif

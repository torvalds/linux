/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __dhd_chip_info_h__
#define __dhd_chip_info_h__

#ifdef LOAD_DHD_WITH_FW_ALIVE

#include <typedefs.h>

#define chip_info_dump 0

#define ai_core_43341 7
#define oob_router_43341 0x18108000

#define ai_core_43430 6
#define oob_router_43430 0x18107000

#define ai_core_43012 8
#define oob_router_43012 0x1810c000

#define FW_ALIVE_MAGIC 0x151515

extern int alive;
extern int card_dev;
extern int card_rev;

extern uint32	bcm43341_coreid[];
extern uint32	bcm43341_coresba[];
extern uint32	bcm43341_coresba_size[];
extern uint32	bcm43341_coresba2_size[];
extern uint32	bcm43341_wrapba[];
extern uint32	bcm43341_cia[];
extern uint32	bcm43341_cib[];

extern uint32	bcm43430_coreid[];
extern uint32	bcm43430_coresba[];
extern uint32	bcm43430_coresba_size[];
extern uint32	bcm43430_wrapba[];
extern uint32	bcm43430_cia[];
extern uint32	bcm43430_cib[];
extern uint32	bcm43436_cib[];

extern int	sii_pub_43341[];
extern int	sii_pub_43430[];
extern int	sii_pub_43436[];

extern uint32 bcm43012_coreid[];
extern uint32 bcm43012_coresba[];
extern uint32 bcm43012_coresba_size[];
extern uint32 bcm43012_wrapba[];
extern uint32 bcm43012_cia[];
extern uint32 bcm43012_cib[];
extern int sii_pub_43012[];

#endif	//LOAD_DHD_WITH_FW_ALIVE

#endif	//__dhd_chip_info_h__

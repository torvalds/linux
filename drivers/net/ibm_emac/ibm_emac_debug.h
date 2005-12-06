/*
 * drivers/net/ibm_emac/ibm_ocp_debug.h
 *
 * Driver for PowerPC 4xx on-chip ethernet controller, debug print routines.
 *
 * Copyright (c) 2004, 2005 Zultys Technologies
 * Eugene Surovegin <eugene.surovegin@zultys.com> or <ebs@ebshome.net>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */
#ifndef __IBM_EMAC_DEBUG_H_
#define __IBM_EMAC_DEBUG_H_

#include <linux/config.h>
#include <linux/init.h>
#include "ibm_emac_core.h"
#include "ibm_emac_mal.h"

#if defined(CONFIG_IBM_EMAC_DEBUG)
void emac_dbg_register(int idx, struct ocp_enet_private *dev);
void mal_dbg_register(int idx, struct ibm_ocp_mal *mal);
int emac_init_debug(void) __init;
void emac_fini_debug(void) __exit;
void emac_dbg_dump_all(void);
# define DBG_LEVEL		1
#else
# define emac_dbg_register(x,y) ((void)0)
# define mal_dbg_register(x,y)	((void)0)
# define emac_init_debug()	((void)0)
# define emac_fini_debug()	((void)0)
# define emac_dbg_dump_all()	((void)0)
# define DBG_LEVEL		0
#endif

#if DBG_LEVEL > 0
#  define DBG(f,x...)		printk("emac" f, ##x)
#  define MAL_DBG(f,x...)	printk("mal" f, ##x)
#  define ZMII_DBG(f,x...)	printk("zmii" f, ##x)
#  define RGMII_DBG(f,x...)	printk("rgmii" f, ##x)
#  define NL			"\n"
#else
#  define DBG(f,x...)		((void)0)
#  define MAL_DBG(f,x...)	((void)0)
#  define ZMII_DBG(f,x...)	((void)0)
#  define RGMII_DBG(f,x...)	((void)0)
#endif
#if DBG_LEVEL > 1
#  define DBG2(f,x...) 		DBG(f, ##x)
#  define MAL_DBG2(f,x...) 	MAL_DBG(f, ##x)
#  define ZMII_DBG2(f,x...) 	ZMII_DBG(f, ##x)
#  define RGMII_DBG2(f,x...) 	RGMII_DBG(f, ##x)
#else
#  define DBG2(f,x...) 		((void)0)
#  define MAL_DBG2(f,x...) 	((void)0)
#  define ZMII_DBG2(f,x...) 	((void)0)
#  define RGMII_DBG2(f,x...) 	((void)0)
#endif

#endif				/* __IBM_EMAC_DEBUG_H_ */

/*
 * drivers/net/ethernet/ibm/emac/debug.h
 *
 * Driver for PowerPC 4xx on-chip ethernet controller, debug print routines.
 *
 * Copyright 2007 Benjamin Herrenschmidt, IBM Corp.
 *                <benh@kernel.crashing.org>
 *
 * Based on the arch/ppc version of the driver:
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
#ifndef __IBM_NEWEMAC_DEBUG_H
#define __IBM_NEWEMAC_DEBUG_H

#include <linux/init.h>

#include "core.h"

#if defined(CONFIG_IBM_EMAC_DEBUG)

struct emac_instance;
struct mal_instance;

void emac_dbg_register(struct emac_instance *dev);
void emac_dbg_unregister(struct emac_instance *dev);
void mal_dbg_register(struct mal_instance *mal);
void mal_dbg_unregister(struct mal_instance *mal);
int emac_init_debug(void) __init;
void emac_fini_debug(void) __exit;
void emac_dbg_dump_all(void);

# define DBG_LEVEL		1

#else

# define emac_dbg_register(x)	do { } while(0)
# define emac_dbg_unregister(x)	do { } while(0)
# define mal_dbg_register(x)	do { } while(0)
# define mal_dbg_unregister(x)	do { } while(0)
# define emac_init_debug()	do { } while(0)
# define emac_fini_debug()	do { } while(0)
# define emac_dbg_dump_all()	do { } while(0)

# define DBG_LEVEL		0

#endif

#define EMAC_DBG(d, name, fmt, arg...) \
	printk(KERN_DEBUG #name "%s: " fmt, d->ofdev->dev.of_node->full_name, ## arg)

#if DBG_LEVEL > 0
#  define DBG(d,f,x...)		EMAC_DBG(d, emac, f, ##x)
#  define MAL_DBG(d,f,x...)	EMAC_DBG(d, mal, f, ##x)
#  define ZMII_DBG(d,f,x...)	EMAC_DBG(d, zmii, f, ##x)
#  define RGMII_DBG(d,f,x...)	EMAC_DBG(d, rgmii, f, ##x)
#  define NL			"\n"
#else
#  define DBG(f,x...)		((void)0)
#  define MAL_DBG(d,f,x...)	((void)0)
#  define ZMII_DBG(d,f,x...)	((void)0)
#  define RGMII_DBG(d,f,x...)	((void)0)
#endif
#if DBG_LEVEL > 1
#  define DBG2(d,f,x...) 	DBG(d,f, ##x)
#  define MAL_DBG2(d,f,x...) 	MAL_DBG(d,f, ##x)
#  define ZMII_DBG2(d,f,x...) 	ZMII_DBG(d,f, ##x)
#  define RGMII_DBG2(d,f,x...) 	RGMII_DBG(d,f, ##x)
#else
#  define DBG2(f,x...) 		((void)0)
#  define MAL_DBG2(d,f,x...) 	((void)0)
#  define ZMII_DBG2(d,f,x...) 	((void)0)
#  define RGMII_DBG2(d,f,x...) 	((void)0)
#endif

#endif /* __IBM_NEWEMAC_DEBUG_H */

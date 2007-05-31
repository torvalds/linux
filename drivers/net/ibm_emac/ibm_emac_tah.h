/*
 * drivers/net/ibm_emac/ibm_emac_tah.h
 *
 * Driver for PowerPC 4xx on-chip ethernet controller, TAH support.
 *
 * Copyright 2004 MontaVista Software, Inc.
 * Matt Porter <mporter@kernel.crashing.org>
 *
 * Copyright (c) 2005 Eugene Surovegin <ebs@ebshome.net>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef _IBM_EMAC_TAH_H
#define _IBM_EMAC_TAH_H

#include <linux/init.h>
#include <asm/ocp.h>

/* TAH */
struct tah_regs {
	u32 revid;
	u32 pad[3];
	u32 mr;
	u32 ssr0;
	u32 ssr1;
	u32 ssr2;
	u32 ssr3;
	u32 ssr4;
	u32 ssr5;
	u32 tsr;
};

/* TAH engine */
#define TAH_MR_CVR		0x80000000
#define TAH_MR_SR		0x40000000
#define TAH_MR_ST_256		0x01000000
#define TAH_MR_ST_512		0x02000000
#define TAH_MR_ST_768		0x03000000
#define TAH_MR_ST_1024		0x04000000
#define TAH_MR_ST_1280		0x05000000
#define TAH_MR_ST_1536		0x06000000
#define TAH_MR_TFS_16KB		0x00000000
#define TAH_MR_TFS_2KB		0x00200000
#define TAH_MR_TFS_4KB		0x00400000
#define TAH_MR_TFS_6KB		0x00600000
#define TAH_MR_TFS_8KB		0x00800000
#define TAH_MR_TFS_10KB		0x00a00000
#define TAH_MR_DTFP		0x00100000
#define TAH_MR_DIG		0x00080000

#ifdef CONFIG_IBM_EMAC_TAH
int tah_attach(void *emac) __init;

void __tah_fini(struct ocp_device *ocpdev);
static inline void tah_fini(struct ocp_device *ocpdev)
{
	if (ocpdev)
		__tah_fini(ocpdev);
}

void __tah_reset(struct ocp_device *ocpdev);
static inline void tah_reset(struct ocp_device *ocpdev)
{
	if (ocpdev)
		__tah_reset(ocpdev);
}

int __tah_get_regs_len(struct ocp_device *ocpdev);
static inline int tah_get_regs_len(struct ocp_device *ocpdev)
{
	return ocpdev ? __tah_get_regs_len(ocpdev) : 0;
}

void *tah_dump_regs(struct ocp_device *ocpdev, void *buf);
#else
# define tah_attach(x)		0
# define tah_fini(x)		((void)0)
# define tah_reset(x)		((void)0)
# define tah_get_regs_len(x)	0
# define tah_dump_regs(x,buf)	(buf)
#endif				/* !CONFIG_IBM_EMAC_TAH */

#endif				/* _IBM_EMAC_TAH_H */

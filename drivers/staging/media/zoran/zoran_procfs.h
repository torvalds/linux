/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Zoran zr36057/zr36067 PCI controller driver, for the
 * Pinnacle/Miro DC10/DC10+/DC30/DC30+, Iomega Buz, Linux
 * Media Labs LML33/LML33R10.
 *
 * This part handles card-specific data and detection
 *
 * Copyright (C) 2000 Serguei Miridonov <mirsev@cicese.mx>
 *
 * Currently maintained by:
 *   Ronald Bultje    <rbultje@ronald.bitfreak.net>
 *   Laurent Pinchart <laurent.pinchart@skynet.be>
 *   Mailinglist      <mjpeg-users@lists.sf.net>
 */
#ifndef __ZORAN_PROCFS_H__
#define __ZORAN_PROCFS_H__

extern int zoran_proc_init(struct zoran *zr);
extern void zoran_proc_cleanup(struct zoran *zr);

#endif				/* __ZORAN_PROCFS_H__ */

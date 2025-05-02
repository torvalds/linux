/*
 * Broadcom PCIe 1570 webcam driver
 *
 * Copyright (C) 2014 Patrik Jakobsson (patrik.r.jakobsson@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation.
 *
 */

#ifndef _FTHD_DDR_H
#define _FTHD_DDR_H

#define MEM_VERIFY_BASE		0x0 /* 0x1000000 */
#define MEM_VERIFY_NUM		128
#define MEM_VERIFY_NUM_FULL	(1 * 1024 * 1024)

int fthd_ddr_calibrate(struct fthd_private *dev_priv);
int fthd_ddr_verify_mem(struct fthd_private *dev_priv, u32 base, int count);

#endif
